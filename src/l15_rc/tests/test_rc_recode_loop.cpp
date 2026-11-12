#include <doctest.h>
#include <cmath>
#include <cstdint>

#include "rc_arith.h"

// RC2 Wave D2: the recode-loop driver. Reference span: rc_vbr_cbr.c
// :1794-1926 (recode_loop_update_q).
//
// FLOAT DISCIPLINE: every output here is INTEGER and pinned exactly; the
// rcf trajectory pin is a double at fabs 1e-9.
//
// EXPECTATION DERIVATION: oracle-derived (the python oracle replicates the
// whole driver over the landed primitives: the closed-form
// rc_bits_per_mb, the ac_qlookup, the D3 updater, the D3 regulated retry
// loops, the D1 bounds/test/find_qindex helpers). The dummy-pack
// projected value is (pcs_total_rate + 256) >> 9 (+13 on KEY) - the
// oracle's 195 for pcs_total_rate 100000 corrected a hand-calc 196.
//
// PARK (named): capped_crf_reencode (rc_crf_cbr... rc_crf_cqp.c, OUT of
// this file) is dropped at the AOM_Q + max_bit_rate + tl==0 branch - the
// q mutation it would perform is absent, and the TC-C case pins exactly
// that (q stays 100 through the parked branch; the TAIL tpl search still
// runs and is pinned).

namespace {

l15_rc::RcControl d2Rc() {
    l15_rc::RcControl rc{};
    for (int i = 0; i < 7; ++i) rc.rateCorrectionFactors[i] = 0.7;
    rc.q1Frame = 100;
    rc.q2Frame = 90;
    rc.rc1Frame = 0;
    rc.rc2Frame = 1;
    rc.percentRefreshAdjustment = 0;
    rc.rateRatioQdeltaAdjustment = 0.0;
    rc.worstQuality = 255;
    rc.bestQuality = 0;
    rc.maxFrameBandwidth = 50000;
    rc.activeWorstQuality = 200;
    rc.kfBoost = 4500;
    rc.gfuBoost = 1500;
    rc.avgFrameLowMotion = 40;
    return rc;
}

l15_rc::RcConfig d2Cfg() {
    l15_rc::RcConfig cfg{};
    cfg.mode = l15_rc::kRcVbr;
    cfg.worstAllowedQ = 255;
    cfg.bestAllowedQ = 0;
    cfg.minQpAllowed = 0;
    cfg.maxQpAllowed = 63;
    cfg.minCr = 0;
    return cfg;
}

l15_rc::PcsCarrier d2Pcs() {
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
    pcs.pcsTotalRate = 100000;
    pcs.thisFrameTarget = 10000;
    pcs.superresUpscaledWidth = 1920;
    pcs.seqProfile = l15_rc::kMainProfile;
    pcs.maxFrameSize = 100000;
    pcs.loopCount = 0;
    pcs.sliceType = l15_rc::kBslice;
    return pcs;
}

l15_rc::CrRefreshCarrier d2Cr() {
    l15_rc::CrRefreshCarrier cr{};
    cr.applyCyclicRefresh = 0;
    return cr;
}

}  // namespace

TEST_CASE("recodeLoopUpdateQ min_cr: the low-CR path raises q and returns early") {
    l15_rc::RcControl rc = d2Rc();
    l15_rc::RcConfig cfg = d2Cfg();
    cfg.minCr = 50;  // target_cr = 0.5
    l15_rc::PcsCarrier pcs = d2Pcs();
    pcs.pcsTotalRate = 35840000000ULL;  // dummy pack -> projected 70000000
    const l15_rc::CrRefreshCarrier cr = d2Cr();
    bool loop = false;
    int q = 100;
    int qLow = 50;
    int qHigh = 150;
    int undershootSeen = 0;
    int overshootSeen = 0;
    int lowCrSeen = 0;
    l15_rc::recodeLoopUpdateQ(rc, cfg, pcs, cr, 100, l15_rc::kAllowRecode, 0, 25, 8, 200, 0, 0, loop, q,
                              qLow, qHigh, undershootSeen, overshootSeen, lowCrSeen);
    CHECK(pcs.projectedFrameSize == 70000000);
    // compression_ratio = 3888000 / (70000000>>3 - 128) = 0.444349... < 0.5:
    // cr_ratio = 1.1252407..., q*cr_ratio^2 = 126.6166 -> (int) 126;
    // q = min(min(max(101, 126), 132), 255) = 126; q_low/q_high raised.
    CHECK(q == 126);
    CHECK(qLow == 126);
    CHECK(qHigh == 150);
    CHECK(loop == true);
    CHECK(lowCrSeen == 1);
    // The low_cr_seen RETURN precedes the clamp_qindex tail - the branch
    // counters stay untouched.
    CHECK(undershootSeen == 0);
    CHECK(overshootSeen == 0);
}

