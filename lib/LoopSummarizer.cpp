#include <spdlog/spdlog.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <exception>
#include "LoopSummarizer.h"
#include "llvm/Analysis/ValueTracking.h"
#include "EntryArray.h"
#include "ArrayGuard.h"
#include "ArrayShape.h"
#include "VerificationSession.h"


namespace ari_exe {
    static bool contains_assumption(llvm::Loop* loop) {
        std::vector<llvm::Function*> pending;
        std::set<llvm::Function*> visited;
        for (auto* block : loop->blocks()) {
            for (auto& instruction : *block) {
                auto* call = llvm::dyn_cast<llvm::CallInst>(&instruction);
                if (call) {
                    if (auto* callee = llvm::dyn_cast<llvm::Function>(
                            call->getCalledOperand()->stripPointerCasts())) pending.push_back(callee);
                }
            }
        }
        while (!pending.empty()) {
            auto* function = pending.back(); pending.pop_back();
            if (!visited.insert(function).second) continue;
            if (function->getName().contains("assume")) return true;
            if (function->isDeclaration()) continue;
            for (auto& instruction : llvm::instructions(function)) {
                auto* call = llvm::dyn_cast<llvm::CallInst>(&instruction);
                if (call) {
                    if (auto* callee = llvm::dyn_cast<llvm::Function>(
                            call->getCalledOperand()->stripPointerCasts())) pending.push_back(callee);
                }
            }
        }
        return false;
    }

    static z3::expr arithmetic_conjuncts(const z3::expr& domain) {
        std::map<unsigned, bool> arithmetic_cache;
        unsigned budget = 4096;
        const auto arithmetic = [&](const z3::expr& term, const auto& self) -> bool {
            auto found = arithmetic_cache.find(term.id());
            if (found != arithmetic_cache.end()) return found->second;
            if (!budget) return false;
            --budget;
            bool result = term.is_app() &&
                (term.num_args() == 0 || term.decl().decl_kind() != Z3_OP_UNINTERPRETED);
            if (result) {
                for (const auto& arg : term.args()) {
                    if (!self(arg, self)) { result = false; break; }
                }
            }
            arithmetic_cache.emplace(term.id(), result);
            return result;
        };
        z3::expr_vector conjuncts(domain.ctx());
        std::vector<z3::expr> pending{domain};
        std::set<unsigned> visited;
        while (!pending.empty() && visited.size() < 4096) {
            auto term = pending.back(); pending.pop_back();
            if (!visited.insert(term.id()).second) continue;
            if (term.is_and()) {
                for (const auto& arg : term.args()) pending.push_back(arg);
            } else if (arithmetic(term, arithmetic)) {
                conjuncts.push_back(term);
            }
        }
        return z3::mk_and(conjuncts);
    }

    static bool native_array_context(const state_ptr& parent) {
        if (!parent->session.uses_integer_relaxation()) return false;
        const auto arrays=parent->memory.get_arrays();
        return std::any_of(arrays.begin(),arrays.end(),
            [](const MemoryObject* array) { return array->has_native_vla_shape(); });
    }

    static z3::expr frozen_loop_domain(llvm::Loop* loop, const state_ptr& parent) {
        std::set<std::string> changing;
        for (auto& phi:loop->getHeader()->phis())
            changing.insert(get_z3_name(phi.getName().str()));
        for (const auto* object:parent->memory.get_accessible_objects())
            if (object->is_scalar()) changing.insert(object->get_signature().decl().name().str());
        auto domain=parent->z3ctx.bool_val(true);
        std::vector<z3::expr> pending{arithmetic_conjuncts(parent->get_path_condition().as_expr())};
        while (!pending.empty()) {
            const auto term=pending.back();pending.pop_back();
            if (term.is_and()) {
                for (const auto& arg:term.args()) pending.push_back(arg);
                continue;
            }
            bool frozen=true;
            for (const auto& variable:Logic().collect_vars(term))
                if (changing.count(variable.decl().name().str())) { frozen=false;break; }
            if (frozen) domain=domain&&term;
        }
        return domain.simplify();
    }

    static bool is_uninterpreted_loop_call(const llvm::CallInst* call) {
        const auto* function = llvm::dyn_cast<llvm::Function>(
            call->getCalledOperand()->stripPointerCasts());
        if (!function) return true;
        const auto name = function->getName();
        // These calls already have instruction-level verification semantics.
        if (name.ends_with("assert") || name.contains("assume") ||
            name.contains("reach_error") || name == "__VERIFIER_error" ||
            name == "__assert_fail") return false;
        return !function->hasExactDefinition();
    }

    LoopSummarizer::LoopSummarizer(llvm::Loop* loop, state_ptr parent_state)
        : loop(loop), rec_s(parent_state->z3ctx),
          iteration_parameter(parent_state->z3ctx.int_const(
              (get_z3_name(loop->getHeader()->getParent()->getName().str() + "_" +
                           loop->getHeader()->getName().str()) + "_exit_count_" +
               std::to_string(parent_state->session.next_call_value_id(loop->getHeader()))).c_str())),
          parent_state(parent_state) {}

    static unsigned expression_size(const z3::expr& expression) {
        unsigned size = 1;
        if (!expression.is_app()) return size;
        for (const z3::expr& argument : expression.args()) {
            size += expression_size(argument);
        }
        return size;
    }

    static void collect_boolean_atoms(const z3::expr& expression,
                                      std::vector<z3::expr>& atoms) {
        if (!expression.is_app()) return;
        const Z3_decl_kind kind = expression.decl().decl_kind();
        const bool connective =
            kind == Z3_OP_AND || kind == Z3_OP_OR || kind == Z3_OP_NOT ||
            kind == Z3_OP_IMPLIES || kind == Z3_OP_ITE;
        if (expression.is_bool() && !connective) {
            const bool already_present = std::any_of(
                atoms.begin(), atoms.end(), [&](const z3::expr& atom) {
                    return z3::eq(atom, expression);
                });
            if (!already_present) atoms.push_back(expression);
        }
        for (const z3::expr& argument : expression.args()) {
            collect_boolean_atoms(argument, atoms);
        }
    }

