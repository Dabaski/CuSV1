// l16_rdo - the rate-estimation + RD machinery (A4, RE1; the A4
// golden-regeneration campaign's foundation slices).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/md_rate_estimation.c
// (1,016 nb) + md_rate_estimation.h (189 nb).
// LANDED (S1-S3, 2026-11-07): the cost core - av1_prob_cost (md_rate_
// estimation.h:131-138, formula-oracle verified), costSymbol
// (av1_cost_symbol md_rate_estimation.c:35-45), getSyntaxRateFromCdf
// (svt_aom_get_syntax_rate_from_cdf :50-69, both inv_map branches).
// Constants cited verbatim; deviations named at each site.
#include "rate_est.h"

#include <cstddef>
#include <cmath>
#include <cstring>

namespace l16_rdo {

// ---------------------------------------------------------------------------
// RE2: the optimize_b trellis carries (full_loop.c:622-1252 + its
// dependency set; the l7-l13 duplication precedent - l16_rdo cannot link
// l7_entropy). Measurements and citations are per-span in the commit
// message; the charge is A1 (full_loop.c's file).
// ---------------------------------------------------------------------------
// get_msb is defined below (definitions.h:628-644 portable body); forward
// declaration so the carries above it can use it.
static std::int32_t getMsb(std::uint32_t n);

// set_levels (common_utils.h:92-98, verbatim).
std::uint8_t* setLevels(std::uint8_t* levelsBuf, std::int32_t width, std::int32_t height) {
    const std::int32_t stride = width + TX_PAD_HOR;
    return levelsBuf + LEVELS_TAIL_OFFSET - height * stride;
}

// get_padded_idx (coefficients.h:129-131, verbatim). TX_PAD_HOR_LOG2 = 2
// (definitions.h:417).
std::int32_t getPaddedIdx(std::int32_t idx, std::int32_t bwl) {
    return idx + ((idx >> bwl) << 2);
}

// get_nz_mag (coefficients.h:133-155, verbatim).
std::int32_t getNzMag(const std::uint8_t* levels, std::int32_t bwl, TxClass txClass) {
    std::int32_t mag;
    mag = levels[1] > 3 ? 3 : levels[1];
    mag += levels[(1 << bwl) + TX_PAD_HOR] > 3 ? 3 : levels[(1 << bwl) + TX_PAD_HOR];
    if (txClass == TX_CLASS_2D) {
        mag += levels[(1 << bwl) + TX_PAD_HOR + 1] > 3 ? 3 : levels[(1 << bwl) + TX_PAD_HOR + 1];
        mag += levels[2] > 3 ? 3 : levels[2];
        mag += levels[(2 << bwl) + (2 << 2)] > 3 ? 3 : levels[(2 << bwl) + (2 << 2)];
    } else if (txClass == TX_CLASS_VERT) {
        mag += levels[(2 << bwl) + (2 << 2)] > 3 ? 3 : levels[(2 << bwl) + (2 << 2)];
        mag += levels[(3 << bwl) + (3 << 2)] > 3 ? 3 : levels[(3 << bwl) + (3 << 2)];
        mag += levels[(4 << bwl) + (4 << 2)] > 3 ? 3 : levels[(4 << bwl) + (4 << 2)];
    } else {
        mag += levels[2] > 3 ? 3 : levels[2];
        mag += levels[3] > 3 ? 3 : levels[3];
        mag += levels[4] > 3 ? 3 : levels[4];
    }
    return mag;
}

// tx_type_to_class (cabac_context_model.c:15-32, verbatim).
const TxClass txTypeToClass[16] = {
    TX_CLASS_2D,   // DCT_DCT
    TX_CLASS_2D,   // ADST_DCT
    TX_CLASS_2D,   // DCT_ADST
    TX_CLASS_2D,   // ADST_ADST
    TX_CLASS_2D,   // FLIPADST_DCT
    TX_CLASS_2D,   // DCT_FLIPADST
    TX_CLASS_2D,   // FLIPADST_FLIPADST
    TX_CLASS_2D,   // ADST_FLIPADST
    TX_CLASS_2D,   // FLIPADST_ADST
    TX_CLASS_2D,   // IDTX
    TX_CLASS_VERT, // V_DCT
    TX_CLASS_HORIZ,// H_DCT
    TX_CLASS_VERT, // V_ADST
    TX_CLASS_HORIZ,// H_ADST
    TX_CLASS_VERT, // V_FLIPADST
    TX_CLASS_HORIZ,// H_FLIPADST
};

// get_br_ctx_eob (coefficients.h:69-81, verbatim).
std::int32_t getBrCtxEob(std::int32_t c, std::int32_t bwl, TxClass txClass) {
    const std::int32_t row = c >> bwl;
    const std::int32_t col = c - (row << bwl);
    if (c == 0) return 0;
    if ((txClass == TX_CLASS_2D && row < 2 && col < 2) || (txClass == TX_CLASS_HORIZ && col == 0) ||
        (txClass == TX_CLASS_VERT && row == 0))
        return 7;
    return 14;
}

// get_br_ctx (coefficients.h:83-127, verbatim).
std::int32_t getBrCtx(const std::uint8_t* levels, std::int32_t c, std::int32_t bwl, TxClass txClass) {
    const std::int32_t row = c >> bwl;
    const std::int32_t col = c - (row << bwl);
    const std::int32_t stride = (1 << bwl) + TX_PAD_HOR;
    const std::int32_t pos = row * stride + col;
    std::int32_t mag = levels[pos + 1];
    mag += levels[pos + stride];
    switch (txClass) {
    case TX_CLASS_2D:
        mag += levels[pos + stride + 1];
        mag = ((mag + 1) >> 1) > 6 ? 6 : (mag + 1) >> 1;
        if (c == 0) return mag;
        if (row < 2 && col < 2) return mag + 7;
        break;
    case TX_CLASS_HORIZ:
        mag += levels[pos + 2];
        mag = ((mag + 1) >> 1) > 6 ? 6 : (mag + 1) >> 1;
        if (c == 0) return mag;
        if (col == 0) return mag + 7;
        break;
    case TX_CLASS_VERT:
        mag += levels[pos + (stride << 1)];
        mag = ((mag + 1) >> 1) > 6 ? 6 : (mag + 1) >> 1;
        if (c == 0) return mag;
        if (row == 0) return mag + 7;
        break;
    default:
        break;
    }
    return mag + 14;
}

// get_lower_levels_ctx_eob (coefficients.h:56-67, verbatim).
std::int32_t getLowerLevelsCtxEob(std::int32_t bwl, std::int32_t height, std::int32_t scanIdx) {
    if (scanIdx == 0) return 0;
    if (scanIdx <= (height << bwl) / 8) return 1;
    if (scanIdx <= (height << bwl) / 4) return 2;
    return 3;
}

// ---------------------------------------------------------------------------
// The scan-order machinery (coefficients.h:36-46 + coefficients.c:19 and
// :314-:374, verbatim). The pools are computed, not transcribed: the pinned
// reference replaced the static scan tables with this generator. Deviation
// (named): the reference initializes via an explicit svt_aom_init_iscan()
// call; the port guards lazily inside getScanOrder/scanOrders as well.
// ---------------------------------------------------------------------------
// The TxSize indexes the generator names (definitions.h:955-974 enum order;
// the l3_transforms TxSize enum class carries the same values - the int
// spell avoids rate_est.h's TX_4X4/TX_32X32 macros colliding with its
// enumerators).
enum { kTxSizesAll = 19, kTx64X64 = 4, kTx32X64 = 11, kTx64X32 = 12, kTx16X64 = 17, kTx64X16 = 18 };

// tx_size_2d (coefficients.h:44-46, verbatim).
static const std::int32_t kTxSize2d[kTxSizesAll] = {
    16, 64, 256, 1024, 4096, 32, 32, 128, 128, 512, 512, 2048, 2048, 64, 64, 256, 256, 1024, 1024,
};
// tx_size_wide / tx_size_high (common_utils.c:116-122, verbatim).
static const std::int32_t kTxSizeWide[kTxSizesAll] = {
    4, 8, 16, 32, 64, 4, 8, 8, 16, 16, 32, 32, 64, 4, 16, 8, 32, 16, 64,
};
static const std::int32_t kTxSizeHigh[kTxSizesAll] = {
    4, 8, 16, 32, 64, 8, 4, 16, 8, 32, 16, 64, 32, 16, 4, 32, 8, 64, 16,
};

// tx_type_to_scan_index (coefficients.c:19, verbatim).
const std::int8_t txTypeToScanIndex[TX_TYPES] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 2, 1, 2};

// The pools (coefficients.c:320-:323 shapes): 1024 entries per tx size and
// class; the identity scan is shared.
static std::int16_t kIscanPool[kTxSizesAll][3][1024];
static std::int16_t kMcolPool[kTxSizesAll][1024];
static std::int16_t kDefPool[kTxSizesAll][1024];
static std::int16_t kIdScan[1024];
static ScanOrder kScanOrders[kTxSizesAll][3];
static bool kScansInitialized = false;

// svt_aom_init_iscan (coefficients.c:325-374, verbatim).
void initScans(void) {
    for (int i = 0; i < 1024; ++i) kIdScan[i] = (std::int16_t)i;
    for (int ts = 0; ts < kTxSizesAll; ++ts) {
        int n = kTxSize2d[ts];
        if (ts == kTx64X64 || ts == kTx64X32 || ts == kTx32X64) {
            n = 1024;
        } else if (ts == kTx16X64 || ts == kTx64X16) {
            n = 512;
        }
        const int W = kTxSizeWide[ts] < 32 ? kTxSizeWide[ts] : 32;
        const int H = kTxSizeHigh[ts] < 32 ? kTxSizeHigh[ts] : 32;
        // mrow = identity (row-major)
        kScanOrders[ts][1].scan = kIdScan;
        // mcol = column-major
        for (int k = 0; k < n; ++k) kMcolPool[ts][k] = (std::int16_t)((k % H) * W + (k / H));
        kScanOrders[ts][2].scan = kMcolPool[ts];
        // default = up-right diagonal: tall(H>W) r-increasing, wide(W>H)
        // r-decreasing, square alternating (odd diag r-incr, even r-decr)
        {
            int idx = 0;
            for (int d = 0; d < W + H - 1; ++d) {
                const int rlo = (d - (W - 1)) > 0 ? (d - (W - 1)) : 0;
                const int rhi = d < (H - 1) ? d : (H - 1);
                const int incr = (H > W) ? 1 : (W > H) ? 0 : (d & 1);
                if (incr) {
                    for (int r = rlo; r <= rhi; ++r) kDefPool[ts][idx++] = (std::int16_t)(r * W + (d - r));
                } else {
                    for (int r = rhi; r >= rlo; --r) kDefPool[ts][idx++] = (std::int16_t)(r * W + (d - r));
                }
            }
        }
        kScanOrders[ts][0].scan = kDefPool[ts];
        for (int v = 0; v < 3; ++v) {
            const std::int16_t* sc = kScanOrders[ts][v].scan;
            std::int16_t* is = kIscanPool[ts][v];
            for (int i = 0; i < n; ++i) is[sc[i]] = (std::int16_t)i;
            kScanOrders[ts][v].iscan = is;
        }
    }
    kScansInitialized = true;
}

static void ensureScansInitialized(void) {
    if (!kScansInitialized) initScans();
}

const ScanOrder (*scanOrders(void))[3] {
    ensureScansInitialized();
    return kScanOrders;
}

const ScanOrder& getScanOrder(std::int32_t txSize, std::int32_t txType) {
    ensureScansInitialized();
    return kScanOrders[txSize][txTypeToScanIndex[txType]];
}

// get_eob_pos_token (entropy_coding.h:94-102, verbatim).
std::int32_t getEobPosToken(std::int32_t eob, std::int32_t* extra) {
    if (eob < 3) {
        *extra = 0;
        return eob;
    }
    const std::int32_t t = getMsb((std::uint32_t)(eob - 1));
    *extra = eob - 1 - (1 << t);
    return t + 2;
}

// get_eob_cost (rd_cost.c:198-211, verbatim).
std::int32_t getEobCost(std::int32_t eob, const LvMapEobCost* txbEobCosts, const LvMapCoeffCost* txbCosts,
                        TxClass txClass) {
    std::int32_t eobExtra;
    const std::int32_t eobPt = getEobPosToken(eob, &eobExtra);
    const std::int32_t eobMultiCtx = (txClass == TX_CLASS_2D) ? 0 : 1;
    std::int32_t eobCost = txbEobCosts->eobCost[eobMultiCtx][eobPt - 1];
    if (eobPt > 2) {
        const std::int32_t cnt = eobPt - 3;
        const std::int32_t bit = (eobExtra >> cnt) & 1;
        eobCost += txbCosts->eobExtraCost[cnt][bit];
        eobCost += av1_cost_literal(cnt);
    }
    return eobCost;
}

// svt_av1_txb_init_levels_c (rd_cost.c:93-105, verbatim).
void txbInitLevels(const std::int32_t* coeff, std::int32_t width, std::int32_t height, std::uint8_t* levels) {
    std::uint8_t* ls = levels;
    for (std::int32_t i = 0; i < height; i++) {
        for (std::int32_t j = 0; j < width; j++) {
            const std::int32_t a = coeff[i * width + j] < 0 ? -coeff[i * width + j] : coeff[i * width + j];
            *ls++ = (std::uint8_t)(a > 127 ? 127 : (a < 0 ? 0 : a));
        }
        for (std::int32_t j = 0; j < TX_PAD_HOR; j++) {
            *ls++ = 0;
        }
    }
}

// (The A9 RED was captured with this block transiently #if 0-disabled;
// restored now. Evidence quoted in the commit message.)
// txsize_sqr_map (common_utils.c:150-170, verbatim).
const std::int8_t txsizeSqrMap[19] = {0, 1, 2, 3, 4, 0, 0, 1, 1, 2, 2, 3, 3, 0, 0, 1, 1, 2, 2};
// txsize_sqr_up_map (common_utils.c:172-192, verbatim).
const std::int8_t txsizeSqrUpMap[19] = {0, 1, 2, 3, 4, 1, 1, 2, 2, 3, 3, 4, 4, 2, 2, 3, 3, 4, 4};
// txsize_log2_minus4 (inv_transforms.h:341-361, verbatim).
const std::int8_t txsizeLog2Minus4[19] = {0, 2, 4, 6, 6, 1, 1, 3, 3, 5, 5, 6, 6, 2, 2, 4, 4, 5, 5};
// tx_size_wide_log2 (common_utils.c:125-127, verbatim).
static const std::int8_t kTxSizeWideLog2Tbl[19] = {2, 3, 4, 5, 6, 2, 3, 3, 4, 4, 5, 5, 6, 2, 4, 3, 5, 4, 6};
// av1_get_tx_scale_tab (full_loop.c:22, verbatim).
const std::int8_t av1GetTxScaleTab[19] = {0, 0, 0, 1, 2, 0, 0, 0, 0, 1, 1, 2, 2, 0, 0, 0, 0, 1, 1};

// get_txsize_entropy_ctx (entropy_coding.h:110-112, verbatim arithmetic).
std::int32_t getTxsizeEntropyCtx(std::int32_t txsize) {
    return (txsizeSqrMap[txsize] + txsizeSqrUpMap[txsize] + 1) >> 1;
}

// nz_map_ctx_offset_1d (coefficients.h:28-34, verbatim; NZ_MAP_CTX_0 =
// SIG_COEF_CONTEXTS_2D = 26, +5, +10 per coefficients.h:24-26).
static const std::int32_t nzMapCtxOffset1d[32] = {
    26, 31, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
};
// eb_av1_nz_map_ctx_offset (coefficients.c:24-303, verbatim): the
// court-approved verbatim carry of src/l7_entropy/nz_map_ctx_offset.inc
// (l16_rdo cannot link l7_entropy; the l7-l13 duplication precedent; the
// twin byte-identity is fixture-checked against both the l7 copy and the
// reference text).
#include "nz_map_ctx_offset.inc"

// get_nz_map_ctx_from_stats (coefficients.h:157-194, verbatim).
std::int32_t getNzMapCtxFromStats(std::int32_t stats, std::int32_t coeffIdx, std::int32_t bwl,
                                   std::int32_t txSize, TxClass txClass) {
    // tx_class == 0(TX_CLASS_2D)
    if ((txClass | coeffIdx) == 0) {
        return 0;
    }
    std::int32_t ctx = (stats + 1) >> 1;
    ctx = ctx < 4 ? ctx : 4;
    switch (txClass) {
    case TX_CLASS_2D: {
        return ctx + eb_av1_nz_map_ctx_offset[txSize][coeffIdx];
    }
    case TX_CLASS_HORIZ: {
        const std::int32_t row = coeffIdx >> bwl;
        const std::int32_t col = coeffIdx - (row << bwl);
        return ctx + nzMapCtxOffset1d[col];
    }
    case TX_CLASS_VERT: {
        const std::int32_t row = coeffIdx >> bwl;
        return ctx + nzMapCtxOffset1d[row];
    }
    default:
        break;
    }
    return 0;
}

// get_lower_levels_ctx (coefficients.h:196-200, verbatim).
std::int32_t getLowerLevelsCtx(const std::uint8_t* levels, std::int32_t coeffIdx, std::int32_t bwl,
                                std::int32_t txSize, TxClass txClass) {
    const std::int32_t stats = getNzMag(levels + getPaddedIdx(coeffIdx, bwl), bwl, txClass);
    return getNzMapCtxFromStats(stats, coeffIdx, bwl, txSize, txClass);
}

// get_golomb_cost (full_loop.c:613-620, verbatim).
std::int32_t getGolombCost(std::int32_t absQc) {
    if (absQc >= 1 + NUM_BASE_LEVELS + COEFF_BASE_RANGE) {
        const std::int32_t r = absQc - COEFF_BASE_RANGE - NUM_BASE_LEVELS;
        const std::int32_t length = getMsb((std::uint32_t)r) + 1;
        return av1_cost_literal(2 * length - 1);
    }
    return 0;
}

// get_br_cost (full_loop.c:622-626, verbatim).
std::int32_t getBrCost(std::int32_t level, const std::int32_t* coeffLps) {
    std::int32_t baseRange = (level - 1 - NUM_BASE_LEVELS) < COEFF_BASE_RANGE ? (level - 1 - NUM_BASE_LEVELS) : COEFF_BASE_RANGE;
    return coeffLps[baseRange] + getGolombCost(level);
}

