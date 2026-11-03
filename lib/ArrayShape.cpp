#include "ArrayShape.h"

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/DebugProgramInstruction.h>
#include <llvm/BinaryFormat/Dwarf.h>
#include <spdlog/spdlog.h>

namespace ari_exe {
namespace {
llvm::Value* dimension_value(llvm::DILocalVariable* variable,
                             llvm::AllocaInst* allocation, const state_ptr& state) {
    llvm::Value* result=nullptr;
    for (auto& instruction:llvm::instructions(allocation->getFunction())) {
        for (auto& record:instruction.getDbgRecordRange()) {
            auto* value=llvm::dyn_cast<llvm::DbgVariableRecord>(&record);
            if (!value || !value->isDbgValue() || value->getVariable()!=variable ||
                value->getExpression()->getNumElements()!=0 ||
                value->getNumVariableLocationOps()!=1) continue;
            auto* candidate=value->getVariableLocationOp(0);
            if (!candidate || !candidate->getType()->isIntegerTy() ||
                llvm::isa<llvm::UndefValue>(candidate) || llvm::isa<llvm::PoisonValue>(candidate))
                continue;
            if (auto* producer=llvm::dyn_cast<llvm::Instruction>(candidate)) {
                if (!AnalysisManager::get_instance()->get_DT(allocation->getFunction())
                        .dominates(producer,allocation)) continue;
            }
            if (!llvm::isa<llvm::ConstantInt>(candidate) && !state->memory.get_object(candidate))
                continue;
            if (result && result!=candidate) return nullptr;
            result=candidate;
        }
    }
    return result;
}

std::optional<std::vector<z3::expr>> multiplication_factors(const z3::expr& expression) {
    std::vector<z3::expr> pending{expression},result;
    unsigned budget=1024;
    while (!pending.empty()) {
        if (!budget--) return std::nullopt;
        const auto term=pending.back();pending.pop_back();
        if (term.is_app() && term.decl().decl_kind()==Z3_OP_MUL)
            for (const auto& factor:term.args()) pending.push_back(factor);
        else result.push_back(term);
    }
    return result;
}

std::pair<z3::expr,z3::expr> split_multiple(const z3::expr& value,
                                          const z3::expr& stride) {
    auto& ctx=value.ctx();
    const auto stride_factors=multiplication_factors(stride);
    if (!stride_factors) return {ctx.int_val(0),value};
    const auto& divisors=*stride_factors;
    const bool constant_stride=std::all_of(divisors.begin(),divisors.end(),
        [](const z3::expr& divisor) { return divisor.is_numeral(); });
    std::vector<z3::expr> terms;
    const auto expanded=value.simplify();
    if (expanded.is_app() && expanded.decl().decl_kind()==Z3_OP_ADD)
        for (const auto& term:expanded.args()) terms.push_back(term);
    else terms.push_back(expanded);
    auto quotient=ctx.int_val(0), remainder=ctx.int_val(0);
    for (const auto& term:terms) {
        const auto factors=multiplication_factors(term);
        if (!factors) { remainder=remainder+term;continue; }
        auto coefficient=ctx.int_val(1), denominator=ctx.int_val(1);
        std::vector<z3::expr> variables;
        for (const auto& factor:*factors) {
            if (factor.is_numeral()) coefficient=coefficient*factor;
            else variables.push_back(factor);
        }
        bool exact=true;
        for (const auto& divisor:divisors) {
            if (divisor.is_numeral()) { denominator=denominator*divisor; continue; }
            auto found=std::find_if(variables.begin(),variables.end(),
                [&](const z3::expr& factor) { return z3::eq(factor,divisor); });
            if (found==variables.end()) { exact=false; break; }
            variables.erase(found);
        }
        coefficient=coefficient.simplify();denominator=denominator.simplify();
        if (exact && !z3::eq(denominator,ctx.int_val(0)) &&
            ((constant_stride && variables.empty()) ||
             z3::eq((coefficient%denominator).simplify(),ctx.int_val(0)))) {
            auto part=(coefficient/denominator).simplify();
            for (const auto& factor:variables) part=part*factor;
            quotient=quotient+part;
            if (constant_stride) remainder=remainder+term-part*stride;
        } else remainder=remainder+term;
    }
    return {quotient.simplify(),remainder.simplify()};
}
}

z3::expr_vector recover_array_shape(llvm::AllocaInst* allocation,
                                    const state_ptr& state, const z3::expr& count) {
    auto& ctx=state->z3ctx;
    z3::expr_vector empty(ctx);
    if (!state->session.uses_integer_relaxation() ||
        !allocation->getAllocatedType()->isIntegerTy()) return empty;
    for (auto* declaration:llvm::findDVRDeclares(allocation)) {
        if (declaration->getExpression()->getNumElements()!=0) continue;
        auto* type=llvm::dyn_cast_or_null<llvm::DICompositeType>(declaration->getVariable()->getType());
        if (!type || type->getTag()!=llvm::dwarf::DW_TAG_array_type ||
            type->getElements().size()<2) continue;
        z3::expr_vector dimensions(ctx);
        auto product=ctx.int_val(1), valid=ctx.bool_val(true);
        for (auto* element:type->getElements()) {
            auto* range=llvm::dyn_cast<llvm::DISubrange>(element);
            if (!range) { dimensions.resize(0); break; }
            llvm::Value* value=nullptr;
            auto bound=range->getCount();
            if (auto* constant=bound.dyn_cast<llvm::ConstantInt*>()) value=constant;
            else if (auto* variable=bound.dyn_cast<llvm::DIVariable*>()) {
                auto* local=llvm::dyn_cast<llvm::DILocalVariable>(variable);
                if (local) value=dimension_value(local,allocation,state);
            }
            if (!value) { dimensions.resize(0); break; }
            const auto dimension=state->evaluate(value,false);
            dimensions.push_back(dimension.as_expr().simplify());
            product=product*dimensions[dimensions.size()-1];
            valid=valid&&dimension.defined()&&dimensions[dimensions.size()-1]>0;
        }
        if (dimensions.size()<2) continue;
        z3::solver certificate(ctx);
        certificate.set("timeout",200u);
        certificate.add(state->get_path_condition().as_expr());
        certificate.add(!(valid&&product==count));
        if (certificate.check()!=z3::unsat) continue;
        spdlog::info("[array-shape] certified {}-dimensional VLA",dimensions.size());
        return dimensions;
    }
    return empty;
}

std::vector<Expression> add_coordinate_offset(
    const MemoryObject& array, const std::vector<Expression>& base,
    const Expression& offset) {
    auto result=base;
    const auto sizes=array.get_sizes();
    auto remaining=offset.as_expr().simplify();
    auto defined_coordinate=[&](const z3::expr& value) {
        z3::expr_vector conditions(value.ctx()),values(value.ctx()),definitions(value.ctx());
        conditions.push_back(value.ctx().bool_val(true));values.push_back(value);
        definitions.push_back(offset.defined());
        return Expression(conditions,values,definitions);
    };
    for (unsigned d=0;d+1<sizes.size();++d) {
        auto stride=remaining.ctx().int_val(1);
        for (unsigned j=d+1;j<sizes.size();++j) stride=stride*sizes[j].as_expr();
        auto [coordinate,remainder]=split_multiple(remaining,stride.simplify());
        result[d]=result[d]+defined_coordinate(coordinate);
        remaining=remainder;
    }
    result.back()=result.back()+defined_coordinate(remaining);
    return result;
}

std::vector<Expression> normalize_array_coordinates(
    const MemoryObject& array, const std::vector<Expression>& indices,
    const state_ptr& state) {
    if (!array.has_native_vla_shape() || indices.empty()) return indices;
    const auto sizes=array.get_sizes();
    if (indices.size()!=sizes.size()) {
        certify_array_coordinates(array,indices,state);
        return indices;
    }
    auto result=indices;
    auto& ctx=state->z3ctx;
    z3::solver checker(ctx);
    checker.set("timeout",100u);
    checker.add(state->get_path_condition().as_expr());
    auto proves=[&](const z3::expr& condition) {
        checker.push();checker.add(!condition);
        const auto proof=checker.check();checker.pop();
        return proof==z3::unsat;
    };
    for (unsigned d=result.size()-1;d>0;--d) {
        const auto value=result[d].as_expr(),width=sizes[d].as_expr();
        if (proves(value>=0&&value<width)) continue;
        bool adjusted=false;
        for (int carry:{-1,1}) {
            const auto candidate=value-carry*width;
            if (!proves(candidate>=0&&candidate<width)) continue;
            result[d]=result[d]-Expression(ctx.int_val(carry))*sizes[d];
            result[d-1]=result[d-1]+Expression(ctx.int_val(carry));
            adjusted=true;
            break;
        }
        if (adjusted) continue;
        // A carry may depend on whether the offset crosses a row boundary.
        // Certify its whole domain before introducing a piecewise coordinate.
        if (!proves(width>0&&value>=-width&&value<2*width)) continue;
        const auto carry=z3::ite(value<0,ctx.int_val(-1),
            z3::ite(value>=width,ctx.int_val(1),ctx.int_val(0)));
        z3::expr_vector conditions(ctx),values(ctx),definitions(ctx);
        conditions.push_back(ctx.bool_val(true));values.push_back(carry);
        definitions.push_back(result[d].defined());
        const Expression step(conditions,values,definitions);
        result[d]=result[d]-step*sizes[d];
        result[d-1]=result[d-1]+step;
    }
    certify_array_coordinates(array,result,state);
    return result;
}

void certify_array_coordinates(const MemoryObject& array,
                               const std::vector<Expression>& indices,
                               const state_ptr& state) {
    if (!array.has_native_vla_shape()) return;
    if (indices.empty()) return;
    const auto sizes=array.get_sizes();
    if (indices.size()!=sizes.size())
        throw VerifierError(VerifierIssueKind::UnsupportedSemantics,"VLA coordinate rank mismatch");
    auto canonical=state->z3ctx.bool_val(true);
    for (unsigned d=1;d<sizes.size();++d)
        canonical=canonical&&indices[d].as_expr()>=0&&indices[d].as_expr()<sizes[d].as_expr();
    z3::solver check(state->z3ctx);
    check.set("timeout",100u);
    check.add(state->get_path_condition().as_expr()&&!canonical);
    if (check.check()!=z3::unsat) {
        spdlog::debug("[array-shape] noncanonical access: {}",check.to_smt2());
        throw VerifierError(VerifierIssueKind::UnsupportedSemantics,
                            "Cannot certify canonical multidimensional VLA coordinates");
    }
}
}
