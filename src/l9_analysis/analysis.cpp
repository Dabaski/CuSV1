#include "analysis.h"

#include "temporal_filter.h"

#include <cassert>
#include <cstring>

namespace analysis {

// pic_analysis_process.c:36
constexpr std::uint32_t kVariancePrecision = 16;

// 1:1 port of svt_compute_mean_c (pic_analysis_process.c:194-216) at 8x8:
// uint64 accumulation of every pixel, row-by-row with the stride advance,
// then (sum << (VARIANCE_PRECISION >> 1)) / (width * height) - shift first,
// truncating division, no rounding term (:211).
std::uint64_t mean8x8(const std::uint8_t* src, std::uint32_t srcStride) {
    std::uint64_t blockMean = 0;

    for (std::uint32_t vi = 0; vi < 8; ++vi) {
        for (std::uint32_t hi = 0; hi < 8; ++hi) {
            blockMean += src[hi];
        }
        src += srcStride;
    }

    blockMean = (blockMean << (kVariancePrecision >> 1)) / (8 * 8);

    return blockMean;
}

// 1:1 port of svt_compute_mean_squared_values_c (pic_analysis_process.c:220-240)
// at 8x8: uint64 accumulation of every pixel square, row-by-row with the stride
// advance, then (sumSq << VARIANCE_PRECISION) / (width * height) - shift first,
// truncating division, no rounding term (:237). At 8x8 the division is exact
// for every sumSq (64 = 2^6 divides 2^16), so the port is sumSq * 1024; the
// shift-then-divide order is settled by the :237 citation, not by an 8x8 test.
std::uint64_t meanSquare8x8(const std::uint8_t* src, std::uint32_t srcStride) {
    std::uint64_t blockMean = 0;

    for (std::uint32_t vi = 0; vi < 8; ++vi) {
        for (std::uint32_t hi = 0; hi < 8; ++hi) {
            blockMean += src[hi] * src[hi];
        }
        src += srcStride;
    }

    blockMean = (blockMean << kVariancePrecision) / (8 * 8);

    return blockMean;
}

// 1:1 port of svt_compute_sub_mean_8x8_c (pic_analysis_process.c:242-261):
// the skip = vi + vi loop ASSIGNS (never increments) skip in the for-update,
// so the summed rows are {0,2,4,6} - 32 px of the full 8-wide row - then
// blockMean << 3 with NO division (:257; the commented-out
// (sum << (VARIANCE_PRECISION >> 1)) / (w*h/2) formula equals sum << 3
// exactly because 32 = 2^5 divides 2^8).
std::uint64_t subMean8x8(const std::uint8_t* src, std::uint16_t srcStride) {
    std::uint64_t blockMean = 0;
    std::uint32_t vi = 0;
    std::uint16_t skip = 0;

    for (; skip < 8; skip = static_cast<std::uint16_t>(vi + vi)) {
        for (std::uint32_t hi = 0; hi < 8; ++hi) {
            blockMean += src[hi];
        }
        src += 2 * srcStride;
        ++vi;
    }

    blockMean = blockMean << 3;

    return blockMean;
}

// 1:1 port of svt_aom_compute_sub_mean_squared_values_c
// (pic_analysis_process.c:263-284): same skip mechanics gated on
// input_area_height - rows {0,2,4,...} while skip < height, i.e.
// ceil(height/2) rows of squares - then blockMean << 11 with NO division
// (:281). The shift is hardwired 11 for every (w,h): the code, not the
// comment (the comment drops the /2). Only live reference caller: (8,8)
// from svt_compute_interm_var_four8x8_c (:293-294).
std::uint64_t subMeanSquareValues(const std::uint8_t* src, std::uint32_t srcStride,
                                  std::uint32_t width, std::uint32_t height) {
    std::uint64_t blockMean = 0;
    std::uint32_t vi = 0;
    std::uint16_t skip = 0;

    for (; skip < height; skip = static_cast<std::uint16_t>(vi + vi)) {
        for (std::uint32_t hi = 0; hi < width; ++hi) {
            blockMean += src[hi] * src[hi];
        }
        src += 2 * srcStride;
        ++vi;
    }

    blockMean = blockMean << 11;

    return blockMean;
}

// 1:1 port of svt_compute_interm_var_four8x8_c (pic_analysis_process.c:286-313):
// the reference CALLS its own sub-mean primitives at block_index = 0, 8, 16,
// 24 - four side-by-side 8x8 blocks along one row origin, all reading rows
// {0,2,4,6} - so the port composes subMean8x8 + subMeanSquareValues
// identically. The unrolled shape, the uint32 blockIndex accumulated +8 and
// the uint16 stride pass-through are the reference's structure, kept
// verbatim (the "(0,1)..(0,4)" labels are the reference's own comments).
void intermVarFour8x8(const std::uint8_t* src, std::uint16_t srcStride,
                      std::uint64_t* meanOf8x8Blocks,
                      std::uint64_t* meanOfSquared8x8Blocks) {
    std::uint32_t blockIndex = 0;
    // (0,1)
    meanOf8x8Blocks[0] = subMean8x8(src + blockIndex, srcStride);
    meanOfSquared8x8Blocks[0] = subMeanSquareValues(src + blockIndex, srcStride, 8, 8);
    // (0,2)
    blockIndex = blockIndex + 8;
    meanOf8x8Blocks[1] = subMean8x8(src + blockIndex, srcStride);
    meanOfSquared8x8Blocks[1] = subMeanSquareValues(src + blockIndex, srcStride, 8, 8);
    // (0,3)
    blockIndex = blockIndex + 8;
    meanOf8x8Blocks[2] = subMean8x8(src + blockIndex, srcStride);
    meanOfSquared8x8Blocks[2] = subMeanSquareValues(src + blockIndex, srcStride, 8, 8);
    // (0,4)
    blockIndex = blockIndex + 8;
    meanOf8x8Blocks[3] = subMean8x8(src + blockIndex, srcStride);
    meanOfSquared8x8Blocks[3] = subMeanSquareValues(src + blockIndex, srcStride, 8, 8);
}

// 1:1 port of calculate_histogram (pic_analysis_process.c:170-188): the
// PIXEL VALUE is the bin index; the outer loop steps its counter by
// decim_step but selects the row by POINTER advance
// (src += stride * decim_step, :184), the inner loop steps the column by
// decim_step; sum accumulates the SAMPLED pixels only (:182) - the caller
// rescales by decim_step^2 (:569) and pre-initializes the bins to 1
// (:545-547), so the port only ADDS on top. Mirrors the reference's
// two-read shape (the ++histogram line and the sum line each index
// src[hi]) rather than caching a local.
void calculateHistogram(const std::uint8_t* src, std::uint32_t width,
                        std::uint32_t height, std::uint32_t stride,
                        std::uint8_t decimStep, std::uint32_t* histogram,
                        std::uint64_t* sum) {
    for (std::uint32_t vi = 0; vi < height; vi += decimStep) {
        for (std::uint32_t hi = 0; hi < width; hi += decimStep) {
            ++(histogram[src[hi]]);
            *sum += src[hi];
        }
        src += stride * decimStep;
    }
}

// 1:1 port of svt_av1_count_colors_with_threshold (pic_analysis_process.c:
// 911-930). The flag array, the per-pixel check and the early-exit false at
// count > threshold are the reference's own; *num_colors is written even on
// the early-exit path (threshold + 1 there).
bool countColorsWithThreshold(const std::uint8_t* src, int stride, int rows,
                              int cols, int numColorsThreshold, int* numColors) {
    bool hasColor[1 << 8] = {false};
    *numColors = 0;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int thisVal = src[r * stride + c];
            if (!hasColor[thisVal]) {
                hasColor[thisVal] = true;
                (*numColors)++;
                if (*numColors > numColorsThreshold) {
                    return false;
                }
            }
        }
    }
    return true;
}

// 1:1 port of is_valid_palette_nb_colors (pic_analysis_process.c:956-978).
// The scan and both exits are the reference's own: the early exit at
// count > threshold and the nb_colors <= 1 palette gate (:973-975).
bool isValidPaletteNbColors(const std::uint8_t* src, int stride, int rows,
                            int cols, int nbColorsThreshold) {
    bool hasColor[1 << 8];
    memset(hasColor, 0, (1 << 8) * sizeof(*hasColor));
    int nbColors = 0;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int thisVal = src[r * stride + c];
            if (hasColor[thisVal] == 0) {
                hasColor[thisVal] = 1;
                nbColors++;
                if (nbColors > nbColorsThreshold) {
                    return false;
                }
            }
        }
    }
    if (nbColors <= 1) {
        return false;
    }

    return true;
}

// 1:1 port of svt_av1_find_dominant_value (pic_analysis_process.c:984-1003):
// 256-bin pixel-value histogram, the dominant value updated on a STRICTLY
// greater count so the first maximum in scan order wins ties.
std::uint8_t findDominantValue(const std::uint8_t* src, int stride, int rows, int cols) {
    std::uint32_t valueCount[1 << 8] = {0};
    std::uint32_t dominantValueCount = 0;
    std::uint8_t dominantValue = 0;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const std::uint8_t value = src[r * (ptrdiff_t)stride + c];

            valueCount[value]++;

            if (valueCount[value] > dominantValueCount) {
                dominantValue = value;
                dominantValueCount = valueCount[value];
            }
        }
    }

    return dominantValue;
}

