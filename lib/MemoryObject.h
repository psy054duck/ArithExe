#ifndef MEMORYOBJECT_H
#define MEMORYOBJECT_H

#include <list>
#include <optional>

#include "z3++.h"
#include "llvm/IR/Instructions.h"
#include "AnalysisManager.h"

namespace ari_exe {
    /**
     * @brief Each llvm object is associated with a MemoryObject.
     *        Meta information about the object is stored in the MemoryObject.
     */
    class MemoryObject {
        public:
            // Constructor to initialize the memory object with a value
            MemoryObject(llvm::Value* llvm_value) : llvm_value(llvm_value), dims(_get_dim(llvm_value)) {}

            MemoryObject(llvm::Value* llvm_value, z3::expr scalar_value): llvm_value(llvm_value), scalar(scalar_value), dims(_get_dim(llvm_value)) {}

            MemoryObject(const MemoryObject& other) = default;

            // Destructor to clean up the memory object
            ~MemoryObject() = default;

            // Get the managed value
            llvm::Value* get_llvm_value() const { return llvm_value; }

            // Get the number of elements of the dth dimension in the memory object
            z3::expr get_num_elements(int d) const { return get_dims()[d]; }

            z3::expr_vector get_dims() const { return dims; }

            std::optional<z3::expr> read() const;

            std::optional<z3::expr> read(z3::expr_vector index) const;

            /**
             * @brief Get the array contents, which is a implicitly global quantified formula
             * @return A pair of z3::expr and z3::expr_vector.
             *         The first element is the array contents, and the second element is the index vector, which are implicitly global quantified.
             */
            std::pair<z3::expr, z3::expr_vector> get_array_value() const;

            void write(z3::expr value);

            void write(z3::expr_vector index, z3::expr value);

        private:
            static z3::expr_vector _get_dim(llvm::Value* value);
            static z3::func_decl _get_func_decl(llvm::Value* value);


            llvm::Value* llvm_value; // Pointer to the value being managed

            size_t num_elements; // the number of elements in this object.

            // when the object is an array, it is associated with a function declaration
            std::optional<z3::func_decl> func;
            // when the object is a scalar, it is associated with a z3 expression
            std::optional<z3::expr> scalar;

            /**
             * @brief The writing traces on the memory object, which is an ordered list
             *        of pair p, where p.first is the index of the memory object,
             *        and p.second is the value written to the memory object at that index.
             *        This is ordered reversely by the time of writing.
             */
            std::list<std::pair<z3::expr_vector, z3::expr>> write_traces;
            
            // dimensions of the memory object
            z3::expr_vector dims;
    };
}

#endif