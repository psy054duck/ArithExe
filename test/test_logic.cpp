#include "gtest/gtest.h"
#include "z3++.h"

#include "logics.h"

using namespace ari_exe;

TEST(ParametricArrayCoordinates, UsesOnlyCertifiedPositiveStrideAndRemainderBounds) {
    z3::context ctx;
    const auto r=ctx.int_const("row"),c=ctx.int_const("column"),n=ctx.int_const("width");
    const auto offset=r*n+c;
    const auto domain=n>0&&c>=0&&c<n;
    EXPECT_TRUE(z3::eq(restrict_to_domain(offset/n,domain),r));
    EXPECT_TRUE(z3::eq(restrict_to_domain(offset%n,domain),c));
    for (const auto& weak:{n>0,n==0,n<0,n>0&&c<0,n>0&&c>=n}) {
        const auto value=restrict_to_domain(offset/n,weak);
        z3::solver check(ctx);check.set("timeout",1000u);
        check.add(weak&&value!=offset/n);
        EXPECT_EQ(check.check(),z3::unsat);
    }
}

TEST(ITEElimination, PreservesInputSymbolsContainingExclamationMarks) {
    z3::context context;
    const auto x = context.int_const("source!0");
    const auto y = context.int_const("source!1");
    const auto f = context.function("input", context.int_sort(), context.int_sort());
    const auto formula = x > 0 && f(z3::ite(x > y, x + 1, y)) == x;
    const auto rewritten = eliminate_ite(formula);
    z3::solver checker(context);
    checker.add(rewritten != formula);
    EXPECT_EQ(checker.check(), z3::unsat);
    checker.reset();
    checker.add(rewritten && x <= 0);
    EXPECT_EQ(checker.check(), z3::unsat);
}

TEST(ITEElimination, PreservesUninterpretedReadsAndQuantifiedGuards) {
    z3::context context;
    const auto x = context.int_const("x"), y = context.int_const("y");
    const auto f = context.function("input", context.int_sort(), context.int_sort());
    const auto formula = z3::forall(x,
        f(z3::ite(x < 0, -x, x)) >= y);
    const auto rewritten = eliminate_ite(formula);
    z3::solver checker(context);
    checker.set("timeout", 2000u);
    checker.add(formula != rewritten);
    EXPECT_EQ(checker.check(), z3::unsat);
}

TEST(QuantifiedIterationCount, CertifiesUniqueConstantsBeforeRejectingQuantifiers) {
    z3::context context;
    const auto n = context.int_const("count");
    const auto k = context.int_const("k");
    const auto input = context.function("entry", context.int_sort(), context.int_sort());
    z3::expr_vector variables(context);
    variables.push_back(n);
    const auto bounds = z3::forall(k, z3::implies(k >= 0 && k < 5, input(k) >= -100));
    const auto values = LinearLogic().solve_vars(bounds && n == 5, variables);
    ASSERT_EQ(values.size(), 1u);
    EXPECT_TRUE(z3::eq(values[0], context.int_val(5)));
    EXPECT_TRUE(LinearLogic().solve_vars(bounds && n >= 5, variables).empty());
    EXPECT_TRUE(LinearLogic().solve_vars(bounds && n == 5 && n == 6, variables).empty());
}

TEST(QuantifiedIterationCount, RestrictsTheCountToTheProvenEntryDomain) {
    z3::context context;
    const auto n = context.int_const("length");
    const auto count = z3::ite(n > 0, n, context.int_val(0));
    EXPECT_TRUE(z3::eq(restrict_to_domain(count, n > 1), n));
    EXPECT_TRUE(z3::eq(restrict_to_domain(count, n <= 0), context.int_val(0)));
    const auto unconstrained = restrict_to_domain(count, context.bool_val(true));
    z3::solver checker(context);
    checker.add(unconstrained != count);
    EXPECT_EQ(checker.check(), z3::unsat);
}

