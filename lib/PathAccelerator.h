#ifndef PATH_ACCELERATOR_H
#define PATH_ACCELERATOR_H

#include <optional>
#include <string>

#include "PathExpression.h"
#include "state.h"

namespace llvm {
class Loop;
}

namespace ari_exe {

/**
 * A certified first-break jump or a stable-path arbitrary-finite exit jump.
 * `covered` projects the repetition count existentially, or substitutes a
 * certified functional count, and preserves the exact ordinary residual.
 */
struct PathAcceleration {
    state_ptr jump;
    z3::expr covered;
};

/** Parametric, certified complete exit of a deterministic counted loop.
 * Live-ins are explicit cache parameters: no caller's concrete values or path
 * assumptions are baked into a reusable summary. The domain includes safety
 * of every skipped assertion and child summary.
 */
struct NestedPathSummary {
    // A complete summary is a union of guarded exits, never an ITE update map.
    std::vector<std::shared_ptr<NestedPathSummary>> cases;
    std::vector<llvm::Value*> inputs;
    z3::expr_vector parameters;
    std::vector<llvm::Value*> outputs;
    z3::expr_vector values;
    z3::expr domain;
    z3::expr count;
    llvm::BasicBlock* exit = nullptr;
    PathWord word;

    explicit NestedPathSummary(z3::context& context)
        : parameters(context), values(context),
          domain(context.bool_val(true)), count(context.int_val(0)) {}
};

class PathExpressionAccelerator {
  public:
    /** Certified over-approximate union-star with guarded boundary replay.
     * No exact word is recorded. Multiple exits and internal assertions are
     * checked in one independent transaction before an entry is discharged.
     * Null leaves precise execution intact. Called at the preheader edge in
     * an independent proof attempt, never on an exact coverage complement.
     * Starts only at an outermost loop; nested scalar CFG edges become
     * nondeterministic maps in the same union-star, including copies/resets.
     */
    static state_ptr accelerate_coarse(llvm::Loop* loop,
                                       const state_ptr& state, unsigned degree = 2);

    /** Independent safety over-approximation of finite exits: havoc scalar
     * header coordinates and retain the actual exit guard. Does not prove
     * termination. Restricted to side-effect-free, nonnested loops with no
     * internal obligations and a single header exit. Null preserves execution.
     */
    static state_ptr accelerate_exit_guard(llvm::Loop* loop, const state_ptr& entry);

    /** Default proof ladder; an explicit environment degree pins one level. */
    static std::vector<unsigned> coarse_degrees();

    /** Select sequence-aware execution instead of the legacy summarizer. */
    static bool has_nondeterministic_header_control(llvm::Loop* loop);

    /** Cheap precise-profile check before waiting for trace evidence. */
    static std::optional<std::string> precise_rejection_reason(llvm::Loop* loop);

    /** Complete bottom-up exit jump; null means ordinary execution is needed.
     * Used at a loop entry, not at an observed backedge. Unsupported children,
     * nonfunctional counters, or unproved domains never filter out a path.
     */
    static std::vector<state_ptr> accelerate_nested(llvm::Loop* loop,
                                       const state_ptr& state);

    static std::optional<PathAcceleration>
    accelerate(llvm::Loop* loop, const state_ptr& state,
               const PathSchemaCandidate& candidate);
};

} // namespace ari_exe

#endif
