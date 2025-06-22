#ifndef MEMORY_H
#define MEMORY_H

#include <map>
#include <list>

#include "MemoryObject.h"
#include "AnalysisManager.h"
#include "MStack.h"

#include "z3++.h"
#include "llvm/IR/Instructions.h"

namespace ari_exe {
        class Memory {
        public:
            // this is used to a pointer to some object as a result of GEP
            // using ObjectPtr = std::pair<llvm::Value*, z3::expr_vector>;

            Memory();

            Memory(const Memory& other);

            // Destructor to clean up memory objects
            ~Memory() = default;

            /**
             * @brief Add new object to globals
             */
            MemoryObjectPtr add_global(llvm::GlobalVariable& value);

            /**
             * @brief Allocate a new memory object for the array in stack.
             */
            MemoryObjectPtr allocate(llvm::Value* value, z3::expr_vector dims);

            /**
             * @brief allocate a new memory object in heap
             */
            MemoryObjectPtr heap_alloca(llvm::Value* value, z3::expr_vector dims);

            /**
             * @brief create temporary memory object in the stack frame.
             */
            MemoryObjectPtr put_temp(llvm::Value* value, const Expression& expr);
            MemoryObjectPtr put_temp(llvm::Value* value, const MemoryAddress_ty& addr);

            /**
             * @brief get the object based on the address.
             */
            Expression load(const MemoryAddress_ty& addr);

            /**
             * @brief get the object pointed by the given LLVM value, which is assumed to be a pointer
             */
            MemoryObjectPtr get_object_pointed_by(llvm::Value* value) const;

            /**
             * @brief store the value to the address.
             */
            void store(const MemoryAddress_ty& addr, const Expression& value);

            /**
             * @brief get the top frame of the stack.
             */
            MStack::StackFrame& top_frame() {
                return m_stack.top_frame();
            }

            /**
             * @brief push a new frame to the stack.
             */
            MStack::StackFrame& push_frame() {
                return m_stack.push_frame();
            }

            /**
             * @brief push the given frame to the stack 
             */
            MStack::StackFrame& push_frame(const MStack::StackFrame& frame) {
                return m_stack.push_frame(frame);
            }

            MStack::StackFrame& push_frame(llvm::Function* func) {
                return m_stack.push_frame(func);
            }

            /**
             * @brief pop the top frame from the stack.
             */
            MStack::StackFrame& pop_frame() {
                return m_stack.pop_frame();
            }

            /**
             * @brief get the memory object created by the given LLVM value.
             * @return the memory object if it exists, otherwise return nullptr.
             */
            MemoryObjectPtr get_object(llvm::Value* value) const;

            /**
             * @brief Given a memory address, get the memory object pointed by the base
             */
            MemoryObjectPtr get_object(const MemoryAddress_ty& addr) const;

            /**
             * @brief Get the stack size, which is the number of frames in the stack.
             */
            size_t stack_size() const {
                return m_stack.size();
            }

            /**
             * @brief get all arrays in the memory.
             */
            std::vector<MemoryObjectPtr> get_arrays() const ;

            // currently, only output the scalar values in the memory
            std::string to_string() const;

            // Get all memory objects accessible in current state.
            // They globals, heap variables, and local variables in the top frame.
            std::vector<MemoryObjectPtr> get_accessible_objects() const;

        private:
            // /**
            //  * @brief Parse a GetElementPtrInst to get the memory object it points to.
            //  */
            // MemoryAddress_ty parse_gep(llvm::GetElementPtrInst* gep);

            MStack m_stack;

            // all global variables and heap variables
            std::vector<MemoryObject> m_objects;

            std::vector<MemoryObject> m_variables;

            // record the next id for each value
            std::map<llvm::Value*, int> next_id;
    };
} // namespace ari_exe

#endif