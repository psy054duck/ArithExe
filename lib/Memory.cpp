#include "Memory.h"

using namespace ari_exe;


// void
// Memory::allocate(llvm::Value* value, z3::expr scalar_value) {
//     auto mem_obj = MemoryObject(value, scalar_value);
//     m_heap.insert_or_assign(value, mem_obj);
// }

MemoryObjectPtr
Memory::allocate(llvm::Value* value, z3::expr_vector dims) {
    return m_stack.allocate(value, dims);
}

MemoryObjectPtr
Memory::add_global(llvm::GlobalVariable& gv) {
    auto manager = AnalysisManager::get_instance();
    auto& z3ctx = manager->get_z3ctx();

    llvm::Type* value_type = gv.getValueType();
    auto initial_value = gv.getInitializer();
    z3::expr_vector dims(z3ctx);
    z3::expr value(z3ctx);
    if (value_type->isArrayTy()) {
        // implement for 1-d array first
        // TODO: implement for multi-d array
        assert(value_type->getArrayElementType()->isIntegerTy());
        if (auto zero = dyn_cast_or_null<llvm::ConstantAggregateZero>(initial_value)) {
            value = z3::const_array(z3ctx.int_sort(), z3ctx.int_val(0));
        } else if (auto array = dyn_cast_or_null<llvm::ConstantDataArray>(initial_value)) {
            if (array->isCString()) {
                value = z3ctx.string_val(array->getAsCString().str());
            } else {
                // only initialize the first initialized elemenets,
                // not sure if need to zero fill the rest
                auto arr_sort = z3ctx.array_sort(z3ctx.int_sort(), z3ctx.int_sort());
                value = z3ctx.constant(gv.getName().str().c_str(), arr_sort);
                for (int i = 0; i < array->getNumElements(); i++) {
                    // TODO: support other types
                    auto element = array->getElementAsInteger(i);
                    value = z3::store(value, z3ctx.int_val(i), z3ctx.int_val(element));
                }
            }
        } else {
            llvm::errs() << "Unsupported global variable initializer type\n";
        }
        dims.push_back(z3ctx.int_val(value_type->getArrayNumElements()));
    } else if (value_type->isIntegerTy()) {
        if (auto constant = dyn_cast_or_null<llvm::ConstantInt>(initial_value)) {
            value = z3ctx.int_val(constant->getSExtValue());
        } else {
            llvm::errs() << "Unsupported global variable initializer type\n";
        }
    }
    auto obj_ptr = heap_alloca(&gv, dims);
    obj_ptr->write(Expression(value));
    return obj_ptr;
}

MemoryObjectPtr
Memory::get_object(llvm::Value* value) const {
    // if value is constant, return the constant value
    auto manager = AnalysisManager::get_instance();
    auto& z3ctx = manager->get_z3ctx();

    auto stack_query = m_stack.get_object(value);
    if (stack_query) {
        return stack_query;
    }

    auto it = std::find_if(m_objects.begin(), m_objects.end(),
                           [value](const MemoryObject& obj) { return obj.get_llvm_value() == value; });
    if (it != m_objects.end()) {
        return const_cast<MemoryObjectPtr>(&(*it));
    }
    return nullptr;
}

void
Memory::store(const MemoryAddress_ty& target, const Expression& val) {
    auto m_obj_opt = get_object(target);
    assert(m_obj_opt != nullptr && "Memory object should be found for the given address");

    // if the memory object is a scalar, write the value directly
    if (m_obj_opt->is_scalar()) {
        m_obj_opt->write(val);
    } else if (m_obj_opt->is_array()) {
        // if it is an array, write the value at the given index
        m_obj_opt->write(target.offset, val);
    } else {
        llvm::errs() << "Unsupported memory object type\n";
    }
}

MemoryObjectPtr
Memory::get_object(const MemoryAddress_ty& addr) const {
    if (addr.loc == STACK) {
        return m_stack.get_object(addr);
    } else {
        auto it = std::find_if(m_objects.begin(), m_objects.end(),
                               [&addr](const MemoryObject& obj) { return obj.get_addr().base.as_expr().get_numeral_int() == addr.base.as_expr().get_numeral_int(); });
        if (it != m_objects.end()) {
            return const_cast<MemoryObjectPtr>(&(*it));
        }
    }
    return nullptr;
}

std::vector<MemoryObjectPtr>
Memory::get_arrays() const {
    auto arrays = m_stack.get_arrays();
    for (auto& obj : m_objects) {
        if (obj.is_array()) {
            arrays.push_back(const_cast<MemoryObjectPtr>(&obj));
        }
    }
    return arrays;
}

MemoryObjectPtr
Memory::heap_alloca(llvm::Value* value, z3::expr_vector dims) {
    auto& z3ctx = AnalysisManager::get_ctx();
    int id = m_objects.size();
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
    m_objects.emplace_back(value, mem_obj_addr, Expression(), std::nullopt, indices, sizes, value->getName().str());
    return &m_objects.back();
}

MemoryObjectPtr
Memory::put_temp(llvm::Value* value, const Expression& expr) {
    return m_stack.put_temp(value, expr);
}

MemoryObjectPtr
Memory::put_temp(llvm::Value* value, const MemoryAddress_ty& addr) {
    return m_stack.put_temp(value, addr);
}