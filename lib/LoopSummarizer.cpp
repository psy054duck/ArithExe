#include <spdlog/spdlog.h>
#include "LoopSummarizer.h"


using namespace ari_exe;

LoopExecution::~LoopExecution() {
    assert(states.empty());
}

LoopExecution::LoopExecution(llvm::Loop *loop, state_ptr parent_state): loop(loop), solver(AnalysisManager::get_instance()->get_z3ctx()), parent_state(parent_state), v_conditions(AnalysisManager::get_instance()->get_z3ctx()) {
    auto initial_state = build_initial_state();
    states.push(initial_state);
}

LoopExecution::TestResult
LoopExecution::test(loop_state_ptr state) {
    auto manager = AnalysisManager::get_instance();
    auto& z3ctx = manager->get_z3ctx();
    z3::expr_vector assumptions(z3ctx);
    assumptions.push_back(state->get_path_condition());
    auto res = solver.check(assumptions);
    LoopExecution::TestResult result;
    switch (res) {
        case z3::unsat:
            result = L_UNFEASIBLE;
            break;
        case z3::sat:
            result = L_FEASIBLE;
            break;
        case z3::unknown:
            result = L_TESTUNKNOWN;
            break;
    }
    return result;
}

loop_state_ptr
LoopExecution::build_initial_state() {
    // set up the stack
    auto stack = AStack();
    stack.push_frame();
    auto header = loop->getHeader();
    auto manager = AnalysisManager::get_instance();
    auto& z3ctx = manager->get_z3ctx();
    auto prev_block = parent_state->trace.back();
    for (auto& phi : header->phis()) {
        auto name = get_z3_name(phi.getName().str());
        auto phi_value = z3ctx.int_const(name.c_str());
        // auto phi_initial = phi.getIncomingValueForBlock(prev_block);
        // auto phi_value = parent_state->evaluate(phi_initial);
        stack.insert_or_assign_value(&phi, phi_value);
    }
    auto initial_pc = AInstruction::create(loop->getHeader()->getFirstNonPHI());
    auto initial_state = std::make_shared<LoopState>(LoopState(z3ctx, initial_pc, nullptr, SymbolTable<z3::expr>(), stack, z3ctx.bool_val(true), z3ctx.bool_val(true), {}, State::RUNNING));
    return initial_state;
}

loop_state_list
LoopExecution::step(loop_state_ptr state) {
    auto pc = state->pc;
    loop_state_list new_states;
    for (auto next_state : pc->execute(state)) {
        auto next_loop_state = std::make_shared<LoopState>(LoopState(*next_state));
        new_states.push_back(next_loop_state);
    }

    return new_states;
}

std::pair<loop_state_list, loop_state_list>
LoopExecution::run() {
    loop_state_list final_states;
    loop_state_list exit_states;

    spdlog::debug("Tracing loop {}", loop->getHeader()->getName().str());
    while (!states.empty()) {
        auto cur_state = states.front();
        spdlog::debug("Current state: {}", cur_state->pc->inst->getName().str());
        states.pop();
        if (back_edge_taken(cur_state)) {
            continue;
        }
        if (is_final_state(cur_state)) {
            final_states.push_back(cur_state);
            continue;
        }
        if (is_exit_state(cur_state)) {
            exit_states.push_back(cur_state);
            continue;
        }
        if (cur_state->status == State::TERMINATED) {
            final_states.push_back(cur_state);
            continue;
        } else if (cur_state->status == State::TESTING) {
            auto res = test(cur_state);
            if (res == L_FEASIBLE) {
                cur_state->status = State::RUNNING;
                states.push(cur_state);
            }
            continue;
        } else if (cur_state->status == State::VERIFYING) {
            cur_state->append_path_condition(cur_state->verification_condition);
            auto res = test(cur_state);
            states.push(cur_state);
            if (res == L_UNFEASIBLE) {
                spdlog::error("The annotated loop invariant is not valid");
            }
            cur_state->status = State::RUNNING;
        }
        auto new_states = step(cur_state);
        for (auto& new_state : new_states) states.push(new_state);
    }
    spdlog::debug("Tracing finished");
    return {final_states, exit_states};
}

bool
LoopExecution::back_edge_taken(loop_state_ptr state) {
    auto pc = state->pc;
    auto prev_pc = state->prev_pc;
    if (prev_pc == nullptr) return false;
    auto cur_block = pc->inst->getParent();
    auto prev_block = prev_pc->inst->getParent();
    
    return loop->getHeader() == cur_block && loop->getLoopLatch() == prev_block;
}

