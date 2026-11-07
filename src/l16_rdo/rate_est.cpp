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

#include <cstring>

namespace l16_rdo {

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

}  // namespace l16_rdo
