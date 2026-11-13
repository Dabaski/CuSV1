#include <doctest.h>
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
