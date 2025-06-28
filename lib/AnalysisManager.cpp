#include "AnalysisManager.h"
#include <spdlog/spdlog.h>

using namespace ari_exe;

AnalysisManager* AnalysisManager::instance = new AnalysisManager();

// z3::context AnalysisManager::z3ctx;

int AnalysisManager::unknown_counter = 0;

std::unique_ptr<llvm::Module>
AnalysisManager::get_module(const std::string& c_filename, z3::context& z3ctx) {
    auto ir_content = generateLLVMIR(c_filename);
    auto mod = parseLLVMIR(ir_content, context);
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

    llvm::CGSCCPassManager CGPM;
    CGPM.addPass(llvm::ArgumentPromotionPass());
    // MPM.addPass(createModuleToFunctionPassAdaptor(llvm::TailCallElimPass()));
    MPM.addPass(llvm::createModuleToPostOrderCGSCCPassAdaptor(std::move(CGPM)));

    MPM.addPass(createModuleToFunctionPassAdaptor(llvm::LowerSwitchPass()));
    // MPM.addPass(createModuleToFunctionPassAdaptor(llvm::PromotePass()));
    MPM.addPass(createModuleToFunctionPassAdaptor(llvm::RegToMemPass()));
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
    MPM.addPass(createModuleToFunctionPassAdaptor(llvm::LowerSwitchPass()));
    // MPM.addPass(createModuleToFunctionPassAdaptor(llvm::TailCallElimPass()));
    // MPM.addPass(createModuleToFunctionPassAdaptor(llvm::PromotePass()));
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
            llvm::DependenceInfo& DI = fam.getResult<llvm::DependenceAnalysis>(*F);
            // LIs[&*F] = LI;
            LIs.emplace(&*F, LI);
            DTs.emplace(&*F, llvm::DominatorTree(*F));
            PDTs.emplace(&*F, llvm::PostDominatorTree(*F));
            DIs.emplace(&*F, DI);
            // MSSAs.emplace(&*F, MSSA);
        }
    }

    CG = &MAM.getResult<llvm::CallGraphAnalysis>(*mod);
    std::string ir_str;
    llvm::raw_string_ostream rso(ir_str);
    mod->print(rso, nullptr);
    spdlog::info("The IR that being analyzed:\n{}", ir_str);
    return mod;
}

std::string
AnalysisManager::generateLLVMIR(const std::string& c_filename) {
    std::ostringstream clang_cmd;
    clang_cmd << "clang -emit-llvm -g -S -O0 -Xclang -disable-O0-optnone "
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
AnalysisManager::parseLLVMIR(const std::string& ir_content, llvm::LLVMContext& context) {
    llvm::SMDiagnostic err;
    auto memBuffer = llvm::MemoryBuffer::getMemBuffer(ir_content, "in-memory.ll");
    auto module = llvm::parseIR(memBuffer->getMemBufferRef(), err, context);
    if (!module) {
        err.print("driver", llvm::errs());
        throw std::runtime_error("Failed to parse LLVM IR");
    }
    return module;
}

z3::expr
AnalysisManager::get_ith_array_index(int i) {
    auto& z3ctx = AnalysisManager::get_ctx();
    auto z3_array = z3ctx.int_const(("array_i" + std::to_string(i)).c_str());
    return z3_array;
}