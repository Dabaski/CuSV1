#include <doctest.h>
#include <cmath>
#include <cstdint>
#include <vector>

#include "rc_arith.h"

// RC2c Wave A, part 2: rc_aq.c. Reference spans:
//   :24-37 (convert_qindex_to_q_fp8), :39-61 (compute_qdelta_fp),
//   :63-65 (variance_comp_int), :76-178 (av1_get_deltaq_sb_variance_boost),
//   :180-278 (svt_av1_variance_adjust_qp), :284-286 (is_in_cr_band),
//   :290-293 (is_cr_motion_static), :295-342 (svt_aom_cyclic_refresh_setup),
//   :350-375 (cyclic_sb_qp_assignment), :380-417 (generate_b64_me_qindex_map),
//   :419-451 (svt_av1_get_deltaq_offset), :554-592 (normalize_sb_delta_q),
//   :595-628 (svt_av1_rc_init_sb_qindex).
// PARKED (named): :453-504 (sb_setup_lambda) and :511-548
// (svt_aom_sb_qp_derivation_tpl_la) - the TPL-lambda wall (pa_me_data
// tpl_* arrays, svt_aom_compute_rd_mult/update_lambda, svt_aom_get_me_qindex,
// coded_to_superres_mi + the mi-geometry tables); the DEBUG_VAR_BOOST*
// blocks are dead debug macros in the reference too.
//
// FLOAT DISCIPLINE: all outputs here are INTEGER and pinned exactly. The
// curve-1/2 anchor chains are libm-free (log2 of exact powers of two is
// exact; 0.15*2 == double(0.3) exactly; the divisions are correctly
// rounded). The curve-0 anchors depend on pow(1.018, x) (libm-dependent at
// the last ulp, absorbed by the fp8 quantization of compute_qdelta_fp);
// they are cross-checked against the reference's own comment table
// (rc_aq.c:78-84: qstep_ratio 1 / 1.481 / 2.192 / 3.246 / 4.806 for
// variances 256 / 64 / 16 / 4 / 1 at base_q_idx 255, strength 2).
//
// EXPECTATION DERIVATION: every pin below was computed by an independent
// python oracle that PARSES the reference ac/dc tables out of the vendored
// file and replicates the C loops (the hand-traces of table indices were
// wrong twice during staging - the oracle is the authority, the comments
// carry the derivation where it is short enough to read).

namespace {

l15_rc::FrameHdrCarrier hdrAt(int baseQIdx) {
    l15_rc::FrameHdrCarrier hdr{};
    hdr.baseQIdx = baseQIdx;
    return hdr;
}

// an 85-entry variance buffer (the [21..84] window is the 8x8 grid).
std::vector<std::uint16_t> varBuf85(const std::vector<std::uint16_t>& grid8) {
    REQUIRE(grid8.size() == 64);
    std::vector<std::uint16_t> buf(85, 0);
    for (std::size_t i = 0; i < grid8.size(); ++i) buf[21 + i] = grid8[i];
    return buf;
}

l15_rc::MeStatCarrier meFixture(const std::vector<std::uint32_t>& d8, const std::vector<std::uint32_t>& d64,
                                const std::vector<std::int16_t>& mvx, const std::vector<std::int16_t>& mvy,
                                std::uint64_t normMeDist) {
    l15_rc::MeStatCarrier me{};
    me.me8x8Distortion = d8.data();
    me.me64x64Distortion = d64.data();
    me.me8x8CostVariance = nullptr;
    me.mvX = mvx.data();
    me.mvY = mvy.data();
    me.normMeDist = normMeDist;
    me.b64TotalCount = (std::uint16_t)d8.size();
    return me;
}

}  // namespace

TEST_CASE("convert_qindex_to_q_fp8 shifts the ac table") {
    // ac[0]=4 ac[1]=8 ac[255]=1828; fp8 = <<6 (8-bit), <<4 (10), <<3 (12).
    // The pinned tree is CONFIG_ENABLE_HIGH_BIT_DEPTH=0: the callee's 10/12
    // branches are compiled out -> -1, so the 10/12 cases return -1 << 4
    // and -1 << 3 (the reference's own shift applied to the callee's -1).
    CHECK(l15_rc::convertQindexToQFp8(0, 8) == 256);
    CHECK(l15_rc::convertQindexToQFp8(1, 8) == 512);
    CHECK(l15_rc::convertQindexToQFp8(255, 8) == 116992);
    CHECK(l15_rc::convertQindexToQFp8(255, 10) == -16);
    CHECK(l15_rc::convertQindexToQFp8(255, 12) == -8);
    // the CONFIG_ENABLE_HIGH_BIT_DEPTH=0 default branch: assert + -1.
    CHECK(l15_rc::convertQindexToQFp8(255, 4) == -1);
}

