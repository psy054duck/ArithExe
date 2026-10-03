#include <gtest/gtest.h>

#include "PathExpression.h"

#include <algorithm>

using namespace ari_exe;

namespace {

PathWord with_star_evidence(const PathSchemaCandidate& candidate) {
    auto evidence = candidate.closed_prefix;
    evidence.push_back(
        {candidate.star, candidate.observed_star_exponent});
    return expand_path_powers(evidence);
}

} // namespace

TEST(PathExpressionCompression, PrimitiveWordRecognition) {
    EXPECT_TRUE(is_primitive_path_word({1}));
    EXPECT_TRUE(is_primitive_path_word({1, 2}));
    EXPECT_TRUE(is_primitive_path_word({1, 1, 2}));
    EXPECT_FALSE(is_primitive_path_word({}));
    EXPECT_FALSE(is_primitive_path_word({1, 1}));
    EXPECT_FALSE(is_primitive_path_word({1, 2, 1, 2}));
}

TEST(PathExpressionCompression, EvidenceAlwaysRoundTrips) {
    const PathWord prefix{1, 1, 1, 2, 2, 2, 1, 2, 1, 2, 1};
    const auto candidates = compress_path_prefix(prefix, 4, 16, 2);
    ASSERT_FALSE(candidates.empty());
    for (const auto& candidate : candidates) {
        EXPECT_EQ(with_star_evidence(candidate), prefix);
        EXPECT_TRUE(is_primitive_path_word(candidate.star));
        EXPECT_LE(candidate.star.size(), 4u);
    }
}

TEST(PathExpressionCompression, FindsDocumentedAaabbbababaFactorization) {
    // aaabbbababa = a^3 b^2 (ba)^3
    const PathWord prefix{1, 1, 1, 2, 2, 2, 1, 2, 1, 2, 1};
    const auto candidates = compress_path_prefix(prefix, 2, 16, 2);
    const auto found = std::find_if(
        candidates.begin(), candidates.end(),
        [](const PathSchemaCandidate& candidate) {
            return candidate.star == PathWord{2, 1} &&
                   candidate.observed_star_exponent == 3 &&
                   candidate.closed_prefix ==
                       std::vector<ConcretePathPower>{{{1}, 3}, {{2}, 2}};
        });
    ASSERT_NE(found, candidates.end());
    EXPECT_EQ(render_path_schema(*found), "(p1)^3 (p2)^2 (p2.p1)^*");
}

TEST(PathExpressionCompression, FallsBackToOneObservedCopy) {
    const PathWord prefix{1, 2, 3};
    const auto candidates = compress_path_prefix(prefix, 2, 4, 3);
    ASSERT_FALSE(candidates.empty());
    EXPECT_EQ(with_star_evidence(candidates.front()), prefix);
    EXPECT_EQ(candidates.front().observed_star_exponent, 1u);
}

TEST(PathExpressionCompression, BeamIsDeterministicAndBounded) {
    const PathWord prefix{1, 2, 1, 2, 1, 2};
    const auto first = compress_path_prefix(prefix, 4, 2, 1);
    const auto second = compress_path_prefix(prefix, 4, 2, 1);
    EXPECT_EQ(first, second);
    EXPECT_LE(first.size(), 2u);
}
