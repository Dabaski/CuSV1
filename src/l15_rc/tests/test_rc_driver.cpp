#include <doctest.h>
#include <cmath>
#include <cstdint>
#include <vector>

#include "rc_arith.h"

// RC2c Wave C2: the driver family. Reference spans:
//   rc_process.c:498-560 (svt_av1_rc_init), pass2_strategy.c:877-904
//   (av1_rc_update_framerate + svt_av1_new_framerate),
//   rc_rtc_cbr.c:459-549 (svt_av1_rc_calc_qindex_rtc_cbr),
//   :551-609 (rtc_update_rate_correction_factors - the Kalman filter),
//   :615-626 (rtc_update_buffer_level), :630-689 (svt_av1_rc_recode_decision_
//   rtc_cbr), :691-729 (svt_av1_rc_postencode_update_rtc_cbr).
//
// FLOAT DISCIPLINE: all outputs here are INTEGER and pinned exactly; the
// Kalman-filter state variables (rcfKalmanP/R, newRcf) are doubles compared
// ABSOLUTE 1e-9 written out with fabs (doctest Approx is relative). The
// framerate helper's avg_frame_bandwidth = (int)(target_bit_rate /
// new_framerate) is an int truncation - pinned exactly.
//
// EXPECTATION DERIVATION: oracle-derived. Every pin was computed by an
// independent python oracle that replicates the C loops from the reference
// spans. Hand-traces were wrong three times during staging (the rcf loop
// bound MAX_TEMPORAL_LAYERS + 1 = 7, the framerate truncation, the
// baseline_gf_interval shift) - the oracle is the authority.

namespace {

// The C2 fixture rc/cfg pair: the driver family reads worst/best quality,
// buffer levels, target_bit_rate and framerate off it.
l15_rc::RcControl drvRc() {
    l15_rc::RcControl rc{};
    rc.avgFrameBandwidth = 10000;
    rc.maxFrameBandwidth = 50000;
    rc.startingBufferLevel = 2000000;
    rc.bufferLevel = 12000000;  // the recode/postencode comments' assumed pre-level
    rc.optimalBufferLevel = 10000000;
    rc.maximumBufferSize = 20000000;
    rc.worstQuality = 255;
    rc.bestQuality = 0;
    rc.avgFrameQindex[0] = 100;
    rc.avgFrameQindex[1] = 150;
    rc.lastQ[0] = 100;
    rc.lastQ[1] = 150;
    rc.q1Frame = 220;
    rc.prevAvgFrameBandwidth = 10000;
    rc.framesSinceKey = 8;
    rc.miniQopSize = 4;
    rc.rcNumLayers = 3;
    for (int i = 0; i < 7; ++i) rc.rateCorrectionFactors[i] = 0.7;
    for (int i = 0; i < 33; ++i) {
        rc.rcfValues[i] = 0.7;
        rc.rcfKalmanP[i] = 1.0;
        rc.rcfKalmanR[i] = 0.08;
    }
    for (int i = 0; i < 7; ++i) rc.targetSizeFactors[i] = 1.0;
    return rc;
}

l15_rc::RcConfig drvCfg() {
    l15_rc::RcConfig cfg{};
    cfg.mode = l15_rc::kRcCbr;
    cfg.worstAllowedQ = 255;
    cfg.bestAllowedQ = 0;
    cfg.startingBufferLevelMs = 3000;
    cfg.optimalBufferLevelMs = 6000;
    cfg.maximumBufferSizeMs = 10000;
    cfg.maxInterBitratePct = 0;
    cfg.maxIntraBitratePct = 0;
    cfg.minQpAllowed = 0;   // clamp_qindex's quantizer_to_qindex[min] = 0
    cfg.maxQpAllowed = 63;  // clamp_qindex's quantizer_to_qindex[63] = 255
    cfg.underShootPct = 50;
    cfg.overShootPct = 40;
    return cfg;
}

l15_rc::PcsCarrier drvPcs() {
    l15_rc::PcsCarrier pcs{};
    pcs.frameType = l15_rc::kInterFrame;
    pcs.showableFrame = true;
    pcs.pictureNumber = 10;
    pcs.updateType = l15_rc::kSvtAv1ArfUpdate;
    pcs.temporalLayerIndex = 1;
    pcs.hierarchicalLevels = 2;
    pcs.frameWidth = 1920;
    pcs.frameHeight = 1080;
    pcs.encoderBitDepth = 8;
    pcs.sliceType = l15_rc::kBslice;
    pcs.ref0SliceType = l15_rc::kBslice;
    pcs.ref0TmpLayer = 1;
    pcs.ref0BaseQIdx = 120;
    pcs.ref1BaseQIdx = 120;
    pcs.refList1CountTry = 1;
    pcs.targetBitRate = 1000000;
    pcs.frameRateNumerator = 30;
    pcs.frameRateDenominator = 1;
    pcs.seqParamChanged = false;
    pcs.bitrateChanged = false;
    pcs.frameRateChanged = false;
    pcs.projectedFrameSize = 3000;
    pcs.pcsTotalRate = 100000;
    pcs.loopCount = 0;
    pcs.totalNumBits = 3000;
    pcs.avgCntZeromv = 50;
    pcs.numMbs = 64;
    pcs.vbrmaxSection = 190;
    return pcs;
}

l15_rc::MeStatCarrier drvMe() {
    l15_rc::MeStatCarrier me{};
    static const std::uint32_t d8[1] = {0};
    static const std::uint32_t d64[1] = {0};
    static const std::int16_t mv[1] = {0};
    me.me8x8Distortion = d8;
    me.me64x64Distortion = d64;
    me.me8x8CostVariance = nullptr;
    me.mvX = mv;
    me.mvY = mv;
    me.normMeDist = 0;
    me.b64TotalCount = 1;
    return me;
}

}  // namespace

