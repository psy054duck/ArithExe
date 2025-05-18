#include "engine.h"
#include "SymbolTable.h"

#include <fstream>
#include <sstream>
#include <algorithm>

#include <spdlog/spdlog.h>

using namespace ari_exe;

Engine::Engine(z3::context& z3ctx): mod(nullptr), z3ctx(z3ctx), solver(z3ctx) {}

Engine::Engine(std::unique_ptr<llvm::Module>& mod, z3::context& z3ctx): mod(std::move(mod)), z3ctx(z3ctx), solver(z3ctx) {
    analyze_module();
}

Engine::Engine(const std::string& c_filename, z3::context& z3ctx): mod(nullptr), z3ctx(z3ctx), solver(z3ctx) {
    std::string ir_content = generateLLVMIR(c_filename);
    mod = parseLLVMIR(ir_content, context);
    analyze_module();
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

std::string
Engine::generateLLVMIR(const std::string& c_filename) {
    std::ostringstream clang_cmd;
    clang_cmd << "clang -emit-llvm -S -O0 -Xclang -disable-O0-optnone "
              << c_filename << " -o -";
    FILE* pipe = popen(clang_cmd.str().c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to run clang");
    }

    std::string ir_content;
    char buffer[8192];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        ir_content += buffer;
    }
    int clang_ret = pclose(pipe);
    if (clang_ret != 0) {
        throw std::runtime_error("Fail to convert " + c_filename + " into LLVM IR");
    }
    return ir_content;
}

std::unique_ptr<llvm::Module>
Engine::parseLLVMIR(const std::string& ir_content, llvm::LLVMContext& context) {
    llvm::SMDiagnostic err;
    auto memBuffer = llvm::MemoryBuffer::getMemBuffer(ir_content, "in-memory.ll");
    auto module = llvm::parseIR(memBuffer->getMemBufferRef(), err, context);
    if (!module) {
        err.print("driver", llvm::errs());
        throw std::runtime_error("Failed to parse LLVM IR");
    }
    return module;
}

std::vector<std::shared_ptr<State>>
Engine::step(std::shared_ptr<State> state) {
    auto pc = state->pc;
    return pc->execute(state);
}

void
Engine::set_entry(llvm::Function* entry) {
    this->entry = entry;
}

void
Engine::run(std::shared_ptr<State> state) {
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
            cur_state->path_condition = cur_state->path_condition
                                      && cur_state->verification_condition;
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
        } else if (cur_state->status == State::RUNNING) {
            auto new_states = step(cur_state);
            for (auto& new_state : new_states) states.push(new_state);
        }
    }
}

void
Engine::run() {
    // set the default entry point if not set
    llvm::errs() << "Running the engine...\n";
    set_default_entry();
    llvm::errs() << "Entry point: " << entry->getName() << "\n";
    std::shared_ptr<State> initial_state = build_initial_state();
    llvm::errs() << "Initial Instruction: " << *initial_state->pc->inst << "\n";
    return run(initial_state);
}

Engine::VeriResult
Engine::verify(std::shared_ptr<State> state) {
    z3::expr_vector assumptions(z3ctx);
    assumptions.push_back(state->path_condition);
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
Engine::test(std::shared_ptr<State> state) {
    z3::expr_vector assumptions(z3ctx);
    assumptions.push_back(state->path_condition);
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

std::shared_ptr<State>
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


void Engine::analyze_module() {
    // record the original module
    // std::error_code ec;
    // llvm::raw_fd_ostream before_fd("tmp/before.ll", ec);
    // mod->print(before_fd, NULL);
    // before_fd.close();

    LAM = llvm::LoopAnalysisManager();
    FAM = llvm::FunctionAnalysisManager();
    CGAM = llvm::CGSCCAnalysisManager();
    MAM = llvm::ModuleAnalysisManager();
    PB = llvm::PassBuilder();
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    MPM.addPass(llvm::ModuleInlinerPass());
    MPM.addPass(createModuleToFunctionPassAdaptor(llvm::LowerSwitchPass()));
    MPM.addPass(createModuleToFunctionPassAdaptor(llvm::PromotePass()));
    MPM.addPass(createModuleToFunctionPassAdaptor(llvm::SROAPass(llvm::SROAOptions::ModifyCFG)));
    MPM.addPass(createModuleToFunctionPassAdaptor(llvm::LoopSimplifyPass()));
    MPM.addPass(createModuleToFunctionPassAdaptor(llvm::LCSSAPass()));
    MPM.addPass(createModuleToFunctionPassAdaptor(llvm::SimplifyCFGPass()));

    // MPM.addPass(createModuleToFunctionPassAdaptor(createFunctionToLoopPassAdaptor(LoopRotatePass())));
    // MPM.addPass(createModuleToFunctionPassAdaptor(LoopFusePass()));
    // MPM.addPass(createModuleToFunctionPassAdaptor(createFunctionToLoopPassAdaptor(IndVarSimplifyPass())));
    MPM.addPass(createModuleToFunctionPassAdaptor(llvm::SCCPPass()));
    MPM.addPass(createModuleToFunctionPassAdaptor(llvm::GVNPass()));
    MPM.addPass(createModuleToFunctionPassAdaptor(llvm::DCEPass()));
    MPM.addPass(createModuleToFunctionPassAdaptor(llvm::InstructionNamerPass()));
    MPM.addPass(createModuleToFunctionPassAdaptor(llvm::AggressiveInstCombinePass()));
    // MPM.addPass(createModuleToFunctionPassAdaptor(RegToMemPass()));
    // MPM.addPass(createModuleToFunctionPassAdaptor(MemorySSAPrinterPass(output_fd, true)));
    // MPM.addPass(createModuleToFunctionPassAdaptor(MemorySSAWrapperPass()));

    MPM.run(*mod, MAM);
    auto &fam = MAM.getResult<llvm::FunctionAnalysisManagerModuleProxy>(*mod).getManager();
    for (auto F = mod->begin(); F != mod->end(); F++) {
        if (!F->isDeclaration()) {
            llvm::LoopInfo& LI = fam.getResult<llvm::LoopAnalysis>(*F);
            // llvm::MemorySSA& MSSA = fam.getResult<llvm::MemorySSAAnalysis>(*F).getMSSA();
            llvm::DominatorTree DT = llvm::DominatorTree(*F);
            llvm::PostDominatorTree PDT = llvm::PostDominatorTree(*F);
            // LIs[&*F] = LI;
            LIs.emplace(&*F, LI);
            DTs.emplace(&*F, llvm::DominatorTree(*F));
            PDTs.emplace(&*F, llvm::PostDominatorTree(*F));
            // MSSAs.emplace(&*F, MSSA);
        }
    }

    std::string ir_str;
    llvm::raw_string_ostream rso(ir_str);
    mod->print(rso, nullptr);
    spdlog::info("The IR that being analyzed:\n{}", ir_str);
}
