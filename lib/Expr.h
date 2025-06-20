#ifndef EXPR_H
#define EXPR_H

#include <z3++.h>
#include "logics.h"
#include "common.h"
#include "AnalysisManager.h"

#include <functional>

namespace ari_exe {
    /**
     * @brief Expression class to represent a (conditional) symbolic expression
     *       It contains a vector of conditions and a vector of expressions.
     *       The size of conditions and expressions must be the same.
     *       The disjunction of all conditions must be true.
     */
    class Expression {
        public:
            Expression(): conditions(AnalysisManager::get_ctx()), expressions(AnalysisManager::get_ctx()) {}
            Expression(z3::context& ctx) : conditions(ctx), expressions(ctx) {}
            Expression(const z3::expr_vector& conds, const z3::expr_vector& exprs)
                : conditions(conds), expressions(exprs) {}
            Expression(const Expression& other)
                : conditions(other.conditions), expressions(other.expressions) {}

            Expression(const z3::expr& expr);

            Expression& operator=(const Expression& other);

            Expression operator+(const Expression& other) const;
            Expression operator-(const Expression& other) const;
            Expression operator*(const Expression& other) const;
            Expression operator/(const Expression& other) const;
            Expression operator%(const Expression& other) const;
            Expression operator==(const Expression& other) const;
            Expression operator!=(const Expression& other) const;
            Expression operator<(const Expression& other) const;
            Expression operator<=(const Expression& other) const;
            Expression operator>(const Expression& other) const;
            Expression operator>=(const Expression& other) const;
            Expression operator&&(const Expression& other) const;
            Expression operator^(const Expression& other) const;
            Expression operator||(const Expression& other) const;
            Expression operator!() const;
            Expression operator-() const; // unary minus
            Expression operator+() const; // unary plus
            z3::expr as_expr() const;
            z3::expr_vector get_conditions() const { return conditions; }
            z3::expr_vector get_expressions() const { return expressions; }
            z3::context& ctx() const { return conditions.ctx(); }

            void push_front(z3::expr condition, z3::expr expr);

            Expression subs(const z3::expr_vector& src, const std::vector<Expression>& dst) const;

        private:
            static Expression bin_operator(const Expression& lhs, const Expression& rhs, 
                                           const std::function<z3::expr(const z3::expr&, const z3::expr&)>& op);
            
            z3::expr_vector conditions;
            z3::expr_vector expressions;
    };
} // namespace ari_exe

#endif