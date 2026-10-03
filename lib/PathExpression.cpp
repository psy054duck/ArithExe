#include "PathExpression.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <sstream>
#include <tuple>

namespace ari_exe {
namespace {

constexpr std::size_t block_weight = 2;
constexpr std::size_t exponent_weight = 1;
constexpr std::size_t star_weight = 2;

std::size_t exponent_bits(std::size_t exponent) {
    return std::bit_width(exponent);
}

std::size_t block_cost(const PathWord& root, std::size_t exponent) {
    return block_weight + root.size() +
           exponent_weight * exponent_bits(exponent);
}

bool word_equal_at(const PathWord& text, std::size_t position,
                   const PathWord& word) {
    if (position + word.size() > text.size()) return false;
    return std::equal(word.begin(), word.end(), text.begin() + position);
}

struct FactorChoice {
    bool present = false;
    std::size_t predecessor = 0;
    PathWord root;
    std::size_t exponent = 0;
};

using ChoiceKey = std::tuple<std::size_t, std::size_t, std::size_t,
                             PathWord, std::size_t>;

ChoiceKey choice_key(std::size_t cost, std::size_t predecessor,
                     const PathWord& root, std::size_t exponent) {
    // Larger exponents win the final tie. Subtracting from max implements -r
    // without introducing a signed overflow edge case.
    return {cost, predecessor, root.size(), root,
            std::numeric_limits<std::size_t>::max() - exponent};
}

struct FactorTable {
    std::vector<std::size_t> costs;
    std::vector<FactorChoice> choices;
};

FactorTable factor_table(const PathWord& prefix,
                         std::size_t max_root_length) {
    const std::size_t infinity = std::numeric_limits<std::size_t>::max();
    FactorTable table;
    table.costs.assign(prefix.size() + 1, infinity);
    table.choices.assign(prefix.size() + 1, FactorChoice{});
    table.costs[0] = 0;

    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (table.costs[i] == infinity) continue;
        const std::size_t max_length =
            std::min(max_root_length, prefix.size() - i);
        for (std::size_t length = 1; length <= max_length; ++length) {
            PathWord root(prefix.begin() + i, prefix.begin() + i + length);
            if (!is_primitive_path_word(root)) continue;

            std::size_t exponent = 0;
            std::size_t end = i;
            while (word_equal_at(prefix, end, root)) {
                ++exponent;
                end += length;
                const std::size_t cost =
                    table.costs[i] + block_cost(root, exponent);
                const ChoiceKey candidate =
                    choice_key(cost, i, root, exponent);
                const FactorChoice& old = table.choices[end];
                const bool improve =
                    !old.present ||
                    candidate < choice_key(table.costs[end], old.predecessor,
                                           old.root, old.exponent);
                if (improve) {
                    table.costs[end] = cost;
                    table.choices[end] =
                        FactorChoice{true, i, root, exponent};
                }
            }
        }
    }
    return table;
}

std::vector<ConcretePathPower>
backtrack(const FactorTable& table, std::size_t end) {
    std::vector<ConcretePathPower> reversed;
    while (end != 0) {
        const FactorChoice& choice = table.choices[end];
        if (!choice.present || choice.exponent == 0 ||
            choice.predecessor >= end) {
            return {};
        }
        reversed.push_back({choice.root, choice.exponent});
        end = choice.predecessor;
    }
    std::reverse(reversed.begin(), reversed.end());
    return reversed;
}

struct CandidateOrder {
    bool operator()(const PathSchemaCandidate& lhs,
                    const PathSchemaCandidate& rhs) const {
        return std::tuple(lhs.score,
                          std::numeric_limits<std::size_t>::max() -
                              lhs.observed_star_exponent,
                          lhs.star.size(), lhs.star, lhs.closed_prefix) <
               std::tuple(rhs.score,
                          std::numeric_limits<std::size_t>::max() -
                              rhs.observed_star_exponent,
                          rhs.star.size(), rhs.star, rhs.closed_prefix);
    }
};

} // namespace

bool is_primitive_path_word(const PathWord& word) {
    if (word.empty()) return false;
    for (std::size_t period = 1; period < word.size(); ++period) {
        if (word.size() % period != 0) continue;
        bool periodic = true;
        for (std::size_t i = 0; i < word.size(); ++i) {
            if (word[i] != word[i % period]) {
                periodic = false;
                break;
            }
        }
        if (periodic) return false;
    }
    return true;
}

PathWord expand_path_powers(const std::vector<ConcretePathPower>& powers) {
    PathWord result;
    for (const ConcretePathPower& power : powers) {
        for (std::size_t count = 0; count < power.exponent; ++count) {
            result.insert(result.end(), power.root.begin(), power.root.end());
        }
    }
    return result;
}

std::vector<PathSchemaCandidate>
compress_path_prefix(const PathWord& prefix, std::size_t max_root_length,
                     std::size_t beam_width,
                     std::size_t evidence_threshold) {
    if (prefix.empty() || max_root_length == 0 || beam_width == 0) return {};
    evidence_threshold = std::max<std::size_t>(evidence_threshold, 1);

    const FactorTable table = factor_table(prefix, max_root_length);
    std::vector<PathSchemaCandidate> candidates;
    const std::size_t max_length =
        std::min(max_root_length, prefix.size());
    for (std::size_t length = 1; length <= max_length; ++length) {
        PathWord root(prefix.end() - length, prefix.end());
        if (!is_primitive_path_word(root)) continue;

        std::size_t exponent = 0;
        std::size_t start = prefix.size();
        while (start >= length &&
               word_equal_at(prefix, start - length, root)) {
            ++exponent;
            start -= length;
        }
        if (exponent < evidence_threshold) continue;

        std::vector<ConcretePathPower> closed = backtrack(table, start);
        if (start != 0 && closed.empty()) continue;
        const std::size_t score =
            table.costs[start] + star_weight + length +
            exponent_weight * exponent_bits(exponent);
        candidates.push_back(
            {std::move(closed), std::move(root), exponent, score});
    }

    if (candidates.empty() && evidence_threshold > 1) {
        return compress_path_prefix(prefix, max_root_length, beam_width, 1);
    }

    std::sort(candidates.begin(), candidates.end(), CandidateOrder{});
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());
    if (candidates.size() > beam_width) candidates.resize(beam_width);
    return candidates;
}

std::string render_path_word(const PathWord& word) {
    std::ostringstream out;
    for (std::size_t i = 0; i < word.size(); ++i) {
        if (i != 0) out << '.';
        out << 'p' << word[i];
    }
    return out.str();
}

std::string render_path_schema(const PathSchemaCandidate& candidate) {
    std::ostringstream out;
    bool first = true;
    for (const ConcretePathPower& power : candidate.closed_prefix) {
        if (!first) out << ' ';
        out << '(' << render_path_word(power.root) << ")^" << power.exponent;
        first = false;
    }
    if (!first) out << ' ';
    out << '(' << render_path_word(candidate.star) << ")^*";
    return out.str();
}

} // namespace ari_exe
