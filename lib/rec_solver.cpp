#include "rec_solver.h"
#include <iostream>
#include <numeric>
#include "boost/algorithm/string/join.hpp"

using namespace ari_exe;

static void
combine_vec(z3::expr_vector& vec1, const z3::expr_vector& vec2) {
    for (z3::expr e : vec2) {
        vec1.push_back(e);
    }
}

void
rec_solver::set_ind_var(z3::expr var) {
    ind_var = var;
}

rec_solver
rec_solver::operator=(const rec_solver& other) {
    ind_var = other.ind_var;
    initial_values_k = other.initial_values_k;
    initial_values_v = other.initial_values_v;
}

z3::expr_vector
find_all_app_of_decl(z3::func_decl func, z3::expr e, z3::context& z3ctx) {
    z3::expr_vector res(z3ctx);
    auto args = e.args();
    auto kind = e.decl().decl_kind();
    if (kind == Z3_OP_ADD || kind == Z3_OP_MUL || kind == Z3_OP_SUB) {
        for (auto arg : args) {
            combine_vec(res, find_all_app_of_decl(func, arg, z3ctx));
        }
    } else if (func.id() == e.decl().id()) {
        res.push_back(e);
    }
    return res;
}

z3::expr
coeff_of(z3::expr e, z3::expr term, z3::context& z3ctx) {
    auto kind = e.decl().decl_kind();
    auto args = e.args();
    z3::expr res = z3ctx.int_val(0);
    if (kind == Z3_OP_ADD) {
        for (auto arg : args) {
            res = res + coeff_of(arg, term, z3ctx);
        }
    } else if (kind == Z3_OP_SUB) {
        assert(args.size() == 2);
        res = coeff_of(args[0], term, z3ctx) - coeff_of(args[1], term, z3ctx);
    } else if (kind == Z3_OP_MUL) {
        int i = 0;
        for (i = 0; i < args.size(); i++) {
            if ((args[i] == term).simplify().is_true()) break;
        }
        if (i != args.size()) {
            res = z3ctx.int_val(1);
            for (int j = 0; j < args.size(); j++) {
                if (j == i) continue;
                res = res * args[j];
            }
        }
    } else if ((e == term).simplify().is_true()) {
        res = z3ctx.int_val(1);
    }
    return res.simplify();
}

static bool
is_one_stride_simple_rec(z3::expr lhs, z3::expr rhs) {
    assert(lhs.decl().arity() == 1);
    z3::expr lhs_arg = lhs.arg(0);
}

void
rec_solver::add_assumption(z3::expr e) {
    assumption  = assumption && e;
}

rec_solver::rec_solver(rec_ty& eqs, z3::expr var, z3::context& z3ctx): z3ctx(z3ctx), ind_var(z3ctx), initial_values_k(z3ctx), initial_values_v(z3ctx), assumption(z3ctx.bool_val(true)) {
    set_eqs(eqs);
    set_ind_var(var);
}

void
rec_solver::set_eqs(const std::vector<z3::expr>& _conds, const std::vector<rec_ty>& _exprs) {
    conds = _conds;
    exprs = _exprs;
}

void rec_solver::set_eqs(rec_ty& eqs) {
    for (auto r : eqs) {
        rec_eqs.insert_or_assign(r.first, hoist_ite(r.second));
    }
}

bool rec_solver::solve() {
    rec2file();
    std::string cmd = "python solver.py tmp/recurrence.txt " + ind_var.to_string() + " 2>/dev/null";
    int err = system(cmd.c_str());
    // int err = system("python rec_solver.py tmp/test.txt");
    if (err) {
        return false;
    }
    file2z3();
    return true;
}

static std::set<z3::expr>
get_all_lhs(const std::vector<rec_ty>& exprs) {
    std::set<z3::expr> res;
    for (auto& expr : exprs) {
        for (auto& eq : expr) {
            res.insert(eq.first);
        }
    }
    return res;
}

