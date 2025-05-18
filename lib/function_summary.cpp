#include "function_summary.h"

using namespace ari_exe;

RecExecution::RecExecution(z3::context& z3ctx, llvm::Function* F): z3ctx(z3ctx), F(F) {
    auto initial_state = build_initial_state();
    states.push(initial_state);
}

State*
RecExecution::build_initial_state() {
    // set up the stack
    auto stack = AStack();
    stack.push_frame();
    for (auto& arg : F->args()) {
        auto arg_value = z3ctx.int_const(arg.getName().str().c_str());
        stack.insert_or_assign_value(&arg, arg_value);
    }
    auto initial_state = new State(z3ctx, AInstruction::create(&*F->begin()->begin()), nullptr, SymbolTable<z3::expr>(), stack, z3ctx.bool_val(true), {});
    return initial_state;
}

std::vector<State*>
RecExecution::step(State* state) {
    auto pc = state->pc;
    if (auto call_inst = dynamic_cast<AInstructionCall*>(pc)) {
        auto call_states = call_inst->execute_if_not_target(state, F);
        return call_states;
    }
    return pc->execute(state);
}

std::vector<State*>
RecExecution::run() {
    std::vector<State*> final_states;
    while (!states.empty()) {
        auto cur_state = states.front();
        states.pop();
        if (cur_state->status == State::TERMINATED) {
            final_states.push_back(cur_state);
            continue;
        }
        auto new_states = step(cur_state);
        for (auto& new_state : new_states) states.push(new_state);
    }
    return {};
}



function_summary::function_summary(llvm::Function* F, z3::context* _z3ctx): F(F), rec_s(*_z3ctx) {
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::PassBuilder PB = llvm::PassBuilder();
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);

    llvm::LoopInfo& LI = FAM.getResult<llvm::LoopAnalysis>(*F);
    // make sure the function is loop free
    assert(LI.empty());
}

void
function_summary::summarize() {
    RecExecution executor(rec_s.z3ctx, F);
    auto states = executor.run();
    // all states are TERMINATED and pc is ret
    std::vector<z3::expr> path_conds;
    std::vector<rec_ty> rec_eqs;
    z3::expr func = function_app_z3(F);
    for (auto state : states) {
        if (state->status != State::TERMINATED) {
            path_conds.push_back(state->path_condition);

            z3::expr one_case = state->evaluate(state->pc->inst);
            rec_ty eq = {{func, one_case}};
            rec_eqs.push_back(eq);
        }
    }
    rec_s.set_eqs(path_conds, rec_eqs);
    rec_s.solve();
    closed_form_ty closed = rec_s.get_res();
    summary = Summary(func.args(), closed.at(func));
}

std::optional<Summary>
function_summary::get_summary() {
    if (!summary.has_value()) {
        summarize();
    }
    return summary;
}

z3::expr
function_summary::function_app_z3(llvm::Function* f) {
    z3::expr_vector args(rec_s.z3ctx);
    for (auto& arg : f->args()) {
        auto arg_value = rec_s.z3ctx.int_const(arg.getName().str().c_str());
        args.push_back(arg_value);
    }
    z3::sort_vector domain(rec_s.z3ctx);
    for (int i = 0; i < f->arg_size(); i++) {
        domain.push_back(rec_s.z3ctx.int_sort());
    }
    z3::func_decl func = rec_s.z3ctx.function(f->getName().str().c_str(), domain, rec_s.z3ctx.int_sort());
    return func(args);
}