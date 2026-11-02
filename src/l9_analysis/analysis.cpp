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
