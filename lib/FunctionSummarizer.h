//--------------------- FunctionSummarizer.h ---------------------
//
// This file contains the FunctionSummarizer class, which is used to summarize
// recursion in LLVM IR. It uses a recurrence solver to analyze the
// function and generate a summary of its behavior.
//
//----------------------------------------------------------------

#ifndef FUNCTIONSUMMARIZER_H
#define FUNCTIONSUMMARIZER_H

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
#include "FunctionSummary.h"

namespace ari_exe {
    class State;
    class FunctionSummary;
    // A tiny SE engine to symbolic execute a recursion
    // to get recurrence for the function
    class RecExecution {
        public:
            enum TestResult {
                F_FEASIBLE,
                F_UNFEASIBLE,
                F_TESTUNKNOWN,
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

    class FunctionSummarizer {
        public:
            // function_summary(): F(nullptr), llvm2z3(F), rec_s(llvm2z3.get_context()) {}
            FunctionSummarizer() = delete;
            FunctionSummarizer(llvm::Function* F, z3::context& _z3ctx);
            // function_summary(const function_summary& other);
            // function_summary operator=(const function_summary& other);
            std::optional<FunctionSummary> get_summary();
            
            // get the function application in z3 based on the function signature
            z3::expr function_app_z3(llvm::Function* f);

        private:
            llvm::Function* F;
            rec_solver rec_s;
            std::optional<FunctionSummary> summary;
            void summarize();
    };
}

#endif