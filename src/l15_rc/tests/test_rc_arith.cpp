#include <doctest.h>
#include <string>
#include <vector>

#include "rc_arith.h"
#include "rc_tables.h"

// RC2 R1: get_active_quality + the kf/gf wrappers (rc_vbr_cbr.c:31-69).
//
// FLOAT DISCIPLINE (module-wide, stated here per the brief): every
// expectation in this file is INTEGER-valued and pinned EXACTLY. Where a
// later slice leaves a double at the API boundary the comparison is 1e-9
// absolute unless the reference's own truncation demands otherwise; the
// choice is stated per test at that slice. R1 has no double outputs.
//
// EXPECTATION DERIVATION: the pinned values below are hand-derived from
// the reference table PRINTS (rc_tables.h:44-298), not recomputed through
// the port's own table access path - the same-table oracle would be blind
// to a table substitution (the RE1 M3 lesson). The pinned q indices hit
// entries where the two tables of each pair DISAGREE by a wide margin, so
// a swapped low/high table fails the boundary cases. The registry tables
// themselves are already diff-verified entry-for-entry against the
// reference by the RC1 test.

namespace {

const std::int32_t* table8(const char* base) {
    std::int32_t len = 0;
    std::string name(base);
    name += "_8"; // the registry carries the bit-depth suffix
    const std::int32_t* t = l15_rc::findTable(name.c_str(), len);
    REQUIRE_MESSAGE(t != nullptr, "registry table missing");
    REQUIRE(len == 256);
    return t;
}

l15_rc::RcControl rcWith(int kfBoost, int gfuBoost) {
    l15_rc::RcControl rc{};
    rc.kfBoost = kfBoost;
    rc.gfuBoost = gfuBoost;
    return rc;
}

}  // namespace

// Hand-derived entries (reference rc_tables.h prints):
//   kf_low[100]=9  kf_high[100]=40  kf_low[200]=65  kf_high[200]=137
//   kf_low[255]=163 kf_high[255]=230 kf_low[49]=2   kf_high[49]=18
//   arf_low[100]=24 arf_high[100]=51 arf_low[200]=110 arf_high[200]=153
//   arf_low[255]=216 arf_high[255]=245 arf_low[49]=10 arf_high[49]=23
// KF bounds: low=400 high=5000 gap=4600 half-gap=2300.
// GF bounds: low=300 high=2400 gap=2100 half-gap=1050.
//
// KF interpolation check at q=100 boost=2700:
//   offset=2300 qdiff=31 adj=(2300*31+2300)/4600=73600/4600=16 -> 9+16=25.
// KF q=200 boost=500: adj=(4500*72+2300)/4600=326300/4600=70 -> 65+70=135.
// KF q=255 boost=4800: adj=(200*67+2300)/4600=15700/4600=3 -> 163+3=166.
// KF q=255 boost=2700: adj=(2300*67+2300)/4600=156400/4600=34 -> 197.
// KF q=49 boost=2700: adj=(2300*16+2300)/4600=39100/4600=8 -> 2+8=10.
// GF q=100 boost=1350: adj=(1050*27+1050)/2100=29400/2100=14 -> 24+14=38.
// GF q=200 boost=1350: adj=(1050*43+1050)/2100=46200/2100=22 -> 110+22=132.
// GF q=255 boost=2399: adj=(1*29+1050)/2100=1079/2100=0 -> 216.

TEST_CASE("get_active_quality boundaries select the named table") {
    const std::int32_t* kfLow = table8("kf_low_motion_minq_cqp");
    const std::int32_t* kfHigh = table8("kf_high_motion_minq");

    // boost > high -> low-motion table.
    CHECK(l15_rc::getActiveQuality(100, 6000, 400, 5000, kfLow, kfHigh) == 9);
    CHECK(l15_rc::getActiveQuality(255, 6000, 400, 5000, kfLow, kfHigh) == 163);
    // boost < low -> high-motion table.
    CHECK(l15_rc::getActiveQuality(100, 200, 400, 5000, kfLow, kfHigh) == 40);
    CHECK(l15_rc::getActiveQuality(255, 200, 400, 5000, kfLow, kfHigh) == 230);
    // boost == high: offset 0, (gap>>1)/gap == 0 -> still the low table.
    CHECK(l15_rc::getActiveQuality(100, 5000, 400, 5000, kfLow, kfHigh) == 9);
    // boost == low: adj == qdiff (gap even) -> still the high table.
    CHECK(l15_rc::getActiveQuality(100, 400, 400, 5000, kfLow, kfHigh) == 40);
}

