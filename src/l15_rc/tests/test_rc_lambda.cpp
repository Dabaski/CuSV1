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