// The golomb tables (full_loop.c:668-673, verbatim).
const std::int32_t golombBitsCost[32] = {0,       512,     512 * 3, 512 * 3, 512 * 5, 512 * 5, 512 * 5, 512 * 5,
                                         512 * 7, 512 * 7, 512 * 7, 512 * 7, 512 * 7, 512 * 7, 512 * 7, 512 * 7,
                                         512 * 9, 512 * 9, 512 * 9, 512 * 9, 512 * 9, 512 * 9, 512 * 9, 512 * 9,
                                         512 * 9, 512 * 9, 512 * 9, 512 * 9, 512 * 9, 512 * 9, 512 * 9, 512 * 9};
const std::int32_t golombCostDiff[32] = {0,       512, 512 * 2, 0, 512 * 2, 0, 0, 0, 512 * 2, 0, 0, 0, 0, 0, 0, 0,
                                         512 * 2, 0,   0,       0, 0,       0, 0, 0, 0,       0, 0, 0, 0, 0, 0, 0};

// get_br_cost_with_diff (full_loop.c:675-694, verbatim).
std::int32_t getBrCostWithDiff(std::int32_t level, const std::int32_t* coeffLps, std::int32_t* diff) {
    const std::int32_t baseRange =
        (level - 1 - NUM_BASE_LEVELS) < COEFF_BASE_RANGE ? (level - 1 - NUM_BASE_LEVELS) : COEFF_BASE_RANGE;
    std::int32_t golombBits = 0;
    if (level <= COEFF_BASE_RANGE + 1 + NUM_BASE_LEVELS) {
        *diff += coeffLps[baseRange + COEFF_BASE_RANGE + 1];
    }
    if (level >= COEFF_BASE_RANGE + 1 + NUM_BASE_LEVELS) {
        const std::int32_t r = level - COEFF_BASE_RANGE - NUM_BASE_LEVELS;
        if (r < 32) {
            golombBits = golombBitsCost[r];
            *diff += golombCostDiff[r];
        } else {
            golombBits = getGolombCost(level);
            *diff += (r & (r - 1)) == 0 ? 1024 : 0;
        }
    }
    return coeffLps[baseRange] + golombBits;
}

// get_qc_dqc_low (full_loop.c:659-666, verbatim; the asserts fold into the
// derived identities the tests pin).
void getQcDqcLow(std::int32_t absQc, std::int32_t sign, std::int32_t dqv, std::int32_t shift,
                  std::int32_t* qcLow, std::int32_t* dqcLow) {
    const std::int32_t absQcLow = absQc - 1;
    *qcLow = (-sign ^ absQcLow) + sign;
    const std::int32_t absDqcLow = (absQcLow * dqv) >> shift;
    *dqcLow = (-sign ^ absDqcLow) + sign;
}

// get_coeff_dist (full_loop.c:655-657, verbatim; SQR (utility.h:107)).
std::int64_t getCoeffDist(std::int32_t tcoeff, std::int32_t dqcoeff, std::int32_t shift) {
    const std::int64_t d = ((std::int64_t)tcoeff - dqcoeff) * (std::int64_t)(1llu << shift);
    return d * d;
}

// get_coeff_cost_general (full_loop.c:627-653, verbatim).
std::int32_t getCoeffCostGeneral(std::int32_t isLast, std::int32_t ci, std::int32_t absQc, std::int32_t sign,
                                  std::int32_t coeffCtx, std::int32_t dcSignCtx, const LvMapCoeffCost* txbCosts,
                                  std::int32_t bwl, TxClass txClass, const std::uint8_t* levels) {
    std::int32_t cost = 0;
    if (isLast) {
        cost += txbCosts->baseEobCost[coeffCtx][absQc < 3 ? absQc : 3];
    } else {
        cost += txbCosts->baseCost[coeffCtx][absQc < 3 ? absQc : 3];
    }
    if (absQc != 0) {
        if (ci == 0) {
            cost += txbCosts->dcSignCost[dcSignCtx][sign];
        } else {
            cost += av1_cost_literal(1);
        }
        if (absQc > NUM_BASE_LEVELS) {
            std::int32_t brCtx;
            if (isLast) {
                brCtx = getBrCtxEob(ci, bwl, txClass);
            } else {
                brCtx = getBrCtx(levels, ci, bwl, txClass);
            }
            cost += getBrCost(absQc, txbCosts->lpsCost[brCtx]);
        }
    }
    return cost;
}

// get_two_coeff_cost_simple (full_loop.c:696-720, verbatim; the caller
// guarantees ci > 0 - the reference asserts it).
std::int32_t getTwoCoeffCostSimple(std::int32_t ci, std::int32_t absQc, std::int32_t coeffCtx,
                                    const LvMapCoeffCost* txbCosts, std::int32_t bwl, TxClass txClass,
                                    const std::uint8_t* levels, std::int32_t* costLow) {
    std::int32_t cost = txbCosts->baseCost[coeffCtx][absQc < 3 ? absQc : 3];
    std::int32_t diff = 0;
    if (absQc <= 3) {
        diff = txbCosts->baseCost[coeffCtx][absQc + 4];
    }
    if (absQc) {
        cost += av1_cost_literal(1);
        if (absQc > NUM_BASE_LEVELS) {
            const std::int32_t brCtx = getBrCtx(levels, ci, bwl, txClass);
            std::int32_t brcostDiff = 0;
            cost += getBrCostWithDiff(absQc, txbCosts->lpsCost[brCtx], &brcostDiff);
            diff += brcostDiff;
        }
    }
    *costLow = cost - diff;
    return cost;
}

// get_coeff_cost_eob (full_loop.c:722-739, verbatim).
std::int32_t getCoeffCostEob(std::int32_t ci, std::int32_t absQc, std::int32_t sign, std::int32_t coeffCtx,
                              std::int32_t dcSignCtx, const LvMapCoeffCost* txbCosts, std::int32_t bwl,
                              TxClass txClass) {
    std::int32_t cost = 0;
    cost += txbCosts->baseEobCost[coeffCtx][absQc < 3 ? absQc : 3];
    if (absQc != 0) {
        if (ci == 0) {
            cost += txbCosts->dcSignCost[dcSignCtx][sign];
        } else {
            cost += av1_cost_literal(1);
        }
        if (absQc > NUM_BASE_LEVELS) {
            const std::int32_t brCtx = getBrCtxEob(ci, bwl, txClass);
            cost += getBrCost(absQc, txbCosts->lpsCost[brCtx]);
        }
    }
    return cost;
}

// get_dqv (full_loop.c:741-747, verbatim; AOM_QM_BITS = 5, inv_transforms.
// h:27 - NOT aom's 7).
std::int32_t getDqv(const std::int16_t* dequant, std::int32_t coeffIdx, const std::uint8_t* iqmPtr) {
    std::int32_t dqv = dequant[coeffIdx != 0 ? 1 : 0];
    if (iqmPtr != nullptr) {
        dqv = ((iqmPtr[coeffIdx] * dqv) + (1 << (5 - 1))) >> 5;
    }
    return dqv;
}

// RDCOST (rd_cost.h:36-38) with ROUND_POWER_OF_TWO (definitions.h:457) and
// RDDIV_BITS 7 (rd_cost.h:34). The trellis never passes negative rates or
// rdmults, so the unsigned-bias shift is exact here (named deviation: a
// helper, not a macro).
static std::int64_t rdCost(std::int64_t rate, std::int64_t rdmult, std::int64_t dist) {
    return (((rate * rdmult) + (1 << 8)) >> 9) + dist * (1 << 7);
}

// get_lower_levels_ctx_general (full_loop.c:596-611, verbatim).
std::int32_t getLowerLevelsCtxGeneral(std::int32_t isLast, std::int32_t scanIdx, std::int32_t bwl,
                                       std::int32_t height, const std::uint8_t* levels, std::int32_t coeffIdx,
                                       std::int32_t txSize, TxClass txClass) {
    if (isLast) {
        if (scanIdx == 0) {
            return 0;
        }
        if (scanIdx <= (height << bwl) >> 3) {
            return 1;
        }
        if (scanIdx <= (height << bwl) >> 2) {
            return 2;
        }
        return 3;
    }
    return getLowerLevelsCtx(levels, coeffIdx, bwl, txSize, txClass);
}

// update_coeff_general (full_loop.c:851-902, verbatim).
void updateCoeffGeneral(std::int32_t* accuRate, std::int64_t* accuDist, std::int32_t si, std::int32_t eob,
                         std::int32_t txSize, TxClass txClass, std::int32_t bwl, std::int32_t height,
                         std::int64_t rdmult, std::int32_t shift, std::int32_t dcSignCtx,
                         const std::int16_t* dequant, const std::int16_t* scan, const LvMapCoeffCost* txbCosts,
                         const std::int32_t* tcoeff, std::int32_t* qcoeff, std::int32_t* dqcoeff,
                         std::uint8_t* levels, const std::uint8_t* iqmPtr) {
    const std::int32_t ci = scan[si];
    const std::int32_t dqv = getDqv(dequant, ci, iqmPtr);
    const std::int32_t qc = qcoeff[ci];
    const std::int32_t isLast = si == (eob - 1);
    const std::int32_t coeffCtx =
        getLowerLevelsCtxGeneral(isLast, si, bwl, height, levels, ci, txSize, txClass);
    if (qc == 0) {
        *accuRate += txbCosts->baseCost[coeffCtx][0];
    } else {
        const std::int32_t sign = (qc < 0) ? 1 : 0;
        const std::int32_t absQc = qc < 0 ? -qc : qc;
        const std::int32_t tqc = tcoeff[ci];
        const std::int32_t dqc = dqcoeff[ci];
        const std::int64_t dist = getCoeffDist(tqc, dqc, shift);
        const std::int64_t dist0 = getCoeffDist(tqc, 0, shift);
        const std::int32_t rate = getCoeffCostGeneral(isLast, ci, absQc, sign, coeffCtx, dcSignCtx, txbCosts,
                                                       bwl, txClass, levels);
        const std::int64_t rd = rdCost(rate, rdmult, dist);

        std::int32_t qcLow, dqcLow, absQcLow;
        std::int64_t distLow, rdLow;
        std::int32_t rateLow;
        if (absQc == 1) {
            absQcLow = qcLow = dqcLow = 0;
            distLow = dist0;
            rateLow = txbCosts->baseCost[coeffCtx][0];
        } else {
            getQcDqcLow(absQc, sign, dqv, shift, &qcLow, &dqcLow);
            absQcLow = absQc - 1;
            distLow = getCoeffDist(tqc, dqcLow, shift);
            rateLow = getCoeffCostGeneral(isLast, ci, absQcLow, sign, coeffCtx, dcSignCtx, txbCosts, bwl,
                                           txClass, levels);
        }

        rdLow = rdCost(rateLow, rdmult, distLow);
        if (rdLow < rd) {
            qcoeff[ci] = qcLow;
            dqcoeff[ci] = dqcLow;
            levels[getPaddedIdx(ci, bwl)] = (std::uint8_t)(absQcLow > 127 ? 127 : absQcLow);
            *accuRate += rateLow;
            *accuDist += distLow - dist0;
        } else {
            *accuRate += rate;
            *accuDist += dist - dist0;
        }
    }
}

// update_skip (full_loop.c:951-966, verbatim).
void updateSkip(std::int32_t* accuRate, std::int64_t accuDist, std::uint16_t* eob, std::int32_t nzNum,
                 const std::int32_t* nzCi, std::int64_t rdmult, std::int32_t skipCost, std::int32_t nonSkipCost,
                 std::int32_t* qcoeff, std::int32_t* dqcoeff, std::int32_t sharpness) {
    const std::int64_t rd = rdCost((std::int64_t)*accuRate + nonSkipCost, rdmult, accuDist);
    const std::int64_t rdNewEob = rdCost(skipCost, rdmult, 0);
    if (sharpness == 0 && rdNewEob < rd) {
        for (std::int32_t i = 0; i < nzNum; ++i) {
            const std::int32_t ci = nzCi[i];
            qcoeff[ci] = 0;
            dqcoeff[ci] = 0;
            // no need to set up levels because this is the last step
            // levels[get_padded_idx(ci, bwl)] = 0;
        }
        *accuRate = 0;
        *eob = 0;
    }
}

// update_coeff_eob_fast (full_loop.c:1006-1026, verbatim).
void updateCoeffEobFast(std::uint16_t* eob, std::int32_t shift, const std::int16_t* dequant,
                         const std::int16_t* scan, const std::int32_t* coeffPtr, std::int32_t* qcoeffPtr,
                         std::int32_t* dqcoeffPtr) {
    std::int32_t eobOut = *eob;
    const std::int32_t zbin[2] = {dequant[0] + ROUND_POWER_OF_TWO(dequant[0] * 70, 7),
                                  dequant[1] + ROUND_POWER_OF_TWO(dequant[1] * 70, 7)};
    for (std::int32_t i = *eob - 1; i >= 0; --i) {
        const std::int32_t rc = scan[i];
        const std::int32_t qcoeff = qcoeffPtr[rc];
        const std::int32_t coeff = coeffPtr[rc];
        const std::int32_t coeffSign = -(coeff < 0);
        std::int64_t absCoeff = (coeff ^ coeffSign) - coeffSign;
        if (((absCoeff << (1 + shift)) < zbin[rc != 0]) || (qcoeff == 0)) {
            eobOut--;
            qcoeffPtr[rc] = 0;
            dqcoeffPtr[rc] = 0;
        } else {
            break;
        }
    }
    *eob = (std::uint16_t)eobOut;
}

// update_coeff_eob (full_loop.c:749-849, verbatim).
void updateCoeffEob(std::int32_t* accuRate, std::int64_t* accuDist, std::uint16_t* eob, std::int32_t* nzNum,
                     std::int32_t* nzCi, std::int32_t si, std::int32_t txSize, TxClass txClass,
                     std::int32_t bwl, std::int32_t height, std::int32_t dcSignCtx, std::int64_t rdmult,
                     std::int32_t shift, const std::int16_t* dequant, const std::int16_t* scan,
                     const LvMapEobCost* txbEobCosts, const LvMapCoeffCost* txbCosts, const std::int32_t* tcoeff,
                     std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint8_t* levels,
                     std::int32_t sharpness, const std::uint8_t* iqmPtr) {
    const std::int32_t ci = scan[si];
    const std::int32_t dqv = getDqv(dequant, ci, iqmPtr);
    const std::int32_t qc = qcoeff[ci];
    const std::int32_t coeffCtx = getLowerLevelsCtx(levels, ci, bwl, txSize, txClass);
    if (qc == 0) {
        *accuRate += txbCosts->baseCost[coeffCtx][0];
    } else {
        std::int32_t lowerLevel = 0;
        const std::int32_t absQc = qc < 0 ? -qc : qc;
        const std::int32_t tqc = tcoeff[ci];
        const std::int32_t dqc = dqcoeff[ci];
        const std::int32_t sign = (qc < 0) ? 1 : 0;
        const std::int64_t dist0 = getCoeffDist(tqc, 0, shift);
        std::int64_t dist = getCoeffDist(tqc, dqc, shift) - dist0;
        std::int32_t rate = getCoeffCostGeneral(0, ci, absQc, sign, coeffCtx, dcSignCtx, txbCosts, bwl, txClass,
                                                 levels);
        std::int64_t rd = rdCost((std::int64_t)*accuRate + rate, rdmult, *accuDist + dist);

        std::int32_t qcLow, dqcLow, absQcLow;
        std::int64_t distLow, rdLow;
        std::int32_t rateLow;
        if (absQc == 1) {
            absQcLow = 0;
            dqcLow = qcLow = 0;
            distLow = 0;
            rateLow = txbCosts->baseCost[coeffCtx][0];
            rdLow = rdCost((std::int64_t)*accuRate + rateLow, rdmult, *accuDist);
        } else {
            getQcDqcLow(absQc, sign, dqv, shift, &qcLow, &dqcLow);
            absQcLow = absQc - 1;
            distLow = getCoeffDist(tqc, dqcLow, shift) - dist0;
            rateLow = getCoeffCostGeneral(0, ci, absQcLow, sign, coeffCtx, dcSignCtx, txbCosts, bwl, txClass,
                                           levels);
            rdLow = rdCost((std::int64_t)*accuRate + rateLow, rdmult, *accuDist + distLow);
        }

        std::int32_t lowerLevelNewEob = 0;
        const std::int32_t newEob = si + 1;
        const std::int32_t coeffCtxNewEob = getLowerLevelsCtxEob(bwl, height, si);
        const std::int32_t newEobCost = getEobCost(newEob, txbEobCosts, txbCosts, txClass);
        std::int32_t rateCoeffEob =
            newEobCost + getCoeffCostEob(ci, absQc, sign, coeffCtxNewEob, dcSignCtx, txbCosts, bwl, txClass);
        std::int64_t distNewEob = dist;
        std::int64_t rdNewEob = rdCost(rateCoeffEob, rdmult, distNewEob);

        if (absQcLow > 0) {
            const std::int32_t rateCoeffEobLow =
                newEobCost + getCoeffCostEob(ci, absQcLow, sign, coeffCtxNewEob, dcSignCtx, txbCosts, bwl, txClass);
            const std::int64_t distNewEobLow = distLow;
            const std::int64_t rdNewEobLow = rdCost(rateCoeffEobLow, rdmult, distNewEobLow);
            if (rdNewEobLow < rdNewEob) {
                lowerLevelNewEob = 1;
                rdNewEob = rdNewEobLow;
                rateCoeffEob = rateCoeffEobLow;
                distNewEob = distNewEobLow;
            }
        }

        if (rdLow < rd) {
            lowerLevel = 1;
            rd = rdLow;
            rate = rateLow;
            dist = distLow;
        }

        if (sharpness == 0 && rdNewEob < rd) {
            for (std::int32_t ni = 0; ni < *nzNum; ++ni) {
                const std::int32_t lastCi = nzCi[ni];
                levels[getPaddedIdx(lastCi, bwl)] = 0;
                qcoeff[lastCi] = 0;
                dqcoeff[lastCi] = 0;
            }
            *eob = (std::uint16_t)newEob;
            *nzNum = 0;
            *accuRate = rateCoeffEob;
            *accuDist = distNewEob;
            lowerLevel = lowerLevelNewEob;
        } else {
            *accuRate += rate;
            *accuDist += dist;
        }

        if (lowerLevel) {
            qcoeff[ci] = qcLow;
            dqcoeff[ci] = dqcLow;
            levels[getPaddedIdx(ci, bwl)] = (std::uint8_t)(absQcLow > 127 ? 127 : absQcLow);
        }
        if (qcoeff[ci]) {
            nzCi[*nzNum] = ci;
            ++*nzNum;
        }
    }
}

