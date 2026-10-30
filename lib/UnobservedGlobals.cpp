#include "UnobservedGlobals.h"

#include <cstdlib>
#include <string>
#include <spdlog/spdlog.h>
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

namespace ari_exe {
namespace {
bool known_memory_object(const llvm::Value* pointer) {
    const auto* base = llvm::getUnderlyingObject(pointer);
    if (llvm::isa<llvm::AllocaInst>(base) ||
        llvm::isa<llvm::ConstantPointerNull>(base)) return true;
    const auto* global = llvm::dyn_cast<llvm::GlobalVariable>(base);
    return global && global->hasDefinitiveInitializer();
}

bool oracle_contract(const llvm::CallBase& call, const llvm::Function& function) {
    const auto name = function.getName();
    if (call.arg_empty() && call.getType()->isIntegerTy() &&
        (name == "__VERIFIER_nondet_bool" || name == "__VERIFIER_nondet_int" ||
         name == "__VERIFIER_nondet_uint" || name == "__VERIFIER_nondet_char" ||
         name == "__VERIFIER_nondet_schar" || name == "__VERIFIER_nondet_uchar" ||
         name == "__VERIFIER_nondet_short" || name == "__VERIFIER_nondet_ushort" ||
         name == "__VERIFIER_nondet_long" || name == "__VERIFIER_nondet_ulong" ||
         name == "__VERIFIER_nondet_longlong" || name == "__VERIFIER_nondet_ulonglong"))
        return true;
    if (!call.getType()->isVoidTy()) return false;
    if (call.arg_empty() &&
        (name == "abort" || name == "reach_error" || name == "__VERIFIER_error"))
        return true;
    if ((name == "__VERIFIER_assert" || name == "__VERIFIER_assume") &&
        call.arg_size() == 1 && call.getArgOperand(0)->getType()->isIntegerTy())
        return true;
    return name == "__assert_fail" && call.arg_size() == 4 &&
           call.getArgOperand(0)->getType()->isPointerTy() &&
           call.getArgOperand(1)->getType()->isPointerTy() &&
           call.getArgOperand(2)->getType()->isIntegerTy() &&
           call.getArgOperand(3)->getType()->isPointerTy();
}

bool known_intrinsic(const llvm::Function& function) {
    if (!function.isIntrinsic()) return false;
    const auto name = function.getName();
    return name.starts_with("llvm.dbg.") || name.starts_with("llvm.lifetime.") ||
           name == "llvm.assume" || name.starts_with("llvm.expect.") ||
           name.starts_with("llvm.stacksave.") || name.starts_with("llvm.stackrestore.") ||
           name.starts_with("llvm.memcpy.") || name.starts_with("llvm.memmove.") ||
           name.starts_with("llvm.memset.") || name == "llvm.trap" || name == "llvm.debugtrap";
}

bool closed_memory_observers(const llvm::Module& module) {
    if (!module.getModuleInlineAsm().empty()) return false;
    // Audit all bodies, including helper functions, rather than assuming a
    // caller's declaration/attributes imply absence of hidden observers.
    for (const auto& function : module) {
        // Exception personalities and GC runtimes are hidden call surfaces.
        if (function.hasPersonalityFn() || function.hasGC()) return false;
        for (const auto& block : function) {
            for (const auto& instruction : block) {
                const llvm::Value* address = nullptr;
                if (const auto* load = llvm::dyn_cast<llvm::LoadInst>(&instruction))
                    address = load->getPointerOperand();
                else if (const auto* store = llvm::dyn_cast<llvm::StoreInst>(&instruction))
                    address = store->getPointerOperand();
                else if (const auto* atomic = llvm::dyn_cast<llvm::AtomicRMWInst>(&instruction))
                    address = atomic->getPointerOperand();
                else if (const auto* atomic = llvm::dyn_cast<llvm::AtomicCmpXchgInst>(&instruction))
                    address = atomic->getPointerOperand();
                if (address && !known_memory_object(address)) return false;
                const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction);
                if (!call) continue;
                const auto* callee = llvm::dyn_cast<llvm::Function>(
                    call->getCalledOperand()->stripPointerCasts());
                if (!callee || call->isInlineAsm() || call->hasOperandBundles()) return false;
                if (!callee->hasExactDefinition() &&
                    (!callee->isDeclaration() ||
                     (!oracle_contract(*call, *callee) && !known_intrinsic(*callee))))
                    return false;
                for (const auto& argument : call->args())
                    if (argument->getType()->isPointerTy() && !known_memory_object(argument.get()))
                        return false;
            }
        }
    }
    return true;
}
} // namespace

std::vector<llvm::GlobalVariable*> unobserved_store_globals(llvm::Module& module) {
    std::vector<llvm::GlobalVariable*> globals;
    if (!closed_memory_observers(module)) return globals;
    for (auto& global : module.globals()) {
        if (!global.hasUniqueInitializer() || global.isConstant() ||
            global.isThreadLocal() || !global.getValueType()->isIntegerTy() ||
            global.use_empty()) continue;
        bool unobserved = true;
        for (const auto* user : global.users()) {
            const auto* store = llvm::dyn_cast<llvm::StoreInst>(user);
            if (!store || store->getPointerOperand() != &global ||
                store->getValueOperand() == &global || store->isVolatile() || store->isAtomic()) {
                unobserved = false;
                break;
            }
        }
        if (unobserved) globals.push_back(&global);
    }
    return globals;
}

std::size_t erase_unobserved_global_stores(llvm::Module& module) {
    std::size_t count = 0;
    for (auto* global : unobserved_store_globals(module)) {
        const std::vector<llvm::User*> users(global->user_begin(), global->user_end());
        spdlog::debug("[spill-cleanup] certified unobserved global {}; removing {} store(s)",
                      global->getName().str(), users.size());
        for (auto* user : users) {
            llvm::cast<llvm::StoreInst>(user)->eraseFromParent();
            ++count;
        }
    }
    return count;
}

llvm::PreservedAnalyses UnobservedGlobalStorePass::run(
    llvm::Module& module, llvm::ModuleAnalysisManager&) {
    if (const char* enabled = std::getenv("ARITHEXE_UNOBSERVED_GLOBAL_CLEANUP");
        enabled && std::string(enabled) == "0") return llvm::PreservedAnalyses::all();
    return erase_unobserved_global_stores(module) ? llvm::PreservedAnalyses::none()
                                                : llvm::PreservedAnalyses::all();
}
} // namespace ari_exe
