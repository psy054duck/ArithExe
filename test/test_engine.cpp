#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#include <stdexcept>
#include "engine.h"
#include "rec_solver.h"
#include "ArrayGuard.h"

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

BenchmarkRun run_benchmark(const std::string& relative_path) {
    auto engine = Engine(benchmark_path(relative_path));
    auto veri_res = engine.verify();
    BenchmarkRun run{
        veri_res,
        engine.has_issue(),
        engine.has_issue() ? engine.get_issue_kind()
                           : VerifierIssueKind::UnknownState,
        engine.get_issue_message(),
    };
    return run;
}

VeriResult verify_benchmark(const std::string& relative_path) {
    return run_benchmark(relative_path).result;
}

void run_bounded_cfinite_benchmark(const std::string& filename) {
    auto veri_res = verify_benchmark("bounded-cfinite/" + filename);
    EXPECT_EQ(veri_res, HOLD) << "Failed on: test/bounded-cfinite/" << filename;
}

class ScopedEnvironmentVariable {
  public:
    ScopedEnvironmentVariable(const char* name, const char* value)
        : name(name) {
        const char* previous_value = std::getenv(name);
        if (previous_value) previous = previous_value;
        setenv(name, value, 1);
    }

    ~ScopedEnvironmentVariable() {
        if (previous.empty()) {
            unsetenv(name.c_str());
        } else {
            setenv(name.c_str(), previous.c_str(), 1);
        }
    }

  private:
    std::string name;
    std::string previous;
};
}

TEST(RecurrencePreparation, SplitsInvariantBooleanConditionalsWithoutIntegerAbstraction) {
    z3::context context;
    const auto t = context.int_const("t");
    const auto n = context.int_const("branch_n");
    const auto x = context.function("x", context.int_sort(), context.int_sort());
    const auto guard = z3::ite(n % 2 == 1, n > 0, n < 0);
    rec_solver solver(context);
    solver.set_ind_var(t);
    const std::vector<z3::expr> conditions{guard, !guard};
    const std::vector<rec_ty> transitions{
        {{x(t+1), x(t)+1}}, {{x(t+1), x(t)+2}}};
    ASSERT_NO_THROW(solver.set_eqs(conditions, transitions));
    const auto serialized = solver.rec2string();
    EXPECT_EQ(serialized.find("ari_rec_parameter_"), std::string::npos);
    EXPECT_NE(serialized.find("branch_n"), std::string::npos);
}

TEST(RecurrencePreparation, RejectsQuantifiersAndBoundVariablesWithoutAborting) {
    z3::context context;
    const auto k = context.int_const("k");
    const auto a = context.function("entry", context.int_sort(), context.int_sort());
    const auto quantified = z3::forall(k, a(k) >= 0);
    rec_solver solver(context);
    EXPECT_THROW(solver.z3_infix(quantified), std::runtime_error);
    EXPECT_THROW(solver.z3_infix(z3::exists(k, a(k) < 0)), std::runtime_error);
    EXPECT_THROW(solver.z3_infix(quantified.body()), std::runtime_error);
    EXPECT_NO_THROW(solver.z3_infix(a(k) + 1));
}

TEST(LoopHelperConditions, RetainsBranchesInHelpersOutsideTheCallerLoop) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine safe(benchmark_path("arrays/true_loop_helper_conditions.c"));
    safe.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(safe.verify(), HOLD);
    Engine unsafe(benchmark_path("arrays/false_loop_helper_conditions.c"));
    unsafe.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(unsafe.verify(), FAIL);
}

TEST(FiniteAggregateRepresentation, ExpandsBoundsAndPreservesSymbolicRanges) {
    VerificationSession session;
    auto activation = session.activate();
    auto& context = session.analyses().get_z3ctx();
    const auto l = context.int_const("l"), u = context.int_const("u");
    const auto k = context.int_const("k"), h = context.int_const("h");
    const auto n = context.int_const("n");
    const auto a = context.function("entry", context.int_sort(), context.int_sort());
    const auto sum = context.function("ari_finite_sum_test", context.int_sort(),
        context.int_sort(), context.int_sort(), context.int_sort());
    session.register_aggregate(false, sum(l, u, h), k, a(k+h));
    EXPECT_TRUE(session.contains_aggregates(sum(context.int_val(0), n, h)));
    EXPECT_TRUE(session.contains_aggregates(session.expand_aggregates(
        sum(context.int_val(0), context.int_val(100000), h))));
    const auto expanded = session.expand_aggregates(
        sum(context.int_val(0), context.int_val(3), context.int_val(4)));
    z3::solver checker(context);
    checker.add(expanded != a(4)+a(5)+a(6));
    EXPECT_EQ(checker.check(), z3::unsat);
    EXPECT_FALSE(session.contains_aggregates(expanded));
    EXPECT_TRUE(z3::eq(session.expand_aggregates(sum(n,n,h)), context.int_val(0)));
    session.clear_module_state();
    EXPECT_FALSE(session.contains_aggregates(sum(context.int_val(0),n,h)));
}

TEST(FiniteAggregateRepresentation, ProductHasEmptyIdentityAndAllowsZeroFactors) {
    VerificationSession session;
    auto activation = session.activate();
    auto& context = session.analyses().get_z3ctx();
    const auto l = context.int_const("l"), u = context.int_const("u");
    const auto k = context.int_const("k");
    const auto product = context.function("ari_finite_product_test", context.int_sort(),
        context.int_sort(), context.int_sort());
    session.register_aggregate(true, product(l,u), k, k);
    EXPECT_TRUE(z3::eq(session.expand_aggregates(
        product(context.int_val(0),context.int_val(0))), context.int_val(1)));
    EXPECT_TRUE(z3::eq(session.expand_aggregates(
        product(context.int_val(0),context.int_val(4))), context.int_val(0)));
    EXPECT_TRUE(z3::eq(session.expand_aggregates(
        product(context.int_val(1),context.int_val(5))), context.int_val(24)));
}

TEST(FiniteAggregateIdentities, CertifiesSwapAndRotationOnASymbolicFiniteDomain) {
    VerificationSession session;
    auto activation = session.activate();
    auto& ctx = session.analyses().get_z3ctx();
    const auto l = ctx.int_const("lower"), u = ctx.int_const("upper");
    const auto n = ctx.int_const("size"), k = ctx.int_const("index");
    const auto a = ctx.function("entry", ctx.int_sort(), ctx.int_sort());
    const auto base = ctx.function("ari_finite_sum_identity", ctx.int_sort(), ctx.int_sort(), ctx.int_sort());
    const auto swap = ctx.function("ari_finite_sum_swap", ctx.int_sort(), ctx.int_sort(), ctx.int_sort());
    const auto rotate = ctx.function("ari_finite_sum_rotate", ctx.int_sort(), ctx.int_sort(), ctx.int_sort(), ctx.int_sort());
    session.register_aggregate(false, base(l,u), k, k+a(k));
    session.register_aggregate(false, swap(l,u), k,
        z3::ite(k==0, k+a(1), z3::ite(k==1, k+a(0), k+a(k))));
    session.register_aggregate(false, rotate(l,u,n), k,
        k+a(z3::ite(k==n-1, ctx.int_val(0), k+1)));
    const auto x = base(ctx.int_val(0),n), y = swap(ctx.int_val(0),n);
    const auto z = rotate(ctx.int_val(0),n,n);
    const auto query = x != y || x != z;
    const auto lemmas = session.aggregate_identities(query, n>1);
    EXPECT_GE(lemmas.size(), 2u);
    z3::solver checker(ctx);
    checker.add(n>1); checker.add(query); checker.add(lemmas);
    EXPECT_EQ(checker.check(), z3::unsat);
}

