#include "engine.h"
#include "PathAccelerator.h"
#include "ResidueProof.h"
#include "RationalQuery.h"
#include "QuantifiedQuery.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <functional>
#include <chrono>

#include <spdlog/spdlog.h>

using namespace ari_exe;

Engine::Engine()
    : session(std::make_shared<VerificationSession>()),
      z3ctx(session->analyses().get_z3ctx()), mod(nullptr), solver(z3ctx) {}

Engine::Engine(const std::string& c_filename)
    : session(std::make_shared<VerificationSession>()),
      z3ctx(session->analyses().get_z3ctx()), mod(nullptr), solver(z3ctx) {
    auto activation = session->activate();
    auto manager = &session->analyses();
    assert(&manager->get_z3ctx() == &z3ctx && "The z3 context is not the same as the analysis manager");
    mod = manager->get_module(c_filename, z3ctx);
}

Engine::~Engine() {
    auto activation = session->activate();
    states = {};
    session->clear_module_state();
    mod.reset();
}

VeriResult
Engine::verify() {
    auto activation = session->activate();
    // Early termination may leave unexplored sibling paths from the previous
    // run. A top-level verification must always start from a fresh queue.
    states = {};
    results.clear();
    issue_recorded = false;
    issue_message.clear();
    violation_instruction = nullptr;
    counterexample_inputs.clear();
    loop_certificates.clear();
    function_certificates.clear();
    property_pruned_states = 0;
    run();
    auto res = VERIUNKNOWN;
    if (std::all_of(results.begin(), results.end(), [](VeriResult veri_res) { return veri_res == HOLD; })) {
        res = HOLD;
    } else if (std::any_of(results.begin(), results.end(), [](VeriResult veri_res) { return veri_res == FAIL; })) {
        res = FAIL;
    }
    if (property_pruned_states)
        spdlog::info("[property-prune] discharged {} error-free continuation(s)",
                     property_pruned_states);
    return res;
}

bool Engine::can_prune_property_irrelevant(const state_ptr& state) const {
    if (const char* enabled = std::getenv("ARITHEXE_PROPERTY_PRUNING");
        enabled && std::string(enabled) == "0") return false;
    // The PC is already advanced for VERIFYING/REACH_ERROR. Never let a
    // harmless suffix hide an obligation or an UNKNOWN recorded at that PC.
    if (state->is_summarizing() ||
        !state->coarse_boundary_states.empty() ||
        (!coarse_proof_active && state->coarse_fallback_state) ||
        (state->status != State::RUNNING && state->status != State::TESTING) ||
        !state->pc || !state->pc->inst ||
        property_reachability.may_reach_obligation(state->pc->inst))
        return false;
    for (const auto* site : state->memory.return_sites()) {
        if (!site || !site->inst ||
            property_reachability.may_reach_obligation(site->inst->getNextNode()))
            return false;
    }
    return true;
}

std::vector<state_ptr>
Engine::step(state_ptr state) {
    auto activation = session->activate();
    auto pc = state->pc;
    return pc->execute(state);
}

void
Engine::set_entry(llvm::Function* entry) {
    this->entry = entry;
}

bool Engine::prove_coarse_independently(const state_ptr& jump) {
    // No exact siblings are run in this transaction. A successful proof
    // covers every finite continuation from the saved entry, so running an
    // exact first-break sibling as well is neither necessary nor useful.
    auto pending = std::move(states);
    states = {};
    const auto result_checkpoint = results.size();
    const auto loop_checkpoint = loop_certificates.size();
    const auto function_checkpoint = function_certificates.size();
    const bool previous_issue = issue_recorded;
    const auto previous_kind = issue_kind;
    const auto previous_message = issue_message;
    const auto previous_violation = violation_instruction;
    const auto previous_inputs = counterexample_inputs;
    bool proved = false;
    coarse_proof_active = true;
    spdlog::debug("[coarse-path] starting independent entry-rooted proof at degree {}",
                  jump->coarse_degree);
    try {
        run(jump);
        proved = states.empty() && std::all_of(
            results.begin() + result_checkpoint, results.end(),
            [](VeriResult result) { return result == HOLD; });
    } catch (const std::exception& error) {
        spdlog::debug("[coarse-path] independent proof failed: {}", error.what());
    }
    coarse_proof_active = false;
    states = std::move(pending);
    // Diagnostics and possible counterexamples from an over-approximate
    // proof are speculative, even if some other assertions were proved.
    issue_recorded = previous_issue;
    issue_kind = previous_kind;
    issue_message = previous_message;
    violation_instruction = previous_violation;
    counterexample_inputs = previous_inputs;
    if (!proved) {
        results.resize(result_checkpoint);
        loop_certificates.erase(loop_certificates.begin() + loop_checkpoint,
                                loop_certificates.end());
        function_certificates.erase(function_certificates.begin() + function_checkpoint,
                                    function_certificates.end());
    }
    spdlog::debug("[coarse-path] independent proof {}; {}",
                  proved ? "discharged" : "inconclusive",
                  proved ? "exact continuation is unnecessary"
                         : "discarding speculative worklist and results");
    return proved;
}

