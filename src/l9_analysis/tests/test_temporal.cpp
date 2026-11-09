#include <doctest.h>

#include <cmath>
#include <iterator>
#include <random>
#include <vector>

#include "temporal_filter.h"

TEST_CASE("l9 temporal scaffold boots") {
    CHECK(true);
}

// TF1 S1: the noise_log1p_fp16 underflow branch. base_fp16 = 65536 +
// noise_level_fp16 <= 0 returns INT32_MIN (temporal_filtering.c:569-572).
TEST_CASE("l9 temporal noiseLog1pFp16 base<=0 returns int32 min") {
    using analysis::temporal::noiseLog1pFp16;
    const int32_t kIntMin = -2147483647 - 1;
    CHECK_EQ(noiseLog1pFp16(-65537), kIntMin);
    CHECK_EQ(noiseLog1pFp16(-65536), kIntMin);
    CHECK_EQ(noiseLog1pFp16(-100000), kIntMin);
}

// TF1 S2: interpolation branch, values hand-computed from the reference
// table + formula (temporal_filtering.c:573-580).
TEST_CASE("l9 temporal noiseLog1pFp16 table interpolation branch") {
    using analysis::temporal::noiseLog1pFp16;
    CHECK_EQ(noiseLog1pFp16(0), 0);
    CHECK_EQ(noiseLog1pFp16(1024), 1008);
    CHECK_EQ(noiseLog1pFp16(512), 504);
    CHECK_EQ(noiseLog1pFp16(4096), 3973);
    CHECK_EQ(noiseLog1pFp16(8192), 7719);
}

// TF1 S3: linear branch for base_fp16 >= 7:fp16, exact formula
// y = (1860*(nl>>8))>>8 + 116456 (temporal_filtering.c:581-585), plus a
// closeness cross-check against std::log1p over an enumerated set.
TEST_CASE("l9 temporal noiseLog1pFp16 linear branch and log1p closeness") {
    using analysis::temporal::noiseLog1pFp16;
    CHECK_EQ(noiseLog1pFp16(393216), 127616);
    CHECK_EQ(noiseLog1pFp16(500000), 130645);
    CHECK_EQ(noiseLog1pFp16(1048576), ((1860 * (1048576 >> 8)) >> 8) + 116456);

    // Independent cross-check against 65536*log1p(nl/65536) for nl >=
    // -32768 (x >= -0.5); MEASURED max delta 29.6 fp16 units over the
    // enumerated range, so tolerance 32. The steep region near x = -1
    // (linear-interp error grows unbounded toward the -inf sentinel) and
    // the linear extrapolation branch are excluded from the closeness
    // bound; both are pinned exactly elsewhere.
    double maxDelta = 0.0;
    for (int32_t nl = -32768; nl <= 393215; nl += 256) {
        double expected = 65536.0 * std::log1p(static_cast<double>(nl) / 65536.0);
        double delta = std::abs(static_cast<double>(noiseLog1pFp16(nl)) - expected);
        if (delta > maxDelta) {
            maxDelta = delta;
        }
        CHECK_LE(delta, 32.0);
    }
    MESSAGE("max log1p closeness delta: ", maxDelta);
}

// TF1 S4: expf_tab_fp16 (temporal_filtering.c:624-634), 129 entries,
// T[X] = exp(-X/16) in fp16. Formula oracle: |tab[i] - 65536*exp(-i/16)|
// <= 1.0 for every entry (MEASURED max 0.9975 at i=76). Also pins the
// kernel index range: every planewise kernel caps scaled_diff16 at
// 7*16 = 112 (AOMMIN ... 7 * 16), so indices touched are [0, 112].
TEST_CASE("l9 temporal expfTabFp16 formula oracle and kernel index range") {
    using analysis::temporal::expfTabFp16;
    REQUIRE_EQ(std::size(expfTabFp16), static_cast<std::size_t>(129));
    double maxDelta = 0.0;
    for (std::size_t i = 0; i < std::size(expfTabFp16); ++i) {
        double expected = 65536.0 * std::exp(-static_cast<double>(i) / 16.0);
        double delta = std::abs(static_cast<double>(expfTabFp16[i]) - expected);
        if (delta > maxDelta) {
            maxDelta = delta;
        }
        CHECK_LE(delta, 1.0);
    }
    MESSAGE("max expfTabFp16 formula delta: ", maxDelta);
    // Kernel index range: max scaled_diff16 = 112 must stay in bounds.
    CHECK_LT(7 * 16, static_cast<int>(std::size(expfTabFp16)));
    CHECK_EQ(expfTabFp16[112], 59);
}

// TF1 S5: svt_aom_expf_tab (temporal_filtering.c:666-677), 81 entries,
// tab[i] = exp(-i/10) to 6 decimal places. Formula oracle with tolerance
// from the 6-decimal rounding plus the double representation of the
// decimal literal (MEASURED max delta 5.24e-07).
TEST_CASE("l9 temporal expfTab formula oracle") {
    using analysis::temporal::expfTab;
    REQUIRE_EQ(std::size(expfTab), static_cast<std::size_t>(81));
    double maxDelta = 0.0;
    for (std::size_t i = 0; i < std::size(expfTab); ++i) {
        double expected = std::exp(-static_cast<double>(i) / 10.0);
        double delta = std::abs(expfTab[i] - expected);
        if (delta > maxDelta) {
            maxDelta = delta;
        }
        CHECK_LE(delta, 0.0000006);
    }
    MESSAGE("max expfTab formula delta: ", maxDelta);
}

// TF1 S6: log1p_tab_fp16 (temporal_filtering.c:339-565), 225 entries,
// tab[i] = log1p(i/32 - 1) in fp16. Entry 0 is the log1p(-1) = -inf
// sentinel pinned exactly; entries 1..224 checked against the formula
// with tolerance 1.0 (MEASURED max delta 0.9984 at i=17).
TEST_CASE("l9 temporal log1pTabFp16 formula oracle") {
    using analysis::temporal::log1pTabFp16;
    REQUIRE_EQ(std::size(log1pTabFp16), static_cast<std::size_t>(225));
    CHECK_EQ(log1pTabFp16[0], -2147483647 - 1);
    double maxDelta = 0.0;
    for (std::size_t i = 1; i < std::size(log1pTabFp16); ++i) {
        double expected = 65536.0 * std::log1p(static_cast<double>(i) / 32.0 - 1.0);
        double delta = std::abs(static_cast<double>(log1pTabFp16[i]) - expected);
        if (delta > maxDelta) {
            maxDelta = delta;
        }
        CHECK_LE(delta, 1.0);
    }
    MESSAGE("max log1pTabFp16 formula delta: ", maxDelta);
}

// TF1 S7: sqrt_array_fp16 (temporal_filtering.c:637-652) and sqrt_fast
// (:655-664, svt_log2f = get_msb per definitions.h:592/:596-612).
// Exact pins at small x; for kernel-range inputs (the non-zz kernels
// pass (col*col+row*row) << 8, so x >= 256) the reference documents
// ~10% max linear error - MEASURED max relative error 0.1054 at x=5118,
// tolerance 0.11.
TEST_CASE("l9 temporal sqrtFast oracle") {
    using analysis::temporal::sqrtFast;
    using analysis::temporal::sqrtArrayFp16;
    REQUIRE_EQ(std::size(sqrtArrayFp16), static_cast<std::size_t>(16));
    CHECK_EQ(sqrtArrayFp16[0], 0u);
    CHECK_EQ(sqrtArrayFp16[15], 253819u);
    CHECK_EQ(sqrtFast(1), 1u);
    CHECK_EQ(sqrtFast(9), 3u);
    CHECK_EQ(sqrtFast(15), 3u);
    CHECK_EQ(sqrtFast(16), 4u);
    CHECK_EQ(sqrtFast(256), 16u);
    double maxRel = 0.0;
    std::mt19937 rng(1);
    std::vector<uint32_t> samples = {256, 512, 1024, 2048, 16384, 65536, 262144, 1048576, 4000000, 1000000};
    for (int k = 0; k < 3000; ++k) {
        samples.push_back(256 + rng() % 4200000);
    }
    for (uint32_t x : samples) {
        double approx = static_cast<double>(sqrtFast(x));
        double trueSqrt = std::sqrt(static_cast<double>(x));
        double rel = std::abs(approx - trueSqrt) / trueSqrt;
        if (rel > maxRel) {
            maxRel = rel;
        }
        CHECK_LE(rel, 0.11);
    }
    MESSAGE("max sqrtFast relative error: ", maxRel);
}

