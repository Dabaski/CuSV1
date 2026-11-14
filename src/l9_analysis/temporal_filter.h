// l9_analysis - the temporal filtering kernel family (A11, TF1; the AN0
// Phase-C spans of temporal_filtering.c).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/temporal_filtering.c.
// LANDED: noiseLog1pFp16 (:568-586), the log1p/expf/sqrt tables
// (:339-565, :624-634, :637-664, :666-677), calculateDecayFactor
// (:589-607), calculateTfShiftFactor (:610-619),
// calculateSquaredErrorsSum (:697-707), the four 8-bit planewise
// medium kernels - zz partial/medium (:738-814) and non-zz
// partial/medium (:930-1087) - with the consumed MeContext fields
// threaded as parameters.
// PARKED (named): the highbd twins (:832-912 zz, :1105-1267 non-zz),
// the subpel searches (:1431-2102+), apply_filtering_block_plane_wise
// (:1289-1424), save_YUV_to_file (:87-152),
// derive_tf_32x32_block_split_flag (:152-205),
// create_me_context_and_picture_control (:205-262).
// LANDED (A11D-T1, 2026-11-14): apply_filtering_central (:262-298) and
// its highbd twin (:300-334) - the consumed MeContext field (tf_chroma)
// and the picture field (y_stride) are threaded as parameters.
#pragma once

#include <cstdint>

