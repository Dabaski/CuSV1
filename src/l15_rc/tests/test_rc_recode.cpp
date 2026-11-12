#include <doctest.h>
#include <cmath>
#include <cstdint>

#include "rc_arith.h"

// RC2 Wave D1: the recode helpers. Reference spans (rc_vbr_cbr.c):
//   av1_find_qindex :1773-1789, av1_get_compression_ratio :1690-1699,
//   av1_rc_compute_frame_size_bounds :1701-1717, recode_loop_test
//   :1756-1771, get_kf_q_tpl :1642-1661, get_gfu_q_tpl :1667-1688.
//
// FLOAT DISCIPLINE: qindex/limit outputs are INTEGER and pinned exactly;
// the compression-ratio double is compared ABSOLUTE 1e-9 with fabs (doctest
// Approx is relative).
//
// EXPECTATION DERIVATION: oracle-derived. Every pin was computed by an
// independent python oracle replicating the reference loops from the raw
// tables (the 8-bit ac_qlookup_QTX and the four minq tables extracted
// verbatim from the landed registry / the pinned reference). The kf/gfu
// search's prev_dif is the INITIAL difference and is NOT refreshed inside
// the loop (rc_vbr_cbr.c:1650-1659) - the oracle replicates exactly that,
// and the stop conditions below pin both loop exits (the |diff| <= 4 exit
// and the zero-iteration boundary).

namespace {

l15_rc::RcControl recRc() {
    l15_rc::RcControl rc{};
    rc.activeWorstQuality = 200;
    rc.maxFrameBandwidth = 50000;
    rc.kfBoost = 4500;
    rc.gfuBoost = 1500;
    return rc;
}

l15_rc::PcsCarrier recPcs() {
    l15_rc::PcsCarrier pcs{};
    pcs.frameType = l15_rc::kInterFrame;
    pcs.updateType = l15_rc::kSvtAv1LfUpdate;  // NOT kf/gf/arf: frameIsKfGfArf false
    pcs.projectedFrameSize = 10000;
    return pcs;
}

}  // namespace

TEST_CASE("av1FindQindex pins the binary search over convertQindexToQ") {
    const int bitDepth = l15_rc::kEbEightBit;
    // desired = q(100) exactly: the first index with q >= desired is 100
    // (the table is strictly increasing there).
    CHECK(l15_rc::av1FindQindex(l15_rc::convertQindexToQ(100, bitDepth), bitDepth, 0, 255) == 100);
    // desired just above q(100): 101.
    CHECK(l15_rc::av1FindQindex(l15_rc::convertQindexToQ(100, bitDepth) + 1e-9, bitDepth, 0, 255) == 101);
    // desired = 0: q(0) = 1.0 >= 0 -> best index.
    CHECK(l15_rc::av1FindQindex(0.0, bitDepth, 0, 255) == 0);
    // desired unsatisfiable: high collapses onto worst.
    CHECK(l15_rc::av1FindQindex(1e9, bitDepth, 0, 255) == 255);
    // desired between q(49) and q(50): the upper index.
    CHECK(l15_rc::av1FindQindex((l15_rc::convertQindexToQ(49, bitDepth) +
                                 l15_rc::convertQindexToQ(50, bitDepth)) / 2.0, bitDepth, 0, 255) == 50);
    // a narrowed [best, worst] window clamps both ends.
    CHECK(l15_rc::av1FindQindex(l15_rc::convertQindexToQ(15, bitDepth), bitDepth, 10, 20) == 15);
}

TEST_CASE("getCompressionRatio pins the profile factor and the 128 overhead") {
    // MAIN_PROFILE (0): factor 15. luma = 1920*1080 = 2073600;
    // (2073600*15) >> 3 = 3888000.
    CHECK(std::fabs(l15_rc::getCompressionRatio(1920, 1080, l15_rc::kMainProfile, 130) - 1944000.0) <= 1e-9);
    // encoded <= 129 subtracts nothing: 129 > 129 is false -> 1.
    CHECK(std::fabs(l15_rc::getCompressionRatio(1920, 1080, l15_rc::kMainProfile, 129) - 3888000.0) <= 1e-9);
    CHECK(std::fabs(l15_rc::getCompressionRatio(1920, 1080, l15_rc::kMainProfile, 100) - 3888000.0) <= 1e-9);
    // encoded 257 -> 129: 3888000/129.
    CHECK(std::fabs(l15_rc::getCompressionRatio(1920, 1080, l15_rc::kMainProfile, 257) -
                    3888000.0 / 129.0) <= 1e-9);
    // HIGH_PROFILE (1): factor 30 -> (2073600*30) >> 3 = 7776000.
    CHECK(std::fabs(l15_rc::getCompressionRatio(1920, 1080, l15_rc::kHighProfile, 130) - 3888000.0) <= 1e-9);
    // PROFESSIONAL_PROFILE (2) (the else branch): factor 36. 1280x720:
    // (921600*36) >> 3 = 4147200, encoded 100 -> 1.
    CHECK(std::fabs(l15_rc::getCompressionRatio(1280, 720, 2, 100) - 4147200.0) <= 1e-9);
}

