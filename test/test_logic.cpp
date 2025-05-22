#include "gtest/gtest.h"
#include "z3++.h"

#include "logics.h"

using namespace ari_exe;

TEST(EQ_FINDING, test_1) {
    auto z3_ctx = z3::context();
    z3::expr x = z3_ctx.int_const("x");
    z3::expr y = z3_ctx.int_const("y");
    z3::expr z = z3_ctx.int_const("z");
    z3::expr constraints = (-x + y + 2 >= 0 && -y + z + 4 >= 0 && x - z - 6 >= 0 && z == 10);
    LinearLogic logic;

    z3::expr_vector vars(z3_ctx);
    vars.push_back(x);
    vars.push_back(y);
    vars.push_back(z);
    auto values = logic.solve_vars(constraints, vars);
    ASSERT_TRUE(values.size() > 0);
    z3::solver s(z3_ctx);
    s.add(constraints);
    for (int i = 0; i < values.size(); i++) {
        s.push();
        s.add(vars[i] != values[i]);
        auto res = s.check();
        EXPECT_EQ(res, z3::unsat) << "Failed on: " << vars[i].to_string() << " == " << values[i].to_string();
        s.pop();
    }
}