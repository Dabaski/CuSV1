#include <doctest.h>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <cstdio>
#include "rate_est.h"

// The port's types at namespace scope (the l7 test-file convention).
using l16_rdo::AomCdfProb;
using l16_rdo::FcTokenCdfs;
using l16_rdo::FcSyntaxCdfs;
using l16_rdo::BLOCK_SIZES_ALL;

// ---------------------------------------------------------------------------
// Independent oracles (test-side transcriptions of the reference
// arithmetic, never shared with the port).
// ---------------------------------------------------------------------------

// get_msb, independent transcription (definitions.h:628-644 portable body).
static std::int32_t oracleGetMsb(std::uint32_t n) {
    std::int32_t log = 0;
    while (n >>= 1) ++log;
    return log;
}

// get_prob, independent transcription (cabac_context_model.h:68-75).
static std::uint8_t oracleGetProb(unsigned num, unsigned den) {
    const int p = (int)(((std::uint64_t)num * 256 + (den >> 1)) / den);
    const int clippedProb = p | ((255 - p) >> 23) | (p == 0);
    return (std::uint8_t)clippedProb;
}

// The av1_prob_cost table derived from its own documented formula
// (md_rate_estimation.h:129 comment: round(-log2(i/256.) * (1 << 9)),
// i = 128..255). Formula-vs-table: the 128 committed entries reproduce
// exactly (verified 2026-11-07 before writing this test).
static int oracleProbCost(std::int32_t j) {
    return (int)std::llround(-std::log2((j + 128) / 256.0) * (1 << 9));
}

// av1_cost_symbol, independent transcription (md_rate_estimation.c:35-45).
static std::int32_t oracleCostSymbol(std::int32_t p15) {
    if (p15 < 1) p15 = 1;
    if (p15 > CDF_PROB_TOP - 1) p15 = CDF_PROB_TOP - 1;
    const std::int32_t shift = CDF_PROB_BITS - 1 - oracleGetMsb(p15);
    const std::int32_t prob = oracleGetProb((unsigned)p15 << shift, CDF_PROB_TOP);
    return oracleProbCost(prob - 128) + (shift << 9);
}

// ---------------------------------------------------------------------------
// MV class plumbing (cabac_context_model.h:187-195 + 173-185), independent
// transcription: CLASS0_BITS 1, CLASS0_SIZE 2, MV_CLASSES 11, MV_MAX
// (1 << 14) - 1 = 16383; the CLASS_10 floor z >= 2 * 4096.
// ---------------------------------------------------------------------------
static l16_rdo::MvClassType oracleGetMvClass(std::int32_t z, std::int32_t* offset) {
    // mv_class_base (md_rate_estimation.c:366-368): c ? 2 << (c + 2) : 0.
    static const std::int32_t kClassBase[11] = {
        0, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192,
    };
    const l16_rdo::MvClassType c = (z >= 2 * 4096) ? l16_rdo::MvClass_10
        : (l16_rdo::MvClassType)oracleGetMsb((unsigned)(z >> 3) | 1);
    if (offset) *offset = z - kClassBase[(int)c];
    return c;
}

TEST_CASE("cwd probe") { std::printf("cwd=%s\n", std::filesystem::current_path().string().c_str()); }

TEST_CASE("l16_rdo costSymbol at p15=16384 equals 512") {
    CHECK(l16_rdo::costSymbol(16384) == 512);
}
TEST_CASE("av1_prob_cost table matches its documented formula over all 128 entries") {
    for (std::int32_t j = 0; j < 128; ++j) {
        CHECK(l16_rdo::av1ProbCost[j] == oracleProbCost(j));
    }
}

TEST_CASE("l16_rdo costSymbol matches the cost-symbol oracle over the enumerated p15 set") {
    // The full set the md-rate paths can reach: the probability-of-one
    // value spans [1, CDF_PROB_TOP - 1]; EC_MIN_PROB (4) and 6656 (the
    // partition-alike patch value) are called out by name.
    static const std::int32_t p15s[] = {
        1, 2, 3, 4, 5, 7, 8, 15, 16, 63, 64, 255, 256, 1023, 1024,
        4095, 4096, 8191, 8192, 16383, 16384, 24575, 24576, 32766, 32767,
    };
    for (std::int32_t p15 : p15s) {
        CHECK(l16_rdo::costSymbol((std::uint16_t)p15) == oracleCostSymbol(p15));
    }
    // The named pin: the partition-alike guard writes 6656 = cost of
    // EC_MIN_PROB (md_rate_estimation.c:106 comment; measured below).
    CHECK(l16_rdo::costSymbol(EC_MIN_PROB) == 6656);
}

// ---------------------------------------------------------------------------
// S2/S3: svt_aom_get_syntax_rate_from_cdf (md_rate_estimation.c:50-69).
// Oracle: independent loop transcription. Fixture: partition row 0 of the
// committed default table (AOM_CDF4(19132, 25510, 30392), l7_entropy/
// entropy.cpp "default_partition_cdf" row 0, mechanically copied here).
// ---------------------------------------------------------------------------
static void oracleRateFromCdf(std::int32_t* costs, const AomCdfProb* cdf, const std::int32_t* invMap) {
    std::int32_t prevCdf = 0;
    for (int i = 0;; ++i) {
        std::int32_t p15 = AOM_ICDF(cdf[i]) - prevCdf;
        p15 = (p15 < EC_MIN_PROB) ? EC_MIN_PROB : p15;
        prevCdf = AOM_ICDF(cdf[i]);
        if (invMap) {
            costs[invMap[i]] = oracleCostSymbol(p15);
        } else {
            costs[i] = oracleCostSymbol(p15);
        }
        if (cdf[i] == AOM_ICDF(CDF_PROB_TOP)) break;
    }
}