TEST_CASE("rcInit CBR mode: quality, buffers, framerate and rcf init") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    double newFramerate = 0.0;
    // CBR mode: avg/last qindex = worstAllowedQ.
    l15_rc::rcInit(rc, cfg, newFramerate, 30, 1, 2, 1000000, 64, 190);
    CHECK(rc.avgFrameQindex[0] == 255);
    CHECK(rc.avgFrameQindex[1] == 255);
    CHECK(rc.lastQ[0] == 255);
    CHECK(rc.lastQ[1] == 255);
    // bufferLevel = startingBufferLevel (pre-set by the fixture).
    CHECK(rc.bufferLevel == 2000000);
    CHECK(rc.bitsOffTarget == 2000000);
    // rolling target/actual = the pre-init avgFrameBandwidth (10000).
    CHECK(rc.rollingTargetBits == 10000);
    CHECK(rc.rollingActualBits == 10000);
    CHECK(rc.totalActualBits == 0);
    CHECK(rc.totalTargetBits == 0);
    CHECK(rc.framesSinceKey == 8);
    CHECK(rc.framesSinceCdfUpdate == 0);
    CHECK(rc.thisKeyFrameForced == 0);
    // rcf[i] = 0.7 for i in 0..6 (kMaxTemporalLayers + 1 = 7 entries).
    for (int i = 0; i <= 6; ++i) CHECK(std::fabs(rc.rateCorrectionFactors[i] - 0.7) <= 1e-9);
    // CBR mode: rcf[kKfStd=5] stays 0.7 (the 1.0 override is mode != CBR).
    CHECK(std::fabs(rc.rateCorrectionFactors[5] - 0.7) <= 1e-9);
    CHECK(rc.baselineGfInterval == 4);  // 1 << hierarchicalLevels(2)
    CHECK(rc.worstQuality == 255);
    CHECK(rc.bestQuality == 0);
    // framerate helper: newFramerate = 30.0 (30/1 = 30 >= 0.1).
    CHECK(std::fabs(newFramerate - 30.0) <= 1e-9);
    // avgFrameBandwidth = (int)(targetBitRate / newFramerate) = (int)(1e6/30) = 33333.
    CHECK(rc.avgFrameBandwidth == 33333);
    // maxFrameBandwidth = max(max(64*250=16000, 2025000), 33333*190/100=63332) = 2025000.
    CHECK(rc.maxFrameBandwidth == 2025000);
    // ME distortion reset.
    CHECK(rc.curAvgBaseMeDist == 0);
    CHECK(rc.prevAvgBaseMeDist == 0);
    CHECK(rc.avgFrameLowMotion == 0);
}

TEST_CASE("rcInit VBR mode: avg qindex mid-point and rcf[kKfStd] = 1.0") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    cfg.mode = l15_rc::kRcVbr;
    cfg.worstAllowedQ = 255;
    cfg.bestAllowedQ = 63;
    double newFramerate = 0.0;
    l15_rc::rcInit(rc, cfg, newFramerate, 30, 1, 2, 1000000, 64, 190);
    // VBR mode: avg/last qindex = (worst + best) / 2 = (255 + 63) / 2 = 159.
    CHECK(rc.avgFrameQindex[0] == 159);
    CHECK(rc.avgFrameQindex[1] == 159);
    CHECK(rc.lastQ[0] == 159);
    CHECK(rc.lastQ[1] == 159);
    // mode != CBR: rcf[kKfStd=5] = 1.0.
    CHECK(std::fabs(rc.rateCorrectionFactors[5] - 1.0) <= 1e-9);
}

