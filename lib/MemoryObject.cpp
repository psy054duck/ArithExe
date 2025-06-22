#include "MemoryObject.h"

using namespace ari_exe;

std::map<std::string, int> MemoryObject::name_counter;

Expression
MemoryObject::read(const std::vector<Expression>& index) const {
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
MemoryObject::write(const std::vector<Expression>& index, const Expression& v) {
    assert(index.size() == get_sizes().size() && "Index size does not match array dimensions");

    auto& z3ctx = AnalysisManager::get_ctx();
    z3::expr new_condition = z3ctx.bool_val(true);
    z3::expr_vector z3_index(z3ctx);
    for (int i = 0; i < index.size(); i++) {
        new_condition = new_condition && indices[i] == index[i].as_expr();
    }
    value.push_front(new_condition, v.as_expr());
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