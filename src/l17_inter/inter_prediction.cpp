#include "inter_prediction.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace inter {

// PACKED_BUFFER_SIZE, definitions.h:344-:346 ((MAX_SB_SIZE + 2*8)^2 with
// MAX_SB_SIZE = 128 -> 20736); the light_pd1 temp buffer is 4x that.
constexpr int definitionsPackediBufferSize = (convolve::maxSbSize + (8 << 1)) * (convolve::maxSbSize + (8 << 1));

// ---------- A3E W1: the wedge mask synthesis (storage) ----------

// block_size_wide/high, common_utils.c:286-:289. Local 22-entry
// duplicates (the l10 kBlockSizeWide precedent; l17 cannot link l14).
const std::uint8_t kBlockSizeWide[22] = {4,  4,  8,  8,   8,   16, 16, 16, 32, 32, 32,
                                         64, 64, 64, 128, 128, 4,  16, 8,  32, 16, 64};
const std::uint8_t kBlockSizeHigh[22] = {4,  8,  4,   8,  16,  8,  16, 32, 16, 32, 64,
                                         32, 64, 128, 64, 128, 16, 4,  32, 8,  64, 16};

// MASK_PRIMARY_STRIDE == MASK_PRIMARY_SIZE (definitions.h:1261-:1262);
// MAX_WEDGE_SQUARE = MAX_WEDGE_SIZE^2 (definitions.h:1259, 32x32);
// WEDGE_WEIGHT_BITS = 6 (definitions.h:1260).
constexpr int kMaskPrimaryStride = kMaskPrimarySize;
constexpr int kMaxWedgeSquare = 32 * 32;
constexpr int kWedgeWeightBits = 6;

// wedge_mask_obl, inter_prediction.c:2071 ([negative][direction]).
std::uint8_t wedgeMaskObl[2][6][kMaskPrimarySize * kMaskPrimarySize];

// wedge_mask_buf, inter_prediction.c:2075 (2 * MAX_WEDGE_TYPES * 4 *
// MAX_WEDGE_SQUARE).
std::uint8_t wedgeMaskBuf[2 * kMaxWedgeTypes * 4 * kMaxWedgeSquare];

// wedge_masks, inter_prediction.c:35 (one WedgeMasksType =
// uint8_t*[MAX_WEDGE_TYPES] per bsize per sign; the flat index of
// WedgeParamsType::masks is sign * kMaxWedgeTypes + wedge_index).
const std::uint8_t* wedgeMasks[kBlockSizesAll][2][kMaxWedgeTypes];

// clamp, the reference's utility.h helper (three-argument clamp).
inline std::int32_t clampInt(std::int32_t value, std::int32_t lo, std::int32_t hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

// svt_aom_is_masked_compound_type, inter_prediction.c:37-:39.
bool isMaskedCompoundType(CompoundType type) {
    return type == CompoundWedge || type == CompoundDiffwtd;
}

// div_mult, inter_prediction.c:32-:34.
const std::int32_t divMult[32] = {0,    16384, 8192, 5461, 4096, 3276, 2730, 2340, 2048, 1820, 1638,
                                  1489, 1365,  1260, 1170, 1092, 1024, 963,  910,  862,  819,  780,
                                  744,  712,   682,  655,  630,  606,  585,  564,  546,  528};

// quant_dist_weight / quant_dist_lookup_table, inter_prediction.c:287-:291.
const int quantDistWeight[4][2] = {{2, 3}, {2, 5}, {2, 7}, {1, kMaxFrameDistance}};
const int quantDistLookupTable[2][4][2] = {
    {{9, 7}, {11, 5}, {12, 4}, {13, 3}},
    {{7, 9}, {5, 11}, {4, 12}, {3, 13}},
};

// svt_aom_get_relative_dist_enc, inter_prediction.c:276-:285.
std::int32_t getRelativeDistEnc(bool enableOrderHint, std::int32_t orderHintBits, std::int32_t refHint,
                                std::int32_t orderHint) {
    if (!enableOrderHint) {
        return 0;
    }
    std::int32_t diff = refHint - orderHint;
    const std::int32_t m = 1 << (orderHintBits - 1);
    diff = (diff & (m - 1)) - (diff & m);
    return diff;
}

// diffwtd_mask, inter_prediction.c:71-:80.
void diffwtdMask(std::uint8_t* mask, int whichInverse, int maskBase, const std::uint8_t* src0, int src0Stride,
                 const std::uint8_t* src1, int src1Stride, int h, int w) {
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            const int diff = abs((int)src0[i * src0Stride + j] - (int)src1[i * src1Stride + j]);
            const int m = clampInt(maskBase + (diff / kDiffFactor), 0, kAomBlendA64MaxAlpha);
            mask[i * w + j] = whichInverse ? static_cast<std::uint8_t>(kAomBlendA64MaxAlpha - m)
                                           : static_cast<std::uint8_t>(m);
        }
    }
}

// svt_av1_build_compound_diffwtd_mask_c, inter_prediction.c:157-:169.
void buildCompoundDiffwtdMask(std::uint8_t* mask, DiffwtdMaskType maskType, const std::uint8_t* src0, int src0Stride,
                              const std::uint8_t* src1, int src1Stride, int h, int w) {
    switch (maskType) {
    case Diffwtd38:
        diffwtdMask(mask, 0, 38, src0, src0Stride, src1, src1Stride, h, w);
        break;
    case Diffwtd38Inv:
        diffwtdMask(mask, 1, 38, src0, src0Stride, src1, src1Stride, h, w);
        break;
    default:
        assert(0);
    }
}

// negative_to_zero, definitions.h:716-:718.
inline unsigned int negativeToZero(int value) {
    return (value < 0) ? 0 : value;
}

// diffwtd_mask_highbd, inter_prediction.c:82-:140.
void diffwtdMaskHighbd(std::uint8_t* mask, int whichInverse, int maskBase, const std::uint16_t* src0, int src0Stride,
                       const std::uint16_t* src1, int src1Stride, int h, int w, unsigned int bd) {
    assert(bd >= 8);
    if (bd == 8) {
        if (whichInverse) {
            for (int i = 0; i < h; ++i) {
                for (int j = 0; j < w; ++j) {
                    int diff = abs((int)src0[j] - (int)src1[j]) / kDiffFactor;
                    unsigned int m = negativeToZero(maskBase + diff);
                    m = m < kAomBlendA64MaxAlpha ? m : kAomBlendA64MaxAlpha;
                    mask[j] = static_cast<std::uint8_t>(kAomBlendA64MaxAlpha - m);
                }
                src0 += src0Stride;
                src1 += src1Stride;
                mask += w;
            }
        } else {
            for (int i = 0; i < h; ++i) {
                for (int j = 0; j < w; ++j) {
                    int diff = abs((int)src0[j] - (int)src1[j]) / kDiffFactor;
                    unsigned int m = negativeToZero(maskBase + diff);
                    m = m < kAomBlendA64MaxAlpha ? m : kAomBlendA64MaxAlpha;
                    mask[j] = static_cast<std::uint8_t>(m);
                }
                src0 += src0Stride;
                src1 += src1Stride;
                mask += w;
            }
        }
    } else {
        const unsigned int bdShift = bd - 8;
        if (whichInverse) {
            for (int i = 0; i < h; ++i) {
                for (int j = 0; j < w; ++j) {
                    int diff = (abs((int)src0[j] - (int)src1[j]) >> bdShift) / kDiffFactor;
                    unsigned int m = negativeToZero(maskBase + diff);
                    m = m < kAomBlendA64MaxAlpha ? m : kAomBlendA64MaxAlpha;
                    mask[j] = static_cast<std::uint8_t>(kAomBlendA64MaxAlpha - m);
                }
                src0 += src0Stride;
                src1 += src1Stride;
                mask += w;
            }
        } else {
            for (int i = 0; i < h; ++i) {
                for (int j = 0; j < w; ++j) {
                    int diff = (abs((int)src0[j] - (int)src1[j]) >> bdShift) / kDiffFactor;
                    unsigned int m = negativeToZero(maskBase + diff);
                    m = m < kAomBlendA64MaxAlpha ? m : kAomBlendA64MaxAlpha;
                    mask[j] = static_cast<std::uint8_t>(m);
                }
                src0 += src0Stride;
                src1 += src1Stride;
                mask += w;
            }
        }
    }
}

// svt_av1_build_compound_diffwtd_mask_highbd_c, inter_prediction.c:142-:155.
void buildCompoundDiffwtdMaskHighbd(std::uint8_t* mask, DiffwtdMaskType maskType, const std::uint16_t* src0,
                                    int src0Stride, const std::uint16_t* src1, int src1Stride, int h, int w,
                                    unsigned int bd) {
    switch (maskType) {
    case Diffwtd38:
        diffwtdMaskHighbd(mask, 0, 38, src0, src0Stride, src1, src1Stride, h, w, bd);
        break;
    case Diffwtd38Inv:
        diffwtdMaskHighbd(mask, 1, 38, src0, src0Stride, src1, src1Stride, h, w, bd);
        break;
    default:
        assert(0);
    }
}

// round helpers, definitions.h:457-:468 (ROUND_POWER_OF_TWO[_SIGNED_64]).
inline std::int32_t roundPowerOfTwo(std::int64_t value, int n) {
    return (std::int32_t)((value + (((std::int64_t)1 << n) >> 1)) >> n);
}

inline std::int64_t roundPowerOfTwo64(std::int64_t value, int n) {
    return (value + (((std::int64_t)1 << n) >> 1)) >> n;
}

inline std::int64_t roundPowerOfTwoSigned64(std::int64_t value, int n) {
    return value < 0 ? -roundPowerOfTwo64(-value, n) : roundPowerOfTwo64(value, n);
}

// scaled_x / scaled_y, inter_prediction.c:172-:183.
std::int32_t scaledX(std::int32_t val, const ScaleFactors* sf) {
    const int off = (sf->xScaleFp - kRefNoScale) * (1 << (kSubpelBits - 1));
    const std::int64_t tval = (std::int64_t)val * sf->xScaleFp + off;
    return (int)roundPowerOfTwoSigned64(tval, kRefScaleShift - kScaleExtraBits);
}

std::int32_t scaledY(std::int32_t val, const ScaleFactors* sf) {
    const std::int32_t off = (sf->yScaleFp - kRefNoScale) * (1 << (kSubpelBits - 1));
    const std::int64_t tval = (std::int64_t)val * sf->yScaleFp + off;
    return (std::int32_t)roundPowerOfTwoSigned64(tval, kRefScaleShift - kScaleExtraBits);
}

// unscaled_value, inter_prediction.c:186-:189.
std::int32_t unscaledValue(std::int32_t val, const ScaleFactors* sf) {
    (void)sf;
    return val << kScaleExtraBits;
}

// get_fixed_point_scale_factor, inter_prediction.c:191-:197.
std::int32_t getFixedPointScaleFactor(std::int32_t otherSize, std::int32_t thisSize) {
    return ((otherSize << kRefScaleShift) + thisSize / 2) / thisSize;
}

// fixed_point_scale_to_coarse_point_scale, inter_prediction.c:200-:202.
std::int32_t fixedPointScaleToCoarsePointScale(std::int32_t scaleFp) {
    return roundPowerOfTwo(scaleFp, kRefScaleShift - kScaleSubpelBits);
}

// has_scale, inter_prediction.c:226-:228.
bool hasScale(std::int32_t xs, std::int32_t ys) {
    return xs != kScaleSubpelShifts || ys != kScaleSubpelShifts;
}

// valid_ref_frame_size, inter_prediction.h:169-:172.
inline bool isValidRefFrameSize(int refW, int refH, int thisW, int thisH) {
    return 2 * thisW >= refW && 2 * thisH >= refH && thisW <= 16 * refW && thisH <= 16 * refH;
}

// av1_is_valid_scale / av1_is_scaled, inter_prediction.h:161-:167.
inline bool av1IsValidScale(const ScaleFactors* sf) {
    return sf->xScaleFp != kRefInvalidScale && sf->yScaleFp != kRefInvalidScale;
}

inline bool av1IsScaled(const ScaleFactors* sf) {
    return av1IsValidScale(sf) && (sf->xScaleFp != kRefNoScale || sf->yScaleFp != kRefNoScale);
}

// revert_scale_extra_bits, inter_prediction.c:230-:239.
void revertScaleExtraBits(SubpelParams* sp) {
    sp->subpelX >>= kScaleExtraBits;
    sp->subpelY >>= kScaleExtraBits;
    sp->xs >>= kScaleExtraBits;
    sp->ys >>= kScaleExtraBits;
    assert(sp->subpelX < convolve::subpelShifts);
    assert(sp->subpelY < convolve::subpelShifts);
    assert(sp->xs <= convolve::subpelShifts);
    assert(sp->ys <= convolve::subpelShifts);
}

// svt_av1_setup_scale_factors_for_frame, inter_prediction.c:204-:224.
void setupScaleFactorsForFrame(ScaleFactors* sf, int otherW, int otherH, int thisW, int thisH) {
    if (!isValidRefFrameSize(otherW, otherH, thisW, thisH)) {
        sf->xScaleFp = kRefInvalidScale;
        sf->yScaleFp = kRefInvalidScale;
        return;
    }
    sf->xScaleFp = getFixedPointScaleFactor(otherW, thisW);
    sf->yScaleFp = getFixedPointScaleFactor(otherH, thisH);
    sf->xStepQ4 = fixedPointScaleToCoarsePointScale(sf->xScaleFp);
    sf->yStepQ4 = fixedPointScaleToCoarsePointScale(sf->yScaleFp);
    if (av1IsScaled(sf)) {
        sf->scaleValueX = scaledX;
        sf->scaleValueY = scaledY;
    } else {
        sf->scaleValueX = unscaledValue;
        sf->scaleValueY = unscaledValue;
    }
}

