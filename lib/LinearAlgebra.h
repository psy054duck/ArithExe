#ifndef LINEARALGEBRA_H
#define LINEARALGEBRA_H

#include <assert.h>
#include <map>
#include <string>
#include "z3++.h"
#include <iostream>
#include "AnalysisManager.h"

namespace ari_exe {
    namespace Algebra {
        namespace LinearAlgebra {
            template<typename data_t>
            class Matrix {
                public:
                    Matrix(int rows, int cols);
                    ~Matrix();
                    Matrix(const Matrix& other);
                    Matrix& operator=(const Matrix& other);
                    Matrix& operator=(Matrix&& other);
                    data_t& operator()(int row, int col);
                    const data_t& operator()(int row, int col) const;
                    Matrix operator-() const;
                    Matrix operator+(const Matrix& other) const;
                    Matrix operator-(const Matrix& other) const;
                    Matrix operator*(const Matrix& other) const;
                    Matrix operator*(const data_t& scalar) const;
                    Matrix transpose() const;
                    Matrix inverse() const;
                    std::string to_string() const;

                    // solve linear equations using Gaussian elimination
                    static Matrix solve(const Matrix& A, const Matrix& b);

                    bool operator==(const Matrix& other) const;

                    // LU decomposition
                    // std::pair<Matrix, Matrix>lu_decompose() const;
                    // static z3::context& z3ctx;

                private:
                    int rows;
                    int cols;
                    // data_t** data;
                    std::vector<std::vector<data_t>> data;

                    data_t addictive_zero() const;
                    data_t multiplicative_one() const;
                    static data_t data_div(const data_t& a, const data_t& b);
                    static bool data_eq(const data_t& a, const data_t& b);
            };

            template<typename data_t>
            Matrix<data_t>::Matrix(int rows, int cols): rows(rows), cols(cols) {
                data.resize(rows);
                for (int i = 0; i < rows; i++) {
                    data[i].resize(cols);
                }
            }

            template<typename data_t>
            Matrix<data_t>::~Matrix() {}

            template<typename data_t>
            Matrix<data_t>::Matrix(const Matrix& other): rows(other.rows), cols(other.cols) {
                data.resize(rows);
                for (int i = 0; i < rows; i++) {
                    data[i].resize(cols);
                }
                for (int i = 0; i < rows; i++) {
                    for (int j = 0; j < cols; j++) {
                        data[i][j] = other.data[i][j].simplify();
                    }
                }
            }

            template<typename data_t>
            Matrix<data_t>& Matrix<data_t>::operator=(const Matrix& other) {
                assert(rows == other.rows && cols == other.cols && "Matrix size mismatch");
                for (int i = 0; i < rows; i++) {
                    for (int j = 0; j < cols; j++) {
                        data[i][j] = other.data[i][j].simplify();
                    }
                }
                return *this;
            }

            template<typename data_t>
            Matrix<data_t>& Matrix<data_t>::operator=(Matrix&& other) {
                assert(rows == other.rows && cols == other.cols && "Matrix size mismatch");
                for (int i = 0; i < rows; i++) {
                    for (int j = 0; j < cols; j++) {
                        data[i][j] = other.data[i][j].simplify();
                    }
                }
                return *this;
            }

            template<typename data_t>
            data_t& Matrix<data_t>::operator()(int row, int col) {
                assert(row >= 0 && row < rows && col >= 0 && col < cols && "Index out of bounds");
                return data[row][col];
            }

            template<typename data_t>
            const data_t& Matrix<data_t>::operator()(int row, int col) const {
                assert(row >= 0 && row < rows && col >= 0 && col < cols && "Index out of bounds");
                return data[row][col];
            }

            template<typename data_t>
            Matrix<data_t> Matrix<data_t>::operator+(const Matrix& other) const {
                assert(rows == other.rows && cols == other.cols && "Matrix size mismatch");
                Matrix result(rows, cols);
                for (int i = 0; i < rows; i++) {
                    for (int j = 0; j < cols; j++) {
                        result(i, j) = (data[i][j] + other.data[i][j]).simplify();
                    }
                }
                return result;
            }

            template<typename data_t>
            Matrix<data_t> Matrix<data_t>::operator-() const {
                Matrix result(rows, cols);
                for (int i = 0; i < rows; i++) {
                    for (int j = 0; j < cols; j++) {
                        result(i, j) = (-data[i][j]).simplify();
                    }
                }
                return result;
            }

            template<typename data_t>
            Matrix<data_t> Matrix<data_t>::operator-(const Matrix& other) const {
                assert(rows == other.rows && cols == other.cols && "Matrix size mismatch");
                return *this + (-other);
            }

