#include <gtest/gtest.h>
#include <llvm/IR/IRBuilder.h>
#include "AInstruction.h"
#include "engine.h"
#include "ArrayShape.h"
#include "rec_solver.h"

using namespace ari_exe;

TEST(RecurrencePreparation, EntrySeedsDependOnChangingFormalCoordinates) {
    z3::context ctx;
    const auto q=ctx.int_const("seed_q"),t=ctx.int_const("seed_t");
    const auto f=ctx.function("seed_f",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto entry=ctx.function("seed_entry",ctx.int_sort(),ctx.int_sort());
    for (bool moving:{false,true}) {
        rec_solver solver(ctx);
        solver.set_ind_var(t);
        std::vector<z3::expr> conditions{t<=0,t>0};
        std::vector<rec_ty> equations{
            {{f(q,t),z3::ite(q==0,ctx.int_val(0),entry(q))}},
            {{f(q,t),f(moving ? q-1 : q,t-1)+1}}};
        solver.set_eqs(conditions,equations);
        const bool abstracted=solver.rec2string().find("ari_rec_parameter_")!=std::string::npos;
        EXPECT_EQ(abstracted,!moving);
    }
}

TEST(GuardedArrayTupling, ProvesMultipathProgressionAndRejectsChangedAssertion) {
    const char* previous=std::getenv("ARITHEXE_MULTIVARIATE_STRATEGY");
    struct Restore {
        bool present;
        std::string value;
        ~Restore() {
            if (present) setenv("ARITHEXE_MULTIVARIATE_STRATEGY",value.c_str(),1);
            else unsetenv("ARITHEXE_MULTIVARIATE_STRATEGY");
        }
    } restore{previous!=nullptr,previous ? previous : ""};
    setenv("ARITHEXE_MULTIVARIATE_STRATEGY","tupling",1);
    for (const auto& prefix:{std::string("true"),std::string("false")}) {
        Engine engine(std::string(ARITHEXE_TEST_BENCHMARK_DIR)+
                      "/arrays/"+prefix+"_multipath_progression.c");
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_EQ(engine.verify(),prefix=="true" ? HOLD : FAIL)<<engine.get_issue_message();
    }
}

TEST(DirectVerificationCalls, LatchErrorBranchIsAnObligationNotAnAssumption) {
    for (const auto& prefix:{std::string("true"),std::string("false")}) {
        Engine engine(std::string(ARITHEXE_TEST_BENCHMARK_DIR)+
                      "/arrays/"+prefix+"_direct_latch_assert.c");
        engine.get_session().set_ignore_bitwidth_constraints(true);
        EXPECT_EQ(engine.verify(),prefix=="true" ? HOLD : FAIL)<<engine.get_issue_message();
    }
}

TEST(CertifiedArithmetic, ExactPolynomialDivisionsDoNotInventDivisibility) {
    VerificationSession session;
    auto& ctx=session.analyses().get_z3ctx();
    auto n=ctx.int_const("div_n"),m=ctx.int_const("div_m");
    for (const auto& numerator : {n*(n-1)*(n-1),n*(n-1)*m}) {
        auto identity=2*(numerator/2)==numerator;
        z3::solver proof(ctx);
        proof.add(session.aggregate_query(!identity,ctx.bool_val(true)));
        EXPECT_EQ(proof.check(),z3::unsat);
    }
    auto wrong=2*((n*n)/2)==n*n;
    z3::solver negative(ctx);
    negative.add(n==1);
    negative.add(session.aggregate_query(!wrong,n==1));
    EXPECT_EQ(negative.check(),z3::sat);
}

TEST(GeneralFolds, ResidualFoldCannotValidateCounterexampleButFiniteFoldCan) {
    const char* previous=std::getenv("ARITHEXE_FOLD_OPERATORS");
    const std::string saved=previous ? previous : "";
    struct Restore {
        std::string saved;
        bool present;
        ~Restore() {
            if (present) setenv("ARITHEXE_FOLD_OPERATORS",saved.c_str(),1);
            else unsetenv("ARITHEXE_FOLD_OPERATORS");
        }
    } restore{saved,previous!=nullptr};
    const auto registry=std::string(ARITHEXE_TEST_BENCHMARK_DIR)+"/../../docs/fold-operators.json";
    setenv("ARITHEXE_FOLD_OPERATORS",registry.c_str(),1);
    for (const auto& name : {"true_residual_fold_max.c","false_user_fold_max.c"}) {
        Engine engine(std::string(ARITHEXE_TEST_BENCHMARK_DIR)+"/arrays/"+name);
        engine.get_session().set_ignore_bitwidth_constraints(true);
        const auto result=engine.verify();
        if (std::string(name)=="false_user_fold_max.c") EXPECT_EQ(result,FAIL);
        else EXPECT_EQ(result,VERIUNKNOWN);
    }
}

TEST(GeneralFolds, StagedQueriesNormalizeAcrossExpansionBoundary) {
    VerificationSession session;
    auto& ctx = session.analyses().get_z3ctx();
    auto lo=ctx.int_const("stage_lo"),hi=ctx.int_const("stage_hi"),n=ctx.int_const("stage_n");
    auto i=ctx.int_const("stage_i");
    auto input=ctx.function("stage_input",ctx.int_sort(),ctx.int_sort());
    auto base=ctx.function("ari_finite_stage_base",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    auto dup=ctx.function("ari_finite_stage_dup",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    session.register_aggregate(false,base(lo,hi),i,input(i));
    session.register_aggregate(false,dup(lo,hi,n),i,z3::ite(i<n,input(i),input(i-n)));
    for (int length : {15,16,17,20,31,32,33,40,0,-1}) {
        auto bound=ctx.int_val(length);
        auto left=dup(ctx.int_val(0),2*bound,bound);
        auto right=2*base(ctx.int_val(0),bound);
        z3::solver proof(ctx);
        proof.add(session.aggregate_query(left!=right,ctx.bool_val(true)));
        EXPECT_EQ(proof.check(),z3::unsat) << length;
        z3::solver negative(ctx);
        negative.add(session.aggregate_query(left!=right+1,ctx.bool_val(true)));
        EXPECT_EQ(negative.check(),z3::sat) << length;
    }
    auto left=dup(ctx.int_val(0),2*n,n),right=2*base(ctx.int_val(0),n);
    z3::solver symbolic(ctx);
    symbolic.add(n>0);
    symbolic.add(session.aggregate_query(left!=right,n>0));
    EXPECT_EQ(symbolic.check(),z3::unsat);
}

TEST(GeneralFolds, CertifiedSymbolicSumPartitionMultiplicity) {
    VerificationSession session;
    auto& ctx = session.analyses().get_z3ctx();
    auto lo=ctx.int_const("lo"), hi=ctx.int_const("hi"), n=ctx.int_const("n");
    auto i=ctx.int_const("idx");
    auto input=ctx.function("partition_input",ctx.int_sort(),ctx.int_sort());
    auto base=ctx.function("ari_finite_base",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    auto dup=ctx.function("ari_finite_dup",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    session.register_aggregate(false,base(lo,hi),i,input(i));
    session.register_aggregate(false,dup(lo,hi,n),i,
        z3::ite(i<n,input(i),input(i-n)));
    auto query=dup(ctx.int_val(0),2*n,n)==2*base(ctx.int_val(0),n);
    z3::solver proof(ctx);
    proof.add(n>0);
    proof.add(session.aggregate_identities(query,n>0));
    proof.add(!query);
    EXPECT_EQ(proof.check(),z3::unsat);
    // An omitted element cannot be normalized to two complete copies.
    auto bad=dup(ctx.int_val(0),2*n-1,n)==2*base(ctx.int_val(0),n);
    z3::solver negative(ctx);
    negative.add(n>1);
    negative.add(session.aggregate_identities(bad,n>1));
    negative.add(!bad);
    EXPECT_EQ(negative.check(),z3::sat);
}

TEST(GeneralFolds, PreservesSeedOrderAndDoesNotApplySumRules) {
    VerificationSession session;
    auto& ctx = session.analyses().get_z3ctx();
    auto lo=ctx.int_const("lo"),hi=ctx.int_const("hi"),seed=ctx.int_const("seed");
    auto i=ctx.int_const("idx"),a=ctx.int_const("left"),b=ctx.int_const("right");
    auto f=ctx.function("ari_finite_fold_ordered",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    auto input=ctx.function("input",ctx.int_sort(),ctx.int_sort());
    session.register_fold(f(lo,hi,seed),i,input(i),a,b,a-b);
    z3::solver check(ctx);
    check.add(session.expand_aggregates(f(ctx.int_val(0),ctx.int_val(2),ctx.int_val(7)))
              != 7-input(0)-input(1));
    EXPECT_EQ(check.check(),z3::unsat);
    EXPECT_TRUE(z3::eq(session.expand_aggregates(f(lo,lo,seed)),seed));
    EXPECT_TRUE(session.aggregate_identities(f(lo,hi,seed),ctx.bool_val(true)).empty());
}

TEST(GeneralFolds, SymbolicIdempotentPartitionPreservesSeed) {
    VerificationSession session;
    auto& ctx=session.analyses().get_z3ctx();
    auto lo=ctx.int_const("lo"),hi=ctx.int_const("hi"),n=ctx.int_const("n");
    auto seed=ctx.int_const("seed"),i=ctx.int_const("idx");
    auto a=ctx.int_const("a"),b=ctx.int_const("b");
    auto input=ctx.function("max_input",ctx.int_sort(),ctx.int_sort());
    auto base=ctx.function("ari_finite_max_base",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    auto dup=ctx.function("ari_finite_max_dup",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    session.register_fold(base(lo,hi,seed),i,input(i),a,b,z3::ite(a>b,a,b));
    session.register_fold(dup(lo,hi,seed,n),i,z3::ite(i<n,input(i),input(i-n)),
                          a,b,z3::ite(a>b,a,b));
    auto query=dup(ctx.int_val(0),2*n,seed,n)==base(ctx.int_val(0),n,seed);
    z3::solver proof(ctx);
    proof.add(n>=0);
    proof.add(session.aggregate_identities(query,n>=0));
    proof.add(!query);
    EXPECT_EQ(proof.check(),z3::unsat);
    auto bad=dup(ctx.int_val(0),2*n,seed+1,n)==base(ctx.int_val(0),n,seed);
    z3::solver negative(ctx);
    negative.add(n>=0);
    negative.add(session.aggregate_identities(bad,n>=0));
    negative.add(!bad);
    EXPECT_EQ(negative.check(),z3::sat);
}

TEST(GeneralFolds, RejectsExternalOperatorFunctions) {
    VerificationSession session;
    auto& ctx=session.analyses().get_z3ctx();
    auto lo=ctx.int_const("lo"),hi=ctx.int_const("hi"),seed=ctx.int_const("seed");
    auto i=ctx.int_const("idx"),a=ctx.int_const("left"),b=ctx.int_const("right");
    auto f=ctx.function("ari_finite_fold_bad",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    auto external=ctx.function("external",ctx.int_sort(),ctx.int_sort());
    EXPECT_THROW(session.register_fold(f(lo,hi,seed),i,i,a,b,external(a)),std::invalid_argument);
}

TEST(GeneralFolds, SymbolicElementwiseSepPermutationAndMultiplicity) {
    VerificationSession session;
    auto& ctx=session.analyses().get_z3ctx();
    auto lo=ctx.int_const("sep_lo"),hi=ctx.int_const("sep_hi");
    auto n=ctx.int_const("sep_n"),i=ctx.int_const("sep_i");
    auto input=ctx.function("sep_input",ctx.int_sort(),ctx.int_sort());
    auto sep=[&](z3::expr value) { return z3::ite(value%2==0,ctx.int_val(1),ctx.int_val(-1)); };
    auto base=ctx.function("ari_finite_sep_base",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    auto rotation=ctx.function("ari_finite_sep_rotation",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    auto duplicate=ctx.function("ari_finite_sep_duplicate",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    auto swap=ctx.function("ari_finite_sep_swap",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    session.register_aggregate(false,base(lo,hi),i,sep(input(i)));
    // Both placements of the conditional map must normalize the full value.
    session.register_aggregate(false,rotation(lo,hi,n),i,sep(input(z3::ite(i+1<n,i+1,ctx.int_val(0)))));
    session.register_aggregate(false,duplicate(lo,hi,n),i,z3::ite(i<n,sep(input(i)),sep(input(i-n))));
    session.register_aggregate(false,swap(lo,hi,n),i,
        z3::ite(i==0,sep(input(n-1)),z3::ite(i==n-1,sep(input(0)),sep(input(i)))));
    auto reference=base(ctx.int_val(0),n);
    for (const auto& equality : {
             rotation(ctx.int_val(0),n,n)==reference,
             swap(ctx.int_val(0),n,n)==reference,
             duplicate(ctx.int_val(0),2*n,n)==2*reference}) {
        z3::solver proof(ctx);
        proof.add(n>1);
        proof.add(session.aggregate_query(!equality,n>1));
        EXPECT_EQ(proof.check(),z3::unsat) << equality;
    }
    for (int length : {16,17,32,33}) {
        auto bound=ctx.int_val(length);
        auto equality=duplicate(ctx.int_val(0),2*bound,bound)==2*base(ctx.int_val(0),bound);
        z3::solver proof(ctx);
        proof.add(session.aggregate_query(!equality,ctx.bool_val(true)));
        EXPECT_EQ(proof.check(),z3::unsat) << length;
    }
    for (const auto& equality : {
             duplicate(ctx.int_val(0),2*n,n)==reference,
             duplicate(ctx.int_val(0),2*n-1,n)==2*reference}) {
        z3::solver negative(ctx);
        negative.add(n>1);
        negative.add(session.aggregate_query(!equality,n>1));
        EXPECT_EQ(negative.check(),z3::sat) << equality;
    }
}

TEST(GeneralFolds, ElementwiseCertificatesRejectChangedValuesAndPositionDependence) {
    VerificationSession session;
    auto& ctx=session.analyses().get_z3ctx();
    auto lo=ctx.int_const("bad_sep_lo"),hi=ctx.int_const("bad_sep_hi");
    auto n=ctx.int_const("bad_sep_n"),i=ctx.int_const("bad_sep_i");
    auto input=ctx.function("bad_sep_input",ctx.int_sort(),ctx.int_sort());
    auto other=ctx.function("bad_sep_other",ctx.int_sort(),ctx.int_sort());
    auto sep=[&](z3::expr value) { return z3::ite(value%2==0,ctx.int_val(1),ctx.int_val(-1)); };
    auto base=ctx.function("ari_finite_bad_sep_base",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    session.register_aggregate(false,base(lo,hi),i,sep(input(i)));
    const auto rotated=z3::ite(i+1<n,i+1,ctx.int_val(0));
    std::vector<z3::expr> bodies{
        -sep(input(rotated)), sep(input(rotated)+1),
        z3::ite(i==0,ctx.int_val(1),sep(input(rotated))),
        i*sep(input(rotated)), sep(other(rotated)),
        sep(input(z3::ite(i==0,ctx.int_val(1),i))),
        z3::ite(i<n,sep(input(i)), -sep(input(i-n)))};
    for (unsigned p=0;p<bodies.size();++p) {
        auto name="ari_finite_bad_sep_"+std::to_string(p);
        auto bad=ctx.function(name.c_str(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
        session.register_aggregate(false,bad(lo,hi,n),i,bodies[p]);
        auto equality=bad(ctx.int_val(0),p==6 ? 2*n : n,n)==base(ctx.int_val(0),n);
        z3::solver negative(ctx);
        negative.add(n>2);
        negative.add(session.aggregate_query(!equality,n>2));
        EXPECT_EQ(negative.check(),z3::sat) << p;
    }
}

TEST(GeneralFolds, SumPermutationRetainsAdditivePositionRemainder) {
    VerificationSession session;
    auto& ctx=session.analyses().get_z3ctx();
    auto lo=ctx.int_const("remainder_lo"),hi=ctx.int_const("remainder_hi");
    auto n=ctx.int_const("remainder_n"),i=ctx.int_const("remainder_i");
    auto input=ctx.function("remainder_input",ctx.int_sort(),ctx.int_sort());
    auto base=ctx.function("ari_finite_remainder_base",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    auto rotation=ctx.function("ari_finite_remainder_rotation",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    auto bad=ctx.function("ari_finite_remainder_bad",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    const auto map=z3::ite(i+1<n,i+1,ctx.int_val(0));
    session.register_aggregate(false,base(lo,hi),i,input(i)+i);
    session.register_aggregate(false,rotation(lo,hi,n),i,input(map)+i);
    session.register_aggregate(false,bad(lo,hi,n),i,input(map)+i+1);
    auto reference=base(ctx.int_val(0),n);
    z3::solver proof(ctx);
    proof.add(n>1);
    proof.add(session.aggregate_query(rotation(ctx.int_val(0),n,n)!=reference,n>1));
    EXPECT_EQ(proof.check(),z3::unsat);
    z3::solver negative(ctx);
    negative.add(n>1);
    negative.add(session.aggregate_query(bad(ctx.int_val(0),n,n)!=reference,n>1));
    EXPECT_EQ(negative.check(),z3::sat);
}

TEST(GeneralFolds, SymbolicCommutativeFoldAndProductPermutation) {
    VerificationSession session;
    auto& ctx=session.analyses().get_z3ctx();
    auto lo=ctx.int_const("perm_lo"),hi=ctx.int_const("perm_hi"),n=ctx.int_const("perm_n");
    auto seed=ctx.int_const("perm_seed"),i=ctx.int_const("perm_i");
    auto a=ctx.int_const("perm_a"),b=ctx.int_const("perm_b");
    auto input=ctx.function("perm_input",ctx.int_sort(),ctx.int_sort());
    auto rotated=input(z3::ite(i+1<n,i+1,ctx.int_val(0)));
    std::vector<z3::expr> operators{z3::ite(a>b,a,b), a+b, a*b, a-b, a+2*b, (a+b)/2};
    for (unsigned p=0;p<operators.size();++p) {
        auto prefix="ari_finite_perm_"+std::to_string(p);
        auto base=ctx.function((prefix+"_base").c_str(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
        auto rotation=ctx.function((prefix+"_rotation").c_str(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
        // Also exercise transformed summands for generic folds.
        auto transform=[&](z3::expr x) { return p==0 ? z3::ite(x%2==0,ctx.int_val(1),ctx.int_val(-1)) : x; };
        session.register_fold(base(lo,hi,seed),i,transform(input(i)),a,b,operators[p]);
        session.register_fold(rotation(lo,hi,seed,n),i,transform(rotated),a,b,operators[p]);
        auto equality=rotation(ctx.int_val(0),n,seed,n)==base(ctx.int_val(0),n,seed);
        z3::solver proof(ctx);
        proof.add(n>1);
        proof.add(session.aggregate_query(!equality,n>1));
        EXPECT_EQ(proof.check(),p<3 ? z3::unsat : z3::sat) << p;
        auto changed_seed=rotation(ctx.int_val(0),n,seed+1,n)==base(ctx.int_val(0),n,seed);
        z3::solver negative(ctx);
        negative.add(n>1);
        negative.add(session.aggregate_query(!changed_seed,n>1));
        EXPECT_EQ(negative.check(),z3::sat) << p;
    }
    auto base=ctx.function("ari_finite_product_base",ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    auto rotation=ctx.function("ari_finite_product_rotation",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    auto dup=ctx.function("ari_finite_product_dup",ctx.int_sort(),ctx.int_sort(),ctx.int_sort(),ctx.int_sort());
    session.register_aggregate(true,base(lo,hi),i,input(i));
    session.register_aggregate(true,rotation(lo,hi,n),i,rotated);
    session.register_aggregate(true,dup(lo,hi,n),i,z3::ite(i<n,input(i),input(i-n)));
    auto value=base(ctx.int_val(0),n);
    for (const auto& equality : {rotation(ctx.int_val(0),n,n)==value,
                                dup(ctx.int_val(0),2*n,n)==value*value}) {
        z3::solver proof(ctx);
        proof.add(n>1);
        proof.add(session.aggregate_query(!equality,n>1));
        EXPECT_EQ(proof.check(),z3::unsat) << equality;
    }
    z3::solver negative(ctx);
    negative.add(n>1);
    negative.add(session.aggregate_query(dup(ctx.int_val(0),2*n,n)!=value,n>1));
    EXPECT_EQ(negative.check(),z3::sat);
}

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

TEST_F(StackArrayAllocation, ReconstructsIndependentCoordinatesWithoutDivision) {
    const auto n=ctx.int_const("columns"),r=ctx.int_const("row"),c=ctx.int_const("column");
    auto state=allocate(llvm::Type::getInt32Ty(llvm_context),Expression(n*n));
    z3::expr_vector dims(ctx);dims.push_back(n);dims.push_back(n);
    auto* array=state->memory.allocate(allocation,dims);
    array->set_native_vla_shape(true);
    const std::vector<Expression> zero{Expression(ctx.int_val(0)),Expression(ctx.int_val(0))};
    auto address=add_coordinate_offset(*array,zero,Expression(r*n));
    address=add_coordinate_offset(*array,address,Expression(c));
    ASSERT_EQ(address.size(),2u);
    EXPECT_TRUE(z3::eq(address[0].as_expr().simplify(),r));
    EXPECT_TRUE(z3::eq(address[1].as_expr().simplify(),c));
    state->append_path_condition(Expression(n>0&&c>=0&&c<n));
    EXPECT_NO_THROW(certify_array_coordinates(*array,address,state));
    EXPECT_THROW(certify_array_coordinates(*array,{Expression(r),Expression(n)},state),VerifierError);
}

TEST_F(StackArrayAllocation, ReconstructsRectangularThreeDimensionalCoordinates) {
    const auto m=ctx.int_const("middle"),n=ctx.int_const("inner");
    const auto i=ctx.int_const("outer_index"),j=ctx.int_const("middle_index"),k=ctx.int_const("inner_index");
    auto state=allocate(llvm::Type::getInt32Ty(llvm_context),Expression(m*n));
    z3::expr_vector dims(ctx);dims.push_back(ctx.int_val(7));dims.push_back(m);dims.push_back(n);
    auto* array=state->memory.allocate(allocation,dims);
    array->set_native_vla_shape(true);
    const std::vector<Expression> zero(3,Expression(ctx.int_val(0)));
    const auto address=add_coordinate_offset(*array,zero,Expression(i*m*n+j*n+k));
    ASSERT_EQ(address.size(),3u);
    EXPECT_TRUE(z3::eq(address[0].as_expr().simplify(),i));
    EXPECT_TRUE(z3::eq(address[1].as_expr().simplify(),j));
    EXPECT_TRUE(z3::eq(address[2].as_expr().simplify(),k));
}

TEST_F(StackArrayAllocation, DecodesConstantFoldedOffsetsWithoutChangingAliases) {
    auto state=allocate(llvm::Type::getInt32Ty(llvm_context),Expression(ctx.int_val(6)));
    z3::expr_vector dims(ctx);dims.push_back(ctx.int_val(2));dims.push_back(ctx.int_val(3));
    auto* array=state->memory.allocate(allocation,dims);
    const std::vector<Expression> zero(2,Expression(ctx.int_val(0)));
    for (const auto& pair:{std::pair{5,std::pair{1,2}},std::pair{-1,std::pair{-1,2}}}) {
        const auto address=add_coordinate_offset(*array,zero,Expression(ctx.int_val(pair.first)));
        EXPECT_TRUE(z3::eq(address[0].as_expr().simplify(),ctx.int_val(pair.second.first)));
        EXPECT_TRUE(z3::eq(address[1].as_expr().simplify(),ctx.int_val(pair.second.second)));
    }
}

TEST_F(StackArrayAllocation, KeepsReverseColumnsInsteadOfDividingTheirCoefficients) {
    const auto i=ctx.int_const("row"),j=ctx.int_const("column");
    auto state=allocate(llvm::Type::getInt32Ty(llvm_context),Expression(ctx.int_val(1500000)));
    z3::expr_vector dims(ctx);dims.push_back(ctx.int_val(1000));dims.push_back(ctx.int_val(1500));
    auto* array=state->memory.allocate(allocation,dims);
    array->set_native_vla_shape(true);
    const std::vector<Expression> zero(2,Expression(ctx.int_val(0)));
    const auto address=add_coordinate_offset(*array,zero,Expression(1499999-1500*i-j));
    EXPECT_TRUE(z3::eq(address[0].as_expr().simplify(),(999-i).simplify()));
    EXPECT_TRUE(z3::eq(address[1].as_expr().simplify(),(1499-j).simplify()));
}

TEST_F(StackArrayAllocation, BorrowsAcrossSymbolicRowsWithoutChangingTheFlatAddress) {
    const auto n=ctx.int_const("width"),r=ctx.int_const("row"),c=ctx.int_const("column");
    auto state=allocate(llvm::Type::getInt32Ty(llvm_context),Expression(n*n));
    z3::expr_vector dims(ctx);dims.push_back(n);dims.push_back(n);
    auto* array=state->memory.allocate(allocation,dims);
    array->set_native_vla_shape(true);
    state->append_path_condition(Expression(n>1&&r>=0&&r<n&&c>=0&&c<n));
    const std::vector<Expression> zero(2,Expression(ctx.int_val(0)));
    for (const auto& offset:{r*n+n-1-c,r*n-c}) {
        auto address=add_coordinate_offset(*array,zero,Expression(offset));
        address=normalize_array_coordinates(*array,address,state);
        z3::solver check(ctx);
        check.add(state->get_path_condition().as_expr());
        check.add(address[0].as_expr()*n+address[1].as_expr()!=offset);
        EXPECT_EQ(check.check(),z3::unsat);
        EXPECT_NO_THROW(certify_array_coordinates(*array,address,state));
    }
    EXPECT_THROW(normalize_array_coordinates(*array,
        {Expression(r),Expression(3*n)},state),VerifierError);
}

TEST_F(StackArrayAllocation, PropagatesReverseBorrowsAcrossThreeVariableDimensions) {
    const auto m=ctx.int_const("middle"),n=ctx.int_const("inner");
    const auto i=ctx.int_const("outer_index"),j=ctx.int_const("middle_index"),k=ctx.int_const("inner_index");
    auto state=allocate(llvm::Type::getInt32Ty(llvm_context),Expression(7*m*n));
    z3::expr_vector dims(ctx);dims.push_back(ctx.int_val(7));dims.push_back(m);dims.push_back(n);
    auto* array=state->memory.allocate(allocation,dims);
    array->set_native_vla_shape(true);
    state->append_path_condition(Expression(m>1&&n>1&&i>=0&&i<7&&j>=0&&j<m&&k>=0&&k<n));
    const std::vector<Expression> zero(3,Expression(ctx.int_val(0)));
    const auto offset=7*m*n-i*m*n-j*n-k-1;
    auto address=add_coordinate_offset(*array,zero,Expression(offset));
    ASSERT_NO_THROW(address=normalize_array_coordinates(*array,address,state))
        <<address[0].as_expr()<<", "<<address[1].as_expr()<<", "<<address[2].as_expr();
    z3::solver check(ctx);check.set("timeout",2000u);
    check.add(state->get_path_condition().as_expr());
    check.add(address[0].as_expr()!=6-i || address[1].as_expr()!=m-1-j ||
              address[2].as_expr()!=n-1-k);
    EXPECT_EQ(check.check(),z3::unsat);
}

TEST_F(StackArrayAllocation, CoordinateRecoveryPreservesOffsetDefinedness) {
    const auto n=ctx.int_const("columns"),i=ctx.int_const("index");
    auto state=allocate(llvm::Type::getInt32Ty(llvm_context),Expression(n*n));
    z3::expr_vector dims(ctx);dims.push_back(n);dims.push_back(n);
    auto* array=state->memory.allocate(allocation,dims);
    z3::expr_vector conditions(ctx),values(ctx),definitions(ctx);
    conditions.push_back(ctx.bool_val(true));values.push_back(i*n);definitions.push_back(i>0);
    const std::vector<Expression> zero(2,Expression(ctx.int_val(0)));
    const auto address=add_coordinate_offset(*array,zero,Expression(conditions,values,definitions));
    z3::solver checker(ctx);
    checker.add(address[0].defined()!=(i>0));
    EXPECT_EQ(checker.check(),z3::unsat);
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

TEST(NativeMultidimensionalVLA, PreservesRectangularCoordinatesEndToEnd) {
    for (const auto& prefix:{std::string("true"),std::string("false")}) {
        Engine engine(std::string(ARITHEXE_TEST_BENCHMARK_DIR)+"/arrays/"+prefix+"_native_vla_2d.c");
        engine.get_session().set_ignore_bitwidth_constraints(true);
        testing::internal::CaptureStdout();
        const auto result=engine.verify();
        const auto output=testing::internal::GetCapturedStdout();
        EXPECT_EQ(result,prefix=="true" ? HOLD : FAIL)<<engine.get_issue_message();
        EXPECT_NE(output.find("certified 2-dimensional VLA"),std::string::npos);
        EXPECT_NE(output.find("array_i1"),std::string::npos);
    }
}

TEST(NativeMultidimensionalVLA, PreservesThreeIndependentExtentsEndToEnd) {
    for (const auto& prefix:{std::string("true"),std::string("false")}) {
        Engine engine(std::string(ARITHEXE_TEST_BENCHMARK_DIR)+"/arrays/"+prefix+"_native_vla_3d.c");
        engine.get_session().set_ignore_bitwidth_constraints(true);
        testing::internal::CaptureStdout();
        const auto result=engine.verify();
        const auto output=testing::internal::GetCapturedStdout();
        EXPECT_EQ(result,prefix=="true" ? HOLD : FAIL)<<engine.get_issue_message();
        EXPECT_NE(output.find("certified 3-dimensional VLA"),std::string::npos);
        EXPECT_NE(output.find("array_i2"),std::string::npos);
    }
}

TEST(NativeMultidimensionalVLA, RetainsAGlobalBoundAcrossReadOnlyHelperCalls) {
    Engine engine(std::string(ARITHEXE_TEST_BENCHMARK_DIR)+"/arrays/true_native_vla_helper_bound.c");
    engine.get_session().set_ignore_bitwidth_constraints(true);
    EXPECT_EQ(engine.verify(),HOLD)<<engine.get_issue_message();
}

TEST(NativeMultidimensionalVLA, DoesNotFreezeAGlobalBoundModifiedByAHelper) {
    Engine engine(std::string(ARITHEXE_TEST_BENCHMARK_DIR)+"/arrays/false_native_vla_helper_bound.c");
    engine.get_session().set_ignore_bitwidth_constraints(true);
    try {
        EXPECT_NE(engine.verify(),HOLD)<<engine.get_issue_message();
    } catch (const VerifierError& error) {
        EXPECT_EQ(error.kind(),VerifierIssueKind::UnsupportedSemantics);
        EXPECT_NE(std::string(error.what()).find("canonical multidimensional"),std::string::npos);
    }
}
