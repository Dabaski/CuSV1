#include <cstdlib>
#include <cstring>

#include "cdef.h"

namespace l10_filters {

namespace {

// 1:1 port of get_msb (definitions.h:628-643, the portable #else fallback):
// floor(log2(n)), n must be != 0.
std::int32_t getMsb(std::uint32_t n) {
    std::int32_t log = 0;
    std::uint32_t value = n;
    for (std::int32_t i = 4; i >= 0; --i) {
        const std::int32_t shift = (1 << i);
        const std::uint32_t x = value >> shift;
        if (x != 0) {
            value = x;
            log += shift;
        }
    }
    return log;
}

// CDEF geometry constants (cdef.h:30-37, the source of the halo/border math):
//   CDEF_VBORDER 3, CDEF_HBORDER 8, CDEF_HALO 2, CDEF_BSTRIDE 144 (144 = 128
//   + 2 * 8 aligned to 8).
constexpr int kCdefVBorder = 3;
constexpr int kCdefHBorder = 8;
constexpr int kCdefHalo = 2;
constexpr int kMiSize64x64 = 16;
} // namespace

// 1:1 port of sign (cdef.c:16-18).
std::int32_t cdefSign(std::int32_t i) {
    return i < 0 ? -1 : 1;
}

// 1:1 port of constrain (cdef.c:20-27).
std::int32_t cdefConstrain(std::int32_t diff, std::int32_t threshold, std::int32_t damping) {
    if (!threshold) {
        return 0;
    }
    const std::int32_t shift = damping - getMsb(static_cast<std::uint32_t>(threshold)) > 0
                                   ? damping - getMsb(static_cast<std::uint32_t>(threshold))
                                   : 0;
    const std::int32_t bound =
        threshold - (std::abs(diff) >> shift) > 0 ? threshold - (std::abs(diff) >> shift) : 0;
    return cdefSign(diff) * (std::abs(diff) < bound ? std::abs(diff) : bound);
}

// 1:1 port of adjust_strength (cdef.c:66-70).
std::int32_t cdefAdjustStrength(std::int32_t strength, std::int32_t var) {
    const std::int32_t v = var >> 6;
    const std::int32_t i = v ? (getMsb(static_cast<std::uint32_t>(v)) < 12 ? getMsb(static_cast<std::uint32_t>(v)) : 12)
                             : 0;
    return var ? (strength * (4 + i) + 8) >> 4 : 0;
}

// 1:1 port of svt_aom_cdef_find_dir_c (cdef.c:88-150).
std::uint8_t cdefFindDir(const std::uint16_t* img, int stride, std::int32_t* var, std::int32_t coeffShift) {
    std::int32_t cost[8] = {0};
    std::int32_t partial[8][15] = {{0}};
    std::int32_t bestCost = 0;
    std::uint8_t bestDir = 0;
    // Instead of dividing by n between 2 and 8, multiply by 3*5*7*8/n = 840/n
    // (the div_table, cdef.c:94-97); the output is 840x larger, which is fine
    // for finding the max.
    static const std::int32_t divTable[9] = {0, 840, 420, 280, 210, 168, 140, 120, 105};
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            // Subtract 128 to reduce the maximum range of the squared partial
            // sums (cdef.c:102-104).
            const std::int32_t x = (img[i * stride + j] >> coeffShift) - 128;
            partial[0][i + j] += x;
            partial[1][i + j / 2] += x;
            partial[2][i] += x;
            partial[3][3 + i - j / 2] += x;
            partial[4][7 + i - j] += x;
            partial[5][3 - i / 2 + j] += x;
            partial[6][j] += x;
            partial[7][i / 2 + j] += x;
        }
    }
    for (int i = 0; i < 8; ++i) {
        cost[2] += partial[2][i] * partial[2][i];
        cost[6] += partial[6][i] * partial[6][i];
    }
    cost[2] *= divTable[8];
    cost[6] *= divTable[8];
    for (int i = 0; i < 7; ++i) {
        cost[0] += (partial[0][i] * partial[0][i] + partial[0][14 - i] * partial[0][14 - i]) * divTable[i + 1];
        cost[4] += (partial[4][i] * partial[4][i] + partial[4][14 - i] * partial[4][14 - i]) * divTable[i + 1];
    }
    cost[0] += partial[0][7] * partial[0][7] * divTable[8];
    cost[4] += partial[4][7] * partial[4][7] * divTable[8];
    for (int i = 1; i < 8; i += 2) {
        for (int j = 0; j < 4 + 1; ++j) {
            cost[i] += partial[i][3 + j] * partial[i][3 + j];
        }
        cost[i] *= divTable[8];
        for (int j = 0; j < 4 - 1; ++j) {
            cost[i] += (partial[i][j] * partial[i][j] + partial[i][10 - j] * partial[i][10 - j]) * divTable[2 * j + 2];
        }
    }
    for (int i = 0; i < 8; ++i) {
        if (cost[i] > bestCost) {
            bestCost = cost[i];
            bestDir = static_cast<std::uint8_t>(i);
        }
    }
    // Difference between the optimal variance and the variance along the
    // orthogonal direction; the sum(x^2) terms cancel (cdef.c:143-148).
    *var = bestCost - cost[(bestDir + 4) & 7];
    *var >>= 10;
    return bestDir;
}

namespace {

// clamp (definitions.h:687-689).
std::int32_t clampInt(std::int32_t value, std::int32_t low, std::int32_t high) {
    return value < low ? low : (value > high ? high : value);
}

} // namespace

