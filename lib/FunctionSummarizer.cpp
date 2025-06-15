#include "FunctionSummarizer.h"
#include <spdlog/spdlog.h>

using namespace ari_exe;

RecExecution::RecExecution(z3::context& z3ctx, llvm::Function* F): z3ctx(z3ctx), F(F), solver(z3ctx) {
    auto initial_state = build_initial_state();
    states.push(initial_state);
}

RecExecution::~RecExecution() {
    assert(states.empty());
}

RecExecution::TestResult
RecExecution::test(state_ptr state) {
    z3::expr_vector assumptions(z3ctx);
    assumptions.push_back(state->get_path_condition());
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

rec_state_ptr
RecExecution::build_initial_state() {
    // set up the stack
    Memory memory;
    memory.push_frame(F); 
    for (auto& arg : F->args()) {
        auto name = "ari_" + arg.getName().str();
        auto arg_value = z3ctx.int_const(name.c_str());
        memory.write(&arg, arg_value);
    }
    auto pc = F->getEntryBlock().getFirstNonPHIOrDbg();
    auto initial_state = std::make_shared<RecState>(State(z3ctx, AInstruction::create(pc), nullptr, memory, z3ctx.bool_val(true), {}));
    return initial_state;
}

rec_state_list
RecExecution::step(state_ptr state, bool unfold) {
    auto pc = state->pc;
    state_list next_states;
    auto call_inst = dynamic_cast<AInstructionCall*>(pc);
    if (unfold && call_inst) {
        next_states = call_inst->execute_if_not_target(state, F);
    } else if (call_inst) {
        next_states = call_inst->execute_naively(state);
        auto llvm_call_inst = dyn_cast_or_null<llvm::CallInst>(call_inst->inst);
        auto func = llvm_call_inst->getCalledFunction();
        next_states = call_inst->execute_naively(state);
    } else {
        next_states = pc->execute(state);
    }

    rec_state_list next_rec_states;
    for (auto next_state : next_states) {
        auto next_rec_state = std::make_shared<RecState>(RecState(*next_state));
        next_rec_states.push_back(next_rec_state);
    }
    return next_rec_states;
}

rec_state_list
RecExecution::run(bool unfold) {
    rec_state_list final_states;
    spdlog::info("Collecting all paths");
    while (!states.empty()) {
        auto cur_state = states.front();
        states.pop();
        spdlog::debug("Current Instruction: {}", cur_state->pc->inst->getName().str());
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
        auto new_states = step(cur_state, unfold);
        for (auto new_state : new_states) states.push(new_state);
    }
    return final_states;
}

FunctionSummarizer::FunctionSummarizer(llvm::Function* F, z3::context& _z3ctx): F(F), z3ctx(_z3ctx) {
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
    spdlog::info("Summarizing function: {}", F->getName().str());
    RecExecution executor(z3ctx, F);
    // auto final_states = executor.run(unfold);
    // all states are TERMINATED and pc is ret
    std::vector<z3::expr> path_conds;
    z3::expr func = function_app_z3(F);
    auto rec_s = prepare_rec_solver();
    if (rec_s.solve()) {
        spdlog::info("Recurrence solved successfully");
    } else {
        spdlog::info("Failed to solve recurrence for function: {}", F->getName().str());
        return;
    }
    closed_form_ty closed = rec_s.get_res();
    // find the function's closed form.
    // check if func has the same name as some key in closed
    auto it = std::find_if(closed.begin(), closed.end(), [&](const auto& pair) {
        return pair.first.decl().name().str() == func.decl().name().str();
    });
    assert(it != closed.end());
    summary = FunctionSummary(func.args(), it->second);
}

rec_solver
FunctionSummarizer::prepare_rec_solver() {
    auto naive_solver = prepare_rec_solver_naive();
    if (naive_solver.has_value()) {
        return naive_solver.value();
    }
    return prepare_rec_solver_unfold();
}

// Check if this function application is of form f(n), where n is a variable.
// In other words, the index variable is a single variable, not an expression.
static bool
is_simple_app(z3::expr e) {
    if (e.num_args() != 1) return false; // must have exactly one argument
    auto arg = e.arg(0);
    return arg.is_const();
}

std::optional<rec_solver>
FunctionSummarizer::prepare_rec_solver_naive() {
    rec_solver rec_s(z3ctx);
    auto manager = AnalysisManager::get_instance();
    auto CG = manager->get_CG();
    auto SCC = get_SCC_for(CG, F);
    std::vector<std::vector<z3::expr>> path_conds;
    std::vector<std::vector<rec_ty>> rec_eqs;
    std::vector<int> sizes;
    for (auto func : SCC) {
        RecExecution executor(z3ctx, func);
        auto final_states = executor.run(false);
        // all states are TERMINATED and pc is ret
        auto [cur_path_conds, cur_rec_eqs] = parse_rec(final_states, func);
        path_conds.push_back(cur_path_conds);
        rec_eqs.push_back(cur_rec_eqs);
        sizes.push_back(cur_path_conds.size());
    }

    std::vector<z3::expr> combined_conds;
    std::vector<rec_ty> combined_eqs;
    for (auto indices : cartesian_product(sizes)) {
        z3::expr_vector conds(z3ctx);
        z3::expr_vector ori_conds(z3ctx);
        z3::expr_vector ori_args(z3ctx);
        rec_ty rec_eq;
        z3::expr_vector keys(z3ctx);
        for (int i = 0; i < indices.size(); i++) {
            auto idx = indices[i];
            assert(rec_eqs[i][idx].size() == 1);
            auto& [key, value] = *rec_eqs[i][idx].begin();
            auto args = key.args();
            ori_args = args;
            if (key.args().size() != 1) return std::nullopt;

            auto args_plus_one = z3::expr_vector(z3ctx);
            for (auto arg : args) args_plus_one.push_back(arg + z3ctx.int_val(1));
            auto shifted_key = key;
            shifted_key = shifted_key.substitute(args, args_plus_one).simplify();
            auto shifted_value = value.substitute(args, args_plus_one).simplify();
            auto all_apps = get_func_apps(shifted_value);
            bool is_all_simple = std::all_of(all_apps.begin(), all_apps.end(), [&](z3::expr e) {
                return is_simple_app(e);
            });
            if (!is_all_simple) {
                return std::nullopt;
            }
            keys.push_back(shifted_key);
            rec_eq.insert_or_assign(shifted_key, shifted_value);
            conds.push_back(path_conds[i][idx].substitute(args, args_plus_one).simplify());
            ori_conds.push_back(path_conds[i][idx].simplify());
        }
        auto cur_cond = z3::mk_and(conds);
        if (is_base_case(keys, z3::mk_and(ori_conds))) {
            z3::expr_vector initial_keys(z3ctx);
            z3::expr_vector initial_values(z3ctx);
            for (auto key : keys) {
                auto shifted_value = rec_eq.at(key);
                auto args_zero = z3::expr_vector(z3ctx);
                auto args_one = z3::expr_vector(z3ctx);
                for (auto arg : ori_args) {
                    args_zero.push_back(z3ctx.int_val(-1));
                    args_one.push_back(z3ctx.int_val(-1));
                }
                auto key_zero = key;
                key_zero = key_zero.substitute(ori_args, args_zero).simplify();
                initial_keys.push_back(key_zero);
                initial_values.push_back(shifted_value.substitute(ori_args, args_one).simplify());
            }
            rec_s.add_initial_values(initial_keys, initial_values);
        } else {
            combined_conds.push_back(cur_cond);
            combined_eqs.push_back(rec_eq);
        }
    }
    rec_s.set_eqs(combined_conds, combined_eqs);
    return rec_s;
}

bool
FunctionSummarizer::is_base_case(const z3::expr_vector& keys, z3::expr cond) {
    auto solver = z3::solver(z3ctx);
    solver.add(cond);
    for (auto f : keys) {
        solver.push();
        for (auto arg : f.args()) {
            solver.add(arg != z3ctx.int_val(1));
        }
        if (solver.check() == z3::sat) return false;
        solver.pop();
    }
    return true;
}

rec_solver
FunctionSummarizer::prepare_rec_solver_unfold() {
    rec_solver rec_s(z3ctx);
    RecExecution executor(rec_s.z3ctx, F);
    auto final_states = executor.run(true);
    // all states are TERMINATED and pc is ret
    auto [path_conds, rec_eqs] = parse_rec(final_states, F);
    rec_s.set_eqs(path_conds, rec_eqs);
    return rec_s;
}

std::pair<std::vector<z3::expr>, std::vector<rec_ty>>
FunctionSummarizer::parse_rec(const rec_state_list& final_states, llvm::Function* f) {
    std::vector<z3::expr> path_conds;
    std::vector<rec_ty> rec_eqs;
    z3::expr func = function_app_z3(f);
    for (auto state : final_states) {
        path_conds.push_back(state->get_path_condition());
        auto ret_inst = dyn_cast_or_null<llvm::ReturnInst>(state->pc->inst);
        assert(ret_inst);
        z3::expr one_case = state->evaluate(ret_inst->getReturnValue());
        rec_ty eq = {{func, one_case}};
        rec_eqs.push_back(eq);
    }
    return {path_conds, rec_eqs};
}

std::optional<FunctionSummary>
FunctionSummarizer::get_summary() {
    try {
        if (!summary.has_value()) {
            summarize();
        }
    } catch (...) {
        spdlog::info("Failed to summarize function: {}", F->getName().str());
    }
    return summary;
}

z3::expr
FunctionSummarizer::function_app_z3(llvm::Function* f) {
    z3::expr_vector args(z3ctx);
    for (auto& arg : f->args()) {
        auto name = "ari_" + arg.getName().str();
        auto arg_value = z3ctx.int_const(name.c_str());
        args.push_back(arg_value);
    }
    z3::sort_vector domain(z3ctx);
    for (int i = 0; i < f->arg_size(); i++) {
        domain.push_back(z3ctx.int_sort());
    }
    auto func_name = "ari_" + f->getName().str();
    z3::func_decl func = z3ctx.function(func_name.c_str(), domain, z3ctx.int_sort());
    return func(args);
}
