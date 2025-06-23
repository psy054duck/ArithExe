#include "state.h"
#include "spdlog/spdlog.h"

using namespace ari_exe;

SymbolTable<FunctionSummary>* State::func_summaries = new SymbolTable<FunctionSummary>();

SymbolTable<LoopSummary>* State::loop_summaries = new SymbolTable<LoopSummary>();

void
State::append_path_condition(const Expression& _path_condition) {
    auto z3_cond = _path_condition.as_expr();
    if (z3_cond.is_int()) {
        path_condition = path_condition && z3_cond != 0;
    } else {
        path_condition = path_condition && z3_cond;
    }
}

Expression
State::evaluate(llvm::Value* v) {
    if (auto undef = llvm::dyn_cast_or_null<llvm::UndefValue>(v)) {
        // If the value is an undef, return a fresh symbolic variable
        auto ty = v->getType();
        std::string name = (undef->getName() + v->getName()).str();
        if (ty->isIntegerTy()) {
            if (ty->getIntegerBitWidth() == 1) {
                // For boolean types, we can use a boolean constant
                return Expression(z3ctx.bool_const(name.c_str()));
            } else {
                return Expression(z3ctx.int_const(name.c_str()));
            }
        } else if (ty->isFloatingPointTy()) {
            // For floating point types, we can use a real constant
            return Expression(z3ctx.real_const(name.c_str()));
        } else {
            assert(false && "Unsupported type for undef value");
        }
    }
    if (auto constant = llvm::dyn_cast_or_null<llvm::ConstantInt>(v)) {
        // If the value is a constant, return its value directly
        if (constant->getBitWidth() == 1) {
            return Expression(z3ctx.bool_val(constant->getSExtValue()));
        }
        return Expression(z3ctx.int_val(constant->getSExtValue()));
    }
    auto obj = memory.get_object(v);
    if (obj) {
        auto res = obj->read().as_expr();
        return res;
    }
    assert(false && "Value not found in memory");
    return Expression(); // return something to avoid compiler warning
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

z3::model
State::get_model() {
    if (model.has_value()) {
        return model.value();
    }
    auto evaluator = z3::solver(z3ctx);
    evaluator.add(get_path_condition().as_expr());
    auto is_sat = evaluator.check();
    assert(is_sat && "Path condition is not satisfiable, this state should not be created");
    model = evaluator.get_model();
    return model.value();
}

bool
State::is_concrete(const Expression& e, const Expression& concrete_value) {
    auto evaluator = z3::solver(z3ctx);
    evaluator.add(get_path_condition().as_expr());
    evaluator.add(concrete_value.as_expr() != e.as_expr());
    auto res = evaluator.check() == z3::check_result::unsat;
    return res;
}

LoopState::LoopState(z3::context& z3ctx, AInstruction* pc, AInstruction* prev_pc, const Memory& memory, const Expression& path_condition, const Expression& path_condition_in_loop, const trace_ty& trace, Status status):
    State(z3ctx, pc, prev_pc, memory, path_condition, trace, status),
    path_condition_in_loop(path_condition_in_loop) {}

RecState::RecState(z3::context& z3ctx, AInstruction* pc, AInstruction* prev_pc, const Memory& memory, const Expression& path_condition, const Expression& path_condition_in_loop, const trace_ty& trace, Status status):
    State(z3ctx, pc, prev_pc, memory, path_condition, trace, status) {}

void
LoopState::append_path_condition(const Expression& _path_condition) {
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
                path_condition_in_loop = path_condition_in_loop && _path_condition.as_expr();
            }
        }
    } else if (auto select = dyn_cast_or_null<llvm::SelectInst>(inst)) {
        path_condition_in_loop = path_condition_in_loop && _path_condition.as_expr();
    } else {
        assert(false && "Unsupported instruction type");
    }
}
