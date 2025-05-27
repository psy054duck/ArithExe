#include "MStack.h"

using namespace ari_exe;


MStack::StackFrame&
MStack::push_frame(const StackFrame& frame) {
    frames.push(frame);
    return frames.top();
}

MStack::StackFrame&
MStack::push_frame() {
    frames.push(StackFrame());
    return frames.top();
}

MStack::StackFrame
MStack::pop_frame() {
    assert(!frames.empty());
    auto frame = frames.top();
    frames.pop();
    return frame;
}

MStack::StackFrame&
MStack::top_frame() {
    assert(!frames.empty());
    return frames.top();
}

std::optional<z3::expr>
MStack::read(llvm::Value* v) const {
    assert(!frames.empty());
    const auto& top_frame = frames.top();
    top_frame.read(v);
    return frames.top().read(v);
}

void
MStack::write(llvm::Value* v, z3::expr value) {
    assert(!frames.empty());
    auto& top_frame = frames.top();
    top_frame.write(v, value);
}

void
MStack::write(llvm::Value* v, z3::expr_vector index, z3::expr value) {
    assert(!frames.empty());
    auto& top_frame = frames.top();
    auto mem_obj = top_frame.get_memory_object(v);
    if (mem_obj.has_value()) {
        mem_obj->write(index, value);
    }
    throw std::runtime_error("Memory object not found in the top frame.");
}

void
MStack::StackFrame::write(llvm::Value* symbol, z3::expr value) {
    // insert_or_assign(symbol, value);
    auto it = m_objects.find(symbol);
    if (it != m_objects.end()) {
        it->second.write(value);
    } else {
        MemoryObject mem_obj(symbol, value);
        m_objects.insert_or_assign(symbol, mem_obj);
    }
}

std::string
MStack::StackFrame::to_string() const {
    std::string result = "StackFrame:\n";
    // result += memory.to_string() + "\n";
    for (auto& pair : m_objects) {
        result += "  " + pair.first->getName().str() + ": " + pair.second.read().value().to_string() + "\n";
    }
    return result;
}

std::optional<z3::expr>
MStack::StackFrame::read(llvm::Value* v) const {
    auto it = m_objects.find(v);
    if (it != m_objects.end()) {
        return it->second.read();
    }
    return std::nullopt;
}
                                
std::optional<MemoryObject>
MStack::StackFrame::get_memory_object(llvm::Value* v) const {
    auto it = m_objects.find(v);
    if (it != m_objects.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<MemoryObject>
MStack::get_memory_object(llvm::Value* v) const {
    return frames.top().get_memory_object(v);
}