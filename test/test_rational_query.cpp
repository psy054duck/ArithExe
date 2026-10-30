#include <gtest/gtest.h>
#include "RationalQuery.h"
#include "QuantifiedQuery.h"
#include "logics.h"

static void equivalent(const z3::expr& original) {
    auto rewritten = ari_exe::integerize_rational_query(original);
    z3::solver solver(original.ctx());
    solver.set("timeout", 5000u);
    // Eliminate Presburger quantifiers on each side before the equivalence
    // check; raw quantified div XOR queries can themselves return unknown.
    auto eliminate = [&](const z3::expr& formula) {
        z3::goal goal(original.ctx());
        goal.add(formula);
        auto result = z3::tactic(original.ctx(), "qe")(goal);
        return result[0].as_expr();
    };
    solver.add(eliminate(original) != eliminate(rewritten));
    EXPECT_EQ(solver.check(), z3::unsat) << original << "\n" << rewritten;
}

TEST(RationalQuery, ExactPolynomialFractions) {
    z3::context c;
    auto x = c.int_const("x"), y = c.int_const("y");
    auto a = z3::to_real(x) * c.real_val("1/2") + z3::to_real(y) * c.real_val("-2/3");
    equivalent(a == c.real_val("5/7"));
    equivalent(a < z3::to_real(x));
    auto rewritten = ari_exe::integerize_rational_query(a <= c.real_val("1/3"));
    EXPECT_EQ(rewritten.to_string().find("to_real"), std::string::npos);
    equivalent(z3::to_real(x) / c.real_val(-2) >= c.real_val("1/3"));
    equivalent(z3::pw(z3::to_real(x) / c.real_val(2), 2) == z3::to_real(y));
}

TEST(RationalQuery, FreeWitnessesPreserveInputModels) {
    z3::context c;
    auto x = c.int_const("x");
    auto original = x == -3 && x / 2 == -2;
    z3::solver solver(c);
    solver.add(ari_exe::integerize_rational_query(original, false));
    ASSERT_EQ(solver.check(), z3::sat);
    EXPECT_EQ(solver.get_model().eval(x).get_numeral_int(), -3);
    solver.add(!original);
    EXPECT_EQ(solver.check(), z3::unsat);
}

TEST(RationalQuery, IntegerDivisionAndModulo) {
    z3::context c;
    auto x = c.int_const("x"), y = c.int_const("y");
    for (int d : {2, -2, 3, -3}) {
        equivalent(x / d == y);
        equivalent(z3::mod(x, c.int_val(d)) == y);
        equivalent(!(x / d < y));
    }
    equivalent(x == -3 && x / 2 == -2);
}

TEST(RationalQuery, QuantifierLocalQuotients) {
    z3::context c;
    auto x = c.int_const("x"), y = c.int_const("y");
    equivalent(z3::forall(x, x / 2 <= x));
    equivalent(!z3::exists(x, x / 3 == y));
    equivalent(!z3::exists(x, x / -3 == 2));
    equivalent(z3::forall(x, z3::exists(y, x / 2 == y)));
    equivalent(z3::forall(x, z3::mod(x, c.int_val(2)) >= 0));
}

TEST(RationalQuery, UnsupportedDivisionRetainsSemantics) {
    z3::context c;
    auto x = c.int_const("x"), y = c.int_const("y");
    auto r = c.real_const("r");
    equivalent(x / y == 1);
    equivalent(x / c.int_val(0) == 1);
    equivalent(r / c.real_val(2) < z3::to_real(x));
    equivalent(z3::to_real(x) / c.real_val(0) == r);
}

TEST(QuantifiedQuery, ProvesWatermelonResidualImpossible) {
    z3::context c;
    auto w = c.int_const("w"), k = c.int_const("k"), t = c.int_const("t");
    auto covered = z3::exists(k, k >= 1 &&
        z3::forall(t, z3::implies(t >= 0 && t < k, 2 + 2*t < w)) &&
        2 + 2*k >= w);
    z3::solver solver(c);
    auto boolean_cast = [&](const z3::expr& condition) {
        return z3::int2bv(1, z3::ite(condition, c.int_val(1), c.int_val(0))) != c.bv_val(0, 1);
    };
    solver.add(w >= 4 && boolean_cast(w > 0) && boolean_cast(w < 10000000) && !covered);
    auto before = solver.assertions().to_string();
    EXPECT_EQ(ari_exe::quantified_unsat_fallback(solver, 3000), z3::unsat);
    EXPECT_EQ(before, solver.assertions().to_string());
}

TEST(QuantifiedQuery, DoesNotAdoptSeparateSatModel) {
    z3::context c;
    auto x = c.int_const("x"), y = c.int_const("y");
    z3::solver solver(c);
    solver.add(x == 7 && z3::exists(y, y > x));
    EXPECT_EQ(ari_exe::quantified_unsat_fallback(solver, 3000), z3::unknown);
    ASSERT_EQ(solver.check(), z3::sat);
    EXPECT_EQ(solver.get_model().eval(x).get_numeral_int(), 7);
}

TEST(QuantifiedQuery, SkipsUnquantifiedAndZeroBudgetQueries) {
    z3::context c;
    z3::solver solver(c);
    solver.add(c.bool_val(false));
    EXPECT_EQ(ari_exe::quantified_unsat_fallback(solver, 3000), z3::unknown);
    EXPECT_EQ(ari_exe::quantified_unsat_fallback(solver, 0), z3::unknown);
}

TEST(QuantifiedQuery, FeasibilityPruningRetainsUndecidedArithmetic) {
    z3::context c;
    auto n = c.int_const("n");
    auto power = z3::pw(c.int_val(2), n);
    // n=2 is a concrete satisfying assignment, even if Z3's arithmetic
    // procedure returns UNKNOWN for the symbolic exponent.
    EXPECT_TRUE(ari_exe::is_feasible(n >= 0 && power >= 4 && power < 5));
    EXPECT_FALSE(ari_exe::is_feasible(n < 0 && n >= 0));
}
