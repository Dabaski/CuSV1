#include <doctest.h>
#include <cmath>
#include <cstdint>

#include "rc_arith.h"

// RC2 Wave D3: the vbr rate-factor updater, the postencode pair and the
// regulated-q pair. Reference spans (rc_vbr_cbr.c):
//   av1_estimate_bits_at_q :1349-1353, av1_rc_update_rate_correction_factors
//   :1355-1472, svt_av1_rc_postencode_update_gop_const :1495-1561,
//   svt_av1_rc_postencode_update :1563-1629, get_regulated_q_overshoot
//   :1720-1735, get_regulated_q_undershoot :1738-1752.
//
// FLOAT DISCIPLINE: integer state pinned exactly; rcf doubles compared
// ABSOLUTE 1e-9 with fabs. The updater's correction_factor arithmetic is
// INT (int64 division truncation at :1406, the int cast at :1451/:1462) -
// the oracle replicates the int truncations, not doubles.
//
// EXPECTATION DERIVATION: oracle-derived. The python oracle replicates the
// reference chains from the closed-form rc_bits_per_mb
// (enumerator * correction_factor / q) and the 8-bit ac_qlookup_QTX; the
// rcf trajectory through the regulated retry loops is deterministic per
// UCRT libm (both the port and the oracle run the same CRT log10).

namespace {

// The D3 fixture rc: every field the updater and the postencode pair
// consume is pre-set to a known value.
l15_rc::RcControl upRc() {
    l15_rc::RcControl rc{};
    for (int i = 0; i < 7; ++i) rc.rateCorrectionFactors[i] = 0.7;
    rc.q1Frame = 100;
    rc.q2Frame = 90;
    rc.rc1Frame = 0;
    rc.rc2Frame = 1;
    rc.percentRefreshAdjustment = 0;
    rc.rateRatioQdeltaAdjustment = 0.0;
    rc.avgFrameQindex[0] = 100;
    rc.avgFrameQindex[1] = 150;
    rc.lastQ[0] = 100;
    rc.lastQ[1] = 150;
    rc.lastBoostedQindex = 180;
    rc.constrainedGfGroup = 0;
    rc.bufferLevel = 12000000;
    rc.bitsOffTarget = 12000000;
    rc.maximumBufferSize = 20000000;
    rc.avgFrameBandwidth = 10000;
    rc.prevAvgFrameBandwidth = 5000;
    rc.rollingTargetBits = 10000;
    rc.rollingActualBits = 10000;
    rc.totalActualBits = 0;
    rc.totalTargetBits = 0;
    rc.avgFrameLowMotion = 40;
    rc.framesSinceKey = 8;
    rc.framesSinceCdfUpdate = 5;
    return rc;
}

l15_rc::PcsCarrier upPcs() {
    l15_rc::PcsCarrier pcs{};
    pcs.frameType = l15_rc::kInterFrame;
    pcs.updateType = l15_rc::kSvtAv1LfUpdate;
    pcs.temporalLayerIndex = 0;
    pcs.baseQIdx = 100;
    pcs.frameWidth = 1920;
    pcs.frameHeight = 1080;
    pcs.encoderBitDepth = 8;
    pcs.isOverlay = false;
    pcs.scClass1 = false;
    pcs.showableFrame = true;
    pcs.totalNumBits = 3000;
    pcs.thisFrameTarget = 5000;
    pcs.avgCntZeromv = 80;
    pcs.projectedFrameSize = 398437;  // EST100: est(q=100, rcf=0.7, 8160 MBs)
    return pcs;
}

l15_rc::CrRefreshCarrier upCr() {
    l15_rc::CrRefreshCarrier cr{};
    cr.applyCyclicRefresh = 0;
    cr.actualNumSeg1Sbs = 0;
    cr.actualNumSeg2Sbs = 0;
    return cr;
}

l15_rc::RcConfig upCfg() {
    l15_rc::RcConfig cfg{};
    cfg.mode = l15_rc::kRcVbr;
    cfg.worstAllowedQ = 255;
    cfg.bestAllowedQ = 0;
    return cfg;
}

}  // namespace

