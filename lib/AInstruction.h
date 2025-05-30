#ifndef AINSTRUCTION_H
#define AINSTRUCTION_H

//----------------------------- Wrapper for llvm instructions -----------------------------//
//
// This file contains all AInstructions, which is a wrapper for LLVM.
// Visitor pattern for symbolic execution is implemented on top of them.
//
//-----------------------------------------------------------------------------------------//

#include <set>

#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"

#include "AnalysisManager.h"
#include "state.h"
#include "FunctionSummary.h"
#include "LoopSummary.h"
#include "FunctionSummarizer.h"
#include "LoopSummarizer.h"

namespace ari_exe {
    // class State;
    class FunctionSummary;
    class FunctionSummarizer;
    class LoopSummary;
    class LoopSummarizer;

    // Abstract instruction, all instructions are derived from this class
    class AInstruction {
        public:
            AInstruction() = default;
            AInstruction(llvm::Instruction* inst): inst(inst) {};
            virtual ~AInstruction() = default;

            // Execute the instruction on the given state and return the new state(s)
            virtual state_list execute(state_ptr state) = 0;
            virtual loop_state_list execute(loop_state_ptr state);

            // Get the next instruction in the same basic block,
            // or nullptr if there is no next instruction in the block.
            virtual AInstruction* get_next_instruction();

            // Get the basic block where the instruction is located
            llvm::BasicBlock* get_block();

            // Factory method to create an AInstruction from an LLVM instruction
            static AInstruction* create(llvm::Instruction* inst);

            // cache all instructions
            static std::map<llvm::Instruction*, AInstruction*> cached_instructions;

            llvm::Instruction* inst;
    };

    class AInstructionBinary: public AInstruction {
        public:
            AInstructionBinary(llvm::Instruction* inst): AInstruction(inst) {};
            ~AInstructionBinary() = default;
            std::vector<state_ptr> execute(state_ptr state) override;
    };

    class AInstructionICmp: public AInstruction {
        public:
            AInstructionICmp(llvm::Instruction* inst): AInstruction(inst) {};
            std::vector<state_ptr> execute(state_ptr state) override;
    };

    class AInstructionCall: public AInstruction {
        public:
            AInstructionCall(llvm::Instruction* inst): AInstruction(inst) {};
            std::vector<state_ptr> execute(state_ptr state) override;

            // This method should only be called when we are doing function summarization.
            // execute the called function if it is not the given function.
            // if the called function is the given one,
            // do not execute it, just save the function application as the value
            std::vector<state_ptr> execute_if_not_target(state_ptr state, llvm::Function* target);

            // summarize the called function completely
            // That is, no path condition is given for summarization
            std::optional<FunctionSummary> summarize_complete(z3::context& z3ctx);

            // check if the function is statically recursive
            bool is_recursive(llvm::Function* target);

        private:
            state_ptr execute_unknown(state_ptr state);
            state_ptr execute_assert(state_ptr state);
            state_ptr execute_assume(state_ptr state);
            state_ptr execute_normal(state_ptr state);
            state_ptr execute_malloc(state_ptr state);
    };

    class AInstructionBranch: public AInstruction {
        public:
            AInstructionBranch(llvm::Instruction* inst): AInstruction(inst) {};
            state_list execute(state_ptr state) override;
            loop_state_list execute(loop_state_ptr state) override;

            template<typename state_ty>
            state_list_base<state_ty> _execute(std::shared_ptr<state_ty> state);
    };

    class AInstructionReturn: public AInstruction {
        public:
            AInstructionReturn(llvm::Instruction* inst): AInstruction(inst) {};
            std::vector<state_ptr> execute(state_ptr state) override;
    };

    class AInstructionZExt: public AInstruction {
        public:
            AInstructionZExt(llvm::Instruction* inst): AInstruction(inst) {};
            std::vector<state_ptr> execute(state_ptr state) override;
    };

    class AInstructionSExt: public AInstruction {
        public:
            AInstructionSExt(llvm::Instruction* inst): AInstruction(inst) {};
            std::vector<state_ptr> execute(state_ptr state) override;
    };

    class AInstructionPhi: public AInstruction {
        public:
            AInstructionPhi(llvm::Instruction* inst): AInstruction(inst) {};
            std::vector<state_ptr> execute(state_ptr state) override;

            /**
             * @brief if reach a loop and the loop is summarizable, use the summary and skip the execution
             */
            std::vector<state_ptr> execute_if_summarizable(state_ptr state);
    
            /**
             * @brief record all loops that are failed to be summarized
             */
            static std::set<llvm::Loop*> failed_loops;
    };

    class AInstructionSelect: public AInstruction {
        public:
            AInstructionSelect(llvm::Instruction* inst): AInstruction(inst) {};
            state_list execute(state_ptr state) override;
            loop_state_list execute(loop_state_ptr state) override;

            template<typename state_ty>
            state_list_base<state_ty> _execute(std::shared_ptr<state_ty> state);
    };

    class AInstructionLoad: public AInstruction {
        public:
            AInstructionLoad(llvm::Instruction* inst): AInstruction(inst) {};
            std::vector<state_ptr> execute(state_ptr state) override;
    };

    class AInstructionStore: public AInstruction {
        public:
            AInstructionStore(llvm::Instruction* inst): AInstruction(inst) {};
            // TODO: for now, only assume it is a write to a scalar variable
            std::vector<state_ptr> execute(state_ptr state) override;
    };

    class AInstructionGEP: public AInstruction {
        public:
            AInstructionGEP(llvm::Instruction* inst): AInstruction(inst) {};
            std::vector<state_ptr> execute(state_ptr state) override;
    };
}
#endif