unsigned Engine::configure_query_timeout(bool accelerated) {
    unsigned timeout = accelerated ? 3000 : 0;
    if (coarse_proof_active) {
        timeout = 3000;
        if (const char* raw = std::getenv("ARITHEXE_COARSE_TIMEOUT_MS")) {
            char* end = nullptr;
            const auto parsed = std::strtoul(raw, &end, 10);
            if (raw[0] && raw[0] != '-' && end && !*end && parsed > 0 &&
                parsed <= std::numeric_limits<unsigned>::max()) timeout = parsed;
        }
    }
    z3::params parameters(z3ctx);
    parameters.set("timeout", timeout);
    solver.set(parameters);
    return timeout;
}

z3::check_result Engine::check_precise_query(unsigned timeout_ms, bool normalize_rationals) {
    const auto original = solver.assertions();
    z3::expr_vector constants(z3ctx);
    bool removed = false;
    std::function<void(z3::expr)> collect = [&](z3::expr e) {
        if (e.is_quantifier()) { collect(e.body()); return; }
        if (!e.is_app()) return;
        if (e.is_const() && e.decl().decl_kind() == Z3_OP_UNINTERPRETED &&
            (e.is_int() || e.is_bool())) {
            for (unsigned i = 0; i < constants.size(); ++i)
                if (z3::eq(constants[i], e)) return;
            constants.push_back(e);
        }
        for (unsigned i = 0; i < e.num_args(); ++i) collect(e.arg(i));
    };
    // Only positive conjuncts may be relaxed. Never erase a quantifier under
    // negation, disjunction or an implication.
    std::function<z3::expr(z3::expr)> relax = [&](z3::expr e) -> z3::expr {
        if (e.is_quantifier() && Z3_is_quantifier_forall(z3ctx, e)) {
            removed = true; return z3ctx.bool_val(true);
        }
        if (e.is_and()) {
            z3::expr result = z3ctx.bool_val(true);
            for (unsigned i = 0; i < e.num_args(); ++i) result = result && relax(e.arg(i));
            return result;
        }
        return e;
    };
    z3::solver candidates(z3ctx);
    z3::params parameters(z3ctx);
    parameters.set("timeout", std::min(timeout_ms, 500u));
    candidates.set(parameters);
    bool has_count = false;
    for (unsigned i = 0; i < original.size(); ++i) {
        collect(original[i]); candidates.add(relax(original[i]));
    }
    for (unsigned i = 0; i < constants.size(); ++i) {
        auto c = constants[i];
        if (c.is_int() && c.decl().name().str().rfind("ari_path_k_", 0) == 0) {
            candidates.add(c >= 0 && c <= 8); has_count = true;
        }
    }
    if (removed && has_count) {
        for (unsigned attempt = 0; attempt < 3 && candidates.check() == z3::sat; ++attempt) {
            auto model = candidates.get_model();
            z3::expr bindings = z3ctx.bool_val(true);
            z3::expr_vector values(z3ctx);
            for (unsigned i = 0; i < constants.size(); ++i) {
                values.push_back(model.eval(constants[i], true));
                bindings = bindings && constants[i] == values.back();
            }
            solver.reset();
            if (normalize_rationals) {
                // Substitute pinned inputs first so constant divisions and
                // closed forms reduce before introducing quotient witnesses.
                auto pinned = z3::mk_and(original).substitute(constants, values).simplify();
                solver.add(bindings);
                solver.add(integerize_rational_query(pinned, false));
            }
            else { solver.add(original); solver.add(bindings); }
            solver.set(parameters);
            // Crucially, validate SAT against the ORIGINAL quantified guards
            // and C definedness conditions. Relaxation alone is never evidence.
            auto validation = solver.check();
            if (normalize_rationals && validation == z3::unknown) {
                // Equivalent encodings can have different solver behavior.
                // Keep the established concrete-candidate validation fallback.
                solver.reset(); solver.add(original); solver.add(bindings);
                solver.set(parameters);
                validation = solver.check();
            }
            if (validation == z3::sat) {
                spdlog::debug("[path-query] validated bounded concrete candidate against original constraints");
                return z3::sat;
            }
            candidates.add(!bindings);
        }
    }
    solver.reset();
    if (normalize_rationals)
        solver.add(integerize_rational_query(z3::mk_and(original), false));
    else solver.add(original);
    parameters.set("timeout", timeout_ms); solver.set(parameters);
    if (normalize_rationals)
        spdlog::debug("[assertion-query normalized; timeout={}ms]\n{}\n[end assertion-query]",
                      timeout_ms, solver.to_smt2());
    auto result = solver.check();
    if (normalize_rationals && result == z3::unknown) {
        solver.reset(); solver.add(original); solver.set(parameters);
        spdlog::debug("[assertion-query original-fallback; timeout={}ms]\n{}\n[end assertion-query]",
                      timeout_ms, solver.to_smt2());
        result = solver.check();
    }
    if (result == z3::unknown) {
        result = quantified_unsat_fallback(solver, timeout_ms);
        spdlog::debug("[quantified-query] precise simplify/qe/smt fallback: {}",
                      result == z3::unsat ? "unsat" : "inconclusive");
    }
    return result;
}