TEST(FiniteAggregateIdentities, RejectsNonPermutationsAndChangedSummandsOrBounds) {
    VerificationSession session;
    auto activation = session.activate();
    auto& ctx = session.analyses().get_z3ctx();
    const auto l = ctx.int_const("lower"), u = ctx.int_const("upper");
    const auto n = ctx.int_const("size"), k = ctx.int_const("index");
    const auto a = ctx.function("entry", ctx.int_sort(), ctx.int_sort());
    const auto base = ctx.function("ari_finite_sum_base", ctx.int_sort(), ctx.int_sort(), ctx.int_sort());
    const auto wrong = ctx.function("ari_finite_sum_wrong", ctx.int_sort(), ctx.int_sort(), ctx.int_sort());
    const auto duplicate = ctx.function("ari_finite_sum_duplicate", ctx.int_sort(), ctx.int_sort(), ctx.int_sort());
    const auto shifted = ctx.function("ari_finite_sum_shifted", ctx.int_sort(), ctx.int_sort(), ctx.int_sort());
    const auto swap = ctx.function("ari_finite_sum_swap", ctx.int_sort(), ctx.int_sort(), ctx.int_sort());
    session.register_aggregate(false, base(l,u), k, k+a(k));
    session.register_aggregate(false, wrong(l,u), k, k+1+a(k));
    session.register_aggregate(false, duplicate(l,u), k, k+a(0));
    session.register_aggregate(false, shifted(l,u), k, k+a(k+1));
    session.register_aggregate(false, swap(l,u), k,
        k+a(z3::ite(k==0,ctx.int_val(1),z3::ite(k==1,ctx.int_val(0),k))));
    const auto x = base(ctx.int_val(0),n);
    for (const auto& y : {wrong(ctx.int_val(0),n), duplicate(ctx.int_val(0),n),
                         shifted(ctx.int_val(0),n), base(ctx.int_val(0),n+1)}) {
        z3::solver checker(ctx);
        checker.add(n>1); checker.add(x!=y);
        checker.add(session.aggregate_identities(x!=y,n>1));
        EXPECT_EQ(checker.check(), z3::sat);
    }
    const auto query = x!=swap(ctx.int_val(0),n);
    z3::solver checker(ctx);
    checker.add(n==1); checker.add(query);
    checker.add(session.aggregate_identities(query,n==1));
    EXPECT_EQ(checker.check(), z3::sat);
}