// TF1 S8: calculate_tf_shift_factor (temporal_filtering.c:610-619). The
// consumed MeContext field tf_64x64_block_error is threaded as a
// parameter; block_err = value >> 12, thresholds LOW_ERROR_THRESHOLD 200
// and MED_ERROR_THRESHOLD 2000 (temporal_filtering.h:65-66). Boundaries
// enumerated, not spot-checked.
TEST_CASE("l9 temporal calculateTfShiftFactor threshold boundaries") {
    using analysis::temporal::calculateTfShiftFactor;
    CHECK_EQ(calculateTfShiftFactor(0), 14);
    CHECK_EQ(calculateTfShiftFactor(199 * 4096), 14);
    CHECK_EQ(calculateTfShiftFactor(199 * 4096 + 4095), 14);
    CHECK_EQ(calculateTfShiftFactor(200 * 4096), 13);
    CHECK_EQ(calculateTfShiftFactor(1999 * 4096), 13);
    CHECK_EQ(calculateTfShiftFactor(1999 * 4096 + 4095), 13);
    CHECK_EQ(calculateTfShiftFactor(2000 * 4096), 12);
    CHECK_EQ(calculateTfShiftFactor(100000 * 4096), 12);
}

// TF1 S9: svt_av1_calculate_decay_factor (temporal_filtering.c:589-607).
// Threading (named): the MeContext field tf_decay_factor_fp16[3] is the
// out-array; n_decay_fp10 is the caller-computed luma value
// (temporal_filtering.c:2732-2733); decay_control_cu/cv, const_0dot7_fp16
// and noise_levels_log1p_fp16 are threaded as parameters. Oracle: an
// independent scalar replication of the reference arithmetic.
TEST_CASE("l9 temporal calculateDecayFactor luma only") {
    using analysis::temporal::calculateDecayFactor;
    uint32_t tf[3] = {7, 7, 7};
    calculateDecayFactor(tf, 1000, 256, 5, 6, 45875, nullptr, 11, false);
    CHECK_EQ(tf[0], (1000LL * 1000 * 256) >> 11);
    // chroma untouched when tf_chroma is false
    CHECK_EQ(tf[1], 7u);
    CHECK_EQ(tf[2], 7u);
}

TEST_CASE("l9 temporal calculateDecayFactor chroma") {
    using analysis::temporal::calculateDecayFactor;
    const int32_t noise[3] = {30000, 10000, 20000};
    uint32_t tf[3] = {0, 0, 0};
    calculateDecayFactor(tf, 1000, 256, 5, 6, 45875, noise, 11, true);
    const int64_t nDecayU = (5 * (45875 + 10000)) / 64;
    const int64_t nDecayV = (6 * (45875 + 20000)) / 64;
    CHECK_EQ(tf[0], (1000LL * 1000 * 256) >> 11);
    CHECK_EQ(tf[1], static_cast<uint32_t>((nDecayU * nDecayU * 256) >> 11));
    CHECK_EQ(tf[2], static_cast<uint32_t>((nDecayV * nDecayV * 256) >> 11));
}

TEST_CASE("l9 temporal calculateDecayFactor shift 14 and zero decay") {
    using analysis::temporal::calculateDecayFactor;
    const int32_t noise[3] = {0, 0, 0};
    uint32_t tf[3] = {0, 0, 0};
    calculateDecayFactor(tf, 1000, 256, 1, 1, 45875, noise, 14, true);
    const int64_t nDecayU = (1 * (45875 + 0)) / 64;
    const int64_t nDecayV = (1 * (45875 + 0)) / 64;
    CHECK_EQ(tf[0], (1000LL * 1000 * 256) >> 14);
    CHECK_EQ(tf[1], static_cast<uint32_t>((nDecayU * nDecayU * 256) >> 14));
    CHECK_EQ(tf[2], static_cast<uint32_t>((nDecayV * nDecayV * 256) >> 14));
}

// TF1 S10: calculate_squared_errors_sum (temporal_filtering.c:697-707).
TEST_CASE("l9 temporal calculateSquaredErrorsSum respects strides") {
    using analysis::temporal::calculateSquaredErrorsSum;
    // 3x2 window inside a 5-wide buffer with padding columns
    const uint8_t s[] = {10, 20, 30, 99, 99,
                         40, 5,  8,  99, 99,
                         99, 99, 99, 99, 99};
    const uint8_t p[] = {12, 18, 30, 0, 0,
                         44, 0,  10, 0, 0,
                         0,  0,  0,  0, 0};
    // sum = 4+4+0+16+25+4 = 53
    CHECK_EQ(calculateSquaredErrorsSum(s, 5, p, 5, 3, 2), 53u);
    CHECK_EQ(calculateSquaredErrorsSum(s, 5, p, 5, 0, 2), 0u);
}

// TF1 S11: svt_av1_apply_zz_based_temporal_filter_planewise_medium_
// partial_c (temporal_filtering.c:738-781). Threading (named): the
// MeContext fields tf_32x32_block_split_flag[idx_32x32],
// tf_16x16_block_error[idx_32x32*4..+3] and tf_32x32_block_error
// [idx_32x32] are threaded as resolved per-call values (splitFlag,
// blockError16x16[4], blockError32x32); the idx_32x32 =
// tf_block_col + tf_block_row*2 resolution is the caller's job.
// y_accum is uint32, y_count is uint16 (:740); the weight is
// (expf_tab_fp16[scaled_diff16] * TF_WEIGHT_SCALE) >> 17 (:767) with
// TF_WEIGHT_SCALE 1000 (temporal_filtering.h:33); scaled_diff16 caps at
// 7*16 with the AOMMAX(decay>>10, 1) guard (:764-765). Oracle: an
// independent scalar replication derived from the reference.
namespace {

struct ZzFixture {
    static const int kStride = 16;
    static const int kDim = 8;  // block 8x8, subblocks 4x4
    uint8_t pre[kStride * 12];
    uint32_t accum[kStride * 12];
    uint16_t count[kStride * 12];