void
Engine::run(state_ptr state) {
    auto activation = session->activate();
    // set the default entry point if not set
    set_default_entry();

    // Rebuild at each exact top-level run, including after callers edit IR
    // between verify() invocations. Speculative nested runs share the index.
    if (!coarse_proof_active && state->pc && state->pc->inst)
        property_reachability.rebuild(*state->pc->inst->getModule());

    states.push(state);
    const bool safety_trial = coarse_proof_active && state->safety_resume_state;
    const auto proof_started = std::chrono::steady_clock::now();
    std::size_t refinement_steps = 0;
    std::size_t proof_steps = 0;
    std::size_t proof_limit = 2048;
    if (const char* raw = std::getenv("ARITHEXE_COARSE_PROOF_STEPS")) {
        char* end = nullptr;
        const auto parsed = std::strtoull(raw, &end, 10);
        if (raw[0] && raw[0] != '-' && end && !*end) proof_limit = parsed;
    }
    // Optional degree-zero safety probes must yield to the saved exact trial.
    // The elapsed budget is checked between operations; an in-flight solver
    // query remains governed by its own timeout.
    if (safety_trial) proof_limit = std::min(proof_limit, std::size_t{64});
    std::size_t refinement_limit = 512;
    if (const char* raw = std::getenv("ARITHEXE_COARSE_REFINE_STEPS")) {
        char* end = nullptr;
        const auto parsed = std::strtoull(raw, &end, 10);
        if (raw[0] && raw[0] != '-' && end && !*end) {
            refinement_limit = parsed;
        }
    }
    while (!states.empty()) {
        auto cur_state = states.top();
        states.pop();
        if (coarse_proof_active && !cur_state->coarse_boundary_states.empty()) {
            // Expand every obligation, including summaries of later loops
            // reached during this transaction. No partial successful exit
            // can justify discarding an unsafe sibling or the exact entry.
            for (const auto& boundary : cur_state->coarse_boundary_states)
                states.push(boundary);
            cur_state = std::make_shared<State>(*cur_state);
            cur_state->coarse_boundary_states.clear();
        }
        if (can_prune_property_irrelevant(cur_state)) {
            // No future call/assertion can violate unreach-call safety, even
            // if this execution diverges. TESTING states need no SAT query
            // when neither a feasible nor an infeasible branch can err.
            ++property_pruned_states;
            spdlog::debug("[property-prune] stopping error-free suffix at {} in {}",
                         cur_state->pc->get_block()->getName().str(),
                         cur_state->pc->inst->getFunction()->getName().str());
            capture_loop_certificates(cur_state);
            capture_function_certificates(cur_state);
            results.push_back(HOLD);
            continue;
        }
        if (!coarse_proof_active && cur_state->coarse_fallback_state) {
            auto exact_entry = cur_state->coarse_fallback_state;
            auto* coarse_loop = cur_state->coarse_loop;
            const unsigned first_degree = cur_state->coarse_degree;
            bool proved = prove_coarse_independently(cur_state);
            if (!proved && cur_state->safety_resume_state) {
                // The optional safety-first trial must not replace a usable
                // exact jump with coarse replay. Resume at the same pre-PHI
                // point, with the trial marked attempted, then check coverage.
                auto& resume = cur_state->safety_resume_state;
                resume->preserve_loop_entry_history = coarse_loop &&
                    resume->pc->get_block() == coarse_loop->getHeader() &&
                    !resume->trace.empty() && !coarse_loop->contains(resume->trace.back());
                states.push(cur_state->safety_resume_state);
                spdlog::debug("[safety-exit] inconclusive; resuming exact trial and residual coverage");
                continue;
            }
            if (!proved) {
                // Retry from the unchanged entry, not the failed summary's
                // exit state or its speculative assumptions/results.
                for (const unsigned degree : PathExpressionAccelerator::coarse_degrees()) {
                    if (degree <= first_degree) continue;
                    session->note_coarse_summary_attempt(degree);
                    spdlog::debug("[coarse-path] property not discharged at degree {}; "
                                  "retrying entry at degree {}", first_degree, degree);
                    try {
                        if (auto retry = PathExpressionAccelerator::accelerate_coarse(
                                coarse_loop, exact_entry, degree)) {
                            proved = prove_coarse_independently(retry);
                            if (proved) break;
                        }
                    } catch (const std::exception& error) {
                        spdlog::debug("[coarse-path] degree {} retry failed: {}",
                                      degree, error.what());
                    }
                }
            }
            if (!proved) {
                session->note_coarse_refinement();
                if (!refinement_limit) {
                    record_issue(VerifierIssueKind::OverApproximation,
                                 "independent coarse proof was inconclusive; "
                                 "precise replay is disabled");
                    results.push_back(VERIUNKNOWN);
                    return;
                }
                auto ordinary = std::make_shared<State>(*exact_entry);
                ordinary->coarse_fallback_state.reset();
                ordinary->coarse_loop = nullptr;
                ordinary->coarse_degree = 0;
                ordinary->coarse_refining = true;
                ordinary->execute_phi_bundle();
                states.push(ordinary);
                spdlog::debug("[coarse-path] resuming separate precise entry "
                              "continuation without coarse constraints");
            }
            continue;
        }
        if (coarse_proof_active && (proof_steps++ >= proof_limit ||
            (safety_trial && std::chrono::steady_clock::now() - proof_started >=
                                 std::chrono::milliseconds(250)))) {
            spdlog::debug("[coarse-path] independent proof budget exhausted; returning inconclusive");
            results.push_back(VERIUNKNOWN);
            return;
        }
        spdlog::debug("Current Instruction: {}", cur_state->pc->inst->getName().str());
        // llvm::errs() << *cur_state->pc->inst << "\n";
        // llvm::errs() << cur_state->memory.to_string() << "\n";

        if (cur_state->status == State::TERMINATED) {
            capture_loop_certificates(cur_state);
            capture_function_certificates(cur_state);
            continue;
        } else if (cur_state->status == State::VERIFYING) {
            VeriResult res;
            try {
                res = verify(cur_state);
            } catch (const z3::exception& error) {
                throw std::runtime_error("while checking an assertion: " +
                                         std::string(error.msg()));
            }
            // early terminate if unsafe path is found
            results.push_back(res);
            if (coarse_proof_active && res != HOLD) return;
            // llvm::errs() << cur_state->memory.to_string() << "\n";
            // llvm::errs() << "Verification condition: " << cur_state->verification_condition.to_string() << "\n";
            if (res == FAIL) {
                violation_instruction = cur_state->prev_pc
                                            ? cur_state->prev_pc->inst
                                            : cur_state->pc->inst;
                return;
            }
            try {
                cur_state->State::append_path_condition(
                    cur_state->verification_condition);
            } catch (const z3::exception& error) {
                throw std::runtime_error(
                    "while recording a verified assertion: " +
                    std::string(error.msg()));
            }
            cur_state->status = State::RUNNING;
            states.push(cur_state);
            continue;
        } else if (cur_state->status == State::REACH_ERROR) {
            auto res = test(cur_state);
            if (res == FEASIBLE) {
                if (cur_state->is_over_approx ||
                    !cur_state->counterexample_complete) {
                    record_issue(
                        cur_state->is_over_approx
                            ? VerifierIssueKind::OverApproximation
                            : VerifierIssueKind::IncompleteCounterexample,
                        "feasible error path depends on an imprecise summary");
                    results.push_back(VERIUNKNOWN);
                    return;
                }
                results.push_back(FAIL);
                violation_instruction = cur_state->pc->inst;
                capture_counterexample(cur_state, solver.get_model());
                return;
            } else if (res == TESTUNKNOWN) {
                record_issue(VerifierIssueKind::Z3Unknown,
                             "Z3 returned unknown while checking reach_error feasibility");
                results.push_back(VERIUNKNOWN);
                return;
            }
        } else if (cur_state->status == State::TESTING) {
            auto res = test(cur_state);
            if (res != UNFEASIBLE) {
                // UNKNOWN is not evidence of infeasibility or unsafety. Keep
                // the complete path constraints and explore this successor.
                if (res == TESTUNKNOWN) {
                    cur_state->feasibility_uncertain = true;
                    spdlog::debug("[path-query] unknown feasibility; retaining constrained successor");
                }
                cur_state->status = State::RUNNING;
                states.push(cur_state);
            }
            continue;
        } else if (cur_state->status == State::UNKNOWN) {
            // if the state is unknown, we should not continue
            record_issue(VerifierIssueKind::UnknownState,
                         "symbolic execution reached an unknown state");
            results.push_back(VERIUNKNOWN);
            return;
        } else if (cur_state->status == State::FAIL) {
            if (cur_state->is_over_approx || !cur_state->counterexample_complete) {
                record_issue(VerifierIssueKind::IncompleteCounterexample,
                             "counterexample path is incomplete");
                results.push_back(VERIUNKNOWN);
                return;
            }
            results.push_back(FAIL);
            violation_instruction = cur_state->pc->inst;
            return;
        }
        assert(cur_state->status == State::RUNNING);
        if (cur_state->coarse_refining && refinement_steps++ >= refinement_limit) {
            record_issue(VerifierIssueKind::OverApproximation,
                         "coarse summary was inconclusive; ordinary refinement "
                         "exhausted ARITHEXE_COARSE_REFINE_STEPS");
            results.push_back(VERIUNKNOWN);
            return;
        }
        std::vector<state_ptr> new_states;
        try {
            new_states = step(cur_state);
        } catch (const z3::exception& error) {
            throw std::runtime_error(
                "while executing LLVM " +
                std::string(cur_state->pc->inst->getOpcodeName()) + ": " +
                error.msg());
        } catch (const VerifierError&) {
            throw;
        } catch (const std::exception& error) {
            throw std::runtime_error(
                "while executing LLVM " +
                std::string(cur_state->pc->inst->getOpcodeName()) + ": " +
                error.what());
        }
        for (auto& new_state : new_states) {
            if (coarse_proof_active && cur_state->coarse_replay_loop &&
                llvm::isa<llvm::BranchInst>(cur_state->pc->inst)) {
                auto* region = cur_state->coarse_replay_loop;
                auto* target = new_state->pc->get_block();
                if (region->contains(target) &&
                    (cur_state->coarse_replay_one_block ||
                     target == region->getHeader())) {
                    // This edge's update is in the certified union-star.
                    // Assertions before it have already been checked. Do not
                    // unfold another iteration/edge and lose the finite proof.
                    spdlog::debug("[coarse-path] certified boundary return {} -> {}",
                        cur_state->pc->get_block()->getName().str(),
                        target->getName().str());
                    continue;
                }
                if (!region->contains(target)) {
                    spdlog::debug("[coarse-path] replaying guarded exit {} -> {}",
                        cur_state->pc->get_block()->getName().str(),
                        target->getName().str());
                    new_state->coarse_replay_loop = nullptr;
                    new_state->coarse_replay_one_block = false;
                    // The boundary is an internal arithmetic probe; code
                    // after an exit returns to ordinary execution semantics.
                    new_state->summary_arithmetic = false;
                }
            }
            states.push(new_state);
        }
    }
}

