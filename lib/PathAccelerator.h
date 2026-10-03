#ifndef PATH_ACCELERATOR_H
#define PATH_ACCELERATOR_H

#include <optional>

#include "PathExpression.h"
#include "state.h"

namespace llvm {
class Loop;
}

namespace ari_exe {

/**
 * A certified first-break jump or a stable-path arbitrary-finite exit jump.
 * `covered` is existentially closed over the fresh repetition count and is
 * used by the caller to preserve the exact ordinary residual.
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
    /** Select sequence-aware execution instead of the legacy summarizer. */
    static bool has_nondeterministic_header_control(llvm::Loop* loop);

    /** Complete bottom-up exit jump; null means ordinary execution is needed.
     * Used at a loop entry, not at an observed backedge. Unsupported children,
     * nonfunctional counters, or unproved domains never filter out a path.
     */
    static state_ptr accelerate_nested(llvm::Loop* loop,
                                       const state_ptr& state);

    static std::optional<PathAcceleration>
    accelerate(llvm::Loop* loop, const state_ptr& state,
               const PathSchemaCandidate& candidate);
};

} // namespace ari_exe

#endif
