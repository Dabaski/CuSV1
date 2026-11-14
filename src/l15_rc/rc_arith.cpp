#include "rc_arith.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "rc_tables.h"

namespace l15_rc {

// ASSIGN_MINQ_TABLE (rc_tables.h:36-39): with CONFIG_ENABLE_HIGH_BIT_DEPTH=0
// the bit-depth argument is consumed and the 8-bit table is selected.
const std::int32_t* minqTable(const char* baseName, int bitDepth) {
    (void)bitDepth;
    std::string name(baseName);
    name += "_8";
    std::int32_t len = 0;
    return findTable(name.c_str(), len);
}

// get_active_quality (rc_vbr_cbr.c:31-45): verbatim.
int getActiveQuality(int q, int boost, int low, int high, const std::int32_t* lowMotionMinq,
                     const std::int32_t* highMotionMinq) {
    if (boost > high) {
        return lowMotionMinq[q];
    }
    if (boost < low) {
        return highMotionMinq[q];
    }
    const int gap = high - low;
    const int offset = high - boost;
    const int qdiff = highMotionMinq[q] - lowMotionMinq[q];
    const int adjustment = (offset * qdiff + (gap >> 1)) / gap;
    return lowMotionMinq[q] + adjustment;
}

// get_kf_active_quality_tpl (rc_vbr_cbr.c:47-54): verbatim.
int getKfActiveQualityTpl(const RcControl& rc, int q, int bitDepth) {
    const std::int32_t* lowMotionMinq = minqTable("kf_low_motion_minq_cqp", bitDepth);
    const std::int32_t* highMotionMinq = minqTable("kf_high_motion_minq", bitDepth);
    return getActiveQuality(q, rc.kfBoost, kBoostKfLow, kBoostKfHigh, lowMotionMinq, highMotionMinq);
}

// get_gf_active_quality_tpl_la (rc_vbr_cbr.c:56-63): verbatim.
int getGfActiveQualityTplLa(const RcControl& rc, int q, int bitDepth) {
    const std::int32_t* lowMotionMinq = minqTable("arfgf_low_motion_minq", bitDepth);
    const std::int32_t* highMotionMinq = minqTable("arfgf_high_motion_minq", bitDepth);
    return getActiveQuality(
        q, rc.gfuBoost, kBoostGfLowTplLa, kBoostGfHighTplLa, lowMotionMinq, highMotionMinq);
}

// get_gf_high_motion_quality (rc_vbr_cbr.c:65-69): verbatim.
int getGfHighMotionQuality(int q, int bitDepth) {
    const std::int32_t* highMotionMinq = minqTable("arfgf_high_motion_minq", bitDepth);
    return highMotionMinq[q];
}

// av1_calc_pframe_target_size_one_pass_cbr (rc_vbr_cbr.c:71-95): verbatim.
int calcPframeTargetSizeOnePassCbr(const RcControl& rc, const RcConfig& rcCfg) {
    const std::int64_t diff = rc.optimalBufferLevel - rc.bufferLevel;
    const std::int64_t onePctBits = 1 + rc.optimalBufferLevel / 100;
    const int minFrameTarget = std::max(rc.avgFrameBandwidth >> 4, kFrameOverheadBits);
    int target = rc.avgFrameBandwidth;

    if (diff > 0) {
        const int pctLow = static_cast<int>(std::min(diff / onePctBits, static_cast<std::int64_t>(rcCfg.underShootPct)));
        target -= (target * pctLow) / 200;
    } else if (diff < 0) {
        const int pctHigh = static_cast<int>(std::min(-diff / onePctBits, static_cast<std::int64_t>(rcCfg.overShootPct)));
        target += (target * pctHigh) / 200;
    }
    if (rcCfg.maxInterBitratePct) {
        const int maxRate = rc.avgFrameBandwidth * rcCfg.maxInterBitratePct / 100;
        target = std::min(target, maxRate);
    }
    return std::max(minFrameTarget, target);
}

// svt_aom_reset_update_frame_target (rc_vbr_cbr.c:97-104): verbatim.
void resetUpdateFrameTarget(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs) {
    rc.bufferLevel = rc.optimalBufferLevel;
    rc.bitsOffTarget = rc.optimalBufferLevel;
    ppcs.thisFrameTarget = calcPframeTargetSizeOnePassCbr(rc, rcCfg);
}

// av1_rc_clamp_iframe_target_size (rc_vbr_cbr.c:518-531): verbatim.
int rcClampIframeTargetSize(const RcControl& rc, const RcConfig& rcCfg, int target) {
    if (rcCfg.maxIntraBitratePct) {
        const int maxRate = rc.avgFrameBandwidth * rcCfg.maxIntraBitratePct / 100;
        target = std::min(target, maxRate);
    }
    if (target > rc.maxFrameBandwidth) {
        target = rc.maxFrameBandwidth;
    }
    return target;
}

// av1_calc_iframe_target_size_one_pass_cbr (rc_vbr_cbr.c:534-551): verbatim.
int calcIframeTargetSizeOnePassCbr(const RcControl& rc, const RcConfig& rcCfg, const PcsCarrier& pcs,
                                   double frameRate) {
    int target;
    if (pcs.pictureNumber == 0) {
        target = (rc.startingBufferLevel / 2) > INT_MAX ? INT_MAX : static_cast<int>(rc.startingBufferLevel / 2);
    } else {
        int kfBoost = 32;
        kfBoost = std::max(kfBoost, static_cast<int>(2 * frameRate - 16));
        if (rc.framesSinceKey < frameRate / 2) {
            kfBoost = static_cast<int>(kfBoost * rc.framesSinceKey / (frameRate / 2));
        }
        target = ((16 + kfBoost) * rc.avgFrameBandwidth) >> 4;
    }
    return rcClampIframeTargetSize(rc, rcCfg, target);
}

// set_rc_buffer_sizes (rc_vbr_cbr.c:591-603): verbatim.
void setRcBufferSizes(RcControl& rc, const RcConfig& rcCfg, std::int64_t bandwidth) {
    const std::int64_t starting = rcCfg.startingBufferLevelMs;
    const std::int64_t optimal = rcCfg.optimalBufferLevelMs;
    const std::int64_t maximum = rcCfg.maximumBufferSizeMs;

    rc.startingBufferLevel = starting * bandwidth / 1000;
    rc.optimalBufferLevel = (optimal == 0) ? bandwidth / 8 : optimal * bandwidth / 1000;
    rc.maximumBufferSize = (maximum == 0) ? bandwidth / 8 : maximum * bandwidth / 1000;
}

// update_buffer_level (rc_vbr_cbr.c:1475-1490): verbatim.
void updateBufferLevel(RcControl& rc, const PcsCarrier& ppcs, int encodedFrameSize) {
    if (!ppcs.showableFrame) {
        rc.bitsOffTarget -= encodedFrameSize;
    } else {
        rc.bitsOffTarget += rc.avgFrameBandwidth - encodedFrameSize;
    }
    rc.bitsOffTarget = std::min(rc.bitsOffTarget, rc.maximumBufferSize);
    rc.bufferLevel = rc.bitsOffTarget;
}

// ---- RC2c Wave A: the D-series dependency helpers ----

namespace {

// CLIP3 (the reference macro): value < min ? min : (value > max ? max : value).
template <typename T>
T clip3(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// rc_aq.c:67-68, :70-74.
constexpr int kVarBoostMaxQstepRatioBoost = 8;
constexpr int kVarBoostMaxDeltaqRange = 80;  // VAR_BOOST_MAX_DELTAQ_RANGE

}  // namespace

// svt_aom_dc_quant_qtx (inv_transforms.c:3467-3482): verbatim; the
// CONFIG_ENABLE_HIGH_BIT_DEPTH=0 build keeps only the 8-bit case and the
// default (assert removed under NDEBUG -> -1).
std::int16_t dcQuantQtx(int qindex, int delta, int bitDepth) {
    const int qClamped = clip3(qindex + delta, 0, kMaxQ);
    switch (bitDepth) {
    case kEbEightBit:
        return kDcQLookupQtx[qClamped];
    default:
        return -1;
    }
}

// svt_aom_ac_quant_qtx (inv_transforms.c:3484-3499): verbatim.
std::int16_t acQuantQtx(int qindex, int delta, int bitDepth) {
    const int qClamped = clip3(qindex + delta, 0, kMaxQ);
    switch (bitDepth) {
    case kEbEightBit:
        return kAcQLookupQtx[qClamped];
    default:
        return -1;
    }
}

// svt_av1_convert_qindex_to_q (rc_process.c:185-198): verbatim.
double convertQindexToQ(int qindex, int bitDepth) {
    switch (bitDepth) {
    case kEbEightBit:
        return acQuantQtx(qindex, 0, bitDepth) / 4.0;
    case kEbTenBit:
        return acQuantQtx(qindex, 0, bitDepth) / 16.0;
    case kEbTwelveBit:
        return acQuantQtx(qindex, 0, bitDepth) / 64.0;
    default:
        return -1.0;
    }
}

// svt_av1_compute_qdelta (rc_process.c:200-222): verbatim; the loop bound
// is i < MAXQ so an unsatisfiable target leaves the index at its last
// assignment 254 (measured semantics, pinned by the tests).
int computeQdelta(double qstart, double qtarget, int bitDepth) {
    int startIndex = kMaxQ;
    int targetIndex = kMaxQ;
    int i;
    for (i = kMinQ; i < kMaxQ; ++i) {
        startIndex = i;
        if (convertQindexToQ(i, bitDepth) >= qstart) {
            break;
        }
    }
    for (i = kMinQ; i < kMaxQ; ++i) {
        targetIndex = i;
        if (convertQindexToQ(i, bitDepth) >= qtarget) {
            break;
        }
    }
    return targetIndex - startIndex;
}

// svt_av1_rc_bits_per_mb (rc_process.c:254-267): verbatim.
int rcBitsPerMb(int frameType, int qindex, double correctionFactor, int bitDepth, int isScreenContent) {
    const double q = convertQindexToQ(qindex, bitDepth);
    int enumerator;
    if (isScreenContent) {
        enumerator = (frameType == kKeyFrame) ? 1000000 : 750000;
    } else {
        enumerator = (frameType == kKeyFrame) ? 1400000 : 1000000;
    }
    return (int)(enumerator * correctionFactor / q);
}

// find_qindex_by_rate (rc_process.c:269-287): static in the reference,
// exported for testability.
int findQindexByRate(int desiredBitsPerMb, int bitDepth, int frameType, int isScreenContent, int bestQindex,
                     int worstQindex) {
    int low = bestQindex;
    int high = worstQindex;
    while (low < high) {
        const int mid = (low + high) >> 1;
        const int midBitsPerMb = rcBitsPerMb(frameType, mid, 1.0, bitDepth, isScreenContent);
        if (midBitsPerMb > desiredBitsPerMb) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low;
}

// svt_av1_compute_qdelta_by_rate (rc_process.c:289-300): verbatim; the
// RATE_CONTROL* consumes best/worst_quality only - threaded as the pair.
int computeQdeltaByRate(int bestQuality, int worstQuality, int frameType, int qindex, double rateTargetRatio,
                        int bitDepth, int isScreenContent) {
    const int baseBitsPerMb = rcBitsPerMb(frameType, qindex, 1.0, bitDepth, isScreenContent);
    const int targetBitsPerMb = (int)(rateTargetRatio * baseBitsPerMb);
    const int targetIndex =
        findQindexByRate(targetBitsPerMb, bitDepth, frameType, isScreenContent, bestQuality, worstQuality);
    return targetIndex - qindex;
}

// svt_av1_rate_factor_deltas (rc_process.c:302-309) and
// svt_av1_rate_factor_levels (:311-319) live in rc_tables.cpp as
// kRateFactorDeltas / kRateFactorLevelsMap.

// svt_av1_get_q_index_from_qstep_ratio (rc_process.c:321-341): verbatim.
int getQIndexFromQstepRatio(int leafQindex, double qstepRatio, int bitDepth) {
    const double leafQstep = dcQuantQtx(leafQindex, 0, bitDepth);
    const double targetQstep = leafQstep * qstepRatio;
    int qindex;
    if (qstepRatio < 1.0) {
        for (qindex = leafQindex; qindex > kMinQ; --qindex) {
            const double qstep = dcQuantQtx(qindex, 0, bitDepth);
            if (qstep <= targetQstep) {
                break;
            }
        }
    } else {
        for (qindex = leafQindex; qindex <= kMaxQ; ++qindex) {
            const double qstep = dcQuantQtx(qindex, 0, bitDepth);
            if (qstep >= targetQstep) {
                break;
            }
        }
    }
    return qindex;
}

// svt_av1_get_cqp_kf_boost_from_r0 (rc_process.c:229-243): verbatim; the
// R0_MIN_DIVISOR floor is part of the reference (:227).
int getCqpKfBoostFromR0(double r0, int framesToKey, int inputResolution) {
    double factor;
    r0 = std::max(r0, 1e-6);
    if (framesToKey == -1) {
        factor = (10.0 + 4.0) / 2;
    } else {
        factor = std::sqrt((double)framesToKey);
        factor = std::min(factor, 10.0);
        factor = std::max(factor, 4.0);
    }
    return inputResolution <= kInputSize720pRange
        ? (int)std::rint(3 * (75.0 + 17.0 * factor) / r0)
        : (int)std::rint(4 * (75.0 + 17.0 * factor) / r0);
}

// svt_av1_get_gfu_boost_from_r0_lap (rc_process.c:245-252): verbatim.
int getGfuBoostFromR0Lap(double minFactor, double maxFactor, double r0, int framesToKey) {
    r0 = std::max(r0, 1e-6);
    double factor = std::sqrt((double)framesToKey);
    factor = std::min(factor, maxFactor);
    factor = std::max(factor, minFactor);
    factor = 200.0 + 10.0 * factor;
    return (int)std::rint(factor / r0);
}

// svt_av1_calculate_boost_bits (rc_process.c:641-667): verbatim.
int calculateBoostBits(int frameCount, int boost, std::int64_t totalGroupBits) {
    int allocationChunks;
    if (!boost || (totalGroupBits <= 0)) {
        return 0;
    }
    if (frameCount <= 0) {
        return (int)std::min(totalGroupBits, (std::int64_t)INT_MAX);
    }
    allocationChunks = (frameCount * 100) + boost;
    if (boost > 1023) {
        const int divisor = boost >> 10;
        boost /= divisor;
        allocationChunks /= divisor;
    }
    return std::max((int)(((std::int64_t)boost * totalGroupBits) / allocationChunks), 0);
}

// ---- A9R: rc_process.c rd-multiplier family ----

// def_inter_rd_multiplier (rc_process.c:346-348): verbatim.
double defInterRdMultiplier(int qindex) {
    return 3.2 + 0.0015 * qindex;
}

// def_arf_rd_multiplier (rc_process.c:353-355): verbatim.
double defArfRdMultiplier(int qindex) {
    return 3.25 + 0.0015 * qindex;
}

// def_kf_rd_multiplier (rc_process.c:360-362): verbatim.
double defKfRdMultiplier(int qindex) {
    return 3.3 + 0.0015 * qindex;
}

// svt_aom_compute_rd_mult_based_on_qindex (rc_process.c:364-392): verbatim.
// The 10/12-bit table cases of the dc_quant callee are the
// CONFIG_ENABLE_HIGH_BIT_DEPTH=0 park (dcQuantQtx -> -1), so the rdmult is
// 3 there and the ROUND_POWER_OF_TWO shift drives it to 0 -> the "> 0"
// test fails -> 1. The default branch returns -1 (assert removed under
// NDEBUG).
int computeRdMultBasedOnQindex(int bitDepth, int updateType, int qindex) {
    const int q = dcQuantQtx(qindex, 0, bitDepth);
    std::int64_t rdmult;

    // Scale rdmult based on frame type
    if (updateType == kSvtAv1KfUpdate) {
        rdmult = (std::int64_t)(defKfRdMultiplier(q) * q * q);
    } else if (updateType == kSvtAv1GfUpdate || updateType == kSvtAv1ArfUpdate) {
        rdmult = (std::int64_t)(defArfRdMultiplier(q) * q * q);
    } else {
        rdmult = (std::int64_t)(defInterRdMultiplier(q) * q * q);
    }

    switch (bitDepth) {
    case kEbEightBit:
        break;
    case kEbTenBit:
        rdmult = roundPowerOfTwo(rdmult, 4);
        break;
    case kEbTwelveBit:
        rdmult = roundPowerOfTwo(rdmult, 8);
        break;
    default:
        return -1;
    }

    return rdmult > 0 ? (int)std::min(rdmult, (std::int64_t)INT_MAX) : 1;
}

// rd_frame_type_factor (rc_process.c:394-395): verbatim.
const int kRdFrameTypeFactor[2][7] = {{150, 180, 150, 150, 180, 180, 150},
                                      {128, 144, 128, 128, 144, 144, 128}};

// update_lambda (rc_process.c:398-441): verbatim; the pcs walls thread (the
// declaration comment names the consumed carriers).
std::uint32_t updateLambda(const PcsCarrier& pcs, std::uint8_t qIndex, std::uint8_t meQIndex, int bitDepth,
                           std::int64_t rdmult, bool rtc, bool statsBasedSbLambdaModulation) {
    const int frameType = pcs.frameType;
    const int temporalLayerIndex = pcs.temporalLayerIndex;
    const int maxTemporalLayer = pcs.hierarchicalLevels;

    // Update rdmult based on the frame's position in the miniGOP
    const int gfUpdateType = frameType == kKeyFrame ? kSvtAv1KfUpdate
        : temporalLayerIndex == 0                   ? kSvtAv1ArfUpdate
        : temporalLayerIndex < maxTemporalLayer     ? kSvtAv1IntnlArfUpdate
                                                    : kSvtAv1LfUpdate;
    rdmult = (rdmult * kRdFrameTypeFactor[bitDepth != kEbEightBit ? 1 : 0][gfUpdateType]) >> 7;
    if (rtc && frameType == kKeyFrame) {
        rdmult = (rdmult * kRtcKfLambdaBoost) >> 7;
    }
    if (statsBasedSbLambdaModulation) {
        int factor = 128;
        if (rtc) {
            const int qdiff = meQIndex - pcs.baseQIdx;
            if (qdiff < 0) {
                factor = (qdiff <= -4) ? 100 : 115;
            }
        } else if (pcs.deltaQPresent || pcs.r0DeltaQpMd) {
            const int qdiff = qIndex - pcs.baseQIdx;
            if (qdiff < 0) {
                factor = (qdiff <= -8) ? 90 : 115;
            } else if (qdiff > 0) {
                factor = (qdiff <= 8) ? 135 : 150;
            }
        } else {
            const int qdiff = meQIndex - pcs.baseQIdx;
            if (qdiff < 0) {
                factor = (qdiff <= -4) ? 100 : 115;
            } else if (qdiff > 0) {
                factor = (qdiff <= 4) ? 135 : 150;
            }
        }

        rdmult = (rdmult * factor) >> 7;
    }
    return (std::uint32_t)rdmult;
}

// svt_aom_compute_rd_mult (rc_process.c:446-451): verbatim.
std::uint32_t computeRdMult(const PcsCarrier& pcs, std::uint8_t qIndex, std::uint8_t meQIndex, int bitDepth,
                            bool rtc, bool statsBasedSbLambdaModulation) {
    // Always use q_index for the derivation of the initial rdmult (i.e. don't use me_q_index)
    const std::int64_t rdmult = computeRdMultBasedOnQindex(bitDepth, pcs.updateType, qIndex);
    return updateLambda(pcs, qIndex, meQIndex, bitDepth, rdmult, rtc, statsBasedSbLambdaModulation);
}

// svt_aom_compute_fast_lambda (rc_process.c:453-464): verbatim; the HBD
// table branch is the named park - the pinned RTC build is 8-bit only.
std::uint32_t computeFastLambda(const PcsCarrier& pcs, std::uint8_t qIndex, std::uint8_t meQIndex, int bitDepth,
                                bool rtc, bool statsBasedSbLambdaModulation) {
    // Always use q_index for the derivation of the initial rdmult (i.e. don't use me_q_index)
    const std::int64_t rdmult = kLambdaModeDecision8BitSad[qIndex];
    return updateLambda(pcs, qIndex, meQIndex, bitDepth, rdmult, rtc, statsBasedSbLambdaModulation);
}

// svt_aom_lambda_assign (rc_process.c:466-496): verbatim; the
// CONFIG_ENABLE_HIGH_BIT_DEPTH=0 build keeps only the 8-bit branch, so for
// bit depths 10/12 the assert-removed else leaves the caller's values and
// only the scale shift applies (the C1/C3 park convention).
void lambdaAssign(const PcsCarrier& pcs, std::uint32_t& fastLambda, std::uint32_t& fullLambda, int bitDepth,
                  std::uint8_t qpIndex, bool multiplyLambda, std::uint64_t lambdaScaleFactor, bool rtc,
                  bool statsBasedSbLambdaModulation) {
    (void)multiplyLambda;  // only used by the 10/12-bit paths (compiled out in 8-bit RTC)
    if (bitDepth == kEbEightBit) {
        fullLambda = computeRdMult(pcs, qpIndex, qpIndex, bitDepth, rtc, statsBasedSbLambdaModulation);
        fastLambda = kLambdaModeDecision8BitSad[qpIndex];
    } else {
        // assert(0) in the reference - removed under NDEBUG.
    }

    // NM: To be done: tune lambda based on the picture type and layer.
    fullLambda = (std::uint32_t)((fullLambda * lambdaScaleFactor) >> 7);
    fastLambda = (std::uint32_t)((fastLambda * lambdaScaleFactor) >> 7);
}

// ---- A9R: initial_rc_process.c ----

// svt_aom_get_tpl_group_level (initial_rc_process.c:190-202): verbatim.
std::uint8_t getTplGroupLevel(std::uint8_t tpl, std::int8_t encMode) {
    std::uint8_t tplGroupLevel;
    if (!tpl) {
        tplGroupLevel = 0;
    } else if (encMode <= kEncM5) {
        tplGroupLevel = 1;
    } else if (encMode <= kEncM8) {
        tplGroupLevel = 3;
    } else {
        tplGroupLevel = 4;
    }
    return tplGroupLevel;
}

// get_tpl_params_level (initial_rc_process.c:307-317): verbatim.
std::uint8_t getTplParamsLevel(std::int8_t encMode) {
    std::uint8_t tplParamsLevel;
    if (encMode <= kEncM2) {
        tplParamsLevel = 1;
    } else if (encMode <= kEncM7) {
        tplParamsLevel = 4;
    } else {
        tplParamsLevel = 5;
    }
    return tplParamsLevel;
}

// set_1pvbr_param (initial_rc_process.c:638-666): verbatim (the mutex
// block/release is the named park; the declaration comment names the
// threaded walls).
void set1pVbrParam(P1vbrCarrier& pcs) {
    pcs.statStruct = pcs.entry.statStruct;
    if (pcs.sliceType != kIslice) {
        std::uint64_t avgMeDist = 0;
        std::uint64_t avgVarianceMeDist = 0;
        for (int b64Idx = 0; b64Idx < pcs.b64TotalCount; ++b64Idx) {
            avgMeDist += pcs.rcMeDistortion[b64Idx];
            avgVarianceMeDist += pcs.me8x8CostVariance[b64Idx];
        }
        avgMeDist /= pcs.b64TotalCount;
        avgVarianceMeDist /= pcs.b64TotalCount;

        double weight = 1;
        if (avgVarianceMeDist > kHigh8x8DistVarTh) {
            weight = 1.5;
        }

        if (pcs.inputResolution <= kInputSize480pRange) {
            weight = 1.5 * weight;
        }
        pcs.statStruct.poc = pcs.pictureNumber;
        pcs.entry.statStruct.totalNumBits = std::max<std::uint64_t>(kMinAvgMeDist, avgMeDist);
        pcs.entry.codedError = (double)avgMeDist * pcs.b64TotalCount * weight / kVbrCodedErrorFactor;
        pcs.entry.statStruct.poc = pcs.pictureNumber;
    }
}

// ---- A9R: pass2_strategy.c (the two-pass stats arithmetic) ----

// calculate_modified_err (pass2_strategy.c:23-31): verbatim.
double calculateModifiedErr(bool hasTotalStats, std::uint64_t thisFrameTotalNumBits) {
    if (!hasTotalStats) {
        return 0;
    }
    return (double)thisFrameTotalNumBits;
}

// subtract_stats (pass2_strategy.c:47-53): verbatim.
void subtractStats(FirstpassStatsCarrier& section, const FirstpassStatsCarrier& frame) {
    section.frame -= frame.frame;
    section.codedError -= frame.codedError;
    section.count -= frame.count;
    section.duration -= frame.duration;
}

// fclamp (definitions.h): the double clamp macro.
static double fclamp(double x, double low, double hi) {
    return x < low ? low : (x > hi ? hi : x);
}

// q_pow_term (pass2_strategy.c:60): verbatim.
const double kQpowTerm[(kQindexRange >> 5) + 1] = {0.65, 0.70, 0.75, 0.80, 0.85, 0.90, 0.95, 0.95, 0.95};

// ERR_DIVISOR (pass2_strategy.c:61).
const double kErrDivisor = 96.0;

// calc_correction_factor (pass2_strategy.c:63-70): verbatim (the assert
// gate is the named NDEBUG park).
double calcCorrectionFactor(double errPerMb, int q) {
    const double errorTerm = errPerMb / kErrDivisor;
    const int index = q >> 5;
    // Adjustment to power term based on qindex
    const double powerTerm =
        kQpowTerm[index] + (((kQpowTerm[index + 1] - kQpowTerm[index]) * (q % 32)) / 32.0);
    return fclamp(std::pow(errorTerm, powerTerm), 0.05, 5.0);
}

// qbpm_enumerator (pass2_strategy.c:72-74): verbatim.
int qbpmEnumerator(int rateErrTol) {
    return 1250000 + ((300000 * std::min(75, std::max(rateErrTol - 25, 0))) / 75);
}

// find_qindex_by_rate_with_correction (pass2_strategy.c:78-99): verbatim
// (the assert gate is the named NDEBUG park).
int findQindexByRateWithCorrection(int desiredBitsPerMb, int bitDepth, double errorPerMb,
                                   double groupWeightFactor, int rateErrTol, int bestQindex,
                                   int worstQindex) {
    int low = bestQindex;
    int high = worstQindex;

    while (low < high) {
        const int mid = (low + high) >> 1;
        const double midFactor = calcCorrectionFactor(errorPerMb, mid);
        const double q = convertQindexToQ(mid, bitDepth);
        const int enumerator = qbpmEnumerator(rateErrTol);
        const int midBitsPerMb = (int)((enumerator * midFactor * groupWeightFactor) / q);

        if (midBitsPerMb > desiredBitsPerMb) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low;
}

// ---- A9R: pass2_strategy.c (the GF-stats walk) ----

namespace {

// input_stats (pass2_strategy.c:37-45): verbatim; the EOF convention is
// the reference's (-1 / 1).
int inputStats(TwoPassCarrier& p, FirstpassStatsCarrier& fps) {
    if (p.statsInPos >= p.statsInCount) {
        return EOF;
    }

    fps = p.statsIn[p.statsInPos];
    ++p.statsInPos;
    return 1;
}

}  // namespace

// accumulate_this_frame_stats (pass2_strategy.c:168-171): verbatim.
void accumulateThisFrameStats(const FirstpassStatsCarrier& stats, double modFrameErr,
                              GfGroupStatsCarrier& gfStats) {
    gfStats.gfGroupErr += modFrameErr;
    gfStats.gfGroupRawError += stats.codedError;
}

// set_baseline_gf_interval (pass2_strategy.c:314-324): verbatim.
void setBaselineGfInterval(const PcsCarrier& pcs, int arfPosition, RcControl& rc) {
    if (frameIsIntraOnly(pcs.frameType) && pcs.idrFlag) {
        rc.baselineGfInterval = std::max(arfPosition - 1, 1);
    } else {
        rc.baselineGfInterval = pcs.gfInterval;
    }
}

// init_gf_stats (pass2_strategy.c:326-339): verbatim.
void initGfStats(GfGroupStatsCarrier& gfStats) {
    gfStats.gfGroupErr = 0.0;
    gfStats.gfStatStruct.poc = 0;
    gfStats.gfStatStruct.totalNumBits = 1;
    gfStats.gfStatStruct.qindex = 172;
    gfStats.gfStatStruct.worstQindex = 172;
    gfStats.gfGroupRawError = 0.0;
    gfStats.gfGroupSkipPct = 0.0;
    gfStats.gfGroupInactiveZoneRows = 0.0;
}

// calculate_gf_stats (pass2_strategy.c:341-390): verbatim; the walls thread
// (the declaration comment names the carriers).
void calculateGfStats(const PcsCarrier& ppcs, RcControl& rc, GfGroupStatsCarrier& gfStats,
                      FirstpassStatsCarrier& thisFrame, int& useAltRef, TwoPassCarrier& twopass) {
    // SequenceControlSet* scs = ppcs->scs; ... (walls threaded)
    FirstpassStatsCarrier nextFrame;
    const int startPos = twopass.statsInPos;

    initGfStats(gfStats);

    // Load stats for the current frame.
    double modFrameErr = calculateModifiedErr(twopass.hasTotalStats, thisFrame.statStruct.totalNumBits);

    // Note the error of the frame at the start of the group. This will be
    // the GF frame error if we code a normal gf.

    // If this is a key frame or the overlay from a previous arf then
    // the error score / cost of this frame has already been accounted for.
    // There is no overlay support for now
    if (frameIsIntraOnly(ppcs.frameType)) {
        gfStats.gfGroupErr -= modFrameErr;
        gfStats.gfGroupRawError -= thisFrame.codedError;
    }
    gfStats.gfStatStruct = thisFrame.statStruct;
    int i = 0;
    while (i < ppcs.gfInterval) {
        ++i;
        // Accumulate error score of frames in this gf group.
        modFrameErr = calculateModifiedErr(twopass.hasTotalStats, thisFrame.statStruct.totalNumBits);
        // accumulate stats for this frame
        accumulateThisFrameStats(thisFrame, modFrameErr, gfStats);

        // read in the next frame
        if (EOF == inputStats(twopass, nextFrame)) {
            break;
        }
        thisFrame = nextFrame;
    }

    // Was the group length constrained by the requirement for a new KF?
    rc.constrainedGfGroup = (i >= rc.framesToKey) ? 1 : 0;
    useAltRef = (i > 2);
    setBaselineGfInterval(ppcs, i, rc);
    // Reset the file position.
    twopass.statsInPos = startPos;
}

// ---- A9R: pass2_strategy.c (the active-worst-quality estimate) ----

namespace {

// clamp (definitions.h): the integer clamp macro.
static int clampInt(int value, int low, int hi) {
    return value < low ? low : (value > hi ? hi : value);
}

}  // namespace

// get_twopass_worst_quality (pass2_strategy.c:125-166): verbatim; the walls
// thread (the declaration comment names the carriers).
int getTwopassWorstQuality(bool firstPassDownsample, int maxInputLumaWidth, int maxInputLumaHeight,
                           double sectionErr, double inactiveZone, int sectionTargetBandwidth,
                           double groupWeightFactor, int encoderBitDepth, int underShootPct, int overShootPct,
                           int bestQuality, int worstQuality) {
    std::uint32_t mbCols;
    std::uint32_t mbRows;
    if (firstPassDownsample) {
        mbCols = 2 * (maxInputLumaWidth + 16 - 1) / 16;
        mbRows = 2 * (maxInputLumaHeight + 16 - 1) / 16;
    } else {
        mbCols = (maxInputLumaWidth + 16 - 1) / 16;
        mbRows = (maxInputLumaHeight + 16 - 1) / 16;
    }
    inactiveZone = fclamp(inactiveZone, 0.0, 1.0);

    if (sectionTargetBandwidth <= 0) {
        return worstQuality;  // Highest value allowed
    }

    const int numMbs = (int)(mbCols * mbRows);
    //(oxcf->resize_cfg.resize_mode != RESIZE_NONE)
    //    ? cpi->initial_mbs
    //    : cpi->common.mi_params.MBs;
    const int activeMbs = std::max(1, numMbs - (int)(numMbs * inactiveZone));
    const double avErrPerMb = sectionErr / activeMbs;
    const int targetNormBitsPerMb =
        (int)(((std::uint64_t)sectionTargetBandwidth << kBperMbNormbits) / activeMbs);

    const int rateErrTol = std::min(underShootPct, overShootPct);

    // Try and pick a max Q that will be high enough to encode the
    // content at the given rate.
    return findQindexByRateWithCorrection(targetNormBitsPerMb, encoderBitDepth, avErrPerMb, groupWeightFactor,
                                          rateErrTol, bestQuality, worstQuality);
}

// calculate_active_worst_quality (pass2_strategy.c:392-455): verbatim; the
// walls thread (the declaration comment names the carriers).
void calculateActiveWorstQuality(const PcsCarrier& ppcs, const GfGroupStatsCarrier& gfStats, RcControl& rc,
                                 std::int64_t targetBitRate, int mbRows, int encoderBitDepth,
                                 bool firstPassDownsample, int maxInputLumaWidth, int maxInputLumaHeight,
                                 int underShootPct, int overShootPct, bool gopConstraintRc, bool singlePass,
                                 bool twoPass) {
    (void)ppcs;

    // Calculate an estimate of the maxq needed for the group.
    // We are more agressive about correcting for sections
    // where there could be significant overshoot than for easier
    // sections where we do not wish to risk creating an overshoot
    // of the allocated bit budget.
    if (rc.baselineGfInterval > 1) {
        const int vbrGroupBitsPerFrame = (int)(rc.gfGroupBits / rc.baselineGfInterval);
        const double groupAvErr = gfStats.gfGroupRawError / rc.baselineGfInterval;
        const double groupAvSkipPct = gfStats.gfGroupSkipPct / rc.baselineGfInterval;
        const double groupAvInactiveZone =
            ((gfStats.gfGroupInactiveZoneRows * 2) / (rc.baselineGfInterval * (double)mbRows));

        int tmpQ;
        // rc factor is a weight factor that corrects for local rate control drift.
        double rcFactor = 1.0;
        const std::int64_t bits = targetBitRate;  // oxcf->target_bandwidth

        if (bits > 0) {
            int rateError;

            rateError = (int)((rc.vbrBitsOffTarget * 100) / bits);
            rateError = clampInt(rateError, -100, 100);
            if (rateError > 0) {
                const double rcFactorMin = gopConstraintRc ? kRcFactorMinGopConst
                    : singlePass                           ? kRcFactorMin1PVbr
                                                           : kRcFactorMin;
                rcFactor = std::max(rcFactorMin, (double)(100 - rateError) / 100.0);
            } else {
                rcFactor = std::min(kRcFactorMax, (double)(100 - rateError) / 100.0);
            }
        }
        tmpQ = getTwopassWorstQuality(firstPassDownsample, maxInputLumaWidth, maxInputLumaHeight, groupAvErr,
                                      (groupAvSkipPct + groupAvInactiveZone), vbrGroupBitsPerFrame, rcFactor,
                                      encoderBitDepth, underShootPct, overShootPct, rc.bestQuality,
                                      rc.worstQuality);
        if (twoPass) {
            const int refQindex = gfStats.gfStatStruct.worstQindex;
            const double refQ = convertQindexToQ(refQindex, encoderBitDepth);
            const std::int64_t refGfGroupBits = (std::int64_t)(gfStats.gfGroupErr);
            const std::int64_t targetGfGroupBits = rc.gfGroupBits;
            {
                int low = rc.bestQuality;
                int high = rc.worstQuality;

                while (low < high) {
                    const int mid = (low + high) >> 1;
                    const double q = convertQindexToQ(mid, encoderBitDepth);
                    const int midBits = (int)(refGfGroupBits * refQ * rcFactor / q);

                    if (midBits > targetGfGroupBits) {
                        low = mid + 1;
                    } else {
                        high = mid;
                    }
                }
                tmpQ = low;
            }
        }
        rc.activeWorstQuality = std::max(tmpQ, rc.activeWorstQuality >> 1);
    }
}

// get_section_target_bandwidth (pass2_strategy.c:746-759): verbatim.
int getSectionTargetBandwidth(const RcControl& rc, bool lapRc, double totalStatsCount, int pictureNumber,
                              std::int64_t bitsLeft) {
    int sectionTargetBandwidth;
    const int framesLeft = (int)(totalStatsCount - pictureNumber);
    if (lapRc) {
        sectionTargetBandwidth = (int)rc.avgFrameBandwidth;
    } else {
        sectionTargetBandwidth = (int)(bitsLeft / framesLeft);
    }
    return sectionTargetBandwidth;
}

// MAX_MB_RATE / MAXRATE_1080P (pass2_strategy.c:875-876): the constants
// ride the header (kMaxMbRate / kMaxRate1080P).

// av1_rc_update_framerate (pass2_strategy.c:880-898): verbatim.
void av1RcUpdateFramerate(RcControl& rc, double newFramerate, std::int64_t targetBitRate, int vbrmaxSection,
                          int numMbs) {
    const int MBs = numMbs;

    rc.avgFrameBandwidth = (int)(targetBitRate / newFramerate);
    // A maximum bitrate for a frame is defined.
    // The baseline for this aligns with HW implementations that
    // can support decode of 1080P content up to a bitrate of MAX_MB_RATE bits
    // per 16x16 MB (averaged over a frame). However this limit is extended if
    // a very high rate is given on the command line or the rate cannot
    // be achieved because of a user specified max q (e.g. when the user
    // specifies lossless encode.
    std::int64_t vbrMaxBits = ((std::int64_t)rc.avgFrameBandwidth * vbrmaxSection) / 100;
    vbrMaxBits = std::min(vbrMaxBits, (std::int64_t)INT_MAX);
    rc.maxFrameBandwidth = (int)std::max(std::max((std::int64_t)(MBs * kMaxMbRate), (std::int64_t)kMaxRate1080P),
                                         vbrMaxBits);
}

// svt_av1_new_framerate (pass2_strategy.c:901-904): verbatim (from aom
// encoder.c, the reference comment).
void av1NewFramerate(RcControl& rc, double& scsNewFramerate, double framerate, std::int64_t targetBitRate,
                     int vbrmaxSection, int numMbs) {
    scsNewFramerate = framerate < 0.1 ? 30 : framerate;
    av1RcUpdateFramerate(rc, scsNewFramerate, targetBitRate, vbrmaxSection, numMbs);
}

// frame_is_intra_only (entropy_coding.h:61-64): verbatim.
bool frameIsIntraOnly(int frameType) {
    return frameType == kKeyFrame || frameType == kIntraOnlyFrame;
}

// frame_is_leaf (enc_mode_config.h:113-116): verbatim (LF_UPDATE).
bool frameIsLeaf(int updateType) {
    return updateType == kSvtAv1LfUpdate;
}

// svt_aom_frame_is_kf_gf_arf (rc_process.c:55-59): verbatim.
bool frameIsKfGfArf(const PcsCarrier& pcs) {
    return frameIsIntraOnly(pcs.frameType) || pcs.updateType == kSvtAv1ArfUpdate ||
        pcs.updateType == kSvtAv1GfUpdate;
}

// clamp_qindex (rc_vbr_cbr.c:21-25): verbatim.
std::uint8_t clampQindex(const RcConfig& cfg, int qindex) {
    const int qmin = kQuantizerToQindex[cfg.minQpAllowed];
    const int qmax = kQuantizerToQindex[cfg.maxQpAllowed];
    return (std::uint8_t)clip3(qindex, qmin, qmax);
}

// clamp_qp (rc_crf_cqp.c:22-26): verbatim.
std::uint8_t clampQp(const RcConfig& cfg, int qp) {
    return (std::uint8_t)clip3(qp, cfg.minQpAllowed, cfg.maxQpAllowed);
}

// ---- RC2c Wave A: the AQ-series (rc_aq.c) ----

// svt_av1_convert_qindex_to_q_fp8 (rc_aq.c:24-37): verbatim.
std::int32_t convertQindexToQFp8(int qindex, int bitDepth) {
    switch (bitDepth) {
    case kEbEightBit:
        return acQuantQtx(qindex, 0, bitDepth) << 6;
    case kEbTenBit:
        return acQuantQtx(qindex, 0, bitDepth) << 4;
    case kEbTwelveBit:
        return acQuantQtx(qindex, 0, bitDepth) << 3;
    default:
        return -1;
    }
}

// svt_av1_compute_qdelta_fp (rc_aq.c:39-61): verbatim.
std::int32_t computeQdeltaFp(std::int32_t qstartFp8, std::int32_t qtargetFp8, int bitDepth) {
    std::int32_t startIndex = kMaxQ;
    std::int32_t targetIndex = kMaxQ;
    std::int32_t i;
    for (i = kMinQ; i < kMaxQ; ++i) {
        startIndex = i;
        if (convertQindexToQFp8(i, bitDepth) >= qstartFp8) {
            break;
        }
    }
    for (i = kMinQ; i < kMaxQ; ++i) {
        targetIndex = i;
        if (convertQindexToQFp8(i, bitDepth) >= qtargetFp8) {
            break;
        }
    }
    return targetIndex - startIndex;
}

// variance_comp_int (rc_aq.c:63-65): verbatim (non-static in the reference).
int varianceCompInt(const void* a, const void* b) {
    return (int)*(const std::uint16_t*)a - *(const std::uint16_t*)b;
}

// av1_get_deltaq_sb_variance_boost (rc_aq.c:76-178): verbatim.
int getDeltaqSbVarianceBoost(std::uint8_t baseQIdx, const std::uint16_t* variances, std::uint8_t strength,
                             int bitDepth, std::uint8_t octile, std::uint8_t curve) {
    std::uint16_t orderedVariances[64];
    std::memcpy(&orderedVariances, variances + kMeTierZeroPu8x8_0, sizeof(std::uint16_t) * 64);
    std::qsort(&orderedVariances, 64, sizeof(std::uint16_t), varianceCompInt);

    const int midIdx = octile * kSubblocksInOctile - 1;
    const int lowIdx = std::max(kSubblocksInOctile - 1, midIdx - kSubblocksInOctile);
    const int uppIdx = std::min(kSubblocksInSb - 1, midIdx + kSubblocksInOctile);
    int variance = (orderedVariances[lowIdx] + orderedVariances[midIdx] * 2 + orderedVariances[uppIdx] + 2) / 4;
    if (variance == 0) {
        variance = 1;
    }

    double qstepRatio = 0;
    static const double strengths[] = {0, 0.65, 1.1, 1.6, 2.5};
    switch (curve) {
    case 1:
        qstepRatio = 0.25 * strength * (-std::log2((double)variance) + 8) + 1;
        break;
    case 2:
        qstepRatio = 0.15 * strength * (-std::log2((double)variance) + 10) + 1;
        break;
    default:
        qstepRatio = std::pow(1.018, strengths[strength] * (-10 * std::log2((double)variance) + 80));
        break;
    }
    qstepRatio = clip3(qstepRatio, 1.0, (double)kVarBoostMaxQstepRatioBoost);

    const std::int32_t baseQ = convertQindexToQFp8(baseQIdx, bitDepth);
    const std::int32_t targetQ = (std::int32_t)(baseQ / qstepRatio);
    std::int32_t boost = 0;
    switch (curve) {
    case 2:
        boost = (std::int32_t)((baseQIdx + 544) * -computeQdeltaFp(baseQ, targetQ, bitDepth) / (255 + 1024));
        break;
    default:
        boost = (std::int32_t)((baseQIdx + 40) * -computeQdeltaFp(baseQ, targetQ, bitDepth) / (255 + 40));
        break;
    }
    boost = std::min(kVarBoostMaxDeltaqRange, boost);
    return boost;
}

// svt_av1_variance_adjust_qp (rc_aq.c:180-278): verbatim; the SB array is
// flat qindexes and the variance grid rides beside it.
void varianceAdjustQp(FrameHdrCarrier& hdr, std::uint8_t* sbQindex, const std::uint16_t* const* variance,
                      std::uint16_t scsSbTotalCount, std::uint16_t ppcsB64TotalCount, bool superresOrResize,
                      int varianceBoostStrength, int encoderBitDepth, std::uint8_t varianceOctile,
                      std::uint8_t varianceBoostCurve) {
    hdr.deltaQPresent = true;

    std::uint16_t sbCnt = scsSbTotalCount;
    if (superresOrResize) {
        sbCnt = ppcsB64TotalCount;
    }

    std::uint8_t minQindex = kMaxQ;
    std::uint8_t maxQindex = kMinQ;

    for (std::uint16_t sbAddr = 0; sbAddr < sbCnt; ++sbAddr) {
        const int boost = getDeltaqSbVarianceBoost((std::uint8_t)hdr.baseQIdx, variance[sbAddr],
            (std::uint8_t)varianceBoostStrength, encoderBitDepth, varianceOctile, varianceBoostCurve);
        // q_index 0 is lossless, and is currently not supported in SVT-AV1.
        sbQindex[sbAddr] = (std::uint8_t)clip3(sbQindex[sbAddr] - boost, 1, kMaxQ);
        minQindex = std::min(minQindex, sbQindex[sbAddr]);
        maxQindex = std::max(maxQindex, sbQindex[sbAddr]);
    }

    int range = maxQindex - minQindex;
    range = std::min(range, kVarBoostMaxDeltaqRange);
    const int normalizedBaseQIdx = (int)minQindex + (range >> 1);

    for (std::uint16_t sbAddr = 0; sbAddr < sbCnt; ++sbAddr) {
        int offset = (int)sbQindex[sbAddr] - normalizedBaseQIdx;
        offset = std::min(offset, kVarBoostMaxDeltaqRange >> 1);
        offset = std::max(offset, -(kVarBoostMaxDeltaqRange >> 1));
        const std::uint8_t normalizedQindex = (std::uint8_t)clip3(normalizedBaseQIdx + offset, 1, kMaxQ);
        sbQindex[sbAddr] = normalizedQindex;
    }
}

// is_in_cr_band (rc_aq.c:284-286): verbatim.
bool isInCrBand(std::uint32_t b64Idx, std::uint32_t sbStart, std::uint32_t sbEnd) {
    return (sbStart <= sbEnd) ? (b64Idx >= sbStart && b64Idx < sbEnd) : (b64Idx >= sbStart || b64Idx < sbEnd);
}

// svt_aom_cyclic_refresh_setup (rc_aq.c:295-342): verbatim; the
// is_cr_motion_static helper (:290-293) is inlined at its only call site.
void cyclicRefreshSetup(CrRefreshCarrier& cr, const MeStatCarrier& me) {
    cr.meDistortion[0] = 0;
    cr.meDistortion[1] = 0;
    cr.meDistortion[2] = 0;

    cr.actualNumSeg1Sbs = 0;
    cr.actualNumSeg2Sbs = 0;
    std::uint64_t seg2Dist = 0;
    const std::uint64_t avgMeDist = me.normMeDist;
    const std::uint64_t distRejectThresh = avgMeDist * 2 + 1;
    for (std::uint32_t b64Idx = 0; b64Idx < me.b64TotalCount; ++b64Idx) {
        const bool inCrRange = isInCrBand(b64Idx, cr.sbStart, cr.sbEnd);
        const bool motionStatic = me.me8x8Distortion[b64Idx] < distRejectThresh && std::abs(me.mvX[b64Idx]) <= 1 &&
            std::abs(me.mvY[b64Idx]) <= 1;
        if (inCrRange && motionStatic) {
            if (me.me8x8Distortion[b64Idx] < avgMeDist) {
                seg2Dist += me.me8x8Distortion[b64Idx];
                cr.meDistortion[2] += me.me64x64Distortion[b64Idx];
                cr.actualNumSeg2Sbs++;
            } else {
                cr.meDistortion[1] += me.me64x64Distortion[b64Idx];
                cr.actualNumSeg1Sbs++;
            }
        } else {
            cr.meDistortion[0] += me.me64x64Distortion[b64Idx];
        }
    }

    const int actualNumSeg0Sbs = me.b64TotalCount - cr.actualNumSeg1Sbs - cr.actualNumSeg2Sbs;
    cr.meDistortion[0] = actualNumSeg0Sbs ? cr.meDistortion[0] / actualNumSeg0Sbs : 0;
    cr.meDistortion[1] = cr.actualNumSeg1Sbs ? cr.meDistortion[1] / cr.actualNumSeg1Sbs : 0;
    cr.meDistortion[2] = cr.actualNumSeg2Sbs ? cr.meDistortion[2] / cr.actualNumSeg2Sbs : 0;

    // If motion gate rejected ALL SBs in the refresh range, disable CR for
    // this frame (rc_aq.c:330-333).
    if (cr.actualNumSeg1Sbs + cr.actualNumSeg2Sbs == 0) {
        cr.applyCyclicRefresh = 0;
        return;
    }
    int rateBoostFac = cr.rateBoostFac;
    if (cr.actualNumSeg2Sbs) {
        seg2Dist = seg2Dist / cr.actualNumSeg2Sbs;
        const std::uint64_t dev = (avgMeDist - seg2Dist) * 100 / avgMeDist;
        // Quadratic scaling; boost = BOOST_MAX * (dev/100)^2.
        rateBoostFac += (int)(10 * dev * dev / (100 * 100));
    }
    cr.rateRatioQdeltaSeg2 = 0.1 * rateBoostFac * cr.rateRatioQdelta;
}

// cyclic_sb_qp_assignment (rc_aq.c:350-375): verbatim.
void cyclicSbQpAssignment(FrameHdrCarrier& hdr, std::uint8_t* sbQindex, const CrRefreshCarrier& cr,
                          const MeStatCarrier& me) {
    hdr.deltaQPresent = true;

    const int baseQIdx = hdr.baseQIdx;
    const std::uint64_t distRejectThresh = me.normMeDist * 2 + 1;
    for (std::uint32_t b64Idx = 0; b64Idx < me.b64TotalCount; ++b64Idx) {
        int offset = 0;
        if (isInCrBand(b64Idx, cr.sbStart, cr.sbEnd)) {
            const bool motionStatic = me.me8x8Distortion[b64Idx] < distRejectThresh &&
                std::abs(me.mvX[b64Idx]) <= 1 && std::abs(me.mvY[b64Idx]) <= 1;
            if (!motionStatic) {
                offset = 0;
            } else if (me.me8x8Distortion[b64Idx] < me.normMeDist) {
                offset = cr.qindexDelta[2];
            } else {
                offset = cr.qindexDelta[1];
            }
        }
        sbQindex[b64Idx] = (std::uint8_t)clip3(baseQIdx + offset, 1, kMaxQ);
    }
}

// svt_av1_generate_b64_me_qindex_map (rc_aq.c:380-417): verbatim.
void generateB64MeQindexMap(const FrameHdrCarrier& hdr, const MeStatCarrier& me, std::uint8_t* b64MeQindex,
                            int sliceType, int temporalLayerIndex) {
    static const int minOffset[kMaxTemporalLayers] = {-8, -8, -8, -8, -8, -8};
    static const int maxOffset[kMaxTemporalLayers] = {8, 8, 8, 8, 8, 8};

    const int baseQIdx = hdr.baseQIdx;
    const int tlIndex = temporalLayerIndex;
    if (sliceType != kIslice && (minOffset[tlIndex] != 0 || maxOffset[tlIndex] != 0)) {
        std::int64_t avgDist = 0;
        std::int64_t minDist = INT64_MAX;
        std::int64_t maxDist = 0;
        for (std::uint32_t b64Idx = 0; b64Idx < me.b64TotalCount; ++b64Idx) {
            avgDist += me.me8x8CostVariance[b64Idx];
            minDist = std::min((std::int64_t)me.me8x8CostVariance[b64Idx], minDist);
            maxDist = std::max((std::int64_t)me.me8x8CostVariance[b64Idx], maxDist);
        }
        avgDist /= me.b64TotalCount;

        const int minQIdx = std::max(1, baseQIdx - 9 * 4 + 1);
        const int maxQIdx = std::min(kMaxQ, baseQIdx + 9 * 4 - 1);
        for (std::uint32_t b64Idx = 0; b64Idx < me.b64TotalCount; ++b64Idx) {
            const int diffDist = (int)(me.me8x8CostVariance[b64Idx] - avgDist);
            int offset = 0;
            if (diffDist < 0) {
                offset = (int)(minOffset[tlIndex] * diffDist / (minDist - avgDist));
            } else if (diffDist > 0) {
                offset = (int)(maxOffset[tlIndex] * diffDist / (maxDist - avgDist));
            }
            b64MeQindex[b64Idx] = (std::uint8_t)clip3(baseQIdx + offset, minQIdx, maxQIdx);
        }
    } else {
        for (std::uint32_t b64Idx = 0; b64Idx < me.b64TotalCount; ++b64Idx) {
            b64MeQindex[b64Idx] = (std::uint8_t)baseQIdx;
        }
    }
}

// svt_av1_get_deltaq_offset (rc_aq.c:419-451): verbatim.
int getDeltaqOffset(int bitDepth, int qindex, double beta, bool isIntra) {
    int q = dcQuantQtx(qindex, 0, bitDepth);
    int newq;
    if (!isIntra && beta > 1) {
        newq = (int)std::rint(q / std::sqrt(std::sqrt(beta)));
    } else {
        newq = (int)std::rint(q / std::sqrt(beta));
    }
    const int origQindex = qindex;
    if (newq == q) {
        return 0;
    }
    if (newq < q) {
        while (qindex > kMinQ) {
            qindex--;
            q = dcQuantQtx(qindex, 0, bitDepth);
            if (newq >= q) {
                break;
            }
        }
    } else {
        while (qindex < kMaxQ) {
            qindex++;
            q = dcQuantQtx(qindex, 0, bitDepth);
            if (newq <= q) {
                break;
            }
        }
    }
    return qindex - origQindex;
}

// svt_av1_normalize_sb_delta_q (rc_aq.c:554-592): verbatim.
void normalizeSbDeltaQ(FrameHdrCarrier& hdr, std::uint8_t* sbQindex, std::uint16_t scsSbTotalCount,
                       std::uint16_t ppcsB64TotalCount, bool superresOrResize) {
    const std::uint8_t deltaQRes = (std::uint8_t)hdr.deltaQRes;

    const std::uint8_t mask = (std::uint8_t)~(deltaQRes - 1);
    const std::uint8_t deltaQRemainder = (std::uint8_t)(hdr.baseQIdx & ~(int)mask);
    const std::int8_t deltaQAdjustment = (std::int8_t)((deltaQRes - deltaQRemainder) - (deltaQRes / 2));

    std::uint16_t sbCnt = scsSbTotalCount;
    if (superresOrResize) {
        sbCnt = ppcsB64TotalCount;
    }
    for (std::uint16_t sbAddr = 0; sbAddr < sbCnt; ++sbAddr) {
        const std::uint8_t adjustedQIndex = (std::uint8_t)clip3(sbQindex[sbAddr] + deltaQAdjustment, 1, kMaxQ);
        const std::uint8_t normalizedQIndex = (std::uint8_t)((adjustedQIndex & mask) + deltaQRemainder);
        sbQindex[sbAddr] = normalizedQIndex == 0 ? deltaQRes : normalizedQIndex;
    }
}

// svt_av1_rc_init_sb_qindex (rc_aq.c:595-628): verbatim; the TPL call site
// (rc_aq.c:624-626, CONFIG_ENABLE_TPL=0 under RTC_BUILD in the pinned
// reference build) is DCE'd upstream and PARKED here - aqMode/tplEnable/r0
// thread the gate shape so the park is named at the signature.
void rcInitSbQindex(int rcMode, CrRefreshCarrier& cr, FrameHdrCarrier& hdr, std::uint8_t* sbQindex,
                    const std::uint16_t* const* variance, std::uint16_t pcsSbTotalCount, const MeStatCarrier& me,
                    bool enableVarianceBoost, int varianceBoostStrength, int encoderBitDepth,
                    std::uint8_t varianceOctile, std::uint8_t varianceBoostCurve, int aqMode, bool tplEnable,
                    double r0) {
    (void)aqMode;
    (void)tplEnable;
    (void)r0;

    hdr.deltaQPresent = false;

    // cyclic refresh is mutually exclusive with other AQ modes and
    // overrides SB qindexes (rc_aq.c:601-603).
    if (rcMode == kRcCbr) {
        if (cr.applyCyclicRefresh) {
            cyclicSbQpAssignment(hdr, sbQindex, cr, me);
        } else {
            for (std::uint16_t sbAddr = 0; sbAddr < pcsSbTotalCount; ++sbAddr) {
                sbQindex[sbAddr] = (std::uint8_t)hdr.baseQIdx;
            }
        }
    } else {
        for (std::uint16_t sbAddr = 0; sbAddr < pcsSbTotalCount; ++sbAddr) {
            sbQindex[sbAddr] = (std::uint8_t)hdr.baseQIdx;
        }
        if (enableVarianceBoost) {
            // superres/resize selection is named-parked at this rung (no
            // carrier field threads frame_superres_enabled /
            // frame_resize_enabled yet): the non-superres branch is the
            // reference shape with sb_cnt = scs->sb_total_count.
            varianceAdjustQp(hdr, sbQindex, variance, pcsSbTotalCount, me.b64TotalCount, false,
                             varianceBoostStrength, encoderBitDepth, varianceOctile, varianceBoostCurve);
        }
    }
}

// ---- RC2c Wave B (V-series): the one-pass rate allocation + dynamic
// resize family (rc_vbr_cbr.c:107-:589) ----

// calc_active_worst_quality_no_stats_cbr (rc_vbr_cbr.c:107-163): verbatim;
// the frame_updated mutex walk (:129-131) threads the counter.
int calcActiveWorstQualityNoStatsCbr(const RcControl& rc, const PcsCarrier& ppcs, int frameUpdated) {
    // Buffer level below which we push active_worst to worst_quality.
    const std::int64_t criticalLevel = rc.optimalBufferLevel >> 3;
    std::int64_t buffLvlStep = 0;
    int adjustment = 0;
    int activeWorstQuality;
    if (ppcs.frameType == kKeyFrame) {
        return rc.worstQuality;
    }
    int ambientQp = (frameUpdated < 4)
        ? std::min(rc.avgFrameQindex[1], rc.avgFrameQindex[0])
        : rc.avgFrameQindex[1];
    ambientQp = std::min(rc.worstQuality, ambientQp);
    if (rc.bufferLevel > rc.optimalBufferLevel) {
        activeWorstQuality = std::min(rc.worstQuality, ambientQp * 5 / 4);
        // Adjust down.
        // Maximum limit for down adjustment, ~30%.
        const int maxAdjustmentDown = activeWorstQuality / 3;
        if (maxAdjustmentDown) {
            buffLvlStep = ((rc.maximumBufferSize - rc.optimalBufferLevel) / maxAdjustmentDown);
            if (buffLvlStep) {
                adjustment = (int)((rc.bufferLevel - rc.optimalBufferLevel) / buffLvlStep);
            }
            activeWorstQuality -= adjustment;
        }
    } else if (rc.bufferLevel > criticalLevel) {
        activeWorstQuality = std::min(rc.worstQuality, ambientQp);
        // Adjust up from ambient Q.
        if (criticalLevel) {
            buffLvlStep = (rc.optimalBufferLevel - criticalLevel);
            if (buffLvlStep) {
                adjustment = (int)((rc.worstQuality - ambientQp) * (rc.optimalBufferLevel - rc.bufferLevel) /
                                   buffLvlStep);
            }
            activeWorstQuality += adjustment;
        }
    } else {
        // Set to worst_quality if buffer is below critical level.
        activeWorstQuality = rc.worstQuality;
    }
    return activeWorstQuality;
}

// adjust_q_cbr (rc_vbr_cbr.c:168-205): verbatim.
int adjustQcbr(const RcControl& rc, const PcsCarrier& ppcs, int q) {
    const int maxDelta = kMaxDeltaPerLayer[ppcs.hierarchicalLevels][ppcs.temporalLayerIndex];
    const int maxDeltaDown = ppcs.scClass1
        ? std::min(maxDelta, std::max(1, rc.q1Frame / 2))
        : std::min(maxDelta, std::max(1, rc.q1Frame / 3));
    const bool changeAvgFrameBandwidth =
        std::abs(rc.avgFrameBandwidth - rc.prevAvgFrameBandwidth) > 0.1 * rc.avgFrameBandwidth;
    // If resolution changes or avg_frame_bandwidth significantly changed,
    // then set this flag to indicate change in target bits per macroblock.
    const bool changeTargetBitsMb = changeAvgFrameBandwidth;
    // Apply some control/clamp to QP under certain conditions.
    if (ppcs.frameType != kKeyFrame && rc.framesSinceKey > 1 && !changeTargetBitsMb) {
        // Adjust Q base on source content change.
        if (ppcs.temporalLayerIndex == 0 && rc.prevAvgBaseMeDist > 0 && rc.framesSinceKey > 5 &&
            rc.curAvgBaseMeDist > 0) {
            const double delta = (double)rc.curAvgBaseMeDist / (double)rc.prevAvgBaseMeDist - 1.0;
            // Push Q downwards if content change is decreasing and buffer
            // level is stable (at least 1/4-optimal level), so not
            // overshooting. Do so only for high Q to avoid excess
            // overshoot.
            if (delta < 0.0 && rc.bufferLevel > (rc.optimalBufferLevel >> 2) && q > (rc.worstQuality >> 1)) {
                const double qAdjFactor = 1.0 + 0.5 * std::tanh(4.0 * delta);
                const double qVal = convertQindexToQ(q, ppcs.encoderBitDepth);
                q += computeQdelta(qVal, qVal * qAdjFactor, ppcs.encoderBitDepth);
            }
        }
        // Make sure q is between oscillating Qs to prevent resonance.
        // Limit the decrease in Q from previous frame.
        if (rc.q1Frame - q > maxDeltaDown) {
            q = rc.q1Frame - maxDeltaDown;
        }
    }
    return std::max(std::min(q, rc.worstQuality), rc.bestQuality);
}

// get_bits_per_mb (rc_vbr_cbr.c:208-211): verbatim.
int getBitsPerMb(const PcsCarrier& ppcs, double correctionFactor, int q) {
    return rcBitsPerMb(ppcs.frameType, q, correctionFactor, ppcs.encoderBitDepth, ppcs.scClass1 ? 1 : 0);
}

// find_closest_qindex_by_rate (rc_vbr_cbr.c:217-254): verbatim.
int findClosestQindexByRate(int desiredBitsPerMb, const PcsCarrier& ppcs, double correctionFactor,
                            int bestQindex, int worstQindex) {
    // Find 'qindex' based on 'desired_bits_per_mb'.
    int low = bestQindex;
    int high = worstQindex;
    while (low < high) {
        const int mid = (low + high) >> 1;
        const int midBitsPerMb = getBitsPerMb(ppcs, correctionFactor, mid);
        if (midBitsPerMb > desiredBitsPerMb) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    // Calculate rate difference of this q index from the desired rate.
    const int currQ = low;
    const int currBitsPerMb = getBitsPerMb(ppcs, correctionFactor, currQ);
    const int currBitDiff = (currBitsPerMb <= desiredBitsPerMb) ? desiredBitsPerMb - currBitsPerMb : INT_MAX;

    // Calculate rate difference for previous q index too.
    const int prevQ = currQ - 1;
    int prevBitDiff;
    if (currBitDiff == INT_MAX || currQ == bestQindex) {
        prevBitDiff = INT_MAX;
    } else {
        const int prevBitsPerMb = getBitsPerMb(ppcs, correctionFactor, prevQ);
        prevBitDiff = prevBitsPerMb - desiredBitsPerMb;
    }

    // Pick one of the two q indices, depending on which one has rate
    // closer to the desired rate.
    return (currBitDiff <= prevBitDiff) ? currQ : prevQ;
}

// get_rate_correction_factor (rc_vbr_cbr.c:256-278): verbatim; the
// rc->rc_mutex block/release is dropped (named park).
double getRateCorrectionFactor(const RcControl& rc, const RcConfig& rcCfg, const PcsCarrier& ppcs, int width,
                               int height) {
    double rcf;
    if (rcCfg.mode == kRcVbr) {
        const int rfLvl = ppcs.frameType == kKeyFrame ? 0 : ppcs.temporalLayerIndex + 1;
        rcf = rc.rateCorrectionFactors[rfLvl];
    } else {
        if (ppcs.frameType == kKeyFrame) {
            rcf = rc.rateCorrectionFactors[kKfStd];
        } else if ((ppcs.updateType == kSvtAv1GfUpdate || ppcs.updateType == kSvtAv1ArfUpdate) &&
                   !ppcs.isOverlay && rcCfg.mode != kRcCbr) {
            rcf = rc.rateCorrectionFactors[kGfArfStd];
        } else {
            rcf = rc.rateCorrectionFactors[kInterNormal];
        }
    }
    rcf *= (double)(ppcs.frameWidth * ppcs.frameHeight) / (width * height);
    return clip3(rcf, kMinBpbFactor, kMaxBpbFactor);
}

// set_rate_correction_factor (rc_vbr_cbr.c:280-305): verbatim.
void setRateCorrectionFactor(RcControl& rc, const RcConfig& rcCfg, const PcsCarrier& ppcs, double factor,
                             int width, int height) {
    // Normalize RCF to account for the size-dependent scaling factor.
    factor /= (double)(ppcs.frameWidth * ppcs.frameHeight) / (width * height);

    factor = clip3(factor, kMinBpbFactor, kMaxBpbFactor);

    if (rcCfg.mode == kRcVbr) {
        const int rfLvl = ppcs.frameType == kKeyFrame ? 0 : ppcs.temporalLayerIndex + 1;
        rc.rateCorrectionFactors[rfLvl] = factor;
    } else {
        if (ppcs.frameType == kKeyFrame) {
            rc.rateCorrectionFactors[kKfStd] = factor;
        } else if ((ppcs.updateType == kSvtAv1GfUpdate || ppcs.updateType == kSvtAv1ArfUpdate) &&
                   !ppcs.isOverlay && rcCfg.mode != kRcCbr) {
            rc.rateCorrectionFactors[kGfArfStd] = factor;
        } else {
            rc.rateCorrectionFactors[kInterNormal] = factor;
        }
    }
}

// av1_rc_regulate_q (rc_vbr_cbr.c:307-322): verbatim.
int av1RcRegulateQ(const RcControl& rc, const RcConfig& rcCfg, const PcsCarrier& ppcs, int activeBestQuality,
                   int activeWorstQuality, int width, int height) {
    const int mbs = ((width + 15) / 16) * ((height + 15) / 16);
    const double correctionFactor = getRateCorrectionFactor(rc, rcCfg, ppcs, width, height);
    const int targetBitsPerMb = (int)(((std::uint64_t)ppcs.thisFrameTarget << kBperMbNormbits) / mbs);

    const int q = findClosestQindexByRate(
        targetBitsPerMb, ppcs, correctionFactor, activeBestQuality, activeWorstQuality);
    if (rcCfg.mode == kRcCbr) {
        return adjustQcbr(rc, ppcs, q);
    }

    return q;
}

// svt_av1_resize_reset_rc (rc_vbr_cbr.c:324-358): verbatim.
void resizeResetRc(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs, int resizeWidth, int resizeHeight,
                   int prevWidth, int prevHeight, int frameUpdated) {
    const double totScaleChange = (double)(resizeWidth * resizeHeight) / (double)(prevWidth * prevHeight);
    // Reset buffer level to optimal, update target size.
    resetUpdateFrameTarget(rc, rcCfg, ppcs);
    if (totScaleChange > 4.0) {
        rc.avgFrameQindex[1] = rc.worstQuality;
    } else if (totScaleChange > 1.0) {
        rc.avgFrameQindex[1] = (rc.avgFrameQindex[1] + rc.worstQuality) >> 1;
    }
    const int activeWorstQuality = calcActiveWorstQualityNoStatsCbr(rc, ppcs, frameUpdated);
    const int qindex =
        av1RcRegulateQ(rc, rcCfg, ppcs, rc.bestQuality, activeWorstQuality, resizeWidth, resizeHeight);
    // If resize is down, check if projected q index is close to
    // worst_quality, and if so, reduce the rate correction factor (since
    // likely can afford lower q for resized frame).
    if (totScaleChange < 1.0 && qindex > 90 * rc.worstQuality / 100) {
        rc.rateCorrectionFactors[kInterNormal] *= 0.85;
    }
    // If resize is back up: check if projected q index is too much above
    // the previous index, and if so, reduce the rate correction factor
    // (since prefer to keep q for resized frame at least closet to previous
    // q). Also check if projected qindex is close to previous qindex, if so
    // increase correction factor (to push qindex higher and avoid
    // overshoot).
    if (totScaleChange >= 1.0) {
        if (totScaleChange < 4.0 && qindex > 130 * rc.lastQ[1] / 100) {
            rc.rateCorrectionFactors[kInterNormal] *= 0.8;
        }
        if (qindex <= 120 * rc.lastQ[1] / 100) {
            rc.rateCorrectionFactors[kInterNormal] *= 1.5;
        }
    }
}

// set_gf_interval_update_onepass_rt (rc_vbr_cbr.c:363-380): verbatim.
bool setGfIntervalUpdateOnepassRt(RcControl& rc, const PcsCarrier& pcs) {
    bool gfUpdate = false;
    // GF update based on frames_till_gf_update_due, also force update on
    // resize pending frame or for scene change.
    if (pcs.frameOffset % kMaxGfInterval == 0) {
        rc.baselineGfInterval = kMaxGfInterval;
        if (rc.baselineGfInterval > rc.framesToKey) {
            rc.baselineGfInterval = rc.framesToKey;
        }
        rc.gfuBoost = kDefaultGfBoostRt;
        rc.constrainedGfGroup = (rc.baselineGfInterval >= rc.framesToKey) ? 1 : 0;
        gfUpdate = true;
    }
    return gfUpdate;
}

// dynamic_resize_one_pass_cbr (rc_vbr_cbr.c:382-492): verbatim; the
// SVT_DEBUG prints are dropped (log macros); the scs walls
// (new_framerate, static_config.rtc, max_input_luma_*) thread as
// parameters.
void dynamicResizeOnePassCbr(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs, bool oneHalfOnly,
                             double newFramerate, bool rtc, int maxInputLumaWidth, int maxInputLumaHeight,
                             int frameUpdated) {
    int resizeAction = kResizeNoResize;
    const int prevResizeState = rc.resizeState;  // resize state before any transition this frame
    const int avgQpThr1 = 70;
    const int avgQpThr2 = 50;
    // Don't allow for resized frame to go below 160x90, resize in steps of
    // 3/4.
    const int minWidth = (160 * 4) / 3;
    const int minHeight = (90 * 4) / 3;
    bool downSizeOn = true;

    // Step 1: check frame type
    // Don't resize on key frame; reset the counters on key frame.
    if (ppcs.frameType == kKeyFrame) {
        rc.resizeAvgQp = 0;
        rc.resizeCount = 0;
        rc.resizeBufferUnderflow = 0;
        return;
    }

    // Step 2: check frame size
    // No resizing down if frame size is below some limit.
    if (ppcs.frameWidth * ppcs.frameHeight < minWidth * minHeight) {
        downSizeOn = false;
    }

    // Step 3: calculate dynamic resize state
    // Resize based on average buffer underflow and QP over some window.
    // Ignore samples close to key frame and scene change, since QP is
    // usually high after key and scene change (scene_change_flag is SVT's
    // equivalent of libaom rc->high_source_sad).
    if (rc.framesSinceKey > newFramerate && !ppcs.sceneChangeFlag) {
        const int window = std::max(60, (int)(3 * newFramerate));
        rc.resizeAvgQp += rc.lastQ[1];
        // Detect buffer underflow. The RTC-CBR path (rc_rtc_cbr.c) models
        // the client buffer with an INVERTED leaky bucket: use the inverse
        // of libaom's test there so the same 30%-of-optimal margin applies
        // in both conventions (rc_vbr_cbr.c:422-425).
        const bool bufferUnderflow = rtc
            ? (rc.bufferLevel >
               rc.maximumBufferSize - 30 * (rc.maximumBufferSize - rc.optimalBufferLevel) / 100)
            : (rc.bufferLevel < 30 * rc.optimalBufferLevel / 100);
        if (bufferUnderflow) {
            ++rc.resizeBufferUnderflow;
        }
        ++rc.resizeCount;
        // Check for resize action every "window" frames.
        if (rc.resizeCount >= window) {
            const int avgQp = rc.resizeAvgQp / rc.resizeCount;
            // Resize down if buffer level has underflowed sufficient amount
            // in past window, and we are at original or 3/4 of original
            // resolution. Resize back up if average QP is low, and we are
            // currently in a resized down state, i.e. 1/2 or 3/4 of
            // original resolution. Currently, use a flag to turn 3/4
            // resizing feature on/off.
            if (rc.resizeBufferUnderflow > (rc.resizeCount >> 2) && downSizeOn) {
                if (rc.resizeState == kResizeThreeQuarter) {
                    resizeAction = kResizeDownOneHalf;
                    rc.resizeState = kResizeOneHalf;
                } else if (rc.resizeState == kResizeOrig) {
                    const int next = oneHalfOnly ? kResizeOneHalf : kResizeThreeQuarter;
                    resizeAction = oneHalfOnly ? kResizeDownOneHalf : kResizeDownThreeFour;
                    rc.resizeState = next;
                }
            } else if (rc.resizeState != kResizeOrig && avgQp < avgQpThr1 * rc.worstQuality / 100) {
                if (rc.resizeState == kResizeThreeQuarter || avgQp < avgQpThr2 * rc.worstQuality / 100 ||
                    oneHalfOnly) {
                    resizeAction = kResizeUpOrig;
                    rc.resizeState = kResizeOrig;
                } else if (rc.resizeState == kResizeOneHalf) {
                    resizeAction = kResizeUpThreeFour;
                    rc.resizeState = kResizeThreeQuarter;
                }
            }
            // Reset for next window measurement.
            rc.resizeAvgQp = 0;
            rc.resizeCount = 0;
            rc.resizeBufferUnderflow = 0;
        }
    }

    // Step 4: reset rate control configuration
    // If decision is to resize, reset some quantities, and check is we
    // should reduce rate correction factor,
    if (resizeAction != kResizeNoResize) {
        // Derive coded dimensions from the ORIGINAL configured size and
        // the resize state (libaom uses oxcf.frm_dim_cfg here, not the
        // current frame size), so the scale change handed to
        // svt_av1_resize_reset_rc is relative to the PREVIOUS resize state
        // rather than always relative to the original.
        const int origW = maxInputLumaWidth;
        const int origH = maxInputLumaHeight;
        int newW = origW, newH = origH, prevW = origW, prevH = origH;
        if (rc.resizeState == kResizeThreeQuarter) {
            newW = origW * 3 / 4, newH = origH * 3 / 4;
        } else if (rc.resizeState == kResizeOneHalf) {
            newW = origW / 2, newH = origH / 2;
        }
        if (prevResizeState == kResizeThreeQuarter) {
            prevW = origW * 3 / 4, prevH = origH * 3 / 4;
        } else if (prevResizeState == kResizeOneHalf) {
            prevW = origW / 2, prevH = origH / 2;
        }
        resizeResetRc(rc, rcCfg, ppcs, newW, newH, prevW, prevH, frameUpdated);
    }
}

// svt_aom_dynamic_resize_decision (rc_vbr_cbr.c:497-516): verbatim (the
// caller passes one_half_only = 1); the resize_pending_params wall threads
// as the output carrier.
void dynamicResizeDecision(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs, double newFramerate, bool rtc,
                           int maxInputLumaWidth, int maxInputLumaHeight, int frameUpdated,
                           ResizePendingCarrier& pending) {
    // libaom's real-time caller always passes one_half_only=1 (ORIG <-> 1/2
    // only; the 3/4 ladder is unused by the RT path). Match that for
    // parity.
    dynamicResizeOnePassCbr(rc, rcCfg, ppcs, true, newFramerate, rtc, maxInputLumaWidth, maxInputLumaHeight,
                            frameUpdated);
    if (rc.resizeState != pending.resizeState) {
        if (rc.resizeState == kResizeOrig) {
            pending.resizeDenom = kScaleNumerator;
        } else if (rc.resizeState == kResizeThreeQuarter) {
            pending.resizeDenom = kScaleThreeQuater;
        } else if (rc.resizeState == kResizeOneHalf) {
            pending.resizeDenom = kScaleDenominatorMax;
        } else {
            // unknown resize denom (the reference asserts)
        }
        pending.resizeState = rc.resizeState;
    }
}

// svt_aom_one_pass_rt_rate_alloc (rc_vbr_cbr.c:553-589): verbatim; the
// scs->static_config gates (resize_mode == RESIZE_DYNAMIC, pass ==
// ENC_SINGLE_PASS, pred_structure == LOW_DELAY) thread as booleans.
void onePassRtRateAlloc(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& pcs, double newFramerate, bool rtc,
                        bool resizeDynamic, bool singlePass, bool lowDelay, bool rcResetFlag,
                        int intraPeriodLength, int maxInputLumaWidth, int maxInputLumaHeight,
                        int frameUpdated, ResizePendingCarrier& pending) {
    // Set frame type.
    if (frameIsIntraOnly(pcs.frameType)) {
        rc.kfBoost = kDefaultKfBoostRt;
        rc.thisKeyFrameForced = (pcs.pictureNumber != 0 && rc.framesToKey == 0) ? 1 : 0;
        rc.framesToKey = intraPeriodLength + 1;
    }

    /* resize dynamic mode makes its decision here and stores it in
     * resize_pending_params; the actual resizing happens on the next new
     * input picture in PD. Resize dynamic mode only works with 1-pass CBR
     * low delay mode (rc_vbr_cbr.c:565-573). */
    if (resizeDynamic && singlePass && lowDelay) {
        dynamicResizeDecision(rc, rcCfg, pcs, newFramerate, rtc, maxInputLumaWidth, maxInputLumaHeight,
                              frameUpdated, pending);
    } else if (rcResetFlag) {
        resizeResetRc(rc, rcCfg, pcs, pcs.renderWidth, pcs.renderHeight, maxInputLumaWidth, maxInputLumaHeight,
                      frameUpdated);
    }

    // Set the GF interval and update flag.
    setGfIntervalUpdateOnepassRt(rc, pcs);

    // Set target size.
    int target;
    if (pcs.frameType == kKeyFrame) {
        target = calcIframeTargetSizeOnePassCbr(rc, rcCfg, pcs, newFramerate);
    } else {
        target = calcPframeTargetSizeOnePassCbr(rc, rcCfg);
    }
    pcs.thisFrameTarget = target;
    pcs.baseFrameTarget = target;
}

// ---- RC2c Wave C1 (RTC-series): the rc_rtc_cbr.c primitives ----

// get_msb (definitions.h:596-605): the portable floor(log2) for n != 0
// (the reference's MSVC branch scans via _BitScanReverse; the assert on
// n != 0 is dropped - the loop form returns 0 for 0).
int getMsb(std::uint32_t n) {
    int r = 0;
    while (n >>= 1) {
        ++r;
    }
    return r;
}

// av1_estimate_bits_at_qindex (rc_rtc_cbr.c:42-58): verbatim.
double estimateBitsAtQindex(bool keyFrame, int minRefBaseQIdx, int qindex, double rcf, std::uint64_t meDist,
                            int bitDepth) {
    const double quantizer = convertQindexToQ(qindex, bitDepth);
    double refScale = 1.0;
    std::int64_t complexity = 5600;
    if (!keyFrame) {
        const double refQ = convertQindexToQ(minRefBaseQIdx, bitDepth);
        refScale = std::sqrt(refQ) / quantizer;
        complexity = std::max<std::int64_t>((std::int64_t)meDist, 64 * 64 / 4);
    }
    return rcf * complexity * refScale / quantizer;
}

// find_closest_arg (rc_rtc_cbr.c:73-97): verbatim; the eval context is a
// callable (the reference's arg_eval_fn + void* pair).
int findClosestArg(double target, int minArg, int maxArg, const std::function<double(int)>& eval) {
    int loArg = minArg;
    int hiArg = maxArg;
    while (loArg < hiArg) {
        const int midArg = (loArg + hiArg) >> 1;
        const double midVal = eval(midArg);
        if (midVal > target) {
            loArg = midArg + 1;
        } else {
            hiArg = midArg;
        }
    }

    int currArg = loArg;
    if (currArg > minArg) {
        const double prevVal = eval(loArg - 1);
        const double currVal = eval(loArg);
        if (std::fabs(prevVal - target) < std::fabs(currVal - target)) {
            currArg = loArg - 1;
        }
    }
    return currArg;
}

// rtc_compute_cr_deltaq (rc_rtc_cbr.c:103-121): verbatim; the rc/cfg
// walls thread (best/worst quality, min/max qp allowed).
int rtcComputeCrDeltaq(const CrRefreshCarrier& cr, const RcControl& rc, const RcConfig& rcCfg, bool keyFrame,
                       int qindex, double rcf, std::uint64_t meDist, double rateRatio, int bitDepth) {
    // Get current estimated bits for given qindex, then target.
    const double baseBits = estimateBitsAtQindex(keyFrame, rc.minRefBaseQIdx, qindex, rcf, meDist, bitDepth);
    const double targetBits = rateRatio * baseBits;

    const int minQindex = (rateRatio < 1.0) ? qindex : rc.bestQuality;
    const int maxQindex = (rateRatio < 1.0) ? rc.worstQuality : qindex;

    const auto evalBlockBits = [&](int q) {
        return estimateBitsAtQindex(keyFrame, rc.minRefBaseQIdx, q, rcf, meDist, bitDepth);
    };

    // Find closest qindex to generate this size.
    int targetIndex = findClosestArg(targetBits, minQindex, maxQindex, evalBlockBits);

    targetIndex = clampQindex(rcCfg, targetIndex);
    return std::max(targetIndex - qindex, -cr.maxQdeltaPerc * qindex / 100);
}

// rtc_cyclic_refresh_compute_cr_qdeltas (rc_rtc_cbr.c:123-136): verbatim.
void rtcCyclicRefreshComputeCrQdeltas(CrRefreshCarrier& cr, const RcControl& rc, const RcConfig& rcCfg,
                                      bool keyFrame, int qindex, double rcf, int bitDepth) {
    cr.qindexDelta[0] = 0;
    cr.qindexDelta[1] = 0;
    cr.qindexDelta[2] = 0;
    if (cr.actualNumSeg1Sbs) {
        const double qdelta = std::min(4.0, cr.rateRatioQdelta);
        cr.qindexDelta[1] = rtcComputeCrDeltaq(cr, rc, rcCfg, keyFrame, qindex, rcf, cr.meDistortion[1], qdelta,
                                               bitDepth);
    }
    if (cr.actualNumSeg2Sbs) {
        const double qdelta = std::min(8.0, cr.rateRatioQdeltaSeg2);
        cr.qindexDelta[2] = rtcComputeCrDeltaq(cr, rc, rcCfg, keyFrame, qindex, rcf, cr.meDistortion[2], qdelta,
                                               bitDepth);
    }
}

// av1_estimate_frame_size (rc_rtc_cbr.c:138-163): verbatim; the CR
// weighting consumes the carrier's segment state.
int estimateFrameSize(const RcControl& rc, const RcConfig& rcCfg, const CrRefreshCarrier& cr, bool keyFrame,
                       int qindex, double rcf, int frameWidth, int frameHeight, std::uint16_t b64TotalCount,
                       bool calcSbQindex, int bitDepth) {
    double estimatedSize;
    if (cr.applyCyclicRefresh) {
        if (calcSbQindex) {
            rtcCyclicRefreshComputeCrQdeltas(const_cast<CrRefreshCarrier&>(cr), rc, rcCfg, keyFrame, qindex, rcf,
                                             bitDepth);
        }

        // Weight for non-base segments.
        const double w1 = (double)cr.actualNumSeg1Sbs / b64TotalCount;
        const double w2 = (double)cr.actualNumSeg2Sbs / b64TotalCount;

        // Take segment weighted average for estimated bits.
        estimatedSize =
            (1.0 - w1 - w2) * estimateBitsAtQindex(keyFrame, rc.minRefBaseQIdx, qindex, rcf, cr.meDistortion[0], bitDepth) +
            w1 * estimateBitsAtQindex(keyFrame, rc.minRefBaseQIdx, qindex + cr.qindexDelta[1], rcf, cr.meDistortion[1], bitDepth) +
            w2 * estimateBitsAtQindex(keyFrame, rc.minRefBaseQIdx, qindex + cr.qindexDelta[2], rcf, cr.meDistortion[2], bitDepth);
    } else {
        estimatedSize = estimateBitsAtQindex(keyFrame, rc.minRefBaseQIdx, qindex, rcf, rc.curAvgBaseMeDist, bitDepth);
    }

    // scale to resolution
    return (int)std::max(estimatedSize * frameWidth * frameHeight / 512, 1.0);
}

// normalize_factors (rc_rtc_cbr.c:175-184): verbatim (the reference calls
// it in-place; the pointers may alias).
void normalizeFactors(double* dst, const double* src, int iStart, int iEnd) {
    double sum = 0.0;
    for (int k = iStart; k < iEnd; k++) {
        sum += src[k] * (1 << std::max(k - iStart - 1, 0));
    }
    const double avgFactor = sum / (1 << std::max(iEnd - iStart - 1, 0));
    for (int k = iStart; k < iEnd; k++) {
        dst[k] = src[k] / avgFactor;
    }
}

// index2tl (rc_rtc_cbr.c:186-188): verbatim.
int index2tl(int index, int levels) {
    return index ? levels - getMsb(index ^ (index - 1)) : 0;
}

// calc_pframe_target_size (rc_rtc_cbr.c:190-269): verbatim; the
// pred_structure gate threads as randomAccess (RANDOM_ACCESS -> 0.5).
int calcPframeTargetSizeRtc(RcControl& rc, const RcConfig& rcCfg, const PcsCarrier& ppcs, bool randomAccess) {
    if (rc.rcMiniGopPos == (rc.miniQopSize - 1)) {
        double weights[kMaxTemporalLayers + 1] = {0};
        double rcfTlx[kMaxTemporalLayers + 1] = {0};

        // prepare weighted RCFs - core components of layer weights
        const int numLayers = rc.rcNumLayers;
        for (int k = 1; k < rc.miniQopSize + 1; k++) {
            const int kTl = index2tl(k - 1, numLayers - 1);
            rcfTlx[kTl + 1] += rc.rcfValues[k] / (1 << std::max(kTl - 1, 0));
        }

        if (ppcs.hierarchicalLevels == 0) {
            for (int k = 1; k < numLayers + 1; k++) {
                weights[k] = rcfTlx[k];
            }
        } else {
            normalizeFactors(rcfTlx, rcfTlx, 1, numLayers + 1);

            // Adaptive rcf_last + avg_frame_qindex + avg_zeromv model:
            const double rcfLast = rcfTlx[1 + numLayers - 1];
            const double avgQindex = rc.avgFrameQindex[1];
            const double avgZeromv = rc.avgFrameLowMotion;

            // Base layer weight is derived from encoding properties:
            const double w0 = -9.32 * rcfLast + 0.023 * avgQindex + 0.034 * avgZeromv + 8.14;

            // Saturating intermediate weights; generalizes to N layers
            // (k=1 is base, k=num_layers is top).
            const double amplitude = 0.81;
            const double rate = 0.83;

            // adjustment for bidirectional prediction efficiency
            const double scale = randomAccess ? 0.5 : 1.0;

            weights[1] = w0;
            for (int k = 2; k < numLayers + 1; k++) {
                const double amp = amplitude * (numLayers - k) / (numLayers - 1);
                const double wK = 1.0 + amp * (1.0 - std::exp(-rate * (std::max(w0, 1.0) - 1.0)));
                weights[k] = wK * scale;
            }
        }

        // Enforce w0 >= w1 >= ... >= wN (monotonically non-increasing)
        for (int k = numLayers; k >= 2; k--) {
            weights[k - 1] = std::max(weights[k - 1], weights[k]);
        }

        normalizeFactors(rc.targetSizeFactors, weights, 1, numLayers + 1);
    }

    double frameTarget = rc.avgFrameBandwidth;
    double bufferDiff = (double)rc.bufferLevel - rc.optimalBufferLevel;
    const double onePctBits = 1.0 + rc.optimalBufferLevel / 100.0;

    // temporal dependency and mode decision modulation
    const int virtualTl = index2tl(rc.rcMiniGopPos + 1, rc.rcNumLayers - 1);
    frameTarget *= rc.targetSizeFactors[virtualTl + 1];

    // buffer adjustment, estimate buffer level after this frame
    bufferDiff += frameTarget - rc.avgFrameBandwidth;
    if (bufferDiff > 0) {
        // Lower the target for this frame.
        const double pct = std::min(bufferDiff / onePctBits, (double)rcCfg.overShootPct);
        frameTarget *= 1.0 - pct / 200;
    } else if (rc.bufferLevel < rc.avgFrameBandwidth / 4) {
        // Increase the target for this frame, less aggressively
        const double pct = std::min(-bufferDiff / onePctBits, (double)rcCfg.underShootPct);
        frameTarget *= 1.0 + pct / 400;
    }

    return (int)frameTarget;
}

// cr_select_sbs (rc_rtc_cbr.c:272-298): verbatim; enc_ctx->cr_sb_index
// threads by reference (the persistent cycling index).
void crSelectSbs(CrRefreshCarrier& cr, std::uint32_t& crSbIndex, std::uint16_t sbsInFrame) {
    const int sbs = sbsInFrame;
    const int sbTarget = cr.percentRefresh * sbs / 100;

    if (sbTarget <= 0) {
        cr.sbStart = 0;
        cr.sbEnd = 0;
        return;
    }

    if ((int)crSbIndex >= sbs) {
        crSbIndex = 0;
    }

    cr.sbStart = crSbIndex;
    int sbEnd = crSbIndex + sbTarget;
    if (sbEnd >= sbs) {
        sbEnd -= sbs;
    }
    cr.sbEnd = (std::uint32_t)sbEnd;

    crSbIndex = sbEnd;
}

// rtc_cyclic_refresh_init (rc_rtc_cbr.c:300-360): verbatim;
// super_block_size threads (the != 64 gate); frame_is_leaf consumes
// ppcs.updateType.
void rtcCyclicRefreshInit(RcControl& rc, CrRefreshCarrier& cr, const PcsCarrier& ppcs, int rcMode,
                          int superBlockSize, std::uint32_t& crSbIndex, std::uint16_t sbsInFrame,
                          int bitDepth) {
    const bool isInterBaseLayer =
        ppcs.sliceType != kIslice && ppcs.temporalLayerIndex == 0 && !frameIsLeaf(ppcs.updateType);
    // Technically it could be used in VBR too (rc_rtc_cbr.c:306-308).
    cr.applyCyclicRefresh = (rcMode == kRcCbr && isInterBaseLayer) ? 1 : 0;

    if (superBlockSize != 64) {
        cr.applyCyclicRefresh = 0;
    }

    const int qpMinThresh = std::max(16, rc.bestQuality + 4);
    const int qpMaxThresh = 118 * kMaxQ >> 7;

    if (rc.avgFrameQindex[1] > qpMaxThresh) {
        cr.applyCyclicRefresh = 0;
    }

    if (rc.avgFrameQindex[1] < qpMinThresh) {
        cr.applyCyclicRefresh = 0;
    }

    if (rc.avgFrameLowMotion < 50) {
        cr.applyCyclicRefresh = 0;
    }

    cr.percentRefresh = 20;

    if (cr.percentRefresh <= 0) {
        cr.applyCyclicRefresh = 0;
    }

    if (!cr.applyCyclicRefresh) {
        return;
    }

    // Select SBs for refresh by cycling through the frame
    crSelectSbs(cr, crSbIndex, sbsInFrame);
    if (cr.sbStart == 0 && cr.sbEnd == 0) {
        cr.applyCyclicRefresh = 0;
        return;
    }

    // Quantizer-based multiplicative adjustment
    const double avgQ = convertQindexToQ(rc.avgFrameQindex[1], bitDepth);

    // these values depend on the R-Q model in av1_estimate_bits_at_qindex
    // (rc_rtc_cbr.c:350-355)
    const double rateRatioQdeltaBase = 2.7;
    const double rateRatioQdeltaScale = 170.0;
    const int rateBoostFac = 18;

    cr.maxQdeltaPerc = 60;
    cr.rateRatioQdelta = rateRatioQdeltaBase + avgQ / rateRatioQdeltaScale;
    cr.rateBoostFac = rateBoostFac;
}

// get_rcf_index (rc_rtc_cbr.c:362-368): verbatim.
int getRcfIndex(bool keyFrame, int rcMiniGopPos, int miniQopSize) {
    if (keyFrame) {
        return 0;
    }
    return ((rcMiniGopPos + 1) % miniQopSize) + 1;
}

// rtc_get_rate_correction_factor (rc_rtc_cbr.c:370-381): verbatim (NO
// fclamp on get - unlike the vbr-path getter).
double rtcGetRateCorrectionFactor(const RcControl& rc, const PcsCarrier& ppcs, int width, int height) {
    const double rcf = rc.rcfValues[getRcfIndex(ppcs.frameType == kKeyFrame, rc.rcMiniGopPos, rc.miniQopSize)];

    // Normalize RCF to account for the size-dependent scaling factor.
    const double resScale = (double)(ppcs.frameWidth * ppcs.frameHeight) / (width * height);

    return rcf * resScale;
}

// rtc_set_rate_correction_factor (rc_rtc_cbr.c:383-400): verbatim (the
// pessimistic reset loop when frames_since_key < mini_qop_size).
void rtcSetRateCorrectionFactor(RcControl& rc, const PcsCarrier& ppcs, double rcf, int width, int height) {
    // Normalize RCF to account for the size-dependent scaling factor.
    const double resScale = (double)(ppcs.frameWidth * ppcs.frameHeight) / (width * height);

    rcf = clip3(rcf, kMinBpbFactor, kMaxBpbFactor) / resScale;

    const int idx = getRcfIndex(ppcs.frameType == kKeyFrame, rc.rcMiniGopPos, rc.miniQopSize);
    rc.rcfValues[idx] = rcf;
    if (rc.framesSinceKey < rc.miniQopSize) {
        // Reset all factors pessimistically at start as initial values
        // could be off
        for (int i = 1; i < rc.miniQopSize + 1; ++i) {
            rc.rcfValues[i] = std::max(rcf, rc.rcfValues[i]);
        }
    }
}

// calculate_qindex (rc_rtc_cbr.c:402-457): verbatim; the ref-object wall
// threads through PcsCarrier; the unconditional cyclic-refresh setup call
// (:447) is the landed AQ4 body.
int calculateQindexRtc(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs, CrRefreshCarrier& cr,
                       const MeStatCarrier& me, std::uint32_t& crSbIndex, std::uint16_t sbsInFrame,
                       int superBlockSize, int intraPeriodLength, bool randomAccess) {
    int minQindex = rc.bestQuality;
    int maxQindex = rc.worstQuality;
    const int vbvBudget = (int)(rc.maximumBufferSize - rc.bufferLevel + rc.avgFrameBandwidth);
    int maxSize = std::min(rc.maxFrameBandwidth, vbvBudget);
    const int minSize = std::max(rc.avgFrameBandwidth >> 4, kFrameOverheadBits);

    const bool keyFrame = frameIsIntraOnly(ppcs.frameType);
    if (keyFrame) {
        rc.framesToKey = intraPeriodLength + 1;

        ppcs.baseFrameTarget = (int)(rc.avgFrameBandwidth * rc.targetSizeFactors[0]);

        if (rcCfg.maxIntraBitratePct && rcCfg.mode == kRcCbr) {
            const int maxi = rc.avgFrameBandwidth * rcCfg.maxIntraBitratePct / 100;
            maxSize = std::min(maxSize, maxi);
        }
    } else {
        rc.minRefBaseQIdx = ppcs.ref0BaseQIdx;
        if (ppcs.refList1CountTry > 0) {
            rc.minRefBaseQIdx = std::min(rc.minRefBaseQIdx, ppcs.ref1BaseQIdx);
        }

        if (ppcs.ref0SliceType == kIslice) {
            minQindex = std::max(minQindex, rc.minRefBaseQIdx - 1 * 4);
        } else {
            const bool isHigherLayer =
                ppcs.ref0TmpLayer < ppcs.temporalLayerIndex && (ppcs.hierarchicalLevels != 0);
            const int minLimit = isHigherLayer ? 0 : 4;
            const int maxLimit = isHigherLayer ? 32 : 16;
            minQindex = std::max(minQindex, rc.minRefBaseQIdx - minLimit * 4);
            maxQindex = std::min(maxQindex, rc.minRefBaseQIdx + maxLimit * 4);
        }
        ppcs.baseFrameTarget = calcPframeTargetSizeRtc(rc, rcCfg, ppcs, randomAccess);

        if (rcCfg.maxInterBitratePct && rcCfg.mode == kRcCbr) {
            const int maxp = rc.avgFrameBandwidth * rcCfg.maxInterBitratePct / 100;
            maxSize = std::min(maxSize, maxp);
        }
    }
    ppcs.baseFrameTarget = std::max(std::min(ppcs.baseFrameTarget, maxSize), minSize);
    ppcs.thisFrameTarget = ppcs.baseFrameTarget;

    rtcCyclicRefreshInit(rc, cr, ppcs, rcCfg.mode, superBlockSize, crSbIndex, sbsInFrame, ppcs.encoderBitDepth);
    cyclicRefreshSetup(cr, me);

    const int width = ppcs.frameWidth;
    const int height = ppcs.frameHeight;
    const double rcf = rtcGetRateCorrectionFactor(rc, ppcs, width, height);

    const auto evalFrameSize = [&](int q) {
        return estimateFrameSize(rc, rcCfg, cr, keyFrame, q, rcf, width, height, me.b64TotalCount, true,
                                 ppcs.encoderBitDepth);
    };

    const int qindex = findClosestArg(ppcs.thisFrameTarget, minQindex, maxQindex, evalFrameSize);
    return clampQindex(rcCfg, qindex);
}

// ---- RC2c Wave C2 (RTC-series): the driver family ----

// ROUND_POWER_OF_TWO (definitions.h:457): verbatim.
std::int64_t roundPowerOfTwo(std::int64_t value, int n) {
    return (value + (((std::int64_t)1 << n) >> 1)) >> n;
}

// av1_rc_update_framerate (pass2_strategy.c:880-898) folded into rcInit:
// MAX_MB_RATE 250, MAXRATE_1080P 2025000 (:877-878).
// svt_av1_rc_init (rc_process.c:498-536): verbatim; the scs walls thread
// (the new_framerate mirror is read by the update_framerate helper).
void rcInit(RcControl& rc, const RcConfig& rcCfg, double& newFramerate, int frameRateNumerator,
            int frameRateDenominator, int hierarchicalLevels, std::int64_t targetBitRate, int numMbs,
            int vbrmaxSection) {
    int i;
    if (rcCfg.mode == kRcCbr) {
        rc.avgFrameQindex[0] = rcCfg.worstAllowedQ;
        rc.avgFrameQindex[1] = rcCfg.worstAllowedQ;
        rc.lastQ[0] = rcCfg.worstAllowedQ;
        rc.lastQ[1] = rcCfg.worstAllowedQ;
    } else {
        rc.avgFrameQindex[0] = (rcCfg.worstAllowedQ + rcCfg.bestAllowedQ) / 2;
        rc.avgFrameQindex[1] = (rcCfg.worstAllowedQ + rcCfg.bestAllowedQ) / 2;
        rc.lastQ[0] = (rcCfg.worstAllowedQ + rcCfg.bestAllowedQ) / 2;
        rc.lastQ[1] = (rcCfg.worstAllowedQ + rcCfg.bestAllowedQ) / 2;
    }
    rc.bufferLevel = rc.startingBufferLevel;
    rc.bitsOffTarget = rc.startingBufferLevel;

    rc.rollingTargetBits = rc.avgFrameBandwidth;
    rc.rollingActualBits = rc.avgFrameBandwidth;
    rc.totalActualBits = 0;
    rc.totalTargetBits = 0;

    rc.framesSinceKey = 8;  // Sensible default for first frame.
    rc.framesSinceCdfUpdate = 0;
    rc.thisKeyFrameForced = 0;
    for (i = 0; i < kMaxTemporalLayers + 1; ++i) {
        rc.rateCorrectionFactors[i] = 0.7;
    }
    if (rcCfg.mode != kRcCbr) {
        rc.rateCorrectionFactors[kKfStd] = 1.0;
    }
    rc.baselineGfInterval = 1 << hierarchicalLevels;

    // Set absolute upper and lower quality limits
    rc.worstQuality = rcCfg.worstAllowedQ;
    rc.bestQuality = rcCfg.bestAllowedQ;
    if (rcCfg.mode != kRcQ) {
        const double frameRate = (double)frameRateNumerator / (double)frameRateDenominator;
        // svt_av1_new_framerate (pass2_strategy.c:901-904): verbatim.
        newFramerate = frameRate < 0.1 ? 30 : frameRate;
        // av1_rc_update_framerate (pass2_strategy.c:880-898): verbatim.
        rc.avgFrameBandwidth = (int)(targetBitRate / newFramerate);
        std::int64_t vbrMaxBits =
            (std::int64_t)rc.avgFrameBandwidth * vbrmaxSection / 100;
        vbrMaxBits = std::min(vbrMaxBits, (std::int64_t)INT_MAX);
        const int mbsFrames = numMbs * 250;  // MAX_MB_RATE
        rc.maxFrameBandwidth =
            (int)std::max(std::max((std::int64_t)mbsFrames, (std::int64_t)2025000), vbrMaxBits);  // MAXRATE_1080P
    }
    // current and previous average base layer ME distortion
    rc.curAvgBaseMeDist = 0;
    rc.prevAvgBaseMeDist = 0;
    rc.avgFrameLowMotion = 0;
}

// svt_av1_rc_calc_qindex_rtc_cbr (rc_rtc_cbr.c:459-549): verbatim; the
// resize gate and the pred-structure walls thread as booleans.
void calcQindexRtcCbr(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs, CrRefreshCarrier& cr,
                      const MeStatCarrier& me, std::uint32_t& crSbIndex, std::uint16_t sbsInFrame,
                      bool resizeDynamic, bool singlePass, bool lowDelay, bool randomAccess,
                      ResizePendingCarrier& pending, double& newFramerate) {
    const bool fullUpdate = ppcs.pictureNumber == 0 || ppcs.seqParamChanged;
    const bool softUpdate = ppcs.bitrateChanged || ppcs.frameRateChanged;
    if (fullUpdate || softUpdate) {
        // Use per-PCS runtime rate values (rc_rtc_cbr.c:469-487): the
        // INVERSE leaky-bucket conversion to the classic bucket.
        const std::int64_t bandwidth = ppcs.targetBitRate;
        const std::int64_t starting = rcCfg.startingBufferLevelMs;
        const std::int64_t optimal = rcCfg.optimalBufferLevelMs;
        const std::int64_t maximum = rcCfg.maximumBufferSizeMs;

        rc.startingBufferLevel = starting * bandwidth / 1000;
        rc.optimalBufferLevel = (maximum - optimal) * bandwidth / 1000;
        rc.maximumBufferSize = maximum * bandwidth / 1000;

        if (fullUpdate) {
            // First frame: full initialization
            rcInit(rc, rcCfg, newFramerate, ppcs.frameRateNumerator, ppcs.frameRateDenominator,
                   ppcs.hierarchicalLevels, ppcs.targetBitRate, ppcs.numMbs, ppcs.vbrmaxSection);

            // d) invert current buffer level
            rc.bufferLevel = (maximum - starting) * bandwidth / 1000;

            // flat uses virtual 3-layer RC model for adaptive bit
            // distribution
            rc.rcNumLayers = ppcs.hierarchicalLevels == 0 ? 3 : ppcs.hierarchicalLevels + 1;
            rc.rcMiniGopPos = 0;
            const int numLayers = rc.rcNumLayers;
            for (int k = 0; k < numLayers + 1; k++) {
                rc.targetSizeFactors[k] = 1.0;  // flat at start
            }
            rc.miniQopSize = 1 << (numLayers - 1);
            for (int k = 0; k < rc.miniQopSize + 1; k++) {
                rc.rcfValues[k] = 0.7;
                rc.rcfKalmanP[k] = 1.0;  // high initial uncertainty
                rc.rcfKalmanR[k] = 0.08;
            }
            if (rcCfg.mode == kRcCbr) {
                // just to match existent CBR
                rc.targetSizeFactors[0] = rc.startingBufferLevel / (2.0 * rc.avgFrameBandwidth);
            } else {
                // TODO: VBR
                rc.targetSizeFactors[0] = 10;
            }
        } else {
            // Bitrate change: incremental update preserving learned RC
            // state. Recompute frame bandwidth from runtime rate values.
            double framerate = (double)ppcs.frameRateNumerator / (double)ppcs.frameRateDenominator;
            if (framerate < 0.1) {
                framerate = 30.0;
            }
            rc.avgFrameBandwidth = (int)(bandwidth / framerate);
            rc.maxFrameBandwidth = std::max(rc.avgFrameBandwidth, rc.maxFrameBandwidth);
        }
    }

    // Dynamic resolution resize (parity with the libaom RT path).
    if (resizeDynamic && singlePass && lowDelay) {
        dynamicResizeDecision(rc, rcCfg, ppcs, newFramerate, false, 0, 0, 0, pending);
    }

    const int qindex = calculateQindexRtc(rc, rcCfg, ppcs, cr, me, crSbIndex, sbsInFrame, 64,
                                          32 /*intra_period_length*/, randomAccess);

    if (cr.applyCyclicRefresh) {
        // compute CR qdeltas with final qindex
        const int width = ppcs.frameWidth;
        const int height = ppcs.frameHeight;
        const double rcf = rtcGetRateCorrectionFactor(rc, ppcs, width, height);
        rtcCyclicRefreshComputeCrQdeltas(cr, rc, rcCfg, ppcs.frameType == kKeyFrame, qindex, rcf,
                                         ppcs.encoderBitDepth);
    }

    ppcs.baseQIdx = qindex;
}

// rtc_update_rate_correction_factors (rc_rtc_cbr.c:551-609): verbatim;
// projected_frame_size threads (ppcs->projected_frame_size); the Kalman
// filter on the log-rcf state.
void rtcUpdateRateCorrectionFactors(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs,
                                    const CrRefreshCarrier& cr, std::uint16_t b64TotalCount, int bitDepth) {
    const int width = ppcs.frameWidth;
    const int height = ppcs.frameHeight;
    const int baseQIdx = ppcs.baseQIdx;
    const double rcf = rtcGetRateCorrectionFactor(rc, ppcs, width, height);

    // Do not update the rate factors for arf overlay frames.
    if (ppcs.isOverlay) {
        return;
    }

    if (ppcs.frameType != kKeyFrame) {
        // Do not update the rate factors for repeated pictures
        if (rc.curAvgBaseMeDist == 0) {
            return;
        }
    }

    // Work out how big we would have expected the frame to be at this Q
    // given the current correction factor.
    const int estimatedSize = estimateFrameSize(rc, rcCfg, cr, ppcs.frameType == kKeyFrame, baseQIdx, rcf, width,
                                                height, b64TotalCount, false, bitDepth);

    // Work out a size correction factor.
    double correctionFactor = 1.0 * ppcs.projectedFrameSize / estimatedSize;

    // Clamp correction factor to prevent anything too extreme
    correctionFactor = std::max(correctionFactor, 0.25);
    correctionFactor = std::min(correctionFactor, 4.0);

    // Kalman filter update for RCF.
    constexpr double kQ = 0.04;
    constexpr double kRMin = 0.04;  // floor to prevent over-reaction

    const int k = getRcfIndex(ppcs.frameType == kKeyFrame, rc.rcMiniGopPos, rc.miniQopSize);

    // Innovation in log domain
    const double innovation = std::log(correctionFactor);

    // Adaptive R: track EMA of squared innovations as measurement noise
    rc.rcfKalmanR[k] += 0.05 * (innovation * innovation - rc.rcfKalmanR[k]);
    const double r = std::max(kRMin, rc.rcfKalmanR[k]);

    // Predict step: P increases by process noise
    const double p = rc.rcfKalmanP[k] + kQ;
    // Update step: Kalman gain
    const double gain = p / (p + r);
    // Update state: log(rcf) += K * innovation -> rcf *= exp(K * innovation)
    double newRcf = rcf * std::exp(gain * innovation);
    // Update estimation variance
    rc.rcfKalmanP[k] = (1.0 - gain) * p;

    rtcSetRateCorrectionFactor(rc, ppcs, newRcf, width, height);
}

// rtc_update_buffer_level (rc_rtc_cbr.c:615-626): verbatim (the INVERTED
// leaky bucket - no upper clip, floored at 0).
void rtcUpdateBufferLevel(RcControl& rc, const PcsCarrier& ppcs, int encodedFrameSize) {
    rc.bufferLevel += encodedFrameSize;
    if (ppcs.showableFrame) {
        rc.bufferLevel -= rc.avgFrameBandwidth;
    }
    rc.bufferLevel = std::max((std::int64_t)0, rc.bufferLevel);
}

// svt_av1_rc_recode_decision_rtc_cbr (rc_rtc_cbr.c:630-689): verbatim.
bool recodeDecisionRtcCbr(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs, const CrRefreshCarrier& cr,
                          std::uint16_t b64TotalCount, int bitDepth) {
    // Prevent re-entry: only one recode pass
    if (ppcs.loopCount > 0) {
        return false;
    }

    // Can't reduce frame size if already on worst quality
    if (ppcs.baseQIdx >= rc.worstQuality) {
        return false;
    }

    int projectedFrameSize = (int)roundPowerOfTwo((std::int64_t)ppcs.pcsTotalRate, 9 /*AV1_PROB_COST_SHIFT*/);
    projectedFrameSize += (ppcs.frameType == kKeyFrame) ? 13 : 0;

    std::int64_t maxFrameSize = INT64_MAX;
    const int maxPct = frameIsIntraOnly(ppcs.frameType) ? rcCfg.maxIntraBitratePct : rcCfg.maxInterBitratePct;
    if (rcCfg.mode == kRcCbr && maxPct > 0) {
        maxFrameSize = (std::int64_t)rc.avgFrameBandwidth * maxPct / 100;
    }

    // Project VBV buffer after this frame
    const std::int64_t projectedBuffer = rc.bufferLevel + projectedFrameSize - rc.avgFrameBandwidth;
    if (projectedBuffer <= rc.maximumBufferSize && projectedFrameSize <= maxFrameSize) {
        return false;
    }

    int newQIdx = rc.worstQuality;

    // Frame would overflow the VBV and/or the frame cap. Compute target
    // size that fits.
    const std::int64_t targetSize =
        std::min(maxFrameSize, rc.maximumBufferSize - rc.bufferLevel + rc.avgFrameBandwidth);
    if (targetSize >= kFrameOverheadBits) {
        const int width = ppcs.frameWidth;
        const int height = ppcs.frameHeight;
        double rcf = rtcGetRateCorrectionFactor(rc, ppcs, width, height);

        // Correct RCF based on actual vs estimated size ratio
        const int baseQIdx = ppcs.baseQIdx;
        const int estimatedSize =
            estimateFrameSize(rc, rcCfg, cr, ppcs.frameType == kKeyFrame, baseQIdx, rcf, width, height,
                              b64TotalCount, false, bitDepth);
        const double correction = (double)projectedFrameSize / estimatedSize;
        rcf *= correction;

        const auto evalFrameSize = [&](int q) {
            return estimateFrameSize(rc, rcCfg, cr, ppcs.frameType == kKeyFrame, q, rcf, width, height,
                                     b64TotalCount, false, bitDepth);
        };
        newQIdx = findClosestArg(static_cast<double>(targetSize), rc.bestQuality, rc.worstQuality, evalFrameSize);

        // New QP must be strictly higher than original, otherwise recode
        // is pointless
        newQIdx = std::max(baseQIdx + 4, newQIdx);
        newQIdx = clampQindex(rcCfg, newQIdx);
    }

    // Store new qindex for recode
    ppcs.baseQIdx = newQIdx;

    return true;
}

// svt_av1_rc_postencode_update_rtc_cbr (rc_rtc_cbr.c:691-729): verbatim;
// the resize_mode == RESIZE_DYNAMIC gate threads as resizeDynamic.
void postencodeUpdateRtcCbr(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs,
                            const CrRefreshCarrier& cr, std::uint16_t b64TotalCount, int bitDepth,
                            bool resizeDynamic) {
    // Update rate control heuristics
    ppcs.projectedFrameSize = (int)ppcs.totalNumBits;

    // Post encode loop adjustment of Q prediction.
    rtcUpdateRateCorrectionFactors(rc, rcCfg, ppcs, cr, b64TotalCount, bitDepth);

    rtcUpdateBufferLevel(rc, ppcs, ppcs.projectedFrameSize);

    const int qindex = ppcs.baseQIdx;

    if (ppcs.frameType == kKeyFrame) {
        rc.avgFrameQindex[0] = (int)roundPowerOfTwo(3 * rc.avgFrameQindex[0] + qindex, 2);

        rc.framesSinceKey = 0;
        rc.framesSinceCdfUpdate = 0;
        rc.rcMiniGopPos = 0;
    } else if (!ppcs.isOverlay) {
        rc.avgFrameQindex[1] = (int)roundPowerOfTwo(3 * rc.avgFrameQindex[1] + qindex, 2);
        // Maintain last_q for the dynamic-resize decision; the RTC path
        // otherwise leaves it stale (rc_rtc_cbr.c:713-719).
        if (resizeDynamic) {
            rc.lastQ[1] = qindex;
        }

        const int avgCntZeromv = ppcs.avgCntZeromv;
        if (rc.avgFrameLowMotion == 0) {
            rc.avgFrameLowMotion = avgCntZeromv;
        } else {
            rc.avgFrameLowMotion = (int)roundPowerOfTwo(3 * rc.avgFrameLowMotion + avgCntZeromv, 2);
        }
        rc.rcMiniGopPos = (rc.rcMiniGopPos + 1) % rc.miniQopSize;
    }
}

// ---- RC2 Wave D3 (the vbr updater + the postencode pair + the regulated
// pair): rc_vbr_cbr.c :1349-:1472 / :1495-:1629 / :1720-:1752 ----

// av1_estimate_bits_at_q (rc_vbr_cbr.c:1349-1353): verbatim.
int estimateBitsAtQ(int frameType, int q, int mbs, double correctionFactor, int bitDepth,
                    bool isScreenContent) {
    const int bpm = rcBitsPerMb(frameType, q, correctionFactor, bitDepth, isScreenContent ? 1 : 0);
    return std::max(kFrameOverheadBits,
                    (int)(((std::uint64_t)bpm * (std::uint64_t)mbs) >> kBperMbNormbits));
}

// av1_rc_update_rate_correction_factors (rc_vbr_cbr.c:1355-1472): verbatim.
void rcUpdateRateCorrectionFactors(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs,
                                   const CrRefreshCarrier& cr, std::uint16_t b64TotalCount, int width,
                                   int height, int bitDepth) {
    const int frameType = ppcs.frameType;
    const int baseQIdx = ppcs.baseQIdx;
    int correctionFactor = 100;
    double rateCorrectionFactor = getRateCorrectionFactor(rc, rcCfg, ppcs, width, height);
    double adjustmentLimit = 0.75;
    const int MBs = ((width + 15) / 16) * ((height + 15) / 16);

    // Do not update the rate factors for arf overlay frames.
    if (ppcs.isOverlay) {
        return;
    }
    int projectedSizeBasedOnQ = 0;
    if (cr.applyCyclicRefresh) {
        // Weight for non-base segments.
        const double weightSegment1 = (double)cr.actualNumSeg1Sbs / b64TotalCount;
        const double weightSegment2 = (double)cr.actualNumSeg2Sbs / b64TotalCount;

        // Take segment weighted average for estimated bits.
        projectedSizeBasedOnQ = (int)std::round(
            (1.0 - weightSegment1 - weightSegment2) *
                    estimateBitsAtQ(frameType, baseQIdx, MBs, rateCorrectionFactor, bitDepth,
                                    ppcs.scClass1) +
                weightSegment1 * estimateBitsAtQ(frameType, baseQIdx + cr.qindexDelta[1], MBs,
                                                 rateCorrectionFactor, bitDepth, ppcs.scClass1) +
                weightSegment2 * estimateBitsAtQ(frameType, baseQIdx + cr.qindexDelta[2], MBs,
                                                 rateCorrectionFactor, bitDepth, ppcs.scClass1));
    } else {
        projectedSizeBasedOnQ =
            estimateBitsAtQ(frameType, baseQIdx, MBs, rateCorrectionFactor, bitDepth, ppcs.scClass1);
    }
    // Work out a size correction factor.
    if (projectedSizeBasedOnQ > kFrameOverheadBits) {
        correctionFactor = (int)(100 * (std::int64_t)ppcs.projectedFrameSize / projectedSizeBasedOnQ);
    }
    // Clamp correction factor to prevent anything too extreme
    correctionFactor = std::max(correctionFactor, 25);
    rc.q2Frame = rc.q1Frame;
    rc.q1Frame = baseQIdx;
    rc.rc2Frame = rc.rc1Frame;
    if (correctionFactor > 110) {
        rc.rc1Frame = -1;
    } else if (correctionFactor < 90) {
        rc.rc1Frame = 1;
    } else {
        rc.rc1Frame = 0;
    }
    // Decide how heavily to dampen the adjustment
    if (rcCfg.mode == kRcCbr) {
        if (correctionFactor > 0) {
            if (ppcs.scClass1) {
                adjustmentLimit =
                    0.25 + 0.5 * std::min(0.5, std::fabs(std::log10(0.01 * correctionFactor)));
            } else {
                adjustmentLimit =
                    0.25 + 0.75 * std::min(0.5, std::fabs(std::log10(0.01 * correctionFactor)));
            }
        } else {
            adjustmentLimit = 0.75;
        }
    } else {
        if (correctionFactor > 0) {
            adjustmentLimit = 0.25 + 0.5 * std::min(1.0, std::fabs(std::log10(0.01 * correctionFactor)));
        } else {
            adjustmentLimit = 0.75;
        }
    }
    // Adjustment to delta Q and number of blocks updated in cyclic refresh
    // based on over or under shoot of target in current frame.
    if (cr.applyCyclicRefresh) {
        if (correctionFactor > 125) {
            rc.percentRefreshAdjustment = std::max(rc.percentRefreshAdjustment - 1, -5);
            rc.rateRatioQdeltaAdjustment = std::max(rc.rateRatioQdeltaAdjustment - 0.05, -0.0);
        } else if (correctionFactor < 50) {
            rc.percentRefreshAdjustment = std::min(rc.percentRefreshAdjustment + 1, 5);
            rc.rateRatioQdeltaAdjustment = std::min(rc.rateRatioQdeltaAdjustment + 0.05, 0.25);
        }
    }
    if (correctionFactor > 101) {
        // We are not already at the worst allowable quality
        correctionFactor = (int)(100 + ((correctionFactor - 100) * adjustmentLimit));
        rateCorrectionFactor = rateCorrectionFactor * correctionFactor / 100;
        // Keep rate_correction_factor within limits
        if (rateCorrectionFactor > kMaxBpbFactor) {
            rateCorrectionFactor = kMaxBpbFactor;
        }
    } else if (correctionFactor < 99) {
        // We are not already at the best allowable quality
        double tmpCorrFac = 100 / (double)correctionFactor;
        tmpCorrFac = (1.0 + ((tmpCorrFac - 1.0) * adjustmentLimit));
        tmpCorrFac = 1.0 / tmpCorrFac;
        correctionFactor = (int)(100 * tmpCorrFac);
        rateCorrectionFactor = rateCorrectionFactor * correctionFactor / 100;

        // Keep rate_correction_factor within limits
        if (rateCorrectionFactor < kMinBpbFactor) {
            rateCorrectionFactor = kMinBpbFactor;
        }
    }

    setRateCorrectionFactor(rc, rcCfg, ppcs, rateCorrectionFactor, width, height);
}

// svt_av1_rc_postencode_update_gop_const (rc_vbr_cbr.c:1495-1561): verbatim;
// the rolling/total bits go to the interval-param carrier; no low-motion EMA.
void postencodeUpdateGopConst(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs,
                              const CrRefreshCarrier& cr, RcIntervalParamCarrier& rcParam,
                              std::uint16_t b64TotalCount, int bitDepth, int& frameUpdated) {
    const int width = ppcs.frameWidth;
    const int height = ppcs.frameHeight;
    const bool isIntrnlArf = ppcs.updateType == kSvtAv1IntnlArfUpdate;

    // Update rate control heuristics
    ppcs.projectedFrameSize = (int)ppcs.totalNumBits;
    // Post encode loop adjustment of Q prediction.
    rcUpdateRateCorrectionFactors(rc, rcCfg, ppcs, cr, b64TotalCount, width, height, bitDepth);

    // Keep a record of last Q and ambient average Q.
    const int qindex = ppcs.baseQIdx;
    if (ppcs.frameType == kKeyFrame) {
        rc.avgFrameQindex[0] = (int)roundPowerOfTwo(3 * rc.avgFrameQindex[0] + qindex, 2);
        rc.lastQ[0] = (int)convertQindexToQ(qindex, bitDepth);
        frameUpdated = 0;
    } else {
        frameUpdated = frameUpdated + 1;
        if (!ppcs.isOverlay &&
            !(ppcs.updateType == kSvtAv1GfUpdate || ppcs.updateType == kSvtAv1ArfUpdate || isIntrnlArf)) {
            rc.avgFrameQindex[1] = (int)roundPowerOfTwo(3 * rc.avgFrameQindex[1] + qindex, 2);
            rc.lastQ[1] = (int)convertQindexToQ(qindex, bitDepth);
        }
    }

    // Keep record of last boosted (KF/GF/ARF) Q value.
    if (qindex < rc.lastBoostedQindex || ppcs.frameType == kKeyFrame ||
        (!rc.constrainedGfGroup &&
         (ppcs.updateType == kSvtAv1ArfUpdate || isIntrnlArf ||
          (ppcs.updateType == kSvtAv1GfUpdate && !ppcs.isOverlay)))) {
        rc.lastBoostedQindex = qindex;
    }
    updateBufferLevel(rc, ppcs, ppcs.projectedFrameSize);
    rc.prevAvgFrameBandwidth = rc.avgFrameBandwidth;

    // Rolling monitors of whether we are over or underspending used to help
    // regulate min and Max Q in two pass.
    if (ppcs.frameType != kKeyFrame) {
        rcParam.rollingTargetBits =
            (int)roundPowerOfTwo((std::int64_t)rcParam.rollingTargetBits * 3 + ppcs.thisFrameTarget, 2);
        rcParam.rollingActualBits = (int)roundPowerOfTwo(
            (std::int64_t)rcParam.rollingActualBits * 3 + ppcs.projectedFrameSize, 2);
    }

    // Actual bits spent
    rcParam.totalActualBits += ppcs.projectedFrameSize;
    rcParam.totalTargetBits += ppcs.showableFrame ? rc.avgFrameBandwidth : 0;

    if (ppcs.frameType == kKeyFrame) {
        rc.framesSinceKey = 0;
        rc.framesSinceCdfUpdate = 0;
    }
}

// svt_av1_rc_postencode_update (rc_vbr_cbr.c:1563-1629): verbatim; the
// low-motion EMA sits OUTSIDE the leaf exclusion (:1618-1620).
void postencodeUpdate(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs, const CrRefreshCarrier& cr,
                      std::uint16_t b64TotalCount, int bitDepth, int& frameUpdated) {
    const int width = ppcs.frameWidth;
    const int height = ppcs.frameHeight;
    const bool isIntrnlArf = ppcs.updateType == kSvtAv1IntnlArfUpdate;

    // Update rate control heuristics
    ppcs.projectedFrameSize = (int)ppcs.totalNumBits;
    // Post encode loop adjustment of Q prediction.
    rcUpdateRateCorrectionFactors(rc, rcCfg, ppcs, cr, b64TotalCount, width, height, bitDepth);

    // Keep a record of last Q and ambient average Q.
    const int qindex = ppcs.baseQIdx;
    if (ppcs.frameType == kKeyFrame) {
        rc.avgFrameQindex[0] = (int)roundPowerOfTwo(3 * rc.avgFrameQindex[0] + qindex, 2);
        rc.lastQ[0] = (int)convertQindexToQ(qindex, bitDepth);
        frameUpdated = 0;
    } else {
        frameUpdated = frameUpdated + 1;
        if (!ppcs.isOverlay &&
            !(ppcs.updateType == kSvtAv1GfUpdate || ppcs.updateType == kSvtAv1ArfUpdate || isIntrnlArf)) {
            rc.avgFrameQindex[1] = (int)roundPowerOfTwo(3 * rc.avgFrameQindex[1] + qindex, 2);
            rc.lastQ[1] = (int)convertQindexToQ(qindex, bitDepth);
        }
    }

    // Keep record of last boosted (KF/GF/ARF) Q value.
    if (qindex < rc.lastBoostedQindex || ppcs.frameType == kKeyFrame ||
        (!rc.constrainedGfGroup &&
         (ppcs.updateType == kSvtAv1ArfUpdate || isIntrnlArf ||
          (ppcs.updateType == kSvtAv1GfUpdate && !ppcs.isOverlay)))) {
        rc.lastBoostedQindex = qindex;
    }
    updateBufferLevel(rc, ppcs, ppcs.projectedFrameSize);
    rc.prevAvgFrameBandwidth = rc.avgFrameBandwidth;

    // Rolling monitors of whether we are over or underspending used to help
    // regulate min and Max Q in two pass.
    if (ppcs.frameType != kKeyFrame) {
        rc.rollingTargetBits =
            (int)roundPowerOfTwo((std::int64_t)rc.rollingTargetBits * 3 + ppcs.thisFrameTarget, 2);
        rc.rollingActualBits = (int)roundPowerOfTwo(
            (std::int64_t)rc.rollingActualBits * 3 + ppcs.projectedFrameSize, 2);
    }
    rc.avgFrameLowMotion = (rc.avgFrameLowMotion == 0)
        ? ppcs.avgCntZeromv
        : (int)((3 * rc.avgFrameLowMotion + ppcs.avgCntZeromv) / 4);
    // Actual bits spent
    rc.totalActualBits += ppcs.projectedFrameSize;
    rc.totalTargetBits += ppcs.showableFrame ? rc.avgFrameBandwidth : 0;

    if (ppcs.frameType == kKeyFrame) {
        rc.framesSinceKey = 0;
        rc.framesSinceCdfUpdate = 0;
    }
}

// get_regulated_q_overshoot (rc_vbr_cbr.c:1720-1735): verbatim.
int getRegulatedQOvershoot(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs,
                           const CrRefreshCarrier& cr, std::uint16_t b64TotalCount, int qLow, int qHigh,
                           int topIndex, int bottomIndex, int bitDepth) {
    const int width = ppcs.frameWidth;
    const int height = ppcs.frameHeight;

    rcUpdateRateCorrectionFactors(rc, rcCfg, ppcs, cr, b64TotalCount, width, height, bitDepth);

    int qRegulated = av1RcRegulateQ(rc, rcCfg, ppcs, bottomIndex, std::max(qHigh, topIndex), width, height);
    int retries = 0;
    while (qRegulated < qLow && retries < 10) {
        rcUpdateRateCorrectionFactors(rc, rcCfg, ppcs, cr, b64TotalCount, width, height, bitDepth);
        qRegulated = av1RcRegulateQ(rc, rcCfg, ppcs, bottomIndex, std::max(qHigh, topIndex), width, height);
        retries++;
    }
    return qRegulated;
}

// get_regulated_q_undershoot (rc_vbr_cbr.c:1738-1752): verbatim.
int getRegulatedQUndershoot(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs,
                            const CrRefreshCarrier& cr, std::uint16_t b64TotalCount, int qHigh, int topIndex,
                            int bottomIndex, int bitDepth) {
    const int width = ppcs.frameWidth;
    const int height = ppcs.frameHeight;

    rcUpdateRateCorrectionFactors(rc, rcCfg, ppcs, cr, b64TotalCount, width, height, bitDepth);
    int qRegulated = av1RcRegulateQ(rc, rcCfg, ppcs, bottomIndex, topIndex, width, height);

    int retries = 0;
    while (qRegulated > qHigh && retries < 10) {
        rcUpdateRateCorrectionFactors(rc, rcCfg, ppcs, cr, b64TotalCount, width, height, bitDepth);
        qRegulated = av1RcRegulateQ(rc, rcCfg, ppcs, bottomIndex, topIndex, width, height);
        retries++;
    }
    return qRegulated;
}

// ---- RC2 Wave D4: the vbr cyclic-refresh trio (rc_vbr_cbr.c :895-:998) ----

// cyclic_refresh_init (rc_vbr_cbr.c:895-972): verbatim.
void cyclicRefreshInit(RcControl& rc, CrRefreshCarrier& cr, const PcsCarrier& ppcs, int superBlockSize,
                       std::uint16_t sbTotalCount, std::uint32_t& crSbEnd, int hierarchicalLevels) {
    // Cases to reset the cyclic refresh adjustment parameters.
    if (ppcs.sliceType == kIslice) {
        rc.percentRefreshAdjustment = 5;
        rc.rateRatioQdeltaAdjustment = 0.25;
    }

    cr.percentRefresh = 20 + rc.percentRefreshAdjustment;

    if (ppcs.scClass1) {
        cr.percentRefresh += 5;
    }

    cr.applyCyclicRefresh = (ppcs.sliceType != kIslice && ppcs.temporalLayerIndex == 0) ? 1 : 0;

    if (superBlockSize != 64) {
        cr.applyCyclicRefresh = 0;
    }

    const int qpThresh = std::max(16, rc.bestQuality + 4);
    const int qpMaxThresh = 118 * kMaxQ >> 7;

    if (rc.avgFrameQindex[1] > qpMaxThresh) {
        cr.applyCyclicRefresh = 0;
    }

    if (rc.avgFrameQindex[1] < qpThresh) {
        cr.applyCyclicRefresh = 0;
    }

    if (rc.avgFrameLowMotion && rc.avgFrameLowMotion < 50) {
        cr.applyCyclicRefresh = 0;
    }

    if (cr.percentRefresh <= 0) {
        cr.applyCyclicRefresh = 0;
    }

    if (!cr.applyCyclicRefresh) {
        return;
    }

    cr.sbStart = crSbEnd;
    cr.sbEnd = std::min(cr.sbStart + (std::uint32_t)sbTotalCount * cr.percentRefresh / 100,
                        (std::uint32_t)sbTotalCount);
    crSbEnd = cr.sbEnd >= sbTotalCount ? 0 : cr.sbEnd;

    // Use larger delta - qp(increase rate_ratio_qdelta) for first few(~4)
    // periods of the refresh cycle, after a key frame.
    cr.maxQdeltaPerc = 60;

    if (!ppcs.scClass1) {
        cr.rateRatioQdelta =
            (rc.framesSinceKey < 4 * (1 << hierarchicalLevels) * 100 / cr.percentRefresh) ? 1.50 : 1.15;
        cr.rateRatioQdelta += rc.rateRatioQdeltaAdjustment;
        cr.rateBoostFac = 15;
    } else {
        const double distanceFromScFactor =
            std::min(0.75, (double)(rc.framesSinceKey / 10) * 0.1);
        cr.rateRatioQdelta = 2.25 + rc.rateRatioQdeltaAdjustment - distanceFromScFactor;
        if (rc.rc1Frame < 0 || rc.rc2Frame < 0) {
            cr.rateRatioQdelta -= 0.25;
        }
        cr.rateBoostFac = 10;
    }
}

// compute_cr_deltaq (rc_vbr_cbr.c:978-985): verbatim.
int computeCrDeltaq(const RcControl& rc, const CrRefreshCarrier& cr, int q, double rateRatioQdelta,
                    int bitDepth, bool scClass1) {
    const int deltaq =
        computeQdeltaByRate(rc.bestQuality, rc.worstQuality, kInterFrame, q, rateRatioQdelta, bitDepth,
                            scClass1);
    return std::max(deltaq, -cr.maxQdeltaPerc * q / 100);
}

// cyclic_refresh_compute_cr_qdeltas (rc_vbr_cbr.c:989-998): verbatim.
void cyclicRefreshComputeCrQdeltas(const RcControl& rc, CrRefreshCarrier& cr, int baseQIdx, int bitDepth,
                                   bool scClass1) {
    const double rateRatioQdelta = cr.rateRatioQdelta;
    const double rateRatioQdeltaSeg2 = std::min(kCrMaxRateTargetRatio, cr.rateRatioQdeltaSeg2);

    cr.qindexDelta[0] = 0;
    cr.qindexDelta[1] = computeCrDeltaq(rc, cr, baseQIdx, rateRatioQdelta, bitDepth, scClass1);
    cr.qindexDelta[2] = computeCrDeltaq(rc, cr, baseQIdx, rateRatioQdeltaSeg2, bitDepth, scClass1);
}

// ---- RC2 Wave D2: the recode-loop driver (rc_vbr_cbr.c :1794-:1926) ----
// recode_loop_update_q (:1794-1926): verbatim; the capped_crf_reencode
// call (rc_crf_cqp.c) is the named park - q passes through unchanged.
void recodeLoopUpdateQ(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs, const CrRefreshCarrier& cr,
                       std::uint16_t b64TotalCount, int recodeLoop, std::int64_t maxBitRate,
                       int recodeTolerance, int bitDepth, int topIndex, int bottomIndex, int loopCount,
                       bool& loop, int& q, int& qLow, int& qHigh, int& undershootSeen, int& overshootSeen,
                       int& lowCrSeen) {
    const bool doDummyPack =
        (recodeLoop >= kAllowRecodeKfMaxBw && !(rcCfg.mode == kRcQ && maxBitRate == 0)) || rcCfg.minCr > 0;
    if (doDummyPack) {
        ppcs.projectedFrameSize =
            (int)((ppcs.pcsTotalRate + (1 << (kAv1ProbCostShift - 1))) >> kAv1ProbCostShift) +
            (ppcs.frameType == kKeyFrame ? 13 : 0);
    } else {
        ppcs.projectedFrameSize = 0;
    }
    loop = false;
    if (recodeLoop == kAllowRecodeKfMaxBw && ppcs.frameType != kKeyFrame) {
        // skip re-encode for inter frame when setting -recode-loop 1
        return;
    }

    if (rcCfg.minCr > 0) {
        const double compressionRatio =
            getCompressionRatio(ppcs.superresUpscaledWidth, ppcs.frameHeight, ppcs.seqProfile,
                                (std::uint64_t)(ppcs.projectedFrameSize >> 3));
        const double targetCr = rcCfg.minCr / 100.0;
        if (compressionRatio < targetCr) {
            lowCrSeen = 1;
            if (q < rc.worstQuality) {
                const double crRatio = targetCr / compressionRatio;
                const int projectedQ = std::max(q + 1, (int)(q * crRatio * crRatio));
                q = std::min(std::min(projectedQ, q + 32), rc.worstQuality);
                qLow = std::max(q, qLow);
                qHigh = std::max(q, qHigh);
                loop = true;
            }
        }
        if (lowCrSeen) {
            return;
        }
    }
    // Used for capped CRF. Update the active worse quality.
    if (rcCfg.mode == kRcQ && maxBitRate) {
        if (ppcs.temporalLayerIndex > 0) {
            return;
        } else {
            // capped_crf_reencode(ppcs, &q); - PARKED (rc_crf_cqp.c).
        }
    }
    const int lastQ = q;
    int frameOverShootLimit = 0;
    int frameUnderShootLimit = 0;
    computeFrameSizeBounds(rcCfg.mode, recodeTolerance, ppcs.maxFrameSize, ppcs.loopCount,
                           ppcs.thisFrameTarget, rc.maxFrameBandwidth, frameUnderShootLimit,
                           frameOverShootLimit);
    if (frameOverShootLimit == 0) {
        frameOverShootLimit = 1;
    }

    if (recodeLoopTest(rc, ppcs, recodeLoop, frameOverShootLimit, frameUnderShootLimit, q,
                       std::max(qHigh, topIndex), bottomIndex)) {
        const int width = ppcs.frameWidth;
        const int height = ppcs.frameHeight;
        // Is the projected frame size out of range and are we allowed
        // to attempt to recode.

        // Frame size out of permitted range:
        // Update correction factor & compute new Q to try...
        // Frame is too large
        if (ppcs.projectedFrameSize > ppcs.thisFrameTarget) {
            // Special case if the projected size is > the max allowed.
            if (q == qHigh && ppcs.projectedFrameSize >= rc.maxFrameBandwidth) {
                const double qValHighCurrent = convertQindexToQ(qHigh, bitDepth);
                const double qValHighNew =
                    qValHighCurrent * ((double)ppcs.projectedFrameSize / rc.maxFrameBandwidth);
                qHigh = av1FindQindex(qValHighNew, bitDepth, rc.bestQuality, rc.worstQuality);
            }
            // Raise Qlow as to at least the current value
            qLow = std::min(q + 1, qHigh);

            if (undershootSeen || loopCount > 2 || (loopCount == 2 && !frameIsIntraOnly(ppcs.frameType))) {
                rcUpdateRateCorrectionFactors(rc, rcCfg, ppcs, cr, b64TotalCount, width, height, bitDepth);

                q = (qHigh + qLow + 1) / 2;
            } else if (loopCount == 2 && frameIsIntraOnly(ppcs.frameType)) {
                const int qMid = (qHigh + qLow + 1) / 2;
                const int qRegulated =
                    getRegulatedQOvershoot(rc, rcCfg, ppcs, cr, b64TotalCount, qLow, qHigh, topIndex,
                                           bottomIndex, bitDepth);
                // Get 'q' in-between 'q_mid' and 'q_regulated' for a smooth
                // transition between loop_count < 2 and loop_count > 2.
                q = (qMid + qRegulated + 1) / 2;
            } else {
                q = getRegulatedQOvershoot(rc, rcCfg, ppcs, cr, b64TotalCount, qLow, qHigh, topIndex,
                                           bottomIndex, bitDepth);
            }

            overshootSeen = 1;
        } else {
            // Frame is too small
            qHigh = std::max(q - 1, qLow);

            if (overshootSeen || loopCount > 2 || (loopCount == 2 && !frameIsIntraOnly(ppcs.frameType))) {
                rcUpdateRateCorrectionFactors(rc, rcCfg, ppcs, cr, b64TotalCount, width, height, bitDepth);
                q = (qHigh + qLow) / 2;
            } else if (loopCount == 2 && frameIsIntraOnly(ppcs.frameType)) {
                const int qMid = (qHigh + qLow) / 2;
                const int qRegulated =
                    getRegulatedQUndershoot(rc, rcCfg, ppcs, cr, b64TotalCount, qHigh, topIndex,
                                            bottomIndex, bitDepth);
                // Get 'q' in-between 'q_mid' and 'q_regulated' for a smooth
                // transition between loop_count < 2 and loop_count > 2.
                q = (qMid + qRegulated) / 2;
            } else {
                q = getRegulatedQUndershoot(rc, rcCfg, ppcs, cr, b64TotalCount, qHigh, topIndex,
                                            bottomIndex, bitDepth);
            }

            undershootSeen = 1;
        }

        // Clamp Q to upper and lower limits:
        q = clip3(q, qLow, qHigh);
    }

    q = clampQindex(rcCfg, q);
    loop = (q != lastQ);
    // Used for capped CRF. Update the active worse quality based on the final assigned qindex.
    if (rcCfg.mode == kRcQ && maxBitRate && loop == false && ppcs.loopCount > 0) {
        if (ppcs.sliceType == kIslice) {
            rc.activeWorstQuality = getKfQTpl(rc, q, bitDepth);
        } else {
            rc.activeWorstQuality = getGfuQTpl(rc, q, bitDepth);
        }

        rc.activeWorstQuality = clampQindex(rcCfg, rc.activeWorstQuality);
    }
}

// ---- RC2 Wave D (recode helpers): rc_vbr_cbr.c :1642-:1789 ----

// av1_find_qindex (rc_vbr_cbr.c:1773-1789): verbatim.
int av1FindQindex(double desiredQ, int bitDepth, int bestQindex, int worstQindex) {
    int low = bestQindex;
    int high = worstQindex;
    while (low < high) {
        const int mid = (low + high) >> 1;
        const double midQ = convertQindexToQ(mid, bitDepth);
        if (midQ < desiredQ) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low;
}

// av1_get_compression_ratio (rc_vbr_cbr.c:1690-1699): verbatim.
double getCompressionRatio(int superresUpscaledWidth, int height, int profile,
                           std::uint64_t encodedFrameSize) {
    const int lumaPicSize = superresUpscaledWidth * height;
    const int picSizeProfileFactor =
        profile == kMainProfile ? 15 : (profile == kHighProfile ? 30 : 36);
    encodedFrameSize = (encodedFrameSize > 129 ? encodedFrameSize - 128 : 1);
    const std::uint64_t uncompressedFrameSize =
        (std::uint64_t)((lumaPicSize * picSizeProfileFactor) >> 3);
    return uncompressedFrameSize / (double)encodedFrameSize;
}

// av1_rc_compute_frame_size_bounds (rc_vbr_cbr.c:1701-1717): verbatim.
void computeFrameSizeBounds(int mode, int recodeTolerance, int maxFrameSize, int loopCount,
                            int frameTarget, int rcMaxFrameBandwidth, int& frameUnderShootLimit,
                            int& frameOverShootLimit) {
    if (mode == kRcQ) {
        const int tolerance =
            (int)std::max<std::int64_t>(100, (std::int64_t)recodeTolerance * maxFrameSize / 100);
        frameUnderShootLimit = loopCount ? std::max(maxFrameSize - tolerance, 0) : 0;
        frameOverShootLimit = (int)std::min<std::int64_t>((std::int64_t)maxFrameSize + tolerance,
                                                         (std::int64_t)INT_MAX);
    } else {
        const int tolerance =
            (int)std::max<std::int64_t>(100, (std::int64_t)recodeTolerance * frameTarget / 100);
        frameUnderShootLimit = std::max(frameTarget - tolerance, 0);
        frameOverShootLimit = std::min(frameTarget + tolerance, rcMaxFrameBandwidth);
    }
}

// recode_loop_test (rc_vbr_cbr.c:1756-1771): verbatim.
bool recodeLoopTest(const RcControl& rc, const PcsCarrier& pcs, int recodeLoop, int highLimit,
                    int lowLimit, int q, int maxq, int minq) {
    const bool frameIsKfgfarf = frameIsKfGfArf(pcs);
    bool forceRecode = false;
    if ((std::int64_t)pcs.projectedFrameSize >= rc.maxFrameBandwidth || recodeLoop == kAllowRecode ||
        (frameIsKfgfarf && recodeLoop >= kAllowRecodeKfMaxBw)) {
        if ((pcs.projectedFrameSize > highLimit && q < maxq) ||
            (pcs.projectedFrameSize < lowLimit && q > minq)) {
            forceRecode = true;
        }
    }
    return forceRecode;
}

// get_kf_q_tpl (rc_vbr_cbr.c:1642-1661): verbatim; the search's prev_dif is
// the INITIAL difference and is not refreshed inside the loop.
int getKfQTpl(const RcControl& rc, int targetActiveQuality, int bitDepth) {
    const std::int32_t* kfLowMotionMinqCqp = minqTable("kf_low_motion_minq_cqp", bitDepth);
    const std::int32_t* kfHighMotionMinq = minqTable("kf_high_motion_minq", bitDepth);
    int q = rc.activeWorstQuality;
    int activeQuality =
        getActiveQuality(q, rc.kfBoost, kBoostKfLow, kBoostKfHigh, kfLowMotionMinqCqp, kfHighMotionMinq);
    const int prevDif = std::abs(targetActiveQuality - activeQuality);
    while (std::abs(targetActiveQuality - activeQuality) > 4 &&
           std::abs(targetActiveQuality - activeQuality) <= prevDif) {
        if (activeQuality > targetActiveQuality) {
            q--;
        } else {
            q++;
        }
        activeQuality = getActiveQuality(q, rc.kfBoost, kBoostKfLow, kBoostKfHigh, kfLowMotionMinqCqp,
                                         kfHighMotionMinq);
    }
    return q;
}

// get_gfu_q_tpl (rc_vbr_cbr.c:1667-1688): verbatim; the same prev_dif quirk.
int getGfuQTpl(const RcControl& rc, int targetActiveQuality, int bitDepth) {
    const std::int32_t* arfgfLowMotionMinq = minqTable("arfgf_low_motion_minq", bitDepth);
    const std::int32_t* arfgfHighMotionMinq = minqTable("arfgf_high_motion_minq", bitDepth);
    int q = rc.activeWorstQuality;
    int activeQuality = getActiveQuality(q, rc.gfuBoost, kBoostGfLowTplLa, kBoostGfHighTplLa,
                                         arfgfLowMotionMinq, arfgfHighMotionMinq);
    const int prevDif = std::abs(targetActiveQuality - activeQuality);
    while (std::abs(targetActiveQuality - activeQuality) > 4 &&
           std::abs(targetActiveQuality - activeQuality) <= prevDif) {
        if (activeQuality > targetActiveQuality) {
            q--;
        } else {
            q++;
        }
        activeQuality = getActiveQuality(q, rc.gfuBoost, kBoostGfLowTplLa, kBoostGfHighTplLa,
                                         arfgfLowMotionMinq, arfgfHighMotionMinq);
    }
    return q;
}

// ---- A9D: rc_process.c (the interval-param resets and the rc counts) ----

// rc_param_reset (rc_process.c:555-566): verbatim.
void rcParamReset(RcIntervalParamCarrier& rcParam) {
    rcParam.size = -1;
    rcParam.processedFrameNumber = 0;
    rcParam.vbrBitsOffTarget = 0;
    rcParam.vbrBitsOffTargetFast = 0;
    rcParam.rateErrorEstimate = 0;
    rcParam.totalActualBits = 0;
    rcParam.totalTargetBits = 0;
    rcParam.extendMinq = 0;
    rcParam.extendMaxq = 0;
    rcParam.extendMinqFast = 0;
}

// svt_aom_update_rc_counts (rc_process.c:568-590): verbatim; the scs/enc_ctx
// walls thread (the rc carrier + the ppcs frm_hdr gates).
void updateRcCounts(RcControl& rc, const PcsCarrier& ppcs) {
    if (ppcs.showableFrame) {
        // If this is a show_existing_frame with a source other than altref,
        // or if it is not a displayed forward keyframe, the keyframe update
        // counters were incremented when it was originally encoded.
        rc.framesSinceKey++;
        rc.framesToKey--;
        // Reset whenever the CDF is updated for the current frame,
        // covering keyframes, warmup, scene changes, and periodic updates.
        if (!ppcs.disableCdfUpdate) {
            rc.framesSinceCdfUpdate = 0;
        } else {
            rc.framesSinceCdfUpdate++;
        }
    }
}

// reset_rc_param (rc_process.c:592-605): verbatim.
void resetRcParam(PcsCarrier& ppcs) {
    ppcs.loopCount = 0;
    ppcs.overshootSeen = 0;
    ppcs.undershootSeen = 0;
}

} // namespace l15_rc
