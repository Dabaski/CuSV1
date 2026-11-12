#include <doctest.h>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "rc_arith.h"

// RC2c Wave C1: the rc_rtc_cbr.c primitives. Reference spans:
//   :42-58 (av1_estimate_bits_at_qindex), :66-69 (eval_block_bits),
//   :73-97 (find_closest_arg), :103-121 (rtc_compute_cr_deltaq),
//   :123-136 (rtc_cyclic_refresh_compute_cr_qdeltas),
//   :138-163 (av1_estimate_frame_size), :170-173 (eval_frame_size),
//   :175-184 (normalize_factors), :186-188 (index2tl),
//   :190-269 (calc_pframe_target_size), :272-298 (cr_select_sbs),
//   :300-360 (rtc_cyclic_refresh_init), :362-368 (get_rcf_index),
//   :370-381 (rtc_get_rate_correction_factor),
//   :383-400 (rtc_set_rate_correction_factor), :402-457 (calculate_qindex;
//   get_min_ref_base_q_idx :32-39 threads through PcsCarrier).
// PARKED (named): none in this span - the :402-457 svt_aom_cyclic_refresh_
//   setup call is the landed AQ4 body.
//
// FLOAT DISCIPLINE: the estimate doubles compare ABSOLUTE 1e-9 (sqrt and
// plain arithmetic - correctly rounded, platform-stable); the int outputs
// (frame sizes, targets, qindexes, deltas) pin EXACTLY; the (int) casts
// truncate toward zero. EXPECTATION DERIVATION: the python oracle parses
// the ac table and replicates the C loops; the normalize_factors weighting
// (k - i_start - 1) and the calc_pframe weight chain were hand-derived
// WRONG first (the oracle is the authority).

namespace {

// the RTC fixture rc/cfg pair: the CR arithmetic reads min/max qp allowed
// (the clamp), best/worst quality and min_ref_base_q_idx off it.
l15_rc::RcControl rtcRc() {
    l15_rc::RcControl rc{};
    rc.avgFrameBandwidth = 10000;
    rc.maxFrameBandwidth = 50000;
    rc.optimalBufferLevel = 10000000;
    rc.maximumBufferSize = 20000000;
    rc.worstQuality = 255;
    rc.bestQuality = 0;
    rc.avgFrameQindex[1] = 150;
    rc.avgFrameLowMotion = 60;
    rc.miniQopSize = 4;
    rc.rcNumLayers = 3;
    rc.framesSinceKey = 10;
    rc.minRefBaseQIdx = 100;
    for (int i = 0; i < 33; ++i) {
        rc.rcfValues[i] = 0.7;
        rc.rcfKalmanP[i] = 1.0;
        rc.rcfKalmanR[i] = 0.08;
    }
    for (int i = 0; i < 7; ++i) rc.targetSizeFactors[i] = 1.0;
    return rc;
}

l15_rc::RcConfig rtcCfg() {
    l15_rc::RcConfig cfg{};
    cfg.mode = l15_rc::kRcCbr;
    cfg.minQpAllowed = 0;
    cfg.maxQpAllowed = 63;
    return cfg;
}

l15_rc::PcsCarrier rtcPcs() {
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
    return pcs;
}

}  // namespace

TEST_CASE("estimate_bits_at_qindex keeps 5600 for KEY and the ref scale for INTER") {
    // KEY: complexity 5600, ref_scale 1.0: 0.7*5600/convert(255) =
    // 0.7*5600/457.
    CHECK(std::fabs(l15_rc::estimateBitsAtQindex(true, 0, 255, 0.7, 12345, 8) - 8.577680525164) <= 1e-9);
    // INTER: ref_scale = sqrt(ref_q)/q; complexity = max(me_dist, 1024).
    CHECK(std::fabs(l15_rc::estimateBitsAtQindex(false, 100, 100, 1.0, 5000, 8) - 33.746827947252) <= 1e-9);
    // me_dist below the 64*64/4 floor.
    CHECK(std::fabs(l15_rc::estimateBitsAtQindex(false, 100, 100, 1.0, 100, 8) - 6.911350363597) <= 1e-9);
    // a different ref/q/rcf triple.
    CHECK(std::fabs(l15_rc::estimateBitsAtQindex(false, 200, 50, 0.5, 20000, 8) - 622.430269486712) <= 1e-9);
}

