#include "AInstruction.h"
#include "IntegerSemantics.h"
#include "RelaxedIntegerSemantics.h"
#include "PathAccelerator.h"
#include "VerificationSession.h"
#include <spdlog/spdlog.h>

#include "z3++.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugProgramInstruction.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"

#include <optional>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <unordered_map>

using namespace ari_exe;

namespace {
std::pair<z3::expr, z3::expr> relaxed_integer_binary(
    const z3::expr& lhs, const z3::expr& rhs, unsigned opcode,
    unsigned width, bool exact) {
    using namespace integer_semantics;
    auto& ctx = lhs.ctx();
    const z3::expr left = as_int(lhs), right = as_int(rhs);
    z3::expr defined = ctx.bool_val(true);
    z3::expr value(ctx);
    if (width == 1) {
        const z3::expr a = left != 0, b = right != 0;
        switch (opcode) {
        case llvm::Instruction::Add:
        case llvm::Instruction::Sub:
        case llvm::Instruction::Xor: value = a != b; break;
        case llvm::Instruction::Mul:
        case llvm::Instruction::And: value = a && b; break;
        case llvm::Instruction::Or: value = a || b; break;
        default:
            throw VerifierError(VerifierIssueKind::UnsupportedSemantics,
                                "unsupported Boolean arithmetic in integer relaxation");
        }
        return {value, defined};
    }
    switch (opcode) {
    case llvm::Instruction::Add: value = left + right; break;
    case llvm::Instruction::Sub: value = left - right; break;
    case llvm::Instruction::Mul: value = left * right; break;
    case llvm::Instruction::SDiv:
        value = signed_div(left, right);
        defined = right != 0;
        if (exact) defined = defined && signed_rem(left, right) == 0;
        break;
    case llvm::Instruction::UDiv:
        value = left / right;
        defined = right != 0;
        if (exact) defined = defined && left % right == 0;
        break;
    case llvm::Instruction::SRem:
        value = signed_rem(left, right);
        defined = right != 0;
        break;
    case llvm::Instruction::URem:
        value = left % right;
        defined = right != 0;
        break;
    case llvm::Instruction::And:
    case llvm::Instruction::Or:
    case llvm::Instruction::Xor: {
        const auto operation = opcode == llvm::Instruction::And
                                   ? IntegerBitwiseOperation::And
                               : opcode == llvm::Instruction::Or
                                   ? IntegerBitwiseOperation::Or
                                   : IntegerBitwiseOperation::Xor;
        auto result = integer_bitwise(left, right, operation);
        if (!result) {
            throw VerifierError(VerifierIssueKind::UnsupportedSemantics,
                                "integer relaxation needs a constant bitwise mask "
                                "(no bitvector fallback)");
        }
        value = *result;
        break;
    }
    case llvm::Instruction::Shl:
    case llvm::Instruction::LShr:
    case llvm::Instruction::AShr: {
        auto factor = integer_shift_factor(right);
        if (!factor) {
            throw VerifierError(VerifierIssueKind::UnsupportedSemantics,
                                "integer relaxation needs a constant shift amount "
                                "between 0 and 4096 (no bitvector fallback)");
        }
        defined = right >= 0;
        value = opcode == llvm::Instruction::Shl
                    ? left * *factor : left / *factor;
        if (exact && opcode != llvm::Instruction::Shl) {
            defined = defined && left % *factor == 0;
        }
        break;
    }
    default:
        throw VerifierError(VerifierIssueKind::UnsupportedSemantics,
                            "unsupported operation in integer relaxation");
    }
    return {value.simplify(), defined.simplify()};
}

std::size_t positive_environment_size(const char* name,
                                      std::size_t default_value) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') return default_value;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (!end || *end != '\0' || parsed == 0) return default_value;
    return static_cast<std::size_t>(parsed);
}

bool force_path_expression_mode() {
    const char* value = std::getenv("ARITHEXE_FORCE_PATH_EXPRESSIONS");
    return value && std::string(value) == "1";
}

bool is_first_header_phi(const llvm::PHINode* phi, const llvm::Loop* loop) {
    const llvm::BasicBlock* header = loop ? loop->getHeader() : nullptr;
    return header && !header->phis().empty() &&
           phi == &*header->phis().begin();
}

std::string render_exact_prefix(const LoopPathExpressionState& history) {
    std::ostringstream out;
    bool first = true;
    for (const ExactPathPrefixSegment& segment : history.prefix) {
        if (!first) out << ' ';
        if (segment.symbolic_power) {
            out << '(' << render_path_word(segment.symbolic_power->root)
                << ")^(" << segment.symbolic_power->exponent.to_string()
                << ')';
        } else {
            out << render_path_word(segment.explicit_word);
        }
        first = false;
    }
    return out.str();
}

std::optional<PathSymbol>
record_completed_loop_path(const state_ptr& state, llvm::Loop* loop,
                           const llvm::PHINode* phi) {
    if (!loop || !is_first_header_phi(phi, loop)) {
        return std::nullopt;
    }
    auto& history = state->loop_path_expressions[loop];
    if (!state->trace.empty() && !loop->contains(state->trace.back())) {
        // A nested loop's next invocation is a new trace, not another
        // iteration of its previous invocation.
        history = LoopPathExpressionState{};
    }
    if (!history.decision_cursor_initialized) {
        history.decision_cursor = state->path_decisions.size();
        history.decision_cursor_initialized = true;
    }
    if (state->trace.empty()) return std::nullopt;
    llvm::BasicBlock* predecessor = state->trace.back();
    // LLVM's latch query requires an in-loop block; without that check the
    // preheader can look like a backedge and create a fictitious iteration.
    if (!loop->contains(predecessor) ||
        !loop->isLoopLatch(predecessor)) return std::nullopt;

    if (history.decision_cursor > state->path_decisions.size()) {
        return std::nullopt;
    }
    std::vector<PathDecisionEvent> decisions(
        state->path_decisions.begin() + history.decision_cursor,
        state->path_decisions.end());
    history.decision_cursor = state->path_decisions.size();
    if (decisions.empty()) {
        // A branch-free iteration still has one precise path.
        decisions.push_back(
            {reinterpret_cast<std::uintptr_t>(loop->getHeader()), 0});
    }

    const PathSymbol symbol =
        state->session.intern_loop_path(loop, decisions);
    history.append(symbol);
    state->session.note_path_compression();
    history.candidates = compress_path_prefix(
        history.concrete_frontier(),
        positive_environment_size("ARITHEXE_PATH_MAX_ROOT", 4),
        positive_environment_size("ARITHEXE_PATH_BEAM", 4),
        positive_environment_size("ARITHEXE_PATH_EVIDENCE", 2));
    const std::string loop_name = loop->getHeader()->getName().str();
    spdlog::debug(
        "[path-expr] loop {} completed path p{} ({} decisions); exact "
        "prefix: {}",
        loop_name, symbol, decisions.size(), render_exact_prefix(history));
    if (history.candidates.empty()) {
        spdlog::debug(
            "[path-expr] loop {} compression produced no suffix-star "
            "candidate",
            loop_name);
    } else {
        for (std::size_t i = 0; i < history.candidates.size(); ++i) {
            const PathSchemaCandidate& candidate = history.candidates[i];
            spdlog::debug(
                "[path-expr] loop {} candidate #{}: {} "
                "(observed repetitions={}, score={})",
                loop_name, i + 1, render_path_schema(candidate),
                candidate.observed_star_exponent, candidate.score);
        }
    }
    return symbol;
}

const llvm::Function* resolve_called_function(const llvm::CallBase* call) {
    if (const llvm::Function* function = call->getCalledFunction()) {
        return function;
    }
    return llvm::dyn_cast<llvm::Function>(
        call->getCalledOperand()->stripPointerCasts());
}

llvm::Function* resolve_called_function(llvm::CallBase* call) {
    return const_cast<llvm::Function*>(
        resolve_called_function(static_cast<const llvm::CallBase*>(call)));
}

state_ptr clone_state_preserving_type(const state_ptr& state) {
    if (auto rec_state = std::dynamic_pointer_cast<RecState>(state)) {
        return std::make_shared<RecState>(*rec_state);
    }
    if (auto loop_state = std::dynamic_pointer_cast<LoopState>(state)) {
        return std::make_shared<LoopState>(*loop_state);
    }
    return std::make_shared<State>(*state);
}

using IntegerBinaryOperation = std::function<std::pair<z3::expr, z3::expr>(
    const z3::expr&, const z3::expr&)>;

Expression map_integer_binary(const Expression& lhs, const Expression& rhs,
                              const IntegerBinaryOperation& operation) {
    const auto lhs_conditions = lhs.get_conditions();
    const auto rhs_conditions = rhs.get_conditions();
    const auto lhs_expressions = lhs.get_expressions();
    const auto rhs_expressions = rhs.get_expressions();
    const auto lhs_definitions = lhs.get_definitions();
    const auto rhs_definitions = rhs.get_definitions();
    z3::expr_vector conditions(lhs.ctx());
    z3::expr_vector expressions(lhs.ctx());
    z3::expr_vector definitions(lhs.ctx());
    const std::vector<int> sizes = {
        static_cast<int>(lhs_conditions.size()),
        static_cast<int>(rhs_conditions.size())};

    for (const auto& indices : cartesian_product(sizes)) {
        const unsigned lhs_index = indices[0];
        const unsigned rhs_index = indices[1];
        z3::expr condition = lhs_conditions[lhs_index] &&
                             rhs_conditions[rhs_index];
        if (!is_feasible(condition)) continue;
        auto [value, operation_defined] = operation(
            lhs_expressions[lhs_index], rhs_expressions[rhs_index]);
        conditions.push_back(condition);
        expressions.push_back(value);
        definitions.push_back(lhs_definitions[lhs_index] &&
                              rhs_definitions[rhs_index] &&
                              operation_defined);
    }
    return Expression(conditions, expressions, definitions);
}

using IntegerUnaryOperation =
    std::function<z3::expr(const z3::expr&)>;

Expression map_integer_unary(const Expression& operand,
                             const IntegerUnaryOperation& operation) {
    const auto conditions = operand.get_conditions();
    const auto expressions = operand.get_expressions();
    const auto definitions = operand.get_definitions();
    z3::expr_vector mapped(operand.ctx());
    for (const z3::expr& expression : expressions) {
        mapped.push_back(operation(expression));
    }
    return Expression(conditions, mapped, definitions);
}

