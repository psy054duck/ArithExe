#include "AInstruction.h"
#include "state.h"
#include "function_summary.h"

using namespace ari_exe;

std::map<llvm::Instruction*, AInstruction*>
AInstruction::cached_instructions;

AInstruction*
AInstruction::create(llvm::Instruction* inst) {
    if (cached_instructions.find(inst) != cached_instructions.end()) {
        return cached_instructions.at(inst);
    }

    AInstruction* res = nullptr;
    if (auto bin_inst = llvm::dyn_cast_or_null<llvm::BinaryOperator>(inst)) {
        res = new AInstructionBinary(bin_inst);
    } else if (auto cmp_inst = llvm::dyn_cast_or_null<llvm::ICmpInst>(inst)) {
        res = new AInstructionICmp(cmp_inst);
    } else if (auto call_inst = llvm::dyn_cast_or_null<llvm::CallInst>(inst)) {
        res = new AInstructionCall(call_inst);
    } else if (auto branch_inst = llvm::dyn_cast_or_null<llvm::BranchInst>(inst)) {
        res = new AInstructionBranch(branch_inst);
    } else if (auto ret_inst = llvm::dyn_cast_or_null<llvm::ReturnInst>(inst)) {
        // return instruction, do nothing
        res = new AInstructionReturn(inst);
    } else if (auto zext_inst = llvm::dyn_cast_or_null<llvm::ZExtInst>(inst)) {
        res = new AInstructionZExt(zext_inst);
    } else if (auto phi = llvm::dyn_cast_or_null<llvm::PHINode>(inst)) {
        res = new AInstructionPhi(phi);
    } else if (auto select = llvm::dyn_cast_or_null<llvm::SelectInst>(inst)) {
        res = new AInstructionSelect(select);
    } else {
        llvm::errs() << "Unsupported instruction type\n";
        assert(false);
    }
    cached_instructions.insert_or_assign(inst, res);
    return res;
}

AInstruction*
AInstruction::get_next_instruction() {
    auto next_inst = inst->getNextNonDebugInstruction();
    if (next_inst) {
        return create(next_inst);
    }
    return nullptr;
}

std::vector<State*>
AInstructionBinary::execute(State* state) {
    auto bin_inst = dyn_cast<llvm::BinaryOperator>(inst);
    auto opcode = bin_inst->getOpcode();

    // TODO:
    // Current implementation create new states every time.
    // In future, in-place update should be implemented.
    std::vector<State*> new_states;

    auto op0 = bin_inst->getOperand(0);
    auto op1 = bin_inst->getOperand(1);
    auto op0_value = state->evaluate(op0);
    auto op1_value = state->evaluate(op1);

    auto& z3ctx = op0_value.ctx();
    z3::expr result(z3ctx);

    if (opcode == llvm::Instruction::Add) {
        result = op0_value + op1_value;
    } else if (opcode == llvm::Instruction::Sub) {
        result = op0_value - op1_value;
    } else if (opcode == llvm::Instruction::Mul) {
        result = op0_value * op1_value;
    } else if (opcode == llvm::Instruction::SDiv || opcode == llvm::Instruction::UDiv) {
        result = op0_value / op1_value;
    } else if (opcode == llvm::Instruction::SRem || opcode == llvm::Instruction::URem) {
        result = op0_value % op1_value;
    } else {
        throw std::runtime_error("Unsupported binary operation");
    }

    State* new_state = new State(*state);
    new_state->insert_or_assign(inst, result);
    new_state->step_pc();

    return {new_state};
}

std::vector<State*>
AInstructionICmp::execute(State* state) {
    auto cmp_inst = dyn_cast<llvm::ICmpInst>(inst);
    auto pred = cmp_inst->getPredicate();

    auto op0 = cmp_inst->getOperand(0);
    auto op1 = cmp_inst->getOperand(1);
    auto op0_value = state->evaluate(op0);
    auto op1_value = state->evaluate(op1);
    auto& z3ctx = op0_value.ctx();
    z3::expr result(z3ctx);

    if (pred == llvm::ICmpInst::ICMP_EQ) {
        result = op0_value == op1_value;
    } else if (pred == llvm::ICmpInst::ICMP_NE) {
        result = op0_value != op1_value;
    } else if (llvm::ICmpInst::isLT(pred)) {
        result = op0_value < op1_value;
    } else if (llvm::ICmpInst::isLE(pred)) {
        result = op0_value <= op1_value;
    } else if (llvm::ICmpInst::isGT(pred)) {
        result = op0_value > op1_value;
    } else if (llvm::ICmpInst::isGE(pred)) {
        result = op0_value >= op1_value;
    } else {
        llvm::errs() << "Unsupported ICmp predicate\n";
        assert(false);
    }

    State* new_state = new State(*state);
    new_state->insert_or_assign(inst, result);
    new_state->step_pc();
    return {new_state};
}