TEST(QuantifiedIterationCount, KeepsTheUnresolvedZeroTripRegionWithQuantifiedInputs) {
    z3::context context;
    const auto n = context.int_const("length"), i = context.int_const("i");
    const auto a = context.function("entry", context.int_sort(), context.int_sort());
    const auto count = z3::ite(n > 0, n, context.int_val(0));
    const auto bounds = z3::forall(i, z3::implies(i >= 0 && i < n, a(i) >= -100));
    EXPECT_TRUE(z3::eq(restrict_to_domain(count, bounds && n > 0), n));
    const auto unconstrained = restrict_to_domain(count, bounds);
    z3::solver checker(context);
    checker.set("timeout", 1000u);
    checker.add(unconstrained != count);
    EXPECT_EQ(checker.check(), z3::unsat);
}

TEST(CaseMerging, IdenticalExpressionsMergeWithoutChangingTheirValue) {
    z3::context context;
    auto n = context.int_const("n");
    z3::expr_vector conditions(context), values(context);
    conditions.push_back(n < 0);
    conditions.push_back(n >= 0);
    values.push_back(n + 1);
    values.push_back(n + 1);
    auto [merged_conditions, merged_values] = merge_cases(conditions, values);
    ASSERT_EQ(merged_values.size(), 1u);
    z3::solver solver(context);
    solver.add(piecewise2ite(merged_conditions, merged_values) != n + 1);
    EXPECT_EQ(solver.check(), z3::unsat);
}

TEST(CaseMerging, DistinctExpressionsKeepTheirGuardedMeaning) {
    z3::context context;
    auto n = context.int_const("n");
    z3::expr_vector conditions(context), values(context);
    conditions.push_back(n < 0);
    conditions.push_back(n >= 0);
    values.push_back(n);
    values.push_back(n + 1);
    auto [merged_conditions, merged_values] = merge_cases(conditions, values);
    ASSERT_EQ(merged_values.size(), 2u);
    z3::solver solver(context);
    solver.add(piecewise2ite(merged_conditions, merged_values) != z3::ite(n < 0, n, n + 1));
    EXPECT_EQ(solver.check(), z3::unsat);
}

TEST(ExpressionTraversal, CollectsOnlyFreeVariablesUnderQuantifiers) {
    z3::context context;
    auto x = context.int_const("x");
    auto y = context.int_const("y");
    const auto variables = Logic().collect_vars(z3::forall(x, x >= y));
    ASSERT_EQ(variables.size(), 1u);
    EXPECT_TRUE(variables.contains(y));
}

TEST(ExpressionTraversal, HandlesAlgebraicValues) {
    z3::context context;
    auto x = context.real_const("x");
    z3::solver solver(context);
    solver.add(x*x == 2 && x > 0);
    ASSERT_EQ(solver.check(), z3::sat);
    const auto root = solver.get_model().eval(x);
    ASSERT_TRUE(root.is_algebraic());
    EXPECT_TRUE(Logic().collect_vars(root).empty());
}

TEST(ExpressionTraversal, HandlesBooleanAtomsWithoutArguments) {
    z3::context context;
    auto flag = context.bool_const("flag");
    const auto atoms = Logic().atoms(flag);
    ASSERT_EQ(atoms.size(), 1u);
    EXPECT_TRUE(z3::eq(atoms[0], flag));
}

TEST(ExpressionTraversal, TreatsQuantifiedFormulasAsAtoms) {
    z3::context context;
    auto x = context.int_const("x");
    auto y = context.int_const("y");
    auto f = context.function("f", context.int_sort(), context.int_sort());
    auto formula = z3::forall(x, f(x) > y);
    const auto atoms = Logic().atoms(formula);
    ASSERT_EQ(atoms.size(), 1u);
    EXPECT_TRUE(z3::eq(atoms[0], formula));
}

TEST(ITE2PIECEWISE, RejectsEmptyAlternativesWithoutAsserting) {
    z3::context context;
    z3::expr_vector empty(context);
    EXPECT_THROW(piecewise2ite(empty, empty), std::invalid_argument);
}

