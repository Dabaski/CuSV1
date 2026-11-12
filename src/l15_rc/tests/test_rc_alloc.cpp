#include <doctest.h>
#include <cmath>
#include <cstdint>
#include <vector>

#include "rc_arith.h"

// RC2c Wave B: the one-pass rate allocation + dynamic resize family
// (rc_vbr_cbr.c:107-:589). Reference spans:
//   :107-163 (calc_active_worst_quality_no_stats_cbr),
//   :165-166 (max_delta_per_layer - landed with Wave A),
//   :168-205 (adjust_q_cbr), :208-211 (get_bits_per_mb),
//   :217-254 (find_closest_qindex_by_rate),
//   :256-278 (get_rate_correction_factor), :280-305 (set_rate_correction_
//   factor), :307-322 (av1_rc_regulate_q), :324-358 (svt_av1_resize_reset_
//   rc), :363-380 (set_gf_interval_update_onepass_rt),
//   :382-492 (dynamic_resize_one_pass_cbr), :497-516
//   (svt_aom_dynamic_resize_decision), :553-589 (svt_aom_one_pass_rt_rate_
//   alloc).
// PARKED (named): the rc->rc_mutex / frame_updated_mutex block/release
// pairs (the counters thread through as parameters).
//
// FLOAT DISCIPLINE: all outputs INTEGER and pinned exactly; the rcf doubles
// compared ABSOLUTE 1e-9 with fabs (0.7*0.85 = 0.5949999..., 0.7*0.8,
// 0.7*1.5 are within 1e-9 of the decimals). EXPECTATION DERIVATION: every
// pin was computed by the python oracle that parses the reference tables
// and replicates the C loops (two further hand-trace slips caught this way:
// the ac[165]=335 row indexing in adjust_q_cbr, and the avg-update-before-
// active-worst ordering inside resize_reset_rc).

namespace {

l15_rc::RcControl rcBase() {
    l15_rc::RcControl rc{};
    rc.avgFrameBandwidth = 10000;
    rc.maxFrameBandwidth = 50000;
    rc.optimalBufferLevel = 1000000;
    rc.startingBufferLevel = 2000000;
    rc.maximumBufferSize = 5000000;
    rc.worstQuality = 255;
    rc.bestQuality = 0;
    rc.avgFrameQindex[0] = 100;
    rc.avgFrameQindex[1] = 100;
    rc.lastQ[0] = 100;
    rc.lastQ[1] = 100;
    rc.q1Frame = 220;
    rc.prevAvgFrameBandwidth = 10000;
    rc.framesSinceKey = 8;
    for (int i = 0; i < 7; ++i) rc.rateCorrectionFactors[i] = 0.7;
    return rc;
}

l15_rc::RcConfig cfgBase() {
    l15_rc::RcConfig cfg{};
    cfg.mode = l15_rc::kRcCbr;
    cfg.underShootPct = 50;
    cfg.overShootPct = 40;
    return cfg;
}

l15_rc::PcsCarrier pcsBase() {
    l15_rc::PcsCarrier pcs{};
    pcs.frameType = l15_rc::kInterFrame;
    pcs.showableFrame = true;
    pcs.pictureNumber = 10;
    pcs.updateType = l15_rc::kSvtAv1LfUpdate;
    pcs.temporalLayerIndex = 0;
    pcs.hierarchicalLevels = 2;
    pcs.frameWidth = 1920;
    pcs.frameHeight = 1080;
    pcs.encoderBitDepth = 8;
    return pcs;
}

}  // namespace

TEST_CASE("calc_active_worst adjusts down above optimal and up below it") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::PcsCarrier pcs = pcsBase();

    rc.bufferLevel = 1500000;
    CHECK(l15_rc::calcActiveWorstQualityNoStatsCbr(rc, pcs, 2) == 120);
    rc.bufferLevel = 1100000;
    CHECK(l15_rc::calcActiveWorstQualityNoStatsCbr(rc, pcs, 2) == 124);
    // between critical (optimal>>3 = 125000) and optimal: ambient 100,
    // adjustment (255-100)*(1000000-500000)/875000 = 88 -> 188.
    rc.bufferLevel = 500000;
    CHECK(l15_rc::calcActiveWorstQualityNoStatsCbr(rc, pcs, 2) == 188);
    rc.bufferLevel = 999999;
    CHECK(l15_rc::calcActiveWorstQualityNoStatsCbr(rc, pcs, 2) == 100);
    rc.bufferLevel = 100000;  // below critical -> worst
    CHECK(l15_rc::calcActiveWorstQualityNoStatsCbr(rc, pcs, 2) == 255);
    // KEY_FRAME returns worst immediately (rc_vbr_cbr.c:121-123).
    pcs.frameType = l15_rc::kKeyFrame;
    rc.bufferLevel = 1500000;
    CHECK(l15_rc::calcActiveWorstQualityNoStatsCbr(rc, pcs, 2) == 255);
}