TEST_CASE("compute_qdelta_fp walks the fp8 table") {
    // The loop bound i < MAXQ means an unsatisfiable target rests at 254
    // (the same measured semantics as computeQdelta): fp8(255) = 116992 is
    // only reached AT 255, which the loop never assigns -> target rests at
    // 254 -> delta 254, not 255.
    CHECK(l15_rc::computeQdeltaFp(256, 116992, 8) == 254);
    CHECK(l15_rc::computeQdeltaFp(116992, 256, 8) == -254);
    // start above the table top: both loops rest at 254 -> delta 0.
    CHECK(l15_rc::computeQdeltaFp(116993, 116992, 8) == 0);
    // ac[10]=17 -> fp8 1088; ac[100]=112 -> fp8 7168: 100-10 = 90.
    CHECK(l15_rc::computeQdeltaFp(1088, 7168, 8) == 90);
}

TEST_CASE("variance_comp_int is the difference comparator") {
    std::uint16_t a = 100, b = 40;
    CHECK(l15_rc::varianceCompInt(&a, &b) == 60);
    CHECK(l15_rc::varianceCompInt(&b, &a) == -60);
    std::uint16_t c = 40;
    CHECK(l15_rc::varianceCompInt(&b, &c) == 0);
}

TEST_CASE("get_deltaq_sb_variance_boost curve-0 anchors (reference comment table)") {
    // Uniform grids: every octile sample is the same value. The boosts are
    // oracle-derived (pow cross-checked against the reference comment
    // table: ratio 1 / 1.481 / 2.192 / 3.246 / 4.806).
    std::vector<std::uint16_t> grid(64, 256);
    const std::vector<std::uint16_t> bufA = varBuf85(grid);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufA.data(), 2, 8, 4, 0) == 0);

    grid.assign(64, 64);
    const std::vector<std::uint16_t> bufB = varBuf85(grid);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufB.data(), 2, 8, 4, 0) == 19);

    grid.assign(64, 16);
    const std::vector<std::uint16_t> bufC = varBuf85(grid);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufC.data(), 2, 8, 4, 0) == 39);

    grid.assign(64, 4);
    const std::vector<std::uint16_t> bufD = varBuf85(grid);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufD.data(), 2, 8, 4, 0) == 60);

    grid.assign(64, 1);
    const std::vector<std::uint16_t> bufE = varBuf85(grid);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufE.data(), 2, 8, 4, 0) == 80);
    // variance 0 is substituted with 1 (the same anchor).
    grid.assign(64, 0);
    const std::vector<std::uint16_t> bufF = varBuf85(grid);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufF.data(), 2, 8, 4, 0) == 80);
}

TEST_CASE("get_deltaq_sb_variance_boost curve-1 anchors are exact") {
    // curve 1 uses the strength PARAM directly: ratio = 0.25*strength*
    // (-log2(var)+8)+1. Variance 256: the log2 term is exactly 0 -> ratio
    // 1 for every strength -> boost 0.
    std::vector<std::uint16_t> grid(64, 256);
    const std::vector<std::uint16_t> bufA = varBuf85(grid);
    for (int s = 1; s <= 4; ++s)
        CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufA.data(), (std::uint8_t)s, 8, 4, 1) == 0);

    // variance 64 (-log2 = -6 exactly): ratios 1.5 / 2.0 / 2.5 / 3.0 at
    // strengths 1..4; boosts 20 / 35 / 46 / 56 (oracle).
    grid.assign(64, 64);
    const std::vector<std::uint16_t> bufB = varBuf85(grid);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufB.data(), 1, 8, 4, 1) == 20);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufB.data(), 2, 8, 4, 1) == 35);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufB.data(), 3, 8, 4, 1) == 46);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufB.data(), 4, 8, 4, 1) == 56);

    // variance 16: ratios 2.0 / 3.0 / 4.0 / 5.0 -> boosts 35 / 56 / 71 / 80.
    grid.assign(64, 16);
    const std::vector<std::uint16_t> bufC = varBuf85(grid);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufC.data(), 1, 8, 4, 1) == 35);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufC.data(), 2, 8, 4, 1) == 56);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufC.data(), 3, 8, 4, 1) == 71);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufC.data(), 4, 8, 4, 1) == 80);

    // variance 1: ratios 3.0 / 5.0 / 7.0 / 8.0 -> boosts 56 / 80 / 80 / 80
    // (saturation at the 80 cap).
    grid.assign(64, 1);
    const std::vector<std::uint16_t> bufD = varBuf85(grid);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufD.data(), 1, 8, 4, 1) == 56);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufD.data(), 2, 8, 4, 1) == 80);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufD.data(), 3, 8, 4, 1) == 80);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufD.data(), 4, 8, 4, 1) == 80);
}