// 1:1 port of svt_aom_dilate_block (pic_analysis_process.c:1024-1076): the
// full copy first, then one pass writing the dominant value into the eight
// neighbours of every dominant pixel, each direction bound-gated. No
// chaining: the source is read, never the dilated buffer.
void dilateBlock(const std::uint8_t* src, int srcStride, std::uint8_t* dilated,
                 int dilatedStride, int rows, int cols) {
    const std::uint8_t dominantValue = findDominantValue(src, srcStride, rows, cols);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const std::uint8_t value = src[r * (ptrdiff_t)srcStride + c];

            dilated[r * (ptrdiff_t)dilatedStride + c] = value;
        }
    }

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const std::uint8_t value = src[r * (ptrdiff_t)srcStride + c];

            if (value == dominantValue) {
                // Dilate up
                if (r != 0) {
                    dilated[(r - 1) * (ptrdiff_t)dilatedStride + c] = value;
                }
                // Dilate down
                if (r != rows - 1) {
                    dilated[(r + 1) * (ptrdiff_t)dilatedStride + c] = value;
                }
                // Dilate left
                if (c != 0) {
                    dilated[r * (ptrdiff_t)dilatedStride + (c - 1)] = value;
                }
                // Dilate right
                if (c != cols - 1) {
                    dilated[r * (ptrdiff_t)dilatedStride + (c + 1)] = value;
                }
                // Dilate upper-left corner
                if (r != 0 && c != 0) {
                    dilated[(r - 1) * (ptrdiff_t)dilatedStride + (c - 1)] = value;
                }
                // Dilate upper-right corner
                if (r != 0 && c != cols - 1) {
                    dilated[(r - 1) * (ptrdiff_t)dilatedStride + (c + 1)] = value;
                }
                // Dilate lower-left corner
                if (r != rows - 1 && c != 0) {
                    dilated[(r + 1) * (ptrdiff_t)dilatedStride + (c - 1)] = value;
                }
                // Dilate lower-right corner
                if (r != rows - 1 && c != cols - 1) {
                    dilated[(r + 1) * (ptrdiff_t)dilatedStride + (c + 1)] = value;
                }
            }
        }
    }
}

// PU1: the missing VAR(W,H) macro instances of C_DEFAULT/variance.c in the
// GENERAL two-buffer shape (the reference macro :184-190; the AN8
// variance16x16/8x8 above are the offs-folded single-buffer shape). Each
// instance: varianceCore(a, aStride, b, bStride, W, H, sse, &sum), then
// *sse - (uint32_t)(((int64_t)sum * sum) / (W * H)) (:189, truncating).
// The 20 sizes below complete the VARIANCES(W, H) enumeration
// (:208-:229) - the 16x16 and 8x8 instances already landed with AN8.

// 1:1 port of the VAR(4, 4) instance svt_aom_variance4x4_c (:222).
std::uint32_t variance4x4(const std::uint8_t* a, int aStride,
                          const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 4, 4, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (4 * 4));
}

// The remaining VARIANCES(W, H) instances, in the reference's own order
// (:208-:229). The 16x16 and 8x8 shapes live in the offs-folded form above.
std::uint32_t variance128x128(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 128, 128, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (128 * 128));
}

std::uint32_t variance128x64(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 128, 64, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (128 * 64));
}

std::uint32_t variance64x128(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 64, 128, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (64 * 128));
}

std::uint32_t variance64x64(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 64, 64, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (64 * 64));
}

std::uint32_t variance64x32(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 64, 32, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (64 * 32));
}

std::uint32_t variance32x64(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 32, 64, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (32 * 64));
}

std::uint32_t variance32x32(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 32, 32, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (32 * 32));
}

std::uint32_t variance32x16(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 32, 16, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (32 * 16));
}

std::uint32_t variance16x32(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 16, 32, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (16 * 32));
}

std::uint32_t variance16x8(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 16, 8, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (16 * 8));
}

std::uint32_t variance8x16(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 8, 16, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (8 * 16));
}

std::uint32_t variance8x4(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 8, 4, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (8 * 4));
}

std::uint32_t variance4x8(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 4, 8, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (4 * 8));
}

std::uint32_t variance4x16(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 4, 16, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (4 * 16));
}

std::uint32_t variance16x4(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 16, 4, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (16 * 4));
}

std::uint32_t variance8x32(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 8, 32, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (8 * 32));
}

std::uint32_t variance32x8(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 32, 8, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (32 * 8));
}

std::uint32_t variance16x64(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 16, 64, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (16 * 64));
}

std::uint32_t variance64x16(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse) {
    int sum;
    varianceCore(a, aStride, b, bStride, 64, 16, sse, &sum);
    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (64 * 16));
}

// 1:1 port of variance_c (third_party/SVT-AV1/Source/Lib/C_DEFAULT/
// variance.c:141-158) - static there, the core the VAR(W,H) macro drives;
// exported here for the tests (the AN8 statics precedent). Signed diff
// summed as int, squared as uint32, per row advanced by the strides.
void varianceCore(const std::uint8_t* a, int aStride, const std::uint8_t* b,
                  int bStride, int w, int h, std::uint32_t* sse, int* sum) {
    *sum = 0;
    *sse = 0;

    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            const int diff = a[j] - b[j];
            *sum += diff;
            *sse += static_cast<std::uint32_t>(diff * diff);
        }

        a += aStride;
        b += bStride;
    }
}

// 1:1 port of svt_aom_variance16x16_c (C_DEFAULT/variance.c:184-190 via
// variance_c :141-158) at ref_stride 0 over the constant-128 offs
// (pic_analysis_process.c:936-942, folded). The (int64_t) cast on sum*sum
// and the truncating division are the macro's own (:189).
std::uint32_t variance16x16(const std::uint8_t* src, int srcStride, std::uint32_t* sse) {
    int sum = 0;
    *sse = 0;

    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 16; ++j) {
            const int diff = src[j] - 128;
            sum += diff;
            *sse += static_cast<std::uint32_t>(diff * diff);
        }

        src += srcStride;
    }

    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (16 * 16));
}

// 1:1 port of svt_aom_variance8x8_c (C_DEFAULT/variance.c:184-190) at
// W*H = 64, same const-128 offs shape.
std::uint32_t variance8x8(const std::uint8_t* src, int srcStride, std::uint32_t* sse) {
    int sum = 0;
    *sse = 0;

    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            const int diff = src[j] - 128;
            sum += diff;
            *sse += static_cast<std::uint32_t>(diff * diff);
        }

        src += srcStride;
    }

    return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (8 * 8));
}

// 1:1 ports of svt_av1_get_sby_perpixel_variance (pic_analysis_process.c:
// 944-952) at the two sizes the detectors use: the VAR kernel above, then
// ROUND_POWER_OF_TWO(var, 8) / ROUND_POWER_OF_TWO(var, 6)
// (definitions.h:457; eb_num_pels_log2_lookup common_utils.c:39).
std::uint32_t getSbyPerpixelVariance16x16(const std::uint8_t* src, int stride) {
    std::uint32_t sse = 0;
    const std::uint32_t var = variance16x16(src, stride, &sse);
    return (var + 128) >> 8;
}

std::uint32_t getSbyPerpixelVariance8x8(const std::uint8_t* src, int stride) {
    std::uint32_t sse = 0;
    const std::uint32_t var = variance8x8(src, stride, &sse);
    return (var + 32) >> 6;
}

// 1:1 port of svt_aom_is_screen_content (pic_analysis_process.c:1354-1429).
// Both passes, both thresholds, all five class formulas and the int
// arithmetic of the reference's comparisons are kept verbatim; the
// pcs->enhanced_pic indirection is threaded as the y/stride/dims params
// and the six sc_class fields become the returned struct (scClass5 stays
// zero-init: the reference never writes it here).
ScClasses isScreenContent(const std::uint8_t* y, std::uint32_t yStride,
                          std::uint32_t width, std::uint32_t height) {
    ScClasses classes = {};
    int blkW = 16;
    int blkH = 16;
    // These threshold values are selected experimentally. (:1357-1359)
    int colorThresh = 4;
    int varThresh = 0;
    // Counts of blocks with no more than color_thresh colors. (:1360-1361)
    int counts1 = 0;
    // Counts of blocks with no more than color_thresh colors and variance
    // larger than var_thresh. (:1362-1364)
    int counts2 = 0;

    for (int r = 0; r + blkH <= static_cast<int>(height); r += blkH) {
        for (int c = 0; c + blkW <= static_cast<int>(width); c += blkW) {
            {
                const std::uint8_t* src = y + r * static_cast<int>(yStride) + c;

                if (isValidPaletteNbColors(src, static_cast<int>(yStride), blkH, blkW, colorThresh)) {
                    ++counts1;
                    const int var = static_cast<int>(getSbyPerpixelVariance16x16(src, static_cast<int>(yStride)));
                    if (var > varThresh) {
                        ++counts2;
                    }
                }
            }
        }
    }

    // The threshold values are selected experimentally. (:1385)
    classes.scClass0 = counts1 * blkH * blkW * 10 > static_cast<int>(width * height);

    // IntraBC would force loop filters off, so we use more strict rules
    // that also requires that the block has high variance. (:1388-1389)
    classes.scClass1 = classes.scClass0 &&
        (counts2 * blkH * blkW * 12 > static_cast<int>(width * height));

    classes.scClass2 = classes.scClass1 ||
        (counts1 * blkH * blkW * 10 > static_cast<int>(width * height) * 4 &&
         counts2 * blkH * blkW * 30 > static_cast<int>(width * height));

    classes.scClass3 = classes.scClass1 ||
        (counts1 * blkH * blkW * 8 > static_cast<int>(width * height) &&
         counts2 * blkH * blkW * 50 > static_cast<int>(width * height));

    blkW = 8;
    blkH = 8;
    // These threshold values are selected experimentally. (:1402-1404)
    colorThresh = 4;
    varThresh = 16;
    counts1 = 0;
    counts2 = 0;
    for (int r = 0; r + blkH <= static_cast<int>(height); r += blkH) {
        for (int c = 0; c + blkW <= static_cast<int>(width); c += blkW) {
            {
                const std::uint8_t* src = y + r * static_cast<int>(yStride) + c;

                if (isValidPaletteNbColors(src, static_cast<int>(yStride), blkH, blkW, colorThresh)) {
                    ++counts1;
                    const int var = static_cast<int>(getSbyPerpixelVariance8x8(src, static_cast<int>(yStride)));
                    if (var > varThresh) {
                        ++counts2;
                    }
                }
            }
        }
    }
    classes.scClass4 = (counts1 * blkH * blkW * 18 > static_cast<int>(width * height)) &&
        (counts2 * blkH * blkW * 20 > static_cast<int>(width * height));

    return classes;
}

