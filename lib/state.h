#ifndef STATE_H
#define STATE_H

#include <map>
#include <vector>
#include <set>

#include "llvm/IR/Instruction.h"
#include "llvm/IR/Constants.h"

#include "z3++.h"

#include "Memory.h"
#include "Expr.h"

namespace ari_exe {
    class AInstruction;
    class AStack;
    class State;
    class LoopState;
    class RecState;

    using trace_ty = std::vector<llvm::BasicBlock*>;

    template<typename state_ty>
    using state_ptr_base = std::shared_ptr<state_ty>;

    using state_ptr = state_ptr_base<State>;
    using loop_state_ptr = state_ptr_base<LoopState>;
    using rec_state_ptr = state_ptr_base<RecState>;

    template<typename state_ty> 
    using state_list_base = std::vector<state_ptr_base<state_ty>>;

    using state_list = state_list_base<State>;
    using loop_state_list = state_list_base<LoopState>;
    using rec_state_list = state_list_base<RecState>;
}

#include "AInstruction.h"
#include "SymbolTable.h"
#include "MStack.h"
#include "FunctionSummary.h"
#include "LoopSummary.h"

namespace ari_exe {
    class State {
        private:
            // path condition collected so far
            Expression path_condition;

        public:
            enum Status {
                RUNNING,        // normal status
                TERMINATED,     // reach the end of the program
                VERIFYING,      // a verification condition is being generated
                TESTING,        // a new branch, should check the feasibility
                REACH_ERROR,    // reach an error state (call reach_error())
                UNKNOWN,
                FAIL,
            };

        public:
            State(z3::context& z3ctx, AInstruction* pc, AInstruction* prev_pc, const Memory& memory, const Expression& path_condition, const trace_ty& trace, Status status = RUNNING): z3ctx(z3ctx), pc(pc), prev_pc(prev_pc), memory(memory), path_condition(path_condition), trace(trace), status(status) {};
            State(const State& state): z3ctx(state.z3ctx), pc(state.pc), prev_pc(state.prev_pc), memory(state.memory), path_condition(state.path_condition), trace(state.trace), status(state.status), verification_condition(state.verification_condition), is_over_approx(state.is_over_approx) {};

            // if the state is in the process of summarizing a loop
            virtual bool is_summarizing() const { return false; }

            virtual void append_path_condition(const Expression& _path_condition);

            /**
             * @brief check if the given e is concrete and equal to concrete_value in the current state
             */
            bool is_concrete(const Expression& e, const Expression& concrete_value);

            // step the pc
            void step_pc(AInstruction* next_pc = nullptr);

            // The context for Z3
            z3::context& z3ctx;

            // The next instruction to be executed
            AInstruction* pc;

            AInstruction* prev_pc;

            bool is_over_approx = false;

            // memory model for symbolic execution
            Memory memory;

            Expression evaluate(llvm::Value* v, bool is_signed = true);

            trace_ty trace;

            // The status of the state
            Status status = RUNNING;

            // verification condition
            Expression verification_condition = z3ctx.bool_val(true);

            // Function summarization result shared by all states
            static SymbolTable<FunctionSummary>* func_summaries;

            // Loop summary result shared by all states
            static SymbolTable<LoopSummary>* loop_summaries;

            Expression get_path_condition() const { return path_condition; }

            /**
             * @brief get a model for current path condition
             */
            z3::model get_model();

            std::optional<z3::model> model;
    };

    class LoopState: public State {
        public:
            LoopState(z3::context& z3ctx, AInstruction* pc, AInstruction* prev_pc, const Memory& memory, const Expression& path_condition, const Expression& path_condition_in_loop, const trace_ty& trace, Status status = RUNNING);
            LoopState(const State& state): State(state), path_condition_in_loop(state.z3ctx.bool_val(true)) {};
            LoopState(const LoopState& state): State(state), path_condition_in_loop(state.path_condition_in_loop) {};

            // if the state is in the process of summarizing a loop
            bool is_summarizing() const override { return true; }

            // append a new path condition to the current state
            // besides, it also appends the path condition in loop body
            // if this state is not exiting the loop
            void append_path_condition(const Expression& _path_condition) override;

            // path condition in loop body, loop guard is discarded
            // should only be used when summarize a loop
            Expression path_condition_in_loop;

            /**
             * @brief store modified values by the loop
             */
            // std::set<llvm::Value*> modified_values;
    };

    class RecState: public State {
        public:
            RecState(z3::context& z3ctx, AInstruction* pc, AInstruction* prev_pc, const Memory& memory, const Expression& path_condition, const Expression& path_condition_in_loop, const trace_ty& trace, Status status = RUNNING);
            RecState(const State& state): State(state) {};
            RecState(const RecState& state): State(state) {};

            // if the state is in the process of summarizing a loop
            bool is_summarizing() const override { return true; }
    };
}

#endif