TEST_CASE("get_active_quality interpolation is exact at pinned points") {
    const std::int32_t* kfLow = table8("kf_low_motion_minq_cqp");
    const std::int32_t* kfHigh = table8("kf_high_motion_minq");

    CHECK(l15_rc::getActiveQuality(100, 2700, 400, 5000, kfLow, kfHigh) == 25);
    // q=200: qdiff=72, adj=(2300*72+2300)/4600=167900/4600=36 -> 65+36=101.
    CHECK(l15_rc::getActiveQuality(200, 2700, 400, 5000, kfLow, kfHigh) == 101);
    CHECK(l15_rc::getActiveQuality(255, 2700, 400, 5000, kfLow, kfHigh) == 197);
    CHECK(l15_rc::getActiveQuality(49, 2700, 400, 5000, kfLow, kfHigh) == 10);
    CHECK(l15_rc::getActiveQuality(200, 500, 400, 5000, kfLow, kfHigh) == 135);
    CHECK(l15_rc::getActiveQuality(255, 4800, 400, 5000, kfLow, kfHigh) == 166);
}

TEST_CASE("get_active_quality truncation is C semantics on negative qdiff") {
    // Arbitrary pair (inter/rtc) where high_motion < low_motion: qdiff=-22,
    // gap=100 (even, half=50). boost=50: adj=(50*-22+50)/100=-1050/100=-10
    // (C truncation toward zero, not floor) -> 91-10=81.
    // boost=25: adj=(75*-22+50)/100=-1600/100=-16 -> 91-16=75.
    const std::int32_t* inter = table8("inter_minq");
    const std::int32_t* rtc = table8("rtc_minq");
    CHECK(l15_rc::getActiveQuality(100, 50, 0, 100, inter, rtc) == 81);
    CHECK(l15_rc::getActiveQuality(100, 25, 0, 100, inter, rtc) == 75);
}

TEST_CASE("get_active_quality is monotone non-increasing in boost and in range") {
    const std::int32_t* kfLow = table8("kf_low_motion_minq_cqp");
    const std::int32_t* kfHigh = table8("kf_high_motion_minq");
    int prev = 256;
    for (int boost = 350; boost <= 5050; boost += 50) {
        const int q100 = l15_rc::getActiveQuality(100, boost, 400, 5000, kfLow, kfHigh);
        CHECK(q100 <= prev);
        CHECK(q100 >= 9);
        CHECK(q100 <= 40);
        prev = q100;
    }
}

TEST_CASE("get_kf_active_quality_tpl mirrors get_active_quality over the kf tables") {
    CHECK(l15_rc::getKfActiveQualityTpl(rcWith(6000, 0), 100, 8) == 9);
    CHECK(l15_rc::getKfActiveQualityTpl(rcWith(200, 0), 100, 8) == 40);
    CHECK(l15_rc::getKfActiveQualityTpl(rcWith(2700, 0), 100, 8) == 25);
    CHECK(l15_rc::getKfActiveQualityTpl(rcWith(2700, 0), 49, 8) == 10);
}

TEST_CASE("get_gf_active_quality_tpl_la selects the arf tables and gf bounds") {
    CHECK(l15_rc::getGfActiveQualityTplLa(rcWith(0, 3000), 100, 8) == 24);
    CHECK(l15_rc::getGfActiveQualityTplLa(rcWith(0, 100), 100, 8) == 51);
    CHECK(l15_rc::getGfActiveQualityTplLa(rcWith(0, 1350), 100, 8) == 38);
    CHECK(l15_rc::getGfActiveQualityTplLa(rcWith(0, 1350), 200, 8) == 132);
    CHECK(l15_rc::getGfActiveQualityTplLa(rcWith(0, 2399), 255, 8) == 216);
    // Boundary: boost == high -> low table; boost == low -> high table.
    CHECK(l15_rc::getGfActiveQualityTplLa(rcWith(0, 2400), 100, 8) == 24);
    CHECK(l15_rc::getGfActiveQualityTplLa(rcWith(0, 300), 100, 8) == 51);
}

TEST_CASE("get_gf_high_motion_quality reads arfgf_high_motion_minq directly") {
    CHECK(l15_rc::getGfHighMotionQuality(100, 8) == 51);
    CHECK(l15_rc::getGfHighMotionQuality(255, 8) == 245);
    CHECK(l15_rc::getGfHighMotionQuality(49, 8) == 23);
    // The bit_depth argument is consumed and ignored (CONFIG_ENABLE_HIGH_
    // BIT_DEPTH=0 branch): 10 gives the same 8-bit table.
    CHECK(l15_rc::getGfHighMotionQuality(100, 10) == 51);
}