// 1:1 port of svt_aom_sc_AA_collect_counts (pic_analysis_process.c:1088-1190).
// The multiplier, the checkerboard skip, the per-block classify chain and
// the x2 fast counts are the reference's own, kept verbatim.
ScAaCounts scAaCollectCounts(const std::uint8_t* y, std::uint32_t yStride,
                             std::uint32_t width, std::uint32_t height,
                             int blkW, int blkH,
                             int complexInitialColorThresh, int simpleColorThresh,
                             int complexFinalColorThresh, int varThresh,
                             bool fastDetection, std::uint8_t* dilatedBlk) {
    ScAaCounts outputCounts = {};

    // Skip every other block and weigh each block twice as much when
    // performing fast detection (:1096-1098)
    const int multiplier = fastDetection ? 2 : 1;

    for (int r = 0; r + blkH <= static_cast<int>(height); r += blkH) {
        // Alternate skipping in a "checkerboard" pattern when performing
        // fast detection (:1101-1102)
        const int initialCol = (fastDetection && (r / blkH) % 2) ? blkW : 0;

        for (int c = initialCol; c + blkW <= static_cast<int>(width); c += blkW * multiplier) {
            // Identify quadrant for this block (2x2 regions) (:1105-1108)
            const int w2 = static_cast<int>(width) >> 1;
            const int h2 = static_cast<int>(height) >> 1;
            const int regionId = ((r >= h2) ? 2 : 0) + ((c >= w2) ? 1 : 0);

            const std::uint8_t* src = y + r * static_cast<int>(yStride) + c;

            int numberOfColors = 0;
            bool isPalette = false;
            bool isPhoto = false;
            bool isIntrabc = false;

            // First, find if the block could be palletized (:1117-1124)
            if (countColorsWithThreshold(src,
                                         static_cast<int>(yStride),
                                         blkH,
                                         blkW,
                                         complexInitialColorThresh,
                                         &numberOfColors) &&
                numberOfColors > 1) {
                if (numberOfColors <= simpleColorThresh) {
                    // Simple block detected, add to block count with no
                    // further processing required (:1125-1132)
                    isPalette = true;

                    const int var = static_cast<int>(blkH == 16
                        ? getSbyPerpixelVariance16x16(src, static_cast<int>(yStride))
                        : getSbyPerpixelVariance8x8(src, static_cast<int>(yStride)));
                    if (var > varThresh) {
                        isIntrabc = true;
                    }

                } else {
                    // Complex block detected, try to find if it's
                    // palettizable - dilate block with dominant color, to
                    // exclude anti-aliased pixels from final palette count
                    // (:1134-1142)
                    dilateBlock(src,
                                static_cast<int>(yStride),
                                dilatedBlk,
                                blkW,
                                blkH,
                                blkW);

                    if (countColorsWithThreshold(dilatedBlk,
                                                 blkW,
                                                 blkH,
                                                 blkW,
                                                 complexFinalColorThresh,
                                                 &numberOfColors)) {
                        const int var = static_cast<int>(blkH == 16
                            ? getSbyPerpixelVariance16x16(src, static_cast<int>(yStride))
                            : getSbyPerpixelVariance8x8(src, static_cast<int>(yStride)));
                        if (var > varThresh) {
                            isPalette = true;
                            isIntrabc = true;
                        }
                    }
                }
            } else {
                if (numberOfColors > complexInitialColorThresh) {
                    isPhoto = true;
                }
            }

            if (isPalette) {
                ++outputCounts.countPalette;
                ++outputCounts.regionPalette[regionId];
            }
            if (isIntrabc) {
                ++outputCounts.countIntrabc;
                ++outputCounts.regionIntrabc[regionId];
            }
            if (isPhoto) {
                ++outputCounts.countPhoto;
                ++outputCounts.regionPhoto[regionId];
            }
        }
    }
    if (fastDetection) {
        outputCounts.countPhoto *= multiplier;
        outputCounts.countPalette *= multiplier;
        outputCounts.countIntrabc *= multiplier;

        for (int i = 0; i < 4; ++i) {
            outputCounts.regionPhoto[i] *= multiplier;
            outputCounts.regionPalette[i] *= multiplier;
            outputCounts.regionIntrabc[i] *= multiplier;
        }
    }

    return outputCounts;
}

// 1:1 port of svt_aom_is_screen_content_antialiasing_aware
// (pic_analysis_process.c:1207-1350). The threshold constants, the two
// collect calls, the region pass loop and all six class formulas are the
// reference's own; blk_area16/8 are the :1208-1215 enum values. The DEBUG
//_AA_SCM stats-file block (:1236-1251, :1322-1349) is a debug-only sink,
// not ported.
ScClasses isScreenContentAaAware(const std::uint8_t* y, std::uint32_t yStride,
                                 std::uint32_t width, std::uint32_t height,
                                 bool fastDetection) {
    ScClasses classes = {};
    enum {
        blkW16 = 16,
        blkH16 = 16,
        blkArea16 = 16 * 16,
        blkW8 = 8,
        blkH8 = 8,
        blkArea8 = 8 * 8,
    };

    // These threshold values are selected experimentally. (:1218-1226)
    // Detects text and glyphs without anti-aliasing, and graphics with a
    // 4-color palette
    const int simpleColorThresh = 4;
    // Detects potential text and glyphs with anti-aliasing, and graphics
    // with a more extended color palette
    const int complexInitialColorThresh = 40;
    // Detects text and glyphs with anti-aliasing, and graphics with a more
    // extended color palette
    const int complexFinalColorThresh = 6;
    // Counts of blocks with no more than final_color_thresh colors
    const int varThresh = 5;
    // Counts of blocks that are candidates for using palette mode
    std::int64_t countPalette16 = 0;
    std::int64_t countPalette8 = 0;
    // Count of blocks that are candidates for using IntraBC than var_thresh
    std::int64_t countIntrabc16 = 0;
    std::int64_t countIntrabc8 = 0;
    // Count of "photo-like" blocks (i.e. can't use palette mode or IntraBC)
    std::int64_t countPhoto16 = 0;

    std::uint8_t dilatedBlk16[blkArea16];
    std::uint8_t dilatedBlk8[blkArea8];

    const std::int64_t area = static_cast<std::int64_t>(width) * height;

    const ScAaCounts counts16 = scAaCollectCounts(y,
                                                  yStride,
                                                  width,
                                                  height,
                                                  blkW16,
                                                  blkH16,
                                                  complexInitialColorThresh,
                                                  simpleColorThresh,
                                                  complexFinalColorThresh,
                                                  varThresh,
                                                  fastDetection,
                                                  dilatedBlk16);
    countPhoto16 = counts16.countPhoto;
    countPalette16 = counts16.countPalette;
    countIntrabc16 = counts16.countIntrabc;

    // SC_class4, SC_class5 (:1276-1278)
    const int complexFinalColorThresh8 = 8;
    const int varThresh8 = 50;

    const ScAaCounts counts8 = scAaCollectCounts(y,
                                                 yStride,
                                                 width,
                                                 height,
                                                 blkW8,
                                                 blkH8,
                                                 complexInitialColorThresh,
                                                 simpleColorThresh,
                                                 complexFinalColorThresh8,
                                                 varThresh8,
                                                 fastDetection,
                                                 dilatedBlk8);

    countPalette8 = counts8.countPalette;
    countIntrabc8 = counts8.countIntrabc;

    // The threshold values are selected experimentally. (:1295-1297)
    // Penalize presence of photo-like blocks (1/16th the weight of a
    // palettizable block)
    classes.scClass0 =
        ((countPalette16 - countPhoto16 / 16) * blkArea16 * 10 > area);

    // IntraBC would force loop filters off, so we use more strict rules
    // that also requires that the block has high variance. (:1299-1302)
    // Penalize presence of photo-like blocks (1/16th the weight of a
    // palettizable block)
    classes.scClass1 = classes.scClass0 &&
        ((countIntrabc16 - countPhoto16 / 16) * blkArea16 * 12 > area);

    classes.scClass2 = classes.scClass1 ||
        (countPalette16 * blkArea16 * 15 > area * 4 && countIntrabc16 * blkArea16 * 30 > area);

    classes.scClass3 = classes.scClass1 ||
        (countPalette16 * blkArea16 * 8 > area && countIntrabc16 * blkArea16 * 50 > area);

    const std::int64_t regionArea = area >> 2;  // area/4 for 2x2 regions
    int pass = 0;

    for (int i = 0; i < 4; ++i) {
        if ((counts8.regionPalette[i] * blkArea8 * 10 > regionArea) &&
            (counts8.regionIntrabc[i] * blkArea8 * 25 > regionArea)) {
            pass++;
        }
    }
    classes.scClass4 = (pass >= 3) && (countPalette8 * blkArea8 * 5 > area);
    classes.scClass5 = (pass >= 3) &&
        ((countPalette8 * blkArea8 * 10 > area) && (countIntrabc8 * blkArea8 * 23 > area));

    return classes;
}