TEST(FiniteSumCanonicalization, CombinesSignsCoefficientsAndConstantSummands) {
    VerificationSession session;
    auto activation = session.activate();
    auto& ctx = session.analyses().get_z3ctx();
    const auto l=ctx.int_const("lower"), u=ctx.int_const("upper");
    const auto k=ctx.int_const("index"), j=ctx.int_const("other_index");
    const auto n=ctx.int_const("length"), c=ctx.int_const("constant");
    const auto a=ctx.function("entry_a",ctx.int_sort(),ctx.int_sort());
    const auto b=ctx.function("entry_b",ctx.int_sort(),ctx.int_sort());
    const auto sum=ctx.function("ari_finite_sum_a",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto neg=ctx.function("ari_finite_sum_neg",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto other=ctx.function("ari_finite_sum_b",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto mix=ctx.function("ari_finite_sum_mix",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    session.register_aggregate(false,sum(l,u),k,a(k));
    session.register_aggregate(false,neg(l,u),j,-a(j));
    session.register_aggregate(false,other(l,u),k,b(k));
    session.register_aggregate(false,mix(l,u,c),k,c*a(k)-2*b(k)+3);
    const auto x=sum(ctx.int_val(0),n), y=other(ctx.int_val(0),n);
    const auto length=z3::ite(n<=0,ctx.int_val(0),n);
    const auto query=x+neg(ctx.int_val(0),n)!=0 ||
        mix(ctx.int_val(0),n,c)!=c*x-2*y+3*length;
    z3::solver checker(ctx);
    checker.add(query);
    checker.add(session.aggregate_identities(query,ctx.bool_val(true)));
    EXPECT_EQ(checker.check(),z3::unsat);
    EXPECT_TRUE(session.contains_aggregates(query));
}

TEST(FiniteSumCanonicalization, KeepsDifferentArraysBoundsArgumentsAndProductsDistinct) {
    VerificationSession session;
    auto activation = session.activate();
    auto& ctx = session.analyses().get_z3ctx();
    const auto l=ctx.int_const("lower"), u=ctx.int_const("upper");
    const auto k=ctx.int_const("index"), p=ctx.int_const("offset");
    const auto n=ctx.int_const("length");
    const auto a=ctx.function("entry_a",ctx.int_sort(),ctx.int_sort());
    const auto b=ctx.function("entry_b",ctx.int_sort(),ctx.int_sort());
    const auto sum=ctx.function("ari_finite_sum_param",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto neg=ctx.function("ari_finite_sum_neg_param",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto other=ctx.function("ari_finite_sum_other",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto product=ctx.function("ari_finite_product_a",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    session.register_aggregate(false,sum(l,u,p),k,a(k+p));
    session.register_aggregate(false,neg(l,u,p),k,-a(k+p));
    session.register_aggregate(false,other(l,u),k,-b(k));
    session.register_aggregate(true,product(l,u),k,a(k));
    const auto zero=ctx.int_val(0), one=ctx.int_val(1), two=ctx.int_val(2);
    const auto x=sum(zero,n,one);
    for (const auto& y : {neg(zero,n,two),neg(zero,n+1,one),neg(one,n,one),
                          other(zero,n),product(zero,n)}) {
        const auto query=x+y!=0;
        z3::solver checker(ctx);
        checker.add(n>40); checker.add(query);
        checker.add(session.aggregate_identities(query,n>40));
        EXPECT_EQ(checker.check(),z3::sat);
    }
}

TEST(FiniteSumCanonicalization, HandlesEmptyIntervalsWithoutAssumingPositiveLengths) {
    VerificationSession session;
    auto activation = session.activate();
    auto& ctx = session.analyses().get_z3ctx();
    const auto l=ctx.int_const("lower"), u=ctx.int_const("upper");
    const auto k=ctx.int_const("index"), c=ctx.int_const("constant");
    const auto a=ctx.function("entry",ctx.int_sort(),ctx.int_sort());
    const auto sum=ctx.function("ari_finite_sum_empty",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto constant=ctx.function("ari_finite_sum_constant",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    session.register_aggregate(false,sum(l,u),k,a(k));
    session.register_aggregate(false,constant(l,u,c),k,c);
    const auto query=sum(l,u)!=0 || constant(l,u,c)!=0;
    z3::solver checker(ctx);
    checker.add(u<=l); checker.add(query);
    checker.add(session.aggregate_identities(query,u<=l));
    EXPECT_EQ(checker.check(),z3::unsat);
}

TEST(FiniteSumCanonicalization, AgreesWithConcreteExpansionIncludingNonlinearAtoms) {
    VerificationSession session;
    auto activation = session.activate();
    auto& ctx = session.analyses().get_z3ctx();
    const auto l=ctx.int_const("lower"), u=ctx.int_const("upper");
    const auto k=ctx.int_const("index");
    const auto a=ctx.function("entry_a",ctx.int_sort(),ctx.int_sort());
    const auto b=ctx.function("entry_b",ctx.int_sort(),ctx.int_sort());
    const auto sum=ctx.function("ari_finite_sum_reference",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto mix=ctx.function("ari_finite_sum_reference_mix",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto nonlinear=ctx.function("ari_finite_sum_reference_nonlinear",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    session.register_aggregate(false,sum(l,u),k,a(k));
    session.register_aggregate(false,mix(l,u),k,-2*a(k)+3*b(k+1)+5);
    session.register_aggregate(false,nonlinear(l,u),k,2*a(k)*b(k+1));
    for (const auto& bounds : {std::pair{-3,2},std::pair{0,0},std::pair{4,1},std::pair{0,5}}) {
        const auto lower=ctx.int_val(bounds.first), upper=ctx.int_val(bounds.second);
        const auto x=sum(lower,upper), y=mix(lower,upper), z=nonlinear(lower,upper);
        z3::solver checker(ctx);
        checker.add(x==session.expand_aggregates(x));
        checker.add(y==session.expand_aggregates(y));
        checker.add(z==session.expand_aggregates(z));
        for (int i=-4;i<=6;++i) {
            checker.add(a(i)==i*i-2); checker.add(b(i)==3*i+1);
        }
        checker.add(session.aggregate_identities(x+y+z,ctx.bool_val(true)));
        EXPECT_EQ(checker.check(),z3::sat);
    }
}

TEST(FiniteSumCanonicalization, BudgetExhaustionDoesNotEmitAPartialSumIdentity) {
    VerificationSession session;
    auto activation = session.activate();
    auto& ctx = session.analyses().get_z3ctx();
    const auto l=ctx.int_const("lower"), u=ctx.int_const("upper");
    const auto k=ctx.int_const("index"), n=ctx.int_const("length");
    const auto sum=ctx.function("ari_finite_sum_budget",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    auto body=ctx.int_val(0);
    for (unsigned i=0;i<300;++i) {
        const auto name="entry_"+std::to_string(i);
        body=body+ctx.function(name.c_str(),ctx.int_sort(),ctx.int_sort())(k);
    }
    session.register_aggregate(false,sum(l,u),k,body.simplify());
    const auto query=sum(ctx.int_val(0),n)!=0;
    EXPECT_TRUE(session.aggregate_identities(query,n>40).empty());
}

TEST(FiniteSumCanonicalization, ProvesGroupedCancellationAcrossSymbolicArrayLoops) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/true_symbolic_sum_cancellation.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(),HOLD) << engine.get_issue_message();
}

TEST(ArrayRegionComposition, SummarizesLargeBandsWithAnIterationDependentValue) {
    z3::context ctx;
    const auto k=ctx.int_const("cell"), t=ctx.int_const("step");
    const auto n=ctx.int_const("count");
    const auto a=ctx.function("entry",ctx.int_sort(),ctx.int_sort());
    const auto f=ctx.function("recurrence",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto base=t<=0||k<0;
    const auto column=k-1000*(t-1);
    const auto write=t<=1000&&column>=0&&column<1000;
    const auto domain=k>=0&&k<1000000;
    auto summary=summarize_overwrite_regions(a(k),f,t,
        {base,write,!(base||write)},{a(k),t-1+column,f(k,t-1)},domain);
    ASSERT_TRUE(summary.has_value());
    z3::expr_vector from(ctx),to(ctx);
    from.push_back(t); to.push_back(n);
    const auto actual=summary->substitute(from,to);
    const auto expected=z3::ite(k<1000*n,k/1000+k%1000,a(k));
    z3::solver checker(ctx);
    checker.set("timeout",1000u);
    checker.add(domain&&n>=0&&n<=1000&&actual!=expected);
    EXPECT_EQ(checker.check(),z3::unsat);
}

TEST(ArrayRegionComposition, UsesTheLastWriteWhenSuccessiveRegionsOverlap) {
    z3::context ctx;
    const auto k=ctx.int_const("cell"), t=ctx.int_const("step");
    const auto n=ctx.int_const("count");
    const auto a=ctx.function("entry",ctx.int_sort(),ctx.int_sort());
    const auto f=ctx.function("recurrence",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto base=t<=0||k<0;
    const auto write=t-1<=k&&k<=t+1;
    const auto domain=k>=0&&k<10000;
    auto summary=summarize_overwrite_regions(a(k),f,t,
        {base,write,!(base||write)},{a(k),t+5,f(k,t-1)},domain);
    ASSERT_TRUE(summary.has_value());
    z3::expr_vector from(ctx),to(ctx);
    from.push_back(t); to.push_back(n);
    const auto actual=summary->substitute(from,to);
    const auto expected=z3::ite(n>=1&&k<=n+1,z3::ite(n<k+1,n,k+1)+5,a(k));
    z3::solver checker(ctx);
    checker.set("timeout",1000u);
    checker.add(domain&&n>=0&&actual!=expected);
    EXPECT_EQ(checker.check(),z3::unsat);
}

TEST(ArrayRegionComposition, CertifiesUniquePairwiseClampWrites) {
    z3::context ctx;
    const auto k=ctx.int_const("cell"), t=ctx.int_const("step");
    const auto a=ctx.function("entry",ctx.int_sort(),ctx.int_sort());
    const auto f=ctx.function("recurrence",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto base=t<=0||k<0;
    const auto even=k==2*t-2&&a(k)>k;
    const auto odd=k==2*t-1&&a(k)>k;
    const auto domain=k>=0&&k<1000;
    auto result=summarize_overwrite_regions(a(k),f,t,
        {base,even,odd,!(base||even||odd)},
        {a(k),k,k,f(k,t-1)},domain,ctx.int_val(500));
    ASSERT_TRUE(result.has_value());
    const auto expected=z3::ite(k<2*t&&a(k)>k,k,a(k));
    z3::solver checker(ctx);
    checker.set("timeout",1000u);
    checker.add(domain&&t>=0&&t<=500&&*result!=expected);
    EXPECT_EQ(checker.check(),z3::unsat);
}

TEST(ArrayRegionComposition, DoesNotTrustNonAffineWriteTimeProposals) {
    z3::context ctx;
    const auto k=ctx.int_const("cell"), t=ctx.int_const("step");
    const auto a=ctx.function("entry",ctx.int_sort(),ctx.int_sort());
    const auto f=ctx.function("recurrence",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto base=t<=0||k<0, write=k==(t-1)*(t-1);
    EXPECT_FALSE(summarize_overwrite_regions(a(k),f,t,
        {base,write,!(base||write)},{a(k),t,f(k,t-1)},k>=0&&k<1000).has_value());
}

TEST(ArrayRegionComposition, CertifiesDiagonalAndOffDiagonalWriteTimes) {
    z3::context ctx;
    const auto k=ctx.int_const("cell"), t=ctx.int_const("step");
    const auto r=ctx.int_const("row"), n=ctx.int_const("width");
    const auto a=ctx.function("entry",ctx.int_sort(),ctx.int_sort());
    const auto f=ctx.function("recurrence",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto base=t<=0||k<0;
    const auto diagonal=t-1==r&&k==r*n+r;
    const auto other=t-1!=r&&k==r*n+t-1;
    const auto domain=n>0&&r>=0&&r<n&&k>=0&&k<n*n;
    auto result=summarize_overwrite_regions(a(k),f,t,
        {base,diagonal,other,!(base||diagonal||other)},
        {a(k),ctx.int_val(0),t-1,f(k,t-1)},domain,n);
    ASSERT_TRUE(result.has_value());
    const auto c=k-r*n;
    const auto expected=z3::ite(c>=0&&c<t,z3::ite(c==r,ctx.int_val(0),c),a(k));
    z3::solver check(ctx);
    check.set("timeout",2000u);
    check.add(domain&&t>=0&&t<=n&&*result!=expected);
    EXPECT_EQ(check.check(),z3::unsat);
}

TEST(ParametricMatrixReduction, ProvesColumnSumsAndRejectsChangedSpecifications) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    for (bool safe:{true,false}) {
        Engine engine(benchmark_path(safe ? "arrays/true_matrix_column_reduction.c"
                                          : "arrays/false_matrix_column_reduction.c"));
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_EQ(engine.verify(),safe ? HOLD : FAIL)<<engine.get_issue_message();
    }
}

TEST(ParametricMatrixReduction, ProvesDistancesAndRejectsChangedSpecifications) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    for (bool safe:{true,false}) {
        Engine engine(benchmark_path(safe ? "arrays/true_matrix_distance_reduction.c"
                                          : "arrays/false_matrix_distance_reduction.c"));
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_EQ(engine.verify(),safe ? HOLD : FAIL)<<engine.get_issue_message();
    }
}

TEST(ParametricMatrixReduction, ProvesAlternationAndRejectsChangedSpecifications) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    for (bool safe:{true,false}) {
        Engine engine(benchmark_path(safe ? "arrays/true_matrix_alternating_reduction.c"
                                          : "arrays/false_matrix_alternating_reduction.c"));
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_EQ(engine.verify(),safe ? HOLD : FAIL)<<engine.get_issue_message();
    }
}

TEST(ArrayRegionComposition, UsesArrayBoundsBeforeAnalyzingUnreachableFeedback) {
    z3::context ctx;
    const auto k=ctx.int_const("cell"), t=ctx.int_const("step");
    const auto a=ctx.function("entry",ctx.int_sort(),ctx.int_sort());
    const auto f=ctx.function("recurrence",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto base=t<=0||k<0, write=k==t-1, outside=k>=1000;
    const auto domain=k>=0&&k<100;
    auto summary=summarize_overwrite_regions(a(k),f,t,
        {base,write,outside,!(base||write||outside)},
        {a(k),ctx.int_val(7),f(k,t-1)+1,f(k,t-1)},domain);
    ASSERT_TRUE(summary.has_value());
    z3::solver checker(ctx);
    checker.set("timeout",1000u);
    checker.add(domain&&t>=0&&*summary!=z3::ite(k<t,ctx.int_val(7),a(k)));
    EXPECT_EQ(checker.check(),z3::unsat);
    const auto after_exit=t>10;
    auto bounded=summarize_overwrite_regions(a(k),f,t,
        {base,write,after_exit,!(base||write||after_exit)},
        {a(k),ctx.int_val(7),f(k,t-1)+1,f(k,t-1)},domain,ctx.int_val(10));
    ASSERT_TRUE(bounded.has_value());
    checker.reset();
    checker.add(domain&&t>=0&&t<=10&&*bounded!=z3::ite(k<t,ctx.int_val(7),a(k)));
    EXPECT_EQ(checker.check(),z3::unsat);
}

TEST(ArrayRegionComposition, RejectsFeedbackAndIncompleteTransitionCoverage) {
    z3::context ctx;
    const auto k=ctx.int_const("cell"), t=ctx.int_const("step");
    const auto a=ctx.function("entry",ctx.int_sort(),ctx.int_sort());
    const auto f=ctx.function("recurrence",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto base=t<=0||k<0, write=k==t-1;
    const auto domain=k>=0&&k<100;
    EXPECT_FALSE(summarize_overwrite_regions(a(k),f,t,
        {base,write,!(base||write)},{a(k),f(k,t-1)+1,f(k,t-1)},domain));
    EXPECT_FALSE(summarize_overwrite_regions(a(k),f,t,
        {base,write},{a(k),ctx.int_val(7)},domain));
}

TEST(FiniteSumCanonicalization, RejectsChangedBoundsArraysAndAssertionsEndToEnd) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    for (const auto* filename : {"false_symbolic_sum_cancellation.c",
            "false_symbolic_sum_changed_bound.c","false_symbolic_sum_changed_array.c"}) {
        SCOPED_TRACE(filename);
        Engine engine(benchmark_path(std::string("arrays/")+filename));
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_NE(engine.verify(),HOLD) << engine.get_issue_message();
        if (engine.has_issue()) {
            EXPECT_NE(engine.get_issue_kind(),VerifierIssueKind::UnsupportedSemantics);
        }
    }
}

TEST(FiniteAggregateIdentities, ProvesSymbolicArrayPermutationEndToEnd) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/true_aggregate_symbolic_permutation.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD) << engine.get_issue_message();
}

TEST(FiniteAggregateIdentities, DoesNotProveChangedSymbolicArrayPermutations) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    for (const auto* filename : {"false_aggregate_symbolic_offset.c",
                                 "false_aggregate_symbolic_duplicate.c"}) {
        SCOPED_TRACE(filename);
        Engine engine(benchmark_path(std::string("arrays/")+filename));
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_NE(engine.verify(), HOLD) << engine.get_issue_message();
    }
}

TEST(FiniteAggregateIdentities, MissingWorkerCannotMakeAnUnsolvedGuardVacuous) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    ScopedEnvironmentVariable python("ARITHEXE_SOLVER_PYTHON", "/nonexistent/arith-exe-python");
    Engine engine(benchmark_path("arrays/false_unsolved_loop_guard.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_NE(engine.verify(), HOLD) << engine.get_issue_message();
}

TEST(HelperReturnCache, PreservesUnexpandedSumAndAverageReturns) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    for (const auto* filename : {"true_helper_aggregate_sum.c",
                                 "true_helper_aggregate_average.c"}) {
        SCOPED_TRACE(filename);
        Engine engine(benchmark_path(std::string("arrays/")+filename));
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_EQ(engine.verify(), HOLD) << engine.get_issue_message();
        EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
    }
}

TEST(HelperReturnCache, DoesNotProveIncorrectAggregateReturnProperties) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    for (const auto* filename : {"false_helper_aggregate_sum.c",
                                 "false_helper_aggregate_average.c"}) {
        SCOPED_TRACE(filename);
        Engine engine(benchmark_path(std::string("arrays/")+filename));
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_NE(engine.verify(), HOLD) << engine.get_issue_message();
        EXPECT_EQ(engine.get_issue_message().find("piecewise expression"),
                  std::string::npos);
    }
}

TEST(HelperReturnCache, DoesNotNarrowLargeIntegerArgumentsOrResults) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    for (bool relaxed : {false, true}) {
        SCOPED_TRACE(relaxed);
        Engine engine(benchmark_path("arrays/true_helper_cache_wide_integer.c"));
        engine.get_session().set_ignore_bitwidth_constraints(relaxed);
        EXPECT_EQ(engine.verify(), HOLD) << engine.get_issue_message();
        Engine incorrect(benchmark_path("arrays/false_helper_cache_wide_integer.c"));
        incorrect.get_session().set_ignore_bitwidth_constraints(relaxed);
        EXPECT_EQ(incorrect.verify(), FAIL) << incorrect.get_issue_message();
    }
}

TEST(IterationCountComposition, ProvesCancellationAcrossPositiveLengthArrayLoops) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/true_count_domain_composition.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD) << engine.get_issue_message();
}

TEST(IterationCountComposition, DoesNotProveAnIncorrectCancellationAssertion) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/false_count_domain_composition.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL) << engine.get_issue_message();
}

TEST(IterationCountComposition, RetainsZeroTripsWithoutAPositiveLengthAssumption) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/false_count_domain_zero_trip.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL) << engine.get_issue_message();
}

TEST(IterationCountComposition, ComposesZeroBasePolynomialReadsUnderILP32) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    ScopedEnvironmentVariable model("ARITHEXE_DATA_MODEL", "ILP32");
    Engine engine(benchmark_path("arrays/true_composed_zero_base.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD) << engine.get_issue_message();
}

TEST(IterationCountComposition, DetectsTheWrongZeroBasePolynomialUnderILP32) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    ScopedEnvironmentVariable model("ARITHEXE_DATA_MODEL", "ILP32");
    Engine engine(benchmark_path("arrays/false_composed_zero_base.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL) << engine.get_issue_message();
}

namespace {
void check_direct_verification_call(const char* filename, VeriResult expected) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path(std::string("arrays/")+filename));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), expected) << engine.get_issue_message();
}
}

TEST(DirectVerificationCalls, DetectsErrorInArrayLoop) {
    check_direct_verification_call("false_direct_error_array.c", FAIL);
}

TEST(DirectVerificationCalls, DetectsNoReturnErrorInArrayLoop) {
    check_direct_verification_call("false_direct_error_noreturn.c", FAIL);
}

TEST(DirectVerificationCalls, DetectsDeclaredReachErrorOnLaterIteration) {
    check_direct_verification_call("false_declared_reach_error.c", FAIL);
}

TEST(DirectVerificationCalls, DetectsFalseDeclaredAssertion) {
    check_direct_verification_call("false_declared_assert.c", FAIL);
}

TEST(DirectVerificationCalls, ProvesTrueDeclaredAssertion) {
    check_direct_verification_call("true_declared_assert.c", HOLD);
}

TEST(DirectVerificationCalls, ZeroTripDoesNotExecuteError) {
    check_direct_verification_call("true_direct_error_zero_trip.c", HOLD);
}

TEST(DirectVerificationCalls, DeclaredAssumptionPrunesTheErrorPath) {
    check_direct_verification_call("true_declared_assume.c", HOLD);
}

TEST(DirectVerificationCalls, LaterAssumptionCannotHideEarlierError) {
    check_direct_verification_call("false_direct_error_before_assume.c", FAIL);
}

TEST(RecurrencePreparation, StillAbstractsIntegerConditionalsWithBooleanChildren) {
    z3::context context;
    const auto t = context.int_const("t");
    const auto n = context.int_const("branch_n");
    const auto x = context.function("x", context.int_sort(), context.int_sort());
    const auto guard = z3::ite(n % 2 == 1, n > 0, n < 0);
    const auto increment = z3::ite(guard, context.int_val(2), context.int_val(1));
    rec_solver solver(context);
    solver.set_ind_var(t);
    const std::vector<z3::expr> conditions{t < 10, t >= 10};
    const std::vector<rec_ty> transitions{
        {{x(t+1), x(t)+increment}}, {{x(t+1), x(t)+increment}}};
    ASSERT_NO_THROW(solver.set_eqs(conditions, transitions));
    EXPECT_NE(solver.rec2string().find("ari_rec_parameter_"), std::string::npos);
}

TEST(RecurrencePreparation, RejectsMismatchedInitialValuesWithoutMutation) {
    z3::context context;
    const auto t = context.int_const("t");
    const auto x = context.function("x", context.int_sort(), context.int_sort());
    const auto y = context.function("y", context.int_sort(), context.int_sort());
    z3::expr_vector keys(context), values(context);
    keys.push_back(x(0));
    keys.push_back(y(0));
    values.push_back(context.int_val(0));
    rec_solver solver(context);
    solver.set_ind_var(t);
    ASSERT_THROW(solver.add_initial_values(keys, values), std::invalid_argument);
    values.resize(0);
    values.push_back(context.int_val(3));
    values.push_back(context.int_val(4));
    ASSERT_NO_THROW(solver.add_initial_values(keys, values));
    solver.set_eqs({context.bool_val(true)}, {{{x(t+1), x(t)+1}, {y(t+1), y(t)+1}}});
    const auto serialized = solver.rec2string();
    EXPECT_NE(serialized.find("= 3;"), std::string::npos);
    EXPECT_NE(serialized.find("= 4;"), std::string::npos);
}

TEST(RecurrencePreparation, RestoresNestedInvariantConditionalParameters) {
    VerificationSession session;
    auto activation = session.activate();
    auto& context = session.analyses().get_z3ctx();
    const auto t = context.int_const("nested_parameter_t");
    const auto n = context.int_const("nested_parameter_n");
    const auto x = context.function("nested_parameter_x", context.int_sort(),
                                    context.int_sort());
    const auto inner = z3::ite(n > 2, context.int_val(3), context.int_val(2));
    const auto middle = z3::ite(n > 1, inner + 1, context.int_val(1));
    const auto increment = z3::ite(n > 0, middle + 1, context.int_val(0));
    rec_solver solver(context);
    solver.set_ind_var(t);
    z3::expr_vector keys(context), values(context);
    keys.push_back(x(0));
    values.push_back(context.int_val(0));
    solver.add_initial_values(keys, values);
    solver.set_eqs({context.bool_val(true)}, {{{x(t+1), x(t)+increment}}});
    ASSERT_TRUE(solver.solve());
    solver.apply_initial_values();
    const auto result = solver.get_res();
    ASSERT_EQ(result.size(), 1u);
    const auto closed = result.begin()->second;
    EXPECT_EQ(closed.to_string().find("ari_rec_parameter_"), std::string::npos);
    z3::solver checker(context);
    checker.add(t >= 0 && closed != t * increment);
    EXPECT_EQ(checker.check(), z3::unsat) << closed;
}

TEST(RecurrencePreparation, DoesNotAbstractIterationDependentConditionals) {
    z3::context context;
    const auto t = context.int_const("array_iteration");
    const auto n = context.int_const("bound");
    const auto x = context.function("array_value", context.int_sort(),
                                    context.int_sort());
    const auto value = z3::ite(t < n, context.int_val(0), t + 1);
    rec_solver solver(context);
    solver.set_ind_var(t);
    solver.set_eqs({t < n, t >= n},
                   {{{x(t+1), value}}, {{x(t+1), value}}});
    EXPECT_EQ(solver.rec2string().find("ari_rec_parameter_"), std::string::npos);
}

TEST(RecurrencePreparation, ProvesBoundedZeroCountAfterConditionalInitialization) {
    ScopedEnvironmentVariable model("ARITHEXE_DATA_MODEL", "ILP32");
    check_direct_verification_call("true_bounded_zero_count.c", HOLD);
}

TEST(RecurrencePreparation, DetectsInvalidBoundOnConditionalZeroCount) {
    ScopedEnvironmentVariable model("ARITHEXE_DATA_MODEL", "ILP32");
    check_direct_verification_call("false_bounded_zero_count.c", FAIL);
}

TEST(RecurrencePreparation, ProvesDelayedPeriodicZeroCount) {
    ScopedEnvironmentVariable model("ARITHEXE_DATA_MODEL", "ILP32");
    check_direct_verification_call("true_delayed_zero_count.c", HOLD);
}

TEST(RecurrencePreparation, DetectsWrongDelayedPeriodicZeroCountBound) {
    ScopedEnvironmentVariable model("ARITHEXE_DATA_MODEL", "ILP32");
    check_direct_verification_call("false_delayed_zero_count.c", FAIL);
}

TEST(RecurrencePreparation, DetectsUnclampedOddElements) {
    ScopedEnvironmentVariable model("ARITHEXE_DATA_MODEL", "ILP32");
    check_direct_verification_call("false_incomplete_pairwise_clamp.c", FAIL);
}

TEST(CompletedIterationAssumptions, RetainsEarlierScalarAssumptions) {
    check_direct_verification_call("true_prior_iteration_assumption.c", HOLD);
}

TEST(CompletedIterationAssumptions, ProvesBoundedRelaxedPrefixSuccessContract) {
    ScopedEnvironmentVariable model("ARITHEXE_DATA_MODEL", "ILP32");
    check_direct_verification_call("true_relaxed_prefix_bounded.c", HOLD);
}

TEST(CompletedIterationAssumptions, DetectsWrongRelaxedPrefixSpecification) {
    ScopedEnvironmentVariable model("ARITHEXE_DATA_MODEL", "ILP32");
    check_direct_verification_call("false_relaxed_prefix_bounded.c", FAIL);
}

TEST(CompletedIterationAssumptions, RetainsEarlierReadOnlyArrayAssumptions) {
    check_direct_verification_call("true_prior_array_assumption.c", HOLD);
}

TEST(CompletedIterationAssumptions, RetainsAssumptionsOnNormalLoopCompletion) {
    check_direct_verification_call("true_completed_loop_assumption.c", HOLD);
}

TEST(CompletedIterationAssumptions, FutureAssumptionsCannotHideFailure) {
    check_direct_verification_call("false_future_iteration_assumption.c", FAIL);
}

TEST(CompletedIterationAssumptions, LaterCurrentAssumptionCannotHideFailure) {
    check_direct_verification_call("false_late_current_assumption.c", FAIL);
}

TEST(CompletedIterationAssumptions, ConditionalAssumptionsRemainConditional) {
    check_direct_verification_call("false_conditional_prior_assumption.c", FAIL);
}

TEST(CompletedIterationAssumptions, DoesNotFreezeAnEvolvingArray) {
    check_direct_verification_call("true_changing_array_assumption.c", HOLD);
}

TEST(CompletedIterationAssumptions, ProjectedArrayHistoryCannotProveAnUnsafeProgram) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    Engine engine(benchmark_path("arrays/false_changing_array_assumption.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_NE(engine.verify(), HOLD);
}

TEST(CompletedIterationAssumptions, ProjectedHistoryCannotManufactureACounterexample) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    Engine engine(benchmark_path("arrays/true_projected_array_history.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_NE(engine.verify(), FAIL);
}

TEST(NondeterministicArraySummary, ProvesUpperClampOfFreshElements) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/true_nondet_clamp.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD) << engine.get_issue_message();
    EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
}

TEST(NondeterministicArraySummary, DetectsInvalidLowerClampAssertion) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/false_nondet_clamp.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL) << engine.get_issue_message();
    EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
}

