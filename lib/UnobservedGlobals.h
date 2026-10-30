#ifndef ARITHEXE_UNOBSERVED_GLOBALS_H
#define ARITHEXE_UNOBSERVED_GLOBALS_H

#include <cstddef>
#include <vector>
#include "llvm/IR/PassManager.h"

namespace llvm {
class GlobalVariable;
class Module;
}

namespace ari_exe {
// Whole-program assertion-reachability contract, not a general-purpose
// library optimization: no external observer of exported state is assumed
// after the checked entry returns. Unknown calls/aliases prevent certification.
std::vector<llvm::GlobalVariable*> unobserved_store_globals(llvm::Module& module);
std::size_t erase_unobserved_global_stores(llvm::Module& module);

struct UnobservedGlobalStorePass : llvm::PassInfoMixin<UnobservedGlobalStorePass> {
    llvm::PreservedAnalyses run(llvm::Module& module,
                               llvm::ModuleAnalysisManager&);
};
} // namespace ari_exe
#endif
