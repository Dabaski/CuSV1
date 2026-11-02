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

// 1:1 port of calculate_histogram (third_party/SVT-AV1/Source/Lib/Codec/
// pic_analysis_process.c:170-188). NO header declaration twin exists:
// grep-verified, the function is file-local to its .c (the only Codec
// occurrences are the :167 comment, the :170 definition and the :560
// caller). Mechanics: the outer loop steps vertical_index by decim_step
// but selects the row by POINTER advance (src += stride * decim_step);
// the inner loop steps the column by decim_step; the PIXEL VALUE is the
// bin index into the 256-bin uint32 histogram; sum accumulates the
// SAMPLED pixels only (the caller rescales by decim_step^2 at :569 and
// pre-initializes the bins to 1 at :545-547 - the function only ADDS).
void calculateHistogram(const std::uint8_t* src, std::uint32_t width,
                        std::uint32_t height, std::uint32_t stride,
                        std::uint8_t decimStep, std::uint32_t* histogram,
                        std::uint64_t* sum);

// 1:1 port of svt_aom_downsample_2d_c (third_party/SVT-AV1/Source/Lib/Codec/
// pic_analysis_process.c:134-164). Declaration twin: Codec/motion_estimation.h:33-35.
// 2x2, 0-phase decimation generic over decim_step: the input pointer is
// PRE-ADVANCED by half_decim_step rows (:148) and prev_input_line is one row
// above it (:151), so the summed 2x2 window sits at rows/cols
// {half-1, half} WITHIN each decim_step cell (the cell's top-left 2x2 at
// step 2, its middle 2x2 at step 4); each output is (sum + 2) >> 2 (:157).
// Live callers pass decim_step 2 (quarter, :1504-1510) and 4 (sixteenth,
// :1530-1536). Host only: the GPU twin is parked, not built.
void downsample2d(const std::uint8_t* src, std::uint32_t srcStride,
                  std::uint32_t areaWidth, std::uint32_t areaHeight,
                  std::uint8_t* dst, std::uint32_t dstStride,
                  std::uint32_t decimStep);

// The reference's block_mean_calc_prec selector: #define BLOCK_MEAN_PREC_FULL
// 0 / BLOCK_MEAN_PREC_SUB 1 (definitions.h:2043-2044). The live reference
// always selects Sub (Globals/enc_handle.c:4339, unconditional assignment);
// the Full branch is ported because the reference carries it.
enum class BlockMeanPrec : std::uint8_t { Full = 0, Sub = 1 };

// 1:1 port of compute_b64_variance (third_party/SVT-AV1/Source/Lib/Codec/
// pic_analysis_process.c:318-442) - static there, single caller :610; this
// is the RDO-input chain's capstone, composing the landed AN primitives:
// the Sub branch is sixteen intermVarFour8x8 calls (:347-361, blk_8x8_idx =
// 8*row + 4*colGroup, blk_offset +32 per call), the Full branch is 64
// mean8x8 + meanSquare8x8 pairs (:335-346); the 16x16 fold averages the
// four 8x8 blocks at 16R + 2C {+0,+1,+8,+9} >> 2 (:364-380), the 32x32
// fold the four 16x16 blocks at 8P + 2Q {+0,+1,+4,+5} >> 2 (:383-399) and
// the 64x64 fold the four 32x32 blocks >> 2 (:402-409) - each array level
// folds from the level below, nothing recomputes from pixels. Variance =
// (sq - mean*mean) >> 16 (:415-417/:423-426/:432-435/:439-441), cast
// uint16. Named deviations from the reference signature: scs/pcs/
// b64_idx/input_luma_origin_index are dropped - y points AT the block
// origin and sbVar is ONE flat variance row (the reference writes
// pcs->variance[b64_idx], max 209 entries, me_sb_results.h:22); the
// sbVar layout is the EbMeTierZeroPu bases (me_context.h:47/:48/:52/:68):
// 64x64 at 0, 32x32 at 1, 16x16 at 5, 8x8 at 21, and ONLY 0..84 are ever
// written (85..208 untouched). The intermediate arrays the reference keeps
// on its stack are exposed as out-params for the tests (deviation, named).
// subBlockVariances threads the :412 gate (scs->allintra || aq_mode == 1
// || variance_octile): when false only the 64x64 entry is written. Host
// only: the GPU twin is parked, not built.
void b64Variance(const std::uint8_t* y, std::uint16_t yStride, BlockMeanPrec prec,
                 bool subBlockVariances, std::uint16_t* sbVar,
                 std::uint64_t* meanOf16x16Blocks,
                 std::uint64_t* meanOf16x16SquaredValuesBlocks,
                 std::uint64_t* meanOf32x32Blocks,
                 std::uint64_t* meanOf32x32SquaredValuesBlocks,
                 std::uint64_t* meanOf64x64Block,
                 std::uint64_t* meanOf64x64SquaredValuesBlock);

}  // namespace analysis
