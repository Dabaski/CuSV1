#pragma once

#include <cstdint>

namespace analysis {

// 1:1 port of svt_compute_mean_c (third_party/SVT-AV1/Source/Lib/Codec/
// pic_analysis_process.c:194-216) at 8x8, the per-size wrapper pattern of
// l5_motion. Declaration twin: Codec/compute_mean.h:21 (classified A12 in
// tools/coverage_areas.txt; both anchors cited). VARIANCE_PRECISION = 16
// (pic_analysis_process.c:36).
std::uint64_t mean8x8(const std::uint8_t* src, std::uint32_t srcStride);

// 1:1 port of svt_compute_mean_squared_values_c (third_party/SVT-AV1/Source/
// Lib/Codec/pic_analysis_process.c:220-240) at 8x8, the per-size wrapper
// pattern of l5_motion. Declaration twin: Codec/compute_mean.h:26. uint64
// accumulation of src[hi] * src[hi] per pixel, then
// (sumSq << VARIANCE_PRECISION) / (width * height) - shift first, truncating
// division, no rounding term (pic_analysis_process.c:237).
std::uint64_t meanSquare8x8(const std::uint8_t* src, std::uint32_t srcStride);

}  // namespace analysis
