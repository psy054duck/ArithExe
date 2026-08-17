#include "state.h"
#include "spdlog/spdlog.h"

using namespace ari_exe;

void
State::append_path_condition(const Expression& _path_condition) {
    auto normalize_bool = [](const z3::expr& expr) {
        return expr.is_int() ? expr != 0 : expr;
    };

    auto current = normalize_bool(path_condition.as_expr());
    auto appended = normalize_bool(_path_condition.as_expr());
    z3::expr_vector conditions(z3ctx);
    z3::expr_vector expressions(z3ctx);
    conditions.push_back(z3ctx.bool_val(true));
    expressions.push_back(current && _path_condition.defined() && appended);
    path_condition = Expression(conditions, expressions);
    model.reset();
}

Expression
State::evaluate(llvm::Value* v, bool is_signed) {
    (void)is_signed;
    if (auto undef = llvm::dyn_cast_or_null<llvm::UndefValue>(v)) {
        // If the value is an undef, return a fresh symbolic variable
        auto ty = v->getType();
        std::string name =
            "ari_undef_" + std::to_string(session.next_call_value_id(v));
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
        return obj->read();
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

LoopState::LoopState(VerificationSession& session, z3::context& z3ctx,
                     AInstruction* pc, AInstruction* prev_pc,
                     const Memory& memory, const Expression& path_condition,
                     const Expression& path_condition_in_loop,
                     const trace_ty& trace, Status status):
    State(session, z3ctx, pc, prev_pc, memory, path_condition, trace, status),
    path_condition_in_loop(path_condition_in_loop),
    summarizing_loop(nullptr) {}

RecState::RecState(VerificationSession& session, z3::context& z3ctx,
                   AInstruction* pc, AInstruction* prev_pc,
                   const Memory& memory, const Expression& path_condition,
                   const Expression& path_condition_in_loop,
                   const trace_ty& trace, Status status):
    State(session, z3ctx, pc, prev_pc, memory, path_condition, trace, status) {}

void
LoopState::append_path_condition(const Expression& _path_condition) {
    State::append_path_condition(_path_condition);
    auto normalize_bool = [](z3::expr expr) {
        if (expr.is_int()) return expr != 0;
        return expr;
    };

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
                path_condition_in_loop = path_condition_in_loop && normalize_bool(_path_condition.as_expr());
            }
        }
    } else if (auto select = dyn_cast_or_null<llvm::SelectInst>(inst)) {
        path_condition_in_loop = path_condition_in_loop && normalize_bool(_path_condition.as_expr());
    } else {
        assert(false && "Unsupported instruction type");
    }
}
