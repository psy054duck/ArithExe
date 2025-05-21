#include "state.h"
#include "spdlog/spdlog.h"

using namespace ari_exe;

SymbolTable<FunctionSummary>* State::func_summaries = new SymbolTable<FunctionSummary>();

SymbolTable<LoopSummary>* State::loop_summaries = new SymbolTable<LoopSummary>();

void
State::append_path_condition(z3::expr _path_condition) {
    path_condition = path_condition && _path_condition;
}

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
    spdlog::error("Value {} not found in state", v->getName().str());
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

LoopState::LoopState(z3::context& z3ctx, AInstruction* pc, AInstruction* prev_pc, const SymbolTable<z3::expr>& globals, const AStack& stack, z3::expr path_condition, z3::expr path_condition_in_loop, const trace_ty& trace, Status status):
    State(z3ctx, pc, prev_pc, globals, stack, path_condition, trace, status),
    path_condition_in_loop(path_condition_in_loop) {}


void
LoopState::append_path_condition(z3::expr _path_condition) {
    State::append_path_condition(_path_condition);

    auto manager = AnalysisManager::get_instance();
    auto inst = pc->inst;
    auto& LI = manager->get_LI(inst->getFunction());
    auto loop = LI.getLoopFor(inst->getParent());
    assert(loop);

    if (auto branch = dyn_cast_or_null<llvm::BranchInst>(inst)) {
        if (branch->isConditional()) {
            auto true_block = branch->getSuccessor(0);
            auto false_block = branch->getSuccessor(1);
            if (loop->contains(true_block) && loop->contains(false_block)) {
                path_condition_in_loop = path_condition_in_loop && _path_condition;
            }
        }
    } else if (auto select = dyn_cast_or_null<llvm::SelectInst>(inst)) {
        path_condition_in_loop = path_condition_in_loop && _path_condition;
    } else {
        assert(false && "Unsupported instruction type");
    }
}