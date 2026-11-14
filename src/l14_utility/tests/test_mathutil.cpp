#include <doctest.h>
#include <mathutil.h>

TEST_CASE("linsolve solves a 1x1 system") {
    double a[1] = {4.0};
    double b[1] = {10.0};
    double x[1] = {0.0};
    CHECK(l14_utility::linsolve(1, a, 1, b, x) == 1);
    CHECK(x[0] == doctest::Approx(2.5));
}

// The pivot fixture: row 0 has a zero leading entry, so the
// largest-magnitude walk (mathutils.h:29-39) must swap rows before the
// elimination divides by A[k*stride+k].
TEST_CASE("linsolve pivots the 2x2 with a zero first pivot") {
    double a[4] = {0.0, 1.0, 1.0, 0.0};
    double b[2] = {2.0, 6.0};
    double x[2] = {0.0, 0.0};
    CHECK(l14_utility::linsolve(2, a, 2, b, x) == 1);
    CHECK(x[0] == doctest::Approx(6.0));
    CHECK(x[1] == doctest::Approx(2.0));
}

TEST_CASE("linsolve returns 0 on a rank-1 system") {
    // A = [[1,2],[2,4]], b consistent, but A[1][1] eliminates to 0 and the
    // backward substitution rejects it (mathutils.h:53-56).
    double a[4] = {1.0, 2.0, 2.0, 4.0};
    double b[2] = {1.0, 2.0};
    double x[2] = {0.0, 0.0};
    CHECK(l14_utility::linsolve(2, a, 2, b, x) == 0);
}

TEST_CASE("linsolve solves a 3x3 system against its known solution") {
    // A*x = b for x = (1,-2,3): b = (-3,5,2) computed independently.
    double a[9] = {2.0, 1.0, -1.0, -3.0, -1.0, 2.0, -2.0, 1.0, 2.0};
    double b[3] = {-3.0, 5.0, 2.0};
    double x[3] = {0.0, 0.0, 0.0};
    CHECK(l14_utility::linsolve(3, a, 3, b, x) == 1);
    CHECK(x[0] == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(x[1] == doctest::Approx(-2.0).epsilon(1e-12));
    CHECK(x[2] == doctest::Approx(3.0).epsilon(1e-12));
}

// least_squares_init/accumulate/solve - mathutils.h:90-108. The exact-fit
// case: three equations whose normal system is [[2,1],[1,2]] (3,0) with
// solution x = (2,-1) - the accumulate step must build A'A and A'b so the
// solve returns the exact x_true.
TEST_CASE("leastSquares trio recovers an exact fit") {
    double mat[4], y[2], x[2];
    l14_utility::leastSquaresInit(mat, y, 2);
    const double a0[2] = {1.0, 0.0};
    const double a1[2] = {0.0, 1.0};
    const double a2[2] = {1.0, 1.0};
    l14_utility::leastSquaresAccumulate(mat, y, a0, 2.0, 2);
    l14_utility::leastSquaresAccumulate(mat, y, a1, -1.0, 2);
    l14_utility::leastSquaresAccumulate(mat, y, a2, 1.0, 2);
    CHECK(mat[0] == doctest::Approx(2.0));
    CHECK(mat[1] == doctest::Approx(1.0));
    CHECK(mat[2] == doctest::Approx(1.0));
    CHECK(mat[3] == doctest::Approx(2.0));
    CHECK(y[0] == doctest::Approx(3.0));
    CHECK(y[1] == doctest::Approx(0.0));
    CHECK(l14_utility::leastSquaresSolve(mat, y, x, 2) == 1);
    CHECK(x[0] == doctest::Approx(2.0).epsilon(1e-12));
    CHECK(x[1] == doctest::Approx(-1.0).epsilon(1e-12));
}

// The overdetermined inconsistent case, normal equations solved by hand:
// mat = [[2,1],[1,2]], y = (5,6), x = (1/3)*(4,7).
TEST_CASE("leastSquares trio returns the least-squares solution") {
    double mat[4], y[2], x[2];
    l14_utility::leastSquaresInit(mat, y, 2);
    const double a0[2] = {1.0, 0.0};
    const double a1[2] = {0.0, 1.0};
    const double a2[2] = {1.0, 1.0};
    l14_utility::leastSquaresAccumulate(mat, y, a0, 1.0, 2);
    l14_utility::leastSquaresAccumulate(mat, y, a1, 2.0, 2);
    l14_utility::leastSquaresAccumulate(mat, y, a2, 4.0, 2);
    CHECK(l14_utility::leastSquaresSolve(mat, y, x, 2) == 1);
    CHECK(x[0] == doctest::Approx(4.0 / 3.0).epsilon(1e-12));
    CHECK(x[1] == doctest::Approx(7.0 / 3.0).epsilon(1e-12));
}

TEST_CASE("leastSquaresSolve returns 0 when the normal system is singular") {
    double mat[4] = {1.0, 2.0, 2.0, 4.0};
    double y[2] = {1.0, 2.0};
    double x[2] = {0.0, 0.0};
    CHECK(l14_utility::leastSquaresSolve(mat, y, x, 2) == 0);
}

// multiply_mat - mathutils.h:111-125. [[1,2,3],[4,5,6]] * [[7,8],[9,10],
// [11,12]] = [[58,64],[139,154]], computed independently.
TEST_CASE("multiplyMat computes a 2x3 by 3x2 product") {
    const double m1[6] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    const double m2[6] = {7.0, 8.0, 9.0, 10.0, 11.0, 12.0};
    double res[4] = {0.0, 0.0, 0.0, 0.0};
    l14_utility::multiplyMat(m1, m2, res, 2, 3, 2);
    CHECK(res[0] == doctest::Approx(58.0));
    CHECK(res[1] == doctest::Approx(64.0));
    CHECK(res[2] == doctest::Approx(139.0));
    CHECK(res[3] == doctest::Approx(154.0));
}

// The inner-dim/m2-cols stride discriminator: m2 diagonal 3x3, m1 2x3, so
// res[r][c] = m1[r][c] only if the m2 row stride is m2_cols (3), and the
// result layout is m1_rows x m2_cols (2x3).
TEST_CASE("multiplyMat strides discriminate inner dim from m2 cols") {
    const double m1[6] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    const double m2[9] = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    double res[6];
    l14_utility::multiplyMat(m1, m2, res, 2, 3, 3);
    CHECK(res[0] == doctest::Approx(1.0));
    CHECK(res[1] == doctest::Approx(2.0));
    CHECK(res[2] == doctest::Approx(3.0));
    CHECK(res[3] == doctest::Approx(4.0));
    CHECK(res[4] == doctest::Approx(5.0));
    CHECK(res[5] == doctest::Approx(6.0));
}