TEST_CASE("find_closest_arg walks the monotone eval and picks the closer side") {
    const l15_rc::PcsCarrier pcs = rtcPcs();
    const auto eval = [](int q) {
        return l15_rc::estimateBitsAtQindex(false, 100, q, 1.0, 5000, 8);
    };
    // oracle: 50 -> 85, 10 -> 138, 1 -> 201, 0.001 -> 255, 1000 -> 14.
    CHECK(l15_rc::findClosestArg(50.0, 0, 255, eval) == 85);
    CHECK(l15_rc::findClosestArg(10.0, 0, 255, eval) == 138);
    CHECK(l15_rc::findClosestArg(1.0, 0, 255, eval) == 201);
    CHECK(l15_rc::findClosestArg(0.001, 0, 255, eval) == 255);
    CHECK(l15_rc::findClosestArg(1000.0, 0, 255, eval) == 14);
    // a single-point range returns it whatever the target.
    CHECK(l15_rc::findClosestArg(1000.0, 40, 40, eval) == 40);
}

TEST_CASE("rtc_compute_cr_deltaq saturates at max_qdelta_perc") {
    // seg1-like: ratio 2.5 on q=100 (me 5000, rcf 1.0, min_ref 100):
    // the oracle gives -36 (within the -60 cap).
    l15_rc::RcControl rc = rtcRc();
    l15_rc::RcConfig cfg = rtcCfg();
    l15_rc::CrRefreshCarrier cr{};
    cr.maxQdeltaPerc = 60;
    CHECK(l15_rc::rtcComputeCrDeltaq(cr, rc, cfg, false, 100, 1.0, 5000, 2.5, 8) == -36);
    // seg2-like: ratio 6.0 pushes past the cap -> -60.
    CHECK(l15_rc::rtcComputeCrDeltaq(cr, rc, cfg, false, 100, 1.0, 5000, 6.0, 8) == -60);
    // ratio < 1 walks the [qindex, worst] window instead: +22.
    CHECK(l15_rc::rtcComputeCrDeltaq(cr, rc, cfg, false, 100, 1.0, 5000, 0.5, 8) == 22);
}

TEST_CASE("rtc_cyclic_refresh_compute_cr_qdeltas fills the three deltas") {
    l15_rc::RcControl rc = rtcRc();
    l15_rc::RcConfig cfg = rtcCfg();
    l15_rc::CrRefreshCarrier cr{};
    cr.rateRatioQdelta = 2.864705882352941;
    cr.rateRatioQdeltaSeg2 = 5.225;
    cr.maxQdeltaPerc = 60;
    cr.actualNumSeg1Sbs = 4;
    cr.actualNumSeg2Sbs = 8;
    cr.meDistortion[1] = 5000;
    cr.meDistortion[2] = 5000;
    l15_rc::rtcCyclicRefreshComputeCrQdeltas(cr, rc, cfg, false, 100, 1.0, 8);
    CHECK(cr.qindexDelta[0] == 0);
    // seg1: qdelta = min(4.0, rate_ratio_qdelta 2.8647) -> the RTC2
    // arithmetic on me 5000 at THAT ratio -> -41 (oracle).
    CHECK(cr.qindexDelta[1] == -41);
    // seg2: qdelta = min(8.0, seg2 ratio 5.225) -> -58 (oracle).
    CHECK(cr.qindexDelta[2] == -58);

    // an empty segment leaves its delta at 0 (rc_rtc_cbr.c:128/:132).
    l15_rc::CrRefreshCarrier cr2{};
    cr2.rateRatioQdelta = 2.864705882352941;
    cr2.rateRatioQdeltaSeg2 = 5.225;
    cr2.maxQdeltaPerc = 60;
    cr2.actualNumSeg1Sbs = 0;
    cr2.actualNumSeg2Sbs = 0;
    l15_rc::rtcCyclicRefreshComputeCrQdeltas(cr2, rc, cfg, false, 100, 1.0, 8);
    CHECK(cr2.qindexDelta[1] == 0);
    CHECK(cr2.qindexDelta[2] == 0);
}

