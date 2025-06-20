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
                StackFrame(const Expression& base, llvm::Function* func): base(base), func(func) {}
                StackFrame(const StackFrame& other): prev_pc(other.prev_pc), base(other.base), locals(other.locals), temp_objects(other.temp_objects), func(other.func) {}
                StackFrame& operator=(const StackFrame&) = default;

                MemoryObjectPtr get_object(llvm::Value* v) const;

                AInstruction* prev_pc = nullptr;

                Expression base;

                std::vector<MemoryObject> locals; // local variables in the stack frame

                // temporary objects created in the stack frame, used for storing intermediate results
                MemoryObjectPtr put_temp(llvm::Value* llvm_value, const Expression& value);
                MemoryObjectPtr put_temp(llvm::Value* llvm_value, const MemoryAddress_ty& ptr_value);
                std::map<llvm::Value*, MemoryObject> temp_objects;
                llvm::Function* func;
            };

        public:
            MStack() = default;
            MStack(const MStack&) = default;
            MStack& operator=(const MStack&) = default;

            // push a new frame to the stack
            StackFrame& push_frame(const StackFrame& frame);

            // push a new empty frame to the stack
            StackFrame& push_frame(llvm::Function* func=nullptr);

            // pop the top frame from the stack
            StackFrame& pop_frame();

            // get the top frame of the stack
            StackFrame& top_frame() { 
                assert(!frames.empty() && "Stack is empty");
                return frames.top(); 
            }

            size_t size() const {
                return frames.size();
            }

            MemoryObjectPtr allocate(llvm::Value* value, z3::expr_vector dims);

            Expression load(const MemoryAddress_ty& addr);

            void store(const MemoryAddress_ty& addr, const Expression& value);

            MemoryObjectPtr put_temp(llvm::Value* llvm_value, const Expression& value);
            MemoryObjectPtr put_temp(llvm::Value* llvm_value, const MemoryAddress_ty& ptr_value);

            // Get the object created by the given llvm::Value
            MemoryObjectPtr get_object(llvm::Value* v) const;

            // given a memory address, get the memory object pointed by the base
            MemoryObjectPtr get_object(const MemoryAddress_ty& addr) const;

            std::vector<MemoryObjectPtr> get_arrays() const;

        private:
            std::stack<StackFrame> frames;

            std::vector<MemoryObject> objects;
    };
}
#endif
