#include "AStack.h"

using namespace ari_exe;


AStack::StackFrame&
AStack::push_frame(const StackFrame& frame) {
    frames.push(frame);
    return frames.top();
}

AStack::StackFrame&
AStack::push_frame() {
    frames.push(StackFrame());
    return frames.top();
}

AStack::StackFrame
AStack::pop_frame() {
    assert(!frames.empty());
    auto frame = frames.top();
    frames.pop();
    return frame;
}

AStack::StackFrame&
AStack::top_frame() {
    assert(!frames.empty());
    return frames.top();
}

std::optional<z3::expr>
AStack::evaluate(llvm::Value* v) {
    assert(!frames.empty());
    auto& top_frame = frames.top();
    return top_frame.table.get_value(v);
}

void
AStack::insert_or_assign_value(llvm::Value* v, z3::expr value) {
    assert(!frames.empty());
    auto& top_frame = frames.top();
    top_frame.insert_or_assign(v, value);
}

void
AStack::StackFrame::insert_or_assign(llvm::Value* symbol, z3::expr value) {
    table.insert_or_assign(symbol, value);
}