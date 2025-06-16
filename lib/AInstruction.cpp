#include "AInstruction.h"
#include <spdlog/spdlog.h>

#include "z3++.h"

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
    } else if (auto sext_inst = llvm::dyn_cast_or_null<llvm::SExtInst>(inst)) {
        res = new AInstructionSExt(sext_inst);
    } else if (auto phi = llvm::dyn_cast_or_null<llvm::PHINode>(inst)) {
        res = new AInstructionPhi(phi);
    } else if (auto select = llvm::dyn_cast_or_null<llvm::SelectInst>(inst)) {
        res = new AInstructionSelect(select);
    } else if (auto load_inst = llvm::dyn_cast_or_null<llvm::LoadInst>(inst)) {
        res = new AInstructionLoad(load_inst);
    } else if (auto store_inst = llvm::dyn_cast_or_null<llvm::StoreInst>(inst)) {
        res = new AInstructionStore(store_inst);
    } else if (auto gep_inst = llvm::dyn_cast_or_null<llvm::GetElementPtrInst>(inst)) {
        res = new AInstructionGEP(gep_inst);
    } else if (auto alloca_inst = llvm::dyn_cast_or_null<llvm::AllocaInst>(inst)) {
        res = new AInstructionAlloca(alloca_inst);
    } else {
        llvm::errs() << "Unsupported instruction type\n";
        assert(false);
    }
    cached_instructions.insert_or_assign(inst, res);
    return res;
}

loop_state_list
AInstruction::execute(loop_state_ptr state) {
    auto new_states = execute(std::static_pointer_cast<State>(state));
    loop_state_list new_base_states;
    for (auto& new_state : new_states) {
        auto new_loop_state = std::make_shared<LoopState>(LoopState(*new_state));
        new_loop_state->path_condition_in_loop = state->path_condition_in_loop;
        new_base_states.push_back(new_loop_state);
    }
    return new_base_states;
}

rec_state_list
AInstruction::execute(rec_state_ptr state) {
    auto new_states = execute(std::static_pointer_cast<State>(state));
    rec_state_list new_base_states;
    for (auto& new_state : new_states) {
        auto new_loop_state = std::make_shared<RecState>(RecState(*new_state));
        new_base_states.push_back(new_loop_state);
    }
    return new_base_states;
}

AInstruction*
AInstruction::get_next_instruction() {
    auto next_inst = inst->getNextNonDebugInstruction();
    if (next_inst) {
        return create(next_inst);
    }
    return nullptr;
}

state_list
AInstructionBinary::execute(state_ptr state) {
    auto bin_inst = dyn_cast<llvm::BinaryOperator>(inst);
    auto opcode = bin_inst->getOpcode();

    // TODO:
    // Current implementation create new states every time.
    // In future, in-place update should be implemented.
    std::vector<state_ptr> new_states;

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
    } else if (opcode == llvm::Instruction::And) {
        result = op0_value & op1_value;
    } else if (opcode == llvm::Instruction::Or) {
        result = op0_value | op1_value;
    } else if (opcode == llvm::Instruction::Xor) {
        result = op0_value ^ op1_value;
    } else {
        throw std::runtime_error("Unsupported binary operation");
    }

    state_ptr new_state = std::make_shared<State>(*state);
    new_state->write(inst, result);
    new_state->step_pc();

    return {new_state};
}

std::vector<state_ptr>
AInstructionICmp::execute(state_ptr state) {
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
    state_ptr new_state = std::make_shared<State>(*state);
    new_state->write(inst, result);
    new_state->step_pc();
    return {new_state};
}