TEST_CASE("estimate_frame_size simple, weighted and the 512 scale") {
    l15_rc::RcControl rc = rtcRc();
    rc.curAvgBaseMeDist = 5000;  // the reference's simple-form me_dist
    l15_rc::RcConfig cfg = rtcCfg();
    l15_rc::CrRefreshCarrier cr{};
    // the simple form (apply 0): est(q, cur_me) * w * h / 512.
    CHECK(l15_rc::estimateFrameSize(rc, cfg, cr, false, 100, 0.7, 1920, 1080, 16, false, 8) == 95672);
    // the KEY form.
    CHECK(l15_rc::estimateFrameSize(rc, cfg, cr, true, 255, 0.7, 1920, 1080, 16, false, 8) == 34739);
    // the CR-weighted form: (1-w1-w2)*est(me0) + w1*est(q+d1, me1) +
    // w2*est(q+d2, me2) with w1 = 4/16, w2 = 8/16.
    cr.applyCyclicRefresh = 1;
    cr.actualNumSeg1Sbs = 4;
    cr.actualNumSeg2Sbs = 8;
    cr.qindexDelta[1] = -3;
    cr.qindexDelta[2] = -6;
    cr.meDistortion[0] = 1000;
    cr.meDistortion[1] = 2000;
    cr.meDistortion[2] = 3000;
    CHECK(l15_rc::estimateFrameSize(rc, cfg, cr, false, 100, 0.7, 1920, 1080, 16, false, 8) == 50873);
    // all-seg2 (w2 = 1).
    cr.actualNumSeg1Sbs = 0;
    cr.actualNumSeg2Sbs = 16;
    CHECK(l15_rc::estimateFrameSize(rc, cfg, cr, false, 100, 0.7, 1920, 1080, 16, false, 8) == 70587);
    // the AOMMAX(x, 1) floor.
    l15_rc::RcControl rc2 = rtcRc();
    l15_rc::CrRefreshCarrier cr2{};
    cr2.applyCyclicRefresh = 1;
    cr2.actualNumSeg1Sbs = 16;
    cr2.meDistortion[0] = 1;
    cr2.meDistortion[1] = 1;
    cr2.meDistortion[2] = 1;
    CHECK(l15_rc::estimateFrameSize(rc2, cfg, cr2, true, 0, 0.005, 2, 2, 16, false, 8) == 1);
    // calc_sb_qindex recomputes the deltas first (apply 1): with empty
    // segments the deltas stay 0 and the estimate is the seg0 form on
    // cr.me_distortion[0].
    l15_rc::CrRefreshCarrier cr3{};
    cr3.applyCyclicRefresh = 1;
    cr3.rateRatioQdelta = 2.864705882352941;
    cr3.rateRatioQdeltaSeg2 = 5.225;
    cr3.maxQdeltaPerc = 60;
    cr3.meDistortion[0] = 5000;
    CHECK(l15_rc::estimateFrameSize(rc, cfg, cr3, false, 100, 0.7, 1920, 1080, 16, true, 8) == 95672);
    CHECK(cr3.qindexDelta[1] == 0);
    CHECK(cr3.qindexDelta[2] == 0);
}

