#include "logics.h"

using namespace ari_exe;

#include "z3++.h"

#include <set>
#include <unordered_set>
#include <spdlog/spdlog.h>
#include <iostream>

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

std::optional<z3::expr>
LinearLogic::solve_var(const z3::expr& constraints, z3::expr& var) {
    auto dnf = to_dnf(constraints);
    spdlog::debug("DNF size: {}", dnf.size());
    z3::expr_vector conds(constraints.ctx());
    z3::expr_vector values(constraints.ctx());
    for (auto conjunct : dnf) {
        auto partial = solve_var_linear(conjunct, var);
        if (!partial.has_value()) {
            return std::nullopt;
        }
        auto partial_value = partial.value();
        values.push_back(partial_value);
        z3::expr_vector src(var.ctx());
        z3::expr_vector dst(var.ctx());
        src.push_back(var);
        dst.push_back(partial_value);
        z3::expr cond = conjunct.substitute(src, dst);
        conds.push_back(cond.simplify());
    }
    z3::expr res = values[0];
    for (int i = 1; i < values.size(); ++i) {
        res = z3::ite(conds[i], values[i], res);
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
        return;
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
    z3::expr val = m.eval(a, true);
    if (z3::eq(val, ctx.bool_val(true))) {
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
    z3::expr clause = s.ctx().bool_val(false);
    if (core.size() > 0)
        clause = !core[0];
    for (unsigned i = 1; i < core.size(); ++i) {
        clause = clause || !core[i];
    }
    return clause;
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
    z3::expr_vector cnf_clauses = to_cnf(fml);
    std::vector<z3::expr_vector> dnf_conjuncts = dnf_rec(cnf_clauses);
    z3::expr_vector conjuncts(fml.ctx());
    for (const auto& conjunct : dnf_conjuncts) {
        z3::expr dnf_clause = conjunct[0];
        for (unsigned i = 1; i < conjunct.size(); ++i) {
            dnf_clause = dnf_clause && conjunct[i];
        }
        conjuncts.push_back(dnf_clause);
    }
    return conjuncts;
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
    for (const auto& conjunct : pre_conjuncts) {
        for (const auto& literal : last_clause) {
            z3::expr_vector new_conjunct(conjunct);
            new_conjunct.push_back(literal);
            dnf_conjuncts.push_back(new_conjunct);
        }
    }
    return dnf_conjuncts;
}



std::optional<z3::expr>
LinearLogic::solve_var_linear(z3::expr& constraints, z3::expr& var) {
    auto literals = to_cnf(constraints);
    for (const auto& clause : literals) {
        assert(is_literal(clause));
    }
    z3::expr target_expr(var.ctx());
    z3::solver s(constraints.ctx());
    s.add(constraints);
    bool found = false;
    for (auto literal : literals) {
        auto normalized = normalize(literal);
        auto vars = collect_vars(normalized);
        if (!vars.contains(var)) {
            continue;
        }
        s.push();
        s.add(normalized.arg(0) != 0);
        if (s.check() == z3::unsat) {
            target_expr = normalized.arg(0);
            found = true;
            break;
        }
        s.pop();
    }
    if (!found) {
        return std::nullopt;
    }
    // get the coefficient of var
    z3::expr_vector src(var.ctx());
    z3::expr_vector dst(var.ctx());
    src.push_back(var);
    dst.push_back(var - 1);
    z3::expr coeff = (target_expr - target_expr.substitute(src, dst)).simplify();
    auto res = ((target_expr - coeff*var) / -coeff).simplify();
    return res;
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