TEST(NondeterministicArraySummary, DoesNotConflateDifferentIterations) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/false_nondet_elements_equal.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL) << engine.get_issue_message();
    EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
}

TEST(NondeterministicArraySummary, AuxiliaryCountersSurviveCopiesWithoutAddingSlots) {
    z3::context context;
    const auto n = context.int_const("n");
    const auto i = context.int_const("i");
    const auto counter = context.int_const("call_count");
    const auto oracle = context.function("oracle", context.int_sort(), context.int_sort());
    z3::expr_vector params(context), values(context), closed(context);
    params.push_back(i);
    values.push_back(context.int_val(8));
    closed.push_back(n);
    LoopSummary summary(params, values, closed, context.bool_val(true), {}, context.int_val(8));
    summary.add_auxiliary_closed_form(counter, n);
    LoopSummary copy(summary);
    EXPECT_EQ(copy.get_params().size(), 1u);
    EXPECT_EQ(copy.get_closed_forms().size(), 1u);
    EXPECT_TRUE(copy.get_modified_values().empty());
    EXPECT_TRUE(z3::eq(copy.evaluate_expr(oracle(counter)), oracle(n)));
    const auto first = copy.evaluate_expr(oracle(counter));
    const auto second = copy.evaluate_expr(oracle(counter+1));
    z3::solver solver(context);
    solver.add(first != second);
    EXPECT_EQ(solver.check(), z3::sat);
}

