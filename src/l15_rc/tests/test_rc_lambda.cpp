#include <doctest.h>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "rc_arith.h"

// A9R C1: the rd-multiplier family (rc_process.c:343-392).
//
// FLOAT DISCIPLINE (module-wide): double outputs compared at 1e-9 ABSOLUTE;
// integer outputs pinned EXACTLY.
//
// ORACLE: the dc-quant QSTEP table is parsed from the reference
// inv_transforms.c at runtime (the RC1 mechanical pattern; the port's
// kDcQLookupQtx registry copy is already diff-verified entry-for-entry by
// test_rc_q.cpp) and the def_* constants are applied by a test-side
// transcription of the reference formula. The port's own path goes through
// its own dcQuantQtx and its own def_* helpers, so a constant swap or a
// wrong branch in the port cannot agree with the oracle.
//
// The pinned tree is CONFIG_ENABLE_HIGH_BIT_DEPTH=0, so dcQuantQtx falls to
// the default branch for bit depths 10/12 (assert removed under NDEBUG ->
// -1): q = -1, rdmult = (int64)(def_*(-1) * 1) = 3, then
// ROUND_POWER_OF_TWO(3, 4/8) = 0 -> the "> 0" test fails -> the function
// returns 1. Pinned below as the deterministic park outcome.

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
    std::string num;
    for (std::size_t i = start; i < end; ++i) {
        const char c = body[i - start];
        if ((c >= '0' && c <= '9') || c == '-') {
            num += c;
        } else if (!num.empty()) {
            ref.push_back(std::atoi(num.c_str()));
            num.clear();
        }
    }
    if (!num.empty()) ref.push_back(std::atoi(num.c_str()));
    REQUIRE((int)ref.size() == expected);
    return ref;
}

// test-side transcription of the reference: dc_qlookup_QTX (inv_transforms.c)
std::vector<int> dcTable() {
    return tableNumbers(refText("/third_party/SVT-AV1/Source/Lib/Codec/inv_transforms.c"),
                        "static const int16_t dc_qlookup_QTX[QINDEX_RANGE] = {", 256);
}

// test-side transcription of svt_aom_compute_rd_mult_based_on_qindex
// (rc_process.c:364-392) over the PARSED dc table and the reference's own
// def_* formulas written out from rc_process.c:346-362.
int oracleRdMult(int bitDepth, int updateType, int qindex, const std::vector<int>& dc) {
    (void)bitDepth;  // the oracle only covers the 8-bit domain (see below)
    const int q = dc[qindex];
    double m;
    if (updateType == 0) {          // SVT_AV1_KF_UPDATE
        m = 3.3 + 0.0015 * q;
    } else if (updateType == 2 || updateType == 3) {  // GF / ARF
        m = 3.25 + 0.0015 * q;
    } else {
        m = 3.2 + 0.0015 * q;
    }
    return (int)((std::int64_t)(m * q * q));
}

}  // namespace

TEST_CASE("def_rd_multipliers pin the three base constants and the shared slope") {
    CHECK(std::fabs(l15_rc::defInterRdMultiplier(0) - 3.2) <= 1e-9);
    CHECK(std::fabs(l15_rc::defArfRdMultiplier(0) - 3.25) <= 1e-9);
    CHECK(std::fabs(l15_rc::defKfRdMultiplier(0) - 3.3) <= 1e-9);
    // the slope is shared: +0.0015 per unit qstep.
    CHECK(std::fabs(l15_rc::defInterRdMultiplier(1000) - 4.7) <= 1e-9);
    CHECK(std::fabs(l15_rc::defArfRdMultiplier(2000) - 6.25) <= 1e-9);
    CHECK(std::fabs(l15_rc::defKfRdMultiplier(1828) - 6.042) <= 1e-9);
}

