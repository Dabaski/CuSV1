#include <doctest.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>

#include "rc_arith.h"
#include "rc_tables.h"

// RC2c Wave A, part 1: the D-series dependency helpers (the quant-index
// arithmetic every RC body consumes). Reference spans:
//   inv_transforms.c:3412-3427 (dc_qlookup_QTX), :3357-3372 (ac_qlookup_QTX),
//   :3467-3482 (svt_aom_dc_quant_qtx), :3484-3499 (svt_aom_ac_quant_qtx);
//   md_process.c:20-23 (quantizer_to_qindex), :25-27 (percents);
//   rc_process.c:185-341 minus the two r0-boost entries :229-252 kept here,
//   :641-667 (calculate_boost_bits), :55-59 (frame_is_kf_gf_arf);
//   entropy_coding.h:61-64 (frame_is_intra_only);
//   enc_mode_config.h:113-116 (frame_is_leaf);
//   rc_vbr_cbr.c:21-25 (clamp_qindex); rc_crf_cqp.c:22-26 (clamp_qp);
//   rc_vbr_cbr.c:165-166 (max_delta_per_layer).
//
// FLOAT DISCIPLINE (module-wide, per the brief): INTEGER outputs pinned
// EXACTLY. convertQindexToQ's double leaves the API -> 1e-9 ABSOLUTE. The
// pinned tree is CONFIG_ENABLE_HIGH_BIT_DEPTH=0 (EbConfigMacros.h), so for
// bit depths 10/12 the ac/dc callee's table cases are compiled out and the
// value is -1 divided by the scale (-0.0625 / -0.015625 - still exact
// binary fractions). The curve-0 variance-boost anchors (test_rc_aq.cpp)
// use the reference's own comment table for the qstep_ratio and pin the
// integer boost only.
//
// Table expectations are parsed from the REFERENCE FILES at runtime (the
// RC1 mechanical pattern: zero hand-typed table values); function
// expectations are hand-derived (python oracle, values cross-checked
// against the reference comment table and the parsed tables).

namespace {

std::string refText(const char* rel) {
    std::ifstream f(std::string(REPO_ROOT) + rel, std::ios::binary);
    REQUIRE_MESSAGE(f.good(), rel);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::vector<int> tableNumbers(const std::string& text, const std::string& decl, int expected) {
    const std::size_t at = text.find(decl);
    REQUIRE_MESSAGE(at != std::string::npos, decl);
    const std::size_t start = at + decl.size();
    const std::size_t end = text.find("};", start);
    REQUIRE(end != std::string::npos);
    const std::string body = text.substr(start, end - start);
    std::vector<int> ref;
    std::regex numRe(R"(-?\d+)");
    for (auto n = std::sregex_iterator(body.begin(), body.end(), numRe); n != std::sregex_iterator(); ++n)
        ref.push_back(std::atoi(n->str().c_str()));
    REQUIRE((int)ref.size() == expected);
    return ref;
}

}  // namespace

TEST_CASE("quantizer_to_qindex and percents match md_process.c entry-for-entry") {
    const std::string text = refText("/third_party/SVT-AV1/Source/Lib/Codec/md_process.c");

    const std::vector<int> qi =
        tableNumbers(text, "const uint8_t quantizer_to_qindex[64] = {", 64);
    int bad = 0;
    for (int i = 0; i < 64; ++i)
        if (l15_rc::kQuantizerToQindex[i] != qi[i]) ++bad;
    CHECK(bad == 0);

    const std::vector<int> pc =
        tableNumbers(text, "const int percents[2][FIXED_QP_OFFSET_COUNT] = {", 12);
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 6; ++c)
            CHECK(l15_rc::kPercents[r][c] == pc[r * 6 + c]);
}

TEST_CASE("the 8-bit QTX tables match inv_transforms.c entry-for-entry") {
    const std::string text = refText("/third_party/SVT-AV1/Source/Lib/Codec/inv_transforms.c");

    const std::vector<int> ac =
        tableNumbers(text, "static const int16_t ac_qlookup_QTX[QINDEX_RANGE] = {", 256);
    const std::vector<int> dc =
        tableNumbers(text, "static const int16_t dc_qlookup_QTX[QINDEX_RANGE] = {", 256);
    int bad = 0;
    for (int i = 0; i < 256; ++i) {
        if (l15_rc::kAcQLookupQtx[i] != ac[i]) ++bad;
        if (l15_rc::kDcQLookupQtx[i] != dc[i]) ++bad;
    }
    CHECK(bad == 0);
}