    ZzFixture() {
        for (int i = 0; i < kStride * 12; ++i) {
            pre[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
            accum[i] = 0;
            count[i] = 0;
        }
    }
};

// Independent scalar oracle for the zz partial kernel.
void zzOracle(bool splitFlag, const uint64_t* blockError16x16, uint64_t blockError32x32, const uint8_t* yPre,
              int yPreStride, unsigned blockWidth, unsigned blockHeight, uint32_t* yAccum, uint16_t* yCount,
              uint32_t tfDecayFactorFp16) {
    using analysis::temporal::expfTabFp16;
    const uint32_t kWeightScale = 1000;  // TF_WEIGHT_SCALE
    uint32_t blockErrorFp8[4];
    if (splitFlag) {
        for (int i = 0; i < 4; ++i) {
            blockErrorFp8[i] = static_cast<uint32_t>(blockError16x16[i]);
        }
    } else {
        for (int i = 0; i < 4; ++i) {
            blockErrorFp8[i] = static_cast<uint32_t>(blockError32x32 >> 2);
        }
    }
    for (unsigned subblock = 0; subblock < 4; ++subblock) {
        uint32_t avgErrFp10 = blockErrorFp8[subblock] << 2;
        uint32_t divisor = tfDecayFactorFp16 >> 10;
        if (divisor < 1) {
            divisor = 1;
        }
        uint32_t scaledDiff16 = avgErrFp10 / divisor;
        if (scaledDiff16 > 7u * 16) {
            scaledDiff16 = 7u * 16;
        }
        uint32_t weight = static_cast<uint32_t>((static_cast<uint64_t>(expfTabFp16[scaledDiff16]) * kWeightScale) >> 17);
        int xOffset = static_cast<int>((subblock % 2) * blockWidth / 2);
        int yOffset = static_cast<int>((subblock / 2) * blockHeight / 2);
        for (unsigned i = 0; i < blockHeight / 2; ++i) {
            for (unsigned j = 0; j < blockWidth / 2; ++j) {
                const int k = (i + yOffset) * yPreStride + j + xOffset;
                yCount[k] = static_cast<uint16_t>(yCount[k] + weight);
                yAccum[k] += weight * yPre[k];
            }
        }
    }
}

void checkAgainstOracle(const ZzFixture& f, bool splitFlag, const uint64_t* blockError16x16,
                        uint64_t blockError32x32, uint32_t tfDecayFactorFp16) {
    ZzFixture oracle;
    zzOracle(splitFlag, blockError16x16, blockError32x32, oracle.pre, ZzFixture::kStride, ZzFixture::kDim,
             ZzFixture::kDim, oracle.accum, oracle.count, tfDecayFactorFp16);
    for (int i = 0; i < ZzFixture::kStride * 12; ++i) {
        CHECK_EQ(f.count[i], oracle.count[i]);
        CHECK_EQ(f.accum[i], oracle.accum[i]);
    }
}

}  // namespace

TEST_CASE("l9 temporal zz partial kernel split path matches oracle") {
    using analysis::temporal::applyZzBasedTemporalFilterPlanewiseMediumPartial;
    ZzFixture f;
    const uint64_t err16[4] = {4, 8, 12, 16};
    applyZzBasedTemporalFilterPlanewiseMediumPartial(true, err16, 0, f.pre, ZzFixture::kStride, ZzFixture::kDim,
                                                     ZzFixture::kDim, f.accum, f.count, 1u << 10);
    // Calibration: the four subblock weights are pairwise distinct, so
    // the fixture really exercises the per-subblock split.
    uint32_t weights[4];
    for (int s = 0; s < 4; ++s) {
        uint32_t avgErrFp10 = static_cast<uint32_t>(err16[s]) << 2;
        uint32_t sd = avgErrFp10 / 1;  // decay>>10 == 1
        weights[s] = static_cast<uint32_t>((static_cast<uint64_t>(analysis::temporal::expfTabFp16[sd]) * 1000) >> 17);
    }
    CHECK_NE(weights[0], weights[1]);
    CHECK_NE(weights[0], weights[2]);
    CHECK_NE(weights[0], weights[3]);
    CHECK_NE(weights[1], weights[2]);
    CHECK_NE(weights[1], weights[3]);
    CHECK_NE(weights[2], weights[3]);
    checkAgainstOracle(f, true, err16, 0, 1u << 10);
}

TEST_CASE("l9 temporal zz partial kernel non-split path matches oracle") {
    using analysis::temporal::applyZzBasedTemporalFilterPlanewiseMediumPartial;
    ZzFixture f;
    const uint64_t err16[4] = {99, 99, 99, 99};  // ignored in the non-split path
    // Non-split: all four subblocks take (err32x32 >> 2) = 3, giving one
    // weight for all pixels.
    applyZzBasedTemporalFilterPlanewiseMediumPartial(false, err16, 12, f.pre, ZzFixture::kStride, ZzFixture::kDim,
                                                     ZzFixture::kDim, f.accum, f.count, 1u << 10);
    checkAgainstOracle(f, false, err16, 12, 1u << 10);
    // Calibration for the non-split path: all four subblock weights
    // coincide.
    uint32_t avgErrFp10 = static_cast<uint32_t>(12 >> 2) << 2;
    uint32_t w = static_cast<uint32_t>((static_cast<uint64_t>(analysis::temporal::expfTabFp16[avgErrFp10]) * 1000) >> 17);
    CHECK_EQ(f.count[0], static_cast<uint16_t>(w));
}

TEST_CASE("l9 temporal zz partial kernel AOMMAX guard fires on zero decay") {
    using analysis::temporal::applyZzBasedTemporalFilterPlanewiseMediumPartial;
    ZzFixture f;
    const uint64_t err16[4] = {200, 100, 50, 25};
    // tfDecayFactorFp16 = 512: >> 10 == 0, so the AOMMAX(.., 1) guard
    // supplies the divisor 1.
    applyZzBasedTemporalFilterPlanewiseMediumPartial(true, err16, 0, f.pre, ZzFixture::kStride, ZzFixture::kDim,
                                                     ZzFixture::kDim, f.accum, f.count, 512);
    checkAgainstOracle(f, true, err16, 0, 512);
    // Calibration: without the guard the divisor would be 0 (division
    // by zero); the weights here equal the scaled_diff16 = avg_err<<2
    // table lookups capped at 112.
    uint32_t sd = (static_cast<uint32_t>(err16[0]) << 2) / 1;
    if (sd > 7u * 16) {
        sd = 7u * 16;
    }
    uint32_t w = static_cast<uint32_t>((static_cast<uint64_t>(analysis::temporal::expfTabFp16[sd]) * 1000) >> 17);
    CHECK_EQ(f.count[0], static_cast<uint16_t>(w));
}

TEST_CASE("l9 temporal zz partial kernel scaled_diff16 cap at 7*16") {
    using analysis::temporal::applyZzBasedTemporalFilterPlanewiseMediumPartial;
    ZzFixture f;
    const uint64_t err16[4] = {1000, 1000, 1000, 1000};
    // avg_err_fp10 = 4000, divisor 1 -> uncapped 4000 > 112, so the cap
    // fires for every subblock.
    applyZzBasedTemporalFilterPlanewiseMediumPartial(true, err16, 0, f.pre, ZzFixture::kStride, ZzFixture::kDim,
                                                     ZzFixture::kDim, f.accum, f.count, 1u << 10);
    checkAgainstOracle(f, true, err16, 0, 1u << 10);
    uint32_t w = static_cast<uint32_t>((static_cast<uint64_t>(analysis::temporal::expfTabFp16[112]) * 1000) >> 17);
    CHECK_EQ(f.count[0], static_cast<uint16_t>(w));
}

// TF1 S12: svt_av1_apply_zz_based_temporal_filter_planewise_medium_c
// (temporal_filtering.c:783-814). Threading (named): tf_decay_factor_
// fp16[3] and tf_chroma are threaded as parameters; the per-plane calls
// use decay[0]/[1]/[2] and the chroma dims are blockWidth >> ss_x,
// blockHeight >> ss_y (:799-800).
TEST_CASE("l9 temporal zz medium luma only when chroma disabled") {
    using analysis::temporal::applyZzBasedTemporalFilterPlanewiseMedium;
    ZzFixture f;
    ZzFixture u;
    ZzFixture v;
    const uint64_t err16[4] = {4, 8, 12, 16};
    const uint32_t decay[3] = {1u << 10, 1u << 11, 1u << 12};
    applyZzBasedTemporalFilterPlanewiseMedium(true, err16, 0, f.pre, ZzFixture::kStride, u.pre, v.pre,
                                              ZzFixture::kStride, ZzFixture::kDim, ZzFixture::kDim, 1, 1, f.accum,
                                              f.count, u.accum, u.count, v.accum, v.count, decay, false);
    ZzFixture o;
    zzOracle(true, err16, 0, o.pre, ZzFixture::kStride, ZzFixture::kDim, ZzFixture::kDim, o.accum, o.count,
             decay[0]);
    for (int i = 0; i < ZzFixture::kStride * 12; ++i) {
        CHECK_EQ(f.count[i], o.count[i]);
        CHECK_EQ(f.accum[i], o.accum[i]);
        CHECK_EQ(u.accum[i], 0u);  // chroma untouched
        CHECK_EQ(v.accum[i], 0u);
    }
}

TEST_CASE("l9 temporal zz medium chroma planes with subsampled dims") {
    using analysis::temporal::applyZzBasedTemporalFilterPlanewiseMedium;
    ZzFixture f;
    ZzFixture u;
    ZzFixture v;
    const uint64_t err16[4] = {4, 8, 12, 16};
    const uint32_t decay[3] = {1u << 10, 1u << 11, 1u << 12};
    applyZzBasedTemporalFilterPlanewiseMedium(true, err16, 0, f.pre, ZzFixture::kStride, u.pre, v.pre,
                                              ZzFixture::kStride, ZzFixture::kDim, ZzFixture::kDim, 1, 1, f.accum,
                                              f.count, u.accum, u.count, v.accum, v.count, decay, true);
    // Luma at 8x8, chroma at 4x4 (8 >> ss_x).
    ZzFixture o;
    zzOracle(true, err16, 0, o.pre, ZzFixture::kStride, ZzFixture::kDim, ZzFixture::kDim, o.accum, o.count,
             decay[0]);
    for (int i = 0; i < ZzFixture::kStride * 12; ++i) {
        CHECK_EQ(f.count[i], o.count[i]);
        CHECK_EQ(f.accum[i], o.accum[i]);
    }
    uint32_t uAccumRef[ZzFixture::kStride * 12] = {};
    uint16_t uCountRef[ZzFixture::kStride * 12] = {};
    uint32_t vAccumRef[ZzFixture::kStride * 12] = {};
    uint16_t vCountRef[ZzFixture::kStride * 12] = {};
    zzOracle(true, err16, 0, u.pre, ZzFixture::kStride, ZzFixture::kDim >> 1, ZzFixture::kDim >> 1, uAccumRef,
             uCountRef, decay[1]);
    zzOracle(true, err16, 0, v.pre, ZzFixture::kStride, ZzFixture::kDim >> 1, ZzFixture::kDim >> 1, vAccumRef,
             vCountRef, decay[2]);
    for (int i = 0; i < ZzFixture::kStride * 12; ++i) {
        CHECK_EQ(u.count[i], uCountRef[i]);
        CHECK_EQ(u.accum[i], uAccumRef[i]);
        CHECK_EQ(v.count[i], vCountRef[i]);
        CHECK_EQ(v.accum[i], vAccumRef[i]);
    }
}

// TF1 S13: svt_av1_apply_temporal_filter_planewise_medium_partial_c
// (temporal_filtering.c:930-1037). Threading (named): the consumed
// MeContext fields are threaded as resolved per-call values -
// mvDistTh (tf_mv_dist_th), splitFlag, mv16x16X/Y[4] and
// blockError16x16[4] (the idx_32x32 slice of tf_16x16_mv_x/y and
// tf_16x16_block_error), mv32x32X/Y and blockError32x32 (the idx_32x32
// slice of tf_32x32_mv_x/y and tf_32x32_block_error). lumaWindowError
// QuadFp8 is the caller-owned shared window-error array (written by the
// luma call, consumed by the chroma calls); isChroma selects the
// chroma blend (window*5 + luma)/6 (:1003-1008). NOTE: the non-split
// branch doubles the LOCAL decay copy (:961). Weight shift here is
// >> 16 (:1023), unlike the zz kernels' >> 17.
// TF1 S13 fixture design (calibrated): per-quadrant SSE = q+1 (one
// extra diff-1 pixel per quadrant), so window errors are 16*(q+1); the
// errors/mvs are chosen so every subblock weight is >= 1 and the four
// weights are pairwise distinct - a zero-weight or shared-weight bug
// fails the CHECKs. The oracle is an independent scalar replication of
// the reference arithmetic (weight table + shifts + quadrant split).
namespace {

struct NonZzFixture {
    static const int kStride = 16;
    static const int kDim = 8;  // block 8x8, quadrants 4x4
    uint8_t src[kStride * 12];
    uint8_t pre[kStride * 12];
    uint32_t accum[kStride * 12];
    uint16_t count[kStride * 12];