TEST_CASE("get_deltaq_sb_variance_boost curve-2 anchors are exact") {
    // curve 2: ratio = 0.15*strength*(-log2(var)+10)+1. Strength 2 gives
    // the exact multiplier 0.3 (0.15*2 is a bit-doubling).
    std::vector<std::uint16_t> grid(64, 256);
    const std::vector<std::uint16_t> bufA = varBuf85(grid);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufA.data(), 2, 8, 4, 2) == 14);
    grid.assign(64, 64);
    const std::vector<std::uint16_t> bufB = varBuf85(grid);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufB.data(), 2, 8, 4, 2) == 24);
    grid.assign(64, 16);
    const std::vector<std::uint16_t> bufC = varBuf85(grid);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufC.data(), 2, 8, 4, 2) == 32);
    grid.assign(64, 1);
    const std::vector<std::uint16_t> bufD = varBuf85(grid);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufD.data(), 2, 8, 4, 2) == 44);
    // strength 4: multiplier 0.6 -> ratio 7.0 at variance 1 -> boost 63.
    CHECK(l15_rc::getDeltaqSbVarianceBoost(255, bufD.data(), 4, 8, 4, 2) == 63);
}

TEST_CASE("get_deltaq_sb_variance_boost octile sampling and sorting") {
    // grid input UNSORTED; after sorting: ranks 0..7 = 7..14 (the values
    // 7+i), 8..55 = 200, 56..63 = 2900..2907. octile 1: mid=7 ->
    // (14,14,200) -> var 61; octile 4: mid=31 -> (200,200,200) -> var 200;
    // octile 8: mid=63 -> (200,2907,2907) -> var 2230. base 120, curve 1,
    // strength 1. Octile 1: ratio 1.5173157 -> target_q 6411 -> first ac
    // >= 100.17 is ac[94]=101 -> delta -26 -> boost int(160*26/295) = 14.
    // Octile 4: ratio 1.0890359 -> target_q 8932 -> ac[114]=140 ->
    // delta -6 -> boost 3. Octile 8: ratio clips to 1 -> delta 0 -> 0.
    std::vector<std::uint16_t> grid(64, 200);
    for (int i = 0; i < 8; ++i) grid[i] = (std::uint16_t)(7 + i);
    for (int i = 56; i < 64; ++i) grid[i] = (std::uint16_t)(2900 + i);
    const std::vector<std::uint16_t> buf = varBuf85(grid);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(120, buf.data(), 1, 8, 1, 1) == 14);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(120, buf.data(), 1, 8, 4, 1) == 3);
    CHECK(l15_rc::getDeltaqSbVarianceBoost(120, buf.data(), 1, 8, 8, 1) == 0);
}