TEST_CASE("recodeLoopUpdateQ KFMAXBW early return after the dummy pack") {
    l15_rc::RcControl rc = d2Rc();
    l15_rc::RcConfig cfg = d2Cfg();
    l15_rc::PcsCarrier pcs = d2Pcs();
    const l15_rc::CrRefreshCarrier cr = d2Cr();
    bool loop = true;  // pre-set: the driver must clear it BEFORE the return
    int q = 100;
    int qLow = 50;
    int qHigh = 150;
    int undershootSeen = 0;
    int overshootSeen = 0;
    int lowCrSeen = 0;
    l15_rc::recodeLoopUpdateQ(rc, cfg, pcs, cr, 100, l15_rc::kAllowRecodeKfMaxBw, 0, 25, 8, 200, 0, 0,
                              loop, q, qLow, qHigh, undershootSeen, overshootSeen, lowCrSeen);
    // dummy pack ran: projected = (100000 + 256) >> 9 = 195 (INTER: no +13).
    CHECK(pcs.projectedFrameSize == 195);
    CHECK(loop == false);
    CHECK(q == 100);
    CHECK(qLow == 50);
    CHECK(qHigh == 150);
    CHECK(undershootSeen == 0);
    CHECK(overshootSeen == 0);
    CHECK(lowCrSeen == 0);
}

TEST_CASE("recodeLoopUpdateQ DISALLOW + VBR: no dummy pack, in-window, no recode") {
    l15_rc::RcControl rc = d2Rc();
    l15_rc::RcConfig cfg = d2Cfg();
    l15_rc::PcsCarrier pcs = d2Pcs();
    const l15_rc::CrRefreshCarrier cr = d2Cr();
    bool loop = false;
    int q = 100;
    int qLow = 50;
    int qHigh = 150;
    int undershootSeen = 0;
    int overshootSeen = 0;
    int lowCrSeen = 0;
    l15_rc::recodeLoopUpdateQ(rc, cfg, pcs, cr, 100, l15_rc::kDisallowRecode, 0, 25, 8, 200, 0, 0, loop,
                              q, qLow, qHigh, undershootSeen, overshootSeen, lowCrSeen);
    CHECK(pcs.projectedFrameSize == 0);
    CHECK(loop == false);
    CHECK(q == 100);
    // The VBR bounds at target 10000: (7500, 12500); projected 0 is inside.
    CHECK(undershootSeen == 0);
    CHECK(overshootSeen == 0);
}

TEST_CASE("recodeLoopUpdateQ AOM_Q + max_bit_rate: the parked CRF branch and the tpl tail") {
    l15_rc::RcControl rc = d2Rc();
    l15_rc::RcConfig cfg = d2Cfg();
    cfg.mode = l15_rc::kRcQ;
    l15_rc::PcsCarrier pcs = d2Pcs();
    // The TAIL gate reads PPCS->loop_count (rc_vbr_cbr.c:1917), not the
    // loop_count parameter - the fixture sets it to 1. The dummy pack
    // moves projected to 100000 so the frame stays inside the Q-mode
    // bounds at that loop_count (under 75000, over 125000): no recode.
    pcs.pcsTotalRate = 51199744ULL;  // (51199744 + 256) >> 9 = 100000
    pcs.loopCount = 1;
    const l15_rc::CrRefreshCarrier cr = d2Cr();
    bool loop = false;
    int q = 100;
    int qLow = 0;
    int qHigh = 150;
    int undershootSeen = 0;
    int overshootSeen = 0;
    int lowCrSeen = 0;
    l15_rc::recodeLoopUpdateQ(rc, cfg, pcs, cr, 100, l15_rc::kAllowRecode, 5000000, 25, 8, 200, 0, 1,
                              loop, q, qLow, qHigh, undershootSeen, overshootSeen, lowCrSeen);
    CHECK(pcs.projectedFrameSize == 100000);
    CHECK(loop == false);
    CHECK(q == 100);  // the parked capped_crf_reencode mutates nothing
    CHECK(rc.activeWorstQuality == 176);  // the gfu tpl search (200 -> 100)
}