TEST_CASE("max_delta_per_layer matches rc_vbr_cbr.c entry-for-entry") {
    const std::string text = refText("/third_party/SVT-AV1/Source/Lib/Codec/rc_vbr_cbr.c");
    const std::vector<int> md =
        tableNumbers(text, "static const int max_delta_per_layer[MAX_HIERARCHICAL_LEVEL][MAX_TEMPORAL_LAYERS] = {", 21);
    int bad = 0;
    for (int r = 0; r < 6; ++r)
        for (int c = 0; c < 6; ++c)
            if (l15_rc::kMaxDeltaPerLayer[r][c] != ((c <= r) ? md[r * (r + 1) / 2 + c] : 0)) ++bad;
    CHECK(bad == 0);
}

TEST_CASE("dc/ac quant qtx read the 8-bit tables with clamped index") {
    // Hand-derived anchors from the reference table prints:
    // dc[0]=4 dc[10]=15 dc[100]=93 dc[255]=1336; ac[0]=4 ac[10]=17 ac[255]=1828.
    CHECK(l15_rc::dcQuantQtx(0, 0, 8) == 4);
    CHECK(l15_rc::dcQuantQtx(10, 0, 8) == 15);
    CHECK(l15_rc::dcQuantQtx(100, 0, 8) == 93);
    CHECK(l15_rc::dcQuantQtx(255, 0, 8) == 1336);
    CHECK(l15_rc::acQuantQtx(0, 0, 8) == 4);
    CHECK(l15_rc::acQuantQtx(10, 0, 8) == 17);
    CHECK(l15_rc::acQuantQtx(255, 0, 8) == 1828);
    // delta shifts the index; the clamp bounds it: q+delta clamped [0,255].
    CHECK(l15_rc::dcQuantQtx(0, 10, 8) == 15);
    CHECK(l15_rc::acQuantQtx(0, 10, 8) == 17);
    CHECK(l15_rc::dcQuantQtx(0, -5, 8) == 4);
    CHECK(l15_rc::dcQuantQtx(250, 10, 8) == 1336);
    // the CONFIG_ENABLE_HIGH_BIT_DEPTH=0 park: 10/12 hit the default branch
    // (assert removed under NDEBUG -> -1).
    CHECK(l15_rc::dcQuantQtx(100, 0, 10) == -1);
    CHECK(l15_rc::acQuantQtx(100, 0, 12) == -1);
}

TEST_CASE("convert_qindex_to_q is the ac table divided by the bit-depth scale") {
    // ac[0]=4, ac[1]=8, ac[255]=1828 -> 1.0 / 2.0 / 457.0 (8-bit: /4, exact
    // powers of two, so the doubles are exact). The pinned tree builds with
    // CONFIG_ENABLE_HIGH_BIT_DEPTH=0 (EbConfigMacros.h), so the callee's
    // bd-10/12 branches are compiled out and ac/dc_quant_qtx fall to the
    // default branch (assert removed under NDEBUG) -> -1; the 10/12 cases
    // of convert_qindex_to_q then return -1/16.0 and -1/64.0. Absolute 1e-9
    // comparisons (the brief's float discipline; doctest's Approx is
    // relative, so the bound is written out).
    CHECK(std::fabs(l15_rc::convertQindexToQ(0, 8) - 1.0) <= 1e-9);
    CHECK(std::fabs(l15_rc::convertQindexToQ(1, 8) - 2.0) <= 1e-9);
    CHECK(std::fabs(l15_rc::convertQindexToQ(255, 8) - 457.0) <= 1e-9);
    CHECK(std::fabs(l15_rc::convertQindexToQ(255, 10) + 0.0625) <= 1e-9);
    CHECK(std::fabs(l15_rc::convertQindexToQ(255, 12) + 0.015625) <= 1e-9);
    // the same -1 default park as the qtx pair.
    CHECK(std::fabs(l15_rc::convertQindexToQ(100, 4) + 1.0) <= 1e-9);
    // monotone non-decreasing across the full range (property).
    double prev = 0.0;
    for (int q = 0; q < 256; ++q) {
        const double cur = l15_rc::convertQindexToQ(q, 8);
        CHECK(cur >= prev);
        prev = cur;
    }
}

