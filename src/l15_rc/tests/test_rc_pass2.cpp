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