TEST_CASE("calc_active_worst ambient_qp selection by frame_updated") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::PcsCarrier pcs = pcsBase();
    rc.bufferLevel = 500000;
    // frame_updated >= 4: INTER only (100) -> 188.
    CHECK(l15_rc::calcActiveWorstQualityNoStatsCbr(rc, pcs, 4) == 188);
    // frame_updated < 4: min(INTER=100, KEY=50) = 50 ->
    // 50 + (255-50)*500000/875000 = 167.
    rc.avgFrameQindex[0] = 50;
    CHECK(l15_rc::calcActiveWorstQualityNoStatsCbr(rc, pcs, 2) == 167);
    // ambient clamps to worst: avg 300 -> 255; adjustment (0) -> 255.
    rc.avgFrameQindex[0] = 300;
    rc.avgFrameQindex[1] = 300;
    CHECK(l15_rc::calcActiveWorstQualityNoStatsCbr(rc, pcs, 4) == 255);
}

TEST_CASE("adjust_q_cbr tanh path and the q_1_frame clamp") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::PcsCarrier pcs = pcsBase();
    rc.prevAvgBaseMeDist = 100;
    rc.curAvgBaseMeDist = 50;
    rc.bufferLevel = 800000;
    // delta = 0.5 -> q_adj = 1+0.5*tanh(-2) = 0.5179862 ->
    // qdelta(159.75, 82.7487) = 165-200 = -35 -> q = 165; the clamp
    // (220-165 = 55, not > 60) does not fire -> 165.
    CHECK(l15_rc::adjustQcbr(rc, pcs, 200) == 165);
    // q_1_frame = 250: after the tanh q=165, 250-165 = 85 > mdd(60) -> 190.
    rc.q1Frame = 250;
    CHECK(l15_rc::adjustQcbr(rc, pcs, 200) == 190);
    // delta > 0: no tanh; the clamp does not fire (220-200 = 20 < 60).
    rc.q1Frame = 220;
    rc.prevAvgBaseMeDist = 50;
    rc.curAvgBaseMeDist = 100;
    CHECK(l15_rc::adjustQcbr(rc, pcs, 200) == 200);
    // the content gate requires q > worst/2: q=100 fails it; the clamp
    // (220-100 = 120 > 60) -> 160.
    rc.prevAvgBaseMeDist = 100;
    rc.curAvgBaseMeDist = 50;
    CHECK(l15_rc::adjustQcbr(rc, pcs, 100) == 160);
}

TEST_CASE("adjust_q_cbr gates skip both adjustments") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::PcsCarrier pcs = pcsBase();
    // bandwidth change: abs(10000-8000) = 2000 > 1000 -> skip.
    rc.prevAvgFrameBandwidth = 8000;
    CHECK(l15_rc::adjustQcbr(rc, pcs, 200) == 200);
    // frames_since_key <= 1 -> skip.
    rc.prevAvgFrameBandwidth = 10000;
    rc.framesSinceKey = 1;
    CHECK(l15_rc::adjustQcbr(rc, pcs, 200) == 200);
    // KEY frame skips the whole block; the final clamp still applies.
    rc.framesSinceKey = 8;
    pcs.frameType = l15_rc::kKeyFrame;
    CHECK(l15_rc::adjustQcbr(rc, pcs, 100) == 100);
    // the max_delta_down divisor halves under sc_class1: q=40, q_1_frame=90
    // (non-sc: max(1, 30) = 30 -> 60; sc: max(1, 45) = 45 -> 45).
    pcs.frameType = l15_rc::kInterFrame;
    rc.q1Frame = 90;
    CHECK(l15_rc::adjustQcbr(rc, pcs, 40) == 60);
    pcs.scClass1 = true;
    CHECK(l15_rc::adjustQcbr(rc, pcs, 40) == 45);
}