TEST_CASE("normalize_factors weights by the 2^k ladder and index2tl decays") {
    double src[5] = {0.0, 1.0, 2.0, 4.0, 8.0};
    double dst[5] = {0, 0, 0, 0, 0};
    // sum = 1*1 + 2*2 + 4*4 = 21? NO - the ladder is (k - i_start - 1):
    // 1*1 + 2*2 + 4*4 = 21 is wrong; the oracle: 1*1 + 2*2 + 4*2 = 11? No:
    // k=1: 1<<0; k=2: 1<<0; k=3: 1<<1 -> 1 + 2 + 8 = 11; avg = 11/4.
    l15_rc::normalizeFactors(dst, src, 1, 4);
    CHECK(std::fabs(dst[1] - 0.36363636363636365) <= 1e-9);
    CHECK(std::fabs(dst[2] - 0.7272727272727273) <= 1e-9);
    CHECK(std::fabs(dst[3] - 1.4545454545454546) <= 1e-9);
    CHECK(dst[0] == 0.0);
    CHECK(dst[4] == 0.0);
    // the in-place call (the reference's own shape).
    double inPlace[5] = {0.0, 1.0, 2.0, 4.0, 8.0};
    l15_rc::normalizeFactors(inPlace, inPlace, 1, 4);
    CHECK(std::fabs(inPlace[1] - 0.36363636363636365) <= 1e-9);

    // index2tl over indices 0..8 at levels 3 (oracle).
    const int want[9] = {0, 3, 2, 3, 1, 3, 2, 3, 0};
    for (int i = 0; i < 9; ++i) CHECK(l15_rc::index2tl(i, 3) == want[i]);
    // get_msb pins (floor log2).
    CHECK(l15_rc::getMsb(1) == 0);
    CHECK(l15_rc::getMsb(2) == 1);
    CHECK(l15_rc::getMsb(3) == 1);
    CHECK(l15_rc::getMsb(4) == 2);
    CHECK(l15_rc::getMsb(7) == 2);
    CHECK(l15_rc::getMsb(8) == 3);
    CHECK(l15_rc::getMsb(255) == 7);
}

TEST_CASE("calc_pframe_target_size_rtc weights, saturates and scales") {
    l15_rc::RcControl rc = rtcRc();
    rc.bufferLevel = 12000000;  // above optimal: the over-trim branch fires
    l15_rc::RcConfig cfg{};
    cfg.overShootPct = 50;
    cfg.underShootPct = 50;
    l15_rc::PcsCarrier pcs = rtcPcs();
    rc.rcMiniGopPos = 3;  // the weights gate: pos == mini_qop_size - 1
    rc.rcfValues[1] = 0.7;
    rc.rcfValues[2] = 0.8;
    rc.rcfValues[3] = 0.9;
    rc.rcfValues[4] = 1.0;
    // RANDOM_ACCESS (scale 0.5): the oracle target 24852 with the tsf
    // chain [2.764070320, 0.501149454, 0.367390113].
    CHECK(l15_rc::calcPframeTargetSizeRtc(rc, cfg, pcs, true) == 24852);
    CHECK(std::fabs(rc.targetSizeFactors[1] - 2.764070320) <= 1e-9);
    CHECK(std::fabs(rc.targetSizeFactors[2] - 0.501149454) <= 1e-9);
    CHECK(std::fabs(rc.targetSizeFactors[3] - 0.367390113) <= 1e-9);
    // LOW_DELAY (scale 1.0): 18992 with [2.111617603, 0.765708456,
    // 0.561336970].
    l15_rc::RcControl rcLd = rtcRc();
    rcLd.rcMiniGopPos = 3;
    rcLd.bufferLevel = 12000000;
    rcLd.rcfValues[1] = 0.7;
    rcLd.rcfValues[2] = 0.8;
    rcLd.rcfValues[3] = 0.9;
    rcLd.rcfValues[4] = 1.0;
    CHECK(l15_rc::calcPframeTargetSizeRtc(rcLd, cfg, pcs, false) == 18992);
    CHECK(std::fabs(rcLd.targetSizeFactors[1] - 2.111617603) <= 1e-9);
    CHECK(std::fabs(rcLd.targetSizeFactors[2] - 0.765708456) <= 1e-9);
    CHECK(std::fabs(rcLd.targetSizeFactors[3] - 0.561336970) <= 1e-9);

    // the gate closed (pos != size-1): the persisted target_size_factors
    // ride through; virtual_tl = index2tl(pos+1, layers-1) = 1 here ->
    // tsf[2] = 1.0 -> 10000 -> the buffer pct trims to 9000.
    l15_rc::RcControl rc2 = rtcRc();
    rc2.rcMiniGopPos = 1;
    rc2.bufferLevel = 12000000;
    rc2.targetSizeFactors[2] = 1.0;
    CHECK(l15_rc::calcPframeTargetSizeRtc(rc2, cfg, pcs, true) == 9000);

    // a mid buffer level keeps the weighted target (neither pct branch).
    l15_rc::RcControl rc3 = rtcRc();
    rc3.rcMiniGopPos = 3;
    rc3.rcfValues[1] = 0.7;
    rc3.rcfValues[2] = 0.8;
    rc3.rcfValues[3] = 0.9;
    rc3.rcfValues[4] = 1.0;
    rc3.bufferLevel = 1000000;
    CHECK(l15_rc::calcPframeTargetSizeRtc(rc3, cfg, pcs, true) == 27640);
}

