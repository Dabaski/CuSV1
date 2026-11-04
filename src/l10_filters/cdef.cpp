#include <cstdlib>

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

} // namespace l10_filters
