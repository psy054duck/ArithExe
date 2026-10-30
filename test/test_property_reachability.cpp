#include <gtest/gtest.h>
#include <iterator>
#include "engine.h"
#include "PropertyReachability.h"
#include "llvm/IR/Verifier.h"

using namespace ari_exe;

namespace {
class PropertyReachabilityTest : public ::testing::Test {
protected:
    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> module;
    PropertyReachability index;

    void load(const std::string& ir) {
        module = AnalysisManager::parseLLVMIR(ir, context);
        ASSERT_NE(module, nullptr);
        ASSERT_FALSE(llvm::verifyModule(*module, &llvm::errs()));
        index.rebuild(*module);
    }
    const llvm::Instruction* entry(const char* name) {
        return &module->getFunction(name)->getEntryBlock().front();
    }
};

std::string cases() {
    return std::string(ARITHEXE_TEST_BENCHMARK_DIR) + "/property_pruning/cases.c";
}

void select(Engine& engine, const char* name) {
    auto* function = engine.get_module()->getFunction(name);
    ASSERT_NE(function, nullptr);
    engine.set_entry(function);
}
} // namespace

TEST_F(PropertyReachabilityTest, FollowsOnlyTheTakenLiteralBranch) {
    load(R"(
        declare void @reach_error()
        define i32 @main() {
        entry:
            br i1 false, label %error, label %safe
        error:
            call void @reach_error()
            ret i32 0
        safe:
            br label %safe
        }
    )");
    EXPECT_FALSE(index.may_reach_obligation(entry("main")));
    EXPECT_TRUE(index.may_reach_obligation(&std::next(module->getFunction("main")->begin())->front()));
}

TEST_F(PropertyReachabilityTest, RetainsBothUnknownBranchSuccessors) {
    load(R"(
        declare void @reach_error()
        define i32 @main(i1 %choice) {
            br i1 %choice, label %error, label %safe
        error:
            call void @reach_error()
            ret i32 0
        safe:
            ret i32 0
        }
    )");
    EXPECT_TRUE(index.may_reach_obligation(entry("main")));
    EXPECT_FALSE(index.may_reach_obligation(&module->getFunction("main")->back().front()));
}

TEST_F(PropertyReachabilityTest, PropagatesThroughMutuallyRecursiveCallees) {
    load(R"(
        declare void @reach_error()
        define void @first(i1 %choice) {
            call void @second(i1 %choice)
            ret void
        }
        define void @second(i1 %choice) {
            br i1 %choice, label %again, label %error
        again:
            call void @first(i1 %choice)
            ret void
        error:
            call void @reach_error()
            ret void
        }
        define i32 @main(i1 %choice) {
            call void @first(i1 %choice)
            ret i32 0
        }
    )");
    EXPECT_TRUE(index.may_reach_obligation(entry("first")));
    EXPECT_TRUE(index.may_reach_obligation(entry("second")));
    EXPECT_TRUE(index.may_reach_obligation(entry("main")));
}

TEST_F(PropertyReachabilityTest, DoesNotInventEdgesFromDeadCalleeBlocks) {
    load(R"(
        declare void @reach_error()
        define void @helper() {
            br i1 true, label %safe, label %error
        safe:
            ret void
        error:
            call void @reach_error()
            ret void
        }
        define i32 @main() {
            call void @helper()
            ret i32 0
        }
    )");
    EXPECT_FALSE(index.may_reach_obligation(entry("helper")));
    EXPECT_FALSE(index.may_reach_obligation(entry("main")));
}

TEST_F(PropertyReachabilityTest, RetainsUnknownIndirectAndReplaceableCalls) {
    load(R"(
        declare i32 @unknown() nounwind memory(none)
        define weak i32 @__VERIFIER_nondet_int() { ret i32 0 }
        define i32 @external() {
            %x = call i32 @unknown()
            ret i32 %x
        }
        define i32 @indirect(ptr %fp) {
            %x = call i32 %fp()
            ret i32 %x
        }
        define i32 @replaceable() {
            %x = call i32 @__VERIFIER_nondet_int()
            ret i32 %x
        }
    )");
    for (const char* name : {"external", "indirect", "replaceable"})
        EXPECT_TRUE(index.may_reach_obligation(entry(name))) << name;
}

TEST_F(PropertyReachabilityTest, OnlyExplicitOracleDeclarationsAreTrusted) {
    load(R"(
        declare i32 @__VERIFIER_nondet_int()
        declare void @abort()
        declare void @__VERIFIER_assume(i32)
        declare i32 @__VERIFIER_nondet_custom()
        define i32 @safe() {
            %x = call i32 @__VERIFIER_nondet_int()
            call void @__VERIFIER_assume(i32 %x)
            call void @abort()
            ret i32 0
        }
        define i32 @uncontracted() {
            %x = call i32 @__VERIFIER_nondet_custom()
            ret i32 %x
        }
    )");
    EXPECT_FALSE(index.may_reach_obligation(entry("safe")));
    EXPECT_TRUE(index.may_reach_obligation(entry("uncontracted")));
}