            template<typename data_t>
            Matrix<data_t> Matrix<data_t>::operator*(const Matrix& other) const {
                assert(cols == other.rows && "Matrix size mismatch");
                Matrix result(rows, other.cols);
                for (int i = 0; i < rows; i++) {
                    for (int j = 0; j < other.cols; j++) {
                        result(i, j) = addictive_zero();
                        for (int k = 0; k < cols; k++) {
                            result(i, j) = (result(i, j) + data[i][k] * other.data[k][j]).simplify();
                        }
                    }
                }
                return result;
            }

            template<typename data_t>
            Matrix<data_t> Matrix<data_t>::operator*(const data_t& scalar) const {
                Matrix result(rows, cols);
                for (int i = 0; i < rows; i++) {
                    for (int j = 0; j < cols; j++) {
                        result(i, j) = (data[i][j] * scalar).simplify();
                    }
                }
                return result;
            }

            template<typename data_t>
            Matrix<data_t> Matrix<data_t>::transpose() const {
                Matrix result(cols, rows);
                for (int i = 0; i < rows; i++) {
                    for (int j = 0; j < cols; j++) {
                        result(j, i) = data[i][j];
                    }
                }
                return result;
            }

            template<typename data_t>
            Matrix<data_t> Matrix<data_t>::inverse() const {
                assert(rows == cols && "Matrix is not square");
                int n = rows;
                Matrix<data_t> A(*this); // Copy of the original matrix
                Matrix<data_t> I(n, n);  // Identity matrix

                // Initialize identity matrix
                for (int i = 0; i < n; ++i)
                    for (int j = 0; j < n; ++j)
                        I(i, j) = (i == j) ? multiplicative_one(): addictive_zero();

                // Forward elimination
                for (int i = 0; i < n; ++i) {
                    // Find the pivot
                    data_t pivot = A(i, i);
                    assert(pivot != 0 && "Matrix is singular and cannot be inverted");

                    // Normalize the pivot row
                    for (int j = 0; j < n; ++j) {
                        A(i, j) = data_div(A(i, j), pivot);
                        I(i, j) = data_div(I(i, j), pivot);
                    }

                    // Eliminate other rows
                    for (int k = 0; k < n; ++k) {
                        if (k == i) continue;
                        data_t factor = A(k, i);
                        for (int j = 0; j < n; ++j) {
                            A(k, j) = A(k, j) - factor * A(i, j);
                            I(k, j) = I(k, j) - factor * I(i, j);
                        }
                    }
                }
                return I;
            }

            // template<typename data_t>
            // std::pair<Matrix<data_t>, Matrix<data_t>>
            // Matrix<data_t>::lu_decompose() const {
            //     assert(rows == cols && "Matrix is not square");
            //     Matrix L(rows, cols);
            //     Matrix U(rows, cols);
            //     for (int i = 0; i < rows; i++) {
            //         for (int j = 0; j < cols; j++) {
            //             if (i > j) {
            //                 L(i, j) = 0;
            //                 U(i, j) = data[i][j];
            //             } else if (i == j) {
            //                 L(i, j) = 1;
            //                 U(i, j) = data[i][j];
            //             } else {
            //                 L(i, j) = data[i][j];
            //                 U(i, j) = 0;
            //             }
            //         }
            //     }
            //     return {L, U};
            // }

            template<typename data_t>
            bool Matrix<data_t>::operator==(const Matrix& other) const {
                if (rows != other.rows || cols != other.cols) return false;
                for (int i = 0; i < rows; i++) {
                    for (int j = 0; j < cols; j++) {
                        if (!data_eq(data[i][j], other.data[i][j])) return false;
                    }
                }
                return true;
            }

            template<typename data_t>
            data_t Matrix<data_t>::addictive_zero() const {
                return 0;
            }

            template<typename data_t>
            data_t Matrix<data_t>::multiplicative_one() const {
                return 1;
            }

            template<>
            inline z3::expr Matrix<z3::expr>::addictive_zero() const {
                auto& z3ctx = AnalysisManager::get_instance()->get_z3ctx();
                return z3ctx.int_val(0);
            }

            template<>
            inline z3::expr Matrix<z3::expr>::multiplicative_one() const {
                auto& z3ctx = AnalysisManager::get_instance()->get_z3ctx();
                return z3ctx.int_val(1);
            }

            template<>
            inline Matrix<z3::expr>::Matrix(int rows, int cols): rows(rows), cols(cols) {
                data.resize(rows);
                auto& z3ctx = AnalysisManager::get_instance()->get_z3ctx();

                for (int i = 0; i < rows; i++) {
                    data[i].resize(cols, z3ctx.int_val(0));
                }
            }

