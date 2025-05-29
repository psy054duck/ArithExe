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
    auto value = memory.read(v);
    if (value.has_value()) {
        return *value;
    }
    assert(false && "Value not found in memory");
}

void
State::write(llvm::Value* v, z3::expr value) {
    memory.write(v, value);
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

// void
// State::push_value(llvm::Value* v, z3::expr value) {
//     stack.insert_or_assign_value(v, value);
// }

MStack::StackFrame&
State::push_frame() {
    auto& frame = memory.push_frame();
    // record the called site
    frame.prev_pc = pc;
    return frame;
}

MStack::StackFrame
State::pop_frame() {
    return memory.pop_frame();
}

LoopState::LoopState(z3::context& z3ctx, AInstruction* pc, AInstruction* prev_pc, const Memory& memory, z3::expr path_condition, z3::expr path_condition_in_loop, const trace_ty& trace, Status status):
    State(z3ctx, pc, prev_pc, memory, path_condition, trace, status),
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

void
State::store_gep(llvm::GetElementPtrInst* gep) {
    memory.store_gep(gep);
}

Memory::ObjectPtr
State::get_gep(llvm::GetElementPtrInst* gep) const {
    return memory.get_gep(gep);
}