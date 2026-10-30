#include "PropertyReachability.h"

#include <unordered_map>
#include <vector>

#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

namespace ari_exe {
namespace {

bool is_obligation_name(llvm::StringRef name) {
    // Match the executor's verifier hooks; also retain the C assertion hook.
    return name.ends_with("assert") || name.contains("reach_error") ||
           name == "__VERIFIER_error" || name == "__assert_fail" ||
           name == "__assert_rtn";
}

bool trusted_external(const llvm::CallBase& call, const llvm::Function& function) {
    const auto name = function.getName();
    // Explicit benchmark-oracle contract, not a guess from readnone/nounwind.
    if (call.arg_empty() && call.getType()->isIntegerTy() &&
        (name == "__VERIFIER_nondet_bool" || name == "__VERIFIER_nondet_int" ||
         name == "__VERIFIER_nondet_uint" || name == "__VERIFIER_nondet_char" ||
         name == "__VERIFIER_nondet_uchar" || name == "__VERIFIER_nondet_short" ||
         name == "__VERIFIER_nondet_ushort" || name == "__VERIFIER_nondet_long" ||
         name == "__VERIFIER_nondet_ulong" || name == "__VERIFIER_nondet_longlong" ||
         name == "__VERIFIER_nondet_ulonglong")) return true;
    if (name == "abort" && call.arg_empty() && call.getType()->isVoidTy()) return true;
    if (name == "__VERIFIER_assume" && call.arg_size() == 1 &&
        call.getArgOperand(0)->getType()->isIntegerTy()) return true;
    if (!function.isIntrinsic()) return false;
    // Only intrinsics with no hidden callbacks. In particular, do not infer
    // this for coroutine/deoptimization intrinsics or arbitrary llvm.* names.
    return name.starts_with("llvm.dbg.") || name.starts_with("llvm.lifetime.") ||
           name == "llvm.assume" || name.starts_with("llvm.expect.") ||
           name.starts_with("llvm.stacksave.") || name.starts_with("llvm.stackrestore.") ||
           name.starts_with("llvm.memcpy.") || name.starts_with("llvm.memmove.") ||
           name.starts_with("llvm.memset.") || name == "llvm.trap" ||
           name == "llvm.debugtrap";
}

} // namespace

void PropertyReachability::rebuild(const llvm::Module& module) {
    indexed_module = &module;
    relevant.clear();
    using Instruction = const llvm::Instruction*;
    std::unordered_map<Instruction, std::vector<Instruction>> predecessors;
    std::vector<Instruction> work;
    const auto seed = [&](Instruction instruction) {
        if (relevant.insert(instruction).second) work.push_back(instruction);
    };
    const auto edge = [&](Instruction source, Instruction destination) {
        if (destination) predecessors[destination].push_back(source);
    };
    for (const auto& function : module) {
        for (const auto& block : function) {
            for (const auto& instruction : block) {
                if (const auto* next = instruction.getNextNode()) {
                    edge(&instruction, next);
                } else if (const auto* branch = llvm::dyn_cast<llvm::BranchInst>(&instruction)) {
                    if (branch->isConditional()) {
                        if (const auto* condition = llvm::dyn_cast<llvm::ConstantInt>(branch->getCondition())) {
                            edge(branch, &branch->getSuccessor(condition->isZero() ? 1 : 0)->front());
                            continue;
                        }
                    }
                    for (const auto* successor : llvm::successors(&block))
                        edge(&instruction, &successor->front());
                } else {
                    for (const auto* successor : llvm::successors(&block))
                        edge(&instruction, &successor->front());
                }
                const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction);
                if (!call) continue;
                const auto* callee = llvm::dyn_cast<llvm::Function>(
                    call->getCalledOperand()->stripPointerCasts());
                if (!callee || call->isInlineAsm() || is_obligation_name(callee->getName())) {
                    seed(call);
                } else if (callee->hasExactDefinition()) {
                    // A single backwards graph also handles recursion and
                    // mutually recursive call cycles to a hazardous leaf.
                    edge(call, &callee->getEntryBlock().front());
                } else if (!callee->isDeclaration() || !trusted_external(*call, *callee)) {
                    // Weak/replaceable definitions and unresolved externals
                    // can execute code absent from this module.
                    seed(call);
                }
            }
        }
    }
    while (!work.empty()) {
        const auto current = work.back();
        work.pop_back();
        for (const auto* predecessor : predecessors[current])
            if (relevant.insert(predecessor).second) work.push_back(predecessor);
    }
}

bool PropertyReachability::may_reach_obligation(const llvm::Instruction* instruction) const {
    // Missing/mismatched indexes and continuations always retain execution.
    return !instruction || instruction->getModule() != indexed_module ||
           relevant.contains(instruction);
}

} // namespace ari_exe