    static z3::expr simplify_boolean_condition(
        const z3::expr& condition, const z3::expr& assumptions) {
        z3::expr simplified = simplify(condition, assumptions);
        std::vector<z3::expr> atoms;
        collect_boolean_atoms(simplified, atoms);
        std::sort(atoms.begin(), atoms.end(),
                  [](const z3::expr& lhs, const z3::expr& rhs) {
                      return expression_size(lhs) < expression_size(rhs);
                  });

        z3::solver solver(condition.ctx());
        z3::params params(condition.ctx());
        params.set("timeout", 200u);
        solver.set(params);
        solver.add(assumptions);
        for (const z3::expr& atom : atoms) {
            solver.push();
            solver.add(simplified != atom);
            const z3::check_result same_result = solver.check();
            solver.pop();
            if (same_result == z3::unsat) return atom;

            solver.push();
            solver.add(simplified == atom);
            const z3::check_result opposite_result = solver.check();
            solver.pop();
            if (opposite_result == z3::unsat) return !atom;
        }
        return simplified;
    }

    static MemoryAddress_ty
    parse_ptr(const MemoryObjectPtr ptr, state_ptr state) {
        assert(ptr->is_pointer() && "Pointer object expected");
        auto pointed_addr = ptr->get_ptr_value();
        auto pointed_obj = state->memory.get_object(pointed_addr);
        if (!pointed_obj->is_pointer()) {
            return pointed_addr;
        }
        auto addr = parse_ptr(pointed_obj, state);
        for (auto& offset : pointed_addr.offset) {
            addr.offset.push_back(offset);
        }
        return addr;
    }
    LoopExecution::~LoopExecution() {
        assert(states.empty() || std::uncaught_exceptions()>0);
    }

    LoopExecution::LoopExecution(llvm::Loop *loop, state_ptr parent_state): loop(loop), solver(AnalysisManager::get_instance()->get_z3ctx()), parent_state(parent_state), v_conditions() {
        auto initial_state = build_initial_state();
        states.push(initial_state);
        stores = get_all_stores(loop);
    }

    std::vector<llvm::StoreInst*>
    LoopExecution::get_all_stores(llvm::Loop* loop) {
        std::vector<llvm::StoreInst*> stores;
        for (auto& block : loop->getBlocks()) {
            for (auto& inst : *block) {
                if (auto store_inst = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
                    stores.push_back(store_inst);
                }
            }
        }
        return stores;
    }

    std::vector<llvm::CallInst*>
    LoopExecution::get_unknown_calls(llvm::Loop* loop) {
        std::vector<llvm::CallInst*> calls;
        for (auto& block : loop->blocks()) {
            for (auto& inst : *block) {
                if (auto call_inst = llvm::dyn_cast_or_null<llvm::CallInst>(&inst)) {
                    if (is_uninterpreted_loop_call(call_inst)) {
                        calls.push_back(call_inst);
                    }
                }
            }
        }
        return calls;
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
        auto initial_pc = AInstruction::create(&*loop->getHeader()->getFirstNonPHIOrDbg());
        auto initial_state = std::make_shared<LoopState>(LoopState(
            parent_state->session, z3ctx, initial_pc, nullptr, memory,
            z3ctx.bool_val(true), z3ctx.bool_val(true), {},
            State::RUNNING));
        initial_state->summarizing_loop = loop;
        const bool native_context=native_array_context(parent_state);

        put_header_phis_in_initial_state(initial_state);
        for (auto& phi:loop->getHeader()->phis()) {
            if (!native_context ||
                !phi.getType()->isIntegerTy() || phi.getType()->isIntegerTy(1)) continue;
            auto* predecessor=parent_state->trace.back();
            const auto initial=parent_state->evaluate(phi.getIncomingValueForBlock(predecessor));
            std::set<llvm::Value*> seen{&phi};
            std::function<bool(llvm::Value*)> unchanged=[&](llvm::Value* incoming) {
                if (auto* relay=llvm::dyn_cast<llvm::PHINode>(incoming);
                    relay && loop->contains(relay)) {
                    // PHI-only cycles relay the same invariant value. Every
                    // external input/load in the web must match its base.
                    if (!seen.insert(relay).second) return true;
                    for (auto& value:relay->incoming_values())
                        if (!unchanged(value.get())) return false;
                    return true;
                }
                std::optional<z3::expr> next;
                if (auto* load=llvm::dyn_cast<llvm::LoadInst>(incoming)) {
                    auto* object=parent_state->memory.get_object_pointed_by(load->getPointerOperand());
                    if (object && object->is_scalar() &&
                        loop->isLoopInvariant(load->getPointerOperand()) &&
                        reads_entry_array(loop,load,parent_state))
                        next=object->read().as_expr();
                } else if (loop->isLoopInvariant(incoming) &&
                           (llvm::isa<llvm::ConstantInt>(incoming) ||
                            parent_state->memory.get_object(incoming))) {
                    next=parent_state->evaluate(incoming).as_expr();
                }
                if (!next) return false;
                z3::solver check(z3ctx);check.set("timeout",100u);
                check.add(parent_state->get_path_condition().as_expr()&&*next!=initial.as_expr());
                return check.check()==z3::unsat;
            };
            bool stable=true;
            for (unsigned i=0;i<phi.getNumIncomingValues();++i) {
                if (!loop->contains(phi.getIncomingBlock(i))) continue;
                auto* incoming=phi.getIncomingValue(i);
                if (incoming!=&phi && !unchanged(incoming)) { stable=false;break; }
            }
            if (stable)
                initial_state->State::append_path_condition(
                    Expression(initial_state->evaluate(&phi).as_expr()==initial.as_expr()));
        }
        // Establish simple inductive bounds before child-loop summaries are
        // computed, rather than only when preparing the parent's recurrence.
        for (auto& phi : loop->getHeader()->phis()) {
            if (!phi.getType()->isIntegerTy() || phi.getType()->isIntegerTy(1) ||
                !native_context) continue;
            bool inductive = true, initialized = false;
            for (unsigned i=0; i<phi.getNumIncomingValues(); ++i) {
                const auto incoming = phi.getIncomingValue(i);
                if (!loop->contains(phi.getIncomingBlock(i))) {
                    z3::solver check(z3ctx);
                    check.set("timeout",100u);
                    check.add(parent_state->get_path_condition().as_expr());
                    check.add(parent_state->evaluate(incoming).as_expr()<0);
                    if (check.check()!=z3::unsat) { inductive=false; break; }
                    initialized=true;
                } else {
                    auto* add=llvm::dyn_cast<llvm::BinaryOperator>(incoming);
                    auto* step=add ? llvm::dyn_cast<llvm::ConstantInt>(add->getOperand(1)) : nullptr;
                    if (!add || add->getOpcode()!=llvm::Instruction::Add ||
                        add->getOperand(0)!=&phi || !step || step->isNegative()) {
                        inductive=false; break;
                    }
                }
            }
            if (inductive && initialized)
                initial_state->State::append_path_condition(
                    Expression(initial_state->evaluate(&phi).as_expr()>=0));
        }
        symbolize_stores(initial_state);
        if (native_context) {
            // Child summaries retain bounds on frozen outer coordinates.
            // Discard entry facts mentioning this loop's fresh state symbols.
            initial_state->State::append_path_condition(
                Expression(frozen_loop_domain(loop,parent_state)));
        }
        for (auto call_inst : get_unknown_calls(loop)) {
            auto name = get_z3_name(call_inst->getName().str()) + "_call_count";
            initial_state->unknown_call_counters.insert_or_assign(call_inst, z3ctx.int_const(name.c_str()));
        }
        return initial_state;
    }