// ---------------------------------------------------------------------------
// S5: svt_av1_get_mv_class (md_rate_estimation.c:370-376) + mv_class_base
// (:366-368). Independent oracle: the same arithmetic transcribed here, with
// the constants from cabac_context_model.h:187-195 (CLASS0_BITS 1,
// CLASS0_SIZE 2, MV_CLASSES 11, MV_MAX (1 << 14) - 1 = 16383).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// S6: build_nmv_component_cost_table (md_rate_estimation.c:378-435).
// Independent oracle: the same per-component loop transcribed here on the
// test's own oracle rate/cost primitives. The fixture is a synthetic
// NmvComponent with deterministic icdf rows built from probability lists
// (valid rows: cdf[i] = 32768 - sum(p[0..i]), terminator 0).
// ---------------------------------------------------------------------------

// Build a valid stored-cdf row from per-symbol probabilities (the AV1
// icdf convention: cdf[i] = 32768 - cum_{i+1} for i < n-1, cdf[n-1] = 0).
static void buildCdfRow(AomCdfProb* cdf, const std::int32_t* probs, int n) {
    std::int32_t cum = 0;
    for (int i = 0; i < n - 1; ++i) {
        cum += probs[i];
        cdf[i] = (AomCdfProb)(CDF_PROB_TOP - cum);
    }
    cdf[n - 1] = 0;
}

// Deterministic test probabilities (non-uniform, every symbol nonzero).
static const std::int32_t kClassesProbs[MV_CLASSES] = {1200, 900, 700, 600, 500, 400, 350, 300, 250, 200, 150};
static const std::int32_t kSignProbs[2] = {15000, 17768};
static const std::int32_t kClass0Probs[CLASS0_SIZE] = {18000, 14768};
// class0_fp and fp use DIFFERENT distributions so the class-0 branch is
// observable (the first S6 mutation - the class-0 fp/fp swap - survived a
// uniform-probability fixture and was killed only after this divergence).
static const std::int32_t kClass0FpProbs[MV_FP_SIZE] = {600, 600, 600, 600};
static const std::int32_t kFpProbs[MV_FP_SIZE] = {12000, 9000, 7000, 4768};
static const std::int32_t kHpProbs[2] = {22000, 10768};
static const std::int32_t kBitsProbs[2] = {24000, 8768};

static void fillTestMvcomp(l16_rdo::NmvComponent* mvcomp) {
    buildCdfRow(mvcomp->sign_cdf, kSignProbs, 2);
    buildCdfRow(mvcomp->classes_cdf, kClassesProbs, MV_CLASSES);
    buildCdfRow(mvcomp->class0_cdf, kClass0Probs, CLASS0_SIZE);
    for (int i = 0; i < MV_OFFSET_BITS; ++i) buildCdfRow(mvcomp->bits_cdf[i], kBitsProbs, 2);
    for (int i = 0; i < CLASS0_SIZE; ++i) buildCdfRow(mvcomp->class0_fp_cdf[i], kClass0FpProbs, MV_FP_SIZE);
    buildCdfRow(mvcomp->fp_cdf, kFpProbs, MV_FP_SIZE);
    buildCdfRow(mvcomp->class0_hp_cdf, kHpProbs, 2);
    buildCdfRow(mvcomp->hp_cdf, kHpProbs, 2);
}

// The reference's per-component loop transcribed on the test oracles
// (md_rate_estimation.c:378-435; the sign branch always takes [0]/[1] via
// mvcost[v] / mvcost[-v]).
static void oracleBuildNmvComponentCost(std::int32_t* mvcost, const l16_rdo::NmvComponent* mvcomp,
                                        l16_rdo::MvSubpelPrecision precision) {
    std::int32_t sign_cost[2], class_cost[MV_CLASSES], class0_cost[CLASS0_SIZE];
    std::int32_t bits_cost[MV_OFFSET_BITS][2];
    std::int32_t class0_fp_cost[CLASS0_SIZE][MV_FP_SIZE], fp_cost[MV_FP_SIZE];
    std::int32_t class0_hp_cost[2], hp_cost[2];

    oracleRateFromCdf(sign_cost, mvcomp->sign_cdf, nullptr);
    oracleRateFromCdf(class_cost, mvcomp->classes_cdf, nullptr);
    oracleRateFromCdf(class0_cost, mvcomp->class0_cdf, nullptr);
    for (int i = 0; i < MV_OFFSET_BITS; ++i) oracleRateFromCdf(bits_cost[i], mvcomp->bits_cdf[i], nullptr);
    for (int i = 0; i < CLASS0_SIZE; ++i) oracleRateFromCdf(class0_fp_cost[i], mvcomp->class0_fp_cdf[i], nullptr);
    oracleRateFromCdf(fp_cost, mvcomp->fp_cdf, nullptr);
    if (precision > l16_rdo::MvSubpelLowPrecision) {
        oracleRateFromCdf(class0_hp_cost, mvcomp->class0_hp_cdf, nullptr);
        oracleRateFromCdf(hp_cost, mvcomp->hp_cdf, nullptr);
    }
    mvcost[0] = 0;
    for (int v = 1; v <= MV_MAX; ++v) {
        std::int32_t z, c, o, d, e, f, cost = 0;
        z = v - 1;
        c = (std::int32_t)oracleGetMvClass(z, &o);
        cost += class_cost[c];
        d = (o >> 3);
        f = (o >> 1) & 3;
        e = (o & 1);
        if (c == 0) {
            cost += class0_cost[d];
        } else {
            const std::int32_t b = c + 1 - 1;  // c + CLASS0_BITS - 1
            for (int i = 0; i < b; ++i) cost += bits_cost[i][((d >> i) & 1)];
        }
        if (precision > l16_rdo::MvSubpelNone) {
            if (c == 0) {
                cost += class0_fp_cost[d][f];
            } else {
                cost += fp_cost[f];
            }
            if (precision > l16_rdo::MvSubpelLowPrecision) {
                if (c == 0) {
                    cost += class0_hp_cost[e];
                } else {
                    cost += hp_cost[e];
                }
            }
        }
        mvcost[v]  = cost + sign_cost[0];
        mvcost[-v] = cost + sign_cost[1];
    }
}