// svt_av1_dist_wtd_comp_weight_assign, inter_prediction.c:293-:330.
void distWtdCompWeightAssign(bool enableOrderHint, std::int32_t orderHintBits, int curFrameIndex, int bckFrameIndex,
                             int fwdFrameIndex, int compoundIdx, int orderIdx, int* fwdOffset, int* bckOffset,
                             int* useDistWtdCompAvg, int isCompound) {
    if (!isCompound || compoundIdx) {
        *useDistWtdCompAvg = 0;
        return;
    }
    *useDistWtdCompAvg = 1;
    const int d0 = clampInt(abs(getRelativeDistEnc(enableOrderHint, orderHintBits, fwdFrameIndex, curFrameIndex)), 0,
                            kMaxFrameDistance);
    const int d1 = clampInt(abs(getRelativeDistEnc(enableOrderHint, orderHintBits, curFrameIndex, bckFrameIndex)), 0,
                            kMaxFrameDistance);
    const int order = d0 <= d1;
    if (d0 == 0 || d1 == 0) {
        *fwdOffset = quantDistLookupTable[orderIdx][3][order];
        *bckOffset = quantDistLookupTable[orderIdx][3][1 - order];
        return;
    }
    int i = 0;
    for (; i < 3; ++i) {
        const int c0 = quantDistWeight[i][order];
        const int c1 = quantDistWeight[i][!order];
        const int d0c0 = d0 * c0;
        const int d1c1 = d1 * c1;
        if ((d0 > d1 && d0c0 < d1c1) || (d0 <= d1 && d0c0 > d1c1)) {
            break;
        }
    }
    *fwdOffset = quantDistLookupTable[orderIdx][i][order];
    *bckOffset = quantDistLookupTable[orderIdx][i][1 - order];
}

// svt_aom_pack_block, inter_prediction.c:26-:30. The callee
// svt_aom_pack2d_src (pic_operators.c:173) is the PO1-parked A6 dispatch,
// threaded as the injectable function pointer.
void packBlock(const std::uint8_t* in8BitBuffer, std::uint32_t in8Stride, const std::uint8_t* innBitBuffer,
               std::uint32_t innStride, std::uint16_t* out16BitBuffer, std::uint32_t outStride, std::uint32_t width,
               std::uint32_t height, Pack2dFn pack2d) {
    pack2d(in8BitBuffer, in8Stride, innBitBuffer, innStride, out16BitBuffer, outStride, width, height);
}

// wedge_primary_oblique_odd, inter_prediction.c:1529-:1533.
const std::uint8_t wedgePrimaryObliqueOdd[kMaskPrimarySize] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  1,  2,  6,  18, 37, 53, 60, 63, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64};

// wedge_primary_oblique_even, inter_prediction.c:1534-:1538.
const std::uint8_t wedgePrimaryObliqueEven[kMaskPrimarySize] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  1,  4,  11, 27, 46, 58, 62, 63, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64};

// wedge_primary_vertical, inter_prediction.c:1539-:1543.
const std::uint8_t wedgePrimaryVertical[kMaskPrimarySize] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  2,  7,  21, 43, 57, 62, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64,     64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64};

// wedge_signflip_lookup, inter_prediction.c:1545-:1942. The 13 zero rows are
// the "// not used" bsizes; the wedge-carrying rows are idx 3-9 and 18-19.
const std::uint8_t wedgeSignflipLookup[kBlockSizesAll][kMaxWedgeTypes] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // BLOCK_4X4
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // BLOCK_4X8
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // BLOCK_8X4
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1}, // BLOCK_8X8
    {1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1}, // BLOCK_8X16
    {1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1}, // BLOCK_16X8
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1}, // BLOCK_16X16
    {1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1}, // BLOCK_16X32
    {1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1}, // BLOCK_32X16
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1}, // BLOCK_32X32
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // BLOCK_32X64
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // BLOCK_64X32
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // BLOCK_64X64
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // BLOCK_64X128
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // BLOCK_128X64
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // BLOCK_128X128
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // BLOCK_4X16
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // BLOCK_16X4
    {1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1}, // BLOCK_8X32
    {1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0, 1}, // BLOCK_32X8
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // BLOCK_16X64
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // BLOCK_64X16
};

// wedge_codebook_16_hgtw / _hltw / _heqw, inter_prediction.c:1944-:1999.
const WedgeCodeType wedgeCodebook16Hgtw[16] = {
    {WedgeOblique27, 4, 4},  {WedgeOblique63, 4, 4},  {WedgeOblique117, 4, 4}, {WedgeOblique153, 4, 4},
    {WedgeHorizontal, 4, 2}, {WedgeHorizontal, 4, 4}, {WedgeHorizontal, 4, 6}, {WedgeVertical, 4, 4},
    {WedgeOblique27, 4, 2},  {WedgeOblique27, 4, 6},  {WedgeOblique153, 4, 2}, {WedgeOblique153, 4, 6},
    {WedgeOblique63, 2, 4},  {WedgeOblique63, 6, 4},  {WedgeOblique117, 2, 4}, {WedgeOblique117, 6, 4}};

const WedgeCodeType wedgeCodebook16Hltw[16] = {
    {WedgeOblique27, 4, 4},  {WedgeOblique63, 4, 4},  {WedgeOblique117, 4, 4}, {WedgeOblique153, 4, 4},
    {WedgeVertical, 2, 4},   {WedgeVertical, 4, 4},   {WedgeVertical, 6, 4},   {WedgeHorizontal, 4, 4},
    {WedgeOblique27, 4, 2},  {WedgeOblique27, 4, 6},  {WedgeOblique153, 4, 2}, {WedgeOblique153, 4, 6},
    {WedgeOblique63, 2, 4},  {WedgeOblique63, 6, 4},  {WedgeOblique117, 2, 4}, {WedgeOblique117, 6, 4}};

const WedgeCodeType wedgeCodebook16Heqw[16] = {
    {WedgeOblique27, 4, 4},  {WedgeOblique63, 4, 4},  {WedgeOblique117, 4, 4}, {WedgeOblique153, 4, 4},
    {WedgeHorizontal, 4, 2}, {WedgeHorizontal, 4, 6}, {WedgeVertical, 2, 4},   {WedgeVertical, 6, 4},
    {WedgeOblique27, 4, 2},  {WedgeOblique27, 4, 6},  {WedgeOblique153, 4, 2}, {WedgeOblique153, 4, 6},
    {WedgeOblique63, 2, 4},  {WedgeOblique63, 6, 4},  {WedgeOblique117, 2, 4}, {WedgeOblique117, 6, 4}};

// wedge_params_lookup, inter_prediction.c:2001-:2024 (bits, codebook,
// signflip row, masks pointer; the port's masks is the flat pointer-table
// form described at the struct). Rows in BlockSize order,
// definitions.h:883-:904.
const WedgeParamsType wedgeParamsLookup[kBlockSizesAll] = {
    {0, nullptr, nullptr, &wedgeMasks[0][0][0]},                             // BLOCK_4X4
    {0, nullptr, nullptr, &wedgeMasks[1][0][0]},                             // BLOCK_4X8
    {0, nullptr, nullptr, &wedgeMasks[2][0][0]},                             // BLOCK_8X4
    {4, wedgeCodebook16Heqw, wedgeSignflipLookup[3], &wedgeMasks[3][0][0]},  // BLOCK_8X8
    {4, wedgeCodebook16Hgtw, wedgeSignflipLookup[4], &wedgeMasks[4][0][0]},  // BLOCK_8X16
    {4, wedgeCodebook16Hltw, wedgeSignflipLookup[5], &wedgeMasks[5][0][0]},  // BLOCK_16X8
    {4, wedgeCodebook16Heqw, wedgeSignflipLookup[6], &wedgeMasks[6][0][0]},  // BLOCK_16X16
    {4, wedgeCodebook16Hgtw, wedgeSignflipLookup[7], &wedgeMasks[7][0][0]},  // BLOCK_16X32
    {4, wedgeCodebook16Hltw, wedgeSignflipLookup[8], &wedgeMasks[8][0][0]},  // BLOCK_32X16
    {4, wedgeCodebook16Heqw, wedgeSignflipLookup[9], &wedgeMasks[9][0][0]},  // BLOCK_32X32
    {0, nullptr, nullptr, &wedgeMasks[10][0][0]},                            // BLOCK_32X64
    {0, nullptr, nullptr, &wedgeMasks[11][0][0]},                            // BLOCK_64X32
    {0, nullptr, nullptr, &wedgeMasks[12][0][0]},                            // BLOCK_64X64
    {0, nullptr, nullptr, &wedgeMasks[13][0][0]},                            // BLOCK_64X128
    {0, nullptr, nullptr, &wedgeMasks[14][0][0]},                            // BLOCK_128X64
    {0, nullptr, nullptr, &wedgeMasks[15][0][0]},                            // BLOCK_128X128
    {0, nullptr, nullptr, &wedgeMasks[16][0][0]},                            // BLOCK_4X16
    {0, nullptr, nullptr, &wedgeMasks[17][0][0]},                            // BLOCK_16X4
    {4, wedgeCodebook16Hgtw, wedgeSignflipLookup[18], &wedgeMasks[18][0][0]}, // BLOCK_8X32
    {4, wedgeCodebook16Hltw, wedgeSignflipLookup[19], &wedgeMasks[19][0][0]}, // BLOCK_32X8
    {0, nullptr, nullptr, &wedgeMasks[20][0][0]},                            // BLOCK_16X64
    {0, nullptr, nullptr, &wedgeMasks[21][0][0]},                            // BLOCK_64X16
};

// svt_aom_is_interintra_wedge_used, inter_prediction.c:2026-:2028.
bool isInterIntraWedgeUsed(int bsize) {
    return wedgeParamsLookup[bsize].bits > 0;
}

// svt_aom_get_wedge_bits_lookup, inter_prediction.c:2030-:2032.
std::int32_t getWedgeBitsLookup(int bsize) {
    return wedgeParamsLookup[bsize].bits;
}

// svt_aom_get_wedge_params_bits, inter_prediction.c:2064-:2066.
std::int32_t getWedgeParamsBits(int bsize) {
    return wedgeParamsLookup[bsize].bits;
}

// ---------- T3: the sr convolve family ----------

// clip_pixel_highbd(val, 8), definitions.h:704-:710 at bd=8 (the 8-bit branch).
inline std::uint8_t clipPixelHighbd8(std::int32_t val) {
    return static_cast<std::uint8_t>(clampInt(val, 0, 255));
}

// clip_pixel_highbd(val, bd), definitions.h:704-:710 (the bd-parameterized form).
inline std::int32_t clipPixelHighbd(std::int32_t val, std::int32_t bd) {
    return clampInt(val, 0, (1 << bd) - 1);
}

// get_msb, portable #else body (definitions.h:628-:643): floor(log2(n)),
// n != 0. l13-palette precedent (the per-module local copy).
inline std::int32_t getMsb(std::uint32_t n) {
    assert(n != 0);
    std::int32_t log = 0;
    std::uint32_t value = n;
    for (std::int32_t i = 4; i >= 0; --i) {
        const std::uint32_t x = value >> (1 << i);
        if (x != 0) {
            value = x;
            log += (1 << i);
        }
    }
    return log;
}

// svt_av1_convolve_2d_sr_c, inter_prediction.c:332-:375.
void convolve2dSr(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst, std::int32_t dstStride,
                  std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                  const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                  ConvolveParams* convParams) {
    std::int16_t imBlock[(convolve::maxSbSize + kMaxFilterTap - 1) * convolve::maxSbSize];
    std::int32_t imH = h + filterParamsY->taps - 1;
    std::int32_t imStride = w;
    const std::int32_t foVert = filterParamsY->taps / 2 - 1;
    const std::int32_t foHoriz = filterParamsX->taps / 2 - 1;
    const std::int32_t bd = 8;
    const std::int32_t bits = convolve::filterBits * 2 - convParams->round0 - convParams->round1;

    // horizontal filter
    const std::uint8_t* srcHoriz = src - foVert * srcStride;
    const std::int16_t* xFilter = convolve::getInterpFilterSubpelKernel(*filterParamsX, subpelXQ4 & convolve::subpelMask);
    for (std::int32_t y = 0; y < imH; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t sum = (1 << (bd + convolve::filterBits - 1));
            for (std::int32_t k = 0; k < filterParamsX->taps; ++k) {
                sum += xFilter[k] * srcHoriz[y * srcStride + x - foHoriz + k];
            }
            assert(0 <= sum && sum < (1 << (bd + convolve::filterBits + 1)));
            imBlock[y * imStride + x] = static_cast<std::int16_t>(roundPowerOfTwo(sum, convParams->round0));
        }
    }

    // vertical filter
    std::int16_t* srcVert = imBlock + foVert * imStride;
    const std::int16_t* yFilter = convolve::getInterpFilterSubpelKernel(*filterParamsY, subpelYQ4 & convolve::subpelMask);
    const std::int32_t offsetBits = bd + 2 * convolve::filterBits - convParams->round0;
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t sum = 1 << offsetBits;
            for (std::int32_t k = 0; k < filterParamsY->taps; ++k) {
                sum += yFilter[k] * srcVert[(y - foVert + k) * imStride + x];
            }
            assert(0 <= sum && sum < (1 << (offsetBits + 2)));
            std::int16_t res = static_cast<std::uint16_t>(roundPowerOfTwo(sum, convParams->round1) -
                ((1 << (offsetBits - convParams->round1)) + (1 << (offsetBits - convParams->round1 - 1))));
            dst[y * dstStride + x] = clipPixelHighbd8(roundPowerOfTwo(res, bits));
        }
    }
}

