#include "MemoryObject.h"
#include "VerificationSession.h"

using namespace ari_exe;

MemoryObject::MemoryObject(
    llvm::Value* llvm_value, const MemoryAddress_ty& obj_addr,
    const Expression& value, std::optional<MemoryAddress_ty> ptr_value,
    const z3::expr_vector& indices, const std::vector<Expression>& sizes,
    const std::string& name, bool is_signed)
    : llvm_value(llvm_value), addr(obj_addr), value(value),
      ptr_value(std::move(ptr_value)), indices(indices), sizes(sizes),
      name(name + std::to_string(
                      VerificationSession::current().memory_object_name_id(
                          name))),
      _is_signed(is_signed), constraints(indices.ctx()) {}

Expression
MemoryObject::read(const std::vector<Expression>& index) const {
    if (index.size() == 0 && get_sizes().size() > 0) {
        // If no index is provided, default to zero for each dimension
        auto& z3ctx = AnalysisManager::get_ctx();
        std::vector<Expression> zeros;
        for (int i = 0; i < get_sizes().size(); i++) {
            zeros.push_back(Expression(z3ctx.int_val(0))); // default index is 0 for each dimension
        }
        return read(zeros);
    }
    assert(index.size() == get_sizes().size() && "Index size does not match array dimensions");
    return value.subs(indices, index);
}

Expression
MemoryObject::read() const {
    std::vector<Expression> zeros;
    auto& z3ctx = AnalysisManager::get_ctx();
    z3::expr_vector conditions(z3ctx);
    z3::expr_vector zeros_expr(z3ctx);
    conditions.push_back(z3ctx.bool_val(true));
    zeros_expr.push_back(z3ctx.int_val(0));
    for (int i = 0; i < get_sizes().size(); ++i) {
        zeros.push_back(Expression(conditions, zeros_expr));
    }
    return read(zeros);
}

void
MemoryObject::write(const Expression& v) {
    value = v;
}

void
MemoryObject::write(const std::vector<Expression>& _index, const Expression& v) {
    auto& z3ctx = AnalysisManager::get_ctx();
    std::vector<Expression> index = _index;
    if (index.size() == 0 && get_sizes().size() > 0) {
        for (int i = 0; i < get_sizes().size(); i++) {
            index.push_back(Expression(z3ctx.int_val(0))); // default index is 0 for each dimension
        }
    }
    assert(index.size() == get_sizes().size() && "Index size does not match array dimensions");

    z3::expr new_condition = z3ctx.bool_val(true);
    z3::expr_vector z3_index(z3ctx);
    for (int i = 0; i < index.size(); i++) {
        new_condition = new_condition && indices[i] == index[i].as_expr();
    }
    value.push_front(new_condition, v);
}

z3::expr
MemoryObject::get_signature() const {
    // For array, the signature is a function of form f(n1, n2, ..., nd);
    auto& z3ctx = AnalysisManager::get_ctx();
    z3::sort_vector sorts(z3ctx);
    for (const auto i: indices) {
        sorts.push_back(i.get_sort());
    }
    auto f = z3ctx.function(name.c_str(), sorts, z3ctx.int_sort());
    return f(indices);
}

MemoryAddress_ty
MemoryObject::get_ptr_value() const {
    if (ptr_value.has_value()) {
        return ptr_value.value();
    } else {
        throw std::runtime_error("MemoryObject does not have a pointer value");
    }
}

std::string
MemoryObject::to_string() const {
    std::string result;
    if (!is_pointer()) {
        result = "MemoryObject: " + get_llvm_value()->getName().str() + "\n";
        result += "Address: " + std::to_string(addr.loc) + "\n";
        // result += "Pointer Value: " + (ptr_value.has_value() ? ptr_value.value().to_string() : "None") + "\n";
        // result += "Indices: ";
        // for (const auto& index : indices) {
        //     result += index.to_string() + " ";
        // }
        result += "\nSizes: ";
        for (const auto& size : sizes)
            result += size.as_expr().to_string() + " ";
        result += "\n";
    } else {
        result = "MemoryObject (Pointer -> " + get_llvm_value()->getName().str() + ")\n";
        // result += "Address: " + std::to_string(addr.loc) + "\n";
        if (ptr_value.has_value()) {
            result += "Pointer Value: " + ptr_value.value().base.as_expr().to_string() + "\n";
        } else {
            result += "Pointer Value: None\n";
        }
    }
    return result;
}
