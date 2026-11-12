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

} // namespace l15_rc