std::vector<state_ptr>
AInstructionAlloca::execute(state_ptr state) {
    auto alloca_inst = dyn_cast<llvm::AllocaInst>(inst);
    auto& z3ctx = state->z3ctx;

    assert(!alloca_inst->isArrayAllocation() && "Array allocation is not supported yet");
    // Allocate memory for the alloca instruction
    auto size = alloca_inst->getAllocatedType()->getPrimitiveSizeInBits() / 8;
    
    z3::expr non_det(z3ctx);
    state_ptr new_state = std::make_shared<State>(*state);
    new_state->memory.allocate(inst, non_det);
    new_state->step_pc();
    return {new_state};
}

std::vector<state_ptr>
AInstructionCall::execute(state_ptr state) {
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = call_inst->getCalledFunction();

    auto& z3ctx = state->z3ctx;

    z3::expr result(z3ctx);

    if (called_func && called_func->getName().ends_with("assert")) {
        // verification. should check if the condition is true
        return {execute_assert(state)};
    } else if (called_func && called_func->getName().find("reach_error") != std::string::npos) {
        return {execute_reach_error(state)};
    } else if (called_func && called_func->getName().find("assume") != std::string::npos) {
        // assume function, add the condition to the path condition
        return {execute_assume(state)};
    } else if (called_func && called_func->getName().find("malloc") != std::string::npos) {
        // malloc function, allocate memory and return the pointer
        return {execute_malloc(state)};
    } else if (called_func && called_func->hasExactDefinition()) {
        return {execute_normal(state)};
    } else {
        return {execute_unknown(state)};
    }
}

state_ptr
AInstructionCall::execute_normal(state_ptr state) {
    auto try_cache = execute_cache(state);
    if (try_cache) return try_cache;

    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = call_inst->getCalledFunction();

    // check if the function is recursive and not yet summarized
    auto ret_type = called_func->getReturnType();
    bool is_visited = Cache::get_instance()->is_visited(called_func);
    if (!ret_type->isVoidTy() && !is_visited && !state->is_summarizing() && is_recursive(called_func) && !State::func_summaries->get_value(called_func).has_value()) {
        Cache::get_instance()->mark_visited(called_func);
        auto summary = summarize_complete(state->z3ctx);
        if (summary.has_value()) {
            State::func_summaries->insert_or_assign(called_func, *summary);
        }
    }

    auto summary = State::func_summaries->get_value(called_func);
    auto& z3ctx = state->z3ctx;

    if (summary.has_value()) {
        state_ptr new_state = std::make_shared<State>(*state);
        z3::expr_vector args(z3ctx);
        for (unsigned i = 0; i < call_inst->arg_size(); i++) {
            auto arg = call_inst->getArgOperand(i);
            auto arg_value = state->evaluate(arg);
            args.push_back(arg_value);
        }
        new_state->write(inst, summary->evaluate(args));
        new_state->step_pc();
        return new_state;
    }

    z3::expr result(z3ctx);

    state_ptr new_state = std::make_shared<State>(*state);
    new_state->push_frame(called_func);
    // push the arguments to the stack
    for (unsigned i = 0; i < call_inst->arg_size(); i++) {
        auto arg = call_inst->getArgOperand(i);
        auto param = called_func->getArg(i);
        if (arg->getType()->isPointerTy()) {
            auto obj = state->get_memory_object(arg);
            new_state->write(param, obj);
        } else {
            auto arg_value = state->evaluate(arg);
            new_state->write(param, arg_value);
        }
    }
    auto called_first_inst = &*called_func->getEntryBlock().getFirstNonPHIOrDbg();
    auto next_pc = AInstruction::create(called_first_inst);
    new_state->step_pc(next_pc);
    return new_state;
}