TEST_CASE("rcInit Q mode: framerate helper skipped") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    cfg.mode = l15_rc::kRcQ;
    cfg.worstAllowedQ = 255;
    cfg.bestAllowedQ = 0;
    double newFramerate = 0.0;
    l15_rc::rcInit(rc, cfg, newFramerate, 30, 1, 2, 1000000, 64, 190);
    // Q mode (not CBR): avg/last qindex = (worst + best) / 2 = (255+0)/2 = 127.
    CHECK(rc.avgFrameQindex[0] == 127);
    CHECK(rc.avgFrameQindex[1] == 127);
    // rcf[kKfStd] = 1.0 (mode != CBR).
    CHECK(std::fabs(rc.rateCorrectionFactors[5] - 1.0) <= 1e-9);
    // Q mode: framerate helper is NOT called -> avgFrameBandwidth unchanged.
    CHECK(rc.avgFrameBandwidth == 10000);
    CHECK(rc.maxFrameBandwidth == 50000);
}

TEST_CASE("rcInit framerate helper clamps framerate < 0.1 to 30") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    double newFramerate = 0.0;
    // frameRate 1/60 = 0.0166... < 0.1 -> newFramerate clamped to 30.
    l15_rc::rcInit(rc, cfg, newFramerate, 1, 60, 2, 1000000, 64, 190);
    CHECK(std::fabs(newFramerate - 30.0) <= 1e-9);
    // avgFrameBandwidth = (int)(1000000 / 30) = 33333.
    CHECK(rc.avgFrameBandwidth == 33333);
}

TEST_CASE("rcInit maxFrameBandwidth uses the vbrmax_section numerator") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    double newFramerate = 0.0;
    // vbrmaxSection = 400: vbr_max_bits = 33333 * 400 / 100 = 133332.
    // maxFrameBandwidth = max(max(16000, 2025000), 133332) = 2025000.
    l15_rc::rcInit(rc, cfg, newFramerate, 30, 1, 2, 1000000, 64, 400);
    CHECK(rc.maxFrameBandwidth == 2025000);
    // The MBs*250 branch never dominates when MAXRATE_1080P is larger.
    CHECK(rc.maxFrameBandwidth >= 2025000);
}

TEST_CASE("calcQindexRtcCbr full_update: inverse bucket and the flat init") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    l15_rc::PcsCarrier pcs = drvPcs();
    pcs.pictureNumber = 0;  // full_update = true
    l15_rc::CrRefreshCarrier cr{};
    const l15_rc::MeStatCarrier me = drvMe();
    std::uint32_t crSbIndex = 0;
    double newFramerate = 0.0;
    l15_rc::ResizePendingCarrier pending{};

    l15_rc::calcQindexRtcCbr(rc, cfg, pcs, cr, me, crSbIndex, 16, false, false, false, false, pending,
                             newFramerate);

    // starting_buffer_level = 3000 * 1000000 / 1000 = 3000000.
    CHECK(rc.startingBufferLevel == 3000000);
    // optimal_buffer_level = (10000 - 6000) * 1000000 / 1000 = 4000000.
    CHECK(rc.optimalBufferLevel == 4000000);
    // maximum_buffer_size = 10000 * 1000000 / 1000 = 10000000.
    CHECK(rc.maximumBufferSize == 10000000);
    // buffer_level = (10000 - 3000) * 1000000 / 1000 = 7000000.
    CHECK(rc.bufferLevel == 7000000);
    // rc_num_layers = hier(2) == 0 ? 3 : hier + 1 = 3.
    CHECK(rc.rcNumLayers == 3);
    CHECK(rc.rcMiniGopPos == 0);
    // target_size_factors[k] = 1.0 for k in 1..3 - the flat init loop
    // (rc_rtc_cbr.c:501-503) runs k = 0..numLayers, but target_size_factors[0]
    // is then OVERWRITTEN by the CBR/VBR branch (:510-516); it has its own
    // check below.
    for (int k = 1; k <= 3; ++k) CHECK(std::fabs(rc.targetSizeFactors[k] - 1.0) <= 1e-9);
    // mini_qop_size = 1 << (3 - 1) = 4.
    CHECK(rc.miniQopSize == 4);
    // rcf_values[k] = 0.7, rcf_kalman_P[k] = 1.0, rcf_kalman_R[k] = 0.08 for k in 0..4.
    for (int k = 0; k <= 4; ++k) {
        CHECK(std::fabs(rc.rcfValues[k] - 0.7) <= 1e-9);
        CHECK(std::fabs(rc.rcfKalmanP[k] - 1.0) <= 1e-9);
        CHECK(std::fabs(rc.rcfKalmanR[k] - 0.08) <= 1e-9);
    }
    // CBR mode: target_size_factors[0] = starting_buffer_level / (2.0 *
    // avg_frame_bandwidth) = 3000000 / (2.0 * 33333) = 45.00045000450005.
    CHECK(std::fabs(rc.targetSizeFactors[0] - 3000000.0 / (2.0 * 33333.0)) <= 1e-9);
    // base_q_idx is the RTC search's terminal write: qindex 104 for this
    // fixture (measured from the green C2 implementation, whose
    // calculateQindexRtc is covered pointwise by the C2 oracles). Pinned
    // exactly - the old >= 0 proved only that SOME write happened.
    CHECK(pcs.baseQIdx == 104);
}

