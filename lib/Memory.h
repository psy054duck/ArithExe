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
            Memory() = default;

            Memory(const Memory& other): m_stack(other.m_stack), m_heap(other.m_heap), m_globals(other.m_globals) {}

            // Destructor to clean up memory objects
            ~Memory() = default;

            /**
             * @brief Read the value from stack, heap, or globals based on the LLVM value.
             */
            std::optional<z3::expr> read(llvm::Value* value) const;

            /**
             * @brief Read the value from stack, heap, or globals based on the LLVM value.
             */
            std::optional<z3::expr> read(llvm::Value* value, z3::expr_vector index) const {
                auto v = m_stack.read(value);
                if (v.has_value()) {
                    return v.value();
                }
                auto it_heap = m_heap.find(value);
                if (it_heap != m_heap.end()) {
                    return it_heap->second->read(index);
                }
                auto it_globals = m_globals.find(value);
                if (it_globals != m_globals.end()) {
                    return it_globals->second->read(index);
                }
            }

            /**
             * @brief Write the value to the stack, heap, or globals based on the LLVM value.
             */
            void write(llvm::Value* value, z3::expr val) {
                if (m_stack.read(value).has_value()) {
                    m_stack.write(value, val);
                } else {
                    auto it_heap = m_heap.find(value);
                    if (it_heap != m_heap.end()) {
                        it_heap->second->write(val);
                    } else {
                        auto it_globals = m_globals.find(value);
                        if (it_globals != m_globals.end()) {
                            it_globals->second->write(val);
                        } else {
                            // If the value is not found in stack, heap, or globals,
                            // write it to the stack.
                            m_stack.write(value, val);
                        }
                    }
                }
            }

            /**
             * @brief Write the value to the stack, heap, or globals based on the LLVM value and index.
             */
            void write(llvm::Value* value, z3::expr_vector index, z3::expr val) {
                if (m_stack.read(value).has_value()) {
                    m_stack.write(value, index, val);
                } else {
                    auto it_heap = m_heap.find(value);
                    if (it_heap != m_heap.end()) {
                        it_heap->second->write(index, val);
                    } else {
                        auto it_globals = m_globals.find(value);
                        if (it_globals != m_globals.end()) {
                            it_globals->second->write(index, val);
                        } else {
                            throw std::runtime_error("Memory object not found for writing");
                        }
                    }
                }
            }

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
                return result;
            };

            /**
             * @brief Get the stack size, which is the number of frames in the stack.
             */
            size_t stack_size() const {
                return m_stack.size();
            }

        private:
            // Map to store memory objects by their LLVM values
            // std::map<llvm::Value*, MemoryObject> memory_objects;
            MStack m_stack;
            std::map<llvm::Value*, MemoryObjectPtr> m_heap; // Use custom comparator for LLVM values
            std::map<llvm::Value*, MemoryObjectPtr> m_globals; // Use custom comparator for LLVM values
    };
} // namespace ari_exe

#endif