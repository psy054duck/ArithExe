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
        FiniteAggregate{product, signature, index, body, {}, {}, {}});
}

void VerificationSession::register_fold(
    const z3::expr& signature, const z3::expr& index, const z3::expr& body,
    const z3::expr& left, const z3::expr& right, const z3::expr& combine) {
    if (signature.num_args() < 3 || !left.is_const() || !right.is_const() ||
        !left.is_int() || !right.is_int() || !combine.is_int() || z3::eq(left, right)) {
        throw std::invalid_argument("malformed finite fold operator");
    }
    std::vector<z3::expr> pending{combine};
    std::set<unsigned> seen;
    while (!pending.empty()) {
        auto term = pending.back(); pending.pop_back();
        if (!seen.insert(term.id()).second) continue;
        if (seen.size() > 128 || !term.is_app() || (!term.is_int() && !term.is_bool()) ||
            (term.decl().decl_kind() == Z3_OP_UNINTERPRETED &&
             !z3::eq(term, left) && !z3::eq(term, right))) {
            throw std::invalid_argument("fold operator must be a closed pure integer expression");
        }
        for (const auto& argument : term.args()) pending.push_back(argument);
    }
    register_aggregate(false, signature, index, body);
    auto& definition = finite_aggregates.at(signature.decl().id());
    definition.combine = combine;
    definition.left = left;
    definition.right = right;
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
                for (unsigned i = definition.combine ? 3 : 2; i < result.num_args(); ++i) {
                    from.push_back(definition.signature.arg(i));
                    to.push_back(result.arg(i));
                }
                from.push_back(definition.index);
                const auto lower = result.arg(0);
                const auto seed = definition.combine ? result.arg(2)
                    : term.ctx().int_val(definition.product ? 1 : 0);
                result = seed;
                if (length > 0) terms -= static_cast<unsigned>(length);
                for (int64_t offset = 0; offset < length; ++offset) {
                    to.push_back((lower + term.ctx().int_val(offset)).simplify());
                    auto body = definition.body;
                    const auto value = visit(body.substitute(from, to));
                    to.pop_back();
                    if (definition.combine) {
                        z3::expr_vector operands(term.ctx()), values(term.ctx());
                        operands.push_back(*definition.left); operands.push_back(*definition.right);
                        values.push_back(result); values.push_back(value);
                        auto combine = *definition.combine;
                        result = combine.substitute(operands, values);
                    } else {
                        result = definition.product ? result * value : result + value;
                    }
                }
                result = result.simplify();
            }
        }
        memo.emplace(term.id(), std::make_pair(term, result));
        return result;
    };
    return visit(expression);
}

