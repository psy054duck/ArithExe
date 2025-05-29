#include "LoopSummary.h"

using namespace ari_exe;

LoopSummary::LoopSummary(const z3::expr_vector& params, const z3::expr_vector& summary, const z3::expr& constraints, const std::vector<llvm::Value*>& modified_values): params(params), summary_exact(summary), is_over_approx(false), constraints(constraints), modified_values(modified_values) {}

LoopSummary::LoopSummary(const z3::expr_vector& params, const std::map<z3::expr, z3::expr>& summary, const z3::expr& constraints, const std::vector<llvm::Value*>& modified_values): params(params), summary_exact(params.ctx()), summary_over_approx(summary), is_over_approx(true), constraints(constraints), modified_values(modified_values) {}

LoopSummary::LoopSummary(const LoopSummary& other): params(other.params), summary_exact(other.summary_exact), summary_over_approx(other.summary_over_approx), is_over_approx(other.is_over_approx), constraints(other.constraints), modified_values(other.modified_values) {}

z3::expr_vector
LoopSummary::evaluate(const z3::expr_vector& args) {
    assert(args.size() == params.size());
    z3::expr_vector result(args.ctx());
    if (is_over_approx) {
        for (auto& v : summary_over_approx) {
            result.push_back(v.first == v.second.substitute(params, args));
        }
        return result;
    }
    for (auto v : summary_exact) {
        result.push_back(v.substitute(params, args));
    }
    return result;
}