// svt_av1_convolve_y_sr_c, inter_prediction.c:377-:403.
void convolveYSr(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst, std::int32_t dstStride,
                 std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                 const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                 ConvolveParams* convParams) {
    assert(filterParamsY);
    const std::int32_t foVert = filterParamsY->taps / 2 - 1;
    (void)filterParamsX;
    (void)subpelXQ4;
    (void)convParams; // the reference voids conv_params: rounding is FILTER_BITS-hardwired here
    assert(convParams->round0 <= convolve::filterBits);
    assert(convParams->round0 + convParams->round1 <= convolve::filterBits + 1 ||
           convParams->round0 + convParams->round1 == convolve::filterBits * 2);
    const std::int16_t* yFilter = convolve::getInterpFilterSubpelKernel(*filterParamsY, subpelYQ4 & convolve::subpelMask);
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t sum = 0;
            for (std::int32_t k = 0; k < filterParamsY->taps; ++k) {
                sum += yFilter[k] * src[(y - foVert + k) * srcStride + x];
            }
            dst[y * dstStride + x] = clipPixelHighbd8(roundPowerOfTwo(sum, convolve::filterBits));
        }
    }
}

// svt_av1_convolve_x_sr_c, inter_prediction.c:405-:432.
void convolveXSR(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst, std::int32_t dstStride,
                 std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                 const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                 ConvolveParams* convParams) {
    const std::int32_t foHoriz = filterParamsX->taps / 2 - 1;
    const std::int32_t bits = convolve::filterBits - convParams->round0;
    (void)filterParamsY;
    (void)subpelYQ4;
    (void)convParams;
    assert(bits >= 0);
    assert((convolve::filterBits - convParams->round1) >= 0 ||
           convParams->round0 + convParams->round1 == convolve::filterBits * 2);
    const std::int16_t* xFilter = convolve::getInterpFilterSubpelKernel(*filterParamsX, subpelXQ4 & convolve::subpelMask);
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t sum = 0;
            for (std::int32_t k = 0; k < filterParamsX->taps; ++k) {
                sum += xFilter[k] * src[y * srcStride + x - foHoriz + k];
            }
            sum = roundPowerOfTwo(sum, convParams->round0);
            dst[y * dstStride + x] = clipPixelHighbd8(roundPowerOfTwo(sum, bits));
        }
    }
}

// svt_av1_convolve_2d_copy_sr_c, inter_prediction.c:434-:449.
void convolve2dCopySr(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst, std::int32_t dstStride,
                      std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                      const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                      ConvolveParams* convParams) {
    (void)filterParamsX;
    (void)filterParamsY;
    (void)subpelXQ4;
    (void)subpelYQ4;
    (void)convParams;
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            dst[y * dstStride + x] = src[y * srcStride + x];
        }
    }
}

// svt_av1_convolve_2d_scale_c, inter_prediction.c:451-:527. The reference's
// SCALE_SUBPEL_BITS=10 stepping (x_step_qn/y_step_qn); the compound branches
// are carried verbatim (isCompound + doAverage).
void convolve2dScale(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst8, std::int32_t dst8Stride,
                     std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                     const InterpFilterParams* filterParamsY, std::int32_t subpelXQn, std::int32_t xStepQn,
                     std::int32_t subpelYQn, std::int32_t yStepQn, ConvolveParams* convParams) {
    std::int16_t imBlock[(2 * convolve::maxSbSize + kMaxFilterTap) * convolve::maxSbSize];
    const std::int32_t imH = (((h - 1) * yStepQn + subpelYQn) >> kScaleSubpelBits) + filterParamsY->taps;
    ConvBufType* dst16 = convParams->dst;
    const std::int32_t dst16Stride = convParams->dstStride;
    const std::int32_t bits = convolve::filterBits * 2 - convParams->round0 - convParams->round1;
    assert(bits >= 0);
    const std::int32_t imStride = w;
    const std::int32_t foVert = filterParamsY->taps / 2 - 1;
    const std::int32_t foHoriz = filterParamsX->taps / 2 - 1;
    const std::int32_t bd = 8;

    // horizontal filter
    const std::uint8_t* srcHoriz = src - foVert * srcStride;
    for (std::int32_t y = 0; y < imH; ++y) {
        std::int32_t xQn = subpelXQn;
        for (std::int32_t x = 0; x < w; ++x, xQn += xStepQn) {
            const std::uint8_t* srcX = &srcHoriz[xQn >> kScaleSubpelBits];
            const std::int32_t xFilterIdx =
                (xQn & kScaleSubpelMask) >> kScaleSubpelBits;
            assert(xFilterIdx < convolve::subpelShifts);
            const std::int16_t* xFilter =
                convolve::getInterpFilterSubpelKernel(*filterParamsX, xFilterIdx);
            std::int32_t sum = (1 << (bd + convolve::filterBits - 1));
            for (std::int32_t k = 0; k < filterParamsX->taps; ++k) {
                sum += xFilter[k] * srcX[k - foHoriz];
            }
            assert(0 <= sum && sum < (1 << (bd + convolve::filterBits + 1)));
            imBlock[y * imStride + x] = static_cast<std::int16_t>(roundPowerOfTwo(sum, convParams->round0));
        }
        srcHoriz += srcStride;
    }

    // vertical filter
    std::int16_t* srcVert = imBlock + foVert * imStride;
    const std::int32_t offsetBits = bd + 2 * convolve::filterBits - convParams->round0;
    for (std::int32_t x = 0; x < w; ++x) {
        std::int32_t yQn = subpelYQn;
        for (std::int32_t y = 0; y < h; ++y, yQn += yStepQn) {
            const std::int16_t* srcY = &srcVert[(yQn >> kScaleSubpelBits) * imStride];
            const std::int32_t yFilterIdx =
                (yQn & kScaleSubpelMask) >> kScaleSubpelBits;
            assert(yFilterIdx < convolve::subpelShifts);
            const std::int16_t* yFilter =
                convolve::getInterpFilterSubpelKernel(*filterParamsY, yFilterIdx);
            std::int32_t sum = 1 << offsetBits;
            for (std::int32_t k = 0; k < filterParamsY->taps; ++k) {
                sum += yFilter[k] * srcY[(k - foVert) * imStride];
            }
            assert(0 <= sum && sum < (1 << (offsetBits + 2)));
            ConvBufType res = static_cast<ConvBufType>(roundPowerOfTwo(sum, convParams->round1));
            if (convParams->isCompound) {
                if (convParams->doAverage) {
                    std::int32_t tmp = dst16[y * dst16Stride + x];
                    if (convParams->useDistWtdCompAvg) {
                        tmp = tmp * convParams->fwdOffset + res * convParams->bckOffset;
                        tmp = tmp >> kDiffFactorLog2;
                    } else {
                        tmp += res;
                        tmp = tmp >> 1;
                    }
                    tmp = tmp -
                        ((1 << (offsetBits - convParams->round1)) + (1 << (offsetBits - convParams->round1 - 1)));
                    dst8[y * dst8Stride + x] = clipPixelHighbd8(roundPowerOfTwo(tmp, bits));
                } else {
                    dst16[y * dst16Stride + x] = res;
                }
            } else {
                std::int32_t tmp = res -
                    ((1 << (offsetBits - convParams->round1)) + (1 << (offsetBits - convParams->round1 - 1)));
                dst8[y * dst8Stride + x] = clipPixelHighbd8(roundPowerOfTwo(tmp, bits));
            }
        }
        ++srcVert;
    }
}

// ---------- A3E S2: the tail utilities ----------

// svt_aom_sum_squares_i16_c, inter_prediction.c:2533-:2541.
std::uint64_t sumSquaresI16(const std::int16_t* src, std::uint32_t n) {
    std::uint64_t ss = 0;
    do {
        const std::int16_t v = *src++;
        ss += (std::uint64_t)v * v;
    } while (--n);

    return ss;
}

// svt_av1_wedge_sse_from_residuals_c, inter_prediction.c:2468-:2477.
// MAX_MASK_VALUE = 1 << WEDGE_WEIGHT_BITS (ip:2438).
std::uint64_t wedgeSseFromResiduals(const std::int16_t* r1, const std::int16_t* d, const std::uint8_t* m, int n) {
    std::uint64_t csse = 0;

    for (int i = 0; i < n; i++) {
        std::int32_t t = (1 << kWedgeWeightBits) * r1[i] + m[i] * d[i];
        t = clampInt(t, INT16_MIN, INT16_MAX);
        csse += (std::uint64_t)(t * t);
    }
    return roundPowerOfTwo64((std::int64_t)csse, 2 * kWedgeWeightBits);
}

// obmc_mask_N[overlap_position], inter_prediction.c:2543-:2554.
const std::uint8_t obmcMask1[1] = {64};
const std::uint8_t obmcMask2[2] = {45, 64};
const std::uint8_t obmcMask4[4] = {39, 50, 59, 64};
const std::uint8_t obmcMask8[8] = {36, 42, 48, 53, 57, 61, 64, 64};
const std::uint8_t obmcMask16[16] = {34, 37, 40, 43, 46, 49, 52, 54, 56, 58, 60, 61, 64, 64, 64, 64};
const std::uint8_t obmcMask32[32] = {33, 35, 36, 38, 40, 41, 43, 44, 45, 47, 48, 50, 51, 52, 53, 55,
                                     56, 57, 58, 59, 60, 60, 61, 62, 64, 64, 64, 64, 64, 64, 64, 64};

// svt_av1_get_obmc_mask, inter_prediction.c:2556-:2574.
const std::uint8_t* getObmcMask(int length) {
    switch (length) {
    case 1: return obmcMask1;
    case 2: return obmcMask2;
    case 4: return obmcMask4;
    case 8: return obmcMask8;
    case 16: return obmcMask16;
    case 32: return obmcMask32;
    default:
        assert(0);
        return nullptr;
    }
}

// svt_aom_mode_context_analyzer, inter_prediction.c:2576-:2592. The
// NEWMV/REFMV ctx bitfield splits (definitions.h:1324-:1331):
// GLOBALMV_OFFSET 3, NEWMV_CTX_MASK 7, REFMV_OFFSET 4, REFMV_CTX_MASK 15,
// COMP_NEWMV_CTXS 5; the compound map is verbatim.
std::int16_t modeContextAnalyzer(std::int16_t modeContext, const std::int32_t* rf) {
    static const unsigned compoundModeCtxMap[3][5] = {
        {0, 1, 1, 1, 1},
        {1, 2, 3, 4, 4},
        {4, 4, 5, 6, 7},
    };

    if (rf[1] <= 0 /* INTRA_FRAME */) {
        return modeContext;
    }

    const unsigned newmvCtx = (unsigned)modeContext & 7;
    const unsigned refmvCtx = ((unsigned)modeContext >> 4) & 15;
    assert((refmvCtx >> 1) < 3);
    const unsigned compCtx = compoundModeCtxMap[refmvCtx >> 1][newmvCtx < 5 ? newmvCtx : 4];
    return (std::int16_t)compCtx;
}

// svt_aom_find_ref_dv, inter_prediction.c:2401-:2412. MI_SIZE = 4
// (definitions.h:354), INTRABC_DELAY_PIXELS = 256 (inter_prediction.h:35).
void findRefDv(Mv* refDv, int miRowStart, int mibSize, int miRow) {
    if (miRow - mibSize < miRowStart) {
        refDv->y = 0;
        refDv->x = (std::int16_t)(-4 * mibSize - 256);
    } else {
        refDv->y = (std::int16_t)(-4 * mibSize);
        refDv->x = 0;
    }
    refDv->y = (std::int16_t)(refDv->y * 8);
    refDv->x = (std::int16_t)(refDv->x * 8);
}

// get_plane_block_size, common_utils.h:135-:140, via the local
// svt_aom_ss_size_lookup transcription (common_utils.c:236-:258). The
// BLOCK_INVALID value is 255 (definitions.h:907).
const std::uint8_t ssSizeLookup[22][2][2] = {
    {{0, 0}, {0, 0}},       {{1, 0}, {255, 0}},     {{2, 255}, {0, 0}},     {{3, 2}, {1, 0}},
    {{4, 3}, {255, 1}},     {{5, 255}, {3, 2}},     {{6, 5}, {4, 3}},       {{7, 6}, {255, 4}},
    {{8, 255}, {6, 5}},     {{9, 8}, {7, 6}},       {{10, 9}, {255, 7}},    {{11, 255}, {9, 8}},
    {{12, 11}, {10, 9}},    {{13, 12}, {255, 10}},  {{14, 255}, {12, 11}},  {{15, 14}, {13, 12}},
    {{16, 1}, {255, 1}},    {{17, 255}, {2, 2}},    {{18, 4}, {255, 16}},   {{19, 255}, {5, 17}},
    {{20, 7}, {255, 18}},   {{21, 255}, {8, 19}},
};

