#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#include "engine.h"
#include "LoopSummarizer.h"
#include "PathAccelerator.h"

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

// These tests isolate the coarse proof and rollback mechanics. The separate
// PreciseFirstScheduling suite exercises the production/default ordering.
class CoarsePathFallback : public ::testing::Test {
    ScopedEnvironmentVariable eager_coarse{"ARITHEXE_PRECISE_TRIAL_ITERATIONS", "0"};
};
}

TEST(PreciseFirstScheduling, CompressesSequentialLoopsBeforeCoarseSynthesis) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "1");
    ScopedEnvironmentVariable budget("ARITHEXE_PRECISE_TRIAL_ITERATIONS", "");
    // An invalid lemma path must not matter when precise reasoning suffices.
    ScopedEnvironmentVariable lemmas("ARITHEXE_COARSE_LEMMA_FILE", "/nonexistent/precise-first.smt2");
    Engine engine(benchmark_path("path_expression/true_precise_sequential_count.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GE(engine.get_session().path_acceleration_count(), 3u);
    EXPECT_GT(engine.get_session().path_compression_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_summary_attempt_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PreciseFirstScheduling, FiveUnsignedTransfersPreserveExactRelations) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable safety("ARITHEXE_EXIT_GUARD_FALLBACK", "1");
    Engine engine(benchmark_path("path_expression/true_precise_five_transfers.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GE(engine.get_session().path_acceleration_count(), 5u);
    EXPECT_EQ(engine.get_session().coarse_summary_attempt_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PreciseFirstScheduling, BoundedTrialFallsBackFromUnchangedEntry) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "1");
    ScopedEnvironmentVariable budget("ARITHEXE_PRECISE_TRIAL_ITERATIONS", "1");
    ScopedEnvironmentVariable evidence("ARITHEXE_PATH_EVIDENCE", "2");
    const auto worker = benchmark_path("../precise_rejecting_worker.py");
    ScopedEnvironmentVariable backend("ARITHEXE_SOLVER_WORKER", worker.c_str());
    Engine engine(benchmark_path("path_expression/true_coarse_cubic_retry.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_compression_count(), 0u);
    EXPECT_EQ(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_degree_attempt_count(2), 1u);
    EXPECT_EQ(engine.get_session().coarse_degree_attempt_count(3), 1u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PreciseFirstScheduling, ChecksAssertionsDuringTheTrial) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable budget("ARITHEXE_PRECISE_TRIAL_ITERATIONS", "12");
    Engine engine(benchmark_path("path_expression/false_precise_trial_assert.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_EQ(engine.get_session().coarse_summary_attempt_count(), 0u);
}

TEST(PreciseFirstScheduling, UnsupportedPreciseProfileStillUsesCoarseProof) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "1");
    ScopedEnvironmentVariable budget("ARITHEXE_PRECISE_TRIAL_ITERATIONS", "12");
    Engine engine(benchmark_path("path_expression/true_coarse_uint_eq.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PreciseFirstScheduling, EntryRootedFallbackRetainsUnsafeExternalSibling) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "1");
    ScopedEnvironmentVariable budget("ARITHEXE_PRECISE_TRIAL_ITERATIONS", "1");
    const auto worker = benchmark_path("../precise_rejecting_worker.py");
    ScopedEnvironmentVariable backend("ARITHEXE_SOLVER_WORKER", worker.c_str());
    Engine engine(benchmark_path("path_expression/false_precise_trial_sibling.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().path_compression_count(), 0u);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
}

TEST(PathExpressionIntegration, CompressesAndAcceleratesSymbolicXYLoop) {
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    ScopedEnvironmentVariable lemmas("ARITHEXE_COARSE_LEMMA_FILE", "/nonexistent/arithexe-precise-only-lemma.smt2");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_xy_phase.c"));
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_compression_count(), 0u);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_lemma_query_count(), 0u);
}

TEST(PathExpressionIntegration, InfersAndSubstitutesGaussSumExitCount) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/true_precise_gauss_sum.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_affine_template_count(), 0u);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PathExpressionIntegration, ProvesWatermelonWithQuantifiedCoverageFallback) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/true_watermelon.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PathExpressionIntegration, SymbolicIncrementSafetyDoesNotRequireTerminationProof) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_symbolic_step_exit.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().coarse_lemma_query_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PathExpressionIntegration, SummarizesPureScalarHelperInsideLoop) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_pure_helper_loop.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PathExpressionIntegration, PureHelperSummaryRetainsViolation) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/false_pure_helper_loop.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(PathExpressionIntegration, ConditionalNestedHelperSummaryAndEffectRejection) {
    Engine engine(benchmark_path("path_expression/pure_helper_profiles.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    auto activation = engine.get_session().activate();
    auto& context = engine.get_session().analyses().get_z3ctx();
    auto* nested = engine.get_module()->getFunction("nested");
    ASSERT_TRUE(is_pure_scalar_helper(nested));
    auto summary = summarize_pure_scalar_helper(nested, context);
    ASSERT_TRUE(summary.has_value());
    auto x = summary->get_params()[0];
    z3::solver check(context);
    check.set("timeout", 3000u);
    check.add(summary->get_summary() != z3::ite(x < 10, x + 3, x + 5));
    EXPECT_EQ(check.check(), z3::unsat);
    EXPECT_FALSE(is_pure_scalar_helper(engine.get_module()->getFunction("effect")));
    EXPECT_FALSE(is_pure_scalar_helper(engine.get_module()->getFunction("recursive")));
}

TEST(PathExpressionIntegration, ProjectsFreshBooleanPhisAndCertifiesNonnegative) {
    Engine engine(benchmark_path("path_expression/true_boolean_phi_control.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
}

TEST(PathExpressionIntegration, BooleanPhiProjectionRetainsBranchCounterexample) {
    Engine engine(benchmark_path("path_expression/false_boolean_phi_control.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
}

TEST(PathExpressionIntegration, GuardedNonnegativeDoesNotAssumeNoOvershoot) {
    Engine engine(benchmark_path("path_expression/false_guarded_nonnegative.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
}

TEST(PathExpressionIntegration, DoesNotProjectBooleanPhiData) {
    Engine engine(benchmark_path("path_expression/false_boolean_phi_data.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 0u);
}

TEST(PathExpressionIntegration, ScalarGlobalTransformerAcceleratesAndRestoresMemory) {
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/true_global_transformer_loop.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_FALSE(engine.get_session().state_transformer_summaries().empty());
    EXPECT_FALSE(engine.has_issue());
}

TEST(PathExpressionIntegration, ScalarGlobalTransformerRetainsCounterexample) {
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/false_global_transformer_loop.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(PathExpressionIntegration, ScalarGlobalTransformerUsesEntryInputsAndSequentialStores) {
    Engine engine(benchmark_path("path_expression/global_transformer_profiles.c"));
    auto activation = engine.get_session().activate();
    auto& ctx = engine.get_session().analyses().get_z3ctx();
    auto summary = summarize_scalar_state_helper(engine.get_module()->getFunction("update"), ctx);
    ASSERT_TRUE(summary);
    ASSERT_EQ(summary->parameters.size(), 3u);
    ASSERT_EQ(summary->updates.size(), 2u);
    auto a = summary->parameters[0], x = summary->parameters[1], y = summary->parameters[2];
    z3::solver certificate(ctx);
    certificate.add(summary->updates[0] != x + a || summary->updates[1] != x + a - y || *summary->result != x);
    EXPECT_EQ(certificate.check(), z3::unsat);
    EXPECT_FALSE(is_scalar_state_helper(engine.get_module()->getFunction("volatile_update")));
    EXPECT_FALSE(is_scalar_state_helper(engine.get_module()->getFunction("pointer_update")));
    EXPECT_FALSE(is_scalar_state_helper(engine.get_module()->getFunction("conditional_update")));
}

TEST(PathExpressionIntegration, NondeterministicPeriodicWordsIncludeAllOffsets) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/true_nondet_periodic_offsets.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PathExpressionIntegration, NondeterministicPeriodicWordsRetainMidCycleViolation) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/false_nondet_periodic_offset.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(PathExpressionIntegration, SafetyExitOverapproximationDoesNotHideOvershoot) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/false_symbolic_step_exit.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    auto result = engine.verify();
    EXPECT_NE(result, HOLD);
    EXPECT_TRUE(result == FAIL || result == VERIUNKNOWN);
}

TEST(PathExpressionIntegration, QuantifiedCoverageRetainsWatermelonViolation) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/false_watermelon.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PathExpressionIntegration, SubstitutedGaussSumCountRetainsViolation) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/false_precise_gauss_sum.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().path_affine_template_count(), 0u);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PathExpressionIntegration, ExactRecurrenceDoesNotChangeCDivision) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/false_precise_gauss_c_div.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PathExpressionIntegration, AcceleratesConditionalLatchConjunctiveLoop) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/true_conditional_latch_conjunctive.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_summary_attempt_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PathExpressionIntegration, AcceleratesConditionalLatchWithFixedWidthDefinedness) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/true_conditional_latch_conjunctive.c"));
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_summary_attempt_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PathExpressionIntegration, RetainsReachableConditionalLatchExit) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/true_conditional_latch_live_break.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_summary_attempt_count(), 0u);
}

TEST(PathExpressionIntegration, FindsViolationAtConditionalLatchExit) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/false_conditional_latch_live_break.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_summary_attempt_count(), 0u);
}

