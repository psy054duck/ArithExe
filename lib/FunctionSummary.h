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
            FunctionSummary(const z3::expr_vector& params, const closed_form_ty& summary_over_approx, z3::expr exit_condition);
            
            // evaluate the function with the given arguments
            Expression evaluate(const std::vector<Expression>& args);

            closed_form_ty get_over_approx() const {
                return summary_over_approx;
            }

            z3::expr_vector get_params() const {
                return params;
            }

            bool is_over_approximated() const {
                return !summary_over_approx.empty();
            }

            z3::expr get_exit_condition() const {
                return exit_condition.value_or(params.ctx().bool_val(true));
            }

        private:
            // formal parameters of the function
            z3::expr_vector params;

            // function value
            Expression summary;

            closed_form_ty summary_over_approx;

            std::optional<z3::expr> exit_condition;
    };
}

#endif