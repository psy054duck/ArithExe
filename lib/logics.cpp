#include "logics.h"


#include "z3++.h"

#include <set>
#include <unordered_set>
#include <spdlog/spdlog.h>
#include <iostream>
#include <algorithm>

using namespace ari_exe;
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
    z3::expr_vector vars_and_tmp_vars(vars);
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
        for (auto v : partial_value) dst.push_back(v);
        z3::expr cond = conjunct.substitute(vars_and_tmp_vars, dst);
        conds.push_back(cond.simplify());
    }

    // combine results uisng if-then-else
    z3::expr_vector res = complete_values[0];
    for (int i = 1; i < complete_values.size(); ++i) {
        for (int j = 0; j < res.size(); ++j) {
            res[j] = z3::ite(conds[i], complete_values[i][j], res[j]);
        }
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
    if (t.arg(0).is_bool()) return false;
    if (k == Z3_OP_TRUE || k == Z3_OP_FALSE || k == Z3_OP_XOR || k == Z3_OP_NOT) return false;
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
    z3::expr_vector not_core(s.ctx());
    for (const auto& a : core) {
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
    // auto atms = atoms(fml);
    // z3::solver s_atoms(fml.ctx());
    // s_atoms.add(atms);
    // z3::expr_vector cnf_clauses = to_cnf(fml);
    // spdlog::debug("CNF size: {}", cnf_clauses.size());
    // std::vector<z3::expr_vector> dnf_conjuncts = dnf_rec(cnf_clauses);
    // z3::expr_vector conjuncts(fml.ctx());
    // for (const auto& conjunct : dnf_conjuncts) {
    //     z3::expr dnf_clause = conjunct[0];
    //     for (unsigned i = 1; i < conjunct.size(); ++i) {
    //         dnf_clause = dnf_clause && conjunct[i];
    //     }
    //     conjuncts.push_back(dnf_clause);
    // }
    // return conjuncts;
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
        auto normalized = normalize(literal);
        auto target = normalized.arg(0);
        s.push();
        s.add(target != 0);
        if (s.check() == z3::unsat) {
            eqs.push_back(target == 0);
        }
        s.pop();
    }
    return solve_linear_equations(eqs, vars);
    // get the coefficient of var
    // z3::expr_vector src(var.ctx());
    // z3::expr_vector dst(var.ctx());
    // src.push_back(var);
    // dst.push_back(var - 1);
    // z3::expr coeff = (target_expr - target_expr.substitute(src, dst)).simplify();
    // auto res = ((target_expr - coeff*var) / -coeff).simplify();
    // return res;
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
    assert((coeff == 1).simplify().is_true() && "support only 1 coeff now");
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