TEST_CASE("l16_rdo getMvClass matches the class-base oracle over the enumerated class boundaries") {
    // Every class c = 0..10 enumerated at its own boundaries: the last
    // magnitude of class c - 1, the first of c, and the first + 1. Plus
    // the MV_CLASS_10 boundary (z >= 2 * 4096) and the range ends.
    static const std::int32_t zs[] = {
        0, 1, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129,
        255, 256, 257, 511, 512, 513, 1023, 1024, 1025, 2047, 2048, 2049,
        4095, 4096, 4097, 8191, 8192, 8193, 16382, 16383,
    };
    for (std::int32_t z : zs) {
        std::int32_t off = -123456, offO = -123456;
        const l16_rdo::MvClassType c = l16_rdo::getMvClass(z, &off);
        const l16_rdo::MvClassType cO = oracleGetMvClass(z, &offO);
        CHECK(c == cO);
        CHECK(off == offO);
        // mv_class_base cross-check: the class's base is the reference's
        // own formula, and every z in the class satisfies z - base in
        // [0, class range).
        CHECK(z - l16_rdo::mvClassBase(c) >= 0);
    }
    // The full range ends.
    CHECK(l16_rdo::getMvClass(16383, nullptr) == l16_rdo::MvClass_10);
    CHECK(l16_rdo::getMvClass(8191, nullptr) == l16_rdo::MvClass_9);
    CHECK(l16_rdo::getMvClass(8192, nullptr) == l16_rdo::MvClass_10);
}

TEST_CASE("l16_rdo buildNmvComponentCostTable matches the reference loop over the full magnitude range") {
    l16_rdo::NmvComponent mvcomp;
    fillTestMvcomp(&mvcomp);
    // The three precisions the cost tables can be built for (the reference
    // gates the fp and hp contributions on > none / > low).
    static const l16_rdo::MvSubpelPrecision precisions[] = {
        l16_rdo::MvSubpelNone, l16_rdo::MvSubpelLowPrecision, l16_rdo::MvSubpelHighPrecision,
    };
    for (auto precision : precisions) {
        std::int32_t port[2 * MV_MAX + 1];
        std::int32_t expect[2 * MV_MAX + 1];
        l16_rdo::buildNmvComponentCostTable(&port[MV_MAX], &mvcomp, precision);
        oracleBuildNmvComponentCost(&expect[MV_MAX], &mvcomp, precision);
        for (std::int32_t v = -MV_MAX; v <= MV_MAX; ++v) {
            CHECK(port[MV_MAX + v] == expect[MV_MAX + v]);
        }
    }
}

TEST_CASE("l16_rdo getSyntaxRateFromCdf inv_map branch writes costs[invMap[i]]") {
    // A 4-symbol row where the inv_map is a permutation: costs[invMap[i]] =
    // costSymbol(p15) for every i. The permutation makes the inv and the
    // non-inv writes differ, so the fixture is discriminating.
    static const AomCdfProb partRow0[4] = {13636, 7258, 2376, 0};
    static const std::int32_t invMap[4] = {2, 3, 0, 1};
    std::int32_t costs[4] = {7, 7, 7, 7};
    l16_rdo::getSyntaxRateFromCdf(costs, partRow0, invMap);
    std::int32_t expect[4] = {-1, -1, -1, -1};
    oracleRateFromCdf(expect, partRow0, nullptr);
    CHECK(costs[0] == expect[2]);
    CHECK(costs[3] == expect[1]);
    CHECK(costs[2] == expect[0]);
    CHECK(costs[1] == expect[3]);
}

TEST_CASE("l16_rdo getSyntaxRateFromCdf clamps zero-probability symbols to EC_MIN_PROB") {
    // A synthesized adapted-like row where symbol 2 has zero probability
    // (consecutive equal icdf values): p15 = 0 -> the reference clamps it
    // to EC_MIN_PROB = 4 (md_rate_estimation.c:55). The oracle treats it
    // the same way, so this fixture is a formula cross-check, and the
    // 6656 named pin (cost of EC_MIN_PROB) makes the clamp observable.
    static const AomCdfProb zeroProbRow[4] = {AOM_ICDF(8192), AOM_ICDF(8192), AOM_ICDF(16384), 0};
    std::int32_t costs[4] = {-1, -1, -1, -1};
    l16_rdo::getSyntaxRateFromCdf(costs, zeroProbRow, nullptr);
    std::int32_t expect[4];
    oracleRateFromCdf(expect, zeroProbRow, nullptr);
    for (int i = 0; i < 4; ++i) CHECK(costs[i] == expect[i]);
    // The clamp is observable: costs[1] == 6656 (cost of EC_MIN_PROB),
    // while the unclamped cost of probability 0 would be the cost of
    // CDF_PROB_TOP - the port must never produce it.
    CHECK(costs[1] == 6656);
}

TEST_CASE("l16_rdo getSyntaxRateFromCdf matches the oracle on the partition row-0 fixture") {
    // AOM_CDF4(19132, 25510, 30392) expanded: {32768-a0, 32768-a1,
    // 32768-a2, 0} = {13636, 7258, 2376, 0}.
    static const AomCdfProb partRow0[4] = {13636, 7258, 2376, 0};
    std::int32_t costs[4] = {-1, -1, -1, -1};
    l16_rdo::getSyntaxRateFromCdf(costs, partRow0, nullptr);
    std::int32_t expect[4];
    oracleRateFromCdf(expect, partRow0, nullptr);
    for (int i = 0; i < 4; ++i) CHECK(costs[i] == expect[i]);
    // p15(0) = 19132 -> msb 14 -> shift 0 -> prob 149 -> probCost[21] = 400.
    CHECK(costs[0] == 400);
}

