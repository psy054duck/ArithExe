#include "VerificationSession.h"

#include <stdexcept>
#include <functional>
#include <optional>
#include <chrono>
#include <tuple>

using namespace ari_exe;

thread_local VerificationSession* VerificationSession::active_session =
    nullptr;

VerificationSession::Activation::Activation(VerificationSession& session)
    : previous_session(active_session),
      previous_manager(AnalysisManager::set_active(&session.analysis_manager)) {
    active_session = &session;
}

VerificationSession::Activation::~Activation() {
    active_session = previous_session;
    AnalysisManager::set_active(previous_manager);
}

VerificationSession::~VerificationSession() {
    clear_module_state();
}

VerificationSession& VerificationSession::current() {
    if (!active_session) {
        throw std::logic_error("no active verification session");
    }
    return *active_session;
}

z3::expr VerificationSession::expand_current_aggregates(const z3::expr& expression) {
    return active_session ? active_session->expand_aggregates(expression) : expression;
}

void VerificationSession::register_aggregate(
    bool product, const z3::expr& signature, const z3::expr& index,
    const z3::expr& body) {
    if (!signature.is_app() || signature.num_args() < 2 ||
        signature.decl().decl_kind() != Z3_OP_UNINTERPRETED ||
        !signature.is_int() || !body.is_int() || !index.is_const() ||
        !index.is_int() || !signature.decl().name().str().starts_with("ari_finite_")) {
        throw std::invalid_argument("malformed finite aggregate definition");
    }
    for (const auto& parameter : signature.args()) {
        if (!parameter.is_const() || !parameter.is_int() || z3::eq(parameter, index)) {
            throw std::invalid_argument("malformed finite aggregate parameter");
        }
    }
    finite_aggregates.insert_or_assign(signature.decl().id(),
        FiniteAggregate{product, signature, index, body});
}

z3::expr VerificationSession::expand_aggregates(const z3::expr& expression) const {
    if (finite_aggregates.empty()) return expression;
    unsigned nodes = 4096, terms = 256;
    std::map<unsigned, std::pair<z3::expr, z3::expr>> memo;
    std::function<z3::expr(const z3::expr&)> visit = [&](const z3::expr& term) -> z3::expr {
        if (!nodes || !term.is_app()) return term;
        auto cached = memo.find(term.id());
        if (cached != memo.end()) return cached->second.second;
        --nodes;
        z3::expr_vector arguments(term.ctx());
        for (const auto& argument : term.args()) arguments.push_back(visit(argument));
        auto result = term.decl()(arguments).simplify();
        auto found = result.is_app() ? finite_aggregates.find(result.decl().id())
                                     : finite_aggregates.end();
        if (found != finite_aggregates.end()) {
            const auto& definition = found->second;
            const auto count = (result.arg(1) - result.arg(0)).simplify();
            int64_t length = 0;
            if (count.is_numeral() && count.is_numeral_i64(length) && length <= 32 &&
                (length <= 0 || static_cast<uint64_t>(length) <= terms)) {
                z3::expr_vector from(term.ctx()), to(term.ctx());
                for (unsigned i = 2; i < result.num_args(); ++i) {
                    from.push_back(definition.signature.arg(i));
                    to.push_back(result.arg(i));
                }
                from.push_back(definition.index);
                const auto lower = result.arg(0);
                result = term.ctx().int_val(definition.product ? 1 : 0);
                if (length > 0) terms -= static_cast<unsigned>(length);
                for (int64_t offset = 0; offset < length; ++offset) {
                    to.push_back((lower + term.ctx().int_val(offset)).simplify());
                    auto body = definition.body;
                    const auto value = visit(body.substitute(from, to));
                    to.pop_back();
                    result = definition.product ? result * value : result + value;
                }
                result = result.simplify();
            }
        }
        memo.emplace(term.id(), std::make_pair(term, result));
        return result;
    };
    return visit(expression);
}

