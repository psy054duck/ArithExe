#pragma once
#include <z3++.h>

namespace ari_exe {
inline bool query_has_quantifier(const z3::expr& expression) {
    if (expression.is_quantifier()) return true;
    if (!expression.is_app()) return false;
    for (unsigned i = 0; i < expression.num_args(); ++i)
        if (query_has_quantifier(expression.arg(i))) return true;
    return false;
}

// Called only after the original solver returns unknown. Accept only UNSAT:
// SAT from a separate tactic solver would require transferring/validating its
// model before callers could use the original solver's get_model(). Preserve
// the original solver, assertions, parameters, and incremental history.
inline z3::check_result quantified_unsat_fallback(z3::solver& original,
                                                unsigned timeout_ms) {
    const auto assertions = original.assertions();
    bool quantified = false;
    for (unsigned i = 0; i < assertions.size(); ++i)
        quantified |= query_has_quantifier(assertions[i]);
    if (!quantified || !timeout_ms) return z3::unknown;
    try {
        auto& context = original.ctx();
        auto strategy = z3::tactic(context, "simplify") &
                        z3::tactic(context, "qe") &
                        z3::tactic(context, "smt");
        auto fallback = z3::try_for(strategy, timeout_ms).mk_solver();
        fallback.add(assertions);
        const auto result = fallback.check();
        if (result == z3::unsat) return result;
    } catch (const z3::exception&) {
        // Timeout/unsupported tactics are inconclusive, never a proof.
    }
    return z3::unknown;
}
}
