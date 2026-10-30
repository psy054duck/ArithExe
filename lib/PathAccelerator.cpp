#include "PathAccelerator.h"

#include "AInstruction.h"
#include "AnalysisManager.h"
#include "LoopSummarizer.h"
#include "VerificationSession.h"
#include "common.h"
#include "logics.h"
#include "RationalQuery.h"

#include <spdlog/spdlog.h>

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/CFG.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <limits>
#include <queue>
#include <set>
#include <sstream>
#include <string>

namespace ari_exe {
namespace {

struct ScalarPathTransition {
    z3::expr guard;
    z3::expr_vector updates;

    ScalarPathTransition(const z3::expr& guard,
                         const z3::expr_vector& updates)
        : guard(guard), updates(updates) {}
};

struct CompositeSolution {
    z3::expr_vector parameters;
    z3::expr guard;
    z3::expr_vector one_step;
    z3::expr_vector closed_forms;
    z3::expr induction_variable;

    explicit CompositeSolution(z3::context& context)
        : parameters(context), guard(context.bool_val(true)),
          one_step(context), closed_forms(context),
          induction_variable(context.int_val(0)) {}
};

struct NondetLoopControl {
    llvm::CallInst* call = nullptr;
    llvm::BasicBlock* exit_block = nullptr;
    bool continue_value = true;
    std::string symbolic_function_name;
};

struct PureNondetControl {
    llvm::BranchInst* branch;
    bool true_when_nonzero;
    std::vector<llvm::Instruction*> predicates;
    llvm::SelectInst* selection = nullptr;
};

z3::expr substitute(z3::expr expression, const z3::expr_vector& source,
                    const z3::expr_vector& destination) {
    return expression.substitute(source, destination);
}

std::string loop_name(const llvm::Loop* loop) {
    if (!loop || !loop->getHeader()) return "<unknown>";
    const std::string name = loop->getHeader()->getName().str();
    return name.empty() ? "<unnamed>" : name;
}

std::string compact_text(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    bool pending_space = false;
    for (unsigned char character : text) {
        if (std::isspace(character)) {
            pending_space = !result.empty();
        } else {
            if (pending_space) result.push_back(' ');
            result.push_back(static_cast<char>(character));
            pending_space = false;
        }
    }
    return result;
}

std::string render_expr(const z3::expr& expression) {
    return compact_text(expression.to_string());
}

std::string render_expr_vector(const z3::expr_vector& expressions) {
    std::ostringstream out;
    out << '[';
    for (unsigned i = 0; i < expressions.size(); ++i) {
        if (i != 0) out << ", ";
        out << render_expr(expressions[i]);
    }
    out << ']';
    return out.str();
}

const char* check_result_name(z3::check_result result) {
    if (result == z3::sat) return "sat";
    if (result == z3::unsat) return "unsat";
    return "unknown";
}

llvm::Function* called_function(llvm::CallInst* call) {
    if (llvm::Function* function = call->getCalledFunction()) return function;
    return llvm::dyn_cast<llvm::Function>(
        call->getCalledOperand()->stripPointerCasts());
}

// Project only known fresh scalar oracles whose entire SSA use chain is a
// truthiness test feeding one branch or correlated selects in the same block. Never
// freeze a returned integer that is also used as a polynomial data input.
std::optional<PureNondetControl> pure_nondet_control(llvm::CallInst* call) {
    if (!call || !call->getType()->isIntegerTy() || !call->arg_empty() ||
        call->use_empty() ||
        (!call->getType()->isIntegerTy(1) && !call->hasOneUse())) return std::nullopt;
    llvm::Function* callee = called_function(call);
    if (!callee || !callee->isDeclaration()) return std::nullopt;
    const llvm::StringRef name = callee->getName();
    if (name != "__VERIFIER_nondet_bool" &&
        name != "__VERIFIER_nondet_int" && name != "__VERIFIER_nondet_uint" &&
        name != "__VERIFIER_nondet_char" && name != "__VERIFIER_nondet_uchar" &&
        name != "__VERIFIER_nondet_short" && name != "__VERIFIER_nondet_ushort" &&
        name != "__VERIFIER_nondet_long" && name != "__VERIFIER_nondet_ulong" &&
        name != "__VERIFIER_nondet_longlong" && name != "__VERIFIER_nondet_ulonglong")
        return std::nullopt;

    llvm::Value* value = call;
    bool true_when_nonzero = true;
    std::vector<llvm::Instruction*> predicates;
    auto* user = llvm::dyn_cast<llvm::Instruction>(*value->user_begin());
    if (auto* comparison = llvm::dyn_cast_or_null<llvm::ICmpInst>(user)) {
        if (!value->hasOneUse()) return std::nullopt;
        auto* zero = llvm::dyn_cast<llvm::ConstantInt>(
            comparison->getOperand(comparison->getOperand(0) == value ? 1 : 0));
        if (!zero || !zero->isZero() || comparison->use_empty() ||
            comparison->getParent() != call->getParent() ||
            (comparison->getPredicate() != llvm::ICmpInst::ICMP_EQ &&
             comparison->getPredicate() != llvm::ICmpInst::ICMP_NE))
            return std::nullopt;
        true_when_nonzero = comparison->getPredicate() == llvm::ICmpInst::ICMP_NE;
        predicates.push_back(comparison);
        value = comparison;
        user = llvm::dyn_cast<llvm::Instruction>(*value->user_begin());
    }
    if (!value->getType()->isIntegerTy(1)) return std::nullopt;
    if (auto* negation = llvm::dyn_cast_or_null<llvm::BinaryOperator>(user);
        value->hasOneUse() && negation) {
        auto* one = llvm::dyn_cast<llvm::ConstantInt>(
            negation->getOperand(negation->getOperand(0) == value ? 1 : 0));
        if (negation->getOpcode() != llvm::Instruction::Xor || !one ||
            !one->isOne() || negation->use_empty() ||
            negation->getParent() != call->getParent()) return std::nullopt;
        true_when_nonzero = !true_when_nonzero;
        predicates.push_back(negation);
        value = negation;
        user = llvm::dyn_cast<llvm::Instruction>(*value->user_begin());
    }
    if (llvm::isa_and_nonnull<llvm::SelectInst>(user)) {
        // All selections keep the same SSA predicate: this is one fresh choice,
        // not an independent nondeterministic choice for each selected value.
        llvm::SelectInst* first = nullptr;
        for (const auto& use : value->uses()) {
            auto* selection = llvm::dyn_cast<llvm::SelectInst>(use.getUser());
            if (!selection || use.getOperandNo() != 0 ||
                selection->getParent() != call->getParent()) return std::nullopt;
            if (!first) first = selection;
        }
        return PureNondetControl{nullptr, true_when_nonzero, predicates, first};
    }
    if (!value->hasOneUse()) return std::nullopt;
    auto* branch = llvm::dyn_cast_or_null<llvm::BranchInst>(user);
    if (!branch || !branch->isConditional() || branch->getCondition() != value ||
        branch->getParent() != call->getParent()) return std::nullopt;
    return PureNondetControl{branch, true_when_nonzero, predicates};
}

std::optional<NondetLoopControl>
find_nondet_loop_control(llvm::Loop* loop, bool require_header_only_exit = true) {
    llvm::BasicBlock* header = loop->getHeader();
    auto* branch = llvm::dyn_cast<llvm::BranchInst>(header->getTerminator());
    if (!branch || !branch->isConditional()) return std::nullopt;
    llvm::CallInst* call = nullptr;
    std::optional<PureNondetControl> control;
    for (auto& instruction : *header) {
        auto* candidate = llvm::dyn_cast<llvm::CallInst>(&instruction);
        auto match = pure_nondet_control(candidate);
        if (match && match->branch == branch) {
            call = candidate;
            control = std::move(match);
            break;
        }
    }
    if (!control) return std::nullopt;

    // Jumping to the exit skips the header. Only the oracle, its single-use
    // truthiness predicates, and PHIs may execute there; other computations could
    // define values used on exit.
    for (const llvm::Instruction& instruction : *header) {
        if (&instruction != call && &instruction != branch &&
            std::find(control->predicates.begin(), control->predicates.end(),
                      &instruction) == control->predicates.end() &&
            !llvm::isa<llvm::PHINode>(instruction) &&
            !instruction.isDebugOrPseudoInst()) return std::nullopt;
    }

    const bool first_inside = loop->contains(branch->getSuccessor(0));
    const bool second_inside = loop->contains(branch->getSuccessor(1));
    if (first_inside == second_inside) return std::nullopt;

    llvm::SmallVector<llvm::BasicBlock*, 4> exiting_blocks;
    loop->getExitingBlocks(exiting_blocks);
    if (require_header_only_exit &&
        (exiting_blocks.size() != 1 || exiting_blocks.front() != header)) {
        return std::nullopt;
    }

    llvm::BasicBlock* exit =
        first_inside ? branch->getSuccessor(1) : branch->getSuccessor(0);
    return NondetLoopControl{
        call, exit, first_inside == control->true_when_nonzero,
        "ari_" + call->getName().str() + "_unknown"};
}

void collect_control_applications(const z3::expr& expression,
                                  const std::string& function_name,
                                  z3::expr_vector& source,
                                  z3::expr_vector& destination,
                                  bool replacement) {
    if (expression.is_app() &&
        expression.decl().name().str() == function_name) {
        source.push_back(expression);
        destination.push_back(expression.is_bool()
            ? expression.ctx().bool_val(replacement)
            : expression.ctx().int_val(replacement ? 1 : 0));
        return;
    }
    if (!expression.is_app()) return;
    for (const z3::expr& argument : expression.args()) {
        collect_control_applications(argument, function_name, source,
                                     destination, replacement);
    }
}

std::optional<z3::expr>
project_nondet_control(const z3::expr& guard,
                       const NondetLoopControl& control) {
    z3::expr_vector source(guard.ctx());
    z3::expr_vector destination(guard.ctx());
    collect_control_applications(
        guard, control.symbolic_function_name, source, destination,
        control.continue_value);
    if (source.empty()) return std::nullopt;
    z3::expr projected = guard;
    return projected.substitute(source, destination).simplify();
}

// A fresh Boolean carried across the backedge is a choice, not a
// polynomial coordinate. Never project Boolean data or persistent flags.
bool control_only_boolean_phi(const llvm::PHINode& phi) {
    if (!phi.getType()->isIntegerTy(1) || phi.use_empty()) return false;
    for (const auto& use : phi.uses()) {
        if (auto* branch = llvm::dyn_cast<llvm::BranchInst>(use.getUser())) {
            if (!branch->isConditional() || branch->getCondition() != &phi) return false;
        } else if (auto* select = llvm::dyn_cast<llvm::SelectInst>(use.getUser())) {
            if (use.getOperandNo() != 0 || select->getCondition() != &phi) return false;
        } else return false;
    }
    for (auto& incoming : phi.incoming_values()) {
        auto* call = llvm::dyn_cast<llvm::CallInst>(incoming.get());
        auto* callee = call ? called_function(call) : nullptr;
        if (!callee || !callee->isDeclaration() ||
            callee->getName() != "__VERIFIER_nondet_bool" ||
            !call->arg_empty() || !call->hasOneUse()) return false;
    }
    return true;
}

std::optional<std::string>
unsupported_profile_reason(
    const llvm::Loop* loop,
    const std::optional<NondetLoopControl>& nondet_control,
    bool coarse = false) {
    if (!coarse && !loop->getSubLoops().empty()) return "nested loop";
    auto globals = scalar_global_loop_bindings(const_cast<llvm::Loop*>(loop));
    if (coarse && !globals.empty()) return "global transformer requires precise summary";
    if (nondet_control && !globals.empty()) return "global transformer with nondeterministic header";
    for (const llvm::PHINode& phi : loop->getHeader()->phis()) {
        if (coarse && loop->getSubLoops().empty() && control_only_boolean_phi(phi)) continue;
        if (!phi.getType()->isIntegerTy() ||
            phi.getType()->isIntegerTy(1)) return "non-integer scalar phi";
    }
    for (const llvm::BasicBlock* block : loop->blocks()) {
        for (const llvm::Instruction& instruction : *block) {
            // Undef/freeze can introduce fresh choices inside the body; they
            // are not a deterministic polynomial map of the scalar state.
            if (llvm::isa<llvm::FreezeInst>(instruction)) return "freeze";
            for (const llvm::Use& operand : instruction.operands()) {
                if (llvm::isa<llvm::UndefValue>(operand.get())) {
                    return "undefined operand";
                }
            }
            if (llvm::isa<llvm::StoreInst>(instruction)) return "store";
            if (auto* load = llvm::dyn_cast<llvm::LoadInst>(&instruction)) {
                auto* global = llvm::dyn_cast<llvm::GlobalVariable>(load->getPointerOperand());
                if (load->isVolatile() || load->isAtomic() || !global || !globals.contains(const_cast<llvm::GlobalVariable*>(global))) return "load";
            }
            if (const auto* call =
                    llvm::dyn_cast<llvm::CallInst>(&instruction)) {
                if (!nondet_control || call != nondet_control->call) {
                    // Preserve the existing Boolean profile, including
                    // compiler-generated selects. Integer oracles are newly
                    // admitted only through the strict pure-control recognizer.
                    // Fresh data in the maps is rejected separately below.
                    llvm::Function* callee = called_function(const_cast<llvm::CallInst*>(call));
                    if (callee && is_pure_scalar_helper(const_cast<llvm::Function*>(callee))) continue;
                    if (callee && is_scalar_state_helper(const_cast<llvm::Function*>(callee))) {
                        for (auto& helper_inst : *callee->begin()) {
                            llvm::Value* pointer = nullptr;
                            if (auto* load = llvm::dyn_cast<llvm::LoadInst>(&helper_inst)) pointer = load->getPointerOperand();
                            if (auto* store = llvm::dyn_cast<llvm::StoreInst>(&helper_inst)) pointer = store->getPointerOperand();
                            if (pointer && !globals.contains(llvm::cast<llvm::GlobalVariable>(pointer))) return "untracked global";
                        }
                        continue;
                    }
                    if (callee && callee->isDeclaration() && callee->getName() == "__VERIFIER_nondet_bool" &&
                        call->arg_empty() && call->use_empty()) continue;
                    // Assertions are proof obligations, not fresh data maps.
                    // Precise returning guards include them; coarse boundary
                    // replay checks them from every summarized finite prefix.
                    if (callee && callee->getName().ends_with("assert") &&
                        call->arg_size() == 1 &&
                        call->getArgOperand(0)->getType()->isIntegerTy()) continue;
                    if (coarse && block != loop->getHeader() &&
                        ((callee && callee->isDeclaration() &&
                          callee->getName() == "__VERIFIER_nondet_bool" &&
                          call->arg_empty() && call->getType()->isIntegerTy(1)) ||
                         pure_nondet_control(const_cast<llvm::CallInst*>(call)))) {
                        continue;
                    }
                    return "call";
                }
            }
        }
    }
    return std::nullopt;
}

PathSymbol path_symbol_for_final_state(llvm::Loop* loop,
                                       const loop_state_ptr& final_state) {
    std::vector<PathDecisionEvent> decisions = final_state->path_decisions;
    if (decisions.empty()) {
        decisions.push_back(
            {reinterpret_cast<std::uintptr_t>(loop->getHeader()), 0});
    }
    return final_state->session.intern_loop_path(loop, decisions);
}

std::optional<std::map<PathSymbol, ScalarPathTransition>>
enumerate_transitions(
    llvm::Loop* loop, const state_ptr& state,
    const std::optional<NondetLoopControl>& nondet_control,
    bool project_controls = false) {
    spdlog::debug("[path-expr] loop {} enumerating one-iteration paths",
                  loop_name(loop));
    const auto globals = scalar_global_loop_bindings(loop);
    if (!globals.empty()) {
        if (!state->session.uses_integer_relaxation() || state->trace.empty()) return std::nullopt;
        for (const auto& [global, phi] : globals) {
            auto object = state->memory.get_object_pointed_by(global);
            auto* incoming = phi->getIncomingValueForBlock(state->trace.back());
            if (!object || !incoming) return std::nullopt;
            auto value = state->evaluate(incoming);
            z3::solver certificate(state->z3ctx);
            certificate.set("timeout", 3000u);
            certificate.add(state->get_path_condition().as_expr() &&
                !(value.defined() && object->read().defined() &&
                  value.as_expr() == object->read().as_expr()));
            if (certificate.check() != z3::unsat) return std::nullopt;
        }
    }
    LoopExecution execution(loop, state);
    auto [final_states, exit_states] = execution.run();
    (void)exit_states;
    if (execution.had_unknown_result()) {
        spdlog::debug(
            "[path-expr] loop {} path enumeration returned unknown",
            loop_name(loop));
        return std::nullopt;
    }

    auto& context = state->z3ctx;
    std::map<PathSymbol, ScalarPathTransition> transitions;
    std::vector<std::pair<llvm::BasicBlock*, loop_state_ptr>> returning_states;
    for (const loop_state_ptr& final_state : final_states) {
        llvm::BasicBlock* latch = final_state->pc->get_block();
        if (!loop->isLoopLatch(latch) ||
            final_state->pc->inst != latch->getTerminator() ||
            !llvm::isa<llvm::BranchInst>(final_state->pc->inst)) {
            spdlog::debug(
                "[path-expr] loop {} rejected a transition that did not "
                "end at a latch",
                loop_name(loop));
            return std::nullopt;
        }

        // The shared recurrence explorer stops BEFORE the latch terminator.
        // A precise iteration ends AFTER taking its backedge: its guard and
        // decision word must include a conditional latch, just as the normal
        // executor's completed prefix does. Do not change the legacy explorer's
        // contract, and do not execute the successor header's PHIs here.
        for (const auto& successor : execution.step(final_state)) {
            const auto feasible = execution.test(successor);
            if (feasible == LoopExecution::L_TESTUNKNOWN) {
                spdlog::debug("[path-expr] loop {} latch feasibility returned unknown",
                              loop_name(loop));
                return std::nullopt;
            }
            if (feasible == LoopExecution::L_UNFEASIBLE) continue;
            if (successor->pc->get_block() == loop->getHeader()) {
                returning_states.emplace_back(latch, successor);
            } else if (loop->contains(successor->pc->get_block())) {
                // A non-returning in-loop successor needs further exploration;
                // rejecting it is safer than claiming exhaustive enumeration.
                return std::nullopt;
            } else {
                spdlog::debug("[path-expr] loop {} latch exit excluded from returning paths",
                              loop_name(loop));
            }
        }
    }

    for (const auto& [latch, final_state] : returning_states) {

        z3::expr_vector updates(context);
        z3::expr updates_defined = context.bool_val(true);
        for (llvm::PHINode& phi : loop->getHeader()->phis()) {
            if (project_controls && control_only_boolean_phi(phi)) continue;
            if (!phi.getType()->isIntegerTy()) {
                spdlog::debug(
                    "[path-expr] loop {} has a non-integer header phi",
                    loop_name(loop));
                return std::nullopt;
            }
            llvm::Value* incoming = phi.getIncomingValueForBlock(latch);
            if (!incoming) {
                spdlog::debug(
                    "[path-expr] loop {} has a phi without a latch value",
                    loop_name(loop));
                return std::nullopt;
            }
            const Expression update = final_state->evaluate(incoming);
            updates.push_back(update.as_expr());
            updates_defined = updates_defined && update.defined();
        }
        const PathSymbol symbol = path_symbol_for_final_state(loop, final_state);
        if (transitions.contains(symbol)) {
            // A block word must identify exactly one guarded transition.
            spdlog::debug(
                "[path-expr] loop {} maps p{} to multiple transitions",
                loop_name(loop), symbol);
            return std::nullopt;
        }
        z3::expr transition_guard =
            final_state->get_path_condition().as_expr() && updates_defined;
        for (const auto& assertion : final_state->summary_invariants)
            transition_guard = transition_guard && assertion.defined() &&
                               assertion.as_expr();
        if (nondet_control) {
            auto projected =
                project_nondet_control(transition_guard, *nondet_control);
            if (!projected) {
                spdlog::debug(
                    "[path-expr] loop {} could not project nondeterministic "
                    "header call from transition p{}",
                    loop_name(loop), symbol);
                return std::nullopt;
            }
            transition_guard = *projected;
        }
        if (project_controls) for (auto& phi : loop->getHeader()->phis()) {
            if (!control_only_boolean_phi(phi)) continue;
            z3::expr_vector source(context), yes(context), no(context);
            source.push_back(context.bool_const(get_z3_name(phi.getName().str()).c_str()));
            yes.push_back(context.bool_val(true));
            no.push_back(context.bool_val(false));
            auto copy = transition_guard;
            auto other = transition_guard;
            transition_guard = (copy.substitute(source, yes) ||
                                other.substitute(source, no)).simplify();
            spdlog::debug("[coarse-path] projected control-only Boolean phi {} from p{}",
                          phi.getName().str(), symbol);
        }
        spdlog::debug(
            "[path-expr] loop {} transition p{}: guard={}, update={}",
            loop_name(loop), symbol, render_expr(transition_guard),
            render_expr_vector(updates));
        transitions.emplace(
            std::piecewise_construct, std::forward_as_tuple(symbol),
            std::forward_as_tuple(transition_guard, updates));
    }
    spdlog::debug("[path-expr] loop {} enumerated {} path transition(s)",
                  loop_name(loop), transitions.size());
    return transitions;
}

bool depends_on_any(const z3::expr& expression,
                    const z3::expr_vector& variables) {
    if (expression.is_quantifier()) {
        return depends_on_any(expression.body(), variables);
    }
    if (!expression.is_app()) return false;
    if (expression.is_app()) {
        for (const z3::expr& variable : variables) {
            if (z3::eq(expression, variable)) return true;
        }
    }
    for (const z3::expr& argument : expression.args()) {
        if (depends_on_any(argument, variables)) return true;
    }
    return false;
}

bool has_real_arithmetic(const z3::expr& formula) {
    if (formula.is_real()) return true;
    if (formula.is_quantifier()) return has_real_arithmetic(formula.body());
    if (!formula.is_app()) return false;
    for (const auto& child : formula.args())
        if (has_real_arithmetic(child)) return true;
    return false;
}

bool contains_quantifier(const z3::expr& formula) {
    if (formula.is_quantifier()) return true;
    if (!formula.is_app()) return false;
    for (const auto& child : formula.args())
        if (contains_quantifier(child)) return true;
    return false;
}

z3::check_result decide_unsat(const z3::expr& formula, bool coarse = false) {
    z3::solver solver(formula.ctx());
    z3::params parameters(formula.ctx());
    parameters.set("timeout", 2000u);
    solver.set(parameters);
    solver.add(has_real_arithmetic(formula)
        ? integerize_rational_query(formula, false) : formula);
    return coarse ? VerificationSession::current().check_coarse_query(solver, 2000u)
                  : solver.check();
}

std::optional<z3::expr> eliminate_quantifiers(const z3::expr& formula,
                                            unsigned timeout_ms = 2000) {
    try {
        z3::goal goal(formula.ctx());
        goal.add(formula);
        // QE is an optional optimization; it must not block acceleration.
        z3::apply_result result = z3::try_for(
            z3::tactic(formula.ctx(), "qe"), timeout_ms)(goal);
        z3::expr reduced = formula.ctx().bool_val(false);
        for (unsigned i = 0; i < result.size(); ++i) {
            reduced = reduced || result[i].as_expr();
        }
        return reduced.simplify();
    } catch (const z3::exception&) {
        return std::nullopt;
    }
}

void collect_affine_bindings(z3::expr formula, const z3::expr& variable,
                             std::vector<z3::expr>& bindings) {
    if (!formula.is_app()) return;
    const Z3_decl_kind kind = formula.decl().decl_kind();
    if (kind == Z3_OP_AND) {
        for (const z3::expr& argument : formula.args()) {
            collect_affine_bindings(argument, variable, bindings);
        }
        return;
    }
    if (kind != Z3_OP_EQ || formula.num_args() != 2) return;
    z3::expr lhs = formula.arg(0);
    z3::expr rhs = formula.arg(1);
    z3::expr_vector singleton(formula.ctx());
    singleton.push_back(variable);
    if (z3::eq(lhs, variable) && !depends_on_any(rhs, singleton)) {
        bindings.push_back(rhs);
    } else if (z3::eq(rhs, variable) &&
               !depends_on_any(lhs, singleton)) {
        bindings.push_back(lhs);
    }
}

// LinearLogic assumes quantifier-free integer arithmetic. QE may leave
// quantifiers, divisibility, or nonlinear terms, so do not pass those to it.
bool is_linear_integer_formula(const z3::expr& expression) {
    if (!expression.is_app()) return false;
    if (expression.is_numeral()) return expression.is_int();
    auto kind = expression.decl().decl_kind();
    if (kind == Z3_OP_UNINTERPRETED)
        return expression.is_const() && expression.is_int();
    if (kind == Z3_OP_TRUE || kind == Z3_OP_FALSE) return true;
    if ((kind == Z3_OP_EQ || kind == Z3_OP_LE || kind == Z3_OP_LT ||
         kind == Z3_OP_GE || kind == Z3_OP_GT) &&
        (!expression.arg(0).is_int() || !expression.arg(1).is_int()))
        return false;
    switch (kind) {
        case Z3_OP_AND: case Z3_OP_OR: case Z3_OP_NOT: case Z3_OP_IMPLIES:
        case Z3_OP_EQ: case Z3_OP_LE: case Z3_OP_LT: case Z3_OP_GE: case Z3_OP_GT:
        case Z3_OP_ADD: case Z3_OP_SUB: case Z3_OP_UMINUS:
            break;
        case Z3_OP_MUL: {
            unsigned nonconstants = 0;
            for (const auto& argument : expression.args())
                if (!argument.is_numeral()) ++nonconstants;
            if (nonconstants > 1) return false;
            break;
        }
        default: return false;
    }
    for (const auto& argument : expression.args())
        if (!is_linear_integer_formula(argument)) return false;
    return true;
}

// The legacy matrix solver requires exact integer division at every pivot.
// With one unknown, unit coefficients guarantee this precondition; other
// stride sizes keep the existing relational fallback instead of asserting.
bool has_unit_count_coefficients(const z3::expr& formula, const z3::expr& count) {
    auto kind = formula.decl().decl_kind();
    if (kind == Z3_OP_EQ || kind == Z3_OP_LE || kind == Z3_OP_LT ||
        kind == Z3_OP_GE || kind == Z3_OP_GT) {
        auto difference = formula.arg(0) - formula.arg(1);
        z3::expr_vector source(count.ctx()), zero(count.ctx()), one(count.ctx());
        source.push_back(count);
        zero.push_back(count.ctx().int_val(0));
        one.push_back(count.ctx().int_val(1));
        auto coefficient = (substitute(difference, source, one) -
                            substitute(difference, source, zero)).simplify();
        int64_t value = 0;
        return coefficient.is_numeral() && coefficient.is_numeral_i64(value)
            && value >= -1 && value <= 1;
    }
    for (const auto& argument : formula.args())
        if (!has_unit_count_coefficients(argument, count)) return false;
    return true;
}

bool has_rational_cast(const z3::expr& value) {
    if (!value.is_app()) return false;
    if (value.decl().decl_kind() == Z3_OP_TO_INT) return true;
    for (const auto& child : value.args())
        if (has_rational_cast(child)) return true;
    return false;
}

// A worker-generated Real closed form is wrapped in to_int solely to keep
// LLVM substitutions well-sorted. For mathematical recurrence certification,
// recover the exact expression; genuine integer div/mod is never unwrapped.
z3::expr exact_closed_form(const z3::expr& value) {
    if (!value.is_app()) return value;
    auto kind = value.decl().decl_kind();
    if (kind == Z3_OP_TO_INT) return value.arg(0);
    if (kind == Z3_OP_TO_REAL) {
        auto child = exact_closed_form(value.arg(0));
        return child.is_real() ? child : z3::to_real(child);
    }
    if (kind == Z3_OP_ADD || kind == Z3_OP_SUB || kind == Z3_OP_MUL) {
        auto result = exact_closed_form(value.arg(0));
        for (unsigned i = 1; i < value.num_args(); ++i) {
            auto child = exact_closed_form(value.arg(i));
            result = kind == Z3_OP_ADD ? result + child
                : kind == Z3_OP_SUB ? result - child : result * child;
        }
        return result;
    }
    if (kind == Z3_OP_UMINUS) return -exact_closed_form(value.arg(0));
    if (kind == Z3_OP_ITE)
        return z3::ite(value.arg(0), exact_closed_form(value.arg(1)),
                      exact_closed_form(value.arg(2)));
    return value;
}

z3::expr lift_polynomial(const z3::expr& value) {
    if (!value.is_int()) return value;
    if (!value.is_app()) return z3::to_real(value);
    auto kind = value.decl().decl_kind();
    if (kind == Z3_OP_ADD || kind == Z3_OP_SUB || kind == Z3_OP_MUL) {
        auto result = lift_polynomial(value.arg(0));
        for (unsigned i = 1; i < value.num_args(); ++i) {
            auto child = lift_polynomial(value.arg(i));
            result = kind == Z3_OP_ADD ? result + child
                : kind == Z3_OP_SUB ? result - child : result * child;
        }
        return result;
    }
    if (kind == Z3_OP_UMINUS) return -lift_polynomial(value.arg(0));
    return z3::to_real(value);
}

bool certify_closed_forms(const CompositeSolution& solution) {
    z3::context& context = solution.guard.ctx();
    z3::expr_vector source(context);
    z3::expr_vector at_zero(context);
    z3::expr_vector at_n(context);
    z3::expr_vector at_successor(context);
    source.push_back(solution.induction_variable);
    at_zero.push_back(context.int_val(0));
    at_n.push_back(solution.induction_variable);
    at_successor.push_back(solution.induction_variable + 1);

    z3::expr base_failure = context.bool_val(false);
    z3::expr step_failure = context.bool_val(false);
    z3::expr_vector state_at_n(context);
    bool rational = false;
    for (const auto& closed : solution.closed_forms)
        rational = rational || has_rational_cast(closed);
    z3::expr_vector exact_parameters(context);
    for (const auto& parameter : solution.parameters)
        exact_parameters.push_back(z3::to_real(parameter));
    for (unsigned i = 0; i < solution.closed_forms.size(); ++i) {
        z3::expr closed = rational ? exact_closed_form(solution.closed_forms[i])
                                  : solution.closed_forms[i];
        if (rational && closed.is_int()) closed = z3::to_real(closed);
        base_failure = base_failure ||
                       closed.substitute(source, at_zero) !=
                           solution.parameters[i];
        state_at_n.push_back(closed.substitute(source, at_n));
    }
    for (unsigned i = 0; i < solution.closed_forms.size(); ++i) {
        z3::expr expected = rational
            ? lift_polynomial(solution.one_step[i]).substitute(exact_parameters, state_at_n)
            : solution.one_step[i].substitute(solution.parameters, state_at_n);
        z3::expr actual = (rational ? exact_closed_form(solution.closed_forms[i])
                                   : solution.closed_forms[i]).substitute(
            source, at_successor);
        step_failure = step_failure || actual != expected;
    }

    if (decide_unsat(base_failure) != z3::unsat) return false;
    return decide_unsat(solution.induction_variable >= 0 && step_failure) ==
           z3::unsat;
}

std::optional<CompositeSolution>
solve_composite(llvm::Loop* loop, const state_ptr& state,
                const PathWord& word,
                const std::map<PathSymbol, ScalarPathTransition>& transitions) {
    if (word.empty()) {
        spdlog::debug("[path-expr] loop {} rejected an empty composite word",
                      loop_name(loop));
        return std::nullopt;
    }
    z3::context& context = state->z3ctx;
    CompositeSolution solution(context);

    for (llvm::PHINode& phi : loop->getHeader()->phis()) {
        const std::string name = get_z3_name(phi.getName().str());
        solution.parameters.push_back(context.int_const(name.c_str()));
    }
    if (solution.parameters.empty()) {
        spdlog::debug(
            "[path-expr] loop {} composite {} has no scalar parameters",
            loop_name(loop), render_path_word(word));
        return std::nullopt;
    }

    z3::expr_vector current(context);
    for (const z3::expr& parameter : solution.parameters) {
        current.push_back(parameter);
    }
    for (PathSymbol symbol : word) {
        auto transition = transitions.find(symbol);
        if (transition == transitions.end() ||
            transition->second.updates.size() != current.size()) {
            spdlog::debug(
                "[path-expr] loop {} cannot compose {}: missing or "
                "ill-shaped transition p{}",
                loop_name(loop), render_path_word(word), symbol);
            return std::nullopt;
        }
        solution.guard =
            solution.guard && substitute(transition->second.guard,
                                         solution.parameters, current);
        z3::expr_vector next(context);
        for (const z3::expr& update : transition->second.updates) {
            next.push_back(
                substitute(update, solution.parameters, current));
        }
        current = next;
    }
    solution.guard = solution.guard.simplify();
    solution.one_step = current;
    spdlog::debug(
        "[path-expr] loop {} composite {}: parameters={}, guard={}, "
        "one-step={}",
        loop_name(loop), render_path_word(word),
        render_expr_vector(solution.parameters), render_expr(solution.guard),
        render_expr_vector(solution.one_step));

    const std::uint64_t id = state->session.next_path_expression_id();
    const std::string induction_name =
        "ari_path_n_" + std::to_string(id);
    solution.induction_variable =
        context.int_const(induction_name.c_str());

    bool elementary = true;
    for (unsigned i = 0; i < current.size(); ++i) {
        z3::expr delta = (current[i] - solution.parameters[i]).simplify();
        if (depends_on_any(delta, solution.parameters)) {
            // A collapsed child often resets a live-out (e.g. v := 10).
            // Reset maps compose with translations and have an exact n=0
            // base case. Keep the same base/step certification as all maps.
            if (!depends_on_any(current[i], solution.parameters)) {
                solution.closed_forms.push_back(z3::ite(
                    solution.induction_variable == 0,
                    solution.parameters[i], current[i]));
                continue;
            }
            elementary = false;
            break;
        }
        solution.closed_forms.push_back(
            (solution.parameters[i] + solution.induction_variable * delta)
                .simplify());
    }

    if (!elementary) {
        spdlog::debug(
            "[path-expr] loop {} composite {} is not a translation; "
            "invoking the recurrence solver",
            loop_name(loop), render_path_word(word));
        solution.closed_forms.resize(0);
        rec_solver solver(context);
        solver.set_ind_var(solution.induction_variable);
        rec_ty equations;
        z3::expr_vector initial_functions(context);
        z3::expr_vector initial_values(context);
        z3::expr_vector functions_at_n(context);
        std::vector<z3::func_decl> functions;
        z3::expr_vector result_keys(context);
        for (unsigned i = 0; i < solution.parameters.size(); ++i) {
            const std::string function_name =
                "ari_path_f_" + std::to_string(id) + "_" +
                std::to_string(i);
            z3::func_decl function = context.function(
                function_name.c_str(), context.int_sort(),
                context.int_sort());
            functions.push_back(function);
            result_keys.push_back(
                context.int_const(function_name.c_str()));
            initial_functions.push_back(function(context.int_val(0)));
            initial_values.push_back(solution.parameters[i]);
            functions_at_n.push_back(function(solution.induction_variable));
        }
        for (unsigned i = 0; i < current.size(); ++i) {
            equations.insert_or_assign(
                functions[i](solution.induction_variable + 1),
                current[i].substitute(solution.parameters, functions_at_n));
        }
        solver.add_initial_values(initial_functions, initial_values);
        solver.set_eqs({context.bool_val(true)}, {equations});
        try {
            if (!solver.solve()) {
                spdlog::debug(
                    "[path-expr] loop {} recurrence solver found no closed "
                    "form for {}",
                    loop_name(loop), render_path_word(word));
                return std::nullopt;
            }
        } catch (const std::exception& error) {
            spdlog::debug(
                "[path-expr] loop {} recurrence solver failed for {}: {}",
                loop_name(loop), render_path_word(word), error.what());
            return std::nullopt;
        }
        const closed_form_ty solved = solver.get_exact_res();
        for (const z3::expr& result_key : result_keys) {
            auto found = solved.find(result_key);
            if (found == solved.end()) {
                spdlog::debug(
                    "[path-expr] loop {} recurrence result for {} omitted "
                    "one component",
                    loop_name(loop), render_path_word(word));
                return std::nullopt;
            }
            solution.closed_forms.push_back(found->second);
        }
    } else {
        spdlog::debug(
            "[path-expr] loop {} composite {} uses the translation closed "
            "form (translations/resets)",
            loop_name(loop), render_path_word(word));
    }

    spdlog::debug(
        "[path-expr] loop {} composite {} proposed closed form at n={}: {}",
        loop_name(loop), render_path_word(word),
        render_expr(solution.induction_variable),
        render_expr_vector(solution.closed_forms));
    if (!certify_closed_forms(solution)) {
        spdlog::debug(
            "[path-expr] loop {} composite {} failed base/step "
            "certification",
            loop_name(loop), render_path_word(word));
        return std::nullopt;
    }
    spdlog::debug(
        "[path-expr] loop {} composite {} passed base/step certification",
        loop_name(loop), render_path_word(word));
    return solution;
}

z3::expr_vector current_header_values(llvm::Loop* loop,
                                      const state_ptr& state, bool project_controls = false) {
    z3::expr_vector values(state->z3ctx);
    if (state->trace.empty()) return values;
    llvm::BasicBlock* predecessor = state->trace.back();
    for (llvm::PHINode& phi : loop->getHeader()->phis()) {
        if (project_controls && control_only_boolean_phi(phi)) continue;
        llvm::Value* incoming = phi.getIncomingValueForBlock(predecessor);
        if (!incoming) return z3::expr_vector(state->z3ctx);
        values.push_back(state->evaluate(incoming).as_expr());
    }
    return values;
}

std::optional<PathAcceleration> accelerate_arbitrary_finite(
    llvm::Loop* loop, const state_ptr& state,
    const PathSchemaCandidate& candidate,
    const std::map<PathSymbol, ScalarPathTransition>& transitions,
    const CompositeSolution& solution, const z3::expr_vector& initial,
    const NondetLoopControl& control) {
    // A stable periodic word covers every finite continuation, including
    // stopping at any proper-prefix offset of its final repetition.
    z3::context& context = state->z3ctx;
    const std::string name = loop_name(loop);
    // The closed forms use mathematical values, not LLVM poison metadata.
    // In particular, a zero-step exit must not heal an undefined incoming
    // value. Only drop that metadata when the source region entails it.
    z3::expr initial_defined = context.bool_val(true);
    for (llvm::PHINode& phi : loop->getHeader()->phis()) {
        llvm::Value* incoming =
            phi.getIncomingValueForBlock(state->trace.back());
        initial_defined = initial_defined && state->evaluate(incoming).defined();
    }
    if (decide_unsat(state->get_path_condition().as_expr() &&
                     !initial_defined) != z3::unsat) {
        spdlog::debug(
            "[path-expr] loop {} incoming values are not proved defined",
            name);
        return std::nullopt;
    }
    const std::string id =
        std::to_string(state->session.next_path_expression_id());
    const z3::expr count =
        context.int_const(("ari_path_k_" + id).c_str());
    const z3::expr probe =
        context.int_const(("ari_path_t_" + id).c_str());
    z3::expr_vector source(context), at_probe(context), at_count(context);
    source.push_back(solution.induction_variable);
    at_probe.push_back(probe);
    at_count.push_back(count);
    z3::expr_vector trajectory(context), values_at_count(context);
    const unsigned period = candidate.star.size();
    const auto periodic_values = [&](const z3::expr& steps) {
        z3::expr_vector from(context), to(context), values(context);
        from.push_back(solution.induction_variable);
        to.push_back(steps / context.int_val(period));
        for (const auto& closed : solution.closed_forms)
            values.push_back(substitute(closed, solution.parameters, initial)
                                 .substitute(from, to).simplify());
        z3::expr_vector selected(context);
        for (const auto& value : values) selected.push_back(value);
        for (unsigned offset = 1; offset < period; ++offset) {
            const auto& edge = transitions.at(candidate.star[offset - 1]);
            z3::expr_vector next(context);
            for (const auto& update : edge.updates)
                next.push_back(substitute(update, solution.parameters, values).simplify());
            values = next;
            for (unsigned i = 0; i < selected.size(); ++i) {
                auto choice = z3::ite(steps % context.int_val(period) ==
                                         context.int_val(offset),
                                     values[i], selected[i]);
                selected.set(i, choice);
            }
        }
        return selected;
    };
    for (const z3::expr& closed : solution.closed_forms) {
        z3::expr from_current =
            substitute(closed, solution.parameters, initial);
        values_at_count.push_back(
            from_current.substitute(source, at_count).simplify());
        trajectory.push_back(
            from_current.substitute(source, at_probe).simplify());
    }

    // For period one retain the legacy, simpler arithmetic representation.
    if (period > 1) {
        trajectory = periodic_values(probe);
        values_at_count = periodic_values(count);
    }

    z3::expr other_enabled = context.bool_val(false);
    z3::expr guard_at_probe = context.bool_val(false);
    for (unsigned offset = 0; offset < period; ++offset) {
        const auto phase = probe % context.int_val(period) == context.int_val(offset);
        const auto symbol = candidate.star[offset];
        guard_at_probe = guard_at_probe || (phase && substitute(
            transitions.at(symbol).guard, solution.parameters, trajectory));
        for (const auto& [other, transition] : transitions)
            if (other != symbol)
                other_enabled = other_enabled || (phase && substitute(
                    transition.guard, solution.parameters, trajectory));
    }
    const z3::check_result stability = decide_unsat(
        state->get_path_condition().as_expr() && probe >= 0 &&
        other_enabled);
    spdlog::debug(
        "[path-expr] loop {} competing-path stability obligation: {}",
        name, check_result_name(stability));
    if (stability != z3::unsat) return std::nullopt;

    const z3::expr iterations = count >= 0 && z3::forall(
        probe, z3::implies(probe >= 0 && probe < count, guard_at_probe));
    // Keep all per-iteration guards, including arithmetic definedness. Pure
    // oracle choices have already been projected out and admit every count.
    spdlog::debug(
        "[path-expr] loop {} arbitrary-finite iteration constraints: {}",
        name, render_expr(iterations));

    state_ptr jump = std::make_shared<State>(*state);
    unsigned index = 0;
    for (llvm::PHINode& phi : loop->getHeader()->phis()) {
        jump->memory.put_temp(&phi, values_at_count[index++]);
    }
    jump->append_path_condition(iterations);
    jump->loop_path_expressions[loop].append_symbolic_power(
        candidate.star, period == 1 ? count : count / context.int_val(period));
    if (period > 1)
        for (unsigned offset = 0; offset + 1 < period; ++offset)
            jump->loop_path_expressions[loop].append_symbolic_power(
                PathWord{candidate.star[offset]},
                z3::ite(count % context.int_val(period) > context.int_val(offset),
                        context.int_val(1), context.int_val(0)));
    jump->loop_path_expressions[loop].candidates.clear();

    // Canonical witness for the existentially projected oracle: n continue
    // values followed by one exit value. Prior observed calls stay in order.
    const auto oracle_value = [&](bool nonzero) {
        return control.call->getType()->isIntegerTy(1)
            ? context.bool_val(nonzero) : context.int_val(nonzero ? 1 : 0);
    };
    const z3::expr sequence = z3::lambda(
        probe, z3::ite(probe < count,
                       oracle_value(control.continue_value),
                       oracle_value(!control.continue_value)));
    jump->nondet_calls.emplace_back(control.call, sequence, count + 1);
    jump->memory.put_temp(control.call,
                          oracle_value(!control.continue_value));
    jump->trace.push_back(loop->getHeader());
    jump->step_pc(AInstruction::create(
        &*control.exit_block->instructionsWithoutDebug().begin()));

    spdlog::debug(
        "[path-expr] loop {} oracle witness b(t)={}, call count={}",
        name, render_expr(sequence), render_expr(count + 1));
    spdlog::debug("[path-expr] loop {} accelerated exit values: {}", name,
                  render_expr_vector(values_at_count));
    state->session.note_path_acceleration();
    spdlog::info(
        "Accelerating loop {} with {} and arbitrary finite count {}",
        name, render_path_schema(candidate), render_expr(count));
    // Enumeration is exhaustive, there is no body exit, and no alternative
    // returning path is possible. Undefined continuations are excluded by
    // iterations; n=0 covers immediate exit. Infinite runs never reach the
    // postcondition, so all assertion-reaching continuations are represented.
    return PathAcceleration{jump, context.bool_val(true)};
}

// A bounded one-iteration explorer for the hierarchical profile. It executes
// scalar instructions with the usual semantics, but intercepts PHIs and child
// entries so it never invokes the legacy nested-loop recurrence collector.
// Assertions are obligations, not assumptions that can silently remove errors.
struct NestedIteration {
    std::vector<state_ptr> returning;
    state_ptr exiting;
    z3::expr errors;
    explicit NestedIteration(z3::context& ctx) : errors(ctx.bool_val(false)) {}
};

bool error_call(llvm::CallInst* call) {
    auto* callee = called_function(call);
    if (!callee) return false;
    const auto name = callee->getName();
    return name.contains("reach_error") || name == "__VERIFIER_error" ||
           name == "__assert_fail";
}

bool nested_instruction_supported(llvm::Instruction* instruction) {
    if (instruction->isDebugOrPseudoInst()) return true;
    for (const llvm::Use& use : instruction->operands()) {
        if (llvm::isa<llvm::UndefValue>(use.get()) ||
            llvm::isa<llvm::PoisonValue>(use.get())) return false;
        if (use->getType()->isIntegerTy() &&
            llvm::isa<llvm::Constant>(use.get()) &&
            !llvm::isa<llvm::ConstantInt>(use.get())) return false;
    }
    if (auto* call = llvm::dyn_cast<llvm::CallInst>(instruction)) {
        auto* callee = called_function(call);
        return error_call(call) ||
               (callee && callee->getName().ends_with("assert") &&
                call->arg_size() == 1 &&
                call->getArgOperand(0)->getType()->isIntegerTy());
    }
    if (llvm::isa<llvm::BranchInst>(instruction)) return true;
    if (!instruction->getType()->isIntegerTy()) return false;
    if (llvm::isa<llvm::ICmpInst>(instruction) &&
        !instruction->getOperand(0)->getType()->isIntegerTy()) return false;
    return llvm::isa<llvm::PHINode>(instruction) ||
           llvm::isa<llvm::ICmpInst>(instruction) ||
           llvm::isa<llvm::BinaryOperator>(instruction) ||
           llvm::isa<llvm::SelectInst>(instruction) ||
           llvm::isa<llvm::ZExtInst>(instruction) ||
           llvm::isa<llvm::SExtInst>(instruction) ||
           llvm::isa<llvm::TruncInst>(instruction);
}

std::optional<NestedIteration> explore_nested_iteration(
    llvm::Loop* loop, const state_ptr& initial, llvm::BasicBlock* normal_exit) {
    NestedIteration result(initial->z3ctx);
    std::queue<state_ptr> work;
    work.push(initial);
    std::size_t steps = 0;
    bool branched_children = false;
    auto& info = initial->session.analyses().get_LI(
        loop->getHeader()->getParent());
    while (!work.empty()) {
        // No unbounded exploration on an unsupported cycle or path explosion.
        if (++steps > 4096) return std::nullopt;
        state_ptr current = work.front();
        work.pop();
        if (current->status == State::TESTING) {
            auto feasible = decide_unsat(current->get_path_condition().as_expr());
            if (feasible == z3::unknown) return std::nullopt;
            if (feasible == z3::unsat) continue;
            current->status = State::RUNNING;
        }
        if (current->status == State::VERIFYING) {
            const Expression assertion = current->verification_condition;
            const z3::expr safe = assertion.defined() && assertion.as_expr();
            result.errors = result.errors ||
                (current->get_path_condition().as_expr() && !safe);
            current->State::append_path_condition(Expression(safe));
            current->status = State::RUNNING;
        }
        llvm::Instruction* instruction = current->pc->inst;
        llvm::BasicBlock* block = instruction->getParent();
        if (block == normal_exit && !current->trace.empty() &&
            current->trace.back() == loop->getHeader()) {
            if (result.exiting) return std::nullopt;
            result.exiting = current;
            continue;
        }
        if (loop->contains(block) && loop->isLoopLatch(block) &&
            instruction == block->getTerminator()) {
            if (!result.returning.empty() && !branched_children) return std::nullopt;
            if (result.returning.size() >= 64) return std::nullopt;
            result.returning.push_back(current);
            continue;
        }
        if (auto* call = llvm::dyn_cast<llvm::CallInst>(instruction);
            call && error_call(call)) {
            result.errors = result.errors ||
                            current->get_path_condition().as_expr();
            continue;
        }
        if (!nested_instruction_supported(instruction)) return std::nullopt;
        if (auto* phi = llvm::dyn_cast<llvm::PHINode>(instruction)) {
            llvm::Loop* child = info.getLoopFor(block);
            if (child && child != loop && child->getHeader() == block &&
                phi == &*block->phis().begin()) {
                auto jumps = PathExpressionAccelerator::accelerate_nested(
                    child, current);
                if (jumps.empty()) return std::nullopt;
                branched_children = branched_children || jumps.size() > 1;
                for (auto& jump : jumps) work.push(jump);
                continue;
            }
            if (current->trace.empty()) return std::nullopt;
            auto* incoming = phi->getIncomingValueForBlock(current->trace.back());
            if (!incoming) return std::nullopt;
            auto next = std::make_shared<State>(*current);
            next->execute_phi_bundle();
            work.push(next);
            continue;
        }
        for (auto& next : current->pc->execute(current)) work.push(next);
    }
    if (result.returning.empty() || !result.exiting) return std::nullopt;
    return result;
}

// Synthesize a functional counter for monotone <, <=, >, >= header tests.
// The template is never trusted: the summary domain below certifies the entire
// guarded trajectory and its exit, including no-wrap semantics in normal mode.
std::optional<z3::expr> nested_count_template(
    llvm::Loop* loop, const CompositeSolution& solution,
    const state_ptr& header_state, bool continue_on_true) {
    auto* branch = llvm::dyn_cast<llvm::BranchInst>(
        loop->getHeader()->getTerminator());
    auto* compare = llvm::dyn_cast<llvm::ICmpInst>(branch->getCondition());
    if (!compare) return std::nullopt;
    llvm::CmpInst::Predicate predicate = compare->getPredicate();
    if (!continue_on_true) predicate = llvm::CmpInst::getInversePredicate(predicate);
    llvm::Value* counter = compare->getOperand(0);
    llvm::Value* bound = compare->getOperand(1);
    auto* phi = llvm::dyn_cast<llvm::PHINode>(counter);
    if (!phi || phi->getParent() != loop->getHeader()) {
        std::swap(counter, bound);
        predicate = llvm::CmpInst::getSwappedPredicate(predicate);
        phi = llvm::dyn_cast<llvm::PHINode>(counter);
    }
    if (!phi || phi->getParent() != loop->getHeader()) return std::nullopt;
    unsigned index = 0;
    for (llvm::PHINode& item : loop->getHeader()->phis()) {
        if (&item == phi) break;
        ++index;
    }
    const z3::expr delta =
        (solution.one_step[index] - solution.parameters[index]).simplify();
    std::int64_t stride = 0;
    if (!delta.is_numeral() || !delta.is_numeral_i64(stride) || stride == 0 ||
        stride == std::numeric_limits<std::int64_t>::min()) return std::nullopt;
    z3::expr endpoint = header_state->evaluate(bound).as_expr();
    if (depends_on_any(endpoint, solution.parameters)) return std::nullopt;
    const bool less = predicate == llvm::CmpInst::ICMP_SLT ||
                      predicate == llvm::CmpInst::ICMP_ULT ||
                      predicate == llvm::CmpInst::ICMP_SLE ||
                      predicate == llvm::CmpInst::ICMP_ULE;
    const bool greater = predicate == llvm::CmpInst::ICMP_SGT ||
                         predicate == llvm::CmpInst::ICMP_UGT ||
                         predicate == llvm::CmpInst::ICMP_SGE ||
                         predicate == llvm::CmpInst::ICMP_UGE;
    if ((!less && !greater) || (less && stride < 0) ||
        (greater && stride > 0)) return std::nullopt;
    const bool inclusive = predicate == llvm::CmpInst::ICMP_SLE ||
                           predicate == llvm::CmpInst::ICMP_ULE ||
                           predicate == llvm::CmpInst::ICMP_SGE ||
                           predicate == llvm::CmpInst::ICMP_UGE;
    auto& ctx = endpoint.ctx();
    z3::expr distance = less ? endpoint - solution.parameters[index]
                            : solution.parameters[index] - endpoint;
    if (inclusive) distance = distance + 1;
    const z3::expr magnitude = ctx.int_val(stride > 0 ? stride : -stride);
    return z3::ite(distance > 0,
                   (distance + magnitude - 1) / magnitude,
                   ctx.int_val(0)).simplify();
}

bool has_foreign_symbols(const z3::expr& expression,
                         const z3::expr_vector& permitted) {
    if (expression.is_quantifier()) {
        return has_foreign_symbols(expression.body(), permitted);
    }
    if (!expression.is_app()) return false;
    if (expression.decl().decl_kind() == Z3_OP_UNINTERPRETED) {
        if (expression.num_args() != 0) return true;
        return std::none_of(permitted.begin(), permitted.end(),
            [&](const z3::expr& parameter) { return z3::eq(expression, parameter); });
    }
    for (const auto& argument : expression.args()) {
        if (has_foreign_symbols(argument, permitted)) return true;
    }
    return false;
}

// Flatten a loop nest into scalar CFG-edge maps. Location and branch guards
// are forgotten, so the union-star contains every concrete finite execution.
// PHI copy webs are coalesced only when SSA liveness proves no interference.
// This is register allocation, not an assumption based on source/debug names.
struct CoarseNestModel {
    std::map<llvm::Value*, unsigned> slots;
    z3::expr_vector parameters;
    z3::expr_vector initial;
    std::map<PathSymbol, ScalarPathTransition> transitions;
    explicit CoarseNestModel(z3::context& ctx) : parameters(ctx), initial(ctx) {}
};

std::optional<CoarseNestModel> flatten_coarse_nest(
    llvm::Loop* loop, const state_ptr& caller, bool canonical_initial = false) {
    auto& ctx = caller->z3ctx;
    CoarseNestModel model(ctx);
    std::vector<llvm::PHINode*> phis;
    // Stable numbering, with the outer entry coordinates first.
    for (auto& phi : loop->getHeader()->phis()) phis.push_back(&phi);
    for (auto& block : *loop->getHeader()->getParent()) {
        if (!loop->contains(&block) || &block == loop->getHeader()) continue;
        for (auto& phi : block.phis()) phis.push_back(&phi);
    }
    std::map<llvm::PHINode*, unsigned> index;
    for (unsigned i = 0; i < phis.size(); ++i) {
        if (!phis[i]->getType()->isIntegerTy() ||
            phis[i]->getType()->isIntegerTy(1)) return std::nullopt;
        index.emplace(phis[i], i);
    }
    using Live = std::set<unsigned>;
    std::map<llvm::BasicBlock*, Live> uses, defs, live_in, live_out;
    auto add_use = [&](Live& set, llvm::Value* value) {
        auto* phi = llvm::dyn_cast<llvm::PHINode>(value);
        if (phi && index.contains(phi)) set.insert(index.at(phi));
    };
    auto* function = loop->getHeader()->getParent();
    // Include users outside the nest: an old PHI live on exit must not be
    // overwritten by a coalesced inner PHI.
    for (auto& block : *function) {
        for (auto& instruction : block) {
            if (auto* phi = llvm::dyn_cast<llvm::PHINode>(&instruction)) {
                if (index.contains(phi)) defs[&block].insert(index.at(phi));
            } else if (!instruction.isDebugOrPseudoInst()) {
                for (const auto& operand : instruction.operands())
                    add_use(uses[&block], operand.get());
            }
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& block : *function) {
            Live out;
            for (auto* successor : llvm::successors(&block)) {
                out.insert(live_in[successor].begin(), live_in[successor].end());
                for (auto& phi : successor->phis())
                    add_use(out, phi.getIncomingValueForBlock(&block));
            }
            Live in;
            for (auto item : uses[&block])
                if (!defs[&block].contains(item)) in.insert(item);
            for (auto item : out)
                if (!defs[&block].contains(item)) in.insert(item);
            if (in != live_in[&block] || out != live_out[&block]) changed = true;
            live_in[&block] = std::move(in);
            live_out[&block] = std::move(out);
        }
    }
    std::set<std::pair<unsigned, unsigned>> interference;
    auto conflict = [&](unsigned a, unsigned b) {
        if (a != b) interference.emplace(std::min(a, b), std::max(a, b));
    };
    for (auto& block : *function) {
        Live after = live_out[&block];
        after.insert(uses[&block].begin(), uses[&block].end());
        after.insert(defs[&block].begin(), defs[&block].end());
        for (auto definition : defs[&block])
            for (auto value : after) conflict(definition, value);
    }
    std::vector<unsigned> group(phis.size());
    for (unsigned i = 0; i < group.size(); ++i) group[i] = i;
    for (unsigned i = 0; i < phis.size(); ++i) {
        for (auto& operand : phis[i]->incoming_values()) {
            auto* source = llvm::dyn_cast<llvm::PHINode>(operand.get());
            if (!source || !index.contains(source) ||
                source->getType() != phis[i]->getType()) continue;
            const unsigned a = group[i], b = group[index.at(source)];
            if (a == b) continue;
            bool safe = true;
            for (unsigned x = 0; x < group.size(); ++x)
                for (unsigned y = 0; y < group.size(); ++y)
                    if (group[x] == a && group[y] == b &&
                        interference.contains({std::min(x, y), std::max(x, y)}))
                        safe = false;
            if (safe) for (auto& item : group) if (item == b) item = a;
        }
    }
    const auto id = caller->session.next_path_expression_id();
    std::map<unsigned, unsigned> group_slot;
    auto allocate = [&](llvm::Value* value) {
        const unsigned slot = model.parameters.size();
        model.slots.emplace(value, slot);
        const auto name = "ari_coarse_nest_" + std::to_string(id) + "_" +
                          std::to_string(slot);
        model.parameters.push_back(ctx.int_const(name.c_str()));
        // Interior SSA coordinates are not live at the outer entry: every
        // real use is preceded by its definition/PHI edge assignment. Give
        // these ghost registers a canonical initial value. This still embeds
        // every concrete CFG execution, while unconstrained unrelated ghosts
        // would destroy invariants under location-forgetting interleavings.
        // The actual outer-header inputs are replaced below, never guessed.
        model.initial.push_back(canonical_initial ? ctx.int_val(0) :
            ctx.int_const((name + "_initial").c_str()));
        return slot;
    };
    for (unsigned i = 0; i < phis.size(); ++i) {
        const unsigned g = group[i];
        if (!group_slot.contains(g)) group_slot.emplace(g, allocate(phis[i]));
        else model.slots.emplace(phis[i], group_slot.at(g));
        if (phis[i]->getParent() == loop->getHeader()) {
            auto* incoming = phis[i]->getIncomingValueForBlock(caller->trace.back());
            if (!incoming) return std::nullopt;
            auto value = caller->evaluate(incoming).as_expr();
            model.initial.set(group_slot.at(g), value);
        }
    }
    // Carry non-PHI SSA results that cross a block boundary. A PHI operand
    // computed in its predecessor is evaluated on that edge instead.
    for (auto* block : loop->blocks()) {
        for (auto& instruction : *block) {
            if (llvm::isa<llvm::PHINode>(instruction) ||
                instruction.isDebugOrPseudoInst() || instruction.getType()->isVoidTy()) continue;
            bool crosses = false;
            for (auto* user : instruction.users()) {
                auto* use = llvm::dyn_cast<llvm::Instruction>(user);
                if (!use || use->isDebugOrPseudoInst()) continue;
                if (auto* phi = llvm::dyn_cast<llvm::PHINode>(use)) {
                    for (unsigned j = 0; j < phi->getNumIncomingValues(); ++j)
                        if (phi->getIncomingValue(j) == &instruction &&
                            phi->getIncomingBlock(j) != block) crosses = true;
                } else if (use->getParent() != block) crosses = true;
            }
            if (crosses) {
                if (!instruction.getType()->isIntegerTy() ||
                    instruction.getType()->isIntegerTy(1)) return std::nullopt;
                allocate(&instruction);
            }
        }
    }
    std::size_t steps = 0;
    for (auto& block : *function) {
        if (!loop->contains(&block)) continue;
        auto* branch = llvm::dyn_cast<llvm::BranchInst>(block.getTerminator());
        if (!branch) return std::nullopt;
        auto initial = std::make_shared<State>(caller->session, ctx,
            AInstruction::create(&*block.getFirstNonPHIOrDbg()), nullptr,
            caller->memory, ctx.bool_val(true), trace_ty{}, State::RUNNING);
        initial->summary_arithmetic = true;
        for (const auto& [value, slot] : model.slots)
            initial->memory.put_temp(value, model.parameters[slot]);
        std::queue<state_ptr> work;
        work.push(initial);
        while (!work.empty()) {
            if (++steps > 4096) return std::nullopt; // never accept a partial map set
            auto current = work.front(); work.pop();
            // Deliberately include infeasible choices. This is an
            // over-approximation, unlike precise guarded path extraction.
            if (current->status == State::TESTING ||
                current->status == State::VERIFYING) current->status = State::RUNNING;
            if (current->status != State::RUNNING) return std::nullopt;
            if (current->pc->inst != branch) {
                auto* instruction = current->pc->inst;
                if (!nested_instruction_supported(instruction) &&
                    !llvm::isa<llvm::CallInst>(instruction)) return std::nullopt;
                for (auto& next : current->pc->execute(current)) work.push(next);
                continue;
            }
            for (unsigned edge = 0; edge < branch->getNumSuccessors(); ++edge) {
                auto* target = branch->getSuccessor(edge);
                if (!loop->contains(target)) continue; // exits are replayed separately
                z3::expr_vector updates(ctx);
                for (const auto& parameter : model.parameters) updates.push_back(parameter);
                for (const auto& [value, slot] : model.slots) {
                    auto* definition = llvm::cast<llvm::Instruction>(value);
                    if (definition->getParent() == &block &&
                        !llvm::isa<llvm::PHINode>(definition)) {
                        auto update = current->evaluate(value).as_expr();
                        updates.set(slot, update);
                    }
                }
                // Parallel predecessor-edge assignment, before any PHI write.
                for (auto& phi : target->phis()) {
                    auto* incoming = phi.getIncomingValueForBlock(&block);
                    if (!incoming) return std::nullopt;
                    auto update = current->evaluate(incoming).as_expr();
                    updates.set(model.slots.at(&phi), update);
                }
                std::vector<PathDecisionEvent> events = current->path_decisions;
                events.push_back({reinterpret_cast<std::uintptr_t>(branch), edge});
                const auto symbol = caller->session.intern_loop_path(loop, events);
                if (model.transitions.contains(symbol)) return std::nullopt;
                bool duplicate = false;
                for (const auto& [existing_symbol, existing] : model.transitions) {
                    (void)existing_symbol;
                    bool same = true;
                    for (unsigned j = 0; j < updates.size(); ++j)
                        same = same && z3::eq(updates[j].simplify(), existing.updates[j].simplify());
                    duplicate = duplicate || same;
                }
                if (!duplicate)
                    model.transitions.emplace(symbol, ScalarPathTransition(ctx.bool_val(true), updates));
                spdlog::debug("[coarse-path] outer loop {} flattened edge {} -> {} "
                              "as p{}: update={}", loop_name(loop), block.getName().str(),
                              target->getName().str(), symbol, render_expr_vector(updates));
            }
        }
    }
    if (model.transitions.empty()) return std::nullopt;
    spdlog::debug("[coarse-path] outer loop {} flattened nest: {} scalar slot(s), "
                  "{} nondeterministic CFG-edge map(s)", loop_name(loop),
                  model.parameters.size(), model.transitions.size());
    return model;
}

// A flattened CFG carries SSA ghost registers as well as source coordinates.
// Their initialized equality web can be jointly inductive even when no single
// equality is preserved on arbitrary unrelated register values. Houdini-style
// elimination keeps only entry-valid equalities certified under EVERY map.
// These are degree-one, zero-growth polynomial observables, not assumed copies.
std::vector<z3::expr> certify_entry_equalities(
    const z3::expr_vector& parameters, const z3::expr_vector& initial,
    const std::map<PathSymbol, ScalarPathTransition>& transitions) {
    std::vector<z3::expr> candidates;
    if (parameters.size() > 32) return candidates;
    auto& ctx = parameters.ctx();
    for (unsigned i = 0; i < parameters.size(); ++i)
        for (unsigned j = i + 1; j < parameters.size(); ++j)
            if ((initial[i] == initial[j]).simplify().is_true())
                candidates.push_back(parameters[i] == parameters[j]);
    unsigned queries = 0;
    while (!candidates.empty()) {
        z3::expr joint = ctx.bool_val(true);
        for (const auto& candidate : candidates) joint = joint && candidate;
        std::vector<z3::expr> surviving;
        for (const auto& candidate : candidates) {
            bool inductive = true;
            for (const auto& [symbol, transition] : transitions) {
                (void)symbol;
                if (++queries > 512) return {};
                // No trusted lemmas or path guards are needed for this check.
                if (decide_unsat(joint && !substitute(
                        candidate, parameters, transition.updates)) != z3::unsat) {
                    inductive = false;
                    break;
                }
            }
            if (inductive) surviving.push_back(candidate);
        }
        if (surviving.size() == candidates.size()) return surviving;
        candidates = std::move(surviving);
    }
    return candidates;
}

std::optional<z3::expr> nested_payload_ite(const z3::expr& expression) {
    if (!expression.is_app()) return std::nullopt;
    if (expression.decl().decl_kind() == Z3_OP_ITE) return expression;
    for (const auto& argument : expression.args())
        if (auto conditional = nested_payload_ite(argument)) return conditional;
    return std::nullopt;
}

std::shared_ptr<NestedPathSummary> copy_nested_summary(
    const std::shared_ptr<NestedPathSummary>& source) {
    auto result = std::make_shared<NestedPathSummary>(*source);
    // Z3 AST vectors share storage on copy; each branch needs its own updates.
    result->values = z3::expr_vector(source->parameters.ctx());
    for (const auto& value : source->values) result->values.push_back(value);
    return result;
}

// Split all piecewise counts/live-outs into correlated guarded exit branches.
// Rewrite the SAME conditional everywhere, including the certification domain.
// No entry assumptions are used to construct this reusable case partition.
bool append_nested_cases(const std::shared_ptr<NestedPathSummary>& leaf,
                         const std::shared_ptr<NestedPathSummary>& aggregate) {
    std::vector<std::shared_ptr<NestedPathSummary>> pending{leaf};
    while (!pending.empty()) {
        auto current = pending.back();
        pending.pop_back();
        auto conditional = nested_payload_ite(current->count);
        for (const auto& value : current->values) {
            if (conditional) break;
            conditional = nested_payload_ite(value);
        }
        if (!conditional) {
            aggregate->cases.push_back(current);
            aggregate->domain = aggregate->domain || current->domain;
        } else {
            for (unsigned arm = 1; arm <= 2; ++arm) {
                auto branch = copy_nested_summary(current);
                z3::expr_vector from(current->parameters.ctx()), to(current->parameters.ctx());
                from.push_back(*conditional);
                to.push_back(conditional->arg(arm));
                const auto guard = arm == 1 ? conditional->arg(0) : !conditional->arg(0);
                branch->domain = (guard && substitute(branch->domain, from, to)).simplify();
                branch->count = substitute(branch->count, from, to).simplify();
                for (unsigned i = 0; i < branch->values.size(); ++i) {
                    auto value = substitute(branch->values[i], from, to).simplify();
                    branch->values.set(i, value);
                }
                if (!branch->domain.is_false()) pending.push_back(branch);
            }
        }
        if (pending.size() + aggregate->cases.size() > 64) return false;
    }
    return true;
}

std::shared_ptr<NestedPathSummary> build_nested_summary(
    llvm::Loop* loop, const state_ptr& caller) {
    auto* header = loop->getHeader();
    auto* branch = llvm::dyn_cast<llvm::BranchInst>(header->getTerminator());
    if (!branch || !branch->isConditional() || !loop->getLoopLatch() ||
        header->phis().empty()) return nullptr;
    const bool first_inside = loop->contains(branch->getSuccessor(0));
    if (first_inside == loop->contains(branch->getSuccessor(1))) return nullptr;
    llvm::BasicBlock* exit = branch->getSuccessor(first_inside ? 1 : 0);
    auto& ctx = caller->z3ctx;
    auto summary = std::make_shared<NestedPathSummary>(ctx);
    summary->exit = exit;
    const auto id = caller->session.next_path_expression_id();
    auto initial = std::make_shared<State>(caller->session, ctx,
        AInstruction::create(&*header->getFirstNonPHIOrDbg()), nullptr,
        caller->memory, ctx.bool_val(true), trace_ty{}, State::RUNNING);
    initial->summary_arithmetic = true;
    std::set<llvm::Value*> inputs;
    for (llvm::PHINode& phi : header->phis()) {
        if (!phi.getType()->isIntegerTy() || phi.getType()->isIntegerTy(1)) return nullptr;
        const auto parameter = ctx.int_const(get_z3_name(phi.getName().str()).c_str());
        summary->inputs.push_back(&phi);
        summary->parameters.push_back(parameter);
        initial->memory.put_temp(&phi, parameter);
        inputs.insert(&phi);
    }
    // Make every external scalar operand symbolic before exploring. A cached
    // child must not specialize to the first outer iteration (e.g. x == 0).
    for (auto* block : loop->blocks()) {
        for (auto& instruction : *block) {
            for (const llvm::Use& use : instruction.operands()) {
                llvm::Value* value = use.get();
                auto* definition = llvm::dyn_cast<llvm::Instruction>(value);
                if (!value->getType()->isIntegerTy() ||
                    llvm::isa<llvm::Constant>(value) || inputs.contains(value) ||
                    (definition && loop->contains(definition->getParent()))) continue;
                const std::string name = "ari_nested_input_" + std::to_string(id) +
                                         "_" + std::to_string(summary->inputs.size());
                const z3::expr parameter = value->getType()->isIntegerTy(1)
                    ? ctx.bool_const(name.c_str()) : ctx.int_const(name.c_str());
                summary->inputs.push_back(value);
                summary->parameters.push_back(parameter);
                initial->memory.put_temp(value, parameter);
                inputs.insert(value);
            }
        }
    }
    spdlog::debug("[nested-path] loop {} building parametric body bottom-up", loop_name(loop));
    auto iteration = explore_nested_iteration(loop, initial, exit);
    if (!iteration) return nullptr;
    auto aggregate = copy_nested_summary(summary);
    aggregate->domain = ctx.bool_val(false);
    for (const auto &returning : iteration->returning) {
        summary = copy_nested_summary(aggregate);
        summary->cases.clear();
        z3::expr_vector updates(ctx);
        z3::expr guard = returning->get_path_condition().as_expr();
        auto *latch = returning->pc->get_block();
        for (auto &phi : header->phis()) {
            auto update = returning->evaluate(phi.getIncomingValueForBlock(latch));
            updates.push_back(update.as_expr());
            guard = guard && update.defined();
        }
        // A macro symbol denotes the complete body, including certified child exits.
        const PathSymbol symbol =
            path_symbol_for_final_state(loop, std::make_shared<LoopState>(*returning));
        summary->word = {symbol};
        std::map<PathSymbol, ScalarPathTransition> transitions;
        transitions.emplace(symbol, ScalarPathTransition(guard, updates));
        auto solution = solve_composite(loop, initial, summary->word, transitions);
        if (!solution)
            return nullptr;
        auto count = nested_count_template(loop, *solution, iteration->exiting, first_inside);
        if (!count)
            return nullptr;
        summary->count = *count;
        const z3::expr probe = ctx.int_const(("ari_nested_t_" + std::to_string(id)).c_str());
        z3::expr_vector induction(ctx), at_probe(ctx), at_count(ctx);
        induction.push_back(solution->induction_variable);
        at_probe.push_back(probe);
        at_count.push_back(*count);
        z3::expr_vector trajectory(ctx), final_values(ctx);
        for (auto closed : solution->closed_forms) {
            trajectory.push_back(closed.substitute(induction, at_probe).simplify());
            final_values.push_back(closed.substitute(induction, at_count).simplify());
        }
        const z3::expr returning_guard =
            substitute(solution->guard, solution->parameters, trajectory);
        const z3::expr exit_guard = substitute(iteration->exiting->get_path_condition().as_expr(),
                                               solution->parameters, final_values);
        const z3::expr errors = substitute(iteration->errors, solution->parameters, trajectory);
        summary->domain =
            *count >= 0 && exit_guard &&
            z3::forall(probe, z3::implies(probe >= 0 && probe < *count, returning_guard)) &&
            z3::forall(probe, z3::implies(probe >= 0 && probe <= *count, !errors));
        // Header-exit loops expose only header definitions. Reject unsupported
        // live-outs rather than accidentally reading stale caller memory on n=0.
        for (auto *block : loop->blocks()) {
            for (auto &instruction : *block) {
                if (!instruction.getType()->isIntegerTy())
                    continue;
                const bool live_out =
                    (llvm::isa<llvm::PHINode>(instruction) && block == header) ||
                    std::any_of(instruction.user_begin(), instruction.user_end(),
                                [&](const llvm::User *user) {
                                    auto *consumer = llvm::dyn_cast<llvm::Instruction>(user);
                                    return consumer && !loop->contains(consumer->getParent());
                                });
                if (!live_out)
                    continue;
                if (block != header)
                    return nullptr;
                auto value = iteration->exiting->evaluate(&instruction);
                summary->outputs.push_back(&instruction);
                summary->values.push_back(
                    substitute(value.as_expr(), solution->parameters, final_values).simplify());
                summary->domain = summary->domain &&
                                  substitute(value.defined(), solution->parameters, final_values);
            }
        }
        // Simplifying a reusable domain for all mathematical inputs can be much
        // harder than proving it at a concrete entry, especially with modulo 2^w.
        // Spend only a small optional QE budget; the exact domain is the fallback.
        if (auto reduced = eliminate_quantifiers(summary->domain, 100))
            summary->domain = *reduced;
        summary->domain = summary->domain.simplify();
        // No dynamic choices, caller-specialized constants, or solver auxiliaries
        // may leak into a parametric transition reused by its parent.
        if (has_foreign_symbols(summary->domain, summary->parameters) ||
            has_foreign_symbols(summary->count, summary->parameters))
            return nullptr;
        for (const auto &value : summary->values) {
            if (has_foreign_symbols(value, summary->parameters))
                return nullptr;
        }
        spdlog::debug("[nested-path] loop {} certified exit: count={}, domain={}, values={}",
                      loop_name(loop), render_expr(summary->count), render_expr(summary->domain),
                      render_expr_vector(summary->values));
        if (!append_nested_cases(summary, aggregate))
            return nullptr;
    }
    aggregate->domain = aggregate->domain.simplify();
    caller->session.note_nested_path_summary();
    spdlog::debug("[nested-path] loop {} encoded {} explicit guarded exit branch(es)",
                  loop_name(loop), aggregate->cases.size());
    return aggregate;
}

} // namespace

std::vector<state_ptr> PathExpressionAccelerator::accelerate_nested(
    llvm::Loop* loop, const state_ptr& state) {
    if (!loop || state->trace.empty() || loop->contains(state->trace.back())) return {};
    auto& cache = state->session.nested_path_summaries();
    auto found = cache.find(loop);
    std::shared_ptr<NestedPathSummary> summary;
    if (found != cache.end()) {
        summary = found->second;
    } else {
        try {
            summary = build_nested_summary(loop, state);
        } catch (const VerifierError& error) {
            spdlog::debug("[nested-path] loop {} symbolic probe unsupported: {}",
                          loop_name(loop), error.what());
        } catch (const z3::exception& error) {
            spdlog::debug("[nested-path] loop {} symbolic probe failed: {}",
                          loop_name(loop), error.msg());
        }
        if (!summary) {
            spdlog::debug("[nested-path] loop {} unsupported or uncertified; retaining ordinary execution", loop_name(loop));
            return {};
        }
        cache.emplace(loop, summary);
    }
    z3::expr_vector actual(state->z3ctx);
    z3::expr defined = state->z3ctx.bool_val(true);
    for (llvm::Value* input : summary->inputs) {
        if (auto* phi = llvm::dyn_cast<llvm::PHINode>(input);
            phi && phi->getParent() == loop->getHeader()) {
            input = phi->getIncomingValueForBlock(state->trace.back());
        }
        if (!input || (!llvm::isa<llvm::ConstantInt>(input) &&
                       !state->memory.get_object(input))) return {};
        const auto value = state->evaluate(input);
        actual.push_back(value.as_expr());
        defined = defined && value.defined();
    }
    const z3::expr domain = defined && substitute(summary->domain, summary->parameters, actual);
    auto proof = decide_unsat(state->get_path_condition().as_expr() && !domain);
    spdlog::debug("[nested-path] loop {} entry safety/coverage certificate: {}", loop_name(loop), check_result_name(proof));
    if (proof != z3::unsat) return {};
    std::vector<state_ptr> jumps;
    unsigned case_index = 1;
    for (const auto &branch : summary->cases) {
        ++case_index;
        const auto branch_domain =
            substitute(branch->domain, summary->parameters, actual).simplify();
        if (branch_domain.is_false())
            continue;
        state_ptr jump = std::make_shared<State>(*state);
        jump->append_path_condition(branch_domain);
        for (unsigned i = 0; i < branch->outputs.size(); ++i) {
            auto closed = substitute(branch->values[i], summary->parameters, actual).simplify();
            if (has_rational_cast(closed)) {
                z3::expr value(state->z3ctx, Z3_mk_fresh_const(state->z3ctx, "ari_exact_nested",
                                                               state->z3ctx.int_sort()));
                jump->append_path_condition(integerize_rational_query(
                    z3::to_real(value) == exact_closed_form(closed), false));
                jump->memory.put_temp(branch->outputs[i], value);
            } else
                jump->memory.put_temp(branch->outputs[i], closed);
        }
        const z3::expr count = substitute(branch->count, summary->parameters, actual).simplify();
        // Each invocation starts a new dynamic prefix. Retaining an inner loop's
        // previous cursor mixes unrelated outer iterations into its path symbol.
        jump->loop_path_expressions.erase(loop);
        jump->loop_path_expressions[loop].append_symbolic_power(branch->word, count);
        jump->path_decisions.push_back(
            {reinterpret_cast<std::uintptr_t>(loop->getHeader()), case_index});
        jump->trace.push_back(loop->getHeader());
        jump->step_pc(AInstruction::create(&*summary->exit->instructionsWithoutDebug().begin()));
        jump->status = State::TESTING;
        state->session.note_path_acceleration();
        spdlog::info("[nested-path] Accelerating loop {} bottom-up with {} repetition(s)",
                     loop_name(loop), render_expr(count));
        jumps.push_back(jump);
    }
    return jumps;
}

bool PathExpressionAccelerator::has_nondeterministic_header_control(
    llvm::Loop* loop) {
    return loop && find_nondet_loop_control(loop).has_value();
}

std::optional<std::string> PathExpressionAccelerator::precise_rejection_reason(
    llvm::Loop* loop) {
    if (!loop) return "missing loop";
    return unsupported_profile_reason(loop, find_nondet_loop_control(loop));
}

std::vector<unsigned> PathExpressionAccelerator::coarse_degrees() {
    if (const char* raw = std::getenv("ARITHEXE_COARSE_POLY_EXPR_DEGREE")) {
        char* end = nullptr;
        const auto parsed = std::strtoul(raw, &end, 10);
        if (raw[0] && raw[0] != '-' && end && !*end && parsed > 0 &&
            parsed <= std::numeric_limits<unsigned>::max()) {
            return {static_cast<unsigned>(parsed)};
        }
    }
    return {2, 3};
}

state_ptr PathExpressionAccelerator::accelerate_exit_guard(
    llvm::Loop* loop, const state_ptr& entry) {
    if (!loop || loop->getParentLoop() || !loop->getSubLoops().empty() ||
        entry->is_summarizing() || entry->coarse_refining ||
        entry->coarse_fallback_state || entry->trace.empty() ||
        loop->contains(entry->trace.back())) return nullptr;
    if (const char* enabled = std::getenv("ARITHEXE_EXIT_GUARD_FALLBACK");
        enabled && std::string(enabled) == "0") return nullptr;
    auto* header = loop->getHeader();
    auto* branch = llvm::dyn_cast<llvm::BranchInst>(header->getTerminator());
    auto* exit = loop->getExitBlock();
    llvm::SmallVector<llvm::BasicBlock*, 4> exiting;
    loop->getExitingBlocks(exiting);
    if (!branch || !branch->isConditional() || !exit ||
        header->phis().empty() || exiting.size() != 1 || exiting[0] != header ||
        (branch->getSuccessor(0) != exit && branch->getSuccessor(1) != exit))
        return nullptr;
    for (auto& phi : header->phis())
        if (!phi.getType()->isIntegerTy() || phi.getType()->isIntegerTy(1))
            return nullptr;
    for (auto* block : loop->blocks()) for (auto& instruction : *block) {
        if (instruction.isDebugOrPseudoInst()) continue;
        if (!instruction.getType()->isIntegerTy() &&
            !instruction.getType()->isVoidTy()) return nullptr;
        // Fresh, control-only body oracles have no observable side effects.
        // Other skipped calls include assertions, error/abort, assumptions,
        // nondeterministic data, intrinsics with side effects, and callees.
        if (auto* call = llvm::dyn_cast<llvm::CallInst>(&instruction);
            call && block != header && pure_nondet_control(call)) continue;
        if (llvm::isa<llvm::CallBase>(instruction) ||
            instruction.mayReadOrWriteMemory() ||
            (instruction.mayHaveSideEffects() && !llvm::isa<llvm::BranchInst>(instruction)))
            return nullptr;
        // Every live-out must be recomputed in the header from havoced PHIs.
        // Other body temporaries cannot retain stale pre-summary values.
        if (block != header) for (auto* user : instruction.users()) {
            auto* use = llvm::dyn_cast<llvm::Instruction>(user);
            if (!use || !loop->contains(use->getParent())) return nullptr;
        }
    }
    auto jump = std::make_shared<State>(*entry);
    for (auto& phi : header->phis()) {
        // Use Z3's fresh symbol directly, never a reusable fixed name.
        auto fresh = z3::expr(entry->z3ctx, Z3_mk_fresh_const(
            entry->z3ctx, "ari_safety_exit", entry->z3ctx.int_sort()));
        jump->memory.put_temp(&phi, fresh);
    }
    jump->step_pc(AInstruction::create(&*header->getFirstNonPHIOrDbg()));
    while (jump->pc->inst != header->getTerminator()) {
        // Keep header selects as expressions in this safety-only guard query.
        // This is not a polynomial transition sent to the recurrence solver.
        if (auto* selection = llvm::dyn_cast<llvm::SelectInst>(jump->pc->inst)) {
            if (!selection->getType()->isIntegerTy(1)) return nullptr;
            const auto condition = jump->evaluate(selection->getCondition());
            const auto yes = jump->evaluate(selection->getTrueValue());
            const auto no = jump->evaluate(selection->getFalseValue());
            jump->append_path_condition(condition.defined() &&
                ((condition.as_expr() && yes.defined()) ||
                 (!condition.as_expr() && no.defined())));
            jump->memory.put_temp(selection,
                ((condition.as_expr() && yes.as_expr()) ||
                 (!condition.as_expr() && no.as_expr())).simplify());
            jump->step_pc();
            continue;
        }
        auto successors = jump->pc->execute(jump);
        if (successors.size() != 1 || successors.front()->status != State::RUNNING)
            return nullptr;
        jump = successors.front();
    }
    const auto guard = jump->evaluate(branch->getCondition());
    jump->append_path_condition(guard.defined());
    jump->append_path_condition(branch->getSuccessor(0) == exit
        ? guard.as_expr() : !guard.as_expr());
    jump->trace.push_back(header);
    jump->step_pc(AInstruction::create(&*exit->begin()));
    jump->status = State::RUNNING;
    jump->is_over_approx = true;
    jump->counterexample_complete = false;
    jump->loop_path_expressions.erase(loop);
    // Reuse the entry-rooted safety-proof transaction. Degree zero denotes
    // this havoc summary; failed proofs fall back to polynomial degrees 2/3
    // and ultimately the untouched exact entry, never a filtered residual.
    jump->coarse_fallback_state = std::make_shared<State>(*entry);
    jump->coarse_loop = loop;
    jump->coarse_degree = 0;
    spdlog::info("[safety-exit] loop {}: independent finite-exit over-approximation (no termination obligation)",
                 loop_name(loop));
    return jump;
}

state_ptr PathExpressionAccelerator::accelerate_coarse(
    llvm::Loop* loop, const state_ptr& state, unsigned degree) {
    if (!loop || loop->getParentLoop() || state->is_summarizing()) return nullptr;
    const auto control = find_nondet_loop_control(loop, false);
    if (auto reason = unsupported_profile_reason(loop, control, true)) {
        spdlog::debug("[coarse-path] loop {} rejected: {}",
                      loop_name(loop), *reason);
        return nullptr;
    }
    llvm::SmallVector<llvm::BasicBlock*, 4> exiting;
    loop->getExitingBlocks(exiting);
    auto* header = loop->getHeader();
    auto* branch = llvm::dyn_cast<llvm::BranchInst>(header->getTerminator());
    auto* exit = loop->getExitBlock();
    if (!branch || header->phis().empty() || state->trace.empty()) return nullptr;
    const bool header_only_exit = exiting.size() == 1 &&
        exiting.front() == header && exit && branch->isConditional() &&
        (branch->getSuccessor(0) == exit || branch->getSuccessor(1) == exit);
    bool internal_assertion = false;
    for (auto* block : loop->blocks()) for (auto& instruction : *block) {
        if (auto* call = llvm::dyn_cast<llvm::CallInst>(&instruction)) {
            auto* callee = called_function(call);
            internal_assertion = internal_assertion ||
                (callee && callee->getName().ends_with("assert"));
        }
    }
    // Keep the fast header-only route where it is complete. Otherwise every
    // terminal fragment and assertion must be replayed, not just one exit.
    const bool boundary_replay = !header_only_exit || internal_assertion;

    auto& context = state->z3ctx;
    auto initial = current_header_values(loop, state, true);
    unsigned numeric_phis = 0;
    for (auto& phi : header->phis()) if (!control_only_boolean_phi(phi)) ++numeric_phis;
    if (initial.size() != numeric_phis) return nullptr;
    z3::expr initial_defined = context.bool_val(true);
    for (auto& phi : header->phis()) {
        auto* incoming = phi.getIncomingValueForBlock(state->trace.back());
        initial_defined = initial_defined && state->evaluate(incoming).defined();
    }
    if (decide_unsat(state->get_path_condition().as_expr() &&
                     !initial_defined, true) != z3::unsat) return nullptr;

    std::optional<CoarseNestModel> nest;
    z3::expr_vector parameters(context);
    std::optional<std::map<PathSymbol, ScalarPathTransition>> transitions;
    if (!loop->getSubLoops().empty()) {
        nest = flatten_coarse_nest(loop, state, boundary_replay);
        if (!nest) return nullptr;
        initial = nest->initial;
        parameters = nest->parameters;
        transitions = nest->transitions;
    } else {
        for (auto& phi : header->phis()) {
            if (control_only_boolean_phi(phi)) continue;
            parameters.push_back(context.int_const(get_z3_name(phi.getName().str()).c_str()));
        }
        transitions = enumerate_transitions(loop, state, control, true);
    }
    if (!transitions || transitions->empty()) return nullptr;

    // Guarded Houdini: these inequalities are independent of the polynomial
    // equalities and are admitted only after entry and preservation proofs.
    std::vector<z3::expr> nonnegative;
    if (!nest) {
        for (unsigned i = 0; i < parameters.size(); ++i)
            if (decide_unsat(state->get_path_condition().as_expr() &&
                             !(initial[i] >= 0), false) == z3::unsat)
                nonnegative.push_back(parameters[i] >= 0);
        bool changed;
        do {
            changed = false;
            auto joint = context.bool_val(true);
            for (const auto& candidate : nonnegative) joint = joint && candidate;
            std::vector<z3::expr> retained;
            for (const auto& candidate : nonnegative) {
                bool valid = true;
                for (const auto& [symbol, transition] : *transitions)
                    if (decide_unsat(joint && transition.guard &&
                        !substitute(candidate, parameters, transition.updates), false) != z3::unsat) {
                        valid = false;
                        break;
                    }
                if (valid) retained.push_back(candidate); else changed = true;
            }
            nonnegative = std::move(retained);
        } while (changed);
        for (const auto& invariant : nonnegative)
            spdlog::debug("[coarse-path] certified guarded invariant {}",
                          render_expr(invariant));
    }

    // Only fixed external symbols and scalar coordinates may occur in a map.
    // In particular fresh nondet calls may select a branch, but never act as
    // a supposedly fixed polynomial data input across arbitrary repetitions.
    if (nest) {
        z3::expr_vector permitted(context);
        for (const auto& parameter : parameters) permitted.push_back(parameter);
        const auto collect = [&](const z3::expr& expr, const auto& self) -> void {
            if (!expr.is_app()) return;
            if (expr.decl().decl_kind() == Z3_OP_UNINTERPRETED && expr.num_args() == 0)
                permitted.push_back(expr);
            for (const auto& argument : expr.args()) self(argument, self);
        };
        for (auto* block : loop->blocks()) {
            for (auto& instruction : *block) {
                if (instruction.isDebugOrPseudoInst()) continue;
                for (const auto& operand : instruction.operands()) {
                    auto* value = operand.get();
                    auto* definition = llvm::dyn_cast<llvm::Instruction>(value);
                    if (value->getType()->isIntegerTy() &&
                        (!definition || !loop->contains(definition->getParent())))
                        collect(state->evaluate(value).as_expr(), collect);
                }
            }
        }
        for (const auto& [symbol, transition] : *transitions) {
            (void)symbol;
            for (const auto& update : transition.updates)
                if (has_foreign_symbols(update, permitted)) return nullptr;
        }
    }

    // Do not erase modulo/BV semantics to get a polynomial map. The fixed-
    // width profile can only use this fallback when the extracted updates
    // already have polynomial form; otherwise execution remains ordinary.
    const auto polynomial = [](const z3::expr& expression,
                                const auto& self) -> bool {
        if (expression.is_numeral()) return true;
        if (!expression.is_app() || !expression.is_int()) return false;
        const auto kind = expression.decl().decl_kind();
        if (kind == Z3_OP_UNINTERPRETED) return expression.num_args() == 0;
        if (kind != Z3_OP_ADD && kind != Z3_OP_SUB &&
            kind != Z3_OP_MUL && kind != Z3_OP_UMINUS) return false;
        for (const auto& argument : expression.args()) {
            if (!self(argument, self)) return false;
        }
        return true;
    };
    for (const auto& [symbol, transition] : *transitions) {
        (void)symbol;
        for (auto update : transition.updates) {
            if (!polynomial(update.simplify(), polynomial)) return nullptr;
        }
    }

    const std::string suffix =
        std::to_string(state->session.next_path_expression_id());
    z3::expr n = context.int_const(("ari_coarse_t_" + suffix).c_str());
    z3::expr count = context.int_const(("ari_coarse_k_" + suffix).c_str());
    z3::expr choice = context.int_const(("ari_coarse_choice_" + suffix).c_str());
    z3::expr_vector initial_parameters(context);
    z3::expr_vector functions(context), functions_zero(context);
    for (unsigned i = 0; i < parameters.size(); ++i) {
        const auto name = parameters[i].decl().name().str();
        initial_parameters.push_back(context.int_const(
            ("ari_coarse_init_" + suffix + "_" +
             std::to_string(i + 1)).c_str()));
        auto function = context.function(name.c_str(), context.int_sort(),
                                         context.int_sort());
        functions.push_back(function(n));
        functions_zero.push_back(function(0));
    }
    rec_solver solver(context);
    solver.set_ind_var(n);
    solver.add_initial_values(functions_zero, initial_parameters);
    std::vector<z3::expr> conditions;
    std::vector<rec_ty> equations;
    std::ostringstream language;
    language << '(';
    for (const auto& [symbol, transition] : *transitions) {
        if (!conditions.empty()) language << '|';
        language << 'p' << symbol;
        // Synthetic disjoint selectors only transport the maps through the
        // existing parser. COARSE synthesis ignores these and all guards.
        conditions.push_back(choice == context.int_val(
            static_cast<uint64_t>(conditions.size())));
        rec_ty update;
        for (unsigned i = 0; i < functions.size(); ++i) {
            update.emplace(functions[i].decl()(n + 1),
                substitute(transition.updates[i].simplify(),
                           parameters, functions));
        }
        equations.push_back(std::move(update));
    }
    language << ")*";
    // Cover every selector value, avoiding the parser's implicit stuttering
    // default branch, which would otherwise erase nonzero growth rates.
    conditions.back() = context.bool_val(true);
    spdlog::debug("[coarse-path] loop {} degree {} trying {} from {}",
                  loop_name(loop), degree, language.str(), render_expr_vector(initial));
    solver.set_eqs(conditions, equations);
    const auto entry_equalities = nest && boundary_replay
        ? certify_entry_equalities(parameters, initial, *transitions)
        : std::vector<z3::expr>{};
    closed_form_ty solved;
    // Preserve the full degree-2 then degree-3 worker ladder. Certified entry
    // equalities strengthen it and remain available if synthesis times out.
    if (solver.solve(true, degree)) solved = solver.get_exact_res();
    if (solved.empty() && entry_equalities.empty() && nonnegative.empty()) return nullptr;

    z3::expr_vector n_source(context), zero(context), next(context);
    n_source.push_back(n);
    zero.push_back(context.int_val(0));
    next.push_back(n + 1);
    z3::expr relation = context.bool_val(true);
    z3::expr base = context.bool_val(true);
    bool useful = !nonnegative.empty();
    for (const auto& equality : entry_equalities) {
        useful = true;
        relation = relation && equality;
        base = base && substitute(equality, parameters, initial);
        spdlog::debug("[coarse-path] certified entry equality {}",
                      render_expr(equality));
    }
    for (const auto& [observable, closed] : solved) {
        if (!depends_on_any(observable, parameters)) return nullptr;
        useful = true;
        auto exact = exact_closed_form(closed);
        relation = relation && observable == exact;
        base = base && substitute(observable, parameters, initial_parameters) ==
                       substitute(exact, n_source, zero);
        spdlog::debug("[coarse-path] observable {} = {}",
                      render_expr(observable), render_expr(closed));
    }
    if (!useful || decide_unsat(!base, true) != z3::unsat) return nullptr;
    // Joint inductiveness certifies also mutually dependent observables.
    // There are no branch guards in this check: it proves the union-star,
    // not separate solutions for A* and B*.
    for (const auto& [symbol, transition] : *transitions) {
        z3::expr step = context.bool_val(true);
        for (const auto& [observable, closed] : solved) {
            step = step && substitute(observable, parameters, transition.updates) ==
                           substitute(exact_closed_form(closed), n_source, next);
        }
        for (const auto& equality : entry_equalities)
            step = step && substitute(equality, parameters, transition.updates);
        const auto result = decide_unsat(n >= 0 && relation && !step, true);
        spdlog::debug("[coarse-path] loop {} branch p{} step certificate: {}",
                      loop_name(loop), symbol, check_result_name(result));
        if (result != z3::unsat) return nullptr;
    }

    auto jump = std::make_shared<State>(*state);
    z3::expr_vector post(context), count_vector(context);
    count_vector.push_back(count);
    for (unsigned i = 0; i < parameters.size(); ++i) {
        auto fresh = context.int_const(("ari_coarse_exit_" + suffix + "_" +
                                       std::to_string(i)).c_str());
        // Avoid distributing many shared zero-trip ITEs through nonlinear
        // observables in a flattened nest. State the same condition separately.
        post.push_back(nest ? fresh : z3::ite(count == 0, initial[i], fresh));
    }
    if (nest) {
        for (const auto& [value, slot] : nest->slots)
            jump->memory.put_temp(value, post[slot]);
    } else {
        unsigned i = 0;
        for (auto& phi : header->phis()) {
            if (control_only_boolean_phi(phi)) {
                auto fresh = z3::expr(context, Z3_mk_fresh_const(context,
                    "ari_coarse_control", context.bool_sort()));
                auto original = state->evaluate(phi.getIncomingValueForBlock(state->trace.back())).as_expr();
                jump->memory.put_temp(&phi, z3::ite(count == 0, original, fresh));
            } else jump->memory.put_temp(&phi, post[i++]);
        }
    }
    for (const auto& invariant : nonnegative)
        jump->append_path_condition(substitute(invariant, parameters, post));
    jump->append_path_condition(count >= 0);
    if (nest) {
        z3::expr zero_trip = context.bool_val(true);
        for (unsigned i = 0; i < post.size(); ++i)
            zero_trip = zero_trip && post[i] == initial[i];
        jump->append_path_condition(z3::implies(count == 0, zero_trip));
    }
    jump->append_path_condition(substitute(
        substitute(substitute(relation, parameters, post),
                   initial_parameters, initial), n_source, count_vector));
    // Every positive-length finite execution has a guarded last step. Keep
    // this necessary condition even though the intermediate word is unknown.
    // In particular i'<n followed by i=i'+1 and i>=n implies i=n; merely
    // keeping the final exit guard would lose this common precision.
    // A flattened nest intentionally forgets CFG location and all guards.
    // Do not reconstruct a purported guarded outer iteration/last step.
    if (!nest) {
        z3::expr_vector before(context), previous_count(context);
        previous_count.push_back(count - 1);
        for (unsigned j = 0; j < parameters.size(); ++j) {
            auto fresh = context.int_const(("ari_coarse_before_" + suffix +
                "_" + std::to_string(j)).c_str());
            before.push_back(z3::ite(count == 1, initial[j], fresh));
        }
        z3::expr last_step = context.bool_val(false);
        for (const auto& [symbol, transition] : *transitions) {
            (void)symbol;
            z3::expr path = substitute(transition.guard, parameters, before);
            for (unsigned j = 0; j < parameters.size(); ++j) {
                path = path && post[j] == substitute(
                    transition.updates[j], parameters, before);
            }
            last_step = last_step || path;
        }
        z3::expr previous_relation = substitute(
            substitute(substitute(relation, parameters, before),
                       initial_parameters, initial), n_source, previous_count);
        jump->append_path_condition(count == 0 ||
                                    (previous_relation && last_step));
    }
    jump->is_over_approx = true;
    jump->counterexample_complete = false;
    if (!jump->coarse_fallback_state) {
        // Keep the pre-PHI entry so every degree sees the identical input.
        // Execute this bundle only when all coarse degrees fail to prove.
        jump->coarse_fallback_state = std::make_shared<State>(*state);
    }
    jump->coarse_loop = loop;
    jump->coarse_degree = degree;
    // An opaque union-star must not be recorded as an exact compressed word.
    jump->loop_path_expressions.erase(loop);
    jump->step_pc(AInstruction::create(&*header->getFirstNonPHIOrDbg()));
    if (boundary_replay) {
        jump->coarse_replay_loop = loop;
        jump->coarse_replay_one_block = nest.has_value();
        jump->summary_arithmetic = true;
        // A flat returning-map star covers all header prefixes: replay an
        // entire final iteration, preserving intermediate guards/updates.
        // For a flattened nest the star counts CFG edges instead. Check a
        // terminal block at every location, including inner assertions/exits.
        // No extra seed may assume an unexecuted zero-length prefix reached
        // an interior block. Zero trips are replayed only at the outer header.
        if (nest) {
            for (auto* block : loop->blocks()) {
                if (block == header) continue;
                auto seed = std::make_shared<State>(*jump);
                seed->coarse_boundary_states.clear();
                seed->append_path_condition(count > 0);
                seed->step_pc(AInstruction::create(&*block->getFirstNonPHIOrDbg()));
                jump->coarse_boundary_states.push_back(std::move(seed));
            }
        }
        state->session.note_coarse_summary();
        spdlog::info("[coarse-path] Summarized loop {} at degree {} with {} and {} "
                    "certified observable(s); replaying {} boundary state(s), {} "
                    "exiting block(s)", loop_name(loop), degree, language.str(),
                    solved.size() + entry_equalities.size(), jump->coarse_boundary_states.size() + 1,
                    exiting.size());
        return jump;
    }
    while (jump->pc->inst != header->getTerminator()) {
        if (auto* selection = llvm::dyn_cast<llvm::SelectInst>(jump->pc->inst)) {
            if (!selection->getType()->isIntegerTy(1)) return nullptr;
            const auto condition = jump->evaluate(selection->getCondition());
            const auto yes = jump->evaluate(selection->getTrueValue());
            const auto no = jump->evaluate(selection->getFalseValue());
            jump->append_path_condition(condition.defined() &&
                ((condition.as_expr() && yes.defined()) ||
                 (!condition.as_expr() && no.defined())));
            jump->memory.put_temp(selection,
                ((condition.as_expr() && yes.as_expr()) ||
                 (!condition.as_expr() && no.as_expr())).simplify());
            jump->step_pc();
            continue;
        }
        auto successors = jump->pc->execute(jump);
        if (successors.size() != 1 ||
            successors.front()->status != State::RUNNING) return nullptr;
        jump = successors.front();
    }
    // Replay the final header and keep only its actual exit guard. Dropping
    // earlier guards is safe over-approximation, not a termination proof.
    const auto condition = jump->evaluate(branch->getCondition());
    jump->append_path_condition(condition.defined());
    const bool first_exits = branch->getSuccessor(0) == exit;
    jump->append_path_condition(first_exits ? condition.as_expr()
                                            : !condition.as_expr());
    jump->trace.push_back(header);
    jump->step_pc(AInstruction::create(&*exit->begin()));
    jump->status = State::RUNNING;
    state->session.note_coarse_summary();
    spdlog::info("[coarse-path] Summarized loop {} at degree {} with {} and {} certified "
                 "observable(s), count {} >= 0 (over-approximation)",
                 loop_name(loop), degree, language.str(), solved.size(), render_expr(count));
    return jump;
}

std::optional<PathAcceleration>
PathExpressionAccelerator::accelerate(
    llvm::Loop* loop, const state_ptr& state,
    const PathSchemaCandidate& candidate) {
    if (!loop) return std::nullopt;
    const std::string name = loop_name(loop);
    spdlog::debug("[path-expr] loop {} trying candidate {}", name,
                  render_path_schema(candidate));
    if (candidate.star.empty()) {
        spdlog::debug("[path-expr] loop {} rejected an empty star word",
                      name);
        return std::nullopt;
    }
    const auto nondet_control = find_nondet_loop_control(loop);
    if (nondet_control) {
        spdlog::debug(
            "[path-expr] loop {} recognized {} as a pure nondeterministic "
            "header control",
            name, called_function(nondet_control->call)->getName().str());
    }
    if (auto reason = unsupported_profile_reason(loop, nondet_control)) {
        spdlog::debug(
            "[path-expr] loop {} is outside the accelerator profile: {}",
            name, *reason);
        return std::nullopt;
    }

    const auto transitions =
        enumerate_transitions(loop, state, nondet_control);
    if (!transitions) {
        spdlog::debug("[path-expr] loop {} transition enumeration failed",
                      name);
        return std::nullopt;
    }
    const auto solution =
        solve_composite(loop, state, candidate.star, *transitions);
    if (!solution) {
        spdlog::debug("[path-expr] loop {} composite solving failed for {}",
                      name, render_path_word(candidate.star));
        return std::nullopt;
    }

    z3::context& context = state->z3ctx;
    z3::expr_vector initial = current_header_values(loop, state);
    if (initial.size() != solution->parameters.size()) {
        spdlog::debug(
            "[path-expr] loop {} current header state has {} values; "
            "expected {}",
            name, initial.size(), solution->parameters.size());
        return std::nullopt;
    }
    spdlog::debug("[path-expr] loop {} current header values: {}", name,
                  render_expr_vector(initial));

    if (nondet_control) {
        return accelerate_arbitrary_finite(
            loop, state, candidate, *transitions, *solution, initial,
            *nondet_control);
    }

    const std::uint64_t id = state->session.next_path_expression_id();
    const std::string count_name =
        "ari_path_k_" + std::to_string(id);
    const std::string probe_name =
        "ari_path_t_" + std::to_string(id);
    z3::expr count = context.int_const(count_name.c_str());
    z3::expr probe = context.int_const(probe_name.c_str());

    z3::expr_vector induction_source(context);
    induction_source.push_back(solution->induction_variable);
    z3::expr_vector probe_target(context);
    probe_target.push_back(probe);
    z3::expr_vector count_target(context);
    count_target.push_back(count);

    z3::expr_vector closed_from_current(context);
    for (const z3::expr& closed : solution->closed_forms) {
        closed_from_current.push_back(
            substitute(closed, solution->parameters, initial));
    }
    z3::expr guard_closed = substitute(
        solution->guard, solution->parameters, closed_from_current);
    z3::expr guard_at_probe =
        guard_closed.substitute(induction_source, probe_target);
    z3::expr guard_at_count =
        guard_closed.substitute(induction_source, count_target);
    z3::expr first_break =
        count >= 1 &&
        z3::forall(probe,
                   z3::implies(probe >= 0 && probe < count,
                               guard_at_probe)) &&
        !guard_at_count;
    spdlog::debug("[path-expr] loop {} first-break relation: {}", name,
                  render_expr(first_break));

    const z3::expr source_path = state->get_path_condition().as_expr();
    z3::solver feasibility(context);
    z3::params parameters(context);
    parameters.set("timeout", 3000u);
    feasibility.set(parameters);
    feasibility.add(source_path && first_break);
    const z3::check_result feasibility_result = feasibility.check();
    spdlog::debug("[path-expr] loop {} first-break feasibility: {}", name,
                  check_result_name(feasibility_result));
    if (feasibility_result != z3::sat) return std::nullopt;

    // Prefer a proved functional affine/piecewise-affine counter. The exact
    // relational first-break formula remains the fallback.
    z3::expr path_count = count;
    z3::expr jump_domain = first_break;
    if (auto quantifier_free = eliminate_quantifiers(first_break)) {
        // A new rational trajectory can make certification succeed while
        // leaving a nonlinear quantified first-break obligation unsolved.
        // Do not consume the entry with such an optional jump: preserve the
        // precise trial and its independent coarse fallback.
        if (has_rational_cast(guard_closed) && contains_quantifier(*quantifier_free)) {
            spdlog::debug("[path-expr] loop {} rational guard remains quantified; "
                          "retaining trial/coarse fallback", name);
            return std::nullopt;
        }
        spdlog::debug("[path-expr] loop {} quantifier-free first-break: {}",
                      name, render_expr(*quantifier_free));
        std::vector<z3::expr> bindings;
        collect_affine_bindings(*quantifier_free, count, bindings);
        if (is_linear_integer_formula(*quantifier_free) &&
            has_unit_count_coefficients(*quantifier_free, count)) {
            try {
                LinearLogic logic;
                z3::expr_vector variables(context);
                variables.push_back(count);
                auto inferred = logic.solve_vars(*quantifier_free, variables);
                if (inferred.size() == 1) bindings.push_back(inferred[0]);
            } catch (const z3::exception& error) {
                spdlog::debug("[path-expr] loop {} linear count inference failed: {}",
                              name, error.msg());
            }
        }
        for (z3::expr binding : bindings) {
            // Expanding a large piecewise count into every state expression
            // can be much worse than retaining its relational representation.
            // This fast path substitutes only affine integer expressions.
            if (!binding.is_int() || !is_linear_integer_formula(binding)) continue;
            if (depends_on_any(binding, count_target)) continue;
            // Certify against the original relation, not just the proposed
            // solution. No unconstrained region may be discarded by inference.
            if (decide_unsat(first_break && count != binding) == z3::unsat) {
                path_count = binding.simplify();
                z3::expr_vector replacement(context);
                replacement.push_back(path_count);
                jump_domain = substitute(*quantifier_free, count_target,
                                         replacement).simplify();
                state->session.note_path_affine_template();
                spdlog::debug(
                    "[path-expr] loop {} proved affine repetition count {} "
                    "= {}",
                    name, render_expr(count), render_expr(path_count));
                break;
            }
        }
    } else {
        spdlog::debug(
            "[path-expr] loop {} quantifier elimination failed; retaining "
            "the relational repetition count {}",
            name, render_expr(count));
    }
    if (z3::eq(path_count, count)) {
        spdlog::debug(
            "[path-expr] loop {} uses relational repetition count {}",
            name, render_expr(count));
    }

    state_ptr jump = std::make_shared<State>(*state);
    z3::expr_vector effective_count(context);
    effective_count.push_back(path_count);
    z3::expr_vector values_at_count(context);
    for (const z3::expr& closed : closed_from_current) {
        if (has_rational_cast(closed)) {
            auto exact = substitute(exact_closed_form(closed), induction_source,
                                    effective_count).simplify();
            z3::expr value(context, Z3_mk_fresh_const(context, "ari_exact_closed", context.int_sort()));
            jump->append_path_condition(integerize_rational_query(
                z3::to_real(value) == exact, false));
            values_at_count.push_back(value);
        } else {
            values_at_count.push_back(
                substitute(closed, induction_source, effective_count).simplify());
        }
    }
    spdlog::debug("[path-expr] loop {} accelerated header values: {}", name,
                  render_expr_vector(values_at_count));
    unsigned index = 0;
    for (llvm::PHINode& phi : loop->getHeader()->phis()) {
        jump->memory.put_temp(&phi, values_at_count[index++]);
    }
    for (const auto& [global, phi] : scalar_global_loop_bindings(loop)) {
        auto pointer = jump->memory.get_object(global);
        jump->memory.store(pointer->get_ptr_value(), jump->evaluate(phi));
    }
    jump->append_path_condition(jump_domain);
    jump->loop_path_expressions[loop].append_symbolic_power(
        candidate.star, path_count);
    jump->loop_path_expressions[loop].candidates.clear();
    jump->step_pc(AInstruction::create(
        &*loop->getHeader()->getFirstNonPHIOrDbg()));

    z3::expr covered = z3::eq(path_count, count)
        ? z3::exists(count, first_break) : jump_domain;
    spdlog::debug("[path-expr] loop {} covered source region: {}", name,
                  render_expr(covered));
    state->session.note_path_acceleration();
    spdlog::info("Accelerating loop {} with {} and first-break count {}",
                 loop->getHeader()->getName().str(),
                 render_path_schema(candidate), path_count.to_string());
    return PathAcceleration{jump, covered};
}

} // namespace ari_exe
