#include "gtest/gtest.h"
#include "z3++.h"

#include "logics.h"

using namespace ari_exe;

TEST(ITE2PIECEWISE, PreservesOriginalFreshAndBangNamedConstants) {
    z3::context c;
    auto x = c.int_const("x!program"), y = c.int_const("y");
    auto choice = z3::expr(c, Z3_mk_fresh_const(c, "program_choice", c.bool_sort()));
    auto formula = z3::ite(choice, x, y) == 3 && x >= 0;
    auto result = eliminate_ite(formula);
    EXPECT_TRUE(is_equivalent(result, formula));
    z3::solver solver(c);
    solver.add(result && choice && x != 3);
    EXPECT_EQ(solver.check(), z3::unsat);
}

TEST(ITE2PIECEWISE, AuxiliaryIdentificationUsesInputAstNotNames) {
    z3::context c;
    auto original = c.int_const("original!0");
    auto introduced = c.int_const("plain_aux");
    Logic logic;
    auto auxiliaries = logic.collect_aux_vars(original == introduced, original >= 0);
    ASSERT_EQ(auxiliaries.size(), 1u);
    EXPECT_TRUE(z3::eq(auxiliaries[0], introduced));
}

TEST(ITE2PIECEWISE, PreservesFreeSymbolsInsideQuantifiers) {
    z3::context c;
    auto x = c.int_const("input!0"), t = c.int_const("t");
    auto b = c.bool_const("choice!0");
    auto formula = z3::forall(t, z3::ite(b, x, x + 1) >= t || t > 5);
    EXPECT_TRUE(is_equivalent(eliminate_ite(formula), formula));
}

TEST(EQ_FINDING, InfersUniqueIntegerCountFromStrictBounds) {
    z3::context c;
    auto k = c.int_const("k"), n = c.int_const("n");
    auto constraints = k >= 1 && k < n && k > n - 2;
    z3::expr_vector variables(c);
    variables.push_back(k);
    LinearLogic logic;
    auto values = logic.solve_vars(constraints, variables);
    ASSERT_EQ(values.size(), 1u);
    z3::solver solver(c);
    solver.add(constraints && values[0] != n - 1);
    EXPECT_EQ(solver.check(), z3::unsat);
}

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

TEST(ITE2PIECEWISE, test_1) {
    auto z3_ctx = z3::context();
    z3::expr x = z3_ctx.int_const("x");
    z3::expr y = z3_ctx.int_const("y");
    z3::expr cond = x > 0;

    z3::expr expr = z3::ite(cond, x + 1, y - 1);

    auto solver = z3::solver(z3_ctx);

    auto [conditions, expressions] = expr2piecewise(expr);
    
    ASSERT_EQ(conditions.size(), 2);
    ASSERT_EQ(expressions.size(), 2);
    
    EXPECT_TRUE(is_equivalent(conditions[0], cond));
    EXPECT_EQ(expressions[0], x + 1);
    
    EXPECT_TRUE(is_equivalent(conditions[1], !cond));
    EXPECT_EQ(expressions[1], y - 1);

    z3::expr expr2 = z3::ite(y < 0, y + 3, expr);
    auto [conditions2, expressions2] = expr2piecewise(expr2);

    ASSERT_EQ(conditions2.size(), 3);
    ASSERT_EQ(expressions2.size(), 3);

    EXPECT_TRUE(is_equivalent(conditions2[0], y < 0));
    EXPECT_TRUE(is_equivalent(conditions2[1], y >= 0 && cond));

    EXPECT_EQ(expressions2[0], y + 3);
    EXPECT_EQ(expressions2[1], x + 1);

    z3::expr ite_expr = piecewise2ite(conditions2, expressions2);
    EXPECT_TRUE(is_equivalent(ite_expr, expr2));
}
