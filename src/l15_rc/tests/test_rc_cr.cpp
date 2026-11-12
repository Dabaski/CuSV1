#include <doctest.h>
#include <cmath>
#include <cstdint>

#include "rc_arith.h"

// RC2 Wave D4: the vbr cyclic-refresh trio. Reference spans (rc_vbr_cbr.c):
//   cyclic_refresh_init :895-972, compute_cr_deltaq :978-985 (static,
//   exported for testability), cyclic_refresh_compute_cr_qdeltas
//   :989-998, CR_MAX_RATE_TARGET_RATIO :987.
//
// FLOAT DISCIPLINE: integer state pinned exactly; the rate_ratio doubles
// compared ABSOLUTE 1e-9 with fabs (1.15 stores as 1.1499999... - the
// tolerance covers it).
//
// EXPECTATION DERIVATION: oracle-derived. qp_max_thresh = (118*255)>>7 =
// 235; the sc_class1 distance factor uses INTEGER frames_since_key/10
// (8/10 = 0, 100/10 = 10 -> the 0.75 clamp); the fsk gate compares
// against 4*(1<<hier)*100/percent with INTEGER arithmetic; the deltaq
// oracle replicates the closed-form find_qindex_by_rate chain.

namespace {

l15_rc::RcControl cr4Rc() {
    l15_rc::RcControl rc{};
    rc.percentRefreshAdjustment = 0;
    rc.rateRatioQdeltaAdjustment = 0.0;
    rc.bestQuality = 0;
    rc.worstQuality = 255;
    rc.avgFrameQindex[1] = 150;
    rc.avgFrameLowMotion = 0;
    rc.framesSinceKey = 8;
    rc.rc1Frame = 0;
    rc.rc2Frame = 0;
    return rc;
}

l15_rc::CrRefreshCarrier cr4Cr() {
    l15_rc::CrRefreshCarrier cr{};
    cr.sbStart = 77;  // sentinels: the early return must leave them
    cr.sbEnd = 77;
    return cr;
}

l15_rc::PcsCarrier cr4Pcs() {
    l15_rc::PcsCarrier pcs{};
    pcs.sliceType = l15_rc::kBslice;
    pcs.temporalLayerIndex = 0;
    pcs.scClass1 = false;
    return pcs;
}

}  // namespace

TEST_CASE("cyclicRefreshInit I_SLICE: the adjustment reset and the early return") {
    l15_rc::RcControl rc = cr4Rc();
    l15_rc::CrRefreshCarrier cr = cr4Cr();
    l15_rc::PcsCarrier pcs = cr4Pcs();
    pcs.sliceType = l15_rc::kIslice;
    std::uint32_t crSbEnd = 42;
    l15_rc::cyclicRefreshInit(rc, cr, pcs, 64, 100, crSbEnd, 2);
    CHECK(rc.percentRefreshAdjustment == 5);
    CHECK(std::fabs(rc.rateRatioQdeltaAdjustment - 0.25) <= 1e-9);
    CHECK(cr.percentRefresh == 25);  // 20 + 5
    CHECK(cr.applyCyclicRefresh == 0);  // I_SLICE fails the apply gate
    CHECK(cr.sbStart == 77);  // the early return leaves everything
    CHECK(cr.sbEnd == 77);
    CHECK(cr.maxQdeltaPerc == 0);
    CHECK(crSbEnd == 42);
}

TEST_CASE("cyclicRefreshInit plain B-slice: the full init path") {
    l15_rc::RcControl rc = cr4Rc();
    l15_rc::CrRefreshCarrier cr = cr4Cr();
    l15_rc::PcsCarrier pcs = cr4Pcs();
    std::uint32_t crSbEnd = 0;
    l15_rc::cyclicRefreshInit(rc, cr, pcs, 64, 100, crSbEnd, 2);
    CHECK(cr.percentRefresh == 20);
    CHECK(cr.applyCyclicRefresh == 1);
    CHECK(cr.sbStart == 0);
    CHECK(cr.sbEnd == 20);  // min(0 + 100*20/100, 100)
    CHECK(crSbEnd == 20);   // 20 < 100: the cycle advances
    CHECK(cr.maxQdeltaPerc == 60);
    // fsk 8 < 4*(1<<2)*100/20 = 80 -> 1.50 (+ adjustment 0.0)
    CHECK(std::fabs(cr.rateRatioQdelta - 1.5) <= 1e-9);
    CHECK(cr.rateBoostFac == 15);
}

TEST_CASE("cyclicRefreshInit qp_max_thresh gate: avgQ 240 > 235") {
    l15_rc::RcControl rc = cr4Rc();
    rc.avgFrameQindex[1] = 240;
    l15_rc::CrRefreshCarrier cr = cr4Cr();
    l15_rc::PcsCarrier pcs = cr4Pcs();
    std::uint32_t crSbEnd = 7;
    l15_rc::cyclicRefreshInit(rc, cr, pcs, 64, 100, crSbEnd, 2);
    CHECK(cr.applyCyclicRefresh == 0);
    CHECK(cr.sbStart == 77);
    CHECK(cr.sbEnd == 77);
    CHECK(cr.maxQdeltaPerc == 0);
    CHECK(crSbEnd == 7);
}