TEST(DomainRestriction, SimplifiesArrayReadArgumentsUnderTheDomain) {
    z3::context context;
    auto x = context.int_const("x");
    auto initial = context.function("initial", context.int_sort(), context.int_sort());
    auto value = initial(z3::ite(x >= 0, x, -x));
    const auto restricted = restrict_to_domain(value, x >= 0);
    EXPECT_TRUE(z3::eq(restricted, initial(x)));
}

TEST(DomainRestriction, DoesNotShareCacheAcrossOppositeBranchAssumptions) {
    z3::context context;
    auto x = context.int_const("x");
    auto initial = context.function("initial", context.int_sort(), context.int_sort());
    auto shared = z3::ite(x < 0, context.int_val(0), context.int_val(1));
    auto value = z3::ite(x < 0, initial(shared), initial(shared) + 1);
    const auto restricted = restrict_to_domain(value, context.bool_val(true));
    z3::solver solver(context);
    solver.add(restricted != value);
    EXPECT_EQ(solver.check(), z3::unsat);
}

TEST(DomainRestriction, PreservesTheValueOnAllValidIndices) {
    z3::context context;
    auto x = context.int_const("x");
    auto n = context.int_const("n");
    auto initial = context.function("initial", context.int_sort(), context.int_sort());
    auto value = z3::ite(x < n, 2 + 20*x, initial(x));
    auto domain = n > 0 && x >= 0 && x < 2*n;
    const auto restricted = restrict_to_domain(value, domain);
    z3::solver solver(context);
    solver.add(domain && restricted != value);
    EXPECT_EQ(solver.check(), z3::unsat);
}

TEST(DomainRestriction, RemovesReadBranchesBeyondTheIterationBound) {
    z3::context context;
    auto n = context.int_const("n");
    auto count = context.int_const("count");
    auto initial = context.function("initial", context.int_sort(), context.int_sort());
    auto value = z3::ite(n > count && n >= 2, initial(n - 1), 2 + 20*(n - 1));
    auto domain = n >= 1 && n < count && count > 0;
    const auto restricted = restrict_to_domain(value, domain);
    z3::solver solver(context);
    solver.add(restricted != 2 + 20*(n - 1));
    EXPECT_EQ(solver.check(), z3::unsat);
}

TEST(DomainRestriction, KeepsAnArrayReadWhenItsBranchIsReachable) {
    z3::context context;
    auto i = context.int_const("i");
    auto initial = context.function("initial", context.int_sort(), context.int_sort());
    auto value = z3::ite(i < 0, initial(i), context.int_val(1));
    auto domain = i >= -1 && i < 4;
    const auto restricted = restrict_to_domain(value, domain);
    z3::solver solver(context);
    solver.add(domain && restricted != value);
    EXPECT_EQ(solver.check(), z3::unsat);
    solver.reset();
    solver.add(i == -1 && restricted != 1);
    EXPECT_EQ(solver.check(), z3::sat);
}

TEST(DomainRestriction, MergesPolynomialBoundaryValuesOnValidArrayIndices) {
    z3::context context;
    const auto i = context.int_const("boundary_i");
    const auto n = context.int_const("boundary_n");
    const auto polynomial = 1 + 3*i + 3*i*i;
    const auto value = z3::ite(i == 0, context.int_val(1),
                              z3::ite(i == 1, context.int_val(7), polynomial));
    const auto restricted = restrict_to_domain(value, n > 0 && i >= 0 && i < n);
    EXPECT_TRUE(z3::eq(restricted.simplify(), polynomial.simplify()));
}

TEST(DomainRestriction, MergesEitherOrientationOfABoundaryBranch) {
    z3::context context;
    const auto i = context.int_const("orientation_i");
    const auto polynomial = 1 + 3*i + 3*i*i;
    const auto value = z3::ite(i != 0, polynomial, context.int_val(1));
    const auto restricted = restrict_to_domain(value, i >= 0);
    EXPECT_TRUE(z3::eq(restricted.simplify(), polynomial.simplify()));
}

