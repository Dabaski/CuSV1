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

// 1:1 port of svt_compute_sub_mean_8x8_c (third_party/SVT-AV1/Source/Lib/
// Codec/pic_analysis_process.c:242-261), the per-size wrapper pattern of
// l5_motion. Declaration twin: Codec/compute_mean.h:31. Sums the full
// 8-wide rows {0,2,4,6} (the skip = vi + vi loop, 32 px), then sum << 3
// with NO division (pic_analysis_process.c:257) - the commented-out
// formula (sum << (VARIANCE_PRECISION >> 1)) / (w*h/2) = (sum << 8) / 32
// equals sum << 3 exactly because 32 = 2^5 divides 2^8.
std::uint64_t subMean8x8(const std::uint8_t* src, std::uint16_t srcStride);

// 1:1 port of svt_aom_compute_sub_mean_squared_values_c
// (pic_analysis_process.c:263-284). Declaration twin: compute_mean.h:34.
// Generic over width/height: the skip = vi + vi loop sums
// src[hi] * src[hi] over ceil(height/2) rows (skip < input_area_height),
// then sumSq << 11 with NO division (:281) = (sumSq << VARIANCE_PRECISION)
// / (w*h/2) at (8,8) - the shift is HARDWIRED 11 for every (w,h),
// matching the code, not the comment.
std::uint64_t subMeanSquareValues(const std::uint8_t* src, std::uint32_t srcStride,
                                  std::uint32_t width, std::uint32_t height);

// 1:1 port of svt_compute_interm_var_four8x8_c (third_party/SVT-AV1/Source/
// Lib/Codec/pic_analysis_process.c:286-313). Declaration twin:
// Codec/compute_mean.h:40. COMPOSES the AN3 primitives - the reference
// calls its own sub-mean pair, it does not recompute: four pairs of
// subMean8x8 + subMeanSquareValues(src, stride, 8, 8) at blockIndex = 0,
// 8, 16, 24 accumulated +8, i.e. four side-by-side 8x8 blocks along ONE
// row origin, all reading rows {0,2,4,6}. The reference's own
// "(0,1)..(0,4)" comments are meaningless labels: the live caller
// (pic_analysis_process.c:350-359) passes the frame-strided buffer and
// advances by 32 per call.
void intermVarFour8x8(const std::uint8_t* src, std::uint16_t srcStride,
                      std::uint64_t* meanOf8x8Blocks,
                      std::uint64_t* meanOfSquared8x8Blocks);

}  // namespace analysis