void
Engine::record_issue(VerifierIssueKind kind, const std::string& message) {
    if (issue_recorded) return;
    issue_recorded = true;
    issue_kind = kind;
    issue_message = message;
}

bool
Engine::reach_loop(state_ptr state) {
    auto pc = state->pc;
    auto cur_block = pc->get_block();
    auto func = cur_block->getParent();
    auto& LI = AnalysisManager::get_instance()->get_LI(func);
    auto loop = LI.getLoopFor(cur_block);
    if (loop == nullptr) return false;
    return cur_block == loop->getHeader() && pc->inst == &*cur_block->begin();
}

void
Engine::run() {
    auto activation = session->activate();
    // set the default entry point if not set
    llvm::errs() << "Running the engine...\n";
    set_default_entry();
    llvm::errs() << "Entry point: " << entry->getName() << "\n";
    // A guard-sensitive coarse proof is independent of exact path histories.
    // Only a complete safe fixed point can bypass the ordinary worklist.
    if (prove_guarded_residues(entry, *session)) {
        results.push_back(HOLD);
        return;
    }
    state_ptr initial_state = build_initial_state();
    llvm::errs() << "Initial Instruction: " << *initial_state->pc->inst << "\n";
    return run(initial_state);
}

VeriResult
Engine::verify(state_ptr state) {
    auto activation = session->activate();
    z3::expr path_condition(z3ctx);
    z3::expr assertion_defined(z3ctx);
    z3::expr assertion(z3ctx);
    try {
        path_condition = state->get_path_condition().as_expr();
    } catch (const z3::exception& error) {
        throw std::runtime_error("failed to assemble assertion path: " +
                                 std::string(error.msg()));
    }
    try {
        assertion_defined = state->verification_condition.defined();
    } catch (const z3::exception& error) {
        throw std::runtime_error("failed to assemble assertion definedness: " +
                                 std::string(error.msg()));
    }
    try {
        assertion = state->verification_condition.as_expr();
    } catch (const z3::exception& error) {
        throw std::runtime_error("failed to assemble verification condition: " +
                                 std::string(error.msg()));
    }
    if (!path_condition.is_bool()) {
        throw std::runtime_error("assertion path condition is not Boolean");
    }
    if (!assertion_defined.is_bool()) {
        throw std::runtime_error("assertion definedness is not Boolean");
    }
    if (!assertion.is_bool()) {
        throw std::runtime_error("verification condition is not Boolean");
    }
    z3::expr_vector assumptions(z3ctx);
    assumptions.push_back(path_condition);
    assumptions.push_back(assertion_defined);
    assumptions.push_back(!assertion);
    // llvm::errs() << state->get_path_condition().as_expr().to_string() << "\n";
    // llvm::errs() << assumptions.to_string() << "\n";
    // Each program assertion is an independent query. Reusing Z3's
    // incremental state here can make a later nonlinear query dramatically
    // slower after an earlier UNSAT result.
    solver.reset();
    const unsigned query_timeout = configure_query_timeout(session->path_acceleration_count() > 0);
    z3::check_result res = z3::unknown;
    const bool use_coarse_lemmas = coarse_proof_active && state->coarse_degree != 0;
    try {
        // Keep candidate generation on the original syntax: quotient witnesses
        // are implementation details, not program inputs to pin in candidates.
        if (!use_coarse_lemmas && query_timeout) solver.add(assumptions);
        else solver.add(integerize_rational_query(z3::mk_and(assumptions), false));
        res = use_coarse_lemmas
            ? session->check_coarse_query(solver, query_timeout)
            : query_timeout ? check_precise_query(query_timeout, true) : solver.check();
    } catch (const z3::exception& error) {
        throw std::runtime_error("Z3 assertion query failed: " +
                                 std::string(error.msg()));
    }
    VeriResult result;
    switch (res) {
        case z3::unsat:
            result = HOLD;
            break;
        case z3::sat:
            if (state->is_over_approx || !state->counterexample_complete) {
                record_issue(
                    state->is_over_approx
                        ? VerifierIssueKind::OverApproximation
                        : VerifierIssueKind::IncompleteCounterexample,
                    "verification condition may fail, but the path is imprecise");
                result = VERIUNKNOWN;
                break;
            }
            llvm::errs() << solver.get_model().to_string() << "\n";
            capture_counterexample(state, solver.get_model());
            result = FAIL;
            break;
        case z3::unknown:
            record_issue(VerifierIssueKind::Z3Unknown,
                         "Z3 returned unknown while proving a verification condition");
            result = VERIUNKNOWN;
            break;
    }
    return result;
}