// 1:1 port of svt_aom_generate_padding (pic_operators.c:266-301). The
// svt_memset/svt_memcpy substitutes are std::memset-style loops (named:
// svt_memset -> the byte-assignment loop, svt_memcpy -> the byte loop of
// equal length; both are plain byte ops here).
void generatePadding(std::uint8_t* srcPic, std::uint32_t srcStride,
                     std::uint32_t originalSrcWidth, std::uint32_t originalSrcHeight,
                     std::uint32_t paddingWidth, std::uint32_t paddingHeight) {
    if (!srcPic) {
        return;
    }

    const std::uint32_t rowBytes = srcStride;

    // Horizontal padding: extend each active row left and right (:277-286)
    std::uint8_t* row = srcPic;
    for (std::uint32_t y = 0; y < originalSrcHeight; ++y) {
        const std::uint8_t leftPixel = row[0];
        const std::uint8_t rightPixel = row[originalSrcWidth - 1];

        std::uint8_t* leftDst = row - paddingWidth;
        for (std::uint32_t i = 0; i < paddingWidth; ++i) {
            leftDst[i] = leftPixel;
        }
        for (std::uint32_t i = 0; i < paddingWidth; ++i) {
            row[originalSrcWidth + i] = rightPixel;
        }

        row += srcStride;
    }

    // Vertical padding: replicate the fully padded first and last rows
    // (:288-300), row_bytes = src_stride bytes per copy.
    std::uint8_t* const topSrcRow = srcPic - paddingWidth;
    std::uint8_t* const bottomSrcRow = srcPic - paddingWidth + (originalSrcHeight - 1) * srcStride;

    std::uint8_t* topDstRow = topSrcRow;
    std::uint8_t* bottomDstRow = bottomSrcRow;

    for (std::uint32_t y = 0; y < paddingHeight; ++y) {
        topDstRow -= srcStride;
        bottomDstRow += srcStride;

        for (std::uint32_t i = 0; i < rowBytes; ++i) {
            topDstRow[i] = topSrcRow[i];
        }
        for (std::uint32_t i = 0; i < rowBytes; ++i) {
            bottomDstRow[i] = bottomSrcRow[i];
        }
    }
}

// 1:1 port of svt_aom_generate_padding_compressed_10bit
// (pic_operators.c:303-342). EB_MEMSET -> the byte-assignment loop (named
// substitution); svt_memcpy -> the byte loop.
void generatePaddingCompressed10bit(std::uint8_t* srcPic, std::uint32_t srcStride,
                                    std::uint32_t originalSrcWidth, std::uint32_t originalSrcHeight,
                                    std::uint32_t paddingWidth, std::uint32_t paddingHeight) {
    if (!srcPic) {
        return;
    }

    std::uint8_t* row = srcPic;

    for (std::uint32_t r = 0; r < originalSrcHeight; ++r) {
        const std::uint8_t leftPixel = (row[0] >> 6) & 0x03;
        const std::uint8_t rightPixel = row[originalSrcWidth / 4 - 1] & 0x03;

        std::uint8_t* leftDst = row - paddingWidth / 4;
        for (std::uint32_t i = 0; i < paddingWidth / 4; ++i) {
            leftDst[i] = static_cast<std::uint8_t>(leftPixel * 0b01010101);
        }
        std::uint8_t* rightDst = row + originalSrcWidth / 4;
        for (std::uint32_t i = 0; i < paddingWidth / 4; ++i) {
            rightDst[i] = static_cast<std::uint8_t>(rightPixel * 0b01010101);
        }

        row += srcStride;
    }

    // vertical padding (:335-341)
    std::uint8_t* topSrcRow = srcPic - (paddingWidth / 4);
    std::uint8_t* bottomSrcRow = srcPic - (paddingWidth / 4) + (originalSrcHeight - 1) * srcStride;
    std::uint8_t* topDstRow = topSrcRow;
    std::uint8_t* bottomDstRow = bottomSrcRow;

    std::uint32_t verticalIdx = paddingHeight;
    while (verticalIdx) {
        // top part data copy
        topDstRow -= srcStride;
        for (std::uint32_t i = 0; i < srcStride; ++i) {
            topDstRow[i] = topSrcRow[i];
        }
        // bottom part data copy
        bottomDstRow += srcStride;
        for (std::uint32_t i = 0; i < srcStride; ++i) {
            bottomDstRow[i] = bottomSrcRow[i];
        }
        --verticalIdx;
    }
}

// 1:1 port of pad_input_picture (pic_operators.c:393-436). svt_memset ->
// the byte-assignment loop, svt_memcpy -> the byte loop (named
// substitutions).
void padInputPicture(std::uint8_t* srcPic, std::uint32_t srcStride,
                     std::uint32_t originalSrcWidth, std::uint32_t originalSrcHeight,
                     std::uint32_t padRight, std::uint32_t padBottom) {
    if (!srcPic) {
        return;
    }

    if (padRight) {
        // Add padding @ the right (:409-419)
        std::uint32_t verticalIdx = originalSrcHeight;
        std::uint8_t* row = srcPic;

        while (verticalIdx) {
            const std::uint8_t lastPixel = row[originalSrcWidth - 1];
            for (std::uint32_t i = 0; i < padRight; ++i) {
                row[originalSrcWidth + i] = lastPixel;
            }
            row += srcStride;
            --verticalIdx;
        }
    }

    if (padBottom) {
        // Add padding @ the bottom (:420-435)
        std::uint32_t verticalIdx = padBottom;
        std::uint8_t* lastRow = srcPic + (originalSrcHeight - 1) * srcStride;
        std::uint8_t* dstRow = lastRow;

        while (verticalIdx) {
            dstRow += srcStride;
            for (std::uint32_t i = 0; i < originalSrcWidth + padRight; ++i) {
                dstRow[i] = lastRow[i];
            }
            --verticalIdx;
        }
    }
}

// 1:1 port of svt_aom_pad_input_picture_16bit (pic_operators.c:441-479).
// memset16bit (pic_operators.h:39-45) -> the same element loop (named
// substitution); svt_memcpy -> the element loop.
void padInputPicture16bit(std::uint16_t* srcPic, std::uint32_t srcStride,
                          std::uint32_t originalSrcWidth, std::uint32_t originalSrcHeight,
                          std::uint32_t padRight, std::uint32_t padBottom) {
    if (padRight) {
        // Add padding @ the right (:453-463)
        std::uint32_t verticalIdx = originalSrcHeight;
        std::uint16_t* row = srcPic;

        while (verticalIdx) {
            const std::uint16_t lastPixel = row[originalSrcWidth - 1];
            for (std::uint32_t i = 0; i < padRight; ++i) {
                row[originalSrcWidth + i] = lastPixel;
            }
            row += srcStride;
            --verticalIdx;
        }
    }

    if (padBottom) {
        // Add padding @ the bottom (:464-478)
        std::uint32_t verticalIdx = padBottom;
        std::uint16_t* lastRow = srcPic + (originalSrcHeight - 1) * srcStride;
        std::uint16_t* dstRow = lastRow;

        while (verticalIdx) {
            dstRow += srcStride;
            for (std::uint32_t i = 0; i < originalSrcWidth + padRight; ++i) {
                dstRow[i] = lastRow[i];
            }
            --verticalIdx;
        }
    }
}

// 1:1 port of pad_2b_compressed_input_picture (pic_analysis_process.c:
// 648-739). svt_memcpy -> the byte loop (named substitution).
void pad2bCompressedInputPicture(std::uint8_t* srcPic, std::uint32_t srcStride,
                                 std::uint32_t originalSrcWidth, std::uint32_t originalSrcHeight,
                                 std::uint32_t padRight, std::uint32_t padBottom) {
    // The bottom pass lands with its own test slice; padBottom is consumed
    // there (the reference's :731-738 block).
    (void)padBottom;
    if (padRight > 0) {
        const std::uint32_t wM4 = (originalSrcWidth / 4) * 4;
        const std::uint32_t lastCol = wM4 / 4;

        if (padRight == 1) {
            for (std::uint32_t row = 0; row < originalSrcHeight; ++row) {
                std::uint8_t lastByte = srcPic[lastCol + row * srcStride];
                lastByte &= 0xfc;
                const std::uint8_t lastPixel = (lastByte >> 2) & 0x03;

                const std::uint8_t newByte = static_cast<std::uint8_t>(lastByte | lastPixel);
                srcPic[lastCol + row * srcStride] = newByte;
            }
        } else if (padRight == 2) {
            for (std::uint32_t row = 0; row < originalSrcHeight; ++row) {
                std::uint8_t lastByte = srcPic[lastCol + row * srcStride];
                lastByte &= 0xf0;
                const std::uint8_t lastPixel = (lastByte >> 4) & 0x03;

                const std::uint8_t newByte =
                    static_cast<std::uint8_t>(lastByte | (lastPixel << 2) | lastPixel);
                srcPic[lastCol + row * srcStride] = newByte;
            }
        } else if (padRight == 3) {
            for (std::uint32_t row = 0; row < originalSrcHeight; ++row) {
                std::uint8_t lastByte = srcPic[lastCol + row * srcStride];
                lastByte &= 0xc0;
                const std::uint8_t lastPixel = (lastByte >> 6) & 0x03;

                const std::uint8_t newByte = static_cast<std::uint8_t>(
                    lastByte | (lastPixel << 4) | (lastPixel << 2) | lastPixel);
                srcPic[lastCol + row * srcStride] = newByte;
            }
        } else if (padRight == 4) {
            for (std::uint32_t row = 0; row < originalSrcHeight; ++row) {
                const std::uint8_t lastByte = srcPic[lastCol - 1 + row * srcStride];
                const std::uint8_t lastPixel = static_cast<std::uint8_t>(lastByte & 0x03);

                const std::uint8_t newByte = static_cast<std::uint8_t>(
                    (lastPixel << 6) | (lastPixel << 4) | (lastPixel << 2) | lastPixel);
                srcPic[lastCol + row * srcStride] = newByte;
            }
        } else if (padRight == 5) {
            for (std::uint32_t row = 0; row < originalSrcHeight; ++row) {
                std::uint8_t lastByte = srcPic[lastCol + row * srcStride];
                lastByte &= 0xfc;
                const std::uint8_t lastPixel = (lastByte >> 2) & 0x03;

                const std::uint8_t newByte = static_cast<std::uint8_t>(lastByte | lastPixel);
                srcPic[lastCol + row * srcStride] = newByte;

                const std::uint8_t newByte2 = static_cast<std::uint8_t>(
                    (lastPixel << 6) | (lastPixel << 4) | (lastPixel << 2) | lastPixel);
                srcPic[lastCol + 1 + row * srcStride] = newByte2;
            }
        } else if (padRight == 6) {
            for (std::uint32_t row = 0; row < originalSrcHeight; ++row) {
                std::uint8_t lastByte = srcPic[lastCol + row * srcStride];
                lastByte &= 0xf0;
                const std::uint8_t lastPixel = (lastByte >> 4) & 0x03;

                const std::uint8_t newByte =
                    static_cast<std::uint8_t>(lastByte | (lastPixel << 2) | lastPixel);
                srcPic[lastCol + row * srcStride] = newByte;

                const std::uint8_t newByte2 = static_cast<std::uint8_t>(
                    (lastPixel << 6) | (lastPixel << 4) | (lastPixel << 2) | lastPixel);
                srcPic[lastCol + 1 + row * srcStride] = newByte2;
            }
        } else if (padRight == 7) {
            for (std::uint32_t row = 0; row < originalSrcHeight; ++row) {
                std::uint8_t lastByte = srcPic[lastCol + row * srcStride];
                lastByte &= 0xc0;
                const std::uint8_t lastPixel = (lastByte >> 6) & 0x03;

                const std::uint8_t newByte = static_cast<std::uint8_t>(
                    lastByte | (lastPixel << 4) | (lastPixel << 2) | lastPixel);
                srcPic[lastCol + row * srcStride] = newByte;

                const std::uint8_t newByte2 = static_cast<std::uint8_t>(
                    (lastPixel << 6) | (lastPixel << 4) | (lastPixel << 2) | lastPixel);
                srcPic[lastCol + 1 + row * srcStride] = newByte2;
            }
        } else {
            assert(0 && "wrong pad value");
        }
    }

    if (padBottom) {
        std::uint8_t* lastRow = srcPic + (originalSrcHeight - 1) * srcStride;
        for (std::uint32_t row = 0; row < padBottom; ++row) {
            std::uint8_t* dstRow = lastRow + (row + 1) * srcStride;
            const std::uint32_t count = (originalSrcWidth + padRight) / 4;
            for (std::uint32_t i = 0; i < count; ++i) {
                dstRow[i] = lastRow[i];
            }
        }
    }
}

