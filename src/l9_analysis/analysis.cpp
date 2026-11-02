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

}  // namespace analysis
