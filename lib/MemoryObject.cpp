#include "MemoryObject.h"

using namespace ari_exe;

// std::optional<z3::expr>
// MemoryObject::read() const {
//     return scalar;
// }
// 
// std::optional<z3::expr>
// MemoryObject::read(z3::expr_vector index) const {
//     // TODO: should add bounds checking here
//     
//     // read to an array object should reflect the writing traces
//     auto& z3ctx = index.ctx();
//     if (!func.has_value()) return std::nullopt;
// 
//     z3::expr res = func.value()(index);
//     for (auto wr = write_traces.begin(); wr != write_traces.end(); ++wr) {
//         z3::expr cond = z3ctx.bool_val(true);
//         for (int i = 0; i < index.size(); ++i) {
//             cond = cond && (wr->first[i] == index[i]);
//         }
//         res = z3::ite(cond, wr->second, res);
//     }
//     return res;
// }
// 
// void
// MemoryObject::write(z3::expr val) {
//     scalar = val;
// }
// 
// void
// MemoryObject::write(z3::expr_vector index, z3::expr val) {
//     write_traces.push_front(std::make_pair(index, val));
// }
// 
// std::pair<z3::expr, z3::expr_vector>
// MemoryObject::get_array_value() const {
//     assert(func.has_value() && "MemoryObject is not an array, cannot get array value");
//     auto& z3ctx = AnalysisManager::get_instance()->get_z3ctx();
//     z3::expr_vector indices(z3ctx);
//     std::string name_prefix = func.value().name().str() + "_index_";
//     for (int i = 0; i < get_dims().size(); ++i) {
//         auto name = name_prefix + std::to_string(i);
//         indices.push_back(z3ctx.int_const(name.c_str()));
//     }
//     return {read(indices).value(), indices};
// }
// 
// z3::expr_vector
// MemoryObject::_get_dim(llvm::Value* value) {
//     z3::expr_vector dims(AnalysisManager::get_instance()->get_z3ctx());
//     assert(false && "exit for dev");
//     if (auto array_type = llvm::dyn_cast<llvm::ArrayType>(value->getType())) {
//         auto element_type = array_type->getElementType();
//         auto num_elements = array_type->getNumElements();
//         dims.push_back(AnalysisManager::get_instance()->get_z3ctx().int_val(num_elements));
//         while (llvm::isa<llvm::ArrayType>(element_type)) {
//             auto sub_array_type = llvm::cast<llvm::ArrayType>(element_type);
//             dims.push_back(AnalysisManager::get_instance()->get_z3ctx().int_val(sub_array_type->getNumElements()));
//             element_type = sub_array_type->getElementType();
//         }
//     } else {
//         // For non-array types, we assume a single dimension of size 1
//         dims.push_back(AnalysisManager::get_instance()->get_z3ctx().int_val(1));
//     }
//     return dims;
// }
// 
// z3::func_decl
// MemoryObject::_get_func_decl(llvm::Value* value) {
//     auto& z3ctx = AnalysisManager::get_instance()->get_z3ctx();
//     auto name = value->getName().str();
//     z3::sort_vector sorts(z3ctx);
//     for (int i = 0; i < _get_dim(value).size(); ++i) sorts.push_back(z3ctx.int_sort());
//     z3::func_decl func_decl = z3ctx.function(name.c_str(), sorts, z3ctx.int_sort());
//     return func_decl;
// }
// 
// static z3::expr_vector
// dummy_dim_gen() {
//     auto& z3ctx = AnalysisManager::get_instance()->get_z3ctx();
//     z3::expr_vector dims(z3ctx);
//     dims.push_back(z3ctx.int_val(1)); // Default to a single dimension of size 1
//     return dims;
// }
// 
// MemoryObject::MemoryObject(llvm::Value* llvm_value, z3::expr size)
//     : llvm_value(llvm_value), scalar(size), dims(dummy_dim_gen()) {
//     if (llvm_value->getType()->isArrayTy()) {
//         func = _get_func_decl(llvm_value);
//     } else {
//         func = std::nullopt;
//     }
// }
// 
MemoryObjectArray::MemoryObjectArray(llvm::Value* llvm_value, z3::expr_vector dims)
    : MemoryObject(llvm_value), dims(dims), indices(dims.ctx()), conditions(dims.ctx()), expressions(dims.ctx()) {
        std::string name_prefix = llvm_value->getName().str() + "_array_func_n";
        for (size_t i = 0; i < dims.size(); ++i) {
            auto name = name_prefix + std::to_string(i);
            indices.push_back(dims.ctx().int_const(name.c_str()));
        }
        auto& z3ctx = dims.ctx();
        conditions.push_back(dims.ctx().bool_val(true));
        z3::sort_vector sorts(z3ctx);
        for (size_t i = 0; i < dims.size(); ++i) {
            sorts.push_back(z3ctx.int_sort());
        }
        z3::func_decl func_decl = z3ctx.function(llvm_value->getName().str().c_str(), sorts, z3ctx.int_sort());
        expressions.push_back(func_decl(indices));
    }

