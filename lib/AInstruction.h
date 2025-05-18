#ifndef AINSTRUCTION_H
#define AINSTRUCTION_H

//----------------------------- Wrapper for llvm instructions -----------------------------//
//
// This file contains all AInstructions, which is a wrapper for LLVM.
// Visitor pattern for symbolic execution is implemented on top of them.
//
//-----------------------------------------------------------------------------------------//

#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"

#include "state.h"
#include "function_summary.h"

namespace ari_exe {
    class State;
    class Summary;

    // Abstract instruction, all instructions are derived from this class
    class AInstruction {
        public:
            AInstruction() = default;
            AInstruction(llvm::Instruction* inst): inst(inst) {};
            virtual ~AInstruction() = default;

            // Execute the instruction on the given state and return the new state(s)
            virtual std::vector<State> execute(State& state) = 0;

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
            std::vector<State> execute(State& state) override;
    };

    class AInstructionICmp: public AInstruction {
        public:
            AInstructionICmp(llvm::Instruction* inst): AInstruction(inst) {};
            std::vector<State> execute(State& state) override;
    };

    class AInstructionCall: public AInstruction {
        public:
            AInstructionCall(llvm::Instruction* inst): AInstruction(inst) {};
            std::vector<State> execute(State& state) override;

            // This method should only be called when we are doing function summarization.
            // execute the called function if it is not the given function.
            // if the called function is the given one,
            // do not execute it, just save the function application as the value
            std::vector<State> execute_if_not_target(State& state, llvm::Function* target);

            // summarize the called function completely
            // That is, no path condition is given for summarization
            std::optional<Summary> summarize_complete(z3::context& z3ctx);

            // check if the function is statically recursive
            bool is_recursive(llvm::Function* target);

        private:
            State execute_unknown(State& state);
            State execute_assert(State& state);
            State execute_assume(State& state);
            State execute_normal(State& state);
    };

    class AInstructionBranch: public AInstruction {
        public:
            AInstructionBranch(llvm::Instruction* inst): AInstruction(inst) {};
            std::vector<State> execute(State& state) override;
    };

    class AInstructionReturn: public AInstruction {
        public:
            AInstructionReturn(llvm::Instruction* inst): AInstruction(inst) {};
            std::vector<State> execute(State& state) override;
    };

    class AInstructionZExt: public AInstruction {
        public:
            AInstructionZExt(llvm::Instruction* inst): AInstruction(inst) {};
            std::vector<State> execute(State& state) override;
    };

    class AInstructionPhi: public AInstruction {
        public:
            AInstructionPhi(llvm::Instruction* inst): AInstruction(inst) {};
            std::vector<State> execute(State& state) override;
    };

    class AInstructionSelect: public AInstruction {
        public:
            AInstructionSelect(llvm::Instruction* inst): AInstruction(inst) {};
            std::vector<State> execute(State& state) override;
    };
}
#endif