TEST_CASE("cr_select_sbs advances and wraps the cycling index") {
    l15_rc::CrRefreshCarrier cr{};
    cr.percentRefresh = 20;
    std::uint32_t crSbIndex = 5;
    l15_rc::crSelectSbs(cr, crSbIndex, 16);
    CHECK(cr.sbStart == 5);
    CHECK(cr.sbEnd == 8);
    CHECK(crSbIndex == 8);

    // the wrap: 14 + 3 >= 16 -> end 1.
    std::uint32_t crSbIndex2 = 14;
    l15_rc::crSelectSbs(cr, crSbIndex2, 16);
    CHECK(cr.sbStart == 14);
    CHECK(cr.sbEnd == 1);
    CHECK(crSbIndex2 == 1);

    // an index past the frame wraps to 0 first.
    std::uint32_t crSbIndex3 = 20;
    l15_rc::crSelectSbs(cr, crSbIndex3, 16);
    CHECK(cr.sbStart == 0);
    CHECK(cr.sbEnd == 3);
    CHECK(crSbIndex3 == 3);

    // a zero target zeroes the band and keeps the index.
    cr.percentRefresh = 0;
    std::uint32_t crSbIndex4 = 5;
    l15_rc::crSelectSbs(cr, crSbIndex4, 16);
    CHECK(cr.sbStart == 0);
    CHECK(cr.sbEnd == 0);
    CHECK(crSbIndex4 == 5);
}