void
LoopSummarizer::summarize() {
    LoopExecution executor(loop, parent_state);
    auto execution_res = executor.run();
    auto final_states = execution_res.first;
    auto exit_states = execution_res.second;
    auto manager = AnalysisManager::get_instance();
    auto& z3ctx = manager->get_z3ctx();
    spdlog::info("For loop {}, there are {} Final states", loop->getName(), final_states.size());
    spdlog::info("For loop {}, there are {} exit states", loop->getName(), exit_states.size());
    for (int i = 0; i < final_states.size(); i++) {
        auto state = final_states[i];
        std::string path = state->trace[0]->getName().str();
        for (int j = 1; j < state->trace.size(); j++) {
            path += " -> " + state->trace[j]->getName().str();
        }
        path += " -> " + state->pc->get_block()->getName().str();
        spdlog::info("{}th Final state path: {}", i + 1, path);
    }

    for (int i = 0; i < exit_states.size(); i++) {
        auto state = exit_states[i];
        std::string path = state->trace[0]->getName().str();
        for (int j = 1; j < state->trace.size(); j++) {
            path += " -> " + state->trace[j]->getName().str();
        }
        path += " -> " + state->pc->get_block()->getName().str();
        spdlog::info("{}th exit state path: {}", i + 1, path);
    }

    auto conditions_and_updates = get_conditions_and_updates(final_states);
    auto path_conds = conditions_and_updates.first;
    auto rec_eqs = conditions_and_updates.second;

    auto initial_pairs = get_initial_values();
    rec_s.add_initial_values(initial_pairs.first, initial_pairs.second);
    rec_s.set_eqs(path_conds, rec_eqs);
    rec_s.solve();
    closed_form_ty closed = rec_s.get_res();
    // TODO: assume the summary is exact for now
    z3::expr_vector params(rec_s.z3ctx);
    z3::expr_vector params_values(rec_s.z3ctx);
    for (auto& phi : loop->getHeader()->phis()) {
        auto name = get_z3_name(phi.getName().str());
        auto phi_func = rec_s.z3ctx.int_const(name.c_str());
        params.push_back(phi_func);
        auto it = closed.find(phi_func);
        if (it != closed.end()) {
            auto closed_form = it->second;
            params_values.push_back(closed_form);
        } else {
            spdlog::error("Cannot find closed form for phi {}", phi.getName().str());
            assert(false);
        }
    }
    spdlog::info("Closed-form solutions are computed successfully:");
    spdlog::info("Computing the number of iterations");
    auto N = get_iterations(exit_states, params, params_values);
    if (N.has_value()) {
        spdlog::info("The number of iterations is {}", N.value().simplify().to_string());
    } else {
        spdlog::info("Cannot find the number of iterations");
        return;
    }
    auto N_value = N.value();
    z3::expr_vector exit_values(rec_s.z3ctx);
    z3::expr_vector src(z3ctx);
    z3::expr_vector dst(z3ctx);
    src.push_back(manager->get_ind_var());
    dst.push_back(N_value);
    for (auto expr : params_values) {
        exit_values.push_back(expr.substitute(src, dst));
    }
    summary = LoopSummary(params, exit_values);
}

initial_ty
LoopSummarizer::get_initial_values() {
    auto scalar_and_func = get_header_phis_scalar_and_func();
    auto func = scalar_and_func.second;
    auto header = loop->getHeader();
    z3::expr_vector values(func.ctx());
    auto prev_block = parent_state->trace.back();
    for (auto& phi : header->phis()) {
        // get the incoming value from the preheader
        auto phi_value = phi.getIncomingValueForBlock(prev_block);
        values.push_back(parent_state->evaluate(phi_value).simplify());
    }
    z3::expr_vector func_0(func.ctx());
    for (auto f : func) { func_0.push_back(f.decl()(0)); }
    return {func_0, values};
}

llvm::BasicBlock*
LoopSummarizer::get_predecessor() {
    auto header = loop->getHeader();
    for (auto pred : llvm::predecessors(header)) {
        if (loop->contains(pred)) {
            continue;
        }
        return pred;
    }
    return nullptr;
}

std::pair<std::vector<z3::expr>, std::vector<rec_ty>>
LoopSummarizer::get_conditions_and_updates(const loop_state_list& final_states) {
    auto manager = AnalysisManager::get_instance();
    std::vector<z3::expr> path_conds;
    std::vector<rec_ty> rec_eqs;
    // z3::expr func = function_app_z3(F);
    auto header = loop->getHeader();
    auto phis = get_header_phis();
    auto header_phis_scalar_and_func = get_header_phis_scalar_and_func();
    auto src = header_phis_scalar_and_func.first;
    auto dst = header_phis_scalar_and_func.second;
    for (auto state : final_states) {
        path_conds.push_back(state->path_condition_in_loop.substitute(src, dst));
        auto updates = get_update(state);
        rec_ty eq;
        for (int i = 0; i < updates.size(); i++) {
            auto phi = phis[i];
            auto name = get_z3_name(phi->getName().str());
            auto phi_func = rec_s.z3ctx.function(name.c_str(), rec_s.z3ctx.int_sort(), rec_s.z3ctx.int_sort());
            auto phi_value = updates[i];
            eq.insert_or_assign(phi_func(manager->get_ind_var() + 1), phi_value.substitute(src, dst));
        }
        rec_eqs.push_back(eq);
    }
    return {path_conds, rec_eqs};
}