TEST_CASE("compute_qdelta pins and loop-overflow semantics") {
    // (1.0, 457.0): start = 0 (ac[0]=4, 4/4 >= 1); target needs ac[i]/4 >=
    // 457 -> ac[i] >= 1828 -> only i=255 satisfies, but the loop bound is
    // i < MAXQ = 255, so the loop never assigns 255 and target_index rests
    // at its last assignment 254 -> delta = 254 - 0 = 254.
    CHECK(l15_rc::computeQdelta(1.0, 457.0, 8) == 254);
    CHECK(l15_rc::computeQdelta(2.0, 2.0, 8) == 0);
    CHECK(l15_rc::computeQdelta(2.0, 1.0, 8) == -1);
    // nothing found in either loop: both indices rest at 254 -> 0.
    CHECK(l15_rc::computeQdelta(1000.0, 1000.0, 8) == 0);
    // (8.0, 457.0): start = first i with ac[i] >= 32 -> i=25 (ac[25]=32);
    // target 254 (loop bound) -> 254-25 = 229.
    CHECK(l15_rc::computeQdelta(8.0, 457.0, 8) == 229);
}

TEST_CASE("rc_bits_per_mb enumerator quadrant and truncation") {
    // KEY_FRAME non-sc 1400000, INTER non-sc 1000000, KEY sc 1000000,
    // INTER sc 750000. ac[1]=8 -> q=2.0 -> 700000 / 500000 / 500000 / 375000.
    CHECK(l15_rc::rcBitsPerMb(0, 1, 1.0, 8, 0) == 700000);
    CHECK(l15_rc::rcBitsPerMb(1, 1, 1.0, 8, 0) == 500000);
    CHECK(l15_rc::rcBitsPerMb(0, 1, 1.0, 8, 1) == 500000);
    CHECK(l15_rc::rcBitsPerMb(1, 1, 1.0, 8, 1) == 375000);
    // ac[10]=17 -> q=4.25; 1000000*0.7/4.25 = 164705.88... -> 164705
    // (1000000*0.7 = 699999.9999999999555..., /4.25 then (int) truncates).
    CHECK(l15_rc::rcBitsPerMb(1, 10, 0.7, 8, 0) == 164705);
    CHECK(l15_rc::rcBitsPerMb(1, 10, 0.7, 8, 1) == 123529);
    // bits are decreasing in q (property over a sample span).
    int prev = -1;
    for (int q = 1; q < 256; q += 32) {
        const int cur = l15_rc::rcBitsPerMb(1, q, 1.0, 8, 0);
        if (prev >= 0) CHECK(cur < prev);
        prev = cur;
    }
}

TEST_CASE("find_qindex_by_rate binary search finds the first q at or under rate") {
    // bits(q) <= desired: q=2 is the first with ac[2]=9 -> 2.25 -> 1000000/2.25
    // = 444444.4 <= 450000 -> low=2; q=1 gives 500000 > 450000.
    CHECK(l15_rc::findQindexByRate(450000, 8, 1, 0, 0, 255) == 2);
    // At the top the rate is never reached: 1000000/(ac[255]/4=457) =
    // 2188.18... > 1000 -> the search pins at worst_qindex=255.
    CHECK(l15_rc::findQindexByRate(1000, 8, 1, 0, 0, 255) == 255);
    // Trivial range best==worst.
    CHECK(l15_rc::findQindexByRate(1000, 8, 1, 0, 40, 40) == 40);
    CHECK(l15_rc::findQindexByRate(1, 8, 1, 0, 40, 40) == 40);
}

