// l14_utility - the A13 math utility surface: the mathutils.h family and
// the scalar helpers of definitions.h/utility.h.
// Reference: third_party/SVT-AV1/Source/Lib/Codec/mathutils.h.
// LANDED so far (MU1): linsolve. The reference keeps these static INLINE
// in a header so every includer carries its own copy; per-TU copies
// already landed: l9 noise_model.cpp (AN16, linsolve only), l17 ransac.cpp
// (A3W, linsolve + the least-squares trio) - those stay; this is the
// shared landing.
#pragma once

#include <cstdint>

namespace l14_utility {

// linsolve - mathutils.h:22-65 verbatim. Solves Ax = b, where x and b are
// column vectors of size nx1 and A is nxn. Returns 1 on success, 0 when a
// pivot magnitude falls below tiny_near_zero (singular).
std::int32_t linsolve(std::int32_t n, double* A, std::int32_t stride, double* b, double* x);

// leastSquares* - mathutils.h:90-108 verbatim: the three-step least-squares
// process for n-dim x minimizing |Ax - b|^2; mat/y are the A'A / A'b
// accumulators.
void leastSquaresInit(double* mat, double* y, int n);
void leastSquaresAccumulate(double* mat, double* y, const double* a, double b, int n);
std::int32_t leastSquaresSolve(double* mat, double* y, double* x, int n);

// multiplyMat - mathutils.h:111-125 verbatim: res = m1 * m2, m1 is
// m1_rows x inner_dim, m2 is inner_dim x m2_cols.
void multiplyMat(const double* m1, const double* m2, double* res, std::int32_t m1_rows, std::int32_t inner_dim,
                 std::int32_t m2_cols);

}  // namespace l14_utility