// ---------------------------------------------------------------------------
// S7: svt_aom_estimate_coefficients_rate (md_rate_estimation.c:486-584).
// The minimal FcTokenCdfs carry (the reference consumes FRAME_CONTEXT token
// cdf families; our l7 EcFrameContext cannot be linked from l16 - the l7-l13
// duplication precedent - so the slice carries the SAME families with the
// SAME dims, cited to entropy.h:562-574 = cabac_context_model.h:261-291).
// ---------------------------------------------------------------------------
static const int kTxSizes = 4;        // TX_SIZES
static const int kPlaneTypes = 2;     // PLANE_TYPES
static const int kTxbSkipContexts = 13;
static const int kSigCoefContexts = 42;  // SIG_COEF_CONTEXTS_2D 26 + 1D 16
static const int kSigCoefContextsEob = 4;
static const int kEobCoefContexts = 9;
static const int kDcSignContexts = 3;
static const int kLevelContexts = 21;
static const int kCoeffBaseRange = 12;  // COEFF_BASE_RANGE = 4 * (4-1)
static const int kBrCdfSize = 4;        // BR_CDF_SIZE

// The token cdf families the reference's :486-584 reads live in the
// header's minimal carry (l16_rdo::FcTokenCdfs).

// Deterministic per-row prob list fill (every symbol nonzero, rows differ
// by seed; the total stays under 32768 - the largest family is 42 rows).
static void fillRow(AomCdfProb* cdf, int n, int seed) {
    std::int32_t probs[64];
    for (int i = 0; i < n - 1; ++i) probs[i] = 100 + ((seed * 7 + i * 13) % 400);
    buildCdfRow(cdf, probs, n);
}
static void fillFcTokenCdfs(FcTokenCdfs* fc) {
    int seed = 0;
    for (int p = 0; p < 2; ++p)
        for (int c = 0; c < 2; ++c) {
            fillRow(&fc->eobFlagCdf16[p][c][0], 5, seed++);
            fillRow(&fc->eobFlagCdf32[p][c][0], 6, seed++);
            fillRow(&fc->eobFlagCdf64[p][c][0], 7, seed++);
            fillRow(&fc->eobFlagCdf128[p][c][0], 8, seed++);
            fillRow(&fc->eobFlagCdf256[p][c][0], 9, seed++);
            fillRow(&fc->eobFlagCdf512[p][c][0], 10, seed++);
            fillRow(&fc->eobFlagCdf1024[p][c][0], 11, seed++);
        }
    for (int t = 0; t < 4; ++t) {
        for (int c = 0; c < 13; ++c) fillRow(&fc->txbSkipCdf[t][c][0], 2, seed++);
        for (int p = 0; p < 2; ++p) {
            for (int c = 0; c < 4; ++c) fillRow(&fc->coeffBaseEobCdf[t][p][c][0], 3, seed++);
            for (int c = 0; c < 42; ++c) fillRow(&fc->coeffBaseCdf[t][p][c][0], 4, seed++);
            for (int c = 0; c < 9; ++c) fillRow(&fc->eobExtraCdf[t][p][c][0], 2, seed++);
            for (int c = 0; c < 21; ++c) fillRow(&fc->coeffBrCdf[t][p][c][0], 4, seed++);
        }
    }
    for (int p = 0; p < 2; ++p)
        for (int c = 0; c < 3; ++c) fillRow(&fc->dcSignCdf[p][c][0], 2, seed++);
}

// The reference's own loops transcribed (md_rate_estimation.c:486-584):
// the eob_frac_bits walk, the coeff_fac_bits walk (txb_skip, base_eob,
// base + the [4..7] derivation, eob_extra, dc_sign, the br_rate lps chain).
static void oracleEstimateCoefficientsRate(l16_rdo::MdRateEstimationContext* me, const FcTokenCdfs* fc) {
    const int nplanes = 2;  // AOMMIN(3, PLANE_TYPES)
    for (int eob_multi_size = 0; eob_multi_size < 7; ++eob_multi_size) {
        for (int plane = 0; plane < nplanes; ++plane) {
            l16_rdo::LvMapEobCost* pcost = &me->eobFracBits[eob_multi_size][plane];
            for (int ctx = 0; ctx < 2; ++ctx) {
                const AomCdfProb* pcdf;
                switch (eob_multi_size) {
                case 0: pcdf = &fc->eobFlagCdf16[plane][ctx][0]; break;
                case 1: pcdf = &fc->eobFlagCdf32[plane][ctx][0]; break;
                case 2: pcdf = &fc->eobFlagCdf64[plane][ctx][0]; break;
                case 3: pcdf = &fc->eobFlagCdf128[plane][ctx][0]; break;
                case 4: pcdf = &fc->eobFlagCdf256[plane][ctx][0]; break;
                case 5: pcdf = &fc->eobFlagCdf512[plane][ctx][0]; break;
                case 6: pcdf = &fc->eobFlagCdf1024[plane][ctx][0]; break;
                default: pcdf = &fc->eobFlagCdf1024[plane][ctx][0]; break;
                }
                oracleRateFromCdf(pcost->eobCost[ctx], pcdf, nullptr);
            }
        }
    }
    for (int tx_size = 0; tx_size < 4; ++tx_size) {
        for (int plane = 0; plane < nplanes; ++plane) {
            l16_rdo::LvMapCoeffCost* pcost = &me->coeffFacBits[tx_size][plane];
            for (int ctx = 0; ctx < 13; ++ctx)
                oracleRateFromCdf(pcost->txbSkipCost[ctx], &fc->txbSkipCdf[tx_size][ctx][0], nullptr);
            for (int ctx = 0; ctx < 4; ++ctx)
                oracleRateFromCdf(pcost->baseEobCost[ctx], &fc->coeffBaseEobCdf[tx_size][plane][ctx][0], nullptr);
            for (int ctx = 0; ctx < 42; ++ctx)
                oracleRateFromCdf(pcost->baseCost[ctx], &fc->coeffBaseCdf[tx_size][plane][ctx][0], nullptr);
            for (int ctx = 0; ctx < 42; ++ctx) {
                pcost->baseCost[ctx][4] = 0;
                pcost->baseCost[ctx][5] = pcost->baseCost[ctx][1] + 512 - pcost->baseCost[ctx][0];
                pcost->baseCost[ctx][6] = pcost->baseCost[ctx][2] - pcost->baseCost[ctx][1];
                pcost->baseCost[ctx][7] = pcost->baseCost[ctx][3] - pcost->baseCost[ctx][2];
            }
            for (int ctx = 0; ctx < 9; ++ctx)
                oracleRateFromCdf(pcost->eobExtraCost[ctx], &fc->eobExtraCdf[tx_size][plane][ctx][0], nullptr);
            for (int ctx = 0; ctx < 3; ++ctx)
                oracleRateFromCdf(pcost->dcSignCost[ctx], &fc->dcSignCdf[plane][ctx][0], nullptr);
            for (int ctx = 0; ctx < 21; ++ctx) {
                std::int32_t brRate[4];
                std::int32_t prevCost = 0;
                int i, j;
                oracleRateFromCdf(brRate, &fc->coeffBrCdf[tx_size][plane][ctx][0], nullptr);
                for (i = 0; i < 12; i += 3) {
                    for (j = 0; j < 3; ++j) pcost->lpsCost[ctx][i + j] = prevCost + brRate[j];
                    prevCost += brRate[j];
                }
                pcost->lpsCost[ctx][i] = prevCost;
            }
            for (int ctx = 0; ctx < 21; ++ctx) {
                pcost->lpsCost[ctx][0 + 12 + 1] = pcost->lpsCost[ctx][0];
                for (int i = 1; i <= 12; ++i)
                    pcost->lpsCost[ctx][i + 12 + 1] = pcost->lpsCost[ctx][i] - pcost->lpsCost[ctx][i - 1];
            }
        }
    }
}