state_ptr
AInstructionCall::execute_cache(state_ptr state) {
    auto cache = Cache::get_instance();
    auto call_inst = dyn_cast_or_null<llvm::CallInst>(inst);
    assert(call_inst);
    auto called_func = call_inst->getCalledFunction();

    auto model = state->get_model();
    param_list_ty args;
    for (int i = 0; i < call_inst->arg_size(); i++) {
        auto arg = call_inst->getArgOperand(i);
        auto state_value = state->evaluate(arg);
        auto concrete_value = model.eval(state_value, true);
        if (!state->is_concrete(state_value, concrete_value)) {
            return nullptr;
        }
        args.push_back(concrete_value.as_int64());
    }
    auto cached_value = cache->get_func_value(called_func, args);
    if (cached_value.has_value()) {
        // if the function is cached, return the cached value
        state_ptr new_state = std::make_shared<State>(*state);
        auto& z3ctx = state->z3ctx;
        new_state->write(inst, z3ctx.int_val(cached_value.value()));
        new_state->step_pc();
        return new_state;
    }
    return nullptr;
}

state_ptr
AInstructionCall::execute_assert(state_ptr state) {
    // assert function, add the condition to the path condition
    // and check if the condition is true
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = call_inst->getCalledFunction();

    auto& z3ctx = state->z3ctx;

    auto cond = call_inst->getArgOperand(0);
    auto cond_value = state->evaluate(cond);
    auto new_pc = state->pc->get_next_instruction();
    state_ptr new_state = std::make_shared<State>(*state);
    new_state->step_pc();
    new_state->status = State::VERIFYING;
    if (cond_value.is_bool()) {
        new_state->verification_condition = cond_value;
    } else {
        new_state->verification_condition = cond_value != z3ctx.int_val(0);
    }
    return new_state;
}

state_ptr
AInstructionCall::execute_reach_error(state_ptr state) {
    // assert function, add the condition to the path condition
    // and check if the condition is true
    state->status = State::REACH_ERROR;
    return state;
}

state_ptr
AInstructionCall::execute_assume(state_ptr state) {
    // assume function, add the condition to the path condition
    // and check if the condition is true
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = call_inst->getCalledFunction();

    auto& z3ctx = state->z3ctx;

    auto cond = call_inst->getArgOperand(0);
    auto cond_value = state->evaluate(cond);

    state_ptr new_state = std::make_shared<State>(*state);
    new_state->append_path_condition(cond_value);
    new_state->step_pc();
    new_state->status = State::TESTING;
    return new_state;
}

state_ptr
AInstructionCall::execute_unknown(state_ptr state) {
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = call_inst->getCalledFunction();

    auto& z3ctx = state->z3ctx;

    z3::expr result(z3ctx);

    // external function value is unknown, so symbolic
    auto name = "ari_" + inst->getName();
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
    state_ptr new_state = std::make_shared<State>(*state);
    new_state->write(inst, result);
    if (called_func && called_func->getName().ends_with("uint")) {
        new_state->append_path_condition(result >= z3ctx.int_val(0));
    }
    new_state->step_pc();
    return new_state;
}

state_ptr
AInstructionCall::execute_malloc(state_ptr state) {
    // malloc function, allocate memory and return the pointer
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto size_bytes = call_inst->arg_begin()->get();
    auto size_bytes_expr = state->evaluate(size_bytes);
    // TODO: assume it an integer array and the size of an int is 32 bits;
    // TODO: assume it is 1-d
    z3::expr_vector dims(state->z3ctx);
    dims.push_back((size_bytes_expr / state->z3ctx.int_val(4)).simplify());
    auto new_state = std::make_shared<State>(*state);
    new_state->memory.allocate(call_inst, dims);
    new_state->step_pc();
    return {new_state};
}

std::vector<state_ptr>
AInstructionCall::execute_if_not_target(state_ptr state, llvm::Function* target) {
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = call_inst->getCalledFunction();

    auto& z3ctx = state->z3ctx;

    if (called_func == target) {
        // TODO: assume all types are int
        return execute_naively(state);
    } else {
        // execute the called function
        auto new_state = execute_normal(state);
        // return execute_if_not_target(new_state, target);
        return {new_state};
    }
}

