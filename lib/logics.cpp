#include "logics.h"


#include "z3++.h"

#include <set>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <spdlog/spdlog.h>
#include <iostream>
#include <algorithm>
#include <chrono>

namespace ari_exe {

    z3::expr
    simplify(const z3::expr& expr, std::optional<z3::expr> assumption) {
        auto logic = Logic();
        auto new_expr = expr.simplify();
        auto clauses= logic.to_cnf(new_expr);
        auto& z3ctx = expr.ctx();
        auto solver = z3::solver(z3ctx);
        if (assumption.has_value()) {
            solver.add(assumption.value());
        }
        z3::expr_vector remains(z3ctx);
        for (const auto& clause : clauses) {
            solver.push();
            solver.add(!clause);
            if (solver.check() != z3::unsat) {
                // this clause is not entailed, so keep it
                z3::expr_vector literals(z3ctx);
                if (clause.is_or()) {
                    for (const auto& lit : clause.args()) {
                        literals.push_back(!lit);
                    }
                } else {
                    literals.push_back(!clause);
                }
                auto minimized_conjunction = minimize_conjunction(literals, assumption.value_or(z3ctx.bool_val(true)));
                z3::expr_vector minimized_literals(z3ctx);
                for (const auto& lit : minimized_conjunction) {
                    minimized_literals.push_back(!lit);
                }
                auto new_clause = z3::mk_or(minimized_literals);
                remains.push_back(new_clause);
            }
            solver.pop();
        }
        return z3::mk_and(remains);
    }

    bool
    implies(const z3::expr& a, const z3::expr& b) {
        auto& z3ctx = a.ctx();
        z3::solver s(z3ctx);
        s.add(a);
        s.add(!b);
        return s.check() == z3::unsat;
    }

    /**
     * @brief check if e1 and e2 are equivalent under the condition cond
     */
    static bool
    conditionally_eq(const z3::expr& cond, const z3::expr& e1, const z3::expr& e2) {
        if (z3::eq(e1, e2)) return true;
        auto& z3ctx = e1.ctx();
        z3::solver s(z3ctx);
        z3::params p(z3ctx);
        p.set("timeout", 3*1000u);
        s.set(p);
        s.add(cond);
        s.add(e1 != e2);
        return s.check() == z3::unsat;
    }

    std::pair<z3::expr_vector, z3::expr_vector>
    merge_cases(const z3::expr_vector& conditions, const z3::expr_vector& expressions) {
        bool changed = true;
        int pivot = 0;
        auto res_conditions = conditions;
        auto res_expressions = expressions;
        while (pivot < res_expressions.size()) {
            std::set<int> merged;
            auto pivot_expr = res_expressions[pivot];
            auto pivot_cond = res_conditions[pivot];
            // iterate through to check if some case is equivalent to pivot expression
            for (int i = 0; i < res_expressions.size(); ++i) {
                if (i == pivot) continue; // skip pivot
                if (merged.count(i)) continue; // skip already merged
                if (conditionally_eq(res_conditions[i], pivot_expr, res_expressions[i])) {
                    pivot_cond = pivot_cond || res_conditions[i];
                    merged.insert(i);
                }
            }
            auto new_conditions = z3::expr_vector(res_conditions.ctx());
            auto new_expressions = z3::expr_vector(res_expressions.ctx());
            int new_pivot_location = 0;
            for (int i = 0; i < res_conditions.size(); ++i) {
                if (merged.count(i) > 0) continue; // skip pivot and merged
                if (i == pivot) {
                    new_pivot_location = new_conditions.size();
                    new_conditions.push_back(pivot_cond);
                } else {
                    new_conditions.push_back(res_conditions[i]);
                }
                new_expressions.push_back(res_expressions[i]);
            }
            if (!merged.empty()) {
                pivot = new_pivot_location + 1;
            } else {
                pivot++;
            }
            res_conditions = new_conditions;
            res_expressions = new_expressions;
        }
        return {res_conditions, res_expressions};
    }