TEST_CASE("l16_rdo estimateCoefficientsRate fills the token cost arrays 1:1 with the reference's loops") {
    FcTokenCdfs fc;
    fillFcTokenCdfs(&fc);
    l16_rdo::MdRateEstimationContext me;
    l16_rdo::estimateCoefficientsRate(&me, &fc);
    l16_rdo::MdRateEstimationContext expect;
    oracleEstimateCoefficientsRate(&expect, &fc);
    // eob_frac_bits: 7 sizes x 2 planes x 2 ctx x 11 entries (rows 5..10
    // unused - the reference writes the full [2][11] array; get_syntax_rate
    // fills only the first n+1... no: the walk fills [0..n-1] symbols + the
    // terminator slot is not written. The reference's eob_cost[2][11] holds
    // the symbol costs [0..n-1]; entries [n..10] are NOT written by the
    // reference either (uninitialized in its caller's memset). Our fixture
    // zero-fills the struct first; both sides skip them.
    std::memset(&me, 0, sizeof(me));
    std::memset(&expect, 0, sizeof(expect));
    l16_rdo::estimateCoefficientsRate(&me, &fc);
    oracleEstimateCoefficientsRate(&expect, &fc);
    int nCheck = 0;
    for (int ms = 0; ms < 7; ++ms)
        for (int p = 0; p < 2; ++p)
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 5 + ms; ++i) {
                    CHECK(me.eobFracBits[ms][p].eobCost[c][i] == expect.eobFracBits[ms][p].eobCost[c][i]);
                    ++nCheck;
                }
    for (int t = 0; t < 4; ++t)
        for (int p = 0; p < 2; ++p) {
            const l16_rdo::LvMapCoeffCost& a = me.coeffFacBits[t][p];
            const l16_rdo::LvMapCoeffCost& b = expect.coeffFacBits[t][p];
            for (int c = 0; c < 13; ++c) for (int i = 0; i < 2; ++i) { CHECK(a.txbSkipCost[c][i] == b.txbSkipCost[c][i]); ++nCheck; }
            for (int c = 0; c < 4; ++c) for (int i = 0; i < 3; ++i) { CHECK(a.baseEobCost[c][i] == b.baseEobCost[c][i]); ++nCheck; }
            for (int c = 0; c < 42; ++c) for (int i = 0; i < 8; ++i) { CHECK(a.baseCost[c][i] == b.baseCost[c][i]); ++nCheck; }
            for (int c = 0; c < 9; ++c) for (int i = 0; i < 2; ++i) { CHECK(a.eobExtraCost[c][i] == b.eobExtraCost[c][i]); ++nCheck; }
            for (int c = 0; c < 3; ++c) for (int i = 0; i < 2; ++i) { CHECK(a.dcSignCost[c][i] == b.dcSignCost[c][i]); ++nCheck; }
            for (int c = 0; c < 21; ++c) for (int i = 0; i < 26; ++i) { CHECK(a.lpsCost[c][i] == b.lpsCost[c][i]); ++nCheck; }
        }
    // The [4..7] derivation pins (the reference's :539-543 formulas on one
    // concrete row - the full-enumeration check above covers them, these
    // three assert the exact derived values stay observable):
    CHECK(me.coeffFacBits[0][0].baseCost[5][5] ==
          me.coeffFacBits[0][0].baseCost[5][1] + 512 - me.coeffFacBits[0][0].baseCost[5][0]);
    CHECK(me.coeffFacBits[2][1].baseCost[7][4] == 0);
    // The lps delta row pin: lps[13] == lps[0].
    CHECK(me.coeffFacBits[1][0].lpsCost[3][13] == me.coeffFacBits[1][0].lpsCost[3][0]);
}

// ---------------------------------------------------------------------------
// S8: svt_aom_estimate_syntax_rate (md_rate_estimation.c:76-364, the
// is_i_slice path). The live-surface oracle over the pinned CONFIG's
// DCE'd set (EbConfigMacros.h:59/:77/:83 force the FI/palette/restoration
// flags to 0). The S9 gather helpers are transcribed from
// cabac_context_model.h:378-406 (cdf_element_prob :373-376).
// ---------------------------------------------------------------------------
static const int kPartNone = 0, kPartHorz = 1, kPartVert = 2, kPartSplit = 3;
static const int kPartHorzA = 4, kPartHorzB = 5, kPartVertA = 6, kPartVertB = 7;
static const int kPartHorz4 = 8, kPartVert4 = 9;