void
Engine::capture_counterexample(state_ptr state, const z3::model& model) {
    counterexample_inputs.clear();
    for (const NondetCall& call : state->nondet_calls) {
        std::vector<z3::expr> values;
        if (call.value) {
            values.push_back(model.eval(*call.value, true).simplify());
        } else if ((call.values || call.sequence) && call.count) {
            z3::expr evaluated_count = model.eval(*call.count, true).simplify();
            int64_t count = 0;
            if (!evaluated_count.is_numeral_i64(count) || count < 0) continue;
            for (int64_t index = 0; index < count; ++index) {
                z3::expr argument = z3ctx.int_val(std::to_string(index).c_str());
                const z3::expr value = call.sequence
                    ? z3::select(*call.sequence, argument)
                    : (*call.values)(argument);
                values.push_back(model.eval(value, true).simplify());
            }
        }

        for (const z3::expr& value : values) {
            std::string rendered = value.to_string();
            int64_t integer_value = 0;
            if (value.is_numeral_i64(integer_value)) {
                rendered = std::to_string(integer_value);
            } else if (value.is_true()) {
                rendered = "1";
            } else if (value.is_false()) {
                rendered = "0";
            }
            counterexample_inputs.push_back({call.instruction, rendered});
        }
    }
}

void
Engine::capture_loop_certificates(state_ptr state) {
    for (const LoopCertificate& certificate : state->loop_certificates) {
        const bool already_recorded = std::any_of(
            loop_certificates.begin(), loop_certificates.end(),
            [&](const LoopCertificate& existing) {
                return existing.loop == certificate.loop;
            });
        if (!already_recorded) loop_certificates.push_back(certificate);
    }
}

