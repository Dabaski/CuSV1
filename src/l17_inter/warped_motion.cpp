#include "warped_motion.h"

#include <cassert>
#include <cstdlib>
#include <cstring>

namespace l17_inter {

// block_size_wide / block_size_high, common_utils.c:286-:291 (verbatim).
const std::uint8_t blockSizeWide[kBlockSizesAll] = {4,  4,  8,  8,   8,   16, 16, 16, 32, 32, 32,
                                                    64, 64, 64, 128, 128, 4,  16, 8,  32, 16, 64};

const std::uint8_t blockSizeHigh[kBlockSizesAll] = {4,  8,  4,   8,  16,  8,  16, 32, 16, 32, 64,
                                                    32, 64, 128, 64, 128, 16, 4,  32, 8,  64, 16};

// get_msb, portable #else body (definitions.h:628-:644): floor(log2(n)),
// valid for n != 0. Per-module local (l7/l10/l13 precedent).
inline std::int32_t getMsb(std::uint32_t n) {
    std::int32_t d = 31;
    while (d >= 0 && ((n & (1u << d)) == 0)) --d;
    return d;
}

// svt_aom_warped_filter, warped_motion.c:57-:216 (the active ==6 branch,
// 193x8). Mechanically extracted into warped_filter_table.inc.
const std::int16_t warpedFilter[kWarpedPixelPrecShifts * 3 + 1][8] = {
#include "warped_filter_table.inc"
};

// div_lut, warped_motion.c:295-:313. Mechanically extracted from the
// reference file (see tools note in the slice report); transcription caveat
// stated at the declaration.
const std::uint16_t divLut[kDivLutNum + 1] = {
    16384, 16320, 16257, 16194, 16132, 16070, 16009, 15948, 15888, 15828, 15768, 15709, 15650, 15592, 15534, 15477,
    15420, 15364, 15308, 15252, 15197, 15142, 15087, 15033, 14980, 14926, 14873, 14821, 14769, 14717, 14665, 14614,
    14564, 14513, 14463, 14413, 14364, 14315, 14266, 14218, 14170, 14122, 14075, 14028, 13981, 13935, 13888, 13843,
    13797, 13752, 13707, 13662, 13618, 13574, 13530, 13487, 13443, 13400, 13358, 13315, 13273, 13231, 13190, 13148,
    13107, 13066, 13026, 12985, 12945, 12906, 12866, 12827, 12788, 12749, 12710, 12672, 12633, 12596, 12558, 12520,
    12483, 12446, 12409, 12373, 12336, 12300, 12264, 12228, 12193, 12157, 12122, 12087, 12053, 12018, 11984, 11950,
    11916, 11882, 11848, 11815, 11782, 11749, 11716, 11683, 11651, 11619, 11586, 11555, 11523, 11491, 11460, 11429,
    11398, 11367, 11336, 11305, 11275, 11245, 11215, 11185, 11155, 11125, 11096, 11067, 11038, 11009, 10980, 10951,
    10923, 10894, 10866, 10838, 10810, 10782, 10755, 10727, 10700, 10673, 10645, 10618, 10592, 10565, 10538, 10512,
    10486, 10460, 10434, 10408, 10382, 10356, 10331, 10305, 10280, 10255, 10230, 10205, 10180, 10156, 10131, 10107,
    10082, 10058, 10034, 10010, 9986,  9963,  9939,  9916,  9892,  9869,  9846,  9823,  9800,  9777,  9754,  9732,
    9709,  9687,  9664,  9642,  9620,  9598,  9576,  9554,  9533,  9511,  9489,  9468,  9447,  9425,  9404,  9383,
    9362,  9341,  9321,  9300,  9279,  9259,  9239,  9218,  9198,  9178,  9158,  9138,  9118,  9098,  9079,  9059,
    9039,  9020,  9001,  8981,  8962,  8943,  8924,  8905,  8886,  8867,  8849,  8830,  8812,  8793,  8775,  8756,
    8738,  8720,  8702,  8684,  8666,  8648,  8630,  8613,  8595,  8577,  8560,  8542,  8525,  8508,  8490,  8473,
    8456,  8439,  8422,  8405,  8389,  8372,  8355,  8339,  8322,  8306,  8289,  8273,  8257,  8240,  8224,  8208,
    8192,
};

// resolve_divisor_64, warped_motion.c:317-:332.
std::int16_t resolveDivisor64(std::uint64_t d, std::int16_t* shift) {
    std::int64_t f;
    *shift = (std::int16_t)((d >> 32) ? getMsb((std::uint32_t)(d >> 32)) + 32 : getMsb((std::uint32_t)d));
    // e is obtained from d after resetting the most significant 1 bit.
    const std::int64_t e = (std::int64_t)(d - ((std::uint64_t)1 << *shift));
    // Get the most significant DIV_LUT_BITS (8) bits of e into f
    if (*shift > kDivLutBits) {
        f = (e + (((std::int64_t)1 << (*shift - kDivLutBits)) >> 1)) >> (*shift - kDivLutBits);
    } else {
        f = e << (kDivLutBits - *shift);
    }
    assert(f <= kDivLutNum);
    *shift = (std::int16_t)(*shift + kDivLutPrecBits);
    // Use f as lookup into the precomputed table of multipliers
    return divLut[f];
}

// resolve_divisor_32, warped_motion.c:334-:349.
std::int16_t resolveDivisor32(std::uint32_t d, std::int16_t* shift) {
    std::int32_t f;
    *shift = (std::int16_t)getMsb(d);
    // e is obtained from d after resetting the most significant 1 bit.
    const std::int32_t e = (std::int32_t)(d - (1u << *shift));
    // Get the most significant DIV_LUT_BITS (8) bits of e into f
    if (*shift > kDivLutBits) {
        f = (e + (1 << (*shift - kDivLutBits - 1))) >> (*shift - kDivLutBits);
    } else {
        f = e << (kDivLutBits - *shift);
    }
    assert(f <= kDivLutNum);
    *shift = (std::int16_t)(*shift + kDivLutPrecBits);
    // Use f as lookup into the precomputed table of multipliers
    return divLut[f];
}

// clamp64, definitions.h:691-:693.
inline std::int64_t clamp64(std::int64_t value, std::int64_t low, std::int64_t high) {
    return value < low ? low : (value > high ? high : value);
}

// ROUND_POWER_OF_TWO_SIGNED_64, definitions.h:467-:468.
inline std::int64_t roundPowerOfTwoSigned64(std::int64_t value, int n) {
    return value < 0 ? -(((-value) + (((std::int64_t)1 << n) >> 1)) >> n)
                     : ((value + (((std::int64_t)1 << n) >> 1)) >> n);
}

// get_mult_shift_ndiag, warped_motion.c:276-:281 (the active #else branch).
std::int32_t getMultShiftNdiag(std::int64_t pX, std::int16_t iDet, int shift) {
    const std::int64_t v = pX * (std::int64_t)iDet;
    return (std::int32_t)clamp64(roundPowerOfTwoSigned64(v, shift),
                                 -(std::int64_t)kWarpedModelNondiagaffineClamp + 1,
                                 (std::int64_t)kWarpedModelNondiagaffineClamp - 1);
}

// get_mult_shift_diag, warped_motion.c:283-:288 (the active #else branch).
std::int32_t getMultShiftDiag(std::int64_t pX, std::int16_t iDet, int shift) {
    const std::int64_t v = pX * (std::int64_t)iDet;
    return (std::int32_t)clamp64(roundPowerOfTwoSigned64(v, shift),
                                 ((std::int64_t)1 << kWarpedModelPrecBits) - kWarpedModelNondiagaffineClamp + 1,
                                 ((std::int64_t)1 << kWarpedModelPrecBits) + kWarpedModelNondiagaffineClamp - 1);
}

// is_affine_valid, warped_motion.c:351-:354.
bool isAffineValid(const WarpedMotionParams* wm) {
    const std::int32_t* mat = wm->wmmat;
    return mat[2] > 0;
}

// is_affine_shear_allowed, warped_motion.c:356-:363.
bool isAffineShearAllowed(std::int16_t alpha, std::int16_t beta, std::int16_t gamma, std::int16_t delta) {
    if ((4 * abs(alpha) + 7 * abs(beta) >= (1 << kWarpedModelPrecBits)) ||
        (4 * abs(gamma) + 4 * abs(delta) >= (1 << kWarpedModelPrecBits))) {
        return false;
    } else {
        return true;
    }
}

// ROUND_POWER_OF_TWO_SIGNED, definitions.h:460-:461.
inline std::int32_t roundPowerOfTwoSigned(std::int32_t value, int n) {
    return value < 0 ? -(((-value) + ((1 << n) >> 1)) >> n) : ((value + ((1 << n) >> 1)) >> n);
}

// ROUND_POWER_OF_TWO, definitions.h:457 (the reference applies this to
// negative subpel accumulators too, arithmetic-shift semantics preserved).
inline std::int32_t roundPowerOfTwo(std::int32_t value, int n) {
    return (value + ((1 << n) >> 1)) >> n;
}

// clamp, definitions.h:687-:689.
inline std::int32_t clampInt(std::int32_t value, std::int32_t low, std::int32_t high) {
    return value < low ? low : (value > high ? high : value);
}

// svt_get_shear_params, warped_motion.c:907-:933. Returns 1 on success or 0
// on an invalid affine set.
bool getShearParams(WarpedMotionParams* wm) {
    const std::int32_t* mat = wm->wmmat;
    if (!isAffineValid(wm)) {
        return false;
    }
    wm->alpha = (std::int16_t)clampInt(mat[2] - (1 << kWarpedModelPrecBits), INT16_MIN, INT16_MAX);
    wm->beta = (std::int16_t)clampInt(mat[3], INT16_MIN, INT16_MAX);
    std::int16_t shift;
    const std::int16_t y =
        (std::int16_t)(resolveDivisor32((std::uint32_t)abs(mat[2]), &shift) * (mat[2] < 0 ? -1 : 1));
    std::int64_t v = ((std::int64_t)mat[4] * (1 << kWarpedModelPrecBits)) * y;
    wm->gamma = (std::int16_t)clampInt((std::int32_t)roundPowerOfTwoSigned64(v, shift), INT16_MIN, INT16_MAX);
    v = ((std::int64_t)mat[3] * mat[4]) * y;
    wm->delta = (std::int16_t)clampInt(
        mat[5] - (std::int32_t)roundPowerOfTwoSigned64(v, shift) - (1 << kWarpedModelPrecBits), INT16_MIN, INT16_MAX);

    wm->alpha = (std::int16_t)(roundPowerOfTwoSigned(wm->alpha, kWarpParamReduceBits) * (1 << kWarpParamReduceBits));
    wm->beta = (std::int16_t)(roundPowerOfTwoSigned(wm->beta, kWarpParamReduceBits) * (1 << kWarpParamReduceBits));
    wm->gamma = (std::int16_t)(roundPowerOfTwoSigned(wm->gamma, kWarpParamReduceBits) * (1 << kWarpParamReduceBits));
    wm->delta = (std::int16_t)(roundPowerOfTwoSigned(wm->delta, kWarpParamReduceBits) * (1 << kWarpParamReduceBits));

    if (!isAffineShearAllowed(wm->alpha, wm->beta, wm->gamma, wm->delta)) {
        return false;
    }

    return true;
}

// ---------- find_affine_int / find_projection ----------

// warped_motion.c:22-:49 - the least-squares constants and macros.
constexpr int kLsMvMax = 256; // LS_MV_MAX, max mv in 1/8-pel
constexpr int kLsStep = 8;    // LS_STEP
// LS_MAT_RANGE_BITS = ((MAX_SB_SIZE_LOG2 + 4) * 2 + LEAST_SQUARES_SAMPLES_MAX_BITS) = 25.
constexpr int kLsMatRangeBits = ((7 + 4) * 2 + 3); // definitions.h:338/:447
constexpr int kLsMatDownBits = 2;                  // :37
constexpr int kLsMatBits = (kLsMatRangeBits - kLsMatDownBits);             // :40
constexpr std::int32_t kLsMatMin = -(1 << (kLsMatBits - 1));               // :41
constexpr std::int32_t kLsMatMax = ((1 << (kLsMatBits - 1)) - 1);          // :42

inline std::int32_t lsSquare(std::int32_t a) {
    return (a * a * 4 + a * 4 * kLsStep + kLsStep * kLsStep * 2) >> (2 + kLsMatDownBits);
}

inline std::int32_t lsProduct1(std::int32_t a, std::int32_t b) {
    return (a * b * 4 + ((a) + (b)) * 2 * kLsStep + kLsStep * kLsStep) >> (2 + kLsMatDownBits);
}

inline std::int32_t lsProduct2(std::int32_t a, std::int32_t b) {
    return (a * b * 4 + ((a) + (b)) * 2 * kLsStep + kLsStep * kLsStep * 2) >> (2 + kLsMatDownBits);
}

// AOMMAX/AOMMIN (definitions.h:451-:454 shapes).
inline std::int32_t aomMax(std::int32_t a, std::int32_t b) {
    return a > b ? a : b;
}
inline std::int32_t aomMin(std::int32_t a, std::int32_t b) {
    return a < b ? a : b;
}

// find_affine_int, warped_motion.c:365-:471.
bool findAffineInt(int np, const std::int32_t* pts1, const std::int32_t* pts2, BlockSize bsize, Mv mv,
                   WarpedMotionParams* wm, int miRow, int miCol) {
    std::int32_t A[2][2] = {{0, 0}, {0, 0}};
    std::int32_t bx[2] = {0, 0};
    std::int32_t by[2] = {0, 0};

    const int bw = blockSizeWide[bsize];
    const int bh = blockSizeHigh[bsize];
    const int rsuy = (aomMax(bh, kMiSize) / 2 - 1);
    const int rsux = (aomMax(bw, kMiSize) / 2 - 1);
    const int suy = rsuy * 8;
    const int sux = rsux * 8;
    const int duy = suy + mv.y;
    const int dux = sux + mv.x;
    const int isuy = (miRow * kMiSize + rsuy);
    const int isux = (miCol * kMiSize + rsux);

    // The loop computes: A = P'P, bx = P'q, by = P'r (wm:383-:404).
    for (int i = 0; i < np; i++) {
        const int dx = pts2[i * 2] - dux;
        const int dy = pts2[i * 2 + 1] - duy;
        const int sx = pts1[i * 2] - sux;
        const int sy = pts1[i * 2 + 1] - suy;
        if (abs(sx - dx) < kLsMvMax && abs(sy - dy) < kLsMvMax) {
            A[0][0] += lsSquare(sx);
            A[0][1] += lsProduct1(sx, sy);
            A[1][1] += lsSquare(sy);
            bx[0] += lsProduct2(sx, dx);
            bx[1] += lsProduct1(sy, dx);
            by[0] += lsProduct1(sx, dy);
            by[1] += lsProduct2(sy, dy);
        }
    }

    assert(A[0][0] >= kLsMatMin && A[0][0] <= kLsMatMax);
    assert(A[0][1] >= kLsMatMin && A[0][1] <= kLsMatMax);
    assert(A[1][1] >= kLsMatMin && A[1][1] <= kLsMatMax);
    assert(bx[0] >= kLsMatMin && bx[0] <= kLsMatMax);
    assert(bx[1] >= kLsMatMin && bx[1] <= kLsMatMax);
    assert(by[0] >= kLsMatMin && by[0] <= kLsMatMax);
    assert(by[1] >= kLsMatMin && by[1] <= kLsMatMax);

    std::int64_t det;
    std::int16_t iDet, shift;

    // Compute Determinant of A
    det = (std::int64_t)A[0][0] * A[1][1] - (std::int64_t)A[0][1] * A[0][1];
    if (det == 0) {
        return true;
    }
    iDet = (std::int16_t)(resolveDivisor64((std::uint64_t)llabs(det), &shift) * (det < 0 ? -1 : 1));
    shift = (std::int16_t)(shift - kWarpedModelPrecBits);
    if (shift < 0) {
        iDet = (std::int16_t)(iDet << (-shift));
        shift = 0;
    }

    std::int64_t pX[2], pY[2];

    // These divided by the det, are the least squares solutions
    pX[0] = (std::int64_t)A[1][1] * bx[0] - (std::int64_t)A[0][1] * bx[1];
    pX[1] = -(std::int64_t)A[0][1] * bx[0] + (std::int64_t)A[0][0] * bx[1];
    pY[0] = (std::int64_t)A[1][1] * by[0] - (std::int64_t)A[0][1] * by[1];
    pY[1] = -(std::int64_t)A[0][1] * by[0] + (std::int64_t)A[0][0] * by[1];

    wm->wmmat[2] = getMultShiftDiag(pX[0], iDet, shift);
    wm->wmmat[3] = getMultShiftNdiag(pX[1], iDet, shift);
    wm->wmmat[4] = getMultShiftNdiag(pY[0], iDet, shift);
    wm->wmmat[5] = getMultShiftDiag(pY[1], iDet, shift);

    std::int32_t vx = mv.x * (1 << (kWarpedModelPrecBits - 3)) -
        (isux * (wm->wmmat[2] - (1 << kWarpedModelPrecBits)) + isuy * wm->wmmat[3]);
    std::int32_t vy = mv.y * (1 << (kWarpedModelPrecBits - 3)) -
        (isux * wm->wmmat[4] + isuy * (wm->wmmat[5] - (1 << kWarpedModelPrecBits)));
    wm->wmmat[0] = clampInt(vx, -kWarpedModelTransClamp, kWarpedModelTransClamp - 1);
    wm->wmmat[1] = clampInt(vy, -kWarpedModelTransClamp, kWarpedModelTransClamp - 1);

    return false;
}

// svt_find_projection, warped_motion.c:473-:485.
bool findProjection(int np, const std::int32_t* pts1, const std::int32_t* pts2, BlockSize bsize, Mv mv,
                    WarpedMotionParams* wm, int miRow, int miCol) {
    if (findAffineInt(np, pts1, pts2, bsize, mv, wm, miRow, miCol)) {
        return true;
    }

    // check compatibility with the fast warp filter
    if (!getShearParams(wm)) {
        return true;
    }

    return false;
}

// ---------- warp ----------

// svt_av1_warp_affine_c, warped_motion.c:571-:684.
void warpAffineC(const std::int32_t* mat, const std::uint8_t* ref, int width, int height, int stride,
                 std::uint8_t* pred, int pCol, int pRow, int pWidth, int pHeight, int pStride, int subsamplingX,
                 int subsamplingY, inter::ConvolveParams* convParams, std::int16_t alpha, std::int16_t beta,
                 std::int16_t gamma, std::int16_t delta) {
    std::int32_t tmp[15 * 8];
    const int bd = 8;
    const int reduce_bits_horiz = convParams->round0;
    const int reduce_bits_vert =
        convParams->isCompound ? convParams->round1 : 2 * kFilterBits - reduce_bits_horiz;
    const int max_bits_horiz = bd + kFilterBits + 1 - reduce_bits_horiz;
    const int offset_bits_horiz = bd + kFilterBits - 1;
    const int offset_bits_vert = bd + 2 * kFilterBits - reduce_bits_horiz;
    const int round_bits = 2 * kFilterBits - convParams->round0 - convParams->round1;
    const int offset_bits = bd + 2 * kFilterBits - convParams->round0;
    (void)max_bits_horiz;
    assert(!convParams->isCompound || convParams->dst != nullptr);
    assert(!convParams->doAverage || convParams->isCompound);

    for (int i = pRow; i < pRow + pHeight; i += 8) {
        for (int j = pCol; j < pCol + pWidth; j += 8) {
            // Calculate the center of this 8x8 block,
            // project to luma coordinates (if in a subsampled chroma plane),
            // apply the affine transformation,
            // then convert back to the original coordinates (if necessary)
            const std::int32_t src_x = (j + 4) << subsamplingX;
            const std::int32_t src_y = (i + 4) << subsamplingY;
            const std::int32_t dst_x = mat[2] * src_x + mat[3] * src_y + mat[0];
            const std::int32_t dst_y = mat[4] * src_x + mat[5] * src_y + mat[1];
            const std::int32_t x4 = dst_x >> subsamplingX;
            const std::int32_t y4 = dst_y >> subsamplingY;

            std::int32_t ix4 = x4 >> kWarpedModelPrecBits;
            std::int32_t sx4 = x4 & ((1 << kWarpedModelPrecBits) - 1);
            std::int32_t iy4 = y4 >> kWarpedModelPrecBits;
            std::int32_t sy4 = y4 & ((1 << kWarpedModelPrecBits) - 1);

            sx4 += alpha * (-4) + beta * (-4);
            sy4 += gamma * (-4) + delta * (-4);

            sx4 &= ~((1 << kWarpParamReduceBits) - 1);
            sy4 &= ~((1 << kWarpParamReduceBits) - 1);

            // Horizontal filter
            for (int k = -7; k < 8; ++k) {
                // Clamp to top/bottom edge of the frame
                const int iy = clampInt(iy4 + k, 0, height - 1);

                int sx = sx4 + beta * (k + 4);

                for (int l = -4; l < 4; ++l) {
                    int ix = ix4 + l - 3;
                    // At this point, sx = sx4 + alpha * l + beta * k
                    const int offs = (int)roundPowerOfTwo(sx, kWarpeddiffPrecBits) + kWarpedPixelPrecShifts;
                    assert(offs >= 0 && offs <= kWarpedPixelPrecShifts * 3);
                    const std::int16_t* coeffs = warpedFilter[offs];

                    std::int32_t sum = 1 << offset_bits_horiz;
                    for (int m = 0; m < 8; ++m) {
                        // Clamp to left/right edge of the frame
                        const int sample_x = clampInt(ix + m, 0, width - 1);

                        sum += ref[iy * stride + sample_x] * coeffs[m];
                    }
                    sum = (std::int32_t)roundPowerOfTwo(sum, reduce_bits_horiz);
                    assert(0 <= sum && sum < (1 << max_bits_horiz));
                    tmp[(k + 7) * 8 + (l + 4)] = sum;
                    sx += alpha;
                }
            }

            // Vertical filter
            for (int k = -4; k < aomMin(4, pRow + pHeight - i - 4); ++k) {
                int sy = sy4 + delta * (k + 4);
                for (int l = -4; l < aomMin(4, pCol + pWidth - j - 4); ++l) {
                    // At this point, sy = sy4 + gamma * l + delta * k
                    const int offs = (int)roundPowerOfTwo(sy, kWarpeddiffPrecBits) + kWarpedPixelPrecShifts;
                    assert(offs >= 0 && offs <= kWarpedPixelPrecShifts * 3);
                    const std::int16_t* coeffs = warpedFilter[offs];

                    std::int32_t sum = 1 << offset_bits_vert;
                    for (int m = 0; m < 8; ++m) {
                        sum += tmp[(k + m + 4) * 8 + (l + 4)] * coeffs[m];
                    }
                    if (convParams->isCompound) {
                        inter::ConvBufType* p =
                            &convParams->dst[(i - pRow + k + 4) * convParams->dstStride + (j - pCol + l + 4)];
                        sum = (std::int32_t)roundPowerOfTwo(sum, reduce_bits_vert);
                        if (convParams->doAverage) {
                            std::uint8_t* dst8 = &pred[(i - pRow + k + 4) * pStride + (j - pCol + l + 4)];
                            std::int32_t tmp32 = *p;
                            if (convParams->useJntCompAvg) {
                                tmp32 = tmp32 * convParams->fwdOffset + sum * convParams->bckOffset;
                                tmp32 = tmp32 >> kDistPrecisionBits;
                            } else {
                                tmp32 += sum;
                                tmp32 = tmp32 >> 1;
                            }
                            tmp32 = tmp32 - (1 << (offset_bits - convParams->round1)) -
                                (1 << (offset_bits - convParams->round1 - 1));
                            *dst8 =
                                (std::uint8_t)clampInt((std::int32_t)roundPowerOfTwo(tmp32, round_bits), 0, 255);
                        } else {
                            *p = (inter::ConvBufType)sum;
                        }
                    } else {
                        std::uint8_t* p = &pred[(i - pRow + k + 4) * pStride + (j - pCol + l + 4)];
                        sum = (std::int32_t)roundPowerOfTwo(sum, reduce_bits_vert);
                        assert(0 <= sum && sum < (1 << (bd + 2)));
                        *p = (std::uint8_t)clampInt((std::int32_t)sum - (1 << (bd - 1)) - (1 << bd), 0, 255);
                    }
                    sy += gamma;
                }
            }
        }
    }
}

// svt_warp_plane, warped_motion.c:686-:717.
void warpPlane(WarpedMotionParams* wm, const std::uint8_t* ref, int width, int height, int stride, std::uint8_t* pred,
               int pCol, int pRow, int pWidth, int pHeight, int pStride, int subsamplingX, int subsamplingY,
               inter::ConvolveParams* convParams) {
    assert(wm->wmtype <= Affine);
    if (wm->wmtype == Rotzoom) {
        wm->wmmat[5] = wm->wmmat[2];
        wm->wmmat[4] = -wm->wmmat[3];
    }
    const std::int32_t* const mat = wm->wmmat;
    const std::int16_t alpha = wm->alpha;
    const std::int16_t beta = wm->beta;
    const std::int16_t gamma = wm->gamma;
    const std::int16_t delta = wm->delta;
    warpAffineC(mat, ref, width, height, stride, pred, pCol, pRow, pWidth, pHeight, pStride, subsamplingX,
                subsamplingY, convParams, alpha, beta, gamma, delta);
}

// svt_aom_select_samples, warped_motion.c:935-:955. Select samples according
// to the motion vector difference.
std::uint8_t selectSamples(Mv mv, std::int32_t* pts, std::int32_t* ptsInref, int len, BlockSize bsize) {
    const int bw = blockSizeWide[bsize];
    const int bh = blockSizeHigh[bsize];
    const int thresh = clampInt(aomMax(bw, bh), 16, 112);
    std::uint8_t ret = 0;

    // Only keep the samples with MV differences within threshold.
    for (int i = 0; i < len; ++i) {
        const int diff = abs(ptsInref[2 * i] - pts[2 * i] - mv.x) + abs(ptsInref[2 * i + 1] - pts[2 * i + 1] - mv.y);
        if (diff > thresh) {
            continue;
        }
        if (ret != i) {
            memcpy(pts + 2 * ret, pts + 2 * i, 2 * sizeof(pts[0]));
            memcpy(ptsInref + 2 * ret, ptsInref + 2 * i, 2 * sizeof(ptsInref[0]));
        }
        ++ret;
    }
    // Keep at least 1 sample.
    return (std::uint8_t)aomMax(ret, 1);
}

} // namespace l17_inter