static AomCdfProb cdfElementProb(const AomCdfProb* cdf, int element) {
    return (element > 0 ? cdf[element - 1] : CDF_PROB_TOP) - cdf[element];
}
static void oracleGatherVertAlike(AomCdfProb* out, const AomCdfProb* in, int bsizeIs128) {
    std::int32_t p0 = CDF_PROB_TOP;
    p0 -= cdfElementProb(in, kPartVert);
    p0 -= cdfElementProb(in, kPartSplit);
    p0 -= cdfElementProb(in, kPartHorzA);
    p0 -= cdfElementProb(in, kPartVertA);
    p0 -= cdfElementProb(in, kPartVertB);
    if (!bsizeIs128) p0 -= cdfElementProb(in, kPartVert4);
    out[0] = (AomCdfProb)AOM_ICDF(p0);
    out[1] = (AomCdfProb)AOM_ICDF(CDF_PROB_TOP);
    out[2] = 0;
}
static void oracleGatherHorzAlike(AomCdfProb* out, const AomCdfProb* in, int bsizeIs128) {
    std::int32_t p0 = CDF_PROB_TOP;
    p0 -= cdfElementProb(in, kPartHorz);
    p0 -= cdfElementProb(in, kPartSplit);
    p0 -= cdfElementProb(in, kPartHorzA);
    p0 -= cdfElementProb(in, kPartHorzB);
    p0 -= cdfElementProb(in, kPartVertA);
    if (!bsizeIs128) p0 -= cdfElementProb(in, kPartHorz4);
    out[0] = (AomCdfProb)AOM_ICDF(p0);
    out[1] = (AomCdfProb)AOM_ICDF(CDF_PROB_TOP);
    out[2] = 0;
}

static void fillFcSyntaxCdfs(FcSyntaxCdfs* fc) {
    int seed = 1000;
    for (int i = 0; i < 20; ++i) fillRow(&fc->partition_cdf[i][0], 10, seed++);
    for (int i = 0; i < 3; ++i) fillRow(&fc->skip_mode_cdfs[i][0], 2, seed++);
    for (int i = 0; i < 3; ++i) fillRow(&fc->skip_cdfs[i][0], 2, seed++);
    for (int a = 0; a < 5; ++a)
        for (int b = 0; b < 5; ++b) fillRow(&fc->kf_y_cdf[a][b][0], 13, seed++);
    for (int i = 0; i < 4; ++i) fillRow(&fc->y_mode_cdf[i][0], 13, seed++);
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 13; ++j) fillRow(&fc->uv_mode_cdf[i][j][0], 14, seed++);
    for (int i = 0; i < 16; ++i) fillRow(&fc->switchable_interp_cdf[i][0], 3, seed++);
    fillRow(&fc->cfl_sign_cdf[0], 8, seed++);
    for (int i = 0; i < 6; ++i) fillRow(&fc->cfl_alpha_cdf[i][0], 16, seed++);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) fillRow(&fc->tx_size_cdf[i][j][0], 3, seed++);
    for (int i = 0; i < 15; ++i) fillRow(&fc->txfm_partition_cdf[i][0], 2, seed++);
    for (int s = 0; s < 3; ++s)
        for (int t = 0; t < 4; ++t)
            for (int m = 0; m < 13; ++m) fillRow(&fc->intra_ext_tx_cdf[s][t][m][0], 16, seed++);
    for (int s = 0; s < 4; ++s)
        for (int t = 0; t < 4; ++t) fillRow(&fc->inter_ext_tx_cdf[s][t][0], 16, seed++);
    for (int i = 0; i < 8; ++i) fillRow(&fc->angle_delta_cdf[i][0], 7, seed++);
    fillRow(&fc->intrabc_cdf[0], 2, seed++);
}

