//----------------------------- Stack.h -----------------------------
// 
// This file contains stack recording the stack information for implementing symbolic function calls.
//
//------------------------------------------------------------------------------------------//


#ifndef ASTACK_H
#define ASTACK_H

#include "SymbolTable.h"

#include <map>
#include <vector>
#include <stack>
#include <optional>

#include "z3++.h"

namespace ari_exe {

    class AInstruction;

    class AStack {
        public:
            struct StackFrame {
                StackFrame() = default;
                StackFrame(const StackFrame&) = default;
                StackFrame& operator=(const StackFrame&) = default;

                // insert or assign a value to the stack frame
                void insert_or_assign(llvm::Value* symbol, z3::expr value);

                // record values for stack variables
                SymbolTable<z3::expr> table;

                // record the called site
                AInstruction* prev_pc = nullptr;
            };

        public:
            AStack() = default;
            AStack(const AStack&) = default;
            AStack& operator=(const AStack&) = default;

            // push a new frame to the stack
            StackFrame& push_frame(const StackFrame& frame);

            // push a new empty frame to the stack
            StackFrame& push_frame();

            // pop the top frame from the stack
            StackFrame pop_frame();

            // get the top frame from the stack
            StackFrame& top_frame();

            // get the value of a variable in the top frame
            std::optional<z3::expr> evaluate(llvm::Value* v);

            // add or update a value in the top frame
            void insert_or_assign_value(llvm::Value* v, z3::expr value);

            // get the number of frames in the stack
            size_t size() const {
                return frames.size();
            }

        private:
            std::stack<StackFrame> frames;
    };

}
#endif