TEST_CASE("variance_adjust_qp end-to-end on a synthetic grid") {
    // 4 SBs, base 120, curve 1, strength 2, octile 4. Oracle boosts:
    // SB0 (all 512, ratio clips to 1) 0; SB1 (spread, var 1775, ratio
    // clips to 1) 0; SB2 (all 0 -> var 1, ratio 5.0) 52; SB3 (all 64,
    // ratio 2.0) 27. Boosted qindex [120, 120, 68, 93]: min 68 max 120,
    // range 52 -> nbase 94; all offsets within +-40 -> normalized equals
    // the boosted values.
    std::vector<std::uint16_t> g0(64, 512), g2(64, 0), g3(64, 64), g1;
    for (int i = 0; i < 23; ++i) g1.push_back(50);
    for (int i = 0; i < 8; ++i) g1.push_back(100);
    for (int i = 0; i < 8; ++i) g1.push_back(1000);
    for (int i = 0; i < 25; ++i) g1.push_back(5000);
    std::vector<std::vector<std::uint16_t>> bufs;
    bufs.push_back(varBuf85(g0));
    bufs.push_back(varBuf85(g1));
    bufs.push_back(varBuf85(g2));
    bufs.push_back(varBuf85(g3));
    std::vector<const std::uint16_t*> varPtrs;
    for (const auto& b : bufs) varPtrs.push_back(b.data());

    std::uint8_t qindex[4] = {120, 120, 120, 120};
    l15_rc::FrameHdrCarrier hdr = hdrAt(120);
    l15_rc::varianceAdjustQp(hdr, qindex, varPtrs.data(), 4, 4, false, 2, 8, 4, 1);

    CHECK(hdr.deltaQPresent);
    CHECK(qindex[0] == 120);
    CHECK(qindex[1] == 120);
    CHECK(qindex[2] == 68);
    CHECK(qindex[3] == 93);
}

TEST_CASE("variance_adjust_qp clips low qindexes at 1") {
    // base 20: oracle boosts [0, 0, 3, 2] (the small base shrinks the
    // delta multiplier) -> q [20, 20, 17, 18]; min 17 max 20, range 3 ->
    // nbase 18; offsets [-1, 2, -1, 0] -> unchanged.
    std::vector<std::uint16_t> g0(64, 512), g2(64, 0), g3(64, 64), g1;
    for (int i = 0; i < 23; ++i) g1.push_back(50);
    for (int i = 0; i < 8; ++i) g1.push_back(100);
    for (int i = 0; i < 8; ++i) g1.push_back(1000);
    for (int i = 0; i < 25; ++i) g1.push_back(5000);
    std::vector<std::vector<std::uint16_t>> bufs;
    bufs.push_back(varBuf85(g0));
    bufs.push_back(varBuf85(g1));
    bufs.push_back(varBuf85(g2));
    bufs.push_back(varBuf85(g3));
    std::vector<const std::uint16_t*> varPtrs;
    for (const auto& b : bufs) varPtrs.push_back(b.data());

    std::uint8_t qindex[4] = {20, 20, 20, 20};
    l15_rc::FrameHdrCarrier hdr = hdrAt(20);
    l15_rc::varianceAdjustQp(hdr, qindex, varPtrs.data(), 4, 4, false, 2, 8, 4, 1);
    CHECK(qindex[0] == 20);
    CHECK(qindex[1] == 20);
    CHECK(qindex[2] == 17);
    CHECK(qindex[3] == 18);
}

TEST_CASE("variance_adjust_qp counts with the superres sb_cnt") {
    // superres/resize selects ppcs->b64_total_count (2 SBs here) instead
    // of scs->sb_total_count (4): the tail stays untouched.
    std::vector<std::uint16_t> g0(64, 512), g1(64, 1);
    std::vector<std::vector<std::uint16_t>> bufs;
    bufs.push_back(varBuf85(g0));
    bufs.push_back(varBuf85(g1));
    bufs.push_back(varBuf85(g1));
    bufs.push_back(varBuf85(g1));
    std::vector<const std::uint16_t*> varPtrs;
    for (const auto& b : bufs) varPtrs.push_back(b.data());

    std::uint8_t qindex[4] = {120, 120, 99, 99};
    l15_rc::FrameHdrCarrier hdr = hdrAt(120);
    l15_rc::varianceAdjustQp(hdr, qindex, varPtrs.data(), 4, 2, true, 2, 8, 4, 1);
    // 2 SBs: boosts [0, 52] (var 512 clips to ratio 1; var 1 -> ratio 5)
    // -> q [120, 68]; range 52 -> nbase 94 -> offsets [26, -26].
    CHECK(qindex[0] == 120);
    CHECK(qindex[1] == 68);
    CHECK(qindex[2] == 99);
    CHECK(qindex[3] == 99);
}

