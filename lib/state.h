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
#include "function_summary_utils.h"

namespace ari_exe {
    class AInstruction;
    class AStack;
    // class Summary;

    using trace_ty = std::vector<llvm::BasicBlock*>;

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
            State(z3::context& z3ctx, AInstruction* pc, AInstruction* prev_pc, const SymbolTable<z3::expr>& globals, const AStack& stack, z3::expr path_condition, const trace_ty& trace, Status status = RUNNING): z3ctx(z3ctx), pc(pc), prev_pc(prev_pc), globals(globals), stack(stack), path_condition(path_condition), trace(trace), status(status) {};
            State(const State& state): z3ctx(state.z3ctx), pc(state.pc), prev_pc(state.prev_pc), globals(state.globals), stack(state.stack), path_condition(state.path_condition), trace(state.trace), status(state.status), verification_condition(state.verification_condition) {};

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

            z3::expr evaluate(llvm::Value* v);

            trace_ty trace;

            // The status of the state
            Status status = RUNNING;

            // verification condition
            z3::expr verification_condition = z3ctx.bool_val(true);

            // summarization result shared by all states
            static SymbolTable<Summary>* summaries;
    };
}

#endif