#include <spdlog/spdlog.h>
#include "LoopSummarizer.h"


namespace ari_exe {
    LoopExecution::~LoopExecution() {
        assert(states.empty());
    }

    LoopExecution::LoopExecution(llvm::Loop *loop, state_ptr parent_state): loop(loop), solver(AnalysisManager::get_instance()->get_z3ctx()), parent_state(parent_state), v_conditions(AnalysisManager::get_instance()->get_z3ctx()) {
        auto initial_state = build_initial_state();
        states.push(initial_state);
    }

    LoopState
    LoopExecution::run_header(loop_state_ptr state) {
        auto header = loop->getHeader();
        auto cur_state = state;
        while (cur_state->pc->inst != header->getTerminator()) {
            auto states = step(cur_state);
            assert(states.size() == 1);
            cur_state = states.front();
        }
        return *cur_state;
    }

    LoopExecution::TestResult
    LoopExecution::test(loop_state_ptr state) {
        auto manager = AnalysisManager::get_instance();
        auto& z3ctx = manager->get_z3ctx();
        z3::expr_vector assumptions(z3ctx);
        assumptions.push_back(state->get_path_condition().as_expr());
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
        auto manager = AnalysisManager::get_instance();
        auto& z3ctx = manager->get_z3ctx();
        auto memory = Memory(parent_state->memory);
        auto initial_pc = AInstruction::create(loop->getHeader()->getFirstNonPHIOrDbg());
        auto initial_state = std::make_shared<LoopState>(LoopState(z3ctx, initial_pc, nullptr, memory, z3ctx.bool_val(true), z3ctx.bool_val(true), {}, State::RUNNING));

        put_header_phis_in_initial_state(initial_state);
        symbolize_stores(initial_state);
        return initial_state;
    }

    loop_state_list
    LoopExecution::step(loop_state_ptr state) {
        auto pc = state->pc;
        loop_state_list new_states;
        // auto modified_values = state->modified_values;
        for (auto next_state : pc->execute(state)) {
            auto next_loop_state = std::make_shared<LoopState>(LoopState(*next_state));
            new_states.push_back(next_loop_state);
        }
        return new_states;
    }

    llvm::Value*
    LoopExecution::get_modified_value(state_ptr state, llvm::Instruction* inst) {
        // if the value is a store instruction, get the value operand
        if (auto store_inst = dyn_cast_or_null<llvm::StoreInst>(inst)) {
            auto ptr = store_inst->getPointerOperand();
            auto ptr_obj = state->memory.get_object(ptr);
            assert(ptr_obj->is_pointer());
            auto addr = ptr_obj->get_ptr_value();   
            return state->memory.get_object(addr)->get_llvm_value();
        }
        return nullptr;
    }