// ---- R2: the one-pass CBR target-size family (rc_vbr_cbr.c:71-104,
// :518-603, :1475-1490) ----
//
// Hand-derived expectations (all exact integer arithmetic):
// calc_pframe q=71-95: one_pct = 1 + optimal/100 (int64 truncation);
// pct_low = min(diff/one_pct, under); target -= (target*pct)/200;
// max_rate = avg*max_inter/100; return max(max(avg>>4, 200), target).
//   A. avg=10000 optimal=1000000 buffer=900000 under=50: diff=100000,
//      one_pct=10001, pct=100000/10001=9, target=10000-90000/200=9550.
//   B. buffer=1100000: diff=-100000, pct=min(9, over=40)=9, +450 -> 10450.
//   C. A with max_inter=50: max_rate=5000, target=min(9550,5000)=5000.
//   D. avg=1000 buffer=optimal: diff=0 -> 1000 (floor 200 not binding).
//   E. avg=200 optimal=100000 buffer=0 under=100: one_pct=1001,
//      pct=min(99,100)=99, target=200-99=101, floor max(12,200)=200 -> 200.
//   F. A with under=8: pct=min(9,8)=8 -> 9600.
// iframe :534-551: pn==0 -> starting/2 (INT_MAX-clamped); else
//   kf_boost=max(32,(int)(2*fr-16)); if fsk < fr/2: (int)(boost*fsk/(fr/2))
//   (int*int then /double, truncation); target=((16+boost)*avg)>>4.
//   fr=30 fsk=5: boost=44, 44*5/15.0=14.67->14, (30*10000)>>4=18750.
//   fr=30 fsk=15: 15<15.0 false -> boost 44 -> (60*10000)>>4=37500.
//   fr=30 fsk=1: 44*1/15.0=2.93->2, (18*10000)>>4=11250.
//   fr=10: boost=max(32,4)=32, fsk=1<5: 32*1/5.0=6.4->6, (22*10000)>>4=13750.
// clamp :518-531: max_intra>0: min(target, avg*pct/100); then min with
//   max_frame_bandwidth.
// buffers :591-603: starting*bandwidth/1000; optimal==0 -> bandwidth/8.
// buffer level :1475-1490: showable += avg-encoded; !showable -= encoded;
//   clip bits_off to maximum; buffer = bits_off.

namespace {

l15_rc::RcControl rcBase() {
    l15_rc::RcControl rc{};
    rc.avgFrameBandwidth = 10000;
    rc.maxFrameBandwidth = 50000;
    rc.optimalBufferLevel = 1000000;
    rc.startingBufferLevel = 2000000;
    rc.maximumBufferSize = 5000000;
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
    pcs.frameType = 1;  // INTER_FRAME
    pcs.showableFrame = true;
    pcs.pictureNumber = 10;
    return pcs;
}

}  // namespace

TEST_CASE("calc_pframe_target_size_one_pass_cbr lowers and raises by pct") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::RcConfig cfg = cfgBase();
    rc.bufferLevel = 900000;
    CHECK(l15_rc::calcPframeTargetSizeOnePassCbr(rc, cfg) == 9550);
    rc.bufferLevel = 1100000;
    CHECK(l15_rc::calcPframeTargetSizeOnePassCbr(rc, cfg) == 10450);
    // under_shoot_pct clamps the pct before the division.
    cfg.underShootPct = 8;
    rc.bufferLevel = 900000;
    CHECK(l15_rc::calcPframeTargetSizeOnePassCbr(rc, cfg) == 9600);
}

TEST_CASE("calc_pframe honours the inter cap and the min_frame floor") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::RcConfig cfg = cfgBase();
    rc.bufferLevel = 900000;
    cfg.maxInterBitratePct = 50;
    CHECK(l15_rc::calcPframeTargetSizeOnePassCbr(rc, cfg) == 5000);

    rc.avgFrameBandwidth = 200;
    rc.optimalBufferLevel = 100000;
    rc.bufferLevel = 0;
    cfg.underShootPct = 100;
    cfg.maxInterBitratePct = 0;
    CHECK(l15_rc::calcPframeTargetSizeOnePassCbr(rc, cfg) == 200);
}

TEST_CASE("calc_pframe with diff == 0 returns avg_frame_bandwidth") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::RcConfig cfg = cfgBase();
    rc.avgFrameBandwidth = 1000;
    rc.bufferLevel = rc.optimalBufferLevel; // the premise: diff == 0
    CHECK(l15_rc::calcPframeTargetSizeOnePassCbr(rc, cfg) == 1000);
}

TEST_CASE("reset_update_frame_target resets the bucket then recomputes") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::RcConfig cfg = cfgBase();
    l15_rc::PcsCarrier pcs = pcsBase();
    rc.bufferLevel = 999;
    rc.bitsOffTarget = 1;
    l15_rc::resetUpdateFrameTarget(rc, cfg, pcs);
    CHECK(rc.bufferLevel == 1000000);
    CHECK(rc.bitsOffTarget == 1000000);
    CHECK(pcs.thisFrameTarget == 10000);
}