TEST(NondeterministicArraySummary, RetainsAssumptionsForEachExecutedIteration) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/true_nondet_assume.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD) << engine.get_issue_message();
    EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
}

TEST(NondeterministicArraySummary, RetainedAssumptionsDoNotStrengthenAssertions) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    for (const auto* file : {"arrays/false_nondet_assume.c",
                             "arrays/false_nondet_assume_zero.c"}) {
        SCOPED_TRACE(file);
        Engine engine(benchmark_path(file));
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_EQ(engine.verify(), FAIL) << engine.get_issue_message();
        EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
    }
}

TEST(NondeterministicArraySummary, LaterAbortCannotHideEarlierAssertionFailure) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/false_assert_before_later_assume.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL) << engine.get_issue_message();
    EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
}

TEST(NondeterministicArraySummary, ConditionalAssumptionsRemainPathSpecific) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    for (bool safe : {false, true}) {
        const auto* file = safe ? "arrays/true_nondet_conditional_assume.c"
                                : "arrays/false_nondet_conditional_assume.c";
        SCOPED_TRACE(file);
        Engine engine(benchmark_path(file));
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_EQ(engine.verify(), safe ? HOLD : FAIL) << engine.get_issue_message();
        EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
    }
}

TEST(NondeterministicArraySummary, ZeroTripDoesNotRequireBodyAssumptions) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/false_nondet_assume_zero_trip.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL) << engine.get_issue_message();
    EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
}

