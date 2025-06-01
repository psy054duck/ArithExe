//----------------------------- Stack.h -----------------------------
// 
// This file contains stack recording the stack information for implementing symbolic function calls.
//
//------------------------------------------------------------------------------------------//


#ifndef ASTACK_H
#define ASTACK_H

#include "MemoryObject.h"

#include <map>
#include <vector>
#include <stack>
#include <optional>

#include "z3++.h"

namespace ari_exe {

    class AInstruction;

    class MStack {
        public:
            struct StackFrame {
                StackFrame() = default;
                StackFrame(const StackFrame&) = default;
                StackFrame& operator=(const StackFrame&) = default;

                // insert or assign a value to the stack frame
                void write(llvm::Value* symbol, z3::expr value);

                void write(llvm::Value* symbol, z3::expr_vector index, z3::expr value);

                std::optional<z3::expr> read(llvm::Value* v) const;

                std::optional<MemoryObjectPtr> get_memory_object(llvm::Value* v) const;

                // get all arrays in the stack frame
                std::vector<MemoryObjectArrayPtr> get_arrays() const;

                // record values for stack variables
                // SymbolTable<z3::expr> table;
                // Memory memory;
                std::map<llvm::Value*, MemoryObjectPtr> m_objects; // map of values in the stack frame

                // record the called site
                AInstruction* prev_pc = nullptr;

                std::string to_string() const;
            };

        public:
            MStack() = default;
            MStack(const MStack&) = default;
            MStack& operator=(const MStack&) = default;

            // push a new frame to the stack
            StackFrame& push_frame(const StackFrame& frame);

            // push a new empty frame to the stack
            StackFrame& push_frame();

            // pop the top frame from the stack
            StackFrame pop_frame();

            // get the top frame from the stack
            StackFrame& top_frame() const;
            StackFrame& top_frame();

            // get the value of a variable in the top frame
            std::optional<z3::expr> read(llvm::Value* v) const;

            // get the memory object of a variable in the top frame
            std::optional<MemoryObjectPtr> get_memory_object(llvm::Value* v) const;

            // get all arrays in the top frame
            std::vector<MemoryObjectArrayPtr> get_arrays() const;

            // add or update a value in the top frame
            void write(llvm::Value* v, z3::expr value);
            void write(llvm::Value* v, z3::expr_vector index, z3::expr value);

            // get the number of frames in the stack
            size_t size() const {
                return frames.size();
            }

            std::string top_frame_to_string() const {
                return frames.top().to_string();
            }

        private:
            std::stack<StackFrame> frames;
    };
}
#endif
