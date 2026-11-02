#include "analysis.h"

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

}  // namespace analysis