TEST_CASE("cyclicRefreshInit qp_thresh gate: best 200 raises the floor to 204") {
    l15_rc::RcControl rc = cr4Rc();
    rc.bestQuality = 200;  // qp_thresh = max(16, 204)
    l15_rc::CrRefreshCarrier cr = cr4Cr();
    l15_rc::PcsCarrier pcs = cr4Pcs();
    std::uint32_t crSbEnd = 7;
    l15_rc::cyclicRefreshInit(rc, cr, pcs, 64, 100, crSbEnd, 2);
    CHECK(cr.applyCyclicRefresh == 0);  // avgQ 150 < 204
    CHECK(crSbEnd == 7);
}

TEST_CASE("cyclicRefreshInit low-motion gate: 40 is nonzero and under 50") {
    l15_rc::RcControl rc = cr4Rc();
    rc.avgFrameLowMotion = 40;
    l15_rc::CrRefreshCarrier cr = cr4Cr();
    l15_rc::PcsCarrier pcs = cr4Pcs();
    std::uint32_t crSbEnd = 7;
    l15_rc::cyclicRefreshInit(rc, cr, pcs, 64, 100, crSbEnd, 2);
    CHECK(cr.applyCyclicRefresh == 0);
    CHECK(crSbEnd == 7);
}

TEST_CASE("cyclicRefreshInit sc_class1: the scene-change distance factor") {
    l15_rc::RcControl rc = cr4Rc();
    rc.rateRatioQdeltaAdjustment = 0.25;
    l15_rc::CrRefreshCarrier cr = cr4Cr();
    l15_rc::PcsCarrier pcs = cr4Pcs();
    pcs.scClass1 = true;
    std::uint32_t crSbEnd = 0;
    l15_rc::cyclicRefreshInit(rc, cr, pcs, 64, 100, crSbEnd, 2);
    CHECK(cr.percentRefresh == 25);  // 20 + 0 + 5
    // fsk 8: INTEGER 8/10 = 0 -> distance 0.0; 2.25 + 0.25 - 0.0 = 2.5;
    // rc_1/rc_2 are 0 -> no -0.25.
    CHECK(std::fabs(cr.rateRatioQdelta - 2.5) <= 1e-9);
    CHECK(cr.rateBoostFac == 10);
}

TEST_CASE("cyclicRefreshInit sc_class1 far from the scene change: the 0.75 clamp and rc_1") {
    l15_rc::RcControl rc = cr4Rc();
    rc.framesSinceKey = 100;  // INTEGER 100/10 = 10 -> min(0.75, 1.0) = 0.75
    rc.rc1Frame = -1;         // the overshoot marker: another -0.25
    l15_rc::CrRefreshCarrier cr = cr4Cr();
    l15_rc::PcsCarrier pcs = cr4Pcs();
    pcs.scClass1 = true;
    std::uint32_t crSbEnd = 0;
    l15_rc::cyclicRefreshInit(rc, cr, pcs, 64, 100, crSbEnd, 2);
    // 2.25 + 0.0 - 0.75 - 0.25 = 1.25.
    CHECK(std::fabs(cr.rateRatioQdelta - 1.25) <= 1e-9);
    CHECK(cr.rateBoostFac == 10);
}

TEST_CASE("cyclicRefreshInit the sb_end wrap resets cr_sb_end to 0") {
    l15_rc::RcControl rc = cr4Rc();
    rc.percentRefreshAdjustment = 80;  // percent_refresh = 100
    l15_rc::CrRefreshCarrier cr = cr4Cr();
    l15_rc::PcsCarrier pcs = cr4Pcs();
    std::uint32_t crSbEnd = 0;
    l15_rc::cyclicRefreshInit(rc, cr, pcs, 64, 20, crSbEnd, 2);
    CHECK(cr.sbStart == 0);
    CHECK(cr.sbEnd == 20);  // min(0 + 20*100/100, 20) = 20
    CHECK(crSbEnd == 0);    // 20 >= 20: the wrap
    // A mid-cycle start wraps the same way.
    l15_rc::CrRefreshCarrier cr2 = cr4Cr();
    std::uint32_t crSbEnd2 = 10;
    l15_rc::cyclicRefreshInit(rc, cr2, pcs, 64, 20, crSbEnd2, 2);
    CHECK(cr2.sbStart == 10);
    CHECK(cr2.sbEnd == 20);  // min(10 + 20, 20)
    CHECK(crSbEnd2 == 0);
}