TEST_CASE("get_bits_per_mb threads frame type and sc_class1") {
    l15_rc::PcsCarrier pcs = pcsBase();
    pcs.encoderBitDepth = 8;
    // ac[1]=8 -> q=2.0; cf 0.5. The enumerator quadrants
    // (rc_process.c:258-262): KEY non-sc 1400000, INTER non-sc 1000000,
    // KEY sc 1000000, INTER sc 750000.
    CHECK(l15_rc::getBitsPerMb(pcs, 0.5, 1) == 250000);  // INTER, sc=0
    pcs.scClass1 = true;
    CHECK(l15_rc::getBitsPerMb(pcs, 0.5, 1) == 187500);  // INTER, sc=1
    pcs.frameType = l15_rc::kKeyFrame;
    CHECK(l15_rc::getBitsPerMb(pcs, 0.5, 1) == 250000);  // KEY, sc=1
}

TEST_CASE("find_closest_qindex_by_rate picks the nearer of curr and prev") {
    l15_rc::PcsCarrier pcs = pcsBase();
    // cf 0.7 INTER: bits(q) = int(2800000/ac[q]).
    // desired 20000: first under is 115 (bits 19718, diff 282); prev 114
    // (ac=140, bits 20000, diff 0) -> prev wins.
    CHECK(l15_rc::findClosestQindexByRate(20000, pcs, 0.7, 0, 255) == 114);
    // desired 19719: curr 115 diff 1, prev diff 281 -> curr wins.
    CHECK(l15_rc::findClosestQindexByRate(19719, pcs, 0.7, 0, 255) == 115);
    // window [120, 255]: first under 10000 is 155 (ac=280, exact hit).
    CHECK(l15_rc::findClosestQindexByRate(10000, pcs, 0.7, 120, 255) == 155);
    // desired 18421 = bits(120) exactly: curr=120 diff 0 -> curr wins.
    CHECK(l15_rc::findClosestQindexByRate(18421, pcs, 0.7, 0, 255) == 120);
    // nothing under: the search pins at worst and both diffs are INT_MAX.
    CHECK(l15_rc::findClosestQindexByRate(1000, pcs, 0.7, 0, 255) == 255);
}

TEST_CASE("rate correction factor levels and the resolution scale") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::RcConfig cfg = cfgBase();
    l15_rc::PcsCarrier pcs = pcsBase();
    rc.rateCorrectionFactors[0] = 0.7;
    rc.rateCorrectionFactors[3] = 1.1;
    rc.rateCorrectionFactors[4] = 1.2;
    rc.rateCorrectionFactors[5] = 1.3;

    // VBR: KEY -> level 0; INTER tl=2 -> level 3.
    cfg.mode = l15_rc::kRcVbr;
    pcs.temporalLayerIndex = 2;
    CHECK(std::fabs(l15_rc::getRateCorrectionFactor(rc, cfg, pcs, 960, 540) - 4.4) <= 1e-9);
    pcs.frameType = l15_rc::kKeyFrame;
    CHECK(std::fabs(l15_rc::getRateCorrectionFactor(rc, cfg, pcs, 1920, 1080) - 0.7) <= 1e-9);
    // CBR: KEY -> KF_STD (5); INTER GF -> the mode != CBR gate fails ->
    // INTER_NORMAL (0).
    cfg.mode = l15_rc::kRcCbr;
    CHECK(std::fabs(l15_rc::getRateCorrectionFactor(rc, cfg, pcs, 1920, 1080) - 1.3) <= 1e-9);
    pcs.frameType = l15_rc::kInterFrame;
    pcs.updateType = l15_rc::kSvtAv1GfUpdate;
    CHECK(std::fabs(l15_rc::getRateCorrectionFactor(rc, cfg, pcs, 1920, 1080) - 0.7) <= 1e-9);
    // Q: INTER GF overlay -> INTER_NORMAL; ARF not overlay -> GF_ARF_STD.
    cfg.mode = l15_rc::kRcQ;
    pcs.isOverlay = true;
    CHECK(std::fabs(l15_rc::getRateCorrectionFactor(rc, cfg, pcs, 1920, 1080) - 0.7) <= 1e-9);
    pcs.isOverlay = false;
    pcs.updateType = l15_rc::kSvtAv1ArfUpdate;
    CHECK(std::fabs(l15_rc::getRateCorrectionFactor(rc, cfg, pcs, 1920, 1080) - 1.2) <= 1e-9);
    // the BPB clamps.
    rc.rateCorrectionFactors[0] = 0.0001;
    pcs.updateType = l15_rc::kSvtAv1LfUpdate;
    CHECK(std::fabs(l15_rc::getRateCorrectionFactor(rc, cfg, pcs, 1920, 1080) - 0.005) <= 1e-9);
    rc.rateCorrectionFactors[0] = 100.0;
    CHECK(std::fabs(l15_rc::getRateCorrectionFactor(rc, cfg, pcs, 1920, 1080) - 50.0) <= 1e-9);
}

