//--------------------------------- Symbolic Execution Engine ---------------------------------
//
// TODO:
//
//---------------------------------------------------------------------------------------------

#ifndef ENGINE_H
#define ENGINE_H

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

#include "state.h"
#include "function_summary.h"

#include <vector>
#include <queue>

namespace ari_exe {
    const std::string default_entry_function_name = "main";

    class Engine {
        public:
            // result of verification
            enum VeriResult {
                HOLD,        // the verification condition is true
                FAIL,        // the verification condition is false
                VERIUNKNOWN, // the verification condition is unknown
            };

            enum TestResult {
                FEASIBLE,    // the state is feasible
                UNFEASIBLE,  // the state is unfeasible
                TESTUNKNOWN, // the state is unknown
            };

            Engine();
            Engine(std::unique_ptr<llvm::Module>& mod);

            // set the entry point of the module
            void set_entry(llvm::Function* entry);

            // run the engine from empty state
            void run();

            // run the engine from the given state
            void run(State &state);

            // run the engine from the given state one step
            // when branching, it will return two states if both branches are feasible
            virtual std::vector<State> step(State &state);

            // build initial state
            State build_initial_state();

            // verity the current state
            VeriResult verify(State &state);

            // test the feasibility of the current state
            TestResult test(State &state);

        private:

            // Transform and analyze the module
            void analyze_module();

            // Set the default entry point of the module if not set
            void set_default_entry();

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

            // Z3 related
            z3::context z3ctx;

            // symbol table


            std::unique_ptr<llvm::Module> mod;
            llvm::Function* entry = nullptr;

            // queue of states
            std::queue<State> states;

            // z3 solver
            z3::solver solver;

    };
}

#endif