TEST_CASE("compute_rd_mult_based_on_qindex matches the oracle over the full 8-bit domain") {
    const std::vector<int> dc = dcTable();
    int bad = 0;
    for (int qindex = 0; qindex < 256; ++qindex) {
        // KF (update 0), GF (2), ARF (3), LF (1), OVERLAY (4), INTNL_OVERLAY
        // (5), INTNL_ARF (6) - the KF branch, the arf branch and the inter
        // default are all covered.
        for (int upd = 0; upd < 7; ++upd) {
            if (l15_rc::computeRdMultBasedOnQindex(8, upd, qindex) != oracleRdMult(8, upd, qindex, dc)) ++bad;
        }
    }
    CHECK(bad == 0);
}

TEST_CASE("compute_rd_mult_based_on_qindex pins the branches at hand-derived anchors") {
    // Anchors MEASURED in IEEE double with the reference's own evaluation
    // order ((m*q)*q, then the int64 truncation toward zero):
    //   qstep 4   -> kf 52, arf 52, inter 51
    //   qstep 93  -> kf 29748, arf 29315, inter 28883
    //   qstep 1336 -> kf 9467088, arf 9377843, inter 9288598
    // q=100 -> dc[100]=93; q=0 -> dc[0]=4; q=255 -> dc[255]=1336.
    CHECK(l15_rc::computeRdMultBasedOnQindex(8, 0, 100) == 29748);
    // arf branch: both GF (2) and ARF (3) take the 3.25 base.
    CHECK(l15_rc::computeRdMultBasedOnQindex(8, 3, 100) == 29315);
    CHECK(l15_rc::computeRdMultBasedOnQindex(8, 2, 100) == 29315);
    // inter default: LF (1), OVERLAY (4), INTNL_OVERLAY (5), INTNL_ARF (6).
    CHECK(l15_rc::computeRdMultBasedOnQindex(8, 1, 100) == 28883);
    CHECK(l15_rc::computeRdMultBasedOnQindex(8, 6, 100) == 28883);
    // qindex 0: dc[0]=4; kf: 3.3*16 = 52.8 -> 52.
    CHECK(l15_rc::computeRdMultBasedOnQindex(8, 0, 0) == 52);
    CHECK(l15_rc::computeRdMultBasedOnQindex(8, 3, 0) == 52);
    CHECK(l15_rc::computeRdMultBasedOnQindex(8, 1, 0) == 51);
    // the top of the 8-bit domain: dc[255]=1336.
    CHECK(l15_rc::computeRdMultBasedOnQindex(8, 0, 255) == 9467088);
    CHECK(l15_rc::computeRdMultBasedOnQindex(8, 3, 255) == 9377843);
    CHECK(l15_rc::computeRdMultBasedOnQindex(8, 1, 255) == 9288598);
}

TEST_CASE("compute_rd_mult_based_on_qindex pins the 10/12-bit park outcomes") {
    // CONFIG_ENABLE_HIGH_BIT_DEPTH=0: dcQuantQtx returns -1 for bd 10/12
    // (the assert-removed default), so rdmult = (int64)(def_*(-1)) = 3 and
    // ROUND_POWER_OF_TWO(3, 4) = (3+8)>>4 = 0 -> not > 0 -> the function
    // returns 1 (the deterministic park outcome, not the reference's real
    // 10-bit arithmetic - that table is the named HBD park).
    for (int upd = 0; upd < 7; ++upd) {
        CHECK(l15_rc::computeRdMultBasedOnQindex(10, upd, 100) == 1);
        CHECK(l15_rc::computeRdMultBasedOnQindex(12, upd, 100) == 1);
    }
    CHECK(l15_rc::computeRdMultBasedOnQindex(4, 0, 100) == -1);
}

// ---- A9R C2: update_lambda (rc_process.c:394-441) ----