// The reference's estimate_syntax_rate loops transcribed on the oracle
// primitives (md_rate_estimation.c:92-268). The pinned build's CONFIG
// (EbConfigMacros.h:59/:77/:83) forces the FI/palette/restoration guards
// to 0 (md_rate_estimation.c:82-90), DCE-ing those fills; the inter block
// :270-363 is the named park (the port's signature drops is_i_slice).
static void oracleEstimateSyntaxRate(l16_rdo::MdRateEstimationContext* me, std::uint8_t pic_filter_intra_level,
                                     std::uint8_t allow_screen_content_tools, std::uint8_t enable_restoration,
                                     std::uint8_t allow_intrabc, const FcSyntaxCdfs* fc) {
    (void)pic_filter_intra_level; (void)allow_screen_content_tools; (void)enable_restoration;
    pic_filter_intra_level = 0;
    allow_screen_content_tools = 0;
    enable_restoration = 0;
    me->initialized = 1;
    AomCdfProb cdf[3];
    for (int i = 0; i < 20; ++i) {
        oracleRateFromCdf(me->partitionFacBits[i], fc->partition_cdf[i], nullptr);
        oracleGatherVertAlike(cdf, fc->partition_cdf[i], 0);
        oracleRateFromCdf(me->partitionVertAlikeFacBits[i], cdf, nullptr);
        if (cdf[0] == 0) me->partitionVertAlikeFacBits[i][1] = oracleCostSymbol(4);
        oracleGatherVertAlike(cdf, fc->partition_cdf[i], 1);
        oracleRateFromCdf(me->partitionVertAlike128FacBits[i], cdf, nullptr);
        if (cdf[0] == 0) me->partitionVertAlike128FacBits[i][1] = oracleCostSymbol(4);
        oracleGatherHorzAlike(cdf, fc->partition_cdf[i], 0);
        oracleRateFromCdf(me->partitionHorzAlikeFacBits[i], cdf, nullptr);
        if (cdf[0] == 0) me->partitionHorzAlikeFacBits[i][1] = oracleCostSymbol(4);
        oracleGatherHorzAlike(cdf, fc->partition_cdf[i], 1);
        oracleRateFromCdf(me->partitionHorzAlike128FacBits[i], cdf, nullptr);
        if (cdf[0] == 0) me->partitionHorzAlike128FacBits[i][1] = oracleCostSymbol(4);
    }
    for (int i = 0; i < 3; ++i) {
        oracleRateFromCdf(me->skipModeFacBits[i], fc->skip_mode_cdfs[i], nullptr);
        oracleRateFromCdf(me->skipFacBits[i], fc->skip_cdfs[i], nullptr);
    }
    for (int a = 0; a < 5; ++a) {
        for (int b = 0; b < 5; ++b) {
            oracleRateFromCdf(me->yModeFacBits[a][b], fc->kf_y_cdf[a][b], nullptr);
        }
    }
    for (int i = 0; i < 4; ++i) {
        oracleRateFromCdf(me->mbModeFacBits[i], fc->y_mode_cdf[i], nullptr);
    }
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 13; ++j) {
            oracleRateFromCdf(me->intraUvModeFacBits[i][j], fc->uv_mode_cdf[i][j], nullptr);
        }
    }
    for (int i = 0; i < 16; ++i) {
        oracleRateFromCdf(me->switchableInterpFacBits[i], fc->switchable_interp_cdf[i], nullptr);
    }
    std::int32_t signFacBits[8];
    oracleRateFromCdf(signFacBits, fc->cfl_sign_cdf, nullptr);
    for (int js = 0; js < 8; ++js) {
        std::int32_t* facBitsU = me->cflAlphaFacBits[js][0];
        std::int32_t* facBitsV = me->cflAlphaFacBits[js][1];
        if (CFL_SIGN_U(js) == 0) {
            memset(facBitsU, 0, 16 * sizeof(*facBitsU));
        } else {
            oracleRateFromCdf(facBitsU, fc->cfl_alpha_cdf[CFL_CONTEXT_U(js)], nullptr);
        }
        if (CFL_SIGN_V(js) == 0) {
            memset(facBitsV, 0, 16 * sizeof(*facBitsV));
        } else {
            oracleRateFromCdf(facBitsV, fc->cfl_alpha_cdf[CFL_CONTEXT_V(js)], nullptr);
        }
        for (int u = 0; u < 16; ++u) facBitsU[u] += signFacBits[js];
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            oracleRateFromCdf(me->txSizeFacBits[i][j], fc->tx_size_cdf[i][j], nullptr);
        }
    }
    for (int i = 0; i < 15; ++i) {
        oracleRateFromCdf(me->txfmPartitionFacBits[i], fc->txfm_partition_cdf[i], nullptr);
    }
    for (int i = 0; i < 4; ++i) {
        for (int s = 1; s < 4; ++s) {
            if (l16_rdo::useInterExtTxForTxsize[s][i]) {
                oracleRateFromCdf(me->interTxTypeFacBits[s][i], fc->inter_ext_tx_cdf[s][i],
                                  l16_rdo::av1ExtTxInv[l16_rdo::av1ExtTxSetIdxToType[1][s]]);
            }
        }
        for (int s = 1; s < 3; ++s) {
            if (l16_rdo::useIntraExtTxForTxsize[s][i]) {
                for (int j = 0; j < 13; ++j) {
                    oracleRateFromCdf(me->intraTxTypeFacBits[s][i][j], fc->intra_ext_tx_cdf[s][i][j],
                                      l16_rdo::av1ExtTxInv[l16_rdo::av1ExtTxSetIdxToType[0][s]]);
                }
            }
        }
    }
    for (int i = 0; i < 8; ++i) {
        oracleRateFromCdf(me->angleDeltaFacBits[i], fc->angle_delta_cdf[i], nullptr);
    }
    if (allow_intrabc) {
        oracleRateFromCdf(me->intrabcFacBits, fc->intrabc_cdf, nullptr);
    }
}

