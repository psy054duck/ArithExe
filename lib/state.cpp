#include "state.h"

using namespace ari_exe;

SymbolTable<Summary> State::summaries;

z3::expr
State::evaluate(llvm::Value* v) {
    auto stack_value = stack.evaluate(v);
    if (stack_value.has_value()) {
        return *stack_value;
    }
    auto globals_value = globals.get_value(v);
    if (globals_value.has_value()) {
        return *globals_value;
    }
    // if not found, assume it is a constant int
    // TODO: support other types
    auto const_val = llvm::dyn_cast_or_null<llvm::ConstantInt>(v);
    if (const_val) {
        return z3ctx.int_val(const_val->getSExtValue());
    }
    llvm::errs() << "Value not found in state: " << *v << "\n";
    assert(false);
}

void
State::insert_or_assign(llvm::Value* v, z3::expr value) {
    auto stack_value = stack.evaluate(v);
    if (stack_value.has_value()) {
        stack.insert_or_assign_value(v, value);
    } else if (globals.get_value(v).has_value()) {
        globals.insert_or_assign(v, value);
    } else {
        stack.insert_or_assign_value(v, value);
    }
}

void
State::step_pc(AInstruction* next_pc) {
    prev_pc = pc;
    if (next_pc) {
        pc = next_pc;
    } else {
        pc = pc->get_next_instruction();
    }
}

void
State::push_value(llvm::Value* v, z3::expr value) {
    stack.insert_or_assign_value(v, value);
}

AStack::StackFrame&
State::push_frame() {
    auto& frame = stack.push_frame();
    // record the called site
    frame.prev_pc = pc;
    return frame;
}

AStack::StackFrame
State::pop_frame() {
    return stack.pop_frame();
}