namespace {

std::vector<int> rdFactorTable() {
    return tableNumbers(refText("/third_party/SVT-AV1/Source/Lib/Codec/rc_process.c"),
                        "static const int rd_frame_type_factor[2][SVT_AV1_FRAME_UPDATE_TYPES] = {", 14);
}

l15_rc::PcsCarrier lambdaPcs(int frameType, int tl, int maxTl, int baseQIdx, bool deltaQPresent,
                             bool r0DeltaQpMd) {
    l15_rc::PcsCarrier pcs{};
    pcs.frameType = frameType;
    pcs.temporalLayerIndex = tl;
    pcs.hierarchicalLevels = maxTl;
    pcs.baseQIdx = baseQIdx;
    pcs.deltaQPresent = deltaQPresent;
    pcs.r0DeltaQpMd = r0DeltaQpMd;
    return pcs;
}

// test-side transcription of update_lambda (rc_process.c:398-441).
std::uint32_t oracleUpdateLambda(const l15_rc::PcsCarrier& pcs, int qIndex, int meQIndex, int bitDepth,
                                 std::int64_t rdmult, bool rtc, bool statsBased, const std::vector<int>& factors) {
    const int gfUpdateType = pcs.frameType == 0 ? 0
        : pcs.temporalLayerIndex == 0           ? 3
        : pcs.temporalLayerIndex < pcs.hierarchicalLevels ? 6
                                                          : 1;
    rdmult = (rdmult * factors[(bitDepth != 8 ? 1 : 0) * 7 + gfUpdateType]) >> 7;
    if (rtc && pcs.frameType == 0) {
        rdmult = (rdmult * 100) >> 7;
    }
    if (statsBased) {
        int factor = 128;
        if (rtc) {
            const int qdiff = meQIndex - pcs.baseQIdx;
            if (qdiff < 0) {
                factor = (qdiff <= -4) ? 100 : 115;
            }
        } else if (pcs.deltaQPresent || pcs.r0DeltaQpMd) {
            const int qdiff = qIndex - pcs.baseQIdx;
            if (qdiff < 0) {
                factor = (qdiff <= -8) ? 90 : 115;
            } else if (qdiff > 0) {
                factor = (qdiff <= 8) ? 135 : 150;
            }
        } else {
            const int qdiff = meQIndex - pcs.baseQIdx;
            if (qdiff < 0) {
                factor = (qdiff <= -4) ? 100 : 115;
            } else if (qdiff > 0) {
                factor = (qdiff <= 4) ? 135 : 150;
            }
        }
        rdmult = (rdmult * factor) >> 7;
    }
    return (std::uint32_t)rdmult;
}

}  // namespace

TEST_CASE("rd_frame_type_factor matches rc_process.c entry-for-entry") {
    const std::vector<int> ref = rdFactorTable();
    const int want[14] = {150, 180, 150, 150, 180, 180, 150, 128, 144, 128, 128, 144, 144, 128};
    int bad = 0;
    for (int i = 0; i < 14; ++i)
        if (l15_rc::kRdFrameTypeFactor[i / 7][i % 7] != want[i] || want[i] != ref[i]) ++bad;
    CHECK(bad == 0);
}

TEST_CASE("update_lambda selects the gf_update_type by frame type and temporal layer") {
    // rdmult 1000, no rtc, no stats modulation: the row factor is the only
    // arithmetic. KF (0) / ARF (3) / INTNL_ARF (6) all carry 150; LF (1)
    // carries 180. MEASURED: 1000*150>>7 = 1171, 1000*180>>7 = 1406.
    CHECK(l15_rc::updateLambda(lambdaPcs(0, 0, 4, 100, false, false), 100, 100, 8, 1000, false, false) == 1171);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 0, 4, 100, false, false), 100, 100, 8, 1000, false, false) == 1171);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, false, false), 100, 100, 8, 1000, false, false) == 1171);
    // tl == max (4): the LF branch.
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 4, 4, 100, false, false), 100, 100, 8, 1000, false, false) == 1406);
    // tl > max also falls to LF.
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 5, 4, 100, false, false), 100, 100, 8, 1000, false, false) == 1406);
}

TEST_CASE("update_lambda switches the factor row by bit depth") {
    // bd != EB_EIGHT_BIT selects row [1] {128, 144, ...}: KF 1000*128>>7 =
    // 1000; LF 1000*144>>7 = 1125.
    CHECK(l15_rc::updateLambda(lambdaPcs(0, 0, 4, 100, false, false), 100, 100, 10, 1000, false, false) == 1000);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 4, 4, 100, false, false), 100, 100, 12, 1000, false, false) == 1125);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, false, false), 100, 100, 10, 1000, false, false) == 1000);
}

