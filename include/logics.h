//---------------------------logics.h---------------------------
//
// This file contains a bunch of logic related classes.
// CNF conversion related parts are borrowed from Z3 guide
// (https://microsoft.github.io/z3guide/programming/Example%20Programs/Formula%20Simplification).
//
//--------------------------------------------------------------

#ifndef LOGICS_H
#define LOGICS_H

#include "z3++.h"
#include <set>
#include <string>

namespace ari_exe {

    /**
     * @brief A comparator for z3 variables, not designed for general use.
     * @details This is used to compare two z3::expr objects in a set.
     *          The comparison is done by comparing the string representation of the expressions.
     */
    struct expr_compare {
        bool operator()(const z3::expr& a, const z3::expr& b) const {
            return a.to_string() < b.to_string();
        }
    };

    class Logic {
        public:
            /**
             * @brief check if this expression is atomic in the theory
             */
            virtual bool is_atom(const z3::expr& expr);

            /**
             * @brief check if this expression is a literal in the theory
             */
            virtual bool is_literal(const z3::expr& expr);

            /**
             * @brief convert the formula into CNF
             */
            z3::expr_vector to_cnf(const z3::expr& fml);

            /**
             * @brief convert the formula into DNF
             */
            z3::expr_vector to_dnf(const z3::expr& fml);

            /**
             * @brief check if the formula is >=
             */
            bool is_ge(const z3::expr& e);

            /**
             * @brief check if the formula is <=
             */
            bool is_le(const z3::expr& e);

            /**
             * @brief check if the formula is <
             */
            bool is_lt(const z3::expr& e);

            /**
             * @brief check if the formula is >
             */
            bool is_gt(const z3::expr& e);

            /**
             * @brief check if the formula is ==
             */
            bool is_eq(const z3::expr& e);

            /**
             * @brief get all atoms in the formula
             */
            z3::expr_vector atoms(const z3::expr& fml);

            /**
             * @brief get all variables in the formula
             */
            std::set<z3::expr, expr_compare> collect_vars(const z3::expr& e);

        private:
            void atoms_rec(const z3::expr& t, std::set<std::string>& visited, z3::expr_vector& atms);
            

            /**
             * @brief Given a model and an atom, return the literal satisfied by the model
             */
            z3::expr atom2literal(const z3::model& m, const z3::expr& a);

            /**
             * @brief Returns a clause (disjunction of negated unsat core) for the given solvers and atoms
             */
            z3::expr implicant(const z3::expr_vector& atoms, z3::solver& s, z3::solver& snot);

            /**
             * @brief helper function of to_dnf
             * @param clauses the CNF formula
             * @return list of list of expr, each inner list is a conjunction of literals
             */
            std::vector<z3::expr_vector> dnf_rec(const z3::expr_vector& clauses);

            /**
             * @brief recursively collect all variables in the formula
             */
            void collect_vars_rec(const z3::expr& e, std::set<z3::expr, expr_compare>& vars);
    };

    class LinearLogic: public Logic {
        public:
            /**
             * @brief Assuming var is a piecewise linear expression of other variables,
             *        this function computes this expressions
             * @param constraints quantifer-free constraints
             * @param var the variable to be solved
             * @return the piecewise linear expression of var entailed by the constraints
             */
            std::optional<z3::expr> solve_var(const z3::expr& constraints, z3::expr& var);

            /**
             * @brief transform literal into normal form of e >= 0. Only applicable to LIA.
             */
            z3::expr normalize(z3::expr literal);

        private:
            /**
             * @brief Assumming var is a linear expression of other variables
             *        entailed by the constraints, which is a conjunction of linear
             *        inequalities, this function computes the linear expression
             */
            std::optional<z3::expr> solve_var_linear(z3::expr& constraints, z3::expr& var);

            /**
             * @brief given a linear expression, return the coefficients of the variable
             */
            z3::expr get_coeff(z3::expr expr, z3::expr var);
    };
}

#endif