#include "Memory.h"

using namespace ari_exe;


// void
// Memory::allocate(llvm::Value* value, z3::expr scalar_value) {
//     auto mem_obj = MemoryObject(value, scalar_value);
//     m_heap.insert_or_assign(value, mem_obj);
// }

void
Memory::allocate(llvm::Value* value, z3::expr_vector dims) {
    auto mem_obj = std::make_shared<MemoryObjectArray>(value, dims);
    m_heap.insert_or_assign(value, mem_obj);
}

void
Memory::allocate(llvm::Value* value, z3::expr scalar_value) {
    auto mem_obj = std::make_shared<MemoryObjectScalar>(value, scalar_value);
    m_heap.insert_or_assign(value, mem_obj);
}

void
Memory::add_global(llvm::GlobalVariable& gv) {
    auto manager = AnalysisManager::get_instance();
    auto& z3ctx = manager->get_z3ctx();

    llvm::Type* value_type = gv.getValueType();
    auto initial_value = gv.getInitializer();
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
    } else if (value_type->isIntegerTy()) {
        if (auto constant = dyn_cast_or_null<llvm::ConstantInt>(initial_value)) {
            value = z3ctx.int_val(constant->getSExtValue());
            auto mem_obj = std::make_shared<MemoryObjectScalar>(&gv, value);
            m_globals.insert_or_assign(&gv, mem_obj);
        } else {
            llvm::errs() << "Unsupported global variable initializer type\n";
        }
    }
}

std::optional<z3::expr>
Memory::read(llvm::Value* value) const {
    // if value is constant, return the constant value
    auto manager = AnalysisManager::get_instance();
    auto& z3ctx = manager->get_z3ctx();
    if (auto const_value = dyn_cast_or_null<llvm::ConstantInt>(value)) {
        if (const_value->getBitWidth() == 1) {
            return const_value->getZExtValue() ? z3ctx.bool_val(true) : z3ctx.bool_val(false);
        } else {
            return z3ctx.int_val(const_value->getSExtValue());
        }
    }
    auto v = m_stack.read(value);
    if (v.has_value()) {
        return v.value();
    }
    auto it_heap = m_heap.find(value);
    if (it_heap != m_heap.end()) {
        return it_heap->second->read();
    }
    auto it_globals = m_globals.find(value);
    if (it_globals != m_globals.end()) {
        return it_globals->second->read();
    }
    return std::nullopt; // Value not found in stack, heap, or globals
}