TEST_CASE("estimateBitsAtQ pins the bpm-to-bits conversion and the floor") {
    // bpm(INTER, q=100, cf=1.0) = (int)(1000000/28.0) = 35714;
    // bits = (35714*8160) >> 9 = 291426240 >> 9 = 569191 (the quotient is
    // 569191.875 - the floor, no rounding).
    CHECK(l15_rc::estimateBitsAtQ(l15_rc::kInterFrame, 100, 8160, 1.0, l15_rc::kEbEightBit, false) == 569191);
    // bpm(KEY) = (int)(1400000/28.0) = 50000 -> 796875.
    CHECK(l15_rc::estimateBitsAtQ(l15_rc::kKeyFrame, 100, 8160, 1.0, l15_rc::kEbEightBit, false) == 796875);
    // screen-content INTER: bpm = (int)(750000/28.0) = 26785 -> 426885.
    CHECK(l15_rc::estimateBitsAtQ(l15_rc::kInterFrame, 100, 8160, 1.0, l15_rc::kEbEightBit, true) == 426885);
    // mbs = 0: the product collapses to 0 -> the FRAME_OVERHEAD_BITS floor.
    CHECK(l15_rc::estimateBitsAtQ(l15_rc::kInterFrame, 100, 0, 1.0, l15_rc::kEbEightBit, false) == 200);
}

TEST_CASE("rcUpdateRateCorrectionFactors deadband: q/rc frames shift, rcf holds") {
    l15_rc::RcControl rc = upRc();
    l15_rc::PcsCarrier pcs = upPcs();
    const l15_rc::CrRefreshCarrier cr = upCr();
    // projected == est(398437): correction_factor = 100 -> the deadband.
    l15_rc::rcUpdateRateCorrectionFactors(rc, upCfg(), pcs, cr, 100, 1920, 1080, 8);
    CHECK(rc.q2Frame == 100);  // old q1
    CHECK(rc.q1Frame == 100);  // base_q_idx
    CHECK(rc.rc2Frame == 0);   // old rc1
    CHECK(rc.rc1Frame == 0);   // 100 in [90, 110]
    CHECK(rc.percentRefreshAdjustment == 0);
    CHECK(std::fabs(rc.rateCorrectionFactors[1] - 0.7) <= 1e-9);  // VBR slot tl+1
}

TEST_CASE("rcUpdateRateCorrectionFactors overshoot: the 10x clamp to al 0.75") {
    l15_rc::RcControl rc = upRc();
    l15_rc::PcsCarrier pcs = upPcs();
    const l15_rc::CrRefreshCarrier cr = upCr();
    pcs.projectedFrameSize = 3984370;  // 10x est -> cf = 1000
    l15_rc::rcUpdateRateCorrectionFactors(rc, upCfg(), pcs, cr, 100, 1920, 1080, 8);
    CHECK(rc.rc2Frame == 0);
    CHECK(rc.rc1Frame == -1);  // 1000 > 110
    // VBR al = 0.25 + 0.5*min(1, |log10(10)|) = 0.75; cf = 100 + 900*0.75 = 775.
    CHECK(std::fabs(rc.rateCorrectionFactors[1] - 5.425) <= 1e-9);  // 0.7 * 7.75
}

TEST_CASE("rcUpdateRateCorrectionFactors undershoot: the 25 floor then 0.259") {
    l15_rc::RcControl rc = upRc();
    l15_rc::PcsCarrier pcs = upPcs();
    const l15_rc::CrRefreshCarrier cr = upCr();
    pcs.projectedFrameSize = 39843;  // 0.1x -> cf raw 9, floored at 25
    l15_rc::rcUpdateRateCorrectionFactors(rc, upCfg(), pcs, cr, 100, 1920, 1080, 8);
    CHECK(rc.rc2Frame == 0);
    CHECK(rc.rc1Frame == 1);  // 25 < 90
    // VBR al = 0.25 + 0.5*|log10(0.25)| = 0.551030...; cf = 100/(1+3*al)
    // = 0.376923... -> int 37; rcf = 0.7 * 0.37 = 0.259.
    CHECK(std::fabs(rc.rateCorrectionFactors[1] - 0.259) <= 1e-9);
}