std::vector<state_ptr>
AInstructionCall::execute_naively(state_ptr state) {
    // execute the function call f(args) by simply creating z3::expr f(args)
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = call_inst->getCalledFunction();

    auto& z3ctx = state->z3ctx;

    size_t num_args = call_inst->arg_size();
    z3::sort_vector domain(z3ctx);
    for (int i = 0; i < num_args; i++) domain.push_back(z3ctx.int_sort());
    auto func_name = "ari_" + called_func->getName().str();
    z3::func_decl f = z3ctx.function(func_name.c_str(), domain, z3ctx.int_sort());
    z3::expr_vector args(z3ctx);
    for (int i = 0; i < num_args; i++) {
        auto arg = call_inst->getArgOperand(i);
        auto arg_value = state->evaluate(arg);
        args.push_back(arg_value);
    }
    auto new_state = std::make_shared<State>(*state);
    new_state->write(inst, f(args));
    new_state->step_pc();
    return {new_state};
}

bool
AInstructionCall::is_recursive(llvm::Function* target) {
    // this function is generated by Copilot
    auto CG = AnalysisManager::get_instance()->get_CG();
    auto* node = CG->operator[](target);
    std::set<llvm::CallGraphNode*> visited;
    std::function<bool(llvm::CallGraphNode*)> dfs = [&](llvm::CallGraphNode* n) {
        if (!n) return false;
        if (!visited.insert(n).second) return false;
        for (auto& callRecord : *n) {
            auto* callee = callRecord.second;
            if (callee == node) return true; // cycle detected
            if (dfs(callee)) return true;
        }
        return false;
    };
    return dfs(node);

}

std::optional<FunctionSummary>
AInstructionCall::summarize_complete(z3::context& z3ctx) {
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = call_inst->getCalledFunction();

    FunctionSummarizer fs(called_func, z3ctx);
    return fs.get_summary();
}

state_list
AInstructionBranch::execute(state_ptr state) {
    return _execute(state);
}

loop_state_list
AInstructionBranch::execute(loop_state_ptr state) {
    return _execute(state);
}

rec_state_list
AInstructionBranch::execute(rec_state_ptr state) {
    return _execute(state);
}

template<typename state_ty>
state_list_base<state_ty>
AInstructionBranch::_execute(std::shared_ptr<state_ty> state) {
    auto branch_inst = dyn_cast<llvm::BranchInst>(inst);
    auto new_trace = state->trace;
    new_trace.push_back(branch_inst->getParent());

    std::vector<state_ptr_base<state_ty>> new_states;
    if (branch_inst->isConditional()) {
        auto cond = branch_inst->getCondition();
        auto cond_value = state->evaluate(cond);

        auto true_block = branch_inst->getSuccessor(0);
        auto true_pc = AInstruction::create(&*true_block->instructionsWithoutDebug().begin());
        auto true_state = std::make_shared<state_ty>(*state);
        true_state->trace = new_trace;
        true_state->status = State::TESTING;
        // true_state->path_condition = state->path_condition && cond_value;
        true_state->append_path_condition(cond_value);
        true_state->step_pc(true_pc);

        auto false_block = branch_inst->getSuccessor(1);
        auto false_pc = AInstruction::create(&*false_block->instructionsWithoutDebug().begin());
        auto false_state = std::make_shared<state_ty>(*state);
        false_state->trace = new_trace;
        false_state->status = State::TESTING;
        // false_state->path_condition = state->path_condition && !cond_value;
        false_state->append_path_condition(!cond_value);
        false_state->step_pc(false_pc);

        new_states.push_back(true_state);
        new_states.push_back(false_state);
    } else {
        // unconditional branch
        auto next_pc = AInstruction::create(&*branch_inst->getSuccessor(0)->instructionsWithoutDebug().begin());
        auto new_state = std::make_shared<state_ty>(*state);
        new_state->trace = new_trace;
        new_state->step_pc(next_pc);
        new_states.push_back(new_state);
    }
    return new_states;
}

