#include "engine.h"

#include <fstream>
#include <sstream>
#include <algorithm>

#include <spdlog/spdlog.h>

using namespace ari_exe;

Engine::Engine(): mod(nullptr), solver(z3ctx) {}

Engine::Engine(const std::string& c_filename): mod(nullptr), solver(z3ctx) {
    auto manager = AnalysisManager::get_instance();
    assert(&manager->get_z3ctx() == &z3ctx && "The z3 context is not the same as the analysis manager");
    mod = manager->get_module(c_filename, z3ctx);
}

VeriResult
Engine::verify() {
    results.clear();
    violation_instruction = nullptr;
    counterexample_inputs.clear();
    loop_certificates.clear();
    function_certificates.clear();
    run();
    auto res = VERIUNKNOWN;
    if (std::all_of(results.begin(), results.end(), [](VeriResult veri_res) { return veri_res == HOLD; })) {
        res = HOLD;
    } else if (std::any_of(results.begin(), results.end(), [](VeriResult veri_res) { return veri_res == FAIL; })) {
        res = FAIL;
    }
    return res;
}

std::vector<state_ptr>
Engine::step(state_ptr state) {
    auto pc = state->pc;
    return pc->execute(state);
}

void
Engine::set_entry(llvm::Function* entry) {
    this->entry = entry;
}

void
Engine::run(state_ptr state) {
    // set the default entry point if not set
    set_default_entry();

    states.push(state);
    while (!states.empty()) {
        auto cur_state = states.top();
        states.pop();
        spdlog::debug("Current Instruction: {}", cur_state->pc->inst->getName().str());
        // llvm::errs() << *cur_state->pc->inst << "\n";
        // llvm::errs() << cur_state->memory.to_string() << "\n";

        if (cur_state->status == State::TERMINATED) {
            capture_loop_certificates(cur_state);
            capture_function_certificates(cur_state);
            continue;
        } else if (cur_state->status == State::VERIFYING) {
            auto res = verify(cur_state);
            // early terminate if unsafe path is found
            results.push_back(res);
            // llvm::errs() << cur_state->memory.to_string() << "\n";
            // llvm::errs() << "Verification condition: " << cur_state->verification_condition.to_string() << "\n";
            if (res == FAIL) {
                violation_instruction = cur_state->prev_pc
                                            ? cur_state->prev_pc->inst
                                            : cur_state->pc->inst;
                return;
            }
            cur_state->append_path_condition(cur_state->verification_condition);
            cur_state->status = State::RUNNING;
            states.push(cur_state);
            continue;
        } else if (cur_state->status == State::REACH_ERROR) {
            auto res = test(cur_state);
            if (res == FEASIBLE) {
                if (cur_state->is_over_approx ||
                    !cur_state->counterexample_complete) {
                    results.push_back(VERIUNKNOWN);
                    return;
                }
                results.push_back(FAIL);
                violation_instruction = cur_state->pc->inst;
                capture_counterexample(cur_state, solver.get_model());
                return;
            } else if (res == TESTUNKNOWN) {
                results.push_back(VERIUNKNOWN);
                return;
            }
        } else if (cur_state->status == State::TESTING) {
            auto res = test(cur_state);
            if (res == FEASIBLE) {
                cur_state->status = State::RUNNING;
                states.push(cur_state);
            }
            // TODO: what to do with unknown path?
            continue;
        } else if (cur_state->status == State::UNKNOWN) {
            // if the state is unknown, we should not continue
            results.push_back(VERIUNKNOWN);
            return;
        } else if (cur_state->status == State::FAIL) {
            if (!cur_state->counterexample_complete) {
                results.push_back(VERIUNKNOWN);
                return;
            }
            results.push_back(FAIL);
            violation_instruction = cur_state->pc->inst;
            return;
        }
        assert(cur_state->status == State::RUNNING);
        auto new_states = step(cur_state);
        for (auto& new_state : new_states) states.push(new_state);
    }
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
    // set the default entry point if not set
    llvm::errs() << "Running the engine...\n";
    set_default_entry();
    llvm::errs() << "Entry point: " << entry->getName() << "\n";
    state_ptr initial_state = build_initial_state();
    llvm::errs() << "Initial Instruction: " << *initial_state->pc->inst << "\n";
    return run(initial_state);
}

VeriResult
Engine::verify(state_ptr state) {
    z3::expr_vector assumptions(z3ctx);
    assumptions.push_back(state->get_path_condition().as_expr());
    assumptions.push_back(!state->verification_condition.as_expr());
    // llvm::errs() << state->get_path_condition().as_expr().to_string() << "\n";
    // llvm::errs() << assumptions.to_string() << "\n";
    auto res = solver.check(assumptions);
    VeriResult result;
    switch (res) {
        case z3::unsat:
            result = HOLD;
            break;
        case z3::sat:
            if (state->is_over_approx || !state->counterexample_complete) {
                result = VERIUNKNOWN;
                break;
            }
            llvm::errs() << solver.get_model().to_string() << "\n";
            capture_counterexample(state, solver.get_model());
            result = FAIL;
            break;
        case z3::unknown:
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
        } else if (call.values && call.count) {
            z3::expr evaluated_count = model.eval(*call.count, true).simplify();
            int64_t count = 0;
            if (!evaluated_count.is_numeral_i64(count) || count < 0) continue;
            for (int64_t index = 0; index < count; ++index) {
                z3::expr argument = z3ctx.int_val(std::to_string(index).c_str());
                values.push_back(
                    model.eval((*call.values)(argument), true).simplify());
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
    z3::expr_vector assumptions(z3ctx);
    assumptions.push_back(state->get_path_condition().as_expr());
    auto res = solver.check(assumptions);
    TestResult result;
    switch (res) {
        case z3::unsat:
            result = UNFEASIBLE;
            break;
        case z3::sat:
            result = FEASIBLE;
            break;
        case z3::unknown:
            result = TESTUNKNOWN;
            break;
    }
    return result;
}

state_ptr
Engine::build_initial_state() {
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
    auto initial_state = std::make_shared<State>(State(z3ctx, AInstruction::create(pc), nullptr,
                               memory, path_condition, {}));
    return initial_state;
}

void Engine::set_default_entry() {
    if (entry == nullptr) {
        entry = mod->getFunction(ari_exe::default_entry_function_name);
    }
}
