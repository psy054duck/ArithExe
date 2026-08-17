#ifndef FUNCTIONSUMMARY_H
#define FUNCTIONSUMMARY_H

#include "z3++.h"
#include "Expr.h"
#include "rec_solver.h"

namespace ari_exe {

    // Store the summary of the function
    class FunctionSummary {

        public:
            FunctionSummary() = delete;
            FunctionSummary(const z3::expr_vector& params, const z3::expr& summary);
            FunctionSummary(const FunctionSummary& other);
            FunctionSummary(const z3::expr_vector& params,
                            const closed_form_ty& summary_over_approx,
                            z3::expr exit_condition,
                            z3::expr iteration_condition,
                            const z3::expr_vector& relation_initial_params,
                            const z3::expr_vector& relation_final_params,
                            z3::expr relational_condition);
            
            // evaluate the function with the given arguments
            Expression evaluate(const std::vector<Expression>& args);

            closed_form_ty get_over_approx() const {
                return summary_over_approx;
            }

            z3::expr_vector get_params() const {
                return params;
            }

            z3::expr get_summary() const { return summary.as_expr(); }

            bool is_over_approximated() const {
                return !summary_over_approx.empty() ||
                       relational_condition.has_value();
            }

            z3::expr get_exit_condition() const {
                return exit_condition.value_or(params.ctx().bool_val(true));
            }

            z3::expr get_iteration_condition() const {
                return iteration_condition.value_or(params.ctx().bool_val(true));
            }

            z3::expr_vector get_relation_initial_params() const {
                return relation_initial_params;
            }

            z3::expr_vector get_relation_final_params() const {
                return relation_final_params;
            }

            z3::expr get_relational_condition() const {
                return relational_condition.value_or(
                    params.ctx().bool_val(true));
            }

        private:
            // formal parameters of the function
            z3::expr_vector params;

            // function value
            Expression summary;

            closed_form_ty summary_over_approx;

            std::optional<z3::expr> exit_condition;
            std::optional<z3::expr> iteration_condition;
            z3::expr_vector relation_initial_params{params.ctx()};
            z3::expr_vector relation_final_params{params.ctx()};
            std::optional<z3::expr> relational_condition;
    };
}

#endif
