#include "MemoryObject.h"

using namespace ari_exe;

std::optional<z3::expr>
MemoryObject::read() const {
    return scalar;
}

std::optional<z3::expr>
MemoryObject::read(z3::expr_vector index) const {
    // TODO: should add bounds checking here
    
    // read to an array object should reflect the writing traces
    auto& z3ctx = index.ctx();
    if (!func.has_value()) return std::nullopt;

    z3::expr res = func.value()(index);
    for (auto wr = write_traces.begin(); wr != write_traces.end(); ++wr) {
        z3::expr cond = z3ctx.bool_val(true);
        for (int i = 0; i < index.size(); ++i) {
            cond = cond && (wr->first[i] == index[i]);
        }
        res = z3::ite(cond, wr->second, res);
    }
    return res;
}

void
MemoryObject::write(z3::expr val) {
    scalar = val;
}

void
MemoryObject::write(z3::expr_vector index, z3::expr val) {
    write_traces.push_front(std::make_pair(index, val));
}

std::pair<z3::expr, z3::expr_vector>
MemoryObject::get_array_value() const {
    assert(func.has_value() && "MemoryObject is not an array, cannot get array value");
    auto& z3ctx = AnalysisManager::get_instance()->get_z3ctx();
    z3::expr_vector indices(z3ctx);
    std::string name_prefix = func.value().name().str() + "_index_";
    for (int i = 0; i < get_dims().size(); ++i) {
        auto name = name_prefix + std::to_string(i);
        indices.push_back(z3ctx.int_const(name.c_str()));
    }
    return {read(indices).value(), indices};
}

z3::expr_vector
MemoryObject::_get_dim(llvm::Value* value) {
    z3::expr_vector dims(AnalysisManager::get_instance()->get_z3ctx());
    assert(false && "exit for dev");
    if (auto array_type = llvm::dyn_cast<llvm::ArrayType>(value->getType())) {
        auto element_type = array_type->getElementType();
        auto num_elements = array_type->getNumElements();
        dims.push_back(AnalysisManager::get_instance()->get_z3ctx().int_val(num_elements));
        while (llvm::isa<llvm::ArrayType>(element_type)) {
            auto sub_array_type = llvm::cast<llvm::ArrayType>(element_type);
            dims.push_back(AnalysisManager::get_instance()->get_z3ctx().int_val(sub_array_type->getNumElements()));
            element_type = sub_array_type->getElementType();
        }
    } else {
        // For non-array types, we assume a single dimension of size 1
        dims.push_back(AnalysisManager::get_instance()->get_z3ctx().int_val(1));
    }
    return dims;
}

z3::func_decl
MemoryObject::_get_func_decl(llvm::Value* value) {
    auto& z3ctx = AnalysisManager::get_instance()->get_z3ctx();
    auto name = value->getName().str();
    z3::sort_vector sorts(z3ctx);
    for (int i = 0; i < _get_dim(value).size(); ++i) sorts.push_back(z3ctx.int_sort());
    z3::func_decl func_decl = z3ctx.function(name.c_str(), sorts, z3ctx.int_sort());
    return func_decl;
}

static z3::expr_vector
dummy_dim_gen() {
    auto& z3ctx = AnalysisManager::get_instance()->get_z3ctx();
    z3::expr_vector dims(z3ctx);
    dims.push_back(z3ctx.int_val(1)); // Default to a single dimension of size 1
    return dims;
}

MemoryObject::MemoryObject(llvm::Value* llvm_value, z3::expr size)
    : llvm_value(llvm_value), scalar(size), dims(dummy_dim_gen()) {
    if (llvm_value->getType()->isArrayTy()) {
        func = _get_func_decl(llvm_value);
    } else {
        func = std::nullopt;
    }
}