TEST(DomainRestriction, UsesActualIterationBoundsForComposedArrayReads) {
    z3::context context;
    const auto t = context.int_const("composed_t");
    const auto n = context.int_const("composed_n");
    const auto exceptional = (t < 4 || n < t) && (n < 3 || t != 3);
    const auto polynomial = -2 + 3*(t-1) + 3*(t-2)*(t-2);
    const auto value = z3::ite(exceptional, context.int_val(1), polynomial);
    const auto restricted = restrict_to_domain(value, t >= 2 && t <= n);
    EXPECT_TRUE(z3::eq(restricted.simplify(), polynomial.simplify()));
    const auto unrestricted = restrict_to_domain(value, context.bool_val(true));
    z3::solver solver(context);
    solver.add(unrestricted != polynomial);
    EXPECT_EQ(solver.check(), z3::sat);
    solver.reset();
    solver.add(unrestricted != value);
    EXPECT_EQ(solver.check(), z3::unsat);
}

TEST(DomainRestriction, DoesNotMergeDistinctReachableBoundaryValues) {
    z3::context context;
    const auto i = context.int_const("distinct_boundary_i");
    const auto polynomial = 1 + 3*i + 3*i*i;
    const auto value = z3::ite(i == 0, context.int_val(2), polynomial);
    const auto restricted = restrict_to_domain(value, i >= 0);
    z3::solver solver(context);
    solver.add(i >= 0 && restricted != value);
    EXPECT_EQ(solver.check(), z3::unsat);
    solver.reset();
    solver.add(i == 0 && restricted != polynomial);
    EXPECT_EQ(solver.check(), z3::sat);
}

static z3::expr composed_quadratic_summary(
    const z3::expr& i, const z3::expr& n, const z3::expr& entry) {
    const auto interior = i >= 1 && n-i >= 2;
    const auto last = n >= 2 && i-n == -1;
    const auto beyond = !(i-n <= -1);
    const auto first = 0 <= i && n >= 2 && !(i >= 1);
    const auto unwritten = 0 <= i && i >= 1 &&
        (!(n >= 2) || beyond) && n >= 1;
    const auto frame = !(0 <= i) ||
        0 >= z3::ite(first, i.ctx().int_val(1),
                    z3::ite(unwritten, i.ctx().int_val(0), n));
    return z3::ite(interior, 1 + 3*i + 3*i*i,
        z3::ite(!last || unwritten || first,
                z3::ite(frame, entry, i.ctx().int_val(1)),
                1 - 3*n + 3*n*n));
}

TEST(DomainRestriction, CollapsesTheWholeComposedQuadraticOnItsReadDomain) {
    z3::context context;
    const auto i = context.int_const("composed_read_i");
    const auto n = context.int_const("composed_read_n");
    const auto entry = context.function("entry", context.int_sort(), context.int_sort());
    const auto value = composed_quadratic_summary(i, n, entry(i));
    const auto restricted = restrict_to_domain(value, n > 0 && 0 <= i && i < n);
    EXPECT_TRUE(z3::eq(restricted.simplify(), (1 + 3*i + 3*i*i).simplify()));
}

TEST(DomainRestriction, PreservesTheComposedQuadraticFrameBeyondWrittenIndices) {
    z3::context context;
    const auto i = context.int_const("composed_frame_i");
    const auto n = context.int_const("composed_frame_n");
    const auto entry = context.function("entry", context.int_sort(), context.int_sort());
    const auto value = composed_quadratic_summary(i, n, entry(i));
    const auto domain = n > 0 && 0 <= i && i < 2*n;
    const auto restricted = restrict_to_domain(value, domain);
    z3::solver solver(context);
    solver.add(domain && restricted != value);
    EXPECT_EQ(solver.check(), z3::unsat);
    solver.reset();
    solver.add(n > 0 && n <= i && i < 2*n && restricted != entry(i));
    EXPECT_EQ(solver.check(), z3::unsat);
}