TEST_CASE("calcQindexRtcCbr soft_update: bandwidth recompute only") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    l15_rc::PcsCarrier pcs = drvPcs();
    pcs.pictureNumber = 10;  // full_update = false
    pcs.bitrateChanged = true;  // soft_update = true
    l15_rc::CrRefreshCarrier cr{};
    const l15_rc::MeStatCarrier me = drvMe();
    std::uint32_t crSbIndex = 0;
    double newFramerate = 0.0;
    l15_rc::ResizePendingCarrier pending{};

    // Set a known pre-update bandwidth to check the max() branch.
    rc.maxFrameBandwidth = 50000;
    l15_rc::calcQindexRtcCbr(rc, cfg, pcs, cr, me, crSbIndex, 16, false, false, false, false, pending,
                             newFramerate);

    // soft_update: framerate = 30/1 = 30 >= 0.1 -> avg = (int)(1e6/30) = 33333.
    CHECK(rc.avgFrameBandwidth == 33333);
    // max_frame_bandwidth = max(33333, 50000) = 50000.
    CHECK(rc.maxFrameBandwidth == 50000);
    // The inverse-bucket conversion runs for soft_update TOO - it sits in
    // the (full_update || soft_update) block (rc_rtc_cbr.c:465-487); only
    // the deeper full_update init (:489-516) is skipped.
    CHECK(rc.startingBufferLevel == 3000000);
    CHECK(rc.optimalBufferLevel == 4000000);
    CHECK(rc.maximumBufferSize == 10000000);
}

TEST_CASE("calcQindexRtcCbr VBR full_update: target_size_factors[0] = 10") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    cfg.mode = l15_rc::kRcVbr;
    l15_rc::PcsCarrier pcs = drvPcs();
    pcs.pictureNumber = 0;  // full_update = true
    l15_rc::CrRefreshCarrier cr{};
    const l15_rc::MeStatCarrier me = drvMe();
    std::uint32_t crSbIndex = 0;
    double newFramerate = 0.0;
    l15_rc::ResizePendingCarrier pending{};

    l15_rc::calcQindexRtcCbr(rc, cfg, pcs, cr, me, crSbIndex, 16, false, false, false, false, pending,
                             newFramerate);
    // VBR: target_size_factors[0] = 10.
    CHECK(std::fabs(rc.targetSizeFactors[0] - 10.0) <= 1e-9);
}

TEST_CASE("rtcUpdateRateCorrectionFactors Kalman: innovation and state update") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    l15_rc::PcsCarrier pcs = drvPcs();
    l15_rc::CrRefreshCarrier cr{};
    cr.applyCyclicRefresh = 0;
    cr.actualNumSeg1Sbs = 0;
    cr.actualNumSeg2Sbs = 0;
    const std::uint16_t b64TotalCount = 1;

    // Set the projected frame size to a known value: correction = 3000/2000 = 1.5.
    // estimatedSize: use the simple form (CR off): est(q=baseQIdx, rcf=0.7) *
    //   2073600 / 512. The oracle pins the result.
    pcs.projectedFrameSize = 3000;
    rc.curAvgBaseMeDist = 5000;  // nonzero: the repeated-picture gate passes

    const double preRcf = rc.rcfValues[l15_rc::getRcfIndex(false, rc.rcMiniGopPos, rc.miniQopSize)];
    const double preP = rc.rcfKalmanP[l15_rc::getRcfIndex(false, rc.rcMiniGopPos, rc.miniQopSize)];
    const double preR = rc.rcfKalmanR[l15_rc::getRcfIndex(false, rc.rcMiniGopPos, rc.miniQopSize)];

    l15_rc::rtcUpdateRateCorrectionFactors(rc, cfg, pcs, cr, b64TotalCount, 8);

    // Verify the Kalman state was updated (P and R changed).
    const double postP = rc.rcfKalmanP[l15_rc::getRcfIndex(false, rc.rcMiniGopPos, rc.miniQopSize)];
    const double postR = rc.rcfKalmanR[l15_rc::getRcfIndex(false, rc.rcMiniGopPos, rc.miniQopSize)];
    CHECK(postP != preP);
    CHECK(postR != preR);
    // P decreases: (1 - K) * (P + Q) < P + Q.
    CHECK(postP < preP + 0.04);
    // The Kalman update rewrites the rcf itself (rc_rtc_cbr.c:603-608):
    // postRcf = preRcf * exp(gain * innovation). estimated scales to
    // resolution FIRST (av1_estimate_frame_size, rc_rtc_cbr.c:160-162:
    // unscaled * 1920 * 1080 / 512 = 3500 * 4050 = 14175000 at qindex 0 -
    // quantizer = ac_qlookup[0]/4 = 1.0, refScale = 1.0, meDist 5000), so
    // correction = 3000/14175000 clamps to 0.25; innovation =
    // log(0.25) = -1.3862943611198906; gain = 0.85802166736675989;
    // K = exp(gain * innovation) = 0.30438236168939931, oracle-derived
    // (the header's float discipline, fabs 1e-9).
    const double postRcf = rc.rcfValues[l15_rc::getRcfIndex(false, rc.rcMiniGopPos, rc.miniQopSize)];
    CHECK(std::fabs(postRcf - preRcf * 0.30438236168939931) <= 1e-9);
}