    NonZzFixture() {
        for (int i = 0; i < kStride * 12; ++i) {
            pre[i] = static_cast<uint8_t>((i * 7 + 3) & 0x7F);
            src[i] = pre[i];
            accum[i] = 0;
            count[i] = 0;
        }
        // quadrant q gets q+1 pixels with |src - pre| = 1, scanned
        // row-major within the quadrant; quadrant q covers rows
        // (q/2)*4.. and cols (q%2)*4.. of the 8x8 block.
        for (int q = 0; q < 4; ++q) {
            for (int p = 0; p <= q; ++p) {
                int r = (q / 2) * 4 + p / 4;
                int c = (q % 2) * 4 + p % 4;
                int idx = r * kStride + c;
                if (pre[idx] < 255) {
                    src[idx] = static_cast<uint8_t>(pre[idx] + 1);
                } else {
                    src[idx] = static_cast<uint8_t>(pre[idx] - 1);
                }
            }
        }
    }
};

uint32_t getMsbOracle(uint32_t n) {
    uint32_t p = 0;
    while (n > 1) {
        n >>= 1;
        ++p;
    }
    return p;
}

uint32_t sqrtFastOracle(uint32_t x) {
    // Independent re-derivation from the reference formula; the table
    // itself is diff-verified in the sqrtFast test case above.
    static const uint32_t kSqrtArrayRef[16] = {0,     65536,  92681,  113511, 131072, 146542,
                                               160529, 173391, 185363, 196608, 207243, 217358,
                                               227023, 236293, 245213, 253819};
    if (x > 15) {
        uint32_t log2Half = getMsbOracle(x) >> 1;
        uint32_t mul2 = log2Half << 1;
        uint32_t base = x >> (mul2 - 2);
        return kSqrtArrayRef[base] >> (17 - log2Half);
    }
    return kSqrtArrayRef[x] >> 16;
}

void nonZzOracle(uint16_t mvDistTh, bool splitFlag, const int16_t* mv16x16X, const int16_t* mv16x16Y,
                 const uint64_t* blockError16x16, int16_t mv32x32X, int16_t mv32x32Y, uint64_t blockError32x32,
                 const uint8_t* ySrc, int ySrcStride, const uint8_t* yPre, int yPreStride, unsigned blockWidth,
                 unsigned blockHeight, uint32_t* yAccum, uint16_t* yCount, uint32_t tfDecayFactorFp16,
                 uint32_t lumaWindowErrorQuadFp8[4], bool isChroma) {
    using analysis::temporal::expfTabFp16;
    const uint32_t kWeightScale = 1000;  // TF_WEIGHT_SCALE
    const uint32_t kBalance = 5;         // TF_WINDOW_BLOCK_BALANCE_WEIGHT

    uint32_t distanceThresholdFp16 = (static_cast<uint32_t>(mvDistTh) << 16) / 10;
    if (distanceThresholdFp16 < (1u << 16)) {
        distanceThresholdFp16 = 1u << 16;
    }

    uint32_t dFactorFp8[4];
    uint32_t blockErrorFp8[4];
    uint32_t chromaWindowErrorQuadFp8[4];
    uint32_t* windowErrorQuadFp8 = isChroma ? chromaWindowErrorQuadFp8 : lumaWindowErrorQuadFp8;

    if (splitFlag) {
        for (int i = 0; i < 4; ++i) {
            int32_t col = mv16x16X[i];
            int32_t row = mv16x16Y[i];
            uint32_t distanceFp4 = sqrtFastOracle(static_cast<uint32_t>(col * col + row * row) << 8);
            uint32_t d = (distanceFp4 << 12) / (distanceThresholdFp16 >> 8);
            dFactorFp8[i] = d < (1u << 8) ? (1u << 8) : d;
            blockErrorFp8[i] = static_cast<uint32_t>(blockError16x16[i]);
        }
    } else {
        tfDecayFactorFp16 <<= 1;
        int32_t col = mv32x32X;
        int32_t row = mv32x32Y;
        uint32_t distanceFp4 = sqrtFastOracle(static_cast<uint32_t>(col * col + row * row) << 8);
        uint32_t d = (distanceFp4 << 12) / (distanceThresholdFp16 >> 8);
        d = d < (1u << 8) ? (1u << 8) : d;
        dFactorFp8[0] = dFactorFp8[1] = dFactorFp8[2] = dFactorFp8[3] = d;
        blockErrorFp8[0] = blockErrorFp8[1] = blockErrorFp8[2] = blockErrorFp8[3] =
            static_cast<uint32_t>(blockError32x32 >> 2);
    }
    const uint32_t bwHalf = blockWidth >> 1;
    const uint32_t bhHalf = blockHeight >> 1;
    for (int q = 0; q < 4; ++q) {
        const uint8_t* sBase = ySrc + (q / 2) * ySrcStride * bhHalf + (q % 2) * bwHalf;
        const uint8_t* pBase = yPre + (q / 2) * yPreStride * bhHalf + (q % 2) * bwHalf;
        uint32_t sum = 0;
        for (uint32_t i = 0; i < bhHalf; ++i) {
            for (uint32_t j = 0; j < bwHalf; ++j) {
                int diff = static_cast<int>(sBase[i * ySrcStride + j]) - static_cast<int>(pBase[i * yPreStride + j]);
                sum += static_cast<uint32_t>(diff * diff);
            }
        }
        windowErrorQuadFp8[q] = (((sum << 4) / bwHalf) << 4) / bhHalf;
    }

    if (isChroma) {
        for (int i = 0; i < 4; ++i) {
            windowErrorQuadFp8[i] = (windowErrorQuadFp8[i] * 5 + lumaWindowErrorQuadFp8[i]) / 6;
        }
    }

    for (unsigned subblockIdx = 0; subblockIdx < 4; ++subblockIdx) {
        uint32_t combinedErrorFp8 =
            (windowErrorQuadFp8[subblockIdx] * kBalance + blockErrorFp8[subblockIdx]) / (kBalance + 1);
        uint64_t avgErrFp10 =
            static_cast<uint64_t>(combinedErrorFp8 >> 3) * static_cast<uint64_t>(dFactorFp8[subblockIdx] >> 3);
        uint32_t divisor = tfDecayFactorFp16 >> 10;
        if (divisor < 1) {
            divisor = 1;
        }
        uint64_t scaled = avgErrFp10 / divisor;
        uint32_t scaledDiff16 = scaled > (7u * 16) ? (7u * 16) : static_cast<uint32_t>(scaled);
        uint32_t weight =
            static_cast<uint32_t>((static_cast<uint64_t>(expfTabFp16[scaledDiff16]) * kWeightScale) >> 16);
        int xOffset = static_cast<int>((subblockIdx % 2) * blockWidth / 2);
        int yOffset = static_cast<int>((subblockIdx / 2) * blockHeight / 2);
        for (unsigned i = 0; i < blockHeight / 2; ++i) {
            for (unsigned j = 0; j < blockWidth / 2; ++j) {
                const int k = (i + yOffset) * yPreStride + j + xOffset;
                yCount[k] = static_cast<uint16_t>(yCount[k] + weight);
                yAccum[k] += weight * yPre[k];
            }
        }
    }
}

}  // namespace

TEST_CASE("l9 temporal non-zz partial kernel luma split path matches oracle") {
    using analysis::temporal::applyTemporalFilterPlanewiseMediumPartial;
    NonZzFixture f;
    const int16_t mvX[4] = {3, 0, 6, 2};
    const int16_t mvY[4] = {0, 0, 0, 2};
    const uint64_t err16[4] = {210, 400, 790, 1560};
    uint32_t lumaWindow[4] = {0, 0, 0, 0};
    applyTemporalFilterPlanewiseMediumPartial(15, true, mvX, mvY, err16, 0, 0, 0, f.src, NonZzFixture::kStride,
                                              f.pre, NonZzFixture::kStride, NonZzFixture::kDim, NonZzFixture::kDim,
                                              f.accum, f.count, 1u << 16, lumaWindow, false);
    NonZzFixture o;
    uint32_t lumaWindowRef[4] = {0, 0, 0, 0};
    nonZzOracle(15, true, mvX, mvY, err16, 0, 0, 0, o.src, NonZzFixture::kStride, o.pre, NonZzFixture::kStride,
                NonZzFixture::kDim, NonZzFixture::kDim, o.accum, o.count, 1u << 16, lumaWindowRef, false);
    for (int i = 0; i < NonZzFixture::kStride * 12; ++i) {
        CHECK_EQ(f.count[i], o.count[i]);
        CHECK_EQ(f.accum[i], o.accum[i]);
    }
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(lumaWindow[i], lumaWindowRef[i]);
        // calibration: window errors are 16*(q+1) > 0 and pairwise
        // distinct, so the quadrant split really fired
        CHECK_EQ(lumaWindow[i], 16u * static_cast<uint32_t>(i + 1));
    }
    // calibration: the four subblock weights are nonzero and pairwise
    // distinct
    uint32_t w0 = f.count[0], w1 = f.count[4], w2 = f.count[4 * NonZzFixture::kStride],
             w3 = f.count[4 * NonZzFixture::kStride + 4];
    CHECK_NE(w0, 0u);
    CHECK_NE(w0, w1);
    CHECK_NE(w0, w2);
    CHECK_NE(w0, w3);
    CHECK_NE(w1, w2);
    CHECK_NE(w1, w3);
    CHECK_NE(w2, w3);
}

