#include <gtest/gtest.h>
#include "LinearAlgebra.h"

// TEST(LinearAlgebra, MatrixAddition) {
//     ari_exe::Algebra::LinearAlgebra::Matrix<int> A(2, 2);
//     ari_exe::Algebra::LinearAlgebra::Matrix<int> B(2, 2);
//     A(0, 0) = 1; A(0, 1) = 2; A(1, 0) = 3; A(1, 1) = 4;
//     B(0, 0) = 5; B(0, 1) = 6; B(1, 0) = 7; B(1, 1) = 8;
//     auto C = A + B;
//     EXPECT_EQ(C(0, 0), 6);
//     EXPECT_EQ(C(0, 1), 8);
//     EXPECT_EQ(C(1, 0), 10);
//     EXPECT_EQ(C(1, 1), 12);
// }
// 
// TEST(LinearAlgebra, MatrixSubtraction) {
//     ari_exe::Algebra::LinearAlgebra::Matrix<int> A(2, 2);
//     ari_exe::Algebra::LinearAlgebra::Matrix<int> B(2, 2);
//     A(0, 0) = 5; A(0, 1) = 6; A(1, 0) = 7; A(1, 1) = 8;
//     B(0, 0) = 1; B(0, 1) = 2; B(1, 0) = 3; B(1, 1) = 4;
//     auto C = A - B;
//     EXPECT_EQ(C(0, 0), 4);
//     EXPECT_EQ(C(0, 1), 4);
//     EXPECT_EQ(C(1, 0), 4);
//     EXPECT_EQ(C(1, 1), 4);
// }
// 
// TEST(LinearAlgebra, MatrixMultiplication) {
//     ari_exe::Algebra::LinearAlgebra::Matrix<int> A(2, 3);
//     ari_exe::Algebra::LinearAlgebra::Matrix<int> B(3, 2);
//     A(0, 0) = 1; A(0, 1) = 2; A(0, 2) = 3;
//     A(1, 0) = 4; A(1, 1) = 5; A(1, 2) = 6;
//     B(0, 0) = 7; B(0, 1) = 8;
//     B(1, 0) = 9; B(1, 1) = 10;
//     B(2, 0) = 11; B(2, 1) = 12;
//     auto C = A * B;
//     EXPECT_EQ(C(0, 0), 58);
//     EXPECT_EQ(C(0, 1), 64);
//     EXPECT_EQ(C(1, 0), 139);
//     EXPECT_EQ(C(1, 1), 154);
// }
// 
// TEST(LinearAlgebra, MatrixScalarMultiplication) {
//     ari_exe::Algebra::LinearAlgebra::Matrix<int> A(2, 2);
//     A(0, 0) = 1; A(0, 1) = 2; A(1, 0) = 3; A(1, 1) = 4;
//     auto C = A * 2;
//     EXPECT_EQ(C(0, 0), 2);
//     EXPECT_EQ(C(0, 1), 4);
//     EXPECT_EQ(C(1, 0), 6);
//     EXPECT_EQ(C(1, 1), 8);
// }
// 
// TEST(LinearAlgebra, MatrixTranspose) {
//     ari_exe::Algebra::LinearAlgebra::Matrix<int> A(2, 3);
//     A(0, 0) = 1; A(0, 1) = 2; A(0, 2) = 3;
//     A(1, 0) = 4; A(1, 1) = 5; A(1, 2) = 6;
//     auto C = A.transpose();
//     EXPECT_EQ(C(0, 0), 1);
//     EXPECT_EQ(C(0, 1), 4);
//     EXPECT_EQ(C(1, 0), 2);
//     EXPECT_EQ(C(1, 1), 5);
//     EXPECT_EQ(C(2, 0), 3);
//     EXPECT_EQ(C(2, 1), 6);
// }
// 
// TEST(LinearAlgebra, MatrixInverse) {
//     ari_exe::Algebra::LinearAlgebra::Matrix<double> A(2, 2);
//     A(0, 0) = 4; A(0, 1) = 3;
//     A(1, 0) = 3; A(1, 1) = 2;
//     auto C = A.inverse();
//     EXPECT_EQ(C(0, 0), -2);
//     EXPECT_EQ(C(0, 1), 3);
//     EXPECT_EQ(C(1, 0), 3);
//     EXPECT_EQ(C(1, 1), -4);
// }
// 
// TEST(LinearAlgebra, MatrixInverseZ3) {
//     ari_exe::Algebra::LinearAlgebra::Matrix<z3::expr> A(2, 2);
//     auto& ctx = A(0, 0).ctx();
//     A(0, 0) = ctx.int_val(1); A(0, 1) = ctx.int_val(0);
//     A(1, 0) = ctx.int_val(0); A(1, 1) = ctx.int_val(1);
//     auto C = A.inverse();
//     EXPECT_EQ(C(0, 0).simplify().to_string(), "1");
//     EXPECT_EQ(C(0, 1).simplify().to_string(), "0");
//     EXPECT_EQ(C(1, 0).simplify().to_string(), "0");
//     EXPECT_EQ(C(1, 1).simplify().to_string(), "1");
// }
// 
// TEST(LinearAlgebra, SolveSimple) {
//     ari_exe::Algebra::LinearAlgebra::Matrix<double> A(2, 2);
//     ari_exe::Algebra::LinearAlgebra::Matrix<double> b(2, 1);
//     A(0, 0) = 1; A(0, 1) = 1;
//     A(1, 0) = 0; A(1, 1) = 2;
//     b(0, 0) = 10;
//     b(1, 0) = 8;
//     auto C = A.solve(A, b);
//     EXPECT_EQ(C(0, 0), 6);
//     EXPECT_EQ(C(1, 0), 4);
// }

TEST(LinearAlgebra, SolveNumericZ3) {
    ari_exe::Algebra::LinearAlgebra::Matrix<z3::expr> A(2, 2);
    auto& ctx = A(0, 0).ctx();
    A(0, 0) = ctx.int_val(1); A(0, 1) = ctx.int_val(2);
    A(1, 0) = ctx.int_val(0); A(1, 1) = ctx.int_val(1);
    ari_exe::Algebra::LinearAlgebra::Matrix<z3::expr> b(2, 1);
    b(0, 0) = ctx.int_val(18);
    b(1, 0) = ctx.int_val(8);
    auto C = ari_exe::Algebra::LinearAlgebra::Matrix<z3::expr>::solve(A, b);
    EXPECT_EQ(C(0, 0).simplify().to_string(), "2");
    EXPECT_EQ(C(1, 0).simplify().to_string(), "8");
}

// TEST(LinearAlgebra, MatrixLUDecomposition) {
//     ari_exe::Algebra::LinearAlgebra::Matrix<double> A(3, 3);
//     A(0, 0) = 1; A(0, 1) = 1; A(0, 2) = 1;
//     A(1, 0) = 2; A(1, 1) = 4; A(1, 2) = 8;
//     A(2, 0) = 3; A(2, 1) = 9; A(2, 2) = 27;
//     auto [L, U] = A.lu_decompose();
//     EXPECT_EQ(L(0, 0), 1);
//     EXPECT_EQ(L(0, 1), 0);
//     EXPECT_EQ(L(0, 2), 0);
//     EXPECT_EQ(L(1, 0), 2);
//     EXPECT_EQ(L(1, 1), 1);
//     EXPECT_EQ(L(1, 2), 0);
//     EXPECT_EQ(L(2, 0), 3);
//     EXPECT_EQ(L(2, 1), 3);
//     EXPECT_EQ(L(2, 2), 1);
//     EXPECT_EQ(L*U, A);
// 
// }