// 1:1 port of svt_cdef_filter_block_c (cdef.c:193-254).
void cdefFilterBlock(std::uint8_t* dst8, std::uint16_t* dst16, std::int32_t dstride, const std::uint16_t* in,
                     std::int32_t priStrength, std::int32_t secStrength, std::int32_t dir, std::int32_t priDamping,
                     std::int32_t secDamping, CdefBlockSize bsize, std::int32_t coeffShift,
                     std::uint8_t subsamplingFactor) {
    const std::int32_t s = kCdefBstride;
    const std::int32_t* priTaps = kCdefPriTaps[(priStrength >> coeffShift) & 1];
    const std::int32_t* secTaps = kCdefSecTaps[(priStrength >> coeffShift) & 1];
    const std::int32_t rows = 4 << static_cast<int>(
        static_cast<bool>(bsize == CdefBlockSize::BLOCK_8X8) || static_cast<bool>(bsize == CdefBlockSize::BLOCK_4X8));
    const std::int32_t cols = 4 << static_cast<int>(
        static_cast<bool>(bsize == CdefBlockSize::BLOCK_8X8) || static_cast<bool>(bsize == CdefBlockSize::BLOCK_8X4));
    for (std::int32_t i = 0; i < rows; i += subsamplingFactor) {
        for (std::int32_t j = 0; j < cols; ++j) {
            std::int16_t sum = 0;
            const std::int16_t x = static_cast<std::int16_t>(in[i * s + j]);
            std::int32_t max = x;
            std::int32_t min = x;
            for (int k = 0; k < 2; ++k) {
                const std::int16_t p0 = static_cast<std::int16_t>(in[i * s + j + kCdefDirectionsPadded[dir + 2][k]]);
                const std::int16_t p1 = static_cast<std::int16_t>(in[i * s + j - kCdefDirectionsPadded[dir + 2][k]]);
                sum = static_cast<std::int16_t>(
                    sum + static_cast<std::int16_t>(priTaps[k] * cdefConstrain(p0 - x, priStrength, priDamping)));
                sum = static_cast<std::int16_t>(
                    sum + static_cast<std::int16_t>(priTaps[k] * cdefConstrain(p1 - x, priStrength, priDamping)));
                if (p0 != static_cast<std::int16_t>(kCdefVeryLarge)) {
                    max = p0 > max ? p0 : max;
                }
                if (p1 != static_cast<std::int16_t>(kCdefVeryLarge)) {
                    max = p1 > max ? p1 : max;
                }
                min = p0 < min ? p0 : min;
                min = p1 < min ? p1 : min;
                // directions[dir+2] and directions[dir-2] through the padded
                // table: rows dir+4 and dir (cdef.c:221-224).
                const std::int16_t s0 = static_cast<std::int16_t>(in[i * s + j + kCdefDirectionsPadded[dir + 4][k]]);
                const std::int16_t s1 = static_cast<std::int16_t>(in[i * s + j - kCdefDirectionsPadded[dir + 4][k]]);
                const std::int16_t s2 = static_cast<std::int16_t>(in[i * s + j + kCdefDirectionsPadded[dir][k]]);
                const std::int16_t s3 = static_cast<std::int16_t>(in[i * s + j - kCdefDirectionsPadded[dir][k]]);
                if (s0 != static_cast<std::int16_t>(kCdefVeryLarge)) {
                    max = s0 > max ? s0 : max;
                }
                if (s1 != static_cast<std::int16_t>(kCdefVeryLarge)) {
                    max = s1 > max ? s1 : max;
                }
                if (s2 != static_cast<std::int16_t>(kCdefVeryLarge)) {
                    max = s2 > max ? s2 : max;
                }
                if (s3 != static_cast<std::int16_t>(kCdefVeryLarge)) {
                    max = s3 > max ? s3 : max;
                }
                min = s0 < min ? s0 : min;
                min = s1 < min ? s1 : min;
                min = s2 < min ? s2 : min;
                min = s3 < min ? s3 : min;
                sum = static_cast<std::int16_t>(
                    sum + static_cast<std::int16_t>(secTaps[k] * cdefConstrain(s0 - x, secStrength, secDamping)));
                sum = static_cast<std::int16_t>(
                    sum + static_cast<std::int16_t>(secTaps[k] * cdefConstrain(s1 - x, secStrength, secDamping)));
                sum = static_cast<std::int16_t>(
                    sum + static_cast<std::int16_t>(secTaps[k] * cdefConstrain(s2 - x, secStrength, secDamping)));
                sum = static_cast<std::int16_t>(
                    sum + static_cast<std::int16_t>(secTaps[k] * cdefConstrain(s3 - x, secStrength, secDamping)));
            }
            const std::int16_t y = static_cast<std::int16_t>(
                clampInt(static_cast<int>(x) + ((8 + sum - (sum < 0)) >> 4), min, max));
            if (dst8) {
                dst8[i * dstride + j] = static_cast<std::uint8_t>(y);
            } else {
                dst16[i * dstride + j] = static_cast<std::uint16_t>(y);
            }
        }
    }
}

// 1:1 port of svt_cdef_filter_block_8bit_c (cdef.c:257-300): the interior
// 8-bit twin - the SPEC kernel's math with the sentinel path removed
// (max/min unconditional) and the BLOCK_4X4 sub override.
void cdefFilterBlock8bit(std::uint8_t* dst, std::int32_t dstride, const std::uint8_t* in, std::int32_t priStrength,
                         std::int32_t secStrength, std::int32_t dir, std::int32_t damping, CdefBlockSize bsize,
                         std::int32_t coeffShift, std::uint8_t subsamplingFactor) {
    const std::int32_t s = kCdefBstride;
    const std::int32_t* priTaps = kCdefPriTaps[(priStrength >> coeffShift) & 1];
    const std::int32_t* secTaps = kCdefSecTaps[(priStrength >> coeffShift) & 1];
    const std::int32_t rows = (bsize == CdefBlockSize::BLOCK_8X8 || bsize == CdefBlockSize::BLOCK_4X8) ? 8 : 4;
    const std::int32_t cols = (bsize == CdefBlockSize::BLOCK_8X8 || bsize == CdefBlockSize::BLOCK_8X4) ? 8 : 4;
    const std::int32_t sub = (bsize == CdefBlockSize::BLOCK_4X4) ? 1 : subsamplingFactor;
    for (std::int32_t i = 0; i < rows; i += sub) {
        for (std::int32_t j = 0; j < cols; ++j) {
            const std::int16_t x = static_cast<std::int16_t>(in[i * s + j]);
            std::int16_t sum = 0;
            std::int32_t max = x, min = x;
            for (int k = 0; k < 2; ++k) {
                const std::int16_t p0 = static_cast<std::int16_t>(in[i * s + j + kCdefDirectionsPadded[dir + 2][k]]);
                const std::int16_t p1 = static_cast<std::int16_t>(in[i * s + j - kCdefDirectionsPadded[dir + 2][k]]);
                sum = static_cast<std::int16_t>(
                    sum + static_cast<std::int16_t>(priTaps[k] * cdefConstrain(p0 - x, priStrength, damping)));
                sum = static_cast<std::int16_t>(
                    sum + static_cast<std::int16_t>(priTaps[k] * cdefConstrain(p1 - x, priStrength, damping)));
                max = p0 > max ? p0 : max;
                max = p1 > max ? p1 : max;
                min = p0 < min ? p0 : min;
                min = p1 < min ? p1 : min;
                const std::int16_t s0 = static_cast<std::int16_t>(in[i * s + j + kCdefDirectionsPadded[dir + 4][k]]);
                const std::int16_t s1 = static_cast<std::int16_t>(in[i * s + j - kCdefDirectionsPadded[dir + 4][k]]);
                const std::int16_t s2 = static_cast<std::int16_t>(in[i * s + j + kCdefDirectionsPadded[dir][k]]);
                const std::int16_t s3 = static_cast<std::int16_t>(in[i * s + j - kCdefDirectionsPadded[dir][k]]);
                sum = static_cast<std::int16_t>(
                    sum + static_cast<std::int16_t>(secTaps[k] * cdefConstrain(s0 - x, secStrength, damping)));
                sum = static_cast<std::int16_t>(
                    sum + static_cast<std::int16_t>(secTaps[k] * cdefConstrain(s1 - x, secStrength, damping)));
                sum = static_cast<std::int16_t>(
                    sum + static_cast<std::int16_t>(secTaps[k] * cdefConstrain(s2 - x, secStrength, damping)));
                sum = static_cast<std::int16_t>(
                    sum + static_cast<std::int16_t>(secTaps[k] * cdefConstrain(s3 - x, secStrength, damping)));
                max = s0 > max ? s0 : max;
                max = s1 > max ? s1 : max;
                max = s2 > max ? s2 : max;
                max = s3 > max ? s3 : max;
                min = s0 < min ? s0 : min;
                min = s1 < min ? s1 : min;
                min = s2 < min ? s2 : min;
                min = s3 < min ? s3 : min;
            }
            dst[i * dstride + j] = static_cast<std::uint8_t>(
                clampInt(static_cast<int>(x) + ((8 + sum - (sum < 0)) >> 4), min, max));
        }
    }
}

// 1:1 port of svt_aom_cdef_find_dir_8bit_c (cdef.c:303-311).
std::uint8_t cdefFindDir8bit(const std::uint8_t* img, int stride, std::int32_t* var, std::int32_t coeffShift) {
    std::uint16_t img16[8 * 8];
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            img16[i * 8 + j] = img[i * stride + j];
        }
    }
    return cdefFindDir(img16, 8, var, coeffShift);
}