z3::expr_vector
MemoryObjectScalar::get_dims() const {
    auto& z3ctx = scalar.ctx();
    z3::expr_vector dims(z3ctx);
    dims.push_back(z3ctx.int_val(1)); // A scalar has a single dimension of size 1
    return dims;
}

z3::expr
MemoryObjectArray::read(z3::expr_vector index) {
    assert(index.size() == dims.size() && "Index size does not match array dimensions");

    auto ite_expr = expressions.back();
    // Read the value at the given index
    // Apply conditions to get the correct value
    for (int i = static_cast<int>(conditions.size()) - 1; i >= 0; --i) {
        ite_expr = z3::ite(conditions[i], expressions[i], ite_expr);
    }
    auto res = ite_expr.substitute(indices, index);
    return res.simplify();
}

z3::expr
MemoryObjectArray::read() {
    // Read the first element of the array
    z3::expr_vector zeros(dims.ctx());
    for (int i = 0; i < dims.size(); ++i) {
        zeros.push_back(dims.ctx().int_val(0)); // Default to zero index for each dimension
    }
    return read(zeros);
}

MemoryObjectPtr
MemoryObjectScalar::write(z3::expr value) {
    // For scalar, we just update the scalar value
    return std::make_shared<MemoryObjectScalar>(get_llvm_value(), value);
}

MemoryObjectPtr
MemoryObjectScalar::write(z3::expr_vector index, z3::expr value) {
    // Scalar does not support writing with index, just return itself
    return std::make_shared<MemoryObjectScalar>(get_llvm_value(), value);
}

MemoryObjectPtr
MemoryObjectArray::write(z3::expr value) {
    // For array, we write the value to the first element
    z3::expr_vector zeros(dims.ctx());
    for (int i = 0; i < dims.size(); ++i) {
        zeros.push_back(dims.ctx().int_val(0)); // Default to zero index for each dimension
    }
    return write(zeros, value);
}

MemoryObjectPtr
MemoryObjectArray::write(z3::expr_vector index, z3::expr value) {
    assert(index.size() == dims.size() && "Index size does not match array dimensions");

    // Store the value at the given index
    auto new_array = std::make_shared<MemoryObjectArray>(get_llvm_value(), dims);
    new_array->conditions.pop_back();
    new_array->expressions.pop_back();

    z3::expr_vector index_condition(index.ctx());
    for (int i = 0; i < index.size(); ++i) {
        // Create a condition for each index
        index_condition.push_back(indices[i] == index[i]);
    }
    // latter writes will overwrite previous ones, so it is put first
    new_array->conditions.push_back(z3::mk_and(index_condition));
    new_array->expressions.push_back(value);

    // copy existing conditions and expressions
    for (int i = 0; i < conditions.size(); ++i) {
        new_array->conditions.push_back(conditions[i]);
        new_array->expressions.push_back(expressions[i]);
    }


    return new_array;
}

z3::expr_vector
MemoryObjectArray::get_dims() const {
    return dims;
}

z3::expr
MemoryObjectArray::get_signature() const {
    // For array, the signature is a function of form f(n1, n2, ..., nd);
    auto& z3ctx = dims.ctx();
    z3::sort_vector sorts(z3ctx);
    for (const auto& dim : dims) {
        sorts.push_back(dim.get_sort());
    }
    auto f = z3ctx.function(get_llvm_value()->getName().str().c_str(), sorts, z3ctx.int_sort());
    return f(indices);
}