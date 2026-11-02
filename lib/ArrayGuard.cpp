#include "ArrayGuard.h"

#include <isl/constraint.h>
#include <isl/ctx.h>
#include <isl/options.h>
#include <isl/set.h>
#include <isl/val.h>
#include <isl/aff.h>
#include <isl/ast.h>
#include <isl/ast_build.h>
#include <isl/id.h>
#include <isl/space.h>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <set>
#include <chrono>
#include <spdlog/spdlog.h>

namespace ari_exe {
namespace {
class LinearGuard {
  public:
    std::optional<std::string> integer(const z3::expr& e) {
        if (e.is_numeral() && e.is_int()) return std::string(Z3_get_numeral_string(e.ctx(), e));
        if (e.is_const() && e.is_int() &&
            e.decl().decl_kind() == Z3_OP_UNINTERPRETED) {
            auto [it, inserted] = names.try_emplace(e.id(), "x"+std::to_string(names.size()));
            if (inserted) variables.push_back(e);
            return it->second;
        }
        if (!e.is_app()) return std::nullopt;
        const auto kind = e.decl().decl_kind();
        if (kind == Z3_OP_ITE) {
            auto [it, inserted] = intermediates.try_emplace(
                e.id(), "e"+std::to_string(intermediates.size()));
            if (!inserted) return it->second;
            if (intermediates.size() > 32) return std::nullopt;
            auto condition = boolean(e.arg(0));
            auto lhs = integer(e.arg(1)), rhs = integer(e.arg(2));
            if (!condition || !lhs || !rhs) return std::nullopt;
            definitions.push_back("(("+*condition+") and "+it->second+"=("+*lhs+")) or "
                                  "((not ("+*condition+")) and "+it->second+"=("+*rhs+"))");
            return it->second;
        }
        if (kind == Z3_OP_UMINUS) {
            auto value = integer(e.arg(0));
            if (value) return "(0-("+*value+"))";
            return std::nullopt;
        }
        if ((kind == Z3_OP_IDIV || kind == Z3_OP_MOD) &&
            e.arg(1).is_numeral()) {
            std::int64_t divisor = 0;
            if (!e.arg(1).is_numeral_i64(divisor) || divisor <= 0) return std::nullopt;
            auto numerator = integer(e.arg(0));
            if (!numerator) return std::nullopt;
            const auto literal = std::to_string(divisor);
            const auto quotient = "floor(("+*numerator+")/"+literal+")";
            return kind == Z3_OP_IDIV ? quotient
                : "(("+*numerator+")-"+literal+"*("+quotient+"))";
        }
        if (kind == Z3_OP_MUL) {
            z3::expr factor = e.ctx().int_val(1);
            std::optional<std::string> variable;
            for (const auto& arg : e.args()) {
                if (arg.is_numeral()) factor = factor * arg;
                else {
                    if (variable) return std::nullopt;
                    variable = integer(arg);
                    if (!variable) return std::nullopt;
                }
            }
            factor = factor.simplify();
            const std::string literal = Z3_get_numeral_string(e.ctx(), factor);
            return variable ? literal+"*("+*variable+")" : literal;
        }
        if (kind != Z3_OP_ADD && kind != Z3_OP_SUB)
            return std::nullopt;
        std::string text;
        for (const auto& arg : e.args()) {
            auto term = integer(arg);
            if (!term) return std::nullopt;
            if (!text.empty()) text += kind == Z3_OP_ADD ? "+" : kind == Z3_OP_SUB ? "-" : "*";
            text += "("+*term+")";
        }
        return "("+text+")";
    }