    z3::expr
    restrict_to_domain(const z3::expr& f, z3::expr domain) {
        z3::solver solver(f.ctx());
        z3::params parameters(f.ctx());
        parameters.set("timeout", 50u);
        solver.set(parameters);
        solver.add(domain);
        unsigned queries = 0;
        auto entails = [&](const z3::expr& condition) {
            if (queries >= 256) return false;
            ++queries;
            solver.push();
            solver.add(!condition);
            const bool proved = solver.check() == z3::unsat;
            solver.pop();
            return proved;
        };
        std::unordered_map<unsigned, bool> polynomial_cache;
        unsigned polynomial_nodes = 0;
        std::function<bool(const z3::expr&)> is_polynomial;
        is_polynomial = [&](const z3::expr& expression) {
            const auto found = polynomial_cache.find(expression.id());
            if (found != polynomial_cache.end()) return found->second;
            if (++polynomial_nodes > 1024 || !expression.is_app() ||
                (!expression.is_int() && !expression.is_real())) return false;
            const auto kind = expression.decl().decl_kind();
            bool result = expression.is_numeral() ||
                (expression.is_const() && kind == Z3_OP_UNINTERPRETED);
            if (kind == Z3_OP_ADD || kind == Z3_OP_SUB ||
                kind == Z3_OP_UMINUS || kind == Z3_OP_MUL) {
                result = true;
                for (const auto& argument : expression.args()) {
                    if (!is_polynomial(argument)) { result = false; break; }
                }
            }
            polynomial_cache.emplace(expression.id(), result);
            return result;
        };
        unsigned polynomial_queries = 0;
        const auto polynomial_deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(150);
        auto whole_polynomial = [&](const z3::expr& expression)
            -> std::optional<z3::expr> {
            if (!expression.is_int() && !expression.is_real()) return std::nullopt;
            std::vector<z3::expr> pending{expression};
            std::unordered_set<unsigned> seen;
            while (!pending.empty() && seen.size() < 128 &&
                   polynomial_queries < 8 && queries < 256 &&
                   std::chrono::steady_clock::now() < polynomial_deadline) {
                auto candidate = pending.back();
                pending.pop_back();
                if (!seen.insert(candidate.id()).second) continue;
                if (candidate.is_ite()) {
                    pending.push_back(candidate.arg(2));
                    pending.push_back(candidate.arg(1));
                } else if (!candidate.is_numeral() && is_polynomial(candidate)) {
                    // A leaf polynomial can cover several nonadjacent boundary
                    // cases, but only a whole-expression certificate permits it.
                    ++polynomial_queries;
                    if (entails(expression == candidate)) return candidate.simplify();
                }
            }
            return std::nullopt;
        };
        using Cache = std::unordered_map<unsigned, z3::expr>;
        std::function<z3::expr(const z3::expr&, Cache&)> visit;
        visit = [&](const z3::expr& expression, Cache& cache) -> z3::expr {
            const auto found = cache.find(expression.id());
            if (found != cache.end()) return found->second;
            z3::expr result = expression;
            if (queries >= 256 || !expression.is_app() || expression.is_const()) {
                return expression.simplify();
            }
            if (expression.is_ite()) {
                if (auto polynomial = whole_polynomial(expression)) {
                    cache.emplace(expression.id(), *polynomial);
                    return *polynomial;
                }
                const z3::expr condition = visit(expression.arg(0), cache);
                if (condition.is_true() || entails(condition)) {
                    result = visit(expression.arg(1), cache);
                } else if (condition.is_false() || entails(!condition)) {
                    result = visit(expression.arg(2), cache);
                } else {
                    // Cache entries are local to the branch assumptions.
                    Cache then_cache, else_cache;
                    solver.push();
                    solver.add(condition);
                    const z3::expr then_value = visit(expression.arg(1), then_cache);
                    solver.pop();
                    solver.push();
                    solver.add(!condition);
                    const z3::expr else_value = visit(expression.arg(2), else_cache);
                    solver.pop();
                    if (z3::eq(then_value, else_value)) {
                        result = then_value;
                    } else if (entails(z3::implies(condition, then_value == else_value))) {
                        // A boundary value may be an instance of the other
                        // branch's polynomial on the current domain.
                        result = else_value;
                    } else if (entails(z3::implies(!condition, then_value == else_value))) {
                        result = then_value;
                    } else {
                        result = z3::ite(condition, then_value, else_value).simplify();
                    }
                }
            } else {
                z3::expr_vector arguments(f.ctx());
                for (const z3::expr& argument : expression.args()) {
                    arguments.push_back(visit(argument, cache));
                }
                result = expression.decl()(arguments).simplify();
                if (result.is_app() && result.num_args()==2 &&
                    !result.arg(1).is_numeral() &&
                    (result.decl().decl_kind()==Z3_OP_IDIV ||
                     result.decl().decl_kind()==Z3_OP_MOD)) {
                    const auto divisor=result.arg(1), dividend=result.arg(0);
                    auto quotient=f.ctx().int_val(0), remainder=f.ctx().int_val(0);
                    std::vector<z3::expr> terms;
                    if (dividend.is_app() && dividend.decl().decl_kind()==Z3_OP_ADD)
                        for (const auto& term:dividend.args()) terms.push_back(term);
                    else terms.push_back(dividend);
                    for (const auto& term:terms) {
                        if (z3::eq(term,divisor)) quotient=quotient+1;
                        else if (term.is_app() && term.decl().decl_kind()==Z3_OP_MUL) {
                            auto factor=f.ctx().int_val(1);
                            bool divided=false;
                            for (const auto& arg:term.args()) {
                                if (!divided && z3::eq(arg,divisor)) divided=true;
                                else factor=factor*arg;
                            }
                            if (divided) quotient=quotient+factor;
                            else remainder=remainder+term;
                        } else remainder=remainder+term;
                    }
                    quotient=quotient.simplify();remainder=remainder.simplify();
                    // Euclidean coordinates are valid only on a certified
                    // positive-stride, in-range remainder domain.
                    if (entails(divisor>0 && remainder>=0 && remainder<divisor))
                        result=result.decl().decl_kind()==Z3_OP_IDIV ? quotient : remainder;
                }
            }
            cache.emplace(expression.id(), result);
            return result;
        };
        Cache cache;
        return visit(f, cache);
    }

