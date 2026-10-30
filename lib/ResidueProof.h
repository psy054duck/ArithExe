#ifndef ARITHEXE_RESIDUE_PROOF_H
#define ARITHEXE_RESIDUE_PROOF_H

namespace llvm { class Function; }
namespace ari_exe {
class VerificationSession;

// Independent, proof-only guarded CFG closure. SAT, UNKNOWN, an unsupported
// instruction or exhausted budget never produces a verdict or filters an
// ordinary state. Unlike polynomial union-star, it retains branch guards.
bool prove_guarded_residues(llvm::Function*, VerificationSession&);
}
#endif