    std::optional<std::string> boolean(const z3::expr& e) {
        if (e.is_true()) return "1=1";
        if (e.is_false()) return "1=0";
        if (!e.is_app()) return std::nullopt;
        const auto kind = e.decl().decl_kind();
        if (kind == Z3_OP_ITE) {
            auto condition = boolean(e.arg(0));
            auto lhs = boolean(e.arg(1)), rhs = boolean(e.arg(2));
            if (!condition || !lhs || !rhs) return std::nullopt;
            return "(("+*condition+") and ("+*lhs+")) or "
                   "((not ("+*condition+")) and ("+*rhs+"))";
        }
        if (kind == Z3_OP_NOT) {
            auto value = boolean(e.arg(0));
            if (value) return "not ("+*value+")";
            return std::nullopt;
        }
        if (kind == Z3_OP_AND || kind == Z3_OP_OR) {
            std::string text;
            for (const auto& arg : e.args()) {
                auto value = boolean(arg);
                if (!value) return std::nullopt;
                if (!text.empty()) text += kind == Z3_OP_AND ? " and " : " or ";
                text += "("+*value+")";
            }
            return text;
        }
        const char* comparison = kind == Z3_OP_EQ ? "=" :
            kind == Z3_OP_LE ? "<=" : kind == Z3_OP_GE ? ">=" :
            kind == Z3_OP_LT ? "<" : kind == Z3_OP_GT ? ">" : nullptr;
        if (!comparison || e.num_args() != 2) return std::nullopt;
        auto lhs = integer(e.arg(0)), rhs = integer(e.arg(1));
        if (!lhs || !rhs) return std::nullopt;
        return "("+*lhs+")"+comparison+"("+*rhs+")";
    }