TEST(GuardedResidueFallback, ProvesMod3WithIntegerSelectControls) {
    Engine engine(benchmark_path("path_expression/true_guarded_mod3.c"));
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().residue_proof_count(), 1u);
    EXPECT_EQ(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 0u);
}

TEST(GuardedResidueFallback, ProvesReducerSpillsAndGotoCycles) {
    Engine engine(benchmark_path("path_expression/true_guarded_mod3_spills.c"));
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().residue_proof_count(), 1u);
}

TEST(GuardedResidueFallback, CanBeSelectedInRelaxedModeWithoutErasingWrap) {
    Engine engine(benchmark_path("path_expression/true_guarded_mod3.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().residue_proof_count(), 1u);
}

TEST(GuardedResidueFallback, WrongResidueUpdateFallsBackToExactCounterexample) {
    Engine engine(benchmark_path("path_expression/false_guarded_mod3.c"));
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_EQ(engine.get_session().residue_proof_count(), 0u);
    EXPECT_GT(engine.get_session().residue_attempt_count(), 0u);
}

TEST(GuardedResidueFallback, ChecksAssertionsInsideTheLoop) {
    Engine engine(benchmark_path("path_expression/false_guarded_mod3_internal.c"));
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_EQ(engine.get_session().residue_proof_count(), 0u);
}

TEST(GuardedResidueFallback, DoesNotEraseUnsignedWraparound) {
    Engine engine(benchmark_path("path_expression/false_guarded_mod3_wrap.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_EQ(engine.get_session().residue_proof_count(), 0u);
}

TEST(GuardedResidueFallback, DoesNotDiscardAnObservedGlobal) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/false_guarded_mod3_observed_global.c"));
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_EQ(engine.get_session().residue_proof_count(), 0u);
}

TEST(GuardedResidueFallback, BudgetFailureLeavesOrdinaryStateUntouched) {
    ScopedEnvironmentVariable budget("ARITHEXE_RESIDUE_TIMEOUT_MS", "0");
    Engine engine(benchmark_path("path_expression/false_guarded_mod3.c"));
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_EQ(engine.get_session().residue_proof_count(), 0u);
    EXPECT_GT(engine.get_session().residue_attempt_count(), 0u);
}

TEST(GuardedResidueFallback, AppliesParallelPhiEdgesAndPreservesZeroTrips) {
    Engine engine(benchmark_path("path_expression/true_guarded_mod3_swap.c"));
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().residue_proof_count(), 1u);
}

TEST(GuardedResidueFallback, DoesNotFreezeOrProjectAwayOracleData) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/false_guarded_mod3_data.c"));
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_EQ(engine.get_session().residue_proof_count(), 0u);
    EXPECT_GT(engine.get_session().residue_attempt_count(), 0u);
}

TEST_F(CoarsePathFallback, ProjectsControlOnlyIntegerSelects) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_coarse_int_select_sum.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_EQ(engine.get_session().residue_proof_count(), 0u);
}