TEST_CASE("set_rate_correction_factor normalizes, clamps and stores") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::RcConfig cfg = cfgBase();
    l15_rc::PcsCarrier pcs = pcsBase();
    cfg.mode = l15_rc::kRcVbr;
    pcs.temporalLayerIndex = 2;  // VBR level 3
    // factor 4.4 with scale 4 -> 1.1 stored at level 3; the round trip
    // through get returns 4.4 again.
    l15_rc::setRateCorrectionFactor(rc, cfg, pcs, 4.4, 960, 540);
    CHECK(std::fabs(rc.rateCorrectionFactors[3] - 1.1) <= 1e-9);
    CHECK(std::fabs(l15_rc::getRateCorrectionFactor(rc, cfg, pcs, 960, 540) - 4.4) <= 1e-9);
    // the MAX_BPB clamp on the stored value.
    l15_rc::setRateCorrectionFactor(rc, cfg, pcs, 500.0, 1920, 1080);
    CHECK(std::fabs(rc.rateCorrectionFactors[3] - 50.0) <= 1e-9);
}

TEST_CASE("av1_rc_regulate_q VBR path and the CBR adjust chain") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::RcConfig cfg = cfgBase();
    l15_rc::PcsCarrier pcs = pcsBase();
    pcs.frameWidth = 64;
    pcs.frameHeight = 64;
    pcs.thisFrameTarget = 9550;
    rc.bufferLevel = 900000;
    rc.framesSinceKey = 8;
    // MBs = ((64+15)/16)^2 = 16; target_bits_per_mb = (9550 << 9)/16 =
    // 305600; cf = 0.7 (scale 64x64/64x64). bits(q) = int(2800000/ac[q]):
    // the first q <= 305600 is 3 (ac=10 -> 280000, diff 25600), but the
    // closer-previous logic prefers 2 (ac=9 -> 311111, diff 5511) -> 2.
    cfg.mode = l15_rc::kRcVbr;
    CHECK(l15_rc::av1RcRegulateQ(rc, cfg, pcs, 0, 255, 64, 64) == 2);
    // CBR: adjust_q_cbr then clamps: 220-2 = 218 > mdd(60) -> 160.
    cfg.mode = l15_rc::kRcCbr;
    CHECK(l15_rc::av1RcRegulateQ(rc, cfg, pcs, 0, 255, 64, 64) == 160);
}

TEST_CASE("resize_reset_rc drives the three scale branches") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::RcConfig cfg = cfgBase();
    l15_rc::PcsCarrier pcs = pcsBase();
    rc.avgFrameQindex[1] = 250;
    rc.bufferLevel = 500000;
    rc.framesSinceKey = 8;
    // down 0.25: qindex 250 > 229 -> rcf[0] *= 0.85 -> 0.595; the buffer
    // resets to optimal; the target resets to the pframe calc (10000 at
    // buffer == optimal). NOTE: svt_aom_reset_update_frame_target
    // (rc_vbr_cbr.c:97-104) writes ONLY this_frame_target -
    // base_frame_target is untouched by the reference.
    pcs.baseFrameTarget = 777;
    l15_rc::resizeResetRc(rc, cfg, pcs, 960, 540, 1920, 1080, 4);
    CHECK(std::fabs(rc.rateCorrectionFactors[0] - 0.595) <= 1e-9);
    CHECK(rc.bufferLevel == 1000000);
    CHECK(pcs.thisFrameTarget == 10000);
    CHECK(pcs.baseFrameTarget == 777);  // not written by the reference
    CHECK(rc.avgFrameQindex[1] == 250);  // tot < 1.0: no avg update

    // up exactly 4.0: the avg folds toward worst ((100+255)>>1 = 177);
    // the regulate's adjust clamps q to 220-60 = 160, and neither rcf
    // branch fires (tot not < 4.0; 160 > 120 fails the 1.5 arm's <=).
    l15_rc::RcControl rc2 = rcBase();
    rc2.avgFrameQindex[1] = 100;
    rc2.bufferLevel = 1000000;
    l15_rc::PcsCarrier pcs2 = pcsBase();
    l15_rc::resizeResetRc(rc2, cfg, pcs2, 1920, 1080, 960, 540, 0);
    CHECK(rc2.avgFrameQindex[1] == 177);
    CHECK(std::fabs(rc2.rateCorrectionFactors[0] - 0.7) <= 1e-9);

    // mid 2.25 with last_q=100: the clamped qindex 160 > 130 -> 0.8.
    l15_rc::RcControl rc3 = rcBase();
    rc3.avgFrameQindex[1] = 100;
    rc3.bufferLevel = 1000000;
    rc3.lastQ[1] = 100;
    l15_rc::PcsCarrier pcs3 = pcsBase();
    l15_rc::resizeResetRc(rc3, cfg, pcs3, 1920, 1080, 1280, 720, 0);
    CHECK(std::fabs(rc3.rateCorrectionFactors[0] - 0.56) <= 1e-9);

    // up 4.0 with last_q=150: the clamped qindex 160 <= 180 -> rcf[0]
    // *= 1.5.
    l15_rc::RcControl rc4 = rcBase();
    rc4.avgFrameQindex[1] = 100;
    rc4.bufferLevel = 1000000;
    rc4.lastQ[1] = 150;
    l15_rc::PcsCarrier pcs4 = pcsBase();
    l15_rc::resizeResetRc(rc4, cfg, pcs4, 1920, 1080, 960, 540, 0);
    CHECK(std::fabs(rc4.rateCorrectionFactors[0] - 1.05) <= 1e-9);

    // huge up 16: avg[INTER] = worst directly.
    l15_rc::RcControl rc5 = rcBase();
    rc5.avgFrameQindex[1] = 100;
    rc5.bufferLevel = 1000000;
    l15_rc::PcsCarrier pcs5 = pcsBase();
    l15_rc::resizeResetRc(rc5, cfg, pcs5, 3840, 2160, 960, 540, 0);
    CHECK(rc5.avgFrameQindex[1] == 255);
}