Expression canonicalize_signed(const Expression& operand, unsigned width,
                               const z3::expr& assumptions) {
    if (width == 1 ||
        VerificationSession::current().ignores_integer_width(width)) {
        return operand;
    }

    const auto conditions = operand.get_conditions();
    const auto expressions = operand.get_expressions();
    const auto definitions = operand.get_definitions();
    z3::expr_vector canonical(operand.ctx());
    for (unsigned i = 0; i < expressions.size(); ++i) {
        z3::expr integer = integer_semantics::as_int(expressions[i]);
        z3::expr known = assumptions && conditions[i] && definitions[i];
        canonical.push_back(
            ari_exe::implies(
                known, integer_semantics::in_signed_range(integer, width))
                ? integer
                : integer_semantics::as_signed(integer, width));
    }
    return Expression(conditions, canonical, definitions);
}

bool has_canonical_signed_representation(const llvm::Value* value) {
    enum class VisitState { Visiting, Canonical, NonCanonical };
    std::unordered_map<const llvm::Value*, VisitState> visited;
    const auto underlying_pointer = [](const llvm::Value* pointer) {
        const llvm::Value* current = pointer->stripPointerCasts();
        while (const auto* gep =
                   llvm::dyn_cast<llvm::GetElementPtrInst>(current)) {
            current = gep->getPointerOperand()->stripPointerCasts();
        }
        return current;
    };
    std::function<bool(const llvm::Value*)> visit =
        [&](const llvm::Value* current) {
            const auto found = visited.find(current);
            if (found != visited.end()) {
                return found->second != VisitState::NonCanonical;
            }
            visited.emplace(current, VisitState::Visiting);

            bool canonical =
                llvm::isa<llvm::ConstantInt>(current) ||
                llvm::isa<llvm::UndefValue>(current) ||
                llvm::isa<llvm::Argument>(current) ||
                llvm::isa<llvm::SExtInst>(current) ||
                llvm::isa<llvm::ZExtInst>(current) ||
                llvm::isa<llvm::TruncInst>(current) ||
                llvm::isa<llvm::ICmpInst>(current);
            if (const auto* call = llvm::dyn_cast<llvm::CallBase>(current)) {
                canonical = call->getType()->isIntegerTy();
            } else if (const auto* binary =
                           llvm::dyn_cast<llvm::BinaryOperator>(current)) {
                switch (binary->getOpcode()) {
                case llvm::Instruction::SDiv:
                case llvm::Instruction::SRem:
                case llvm::Instruction::And:
                case llvm::Instruction::Or:
                case llvm::Instruction::Xor:
                case llvm::Instruction::Shl:
                case llvm::Instruction::LShr:
                case llvm::Instruction::AShr:
                    canonical = true;
                    break;
                default:
                    canonical = binary->hasNoSignedWrap();
                    break;
                }
            } else if (const auto* phi = llvm::dyn_cast<llvm::PHINode>(current)) {
                canonical = true;
                for (const llvm::Value* incoming : phi->incoming_values()) {
                    canonical = canonical && visit(incoming);
                }
            } else if (const auto* load =
                           llvm::dyn_cast<llvm::LoadInst>(current)) {
                const llvm::Value* pointer =
                    underlying_pointer(load->getPointerOperand());
                canonical = false;
                bool found_store = false;
                bool all_stores_canonical = true;
                for (const llvm::BasicBlock& block :
                     *load->getFunction()) {
                    for (const llvm::Instruction& instruction : block) {
                        const auto* store =
                            llvm::dyn_cast<llvm::StoreInst>(&instruction);
                        if (!store ||
                            underlying_pointer(store->getPointerOperand()) !=
                                pointer) {
                            continue;
                        }
                        found_store = true;
                        all_stores_canonical =
                            all_stores_canonical &&
                            visit(store->getValueOperand());
                    }
                    if (!all_stores_canonical) {
                        continue;
                    }
                }
                canonical = found_store && all_stores_canonical;
            } else if (const auto* select =
                           llvm::dyn_cast<llvm::SelectInst>(current)) {
                canonical = visit(select->getTrueValue()) &&
                            visit(select->getFalseValue());
            }

            visited[current] = canonical ? VisitState::Canonical
                                         : VisitState::NonCanonical;
            return canonical;
        };
    return visit(value);
}

z3::expr signed_value(const z3::expr& value, const llvm::Value* llvm_value,
                      unsigned width, const state_ptr& state) {
    z3::expr integer = integer_semantics::as_int(value);
    if (state->session.ignores_integer_width(width) ||
        has_canonical_signed_representation(llvm_value) ||
        ari_exe::implies(state->get_path_condition().as_expr(),
                         integer_semantics::in_signed_range(integer,
                                                            width))) {
        return integer;
    }
    return integer_semantics::as_signed(integer, width);
}

z3::expr unsigned_value(const z3::expr& value, unsigned width,
                        const state_ptr& state) {
    z3::expr integer = integer_semantics::as_int(value);
    if (state->session.ignores_integer_width(width)) return integer;
    return ari_exe::implies(
               state->get_path_condition().as_expr(),
               integer_semantics::in_unsigned_range(integer, width))
               ? integer
               : integer_semantics::as_unsigned(integer, width);
}

std::string source_type_name(const llvm::DIType* type) {
    const llvm::DIType* current = type;
    while (current) {
        if (const auto* basic = llvm::dyn_cast<llvm::DIBasicType>(current)) {
            const uint64_t width = basic->getSizeInBits();
            switch (basic->getEncoding()) {
            case llvm::dwarf::DW_ATE_boolean:
                return "_Bool";
            case llvm::dwarf::DW_ATE_unsigned:
            case llvm::dwarf::DW_ATE_unsigned_char:
                if (width <= 8) return "unsigned char";
                if (width <= 16) return "unsigned short";
                if (width <= 32) return "unsigned int";
                return "unsigned long long";
            case llvm::dwarf::DW_ATE_signed:
            case llvm::dwarf::DW_ATE_signed_char:
                if (width <= 8) return "signed char";
                if (width <= 16) return "short";
                if (width <= 32) return "int";
                return "long long";
            default:
                return "int";
            }
        }
        const auto* derived = llvm::dyn_cast<llvm::DIDerivedType>(current);
        current = derived ? derived->getBaseType() : nullptr;
    }
    return "int";
}

std::optional<std::pair<std::string, std::string>>
source_variable(llvm::Value* value) {
    for (llvm::DbgVariableRecord* record : llvm::findDVRValues(value)) {
        llvm::DILocalVariable* variable = record->getVariable();
        if (!variable || variable->getName().empty()) continue;
        return std::pair{variable->getName().str(),
                         source_type_name(variable->getType())};
    }
    return std::nullopt;
}
} // namespace

std::string llvm_value_to_string(const llvm::Value& value) {
    std::string text;
    llvm::raw_string_ostream stream(text);
    value.print(stream);
    return stream.str();
}

AInstruction*
AInstruction::create(llvm::Instruction* inst) {
    auto& cached_instructions =
        VerificationSession::current().instructions();
    if (cached_instructions.find(inst) != cached_instructions.end()) {
        return cached_instructions.at(inst).get();
    }

    AInstruction* res = nullptr;
    if (inst->isDebugOrPseudoInst()) {
        res = new AInstructionDebug(inst);
    } else if (auto bin_inst = llvm::dyn_cast_or_null<llvm::BinaryOperator>(inst)) {
        res = new AInstructionBinary(bin_inst);
    } else if (auto cmp_inst = llvm::dyn_cast_or_null<llvm::ICmpInst>(inst)) {
        res = new AInstructionICmp(cmp_inst);
    } else if (auto call_inst = llvm::dyn_cast_or_null<llvm::CallInst>(inst)) {
        res = new AInstructionCall(call_inst);
    } else if (auto branch_inst = llvm::dyn_cast_or_null<llvm::BranchInst>(inst)) {
        res = new AInstructionBranch(branch_inst);
    } else if (auto ret_inst = llvm::dyn_cast_or_null<llvm::ReturnInst>(inst)) {
        // return instruction, do nothing
        res = new AInstructionReturn(inst);
    } else if (auto zext_inst = llvm::dyn_cast_or_null<llvm::ZExtInst>(inst)) {
        res = new AInstructionZExt(zext_inst);
    } else if (auto sext_inst = llvm::dyn_cast_or_null<llvm::SExtInst>(inst)) {
        res = new AInstructionSExt(sext_inst);
    } else if (auto trunc_inst = llvm::dyn_cast_or_null<llvm::TruncInst>(inst)) {
        res = new AInstructionTrunc(trunc_inst);
    } else if (auto phi = llvm::dyn_cast_or_null<llvm::PHINode>(inst)) {
        res = new AInstructionPhi(phi);
    } else if (auto select = llvm::dyn_cast_or_null<llvm::SelectInst>(inst)) {
        res = new AInstructionSelect(select);
    } else if (auto load_inst = llvm::dyn_cast_or_null<llvm::LoadInst>(inst)) {
        res = new AInstructionLoad(load_inst);
    } else if (auto store_inst = llvm::dyn_cast_or_null<llvm::StoreInst>(inst)) {
        res = new AInstructionStore(store_inst);
    } else if (auto gep_inst = llvm::dyn_cast_or_null<llvm::GetElementPtrInst>(inst)) {
        res = new AInstructionGEP(gep_inst);
    } else if (auto alloca_inst = llvm::dyn_cast_or_null<llvm::AllocaInst>(inst)) {
        res = new AInstructionAlloca(alloca_inst);
    } else {
        assert(false && "Unspported instruction type");
    }
    cached_instructions.insert_or_assign(
        inst, std::shared_ptr<AInstruction>(res));
    return res;
}

loop_state_list
AInstruction::execute(loop_state_ptr state) {
    auto new_states = execute(std::static_pointer_cast<State>(state));
    loop_state_list new_base_states;
    for (auto& new_state : new_states) {
        auto new_loop_state = std::make_shared<LoopState>(LoopState(*new_state));
        new_loop_state->path_condition_in_loop = state->path_condition_in_loop;
        new_loop_state->summarizing_loop = state->summarizing_loop;
        new_loop_state->unknown_call_counters = state->unknown_call_counters;
        new_base_states.push_back(new_loop_state);
    }
    return new_base_states;
}

rec_state_list
AInstruction::execute(rec_state_ptr state) {
    auto new_states = execute(std::static_pointer_cast<State>(state));
    rec_state_list new_base_states;
    for (auto& new_state : new_states) {
        auto new_loop_state = std::make_shared<RecState>(RecState(*new_state));
        new_base_states.push_back(new_loop_state);
    }
    return new_base_states;
}