TEST_CASE("rtc_cyclic_refresh_init gates and derives the ratio") {
    l15_rc::RcControl rc = rtcRc();
    l15_rc::CrRefreshCarrier cr{};
    // the live fixture is a BASE-LAYER frame (tl 0): the is_inter_base_
    // layer gate (rc_rtc_cbr.c:305) requires temporal_layer_index == 0.
    l15_rc::PcsCarrier pcs = rtcPcs();
    pcs.temporalLayerIndex = 0;
    // the ratio derives from avg_frame_qindex[INTER]: the oracle fixture
    // uses 100 -> convert(100) = 28 -> 2.7 + 28/170.
    rc.avgFrameQindex[1] = 100;
    std::uint32_t crSbIndex = 5;
    l15_rc::rtcCyclicRefreshInit(rc, cr, pcs, l15_rc::kRcCbr, 64, crSbIndex, 16, 8);
    CHECK(cr.applyCyclicRefresh == 1);
    CHECK(cr.percentRefresh == 20);
    CHECK(cr.sbStart == 5);
    CHECK(cr.sbEnd == 8);
    // rate_ratio_qdelta = 2.7 + convert(100)/170 = 2.7 + 28/170.
    CHECK(std::fabs(cr.rateRatioQdelta - 2.864705882352941) <= 1e-9);
    CHECK(cr.rateBoostFac == 18);
    CHECK(cr.maxQdeltaPerc == 60);

    // the disable gates (each zeroes apply and skips the band selection).
    l15_rc::RcControl rc2 = rtcRc();
    l15_rc::CrRefreshCarrier cr2{};
    rc2.avgFrameLowMotion = 40;
    std::uint32_t crSbIndex2 = 5;
    l15_rc::rtcCyclicRefreshInit(rc2, cr2, pcs, l15_rc::kRcCbr, 64, crSbIndex2, 16, 8);
    CHECK(cr2.applyCyclicRefresh == 0);
    CHECK(cr2.sbStart == 0);
    CHECK(cr2.sbEnd == 0);

    // VBR mode: not CBR -> 0.
    l15_rc::RcControl rc3 = rtcRc();
    l15_rc::CrRefreshCarrier cr3{};
    std::uint32_t crSbIndex3 = 5;
    l15_rc::rtcCyclicRefreshInit(rc3, cr3, pcs, l15_rc::kRcVbr, 64, crSbIndex3, 16, 8);
    CHECK(cr3.applyCyclicRefresh == 0);

    // the leaf-frame gate (LF update).
    l15_rc::PcsCarrier pcsLeaf = rtcPcs();
    pcsLeaf.updateType = l15_rc::kSvtAv1LfUpdate;
    l15_rc::RcControl rc4 = rtcRc();
    l15_rc::CrRefreshCarrier cr4{};
    std::uint32_t crSbIndex4 = 5;
    l15_rc::rtcCyclicRefreshInit(rc4, cr4, pcsLeaf, l15_rc::kRcCbr, 64, crSbIndex4, 16, 8);
    CHECK(cr4.applyCyclicRefresh == 0);

    // super-block size 128.
    l15_rc::RcControl rc5 = rtcRc();
    l15_rc::CrRefreshCarrier cr5{};
    std::uint32_t crSbIndex5 = 5;
    l15_rc::rtcCyclicRefreshInit(rc5, cr5, pcs, l15_rc::kRcCbr, 128, crSbIndex5, 16, 8);
    CHECK(cr5.applyCyclicRefresh == 0);

    // the qp windows: avg_q_inter 250 > 235 and 10 < 16.
    l15_rc::RcControl rc6 = rtcRc();
    rc6.avgFrameQindex[1] = 250;
    l15_rc::CrRefreshCarrier cr6{};
    std::uint32_t crSbIndex6 = 5;
    l15_rc::rtcCyclicRefreshInit(rc6, cr6, pcs, l15_rc::kRcCbr, 64, crSbIndex6, 16, 8);
    CHECK(cr6.applyCyclicRefresh == 0);
    l15_rc::RcControl rc7 = rtcRc();
    rc7.avgFrameQindex[1] = 10;
    l15_rc::CrRefreshCarrier cr7{};
    std::uint32_t crSbIndex7 = 5;
    l15_rc::rtcCyclicRefreshInit(rc7, cr7, pcs, l15_rc::kRcCbr, 64, crSbIndex7, 16, 8);
    CHECK(cr7.applyCyclicRefresh == 0);

    // the I-slice gate (slice_type == I_SLICE).
    l15_rc::PcsCarrier pcsKey = rtcPcs();
    pcsKey.sliceType = l15_rc::kIslice;
    l15_rc::RcControl rc8 = rtcRc();
    l15_rc::CrRefreshCarrier cr8{};
    std::uint32_t crSbIndex8 = 5;
    l15_rc::rtcCyclicRefreshInit(rc8, cr8, pcsKey, l15_rc::kRcCbr, 64, crSbIndex8, 16, 8);
    CHECK(cr8.applyCyclicRefresh == 0);
}

TEST_CASE("rtc rcf index, getter (unclamped) and setter (clamped + reset)") {
    CHECK(l15_rc::getRcfIndex(true, 2, 4) == 0);
    CHECK(l15_rc::getRcfIndex(false, 2, 4) == 4);
    CHECK(l15_rc::getRcfIndex(false, 3, 4) == 1);

    l15_rc::RcControl rc = rtcRc();
    l15_rc::PcsCarrier pcs = rtcPcs();  // 1920x1080
    // the getter applies the res scale WITHOUT the BPB clamp
    // (rc_rtc_cbr.c:370-381 has no fclamp, unlike the vbr getter). The
    // index threads: mini_gop_pos 2 -> ((2+1)%4)+1 = 4.
    rc.rcMiniGopPos = 2;
    rc.rcfValues[4] = 100.0;
    CHECK(std::fabs(l15_rc::rtcGetRateCorrectionFactor(rc, pcs, 960, 540) - 400.0) <= 1e-9);

    // the setter: fclamp then divide by the scale; with
    // frames_since_key < mini_qop_size the pessimistic reset lifts the
    // whole 1..mini_qop_size range.
    l15_rc::RcControl rc2 = rtcRc();
    rc2.framesSinceKey = 10;
    l15_rc::rtcSetRateCorrectionFactor(rc2, pcs, 4.4, 960, 540);
    CHECK(std::fabs(rc2.rcfValues[l15_rc::getRcfIndex(false, rc2.rcMiniGopPos, 4)] - 1.1) <= 1e-9);
    CHECK(std::fabs(rc2.rcfValues[1] - 0.7) <= 1e-9);  // no reset (fsk >= size)

    l15_rc::RcControl rc3 = rtcRc();
    rc3.framesSinceKey = 2;
    l15_rc::rtcSetRateCorrectionFactor(rc3, pcs, 4.4, 960, 540);
    CHECK(std::fabs(rc3.rcfValues[1] - 1.1) <= 1e-9);
    CHECK(std::fabs(rc3.rcfValues[2] - 1.1) <= 1e-9);
    CHECK(std::fabs(rc3.rcfValues[4] - 1.1) <= 1e-9);

    // the BPB clamps inside the setter.
    l15_rc::RcControl rc4 = rtcRc();
    rc4.framesSinceKey = 10;
    l15_rc::rtcSetRateCorrectionFactor(rc4, pcs, 0.001, 1920, 1080);
    CHECK(std::fabs(rc4.rcfValues[l15_rc::getRcfIndex(false, rc4.rcMiniGopPos, 4)] - 0.005) <= 1e-9);
    l15_rc::RcControl rc5 = rtcRc();
    rc5.framesSinceKey = 10;
    l15_rc::rtcSetRateCorrectionFactor(rc5, pcs, 400.0, 1920, 1080);
    CHECK(std::fabs(rc5.rcfValues[l15_rc::getRcfIndex(false, rc5.rcMiniGopPos, 4)] - 50.0) <= 1e-9);
}

