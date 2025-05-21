#include "engine.h"

#include <fstream>
#include <sstream>
#include <algorithm>

#include <spdlog/spdlog.h>

using namespace ari_exe;

Engine::Engine(): mod(nullptr), solver(z3ctx) {}

Engine::Engine(const std::string& c_filename): mod(nullptr), solver(z3ctx) {
    auto manager = AnalysisManager::get_instance();
    assert(manager->get_z3ctx() == z3ctx && "The z3 context is not the same as the analysis manager");
    mod = manager->get_module(c_filename, z3ctx);
}

Engine::VeriResult
Engine::verify() {
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
        auto cur_state = states.front();
        states.pop();

        if (cur_state->status == State::TERMINATED) {
            continue;
        } else if (cur_state->status == State::VERIFYING) {
            auto res = verify(cur_state);
            results.push_back(res);
            cur_state->append_path_condition(cur_state->verification_condition);
            // cur_state->path_condition = cur_state->path_condition
            //                           && cur_state->verification_condition;
            cur_state->status = State::RUNNING;
            states.push(cur_state);
            continue;
        } else if (cur_state->status == State::TESTING) {
            auto res = test(cur_state);
            if (res == FEASIBLE) {
                cur_state->status = State::RUNNING;
                states.push(cur_state);
            }
            // TODO: what to do with unknown path?
            continue;
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

Engine::VeriResult
Engine::verify(state_ptr state) {
    z3::expr_vector assumptions(z3ctx);
    assumptions.push_back(state->get_path_condition());
    assumptions.push_back(!state->verification_condition);
    auto res = solver.check(assumptions);
    Engine::VeriResult result;
    switch (res) {
        case z3::unsat:
            result = HOLD;
            break;
        case z3::sat:
            result = FAIL;
            break;
        case z3::unknown:
            result = VERIUNKNOWN;
            break;
    }
    return result;
}

Engine::TestResult
Engine::test(state_ptr state) {
    z3::expr_vector assumptions(z3ctx);
    assumptions.push_back(state->get_path_condition());
    auto res = solver.check(assumptions);
    Engine::TestResult result;
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
    SymbolTable<z3::expr> globals;
    for (auto& gv : mod->globals()) {
        llvm::Type* value_type = gv.getValueType();
        auto initial_value = gv.getInitializer();
        z3::expr value(z3ctx);
        if (value_type->isArrayTy()) {
            // implement for 1-d array first
            // TODO: implement for multi-d array
            assert(value_type->getArrayElementType()->isIntegerTy());
            if (auto zero = dyn_cast_or_null<llvm::ConstantAggregateZero>(initial_value)) {
                value = z3::const_array(z3ctx.int_sort(), z3ctx.int_val(0));
            } else if (auto array = dyn_cast_or_null<llvm::ConstantDataArray>(initial_value)) {
                if (array->isCString()) {
                    value = z3ctx.string_val(array->getAsCString().str());
                } else {
                    // only initialize the first initialized elemenets,
                    // not sure if need to zero fill the rest
                    auto arr_sort = z3ctx.array_sort(z3ctx.int_sort(), z3ctx.int_sort());
                    value = z3ctx.constant(gv.getName().str().c_str(), arr_sort);
                    for (int i = 0; i < array->getNumElements(); i++) {
                        // TODO: support other types
                        auto element = array->getElementAsInteger(i);
                        value = z3::store(value, z3ctx.int_val(i), z3ctx.int_val(element));
                    }
                }
            } else {
                llvm::errs() << "Unsupported global variable initializer type\n";
            }
        }
        globals.insert_or_assign(&gv, value);
    }

    auto pc = &*entry->getEntryBlock().begin();
    auto stack = AStack();
    stack.push_frame();
    for (auto& arg : entry->args()) {
        auto name = "ari_" + arg.getName().str();
        auto arg_value = z3ctx.int_const(name.c_str());
        stack.insert_or_assign_value(&arg, arg_value);
    }
    auto initial_state = std::make_shared<State>(State(z3ctx, AInstruction::create(pc), nullptr,
                               globals, stack, z3ctx.bool_val(true), {}));
    return initial_state;
}

void Engine::set_default_entry() {
    if (entry == nullptr) {
        entry = mod->getFunction(ari_exe::default_entry_function_name);
    }
}