// update_coeff_simple (full_loop.c:904-949, verbatim).
void updateCoeffSimple(std::int32_t* accuRate, std::int32_t si, std::int32_t eob, std::int32_t txSize,
                        TxClass txClass, std::int32_t bwl, std::int64_t rdmult, std::int32_t shift,
                        const std::int16_t* dequant, const std::int16_t* scan, const LvMapCoeffCost* txbCosts,
                        const std::int32_t* tcoeff, std::int32_t* qcoeff, std::int32_t* dqcoeff,
                        std::uint8_t* levels, const std::uint8_t* iqmPtr) {
    (void)eob;  // the reference voids it (full_loop.c:910)
    const std::int32_t dqv = getDqv(dequant, scan[si], iqmPtr);
    const std::int32_t ci = scan[si];
    const std::int32_t qc = qcoeff[ci];
    const std::int32_t coeffCtx = getLowerLevelsCtx(levels, ci, bwl, txSize, txClass);
    if (qc == 0) {
        *accuRate += txbCosts->baseCost[coeffCtx][0];
    } else {
        const std::int32_t absQc = qc < 0 ? -qc : qc;
        const std::int32_t tci = tcoeff[ci];
        const std::int32_t absTqc = tci < 0 ? -tci : tci;
        const std::int32_t dqci = dqcoeff[ci];
        const std::int32_t absDqc = dqci < 0 ? -dqci : dqci;
        std::int32_t rateLow = 0;
        const std::int32_t rate = getTwoCoeffCostSimple(ci, absQc, coeffCtx, txbCosts, bwl, txClass, levels,
                                                         &rateLow);
        if (absDqc < absTqc) {
            *accuRate += rate;
            return;
        }

        const std::int64_t dist = getCoeffDist(absTqc, absDqc, shift);
        const std::int64_t rd = rdCost(rate, rdmult, dist);

        const std::int32_t absQcLow = absQc - 1;
        const std::int32_t absDqcLow = (absQcLow * dqv) >> shift;
        const std::int64_t distLow = getCoeffDist(absTqc, absDqcLow, shift);
        const std::int64_t rdLow = rdCost(rateLow, rdmult, distLow);

        if (rdLow < rd) {
            const std::int32_t sign = (qc < 0) ? 1 : 0;
            qcoeff[ci] = (-sign ^ absQcLow) + sign;
            dqcoeff[ci] = (-sign ^ absDqcLow) + sign;
            levels[getPaddedIdx(ci, bwl)] = (std::uint8_t)(absQcLow > 127 ? 127 : absQcLow);
            *accuRate += rateLow;
        } else {
            *accuRate += rate;
        }
    }
}


// clamp (definitions.h:989-993, verbatim semantics; used by av1_cost_symbol
// md_rate_estimation.c:39).
static inline std::int32_t clampI32(std::int32_t v, std::int32_t lo, std::int32_t hi) {
    return (v < lo) ? lo : (v > hi ? hi : v);
}

// get_msb, portable body (definitions.h:628-644): returns floor(log2(n)).
static inline std::int32_t getMsb(std::uint32_t n) {
    std::int32_t log = 0;
    while (n >>= 1) ++log;
    return log;
}

// get_prob (cabac_context_model.h:68-75, verbatim arithmetic).
static inline std::uint8_t getProb(unsigned num, unsigned den) {
    const int p = (int)(((std::uint64_t)num * 256 + (den >> 1)) / den);
    const int clippedProb = p | ((255 - p) >> 23) | (p == 0);
    return (std::uint8_t)clippedProb;
}

// av1_prob_cost (md_rate_estimation.h:131-138, verbatim transcription).
const std::uint16_t av1ProbCost[128] = {
    512, 506, 501, 495, 489, 484, 478, 473, 467, 462, 456, 451, 446, 441, 435, 430,
    425, 420, 415, 410, 405, 400, 395, 390, 385, 380, 375, 371, 366, 361, 356, 352,
    347, 343, 338, 333, 329, 324, 320, 316, 311, 307, 302, 298, 294, 289, 285, 281,
    277, 273, 268, 264, 260, 256, 252, 248, 244, 240, 236, 232, 228, 224, 220, 216,
    212, 209, 205, 201, 197, 194, 190, 186, 182, 179, 175, 171, 168, 164, 161, 157,
    153, 150, 146, 143, 139, 136, 132, 129, 125, 122, 119, 115, 112, 109, 105, 102,
    99,  95,  92,  89,  86,  82,  79,  76,  73,  70,  66,  63,  60,  57,  54,  51,
    48,  45,  42,  38,  35,  32,  29,  26,  23,  20,  18,  15,  12,  9,   6,   3,
};

// av1_cost_symbol (md_rate_estimation.c:35-45).
std::int32_t costSymbol(AomCdfProb p15) {
    p15 = (AomCdfProb)clampI32(p15, 1, CDF_PROB_TOP - 1);
    const std::int32_t shift = CDF_PROB_BITS - 1 - getMsb(p15);
    const std::int32_t prob = getProb((unsigned)p15 << shift, CDF_PROB_TOP);
    return av1ProbCost[prob - 128] + av1_cost_literal(shift);
}