AInstruction*
AInstruction::get_next_instruction() {
    // auto next_inst = inst->getNextNonDebugInstruction();
    auto next_inst = inst->getNextNode();
    if (next_inst) {
        return create(next_inst);
    }
    return nullptr;
}

state_list
AInstructionBinary::execute(state_ptr state) {
    auto bin_inst = dyn_cast<llvm::BinaryOperator>(inst);
    auto opcode = bin_inst->getOpcode();

    // TODO:
    // Current implementation create new states every time.
    // In future, in-place update should be implemented.
    std::vector<state_ptr> new_states;

    auto op0 = bin_inst->getOperand(0);
    auto op1 = bin_inst->getOperand(1);
    auto op0_value = state->evaluate(op0);
    auto op1_value = state->evaluate(op1);

    auto& z3ctx = op0_value.ctx();
    const unsigned width = bin_inst->getType()->getIntegerBitWidth();
    const bool no_signed_wrap = bin_inst->hasNoSignedWrap();
    const bool no_unsigned_wrap = bin_inst->hasNoUnsignedWrap();
    const bool exact = bin_inst->isExact();

    Expression result = map_integer_binary(
        op0_value, op1_value,
        [&](const z3::expr& lhs, const z3::expr& rhs) {
            using namespace integer_semantics;
            if (state->session.uses_integer_relaxation()) {
                return relaxed_integer_binary(lhs, rhs, opcode, width, exact);
            }
            z3::expr lhs_bv(z3ctx);
            z3::expr rhs_bv(z3ctx);
            bool bitvectors_ready = false;
            const auto prepare_bitvectors = [&]() {
                if (bitvectors_ready) return;
                lhs_bv = to_bv(lhs, width);
                rhs_bv = to_bv(rhs, width);
                bitvectors_ready = true;
            };
            z3::expr defined = z3ctx.bool_val(true);
            z3::expr result_bv(z3ctx);
            z3::expr value(z3ctx);

            if (opcode == llvm::Instruction::Add) {
                if (no_signed_wrap) {
                    z3::expr integer_result =
                        signed_value(lhs, op0, width, state) +
                        signed_value(rhs, op1, width, state);
                    defined = defined && in_signed_range(integer_result, width);
                    if (width == 1) {
                        prepare_bitvectors();
                        value = from_bv(lhs_bv + rhs_bv, width);
                    } else {
                        value = integer_result;
                    }
                } else {
                    value = as_int(lhs) + as_int(rhs);
                }
                if (no_unsigned_wrap) {
                    defined = defined &&
                              as_unsigned(lhs, width) +
                                      as_unsigned(rhs, width) <
                                  power_of_two(z3ctx, width);
                }
            } else if (opcode == llvm::Instruction::Sub) {
                if (no_signed_wrap) {
                    z3::expr integer_result =
                        signed_value(lhs, op0, width, state) -
                        signed_value(rhs, op1, width, state);
                    defined = defined && in_signed_range(integer_result, width);
                    if (width == 1) {
                        prepare_bitvectors();
                        value = from_bv(lhs_bv - rhs_bv, width);
                    } else {
                        value = integer_result;
                    }
                } else {
                    value = as_int(lhs) - as_int(rhs);
                }
                if (no_unsigned_wrap) {
                    defined = defined &&
                              as_unsigned(lhs, width) >=
                                  as_unsigned(rhs, width);
                }
            } else if (opcode == llvm::Instruction::Mul) {
                if (no_signed_wrap) {
                    z3::expr integer_result =
                        signed_value(lhs, op0, width, state) *
                        signed_value(rhs, op1, width, state);
                    defined = defined && in_signed_range(integer_result, width);
                    if (width == 1) {
                        prepare_bitvectors();
                        value = from_bv(lhs_bv * rhs_bv, width);
                    } else {
                        value = integer_result;
                    }
                } else {
                    value = as_int(lhs) * as_int(rhs);
                }
                if (no_unsigned_wrap) {
                    defined = defined &&
                              as_unsigned(lhs, width) *
                                      as_unsigned(rhs, width) <
                                  power_of_two(z3ctx, width);
                }
            } else if (opcode == llvm::Instruction::SDiv) {
                z3::expr integer_lhs = signed_value(lhs, op0, width, state);
                z3::expr integer_rhs = signed_value(rhs, op1, width, state);
                const bool constant_divisor =
                    integer_rhs.simplify().is_numeral();
                defined = integer_rhs != 0 &&
                          !(integer_lhs == signed_min(z3ctx, width) &&
                            integer_rhs == -1);
                if (!state->is_summarizing() && constant_divisor) {
                    const std::string suffix =
                        std::to_string(state->session.next_call_value_id(inst));
                    z3::expr quotient = z3ctx.int_const(
                        ("ari_sdiv_q_" + suffix).c_str());
                    z3::expr remainder = z3ctx.int_const(
                        ("ari_sdiv_r_" + suffix).c_str());
                    z3::expr abs_rhs =
                        z3::ite(integer_rhs < 0, -integer_rhs, integer_rhs)
                            .simplify();
                    defined =
                        defined &&
                        integer_lhs == quotient * integer_rhs + remainder &&
                        -abs_rhs < remainder && remainder < abs_rhs &&
                        (remainder == 0 ||
                         (integer_lhs < 0 && remainder < 0) ||
                         (integer_lhs > 0 && remainder > 0));
                    if (exact) defined = defined && remainder == 0;
                    value = quotient;
                } else if (state->is_summarizing()) {
                    value = signed_div(integer_lhs, integer_rhs);
                    if (exact) {
                        defined = defined &&
                                  signed_rem(integer_lhs, integer_rhs) == 0;
                    }
                } else {
                    prepare_bitvectors();
                    value = from_bv(lhs_bv / rhs_bv, width);
                    if (exact) {
                        defined = defined &&
                                  z3::srem(lhs_bv, rhs_bv) ==
                                      z3ctx.bv_val(0, width);
                    }
                }
            } else if (opcode == llvm::Instruction::UDiv) {
                z3::expr integer_lhs = as_unsigned(lhs, width);
                z3::expr integer_rhs = as_unsigned(rhs, width);
                defined = integer_rhs != 0;
                if (exact) {
                    defined = defined &&
                              integer_lhs % integer_rhs == 0;
                }
                value = integer_lhs / integer_rhs;
            } else if (opcode == llvm::Instruction::SRem) {
                z3::expr integer_lhs = signed_value(lhs, op0, width, state);
                z3::expr integer_rhs = signed_value(rhs, op1, width, state);
                defined = integer_rhs != 0;
                if (!state->is_summarizing() &&
                    integer_rhs.simplify().is_numeral()) {
                    const std::string suffix =
                        std::to_string(state->session.next_call_value_id(inst));
                    z3::expr quotient = z3ctx.int_const(
                        ("ari_srem_q_" + suffix).c_str());
                    z3::expr remainder = z3ctx.int_const(
                        ("ari_srem_r_" + suffix).c_str());
                    z3::expr abs_rhs =
                        z3::ite(integer_rhs < 0, -integer_rhs, integer_rhs)
                            .simplify();
                    defined =
                        defined &&
                        integer_lhs == quotient * integer_rhs + remainder &&
                        -abs_rhs < remainder && remainder < abs_rhs &&
                        (remainder == 0 ||
                         (integer_lhs < 0 && remainder < 0) ||
                         (integer_lhs > 0 && remainder > 0));
                    value = remainder;
                } else if (state->is_summarizing()) {
                    value = signed_rem(integer_lhs, integer_rhs);
                } else {
                    prepare_bitvectors();
                    value = from_bv(z3::srem(lhs_bv, rhs_bv), width);
                }
            } else if (opcode == llvm::Instruction::URem) {
                z3::expr integer_lhs = as_unsigned(lhs, width);
                z3::expr integer_rhs = as_unsigned(rhs, width);
                defined = integer_rhs != 0;
                value = integer_lhs % integer_rhs;
            } else if (opcode == llvm::Instruction::And) {
                prepare_bitvectors();
                value = from_bv(lhs_bv & rhs_bv, width);
            } else if (opcode == llvm::Instruction::Or) {
                prepare_bitvectors();
                value = from_bv(lhs_bv | rhs_bv, width);
            } else if (opcode == llvm::Instruction::Xor) {
                prepare_bitvectors();
                value = from_bv(lhs_bv ^ rhs_bv, width);
            } else if (opcode == llvm::Instruction::Shl ||
                       opcode == llvm::Instruction::LShr ||
                       opcode == llvm::Instruction::AShr) {
                z3::expr shift = as_unsigned(rhs, width);
                defined = shift < z3ctx.int_val(width);
                prepare_bitvectors();
                if (opcode == llvm::Instruction::Shl) {
                    result_bv = z3::shl(lhs_bv, rhs_bv);
                    if (no_unsigned_wrap) {
                        defined = defined &&
                                  z3::lshr(result_bv, rhs_bv) == lhs_bv;
                    }
                    if (no_signed_wrap) {
                        defined = defined &&
                                  z3::ashr(result_bv, rhs_bv) == lhs_bv;
                    }
                } else if (opcode == llvm::Instruction::LShr) {
                    result_bv = z3::lshr(lhs_bv, rhs_bv);
                } else {
                    result_bv = z3::ashr(lhs_bv, rhs_bv);
                }
                if (exact) {
                    defined = defined &&
                              z3::shl(result_bv, rhs_bv) == lhs_bv;
                }
                value = from_bv(result_bv, width);
            } else {
                throw std::runtime_error(
                    "Unsupported binary operation " +
                    std::string(bin_inst->getOpcodeName()) + ": " +
                    llvm_value_to_string(*bin_inst));
            }
            return std::pair<z3::expr, z3::expr>{value, defined};
        });

    state_ptr new_state = clone_state_preserving_type(state);
    new_state->memory.put_temp(inst, result);
    new_state->step_pc();

    return {new_state};
}