inline int getPlaneBlockSize(int bsize, int ssx, int ssy) {
    return ssSizeLookup[bsize][ssx][ssy];
}

// svt_av1_skip_u4x4_pred_in_obmc, inter_prediction.c:2414-:2436.
int skipU4x4PredInObmc(int bsize, int dir, int subsamplingX, int subsamplingY) {
    const int bsizePlane = getPlaneBlockSize(bsize, subsamplingX, subsamplingY);
    switch (bsizePlane) {
    // DISABLE_CHROMA_U8X8_OBMC = 0 (inter_prediction.h:41): the one-sided
    // branch (dir == 0 skips).
    case 0: // BLOCK_4X4
    case 2: // BLOCK_8X4
    case 1: // BLOCK_4X8
        return dir == 0;
    default:
        return 0;
    }
}

// svt_aom_scale_chroma_bsize, intra_prediction.c:2492-:2544. The enum
// values thread as the 0..21 indices (BLOCK_8X8 = 3, BLOCK_8X4 = 2,
// BLOCK_4X8 = 1, BLOCK_8X16 = 4, BLOCK_16X8 = 5).
int scaleChromaBsize(int bsize, int subsamplingX, int subsamplingY) {
    int bs = bsize;
    switch (bsize) {
    case 0: // BLOCK_4X4
        if (subsamplingX == 1 && subsamplingY == 1) {
            bs = 3;
        } else if (subsamplingX == 1) {
            bs = 2;
        } else if (subsamplingY == 1) {
            bs = 1;
        }
        break;
    case 1: // BLOCK_4X8
        if (subsamplingX == 1 && subsamplingY == 1) {
            bs = 3;
        } else if (subsamplingX == 1) {
            bs = 3;
        } else if (subsamplingY == 1) {
            bs = 1;
        }
        break;
    case 2: // BLOCK_8X4
        if (subsamplingX == 1 && subsamplingY == 1) {
            bs = 3;
        } else if (subsamplingX == 1) {
            bs = 2;
        } else if (subsamplingY == 1) {
            bs = 3;
        }
        break;
    case 16: // BLOCK_4X16
        if (subsamplingX == 1 && subsamplingY == 1) {
            bs = 4;
        } else if (subsamplingX == 1) {
            bs = 4;
        } else if (subsamplingY == 1) {
            bs = 16;
        }
        break;
    case 17: // BLOCK_16X4
        if (subsamplingX == 1 && subsamplingY == 1) {
            bs = 5;
        } else if (subsamplingX == 1) {
            bs = 17;
        } else if (subsamplingY == 1) {
            bs = 5;
        }
        break;
    default:
        break;
    }
    return bs;
}

// ---------- A3E W1: the wedge mask synthesis (functions) ----------

// shift_copy, inter_prediction.c:2053-:2062.
void shiftCopy(const std::uint8_t* src, std::uint8_t* dst, int shift, int width) {
    if (shift >= 0) {
        std::memcpy(dst + shift, src, width - shift);
        std::memset(dst, src[0], shift);
    } else {
        shift = -shift;
        std::memcpy(dst, src + shift, width - shift);
        std::memset(dst + width - shift, src[width - 1], shift);
    }
}

// aom_convolve_copy_c, inter_prediction.c:2038-:2051 (filter args voided).
void aomConvolveCopyC(const std::uint8_t* src, std::ptrdiff_t srcStride, std::uint8_t* dst, std::ptrdiff_t dstStride,
                      int w, int h) {
    for (int r = h; r > 0; --r) {
        std::memcpy(dst, src, w);
        src += srcStride;
        dst += dstStride;
    }
}

// init_wedge_primary_masks, inter_prediction.c:2077-:2130 (the
// USE_PRECOMPUTED_WEDGE_MASK branch; the tanh fallback :2098-:2113 is the
// #else arm, not compiled in the pinned build).
void initWedgePrimaryMasks() {
    const int w = kMaskPrimarySize;
    const int h = kMaskPrimarySize;
    const int stride = kMaskPrimaryStride;
    // Note: index [0] stores the primary, and [1] its complement.
    // Generate prototype by shifting the primary
    int shift = h / 4;
    for (int i = 0; i < h; i += 2) {
        shiftCopy(wedgePrimaryObliqueEven, &wedgeMaskObl[0][WedgeOblique63][i * stride], shift, kMaskPrimarySize);
        shift--;
        shiftCopy(wedgePrimaryObliqueOdd, &wedgeMaskObl[0][WedgeOblique63][(i + 1) * stride], shift,
                  kMaskPrimarySize);
        std::memcpy(&wedgeMaskObl[0][WedgeVertical][i * stride], wedgePrimaryVertical,
                    kMaskPrimarySize * sizeof(wedgePrimaryVertical[0]));
        std::memcpy(&wedgeMaskObl[0][WedgeVertical][(i + 1) * stride], wedgePrimaryVertical,
                    kMaskPrimarySize * sizeof(wedgePrimaryVertical[0]));
    }
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            const int msk = wedgeMaskObl[0][WedgeOblique63][i * stride + j];
            wedgeMaskObl[0][WedgeOblique27][j * stride + i] = (std::uint8_t)msk;
            wedgeMaskObl[0][WedgeOblique117][i * stride + w - 1 - j] = wedgeMaskObl[0][WedgeOblique153][(w - 1 - j) * stride + i] =
                (std::uint8_t)((1 << kWedgeWeightBits) - msk);
            wedgeMaskObl[1][WedgeOblique63][i * stride + j] = wedgeMaskObl[1][WedgeOblique27][j * stride + i] =
                (std::uint8_t)((1 << kWedgeWeightBits) - msk);
            wedgeMaskObl[1][WedgeOblique117][i * stride + w - 1 - j] =
                wedgeMaskObl[1][WedgeOblique153][(w - 1 - j) * stride + i] = (std::uint8_t)msk;
            const int mskx = wedgeMaskObl[0][WedgeVertical][i * stride + j];
            wedgeMaskObl[0][WedgeHorizontal][j * stride + i] = (std::uint8_t)mskx;
            wedgeMaskObl[1][WedgeVertical][i * stride + j] = wedgeMaskObl[1][WedgeHorizontal][j * stride + i] =
                (std::uint8_t)((1 << kWedgeWeightBits) - mskx);
        }
    }
}

// get_wedge_mask_inplace, inter_prediction.c:2173-:2186.
const std::uint8_t* getWedgeMaskInplace(int wedgeIndex, int neg, int bsize) {
    const int bh = kBlockSizeHigh[bsize];
    const int bw = kBlockSizeWide[bsize];

    assert(wedgeIndex >= 0 && wedgeIndex < (1 << getWedgeBitsLookup(bsize)));
    const WedgeCodeType* a = wedgeParamsLookup[bsize].codebook + wedgeIndex;
    const std::uint8_t wsignflip = wedgeParamsLookup[bsize].signflip[wedgeIndex];

    const int woff = (a->xOffset * bw) >> 3;
    const int hoff = (a->yOffset * bh) >> 3;
    return &wedgeMaskObl[neg ^ wsignflip][a->direction][kMaskPrimaryStride * (kMaskPrimarySize / 2 - hoff) +
                                                       kMaskPrimarySize / 2 - woff];
}

// init_wedge_masks, inter_prediction.c:2188-:2214 (file-local static in
// the reference; named initWedgeMaskTable here to avoid clashing with the
// public svt_av1_init_wedge_masks entry).
void initWedgeMaskTable() {
    std::uint8_t* dst = wedgeMaskBuf;
    std::memset(wedgeMasks, 0, sizeof(wedgeMasks));
    for (int bsize = 0; bsize < kBlockSizesAll; ++bsize) {
        const int bw = kBlockSizeWide[bsize];
        const int bh = kBlockSizeHigh[bsize];
        const WedgeParamsType* wedgeParams = &wedgeParamsLookup[bsize];
        const int wbits = wedgeParams->bits;
        const int wtypes = 1 << wbits;
        if (wbits == 0) {
            continue;
        }
        for (int w = 0; w < wtypes; ++w) {
            const std::uint8_t* mask = getWedgeMaskInplace(w, 0, bsize);
            aomConvolveCopyC(mask, kMaskPrimaryStride, dst, bw, bw, bh);
            wedgeMasks[bsize][0][w] = dst;
            dst += bw * bh;

            mask = getWedgeMaskInplace(w, 1, bsize);
            aomConvolveCopyC(mask, kMaskPrimaryStride, dst, bw, bw, bh);
            wedgeMasks[bsize][1][w] = dst;
            dst += bw * bh;
        }
        assert(sizeof(wedgeMaskBuf) >= (size_t)(dst - wedgeMaskBuf));
    }
}

// svt_av1_init_wedge_masks, inter_prediction.c:2217-:2223.
// Equation of line: f(x, y) = a[0]*(x - a[2]*w/8) + a[1]*(y - a[3]*h/8) = 0
void initWedgeMasks() {
    initWedgePrimaryMasks();
    // #if !USE_PRECOMPUTED_WEDGE_SIGN -> init_wedge_signs(): compiled out
    // of the pinned build (USE_PRECOMPUTED_WEDGE_SIGN = 1, :1525).
    initWedgeMaskTable();
}

// svt_aom_get_contiguous_soft_mask, inter_prediction.c:2034-:2036. The
// reference reads masks[wedge_sign][wedge_index] over WedgeMasksType
// (uint8_t*[MAX_WEDGE_TYPES]); the port's flat index is
// sign * kMaxWedgeTypes + wedge_index.
const std::uint8_t* getContiguousSoftMask(int wedgeIndex, int wedgeSign, int bsize) {
    return wedgeParamsLookup[bsize].masks[wedgeSign * kMaxWedgeTypes + wedgeIndex];
}

// Test-only read-only view of wedge_mask_obl (see the header note).
const std::uint8_t* wedgeMaskOblData() {
    return &wedgeMaskObl[0][0][0];
}

// ---------- A3E J1: the jnt (compound) convolve family ----------

// svt_av1_jnt_convolve_2d_c, inter_prediction.c:529-:585. Same arithmetic as
// convolve2dSr through the vertical pass; the tail composes against the
// conv_params->dst CONV_BUF plane (use_jnt_comp_avg weighted or plain
// average) instead of the single-ref offset subtract.
void jntConvolve2d(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst8, std::int32_t dst8Stride,
                   std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                   const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                   ConvolveParams* convParams) {
    ConvBufType* dst = convParams->dst;
    std::int32_t dstStride = convParams->dstStride;
    std::int16_t imBlock[(convolve::maxSbSize + kMaxFilterTap - 1) * convolve::maxSbSize];
    std::int32_t imH = h + filterParamsY->taps - 1;
    std::int32_t imStride = w;
    const std::int32_t foVert = filterParamsY->taps / 2 - 1;
    const std::int32_t foHoriz = filterParamsX->taps / 2 - 1;
    const std::int32_t bd = 8;
    const std::int32_t roundBits = 2 * convolve::filterBits - convParams->round0 - convParams->round1;

    // horizontal filter
    const std::uint8_t* srcHoriz = src - foVert * srcStride;
    const std::int16_t* xFilter = convolve::getInterpFilterSubpelKernel(*filterParamsX, subpelXQ4 & convolve::subpelMask);
    for (std::int32_t y = 0; y < imH; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t sum = (1 << (bd + convolve::filterBits - 1));
            for (std::int32_t k = 0; k < filterParamsX->taps; ++k) {
                sum += xFilter[k] * srcHoriz[y * srcStride + x - foHoriz + k];
            }
            assert(0 <= sum && sum < (1 << (bd + convolve::filterBits + 1)));
            imBlock[y * imStride + x] = static_cast<std::int16_t>(roundPowerOfTwo(sum, convParams->round0));
        }
    }

    // vertical filter
    std::int16_t* srcVert = imBlock + foVert * imStride;
    const std::int16_t* yFilter = convolve::getInterpFilterSubpelKernel(*filterParamsY, subpelYQ4 & convolve::subpelMask);
    const std::int32_t offsetBits = bd + 2 * convolve::filterBits - convParams->round0;
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t sum = 1 << offsetBits;
            for (std::int32_t k = 0; k < filterParamsY->taps; ++k) {
                sum += yFilter[k] * srcVert[(y - foVert + k) * imStride + x];
            }
            assert(0 <= sum && sum < (1 << (offsetBits + 2)));
            ConvBufType res = static_cast<ConvBufType>(roundPowerOfTwo(sum, convParams->round1));
            if (convParams->doAverage) {
                std::int32_t tmp = dst[y * dstStride + x];
                if (convParams->useJntCompAvg) {
                    tmp = tmp * convParams->fwdOffset + res * convParams->bckOffset;
                    tmp = tmp >> kDistPrecisionBits;
                } else {
                    tmp += res;
                    tmp = tmp >> 1;
                }
                tmp -= (1 << (offsetBits - convParams->round1)) + (1 << (offsetBits - convParams->round1 - 1));
                dst8[y * dst8Stride + x] = clipPixelHighbd8(roundPowerOfTwo(tmp, roundBits));
            } else {
                dst[y * dstStride + x] = res;
            }
        }
    }
}