namespace analysis {
namespace temporal {

// A11D-T1: 1:1 port of svt_aom_apply_filtering_central_c
// (temporal_filtering.c:262-298). The consumed MeContext field
// (tf_chroma) and the picture field (y_stride) are threaded as
// parameters (the b64Variance carrier pattern); the src/accum/count
// pointer triples are the reference's EbByte*/uint32_t**/uint16_t**
// shapes (the reference's src is read-only, the port's const-ness is
// the named deviation).
void applyFilteringCentral(bool tfChroma, const uint8_t* const* src,
                           uint32_t** accum, uint16_t** count,
                           uint16_t blkWidth, uint16_t blkHeight,
                           uint32_t ssX, uint32_t ssY, uint16_t srcStrideY);

// A11D-T1b: 1:1 port of svt_aom_apply_filtering_central_highbd_c
// (temporal_filtering.c:300-334). The uint16_t src triple is the
// reference's uint16_t** shape; everything else identical to the
// 8-bit twin.
void applyFilteringCentralHbd(bool tfChroma, const uint16_t* const* src,
                              uint32_t** accum, uint16_t** count,
                              uint16_t blkWidth, uint16_t blkHeight,
                              uint32_t ssX, uint32_t ssY, uint16_t srcStrideY);

// 1:1 port of svt_aom_noise_log1p_fp16 (temporal_filtering.c:568-586).
int32_t noiseLog1pFp16(int32_t noiseLevelFp16);

// AM1: 1:1 port of svt_vmaf_hpass_row_c (temporal_filtering.c:3720-3733).
// Two cascaded delay-line box stages = the [1,4,6,4,1] kernel over the
// edge-clamped row; hRow holds width + 2*steps_x = width+4 entries and
// hRow[x + steps_x] is the running output at loop position x (the
// cascade's 4-step delay is why the vpass offsets by blur_start = 4).
void vmafHpassRow(const uint8_t* srcRow, int width, int16_t* hRow);

// AM1: 1:1 port of svt_vmaf_vpass_row_c (temporal_filtering.c:3660-3669).
// The vertical [1,4,6,4,1] over the five ring rows; j = x + 2*steps_x.
void vmafVpassRow(const int16_t* r0, const int16_t* r1, const int16_t* r2, const int16_t* r3,
                  const int16_t* r4, uint8_t* blurRow, int width, int stepsX);

// AM1: 1:1 port of vmaf_box_blur_frame (pic_analysis_process.c:1773-1799).
// Separable two-stage box blur (steps_x = steps_y = 2) over the ring
// buffers hring[5] (caller-owned, each width+4 int16): four primed rows
// k-steps_y clamped, then per output row the new m+steps_y row and the
// vpass, rotating the ring oldest-first.
void vmafBoxBlurFrame(const uint8_t* lumaPlane, int stride, uint8_t* blurPlane, int width, int height,
                      int16_t* const hring[5]);

// AM1: 1:1 port of svt_vmaf_apply_unsharp_row_c
// (temporal_filtering.c:3650-3658).
void vmafApplyUnsharpRow(const uint8_t* src, const uint8_t* blur, uint8_t* dst, int width, int amount,
                         int32_t maxDelta);

// AM1: 1:1 port of vmaf_unsharp_apply_frame
// (pic_analysis_process.c:1815-1821): the per-row dispatch over
// vmafApplyUnsharpRow - src/dst advance by stride, blur by width.
void vmafUnsharpApplyFrame(const uint8_t* src, const uint8_t* blurPlane, uint8_t* dst, int width, int height,
                           int stride, int sharpAmount, int32_t deltaClip);

// AM1: 1:1 port of svt_vmaf_compute_avg_mad_c
// (temporal_filtering.c:3622-3648).
uint32_t vmafComputeAvgMad(const uint8_t* src, int width, int height, int stride);

// AM1: 1:1 port of svt_vmaf_compute_gradient_coherence_c
// (temporal_filtering.c:3671-3702): 16-step blockwise gradient
// coherence, double accumulators, float sqrtf; zero weight sum -> 1.0f.
float vmafComputeGradientCoherence(const uint8_t* src, int width, int height, int stride);

// AM1: 1:1 port of svt_vmaf_count_detail_le_c
// (temporal_filtering.c:3704-3718): the count of |src - blur| <= thresh;
// src advances by srcStride, blur by width.
uint32_t vmafCountDetailLe(const uint8_t* src, const uint8_t* blur, int width, int height, int srcStride,
                           int thresh);

// AM1: 1:1 port of svt_estimate_noise_fp16_c
// (temporal_filtering.c:3556-3585): Sobel-admitted 8-neighbour Laplacian
// noise estimate in fp16; constants EDGE_THRESHOLD 50, SMOOTH_THRESHOLD
// 16, SQRT_PI_BY_2_FP16 82137 (temporal_filtering.h:19-21); num < 16 ->
// -65536.
int32_t vmafEstimateNoiseFp16(const uint8_t* src, uint16_t width, uint16_t height, uint16_t yStride);

// log1p(x) for x in [-1..6], step 1/32, fixed point shift 16
// (temporal_filtering.c:339-565). 225 entries.
extern const int32_t log1pTabFp16[225];

// T[X] = exp(-(X)/16) for x in [0..7], step 1/16, fixed point shift 16
// (temporal_filtering.c:624-634). 129 entries.
extern const int32_t expfTabFp16[129];

// exp(-x) for x in [0..7] step 1/10 (temporal_filtering.c:666-677).
// 81 entries.
extern const double expfTab[81];

// sqrt(x)*65536 for x in [0..15] (temporal_filtering.c:637-652).
extern const uint32_t sqrtArrayFp16[16];

// 1:1 port of sqrt_fast (temporal_filtering.c:655-664); svt_log2f is
// get_msb (definitions.h:592, :596-612).
uint32_t sqrtFast(uint32_t x);

// 1:1 port of calculate_tf_shift_factor (temporal_filtering.c:610-619)
// with the consumed MeContext field threaded as a parameter (the
// campaign's struct-parking deviation pattern).
uint8_t calculateTfShiftFactor(uint64_t tf64x64BlockError);

// 1:1 port of svt_av1_calculate_decay_factor (temporal_filtering.c:589-607).
// Threading: tfDecayFactorFp16 is the out-array (MeContext
// tf_decay_factor_fp16[3]); nDecayFp10 is the caller-computed luma value
// (temporal_filtering.c:2732-2733); noiseLevelsLog1pFp16 indexes
// PLANE_U/PLANE_V and may be null when tfChroma is false.
void calculateDecayFactor(uint32_t tfDecayFactorFp16[3], int32_t nDecayFp10, uint32_t qDecayFp8,
                          int decayControlCu, int decayControlCv, int32_t const0dot7Fp16,
                          const int32_t* noiseLevelsLog1pFp16, uint8_t shiftFactor, bool tfChroma);

// 1:1 port of calculate_squared_errors_sum (temporal_filtering.c:697-707).
uint32_t calculateSquaredErrorsSum(const uint8_t* s, int sStride, const uint8_t* p, int pStride,
                                   unsigned int w, unsigned int h);

// TF2: 1:1 port of calculate_squared_errors_sum_highbd
// (temporal_filtering.c:710-720). The SSE accumulates in int arithmetic
// (SQR(x) = (x)*(x), utility.h:107) and shift_factor is applied ONCE at
// the end (:719), not per pixel; shift_factor = (encoder_bit_depth - 8)
// * 2 is the caller's derivation (:1113).
uint32_t calculateSquaredErrorsSumHbd(const uint16_t* s, int sStride, const uint16_t* p, int pStride,
                                      unsigned int w, unsigned int h, int shiftFactor);

// 1:1 port of svt_av1_apply_zz_based_temporal_filter_planewise_medium_
// partial_c (temporal_filtering.c:738-781). Threading: the consumed
// MeContext fields are threaded as resolved per-call values - splitFlag
// (tf_32x32_block_split_flag[idx_32x32]), blockError16x16[4]
// (tf_16x16_block_error[idx_32x32*4..+3]) and blockError32x32
// (tf_32x32_block_error[idx_32x32]); the idx_32x32 =
// tf_block_col + tf_block_row*2 resolution is the caller's job.
void applyZzBasedTemporalFilterPlanewiseMediumPartial(bool splitFlag, const uint64_t* blockError16x16,
                                                      uint64_t blockError32x32, const uint8_t* yPre,
                                                      int yPreStride, unsigned int blockWidth,
                                                      unsigned int blockHeight, uint32_t* yAccum,
                                                      uint16_t* yCount, uint32_t tfDecayFactorFp16);

// 1:1 port of svt_av1_apply_zz_based_temporal_filter_planewise_medium_c
// (temporal_filtering.c:783-814). Threading: tfDecayFactorFp16 indexes
// MeContext tf_decay_factor_fp16[3] (Y/U/V) and tfChroma is MeContext
// tf_chroma; the same resolved per-call block-error threading as the
// partial kernel (the idx_32x32 resolution is the caller's job and is
// shared by all three planes).
void applyZzBasedTemporalFilterPlanewiseMedium(bool splitFlag, const uint64_t* blockError16x16,
                                               uint64_t blockError32x32, const uint8_t* yPre, int yPreStride,
                                               const uint8_t* uPre, const uint8_t* vPre, int uvPreStride,
                                               unsigned int blockWidth, unsigned int blockHeight, int ssX,
                                               int ssY, uint32_t* yAccum, uint16_t* yCount, uint32_t* uAccum,
                                               uint16_t* uCount, uint32_t* vAccum, uint16_t* vCount,
                                               const uint32_t* tfDecayFactorFp16, bool tfChroma);

// TF2: 1:1 port of svt_av1_apply_zz_based_temporal_filter_planewise_
// medium_hbd_partial_c (temporal_filtering.c:832-874). The kernel
// (void)s encoder_bit_depth (:839) so the parameter is DROPPED (named
// deviation); the block-error pre-shifts are HARD-CODED >> 4 / >> 6
// (:845, :849), NOT bd-parameterized; the weight is
// (expf_tab_fp16[scaled_diff16] * TF_WEIGHT_SCALE) >> 17 (:860) with the
// tables NOT bd-scaled; accumulator/count types are NOT widened
// (uint32_t* y_accum, uint16_t* y_count, :834). Threading identical to
// the 8-bit twin.
void applyZzBasedTemporalFilterPlanewiseMediumPartialHbd(bool splitFlag, const uint64_t* blockError16x16,
                                                         uint64_t blockError32x32, const uint16_t* yPre,
                                                         int yPreStride, unsigned int blockWidth,
                                                         unsigned int blockHeight, uint32_t* yAccum,
                                                         uint16_t* yCount, uint32_t tfDecayFactorFp16);

// TF2: 1:1 port of svt_av1_apply_zz_based_temporal_filter_planewise_
// medium_hbd_c (temporal_filtering.c:876-912). Same threading as the
// 8-bit wrapper (:783-814); the reference threads encoder_bit_depth
// only into the (void)ed partial parameter, so it is DROPPED here too
// (named deviation).
void applyZzBasedTemporalFilterPlanewiseMediumHbd(bool splitFlag, const uint64_t* blockError16x16,
                                                  uint64_t blockError32x32, const uint16_t* yPre, int yPreStride,
                                                  const uint16_t* uPre, const uint16_t* vPre, int uvPreStride,
                                                  unsigned int blockWidth, unsigned int blockHeight, int ssX,
                                                  int ssY, uint32_t* yAccum, uint16_t* yCount, uint32_t* uAccum,
                                                  uint16_t* uCount, uint32_t* vAccum, uint16_t* vCount,
                                                  const uint32_t* tfDecayFactorFp16, bool tfChroma);

// 1:1 port of svt_av1_apply_temporal_filter_planewise_medium_partial_c
// (temporal_filtering.c:930-1037). Threading: mvDistTh (MeContext
// tf_mv_dist_th), splitFlag, mv16x16X/Y[4] + blockError16x16[4] (the
// idx_32x32 slice of tf_16x16_mv_x/y and tf_16x16_block_error),
// mv32x32X/Y + blockError32x32 (the idx_32x32 slice of
// tf_32x32_mv_x/y and tf_32x32_block_error); lumaWindowErrorQuadFp8 is
// the caller-owned shared window-error array (written by the luma call,
// consumed by the chroma calls, :1046-1085).
void applyTemporalFilterPlanewiseMediumPartial(uint16_t mvDistTh, bool splitFlag, const int16_t* mv16x16X,
                                               const int16_t* mv16x16Y, const uint64_t* blockError16x16,
                                               int16_t mv32x32X, int16_t mv32x32Y, uint64_t blockError32x32,
                                               const uint8_t* ySrc, int ySrcStride, const uint8_t* yPre,
                                               int yPreStride, unsigned int blockWidth, unsigned int blockHeight,
                                               uint32_t* yAccum, uint16_t* yCount, uint32_t tfDecayFactorFp16,
                                               uint32_t lumaWindowErrorQuadFp8[4], bool isChroma);

// 1:1 port of svt_av1_apply_temporal_filter_planewise_medium_c
// (temporal_filtering.c:1039-1087). Threading: mvDistTh, the resolved
// idx_32x32 block-error/mv slices, tfDecayFactorFp16[3] (Y/U/V) and
// tfChroma threaded as parameters; the shared luma window-error array
// is owned by this wrapper (the reference's luma_window_error_quad_
// fp8 local at :1046).
void applyTemporalFilterPlanewiseMedium(uint16_t mvDistTh, bool splitFlag, const int16_t* mv16x16X,
                                        const int16_t* mv16x16Y, const uint64_t* blockError16x16,
                                        int16_t mv32x32X, int16_t mv32x32Y, uint64_t blockError32x32,
                                        const uint8_t* ySrc, int ySrcStride, const uint8_t* yPre, int yPreStride,
                                        const uint8_t* uSrc, const uint8_t* vSrc, int uvSrcStride,
                                        const uint8_t* uPre, const uint8_t* vPre, int uvPreStride,
                                        unsigned int blockWidth, unsigned int blockHeight, int ssX, int ssY,
                                        uint32_t* yAccum, uint16_t* yCount, uint32_t* uAccum, uint16_t* uCount,
                                        uint32_t* vAccum, uint16_t* vCount, const uint32_t* tfDecayFactorFp16,
                                        bool tfChroma);

// TF2: 1:1 port of svt_av1_apply_temporal_filter_planewise_medium_hbd_
// partial_c (temporal_filtering.c:1105-1214). Threading: the same
// resolved per-call block-error/mv fields as the 8-bit partial plus
// encoderBitDepth, which is consumed ONLY as shift_factor =
// (encoder_bit_depth - 8) * 2 (:1113) fed to the four
// calculate_squared_errors_sum_highbd calls (:1153-1176). The
// block-error pre-shifts are HARD-CODED >> 4 / >> 6 (:1133, :1147); the
// weight is (expf_tab_fp16[scaled_diff16] * TF_WEIGHT_SCALE) >> 16
// (:1200); accumulator/count types are NOT widened (:1107-1108).
void applyTemporalFilterPlanewiseMediumPartialHbd(uint16_t mvDistTh, bool splitFlag, const int16_t* mv16x16X,
                                                  const int16_t* mv16x16Y, const uint64_t* blockError16x16,
                                                  int16_t mv32x32X, int16_t mv32x32Y, uint64_t blockError32x32,
                                                  const uint16_t* ySrc, int ySrcStride, const uint16_t* yPre,
                                                  int yPreStride, unsigned int blockWidth, unsigned int blockHeight,
                                                  uint32_t* yAccum, uint16_t* yCount, uint32_t tfDecayFactorFp16,
                                                  uint32_t lumaWindowErrorQuadFp8[4], bool isChroma,
                                                  uint32_t encoderBitDepth);

// TF2: 1:1 port of svt_av1_apply_temporal_filter_planewise_medium_hbd_c
// (temporal_filtering.c:1216-1267). The wrapper owns the shared luma
// window-error array (:1222) and threads encoder_bit_depth to every
// partial call (:1236, :1251, :1265).
void applyTemporalFilterPlanewiseMediumHbd(uint16_t mvDistTh, bool splitFlag, const int16_t* mv16x16X,
                                           const int16_t* mv16x16Y, const uint64_t* blockError16x16,
                                           int16_t mv32x32X, int16_t mv32x32Y, uint64_t blockError32x32,
                                           const uint16_t* ySrc, int ySrcStride, const uint16_t* yPre,
                                           int yPreStride, const uint16_t* uSrc, const uint16_t* vSrc,
                                           int uvSrcStride, const uint16_t* uPre, const uint16_t* vPre,
                                           int uvPreStride, unsigned int blockWidth, unsigned int blockHeight,
                                           int ssX, int ssY, uint32_t* yAccum, uint16_t* yCount, uint32_t* uAccum,
                                           uint16_t* uCount, uint32_t* vAccum, uint16_t* vCount,
                                           const uint32_t* tfDecayFactorFp16, bool tfChroma,
                                            uint32_t encoderBitDepth);
}
}