namespace {

l15_rc::RcControl cqRc() {
    l15_rc::RcControl rc = rtcRc();
    rc.bufferLevel = 12000000;
    rc.rcMiniGopPos = 3;
    rc.rcfValues[1] = 0.7;
    rc.rcfValues[2] = 0.7;
    rc.rcfValues[3] = 0.7;
    rc.rcfValues[4] = 0.7;
    rc.curAvgBaseMeDist = 5000;  // the eval's complexity input
    // CR off (avg_frame_low_motion < 50): the init zeroes apply, the
    // setup's band is empty (sb_start/sb_end 0), and estimate_frame_size
    // takes the simple form - the oracle pins assume exactly this.
    rc.avgFrameLowMotion = 40;
    return rc;
}

l15_rc::MeStatCarrier cqMe() {
    l15_rc::MeStatCarrier me{};
    static const std::uint32_t d8[1] = {0};
    static const std::uint32_t d64[1] = {0};
    static const std::int16_t mv[1] = {0};
    me.me8x8Distortion = d8;
    me.me64x64Distortion = d64;
    me.mvX = mv;
    me.mvY = mv;
    me.normMeDist = 0;
    me.b64TotalCount = 1;
    return me;
}

}  // namespace

TEST_CASE("calculate_qindex KEY keeps the tsf[0] target and pins at worst") {
    l15_rc::RcControl rc = cqRc();
    rc.targetSizeFactors[0] = 0.9;
    l15_rc::RcConfig cfg = rtcCfg();
    cfg.overShootPct = 50;
    cfg.underShootPct = 50;
    l15_rc::PcsCarrier pcs = rtcPcs();
    pcs.frameType = l15_rc::kKeyFrame;
    l15_rc::CrRefreshCarrier cr{};
    const l15_rc::MeStatCarrier me = cqMe();
    std::uint32_t crSbIndex = 5;
    // base = int(10000*0.9) = 9000 clamped to [max(625, 200),
    // min(50000, vbv 10000)] = 9000; the eval at 255 (34740) exceeds the
    // target -> the search pins at the worst bound 255.
    CHECK(l15_rc::calculateQindexRtc(rc, cfg, pcs, cr, me, crSbIndex, 16, 64, 32, true) == 255);
    CHECK(pcs.baseFrameTarget == 9000);
    CHECK(pcs.thisFrameTarget == 9000);
    CHECK(rc.framesToKey == 33);  // intra_period_length 32 + 1
}

