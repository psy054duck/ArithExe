#include "Expr.h"

namespace ari_exe {
    Expression::Expression(const z3::expr& expr)
        : conditions(expr.ctx()), expressions(expr.ctx()), definitions(expr.ctx()) {
        auto [_conditions, _expressions] = expr2piecewise(expr);
        conditions = _conditions;
        expressions = _expressions;
        for (unsigned i = 0; i < expressions.size(); ++i) {
            definitions.push_back(expr.ctx().bool_val(true));
        }
    }

    Expression&
    Expression::operator=(const Expression& other) {
        if (this != &other) {
            conditions = other.conditions;
            expressions = other.expressions;
            definitions = other.definitions;
        }
        return *this;
    }

    Expression Expression::operator+(const Expression& other) const {
        return bin_operator(*this, other, [](const z3::expr& lhs, const z3::expr& rhs) { return lhs + rhs; });
    }

    Expression Expression::operator-(const Expression& other) const {
        return bin_operator(*this, other, [](const z3::expr& lhs, const z3::expr& rhs) { return lhs - rhs; });
    }

    Expression Expression::operator*(const Expression& other) const {
        return bin_operator(*this, other, [](const z3::expr& lhs, const z3::expr& rhs) { return lhs * rhs; });
    }

    Expression Expression::operator/(const Expression& other) const {
        return bin_operator(*this, other, [](const z3::expr& lhs, const z3::expr& rhs) { return (lhs / rhs).simplify(); });
    }

    Expression Expression::operator%(const Expression& other) const {
        return bin_operator(*this, other, [](const z3::expr& lhs, const z3::expr& rhs) { return lhs % rhs; });
    }

    Expression Expression::operator==(const Expression& other) const {
        return bin_operator(*this, other, [](const z3::expr& lhs, const z3::expr& rhs) { return lhs == rhs; });
    }

    Expression Expression::operator!=(const Expression& other) const {
        return bin_operator(*this, other, [](const z3::expr& lhs, const z3::expr& rhs) { return lhs != rhs; });
    }

    Expression Expression::operator<(const Expression& other) const {
        return bin_operator(*this, other, [](const z3::expr& lhs, const z3::expr& rhs) { return lhs < rhs; });
    }

    Expression Expression::operator<=(const Expression& other) const {
        return bin_operator(*this, other, [](const z3::expr& lhs, const z3::expr& rhs) { return lhs <= rhs; });
    }

    Expression Expression::operator>(const Expression& other) const {
        return bin_operator(*this, other, [](const z3::expr& lhs, const z3::expr& rhs) { return lhs > rhs; });
    }

    Expression Expression::operator>=(const Expression& other) const {
        return bin_operator(*this, other, [](const z3::expr& lhs, const z3::expr& rhs) { return lhs >= rhs; });
    }

    Expression Expression::operator&&(const Expression& other) const {
        return bin_operator(*this, other, [](const z3::expr& lhs, const z3::expr& rhs) { return lhs && rhs; });
    }

    Expression Expression::operator||(const Expression& other) const {
        return bin_operator(*this, other, [](const z3::expr& lhs, const z3::expr& rhs) { return lhs || rhs; });
    }

    Expression Expression::operator^(const Expression& other) const {
        return bin_operator(*this, other, [](const z3::expr& lhs, const z3::expr& rhs) { return lhs ^ rhs; });
    }

    Expression Expression::operator!() const {
        z3::expr_vector new_expressions(ctx());
        for (const auto& expr : expressions) {
            if (expr.is_bool()) {
                new_expressions.push_back(!expr);
            } else {
                new_expressions.push_back(expr == 0);
            }
        }
        return Expression(conditions, new_expressions, definitions);
    }

    Expression Expression::operator-() const {
        z3::expr_vector new_expressions(ctx());
        for (const auto& expr : expressions) {
            new_expressions.push_back(-expr);
        }
        return Expression(conditions, new_expressions, definitions);
    }

    Expression Expression::operator+() const {
        return *this;
    }

    z3::expr Expression::as_expr() const {
        auto res = piecewise2ite(conditions, expressions);
        return res;
    }

    z3::expr Expression::defined() const {
        if (definitions.empty()) return ctx().bool_val(true);
        return piecewise2ite(conditions, definitions);
    }

    Expression Expression::bin_operator(const Expression& lhs, const Expression& rhs, 
                                                const std::function<z3::expr(const z3::expr&, const z3::expr&)>& op) {
        std::vector<int> sizes = {static_cast<int>(lhs.conditions.size()), static_cast<int>(rhs.conditions.size())};
        z3::expr_vector new_conditions(lhs.ctx());
        z3::expr_vector new_expressions(lhs.ctx());
        z3::expr_vector new_definitions(lhs.ctx());
        for (const auto& indices : cartesian_product(sizes)) {
            auto lhs_cond = lhs.conditions[indices[0]];
            auto rhs_cond = rhs.conditions[indices[1]];
            auto cur_cond = lhs_cond && rhs_cond;
            if (!is_feasible(cur_cond)) continue;
            new_conditions.push_back(cur_cond);
            new_expressions.push_back(op(lhs.expressions[indices[0]], rhs.expressions[indices[1]]));
            new_definitions.push_back(lhs.definitions[indices[0]] &&
                                      rhs.definitions[indices[1]]);
        }
        return Expression(new_conditions, new_expressions, new_definitions);
    }

    void
    Expression::push_front(z3::expr condition, const Expression& expr) {
        z3::expr_vector new_conditions(ctx());
        z3::expr_vector new_expressions(ctx());
        z3::expr_vector new_definitions(ctx());
        new_conditions.push_back(condition);
        new_expressions.push_back(expr.as_expr());
        new_definitions.push_back(expr.defined());
        z3::expr acc_cond = condition;
        for (int i = 0; i < conditions.size(); ++i) {
            new_conditions.push_back(simplify(!acc_cond && conditions[i]));
            new_expressions.push_back(expressions[i]);
            new_definitions.push_back(definitions[i]);
            acc_cond = acc_cond || conditions[i];
        }
        conditions = new_conditions;
        expressions = new_expressions;
        definitions = new_definitions;
    }

    Expression
    Expression::subs(const z3::expr_vector& src, const std::vector<Expression>& dst) const {
        assert(src.size() == dst.size() && "Source and destination vectors must have the same size");
        z3::expr_vector new_conditions(ctx());
        z3::expr_vector new_expressions(ctx());
        z3::expr_vector new_definitions(ctx());
        auto z3_dst = z3::expr_vector(ctx());
        for (const auto& expr : dst) {
            z3_dst.push_back(expr.as_expr());
        }
        for (auto cond : conditions) {
            new_conditions.push_back(cond.substitute(src, z3_dst));
        }
        for (auto expr : expressions) {
            new_expressions.push_back(expr.substitute(src, z3_dst).simplify());
        }
        for (auto definition : definitions) {
            new_definitions.push_back(
                definition.substitute(src, z3_dst).simplify());
        }

        return Expression(new_conditions, new_expressions, new_definitions);
        // for (int i = 0; i < conditions.size(); ++i) {
        //     z3::expr new_condition = conditions[i];
        //     z3::expr new_expression = expressions[i];
        //     for (int j = 0; j < src.size(); ++j) {
        //         new_condition = new_condition.substitute(src[j], dst[j].as_expr());
        //         new_expression = new_expression.substitute(src[j], dst[j].as_expr());
        //     }
        // }
    }
}