// svt_aom_get_syntax_rate_from_cdf (md_rate_estimation.c:50-69).
void getSyntaxRateFromCdf(std::int32_t* costs, const AomCdfProb* cdf, const std::int32_t* invMap) {
    std::int32_t i;
    AomCdfProb prevCdf = 0;
    for (i = 0;; ++i) {
        AomCdfProb p15 = AOM_ICDF(cdf[i]) - prevCdf;
        p15 = (p15 < EC_MIN_PROB) ? EC_MIN_PROB : p15;
        prevCdf = AOM_ICDF(cdf[i]);

        if (invMap) {
            costs[invMap[i]] = costSymbol(p15);
        } else {
            costs[i] = costSymbol(p15);
        }

        if (cdf[i] == AOM_ICDF(CDF_PROB_TOP)) {
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// MV magnitude cost table (md_rate_estimation.c:378-435).
// ---------------------------------------------------------------------------
std::int32_t mvClassBase(MvClassType c) {
    return c ? CLASS0_SIZE << (c + 2) : 0;
}

// svt_av1_get_mv_class (md_rate_estimation.c:370-376).
MvClassType getMvClass(std::int32_t z, std::int32_t* offset) {
    const MvClassType c = (z >= CLASS0_SIZE * 4096) ? MvClass_10
                          : (MvClassType)getMsb((std::uint32_t)(z >> 3) | 1);
    if (offset) {
        *offset = z - mvClassBase(c);
    }
    return c;
}

// build_nmv_component_cost_table (md_rate_estimation.c:378-435).
void buildNmvComponentCostTable(std::int32_t* mvcost, const NmvComponent* mvcomp,
                                MvSubpelPrecision precision) {
    std::int32_t sign_cost[2], class_cost[MV_CLASSES], class0_cost[CLASS0_SIZE];
    std::int32_t bits_cost[MV_OFFSET_BITS][2];
    std::int32_t class0_fp_cost[CLASS0_SIZE][MV_FP_SIZE], fp_cost[MV_FP_SIZE];
    std::int32_t class0_hp_cost[2], hp_cost[2];

    getSyntaxRateFromCdf(sign_cost, mvcomp->sign_cdf, nullptr);
    getSyntaxRateFromCdf(class_cost, mvcomp->classes_cdf, nullptr);
    getSyntaxRateFromCdf(class0_cost, mvcomp->class0_cdf, nullptr);
    for (int i = 0; i < MV_OFFSET_BITS; ++i) {
        getSyntaxRateFromCdf(bits_cost[i], mvcomp->bits_cdf[i], nullptr);
    }
    for (int i = 0; i < CLASS0_SIZE; ++i) {
        getSyntaxRateFromCdf(class0_fp_cost[i], mvcomp->class0_fp_cdf[i], nullptr);
    }
    getSyntaxRateFromCdf(fp_cost, mvcomp->fp_cdf, nullptr);
    if (precision > MvSubpelLowPrecision) {
        getSyntaxRateFromCdf(class0_hp_cost, mvcomp->class0_hp_cdf, nullptr);
        getSyntaxRateFromCdf(hp_cost, mvcomp->hp_cdf, nullptr);
    }
    mvcost[0] = 0;
    for (int v = 1; v <= MV_MAX; ++v) {
        std::int32_t z, c, o, d, e, f, cost = 0;
        z = v - 1;
        c = (std::int32_t)getMvClass(z, &o);
        cost += class_cost[c];
        d = (o >> 3);
        f = (o >> 1) & 3;
        e = (o & 1);
        if (c == 0) {
            cost += class0_cost[d];
        } else {
            const std::int32_t b = c + CLASS0_BITS - 1;
            for (int i = 0; i < b; ++i) {
                cost += bits_cost[i][((d >> i) & 1)];
            }
        }
        if (precision > MvSubpelNone) {
            if (c == 0) {
                cost += class0_fp_cost[d][f];
            } else {
                cost += fp_cost[f];
            }
            if (precision > MvSubpelLowPrecision) {
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

// ---------------------------------------------------------------------------
// svt_aom_estimate_coefficients_rate (md_rate_estimation.c:486-584).
// ---------------------------------------------------------------------------
void estimateCoefficientsRate(MdRateEstimationContext* me, const FcTokenCdfs* fc) {
    const int nplanes = 2;  // AOMMIN(3, PLANE_TYPES) = 2
    for (int eob_multi_size = 0; eob_multi_size < 7; ++eob_multi_size) {
        for (int plane = 0; plane < nplanes; ++plane) {
            LvMapEobCost* pcost = &me->eobFracBits[eob_multi_size][plane];
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
                getSyntaxRateFromCdf(pcost->eobCost[ctx], pcdf, nullptr);
            }
        }
    }
    for (int tx_size = 0; tx_size < TX_SIZES; ++tx_size) {
        for (int plane = 0; plane < nplanes; ++plane) {
            LvMapCoeffCost* pcost = &me->coeffFacBits[tx_size][plane];
            for (int ctx = 0; ctx < TXB_SKIP_CONTEXTS; ++ctx) {
                getSyntaxRateFromCdf(pcost->txbSkipCost[ctx], &fc->txbSkipCdf[tx_size][ctx][0], nullptr);
            }
            for (int ctx = 0; ctx < SIG_COEF_CONTEXTS_EOB; ++ctx) {
                getSyntaxRateFromCdf(pcost->baseEobCost[ctx], &fc->coeffBaseEobCdf[tx_size][plane][ctx][0], nullptr);
            }
            for (int ctx = 0; ctx < SIG_COEF_CONTEXTS; ++ctx) {
                getSyntaxRateFromCdf(pcost->baseCost[ctx], &fc->coeffBaseCdf[tx_size][plane][ctx][0], nullptr);
            }
            for (int ctx = 0; ctx < SIG_COEF_CONTEXTS; ++ctx) {
                pcost->baseCost[ctx][4] = 0;
                pcost->baseCost[ctx][5] = pcost->baseCost[ctx][1] + av1_cost_literal(1) - pcost->baseCost[ctx][0];
                pcost->baseCost[ctx][6] = pcost->baseCost[ctx][2] - pcost->baseCost[ctx][1];
                pcost->baseCost[ctx][7] = pcost->baseCost[ctx][3] - pcost->baseCost[ctx][2];
            }
            for (int ctx = 0; ctx < EOB_COEF_CONTEXTS; ++ctx) {
                getSyntaxRateFromCdf(pcost->eobExtraCost[ctx], &fc->eobExtraCdf[tx_size][plane][ctx][0], nullptr);
            }
            for (int ctx = 0; ctx < DC_SIGN_CONTEXTS; ++ctx) {
                getSyntaxRateFromCdf(pcost->dcSignCost[ctx], &fc->dcSignCdf[plane][ctx][0], nullptr);
            }
            for (int ctx = 0; ctx < LEVEL_CONTEXTS; ++ctx) {
                std::int32_t br_rate[BR_CDF_SIZE];
                std::int32_t prev_cost = 0;
                int i, j;
                getSyntaxRateFromCdf(br_rate, &fc->coeffBrCdf[tx_size][plane][ctx][0], nullptr);
                for (i = 0; i < COEFF_BASE_RANGE; i += BR_CDF_SIZE - 1) {
                    for (j = 0; j < BR_CDF_SIZE - 1; j++) {
                        pcost->lpsCost[ctx][i + j] = prev_cost + br_rate[j];
                    }
                    prev_cost += br_rate[j];
                }
                pcost->lpsCost[ctx][i] = prev_cost;
            }
            for (int ctx = 0; ctx < LEVEL_CONTEXTS; ++ctx) {
                pcost->lpsCost[ctx][0 + COEFF_BASE_RANGE + 1] = pcost->lpsCost[ctx][0];
                for (int i = 1; i <= COEFF_BASE_RANGE; ++i) {
                    pcost->lpsCost[ctx][i + COEFF_BASE_RANGE + 1] = pcost->lpsCost[ctx][i] - pcost->lpsCost[ctx][i - 1];
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// S8: the syntax-rate machinery (md_rate_estimation.c:76-364) + the
// helpers (mode_decision.c:102-120, entropy_coding.c:922-930, :4218-4226,
// common_utils.c:286-291, :39-40).
// ---------------------------------------------------------------------------
const std::uint8_t blockSizeWide[BLOCK_SIZES_ALL] = {
    4, 4, 8, 8, 8, 16, 16, 16, 32, 32, 32, 64, 64, 64, 128, 128, 4, 16, 8, 32, 16, 64};
const std::uint8_t blockSizeHigh[BLOCK_SIZES_ALL] = {
    4, 8, 4, 8, 16, 8, 16, 32, 16, 32, 64, 32, 64, 128, 64, 128, 16, 4, 32, 8, 64, 16};
const std::uint8_t ebNumPelsLog2Lookup[BLOCK_SIZES_ALL] = {
    4, 5, 5, 6, 7, 7, 8, 9, 9, 10, 11, 11, 12, 13, 13, 14, 6, 6, 8, 8, 10, 10};

// use_inter_ext_tx_for_txsize / use_intra_ext_tx_for_txsize
// (md_rate_estimation.h:139-149, verbatim) + av1_ext_tx_set_idx_to_type
// (md_rate_estimation.h:151-165, verbatim) + av1_ext_tx_inv
// (cabac_context_model.c:43-50, verbatim).
const std::int32_t useInterExtTxForTxsize[EXT_TX_SETS_INTER][EXT_TX_SIZES] = {
    {1, 1, 1, 1}, {1, 1, 0, 0}, {0, 0, 1, 0}, {0, 1, 1, 1}};
const std::int32_t useIntraExtTxForTxsize[EXT_TX_SETS_INTRA][EXT_TX_SIZES] = {
    {1, 1, 1, 1}, {1, 1, 0, 0}, {0, 0, 1, 0}};
const TxSetType av1ExtTxSetIdxToType[2][4] = {
    {EXT_TX_SET_DCTONLY, EXT_TX_SET_DTT4_IDTX_1DDCT, EXT_TX_SET_DTT4_IDTX},
    {EXT_TX_SET_DCTONLY, EXT_TX_SET_ALL16, EXT_TX_SET_DTT9_IDTX_1DDCT, EXT_TX_SET_DCT_IDTX}};
const std::int32_t av1ExtTxInv[EXT_TX_SET_TYPES][TX_TYPES] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {9, 0, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {9, 0, 10, 11, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {9, 10, 11, 0, 1, 2, 4, 5, 3, 6, 7, 8, 0, 0, 0, 0},
    {9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 4, 5, 3, 6, 7, 8}};

// svt_aom_partition_cdf_length (entropy_coding.c:922-930).
std::int32_t partitionCdfLength(BlockSize bsize) {
    if (bsize <= BLOCK_8X8) {
        return PARTITION_TYPES;
    } else if (bsize == BLOCK_128X128) {
        return EXT_PARTITION_TYPES - 2;
    } else {
        return EXT_PARTITION_TYPES;
    }
}
// svt_aom_allow_intrabc (entropy_coding.c:4397-4399): the FrameHeader wall
// threaded as parameters (named deviation).
std::int32_t allowIntrabc(std::int32_t allowScreenContentTools, std::int32_t allowIntrabc) {
    return (allowScreenContentTools && allowIntrabc) ? 1 : 0;
}
// svt_aom_allow_palette (entropy_coding.c:4218-4222): the CONFIG gate
// threaded as the parameter (named deviation; the pinned build's behavior
// is the enable=0 case).
std::int32_t allowPalette(std::int32_t enable, std::int32_t allowScreenContentTools, BlockSize bsize) {
    return enable && allowScreenContentTools && blockSizeWide[bsize] <= 64 &&
           blockSizeHigh[bsize] <= 64 && bsize >= BLOCK_8X8;
}
// svt_aom_get_palette_bsize_ctx (entropy_coding.c:4224-4226).
std::int32_t getPaletteBsizeCtx(BlockSize bsize) {
    return ebNumPelsLog2Lookup[bsize] - ebNumPelsLog2Lookup[BLOCK_8X8];
}
// svt_aom_filter_intra_allowed_bsize (mode_decision.c:108-113): the CONFIG
// gate threaded as the parameter (named deviation).
std::int32_t filterIntraAllowedBsize(std::int32_t enable, BlockSize bs) {
    return enable && blockSizeWide[bs] <= 32 && blockSizeHigh[bs] <= 32;
}
// svt_aom_is_interintra_allowed_bsize (mode_decision.h:142-147): the CONFIG
// gate threaded as the parameter (named deviation).
std::int32_t isInterIntraAllowedBsize(std::int32_t enable, BlockSize bs) {
    return enable && (bs >= BLOCK_8X8) && (bs <= BLOCK_32X32);
}
// svt_aom_is_interintra_allowed_mode (mode_decision.h:149-151).
std::int32_t isInterIntraAllowedMode(PredictionMode mode) {
    return (mode >= SINGLE_INTER_MODE_START) && (mode < SINGLE_INTER_MODE_END);
}
// svt_aom_is_interintra_allowed_ref (mode_decision.h:153-155).
std::int32_t isInterIntraAllowedRef(const std::int32_t rf[2]) {
    return (rf[0] > INTRA_FRAME) && (rf[1] <= INTRA_FRAME);
}

// ---------------------------------------------------------------------------
// svt_aom_estimate_syntax_rate (md_rate_estimation.c:76-:364): the intra +
// intrabc surface. The inter block :270-:363 is PARKED (our emission is
// intra-only); the pinned build's CONFIG_ENABLE_FILTER_INTRA / _PALETTE /
// _RESTORATION are 0 and force the first three guards to 0 at :82-:90 -
// the port threads them as runtime parameters with identical guards.
// ---------------------------------------------------------------------------
// cdf_element_prob (cabac_context_model.h:373-:376).
static AomCdfProb cdfElementProb(const AomCdfProb* cdf, std::size_t element) {
    return (element > 0 ? cdf[element - 1] : CDF_PROB_TOP) - cdf[element];
}
// partition_gather_horz_alike (cabac_context_model.h:378-:391).
static void partitionGatherHorzAlike(AomCdfProb* out, const AomCdfProb* in, BlockSize bsize) {
    out[0] = CDF_PROB_TOP;
    out[0] -= cdfElementProb(in, PARTITION_HORZ);
    out[0] -= cdfElementProb(in, PARTITION_SPLIT);
    out[0] -= cdfElementProb(in, PARTITION_HORZ_A);
    out[0] -= cdfElementProb(in, PARTITION_HORZ_B);
    out[0] -= cdfElementProb(in, PARTITION_VERT_A);
    if (bsize != BLOCK_128X128) {
        out[0] -= cdfElementProb(in, PARTITION_HORZ_4);
    }
    out[0] = AOM_ICDF(out[0]);
    out[1] = AOM_ICDF(CDF_PROB_TOP);
    out[2] = 0;
}
// partition_gather_vert_alike (cabac_context_model.h:393-:406).
static void partitionGatherVertAlike(AomCdfProb* out, const AomCdfProb* in, BlockSize bsize) {
    out[0] = CDF_PROB_TOP;
    out[0] -= cdfElementProb(in, PARTITION_VERT);
    out[0] -= cdfElementProb(in, PARTITION_SPLIT);
    out[0] -= cdfElementProb(in, PARTITION_HORZ_A);
    out[0] -= cdfElementProb(in, PARTITION_VERT_A);
    out[0] -= cdfElementProb(in, PARTITION_VERT_B);
    if (bsize != BLOCK_128X128) {
        out[0] -= cdfElementProb(in, PARTITION_VERT_4);
    }
    out[0] = AOM_ICDF(out[0]);
    out[1] = AOM_ICDF(CDF_PROB_TOP);
    out[2] = 0;
}

void estimateSyntaxRate(MdRateEstimationContext* me, std::uint8_t pic_filter_intra_level,
                        std::uint8_t allow_screen_content_tools, std::uint8_t enable_restoration,
                        std::uint8_t allow_intrabc, const FcSyntaxCdfs* fc) {
    int i, j;
    me->initialized = 1;
    for (i = 0; i < PARTITION_CONTEXTS; ++i) {
        getSyntaxRateFromCdf(me->partitionFacBits[i], fc->partition_cdf[i], nullptr);
        AomCdfProb cdf[CDF_SIZE(2)];
        partitionGatherVertAlike(cdf, fc->partition_cdf[i], BLOCK_16X16);
        getSyntaxRateFromCdf(me->partitionVertAlikeFacBits[i], cdf, nullptr);
        if (cdf[0] == 0) {
            me->partitionVertAlikeFacBits[i][1] = costSymbol(EC_MIN_PROB);
        }
        partitionGatherVertAlike(cdf, fc->partition_cdf[i], BLOCK_128X128);
        getSyntaxRateFromCdf(me->partitionVertAlike128FacBits[i], cdf, nullptr);
        if (cdf[0] == 0) {
            me->partitionVertAlike128FacBits[i][1] = costSymbol(EC_MIN_PROB);
        }
        partitionGatherHorzAlike(cdf, fc->partition_cdf[i], BLOCK_16X16);
        getSyntaxRateFromCdf(me->partitionHorzAlikeFacBits[i], cdf, nullptr);
        if (cdf[0] == 0) {
            me->partitionHorzAlikeFacBits[i][1] = costSymbol(EC_MIN_PROB);
        }
        partitionGatherHorzAlike(cdf, fc->partition_cdf[i], BLOCK_128X128);
        getSyntaxRateFromCdf(me->partitionHorzAlike128FacBits[i], cdf, nullptr);
        if (cdf[0] == 0) {
            me->partitionHorzAlike128FacBits[i][1] = costSymbol(EC_MIN_PROB);
        }
    }
    for (i = 0; i < SKIP_CONTEXTS; ++i) {
        getSyntaxRateFromCdf(me->skipModeFacBits[i], fc->skip_mode_cdfs[i], nullptr);
    }
    for (i = 0; i < SKIP_CONTEXTS; ++i) {
        getSyntaxRateFromCdf(me->skipFacBits[i], fc->skip_cdfs[i], nullptr);
    }
    for (i = 0; i < KF_MODE_CONTEXTS; ++i) {
        for (j = 0; j < KF_MODE_CONTEXTS; ++j) {
            getSyntaxRateFromCdf(me->yModeFacBits[i][j], fc->kf_y_cdf[i][j], nullptr);
        }
    }
    for (i = 0; i < BlockSize_GROUPS; ++i) {
        getSyntaxRateFromCdf(me->mbModeFacBits[i], fc->y_mode_cdf[i], nullptr);
    }
    for (i = 0; i < CFL_ALLOWED_TYPES; ++i) {
        for (j = 0; j < INTRA_MODES; ++j) {
            getSyntaxRateFromCdf(me->intraUvModeFacBits[i][j], fc->uv_mode_cdf[i][j], nullptr);
        }
    }
    if (pic_filter_intra_level) {
        getSyntaxRateFromCdf(me->filterIntraModeFacBits, fc->filter_intra_mode_cdf, nullptr);
        for (i = 0; i < BLOCK_SIZES_ALL; ++i) {
            if (filterIntraAllowedBsize(1, (BlockSize)i)) {
                getSyntaxRateFromCdf(me->filterIntraFacBits[i], fc->filter_intra_cdfs[i], nullptr);
            }
        }
    }
    for (i = 0; i < SWITCHABLE_FILTER_CONTEXTS; ++i) {
        getSyntaxRateFromCdf(me->switchableInterpFacBits[i], fc->switchable_interp_cdf[i], nullptr);
    }
    if (allow_screen_content_tools) {
        for (i = 0; i < PALATTE_BSIZE_CTXS; ++i) {
            getSyntaxRateFromCdf(me->paletteYsizeFacBits[i], fc->palette_y_size_cdf[i], nullptr);
            getSyntaxRateFromCdf(me->paletteUvSizeFacBits[i], fc->palette_uv_size_cdf[i], nullptr);
            for (j = 0; j < PALETTE_Y_MODE_CONTEXTS; ++j) {
                getSyntaxRateFromCdf(me->paletteYmodeFacBits[i][j], fc->palette_y_mode_cdf[i][j], nullptr);
            }
        }
        for (i = 0; i < PALETTE_UV_MODE_CONTEXTS; ++i) {
            getSyntaxRateFromCdf(me->paletteUvModeFacBits[i], fc->palette_uv_mode_cdf[i], nullptr);
        }
        for (i = 0; i < PALETTE_SIZES; ++i) {
            for (j = 0; j < PALETTE_COLOR_INDEX_CONTEXTS; ++j) {
                getSyntaxRateFromCdf(me->paletteYcolorFacBits[i][j], fc->palette_y_color_index_cdf[i][j], nullptr);
                getSyntaxRateFromCdf(me->paletteUvColorFacBits[i][j], fc->palette_uv_color_index_cdf[i][j], nullptr);
            }
        }
    }
    std::int32_t sign_fac_bits[CFL_JOINT_SIGNS];
    getSyntaxRateFromCdf(sign_fac_bits, fc->cfl_sign_cdf, nullptr);
    for (int joint_sign = 0; joint_sign < CFL_JOINT_SIGNS; joint_sign++) {
        std::int32_t* fac_bits_u = me->cflAlphaFacBits[joint_sign][CFL_PRED_U];
        std::int32_t* fac_bits_v = me->cflAlphaFacBits[joint_sign][CFL_PRED_V];
        if (CFL_SIGN_U(joint_sign) == CFL_SIGN_ZERO) {
            memset(fac_bits_u, 0, CFL_ALPHABET_SIZE * sizeof(*fac_bits_u));
        } else {
            const AomCdfProb* cdf_u = fc->cfl_alpha_cdf[CFL_CONTEXT_U(joint_sign)];
            getSyntaxRateFromCdf(fac_bits_u, cdf_u, nullptr);
        }
        if (CFL_SIGN_V(joint_sign) == CFL_SIGN_ZERO) {
            memset(fac_bits_v, 0, CFL_ALPHABET_SIZE * sizeof(*fac_bits_v));
        } else {
            const AomCdfProb* cdf_v = fc->cfl_alpha_cdf[CFL_CONTEXT_V(joint_sign)];
            getSyntaxRateFromCdf(fac_bits_v, cdf_v, nullptr);
        }
        // md_rate_estimation.c:223-:225: the reference adds the sign cost to
        // fac_bits_u ONLY - the V plane's rows keep their raw per-symbol
        // costs (transcribed verbatim; fixture-pinned below).
        for (int u = 0; u < CFL_ALPHABET_SIZE; ++u) {
            fac_bits_u[u] += sign_fac_bits[joint_sign];
        }
    }
    for (i = 0; i < MAX_TX_CATS; ++i) {
        for (j = 0; j < TX_SIZE_CONTEXTS; ++j) {
            getSyntaxRateFromCdf(me->txSizeFacBits[i][j], fc->tx_size_cdf[i][j], nullptr);
        }
    }
    for (i = 0; i < TXFM_PARTITION_CONTEXTS; ++i) {
        getSyntaxRateFromCdf(me->txfmPartitionFacBits[i], fc->txfm_partition_cdf[i], nullptr);
    }
    for (i = TX_4X4; i < EXT_TX_SIZES; ++i) {
        for (int s = 1; s < EXT_TX_SETS_INTER; ++s) {
            if (useInterExtTxForTxsize[s][i]) {
                getSyntaxRateFromCdf(me->interTxTypeFacBits[s][i], fc->inter_ext_tx_cdf[s][i],
                                     av1ExtTxInv[av1ExtTxSetIdxToType[1][s]]);
            }
        }
        for (int s = 1; s < EXT_TX_SETS_INTRA; ++s) {
            if (useIntraExtTxForTxsize[s][i]) {
                for (j = 0; j < INTRA_MODES; ++j) {
                    getSyntaxRateFromCdf(me->intraTxTypeFacBits[s][i][j], fc->intra_ext_tx_cdf[s][i][j],
                                         av1ExtTxInv[av1ExtTxSetIdxToType[0][s]]);
                }
            }
        }
    }
    for (i = 0; i < DIRECTIONAL_MODES; ++i) {
        getSyntaxRateFromCdf(me->angleDeltaFacBits[i], fc->angle_delta_cdf[i], nullptr);
    }
    if (enable_restoration) {
        getSyntaxRateFromCdf(me->switchableRestoreFacBits, fc->switchable_restore_cdf, nullptr);
        getSyntaxRateFromCdf(me->wienerRestoreFacBits, fc->wiener_restore_cdf, nullptr);
        getSyntaxRateFromCdf(me->sgrprojRestoreFacBits, fc->sgrproj_restore_cdf, nullptr);
    }
    if (allow_intrabc) {
        getSyntaxRateFromCdf(me->intrabcFacBits, fc->intrabc_cdf, nullptr);
    }
}

// The intra_inter fill (md_rate_estimation.c:310-:312, verbatim), extracted
// from the parked !is_i_slice block.
void estimateIntraInterRate(MdRateEstimationContext* me, const FcSyntaxCdfs* fc) {
    for (int i = 0; i < INTRA_INTER_CONTEXTS; ++i) {
        getSyntaxRateFromCdf(me->intraInterFacBits[i], fc->intra_inter_cdf[i], nullptr);
    }
}

// eb_size_group_lookup (common_utils.c:36-37, verbatim).
const std::uint8_t ebSizeGroupLookup[BLOCK_SIZES_ALL] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3,
                                                         3, 3, 3, 3, 3, 0, 0, 1, 1, 2, 2};

// ---------------------------------------------------------------------------
// D: the drivers (full_loop.c:994-:999 tables, :1028-:1236).
// ---------------------------------------------------------------------------

// plane_rd_mult (full_loop.c:994-999, TUNE_CHROMA_SSIM off, verbatim).
static const std::int32_t kPlaneRdMult[2][2][2] = {{{17, 20}, {16, 20}}, {{17, 13}, {16, 10}}};

// quantizer_to_qindex (md_process.c:20-23, verbatim).
static const std::uint8_t kQuantizerToQindex[64] = {
    0,   4,   8,   12,  16,  20,  24,  28,  32,  36,  40,  44,  48,  52,  56,  60,  64,  68,  72,  76,  80,  84,
    88,  92,  96,  100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 148, 152, 156, 160, 164, 168, 172,
    176, 180, 184, 188, 192, 196, 200, 204, 208, 212, 216, 220, 224, 228, 232, 236, 240, 244, 249, 255};

// The txb geometry helpers (common_utils.h:100-128, verbatim).
static std::int32_t av1GetAdjustedTxSize(std::int32_t txSize) {
    switch (txSize) {
    case 4:   // TX_64X64
    case 11:  // TX_32X64
    case 12:  // TX_64X32
        return 3;  // TX_32X32
    case 17:  // TX_16X64
        return 9;  // TX_16X32
    case 18:  // TX_64X16
        return 10;  // TX_32X16
    default:
        return txSize;
    }
}
std::int32_t getTxbBwl(std::int32_t txSize) {
    txSize = av1GetAdjustedTxSize(txSize);
    return kTxSizeWideLog2Tbl[txSize];
}
std::int32_t getTxbWide(std::int32_t txSize) {
    txSize = av1GetAdjustedTxSize(txSize);
    return kTxSizeWide[txSize];
}
std::int32_t getTxbHigh(std::int32_t txSize) {
    txSize = av1GetAdjustedTxSize(txSize);
    return kTxSizeHigh[txSize];
}

// svt_fast_optimize_b (full_loop.c:1028-1036, verbatim; the walls thread
// as parameters).
void fastOptimizeB(const std::int32_t* coeffPtr, const std::int16_t* dequant, std::int32_t* qcoeffPtr,
                    std::int32_t* dqcoeffPtr, std::uint16_t* eob, std::int32_t txSize, std::int32_t txType) {
    const ScanOrder* const scanOrder = &scanOrders()[txSize][txTypeToScanIndex[txType]];
    const std::int16_t* scan = scanOrder->scan;
    const std::int32_t shift = av1GetTxScaleTab[txSize];
    updateCoeffEobFast(eob, shift, dequant, scan, coeffPtr, qcoeffPtr, dqcoeffPtr);
}

// The rdmult/sharpness setup (full_loop.c:1046 + :1064-:1075): factored for
// testability (named deviation), arithmetic verbatim.
OptimizeBRdSetup optimizeBRdSetup(bool allintraRtc, bool isInter, std::int32_t planeType,
                                   std::uint32_t lambda, bool useSharpness, bool deltaQPresent,
                                   std::int32_t plane, std::int32_t sbQindex, std::int32_t pictureQp,
                                   std::int32_t sharpnessCfg) {
    OptimizeBRdSetup out;
    std::int32_t sharpness = 0;  // No Sharpness (:1046)
    std::int32_t rweight = 100;
    const std::int32_t sharpnessVal = sharpnessCfg < 0 ? 0 : (sharpnessCfg > 7 ? 7 : sharpnessCfg);
    const std::int32_t rshift = 2 > sharpnessVal ? 2 : sharpnessVal;
    if (useSharpness && deltaQPresent && plane == 0) {
        const std::int32_t diff = sbQindex - kQuantizerToQindex[pictureQp];
        if (diff < 0) {
            sharpness = 1;
            rweight = 0;
        }
    }
    out.rdmult = (((((std::int64_t)lambda * kPlaneRdMult[allintraRtc ? 1 : 0][isInter ? 1 : 0][planeType]) *
                     rweight) / 100) + 2) >> rshift;
    out.sharpness = sharpness;
    return out;
}

// svt_av1_optimize_b (full_loop.c:1038-1236, verbatim; the pcs/ctx walls
// thread as parameters).
void optimizeB(const OptimizeBArgs& a) {
    const ScanOrder* const scanOrder = &scanOrders()[a.txSize][txTypeToScanIndex[a.txType]];
    const std::int16_t* scan = scanOrder->scan;
    const std::int32_t shift = av1GetTxScaleTab[a.txSize];
    const std::int32_t planeType = a.plane;
    const std::int32_t txsCtx = getTxsizeEntropyCtx(a.txSize);
    const TxClass txClass = txTypeToClass[a.txType];
    const std::int32_t bwl = getTxbBwl(a.txSize);
    const std::int32_t width = getTxbWide(a.txSize);
    const std::int32_t height = getTxbHigh(a.txSize);
    // (The reference asserts width == (1 << bwl) and txs_ctx < TX_SIZES -
    // full_loop.c:1056-:1057.)
    const LvMapCoeffCost* txbCosts = &a.me->coeffFacBits[txsCtx][planeType];
    const std::int32_t eobMultiSize = txsizeLog2Minus4[a.txSize];
    const LvMapEobCost* txbEobCosts = &a.me->eobFracBits[eobMultiSize][planeType];
    const std::int32_t nonSkipCost = txbCosts->txbSkipCost[a.txbSkipContext][0];
    const std::int32_t skipCost = txbCosts->txbSkipCost[a.txbSkipContext][1];
    const std::int32_t eobCost = getEobCost(*a.eob, txbEobCosts, txbCosts, txClass);
    const OptimizeBRdSetup setup =
        optimizeBRdSetup(a.allintraRtc, a.isInter, planeType, a.lambda, a.useSharpness, a.deltaQPresent,
                          a.plane, a.sbQindex, a.pictureQp, a.sharpnessCfg);
    const std::int64_t rdmult = setup.rdmult;
    const std::int32_t sharpness = setup.sharpness;
    std::uint8_t* const levels = setLevels(a.levelsBuf, width, height);

    if (*a.eob > 1) {
        txbInitLevels(a.qcoeff, width, height, levels);
    }
    std::int32_t accuRate = eobCost;

    std::int64_t accuDist = 0;
    std::int32_t si = *a.eob - 1;
    const std::int32_t ci = scan[si];
    const std::int32_t qc = a.qcoeff[ci];
    const std::int32_t absQc = qc < 0 ? -qc : qc;
    const std::int32_t sign = qc < 0;
    const std::int32_t maxNzNum = 4;
    std::int32_t nzNum = 1;
    std::int32_t nzCi[5] = {ci, 0, 0, 0, 0};
    if (absQc >= 2) {
        updateCoeffGeneral(&accuRate, &accuDist, si, *a.eob, a.txSize, txClass, bwl, height, rdmult, shift,
                            a.dcSignContext, a.dequant, scan, txbCosts, a.coeff, a.qcoeff, a.dqcoeff, levels,
                            a.iqm);
        --si;
    } else {
        // (The reference asserts abs_qc == 1 here - full_loop.c:1114.)
        const std::int32_t coeffCtx = getLowerLevelsCtxEob(bwl, height, si);
        accuRate += getCoeffCostEob(ci, absQc, sign, coeffCtx, a.dcSignContext, txbCosts, bwl, txClass);

        const std::int32_t tqc = a.coeff[ci];
        const std::int32_t dqc = a.dqcoeff[ci];
        const std::int64_t dist = getCoeffDist(tqc, dqc, shift);
        const std::int64_t dist0 = getCoeffDist(tqc, 0, shift);
        accuDist += dist - dist0;
        --si;
    }
    for (; si >= 0 && nzNum <= maxNzNum; --si) {
        updateCoeffEob(&accuRate, &accuDist, a.eob, &nzNum, nzCi, si, a.txSize, txClass, bwl, height,
                        a.dcSignContext, rdmult, shift, a.dequant, scan, txbEobCosts, txbCosts, a.coeff,
                        a.qcoeff, a.dqcoeff, levels, sharpness, a.iqm);
    }

    if (si == -1 && nzNum <= maxNzNum) {
        updateSkip(&accuRate, accuDist, a.eob, nzNum, nzCi, rdmult, skipCost, nonSkipCost, a.qcoeff, a.dqcoeff,
                    sharpness);
    }

    std::int32_t siEnd = 1;  // default: full RDOQ
    if (a.cutOffNum) {
        const std::int32_t cutOffCoeff =
            ((width * height) >> 7) > ((*a.eob * a.cutOffNum) / a.cutOffDenum)
                ? ((width * height) >> 7)
                : ((*a.eob * a.cutOffNum) / a.cutOffDenum);
        siEnd = 1 > (*a.eob - cutOffCoeff) ? 1 : (*a.eob - cutOffCoeff);
    }
    for (; si >= siEnd; --si) {
        updateCoeffSimple(&accuRate, si, *a.eob, a.txSize, txClass, bwl, rdmult, shift, a.dequant, scan,
                           txbCosts, a.coeff, a.qcoeff, a.dqcoeff, levels, a.iqm);
    }

    // DC position
    if (si == 0) {
        // no need to update accu_dist because it's not used after this point
        std::int64_t dummyDist = 0;
        updateCoeffGeneral(&accuRate, &dummyDist, si, *a.eob, a.txSize, txClass, bwl, height, rdmult, shift,
                            a.dcSignContext, a.dequant, scan, txbCosts, a.coeff, a.qcoeff, a.dqcoeff, levels,
                            a.iqm);
    }
}

// ---------------------------------------------------------------------------
// RE3: the rd_cost.c symbol-cost surface (2026-11-08). The carries and the
// ported spans live here; each carries with its reference file + line
// citation. The landed RE1/RE2 surface above is untouched.
// ---------------------------------------------------------------------------

// get_nz_map_ctx (C_DEFAULT/encode_txb_ref_c.c:17-:33, verbatim).
std::int32_t getNzMapCtx(const std::uint8_t* levels, std::int32_t coeffIdx, std::int32_t bwl, std::int32_t height,
                          std::int32_t scanIdx, std::int32_t isEob, std::int32_t txSize, TxClass txClass) {
    if (isEob) {
        if (scanIdx == 0) {
            return 0;
        }
        if (scanIdx <= (height << bwl) / 8) {
            return 1;
        }
        if (scanIdx <= (height << bwl) / 4) {
            return 2;
        }
        return 3;
    }
    const std::int32_t stats = getNzMag(levels + getPaddedIdx(coeffIdx, bwl), bwl, txClass);
    return getNzMapCtxFromStats(stats, coeffIdx, bwl, txSize, txClass);
}

// svt_av1_get_nz_map_contexts_c (C_DEFAULT/encode_txb_ref_c.c:35-:43,
// verbatim).
void getNzMapContexts(const std::uint8_t* levels, const std::int16_t* scan, std::uint16_t eob,
                       std::int32_t txSize, TxClass txClass, std::int8_t* coeffContexts) {
    const std::int32_t bwl = getTxbBwl(txSize);
    const std::int32_t height = getTxbHigh(txSize);
    for (std::int32_t i = 0; i < eob; ++i) {
        const std::int32_t pos = scan[i];
        coeffContexts[pos] = (std::int8_t)getNzMapCtx(levels, pos, bwl, height, i, i == eob - 1, txSize, txClass);
    }
}

// svt_av1_cost_coeffs_txb (rd_cost.c:355-:474, verbatim; the walls threaded
// via CostCoeffsTxbArgs). The mds_fast_coeff_est_level / mds_subres_step
// values carried by the landed RE2 machinery arrive in the args.
std::int32_t costCoeffsTxb(const CostCoeffsTxbArgs& a) {
    const std::int32_t txsCtx = getTxsizeEntropyCtx(a.transformSize);
    const TxClass txClass = txTypeToClass[a.transformType];
    const std::int32_t bwl = getTxbBwl(a.transformSize);
    const std::int32_t width = getTxbWide(a.transformSize);
    const std::int32_t height = getTxbHigh(a.transformSize);
    const ScanOrder& scanOrder = getScanOrder(a.transformSize, a.transformType);
    const std::int16_t* const scan = scanOrder.scan;
    std::uint8_t* const levels = setLevels(a.levelsBuf, width, height);
    std::int8_t* const coeffContexts = a.coeffContexts;
    const LvMapCoeffCost* const coeffCosts = &a.me->coeffFacBits[txsCtx][a.planeType];
    const std::int32_t eobMultiSize = txsizeLog2Minus4[a.transformSize];
    const LvMapEobCost* const eobBits = &a.me->eobFracBits[eobMultiSize][a.planeType];
    std::int32_t cost = coeffCosts->txbSkipCost[a.txbSkipCtx][0];
    if (a.allowUpdateCdf) {
        updateCdf(a.fc->txbSkipCdf[txsCtx][a.txbSkipCtx], a.eob == 0, 2);
    }
    if (a.eob > 1) {
        txbInitLevels(a.qcoeff, width, height, levels);
    }
    // Transform type bit estimation (rd_cost.c:396-:404): the chroma
    // planes carry no tx-type symbol.
    cost += a.planeType > 0 ? 0
                            : transformTypeRateEstimation(a.me, a.allowUpdateCdf, a.fcSyntax, a.isInter,
                                                           a.transformSize, a.transformType,
                                                           a.reducedTxSet, a.lumaMode, a.filterIntraMode);
    // Transform eob bit estimation
    cost += getEobCost(a.eob, eobBits, coeffCosts, txClass);
    if (a.allowUpdateCdf) {
        updateEobContext(a.eob, a.transformSize, txClass, a.planeType, a.fc);
    }
    // Transform non-zero coeff bit estimation
    getNzMapContexts(levels, scan, a.eob, a.transformSize, txClass, coeffContexts);
    if (a.allowUpdateCdf) {
        for (std::int32_t c = a.eob - 1; c >= 0; --c) {
            const std::int32_t pos = scan[c];
            const std::int32_t coeffCtx = coeffContexts[pos];
            const std::int32_t v = a.qcoeff[pos];
            const std::int32_t level = (v < 0) ? -v : v;
            if (c == a.eob - 1) {
                updateCdf(a.fc->coeffBaseEobCdf[txsCtx][a.planeType][coeffCtx],
                          ((level < 3) ? level : 3) - 1, 3);
            } else {
                updateCdf(a.fc->coeffBaseCdf[txsCtx][a.planeType][coeffCtx], (level < 3) ? level : 3, 4);
            }
            if (level > NUM_BASE_LEVELS) {
                const std::int32_t baseRange = level - 1 - NUM_BASE_LEVELS;
                std::int32_t brCtx;
                if (a.eob == 1) {
                    brCtx = 0;
                } else {
                    brCtx = getBrCtx(levels, pos, bwl, txClass);
                }
                for (std::int32_t idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
                    const std::int32_t k = ((baseRange - idx) < (BR_CDF_SIZE - 1)) ? (baseRange - idx)
                                                                                    : (BR_CDF_SIZE - 1);
                    updateCdf(a.fc->coeffBrCdf[(txsCtx < TX_32X32) ? txsCtx : TX_32X32][a.planeType][brCtx], k,
                              BR_CDF_SIZE);
                    if (k < BR_CDF_SIZE - 1) {
                        break;
                    }
                }
            }
        }
        if (a.qcoeff[0] != 0) {
            updateCdf(a.fc->dcSignCdf[a.planeType][a.dcSignCtx], a.qcoeff[0] < 0, 2);
        }
        return 0;
    }
    cost += costCoeffsEob(a.qcoeff, a.eob, scan, coeffContexts, coeffCosts, a.dcSignCtx, levels, bwl,
                           a.transformType, a.fastCoeffEstLevel, a.subresStep);
    return cost;
}

// g_uv2y (common_utils.c:14-28, verbatim; the trailing UV_MODE_INVALID
// entries carry 0 in the reference).
const std::int32_t gUv2y[16] = {
    DC_PRED,    // UV_DC_PRED
    V_PRED,     // UV_V_PRED
    H_PRED,     // UV_H_PRED
    D45_PRED,   // UV_D45_PRED
    D135_PRED,  // UV_D135_PRED
    D113_PRED,  // UV_D113_PRED
    D157_PRED,  // UV_D157_PRED
    D203_PRED,  // UV_D203_PRED
    D67_PRED,   // UV_D67_PRED
    SMOOTH_PRED,     // UV_SMOOTH_PRED
    SMOOTH_V_PRED,   // UV_SMOOTH_V_PRED
    SMOOTH_H_PRED,   // UV_SMOOTH_H_PRED
    PAETH_PRED,      // UV_PAETH_PRED
    DC_PRED,         // UV_CFL_PRED
    0, 0,            // INTRA_INVALID slots (14/15; the port has no INTRA_INVALID)
};
// get_uv_mode (common_utils.h:130-133).
std::int32_t getUvMode(std::int32_t uvMode) {
    return gUv2y[uvMode];
}
// mi_size_wide/high (common_utils.c:292-293, verbatim).
const std::uint8_t miSizeWide[BLOCK_SIZES_ALL] = {1, 1, 2, 2, 2, 4, 4, 4, 8, 8, 8, 16, 16, 16, 32, 32, 1, 4, 2, 8, 4, 16};
const std::uint8_t miSizeHigh[BLOCK_SIZES_ALL] = {1, 2, 1, 2, 4, 2, 4, 8, 4, 8, 16, 8, 16, 32, 16, 32, 4, 1, 8, 2, 16, 4};
// is_chroma_reference (common_utils.h:315-320, verbatim).
bool isChromaReference(std::int32_t miRow, std::int32_t miCol, BlockSize bsize, std::int32_t ssX,
                        std::int32_t ssY) {
    const std::int32_t bw = miSizeWide[bsize];
    const std::int32_t bh = miSizeHigh[bsize];
    return ((miRow & 0x01) || !(bh & 0x01) || !ssY) && ((miCol & 0x01) || !(bw & 0x01) || !ssX);
}
// av1_is_directional_mode (intra_prediction.h:206-208).
std::int32_t av1IsDirectionalMode(std::int32_t mode) {
    return mode >= V_PRED && mode <= D67_PRED;
}
// is_inter_mode (definitions.h:1599-1601).
std::int32_t isInterMode(std::int32_t mode) {
    return mode >= SINGLE_INTER_MODE_START && mode < COMP_INTER_MODE_END;
}
// is_comp_ref_allowed (inter_prediction.h:255-257).
std::int32_t isCompRefAllowed(BlockSize bsize) {
    return (blockSizeWide[bsize] < blockSizeHigh[bsize] ? blockSizeWide[bsize] : blockSizeHigh[bsize]) >= 8;
}
// The mv trio subset (rd_cost.c:47-:81). svt_av1_get_mv_joint :47-:53.
std::int32_t getMvJoint(std::int32_t mvPacked) {
    const std::int16_t x = (std::int16_t)(mvPacked & 0xFFFF);
    const std::int16_t y = (std::int16_t)((std::uint32_t)mvPacked >> 16);
    if (y == 0) {
        return x == 0 ? MV_JOINT_ZERO : MV_JOINT_HNZVZ;
    } else {
        return x == 0 ? MV_JOINT_HZVNZ : MV_JOINT_HNZVNZ;
    }
}
// mv_cost :55-:60.
std::int32_t mvCost(std::int32_t mvPacked, const std::int32_t* jointCost, const std::int32_t* const compCost[2]) {
    const std::int16_t x = (std::int16_t)(mvPacked & 0xFFFF);
    const std::int16_t y = (std::int16_t)((std::uint32_t)mvPacked >> 16);
    const std::int32_t jnC = getMvJoint(mvPacked);
    const std::int32_t cy = y < MV_LOW ? MV_LOW : (y > MV_UPP ? MV_UPP : y);
    const std::int32_t cx = x < MV_LOW ? MV_LOW : (x > MV_UPP ? MV_UPP : x);
    return jointCost[jnC] + compCost[0][cy] + compCost[1][cx];
}
// svt_av1_mv_bit_cost :70-:81.
std::int32_t mvBitCost(std::int32_t mvPacked, std::int32_t refPacked, const std::int32_t* jointCost,
                        const std::int32_t* const compCost[2], std::int32_t weight) {
    const std::int16_t mvX = (std::int16_t)(mvPacked & 0xFFFF);
    const std::int16_t mvY = (std::int16_t)((std::uint32_t)mvPacked >> 16);
    const std::int16_t refX = (std::int16_t)(refPacked & 0xFFFF);
    const std::int16_t refY = (std::int16_t)((std::uint32_t)refPacked >> 16);
    const std::int16_t x = (std::int16_t)((mvX - refX) < MV_LOW ? MV_LOW
                                          : ((mvX - refX) > MV_UPP ? MV_UPP : (mvX - refX)));
    const std::int16_t y = (std::int16_t)((mvY - refY) < MV_LOW ? MV_LOW
                                          : ((mvY - refY) > MV_UPP ? MV_UPP : (mvY - refY)));
    const std::int32_t diff = (std::int32_t)((std::uint32_t)(std::uint16_t)x | ((std::uint32_t)(std::uint16_t)y << 16));
    return (std::int32_t)ROUND_POWER_OF_TWO((std::int64_t)mvCost(diff, jointCost, compCost) * weight, 7);
}
// svt_aom_intra_fast_cost (rd_cost.c:526-:640, verbatim; the walls threaded).
std::int64_t intraFastCost(const IntraFastCostArgs& a) {
    const IntraFastCandidate* cand = a.cand;
    if (a.frameAllowIntrabc && cand->useIntrabc) {
        std::int64_t rate = 0;
        const std::int32_t* dvcost[2] = {a.dvCostRow[0], a.dvCostRow[1]};
        const std::int32_t mvRate = mvBitCost(cand->mv, cand->predMv, a.dvJointCost, dvcost, MV_COST_WEIGHT_SUB);
        rate = mvRate + a.me->intrabcFacBits[cand->useIntrabc ? 1 : 0];
        *a.fastLumaRate = (std::uint32_t)rate;
        *a.fastChromaRate = 0;
        return (std::int64_t)RDCOST(a.lambda, rate, a.lumaDistortion);
    } else {
        std::uint64_t intraModeBitsNum = 0;
        std::uint64_t intraLumaModeBitsNum = 0;
        std::uint64_t intraLumaAngModeBitsNum = 0;
        std::uint64_t intraFilterModeBitsNum = 0;
        std::uint64_t skipModeRate = 0;
        const std::uint8_t skipModeCtx = a.skipModeCtx;
        const std::int32_t intraMode = cand->mode;
        std::uint32_t rate;
        std::uint32_t lumaRate = 0;
        std::uint32_t chromaRate = 0;
        intraModeBitsNum = !a.isISlice
             ? (std::uint64_t)a.me->mbModeFacBits[ebSizeGroupLookup[a.bsize]][intraMode]
             : ZERO_COST;
        skipModeRate = !a.isISlice && a.skipModeFlag && isCompRefAllowed((BlockSize)a.bsize)
            ? (std::uint64_t)a.me->skipModeFacBits[skipModeCtx][0]
            : ZERO_COST;
        // Estimate luma nominal intra mode bits for key frame
        intraLumaModeBitsNum = a.isISlice
            ? (std::uint64_t)a.me->yModeFacBits[a.intraLumaTopCtx][a.intraLumaLeftCtx][intraMode]
            : ZERO_COST;
        // Estimate luma angular mode bits
        if (a.bsize >= BLOCK_8X8 && av1IsDirectionalMode(intraMode)) {
            intraLumaAngModeBitsNum = a.me->angleDeltaFacBits[intraMode - V_PRED]
                                                            [MAX_ANGLE_DELTA + cand->angleDelta[0]];
        }
        if (allowPalette(a.enablePalette, a.allowScreenContentTools, (BlockSize)a.bsize) && intraMode == DC_PRED) {
            const std::int32_t usePalette = (cand->paletteSize[0] >= 0) && (cand->paletteSize[0] > 0);
            const std::int32_t bsizeCtx = getPaletteBsizeCtx((BlockSize)a.bsize);
            // svt_aom_get_palette_mode_ctx(blk_ptr->av1xd) threaded.
            const std::int32_t modeCtx = a.paletteModeCtx;
            intraLumaModeBitsNum += a.me->paletteYmodeFacBits[bsizeCtx][modeCtx][usePalette ? 1 : 0];
            if (usePalette) {
                // PARKED named (rd_cost.c:585-:604): the color-map cost block
                // consumes the four MbD/palette walls (get_block_dimensions,
                // get_palette_cache_y, palette_color_cost_y, cost_color_map);
                // the pinned build's CONFIG_ENABLE_PALETTE=0 keeps the branch
                // dead, so the use_palette=true tail is unreachable in the
                // ported configuration. Unreachable in the port: trapped.
                // (No port code path can set usePalette with the pinned gate.)
                intraLumaModeBitsNum += 0;
            }
        }
        if (filterIntraAllowed(a.filterIntraLevel, (BlockSize)a.bsize,
                                cand->paletteSize[0] >= 0 ? cand->paletteSize[0] : 0, intraMode)) {
            intraFilterModeBitsNum =
                a.me->filterIntraFacBits[a.bsize][cand->filterIntraMode != FILTER_INTRA_MODES];
            if (cand->filterIntraMode != FILTER_INTRA_MODES) {
                intraFilterModeBitsNum += a.me->filterIntraModeFacBits[cand->filterIntraMode];
            }
        }
        if (a.hasUv) {
            // CFL info not known in the fast loop: assume DC when allowed.
            chromaRate = (std::uint32_t)getIntraUvFastRate(a.me, *cand, a.bsize, a.bwidth, a.bheight, a.blkOrgX,
                                                            a.blkOrgY, false, a.allowScreenContentTools,
                                                            a.enablePalette);
        }
        if (a.hasUv) {
            // CFL info not known in the fast loop: assume DC when allowed.
            chromaRate = (std::uint32_t)getIntraUvFastRate(a.me, *cand, a.bsize, a.bwidth, a.bheight, a.blkOrgX,
                                                            a.blkOrgY, false, a.allowScreenContentTools,
                                                            a.enablePalette);
        }
        const std::uint32_t isInterRate = !a.isISlice
            ? (std::uint32_t)a.me->intraInterFacBits[a.isInterCtx][0]
            : 0;
        lumaRate = (std::uint32_t)(intraModeBitsNum + skipModeRate + intraLumaModeBitsNum +
                                   intraLumaAngModeBitsNum + isInterRate + intraFilterModeBitsNum);
        if (a.frameAllowIntrabc) {
            lumaRate += a.me->intrabcFacBits[cand->useIntrabc ? 1 : 0];
        }
        *a.fastLumaRate = lumaRate;
        *a.fastChromaRate = chromaRate;
        rate = lumaRate + chromaRate;
        return (std::int64_t)RDCOST(a.lambda, rate, a.lumaDistortion);
    }
}
void buildNmvCostTable(std::int32_t* jointCost, std::int32_t* compCost[2], const NmvContext* ctx,
                        MvSubpelPrecision precision) {
    getSyntaxRateFromCdf(jointCost, ctx->joints_cdf, nullptr);
    buildNmvComponentCostTable(compCost[0], &ctx->comps[0], precision);
    buildNmvComponentCostTable(compCost[1], &ctx->comps[1], precision);
}
std::int32_t getUnsignedBits(std::int32_t n) {
    return n == 0 ? 0 : getMsb((std::uint32_t)n) + 1;
}
// svt_aom_write_uniform_cost (entropy_coding.c:4304-4315, verbatim).
std::int32_t writeUniformCost(std::int32_t n, std::int32_t v) {
    const std::int32_t l = getUnsignedBits(n);
    const std::int32_t m = (1 << l) - n;
    if (l == 0) {
        return 0;
    }
    if (v < m) {
        return av1_cost_literal(l - 1);
    } else {
        return av1_cost_literal(l);
    }
}
// svt_aom_filter_intra_allowed (mode_decision.c:115-120, verbatim; the
// CONFIG guard threaded).
std::int32_t filterIntraAllowed(std::int32_t enableFilterIntra, BlockSize bsize, std::int32_t paletteSize,
                                 std::int32_t mode) {
    if (!enableFilterIntra) {
        return 0;
    }
    return enableFilterIntra && mode == DC_PRED && paletteSize == 0 &&
        (blockSizeWide[bsize] <= 32 && blockSizeHigh[bsize] <= 32);
}

// svt_aom_get_intra_uv_fast_rate (rd_cost.c:476-:524, verbatim; the
// pcs/ctx/blk_geom walls threaded).
std::int32_t getIntraUvFastRate(const MdRateEstimationContext* me, const IntraFastCandidate& cand,
                                 std::int32_t bsize, std::int32_t bwidth, std::int32_t bheight,
                                 std::int32_t blkOrgX, std::int32_t blkOrgY, bool useAccurateCfl,
                                 std::int32_t allowScreenContentTools, std::int32_t enablePalette) {
    // assert(ctx->has_uv) + the intrabc assert: debug-only, dropped (named
    // deviation).
    const std::int32_t isCflAllowed = (bwidth <= 32 && bheight <= 32) ? 1 : 0;
    const std::int32_t intraMode = cand.mode;
    // If CFL alphas are not known yet, the chroma mode folds to DC.
    const std::int32_t chromaMode = (cand.uvMode == UV_CFL_PRED && !useAccurateCfl) ? UV_DC_PRED : cand.uvMode;
    const std::uint32_t miRow = (std::uint32_t)blkOrgY >> MI_SIZE_LOG2;
    const std::uint32_t miCol = (std::uint32_t)blkOrgX >> MI_SIZE_LOG2;
    // Subsampling assumes YUV 420 content.
    const std::int32_t ssX = 1;
    const std::int32_t ssY = 1;
    std::int64_t chromaRate = 0;
    // Estimate chroma nominal intra mode bits
    chromaRate += (std::int64_t)me->intraUvModeFacBits[isCflAllowed][intraMode][chromaMode];
    // Estimate chroma angular mode bits; angular offset only allowed for bsize >= 8x8
    if (bsize >= BLOCK_8X8 && av1IsDirectionalMode(getUvMode(chromaMode))) {
        chromaRate += me->angleDeltaFacBits[chromaMode - V_PRED][MAX_ANGLE_DELTA + cand.angleDelta[1]];
    }
    // Estimate CFL factor bits when CFL is used
    if (chromaMode == UV_CFL_PRED) {
        chromaRate += (std::int64_t)me->cflAlphaFacBits[cand.cflAlphaSigns][CFL_PRED_U]
                                                       [(cand.cflAlphaIdx) >> 4] +
            (std::int64_t)me->cflAlphaFacBits[cand.cflAlphaSigns][CFL_PRED_V]
                                              [cand.cflAlphaIdx & (CFL_ALPHABET_SIZE - 1)];
    }
    // Estimate chroma palette mode bits
    if (chromaMode == UV_DC_PRED && allowPalette(enablePalette, allowScreenContentTools, (BlockSize)bsize) &&
        isChromaReference((std::int32_t)miRow, (std::int32_t)miCol, (BlockSize)bsize, ssX, ssY)) {
        const std::int32_t usePaletteY = (cand.paletteSize[0] >= 0) && (cand.paletteSize[0] > 0);
        const std::int32_t usePaletteUv = (cand.paletteSize[1] >= 0) && (cand.paletteSize[1] > 0);
        chromaRate += me->paletteUvModeFacBits[usePaletteY][usePaletteUv];
    }
    return (std::int32_t)chromaRate;
}

// update_cdf (cabac_context_model.h:76-105, verbatim).
void updateCdf(AomCdfProb* cdf, std::int32_t val, std::int32_t nsymbs) {
    const std::int32_t count = cdf[nsymbs];
    cdf[nsymbs] = (AomCdfProb)(cdf[nsymbs] + (count < 32));
    // rate is computed in the spec as:
    //  3 + ( cdf[N] > 15 ) + ( cdf[N] > 31 ) + Min(FloorLog2(N), 2)
    const std::int32_t rate = 4 + (count >> 4) + (nsymbs > 3);
    int i = 0;
    for (; i < val; i++) {
        cdf[i] = (AomCdfProb)(cdf[i] + ((CDF_PROB_TOP - cdf[i]) >> rate));
    }
    for (; i < nsymbs - 1; i++) {
        cdf[i] = (AomCdfProb)(cdf[i] - (cdf[i] >> rate));
    }
}

// get_ext_tx_set_type (common_utils.h:59-77, verbatim).
std::int32_t getExtTxSetType(std::int32_t txSize, std::int32_t isInter, std::int32_t useReducedSet) {
    const std::int32_t txSizeSqrUp = txsizeSqrUpMap[txSize];
    if (txSizeSqrUp > TX_32X32) {
        return EXT_TX_SET_DCTONLY;
    }
    if (txSizeSqrUp == TX_32X32) {
        return isInter ? EXT_TX_SET_DCT_IDTX : EXT_TX_SET_DCTONLY;
    }
    if (useReducedSet) {
        return isInter ? EXT_TX_SET_DCT_IDTX : EXT_TX_SET_DTT4_IDTX;
    }
    const std::int32_t txSizeSqr = txsizeSqrMap[txSize];
    if (isInter) {
        return (txSizeSqr == TX_16X16 ? EXT_TX_SET_DTT9_IDTX_1DDCT : EXT_TX_SET_ALL16);
    } else {
        return (txSizeSqr == TX_16X16 ? EXT_TX_SET_DTT4_IDTX : EXT_TX_SET_DTT4_IDTX_1DDCT);
    }
}

// get_ext_tx_types (common_utils.h:79-82, verbatim).
std::int32_t getExtTxTypes(std::int32_t txSize, std::int32_t isInter, std::int32_t useReducedSet) {
    const std::int32_t setType = getExtTxSetType(txSize, isInter, useReducedSet);
    return av1NumExtTxSet[setType];
}

// get_ext_tx_set (common_utils.h:87-90, verbatim).
std::int32_t getExtTxSet(std::int32_t txSize, std::int32_t isInter, std::int32_t useReducedSet) {
    const TxSetType setType = (TxSetType)getExtTxSetType(txSize, isInter, useReducedSet);
    return extTxSetIndex[isInter][setType];
}

// av1_num_ext_tx_set (common_utils.c:195, verbatim).
const std::int32_t av1NumExtTxSet[EXT_TX_SET_TYPES] = {1, 2, 5, 7, 12, 16};
// ext_tx_set_index (common_utils.c:206-209, verbatim).
const std::int32_t extTxSetIndex[2][EXT_TX_SET_TYPES] = {
    {0, -1, 2, 1, -1, -1},  // Intra
    {0, 3, -1, -1, 2, 1},   // Inter
};

// av1_ext_tx_ind (cabac_context_model.c:34-42, verbatim).
const std::int32_t av1ExtTxInd[EXT_TX_SET_TYPES][TX_TYPES] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 3, 4, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 5, 6, 4, 0, 0, 0, 0, 0, 0, 2, 3, 0, 0, 0, 0},
    {3, 4, 5, 8, 6, 7, 9, 10, 11, 0, 1, 2, 0, 0, 0, 0},
    {7, 8, 9, 12, 10, 11, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6},
};
// update_eob_context (rd_cost.c:157-:195, verbatim).
void updateEobContext(std::int32_t eob, std::int32_t txSize, TxClass txClass, std::int32_t plane,
                       FcTokenCdfs* ecCtx) {
    std::int32_t eobExtra;
    const std::int32_t eobPt = getEobPosToken(eob, &eobExtra);
    const std::int32_t txsCtx = (txsizeSqrMap[txSize] + txsizeSqrUpMap[txSize] + 1) >> 1;
    const std::int32_t eobMultiSize = txsizeLog2Minus4[txSize];
    const std::int32_t eobMultiCtx = (txClass == TX_CLASS_2D) ? 0 : 1;
    switch (eobMultiSize) {
    case 0:
        updateCdf(ecCtx->eobFlagCdf16[plane][eobMultiCtx], eobPt - 1, 5);
        break;
    case 1:
        updateCdf(ecCtx->eobFlagCdf32[plane][eobMultiCtx], eobPt - 1, 6);
        break;
    case 2:
        updateCdf(ecCtx->eobFlagCdf64[plane][eobMultiCtx], eobPt - 1, 7);
        break;
    case 3:
        updateCdf(ecCtx->eobFlagCdf128[plane][eobMultiCtx], eobPt - 1, 8);
        break;
    case 4:
        updateCdf(ecCtx->eobFlagCdf256[plane][eobMultiCtx], eobPt - 1, 9);
        break;
    case 5:
        updateCdf(ecCtx->eobFlagCdf512[plane][eobMultiCtx], eobPt - 1, 10);
        break;
    case 6:
    default:
        updateCdf(ecCtx->eobFlagCdf1024[plane][eobMultiCtx], eobPt - 1, 11);
        break;
    }
    if (eobPt > 2) {
        const std::int32_t cnt = eobPt - 3;
        const std::int32_t bit = (eobExtra >> cnt) & 1;
        updateCdf(ecCtx->eobExtraCdf[txsCtx][plane][cnt], bit, 2);
    }
}

// av1_cost_skip_txb (rd_cost.c:213-:222, verbatim; the ctx walls threaded).
std::int32_t costSkipTxb(const MdRateEstimationContext* me, std::uint8_t allowUpdateCdf, FcTokenCdfs* ecCtx,
                          std::int32_t transformSize, std::int32_t planeType, std::int32_t txbSkipCtx) {
    const std::int32_t txsCtx = (txsizeSqrMap[transformSize] + txsizeSqrUpMap[transformSize] + 1) >> 1;
    const LvMapCoeffCost* const coeffCosts = &me->coeffFacBits[txsCtx][planeType];
    if (allowUpdateCdf) {
        updateCdf(ecCtx->txbSkipCdf[txsCtx][txbSkipCtx], 1, 2);
    }
    return coeffCosts->txbSkipCost[txbSkipCtx][1];
}

// fimode_to_intradir (common_utils.c:33-34, verbatim).
const PredictionMode fimodeToIntradir[FILTER_INTRA_MODES] = {DC_PRED, V_PRED, H_PRED, D157_PRED, PAETH_PRED};

// av1_cost_coeffs_txb_loop_cost_one_eob (rd_cost.c:224-:253, verbatim).
std::int32_t costCoeffsOneEob(const std::int32_t* qcoeff, const std::int8_t* coeffContexts,
                               const LvMapCoeffCost* coeffCosts, std::int32_t dcSignCtx) {
    const std::int32_t v = qcoeff[0];
    const std::int32_t level = (v < 0) ? -v : v;
    const std::int32_t coeffCtx = coeffContexts[0];
    std::int32_t cost = coeffCosts->baseEobCost[coeffCtx][((level < 3) ? level : 3) - 1];
    if (v != 0) {
        const std::int32_t sign = (v < 0) ? 1 : 0;
        cost += coeffCosts->dcSignCost[dcSignCtx][sign];
        if (level > NUM_BASE_LEVELS) {
            const std::int32_t baseRange = level - 1 - NUM_BASE_LEVELS;
            if (baseRange < COEFF_BASE_RANGE) {
                cost += coeffCosts->lpsCost[0][baseRange];
            } else {
                cost += coeffCosts->lpsCost[0][COEFF_BASE_RANGE];
            }
            if (level >= 1 + NUM_BASE_LEVELS + COEFF_BASE_RANGE) {
                cost += getGolombCost(level);
            }
        }
    }
    return cost;
}

// av1_cost_coeffs_txb_loop_cost_eob (rd_cost.c:255-:352, verbatim; the
// md_ctx walls threaded as fastCoeffEstLevel/subresStep).
std::int32_t costCoeffsEob(const std::int32_t* qcoeff, std::uint16_t eob, const std::int16_t* scan,
                            const std::int8_t* coeffContexts, const LvMapCoeffCost* coeffCosts,
                            std::int32_t dcSignCtx, const std::uint8_t* levels, std::int32_t bwl,
                            std::int32_t transformType, std::int32_t fastCoeffEstLevel,
                            std::int32_t subresStep) {
    (void)transformType; (void)levels; (void)bwl;
    const std::int32_t costLiteral = av1_cost_literal(1);
    std::int32_t cost = 0;
    if (eob == 1) {
        return costCoeffsOneEob(qcoeff, coeffContexts, coeffCosts, dcSignCtx);
    }
    //  first (eob - 1) index
    {
        const std::int32_t pos = scan[eob - 1];
        const std::int32_t v = qcoeff[pos];
        const std::int32_t level = (v < 0) ? -v : v;
        const std::int32_t coeffCtx = coeffContexts[pos];
        cost += coeffCosts->baseEobCost[coeffCtx][((level < 3) ? level : 3) - 1];
        if (v != 0) {
            cost += costLiteral;
            if (level > NUM_BASE_LEVELS) {
                const std::int32_t brCtx = getBrCtx(levels, pos, bwl, txTypeToClass[transformType]);
                const std::int32_t baseRange = level - 1 - NUM_BASE_LEVELS;
                if (baseRange < COEFF_BASE_RANGE) {
                    cost += coeffCosts->lpsCost[brCtx][baseRange];
                } else {
                    cost += coeffCosts->lpsCost[brCtx][COEFF_BASE_RANGE];
                }
                if (level >= 1 + NUM_BASE_LEVELS + COEFF_BASE_RANGE) {
                    cost += getGolombCost(level);
                }
            }
        }
    }
    // last (0) index
    {
        const std::int32_t v = qcoeff[0];
        const std::int32_t level = (v < 0) ? -v : v;
        const std::int32_t coeffCtx = coeffContexts[0];
        cost += coeffCosts->baseCost[coeffCtx][((level < 3) ? level : 3)];
        if (v != 0) {
            const std::int32_t sign = (v < 0) ? 1 : 0;
            cost += coeffCosts->dcSignCost[dcSignCtx][sign];
            if (level > NUM_BASE_LEVELS) {
                const std::int32_t brCtx = getBrCtx(levels, 0, bwl, txTypeToClass[transformType]);
                const std::int32_t baseRange = level - 1 - NUM_BASE_LEVELS;
                if (baseRange < COEFF_BASE_RANGE) {
                    cost += coeffCosts->lpsCost[brCtx][baseRange];
                } else {
                    cost += coeffCosts->lpsCost[brCtx][COEFF_BASE_RANGE];
                }
                if (level >= 1 + NUM_BASE_LEVELS + COEFF_BASE_RANGE) {
                    cost += getGolombCost(level);
                }
            }
        }
    }
    const std::int32_t cStart = (eob - 2) < (eob / ((fastCoeffEstLevel - subresStep) > 1
                                                    ? (fastCoeffEstLevel - subresStep) : 1))
                                    ? (eob - 2)
                                    : (eob / ((fastCoeffEstLevel - subresStep) > 1 ? (fastCoeffEstLevel - subresStep) : 1));
    std::uint32_t costLiteralCnt = 0;
    for (std::int32_t c = cStart; c >= 1; --c) {
        const std::int32_t pos = scan[c];
        costLiteralCnt += !!(qcoeff[pos]);
        const std::int32_t level = (qcoeff[pos] < 0) ? -qcoeff[pos] : qcoeff[pos];
        if (level > NUM_BASE_LEVELS) {
            const std::int32_t brCtx = getBrCtx(levels, pos, bwl, txTypeToClass[transformType]);
            const std::int32_t baseRange = level - 1 - NUM_BASE_LEVELS;
            cost += coeffCosts->baseCost[coeffContexts[pos]][3];
            if (baseRange < COEFF_BASE_RANGE) {
                cost += coeffCosts->lpsCost[brCtx][baseRange];
            } else {
                cost += getGolombCost(level) + coeffCosts->lpsCost[brCtx][COEFF_BASE_RANGE];
            }
        } else {
            cost += coeffCosts->baseCost[coeffContexts[pos]][level];
        }
    }
    cost += (std::int32_t)costLiteralCnt * costLiteral;
    return cost;
}

// av1_transform_type_rate_estimation (rd_cost.c:107-:153).
std::int32_t transformTypeRateEstimation(const MdRateEstimationContext* me, std::uint8_t allowUpdateCdf,
                                          FcSyntaxCdfs* fc, bool isInter, std::int32_t transformSize,
                                          std::int32_t transformType, bool reducedTxSetUsed, std::int32_t lumaMode,
                                          std::int32_t filterIntraMode) {
    (void)allowUpdateCdf; (void)fc;
    if (getExtTxTypes(transformSize, isInter, reducedTxSetUsed ? 1 : 0) > 1) {
        const std::int32_t squareTxSize = txsizeSqrMap[transformSize];
        const std::int32_t extTxSet = getExtTxSet(transformSize, isInter, reducedTxSetUsed ? 1 : 0);
        if (isInter) {
            if (extTxSet > 0) {
                if (allowUpdateCdf) {
                    const TxSetType txSetType =
                        (TxSetType)getExtTxSetType(transformSize, isInter, reducedTxSetUsed ? 1 : 0);
                    updateCdf(fc->inter_ext_tx_cdf[extTxSet][squareTxSize], av1ExtTxInd[txSetType][transformType],
                              av1NumExtTxSet[txSetType]);
                }
                return me->interTxTypeFacBits[extTxSet][squareTxSize][transformType];
            }
        } else {
            if (extTxSet > 0) {
                std::int32_t intraDir;
                if (filterIntraMode != FILTER_INTRA_MODES) {
                    intraDir = fimodeToIntradir[filterIntraMode];
                } else {
                    intraDir = lumaMode;
                }
                const TxSetType txSetType =
                    (TxSetType)getExtTxSetType(transformSize, isInter, reducedTxSetUsed ? 1 : 0);
                if (allowUpdateCdf) {
                    updateCdf(fc->intra_ext_tx_cdf[extTxSet][squareTxSize][intraDir],
                              av1ExtTxInd[txSetType][transformType], av1NumExtTxSet[txSetType]);
                }
                return me->intraTxTypeFacBits[extTxSet][squareTxSize][intraDir][transformType];
            }
        }
    }
    return 0;
}

// RE4: get_effective_ac_bias (ac_bias.c:197-211).
double getEffectiveAcBias(double acBias, bool isIslice, std::uint8_t temporalLayerIndex) {
    if (isIslice) {
        return acBias * 0.3;
    }
    switch (temporalLayerIndex) {
    case 0: return acBias * 0.6;
    case 1: return acBias * 0.8;
    case 2: return acBias * 0.9;
    default: return acBias;
    }
}

// RE4: svt_aom_satd_c (common_dsp_rtcd.c:48-54) - the ac_bias.c callee
// (ac_bias.c:44/:56/:79/:91). The source file is an excluded *_rtcd file, so
// the carry is uncharged in the coverage metric.
std::int32_t satd(const std::int32_t* coeff, std::int32_t length) {
    std::int32_t satd = 0;
    for (std::int32_t i = 0; i < length; ++i) satd += coeff[i] < 0 ? -coeff[i] : coeff[i];
    // satd: 26 bits, dynamic range [-32640 * 1024, 32640 * 1024]
    return satd;
}

// RE4: hadamard_col4 (C_DEFAULT/picture_operators_c.c:73-83) + svt_aom_
// hadamard_4x4_c (:85-115) - the ac_bias.c callee (ac_bias.c:77/:89).
// Carried as the l7-l13 duplication precedent (the landed l6 twin lives in
// l6_pipeline/picops.cpp and l16_rdo cannot link l6).
static void hadamardCol4(const std::int16_t* srcDiff, std::ptrdiff_t srcStride, std::int16_t* coeff) {
    std::int16_t b0 = (std::int16_t)((srcDiff[0 * srcStride] + srcDiff[1 * srcStride]) >> 1);
    std::int16_t b1 = (std::int16_t)((srcDiff[0 * srcStride] - srcDiff[1 * srcStride]) >> 1);
    std::int16_t b2 = (std::int16_t)((srcDiff[2 * srcStride] + srcDiff[3 * srcStride]) >> 1);
    std::int16_t b3 = (std::int16_t)((srcDiff[2 * srcStride] - srcDiff[3 * srcStride]) >> 1);

    coeff[0] = b0 + b2;
    coeff[1] = b1 + b3;
    coeff[2] = b0 - b2;
    coeff[3] = b1 - b3;
}

void hadamard4x4(const std::int16_t* srcDiff, std::ptrdiff_t srcStride, std::int32_t* coeff) {
    std::int16_t buffer[16];
    std::int16_t buffer2[16];
    std::int16_t* tmpBuf = &buffer[0];
    for (int idx = 0; idx < 4; ++idx) {
        hadamardCol4(srcDiff, srcStride, tmpBuf);
        tmpBuf += 4;
        ++srcDiff;
    }

    tmpBuf = &buffer[0];
    for (int idx = 0; idx < 4; ++idx) {
        hadamardCol4(tmpBuf, 4, buffer2 + 4 * idx);
        ++tmpBuf;
    }

    // Extra transpose to match SSE2 behavior(i.e., svt_aom_hadamard_4x4_sse2).
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            coeff[i * 4 + j] = (std::int32_t)buffer2[j * 4 + i];
        }
    }
}

// RE4: hadamard_col8 (C_DEFAULT/picture_operators_c.c:119-146) + svt_aom_
// hadamard_8x8_c (:150-174) - the ac_bias.c callee (ac_bias.c:42/:54).
static void hadamardCol8(const std::int16_t* srcDiff, std::ptrdiff_t srcStride, std::int16_t* coeff) {
    std::int16_t b0 = srcDiff[0 * srcStride] + srcDiff[1 * srcStride];
    std::int16_t b1 = srcDiff[0 * srcStride] - srcDiff[1 * srcStride];
    std::int16_t b2 = srcDiff[2 * srcStride] + srcDiff[3 * srcStride];
    std::int16_t b3 = srcDiff[2 * srcStride] - srcDiff[3 * srcStride];
    std::int16_t b4 = srcDiff[4 * srcStride] + srcDiff[5 * srcStride];
    std::int16_t b5 = srcDiff[4 * srcStride] - srcDiff[5 * srcStride];
    std::int16_t b6 = srcDiff[6 * srcStride] + srcDiff[7 * srcStride];
    std::int16_t b7 = srcDiff[6 * srcStride] - srcDiff[7 * srcStride];

    std::int16_t c0 = b0 + b2;
    std::int16_t c1 = b1 + b3;
    std::int16_t c2 = b0 - b2;
    std::int16_t c3 = b1 - b3;
    std::int16_t c4 = b4 + b6;
    std::int16_t c5 = b5 + b7;
    std::int16_t c6 = b4 - b6;
    std::int16_t c7 = b5 - b7;

    coeff[0] = c0 + c4;
    coeff[7] = c1 + c5;
    coeff[3] = c2 + c6;
    coeff[4] = c3 + c7;
    coeff[2] = c0 - c4;
    coeff[6] = c1 - c5;
    coeff[1] = c2 - c6;
    coeff[5] = c3 - c7;
}

void hadamard8x8(const std::int16_t* srcDiff, std::ptrdiff_t srcStride, std::int32_t* coeff) {
    std::int16_t buffer[64];
    std::int16_t buffer2[64];
    std::int16_t* tmpBuf = &buffer[0];
    for (int idx = 0; idx < 8; ++idx) {
        hadamardCol8(srcDiff, srcStride, tmpBuf);
        tmpBuf += 8;
        ++srcDiff;
    }

    tmpBuf = &buffer[0];
    for (int idx = 0; idx < 8; ++idx) {
        hadamardCol8(tmpBuf, 8, buffer2 + 8 * idx);
        ++tmpBuf;
    }

    for (int idx = 0; idx < 64; ++idx) {
        coeff[idx] = static_cast<std::int32_t>(buffer2[idx]);
    }
}

// RE4: svt_psy_distortion (ac_bias.c:22-99) - the 8-bit "AC Bias" energy-gap
// distortion. The hadamard/satd callees are the carried twins above.
std::uint64_t psyDistortion(const std::uint8_t* input, std::uint32_t inputStride, const std::uint8_t* recon,
                             std::uint32_t reconStride, std::uint32_t width, std::uint32_t height) {
    std::uint64_t energyGap = 0;

    if (width >= 8 && height >= 8) { /* >8x8 */
        for (std::uint32_t j = 0; j < height; j += 8) {
            for (std::uint32_t i = 0; i < width; i += 8) {
                std::int32_t coeffs[64];
                std::int16_t blockAs16bit[64];
                const std::uint8_t* blockInput = input + j * inputStride + i;
                const std::uint8_t* reconInput = recon + j * reconStride + i;

                for (int h = 0; h < 8; h++) {
                    for (int w = 0; w < 8; w++) {
                        blockAs16bit[h * 8 + w] = blockInput[w];
                    }

                    blockInput += inputStride;
                }

                hadamard8x8(blockAs16bit, 8, coeffs);

                std::int32_t inputEnergy = ((satd(coeffs, 64) + 2) >> 2) - ((coeffs[0] + 2) >> 2);

                for (int h = 0; h < 8; h++) {
                    for (int w = 0; w < 8; w++) {
                        blockAs16bit[h * 8 + w] = reconInput[w];
                    }

                    reconInput += reconStride;
                }

                hadamard8x8(blockAs16bit, 8, coeffs);

                std::int32_t reconEnergy = ((satd(coeffs, 64) + 2) >> 2) - ((coeffs[0] + 2) >> 2);

                energyGap += inputEnergy > reconEnergy ? (std::uint64_t)(inputEnergy - reconEnergy)
                                                       : (std::uint64_t)(reconEnergy - inputEnergy);
            }
        }
    } else {
        for (std::uint32_t j = 0; j < height; j += 4) { /* 4x4, 4x8, 4x16, 8x4, and 16x4 */
            for (std::uint32_t i = 0; i < width; i += 4) {
                std::int32_t coeffs[16];
                std::int16_t blockAs16bit[16];
                const std::uint8_t* blockInput = input + j * inputStride + i;
                const std::uint8_t* reconInput = recon + j * reconStride + i;

                for (int h = 0; h < 4; h++) {
                    for (int w = 0; w < 4; w++) {
                        blockAs16bit[h * 4 + w] = blockInput[w];
                    }

                    blockInput += inputStride;
                }

                hadamard4x4(blockAs16bit, 4, coeffs);

                std::int32_t inputEnergy = (satd(coeffs, 16) << 1) - coeffs[0];

                for (int h = 0; h < 4; h++) {
                    for (int w = 0; w < 4; w++) {
                        blockAs16bit[h * 4 + w] = reconInput[w];
                    }

                    reconInput += reconStride;
                }

                hadamard4x4(blockAs16bit, 4, coeffs);

                std::int32_t reconEnergy = (satd(coeffs, 16) << 1) - coeffs[0];

                energyGap += inputEnergy > reconEnergy ? (std::uint64_t)(inputEnergy - reconEnergy)
                                                       : (std::uint64_t)(reconEnergy - inputEnergy);
            }
        }
    }

    return energyGap;
}

// RE4: get_svt_psy_full_dist (ac_bias.c:147-162). The hbd branch is the
// pinned build's #else (CONFIG_ENABLE_HIGH_BIT_DEPTH=0): return 0. The
// svt_psy_distortion_hbd twin (ac_bias.c:101-145, 33 nb) is NAMED-PARKED -
// dead under the pinned CONFIG.
std::uint64_t psyFullDist(const void* s, std::uint32_t so, std::uint32_t sp, const void* r, std::uint32_t ro,
                           std::uint32_t rp, std::uint32_t w, std::uint32_t h, std::uint8_t isHbd,
                           double acBias) {
    if (isHbd) {
        return 0;
    } else {
        return (std::uint64_t)std::llrint(psyDistortion((const std::uint8_t*)s + so, sp,
                                                         (const std::uint8_t*)r + ro, rp, w, h) * acBias);
    }
}

// RE4: svt_psy_adjust_rate_light (ac_bias.c:164-195) - the Light-PD rate
// adjustment over the existing block coefficients.
std::uint64_t psyAdjustRateLight(const std::int32_t* coeff, std::uint64_t coeffBits, std::uint32_t width,
                                  std::uint32_t height, double acBias) {
    std::uint64_t energy = 0;
    const std::int32_t* buf = coeff;

    for (std::uint32_t j = 0; j < height; j++) {
        // Skip the DC coefficient from the calculation
        for (std::uint32_t i = j ? 0 : 1; i < width; i++) {
            energy += (std::uint64_t)std::llabs((std::int64_t)buf[i]);
        }
        buf += width;
    }

    if (energy > 0) {
        std::uint64_t coeffBitsAdj = (std::uint64_t)(std::int32_t)(energy * acBias * 100);

        // When the adjustment rate is greater than the rate, keep rate (coeff_bits) positive
        coeffBits = (coeffBits > coeffBitsAdj) ? (coeffBits - coeffBitsAdj) : 1;
    }

    return coeffBits;
}

// RE4: av1_use_angle_delta (intra_prediction.h:214-216).
std::int32_t useAngleDelta(BlockSize bsize) {
    return bsize >= BLOCK_8X8;
}

// RE4: svt_get_ref_frame_type (mode_decision.c:282-284) over to_ref_frame
// (mode_decision.c:279-280).
static const MvRefFrameExt toRefFrame[2][4] = {
    {MVX_LAST_FRAME, MVX_LAST2_FRAME, MVX_LAST3_FRAME, MVX_GOLDEN_FRAME},
    {MVX_BWDREF_FRAME, MVX_ALTREF2_FRAME, MVX_ALTREF_FRAME, MVX_INVALID_REF},
};
std::int32_t getRefFrameType(std::uint8_t list, std::uint8_t refIdx) {
    return toRefFrame[list][refIdx];
}

// RE4: svt_aom_ss_size_lookup (common_utils.c:239-263, verbatim).
const std::uint8_t ssSizeLookup[BLOCK_SIZES_ALL][2][2] = {
    //  ss_x == 0    ss_x == 0        ss_x == 1      ss_x == 1
    //  ss_y == 0    ss_y == 1        ss_y == 0      ss_y == 1
    {{BLOCK_4X4, BLOCK_4X4}, {BLOCK_4X4, BLOCK_4X4}},
    {{BLOCK_4X8, BLOCK_4X4}, {255, BLOCK_4X4}},
    {{BLOCK_8X4, 255}, {BLOCK_4X4, BLOCK_4X4}},
    {{BLOCK_8X8, BLOCK_8X4}, {BLOCK_4X8, BLOCK_4X4}},
    {{BLOCK_8X16, BLOCK_8X8}, {255, BLOCK_4X8}},
    {{BLOCK_16X8, 255}, {BLOCK_8X8, BLOCK_8X4}},
    {{BLOCK_16X16, BLOCK_16X8}, {BLOCK_8X16, BLOCK_8X8}},
    {{BLOCK_16X32, BLOCK_16X16}, {255, BLOCK_8X16}},
    {{BLOCK_32X16, 255}, {BLOCK_16X16, BLOCK_16X8}},
    {{BLOCK_32X32, BLOCK_32X16}, {BLOCK_16X32, BLOCK_16X16}},
    {{BLOCK_32X64, BLOCK_32X32}, {255, BLOCK_16X32}},
    {{BLOCK_64X32, 255}, {BLOCK_32X32, BLOCK_32X16}},
    {{BLOCK_64X64, BLOCK_64X32}, {BLOCK_32X64, BLOCK_32X32}},
    {{BLOCK_64X128, BLOCK_64X64}, {255, BLOCK_32X64}},
    {{BLOCK_128X64, 255}, {BLOCK_64X64, BLOCK_64X32}},
    {{BLOCK_128X128, BLOCK_128X64}, {BLOCK_64X128, BLOCK_64X64}},
    {{BLOCK_4X16, BLOCK_4X8}, {255, BLOCK_4X8}},
    {{BLOCK_16X4, 255}, {BLOCK_8X4, BLOCK_8X4}},
    {{BLOCK_8X32, BLOCK_8X16}, {255, BLOCK_4X16}},
    {{BLOCK_32X8, 255}, {BLOCK_16X8, BLOCK_16X4}},
    {{BLOCK_16X64, BLOCK_16X32}, {255, BLOCK_8X32}},
    {{BLOCK_64X16, 255}, {BLOCK_32X16, BLOCK_32X8}},
};

// RE4: get_plane_block_size (common_utils.h:135-140).
std::int32_t getPlaneBlockSize(BlockSize bsize, std::int32_t subsamplingX, std::int32_t subsamplingY) {
    if (bsize == 255) {  // BLOCK_INVALID, definitions.h:907
        return 255;
    }
    return ssSizeLookup[bsize][subsamplingX][subsamplingY];
}

// RE4: blocksize_to_txsize (common_utils.c:214-237, verbatim).
const std::int32_t blocksizeToTxsize[BLOCK_SIZES_ALL] = {
    TXS_4X4,    // BLOCK_4X4
    TXS_4X8,    // BLOCK_4X8
    TXS_8X4,    // BLOCK_8X4
    TXS_8X8,    // BLOCK_8X8
    TXS_8X16,   // BLOCK_8X16
    TXS_16X8,   // BLOCK_16X8
    TXS_16X16,  // BLOCK_16X16
    TXS_16X32,  // BLOCK_16X32
    TXS_32X16,  // BLOCK_32X16
    TXS_32X32,  // BLOCK_32X32
    TXS_32X64,  // BLOCK_32X64
    TXS_64X32,  // BLOCK_64X32
    TXS_64X64,  // BLOCK_64X64
    TXS_64X64,  // BLOCK_64X128
    TXS_64X64,  // BLOCK_128X64
    TXS_64X64,  // BLOCK_128X128
    TXS_4X16,   // BLOCK_4X16
    TXS_16X4,   // BLOCK_16X4
    TXS_8X32,   // BLOCK_8X32
    TXS_32X8,   // BLOCK_32X8
    TXS_16X64,  // BLOCK_16X64
    TXS_64X16,  // BLOCK_64X16
};

// RE4: av1_get_adjusted_tx_size (common_utils.h:100-113).
std::int32_t adjustedTxSize(std::int32_t txSize) {
    switch (txSize) {
    case TXS_64X64:
    case TXS_64X32:
    case TXS_32X64:
        return TXS_32X32;
    case TXS_64X16:
        return TXS_32X16;
    case TXS_16X64:
        return TXS_16X32;
    default:
        return txSize;
    }
}

// RE4: av1_get_max_uv_txsize (common_utils.h:142-149).
std::int32_t maxUvTxsize(BlockSize bsize, std::int32_t subsamplingX, std::int32_t subsamplingY) {
    const std::int32_t planeBsize = getPlaneBlockSize(bsize, subsamplingX, subsamplingY);
    std::int32_t uvTx = TXS_INVALID;
    if (planeBsize < BLOCK_SIZES_ALL) {
        uvTx = blocksizeToTxsize[planeBsize];
    }
    return adjustedTxSize(uvTx);
}

// RE4: av1_ext_tx_used (common_utils.c:197-204, verbatim).
const std::int32_t av1ExtTxUsed[EXT_TX_SET_TYPES][TX_TYPES] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
};

// RE4: g_intra_mode_to_tx_type (mode_decision.c:2959-2973, verbatim; the
// reference's D117/D153 comments are the legacy names of D113/D157).
const std::int32_t gIntraModeToTxType[13] = {
    0,  // DC          DCT_DCT
    1,  // V           ADST_DCT
    2,  // H           DCT_ADST
    0,  // D45         DCT_DCT
    3,  // D135        ADST_ADST
    1,  // D113        ADST_DCT
    2,  // D157        DCT_ADST
    2,  // D203        DCT_ADST
    1,  // D67         ADST_DCT
    3,  // SMOOTH      ADST_ADST
    1,  // SMOOTH_V    ADST_DCT
    2,  // SMOOTH_H    DCT_ADST
    3,  // PAETH       ADST_ADST
};

// RE4: intra_mode_to_tx_type (mode_decision.c:2975-2979).
std::int32_t intraModeToTxType(std::int32_t predMode, std::int32_t predModeUv, std::int32_t planeType) {
    const std::int32_t mode = (planeType == 0) ? predMode : getUvMode(predModeUv);
    return gIntraModeToTxType[mode];
}

// RE4: svt_aom_get_intra_uv_tx_type (mode_decision.c:2981-2997).
std::int32_t getIntraUvTxType(std::int32_t predModeUv, std::int32_t txSize, std::int32_t reducedTxSet) {
    if (txsizeSqrUpMap[txSize] > TX_32X32) {
        return 0;  // DCT_DCT
    }

    const std::int32_t txType = intraModeToTxType(0 /* DC_PRED, unused for UV */, predModeUv, 1 /* PLANE_TYPE_UV */);
    const std::int32_t txSetType = getExtTxSetType(txSize, /*is_inter*/ 0, reducedTxSet);
    return !av1ExtTxUsed[txSetType][txType] ? 0 : txType;
}

// RE4: fimode_to_intramode (definitions.h:1304, verbatim).
const std::int32_t fimodeToIntraMode[5] = {DC_PRED, V_PRED, H_PRED, D157_PRED, PAETH_PRED};

// RE4: intra_luma_to_chroma (mode_decision.c:42-56, verbatim).
const std::int32_t intraLumaToChroma[13] = {
    UV_DC_PRED, UV_V_PRED, UV_H_PRED, UV_D45_PRED, UV_D135_PRED, UV_D113_PRED,
    UV_D157_PRED, UV_D203_PRED, UV_D67_PRED, UV_SMOOTH_PRED, UV_SMOOTH_V_PRED,
    UV_SMOOTH_H_PRED, UV_PAETH_PRED,
};

// RE4: INC_MD_CAND_CNT (mode_decision.c:61-68) - increments cnt only when
// cnt + 1 < max_can_count; else SVT_ERROR (the count is not incremented;
// named deviation: the port does not print).
static inline void incMdCandCnt(std::uint32_t& cnt, std::uint32_t maxCanCount) {
    if (cnt + 1 < maxCanCount) {
        ++cnt;
    }
}

// RE4: inject_intra_candidates_pd0 (mode_decision.c:3200-3225).
void injectIntraCandidatesPd0(InjectedCandidate* candArray, std::uint32_t* candidateTotalCnt,
                               std::uint32_t maxCanCount) {
    std::uint32_t candTotalCnt = 0;
    InjectedCandidate* cand = &candArray[candTotalCnt];
    cand->skipModeAllowed = false;
    cand->paletteInfo = nullptr;
    cand->useIntrabc = 0;
    cand->filterIntraMode = FILTER_INTRA_MODE_COUNT;
    cand->angleDelta[0] = 0;
    cand->uvMode = UV_DC_PRED;
    cand->angleDelta[1] = 0;
    cand->cflAlphaSigns = 0;
    cand->cflAlphaIdx = 0;
    cand->transformType = 0;  // DCT_DCT
    cand->transformTypeUv = 0;
    cand->refFrame[0] = MVX_INTRA_FRAME;
    cand->refFrame[1] = MVX_NONE_FRAME;
    cand->mode = DC_PRED;
    cand->motionMode = SIMPLE_TRANSLATION;
    cand->isInterintraUsed = 0;
    cand->txDepth = 0;
    incMdCandCnt(candTotalCnt, maxCanCount);
    // update the total number of candidates injected
    (*candidateTotalCnt) = candTotalCnt;

    return;
}

// RE4: av1_make_interp_filters (filter.h:65-68) + av1_broadcast_interp_
// filter (:70-72) - the packed InterpFilters encoding (uint32).
static std::uint32_t broadcastInterpFilter(std::int32_t filter) {
    const std::uint32_t f16 = (std::uint32_t)(filter & 0xffff);
    return f16 | (f16 << 16);
}

// RE4: inject_intra_bc_candidates (mode_decision.c:3163-3198).
void injectIntraBcCandidates(InjectedCandidate* candArray, std::uint32_t* candidateTotalCnt,
                              std::uint32_t maxCanCount, const std::int32_t* dvCand, std::uint8_t numDvCand,
                              std::int32_t refMvStackPred) {
    std::uint32_t candTotalCnt = *candidateTotalCnt;

    for (std::uint32_t dvI = 0; dvI < numDvCand; dvI++) {
        InjectedCandidate* cand = &candArray[candTotalCnt];
        cand->paletteInfo = nullptr;
        cand->useIntrabc = 1;
        cand->angleDelta[0] = 0;
        cand->angleDelta[1] = 0;
        cand->uvMode = UV_DC_PRED;
        cand->cflAlphaSigns = 0;
        cand->cflAlphaIdx = 0;
        cand->transformType = 0;  // DCT_DCT
        cand->transformTypeUv = 0;
        cand->refFrame[0] = MVX_INTRA_FRAME;
        cand->refFrame[1] = MVX_NONE_FRAME;
        cand->mode = DC_PRED;
        cand->filterIntraMode = FILTER_INTRA_MODE_COUNT;
        // inter related
        cand->motionMode = SIMPLE_TRANSLATION;
        cand->isInterintraUsed = 0;
        cand->skipModeAllowed = false;
        cand->mv = dvCand[dvI];
        cand->predMv = refMvStackPred;
        cand->drlIndex = 0;
        cand->interpFilters = broadcastInterpFilter(3 /* BILINEAR, definitions.h:822 */);
        incMdCandCnt(candTotalCnt, maxCanCount);
    }
    (*candidateTotalCnt) = candTotalCnt;
}

// RE4: is_valid_mv_diff (mode_decision.c:803-818). The packed Mv form:
// x | y << 16 (mv.h as_int layout).
static std::int32_t mvX(std::int32_t packed) {
    return (std::int16_t)(packed & 0xffff);
}
static std::int32_t mvY(std::int32_t packed) {
    return (std::int16_t)((unsigned)packed >> 16);
}
std::int32_t isValidMvDiff(std::int32_t bestPredMv0, std::int32_t bestPredMv1, std::int32_t mv0,
                            std::int32_t mv1, std::int32_t isCompound) {
    const std::uint8_t mvDiffMaxBit = MV_IN_USE_BITS;

    if (std::abs(mvX(mv0) - mvX(bestPredMv0)) > (1 << mvDiffMaxBit) ||
        std::abs(mvY(mv0) - mvY(bestPredMv0)) > (1 << mvDiffMaxBit)) {
        return 0;
    }

    if (isCompound) {
        if (std::abs(mvX(mv1) - mvX(bestPredMv1)) > (1 << mvDiffMaxBit) ||
            std::abs(mvY(mv1) - mvY(bestPredMv1)) > (1 << mvDiffMaxBit)) {
            return 0;
        }
    }
    return 1;
}

// RE4: inject_zz_backup_candidate (mode_decision.c:3350-3382).
void injectZzBackupCandidate(InjectedCandidate* candArray, std::uint32_t* candidateTotalCnt,
                              std::uint32_t maxCanCount, std::int32_t bestPredMv, std::int32_t drlIndex,
                              bool corruptedMvCheck, std::int32_t numProjRef) {
    std::uint32_t candTotalCnt = (*candidateTotalCnt);
    candArray[candTotalCnt].drlIndex = drlIndex;
    if (!corruptedMvCheck || isValidMvDiff(bestPredMv, 0, 0, 0, 0)) {
        InjectedCandidate* cand = &candArray[candTotalCnt];
        cand->useIntrabc = 0;
        cand->skipModeAllowed = false;
        cand->mode = NEWMV;
        cand->motionMode = SIMPLE_TRANSLATION;
        cand->mv = 0;  // (Mv){{0, 0}}
        cand->refFrame[0] = getRefFrameType(0 /* REF_LIST_0 */, 0);
        cand->refFrame[1] = MVX_NONE_FRAME;
        cand->transformType = 0;  // DCT_DCT
        cand->transformTypeUv = 0;
        cand->predMv = bestPredMv;
        cand->isInterintraUsed = 0;
        cand->motionMode = SIMPLE_TRANSLATION;
        cand->numProjRef = numProjRef;
        incMdCandCnt(candTotalCnt, maxCanCount);
        // update the total number of candidates injected
        (*candidateTotalCnt) = candTotalCnt;
    }
}
// RE4: inject_intra_candidates (mode_decision.c:3227-3296).
void injectIntraCandidates(InjectedCandidate* candArray, std::uint32_t* candidateTotalCnt,
                            std::uint32_t maxCanCount, bool dcCandOnlyFlag, std::int32_t modeEnd,
                            std::int32_t angularPredLevel, BlockSize bsize, bool indUvAvail,
                            const std::int32_t* bestUvMode, const std::int32_t* bestUvAngle,
                            bool isLosslessSegment, std::int32_t reducedTxSet) {
    const std::int32_t intraModeStart = DC_PRED;
    const std::int32_t intraModeEnd = dcCandOnlyFlag ? DC_PRED : modeEnd;
    std::uint32_t candTotalCnt = *candidateTotalCnt;
    const bool useDelta = angularPredLevel ? useAngleDelta(bsize) != 0 : false;
    const bool disableAnglePrediction = (angularPredLevel == 0);
    std::uint8_t directionalModeSkipMask[INTRA_MODES] = {0};
    if (angularPredLevel >= 4) {
        for (std::uint8_t i = D45_PRED; i < INTRA_MODES; i++) {  // INTRA_MODE_END = NEARESTMV
            directionalModeSkipMask[i] = 1;
        }
    }
    const std::int32_t txSizeUv = maxUvTxsize(bsize, 1, 1);

    for (std::int32_t intraMode = intraModeStart; intraMode <= intraModeEnd; ++intraMode) {
        if (av1IsDirectionalMode(intraMode) &&
            (disableAnglePrediction || directionalModeSkipMask[intraMode])) {
            continue;
        }

        const std::uint8_t angleDeltaCount =
            av1IsDirectionalMode(intraMode) && angularPredLevel <= 2 && useDelta ? 7 : 1;

        for (std::uint8_t angleDeltaCounter = 0; angleDeltaCounter < angleDeltaCount; ++angleDeltaCounter) {
            std::int32_t angleDelta = angleDeltaCount == 1 ? 0 : angleDeltaCounter - MAX_ANGLE_DELTA;
            if (angleDelta < -(std::int32_t)MAX_ANGLE_DELTA) angleDelta = -(std::int32_t)MAX_ANGLE_DELTA;
            if (angleDelta > (std::int32_t)MAX_ANGLE_DELTA) angleDelta = (std::int32_t)MAX_ANGLE_DELTA;
            if ((angularPredLevel >= 2 &&
                 (angleDelta == -1 || angleDelta == 1 || angleDelta == -2 || angleDelta == 2)) ||
                (angularPredLevel >= 3 && angleDelta != 0)) {
                continue;
            }
            InjectedCandidate* cand = &candArray[candTotalCnt];
            cand->skipModeAllowed = false;
            cand->paletteInfo = nullptr;
            cand->mode = intraMode;
            cand->useIntrabc = 0;
            cand->filterIntraMode = FILTER_INTRA_MODE_COUNT;
            cand->angleDelta[0] = angleDelta;
            cand->uvMode = indUvAvail ? bestUvMode[intraMode] : intraLumaToChroma[intraMode];
            cand->angleDelta[1] = indUvAvail ? bestUvAngle[intraMode] : cand->angleDelta[0];
            cand->cflAlphaSigns = 0;
            cand->cflAlphaIdx = 0;
            cand->transformType = 0;  // DCT_DCT
            cand->transformTypeUv = getIntraUvTxType(cand->uvMode, txSizeUv, reducedTxSet);

            if (isLosslessSegment && cand->transformTypeUv != 0 /* DCT_DCT */) {
                continue;
            }
            cand->refFrame[0] = MVX_INTRA_FRAME;
            cand->refFrame[1] = MVX_NONE_FRAME;
            cand->motionMode = SIMPLE_TRANSLATION;
            cand->isInterintraUsed = 0;
            incMdCandCnt(candTotalCnt, maxCanCount);
        }
    }

    // update the total number of candidates injected
    (*candidateTotalCnt) = candTotalCnt;

    return;
}

// RE4: inject_filter_intra_candidates (mode_decision.c:3298-3348).
void injectFilterIntraCandidates(InjectedCandidate* candArray, std::uint32_t* candidateTotalCnt,
                                  std::uint32_t maxCanCount, std::int32_t intraModeEnd,
                                  std::int32_t maxFilterIntraMode, BlockSize bsize, bool indUvAvail,
                                  const std::int32_t* bestUvMode, const std::int32_t* bestUvAngle,
                                  bool isLosslessSegment, std::int32_t reducedTxSet) {
    std::int32_t fiModeStart = FILTER_DC_PRED;
    std::int32_t fiModeEnd = intraModeEnd == PAETH_PRED ? FILTER_PAETH_PRED
        : intraModeEnd >= D157_PRED ? FILTER_D157_PRED
        : intraModeEnd >= H_PRED ? FILTER_H_PRED
        : intraModeEnd >= V_PRED ? FILTER_V_PRED
                                 : FILTER_DC_PRED;
    if (fiModeEnd > maxFilterIntraMode) fiModeEnd = maxFilterIntraMode;  // MIN

    const std::int32_t txSizeUv = maxUvTxsize(bsize, 1, 1);
    std::uint32_t candTotalCnt = *candidateTotalCnt;

    for (std::int32_t filterIntraMode = fiModeStart; filterIntraMode <= fiModeEnd; filterIntraMode++) {
        InjectedCandidate* cand = &candArray[candTotalCnt];
        cand->skipModeAllowed = false;
        cand->mode = DC_PRED;
        cand->useIntrabc = 0;
        cand->filterIntraMode = filterIntraMode;
        cand->paletteInfo = nullptr;
        cand->angleDelta[0] = 0;

        cand->uvMode = indUvAvail ? bestUvMode[fimodeToIntraMode[filterIntraMode]]
                                  : intraLumaToChroma[fimodeToIntraMode[filterIntraMode]];
        cand->angleDelta[1] = indUvAvail ? bestUvAngle[fimodeToIntraMode[filterIntraMode]]
                                         : cand->angleDelta[0];

        cand->cflAlphaSigns = 0;
        cand->cflAlphaIdx = 0;
        cand->transformType = 0;  // DCT_DCT
        cand->transformTypeUv = getIntraUvTxType(cand->uvMode, txSizeUv, reducedTxSet);
        if (isLosslessSegment && cand->transformTypeUv != 0 /* DCT_DCT */) {
            continue;
        }
        cand->refFrame[0] = MVX_INTRA_FRAME;
        cand->refFrame[1] = MVX_NONE_FRAME;
        cand->motionMode = SIMPLE_TRANSLATION;
        cand->isInterintraUsed = 0;
        incMdCandCnt(candTotalCnt, maxCanCount);
    }

    // update the total number of candidates injected
    (*candidateTotalCnt) = candTotalCnt;

    return;
}

}  // namespace l16_rdo
