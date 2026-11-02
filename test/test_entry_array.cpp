#include <gtest/gtest.h>
#include "EntryArray.h"

using namespace ari_exe;

namespace {
bool classify(const std::string& body, bool aliases = false, bool integer = true,
              const std::string& increment = "1") {
    VerificationSession session;
    auto activation = session.activate();
    session.set_ignore_bitwidth_constraints(integer);
    auto& context = session.analyses().get_z3ctx();
    llvm::LLVMContext llvm_context;
    const std::string ir = "declare void @clobber(ptr)\n"
        "define void @accesses(ptr %a, ptr %b, i64 %N) {\n"
        "entry: br label %loop\nloop:\n"
        "%i = phi i64 [0,%entry],[%inc,%loop]\n" + body +
        "\n%inc = add i64 %i," + increment + "\n"
        "%test = icmp slt i64 %inc,%N\n"
        "br i1 %test,label %loop,label %exit\nexit: ret void\n}";
    auto module = AnalysisManager::parseLLVMIR(ir, llvm_context);
    if (!module) throw std::runtime_error("test IR failed to parse");
    module->setDataLayout("e-p:64:64-i64:64-n8:16:32:64");
    auto* function = module->getFunction("accesses");
    llvm::DominatorTree dominators(*function);
    llvm::LoopInfo loops(dominators);
    auto* loop = *loops.begin();
    llvm::LoadInst* read = nullptr;
    for (auto& instruction : llvm::instructions(function))
        if (instruction.getName() == "read") read = llvm::cast<llvm::LoadInst>(&instruction);
    Memory memory;
    memory.push_frame(function);
    z3::expr_vector dims(context);
    dims.push_back(context.int_val(64));
    memory.allocate(function->getArg(0), dims);
    if (aliases) {
        auto address = memory.get_object(function->getArg(0))->get_ptr_value();
        memory.put_temp(function->getArg(1), address);
    } else {
        memory.allocate(function->getArg(1), dims);
    }
    auto entry = std::make_shared<State>(session, context, nullptr, nullptr,
                                        memory, Expression(context.bool_val(true)), trace_ty{});
    return reads_entry_array(loop, read, entry);
}
}

TEST(PolyhedralEntryReads, ReadBeforeSameElementWriteUsesEntry) {
    EXPECT_TRUE(classify("%p = getelementptr i32,ptr %a,i64 %i\n"
                         "%read = load i32,ptr %p\nstore i32 0,ptr %p"));
}

TEST(PolyhedralEntryReads, SameIterationWriteBeforeReadIsNotEntry) {
    EXPECT_FALSE(classify("%p = getelementptr i32,ptr %a,i64 %i\n"
                          "store i32 0,ptr %p\n%read = load i32,ptr %p"));
}

TEST(PolyhedralEntryReads, PreviousIterationWriteIsNotEntry) {
    EXPECT_FALSE(classify("%prev = sub i64 %i,1\n"
        "%p = getelementptr i32,ptr %a,i64 %prev\n%read = load i32,ptr %p\n"
        "%q = getelementptr i32,ptr %a,i64 %i\nstore i32 0,ptr %q"));
}

TEST(PolyhedralEntryReads, RepeatedConstantElementIsNotEntry) {
    EXPECT_FALSE(classify("%read = load i32,ptr %a\nstore i32 0,ptr %a"));
}

TEST(PolyhedralEntryReads, StridedDisjointElementsUseEntry) {
    EXPECT_TRUE(classify("%even = mul i64 %i,2\n%odd = add i64 %even,1\n"
        "%p = getelementptr i32,ptr %a,i64 %even\n%read = load i32,ptr %p\n"
        "%q = getelementptr i32,ptr %a,i64 %odd\nstore i32 0,ptr %q"));
}

TEST(PolyhedralEntryReads, DescendingInjectiveAccessUsesEntry) {
    EXPECT_TRUE(classify("%p = getelementptr i32,ptr %a,i64 %i\n"
                         "%read = load i32,ptr %p\nstore i32 0,ptr %p", false, true, "-1"));
}

TEST(PolyhedralEntryReads, ByteOverlapIsNotMistakenForDistinctElements) {
    EXPECT_FALSE(classify("%p = getelementptr i8,ptr %a,i64 %i\n"
        "%read = load i32,ptr %p\n%next = add i64 %i,1\n"
        "%q = getelementptr i8,ptr %a,i64 %next\nstore i64 0,ptr %q"));
}

TEST(PolyhedralEntryReads, UnknownMemoryEffectsPreventSnapshot) {
    EXPECT_FALSE(classify("call void @clobber(ptr %a)\n%read = load i32,ptr %a"));
}

TEST(PolyhedralEntryReads, ReadOnlyNonAffineAccessUsesEntry) {
    EXPECT_TRUE(classify("%square = mul i64 %i,%i\n"
        "%p = getelementptr i32,ptr %a,i64 %square\n%read = load i32,ptr %p"));
}

TEST(PolyhedralEntryReads, NonAffineAliasingWriteDoesNotProduceCertificate) {
    EXPECT_FALSE(classify("%square = mul i64 %i,%i\n"
        "%p = getelementptr i32,ptr %a,i64 %square\n"
        "store i32 0,ptr %p\n%read = load i32,ptr %p"));
}

TEST(PolyhedralEntryReads, ParametricStrideWithDisjointWritesUsesEntry) {
    const std::string body="%index = mul i64 %i,%N\n"
        "%p = getelementptr i32,ptr %a,i64 %index\n%read = load i32,ptr %p\n"
        "%q = getelementptr i32,ptr %b,i64 %i\nstore i32 0,ptr %q";
    EXPECT_TRUE(classify(body));
    EXPECT_FALSE(classify(body,true));
}

TEST(PolyhedralEntryReads, UnresolvedAliasedRootsPreventSnapshot) {
    const std::string accesses = "%p = getelementptr i32,ptr %a,i64 %i\n"
        "%read = load i32,ptr %p\n%q = getelementptr i32,ptr %b,i64 %i\nstore i32 0,ptr %q";
    EXPECT_TRUE(classify(accesses));
    EXPECT_FALSE(classify(accesses, true));
}

TEST(PolyhedralEntryReads, IntegerProofDoesNotLeakIntoFixedWidthMode) {
    EXPECT_FALSE(classify("%p = getelementptr i32,ptr %a,i64 %i\n"
                          "%read = load i32,ptr %p\nstore i32 0,ptr %p", false, false));
}