std::vector<State*>
AInstructionCall::execute(State* state) {
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = call_inst->getCalledFunction();

    auto& z3ctx = state->path_condition.ctx();

    z3::expr result(z3ctx);
    z3::expr new_path_condition = state->path_condition;

    if (called_func->getName().ends_with("assert")) {
        // verification. should check if the condition is true
        return {execute_assert(state)};
    } else if (called_func->getName().ends_with("assume")) {
        // assume function, add the condition to the path condition
        return {execute_assume(state)};
    } else if (called_func->hasExactDefinition()) {
        return {execute_normal(state)};
    } else {
        return {execute_unknown(state)};
    }
}

State*
AInstructionCall::execute_normal(State* state) {
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = call_inst->getCalledFunction();

    // check if the function is recursive and not yet summarized
    if (is_recursive(called_func) && !State::summaries.get_value(called_func).has_value()) {
        auto summary = summarize_complete(state->path_condition.ctx());
        if (summary.has_value()) {
            State::summaries.insert_or_assign(called_func, *summary);
        }
    }

    auto summary = State::summaries.get_value(called_func);
    if (summary.has_value()) {
        State* new_state = new State(*state);
        z3::expr_vector args(state->path_condition.ctx());
        for (unsigned i = 0; i < call_inst->arg_size(); i++) {
            auto arg = call_inst->getArgOperand(i);
            auto arg_value = state->evaluate(arg);
            args.push_back(arg_value);
        }
        new_state->insert_or_assign(inst, summary->evaluate(args));
        new_state->step_pc();
        return new_state;
    }
    auto& z3ctx = state->path_condition.ctx();

    z3::expr result(z3ctx);

    State* new_state = new State(*state);
    new_state->push_frame();
    // push the arguments to the stack
    for (unsigned i = 0; i < call_inst->arg_size(); i++) {
        auto arg = call_inst->getArgOperand(i);
        auto arg_value = state->evaluate(arg);
        auto param = called_func->getArg(i);
        new_state->insert_or_assign(param, arg_value);
    }
    auto called_first_inst = &*called_func->getEntryBlock().begin();
    auto next_pc = AInstruction::create(called_first_inst);
    new_state->step_pc(next_pc);
    return new_state;
}

State*
AInstructionCall::execute_assert(State* state) {
    // assert function, add the condition to the path condition
    // and check if the condition is true
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = call_inst->getCalledFunction();

    auto& z3ctx = state->path_condition.ctx();

    z3::expr new_path_condition = state->path_condition;

    auto cond = call_inst->getArgOperand(0);
    auto cond_value = state->evaluate(cond);
    auto new_pc = state->pc->get_next_instruction();
    State* new_state = new State(*state);
    new_state->step_pc();
    new_state->status = State::VERIFYING;
    new_state->verification_condition = cond_value;
    return new_state;
}

State*
AInstructionCall::execute_assume(State* state) {
    // assume function, add the condition to the path condition
    // and check if the condition is true
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = call_inst->getCalledFunction();

    auto& z3ctx = state->path_condition.ctx();

    auto cond = call_inst->getArgOperand(0);
    auto cond_value = state->evaluate(cond);
    z3::expr new_path_condition = state->path_condition && cond_value;

    State* new_state = new State(*state);
    new_state->step_pc();
    new_state->status = State::TESTING;
    return new_state;
}

State*
AInstructionCall::execute_unknown(State* state) {
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = call_inst->getCalledFunction();

    auto& z3ctx = state->path_condition.ctx();

    z3::expr result(z3ctx);
    z3::expr new_path_condition = state->path_condition;

    // external function value is unknown, so symbolic
    auto name = inst->getName();
    auto ret_type = call_inst->getType();

    if (ret_type->isIntegerTy()) {
        result = z3ctx.int_const(name.str().c_str());
    } else if (ret_type->isDoubleTy()) {
        result = z3ctx.real_const(name.str().c_str());
    } else if (ret_type->isFloatTy()) {
        result = z3ctx.real_const(name.str().c_str());
    } else {
        llvm::errs() << "Unsupported return type for call instruction\n";
        assert(false);
    }
    State* new_state = new State(*state);
    new_state->insert_or_assign(inst, result);
    new_state->step_pc();
    return new_state;
}

std::vector<State*>
AInstructionCall::execute_if_not_target(State* state, llvm::Function* target) {
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = call_inst->getCalledFunction();

    auto& z3ctx = state->path_condition.ctx();

    if (called_func == target) {
        // TODO: assume all types are int
        size_t num_args = call_inst->arg_size();
        z3::sort_vector domain(z3ctx);
        for (int i = 0; i < num_args; i++) domain.push_back(z3ctx.int_sort());
        z3::func_decl f = z3ctx.function(called_func->getName().str().c_str(), domain, z3ctx.int_sort());
        z3::expr_vector args(z3ctx);
        for (int i = 0; i < num_args; i++) {
            auto arg = call_inst->getArgOperand(i);
            auto arg_value = state->evaluate(arg);
            args.push_back(arg_value);
        }
        auto new_state = state;
        new_state->insert_or_assign(inst, f(args));
        new_state->step_pc();
        return {new_state};
    } else {
        // execute the called function
        auto new_state = execute_normal(state);
        return {new_state};
    }
}

