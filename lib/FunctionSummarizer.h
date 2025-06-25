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
#include <algorithm>

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
#include "common.h"
#include "logics.h"

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
            rec_state_list step(state_ptr state, bool unfold=true);
    
            /**
             * @brief run the function symbolically and get the final states
             * @param unfold If true, unfold nested calls
             */
            rec_state_list run(bool unfold=true);

            // build the initial state
            // assuming the function do not access global variables
            rec_state_ptr build_initial_state();

            /**
             * @brief Test the feasibility of the current state
             * @param state The current state
             */
            TestResult test(state_ptr state);

        private:
            z3::context& z3ctx;
            llvm::Function* F;

            z3::solver solver;
    
            std::queue<rec_state_ptr> states;
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
            z3::context& z3ctx;
            std::optional<FunctionSummary> summary;
            void summarize();

            // This is used for summarizing void function with pointer parameters
            // return true on success
            bool summarize_pointers();

            // This is used for summarizing functions with scalar parameters
            void summarize_scalar();

            // Check if all branches of the function are either base cases or tail calls
            bool is_tail_call(llvm::Function* f, const std::set<llvm::Function*>& SCC);

            // Check if all functions in the SCC are (mutually) tail recursive
            bool is_tail_recursive(const std::set<llvm::Function*>& SCC);

            bool is_base_case_final_state(const rec_state_ptr& state, const std::set<llvm::Function*>& SCC);

            std::pair<rec_state_list, rec_state_list> split_cases(const rec_state_list& final_states, const std::set<llvm::Function*>& SCC);

            rec_solver prepare_rec_solver();

            std::vector<llvm::Instruction*> combine_trace(const trace_ty& trace);

            llvm::Instruction* get_last_meaningful_inst(const trace_ty& trace);

            rec_solver prepare_rec_solver_unfold();
            std::optional<rec_solver> prepare_rec_solver_naive();
            bool is_base_case(const z3::expr_vector& keys, z3::expr cond);

            std::pair<std::vector<z3::expr>, std::vector<rec_ty>>
            parse_rec(const rec_state_list& final_states, llvm::Function* f);
    };
}

#endif