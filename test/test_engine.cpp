#include <gtest/gtest.h>
#include <string>
#include "engine.h"
#include "AInstruction.h"
#include "z3++.h"

using namespace ari_exe;

namespace {
#ifndef ARITHEXE_TEST_BENCHMARK_DIR
#define ARITHEXE_TEST_BENCHMARK_DIR "../../test/benchmark"
#endif

std::string benchmark_path(const std::string& relative_path) {
    return std::string(ARITHEXE_TEST_BENCHMARK_DIR) + "/" + relative_path;
}

struct BenchmarkRun {
    VeriResult result;
    bool has_issue;
    VerifierIssueKind issue_kind;
    std::string issue_message;
};

void reset_test_caches() {
    for (auto& pair : AInstruction::cached_instructions) {
        delete pair.second;
    }
    AInstruction::cached_instructions.clear();
    AInstructionPhi::failed_loops.clear();
    AnalysisManager::unknown_counter = 0;

    delete State::func_summaries;
    State::func_summaries = new SymbolTable<FunctionSummary>();
    delete State::loop_summaries;
    State::loop_summaries = new SymbolTable<LoopSummary>();
}

BenchmarkRun run_benchmark(const std::string& relative_path) {
    reset_test_caches();
    auto engine = Engine(benchmark_path(relative_path));
    auto veri_res = engine.verify();
    BenchmarkRun run{
        veri_res,
        engine.has_issue(),
        engine.has_issue() ? engine.get_issue_kind()
                           : VerifierIssueKind::UnknownState,
        engine.get_issue_message(),
    };
    reset_test_caches();
    return run;
}

VeriResult verify_benchmark(const std::string& relative_path) {
    return run_benchmark(relative_path).result;
}

void run_bounded_cfinite_benchmark(const std::string& filename) {
    auto veri_res = verify_benchmark("bounded-cfinite/" + filename);
    EXPECT_EQ(veri_res, HOLD) << "Failed on: test/bounded-cfinite/" << filename;
}
}

