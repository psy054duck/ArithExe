#ifndef LOOPSUMMARY_H
#define LOOPSUMMARY_H

#include "z3++.h"

#include "llvm/IR/Instructions.h"

#include <utility>
#include <map>

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
            LoopSummary(const z3::expr_vector& params, const z3::expr_vector& summary, const z3::expr& constraints, const std::vector<llvm::Value*>& modified_values);
            LoopSummary(const z3::expr_vector& params, const std::map<z3::expr, z3::expr>& summary, const z3::expr& constraints, const std::vector<llvm::Value*>& modified_values);
            
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

        private:
            /**
             * @brief The parameters of the loop, which are header phis and global variables modified by the loop.
             *        When treating the loop as recurrence, the parameters are
             *        symbolic initial values of the recurrence.
             */
            z3::expr_vector params;

            /**
             * @brief LLVM values that are modified by the loop.
             */
            std::vector<llvm::Value*> modified_values;

            /**
             * @brief The exact summary of the loop
             * @details if is_over_approx is false, which means
             *          the loop summary is exact, then the summary expressions 
             *          representing closed-form solutions to each param in params (loop variables);
             */
            z3::expr_vector summary_exact;

             /**
             * @brief The over-approximated summary of the loop
             * @details if is_over_approx is true, which means the loop summary is
             *          over-approximated, then the summary are closed-form solutions
             *          polynomials of params which satisfy C-finite recurrences.
             *          ret.first is the polynomial, and ret.second is the corresponding
             *          closed-form solution.
             */
            std::map<z3::expr, z3::expr> summary_over_approx;

            /**
             * constraints of this loop, which may contains constraints on the number of iterations
             * should be included in the path condition of parent state's
             */
            z3::expr constraints;

            /**
             * @brief indicates whether the loop is over-approximated or not.
             */
            bool is_over_approx;

    };
}
#endif