// 1:1 port of svt_aom_cdef_find_dir_dual_8bit_c (cdef.c:313-317).
void cdefFindDirDual8bit(const std::uint8_t* img1, const std::uint8_t* img2, int stride, std::int32_t* var1,
                         std::int32_t* var2, std::int32_t coeffShift, std::uint8_t* out1, std::uint8_t* out2) {
    *out1 = cdefFindDir8bit(img1, stride, var1, coeffShift);
    *out2 = cdefFindDir8bit(img2, stride, var2, coeffShift);
}

// 1:1 port of cdef_tap_off_frame (cdef.c:427-430).
bool cdefTapOffFrame(int r, int c, int rows, int cols, int edgeTop, int edgeBottom, int edgeLeft, int edgeRight) {
    return (edgeTop && r < 0) || (edgeBottom && r >= rows) || (edgeLeft && c < 0) || (edgeRight && c >= cols);
}

// 1:1 port of svt_cdef_filter_block_8bit_bounded_c (cdef.c:437-504).
void cdefFilterBlock8bitBounded(std::uint8_t* dst, std::int32_t dstride, const std::uint8_t* in,
                                std::int32_t priStrength, std::int32_t secStrength, std::int32_t dir,
                                std::int32_t damping, CdefBlockSize bsize, std::int32_t coeffShift,
                                std::uint8_t subsamplingFactor, int edgeTop, int edgeLeft, int edgeBottom,
                                int edgeRight) {
    const std::int32_t s = kCdefBstride;
    const std::int32_t* priTaps = kCdefPriTaps[(priStrength >> coeffShift) & 1];
    const std::int32_t* secTaps = kCdefSecTaps[(priStrength >> coeffShift) & 1];
    const std::int32_t rows = (bsize == CdefBlockSize::BLOCK_8X8 || bsize == CdefBlockSize::BLOCK_4X8) ? 8 : 4;
    const std::int32_t cols = (bsize == CdefBlockSize::BLOCK_8X8 || bsize == CdefBlockSize::BLOCK_8X4) ? 8 : 4;
    const std::int32_t sub = (bsize == CdefBlockSize::BLOCK_4X4) ? 1 : subsamplingFactor;
    for (std::int32_t i = 0; i < rows; i += sub) {
        for (std::int32_t j = 0; j < cols; ++j) {
            const std::int16_t x = static_cast<std::int16_t>(in[i * s + j]);
            std::int16_t sum = 0;
            std::int32_t max = x, min = x;
            for (int k = 0; k < 2; ++k) {
                // Primary taps p0 at (i+dr, j+dc), p1 at (i-dr, j-dc).
                const int pdr = kCdefDirectionsRc[dir + 2][k][0];
                const int pdc = kCdefDirectionsRc[dir + 2][k][1];
                const std::int32_t poff = kCdefDirectionsPadded[dir + 2][k];
                if (!cdefTapOffFrame(i + pdr, j + pdc, rows, cols, edgeTop, edgeBottom, edgeLeft, edgeRight)) {
                    const std::int16_t p0 = static_cast<std::int16_t>(in[i * s + j + poff]);
                    sum = static_cast<std::int16_t>(
                        sum + static_cast<std::int16_t>(priTaps[k] * cdefConstrain(p0 - x, priStrength, damping)));
                    max = p0 > max ? p0 : max;
                    min = p0 < min ? p0 : min;
                }
                if (!cdefTapOffFrame(i - pdr, j - pdc, rows, cols, edgeTop, edgeBottom, edgeLeft, edgeRight)) {
                    const std::int16_t p1 = static_cast<std::int16_t>(in[i * s + j - poff]);
                    sum = static_cast<std::int16_t>(
                        sum + static_cast<std::int16_t>(priTaps[k] * cdefConstrain(p1 - x, priStrength, damping)));
                    max = p1 > max ? p1 : max;
                    min = p1 < min ? p1 : min;
                }
                // Secondary taps from directions dir+2 and dir-2 (+/- offset).
                const int s0dr = kCdefDirectionsRc[dir + 4][k][0];
                const int s0dc = kCdefDirectionsRc[dir + 4][k][1];
                const std::int32_t s0off = kCdefDirectionsPadded[dir + 4][k];
                const int s2dr = kCdefDirectionsRc[dir][k][0];
                const int s2dc = kCdefDirectionsRc[dir][k][1];
                const std::int32_t s2off = kCdefDirectionsPadded[dir][k];
                if (!cdefTapOffFrame(i + s0dr, j + s0dc, rows, cols, edgeTop, edgeBottom, edgeLeft, edgeRight)) {
                    const std::int16_t s0 = static_cast<std::int16_t>(in[i * s + j + s0off]);
                    sum = static_cast<std::int16_t>(
                        sum + static_cast<std::int16_t>(secTaps[k] * cdefConstrain(s0 - x, secStrength, damping)));
                    max = s0 > max ? s0 : max;
                    min = s0 < min ? s0 : min;
                }
                if (!cdefTapOffFrame(i - s0dr, j - s0dc, rows, cols, edgeTop, edgeBottom, edgeLeft, edgeRight)) {
                    const std::int16_t s1 = static_cast<std::int16_t>(in[i * s + j - s0off]);
                    sum = static_cast<std::int16_t>(
                        sum + static_cast<std::int16_t>(secTaps[k] * cdefConstrain(s1 - x, secStrength, damping)));
                    max = s1 > max ? s1 : max;
                    min = s1 < min ? s1 : min;
                }
                if (!cdefTapOffFrame(i + s2dr, j + s2dc, rows, cols, edgeTop, edgeBottom, edgeLeft, edgeRight)) {
                    const std::int16_t s2 = static_cast<std::int16_t>(in[i * s + j + s2off]);
                    sum = static_cast<std::int16_t>(
                        sum + static_cast<std::int16_t>(secTaps[k] * cdefConstrain(s2 - x, secStrength, damping)));
                    max = s2 > max ? s2 : max;
                    min = s2 < min ? s2 : min;
                }
                if (!cdefTapOffFrame(i - s2dr, j - s2dc, rows, cols, edgeTop, edgeBottom, edgeLeft, edgeRight)) {
                    const std::int16_t s3 = static_cast<std::int16_t>(in[i * s + j - s2off]);
                    sum = static_cast<std::int16_t>(
                        sum + static_cast<std::int16_t>(secTaps[k] * cdefConstrain(s3 - x, secStrength, damping)));
                    max = s3 > max ? s3 : max;
                    min = s3 < min ? s3 : min;
                }
            }
            dst[i * dstride + j] = static_cast<std::uint8_t>(
                clampInt(static_cast<int>(x) + ((8 + sum - (sum < 0)) >> 4), min, max));
        }
    }
}

// 1:1 port of svt_aom_cdef_find_dir_dual_c (cdef.c:152-156).
void cdefFindDirDual(const std::uint16_t* img1, const std::uint16_t* img2, int stride, std::int32_t* var1,
                     std::int32_t* var2, std::int32_t coeffShift, std::uint8_t* out1, std::uint8_t* out2) {
    *out1 = cdefFindDir(img1, stride, var1, coeffShift);
    *out2 = cdefFindDir(img2, stride, var2, coeffShift);
}

