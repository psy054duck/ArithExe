#include <gtest/gtest.h>
#include <string>
#include "engine.h"
#include "AInstruction.h"
#include "z3++.h"

using namespace ari_exe;

namespace {
void reset_test_caches() {
    for (auto& pair : AInstruction::cached_instructions) {
        delete pair.second;
    }
    AInstruction::cached_instructions.clear();
    AInstructionPhi::failed_loops.clear();

    delete State::func_summaries;
    State::func_summaries = new SymbolTable<FunctionSummary>();
    delete State::loop_summaries;
    State::loop_summaries = new SymbolTable<LoopSummary>();
}

void run_bounded_cfinite_benchmark(const std::string& filename) {
    reset_test_caches();
    auto engine = Engine("../../test/benchmark/bounded-cfinite/" + filename);
    auto veri_res = engine.verify();
    reset_test_caches();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: test/bounded-cfinite/" << filename;
}
}

TEST(BENCHMARK_ARRAYS_LOOP, true_1) {
    auto engine = Engine("../../test/benchmark/arrays/loop/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/arrays/loop/true_1.c";
}

TEST(BENCHMARK_ARRAYS_LOOP, true_2) {
    auto engine = Engine("../../test/benchmark/arrays/loop/true_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/arrays/loop/true_2.c";
}

TEST(BENCHMARK_ARRAYS_LOOP, true_3) {
    auto engine = Engine("../../test/benchmark/arrays/loop/true_3.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/arrays/loop/true_3.c";
}

TEST(BENCHMARK_ARRAYS_LOOP_FREE, false_1) {
    auto engine = Engine("../../test/benchmark/arrays/loop_free/false_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/arrays/loop_free/false_1.c";
}

TEST(BENCHMARK_ARRAYS_LOOP_FREE, false_2) {
    auto engine = Engine("../../test/benchmark/arrays/loop_free/false_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/arrays/loop_free/false_2.c";
}

TEST(BENCHMARK_ARRAYS_LOOP_FREE, true_1) {
    auto engine = Engine("../../test/benchmark/arrays/loop_free/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/arrays/loop_free/true_1.c";
}

TEST(BENCHMARK_ARRAYS_LOOP_FREE, true_2) {
    auto engine = Engine("../../test/benchmark/arrays/loop_free/true_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/arrays/loop_free/true_2.c";
}

TEST(BENCHMARK_NLA, true_1) {
    auto engine = Engine("../../test/benchmark/nla/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/nla/true_1.c";
}

TEST(BENCHMARK_NLA, true_2) {
    auto engine = Engine("../../test/benchmark/nla/true_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/nla/true_2.c";
}

TEST(BENCHMARK_NLA, true_3) {
    auto engine = Engine("../../test/benchmark/nla/true_3.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/nla/true_3.c";
}

TEST(BENCHMARK_NLA, true_4) {
    auto engine = Engine("../../test/benchmark/nla/true_4.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/nla/true_4.c";
}

TEST(BENCHMARK_NLA, true_bounded_cfinite_map_fixed) {
    auto engine = Engine("../../test/benchmark/nla/true_bounded_cfinite_map_fixed.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/nla/true_bounded_cfinite_map_fixed.c";
}

TEST(BENCHMARK_NLA, true_bounded_cfinite_map_symbolic) {
    auto engine = Engine("../../test/benchmark/nla/true_bounded_cfinite_map_symbolic.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/nla/true_bounded_cfinite_map_symbolic.c";
}

TEST(BENCHMARK_NLA, true_nested_bounded_cfinite) {
    auto engine = Engine("../../test/benchmark/nla/true_nested_bounded_cfinite.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/nla/true_nested_bounded_cfinite.c";
}

TEST(BENCHMARK_LOOP_FREE, false_1) {
    auto engine = Engine("../../test/benchmark/loop_free/false_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/loop_free/false_1.c";
}

TEST(BENCHMARK_LOOP_FREE, false_2) {
    auto engine = Engine("../../test/benchmark/loop_free/false_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/loop_free/false_2.c";
}

TEST(BENCHMARK_LOOP_FREE, true_1) {
    auto engine = Engine("../../test/benchmark/loop_free/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loop_free/true_1.c";
}

TEST(BENCHMARK_LOOP_FREE, true_2) {
    auto engine = Engine("../../test/benchmark/loop_free/true_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loop_free/true_2.c";
}

TEST(BENCHMARK_LOOP_FREE, true_3) {
    auto engine = Engine("../../test/benchmark/loop_free/true_3.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loop_free/true_3.c";
}

TEST(BENCHMARK_LOOP_FREE, true_4) {
    auto engine = Engine("../../test/benchmark/loop_free/true_4.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loop_free/true_4.c";
}

TEST(BENCHMARK_LOOPS, true_1) {
    auto engine = Engine("../../test/benchmark/loops/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_1.c";
}

TEST(BENCHMARK_LOOPS, true_10) {
    auto engine = Engine("../../test/benchmark/loops/true_10.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_10.c";
}

TEST(BENCHMARK_LOOPS, true_11) {
    auto engine = Engine("../../test/benchmark/loops/true_11.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_11.c";
}

TEST(BENCHMARK_LOOPS, true_12) {
    auto engine = Engine("../../test/benchmark/loops/true_12.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_12.c";
}

TEST(BENCHMARK_LOOPS, true_nested_affine_dependent) {
    auto engine = Engine("../../test/benchmark/loops/true_nested_affine_dependent.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_nested_affine_dependent.c";
}

TEST(BENCHMARK_LOOPS, true_2) {
    auto engine = Engine("../../test/benchmark/loops/true_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_2.c";
}

TEST(BENCHMARK_LOOPS, true_3) {
    auto engine = Engine("../../test/benchmark/loops/true_3.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_3.c";
}

TEST(BENCHMARK_LOOPS, true_4) {
    auto engine = Engine("../../test/benchmark/loops/true_4.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_4.c";
}

TEST(BENCHMARK_LOOPS, true_5) {
    auto engine = Engine("../../test/benchmark/loops/true_5.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_5.c";
}

TEST(BENCHMARK_LOOPS, true_6) {
    auto engine = Engine("../../test/benchmark/loops/true_6.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_6.c";
}

TEST(BENCHMARK_LOOPS, true_7) {
    auto engine = Engine("../../test/benchmark/loops/true_7.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_7.c";
}

TEST(BENCHMARK_LOOPS, true_8) {
    auto engine = Engine("../../test/benchmark/loops/true_8.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_8.c";
}

TEST(BENCHMARK_LOOPS, true_9) {
    auto engine = Engine("../../test/benchmark/loops/true_9.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_9.c";
}

TEST(BENCHMARK_PTR, false_1) {
    auto engine = Engine("../../test/benchmark/ptr/false_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/ptr/false_1.c";
}

TEST(BENCHMARK_PTR, true_1) {
    auto engine = Engine("../../test/benchmark/ptr/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/ptr/true_1.c";
}

TEST(BENCHMARK_RECURSION, false_1) {
    auto engine = Engine("../../test/benchmark/recursion/false_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/recursion/false_1.c";
}

TEST(BENCHMARK_RECURSION, false_2) {
    auto engine = Engine("../../test/benchmark/recursion/false_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/recursion/false_2.c";
}

TEST(BENCHMARK_RECURSION, false_3) {
    auto engine = Engine("../../test/benchmark/recursion/false_3.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/recursion/false_3.c";
}

TEST(BENCHMARK_RECURSION, true_1) {
    auto engine = Engine("../../test/benchmark/recursion/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_1.c";
}

TEST(BENCHMARK_RECURSION, true_10) {
    auto engine = Engine("../../test/benchmark/recursion/true_10.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_10.c";
}

TEST(BENCHMARK_RECURSION, true_11) {
    auto engine = Engine("../../test/benchmark/recursion/true_11.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_11.c";
}

TEST(BENCHMARK_RECURSION, true_12) {
    auto engine = Engine("../../test/benchmark/recursion/true_12.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_12.c";
}

TEST(BENCHMARK_RECURSION, true_13) {
    auto engine = Engine("../../test/benchmark/recursion/true_13.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_13.c";
}

TEST(BENCHMARK_RECURSION, true_14) {
    auto engine = Engine("../../test/benchmark/recursion/true_14.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_14.c";
}

TEST(BENCHMARK_RECURSION, true_15) {
    auto engine = Engine("../../test/benchmark/recursion/true_15.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_15.c";
}

TEST(BENCHMARK_RECURSION, true_16) {
    auto engine = Engine("../../test/benchmark/recursion/true_16.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_16.c";
}

TEST(BENCHMARK_RECURSION, true_17) {
    auto engine = Engine("../../test/benchmark/recursion/true_17.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_17.c";
}

TEST(BENCHMARK_RECURSION, true_2) {
    auto engine = Engine("../../test/benchmark/recursion/true_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_2.c";
}

TEST(BENCHMARK_RECURSION, true_3) {
    auto engine = Engine("../../test/benchmark/recursion/true_3.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_3.c";
}

TEST(BENCHMARK_RECURSION, true_7) {
    auto engine = Engine("../../test/benchmark/recursion/true_7.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_7.c";
}

TEST(BENCHMARK_RECURSION, true_8) {
    auto engine = Engine("../../test/benchmark/recursion/true_8.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_8.c";
}

TEST(BENCHMARK_RECURSION, true_9) {
    auto engine = Engine("../../test/benchmark/recursion/true_9.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_9.c";
}

TEST(BENCHMARK_BOUNDED_CFINITE, bcf_01_square_invariant) {
    run_bounded_cfinite_benchmark("bcf_01_square_invariant_bmax.c");
}

TEST(BENCHMARK_BOUNDED_CFINITE, bcf_06_cubic_invariant) {
    run_bounded_cfinite_benchmark("bcf_06_cubic_invariant_bmax.c");
}

TEST(BENCHMARK_BOUNDED_CFINITE, bcf_07_mixed_quadratic_invariant) {
    run_bounded_cfinite_benchmark("bcf_07_mixed_quadratic_invariant_bmax.c");
}

TEST(BENCHMARK_BOUNDED_CFINITE, bcf_09_weighted_linear_phi) {
    run_bounded_cfinite_benchmark("bcf_09_weighted_linear_phi_bmax.c");
}

TEST(BENCHMARK_BOUNDED_CFINITE, bcf_10_quartic_invariant) {
    run_bounded_cfinite_benchmark("bcf_10_quartic_invariant_bmax.c");
}