// 1:1 port of svt_aom_pad_picture_to_multiple_of_min_blk_size_dimensions
// (pic_analysis_process.c:745-813). The subsampling derivation is the
// reference's 8-bit form (:750-751); the comp strides are computed here, as
// in the reference (:780-782).
void padToMinBlkSizeDimensions(std::uint8_t* yBuffer, std::uint32_t yStride,
                               std::uint8_t* uBuffer, std::uint32_t uStride,
                               std::uint8_t* vBuffer, std::uint32_t vStride,
                               std::uint8_t* yBufferBitInc, std::uint8_t* uBufferBitInc,
                               std::uint8_t* vBufferBitInc,
                               std::uint32_t width, std::uint32_t height,
                               std::uint32_t padRight, std::uint32_t padBottom,
                               bool is16BitInput, ColorFormat colorFormat) {
    const std::uint16_t subsamplingX = (colorFormat == ColorFormat::Yuv444) ? 0 : 1;
    const std::uint16_t subsamplingY = (colorFormat >= ColorFormat::Yuv422) ? 0 : 1;

    // Input Picture Padding (:754-759)
    padInputPicture(yBuffer,
                    yStride,
                    width - padRight,
                    height - padBottom,
                    padRight,
                    padBottom);

    if (uBuffer) {
        padInputPicture(uBuffer,
                        uStride,
                        (width + subsamplingX - padRight) >> subsamplingX,
                        (height + subsamplingY - padBottom) >> subsamplingY,
                        padRight >> subsamplingX,
                        padBottom >> subsamplingY);
    }

    if (vBuffer) {
        padInputPicture(vBuffer,
                        vStride,
                        (width + subsamplingX - padRight) >> subsamplingX,
                        (height + subsamplingY - padBottom) >> subsamplingY,
                        padRight >> subsamplingX,
                        padBottom >> subsamplingY);
    }

    if (is16BitInput) {
        const std::uint32_t compStrideY = yStride / 4;
        const std::uint32_t compStrideUv = uStride / 4;

        if (yBufferBitInc) {
            pad2bCompressedInputPicture(yBufferBitInc,
                                        compStrideY,
                                        width - padRight,
                                        height - padBottom,
                                        padRight,
                                        padBottom);
        }

        if (uBufferBitInc) {
            pad2bCompressedInputPicture(uBufferBitInc,
                                        compStrideUv,
                                        (width + subsamplingX - padRight) >> subsamplingX,
                                        (height + subsamplingY - padBottom) >> subsamplingY,
                                        padRight >> subsamplingX,
                                        padBottom >> subsamplingY);
        }

        if (vBufferBitInc) {
            pad2bCompressedInputPicture(vBufferBitInc,
                                        compStrideUv,
                                        (width + subsamplingX - padRight) >> subsamplingX,
                                        (height + subsamplingY - padBottom) >> subsamplingY,
                                        padRight >> subsamplingX,
                                        padBottom >> subsamplingY);
        }
    }
}

// 1:1 port of svt_aom_pad_picture_to_multiple_of_min_blk_size_dimensions_16bit
// (pic_analysis_process.c:820-852). The subsampling derivation mirrors the
// 16-bit driver's own textual form (:825-826), NOT the 8-bit one - the two
// are provably equivalent for all four EbColorFormat members, and the port
// keeps the reference's divergence instead of unifying it.
void padToMinBlkSizeDimensions16bit(std::uint16_t* yBuffer, std::uint32_t yStride,
                                    std::uint16_t* uBuffer, std::uint32_t uStride,
                                    std::uint16_t* vBuffer, std::uint32_t vStride,
                                    std::uint32_t width, std::uint32_t height,
                                    std::uint32_t padRight, std::uint32_t padBottom,
                                    ColorFormat colorFormat) {
    const std::uint8_t subsamplingX = (colorFormat == ColorFormat::Yuv444) ? 0 : 1;
    const std::uint8_t subsamplingY =
        ((colorFormat == ColorFormat::Yuv444 || colorFormat == ColorFormat::Yuv422) ? 0 : 1);

    // Input Picture Padding (:829-834)
    padInputPicture16bit(yBuffer,
                         yStride,
                         width - padRight,
                         height - padBottom,
                         padRight,
                         padBottom);

    // Input Picture Padding for U and V planes (:836-851)
    padInputPicture16bit(uBuffer,
                         uStride,
                         (width + subsamplingX - padRight) >> subsamplingX,
                         (height + subsamplingY - padBottom) >> subsamplingY,
                         padRight >> subsamplingX,
                         padBottom >> subsamplingY);

    padInputPicture16bit(vBuffer,
                         vStride,
                         (width + subsamplingX - padRight) >> subsamplingX,
                         (height + subsamplingY - padBottom) >> subsamplingY,
                         padRight >> subsamplingX,
                         padBottom >> subsamplingY);
}

// 1:1 port of svt_aom_pad_picture_to_multiple_of_sb_dimensions
// (pic_analysis_process.c:858-866).
void padToSbDimensions(std::uint8_t* yBuffer, std::uint32_t yStride,
                       std::uint32_t width, std::uint32_t height,
                       std::uint32_t border) {
    // Generate Padding (:861-865)
    generatePadding(yBuffer,
                    yStride,
                    width,
                    height,
                    border,
                    border);
}

// 1:1 port of svt_aom_pad_input_pictures (pic_analysis_process.c:1549-1615).
// The order - edge pad, then border pad - is the contract the tests pin.
void padInputPictures(std::uint8_t* yBuffer, std::uint32_t yStride,
                      std::uint8_t* uBuffer, std::uint32_t uStride,
                      std::uint8_t* vBuffer, std::uint32_t vStride,
                      std::uint8_t* yBufferBitInc, std::uint8_t* uBufferBitInc,
                      std::uint8_t* vBufferBitInc,
                      std::uint32_t width, std::uint32_t height,
                      std::uint32_t padRight, std::uint32_t padBottom,
                      std::uint32_t border,
                      std::uint16_t subsamplingX, std::uint16_t subsamplingY,
                      bool is16BitInput, ColorFormat colorFormat) {
    // Pad pictures to multiple min cu size (:1551-1552). The reference
    // derives the min-blk pass's subsampling from input_pic->color_format,
    // NOT from the scs->subsampling_x/y the border passes read - two
    // different sources, threaded as such.
    padToMinBlkSizeDimensions(yBuffer, yStride, uBuffer, uStride, vBuffer, vStride,
                              yBufferBitInc, uBufferBitInc, vBufferBitInc,
                              width, height, padRight, padBottom, is16BitInput,
                              colorFormat);

    // Y samples (:1554-1559)
    generatePadding(yBuffer, yStride, width, height, border, border);

    const std::uint32_t compStrideY = yStride / 4;
    const std::uint32_t compStrideUv = uStride / 4;

    if (is16BitInput) {
        if (yBufferBitInc) {
            generatePaddingCompressed10bit(yBufferBitInc, compStrideY, width, height,
                                           border, border);
        }
    }

    // Safe to divide by 2 (input_pic->width >> scs->subsampling_x), with no
    // risk of off-of-one issues from chroma subsampling as picture is
    // already 8px aligned (:1575-1576)
    if (uBuffer) {
        generatePadding(uBuffer, uStride, width >> subsamplingX, height >> subsamplingY,
                        border >> subsamplingX, border >> subsamplingY);
    }

    if (vBuffer) {
        generatePadding(vBuffer, vStride, width >> subsamplingX, height >> subsamplingY,
                        border >> subsamplingX, border >> subsamplingY);
    }

    // PAD the bit inc buffer in 10bit (:1594-1614)
    if (is16BitInput) {
        if (uBufferBitInc) {
            generatePaddingCompressed10bit(uBufferBitInc, compStrideUv,
                                           width >> subsamplingX, height >> subsamplingY,
                                           border >> subsamplingX, border >> subsamplingY);
        }

        if (vBufferBitInc) {
            generatePaddingCompressed10bit(vBufferBitInc, compStrideUv,
                                           width >> subsamplingX, height >> subsamplingY,
                                           border >> subsamplingX, border >> subsamplingY);
        }
    }
}