TEST_CASE("l9 temporal non-zz partial kernel non-split path and chroma blend") {
    using analysis::temporal::applyTemporalFilterPlanewiseMediumPartial;
    NonZzFixture f;
    const uint64_t err16[4] = {7, 7, 7, 7};  // ignored in the non-split path
    uint32_t lumaWindow[4] = {0, 0, 0, 0};
    // decay 1<<16 doubles to 1<<17 in the non-split branch (divisor 128).
    applyTemporalFilterPlanewiseMediumPartial(15, false, nullptr, nullptr, err16, 4, -3, 100, f.src,
                                              NonZzFixture::kStride, f.pre, NonZzFixture::kStride,
                                              NonZzFixture::kDim, NonZzFixture::kDim, f.accum, f.count, 1u << 16,
                                              lumaWindow, false);
    NonZzFixture o;
    uint32_t lumaWindowRef[4] = {0, 0, 0, 0};
    nonZzOracle(15, false, nullptr, nullptr, err16, 4, -3, 100, o.src, NonZzFixture::kStride, o.pre,
                NonZzFixture::kStride, NonZzFixture::kDim, NonZzFixture::kDim, o.accum, o.count, 1u << 16,
                lumaWindowRef, false);
    for (int i = 0; i < NonZzFixture::kStride * 12; ++i) {
        CHECK_EQ(f.count[i], o.count[i]);
        CHECK_EQ(f.accum[i], o.accum[i]);
    }
    // calibration: subblock weights nonzero and pairwise distinct
    uint32_t w0 = f.count[0], w1 = f.count[4], w2 = f.count[4 * NonZzFixture::kStride],
             w3 = f.count[4 * NonZzFixture::kStride + 4];
    CHECK_NE(w0, 0u);
    CHECK_NE(w0, w1);
    CHECK_NE(w0, w2);
    CHECK_NE(w0, w3);
    CHECK_NE(w1, w2);
    CHECK_NE(w1, w3);
    CHECK_NE(w2, w3);

    // Chroma call on a fresh fixture with a caller-supplied luma window
    // array (as the real two-call sequence produces): the blend
    // (window*5 + luma)/6 changes the effective error, so the chroma
    // weights differ from the luma call's - the blend path genuinely
    // fires. The <<1 decay doubling in :961 is per-call local (the
    // caller's decay value is untouched between calls).
    NonZzFixture c;
    uint32_t staleLumaWindow[4] = {900, 900, 900, 900};
    applyTemporalFilterPlanewiseMediumPartial(15, false, nullptr, nullptr, err16, 4, -3, 100, c.src,
                                              NonZzFixture::kStride, c.pre, NonZzFixture::kStride,
                                              NonZzFixture::kDim, NonZzFixture::kDim, c.accum, c.count, 1u << 16,
                                              staleLumaWindow, true);
    NonZzFixture co;
    nonZzOracle(15, false, nullptr, nullptr, err16, 4, -3, 100, co.src, NonZzFixture::kStride, co.pre,
                NonZzFixture::kStride, NonZzFixture::kDim, NonZzFixture::kDim, co.accum, co.count, 1u << 16,
                staleLumaWindow, true);
    for (int i = 0; i < NonZzFixture::kStride * 12; ++i) {
        CHECK_EQ(c.count[i], co.count[i]);
        CHECK_EQ(c.accum[i], co.accum[i]);
    }
    // Calibration: chroma weights differ from the luma call's.
    CHECK_NE(c.count[0], f.count[0]);
    CHECK_NE(c.count[4], f.count[4]);
}

