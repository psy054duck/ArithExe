#include <gtest/gtest.h>
#include <llvm/IR/IRBuilder.h>
#include "AInstruction.h"
#include "engine.h"

using namespace ari_exe;

namespace {
class StackArrayAllocation : public ::testing::Test {
  protected:
    VerificationSession session;
    VerificationSession::Activation activation = session.activate();
    llvm::LLVMContext llvm_context;
    llvm::Module module{"stack-array-tests", llvm_context};
    z3::context& ctx = session.analyses().get_z3ctx();
    llvm::AllocaInst* allocation = nullptr;

    void SetUp() override { session.set_ignore_bitwidth_constraints(true); }
    void TearDown() override { session.clear_module_state(); }

    state_ptr allocate(llvm::Type* type, const Expression& count,
                       unsigned width = 64, bool constant_one = false) {
        auto* count_type = llvm::IntegerType::get(llvm_context, width);
        auto* signature = llvm::FunctionType::get(llvm::Type::getVoidTy(llvm_context),
                                                  {count_type}, false);
        auto* function = llvm::Function::Create(signature, llvm::Function::ExternalLinkage,
                                                "allocate", module);
        auto* block = llvm::BasicBlock::Create(llvm_context, "entry", function);
        llvm::IRBuilder<> builder(block);
        llvm::Value* operand = function->getArg(0);
        if (constant_one) operand = llvm::ConstantInt::get(count_type, 1);
        allocation = builder.CreateAlloca(type, operand, "array");
        builder.CreateRetVoid();
        Memory memory;
        memory.push_frame(function);
        memory.put_temp(function->getArg(0), count);
        auto* instruction = AInstruction::create(allocation);
        auto state = std::make_shared<State>(session, ctx, instruction, nullptr,
                                            memory, Expression(ctx.bool_val(true)), trace_ty{});
        auto successors = instruction->execute(state);
        if (successors.size() != 1) throw std::runtime_error("expected one allocation successor");
        return successors.front();
    }

    MemoryObjectPtr object(const state_ptr& state) {
        return state->memory.get_object_pointed_by(allocation);
    }
};
}

TEST_F(StackArrayAllocation, RetainsSymbolicExtentAndSupportsIndexedReadsAndWrites) {
    const auto n = ctx.int_const("stack_n");
    auto state = allocate(llvm::Type::getInt32Ty(llvm_context), Expression(n));
    auto* array = object(state);
    ASSERT_EQ(array->get_sizes().size(), 1u);
    EXPECT_TRUE(z3::eq(array->get_sizes()[0].as_expr(), n));
    EXPECT_EQ(array->get_indices().size(), 1u);
    array->write({Expression(n-1)}, Expression(ctx.int_val(7)));
    z3::solver solver(ctx);
    solver.add(n > 0 && array->read({Expression(n-1)}).as_expr() != 7);
    EXPECT_EQ(solver.check(), z3::unsat);
}

TEST_F(StackArrayAllocation, ResolvesCallerAllocationInsideHelperFrame) {
    auto state = allocate(llvm::Type::getInt32Ty(llvm_context), Expression(ctx.int_val(8)));
    const auto address = object(state)->get_addr();
    state->memory.push_frame();
    auto* array = state->memory.get_object_pointed_by(allocation);
    ASSERT_NE(array, nullptr);
    EXPECT_EQ(array, state->memory.get_object(address));
    array->write({Expression(ctx.int_val(3))}, Expression(ctx.int_val(19)));
    EXPECT_TRUE(z3::eq(state->memory.get_object(address)->read(
        {Expression(ctx.int_val(3))}).as_expr().simplify(), ctx.int_val(19)));
}

