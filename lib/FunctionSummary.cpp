#include "FunctionSummary.h"

using namespace ari_exe;

z3::expr
FunctionSummary::evaluate(const z3::expr_vector& args) {
    assert(args.size() == params.size());
    z3::expr result = summary.substitute(params, args);
    return result;
}

FunctionSummary::FunctionSummary(const z3::expr_vector& params, const z3::expr& summary): params(params), summary(summary) {}

FunctionSummary::FunctionSummary(const FunctionSummary& other): params(other.params), summary(other.summary) {}