namespace {
z3::expr_vector exact_polynomial_divisions(const z3::expr& expression) {
    auto& ctx = expression.ctx();
    z3::expr_vector identities(ctx);
    std::vector<z3::expr> pending{expression};
    std::set<unsigned> visited;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (!pending.empty() && visited.size() < 4096 && identities.size() < 16 &&
           std::chrono::steady_clock::now() < deadline) {
        auto term = pending.back(); pending.pop_back();
        if (!visited.insert(term.id()).second || !term.is_app()) continue;
        for (const auto& arg : term.args()) pending.push_back(arg);
        int64_t divisor = 0;
        if (term.decl().decl_kind() != Z3_OP_IDIV ||
            !term.arg(1).is_numeral() || !term.arg(1).is_numeral_i64(divisor) || divisor < 2 || divisor > 8) continue;
        std::map<unsigned, unsigned> variables;
        std::vector<z3::expr> symbols;
        unsigned steps = 30000;
        bool supported = true;
        std::vector<z3::expr> polynomial{term.arg(0)};
        std::set<unsigned> seen;
        while (!polynomial.empty() && supported) {
            auto node = polynomial.back(); polynomial.pop_back();
            if (!seen.insert(node.id()).second) continue;
            if (seen.size() > 256 || !node.is_app() || !node.is_int()) { supported=false; break; }
            int64_t literal = 0;
            if (node.is_numeral() && node.is_numeral_i64(literal)) continue;
            const auto kind = node.decl().decl_kind();
            if (kind == Z3_OP_UNINTERPRETED && !node.num_args()) {
                if (!variables.contains(node.id())) {
                    variables.emplace(node.id(), symbols.size()); symbols.push_back(node);
                }
                if (symbols.size() > 4) supported=false;
            } else if (kind == Z3_OP_ADD || kind == Z3_OP_SUB ||
                       kind == Z3_OP_UMINUS || kind == Z3_OP_MUL) {
                for (const auto& arg : node.args()) polynomial.push_back(arg);
            } else supported=false;
        }
        if (!supported) continue;
        std::vector<int64_t> residues(symbols.size());
        auto reduce = [&](int64_t value) { return (value % divisor + divisor) % divisor; };
        std::function<int64_t(const z3::expr&)> evaluate = [&](const z3::expr& node) -> int64_t {
            if (!steps || std::chrono::steady_clock::now() >= deadline) { supported=false; return 0; }
            --steps;
            int64_t literal = 0;
            if (node.is_numeral() && node.is_numeral_i64(literal)) return reduce(literal);
            if (variables.contains(node.id())) return residues[variables.at(node.id())];
            auto kind = node.decl().decl_kind();
            int64_t value = kind == Z3_OP_MUL ? 1 : 0;
            for (unsigned j=0; j<node.num_args(); ++j) {
                auto child=evaluate(node.arg(j));
                if (kind == Z3_OP_MUL) value=reduce(value*child);
                else value=reduce(value+((kind == Z3_OP_UMINUS || (kind == Z3_OP_SUB && j)) ? -child : child));
            }
            return value;
        };
        std::function<bool(unsigned)> check = [&](unsigned position) {
            if (!supported) return false;
            if (position == residues.size()) return evaluate(term.arg(0)) == 0 && supported;
            for (int64_t value=0; value<divisor; ++value) {
                residues[position]=value;
                if (!check(position+1)) return false;
            }
            return true;
        };
        // Integer polynomials are periodic modulo d. Exhaustive residue
        // evaluation certifies divisibility for every integer assignment.
        if (check(0)) identities.push_back(static_cast<int>(divisor)*term == term.arg(0));
    }
    return identities;
}
}