TEST_F(StackArrayAllocation, HelperAliasesPreserveAddressesAndOffsetsAfterCopy) {
    auto state = allocate(llvm::Type::getInt32Ty(llvm_context), Expression(ctx.int_val(8)));
    auto address = object(state)->get_addr();
    address.offset.emplace_back(ctx.int_val(2));
    auto* signature = llvm::FunctionType::get(llvm::Type::getVoidTy(llvm_context),
        {llvm::PointerType::getUnqual(llvm_context),
         llvm::PointerType::getUnqual(llvm_context)}, false);
    auto* helper = llvm::Function::Create(signature, llvm::Function::ExternalLinkage,
                                         "helper", module);
    state->memory.push_frame(helper);
    state->memory.put_temp(helper->getArg(0), address);
    state->memory.put_temp(helper->getArg(1), address);
    Memory copy(state->memory);
    auto* first = copy.get_object_pointed_by(helper->getArg(0));
    auto* second = copy.get_object_pointed_by(helper->getArg(1));
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_EQ(first, copy.get_object_pointed_by(allocation));
    auto target = copy.get_object(helper->getArg(1))->get_ptr_value();
    ASSERT_EQ(target.offset.size(), 1u);
    EXPECT_TRUE(z3::eq(target.offset[0].as_expr(), ctx.int_val(2)));
    copy.store(target, Expression(ctx.int_val(23)));
    EXPECT_TRUE(z3::eq(first->read({Expression(ctx.int_val(2))}).as_expr().simplify(),
                       ctx.int_val(23)));
    EXPECT_NE(first, state->memory.get_object_pointed_by(allocation));
}

TEST_F(StackArrayAllocation, RejectsNegativeIntegerCountsWithoutRejectingZero) {
    const auto n = ctx.int_const("stack_nonnegative_n");
    auto state = allocate(llvm::Type::getInt32Ty(llvm_context), Expression(n));
    z3::solver solver(ctx);
    solver.add(state->get_path_condition().as_expr() && n < 0);
    EXPECT_EQ(solver.check(), z3::unsat);
    solver.reset();
    solver.add(state->get_path_condition().as_expr() && n == 0);
    EXPECT_EQ(solver.check(), z3::sat);
}

TEST_F(StackArrayAllocation, PreservesAllocationCountDefinedness) {
    const auto n = ctx.int_const("stack_defined_n");
    const auto defined = ctx.bool_const("stack_count_defined");
    z3::expr_vector conditions(ctx), values(ctx), definitions(ctx);
    conditions.push_back(ctx.bool_val(true));
    values.push_back(n);
    definitions.push_back(defined);
    auto state = allocate(llvm::Type::getInt32Ty(llvm_context),
                          Expression(conditions, values, definitions));
    z3::solver solver(ctx);
    solver.add(state->get_path_condition().as_expr() && !defined);
    EXPECT_EQ(solver.check(), z3::unsat);
}

TEST_F(StackArrayAllocation, FixedWidthCountsAreUnsigned) {
    session.set_ignore_bitwidth_constraints(false);
    auto state = allocate(llvm::Type::getInt32Ty(llvm_context), Expression(ctx.int_val(-1)), 8);
    EXPECT_TRUE(z3::eq(object(state)->get_sizes()[0].as_expr().simplify(), ctx.int_val(255)));
}

TEST_F(StackArrayAllocation, PreservesFixedMultidimensionalArrayShape) {
    auto* row = llvm::ArrayType::get(llvm::Type::getInt32Ty(llvm_context), 3);
    auto* matrix = llvm::ArrayType::get(row, 2);
    auto state = allocate(matrix, Expression(ctx.int_val(1)), 64, true);
    const auto sizes = object(state)->get_sizes();
    ASSERT_EQ(sizes.size(), 2u);
    EXPECT_TRUE(z3::eq(sizes[0].as_expr(), ctx.int_val(2)));
    EXPECT_TRUE(z3::eq(sizes[1].as_expr(), ctx.int_val(3)));
}

TEST_F(StackArrayAllocation, PreservesDynamicCountOfFixedArrayElements) {
    const auto n = ctx.int_const("stack_rows");
    auto* row = llvm::ArrayType::get(llvm::Type::getInt32Ty(llvm_context), 3);
    auto state = allocate(row, Expression(n));
    const auto sizes = object(state)->get_sizes();
    ASSERT_EQ(sizes.size(), 2u);
    EXPECT_TRUE(z3::eq(sizes[0].as_expr(), n));
    EXPECT_TRUE(z3::eq(sizes[1].as_expr(), ctx.int_val(3)));
}

TEST(StackArrayIntegration, ProvesSymbolicExtentInitialization) {
    Engine engine(std::string(ARITHEXE_TEST_BENCHMARK_DIR)+"/arrays/true_vla_summary.c");
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), HOLD) << engine.get_issue_message();
    EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
}

TEST(StackArrayIntegration, DetectsWrongSymbolicExtentAssertion) {
    Engine engine(std::string(ARITHEXE_TEST_BENCHMARK_DIR)+"/arrays/false_vla_summary.c");
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(), FAIL) << engine.get_issue_message();
    EXPECT_FALSE(engine.has_issue()) << engine.get_issue_message();
}