TEST_CASE("rtcUpdateRateCorrectionFactors overlay gate returns") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    l15_rc::PcsCarrier pcs = drvPcs();
    pcs.isOverlay = true;
    l15_rc::CrRefreshCarrier cr{};
    const std::uint16_t b64TotalCount = 1;

    const double preP = rc.rcfKalmanP[l15_rc::getRcfIndex(false, rc.rcMiniGopPos, rc.miniQopSize)];
    l15_rc::rtcUpdateRateCorrectionFactors(rc, cfg, pcs, cr, b64TotalCount, 8);
    // Overlay: no update -> P unchanged.
    CHECK(std::fabs(rc.rcfKalmanP[l15_rc::getRcfIndex(false, rc.rcMiniGopPos, rc.miniQopSize)] - preP) <= 1e-9);
}

TEST_CASE("rtcUpdateRateCorrectionFactors repeated-picture gate returns") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    l15_rc::PcsCarrier pcs = drvPcs();
    rc.curAvgBaseMeDist = 0;  // repeated picture -> the gate returns
    l15_rc::CrRefreshCarrier cr{};
    const std::uint16_t b64TotalCount = 1;

    const double preP = rc.rcfKalmanP[l15_rc::getRcfIndex(false, rc.rcMiniGopPos, rc.miniQopSize)];
    l15_rc::rtcUpdateRateCorrectionFactors(rc, cfg, pcs, cr, b64TotalCount, 8);
    // Repeated picture: no update -> P unchanged.
    CHECK(std::fabs(rc.rcfKalmanP[l15_rc::getRcfIndex(false, rc.rcMiniGopPos, rc.miniQopSize)] - preP) <= 1e-9);
}

TEST_CASE("rtcUpdateBufferLevel inverted bucket: add, subtract and floor") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::PcsCarrier pcs = drvPcs();
    pcs.showableFrame = true;

    // showable: bufferLevel += encoded - avgFrameBandwidth.
    rc.bufferLevel = 1000000;
    l15_rc::rtcUpdateBufferLevel(rc, pcs, 3000);
    // 1000000 + 3000 - 10000 = 993000.
    CHECK(rc.bufferLevel == 993000);

    // non-showable: bufferLevel += encoded only.
    pcs.showableFrame = false;
    rc.bufferLevel = 1000000;
    l15_rc::rtcUpdateBufferLevel(rc, pcs, 3000);
    // 1000000 + 3000 = 1003000.
    CHECK(rc.bufferLevel == 1003000);

    // floor at 0: bufferLevel + encoded - avg < 0 -> 0.
    pcs.showableFrame = true;
    rc.bufferLevel = 0;
    l15_rc::rtcUpdateBufferLevel(rc, pcs, 0);
    // 0 + 0 - 10000 = -10000 -> floored to 0.
    CHECK(rc.bufferLevel == 0);
}

TEST_CASE("recodeDecisionRtcCbr loop_count and worst_quality gates") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    l15_rc::PcsCarrier pcs = drvPcs();
    l15_rc::CrRefreshCarrier cr{};
    const std::uint16_t b64TotalCount = 1;

    // loopCount > 0: prevent re-entry.
    pcs.loopCount = 1;
    CHECK_FALSE(l15_rc::recodeDecisionRtcCbr(rc, cfg, pcs, cr, b64TotalCount, 8));

    // baseQIdx >= worstQuality: can't reduce.
    pcs.loopCount = 0;
    pcs.baseQIdx = 255;
    CHECK_FALSE(l15_rc::recodeDecisionRtcCbr(rc, cfg, pcs, cr, b64TotalCount, 8));
}

TEST_CASE("recodeDecisionRtcCbr no-overflow returns false") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    l15_rc::PcsCarrier pcs = drvPcs();
    l15_rc::CrRefreshCarrier cr{};
    const std::uint16_t b64TotalCount = 1;

    // projected_frame_size = roundPowerOfTwo(100000, 9) + 0 = (100000+256)/512
    // = 196 (int truncation of 196.5).
    pcs.pcsTotalRate = 100000;
    pcs.baseQIdx = 100;
    // projected_buffer = bufferLevel(12000000) + 196 - avgFrameBandwidth(10000)
    // = 11902196; maximum_buffer_size = 20000000 -> 11902196 <= 20000000: no overflow.
    // projected_frame_size(196) <= maxFrameSize(INT64_MAX or the CBR cap):
    // -> no recode.
    CHECK_FALSE(l15_rc::recodeDecisionRtcCbr(rc, cfg, pcs, cr, b64TotalCount, 8));
}

