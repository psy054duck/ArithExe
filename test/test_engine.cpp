#include <gtest/gtest.h>
#include <string>
#include "engine.h"
#include "z3++.h"

using namespace ari_exe;

TEST(RECURSION, false_1) {
    z3::context z3ctx;
    auto engine = Engine("../../test/benchmark/recursion/false_1.c", z3ctx);
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::FAIL) << "Failed on: benchmark/recursion/false_1.c";
}

TEST(RECURSION, true_1) {
    z3::context z3ctx;
    auto engine = Engine("../../test/benchmark/recursion/true_1.c", z3ctx);
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/recursion/true_1.c";
}

TEST(LOOP_FREE, false_1) {
    z3::context z3ctx;
    auto engine = Engine("../../test/benchmark/loop_free/false_1.c", z3ctx);
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::FAIL) << "Failed on: benchmark/loop_free/false_1.c";
}

TEST(LOOP_FREE, false_2) {
    z3::context z3ctx;
    auto engine = Engine("../../test/benchmark/loop_free/false_2.c", z3ctx);
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::FAIL) << "Failed on: benchmark/loop_free/false_2.c";
}

TEST(LOOP_FREE, true_1) {
    z3::context z3ctx;
    auto engine = Engine("../../test/benchmark/loop_free/true_1.c", z3ctx);
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loop_free/true_1.c";
}

TEST(LOOP_FREE, true_2) {
    z3::context z3ctx;
    auto engine = Engine("../../test/benchmark/loop_free/true_2.c", z3ctx);
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loop_free/true_2.c";
}

TEST(LOOP_FREE, true_3) {
    z3::context z3ctx;
    auto engine = Engine("../../test/benchmark/loop_free/true_3.c", z3ctx);
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loop_free/true_3.c";
}

TEST(LOOP_FREE, true_4) {
    z3::context z3ctx;
    auto engine = Engine("../../test/benchmark/loop_free/true_4.c", z3ctx);
    auto veri_res = engine.verify();
    EXPECT_EQ(veri_res, Engine::HOLD) << "Failed on: benchmark/loop_free/true_4.c";
}
