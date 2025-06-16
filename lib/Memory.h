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
            using ObjectPtr = std::pair<llvm::Value*, z3::expr_vector>;

            Memory() = default;

            Memory(const Memory& other): m_stack(other.m_stack), m_heap(other.m_heap), m_globals(other.m_globals), m_pointers(other.m_pointers) {}

            // Destructor to clean up memory objects
            ~Memory() = default;

            /**
             * @brief parse a pointer type to get the memory object and offset it points to.
             */
            ObjectPtr parse_pointer(llvm::Value* value) const;

            /**
             * @brief Read the value from stack, heap, or globals based on the LLVM value.
             */
            std::optional<z3::expr> read(llvm::Value* value) const;

            /**
             * @brief Read the value from stack, heap, or globals based on the LLVM value.
             */
            std::optional<z3::expr> read(llvm::Value* value, z3::expr_vector index) const;

            /**
             * @brief Write the value to the stack, heap, or globals based on the LLVM value.
             */
            void write(llvm::Value* value, z3::expr val);

            /**
             * @brief Write the value to the stack, heap, or globals based on the LLVM value and index.
             */
            void write(llvm::Value* value, z3::expr_vector index, z3::expr val);

            /**
             * @brief Write the memory object to the stack, heap, or globals based on the LLVM value.
             */
            void write(llvm::Value* value, MemoryObjectPtr memory_object);

            /**
             * @brief Store gep
             */
            void store_gep(llvm::GetElementPtrInst* gep);

            /**
             * @brief Get gep
             */
            ObjectPtr get_gep(llvm::GetElementPtrInst* gep) const;

            /**
             * @brief Add new object to globals
             */
            void add_global(llvm::GlobalVariable& value);

            /**
             * @brief Allocate a new memory object for the array.
             */
            void allocate(llvm::Value* value, z3::expr_vector dims);

            /**
             * @brief Allocate a new memory object for the given LLVM scalar value.
             */
            void allocate(llvm::Value* value, z3::expr val);

            /**
             * @brief Allocate a new memory object for the given LLVM array.
             */
            void allocate(llvm::Value* value);

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

            /**
             * @brief pop the top frame from the stack.
             */
            MStack::StackFrame pop_frame() {
                return m_stack.pop_frame();
            }

            /**
             * @brief Get the top frame from the stack.
             */
            MStack::StackFrame& top_frame() {
                return m_stack.top_frame();
            }

            std::optional<MemoryObjectPtr> get_memory_object(llvm::Value* value) const {
                auto stack_value = m_stack.get_memory_object(value);
                if (stack_value.has_value()) {
                    return stack_value.value();
                }
                auto it_heap = m_heap.find(value);
                if (it_heap != m_heap.end()) {
                    return it_heap->second;
                }
                auto it_globals = m_globals.find(value);
                if (it_globals != m_globals.end()) {
                    return it_globals->second;
                }
                return std::nullopt; // Memory object not found in stack, heap, or globals
            }

            std::string to_string() const {
                std::string result = "Memory:\n";
                result += "Stack:\n" + m_stack.top_frame_to_string() + "\n";
                result += "Heap:\n";
                for (const auto& pair : m_heap) {
                    result += "  " + pair.first->getName().str() + ": " + pair.second->read().to_string() + "\n";
                }
                result += "Globals:\n";
                for (const auto& pair : m_globals) {
                    result += "  " + pair.first->getName().str() + ": " + pair.second->read().to_string() + "\n";
                }
                result += "GEP Pointers:\n";
                for (const auto& gep_info: m_pointers) {
                    result += "GEP: " + gep_info.first->getName().str() + "\n ";
                    result += "  Points to: " + gep_info.second.first->getName().str() + " + " + gep_info.second.second.to_string() + "\n";
                }
                return result;
            };

            /**
             * @brief Get the stack size, which is the number of frames in the stack.
             */
            size_t stack_size() const {
                return m_stack.size();
            }

            /**
             * @brief get all arrays in the memory.
             */
            std::vector<MemoryObjectArrayPtr> get_arrays() const;

        private:
            /**
             * @brief Parse a GetElementPtrInst to get the memory object it points to.
             */
            ObjectPtr parse_gep(llvm::GetElementPtrInst* gep) const;
            // Map to store memory objects by their LLVM values
            // std::map<llvm::Value*, MemoryObject> memory_objects;
            MStack m_stack;

            std::map<llvm::Value*, MemoryObjectPtr> m_heap; 

            std::map<llvm::Value*, MemoryObjectPtr> m_globals;

            // This map is used to store the results of GEP operations
            std::map<llvm::GetElementPtrInst*, ObjectPtr> m_pointers;
    };
} // namespace ari_exe

#endif