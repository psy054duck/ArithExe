#include <gtest/gtest.h>
#include <string>
#include "engine.h"
#include "z3++.h"

using namespace ari_exe;

TEST(RECURSION, false_1) {
    auto engine = Engine("../../test/benchmark/recursion/false_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::FAIL) << "Failed on: benchmark/recursion/false_1.c";
}

TEST(RECURSION, true_1) {
    auto engine = Engine("../../test/benchmark/recursion/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_1.c";
}

TEST(LOOP_FREE, false_1) {
    auto engine = Engine("../../test/benchmark/loop_free/false_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::FAIL) << "Failed on: benchmark/loop_free/false_1.c";
}

TEST(LOOP_FREE, false_2) {
    auto engine = Engine("../../test/benchmark/loop_free/false_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::FAIL) << "Failed on: benchmark/loop_free/false_2.c";
}

TEST(LOOP_FREE, true_1) {
    auto engine = Engine("../../test/benchmark/loop_free/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loop_free/true_1.c";
}

TEST(LOOP_FREE, true_2) {
    auto engine = Engine("../../test/benchmark/loop_free/true_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loop_free/true_2.c";
}

TEST(LOOP_FREE, true_3) {
    auto engine = Engine("../../test/benchmark/loop_free/true_3.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loop_free/true_3.c";
}

TEST(LOOP_FREE, true_4) {
    auto engine = Engine("../../test/benchmark/loop_free/true_4.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loop_free/true_4.c";
}

TEST(LOOPS, true_1) {
    auto engine = Engine("../../test/benchmark/loops/true_1.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_1.c";
}

TEST(LOOPS, true_2) {
    auto engine = Engine("../../test/benchmark/loops/true_2.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_2.c";
}

TEST(LOOPS, true_3) {
    auto engine = Engine("../../test/benchmark/loops/true_3.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_3.c";
}

TEST(LOOPS, true_4) {
    auto engine = Engine("../../test/benchmark/loops/true_4.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_4.c";
}

TEST(LOOPS, true_5) {
    auto engine = Engine("../../test/benchmark/loops/true_5.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_5.c";
}

TEST(LOOPS, true_6) {
    auto engine = Engine("../../test/benchmark/loops/true_6.c");
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loops/true_6.c";
}
