#include "FunctionSummary.h"

using namespace ari_exe;

Expression
FunctionSummary::evaluate(const std::vector<Expression>& args) {
    assert(args.size() == params.size());
    auto result = summary.subs(params, args);
    return result;
}

FunctionSummary::FunctionSummary(const z3::expr_vector& params, const z3::expr& summary): params(params), summary(summary) {}

FunctionSummary::FunctionSummary(const FunctionSummary& other): params(other.params), summary(other.summary), summary_over_approx(other.summary_over_approx), exit_condition(other.exit_condition) {}

FunctionSummary::FunctionSummary(const z3::expr_vector& params, const closed_form_ty& summary_over_approx, z3::expr exit_condition): params(params), summary_over_approx(summary_over_approx), exit_condition(exit_condition) {}