// 1:1 port of cdef_find_dir (cdef.c:158-187), the list walker.
void cdefFindDirList(const std::uint16_t* in, const CdefList* dlist, std::int32_t var[kCdefNblocks][kCdefNblocks],
                     std::int32_t cdefCount, std::int32_t coeffShift, std::uint8_t dir[kCdefNblocks][kCdefNblocks]) {
    int bi;
    // Find direction of two 8x8 blocks together.
    for (bi = 0; bi < cdefCount - 1; bi += 2) {
        const std::uint8_t by = dlist[bi].by;
        const std::uint8_t bx = dlist[bi].bx;
        const std::uint8_t by2 = dlist[bi + 1].by;
        const std::uint8_t bx2 = dlist[bi + 1].bx;
        const int pos1 = 8 * by * kCdefBstride + 8 * bx;
        const int pos2 = 8 * by2 * kCdefBstride + 8 * bx2;
        cdefFindDirDual(&in[pos1], &in[pos2], kCdefBstride, &var[by][bx], &var[by2][bx2], coeffShift, &dir[by][bx],
                        &dir[by2][bx2]);
    }
    // Process remaining 8x8 blocks here. One 8x8 at a time.
    if (cdefCount % 2) {
        const std::uint8_t by = dlist[bi].by;
        const std::uint8_t bx = dlist[bi].bx;
        dir[by][bx] = cdefFindDir(&in[8 * by * kCdefBstride + 8 * bx], kCdefBstride, &var[by][bx], coeffShift);
    }
}

// 1:1 port of cdef_find_dir_8bit (cdef.c:509-535), the 8-bit list walker.
void cdefFindDir8bitList(const std::uint8_t* in, const CdefList* dlist,
                         std::int32_t var[kCdefNblocks][kCdefNblocks], std::int32_t cdefCount,
                         std::int32_t coeffShift, std::uint8_t dir[kCdefNblocks][kCdefNblocks]) {
    int bi;
    for (bi = 0; bi < cdefCount - 1; bi += 2) {
        const std::uint8_t by = dlist[bi].by;
        const std::uint8_t bx = dlist[bi].bx;
        const std::uint8_t by2 = dlist[bi + 1].by;
        const std::uint8_t bx2 = dlist[bi + 1].bx;
        const int pos1 = 8 * by * kCdefBstride + 8 * bx;
        const int pos2 = 8 * by2 * kCdefBstride + 8 * bx2;
        cdefFindDirDual8bit(&in[pos1], &in[pos2], kCdefBstride, &var[by][bx], &var[by2][bx2], coeffShift, &dir[by][bx],
                            &dir[by2][bx2]);
    }
    if (cdefCount % 2) {
        const std::uint8_t by = dlist[bi].by;
        const std::uint8_t bx = dlist[bi].bx;
        dir[by][bx] = cdefFindDir8bit(&in[8 * by * kCdefBstride + 8 * bx], kCdefBstride, &var[by][bx], coeffShift);
    }
}

// The strength derivation of cdef.c:327-329 / :551-553 (named deviation:
// factored out of the fb bodies for testability).
std::int32_t cdefPriStrength(std::int32_t cdefStrength, std::int32_t coeffShift) {
    return (cdefStrength / kCdefSecStrengths) << coeffShift;
}

std::int32_t cdefSecStrength(std::int32_t cdefStrength, std::int32_t coeffShift) {
    const std::int32_t sec = cdefStrength % kCdefSecStrengths;
    return (sec + (sec == 3)) << coeffShift;
}

// 1:1 port of svt_aom_copy_rect8_8bit_to_16bit_c (cdef.c:72-79).
void cdefCopyRect8To16(std::uint16_t* dst, std::int32_t dstride, const std::uint8_t* src, std::int32_t sstride,
                       std::int32_t v, std::int32_t h) {
    for (std::int32_t i = 0; i < v; ++i) {
        for (std::int32_t j = 0; j < h; ++j) {
            dst[i * dstride + j] = src[i * sstride + j];
        }
    }
}

// 1:1 port of svt_cdef_copy_rect8 (cdef_copy.h:28-74): the row copy is a
// memcpy of h bytes either at a listed constant width or the variable
// fallback; base = src + src_voffset * sstride + src_hoffset.
void cdefCopyRect8(std::uint8_t* dst, std::int32_t dstride, const std::uint8_t* src, std::int32_t srcVoffset,
                   std::int32_t srcHoffset, std::int32_t sstride, std::int32_t v, std::int32_t h) {
    const std::uint8_t* base = src + srcVoffset * sstride + srcHoffset;
    if (h == 2 || h == 32 || h == 34 || h == 36 || h == 64 || h == 66 || h == 68) {
        for (std::int32_t r = 0; r < v; ++r) {
            std::memcpy(dst, base, static_cast<std::size_t>(h));
            dst += dstride;
            base += sstride;
        }
        return;
    }
    for (std::int32_t r = 0; r < v; ++r) {
        std::memcpy(dst, base, static_cast<std::size_t>(h));
        dst += dstride;
        base += sstride;
    }
}

// 1:1 port of svt_aom_copy_rect (cdef_copy.h:79-85) - copy_sb8_16's 16-bit
// branch dependency.
void cdefCopyRect16(std::uint16_t* dst, std::int32_t dstride, const std::uint16_t* src, std::int32_t sstride,
                    std::int32_t v, std::int32_t h) {
    for (std::int32_t r = 0; r < v; ++r) {
        std::memcpy(dst, src, sizeof(*dst) * static_cast<std::size_t>(h));
        dst += dstride;
        src += sstride;
    }
}

// 1:1 port of svt_aom_copy_sb8_16 (cdef_copy.h:96-105).
void cdefCopySb8To16(std::uint16_t* dst, std::int32_t dstride, const std::uint8_t* src, std::int32_t srcVoffset,
                     std::int32_t srcHoffset, std::int32_t sstride, std::int32_t vsize, std::int32_t hsize,
                     int is16bit) {
    if (is16bit) {
        cdefCopyRect16(dst, dstride, reinterpret_cast<const std::uint16_t*>(src) + (srcVoffset * sstride + srcHoffset),
                       sstride, vsize, hsize);
    } else {
        cdefCopyRect8To16(dst, dstride, src + srcVoffset * sstride + srcHoffset, sstride, vsize, hsize);
    }
}