// 1:1 port of svt_aom_is_input_luma_dominant (pic_analysis_process.c:
// 1440-1493) with the FRAME_LUMA_DOMINANT_* constants (:1431-1438). The
// sampling loop, the three per-sample counters, the uint32 chroma_mag_sq
// and the exact return expression are the reference's own; the null /
// EB_YUV400 guard is threaded as the monochrome flag (named deviation).
bool isInputLumaDominant(const std::uint8_t* u, const std::uint8_t* v,
                         std::uint32_t uStride, std::uint32_t vStride,
                         std::uint32_t width, std::uint32_t height,
                         bool monochrome) {
    if (monochrome) {
        return false;
    }

    const std::uint32_t uvW = width >> 1;
    const std::uint32_t uvH = height >> 1;

    if (!uvW || !uvH) {
        return false;
    }

    std::uint32_t sampleCnt = 0;
    std::uint32_t coreCnt = 0;
    std::uint32_t tailCnt = 0;
    std::uint32_t neutralCnt = 0;

    constexpr std::uint32_t sampleStep = 8;      // FRAME_LUMA_DOMINANT_SAMPLE_STEP
    constexpr std::uint32_t coreThrSq = 16 * 16; // FRAME_LUMA_DOMINANT_CORE_THR
    constexpr std::uint32_t tailThrSq = 18 * 18; // FRAME_LUMA_DOMINANT_TAIL_THR
    constexpr std::uint32_t minCorePct = 85;     // FRAME_LUMA_DOMINANT_MIN_CORE_PCT
    constexpr std::uint32_t minTailPct = 95;     // FRAME_LUMA_DOMINANT_MIN_TAIL_PCT
    constexpr std::uint32_t neutralThr = 6;      // FRAME_LUMA_DOMINANT_NEUTRAL_THR
    constexpr std::uint32_t uvDiffThr = 4;       // FRAME_LUMA_DOMINANT_UV_DIFF_THR
    constexpr std::uint32_t minNeutralPct = 75;  // FRAME_LUMA_DOMINANT_MIN_NEUTRAL_PCT

    for (std::uint32_t y = 0; y < uvH; y += sampleStep) {
        const std::uint8_t* ub = u + y * uStride;
        const std::uint8_t* vb = v + y * vStride;

        for (std::uint32_t x = 0; x < uvW; x += sampleStep) {
            const std::int32_t du = static_cast<std::int32_t>(ub[x]) - 128;
            const std::int32_t dv = static_cast<std::int32_t>(vb[x]) - 128;
            const std::int32_t uv = static_cast<std::int32_t>(ub[x]) - static_cast<std::int32_t>(vb[x]);
            const std::uint32_t chromaMagSq = static_cast<std::uint32_t>(du * du + dv * dv);
            const std::int32_t absDu = du < 0 ? -du : du;
            const std::int32_t absDv = dv < 0 ? -dv : dv;
            const std::int32_t absUv = uv < 0 ? -uv : uv;

            // Most samples must stay inside a tight near-neutral chroma
            // region, and almost all samples must stay inside a slightly
            // looser region. (:1473-1474)
            if (chromaMagSq <= coreThrSq) {
                coreCnt++;
            }
            if (chromaMagSq <= tailThrSq) {
                tailCnt++;
            }
            if (absDu <= static_cast<std::int32_t>(neutralThr) &&
                absDv <= static_cast<std::int32_t>(neutralThr) &&
                absUv <= static_cast<std::int32_t>(uvDiffThr)) {
                neutralCnt++;
            }

            sampleCnt++;
        }
    }

    return sampleCnt && coreCnt * 100 >= sampleCnt * minCorePct &&
        (tailCnt * 100 >= sampleCnt * minTailPct ||
         neutralCnt * 100 >= sampleCnt * minNeutralPct);
}

// 1:1 port of svt_aom_downsample_2d_c (pic_analysis_process.c:134-164).
// The reference's own pointer arithmetic is kept verbatim: the input
// pointer pre-advanced by half_decim_step rows (:148), prev_input_line
// derived per row as one stride above it (:151), the horizontal loop
// stepping both the input column by decim_step and the output index by 1
// (:152-153), the four-pixel sum and (sum + 2) >> 2 (:154-157), and the
// per-row advances input_samples += input_stripe_stride /
// decim_samples += decim_stride (:159-160). Const on the input is this
// port's only deviation (the reference takes uint8_t* and never writes it).
void downsample2d(const std::uint8_t* src, std::uint32_t srcStride,
                  std::uint32_t areaWidth, std::uint32_t areaHeight,
                  std::uint8_t* dst, std::uint32_t dstStride,
                  std::uint32_t decimStep) {
    const std::uint32_t inputStripeStride = srcStride * decimStep;
    const std::uint32_t halfDecimStep = decimStep >> 1;

    const std::uint8_t* curLine = src + halfDecimStep * srcStride;
    for (std::uint32_t vi = halfDecimStep; vi < areaHeight; vi += decimStep) {
        const std::uint8_t* prevLine = curLine - srcStride;
        std::uint32_t decimHorizontalIndex = 0;
        for (std::uint32_t hi = halfDecimStep; hi < areaWidth;
             hi += decimStep, ++decimHorizontalIndex) {
            const std::uint32_t sum = static_cast<std::uint32_t>(prevLine[hi - 1]) +
                static_cast<std::uint32_t>(prevLine[hi]) +
                static_cast<std::uint32_t>(curLine[hi - 1]) +
                static_cast<std::uint32_t>(curLine[hi]);
            dst[decimHorizontalIndex] = static_cast<std::uint8_t>((sum + 2) >> 2);
        }
        curLine += inputStripeStride;
        dst += dstStride;
    }
}

// 1:1 port of svt_aom_downsample_filtering_input_picture
// (pic_analysis_process.c:1498-1547). The contract comment lives on the
// declaration (analysis.h); the definition mirrors the reference's branch
// text: outer gate :1502, the level1 branch :1503-1517 (downsample_2d
// :1504-1510, generate_padding :1511-1516), the level0 branch :1519-1545
// (the level1-dependent source :1521-1537, generate_padding :1539-1544).
// resize.c:139 is a DECLARATION (semicolon at :141, no body) - this is the
// reference's only definition, ported once.
void downsampleFilteringInputPicture(
    const std::uint8_t* inputPaddedY, std::uint32_t inputPaddedYStride,
    std::uint32_t inputPaddedWidth, std::uint32_t inputPaddedHeight,
    std::uint8_t* quarterY, std::uint32_t quarterYStride,
    std::uint32_t quarterWidth, std::uint32_t quarterHeight,
    std::uint32_t quarterBorder,
    std::uint8_t* sixteenthY, std::uint32_t sixteenthYStride,
    std::uint32_t sixteenthWidth, std::uint32_t sixteenthHeight,
    std::uint32_t sixteenthBorder,
    bool hmeEnabled, bool tfHmeEnabled, bool level1Enabled,
    bool tfLevel1Enabled, bool level0Enabled, bool tfLevel0Enabled) {
    if (hmeEnabled || tfHmeEnabled) {
        if (level1Enabled || tfLevel1Enabled) {
            downsample2d(inputPaddedY, inputPaddedYStride, inputPaddedWidth,
                         inputPaddedHeight, quarterY, quarterYStride, 2);
            generatePadding(quarterY, quarterYStride, quarterWidth, quarterHeight,
                            quarterBorder, quarterBorder);
        }
        if (level0Enabled || tfLevel0Enabled) {
            if (level1Enabled || tfLevel1Enabled) {
                downsample2d(quarterY, quarterYStride, quarterWidth,
                             quarterHeight, sixteenthY, sixteenthYStride, 2);
            } else {
                downsample2d(inputPaddedY, inputPaddedYStride, inputPaddedWidth,
                             inputPaddedHeight, sixteenthY, sixteenthYStride, 4);
            }
            generatePadding(sixteenthY, sixteenthYStride, sixteenthWidth,
                            sixteenthHeight, sixteenthBorder, sixteenthBorder);
        }
    }
}