TEST_CASE("set_gf_interval_update_onepass_rt gates on the frame offset") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::PcsCarrier pcs = pcsBase();
    rc.framesToKey = 50;
    pcs.frameOffset = 32;
    CHECK(l15_rc::setGfIntervalUpdateOnepassRt(rc, pcs));
    CHECK(rc.baselineGfInterval == 32);
    CHECK(rc.gfuBoost == l15_rc::kDefaultGfBoostRt);
    CHECK(rc.constrainedGfGroup == 0);

    rc.framesToKey = 10;
    CHECK(l15_rc::setGfIntervalUpdateOnepassRt(rc, pcs));
    CHECK(rc.baselineGfInterval == 10);
    CHECK(rc.constrainedGfGroup == 1);

    pcs.frameOffset = 33;
    rc.baselineGfInterval = 7;
    rc.gfuBoost = 0;
    CHECK_FALSE(l15_rc::setGfIntervalUpdateOnepassRt(rc, pcs));
    CHECK(rc.baselineGfInterval == 7);
    CHECK(rc.gfuBoost == 0);
}

namespace {

// the dyn-resize fixture: 1920x1080 source, window 90 at 30 fps.
l15_rc::RcControl drRc() {
    l15_rc::RcControl rc = rcBase();
    rc.bufferLevel = 0;
    rc.lastQ[1] = 50;
    rc.framesSinceKey = 100;
    return rc;
}

void runDyn(l15_rc::RcControl& rc, l15_rc::PcsCarrier& pcs, bool oneHalfOnly, double fr, bool rtc, int frames,
            int frameUpdated) {
    for (int i = 0; i < frames; ++i)
        l15_rc::dynamicResizeOnePassCbr(rc, cfgBase(), pcs, oneHalfOnly, fr, rtc, 1920, 1080, frameUpdated);
}

}  // namespace

TEST_CASE("dynamic_resize down ORIG to ONE_HALF on sustained underflow") {
    l15_rc::RcControl rc = drRc();
    l15_rc::PcsCarrier pcs = pcsBase();
    runDyn(rc, pcs, true, 30.0, false, 90, 4);
    // window 90 reached with underflow 90 > 22 -> ORIG -> ONE_HALF
    // (one_half_only); the Step-4 reset then runs on (960x540, 1920x1080):
    // buffer resets to optimal and the target recomputes (10000 at
    // buffer == optimal); the rcf branch needs avg INTER >= 148-ish, so it
    // does not fire here (qindex 100 <= 229).
    CHECK(rc.resizeState == l15_rc::kResizeOneHalf);
    CHECK(rc.resizeCount == 0);
    CHECK(rc.resizeAvgQp == 0);
    CHECK(rc.resizeBufferUnderflow == 0);
    CHECK(rc.bufferLevel == 1000000);
    CHECK(pcs.thisFrameTarget == 10000);
    CHECK(std::fabs(rc.rateCorrectionFactors[0] - 0.7) <= 1e-9);
    CHECK(rc.avgFrameQindex[1] == 100);
}

