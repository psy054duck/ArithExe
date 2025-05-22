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
#include "AnalysisManager.h"
#include "LinearAlgebra.h"

namespace ari_exe {
    using Matrix = ari_exe::Algebra::LinearAlgebra::Matrix<z3::expr>;

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
             * @brief simplify the formula using to_cnf
             */
            z3::expr simplify(const z3::expr& fml);

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
             * @brief Assuming vars are piecewise linear expression of others,
             *        this function computes this expressions
             * @param constraints quantifer-free constraints
             * @param vars variables to be solved
             * @return piecewise linear expressions of vars entailed by the constraints
             */
            z3::expr_vector solve_vars(const z3::expr& constraints, const z3::expr_vector& vars);

            /**
             * @brief transform literal into normal form of e >= 0. Only applicable to LIA.
             */
            z3::expr normalize(z3::expr literal);

            /**
             * @brief solve linear equations for given variables
             * @return solution to variables in order
             */
            z3::expr_vector solve_linear_equations(z3::expr_vector equations, z3::expr_vector vars);

        private:
            /**
             * @brief Assumming vars are linear expressions of other variables
             *        entailed by the constraints, which is a conjunction of linear
             *        inequalities, this function computes these linear expressions
             */
            z3::expr_vector solve_vars_linear(z3::expr constraints, z3::expr_vector var);

            /**
             * @brief given a linear expression, return the coefficients of the variable
             */
            z3::expr get_coeff(z3::expr expr, z3::expr var);

            /**
             * @brief preprocess the constraints to eliminate if-then-else terms
             * @return the processed constraints and temporary variables introduced
             *         due to the elimination
             */
            std::pair<z3::expr, z3::expr_vector> preprocess_constraints(z3::expr constraints);

            /**
             * @brief build Ax = b form of the linear equations
             * @param equations the linear equations
             * @param vars the variables x in the equations
             * @return the matrix A and vector b
             */
            std::pair<Matrix, Matrix> build_matrix_form(z3::expr_vector equations, z3::expr_vector vars);
    };
}

#endif