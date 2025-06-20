#include "MStack.h"

using namespace ari_exe;


// MStack::StackFrame&
// MStack::push_frame(const StackFrame& frame) {
//     frames.push(frame);
//     return frames.top();
// }

MemoryObjectPtr
MStack::StackFrame::get_object(llvm::Value* v) const {
    auto it = temp_objects.find(v);
    if (it != temp_objects.end()) {
        return const_cast<MemoryObjectPtr>(&it->second);
    }
    return nullptr;
}

MStack::StackFrame&
MStack::push_frame(llvm::Function* func) {
    int num_objs = objects.size();
    auto& z3ctx = AnalysisManager::get_ctx();
    auto base = Expression(z3ctx.int_val(num_objs));
    frames.push(StackFrame(base, func));
    return frames.top();
}

MStack::StackFrame&
MStack::pop_frame() {
    assert(!frames.empty());
    auto& frame = frames.top();
    // objects.resize(frame.base.as_expr().get_numeral_int());

    while (objects.size() > frame.base.as_expr().get_numeral_int()) {
        objects.pop_back();
    }
    frames.pop();
    return frame;
}

Expression
MStack::load(const MemoryAddress_ty& addr) {
    auto obj = get_object(addr);
    return obj->read(addr.offset);
}

void
MStack::store(const MemoryAddress_ty& addr, const Expression& value) {
    assert(addr.loc == STACK);
    auto base_z3 = addr.base.as_expr();
    assert(base_z3.is_numeral() && "Only support concrete store for now");
    int base = base_z3.get_numeral_int();
    auto& obj = objects[base];
    obj.write(addr.offset, value);
}

MemoryObjectPtr
MStack::allocate(llvm::Value* value, z3::expr_vector dims) {
    auto& z3ctx = AnalysisManager::get_ctx();
    int id = objects.size();
    MemoryAddress_ty mem_obj_addr{STACK, Expression(z3ctx.int_val(id)), {}};
    auto indices  = z3::expr_vector(z3ctx);
    for (int i = 0; i < dims.size(); ++i) {
        indices.push_back(z3ctx.int_const(("i" + std::to_string(i)).c_str()));
    }
    std::vector<Expression> sizes;
    for (const auto& dim : dims) {
        if (dim.is_numeral()) {
            sizes.emplace_back(dim);
        }
    }
    objects.emplace_back(value, mem_obj_addr, Expression(), std::nullopt, indices, sizes, value->getName().str());
    return &objects.back();
}

MemoryObjectPtr
MStack::get_object(llvm::Value* v) const {

    auto obj = frames.top().get_object(v);
    if (obj) {
        return obj;
    }

    auto it = std::find_if(objects.begin(), objects.end(),
                           [v](const MemoryObject& obj) { return obj.get_llvm_value() == v; });
    if (it != objects.end()) {
        return const_cast<MemoryObjectPtr>(&(*it));
    }
    return nullptr;
}

MemoryObjectPtr
MStack::get_object(const MemoryAddress_ty& addr) const {
    assert(addr.loc == STACK);
    auto base_z3 = addr.base.as_expr();
    assert(base_z3.is_numeral() && "Only support concrete get_object for now");
    int base = base_z3.get_numeral_int();
    return const_cast<MemoryObjectPtr>(&objects[base]);
}

MemoryObjectPtr
MStack::StackFrame::put_temp(llvm::Value* llvm_value, const Expression& value) {
    auto obj = MemoryObject(llvm_value, MemoryAddress_ty{STACK, value, {}}, value, std::nullopt, z3::expr_vector(AnalysisManager::get_ctx()), {}, llvm_value->getName().str());
    temp_objects.insert_or_assign(llvm_value, obj);
    return &temp_objects.at(llvm_value);
}

MemoryObjectPtr
MStack::StackFrame::put_temp(llvm::Value* llvm_value, const MemoryAddress_ty& ptr_value) {
    auto obj = MemoryObject(llvm_value, MemoryAddress_ty{STACK, Expression(), {}}, Expression(), ptr_value, z3::expr_vector(AnalysisManager::get_ctx()), {}, llvm_value->getName().str());
    temp_objects.insert_or_assign(llvm_value, obj);
    return &temp_objects.at(llvm_value);
}

MemoryObjectPtr
MStack::put_temp(llvm::Value* llvm_value, const Expression& value) {
    return frames.top().put_temp(llvm_value, value);
}

MemoryObjectPtr
MStack::put_temp(llvm::Value* llvm_value, const MemoryAddress_ty& ptr_value) {
    return frames.top().put_temp(llvm_value, ptr_value);
}

std::vector<MemoryObjectPtr>
MStack::get_arrays() const {
    std::vector<MemoryObjectPtr> arrays;
    for (const auto& obj : objects) {
        if (obj.is_array()) {
            arrays.push_back(const_cast<MemoryObjectPtr>(&obj));
        }
    }
    return arrays;
}