TEST_CASE("cyclicRefreshInit the fsk gate boundary: 80 is not < 80") {
    l15_rc::RcControl rc = cr4Rc();
    rc.framesSinceKey = 80;
    l15_rc::CrRefreshCarrier cr = cr4Cr();
    l15_rc::PcsCarrier pcs = cr4Pcs();
    std::uint32_t crSbEnd = 0;
    l15_rc::cyclicRefreshInit(rc, cr, pcs, 64, 100, crSbEnd, 2);
    CHECK(std::fabs(cr.rateRatioQdelta - 1.15) <= 1e-9);
    // One below: the 1.50 branch.
    l15_rc::RcControl rc2 = cr4Rc();
    rc2.framesSinceKey = 79;
    l15_rc::CrRefreshCarrier cr2 = cr4Cr();
    l15_rc::cyclicRefreshInit(rc2, cr2, pcs, 64, 100, crSbEnd, 2);
    CHECK(std::fabs(cr2.rateRatioQdelta - 1.5) <= 1e-9);
}

TEST_CASE("cyclicRefreshInit super_block_size != 64 disables the refresh") {
    l15_rc::RcControl rc = cr4Rc();
    l15_rc::CrRefreshCarrier cr = cr4Cr();
    l15_rc::PcsCarrier pcs = cr4Pcs();
    std::uint32_t crSbEnd = 0;
    l15_rc::cyclicRefreshInit(rc, cr, pcs, 128, 100, crSbEnd, 2);
    CHECK(cr.applyCyclicRefresh == 0);
    CHECK(crSbEnd == 0);
}

TEST_CASE("computeCrDeltaq pins the deltaq search and the max_qdelta floor") {
    l15_rc::RcControl rc = cr4Rc();
    l15_rc::CrRefreshCarrier cr = cr4Cr();
    cr.maxQdeltaPerc = 60;
    // The reference's target is a MULTIPLICATION (rc_process.c:295:
    // rate_target_ratio * base_bits_per_mb) - a ratio above 1 asks for
    // MORE bits -> a LOWER qindex -> a NEGATIVE delta (better quality).
    // bpm(INTER, 100, 1.0) = 35714; target = 1.5*35714 = 53571 -> the
    // first qindex with bpm <= 53571 is 68 -> delta 68-100 = -32
    // (>= the floor -60).
    CHECK(l15_rc::computeCrDeltaq(rc, cr, 100, 1.5, l15_rc::kEbEightBit, false) == -32);
    // ratio 4.0 (the seg2 clamp value): the raw delta underflows the
    // floor: max_qdelta 60 at q 100 binds -> -60.
    CHECK(l15_rc::computeCrDeltaq(rc, cr, 100, 4.0, l15_rc::kEbEightBit, false) == -60);
    // q 200 (bpm 6259), ratio 1.15: delta -7.
    CHECK(l15_rc::computeCrDeltaq(rc, cr, 200, 1.15, l15_rc::kEbEightBit, false) == -7);
    // screen content: bpm enumerator 750000 (q 50 -> 52631); ratio 2.5
    // -> delta -30.
    CHECK(l15_rc::computeCrDeltaq(rc, cr, 50, 2.5, l15_rc::kEbEightBit, true) == -30);
    // A tighter max_qdelta BINDS: max_qdelta 30 at q 100 floors the raw
    // -32 to -30.
    l15_rc::CrRefreshCarrier cr30 = cr4Cr();
    cr30.maxQdeltaPerc = 30;
    CHECK(l15_rc::computeCrDeltaq(rc, cr30, 100, 1.5, l15_rc::kEbEightBit, false) == -30);
}

TEST_CASE("cyclicRefreshComputeCrQdeltas writes the three segment deltas") {
    l15_rc::RcControl rc = cr4Rc();
    l15_rc::CrRefreshCarrier cr = cr4Cr();
    cr.maxQdeltaPerc = 60;
    cr.rateRatioQdelta = 1.50;
    cr.rateRatioQdeltaSeg2 = 10.0;  // clamps to CR_MAX_RATE_TARGET_RATIO 4.0
    l15_rc::cyclicRefreshComputeCrQdeltas(rc, cr, 100, l15_rc::kEbEightBit, false);
    CHECK(cr.qindexDelta[0] == 0);
    CHECK(cr.qindexDelta[1] == -32);
    CHECK(cr.qindexDelta[2] == -60);
    // The unclamped seg2 path: 1.15 / 2.0.
    l15_rc::CrRefreshCarrier cr2 = cr4Cr();
    cr2.maxQdeltaPerc = 60;
    cr2.rateRatioQdelta = 1.15;
    cr2.rateRatioQdeltaSeg2 = 2.0;
    l15_rc::cyclicRefreshComputeCrQdeltas(rc, cr2, 100, l15_rc::kEbEightBit, false);
    CHECK(cr2.qindexDelta[0] == 0);
    CHECK(cr2.qindexDelta[1] == -9);
    CHECK(cr2.qindexDelta[2] == -51);
}
