#include "logics.h"


#include "z3++.h"

#include <set>
#include <unordered_set>
#include <spdlog/spdlog.h>
#include <iostream>
#include <algorithm>

namespace ari_exe {

    z3::expr
    simplify(const z3::expr& expr, std::optional<z3::expr> assumption) {
        auto logic = Logic();
        auto new_expr = expr.simplify();
        auto clauses= logic.to_cnf(new_expr);
        auto solver = z3::solver(expr.ctx());
        if (assumption.has_value()) {
            solver.add(assumption.value());
        }
        z3::expr_vector remains(expr.ctx());
        for (const auto& clause : clauses) {
            solver.push();
            solver.add(!clause);
            if (solver.check() == z3::sat) {
                // this clause is not entailed, so keep it
                remains.push_back(clause);
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
        auto& z3ctx = e1.ctx();
        z3::solver s(z3ctx);
        z3::params p(z3ctx);
        p.set("timeout", 3*1000u);
        s.set(p);
        s.add(cond);
        s.add(e1 != e2);
        s.check();
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
        auto [conditions, expressions] = expr2piecewise(f);
        auto solver = z3::solver(f.ctx());
        solver.add(domain);
        auto feasible_conditions = z3::expr_vector(f.ctx());
        auto feasible_expressions = z3::expr_vector(f.ctx());
        for (int i = 0; i < conditions.size(); ++i) {
            // auto restricted_conditions = simplify(conditions[i] && domain);
            auto restricted_conditions = simplify(conditions[i], domain);
            solver.push();
            solver.add(restricted_conditions);
            if (solver.check() == z3::sat) {
                feasible_conditions.push_back(restricted_conditions);
                feasible_expressions.push_back(expressions[i]);
            }
            solver.pop();
        }
        return piecewise2ite(feasible_conditions, feasible_expressions);
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
        if (expr.is_const() || expr.is_var()) {
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
        auto aux_vars = logic.collect_aux_vars(eliminated_expr);
        if (!aux_vars.empty()) {
            // If there are auxiliary variables, we need to eliminate them
            auto exits_expr = z3::exists(aux_vars, eliminated_expr);
            auto qe_goal = z3::goal(expr.ctx());
            qe_goal.add(exits_expr);
            auto qe_res = elim_temp(qe_goal);
            z3::expr_vector qe_conditions(expr.ctx());
            for (int j = 0; j < qe_res.size(); ++j) {
                qe_conditions.push_back(qe_res[j].as_expr());
            }
            eliminated_expr = z3::mk_or(qe_conditions).simplify();
            assert(is_equivalent(eliminated_expr, expr));
        }
        return eliminated_expr.simplify();
    }

    z3::expr
    piecewise2ite(const z3::expr_vector& conditions, const z3::expr_vector& expressions) {
        if (conditions.size() != expressions.size()) {
            throw std::invalid_argument("Conditions and expressions must have the same size.");
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
        auto [processed_cons, tmp_vars] = preprocess_constraints(constraints);

        auto dnf = to_dnf(processed_cons);
        spdlog::debug("DNF size: {}", dnf.size());

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

        // combine results uisng if-then-else
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
        if (!t.is_app()) return false;
        Z3_decl_kind k = t.decl().decl_kind();
        if (k == Z3_OP_AND || k == Z3_OP_OR || k == Z3_OP_IMPLIES) return false;
        // if (k == Z3_OP_EQ && t.arg(0).is_bool()) return false;
        if (k == Z3_OP_TRUE || k == Z3_OP_FALSE || k == Z3_OP_XOR || k == Z3_OP_NOT) return false;
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
    minimize_conjunction(const z3::expr_vector& conjunction, int pivot) {
        if (pivot >= conjunction.size()) {
            return conjunction; // nothing to minimize
        }
        auto solver = z3::solver(conjunction.ctx());
        z3::expr pivot_expr = conjunction[pivot];
        auto remaining_exprs = get_expr_vec_except(conjunction, pivot);
        solver.add(remaining_exprs);
        solver.add(!pivot_expr);
        if (solver.check() == z3::unsat) {
            // the pivot is entailed, so we can remove it
            return minimize_conjunction(remaining_exprs, pivot);
        } else {
            return minimize_conjunction(conjunction, ++pivot);
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
        assert(is_sat == z3::unsat);

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

        while (snot.check() == z3::sat) {
            z3::expr clause = implicant(atms, s, snot);
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
        auto [A, b] = build_matrix_form(equations, vars);
        auto sol = A.solve(A, b);
        z3::expr_vector res(vars.ctx());
        for (int i = 0; i < vars.size(); ++i) {
            res.push_back(sol(i, 0));
        }
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