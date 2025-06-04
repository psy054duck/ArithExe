//--------------------------------- Symbolic Execution Engine ---------------------------------
//
// TODO:
//
//---------------------------------------------------------------------------------------------

#ifndef ENGINE_H
#define ENGINE_H

#include <cassert>

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

#include "AnalysisManager.h"
#include "state.h"
#include "FunctionSummarizer.h"

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
            
            // /**
            //  * @brief Construct a new Engine object with the given module
            //  */
            // Engine(std::unique_ptr<llvm::Module>& mod, z3::context& z3ctx);

            /**
             * @brief Load a C file and create a llvm module
             * @param filename The name of C file to load
             */
            Engine(const std::string& c_filename);

            /**
             * @brief set the entry point of the module
             * @param entry the entry point of the module
             */
            void set_entry(llvm::Function* entry);

            // run the engine from empty state
            void run();

            // run the engine until reach the end of some basic block
            void run_until(state_ptr state, llvm::BasicBlock* end_block);

            // run the engine from the given state
            void run(state_ptr state);

            // run the engine from the given state one step
            // when branching, it will return two states if both branches are feasible
            std::vector<state_ptr> step(state_ptr state);

            // build initial state
            state_ptr build_initial_state();

            // verity the current state
            VeriResult verify(state_ptr state);

            // test the feasibility of the current state
            TestResult test(state_ptr state);

            /**
             * @brief Verify if the program is correct
             * @return The result of the verification
             */
            VeriResult verify();

        private:

            // Set the default entry point of the module if not set
            void set_default_entry();

            /**
             * @brief check if the state just reach a loop
             */
            bool reach_loop(state_ptr state);

            // Z3 related
            z3::context& z3ctx = AnalysisManager::get_instance()->get_z3ctx();

            std::unique_ptr<llvm::Module> mod;
            llvm::Function* entry = nullptr;

            // queue of states
            std::queue<state_ptr> states;

            // z3 solver
            z3::solver solver;

            // store all verification results for all paths
            std::vector<VeriResult> results;
    };
}

#endif