// svt_av1_jnt_convolve_y_c, inter_prediction.c:587-:630. Single-axis
// vertical; res *= (1 << bits) (bits = FILTER_BITS - round_0) BEFORE
// ROUND_POWER_OF_TWO(res, round_1), then + round_offset (compose and
// subtract it again) - the order quirk the y/x twins carry and 2d does not.
void jntConvolveY(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst8, std::int32_t dst8Stride,
                  std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                  const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                  ConvolveParams* convParams) {
    ConvBufType* dst = convParams->dst;
    std::int32_t dstStride = convParams->dstStride;
    const std::int32_t foVert = filterParamsY->taps / 2 - 1;
    const std::int32_t bits = convolve::filterBits - convParams->round0;
    const std::int32_t bd = 8;
    const std::int32_t offsetBits = bd + 2 * convolve::filterBits - convParams->round0;
    const std::int32_t roundOffset = (1 << (offsetBits - convParams->round1)) +
        (1 << (offsetBits - convParams->round1 - 1));
    const std::int32_t roundBits = 2 * convolve::filterBits - convParams->round0 - convParams->round1;
    (void)filterParamsX;
    (void)subpelXQ4;

    // vertical filter
    const std::int16_t* yFilter = convolve::getInterpFilterSubpelKernel(*filterParamsY, subpelYQ4 & convolve::subpelMask);
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t res = 0;
            for (std::int32_t k = 0; k < filterParamsY->taps; ++k) {
                res += yFilter[k] * src[(y - foVert + k) * srcStride + x];
            }
            res *= (1 << bits);
            res = roundPowerOfTwo(res, convParams->round1) + roundOffset;

            if (convParams->doAverage) {
                std::int32_t tmp = dst[y * dstStride + x];
                if (convParams->useJntCompAvg) {
                    tmp = tmp * convParams->fwdOffset + res * convParams->bckOffset;
                    tmp = tmp >> kDistPrecisionBits;
                } else {
                    tmp += res;
                    tmp = tmp >> 1;
                }
                tmp -= roundOffset;
                dst8[y * dst8Stride + x] = clipPixelHighbd8(roundPowerOfTwo(tmp, roundBits));
            } else {
                dst[y * dstStride + x] = static_cast<ConvBufType>(res);
            }
        }
    }
}

// svt_av1_jnt_convolve_x_c, inter_prediction.c:632-:675. Single-axis
// horizontal; res = (1 << bits) * ROUND(res, round_0) (bits =
// FILTER_BITS - round_1) FIRST, then + round_offset - the mirror order of
// the y twin, pinned by the two oracle order fixtures.
void jntConvolveX(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst8, std::int32_t dst8Stride,
                  std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                  const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                  ConvolveParams* convParams) {
    ConvBufType* dst = convParams->dst;
    std::int32_t dstStride = convParams->dstStride;
    const std::int32_t foHoriz = filterParamsX->taps / 2 - 1;
    const std::int32_t bits = convolve::filterBits - convParams->round1;
    const std::int32_t bd = 8;
    const std::int32_t offsetBits = bd + 2 * convolve::filterBits - convParams->round0;
    const std::int32_t roundOffset = (1 << (offsetBits - convParams->round1)) +
        (1 << (offsetBits - convParams->round1 - 1));
    const std::int32_t roundBits = 2 * convolve::filterBits - convParams->round0 - convParams->round1;
    (void)filterParamsY;
    (void)subpelYQ4;

    // horizontal filter
    const std::int16_t* xFilter = convolve::getInterpFilterSubpelKernel(*filterParamsX, subpelXQ4 & convolve::subpelMask);
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t res = 0;
            for (std::int32_t k = 0; k < filterParamsX->taps; ++k) {
                res += xFilter[k] * src[y * srcStride + x - foHoriz + k];
            }
            res = (1 << bits) * roundPowerOfTwo(res, convParams->round0);
            res += roundOffset;

            if (convParams->doAverage) {
                std::int32_t tmp = dst[y * dstStride + x];
                if (convParams->useJntCompAvg) {
                    tmp = tmp * convParams->fwdOffset + res * convParams->bckOffset;
                    tmp = tmp >> kDistPrecisionBits;
                } else {
                    tmp += res;
                    tmp = tmp >> 1;
                }
                tmp -= roundOffset;
                dst8[y * dst8Stride + x] = clipPixelHighbd8(roundPowerOfTwo(tmp, roundBits));
            } else {
                dst[y * dstStride + x] = static_cast<ConvBufType>(res);
            }
        }
    }
}

// svt_av1_jnt_convolve_2d_copy_c, inter_prediction.c:677-:714. The copy
// twin: res = src << bits with bits = 2*FILTER_BITS - round_1 - round_0,
// + round_offset (ConvBufType arithmetic, the wrap the compose reads back).
void jntConvolve2dCopy(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst8, std::int32_t dst8Stride,
                       std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                       const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                       ConvolveParams* convParams) {
    ConvBufType* dst = convParams->dst;
    std::int32_t dstStride = convParams->dstStride;
    const std::int32_t bits = convolve::filterBits * 2 - convParams->round1 - convParams->round0;
    const std::int32_t bd = 8;
    const std::int32_t offsetBits = bd + 2 * convolve::filterBits - convParams->round0;
    const std::int32_t roundOffset = (1 << (offsetBits - convParams->round1)) +
        (1 << (offsetBits - convParams->round1 - 1));
    (void)filterParamsX;
    (void)filterParamsY;
    (void)subpelXQ4;
    (void)subpelYQ4;

    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            ConvBufType res = static_cast<ConvBufType>(src[y * srcStride + x] << bits);
            res += static_cast<ConvBufType>(roundOffset);

            if (convParams->doAverage) {
                std::int32_t tmp = dst[y * dstStride + x];
                if (convParams->useJntCompAvg) {
                    tmp = tmp * convParams->fwdOffset + res * convParams->bckOffset;
                    tmp = tmp >> kDistPrecisionBits;
                } else {
                    tmp += res;
                    tmp = tmp >> 1;
                }
                tmp -= roundOffset;
                dst8[y * dst8Stride + x] = clipPixelHighbd8(roundPowerOfTwo(tmp, bits));
            } else {
                dst[y * dstStride + x] = res;
            }
        }
    }
}

// svt_av1_highbd_convolve_x_sr_c, inter_prediction.c:734-:759. The clip is
// bd-parameterized (clipPixelHighbd), the 8-bit twin's is hardwired 8.
void highbdConvolveXsr(const std::uint16_t* src, std::int32_t srcStride, std::uint16_t* dst, std::int32_t dstStride,
                       std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                       const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                       ConvolveParams* convParams, std::int32_t bd) {
    const std::int32_t foHoriz = filterParamsX->taps / 2 - 1;
    const std::int32_t bits = convolve::filterBits - convParams->round0;
    (void)filterParamsY;
    (void)subpelYQ4;

    assert(bits >= 0);
    assert((convolve::filterBits - convParams->round1) >= 0 ||
           ((convParams->round0 + convParams->round1) == 2 * convolve::filterBits));

    // horizontal filter
    const std::int16_t* xFilter = convolve::getInterpFilterSubpelKernel(*filterParamsX, subpelXQ4 & convolve::subpelMask);
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t res = 0;
            for (std::int32_t k = 0; k < filterParamsX->taps; ++k) {
                res += xFilter[k] * src[y * srcStride + x - foHoriz + k];
            }
            res = roundPowerOfTwo(res, convParams->round0);
            dst[y * dstStride + x] = static_cast<std::uint16_t>(clipPixelHighbd(roundPowerOfTwo(res, bits), bd));
        }
    }
}

// svt_av1_highbd_convolve_y_sr_c, inter_prediction.c:761-:785.
void highbdConvolveYsr(const std::uint16_t* src, std::int32_t srcStride, std::uint16_t* dst, std::int32_t dstStride,
                       std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                       const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                       ConvolveParams* convParams, std::int32_t bd) {
    assert(filterParamsY);
    const std::int32_t foVert = filterParamsY->taps / 2 - 1;
    (void)filterParamsX;
    (void)subpelXQ4;
    (void)convParams;

    assert(convParams->round0 <= convolve::filterBits);
    assert(((convParams->round0 + convParams->round1) <= (convolve::filterBits + 1)) ||
           ((convParams->round0 + convParams->round1) == (2 * convolve::filterBits)));
    // vertical filter
    const std::int16_t* yFilter = convolve::getInterpFilterSubpelKernel(*filterParamsY, subpelYQ4 & convolve::subpelMask);
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t res = 0;
            for (std::int32_t k = 0; k < filterParamsY->taps; ++k) {
                res += yFilter[k] * src[(y - foVert + k) * srcStride + x];
            }
            dst[y * dstStride + x] =
                static_cast<std::uint16_t>(clipPixelHighbd(roundPowerOfTwo(res, convolve::filterBits), bd));
        }
    }
}

// svt_av1_highbd_convolve_2d_sr_c, inter_prediction.c:787-:829. The uint16
// intermediate store and the int32 res are pinned by the J2 oracle.
void highbdConvolve2dSr(const std::uint16_t* src, std::int32_t srcStride, std::uint16_t* dst, std::int32_t dstStride,
                        std::int32_t w, std::int32_t h, const InterpFilterParams* filterParamsX,
                        const InterpFilterParams* filterParamsY, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                        ConvolveParams* convParams, std::int32_t bd) {
    std::int16_t imBlock[(convolve::maxSbSize + kMaxFilterTap - 1) * convolve::maxSbSize];
    std::int32_t imH = h + filterParamsY->taps - 1;
    std::int32_t imStride = w;
    const std::int32_t foVert = filterParamsY->taps / 2 - 1;
    const std::int32_t foHoriz = filterParamsX->taps / 2 - 1;
    const std::int32_t bits = convolve::filterBits * 2 - convParams->round0 - convParams->round1;
    assert(bits >= 0);

    // horizontal filter
    const std::uint16_t* srcHoriz = src - foVert * srcStride;
    const std::int16_t* xFilter = convolve::getInterpFilterSubpelKernel(*filterParamsX, subpelXQ4 & convolve::subpelMask);
    for (std::int32_t y = 0; y < imH; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t sum = (1 << (bd + convolve::filterBits - 1));
            for (std::int32_t k = 0; k < filterParamsX->taps; ++k) {
                sum += xFilter[k] * srcHoriz[y * srcStride + x - foHoriz + k];
            }
            assert(0 <= sum && sum < (1 << (bd + convolve::filterBits + 1)));
            imBlock[y * imStride + x] = static_cast<std::int16_t>(static_cast<ConvBufType>(roundPowerOfTwo(sum, convParams->round0)));
        }
    }

    // vertical filter
    std::int16_t* srcVert = imBlock + foVert * imStride;
    const std::int16_t* yFilter = convolve::getInterpFilterSubpelKernel(*filterParamsY, subpelYQ4 & convolve::subpelMask);
    const std::int32_t offsetBits = bd + 2 * convolve::filterBits - convParams->round0;
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t sum = 1 << offsetBits;
            for (std::int32_t k = 0; k < filterParamsY->taps; ++k) {
                sum += yFilter[k] * srcVert[(y - foVert + k) * imStride + x];
            }
            assert(0 <= sum && sum < (1 << (offsetBits + 2)));
            std::int32_t res = roundPowerOfTwo(sum, convParams->round1) -
                ((1 << (offsetBits - convParams->round1)) + (1 << (offsetBits - convParams->round1 - 1)));
            dst[y * dstStride + x] =
                static_cast<std::uint16_t>(clipPixelHighbd(roundPowerOfTwo(res, bits), bd));
        }
    }
}

// svt_av1_highbd_convolve_2d_copy_sr_c, inter_prediction.c:716-:732.
void highbdConvolve2dCopySr(const std::uint16_t* src, std::int32_t srcStride, std::uint16_t* dst,
                            std::int32_t dstStride, std::int32_t w, std::int32_t h,
                            const InterpFilterParams* filterParamsX, const InterpFilterParams* filterParamsY,
                            std::int32_t subpelXQ4, std::int32_t subpelYQ4, ConvolveParams* convParams,
                            std::int32_t bd) {
    (void)filterParamsX;
    (void)filterParamsY;
    (void)subpelXQ4;
    (void)subpelYQ4;
    (void)convParams;
    (void)bd;

    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            dst[y * dstStride + x] = src[y * srcStride + x];
        }
    }
}