// 1:1 port of compute_b64_variance (pic_analysis_process.c:318-442). The
// reference's stack arrays become the out-params; the loops and index
// arithmetic are the reference's own, kept verbatim. The EbMeTierZeroPu
// bases are me_context.h:47/:48/:52/:68.
void b64Variance(const std::uint8_t* y, std::uint16_t yStride, BlockMeanPrec prec,
                 bool subBlockVariances, std::uint16_t* sbVar,
                 std::uint64_t* meanOf16x16Blocks,
                 std::uint64_t* meanOf16x16SquaredValuesBlocks,
                 std::uint64_t* meanOf32x32Blocks,
                 std::uint64_t* meanOf32x32SquaredValuesBlocks,
                 std::uint64_t* meanOf64x64Block,
                 std::uint64_t* meanOf64x64SquaredValuesBlock) {
    std::uint64_t meanOf8x8Blocks[64];
    std::uint64_t meanOf8x8SquaredValuesBlocks[64];

    if (prec == BlockMeanPrec::Full) {
        // :335-346 - every 8x8 with the AN1/AN2 primitives.
        for (int blk8x8Row = 0; blk8x8Row < 8; ++blk8x8Row) {
            int blkOffset = 8 * yStride * blk8x8Row;
            for (int blk8x8Col = 0; blk8x8Col < 8; ++blk8x8Col) {
                const int blk8x8Idx = 8 * blk8x8Row + blk8x8Col;
                meanOf8x8Blocks[blk8x8Idx] = mean8x8(y + blkOffset, yStride);
                meanOf8x8SquaredValuesBlocks[blk8x8Idx] = meanSquare8x8(y + blkOffset, yStride);
                blkOffset += 8;
            }
        }
    } else {
        // :347-361 - sixteen AN4 calls, four 8x8 blocks each, +32 per call.
        for (int blk8x8Row = 0; blk8x8Row < 8; ++blk8x8Row) {
            int blkOffset = 8 * yStride * blk8x8Row;
            for (int blk8x8Col = 0; blk8x8Col < 2; ++blk8x8Col) {
                const int blk8x8Idx = 8 * blk8x8Row + 4 * blk8x8Col;
                intermVarFour8x8(y + blkOffset, yStride,
                                 &meanOf8x8Blocks[blk8x8Idx],
                                 &meanOf8x8SquaredValuesBlocks[blk8x8Idx]);
                blkOffset += 32;  // 4 * 8x8 blocks
            }
        }
    }

    // 16x16 :364-380
    for (int blk16x16Row = 0; blk16x16Row < 4; ++blk16x16Row) {
        for (int blk16x16Col = 0; blk16x16Col < 4; ++blk16x16Col) {
            const int blk16x16Idx = 4 * blk16x16Row + blk16x16Col;
            const int first8x8BlkIdx = 16 * blk16x16Row + 2 * blk16x16Col;
            meanOf16x16Blocks[blk16x16Idx] = (meanOf8x8Blocks[first8x8BlkIdx] +
                                             meanOf8x8Blocks[first8x8BlkIdx + 1] +
                                             meanOf8x8Blocks[first8x8BlkIdx + 8] +
                                             meanOf8x8Blocks[first8x8BlkIdx + 9]) >>
                2;
            meanOf16x16SquaredValuesBlocks[blk16x16Idx] =
                (meanOf8x8SquaredValuesBlocks[first8x8BlkIdx] +
                 meanOf8x8SquaredValuesBlocks[first8x8BlkIdx + 1] +
                 meanOf8x8SquaredValuesBlocks[first8x8BlkIdx + 8] +
                 meanOf8x8SquaredValuesBlocks[first8x8BlkIdx + 9]) >>
                2;
        }
    }

    // 32x32 :383-399
    for (int blk32x32Row = 0; blk32x32Row < 2; ++blk32x32Row) {
        for (int blk32x32Col = 0; blk32x32Col < 2; ++blk32x32Col) {
            const int blk32x32Idx = 2 * blk32x32Row + blk32x32Col;
            const int first16x16BlkIdx = 8 * blk32x32Row + 2 * blk32x32Col;
            meanOf32x32Blocks[blk32x32Idx] = (meanOf16x16Blocks[first16x16BlkIdx] +
                                             meanOf16x16Blocks[first16x16BlkIdx + 1] +
                                             meanOf16x16Blocks[first16x16BlkIdx + 4] +
                                             meanOf16x16Blocks[first16x16BlkIdx + 5]) >>
                2;
            meanOf32x32SquaredValuesBlocks[blk32x32Idx] =
                (meanOf16x16SquaredValuesBlocks[first16x16BlkIdx] +
                 meanOf16x16SquaredValuesBlocks[first16x16BlkIdx + 1] +
                 meanOf16x16SquaredValuesBlocks[first16x16BlkIdx + 4] +
                 meanOf16x16SquaredValuesBlocks[first16x16BlkIdx + 5]) >>
                2;
        }
    }

    // 64x64 :402-409
    *meanOf64x64Block = (meanOf32x32Blocks[0] + meanOf32x32Blocks[1] +
                         meanOf32x32Blocks[2] + meanOf32x32Blocks[3]) >>
        2;
    *meanOf64x64SquaredValuesBlock =
        (meanOf32x32SquaredValuesBlocks[0] + meanOf32x32SquaredValuesBlocks[1] +
         meanOf32x32SquaredValuesBlocks[2] +
         meanOf32x32SquaredValuesBlocks[3]) >>
        2;

    // :412-437 - the scs->allintra || aq_mode == 1 || variance_octile gate,
    // threaded as subBlockVariances.
    if (subBlockVariances) {
        for (int blk8x8Idx = 0, mePuIdx = 21; blk8x8Idx < 64; ++blk8x8Idx, ++mePuIdx) {
            sbVar[mePuIdx] = static_cast<std::uint16_t>(
                (meanOf8x8SquaredValuesBlocks[blk8x8Idx] -
                 (meanOf8x8Blocks[blk8x8Idx] * meanOf8x8Blocks[blk8x8Idx])) >>
                kVariancePrecision);
        }
        for (int blk16x16Idx = 0, mePuIdx = 5; blk16x16Idx < 16; ++blk16x16Idx, ++mePuIdx) {
            sbVar[mePuIdx] = static_cast<std::uint16_t>(
                (meanOf16x16SquaredValuesBlocks[blk16x16Idx] -
                 (meanOf16x16Blocks[blk16x16Idx] * meanOf16x16Blocks[blk16x16Idx])) >>
                kVariancePrecision);
        }
        for (int blk32x32Idx = 0, mePuIdx = 1; blk32x32Idx < 4; ++blk32x32Idx, ++mePuIdx) {
            sbVar[mePuIdx] = static_cast<std::uint16_t>(
                (meanOf32x32SquaredValuesBlocks[blk32x32Idx] -
                 (meanOf32x32Blocks[blk32x32Idx] * meanOf32x32Blocks[blk32x32Idx])) >>
                kVariancePrecision);
        }
    }
    // :438-441 - the 64x64 entry, unconditional.
    sbVar[0] = static_cast<std::uint16_t>(
        (*meanOf64x64SquaredValuesBlock -
         (*meanOf64x64Block * *meanOf64x64Block)) >>
        kVariancePrecision);
}

// AM1: vmaf_get_spatial_amount (pic_analysis_process.c:1642-1652).
float vmafGetSpatialAmount(std::uint32_t avgMad) {
    if (avgMad < 2) {
        return 0.15f;
    } else if (avgMad < 5) {
        return 0.22f;
    } else if (avgMad < 12) {
        return 0.28f;
    } else {
        return 0.30f;
    }
}

// AM1: vmaf_get_qp_amount (pic_analysis_process.c:1654-1659).
float vmafGetQpAmount(std::uint32_t baseQp) {
    if (baseQp >= 35) {
        return 0.3f;
    }
    return 0.5f - (baseQp / 35.0f) * (0.5f - 0.3f);
}

// AM1: vmaf_get_coherence_factor (pic_analysis_process.c:1661-1669).
float vmafGetCoherenceFactor(float gcoh) {
    if (gcoh < 0.40f) {
        return 0.80f;
    } else if (gcoh < 0.60f) {
        return 0.9f;
    } else {
        return 1.0f;
    }
}

// AM1: vmaf_compute_combined_amount (pic_analysis_process.c:1671-1678).
float vmafComputeCombinedAmount(std::uint32_t baseQp, std::uint32_t avgMad, float gcoh) {
    float per_qp = vmafGetQpAmount(baseQp);
    float spatial = vmafGetSpatialAmount(avgMad);
    float coh_factor = vmafGetCoherenceFactor(gcoh);

    float combined_amount = (per_qp + spatial) / 2.0f;
    return combined_amount * coh_factor;
}

// AM1: vmaf_get_delta_clip (pic_analysis_process.c:1741-1754).
std::int32_t vmafGetDeltaClip(std::int32_t baseQp, int busy_frame) {
    std::int32_t qp_delta;
    if (baseQp <= 42) {
        qp_delta = 8;
    } else if (baseQp <= 51) {
        qp_delta = 9;
    } else if (baseQp <= 57) {
        qp_delta = 10;
    } else {
        qp_delta = 12;
    }

    return busy_frame ? qp_delta - 4 : qp_delta;
}

// AM1: vmaf_get_noise_gate (pic_analysis_process.c:1701-1726). The
// enhanced_pic wall threaded as the plane parameters (named deviation).
float vmafGetNoiseGate(const std::uint8_t* y, std::uint16_t width, std::uint16_t height,
                       std::uint16_t stride) {
    std::int32_t noise_fp16 = temporal::vmafEstimateNoiseFp16(y, width, height, stride);
    if (noise_fp16 < 0) {
        return 1.0f;
    }

    std::int32_t noise_log1p = temporal::noiseLog1pFp16(noise_fp16);

    const std::int32_t gate_start = 40000;
    const std::int32_t gate_end = 80000;
    const float gate_floor = 0.3f;

    if (noise_log1p <= gate_start) {
        return 1.0f;
    }
    if (noise_log1p >= gate_end) {
        return gate_floor;
    }

    float t = (float)(noise_log1p - gate_start) / (float)(gate_end - gate_start);
    return 1.0f - t * (1.0f - gate_floor);
}

// AM1: vmaf_unsharp_apply_frame (pic_analysis_process.c:1815-1821), the
// local row loop over the temporal:: row kernel.
void vmafUnsharpApplyFrameLocal(const std::uint8_t* src, const std::uint8_t* blur_plane, std::uint8_t* dst,
                                int width, int height, int stride, int sharp_amount,
                                std::int32_t delta_clip) {
    for (int y = 0; y < height; y++) {
        temporal::vmafApplyUnsharpRow(
            src + y * stride, blur_plane + y * width, dst + y * stride, width, sharp_amount, delta_clip);
    }
}

