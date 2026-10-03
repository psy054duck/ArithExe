#include <gtest/gtest.h>
#include <llvm/IR/IRBuilder.h>
#include "AInstruction.h"
#include "IntegerSemantics.h"
#include "RelaxedIntegerSemantics.h"

using namespace ari_exe;

namespace {
bool has_bitvector(const z3::expr& value) {
    if (value.get_sort().is_bv()) return true;
    if (value.is_app()) {
        const auto kind = value.decl().decl_kind();
        if (kind == Z3_OP_INT2BV || kind == Z3_OP_BV2INT) return true;
        for (const auto& argument : value.args()) {
            if (has_bitvector(argument)) return true;
        }
    }
    return false;
}

class RelaxedIntegerOperations : public ::testing::Test {
  protected:
    VerificationSession session;
    VerificationSession::Activation activation = session.activate();
    llvm::LLVMContext llvm_context;
    llvm::Module module{"integer-relaxation-tests", llvm_context};
    z3::context& ctx = session.analyses().get_z3ctx();

    void SetUp() override { session.set_ignore_bitwidth_constraints(true); }
    void TearDown() override { session.clear_module_state(); }

    llvm::Function* function(unsigned width, llvm::Type* result,
                             unsigned arity) {
        auto* type = llvm::IntegerType::get(llvm_context, width);
        std::vector<llvm::Type*> arguments(arity, type);
        auto* function_type = llvm::FunctionType::get(result, arguments, false);
        return llvm::Function::Create(function_type,
                                      llvm::Function::ExternalLinkage,
                                      "case", module);
    }

    llvm::CallInst* nondet_call(const char* name, unsigned width) {
        // Keep the exact API spelling even when testing multiple data models.
        if (auto* previous = module.getFunction(name)) previous->setName("previous");
        auto* type = llvm::IntegerType::get(llvm_context, width);
        auto* f = function(width, type, 0);
        auto* block = llvm::BasicBlock::Create(llvm_context, "entry", f);
        auto* callee = llvm::Function::Create(llvm::FunctionType::get(type, false),
            llvm::Function::ExternalLinkage, name, module);
        auto* call = llvm::CallInst::Create(callee, {}, "input", block);
        llvm::ReturnInst::Create(llvm_context, call, block);
        return call;
    }

    Expression execute(llvm::Function* function, llvm::Instruction* operation,
                       const std::vector<z3::expr>& inputs) {
        Memory memory;
        memory.push_frame(function);
        for (unsigned i = 0; i < inputs.size(); ++i) {
            memory.put_temp(function->getArg(i), Expression(inputs[i]));
        }
        auto* instruction = AInstruction::create(operation);
        auto state = std::make_shared<State>(session, ctx, instruction,
                                            nullptr, memory,
                                            Expression(ctx.bool_val(true)),
                                            trace_ty{});
        auto states = instruction->execute(state);
        if (states.size() != 1) throw std::runtime_error("expected one successor");
        return states.front()->evaluate(operation);
    }

    Expression binary(unsigned opcode, unsigned width, z3::expr lhs,
                      z3::expr rhs, bool exact = false) {
        auto* type = llvm::IntegerType::get(llvm_context, width);
        auto* f = function(width, type, 2);
        auto* block = llvm::BasicBlock::Create(llvm_context, "entry", f);
        auto* operation = llvm::BinaryOperator::Create(
            static_cast<llvm::Instruction::BinaryOps>(opcode), f->getArg(0),
            f->getArg(1), "result", block);
        if (opcode == llvm::Instruction::Add || opcode == llvm::Instruction::Sub ||
            opcode == llvm::Instruction::Mul || opcode == llvm::Instruction::Shl) {
            operation->setHasNoSignedWrap(true);
            operation->setHasNoUnsignedWrap(true);
        }
        if (exact) operation->setIsExact(true);
        llvm::ReturnInst::Create(llvm_context, operation, block);
        return execute(f, operation, {lhs, rhs});
    }