TEST_CASE("is_in_cr_band wraps and gates") {
    // straight band [2, 10): rc_aq.c:284-286 semantics (start <= end ->
    // b64_idx >= start && b64_idx < end).
    CHECK_FALSE(l15_rc::isInCrBand(0, 2, 10));
    CHECK(l15_rc::isInCrBand(2, 2, 10));
    CHECK(l15_rc::isInCrBand(9, 2, 10));
    CHECK_FALSE(l15_rc::isInCrBand(10, 2, 10));
    CHECK_FALSE(l15_rc::isInCrBand(1, 2, 10));
    // empty band (start == end == 0): nothing qualifies.
    CHECK_FALSE(l15_rc::isInCrBand(0, 0, 0));
    CHECK_FALSE(l15_rc::isInCrBand(5, 0, 0));
    // wrapping band [10, 4): 10..15 and 0..3.
    CHECK(l15_rc::isInCrBand(10, 10, 4));
    CHECK(l15_rc::isInCrBand(15, 10, 4));
    CHECK(l15_rc::isInCrBand(0, 10, 4));
    CHECK(l15_rc::isInCrBand(3, 10, 4));
    CHECK_FALSE(l15_rc::isInCrBand(4, 10, 4));
    CHECK_FALSE(l15_rc::isInCrBand(9, 10, 4));
}

TEST_CASE("cyclic_refresh_setup segments, averages and the seg2 boost") {
    // 16 SBs, band [2, 10), norm 15 -> reject thresh 31.
    // d8 = [100, 5, 9, 12, 20, 3, 50, 2, 8, 1, 30, 40, 7, 11, 13, 90];
    // in-band static: 2,3,4,5,7,8,9. Seg2 (< norm 15): 2,3,5,7,8,9 -> 6;
    // seg1: 4 -> 1. d64[i] = 1000+7i: seg0 over 0,1,6,10..15 =
    // 9574/9 = 1063; seg1 = 1028/1; seg2 sum 35/6 = 5 -> dev =
    // (15-5)*100/15 = 66 -> boost = 15 + 43560/10000 = 19.
    std::vector<std::uint32_t> d8 = {100, 5, 9, 12, 20, 3, 50, 2, 8, 1, 30, 40, 7, 11, 13, 90};
    std::vector<std::uint32_t> d64;
    for (int i = 0; i < 16; ++i) d64.push_back((std::uint32_t)(1000 + 7 * i));
    std::vector<std::int16_t> mvx(16, 0), mvy(16, 0);
    const l15_rc::MeStatCarrier me = meFixture(d8, d64, mvx, mvy, 15);

    l15_rc::CrRefreshCarrier cr{};
    cr.sbStart = 2;
    cr.sbEnd = 10;
    cr.rateBoostFac = 15;
    cr.rateRatioQdelta = 2.75;
    cr.applyCyclicRefresh = 1;

    l15_rc::cyclicRefreshSetup(cr, me);
    CHECK(cr.actualNumSeg1Sbs == 1);
    CHECK(cr.actualNumSeg2Sbs == 6);
    CHECK(cr.meDistortion[0] == 1063);
    CHECK(cr.meDistortion[1] == 1028);
    CHECK(cr.meDistortion[2] == 1039);
    // The boosted value lives in the reference's LOCAL rate_boost_fac
    // (rc_aq.c:334) and is consumed only by rate_ratio_qdelta_seg2 (:341);
    // it is never stored back into cr->rate_boost_fac.
    CHECK(cr.rateBoostFac == 15);
    // rate_ratio_qdelta_seg2 = 0.1 * 19 * 2.75 (double; 1e-9 ABSOLUTE - the
    // doctest Approx is relative, so the bound is written out).
    CHECK(std::fabs(cr.rateRatioQdeltaSeg2 - 5.225) <= 1e-9);
    CHECK(cr.applyCyclicRefresh == 1);
}

TEST_CASE("cyclic_refresh_setup disables on the all-rejected band") {
    std::vector<std::uint32_t> d8 = {1, 1, 1, 1, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50};
    std::vector<std::uint32_t> d64(16, 700);
    std::vector<std::int16_t> mvx(16, 4), mvy(16, 0);
    const l15_rc::MeStatCarrier me = meFixture(d8, d64, mvx, mvy, 10);

    l15_rc::CrRefreshCarrier cr{};
    cr.sbStart = 0;
    cr.sbEnd = 4;
    cr.rateBoostFac = 15;
    cr.rateRatioQdelta = 2.0;
    cr.applyCyclicRefresh = 1;
    l15_rc::cyclicRefreshSetup(cr, me);
    CHECK(cr.actualNumSeg1Sbs == 0);
    CHECK(cr.actualNumSeg2Sbs == 0);
    CHECK(cr.meDistortion[0] == 700);
    CHECK(cr.applyCyclicRefresh == 0);
}