TEST_CASE("calculate_qindex INTER gates the window on the ref q") {
    l15_rc::RcControl rc = cqRc();
    l15_rc::RcConfig cfg = rtcCfg();
    cfg.overShootPct = 50;
    cfg.underShootPct = 50;
    l15_rc::CrRefreshCarrier cr{};
    const l15_rc::MeStatCarrier me = cqMe();
    std::uint32_t crSbIndex = 5;
    // min_ref = min(120, 120) = 120; the non-I ref at tl=1/tmp=1 is not
    // higher-layer -> the window [104, 184]; the weighted target 24587
    // (avg_zeromv 40) lands on qindex 146 (oracle).
    l15_rc::PcsCarrier pcs = rtcPcs();
    CHECK(l15_rc::calculateQindexRtc(rc, cfg, pcs, cr, me, crSbIndex, 16, 64, 32, true) == 146);
    CHECK(pcs.baseFrameTarget == 24587);
    CHECK(pcs.thisFrameTarget == 24587);

    // the search target IS the clamped base target (rc_rtc_cbr.c:444), so
    // the window bounds are exercised through avg_frame_bandwidth: a huge
    // bandwidth inflates the weighted target past the eval maximum -> the
    // search pins at the LOWER window bound.
    l15_rc::RcControl rc2 = cqRc();
    rc2.avgFrameBandwidth = 1000000000;
    rc2.bufferLevel = 0;
    rc2.maximumBufferSize = 3000000000;
    rc2.maxFrameBandwidth = 2147483647;
    l15_rc::PcsCarrier pcs2 = rtcPcs();
    CHECK(l15_rc::calculateQindexRtc(rc2, cfg, pcs2, cr, me, crSbIndex, 16, 64, 32, true) == 104);
    // an I-slice reference: the window is [min_ref-4, worst] = [116, 255].
    l15_rc::RcControl rc3 = cqRc();
    rc3.avgFrameBandwidth = 1000000000;
    rc3.bufferLevel = 0;
    rc3.maximumBufferSize = 3000000000;
    rc3.maxFrameBandwidth = 2147483647;
    l15_rc::PcsCarrier pcs3 = rtcPcs();
    pcs3.ref0SliceType = l15_rc::kIslice;
    CHECK(l15_rc::calculateQindexRtc(rc3, cfg, pcs3, cr, me, crSbIndex, 16, 64, 32, true) == 116);
    // a higher-layer reference (tmp 0 < tl 2, hier != 0): [120, 248].
    l15_rc::RcControl rc4 = cqRc();
    rc4.avgFrameBandwidth = 1000000000;
    rc4.bufferLevel = 0;
    rc4.maximumBufferSize = 3000000000;
    rc4.maxFrameBandwidth = 2147483647;
    l15_rc::PcsCarrier pcs4 = rtcPcs();
    pcs4.temporalLayerIndex = 2;
    pcs4.ref0TmpLayer = 0;
    CHECK(l15_rc::calculateQindexRtc(rc4, cfg, pcs4, cr, me, crSbIndex, 16, 64, 32, true) == 120);

    // a tiny bandwidth deflates the target into the eval range -> the
    // search crosses at 178 in every window (the eval is quadratic in the
    // quantizer: ref_scale = sqrt(ref_q)/q divides by to_q TWICE; oracle).
    l15_rc::RcControl rc5 = cqRc();
    rc5.avgFrameBandwidth = 3200;
    l15_rc::PcsCarrier pcs5 = rtcPcs();
    CHECK(l15_rc::calculateQindexRtc(rc5, cfg, pcs5, cr, me, crSbIndex, 16, 64, 32, true) == 178);
    l15_rc::RcControl rc6 = cqRc();
    rc6.avgFrameBandwidth = 3200;
    l15_rc::PcsCarrier pcs6 = rtcPcs();
    pcs6.ref0SliceType = l15_rc::kIslice;
    CHECK(l15_rc::calculateQindexRtc(rc6, cfg, pcs6, cr, me, crSbIndex, 16, 64, 32, true) == 178);
    l15_rc::RcControl rc7 = cqRc();
    rc7.avgFrameBandwidth = 3200;
    l15_rc::PcsCarrier pcs7 = rtcPcs();
    pcs7.temporalLayerIndex = 2;
    pcs7.ref0TmpLayer = 0;
    CHECK(l15_rc::calculateQindexRtc(rc7, cfg, pcs7, cr, me, crSbIndex, 16, 64, 32, true) == 178);
}