TEST_CASE("rcUpdateRateCorrectionFactors overlay returns before any write") {
    l15_rc::RcControl rc = upRc();
    l15_rc::PcsCarrier pcs = upPcs();
    const l15_rc::CrRefreshCarrier cr = upCr();
    pcs.isOverlay = true;
    pcs.projectedFrameSize = 3984370;  // would move everything
    l15_rc::rcUpdateRateCorrectionFactors(rc, upCfg(), pcs, cr, 100, 1920, 1080, 8);
    CHECK(rc.q2Frame == 90);  // untouched
    CHECK(rc.q1Frame == 100);
    CHECK(rc.rc2Frame == 1);
    CHECK(rc.rc1Frame == 0);
    CHECK(std::fabs(rc.rateCorrectionFactors[1] - 0.7) <= 1e-9);
}

TEST_CASE("rcUpdateRateCorrectionFactors CR-on CBR overshoot clamps at MAX_BPB") {
    l15_rc::RcControl rc = upRc();
    l15_rc::PcsCarrier pcs = upPcs();
    l15_rc::CrRefreshCarrier cr = upCr();
    l15_rc::RcConfig cfg = upCfg();
    cfg.mode = l15_rc::kRcCbr;
    // Weighted CR estimate: w1 = 0.2 (qd +8), w2 = 0.1 (qd +16);
    // est = 0.7*398437 + 0.2*348632 + 0.1*309888 = 379621.
    cr.applyCyclicRefresh = 1;
    cr.actualNumSeg1Sbs = 20;
    cr.actualNumSeg2Sbs = 10;
    cr.qindexDelta[1] = 8;
    cr.qindexDelta[2] = 16;
    pcs.projectedFrameSize = 56919200;  // cf raw 14993
    l15_rc::rcUpdateRateCorrectionFactors(rc, cfg, pcs, cr, 100, 1920, 1080, 8);
    CHECK(rc.rc1Frame == -1);  // cf > 110
    CHECK(rc.percentRefreshAdjustment == -1);  // cf > 125: pra-1 floored -5
    CHECK(std::fabs(rc.rateRatioQdeltaAdjustment - 0.0) <= 1e-9);  // max(-0.05, -0.0)
    // CBR al = 0.25 + 0.75*min(0.5, ...) = 0.625; cf = 100 + 14893*0.625
    // = 9408; rcf = 0.7*94.08 = 65.856 -> clamped at MAX_BPB 50. CBR INTER
    // leaf slot is rateCorrectionFactors[0].
    CHECK(std::fabs(rc.rateCorrectionFactors[0] - 50.0) <= 1e-9);
}

TEST_CASE("rcUpdateRateCorrectionFactors CR-on CBR undershoot: pra/rrqa rise") {
    l15_rc::RcControl rc = upRc();
    l15_rc::PcsCarrier pcs = upPcs();
    l15_rc::CrRefreshCarrier cr = upCr();
    l15_rc::RcConfig cfg = upCfg();
    cfg.mode = l15_rc::kRcCbr;
    cr.applyCyclicRefresh = 1;
    cr.actualNumSeg1Sbs = 20;
    cr.actualNumSeg2Sbs = 10;
    cr.qindexDelta[1] = 8;
    cr.qindexDelta[2] = 16;
    pcs.projectedFrameSize = 5691;  // cf raw 1, floored at 25
    l15_rc::rcUpdateRateCorrectionFactors(rc, cfg, pcs, cr, 100, 1920, 1080, 8);
    CHECK(rc.rc1Frame == 1);  // 25 < 90
    CHECK(rc.percentRefreshAdjustment == 1);   // cf < 50: pra+1 capped 5
    CHECK(std::fabs(rc.rateRatioQdeltaAdjustment - 0.05) <= 1e-9);
    // < 99 branch with al 0.625: cf = (int)(100/(1+3*0.625)) = 34;
    // rcf = 0.7*0.34 = 0.238.
    CHECK(std::fabs(rc.rateCorrectionFactors[0] - 0.238) <= 1e-9);
}