std::vector<state_ptr>
AInstructionReturn::execute(state_ptr state) {
    if (state->memory.stack_size() == 1) {
        // no function call, just terminate
        state->status = State::TERMINATED;
        return {state};
    }
    // llvm::errs() << state->memory.to_string() << "\n";
    auto ret = dyn_cast<llvm::ReturnInst>(inst);

    state_ptr new_state = std::make_shared<State>(*state);
    auto frame = new_state->pop_frame();

    auto ret_value = ret->getReturnValue();
    new_state->step_pc(frame.prev_pc);
    auto call_inst = dyn_cast<llvm::CallInst>(new_state->pc->inst);
    if (ret_value) {
        auto ret_expr = state->evaluate(ret_value);

        // go back to the called site
        assert(call_inst);
        new_state->write(call_inst, ret_expr);
        cache_func_value(state, ret_expr);
    }
    new_state->step_pc();
    return {new_state};
}

void
AInstructionReturn::cache_func_value(state_ptr state, z3::expr result) {
    auto cache_instance = Cache::get_instance();
    auto& top_frame = state->top_frame();
    param_list_ty args;

    auto model = state->get_model();
    auto concrete_result = model.eval(result, false);
    if (!state->is_concrete(result, concrete_result)) return;

    for (auto& param : top_frame.func->args()) {
        auto arg_value = state->evaluate(&param);
        auto concrete_value = model.eval(arg_value, true);
        if (!state->is_concrete(arg_value, concrete_value)) return;
        args.push_back(concrete_value.as_int64());
    }
    cache_instance->cache_func_value(top_frame.func, args, concrete_result.as_int64());
}

std::vector<state_ptr>
AInstructionZExt::execute(state_ptr state) {
    auto zext_inst = dyn_cast<llvm::ZExtInst>(inst);
    auto op = zext_inst->getOperand(0);
    auto op_value = state->evaluate(op);
    auto& z3ctx = op_value.ctx();
    z3::expr result(z3ctx);

    state_ptr new_state = std::make_shared<State>(*state);
    new_state->write(inst, op_value);
    new_state->step_pc();
    return {new_state};
}

std::vector<state_ptr>
AInstructionSExt::execute(state_ptr state) {
    auto sext_inst = dyn_cast_or_null<llvm::SExtInst>(inst);
    auto op = sext_inst->getOperand(0);
    auto op_value = state->evaluate(op);
    auto& z3ctx = op_value.ctx();
    z3::expr result(z3ctx);

    state_ptr new_state = std::make_shared<State>(*state);
    new_state->write(inst, op_value);
    new_state->step_pc();
    return {new_state};
}

std::set<llvm::Loop*> AInstructionPhi::failed_loops;

std::vector<state_ptr>
AInstructionPhi::execute(state_ptr state) {
    auto phi_inst = dyn_cast<llvm::PHINode>(inst);
    auto manager = AnalysisManager::get_instance();
    auto& LI = manager->get_LI(phi_inst->getFunction());
    auto loop = LI.getLoopFor(phi_inst->getParent());

    if (loop && failed_loops.find(loop) == failed_loops.end()) {
        if (!state->is_summarizing()) {
            auto accelarated_states = execute_if_summarizable(state);
            if (accelarated_states.size() > 0) {
                return accelarated_states;
            } else {
                failed_loops.insert(loop);
            }
        }
    }

    auto& z3ctx = state->z3ctx;
    auto prev_block = state->trace.back();
    llvm::Value* selected_value = phi_inst->getIncomingValueForBlock(prev_block);
    auto selected_value_expr = state->evaluate(selected_value);

    state_ptr new_state = std::make_shared<State>(*state);
    new_state->write(inst, selected_value_expr);
    new_state->step_pc();

    return {new_state};
}