TEST_CASE("calc_iframe_target_size_one_pass_cbr first frame and boost ramp") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::RcConfig cfg = cfgBase();
    l15_rc::PcsCarrier pcs = pcsBase();

    pcs.pictureNumber = 0;
    rc.maxFrameBandwidth = 2147483647; // observe the starting/2 pin, not the buffer clamp
    CHECK(l15_rc::calcIframeTargetSizeOnePassCbr(rc, cfg, pcs, 30.0) == 1000000);

    // starting/2 overflows int: pinned to INT_MAX by the reference ternary.
    rc.startingBufferLevel = 5000000000LL;
    CHECK(l15_rc::calcIframeTargetSizeOnePassCbr(rc, cfg, pcs, 30.0) == 2147483647);
    rc.startingBufferLevel = 2000000;
    rc.maxFrameBandwidth = 50000;

    pcs.pictureNumber = 10;
    rc.framesSinceKey = 5;
    cfg.maxIntraBitratePct = 100;
    CHECK(l15_rc::calcIframeTargetSizeOnePassCbr(rc, cfg, pcs, 30.0) == 10000);

    rc.framesSinceKey = 1;
    cfg.maxIntraBitratePct = 0;
    CHECK(l15_rc::calcIframeTargetSizeOnePassCbr(rc, cfg, pcs, 30.0) == 11250);

    // fsk == framerate/2 exactly: the ramp does not apply.
    rc.framesSinceKey = 15;
    rc.maxFrameBandwidth = 15000;  // forces the max_frame_bandwidth clamp
    CHECK(l15_rc::calcIframeTargetSizeOnePassCbr(rc, cfg, pcs, 30.0) == 15000);

    rc.maxFrameBandwidth = 50000;
    rc.framesSinceKey = 1;  // the comment's premise: fsk=1 at fr=10
    CHECK(l15_rc::calcIframeTargetSizeOnePassCbr(rc, cfg, pcs, 10.0) == 13750);
}

TEST_CASE("rc_clamp_iframe_target_size applies both caps in order") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::RcConfig cfg = cfgBase();
    CHECK(l15_rc::rcClampIframeTargetSize(rc, cfg, 60000) == 50000);
    cfg.maxIntraBitratePct = 100;
    CHECK(l15_rc::rcClampIframeTargetSize(rc, cfg, 60000) == 10000);
    cfg.maxIntraBitratePct = 0;
    CHECK(l15_rc::rcClampIframeTargetSize(rc, cfg, 700) == 700);
}

TEST_CASE("set_rc_buffer_sizes multiplies ms by bandwidth and divides by 1000") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::RcConfig cfg = cfgBase();
    cfg.startingBufferLevelMs = 5000;
    cfg.optimalBufferLevelMs = 10000;
    cfg.maximumBufferSizeMs = 20000;
    l15_rc::setRcBufferSizes(rc, cfg, 3000000);
    CHECK(rc.startingBufferLevel == 15000000);
    CHECK(rc.optimalBufferLevel == 30000000);
    CHECK(rc.maximumBufferSize == 60000000);

    // optimal/maximum == 0 fall back to bandwidth/8.
    cfg.optimalBufferLevelMs = 0;
    cfg.maximumBufferSizeMs = 0;
    l15_rc::setRcBufferSizes(rc, cfg, 3000000);
    CHECK(rc.optimalBufferLevel == 375000);
    CHECK(rc.maximumBufferSize == 375000);
    CHECK(rc.startingBufferLevel == 15000000);
}

TEST_CASE("update_buffer_level leaky bucket both showable forms and clip") {
    l15_rc::RcControl rc = rcBase();
    l15_rc::PcsCarrier pcs = pcsBase();
    rc.bitsOffTarget = 1000;
    l15_rc::updateBufferLevel(rc, pcs, 4000);
    CHECK(rc.bitsOffTarget == 7000);
    CHECK(rc.bufferLevel == 7000);

    pcs.showableFrame = false;
    l15_rc::updateBufferLevel(rc, pcs, 4000);
    CHECK(rc.bitsOffTarget == 3000);
    CHECK(rc.bufferLevel == 3000);

    rc.bitsOffTarget = 49000;
    rc.maximumBufferSize = 50000;
    pcs.showableFrame = true;
    l15_rc::updateBufferLevel(rc, pcs, 0);
    CHECK(rc.bitsOffTarget == 50000);
    CHECK(rc.bufferLevel == 50000);
}
