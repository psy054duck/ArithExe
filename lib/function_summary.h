#ifndef FUNCTION_SUMMARY
#define FUNCTION_SUMMARY

#include <vector>
#include <optional>
#include <queue>

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "z3++.h"

#include "rec_solver.h"
#include "state.h"
#include "function_summary_utils.h"

namespace ari_exe {
    class State;
    class Summary;
    // A tiny SE engine to symbolic execute a recursion
    // to get recurrence for the function
    class RecExecution {
        public:
            enum TestResult {
                FEASIBLE,
                UNFEASIBLE,
                TESTUNKNOWN,
            };
            RecExecution(z3::context& z3ctx, llvm::Function* F);
            ~RecExecution();
    
            // execute one instruction
            std::vector<std::shared_ptr<State>> step(std::shared_ptr<State> state);
    
            // execute the function
            // return a list of final states
            std::vector<std::shared_ptr<State>> run();
    
            // build the initial state
            // assuming the function do not access global variables
            std::shared_ptr<State> build_initial_state();

            /**
             * @brief Test the feasibility of the current state
             * @param state The current state
             */
            TestResult test(std::shared_ptr<State> state);

        private:
            z3::context& z3ctx;
            llvm::Function* F;

            z3::solver solver;
    
            std::queue<std::shared_ptr<State>> states;
    };

    class function_summary {
        public:
            // function_summary(): F(nullptr), llvm2z3(F), rec_s(llvm2z3.get_context()) {}
            function_summary() = delete;
            function_summary(llvm::Function* F, z3::context& _z3ctx);
            // function_summary(const function_summary& other);
            // function_summary operator=(const function_summary& other);
            std::optional<Summary> get_summary();
            
            // get the function application in z3 based on the function signature
            z3::expr function_app_z3(llvm::Function* f);

        private:
            llvm::Function* F;
            rec_solver rec_s;
            std::optional<Summary> summary;
            void summarize();
    };
}

#endif