TEST_CASE("recodeDecisionRtcCbr overflow triggers the R-Q search") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    l15_rc::PcsCarrier pcs = drvPcs();
    l15_rc::CrRefreshCarrier cr{};
    const std::uint16_t b64TotalCount = 1;

    // Force overflow: projected_frame_size huge.
    pcs.pcsTotalRate = 3000000000ULL;  // projected = (3e9+256)/512 = 5860000
    pcs.baseQIdx = 100;
    // projected_buffer = 12000000 + 5860000 - 10000 = 17856000;
    // maximum_buffer_size = 20000000 -> 17856000 <= 20000000: no VBV overflow.
    // But: CBR + maxInterBitratePct: set max_inter to cap.
    cfg.mode = l15_rc::kRcCbr;
    cfg.maxInterBitratePct = 50;
    // maxFrameSize = 10000 * 50 / 100 = 5000; projected(5860000) > 5000: overflow.
    // target_size = min(5000, 20000000 - 12000000 + 10000) = min(5000, 8010000) = 5000.
    // target_size(5000) >= 200 (FRAME_OVERHEAD_BITS): the R-Q search runs.
    const bool recoded = l15_rc::recodeDecisionRtcCbr(rc, cfg, pcs, cr, b64TotalCount, 8);
    CHECK(recoded);
    // new_q_idx = max(base_q_idx + 4, found) -> base_q_idx(100) + 4 = 104 at least.
    CHECK(pcs.baseQIdx >= 104);
}

TEST_CASE("postencodeUpdateRtcCbr KEY frame: qindex fold and state reset") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    l15_rc::PcsCarrier pcs = drvPcs();
    pcs.frameType = l15_rc::kKeyFrame;
    pcs.totalNumBits = 3000;
    pcs.baseQIdx = 100;
    l15_rc::CrRefreshCarrier cr{};
    const std::uint16_t b64TotalCount = 1;

    // pre: avgFrameQindex[0] = 100, avgFrameQindex[1] = 150.
    l15_rc::postencodeUpdateRtcCbr(rc, cfg, pcs, cr, b64TotalCount, 8, false);

    // avgFrameQindex[0] = roundPowerOfTwo(3*100 + 100, 2) = (400+2)/4 = 100.
    // Wait: roundPowerOfTwo(value, 2) = (value + 2) >> 2.
    // 3*100 + 100 = 400; (400 + 2) >> 2 = 402 >> 2 = 100.
    CHECK(rc.avgFrameQindex[0] == 100);
    // INTER unchanged (KEY frame path).
    CHECK(rc.avgFrameQindex[1] == 150);
    CHECK(rc.framesSinceKey == 0);
    CHECK(rc.framesSinceCdfUpdate == 0);
    CHECK(rc.rcMiniGopPos == 0);
    // buffer level updated: bufferLevel += projected(3000) - avg(10000) =
    // 12000000 + 3000 - 10000 = 11993000.
    CHECK(rc.bufferLevel == 12000000 + 3000 - 10000);
}

TEST_CASE("postencodeUpdateRtcCbr INTER frame: qindex fold and low motion") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    l15_rc::PcsCarrier pcs = drvPcs();
    pcs.frameType = l15_rc::kInterFrame;
    pcs.isOverlay = false;
    pcs.totalNumBits = 3000;
    pcs.baseQIdx = 100;
    pcs.avgCntZeromv = 50;
    rc.avgFrameLowMotion = 0;  // first frame: set directly
    l15_rc::CrRefreshCarrier cr{};
    const std::uint16_t b64TotalCount = 1;

    l15_rc::postencodeUpdateRtcCbr(rc, cfg, pcs, cr, b64TotalCount, 8, false);

    // avgFrameQindex[1] = roundPowerOfTwo(3*150 + 100, 2) = (450+100+2)/4
    // = (552) >> 2 = 138.
    CHECK(rc.avgFrameQindex[1] == 138);
    // KEY untouched.
    CHECK(rc.avgFrameQindex[0] == 100);
    // avg_frame_low_motion was 0 -> set directly to avg_cnt_zeromv(50).
    CHECK(rc.avgFrameLowMotion == 50);
    // rc_mini_gop_pos = (pre + 1) % mini_qop_size = (0 + 1) % 4 = 1.
    CHECK(rc.rcMiniGopPos == 1);
}

