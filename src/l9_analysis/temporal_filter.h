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
// create_me_context_and_picture_control (:205-262),
// apply_filtering_central (:262-300) + its highbd twin (:300-335).
#pragma once

#include <cstdint>

namespace analysis {
namespace temporal {

  // 1:1 port of svt_aom_noise_log1p_fp16 (temporal_filtering.c:568-586).
int32_t noiseLog1pFp16(int32_t noiseLevelFp16);

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
}
}