    bool is_feasible(const z3::expr& fml, std::optional<z3::expr> assumption) {
        auto& z3ctx = fml.ctx();
        z3::solver s(z3ctx);
        if (assumption.has_value()) {
            s.add(assumption.value());
        }
        s.add(fml);
        return s.check() == z3::sat;
    }

    bool is_equivalent(const z3::expr& f1, const z3::expr& f2) {
        auto& z3ctx = f1.ctx();
        z3::solver s(z3ctx);
        s.add(f1 != f2);
        return s.check() == z3::unsat;
    }

    static void
    aux_expr2piecewise(const z3::expr& expr, const z3::expr& cur_cond, z3::expr_vector& conditions, z3::expr_vector& expressions) {
        // Base case: if the expression is a constant or a variable, we can directly add it
        if (expr.is_const() || expr.is_var() || expr.is_quantifier()) {
            conditions.push_back(cur_cond);
            expressions.push_back(expr);
        } else if (expr.is_app() && expr.decl().decl_kind() == Z3_OP_ITE) {
            // If the expression is an if-then-else, we can extract the conditions and expressions
            auto cond = expr.arg(0);
            auto then_expr = expr.arg(1);
            auto else_expr = expr.arg(2);

            aux_expr2piecewise(then_expr, cond && cur_cond, conditions, expressions);
            aux_expr2piecewise(else_expr, !cond && cur_cond, conditions, expressions);

        } else if (expr.is_app()){
            int arity = expr.num_args();
            std::vector<z3::expr_vector> all_conditions;
            std::vector<z3::expr_vector> all_expressions;
            std::vector<int> num_cases;
            for (auto arg : expr.args()) {
                auto [cur_conditions, cur_expressions] = expr2piecewise(arg);
                all_conditions.push_back(cur_conditions);
                all_expressions.push_back(cur_expressions);
                num_cases.push_back(cur_conditions.size());
            }
            for (auto& indices : cartesian_product(num_cases)) {
                z3::expr acc_condition = expr.ctx().bool_val(true);
                z3::expr_vector acc_expressions(expr.ctx());
                for (int i = 0; i < indices.size(); ++i) {
                    auto cur_condition = all_conditions[i][indices[i]];
                    acc_condition = acc_condition && cur_condition;
                    acc_expressions.push_back(all_expressions[i][indices[i]]);
                }
                conditions.push_back(acc_condition && cur_cond);
                auto new_expr = expr.decl()(acc_expressions);
                expressions.push_back(new_expr);
            }
        }
    }

    std::pair<z3::expr_vector, z3::expr_vector>
    expr2piecewise(const z3::expr& expr) {
        z3::expr_vector conditions(expr.ctx());
        z3::expr_vector expressions(expr.ctx());
        aux_expr2piecewise(expr, expr.ctx().bool_val(true), conditions, expressions);
        z3::expr_vector eliminated_conditions(expr.ctx());
        for (const auto& cond : conditions) {
            eliminated_conditions.push_back(eliminate_ite(cond));
        }
        return merge_cases(eliminated_conditions, expressions);
    }