TEST_CASE("postencodeUpdate KEY frame: fold, lastQ, boosted, buffer, totals") {
    l15_rc::RcControl rc = upRc();
    l15_rc::PcsCarrier pcs = upPcs();
    pcs.frameType = l15_rc::kKeyFrame;
    const l15_rc::CrRefreshCarrier cr = upCr();
    int frameUpdated = 2;
    l15_rc::postencodeUpdate(rc, upCfg(), pcs, cr, 100, 8, frameUpdated);
    CHECK(frameUpdated == 0);  // KEY resets the frame_updated counter
    CHECK(rc.avgFrameQindex[0] == 100);  // RPOT(3*100+100, 2)
    CHECK(rc.lastQ[0] == 28);  // (int)convert_qindex_to_q(100, 8) = 28.0
    CHECK(rc.framesSinceKey == 0);
    CHECK(rc.framesSinceCdfUpdate == 0);
    CHECK(rc.lastBoostedQindex == 100);  // 100 < 180 and KEY
    // update_buffer_level: showable -> bitsOff += 10000 - 3000.
    CHECK(rc.bitsOffTarget == 12007000);
    CHECK(rc.bufferLevel == 12007000);
    CHECK(rc.totalActualBits == 3000);
    CHECK(rc.totalTargetBits == 10000);
    CHECK(rc.prevAvgFrameBandwidth == 10000);
    // The updater ran first with the KEY frame type: est uses the KEY
    // enumerator (35000 bpm -> 557812), projected 3000 -> cf 25 -> the
    // undershoot branch; VBR KEY slot is rateCorrectionFactors[0].
    CHECK(rc.rc1Frame == 1);
    CHECK(std::fabs(rc.rateCorrectionFactors[0] - 0.259) <= 1e-9);
}

TEST_CASE("postencodeUpdate INTER leaf: fold, rolling bits, low-motion EMA") {
    l15_rc::RcControl rc = upRc();
    l15_rc::PcsCarrier pcs = upPcs();  // INTER, LF update: a leaf
    const l15_rc::CrRefreshCarrier cr = upCr();
    int frameUpdated = 2;
    l15_rc::postencodeUpdate(rc, upCfg(), pcs, cr, 100, 8, frameUpdated);
    CHECK(frameUpdated == 3);  // INTER increments
    CHECK(rc.avgFrameQindex[1] == 138);  // RPOT(3*150+100, 2)
    CHECK(rc.lastQ[1] == 28);
    CHECK(rc.framesSinceKey == 8);  // unchanged (not KEY)
    CHECK(rc.lastBoostedQindex == 100);  // 100 < 180
    CHECK(rc.bitsOffTarget == 12007000);
    CHECK(rc.rollingTargetBits == 8750);  // RPOT(3*10000 + 5000, 2)
    CHECK(rc.rollingActualBits == 8250);  // RPOT(3*10000 + 3000, 2)
    CHECK(rc.avgFrameLowMotion == 50);    // RPOT(3*40 + 80, 2)
    CHECK(rc.totalActualBits == 3000);
}

TEST_CASE("postencodeUpdate ARF leaf-exclusion skips the fold, keeps the EMA") {
    l15_rc::RcControl rc = upRc();
    l15_rc::PcsCarrier pcs = upPcs();
    pcs.updateType = l15_rc::kSvtAv1ArfUpdate;  // GF/ARF: excluded from the fold
    const l15_rc::CrRefreshCarrier cr = upCr();
    int frameUpdated = 2;
    l15_rc::postencodeUpdate(rc, upCfg(), pcs, cr, 100, 8, frameUpdated);
    CHECK(rc.avgFrameQindex[1] == 150);  // unchanged
    CHECK(rc.lastQ[1] == 150);           // unchanged
    // The low-motion EMA sits OUTSIDE the leaf exclusion (rc_vbr_cbr.c
    // :1618-1620) - it runs for every non-KEY frame, unlike the RTC twin.
    CHECK(rc.avgFrameLowMotion == 50);
}