    void
    LoopExecution::record_assertion(const loop_state_ptr& state,
                                    const Expression& assertion) {
        v_conditions.emplace_back(z3::implies(
            state->get_path_condition().as_expr() && assertion.defined(),
            assertion.as_expr()));
    }

    loop_state_list
    LoopExecution::step(loop_state_ptr state) {
        auto pc = state->pc;
        auto& z3ctx = state->z3ctx;
        loop_state_list new_states;
        if (auto call_inst = llvm::dyn_cast_or_null<llvm::CallInst>(pc->inst)) {
            if (is_uninterpreted_loop_call(call_inst)) {
                auto name = "ari_" + call_inst->getName().str() + "_unknown";
                const z3::sort return_sort =
                    call_inst->getType()->isIntegerTy(1)
                        ? z3ctx.bool_sort() : z3ctx.int_sort();
                auto undef_func = z3ctx.function(
                    name.c_str(), z3ctx.int_sort(), return_sort);
                auto new_state = std::make_shared<LoopState>(*state);
                auto counter_it = new_state->unknown_call_counters.find(call_inst);
                if (counter_it == new_state->unknown_call_counters.end()) {
                    auto counter_name = get_z3_name(call_inst->getName().str()) + "_call_count";
                    auto inserted = new_state->unknown_call_counters.insert_or_assign(call_inst, z3ctx.int_const(counter_name.c_str()));
                    counter_it = inserted.first;
                }
                auto result = undef_func(counter_it->second);
                counter_it->second = counter_it->second + 1;
                new_state->memory.put_temp(call_inst, result);
                new_state->constrain_nondet_input(call_inst, Expression(result));
                new_state->step_pc();
                return {new_state};
            }
        } else if (auto load_inst = llvm::dyn_cast_or_null<llvm::LoadInst>(pc->inst)) {
            // if the instruction is a load, we need to get the value from memory
            bool not_affected = true;
            if (state->session.uses_integer_relaxation()) {
                auto [it, inserted] = entry_reads.try_emplace(load_inst, false);
                if (inserted) {
                    it->second = reads_entry_array(loop, load_inst, parent_state);
                    spdlog::info("[poly-entry] load {} in {}: {}", load_inst->getName().str(),
                                 loop->getHeader()->getName().str(),
                                 it->second ? "entry snapshot proved" : "may depend on loop writes");
                }
                not_affected = it->second;
            } else {
                auto& DI = AnalysisManager::get_instance()->get_DI(load_inst->getFunction());
                for (auto store_inst : stores) {
                    if (auto dep = DI.depends(store_inst, load_inst, true)) {
                        if (dep->isFlow() && !dep->isLoopIndependent()) {
                            not_affected = false;
                            break;
                        }
                    }
                }
            }
            if (not_affected) {
                // if the load instruction is not affected by any store in the loop,
                // we can load the value from parent state
                auto ptr = load_inst->getPointerOperand();
                auto new_state = std::make_shared<LoopState>(*state);

                auto addr = parse_ptr(new_state->memory.get_object(ptr), new_state);
                auto pointed_obj = parent_state->memory.get_object(addr);
                assert(pointed_obj && "Pointed object must exist");
                addr.offset=normalize_array_coordinates(*pointed_obj,addr.offset,new_state);

                auto load_value = pointed_obj->read(addr.offset);
                const z3::expr path =
                    new_state->get_path_condition().as_expr();
                const auto load_conditions = load_value.get_conditions();
                const auto load_expressions = load_value.get_expressions();
                const auto load_definitions = load_value.get_definitions();
                z3::expr_vector restricted_conditions(z3ctx);
                z3::expr_vector restricted_expressions(z3ctx);
                z3::expr_vector restricted_definitions(z3ctx);
                for (unsigned i = 0; i < load_conditions.size(); ++i) {
                    z3::expr condition =
                        simplify_boolean_condition(load_conditions[i], path);
                    if (!is_feasible(condition, path)) continue;
                    restricted_conditions.push_back(condition);
                    // Inline only the part of an earlier array summary that
                    // can be read on this iteration's execution path.
                    restricted_expressions.push_back(restrict_to_domain(
                        load_expressions[i], path && condition));
                    restricted_definitions.push_back(load_definitions[i]);
                }
                assert(!restricted_conditions.empty() &&
                       "An array read must have a feasible value branch");
                new_state->memory.put_temp(
                    load_inst,
                    Expression(restricted_conditions,
                               restricted_expressions,
                               restricted_definitions));
                new_state->step_pc();
                return {new_state};
            }
        }

        for (auto next_state : pc->execute(state)) {
            auto next_loop_state = std::make_shared<LoopState>(LoopState(*next_state));
            for (const auto& condition : next_loop_state->summary_obligations) {
                v_conditions.emplace_back(condition);
            }
            next_loop_state->summary_obligations.clear();
            // The instruction interface records obligations while summarizing
            // instead of switching to the ordinary VERIFYING execution state.
            for (const Expression& assertion : next_loop_state->summary_invariants) {
                record_assertion(next_loop_state, assertion);
            }
            next_loop_state->summary_invariants.clear();
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
            if (cur_state->status == State::UNKNOWN || cur_state->is_over_approx) {
                saw_unknown = true;
                continue;
            }
            if (cur_state->status == State::REACH_ERROR) {
                // Do not summarize only the normal-completion paths and lose
                // an earlier error. Fall back to ordinary execution instead.
                saw_unknown = true;
                continue;
            }
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
                } else if (res == L_TESTUNKNOWN) {
                    saw_unknown = true;
                }
                continue;
            } else if (cur_state->status == State::VERIFYING) {
                record_assertion(cur_state, cur_state->verification_condition);
                // cur_state->append_path_condition(cur_state->verification_condition);
                // auto res = test(cur_state);
                states.push(cur_state);
                // if (res == L_UNFEASIBLE) {
                //     spdlog::error("The annotated loop invariant is not valid");
                // }
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
        auto [final_states, exit_states, v_conditions] = get_final_and_exit_states();
        if (incomplete_execution || final_states.empty() || exit_states.empty()) return;
        log_states(final_states, exit_states);

        summarize_scalar(final_states, exit_states);

        if (summary.has_value()) {//  && !summary.value().is_over_approximated()) {
            summarize_array(final_states, exit_states);
        }
        if (summary.has_value()) { 
            const bool assumptions = contains_assumption(loop);
            bool complete = true;
            auto feasible = parent_state->z3ctx.bool_val(true);
            if (!summary->is_over_approximated() &&
                (assumptions || !get_unknown_calls().empty())) {
                feasible = iteration_feasibility(final_states, assumptions, complete);
            }
            auto correct = prove_invariants(v_conditions,
                assumptions ? feasible : parent_state->z3ctx.bool_val(true),
                !assumptions || complete);
            summary->add_invariant_result(correct);
            summary->set_exact_feasibility(!assumptions || complete);
            // Completion assumptions constrain the exit state, not assertions
            // that may fail before a later iteration aborts.
            retain_input_constraints(feasible);
        }
        spdlog::info("finish summarization");
    }

