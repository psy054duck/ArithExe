#ifndef FUNCTIONSUMMARY_H
#define FUNCTIONSUMMARY_H

#include "z3++.h"
#include "Expr.h"

namespace ari_exe {

    // Store the summary of the function
    class FunctionSummary {

        public:
            FunctionSummary() = delete;
            FunctionSummary(const z3::expr_vector& params, const z3::expr& summary);
            FunctionSummary(const FunctionSummary& other);
            
            // evaluate the function with the given arguments
            Expression evaluate(const std::vector<Expression>& args);

        private:
            // formal parameters of the function
            z3::expr_vector params;

            // function value
            Expression summary;
    };
}

#endif