#include "FunctionSummarizer.h"

using namespace ari_exe;

RecExecution::RecExecution(z3::context& z3ctx, llvm::Function* F): z3ctx(z3ctx), F(F), solver(z3ctx) {
    auto initial_state = build_initial_state();
    states.push(initial_state);
}

RecExecution::~RecExecution() {
    assert(states.empty());
}

RecExecution::TestResult
RecExecution::test(std::shared_ptr<State> state) {
    z3::expr_vector assumptions(z3ctx);
    assumptions.push_back(state->path_condition);
    auto res = solver.check(assumptions);
    RecExecution::TestResult result;
    switch (res) {
        case z3::unsat:
            result = F_UNFEASIBLE;
            break;
        case z3::sat:
            result = F_FEASIBLE;
            break;
        case z3::unknown:
            result = F_TESTUNKNOWN;
            break;
    }
    return result;
}

std::shared_ptr<State>
RecExecution::build_initial_state() {
    // set up the stack
    auto stack = AStack();
    stack.push_frame();
    for (auto& arg : F->args()) {
        auto name = "ari_" + arg.getName().str();
        auto arg_value = z3ctx.int_const(name.c_str());
        stack.insert_or_assign_value(&arg, arg_value);
    }
    auto initial_state = std::make_shared<State>(State(z3ctx, AInstruction::create(&*F->begin()->begin()), nullptr, SymbolTable<z3::expr>(), stack, z3ctx.bool_val(true), z3ctx.bool_val(true), {}));
    return initial_state;
}

std::vector<std::shared_ptr<State>>
RecExecution::step(std::shared_ptr<State> state) {
    auto pc = state->pc;
    if (auto call_inst = dynamic_cast<AInstructionCall*>(pc)) {
        auto call_states = call_inst->execute_if_not_target(state, F);
        return call_states;
    }
    return pc->execute(state);
}

std::vector<std::shared_ptr<State>>
RecExecution::run() {
    std::vector<std::shared_ptr<State>> final_states;
    while (!states.empty()) {
        auto cur_state = states.front();
        states.pop();
        if (cur_state->status == State::TERMINATED) {
            final_states.push_back(cur_state);
            continue;
        } else if (cur_state->status == State::TESTING) {
            auto res = test(cur_state);
            if (res == F_FEASIBLE) {
                cur_state->status = State::RUNNING;
                states.push(cur_state);
            }
            continue;
        }
        auto new_states = step(cur_state);
        for (auto& new_state : new_states) states.push(new_state);
    }
    return final_states;
}

FunctionSummarizer::FunctionSummarizer(llvm::Function* F, z3::context& _z3ctx): F(F), rec_s(_z3ctx) {
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
FunctionSummarizer::summarize() {
    RecExecution executor(rec_s.z3ctx, F);
    auto final_states = executor.run();
    // all states are TERMINATED and pc is ret
    std::vector<z3::expr> path_conds;
    std::vector<rec_ty> rec_eqs;
    z3::expr func = function_app_z3(F);
    for (auto state : final_states) {
        path_conds.push_back(state->path_condition);
        auto ret_inst = dyn_cast_or_null<llvm::ReturnInst>(state->pc->inst);
        assert(ret_inst);
        z3::expr one_case = state->evaluate(ret_inst->getReturnValue());
        rec_ty eq = {{func, one_case}};
        rec_eqs.push_back(eq);
    }
    rec_s.set_eqs(path_conds, rec_eqs);
    rec_s.solve();
    closed_form_ty closed = rec_s.get_res();
    // find the function's closed form.
    // check if func has the same name as some key in closed
    auto it = std::find_if(closed.begin(), closed.end(), [&](const auto& pair) {
        return pair.first.decl().name().str() == func.decl().name().str();
    });
    assert(it != closed.end());
    summary = FunctionSummary(func.args(), it->second);
}

std::optional<FunctionSummary>
FunctionSummarizer::get_summary() {
    if (!summary.has_value()) {
        summarize();
    }
    return summary;
}

z3::expr
FunctionSummarizer::function_app_z3(llvm::Function* f) {
    z3::expr_vector args(rec_s.z3ctx);
    for (auto& arg : f->args()) {
        auto name = "ari_" + arg.getName().str();
        auto arg_value = rec_s.z3ctx.int_const(name.c_str());
        args.push_back(arg_value);
    }
    z3::sort_vector domain(rec_s.z3ctx);
    for (int i = 0; i < f->arg_size(); i++) {
        domain.push_back(rec_s.z3ctx.int_sort());
    }
    auto func_name = "ari_" + f->getName().str();
    z3::func_decl func = rec_s.z3ctx.function(func_name.c_str(), domain, rec_s.z3ctx.int_sort());
    return func(args);
}