#ifndef ARRAY_GUARD_H
#define ARRAY_GUARD_H

#include "z3++.h"
#include <optional>
#include <vector>

namespace ari_exe {
// Use a convex guard only when equivalence to the original is certified.
z3::expr normalize_array_guard(const z3::expr& guard);

// Compose independent overwrite regions; certify the result by induction.
std::optional<z3::expr> summarize_overwrite_regions(
    const z3::expr& signature, const z3::func_decl& recurrence,
    const z3::expr& iteration, const std::vector<z3::expr>& conditions,
    const std::vector<z3::expr>& values, const z3::expr& valid_domain,
    std::optional<z3::expr> iteration_limit=std::nullopt);
}

#endif