TEST_CASE("computeFrameSizeBounds pins the Q and non-Q tolerance branches") {
    int under = -1;
    int over = -1;
    // non-Q: tolerance = max(100, 25*5000/100 = 1250) = 1250;
    // under = 5000-1250 = 3750, over = min(6250, 6000) = 6000.
    l15_rc::computeFrameSizeBounds(l15_rc::kRcCbr, 25, 10000, 0, 5000, 6000, under, over);
    CHECK(under == 3750);
    CHECK(over == 6000);
    // non-Q floor: 25*150/100 = 37 -> tolerance floor 100.
    l15_rc::computeFrameSizeBounds(l15_rc::kRcCbr, 25, 10000, 0, 150, 6000, under, over);
    CHECK(under == 50);
    CHECK(over == 250);
    // Q mode: tolerance off max_frame_size; loop_count 0 forces under = 0.
    l15_rc::computeFrameSizeBounds(l15_rc::kRcQ, 25, 10000, 0, 5000, 6000, under, over);
    CHECK(under == 0);
    CHECK(over == 12500);
    // Q mode with a recode pass: under = max(10000-2500, 0) = 7500.
    l15_rc::computeFrameSizeBounds(l15_rc::kRcQ, 25, 10000, 1, 5000, 6000, under, over);
    CHECK(under == 7500);
    CHECK(over == 12500);
    // Q mode floor: 25*200/100 = 50 -> tolerance floor 100.
    l15_rc::computeFrameSizeBounds(l15_rc::kRcQ, 25, 200, 1, 5000, 6000, under, over);
    CHECK(under == 100);
    CHECK(over == 300);
}

TEST_CASE("recodeLoopTest pins the gate and the two limit conditions") {
    l15_rc::RcControl rc = recRc();
    l15_rc::PcsCarrier pcs = recPcs();
    // Gate passes via kfgfarf (ARF update): projected 10000 > high 9000
    // and q 100 < maxq 200 -> recode.
    pcs.updateType = l15_rc::kSvtAv1ArfUpdate;
    CHECK(l15_rc::recodeLoopTest(rc, pcs, l15_rc::kAllowRecodeKfMaxBw, 9000, 11000, 100, 200, 50));
    // Same gate, both limit conditions false: projected 10000 in
    // (low 5000, high 20000) -> no recode.
    CHECK_FALSE(l15_rc::recodeLoopTest(rc, pcs, l15_rc::kAllowRecodeKfMaxBw, 20000, 5000, 100, 200, 50));
    // Gate passes via ALLOW_RECODE even when not kfgfarf.
    pcs.updateType = l15_rc::kSvtAv1LfUpdate;
    CHECK(l15_rc::recodeLoopTest(rc, pcs, l15_rc::kAllowRecode, 9000, 11000, 100, 200, 50));
    // Gate FAILS: DISALLOW_RECODE, not kfgfarf, projected < max bandwidth.
    CHECK_FALSE(l15_rc::recodeLoopTest(rc, pcs, l15_rc::kDisallowRecode, 9000, 11000, 100, 200, 50));
    // The bandwidth gate fires at >= even with DISALLOW_RECODE and no
    // kfgfarf: projected 10000 >= max_frame_bandwidth 10000.
    rc.maxFrameBandwidth = 10000;
    CHECK(l15_rc::recodeLoopTest(rc, pcs, l15_rc::kDisallowRecode, 9000, 11000, 100, 200, 50));
    // ...but the limit conditions still decide: in-window -> no recode.
    CHECK_FALSE(l15_rc::recodeLoopTest(rc, pcs, l15_rc::kDisallowRecode, 20000, 5000, 100, 200, 50));
}

TEST_CASE("getKfQTpl pins the iterative kf quality search") {
    l15_rc::RcControl rc = recRc();
    // boost > KF_HIGH (5000): the low-motion table drives the walk.
    rc.kfBoost = 5500;
    CHECK(l15_rc::getKfQTpl(rc, 150, l15_rc::kEbEightBit) == 248);
    // boost < KF_LOW (400): the high-motion table.
    rc.kfBoost = 300;
    CHECK(l15_rc::getKfQTpl(rc, 150, l15_rc::kEbEightBit) == 208);
    // boost inside the band: the interpolated active-quality curve.
    rc.kfBoost = 4500;
    CHECK(l15_rc::getKfQTpl(rc, 150, l15_rc::kEbEightBit) == 245);
    CHECK(l15_rc::getKfQTpl(rc, 120, l15_rc::kEbEightBit) == 229);
}

TEST_CASE("getGfuQTpl pins the iterative gfu quality search") {
    l15_rc::RcControl rc = recRc();
    // boost > GF_HIGH_TPL_LA (2400): the low-motion table; start
    // active = gf_low[200] = 110 < 150 -> walks q up to 227.
    rc.gfuBoost = 2500;
    CHECK(l15_rc::getGfuQTpl(rc, 150, l15_rc::kEbEightBit) == 227);
    // boost < GF_LOW_TPL_LA (300): the high-motion table;
    // active = gf_high[200] = 153, |153-150| = 3 <= 4: ZERO iterations.
    rc.gfuBoost = 200;
    CHECK(l15_rc::getGfuQTpl(rc, 150, l15_rc::kEbEightBit) == 200);
    // boost inside the band: interpolated curve.
    rc.gfuBoost = 1500;
    CHECK(l15_rc::getGfuQTpl(rc, 100, l15_rc::kEbEightBit) == 176);
    // a different start (240): active = gf_high[240] = 189 > 140 -> walks
    // q DOWN to 213 (the start the walk-fixture intended; set explicitly).
    rc.activeWorstQuality = 240;
    CHECK(l15_rc::getGfuQTpl(rc, 140, l15_rc::kEbEightBit) == 213);
}