    z3::expr
    eliminate_ite(const z3::expr& expr) {
        if (!expr.is_bool()) return expr;
        bool has_ite = false;
        std::vector<z3::expr> pending{expr};
        std::set<unsigned> visited;
        while (!pending.empty()) {
            const auto term = pending.back();
            pending.pop_back();
            if (!visited.insert(term.id()).second) continue;
            if (term.is_quantifier()) pending.push_back(term.body());
            if (!term.is_app()) continue;
            if (term.is_ite()) {
                has_ite = true;
                break;
            }
            for (const auto& argument : term.args()) pending.push_back(argument);
        }
        if (!has_ite) return expr.simplify();
        // Tactics can introduce fresh symbols, but source symbols may also
        // contain '!'. Only project constants absent from the original AST.
        const auto original_variables = Logic().collect_vars(expr);
        try {
            auto goal = z3::goal(expr.ctx());
            goal.add(expr);
            z3::expr_vector to_or(expr.ctx());
            auto elim_ite = z3::tactic(expr.ctx(), "elim-term-ite");
            auto elim_res = elim_ite(goal);
            for (int i = 0; i < elim_res.size(); ++i) {
                to_or.push_back(elim_res[i].as_expr());
            }
            auto eliminated_expr = z3::mk_or(to_or);
            auto elim_temp = z3::tactic(expr.ctx(), "qe");
            auto logic = Logic();
            z3::expr_vector aux_vars(expr.ctx());
            for (const auto& variable : logic.collect_vars(eliminated_expr)) {
                if (!original_variables.contains(variable)) aux_vars.push_back(variable);
            }
            if (!aux_vars.empty()) {
                auto exits_expr = z3::exists(aux_vars, eliminated_expr);
                auto qe_goal = z3::goal(expr.ctx());
                qe_goal.add(exits_expr);
                auto qe_res = z3::try_for(elim_temp, 100)(qe_goal);
                z3::expr_vector qe_conditions(expr.ctx());
                for (int j = 0; j < qe_res.size(); ++j) {
                    qe_conditions.push_back(qe_res[j].as_expr());
                }
                eliminated_expr = z3::mk_or(qe_conditions).simplify();
            }
            z3::solver certificate(expr.ctx());
            certificate.set("timeout", 100u);
            certificate.add(eliminated_expr != expr);
            if (certificate.check() == z3::unsat) return eliminated_expr.simplify();
        } catch (const z3::exception&) {
            // Unsupported or timed-out QE never justifies a replacement.
        }
        return expr.simplify();
    }

    z3::expr
    piecewise2ite(const z3::expr_vector& conditions, const z3::expr_vector& expressions) {
        if (conditions.size() != expressions.size()) {
            throw std::invalid_argument("Conditions and expressions must have the same size.");
        }
        if (conditions.empty()) {
            throw std::invalid_argument("A piecewise expression needs at least one branch.");
        }

        if (conditions.size() == 1) {
            return expressions[0];
        }

        auto [new_conditions, new_expressions] = merge_cases(conditions, expressions);
        z3::expr ite_expr = new_expressions.back();
        for (int i = (int) new_conditions.size() - 2; i >= 0; --i) {
            ite_expr = z3::ite(new_conditions[i], new_expressions[i], ite_expr);
        }
        return ite_expr;
    }


    z3::expr
    apply_result2expr(z3::apply_result result) {
        z3::expr res = result.ctx().bool_val(false);
        for (int i = 0; i < result.size(); i++) {
            auto r = result[i].as_expr();
            res = res || r;
        }
        return res.simplify();
    }

    std::set<z3::expr, expr_compare>
    Logic::collect_vars(const z3::expr& e) {
        std::set<z3::expr, expr_compare> vars;
        collect_vars_rec(e, vars);
        return vars;
    }

    void
    Logic::collect_vars_rec(const z3::expr& e, std::set<z3::expr, expr_compare>& vars) {
        if (e.is_quantifier()) {
            collect_vars_rec(e.body(), vars);
            return;
        }
        if (!e.is_app()) return;
        if (e.is_const() && e.num_args() == 0 && e.decl().decl_kind() == Z3_OP_UNINTERPRETED) {
            vars.insert(e);
            return;
        }
        for (unsigned i = 0; i < e.num_args(); ++i) {
            collect_vars_rec(e.arg(i), vars);
        }
    }

    z3::expr_vector
    Logic::collect_aux_vars(const z3::expr& e) {
        auto vars = collect_vars(e);
        z3::expr_vector aux_vars(e.ctx());
        for (const auto& var : vars) {
            if (var.to_string().find("!") != std::string::npos) {
                aux_vars.push_back(var);
            }
        }
        return aux_vars;
    }

    std::pair<z3::expr, z3::expr_vector>
    LinearLogic::preprocess_constraints(z3::expr constraints) {
        // eliminate the if-then-else terms
        auto& z3ctx = constraints.ctx();
        z3::tactic elim_ite(z3ctx, "elim-term-ite");
        z3::goal g(z3ctx);
        g.add(constraints);
        z3::apply_result result = elim_ite(g);
        auto new_fml = apply_result2expr(result);

        // replace all auxiliary variables with fresh variables
        // this is needed because the z3 solver does not generate models for
        // auxiliary variables, which are needed for to_cnf/to_dnf
        auto vars = collect_vars(new_fml);
        z3::expr_vector aux_vars(new_fml.ctx());
        z3::expr_vector tmp_vars(new_fml.ctx());
        int tmp_index = 0;
        for (const auto& var : vars) {
            if (var.to_string().find("!") != std::string::npos) {
                aux_vars.push_back(var);
                auto changed = "tmp_" + std::to_string(tmp_index++);
                tmp_vars.push_back(z3ctx.int_const(changed.c_str()));
            }
        }
        new_fml = new_fml.substitute(aux_vars, tmp_vars);

        z3::params p(z3ctx);
        p.set("eq2ineq", true);
        new_fml = new_fml.simplify(p);
        return {new_fml, tmp_vars};
    }