std::vector<state_ptr>
AInstructionICmp::execute(state_ptr state) {
    auto cmp_inst = dyn_cast<llvm::ICmpInst>(inst);
    auto pred = cmp_inst->getPredicate();

    auto op0 = cmp_inst->getOperand(0);
    auto op1 = cmp_inst->getOperand(1);
    const bool signed_constants = !cmp_inst->isUnsigned();
    auto op0_value = state->evaluate(op0, signed_constants);
    auto op1_value = state->evaluate(op1, signed_constants);
    auto& z3ctx = op0_value.ctx();
    const unsigned width = op0->getType()->getIntegerBitWidth();
    Expression result = map_integer_binary(
        op0_value, op1_value,
        [&](const z3::expr& lhs, const z3::expr& rhs) {
            using namespace integer_semantics;
            z3::expr value(z3ctx);
            if (pred == llvm::ICmpInst::ICMP_EQ) {
                value = state->session.ignores_integer_width(width) ||
                                (has_canonical_signed_representation(op0) &&
                                 has_canonical_signed_representation(op1))
                            ? lhs == rhs
                        : lhs.is_int() && rhs.is_int()
                            ? (as_int(lhs) - as_int(rhs)) %
                                      power_of_two(z3ctx, width) ==
                                  0
                            : lhs == rhs;
            } else if (pred == llvm::ICmpInst::ICMP_NE) {
                value = state->session.ignores_integer_width(width) ||
                                (has_canonical_signed_representation(op0) &&
                                 has_canonical_signed_representation(op1))
                            ? lhs != rhs
                        : lhs.is_int() && rhs.is_int()
                            ? (as_int(lhs) - as_int(rhs)) %
                                      power_of_two(z3ctx, width) !=
                                  0
                            : lhs != rhs;
            } else if (pred == llvm::ICmpInst::ICMP_SLT) {
                value = signed_value(lhs, op0, width, state) <
                        signed_value(rhs, op1, width, state);
            } else if (pred == llvm::ICmpInst::ICMP_SLE) {
                value = signed_value(lhs, op0, width, state) <=
                        signed_value(rhs, op1, width, state);
            } else if (pred == llvm::ICmpInst::ICMP_SGT) {
                value = signed_value(lhs, op0, width, state) >
                        signed_value(rhs, op1, width, state);
            } else if (pred == llvm::ICmpInst::ICMP_SGE) {
                value = signed_value(lhs, op0, width, state) >=
                        signed_value(rhs, op1, width, state);
            } else if (pred == llvm::ICmpInst::ICMP_ULT) {
                value = unsigned_value(lhs, width, state) <
                        unsigned_value(rhs, width, state);
            } else if (pred == llvm::ICmpInst::ICMP_ULE) {
                value = unsigned_value(lhs, width, state) <=
                        unsigned_value(rhs, width, state);
            } else if (pred == llvm::ICmpInst::ICMP_UGT) {
                value = unsigned_value(lhs, width, state) >
                        unsigned_value(rhs, width, state);
            } else if (pred == llvm::ICmpInst::ICMP_UGE) {
                value = unsigned_value(lhs, width, state) >=
                        unsigned_value(rhs, width, state);
            } else {
                throw std::runtime_error("Unsupported ICmp predicate");
            }
            return std::pair<z3::expr, z3::expr>{
                value, z3ctx.bool_val(true)};
        });
    state_ptr new_state = std::make_shared<State>(*state);
    new_state->memory.put_temp(inst, result);
    new_state->step_pc();
    return {new_state};
}

std::vector<state_ptr>
AInstructionAlloca::execute(state_ptr state) {
    auto alloca_inst = dyn_cast<llvm::AllocaInst>(inst);
    auto& z3ctx = state->z3ctx;

    assert(!alloca_inst->isArrayAllocation() && "Array allocation is not supported yet");
    // Allocate memory for the alloca instruction
    // auto size = alloca_inst->getAllocatedType()->getPrimitiveSizeInBits() / 8;
    auto size = 1;
    auto ty = alloca_inst->getAllocatedType();
    if (ty->isArrayTy()) {
        auto size_value = alloca_inst->getArraySize();
        size = ty->getArrayNumElements();
    }
    
    state_ptr new_state = std::make_shared<State>(*state);
    z3::expr_vector sizes(z3ctx);
    sizes.push_back(z3ctx.int_val(size));
    new_state->memory.allocate(inst, sizes);
    new_state->step_pc();
    return {new_state};
}

std::vector<state_ptr>
AInstructionCall::execute(state_ptr state) {
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = resolve_called_function(call_inst);

    state_ptr call_state = state;
    bool consumed_noundef = false;
    for (unsigned i = 0; i < call_inst->arg_size(); ++i) {
        llvm::Value* argument = call_inst->getArgOperand(i);
        if (!argument->getType()->isIntegerTy() ||
            !call_inst->paramHasAttr(i, llvm::Attribute::NoUndef)) {
            continue;
        }
        if (call_state == state) {
            call_state = clone_state_preserving_type(state);
        }
        consumed_noundef = true;
        call_state->append_path_condition(
            Expression(state->evaluate(argument).defined()));
    }
    if (consumed_noundef &&
        !is_feasible(call_state->get_path_condition().as_expr())) {
        return {};
    }

    auto& z3ctx = call_state->z3ctx;

    z3::expr result(z3ctx);

    if (called_func && called_func->getName().ends_with("assert")) {
        // verification. should check if the condition is true
        return {execute_assert(call_state)};
    } else if (called_func &&
               (called_func->getName().find("reach_error") != std::string::npos ||
                called_func->getName() == "__VERIFIER_error")) {
        return {execute_reach_error(call_state)};
    } else if (called_func && called_func->getName().find("assume") != std::string::npos) {
        // assume function, add the condition to the path condition
        return {execute_assume(call_state)};
    } else if (called_func && called_func->getName().find("malloc") != std::string::npos) {
        // malloc function, allocate memory and return the pointer
        return {execute_malloc(call_state)};
    } else if (called_func && called_func->hasExactDefinition()) {
        return {execute_normal(call_state)};
    } else if (called_func && called_func->getName().find("llvm.stacksave.p0") != std::string::npos) {
        // handle llvm.stacksave.p0
        auto dummy_ptr = z3ctx.int_val(0); // or create a symbolic pointer
        call_state->memory.put_temp(call_inst, dummy_ptr);
        call_state->step_pc();
        return {call_state};
    } else if (called_func && called_func->getName().find("llvm.stackrestore.p0") != std::string::npos) {
        // handle llvm.stackrestore.p0
        call_state->step_pc();
        return {call_state};
    } else if (called_func && called_func->getName().find("llvm.memcpy") != std::string::npos) {
        return {execute_memcpy(call_state)};
    } else {
        return {execute_unknown(call_state)};
    }
}

state_ptr
AInstructionCall::execute_normal(state_ptr state) {
    auto try_cache = execute_cache(state);
    if (try_cache) return try_cache;

    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = resolve_called_function(call_inst);

    // check if the function is recursive and not yet summarized
    auto ret_type = called_func->getReturnType();
    auto& session = state->session;
    auto& function_summaries = session.function_summaries();
    auto& function_cache = session.function_cache();
    bool is_visited = function_cache.is_visited(called_func);
    const bool recursive = is_recursive(called_func);
    if (!is_visited && !state->is_summarizing() && recursive &&
        !function_summaries.get_value(called_func).has_value()) {
        function_cache.mark_visited(called_func);
        auto summary = summarize_complete(state->z3ctx);
        if (summary.has_value()) {
            function_summaries.insert_or_assign(called_func, *summary);
        }
    }

    auto summary = function_summaries.get_value(called_func);
    auto& z3ctx = state->z3ctx;

    state_ptr new_state = clone_state_preserving_type(state);
    const bool concrete_scalar_call = std::all_of(
        call_inst->arg_begin(), call_inst->arg_end(),
        [&](const llvm::Use& argument) {
            if (argument->getType()->isPointerTy()) return false;
            return state->evaluate(argument.get()).as_expr().simplify().is_numeral();
        });
    if (recursive && !state->is_summarizing() && !summary.has_value() &&
        !concrete_scalar_call) {
        new_state->status = State::UNKNOWN;
        return new_state;
    }
    if (summary.has_value() && !summary->is_over_approximated()) {
        const z3::expr_vector parameters = summary->get_params();
        if (parameters.size() == called_func->arg_size()) {
            std::vector<FunctionParameterCertificate> source_parameters;
            bool source_complete = true;
            for (unsigned index = 0; index < parameters.size(); ++index) {
                const auto source = source_variable(
                    called_func->getArg(index));
                if (!source) {
                    source_complete = false;
                    break;
                }
                source_parameters.emplace_back(source->first,
                                               parameters[index]);
            }
            if (source_complete) {
                const bool already_recorded = std::any_of(
                    new_state->function_certificates.begin(),
                    new_state->function_certificates.end(),
                    [&](const FunctionCertificate& existing) {
                        return existing.function == called_func;
                    });
                if (!already_recorded) {
                    new_state->function_certificates.emplace_back(
                        called_func, std::move(source_parameters),
                        summary->get_summary());
                }
            }
        }
        for (llvm::Instruction& candidate : llvm::instructions(called_func)) {
            auto* nested_call = llvm::dyn_cast<llvm::CallInst>(&candidate);
            llvm::Function* nested_callee =
                nested_call ? resolve_called_function(nested_call) : nullptr;
            if (nested_callee && nested_callee->getName().starts_with(
                                     "__VERIFIER_nondet_")) {
                new_state->counterexample_complete = false;
                break;
            }
        }
        std::vector<Expression> args;
        for (unsigned i = 0; i < call_inst->arg_size(); i++) {
            auto arg = call_inst->getArgOperand(i);
            auto arg_value = state->evaluate(arg);
            args.push_back(arg_value);
        }

        auto function_value = summary->evaluate(args);
        // new_state->memory.allocate(inst, z3::expr_vector(z3ctx));
        new_state->memory.put_temp(inst, function_value);
        new_state->step_pc();
        return new_state;
    }
    if (summary.has_value() && summary->is_over_approximated()) {
        z3::expr_vector unknowns(z3ctx);
        z3::expr_vector initial_values(z3ctx);
        for (int i = 0; i < call_inst->arg_size(); i++) {
            auto arg = call_inst->getArgOperand(i);
            assert(arg->getType()->isPointerTy() && "Over-approximated function should only have pointer arguments");
            auto arg_obj = new_state->memory.get_object_pointed_by(arg);
            auto arg_value = arg_obj->read().as_expr();
            initial_values.push_back(arg_value);
            auto name = arg->getName() + "_unknwon_over_approximated" +
                        std::to_string(session.next_call_value_id(inst));
            auto unknown = z3ctx.int_const(name.str().c_str());
            unknowns.push_back(unknown);
            arg_obj->write(unknown);
        }

        closed_form_ty closed_form;
        auto params = summary->get_params();
        for (auto closed : summary->get_over_approx()) {
            auto lhs = closed.first;
            auto rhs = closed.second;
            lhs = lhs.substitute(params, unknowns);
            rhs = rhs.substitute(params, initial_values).simplify();
            new_state->append_path_condition(lhs == rhs);
        }
        new_state->append_path_condition(summary->get_exit_condition().substitute(params, unknowns));
        new_state->append_path_condition(
            summary->get_iteration_condition().substitute(params,
                                                          initial_values));
        z3::expr relation = summary->get_relational_condition();
        relation = relation.substitute(summary->get_relation_final_params(),
                                       unknowns);
        relation = relation.substitute(
            summary->get_relation_initial_params(), initial_values);
        new_state->append_path_condition(relation);
        new_state->step_pc();
        new_state->is_over_approx = true;
        return new_state;
    }

    z3::expr result(z3ctx);

    auto& frame = new_state->memory.push_frame(called_func);
    frame.prev_pc = state->pc;
    // push the arguments to the stack
    for (unsigned i = 0; i < call_inst->arg_size(); i++) {
        auto arg = call_inst->getArgOperand(i);
        auto param = called_func->getArg(i);
        if (param->getType()->isPointerTy()) {
            auto target_obj = state->memory.get_object(arg);
            // auto addr = new_state->memory.allocate(param, target_obj->get_address());
            new_state->memory.put_temp(param, target_obj->get_ptr_value());
        } else {
            auto arg_value = state->evaluate(arg);
            // new_state->memory.allocate(param, obj->read());
            new_state->memory.put_temp(param, arg_value);
        }
    }
    auto called_first_inst = &*called_func->getEntryBlock().getFirstNonPHIOrDbg();
    auto next_pc = AInstruction::create(called_first_inst);
    new_state->step_pc(next_pc);
    return new_state;
}