bool VerificationSession::contains_aggregates(const z3::expr& expression) const {
    if (finite_aggregates.empty()) return false;
    std::vector<z3::expr> pending{expression};
    std::set<unsigned> visited;
    while (!pending.empty()) {
        auto term = pending.back();
        pending.pop_back();
        if (!visited.insert(term.id()).second) continue;
        if (term.is_quantifier()) pending.push_back(term.body());
        if (!term.is_app()) continue;
        if (finite_aggregates.contains(term.decl().id())) return true;
        for (const auto& argument : term.args()) pending.push_back(argument);
    }
    return false;
}

z3::expr_vector VerificationSession::sum_linearity_identities(
    const z3::expr& expression) const {
    auto& ctx = expression.ctx();
    z3::expr_vector identities(ctx);
    const z3::expr index(ctx, Z3_mk_fresh_const(ctx, "sum_linear_index", ctx.int_sort()));
    unsigned nodes = 4096, terms = 256;
    std::vector<z3::expr> pending{expression}, applications;
    std::set<unsigned> seen;
    while (!pending.empty() && nodes) {
        auto term = pending.back(); pending.pop_back();
        if (!seen.insert(term.id()).second || !term.is_app()) continue;
        --nodes;
        auto found = finite_aggregates.find(term.decl().id());
        if (found != finite_aggregates.end() && !found->second.product) {
            if (applications.size() == 32) break;
            applications.push_back(term);
        }
        for (const auto& argument : term.args()) pending.push_back(argument);
    }

    std::map<unsigned, std::pair<z3::expr, bool>> dependencies;
    std::function<std::optional<bool>(const z3::expr&)> depends =
        [&](const z3::expr& term) -> std::optional<bool> {
        auto found = dependencies.find(term.id());
        if (found != dependencies.end()) return found->second.second;
        if (!nodes || !term.is_app()) return std::nullopt;
        --nodes;
        bool result = z3::eq(term, index);
        for (const auto& argument : term.args()) {
            auto child = depends(argument);
            if (!child) return std::nullopt;
            result = result || *child;
        }
        dependencies.emplace(term.id(), std::make_pair(term, result));
        return result;
    };
    struct Atom {
        z3::expr lower, upper, body, value;
    };
    std::map<std::tuple<unsigned, unsigned, unsigned>, Atom> atoms;
    for (const auto& application : applications) {
        const auto& definition = finite_aggregates.at(application.decl().id());
        z3::expr_vector from(ctx), to(ctx);
        from.push_back(definition.index); to.push_back(index);
        for (unsigned p = 2; p < application.num_args(); ++p) {
            from.push_back(definition.signature.arg(p)); to.push_back(application.arg(p));
        }
        auto body = definition.body;
        body = body.substitute(from, to).simplify();
        const auto lower = application.arg(0).simplify();
        const auto upper = application.arg(1).simplify();
        const auto length = z3::ite(upper <= lower, ctx.int_val(0), upper-lower).simplify();
        auto result = ctx.int_val(0);
        std::function<bool(const z3::expr&, const z3::expr&)> collect =
            [&](const z3::expr& term, const z3::expr& coefficient) {
            if (!terms) return false;
            --terms;
            auto dependent = depends(term);
            if (!dependent) return false;
            if (!*dependent) {
                result = result + coefficient * length * term;
                return true;
            }
            const auto kind = term.decl().decl_kind();
            if (kind == Z3_OP_ADD || kind == Z3_OP_SUB || kind == Z3_OP_UMINUS) {
                for (unsigned p = 0; p < term.num_args(); ++p) {
                    const bool negative = kind == Z3_OP_UMINUS || (kind == Z3_OP_SUB && p);
                    if (!collect(term.arg(p), negative ? -coefficient : coefficient)) return false;
                }
                return true;
            }
            auto atom = term;
            auto scale = coefficient;
            if (kind == Z3_OP_MUL) {
                z3::expr_vector factors(ctx);
                for (const auto& factor : term.args()) {
                    auto child = depends(factor);
                    if (!child) return false;
                    if (*child) factors.push_back(factor);
                    else scale = scale * factor;
                }
                if (factors.size() == 1) return collect(factors[0], scale.simplify());
                atom = ctx.int_val(1);
                for (const auto& factor : factors) atom = atom * factor;
                atom = atom.simplify();
            }
            const auto key = std::make_tuple(lower.id(), upper.id(), atom.id());
            auto found = atoms.find(key);
            if (found == atoms.end()) {
                if (atoms.size() == 128) return false;
                const z3::expr value(ctx, Z3_mk_fresh_const(ctx, "sum_linear_atom", ctx.int_sort()));
                found = atoms.emplace(key, Atom{lower, upper, atom,
                    z3::ite(upper <= lower, ctx.int_val(0), value).simplify()}).first;
            }
            result = result + scale * found->second.value;
            return true;
        };
        // Each fresh atom denotes a whole finite sum, not an array element.
        // Only complete decompositions become lemmas; opaque sums remain in
        // the original query so a SAT model still cannot certify a bug.
        if (collect(body, ctx.int_val(1))) {
            identities.push_back(application == result.simplify());
        }
    }
    return identities;
}

