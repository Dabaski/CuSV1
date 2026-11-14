#pragma once

#include <cstdint>

#include "convolve.h"

// l17_inter - the A3 inter-prediction surface. Ported from
// third_party/SVT-AV1/Source/Lib/Codec/inter_prediction.c and the struct-free
// spans of enc_inter_prediction.c. Every ported function cites its reference
// span; parks and deviations are named at their declaration.
//
// LANDED (this slice, MC1b):
//  - compound-type predicate + dist-wtd core (T1a)
//  - diffwtd mask family (8-bit + highbd) + the scale-factor machinery +
//    the dist-wtd weight assignment + the pack_block facade (T1b)
//  - the wedge/mask tables, mechanically transcribed, runtime-diff-verified
//    against the reference file (T2)
//  - the sr convolve wrappers + intrabc convolve + the inter-predictor
//    dispatch (T3)
//  - the struct-free enc helpers: wedge scoring, SSE, the RD model (T4)
//
// PARKED, named:
//  - svt_aom_pack2d_src (pic_operators.c:173, the A6 dispatch PO1 parked) -
//    threaded as an injectable callee into packBlock
//  - svt_aom_subtract_block_c / svt_aom_highbd_subtract_block_c
//    (inter_prediction.c:41-:69) - already 1:1 in l6 as
//    picops::residualKernel8bit/16bit, charged there, not re-ported
//  - sub_pel_filters_8/4 (inter_prediction.c:241-:272) - landed in l12 as
//    subPelFilters8/subPelFilters4
//  - the jnt compound convolve family :529-:714 (RTC minimal is single-ref;
//    the reference's own CONFIG_ENABLE_INTER_COMPOUND strip note :1122-:1130)
//  - the highbd convolve family :716-:1098 + svt_highbd_inter_predictor
//    :1455-:1523 (hbd convolve parked)
//  - the asm function-pointer tables + setters :1101-:1131 (the dispatch
//    indexes direct calls instead)
//  - svt_inter_predictor_pd0 / svt_inter_predictor_light_pd1 :1261-:1396
//  - the wedge MASK synthesis (wedge_mask_obl/wedge_mask_buf +
//    init_wedge_primary_masks/init_wedge_signs/init_wedge_masks +
//    get_wedge_mask_inplace + get_contiguous_soft_mask, :2071-:2224 and
//    :2034-:2036) - runtime buffers, not transcribed tables
//  - the obmc/interintra table block :2228-:2574 and the rest of
//    enc_inter_prediction.c's struct-walled machinery (per-span parks in the
//    slice report)
namespace inter {

// ---------- constants ----------

constexpr int kSubpelBits = 4;              // filter.h:22-:27 (l12 subpelBits)
constexpr int kScaleSubpelBits = 10;        // inter_prediction.c:21
constexpr int kScaleSubpelShifts = 1 << kScaleSubpelBits;       // :22
constexpr int kScaleSubpelMask = kScaleSubpelShifts - 1;        // :23
constexpr int kScaleExtraBits = kScaleSubpelBits - kSubpelBits; // :24

constexpr int kRefScaleShift = 14;        // inter_prediction.h:27
constexpr int kRefNoScale = 1 << kRefScaleShift; // :28
constexpr int kRefInvalidScale = -1;      // :29

constexpr int kMaxFrameDistance = (1 << 5) - 1; // definitions.h:383-:384
constexpr int kDiffFactorLog2 = 4;              // definitions.h:1252
constexpr int kDiffFactor = 1 << kDiffFactorLog2; // :1253
constexpr int kAomBlendA64RoundBits = 6;          // definitions.h:1247
constexpr int kAomBlendA64MaxAlpha = 1 << kAomBlendA64RoundBits; // :1248
constexpr int kMaskPrimarySize = 64;             // definitions.h:1257-:1261
constexpr int kMaxWedgeTypes = 1 << 4;            // definitions.h:1256
constexpr int kBlockSizesAll = 22;                // definitions.h:882-:905
constexpr int kMaxFilterTap = 8;                  // inter_prediction.c:274
constexpr int kAv1ProbCostShift = 9;              // md_rate_estimation.h:29
constexpr int kDistPrecisionBits = 4;             // definitions.h:430 (DIST_PRECISION_BITS)

// ---------- enums and small structs ----------

// CompoundType, definitions.h:1238-:1245.
enum CompoundType {
    CompoundAverage = 0, // COMPOUND_AVERAGE
    CompoundDistwtd,     // COMPOUND_DISTWTD
    CompoundWedge,       // COMPOUND_WEDGE
    CompoundDiffwtd,     // COMPOUND_DIFFWTD
    CompoundTypes,
    MaskedCompoundTypes = 2, // MASKED_COMPOUND_TYPES
};

// DIFFWTD_MASK_TYPE, definitions.h:1276-:1277.
enum DiffwtdMaskType {
    Diffwtd38 = 0,    // DIFFWTD_38
    Diffwtd38Inv,     // DIFFWTD_38_INV
};

// WedgeDirectionType, inter_prediction.h:70-:78.
enum WedgeDirectionType {
    WedgeHorizontal = 0, // WEDGE_HORIZONTAL
    WedgeVertical,       // WEDGE_VERTICAL
    WedgeOblique27,      // WEDGE_OBLIQUE27
    WedgeOblique63,      // WEDGE_OBLIQUE63
    WedgeOblique117,     // WEDGE_OBLIQUE117
    WedgeOblique153,     // WEDGE_OBLIQUE153
    WedgeDirections,     // WEDGE_DIRECTIONS
};

// WedgeCodeType, inter_prediction.h:87-:91 (the {direction, x, y} 3-tuple).
struct WedgeCodeType {
    int direction;
    std::int32_t xOffset;
    std::int32_t yOffset;
};

// WedgeParamsType, inter_prediction.h:93-:98. NAMED DEVIATION: the
// uint8_t* masks member (the runtime wedge_masks buffer) is parked with the
// mask-synthesis machinery, so the port carries bits/codebook/signflip only.
struct WedgeParamsType {
    std::int32_t bits;
    const WedgeCodeType* codebook;
    const std::uint8_t* signflip;
};

// SubpelParams, inter_prediction.h:50-:55.
struct SubpelParams {
    std::int32_t xs;
    std::int32_t ys;
    std::int32_t subpelX;
    std::int32_t subpelY;
};

// ScaleFactors, definitions.h:1763-:1771 - the two scale_value function
// pointers are part of the reference semantics (set by
// setupScaleFactorsForFrame).
struct ScaleFactors;
using ScaleValueFn = std::int32_t (*)(std::int32_t val, const ScaleFactors* sf);
struct ScaleFactors {
    std::int32_t xScaleFp;
    std::int32_t yScaleFp;
    std::int32_t xStepQ4;
    std::int32_t yStepQ4;
    ScaleValueFn scaleValueX;
    ScaleValueFn scaleValueY;
};

// ConvBufType, definitions.h:658.
using ConvBufType = std::uint16_t;

// ConvolveParams, definitions.h:660-:671, the FULL field set (l12's minimal
// round_0/round_1 carry is the wiener-path subset; the sr convolve family
// consumes do_average/dst/dst_stride/is_compound too).
struct ConvolveParams {
    std::int32_t doAverage;
    ConvBufType* dst;
    std::int32_t dstStride;
    std::int32_t round0;
    std::int32_t round1;
    std::int32_t isCompound;
    std::int32_t useJntCompAvg;
    std::int32_t fwdOffset;
    std::int32_t bckOffset;
    std::int32_t useDistWtdCompAvg;
};

// InterpFilterParams is l12's 1:1 carry (definitions.h:849-:854).
using InterpFilterParams = convolve::InterpFilterParams;

// ---------- T1a: compound-type predicate, dist-wtd core ----------

// svt_aom_is_masked_compound_type, inter_prediction.c:37-:39.
bool isMaskedCompoundType(CompoundType type);

// div_mult, inter_prediction.c:32-:34.
extern const std::int32_t divMult[32];

// svt_aom_get_relative_dist_enc, inter_prediction.c:276-:285. NAMED
// DEVIATION: SeqHeader->order_hint_info threads as the two parameters.
std::int32_t getRelativeDistEnc(bool enableOrderHint, std::int32_t orderHintBits, std::int32_t refHint,
                                std::int32_t orderHint);

// diffwtd_mask, inter_prediction.c:71-:80.
void diffwtdMask(std::uint8_t* mask, int whichInverse, int maskBase, const std::uint8_t* src0, int src0Stride,
                 const std::uint8_t* src1, int src1Stride, int h, int w);

// svt_av1_build_compound_diffwtd_mask_c, inter_prediction.c:157-:169.
void buildCompoundDiffwtdMask(std::uint8_t* mask, DiffwtdMaskType maskType, const std::uint8_t* src0, int src0Stride,
                              const std::uint8_t* src1, int src1Stride, int h, int w);

// ---------- T1b: hbd diffwtd + scale-factor machinery ----------

// diffwtd_mask_highbd, inter_prediction.c:82-:140.
void diffwtdMaskHighbd(std::uint8_t* mask, int whichInverse, int maskBase, const std::uint16_t* src0, int src0Stride,
                       const std::uint16_t* src1, int src1Stride, int h, int w, unsigned int bd);

// svt_av1_build_compound_diffwtd_mask_highbd_c, inter_prediction.c:142-:155.
void buildCompoundDiffwtdMaskHighbd(std::uint8_t* mask, DiffwtdMaskType maskType, const std::uint16_t* src0,
                                    int src0Stride, const std::uint16_t* src1, int src1Stride, int h, int w,
                                    unsigned int bd);

// scaled_x / scaled_y, inter_prediction.c:172-:183.
std::int32_t scaledX(std::int32_t val, const ScaleFactors* sf);
std::int32_t scaledY(std::int32_t val, const ScaleFactors* sf);

// unscaled_value, inter_prediction.c:186-:189.
std::int32_t unscaledValue(std::int32_t val, const ScaleFactors* sf);

// get_fixed_point_scale_factor, inter_prediction.c:191-:197.
std::int32_t getFixedPointScaleFactor(std::int32_t otherSize, std::int32_t thisSize);

// fixed_point_scale_to_coarse_point_scale, inter_prediction.c:200-:202.
std::int32_t fixedPointScaleToCoarsePointScale(std::int32_t scaleFp);

// svt_av1_setup_scale_factors_for_frame, inter_prediction.c:204-:224.
void setupScaleFactorsForFrame(ScaleFactors* sf, int otherW, int otherH, int thisW, int thisH);

// has_scale, inter_prediction.c:226-:228.
bool hasScale(std::int32_t xs, std::int32_t ys);

// revert_scale_extra_bits, inter_prediction.c:230-:239.
void revertScaleExtraBits(SubpelParams* sp);

// svt_av1_dist_wtd_comp_weight_assign, inter_prediction.c:293-:330. NAMED
// DEVIATION: SeqHeader->order_hint_info threads as the two parameters.
void distWtdCompWeightAssign(bool enableOrderHint, std::int32_t orderHintBits, int curFrameIndex, int bckFrameIndex,
                             int fwdFrameIndex, int compoundIdx, int orderIdx, int* fwdOffset, int* bckOffset,
                             int* useDistWtdCompAvg, int isCompound);

// svt_aom_pack_block, inter_prediction.c:26-:30. NAMED DEVIATION: the
// callee svt_aom_pack2d_src (pic_operators.c:173) is the PO1-parked A6
// dispatch, so it threads as an injectable function pointer.
using Pack2dFn = void (*)(const std::uint8_t* in8, std::uint32_t in8Stride, const std::uint8_t* innBit,
                          std::uint32_t innStride, std::uint16_t* out16, std::uint32_t outStride, std::uint32_t width,
                          std::uint32_t height);
void packBlock(const std::uint8_t* in8BitBuffer, std::uint32_t in8Stride, const std::uint8_t* innBitBuffer,
               std::uint32_t innStride, std::uint16_t* out16BitBuffer, std::uint32_t outStride, std::uint32_t width,
               std::uint32_t height, Pack2dFn pack2d);

// ---------- T2: the wedge/mask tables ----------

// wedge_primary_oblique_odd / _even / _vertical, inter_prediction.c:1529-:1543.
extern const std::uint8_t wedgePrimaryObliqueOdd[kMaskPrimarySize];
extern const std::uint8_t wedgePrimaryObliqueEven[kMaskPrimarySize];
extern const std::uint8_t wedgePrimaryVertical[kMaskPrimarySize];

// wedge_signflip_lookup, inter_prediction.c:1545-:1942.
extern const std::uint8_t wedgeSignflipLookup[kBlockSizesAll][kMaxWedgeTypes];

// wedge_codebook_16_hgtw / _hltw / _heqw, inter_prediction.c:1944-:1999.
extern const WedgeCodeType wedgeCodebook16Hgtw[16];
extern const WedgeCodeType wedgeCodebook16Hltw[16];
extern const WedgeCodeType wedgeCodebook16Heqw[16];

// wedge_params_lookup, inter_prediction.c:2001-:2024.
extern const WedgeParamsType wedgeParamsLookup[kBlockSizesAll];

// svt_aom_is_interintra_wedge_used, inter_prediction.c:2026-:2028. NAMED
// DEVIATION: BlockSize threads as the 0..21 index (definitions.h:883-:904).
bool isInterIntraWedgeUsed(int bsize);

// svt_aom_get_wedge_bits_lookup, inter_prediction.c:2030-:2032.
std::int32_t getWedgeBitsLookup(int bsize);

// svt_aom_get_wedge_params_bits, inter_prediction.c:2064-:2066.
std::int32_t getWedgeParamsBits(int bsize);

// ---------- A3E J1: the jnt (compound) convolve family, 8-bit ----------

// svt_av1_jnt_convolve_2d_c, inter_prediction.c:529-:585. The MC1 park
// ("the jnt compound convolve family :529-:714") RESOLVES here: the pinned
// build has CONFIG_ENABLE_INTER_COMPOUND=1 (EbConfigMacros.h:187), so the
// jnt slots are live dispatch targets, not dead code.
void jntConvolve2d(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst8, std::int32_t dst8Stride,
                   std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                   const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                   ConvolveParams* convParams);

// svt_av1_jnt_convolve_y_c, inter_prediction.c:587-:630. The single-axis
// order quirk is pinned by test: res is scaled by (1 << bits) with
// bits = FILTER_BITS - round_0 BEFORE the round_1, then round_offset is
// added back in (the 2d form has no such scale).
void jntConvolveY(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst8, std::int32_t dst8Stride,
                  std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                  const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                  ConvolveParams* convParams);

// svt_av1_jnt_convolve_x_c, inter_prediction.c:632-:675. The x twin's order
// is the INVERSE of the y twin: res = (1 << bits) * ROUND(res, round_0)
// with bits = FILTER_BITS - round_1, then round_offset is added.
void jntConvolveX(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst8, std::int32_t dst8Stride,
                  std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                  const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                  ConvolveParams* convParams);

// svt_av1_jnt_convolve_2d_copy_c, inter_prediction.c:677-:714. The no-subpel
// compound copy: res = src << bits (bits = 2*FILTER_BITS - round_1 -
// round_0), + round_offset as ConvBufType, then the same compose tail.
void jntConvolve2dCopy(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst8, std::int32_t dst8Stride,
                       std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                       const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                       ConvolveParams* convParams);

// ---------- T3: sr convolve wrappers + intrabc + the dispatch ----------

// CONV_BUF_TYPE* dst threading: the sr wrappers are single-ref (the jnt
// compound family parks), but convolve2dScale carries the reference's
// compound branches verbatim (:501-:517).

// svt_av1_convolve_2d_sr_c, inter_prediction.c:332-:375.
void convolve2dSr(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst, std::int32_t dstStride,
                  std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                  const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                  ConvolveParams* convParams);

// svt_av1_convolve_y_sr_c, inter_prediction.c:377-:403.
void convolveYSr(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst, std::int32_t dstStride,
                 std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                 const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                 ConvolveParams* convParams);

// svt_av1_convolve_x_sr_c, inter_prediction.c:405-:432.
void convolveXSR(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst, std::int32_t dstStride,
                 std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                 const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                 ConvolveParams* convParams);

// svt_av1_convolve_2d_copy_sr_c, inter_prediction.c:434-:449.
void convolve2dCopySr(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst, std::int32_t dstStride,
                      std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                      const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                      ConvolveParams* convParams);

// svt_av1_convolve_2d_scale_c, inter_prediction.c:451-:527.
void convolve2dScale(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst8, std::int32_t dst8Stride,
                     std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                     const InterpFilterParams* filterParamsY, std::int32_t subpelXQn, std::int32_t xStepQn,
                     std::int32_t subpelYQn, std::int32_t yStepQn, ConvolveParams* convParams);

// convolve_2d_for_intrabc, inter_prediction.c:1200-:1241.
void convolve2dForIntrabc(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst, std::int32_t dstStride,
                          std::int32_t w, std::int32_t h, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                          ConvolveParams* convParams);

// svt_inter_predictor, inter_prediction.c:1397-:1453. NAMED DEVIATION: the
// svt_aom_convolve function-pointer table (:1115, parked with the asm
// setters :1101-:1131) dispatches as direct calls; the compound jnt slots
// park per the reference's own CONFIG_ENABLE_INTER_COMPOUND strip note.
void interPredictor(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst, std::int32_t dstStride,
                    const SubpelParams* subpelParams, const ScaleFactors* sf, std::int32_t w, std::int32_t h,
                    ConvolveParams* convParams, std::uint32_t interpFilters, std::int32_t isIntrabc);

// ---------- T4: struct-free enc helpers (enc_inter_prediction.c) ----------

// svt_av1_wedge_compute_delta_squares_c, enc_inter_prediction.c:375-:381.
void wedgeComputeDeltaSquares(std::int16_t* d, const std::int16_t* a, const std::int16_t* b, int n);

// svt_av1_wedge_sign_from_residuals_c, enc_inter_prediction.c:414-:422.
std::int8_t wedgeSignFromResiduals(const std::int16_t* ds, const std::uint8_t* m, int n, std::int64_t limit);

// svt_aom_sse_c, enc_inter_prediction.c:615-:627.
std::int64_t sse8(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, int width, int height);

// svt_aom_highbd_sse_c, enc_inter_prediction.c:600-:613.
std::int64_t sseHighbd(const std::uint8_t* a8, int aStride, const std::uint8_t* b8, int bStride, int width,
                       int height);

// model_rd_norm, enc_inter_prediction.c:1883-:1937.
void modelRdNorm(std::int32_t xsqQ10, std::int32_t* rQ10, std::int32_t* dQ10);

// svt_av1_model_rd_from_var_lapndz, enc_inter_prediction.c:1939-:1958.
void modelRdFromVarLapndz(std::int64_t var, std::uint32_t nLog2, std::uint32_t qstep, std::int32_t* rate,
                          std::int64_t* dist);

// model_rd_from_sse, enc_inter_prediction.c:1960-:1981. NAMED DEVIATION:
// eb_num_pels_log2_lookup[bsize] threads as numPelsLog2 (the caller-side
// table read at :1977).
void modelRdFromSse(std::int32_t numPelsLog2, std::int16_t quantizer, std::uint8_t bitDepth, std::uint64_t sse,
                    std::uint32_t* rate, std::uint64_t* dist, std::uint8_t simpleModelRdFromVar);

} // namespace inter