// AM1: vmaf_preprocess_frame (pic_analysis_process.c:1842-1900). The
// :1856-1881 buffer-management block is the pa_ctx wall and is NOT
// ported - the buffers arrive caller-owned.
void vmafPreprocessFrame(std::uint8_t* luma, int stride, int width, int height, int16_t* const hring[5],
                         std::uint8_t* blurPlane, std::uint32_t baseQp, int* sharpAmount,
                         std::int32_t* deltaClip) {
    /* Step 1: compute the per-frame sharpening amount, then gate it down on noisy frames. */
    std::uint32_t avg_mad = temporal::vmafComputeAvgMad(luma, width, height, stride);
    float gcoh = temporal::vmafComputeGradientCoherence(luma, width, height, stride);
    int sharp_amount = (int)(vmafComputeCombinedAmount(baseQp, avg_mad, gcoh) * 32768.0f);
    sharp_amount = (int)(sharp_amount * vmafGetNoiseGate(luma, (std::uint16_t)width, (std::uint16_t)height,
                                                         (std::uint16_t)stride));
    *sharpAmount = sharp_amount;

    /* Step 2: build the low-pass reference by box-blurring the luma plane. */
    temporal::vmafBoxBlurFrame(luma, stride, blurPlane, width, height, hring);

    /* Step 3: flag busy frames (under 85% flat pixels) and derive the per-pixel delta clip. */
    const std::int32_t flat_detail_thr = 12;
    const std::uint32_t pixel_count = (std::uint32_t)(width * height);
    const std::uint32_t flat_pixel_target = pixel_count * 85 / 100;
    const std::uint32_t flat_pixel_count = temporal::vmafCountDetailLe(
        luma, blurPlane, width, height, stride, flat_detail_thr);
    const int is_busy_frame = (flat_pixel_count < flat_pixel_target);

    const std::int32_t delta_clip = vmafGetDeltaClip((std::int32_t)baseQp, is_busy_frame);
    *deltaClip = delta_clip;

    /* Step 4: apply the unsharp mask in place, writing the sharpened luma back over the source. */
    vmafUnsharpApplyFrameLocal(luma, blurPlane, luma, width, height, stride, sharp_amount, delta_clip);
}

// AM1: sub_sample_luma_generate_pixel_intensity_histogram_bins
// (pic_analysis_process.c:527-589).
void subSampleLumaHistogramBins(std::uint32_t regionsPerWidth, std::uint32_t regionsPerHeight,
                                bool sceneChangeDetection, const std::uint8_t* y, std::uint32_t width,
                                std::uint32_t height, std::uint32_t yStride,
                                std::uint32_t* histograms[4][4], std::uint64_t avgIntensityPerRegion[4][4],
                                std::uint64_t* sumAvgIntensityTtlRegionsLuma) {
    *sumAvgIntensityTtlRegionsLuma = 0;
    std::uint32_t region_width = width / regionsPerWidth;
    std::uint32_t region_height = height / regionsPerHeight;

    // Loop over regions inside the picture
    for (std::uint32_t region_in_picture_width_index = 0;
         region_in_picture_width_index < regionsPerWidth;
         region_in_picture_width_index++) {  // loop over horizontal regions
        for (std::uint32_t region_in_picture_height_index = 0;
             region_in_picture_height_index < regionsPerHeight;
             region_in_picture_height_index++) {  // loop over vertical regions

            std::uint64_t sum = 0;

            // Initialize bins to 1 (svt_initialize_buffer_32bits(h, 64, 0, 1)
            // = 64*4 + 0 = HISTOGRAM_NUMBER_OF_BINS entries).
            std::uint32_t* hist = histograms[region_in_picture_width_index]
                                             [region_in_picture_height_index];
            for (std::uint32_t bin = 0; bin < 64 * 4; ++bin) {
                hist[bin] = 1;
            }

            std::uint32_t region_width_offset = (region_in_picture_width_index ==
                                                 regionsPerWidth - 1)
                ? width - (regionsPerWidth * region_width)
                : 0;

            std::uint32_t region_height_offset = (region_in_picture_height_index ==
                                                  regionsPerHeight - 1)
                ? height - (regionsPerHeight * region_height)
                : 0;
            std::uint8_t decim_step = sceneChangeDetection ? 1 : 4;
            // Y Histogram
            calculateHistogram(
                &y[(region_in_picture_width_index * region_width) +
                   ((region_in_picture_height_index * region_height) * yStride)],
                region_width + region_width_offset,
                region_height + region_height_offset,
                yStride,
                decim_step,
                histograms[region_in_picture_width_index][region_in_picture_height_index],
                &sum);
            sum = (sum * decim_step * decim_step);

            avgIntensityPerRegion[region_in_picture_width_index][region_in_picture_height_index] =
                (std::uint8_t)((sum +
                                (((region_width + region_width_offset) * (region_height + region_height_offset)) >> 1)) /
                               ((region_width + region_width_offset) * (region_height + region_height_offset)));
            *sumAvgIntensityTtlRegionsLuma += sum;
            for (std::uint32_t histogram_bin = 0; histogram_bin < 256 /*HISTOGRAM_NUMBER_OF_BINS*/;
                 histogram_bin++) {  // Loop over the histogram bins
                histograms[region_in_picture_width_index][region_in_picture_height_index][histogram_bin] =
                    histograms[region_in_picture_width_index][region_in_picture_height_index]
                                              [histogram_bin] *
                    4 * 4 * decim_step * decim_step;
            }
        }
    }

    *sumAvgIntensityTtlRegionsLuma /= (width * height);
}

// AM1: compute_picture_spatial_statistics (pic_analysis_process.c:597-617).
void computePictureSpatialStatistics(const std::uint8_t* y, std::uint16_t yStride, BlockMeanPrec prec,
                                     bool subBlockVariances, std::uint16_t b64TotalCount,
                                     const std::uint16_t* orgX, const std::uint16_t* orgY,
                                     std::uint16_t* sbVar, std::uint16_t* picAvgVariance) {
    // Variance
    std::uint64_t pic_tot_variance = 0;

    for (std::uint16_t b64_idx = 0; b64_idx < b64TotalCount; ++b64_idx) {
        std::uint16_t b64_origin_x = orgX[b64_idx];
        std::uint16_t b64_origin_y = orgY[b64_idx];
        std::uint32_t input_luma_origin_index = ((std::uint32_t)b64_origin_y) * yStride + b64_origin_x;

        // the fold arrays are compute_b64_variance internals in the
        // reference (its stack); the AN7 port exposes them as params.
        std::uint64_t meanOf16x16Blocks[16];
        std::uint64_t meanOf16x16SquaredValuesBlocks[16];
        std::uint64_t meanOf32x32Blocks[4];
        std::uint64_t meanOf32x32SquaredValuesBlocks[4];
        std::uint64_t meanOf64x64Block;
        std::uint64_t meanOf64x64SquaredValuesBlock;
        b64Variance(&y[input_luma_origin_index], yStride, prec, subBlockVariances,
                    sbVar + (std::size_t)b64_idx * kSbVarRowEntries, meanOf16x16Blocks,
                    meanOf16x16SquaredValuesBlocks, meanOf32x32Blocks, meanOf32x32SquaredValuesBlocks,
                    &meanOf64x64Block, &meanOf64x64SquaredValuesBlock);
        pic_tot_variance += (sbVar[(std::size_t)b64_idx * kSbVarRowEntries + 0 /*RASTER_SCAN_CU_INDEX_64x64*/]);
    }

    *picAvgVariance = (std::uint16_t)(pic_tot_variance / b64TotalCount);
}

// AM1: svt_aom_gathering_picture_statistics (pic_analysis_process.c:624-643).
void gatheringPictureStatistics(bool calcHist, bool calculateVariance,
                                std::uint32_t regionsPerWidth, std::uint32_t regionsPerHeight,
                                bool sceneChangeDetection, const std::uint8_t* histY, std::uint32_t histWidth,
                                std::uint32_t histHeight, std::uint32_t histStride,
                                std::uint32_t* histograms[4][4], std::uint64_t avgIntensityPerRegion[4][4],
                                const std::uint8_t* statY, std::uint16_t statStride, BlockMeanPrec prec,
                                bool subBlockVariances, std::uint16_t b64TotalCount, const std::uint16_t* orgX,
                                const std::uint16_t* orgY, std::uint16_t* sbVar, std::uint64_t* avgLuma,
                                std::uint16_t* picAvgVariance) {
    *avgLuma = 256 /*INVALID_LUMA, definitions.h:93*/;
    // Histogram bins
    if (calcHist) {
        // Use 1/16 Luma for Histogram generation
        // 1/16 input ready
        subSampleLumaHistogramBins(regionsPerWidth, regionsPerHeight, sceneChangeDetection, histY, histWidth,
                                   histHeight, histStride, histograms, avgIntensityPerRegion, avgLuma);
    }

    if (calculateVariance) {
        computePictureSpatialStatistics(statY, statStride, prec, subBlockVariances, b64TotalCount, orgX, orgY,
                                        sbVar, picAvgVariance);
    } else {
        *picAvgVariance = 0;
    }
}

// AN20: svt_av1_twopass_zero_stats (firstpass.c:133-139).
void twopassZeroStats(FirstpassStats* section) {
    section->frame = 0.0;
    section->codedError = 0.0;
    section->count = 0.0;
    section->duration = 1.0;
    std::memset(&section->statStruct, 0, sizeof(FirstpassStatStruct));
}

// AN20: svt_av1_accumulate_stats (firstpass.c:141-146).
void accumulateStats(FirstpassStats* section, const FirstpassStats* frame) {
    section->frame += frame->frame;
    section->codedError += frame->codedError;
    section->count += frame->count;
    section->duration += frame->duration;
}
}  // namespace analysis