    z3::expr_vector
    LinearLogic::solve_vars(const z3::expr& constraints, const z3::expr_vector& vars) {
        auto& z3ctx = constraints.ctx();
        const auto has_quantifier = [](const z3::expr& expression,
                                       const auto& self) -> bool {
            if (expression.is_quantifier()) return true;
            if (!expression.is_app()) return false;
            for (const auto& argument : expression.args()) {
                if (self(argument, self)) return true;
            }
            return false;
        };
        // A uniquely forced numeral is already a closed form. Certify the
        // model values before invoking elimination with auxiliary variables.
        z3::solver constants(z3ctx);
        constants.set("timeout", 100u);
        constants.add(constraints);
        if (!vars.empty() && constants.check() == z3::sat) {
            auto model = constants.get_model();
            z3::expr_vector values(z3ctx), equalities(z3ctx);
            for (const auto& variable : vars) {
                const auto value = model.eval(variable, true).simplify();
                if (!value.is_numeral()) break;
                values.push_back(value);
                equalities.push_back(variable == value);
            }
            if (values.size() == vars.size()) {
                constants.add(!z3::mk_and(equalities));
                if (constants.check() == z3::unsat) return values;
            }
        }
        if (has_quantifier(constraints, has_quantifier)) {
            // A forced constant above is safe even with unrelated quantified
            // input constraints. Do not attempt general algebra on quantifiers.
            return z3::expr_vector(z3ctx);
        }
        auto [processed_cons, tmp_vars] = preprocess_constraints(constraints);

        z3::solver tmp_s(z3ctx);
        tmp_s.add(processed_cons);
        z3::solver tmp_s2(z3ctx);
        tmp_s2.add(constraints);
        auto dnf = to_dnf(processed_cons);
        spdlog::debug("DNF size: {}", dnf.size());
        if (tmp_s.check() == z3::unsat || dnf.size() == 0) {
            return z3::expr_vector(z3ctx);
        }

        z3::expr_vector conds(constraints.ctx());
        std::vector<z3::expr_vector> complete_values;

        // ask linear solver to solve for both given vars and tmp_vars
        // tmp_vars do not appear in the constraints so they cannot appear in solutions.
        z3::expr_vector vars_and_tmp_vars(z3ctx);
        for (const auto& var : vars) vars_and_tmp_vars.push_back(var);
        for (const auto& tmp_var : tmp_vars) vars_and_tmp_vars.push_back(tmp_var);

        // solve case by case
        for (auto conjunct : dnf) {
            auto partial_value_with_tmp = solve_vars_linear(conjunct, vars_and_tmp_vars);
            if (partial_value_with_tmp.size() < vars.size()) {
                continue;
            }
            // remove tmp_vars from the solution
            z3::expr_vector partial_value(z3ctx);
            for (int i = 0; i < vars.size(); ++i) {
                partial_value.push_back(partial_value_with_tmp[i]);
            }
            complete_values.push_back(partial_value);
            z3::expr_vector dst(z3ctx);
            for (auto v : partial_value_with_tmp) dst.push_back(v);
            z3::expr cond = conjunct.substitute(vars_and_tmp_vars, dst);
            conds.push_back(cond.simplify());
        }
        if (complete_values.empty()) {
            return z3::expr_vector(z3ctx);
        }

        // combine results using if-then-else
        std::vector<z3::expr> res_vec;
        for (int i = 0; i < complete_values[0].size(); ++i) {
            res_vec.push_back(complete_values[0][i]);
        }
        for (int i = 1; i < complete_values.size(); ++i) {
            for (int j = 0; j < res_vec.size(); ++j) {
                res_vec[j] = z3::ite(simplify(conds[i]), complete_values[i][j], res_vec[j]);
            }
        }

        // prepare the result
        z3::expr_vector res(z3ctx);
        for (int i = 0; i < res_vec.size(); ++i) {
            res.push_back(res_vec[i].simplify());
        }
        // A rejected DNF case must not leave an arbitrary default solution
        // covering that case. Certify the combined result on the full input.
        z3::solver certificate(z3ctx);
        certificate.set("timeout", 100u);
        certificate.add(constraints);
        z3::expr_vector equalities(z3ctx);
        for (unsigned i = 0; i < vars.size(); ++i) equalities.push_back(vars[i] == res[i]);
        certificate.add(!z3::mk_and(equalities));
        if (certificate.check() != z3::unsat) return z3::expr_vector(z3ctx);
        return res;
    }