z3::expr_vector VerificationSession::aggregate_identities(
    const z3::expr& expression, const z3::expr& assumptions) const {
    z3::expr_vector identities(expression.ctx());
    if (!contains_aggregates(expression)) return identities;
    identities = sum_linearity_identities(expression);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    auto& ctx = expression.ctx();
    // Fresh constants must not alias a source-level array index or parameter.
    const z3::expr i(ctx, Z3_mk_fresh_const(ctx, "sum_permutation_i", ctx.int_sort()));
    const z3::expr j(ctx, Z3_mk_fresh_const(ctx, "sum_permutation_j", ctx.int_sort()));
    auto arithmetic = [&](const z3::expr& root) {
        std::vector<z3::expr> pending{root};
        std::set<unsigned> seen;
        while (!pending.empty()) {
            auto term = pending.back(); pending.pop_back();
            if (!seen.insert(term.id()).second) continue;
            if (!term.is_app() || (term.decl().decl_kind() == Z3_OP_UNINTERPRETED &&
                                   term.num_args() != 0)) return false;
            for (const auto& arg : term.args()) pending.push_back(arg);
        }
        return true;
    };
    z3::expr domain = ctx.bool_val(true);
    std::vector<z3::expr> pending{assumptions};
    while (!pending.empty()) {
        auto term = pending.back(); pending.pop_back();
        if (term.is_and()) {
            for (const auto& arg : term.args()) pending.push_back(arg);
        } else if (arithmetic(term)) {
            domain = domain && term;
        }
    }
    unsigned checks = 0;
    auto prove = [&](const z3::expr& premise, const z3::expr& conclusion) {
        if (++checks > 24 || std::chrono::steady_clock::now() >= deadline) return false;
        z3::solver certificate(ctx);
        certificate.set("timeout", 50u);
        certificate.add(domain && premise && !conclusion);
        return certificate.check() == z3::unsat;
    };
    struct ReadSum {
        z3::func_decl array;
        z3::expr argument;
        z3::expr remainder;
    };
    std::function<std::optional<ReadSum>(const z3::expr&)> decompose =
        [&](const z3::expr& term) -> std::optional<ReadSum> {
        if (!term.is_app()) return std::nullopt;
        if (term.decl().decl_kind() == Z3_OP_UNINTERPRETED && term.num_args() == 1 &&
            !finite_aggregates.contains(term.decl().id()) && term.is_int() &&
            term.arg(0).is_int() && arithmetic(term.arg(0))) {
            return ReadSum{term.decl(), term.arg(0), ctx.int_val(0)};
        }
        if (term.is_ite() && arithmetic(term.arg(0))) {
            auto yes = decompose(term.arg(1)), no = decompose(term.arg(2));
            if (yes && no && z3::eq(yes->array, no->array)) {
                return ReadSum{yes->array,
                    z3::ite(term.arg(0), yes->argument, no->argument).simplify(),
                    z3::ite(term.arg(0), yes->remainder, no->remainder).simplify()};
            }
        }
        if (term.decl().decl_kind() == Z3_OP_ADD) {
            std::optional<ReadSum> read;
            auto remainder = ctx.int_val(0);
            for (const auto& arg : term.args()) {
                if (arithmetic(arg)) { remainder = remainder + arg; continue; }
                if (read) return std::nullopt;
                read = decompose(arg);
                if (!read) return std::nullopt;
            }
            if (read) {
                read->remainder = (read->remainder + remainder).simplify();
                return read;
            }
        }
        return std::nullopt;
    };
    struct Sum {
        z3::expr application;
        ReadSum read;
        std::optional<bool> permutation;
    };
    std::vector<Sum> sums;
    std::set<unsigned> seen;
    pending = {expression};
    unsigned nodes = 4096;
    while (!pending.empty() && nodes--) {
        auto term = pending.back(); pending.pop_back();
        if (!seen.insert(term.id()).second || !term.is_app()) continue;
        auto found = finite_aggregates.find(term.decl().id());
        if (found != finite_aggregates.end() && !found->second.product && sums.size() < 16) {
            auto body = found->second.body;
            z3::expr_vector from(ctx), to(ctx);
            from.push_back(found->second.index); to.push_back(i);
            for (unsigned p = 2; p < term.num_args(); ++p) {
                from.push_back(found->second.signature.arg(p)); to.push_back(term.arg(p));
            }
            auto read = decompose(body.substitute(from, to).simplify());
            if (read) sums.push_back(Sum{term, *read, std::nullopt});
        }
        for (const auto& arg : term.args()) pending.push_back(arg);
    }
    auto in_range = [](const z3::expr& x, const z3::expr& sum) {
        return sum.arg(0) <= x && x < sum.arg(1);
    };
    auto permutation = [&](Sum& sum) {
        if (!sum.permutation) {
            auto at_j = sum.read.argument;
            z3::expr_vector from(ctx), to(ctx);
            from.push_back(i); to.push_back(j);
            at_j = at_j.substitute(from, to);
            // A self-map of a finite integer interval is a permutation iff it
            // is injective. No quantified sum axioms or guessed inverses occur.
            sum.permutation = prove(in_range(i, sum.application),
                                     in_range(sum.read.argument, sum.application)) &&
                prove(in_range(i, sum.application) && in_range(j, sum.application) &&
                          sum.read.argument == at_j, i == j);
        }
        return *sum.permutation;
    };
    for (unsigned a = 0; a < sums.size(); ++a) {
        for (unsigned b = a + 1; b < sums.size(); ++b) {
            auto& left = sums[a]; auto& right = sums[b];
            if (!z3::eq(left.read.array, right.read.array)) continue;
            if (!prove(ctx.bool_val(true), left.application.arg(0) == right.application.arg(0) &&
                       left.application.arg(1) == right.application.arg(1))) continue;
            if (!prove(in_range(i, left.application),
                       left.read.remainder == right.read.remainder)) continue;
            if (prove(in_range(i, left.application), left.read.argument == right.read.argument) ||
                (permutation(left) && permutation(right))) {
                identities.push_back(left.application == right.application);
            }
        }
    }
    return identities;
}

PathSymbol VerificationSession::intern_loop_path(
    llvm::Loop* loop, const std::vector<PathDecisionEvent>& decisions) {
    auto& alphabet = loop_path_alphabets[loop];
    auto found = alphabet.find(decisions);
    if (found != alphabet.end()) return found->second;
    const PathSymbol symbol = next_path_symbol++;
    alphabet.emplace(decisions, symbol);
    return symbol;
}

void VerificationSession::clear_module_state() {
    finite_aggregates.clear();
    instruction_cache.clear();
    function_summary_cache = SymbolTable<FunctionSummary>();
    loop_summary_cache = SymbolTable<LoopSummary>();
    cached_function_values.clear();
    solver_worker.reset();
    call_value_counters.clear();
    failed_loops.clear();
    memory_object_name_counters.clear();
    loop_path_alphabets.clear();
    nested_path_summary_cache.clear();
    unsupported_nested_profiles.clear();
    unsupported_profiles.clear();
    nested_path_summaries_built = 0;
    next_path_symbol = 1;
    path_expression_id = 1;
    path_compressions = 0;
    path_accelerations = 0;
    path_affine_templates = 0;
    analysis_manager.clear_module_state();
}
