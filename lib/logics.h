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

            z3::expr_vector atoms(const z3::expr& fml);

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

            // /**
            //  * @brief Convert clause to vector of literals
            //  */
            // z3::expr_vector clause2list(const z3::expr& clause);

            /**
             * @brief helper function of to_dnf
             * @param clauses the CNF formula
             * @return list of list of expr, each inner list is a conjunction of literals
             */
            std::vector<z3::expr_vector> dnf_rec(const z3::expr_vector& clauses);
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