TEST(DomainRestriction, DoesNotReplaceADistinctInteriorWithAPolynomialCandidate) {
    z3::context context;
    const auto i = context.int_const("different_interior_i");
    const auto n = context.int_const("different_interior_n");
    const auto polynomial = 1 + 3*i + 3*i*i;
    const auto value = z3::ite(i == n-1, polynomial+1, polynomial);
    const auto domain = n > 1 && 0 <= i && i < n;
    const auto restricted = restrict_to_domain(value, domain);
    z3::solver solver(context);
    solver.add(domain && restricted != value);
    EXPECT_EQ(solver.check(), z3::unsat);
}

TEST(LinearSolving, LeavesUneliminatedQuantifiersForTheSymbolicSummary) {
    z3::context context;
    auto n = context.int_const("n");
    auto k = context.int_const("k");
    auto unknown = context.function("unknown", context.int_sort(), context.int_sort());
    z3::expr_vector variables(context);
    variables.push_back(n);
    const auto constraints = n >= 0 &&
        z3::forall(k, z3::implies(k >= 0 && k < n, unknown(k) > 0));
    EXPECT_TRUE(LinearLogic().solve_vars(constraints, variables).empty());
}

TEST(LinearSolving, CertifiesAConstantIterationCountBeforeElimination) {
    z3::context context;
    auto n = context.int_const("iteration_count");
    z3::expr_vector variables(context);
    variables.push_back(n);
    const auto result = LinearLogic().solve_vars(n >= 0 && n <= 2 && n > 1, variables);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_TRUE(z3::eq(result[0], context.int_val(2)));
}

TEST(LinearSolving, DoesNotMistakeAModelValueForAConstantSolution) {
    z3::context context;
    auto n = context.int_const("iteration_count");
    z3::expr_vector variables(context);
    variables.push_back(n);
    EXPECT_TRUE(LinearLogic().solve_vars(n >= 0 && n <= 2, variables).empty());
}

TEST(LinearSolving, AnUnsolvedDisjunctCannotUseAnotherCasesDefault) {
    z3::context context;
    auto n = context.int_const("iteration_count");
    auto p = context.int_const("parameter");
    z3::expr_vector variables(context);
    variables.push_back(n);
    const auto constraints = (p == 0 && n == 2) || (p == 1 && n >= 3 && n <= 4);
    EXPECT_TRUE(LinearLogic().solve_vars(constraints, variables).empty());
}

TEST(LinearSolving, RejectsSymbolicPivotsWithoutAborting) {
    z3::context context;
    auto x = context.int_const("x");
    auto p = context.int_const("p");
    z3::expr_vector equations(context), variables(context);
    equations.push_back(p*x == 1);
    variables.push_back(x);
    EXPECT_TRUE(LinearLogic().solve_linear_equations(equations, variables).empty());
}

TEST(LinearSolving, RejectsSingularAndInconsistentSystems) {
    z3::context context;
    auto x = context.int_const("x");
    auto y = context.int_const("y");
    z3::expr_vector equations(context), variables(context);
    variables.push_back(x);
    variables.push_back(y);
    equations.push_back(x+y == 2);
    equations.push_back(2*x+2*y == 4);
    EXPECT_TRUE(LinearLogic().solve_linear_equations(equations, variables).empty());
    equations.resize(0);
    equations.push_back(x+y == 2);
    equations.push_back(2*x+2*y == 5);
    EXPECT_TRUE(LinearLogic().solve_linear_equations(equations, variables).empty());
}

TEST(LinearSolving, RationalEliminationProducesCertifiedIntegerValues) {
    z3::context context;
    auto x = context.int_const("x");
    auto y = context.int_const("y");
    auto p = context.int_const("p");
    z3::expr_vector equations(context), variables(context);
    variables.push_back(x);
    variables.push_back(y);
    equations.push_back(2*x+y == p);
    equations.push_back(x-y == 0);
    const auto result = LinearLogic().solve_linear_equations(equations, variables);
    ASSERT_EQ(result.size(), 2u);
    z3::solver certificate(context);
    certificate.add(z3::mk_and(equations));
    certificate.add(x != result[0] || y != result[1]);
    EXPECT_EQ(certificate.check(), z3::unsat);
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