// TF1 S14: svt_av1_apply_temporal_filter_planewise_medium_c
// (temporal_filtering.c:1039-1087). Threading (named): tf_mv_dist_th,
// the resolved idx_32x32 block-error/mv slices, tf_decay_factor_fp16[3]
// and tf_chroma threaded as parameters; the wrapper owns the shared
// luma window-error array internally (:1046).
TEST_CASE("l9 temporal non-zz medium wrapper luma and chroma planes") {
    using analysis::temporal::applyTemporalFilterPlanewiseMedium;
    NonZzFixture f;
    NonZzFixture u;
    NonZzFixture v;
    const int16_t mvX[4] = {3, 0, 6, 2};
    const int16_t mvY[4] = {0, 0, 0, 2};
    const uint64_t err16[4] = {210, 400, 790, 1560};
    const uint32_t decay[3] = {1u << 16, 1u << 17, 1u << 18};
    applyTemporalFilterPlanewiseMedium(15, true, mvX, mvY, err16, 0, 0, 0, f.src, NonZzFixture::kStride, f.pre,
                                       NonZzFixture::kStride, u.src, v.src, NonZzFixture::kStride, u.pre, v.pre,
                                       NonZzFixture::kStride, NonZzFixture::kDim, NonZzFixture::kDim, 1, 1, f.accum,
                                       f.count, u.accum, u.count, v.accum, v.count, decay, true);
    // Luma at 8x8, chroma at 4x4.
    NonZzFixture o;
    uint32_t lumaWindowRef[4] = {0, 0, 0, 0};
    nonZzOracle(15, true, mvX, mvY, err16, 0, 0, 0, o.src, NonZzFixture::kStride, o.pre, NonZzFixture::kStride,
                NonZzFixture::kDim, NonZzFixture::kDim, o.accum, o.count, decay[0], lumaWindowRef, false);
    for (int i = 0; i < NonZzFixture::kStride * 12; ++i) {
        CHECK_EQ(f.count[i], o.count[i]);
        CHECK_EQ(f.accum[i], o.accum[i]);
    }
    uint32_t uAccumRef[NonZzFixture::kStride * 12] = {};
    uint16_t uCountRef[NonZzFixture::kStride * 12] = {};
    uint32_t vAccumRef[NonZzFixture::kStride * 12] = {};
    uint16_t vCountRef[NonZzFixture::kStride * 12] = {};
    nonZzOracle(15, true, mvX, mvY, err16, 0, 0, 0, u.src, NonZzFixture::kStride, u.pre, NonZzFixture::kStride,
                NonZzFixture::kDim >> 1, NonZzFixture::kDim >> 1, uAccumRef, uCountRef, decay[1], lumaWindowRef,
                true);
    nonZzOracle(15, true, mvX, mvY, err16, 0, 0, 0, v.src, NonZzFixture::kStride, v.pre, NonZzFixture::kStride,
                NonZzFixture::kDim >> 1, NonZzFixture::kDim >> 1, vAccumRef, vCountRef, decay[2], lumaWindowRef,
                true);
    for (int i = 0; i < NonZzFixture::kStride * 12; ++i) {
        CHECK_EQ(u.count[i], uCountRef[i]);
        CHECK_EQ(u.accum[i], uAccumRef[i]);
        CHECK_EQ(v.count[i], vCountRef[i]);
        CHECK_EQ(v.accum[i], vAccumRef[i]);
    }
    // Calibration: chroma weights differ from luma (blend + decay differ).
    CHECK_NE(u.count[0], f.count[0]);
}

// TF1 fixture (e): the zz and non-zz variants on the SAME input - same
// pre buffer, same block errors, same decay - pinned to differ. The
// variants differ structurally: the zz kernel accumulates directly from
// the block errors with weight shift >> 17 (:767), the non-zz kernel
// blends the measured window error with the block error and shifts
// >> 16 (:1023), so identical inputs must NOT produce identical counts.
TEST_CASE("l9 temporal zz vs non-zz variants differ on the same input") {
    using analysis::temporal::applyZzBasedTemporalFilterPlanewiseMediumPartial;
    using analysis::temporal::applyTemporalFilterPlanewiseMediumPartial;
    NonZzFixture z;
    NonZzFixture n;
    const int16_t mvX[4] = {0, 0, 0, 0};
    const int16_t mvY[4] = {0, 0, 0, 0};
    const uint64_t err16[4] = {210, 400, 790, 1560};
    uint32_t lumaWindow[4] = {0, 0, 0, 0};
    applyZzBasedTemporalFilterPlanewiseMediumPartial(true, err16, 0, z.pre, NonZzFixture::kStride,
                                                     NonZzFixture::kDim, NonZzFixture::kDim, z.accum, z.count,
                                                     1u << 16);
    applyTemporalFilterPlanewiseMediumPartial(15, true, mvX, mvY, err16, 0, 0, 0, n.src, NonZzFixture::kStride,
                                              n.pre, NonZzFixture::kStride, NonZzFixture::kDim,
                                              NonZzFixture::kDim, n.accum, n.count, 1u << 16, lumaWindow, false);
    CHECK_NE(z.count[0], n.count[0]);
    CHECK_NE(z.count[4], n.count[4]);
    CHECK_NE(z.count[4 * NonZzFixture::kStride], n.count[4 * NonZzFixture::kStride]);
    CHECK_NE(z.count[4 * NonZzFixture::kStride + 4], n.count[4 * NonZzFixture::kStride + 4]);
}

// AM1 T1: the two-stage cascade of svt_vmaf_hpass_row_c
// (temporal_filtering.c:3720-3733). Each stage is the delay-line pair
// tmp2 = h_acc[s] + tmp1; h_acc[s] = tmp1; tmp1 = h_acc[s+1] + tmp2;
// h_acc[s+1] = tmp2, so stage-0 output o0[c] = u[c-2]+2u[c-1]+u[c] and
// stage-2 output p[c] = o0[c-2]+2o0[c-1]+o0[c]: the [1,4,6,4,1] kernel
// with a startup ramp. Constant u = c gives p = [c, 5c, 11c, 15c, 16c,
// 16c, ...] (c < 4 entries are the startup prefix, h_acc starts at 0).
TEST_CASE("l9 temporal vmafHpassRow constant row startup ramp") {
    const std::uint8_t src[4] = {10, 10, 10, 10};
    std::int16_t h[8];
    analysis::temporal::vmafHpassRow(src, 4, h);
    const std::int16_t expect[8] = {10, 50, 110, 150, 160, 160, 160, 160};
    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(h[i], expect[i]);
    }
}

// AM1 T2: a ramp row against an INDEPENDENT oracle: the closed form
// out[c] = [1,4,6,4,1] . u[c-4..c] (u = the edge-clamped sample sequence
// u[i] = src[clamp(i-2)]), written here as a direct convolution, not the
// reference's delay-line loop. Also pins the edge clamp: x <= 0 reads
// src[0], x >= width reads src[width-1] (temporal_filtering.c:3724).
namespace {
std::int16_t hpassOracle(const std::uint8_t* src, int width, int j) {
    const int taps[5] = {1, 4, 6, 4, 1};
    // full window exists only for j >= 4; before that the missing taps
    // contribute zero (h_acc starts cleared) - the startup prefix.
    int sum = 0;
    for (int k = 0; k < 5; ++k) {
        int idx = j - 4 + k;
        if (idx < 0) {
            continue;
        }
        int x = idx - 2;
        std::uint8_t s = x <= 0 ? src[0] : x >= width ? src[width - 1] : src[x];
        sum += taps[k] * s;
    }
    return static_cast<std::int16_t>(sum);
}
}  // namespace

TEST_CASE("l9 temporal vmafHpassRow ramp row matches the direct convolution oracle") {
    const int w = 9;
    std::uint8_t src[w] = {30, 40, 50, 60, 70, 80, 90, 100, 110};
    std::int16_t h[w + 4];
    analysis::temporal::vmafHpassRow(src, w, h);
    for (int j = 0; j < w + 4; ++j) {
        CHECK_EQ(h[j], hpassOracle(src, w, j));
    }
}

