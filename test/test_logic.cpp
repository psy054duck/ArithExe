#include "gtest/gtest.h"
#include "z3++.h"

#include "logics.h"

using namespace ari_exe;

TEST(EQ_FINDING, test_1) {
    auto z3_ctx = z3::context();
    z3::expr x = z3_ctx.int_const("x");
    z3::expr y = z3_ctx.int_const("y");
    z3::expr z = z3_ctx.int_const("z");
    z3::expr constraints = (-x + y + 2 >= 0 && -y + z + 4 >= 0 && x - z - 6 >= 0);
    LinearLogic logic;
    auto x_value = logic.solve_var(constraints, x);
    auto y_value = logic.solve_var(constraints, y);
    auto z_value = logic.solve_var(constraints, z);
    ASSERT_TRUE(x_value.has_value());
    ASSERT_TRUE(y_value.has_value());
    ASSERT_TRUE(z_value.has_value());
    z3::solver s(z3_ctx);
    s.add(constraints);
    std::vector<std::pair<z3::expr, z3::expr>> pairs = {
        {x, x_value.value()},
        {y, y_value.value()},
        {z, z_value.value()}
    };
    for (auto& p : pairs) {
        s.push();
        s.add(p.first != p.second);
        auto res = s.check();
        EXPECT_EQ(res, z3::unsat) << "Failed on: " << p.first << " == " << p.second;
        s.pop();
    }
}