TEST(NondeterministicArraySummary, LoopStateCopiesRetainIndependentCounterMaps) {
    VerificationSession session;
    z3::context context;
    Memory memory;
    const Expression truth(context.bool_val(true));
    LoopState state(session, context, nullptr, nullptr, memory, truth, truth, {});
    state.unknown_call_counters.emplace(nullptr, context.int_val(7));
    LoopState copy(state);
    ASSERT_EQ(copy.unknown_call_counters.size(), 1u);
    EXPECT_TRUE(z3::eq(copy.unknown_call_counters.at(nullptr), context.int_val(7)));
    copy.unknown_call_counters.insert_or_assign(nullptr, context.int_val(8));
    EXPECT_TRUE(z3::eq(state.unknown_call_counters.at(nullptr), context.int_val(7)));
}

TEST(PathExpressionIntegration, CompressesAndAcceleratesSymbolicXYLoop) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_xy_phase.c"));
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_compression_count(), 0u);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(ArraySummaryComposition, ProvesSequentialArraySummaries) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    const auto run = run_benchmark("arrays/true_sequential_summary.c");
    EXPECT_EQ(run.result, HOLD) << run.issue_message;
    EXPECT_FALSE(run.has_issue) << run.issue_message;
}

namespace {
void check_helper_array_call(const char* filename, VeriResult expected) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path(std::string("arrays/") + filename));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), expected) << engine.get_issue_message();
    EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
}
}

TEST(HelperArraySummary, ProvesArrayParameterUpdates) {
    check_helper_array_call("true_helper_array.c", HOLD);
}

TEST(HelperArraySummary, DetectsWrongArrayParameterAssertion) {
    check_helper_array_call("false_helper_array.c", FAIL);
}

TEST(HelperArraySummary, PreservesOffsetsFramesAndZeroTripCalls) {
    check_helper_array_call("true_helper_array_offset.c", HOLD);
}

TEST(HelperArraySummary, DetectsWrongOffsetAssertion) {
    check_helper_array_call("false_helper_array_offset.c", FAIL);
}

TEST(HelperArraySummary, PreservesAliasedArrayParameters) {
    check_helper_array_call("true_helper_array_alias.c", HOLD);
}

TEST(HelperArraySummary, DetectsWrongAliasedArrayAssertion) {
    check_helper_array_call("false_helper_array_alias.c", FAIL);
}

TEST(FiniteAggregateIntegration, ProvesFiniteEntryArraySum) {
    check_helper_array_call("true_aggregate_sum.c", HOLD);
}

TEST(FiniteAggregateIntegration, DetectsWrongFiniteSumAssertion) {
    check_helper_array_call("false_aggregate_sum.c", FAIL);
}

TEST(FiniteAggregateIntegration, ProvesFiniteEntryArrayProduct) {
    check_helper_array_call("true_aggregate_product.c", HOLD);
}

TEST(FiniteAggregateIntegration, DetectsWrongFiniteProductAssertion) {
    check_helper_array_call("false_aggregate_product.c", FAIL);
}

TEST(FiniteAggregateIntegration, ProvesAverageAfterSwapAndRotation) {
    check_helper_array_call("true_aggregate_average.c", HOLD);
}

TEST(FiniteAggregateIntegration, DetectsWrongAverageAfterSwapAndRotation) {
    check_helper_array_call("false_aggregate_average.c", FAIL);
}

TEST(FiniteAggregateIntegration, UninterpretedSymbolicSumIsNotACounterexample) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/unknown_aggregate_sum.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), VERIUNKNOWN) << engine.get_issue_message();
    EXPECT_TRUE(engine.has_issue());
    EXPECT_EQ(engine.get_issue_kind(), VerifierIssueKind::IncompleteCounterexample);
}

TEST(SharedExitSummary, PreservesArrayAndHeaderLiveOuts) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/true_shared_exit_array.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD) << engine.get_issue_message();
    EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
}

TEST(SharedExitSummary, SequentialIterationCountsCannotProduceVacuousProofs) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/false_shared_exit_counts.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    const auto result = engine.verify();
    EXPECT_NE(result, HOLD);
    if (result == VERIUNKNOWN) {
        ASSERT_TRUE(engine.has_issue());
        EXPECT_EQ(engine.get_issue_kind(), VerifierIssueKind::IncompleteCounterexample);
    }
}

TEST(SharedExitSummary, KeepsEdgeSpecificPhiValuesAndPartialStores) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    for (const auto* file : {"arrays/false_shared_exit_phi.c",
                             "arrays/false_shared_exit_partial_store.c",
                             "arrays/false_shared_exit_array_guard.c",
                             "arrays/false_distinct_loop_exits.c"}) {
        SCOPED_TRACE(file);
        Engine engine(benchmark_path(file));
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_EQ(engine.verify(), FAIL) << engine.get_issue_message();
        EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
    }
}

TEST(ArraySummaryComposition, IntegerSemanticsRetainsRecurrenceSummarization) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    ScopedEnvironmentVariable model("ARITHEXE_DATA_MODEL", "ILP32");
    Engine engine(benchmark_path("arrays/true_integer_prefix_summary.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    testing::internal::CaptureStdout();
    const auto result = engine.verify();
    const auto output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(result, HOLD) << engine.get_issue_message();
    EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
    EXPECT_NE(output.find("Summarizing arrays"), std::string::npos);
}

TEST(ArraySummaryComposition, IntegerSemanticsStillDetectsFalseAssertions) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/false_sequential_summary.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL) << engine.get_issue_message();
    EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
}

TEST(ArraySummaryComposition, InvariantParityGuardsPreserveSafeAndUnsafeVerdicts) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    for (bool relaxed : {false, true}) {
        for (bool safe : {false, true}) {
            const auto file = safe ? "arrays/true_invariant_parity_update.c"
                                   : "arrays/false_invariant_parity_update.c";
            SCOPED_TRACE(std::string(file) + (relaxed ? " integer" : " fixed-width"));
            Engine engine(benchmark_path(file));
            engine.get_session().set_ignore_bitwidth_constraints(relaxed);
            EXPECT_EQ(engine.verify(), safe ? HOLD : FAIL) << engine.get_issue_message();
            EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
        }
    }
}

TEST(ArraySummaryComposition, IntegerSemanticsProvesEntryArrayAccumulator) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/true_modulo_summary.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD) << engine.get_issue_message();
    EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
}

TEST(ArraySummaryComposition, IntegerSemanticsDetectsWrongEntryArrayAccumulatorAssertion) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/false_modulo_summary.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL) << engine.get_issue_message();
    EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
}

TEST(ArraySummaryComposition, SolvesArrayDependentScalarAccumulator) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    const auto run = run_benchmark("arrays/true_modulo_summary.c");
    EXPECT_EQ(run.result, HOLD) << run.issue_message;
    EXPECT_FALSE(run.has_issue) << run.issue_message;
}

TEST(ArraySummaryComposition, DetectsWrongModuloSummaryAssertion) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    const auto run = run_benchmark("arrays/false_modulo_summary.c");
    EXPECT_EQ(run.result, FAIL) << run.issue_message;
    EXPECT_FALSE(run.has_issue) << run.issue_message;
}

