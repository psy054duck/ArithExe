#ifndef MEMORYOBJECT_H
#define MEMORYOBJECT_H

#include <list>
#include <optional>

#include "z3++.h"
#include "llvm/IR/Instructions.h"
#include "AnalysisManager.h"
#include "logics.h"
#include "Expr.h"

namespace ari_exe {
    /**
     * @brief Each llvm object is associated with a MemoryObject.
     *        Meta information about the object is stored in the MemoryObject.
     */
    class MemoryObject;

    using MemoryObjectPtr = MemoryObject*;

    enum Location {
        STACK,
        HEAP,
        GLOBAL
    };
    struct MemoryAddress_ty {
        Location loc;
        Expression base;
        std::vector<Expression> offset;
    };


    // Wrapper for all memory objects. Here, by memory object we mean all temperary
    // values (registers in llvm ir), global values, and objects allocated explicitly using alloca, malloc.
    // It is designed for supporting pointer dereferencing.
    class MemoryObject {
        public:

            MemoryObject(llvm::Value* llvm_value,
                         const MemoryAddress_ty& obj_addr,
                         const Expression& value,
                         std::optional<MemoryAddress_ty> ptr_value,
                         const z3::expr_vector& indices,
                         const std::vector<Expression>& sizes,
                         const std::string& name,
                         bool _is_signed = true): llvm_value(llvm_value), addr(obj_addr), value(value), ptr_value(ptr_value), indices(indices), sizes(sizes), name(name + std::to_string(name_counter[name])), _is_signed(_is_signed), constraints(indices.ctx()) {}

            // read the first value of the memory object
            Expression read() const;

            // read the value at the given index
            Expression read(const std::vector<Expression>& index) const;

            // write the value in place
            void write(const Expression& v);

            // write the value at the given index in place
            void write(const std::vector<Expression>& index, const Expression& v);

            // get sizes of the memory object
            std::vector<Expression> get_sizes() const { return sizes; }

            void set_sizes(const std::vector<Expression>& new_sizes) {
                sizes = new_sizes;
            }

            // is the memory object a scalar value?
            bool is_scalar() const { return !is_pointer() && indices.size() == 0; };

            // is the memory object an array?
            bool is_array() const { return !is_pointer() && !is_scalar(); };

            bool is_pointer() const { return ptr_value.has_value(); };

            Expression get_value() const { return value; }

            MemoryAddress_ty get_ptr_value() const;

            // get the z3 function signature of the memory object
            // for scalar, it is a constant function
            // for array, it is a function of form f(n1, n2, ..., nd);
            z3::expr get_signature() const;

            llvm::Value* get_llvm_value() const { return llvm_value; }

            MemoryAddress_ty get_addr() const { return addr; }

            z3::expr_vector get_indices() const { return indices; }

            std::string to_string() const;

            bool is_signed() const { return _is_signed; }

            void set_signed(bool is_signed) { _is_signed = is_signed; }

            z3::expr get_constraints() const { return constraints; }
            void set_constraints(const z3::expr& constr) { constraints = constr; }

        private:
            // the instruction that creates this memory object
            llvm::Value* llvm_value;

            // the address of the memory object in the memory
            MemoryAddress_ty addr;

            // the value of the object, which is either a scalar or an array
            Expression value;

            // the value of the pointer if this memory object stores pointer
            std::optional<MemoryAddress_ty> ptr_value;

            // for array A(i1, i2, ..., id), these indices variables i1, ..., id are stored here
            // for scalar, it is empty
            z3::expr_vector indices;

            // the size of each dimension of the array
            std::vector<Expression> sizes;

            bool _is_signed = true;
            // the name of this object, used for generate z3 function.
            std::string name;

            static std::map<std::string, int> name_counter;
            
            z3::expr constraints;
    };
}

#endif