TEST(BENCHMARK_ARRAYS_LOOP, true_1) {
    auto veri_res = verify_benchmark("arrays/loop/true_1.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/arrays/loop/true_1.c";
}

TEST(BENCHMARK_ARRAYS_LOOP, true_2) {
    auto veri_res = verify_benchmark("arrays/loop/true_2.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/arrays/loop/true_2.c";
}

TEST(BENCHMARK_ARRAYS_LOOP, true_3) {
    auto veri_res = verify_benchmark("arrays/loop/true_3.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/arrays/loop/true_3.c";
}

TEST(BENCHMARK_ARRAYS_LOOP_FREE, false_1) {
    auto veri_res = verify_benchmark("arrays/loop_free/false_1.c");
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/arrays/loop_free/false_1.c";
}

TEST(BENCHMARK_ARRAYS_LOOP_FREE, false_2) {
    auto veri_res = verify_benchmark("arrays/loop_free/false_2.c");
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/arrays/loop_free/false_2.c";
}

TEST(BENCHMARK_ARRAYS_LOOP_FREE, true_1) {
    auto veri_res = verify_benchmark("arrays/loop_free/true_1.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/arrays/loop_free/true_1.c";
}

TEST(BENCHMARK_ARRAYS_LOOP_FREE, true_2) {
    auto veri_res = verify_benchmark("arrays/loop_free/true_2.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/arrays/loop_free/true_2.c";
}

TEST(BENCHMARK_NLA, true_1) {
    auto veri_res = verify_benchmark("nla/true_1.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/nla/true_1.c";
}

TEST(BENCHMARK_NLA, true_2) {
    auto veri_res = verify_benchmark("nla/true_2.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/nla/true_2.c";
}

TEST(BENCHMARK_NLA, true_3) {
    auto veri_res = verify_benchmark("nla/true_3.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/nla/true_3.c";
}

TEST(BENCHMARK_NLA, true_4) {
    auto veri_res = verify_benchmark("nla/true_4.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/nla/true_4.c";
}

TEST(BENCHMARK_LOOPS, true_bounded_cfinite_map_fixed) {
    auto veri_res = verify_benchmark("loops/true_bounded_cfinite_map_fixed.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_bounded_cfinite_map_fixed.c";
}

TEST(BENCHMARK_LOOPS, true_bounded_cfinite_map_symbolic) {
    auto veri_res = verify_benchmark("loops/true_bounded_cfinite_map_symbolic.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_bounded_cfinite_map_symbolic.c";
}

TEST(BENCHMARK_LOOPS, true_nested_bounded_cfinite) {
    auto veri_res = verify_benchmark("loops/true_nested_bounded_cfinite.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_nested_bounded_cfinite.c";
}

TEST(BENCHMARK_LOOP_FREE, false_1) {
    auto veri_res = verify_benchmark("loop_free/false_1.c");
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/loop_free/false_1.c";
}

TEST(BENCHMARK_LOOP_FREE, false_2) {
    auto veri_res = verify_benchmark("loop_free/false_2.c");
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/loop_free/false_2.c";
}

TEST(BENCHMARK_LOOP_FREE, true_1) {
    auto veri_res = verify_benchmark("loop_free/true_1.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loop_free/true_1.c";
}

TEST(BENCHMARK_LOOP_FREE, true_2) {
    auto veri_res = verify_benchmark("loop_free/true_2.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loop_free/true_2.c";
}

TEST(BENCHMARK_LOOP_FREE, true_3) {
    auto veri_res = verify_benchmark("loop_free/true_3.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loop_free/true_3.c";
}

TEST(BENCHMARK_LOOP_FREE, true_4) {
    auto veri_res = verify_benchmark("loop_free/true_4.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loop_free/true_4.c";
}

TEST(BENCHMARK_LOOPS, true_1) {
    auto veri_res = verify_benchmark("loops/true_1.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_1.c";
}

TEST(BENCHMARK_LOOPS, true_10) {
    auto veri_res = verify_benchmark("loops/true_10.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_10.c";
}

TEST(BENCHMARK_LOOPS, true_11) {
    auto veri_res = verify_benchmark("loops/true_11.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_11.c";
}

TEST(BENCHMARK_LOOPS, true_12) {
    auto veri_res = verify_benchmark("loops/true_12.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_12.c";
}

TEST(BENCHMARK_LOOPS, true_nested_affine_dependent) {
    auto veri_res = verify_benchmark("loops/true_nested_affine_dependent.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_nested_affine_dependent.c";
}

TEST(BENCHMARK_LOOPS, true_2) {
    auto veri_res = verify_benchmark("loops/true_2.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_2.c";
}

TEST(BENCHMARK_LOOPS, true_3) {
    auto veri_res = verify_benchmark("loops/true_3.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_3.c";
}

TEST(BENCHMARK_LOOPS, true_4) {
    auto veri_res = verify_benchmark("loops/true_4.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_4.c";
}

TEST(BENCHMARK_LOOPS, true_5) {
    auto veri_res = verify_benchmark("loops/true_5.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_5.c";
}

TEST(BENCHMARK_LOOPS, true_6) {
    auto veri_res = verify_benchmark("loops/true_6.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_6.c";
}

TEST(BENCHMARK_LOOPS, true_7) {
    auto veri_res = verify_benchmark("loops/true_7.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_7.c";
}

TEST(BENCHMARK_LOOPS, true_8) {
    auto veri_res = verify_benchmark("loops/true_8.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_8.c";
}

TEST(BENCHMARK_LOOPS, true_9) {
    auto veri_res = verify_benchmark("loops/true_9.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/loops/true_9.c";
}

TEST(BENCHMARK_PTR, false_1) {
    auto veri_res = verify_benchmark("ptr/false_1.c");
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/ptr/false_1.c";
}

TEST(BENCHMARK_PTR, true_1) {
    auto veri_res = verify_benchmark("ptr/true_1.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/ptr/true_1.c";
}

TEST(BENCHMARK_RECURSION, false_1) {
    auto veri_res = verify_benchmark("recursion/false_1.c");
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/recursion/false_1.c";
}

TEST(BENCHMARK_RECURSION, false_2) {
    auto veri_res = verify_benchmark("recursion/false_2.c");
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/recursion/false_2.c";
}

TEST(BENCHMARK_RECURSION, false_3) {
    auto veri_res = verify_benchmark("recursion/false_3.c");
    EXPECT_EQ(veri_res, FAIL) << "Failed on: benchmark/recursion/false_3.c";
}

TEST(BENCHMARK_RECURSION, true_1) {
    auto veri_res = verify_benchmark("recursion/true_1.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_1.c";
}

TEST(BENCHMARK_RECURSION, true_10) {
    auto veri_res = verify_benchmark("recursion/true_10.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_10.c";
}

TEST(BENCHMARK_RECURSION, true_11) {
    auto veri_res = verify_benchmark("recursion/true_11.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_11.c";
}

TEST(BENCHMARK_RECURSION, true_12) {
    auto veri_res = verify_benchmark("recursion/true_12.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_12.c";
}

TEST(BENCHMARK_RECURSION, true_13) {
    auto veri_res = verify_benchmark("recursion/true_13.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_13.c";
}

TEST(BENCHMARK_RECURSION, true_14) {
    auto veri_res = verify_benchmark("recursion/true_14.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_14.c";
}

TEST(BENCHMARK_RECURSION, true_15) {
    auto veri_res = verify_benchmark("recursion/true_15.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_15.c";
}

TEST(BENCHMARK_RECURSION, true_16) {
    auto veri_res = verify_benchmark("recursion/true_16.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_16.c";
}

TEST(BENCHMARK_RECURSION, true_17) {
    auto veri_res = verify_benchmark("recursion/true_17.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_17.c";
}

TEST(BENCHMARK_RECURSION, true_2) {
    auto veri_res = verify_benchmark("recursion/true_2.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_2.c";
}

TEST(BENCHMARK_RECURSION, true_3) {
    auto veri_res = verify_benchmark("recursion/true_3.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_3.c";
}

TEST(BENCHMARK_RECURSION, true_7) {
    auto veri_res = verify_benchmark("recursion/true_7.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_7.c";
}

TEST(BENCHMARK_RECURSION, true_8) {
    auto veri_res = verify_benchmark("recursion/true_8.c");
    EXPECT_EQ(veri_res, HOLD) << "Failed on: benchmark/recursion/true_8.c";
}

TEST(BENCHMARK_RECURSION, true_9) {
    auto veri_res = verify_benchmark("recursion/true_9.c");
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