z3::expr VerificationSession::aggregate_query(
    const z3::expr& expression, const z3::expr& assumptions) const {
    // Normalize while both sides still retain their finite-domain structure.
    // Expansion is exact, but can erase only one side at the size cutoff.
    auto identities = aggregate_identities(expression, assumptions);
    z3::expr_vector query(expression.ctx());
    query.push_back(expand_aggregates(expression));
    for (const auto& identity : identities) query.push_back(expand_aggregates(identity));
    auto divisions = exact_polynomial_divisions(z3::mk_and(query));
    if (divisions.empty()) return z3::mk_and(query);
    for (const auto& identity : divisions) query.push_back(identity);
    z3::params normalization(expression.ctx());
    normalization.set("som", true);
    return z3::mk_and(query).simplify(normalization);
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
        if (found != finite_aggregates.end() && !found->second.product && !found->second.combine) {
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
    const z3::expr element(ctx, Z3_mk_fresh_const(ctx, "fold_element", ctx.int_sort()));
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
        if (++checks > 96 || std::chrono::steady_clock::now() >= deadline) return false;
        z3::solver certificate(ctx);
        certificate.set("timeout", 50u);
        certificate.add(domain && premise && !conclusion);
        return certificate.check() == z3::unsat;
    };
    struct ReadSum {
        z3::func_decl array;
        z3::expr argument;
        z3::expr transform;
        z3::expr remainder;
    };
    unsigned template_nodes = 4096;
    std::function<std::optional<ReadSum>(const z3::expr&)> decompose =
        [&](const z3::expr& term) -> std::optional<ReadSum> {
        if (!template_nodes || !term.is_app()) return std::nullopt;
        --template_nodes;
        if (term.decl().decl_kind() == Z3_OP_UNINTERPRETED && term.num_args() == 1 &&
            !finite_aggregates.contains(term.decl().id()) && term.is_int() &&
            term.arg(0).is_int() && arithmetic(term.arg(0))) {
            return ReadSum{term.decl(), term.arg(0), element, ctx.int_val(0)};
        }
        if (term.is_ite() && arithmetic(term.arg(0))) {
            auto yes = decompose(term.arg(1)), no = decompose(term.arg(2));
            if (yes && no && z3::eq(yes->array, no->array) &&
                z3::eq(yes->transform, no->transform)) {
                return ReadSum{yes->array,
                    z3::ite(term.arg(0), yes->argument, no->argument).simplify(),
                    yes->transform, ctx.int_val(0)};
            }
        }
        if (term.decl().decl_kind() == Z3_OP_UNINTERPRETED) return std::nullopt;
        std::optional<ReadSum> read;
        z3::expr_vector arguments(ctx);
        for (const auto& arg : term.args()) {
            if (arithmetic(arg)) {
                // A transform may mention parameters, but never the position.
                z3::expr_vector from(ctx), to(ctx);
                from.push_back(i); to.push_back(j);
                auto renamed = arg;
                if (!z3::eq(arg, renamed.substitute(from, to))) return std::nullopt;
                arguments.push_back(arg);
                continue;
            }
            auto child = decompose(arg);
            if (!child || (read && (!z3::eq(read->array, child->array) ||
                                    !z3::eq(read->argument, child->argument)))) return std::nullopt;
            arguments.push_back(child->transform);
            if (!read) read = child;
        }
        if (read) {
            read->transform = term.decl()(arguments).simplify();
            return read;
        }
        return std::nullopt;
    };
    // Z3 can turn ite(g,h(A(p)),h(A(q))) into Boolean combinations inside h.
    // Shannon-split only position predicates, then compare complete templates.
    auto same_transform = [&](const z3::expr& left, const z3::expr& right) {
        return z3::eq(left,right) || prove(ctx.bool_val(true),left == right);
    };
    auto certified_read = [&](const z3::expr& term,
                              const z3::expr& region) -> std::optional<ReadSum> {
        std::vector<z3::expr> search{term}, reads;
        std::set<unsigned> visited;
        while (!search.empty()) {
            auto child = search.back(); search.pop_back();
            if (!visited.insert(child.id()).second) continue;
            if (visited.size() > 128 || !child.is_app()) return std::nullopt;
            if (child.decl().decl_kind() == Z3_OP_UNINTERPRETED && child.num_args()) {
                if (child.num_args() != 1 || !child.is_int() || !child.arg(0).is_int() ||
                    !arithmetic(child.arg(0)) || finite_aggregates.contains(child.decl().id()) ||
                    reads.size() == 8 || (!reads.empty() &&
                    !z3::eq(reads.front().decl(),child.decl()))) return std::nullopt;
                reads.push_back(child);
            } else {
                for (const auto& arg : child.args()) search.push_back(arg);
            }
        }
        if (reads.empty()) return std::nullopt;
        z3::expr_vector from(ctx), to(ctx);
        for (const auto& read : reads) { from.push_back(read); to.push_back(element); }
        auto transform = term;
        transform = transform.substitute(from,to).simplify();
        from.resize(0); to.resize(0);
        from.push_back(i); to.push_back(j);
        auto renamed = transform;
        if (!z3::eq(transform,renamed.substitute(from,to))) return std::nullopt;
        // Collapsing reads only proposes a template. A pointwise certificate
        // must establish which single read actually supplies this region.
        for (const auto& read : reads) {
            from.resize(0); to.resize(0);
            from.push_back(element); to.push_back(read);
            auto value = transform;
            if (prove(region,term == value.substitute(from,to)))
                return ReadSum{read.decl(),read.arg(0),transform,ctx.int_val(0)};
        }
        return std::nullopt;
    };
    unsigned guard_splits = 7;
    std::function<std::optional<ReadSum>(const z3::expr&, unsigned, const z3::expr&)> elementwise =
        [&](const z3::expr& term, unsigned depth,
            const z3::expr& region) -> std::optional<ReadSum> {
        auto read = decompose(term);
        if (read) return read;
        read = certified_read(term,region);
        if (read || depth == 8 || !guard_splits) return read;
        std::vector<z3::expr> search{term};
        std::set<unsigned> visited;
        std::optional<z3::expr> guard;
        while (!search.empty() && visited.size() < 128) {
            auto candidate = search.back(); search.pop_back();
            if (!visited.insert(candidate.id()).second || !candidate.is_app()) continue;
            if (candidate.is_bool() && candidate.num_args() &&
                candidate.arg(0).is_int() && arithmetic(candidate)) {
                z3::expr_vector from(ctx), to(ctx);
                from.push_back(i); to.push_back(j);
                auto renamed = candidate;
                if (!z3::eq(candidate, renamed.substitute(from,to))) {
                    guard = candidate;
                    break;
                }
            }
            for (const auto& arg : candidate.args()) search.push_back(arg);
        }
        if (!guard) return std::nullopt;
        --guard_splits;
        z3::expr_vector from(ctx), yes_to(ctx), no_to(ctx);
        from.push_back(*guard);
        yes_to.push_back(ctx.bool_val(true)); no_to.push_back(ctx.bool_val(false));
        auto yes_term = term, no_term = term;
        const auto yes_region = region && *guard, no_region = region && !*guard;
        if (prove(yes_region,ctx.bool_val(false)))
            return elementwise(no_term.substitute(from,no_to).simplify(),depth+1,no_region);
        if (prove(no_region,ctx.bool_val(false)))
            return elementwise(yes_term.substitute(from,yes_to).simplify(),depth+1,yes_region);
        auto yes = elementwise(yes_term.substitute(from,yes_to).simplify(), depth+1,yes_region);
        auto no = elementwise(no_term.substitute(from,no_to).simplify(), depth+1,no_region);
        if (!yes || !no || !z3::eq(yes->array,no->array) ||
            !same_transform(yes->transform,no->transform)) return std::nullopt;
        return ReadSum{yes->array, z3::ite(*guard,yes->argument,no->argument).simplify(),
                       yes->transform,ctx.int_val(0)};
    };
    // Preserve the older sum-only rule: an additive positional term is summed
    // separately, not carried along with the permuted array element.
    std::function<std::optional<ReadSum>(const z3::expr&)> additive_read =
        [&](const z3::expr& term) -> std::optional<ReadSum> {
        if (!template_nodes || !term.is_app()) return std::nullopt;
        --template_nodes;
        if (term.decl().decl_kind() == Z3_OP_UNINTERPRETED && term.num_args() == 1 &&
            !finite_aggregates.contains(term.decl().id()) && term.is_int() &&
            term.arg(0).is_int() && arithmetic(term.arg(0))) {
            return ReadSum{term.decl(), term.arg(0), element, ctx.int_val(0)};
        }
        if (term.is_ite() && arithmetic(term.arg(0))) {
            auto yes = additive_read(term.arg(1)), no = additive_read(term.arg(2));
            if (yes && no && z3::eq(yes->array, no->array)) {
                return ReadSum{yes->array,
                    z3::ite(term.arg(0),yes->argument,no->argument).simplify(), element,
                    z3::ite(term.arg(0),yes->remainder,no->remainder).simplify()};
            }
        }
        if (term.decl().decl_kind() != Z3_OP_ADD) return std::nullopt;
        std::optional<ReadSum> read;
        auto remainder = ctx.int_val(0);
        for (const auto& arg : term.args()) {
            if (arithmetic(arg)) { remainder = remainder + arg; continue; }
            if (read) return std::nullopt;
            read = additive_read(arg);
            if (!read) return std::nullopt;
        }
        if (read) read->remainder = (read->remainder + remainder).simplify();
        return read;
    };
    struct Sum {
        z3::expr application;
        ReadSum read;
        std::optional<bool> permutation;
        const FiniteAggregate* definition;
    };
    std::vector<Sum> sums;
    std::set<unsigned> seen;
    pending = {expression};
    unsigned nodes = 4096;
    while (!pending.empty() && nodes--) {
        auto term = pending.back(); pending.pop_back();
        if (!seen.insert(term.id()).second || !term.is_app()) continue;
        auto found = finite_aggregates.find(term.decl().id());
        if (found != finite_aggregates.end() && sums.size() < 16) {
            auto body = found->second.body;
            z3::expr_vector from(ctx), to(ctx);
            from.push_back(found->second.index); to.push_back(i);
            for (unsigned p = found->second.combine ? 3 : 2; p < term.num_args(); ++p) {
                from.push_back(found->second.signature.arg(p)); to.push_back(term.arg(p));
            }
            body = body.substitute(from, to).simplify();
            const auto region = term.arg(0) <= i && i < term.arg(1);
            guard_splits = 7;
            auto read = elementwise(body, 0, region);
            if (read) {
                z3::expr_vector values(ctx), replacements(ctx);
                values.push_back(element); replacements.push_back(read->array(read->argument));
                auto value = read->transform;
                if (!prove(region,body == value.substitute(values,replacements))) read.reset();
            }
            if (!read && !found->second.product && !found->second.combine)
                read = additive_read(body);
            if (read) sums.push_back(Sum{term, *read, std::nullopt, &found->second});
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
    auto combine = [&](const FiniteAggregate& op, z3::expr x, z3::expr y) {
        z3::expr_vector from(ctx), to(ctx);
        from.push_back(*op.left); from.push_back(*op.right);
        to.push_back(x); to.push_back(y);
        auto result = *op.combine;
        return result.substitute(from, to).simplify();
    };
    std::map<unsigned, std::pair<z3::expr, bool>> commutative_associative, idempotent;
    const z3::expr k(ctx, Z3_mk_fresh_const(ctx, "fold_law_k", ctx.int_sort()));
    auto laws = [&](const FiniteAggregate& op, bool duplication) {
        auto canonical = combine(op, i, j);
        const auto key = canonical.id();
        if (!commutative_associative.contains(key)) {
            // Certify closed operators without borrowing program assumptions.
            auto saved_domain = domain;
            domain = ctx.bool_val(true);
            commutative_associative.emplace(key, std::make_pair(canonical,
                prove(ctx.bool_val(true), combine(op, combine(op,i,j),k) ==
                                           combine(op,i,combine(op,j,k))) &&
                prove(ctx.bool_val(true), canonical == combine(op,j,i))));
            domain = saved_domain;
        }
        if (!commutative_associative.at(key).second) return false;
        if (!duplication) return true;
        if (!idempotent.contains(key)) {
            auto saved_domain = domain;
            domain = ctx.bool_val(true);
            idempotent.emplace(key, std::make_pair(canonical,
                prove(ctx.bool_val(true), combine(op,i,i) == i)));
            domain = saved_domain;
        }
        return idempotent.at(key).second;
    };
    auto compatible = [&](Sum& source, Sum& target) {
        const auto& op = *source.definition;
        const auto& other = *target.definition;
        if (op.product != other.product || bool(op.combine) != bool(other.combine)) return false;
        if (!op.combine) return true;
        // Matching seeds are sufficient; neither seed need be an identity.
        return z3::eq(combine(op,i,j), combine(other,i,j)) && laws(op, false) &&
            prove(ctx.bool_val(true), source.application.arg(2) == target.application.arg(2));
    };
    // An ITE tree is an exhaustive, disjoint partition by construction. Each
    // leaf must independently cover the reference interval exactly once.
    auto partition_multiple = [&](Sum& source, Sum& target) {
        if (!source.read.argument.is_ite()) return;
        const auto& op = *source.definition;
        if (op.combine && !laws(op, true)) return;
        if (!prove(in_range(i, source.application), source.read.remainder == 0) ||
            !prove(in_range(i, target.application), target.read.remainder == 0) ||
            !prove(in_range(i, target.application), target.read.argument == i)) return;
        struct Piece { z3::expr guard, map; };
        std::vector<Piece> pieces;
        unsigned budget = 32;
        std::function<bool(z3::expr, z3::expr)> split =
            [&](z3::expr map, z3::expr guard) {
                if (!budget--) return false;
                if (map.is_ite()) {
                    return split(map.arg(1), guard && map.arg(0)) &&
                           split(map.arg(2), guard && !map.arg(0));
                }
                pieces.push_back({guard, map});
                return true;
            };
        if (!split(source.read.argument, in_range(i, source.application)) ||
            pieces.size() < 2 || pieces.size() > 8) return;
        for (auto& piece : pieces) {
            z3::expr_vector from(ctx), to(ctx);
            from.push_back(i); to.push_back(j);
            auto guard_j = piece.guard, map_j = piece.map;
            guard_j = guard_j.substitute(from, to);
            map_j = map_j.substitute(from, to);
            // Quantified coverage avoids guessing an inverse. UNKNOWN is a
            // failed certificate, never permission to add an identity.
            if (!prove(piece.guard, in_range(piece.map, target.application)) ||
                !prove(piece.guard && guard_j && piece.map == map_j, i == j) ||
                !prove(in_range(j, target.application),
                       z3::exists(i, piece.guard && piece.map == j))) return;
        }
        auto value = target.application;
        if (!op.combine) {
            for (size_t p=1; p<pieces.size(); ++p)
                value = op.product ? value * target.application : value + target.application;
        }
        identities.push_back(source.application == value);
    };
    for (unsigned a = 0; a < sums.size(); ++a) {
        for (unsigned b = a + 1; b < sums.size(); ++b) {
            auto& left = sums[a]; auto& right = sums[b];
            if (!z3::eq(left.read.array, right.read.array) ||
                !same_transform(left.read.transform, right.read.transform) ||
                !compatible(left, right)) continue;
            partition_multiple(left, right);
            partition_multiple(right, left);
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