state_ptr
AInstructionCall::execute_cache(state_ptr state) {
    auto call_inst = dyn_cast_or_null<llvm::CallInst>(inst);
    assert(call_inst);
    auto called_func = resolve_called_function(call_inst);

    for (auto& arg : call_inst->args()) {
        if (arg->getType()->isPointerTy()) {
            return nullptr;
        }
    }

    auto& cache = state->session.function_cache();
    auto model = state->get_model();
    param_list_ty args;
    for (int i = 0; i < call_inst->arg_size(); i++) {
        auto arg = call_inst->getArgOperand(i);
        auto state_value = state->evaluate(arg);
        auto concrete_value = model.eval(state_value.as_expr(), true);
        if (!state->is_concrete(state_value, concrete_value)) {
            return nullptr;
        }
        args.push_back(concrete_value.as_int64());
    }
    auto cached_value = cache.get_func_value(called_func, args);
    if (cached_value.has_value()) {
        // if the function is cached, return the cached value
        state_ptr new_state = std::make_shared<State>(*state);
        auto& z3ctx = state->z3ctx;
        auto value = z3ctx.int_val(cached_value.value());
        // new_state->memory.allocate(inst, value);
        new_state->memory.put_temp(inst, value);
        new_state->step_pc();
        return new_state;
    }
    return nullptr;
}

state_ptr
AInstructionCall::execute_assert(state_ptr state) {
    // assert function, add the condition to the path condition
    // and check if the condition is true
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = resolve_called_function(call_inst);

    auto& z3ctx = state->z3ctx;

    auto cond = call_inst->getArgOperand(0);
    auto cond_value = state->evaluate(cond);
    state_ptr new_state = clone_state_preserving_type(state);
    new_state->step_pc();
    if (state->is_summarizing()) {
        new_state->summary_invariants.push_back(
            cond_value.as_expr().is_bool()
                ? cond_value
                : cond_value != z3ctx.int_val(0));
        new_state->status = State::RUNNING;
        return new_state;
    }
    new_state->status = State::VERIFYING;
    if (cond_value.as_expr().is_bool()) {
        new_state->verification_condition = cond_value;
    } else {
        new_state->verification_condition = cond_value != z3ctx.int_val(0);
    }
    return new_state;
}

state_ptr
AInstructionCall::execute_reach_error(state_ptr state) {
    // assert function, add the condition to the path condition
    // and check if the condition is true
    state->status = State::REACH_ERROR;
    return state;
}

state_ptr
AInstructionCall::execute_assume(state_ptr state) {
    // assume function, add the condition to the path condition
    // and check if the condition is true
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = resolve_called_function(call_inst);

    auto& z3ctx = state->z3ctx;

    auto cond = call_inst->getArgOperand(0);
    auto cond_value = state->evaluate(cond);

    state_ptr new_state = std::make_shared<State>(*state);
    new_state->append_path_condition(cond_value);
    new_state->step_pc();
    new_state->status = State::TESTING;
    return new_state;
}

state_ptr
AInstructionCall::execute_unknown(state_ptr state) {
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = resolve_called_function(call_inst);

    auto& z3ctx = state->z3ctx;

    Expression result(z3ctx);

    // external function value is unknown, so symbolic
    auto name = "ari_" + inst->getName().str() + "_unknown_" +
                std::to_string(state->session.next_call_value_id(inst));
    auto ret_type = call_inst->getType();

    if (ret_type->isIntegerTy()) {
        if (ret_type->getIntegerBitWidth() == 1) {
            result = z3ctx.bool_const(name.c_str());
        } else {
            result = z3ctx.int_const(name.c_str());
        }
    } else if (ret_type->isDoubleTy()) {
        result = z3ctx.real_const(name.c_str());
    } else if (ret_type->isFloatTy()) {
        result = z3ctx.real_const(name.c_str());
    } else {
        throw std::runtime_error("Unsupported return type for call instruction");
    }
    state_ptr new_state = std::make_shared<State>(*state);
    new_state->memory.put_temp(inst, result);
    if (called_func &&
        called_func->getName().starts_with("__VERIFIER_nondet_")) {
        new_state->nondet_calls.emplace_back(call_inst, result.as_expr());
    }
    new_state->constrain_nondet_input(call_inst, result);
    // new_state->write(inst, result);
    if (called_func &&
        called_func->getName().starts_with("__VERIFIER_nondet_") &&
        ret_type->isIntegerTy() &&
        !state->session.uses_integer_relaxation()) {
        const unsigned width = ret_type->getIntegerBitWidth();

        const char* signed_min = nullptr;
        const char* signed_max = nullptr;
        switch (width) {
        case 1:
            break;
        case 8:
            signed_min = "-128";
            signed_max = "127";
            break;
        case 16:
            signed_min = "-32768";
            signed_max = "32767";
            break;
        case 32:
            signed_min = "-2147483648";
            signed_max = "2147483647";
            break;
        case 64:
            signed_min = "-9223372036854775808";
            signed_max = "9223372036854775807";
            break;
        default:
            break;
        }
        if (signed_min && signed_max) {
            new_state->append_path_condition(
                result >= z3ctx.int_val(signed_min));
            new_state->append_path_condition(
                result <= z3ctx.int_val(signed_max));
        }
    }
    new_state->step_pc();
    return new_state;
}

state_ptr
AInstructionCall::execute_memcpy(state_ptr state) {
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto dst = call_inst->getArgOperand(0);
    auto src = call_inst->getArgOperand(1);
    auto len = call_inst->getArgOperand(2);

    auto len_value = state->evaluate(len).as_expr();
    auto const_len_value = len_value.as_int64() / 4; // ensure it is concrete

    state_ptr new_state = std::make_shared<State>(*state);
    auto dst_obj = new_state->memory.get_object_pointed_by(dst);
    auto src_obj = new_state->memory.get_object_pointed_by(src);
    for (int i = 0; i < const_len_value; i++) {
        Expression idx(new_state->z3ctx.int_val(i));
        auto v = src_obj->read({idx}).as_expr();
        dst_obj->write({idx}, v);
    }

    new_state->step_pc();
    return new_state;
}

state_ptr
AInstructionCall::execute_malloc(state_ptr state) {
    // malloc function, allocate memory and return the pointer
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto size_bytes = call_inst->arg_begin()->get();
    auto size_bytes_expr = state->evaluate(size_bytes);
    // TODO: assume it an integer array and the size of an int is 32 bits;
    // TODO: assume it is 1-d
    z3::expr_vector dims(state->z3ctx);
    const unsigned size_width = size_bytes->getType()->getIntegerBitWidth();
    z3::expr element_count =
        (unsigned_value(size_bytes_expr.as_expr(), size_width, state) /
         4)
            .simplify();
    if (auto* multiply = llvm::dyn_cast<llvm::BinaryOperator>(size_bytes);
        multiply && multiply->getOpcode() == llvm::Instruction::Mul) {
        for (unsigned constant_index = 0; constant_index < 2;
             ++constant_index) {
            auto* element_size = llvm::dyn_cast<llvm::ConstantInt>(
                multiply->getOperand(constant_index));
            if (!element_size || !element_size->equalsInt(4)) continue;

            Expression candidate =
                state->evaluate(multiply->getOperand(1 - constant_index));
            element_count = state->session.uses_integer_relaxation()
                ? integer_semantics::as_int(candidate.as_expr())
                : (integer_semantics::as_unsigned(candidate.as_expr(),
                                                size_width) %
                 integer_semantics::power_of_two(state->z3ctx,
                                                 size_width - 2))
                    .simplify();
            break;
        }
    }
    dims.push_back(element_count);
    auto new_state = std::make_shared<State>(*state);
    if (state->session.uses_integer_relaxation()) {
        new_state->append_path_condition(Expression(size_bytes_expr.defined()));
        new_state->append_path_condition(Expression(
            integer_semantics::as_int(size_bytes_expr.as_expr()) >= 0));
    }
    new_state->memory.heap_alloca(call_inst, dims);
    new_state->step_pc();
    return {new_state};
}

std::vector<state_ptr>
AInstructionCall::execute_if_not_target(state_ptr state, llvm::Function* target) {
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = resolve_called_function(call_inst);

    auto& z3ctx = state->z3ctx;

    if (called_func == target) {
        // TODO: assume all types are int
        return execute_naively(state);
    } else {
        // execute the called function
        auto new_state = execute_normal(state);
        // return execute_if_not_target(new_state, target);
        return {new_state};
    }
}