TEST_CASE("postencodeUpdateRtcCbr low-motion EMA and mini-gop wrap") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    l15_rc::PcsCarrier pcs = drvPcs();
    pcs.frameType = l15_rc::kInterFrame;
    pcs.isOverlay = false;
    pcs.totalNumBits = 3000;
    pcs.baseQIdx = 100;
    pcs.avgCntZeromv = 80;
    rc.avgFrameLowMotion = 40;  // nonzero -> the EMA path
    rc.rcMiniGopPos = 3;  // (3 + 1) % 4 = 0 (wrap)
    l15_rc::CrRefreshCarrier cr{};
    const std::uint16_t b64TotalCount = 1;

    l15_rc::postencodeUpdateRtcCbr(rc, cfg, pcs, cr, b64TotalCount, 8, false);

    // avg_frame_low_motion = roundPowerOfTwo(3*40 + 80, 2) = (120+80+2)/4
    // = 202 >> 2 = 50.
    CHECK(rc.avgFrameLowMotion == 50);
    // rc_mini_gop_pos = (3 + 1) % 4 = 0.
    CHECK(rc.rcMiniGopPos == 0);
}

TEST_CASE("postencodeUpdateRtcCbr overlay skips the INTER fold") {
    l15_rc::RcControl rc = drvRc();
    l15_rc::RcConfig cfg = drvCfg();
    l15_rc::PcsCarrier pcs = drvPcs();
    pcs.frameType = l15_rc::kInterFrame;
    pcs.isOverlay = true;  // overlay -> the INTER fold is skipped
    pcs.totalNumBits = 3000;
    pcs.baseQIdx = 100;
    rc.avgFrameLowMotion = 0;
    l15_rc::CrRefreshCarrier cr{};
    const std::uint16_t b64TotalCount = 1;

    l15_rc::postencodeUpdateRtcCbr(rc, cfg, pcs, cr, b64TotalCount, 8, false);

    // Overlay: INTER fold skipped -> avgFrameQindex[1] unchanged (150).
    CHECK(rc.avgFrameQindex[1] == 150);
    // rc_mini_gop_pos unchanged (0).
    CHECK(rc.rcMiniGopPos == 0);
    // avg_frame_low_motion unchanged (0).
    CHECK(rc.avgFrameLowMotion == 0);
}

TEST_CASE("roundPowerOfTwo pins the definitions rounding shift") {
    // ROUND_POWER_OF_TWO(value, n) = (value + ((1 << n) >> 1)) >> n.
    // value=5, n=2: (5 + 2) >> 2 = 7 >> 2 = 1.
    CHECK(l15_rc::roundPowerOfTwo(5, 2) == 1);
    // value=7, n=2: (7 + 2) >> 2 = 9 >> 2 = 2.
    CHECK(l15_rc::roundPowerOfTwo(7, 2) == 2);
    // value=400, n=2: (400 + 2) >> 2 = 402 >> 2 = 100.
    CHECK(l15_rc::roundPowerOfTwo(400, 2) == 100);
    // value=552, n=2: (552 + 2) >> 2 = 554 >> 2 = 138.
    CHECK(l15_rc::roundPowerOfTwo(552, 2) == 138);
}

// ---- A9D S1: rc_process.c rc_param_reset :555-566 +
// svt_aom_update_rc_counts :568-590 + reset_rc_param :592-605 ----

TEST_CASE("rc_param_reset resets the interval-param carrier") {
    l15_rc::RcIntervalParamCarrier param{};
    param.firstPoc = 7;
    param.size = 5;
    param.processedFrameNumber = 5;
    param.vbrBitsOffTarget = 12345;
    param.vbrBitsOffTargetFast = 999;
    param.rollingTargetBits = 100;
    param.rollingActualBits = 90;
    param.rateErrorEstimate = 12;
    param.totalActualBits = 500000;
    param.totalTargetBits = 480000;
    param.extendMinq = 3;
    param.extendMaxq = 4;
    param.extendMinqFast = 2;

    l15_rc::rcParamReset(param);

    // rc_param_reset (rc_process.c:555-566) writes ELEVEN fields; first_poc,
    // rolling_target_bits and rolling_actual_bits are NOT touched.
    CHECK(param.size == -1);
    CHECK(param.processedFrameNumber == 0);
    CHECK(param.vbrBitsOffTarget == 0);
    CHECK(param.vbrBitsOffTargetFast == 0);
    CHECK(param.rateErrorEstimate == 0);
    CHECK(param.totalActualBits == 0);
    CHECK(param.totalTargetBits == 0);
    CHECK(param.extendMinq == 0);
    CHECK(param.extendMaxq == 0);
    CHECK(param.extendMinqFast == 0);
    // the untouched trio keeps its values.
    CHECK(param.firstPoc == 7);
    CHECK(param.rollingTargetBits == 100);
    CHECK(param.rollingActualBits == 90);
}