    bool
    Logic::is_ge(const z3::expr& e) {
        return e.is_app() && e.decl().decl_kind() == Z3_OP_GE;
    }

    bool
    Logic::is_le(const z3::expr& e) {
        return e.is_app() && e.decl().decl_kind() == Z3_OP_LE;
    }

    bool
    Logic::is_eq(const z3::expr& e) {
        return e.is_app() && e.decl().decl_kind() == Z3_OP_EQ;
    }

    bool
    Logic::is_lt(const z3::expr& e) {
        return e.is_app() && e.decl().decl_kind() == Z3_OP_LT;
    }

    bool
    Logic::is_gt(const z3::expr& e) {
        return e.is_app() && e.decl().decl_kind() == Z3_OP_GT;
    }

    bool 
    Logic::is_atom(const z3::expr& t) {
        if (!t.is_bool()) return false;
        if (t.is_quantifier()) return true;
        if (!t.is_app()) return false;
        Z3_decl_kind k = t.decl().decl_kind();
        if (k == Z3_OP_AND || k == Z3_OP_OR || k == Z3_OP_IMPLIES) return false;
        // if (k == Z3_OP_EQ && t.arg(0).is_bool()) return false;
        if (k == Z3_OP_TRUE || k == Z3_OP_FALSE || k == Z3_OP_XOR || k == Z3_OP_NOT) return false;
        if (t.num_args() == 0) return true;
        if (t.arg(0).is_bool()) return false;
        return true;
    }

    // Recursively collect all atoms in the formula
    void 
    Logic::atoms_rec(const z3::expr& t, std::set<std::string>& visited, z3::expr_vector& atms) {
        if (visited.contains(t.to_string())) {
            return;
        }
        visited.insert(t.to_string());
        if (is_atom(t)) {
            atms.push_back(t);
        }
        if (!t.is_app()) return;
        for (unsigned i = 0; i < t.num_args(); ++i) {
            atoms_rec(t.arg(i), visited, atms);
        }
    }

    z3::expr_vector
    Logic::atoms(const z3::expr& fml) {
        std::set<std::string> visited;
        z3::expr_vector atms(fml.ctx());
        atoms_rec(fml, visited, atms);
        return atms;
    }

    z3::expr
    Logic::atom2literal(const z3::model& m, const z3::expr& a) {
        z3::context& ctx = a.ctx();
        z3::expr val = m.eval(a).simplify();
        if (val.is_true()) {
            return a;
        }
        return !a;
    }

    static z3::expr_vector
    get_expr_vec_except(const z3::expr_vector& vec, int pivot) {
        z3::expr_vector res(vec.ctx());
        for (int i = 0; i < vec.size(); ++i) {
            if (i != pivot) {
                res.push_back(vec[i]);
            }
        }
        return res;
    }

    z3::expr_vector
    minimize_conjunction(const z3::expr_vector& conjunction, int pivot, std::optional<z3::expr> assumption) {
        if (!assumption.has_value()) {
            assumption = conjunction.ctx().bool_val(true);
        }
        if (pivot >= conjunction.size()) {
            return conjunction; // nothing to minimize
        }
        auto solver = z3::solver(conjunction.ctx());
        auto& ctx = conjunction.ctx();
        solver.add(solver.ctx().bool_val(true)); // add a trivial constraint to avoid empty solver
        solver.add(assumption.value());
        z3::expr pivot_expr = conjunction[pivot];
        auto remaining_exprs = get_expr_vec_except(conjunction, pivot);
        solver.add(remaining_exprs);
        solver.add(!pivot_expr);
        if (solver.check() == z3::unsat) {
            // the pivot is entailed, so we can remove it
            return minimize_conjunction(remaining_exprs, pivot, assumption);
        } else {
            return minimize_conjunction(conjunction, ++pivot, assumption);
        }
    }

    z3::expr
    Logic::implicant(const z3::expr_vector& atoms, z3::solver& s, z3::solver& snot) {
        z3::model m = snot.get_model();
        z3::expr_vector lits(s.ctx());
        for (const auto& a : atoms) {
            lits.push_back(atom2literal(m, a));
        }
        z3::check_result is_sat = s.check(lits);
        if (is_sat != z3::unsat) return s.ctx().bool_val(true);

        z3::expr_vector core = s.unsat_core();
        z3::expr_vector minimized_core = minimize_conjunction(core);
        // assert(is_equivalent(z3::mk_and(minimized_core), z3::mk_and(core)));
        z3::expr_vector not_core(s.ctx());
        for (const auto& a : minimized_core) {
            not_core.push_back(!a);
        }
        return z3::mk_or(not_core);
    }

