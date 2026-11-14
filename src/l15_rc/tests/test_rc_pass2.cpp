#include <doctest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "rc_arith.h"

// A9R C6: the pass2_strategy.c stats arithmetic
//   calculate_modified_err (pass2_strategy.c:21-31) - the comment block at
//     :21-22 and the body :23-31; the twopass->stats_buf_ctx->total_stats
//     NULL gate threads as hasTotalStats.
//   subtract_stats (:47-53) - the four subtracted FIRSTPASS_STATS fields.
//   q_pow_term (:60) + ERR_DIVISOR (:61) + calc_correction_factor (:63-70).
//   qbpm_enumerator (:72-74).
//
// FLOAT DISCIPLINE: doubles at 1e-9 ABSOLUTE (the module convention).
// The q_pow_term table is runtime-parsed from pass2_strategy.c (zero
// hand-typed values) and calc_correction_factor is checked against a
// test-side transcription using the SAME parsed table, so a port-side
// table substitution or shift change cannot agree with the oracle.
// The assert(error_term >= 0.0) gate (:68) is removed under NDEBUG -
// negative inputs are the named NDEBUG park and are not tested.

namespace {

std::string refText(const char* rel) {
    std::ifstream f(std::string(REPO_ROOT) + rel, std::ios::binary);
    REQUIRE_MESSAGE(f.good(), rel);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// q_pow_term (pass2_strategy.c:60) - 9 entries.
std::vector<double> qPowTerm() {
    const std::string text = refText("/third_party/SVT-AV1/Source/Lib/Codec/pass2_strategy.c");
    const std::string decl = "static const double q_pow_term[(QINDEX_RANGE >> 5) + 1] = {";
    const std::size_t at = text.find(decl);
    REQUIRE_MESSAGE(at != std::string::npos, decl);
    const std::size_t start = at + decl.size();
    const std::size_t end = text.find("};", start);
    REQUIRE(end != std::string::npos);
    std::vector<double> ref;
    std::string num;
    for (std::size_t i = start; i < end; ++i) {
        const char c = text[i];
        if ((c >= '0' && c <= '9') || c == '.' || c == '-') {
            num += c;
        } else if (!num.empty()) {
            ref.push_back(std::atof(num.c_str()));
            num.clear();
        }
    }
    if (!num.empty()) ref.push_back(std::atof(num.c_str()));
    REQUIRE((int)ref.size() == 9);
    return ref;
}

double fclamp(double x, double low, double hi) {
    return x < low ? low : (x > hi ? hi : x);
}

// test-side transcription of calc_correction_factor (pass2_strategy.c:63-70).
double oracleCorrectionFactor(double errPerMb, int q, const std::vector<double>& powTerm) {
    const double errorTerm = errPerMb / 96.0;
    const int index = q >> 5;
    const double powerTerm =
        powTerm[index] + (((powTerm[index + 1] - powTerm[index]) * (q % 32)) / 32.0);
    return fclamp(std::pow(errorTerm, powerTerm), 0.05, 5.0);
}

}  // namespace

TEST_CASE("calculate_modified_err is the total_num_bits or zero without total stats") {
    // The reference reads this_frame->stat_struct.total_num_bits and
    // IGNORES total_stats itself beyond the NULL gate (:24-28).
    CHECK(l15_rc::calculateModifiedErr(false, 123456789) == 0.0);
    CHECK(l15_rc::calculateModifiedErr(true, 0) == 0.0);
    CHECK(l15_rc::calculateModifiedErr(true, 123456789) == 123456789.0);
    CHECK(l15_rc::calculateModifiedErr(true, 7) == 7.0);
}

TEST_CASE("subtract_stats subtracts the four accumulated fields") {
    l15_rc::FirstpassStatsCarrier section{};
    section.frame = 10.0;
    section.codedError = 5.5;
    section.duration = 8.0;
    section.count = 12.0;
    section.statStruct.poc = 42;
    l15_rc::FirstpassStatsCarrier frame{};
    frame.frame = 1.0;
    frame.codedError = 0.5;
    frame.duration = 2.0;
    frame.count = 1.0;
    l15_rc::subtractStats(section, frame);
    CHECK(section.frame == 9.0);
    CHECK(section.codedError == 5.0);
    CHECK(section.duration == 6.0);
    CHECK(section.count == 11.0);
    // the stat_struct is untouched by subtract_stats (the reference only
    // touches the four double fields).
    CHECK(section.statStruct.poc == 42);
}

TEST_CASE("q_pow_term registry matches pass2_strategy.c entry-for-entry") {
    const std::vector<double> ref = qPowTerm();
    int bad = 0;
    for (int i = 0; i < 9; ++i)
        if (std::fabs(l15_rc::kQpowTerm[i] - ref[i]) > 1e-12) ++bad;
    CHECK(bad == 0);
    CHECK(l15_rc::kErrDivisor == 96.0);
}

TEST_CASE("calc_correction_factor matches the oracle over the full qindex domain") {
    const std::vector<double> powTerm = qPowTerm();
    int bad = 0;
    for (int q = 0; q < 256; ++q) {
        for (double err : {0.0, 0.1, 1.0, 12.34, 96.0, 1000.0}) {
            if (std::fabs(l15_rc::calcCorrectionFactor(err, q) - oracleCorrectionFactor(err, q, powTerm)) >
                1e-9)
                ++bad;
        }
    }
    CHECK(bad == 0);
}

TEST_CASE("calc_correction_factor pins the clamps and the q bands") {
    // err 0 -> error_term 0 -> pow(0, pt) = 0 -> the 0.05 floor.
    CHECK(std::fabs(l15_rc::calcCorrectionFactor(0.0, 100) - 0.05) <= 1e-9);
    // huge err -> the 5.0 ceiling (err_per_mb = 1e7).
    CHECK(std::fabs(l15_rc::calcCorrectionFactor(1e7, 100) - 5.0) <= 1e-9);
    // the q band boundaries move the power term: q=31 (band 0) vs q=32
    // (band 1), same err.
    const std::vector<double> powTerm = qPowTerm();
    const double b0 = oracleCorrectionFactor(96.0, 31, powTerm);
    const double b1 = oracleCorrectionFactor(96.0, 32, powTerm);
    // err 96 -> error_term 1.0 -> any power_term gives 1.0: the one point
    // where the bands agree (1^x = 1).
    CHECK(std::fabs(b0 - 1.0) <= 1e-9);
    CHECK(std::fabs(b1 - 1.0) <= 1e-9);
    // off the 1.0 ridge the bands differ (0.65..0.70 band vs 0.70..0.75).
    const double c0 = oracleCorrectionFactor(4.0, 31, powTerm);
    const double c1 = oracleCorrectionFactor(4.0, 32, powTerm);
    CHECK(std::fabs(l15_rc::calcCorrectionFactor(4.0, 31) - c0) <= 1e-9);
    CHECK(std::fabs(l15_rc::calcCorrectionFactor(4.0, 32) - c1) <= 1e-9);
    CHECK(std::fabs(c0 - c1) > 1e-9);
}

TEST_CASE("qbpm_enumerator pins the clamp band over the full domain") {
    int bad = 0;
    for (int tol = -50; tol <= 150; ++tol) {
        const int clamped = tol < 25 ? 0 : (tol > 100 ? 75 : tol - 25);
        const int want = 1250000 + (300000 * clamped) / 75;
        if (l15_rc::qbpmEnumerator(tol) != want) ++bad;
    }
    CHECK(bad == 0);
    // anchors: tol <= 25 -> the base; tol 100 -> the 300000 full slope.
    CHECK(l15_rc::qbpmEnumerator(25) == 1250000);
    CHECK(l15_rc::qbpmEnumerator(26) == 1254000);
    CHECK(l15_rc::qbpmEnumerator(100) == 1550000);
    CHECK(l15_rc::qbpmEnumerator(150) == 1550000);
    CHECK(l15_rc::qbpmEnumerator(0) == 1250000);
}

// ---- A9R C7: find_qindex_by_rate_with_correction (pass2_strategy.c:78-99)
// ----

namespace {

std::vector<int> acTable() {
    const std::string text = refText("/third_party/SVT-AV1/Source/Lib/Codec/inv_transforms.c");
    const std::string decl = "static const int16_t ac_qlookup_QTX[QINDEX_RANGE] = {";
    const std::size_t at = text.find(decl);
    REQUIRE_MESSAGE(at != std::string::npos, decl);
    const std::size_t start = at + decl.size();
    const std::size_t end = text.find("};", start);
    REQUIRE(end != std::string::npos);
    std::vector<int> ref;
    std::string num;
    for (std::size_t i = start; i < end; ++i) {
        const char c = text[i];
        if ((c >= '0' && c <= '9') || c == '-') {
            num += c;
        } else if (!num.empty()) {
            ref.push_back(std::atoi(num.c_str()));
            num.clear();
        }
    }
    if (!num.empty()) ref.push_back(std::atoi(num.c_str()));
    REQUIRE((int)ref.size() == 256);
    return ref;
}

// test-side transcription of find_qindex_by_rate_with_correction over the
// oracle correction factor and the PARSED ac table (bd 8: q = ac/4).
int oracleFindQWithCorrection(int desired, double errPerMb, double gwf, int tol, int best, int worst,
                              const std::vector<double>& powTerm, const std::vector<int>& ac) {
    int low = best;
    int high = worst;
    while (low < high) {
        const int mid = (low + high) >> 1;
        const double midFactor = oracleCorrectionFactor(errPerMb, mid, powTerm);
        const double q = ac[mid] / 4.0;
        const int enumerator = 1250000 + ((300000 * std::min(75, std::max(tol - 25, 0))) / 75);
        const int midBits = (int)((enumerator * midFactor * gwf) / q);
        if (midBits > desired) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low;
}

}  // namespace

TEST_CASE("find_qindex_by_rate_with_correction structural edges") {
    // degenerate range: the loop never runs.
    CHECK(l15_rc::findQindexByRateWithCorrection(1000, 8, 4.0, 1.0, 100, 200, 200) == 200);
    // desired INT_MAX: every mid_bits <= desired (max ~7.75M at q=1) ->
    // collapses to best.
    CHECK(l15_rc::findQindexByRateWithCorrection(INT32_MAX, 8, 4.0, 1.0, 100, 40, 255) == 40);
    // desired -1: every mid_bits > desired -> climbs to worst.
    CHECK(l15_rc::findQindexByRateWithCorrection(-1, 8, 4.0, 1.0, 100, 40, 255) == 255);
}

TEST_CASE("find_qindex_by_rate_with_correction matches the oracle over the parameter grid") {
    const std::vector<double> powTerm = qPowTerm();
    const std::vector<int> ac = acTable();
    const double errs[4] = {0.1, 4.0, 96.0, 1000.0};
    int bad = 0;
    for (int errCase = 0; errCase < 4; ++errCase) {
        const double err = errs[errCase];
        for (double gwf : {0.5, 1.0, 1.75}) {
            for (int tol : {25, 40, 100}) {
                for (int best : {0, 40, 120}) {
                    for (int desired : {500, 5000, 50000, 200000, 800000}) {
                        if (l15_rc::findQindexByRateWithCorrection(desired, 8, err, gwf, tol, best, 255) !=
                            oracleFindQWithCorrection(desired, err, gwf, tol, best, 255, powTerm, ac))
                            ++bad;
                    }
                }
            }
        }
    }
    CHECK(bad == 0);
}

TEST_CASE("find_qindex_by_rate_with_correction pins the bd-10 park collapse") {
    // CONFIG_ENABLE_HIGH_BIT_DEPTH=0: convert_qindex_to_q returns -1/16 for
    // bd 10, so every mid_bits is negative and the search collapses to
    // best_qindex (the deterministic park outcome; the real 10-bit table is
    // the named HBD park).
    CHECK(l15_rc::findQindexByRateWithCorrection(50000, 10, 4.0, 1.0, 100, 0, 255) == 0);
    CHECK(l15_rc::findQindexByRateWithCorrection(50000, 10, 4.0, 1.0, 100, 40, 220) == 40);
}

// ---- A9R C8: the GF-stats walk (pass2_strategy.c:33-45, :168-171,
// :314-324, :326-390) ----

namespace {

l15_rc::FirstpassStatsCarrier fpStat(double frame, double codedError, std::uint64_t totalNumBits, int qindex) {
    l15_rc::FirstpassStatsCarrier s{};
    s.frame = frame;
    s.codedError = codedError;
    s.duration = 1.0;
    s.count = 1.0;
    s.statStruct.poc = (std::uint64_t)frame;
    s.statStruct.totalNumBits = totalNumBits;
    s.statStruct.qindex = (std::uint8_t)qindex;
    s.statStruct.worstQindex = (std::uint8_t)(qindex + 2);
    s.statStruct.temporalLayerIndex = 0;
    return s;
}

l15_rc::TwoPassCarrier walkBase(const std::vector<l15_rc::FirstpassStatsCarrier>& stats) {
    l15_rc::TwoPassCarrier tp{};
    tp.statsIn = stats.data();
    tp.statsInCount = (int)stats.size();
    // the CALLER-ADVANCED state: process_first_pass_stats reads frame 0
    // into this_frame via input_stats (pass2_strategy.c:828-830) BEFORE
    // calculate_gf_stats runs, so twopass->stats_in already points past the
    // current frame (index 1). The first input_stats inside the walk reads
    // frame 1, not frame 0 again.
    tp.statsInPos = 1;
    tp.hasTotalStats = true;
    tp.bitsLeft = 0;
    return tp;
}

l15_rc::PcsCarrier gfPcs(int frameType, int gfInterval, bool idr) {
    l15_rc::PcsCarrier pcs{};
    pcs.frameType = frameType;
    pcs.gfInterval = gfInterval;
    pcs.idrFlag = idr;
    return pcs;
}

l15_rc::RcControl gfRc() {
    l15_rc::RcControl rc{};
    rc.framesToKey = 10;
    rc.constrainedGfGroup = -1;  // poison: the walk must overwrite it
    rc.baselineGfInterval = -1;
    return rc;
}

}  // namespace

TEST_CASE("init_gf_stats pins the reference defaults") {
    l15_rc::GfGroupStatsCarrier gs{};
    gs.gfGroupErr = 9.0;
    gs.gfGroupRawError = 9.0;
    gs.gfGroupSkipPct = 9.0;
    gs.gfGroupInactiveZoneRows = 9.0;
    gs.gfStatStruct.poc = 9;
    l15_rc::initGfStats(gs);
    CHECK(gs.gfGroupErr == 0.0);
    CHECK(gs.gfStatStruct.poc == 0);
    CHECK(gs.gfStatStruct.totalNumBits == 1);
    CHECK(gs.gfStatStruct.qindex == 172);
    CHECK(gs.gfStatStruct.worstQindex == 172);
    CHECK(gs.gfGroupRawError == 0.0);
    CHECK(gs.gfGroupSkipPct == 0.0);
    CHECK(gs.gfGroupInactiveZoneRows == 0.0);
}

TEST_CASE("accumulate_this_frame_stats adds mod err and raw coded error") {
    l15_rc::GfGroupStatsCarrier gs{};
    gs.gfGroupErr = 1.5;
    gs.gfGroupRawError = 2.5;
    const l15_rc::FirstpassStatsCarrier f = fpStat(3.0, 0.75, 100, 40);
    l15_rc::accumulateThisFrameStats(f, 10.0, gs);
    CHECK(gs.gfGroupErr == 11.5);
    CHECK(gs.gfGroupRawError == 3.25);
}

TEST_CASE("set_baseline_gf_interval selects the idr clamp or the gop interval") {
    l15_rc::RcControl rc = gfRc();
    const l15_rc::PcsCarrier intraIdr = gfPcs(0, 8, true);
    l15_rc::setBaselineGfInterval(intraIdr, 5, rc);
    CHECK(rc.baselineGfInterval == 4);
    l15_rc::setBaselineGfInterval(intraIdr, 1, rc);
    CHECK(rc.baselineGfInterval == 1);
    l15_rc::setBaselineGfInterval(intraIdr, 0, rc);
    CHECK(rc.baselineGfInterval == 1);
    // intra but NOT idr: the gop interval.
    const l15_rc::PcsCarrier intraNoIdr = gfPcs(0, 8, false);
    l15_rc::setBaselineGfInterval(intraNoIdr, 5, rc);
    CHECK(rc.baselineGfInterval == 8);
    // non-intra: the gop interval.
    const l15_rc::PcsCarrier inter = gfPcs(1, 7, true);
    l15_rc::setBaselineGfInterval(inter, 5, rc);
    CHECK(rc.baselineGfInterval == 7);
}

TEST_CASE("calculate_gf_stats walks the window and rewinds the position") {
    const std::vector<l15_rc::FirstpassStatsCarrier> stats = {
        fpStat(0.0, 1.0, 100, 40), fpStat(1.0, 2.0, 200, 41), fpStat(2.0, 3.0, 300, 42),
        fpStat(3.0, 4.0, 400, 43), fpStat(4.0, 5.0, 500, 44)};
    l15_rc::TwoPassCarrier tp = walkBase(stats);
    l15_rc::RcControl rc = gfRc();
    l15_rc::PcsCarrier pcs = gfPcs(1, 4, false);  // inter, 4-frame group
    l15_rc::GfGroupStatsCarrier gs{};
    l15_rc::FirstpassStatsCarrier thisFrame = stats[0];
    int useAltRef = -1;
    l15_rc::calculateGfStats(pcs, rc, gs, thisFrame, useAltRef, tp);
    // the loop accumulates frames 0..3 (i = 1..4): err 100+200+300+400,
    // raw 1+2+3+4.
    CHECK(gs.gfGroupErr == 1000.0);
    CHECK(gs.gfGroupRawError == 10.0);
    // gf_stat_struct is frame 0's.
    CHECK(gs.gfStatStruct.totalNumBits == 100);
    CHECK(gs.gfStatStruct.qindex == 40);
    CHECK(gs.gfStatStruct.worstQindex == 42);
    // this_frame advanced to the NEXT unread frame (4).
    CHECK(thisFrame.statStruct.totalNumBits == 500);
    // constrained: i(4) >= frames_to_key(10) is false; arf: 4 > 2.
    CHECK(rc.constrainedGfGroup == 0);
    CHECK(useAltRef == 1);
    // non-intra: the baseline is the gop interval.
    CHECK(rc.baselineGfInterval == 4);
    // the walk position rewound (to the caller-advanced start, 1).
    CHECK(tp.statsInPos == 1);
    // a second run on the rewound state gives identical totals.
    l15_rc::GfGroupStatsCarrier gs2{};
    l15_rc::FirstpassStatsCarrier thisFrame2 = stats[0];
    int useAltRef2 = -1;
    l15_rc::calculateGfStats(pcs, rc, gs2, thisFrame2, useAltRef2, tp);
    CHECK(gs2.gfGroupErr == 1000.0);
    CHECK(gs2.gfGroupRawError == 10.0);
}

TEST_CASE("calculate_gf_stats intra subtraction and the idr baseline") {
    const std::vector<l15_rc::FirstpassStatsCarrier> stats = {
        fpStat(0.0, 1.0, 100, 40), fpStat(1.0, 2.0, 200, 41), fpStat(2.0, 3.0, 300, 42),
        fpStat(3.0, 4.0, 400, 43), fpStat(4.0, 5.0, 500, 44)};
    l15_rc::TwoPassCarrier tp = walkBase(stats);
    l15_rc::RcControl rc = gfRc();
    l15_rc::PcsCarrier pcs = gfPcs(0, 4, true);  // KEY + idr
    l15_rc::GfGroupStatsCarrier gs{};
    l15_rc::FirstpassStatsCarrier thisFrame = stats[0];
    int useAltRef = -1;
    l15_rc::calculateGfStats(pcs, rc, gs, thisFrame, useAltRef, tp);
    // intra: frame 0's err (100) and raw (1.0) are subtracted BEFORE the
    // loop, but the loop's FIRST iteration re-accumulates this_frame (f0):
    // -100 + (100+200+300+400) = 900 / -1 + (1+2+3+4) = 9 - the intra
    // group's error is the sum minus ONE f0 contribution.
    CHECK(gs.gfGroupErr == 900.0);
    CHECK(gs.gfGroupRawError == 9.0);
    // the idr baseline: max(i - 1, 1) = 3.
    CHECK(rc.baselineGfInterval == 3);
    CHECK(rc.constrainedGfGroup == 0);
}

TEST_CASE("calculate_gf_stats stops at the window end") {
    const std::vector<l15_rc::FirstpassStatsCarrier> stats = {
        fpStat(0.0, 1.0, 100, 40), fpStat(1.0, 2.0, 200, 41), fpStat(2.0, 3.0, 300, 42)};
    l15_rc::TwoPassCarrier tp = walkBase(stats);
    l15_rc::RcControl rc = gfRc();
    l15_rc::PcsCarrier pcs = gfPcs(1, 10, false);  // group longer than the window
    l15_rc::GfGroupStatsCarrier gs{};
    l15_rc::FirstpassStatsCarrier thisFrame = stats[0];
    int useAltRef = -1;
    l15_rc::calculateGfStats(pcs, rc, gs, thisFrame, useAltRef, tp);
    // EOF at the 4th read: i rests at 3; frames 0..2 accumulated.
    CHECK(gs.gfGroupErr == 600.0);
    CHECK(gs.gfGroupRawError == 6.0);
    // this_frame stays at frame 2 (the EOF break does not overwrite it).
    CHECK(thisFrame.statStruct.totalNumBits == 300);
    CHECK(useAltRef == 1);  // 3 > 2
    CHECK(rc.baselineGfInterval == 10);
    CHECK(tp.statsInPos == 1);
}

TEST_CASE("calculate_gf_stats constrained by frames_to_key and the no-total-stats park") {
    const std::vector<l15_rc::FirstpassStatsCarrier> stats = {
        fpStat(0.0, 1.0, 100, 40), fpStat(1.0, 2.0, 200, 41), fpStat(2.0, 3.0, 300, 42),
        fpStat(3.0, 4.0, 400, 43)};
    // constrained: i(4) >= frames_to_key(3).
    {
        l15_rc::TwoPassCarrier tp = walkBase(stats);
        l15_rc::RcControl rc = gfRc();
        rc.framesToKey = 3;
        l15_rc::PcsCarrier pcs = gfPcs(1, 4, false);
        l15_rc::GfGroupStatsCarrier gs{};
        l15_rc::FirstpassStatsCarrier thisFrame = stats[0];
        int useAltRef = -1;
        l15_rc::calculateGfStats(pcs, rc, gs, thisFrame, useAltRef, tp);
        CHECK(rc.constrainedGfGroup == 1);
    }
    // the EXACT boundary i == frames_to_key (4 == 4): the reference's
    // ">=" marks it constrained (an i == frames_to_key > case would not).
    {
        l15_rc::TwoPassCarrier tp = walkBase(stats);
        l15_rc::RcControl rc = gfRc();
        rc.framesToKey = 4;
        l15_rc::PcsCarrier pcs = gfPcs(1, 4, false);
        l15_rc::GfGroupStatsCarrier gs{};
        l15_rc::FirstpassStatsCarrier thisFrame = stats[0];
        int useAltRef = -1;
        l15_rc::calculateGfStats(pcs, rc, gs, thisFrame, useAltRef, tp);
        CHECK(rc.constrainedGfGroup == 1);
    }
    // hasTotalStats false: mod_err is 0 everywhere, the raw error is not.
    {
        l15_rc::TwoPassCarrier tp = walkBase(stats);
        tp.hasTotalStats = false;
        l15_rc::RcControl rc = gfRc();
        l15_rc::PcsCarrier pcs = gfPcs(1, 4, false);
        l15_rc::GfGroupStatsCarrier gs{};
        l15_rc::FirstpassStatsCarrier thisFrame = stats[0];
        int useAltRef = -1;
        l15_rc::calculateGfStats(pcs, rc, gs, thisFrame, useAltRef, tp);
        CHECK(gs.gfGroupErr == 0.0);
        CHECK(gs.gfGroupRawError == 10.0);
    }
}

// ---- A9R C9: get_twopass_worst_quality (pass2_strategy.c:125-166) +
// the RC_FACTOR constants (:309-312) + calculate_active_worst_quality
// (:392-455) ----

namespace {

// test-side transcription of get_twopass_worst_quality over the C7 oracle
// search and the parsed ac table (bd 8).
int oracleTwopassWq(bool firstPassDownsample, int w, int h, double sectionErr, double inactiveZone,
                    int sbw, double gwf, int under, int over, int best, int worst,
                    const std::vector<double>& powTerm, const std::vector<int>& ac) {
    std::uint32_t mbCols, mbRows;
    if (firstPassDownsample) {
        mbCols = 2 * (w + 16 - 1) / 16;
        mbRows = 2 * (h + 16 - 1) / 16;
    } else {
        mbCols = (w + 16 - 1) / 16;
        mbRows = (h + 16 - 1) / 16;
    }
    inactiveZone = inactiveZone < 0.0 ? 0.0 : (inactiveZone > 1.0 ? 1.0 : inactiveZone);
    if (sbw <= 0) {
        return worst;
    }
    const int numMbs = (int)(mbCols * mbRows);
    const int activeMbs = std::max(1, numMbs - (int)(numMbs * inactiveZone));
    const double avErrPerMb = sectionErr / activeMbs;
    const int targetNormBitsPerMb = (int)(((std::uint64_t)sbw << 9) / activeMbs);
    const int rateErrTol = std::min(under, over);
    return oracleFindQWithCorrection(targetNormBitsPerMb, avErrPerMb, gwf, rateErrTol, best, worst, powTerm, ac);
}

l15_rc::GfGroupStatsCarrier wqStats(double rawError, double skipPct, double zoneRows, std::uint8_t worstQindex,
                                    double groupErr) {
    l15_rc::GfGroupStatsCarrier gs{};
    gs.gfGroupRawError = rawError;
    gs.gfGroupSkipPct = skipPct;
    gs.gfGroupInactiveZoneRows = zoneRows;
    gs.gfStatStruct.worstQindex = worstQindex;
    gs.gfGroupErr = groupErr;
    return gs;
}

l15_rc::RcControl awqRc() {
    l15_rc::RcControl rc{};
    rc.baselineGfInterval = 8;
    rc.bestQuality = 0;
    rc.worstQuality = 255;
    rc.activeWorstQuality = 100;
    rc.gfGroupBits = 3920000;
    return rc;
}

l15_rc::PcsCarrier awqPcs() {
    l15_rc::PcsCarrier pcs{};
    return pcs;
}

}  // namespace

TEST_CASE("get_twopass_worst_quality structural edges") {
    // sbw <= 0 -> the highest value allowed.
    CHECK(l15_rc::getTwopassWorstQuality(false, 1920, 1080, 100.0, 0.0, 0, 1.0, 8, 50, 40, 10, 200) == 200);
    CHECK(l15_rc::getTwopassWorstQuality(false, 1920, 1080, 100.0, 0.0, -5, 1.0, 8, 50, 40, 10, 200) == 200);
    // degenerate range.
    CHECK(l15_rc::getTwopassWorstQuality(false, 1920, 1080, 100.0, 0.0, 1000, 1.0, 8, 50, 40, 77, 77) == 77);
}

TEST_CASE("get_twopass_worst_quality matches the oracle over the parameter grid") {
    const std::vector<double> powTerm = qPowTerm();
    const std::vector<int> ac = acTable();
    const int dims[4][2] = {{320, 240}, {1920, 1080}, {1280, 720}, {64, 64}};
    int bad = 0;
    for (int ds = 0; ds <= 1; ++ds) {
        for (int d = 0; d < 4; ++d) {
            for (double zone : {-0.5, 0.0, 0.25, 0.5, 1.5}) {
                for (int sbw : {1000, 81600, 500000}) {
                    for (double gwf : {0.75, 1.0, 1.5}) {
                        for (int tolCase = 0; tolCase < 2; ++tolCase) {
                            const int under = tolCase == 0 ? 50 : 30;
                            const int over = tolCase == 0 ? 40 : 45;
                            const int got = l15_rc::getTwopassWorstQuality(ds != 0, dims[d][0], dims[d][1],
                                                                           8160.0, zone, sbw, gwf, 8, under,
                                                                           over, 0, 255);
                            const int want = oracleTwopassWq(ds != 0, dims[d][0], dims[d][1], 8160.0, zone,
                                                             sbw, gwf, under, over, 0, 255, powTerm, ac);
                            if (got != want) ++bad;
                        }
                    }
                }
            }
        }
    }
    CHECK(bad == 0);
}

TEST_CASE("calculate_active_worst_quality is a no-op below baseline_gf_interval 2") {
    l15_rc::RcControl rc = awqRc();
    rc.baselineGfInterval = 1;
    const l15_rc::GfGroupStatsCarrier gs = wqStats(80.0, 10.0, 4.0, 100, 280000.0);
    l15_rc::calculateActiveWorstQuality(awqPcs(), gs, rc, 0, 68, 8, false, 1920, 1080, 50, 40, false, true,
                                        false);
    CHECK(rc.activeWorstQuality == 100);  // untouched
    rc.baselineGfInterval = 0;
    l15_rc::calculateActiveWorstQuality(awqPcs(), gs, rc, 0, 68, 8, false, 1920, 1080, 50, 40, false, true,
                                        false);
    CHECK(rc.activeWorstQuality == 100);
}

TEST_CASE("calculate_active_worst_quality rc_factor bands") {
    // The rc_factor arithmetic is observable through the two-pass override
    // search (it multiplies ref_gf_group_bits * ref_q * rc_factor).
    // ref: worst_qindex 100 -> ref_q = ac[100]/4 = 28; gf_group_err 280000
    // -> numerator 7,840,000 * rc_factor; target gf_group_bits 3,920,000.
    // rc_factor 1.0 -> the first q with bits <= 3,920,000 is q=1
    // (ac[1]=8 -> 2.0 -> 3,920,000, NOT >): tmp_q = 1.
    // active_worst = max(tmp_q, 100 >> 1) = 50.
    {
        l15_rc::RcControl rc = awqRc();
        // bits > 0, rate_error 50 (over target): single pass min = 1 ->
        // rc_factor = max(1, 0.5) = 1.0.
        rc.vbrBitsOffTarget = 50000000;
        l15_rc::GfGroupStatsCarrier gs = wqStats(80.0, 10.0, 4.0, 100, 280000.0);
        l15_rc::calculateActiveWorstQuality(awqPcs(), gs, rc, 1000000, 68, 8, false, 1920, 1080, 50, 40,
                                            false, true, true);
        CHECK(rc.activeWorstQuality == 50);
    }
    {
        // gop_constraint min 0.5 -> rc_factor = max(0.5, 0.5) = 0.5:
        // numerator 3,920,000: the first q with bits <= 3,920,000:
        // q=0: 3,920,000 <= target (NOT >) -> high=0 -> tmp_q = 0.
        // active_worst = max(0, 50) = 50.
        l15_rc::RcControl rc = awqRc();
        rc.vbrBitsOffTarget = 50000000;
        l15_rc::GfGroupStatsCarrier gs = wqStats(80.0, 10.0, 4.0, 100, 280000.0);
        l15_rc::calculateActiveWorstQuality(awqPcs(), gs, rc, 1000000, 68, 8, false, 1920, 1080, 50, 40, true,
                                            true, true);
        CHECK(rc.activeWorstQuality == 50);
    }
    {
        // two-pass (not single, not gop) min 0.75 -> rc_factor 0.75:
        // numerator 5,880,000: q=0: 5,880,000 > 3,920,000 -> low=1;
        // q=1: 2,940,000 <= -> tmp_q 1 -> max(1, 50) = 50.
        l15_rc::RcControl rc = awqRc();
        rc.vbrBitsOffTarget = 50000000;
        l15_rc::GfGroupStatsCarrier gs = wqStats(80.0, 10.0, 4.0, 100, 280000.0);
        l15_rc::calculateActiveWorstQuality(awqPcs(), gs, rc, 1000000, 68, 8, false, 1920, 1080, 50, 40,
                                            false, false, true);
        CHECK(rc.activeWorstQuality == 50);
    }
    {
        // rate_error -50 (underspend): rc_factor = min(2, 1.5) = 1.5:
        // numerator 11,760,000: q=1: 5,880,000 > 3,920,000 -> low=2;
        // q=2: ac[2]=9 -> 2.25 -> 5,226,666 > -> low=3; q=3: ac[3]=10 ->
        // 2.5 -> 4,704,000 > -> low=4; q=4: ac[4]=11 -> 2.75 -> 4,276,363
        // > -> low=5; q=5: ac[5]=12 -> 3.0 -> 3,920,000 <= -> tmp_q 5.
        // active_worst = max(5, 50) = 50.
        l15_rc::RcControl rc = awqRc();
        rc.vbrBitsOffTarget = -50000000;
        l15_rc::GfGroupStatsCarrier gs = wqStats(80.0, 10.0, 4.0, 100, 280000.0);
        l15_rc::calculateActiveWorstQuality(awqPcs(), gs, rc, 1000000, 68, 8, false, 1920, 1080, 50, 40,
                                            false, true, true);
        CHECK(rc.activeWorstQuality == 50);
    }
    {
        // bits <= 0: rc_factor stays 1.0 -> tmp_q 1 -> max(1, 50) = 50.
        l15_rc::RcControl rc = awqRc();
        l15_rc::GfGroupStatsCarrier gs = wqStats(80.0, 10.0, 4.0, 100, 280000.0);
        l15_rc::calculateActiveWorstQuality(awqPcs(), gs, rc, 0, 68, 8, false, 1920, 1080, 50, 40, false,
                                            true, true);
        CHECK(rc.activeWorstQuality == 50);
    }
    {
        // the max() floor: active_worst 5 -> max(tmp_q 1, 5 >> 1 = 2) = 2.
        l15_rc::RcControl rc = awqRc();
        rc.activeWorstQuality = 5;
        l15_rc::GfGroupStatsCarrier gs = wqStats(80.0, 10.0, 4.0, 100, 280000.0);
        l15_rc::calculateActiveWorstQuality(awqPcs(), gs, rc, 0, 68, 8, false, 1920, 1080, 50, 40, false,
                                            true, true);
        CHECK(rc.activeWorstQuality == 2);
    }
    {
        // the single-pass min DISCRIMINATOR: target gf_group_bits 3,000,000
        // with active_worst 3 (floor max(tmp_q, 1)): rate_error 50 ->
        // single-pass rc_factor = max(1, 0.5) = 1.0 -> numerator 7,840,000:
        // bits(1)=3.92M >, bits(2)=3.48M >, bits(3)=3.14M >, bits(4)=2.85M
        // <= -> tmp_q 4 -> active_worst 4. (A two-pass min 0.75 would give
        // numerator 5,880,000 -> bits(1)=2.94M <= -> tmp_q 1 -> 1.)
        l15_rc::RcControl rc = awqRc();
        rc.activeWorstQuality = 3;
        rc.gfGroupBits = 3000000;
        rc.vbrBitsOffTarget = 50000000;
        l15_rc::GfGroupStatsCarrier gs = wqStats(80.0, 10.0, 4.0, 100, 280000.0);
        l15_rc::calculateActiveWorstQuality(awqPcs(), gs, rc, 1000000, 68, 8, false, 1920, 1080, 50, 40,
                                            false, true, true);
        CHECK(rc.activeWorstQuality == 4);
    }
    {
        // the two-pass search driving tmp_q to 4 with a tighter target:
        // gf_group_bits 3,000,000 -> numerator 7,840,000 (rc_factor 1):
        // q=1: 3,920,000 > 3,000,000; q=2: 3,484,444 >; q=3: 3,136,000 >;
        // q=4: ac[4]=11 -> 2.75 -> 2,850,909 <= -> tmp_q 4 -> max(4, 50).
        l15_rc::RcControl rc = awqRc();
        rc.gfGroupBits = 3000000;
        l15_rc::GfGroupStatsCarrier gs = wqStats(80.0, 10.0, 4.0, 100, 280000.0);
        l15_rc::calculateActiveWorstQuality(awqPcs(), gs, rc, 0, 68, 8, false, 1920, 1080, 50, 40, false,
                                            true, true);
        CHECK(rc.activeWorstQuality == 50);
        CHECK(rc.gfGroupBits == 3000000);
    }
}

TEST_CASE("calculate_active_worst_quality one-pass path matches the oracle") {
    const std::vector<double> powTerm = qPowTerm();
    const std::vector<int> ac = acTable();
    int bad = 0;
    // the one-pass path feeds get_twopass_worst_quality; pin the composed
    // result against the oracle for a few (vbrBitsOffTarget, gfGroupBits)
    // states at baseline 8, mb_rows 68, w 1920 h 1080, zone 0.25.
    for (int vbr : {50000000, -50000000, 0}) {
        for (std::int64_t gfBits : {1000000, 3920000}) {
            l15_rc::RcControl rc = awqRc();
            rc.vbrBitsOffTarget = vbr;
            rc.gfGroupBits = gfBits;
            l15_rc::GfGroupStatsCarrier gs = wqStats(80.0, 10.0, 4.0, 100, 280000.0);
            l15_rc::calculateActiveWorstQuality(awqPcs(), gs, rc, 1000000, 68, 8, false, 1920, 1080, 50, 40,
                                                false, true, false);
            // oracle: rc_factor first, then the twopass-wq transcription.
            double rcFactor = 1.0;
            {
                int rateError = (int)((vbr * 100) / 1000000);
                rateError = rateError < -100 ? -100 : (rateError > 100 ? 100 : rateError);
                if (rateError > 0) {
                    rcFactor = std::max(1.0, (double)(100 - rateError) / 100.0);
                } else {
                    rcFactor = std::min(2.0, (double)(100 - rateError) / 100.0);
                }
            }
            const int vbrGroupBitsPerFrame = (int)(gfBits / rc.baselineGfInterval);
            const double groupAvErr = 80.0 / 8;
            const double groupAvSkipPct = 10.0 / 8;
            const double groupAvZone = (4.0 * 2) / (8 * 68.0);
            const int want = oracleTwopassWq(false, 1920, 1080, groupAvErr, groupAvSkipPct + groupAvZone,
                                             vbrGroupBitsPerFrame, rcFactor, 50, 40, 0, 255, powTerm, ac);
            // the port already applied the max with the PRE-call value 100;
            // emulate: max(want, 50).
            if (rc.activeWorstQuality != std::max(want, 50)) ++bad;
        }
    }
    CHECK(bad == 0);
}

// ---- A9R C10: get_section_target_bandwidth (pass2_strategy.c:741-759) +
// the framerate pair (:875-904) ----

TEST_CASE("get_section_target_bandwidth selects the lap or the bits_left split") {
    // lap_rc: the avg frame bandwidth passes through.
    {
        l15_rc::RcControl rc{};
        rc.avgFrameBandwidth = 20000;
        CHECK(l15_rc::getSectionTargetBandwidth(rc, true, 100.0, 40, 1234567) == 20000);
    }
    // non-lap: (int)(bits_left / frames_left); frames_left =
    // (int)(total_stats->count - picture_number). 1,200,000/60 = 20000.
    {
        l15_rc::RcControl rc{};
        rc.avgFrameBandwidth = 20000;
        CHECK(l15_rc::getSectionTargetBandwidth(rc, false, 100.0, 40, 1200000) == 20000);
    }
    // the int64 division truncates: 1,000,009/60 = 16666.81 -> 16666.
    {
        l15_rc::RcControl rc{};
        rc.avgFrameBandwidth = 20000;
        CHECK(l15_rc::getSectionTargetBandwidth(rc, false, 100.0, 40, 1000009) == 16666);
    }
    // frames_left is an int cast of the double difference: 99.5 - 40 ->
    // (int) 59.5 = 59; 1,000,009/59 = 16949.3 -> 16949.
    {
        l15_rc::RcControl rc{};
        rc.avgFrameBandwidth = 20000;
        CHECK(l15_rc::getSectionTargetBandwidth(rc, false, 99.5, 40, 1000009) == 16949);
    }
}

TEST_CASE("av1_rc_update_framerate pins the bandwidth and the max-frame floor") {
    {
        // 3,000,000 @ 30 -> avg 100,000; vbrmax 250% -> 250,000;
        // MBs 8160 * MAX_MB_RATE 250 = 2,040,000 > MAXRATE_1080P 2,025,000.
        l15_rc::RcControl rc{};
        l15_rc::av1RcUpdateFramerate(rc, 30.0, 3000000, 250, 8160);
        CHECK(rc.avgFrameBandwidth == 100000);
        CHECK(rc.maxFrameBandwidth == 2040000);
    }
    {
        // small frame: the MAXRATE_1080P floor binds (25,000 < 2,025,000).
        l15_rc::RcControl rc{};
        l15_rc::av1RcUpdateFramerate(rc, 30.0, 1000000, 250, 100);
        CHECK(rc.avgFrameBandwidth == 33333);
        CHECK(rc.maxFrameBandwidth == 2025000);
    }
    {
        // vbrmax above the floors: 30,000,000 @ 30 -> avg 1,000,000;
        // 1,000,000 * 300/100 = 3,000,000 > the 2,025,000 floor (MBs 100
        // keeps the per-MB term at 25,000) -> the vbr term binds.
        l15_rc::RcControl rc{};
        l15_rc::av1RcUpdateFramerate(rc, 30.0, 30000000, 300, 100);
        CHECK(rc.avgFrameBandwidth == 1000000);
        CHECK(rc.maxFrameBandwidth == 3000000);
    }
    {
        // the INT_MAX guard: 1,500,000,000 @ 30 -> avg 50,000,000;
        // 50,000,000 * 5000 / 100 = 2,500,000,000 -> clamped to INT_MAX.
        l15_rc::RcControl rc{};
        l15_rc::av1RcUpdateFramerate(rc, 30.0, 1500000000, 5000, 8160);
        CHECK(rc.avgFrameBandwidth == 50000000);
        CHECK(rc.maxFrameBandwidth == 2147483647);
    }
}

TEST_CASE("svt_av1_new_framerate clamps below 0.1 and drives the update") {
    {
        // 0.05 -> the 30 clamp, then the update.
        l15_rc::RcControl rc{};
        double scsFr = 0.0;
        l15_rc::av1NewFramerate(rc, scsFr, 0.05, 3000000, 250, 8160);
        CHECK(scsFr == 30.0);
        CHECK(rc.avgFrameBandwidth == 100000);
        CHECK(rc.maxFrameBandwidth == 2040000);
    }
    {
        // 24 passes through.
        l15_rc::RcControl rc{};
        double scsFr = 0.0;
        l15_rc::av1NewFramerate(rc, scsFr, 24.0, 2400000, 250, 8160);
        CHECK(scsFr == 24.0);
        CHECK(rc.avgFrameBandwidth == 100000);
        CHECK(rc.maxFrameBandwidth == 2040000);
    }
    {
        // exactly 0.1: NOT below 0.1 -> passes through.
        l15_rc::RcControl rc{};
        double scsFr = 0.0;
        l15_rc::av1NewFramerate(rc, scsFr, 0.1, 300000, 250, 8160);
        CHECK(scsFr == 0.1);
        CHECK(rc.avgFrameBandwidth == 3000000);
    }
}

// ---- A9D S4: pass2_strategy.c frame_max_bits (:55-58) +
// calculate_total_gf_group_bits (:186-220) ----

TEST_CASE("frame_max_bits is the vbrmax share clipped to max_frame_bandwidth") {
    // avg 10,000 * 190/100 = 19,000; the 50,000 ceiling does not bind.
    {
        l15_rc::RcControl rc{};
        rc.avgFrameBandwidth = 10000;
        rc.maxFrameBandwidth = 50000;
        CHECK(l15_rc::frameMaxBits(rc, 190) == 19000);
    }
    {
        // the ceiling binds: 19,000 > 10,000 -> 10,000.
        l15_rc::RcControl rc{};
        rc.avgFrameBandwidth = 10000;
        rc.maxFrameBandwidth = 10000;
        CHECK(l15_rc::frameMaxBits(rc, 190) == 10000);
    }
    {
        // vbrmax 0: the product is 0, the CLIP3 low bound keeps 0.
        l15_rc::RcControl rc{};
        rc.avgFrameBandwidth = 10000;
        rc.maxFrameBandwidth = 50000;
        CHECK(l15_rc::frameMaxBits(rc, 0) == 0);
    }
}

TEST_CASE("calculate_total_gf_group_bits walks the kf scaling and the clamps") {
    // The oracle: (kf_group_bits * (gf_err / kf_error_left)) truncated, the
    // three clamps, and the kf_group_bits -= total update.
    {
        // kf bits 1000, err left 500, gf err 250 -> 500 total; the kf pool
        // drops to 500.
        l15_rc::TwoPassCarrier tp{};
        tp.kfGroupBits = 1000;
        tp.kfGroupErrorLeft = 500;
        l15_rc::RcControl rc{};
        rc.baselineGfInterval = 8;
        rc.avgFrameBandwidth = 100000;
        rc.maxFrameBandwidth = 1000000;
        CHECK(l15_rc::calculateTotalGfGroupBits(rc, 190, tp, false, 0, 0, 0, 0, 250.0) == 500);
        CHECK(tp.kfGroupBits == 500);
    }
    {
        // kf_group_bits <= 0: 0 total, and the pool does not move.
        l15_rc::TwoPassCarrier tp{};
        tp.kfGroupBits = 0;
        tp.kfGroupErrorLeft = 500;
        l15_rc::RcControl rc{};
        rc.baselineGfInterval = 8;
        rc.avgFrameBandwidth = 100000;
        rc.maxFrameBandwidth = 1000000;
        CHECK(l15_rc::calculateTotalGfGroupBits(rc, 190, tp, false, 0, 0, 0, 0, 250.0) == 0);
        CHECK(tp.kfGroupBits == 0);
    }
    {
        // err_left <= 0: 0 total.
        l15_rc::TwoPassCarrier tp{};
        tp.kfGroupBits = 1000;
        tp.kfGroupErrorLeft = 0;
        l15_rc::RcControl rc{};
        rc.baselineGfInterval = 8;
        rc.avgFrameBandwidth = 100000;
        rc.maxFrameBandwidth = 1000000;
        CHECK(l15_rc::calculateTotalGfGroupBits(rc, 190, tp, false, 0, 0, 0, 0, 250.0) == 0);
        CHECK(tp.kfGroupBits == 1000);
    }
    {
        // the lap scaling branch: (lad_mg + 1) * 2^hier < intra_period.
        // kf 1000 * min(frames_in_sw 30, frames_to_key 60) / 60 = 500, then
        // * (250/500) = 250.
        l15_rc::TwoPassCarrier tp{};
        tp.kfGroupBits = 1000;
        tp.kfGroupErrorLeft = 500;
        l15_rc::RcControl rc{};
        rc.framesToKey = 60;
        rc.baselineGfInterval = 8;
        rc.avgFrameBandwidth = 100000;
        rc.maxFrameBandwidth = 1000000;
        // lad_mg 2, hier 3 -> 3*8 = 24 < 240.
        CHECK(l15_rc::calculateTotalGfGroupBits(rc, 190, tp, true, 2, 3, 240, 30, 250.0) == 250);
        CHECK(tp.kfGroupBits == 750);
    }
    {
        // the lap gate NOT taken ((lad_mg+1)*2^hier == intra_period): the
        // unscaled path. 3*8 = 24 == 24 -> no scaling.
        l15_rc::TwoPassCarrier tp{};
        tp.kfGroupBits = 1000;
        tp.kfGroupErrorLeft = 500;
        l15_rc::RcControl rc{};
        rc.framesToKey = 60;
        rc.baselineGfInterval = 8;
        rc.avgFrameBandwidth = 100000;
        rc.maxFrameBandwidth = 1000000;
        CHECK(l15_rc::calculateTotalGfGroupBits(rc, 190, tp, true, 2, 3, 24, 30, 250.0) == 500);
    }
    {
        // negative result clamps to 0 (gf_err < 0).
        l15_rc::TwoPassCarrier tp{};
        tp.kfGroupBits = 1000;
        tp.kfGroupErrorLeft = 500;
        l15_rc::RcControl rc{};
        rc.baselineGfInterval = 8;
        rc.avgFrameBandwidth = 100000;
        rc.maxFrameBandwidth = 1000000;
        CHECK(l15_rc::calculateTotalGfGroupBits(rc, 190, tp, false, 0, 0, 0, 0, -250.0) == 0);
        CHECK(tp.kfGroupBits == 1000);
    }
    {
        // above the kf pool: clamps at kf_group_bits (gf_err > err_left).
        l15_rc::TwoPassCarrier tp{};
        tp.kfGroupBits = 1000;
        tp.kfGroupErrorLeft = 500;
        l15_rc::RcControl rc{};
        rc.baselineGfInterval = 8;
        rc.avgFrameBandwidth = 100000;
        rc.maxFrameBandwidth = 1000000;
        CHECK(l15_rc::calculateTotalGfGroupBits(rc, 190, tp, false, 0, 0, 0, 0, 2000.0) == 1000);
        CHECK(tp.kfGroupBits == 0);
    }
    {
        // the data-rate variability clip: max_bits * baseline_gf_interval.
        // vbrmax 50: max_bits = 100000*50/100 = 50000; *8 = 400,000. The raw
        // total is 800,000 (kf 1,000,000 * (400/500)); the clip binds.
        l15_rc::TwoPassCarrier tp{};
        tp.kfGroupBits = 1000000;
        tp.kfGroupErrorLeft = 500;
        l15_rc::RcControl rc{};
        rc.baselineGfInterval = 8;
        rc.avgFrameBandwidth = 100000;
        rc.maxFrameBandwidth = 1000000;
        CHECK(l15_rc::calculateTotalGfGroupBits(rc, 50, tp, false, 0, 0, 0, 0, 400.0) == 400000);
        CHECK(tp.kfGroupBits == 600000);
    }
}

// ---- A9D S5: pass2_strategy.c allocate_gf_group_bits (:240-307) +
// layer_fraction (:238) + av1_gop_bit_allocation (:457-463) ----

namespace {

// an 8-frame hierarchical-3 group: KF at 0, ARFs at layers 3/2, an INTNL_ARF
// at layer 1, leaves elsewhere.
void hier3Group(l15_rc::GfGroupMemberView* g, int* layerDepthOverride = nullptr) {
    const int ut[8] = {0, 3, 1, 3, 1, 1, 6, 1};
    const int ld[8] = {0, 3, 0, 2, 0, 0, 1, 0};
    for (int i = 0; i < 8; ++i) {
        g[i].updateType = ut[i];
        g[i].layerDepth = layerDepthOverride ? layerDepthOverride[i] : ld[i];
        g[i].baseFrameTarget = -1;  // poison: the walk must write every idx
    }
}

}  // namespace

TEST_CASE("allocate_gf_group_bits pins the 8-frame hier-3 allocation") {
    l15_rc::GfGroupMemberView g[8];
    hier3Group(g);
    l15_rc::PcsCarrier pcs{};
    pcs.gfInterval = 8;
    pcs.hierarchicalLevels = 3;
    l15_rc::RcControl rc{};
    rc.baselineGfInterval = 8;
    rc.gfuBoost = 2400;
    // gfu_boost 2400 over 1000 group bits -> gf_arf_bits 750 (the >1023
    // divisor path); total 1000-750 = 250; base = 250/8 = 31.
    l15_rc::av1GopBitAllocation(pcs, rc, g, 8, true, true, 1000);
    // key frame idx0 is SKIPPED (frame_index 1) - still the poison.
    CHECK(g[0].baseFrameTarget == -1);
    // idx1 ARF layer3: 31 + 45; idx3 ARF layer2: 31 + 105;
    // idx6 INTNL_ARF layer1: 31 + 600.
    CHECK(g[1].baseFrameTarget == 76);
    CHECK(g[3].baseFrameTarget == 136);
    CHECK(g[6].baseFrameTarget == 631);
    // the leaves and the KF-chain idx7: 31.
    CHECK(g[2].baseFrameTarget == 31);
    CHECK(g[4].baseFrameTarget == 31);
    CHECK(g[5].baseFrameTarget == 31);
    CHECK(g[7].baseFrameTarget == 31);
}

TEST_CASE("allocate_gf_group_bits pins the overlay zero and the no-arf path") {
    {
        // an OVERLAY member gets 0, not the base bits.
        l15_rc::GfGroupMemberView g[4];
        g[0].updateType = 1; g[0].layerDepth = 0; g[0].baseFrameTarget = -1;
        g[1].updateType = 5; g[1].layerDepth = 0; g[1].baseFrameTarget = -1;  // OVERLAY
        g[2].updateType = 1; g[2].layerDepth = 0; g[2].baseFrameTarget = -1;
        g[3].updateType = 4; g[3].layerDepth = 0; g[3].baseFrameTarget = -1;  // INTNL_OVERLAY
        l15_rc::PcsCarrier pcs{};
        pcs.gfInterval = 4;
        pcs.hierarchicalLevels = 1;
        l15_rc::RcControl rc{};
        rc.baselineGfInterval = 4;
        l15_rc::av1GopBitAllocation(pcs, rc, g, 4, false, true, 400);
        // gfu_boost 0 -> gf_arf_bits 0 (the calculate_boost_bits zero gate);
        // total 400; base = 100.
        CHECK(g[0].baseFrameTarget == 100);
        CHECK(g[1].baseFrameTarget == 0);
        CHECK(g[2].baseFrameTarget == 100);
        CHECK(g[3].baseFrameTarget == 0);
    }
    {
        // use_arf false + non-key: the (use_arf || !key_frame) gate still
        // fires (!key_frame) - but gfu_boost 0 makes gf_arf_bits 0, so the
        // whole pool is the base.
        l15_rc::GfGroupMemberView g[4];
        for (int i = 0; i < 4; ++i) {
            g[i].updateType = 1;
            g[i].layerDepth = 0;
            g[i].baseFrameTarget = -1;
        }
        l15_rc::PcsCarrier pcs{};
        pcs.gfInterval = 4;
        pcs.hierarchicalLevels = 1;
        l15_rc::RcControl rc{};
        rc.baselineGfInterval = 4;
        l15_rc::av1GopBitAllocation(pcs, rc, g, 4, false, false, 400);
        CHECK(g[0].baseFrameTarget == 100);
        CHECK(g[3].baseFrameTarget == 100);
    }
    {
        // use_arf false + KEY: the (use_arf || !key_frame) gate FAILS - no
        // subtraction; the pool stays whole but the ARF layer still takes
        // its extra bits out of gf_arf_bits.
        l15_rc::GfGroupMemberView g[4];
        for (int i = 0; i < 4; ++i) {
            g[i].updateType = (i == 1) ? 3 : 1;
            g[i].layerDepth = (i == 1) ? 1 : 0;
            g[i].baseFrameTarget = -1;
        }
        l15_rc::PcsCarrier pcs{};
        pcs.gfInterval = 4;
        pcs.hierarchicalLevels = 1;
        l15_rc::RcControl rc{};
        rc.baselineGfInterval = 4;
        rc.gfuBoost = 400;
        // gfu_boost 400 -> chunks 400+400=800; arf = 400*400/800 = 200.
        l15_rc::av1GopBitAllocation(pcs, rc, g, 4, true, false, 400);
        // NO subtraction: total 400; base = 100; the ARF layer1 (max layer)
        // takes fraction 1.0 of gf_arf_bits -> extra 200.
        CHECK(g[0].baseFrameTarget == -1);  // key frame: skipped
        CHECK(g[1].baseFrameTarget == 100 + 200);
        CHECK(g[2].baseFrameTarget == 100);
        CHECK(g[3].baseFrameTarget == 100);
    }
    {
        // baseline_gf_interval 0: base_frame_bits = 1.
        l15_rc::GfGroupMemberView g[2];
        for (int i = 0; i < 2; ++i) {
            g[i].updateType = 1;
            g[i].layerDepth = 0;
            g[i].baseFrameTarget = -1;
        }
        l15_rc::PcsCarrier pcs{};
        pcs.gfInterval = 2;
        pcs.hierarchicalLevels = 1;
        l15_rc::RcControl rc{};
        rc.baselineGfInterval = 0;
        l15_rc::av1GopBitAllocation(pcs, rc, g, 2, false, false, 400);
        CHECK(g[0].baseFrameTarget == 1);
        CHECK(g[1].baseFrameTarget == 1);
    }
    {
        // the doubling loop: baseline 2 < 8>>1 = 4 -> the ARF/INTNL_ARF
        // layer_frames DOUBLE-count (+1 / +2), splitting the extra bits.
        l15_rc::GfGroupMemberView g[8];
        hier3Group(g);
        l15_rc::PcsCarrier pcs{};
        pcs.gfInterval = 8;
        pcs.hierarchicalLevels = 3;
        l15_rc::RcControl rc{};
        rc.baselineGfInterval = 2;
        rc.gfuBoost = 2400;
        l15_rc::av1GopBitAllocation(pcs, rc, g, 8, true, true, 1000);
        // REFERENCE QUIRK: av1_gop_bit_allocation passes rc->baseline_gf_
        // interval (=2 here, NOT the gf_interval param) as the frame count
        // to calculate_boost_bits: chunks = 2*100+2400 = 2600, the >1023
        // divisor halves both -> 1200*1000/1300 = 923.
        // total = 1000-923 = 77; base = 77/2 = 38.
        // layer_frames after both walks: layer1 = 1+2 = 3, layer2 = 2,
        // layer3 = 2.
        // i=1: extra[1] = (int)(923*0.8)/3 = 246; arf -= 738 -> 185.
        // i=2: extra[2] = (int)(185*0.7)/2 = 64; arf -= 129 -> 56.
        // i=3: extra[3] = 56/2 = 28.
        CHECK(g[1].baseFrameTarget == 38 + 28);
        CHECK(g[3].baseFrameTarget == 38 + 64);
        CHECK(g[6].baseFrameTarget == 38 + 246);
        CHECK(g[2].baseFrameTarget == 38);
    }
    {
        // the doubling gate's OTHER side: baseline 5 is NOT < 8>>1 = 4, so
        // no doubling (a mutation that compares against gf_interval itself
        // would fire here - 5 < 8).
        l15_rc::GfGroupMemberView g[8];
        hier3Group(g);
        l15_rc::PcsCarrier pcs{};
        pcs.gfInterval = 8;
        pcs.hierarchicalLevels = 3;
        l15_rc::RcControl rc{};
        rc.baselineGfInterval = 5;
        rc.gfuBoost = 2400;
        l15_rc::av1GopBitAllocation(pcs, rc, g, 8, true, true, 1000);
        // arf = calculate_boost_bits(5, 2400, 1000): chunks 2900 -> divisor
        // 2 -> 1200*1000/1450 = 827; total = 173; base = 34; NO doubling:
        // extra[1] = 661, extra[2] = 116, extra[3] = 50.
        CHECK(g[1].baseFrameTarget == 34 + 50);
        CHECK(g[3].baseFrameTarget == 34 + 116);
        CHECK(g[6].baseFrameTarget == 34 + 661);
        CHECK(g[2].baseFrameTarget == 34);
    }
}

// ---- A9D S6: pass2_strategy.c lap_rc_init (:512-555) +
// lap_rc_group_error_calc (:562-581) ----

namespace {

// a lap window: 3 stats with coded errors 1/2/3 and total_num_bits 100/200/
// 300.
std::vector<l15_rc::FirstpassStatsCarrier> lapWindow() {
    std::vector<l15_rc::FirstpassStatsCarrier> v;
    const double ce[3] = {1.0, 2.0, 3.0};
    const std::uint64_t nb[3] = {100, 200, 300};
    for (int i = 0; i < 3; ++i) {
        l15_rc::FirstpassStatsCarrier s{};
        s.frame = (double)i;
        s.codedError = ce[i];
        s.count = 1.0;
        s.duration = 1.0;
        s.statStruct.totalNumBits = nb[i];
        v.push_back(s);
    }
    return v;
}

l15_rc::TwoPassCarrier lapWalk(const std::vector<l15_rc::FirstpassStatsCarrier>& stats) {
    l15_rc::TwoPassCarrier tp{};
    tp.statsIn = stats.data();
    tp.statsInCount = (int)stats.size();
    tp.statsInPos = 0;
    tp.hasTotalStats = true;
    tp.bitsLeft = 1000;
    return tp;
}

}  // namespace

TEST_CASE("lap_rc_init walks the lookahead window with the <= end quirk") {
    const std::vector<l15_rc::FirstpassStatsCarrier> stats = lapWindow();
    l15_rc::TwoPassCarrier tp = lapWalk(stats);
    // the caller's current frame (coded_error 10, total_num_bits 10).
    l15_rc::FirstpassStatsCarrier arg{};
    arg.codedError = 10.0;
    arg.statStruct.totalNumBits = 10;

    l15_rc::lapRcInit(tp, arg, 50, 190, 3000000, 30.0);

    // QUIRK: the `stats_in <= stats_in_end` guard runs the body one final
    // time before input_stats EOFs -> num_stats = window + 1 = 4, and the
    // coded-error sum is the arg + all three elements = 16.
    // DOUBLE_DIVIDE_CHECK: num_stats + 0.000001 -> avg = 16/4.000001.
    const double avg = 16.0 / (4.0 + 0.000001);
    CHECK(std::fabs(tp.modifiedErrorMin - (avg * 50.0 / 100.0)) <= 1e-9);
    CHECK(std::fabs(tp.modifiedErrorMax - (avg * 190.0 / 100.0)) <= 1e-9);
    // the second walk: calculate_modified_err = total_num_bits, accumulated
    // arg + all three = 10 + 100 + 200 + 300 = 610.
    CHECK(std::fabs(tp.modifiedErrorLeft - 610.0) <= 1e-9);
    // bits_left += (int64)(num_stats * (target / framerate)) = 4 * 100000.
    CHECK(tp.bitsLeft == 1000 + 400000);
    // the walk position rewound.
    CHECK(tp.statsInPos == 0);
}

TEST_CASE("lap_rc_init pins the empty-window and the rewind") {
    // pos == count at entry: the body still runs once (the <= guard), the
    // input_stats EOFs immediately; num_stats 1, coded error = arg only.
    const std::vector<l15_rc::FirstpassStatsCarrier> stats = lapWindow();
    l15_rc::TwoPassCarrier tp = lapWalk(stats);
    tp.statsInPos = 3;  // at the end
    l15_rc::FirstpassStatsCarrier arg{};
    arg.codedError = 10.0;
    arg.statStruct.totalNumBits = 10;
    l15_rc::lapRcInit(tp, arg, 50, 190, 3000000, 30.0);
    const double avg = 10.0 / (1.0 + 0.000001);
    CHECK(std::fabs(tp.modifiedErrorMin - (avg * 50.0 / 100.0)) <= 1e-9);
    CHECK(std::fabs(tp.modifiedErrorLeft - 10.0) <= 1e-9);
    CHECK(tp.bitsLeft == 1000 + 100000);
    CHECK(tp.statsInPos == 3);
}

TEST_CASE("lap_rc_group_error_calc caps at frames_to_key and rewinds") {
    const std::vector<l15_rc::FirstpassStatsCarrier> stats = lapWindow();
    {
        // frames_to_key 2: terms = arg + loaded[0] (the cap).
        l15_rc::TwoPassCarrier tp = lapWalk(stats);
        l15_rc::FirstpassStatsCarrier arg{};
        arg.codedError = 10.0;
        arg.statStruct.totalNumBits = 10;
        const double total = l15_rc::lapRcGroupErrorCalc(tp, arg, 2);
        CHECK(std::fabs(total - 110.0) <= 1e-9);
        CHECK(tp.statsInPos == 0);
    }
    {
        // frames_to_key beyond the window: the EOF break - arg + all three
        // = 610 (the same <= quirk as lap_rc_init).
        l15_rc::TwoPassCarrier tp = lapWalk(stats);
        l15_rc::FirstpassStatsCarrier arg{};
        arg.codedError = 10.0;
        arg.statStruct.totalNumBits = 10;
        const double total = l15_rc::lapRcGroupErrorCalc(tp, arg, 50);
        CHECK(std::fabs(total - 610.0) <= 1e-9);
        CHECK(tp.statsInPos == 0);
    }
}

// ---- A9D S7: pass2_strategy.c set_kf_interval_variables (:594-627) +
// get_kf_group_bits (:629-642) + kf_group_rate_assingment (:651-742) ----

namespace {

// the KF fixture: a 4-stat window, the caller's frame (total_num_bits 50)
// already consumed - the walk starts at pos 1 (the caller-advanced
// convention). total_num_bits 100/200/300/400.
std::vector<l15_rc::FirstpassStatsCarrier> kfWindow() {
    std::vector<l15_rc::FirstpassStatsCarrier> v;
    const std::uint64_t nb[4] = {100, 200, 300, 400};
    for (int i = 0; i < 4; ++i) {
        l15_rc::FirstpassStatsCarrier s{};
        s.frame = (double)i;
        s.codedError = 1.0;
        s.count = 1.0;
        s.duration = 1.0;
        s.statStruct.totalNumBits = nb[i];
        v.push_back(s);
    }
    return v;
}

l15_rc::TwoPassCarrier kfWalk(const std::vector<l15_rc::FirstpassStatsCarrier>& stats) {
    l15_rc::TwoPassCarrier tp{};
    tp.statsIn = stats.data();
    tp.statsInCount = (int)stats.size();
    tp.statsInPos = 1;  // the caller already consumed stats[0]
    tp.hasTotalStats = true;
    tp.bitsLeft = 100000;
    tp.modifiedErrorLeft = 1000.0;
    tp.kfGroupBits = -1;  // poison: the assignment must overwrite
    tp.kfGroupErrorLeft = -1;
    return tp;
}

l15_rc::FirstpassStatsCarrier kfArg() {
    l15_rc::FirstpassStatsCarrier arg{};
    arg.codedError = 1.0;
    arg.count = 1.0;
    arg.duration = 1.0;
    arg.statStruct.totalNumBits = 50;
    return arg;
}

l15_rc::RcControl kfRc() {
    l15_rc::RcControl rc{};
    rc.avgFrameBandwidth = 10000;
    rc.maxFrameBandwidth = 1000000;
    rc.kfBoost = 400;
    rc.framesToKey = -1;  // poison
    rc.framesSinceKey = 8;
    rc.framesSinceCdfUpdate = 8;
    return rc;
}

}  // namespace

TEST_CASE("set_kf_interval_variables walks, caps and writes the lap outcomes") {
    const std::vector<l15_rc::FirstpassStatsCarrier> stats = kfWindow();
    {
        // non-lap: frames_to_key = MIN(intra+1, walked). From the
        // caller-advanced pos 1 the window holds 3 elements + the <= quirk
        // extra iteration -> walked = 4 -> MIN(11, 4) = 4.
        l15_rc::TwoPassCarrier tp = kfWalk(stats);
        l15_rc::RcControl rc = kfRc();
        l15_rc::RcIntervalParamCarrier param{};
        l15_rc::FirstpassStatsCarrier arg = kfArg();
        double kfGroupErr = 0.0;
        l15_rc::setKfIntervalVariables(tp, arg, &kfGroupErr, 11, false, false, 10, rc, param);
        CHECK(rc.framesToKey == 4);
        // the accumulated error: arg 50 + 200 + 300 + 400 = 950.
        CHECK(std::fabs(kfGroupErr - 950.0) <= 1e-9);
        // no rewind here (the caller kf_group_rate_assingment rewinds).
        CHECK(tp.statsInPos == 4);
        CHECK(param.endOfSeqSeen == 0);
    }
    {
        // the NULL kf_group_err: the walk runs, nothing accumulates.
        l15_rc::TwoPassCarrier tp = kfWalk(stats);
        l15_rc::RcControl rc = kfRc();
        l15_rc::RcIntervalParamCarrier param{};
        l15_rc::FirstpassStatsCarrier arg = kfArg();
        l15_rc::setKfIntervalVariables(tp, arg, nullptr, 11, false, false, 10, rc, param);
        CHECK(rc.framesToKey == 4);
    }
    {
        // num_frames_to_detect_scenecut == 0: early return, NOTHING moves.
        l15_rc::TwoPassCarrier tp = kfWalk(stats);
        l15_rc::RcControl rc = kfRc();
        l15_rc::RcIntervalParamCarrier param{};
        l15_rc::FirstpassStatsCarrier arg = kfArg();
        double kfGroupErr = 7.0;
        l15_rc::setKfIntervalVariables(tp, arg, &kfGroupErr, 0, false, false, 10, rc, param);
        CHECK(rc.framesToKey == -1);
        CHECK(std::fabs(kfGroupErr - 7.0) <= 1e-9);
        CHECK(tp.statsInPos == 1);
    }
    {
        // lap + !end_of_sequence_region: frames_to_key = intra + 1.
        l15_rc::TwoPassCarrier tp = kfWalk(stats);
        l15_rc::RcControl rc = kfRc();
        l15_rc::RcIntervalParamCarrier param{};
        l15_rc::FirstpassStatsCarrier arg = kfArg();
        double kfGroupErr = 0.0;
        l15_rc::setKfIntervalVariables(tp, arg, &kfGroupErr, 11, true, false, 10, rc, param);
        CHECK(rc.framesToKey == 11);
        CHECK(param.endOfSeqSeen == 0);
    }
    {
        // lap + end_of_sequence_region: the param's end_of_seq_seen = 1 and
        // the MIN path applies.
        l15_rc::TwoPassCarrier tp = kfWalk(stats);
        l15_rc::RcControl rc = kfRc();
        l15_rc::RcIntervalParamCarrier param{};
        l15_rc::FirstpassStatsCarrier arg = kfArg();
        double kfGroupErr = 0.0;
        l15_rc::setKfIntervalVariables(tp, arg, &kfGroupErr, 11, true, true, 10, rc, param);
        CHECK(rc.framesToKey == 4);
        CHECK(param.endOfSeqSeen == 1);
    }
}

TEST_CASE("get_kf_group_bits selects the lap bandwidth or the error split") {
    const std::vector<l15_rc::FirstpassStatsCarrier> stats = kfWindow();
    l15_rc::RcControl rc = kfRc();
    rc.framesToKey = 11;
    l15_rc::TwoPassCarrier tp = kfWalk(stats);
    {
        // non-lap: bits_left * (err / modified_error_left).
        CHECK(l15_rc::getKfGroupBits(false, 3, 10, false, rc, tp, 950.0) == 95000);
    }
    {
        // lap + frames_in_sw < intra + !eos: frames_to_key * bandwidth.
        CHECK(l15_rc::getKfGroupBits(true, 3, 10, false, rc, tp, 950.0) == 110000);
    }
    {
        // lap + eos: the error split despite lap.
        CHECK(l15_rc::getKfGroupBits(true, 3, 10, true, rc, tp, 950.0) == 95000);
    }
    {
        // lap + frames_in_sw >= intra: the error split.
        CHECK(l15_rc::getKfGroupBits(true, 10, 10, false, rc, tp, 950.0) == 95000);
    }
}

TEST_CASE("kf_group_rate_assingment pins the one-pass composition") {
    const std::vector<l15_rc::FirstpassStatsCarrier> stats = kfWindow();
    l15_rc::TwoPassCarrier tp = kfWalk(stats);
    l15_rc::RcControl rc = kfRc();
    l15_rc::PcsCarrier pcs{};
    l15_rc::RcIntervalParamCarrier param{};
    l15_rc::FirstpassStatsCarrier arg = kfArg();
    l15_rc::kfGroupRateAssignment(pcs, rc, tp, arg, param, false, 10, 3000000, 30.0, 190, 1);
    // walked kf_group_err 950, frames_to_key MIN(11, 4) = 4.
    // kf_group_bits: 100000 * (950/1000) = 95000; the max_grp_bits clip:
    // 19000 * 4 = 76000 -> kf_group_bits 76000.
    // bits_left: 100000 - 76000 = 24000.
    // kf_bits = calculate_boost_bits(MIN(4, INT_MAX) - 1 = 3, 400, 76000):
    // chunks 700 -> 400*76000/700 = 43428.
    CHECK(rc.framesToKey == 4);
    CHECK(rc.framesSinceKey == 0);
    CHECK(rc.framesSinceCdfUpdate == 0);
    CHECK(tp.kfGroupBits == 76000 - 43428);
    CHECK(tp.bitsLeft == 24000);
    CHECK(pcs.baseFrameTarget == 43428);
    // kf_group_error_left = 950 - 50 = 900.
    CHECK(tp.kfGroupErrorLeft == 900);
    // modified_error_left = 1000 - 950 = 50.
    CHECK(std::fabs(tp.modifiedErrorLeft - 50.0) <= 1e-9);
    CHECK(tp.kfZeroMotionPct == 0);
    CHECK(tp.statsInPos == 1);
}

TEST_CASE("kf_group_rate_assingment pins the two-pass kf_bits branch") {
    const std::vector<l15_rc::FirstpassStatsCarrier> stats = kfWindow();
    l15_rc::TwoPassCarrier tp = kfWalk(stats);
    l15_rc::RcControl rc = kfRc();
    l15_rc::PcsCarrier pcs{};
    l15_rc::RcIntervalParamCarrier param{};
    l15_rc::FirstpassStatsCarrier arg = kfArg();
    l15_rc::kfGroupRateAssignment(pcs, rc, tp, arg, param, false, 10, 3000000, 30.0, 190, 2);
    // passes == 2: kf_bits = (int)(kf_group_bits * (stats_in - 1)->
    // stat_struct.total_num_bits / kf_group_err) = 76000 * 100 / 950 = 8000.
    CHECK(pcs.baseFrameTarget == 8000);
    CHECK(tp.kfGroupBits == 76000 - 8000);
}

// ---- A9D S8: pass2_strategy.c process_first_pass_stats (:761-819) +
// is_new_gf_group (:823-845) ----

namespace {

// the process_first_pass_stats fixture: 3 stats in the window, a total and
// a total_left stats pair.
l15_rc::FirstpassStatsCarrier pfStat(double frame, double codedError, std::uint64_t tnb) {
    l15_rc::FirstpassStatsCarrier s{};
    s.frame = frame;
    s.codedError = codedError;
    s.count = 1.0;
    s.duration = 1.0;
    s.statStruct.totalNumBits = tnb;
    return s;
}

}  // namespace

TEST_CASE("process_first_pass_stats first-frame case and the left-stats subtract") {
    const std::vector<l15_rc::FirstpassStatsCarrier> stats = {pfStat(0.0, 10.0, 100), pfStat(1.0, 20.0, 200),
                                                              pfStat(2.0, 30.0, 400)};
    l15_rc::FirstpassStatsCarrier totalStats = pfStat(3.0, 60.0, 700);
    totalStats.count = 3.0;
    l15_rc::FirstpassStatsCarrier totalLeftStats = totalStats;
    l15_rc::TwoPassCarrier tp{};
    tp.statsIn = stats.data();
    tp.statsInCount = 3;
    tp.statsInPos = 0;
    tp.hasTotalStats = true;
    tp.bitsLeft = 300000;
    l15_rc::RcControl rc{};
    rc.bestQuality = 0;
    rc.worstQuality = 255;
    rc.avgFrameQindex[1] = -1;
    rc.avgFrameQindex[0] = -1;
    l15_rc::RcConfig cfg{};
    cfg.bestAllowedQ = 40;
    l15_rc::PcsCarrier pcs{};
    pcs.pictureNumber = 0;
    l15_rc::FirstpassStatsCarrier thisFrame{};

    l15_rc::processFirstPassStats(pcs, rc, cfg, tp, thisFrame, &totalStats, &totalLeftStats, stats.data(), false, 1,
                                  8, 50, 40, false, 1920, 1080);

    // one-pass: tmp_q = get_twopass_worst_quality(section_error,
    // 0, section_target_bandwidth, 1.0). section_length = 3,
    // section_error = 60/3 = 20; stb = (int)(300000 / 3) = 100000.
    const int expected = oracleTwopassWq(false, 1920, 1080, 20.0, 0.0, 100000, 1.0, 50, 40, 0, 255,
                                         qPowTerm(), acTable());
    CHECK(rc.activeWorstQuality == expected);
    CHECK(rc.avgFrameQindex[1] == expected);
    // the integer division: (tmp_q + best_allowed_q) / 2 truncates.
    CHECK(rc.avgFrameQindex[0] == (expected + 40) / 2);
    // the walk consumed frame 0.
    CHECK(tp.statsInPos == 1);
    CHECK(thisFrame.statStruct.totalNumBits == 100);
    // subtract_stats on total_left_stats: frame 3 - 0 = 3, coded_error
    // 60 - 10 = 50, count 3 - 1 = 2, duration 1 - 1 = 0 (pfStat's duration
    // is 1.0 - the fixture never overrode it).
    CHECK(std::fabs(totalLeftStats.frame - 3.0) <= 1e-9);
    CHECK(std::fabs(totalLeftStats.codedError - 50.0) <= 1e-9);
    CHECK(std::fabs(totalLeftStats.count - 2.0) <= 1e-9);
    CHECK(std::fabs(totalLeftStats.duration - 0.0) <= 1e-9);
}

TEST_CASE("process_first_pass_stats the two-pass first-frame search") {
    const std::vector<l15_rc::FirstpassStatsCarrier> stats = {pfStat(0.0, 10.0, 100), pfStat(1.0, 20.0, 200),
                                                              pfStat(2.0, 30.0, 400)};
    l15_rc::FirstpassStatsCarrier totalStats = pfStat(3.0, 60.0, 700);
    totalStats.count = 3.0;
    l15_rc::FirstpassStatsCarrier totalLeftStats = totalStats;
    l15_rc::TwoPassCarrier tp{};
    tp.statsIn = stats.data();
    tp.statsInCount = 3;
    tp.statsInPos = 0;
    tp.hasTotalStats = true;
    tp.bitsLeft = 3920000;
    l15_rc::RcControl rc{};
    rc.bestQuality = 0;
    rc.worstQuality = 255;
    l15_rc::RcConfig cfg{};
    cfg.bestAllowedQ = 40;
    l15_rc::PcsCarrier pcs{};
    pcs.pictureNumber = 0;
    l15_rc::FirstpassStatsCarrier thisFrame{};

    l15_rc::processFirstPassStats(pcs, rc, cfg, tp, thisFrame, &totalStats, &totalLeftStats, stats.data(), false, 2,
                                  8, 50, 40, false, 1920, 1080);

    // passes == 2: ref_qindex = stats_in_start->stat_struct.worst_qindex.
    // The fixture leaves it 0 -> ref_q = ac[0]/4 = 16.0; ref bits =
    // total_stats->stat_struct.total_num_bits = 700.
    // test-side transcription of the reference search (:786-801).
    const double refQ = 16.0;
    const std::int64_t refBits = 700;
    const std::int64_t target = 3920000;
    int low = 0, high = 255;
    while (low < high) {
        const int mid = (low + high) >> 1;
        const double q = l15_rc::convertQindexToQ(mid, 8);
        const int midBits = (int)(refBits * refQ / q);
        if (midBits > target) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    CHECK(rc.activeWorstQuality == low);
}

TEST_CASE("process_first_pass_stats the picture_number gate and the EOF") {
    const std::vector<l15_rc::FirstpassStatsCarrier> stats = {pfStat(0.0, 10.0, 100)};
    l15_rc::FirstpassStatsCarrier totalStats = pfStat(1.0, 10.0, 100);
    totalStats.count = 1.0;
    l15_rc::FirstpassStatsCarrier totalLeftStats = totalStats;
    l15_rc::TwoPassCarrier tp{};
    tp.statsIn = stats.data();
    tp.statsInCount = 1;
    tp.statsInPos = 1;  // the window is already consumed
    tp.hasTotalStats = true;
    tp.bitsLeft = 300000;
    l15_rc::RcControl rc{};
    rc.avgFrameQindex[1] = -1;
    l15_rc::RcConfig cfg{};
    l15_rc::PcsCarrier pcs{};
    pcs.pictureNumber = 0;
    l15_rc::FirstpassStatsCarrier thisFrame{};
    l15_rc::processFirstPassStats(pcs, rc, cfg, tp, thisFrame, &totalStats, &totalLeftStats, stats.data(), false, 1,
                                  8, 50, 40, false, 1920, 1080);
    // the input_stats EOF: the subtract never runs (the values stay).
    CHECK(std::fabs(totalLeftStats.codedError - 10.0) <= 1e-9);
    CHECK(tp.statsInPos == 1);
    {
        // picture_number != 0: the whole first-frame case is skipped - the
        // q fields are untouched; the subtract still runs on frame 1.
        const std::vector<l15_rc::FirstpassStatsCarrier> stats2 = {pfStat(0.0, 10.0, 100),
                                                                   pfStat(1.0, 20.0, 200)};
        l15_rc::FirstpassStatsCarrier ts = pfStat(2.0, 30.0, 300);
        ts.count = 2.0;
        l15_rc::FirstpassStatsCarrier tls = ts;
        l15_rc::TwoPassCarrier tp2{};
        tp2.statsIn = stats2.data();
        tp2.statsInCount = 2;
        tp2.statsInPos = 1;  // the caller-advanced convention
        tp2.hasTotalStats = true;
        tp2.bitsLeft = 300000;
        l15_rc::RcControl rc2{};
        rc2.avgFrameQindex[1] = 77;
        rc2.avgFrameQindex[0] = 77;
        l15_rc::RcConfig cfg2{};
        l15_rc::PcsCarrier pcs2{};
        pcs2.pictureNumber = 5;
        l15_rc::FirstpassStatsCarrier tf2{};
        l15_rc::processFirstPassStats(pcs2, rc2, cfg2, tp2, tf2, &ts, &tls, stats2.data(), false, 1, 8, 50, 40,
                                      false, 1920, 1080);
        CHECK(rc2.avgFrameQindex[1] == 77);
        CHECK(rc2.avgFrameQindex[0] == 77);
        CHECK(tf2.statStruct.totalNumBits == 200);
        CHECK(std::fabs(tls.codedError - 10.0) <= 1e-9);
        CHECK(tp2.statsInPos == 2);
    }
}

TEST_CASE("is_new_gf_group the simple and the incomplete-minigop paths") {
    {
        // NOT an incomplete-mg frame: is_new_gf_group = gf_update_due.
        l15_rc::PcsCarrier pcs{};
        pcs.gfInterval = 8;
        pcs.isNewGfGroup = -1;
        pcs.gfUpdateDue = 1;
        l15_rc::isNewGfGroup(pcs, nullptr, false);
        CHECK(pcs.isNewGfGroup == 1);
        pcs.gfUpdateDue = 0;
        l15_rc::isNewGfGroup(pcs, nullptr, false);
        CHECK(pcs.isNewGfGroup == 0);
    }
    {
        // an incomplete-mg frame: the member walk. Member 1 is within the
        // gf_interval POC distance (the == gf_interval boundary counts),
        // incomp, and gf_update_due -> 1; the clear pass zeroes every
        // member's gf_update_due.
        l15_rc::PcsCarrier pcs{};
        pcs.gfInterval = 8;
        pcs.pictureNumber = 100;
        pcs.gfUpdateDue = 0;
        l15_rc::GfGroupPocView group[8] = {};
        group[0].hasPcs = true;
        group[0].pictureNumber = 50;   // |50-100| = 50 > 8 -> NOT counted
        group[0].gfUpdateDue = 1;
        group[0].incompMgFrame = true;
        group[1].hasPcs = true;
        group[1].pictureNumber = 108;  // |108-100| = 8 == gf_interval -> counted
        group[1].gfUpdateDue = 1;
        group[1].incompMgFrame = true;
        group[2].hasPcs = false;       // the NULL member: skipped
        group[2].gfUpdateDue = 1;
        l15_rc::isNewGfGroup(pcs, group, true);
        CHECK(pcs.isNewGfGroup == 1);
        CHECK(group[0].gfUpdateDue == false);
        CHECK(group[1].gfUpdateDue == false);
        // the NULL member is NOT cleared (the reference's own
        // if (pcs->gf_group[pic_i]) guard).
        CHECK(group[2].gfUpdateDue == true);
    }
    {
        // an incomplete-mg frame with NO matching member: 0, no clears.
        l15_rc::PcsCarrier pcs{};
        pcs.gfInterval = 8;
        pcs.pictureNumber = 100;
        l15_rc::GfGroupPocView group[8] = {};
        group[0].hasPcs = true;
        group[0].pictureNumber = 105;
        group[0].gfUpdateDue = 1;
        group[0].incompMgFrame = false;  // not incomp -> not counted
        l15_rc::isNewGfGroup(pcs, group, true);
        CHECK(pcs.isNewGfGroup == 0);
        CHECK(group[0].gfUpdateDue == true);
    }
}

// ---- A9D S9: pass2_strategy.c set_rc_param (:906-949) +
// read_stat_from_file (:955-969) + init_single_pass_lap (:971-995) +
// init_second_pass (:997-1058) ----

TEST_CASE("set_rc_param derives the cfg and the frame_info") {
    l15_rc::FrameInfoCarrier fi{};
    l15_rc::RcConfig cfg{};
    int vbrmin = -1, vbrmax = -1, sframeDist = -1, sframeMode = -1;
    // non-downsample: 1920x1080 -> mb 120x68, 8160 mbs.
    l15_rc::setRcParam(false, 1920, 1080, 8, 50, 190, 1, 10, 63, false, 25, 35, 10000, 3000, 6000, 2000, 1500,
                       fi, cfg, vbrmin, vbrmax, sframeDist, sframeMode);
    CHECK(fi.frameWidth == 1920);
    CHECK(fi.frameHeight == 1080);
    CHECK(fi.mbCols == 120);
    CHECK(fi.mbRows == 68);
    CHECK(fi.numMbs == 8160);
    CHECK(fi.bitDepth == 8);
    CHECK(vbrmin == 50);
    CHECK(vbrmax == 190);
    // SVT_AV1_RC_MODE_VBR (1) -> AOM_VBR (0).
    CHECK(cfg.mode == l15_rc::kRcVbr);
    // quantizer_to_qindex[10] = 40, [63] = 255 (the index*4 ladder).
    CHECK(cfg.bestAllowedQ == 40);
    CHECK(cfg.worstAllowedQ == 255);
    CHECK(cfg.overShootPct == 25);
    CHECK(cfg.underShootPct == 35);
    // the VBR buffer overrides.
    CHECK(cfg.maximumBufferSizeMs == 240000);
    CHECK(cfg.startingBufferLevelMs == 60000);
    CHECK(cfg.optimalBufferLevelMs == 60000);
    CHECK(cfg.maxIntraBitratePct == 2000);
    CHECK(cfg.maxInterBitratePct == 1500);
    CHECK(sframeDist == 0);
    CHECK(sframeMode == 0);
    {
        // gop_constraint_rc zeroes the shoot pcts; CQP (0) -> AOM_Q (2) and
        // the static buffer ms pass through; downsample doubles the dims.
        l15_rc::FrameInfoCarrier fi2{};
        l15_rc::RcConfig cfg2{};
        int vbrmin2 = -1, vbrmax2 = -1, sd2 = -1, sm2 = -1;
        l15_rc::setRcParam(true, 640, 360, 10, 50, 190, 0, 0, 63, true, 25, 35, 7777, 8888, 9999, 2000, 1500,
                           fi2, cfg2, vbrmin2, vbrmax2, sd2, sm2);
        CHECK(fi2.frameWidth == 1280);
        CHECK(fi2.frameHeight == 720);
        CHECK(fi2.mbCols == 80);
        CHECK(fi2.mbRows == 46);
        CHECK(cfg2.mode == l15_rc::kRcQ);
        CHECK(cfg2.overShootPct == 0);
        CHECK(cfg2.underShootPct == 0);
        CHECK(cfg2.maximumBufferSizeMs == 7777);
        CHECK(cfg2.startingBufferLevelMs == 8888);
        CHECK(cfg2.optimalBufferLevelMs == 9999);
    }
    {
        // CBR (2) -> AOM_CBR (1).
        l15_rc::RcConfig cfg3{};
        l15_rc::FrameInfoCarrier fi3{};
        int v3, v4, sd3, sm3;
        l15_rc::setRcParam(false, 64, 64, 8, 50, 190, 2, 10, 63, false, 25, 35, 1, 2, 3, 0, 0,
                           fi3, cfg3, v3, v4, sd3, sm3);
        CHECK(cfg3.mode == l15_rc::kRcCbr);
    }
}

TEST_CASE("read_stat_from_file fills zero-tnb entries per layer") {
    // window: {100 (layer1), 0 (layer1), 50 (layer0)}.
    std::vector<l15_rc::FirstpassStatsCarrier> buf(3);
    for (int i = 0; i < 3; ++i) {
        buf[i].frame = (double)i;
        buf[i].count = 1.0;
        buf[i].duration = 1.0;
    }
    buf[0].statStruct.totalNumBits = 100;
    buf[0].statStruct.temporalLayerIndex = 1;
    buf[1].statStruct.totalNumBits = 0;
    buf[1].statStruct.temporalLayerIndex = 1;
    buf[2].statStruct.totalNumBits = 50;
    buf[2].statStruct.temporalLayerIndex = 0;
    l15_rc::FirstpassStatsCarrier totalStats{};
    l15_rc::TwoPassCarrier tp{};
    tp.statsIn = buf.data();
    tp.statsInCount = 3;
    tp.hasTotalStats = true;
    l15_rc::readStatFromFile(tp, buf.data(), &totalStats);
    // frame1 filled from previous_num_bits[layer1] = 100.
    CHECK(buf[1].statStruct.totalNumBits == 100);
    // total = 100 + 100 + 50 = 250 -> total_stats.
    CHECK(totalStats.statStruct.totalNumBits == 250);
}

TEST_CASE("init_single_pass_lap resets the twopass and rc counters") {
    // the set_rc_param composition runs through the same walls (verified by
    // the cfg writes); the counters reset.
    l15_rc::TwoPassCarrier tp{};
    tp.statsInCount = 3;  // the stats_in_end-present gate
    tp.bitsLeft = 999;
    tp.modifiedErrorMin = 9.0;
    tp.modifiedErrorMax = 9.0;
    tp.modifiedErrorLeft = 9.0;
    tp.kfZeroMotionPct = 0;
    l15_rc::RcControl rc{};
    rc.vbrBitsOffTarget = 123;
    rc.vbrBitsOffTargetFast = 456;
    rc.rateErrorEstimate = 789;
    l15_rc::FrameInfoCarrier fi{};
    l15_rc::RcConfig cfg{};
    int v1, v2, sd, sm;
    l15_rc::initSinglePassLap(tp, rc, false, 1920, 1080, 8, 50, 190, 1, 10, 63, false, 25, 35, 1, 2, 3, 0, 0,
                              fi, cfg, v1, v2, sd, sm);
    CHECK(tp.bitsLeft == 0);
    CHECK(tp.modifiedErrorMin == 0.0);
    CHECK(tp.modifiedErrorMax == 0.0);
    CHECK(tp.modifiedErrorLeft == 0.0);
    CHECK(tp.kfZeroMotionPct == 100);
    CHECK(rc.vbrBitsOffTarget == 0);
    CHECK(rc.vbrBitsOffTargetFast == 0);
    CHECK(rc.rateErrorEstimate == 0);
    // the set_rc_param composition ran (the cfg derivation is observable).
    CHECK(cfg.mode == l15_rc::kRcVbr);
    CHECK(fi.numMbs == 8160);
    {
        // the empty-stats gate: NOTHING moves.
        l15_rc::TwoPassCarrier tp2{};
        tp2.statsInCount = 0;
        tp2.bitsLeft = 999;
        tp2.kfZeroMotionPct = 5;
        l15_rc::RcControl rc2{};
        rc2.vbrBitsOffTarget = 123;
        l15_rc::FrameInfoCarrier fi2{};
        l15_rc::RcConfig cfg2{};
        int w1, w2, wd, wm;
        l15_rc::initSinglePassLap(tp2, rc2, false, 1920, 1080, 8, 50, 190, 1, 10, 63, false, 25, 35, 1, 2, 3, 0, 0,
                                  fi2, cfg2, w1, w2, wd, wm);
        CHECK(tp2.bitsLeft == 999);
        CHECK(tp2.kfZeroMotionPct == 5);
        CHECK(rc2.vbrBitsOffTarget == 123);
        // set_rc_param did NOT run.
        CHECK(fi2.numMbs == 0);
    }
}

TEST_CASE("init_second_pass accumulates, derives the framerate and walks the error") {
    std::vector<l15_rc::FirstpassStatsCarrier> buf(3);
    for (int i = 0; i < 3; ++i) {
        buf[i].frame = (double)i + 1.0;
        buf[i].codedError = 10.0 * (i + 1);
        buf[i].count = 1.0;
        buf[i].duration = 1.0;
        buf[i].statStruct.totalNumBits = 100 * (i + 1);
        buf[i].statStruct.temporalLayerIndex = 0;
    }
    l15_rc::FirstpassStatsCarrier statsInEnd{};  // the accumulator cell
    statsInEnd.frame = 99.0;                     // poison: zero_stats must clear
    statsInEnd.duration = 99.0;
    l15_rc::FirstpassStatsCarrier totalStats{};
    totalStats.frame = 88.0;
    l15_rc::FirstpassStatsCarrier totalLeftStats{};
    l15_rc::TwoPassCarrier tp{};
    tp.statsIn = buf.data();
    tp.statsInCount = 3;
    tp.statsInPos = 0;
    tp.hasTotalStats = true;
    tp.bitsLeft = -1;
    tp.kfZeroMotionPct = 0;
    l15_rc::RcControl rc{};
    rc.vbrBitsOffTarget = 123;
    rc.vbrBitsOffTargetFast = 456;
    rc.rateErrorEstimate = 789;
    rc.avgFrameBandwidth = -1;
    l15_rc::FrameInfoCarrier fi{};
    l15_rc::RcConfig cfg{};
    double scsFr = 0.0;
    int v1, v2, sd, sm;
    l15_rc::initSecondPass(tp, buf.data(), &statsInEnd, &totalStats, &totalLeftStats, rc, scsFr, 3000000, 100,
                           false, 1920, 1080, 8, 50, 190, 1, 10, 63, false, 25, 35, 1, 2, 3, 0, 0,
                           fi, cfg, v1, v2, sd, sm);
    // the accumulator cell: zero_stats then accumulate the three frames:
    // frame 1+2+3 = 6, coded_error 10+20+30 = 60, count 3, duration
    // 1 + 1+1+1 = 4 (zero_stats starts duration at 1.0).
    CHECK(std::fabs(statsInEnd.frame - 6.0) <= 1e-9);
    CHECK(std::fabs(statsInEnd.codedError - 60.0) <= 1e-9);
    CHECK(std::fabs(statsInEnd.count - 3.0) <= 1e-9);
    CHECK(std::fabs(statsInEnd.duration - 4.0) <= 1e-9);
    // the accumulate's total_num_bits = 100+200+300 = 600.
    CHECK(statsInEnd.statStruct.totalNumBits == 600);
    // total_stats <- stats_in_end; total_left_stats <- total_stats.
    CHECK(std::fabs(totalStats.frame - 6.0) <= 1e-9);
    CHECK(std::fabs(totalLeftStats.frame - 6.0) <= 1e-9);
    // frame_rate = 10000000.0 * 3 / 4 = 7,500,000.
    CHECK(std::fabs(scsFr - 7500000.0) <= 1e-9);
    CHECK(rc.avgFrameBandwidth == (int)(3000000 / 7500000.0));
    // bits_left = (int64)(4.0 * 3000000 / 10000000.0) = (int64)1.2 = 1.
    CHECK(tp.bitsLeft == 1);
    // read_stat_from_file: no zero-tnb entries; the sum 600 -> total_stats.
    CHECK(totalStats.statStruct.totalNumBits == 600);
    // avg_error = 60 / (3 + 0.000001); the vbrmin/vbrmax 50/190 splits.
    const double avg = 60.0 / (3.0 + 0.000001);
    CHECK(std::fabs(tp.modifiedErrorMin - (avg * 50.0 / 100.0)) <= 1e-9);
    CHECK(std::fabs(tp.modifiedErrorMax - (avg * 190.0 / 100.0)) <= 1e-9);
    // the modified-error walk: calculate_modified_err = tnb per frame.
    CHECK(std::fabs(tp.modifiedErrorLeft - 600.0) <= 1e-9);
    // the counter resets and the zeromotion pin.
    CHECK(rc.vbrBitsOffTarget == 0);
    CHECK(rc.vbrBitsOffTargetFast == 0);
    CHECK(rc.rateErrorEstimate == 0);
    CHECK(tp.kfZeroMotionPct == 100);
}

// ---- A9D S10: pass2_strategy.c svt_av1_twopass_postencode_update_gop_const
// (:1063-1174) + svt_av1_twopass_postencode_update (:1176-1269) ----

namespace {

l15_rc::RcControl peRc() {
    l15_rc::RcControl rc{};
    rc.worstQuality = 255;
    rc.activeWorstQuality = 100;   // maxq_adj_limit = 155
    rc.avgFrameBandwidth = 10000;
    rc.rollingTargetBits = 500;
    rc.rollingActualBits = 400;
    for (int i = 0; i < 7; ++i) rc.activeBestQuality[i] = -1;
    return rc;
}

l15_rc::PcsCarrier pePcs() {
    l15_rc::PcsCarrier pcs{};
    pcs.baseFrameTarget = 1000;
    pcs.projectedFrameSize = 800;  // undershoot: +200
    pcs.isOverlay = false;
    pcs.layerDepth = 2;
    pcs.baseQIdx = 120;
    pcs.frameType = l15_rc::kInterFrame;       // not kf_gf_arf
    pcs.updateType = l15_rc::kSvtAv1LfUpdate;  // not kf_gf_arf
    return pcs;
}

}  // namespace

TEST_CASE("twopass_postencode_update_gop_const pins the undershoot arm") {
    l15_rc::RcControl rc = peRc();
    l15_rc::RcConfig cfg{};
    cfg.underShootPct = 10;
    cfg.overShootPct = 40;
    l15_rc::RcIntervalParamCarrier param{};
    param.vbrBitsOffTarget = 0;
    param.totalActualBits = 1000;
    param.totalTargetBits = 2000;
    param.rollingTargetBits = 500;
    param.rollingActualBits = 400;
    param.extendMinq = 2;
    param.extendMaxq = 2;
    param.extendMinqFast = 5;
    param.vbrBitsOffTargetFast = 7;
    l15_rc::PcsCarrier pcs = pePcs();

    l15_rc::twopassPostencodeUpdateGopConst(rc, cfg, param, pcs, false, pcs.layerDepth);

    // vbr_bits_off_target += 1000 - 800 = +200.
    CHECK(param.vbrBitsOffTarget == 200);
    // rate_error = (200*100)/1000 = 20; target = (200*100)/2000 = 10.
    CHECK(param.rateErrorEstimate == 20);
    // the pyramid: [2..6] = 120, [0..1] untouched (-1).
    CHECK(rc.activeBestQuality[1] == -1);
    CHECK(rc.activeBestQuality[2] == 120);
    CHECK(rc.activeBestQuality[6] == 120);
    // undershoot: --extend_maxq (2->1); rolling 500 >= 400 -> ++extend_minq
    // (2->3); the clamps keep both.
    CHECK(param.extendMinq == 3);
    CHECK(param.extendMaxq == 1);
    // the fast block: projected 800 < thresh 500? NO; vbr_off_fast 7 ->
    // extend_minq_fast = MIN(5, 48-3) = 5.
    CHECK(param.extendMinqFast == 5);
    CHECK(param.vbrBitsOffTargetFast == 7);
}

TEST_CASE("twopass_postencode_update_gop_const pins the overshoot and the fast feed") {
    {
        // overshoot: base 500, projected 1200 -> -700; rate_error = -70
        // < -40 -> --extend_minq; rolling 500 < 600 -> extend_maxq += 1.
        l15_rc::RcControl rc = peRc();
        l15_rc::RcConfig cfg{};
        cfg.underShootPct = 10;
        cfg.overShootPct = 40;
        l15_rc::RcIntervalParamCarrier param{};
        param.totalActualBits = 1000;
        param.totalTargetBits = 2000;
        param.rollingTargetBits = 500;
        param.rollingActualBits = 600;
        param.extendMinq = 2;
        param.extendMaxq = 2;
        l15_rc::PcsCarrier pcs = pePcs();
        pcs.baseFrameTarget = 500;
        pcs.projectedFrameSize = 1200;
        l15_rc::twopassPostencodeUpdateGopConst(rc, cfg, param, pcs, false, pcs.layerDepth);
        CHECK(param.vbrBitsOffTarget == -700);
        CHECK(param.rateErrorEstimate == -70);
        CHECK(param.extendMinq == 1);
        CHECK(param.extendMaxq == 3);
    }
    {
        // the fast feed: base 2000, projected 100 -> thresh 1000 > 100 and
        // rate_error 20 > 0 -> vbr_off_fast += 900 (clamped to
        // 4*avg_frame_bandwidth = 40000 - no clamp here);
        // extend_minq_fast = (int)(900 * 8 / 10000) = 0; MIN(0, 47) = 0.
        l15_rc::RcControl rc = peRc();
        l15_rc::RcConfig cfg{};
        cfg.underShootPct = 10;
        cfg.overShootPct = 40;
        l15_rc::RcIntervalParamCarrier param{};
        param.totalActualBits = 1000;
        param.totalTargetBits = 2000;
        param.rollingTargetBits = 500;
        param.rollingActualBits = 400;
        param.extendMinq = 2;
        param.extendMaxq = 2;
        param.extendMinqFast = 5;
        l15_rc::PcsCarrier pcs = pePcs();
        pcs.baseFrameTarget = 2000;
        pcs.projectedFrameSize = 100;
        l15_rc::twopassPostencodeUpdateGopConst(rc, cfg, param, pcs, false, pcs.layerDepth);
        CHECK(param.vbrBitsOffTarget == 1900);
        CHECK(param.vbrBitsOffTargetFast == 900);
        CHECK(param.extendMinqFast == 0);
    }
    {
        // the kf_gf_arf frame: the fast block SKIPPED entirely (the state
        // keeps its values).
        l15_rc::RcControl rc = peRc();
        l15_rc::RcConfig cfg{};
        cfg.underShootPct = 10;
        cfg.overShootPct = 40;
        l15_rc::RcIntervalParamCarrier param{};
        param.totalActualBits = 1000;
        param.extendMinqFast = 5;
        param.vbrBitsOffTargetFast = 7;
        l15_rc::PcsCarrier pcs = pePcs();
        pcs.updateType = l15_rc::kSvtAv1ArfUpdate;  // kf_gf_arf
        l15_rc::twopassPostencodeUpdateGopConst(rc, cfg, param, pcs, false, pcs.layerDepth);
        CHECK(param.extendMinqFast == 5);
        CHECK(param.vbrBitsOffTargetFast == 7);
    }
    {
        // the overlay frame: NO pyramid write, NO extend adjustments.
        l15_rc::RcControl rc = peRc();
        l15_rc::RcConfig cfg{};
        l15_rc::RcIntervalParamCarrier param{};
        param.totalActualBits = 1000;
        param.extendMinq = 2;
        param.extendMaxq = 2;
        l15_rc::PcsCarrier pcs = pePcs();
        pcs.isOverlay = true;
        l15_rc::twopassPostencodeUpdateGopConst(rc, cfg, param, pcs, false, pcs.layerDepth);
        CHECK(rc.activeBestQuality[2] == -1);
        CHECK(param.extendMinq == 2);
        CHECK(param.extendMaxq == 2);
    }
    {
        // the short-clip overshoot: rate_error_estimate_target < -100
        // (-700*100/500 = -140) -> extend_maxq += 10; the short-clip
        // extend_minq clamp lower bound -12.
        l15_rc::RcControl rc = peRc();
        l15_rc::RcConfig cfg{};
        cfg.underShootPct = 10;
        cfg.overShootPct = 40;
        l15_rc::RcIntervalParamCarrier param{};
        param.totalActualBits = 1000;
        param.totalTargetBits = 500;
        param.rollingTargetBits = 500;
        param.rollingActualBits = 600;
        param.extendMinq = 2;
        param.extendMaxq = 2;
        l15_rc::PcsCarrier pcs = pePcs();
        pcs.baseFrameTarget = 500;
        pcs.projectedFrameSize = 1200;
        l15_rc::twopassPostencodeUpdateGopConst(rc, cfg, param, pcs, true, pcs.layerDepth);
        CHECK(param.extendMinq == 1);
        CHECK(param.extendMaxq == 12);
    }
    {
        // the extreme-local-overshoot else arm: avg_frame_bandwidth 200 ->
        // projected 500 > 2*base 200 and > 2*avg 400 -> ++extend_maxq;
        // rolling 100 < 200 -> --extend_minq (the unwind).
        l15_rc::RcControl rc = peRc();
        rc.avgFrameBandwidth = 200;
        l15_rc::RcConfig cfg{};
        cfg.underShootPct = 5;
        cfg.overShootPct = 40;
        l15_rc::RcIntervalParamCarrier param{};
        param.totalActualBits = 100000;
        param.totalTargetBits = 200000;
        param.rollingTargetBits = 100;
        param.rollingActualBits = 200;
        param.extendMinq = 2;
        param.extendMaxq = 2;
        l15_rc::PcsCarrier pcs = pePcs();
        pcs.baseFrameTarget = 100;
        pcs.projectedFrameSize = 500;
        l15_rc::twopassPostencodeUpdateGopConst(rc, cfg, param, pcs, false, pcs.layerDepth);
        // rate_error = (400*100)/100000 = 0 -> the else arm.
        CHECK(param.rateErrorEstimate == 0);
        CHECK(param.extendMinq == 1);
        CHECK(param.extendMaxq == 3);
    }
}

TEST_CASE("twopass_postencode_update pins the rc/twopass state split") {
    {
        // undershoot on the rc/twopass walls.
        l15_rc::RcControl rc = peRc();
        rc.totalActualBits = 1000;
        rc.totalTargetBits = 2000;
        rc.vbrBitsOffTargetFast = 7;
        l15_rc::RcConfig cfg{};
        cfg.underShootPct = 10;
        cfg.overShootPct = 40;
        l15_rc::TwoPassCarrier tp{};
        tp.extendMinq = 2;
        tp.extendMaxq = 2;
        tp.extendMinqFast = 5;
        l15_rc::PcsCarrier pcs = pePcs();
        l15_rc::twopassPostencodeUpdate(rc, cfg, tp, pcs, false, pcs.layerDepth);
        CHECK(rc.vbrBitsOffTarget == 200);
        CHECK(rc.rateErrorEstimate == 20);
        CHECK(rc.activeBestQuality[2] == 120);
        CHECK(tp.extendMinq == 3);
        CHECK(tp.extendMaxq == 1);
        CHECK(tp.extendMinqFast == 5);
    }
    {
        // the twopass twin has NO short-clip else-block: the else arm with
        // short clip does NOT run the -5/+5 adjustments. avg_frame_bandwidth
        // 200 makes the local-overshoot ++ fire; the unwind then pulls it
        // back (--), netting the original value.
        l15_rc::RcControl rc = peRc();
        rc.avgFrameBandwidth = 200;
        rc.totalActualBits = 100000;
        rc.totalTargetBits = 200000;
        rc.rollingTargetBits = 300;  // > actual -> the unwind hits extend_maxq
        rc.rollingActualBits = 200;
        l15_rc::RcConfig cfg{};
        cfg.underShootPct = 5;
        cfg.overShootPct = 40;
        l15_rc::TwoPassCarrier tp{};
        tp.extendMinq = 40;  // > 48/3 = 16: the gop-const twin would -5
        tp.extendMaxq = 2;
        l15_rc::PcsCarrier pcs = pePcs();
        pcs.baseFrameTarget = 100;
        pcs.projectedFrameSize = 500;
        l15_rc::twopassPostencodeUpdate(rc, cfg, tp, pcs, true, pcs.layerDepth);
        // else arm: ++extend_maxq (2->3, projected 500 > 2*avg 400); unwind
        // rolling 300 > 200 -> --extend_maxq (3->2); NO short-clip block;
        // extend_minq stays 40 (clamped 0..48); extend_maxq NOT clamped
        // (short clip).
        CHECK(tp.extendMinq == 40);
        CHECK(tp.extendMaxq == 2);
    }
}

TEST_CASE("kf_group_rate_assingment pins the lap path and the clip") {
    const std::vector<l15_rc::FirstpassStatsCarrier> stats = kfWindow();
    {
        // lap: frames_to_key = intra + 1 = 11; kf_group_bits =
        // 11 * 10000 = 110000 (frames_in_sw 3 < 10); the max_grp_bits clip
        // 19000 * 11 = 209000 -> no clip.
        // bits_left -= (110000 + (3 - 11) * 100000) = 100000 + 690000.
        // frames_to_key_clipped = (int)(5 * 30) = 150 >= 11 -> no bit clip.
        // kf_bits = calculate_boost_bits(MIN(11, 150) - 1 = 10, 400,
        // 110000): chunks 1400 -> 400*110000/1400 = 31428.
        l15_rc::TwoPassCarrier tp = kfWalk(stats);
        l15_rc::RcControl rc = kfRc();
        l15_rc::PcsCarrier pcs{};
        l15_rc::RcIntervalParamCarrier param{};
        pcs.framesInSw = 3;
        l15_rc::FirstpassStatsCarrier arg = kfArg();
        l15_rc::kfGroupRateAssignment(pcs, rc, tp, arg, param, true, 10, 3000000, 30.0, 190, 1);
        CHECK(rc.framesToKey == 11);
        CHECK(tp.kfGroupBits == 110000 - 31428);
        CHECK(tp.bitsLeft == 100000 - (110000 - 800000));
        CHECK(pcs.baseFrameTarget == 31428);
    }
    {
        // the kf_group_bits clip: a tiny max_frame_bandwidth. avg 10000,
        // vbrmax 10 -> max_bits 1000; max_grp_bits = 1000 * 4 = 4000 ->
        // kf_group_bits 4000; bits_left 96000.
        l15_rc::TwoPassCarrier tp = kfWalk(stats);
        l15_rc::RcControl rc = kfRc();
        rc.maxFrameBandwidth = 2000;
        l15_rc::PcsCarrier pcs{};
        l15_rc::RcIntervalParamCarrier param{};
        l15_rc::FirstpassStatsCarrier arg = kfArg();
        // kf_group_bits 4000; then kf_bits = calculate_boost_bits(3, 400,
        // 4000): chunks 700 -> 400*4000/700 = 2285; the final -= gives 1715.
        l15_rc::kfGroupRateAssignment(pcs, rc, tp, arg, param, false, 10, 3000000, 30.0, 10, 1);
        CHECK(tp.kfGroupBits == 4000 - 2285);
        CHECK(tp.bitsLeft == 96000);
    }
}
