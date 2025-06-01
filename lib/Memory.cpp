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
    if (auto undef = dyn_cast_or_null<llvm::UndefValue>(value)) {
        auto name = "_undef_" + std::to_string(manager->unknown_counter++);
        return z3ctx.int_const(name.c_str());
    }
    if (auto const_value = dyn_cast_or_null<llvm::ConstantInt>(value)) {
        if (const_value->getBitWidth() == 1) {
            return const_value->getZExtValue() ? z3ctx.bool_val(true) : z3ctx.bool_val(false);
        } else {
            return z3ctx.int_val(const_value->getSExtValue());
        }
    }

    // if the value is a pointer
    auto value_type = value->getType();
    if (value_type->isPointerTy()) {
        // check if the value is a global variable
        if (auto global_var = dyn_cast_or_null<llvm::GlobalVariable>(value)) {
            auto it_globals = m_globals.find(global_var);
            if (it_globals != m_globals.end()) {
                return it_globals->second->read();
            }
        } else if (auto alloca_inst = dyn_cast_or_null<llvm::AllocaInst>(value)) {
            // check if the value is an alloca instruction
            // this is a stack variable, so we can read it from the stack
            return m_stack.read(value);
        } else if (auto gep = dyn_cast_or_null<llvm::GetElementPtrInst>(value)) {
            // if the value is a GEP, we need to parse it to get the base pointer and the indices
            auto gep_info = parse_gep(gep);
            auto base_ptr = gep_info.first;
            auto indices = gep_info.second;

            // read the base pointer from the stack or heap
            return read(base_ptr, indices);
        } else if (auto call = dyn_cast_or_null<llvm::CallInst>(value)) {
            // if the value is a call instruction, we can read the return value from the stack
            if (call->getCalledFunction()->getName() == "malloc") {
                return m_heap.at(value)->read();
            }
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

Memory::ObjectPtr
Memory::parse_gep(llvm::GetElementPtrInst* gep) const {
    // get the base pointer and the indices
    auto& z3ctx = AnalysisManager::get_instance()->get_z3ctx();
    llvm::Value* base_ptr = gep->getPointerOperand();

    z3::expr_vector indices(z3ctx);
    if (auto gep_inst = dyn_cast_or_null<llvm::GetElementPtrInst>(base_ptr)) {
        // if the base pointer is also a GEP, recursively parse it
        auto base = parse_gep(gep_inst);
        base_ptr = base.first;
        for (const auto& index : base.second) {
            indices.push_back(index);
        }
    }
    for (auto& idx : gep->indices()) {
        auto idx_value = idx.get();
        auto cur_idx_value = read(idx_value);
        assert(cur_idx_value.has_value() && "GEP index value should be readable");
        indices.push_back(cur_idx_value.value());
    }
    return std::make_pair(base_ptr, indices);
}

void
Memory::store_gep(llvm::GetElementPtrInst* gep) {
    // parse the GEP to get the base pointer and the indices
    auto gep_info = parse_gep(gep);
    m_pointers.insert_or_assign(gep, gep_info);
}

Memory::ObjectPtr
Memory::get_gep(llvm::GetElementPtrInst* gep) const {
    return m_pointers.at(gep);
}

void
Memory::write(llvm::Value* value, z3::expr val) {
    // if value is a pointer
    val = val.simplify(); // Simplify the value before writing
    auto value_type = value->getType();
    if (value_type->isPointerTy()) {
        // check if the value is a global variable
        if (auto global_var = dyn_cast_or_null<llvm::GlobalVariable>(value)) {
            auto it_globals = m_globals.find(global_var);
            if (it_globals != m_globals.end()) {
                it_globals->second = it_globals->second->write(val);
                return;
            }
        } else if (auto alloca_inst = dyn_cast_or_null<llvm::AllocaInst>(value)) {
            // this is a stack variable, so we can write it to the stack
            m_stack.write(value, val);
            return;
        } else if (auto gep = dyn_cast_or_null<llvm::GetElementPtrInst>(value)) {
            // if the value is a GEP, we need to parse it to get the base pointer and the indices
            auto gep_info = parse_gep(gep);
            auto base_ptr = gep_info.first;
            auto indices = gep_info.second;

            // write the value to the base pointer with the indices

            write(base_ptr, indices, val);
            return;
        } else if (auto call = dyn_cast_or_null<llvm::CallInst>(value)) {
            // if the value is a call instruction, we can write the return value to the heap
            if (call->getCalledFunction()->getName() == "malloc") {
                auto new_mem_obj = m_heap.at(value)->write(val);
                m_heap.insert_or_assign(value, new_mem_obj);
                return;
            }
        }
        assert(false && "Unsupported pointer type for writing");
    }

    if (m_stack.read(value).has_value()) {
        m_stack.write(value, val);
    } else {
        auto it_heap = m_heap.find(value);
        if (it_heap != m_heap.end()) {
            it_heap->second->write(val);
        } else {
            auto it_globals = m_globals.find(value);
            if (it_globals != m_globals.end()) {
                it_globals->second->write(val);
            } else {
                // If the value is not found in stack, heap, or globals,
                // write it to the stack.
                m_stack.write(value, val);
            }
        }
    }
}

void 
Memory::write(llvm::Value* value, z3::expr_vector index, z3::expr val) {
    val = val.simplify(); // Simplify the value before writing
    if (m_stack.read(value).has_value()) {
        m_stack.write(value, index, val);
    } else {
        auto it_heap = m_heap.find(value);
        if (it_heap != m_heap.end()) {
            it_heap->second = it_heap->second->write(index, val);
        } else {
            auto it_globals = m_globals.find(value);
            if (it_globals != m_globals.end()) {
                it_globals->second = it_globals->second->write(index, val);
            } else {
                throw std::runtime_error("Memory object not found for writing");
            }
        }
    }
}

std::optional<z3::expr>
Memory::read(llvm::Value* value, z3::expr_vector index) const {
    auto v = m_stack.read(value);
    if (v.has_value()) {
        return v.value();
    }
    auto it_heap = m_heap.find(value);
    if (it_heap != m_heap.end()) {
        return it_heap->second->read(index);
    }
    auto it_globals = m_globals.find(value);
    if (it_globals != m_globals.end()) {
        return it_globals->second->read(index);
    }
    return std::nullopt;
}

Memory::ObjectPtr
Memory::parse_pointer(llvm::Value* value) const {
    // if value is a pointer, return the base pointer and the indices
    auto& z3ctx = AnalysisManager::get_instance()->get_z3ctx();
    if (auto gep = dyn_cast_or_null<llvm::GetElementPtrInst>(value)) {
        return parse_gep(gep);
    }
    // assume it points to a memory object with zero offset
    auto m_obj_opt = get_memory_object(value);
    assert(m_obj_opt.has_value() && "Memory object should be found for the given value");
    auto m_obj = m_obj_opt.value();
    z3::expr_vector indices(z3ctx);
    for (int i = 0; i < m_obj->get_dims().size(); i++) {
        indices.push_back(z3ctx.int_val(0));
    }
    return std::make_pair(value, indices);
}

std::vector<MemoryObjectArrayPtr>
Memory::get_arrays() const {
    std::vector<MemoryObjectArrayPtr> arrays = m_stack.get_arrays();

    for (const auto& [key, value] : m_heap) {
        if (value->is_array()) {
            arrays.push_back(dynamic_pointer_cast<MemoryObjectArray>(value));
        }
    }

    for (const auto& [key, value] : m_globals) {
        if (value->is_array()) {
            arrays.push_back(dynamic_pointer_cast<MemoryObjectArray>(value));
        }
    }

    return arrays;
}