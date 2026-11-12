#include "rc_arith.h"

#include <algorithm>
#include <climits>
#include <cstdint>
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

} // namespace l15_rc