TEST_CASE("dynamic_resize down fires the 0.85 rcf branch with high avg q") {
    l15_rc::RcControl rc = drRc();
    rc.avgFrameQindex[1] = 250;
    l15_rc::PcsCarrier pcs = pcsBase();
    runDyn(rc, pcs, true, 30.0, false, 90, 4);
    // the same down transition; the reset's regulate returns 250
    // (nothing in [0,250] reaches 2509 bits/mb) -> 250 > 229 -> 0.7*0.85.
    CHECK(rc.resizeState == l15_rc::kResizeOneHalf);
    CHECK(std::fabs(rc.rateCorrectionFactors[0] - 0.595) <= 1e-9);
    CHECK(rc.avgFrameQindex[1] == 250);  // tot 0.25 < 1.0: no avg update
}

TEST_CASE("dynamic_resize up ONE_HALF to ORIG on low average qp") {
    l15_rc::RcControl rc = drRc();
    rc.resizeState = l15_rc::kResizeOneHalf;
    rc.bufferLevel = 5000000;  // no underflow (5000000 >= 300000)
    l15_rc::PcsCarrier pcs = pcsBase();
    runDyn(rc, pcs, true, 30.0, false, 90, 4);
    // avg_qp = 50 < 178 -> the up branch; one_half_only forces UP_ORIG.
    // The Step-4 reset runs on (1920x1080, 960x540): tot = 4.0 -> the avg
    // folds (100+255)>>1 = 177; neither rcf branch fires (177 > 60 fails
    // the 1.5 arm's <=).
    CHECK(rc.resizeState == l15_rc::kResizeOrig);
    CHECK(rc.avgFrameQindex[1] == 177);
    CHECK(rc.bufferLevel == 1000000);
    CHECK(std::fabs(rc.rateCorrectionFactors[0] - 0.7) <= 1e-9);
}

TEST_CASE("dynamic_resize three-quarter ladder with one_half_only false") {
    l15_rc::RcControl rc = drRc();
    l15_rc::PcsCarrier pcs = pcsBase();
    runDyn(rc, pcs, false, 30.0, false, 90, 4);
    CHECK(rc.resizeState == l15_rc::kResizeThreeQuarter);
    l15_rc::RcControl rc2 = drRc();
    rc2.resizeState = l15_rc::kResizeThreeQuarter;
    l15_rc::PcsCarrier pcs2 = pcsBase();
    runDyn(rc2, pcs2, false, 30.0, false, 90, 4);
    CHECK(rc2.resizeState == l15_rc::kResizeOneHalf);
}

TEST_CASE("dynamic_resize gates: key frame, warmup, scene change, window") {
    l15_rc::RcControl rc = drRc();
    rc.resizeAvgQp = 999;
    rc.resizeCount = 50;
    rc.resizeBufferUnderflow = 50;
    l15_rc::PcsCarrier pcs = pcsBase();
    pcs.frameType = l15_rc::kKeyFrame;
    runDyn(rc, pcs, true, 30.0, false, 1, 4);
    // the key frame resets the counters and returns.
    CHECK(rc.resizeAvgQp == 0);
    CHECK(rc.resizeCount == 0);
    CHECK(rc.resizeBufferUnderflow == 0);
    CHECK(rc.resizeState == l15_rc::kResizeOrig);

    // frames_since_key <= framerate: no accumulation.
    l15_rc::RcControl rc2 = drRc();
    rc2.framesSinceKey = 30;
    l15_rc::PcsCarrier pcs2 = pcsBase();
    runDyn(rc2, pcs2, true, 30.0, false, 5, 4);
    CHECK(rc2.resizeCount == 0);

    // scene change: no accumulation.
    l15_rc::RcControl rc3 = drRc();
    l15_rc::PcsCarrier pcs3 = pcsBase();
    pcs3.sceneChangeFlag = true;
    runDyn(rc3, pcs3, true, 30.0, false, 5, 4);
    CHECK(rc3.resizeCount == 0);

    // window not reached (framerate 10 -> window 60; 59 frames).
    l15_rc::RcControl rc4 = drRc();
    l15_rc::PcsCarrier pcs4 = pcsBase();
    runDyn(rc4, pcs4, true, 10.0, false, 59, 4);
    CHECK(rc4.resizeCount == 59);
    CHECK(rc4.resizeState == l15_rc::kResizeOrig);

    // a frame below 213x120 disables the down branch (rc_vbr_cbr.c:406-408
    // vs :390-392): the window still fires (the counters reset) but the
    // state never leaves ORIG.
    l15_rc::RcControl rc5 = drRc();
    l15_rc::PcsCarrier pcs5 = pcsBase();
    pcs5.frameWidth = 192;
    pcs5.frameHeight = 108;
    runDyn(rc5, pcs5, true, 30.0, false, 90, 4);
    CHECK(rc5.resizeState == l15_rc::kResizeOrig);
    CHECK(rc5.resizeCount == 0);
}