void rec_solver::expr_solve(z3::expr e) {
    z3::expr_vector all_apps(z3ctx);
    z3::solver solver(z3ctx);
    
    for (auto& i : rec_eqs) {
        solver.add(i.first == i.second);
    }
    z3::expr_vector ind_vars(z3ctx);
    z3::expr_vector ind_varps(z3ctx);
    z3::expr_vector zeros(z3ctx);
    ind_vars.push_back(ind_var);
    ind_varps.push_back(ind_var + 1);
    zeros.push_back(z3ctx.int_val(0));
    z3::expr ep = e.substitute(ind_vars, ind_varps);
    z3::expr d = z3ctx.int_const("_d");
    solver.push();
    solver.add(ep == e + d);
    auto check_res = solver.check();
    bool solved = false;
    if (check_res == z3::sat) {
        z3::model m = solver.get_model();
        z3::expr instance_d = m.eval(d);
        solver.pop();
        solver.push();
        solver.add(ep != e + instance_d);
        auto check_res = solver.check();
        if (check_res == z3::unsat) {
            z3::expr closed = e.substitute(ind_vars, zeros) + instance_d*ind_var;
            res.insert_or_assign(e, closed);
            solved = true;
        }
        solver.pop();
    } else {
        solver.pop();
    }
    if (!solved) {
        solver.push();
        solver.add(ep == d);
        auto check_res = solver.check();
        if (check_res == z3::sat) {
            z3::model m = solver.get_model();
            z3::expr instance_d = m.eval(d);
            solver.pop();
            solver.add(ep != instance_d);
            auto check_res = solver.check();
            if (check_res == z3::unsat) {
                res.insert_or_assign(e, instance_d);
            }
        }
    }
}

closed_form_ty rec_solver::get_res() {
    return res;
}

