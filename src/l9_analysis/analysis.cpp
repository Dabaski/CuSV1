#include "analysis.h"

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

}  // namespace analysis
