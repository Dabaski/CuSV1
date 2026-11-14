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
#include <utility.h>

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

// ------------------------------------------------------------------
// definitions.h / utility.h scalar helpers (MU2). Only the NOT-yet-
// landed helpers land here; per-TU copies already landed elsewhere are
// named in the commit and NOT re-ported (roundPowerOfTwo*/clamp*,
// clipPixel*, negativeToZero, divideAndRound, clip3, roundUv,
// ebAbsDiff).
// ------------------------------------------------------------------

// IS_POWER_OF_TWO - definitions.h:470 verbatim body. Quirk pinned: x=0
// returns TRUE ((0 & -1) == 0), as the reference macro does.
template <typename T>
constexpr bool isPowerOfTwo(T x) {
    return (x & (x - 1)) == 0;
}

// RIGHT_SIGNED_SHIFT - definitions.h:547 verbatim: shift right or left
// depending on the sign of n.
template <typename T>
constexpr T rightSignedShift(T value, int n) {
    return n < 0 ? (value << (-n)) : (value >> n);
}

// mem_put_le16/24/32 - definitions.h:562-:584 verbatim: little-endian
// byte stores of the low bits of val.
void memPutLe16(void* vmem, std::int32_t val);
void memPutLe24(void* vmem, std::int32_t val);
void memPutLe32(void* vmem, std::int32_t val);

// svt_numbits - definitions.h:649-:654 verbatim: the portable branchless
// popcount.
int svtNumbits(unsigned int x);

// svt_log2f_safe - definitions.h:591 verbatim: getMsb((x) | 1). getMsb is
// the landed U1 portable body (utility.cpp, definitions.h:628-:644).
int svtLog2fSafe(std::uint32_t x);

// DIVIDE_AND_CEIL - utility.h:97 verbatim (DIVIDE_AND_ROUND :96 landed
// with A6C in l6_pipeline; its l14 twin is NOT re-ported).
constexpr std::int32_t divideAndCeil(std::int32_t x, std::int32_t y) {
    return (x + (y - 1)) / y;
}

// CLIP3EQ - utility.h:102 verbatim shape. FINDING: observationally
// identical to clip3 for pure inputs; the reference carries both because
// the macros double-evaluate their arguments.
constexpr std::int32_t clip3eq(std::int32_t min_val, std::int32_t max_val, std::int32_t a) {
    return (a <= min_val) ? min_val : ((a >= max_val) ? max_val : a);
}

// TWO_D_INDEX - utility.h:150 verbatim.
constexpr std::int32_t twoDIndex(std::int32_t x, std::int32_t y, std::int32_t stride) {
    return (y * stride) + x;
}

// MAX / MIN - utility.h:98-:99 verbatim bodies.
template <typename T>
constexpr T max(T x, T y) {
    return x > y ? x : y;
}
template <typename T>
constexpr T min(T x, T y) {
    return x < y ? x : y;
}

// SQR - utility.h:107 verbatim body.
template <typename T>
constexpr T sqr(T x) {
    return x * x;
}

// MIN_UNSIGNED_VALUE / MIN_SIGNED_VALUE - utility.h:162-:164. The
// reference expression ~0 - ((signed)(~0u >> 1)) evaluates to INT_MIN.
constexpr std::uint32_t kMinUnsignedValue = 0;
constexpr std::int32_t kMinSignedValue = ~0 - static_cast<std::int32_t>(~0u >> 1);

// mini_gop_offset - utility.h:171-:172 verbatim table; the size is
// MAX_HIERARCHICAL_LEVEL(6, Source/API/EbSvtAv1Enc.h:34) -
// MIN_HIERARCHICAL_LEVEL(1, utility.h:171).
constexpr std::uint32_t kMiniGopOffsetCount = 5;
extern const std::uint8_t miniGopOffset[5];

// ------------------------------------------------------------------
// common_utils.c/h remnants (CU1).
// ------------------------------------------------------------------

// TxSize - definitions.h:951-:983 verbatim order. Mirror deviation: l14
// carries its own mirror (the BlockSize mirror precedent in utility.h);
// the entropy::TxSize copies live in l7 and the transform::TxSize in l3,
// neither linkable from l14 (they are added before l6 which links l14).
enum TxSize {
    TX_4X4,
    TX_8X8,
    TX_16X16,
    TX_32X32,
    TX_64X64,
    TX_4X8,
    TX_8X4,
    TX_8X16,
    TX_16X8,
    TX_16X32,
    TX_32X16,
    TX_32X64,
    TX_64X32,
    TX_4X16,
    TX_16X4,
    TX_8X32,
    TX_32X8,
    TX_16X64,
    TX_64X16,
    TX_SIZES_ALL,
    TX_SIZES = TX_4X8,
    TX_SIZES_LARGEST = TX_64X64,
    TX_INVALID = 255
};

// Dep twins, DUPLICATED from the landed l6 A6C surface (l14 cannot link
// l6: l6 links l14) - the SYM1 wb-trio / A3M svt_mv_cost precedent:
// blocksize_to_txsize (common_utils.c:214-:237, indexed by
// BLOCK_SIZES_ALL), tx_depth_to_tx_size (common_utils.c:95-:115, [3]
// [BLOCK_SIZES_ALL]), av1_get_adjusted_tx_size (common_utils.h:100-:113).
extern const TxSize blocksizeToTxsize[BLOCK_SIZES_ALL];
extern const TxSize txDepthToTxSize[3][22];
TxSize av1GetAdjustedTxSize(TxSize tx_size);

// eb_sub_tx_size_map - common_utils.c:73-:93 verbatim (the only
// common_utils.c table not landed elsewhere: grep-verified 0 hits for
// ebSubTxSizeMap before this slice).
extern const TxSize ebSubTxSizeMap[TX_SIZES_ALL];

// av1_get_tx_size - common_utils.h:152-:163 verbatim: plane 0 reads
// tx_depth_to_tx_size, plane > 0 folds the 4:2:0 plane bsize through
// blocksize_to_txsize + av1_get_adjusted_tx_size.
TxSize av1GetTxSize(std::int32_t bsize, int tx_depth, int plane);

}  // namespace l14_utility
