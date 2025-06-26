#include <gtest/gtest.h>
#include <string>
#include "engine.h"
#include "z3++.h"

using namespace ari_exe;

TEST(BENCHMARK_ARRAYS_LOOP, true_1) {
    auto engine = Engine("../../test/benchmark/arrays/loop/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/arrays/loop/true_1.c";
}

TEST(BENCHMARK_ARRAYS_LOOP, true_2) {
    auto engine = Engine("../../test/benchmark/arrays/loop/true_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/arrays/loop/true_2.c";
}

TEST(BENCHMARK_ARRAYS_LOOP, true_3) {
    auto engine = Engine("../../test/benchmark/arrays/loop/true_3.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/arrays/loop/true_3.c";
}

TEST(BENCHMARK_ARRAYS_LOOP_FREE, false_1) {
    auto engine = Engine("../../test/benchmark/arrays/loop_free/false_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::FAIL) << "Failed on: benchmark/arrays/loop_free/false_1.c";
}

TEST(BENCHMARK_ARRAYS_LOOP_FREE, false_2) {
    auto engine = Engine("../../test/benchmark/arrays/loop_free/false_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::FAIL) << "Failed on: benchmark/arrays/loop_free/false_2.c";
}

TEST(BENCHMARK_ARRAYS_LOOP_FREE, true_1) {
    auto engine = Engine("../../test/benchmark/arrays/loop_free/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/arrays/loop_free/true_1.c";
}

TEST(BENCHMARK_ARRAYS_LOOP_FREE, true_2) {
    auto engine = Engine("../../test/benchmark/arrays/loop_free/true_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/arrays/loop_free/true_2.c";
}

TEST(BENCHMARK_NLA, true_1) {
    auto engine = Engine("../../test/benchmark/nla/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/nla/true_1.c";
}

TEST(BENCHMARK_NLA, true_2) {
    auto engine = Engine("../../test/benchmark/nla/true_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/nla/true_2.c";
}

TEST(BENCHMARK_NLA, true_3) {
    auto engine = Engine("../../test/benchmark/nla/true_3.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/nla/true_3.c";
}

TEST(BENCHMARK_NLA, true_4) {
    auto engine = Engine("../../test/benchmark/nla/true_4.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/nla/true_4.c";
}

TEST(BENCHMARK_LOOP_FREE, false_1) {
    auto engine = Engine("../../test/benchmark/loop_free/false_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::FAIL) << "Failed on: benchmark/loop_free/false_1.c";
}

TEST(BENCHMARK_LOOP_FREE, false_2) {
    auto engine = Engine("../../test/benchmark/loop_free/false_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::FAIL) << "Failed on: benchmark/loop_free/false_2.c";
}

TEST(BENCHMARK_LOOP_FREE, true_1) {
    auto engine = Engine("../../test/benchmark/loop_free/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loop_free/true_1.c";
}

TEST(BENCHMARK_LOOP_FREE, true_2) {
    auto engine = Engine("../../test/benchmark/loop_free/true_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loop_free/true_2.c";
}

TEST(BENCHMARK_LOOP_FREE, true_3) {
    auto engine = Engine("../../test/benchmark/loop_free/true_3.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loop_free/true_3.c";
}

TEST(BENCHMARK_LOOP_FREE, true_4) {
    auto engine = Engine("../../test/benchmark/loop_free/true_4.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loop_free/true_4.c";
}

TEST(BENCHMARK_LOOPS, true_1) {
    auto engine = Engine("../../test/benchmark/loops/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_1.c";
}

TEST(BENCHMARK_LOOPS, true_10) {
    auto engine = Engine("../../test/benchmark/loops/true_10.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_10.c";
}

TEST(BENCHMARK_LOOPS, true_11) {
    auto engine = Engine("../../test/benchmark/loops/true_11.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_11.c";
}

TEST(BENCHMARK_LOOPS, true_12) {
    auto engine = Engine("../../test/benchmark/loops/true_12.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_12.c";
}

TEST(BENCHMARK_LOOPS, true_2) {
    auto engine = Engine("../../test/benchmark/loops/true_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_2.c";
}

TEST(BENCHMARK_LOOPS, true_3) {
    auto engine = Engine("../../test/benchmark/loops/true_3.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_3.c";
}

TEST(BENCHMARK_LOOPS, true_4) {
    auto engine = Engine("../../test/benchmark/loops/true_4.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_4.c";
}

TEST(BENCHMARK_LOOPS, true_5) {
    auto engine = Engine("../../test/benchmark/loops/true_5.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_5.c";
}

TEST(BENCHMARK_LOOPS, true_6) {
    auto engine = Engine("../../test/benchmark/loops/true_6.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_6.c";
}

TEST(BENCHMARK_LOOPS, true_7) {
    auto engine = Engine("../../test/benchmark/loops/true_7.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_7.c";
}

TEST(BENCHMARK_LOOPS, true_8) {
    auto engine = Engine("../../test/benchmark/loops/true_8.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_8.c";
}

TEST(BENCHMARK_LOOPS, true_9) {
    auto engine = Engine("../../test/benchmark/loops/true_9.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_9.c";
}

TEST(BENCHMARK_PTR, false_1) {
    auto engine = Engine("../../test/benchmark/ptr/false_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::FAIL) << "Failed on: benchmark/ptr/false_1.c";
}

TEST(BENCHMARK_PTR, true_1) {
    auto engine = Engine("../../test/benchmark/ptr/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/ptr/true_1.c";
}

TEST(BENCHMARK_RECURSION, false_1) {
    auto engine = Engine("../../test/benchmark/recursion/false_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::FAIL) << "Failed on: benchmark/recursion/false_1.c";
}

TEST(BENCHMARK_RECURSION, false_2) {
    auto engine = Engine("../../test/benchmark/recursion/false_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::FAIL) << "Failed on: benchmark/recursion/false_2.c";
}

TEST(BENCHMARK_RECURSION, false_3) {
    auto engine = Engine("../../test/benchmark/recursion/false_3.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::FAIL) << "Failed on: benchmark/recursion/false_3.c";
}

TEST(BENCHMARK_RECURSION, true_1) {
    auto engine = Engine("../../test/benchmark/recursion/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_1.c";
}

TEST(BENCHMARK_RECURSION, true_10) {
    auto engine = Engine("../../test/benchmark/recursion/true_10.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_10.c";
}

TEST(BENCHMARK_RECURSION, true_11) {
    auto engine = Engine("../../test/benchmark/recursion/true_11.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_11.c";
}

TEST(BENCHMARK_RECURSION, true_12) {
    auto engine = Engine("../../test/benchmark/recursion/true_12.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_12.c";
}

TEST(BENCHMARK_RECURSION, true_13) {
    auto engine = Engine("../../test/benchmark/recursion/true_13.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_13.c";
}

TEST(BENCHMARK_RECURSION, true_14) {
    auto engine = Engine("../../test/benchmark/recursion/true_14.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_14.c";
}

TEST(BENCHMARK_RECURSION, true_15) {
    auto engine = Engine("../../test/benchmark/recursion/true_15.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_15.c";
}

TEST(BENCHMARK_RECURSION, true_16) {
    auto engine = Engine("../../test/benchmark/recursion/true_16.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_16.c";
}

TEST(BENCHMARK_RECURSION, true_17) {
    auto engine = Engine("../../test/benchmark/recursion/true_17.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_17.c";
}

TEST(BENCHMARK_RECURSION, true_2) {
    auto engine = Engine("../../test/benchmark/recursion/true_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_2.c";
}

TEST(BENCHMARK_RECURSION, true_3) {
    auto engine = Engine("../../test/benchmark/recursion/true_3.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_3.c";
}

TEST(BENCHMARK_RECURSION, true_7) {
    auto engine = Engine("../../test/benchmark/recursion/true_7.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_7.c";
}

TEST(BENCHMARK_RECURSION, true_8) {
    auto engine = Engine("../../test/benchmark/recursion/true_8.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_8.c";
}

TEST(BENCHMARK_RECURSION, true_9) {
    auto engine = Engine("../../test/benchmark/recursion/true_9.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_9.c";
}