// 1:1 port of cdef_filter_fb_lbd_impl (cdef.c:542-638): the 8-bit fb BODY.
// Exported (named deviation - see the header note).
void cdefFilterFbLbdImpl(std::uint8_t* dst8, std::int32_t dstride, const std::uint8_t* in8, int frameTop,
                         int frameLeft, int frameBottom, int frameRight, int vsize, int hsize, std::int32_t xdec,
                         std::int32_t ydec, std::uint8_t dir[kCdefNblocks][kCdefNblocks], std::int32_t* dirinit,
                         std::int32_t var[kCdefNblocks][kCdefNblocks], std::int32_t pli, const CdefList* dlist,
                         std::int32_t cdefCount, std::int32_t cdefStrength, std::int32_t damping,
                         std::int32_t coeffShift, std::uint8_t subsamplingFactor, CdefBlockSize bsize,
                         std::int32_t bsizex, std::int32_t bsizey, int isLuma) {
    const std::int32_t priStrength = cdefPriStrength(cdefStrength, coeffShift);
    const std::int32_t secStrength = cdefSecStrength(cdefStrength, coeffShift);
    damping += coeffShift - (isLuma ? 0 : 1);

    // Last block row/col index within the fb (frame-edge ring detection)
    // (cdef.c:557-558).
    const std::int32_t byLast = (vsize >> bsizey) - 1;
    const std::int32_t bxLast = (hsize >> bsizex) - 1;

    if (!dstride && cdefStrength == 0) {
        // Zero-strength search candidate: copy input straight to packed
        // output (4/8-byte rows) (cdef.c:561-581).
        const int w = 1 << bsizex;
        if (w == 8) {
            for (std::int32_t bi = 0; bi < cdefCount; ++bi) {
                const std::uint8_t* src8 = in8 + ((dlist[bi].by << bsizey) * kCdefBstride + (dlist[bi].bx << bsizex));
                std::uint8_t* dstb = dst8 + (bi << (bsizex + bsizey));
                for (std::int32_t iy = 0; iy < (1 << bsizey); iy += subsamplingFactor) {
                    std::memcpy(dstb + (iy << bsizex), src8 + iy * kCdefBstride, 8);
                }
            }
        } else {
            for (std::int32_t bi = 0; bi < cdefCount; ++bi) {
                const std::uint8_t* src8 = in8 + ((dlist[bi].by << bsizey) * kCdefBstride + (dlist[bi].bx << bsizex));
                std::uint8_t* dstb = dst8 + (bi << (bsizex + bsizey));
                for (std::int32_t iy = 0; iy < (1 << bsizey); iy += subsamplingFactor) {
                    std::memcpy(dstb + (iy << bsizex), src8 + iy * kCdefBstride, 4);
                }
            }
        }
        return;
    }

    if (isLuma) {
        if (!dirinit || !*dirinit) {
            cdefFindDir8bitList(in8, dlist, var, cdefCount, coeffShift, dir);
            if (dirinit) {
                *dirinit = 1;
            }
        }
    } else if (pli == 1 && xdec != ydec) {
        for (std::int32_t bi = 0; bi < cdefCount; ++bi) {
            static const std::uint8_t conv422[8] = {7, 0, 2, 4, 5, 6, 6, 6};
            static const std::uint8_t conv440[8] = {1, 2, 2, 2, 3, 4, 6, 0};
            const std::int32_t by = dlist[bi].by;
            const std::int32_t bx = dlist[bi].bx;
            dir[by][bx] = (xdec ? conv422 : conv440)[dir[by][bx]];
        }
    }

    // Strength 0 reaches here only as the "compute dir for later planes"
    // pass; identity passthrough (cdef.c:601-603).
    if (cdefStrength == 0) {
        return;
    }

    const bool edgeFb = frameTop || frameLeft || frameBottom || frameRight;
    for (std::int32_t bi = 0; bi < cdefCount; ++bi) {
        const std::int32_t by = dlist[bi].by;
        const std::int32_t bx = dlist[bi].bx;
        const std::int32_t t = isLuma ? cdefAdjustStrength(priStrength, var[by][bx]) : priStrength;
        const std::int32_t k = dstride ? ((by << bsizey) * dstride + (bx << bsizex)) : (bi << (bsizex + bsizey));
        const std::int32_t ds = dstride ? dstride : (1 << bsizex);
        const std::int32_t off = (by * kCdefBstride << bsizey) + (bx << bsizex);
        const std::int32_t bdir = priStrength ? dir[by][bx] : 0;
        const int edgeTop = frameTop && by == 0;
        const int edgeBottom = frameBottom && by == byLast;
        const int edgeLeft = frameLeft && bx == 0;
        const int edgeRight = frameRight && bx == bxLast;
        if (edgeFb && (edgeTop || edgeBottom || edgeLeft || edgeRight)) {
            cdefFilterBlock8bitBounded(&dst8[k],
                                       ds,
                                       &in8[off],
                                       t,
                                       secStrength,
                                       bdir,
                                       damping,
                                       bsize,
                                       coeffShift,
                                       subsamplingFactor,
                                       edgeTop,
                                       edgeLeft,
                                       edgeBottom,
                                       edgeRight);
        } else {
            cdefFilterBlock8bit(&dst8[k], ds, &in8[off], t, secStrength, bdir, damping, bsize, coeffShift,
                                subsamplingFactor);
        }
    }
}

// 1:1 port of cdef_filter_fb_lbd_luma (cdef.c:640-671): the compile-time
// luma pinning 8X8 / 3 / 3 / is_luma 1, pli forced 0, xdec=ydec=0.
void cdefFilterFbLbdLuma(std::uint8_t* dst8, std::int32_t dstride, const std::uint8_t* in8, int frameTop,
                         int frameLeft, int frameBottom, int frameRight, int vsize, int hsize,
                         std::uint8_t dir[kCdefNblocks][kCdefNblocks], std::int32_t* dirinit,
                         std::int32_t var[kCdefNblocks][kCdefNblocks], const CdefList* dlist, std::int32_t cdefCount,
                         std::int32_t cdefStrength, std::int32_t damping, std::int32_t coeffShift,
                         std::uint8_t subsamplingFactor) {
    cdefFilterFbLbdImpl(dst8, dstride, in8, frameTop, frameLeft, frameBottom, frameRight, vsize, hsize, 0, 0, dir,
                        dirinit, var, 0, dlist, cdefCount, cdefStrength, damping, coeffShift, subsamplingFactor,
                        CdefBlockSize::BLOCK_8X8, 3, 3, 1);
}

// 1:1 port of cdef_filter_fb_lbd_chroma (cdef.c:673-704): the compile-time
// chroma 4:2:0 pinning 4X4 / 2 / 2 / is_luma 0, pli passed through.
void cdefFilterFbLbdChroma(std::uint8_t* dst8, std::int32_t dstride, const std::uint8_t* in8, int frameTop,
                           int frameLeft, int frameBottom, int frameRight, int vsize, int hsize, std::int32_t pli,
                           std::uint8_t dir[kCdefNblocks][kCdefNblocks], std::int32_t* dirinit,
                           std::int32_t var[kCdefNblocks][kCdefNblocks], const CdefList* dlist,
                           std::int32_t cdefCount, std::int32_t cdefStrength, std::int32_t damping,
                           std::int32_t coeffShift, std::uint8_t subsamplingFactor) {
    cdefFilterFbLbdImpl(dst8, dstride, in8, frameTop, frameLeft, frameBottom, frameRight, vsize, hsize, 1, 1, dir,
                        dirinit, var, pli, dlist, cdefCount, cdefStrength, damping, coeffShift, subsamplingFactor,
                        CdefBlockSize::BLOCK_4X4, 2, 2, 0);
}

// 1:1 port of svt_cdef_filter_fb_lbd (cdef.c:706-778): the xdec/ydec
// dispatch.
void cdefFilterFbLbd(std::uint8_t* dst8, std::int32_t dstride, const std::uint8_t* in8, int frameTop, int frameLeft,
                     int frameBottom, int frameRight, int vsize, int hsize, std::int32_t xdec, std::int32_t ydec,
                     std::uint8_t dir[kCdefNblocks][kCdefNblocks], std::int32_t* dirinit,
                     std::int32_t var[kCdefNblocks][kCdefNblocks], std::int32_t pli, const CdefList* dlist,
                     std::int32_t cdefCount, std::int32_t cdefStrength, std::int32_t damping,
                     std::int32_t coeffShift, std::uint8_t subsamplingFactor) {
    if (xdec == 0 && ydec == 0) {  // luma: BLOCK_8X8
        cdefFilterFbLbdLuma(dst8, dstride, in8, frameTop, frameLeft, frameBottom, frameRight, vsize, hsize, dir,
                            dirinit, var, dlist, cdefCount, cdefStrength, damping, coeffShift, subsamplingFactor);
    } else if (xdec == 1 && ydec == 1) {  // chroma 4:2:0: BLOCK_4X4
        cdefFilterFbLbdChroma(dst8, dstride, in8, frameTop, frameLeft, frameBottom, frameRight, vsize, hsize, pli, dir,
                              dirinit, var, dlist, cdefCount, cdefStrength, damping, coeffShift, subsamplingFactor);
    } else {  // 4:2:2 / 4:4:4: runtime block size
        const CdefBlockSize bsize =
            ydec ? (xdec ? CdefBlockSize::BLOCK_4X4 : CdefBlockSize::BLOCK_8X4)
                 : (xdec ? CdefBlockSize::BLOCK_4X8 : CdefBlockSize::BLOCK_8X8);
        cdefFilterFbLbdImpl(dst8, dstride, in8, frameTop, frameLeft, frameBottom, frameRight, vsize, hsize, xdec, ydec,
                            dir, dirinit, var, pli, dlist, cdefCount, cdefStrength, damping, coeffShift,
                            subsamplingFactor, bsize, 3 - xdec, 3 - ydec, pli == 0);
    }
}