std::vector<state_ptr>
AInstructionPhi::execute_if_summarizable(state_ptr state) {
    auto phi_inst = dyn_cast<llvm::PHINode>(inst);
    auto manager = AnalysisManager::get_instance();
    auto& LI = manager->get_LI(phi_inst->getFunction());
    auto loop = LI.getLoopFor(phi_inst->getParent());
    if (loop == nullptr) return {};

    auto header = loop->getHeader();

    // try summarizing the loop only when encountering the first phi of the loop
    if (phi_inst != &*header->phis().begin()) return {};

    spdlog::info("Summarizing loop {}", loop->getHeader()->getName().str());
    auto loop_summarizer = LoopSummarizer(loop, state);
    auto summary = loop_summarizer.get_summary();
    if (!summary.has_value()) {
        spdlog::info("Cannot summarize loop {}", loop->getHeader()->getName().str());
        return {};
    }

    // assert(!summary->is_over_approximated() && "Over-approximation is not supported yet");


    auto exit_block = loop->getExitBlock();
    // only consider those loops with only one exit block
    assert(exit_block);

    state_ptr new_state = std::make_shared<State>(*state);
    auto entering_block = get_loop_entering_block(loop);
    if (summary->is_over_approximated()) {
        for (auto& inst : *header) {
            if (auto phi = llvm::dyn_cast_or_null<llvm::PHINode>(&inst)) {
                auto name = get_z3_name(phi->getName().str());
                auto phi_expr = new_state->z3ctx.int_const(name.c_str());
                auto initial_value = phi->getIncomingValueForBlock(entering_block);
                auto initial_value_expr = new_state->evaluate(initial_value);
                auto evaluated_value = summary->evaluate_expr(phi_expr);
                auto N = summary->get_N();
                if (N.has_value()) {
                    z3::expr_vector src(new_state->z3ctx);
                    z3::expr_vector dst(new_state->z3ctx);
                    src.push_back(manager->get_ind_var());
                    dst.push_back(N.value());
                    new_state->write(phi, z3::ite(*N == 0, initial_value_expr, evaluated_value.substitute(src, dst)));
                } else {
                    new_state->write(phi, evaluated_value);
                }
            }
        }
        for (auto p : summary->summary_over_approx) {
            new_state->append_path_condition(p.first == p.second);
        }
        new_state->append_path_condition(summary->get_constraints());
    } else {
        auto& z3ctx = manager->get_z3ctx();
        z3::expr_vector args(z3ctx);
        for (auto& inst : *header) {
            if (auto phi = llvm::dyn_cast_or_null<llvm::PHINode>(&inst)) {
                auto name = get_z3_name(phi->getName().str());
                auto phi_expr = z3ctx.int_const(name.c_str());
                args.push_back(phi_expr);
            } else if (auto store_inst = llvm::dyn_cast_or_null<llvm::StoreInst>(&inst)) {
                // for store instruction, we need to get the pointer operand
                auto ptr = store_inst->getPointerOperand();
                auto name = get_z3_name(ptr->getName().str());
                auto ptr_expr = z3ctx.int_const(name.c_str());
                args.push_back(ptr_expr);
            }
        }
        auto arrays_ptr = state->memory.get_arrays();
        for (auto& array_ptr : arrays_ptr) {
            args.push_back(array_ptr->get_signature());
        }
        z3::expr_vector closed_forms = summary->evaluate(args);
        // auto phi_it = header->phis().begin();
        auto modified_values = summary->get_modified_values();
        for (int i = 0; i < closed_forms.size(); i++) {
            auto modified_value = modified_values[i];
            auto N = summary->get_N();
            if (N.has_value()) {
                z3::expr_vector src(z3ctx);
                z3::expr_vector dst(z3ctx);
                src.push_back(manager->get_ind_var());
                dst.push_back(N.value());
                new_state->write(modified_value, closed_forms[i].substitute(src, dst));
            } else {
                new_state->write(modified_value, closed_forms[i]);
            }
        }
    }
    // loop summary only computes the values of phi nodes and store instructions
    // so we need to execute the instructions in the header until the terminator
    // to get closed-form solutions to other values in header, which may also be
    // used outside the loop
    auto cur_inst = header->getFirstNonPHIOrDbg();
    auto cur_state = new_state;
    cur_state->step_pc(AInstruction::create(cur_inst));
    std::queue<state_ptr> states;
    std::vector<state_ptr> res;
    states.push(cur_state);
    while (!states.empty()) {
        cur_state = states.front();
        states.pop();
        if (cur_state->pc->inst == header->getTerminator()) {
            res.push_back(cur_state);
            continue;
        }
        auto new_states = cur_state->pc->execute(cur_state);
        for (auto& new_state : new_states) states.push(new_state);
    }

    for (auto& state : res) {
        state->trace.push_back(header);
        // auto new_pc = AInstruction::create(&*exit_block->begin());
        auto exit_block_first_inst = &*exit_block->begin();
        if (exit_block_first_inst->isDebugOrPseudoInst())
            exit_block_first_inst = exit_block_first_inst->getNextNonDebugInstruction();
        auto new_pc = AInstruction::create(exit_block_first_inst);
        state->step_pc(new_pc);
    }
    return res;
}