    z3::expr_vector
    Logic::to_cnf(const z3::expr& fml) {
        z3::context& ctx = fml.ctx();
        z3::expr_vector cnf_clauses(ctx);
        auto atms = atoms(fml);
        z3::solver s(ctx);
        z3::solver snot(ctx);
        snot.add(!fml);
        s.add(fml);

        while (true) {
            const auto result = snot.check();
            if (result == z3::unsat) break;
            if (result == z3::unknown) {
                // An unknown query does not justify dropping the formula.
                cnf_clauses.push_back(fml);
                return cnf_clauses;
            }
            z3::expr clause = implicant(atms, s, snot);
            if (clause.is_true()) {
                cnf_clauses.push_back(fml);
                return cnf_clauses;
            }
            cnf_clauses.push_back(clause.simplify());
            snot.add(clause);
        }
        return cnf_clauses;
    }

    static z3::expr_vector
    clause2list(const z3::expr& clause) {
        z3::expr_vector literals(clause.ctx());
        if (clause.decl().decl_kind() == Z3_OP_OR) {
            for (unsigned i = 0; i < clause.num_args(); ++i) {
                auto partial = clause2list(clause.arg(i));
                for (auto lit : partial) {
                    literals.push_back(lit);
                }
            }
        } else {
            literals.push_back(clause);
        }
        return literals;
    }



    z3::expr_vector
    Logic::to_dnf(const z3::expr& fml) {
        // S <=> ((a1 ^ .. ^ a_n || ... ||  b1 ^ .. ^ b_m))
        // not S <=> ((!a1 || ... || !a_n) && ... && (!b1 || ... || !b_m))
        auto neg_fml = to_cnf(!fml);
        z3::params p(fml.ctx());
        z3::expr_vector res(fml.ctx());
        for (auto clause : neg_fml) {
            auto clause_list = clause2list(clause);
            z3::expr_vector neg_clause_list(fml.ctx());
            for (auto lit : clause_list) {
                neg_clause_list.push_back(!lit);
            }
            res.push_back(z3::mk_and(neg_clause_list));
        }
        return res;
    }

    std::vector<z3::expr_vector>
    Logic::dnf_rec(const z3::expr_vector& clauses) {
        if (clauses.size() == 0) {
            return {};
        }
        auto last_clause = clause2list(clauses.back());
        if (clauses.size() == 1) {
            return {last_clause};
        }
        // auto clauses_except_last = clauses;
        z3::expr_vector clauses_except_last(clauses.ctx());
        for (unsigned i = 0; i < clauses.size() - 1; ++i) {
            clauses_except_last.push_back(clauses[i]);
        }
        auto pre_conjuncts = dnf_rec(clauses_except_last);
        std::vector<z3::expr_vector> dnf_conjuncts;
        for (const auto conjunct : pre_conjuncts) {
            for (const auto literal : last_clause) {
                z3::expr_vector new_conjunct(conjunct);
                new_conjunct.push_back(literal);
                dnf_conjuncts.push_back(new_conjunct);
            }
        }
        return dnf_conjuncts;
    }

    z3::expr_vector
    LinearLogic::solve_vars_linear(z3::expr constraints, z3::expr_vector vars) {
        auto& ctx = constraints.ctx();
        auto literals = to_cnf(constraints);
        for (const auto& clause : literals) {
            assert(is_literal(clause));
        }
        z3::expr target_expr(ctx);
        z3::solver s(constraints.ctx());
        s.add(constraints);
        bool found = false;
        z3::expr_vector eqs(ctx);
        for (auto literal : literals) {
            auto lit_vars = collect_vars(literal);
            bool is_itersect = false; // check if the literal contains any of the vars
            for (auto var : vars) {
                if (lit_vars.contains(var)) {
                    is_itersect = true;
                    break;
                }
            }
            if (!is_itersect) continue;
            const auto atom = literal.is_not() ? literal.arg(0) : literal;
            if (!(is_ge(atom) || is_le(atom) || is_eq(atom) ||
                  is_lt(atom) || is_gt(atom)) || atom.num_args() != 2 ||
                !atom.arg(0).is_int() || !atom.arg(1).is_int()) continue;
            auto normalized = normalize(literal);
            auto target = normalized.arg(0);
            s.push();
            s.add(target != 0);
            if (s.check() == z3::unsat && !ari_exe::implies(z3::mk_and(eqs), target == 0)) {
                eqs.push_back(target == 0);
            }
            if (eqs.size() >= vars.size()) {
                break;
            }
            s.pop();
        }
        if (eqs.size() < vars.size()) {
            return z3::expr_vector(constraints.ctx());
        }
        return solve_linear_equations(eqs, vars);
    }

