#ifndef PATH_EXPRESSION_H
#define PATH_EXPRESSION_H

#include <cstddef>
#include <cstdint>
#include <compare>
#include <string>
#include <vector>

namespace ari_exe {

using PathSymbol = std::uint32_t;
using PathWord = std::vector<PathSymbol>;

struct PathDecisionEvent {
    std::uintptr_t site = 0;
    std::uint32_t choice = 0;

    auto operator<=>(const PathDecisionEvent&) const = default;
};

struct ConcretePathPower {
    PathWord root;
    std::size_t exponent = 0;

    auto operator<=>(const ConcretePathPower&) const = default;
};

struct PathSchemaCandidate {
    std::vector<ConcretePathPower> closed_prefix;
    PathWord star;
    std::size_t observed_star_exponent = 0;
    std::size_t score = 0;

    auto operator<=>(const PathSchemaCandidate&) const = default;
};

/** Return true exactly when word is nonempty and has no shorter root. */
bool is_primitive_path_word(const PathWord& word);

/** Expand a finite sequence of powers. Intended for evidence checking/tests. */
PathWord expand_path_powers(const std::vector<ConcretePathPower>& powers);

/**
 * Deterministic minimum-cost compression from the method specification.
 *
 * Every result is an open expression Q star^* with exact evidence
 * Q star^r == prefix. The final word is primitive, has length at most
 * max_root_length, and occurs at least evidence_threshold times. If that
 * threshold finds no candidate, the algorithm retries with threshold one.
 */
std::vector<PathSchemaCandidate>
compress_path_prefix(const PathWord& prefix, std::size_t max_root_length,
                     std::size_t beam_width,
                     std::size_t evidence_threshold);

std::string render_path_word(const PathWord& word);
std::string render_path_schema(const PathSchemaCandidate& candidate);

} // namespace ari_exe

#endif
