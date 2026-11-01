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

}  // namespace analysis