// 1:1 port of svt_cdef_filter_fb (cdef.c:322-402). bsize derives per the
// reference ternary (:332); the zero-strength + packed-dst search-scratch
// identity copy is cdef.c:336-364.
void cdefFilterFb(std::uint8_t* dst8, std::uint16_t* dst16, std::int32_t dstride, const std::uint16_t* in,
                  std::int32_t xdec, std::int32_t ydec, std::uint8_t dir[kCdefNblocks][kCdefNblocks],
                  std::int32_t* dirinit, std::int32_t var[kCdefNblocks][kCdefNblocks], std::int32_t pli,
                  const CdefList* dlist, std::int32_t cdefCount, std::int32_t cdefStrength, std::int32_t damping,
                  std::int32_t coeffShift, std::uint8_t subsamplingFactor) {
    const std::int32_t priStrength = cdefPriStrength(cdefStrength, coeffShift);
    const std::int32_t secStrength = cdefSecStrength(cdefStrength, coeffShift);
    damping += coeffShift - (pli != 0);

    const CdefBlockSize bsize =
        ydec ? (xdec ? CdefBlockSize::BLOCK_4X4 : CdefBlockSize::BLOCK_8X4)
             : (xdec ? CdefBlockSize::BLOCK_4X8 : CdefBlockSize::BLOCK_8X8);
    const std::int32_t bsizex = 3 - xdec;
    const std::int32_t bsizey = 3 - ydec;

    if (!dstride && cdefStrength == 0) {
        for (std::int32_t bi = 0; bi < cdefCount; ++bi) {
            const std::int32_t by = dlist[bi].by << bsizey;
            const std::int32_t bx = dlist[bi].bx << bsizex;
            const std::uint16_t* src16 = in + (by * kCdefBstride + bx);
            if (dst8) {
                // size 2x2 and 3x3, no gain to use SIMD (cdef.c:348)
                std::uint8_t* dst8b = dst8 + (bi << (bsizex + bsizey));
                for (std::int32_t iy = 0; iy < (1 << bsizey); iy += subsamplingFactor) {
                    for (std::int32_t ix = 0; ix < (1 << bsizex); ++ix) {
                        dst8b[(iy << bsizex) + ix] = static_cast<std::uint8_t>(src16[iy * kCdefBstride + ix]);
                    }
                }
            } else {
                std::uint16_t* dst16b = dst16 + (bi << (bsizex + bsizey));
                for (std::int32_t iy = 0; iy < (1 << bsizey); iy += subsamplingFactor) {
                    std::memcpy(dst16b + (iy << bsizex), src16 + iy * kCdefBstride,
                                static_cast<std::uint32_t>(1 << bsizex) * sizeof(std::uint16_t));
                }
            }
        }
        return;
    }

    if (pli == 0) {
        if (!dirinit || !*dirinit) {
            cdefFindDirList(in, dlist, var, cdefCount, coeffShift, dir);
            if (dirinit) {
                *dirinit = 1;
            }
        }
    } else if (pli == 1 && xdec != ydec) {
        for (std::int32_t bi = 0; bi < cdefCount; ++bi) {
            static const std::uint8_t conv422[8] = {7, 0, 2, 4, 5, 6, 6, 6};
            static const std::uint8_t conv440[8] = {1, 2, 2, 2, 3, 4, 6, 0};

            const std::int32_t by = dlist[bi].by;
            const std::int32_t bx = dlist[bi].bx;
            dir[by][bx] = (xdec ? conv422 : conv440)[dir[by][bx]];
        }
    }

    for (std::int32_t bi = 0; bi < cdefCount; ++bi) {
        const std::int32_t by = dlist[bi].by;
        const std::int32_t bx = dlist[bi].bx;
        const std::int32_t t = pli ? priStrength : cdefAdjustStrength(priStrength, var[by][bx]);
        const std::int32_t k = dstride ? ((by << bsizey) * dstride + (bx << bsizex)) : (bi << (bsizex + bsizey));
        cdefFilterBlock(dst8 ? &dst8[k] : nullptr,
                        dst8 ? nullptr : &dst16[k],
                        dstride ? dstride : (1 << bsizex),
                        &in[(by * kCdefBstride << bsizey) + (bx << bsizex)],
                        t,
                        secStrength,
                        priStrength ? dir[by][bx] : 0,
                        damping,
                        damping,
                        bsize,
                        coeffShift,
                        subsamplingFactor);
    }
}

namespace {

// 1:1 port of svt_aom_memset16 (svt_malloc.h:95-101): the element loop the
// cdef_copy.h helpers compose (named deviation: the reference svt_malloc.h
// body, as a plain function - behavior identical, no allocator surface).
void svtAomMemset16(std::uint16_t* dest, std::int32_t val, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
        dest[i] = static_cast<std::uint16_t>(val);
    }
}

} // namespace

// 1:1 port of svt_aom_fill_rect (cdef_copy.h:88-92): the uint16 rect fill
// (edge-path off-frame sentinel); composes svt_aom_memset16 per row.
void cdefFillRect(std::uint16_t* dst, std::int32_t dstride, std::int32_t v, std::int32_t h, std::uint16_t x) {
    for (std::int32_t r = 0; r < v; ++r) {
        svtAomMemset16(dst + r * dstride, x, static_cast<std::size_t>(h));
    }
}

// 1:1 port of mse_8xn_16bit_c (enc_cdef.c:24-35). src packed (row stride 8),
// dst strided by dstride; rows step by the subsampling factor.
std::uint64_t cdefMse8xn16bit(const std::uint16_t* src, const std::uint16_t* dst, std::int32_t dstride,
                              std::int32_t height, std::uint8_t subsamplingFactor) {
    std::uint64_t sum = 0;
    for (std::int32_t i = 0; i < height; i += subsamplingFactor) {
        for (std::int32_t j = 0; j < 8; ++j) {
            const std::int32_t e = dst[i * dstride + j] - src[8 * i + j];
            sum += e * e;
        }
    }
    return sum;
}

// 1:1 port of mse_4xn_16bit_c (enc_cdef.c:37-48): the 4-wide twin.
std::uint64_t cdefMse4xn16bit(const std::uint16_t* src, const std::uint16_t* dst, std::int32_t dstride,
                              std::int32_t height, std::uint8_t subsamplingFactor) {
    std::uint64_t sum = 0;
    for (std::int32_t i = 0; i < height; i += subsamplingFactor) {
        for (std::int32_t j = 0; j < 4; ++j) {
            const std::int32_t e = dst[i * dstride + j] - src[4 * i + j];
            sum += e * e;
        }
    }
    return sum;
}