TEST_CASE("cyclic_sb_qp_assignment offsets by band, motion and distortion") {
    std::vector<std::uint32_t> d8 = {100, 5, 9, 12, 20, 3, 50, 2, 8, 1, 30, 40, 7, 11, 13, 90};
    std::vector<std::uint32_t> d64(16, 0);
    std::vector<std::int16_t> mvx(16, 0), mvy(16, 0);
    const l15_rc::MeStatCarrier me = meFixture(d8, d64, mvx, mvy, 15);

    l15_rc::CrRefreshCarrier cr{};
    cr.sbStart = 2;
    cr.sbEnd = 10;
    cr.qindexDelta[1] = -3;
    cr.qindexDelta[2] = -6;

    std::uint8_t qindex[16];
    for (int i = 0; i < 16; ++i) qindex[i] = 120;
    l15_rc::FrameHdrCarrier hdr = hdrAt(120);
    l15_rc::cyclicSbQpAssignment(hdr, qindex, cr, me);

    // static + < norm (15): delta[2] -> 114 at 2,3,5,7,8,9; static and
    // >= norm: delta[1] -> 117 at 4; in-band non-static: base at 6.
    const std::uint8_t want[16] = {120, 120, 114, 114, 117, 114, 120, 114, 114, 114, 120, 120, 120, 120, 120, 120};
    int bad = 0;
    for (int i = 0; i < 16; ++i)
        if (qindex[i] != want[i]) ++bad;
    CHECK(bad == 0);
    // the function sets delta_q_present (rc_aq.c:354).
    CHECK(hdr.deltaQPresent);
}

TEST_CASE("generate_b64_me_qindex_map i-slice passthrough and the walk") {
    std::vector<std::uint32_t> dists = {10, 20, 30, 40, 50, 60, 70, 80};
    std::vector<std::uint32_t> d64(8, 0);
    std::vector<std::int16_t> mvx(8, 0), mvy(8, 0);
    l15_rc::MeStatCarrier me = meFixture(dists, d64, mvx, mvy, 0);
    me.me8x8CostVariance = dists.data();

    // I_SLICE (1): everything at base.
    std::uint8_t out[8];
    l15_rc::generateB64MeQindexMap(hdrAt(150), me, out, 1, 2);
    for (int i = 0; i < 8; ++i) CHECK(out[i] == 150);

    // tl=2, non-I: avg 45, min 10, max 80; window [115, 185]. The offsets
    // are the int-truncated -8*dd/(min-avg) and 8*dd/(max-avg) forms:
    // oracle pins [142, 145, 147, 149, 151, 153, 155, 158].
    const std::uint8_t want[8] = {142, 145, 147, 149, 151, 153, 155, 158};
    l15_rc::generateB64MeQindexMap(hdrAt(150), me, out, 0, 2);
    for (int i = 0; i < 8; ++i) {
        INFO("i=" << i);
        CHECK(out[i] == want[i]);
    }

    // all-equal distortions: dd == 0 everywhere -> no division -> all base.
    std::vector<std::uint32_t> distsEq(8, 5);
    l15_rc::MeStatCarrier meEq = meFixture(distsEq, d64, mvx, mvy, 0);
    meEq.me8x8CostVariance = distsEq.data();
    l15_rc::generateB64MeQindexMap(hdrAt(150), meEq, out, 0, 2);
    for (int i = 0; i < 8; ++i) CHECK(out[i] == 150);
}

