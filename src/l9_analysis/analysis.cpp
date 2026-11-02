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

}  // namespace analysis
