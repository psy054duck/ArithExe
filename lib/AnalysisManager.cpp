#include "AnalysisManager.h"
#include "common.h"
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <sstream>
#include <string>

#include "llvm/Support/raw_ostream.h"

using namespace ari_exe;

AnalysisManager* AnalysisManager::instance = new AnalysisManager();

// z3::context AnalysisManager::z3ctx;

int AnalysisManager::unknown_counter = 0;

namespace {
std::string diagnostic_to_string(const llvm::SMDiagnostic& diagnostic) {
    std::string message;
    llvm::raw_string_ostream out(message);
    diagnostic.print("driver", out);
    return out.str();
}

std::string clang_debug_flag() {
    const char* raw = std::getenv("ARITHEXE_CLANG_DEBUG");
    if (raw == nullptr || raw[0] == '\0') {
        return "-g";
    }
    const std::string mode(raw);
    if (mode == "0" || mode == "false" || mode == "off" ||
        mode == "none") {
        return "-g0";
    }
    if (mode == "full" || mode == "2" || mode == "true" ||
        mode == "on") {
        return "-g";
    }
    if (mode == "line-tables-only" || mode == "line-tables") {
        return "-gline-tables-only";
    }
    spdlog::warn("Unknown ARITHEXE_CLANG_DEBUG={}, using full debug info",
                 mode);
    return "-g";
}
}

std::unique_ptr<llvm::Module>
AnalysisManager::get_module(const std::string& c_filename, z3::context& z3ctx) {
    auto ir_content = generateLLVMIR(c_filename);
    auto mod = parseLLVMIR(ir_content, context);
    MPM = llvm::ModulePassManager();
    LAM = llvm::LoopAnalysisManager();
    FAM = llvm::FunctionAnalysisManager();
    CGAM = llvm::CGSCCAnalysisManager();
    MAM = llvm::ModuleAnalysisManager();
    PB = llvm::PassBuilder();
    LIs.clear();
    DTs.clear();
    PDTs.clear();
    DIs.clear();
    CG = nullptr;
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
    auto shell_quote = [](const std::string& value) {
        std::string quoted = "'";
        for (char ch : value) {
            if (ch == '\'') quoted += "'\\''";
            else quoted += ch;
        }
        return quoted + "'";
    };

    const char* configured_clang = std::getenv("ARITHEXE_CLANG");
#ifdef ARITHEXE_DEFAULT_CLANG
    const std::string clang_path = configured_clang ? configured_clang
                                                    : ARITHEXE_DEFAULT_CLANG;
#else
    const std::string clang_path = configured_clang ? configured_clang : "clang";
#endif
    std::ostringstream clang_cmd;
    clang_cmd << shell_quote(clang_path)
              << " -emit-llvm " << clang_debug_flag()
              << " -S -O0 -Xclang -disable-O0-optnone ";
    if (const char* data_model = std::getenv("ARITHEXE_DATA_MODEL")) {
        if (std::string(data_model) == "ILP32") clang_cmd << "-m32 ";
        else if (std::string(data_model) == "LP64") clang_cmd << "-m64 ";
    }
    clang_cmd << shell_quote(c_filename) << " -o -";
    spdlog::debug("Running frontend command: {}", clang_cmd.str());
    FILE* pipe = popen(clang_cmd.str().c_str(), "r");
    if (!pipe) {
        throw VerifierError(VerifierIssueKind::FrontendCommand,
                            "failed to run clang command: " +
                                clang_cmd.str());
    }

    std::string ir_content;
    char buffer[8192];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        ir_content += buffer;
    }
    int clang_ret = pclose(pipe);
    if (clang_ret != 0) {
        throw VerifierError(VerifierIssueKind::FrontendCommand,
                            "failed to convert " + c_filename +
                                " into LLVM IR with command: " +
                                clang_cmd.str());
    }
    return ir_content;
}

std::string
AnalysisManager::sanitizeLLVMIRForParser(const std::string& ir_content) {
    std::istringstream input(ir_content);
    std::ostringstream output;
    std::string line;
    while (std::getline(input, line)) {
        if (line.find("!DILabel(") != std::string::npos) {
            const std::string marker = ", column: ";
            size_t marker_pos = line.find(marker);
            if (marker_pos != std::string::npos) {
                size_t end_pos = line.find(',', marker_pos + marker.size());
                if (end_pos == std::string::npos) {
                    end_pos = line.find(')', marker_pos + marker.size());
                }
                if (end_pos != std::string::npos) {
                    line.erase(marker_pos, end_pos - marker_pos);
                }
            }
        }
        output << line << '\n';
    }
    return output.str();
}

std::unique_ptr<llvm::Module>
AnalysisManager::parseLLVMIR(const std::string& ir_content, llvm::LLVMContext& context) {
    llvm::SMDiagnostic err;
    auto memBuffer = llvm::MemoryBuffer::getMemBuffer(ir_content, "in-memory.ll");
    auto module = llvm::parseIR(memBuffer->getMemBufferRef(), err, context);
    if (module) {
        return module;
    }

    const std::string first_error = diagnostic_to_string(err);
    std::string sanitized = sanitizeLLVMIRForParser(ir_content);
    if (sanitized != ir_content) {
        llvm::SMDiagnostic retry_err;
        auto retry_buffer =
            llvm::MemoryBuffer::getMemBuffer(sanitized, "in-memory.ll");
        auto retry_module =
            llvm::parseIR(retry_buffer->getMemBufferRef(), retry_err,
                          context);
        if (retry_module) {
            spdlog::warn(
                "LLVM IR parsed after removing unsupported debug metadata: {}",
                first_error);
            return retry_module;
        }
        throw VerifierError(
            VerifierIssueKind::FrontendIRParse,
            "failed to parse LLVM IR after metadata sanitization:\n" +
                diagnostic_to_string(retry_err));
    }

    throw VerifierError(VerifierIssueKind::FrontendIRParse,
                        "failed to parse LLVM IR:\n" + first_error);
}

z3::expr
AnalysisManager::get_ith_array_index(int i) {
    auto& z3ctx = AnalysisManager::get_ctx();
    auto z3_array = z3ctx.int_const(("array_i" + std::to_string(i)).c_str());
    return z3_array;
}