TEST_F(CoarsePathFallback, PreservesCorrelationBetweenIntegerSelects) {
    Engine engine(benchmark_path("path_expression/true_coarse_correlated_selects.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST_F(CoarsePathFallback, CorrelatedSelectsRetainCounterexamples) {
    Engine engine(benchmark_path("path_expression/false_coarse_correlated_selects.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
}

TEST(PathExpressionIntegration, RecognizesOldStyleBooleanSelectExitGuard) {
    Engine engine(benchmark_path("path_expression/true_bool_select_exit_guard.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST_F(CoarsePathFallback, BooleanFanoutDoesNotHideDataCounterexamples) {
    Engine engine(benchmark_path("path_expression/false_bool_select_data.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_FALSE(engine.has_issue());
}

TEST_F(CoarsePathFallback, OldStyleBooleanSelectsKeepOneCorrelatedChoice) {
    Engine engine(benchmark_path("path_expression/true_coarse_bool_correlated_selects.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PathExpressionIntegration, BooleanExitGuardProofRollsBackOnUnsafeAssertion) {
    Engine engine(benchmark_path("path_expression/false_bool_select_exit_guard.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_FALSE(engine.has_issue());
}

TEST_F(CoarsePathFallback, ReplaysAllMultiExitFragmentsIncludingZeroTrips) {
    Engine engine(benchmark_path("path_expression/true_coarse_multi_exit.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_refinement_count(), 0u);
    EXPECT_EQ(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST_F(CoarsePathFallback, ReplaysFreshNondeterministicHeadersWithInternalExits) {
    Engine engine(benchmark_path("path_expression/true_coarse_multi_exit_nondet_header.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_refinement_count(), 0u);
}

TEST_F(CoarsePathFallback, ReplaysNestedInternalAssertionsAndAllExits) {
    Engine engine(benchmark_path("path_expression/true_coarse_multi_exit_nested.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_refinement_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST_F(CoarsePathFallback, PreservesIntermediateUpdatesAndExitPhis) {
    Engine engine(benchmark_path("path_expression/true_coarse_multi_exit_phi.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_refinement_count(), 0u);
}

TEST_F(CoarsePathFallback, NeverAcceptsAWrongInternalExitState) {
    Engine engine(benchmark_path("path_expression/false_multi_exit_update.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_GT(engine.get_session().coarse_refinement_count(), 0u);
}

TEST_F(CoarsePathFallback, RollsBackAllNestedMultiExitObligations) {
    Engine engine(benchmark_path("path_expression/false_coarse_multi_exit_nested.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_GT(engine.get_session().coarse_refinement_count(), 0u);
}

TEST_F(CoarsePathFallback, ExpandsBoundaryObligationsForLaterLoops) {
    Engine engine(benchmark_path("path_expression/false_coarse_multi_exit_later_loop.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 1u);
    EXPECT_GT(engine.get_session().coarse_refinement_count(), 0u);
}

TEST(PathExpressionIntegration, PreciseFirstBreakReplaysInternalExits) {
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_precise_multi_exit.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PathExpressionIntegration, PreciseFirstBreakStopsBeforeAFailingInternalAssertion) {
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/false_precise_internal_assert.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(PathExpressionIntegration, DeclaredInternalAssertionsRemainLegacyObligations) {
    Engine engine(benchmark_path("path_expression/false_precise_internal_assert.c"));
    auto& session = engine.get_session();
    auto activation = session.activate();
    auto& context = session.analyses().get_z3ctx();
    auto* function = engine.get_module()->getFunction("main");
    auto& loops = session.analyses().get_LI(function);
    ASSERT_EQ(std::distance(loops.begin(), loops.end()), 1);
    auto* loop = *loops.begin();
    Memory memory;
    memory.push_frame(function);
    auto parent = std::make_shared<State>(session, context,
        AInstruction::create(&*loop->getHeader()->begin()), nullptr,
        memory, Expression(context.bool_val(true)), trace_ty{});
    auto entry_probe = std::make_shared<State>(*parent);
    ASSERT_NE(loop->getLoopPreheader(), nullptr);
    entry_probe->trace.push_back(loop->getLoopPreheader());
    EXPECT_EQ(PathExpressionAccelerator::accelerate_exit_guard(loop, entry_probe), nullptr)
        << "An exit-only summary must never skip an internal assertion";
    LoopExecution execution(loop, parent);
    auto [final_states, exit_states] = execution.run();
    ASSERT_FALSE(execution.had_unknown_result());
    ASSERT_FALSE(final_states.empty());
    ASSERT_FALSE(exit_states.empty());
    ASSERT_FALSE(execution.get_v_conditions().empty());
    EXPECT_FALSE(final_states.front()->summary_invariants.empty());
}

TEST(PathExpressionIntegration, FixedWidthLegacyExecutionNeverSkipsInternalAssertions) {
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "0");
    Engine engine(benchmark_path("path_expression/false_coarse_internal_assert.c"));
    EXPECT_NE(engine.verify(), HOLD);
}

TEST(CoarseLemmaQueries,LoadsRootLemmasAndProvesDivisibility) {
    VerificationSession session;
    auto activation = session.activate();
    auto& context = session.analyses().get_z3ctx();
    auto n = context.int_const("lemma_test_n");
    auto numerator = n * (1 + 2*n*n + 3*n);
    z3::solver query(context);
    query.add(6 * (numerator / 6) != numerator);
    EXPECT_EQ(session.check_coarse_query(query, 2000), z3::unsat);
    EXPECT_EQ(session.coarse_lemma_count(), 7u);
    EXPECT_EQ(session.coarse_lemma_query_count(), 1u);
    EXPECT_EQ(session.coarse_lemma_retry_count(), 0u);
    EXPECT_EQ(query.assertions().size(), 8u);
    // Parse once per context and re-add after solver.reset().
    query.reset();
    query.add(6 * (numerator / 6) != numerator);
    EXPECT_EQ(session.check_coarse_query(query, 2000), z3::unsat);
    EXPECT_EQ(session.coarse_lemma_query_count(), 2u);
}

TEST(CoarseLemmaQueries,UnknownQuantifiedFeasibilityRetriesCleanly) {
    VerificationSession session;
    auto activation = session.activate();
    auto& context = session.analyses().get_z3ctx();
    auto x = context.int_const("lemma_test_positive_x");
    z3::solver query(context);
    query.add(x > 0);
    EXPECT_EQ(session.check_coarse_query(query, 2000), z3::sat);
    EXPECT_EQ(session.coarse_lemma_query_count(), 1u);
    // Z3 may solve the quantified query directly in future versions.
    EXPECT_LE(session.coarse_lemma_retry_count(), 1u);
    if (session.coarse_lemma_retry_count()) EXPECT_EQ(query.assertions().size(), 1u);
    EXPECT_TRUE(query.get_model().eval(x > 0).is_true());
    z3::solver ordinary(context);
    ordinary.add(x > 0);
    EXPECT_EQ(ordinary.assertions().size(), 1u);
    EXPECT_EQ(ordinary.check(), z3::sat);
}

TEST(CoarseLemmaQueries,MissingOrMalformedLemmaFileRejectsQuery) {
    VerificationSession session;
    auto activation = session.activate();
    auto& context = session.analyses().get_z3ctx();
    z3::solver query(context);
    query.add(context.bool_val(false));
    {
        ScopedEnvironmentVariable file("ARITHEXE_COARSE_LEMMA_FILE", "/nonexistent/arithexe-coarse-lemma.smt2");
        EXPECT_THROW(session.check_coarse_query(query, 2000), std::runtime_error);
    }
    {
        const auto malformed = benchmark_path("../coarse_malformed_lemma.smt2");
        ScopedEnvironmentVariable file("ARITHEXE_COARSE_LEMMA_FILE", malformed.c_str());
        EXPECT_THROW(session.check_coarse_query(query, 2000), std::runtime_error);
    }
    EXPECT_EQ(session.coarse_lemma_query_count(), 0u);
    EXPECT_EQ(session.coarse_lemma_count(), 0u);
}

TEST_F(CoarsePathFallback, ProvesSumWithoutSelectingAnExactBranchWord) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_coarse_branch_sum.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_EQ(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_degree_attempt_count(2), 1u);
    EXPECT_EQ(engine.get_session().coarse_degree_attempt_count(3), 0u);
    EXPECT_GT(engine.get_session().coarse_lemma_query_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_lemma_count(), 7u);
}

TEST_F(CoarsePathFallback, RetriesDegreeThreeAfterDegreeTwoFailsToProve) {
    Engine engine(benchmark_path("path_expression/true_coarse_cubic_retry.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().coarse_degree_attempt_count(2), 1u);
    EXPECT_EQ(engine.get_session().coarse_degree_attempt_count(3), 1u);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 2u);
    EXPECT_EQ(engine.get_session().coarse_refinement_count(), 0u);
    EXPECT_EQ(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST_F(CoarsePathFallback, ExplicitDegreePinsTheProofLevel) {
    ScopedEnvironmentVariable degree("ARITHEXE_COARSE_POLY_EXPR_DEGREE", "2");
    Engine engine(benchmark_path("path_expression/true_coarse_cubic_retry.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().coarse_degree_attempt_count(2), 1u);
    EXPECT_EQ(engine.get_session().coarse_degree_attempt_count(3), 0u);
    EXPECT_GT(engine.get_session().coarse_refinement_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST_F(CoarsePathFallback, InvalidLemmaFilePreservesPreciseExecution) {
    const auto malformed = benchmark_path("../coarse_malformed_lemma.smt2");
    ScopedEnvironmentVariable lemmas("ARITHEXE_COARSE_LEMMA_FILE", malformed.c_str());
    Engine engine(benchmark_path("path_expression/true_coarse_cubic_retry.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_lemma_query_count(), 0u);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST_F(CoarsePathFallback, ProvesFermatTwoAfterRejectingMixedSelectMaps) {
    Engine engine(benchmark_path("path_expression/true_coarse_fermat2.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    // Ensure the frontend really exercises the correlated-select profile.
    unsigned select_count = 0;
    for (auto& block : *engine.get_module()->getFunction("main")) {
        for (auto& instruction : block) {
            if (llvm::isa<llvm::SelectInst>(instruction)) ++select_count;
        }
    }
    ASSERT_GE(select_count, 3u);
    {
        auto& session = engine.get_session();
        auto activation = session.activate();
        auto& context = session.analyses().get_z3ctx();
        auto* function = engine.get_module()->getFunction("main");
        auto& loops = session.analyses().get_LI(function);
        ASSERT_EQ(std::distance(loops.begin(), loops.end()), 1);
        auto* loop = *loops.begin();
        Memory memory;
        memory.push_frame(function);
        auto parent = std::make_shared<State>(session, context,
            AInstruction::create(&*loop->getHeader()->begin()), nullptr,
            memory, Expression(context.bool_val(true)), trace_ty{});
        LoopExecution execution(loop, parent);
        auto [final_states, exit_states] = execution.run();
        ASSERT_FALSE(execution.had_unknown_result());
        ASSERT_EQ(final_states.size(), 2u); // Not four mixed select maps.
        ASSERT_EQ(exit_states.size(), 1u);
        for (const auto& state : final_states) {
            EXPECT_EQ(state->status, State::RUNNING);
            z3::solver guard_solver(context);
            z3::params limits(context);
            limits.set("timeout", 3000u);
            guard_solver.set(limits);
            guard_solver.add(state->get_path_condition().as_expr());
            EXPECT_EQ(guard_solver.check(), z3::sat);
        }
        for (const auto& state : exit_states) {
            EXPECT_EQ(state->status, State::RUNNING);
            z3::solver guard_solver(context);
            z3::params limits(context);
            limits.set("timeout", 3000u);
            guard_solver.set(limits);
            guard_solver.add(state->get_path_condition().as_expr());
            EXPECT_EQ(guard_solver.check(), z3::sat);
        }
    }
    // Enumeration is a separate obligation: do not reuse its Z3 context and
    // query history for the end-to-end verification regression.
    Engine verification(benchmark_path("path_expression/true_coarse_fermat2.c"));
    verification.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(verification.verify(), HOLD);
    EXPECT_EQ(verification.get_session().coarse_degree_attempt_count(2), 1u);
    EXPECT_EQ(verification.get_session().coarse_degree_attempt_count(3), 0u);
    EXPECT_EQ(verification.get_session().coarse_summary_count(), 1u);
    EXPECT_EQ(verification.get_session().coarse_refinement_count(), 0u);
    EXPECT_EQ(verification.get_session().path_acceleration_count(), 0u);
    EXPECT_FALSE(verification.has_issue());
}

TEST_F(CoarsePathFallback, FlattensFermatOneAtTheOutermostHeader) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_coarse_fermat1.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 1u);
    EXPECT_EQ(engine.get_session().coarse_degree_attempt_count(2), 1u);
    EXPECT_EQ(engine.get_session().coarse_degree_attempt_count(3), 0u);
    EXPECT_EQ(engine.get_session().coarse_refinement_count(), 0u);
    EXPECT_EQ(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_EQ(engine.get_session().nested_path_summary_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST_F(CoarsePathFallback, FlattenedNestIncludesResetsAndZeroTrips) {
    Engine engine(benchmark_path("path_expression/true_coarse_nested_reset.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 1u);
    EXPECT_EQ(engine.get_session().coarse_refinement_count(), 0u);
}

TEST_F(CoarsePathFallback, NeverCoalescesAnOldValueLiveAcrossAnInnerLoop) {
    Engine engine(benchmark_path("path_expression/false_coarse_nested_live_old.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_EQ(engine.get_session().coarse_degree_attempt_count(2), 1u);
}

TEST_F(CoarsePathFallback, FlatteningNeverSkipsAnInnerAssertion) {
    Engine engine(benchmark_path("path_expression/false_coarse_nested_assert.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    // If no useful observable exists, rejecting synthesis is also safe.
    // Neither rejection nor an inconclusive boundary proof may hide the error.
    EXPECT_GT(engine.get_session().coarse_summary_attempt_count(), 0u);
}

TEST_F(CoarsePathFallback, FlattenedPhiEdgesAreParallelAndSwapsStayDistinct) {
    Engine engine(benchmark_path("path_expression/false_coarse_nested_swap.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_GT(engine.get_session().coarse_refinement_count(), 0u);
}

TEST_F(CoarsePathFallback, FlatteningDoesNotFreezeFreshBooleanDataUpdates) {
    Engine engine(benchmark_path("path_expression/true_coarse_nested_boolean_data.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_degree_attempt_count(2), 1u);
}

TEST_F(CoarsePathFallback, DoesNotReportAnUnvalidatedCounterexample) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable refinement("ARITHEXE_COARSE_REFINE_STEPS", "16");
    Engine engine(benchmark_path("path_expression/false_coarse_spurious.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), VERIUNKNOWN);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_GT(engine.get_session().coarse_refinement_count(), 0u);
    EXPECT_EQ(engine.get_issue_kind(), VerifierIssueKind::OverApproximation);
}

TEST_F(CoarsePathFallback, ProjectsFreshHeaderAndBodyBooleanChoices) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_coarse_nondet_exit.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
}

TEST_F(CoarsePathFallback, ProjectsFreshHeaderAndBodyUintTruthiness) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_coarse_uint_eq.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 1u);
    EXPECT_EQ(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST_F(CoarsePathFallback, ProjectsNegatedAndReversedIntegerTruthiness) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_coarse_int_negated.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
}

TEST_F(CoarsePathFallback, RejectsIntegerOraclesAlsoUsedAsData) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/false_coarse_uint_data.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_EQ(engine.get_session().path_acceleration_count(), 0u);
}

TEST(PathExpressionIntegration, ProjectsUintHeaderWithIntegerWitnessSequence) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/true_nondet_uint_counter.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 0u);
}

TEST(PathExpressionIntegration, ReconstructsAcceleratedUintSequence) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/false_nondet_uint_count.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    ASSERT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    const auto& inputs = engine.get_counterexample_inputs();
    ASSERT_GE(inputs.size(), 3u);
    for (std::size_t i = 0; i + 1 < inputs.size(); ++i)
        EXPECT_GT(std::stoull(inputs[i].value), 0u);
    EXPECT_EQ(inputs.back().value, "0");
}

TEST(PathExpressionIntegration, ReconstructsInvertedIntegerSequence) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/false_nondet_int_inverted.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    ASSERT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    const auto& inputs = engine.get_counterexample_inputs();
    ASSERT_GE(inputs.size(), 3u);
    for (std::size_t i = 0; i + 1 < inputs.size(); ++i)
        EXPECT_EQ(inputs[i].value, "0");
    EXPECT_NE(inputs.back().value, "0");
}

TEST(PathExpressionIntegration, RetainsIntegerOracleZeroTripViolation) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/false_nondet_uint_zero.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    ASSERT_EQ(engine.verify(), FAIL);
    const auto& inputs = engine.get_counterexample_inputs();
    ASSERT_EQ(inputs.size(), 1u);
    EXPECT_EQ(inputs.front().value, "0");
}

TEST_F(CoarsePathFallback, UnsupportedUpdatesContinueOrdinaryExecution) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_coarse_unsupported.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_attempt_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 0u);
}

TEST_F(CoarsePathFallback, CanBeDisabledForPreciseOnlyComparisons) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/true_coarse_unsupported.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().coarse_summary_attempt_count(), 0u);
}

TEST_F(CoarsePathFallback, DoesNotEraseFixedWidthWraparound) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_coarse_fixed_wrap.c"));
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_FALSE(engine.get_session().uses_integer_relaxation());
    EXPECT_GT(engine.get_session().coarse_summary_attempt_count(), 0u);
    // Plain LLVM add may use an unnormalized integer representative. Its
    // map is polynomial; fixed-width normalization on uses must remain.
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
}

TEST_F(CoarsePathFallback, NeverSkipsAnInternalAssertion) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/false_coarse_internal_assert.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_GT(engine.get_session().coarse_refinement_count(), 0u);
}

TEST_F(CoarsePathFallback, RejectsSolutionsValidForOnlyOneBranch) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    const std::string worker = benchmark_path("../coarse_invalid_worker.py");
    ScopedEnvironmentVariable backend("ARITHEXE_SOLVER_WORKER", worker.c_str());
    Engine engine(benchmark_path("path_expression/true_coarse_small_choices.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_attempt_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 0u);
}

TEST_F(CoarsePathFallback, WorkerTimeoutRetainsOrdinaryState) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    const std::string worker = benchmark_path("../coarse_invalid_worker.py");
    ScopedEnvironmentVariable backend("ARITHEXE_SOLVER_WORKER", worker.c_str());
    ScopedEnvironmentVariable stall("ARITHEXE_TEST_COARSE_WORKER_STALL", "1");
    ScopedEnvironmentVariable timeout("ARITHEXE_COARSE_TIMEOUT_MS", "1");
    Engine engine(benchmark_path("path_expression/true_coarse_small_choices.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_attempt_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 0u);
}

TEST_F(CoarsePathFallback, ProvesBresenhamIndependentlyOfExactFirstBreaks) {
    ScopedEnvironmentVariable force("ARITHEXE_FORCE_PATH_EXPRESSIONS", "1");
    Engine engine(benchmark_path("path_expression/true_coarse_bresenham.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 1u);
    EXPECT_EQ(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST_F(CoarsePathFallback, InconclusiveProofDoesNotPoisonPreciseAcceleration) {
    Engine engine(benchmark_path("path_expression/true_coarse_precise_retry.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_GT(engine.get_session().coarse_refinement_count(), 0u);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
    EXPECT_EQ(engine.get_session().coarse_degree_attempt_count(2), 1u);
    EXPECT_EQ(engine.get_session().coarse_degree_attempt_count(3), 1u);
}

TEST_F(CoarsePathFallback, RollsBackTheWholeApproximateWorklist) {
    Engine engine(benchmark_path("path_expression/true_coarse_transaction.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_GT(engine.get_session().coarse_refinement_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST_F(CoarsePathFallback, SuccessfulProofDoesNotDropExternalSiblings) {
    Engine engine(benchmark_path("path_expression/false_coarse_external_sibling.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_refinement_count(), 0u);
}

TEST_F(CoarsePathFallback, ValidatesFiniteCounterexamplesByExactReplay) {
    Engine engine(benchmark_path("path_expression/false_coarse_finite.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_GT(engine.get_session().coarse_refinement_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST_F(CoarsePathFallback, ProofBudgetFailureRetainsExactEntry) {
    ScopedEnvironmentVariable budget("ARITHEXE_COARSE_PROOF_STEPS", "1");
    Engine engine(benchmark_path("path_expression/true_coarse_small_choices.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_GT(engine.get_session().coarse_refinement_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
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
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
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
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
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
    // No summary is involved: even this session's bypass retains C wrapping.
    EXPECT_EQ(relaxed.verify(), HOLD);
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
    // This regression specifically tests the exact periodic accelerator.
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/true_sumt2.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
}

TEST(OrdinaryCSemantics, SummaryBypassRetainsMachineArithmetic) {
    Engine engine(benchmark_path("loop_free/true_ordinary_width_semantics.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_summary_count(), 0u);
}

TEST(OrdinaryCSemantics, DetectsUnsignedFailuresWithoutAcceleration) {
    for (const char* file : {"false_ordinary_char_wrap.c",
                             "false_ordinary_unsigned_widen.c",
                             "false_ordinary_unsigned_double.c"}) {
        SCOPED_TRACE(file);
        Engine engine(benchmark_path(std::string("loop_free/") + file));
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_EQ(engine.verify(), FAIL);
        EXPECT_FALSE(engine.has_issue());
        EXPECT_EQ(engine.get_session().path_acceleration_count(), 0u);
        EXPECT_EQ(engine.get_session().coarse_summary_count(), 0u);
    }
}

TEST(NestedPathAcceleration, AcceleratesFiveLevelsAndLargeOuterCount) {
    Engine engine(benchmark_path("path_expression/true_nested5_1.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_EQ(engine.get_session().nested_path_summary_count(), 5u);
    EXPECT_EQ(engine.get_session().path_acceleration_count(), 5u);
}

TEST(NestedPathAcceleration, ComposesGuardedSymbolicChildExits) {
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    for (const auto* file : {"true_nested_symbolic_bounds.c", "true_nested_guarded_exits.c"}) {
        SCOPED_TRACE(file);
        Engine engine(benchmark_path(std::string("path_expression/") + file));
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_EQ(engine.verify(), HOLD);
        EXPECT_EQ(engine.get_session().nested_path_summary_count(), 2u);
        EXPECT_GT(engine.get_session().path_acceleration_count(), 0u);
        EXPECT_FALSE(engine.has_issue());
        auto contains_ite = [](auto&& self, const z3::expr& expression) -> bool {
            if (!expression.is_app()) return false;
            if (expression.decl().decl_kind() == Z3_OP_ITE) return true;
            for (const auto& argument : expression.args())
                if (self(self, argument)) return true;
            return false;
        };
        for (const auto& [loop, summary] : engine.get_session().nested_path_summaries()) {
            (void)loop;
            ASSERT_FALSE(summary->cases.empty());
            for (const auto& branch : summary->cases) {
                EXPECT_FALSE(contains_ite(contains_ite, branch->count));
                for (const auto& value : branch->values)
                    EXPECT_FALSE(contains_ite(contains_ite, value));
            }
        }
    }
}

TEST(NestedPathAcceleration, GuardedZeroTripExitRetainsCounterexample) {
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
    Engine engine(benchmark_path("path_expression/false_nested_guarded_zero.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_EQ(engine.get_session().nested_path_summary_count(), 2u);
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
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
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
    EXPECT_FALSE(engine.has_issue());
    const auto& inputs = engine.get_counterexample_inputs();
    ASSERT_GE(inputs.size(), 7u);
    EXPECT_EQ(inputs[4].value, "1"); // flag is true in the genuine violation.
    EXPECT_EQ(inputs[5].value, "1"); // At least one iteration executes.
    EXPECT_EQ(inputs.back().value, "0");
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
    ScopedEnvironmentVariable coarse("ARITHEXE_COARSE_FALLBACK", "0");
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

TEST(Z3UnknownPropagationRegression, SafeAssertionDischargesUncertainBranch) {
    auto run = run_benchmark("loops/true_z3_unknown_branch.c");
    EXPECT_EQ(run.result, HOLD);
    EXPECT_FALSE(run.has_issue);
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
