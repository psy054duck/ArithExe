#include "RationalQuery.h"
#include <optional>
#include <vector>

namespace ari_exe {
namespace {
struct Fraction { z3::expr numerator, denominator; };

// Recognize only exact rational polynomials over integer terms. In particular,
// never silently change the domain of a genuine Real variable.
std::optional<Fraction> fraction(const z3::expr& e) {
    auto& c = e.ctx();
    if (e.is_int()) return Fraction{e, c.int_val(1)};
    if (!e.is_real() || !e.is_app()) return {};
    if (e.is_numeral()) {
        z3::expr n(c, Z3_get_numerator(c, e));
        z3::expr d(c, Z3_get_denominator(c, e));
        return Fraction{c.int_val(Z3_get_numeral_string(c, n)),
                        c.int_val(Z3_get_numeral_string(c, d))};
    }
    auto kind = e.decl().decl_kind();
    if (kind == Z3_OP_TO_REAL && e.arg(0).is_int())
        return Fraction{e.arg(0), c.int_val(1)};
    if (kind == Z3_OP_UMINUS) {
        auto a = fraction(e.arg(0));
        if (a) return Fraction{-a->numerator, a->denominator};
        return {};
    }
    if (kind == Z3_OP_POWER && e.arg(1).is_numeral()) {
        int64_t exponent = 0;
        if (!e.arg(1).is_numeral_i64(exponent) || exponent < 0 || exponent > 64)
            return {};
        auto a = fraction(e.arg(0));
        if (!a) return {};
        Fraction power{c.int_val(1), c.int_val(1)};
        for (int64_t i = 0; i < exponent; ++i) {
            power.numerator = power.numerator * a->numerator;
            power.denominator = (power.denominator * a->denominator).simplify();
        }
        return power;
    }
    if (kind == Z3_OP_DIV && e.arg(1).is_numeral()) {
        auto a = fraction(e.arg(0)), b = fraction(e.arg(1));
        if (!a || !b || (b->numerator == 0).simplify().is_true()) return {};
        auto n = (a->numerator * b->denominator).simplify();
        auto d = (a->denominator * b->numerator).simplify();
        if (d.is_numeral() && Z3_get_numeral_string(c, d)[0] == '-') {
            n = (-n).simplify(); d = (-d).simplify();
        }
        return Fraction{n, d};
    }
    if (kind != Z3_OP_ADD && kind != Z3_OP_SUB && kind != Z3_OP_MUL)
        return {};
    auto a = fraction(e.arg(0));
    if (!a) return {};
    for (unsigned i = 1; i < e.num_args(); ++i) {
        auto b = fraction(e.arg(i));
        if (!b) return {};
        auto n = kind == Z3_OP_MUL ? a->numerator * b->numerator
            : kind == Z3_OP_SUB ? a->numerator * b->denominator - b->numerator * a->denominator
            : a->numerator * b->denominator + b->numerator * a->denominator;
        a = Fraction{n.simplify(), (a->denominator * b->denominator).simplify()};
    }
    return a;
}

struct Scope {
    z3::context& c;
    z3::expr_vector variables, definitions;
    explicit Scope(z3::context& ctx) : c(ctx), variables(ctx), definitions(ctx) {}
    z3::expr close(const z3::expr& body) {
        return variables.empty() ? body : z3::exists(variables, z3::mk_and(definitions) && body);
    }
    z3::expr rewrite(const z3::expr& e) {
        if (e.is_quantifier()) {
            if (Z3_is_lambda(c, e)) return e;
            unsigned count = Z3_get_quantifier_num_bound(c, e);
            z3::expr_vector binders(c);
            std::vector<Z3_ast> substitutions(count);
            for (unsigned i = 0; i < count; ++i) {
                z3::sort s(c, Z3_get_quantifier_bound_sort(c, e, i));
                z3::expr v(c, Z3_mk_fresh_const(c, "rational_bound", s));
                binders.push_back(v);
                substitutions[count - i - 1] = v;
            }
            z3::expr opened(c, Z3_substitute_vars(c, e.body(), count, substitutions.data()));
            Scope inner(c);
            auto body = inner.close(inner.rewrite(opened));
            return e.is_forall() ? z3::forall(binders, body) : z3::exists(binders, body);
        }
        if (!e.is_app() || e.num_args() == 0) return e;
        z3::expr_vector args(c);
        for (unsigned i = 0; i < e.num_args(); ++i) args.push_back(rewrite(e.arg(i)));
        auto result = e.decl()(args);
        auto kind = e.decl().decl_kind();
        if ((kind == Z3_OP_IDIV || kind == Z3_OP_MOD) && args[1].is_numeral()
            && !(args[1] == 0).simplify().is_true()) {
            z3::expr q(c, Z3_mk_fresh_const(c, "rational_quotient", c.int_sort()));
            auto d = args[1];
            auto magnitude = Z3_get_numeral_string(c, d)[0] == '-' ? (-d).simplify() : d;
            auto remainder = args[0] - d*q;
            variables.push_back(q);
            definitions.push_back(remainder >= 0 && remainder < magnitude);
            return kind == Z3_OP_MOD ? remainder : q;
        }
        if (args.size() == 2 && (kind == Z3_OP_EQ || kind == Z3_OP_LE ||
            kind == Z3_OP_LT || kind == Z3_OP_GE || kind == Z3_OP_GT) &&
            (args[0].is_real() || args[1].is_real())) {
            auto a = fraction(args[0]), b = fraction(args[1]);
            if (a && b) {
                auto left = (a->numerator * b->denominator).simplify();
                auto right = (b->numerator * a->denominator).simplify();
                switch (kind) {
                    case Z3_OP_EQ: return left == right;
                    case Z3_OP_LE: return left <= right;
                    case Z3_OP_LT: return left < right;
                    case Z3_OP_GE: return left >= right;
                    default: return left > right;
                }
            }
        }
        return result;
    }
};
}
z3::expr integerize_rational_query(const z3::expr& query, bool bind_top_level) {
    Scope scope(query.ctx());
    auto body = scope.rewrite(query);
    z3::params parameters(query.ctx());
    parameters.set("som", true);
    return (bind_top_level ? scope.close(body)
                          : z3::mk_and(scope.definitions) && body).simplify(parameters);
}
}