TEST(ArraySummaryComposition, DoesNotAssumeANegativeCounterIsNonnegative) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    const auto run = run_benchmark("arrays/true_negative_counter_summary.c");
    EXPECT_EQ(run.result, HOLD) << run.issue_message;
    EXPECT_FALSE(run.has_issue) << run.issue_message;
}

TEST(ArraySummaryComposition, PreservedSlotsSurviveSummaryCopy) {
    llvm::LLVMContext llvm_context;
    llvm::Module module("summary-copy", llvm_context);
    auto* type = llvm::FunctionType::get(llvm::Type::getVoidTy(llvm_context), false);
    auto* kept = llvm::Function::Create(type, llvm::Function::ExternalLinkage,
                                        "kept", module);
    auto* changed = llvm::Function::Create(type, llvm::Function::ExternalLinkage,
                                           "changed", module);
    z3::context context;
    z3::expr_vector empty(context);
    LoopSummary summary(empty, empty, empty, context.bool_val(true), {}, std::nullopt);
    summary.add_modified_value(kept);
    summary.add_preserved_value(kept);
    LoopSummary copy(summary);
    EXPECT_TRUE(copy.preserves_value(kept));
    EXPECT_FALSE(copy.preserves_value(changed));
    EXPECT_EQ(copy.get_modified_values().size(), 1u);
}

TEST(ArraySummaryComposition, DetectsWrongSequentialArrayAssertion) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    const auto run = run_benchmark("arrays/false_sequential_summary.c");
    EXPECT_EQ(run.result, FAIL) << run.issue_message;
    EXPECT_FALSE(run.has_issue) << run.issue_message;
}

TEST(ArraySummaryComposition, ProvesComposedCubeAndOffsetCubeSummaries) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    for (const auto* file : {"arrays/true_composed_cube.c",
                             "arrays/true_composed_offset_cube.c"}) {
        SCOPED_TRACE(file);
        Engine engine(benchmark_path(file));
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_EQ(engine.verify(), HOLD) << engine.get_issue_message();
        EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
    }
}

TEST(ArraySummaryComposition, DetectsWrongComposedOffsetCubeAssertion) {
    ScopedEnvironmentVariable strategy("ARITHEXE_MULTIVARIATE_STRATEGY", "tupling");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/false_composed_offset_cube.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL) << engine.get_issue_message();
    EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
}

TEST(PathExpressionIntegration, EvaluatesPhiAssignmentsInParallel) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    for (bool relaxed : {false, true}) {
        Engine engine(benchmark_path("path_expression/true_phi_parallel.c"));
        engine.get_session().set_ignore_bitwidth_constraints(relaxed);
        EXPECT_EQ(engine.verify(), HOLD);
    }
}

TEST(IntegerRelaxation, RetainsUnsignedInputNonnegativity) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_unsigned_nondet_domain.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(IntegerRelaxation, BoundsEachNondetInputInTheLoopBody) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_bounded_nondet_body.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
}