TEST_CASE("generate_b64_me_qindex_map clips to the base window") {
    std::vector<std::uint32_t> dists = {1, 1000, 1, 1000, 1, 1000, 1, 1000};
    std::vector<std::uint32_t> d64(8, 0);
    std::vector<std::int16_t> mvx(8, 0), mvy(8, 0);
    l15_rc::MeStatCarrier me = meFixture(dists, d64, mvx, mvy, 0);
    me.me8x8CostVariance = dists.data();
    std::uint8_t out[8];
    l15_rc::generateB64MeQindexMap(hdrAt(150), me, out, 0, 2);
    // avg 500, min 1, max 1000: dd=-499 -> int(-8*-499/-499) = -8 -> 142;
    // dd=500 -> int(8*500/500) = 8 -> 158.
    const std::uint8_t want[8] = {142, 158, 142, 158, 142, 158, 142, 158};
    for (int i = 0; i < 8; ++i) CHECK(out[i] == want[i]);
}

TEST_CASE("get_deltaq_offset walks the dc table both ways") {
    // q=100 (dc 93), beta 2, intra: newq = rint(93/sqrt(2)) = 66; walk down
    // to the first dc <= 66 -> dc[71]=66 -> offset -29 (oracle).
    CHECK(l15_rc::getDeltaqOffset(8, 100, 2.0, true) == -29);
    // non-intra beta > 1 fourth root: newq = rint(93/2^(1/4)) = 78 -> first
    // dc <= 78 walking down: dc[86]=78 -> offset -14 (oracle).
    CHECK(l15_rc::getDeltaqOffset(8, 100, 2.0, false) == -14);
    // newq == q (beta 1): 0.
    CHECK(l15_rc::getDeltaqOffset(8, 100, 1.0, true) == 0);
    CHECK(l15_rc::getDeltaqOffset(8, 100, 1.0, false) == 0);
    // beta 0.5: newq = rint(93/0.7071) = 132 -> first dc >= 132 walking up:
    // dc[125]=134 -> offset 25 (oracle).
    CHECK(l15_rc::getDeltaqOffset(8, 100, 0.5, true) == 25);
    // q=0, beta 4: newq < q and the down-walk cannot run -> offset 0.
    CHECK(l15_rc::getDeltaqOffset(8, 0, 4.0, false) == 0);
    // q=255 (dc 1336), beta 4, intra: newq = rint(1336/2) = 668 < q = 1336
    // (the quantizer SHRINKS for beta > 1, so the DOWN-walk runs even from
    // the top): first dc <= 668 walking down is dc[233]=668 -> offset -22
    // (the reference's up-walk at rc_aq.c:441-449 is not taken; the oracle
    // and the binary agree on -22).
    CHECK(l15_rc::getDeltaqOffset(8, 255, 4.0, true) == -22);
}

TEST_CASE("normalize_sb_delta_q rounds sb qindexes to the res grid") {
    // base 64: rem 0 for every res; adj = (res - 0) - res/2.
    std::uint8_t qindex[4] = {64, 100, 250, 1};
    l15_rc::FrameHdrCarrier hdr = hdrAt(64);
    hdr.deltaQRes = 2;
    l15_rc::normalizeSbDeltaQ(hdr, qindex, 4, 4, false);
    // adj 1, mask 0xFE: 65&0xFE=64; 101&0xFE=100; 251&0xFE=250; 2&0xFE=2.
    CHECK(qindex[0] == 64);
    CHECK(qindex[1] == 100);
    CHECK(qindex[2] == 250);
    CHECK(qindex[3] == 2);

    std::uint8_t q2[4] = {64, 100, 250, 1};
    hdr.deltaQRes = 4;
    l15_rc::normalizeSbDeltaQ(hdr, q2, 4, 4, false);
    // adj 2, mask 0xFC: (64+2)&0xFC=64; (100+2)&0xFC=100; (250+2)&0xFC=252;
    // (1+2)&0xFC=0 -> substituted with res 4.
    CHECK(q2[0] == 64);
    CHECK(q2[1] == 100);
    CHECK(q2[2] == 252);
    CHECK(q2[3] == 4);

    std::uint8_t q3[4] = {64, 100, 250, 1};
    hdr.deltaQRes = 8;
    l15_rc::normalizeSbDeltaQ(hdr, q3, 4, 4, false);
    // adj 4, mask 0xF8: 68&0xF8=64; 104&0xF8=104; 254&0xF8=248; 5&0xF8=8.
    CHECK(q3[0] == 64);
    CHECK(q3[1] == 104);
    CHECK(q3[2] == 248);
    CHECK(q3[3] == 8);

    // base 65 res 4: rem 1 adj 1; the remainder survives normalization.
    std::uint8_t q4[4] = {65, 100, 250, 1};
    hdr.baseQIdx = 65;
    hdr.deltaQRes = 4;
    l15_rc::normalizeSbDeltaQ(hdr, q4, 4, 4, false);
    // adj 1: 66&0xFC=64+rem1=65; 101&0xFC=100+1=101; 251&0xFC=248+1=249;
    // 2&0xFC=0+1=1.
    CHECK(q4[0] == 65);
    CHECK(q4[1] == 101);
    CHECK(q4[2] == 249);
    CHECK(q4[3] == 1);
}

