#ifndef ARITHEXE_PROPERTY_REACHABILITY_H
#define ARITHEXE_PROPERTY_REACHABILITY_H

#include <unordered_set>

namespace llvm {
class Instruction;
class Module;
}

namespace ari_exe {

// Context-insensitive, conservative instruction/call reachability for
// unreach-call/assertion safety. Unknown calls count as obligations too.
// Returns have no static caller edge: dynamic caller continuations must be
// checked separately before pruning an execution state.
class PropertyReachability {
  public:
    void rebuild(const llvm::Module& module);
    bool may_reach_obligation(const llvm::Instruction* instruction) const;

  private:
    const llvm::Module* indexed_module = nullptr;
    std::unordered_set<const llvm::Instruction*> relevant;
};

} // namespace ari_exe
#endif
