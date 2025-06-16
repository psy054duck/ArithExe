#include "state.h"
#include "spdlog/spdlog.h"

using namespace ari_exe;

SymbolTable<FunctionSummary>* State::func_summaries = new SymbolTable<FunctionSummary>();

SymbolTable<LoopSummary>* State::loop_summaries = new SymbolTable<LoopSummary>();

void
State::append_path_condition(z3::expr _path_condition) {
    if (_path_condition.is_int()) {
        path_condition = path_condition && _path_condition != 0;
    } else {
        path_condition = path_condition && _path_condition;
    }
}

z3::expr
State::evaluate(llvm::Value* v) {
    auto value = memory.read(v);
    if (value.has_value()) {
        return *value;
    }
    assert(false && "Value not found in memory");
    return value.value(); // return something to avoid compiler warning
}

z3::expr
State::load(llvm::Value* v) const {
    auto value = memory.read(v);
    if (value.has_value()) {
        return *value;
    }
    assert(false && "Value not found in memory");
    return value.value(); // return something to avoid compiler warning
}

void
State::write(llvm::Value* v, z3::expr value) {
    memory.write(v, value);
}

void
State::write(llvm::Value* v, MemoryObjectPtr memory_object) {
    memory.write(v, memory_object);
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

MemoryObjectPtr
State::get_memory_object(llvm::Value* value) const {
    auto m_obj_opt = memory.get_memory_object(value);
    assert(m_obj_opt.has_value() && "Memory object not found");
    return m_obj_opt.value();
}

MStack::StackFrame&
State::push_frame(llvm::Function* func) {
    auto& frame = memory.push_frame();
    frame.func = func;
    // record the called site
    frame.prev_pc = pc;
    return frame;
}

z3::model
State::get_model() {
    if (model.has_value()) {
        return model.value();
    }
    auto evaluator = z3::solver(z3ctx);
    evaluator.add(get_path_condition());
    auto is_sat = evaluator.check();
    assert(is_sat && "Path condition is not satisfiable, this state should not be created");
    model = evaluator.get_model();
    return model.value();
}

bool
State::is_concrete(z3::expr e, z3::expr concrete_value) {
    auto evaluator = z3::solver(z3ctx);
    evaluator.add(get_path_condition());
    evaluator.add(concrete_value != e);
    auto res = evaluator.check() == z3::check_result::unsat;
    return res;
}

MStack::StackFrame
State::pop_frame() {
    return memory.pop_frame();
}

LoopState::LoopState(z3::context& z3ctx, AInstruction* pc, AInstruction* prev_pc, const Memory& memory, z3::expr path_condition, z3::expr path_condition_in_loop, const trace_ty& trace, Status status):
    State(z3ctx, pc, prev_pc, memory, path_condition, trace, status),
    path_condition_in_loop(path_condition_in_loop) {}

RecState::RecState(z3::context& z3ctx, AInstruction* pc, AInstruction* prev_pc, const Memory& memory, z3::expr path_condition, z3::expr path_condition_in_loop, const trace_ty& trace, Status status):
    State(z3ctx, pc, prev_pc, memory, path_condition, trace, status) {}

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

MStack::StackFrame&
State::top_frame() {
    return memory.top_frame();
}