void rec_solver::add_initial_values(z3::expr_vector k, z3::expr_vector v) {
    int size = k.size();
    for (int i = 0; i < size; i++) {
        bool found = false;
        for (int j = 0; j < initial_values_k.size(); j++) {
            z3::expr diff = (initial_values_k[j] - k[i]).simplify();
            if (diff.is_numeral() && diff.get_numeral_int64() == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            initial_values_k.push_back(k[i]);
            initial_values_v.push_back(v[i]);
        }
    }
}

void rec_solver::apply_initial_values() {
    for (auto& r : res) {
        r.second = r.second.substitute(initial_values_k, initial_values_v);
    }
}

void rec_solver::rec2file() {
    // if tmp oflder does not exist, create it
    if (system("mkdir -p tmp") == -1) {
        std::cerr << "Error creating tmp directory\n";
        assert(false);
    }
    std::ofstream out("tmp/recurrence.txt", std::ios::out);
    if (!out.is_open()) {
        std::cerr << "Error opening file for writing\n";
        assert(false);
    }
    _rec2file(out);
    out.close();
}

std::string rec_solver::z3_infix(z3::expr e) {
    if (e.is_const() || e.is_numeral()) {
        if (e.to_string().starts_with("nondet")) {
            return "*";
        }
        return e.to_string();
    }
    auto kind = e.decl().decl_kind();
    auto args = e.args();
    std::vector<std::string> args_infix;
    for (auto a : args) {
        args_infix.push_back("(" + z3_infix(a) + ")");
    }

    if (kind == Z3_OP_ADD) {
        // assert(args.size() == 2);
        std::string args_str = boost::join(args_infix, " + ");
        return args_str;
        // return args_infix[0] + " + " + args_infix[1];
    } else if (kind == Z3_OP_MOD) {
        return args_infix[0] + " % " + args_infix[1];
    } else if (kind == Z3_OP_SUB) {
        assert(args.size() == 2);
        return args_infix[0] + " - " + args_infix[1];
    } else if (kind == Z3_OP_MUL) {
        std::string args_str = boost::join(args_infix, " * ");
        return args_str;
        return args_infix[0] + " * " + args_infix[1];
    } else if (kind == Z3_OP_DIV || kind == Z3_OP_IDIV) {
        assert(args.size() == 2);
        return args_infix[0] + " / " + args_infix[1];
    } else if (kind == Z3_OP_LE) {
        assert(args.size() == 2);
        return args_infix[0] + " <= " + args_infix[1];
    } else if (kind == Z3_OP_LT) {
        assert(args.size() == 2);
        return args_infix[0] + " < " + args_infix[1];
    } else if (kind == Z3_OP_GT) {
        assert(args.size() == 2);
        return args_infix[0] + " > " + args_infix[1];
    } else if (kind == Z3_OP_GE) {
        assert(args.size() == 2);
        return args_infix[0] + " >= " + args_infix[1];
    } else if (kind == Z3_OP_EQ) {
        assert(args.size() == 2);
        return args_infix[0] + " == " + args_infix[1];
    } else if (kind == Z3_OP_DISTINCT) {
        assert(args.size() == 2);
        return args_infix[0] + " != " + args_infix[1];
    } else if (kind == Z3_OP_AND) {
        std::string res = args_infix[0];
        for (int i = 1; i < args_infix.size(); i++)
            res = res + ", " + args_infix[i];
        return "And(" + res + ")";
    } else if (kind == Z3_OP_OR) {
        std::string res = args_infix[0];
        for (int i = 1; i < args_infix.size(); i++)
            res = res + ", " + args_infix[i];
        return "Or(" + res + ")";
    } else if (kind == Z3_OP_NOT) {
        assert(args.size() == 1);
        return "!" + args_infix[0];
    } else if (kind == Z3_OP_UNINTERPRETED) {
        std::string f = e.decl().name().str();
        std::string args_str = boost::join(args_infix, ", ");
        std::string s = f + "(" + args_str + ")";
        return s;
    } else {
        std::cout << e.to_string() << "\n";
        std::cout << kind << "\n";
        abort();
        // return e.to_string();
        // std::cout << e.to_string() << "\n";
        // assert(false);
    }
}

void rec_solver::_rec2file(std::ofstream& out) {
    if (!is_formatted()) {
        _format();
    }
    // z3::expr_vector src(z3ctx);
    // z3::expr_vector dst(z3ctx);
    int name_idx = 0;
    for (int i = 0; i < initial_values_k.size(); i++) {
        z3::expr lhs = initial_values_k[i];
        z3::expr rhs = initial_values_v[i];
        // z3::expr new_lhs = lhs.substitute(src, dst);
        // initial_src.push_back(new_lhs);
        // initial_dst.push_back(rhs);
        out << z3_infix(lhs) << " = " << z3_infix(rhs) << ";\n";
    }

    z3::expr first_cond = z3ctx.bool_val(true);
    if (conds.size() > 0) first_cond = conds[0];
    // out << "if (" << z3_infix(first_cond.substitute(src, dst)) << ") {\n";
    out << "if (" << z3_infix(first_cond) << ") {\n";
    for (auto k_e : exprs[0]) {
        z3::expr lhs = k_e.first;
        z3::expr rhs = k_e.second;
        // out << "\t" << z3_infix(lhs.substitute(src, dst)) << " = " << z3_infix(rhs.substitute(src, dst)) << ";\n";
        out << "\t" << z3_infix(lhs) << " = " << z3_infix(rhs) << ";\n";
    }
    out << "} ";
    for (int i = 1; i < conds.size(); i++) {
        // out << "else if (" << z3_infix(conds[i].substitute(src, dst)) << ") {\n";
        out << "else if (" << z3_infix(conds[i]) << ") {\n";
        for (auto k_e : exprs[i]) {
            z3::expr lhs = k_e.first;
            z3::expr rhs = k_e.second;
            // out << "\t" << z3_infix(lhs.substitute(src, dst)) << " = " << z3_infix(rhs.substitute(src, dst)) << ";\n";
            out << "\t" << z3_infix(lhs) << " = " << z3_infix(rhs) << ";\n";
        }
        out << "} ";
    }
    if (conds.size() < exprs.size() && conds.size() > 0) {
        out << "else {\n";
        for (auto k_e : exprs.back()) {
            z3::expr lhs = k_e.first;
            z3::expr rhs = k_e.second;
            // out << "\t" << z3_infix(lhs.substitute(src, dst)) << " = " << z3_infix(rhs.substitute(src, dst)) << ";\n";
            out << "\t" << z3_infix(lhs) << " = " << z3_infix(rhs) << ";\n";
        }
        out << "}";
    }
    // return {initial_src, initial_dst};
}

void rec_solver::_format() {
    // std::vector<z3::expr> largest_conds;
    for (auto r : rec_eqs) {
        // std::cout << r.first.to_string() << " = " << r.second.to_string() << "\n";
        auto cur_conds = parse_cond(r.second);
        // for (auto e : cur_conds) {
        //     std::cout << e.to_string() << "\n";
        // }
        // std::cout << "************\n";
        if (cur_conds.size() > conds.size()) {
            conds = cur_conds;
        }
    }
    z3::expr acc_cond = z3ctx.bool_val(true);

    exprs.push_back({});
    for (auto c : conds) exprs.push_back({});

    for (auto r : rec_eqs) {
        auto cur_conds = parse_cond(r.second);
        auto cur_exprs = parse_expr(r.second);
        // rec_ty cur_k_e;
        assert(cur_conds.size() == conds.size() || cur_conds.size() == 0);
        for (int i = 0; i < conds.size(); i++) {
            z3::expr e = cur_exprs[0];
            if (cur_conds.size() == conds.size())
                e = cur_exprs[i];
            exprs[i].insert_or_assign(r.first, e);
        }
        if (conds.size() < cur_exprs.size() || cur_conds.size() == 0) {
            z3::expr e = cur_exprs.back();
            exprs.back().insert_or_assign(r.first, e);
        }
    }
}

std::vector<z3::expr> rec_solver::parse_cond(z3::expr e) {
    auto kind = e.decl().decl_kind();
    auto args = e.args();
    std::vector<z3::expr> res;
    if (kind == Z3_OP_ITE) {
        res.push_back(args[0]);
        if (!is_ite_free(args[2])) {
            for (auto e : parse_cond(args[2])) {
                res.push_back(e);
            }
        }
    }
    return res;
}

// std::pair<std::vector<z3::expr>, std::vector<z3::expr>>
// rec_solver::parse_expr_(z3::expr e) {
//     std::vector<z3::expr> conds;
//     std::vector<z3::expr> bodies;
//     auto kind = e.decl().decl_kind();
//     auto args = e.args();
//     if (e.is_numeral() || e.is_const()) {
//         return {conds, {e}};
//     } else if (kind == Z3_OP_ADD) {
//         for (auto arg : args) {
//             auto arg_cond_body = parse_expr_(arg);
//         }
//     }
// }

std::vector<z3::expr> rec_solver::parse_expr(z3::expr e) {
    auto kind = e.decl().decl_kind();
    auto args = e.args();
    // assert(kind == Z3_OP_ITE);
    std::vector<z3::expr> res;
    if (is_ite_free(e)) {
        res.push_back(e);
    } else if (kind == Z3_OP_ITE) {
        for (auto ep : parse_expr(args[1])) {
            res.push_back(ep);
        }
        for (auto ep : parse_expr(args[2])) {
            res.push_back(ep);
        }
    }
    return res;
}

bool rec_solver::is_ite_free(z3::expr e) {
    auto kind = e.decl().decl_kind();
    auto args = e.args();
    if (e.is_numeral() || e.is_const()) {
        return true;
    }
    if (kind == Z3_OP_ITE) {
        return false;
    }
    bool res = true;
    for (auto ep : args) {
        res = res && is_ite_free(ep);
    }
    return res;
}

void rec_solver::file2z3() {
    _file2z3("tmp/closed.smt2");
}

void rec_solver::_file2z3(const std::string& filename) {
    z3::expr_vector c = z3ctx.parse_file(filename.data());
    for (auto e : c) {
        auto kind = e.decl().decl_kind();
        auto args = e.args();
        assert(kind == Z3_OP_EQ);
        z3::expr k = args[0].simplify();
        z3::expr v = args[1].simplify();
        if (k.is_numeral()) {
            k = args[1];
            v = args[0];
        }
        res.insert_or_assign(k, v.simplify());
    }
}

void rec_solver::print_recs() {
    for (auto r : rec_eqs) {
        std::cout << r.first.to_string() << " = " << r.second.to_string() << "\n";
    }
}

void rec_solver::print_res() {
    for (auto r : res) {
        std::cout << r.first.to_string() << " = " << r.second.to_string() << "\n";
    }
}

z3::expr rec_solver::hoist_ite(z3::expr e) {
    auto kind = e.decl().decl_kind();
    auto args = e.args();
    if (e.is_numeral() || e.is_const() || kind == Z3_OP_ITE) {
        return e;
    }
    z3::expr_vector hoisted_args(z3ctx);
    for (auto arg : args) {
        hoisted_args.push_back(hoist_ite(arg));
    }
    int which = 0;
    for (; which < hoisted_args.size() && hoisted_args[which].decl().decl_kind() != Z3_OP_ITE; which++);
    if (which == hoisted_args.size()) {
        return e;
    }
    auto ite_args = hoisted_args[which].args();
    z3::expr cond = ite_args[0];
    z3::expr body_true = ite_args[1];
    z3::expr body_false = ite_args[2];
    if (kind == Z3_OP_ADD) {
        for (int i = 0; i < hoisted_args.size(); i++) {
            if (i == which) continue;
            body_true = body_true + hoisted_args[i];
            body_false = body_false + hoisted_args[i];
        }
    } else if (kind == Z3_OP_MUL) {
        for (int i = 0; i < hoisted_args.size(); i++) {
            if (i == which) continue;
            body_true = body_true * hoisted_args[i];
            body_false = body_false * hoisted_args[i];
        }
    } else if (kind == Z3_OP_SUB) {
        for (int i = 0; i < hoisted_args.size(); i++) {
            if (i == which) continue;
            if (i < which) {
                body_true = hoisted_args[i] - body_true;
                body_false = hoisted_args[i] - body_false;
            } else {
                body_true = body_true - hoisted_args[i];
                body_false = body_false - hoisted_args[i];
            }
        }
    } else if (kind == Z3_OP_IDIV) {
        for (int i = 0; i < hoisted_args.size(); i++) {
            if (i == which) continue;
            if (i < which) {
                body_true = hoisted_args[i] / body_true;
                body_false = hoisted_args[i] / body_false;
            } else {
                body_true = body_true / hoisted_args[i];
                body_false = body_false / hoisted_args[i];
            }
        }
    } else {
        std::cout << e.to_string() << "\n";
        std::cout << kind << "\n";
        abort();
    }
    return z3::ite(cond, body_true, body_false);
}