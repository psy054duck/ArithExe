#ifndef ANALYSISMANAGER_H
#define ANALYSISMANAGER_H

#include "z3++.h"

#include "llvm/IRReader/IRReader.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LCSSA.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Utils/InstructionNamer.h"
#include "llvm/Transforms/Utils/LowerSwitch.h"

#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Scalar/LoopFuse.h"
#include "llvm/Transforms/Scalar/LoopRotation.h"
#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/Transforms/Scalar/SCCP.h"
#include "llvm/Transforms/Scalar/IndVarSimplify.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/DCE.h"
#include "llvm/Transforms/Scalar/Reg2Mem.h"

#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombine.h"
#include "llvm/Transforms/IPO/ModuleInliner.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Analysis/CallGraph.h"

namespace ari_exe {
    
    /**
     * @brief The analysis manager for the whole module. It is implemented in singleton pattern.
     * @details This class is used to manage the analysis of the whole program.
     *          It is a singleton class, and only one instance of it can be created.
     */
    class AnalysisManager {
        public:
            ~AnalysisManager() = default;
            AnalysisManager(const AnalysisManager&) = delete;
            AnalysisManager& operator=(const AnalysisManager&) = delete;
            AnalysisManager(AnalysisManager&&) = delete;
            AnalysisManager& operator=(AnalysisManager&&) = delete;
            static AnalysisManager* get_instance() { return instance; }

            /** 
             * @brief Run clang to compile C into llvm ir
             * @param filename The name of the C source file to compile
             * @return The LLVM IR as a string
             */
            static std::string generateLLVMIR(const std::string& c_filename);

            /**
             * @brief Parse the LLVM IR string into a Module
             * @param ir_content The LLVM IR as a string
             * @param context The LLVM context to use for parsing
             * @return A unique pointer to the parsed Module
             */
            static std::unique_ptr<llvm::Module> parseLLVMIR(const std::string& ir_content, llvm::LLVMContext& context);



            /**
             * @brief compile the given C file into LLVM IR and analyze it
             * @param c_filename The name of the C file to be analyzed
             * @param z3ctx The Z3 context to be used for analysis
             * @return A unique pointer to the analyzed module
             */
            std::unique_ptr<llvm::Module> get_module(const std::string& c_filename, z3::context& z3ctx);

            // getters of all fields
            z3::context& get_z3ctx() { return z3ctx; }
            llvm::LLVMContext& get_context() { return context; }
            llvm::ModulePassManager& get_MPM() { return MPM; }
            llvm::LoopAnalysisManager& get_LAM() { return LAM; }
            llvm::FunctionAnalysisManager& get_FAM() { return FAM; }
            llvm::CGSCCAnalysisManager& get_CGAM() { return CGAM; }
            llvm::ModuleAnalysisManager& get_MAM() { return MAM; }
            llvm::PassBuilder& get_PB() { return PB; }
            llvm::LoopInfo& get_LI(llvm::Function* F) { return LIs.at(F); }
            llvm::DominatorTree& get_DT(llvm::Function* F) { return DTs.at(F); }
            llvm::PostDominatorTree& get_PDT(llvm::Function* F) { return PDTs.at(F); }
            llvm::CallGraph* get_CG() { return CG; }

            z3::expr get_ind_var() { return ind_var; }
            z3::expr get_loop_N() { return loop_N; }

            // counter for unknowns
            static int unknown_counter;

        private :
            AnalysisManager(): ind_var(z3ctx.int_const("ari_loop_n")), loop_N(z3ctx.int_const("ari_loop_N")) {}
            static AnalysisManager* instance;

            // LLVM context for keeping the module
            llvm::LLVMContext context;

            // pass managers
            llvm::ModulePassManager MPM;
            llvm::LoopAnalysisManager LAM;
            llvm::FunctionAnalysisManager FAM;
            llvm::CGSCCAnalysisManager CGAM;
            llvm::ModuleAnalysisManager MAM;
            llvm::PassBuilder PB;

            // analysis results
            std::map<llvm::Function*, llvm::LoopInfo&> LIs;
            std::map<llvm::Function*, llvm::DominatorTree> DTs;
            std::map<llvm::Function*, llvm::PostDominatorTree> PDTs;

            llvm::CallGraph* CG = nullptr;

            z3::context z3ctx;

            // The symbolic variable for the loop induction variable
            z3::expr ind_var;

            // The symbolic variable for the loop iteration count
            z3::expr loop_N;

    };
}

#endif