// 1:1 port of mse_8xn_8bit_c (enc_cdef.c:50-61): the 8-bit 8-wide twin.
std::uint64_t cdefMse8xn8bit(const std::uint8_t* src, const std::uint8_t* dst, std::int32_t dstride,
                             std::int32_t height, std::uint8_t subsamplingFactor) {
    std::uint64_t sum = 0;
    for (std::int32_t i = 0; i < height; i += subsamplingFactor) {
        for (std::int32_t j = 0; j < 8; ++j) {
            const std::int32_t e = dst[i * dstride + j] - src[8 * i + j];
            sum += e * e;
        }
    }
    return sum;
}

// 1:1 port of mse_4xn_8bit_c (enc_cdef.c:63-74): the 8-bit 4-wide twin.
std::uint64_t cdefMse4xn8bit(const std::uint8_t* src, const std::uint8_t* dst, std::int32_t dstride,
                             std::int32_t height, std::uint8_t subsamplingFactor) {
    std::uint64_t sum = 0;
    for (std::int32_t i = 0; i < height; i += subsamplingFactor) {
        for (std::int32_t j = 0; j < 4; ++j) {
            const std::int32_t e = dst[i * dstride + j] - src[4 * i + j];
            sum += e * e;
        }
    }
    return sum;
}

// ---- CDEF3: the block-level dist walkers ----------------------------------

// svt_aom_compute_cdef_dist_16bit_c (enc_cdef.c:77-113): the block-level dist
// walker. src is the packed filtered-block scratch (bi << (log2w + log2h)),
// dst the recon frame strided by dstride; the bsize branch pins the (by,bx)
// origin shifts and the mse kernel per branch, and the sum is returned
// >> 2*coeff_shift.
std::uint64_t cdefComputeDist16bit(const std::uint16_t* dst, std::int32_t dstride, const std::uint16_t* src,
                                   const CdefList* dlist, std::int32_t cdefCount, CdefBlockSize bsize,
                                   std::int32_t coeffShift, std::uint8_t subsamplingFactor) {
    std::uint64_t sum = 0;
    std::int32_t bi, bx, by;
    if (bsize == CdefBlockSize::BLOCK_8X8) {
        for (bi = 0; bi < cdefCount; bi++) {
            by = dlist[bi].by;
            bx = dlist[bi].bx;
            sum += cdefMse8xn16bit(&src[bi << (3 + 3)], &dst[(by << 3) * dstride + (bx << 3)], dstride, 8,
                                   subsamplingFactor);
        }
    } else if (bsize == CdefBlockSize::BLOCK_4X8) {
        for (bi = 0; bi < cdefCount; bi++) {
            by = dlist[bi].by;
            bx = dlist[bi].bx;
            sum += cdefMse4xn16bit(&src[bi << (3 + 2)], &dst[(by << 3) * dstride + (bx << 2)], dstride, 8,
                                   subsamplingFactor);
        }
    } else if (bsize == CdefBlockSize::BLOCK_8X4) {
        for (bi = 0; bi < cdefCount; bi++) {
            by = dlist[bi].by;
            bx = dlist[bi].bx;
            sum += cdefMse8xn16bit(&src[bi << (2 + 3)], &dst[(by << 2) * dstride + (bx << 3)], dstride, 4,
                                   subsamplingFactor);
        }
    } else {
        for (bi = 0; bi < cdefCount; bi++) {
            by = dlist[bi].by;
            bx = dlist[bi].bx;
            sum += cdefMse4xn16bit(&src[bi << (2 + 2)], &dst[(by << 2) * dstride + (bx << 2)], dstride, 4,
                                   subsamplingFactor);
        }
    }
    return sum >> 2 * coeffShift;
}

// svt_aom_compute_cdef_dist_8bit_c (enc_cdef.c:115-151): the 8-bit twin -
// identical shift pairs through the 8-bit mse kernels.
std::uint64_t cdefComputeDist8bit(const std::uint8_t* dst, std::int32_t dstride, const std::uint8_t* src,
                                  const CdefList* dlist, std::int32_t cdefCount, CdefBlockSize bsize,
                                  std::int32_t coeffShift, std::uint8_t subsamplingFactor) {
    std::uint64_t sum = 0;
    std::int32_t bi, bx, by;
    if (bsize == CdefBlockSize::BLOCK_8X8) {
        for (bi = 0; bi < cdefCount; bi++) {
            by = dlist[bi].by;
            bx = dlist[bi].bx;
            sum += cdefMse8xn8bit(&src[bi << (3 + 3)], &dst[(by << 3) * dstride + (bx << 3)], dstride, 8,
                                  subsamplingFactor);
        }
    } else if (bsize == CdefBlockSize::BLOCK_4X8) {
        for (bi = 0; bi < cdefCount; bi++) {
            by = dlist[bi].by;
            bx = dlist[bi].bx;
            sum += cdefMse4xn8bit(&src[bi << (3 + 2)], &dst[(by << 3) * dstride + (bx << 2)], dstride, 8,
                                  subsamplingFactor);
        }
    } else if (bsize == CdefBlockSize::BLOCK_8X4) {
        for (bi = 0; bi < cdefCount; bi++) {
            by = dlist[bi].by;
            bx = dlist[bi].bx;
            sum += cdefMse8xn8bit(&src[bi << (2 + 3)], &dst[(by << 2) * dstride + (bx << 3)], dstride, 4,
                                  subsamplingFactor);
        }
    } else {
        for (bi = 0; bi < cdefCount; bi++) {
            by = dlist[bi].by;
            bx = dlist[bi].bx;
            sum += cdefMse4xn8bit(&src[bi << (2 + 2)], &dst[(by << 2) * dstride + (bx << 2)], dstride, 4,
                                  subsamplingFactor);
        }
    }
    return sum >> 2 * coeffShift;
}