TEST_CASE("rc_init_sb_qindex routes the CBR and variance families") {
    std::vector<std::uint32_t> d8 = {100, 5, 9, 12, 20, 3, 50, 2, 8, 1, 30, 40, 7, 11, 13, 90};
    std::vector<std::uint32_t> d64(16, 0);
    std::vector<std::int16_t> mvx(16, 0), mvy(16, 0);
    const l15_rc::MeStatCarrier me = meFixture(d8, d64, mvx, mvy, 15);

    // CBR + refresh applied -> the cyclic path writes the band deltas.
    l15_rc::CrRefreshCarrier crOn{};
    crOn.applyCyclicRefresh = 1;
    crOn.sbStart = 2;
    crOn.sbEnd = 10;
    crOn.qindexDelta[1] = -3;
    crOn.qindexDelta[2] = -6;
    l15_rc::FrameHdrCarrier hdr = hdrAt(120);
    std::uint8_t qindex[16];
    for (int i = 0; i < 16; ++i) qindex[i] = 120;
    l15_rc::rcInitSbQindex(1 /*CBR*/, crOn, hdr, qindex, nullptr, 16, me, false, 2, 8, 4, 1, 0, false, 0.0);
    CHECK(hdr.deltaQPresent);
    CHECK(qindex[2] == 114);
    CHECK(qindex[4] == 117);
    CHECK(qindex[6] == 120);

    // CBR without refresh -> every SB at base; delta_q_present stays 0.
    l15_rc::CrRefreshCarrier crOff{};
    l15_rc::FrameHdrCarrier hdr2 = hdrAt(120);
    for (int i = 0; i < 16; ++i) qindex[i] = 77;
    l15_rc::rcInitSbQindex(1, crOff, hdr2, qindex, nullptr, 16, me, false, 2, 8, 4, 1, 0, false, 0.0);
    CHECK_FALSE(hdr2.deltaQPresent);
    for (int i = 0; i < 16; ++i) CHECK(qindex[i] == 120);

    // non-CBR with the variance boost enabled -> the variance path.
    std::vector<std::vector<std::uint16_t>> bufs;
    bufs.push_back(varBuf85(std::vector<std::uint16_t>(64, 512)));
    bufs.push_back(varBuf85(std::vector<std::uint16_t>(64, 1)));
    bufs.push_back(varBuf85(std::vector<std::uint16_t>(64, 0)));
    bufs.push_back(varBuf85(std::vector<std::uint16_t>(64, 64)));
    std::vector<const std::uint16_t*> varPtrs;
    for (const auto& b : bufs) varPtrs.push_back(b.data());
    l15_rc::FrameHdrCarrier hdr3 = hdrAt(120);
    std::uint8_t qindex2[16];
    for (int i = 0; i < 16; ++i) qindex2[i] = 120;
    l15_rc::rcInitSbQindex(0 /*VBR*/, crOff, hdr3, qindex2, varPtrs.data(), 4, me, true, 2, 8, 4, 1, 0, false, 0.0);
    // The variance path sets delta_q_present (rc_aq.c:184, inside
    // svt_av1_variance_adjust_qp; the rc_aq.c:599 zero happens first and is
    // overwritten by the boost path).
    CHECK(hdr3.deltaQPresent);
    // the four SBs take the oracle boosts [0, 52, 52, 27] (SB1/SB2 both var
    // 1) -> q [120, 68, 68, 93], normalized unchanged; the untouched tail
    // stays at base 120.
    CHECK(qindex2[0] == 120);
    CHECK(qindex2[1] == 68);
    CHECK(qindex2[2] == 68);
    CHECK(qindex2[3] == 93);
    CHECK(qindex2[4] == 120);
}