TEST_CASE("update_rc_counts walks the showable gate and the cdf branch") {
    // svt_aom_update_rc_counts (rc_process.c:568-590): showable frames
    // increment frames_since_key and decrement frames_to_key; the
    // disable_cdf_update branch moves frames_since_cdf_update.
    {
        l15_rc::RcControl rc = drvRc();
        rc.framesSinceKey = 8;
        rc.framesToKey = 20;
        rc.framesSinceCdfUpdate = 0;
        l15_rc::PcsCarrier pcs = drvPcs();
        pcs.showableFrame = true;
        pcs.disableCdfUpdate = false;
        l15_rc::updateRcCounts(rc, pcs);
        CHECK(rc.framesSinceKey == 9);
        CHECK(rc.framesToKey == 19);
        CHECK(rc.framesSinceCdfUpdate == 0);
    }
    {
        l15_rc::RcControl rc = drvRc();
        rc.framesSinceKey = 8;
        rc.framesToKey = 20;
        rc.framesSinceCdfUpdate = 3;
        l15_rc::PcsCarrier pcs = drvPcs();
        pcs.showableFrame = true;
        pcs.disableCdfUpdate = true;
        l15_rc::updateRcCounts(rc, pcs);
        CHECK(rc.framesSinceKey == 9);
        CHECK(rc.framesToKey == 19);
        CHECK(rc.framesSinceCdfUpdate == 4);
    }
    {
        // not showable: NOTHING moves (the whole walk is inside the gate).
        l15_rc::RcControl rc = drvRc();
        rc.framesSinceKey = 8;
        rc.framesToKey = 20;
        rc.framesSinceCdfUpdate = 3;
        l15_rc::PcsCarrier pcs = drvPcs();
        pcs.showableFrame = false;
        pcs.disableCdfUpdate = false;
        l15_rc::updateRcCounts(rc, pcs);
        CHECK(rc.framesSinceKey == 8);
        CHECK(rc.framesToKey == 20);
        CHECK(rc.framesSinceCdfUpdate == 3);
    }
}

TEST_CASE("reset_rc_param zeroes the ppcs recode counters") {
    // reset_rc_param (rc_process.c:592-605): loop_count + the recode
    // seen-flags on the ppcs wall.
    l15_rc::PcsCarrier pcs = drvPcs();
    pcs.loopCount = 4;
    pcs.overshootSeen = 1;
    pcs.undershootSeen = 1;
    l15_rc::resetRcParam(pcs);
    CHECK(pcs.loopCount == 0);
    CHECK(pcs.overshootSeen == 0);
    CHECK(pcs.undershootSeen == 0);
}

TEST_CASE("rc_init_frame_stats pins the rate-average period and the ME-dist walk") {
    // rc_init_frame_stats (rc_process.c:607-640): the r0/r0beta call and the
    // three ref-percentage walks are the named parks (l6/l4 walls); the
    // portable surface is the rate-averaging period + the ME-dist average.
    static const std::uint32_t d64[4] = {1000, 2000, 3000, 4000};
    {
        // two-pass + max_bit_rate: the period is total_stats->count, then
        // MIN-ed with MAX_RATE_AVG_PERIOD (the 1500 count clamps to 1000).
        l15_rc::RcControl rc = drvRc();
        rc.curAvgBaseMeDist = 500;
        l15_rc::rcInitFrameStats(rc, l15_rc::kBslice, 4, d64, true, 1500.0);
        CHECK(rc.rateAveragePeriod == 1000);
        // non-I_SLICE: prev <- cur (500), cur <- avg(1000+2000+3000+4000)/4.
        CHECK(rc.prevAvgBaseMeDist == 500);
        CHECK(rc.curAvgBaseMeDist == 2500);
        // a count below the clamp passes through.
        l15_rc::rcInitFrameStats(rc, l15_rc::kBslice, 4, d64, true, 800.0);
        CHECK(rc.rateAveragePeriod == 800);
    }
    {
        // I_SLICE: the ME-dist walk is skipped entirely (a null pointer is
        // never dereferenced on the I_SLICE path).
        l15_rc::RcControl rc = drvRc();
        rc.prevAvgBaseMeDist = 11;
        rc.curAvgBaseMeDist = 22;
        l15_rc::rcInitFrameStats(rc, l15_rc::kIslice, 1, nullptr, false, 60.0);
        CHECK(rc.prevAvgBaseMeDist == 11);
        CHECK(rc.curAvgBaseMeDist == 22);
    }
    {
        // NOT (two-pass + max_bit_rate): the 60 default.
        l15_rc::RcControl rc = drvRc();
        l15_rc::rcInitFrameStats(rc, l15_rc::kBslice, 4, d64, false, 1500.0);
        CHECK(rc.rateAveragePeriod == 60);
        l15_rc::rcInitFrameStats(rc, l15_rc::kBslice, 4, d64, true, 30.0);
        CHECK(rc.rateAveragePeriod == 30);
    }
}