// svt_av1_highbd_jnt_convolve_x_c, inter_prediction.c:908-:951. Same
// arithmetic as the 8-bit jntConvolveX; the OUTPUT is the dst16 parameter
// (clip at bd) and offset_bits/round_offset carry bd.
void highbdJntConvolveX(const std::uint16_t* src, std::int32_t srcStride, std::uint16_t* dst16,
                        std::int32_t dst16Stride, std::int32_t w, std::int32_t h,
                        const InterpFilterParams* filterParamsX, const InterpFilterParams* filterParamsY,
                        std::int32_t subpelXQ4, std::int32_t subpelYQ4, ConvolveParams* convParams,
                        std::int32_t bd) {
    ConvBufType* dst = convParams->dst;
    std::int32_t dstStride = convParams->dstStride;
    const std::int32_t foHoriz = filterParamsX->taps / 2 - 1;
    const std::int32_t bits = convolve::filterBits - convParams->round1;
    const std::int32_t offsetBits = bd + 2 * convolve::filterBits - convParams->round0;
    const std::int32_t roundOffset = (1 << (offsetBits - convParams->round1)) +
        (1 << (offsetBits - convParams->round1 - 1));
    const std::int32_t roundBits = 2 * convolve::filterBits - convParams->round0 - convParams->round1;
    assert(roundBits >= 0);
    (void)filterParamsY;
    (void)subpelYQ4;
    assert(bits >= 0);
    // horizontal filter
    const std::int16_t* xFilter = convolve::getInterpFilterSubpelKernel(*filterParamsX, subpelXQ4 & convolve::subpelMask);
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t res = 0;
            for (std::int32_t k = 0; k < filterParamsX->taps; ++k) {
                res += xFilter[k] * src[y * srcStride + x - foHoriz + k];
            }
            res = (1 << bits) * roundPowerOfTwo(res, convParams->round0);
            res += roundOffset;

            if (convParams->doAverage) {
                std::int32_t tmp = dst[y * dstStride + x];
                if (convParams->useJntCompAvg) {
                    tmp = tmp * convParams->fwdOffset + res * convParams->bckOffset;
                    tmp = tmp >> kDistPrecisionBits;
                } else {
                    tmp += res;
                    tmp = tmp >> 1;
                }
                tmp -= roundOffset;
                dst16[y * dst16Stride + x] = static_cast<std::uint16_t>(clipPixelHighbd(roundPowerOfTwo(tmp, roundBits), bd));
            } else {
                dst[y * dstStride + x] = static_cast<ConvBufType>(res);
            }
        }
    }
}

// svt_av1_highbd_jnt_convolve_y_c, inter_prediction.c:953-:996. Scale
// (1 << bits) BEFORE round_1, + round_offset (bd-carried), compose, subtract.
void highbdJntConvolveY(const std::uint16_t* src, std::int32_t srcStride, std::uint16_t* dst16,
                        std::int32_t dst16Stride, std::int32_t w, std::int32_t h,
                        const InterpFilterParams* filterParamsX, const InterpFilterParams* filterParamsY,
                        std::int32_t subpelXQ4, std::int32_t subpelYQ4, ConvolveParams* convParams,
                        std::int32_t bd) {
    ConvBufType* dst = convParams->dst;
    std::int32_t dstStride = convParams->dstStride;
    const std::int32_t foVert = filterParamsY->taps / 2 - 1;
    const std::int32_t bits = convolve::filterBits - convParams->round0;
    const std::int32_t offsetBits = bd + 2 * convolve::filterBits - convParams->round0;
    const std::int32_t roundOffset = (1 << (offsetBits - convParams->round1)) +
        (1 << (offsetBits - convParams->round1 - 1));
    const std::int32_t roundBits = 2 * convolve::filterBits - convParams->round0 - convParams->round1;
    assert(roundBits >= 0);
    (void)filterParamsX;
    (void)subpelXQ4;
    assert(bits >= 0);
    // vertical filter
    const std::int16_t* yFilter = convolve::getInterpFilterSubpelKernel(*filterParamsY, subpelYQ4 & convolve::subpelMask);
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t res = 0;
            for (std::int32_t k = 0; k < filterParamsY->taps; ++k) {
                res += yFilter[k] * src[(y - foVert + k) * srcStride + x];
            }
            res *= (1 << bits);
            res = roundPowerOfTwo(res, convParams->round1) + roundOffset;

            if (convParams->doAverage) {
                std::int32_t tmp = dst[y * dstStride + x];
                if (convParams->useJntCompAvg) {
                    tmp = tmp * convParams->fwdOffset + res * convParams->bckOffset;
                    tmp = tmp >> kDistPrecisionBits;
                } else {
                    tmp += res;
                    tmp = tmp >> 1;
                }
                tmp -= roundOffset;
                dst16[y * dst16Stride + x] = static_cast<std::uint16_t>(clipPixelHighbd(roundPowerOfTwo(tmp, roundBits), bd));
            } else {
                dst[y * dstStride + x] = static_cast<ConvBufType>(res);
            }
        }
    }
}

// svt_av1_highbd_jnt_convolve_2d_copy_c, inter_prediction.c:998-:1035.
void highbdJntConvolve2dCopy(const std::uint16_t* src, std::int32_t srcStride, std::uint16_t* dst16,
                             std::int32_t dst16Stride, std::int32_t w, std::int32_t h,
                             const InterpFilterParams* filterParamsX, const InterpFilterParams* filterParamsY,
                             std::int32_t subpelXQ4, std::int32_t subpelYQ4, ConvolveParams* convParams,
                             std::int32_t bd) {
    ConvBufType* dst = convParams->dst;
    std::int32_t dstStride = convParams->dstStride;
    const std::int32_t bits = convolve::filterBits * 2 - convParams->round1 - convParams->round0;
    const std::int32_t offsetBits = bd + 2 * convolve::filterBits - convParams->round0;
    const std::int32_t roundOffset = (1 << (offsetBits - convParams->round1)) +
        (1 << (offsetBits - convParams->round1 - 1));
    assert(bits >= 0);
    (void)filterParamsX;
    (void)filterParamsY;
    (void)subpelXQ4;
    (void)subpelYQ4;

    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            ConvBufType res = static_cast<ConvBufType>(src[y * srcStride + x] << bits);
            res += static_cast<ConvBufType>(roundOffset);
            if (convParams->doAverage) {
                std::int32_t tmp = dst[y * dstStride + x];
                if (convParams->useJntCompAvg) {
                    tmp = tmp * convParams->fwdOffset + res * convParams->bckOffset;
                    tmp = tmp >> kDistPrecisionBits;
                } else {
                    tmp += res;
                    tmp = tmp >> 1;
                }
                tmp -= roundOffset;
                dst16[y * dst16Stride + x] = static_cast<std::uint16_t>(clipPixelHighbd(roundPowerOfTwo(tmp, bits), bd));
            } else {
                dst[y * dstStride + x] = res;
            }
        }
    }
}

// svt_av1_highbd_jnt_convolve_2d_c, inter_prediction.c:1037-:1097. The
// bd-carried bias (1 << (bd + FB - 1)) distinguishes the hbd 2d form from
// its 8-bit twin (hardwired bd=8 there); staging via ConvBufType in the
// int16 im_block, exactly the reference's cast chain.
void highbdJntConvolve2d(const std::uint16_t* src, std::int32_t srcStride, std::uint16_t* dst16,
                         std::int32_t dst16Stride, std::int32_t w, std::int32_t h,
                         const InterpFilterParams* filterParamsX, const InterpFilterParams* filterParamsY,
                         std::int32_t subpelXQ4, std::int32_t subpelYQ4, ConvolveParams* convParams,
                         std::int32_t bd) {
    std::int16_t imBlock[(convolve::maxSbSize + kMaxFilterTap - 1) * convolve::maxSbSize];
    ConvBufType* dst = convParams->dst;
    std::int32_t dstStride = convParams->dstStride;
    std::int32_t imH = h + filterParamsY->taps - 1;
    std::int32_t imStride = w;
    const std::int32_t foVert = filterParamsY->taps / 2 - 1;
    const std::int32_t foHoriz = filterParamsX->taps / 2 - 1;

    const std::int32_t roundBits = 2 * convolve::filterBits - convParams->round0 - convParams->round1;
    assert(roundBits >= 0);

    // horizontal filter
    const std::uint16_t* srcHoriz = src - foVert * srcStride;
    const std::int16_t* xFilter = convolve::getInterpFilterSubpelKernel(*filterParamsX, subpelXQ4 & convolve::subpelMask);
    for (std::int32_t y = 0; y < imH; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t sum = (1 << (bd + convolve::filterBits - 1));
            for (std::int32_t k = 0; k < filterParamsX->taps; ++k) {
                sum += xFilter[k] * srcHoriz[y * srcStride + x - foHoriz + k];
            }
            assert(0 <= sum && sum < (1 << (bd + convolve::filterBits + 1)));
            (void)bd;
            imBlock[y * imStride + x] =
                static_cast<std::int16_t>(static_cast<ConvBufType>(roundPowerOfTwo(sum, convParams->round0)));
        }
    }

    // vertical filter
    std::int16_t* srcVert = imBlock + foVert * imStride;
    const std::int32_t offsetBits = bd + 2 * convolve::filterBits - convParams->round0;
    const std::int16_t* yFilter = convolve::getInterpFilterSubpelKernel(*filterParamsY, subpelYQ4 & convolve::subpelMask);
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t sum = 1 << offsetBits;
            for (std::int32_t k = 0; k < filterParamsY->taps; ++k) {
                sum += yFilter[k] * srcVert[(y - foVert + k) * imStride + x];
            }
            assert(0 <= sum && sum < (1 << (offsetBits + 2)));
            ConvBufType res = static_cast<ConvBufType>(roundPowerOfTwo(sum, convParams->round1));
            if (convParams->doAverage) {
                std::int32_t tmp = dst[y * dstStride + x];
                if (convParams->useJntCompAvg) {
                    tmp = tmp * convParams->fwdOffset + res * convParams->bckOffset;
                    tmp = tmp >> kDistPrecisionBits;
                } else {
                    tmp += res;
                    tmp = tmp >> 1;
                }
                tmp -= (1 << (offsetBits - convParams->round1)) + (1 << (offsetBits - convParams->round1 - 1));
                dst16[y * dst16Stride + x] =
                    static_cast<std::uint16_t>(clipPixelHighbd(roundPowerOfTwo(tmp, roundBits), bd));
            } else {
                dst[y * dstStride + x] = res;
            }
        }
    }
}

// svt_av1_highbd_convolve_2d_scale_c, inter_prediction.c:831-:906. The
// SCALE_SUBPEL_BITS=10 stepping (x_step_qn/y_step_qn); the compound
// branches carry dst16 in conv_params (use_dist_wtd_comp_avg or plain
// average), all clipped at bd (clipPixelHighbd).
void highbdConvolve2dScale(const std::uint16_t* src, std::int32_t srcStride, std::uint16_t* dst,
                           std::int32_t dstStride, std::int32_t w, std::int32_t h,
                           const InterpFilterParams* filterParamsX, const InterpFilterParams* filterParamsY,
                           std::int32_t subpelXQn, std::int32_t xStepQn, std::int32_t subpelYQn, std::int32_t yStepQn,
                           ConvolveParams* convParams, std::int32_t bd) {
    std::int16_t imBlock[(2 * convolve::maxSbSize + kMaxFilterTap) * convolve::maxSbSize];
    const std::int32_t imH = (((h - 1) * yStepQn + subpelYQn) >> kScaleSubpelBits) + filterParamsY->taps;
    std::int32_t imStride = w;
    const std::int32_t foVert = filterParamsY->taps / 2 - 1;
    const std::int32_t foHoriz = filterParamsX->taps / 2 - 1;
    ConvBufType* dst16 = convParams->dst;
    const std::int32_t dst16Stride = convParams->dstStride;
    const std::int32_t bits = convolve::filterBits * 2 - convParams->round0 - convParams->round1;
    assert(bits >= 0);
    // horizontal filter
    const std::uint16_t* srcHoriz = src - foVert * srcStride;
    for (std::int32_t y = 0; y < imH; ++y) {
        std::int32_t xQn = subpelXQn;
        for (std::int32_t x = 0; x < w; ++x, xQn += xStepQn) {
            const std::uint16_t* srcX = &srcHoriz[xQn >> kScaleSubpelBits];
            const std::int32_t xFilterIdx = (xQn & kScaleSubpelMask) >> kScaleExtraBits;
            assert(xFilterIdx < convolve::subpelShifts);
            const std::int16_t* xFilter = convolve::getInterpFilterSubpelKernel(*filterParamsX, xFilterIdx);
            std::int32_t sum = (1 << (bd + convolve::filterBits - 1));
            for (std::int32_t k = 0; k < filterParamsX->taps; ++k) {
                sum += xFilter[k] * srcX[k - foHoriz];
            }
            assert(0 <= sum && sum < (1 << (bd + convolve::filterBits + 1)));
            imBlock[y * imStride + x] = static_cast<std::int16_t>(roundPowerOfTwo(sum, convParams->round0));
        }
        srcHoriz += srcStride;
    }

    // vertical filter
    std::int16_t* srcVert = imBlock + foVert * imStride;
    const std::int32_t offsetBits = bd + 2 * convolve::filterBits - convParams->round0;
    for (std::int32_t x = 0; x < w; ++x) {
        std::int32_t yQn = subpelYQn;
        for (std::int32_t y = 0; y < h; ++y, yQn += yStepQn) {
            const std::int16_t* srcY = &srcVert[(yQn >> kScaleSubpelBits) * imStride];
            const std::int32_t yFilterIdx = (yQn & kScaleSubpelMask) >> kScaleExtraBits;
            assert(yFilterIdx < convolve::subpelShifts);
            const std::int16_t* yFilter = convolve::getInterpFilterSubpelKernel(*filterParamsY, yFilterIdx);
            std::int32_t sum = 1 << offsetBits;
            for (std::int32_t k = 0; k < filterParamsY->taps; ++k) {
                sum += yFilter[k] * srcY[(k - foVert) * imStride];
            }
            assert(0 <= sum && sum < (1 << (offsetBits + 2)));
            ConvBufType res = static_cast<ConvBufType>(roundPowerOfTwo(sum, convParams->round1));
            if (convParams->isCompound) {
                if (convParams->doAverage) {
                    std::int32_t tmp = dst16[y * dst16Stride + x];
                    if (convParams->useDistWtdCompAvg) {
                        tmp = tmp * convParams->fwdOffset + res * convParams->bckOffset;
                        tmp = tmp >> kDiffFactorLog2;
                    } else {
                        tmp += res;
                        tmp = tmp >> 1;
                    }
                    // Subtract round offset and convolve round
                    tmp = tmp -
                        ((1 << (offsetBits - convParams->round1)) + (1 << (offsetBits - convParams->round1 - 1)));
                    dst[y * dstStride + x] =
                        static_cast<std::uint16_t>(clipPixelHighbd(roundPowerOfTwo(tmp, bits), bd));
                } else {
                    dst16[y * dst16Stride + x] = res;
                }
            } else {
                // Subtract round offset and convolve round
                std::int32_t tmp = res -
                    ((1 << (offsetBits - convParams->round1)) + (1 << (offsetBits - convParams->round1 - 1)));
                dst[y * dstStride + x] = static_cast<std::uint16_t>(clipPixelHighbd(roundPowerOfTwo(tmp, bits), bd));
            }
        }
        ++srcVert;
    }
}