bool
AInstructionCall::is_recursive(llvm::Function* target) {
    for (auto& bb : *target) {
        for (auto& inst : bb) {
            if (auto call_inst = llvm::dyn_cast<llvm::CallInst>(&inst)) {
                auto called_func = call_inst->getCalledFunction();
                if (called_func == target) {
                    return true;
                }
            }
        }
    }
    return false;
}

std::optional<Summary>
AInstructionCall::summarize_complete(z3::context& z3ctx) {
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = call_inst->getCalledFunction();

    function_summary fs(called_func, &z3ctx);
    return fs.get_summary();
}


std::vector<State*>
AInstructionBranch::execute(State* state) {
    auto& z3ctx = state->path_condition.ctx();

    auto branch_inst = dyn_cast<llvm::BranchInst>(inst);
    auto new_trace = state->trace;
    new_trace.push_back(branch_inst->getParent());

    std::vector<State*> new_states;
    if (branch_inst->isConditional()) {

        auto cond = branch_inst->getCondition();
        auto cond_value = state->evaluate(cond);

        auto true_block = branch_inst->getSuccessor(0);
        auto true_pc = AInstruction::create(&*true_block->begin());
        State* true_state = new State(*state);
        true_state->trace = new_trace;
        true_state->status = State::TESTING;
        true_state->path_condition = state->path_condition && cond_value;
        true_state->step_pc(true_pc);

        auto false_block = branch_inst->getSuccessor(1);
        auto false_pc = AInstruction::create(&*false_block->begin());
        State* false_state = new State(*state);
        false_state->trace = new_trace;
        false_state->status = State::TESTING;
        false_state->path_condition = state->path_condition && !cond_value;
        false_state->step_pc(false_pc);

        new_states.push_back(true_state);
        new_states.push_back(false_state);
    } else {
        // unconditional branch
        auto next_pc = AInstruction::create(&*branch_inst->getSuccessor(0)->begin());
        State* new_state = new State(*state);
        new_state->trace = new_trace;
        new_state->step_pc(next_pc);
        new_states.push_back(new_state);
    }
    return new_states;
}

std::vector<State*>
AInstructionReturn::execute(State* state) {
    if (state->stack.size() == 1) {
        // no function call, just terminate
        state->status = State::TERMINATED;
        return {state};
    }
    auto ret = dyn_cast<llvm::ReturnInst>(inst);
    auto ret_value = ret->getReturnValue();
    // get the return value
    auto ret_expr = state->evaluate(ret_value);

    State* new_state = new State(*state);
    auto frame = new_state->pop_frame();
    // go back to the called site
    new_state->step_pc(frame.prev_pc);
    auto call_inst = dyn_cast<llvm::CallInst>(new_state->pc->inst);
    assert(call_inst);
    new_state->insert_or_assign(call_inst, ret_expr);
    new_state->step_pc();
    return {new_state};
}

std::vector<State*>
AInstructionZExt::execute(State* state) {
    auto zext_inst = dyn_cast<llvm::ZExtInst>(inst);
    auto op = zext_inst->getOperand(0);
    auto op_value = state->evaluate(op);
    auto& z3ctx = op_value.ctx();
    z3::expr result(z3ctx);

    State* new_state = new State(*state);
    new_state->insert_or_assign(inst, op_value);
    new_state->step_pc();
    return {new_state};
}

std::vector<State*>
AInstructionPhi::execute(State* state) {
    auto phi_inst = dyn_cast<llvm::PHINode>(inst);
    auto& z3ctx = state->path_condition.ctx();
    auto prev_block = state->trace.back();
    llvm::Value* selected_value = phi_inst->getIncomingValueForBlock(prev_block);
    auto selected_value_expr = state->evaluate(selected_value);

    State* new_state = new State(*state);
    new_state->insert_or_assign(inst, selected_value_expr);
    new_state->step_pc();

    return {new_state};
}

std::vector<State*>
AInstructionSelect::execute(State* state) {
    auto select_inst = dyn_cast<llvm::SelectInst>(inst);
    auto cond = select_inst->getCondition();
    auto cond_value = state->evaluate(cond);

    auto true_value = select_inst->getTrueValue();
    auto true_value_expr = state->evaluate(true_value);
    State* true_state = new State(*state);
    true_state->insert_or_assign(inst, true_value_expr);
    true_state->step_pc();
    true_state->status = State::TESTING;
    true_state->path_condition = state->path_condition && cond_value;

    auto false_value = select_inst->getFalseValue();
    auto false_value_expr = state->evaluate(false_value);
    State* false_state = new State(*state);
    false_state->insert_or_assign(inst, false_value_expr);
    false_state->step_pc();
    false_state->status = State::TESTING;
    false_state->path_condition = state->path_condition && !cond_value;
    
    return {true_state, false_state};
}

llvm::BasicBlock*
AInstruction::get_block() {
    return inst->getParent();
}