TEST_F(PropertyReachabilityTest, IsInstructionSensitiveAfterAssertions) {
    load(R"(
        declare void @__VERIFIER_assert(i32)
        declare void @__assert_fail(ptr, ptr, i32, ptr)
        declare void @__assert_rtn(ptr, ptr, i32, ptr)
        define i32 @main() {
            call void @__VERIFIER_assert(i32 1)
            ret i32 0
        }
        define void @c_assertion() {
            call void @__assert_fail(ptr null, ptr null, i32 0, ptr null)
            ret void
        }
        define void @mac_assertion() {
            call void @__assert_rtn(ptr null, ptr null, i32 0, ptr null)
            ret void
        }
    )");
    EXPECT_TRUE(index.may_reach_obligation(entry("main")));
    EXPECT_FALSE(index.may_reach_obligation(entry("main")->getNextNode()));
    EXPECT_TRUE(index.may_reach_obligation(entry("c_assertion")));
    EXPECT_TRUE(index.may_reach_obligation(entry("mac_assertion")));
}

TEST_F(PropertyReachabilityTest, MissingOrForeignInstructionsStayRelevant) {
    load("define i32 @main() { ret i32 0 }");
    EXPECT_TRUE(index.may_reach_obligation(nullptr));
    llvm::LLVMContext other_context;
    auto other = AnalysisManager::parseLLVMIR("define i32 @main() { ret i32 0 }", other_context);
    ASSERT_NE(other, nullptr);
    EXPECT_TRUE(index.may_reach_obligation(&other->getFunction("main")->front().front()));
    PropertyReachability uninitialized;
    EXPECT_TRUE(uninitialized.may_reach_obligation(entry("main")));
}

TEST(PropertyPruningIntegration, SkipsAnErrorFreeInfiniteSuffix) {
    Engine engine(cases());
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.property_pruned_state_count(), 0u);
    EXPECT_EQ(engine.get_session().path_acceleration_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_summary_attempt_count(), 0u);
}

TEST(PropertyPruningIntegration, ChecksAssertionBeforeDiscardingSuffix) {
    Engine engine(cases());
    select(engine, "safe_suffix");
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.property_pruned_state_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_summary_attempt_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PropertyPruningIntegration, NeverHidesPendingAssertionOrError) {
    for (const char* name : {"unsafe_pending_assert", "unsafe_pending_error"}) {
        SCOPED_TRACE(name);
        Engine engine(cases());
        select(engine, name);
        EXPECT_EQ(engine.verify(), FAIL);
        EXPECT_NE(engine.get_violation_instruction(), nullptr);
    }
}

TEST(PropertyPruningIntegration, KeepsUnsafeWorklistSiblings) {
    Engine engine(cases());
    select(engine, "unsafe_sibling");
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_GT(engine.property_pruned_state_count(), 0u);
}

TEST(PropertyPruningIntegration, ChecksEveryActiveCallerContinuation) {
    for (const char* name : {"unsafe_caller", "unsafe_outer_caller"}) {
        SCOPED_TRACE(name);
        Engine engine(cases());
        select(engine, name);
        EXPECT_EQ(engine.verify(), FAIL);
        EXPECT_FALSE(engine.has_issue());
        EXPECT_EQ(engine.property_pruned_state_count(), 0u);
    }
}

TEST(PropertyPruningIntegration, RetainsErrorsInDefinedCallees) {
    Engine engine(cases());
    select(engine, "unsafe_callee");
    EXPECT_EQ(engine.verify(), FAIL);
    EXPECT_FALSE(engine.has_issue());
}

TEST(PropertyPruningIntegration, PrunesInsideCalleeOnlyWithSafeCaller) {
    Engine engine(cases());
    select(engine, "safe_callee_suffix");
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.property_pruned_state_count(), 0u);
    EXPECT_EQ(engine.get_session().coarse_summary_attempt_count(), 0u);
}

TEST(PropertyPruningIntegration, FinalFrontendRemovesContradictoryErrorEdge) {
    Engine engine(cases());
    select(engine, "dead_error");
    auto* function = engine.get_module()->getFunction("dead_error");
    ASSERT_NE(function, nullptr);
    ASSERT_FALSE(llvm::verifyModule(*engine.get_module(), &llvm::errs()));
    for (auto& block : *function) {
        for (auto& instruction : block) {
            if (auto* branch = llvm::dyn_cast<llvm::BranchInst>(&instruction))
                if (branch->isConditional())
                    EXPECT_FALSE(llvm::isa<llvm::ConstantInt>(branch->getCondition()));
            if (auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction))
                EXPECT_NE(call->getCalledFunction(), engine.get_module()->getFunction("reach_error"));
        }
    }
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.property_pruned_state_count(), 0u);
}
