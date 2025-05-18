#include "function_summary_utils.h"

using namespace ari_exe;

z3::expr
Summary::evaluate(const z3::expr_vector& args) {
    assert(args.size() == params.size());
    z3::expr result = summary.substitute(params, args);
    return result;
}

Summary::Summary(const z3::expr_vector& params, const z3::expr& summary): params(params), summary(summary) {}

Summary::Summary(const Summary& other): params(other.params), summary(other.summary) {}