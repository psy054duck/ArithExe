#ifndef LOOPSUMMARY_H
#define LOOPSUMMARY_H

#include "z3++.h"

#include "llvm/IR/Instructions.h"

#include <utility>
#include <map>
#include "rec_solver.h"

namespace ari_exe {

    /**
     * @brief Store the summary of the Loop
     */
    class LoopSummary {
        public:
            LoopSummary() = delete;
            
            /**
             * @brief Construct an exact LoopSummary object
             * @param params The parameters of the loop, which are header phis.
             *               When treating the loop as recurrence, the parameters are
             *               are symbolic initial values of the recurrence.
             * @param summary The exact summary of the loop
             */
            LoopSummary(const z3::expr_vector& params, const z3::expr_vector& summary, const z3::expr_vector& closed_form, const z3::expr& constraints, const std::vector<llvm::Value*>& modified_values, std::optional<z3::expr> N);

            LoopSummary(const z3::expr_vector& params, const z3::expr_vector& exact_summary, const z3::expr_vector& closed_form, const closed_form_ty& over_approximated, const z3::expr& constraints, std::optional<z3::expr> N);

            // This is used for over-approximated loop summaries
            // LoopSummary(const z3::expr_vector& params, const closed_form_ty& summary, const z3::expr& constraints, const std::vector<llvm::Value*>& modified_values, std::optional<z3::expr> N);

            LoopSummary(const LoopSummary& other);
            
            /**
             * @brief Evaluate the loop with the given arguments
             * @param args The arguments to evaluate the loop
             * @return if is_over_approx is false, return  values of all variables
             *         involved in the loop after executing the loop;
             *         if is_over_approx is true, return polynomial equalities.
             */
            z3::expr_vector evaluate(const z3::expr_vector& args);

            /**
             * @brief evaluate the given z3 expression, which is represented in terms of params,
             *        using the closed-form solutions.
             */
            z3::expr evaluate_expr(z3::expr expr) const;

            /**
             * @brief get modified values by the loop
             */
            std::vector<llvm::Value*> get_modified_values() const {
                return modified_values;
            }

            /**
             * @brief set the modified values by the loop
             */
            void set_modified_values(const std::vector<llvm::Value*>& values) {
                modified_values = values;
            }

            /**
             * @brief add modified value by the loop
             */
            void add_modified_value(llvm::Value* value) {
                modified_values.push_back(value);
            }

            /**
             * @brief get the number of iterations of the loop
             */
            std::optional<z3::expr> get_N() const { return N; }

            /**
             * @brief add new closed-form solution to the loop summary
             */
            void add_closed_form(const z3::expr& param, const z3::expr& closed_form);

            /**
             * @brief check if the loop summary is over-approximated
             */
            bool is_over_approximated() const { return is_over_approx; }

            /**
             * @brief set the loop summary to be over-approximated
             */
            void set_over_approximated(bool over_approx) { is_over_approx = over_approx; }

            z3::expr get_constraints() const { return constraints; }

            /**
             * @brief The over-approximated summary of the loop
             * @details if is_over_approx is true, which means the loop summary is
             *          over-approximated, then the summary are closed-form solutions
             *          polynomials of params which satisfy C-finite recurrences.
             *          ret.first is the polynomial, and ret.second is the corresponding
             *          closed-form solution.
             */
            closed_form_ty summary_over_approx;

        private:
            /**
             * @brief The parameters of the loop, which are header phis and global variables modified by the loop.
             *        When treating the loop as recurrence, the parameters are
             *        symbolic initial values of the recurrence.
             */
            z3::expr_vector params;

            /**
             * @brief LLVM values that are modified by the loop.
             *        It consists of header phis, stack alloca, heap malloc, and global variables modified by the loop.
             */
            std::vector<llvm::Value*> modified_values;

            /**
             * @brief The exact summary of the loop, which is exit value
             * @details if is_over_approx is false, which means
             *          the loop summary is exact, then the summary expressions 
             *          representing closed-form solutions to each param in params (loop variables);
             */
            z3::expr_vector summary_exact;

            /**
             * @brief the closed-form solutions of the loop
             */
            z3::expr_vector summary_closed_form;


            /**
             * constraints of this loop, which may contains constraints on the number of iterations
             * should be included in the path condition of parent state's
             */
            z3::expr constraints;

            /**
             * @brief the number of iterations of the loop
             */
            std::optional<z3::expr> N;

            /**
             * @brief indicates whether the loop is over-approximated or not.
             */
            bool is_over_approx;

    };
}
#endif