// convolve_2d_for_intrabc, inter_prediction.c:1200-:1241. The NULL filter
// pointer slots are only read when the matching subpel is nonzero, and the
// dispatch is: both subpels -> 2d_sr, x only -> x_sr, else y_sr.
void convolve2dForIntrabc(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst, std::int32_t dstStride,
                          std::int32_t w, std::int32_t h, std::int32_t subpelXQ4, std::int32_t subpelYQ4,
                          ConvolveParams* convParams) {
    const InterpFilterParams* filterParamsX =
        subpelXQ4 ? &convolve::interpFilterParamsList[static_cast<int>(convolve::InterpFilter::BILINEAR)] : nullptr;
    const InterpFilterParams* filterParamsY =
        subpelYQ4 ? &convolve::interpFilterParamsList[static_cast<int>(convolve::InterpFilter::BILINEAR)] : nullptr;
    if (subpelXQ4 != 0 && subpelYQ4 != 0) {
        convolve2dSr(src, srcStride, dst, dstStride, w, h, filterParamsX, filterParamsY, 8, 8, convParams);
    } else if (subpelXQ4 != 0) {
        convolveXSR(src, srcStride, dst, dstStride, w, h, filterParamsX, filterParamsY, 8, 0, convParams);
    } else {
        convolveYSr(src, srcStride, dst, dstStride, w, h, filterParamsX, filterParamsY, 0, 8, convParams);
    }
}

// highbd_convolve_2d_for_intrabc, inter_prediction.c:1243-:1257.
void highbdConvolve2dForIntrabc(const std::uint16_t* src, std::int32_t srcStride, std::uint16_t* dst,
                                std::int32_t dstStride, std::int32_t w, std::int32_t h, std::int32_t subpelXQ4,
                                std::int32_t subpelYQ4, ConvolveParams* convParams, std::int32_t bd) {
    const InterpFilterParams* filterParamsX =
        subpelXQ4 ? &convolve::interpFilterParamsList[static_cast<int>(convolve::InterpFilter::BILINEAR)] : nullptr;
    const InterpFilterParams* filterParamsY =
        subpelYQ4 ? &convolve::interpFilterParamsList[static_cast<int>(convolve::InterpFilter::BILINEAR)] : nullptr;
    if (subpelXQ4 != 0 && subpelYQ4 != 0) {
        highbdConvolve2dSr(src, srcStride, dst, dstStride, w, h, filterParamsX, filterParamsY, 8, 8, convParams, bd);
    } else if (subpelXQ4 != 0) {
        highbdConvolveXsr(src, srcStride, dst, dstStride, w, h, filterParamsX, filterParamsY, 8, 0, convParams, bd);
    } else {
        highbdConvolveYsr(src, srcStride, dst, dstStride, w, h, filterParamsX, filterParamsY, 0, 8, convParams, bd);
    }
}

// svt_highbd_inter_predictor, inter_prediction.c:1455-:1523. Direct-call
// dispatch of the convolveHbd table: [0][0][0] copy, [0][1][0] y, [1][0][0]
// x, [1][1][0] 2d, [0][0][1] jnt_2d_copy, [0][1][1] jnt_y, [1][0][1] jnt_x,
// [1][1][1] jnt_2d; the intrabc and scaled arms route as in the reference.
void highbdInterPredictor(const std::uint16_t* src, std::int32_t srcStride, std::uint16_t* dst, std::int32_t dstStride,
                          const SubpelParams* subpelParams, const ScaleFactors* sf, std::int32_t w, std::int32_t h,
                          ConvolveParams* convParams, std::uint32_t interpFilters, std::int32_t isIntrabc,
                          std::int32_t bd) {
    InterpFilterParams filterParamsX, filterParamsY;
    const std::int32_t isScaled = hasScale(subpelParams->xs, subpelParams->ys);

    convolve::getConvolveFilterParams(interpFilters, &filterParamsX, &filterParamsY, w, h);

    assert(convParams->doAverage == 0 || convParams->doAverage == 1);
    assert(sf);
    (void)sf; // assert-only under NDEBUG; the scaled arms consume xs/ys instead
    assert(!(isIntrabc && isScaled));

    if (isScaled) {
        if (isIntrabc && (subpelParams->subpelX != 0 || subpelParams->subpelY != 0)) {
            highbdConvolve2dForIntrabc(src, srcStride, dst, dstStride, w, h, subpelParams->subpelX,
                                       subpelParams->subpelY, convParams, bd);
            return;
        }
        if (convParams->isCompound) {
            assert(convParams->dst != nullptr);
        }
        highbdConvolve2dScale(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY,
                              subpelParams->subpelX, subpelParams->xs, subpelParams->subpelY, subpelParams->ys,
                              convParams, bd);
    } else {
        SubpelParams sp = *subpelParams;
        revertScaleExtraBits(&sp);

        if (isIntrabc && (sp.subpelX != 0 || sp.subpelY != 0)) {
            highbdConvolve2dForIntrabc(src, srcStride, dst, dstStride, w, h, sp.subpelX, sp.subpelY, convParams, bd);
            return;
        }

        if (convParams->isCompound) {
            if (sp.subpelX != 0 && sp.subpelY != 0) {
                highbdJntConvolve2d(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, sp.subpelX,
                                    sp.subpelY, convParams, bd);
            } else if (sp.subpelX != 0) {
                highbdJntConvolveX(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, sp.subpelX, 0,
                                   convParams, bd);
            } else if (sp.subpelY != 0) {
                highbdJntConvolveY(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, 0,
                                   sp.subpelY, convParams, bd);
            } else {
                highbdJntConvolve2dCopy(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, 0, 0,
                                        convParams, bd);
            }
        } else {
            if (sp.subpelX != 0 && sp.subpelY != 0) {
                highbdConvolve2dSr(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, sp.subpelX,
                                   sp.subpelY, convParams, bd);
            } else if (sp.subpelX != 0) {
                highbdConvolveXsr(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, sp.subpelX, 0,
                                  convParams, bd);
            } else if (sp.subpelY != 0) {
                highbdConvolveYsr(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, 0, sp.subpelY,
                                  convParams, bd);
            } else {
                highbdConvolve2dCopySr(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, 0, 0,
                                       convParams, bd);
            }
        }
    }
}

// svt_inter_predictor_pd0, inter_prediction.c:1261-:1286. The unscaled arm
// indexes svt_aom_convolve[0][0][is_compound] with all-zero filter/subpel
// arguments - the [0][0] slots ignore them - so the direct-call dispatch
// passes the same zeros (nullptr filters).
void interPredictorPd0(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst, std::int32_t dstStride,
                       std::int32_t w, std::int32_t h, SubpelParams* subpelParams, ConvolveParams* convParams) {
    const std::int32_t isScaled = hasScale(subpelParams->xs, subpelParams->ys);
    if (isScaled) {
        InterpFilterParams filterParamsX, filterParamsY;
        convolve::getConvolveFilterParams(
            convolve::makeInterpFilters(convolve::InterpFilter::EIGHTTAP_REGULAR, convolve::InterpFilter::EIGHTTAP_REGULAR),
            &filterParamsX, &filterParamsY, w, h);
        convolve2dScale(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, subpelParams->subpelX,
                        subpelParams->xs, subpelParams->subpelY, subpelParams->ys, convParams);
    } else {
        (void)subpelParams;
        if (convParams->isCompound) {
            jntConvolve2dCopy(src, srcStride, dst, dstStride, w, h, nullptr, nullptr, 0, 0, convParams);
        } else {
            convolve2dCopySr(src, srcStride, dst, dstStride, w, h, nullptr, nullptr, 0, 0, convParams);
        }
    }
}

// svt_inter_predictor_light_pd1, inter_prediction.c:1288-:1395.
void interPredictorLightPd1(const std::uint8_t* src, const std::uint8_t* src2b, std::int32_t srcStride,
                            std::uint8_t* dst, std::int32_t dstStride, std::int32_t w, std::int32_t h,
                            std::uint32_t interpFilters, SubpelParams* subpelParams, ConvolveParams* convParams,
                            std::int32_t bd, Pack2dFn pack2d) {
    InterpFilterParams filterParamsX, filterParamsY;
    convolve::getConvolveFilterParams(interpFilters, &filterParamsX, &filterParamsY, w, h);
    const std::int32_t isScaled = hasScale(subpelParams->xs, subpelParams->ys);

    if (bd > 8) {
        // for super-res, the reference frame block might be 2x than
        // predictor in maximum; for reference scaling, 4x (both dims).
        alignas(16) std::uint16_t src16[(definitionsPackediBufferSize) * 4];
        (void)src16;
        std::int32_t srcStride16;
        // pack the reference into temp 16bit buffer
        const std::uint8_t offset = 8; // INTERPOLATION_OFFSET, definitions.h:344
        std::uint32_t widthScale = 1;
        std::uint32_t heightScale = 1;
        if (isScaled) {
            widthScale = subpelParams->xs != kScaleSubpelShifts ? 2 : 1;
            heightScale = subpelParams->ys != kScaleSubpelShifts ? 2 : 1;
        }
        // optimize stride from MAX_SB_SIZE to bwidth to minimum the block buffer size
        srcStride16 = w * widthScale + (offset << 1);
        // 16-byte align of src16
        if (srcStride16 % 8) {
            srcStride16 = ((srcStride16) + ((1 << (3)) - 1)) & ~((1 << (3)) - 1); // ALIGN_POWER_OF_TWO, :996
        }

        packBlock(src - offset - (offset * srcStride), srcStride, src2b - offset - (offset * srcStride), srcStride,
                  src16, srcStride16, w * widthScale + (offset << 1), h * heightScale + (offset << 1), pack2d);
        std::uint16_t* src10b = src16 + offset + (offset * srcStride16);
        std::uint16_t* dst16 = (std::uint16_t*)dst;

        if (isScaled) {
            highbdConvolve2dScale(src10b, srcStride16, dst16, dstStride, w, h, &filterParamsX, &filterParamsY,
                                  subpelParams->subpelX, subpelParams->xs, subpelParams->subpelY, subpelParams->ys,
                                  convParams, bd);
        } else {
            SubpelParams sp = *subpelParams;
            revertScaleExtraBits(&sp);
            // convolveHbd[sx != 0][sy != 0][is_compound] as direct calls.
            if (convParams->isCompound) {
                if (sp.subpelX != 0 && sp.subpelY != 0) {
                    highbdJntConvolve2d(src10b, srcStride16, dst16, dstStride, w, h, &filterParamsX, &filterParamsY,
                                        sp.subpelX, sp.subpelY, convParams, bd);
                } else if (sp.subpelX != 0) {
                    highbdJntConvolveX(src10b, srcStride16, dst16, dstStride, w, h, &filterParamsX, &filterParamsY,
                                       sp.subpelX, 0, convParams, bd);
                } else if (sp.subpelY != 0) {
                    highbdJntConvolveY(src10b, srcStride16, dst16, dstStride, w, h, &filterParamsX, &filterParamsY, 0,
                                       sp.subpelY, convParams, bd);
                } else {
                    highbdJntConvolve2dCopy(src10b, srcStride16, dst16, dstStride, w, h, &filterParamsX,
                                            &filterParamsY, 0, 0, convParams, bd);
                }
            } else {
                if (sp.subpelX != 0 && sp.subpelY != 0) {
                    highbdConvolve2dSr(src10b, srcStride16, dst16, dstStride, w, h, &filterParamsX, &filterParamsY,
                                       sp.subpelX, sp.subpelY, convParams, bd);
                } else if (sp.subpelX != 0) {
                    highbdConvolveXsr(src10b, srcStride16, dst16, dstStride, w, h, &filterParamsX, &filterParamsY,
                                      sp.subpelX, 0, convParams, bd);
                } else if (sp.subpelY != 0) {
                    highbdConvolveYsr(src10b, srcStride16, dst16, dstStride, w, h, &filterParamsX, &filterParamsY, 0,
                                      sp.subpelY, convParams, bd);
                } else {
                    highbdConvolve2dCopySr(src10b, srcStride16, dst16, dstStride, w, h, &filterParamsX,
                                           &filterParamsY, 0, 0, convParams, bd);
                }
            }
        }
    } else {
        (void)src2b;
        if (isScaled) {
            convolve2dScale(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY,
                            subpelParams->subpelX, subpelParams->xs, subpelParams->subpelY, subpelParams->ys,
                            convParams);
        } else {
            SubpelParams sp = *subpelParams;
            revertScaleExtraBits(&sp);
            // svt_aom_convolve[sx != 0][sy != 0][is_compound] as direct calls.
            if (convParams->isCompound) {
                if (sp.subpelX != 0 && sp.subpelY != 0) {
                    jntConvolve2d(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, sp.subpelX,
                                  sp.subpelY, convParams);
                } else if (sp.subpelX != 0) {
                    jntConvolveX(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, sp.subpelX, 0,
                                 convParams);
                } else if (sp.subpelY != 0) {
                    jntConvolveY(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, 0, sp.subpelY,
                                 convParams);
                } else {
                    jntConvolve2dCopy(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, 0, 0,
                                      convParams);
                }
            } else {
                if (sp.subpelX != 0 && sp.subpelY != 0) {
                    convolve2dSr(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, sp.subpelX,
                                 sp.subpelY, convParams);
                } else if (sp.subpelX != 0) {
                    convolveXSR(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, sp.subpelX, 0,
                                convParams);
                } else if (sp.subpelY != 0) {
                    convolveYSr(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, 0, sp.subpelY,
                                convParams);
                } else {
                    convolve2dCopySr(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, 0, 0,
                                     convParams);
                }
            }
        }
    }
}

