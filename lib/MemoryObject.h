#ifndef MEMORYOBJECT_H
#define MEMORYOBJECT_H

#include <list>
#include <optional>

#include "z3++.h"
#include "llvm/IR/Instructions.h"
#include "AnalysisManager.h"
#include "logics.h"

namespace ari_exe {
    /**
     * @brief Each llvm object is associated with a MemoryObject.
     *        Meta information about the object is stored in the MemoryObject.
     */
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

            virtual void write_in_place(z3::expr value) = 0;

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

            void write_in_place(z3::expr value) override;

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

            void write_in_place(z3::expr value) override;

            z3::expr_vector get_dims() const override;

            bool is_scalar() const override { return false; }

            bool is_array() const override { return true; }

            z3::expr get_signature() const override;

            z3::expr as_expr() const;

        // private:
            z3::expr_vector dims;
            z3::expr_vector indices;
            // array values are stored as a conditional expression
            z3::expr_vector conditions;
            z3::expr_vector expressions;
    };
}

#endif