    std::vector<z3::expr> variables;
    std::vector<std::string> definitions;
    std::map<unsigned, std::string> intermediates;
  private:
    std::map<unsigned, std::string> names;
};

std::optional<z3::expr> coefficient(isl_val* value, z3::context& ctx) {
    if (!value) return std::nullopt;
    const bool integral = isl_val_is_int(value) == isl_bool_true;
    char* text = integral ? isl_val_to_str(value) : nullptr;
    isl_val_free(value);
    if (!text) return std::nullopt;
    const auto result = ctx.int_val(text);
    std::free(text);
    return result;
}

struct HullResult {
    z3::context& ctx;
    const std::vector<z3::expr>& variables;
    z3::expr condition;
    bool valid = true;
};

isl_stat add_constraint(isl_constraint* constraint, void* user) {
    auto& result = *static_cast<HullResult*>(user);
    auto constant = coefficient(isl_constraint_get_constant_val(constraint), result.ctx);
    if (!constant) result.valid = false;
    z3::expr term = constant.value_or(result.ctx.int_val(0));
    for (unsigned i = 0; i < result.variables.size(); ++i) {
        auto c = coefficient(isl_constraint_get_coefficient_val(constraint, isl_dim_set, i), result.ctx);
        if (!c) result.valid = false;
        else term = term + *c * result.variables[i];
    }
    result.condition = result.condition &&
        (isl_constraint_is_equality(constraint) == isl_bool_true ? term == 0 : term >= 0);
    isl_constraint_free(constraint);
    return result.valid ? isl_stat_ok : isl_stat_error;
}

std::optional<z3::expr> ast_expression(
    isl_ast_expr* ast, z3::context& ctx,
    const std::map<std::string,z3::expr>& parameters, unsigned& budget) {
    if (!ast || !budget) return std::nullopt;
    --budget;
    if (isl_ast_expr_get_type(ast) == isl_ast_expr_int) {
        return coefficient(isl_ast_expr_int_get_val(ast),ctx);
    }
    if (isl_ast_expr_get_type(ast) == isl_ast_expr_id) {
        std::unique_ptr<isl_id,decltype(&isl_id_free)> id(
            isl_ast_expr_get_id(ast),isl_id_free);
        const auto* name=id ? isl_id_get_name(id.get()) : nullptr;
        auto found=name ? parameters.find(name) : parameters.end();
        return found==parameters.end() ? std::nullopt
                                       : std::optional<z3::expr>(found->second);
    }
    if (isl_ast_expr_get_type(ast) != isl_ast_expr_op) return std::nullopt;
    const auto size=isl_ast_expr_get_op_n_arg(ast);
    std::vector<z3::expr> args;
    for (int i=0;i<size;++i) {
        std::unique_ptr<isl_ast_expr,decltype(&isl_ast_expr_free)> arg(
            isl_ast_expr_get_op_arg(ast,i),isl_ast_expr_free);
        auto converted=ast_expression(arg.get(),ctx,parameters,budget);
        if (!converted) return std::nullopt;
        args.push_back(*converted);
    }
    if (args.empty()) return std::nullopt;
    auto boolean=[](const z3::expr& e) { return e.is_bool() ? e : e!=0; };
    const auto kind=isl_ast_expr_get_op_type(ast);
    if (kind==isl_ast_expr_op_cond || kind==isl_ast_expr_op_select) {
        if (args.size()!=3) return std::nullopt;
        return z3::ite(boolean(args[0]),args[1],args[2]);
    }
    auto result=args[0];
    if (kind==isl_ast_expr_op_minus && args.size()==1) return -result;
    for (unsigned i=1;i<args.size();++i) {
        const auto& next=args[i];
        switch (kind) {
        case isl_ast_expr_op_and: case isl_ast_expr_op_and_then:
            result=boolean(result)&&boolean(next); break;
        case isl_ast_expr_op_or: case isl_ast_expr_op_or_else:
            result=boolean(result)||boolean(next); break;
        case isl_ast_expr_op_add: result=result+next; break;
        case isl_ast_expr_op_sub: result=result-next; break;
        case isl_ast_expr_op_mul: result=result*next; break;
        case isl_ast_expr_op_min: result=z3::ite(result<next,result,next); break;
        case isl_ast_expr_op_max: result=z3::ite(result>next,result,next); break;
        case isl_ast_expr_op_eq: result=result==next; break;
        case isl_ast_expr_op_le: result=result<=next; break;
        case isl_ast_expr_op_lt: result=result<next; break;
        case isl_ast_expr_op_ge: result=result>=next; break;
        case isl_ast_expr_op_gt: result=result>next; break;
        case isl_ast_expr_op_div: case isl_ast_expr_op_fdiv_q:
        case isl_ast_expr_op_pdiv_q: case isl_ast_expr_op_pdiv_r:
        case isl_ast_expr_op_zdiv_r: {
            int64_t divisor=0;
            if (!next.is_numeral() || !next.is_numeral_i64(divisor) || divisor<=0)
                return std::nullopt;
            result=(kind==isl_ast_expr_op_pdiv_r || kind==isl_ast_expr_op_zdiv_r)
                ? result%next : result/next;
            break;
        }
        default: return std::nullopt;
        }
    }
    return result.simplify();
}

struct LastWriteResult {
    z3::context& ctx;
    isl_ast_build* build;
    const std::map<std::string,z3::expr>& parameters;
    z3::expr domain, time;
    unsigned budget=4096, pieces=0;
    bool valid=true;
};

isl_stat last_write_piece(isl_set* domain, isl_aff* value, void* user) {
    auto& result=*static_cast<LastWriteResult*>(user);
    std::unique_ptr<isl_set,decltype(&isl_set_free)> owned_domain(domain,isl_set_free);
    std::unique_ptr<isl_aff,decltype(&isl_aff_free)> owned_value(value,isl_aff_free);
    if (++result.pieces>32) { result.valid=false; return isl_stat_error; }
    std::unique_ptr<isl_ast_expr,decltype(&isl_ast_expr_free)> domain_ast(
        isl_ast_build_expr_from_set(result.build,isl_set_copy(domain)),isl_ast_expr_free);
    std::unique_ptr<isl_ast_expr,decltype(&isl_ast_expr_free)> value_ast(
        isl_ast_build_expr_from_pw_aff(result.build,
            isl_pw_aff_from_aff(owned_value.release())),isl_ast_expr_free);
    auto guard=ast_expression(domain_ast.get(),result.ctx,result.parameters,result.budget);
    auto time=ast_expression(value_ast.get(),result.ctx,result.parameters,result.budget);
    if (!guard || !time || !time->is_int()) {
        result.valid=false; return isl_stat_error;
    }
    const auto condition=guard->is_bool() ? *guard : *guard!=0;
    result.domain=result.domain||condition;
    result.time=result.pieces==1 ? *time : z3::ite(condition,*time,result.time);
    return isl_stat_ok;
}
}

std::optional<z3::expr> summarize_overwrite_regions(
    const z3::expr& signature, const z3::func_decl& recurrence,
    const z3::expr& iteration, const std::vector<z3::expr>& conditions,
    const std::vector<z3::expr>& values, const z3::expr& valid_domain,
    std::optional<z3::expr> iteration_limit) {
    if (conditions.size()!=values.size() || conditions.size()<2 || values.size()>12)
        return std::nullopt;
    if (!z3::eq(values[0],signature)) return std::nullopt;
    auto& ctx=signature.ctx();
    std::vector<z3::expr> domain_terms{valid_domain};
    if (iteration_limit) domain_terms.push_back(*iteration_limit);
    unsigned domain_budget=4096;
    while (!domain_terms.empty()) {
        if (!domain_budget--) return std::nullopt;
        auto term=domain_terms.back(); domain_terms.pop_back();
        if (!term.is_app() || z3::eq(term,iteration)) return std::nullopt;
        for (const auto& arg:term.args()) domain_terms.push_back(arg);
    }
    z3::expr_vector previous_args(signature.args());
    previous_args.push_back(iteration-1);
    const auto previous=recurrence(previous_args);
    const auto active=iteration>=1&&
        (iteration_limit ? iteration<=*iteration_limit : ctx.bool_val(true));
    std::vector<unsigned> overwrites;
    auto written=ctx.bool_val(false);
    for (unsigned c=1;c<values.size();++c) {
        if (z3::eq(values[c].simplify(),previous.simplify())) continue;
        z3::solver reachable(ctx);
        reachable.set("timeout",50u);
        reachable.add(valid_domain&&active&&conditions[c]);
        if (reachable.check()==z3::unsat) continue;
        std::vector<z3::expr> pending{values[c],conditions[c]};
        unsigned budget=4096;
        while (!pending.empty()) {
            if (!budget--) return std::nullopt;
            auto term=pending.back(); pending.pop_back();
            if (!term.is_app() || z3::eq(term.decl(),recurrence)) return std::nullopt;
            for (const auto& arg:term.args()) pending.push_back(arg);
        }
        overwrites.push_back(c);
        written=written||conditions[c];
    }
    if (overwrites.empty()) return std::nullopt;
    const z3::expr count(ctx,Z3_mk_fresh_const(ctx,"region_count",ctx.int_sort()));
    auto verify_candidate = [&](const z3::expr& candidate) {
        auto at = [&](const z3::expr& n) {
            z3::expr_vector from(ctx), to(ctx);
            from.push_back(count); to.push_back(n);
            auto value = candidate;
            return value.substitute(from, to).simplify();
        };
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(1500);
        auto certify = [&](const z3::expr& premise, const z3::expr& conclusion) {
            if (std::chrono::steady_clock::now() >= deadline) return false;
            z3::solver checker(ctx);
            checker.set("timeout", 150u);
            checker.add(valid_domain && premise && !conclusion);
            return checker.check() == z3::unsat;
        };
        if (!certify(ctx.bool_val(true), at(ctx.int_val(0)) == signature)) return false;
        auto covered = ctx.bool_val(false);
        for (unsigned c=1; c<conditions.size(); ++c) covered = covered || conditions[c];
        if (!certify(active, !conditions[0] && covered)) return false;
        const auto current = at(iteration), prior = at(iteration-1);
        for (unsigned c=1; c<values.size(); ++c) {
            const auto rhs = z3::eq(values[c].simplify(), previous.simplify())
                           ? prior : values[c];
            if (!certify(active && conditions[c], current == rhs)) return false;
        }
        return true;
    };
    // An affine address often determines its write time directly. Avoid a
    // lexicographic maximum (and its boundary cases) when induction certifies it.
    auto direct = signature;
    bool direct_complete = true;
    for (const auto c : overwrites) {
        std::optional<z3::expr> time;
        std::vector<z3::expr> pending{conditions[c]};
        while (!pending.empty() && !time) {
            auto term = pending.back(); pending.pop_back();
            if (term.is_and()) {
                for (const auto& arg : term.args()) pending.push_back(arg);
                continue;
            }
            if (!term.is_eq() || !term.arg(0).is_int()) continue;
            auto difference = term.arg(0)-term.arg(1);
            z3::expr_vector from(ctx), zero(ctx), one(ctx);
            from.push_back(iteration); zero.push_back(ctx.int_val(0));
            one.push_back(ctx.int_val(1));
            const auto base = difference.substitute(from, zero).simplify();
            const auto delta = (difference.substitute(from, one)-base).simplify();
            int64_t slope = 0;
            if (!delta.is_numeral_i64(slope) || slope == 0) continue;
            time = slope > 0 ? (-base / ctx.int_val(slope)).simplify()
                             : (base / -ctx.int_val(slope)).simplify();
        }
        if (!time) { direct_complete=false; break; }
        z3::expr_vector from(ctx), to(ctx);
        from.push_back(iteration); to.push_back(*time);
        auto guard=conditions[c], value=values[c];
        direct=z3::ite(*time>=1 && *time<=count &&
                      guard.substitute(from,to), value.substitute(from,to), direct);
    }
    if (direct_complete && verify_candidate(direct)) {
        z3::expr_vector from(ctx), to(ctx);
        from.push_back(count); to.push_back(iteration);
        spdlog::info("[array-region] certified unique-write composition");
        return direct.substitute(from,to).simplify();
    }
    LinearGuard writer;
    const auto event=writer.boolean(
        (valid_domain&&written&&active&&iteration<=count).simplify());
    if (!event || writer.variables.size()>24) return std::nullopt;
    std::string parameters="[", time_name;
    std::map<std::string,z3::expr> names;
    for (unsigned i=0;i<writer.variables.size();++i) {
        const auto name="x"+std::to_string(i);
        if (z3::eq(writer.variables[i],iteration)) time_name=name;
        else {
            if (parameters.size()>1) parameters+=",";
            parameters+=name;
            names.emplace(name,writer.variables[i]);
        }
    }
    if (time_name.empty()) return std::nullopt;
    std::string set=parameters+"] -> { ["+time_name+"] : ";
    if (!writer.intermediates.empty()) {
        set+="exists (";
        for (unsigned i=0;i<writer.intermediates.size();++i) {
            if (i) set+=",";
            set+="e"+std::to_string(i);
        }
        set+=": ";
        for (const auto& definition:writer.definitions) set+="("+definition+") and ";
        set+="("+*event+")) }";
    } else set+=*event+" }";
    std::unique_ptr<isl_ctx,decltype(&isl_ctx_free)> context(isl_ctx_alloc(),isl_ctx_free);
    isl_options_set_on_error(context.get(),ISL_ON_ERROR_CONTINUE);
    isl_ctx_set_max_operations(context.get(),200000);
    std::unique_ptr<isl_set,decltype(&isl_set_free)> events(
        isl_set_read_from_str(context.get(),set.c_str()),isl_set_free);
    if (!events) return std::nullopt;
    std::unique_ptr<isl_pw_multi_aff,decltype(&isl_pw_multi_aff_free)> maximum(
        isl_set_lexmax_pw_multi_aff(isl_set_copy(events.get())),isl_pw_multi_aff_free);
    if (!maximum) return std::nullopt;
    std::unique_ptr<isl_pw_aff,decltype(&isl_pw_aff_free)> last(
        isl_pw_multi_aff_get_pw_aff(maximum.get(),0),isl_pw_aff_free);
    if (!last) return std::nullopt;
    auto* space=isl_space_params(isl_set_get_space(events.get()));
    std::unique_ptr<isl_ast_build,decltype(&isl_ast_build_free)> build(
        isl_ast_build_from_context(isl_set_universe(space)),isl_ast_build_free);
    if (!build) return std::nullopt;
    LastWriteResult decoded{ctx,build.get(),names,ctx.bool_val(false),ctx.int_val(0)};
    if (isl_pw_aff_foreach_piece(last.get(),last_write_piece,&decoded)<0 || !decoded.valid)
        return std::nullopt;
    const auto domain=decoded.domain.simplify(), time=decoded.time.simplify();
    z3::expr_vector source(ctx), target(ctx);
    source.push_back(iteration); target.push_back(time);
    auto result=signature;
    for (const auto c:overwrites) {
        auto guard=conditions[c], value=values[c];
        result=overwrites.size()==1 ? value.substitute(source,target)
            : z3::ite(guard.substitute(source,target),value.substitute(source,target),result);
    }
    result=z3::ite(domain,result,signature).simplify();
    // The polyhedral result is only a candidate. Base and every transition
    // must hold, including identity frames, before it can replace a recurrence.
    if (!verify_candidate(result)) return std::nullopt;
    z3::expr_vector from(ctx), to(ctx);
    from.push_back(count); to.push_back(iteration);
    spdlog::info("[array-region] certified last-write composition");
    return result.substitute(from,to).simplify();
}

z3::expr normalize_array_guard(const z3::expr& guard) {
    const auto original = guard.simplify();
    LinearGuard writer;
    auto text = writer.boolean(original);
    if (!text || writer.variables.empty() || writer.variables.size() > 16) {
        spdlog::debug("[array-guard] not an affine guard");
        return original;
    }
    std::string set = "{ [";
    for (unsigned i = 0; i < writer.variables.size(); ++i) {
        if (i) set += ",";
        set += "x"+std::to_string(i);
    }
    set += "] : ";
    if (!writer.intermediates.empty()) {
        set += "exists (";
        for (unsigned i = 0; i < writer.intermediates.size(); ++i) {
            if (i) set += ",";
            set += "e"+std::to_string(i);
        }
        set += ": ";
        for (const auto& definition : writer.definitions) set += "("+definition+") and ";
        set += "("+*text+")) }";
    } else {
        set += *text+" }";
    }
    isl_ctx* ctx = isl_ctx_alloc();
    isl_options_set_on_error(ctx, ISL_ON_ERROR_CONTINUE);
    isl_ctx_set_max_operations(ctx, 1000000);
    isl_set* domain = isl_set_read_from_str(ctx, set.c_str());
    domain = isl_set_coalesce(domain);
    isl_basic_set* hull = isl_set_convex_hull(domain);
    hull = isl_basic_set_remove_divs(hull);
    HullResult result{guard.ctx(), writer.variables, guard.ctx().bool_val(true)};
    if (!hull || isl_basic_set_dim(hull, isl_dim_div) != 0 ||
        isl_basic_set_is_empty(hull) != isl_bool_false) {
        spdlog::debug("[array-guard] hull unavailable: {}",
            isl_ctx_last_error_msg(ctx) ? isl_ctx_last_error_msg(ctx) : "empty or local divisions");
        result.valid = false;
    }
    if (result.valid && isl_basic_set_foreach_constraint(hull, add_constraint, &result) < 0)
        result.valid = false;
    isl_basic_set_free(hull);
    isl_ctx_free(ctx);
    if (!result.valid) return original;
    z3::solver solver(guard.ctx());
    z3::params parameters(guard.ctx());
    parameters.set("timeout", 100u);
    solver.set(parameters);
    const auto candidate = result.condition.simplify();
    solver.add(original != candidate);
    const auto proof = solver.check();
    spdlog::debug("[array-guard] hull equivalence {}, before={}, after={}",
        proof == z3::unsat ? "proved" : "not proved", original.to_string(), candidate.to_string());
    return proof == z3::unsat ? candidate : original;
}
}