TEST_CASE("recodeLoopUpdateQ overshoot: the regulated search and the seen flags") {
    l15_rc::RcControl rc = d2Rc();
    l15_rc::RcConfig cfg = d2Cfg();
    l15_rc::PcsCarrier pcs = d2Pcs();
    pcs.pcsTotalRate = 102399744ULL;  // dummy pack -> projected 200000
    pcs.thisFrameTarget = 100000;
    const l15_rc::CrRefreshCarrier cr = d2Cr();
    bool loop = false;
    int q = 100;
    int qLow = 50;
    int qHigh = 150;
    int undershootSeen = 0;
    int overshootSeen = 0;
    int lowCrSeen = 0;
    l15_rc::recodeLoopUpdateQ(rc, cfg, pcs, cr, 100, l15_rc::kAllowRecode, 0, 25, 8, 200, 0, 0, loop, q,
                              qLow, qHigh, undershootSeen, overshootSeen, lowCrSeen);
    // Bounds (75000, 50000): projected 200000 > 50000 and q 100 < max(150,
    // 200) -> recode; the overshoot branch (200000 > 100000): q_low =
    // min(101, 150) = 101; loop_count 0 -> the regulated search; the rcf
    // falls each retry (cf = 50 < 99); the final q clamps into [101, 150].
    CHECK(pcs.projectedFrameSize == 200000);
    CHECK(qLow == 101);
    CHECK(qHigh == 150);
    CHECK(q == 150);
    CHECK(loop == true);
    CHECK(overshootSeen == 1);
    CHECK(undershootSeen == 0);
    CHECK(std::fabs(rc.rateCorrectionFactors[1] - 0.497) <= 1e-9);
}

TEST_CASE("recodeLoopUpdateQ undershoot: q_high drops and the search runs") {
    l15_rc::RcControl rc = d2Rc();
    l15_rc::RcConfig cfg = d2Cfg();
    l15_rc::PcsCarrier pcs = d2Pcs();  // dummy pack -> projected 195
    const l15_rc::CrRefreshCarrier cr = d2Cr();
    bool loop = false;
    int q = 100;
    int qLow = 50;
    int qHigh = 150;
    int undershootSeen = 0;
    int overshootSeen = 0;
    int lowCrSeen = 0;
    l15_rc::recodeLoopUpdateQ(rc, cfg, pcs, cr, 100, l15_rc::kAllowRecode, 0, 25, 8, 200, 0, 0, loop, q,
                              qLow, qHigh, undershootSeen, overshootSeen, lowCrSeen);
    // Bounds (7500, 12500): projected 195 < 7500 and q 100 > bottom 0 ->
    // recode; the undershoot branch: q_high = max(99, 50) = 99; the
    // regulated undershoot search lands at 77 after the clamps.
    CHECK(qHigh == 99);
    CHECK(qLow == 50);
    CHECK(q == 77);
    CHECK(loop == true);
    CHECK(undershootSeen == 1);
    CHECK(overshootSeen == 0);
}

TEST_CASE("recodeLoopUpdateQ q==q_high at max bandwidth: the find_qindex rewrite") {
    l15_rc::RcControl rc = d2Rc();
    l15_rc::RcConfig cfg = d2Cfg();
    l15_rc::PcsCarrier pcs = d2Pcs();
    pcs.pcsTotalRate = 102399744ULL;  // dummy pack -> projected 200000
    pcs.thisFrameTarget = 100000;
    const l15_rc::CrRefreshCarrier cr = d2Cr();
    bool loop = false;
    int q = 150;
    int qLow = 50;
    int qHigh = 150;
    int undershootSeen = 0;
    int overshootSeen = 0;
    int lowCrSeen = 0;
    l15_rc::recodeLoopUpdateQ(rc, cfg, pcs, cr, 100, l15_rc::kAllowRecode, 0, 25, 8, 200, 0, 0, loop, q,
                              qLow, qHigh, undershootSeen, overshootSeen, lowCrSeen);
    // q == q_high and projected 200000 >= max_frame_bandwidth 50000:
    // q_val_high_new = q(150) * 200000/50000 = 63.75 * 4 = 255.0;
    // av1_find_qindex(255.0, 0, 255) = 225 (the first qindex whose
    // quantizer reaches 255). q_low = min(151, 225) = 151. The regulated
    // search then converges in ONE pass (zero retries): regulate at
    // tpbm 6274 with the fallen rcf 0.497 returns 162 >= q_low 151;
    // the clamp into [151, 225] keeps 162.
    CHECK(qHigh == 225);
    CHECK(qLow == 151);
    CHECK(q == 162);
    CHECK(loop == true);
    CHECK(overshootSeen == 1);
}