template<typename state_ty>
state_list_base<state_ty>
AInstructionSelect::_execute(std::shared_ptr<state_ty> state) {
    auto select_inst = dyn_cast<llvm::SelectInst>(inst);
    auto cond = select_inst->getCondition();
    auto cond_value = state->evaluate(cond);

    auto true_value = select_inst->getTrueValue();
    auto true_value_expr = state->evaluate(true_value);
    auto true_state = std::make_shared<state_ty>(*state);
    true_state->write(inst, true_value_expr);
    true_state->status = State::TESTING;
    // true_state->path_condition = state->path_condition && cond_value;
    true_state->append_path_condition(cond_value);
    true_state->step_pc();

    auto false_value = select_inst->getFalseValue();
    auto false_value_expr = state->evaluate(false_value);
    auto false_state = std::make_shared<state_ty>(*state);
    false_state->write(inst, false_value_expr);
    false_state->status = State::TESTING;
    // false_state->path_condition = state->path_condition && !cond_value;
    false_state->append_path_condition(!cond_value);
    false_state->step_pc();

    return {true_state, false_state};
}

state_list
AInstructionSelect::execute(state_ptr state) {
    return _execute(state);
}

loop_state_list
AInstructionSelect::execute(loop_state_ptr state) {
    return _execute(state);
}

llvm::BasicBlock*
AInstruction::get_block() {
    return inst->getParent();
}

std::vector<state_ptr>
AInstructionLoad::execute(state_ptr state) {
    auto load_inst = dyn_cast<llvm::LoadInst>(inst);
    auto ptr = load_inst->getPointerOperand();
    auto load_value = state->evaluate(ptr);
    state_ptr new_state = std::make_shared<State>(*state);
    new_state->write(inst, load_value);
    new_state->step_pc();
    return {new_state};
    // auto ptr_value = state->evaluate(ptr);
}

std::vector<state_ptr>
AInstructionStore::execute(state_ptr state) {
    auto store_inst = dyn_cast<llvm::StoreInst>(inst);
    auto ptr = store_inst->getPointerOperand();
    auto value = store_inst->getValueOperand();
    // auto ptr_value = state->evaluate(ptr);
    auto value_expr = state->evaluate(value);

    state_ptr new_state = std::make_shared<State>(*state);
    new_state->write(ptr, value_expr);
    // new_state->write(inst, value_expr);
    new_state->step_pc();
    return {new_state};
}

std::vector<state_ptr>
AInstructionGEP::execute(state_ptr state) {
    auto gep = dyn_cast_or_null<llvm::GetElementPtrInst>(inst);
    assert(gep);
    auto new_state = std::make_shared<State>(*state);
    new_state->store_gep(gep);
    new_state->step_pc();
    return {new_state};
}