std::vector<state_ptr>
AInstructionCall::execute_naively(state_ptr state) {
    // execute the function call f(args) by simply creating z3::expr f(args)
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = resolve_called_function(call_inst);

    auto& z3ctx = state->z3ctx;

    size_t num_args = call_inst->arg_size();
    z3::sort_vector domain(z3ctx);
    for (int i = 0; i < num_args; i++) domain.push_back(z3ctx.int_sort());
    auto func_name = "ari_" + called_func->getName().str();
    z3::func_decl f = z3ctx.function(func_name.c_str(), domain, z3ctx.int_sort());
    z3::expr_vector args(z3ctx);
    for (int i = 0; i < num_args; i++) {
        auto arg = call_inst->getArgOperand(i);
        if (arg->getType()->isPointerTy()) {
            auto arg_value = state->memory.get_object(arg);
            args.push_back(arg_value->get_ptr_value().base.as_expr());
        } else {
            auto arg_value = state->evaluate(arg);
            args.push_back(arg_value.as_expr());
        }
    }
    auto new_state = std::make_shared<State>(*state);
    new_state->memory.put_temp(inst, f(args));
    new_state->step_pc();
    return {new_state};
}

bool
AInstructionCall::is_recursive(llvm::Function* target) {
    // this function is generated by Copilot
    auto CG = AnalysisManager::get_instance()->get_CG();
    auto* node = CG->operator[](target);
    std::set<llvm::CallGraphNode*> visited;
    std::function<bool(llvm::CallGraphNode*)> dfs = [&](llvm::CallGraphNode* n) {
        if (!n) return false;
        if (!visited.insert(n).second) return false;
        for (auto& callRecord : *n) {
            auto* callee = callRecord.second;
            if (callee == node) return true; // cycle detected
            if (dfs(callee)) return true;
        }
        return false;
    };
    return dfs(node);

}

std::optional<FunctionSummary>
AInstructionCall::summarize_complete(z3::context& z3ctx) {
    auto call_inst = dyn_cast<llvm::CallInst>(inst);
    auto called_func = resolve_called_function(call_inst);

    FunctionSummarizer fs(called_func, z3ctx);
    return fs.get_summary();
}

state_list
AInstructionBranch::execute(state_ptr state) {
    return _execute(state);
}

loop_state_list
AInstructionBranch::execute(loop_state_ptr state) {
    return _execute(state);
}

rec_state_list
AInstructionBranch::execute(rec_state_ptr state) {
    return _execute(state);
}

template<typename state_ty>
state_list_base<state_ty>
AInstructionBranch::_execute(std::shared_ptr<state_ty> state) {
    auto branch_inst = dyn_cast<llvm::BranchInst>(inst);
    auto new_trace = state->trace;
    new_trace.push_back(branch_inst->getParent());

    std::vector<state_ptr_base<state_ty>> new_states;
    if (branch_inst->isConditional()) {
        auto cond = branch_inst->getCondition();
        auto cond_value = state->evaluate(cond);

        auto true_block = branch_inst->getSuccessor(0);
        auto true_pc = AInstruction::create(&*true_block->instructionsWithoutDebug().begin());
        auto true_state = std::make_shared<state_ty>(*state);
        true_state->path_decisions.push_back(
            {reinterpret_cast<std::uintptr_t>(branch_inst), 0});
        true_state->trace = new_trace;
        true_state->status = State::TESTING;
        // true_state->path_condition = state->path_condition && cond_value;
        true_state->append_path_condition(cond_value);
        true_state->step_pc(true_pc);

        auto false_block = branch_inst->getSuccessor(1);
        auto false_pc = AInstruction::create(&*false_block->instructionsWithoutDebug().begin());
        auto false_state = std::make_shared<state_ty>(*state);
        false_state->path_decisions.push_back(
            {reinterpret_cast<std::uintptr_t>(branch_inst), 1});
        false_state->trace = new_trace;
        false_state->status = State::TESTING;
        // false_state->path_condition = state->path_condition && !cond_value;
        false_state->append_path_condition(!cond_value);
        false_state->step_pc(false_pc);

        new_states.push_back(true_state);
        new_states.push_back(false_state);
    } else {
        // unconditional branch
        auto next_pc = AInstruction::create(&*branch_inst->getSuccessor(0)->instructionsWithoutDebug().begin());
        auto new_state = std::make_shared<state_ty>(*state);
        new_state->trace = new_trace;
        new_state->step_pc(next_pc);
        new_states.push_back(new_state);
    }
    return new_states;
}

std::vector<state_ptr>
AInstructionReturn::execute(state_ptr state) {
    if (state->memory.stack_size() == 1) {
        // no function call, just terminate
        state->status = State::TERMINATED;
        return {state};
    }
    // llvm::errs() << state->memory.to_string() << "\n";
    auto ret = dyn_cast<llvm::ReturnInst>(inst);

    state_ptr new_state = std::make_shared<State>(*state);
    auto& frame = new_state->memory.pop_frame();

    auto ret_value = ret->getReturnValue();
    new_state->step_pc(frame.prev_pc);
    auto call_inst = dyn_cast<llvm::CallInst>(new_state->pc->inst);
    if (ret_value) {
        auto ret_expr = state->evaluate(ret_value);

        // go back to the called site
        assert(call_inst);
        // new_state->memory.allocate(call_inst, ret_expr);
        new_state->memory.put_temp(call_inst, ret_expr);
        cache_func_value(state, ret_expr);
    }
    new_state->step_pc();
    return {new_state};
}

void
AInstructionReturn::cache_func_value(state_ptr state, const Expression& result) {
    auto& cache_instance = state->session.function_cache();
    auto& top_frame = state->memory.top_frame();
    param_list_ty args;

    auto model = state->get_model();
    auto concrete_result = model.eval(result.as_expr(), false);
    if (!state->is_concrete(result, concrete_result)) return;

    for (auto& param : top_frame.func->args()) {
        auto arg_value = state->evaluate(&param);
        auto concrete_value = model.eval(arg_value.as_expr(), true);
        if (!state->is_concrete(arg_value, concrete_value)) return;
        args.push_back(concrete_value.as_int64());
    }
    cache_instance.cache_func_value(top_frame.func, args,
                                    concrete_result.as_int64());
}

std::vector<state_ptr>
AInstructionZExt::execute(state_ptr state) {
    auto zext_inst = dyn_cast<llvm::ZExtInst>(inst);
    auto op = zext_inst->getOperand(0);
    auto op_value = state->evaluate(op, false);
    const unsigned source_width = op->getType()->getIntegerBitWidth();
    Expression result = map_integer_unary(
        op_value, [&](const z3::expr& value) {
            if (state->session.uses_integer_relaxation()) {
                return integer_semantics::as_int(value);
            }
            return integer_semantics::as_unsigned(value, source_width);
        });

    state_ptr new_state = std::make_shared<State>(*state);
    // new_state->write(inst, op_value);
    // new_state->memory.allocate(inst, op_value);
    new_state->memory.put_temp(inst, result);
    new_state->step_pc();
    return {new_state};
}

std::vector<state_ptr>
AInstructionSExt::execute(state_ptr state) {
    auto sext_inst = dyn_cast_or_null<llvm::SExtInst>(inst);
    auto op = sext_inst->getOperand(0);
    auto op_value = state->evaluate(op);
    const unsigned source_width = op->getType()->getIntegerBitWidth();
    if (state->session.uses_integer_relaxation()) {
        Expression result = map_integer_unary(
            op_value, [&](const z3::expr& value) {
                z3::expr integer = integer_semantics::as_int(value);
                return source_width == 1 ? -integer : integer;
            });
        state_ptr new_state = std::make_shared<State>(*state);
        new_state->memory.put_temp(inst, result);
        new_state->step_pc();
        return {new_state};
    }
    if (has_canonical_signed_representation(op)) {
        state_ptr new_state = std::make_shared<State>(*state);
        new_state->memory.put_temp(inst, op_value);
        new_state->step_pc();
        return {new_state};
    }
    z3::expr assumptions = state->get_path_condition().as_expr() &&
                           op_value.defined();
    Expression result = map_integer_unary(
        op_value, [&](const z3::expr& value) {
            z3::expr integer = integer_semantics::as_int(value);
            if (ari_exe::implies(
                    assumptions,
                    integer_semantics::in_signed_range(integer,
                                                       source_width))) {
                return integer;
            }
            return integer_semantics::as_signed(value, source_width);
        });

    state_ptr new_state = std::make_shared<State>(*state);
    // new_state->write(inst, op_value);
    // new_state->memory.allocate(inst, op_value);
    new_state->memory.put_temp(inst, result);
    new_state->step_pc();
    return {new_state};
}

std::vector<state_ptr>
AInstructionPhi::execute(state_ptr state) {
    auto phi_inst = dyn_cast<llvm::PHINode>(inst);
    auto manager = AnalysisManager::get_instance();
    auto& LI = manager->get_LI(phi_inst->getFunction());
    auto loop = LI.getLoopFor(phi_inst->getParent());

    // Compose exact child exits before attempting a whole-loop legacy
    // recurrence. Eager entry summarization also includes the zero-trip case.
    if (loop && is_first_header_phi(phi_inst, loop) &&
        (loop->getParentLoop() || !loop->getSubLoops().empty()) &&
        !state->trace.empty() && !loop->contains(state->trace.back())) {
        if (auto jump = PathExpressionAccelerator::accelerate_nested(loop, state)) {
            return {jump};
        }
    }

    record_completed_loop_path(state, loop, phi_inst);

    // The legacy recurrence route does not distinguish successive Boolean
    // oracle values in its exit relation. Use the sequence-aware accelerator
    // for recognized nondeterministic header controls even in normal mode.
    // The experimental relaxation also selects the path-expression route;
    // it is intended to bypass the first-break modulo bottleneck here.
    if (loop && !force_path_expression_mode() &&
        !loop->getParentLoop() && loop->getSubLoops().empty() &&
        !state->session.uses_integer_relaxation() &&
        !PathExpressionAccelerator::has_nondeterministic_header_control(loop) &&
        !state->session.loop_summary_failed(loop)) {
        if (!state->is_summarizing()) {
            auto accelarated_states = execute_if_summarizable(state);
            if (accelarated_states.size() > 0) {
                return accelarated_states;
            } else {
                state->session.mark_loop_summary_failed(loop);
            }
        }
    }

    if (loop && is_first_header_phi(phi_inst, loop)) {
        auto history = state->loop_path_expressions.find(loop);
        if (history != state->loop_path_expressions.end()) {
            for (const PathSchemaCandidate& candidate :
                 history->second.candidates) {
                auto acceleration = PathExpressionAccelerator::accelerate(
                    loop, state, candidate);
                if (!acceleration) {
                    spdlog::debug(
                        "[path-expr] loop {} did not accelerate candidate {}",
                        loop->getHeader()->getName().str(),
                        render_path_schema(candidate));
                    continue;
                }

                state_list successors{acceleration->jump};
                state_ptr residual = std::make_shared<State>(*state);
                residual->append_path_condition(!acceleration->covered);
                z3::solver residual_solver(residual->z3ctx);
                z3::params residual_parameters(residual->z3ctx);
                residual_parameters.set("timeout", 3000u);
                residual_solver.set(residual_parameters);
                residual_solver.add(
                    residual->get_path_condition().as_expr());
                const z3::check_result residual_result =
                    residual_solver.check();
                spdlog::debug(
                    "[path-expr] loop {} residual feasibility after jump: {}",
                    loop->getHeader()->getName().str(),
                    residual_result == z3::sat
                        ? "sat"
                        : (residual_result == z3::unsat ? "unsat"
                                                        : "unknown"));
                if (residual_result != z3::unsat) {
                    residual->execute_phi_bundle();
                    successors.push_back(residual);
                }
                return successors;
            }
        }
    }

    state_ptr new_state = std::make_shared<State>(*state);
    new_state->execute_phi_bundle();

    return {new_state};
}