    Expression cast(unsigned opcode, unsigned from, unsigned to, z3::expr value) {
        auto* target = llvm::IntegerType::get(llvm_context, to);
        auto* f = function(from, target, 1);
        auto* block = llvm::BasicBlock::Create(llvm_context, "entry", f);
        auto* operation = llvm::CastInst::Create(
            static_cast<llvm::Instruction::CastOps>(opcode), f->getArg(0),
            target, "result", block);
        llvm::ReturnInst::Create(llvm_context, operation, block);
        return execute(f, operation, {value});
    }
};

TEST_F(RelaxedIntegerOperations, AllBinaryOperationsAvoidMachineWidthEncoding) {
    const std::vector<unsigned> operations = {
        llvm::Instruction::Add, llvm::Instruction::Sub, llvm::Instruction::Mul,
        llvm::Instruction::SDiv, llvm::Instruction::UDiv,
        llvm::Instruction::SRem, llvm::Instruction::URem,
        llvm::Instruction::And, llvm::Instruction::Or, llvm::Instruction::Xor,
        llvm::Instruction::Shl, llvm::Instruction::LShr, llvm::Instruction::AShr};
    for (unsigned width : {8u, 16u, 32u, 64u, 128u}) {
        for (unsigned opcode : operations) {
            SCOPED_TRACE(std::to_string(width) + "/" + std::to_string(opcode));
            Expression result = binary(opcode, width, ctx.int_const("x"), ctx.int_val(3));
            EXPECT_FALSE(has_bitvector(result.as_expr()));
            EXPECT_FALSE(has_bitvector(result.defined()));
            EXPECT_TRUE(result.defined().simplify().is_true());
            const std::string formula = result.as_expr().to_string();
            for (const char* bound : {"256", "65536", "4294967296",
                                      "18446744073709551616"}) {
                EXPECT_EQ(formula.find(bound), std::string::npos);
            }
        }
    }
}

TEST_F(RelaxedIntegerOperations, WideIntegerConstantsDoNotRequireHostWidthConversion) {
    auto* constant = llvm::ConstantInt::get(
        llvm_context, llvm::APInt(128, "18446744073709551616", 10));
    auto* f = function(128, constant->getType(), 0);
    auto* block = llvm::BasicBlock::Create(llvm_context, "entry", f);
    auto* ret = llvm::ReturnInst::Create(llvm_context, constant, block);
    Memory memory;
    memory.push_frame(f);
    State state(session, ctx, AInstruction::create(ret), nullptr,
                memory, Expression(ctx.bool_val(true)), {});
    EXPECT_EQ(state.evaluate(constant).as_expr().to_string(),
              "18446744073709551616");
    auto* global = new llvm::GlobalVariable(
        module, constant->getType(), false, llvm::GlobalValue::ExternalLinkage,
        constant, "wide");
    EXPECT_EQ(memory.add_global(*global)->read().as_expr().to_string(),
              "18446744073709551616");
}

TEST_F(RelaxedIntegerOperations, PhiBundleUsesOnePredecessorSnapshot) {
    auto* type = llvm::Type::getInt32Ty(llvm_context);
    auto* f = function(32, type, 0);
    auto* entry = llvm::BasicBlock::Create(llvm_context, "entry", f);
    auto* header = llvm::BasicBlock::Create(llvm_context, "header", f);
    auto* latch = llvm::BasicBlock::Create(llvm_context, "latch", f);
    llvm::BranchInst::Create(header, entry);
    auto* p = llvm::PHINode::Create(type, 2, "p", header);
    auto* q = llvm::PHINode::Create(type, 2, "q", header);
    auto* unchanged = llvm::PHINode::Create(type, 2, "unchanged", header);
    p->addIncoming(llvm::ConstantInt::get(type, 1), entry);
    q->addIncoming(llvm::ConstantInt::get(type, 2), entry);
    unchanged->addIncoming(llvm::ConstantInt::get(type, 3), entry);
    p->addIncoming(q, latch);
    q->addIncoming(p, latch);
    unchanged->addIncoming(unchanged, latch);
    auto* next = llvm::BranchInst::Create(latch, header);
    llvm::BranchInst::Create(header, latch);
    Memory memory;
    memory.push_frame(f);
    memory.put_temp(p, Expression(ctx.int_const("old_p")));
    memory.put_temp(q, Expression(ctx.int_const("old_q")));
    memory.put_temp(unchanged, Expression(ctx.int_val(3)));
    State original(session, ctx, AInstruction::create(p), nullptr, memory,
                   Expression(ctx.bool_val(true)), {latch});
    State successor(original);
    successor.execute_phi_bundle();
    EXPECT_TRUE(z3::eq(successor.evaluate(p).as_expr(), ctx.int_const("old_q")));
    EXPECT_TRUE(z3::eq(successor.evaluate(q).as_expr(), ctx.int_const("old_p")));
    EXPECT_EQ(successor.evaluate(unchanged).as_expr().to_string(), "3");
    EXPECT_EQ(successor.pc->inst, next);
    EXPECT_TRUE(z3::eq(original.evaluate(p).as_expr(), ctx.int_const("old_p")));
    LoopState probe(original);
    probe.execute_phi_bundle();
    EXPECT_TRUE(z3::eq(probe.evaluate(q).as_expr(), ctx.int_const("old_p")));
    State first_entry(original);
    first_entry.trace = {entry};
    first_entry.execute_phi_bundle();
    EXPECT_EQ(first_entry.evaluate(p).as_expr().to_string(), "1");
    EXPECT_EQ(first_entry.evaluate(q).as_expr().to_string(), "2");
}

TEST_F(RelaxedIntegerOperations, NondetInputsRetainFiniteTypeDomains) {
    struct Case { const char* name; unsigned width; bool is_unsigned; };
    for (const Case test : {
             Case{"__VERIFIER_nondet_uchar", 8, true},
             Case{"__VERIFIER_nondet_ushort", 16, true},
             Case{"__VERIFIER_nondet_uint", 32, true},
             Case{"__VERIFIER_nondet_ulong", 32, true},
             Case{"__VERIFIER_nondet_ulong", 64, true},
             Case{"__VERIFIER_nondet_ulonglong", 64, true},
             Case{"__VERIFIER_nondet_schar", 8, false},
             Case{"__VERIFIER_nondet_short", 16, false},
             Case{"__VERIFIER_nondet_int", 32, false},
             Case{"__VERIFIER_nondet_long", 32, false},
             Case{"__VERIFIER_nondet_long", 64, false},
             Case{"__VERIFIER_nondet_longlong", 64, false},
             // No host-width conversion, and bounds follow IR, not API size.
             Case{"__VERIFIER_nondet_uint", 128, true},
             Case{"__VERIFIER_nondet_int", 128, false}}) {
        SCOPED_TRACE(std::string(test.name) + "/" + std::to_string(test.width));
        auto* call = nondet_call(test.name, test.width);
        Memory memory;
        memory.push_frame(call->getFunction());
        LoopState state(session, ctx, AInstruction::create(call), nullptr,
                        memory, Expression(ctx.bool_val(true)),
                        Expression(ctx.bool_val(true)), {});
        auto input = ctx.int_const("input");
        state.constrain_nondet_input(call, Expression(input));
        auto condition = state.get_path_condition().as_expr();
        EXPECT_FALSE(has_bitvector(condition));
        EXPECT_EQ(condition.to_string().find("mod"), std::string::npos);
        auto minimum = test.is_unsigned ? ctx.int_val(0) :
            integer_semantics::signed_min(ctx, test.width);
        auto maximum = test.is_unsigned ?
            integer_semantics::power_of_two(ctx, test.width) - 1 :
            integer_semantics::signed_max(ctx, test.width);
        for (auto boundary : {minimum, maximum}) {
            z3::solver solver(ctx);
            solver.add(condition && input == boundary);
            EXPECT_EQ(solver.check(), z3::sat);
        }
        for (auto outside : {minimum - 1, maximum + 1}) {
            z3::solver solver(ctx);
            solver.add(condition && input == outside);
            EXPECT_EQ(solver.check(), z3::unsat);
        }
        // This input-domain condition is not a branch decision.
        EXPECT_TRUE(state.path_condition_in_loop.as_expr().simplify().is_true());
    }
}

TEST_F(RelaxedIntegerOperations, FreshCallsEachReceiveTheirOwnInputBounds) {
    auto* call = nondet_call("__VERIFIER_nondet_uchar", 8);
    Memory memory;
    memory.push_frame(call->getFunction());
    auto* instruction = AInstruction::create(call);
    auto state = std::make_shared<State>(session, ctx, instruction, nullptr,
        memory, Expression(ctx.bool_val(true)), trace_ty{});
    auto first = instruction->execute(state).front();
    auto input0 = first->evaluate(call).as_expr();
    first->pc = instruction; // Same call site visited on a later loop iteration.
    auto second = instruction->execute(first).front();
    auto input1 = second->evaluate(call).as_expr();
    EXPECT_FALSE(z3::eq(input0, input1));
    auto condition = second->get_path_condition().as_expr();
    EXPECT_FALSE(has_bitvector(condition));
    EXPECT_EQ(condition.to_string().find("mod"), std::string::npos);
    for (auto input : {input0, input1}) {
        for (int outside : {-1, 256, 65026}) {
            z3::solver solver(ctx);
            solver.add(condition && input == outside);
            EXPECT_EQ(solver.check(), z3::unsat);
        }
    }
    z3::solver independent(ctx);
    independent.add(condition && input0 == 0 && input1 == 255);
    EXPECT_EQ(independent.check(), z3::sat);
}

TEST_F(RelaxedIntegerOperations, SequenceElementsReceiveInputBounds) {
    auto* call = nondet_call("__VERIFIER_nondet_ushort", 16);
    Memory memory;
    memory.push_frame(call->getFunction());
    LoopState state(session, ctx, AInstruction::create(call), nullptr,
        memory, Expression(ctx.bool_val(true)), Expression(ctx.bool_val(true)), {});
    auto oracle = ctx.function("oracle", ctx.int_sort(), ctx.int_sort());
    auto index = ctx.int_const("index");
    state.constrain_nondet_input(call, Expression(oracle(index)));
    state.constrain_nondet_input(call, Expression(oracle(index + 1)));
    auto condition = state.get_path_condition().as_expr();
    for (auto input : {oracle(index), oracle(index + 1)}) {
        z3::solver solver(ctx);
        solver.add(condition && input == ctx.int_val("4294836226"));
        EXPECT_EQ(solver.check(), z3::unsat);
    }
    z3::solver independent(ctx);
    independent.add(condition && oracle(index) == 0 && oracle(index + 1) == 65535);
    EXPECT_EQ(independent.check(), z3::sat);
    EXPECT_TRUE(state.path_condition_in_loop.as_expr().simplify().is_true());
}

TEST_F(RelaxedIntegerOperations, PlainCharUsesAbiSignednessWhenKnown) {
    for (auto attr : {llvm::Attribute::SExt, llvm::Attribute::ZExt,
                      llvm::Attribute::None}) {
        auto* call = nondet_call("__VERIFIER_nondet_char", 8);
        if (attr != llvm::Attribute::None) call->getCalledFunction()->addRetAttr(attr);
        Memory memory;
        State state(session, ctx, AInstruction::create(call), nullptr, memory,
                    Expression(ctx.bool_val(true)), {});
        auto input = ctx.int_const("input");
        state.constrain_nondet_input(call, Expression(input));
        auto condition = state.get_path_condition().as_expr();
        for (int value : {-129, -128, -1, 0, 127, 128, 255, 256}) {
            bool allowed = value >= -128 && value <= 255;
            if (attr == llvm::Attribute::SExt) allowed = value >= -128 && value <= 127;
            if (attr == llvm::Attribute::ZExt) allowed = value >= 0 && value <= 255;
            z3::solver solver(ctx);
            solver.add(condition && input == value);
            EXPECT_EQ(solver.check(), allowed ? z3::sat : z3::unsat);
        }
    }
}

TEST_F(RelaxedIntegerOperations, DoesNotGuessDomainsOfOtherExternalApis) {
    for (const char* name : {"external_int", "__VERIFIER_nondet_unrecognized"}) {
        auto* call = nondet_call(name, 32);
        State state(session, ctx, AInstruction::create(call), nullptr, Memory{},
                    Expression(ctx.bool_val(true)), {});
        state.constrain_nondet_input(call, Expression(ctx.int_const("input")));
        EXPECT_TRUE(state.get_path_condition().as_expr().simplify().is_true());
    }
}

TEST_F(RelaxedIntegerOperations, BooleanInputsKeepTheirBooleanDomain) {
    for (unsigned width : {1u, 32u}) {
        auto* call = nondet_call("__VERIFIER_nondet_bool", width);
        State state(session, ctx, AInstruction::create(call), nullptr, Memory{},
                    Expression(ctx.bool_val(true)), {});
        if (width == 1) {
            state.constrain_nondet_input(call, Expression(ctx.bool_const("input")));
            EXPECT_TRUE(state.get_path_condition().as_expr().simplify().is_true());
        } else {
            auto input = ctx.int_const("input");
            state.constrain_nondet_input(call, Expression(input));
            for (int value : {-1, 0, 1, 2}) {
                z3::solver solver(ctx);
                solver.add(state.get_path_condition().as_expr() && input == value);
                EXPECT_EQ(solver.check(), value == 0 || value == 1 ? z3::sat : z3::unsat);
            }
        }
    }
}

TEST_F(RelaxedIntegerOperations, UnsignedComparisonsDecodeHighBitConstants) {
    auto* type = llvm::Type::getInt64Ty(llvm_context);
    auto* threshold = llvm::ConstantInt::get(
        llvm_context, llvm::APInt(64, "18446744065119617025", 10));
    for (auto pred : {llvm::CmpInst::ICMP_UGT, llvm::CmpInst::ICMP_SGT}) {
        auto* f = function(64, llvm::Type::getInt1Ty(llvm_context), 1);
        auto* block = llvm::BasicBlock::Create(llvm_context, "entry", f);
        auto* cmp = new llvm::ICmpInst(block, pred, f->getArg(0), threshold);
        llvm::ReturnInst::Create(llvm_context, cmp, block);
        auto result = execute(f, cmp, {ctx.int_val(0)}).as_expr().simplify();
        EXPECT_EQ(result.is_true(), pred == llvm::CmpInst::ICMP_SGT);
    }
}

TEST_F(RelaxedIntegerOperations, ComparisonsAreOrdinaryIntegerComparisons) {
    for (unsigned width : {8u, 16u, 32u, 64u, 128u}) {
        for (auto predicate : {llvm::ICmpInst::ICMP_EQ, llvm::ICmpInst::ICMP_NE,
                               llvm::ICmpInst::ICMP_SLT, llvm::ICmpInst::ICMP_SLE,
                               llvm::ICmpInst::ICMP_SGT, llvm::ICmpInst::ICMP_SGE,
                               llvm::ICmpInst::ICMP_ULT, llvm::ICmpInst::ICMP_ULE,
                               llvm::ICmpInst::ICMP_UGT, llvm::ICmpInst::ICMP_UGE}) {
            auto* f = function(width, llvm::Type::getInt1Ty(llvm_context), 2);
            auto* block = llvm::BasicBlock::Create(llvm_context, "entry", f);
            llvm::IRBuilder<> builder(block);
            auto* operation = llvm::cast<llvm::Instruction>(builder.CreateICmp(
                predicate, f->getArg(0), f->getArg(1), "result"));
            llvm::ReturnInst::Create(llvm_context, operation, block);
            const auto result = execute(f, operation, {ctx.int_const("x"), ctx.int_const("y")});
            EXPECT_FALSE(has_bitvector(result.as_expr()));
            EXPECT_EQ(result.as_expr().to_string().find("mod"), std::string::npos);
        }
    }
}

TEST_F(RelaxedIntegerOperations, CastsDoNotNormalizeToAnyIntegerWidth) {
    const z3::expr x = ctx.int_const("x");
    for (const auto& [from, to] : {std::pair{8u, 64u}, std::pair{32u, 128u}}) {
        for (auto opcode : {llvm::Instruction::ZExt, llvm::Instruction::SExt}) {
            const auto result = cast(opcode, from, to, x);
            EXPECT_TRUE(z3::eq(result.as_expr().simplify(), x));
        }
        const auto result = cast(llvm::Instruction::Trunc, to, from, x);
        EXPECT_TRUE(z3::eq(result.as_expr().simplify(), x));
    }
    EXPECT_EQ(cast(llvm::Instruction::ZExt, 1, 64, ctx.bool_val(true))
                  .as_expr().simplify().get_numeral_int(), 1);
    EXPECT_EQ(cast(llvm::Instruction::SExt, 1, 64, ctx.bool_val(true))
                  .as_expr().simplify().get_numeral_int(), -1);
    EXPECT_TRUE(cast(llvm::Instruction::Trunc, 64, 1, ctx.int_val(3))
                    .as_expr().simplify().is_true());
    EXPECT_TRUE(cast(llvm::Instruction::Trunc, 64, 1, ctx.int_val(2))
                    .as_expr().simplify().is_false());
}

TEST_F(RelaxedIntegerOperations, DivisionRetainsZeroAndExactnessChecksOnly) {
    for (auto opcode : {llvm::Instruction::SDiv, llvm::Instruction::UDiv,
                         llvm::Instruction::SRem, llvm::Instruction::URem}) {
        EXPECT_TRUE(binary(opcode, 32, ctx.int_val(7), ctx.int_val(0))
                        .defined().simplify().is_false());
    }
    const auto overflow = binary(llvm::Instruction::SDiv, 32,
                                 ctx.int_val("-2147483648"), ctx.int_val(-1));
    EXPECT_EQ(overflow.as_expr().simplify().to_string(), "2147483648");
    EXPECT_TRUE(overflow.defined().simplify().is_true());
    EXPECT_EQ(binary(llvm::Instruction::SDiv, 64, ctx.int_val(-7), ctx.int_val(3))
                  .as_expr().simplify().get_numeral_int(), -2);
    EXPECT_EQ(binary(llvm::Instruction::SRem, 64, ctx.int_val(-7), ctx.int_val(3))
                  .as_expr().simplify().get_numeral_int(), -1);
    EXPECT_TRUE(binary(llvm::Instruction::SDiv, 32, ctx.int_val(7), ctx.int_val(3), true)
                    .defined().simplify().is_false());
}

TEST_F(RelaxedIntegerOperations, ShiftsHaveNoMachineWidthLimit) {
    const auto result = binary(llvm::Instruction::Shl, 8, ctx.int_val(1), ctx.int_val(40));
    EXPECT_EQ(result.as_expr().simplify().to_string(), "1099511627776");
    EXPECT_TRUE(result.defined().simplify().is_true());
    EXPECT_EQ(binary(llvm::Instruction::AShr, 32, ctx.int_val(-3), ctx.int_val(1))
                  .as_expr().simplify().get_numeral_int(), -2);
    EXPECT_TRUE(binary(llvm::Instruction::Shl, 32, ctx.int_val(1), ctx.int_val(-1))
                    .defined().simplify().is_false());
}

TEST_F(RelaxedIntegerOperations, BooleanLogicAvoidsBitvectors) {
    for (auto opcode : {llvm::Instruction::And, llvm::Instruction::Or,
                         llvm::Instruction::Xor, llvm::Instruction::Add}) {
        const auto result = binary(opcode, 1, ctx.bool_const("a"), ctx.bool_const("b"));
        EXPECT_TRUE(result.as_expr().is_bool());
        EXPECT_FALSE(has_bitvector(result.as_expr()));
    }
}

TEST_F(RelaxedIntegerOperations, SymbolicBitwiseAndShiftsDoNotFallBackToBitvectors) {
    for (auto opcode : {llvm::Instruction::And, llvm::Instruction::Or,
                         llvm::Instruction::Xor, llvm::Instruction::Shl,
                         llvm::Instruction::LShr, llvm::Instruction::AShr}) {
        EXPECT_THROW(binary(opcode, 32, ctx.int_const("x"), ctx.int_const("y")),
                     VerifierError);
    }
}

TEST_F(RelaxedIntegerOperations, ConstantMasksMatchUnboundedBitwiseValues) {
    using namespace integer_semantics;
    for (int value = -12; value <= 12; ++value) {
        for (int mask = -8; mask <= 8; ++mask) {
            for (auto operation : {IntegerBitwiseOperation::And,
                                    IntegerBitwiseOperation::Or,
                                    IntegerBitwiseOperation::Xor}) {
                const auto result = integer_bitwise(ctx.int_val(value), ctx.int_val(mask), operation);
                ASSERT_TRUE(result);
                const int expected = operation == IntegerBitwiseOperation::And ? value & mask
                                   : operation == IntegerBitwiseOperation::Or ? value | mask
                                   : value ^ mask;
                EXPECT_EQ(result->simplify().get_numeral_int(), expected);
                EXPECT_FALSE(has_bitvector(*result));
            }
        }
    }
}
} // namespace
