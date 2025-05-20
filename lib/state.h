#ifndef STATE_H
#define STATE_H

#include <map>
#include <vector>

#include "llvm/IR/Instruction.h"
#include "llvm/IR/Constants.h"

#include "z3++.h"

#include "AInstruction.h"
#include "SymbolTable.h"
#include "AStack.h"
#include "FunctionSummary.h"
#include "LoopSummary.h"

namespace ari_exe {
    class AInstruction;
    class AStack;
    class State;

    using trace_ty = std::vector<llvm::BasicBlock*>;
    using state_list = std::vector<std::shared_ptr<State>>;

    class State {
        private:
            // The context for Z3
            z3::context& z3ctx;

        public:
            enum Status {
                RUNNING,    // normal status
                TERMINATED, // reach the end of the program
                VERIFYING,  // a verification condition is being generated
                TESTING,    // a new branch, should check the feasibility
            };

        public:
            State(z3::context& z3ctx, AInstruction* pc, AInstruction* prev_pc, const SymbolTable<z3::expr>& globals, const AStack& stack, z3::expr path_condition, z3::expr path_condition_in_loop, const trace_ty& trace, Status status = RUNNING, bool summarizing = false): z3ctx(z3ctx), pc(pc), prev_pc(prev_pc), globals(globals), stack(stack), path_condition(path_condition), path_condition_in_loop(path_condition_in_loop), trace(trace), status(status), summarizing(summarizing) {};
            State(const State& state): z3ctx(state.z3ctx), pc(state.pc), prev_pc(state.prev_pc), globals(state.globals), stack(state.stack), path_condition(state.path_condition), path_condition_in_loop(state.path_condition_in_loop), trace(state.trace), status(state.status), verification_condition(state.verification_condition), summarizing(state.summarizing) {};

            // step the pc
            void step_pc(AInstruction* next_pc = nullptr);

            // insert or assign a value to the stack or global
            // if value is not found on both sites, it will be inserted to the stack
            void insert_or_assign(llvm::Value* v, z3::expr value);

            // push a new variables to the stack
            void push_value(llvm::Value* v, z3::expr value);

            // allocate a new stack frame
            AStack::StackFrame& push_frame();

            AStack::StackFrame pop_frame();

            // The next instruction to be executed
            AInstruction* pc;

            AInstruction* prev_pc;

            // values of each variable
            SymbolTable<z3::expr> globals;

            // stack of function calls
            AStack stack;

            // path condition collected so far
            z3::expr path_condition;

            // path condition in loop body, loop guard is discarded
            // should only be used when summarize a loop
            z3::expr path_condition_in_loop;

            z3::expr evaluate(llvm::Value* v);

            trace_ty trace;

            // The status of the state
            Status status = RUNNING;

            // verification condition
            z3::expr verification_condition = z3ctx.bool_val(true);

            // Function summarization result shared by all states
            static SymbolTable<FunctionSummary>* func_summaries;

            // Loop summary result shared by all states
            static SymbolTable<LoopSummary>* loop_summaries;

            // flag to indicate if the state is being summarized
            bool summarizing = false;

            void print_top_stack() {
                stack.top_frame().table.print();
            }
    };
}

#endif