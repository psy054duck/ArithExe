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
    // class MemoryObject {
    //     public:
    //         // Constructor to initialize the memory object with a value
    //         MemoryObject(llvm::Value* llvm_value) : llvm_value(llvm_value), dims(_get_dim(llvm_value)) {}

    //         // Constructor to initialize an array with a value and size
    //         // currently, assume it is a 1-d array.
    //         MemoryObject(llvm::Value* llvm_value, z3::expr size);

    //         // MemoryObject(llvm::Value* llvm_value, z3::expr scalar_value): llvm_value(llvm_value), scalar(scalar_value), dims(_get_dim(llvm_value)) {}

    //         MemoryObject(const MemoryObject& other) = default;

    //         // Destructor to clean up the memory object
    //         ~MemoryObject() = default;

    //         // Get the managed value
    //         llvm::Value* get_llvm_value() const { return llvm_value; }

    //         // Get the number of elements of the dth dimension in the memory object
    //         z3::expr get_num_elements(int d) const { return get_dims()[d]; }

    //         z3::expr_vector get_dims() const { return dims; }

    //         std::optional<z3::expr> read() const;

    //         std::optional<z3::expr> read(z3::expr_vector index) const;

    //         /**
    //          * @brief Get the array contents, which is a implicitly global quantified formula
    //          * @return A pair of z3::expr and z3::expr_vector.
    //          *         The first element is the array contents, and the second element is the index vector, which are implicitly global quantified.
    //          */
    //         std::pair<z3::expr, z3::expr_vector> get_array_value() const;

    //         void write(z3::expr value);

    //         void write(z3::expr_vector index, z3::expr value);

    //     private:
    //         static z3::expr_vector _get_dim(llvm::Value* value);
    //         static z3::func_decl _get_func_decl(llvm::Value* value);


    //         llvm::Value* llvm_value; // Pointer to the value being managed

    //         // when the object is an array, it is associated with a function declaration
    //         std::optional<z3::func_decl> func;

    //         // when the object is a scalar, it is associated with a z3 expression
    //         std::optional<z3::expr> scalar;

    //         /**
    //          * @brief The writing traces on the memory object, which is an ordered list
    //          *        of pair p, where p.first is the index of the memory object,
    //          *        and p.second is the value written to the memory object at that index.
    //          *        This is ordered reversely by the time of writing.
    //          */
    //         std::list<std::pair<z3::expr_vector, z3::expr>> write_traces;
    //         
    //         // dimensions of the memory object
    //         z3::expr_vector dims;
    // };
    using MemoryObjectPtr = std::shared_ptr<class MemoryObject>;
    using MemoryObjectScalarPtr = std::shared_ptr<class MemoryObjectScalar>;
    using MemoryObjectArrayPtr = std::shared_ptr<class MemoryObjectArray>;


    class MemoryObject {
        public:
            MemoryObject(llvm::Value* llvm_value = nullptr): llvm_value(llvm_value) {}

            // read the value, which is a scalar value or the first element of an array
            virtual z3::expr read() = 0;

            // read the value at the given index
            virtual z3::expr read(z3::expr_vector index) = 0;

            // write the value, which is a scalar value or the first element of an array
            virtual MemoryObjectPtr write(z3::expr value) = 0;
            // write the value at the given index
            virtual MemoryObjectPtr write(z3::expr_vector index, z3::expr value) = 0;

            // get dimensions of the memory object
            virtual z3::expr_vector get_dims() const = 0;

            // is the memory object a scalar value?
            virtual bool is_scalar() const = 0;

            // is the memory object an array?
            virtual bool is_array() const = 0;

            // get the z3 function signature of the memory object
            // for scalar, it is a constant function
            // for array, it is a function of form f(n1, n2, ..., nd);
            virtual z3::expr get_signature() const = 0;

            llvm::Value* get_llvm_value() const { return llvm_value; }

        private:
            // Pointer to the LLVM value being managed
            llvm::Value* llvm_value;
    };

    class MemoryObjectScalar : public MemoryObject {
        public:
            MemoryObjectScalar() = delete;

            MemoryObjectScalar(llvm::Value* llvm_value, z3::expr scalar_value)
                : MemoryObject(llvm_value), scalar(scalar_value) {}

            z3::expr read() override { return scalar; }

            z3::expr read(z3::expr_vector index) override { return scalar; }

            MemoryObjectPtr write(z3::expr value) override;

            MemoryObjectPtr write(z3::expr_vector index, z3::expr value) override;

            z3::expr_vector get_dims() const override;

            bool is_scalar() const override { return true; }

            bool is_array() const override { return false; }

            z3::expr get_signature() const override {
                // For scalar, the signature is a constant function
                return scalar;
            }

        private:
            z3::expr scalar;
    };

    class MemoryObjectArray : public MemoryObject {
        public:
            MemoryObjectArray() = delete;

            MemoryObjectArray(llvm::Value* llvm_value, z3::expr_vector dims);

            z3::expr read() override;

            z3::expr read(z3::expr_vector index) override;

            MemoryObjectPtr write(z3::expr value) override;

            MemoryObjectPtr write(z3::expr_vector index, z3::expr value) override;

            z3::expr_vector get_dims() const override;

            bool is_scalar() const override { return false; }

            bool is_array() const override { return true; }

            z3::expr get_signature() const override;

        private:
            z3::expr_vector dims;
            z3::expr_vector indices;
            // array values are stored as a conditional expression
            z3::expr_vector conditions;
            z3::expr_vector expressions;
    };
}

#endif