loop_state_list
AInstructionPhi::execute(loop_state_ptr state) {
    auto phi_inst = dyn_cast<llvm::PHINode>(inst);
    auto manager = AnalysisManager::get_instance();
    auto& LI = manager->get_LI(phi_inst->getFunction());
    auto loop = LI.getLoopFor(phi_inst->getParent());

    if (loop && !state->session.loop_summary_failed(loop)) {
        auto outer_loop = state->summarizing_loop;
        auto header = loop->getHeader();
        bool is_first_phi = phi_inst == &*header->phis().begin();
        bool is_strict_inner_loop = outer_loop && loop != outer_loop && outer_loop->contains(loop);
        if (is_first_phi && is_strict_inner_loop) {
            spdlog::info("Summarizing nested loop {}", loop->getHeader()->getName().str());
            auto accelerated_states = execute_if_summarizable(std::static_pointer_cast<State>(state));
            if (accelerated_states.empty()) {
                state->session.mark_loop_summary_failed(loop);
                return {};
            }

            loop_state_list nested_states;
            for (auto& accelerated_state : accelerated_states) {
                auto nested_state = std::make_shared<LoopState>(*accelerated_state);
                nested_state->path_condition_in_loop = state->path_condition_in_loop;
                nested_state->summarizing_loop = outer_loop;
                nested_states.push_back(nested_state);
            }
            return nested_states;
        }
    }

    loop_state_ptr new_state = std::make_shared<LoopState>(*state);
    new_state->execute_phi_bundle();

    return {new_state};
}

std::vector<state_ptr>
AInstructionPhi::execute_if_summarizable(state_ptr state) {
    auto phi_inst = dyn_cast<llvm::PHINode>(inst);
    auto manager = AnalysisManager::get_instance();
    auto& LI = manager->get_LI(phi_inst->getFunction());
    auto loop = LI.getLoopFor(phi_inst->getParent());
    if (loop == nullptr) return {};

    auto header = loop->getHeader();

    // try summarizing the loop only when encountering the first phi of the loop
    if (phi_inst != &*header->phis().begin()) return {};

    spdlog::info("Summarizing loop {}", loop->getHeader()->getName().str());
    auto loop_summarizer = LoopSummarizer(loop, state);
    auto summary = loop_summarizer.get_summary();
    if (!summary.has_value()) {
        spdlog::info("Cannot summarize loop {}", loop->getHeader()->getName().str());
        return {};
    }

    state_ptr new_state = std::make_shared<State>(*state);
    if (!summary->is_over_approximated()) {
        LoopCertificate certificate{loop, {}};
        const auto modified = summary->get_modified_values();
        const unsigned count = std::min<unsigned>(
            modified.size(), summary->get_closed_forms().size());
        for (unsigned i = 0; i < count; ++i) {
            auto* phi = llvm::dyn_cast<llvm::PHINode>(modified[i]);
            if (!phi || !phi->getType()->isIntegerTy()) continue;
            if (const auto source = source_variable(phi)) {
                certificate.variables.emplace_back(
                    phi, source->first, source->second,
                    summary->get_closed_forms()[i]);
            }
        }
        if (!certificate.variables.empty()) {
            const bool already_recorded = std::any_of(
                new_state->loop_certificates.begin(),
                new_state->loop_certificates.end(),
                [&](const LoopCertificate& existing) {
                    return existing.loop == loop;
                });
            if (!already_recorded) {
                new_state->loop_certificates.push_back(
                    std::move(certificate));
            }
        }
    }
    const auto iteration_count = summary->get_N();
    auto& loop_info = manager->get_LI(phi_inst->getFunction());
    auto& dominator_tree = manager->get_DT(phi_inst->getFunction());
    llvm::BasicBlock* latch = loop->getLoopLatch();
    for (llvm::BasicBlock* block : loop->blocks()) {
        for (llvm::Instruction& candidate : *block) {
            auto* call = llvm::dyn_cast<llvm::CallInst>(&candidate);
            llvm::Function* callee = call ? resolve_called_function(call)
                                          : nullptr;
            if (!callee ||
                !callee->getName().starts_with("__VERIFIER_nondet_")) {
                continue;
            }
            const bool executes_once_per_iteration =
                iteration_count && latch &&
                loop_info.getLoopFor(block) == loop &&
                dominator_tree.dominates(block, latch);
            if (!executes_once_per_iteration) {
                new_state->counterexample_complete = false;
                continue;
            }
            const std::string name =
                "ari_" + call->getName().str() + "_unknown";
            const z3::func_decl values = state->z3ctx.function(
                name.c_str(), state->z3ctx.int_sort(),
                call->getType()->isIntegerTy(1)
                    ? state->z3ctx.bool_sort() : state->z3ctx.int_sort());
            z3::expr call_count = *iteration_count;
            if (block == header) {
                // A call in the loop header is also evaluated once for the
                // final, false guard check.
                call_count = call_count + 1;
            }
            new_state->nondet_calls.emplace_back(
                call, values, call_count);
        }
    }
    if (summary.has_value()) {
        auto invariant_result = summary->get_invariant_results();
        if (std::find(invariant_result.begin(), invariant_result.end(), FAIL) != invariant_result.end()) {
            if (summary->is_over_approximated() || new_state->is_over_approx ||
                !new_state->counterexample_complete) {
                new_state->status = State::UNKNOWN;
            } else {
                new_state->status = State::FAIL;
            }
            return {new_state};
        } else if (std::find(invariant_result.begin(), invariant_result.end(), VERIUNKNOWN) != invariant_result.end()) {
            new_state->status = State::UNKNOWN;
            return {new_state};
        }
    }

    // assert(!summary->is_over_approximated() && "Over-approximation is not supported yet");


    auto exit_block = loop->getExitBlock();
    // only consider those loops with only one exit block
    if (!exit_block) {
        new_state->status = State::TERMINATED;
        return {new_state};
    }

    auto entering_block = get_loop_entering_block(loop);
    if (summary->is_over_approximated()) {
        for (auto& inst : *header) {
            if (auto phi = llvm::dyn_cast_or_null<llvm::PHINode>(&inst)) {
                auto name = get_z3_name(phi->getName().str());
                auto phi_expr = new_state->z3ctx.int_const(name.c_str());
                auto initial_value = phi->getIncomingValueForBlock(entering_block);
                auto initial_value_expr = new_state->evaluate(initial_value);
                auto evaluated_value = summary->evaluate_expr(phi_expr);
                auto N = summary->get_N();
                if (N.has_value()) {
                    z3::expr_vector src(new_state->z3ctx);
                    z3::expr_vector dst(new_state->z3ctx);
                    src.push_back(manager->get_ind_var());
                    dst.push_back(N.value());
                    // new_state->write(phi, z3::ite(*N == 0, initial_value_expr, evaluated_value.substitute(src, dst)));
                    // new_state->memory.allocate(phi, z3::ite(*N == 0, initial_value_expr, evaluated_value.substitute(src, dst)));
                    new_state->memory.put_temp(phi, z3::ite(*N == 0, initial_value_expr.as_expr(), evaluated_value.substitute(src, dst)));
                } else {
                    // new_state->write(phi, evaluated_value);
                    // new_state->memory.allocate(phi, evaluated_value);
                    new_state->memory.put_temp(phi, evaluated_value);
                }
            }
        }
        for (auto p : summary->summary_over_approx) {
            new_state->append_path_condition(p.first == p.second);
        }
        new_state->append_path_condition(summary->get_constraints());
        new_state->is_over_approx = true;
    } else {
        auto& z3ctx = manager->get_z3ctx();
        z3::expr_vector args(z3ctx);
        for (auto& inst : *header) {
            if (auto phi = llvm::dyn_cast_or_null<llvm::PHINode>(&inst)) {
                auto name = get_z3_name(phi->getName().str());
                auto phi_expr = z3ctx.int_const(name.c_str());
                args.push_back(phi_expr);
            } else if (auto store_inst = llvm::dyn_cast_or_null<llvm::StoreInst>(&inst)) {
                auto ptr = store_inst->getPointerOperand();
                auto name = get_z3_name(ptr->getName().str());
                auto ptr_expr = z3ctx.int_const(name.c_str());
                args.push_back(ptr_expr);
            }
        }
        auto arrays_ptr = state->memory.get_arrays();
        for (int i = 0; i < arrays_ptr.size(); i++) {
            // for each array, we need to get the signature
            auto array_ptr = arrays_ptr[i];
            args.push_back(array_ptr->get_signature());
        }
        z3::expr_vector closed_forms = summary->evaluate(args);
        // auto phi_it = header->phis().begin();
        auto modified_values = summary->get_modified_values();
        for (int i = 0; i < closed_forms.size(); i++) {
            auto modified_value = modified_values[i];
            auto N = summary->get_N();
            z3::expr summarized_value = closed_forms[i];
            if (N.has_value()) {
                z3::expr_vector src(z3ctx);
                z3::expr_vector dst(z3ctx);
                src.push_back(manager->get_ind_var());
                dst.push_back(N.value());
                summarized_value = summarized_value.substitute(src, dst);
            }
            if (auto obj =
                    new_state->memory.get_object_pointed_by(modified_value)) {
                obj->write(summarized_value);
            } else {
                new_state->memory.put_temp(modified_value, summarized_value);
            }
        }
        new_state->append_path_condition(summary->get_constraints());
    }
    // loop summary only computes the values of phi nodes and store instructions
    // so we need to execute the instructions in the header until the terminator
    // to get closed-form solutions to other values in header, which may also be
    // used outside the loop
    auto cur_inst = &*header->getFirstNonPHIOrDbg();
    auto cur_state = new_state;
    cur_state->step_pc(AInstruction::create(cur_inst));
    std::queue<state_ptr> states;
    std::vector<state_ptr> res;
    states.push(cur_state);
    while (!states.empty()) {
        cur_state = states.front();
        states.pop();
        if (cur_state->pc->inst == header->getTerminator()) {
            res.push_back(cur_state);
            continue;
        }
        auto new_states = cur_state->pc->execute(cur_state);
        for (auto& new_state : new_states) states.push(new_state);
    }

    for (auto& state : res) {
        state->trace.push_back(header);
        // auto new_pc = AInstruction::create(&*exit_block->begin());
        auto exit_block_first_inst = &*exit_block->begin();
        // if (exit_block_first_inst->isDebugOrPseudoInst())
        //     exit_block_first_inst = exit_block_first_inst->getNextNonDebugInstruction();
        auto new_pc = AInstruction::create(exit_block_first_inst);
        state->step_pc(new_pc);
    }
    return res;
}