TEST_CASE("compute_qdelta_by_rate pins both directions") {
    // INTER q=10: ac[10]=17 -> 4.25 -> base = 1000000/4.25 = 235294 (trunc);
    // target = (int)(2.0*235294) = 470588; first q with bits <= 470588:
    // 1000000/(ac[q]/4) <= 470588 -> ac[q] >= 8.4999 -> ac[2]=9 -> q=2.
    // delta = 2-10 = -8.
    CHECK(l15_rc::computeQdeltaByRate(0, 255, 1, 10, 2.0, 8, 0) == -8);
    // INTER q=100: ac[100]=112 -> 28.0 -> base = 1000000/28.0 = 35714 (trunc);
    // target = (int)(0.5*35714) = 17857; first q with bits <= 17857:
    // ac[q]/4 >= 1000000/17857 = 56.0022 -> ac[q] >= 224.01 -> ac[142]=223
    // misses, ac[143]=227 -> q=143 -> delta = 43 (oracle).
    CHECK(l15_rc::computeQdeltaByRate(0, 255, 1, 100, 0.5, 8, 0) == 43);
    // KEY_FRAME enumerator 1400000: q=10 base = 1400000/4.25 = 329411;
    // target = (int)(0.5*329411) = 164705; first q with bits <= 164705:
    // 5600000/ac[q] <= 164705 -> ac[q] >= 34.0 -> ac[27]=34 (ac[26]=33
    // misses) -> q=27 -> delta = 17 (oracle).
    CHECK(l15_rc::computeQdeltaByRate(0, 255, 0, 10, 0.5, 8, 0) == 17);
}

TEST_CASE("rate_factor_deltas and levels match the reference tables") {
    // rc_process.c:302-319.
    const double d[6] = {1.00, 1.00, 1.00, 1.50, 2.00, 2.00};
    const int lv[7] = {5, 0, 4, 4, 0, 0, 3};  // KF,LF,GF,ARF,OVERLAY,INTNL_OVERLAY,INTNL_ARF
    int bad = 0;
    for (int i = 0; i < 6; ++i)
        if (l15_rc::kRateFactorDeltas[i] != d[i]) ++bad;
    CHECK(bad == 0);
    for (int i = 0; i < 7; ++i)
        CHECK(l15_rc::kRateFactorLevelsMap[i] == lv[i]);
}

TEST_CASE("get_q_index_from_qstep_ratio walks the dc table") {
    // dc[100]=93; ratio 0.5 -> target 46.5: walk DOWN from 100 to the first
    // dc <= 46.5 -> dc[46]=46 at q=46 (dc[47]=47 > 46.5, dc[46]=46 <= 46.5).
    CHECK(l15_rc::getQIndexFromQstepRatio(100, 0.5, 8) == 46);
    // ratio 2.0 -> target 186: walk UP to the first dc >= 186:
    // dc[147]=185 misses, dc[148]=187 -> 148.
    CHECK(l15_rc::getQIndexFromQstepRatio(100, 2.0, 8) == 148);
    // ratio == 1.0 takes the up-branch and stops immediately at the leaf.
    CHECK(l15_rc::getQIndexFromQstepRatio(100, 1.0, 8) == 100);
    // leaf=255, ratio 0.5 -> target 668: walk down to the first dc <= 668
    // -> dc[234]=675 misses, dc[233]=668 -> 233.
    CHECK(l15_rc::getQIndexFromQstepRatio(255, 0.5, 8) == 233);
    // leaf=0 up-walk from 0: target 8; dc[1]=8 >= 8 -> 1.
    CHECK(l15_rc::getQIndexFromQstepRatio(0, 2.0, 8) == 1);
}

TEST_CASE("get_cqp_kf_boost_from_r0 pins factor clamps and the r0 floor") {
    // ftk=-1 -> factor 7; res <= 720p(3): rint(3*(75+17*7)/r0).
    CHECK(l15_rc::getCqpKfBoostFromR0(1.0, -1, 3) == 582);
    // ftk=200 -> sqrt=14.14 -> min 10 -> factor 10; r0 floored at 1e-6.
    CHECK(l15_rc::getCqpKfBoostFromR0(1.0, 200, 0) == 735);
    CHECK(l15_rc::getCqpKfBoostFromR0(0.0, 200, 3) == 735000000);
    // ftk=16 -> sqrt=4 -> the 4.0 min clamp holds; res=4 is above 720p ->
    // the 4x multiplier: rint(4*(75+68)/0.5) = 1144.
    CHECK(l15_rc::getCqpKfBoostFromR0(0.5, 16, 4) == 1144);
}