void
Engine::capture_function_certificates(state_ptr state) {
    for (const FunctionCertificate& certificate :
         state->function_certificates) {
        const bool already_recorded = std::any_of(
            function_certificates.begin(), function_certificates.end(),
            [&](const FunctionCertificate& existing) {
                return existing.function == certificate.function;
            });
        if (!already_recorded) {
            function_certificates.push_back(certificate);
        }
    }
}

TestResult
Engine::test(state_ptr state) {
    auto activation = session->activate();
    z3::expr_vector assumptions(z3ctx);
    assumptions.push_back(state->get_path_condition().as_expr());
    solver.reset();
    const unsigned query_timeout = configure_query_timeout(session->path_acceleration_count() > 0);
    solver.add(assumptions);
    // Degree-zero havoc/exit-guard summaries need no polynomial lemmas.
    // Avoid spending the lemma retry budget on ordinary linear guards.
    auto res = coarse_proof_active && state->coarse_degree != 0
        ? session->check_coarse_query(solver, query_timeout)
        : query_timeout ? check_precise_query(query_timeout) : solver.check();
    TestResult result;
    switch (res) {
        case z3::unsat:
            result = UNFEASIBLE;
            break;
        case z3::sat:
            result = FEASIBLE;
            break;
        case z3::unknown:
            // The caller decides whether this is an error-reachability
            // obligation or merely a branch whose exploration must continue.
            result = TESTUNKNOWN;
            break;
    }
    return result;
}

state_ptr
Engine::build_initial_state() {
    auto activation = session->activate();
    // build the initial state
    // this state should record global variables
    Memory memory;
    Expression path_condition = z3ctx.bool_val(true);
    for (auto& gv : mod->globals()) {
        auto obj = memory.add_global(gv);
        path_condition = path_condition && obj->get_constraints();
    }

    auto pc = &*entry->getEntryBlock().getFirstNonPHIOrDbg();
    memory.push_frame(mod->getFunction(ari_exe::default_entry_function_name));
    for (auto& arg : entry->args()) {
        auto name = "ari_" + arg.getName().str();
        auto arg_value = z3ctx.int_const(name.c_str());
        // memory.allocate(&arg, arg_value);
        // auto obj = memory.allocate(&arg, z3::expr_vector(z3ctx));
        // obj->write(arg_value);
        memory.put_temp(&arg, arg_value);
    }
    auto initial_state = std::make_shared<State>(State(*session, z3ctx, AInstruction::create(pc), nullptr,
                               memory, path_condition, {}));
    return initial_state;
}

void Engine::set_default_entry() {
    if (entry == nullptr) {
        entry = mod->getFunction(ari_exe::default_entry_function_name);
    }
}
