//--------------------------- LoopSummarizer.h ---------------------------
//
// This file contains the LoopSummarizer class, which is used to summarize
// loops in LLVM IR. It uses a recurrence solver to analyze the
// loop and generate a summary of its behavior.
//
//-------------------------------------------------------------------------

#ifndef LOOPSUMMARIZER_H
#define LOOPSUMMARIZER_H

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
#include "llvm/ADT/SmallVector.h"

#include "z3++.h"

#include "rec_solver.h"
#include "AnalysisManager.h"
#include "state.h"
#include "LoopSummary.h"
#include "FunctionSummarizer.h"
#include "common.h"
#include "logics.h"

namespace ari_exe {
    class LoopSummary;
    class State;
    class LoopState;

    // A tiny SE engine to symbolic execute a loop
    // to get recurrence for the loop
    class LoopExecution {
        public:
            enum TestResult {
                L_FEASIBLE,
                L_UNFEASIBLE,
                L_TESTUNKNOWN,
            };
            LoopExecution(llvm::Loop* loop, state_ptr parent_state);
            ~LoopExecution();
    
            // execute one instruction
            loop_state_list step(loop_state_ptr state);
    
            // build the initial state
            // assuming the function do not access global variables
            loop_state_ptr build_initial_state();

            /**
             * @brief  execute the loop body
             * @return a list of final states, which are all states arriving at the
             *         the end of latch.
             *         a list of exit states, which are all states arriving at the
             *         exit blocks
             */
            std::pair<loop_state_list, loop_state_list> run();

            /**
             * @brief only run the header of the loop
             */
            LoopState run_header(loop_state_ptr state);

            /**
             * @brief Test the feasibility of the current state
             * @param state The current state
             */
            TestResult test(loop_state_ptr state);

            /**
             * @brief get the value modified by the instruction
             */
            llvm::Value* get_modified_value(state_ptr state, llvm::Instruction* inst);


        private:
            /**
             * @brief Check if the current state is a final state, which is a state
             *        arriving at the end of the latch
             * @param state The current state
             * @return true if the current state is a final state
             */
            bool is_final_state(loop_state_ptr state);

            /**
             * @brief Check if the given state is an exit state
             * @param state The current state
             * @return true if the current state is an exit state
             */
            bool is_exit_state(loop_state_ptr state);

            /**
             * @brief check if this state just took the back edge
             * @param state The current state
             * @return true if the current state just took the back edge
             */
            bool back_edge_taken(loop_state_ptr state);

            /**
             * @brief get all values that modified by this loop,
             *        for which wee need to summarize.
             */
            std::vector<llvm::Value*> get_scalar_modified_values();

            llvm::Loop* loop;

            z3::solver solver;
    
            std::queue<loop_state_ptr> states;

            // parent state, in which this loop execution is invoked
            state_ptr parent_state;

            // loop invariants encountered in the loop
            // must verify them if summarization succeeds
            z3::expr_vector v_conditions;
    };

    /**
     * @brief This class is in charge of summarizing loops.
     */
    class LoopSummarizer {
        public:
            LoopSummarizer() = delete;
            LoopSummarizer(const LoopSummarizer& other);

            /**
             * @brief Construct a new LoopSummary object
             * @param loop The loop to be summarized
             * @param z3ctx The Z3 context
             */
            LoopSummarizer(llvm::Loop* loop, state_ptr parent_state): loop(loop), rec_s(AnalysisManager::get_instance()->get_z3ctx()), parent_state(parent_state) {};

            std::optional<LoopSummary> get_summary();

        private:
            llvm::Loop* loop;
            rec_solver rec_s;
            std::optional<LoopSummary> summary;

            /**
             * @brief get the update for each header phi in the given final state
             * @param state The final state
             * @return A list values corresponding to the header phis respectively
             */
            std::vector<z3::expr> get_update(loop_state_ptr state);
            
            /**
             * @brief Summarize the loop. This is where recurrence solving happens.
             */
            void summarize();

            /**
             * @brief Summarize the loop for scalar values.
             */
            void summarize_scalar(const loop_state_list& final_states, const loop_state_list& exit_states);

            /**
             * @brief Summarize the loop for array values.
             */
            void summarize_array(const loop_state_list& final_states, const loop_state_list& exit_states);

            /**
             * @brief Get the z3 function used in the recurrence for an array.
             *        Thus, this function is added another parameter to denote
             *        the loop counter.
             */
            z3::func_decl get_array_rec_func(const MemoryObjectArrayPtr array);

            /**
             * @brief get base case for array summarization
             * @return A pair of (condition , transition)
             */
            std::pair<z3::expr, rec_ty> get_array_base_case(const MemoryObjectArrayPtr array);

            /**
             * @brief convert a finial state to recursive case
             * @return A pair of (condition , transition)
             */
            std::pair<std::vector<z3::expr>, std::vector<rec_ty>> get_array_recursive_case(loop_state_ptr final_state, const MemoryObjectArrayPtr array);

            /**
             * @brief solve recurrence
             */
            closed_form_ty solve_recurrence(const std::vector<z3::expr>& conditions, const std::vector<rec_ty>& eqs);

            /**
             * @brief get the frame case for array summarization
             * @return A pair of (condition , transition)
             */
            std::pair<z3::expr, rec_ty> get_array_frame_case(std::vector<z3::expr> conditions, const MemoryObjectArrayPtr array);

            /**
             * @brief get all header phis in order
             */
            std::vector<llvm::PHINode*> get_header_phis();

            /**
             * @brief get constraints on the number of iterations of the loop
             * @param exit_states The exit states
             * @param params header phis
             * @param values The closed-form solution to the header phis
             * @return constraints on the number of iterations of the loop and the symbol used to denote the number of iterations
             */
            std::pair<z3::expr, z3::expr> get_iterations_constraints(const loop_state_list& exit_states, const z3::expr_vector& params, const z3::expr_vector& values);

            /**
             * @brief get exit conditions for the loop
             */
            std::pair<z3::expr, z3::expr> get_exit_loop_guard(const loop_state_list& exit_states);


            /**
             * @brief get the loop guard condition
             * @param exit_states The exit states
             * @return The loop guard condition
             */
            z3::expr get_loop_guard_condition(const loop_state_list& exit_states);

            /**
             * @brief get scalar version and function version of header phis
             */
            std::pair<z3::expr_vector, z3::expr_vector> get_header_phis_scalar_and_func();

            /**
             * @brief get llvm values that are modified by the loop and their z3 functions
             */
            std::pair<z3::expr_vector, z3::expr_vector> get_modified_values_and_functions();

            /**
             * @brief get array modified statements
             */
            std::vector<llvm::StoreInst*> get_array_modified_stmts();

            /**
             * @brief extract conditions and update from final states
             */
            std::pair<std::vector<z3::expr>, std::vector<rec_ty>> get_conditions_and_updates(const loop_state_list& final_states);

            /**
             * @brief get final and exit states
             * @return (final_states, exit_states)
             */
            std::pair<loop_state_list, loop_state_list> get_final_and_exit_states();

            /**
             * @brief get initial values for the loop
             */
            initial_ty get_initial_values();

            /**
             * @brief get predecessor of the loop
             */
            llvm::BasicBlock* get_predecessor();

            /**
             * @brief parent state, in which this loop summarization is invoked
             */
            state_ptr parent_state;

            /**
             * @brief log info
             */
            void log_states(const loop_state_list& final_states, const loop_state_list& exit_states);

            /**
             * @brief check if some object is invariant in the loop
             */
            bool is_invariant(const loop_state_ptr final_state, llvm::Value* v) const;
    };
}

#endif