TEST_CASE("dynamic_resize rtc uses the inverted underflow predicate") {
    // rtc=1: buffer 4000000 > 5000000 - 1200000 = 3800000 -> underflow ->
    // the down transition fires.
    l15_rc::RcControl rc = drRc();
    rc.bufferLevel = 4000000;
    l15_rc::PcsCarrier pcs = pcsBase();
    runDyn(rc, pcs, true, 30.0, true, 90, 4);
    CHECK(rc.resizeState == l15_rc::kResizeOneHalf);

    // rtc=0: 4000000 < 300000 is false -> no underflow -> no down; the up
    // branch cannot fire from ORIG; the counters reset at the window.
    l15_rc::RcControl rc2 = drRc();
    rc2.bufferLevel = 4000000;
    l15_rc::PcsCarrier pcs2 = pcsBase();
    runDyn(rc2, pcs2, true, 30.0, false, 90, 4);
    CHECK(rc2.resizeState == l15_rc::kResizeOrig);
    CHECK(rc2.resizeCount == 0);
}

TEST_CASE("dynamic_resize_decision publishes the pending denominator") {
    l15_rc::RcControl rc = drRc();
    l15_rc::PcsCarrier pcs = pcsBase();
    l15_rc::ResizePendingCarrier pending{};
    pending.resizeState = l15_rc::kResizeOrig;
    pending.resizeDenom = l15_rc::kScaleNumerator;
    // 90 frames of underflow -> ONE_HALF -> denom = SCALE_DENOMINATOR_MAX.
    for (int i = 0; i < 90; ++i)
        l15_rc::dynamicResizeDecision(rc, cfgBase(), pcs, 30.0, false, 1920, 1080, 4, pending);
    CHECK(rc.resizeState == l15_rc::kResizeOneHalf);
    CHECK(pending.resizeState == l15_rc::kResizeOneHalf);
    CHECK(pending.resizeDenom == l15_rc::kScaleDenominatorMax);
    CHECK(rc.bufferLevel == 1000000);  // the Step-4 reset ran
    // The second window has NO underflow (the buffer was reset to
    // optimal): the low avg_qp (50 < 127) drives the UP transition back to
    // ORIG; the up-reset folds avg INTER to (100+255)>>1 = 177 and
    // publishes SCALE_NUMERATOR again.
    for (int i = 0; i < 90; ++i)
        l15_rc::dynamicResizeDecision(rc, cfgBase(), pcs, 30.0, false, 1920, 1080, 4, pending);
    CHECK(rc.resizeState == l15_rc::kResizeOrig);
    CHECK(pending.resizeState == l15_rc::kResizeOrig);
    CHECK(pending.resizeDenom == l15_rc::kScaleNumerator);
    CHECK(rc.avgFrameQindex[1] == 177);
    CHECK(std::fabs(rc.rateCorrectionFactors[0] - 0.7) <= 1e-9);
}

namespace {

l15_rc::RcControl allocRc() {
    l15_rc::RcControl rc = rcBase();
    rc.bufferLevel = 900000;
    rc.framesToKey = 50;
    return rc;
}

}  // namespace

TEST_CASE("one_pass_rt_rate_alloc INTER frame: gf interval and the target") {
    l15_rc::RcControl rc = allocRc();
    l15_rc::PcsCarrier pcs = pcsBase();
    pcs.frameOffset = 32;
    l15_rc::ResizePendingCarrier pending{};
    l15_rc::onePassRtRateAlloc(rc, cfgBase(), pcs, 30.0, false, false, false, false, false, 32, 1920, 1080, 4,
                               pending);
    CHECK(rc.baselineGfInterval == 32);
    CHECK(rc.gfuBoost == l15_rc::kDefaultGfBoostRt);
    CHECK(rc.constrainedGfGroup == 0);
    // the pframe target at buffer 900000 (the R2 fixture arithmetic).
    CHECK(pcs.thisFrameTarget == 9550);
    CHECK(pcs.baseFrameTarget == 9550);
}

