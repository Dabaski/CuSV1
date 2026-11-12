#include "inter_prediction.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace inter {

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
// signflip row; the masks member is the parked runtime buffer). Rows in
// BlockSize order, definitions.h:883-:904.
const WedgeParamsType wedgeParamsLookup[kBlockSizesAll] = {
    {0, nullptr, nullptr},                             // BLOCK_4X4
    {0, nullptr, nullptr},                             // BLOCK_4X8
    {0, nullptr, nullptr},                             // BLOCK_8X4
    {4, wedgeCodebook16Heqw, wedgeSignflipLookup[3]},  // BLOCK_8X8
    {4, wedgeCodebook16Hgtw, wedgeSignflipLookup[4]},  // BLOCK_8X16
    {4, wedgeCodebook16Hltw, wedgeSignflipLookup[5]},  // BLOCK_16X8
    {4, wedgeCodebook16Heqw, wedgeSignflipLookup[6]},  // BLOCK_16X16
    {4, wedgeCodebook16Hgtw, wedgeSignflipLookup[7]},  // BLOCK_16X32
    {4, wedgeCodebook16Hltw, wedgeSignflipLookup[8]},  // BLOCK_32X16
    {4, wedgeCodebook16Heqw, wedgeSignflipLookup[9]},  // BLOCK_32X32
    {0, nullptr, nullptr},                             // BLOCK_32X64
    {0, nullptr, nullptr},                             // BLOCK_64X32
    {0, nullptr, nullptr},                             // BLOCK_64X64
    {0, nullptr, nullptr},                             // BLOCK_64X128
    {0, nullptr, nullptr},                             // BLOCK_128X64
    {0, nullptr, nullptr},                             // BLOCK_128X128
    {0, nullptr, nullptr},                             // BLOCK_4X16
    {0, nullptr, nullptr},                             // BLOCK_16X4
    {4, wedgeCodebook16Hgtw, wedgeSignflipLookup[18]}, // BLOCK_8X32
    {4, wedgeCodebook16Hltw, wedgeSignflipLookup[19]}, // BLOCK_32X8
    {0, nullptr, nullptr},                             // BLOCK_16X64
    {0, nullptr, nullptr},                             // BLOCK_64X16
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

} // namespace inter