// svt_inter_predictor, inter_prediction.c:1397-:1453. The svt_aom_convolve
// function-pointer table (:1115, set at :1118-:1129) dispatches as direct
// calls: [0][0][0] copy_sr, [0][1][0] y_sr, [1][0][0] x_sr, [1][1][0] 2d_sr;
// the four compound jnt slots park per the header's named deviation.
void interPredictor(const std::uint8_t* src, std::int32_t srcStride, std::uint8_t* dst, std::int32_t dstStride,
                    const SubpelParams* subpelParams, const ScaleFactors* sf, std::int32_t w, std::int32_t h,
                    ConvolveParams* convParams, std::uint32_t interpFilters, std::int32_t isIntrabc) {
    InterpFilterParams filterParamsX, filterParamsY;
    const std::int32_t isScaled = hasScale(subpelParams->xs, subpelParams->ys);

    convolve::getConvolveFilterParams(interpFilters, &filterParamsX, &filterParamsY, w, h);

    assert(convParams->doAverage == 0 || convParams->doAverage == 1);
    assert(sf);
    assert(!(isIntrabc && isScaled));
    (void)sf; // assert-only under NDEBUG; the scaled arms consume xs/ys instead

    if (isScaled) {
        if (isIntrabc && (subpelParams->subpelX != 0 || subpelParams->subpelY != 0)) {
            convolve2dForIntrabc(src, srcStride, dst, dstStride, w, h, subpelParams->subpelX, subpelParams->subpelY,
                                 convParams);
            return;
        }
        if (convParams->isCompound) {
            assert(convParams->dst != nullptr);
        }
        convolve2dScale(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, subpelParams->subpelX,
                        subpelParams->xs, subpelParams->subpelY, subpelParams->ys, convParams);
    } else {
        SubpelParams sp = *subpelParams;
        revertScaleExtraBits(&sp);

        if (isIntrabc && (sp.subpelX != 0 || sp.subpelY != 0)) {
            convolve2dForIntrabc(src, srcStride, dst, dstStride, w, h, sp.subpelX, sp.subpelY, convParams);
            return;
        }

        if (sp.subpelX != 0 && sp.subpelY != 0) {
            convolve2dSr(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, sp.subpelX,
                         sp.subpelY, convParams);
        } else if (sp.subpelX != 0) {
            convolveXSR(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, sp.subpelX, 0,
                        convParams);
        } else if (sp.subpelY != 0) {
            convolveYSr(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, 0, sp.subpelY,
                        convParams);
        } else {
            convolve2dCopySr(src, srcStride, dst, dstStride, w, h, &filterParamsX, &filterParamsY, sp.subpelX,
                             sp.subpelY, convParams);
        }
    }
}

// ---------- T4: struct-free enc helpers (enc_inter_prediction.c) ----------

// svt_av1_wedge_compute_delta_squares_c, enc_inter_prediction.c:375-:381.
// clamp = the reference's utility.h three-argument macro, clampInt above.
void wedgeComputeDeltaSquares(std::int16_t* d, const std::int16_t* a, const std::int16_t* b, int n) {
    for (int i = 0; i < n; ++i) {
        d[i] = static_cast<std::int16_t>(clampInt(a[i] * a[i] - b[i] * b[i], INT16_MIN, INT16_MAX));
    }
}

// svt_av1_wedge_sign_from_residuals_c, enc_inter_prediction.c:414-:422.
std::int8_t wedgeSignFromResiduals(const std::int16_t* ds, const std::uint8_t* m, int n, std::int64_t limit) {
    std::int64_t acc = 0;

    do {
        acc += *ds++ * *m++;
    } while (--n);

    return acc > limit;
}

// svt_aom_highbd_sse_c, enc_inter_prediction.c:600-:613. The reference's
// CONVERT_TO_SHORTPTR cast is the hbd staging entry; the port aliases the
// uint16 buffers through the uint8 parameters per the header contract.
// SQR = utility.h:107 ((x)*(x)): the product is the reference's int multiply,
// widened only at the += into the int64 accumulator.
std::int64_t sseHighbd(const std::uint8_t* a8, int aStride, const std::uint8_t* b8, int bStride, int width,
                       int height) {
    const std::uint16_t* a = (const std::uint16_t*)a8;
    const std::uint16_t* b = (const std::uint16_t*)b8;
    std::int64_t sse = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::int32_t diff = (std::int32_t)a[x] - (std::int32_t)b[x];
            sse += (std::int64_t)(diff * diff);
        }
        a += aStride;
        b += bStride;
    }
    return sse;
}

// svt_aom_sse_c, enc_inter_prediction.c:615-:627. Same SQR discipline.
std::int64_t sse8(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, int width, int height) {
    std::int64_t sse = 0;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::int32_t diff = (std::int32_t)a[x] - (std::int32_t)b[x];
            sse += (std::int64_t)(diff * diff);
        }
        a += aStride;
        b += bStride;
    }
    return sse;
}

// model_rd_norm, enc_inter_prediction.c:1883-:1937. Static in the reference; exported here per the header (the test oracle consumes it directly).
void modelRdNorm(std::int32_t xsqQ10, std::int32_t* rQ10, std::int32_t* dQ10) {
    // NOTE: The tables below must be of the same size.
    // The functions described below are sampled at the four most significant
    // bits of x^2 + 8 / 256.
    // Normalized rate:
    // This table models the rate for a Laplacian source with given variance
    // when quantized with a uniform quantizer with given stepsize. The
    // closed form expression is:
    // Rn(x) = H(sqrt(r)) + sqrt(r)*[1 + H(r)/(1 - r)],
    // where r = exp(-sqrt(2) * x) and x = qpstep / sqrt(variance),
    // and H(x) is the binary entropy function.
    static const std::int32_t rateTabQ10[] = {
        65536, 6086, 5574, 5275, 5063, 4899, 4764, 4651, 4553, 4389, 4255, 4142, 4044, 3958, 3881, 3811, 3748, 3635,
        3538,  3453, 3376, 3307, 3244, 3186, 3133, 3037, 2952, 2877, 2809, 2747, 2690, 2638, 2589, 2501, 2423, 2353,
        2290,  2232, 2179, 2130, 2084, 2001, 1928, 1862, 1802, 1748, 1698, 1651, 1608, 1530, 1460, 1398, 1342, 1290,
        1243,  1199, 1159, 1086, 1021, 963,  911,  864,  821,  781,  745,  680,  623,  574,  530,  490,  455,  424,
        395,   345,  304,  269,  239,  213,  190,  171,  154,  126,  104,  87,   73,   61,   52,   44,   38,   28,
        21,    16,   12,   10,   8,    6,    5,    3,    2,    1,    1,    1,    0,    0,
    };
    // Normalized distortion:
    // This table models the normalized distortion for a Laplacian source
    // with given variance when quantized with a uniform quantizer
    // with given stepsize. The closed form expression is:
    // Dn(x) = 1 - 1/sqrt(2) * x / sinh(x/sqrt(2))
    // where x = qpstep / sqrt(variance).
    // Note the actual distortion is Dn * variance.
    static const std::int32_t distTabQ10[] = {
        0,    0,    1,    1,    1,    2,    2,    2,    3,    3,    4,    5,    5,    6,    7,   7,   8,   9,
        11,   12,   13,   15,   16,   17,   18,   21,   24,   26,   29,   31,   34,   36,   39,  44,  49,  54,
        59,   64,   69,   73,   78,   88,   97,   106,  115,  124,  133,  142,  151,  167,  184, 200, 215, 231,
        245,  260,  274,  301,  327,  351,  375,  397,  418,  439,  458,  495,  528,  559,  587, 613, 637, 659,
        680,  717,  749,  777,  801,  823,  842,  859,  874,  899,  919,  936,  949,  960,  969, 977, 983, 994,
        1001, 1006, 1010, 1013, 1015, 1017, 1018, 1020, 1022, 1022, 1023, 1023, 1023, 1024,
    };
    static const std::int32_t xsqIqQ10[] = {
        0,     4,     8,      12,     16,     20,     24,     28,     32,     40,     48,     56,     64,
        72,    80,    88,     96,     112,    128,    144,    160,    176,    192,    208,    224,    256,
        288,   320,   352,    384,    416,    448,    480,    544,    608,    672,    736,    800,    864,
        928,   992,   1120,   1248,   1376,   1504,   1632,   1760,   1888,   2016,   2272,   2528,   2784,
        3040,  3296,  3552,   3808,   4064,   4576,   5088,   5600,   6112,   6624,   7136,   7648,   8160,
        9184,  10208, 11232,  12256,  13280,  14304,  15328,  16352,  18400,  20448,  22496,  24544,  26592,
        28640, 30688, 32736,  36832,  40928,  45024,  49120,  53216,  57312,  61408,  65504,  73696,  81888,
        90080, 98272, 106464, 114656, 122848, 131040, 147424, 163808, 180192, 196576, 212960, 229344, 245728,
    };
    const std::int32_t tmp     = (xsqQ10 >> 2) + 8;
    const std::int32_t k       = getMsb(tmp) - 3;
    const std::int32_t xq      = (k << 3) + ((tmp >> k) & 0x7);
    const std::int32_t oneQ10 = 1 << 10;
    const std::int32_t aQ10   = ((xsqQ10 - xsqIqQ10[xq]) << 10) >> (2 + k);
    const std::int32_t bQ10   = oneQ10 - aQ10;
    *rQ10                = (rateTabQ10[xq] * bQ10 + rateTabQ10[xq + 1] * aQ10) >> 10;
    *dQ10                = (distTabQ10[xq] * bQ10 + distTabQ10[xq + 1] * aQ10) >> 10;
}

// svt_av1_model_rd_from_var_lapndz, enc_inter_prediction.c:1939-:1958.
void modelRdFromVarLapndz(std::int64_t var, std::uint32_t nLog2, std::uint32_t qstep, std::int32_t* rate, std::int64_t* dist) {
    // This function models the rate and distortion for a Laplacian
    // source with given variance when quantized with a uniform quantizer
    // with given stepsize. The closed form expressions are in:
    // Hang and Chen, "Source Model for transform video coder and its
    // application - Part I: Fundamental Theory", IEEE Trans. Circ.
    // Sys. for Video Tech., April 1997.
    if (var == 0) {
        *rate = 0;
        *dist = 0;
    } else {
        std::int32_t               dQ10, rQ10;
        static const std::uint32_t kMaxXsqQ10 = 245727;
        const std::uint64_t        xsqQ10_64  = (((std::uint64_t)qstep * qstep << (nLog2 + 10)) + (var >> 1)) / var;
        const std::int32_t         xsqQ10     = (std::int32_t)std::min<std::uint64_t>(xsqQ10_64, kMaxXsqQ10);
        modelRdNorm(xsqQ10, &rQ10, &dQ10);
        *rate = roundPowerOfTwo(rQ10 << nLog2, 10 - kAv1ProbCostShift);
        *dist = (var * (std::int64_t)dQ10 + 512) >> 10;
    }
}

// model_rd_from_sse, enc_inter_prediction.c:1960-:1981. NAMED DEVIATION: eb_num_pels_log2_lookup[bsize] threads as numPelsLog2 (the caller-side table read at :1977).
void modelRdFromSse(std::int32_t numPelsLog2, std::int16_t quantizer, std::uint8_t bitDepth, std::uint64_t sse, std::uint32_t* rate,
                       std::uint64_t* dist, std::uint8_t simpleModelRdFromVar) {
    std::int32_t dequantShift = bitDepth - 5;
    // Fast approximate the modelling function.
    if (simpleModelRdFromVar) {
        std::int64_t squareError = sse;
        quantizer            = quantizer >> dequantShift;
        if (quantizer < 120) {
            *rate = (std::int32_t)((squareError * (280 - quantizer)) >> (16 - kAv1ProbCostShift));
        } else {
            *rate = 0;
        }
        *dist = (std::uint64_t)(squareError * quantizer) >> 8;
    } else {
        modelRdFromVarLapndz(
            sse, numPelsLog2, quantizer >> dequantShift, (std::int32_t*)rate, (std::int64_t*)dist);
    }
    *dist <<= 4;
}

} // namespace inter
