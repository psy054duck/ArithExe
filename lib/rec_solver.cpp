#include "rec_solver.h"
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "boost/algorithm/string/join.hpp"

using namespace ari_exe;

namespace {
    class SolverRequestError : public std::runtime_error {
        public:
            using std::runtime_error::runtime_error;
    };

    class SolverWorker {
        public:
            ~SolverWorker() {
                stop();
            }

            std::string solve(const std::string& recurrence, const std::string& ind_var) {
                std::lock_guard<std::mutex> guard(mu);
                std::string last_error;
                for (int attempt = 0; attempt < 2; attempt++) {
                    try {
                        ensure_started();
                        return send_solve_request(recurrence, ind_var);
                    } catch (const SolverRequestError&) {
                        throw;
                    } catch (const std::exception& e) {
                        last_error = e.what();
                        stop();
                    }
                }
                throw std::runtime_error("recurrence solver worker failed: " + last_error);
            }

        private:
            pid_t pid = -1;
            int child_stdin = -1;
            int child_stdout = -1;
            uint64_t next_request_id = 1;
            std::mutex mu;

            static void close_fd(int& fd) {
                if (fd != -1) {
                    close(fd);
                    fd = -1;
                }
            }

            static std::string errno_message(const std::string& prefix) {
                return prefix + ": " + std::strerror(errno);
            }

            void ensure_started() {
                if (pid > 0) return;
                start();
            }

            void start() {
                static std::once_flag sigpipe_once;
                std::call_once(sigpipe_once, []() {
                    std::signal(SIGPIPE, SIG_IGN);
                });

                int to_child[2] = {-1, -1};
                int from_child[2] = {-1, -1};
                if (pipe(to_child) == -1) {
                    throw std::runtime_error(errno_message("pipe(to_child) failed"));
                }
                if (pipe(from_child) == -1) {
                    close(to_child[0]);
                    close(to_child[1]);
                    throw std::runtime_error(errno_message("pipe(from_child) failed"));
                }

                pid_t child = fork();
                if (child == -1) {
                    close(to_child[0]);
                    close(to_child[1]);
                    close(from_child[0]);
                    close(from_child[1]);
                    throw std::runtime_error(errno_message("fork failed"));
                }

                if (child == 0) {
                    dup2(to_child[0], STDIN_FILENO);
                    dup2(from_child[1], STDOUT_FILENO);
                    close(to_child[0]);
                    close(to_child[1]);
                    close(from_child[0]);
                    close(from_child[1]);

                    const char* python = std::getenv("ARITHEXE_SOLVER_PYTHON");
                    if (python == nullptr || python[0] == '\0') {
                        python = "python";
                    }
                    const char* worker = std::getenv("ARITHEXE_SOLVER_WORKER");
                    if (worker == nullptr || worker[0] == '\0') {
                        worker = "solver_worker.py";
                    }

                    execlp(python, python, "-u", worker, static_cast<char*>(nullptr));
                    std::cerr << "failed to exec recurrence solver worker: "
                              << std::strerror(errno) << "\n";
                    _exit(127);
                }

                close(to_child[0]);
                close(from_child[1]);
                child_stdin = to_child[1];
                child_stdout = from_child[0];
                pid = child;
            }

            void stop() {
                if (child_stdin != -1) {
                    const char quit[] = "QUIT\n";
                    write(child_stdin, quit, sizeof(quit) - 1);
                }
                close_fd(child_stdin);
                close_fd(child_stdout);
                if (pid > 0) {
                    int status = 0;
                    pid_t exited = waitpid(pid, &status, WNOHANG);
                    if (exited == 0) {
                        kill(pid, SIGTERM);
                        waitpid(pid, &status, 0);
                    }
                    pid = -1;
                }
            }

            static void write_all(int fd, const std::string& data) {
                const char* cursor = data.data();
                size_t remaining = data.size();
                while (remaining > 0) {
                    ssize_t written = write(fd, cursor, remaining);
                    if (written == -1) {
                        if (errno == EINTR) continue;
                        throw std::runtime_error(errno_message("write to solver worker failed"));
                    }
                    if (written == 0) {
                        throw std::runtime_error("write to solver worker wrote zero bytes");
                    }
                    cursor += written;
                    remaining -= static_cast<size_t>(written);
                }
            }

            static std::string read_line(int fd) {
                std::string line;
                char c = '\0';
                while (true) {
                    ssize_t count = read(fd, &c, 1);
                    if (count == -1) {
                        if (errno == EINTR) continue;
                        throw std::runtime_error(errno_message("read from solver worker failed"));
                    }
                    if (count == 0) {
                        throw std::runtime_error("solver worker closed its stdout");
                    }
                    if (c == '\n') {
                        return line;
                    }
                    line.push_back(c);
                    if (line.size() > 4096) {
                        throw std::runtime_error("solver worker response header is too long");
                    }
                }
            }

            static std::string read_exact(int fd, size_t size) {
                std::string data(size, '\0');
                size_t offset = 0;
                while (offset < size) {
                    ssize_t count = read(fd, data.data() + offset, size - offset);
                    if (count == -1) {
                        if (errno == EINTR) continue;
                        throw std::runtime_error(errno_message("read from solver worker failed"));
                    }
                    if (count == 0) {
                        throw std::runtime_error("solver worker closed stdout mid-response");
                    }
                    offset += static_cast<size_t>(count);
                }
                return data;
            }

            std::string send_solve_request(const std::string& recurrence, const std::string& ind_var) {
                std::string request_id = std::to_string(next_request_id++);
                std::string header = "SOLVE " + request_id + " " +
                                     std::to_string(ind_var.size()) + " " +
                                     std::to_string(recurrence.size()) + "\n";
                write_all(child_stdin, header);
                write_all(child_stdin, ind_var);
                write_all(child_stdin, recurrence);

                std::string response_header = read_line(child_stdout);
                std::istringstream header_in(response_header);
                std::string status;
                std::string response_id;
                size_t payload_size = 0;
                if (!(header_in >> status >> response_id >> payload_size)) {
                    throw std::runtime_error("invalid solver worker response header: " + response_header);
                }
                std::string payload = read_exact(child_stdout, payload_size);
                if (response_id != request_id) {
                    throw std::runtime_error("solver worker response id mismatch");
                }
                if (status == "OK") {
                    return payload;
                }
                if (status == "ERR") {
                    throw SolverRequestError(payload);
                }
                throw std::runtime_error("unknown solver worker response status: " + status);
            }
    };

    SolverWorker& solver_worker() {
        static SolverWorker worker;
        return worker;
    }
}

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
    return *this;
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
    try {
        std::string smt2 = solver_worker().solve(rec2string(), ind_var.to_string());
        smt2_to_z3(smt2);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error solving recurrence: " << e.what() << "\n";
        return false;
    }
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

std::string rec_solver::rec2string() {
    std::ostringstream out;
    _rec2file(out);
    return out.str();
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
        assert(false && "Unsupported Z3 operation kind");
        abort();
        // return e.to_string();
        // std::cout << e.to_string() << "\n";
        // assert(false);
    }
}

void rec_solver::_rec2file(std::ostream& out) {
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

void rec_solver::smt2_to_z3(const std::string& smt2) {
    z3::expr_vector c = z3ctx.parse_string(smt2.c_str());
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