template<typename state_ty>
state_list_base<state_ty>
AInstructionSelect::_execute(std::shared_ptr<state_ty> state) {
    auto select_inst = dyn_cast<llvm::SelectInst>(inst);
    auto cond = select_inst->getCondition();
    auto cond_value = state->evaluate(cond);

    auto true_value = select_inst->getTrueValue();
    auto true_value_expr = state->evaluate(true_value);
    auto true_state = std::make_shared<state_ty>(*state);
    true_state->path_decisions.push_back(
        {reinterpret_cast<std::uintptr_t>(select_inst), 0});
    // true_state->memory.allocate(inst, true_value_expr);
    // true_state->write(inst, true_value_expr);
    true_state->memory.put_temp(inst, true_value_expr);
    true_state->status = State::TESTING;
    // true_state->path_condition = state->path_condition && cond_value;
    true_state->append_path_condition(cond_value);
    true_state->step_pc();

    auto false_value = select_inst->getFalseValue();
    auto false_value_expr = state->evaluate(false_value);
    auto false_state = std::make_shared<state_ty>(*state);
    false_state->path_decisions.push_back(
        {reinterpret_cast<std::uintptr_t>(select_inst), 1});
    // false_state->memory.allocate(inst, false_value_expr);
    false_state->memory.put_temp(inst, false_value_expr);
    // false_state->write(inst, false_value_expr);
    false_state->status = State::TESTING;
    // false_state->path_condition = state->path_condition && !cond_value;
    false_state->append_path_condition(!cond_value);
    false_state->step_pc();

    return {true_state, false_state};
}

state_list
AInstructionSelect::execute(state_ptr state) {
    return _execute(state);
}

loop_state_list
AInstructionSelect::execute(loop_state_ptr state) {
    return _execute(state);
}

llvm::BasicBlock*
AInstruction::get_block() {
    return inst->getParent();
}

static MemoryAddress_ty
parse_ptr(const MemoryObjectPtr ptr, state_ptr state) {
    if (!ptr || !ptr->is_pointer()) {
        throw std::runtime_error("Pointer object expected while resolving memory address");
    }
    auto pointed_addr = ptr->get_ptr_value();
    auto pointed_obj = state->memory.get_object(pointed_addr);
    if (!pointed_obj) {
        throw std::runtime_error("Pointer target is missing from symbolic memory");
    }
    if (!pointed_obj->is_pointer()) {
        return pointed_addr;
    }
    auto addr = parse_ptr(pointed_obj, state);
    for (auto& offset : pointed_addr.offset) {
        addr.offset.push_back(offset);
    }
    return addr;
}

static MemoryAddress_ty
parse_gep(llvm::GetElementPtrInst* gep, state_ptr state) {
    // This function parses the GEP instruction and returns the memory address
    // it points to, based on the operands and the current state.
    auto& z3ctx = state->z3ctx;
    auto ptr_operand = gep->getPointerOperand();
    auto ptr_obj = state->memory.get_object(ptr_operand);
    if (!ptr_obj) {
        throw std::runtime_error(
            "The base pointer has no symbolic-memory binding for GEP: " +
            llvm_value_to_string(*gep));
    }
    if (!ptr_obj->is_pointer()) {
        throw std::runtime_error(
            "The GEP base was overwritten by a scalar value: " +
            llvm_value_to_string(*gep));
    }
    auto pointed_obj = state->memory.get_object(ptr_obj->get_ptr_value());
    if (!pointed_obj) {
        throw std::runtime_error(
            "The GEP base points outside symbolic memory: " +
            llvm_value_to_string(*gep));
    }

    std::vector<Expression> offsets;

    MemoryAddress_ty addr;
    if (!pointed_obj->is_pointer()) {
        addr = ptr_obj->get_ptr_value();
    } else {
        addr = parse_ptr(pointed_obj, state);
    }

    for (auto& idx : gep->indices()) {
        auto idx_value = state->evaluate(idx.get());
        addr.offset.push_back(idx_value);
    }
    return addr;
}

std::vector<state_ptr>
AInstructionLoad::execute(state_ptr state) {
    auto load_inst = dyn_cast<llvm::LoadInst>(inst);
    auto ptr = load_inst->getPointerOperand();
    state_ptr new_state = std::make_shared<State>(*state);

    auto addr = parse_ptr(new_state->memory.get_object(ptr), new_state);
    for (const Expression& offset : addr.offset) {
        new_state->append_path_condition(Expression(offset.defined()));
    }
    auto pointed_obj = new_state->memory.get_object(addr);
    assert(pointed_obj && "Pointed object must exist");

    auto load_value = pointed_obj->read(addr.offset);
    new_state->memory.put_temp(inst, load_value);
    new_state->step_pc();
    return {new_state};
}

bool
AInstructionLoad::is_invariant(loop_state_ptr state, llvm::Loop* loop, llvm::Value* ptr) const {
    auto addr = parse_ptr(state->memory.get_object(ptr), state);
    auto pointed_obj = state->memory.get_object(addr);
    for (auto bb : loop->blocks()) {
        for (auto &inst : *bb) {
            if (auto store_inst = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
                auto stored_ptr = store_inst->getPointerOperand();
                auto stored_addr = parse_ptr(state->memory.get_object(stored_ptr), state);
                if (pointed_obj == state->memory.get_object(stored_addr)) {
                    return false;
                }
            }
        }
    }
    return true;
}

std::vector<state_ptr>
AInstructionStore::execute(state_ptr state) {
    auto store_inst = dyn_cast<llvm::StoreInst>(inst);
    auto ptr = store_inst->getPointerOperand();
    auto value = store_inst->getValueOperand();
    // auto ptr_value = state->evaluate(ptr);
    state_ptr new_state = std::make_shared<State>(*state);

    auto pointer_obj = new_state->memory.get_object(ptr);

    auto offset = pointer_obj->get_ptr_value().offset;
    for (const Expression& index : offset) {
        new_state->append_path_condition(Expression(index.defined()));
    }

    auto pointed_obj = new_state->memory.get_object_pointed_by(ptr);
    auto value_expr = state->evaluate(value, pointed_obj->is_signed());
    assert(pointed_obj && "Pointed object must exist");
    if (value->getType()->isIntegerTy() &&
        !has_canonical_signed_representation(value)) {
        value_expr = canonicalize_signed(
            value_expr, value->getType()->getIntegerBitWidth(),
            new_state->get_path_condition().as_expr());
    }
    pointed_obj->write(offset, value_expr);

    new_state->step_pc();
    return {new_state};
}

std::vector<state_ptr>
AInstructionGEP::execute(state_ptr state) {
    auto gep = dyn_cast_or_null<llvm::GetElementPtrInst>(inst);
    assert(gep);
    auto new_state = std::make_shared<State>(*state);
    auto addr = parse_gep(gep, new_state);
    auto target_obj = new_state->memory.get_object(addr);
    if (target_obj->get_sizes().size() < addr.offset.size()) {
        addr.offset = std::vector<Expression>(addr.offset.begin() + 1, addr.offset.end());
    }
    new_state->memory.put_temp(inst, addr);
    // new_state->store_gep(gep);
    new_state->step_pc();
    return {new_state};
}

std::vector<state_ptr>
AInstructionDebug::execute(state_ptr state) {
    // Debug instructions are not executed, just skipped
    if (auto dbg_declare = llvm::dyn_cast_or_null<llvm::DbgDeclareInst>(inst)) {
        auto llvm_value = dbg_declare->getAddress();
        assert(llvm_value->getType()->isPointerTy() && "Expected a pointer type for debug declare");
    } else if (auto dbg_value = llvm::dyn_cast_or_null<llvm::DbgValueInst>(inst)) {
        auto* llvm_value = dbg_value->getValue();
        if (llvm_value->getType()->isPointerTy()) {
            auto obj = state->memory.get_object_pointed_by(llvm_value);
            auto var = dbg_value->getVariable();
            auto base_size = var->getType()->getSizeInBits();
            if (base_size == 64) {
                auto ori_size = obj->get_sizes();
                for (auto& size : ori_size) {
                    size = size / size.ctx().int_val(2);
                }
                obj->set_sizes(ori_size);
            }
        }
    } else {
        // Other debug instructions are ignored
        llvm::errs() << "Ignoring debug instruction: " << *inst << "\n";
    }
    state->step_pc();
    return {state};
}

std::vector<state_ptr>
AInstructionTrunc::execute(state_ptr state) {
    auto trunc_inst = dyn_cast<llvm::TruncInst>(inst);
    auto op = trunc_inst->getOperand(0);
    auto op_value = state->evaluate(op);
    const unsigned target_width = trunc_inst->getType()->getIntegerBitWidth();
    Expression result = map_integer_unary(
        op_value, [&](const z3::expr& value) {
            if (state->session.uses_integer_relaxation()) {
                z3::expr integer = integer_semantics::as_int(value);
                return target_width == 1 ? integer % 2 != 0 : integer;
            }
            return integer_semantics::from_bv(
                integer_semantics::to_bv(value, target_width),
                target_width);
        });

    state_ptr new_state = std::make_shared<State>(*state);
    // new_state->write(inst, op_value);
    // new_state->memory.allocate(inst, op_value);
    new_state->memory.put_temp(inst, result);
    new_state->step_pc();
    return {new_state};
}
