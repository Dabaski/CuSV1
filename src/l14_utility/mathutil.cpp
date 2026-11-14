#include <cmath>
#include <cstring>
#include <mathutil.h>
#include <utility.h>

namespace l14_utility {

// linsolve - mathutils.h:22-65 verbatim.
std::int32_t linsolve(std::int32_t n, double* A, std::int32_t stride, double* b, double* x) {
    const double tiny_near_zero = 1.0E-16;
    std::int32_t i, j, k;
    double c;
    // Forward elimination
    for (k = 0; k < n - 1; k++) {
        // Bring the largest magnitude to the diagonal position
        for (i = n - 1; i > k; i--) {
            if (std::fabs(A[(i - 1) * stride + k]) < std::fabs(A[i * stride + k])) {
                for (j = 0; j < n; j++) {
                    c                       = A[i * stride + j];
                    A[i * stride + j]       = A[(i - 1) * stride + j];
                    A[(i - 1) * stride + j] = c;
                }
                c        = b[i];
                b[i]     = b[i - 1];
                b[i - 1] = c;
            }
        }
        for (i = k; i < n - 1; i++) {
            if (std::fabs(A[k * stride + k]) < tiny_near_zero) {
                return 0;
            }
            c = A[(i + 1) * stride + k] / A[k * stride + k];
            for (j = 0; j < n; j++) {
                A[(i + 1) * stride + j] -= c * A[k * stride + j];
            }
            b[i + 1] -= c * b[k];
        }
    }
    // Backward substitution
    for (i = n - 1; i >= 0; i--) {
        if (std::fabs(A[i * stride + i]) < tiny_near_zero) {
            return 0;
        }
        c = 0;
        for (j = i + 1; j <= n - 1; j++) {
            c += A[i * stride + j] * x[j];
        }
        x[i] = (b[i] - c) / A[i * stride + i];
    }

    return 1;
}

// leastSquares* - mathutils.h:90-108 verbatim.
void leastSquaresInit(double* mat, double* y, int n) {
    std::memset(mat, 0, n * n * sizeof(double));
    std::memset(y, 0, n * sizeof(double));
}

void leastSquaresAccumulate(double* mat, double* y, const double* a, double b, int n) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            mat[i * n + j] += a[i] * a[j];
        }
    }
    for (int i = 0; i < n; i++) {
        y[i] += a[i] * b;
    }
}

std::int32_t leastSquaresSolve(double* mat, double* y, double* x, int n) {
    return linsolve(n, mat, n, y, x);
}

// multiplyMat - mathutils.h:111-125 verbatim.
void multiplyMat(const double* m1, const double* m2, double* res, const std::int32_t m1_rows,
                 const std::int32_t inner_dim, const std::int32_t m2_cols) {
    double sum;

    std::int32_t row, col, inner;
    for (row = 0; row < m1_rows; ++row) {
        for (col = 0; col < m2_cols; ++col) {
            sum = 0;
            for (inner = 0; inner < inner_dim; ++inner) {
                sum += m1[row * inner_dim + inner] * m2[inner * m2_cols + col];
            }
            *(res++) = sum;
        }
    }
}

// definitions.h / utility.h helpers (MU2).
void memPutLe16(void* vmem, std::int32_t val) {
    std::uint8_t* mem = static_cast<std::uint8_t*>(vmem);

    mem[0] = static_cast<std::uint8_t>((val >> 0) & 0xff);
    mem[1] = static_cast<std::uint8_t>((val >> 8) & 0xff);
}

void memPutLe24(void* vmem, std::int32_t val) {
    std::uint8_t* mem = static_cast<std::uint8_t*>(vmem);

    mem[0] = static_cast<std::uint8_t>((val >> 0) & 0xff);
    mem[1] = static_cast<std::uint8_t>((val >> 8) & 0xff);
    mem[2] = static_cast<std::uint8_t>((val >> 16) & 0xff);
}

void memPutLe32(void* vmem, std::int32_t val) {
    std::uint8_t* mem = static_cast<std::uint8_t*>(vmem);

    mem[0] = static_cast<std::uint8_t>((val >> 0) & 0xff);
    mem[1] = static_cast<std::uint8_t>((val >> 8) & 0xff);
    mem[2] = static_cast<std::uint8_t>((val >> 16) & 0xff);
    mem[3] = static_cast<std::uint8_t>((val >> 24) & 0xff);
}

int svtNumbits(unsigned int x) {
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return (int)((x * 0x01010101u) >> 24);
}

int svtLog2fSafe(std::uint32_t x) {
    return getMsb(x | 1);
}

const std::uint8_t miniGopOffset[5] = {1, 3, 7, 15, 31};

}  // namespace l14_utility