TEST_CASE("l16_rdo the ext-tx + geometry tables match the reference verbatim") {
    static const std::int32_t expectInter[4][4] = {{1, 1, 1, 1}, {1, 1, 0, 0}, {0, 0, 1, 0}, {0, 1, 1, 1}};
    static const std::int32_t expectIntra[3][4] = {{1, 1, 1, 1}, {1, 1, 0, 0}, {0, 0, 1, 0}};
    static const l16_rdo::TxSetType expectIdx[2][4] = {
        {l16_rdo::EXT_TX_SET_DCTONLY, l16_rdo::EXT_TX_SET_DTT4_IDTX_1DDCT, l16_rdo::EXT_TX_SET_DTT4_IDTX},
        {l16_rdo::EXT_TX_SET_DCTONLY, l16_rdo::EXT_TX_SET_ALL16, l16_rdo::EXT_TX_SET_DTT9_IDTX_1DDCT,
         l16_rdo::EXT_TX_SET_DCT_IDTX}};
    static const std::int32_t expectInv[6][16] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {9, 0, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {9, 0, 10, 11, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {9, 10, 11, 0, 1, 2, 4, 5, 3, 6, 7, 8, 0, 0, 0, 0},
        {9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 4, 5, 3, 6, 7, 8}};
    for (int s = 0; s < 4; ++s)
        for (int i = 0; i < 4; ++i) CHECK(l16_rdo::useInterExtTxForTxsize[s][i] == expectInter[s][i]);
    for (int s = 0; s < 3; ++s)
        for (int i = 0; i < 4; ++i) CHECK(l16_rdo::useIntraExtTxForTxsize[s][i] == expectIntra[s][i]);
    for (int a = 0; a < 2; ++a)
        for (int s = 0; s < 4; ++s) CHECK(l16_rdo::av1ExtTxSetIdxToType[a][s] == expectIdx[a][s]);
    for (int t = 0; t < 6; ++t)
        for (int i = 0; i < 16; ++i) CHECK(l16_rdo::av1ExtTxInv[t][i] == expectInv[t][i]);
    static const std::uint8_t expectWide[BLOCK_SIZES_ALL] = {
        4, 4, 8, 8, 8, 16, 16, 16, 32, 32, 32, 64, 64, 64, 128, 128, 4, 16, 8, 32, 16, 64};
    static const std::uint8_t expectHigh[BLOCK_SIZES_ALL] = {
        4, 8, 4, 8, 16, 8, 16, 32, 16, 32, 64, 32, 64, 128, 64, 128, 16, 4, 32, 8, 64, 16};
    for (int bs = 0; bs < BLOCK_SIZES_ALL; ++bs) {
        CHECK(l16_rdo::blockSizeWide[bs] == expectWide[bs]);
        CHECK(l16_rdo::blockSizeHigh[bs] == expectHigh[bs]);
    }
    static const std::uint8_t expectPels[BLOCK_SIZES_ALL] = {
        4, 5, 5, 6, 7, 7, 8, 9, 9, 10, 11, 11, 12, 13, 13, 14, 6, 6, 8, 8, 10, 10};
    for (int bs = 0; bs < BLOCK_SIZES_ALL; ++bs) CHECK(l16_rdo::ebNumPelsLog2Lookup[bs] == expectPels[bs]);
}

TEST_CASE("l16_rdo estimateSyntaxRate matches the reference's loop oracle over the live intra surface") {
    FcSyntaxCdfs fc;
    fillFcSyntaxCdfs(&fc);
    l16_rdo::MdRateEstimationContext me, ome;
    memset(&me, 0xA5, sizeof(me));
    memset(&ome, 0xA5, sizeof(ome));
    l16_rdo::estimateSyntaxRate(&me, 0, 0, 0, 1, &fc);
    oracleEstimateSyntaxRate(&ome, 0, 0, 0, 1, &fc);
    CHECK(me.initialized == true);
    for (int i = 0; i < 20; ++i) {
        for (int j = 0; j < 10; ++j) CHECK(me.partitionFacBits[i][j] == ome.partitionFacBits[i][j]);
        for (int j = 0; j < 2; ++j) {
            CHECK(me.partitionVertAlikeFacBits[i][j] == ome.partitionVertAlikeFacBits[i][j]);
            CHECK(me.partitionHorzAlikeFacBits[i][j] == ome.partitionHorzAlikeFacBits[i][j]);
            CHECK(me.partitionVertAlike128FacBits[i][j] == ome.partitionVertAlike128FacBits[i][j]);
            CHECK(me.partitionHorzAlike128FacBits[i][j] == ome.partitionHorzAlike128FacBits[i][j]);
        }
    }
    for (int i = 0; i < 3; ++i) {
        CHECK(me.skipModeFacBits[i][0] == ome.skipModeFacBits[i][0]);
        CHECK(me.skipModeFacBits[i][1] == ome.skipModeFacBits[i][1]);
        CHECK(me.skipFacBits[i][0] == ome.skipFacBits[i][0]);
        CHECK(me.skipFacBits[i][1] == ome.skipFacBits[i][1]);
    }
    for (int a = 0; a < 5; ++a)
        for (int b = 0; b < 5; ++b)
            for (int j = 0; j < 13; ++j) CHECK(me.yModeFacBits[a][b][j] == ome.yModeFacBits[a][b][j]);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 13; ++j) CHECK(me.mbModeFacBits[i][j] == ome.mbModeFacBits[i][j]);
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 13; ++j)
            for (int k = 0; k < 14; ++k) CHECK(me.intraUvModeFacBits[i][j][k] == ome.intraUvModeFacBits[i][j][k]);
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 3; ++j) CHECK(me.switchableInterpFacBits[i][j] == ome.switchableInterpFacBits[i][j]);
    for (int js = 0; js < 8; ++js)
        for (int p = 0; p < 2; ++p)
            for (int u = 0; u < 16; ++u) CHECK(me.cflAlphaFacBits[js][p][u] == ome.cflAlphaFacBits[js][p][u]);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) CHECK(me.txSizeFacBits[i][j][k] == ome.txSizeFacBits[i][j][k]);
    for (int i = 0; i < 15; ++i) {
        CHECK(me.txfmPartitionFacBits[i][0] == ome.txfmPartitionFacBits[i][0]);
        CHECK(me.txfmPartitionFacBits[i][1] == ome.txfmPartitionFacBits[i][1]);
    }
    for (int s = 1; s < 4; ++s)
        for (int i = 0; i < 4; ++i)
            for (int t = 0; t < 16; ++t) CHECK(me.interTxTypeFacBits[s][i][t] == ome.interTxTypeFacBits[s][i][t]);
    for (int s = 1; s < 3; ++s)
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 13; ++j)
                for (int t = 0; t < 16; ++t)
                    CHECK(me.intraTxTypeFacBits[s][i][j][t] == ome.intraTxTypeFacBits[s][i][j][t]);
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 7; ++j) CHECK(me.angleDeltaFacBits[i][j] == ome.angleDeltaFacBits[i][j]);
    CHECK(me.intrabcFacBits[0] == ome.intrabcFacBits[0]);
    CHECK(me.intrabcFacBits[1] == ome.intrabcFacBits[1]);
}

TEST_CASE("l16_rdo the interintra-allowed helpers over the enumerated bsize/mode/ref grids") {
    // mode_decision.h:142-147: enable=1 -> [BLOCK_8X8=3, BLOCK_32X32=9].
    static const std::int32_t expectBsize[BLOCK_SIZES_ALL] = {
        0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int bs = 0; bs < BLOCK_SIZES_ALL; ++bs)
        CHECK(l16_rdo::isInterIntraAllowedBsize(1, (l16_rdo::BlockSize)bs) == expectBsize[bs]);
    for (int bs = 0; bs < BLOCK_SIZES_ALL; ++bs)
        CHECK(l16_rdo::isInterIntraAllowedBsize(0, (l16_rdo::BlockSize)bs) == 0);
    // mode_decision.h:149-151: [NEARESTMV, NEAREST_NEARESTMV).
    for (int m = 0; m < l16_rdo::MB_MODE_COUNT; ++m) {
        const std::int32_t want = (m >= 13) && (m < 17);
        CHECK(l16_rdo::isInterIntraAllowedMode((l16_rdo::PredictionMode)m) == want);
    }
    // mode_decision.h:153-155: rf[0] > INTRA_FRAME && rf[1] <= INTRA_FRAME.
    for (int r0 = 0; r0 < 4; ++r0)
        for (int r1 = 0; r1 < 4; ++r1) {
            const std::int32_t rf[2] = {r0, r1};
            const std::int32_t want = (r0 > 0) && (r1 <= 0);
            CHECK(l16_rdo::isInterIntraAllowedRef(rf) == want);
        }
}