TEST_CASE("postencodeUpdate overlay skips the fold, keeps the EMA") {
    l15_rc::RcControl rc = upRc();
    l15_rc::PcsCarrier pcs = upPcs();
    pcs.isOverlay = true;
    const l15_rc::CrRefreshCarrier cr = upCr();
    int frameUpdated = 2;
    l15_rc::postencodeUpdate(rc, upCfg(), pcs, cr, 100, 8, frameUpdated);
    CHECK(rc.avgFrameQindex[1] == 150);
    CHECK(rc.avgFrameLowMotion == 50);
}

TEST_CASE("postencodeUpdateGopConst routes rolling/total to the param carrier") {
    l15_rc::RcControl rc = upRc();
    l15_rc::PcsCarrier pcs = upPcs();  // INTER leaf
    const l15_rc::CrRefreshCarrier cr = upCr();
    l15_rc::RcIntervalParamCarrier param{};
    param.rollingTargetBits = 20000;
    param.rollingActualBits = 20000;
    param.totalActualBits = 0;
    param.totalTargetBits = 0;
    int frameUpdated = 2;
    l15_rc::postencodeUpdateGopConst(rc, upCfg(), pcs, cr, param, 100, 8, frameUpdated);
    CHECK(frameUpdated == 3);
    CHECK(param.rollingTargetBits == 16250);  // RPOT(3*20000 + 5000, 2)
    CHECK(param.rollingActualBits == 15750);  // RPOT(3*20000 + 3000, 2)
    CHECK(param.totalActualBits == 3000);
    CHECK(param.totalTargetBits == 10000);
    // The gop_const twin does NOT touch the rc-owned rolling monitors.
    CHECK(rc.rollingTargetBits == 10000);
    CHECK(rc.rollingActualBits == 10000);
    // ...and has NO low-motion EMA: avg_frame_low_motion stays 40.
    CHECK(rc.avgFrameLowMotion == 40);
    CHECK(rc.avgFrameQindex[1] == 138);  // the fold itself is shared
}

TEST_CASE("getRegulatedQOvershoot retries to the 10 cap and pins the rcf clamp") {
    l15_rc::RcControl rc = upRc();
    l15_rc::PcsCarrier pcs = upPcs();
    pcs.projectedFrameSize = 30000000;
    pcs.thisFrameTarget = 3000000;
    const l15_rc::CrRefreshCarrier cr = upCr();
    // q_low 240, q_high 255, top 255, bottom 0: regulate keeps returning
    // q < 240 while the rcf climbs; the rcf hits MAX_BPB and the loop
    // exhausts its 10 retries at q 227.
    const int q = l15_rc::getRegulatedQOvershoot(rc, upCfg(), pcs, cr, 100, 240, 255, 255, 0, 8);
    CHECK(q == 227);
    CHECK(std::fabs(rc.rateCorrectionFactors[1] - 50.0) <= 1e-9);  // MAX_BPB
}

TEST_CASE("getRegulatedQUndershoot retries to the 10 cap at MIN_BPB") {
    l15_rc::RcControl rc = upRc();
    l15_rc::PcsCarrier pcs = upPcs();
    pcs.projectedFrameSize = 1000;
    pcs.thisFrameTarget = 300;
    const l15_rc::CrRefreshCarrier cr = upCr();
    // q_high 60: regulate keeps returning q > 60 while the rcf falls;
    // the rcf floors at MIN_BPB and the loop exhausts at q 227.
    const int q = l15_rc::getRegulatedQUndershoot(rc, upCfg(), pcs, cr, 100, 60, 255, 0, 8);
    CHECK(q == 227);
    CHECK(std::fabs(rc.rateCorrectionFactors[1] - 0.005) <= 1e-9);  // MIN_BPB
}