TEST_CASE("update_lambda applies the rtc kf boost after the row factor") {
    // rtc && KEY: (1171 * 100) >> 7 = 914.
    CHECK(l15_rc::updateLambda(lambdaPcs(0, 0, 4, 100, false, false), 100, 100, 8, 1000, true, false) == 914);
    // rtc on an inter frame: no boost.
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, false, false), 100, 100, 8, 1000, true, false) == 1171);
}

TEST_CASE("update_lambda stats-based modulation covers the three qdiff branches") {
    // delta_q_present path reads q_index - base_q_idx:
    //   qdiff -10 -> 90: (1171*90)>>7 = 823; qdiff -5 -> 115: 1052;
    //   qdiff +4 -> 135: 1235; qdiff +20 -> 150: 1372; qdiff 0 -> 128: 1171.
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, true, false), 90, 100, 8, 1000, false, true) == 823);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, false, true), 95, 100, 8, 1000, false, true) == 1052);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, true, false), 104, 100, 8, 1000, false, true) == 1235);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, true, false), 120, 100, 8, 1000, false, true) == 1372);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, true, false), 100, 100, 8, 1000, false, true) == 1171);
    // the boundary bands: qdiff -8 -> 90, -7 -> 115, +8 -> 135, +9 -> 150.
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, true, false), 92, 100, 8, 1000, false, true) == 823);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, true, false), 93, 100, 8, 1000, false, true) == 1052);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, true, false), 108, 100, 8, 1000, false, true) == 1235);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, true, false), 109, 100, 8, 1000, false, true) == 1372);
    // the no-delta path reads me_q_index - base_q_idx, bands at +/-4:
    //   qdiff -4 -> 100: (1171*100)>>7 = 914; -1 -> 115: 1052;
    //   +4 -> 135: 1235; +5 -> 150: 1372.
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, false, false), 100, 96, 8, 1000, false, true) == 914);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, false, false), 100, 99, 8, 1000, false, true) == 1052);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, false, false), 100, 104, 8, 1000, false, true) == 1235);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, false, false), 100, 105, 8, 1000, false, true) == 1372);
    // the rtc path reads me_q_index and only reacts below 0:
    //   qdiff -10 -> 100: 914; qdiff -2 -> 115: 1052; qdiff +3 -> 128: 1171.
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, false, false), 100, 90, 8, 1000, true, true) == 914);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, false, false), 100, 98, 8, 1000, true, true) == 1052);
    CHECK(l15_rc::updateLambda(lambdaPcs(1, 1, 4, 100, false, false), 100, 103, 8, 1000, true, true) == 1171);
}

TEST_CASE("update_lambda matches the oracle over a carrier-state grid") {
    const std::vector<int> ref = rdFactorTable();
    int bad = 0;
    for (int frameType = 0; frameType < 2; ++frameType) {
        for (int tl = 0; tl <= 6; ++tl) {
            for (int maxTl = 0; maxTl <= 6; ++maxTl) {
                for (int base = 40; base <= 220; base += 60) {
                    for (int qOff = -12; qOff <= 12; ++qOff) {
                        for (int bd : {8, 10}) {
                            for (int cfg = 0; cfg < 8; ++cfg) {
                                const bool rtc = cfg & 1;
                                const bool stats = cfg & 2;
                                const bool dq = cfg & 4;
                                const l15_rc::PcsCarrier pcs =
                                    lambdaPcs(frameType, tl, maxTl, base, dq, false);
                                const std::int64_t rdmult = 977;
                                const auto qIdx = (std::uint8_t)(base + qOff);
                                const auto meIdx = (std::uint8_t)(base - qOff);
                                if (l15_rc::updateLambda(pcs, qIdx, meIdx, bd, rdmult, rtc, stats) !=
                                    oracleUpdateLambda(pcs, qIdx, meIdx, bd, rdmult, rtc, stats, ref))
                                    ++bad;
                            }
                        }
                    }
                }
            }
        }
    }
    CHECK(bad == 0);
}
