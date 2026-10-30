#include <gtest/gtest.h>
#include <cstdlib>
#include "engine.h"
#include "UnobservedGlobals.h"
#include "llvm/IR/Verifier.h"

using namespace ari_exe;

namespace {
class ScopedEnvironment {
    const char* name;
    bool had_previous;
    std::string previous;
public:
    ScopedEnvironment(const char* name, const char* value) : name(name) {
        const char* old = std::getenv(name);
        had_previous = old != nullptr;
        if (old) previous = old;
        setenv(name, value, 1);
    }
    ~ScopedEnvironment() {
        if (had_previous) setenv(name, previous.c_str(), 1);
        else unsetenv(name);
    }
};

class UnobservedGlobalsTest : public ::testing::Test {
protected:
    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> module;

    void load(const std::string& ir) {
        module = AnalysisManager::parseLLVMIR(ir, context);
        ASSERT_FALSE(llvm::verifyModule(*module, &llvm::errs()));
    }
    std::size_t clean() {
        const auto result = erase_unobserved_global_stores(*module);
        EXPECT_FALSE(llvm::verifyModule(*module, &llvm::errs()));
        return result;
    }
};
std::string source(const char* name) {
    return std::string(ARITHEXE_TEST_BENCHMARK_DIR) + "/unobserved_globals/" + name;
}
std::size_t stores_to(Engine& engine, const char* name) {
    const auto* global = engine.get_module()->getGlobalVariable(name);
    if (!global) return 0;
    std::size_t count = 0;
    for (const auto* user : global->users())
        if (llvm::isa<llvm::StoreInst>(user)) ++count;
    return count;
}
} // namespace

TEST_F(UnobservedGlobalsTest, RemovesOnlyUnobservedDirectScalarStores) {
    load(R"(
        @spill = global i32 0
        @observed = global i32 0
        define void @helper() {
            store i32 5, ptr @spill
            ret void
        }
        define i32 @main() {
            store i32 4, ptr @spill
            store i32 7, ptr @observed
            call void @helper()
            %x = load i32, ptr @observed
            ret i32 %x
        }
    )");
    EXPECT_EQ(clean(), 2u);
    EXPECT_TRUE(module->getGlobalVariable("spill")->use_empty());
    EXPECT_FALSE(module->getGlobalVariable("observed")->use_empty());
}

TEST_F(UnobservedGlobalsTest, PreservesVolatileAtomicAndNonScalarStorage) {
    load(R"(
        @v = global i32 0
        @a = global i32 0
        @t = thread_local global i32 0
        @f = global float 0.0
        define i32 @main() {
            store volatile i32 1, ptr @v
            store atomic i32 1, ptr @a monotonic, align 4
            store i32 1, ptr @t
            store float 1.0, ptr @f
            ret i32 0
        }
    )");
    EXPECT_EQ(clean(), 0u);
}

TEST_F(UnobservedGlobalsTest, PreservesEscapedAliasedAndDerivedAddresses) {
    load(R"(
        @escaped = global i32 0
        @saved = global ptr @escaped
        @aliased = global i32 0
        @alias = alias i32, ptr @aliased
        @derived = global i32 0
        @integer_address = global i32 0
        define i32 @main() {
            store i32 1, ptr @escaped
            store i32 1, ptr @aliased
            %p = getelementptr i32, ptr @derived, i32 0
            store i32 1, ptr %p
            store i32 1, ptr @integer_address
            %address = ptrtoint ptr @integer_address to i64
            ret i32 0
        }
    )");
    EXPECT_EQ(clean(), 0u);
}

TEST_F(UnobservedGlobalsTest, UnknownExternalObserversBlockCleanupEvenWithAttributes) {
    load(R"(
        @spill = global i32 0
        declare void @external_observer() nounwind memory(none)
        define i32 @main() {
            store i32 1, ptr @spill
            call void @external_observer()
            ret i32 0
        }
    )");
    EXPECT_EQ(clean(), 0u);
}

TEST_F(UnobservedGlobalsTest, IndirectAndInlineAssemblyObserversBlockCleanup) {
    load(R"(
        @spill = global i32 0
        define void @indirect(ptr %f) {
            call void %f()
            ret void
        }
        define i32 @main() {
            store i32 1, ptr @spill
            ret i32 0
        }
    )");
    EXPECT_EQ(clean(), 0u);
    load(R"(
        @spill = global i32 0
        define i32 @main() {
            store i32 1, ptr @spill
            call void asm sideeffect "", ""()
            ret i32 0
        }
    )");
    EXPECT_EQ(clean(), 0u);
    load(R"(
        module asm "# opaque observer"
        @spill = global i32 0
        define i32 @main() { store i32 1, ptr @spill
            ret i32 0 }
    )");
    EXPECT_EQ(clean(), 0u);
}

