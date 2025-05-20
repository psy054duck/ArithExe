#ifndef FUNCTIONSUMMARY_H
#define FUNCTIONSUMMARY_H

#include "z3++.h"

namespace ari_exe {

    // Store the summary of the function
    class FunctionSummary {

        public:
            FunctionSummary() = delete;
            FunctionSummary(const z3::expr_vector& params, const z3::expr& summary);
            FunctionSummary(const FunctionSummary& other);
            
            // evaluate the function with the given arguments
            z3::expr evaluate(const z3::expr_vector& args);

        private:
            // formal parameters of the function
            z3::expr_vector params;

            // function value
            z3::expr summary;
    };
}

#endif