// AM1 T3: svt_vmaf_vpass_row_c (temporal_filtering.c:3660-3669): the
// vertical [1,4,6,4,1] over the five ring rows, offset j = x + blur_start
// with blur_start = 2*steps_x, output (v + 128) >> 8 - the >>8 divides the
// 16-weighted h-values by 16 (256/16). Hand-set distinct ring rows.
TEST_CASE("l9 temporal vmafVpassRow weights and blur_start offset") {
    std::int16_t r0[12], r1[12], r2[12], r3[12], r4[12];
    for (int i = 0; i < 12; ++i) {
        r0[i] = (std::int16_t)(100 + i);
        r1[i] = (std::int16_t)(200 + i);
        r2[i] = (std::int16_t)(300 + i);
        r3[i] = (std::int16_t)(400 + i);
        r4[i] = (std::int16_t)(500 + i);
    }
    std::uint8_t blur[4];
    analysis::temporal::vmafVpassRow(r0, r1, r2, r3, r4, blur, 4, 2);
    // blur_start = 2*steps_x = 4: blur[x] reads j = x+4.
    for (int x = 0; x < 4; ++x) {
        const int j = x + 4;
        const std::uint32_t v = (std::uint32_t)r0[j] + (std::uint32_t)r4[j] +
            4u * ((std::uint32_t)r1[j] + (std::uint32_t)r3[j]) + 6u * (std::uint32_t)r2[j];
        CHECK_EQ(blur[x], (std::uint8_t)((v + 128u) >> 8));
    }
}

// AM1 T4: vmaf_box_blur_frame (pic_analysis_process.c:1773-1799) on a 5x5
// plane against an INDEPENDENT oracle: the separable structure reproduces
// the 2D [1,4,6,4,1]-outer-[1,4,6,4,1] / 256 filter with EDGE REPLICATION
// on both axes - vertical via the k-steps_y/m+steps_y clamps, horizontal
// via hpass's x<=0/x>=width clamps. The hring ring rotation is checked
// implicitly: the vpass window for output row m must be rows
// {clamp(m-2), clamp(m-1), m, clamp(m+1), clamp(m+2)}.
namespace {
int blurOracle(const std::uint8_t* src, int stride, int width, int height, int x, int y) {
    const int taps[5] = {1, 4, 6, 4, 1};
    auto clampRow = [](int v, int h) { return v < 0 ? 0 : v >= h ? h - 1 : v; };
    int sum = 0;
    for (int dy = 0; dy < 5; ++dy) {
        int row = clampRow(y + dy - 2, height);
        const std::uint8_t* srow = src + (std::size_t)row * stride;
        for (int dx = 0; dx < 5; ++dx) {
            int col = clampRow(x + dx - 2, width);
            sum += taps[dy] * taps[dx] * srow[col];
        }
    }
    return (sum + 128) >> 8;
}
}  // namespace

TEST_CASE("l9 temporal vmafBoxBlurFrame 5x5 plane matches the 2D oracle") {
    const int w = 5, h = 5, stride = 7;
    std::uint8_t src[h * stride];
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            src[y * stride + x] = (std::uint8_t)(13 * x + 7 * y + 3);
        }
    }
    std::int16_t ring0[w + 4], ring1[w + 4], ring2[w + 4], ring3[w + 4], ring4[w + 4];
    std::int16_t* hring[5] = {ring0, ring1, ring2, ring3, ring4};
    std::uint8_t blur[w * h];
    analysis::temporal::vmafBoxBlurFrame(src, stride, blur, w, h, hring);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            CHECK_EQ(blur[y * w + x], (std::uint8_t)blurOracle(src, stride, w, h, x, y));
        }
    }
}

// AM1 T5: svt_vmaf_apply_unsharp_row_c (temporal_filtering.c:3650-3658):
// detail = src - blur clamped to [-max_delta, max_delta], result =
// src + (detail * amount) >> 15, dst clamped to [0, 255].
TEST_CASE("l9 temporal vmafApplyUnsharpRow clamp scale and result saturate") {
    // detail in range, amount = 32768 (1.0 in Q15): 100 + 10 = 110.
    {
        const std::uint8_t src[3] = {100, 100, 100};
        const std::uint8_t blur[3] = {90, 90, 90};
        std::uint8_t dst[3] = {0, 0, 0};
        analysis::temporal::vmafApplyUnsharpRow(src, blur, dst, 3, 32768, 25);
        CHECK_EQ(dst[0], 110);
    }
    // detail over the clip: 100 + 30 clipped to +10 -> 110.
    {
        const std::uint8_t src[3] = {100, 100, 100};
        const std::uint8_t blur[3] = {70, 70, 70};
        std::uint8_t dst[3] = {0, 0, 0};
        analysis::temporal::vmafApplyUnsharpRow(src, blur, dst, 3, 32768, 10);
        CHECK_EQ(dst[0], 110);
    }
    // negative detail below the clip and result saturating at 0:
    // src 5, blur 40 -> detail -35 -> clipped -10 -> 5 + (-10) = -5 -> 0.
    {
        const std::uint8_t src[3] = {5, 5, 5};
        const std::uint8_t blur[3] = {40, 40, 40};
        std::uint8_t dst[3] = {9, 9, 9};
        analysis::temporal::vmafApplyUnsharpRow(src, blur, dst, 3, 32768, 10);
        CHECK_EQ(dst[0], 0);
    }
    // result saturating at 255: src 250, blur 240, amount 65536 (2.0):
    // detail 10, 10*65536>>15 = 20, 250+20 = 270 -> 255.
    {
        const std::uint8_t src[3] = {250, 250, 250};
        const std::uint8_t blur[3] = {240, 240, 240};
        std::uint8_t dst[3] = {0, 0, 0};
        analysis::temporal::vmafApplyUnsharpRow(src, blur, dst, 3, 65536, 25);
        CHECK_EQ(dst[0], 255);
    }
    // amount scaling: amount = 16384 (0.5): 100 + (10*16384>>15) = 105.
    {
        const std::uint8_t src[3] = {100, 100, 100};
        const std::uint8_t blur[3] = {90, 90, 90};
        std::uint8_t dst[3] = {0, 0, 0};
        analysis::temporal::vmafApplyUnsharpRow(src, blur, dst, 3, 16384, 25);
        CHECK_EQ(dst[0], 105);
    }
}

// AM1 T6: vmaf_unsharp_apply_frame (pic_analysis_process.c:1815-1821):
// per-row dispatch - src/dst advance by STRIDE, blur advances by WIDTH
// (the compact blur plane). Distinct per-row details pin the mapping;
// amount 32768 adds detail once, max_delta 80 leaves 60/65/70 unclipped.
TEST_CASE("l9 temporal vmafUnsharpApplyFrame stride vs width row mapping") {
    const int w = 4, h = 3, stride = 9;
    std::uint8_t src[h * stride];
    std::uint8_t blur[w * h];
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            src[y * stride + x] = (std::uint8_t)(100 + 10 * y);
            blur[y * w + x] = (std::uint8_t)(40 + 5 * y);
        }
    }
    std::uint8_t dst[h * stride];
    for (int i = 0; i < h * stride; ++i) {
        dst[i] = 77;
    }
    analysis::temporal::vmafUnsharpApplyFrame(src, blur, dst, w, h, stride, 32768, 80);
    // detail per row = (100+10y) - (40+5y) = 60+5y; dst = src + detail.
    const std::uint8_t expectRow[3] = {160, 175, 190};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            CHECK_EQ(dst[y * stride + x], expectRow[y]);
        }
        // outside the row width, dst is untouched
        CHECK_EQ(dst[y * stride + w], 77);
    }
}

// AM1 T7: svt_vmaf_compute_avg_mad_c (temporal_filtering.c:3622-3648):
// per-8x8-block mean (sum >> 6), mad = sum |p - mean| over the block,
// total_activity / (block_count * 64); block_count == 0 -> 0.
TEST_CASE("l9 temporal vmafComputeAvgMad blocks and empty case") {
    // one 8x8 block, half 0 half 128: mean = (32*128) >> 6 = 64,
    // mad = 32*64 + 32*64 = 4096, 4096 / (1*64) = 64.
    std::uint8_t src[8 * 8];
    for (int i = 0; i < 64; ++i) {
        src[i] = i < 32 ? 0 : 128;
    }
    CHECK_EQ(analysis::temporal::vmafComputeAvgMad(src, 8, 8, 8), 64u);
    // two blocks side by side (16 wide, stride 16): block 0 half 0/128
    // mad 4096, block 1 constant 0 mad 0 -> 4096 / (2*64) = 32.
    std::uint8_t src2[8 * 16];
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 16; ++c) {
            src2[r * 16 + c] = c < 8 ? (r < 4 ? 0 : 128) : 0;
        }
    }
    CHECK_EQ(analysis::temporal::vmafComputeAvgMad(src2, 16, 8, 16), 32u);
    // narrower than one block: no blocks -> 0.
    std::uint8_t src3[4 * 8] = {0};
    CHECK_EQ(analysis::temporal::vmafComputeAvgMad(src3, 4, 8, 8), 0u);
}