TEST_F(UnobservedGlobalsTest, ArbitraryPointersAndExternalObjectsBlockCleanup) {
    for (const char* observer : {
            "define i32 @observer(ptr %p) { %x = load i32, ptr %p\n ret i32 %x }",
            "define i32 @observer() { %p = inttoptr i64 123 to ptr\n %x = load i32, ptr %p\n ret i32 %x }",
            "@external_object = external global i32\n define i32 @observer() { %x = load i32, ptr @external_object\n ret i32 %x }"}) {
        load(std::string("@spill = global i32 0\n") + observer +
             "\ndefine i32 @main() { store i32 1, ptr @spill\n ret i32 0 }");
        EXPECT_EQ(clean(), 0u);
    }
}

TEST_F(UnobservedGlobalsTest, ReadsInDefinedCalleesRemainObservable) {
    load(R"(
        @spill = global i32 0
        define i32 @helper() {
            %x = load i32, ptr @spill
            ret i32 %x
        }
        define i32 @main() {
            store i32 1, ptr @spill
            %x = call i32 @helper()
            ret i32 %x
        }
    )");
    EXPECT_EQ(clean(), 0u);
}

TEST_F(UnobservedGlobalsTest, DoesNotTrustReplaceableOracleDefinitionsOrGlobals) {
    load(R"(
        @spill = global i32 0
        define weak i32 @__VERIFIER_nondet_int() { ret i32 0 }
        define i32 @main() {
            store i32 1, ptr @spill
            %x = call i32 @__VERIFIER_nondet_int()
            ret i32 %x
        }
    )");
    EXPECT_EQ(clean(), 0u);
    load(R"(
        @weak_spill = weak global i32 0
        @runtime_spill = externally_initialized global i32 0
        define i32 @main() {
            store i32 1, ptr @weak_spill
            store i32 1, ptr @runtime_spill
            ret i32 0
        }
    )");
    EXPECT_EQ(clean(), 0u);
}

TEST_F(UnobservedGlobalsTest, RetainsOracleCallsAndLiveNumericalUses) {
    load(R"(
        @spill = global i32 0
        declare i32 @__VERIFIER_nondet_int()
        define i32 @main() {
            %x = call i32 @__VERIFIER_nondet_int()
            store i32 %x, ptr @spill
            %y = add i32 %x, 1
            ret i32 %y
        }
    )");
    EXPECT_EQ(clean(), 1u);
    const auto* call = &module->getFunction("main")->front().front();
    EXPECT_TRUE(llvm::isa<llvm::CallInst>(call));
    EXPECT_TRUE(call->hasOneUse());
    EXPECT_TRUE(llvm::isa<llvm::BinaryOperator>(call->getNextNode()));
}

TEST_F(UnobservedGlobalsTest, HiddenRuntimeObserversBlockCleanup) {
    load(R"(
        @spill = global i32 0
        declare i32 @personality(...)
        define i32 @main() personality ptr @personality {
            store i32 1, ptr @spill
            ret i32 0
        }
    )");
    EXPECT_EQ(clean(), 0u);
    load(R"(
        @spill = global i32 0
        define i32 @main() gc "shadow-stack" {
            store i32 1, ptr @spill
            ret i32 0
        }
    )");
    EXPECT_EQ(clean(), 0u);
}

TEST_F(UnobservedGlobalsTest, FrontendPassCanBeDisabledForAblations) {
    load(R"(
        @spill = global i32 0
        define i32 @main() { store i32 1, ptr @spill
            ret i32 0 }
    )");
    llvm::ModuleAnalysisManager analyses;
    ScopedEnvironment disabled("ARITHEXE_UNOBSERVED_GLOBAL_CLEANUP", "0");
    EXPECT_TRUE(UnobservedGlobalStorePass().run(*module, analyses).areAllPreserved());
    EXPECT_FALSE(module->getGlobalVariable("spill")->use_empty());
}

TEST(UnobservedGlobalsIntegration, AdmitsFreshBodyControlAfterDeadSpillCleanup) {
    Engine engine(source("true_control_spill.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(stores_to(engine, "spill"), 0u);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(UnobservedGlobalsIntegration, CoarseSummaryHandlesGsvShapedSpillLoop) {
    Engine engine(source("true_gsv_spill.c"));
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(stores_to(engine, "spill"), 0u);
    EXPECT_EQ(engine.verify(), HOLD);
    EXPECT_GT(engine.get_session().coarse_summary_count(), 0u);
    EXPECT_FALSE(engine.has_issue());
}

TEST(UnobservedGlobalsIntegration, KeepsObservedGlobalsAndNondeterministicData) {
    for (const char* name : {"false_observed_spill.c", "false_live_oracle.c"}) {
        SCOPED_TRACE(name);
        Engine engine(source(name));
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_EQ(engine.verify(), FAIL);
        EXPECT_FALSE(engine.has_issue());
    }
}