    std::pair<loop_state_list, loop_state_list>
    LoopExecution::run() {
        loop_state_list final_states;
        loop_state_list exit_states;

        spdlog::debug("Tracing loop {}", loop->getHeader()->getName().str());
        while (!states.empty()) {
            auto cur_state = states.front();
            spdlog::debug("Current state: {}", cur_state->pc->inst->getName().str());
            // llvm::errs() << *cur_state->pc->inst << "\n";
            // llvm::errs() << cur_state->memory.to_string() << "\n";
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
        auto [final_states, exit_states] = get_final_and_exit_states();
        log_states(final_states, exit_states);

        summarize_scalar(final_states, exit_states);

        if (summary.has_value()) {//  && !summary.value().is_over_approximated()) {
            summarize_array(final_states, exit_states);
        }
        spdlog::info("finish summarization");
    }

    void
    LoopSummarizer::log_states(const loop_state_list& final_states, const loop_state_list& exit_states) {
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
    }

    std::pair<loop_state_list, loop_state_list>
    LoopSummarizer::get_final_and_exit_states() {
        LoopExecution executor(loop, parent_state);
        auto execution_res = executor.run();
        auto final_states = execution_res.first;
        auto exit_states = execution_res.second;

        return {final_states, exit_states};
    }

    void
    LoopSummarizer::summarize_scalar(const loop_state_list& final_states, const loop_state_list& exit_states) {
        spdlog::info("Summarizing scalars");
        auto manager = AnalysisManager::get_instance();
        auto& z3ctx = manager->get_z3ctx();

        auto conditions_and_updates = get_conditions_and_updates(final_states);
        auto path_conds = conditions_and_updates.first;
        auto rec_eqs = conditions_and_updates.second;

        //------------------------------ Recurrence solving ------------------------
        auto initial_pairs = get_initial_values();
        rec_s.add_initial_values(initial_pairs.first, initial_pairs.second);
        rec_s.set_eqs(path_conds, rec_eqs);
        rec_s.solve();
        closed_form_ty closed = rec_s.get_res();
        //------------------------------ End of Recurrence Solving -----------------

        //---------------------------- Collect all parameters ----------------------
        bool is_approximate = false;
        std::vector<llvm::Value*> modified_values;
        z3::expr_vector params(rec_s.z3ctx);
        z3::expr_vector params_values(rec_s.z3ctx);
        for (auto& phi : loop->getHeader()->phis()) {
            modified_values.push_back(&phi);
            auto name = get_z3_name(phi.getName().str());
            auto phi_func = rec_s.z3ctx.int_const(name.c_str());
            auto it = closed.find(phi_func);
            if (it != closed.end()) {
                auto closed_form = it->second;
                params.push_back(phi_func);
                params_values.push_back(closed_form);
            } else {
                is_approximate = true;
            }
        }

        for (auto& inst: *loop->getHeader()) {
            if (auto store_inst = llvm::dyn_cast_or_null<llvm::StoreInst>(&inst)) {
                auto ptr = store_inst->getPointerOperand();
                auto ptr_obj = final_states[0]->memory.get_object(ptr);
                auto name = get_z3_name(ptr_obj->get_llvm_value()->getName().str());

                // auto written_value = store_inst->getValueOperand();
                // modified_values.push_back(written_value);
                // auto name = get_z3_name(ptr->getNa().str());
                auto store_func = rec_s.z3ctx.int_const(name.c_str());
                auto it = closed.find(store_func);
                if (it != closed.end()) {
                    auto closed_form = it->second;
                    params.push_back(store_func);
                    params_values.push_back(closed_form);
                } else {
                    is_approximate = true;
                }
            }
        }
        //---------------------------- End of Collecting Parameters ----------------

        spdlog::info("Closed-form solutions are computed successfully:");
        if (is_approximate) {
            spdlog::info("This is an over-approximation");
        } else {
            spdlog::info("This is a precise solution");
        }

        //---------------------------- compute the number of iterations ------------
        std::optional<z3::expr> N_opt = std::nullopt;
        z3::expr_vector exit_values(rec_s.z3ctx);
        decltype(closed) over_closed;
        auto loop_guard = get_loop_guard_condition(exit_states);
        auto loop_guard_vars = Logic().collect_vars(loop_guard);
        std::vector<z3::expr> modified_values_expr;
        for (auto& m_value : modified_values) {
            auto name = get_z3_name(m_value->getName().str());
            auto z3_value = rec_s.z3ctx.int_const(name.c_str());
            modified_values_expr.push_back(z3_value);
        }
        std::set<std::string> iteration_related_vars;
        std::set<std::string> modified_values_str;
        std::set<std::string> loop_guard_vars_str;
        std::set<std::string> params_str;
        for (auto& expr : modified_values_expr) modified_values_str.insert(expr.decl().name().str());
        for (auto& expr : loop_guard_vars) loop_guard_vars_str.insert(expr.decl().name().str());
        for (auto expr : params) params_str.insert(expr.decl().name().str());
        std::set_intersection(modified_values_str.begin(), modified_values_str.end(),
                              loop_guard_vars_str.begin(), loop_guard_vars_str.end(),
                              std::inserter(iteration_related_vars, iteration_related_vars.begin()));

        auto [N_constraints, N] = get_iterations_constraints(exit_states, params, params_values);
        if (std::includes(params_str.begin(), params_str.end(),
                          iteration_related_vars.begin(), iteration_related_vars.end())) {
            auto linear_logic = LinearLogic();
            z3::expr_vector N_vec(z3ctx);
            N_vec.push_back(N);
            spdlog::info("Solving for N");
            auto N_value = linear_logic.solve_vars(N_constraints, N_vec);
            if (N_value.size() == 0) {
                spdlog::info("fail to compute the number of iterations, record the constraints on it in path conditions");
                N_constraints = z3ctx.bool_val(true);
            } else {
                spdlog::info("The number of iterations is {}", N_value[0].to_string());
                N_opt = N_value[0];
            }
        }
        z3::expr_vector src(z3ctx);
        z3::expr_vector dst(z3ctx);
        if (N_opt.has_value()) {
            src.push_back(manager->get_ind_var());
            dst.push_back(N_opt.value());
        }
        for (auto& p : closed) {
            auto new_first = p.first;
            new_first = new_first.substitute(params, params_values).substitute(src, dst);
            auto new_second = p.second.substitute(params, params_values).substitute(src, dst);
            over_closed.insert_or_assign(new_first, new_second);
        }
        for (auto expr : params_values) {
            exit_values.push_back(expr.substitute(src, dst));
        }
        //---------------------------- End of computing the number of iterations ---

        if (is_approximate) {
            if (N_opt.has_value()) {
                // summary = LoopSummary(params, closed, params.ctx().bool_val(true), modified_values, N_opt);
                summary = LoopSummary(params, exit_values, params_values, over_closed, params.ctx().bool_val(true), N_opt);
            } else {
                auto [exit_condition, N] = get_exit_loop_guard(exit_states);
                summary = LoopSummary(params, exit_values, params_values, over_closed, exit_condition, N_opt);
            }
        } else {
            summary = LoopSummary(params, exit_values, params_values, N_constraints, modified_values, N_opt);
        }
    }

    std::pair<z3::expr, rec_ty>
    LoopSummarizer::get_array_base_case(const MemoryObjectPtr array) {
        auto manager = AnalysisManager::get_instance();
        auto& z3ctx = manager->get_z3ctx();
        auto sig = array->get_signature();
        z3::expr_vector args(sig.args());
        args.push_back(manager->get_ind_var()); // add loop counter
        auto rec_func = get_array_rec_func(array);

        // base case: if any argument is negative or the loop counter is non-positive
        z3::expr base_cond = manager->get_ind_var() <= 0;
        for (auto arg : sig.args()) {
            base_cond = base_cond || arg < 0;
        }

        rec_ty base_eq;
        base_eq.insert_or_assign(rec_func(args), sig);

        return {base_cond, base_eq};
    }

    z3::func_decl
    LoopSummarizer::get_array_rec_func(const MemoryObjectPtr array) {
        auto manager = AnalysisManager::get_instance();
        auto& z3ctx = manager->get_z3ctx();
        auto sig = array->get_signature();
        auto rec_name = sig.decl().name().str() + '_';

        z3::sort_vector rec_sorts(z3ctx);
        for (int i = 0; i < sig.decl().arity(); i++)
            rec_sorts.push_back(sig.decl().domain(i));
        rec_sorts.push_back(z3ctx.int_sort()); // add an extra parameter for loop counter
        return z3ctx.function(rec_name.c_str(), rec_sorts, z3ctx.int_sort());
    }

    std::pair<std::vector<z3::expr>, std::vector<rec_ty>>
    LoopSummarizer::get_array_recursive_case(loop_state_ptr state, llvm::Value* array_ptr) {
        auto manager = AnalysisManager::get_instance();
        auto& z3ctx = manager->get_z3ctx();
        auto array = state->memory.get_object_pointed_by(array_ptr);
        auto sig = array->get_signature();
        auto path_condition = state->path_condition_in_loop;
        // auto array_ptr = state->memory.get_object(array->get_llvm_value());
        auto rec_func = get_array_rec_func(array);
        z3::expr_vector go_back_src(z3ctx);
        z3::expr_vector go_back_dst(z3ctx);
        go_back_src.push_back(manager->get_ind_var());
        go_back_dst.push_back(manager->get_ind_var() - 1);

        z3::expr_vector sub_src(z3ctx);
        z3::expr_vector sub_dst(z3ctx);
        // retrieve closed-form solutions to scalar variables
        // and substitute them into the array update
        auto scalars = summary.value();
        auto update = scalars.evaluate_expr(array->get_value().as_expr()).simplify();
        auto apps_of_arr = get_app_of(update, sig.decl());
        for (auto app : apps_of_arr) {
            sub_src.push_back(app);
            auto args = app.args();
            args.push_back(manager->get_ind_var() - 1);
            sub_dst.push_back(rec_func(args));
        }

        z3::expr_vector lhs_args(sig.args());
        lhs_args.push_back(manager->get_ind_var()); // add loop counter
        std::vector<z3::expr> conditions;
        std::vector<rec_ty> eqs;
        // assume the last condition and expression represent the frame part
        auto array_conditions = array->get_value().get_conditions();
        auto array_expressions = array->get_value().get_expressions();
        for (int i = 0; i < array_conditions.size() - 1; i++) {
            auto condition = scalars.evaluate_expr(path_condition.as_expr() && array_conditions[i]).simplify();
            conditions.push_back(condition.substitute(go_back_src, go_back_dst));
            rec_ty eq;
            auto trans = summary.value().evaluate_expr(array_expressions[i]).substitute(sub_src, sub_dst).substitute(go_back_src, go_back_dst);
            eq.insert_or_assign(rec_func(lhs_args), trans);
            eqs.push_back(eq);
        }
        return {conditions, eqs};
    }

    void
    LoopSummarizer::summarize_array(const loop_state_list& final_states, const loop_state_list& exit_states) {
        spdlog::info("Summarizing arrays");
        auto arrays = parent_state->memory.get_arrays();
        auto manager = AnalysisManager::get_instance();
        auto& z3ctx = manager->get_z3ctx();
        assert(summary.has_value() && "Loop summary for scalars should be computed before summarizing arrays");
        auto scalar_summary = summary.value();

        for (int i = 0; i < arrays.size(); i++) {
            auto array = arrays[i];
            summary->add_modified_value(array->get_llvm_value());
            std::vector<z3::expr> conditions;
            std::vector<rec_ty> eqs;

            auto arbitrary_state = final_states[0];
            // auto array_ptr = arbitrary_state->memory.get_object(array->get_llvm_value());

            //------------------ get recurrence for arrays -------------------------
            // base case: if any argument is negative or the loop counter is non-positive
            auto [base_cond, base_eq] = get_array_base_case(array);
            conditions.push_back(base_cond);
            eqs.push_back(base_eq);

            for (auto state : final_states) {
                auto [case_conditions, case_eqs] = get_array_recursive_case(state, array->get_llvm_value());
                conditions.insert(conditions.end(), case_conditions.begin(), case_conditions.end());
                eqs.insert(eqs.end(), case_eqs.begin(), case_eqs.end());
            }

            // add the frame part
            auto [frame_cond, frame_eq] = get_array_frame_case(conditions, array);
            auto acc_cond = z3ctx.bool_val(false);
            conditions.push_back(frame_cond);
            eqs.push_back(frame_eq);
            //----------------------------------------------------------------------

            // solve recurrence
            assert(conditions.size() == eqs.size());
            auto closed = solve_recurrence(conditions, eqs);
            spdlog::info("Array summaries are computed successfully");

            z3::expr_vector n_src(z3ctx);
            n_src.push_back(manager->get_ind_var());
            z3::expr_vector N_dst(z3ctx);
            auto N = scalar_summary.get_N().value_or(manager->get_loop_N());
            N_dst.push_back(N);
            auto dims = array->get_sizes();
            z3::expr domain(z3ctx.bool_val(true));
            auto indices = array->get_indices();
            for (int i = 0 ; i < dims.size(); i++) {
                domain = domain && 0 <= indices[i] && indices[i] < dims[i].as_expr() && 0 <= N;
            }
            for (auto& [func, expr] : closed) {
                spdlog::info("Restricting array summaries to the domain");
                auto closed_form = restrict_to_domain(expr.substitute(n_src, N_dst), domain).simplify();
                summary->add_closed_form(array->get_signature(), closed_form);
            }
        }
    }

    closed_form_ty
    LoopSummarizer::solve_recurrence(const std::vector<z3::expr>& conditions, const std::vector<rec_ty>& rec_eqs) {
        auto manager = AnalysisManager::get_instance();
        auto& z3ctx = manager->get_z3ctx();
        rec_solver solver(z3ctx);
        solver.set_eqs(conditions, rec_eqs);
        solver.solve();
        return solver.get_res();
    }

    std::pair<z3::expr, rec_ty>
    LoopSummarizer::get_array_frame_case(std::vector<z3::expr> conditions, const MemoryObjectPtr array) {
        auto manager = AnalysisManager::get_instance();
        auto& z3ctx = manager->get_z3ctx();
        auto sig = array->get_signature();
        auto rec_func = get_array_rec_func(array);
        z3::expr_vector lhs_args(sig.args());
        lhs_args.push_back(manager->get_ind_var()); // add loop counter
        z3::expr_vector rhs_args(sig.args());
        rhs_args.push_back(manager->get_ind_var() - 1);

        // assume the last condition and expression represent the frame part
        auto acc_cond = z3ctx.bool_val(false);
        for (auto cond : conditions) acc_cond = acc_cond || cond;

        z3::expr frame_cond = !acc_cond;
        rec_ty frame_eq;
        frame_eq.insert_or_assign(rec_func(lhs_args), rec_func(rhs_args));

        return {frame_cond, frame_eq};
    }

    std::vector<llvm::StoreInst*>
    LoopSummarizer::get_array_modified_stmts() {

        auto executor = LoopExecution(loop, parent_state);
        auto initial_state = executor.build_initial_state();
        auto update_state = executor.run_header(initial_state);

        std::vector<llvm::StoreInst*> modified_stmts;
        auto header = loop->getHeader();
        for (auto& inst : *header) {
            if (auto store_inst = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
                auto ptr = store_inst->getPointerOperand();
                auto ptr_obj = update_state.memory.get_object(ptr);
                assert(ptr_obj->is_pointer());
                auto addr = ptr_obj->get_ptr_value();
                auto m_obj = update_state.memory.get_object(addr);
                if (m_obj->is_array()) {
                    modified_stmts.push_back(store_inst);
                }
            }
        }
        return modified_stmts;
    }

    initial_ty
    LoopSummarizer::get_initial_values() {
        auto scalar_and_func = get_header_phis_scalar_and_func();
        auto func = scalar_and_func.second;
        auto header = loop->getHeader();
        auto& z3ctx = func.ctx();
        z3::expr_vector values(z3ctx);
        auto prev_block = parent_state->trace.back();
        for (auto& phi : header->phis()) {
            // get the incoming value from the preheader
            auto phi_value = phi.getIncomingValueForBlock(prev_block);
            values.push_back(parent_state->evaluate(phi_value).as_expr().simplify());
        }
        auto accessible_objects = parent_state->memory.get_accessible_objects();
        for (auto obj : accessible_objects) {
            if (obj->is_scalar()) {
                auto name = get_z3_name(obj->get_llvm_value()->getName().str());
                auto z3_value = obj->read().as_expr().simplify();
                func.push_back(z3ctx.function(name.c_str(), z3ctx.int_sort(), z3ctx.int_sort())(z3ctx.int_val(0)));
                values.push_back(z3_value);
            }
        }

        z3::expr_vector func_0(func.ctx());
        for (auto f : func) { func_0.push_back(f.decl()(0)); }
        return {func_0, values};
    }

    // std::vector<llvm::Value*>
    // LoopExecution::get_scalar_modified_values() {
    //     std::vector<llvm::Value*> modified_values;
    //     auto header = loop->getHeader();
    //     for (auto& phi : header->phis()) {
    //         modified_values.push_back(&phi);
    //     }
    //     for (auto& block : loop->blocks()) {
    //         for (auto& inst : *block) {
    //             if (auto store_inst = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
    //                 auto ptr = store_inst->getPointerOperand();
    //                 if (!isa<llvm::GetElementPtrInst>(ptr)) {
    //                     modified_values.push_back(store_inst->getPointerOperand());
    //                 }
    //             }
    //         }
    //     }
    //     return modified_values;
    // }
    std::vector<MemoryObjectPtr>
    LoopExecution::get_modified_objects(loop_state_ptr initial_state) {
        std::vector<MemoryObjectPtr> modified_objects;
        auto header = loop->getHeader();
        for (auto& phi : header->phis()) {
            auto obj = initial_state->memory.get_object(&phi);
            assert(obj->is_scalar() && "Phi nodes should be scalar values");
            modified_objects.push_back(obj);
        }

        for (auto& block : loop->blocks()) {
            for (auto& inst : *block) {
                if (auto store_inst = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
                    auto ptr = store_inst->getPointerOperand();
                    auto ptr_obj = initial_state->memory.get_object_pointed_by(ptr);
                    modified_objects.push_back(ptr_obj);
                }
            }
        }
        return modified_objects;
    }

    void
    LoopExecution::put_header_phis_in_initial_state(loop_state_ptr state) {
        auto& memory = state->memory;
        auto header = loop->getHeader();
        auto manager = AnalysisManager::get_instance();
        z3::context& z3ctx = manager->get_z3ctx();
        for (auto& phi : header->phis()) {
            auto name = get_z3_name(phi.getName().str());
            auto z3_value = z3ctx.int_const(name.c_str());
            memory.put_temp(&phi, z3_value);
        }
    }

    static llvm::Value*
    get_base_value(llvm::Value* v) {
        if (llvm::isa<llvm::GlobalVariable>(v)) {
            return v;
        }
        if (auto call_inst = llvm::dyn_cast_or_null<llvm::CallInst>(v)) {
            auto called_func = call_inst->getCalledFunction();
            if (called_func->getName() == "malloc") {
                return v;
            }
        }
        if (llvm::isa<llvm::AllocaInst>(v)) {
            return v;
        }
        if (auto gep = llvm::dyn_cast_or_null<llvm::GetElementPtrInst>(v)) {
            return get_base_value(gep->getPointerOperand());
        }
        assert(false && "not supported yet");
        return nullptr;
    }

    void
    LoopExecution::symbolize_stores(loop_state_ptr state) {
        auto manager = AnalysisManager::get_instance();
        auto& z3ctx = manager->get_z3ctx();
        auto stores = get_all_stores(loop);
        for (auto store : stores) {
            auto ptr = store->getPointerOperand();
            auto base_value = get_base_value(ptr);
            auto written_obj = state->memory.get_object_pointed_by(base_value);
            assert(written_obj);
            written_obj->write(written_obj->get_signature());
        }
    }

    std::vector<llvm::StoreInst*>
    LoopExecution::get_all_stores(llvm::Loop* loop) {
        std::vector<llvm::StoreInst*> res;
        for (auto bb : loop->blocks()) {
            for (auto& inst : *bb) {
                if (auto store = llvm::dyn_cast_or_null<llvm::StoreInst>(&inst)) {
                    res.push_back(store);
                }
            }
        }
        return res;
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



    /**
     * @brief get expressions by indices
     */
    static z3::expr_vector
    get_elements_by_indices(const std::vector<z3::expr_vector>& exprs, const std::vector<int>& indices) {
        assert(exprs.size() == indices.size());
        z3::expr_vector res(exprs[0].ctx());

        for (int i = 0; i < indices.size(); i++) {
            res.push_back(exprs[i][indices[i]]);
        }
        return res;
    }


    std::pair<std::vector<z3::expr>, std::vector<rec_ty>>
    LoopSummarizer::get_conditions_and_updates(const loop_state_list& final_states) {
        auto manager = AnalysisManager::get_instance();
        std::vector<z3::expr> path_conds;
        std::vector<rec_ty> rec_eqs;
        auto header = loop->getHeader();
        auto phis = get_header_phis();
        auto header_phis_scalar_and_func = get_header_phis_scalar_and_func();
        auto src = header_phis_scalar_and_func.first;
        auto dst = header_phis_scalar_and_func.second;
        for (auto state : final_states) {
            auto path_cond = state->path_condition_in_loop.as_expr().substitute(src, dst);
            auto updates = get_update(state);

            // collect all conditions and expressions by unfolding ite
            std::vector<z3::expr_vector> ite_conditions;
            std::vector<z3::expr_vector> ite_expressions;
            std::vector<int> sizes; // record the size of each update
            for (auto update : updates) {
                auto [tmp_conditions, tmp_expressions] = expr2piecewise(update);
                ite_conditions.push_back(tmp_conditions);
                ite_expressions.push_back(tmp_expressions);
                sizes.push_back(tmp_conditions.size());
            }
            for (const auto& indices : cartesian_product(sizes)) {
                z3::expr_vector tmp_conditions = get_elements_by_indices(ite_conditions, indices);
                z3::expr_vector tmp_expressions = get_elements_by_indices(ite_expressions, indices);
                path_conds.push_back(path_cond && z3::mk_and(tmp_conditions).substitute(src, dst));
                rec_ty eq;
                for (int i = 0; i < tmp_expressions.size(); i++) {
                    auto phi = phis[i];
                    auto name = get_z3_name(phi->getName().str());
                    auto phi_func = rec_s.z3ctx.function(name.c_str(), rec_s.z3ctx.int_sort(), rec_s.z3ctx.int_sort());
                    eq.insert_or_assign(phi_func(manager->get_ind_var() + 1), tmp_expressions[i].substitute(src, dst));
                }
                rec_eqs.push_back(eq);
            }
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

    std::pair<z3::expr, z3::expr>
    LoopSummarizer::get_iterations_constraints(const loop_state_list& exit_states, const z3::expr_vector& params, const z3::expr_vector& values) {
        spdlog::info("Computing the number of iterations");
        auto loop_guard = get_loop_guard_condition(exit_states);
        spdlog::info("Loop guard condition: {}", loop_guard.to_string());
        auto logic = Logic();
        auto vars_in_guard = logic.collect_vars(loop_guard);
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
        constraints = constraints && parent_state->get_path_condition().as_expr();
        constraints = constraints.substitute(params, values);

        z3::tactic qe_tactic = z3::tactic(z3ctx, "qe");
        z3::goal g(z3ctx);
        g.add(constraints);
        z3::apply_result result = qe_tactic(g);
        z3::expr_vector N_vec(z3ctx);
        N_vec.push_back(N);
        auto N_constraints = apply_result2expr(result);
        return {N_constraints, N};
    }

    std::pair<z3::expr, z3::expr>
    LoopSummarizer::get_exit_loop_guard(const loop_state_list& exit_states) {
        auto loop_guard = get_loop_guard_condition(exit_states);
        auto manager = AnalysisManager::get_instance();
        z3::expr_vector src(loop_guard.ctx());
        z3::expr_vector dst(loop_guard.ctx());
        auto n = manager->get_ind_var();
        auto N = manager->get_loop_N();
        src.push_back(n);
        dst.push_back(N);
        auto exit_loop_guard = loop_guard.substitute(src, dst);
        return {!exit_loop_guard, N};
    }

    z3::expr
    LoopSummarizer::get_loop_guard_condition(const loop_state_list& exit_states) {
        auto& z3ctx = rec_s.z3ctx;
        z3::expr guard(z3ctx);
        z3::expr acc_exit_cond = z3ctx.bool_val(false);
        for (auto state : exit_states) {
            auto path_cond = state->get_path_condition();
            acc_exit_cond = acc_exit_cond || path_cond.as_expr();
        }
        return (!acc_exit_cond).simplify();
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

    std::pair<z3::expr_vector, z3::expr_vector>
    LoopSummarizer::get_modified_values_and_functions() {
        auto manager = AnalysisManager::get_instance();
        auto& z3ctx = manager->get_z3ctx();

        z3::expr_vector modified_values(z3ctx);
        z3::expr_vector functions(z3ctx);

        auto header = loop->getHeader();
        for (auto& phi : header->phis()) {
            auto name = get_z3_name(phi.getName().str());
            auto z3_const = z3ctx.int_const(name.c_str());
            auto z3_func = z3ctx.function(name.c_str(), z3ctx.int_sort(), z3ctx.int_sort());
            modified_values.push_back(z3_const);
            functions.push_back(z3_func(manager->get_ind_var()));
        }
        for (auto& block : loop->blocks()) {
            for (auto& inst : *block) {
                if (auto store_inst = dyn_cast_or_null<llvm::StoreInst>(&inst)) {
                    auto written_value = store_inst->getValueOperand();
                    auto name = get_z3_name(written_value->getName().str());
                    auto z3_const = z3ctx.int_const(name.c_str());
                    auto z3_func = z3ctx.function(name.c_str(), z3ctx.int_sort(), z3ctx.int_sort());
                    modified_values.push_back(z3_const);
                    functions.push_back(z3_func(manager->get_ind_var()));
                }
            }
        }
        return {modified_values, functions};
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
            updates.push_back(phi_value.as_expr());
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
}