TEST_CASE("one_pass_rt_rate_alloc KEY frame: boost, forced flag, iframe target") {
    l15_rc::RcControl rc = allocRc();
    l15_rc::PcsCarrier pcs = pcsBase();
    pcs.frameType = l15_rc::kKeyFrame;
    pcs.pictureNumber = 0;
    pcs.frameOffset = 1;
    l15_rc::ResizePendingCarrier pending{};
    l15_rc::onePassRtRateAlloc(rc, cfgBase(), pcs, 30.0, false, false, false, false, false, 32, 1920, 1080, 4,
                               pending);
    CHECK(rc.kfBoost == l15_rc::kDefaultKfBoostRt);
    CHECK(rc.thisKeyFrameForced == 0);
    CHECK(rc.framesToKey == 33);
    // starting/2 = 1000000 clamped by max_frame_bandwidth 50000.
    CHECK(pcs.thisFrameTarget == 50000);
    CHECK(pcs.baseFrameTarget == 50000);

    // a later key with frames_to_key == 0 marks the forced flag.
    l15_rc::RcControl rc2 = allocRc();
    rc2.framesToKey = 0;
    l15_rc::PcsCarrier pcs2 = pcsBase();
    pcs2.frameType = l15_rc::kKeyFrame;
    pcs2.pictureNumber = 10;
    l15_rc::onePassRtRateAlloc(rc2, cfgBase(), pcs2, 30.0, false, false, false, false, false, 32, 1920, 1080, 4,
                               pending);
    CHECK(rc2.thisKeyFrameForced == 1);
    CHECK(rc2.framesToKey == 33);
}

TEST_CASE("one_pass_rt_rate_alloc reset-flag path drives resize_reset_rc") {
    l15_rc::RcControl rc = allocRc();
    rc.avgFrameQindex[1] = 250;
    rc.bufferLevel = 500000;
    l15_rc::PcsCarrier pcs = pcsBase();
    pcs.frameOffset = 1;
    pcs.renderWidth = 960;
    pcs.renderHeight = 540;
    pcs.rcResetFlag = true;
    l15_rc::ResizePendingCarrier pending{};
    l15_rc::onePassRtRateAlloc(rc, cfgBase(), pcs, 30.0, false, false, false, false, true, 32, 1920, 1080, 4,
                               pending);
    // the reset (960x540 from 1920x1080): qindex 250 > 229 -> 0.7*0.85;
    // the buffer resets and the target recomputes at buffer == optimal.
    CHECK(std::fabs(rc.rateCorrectionFactors[0] - 0.595) <= 1e-9);
    CHECK(rc.bufferLevel == 1000000);
    CHECK(pcs.thisFrameTarget == 10000);
    CHECK(pcs.baseFrameTarget == 10000);
}

TEST_CASE("one_pass_rt_rate_alloc routes the resize-gated decision") {
    l15_rc::RcControl rc = allocRc();
    rc.bufferLevel = 0;      // sustained underflow: 0 < 30*optimal/100
    rc.framesSinceKey = 100; // the dyn-resize gate: fsk > framerate(30)
    l15_rc::PcsCarrier pcs = pcsBase();
    pcs.frameOffset = 1;
    // the encoder's pending params start at ORIG/SCALE_NUMERATOR; the
    // reference only writes the denom inside the change branch
    // (rc_vbr_cbr.c:504-515), so a no-change window leaves it untouched.
    l15_rc::ResizePendingCarrier pending{};
    pending.resizeState = l15_rc::kResizeOrig;
    pending.resizeDenom = l15_rc::kScaleNumerator;
    // the decision runs ONCE per alloc call: a single call accumulates one
    // window sample and the pending stays at ORIG.
    l15_rc::onePassRtRateAlloc(rc, cfgBase(), pcs, 30.0, false, true, true, true, false, 32, 1920, 1080, 4,
                               pending);
    CHECK(rc.resizeCount == 1);
    CHECK(rc.resizeState == l15_rc::kResizeOrig);
    CHECK(pending.resizeState == l15_rc::kResizeOrig);
    CHECK(pending.resizeDenom == l15_rc::kScaleNumerator);
    // 90 alloc calls (one per encoded frame) reach the window: the down
    // transition publishes ONE_HALF / SCALE_DENOMINATOR_MAX through the
    // pending carrier.
    for (int i = 0; i < 89; ++i)
        l15_rc::onePassRtRateAlloc(rc, cfgBase(), pcs, 30.0, false, true, true, true, false, 32, 1920, 1080, 4,
                                   pending);
    CHECK(rc.resizeState == l15_rc::kResizeOneHalf);
    CHECK(pending.resizeState == l15_rc::kResizeOneHalf);
    CHECK(pending.resizeDenom == l15_rc::kScaleDenominatorMax);
}
