#include "FunctionSummary.h"

using namespace ari_exe;

Expression
FunctionSummary::evaluate(const std::vector<Expression>& args) {
    assert(args.size() == params.size());
    auto result = summary.subs(params, args);
    return result;
}

FunctionSummary::FunctionSummary(const z3::expr_vector& params, const z3::expr& summary): params(params), summary(summary) {}

FunctionSummary::FunctionSummary(const FunctionSummary& other): params(other.params), summary(other.summary), summary_over_approx(other.summary_over_approx), exit_condition(other.exit_condition), iteration_condition(other.iteration_condition), relation_initial_params(other.relation_initial_params), relation_final_params(other.relation_final_params), relational_condition(other.relational_condition) {}

FunctionSummary::FunctionSummary(
    const z3::expr_vector& params,
    const closed_form_ty& summary_over_approx,
    z3::expr exit_condition,
    z3::expr iteration_condition,
    const z3::expr_vector& relation_initial_params,
    const z3::expr_vector& relation_final_params,
    z3::expr relational_condition)
    : params(params), summary_over_approx(summary_over_approx),
      exit_condition(exit_condition), iteration_condition(iteration_condition),
      relation_initial_params(relation_initial_params),
      relation_final_params(relation_final_params),
      relational_condition(relational_condition) {}