TEST_CASE("get_gfu_boost_from_r0_lap pins the min/max clamps") {
    CHECK(l15_rc::getGfuBoostFromR0Lap(4.0, 10.0, 1.0, 100) == 300);
    CHECK(l15_rc::getGfuBoostFromR0Lap(4.0, 10.0, 1.0, 1) == 240);
    CHECK(l15_rc::getGfuBoostFromR0Lap(4.0, 10.0, 0.5, 16) == 480);
    // r0 floor: r0=0 -> 1e-6.
    CHECK(l15_rc::getGfuBoostFromR0Lap(4.0, 10.0, 0.0, 100) == 300000000);
}

TEST_CASE("calculate_boost_bits pins all four gates") {
    // normal: chunks = 10*100+800 = 1800; (800*100000)/1800 = 44444 (trunc).
    CHECK(l15_rc::calculateBoostBits(10, 800, 100000) == 44444);
    // boost > 1023: divisor = boost >> 10 = 1 for 2000 -> unchanged here.
    CHECK(l15_rc::calculateBoostBits(10, 2000, 100000) == 66666);
    // frame_count <= 0: min(total, INT_MAX).
    CHECK(l15_rc::calculateBoostBits(0, 500, 50000) == 50000);
    // boost 0 or total <= 0: 0.
    CHECK(l15_rc::calculateBoostBits(10, 0, 100000) == 0);
    CHECK(l15_rc::calculateBoostBits(10, 500, 0) == 0);
}

TEST_CASE("the tiny predicates thread their arguments") {
    CHECK(l15_rc::frameIsIntraOnly(0));
    CHECK_FALSE(l15_rc::frameIsIntraOnly(1));
    CHECK(l15_rc::frameIsIntraOnly(2));
    CHECK_FALSE(l15_rc::frameIsIntraOnly(3));
    // frame_is_leaf is exactly update_type == SVT_AV1_LF_UPDATE (=1).
    CHECK(l15_rc::frameIsLeaf(1));
    CHECK_FALSE(l15_rc::frameIsLeaf(0));
    CHECK_FALSE(l15_rc::frameIsLeaf(2));
    CHECK_FALSE(l15_rc::frameIsLeaf(3));
    CHECK_FALSE(l15_rc::frameIsLeaf(6));

    l15_rc::PcsCarrier pcs{};
    pcs.frameType = 1;
    pcs.updateType = 0;
    CHECK_FALSE(l15_rc::frameIsKfGfArf(pcs));
    pcs.frameType = 0;
    CHECK(l15_rc::frameIsKfGfArf(pcs));
    pcs.frameType = 1;
    pcs.updateType = 2;
    CHECK(l15_rc::frameIsKfGfArf(pcs));
    pcs.updateType = 3;
    CHECK(l15_rc::frameIsKfGfArf(pcs));
    pcs.frameType = 2;
    CHECK(l15_rc::frameIsKfGfArf(pcs));
    pcs.frameType = 1;
    pcs.updateType = 1;
    CHECK_FALSE(l15_rc::frameIsKfGfArf(pcs));

    l15_rc::RcConfig cfg{};
    cfg.minQpAllowed = 30;
    cfg.maxQpAllowed = 63;
    // qmin = quantizer_to_qindex[30] = 120, qmax = quantizer_to_qindex[63] = 255.
    CHECK(l15_rc::clampQindex(cfg, 200) == 200);
    CHECK(l15_rc::clampQindex(cfg, 50) == 120);
    CHECK(l15_rc::clampQindex(cfg, 300) == 255);
    // clamp_qp clamps against the raw qp values.
    CHECK(l15_rc::clampQp(cfg, 50) == 50);
    CHECK(l15_rc::clampQp(cfg, 10) == 30);
    CHECK(l15_rc::clampQp(cfg, 100) == 63);
}
