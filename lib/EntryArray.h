#ifndef ARITHEXE_ENTRY_ARRAY_H
#define ARITHEXE_ENTRY_ARRAY_H

#include "state.h"
#include "llvm/Analysis/LoopInfo.h"

namespace ari_exe {
// Prove absence of preceding overlapping writes in the analyzed loop.
// Unsupported accesses, calls, and schedules conservatively return false.
bool reads_entry_array(llvm::Loop* loop, llvm::LoadInst* read,
                       const state_ptr& entry);
}

#endif