    z3::expr
    LinearLogic::normalize(z3::expr literal) {
        // check it is in LIA
        assert(is_literal(literal));

        z3::expr atom = literal;
        if (literal.is_not()) {
            atom = literal.arg(0);
        }

        z3::expr lhs = atom.arg(0) - atom.arg(1);
        assert(lhs.is_int());
        if (is_ge(atom)) {
            lhs = lhs; 
        } else if (is_le(atom)) {
            lhs = -lhs;
        } else if (is_lt(atom)) {
            lhs = -lhs - 1;
        } else if (is_gt(atom)) {
            lhs = lhs - 1;
        } else if (is_eq(atom)) {
            lhs = lhs;
        } else {
            assert(false && "Unsupported operator");
        }
        if (literal.is_not()) {
            lhs = -lhs - 1;
        }
        return lhs >= 0;
    }

    bool
    Logic::is_literal(const z3::expr& expr) {
        return is_atom(expr) || expr.is_not() && is_atom(expr.arg(0));
    }

    z3::expr
    LinearLogic::get_coeff(z3::expr expr, z3::expr var) {
        z3::expr_vector src(var.ctx());
        z3::expr_vector dst(var.ctx());
        src.push_back(var);
        dst.push_back(var - 1);
        auto coeff = (expr - expr.substitute(src, dst)).simplify();
        return coeff;
    }

    z3::expr_vector
    LinearLogic::solve_linear_equations(z3::expr_vector equations, z3::expr_vector vars) {
        if (vars.empty() || equations.size() != vars.size()) {
            return z3::expr_vector(vars.ctx());
        }
        auto [A, b] = build_matrix_form(equations, vars);
        const auto mentions_unknown = [&](const z3::expr& expression) {
            const auto used = collect_vars(expression);
            for (const auto& variable : vars) if (used.contains(variable)) return true;
            return false;
        };
        for (unsigned i = 0; i < vars.size(); ++i) {
            if (mentions_unknown(b(i, 0))) return z3::expr_vector(vars.ctx());
            b(i, 0) = z3::to_real(b(i, 0)).simplify();
            for (unsigned j = 0; j < vars.size(); ++j) {
                const auto coefficient = A(i, j).simplify();
                if (!coefficient.is_numeral()) return z3::expr_vector(vars.ctx());
                A(i, j) = z3::to_real(coefficient).simplify();
            }
        }
        z3::expr_vector res(vars.ctx());
        try {
            auto sol = A.solve(A, b);
            for (int i = 0; i < vars.size(); ++i) {
                // Eliminate over rationals; the SMT certificate below checks
                // the resulting integer candidate against the source system.
                res.push_back(z3::expr(vars.ctx(), Z3_mk_real2int(
                    vars.ctx(), sol(i, 0))).simplify());
            }
        } catch (const std::domain_error&) {
            return z3::expr_vector(vars.ctx());
        }
        z3::solver certificate(vars.ctx());
        certificate.set("timeout", 100u);
        certificate.add(z3::mk_and(equations));
        if (certificate.check() != z3::sat) return z3::expr_vector(vars.ctx());
        z3::expr_vector equalities(vars.ctx());
        for (unsigned i = 0; i < vars.size(); ++i) equalities.push_back(vars[i] == res[i]);
        certificate.add(!z3::mk_and(equalities));
        if (certificate.check() != z3::unsat) return z3::expr_vector(vars.ctx());
        return res;
    }

    std::pair<Matrix, Matrix>
    LinearLogic::build_matrix_form(z3::expr_vector equations, z3::expr_vector vars) {
        Matrix A(equations.size(), vars.size());
        Matrix b(equations.size(), 1);
        auto& ctx = equations.ctx();
        for (int i = 0; i < equations.size(); ++i) {
            auto equation = equations[i];
            assert(equation.is_app() && equation.decl().decl_kind() == Z3_OP_EQ);
            auto linear_expr = equation.arg(0) - equation.arg(1);
            b(i, 0) = linear_expr;
            // std::cout << b(i, 0).to_string() << std::endl;
            for (int j = 0; j < vars.size(); ++j) {
                auto coeff = get_coeff(linear_expr, vars[j]);
                // coeff = ctx.real_val(coeff.as_int64());
                A(i, j) = coeff;
                b(i, 0) = b(i, 0) - coeff * vars[j];
            }
            b(i, 0) = (-b(i, 0)).simplify();
        }
        return {A, b};
    }

    z3::expr
    Logic::simplify(const z3::expr& fml) {
        auto cnf = to_cnf(fml);
        z3::expr_vector res(fml.ctx());
        for (auto clause : cnf) {
            res.push_back(clause.simplify());
        }
        return z3::mk_and(res);
    }
}