// AM1 T8a: svt_vmaf_compute_gradient_coherence_c
// (temporal_filtering.c:3671-3702): a flat plane has zero gradients, so
// weight_sum <= 0 and the function returns 1.0f.
TEST_CASE("l9 temporal vmafComputeGradientCoherence flat plane returns 1") {
    std::uint8_t src[8 * 8];
    for (int i = 0; i < 64; ++i) {
        src[i] = 100;
    }
    CHECK_EQ(analysis::temporal::vmafComputeGradientCoherence(src, 8, 8, 8), doctest::Approx(1.0f));
}

// AM1 FLOAT TOLERANCE POLICY (stated once, the AN12 EPSILON precedent):
// 1e-4 ABSOLUTE for every float-valued assertion in the AM1 tests
// (coherence, gate, amounts), written as an explicit CHECK_LT band;
// exact CHECK_EQ everywhere else. The AM1 float graph carries double
// accumulators + a float sqrtf, so the only noise is the float cast.
namespace {
// T8b oracle: the reference's blockwise coherence replicated in pure
// double (std::sqrt, no float casts) - an independent transcription of
// temporal_filtering.c:3671-3702.
double gcohOracle(const std::uint8_t* src, int width, int height, int stride) {
    double weighted_coh = 0.0;
    double weight_sum = 0.0;
    for (int by = 1; by < height - 1; by += 16) {
        for (int bx = 1; bx < width - 1; bx += 16) {
            std::int64_t sum_xx = 0, sum_yy = 0, sum_xy = 0;
            int y_end = (by + 16 < height - 1) ? by + 16 : height - 1;
            int x_end = (bx + 16 < width - 1) ? bx + 16 : width - 1;
            for (int y = by; y < y_end; y++) {
                const std::uint8_t* row = src + y * stride;
                const std::uint8_t* up = src + (y - 1) * stride;
                const std::uint8_t* down = src + (y + 1) * stride;
                for (int x = bx; x < x_end; x++) {
                    std::int32_t gx = (std::int32_t)row[x + 1] - (std::int32_t)row[x - 1];
                    std::int32_t gy = (std::int32_t)down[x] - (std::int32_t)up[x];
                    sum_xx += (std::int64_t)gx * gx;
                    sum_yy += (std::int64_t)gy * gy;
                    sum_xy += (std::int64_t)gx * gy;
                }
            }
            double xx = (double)sum_xx;
            double yy = (double)sum_yy;
            double xy = (double)sum_xy;
            weighted_coh += std::sqrt((xx - yy) * (xx - yy) + 4.0 * xy * xy);
            weight_sum += xx + yy;
        }
    }
    if (weight_sum <= 0.0) {
        return 1.0;
    }
    return weighted_coh / weight_sum;
}
}  // namespace

TEST_CASE("l9 temporal vmafComputeGradientCoherence mixed gradient plane matches double oracle") {
    const int w = 18, h = 18;
    std::uint8_t src[w * h];
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            src[y * w + x] = (std::uint8_t)(2 * x + 3 * y + (x >= 9 ? 17 : 0) + (y >= 8 ? 40 : 0));
        }
    }
    const float got = analysis::temporal::vmafComputeGradientCoherence(src, w, h, w);
    const double o = gcohOracle(src, w, h, w);
    CHECK_LT(std::fabs(got - o), 1e-4);
    // and the fixture is not accidentally coherent (coherence = 1 means
    // the gradient field carries no mixing - the oracle disagrees here)
    CHECK_LT(got, 0.999);
}

// AM1 T9: svt_vmaf_count_detail_le_c (temporal_filtering.c:3704-3718):
// the count of |src - blur| <= thresh; blur advances by width. The <=
// boundary is pinned (detail == thresh counts).
TEST_CASE("l9 temporal vmafCountDetailLe counts details at and below the threshold") {
    const std::uint8_t src[4] = {100, 100, 100, 100};
    const std::uint8_t blur[4] = {100, 95, 88, 87};  // details 0, 5, 12, 13
    CHECK_EQ(analysis::temporal::vmafCountDetailLe(src, blur, 4, 1, 4, 12), 3u);
    // threshold 5: only details 0 and 5.
    CHECK_EQ(analysis::temporal::vmafCountDetailLe(src, blur, 4, 1, 4, 5), 2u);
    // two rows: the second row's src read via src_stride, blur via width.
    const std::uint8_t src2[8] = {1, 1, 1, 1, 200, 200, 200, 200};
    const std::uint8_t blur2[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    // row 0 details 1 (<=12) x4; row 1 details 200 -> 0 counted; thresh 12.
    CHECK_EQ(analysis::temporal::vmafCountDetailLe(src2, blur2, 4, 2, 4, 12), 4u);
}

// AM1 T10: svt_estimate_noise_fp16_c (temporal_filtering.c:3556-3585).
// Constants (temporal_filtering.h:19-21): EDGE_THRESHOLD 50,
// SQRT_PI_BY_2_FP16 82137, SMOOTH_THRESHOLD 16. Interior loop i,j in
// [1, dim-1): Sobel gx/gy, ga = |gx|+|gy| < 50 admits the pixel, then
// the 8-neighbour Laplacian; sum*82137/(6*num) fp16, num < 16 -> -65536.
TEST_CASE("l9 temporal vmafEstimateNoiseFp16 single delta hand value plus branches") {
    // (a) flat plane: every Laplacian 0, num 36 >= 16 -> 0.
    std::uint8_t flat[8 * 8];
    for (int i = 0; i < 64; ++i) {
        flat[i] = 100;
    }
    CHECK_EQ(analysis::temporal::vmafEstimateNoiseFp16(flat, 8, 8, 8), 0);
    // (b) 4x4 interior is 2x2 = 4 px < SMOOTH_THRESHOLD 16 -> -65536 even
    // with an admitted nonzero-Laplacian pixel (sum 200).
    std::uint8_t small[4 * 4];
    for (int i = 0; i < 16; ++i) {
        small[i] = 100;
    }
    small[1 * 4 + 1] = 150;
    CHECK_EQ(analysis::temporal::vmafEstimateNoiseFp16(small, 4, 4, 4), -65536);
    // (c) hand value: 6x6 all 100 with one delta at (2,2) = 110.
    // Interior 4x4; every pixel admitted EXCEPT (3,3) (ga 60 >= 50, so
    // num 15). |Laplacian| sum: center 40, the four edge-neighbours of
    // the delta 20 each, the four diagonal-neighbours 10 each,
    // everything else 0 -> 150. 150*82137 / (6*15) = 136895.
    std::uint8_t src[6 * 6];
    for (int i = 0; i < 36; ++i) {
        src[i] = 100;
    }
    src[2 * 6 + 2] = 110;
    CHECK_EQ(analysis::temporal::vmafEstimateNoiseFp16(src, 6, 6, 6), 136895);
    // (d) ga boundary pair, delta 24 at (2,2): the 8 hood pixels have
    // ga 48 < 50 (admitted), the center ga 0, and the 7 far interior
    // pixels ga 0 (zero Laplacian): num 16, sum = 96 + 4*48 + 4*24 = 384;
    // 384*82137 / (6*16) = 328548 (truncating).
    std::uint8_t d24[6 * 6];
    for (int i = 0; i < 36; ++i) {
        d24[i] = 100;
    }
    d24[2 * 6 + 2] = 124;
    CHECK_EQ(analysis::temporal::vmafEstimateNoiseFp16(d24, 6, 6, 6), 328548);
    // (e) delta 25: every hood pixel sits at ga exactly 50, the ga < 50
    // guard excludes all 8, admitted = center + 7 far = num 8 < 16 ->
    // -65536 (with <= the run would be num 16, sum 400, 342237).
    std::uint8_t d25[6 * 6];
    for (int i = 0; i < 36; ++i) {
        d25[i] = 100;
    }
    d25[2 * 6 + 2] = 125;
    CHECK_EQ(analysis::temporal::vmafEstimateNoiseFp16(d25, 6, 6, 6), -65536);
}