    void
    LoopSummarizer::log_states(const loop_state_list& final_states, const loop_state_list& exit_states) {
        spdlog::info("For loop {}, there are {} Final states", loop->getName().str(), final_states.size());
        spdlog::info("For loop {}, there are {} exit states", loop->getName().str(), exit_states.size());
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

    std::tuple<loop_state_list, loop_state_list, std::vector<Expression>>
    LoopSummarizer::get_final_and_exit_states() {
        LoopExecution executor(loop, parent_state);
        auto execution_res = executor.run();
        incomplete_execution = executor.had_unknown_result();
        auto final_states = execution_res.first;
        auto exit_states = execution_res.second;

        return {final_states, exit_states, executor.get_v_conditions()};
    }

    VeriResult
    LoopSummarizer::prove_invariants(const std::vector<Expression>& v_conditions,
                                    const z3::expr& feasible, bool complete) {
        spdlog::info("Proving invariants");
        auto manager = AnalysisManager::get_instance();
        auto& z3ctx = manager->get_z3ctx();
        auto n = manager->get_ind_var();
        auto N = iteration_parameter;
        auto N_value = summary->get_N();
        for (auto& v_condition : v_conditions) {
            z3::solver solver(z3ctx);
            z3::expr premise = 0 <= n &&
                parent_state->get_path_condition().as_expr() &&
                summary->get_constraints();
            // A body assertion may fail on the partial iteration that exits
            // before the next backedge. Its recorded path still contains the
            // header guard, so ordinary zero-trip exits remain vacuous.
            if (N_value.has_value()) premise = premise && n <= *N_value;
            if (!feasible.is_true()) {
                const z3::expr prior(z3ctx, Z3_mk_fresh_const(
                    z3ctx, "completed_iteration", z3ctx.int_sort()));
                z3::expr_vector from(z3ctx), to(z3ctx);
                from.push_back(n); to.push_back(prior);
                auto condition = feasible;
                // Only completed iterations precede this assertion. An
                // assumption later in this or a future iteration cannot help.
                premise = premise && z3::forall(prior, z3::implies(
                    prior >= 0 && prior < n, condition.substitute(from, to)));
            }
            // looks like there are some bugs in z3 for doing negation of forall
            // z3::expr query =  z3::forall(n, z3::implies(premise, summary->evaluate_expr(v_condition.as_expr())));
            // solver.add(!query);
            z3::expr query = summary->evaluate_expr(v_condition.as_expr());
            if (parent_state->is_summarizing()) {
                // The outer counter is still symbolic. Discharge this guarded
                // child obligation after substituting the outer trajectory.
                summary->add_obligation(z3::forall(n, z3::implies(premise, query)));
                continue;
            }
            auto& session = VerificationSession::current();
            const auto symbolic_query = premise && !query;
            solver.add(session.aggregate_query(symbolic_query, premise));
            auto res = solver.check();
            if (res == z3::sat) {
                if (parent_state->is_over_approx || !parent_state->counterexample_complete ||
                    session.contains_aggregates(session.expand_aggregates(symbolic_query))) {
                    return VERIUNKNOWN;
                }
                if (!complete) {
                    // Projected history is enough for a proof, but not for a
                    // counterexample beyond the first (history-free) iteration.
                    solver.add(n == 0);
                    if (solver.check() != z3::sat) return VERIUNKNOWN;
                }
                if (const auto* directory = std::getenv("ARITHEXE_SOLVER_TRACE_DIR")) {
                    std::filesystem::create_directories(directory);
                    const auto prefix = loop->getHeader()->getParent()->getName().str() +
                                        "_" + loop->getHeader()->getName().str();
                    std::ofstream query(std::filesystem::path(directory) /
                                        (prefix + "_assertion_sat.smt2"));
                    query << solver.to_smt2();
                    std::ofstream model(std::filesystem::path(directory) /
                                        (prefix + "_assertion_sat.model"));
                    model << solver.get_model();
                }
                return FAIL;
            } else if (res == z3::unknown) {
                spdlog::warn("Loop assertion check inconclusive: {}", solver.reason_unknown());
                if (const auto* directory = std::getenv("ARITHEXE_SOLVER_TRACE_DIR")) {
                    std::filesystem::create_directories(directory);
                    std::ofstream output(std::filesystem::path(directory) /
                        ("assert_" + loop->getHeader()->getName().str() + ".smt2"));
                    output << solver.to_smt2();
                }
                return VERIUNKNOWN;
            }
        }
        return HOLD;
    }

    z3::expr
    LoopSummarizer::iteration_feasibility(const loop_state_list& final_states,
                                          bool allow_entry_arrays, bool& complete) {
        const auto calls = get_unknown_calls();
        auto manager = AnalysisManager::get_instance();
        auto& ctx = manager->get_z3ctx();
        std::set<std::string> oracles;
        for (const auto call : calls) {
            oracles.insert("ari_" + call->getName().str() + "_unknown");
        }
        std::set<unsigned> snapshots;
        bool stable_quantifiers = false;
        if (allow_entry_arrays) {
            bool unchanged = true;
            const auto arrays = parent_state->memory.get_arrays();
            for (const auto& array : arrays) {
                for (const auto& state : final_states) {
                    const auto final = state->memory.get_object(array->get_addr());
                    if (!final || !z3::eq(final->get_value().as_expr(), array->get_value().as_expr()) ||
                        !z3::eq(final->get_value().defined(), array->get_value().defined())) {
                        unchanged = false;
                    }
                }
            }
            if (unchanged) {
                stable_quantifiers = true;
                for (const auto& array : arrays) {
                    std::vector<z3::expr> pending{array->get_value().as_expr()};
                    std::set<unsigned> seen;
                    while (!pending.empty()) {
                        auto term = pending.back(); pending.pop_back();
                        if (!seen.insert(term.id()).second || !term.is_app()) continue;
                        if (term.num_args() && term.decl().decl_kind() == Z3_OP_UNINTERPRETED)
                            snapshots.insert(term.decl().id());
                        for (const auto& arg : term.args()) pending.push_back(arg);
                    }
                }
            }
        }
        // Array snapshots in a one-step state are not necessarily entry arrays.
        // Project out entire conjuncts containing such applications, preserving
        // a necessary (possibly weaker) condition on successful iterations.
        const auto scalar_input_only = [&](const z3::expr& expr,
                                            const auto& self) -> bool {
            if (expr.is_quantifier())
                return stable_quantifiers && self(expr.body(), self);
            if (expr.is_var()) return stable_quantifiers;
            if (!expr.is_app()) return false;
            if (expr.num_args() > 0 &&
                expr.decl().decl_kind() == Z3_OP_UNINTERPRETED &&
                !oracles.count(expr.decl().name().str()) &&
                !snapshots.count(expr.decl().id())) return false;
            for (const auto& arg : expr.args()) {
                if (!self(arg, self)) return false;
            }
            return true;
        };
        const auto project = [&](const z3::expr& expr,
                                 const auto& self) -> z3::expr {
            if (expr.is_and()) {
                z3::expr result = ctx.bool_val(true);
                for (const auto& arg : expr.args()) result = result && self(arg, self);
                return result;
            }
            if (scalar_input_only(expr, scalar_input_only)) return expr;
            complete = false;
            return ctx.bool_val(true);
        };
        z3::expr feasible = ctx.bool_val(false);
        for (const auto& state : final_states) {
            feasible = feasible || project(state->get_path_condition().as_expr(), project);
        }
        return project(summary->evaluate_expr(feasible).simplify(), project).simplify();
    }

    void
    LoopSummarizer::retain_input_constraints(const z3::expr& feasible) {
        if (feasible.is_true() || summary->is_over_approximated() ||
            !summary->get_N().has_value()) return;
        auto manager = AnalysisManager::get_instance();
        const auto n = manager->get_ind_var();
        const auto N = *summary->get_N();
        const auto condition = z3::forall(
            n, z3::implies(0 <= n && n < N, feasible)).simplify();
        summary->add_constraints(condition);
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
        std::vector<std::pair<z3::expr, z3::expr>> auxiliary_forms;
        for (auto call : get_unknown_calls()) {
            const auto name = get_z3_name(call->getName().str()) + "_call_count";
            const auto counter = z3ctx.int_const(name.c_str());
            const auto found = closed.find(counter);
            if (found == closed.end()) is_approximate = true;
            else auxiliary_forms.emplace_back(counter, found->second);
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

        z3::expr_vector solving_params(z3ctx), solving_values(z3ctx);
        for (unsigned i = 0; i < params.size(); ++i) {
            solving_params.push_back(params[i]);
            solving_values.push_back(params_values[i]);
        }
        for (const auto& [counter, value] : auxiliary_forms) {
            solving_params.push_back(counter);
            solving_values.push_back(value);
        }
        if (!std::includes(params_str.begin(), params_str.end(),
                           iteration_related_vars.begin(), iteration_related_vars.end())) {
            // An unsolved guard component is a changing state value, not an
            // invariant parameter. Its entry value cannot constrain every
            // iteration or the exit without excluding real executions.
            spdlog::warn("Cannot summarize loop: a guard component has no certified solution");
            return;
        }
        auto [N_constraints, N, count_constraints] = get_iterations_constraints(exit_states, solving_params, solving_values);
        const auto contains_modulo = [](const z3::expr& expression,
                                        const auto& self) -> bool {
            if (expression.is_var()) return false;
            if (expression.is_quantifier()) {
                return self(expression.body(), self);
            }
            if (!expression.is_app()) return false;
            if (expression.decl().decl_kind() == Z3_OP_MOD) return true;
            for (const z3::expr& argument : expression.args()) {
                if (self(argument, self)) return true;
            }
            return false;
        };
        if (!contains_modulo(count_constraints, contains_modulo)) {
            auto linear_logic = LinearLogic();
            z3::expr_vector N_vec(z3ctx);
            N_vec.push_back(N);
            spdlog::info("Solving for N");
            // Input-array quantifiers do not belong in count synthesis. The
            // independent guard constraints still characterize the first exit.
            auto N_value = linear_logic.solve_vars(count_constraints, N_vec);
            if (!N_value.empty()) {
                z3::expr_vector from(z3ctx), to(z3ctx);
                from.push_back(N);
                to.push_back(N_value[0]);
                auto instantiated = count_constraints;
                instantiated = instantiated.substitute(from, to);
                z3::solver certificate(z3ctx);
                certificate.set("timeout", 200u);
                if (native_array_context(parent_state))
                    certificate.add(frozen_loop_domain(loop,parent_state));
                certificate.add(!instantiated);
                if (certificate.check() != z3::unsat) N_value = z3::expr_vector(z3ctx);
            }
            if (N_value.empty()) N_value = linear_logic.solve_vars(N_constraints, N_vec);
            if (N_value.size() == 0) {
                spdlog::info("fail to compute the number of iterations, record the constraints on it in path conditions");
            } else {
                // Independent synthesis includes zero-trip parameter regions.
                // Remove them only when the actual entry path proves they are
                // unreachable, before composing this count into later loops.
                N_opt = restrict_to_domain(N_value[0],
                    parent_state->get_path_condition().as_expr()).simplify();
                spdlog::info("The number of iterations is {}", N_opt->to_string());
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
            auto new_second = p.second.substitute(solving_params, solving_values).substitute(src, dst);
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
                auto [exit_condition, symbolic_N] =
                    get_exit_loop_guard(exit_states);
                (void)symbolic_N;
                summary = LoopSummary(
                    params, exit_values, params_values, over_closed,
                    exit_condition, N_opt);
            }
        } else {
            z3::expr summary_constraints = N_constraints;
            if (N_opt.has_value()) {
                z3::expr_vector from(z3ctx), to(z3ctx);
                from.push_back(N);
                to.push_back(*N_opt);
                summary_constraints = summary_constraints.substitute(from, to).simplify();
            }
            summary = LoopSummary(
                params, exit_values, params_values, summary_constraints,
                modified_values,
                N_opt.has_value() ? N_opt
                                  : std::optional<z3::expr>(N));
        }
        summary->set_functional_iteration_count(N_opt.has_value());
        for (const auto& [counter, value] : auxiliary_forms) {
            summary->add_auxiliary_closed_form(counter, value);
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
        // base_eq.insert_or_assign(rec_func(args), array->get_value().as_expr());

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

        // z3::expr_vector sub_src(z3ctx);
        // z3::expr_vector sub_dst(z3ctx);
        // retrieve closed-form solutions to scalar variables
        // and substitute them into the array update
        // auto update = scalars.evaluate_expr(array->get_value().as_expr()).simplify();
        // llvm::errs() << update.to_string() << "\n";
        // auto apps_of_arr = get_app_of(update, sig.decl());
        // for (auto app : apps_of_arr) {
        //     sub_src.push_back(app);
        //     auto args = app.args();
        //     args.push_back(manager->get_ind_var());
        //     sub_dst.push_back(rec_func(args));
        //     llvm::errs() << app.to_string() << " -> " << rec_func(args).to_string() << "\n";
        // }
        auto scalars = summary.value();

        z3::expr_vector lhs_args(sig.args());
        lhs_args.push_back(manager->get_ind_var()); // add loop counter
        std::vector<z3::expr> conditions;
        std::vector<rec_ty> eqs;
        // A substituted child summary may place its frame in any branch.
        auto array_conditions = array->get_value().get_conditions();
        auto array_expressions = array->get_value().get_expressions();
        for (unsigned i = 0; i < array_conditions.size(); ++i) {
            if (z3::eq(array_expressions[i].simplify(), sig)) continue;
            auto eval_expr = summary.value().evaluate_expr(array_expressions[i]);
            auto execution_domain = scalars.evaluate_expr(
                state->get_path_condition().as_expr() && array_conditions[i]);
            const auto iteration = manager->get_ind_var();
            execution_domain = execution_domain && iteration >= 0;
            if (const auto count = scalars.get_N()) {
                // A recurrence may extend beyond the loop; earlier array
                // summaries only need to be specialized on actual iterations.
                execution_domain = execution_domain && iteration < *count &&
                                   scalars.get_constraints();
            }
            // Dropping non-arithmetic conjuncts weakens the premise, so any
            // rewrite certified here is also valid in the full context.
            // Keep unrelated array constraints out of polynomial certificates.
            eval_expr = restrict_to_domain(eval_expr, arithmetic_conjuncts(execution_domain));
            eval_expr = restrict_to_domain(eval_expr, execution_domain);
            auto apps_of_arr = get_app_of(eval_expr, sig.decl());
            z3::expr_vector sub_src(z3ctx);
            z3::expr_vector sub_dst(z3ctx);
            for (auto app : apps_of_arr) {
                sub_src.push_back(app);
                auto args = app.args();
                args.push_back(manager->get_ind_var());
                sub_dst.push_back(rec_func(args));
            }
            auto condition = scalars.evaluate_expr(path_condition.as_expr() && array_conditions[i]).simplify();
            // llvm::errs() << summary.value().evaluate_expr(array_expressions[i]).to_string() << "\n";
            // llvm::errs() << summary.value().evaluate_expr(array_expressions[i]).substitute(sub_src, sub_dst).to_string() << "\n";
            // auto trans = summary.value().evaluate_expr(array_expressions[i]).substitute(sub_src, sub_dst).substitute(go_back_src, go_back_dst);
            auto trans = eval_expr.substitute(sub_src, sub_dst).substitute(go_back_src, go_back_dst);
            auto [ite_conditions, ite_expressions] = expr2piecewise(trans);
            for (int j = 0; j < ite_conditions.size(); j++) {
                rec_ty eq;
                eq.insert_or_assign(rec_func(lhs_args), ite_expressions[j]);
                eqs.push_back(eq);
                // auto cur_cond = condition && ite_conditions[j];
                const auto guard = condition.substitute(go_back_src, go_back_dst) && ite_conditions[j];
                conditions.push_back(loop->getSubLoops().empty()
                    ? guard : normalize_array_guard(guard));
            }
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
            const Expression entry_value = array->get_value();
            const bool unchanged = !final_states.empty() && std::all_of(
                final_states.begin(), final_states.end(),
                [&](const loop_state_ptr& state) {
                    auto final_array = state->memory.get_object(array->get_addr());
                    if (!final_array) return false;
                    const Expression final_value = final_array->get_value();
                    return z3::eq(final_value.as_expr(), entry_value.as_expr()) &&
                           z3::eq(final_value.defined(), entry_value.defined());
                });
            if (unchanged) {
                // Retain established read-only summaries instead of solving
                // identity recurrences over their piecewise array values.
                summary->add_closed_form(array->get_signature(), entry_value.as_expr());
                summary->add_preserved_value(array->get_llvm_value());
                continue;
            }
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
            for (int j = 0; j < conditions.size(); j++) {

            }
            closed_form_ty closed;
            if (parent_state->session.uses_integer_relaxation() &&
                !scalar_summary.is_over_approximated() &&
                scalar_summary.has_functional_iteration_count()) {
                std::vector<z3::expr> values;
                for (const auto& equation:eqs) {
                    if (equation.size()!=1) break;
                    values.push_back(equation.begin()->second);
                }
                bool independent=true;
                for (const auto other:arrays) {
                    if (other==array) continue;
                    const auto other_value=other->get_value();
                    const bool frozen=std::all_of(final_states.begin(),final_states.end(),
                        [&](const loop_state_ptr& state) {
                            const auto final=state->memory.get_object(other->get_addr());
                            return final&&z3::eq(final->get_value().as_expr(),other_value.as_expr())&&
                                   z3::eq(final->get_value().defined(),other_value.defined());
                        });
                    if (frozen) continue;
                    for (const auto& value:values) {
                        if (!get_app_of(value,other->get_signature().decl()).empty() ||
                            !get_app_of(value,get_array_rec_func(other)).empty()) {
                            independent=false;
                        }
                    }
                }
                if (independent&&values.size()==eqs.size()) {
                    auto valid_domain=arithmetic_conjuncts(
                        parent_state->get_path_condition().as_expr());
                    const auto sizes=array->get_sizes();
                    const auto indices=array->get_indices();
                    for (unsigned d=0;d<indices.size();++d) {
                        valid_domain=valid_domain&&indices[d]>=0&&
                                     indices[d]<sizes[d].as_expr();
                    }
                    if (auto region=summarize_overwrite_regions(
                            array->get_signature(),get_array_rec_func(array),
                            manager->get_ind_var(),conditions,values,valid_domain,
                            scalar_summary.has_functional_iteration_count()
                                ? scalar_summary.get_N() : std::nullopt)) {
                        closed.insert_or_assign(array->get_signature(),*region);
                    }
                }
            }
            if (closed.empty()) closed=solve_recurrence(conditions,eqs);
            spdlog::info("Array summaries are computed successfully");

            z3::expr_vector n_src(z3ctx);
            n_src.push_back(manager->get_ind_var());
            z3::expr_vector N_dst(z3ctx);
            auto N = scalar_summary.get_N().value_or(iteration_parameter);
            N_dst.push_back(N);
            auto dims = array->get_sizes();
            z3::expr domain =
                parent_state->get_path_condition().as_expr();
            auto indices = array->get_indices();
            for (int i = 0 ; i < dims.size(); i++) {
                domain = domain && 0 <= indices[i] && indices[i] < dims[i].as_expr() && 0 <= N;
            }
            if (closed.size() == 0) {
                summary->set_over_approximated(true);
            }
            for (auto& [func, expr] : closed) {
                spdlog::info("Restricting array summaries to the domain");
                z3::expr normalized_expr = expr;
                auto returned_indices = func.args();
                if (returned_indices.size() == indices.size()) {
                    normalized_expr =
                        normalized_expr.substitute(returned_indices, indices)
                            .simplify();
                }
                auto all_apps = get_app_of(
                    normalized_expr, array->get_signature().decl());
                z3::expr_vector app_values(z3ctx);
                auto initial_value = array->get_value().as_expr();
                auto params = array->get_indices();
                for (auto app : all_apps) {
                    auto args = app.args();
                    app_values.push_back(initial_value.substitute(params, args).simplify());
                }
                // substitute initial values
                auto real_closed_form = normalized_expr
                                            .substitute(all_apps, app_values)
                                            .substitute(n_src, N_dst);
                // auto real_closed_form = closed_form.substitute(all_apps, app_values);
                auto closed_form = restrict_to_domain(real_closed_form,
                    arithmetic_conjuncts(domain));
                closed_form = restrict_to_domain(closed_form, domain).simplify();
                spdlog::info("Array closed form: {}", closed_form.to_string());
                summary->add_closed_form(array->get_signature(), closed_form);
                spdlog::info("Finish Restricting");

            }
        }
    }

    closed_form_ty
    LoopSummarizer::solve_recurrence(const std::vector<z3::expr>& conditions, const std::vector<rec_ty>& rec_eqs) {
        auto manager = AnalysisManager::get_instance();
        auto& z3ctx = manager->get_z3ctx();
        rec_solver solver(z3ctx);
        solver.set_ind_var(manager->get_ind_var());
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
        // Counter functions are already included in func alongside the PHIs.
        const auto calls = get_unknown_calls();
        for (std::size_t i = 0; i < calls.size(); ++i) {
            z3::expr initial = z3ctx.int_val(0);
            if (auto outer = std::dynamic_pointer_cast<LoopState>(parent_state)) {
                const auto found = outer->unknown_call_counters.find(calls[i]);
                if (found != outer->unknown_call_counters.end()) initial = found->second;
            }
            values.push_back(initial);
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

    void
    LoopExecution::symbolize_stores(loop_state_ptr state) {
        auto stores = get_all_stores(loop);
        for (auto store : stores) {
            auto ptr = store->getPointerOperand();
            // Resolve formal pointers through their caller-provided addresses.
            // Only object discovery strips offsets; actual loads/stores retain them.
            auto base_value = llvm::getUnderlyingObject(ptr, 0);
            auto written_obj = state->memory.get_object_pointed_by(base_value);
            if (!written_obj) {
                throw VerifierError(VerifierIssueKind::UnsupportedSemantics,
                                    "Cannot resolve loop store memory object");
            }
            written_obj->write(written_obj->get_signature());
        }
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
        auto unknown_calls = get_unknown_calls();
        for (auto state : final_states) {
            for (auto& [call_inst, _] : state->unknown_call_counters) {
                auto unknown_call = llvm::dyn_cast<llvm::CallInst>(call_inst);
                if (unknown_call && std::find(unknown_calls.begin(), unknown_calls.end(), unknown_call) == unknown_calls.end()) {
                    unknown_calls.push_back(unknown_call);
                }
            }
        }
        this->unknown_calls = unknown_calls;
        auto header_phis_scalar_and_func = get_header_phis_scalar_and_func();
        auto src = header_phis_scalar_and_func.first;
        auto dst = header_phis_scalar_and_func.second;
        std::vector<std::vector<z3::expr>> state_updates;
        for (auto state : final_states) state_updates.push_back(get_update(state));

        // Prove simple counter bounds before inlining read-only arrays into
        // dependent accumulators. Unknown queries never justify a bound.
        z3::expr invariant = rec_s.z3ctx.bool_val(true);
        z3::solver invariant_solver(rec_s.z3ctx);
        z3::params invariant_parameters(rec_s.z3ctx);
        invariant_parameters.set("timeout", 200u);
        invariant_solver.set(invariant_parameters);
        const auto predecessor = parent_state->trace.back();
        for (unsigned i = 0; i < phis.size(); ++i) {
            const auto initial = parent_state->evaluate(
                phis[i]->getIncomingValueForBlock(predecessor)).as_expr();
            invariant_solver.push();
            invariant_solver.add(parent_state->get_path_condition().as_expr());
            invariant_solver.add(initial < 0);
            const bool initialized_nonnegative =
                invariant_solver.check() == z3::unsat;
            invariant_solver.pop();
            if (!initialized_nonnegative) continue;
            const auto candidate = src[i] >= 0;
            bool inductive = true;
            for (unsigned j = 0; j < final_states.size(); ++j) {
                invariant_solver.push();
                invariant_solver.add(candidate &&
                    final_states[j]->get_path_condition().as_expr() &&
                    state_updates[j][i] < 0);
                inductive = invariant_solver.check() == z3::unsat;
                invariant_solver.pop();
                if (!inductive) break;
            }
            if (inductive) invariant = invariant && candidate;
        }

        for (unsigned state_index = 0; state_index < final_states.size(); ++state_index) {
            const auto state = final_states[state_index];
            auto path_cond = state->path_condition_in_loop.as_expr().substitute(src, dst);
            auto updates = state_updates[state_index];
            for (auto call_inst : unknown_calls) {
                auto counter_it = state->unknown_call_counters.find(call_inst);
                if (counter_it == state->unknown_call_counters.end()) {
                    auto counter_name = get_z3_name(call_inst->getName().str()) + "_call_count";
                    auto counter = rec_s.z3ctx.int_const(counter_name.c_str());
                    auto unknown_name = "ari_" + call_inst->getName().str() + "_unknown";
                    auto unknown_func = rec_s.z3ctx.function(unknown_name.c_str(), rec_s.z3ctx.int_sort(), rec_s.z3ctx.int_sort());
                    auto unknown_apps = get_app_of(path_cond, unknown_func);
                    updates.push_back(unknown_apps.size() > 0 ? counter + 1 : counter);
                } else {
                    updates.push_back(counter_it->second);
                }
            }

            // collect all conditions and expressions by unfolding ite
            std::vector<z3::expr_vector> ite_conditions;
            std::vector<z3::expr_vector> ite_expressions;
            std::vector<int> sizes; // record the size of each update
            const auto execution_domain =
                state->get_path_condition().as_expr() && invariant;
            for (auto update : updates) {
                update = restrict_to_domain(update, execution_domain);
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
                    std::string name;
                    if (i < phis.size()) {
                        name = get_z3_name(phis[i]->getName().str());
                    } else {
                        auto call_inst = unknown_calls[i - phis.size()];
                        name = get_z3_name(call_inst->getName().str()) + "_call_count";
                    }
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

    static bool contains_modulo(const z3::expr& expression) {
        if (expression.is_var()) return false;
        if (expression.is_quantifier()) {
            return contains_modulo(expression.body());
        }
        if (!expression.is_app()) return false;
        if (expression.decl().decl_kind() == Z3_OP_MOD) return true;
        for (const z3::expr& argument : expression.args()) {
            if (contains_modulo(argument)) return true;
        }
        return false;
    }

    std::tuple<z3::expr, z3::expr, z3::expr>
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
        z3::expr N = iteration_parameter;
        z3::expr n = manager->get_ind_var();
        const auto parameter_domain=native_array_context(parent_state)
            ? frozen_loop_domain(loop,parent_state) : z3ctx.bool_val(true);
        loop_guard_closed_form = simplify_boolean_condition(
            restrict_to_domain(loop_guard_closed_form, n>=0&&parameter_domain),
            n>=0&&parameter_domain);
        z3::expr constraints = z3::forall(n, z3::implies(0 <= n && n < N, loop_guard_closed_form.substitute(params, values)));
        z3::expr_vector src(z3ctx);
        z3::expr_vector dst(z3ctx);
        src.push_back(n);
        dst.push_back(N);
        constraints = constraints && !loop_guard_closed_form.substitute(src, dst);
        constraints = constraints && N >= 0;
        spdlog::info("constraints for N: {}", constraints.to_string());
        constraints = constraints.substitute(params, values);
        auto parent_condition = parent_state->get_path_condition().as_expr();
        parent_condition = parent_condition.substitute(params, values);

        if (contains_modulo(constraints)) {
            return {constraints && parent_condition, N, constraints};
        }

        z3::tactic qe_tactic = z3::tactic(z3ctx, "qe");
        z3::goal g(z3ctx);
        g.add(constraints);
        z3::apply_result result = qe_tactic(g);
        z3::expr_vector N_vec(z3ctx);
        N_vec.push_back(N);
        auto N_constraints = apply_result2expr(result);
        return {N_constraints && parent_condition, N, N_constraints};
    }

    std::pair<z3::expr, z3::expr>
    LoopSummarizer::get_exit_loop_guard(const loop_state_list& exit_states) {
        auto loop_guard = get_loop_guard_condition(exit_states);
        auto manager = AnalysisManager::get_instance();
        z3::expr_vector src(loop_guard.ctx());
        z3::expr_vector dst(loop_guard.ctx());
        auto n = manager->get_ind_var();
        auto N = iteration_parameter;
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
        const auto result=(!acc_exit_cond).simplify();
        return native_array_context(parent_state)
            ? simplify_boolean_condition(result,frozen_loop_domain(loop,parent_state))
            : result;
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
        for (auto call_inst : get_unknown_calls()) {
            auto name = get_z3_name(call_inst->getName().str()) + "_call_count";
            auto counter_src = z3ctx.int_const(name.c_str());
            auto counter_dst = z3ctx.function(name.c_str(), z3ctx.int_sort(), z3ctx.int_sort());
            src.push_back(counter_src);
            dst.push_back(counter_dst(manager->get_ind_var()));
        }
        return {src, dst};
    }

    std::vector<llvm::CallInst*>
    LoopSummarizer::get_unknown_calls() {
        std::vector<llvm::CallInst*> calls;
        for (auto& block : loop->blocks()) {
            for (auto& inst : *block) {
                if (auto call_inst = llvm::dyn_cast_or_null<llvm::CallInst>(&inst)) {
                    if (is_uninterpreted_loop_call(call_inst)) {
                        calls.push_back(call_inst);
                    }
                }
            }
        }
        for (auto call_inst : unknown_calls) {
            if (std::find(calls.begin(), calls.end(), call_inst) == calls.end()) {
                calls.push_back(call_inst);
            }
        }
        return calls;
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