            template<>
            inline Matrix<z3::expr>::Matrix(const Matrix& other): rows(other.rows), cols(other.cols) {
                data.resize(rows);
                auto& z3ctx = AnalysisManager::get_instance()->get_z3ctx();
                for (int i = 0; i < rows; i++) {
                    data[i].resize(cols, z3ctx.int_val(0));
                }
                for (int i = 0; i < rows; i++) {
                    for (int j = 0; j < cols; j++) {
                        data[i][j] = other.data[i][j];
                    }
                }
            }
            
            template<typename data_t>
            data_t Matrix<data_t>::data_div(const data_t& a, const data_t& b) {
                assert(b != 0 && "Division by zero");
                return a / b;
            }

            template<>
            inline z3::expr Matrix<z3::expr>::data_div(const z3::expr& a, const z3::expr& b) {
                assert((b != a.ctx().int_val(0)).simplify().is_true() && "Division by zero");
                z3::solver solver(a.ctx());
                auto to_check = (a / b * b) != a;
                solver.add(to_check);
                auto res = solver.check();
                assert(res == z3::unsat && "for z3::expr matrix, division only applicable when a is a multiple of b");
                return (a / b).simplify();
            }

            template<typename data_t>
            bool Matrix<data_t>::data_eq(const data_t& a, const data_t& b) {
                return a == b;
            }

            template<>
            inline bool Matrix<z3::expr>::data_eq(const z3::expr& a, const z3::expr& b) {
                return (a == b).simplify().is_true();
            }

            template<typename data_t>
            Matrix<data_t> Matrix<data_t>::solve(const Matrix& A, const Matrix& b) {
                // gaussian elimination
                assert(A.rows == A.cols && A.rows == b.rows && "Matrix size mismatch");
                Matrix result(A.rows, b.cols);
                Matrix augmented(A.rows, A.cols + b.cols);
                for (int i = 0; i < A.rows; i++) {
                    for (int j = 0; j < A.cols; j++) {
                        augmented(i, j) = A(i, j).simplify();
                    }
                    for (int j = 0; j < b.cols; j++) {
                        augmented(i, A.cols + j) = b(i, j).simplify();
                    }
                }
                int smaller_dim = A.rows < A.cols ? A.rows : A.cols;
                for (int i = 0; i < smaller_dim; i++) {
                    // --- Partial Pivoting ---
                    int max_row = i;
                    for (int k = i + 1; k < A.rows; k++) {
                        // Use std::abs for numeric types, or a custom abs for symbolic types if needed
                        auto abs_val = [](const data_t& v) {
                            if constexpr (std::is_arithmetic_v<data_t>)
                                return std::abs(v);
                            else
                                return v; // For symbolic types, you may want to implement your own comparison
                        };
                        if ((abs_val(augmented(k, i)) > abs_val(augmented(max_row, i))).simplify().is_true()) {
                            max_row = k;
                        }
                    }
                    if (max_row != i) {
                        // Swap rows i and max_row
                        for (int j = 0; j < A.cols + b.cols; j++) {
                            std::swap(augmented(i, j), augmented(max_row, j));
                        }
                    }
                    // find pivot
                    data_t pivot = augmented(i, i);
                    if ((pivot == 0).simplify().is_true()) {
                        continue;
                    }
                    // assert(pivot != 0 && "Matrix is singular and cannot be solved");
                    for (int j = 0; j < A.cols + b.cols; j++) {
                        augmented(i, j) = data_div(augmented(i, j), pivot);
                    }
                    for (int k = 0; k < A.rows; k++) {
                        if (k == i) continue;
                        data_t factor = augmented(k, i);
                        for (int j = 0; j < A.cols + b.cols; j++) {
                            augmented(k, j) = (augmented(k, j) - factor * augmented(i, j)).simplify();
                        }
                    }
                }
                for (int i = 0; i < A.rows; i++) {
                    for (int j = 0; j < b.cols; j++) {
                        result(i, j) = augmented(i, A.cols + j).simplify();
                    }
                }
                return result;
            }

            template<typename data_t>
            std::string Matrix<data_t>::to_string() const {
                std::string result;
                for (int i = 0; i < rows; i++) {
                    for (int j = 0; j < cols; j++) {
                        result += std::to_string(data[i][j]) + " ";
                    }
                    result += "\n";
                }
                return result;
            }

            template<>
            inline std::string Matrix<z3::expr>::to_string() const {
                std::string result;
                for (int i = 0; i < rows; i++) {
                    for (int j = 0; j < cols; j++) {
                        result += data[i][j].to_string() + " ";
                    }
                    result += "\n";
                }
                return result;
            }
        }
    }
}

#endif