static z3::expr
apply_result2expr(z3::apply_result result) {
    z3::expr res = result.ctx().bool_val(false);
    for (int i = 0; i < result.size(); i++) {
        auto r = result[i].as_expr();
        res = res || r;
    }
    return res.simplify();
}

std::optional<z3::expr>
LoopSummarizer::get_iterations(const loop_state_list& exit_states, const z3::expr_vector& params, const z3::expr_vector& values) {
    auto loop_guard = get_loop_guard_condition(exit_states);
    spdlog::info("Loop guard condition: {}", loop_guard.to_string());
    auto loop_guard_closed_form = loop_guard.substitute(params, values);
    spdlog::info("Loop guard condition (closed-form): {}", loop_guard_closed_form.to_string());
    auto manager = AnalysisManager::get_instance();
    auto& z3ctx = manager->get_z3ctx();
    z3::expr N = manager->get_loop_N();
    z3::expr n = manager->get_ind_var();
    z3::expr constraints = z3::forall(n, z3::implies(0 <= n && n < N, loop_guard_closed_form.substitute(params, values)));
    z3::expr_vector src(z3ctx);
    z3::expr_vector dst(z3ctx);
    src.push_back(n);
    dst.push_back(N);
    constraints = constraints && !loop_guard_closed_form.substitute(src, dst);
    constraints = constraints && N >= 0;
    spdlog::info("constraints for N: {}", constraints.to_string());
    constraints = constraints.substitute(params, values);

    z3::tactic qe_tactic = z3::tactic(z3ctx, "qe");
    z3::goal g(z3ctx);
    g.add(constraints);
    z3::apply_result result = qe_tactic(g);
    auto linear_logic = LinearLogic();
    spdlog::info("Solving for N");
    z3::expr_vector N_vec(z3ctx);
    N_vec.push_back(N);
    auto N_value = linear_logic.solve_vars(apply_result2expr(result), N_vec);
    if (N_value.size() == 0) {
        spdlog::debug("Cannot find the number of iterations");
        return std::nullopt;
    }
    return N_value[0];
}

z3::expr
LoopSummarizer::get_loop_guard_condition(const loop_state_list& exit_states) {
    auto& z3ctx = rec_s.z3ctx;
    z3::expr guard(z3ctx);
    z3::expr acc_exit_cond = z3ctx.bool_val(false);
    for (auto state : exit_states) {
        auto path_cond = state->get_path_condition();
        acc_exit_cond = acc_exit_cond || path_cond;
    }
    return !acc_exit_cond;
}

std::vector<llvm::PHINode*>
LoopSummarizer::get_header_phis() {
    std::vector<llvm::PHINode*> phis;
    auto header = loop->getHeader();
    for (auto& phi : header->phis()) {
        phis.push_back(&phi);
    }
    return phis;
}

std::optional<LoopSummary>
LoopSummarizer::get_summary() {
    if (!summary.has_value()) {
        summarize();
    }
    return summary;
}

std::pair<z3::expr_vector, z3::expr_vector>
LoopSummarizer::get_header_phis_scalar_and_func() {
    auto header = loop->getHeader();
    auto manager = AnalysisManager::get_instance();
    auto& z3ctx = manager->get_z3ctx();

    z3::expr_vector src(z3ctx);
    z3::expr_vector dst(z3ctx);
    for(auto& phi : header->phis()) {
        auto name = get_z3_name(phi.getName().str());
        auto phi_src = z3ctx.int_const(name.c_str());
        auto phi_dst = z3ctx.function(name.c_str(), z3ctx.int_sort(), z3ctx.int_sort());
        src.push_back(phi_src);
        dst.push_back(phi_dst(manager->get_ind_var()));
    }
    return {src, dst};
}

std::vector<z3::expr>
LoopSummarizer::get_update(loop_state_ptr state) {
    std::vector<z3::expr> updates;
    auto header = loop->getHeader();
    auto latch = state->pc->get_block();
    assert(loop->isLoopLatch(latch));
    auto manager = AnalysisManager::get_instance();
    auto& z3ctx = manager->get_z3ctx();

    for (auto& phi : header->phis()) {
        auto phi_final_value = phi.getIncomingValueForBlock(latch);
        auto phi_value = state->evaluate(phi_final_value);
        updates.push_back(phi_value);
    }
    return updates;
}

bool
LoopExecution::is_exit_state(loop_state_ptr state) {
    auto pc = state->pc;
    auto cur_block = pc->get_block();
    llvm::SmallVector<llvm::BasicBlock*> exits;
    loop->getExitBlocks(exits);
    auto found = std::find_if(exits.begin(), exits.end(), [&](llvm::BasicBlock* exit) {
        return cur_block == exit;
    });
    return found != exits.end();
}

bool
LoopExecution::is_final_state(loop_state_ptr state) {
    auto pc = state->pc;
    auto cur_block = pc->get_block();
    if (!loop->isLoopLatch(cur_block)) return false;
    if (cur_block->getTerminator() != pc->inst) return false;
    return true;
}