// 1:1 port of cdef_apply_fb (enc_cdef.c:200-297, static inline, inside the
// CDEF_8BITS_PATH guard): the native 8-bit src8 builder for one 64x64 fb.
// src8 is the padded CDEF_BSTRIDE buffer (stride 144, vborder 3, hborder 8);
// the body copy clamps its bottom/right extent on frame edges (off-frame
// taps are masked geometrically by the bounded kernel). prevRowCdef selects
// linebuf over recon for the top halos; colbuf carries the left halo; the
// filter call runs whenever the strength is active or dir is stale.
void cdefApplyFb(std::uint8_t* recBuff, std::uint32_t recStride, std::uint8_t* src8, std::uint8_t* linebufPli,
                 std::uint8_t* colbufPli, const std::uint8_t* prevRowCdef, int fbr, int fbc, int nvfb, int nhfb,
                 int coffset, int mhl2, int mwl2, int nhb, int nvb, int stride, int rend, int cend, int cstart,
                 int cdefLeft, std::uint8_t dir[kCdefNblocks][kCdefNblocks], int* dirinit,
                 std::int32_t var[kCdefNblocks][kCdefNblocks], int pli, CdefList* dlist, int cdefCount,
                 int cdefStrength, int damping, int coeffShift, int xdec, int ydec, int frameTop, int frameLeft,
                 int frameBottom, int frameRight) {
    const int vsz = nvb << mhl2;
    const int hsz = nhb << mwl2;
    // CDEF taps reach only +-CDEF_HALO px, so the recon copies need only a CDEF_HALO-wide halo even
    // though the buffer is padded to HBORDER/VBORDER for aligned SIMD loads (the extra border is
    // never read into any output). Name the halo geometry inside the padded buffer.
    const int halo_col = kCdefHBorder - kCdefHalo; // left-halo column
    const int halo_row = kCdefVBorder - kCdefHalo; // top-halo row
    const int top_off  = halo_row * kCdefBstride + kCdefHBorder; // src8 (top halo, body col 0)
    const int tl_off   = halo_row * kCdefBstride + halo_col; // src8 (top halo, left halo col)
    const int tr_off   = halo_row * kCdefBstride + kCdefHBorder + hsz; // src8 (top halo, right body edge)
    const int lrow     = halo_row * stride; // linebuf row offset of the halo
    const int cs       = cstart < -kCdefHalo ? -kCdefHalo : cstart;
    // Off-frame bottom/right have no in-frame halo: clamp the recon body copy so it never reads past
    // the frame edge (those taps are masked geometrically by the bounded kernel).
    const int body_rows = frameBottom ? vsz : vsz + kCdefHalo;
    const int cright    = frameRight ? hsz : cend - (kCdefHBorder - kCdefHalo);
    // Body + top halo in ONE copy from recon (also brings the left/right and bottom halo). The top
    // halo is overwritten from the pre-filter linebuf below wherever the top neighbour fb was already
    // CDEF-filtered (its current recon is post-filter and unusable as halo).
    cdefCopyRect8(&src8[(kCdefVBorder - kCdefHalo) * kCdefBstride + kCdefHBorder + cs], kCdefBstride, recBuff,
                  (kMiSize64x64 << mhl2) * fbr - kCdefHalo, coffset + cs, recStride, kCdefHalo + body_rows,
                  cright - cs);
    // Overwrite top center / top-left / top-right from the pre-filter linebuf where that neighbour
    // was filtered (else the recon copied above is correct).
    if (prevRowCdef[fbc]) {
        cdefCopyRect8(&src8[top_off], kCdefBstride, &linebufPli[lrow + coffset], 0, 0, stride, kCdefHalo, hsz);
    }
    if (prevRowCdef[fbc - 1]) {
        cdefCopyRect8(&src8[tl_off], kCdefBstride, &linebufPli[lrow + coffset - kCdefHalo], 0, 0, stride,
                      kCdefHalo, kCdefHalo);
    }
    if (prevRowCdef[fbc + 1]) {
        cdefCopyRect8(&src8[tr_off], kCdefBstride, &linebufPli[lrow + coffset + hsz], 0, 0, stride,
                      kCdefHalo, kCdefHalo);
    }
    // Left halo (from colbuf, if the left fb was filtered).
    if (cdefLeft) {
        cdefCopyRect8(&src8[halo_col], kCdefBstride, &colbufPli[halo_col], 0, 0, kCdefHBorder, rend + kCdefVBorder,
                      kCdefHalo);
    }
    // Save right halo edge to colbuf for the fb to the right.
    if (fbc < nhfb - 1) {
        cdefCopyRect8(&colbufPli[halo_col], kCdefHBorder, &src8[hsz + halo_col], 0, 0, kCdefBstride,
                      rend + kCdefVBorder, kCdefHalo);
    }
    // Save bottom edge to linebuf for the fb below.
    if (fbr < nvfb - 1) {
        cdefCopyRect8(&linebufPli[coffset], stride,
                      &recBuff[((kMiSize64x64 << mhl2) * (fbr + 1) - kCdefVBorder) * recStride + coffset],
                      0, 0, recStride, kCdefVBorder, hsz);
    }
    if (cdefStrength || !*dirinit) {
        cdefFilterFbLbd(&recBuff[recStride * (kMiSize64x64 * fbr << mhl2) + (fbc * kMiSize64x64 << mwl2)],
                        recStride, &src8[kCdefVBorder * kCdefBstride + kCdefHBorder], frameTop, frameLeft,
                        frameBottom, frameRight, vsz, hsz, xdec, ydec, dir, dirinit, var, pli, dlist, cdefCount,
                        cdefStrength, damping, coeffShift, 1);
    }
}

// svt_search_one_dual_c (enc_cdef.c:714-772): the greedy /3 search over
// (lev0, lev1) pairs. The search appends the best pair to lev0/lev1 at
// nbStrengths and returns the total mse. This is the exact reference logic,
// ported as-is with uint64 accumulation and the (uint64_t)1 << 63 init.
std::uint64_t cdefSearchOneDual(std::int32_t* lev0, std::int32_t* lev1, int nbStrengths, std::uint64_t** mse[2],
                                int sbCount, int startGi, int endGi) {
    std::uint64_t totMse[64][64];
    std::int32_t i, j, k;
    std::uint64_t bestTotMse = (std::uint64_t)1 << 63;
    std::int32_t bestId0 = 0;
    std::int32_t bestId1 = 0;
    const std::int32_t totalStrengths = endGi;
    memset(totMse, 0, sizeof(totMse));
    // Loop over the filter blocks in the frame
    for (i = 0; i < sbCount; ++i) {
        std::int32_t gi;
        std::uint64_t bestMse = (std::uint64_t)1 << 63;
        // Find best mse among already selected options.
        for (gi = 0; gi < nbStrengths; ++gi) {
            std::uint64_t curr = mse[0][i][lev0[gi]];
            curr += mse[1][i][lev1[gi]];
            if (curr < bestMse) {
                bestMse = curr;
            }
        }
        // Find best mse when adding each possible new option.
        for (j = startGi; j < totalStrengths; ++j) {
            for (k = startGi; k < totalStrengths; ++k) {
                std::uint64_t best = bestMse;
                std::uint64_t curr = mse[0][i][j];
                curr += mse[1][i][k];
                if (curr < best) {
                    best = curr;
                }
                totMse[j][k] += best;
            }
        }
    }
    // Add the best (lev0, lev1) pair to the selected list.
    for (j = startGi; j < totalStrengths; ++j) {
        for (k = startGi; k < totalStrengths; ++k) {
            if (totMse[j][k] < bestTotMse) {
                bestTotMse = totMse[j][k];
                bestId0 = j;
                bestId1 = k;
            }
        }
    }
    lev0[nbStrengths] = bestId0;
    lev1[nbStrengths] = bestId1;
    return bestTotMse;
}

// joint_strength_search_dual (enc_cdef.c:787-818): the greedy + refine
// search over the strength pairs. The list of nbStrengths pairs is
// determined by adding one such pair at a time through cdefSearchOneDual.
std::uint64_t cdefJointStrengthSearchDual(std::int32_t* bestLev0, std::int32_t* bestLev1, int nbStrengths,
                                          std::uint64_t** mse[2], int sbCount, int startGi, int endGi) {
    std::uint64_t bestTotMse = (std::uint64_t)1 << 63;
    std::int32_t i, j;
    for (i = 0; i < nbStrengths; ++i) {
        bestTotMse = cdefSearchOneDual(bestLev0, bestLev1, i, mse, sbCount, startGi, endGi);
    }
    for (i = 0; i < 4 * nbStrengths; ++i) {
        for (j = 0; j < nbStrengths - 1; ++j) {
            bestLev0[j] = bestLev0[j + 1];
            bestLev1[j] = bestLev1[j + 1];
        }
        bestTotMse = cdefSearchOneDual(bestLev0, bestLev1, nbStrengths - 1, mse, sbCount, startGi, endGi);
    }
    return bestTotMse;
}

} // namespace l10_filters