TEST(IntegerRelaxation, AcceleratesLargeUnsignedThreshold) {
    ScopedEnvironmentVariable normal("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("path_expression/true_unsigned_large_threshold.c"));
    engine.get_session().set_ignore_32bit_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GE(engine.get_session().path_acceleration_count(), 2u);
}

TEST(IntegerRelaxation, DetectsWrongLargeThresholdAssertion) {
    ScopedEnvironmentVariable normal("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("path_expression/false_unsigned_large_threshold.c"));
    engine.get_session().set_ignore_32bit_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GE(engine.get_session().path_acceleration_count(), 2u);
}

TEST(IntegerRelaxation, IsExplicitAndDoesNotLeakBetweenEngines) {
    const auto path = benchmark_path("path_expression/true_unsigned_wrap_profile.c");
    Engine relaxed(path);
    relaxed.get_session().set_ignore_32bit_constraints(true);
    EXPECT_EQ(relaxed.verify(), FAIL);
    Engine fixed_width(path);
    EXPECT_FALSE(fixed_width.get_session().ignores_integer_width(32));
    EXPECT_EQ(fixed_width.verify(), HOLD);
    EXPECT_FALSE(relaxed.get_session().ignores_integer_width(1));
    EXPECT_TRUE(relaxed.get_session().ignores_integer_width(8));
    EXPECT_TRUE(relaxed.get_session().ignores_integer_width(16));
    EXPECT_TRUE(relaxed.get_session().ignores_integer_width(64));
    EXPECT_TRUE(relaxed.get_session().ignores_integer_width(128));
}

TEST(IntegerRelaxation, AcceleratesAlternatingUnsignedSumt2) {
    Engine engine(benchmark_path("path_expression/true_sumt2.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(NestedPathAcceleration, AcceleratesFiveLevelsAndLargeOuterCount) {
    Engine engine(benchmark_path("path_expression/true_nested5_1.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().nested_path_summary_count(), 5u);
    EXPECT_EQ(engine.get_session().path_acceleration_count(), 5u);
}

TEST(NestedArrayRecurrence, PreservesPartialWriteDomainsAcrossOuterIterations) {
    ScopedEnvironmentVariable normal("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("arrays/true_nested_array_domains.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().unsupported_path_profile_count(true), 0u);
}

TEST(NestedArrayRecurrence, AccumulatesChainedGepOffsetsBeforeSummarization) {
    Engine engine(benchmark_path("arrays/true_chained_array_addresses.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
}

TEST(NestedArrayRecurrence, PreservesZeroTripInnerExitAndUntouchedElements) {
    Engine engine(benchmark_path("arrays/true_nested_array_zero_trip.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
}

TEST(NestedArrayRecurrence, ComposesThreeDimensions) {
    Engine engine(benchmark_path("arrays/true_nested_array_3d.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
}

TEST(NestedArrayRecurrence, ChecksAssertionsAtLaterOuterIterations) {
    Engine engine(benchmark_path("arrays/false_nested_array_late_assert.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
}

TEST(NestedArrayRecurrence, DoesNotReuseNondeterministicValuesAcrossRows) {
    Engine engine(benchmark_path("arrays/false_nested_array_fresh_inputs.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_NE(engine.verify(), HOLD);
}

TEST(NestedArrayRecurrence, LaterAbortDoesNotHideEarlierAssertion) {
    Engine engine(benchmark_path("arrays/false_nested_array_assert_before_abort.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_NE(engine.verify(), HOLD);
}

TEST(NestedArrayRecurrence, UnsupportedProfileCachesAreSessionLocal) {
    VerificationSession session;
    session.mark_unsupported_path_profile(nullptr, true);
    session.mark_unsupported_path_profile(nullptr, false);
    EXPECT_EQ(session.unsupported_path_profile_count(true), 1u);
    EXPECT_EQ(session.unsupported_path_profile_count(false), 1u);
    session.clear_module_state();
    EXPECT_EQ(session.unsupported_path_profile_count(true), 0u);
    EXPECT_EQ(session.unsupported_path_profile_count(false), 0u);
    VerificationSession other;
    EXPECT_EQ(other.unsupported_path_profile_count(true), 0u);
}

TEST(NestedArrayRecurrence, CopiesRelationalIterationCountMetadata) {
    z3::context ctx;
    z3::expr_vector empty(ctx);
    auto count = ctx.int_const("unresolved_child_count");
    LoopSummary summary(empty, empty, empty, count >= 0, {}, count);
    summary.set_functional_iteration_count(false);
    LoopSummary copied(summary);
    EXPECT_FALSE(copied.has_functional_iteration_count());
}

TEST(NestedArrayRecurrence, GuardHullMustBeExact) {
    z3::context ctx;
    const auto k = ctx.int_const("guard_k"), n = ctx.int_const("guard_n");
    const auto d = 5*n-k;
    const auto guard = n >= 1 && k >= 0 &&
        (d == 1 || (d >= 2 && d <= 5));
    auto normalized = normalize_array_guard(guard);
    z3::solver solver(ctx);
    solver.add(normalized != guard);
    EXPECT_EQ(solver.check(), z3::unsat);
    EXPECT_EQ(normalized.to_string().find("(or "), std::string::npos);
    auto row = (k-5*n)/5 == 0;
    auto normalized_row = normalize_array_guard(row);
    solver.reset();
    solver.add(normalized_row != row);
    EXPECT_EQ(solver.check(), z3::unsat);
    EXPECT_EQ(normalized_row.to_string().find("div"), std::string::npos);
    auto conditional = z3::ite(k < 5*n, ctx.int_val(0), k-5*n) < 5;
    auto normalized_conditional = normalize_array_guard(conditional);
    solver.reset();
    solver.add(normalized_conditional != conditional);
    EXPECT_EQ(solver.check(), z3::unsat);
    EXPECT_EQ(normalized_conditional.to_string().find("ite"), std::string::npos);
    auto holes = k == 0 || k == 2;
    solver.reset();
    solver.add(normalize_array_guard(holes) != holes);
    EXPECT_EQ(solver.check(), z3::unsat);
    const auto input = ctx.function("guard_input", ctx.int_sort(), ctx.int_sort());
    EXPECT_TRUE(z3::eq(normalize_array_guard(input(k) > 0), (input(k) > 0).simplify()));
}

TEST(NestedPathAcceleration, AlsoCertifiesFiveLevelsWithFixedWidthGuards) {
    Engine engine(benchmark_path("path_expression/true_nested5_1.c"));
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().nested_path_summary_count(), 5u);
    EXPECT_EQ(engine.get_session().path_acceleration_count(), 5u);
}

TEST(NestedPathAcceleration, RetainsFixedWidthDefinednessAndModeIsolation) {
    const auto path = benchmark_path("path_expression/true_nested_overflow.c");
    Engine fixed(path);
    EXPECT_EQ(fixed.verify(), HOLD);
    Engine relaxed(path);
    relaxed.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(relaxed.verify(), FAIL);
}

TEST(NestedPathAcceleration, DoesNotHideAssertionAfterChildExit) {
    Engine engine(benchmark_path("path_expression/false_nested_assert.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().nested_path_summary_count(), 0u);
}

TEST(NestedPathAcceleration, CachedChildRetainsOuterLiveInAndLateViolation) {
    Engine engine(benchmark_path("path_expression/false_nested_late_assert.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().nested_path_summary_count(), 0u);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(NestedPathAcceleration, PreservesZeroTripAndResetLiveOuts) {
    Engine engine(benchmark_path("path_expression/true_nested_zero_reset.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().nested_path_summary_count(), 2u);
}

TEST(NestedPathAcceleration, SupportsDescendingAndNonUnitStrides) {
    Engine engine(benchmark_path("path_expression/true_nested_descending.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().nested_path_summary_count(), 2u);
}

TEST(NestedPathAcceleration, DoesNotSummarizeFreshBodyChoices) {
    Engine engine(benchmark_path("path_expression/false_nested_unknown_call.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_EQ(engine.get_session().nested_path_summary_count(), 0u);
}

TEST(NestedPathAcceleration, RetainsOrdinaryExecutionForMultipleBodyPaths) {
    Engine engine(benchmark_path("path_expression/true_nested_multiple_paths.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().nested_path_summary_count(), 0u);
}

TEST(PathExpressionIntegration, AcceleratesModuloGuardedParityLoop) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_parity_phase.c"));
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_compression_count(), 0u);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(PathExpressionIntegration, DoesNotHideViolationBehindAcceleration) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/false_xy_phase.c"));
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(PathExpressionIntegration, SummarizesNondeterministicPolynomialFlagLoop) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_nondet_flag.c"));
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(PathExpressionIntegration, SelectsSequenceAccelerationInNormalMode) {
    ScopedEnvironmentVariable normal("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("path_expression/true_nondet_flag.c"));
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(PathExpressionIntegration, RetainsNondeterministicZeroIterationExit) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_nondet_zero.c"));
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(PathExpressionIntegration, ReconstructsAcceleratedBooleanSequence) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/false_nondet_count.c"));
    ASSERT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    // One real observed iteration suffices. The preheader is not a path.
    EXPECT_EQ(engine.get_session().path_compression_count(), 1u);
    const auto& inputs = engine.get_counterexample_inputs();
    ASSERT_GE(inputs.size(), 3u);
    for (std::size_t index = 0; index + 1 < inputs.size(); ++index) {
        EXPECT_EQ(inputs[index].value, "1");
    }
    EXPECT_EQ(inputs.back().value, "0");
}

TEST(PathExpressionIntegration, ReconstructsInvertedBooleanSequence) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/false_nondet_inverted.c"));
    ASSERT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    const auto& inputs = engine.get_counterexample_inputs();
    ASSERT_GE(inputs.size(), 3u);
    for (std::size_t index = 0; index + 1 < inputs.size(); ++index) {
        EXPECT_EQ(inputs[index].value, "0");
    }
    EXPECT_EQ(inputs.back().value, "1");
}

TEST(PathExpressionIntegration, FindsFalseEqualityInNondeterministicFlagLoop) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/false_nondet_flag.c"));
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(PathExpressionIntegration, FindsViolationOnZeroIterationExit) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/false_nondet_zero.c"));
    ASSERT_EQ(engine.verify(), FAIL);
    const auto& inputs = engine.get_counterexample_inputs();
    ASSERT_EQ(inputs.size(), 1u);
    EXPECT_EQ(inputs.front().value, "0");
}

TEST(PathExpressionIntegration, DoesNotDiscardChangingBodyPaths) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/false_nondet_changing_path.c"));
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(PathExpressionIntegration, ExcludesSignedOverflowFromAcceleratedPaths) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_nondet_overflow.c"));
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(PathExpressionIntegration, FallsBackForDataAndExternalCalls) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    for (const std::string& filename : {
             "true_unsupported_data_call.c",
             "true_unsupported_external_call.c"}) {
        Engine engine(benchmark_path("path_expression/" + filename));
        EXPECT_EQ(engine.verify(), HOLD) << filename;
        EXPECT_EQ(engine.get_session().path_acceleration_count(), 0u)
            << filename;
    }
}

TEST(VerificationSessionTest, EnginesKeepIndependentModuleState) {
    Engine loop_engine(benchmark_path("loops/true_3.c"));
    Engine failing_engine(benchmark_path("loop_free/false_1.c"));

    EXPECT_NE(&loop_engine.get_session(), &failing_engine.get_session());
    EXPECT_NE(&loop_engine.get_session().analyses().get_z3ctx(),
              &failing_engine.get_session().analyses().get_z3ctx());

    EXPECT_EQ(loop_engine.verify(), HOLD);
    EXPECT_EQ(failing_engine.verify(), FAIL);
    EXPECT_EQ(loop_engine.verify(), HOLD);
}

TEST(EngineRegression, ReverifyStartsWithFreshStateQueue) {
    Engine engine(
        benchmark_path("loop_free/false_reverify_pending_states.c"));
    llvm::Module* module = engine.get_module();
    llvm::Function* unsafe_entry = module->getFunction("unsafe_entry");
    llvm::Function* safe_entry = module->getFunction("safe_entry");

    ASSERT_NE(unsafe_entry, nullptr);
    ASSERT_NE(safe_entry, nullptr);

    engine.set_entry(unsafe_entry);
    ASSERT_EQ(engine.verify(), FAIL);

    engine.set_entry(safe_entry);
    EXPECT_EQ(engine.verify(), HOLD);
}

TEST(IntegerSemantics, CastsPreserveLLVMBitPatterns) {
    EXPECT_EQ(verify_benchmark("loop_free/true_integer_casts.c"), HOLD);
}

TEST(IntegerSemantics, UnsignedOperationsUseUnsignedInterpretation) {
    EXPECT_EQ(verify_benchmark("loop_free/true_unsigned_integer_ops.c"),
              HOLD);
}

TEST(IntegerSemantics, BitwiseAndShiftOperationsUseFixedWidths) {
    EXPECT_EQ(verify_benchmark("loop_free/true_bitwise_shifts.c"), HOLD);
}

TEST(IntegerSemantics, PoisonShiftPathIsUndefined) {
    EXPECT_EQ(verify_benchmark("loop_free/true_poison_integer_ops.c"), HOLD);
}

TEST(IntegerSemantics, UnsafeUnsignedShiftIsReported) {
    EXPECT_EQ(verify_benchmark("loop_free/false_unsigned_shift.c"), FAIL);
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

TEST(Z3UnknownPropagationRegression,
     UnsafeProgramMustNotBeReportedAsHold) {
    auto run = run_benchmark("loops/false_z3_unknown_branch.c");
    constexpr VeriResult expected_result = VERIUNKNOWN;

    EXPECT_TRUE(run.has_issue);
    EXPECT_EQ(run.issue_kind, VerifierIssueKind::Z3Unknown);
    EXPECT_EQ(run.result, expected_result)
        << "Ground truth is UNSAFE; an unexplored branch must produce UNKNOWN, "
           "never HOLD";
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
