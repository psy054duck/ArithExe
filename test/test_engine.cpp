#include <gtest/gtest.h>
#include <string>
#include "engine.h"
#include "z3++.h"

using namespace ari_exe;

TEST(BENCHMARK_RECURSION, false_1) {
    auto engine = Engine("../../test/benchmark/recursion/false_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::FAIL) << "Failed on: benchmark/recursion/false_1.c";
}

TEST(BENCHMARK_RECURSION, true_1) {
    auto engine = Engine("../../test/benchmark/recursion/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_1.c";
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

TEST(BENCHMARK_ARRAYS_LOOP, true_1) {
    auto engine = Engine("../../test/benchmark/arrays/loop/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/arrays/loop/true_1.c";
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

TEST(BENCHMARK_LOOPS, true_9) {
    auto engine = Engine("../../test/benchmark/loops/true_9.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_9.c";
}
