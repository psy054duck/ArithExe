#include "LoopSummary.h"
#include "VerificationSession.h"

using namespace ari_exe;

LoopSummary::LoopSummary(const z3::expr_vector& params, const z3::expr_vector& summary, const z3::expr_vector& closed_form, const z3::expr& constraints, const std::vector<llvm::Value*>& modified_values, std::optional<z3::expr> N): \
                         params(params), summary_exact(summary), summary_closed_form(closed_form), auxiliary_params(params.ctx()), auxiliary_closed_forms(params.ctx()), is_over_approx(false), constraints(constraints), modified_values(modified_values), N(N) {}

// LoopSummary::LoopSummary(const z3::expr_vector& params, const closed_form_ty& summary, const z3::expr& constraints, const std::vector<llvm::Value*>& modified_values, std::optional<z3::expr> N): params(params), summary_exact(summary), summary_closed_form(params.ctx()), summary_over_approx(summary), is_over_approx(true), constraints(constraints), modified_values(modified_values), N(N) {}
LoopSummary::LoopSummary(const z3::expr_vector& params, const z3::expr_vector& exact_summary, const z3::expr_vector& closed_form, const closed_form_ty& over_approximated, const z3::expr& constraints, std::optional<z3::expr> N):
                         params(params), summary_exact(exact_summary), summary_closed_form(closed_form), auxiliary_params(params.ctx()), auxiliary_closed_forms(params.ctx()), summary_over_approx(over_approximated), is_over_approx(true), constraints(constraints), N(N) {}

LoopSummary::LoopSummary(const LoopSummary& other): exact_feasibility(other.exact_feasibility), params(other.params), summary_exact(other.summary_exact), summary_closed_form(other.summary_closed_form), auxiliary_params(other.auxiliary_params), auxiliary_closed_forms(other.auxiliary_closed_forms), summary_over_approx(other.summary_over_approx), is_over_approx(other.is_over_approx), constraints(other.constraints), modified_values(other.modified_values), preserved_values(other.preserved_values), N(other.N), functional_iteration_count(other.functional_iteration_count), invariant_results(other.invariant_results), obligations(other.obligations) {}

z3::expr_vector
LoopSummary::evaluate(const z3::expr_vector& args) {
    z3::expr_vector result(args.ctx());
    if (is_over_approx) {
        for (auto& v : summary_over_approx) {
            result.push_back(v.first == v.second.substitute(auxiliary_params, auxiliary_closed_forms).substitute(params, args));
        }
        return result;
    }
    for (auto v : summary_closed_form) {
        result.push_back(v.substitute(auxiliary_params, auxiliary_closed_forms).substitute(params, args));
    }
    return result;
}

z3::expr
LoopSummary::evaluate_expr(z3::expr expr) const {
    return expr.substitute(auxiliary_params, auxiliary_closed_forms).substitute(params, summary_closed_form);
}

void
LoopSummary::add_closed_form(const z3::expr& param, const z3::expr& closed_form) {
    params.push_back(param);
    summary_closed_form.push_back(closed_form);
}

void
LoopSummary::add_auxiliary_closed_form(const z3::expr& param, const z3::expr& closed_form) {
    auxiliary_params.push_back(param);
    auxiliary_closed_forms.push_back(closed_form);
}
