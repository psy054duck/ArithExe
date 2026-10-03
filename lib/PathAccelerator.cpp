#include "PathAccelerator.h"

#include "AInstruction.h"
#include "AnalysisManager.h"
#include "LoopSummarizer.h"
#include "VerificationSession.h"
#include "common.h"

#include <spdlog/spdlog.h>

#include <llvm/ADT/SmallVector.h>

#include <algorithm>
#include <cctype>
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

std::optional<NondetLoopControl>
find_nondet_loop_control(llvm::Loop* loop) {
    llvm::BasicBlock* header = loop->getHeader();
    auto* branch = llvm::dyn_cast<llvm::BranchInst>(header->getTerminator());
    if (!branch || !branch->isConditional()) return std::nullopt;

    llvm::Value* oracle = branch->getCondition();
    llvm::BinaryOperator* negation = nullptr;
    if (auto* binary = llvm::dyn_cast<llvm::BinaryOperator>(oracle)) {
        if (binary->getOpcode() != llvm::Instruction::Xor ||
            !binary->getType()->isIntegerTy(1) || !binary->hasOneUse() ||
            binary->getParent() != header) return std::nullopt;
        auto* constant =
            llvm::dyn_cast<llvm::ConstantInt>(binary->getOperand(1));
        oracle = binary->getOperand(0);
        if (!constant) {
            constant = llvm::dyn_cast<llvm::ConstantInt>(binary->getOperand(0));
            oracle = binary->getOperand(1);
        }
        if (!constant || !constant->isOne()) return std::nullopt;
        negation = binary;
    }
    auto* call = llvm::dyn_cast<llvm::CallInst>(oracle);
    if (!call || call->getParent() != header ||
        !call->getType()->isIntegerTy(1) || call->arg_size() != 0 ||
        !call->hasOneUse()) {
        return std::nullopt;
    }
    llvm::Function* callee = called_function(call);
    if (!callee ||
        !callee->isDeclaration() ||
        callee->getName() != "__VERIFIER_nondet_bool") {
        return std::nullopt;
    }

    // Jumping to the exit skips the header. Only the oracle, its single-use
    // Boolean negation, and PHIs may execute there; other computations could
    // define values used on exit.
    for (const llvm::Instruction& instruction : *header) {
        if (&instruction != call && &instruction != branch &&
            &instruction != negation &&
            !llvm::isa<llvm::PHINode>(instruction) &&
            !instruction.isDebugOrPseudoInst()) return std::nullopt;
    }

    const bool first_inside = loop->contains(branch->getSuccessor(0));
    const bool second_inside = loop->contains(branch->getSuccessor(1));
    if (first_inside == second_inside) return std::nullopt;

    llvm::SmallVector<llvm::BasicBlock*, 4> exiting_blocks;
    loop->getExitingBlocks(exiting_blocks);
    if (exiting_blocks.size() != 1 || exiting_blocks.front() != header) {
        return std::nullopt;
    }

    llvm::BasicBlock* exit =
        first_inside ? branch->getSuccessor(1) : branch->getSuccessor(0);
    return NondetLoopControl{
        call, exit, first_inside != (negation != nullptr),
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
        destination.push_back(expression.ctx().bool_val(replacement));
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

std::optional<std::string>
unsupported_profile_reason(
    const llvm::Loop* loop,
    const std::optional<NondetLoopControl>& nondet_control) {
    if (!loop->getSubLoops().empty()) return "nested loop";
    for (const llvm::PHINode& phi : loop->getHeader()->phis()) {
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
            if (llvm::isa<llvm::LoadInst>(instruction)) return "load";
            if (const auto* call =
                    llvm::dyn_cast<llvm::CallInst>(&instruction)) {
                if (!nondet_control || call != nondet_control->call) {
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
    const std::optional<NondetLoopControl>& nondet_control) {
    spdlog::debug("[path-expr] loop {} enumerating one-iteration paths",
                  loop_name(loop));
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
    for (const loop_state_ptr& final_state : final_states) {
        llvm::BasicBlock* latch = final_state->pc->get_block();
        if (!loop->isLoopLatch(latch)) {
            spdlog::debug(
                "[path-expr] loop {} rejected a transition that did not "
                "end at a latch",
                loop_name(loop));
            return std::nullopt;
        }

        z3::expr_vector updates(context);
        z3::expr updates_defined = context.bool_val(true);
        for (llvm::PHINode& phi : loop->getHeader()->phis()) {
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

z3::check_result decide_unsat(const z3::expr& formula) {
    z3::solver solver(formula.ctx());
    z3::params parameters(formula.ctx());
    parameters.set("timeout", 2000u);
    solver.set(parameters);
    solver.add(formula);
    return solver.check();
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
    for (unsigned i = 0; i < solution.closed_forms.size(); ++i) {
        z3::expr closed = solution.closed_forms[i];
        base_failure = base_failure ||
                       closed.substitute(source, at_zero) !=
                           solution.parameters[i];
        state_at_n.push_back(closed.substitute(source, at_n));
    }
    for (unsigned i = 0; i < solution.closed_forms.size(); ++i) {
        z3::expr expected = solution.one_step[i].substitute(
            solution.parameters, state_at_n);
        z3::expr actual = solution.closed_forms[i].substitute(
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
        const closed_form_ty solved = solver.get_res();
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
                                      const state_ptr& state) {
    z3::expr_vector values(state->z3ctx);
    if (state->trace.empty()) return values;
    llvm::BasicBlock* predecessor = state->trace.back();
    for (llvm::PHINode& phi : loop->getHeader()->phis()) {
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
    // A stable single path covers every finite continuation from this state.
    // Composite words would additionally require exits at every word offset.
    if (candidate.star.size() != 1) return std::nullopt;
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
    for (const z3::expr& closed : solution.closed_forms) {
        z3::expr from_current =
            substitute(closed, solution.parameters, initial);
        values_at_count.push_back(
            from_current.substitute(source, at_count).simplify());
        trajectory.push_back(
            from_current.substitute(source, at_probe).simplify());
    }

    z3::expr other_enabled = context.bool_val(false);
    for (const auto& [symbol, transition] : transitions) {
        if (symbol != candidate.star.front()) {
            other_enabled = other_enabled || substitute(
                transition.guard, solution.parameters, trajectory);
        }
    }
    const z3::check_result stability = decide_unsat(
        state->get_path_condition().as_expr() && probe >= 0 &&
        other_enabled);
    spdlog::debug(
        "[path-expr] loop {} competing-path stability obligation: {}",
        name, check_result_name(stability));
    if (stability != z3::unsat) return std::nullopt;

    const z3::expr guard_at_probe = substitute(
        solution.guard, solution.parameters, trajectory);
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
        candidate.star, count);
    jump->loop_path_expressions[loop].candidates.clear();

    // Canonical witness for the existentially projected oracle: n continue
    // values followed by one exit value. Prior observed calls stay in order.
    const z3::expr sequence = z3::lambda(
        probe, z3::ite(probe < count,
                       context.bool_val(control.continue_value),
                       context.bool_val(!control.continue_value)));
    jump->nondet_calls.emplace_back(control.call, sequence, count + 1);
    jump->memory.put_temp(control.call,
                          context.bool_val(!control.continue_value));
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
    state_ptr returning;
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
            if (result.returning) return std::nullopt;
            result.returning = current;
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
                auto jump = PathExpressionAccelerator::accelerate_nested(
                    child, current);
                if (!jump) return std::nullopt;
                work.push(jump);
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
    if (!result.returning || !result.exiting) return std::nullopt;
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
    z3::expr_vector updates(ctx);
    z3::expr guard = iteration->returning->get_path_condition().as_expr();
    auto* latch = iteration->returning->pc->get_block();
    for (auto& phi : header->phis()) {
        auto update = iteration->returning->evaluate(phi.getIncomingValueForBlock(latch));
        updates.push_back(update.as_expr());
        guard = guard && update.defined();
    }
    // A macro symbol denotes the complete body, including certified child exits.
    const PathSymbol symbol = path_symbol_for_final_state(
        loop, std::make_shared<LoopState>(*iteration->returning));
    summary->word = {symbol};
    std::map<PathSymbol, ScalarPathTransition> transitions;
    transitions.emplace(symbol, ScalarPathTransition(guard, updates));
    auto solution = solve_composite(loop, initial, summary->word, transitions);
    if (!solution) return nullptr;
    auto count = nested_count_template(loop, *solution, iteration->exiting, first_inside);
    if (!count) return nullptr;
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
    const z3::expr returning_guard = substitute(solution->guard, solution->parameters, trajectory);
    const z3::expr exit_guard = substitute(
        iteration->exiting->get_path_condition().as_expr(), solution->parameters, final_values);
    const z3::expr errors = substitute(iteration->errors, solution->parameters, trajectory);
    summary->domain = *count >= 0 && exit_guard &&
        z3::forall(probe, z3::implies(probe >= 0 && probe < *count, returning_guard)) &&
        z3::forall(probe, z3::implies(probe >= 0 && probe <= *count, !errors));
    // Header-exit loops expose only header definitions. Reject unsupported
    // live-outs rather than accidentally reading stale caller memory on n=0.
    for (auto* block : loop->blocks()) {
        for (auto& instruction : *block) {
            if (!instruction.getType()->isIntegerTy()) continue;
            const bool live_out = (llvm::isa<llvm::PHINode>(instruction) && block == header) ||
                std::any_of(instruction.user_begin(), instruction.user_end(),
                    [&](const llvm::User* user) {
                        auto* consumer = llvm::dyn_cast<llvm::Instruction>(user);
                        return consumer && !loop->contains(consumer->getParent());
                    });
            if (!live_out) continue;
            if (block != header) return nullptr;
            auto value = iteration->exiting->evaluate(&instruction);
            summary->outputs.push_back(&instruction);
            summary->values.push_back(substitute(value.as_expr(), solution->parameters, final_values).simplify());
            summary->domain = summary->domain && substitute(value.defined(), solution->parameters, final_values);
        }
    }
    // Simplifying a reusable domain for all mathematical inputs can be much
    // harder than proving it at a concrete entry, especially with modulo 2^w.
    // Spend only a small optional QE budget; the exact domain is the fallback.
    if (auto reduced = eliminate_quantifiers(summary->domain, 100)) summary->domain = *reduced;
    summary->domain = summary->domain.simplify();
    // No dynamic choices, caller-specialized constants, or solver auxiliaries
    // may leak into a parametric transition reused by its parent.
    if (has_foreign_symbols(summary->domain, summary->parameters) ||
        has_foreign_symbols(summary->count, summary->parameters)) return nullptr;
    for (const auto& value : summary->values) {
        if (has_foreign_symbols(value, summary->parameters)) return nullptr;
    }
    caller->session.note_nested_path_summary();
    spdlog::debug("[nested-path] loop {} certified exit: count={}, domain={}, values={}",
        loop_name(loop), render_expr(summary->count), render_expr(summary->domain),
        render_expr_vector(summary->values));
    return summary;
}

} // namespace

state_ptr PathExpressionAccelerator::accelerate_nested(
    llvm::Loop* loop, const state_ptr& state) {
    if (!loop || state->trace.empty() || loop->contains(state->trace.back())) return nullptr;
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
            return nullptr;
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
                       !state->memory.get_object(input))) return nullptr;
        const auto value = state->evaluate(input);
        actual.push_back(value.as_expr());
        defined = defined && value.defined();
    }
    const z3::expr domain = defined && substitute(summary->domain, summary->parameters, actual);
    auto proof = decide_unsat(state->get_path_condition().as_expr() && !domain);
    spdlog::debug("[nested-path] loop {} entry safety/coverage certificate: {}", loop_name(loop), check_result_name(proof));
    if (proof != z3::unsat) return nullptr;
    state_ptr jump = std::make_shared<State>(*state);
    for (unsigned i = 0; i < summary->outputs.size(); ++i) {
        jump->memory.put_temp(summary->outputs[i],
            substitute(summary->values[i], summary->parameters, actual).simplify());
    }
    const z3::expr count = substitute(summary->count, summary->parameters, actual).simplify();
    // Each invocation starts a new dynamic prefix. Retaining an inner loop's
    // previous cursor mixes unrelated outer iterations into its path symbol.
    jump->loop_path_expressions.erase(loop);
    jump->loop_path_expressions[loop].append_symbolic_power(summary->word, count);
    jump->path_decisions.push_back({reinterpret_cast<std::uintptr_t>(loop->getHeader()), 2});
    jump->trace.push_back(loop->getHeader());
    jump->step_pc(AInstruction::create(&*summary->exit->instructionsWithoutDebug().begin()));
    jump->status = State::RUNNING;
    state->session.note_path_acceleration();
    spdlog::info("[nested-path] Accelerating loop {} bottom-up with {} repetition(s)", loop_name(loop), render_expr(count));
    return jump;
}

bool PathExpressionAccelerator::has_nondeterministic_header_control(
    llvm::Loop* loop) {
    return loop && find_nondet_loop_control(loop).has_value();
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
    if (auto quantifier_free = eliminate_quantifiers(first_break)) {
        spdlog::debug("[path-expr] loop {} quantifier-free first-break: {}",
                      name, render_expr(*quantifier_free));
        std::vector<z3::expr> bindings;
        collect_affine_bindings(*quantifier_free, count, bindings);
        for (z3::expr binding : bindings) {
            if (decide_unsat(source_path && *quantifier_free &&
                             count != binding) == z3::unsat) {
                path_count = binding.simplify();
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
    z3::expr_vector values_at_count(context);
    for (const z3::expr& closed : closed_from_current) {
        values_at_count.push_back(
            substitute(closed, induction_source, count_target));
    }
    spdlog::debug("[path-expr] loop {} accelerated header values: {}", name,
                  render_expr_vector(values_at_count));
    unsigned index = 0;
    for (llvm::PHINode& phi : loop->getHeader()->phis()) {
        jump->memory.put_temp(&phi, values_at_count[index++]);
    }
    jump->append_path_condition(first_break);
    jump->loop_path_expressions[loop].append_symbolic_power(
        candidate.star, path_count);
    jump->loop_path_expressions[loop].candidates.clear();
    jump->step_pc(AInstruction::create(
        &*loop->getHeader()->getFirstNonPHIOrDbg()));

    z3::expr covered = z3::exists(count, first_break);
    spdlog::debug("[path-expr] loop {} covered source region: {}", name,
                  render_expr(covered));
    state->session.note_path_acceleration();
    spdlog::info("Accelerating loop {} with {} and first-break count {}",
                 loop->getHeader()->getName().str(),
                 render_path_schema(candidate), count.to_string());
    return PathAcceleration{jump, covered};
}

} // namespace ari_exe
