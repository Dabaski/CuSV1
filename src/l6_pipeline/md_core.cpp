// l6_pipeline::mdc - see md_core.h for the carrier conventions and the
// park list.
#include "md_core.h"

#include <cassert>

#include <md_slices.h>
#include <motion.h>
#include <motion_search.h>
#include <packunpack.h>
#include <picops.h>
#include <subpel.h>

namespace pipeline {
namespace mdc {

// me_sb_results.h:25 SQUARE_PU_COUNT + motion_estimation.h:97-:130, the
// three PU tables verbatim.
const std::uint32_t partitionWidth[kSquarePuCount] = {
    64,
    32, 32, 32, 32,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
};
const std::uint32_t partitionHeight[kSquarePuCount] = {
    64,
    32, 32, 32, 32,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
};
const std::uint32_t puSearchIndexMap[kSquarePuCount][2] = {
    {0, 0},
    {0, 0},  {32, 0}, {0, 32}, {32, 32},
    {0, 0},  {16, 0}, {32, 0}, {48, 0},
    {0, 16}, {16, 16}, {32, 16}, {48, 16},
    {0, 32}, {16, 32}, {32, 32}, {48, 32},
    {0, 48}, {16, 48}, {32, 48}, {48, 48},
    {0, 0},  {8, 0},  {16, 0}, {24, 0}, {32, 0}, {40, 0}, {48, 0}, {56, 0},
    {0, 8},  {8, 8},  {16, 8}, {24, 8}, {32, 8}, {40, 8}, {48, 8}, {56, 8},
    {0, 16}, {8, 16}, {16, 16}, {24, 16}, {32, 16}, {40, 16}, {48, 16}, {56, 16},
    {0, 24}, {8, 24}, {16, 24}, {24, 24}, {32, 24}, {40, 24}, {48, 24}, {56, 24},
    {0, 32}, {8, 32}, {16, 32}, {24, 32}, {32, 32}, {40, 32}, {48, 32}, {56, 32},
    {0, 40}, {8, 40}, {16, 40}, {24, 40}, {32, 40}, {40, 40}, {48, 40}, {56, 40},
    {0, 48}, {8, 48}, {16, 48}, {24, 48}, {32, 48}, {40, 48}, {48, 48}, {56, 48},
    {0, 56}, {8, 56}, {16, 56}, {24, 56}, {32, 56}, {40, 56}, {48, 56}, {56, 56},
};

// definitions.h:457 ROUND_POWER_OF_TWO (the local-helper precedent).
static std::int64_t roundPowerOfTwo64(std::int64_t value, int n) {
    return (value + (((std::int64_t)1 << n) >> 1)) >> n;
}

// AOMMIN over the uint32 distortion pair (definitions.h).
static std::uint32_t minU32(std::uint32_t a, std::uint32_t b) {
    return a < b ? a : b;
}

// The two-buffer VAR(W, H) instances at 8x8/16x16 (C_DEFAULT/variance.c
// :184-:190). The landed analysis::variance8x8/16x16 carry the AN8
// offs-folded single-buffer call shape (screen-content), so the mefn
// dispatch composes these two from the landed varianceCore - the same
// macro body, the general two-buffer shape the PU1 enumeration names.
static std::uint32_t variance8x8Vf(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride,
                                   std::uint32_t* sse) {
    int sum;
    analysis::varianceCore(a, aStride, b, bStride, 8, 8, sse, &sum);
    return *sse - static_cast<std::uint32_t>((static_cast<std::int64_t>(sum) * sum) / (8 * 8));
}
static std::uint32_t variance16x16Vf(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride,
                                     std::uint32_t* sse) {
    int sum;
    analysis::varianceCore(a, aStride, b, bStride, 16, 16, sse, &sum);
    return *sse - static_cast<std::uint32_t>((static_cast<std::int64_t>(sum) * sum) / (16 * 16));
}

// svt_aom_mefn_ptr (av1me.h:69), the C-kernel dispatch: per-bsize the
// VAR(W, H) instance whose dims name the BlockSize.
static const VarianceFn kMefnVfTable[entropy::BLOCK_SIZES_ALL] = {
    analysis::variance4x4,    // BLOCK_4X4
    analysis::variance4x8,    // BLOCK_4X8
    analysis::variance8x4,    // BLOCK_8X4
    variance8x8Vf,            // BLOCK_8X8
    analysis::variance8x16,   // BLOCK_8X16
    analysis::variance16x8,   // BLOCK_16X8
    variance16x16Vf,          // BLOCK_16X16
    analysis::variance16x32,  // BLOCK_16X32
    analysis::variance32x16,  // BLOCK_32X16
    analysis::variance32x32,  // BLOCK_32X32
    analysis::variance32x64,  // BLOCK_32X64
    analysis::variance64x32,  // BLOCK_64X32
    analysis::variance64x64,  // BLOCK_64X64
    analysis::variance64x128, // BLOCK_64X128
    analysis::variance128x64, // BLOCK_128X64
    analysis::variance128x128,// BLOCK_128X128
    analysis::variance4x16,   // BLOCK_4X16
    analysis::variance16x4,   // BLOCK_16X4
    analysis::variance8x32,   // BLOCK_8X32
    analysis::variance32x8,   // BLOCK_32X8
    analysis::variance16x64,  // BLOCK_16X64
    analysis::variance64x16,  // BLOCK_64X16
};

VarianceFn mefnVf(entropy::BlockSize bsize) {
    return kMefnVfTable[bsize];
}

// definitions.h:1599-1601 is_inter_mode via the landed l16 helper
// (rate_est.h:816-817).
static bool isInterMode(l16_rdo::PredictionMode mode) {
    return l16_rdo::isInterMode(static_cast<std::int32_t>(mode)) != 0;
}

// product_coding_loop.c:966-:979 fast_loop_core_pd0.
void fastLoopCorePd0(MdCandBf* cand_bf, FlCtx* ctx, const PicBuf& input_pic, std::uint32_t input_origin_index,
                     std::uint32_t cu_origin_index) {
    MdCand* cand = cand_bf->cand;
    const PicBuf& pred = cand_bf->pred;

    // intrabc not allowed in light_pd0
    ctx->predTable[isInterMode(cand->mode)](*ctx, *cand_bf);
    VarianceFn vf = mefnVf(ctx->geom.bsize);
    std::uint32_t sse;
    const std::uint8_t* pred_y = pred.buf + cu_origin_index;
    const std::uint8_t* src_y = input_pic.buf + input_origin_index;
    *cand_bf->fast_cost = vf(pred_y, pred.stride, src_y, input_pic.stride, &sse);
}

// rd_cost.h:36-38 RDCOST (RDDIV_BITS = 7, rd_cost.h:33; AV1_PROB_COST_SHIFT
// = 9) + ROUND_POWER_OF_TWO (definitions.h:457).
static std::int64_t rdCost(std::uint64_t lambda, std::uint64_t rate, std::uint64_t dist) {
    const std::int64_t rm = static_cast<std::int64_t>(rate) * static_cast<std::int64_t>(lambda);
    return roundPowerOfTwo64(rm, kAv1ProbCostShift) + static_cast<std::int64_t>(dist) * (1 << kRddivBits);
}

// definitions.h:1595-1597 is_intra_mode = mode < INTRA_MODE_END = mode <
// NEARESTMV = mode < INTRA_MODES.
static bool isIntraMode(l16_rdo::PredictionMode mode) {
    return static_cast<std::int32_t>(mode) < l16_rdo::INTRA_MODES;
}

// The vf_hbd_10 slot (av1me.h:60): the hbd variance is the landed
// analysis::packunpack::varianceHighbd at the block dims (uint16 planes
// through the CONVERT_TO_BYTEPTR casts).
static std::uint32_t mefnVfHbd10(const std::uint16_t* pred, std::uint32_t predStride, const std::uint16_t* src,
                                 std::uint32_t srcStride, std::uint32_t w, std::uint32_t h, std::uint32_t* sse) {
    return analysis::packunpack::varianceHighbd(pred, static_cast<int>(predStride), src, static_cast<int>(srcStride),
                                                static_cast<int>(w), static_cast<int>(h), sse);
}

// product_coding_loop.c:981-:1038 fast_loop_core_light_pd1; assumes luma
// only, 8bit only, and that SSD is not used.
void fastLoopCoreLightPd1(MdCandBf* cand_bf, FlCtx* ctx, const PicBuf& input_pic, std::uint32_t input_origin_index) {
    std::uint64_t luma_fast_dist;
    const std::uint64_t full_lambda = ctx->full_lambda_md[0];

    MdCand* cand = cand_bf->cand;
    const PicBuf& pred = cand_bf->pred;
    // If not first candidate to be tested, take advantage of known info to skip current candidate
    if (ctx->mds0_best_cost != (std::uint64_t)~0) {
        if (isIntraMode(cand->mode) && ctx->cand_elim_enabled) {
            const std::uint64_t best_dist = ctx->cand_bf_ptr_array[ctx->mds0_best_idx]->luma_fast_dist;

            // Use more aggressive dc_only_th at MDS0
            std::uint32_t th = cand->mode != l16_rdo::DC_PRED ? ctx->dc_only_th : ctx->skip_dc_th;
            th *= (ctx->geom.bheight * ctx->geom.bwidth);
            if (best_dist < th) {
                // already injected/tested; set cost to max and exit
                *cand_bf->fast_cost = kMaxModeCost;
                return;
            }
        }
    }
    // Prediction
    ctx->uv_intra_comp_only = false;
    ctx->predTable[isInterMode(cand->mode)](*ctx, *cand_bf);
    // Distortion
    VarianceFn vf = mefnVf(ctx->geom.bsize);
    std::uint32_t sse;
    const std::uint8_t* pred_y = pred.buf;
    const std::uint8_t* src_y = input_pic.buf + input_origin_index;
    cand_bf->luma_fast_dist = vf(pred_y, pred.stride, src_y, input_pic.stride, &sse);
    // Shift variance by 4 because we use full lambda in the cost (since variance is proportional to sse)
    // and full lambda is set with the expectation the variance is a squared metric shifted by 4 (the same
    // shift is applied to sse in the full loop)
    luma_fast_dist = cand_bf->luma_fast_dist << 4;
    // Set full_dist to sse because it's used by lpd1_bypass_tx_th to skip the TX
    cand_bf->full_dist = sse;
    // If distortion cost is greater than the best cost, exit early. This candidate will never be
    // selected b/c only one candidate is sent to MDS3
    if (ctx->mds0_best_cost != (std::uint64_t)~0) {
        const std::uint64_t distortion_cost = static_cast<std::uint64_t>(rdCost(full_lambda, 0, luma_fast_dist));
        if (distortion_cost > ctx->mds0_best_cost) {
            *cand_bf->fast_cost = kMaxModeCost;
            return;
        }
    }

    // Fast Cost
    if (ctx->shut_fast_rate) {
        *cand_bf->fast_cost = luma_fast_dist;
        cand_bf->fast_luma_rate = 0;
        cand_bf->fast_chroma_rate = 0;
    } else {
        *cand_bf->fast_cost = ctx->fastCostTable[isInterMode(cand->mode)](*ctx, *cand_bf, full_lambda, luma_fast_dist);
    }
}

// definitions.h:1608 is_inter_singleref_mode folded through isInterMode:
// NEARESTMV <= mode < NEAREST_NEARESTMV (the single-ref block).
static bool isInterSinglerefMode(l16_rdo::PredictionMode mode) {
    return static_cast<std::int32_t>(mode) >= l16_rdo::NEARESTMV &&
        static_cast<std::int32_t>(mode) < l16_rdo::NEAREST_NEARESTMV;
}

// product_coding_loop.c:1040-:1136 obmc_trans_face_off.
void obmcTransFaceOff(MdCandBf* cand_bf, FlCtx* ctx, const PicBuf& input_pic, std::uint32_t input_origin_index) {
    // 10bit lambda is shifted left by 4 to account for the difference in range between 8bit/10bit SSE. The 10bit variance function
    // automatically shifts the variance so the variance of 10bit will match the range of 8bit. Therefore, the shifted lambda is not
    // needed, and as such, the shift is nullified here so the lambda/distortion are properly proportioned.
    const std::uint64_t full_lambda = ctx->hbd_md ? static_cast<std::uint64_t>(ctx->full_lambda_md[1]) >> 4
                                                  : ctx->full_lambda_md[0];
    MdCand* cand = cand_bf->cand;
    const PicBuf& pred = cand_bf->pred;

    if (isInterSinglerefMode(cand->mode)) {
        const std::int32_t rf[2] = {cand->ref_frame[0], cand->ref_frame[1]};

        const std::uint8_t is_obmc_allowed =
            ctx->obmcAllowedFn(*ctx, *cand) ? 1 : 0;  // == OBMC_CAUSAL

        if (isInterMode(cand->mode) && is_obmc_allowed && cand->motion_mode == kSimpleTranslation &&
            cand->is_interintra_used == 0) {
            // Derive the fast luma rate of OBMC
            const int last_motion_mode_allowed = ctx->motionModeAllowedFn(*ctx, *cand);
            std::int32_t inter_mode_bits_num_translation = 0;
            std::int32_t inter_mode_bits_num_obmc = 0;

            switch (last_motion_mode_allowed) {
            case kSimpleTranslation:
                break;
            case kObmcCausal:
                inter_mode_bits_num_translation =
                    ctx->motionModeFacBits1[static_cast<int>(ctx->geom.bsize) * 2 + 0];
                inter_mode_bits_num_obmc = ctx->motionModeFacBits1[static_cast<int>(ctx->geom.bsize) * 2 + 1];
                break;
            default:
                inter_mode_bits_num_translation = ctx->motionModeFacBits[static_cast<int>(ctx->geom.bsize) *
                                                                             static_cast<int>(kSimpleTranslation)];
                inter_mode_bits_num_obmc =
                    ctx->motionModeFacBits[static_cast<int>(ctx->geom.bsize) * static_cast<int>(kObmcCausal)];
                break;
            }
            const std::uint64_t obmc_fast_luma_rate =
                cand_bf->fast_luma_rate + inter_mode_bits_num_obmc - inter_mode_bits_num_translation;
            std::uint64_t luma_fast_dist;
            // Take a copy of the simple-translation results
            const std::uint64_t simple_translation_cost = *cand_bf->fast_cost;
            const std::uint64_t simple_translation_fast_luma_rate = cand_bf->fast_luma_rate;
            const std::uint64_t simple_translation_fast_chroma_rate = cand_bf->fast_chroma_rate;
            const std::uint64_t simple_translation_luma_fast_distortion = cand_bf->luma_fast_dist;
            // Modify the motion-mode
            cand->motion_mode = kObmcCausal;

            // Prediction
            ctx->uv_intra_comp_only = false;
            ctx->obmcPredFn(*ctx, *cand_bf);

            // Distortion
            std::uint32_t sse;
            if (!ctx->hbd_md) {
                VarianceFn vf = mefnVf(ctx->geom.bsize);
                const std::uint8_t* pred_y = pred.buf;
                const std::uint8_t* src_y = input_pic.buf + input_origin_index;
                cand_bf->luma_fast_dist = vf(pred_y, pred.stride, src_y, input_pic.stride, &sse);
            } else {
                const std::uint16_t* pred_y = reinterpret_cast<const std::uint16_t*>(pred.buf);
                const std::uint16_t* src_y = reinterpret_cast<const std::uint16_t*>(input_pic.buf) + input_origin_index;
                cand_bf->luma_fast_dist = mefnVfHbd10(pred_y, pred.stride, src_y, input_pic.stride,
                                                      ctx->geom.bwidth, ctx->geom.bheight, &sse);
            }
            // Shift variance by 4 because we use full lambda in the cost (since variance is proportional to sse)
            // and full lambda is set with the expectation the variance is a squared metric shifted by 4 (the same
            // shift is applied to sse in the full loop)
            luma_fast_dist = cand_bf->luma_fast_dist << 4;
            cand_bf->fast_luma_rate = obmc_fast_luma_rate;
            *cand_bf->fast_cost =
                static_cast<std::uint64_t>(rdCost(full_lambda, cand_bf->fast_luma_rate + cand_bf->fast_chroma_rate,
                                                  luma_fast_dist));
            if (simple_translation_cost < *cand_bf->fast_cost) {
                // Restore the simple-translation results
                cand->motion_mode = kSimpleTranslation;
                *cand_bf->fast_cost = simple_translation_cost;
                cand_bf->fast_luma_rate = simple_translation_fast_luma_rate;
                cand_bf->fast_chroma_rate = simple_translation_fast_chroma_rate;
                cand_bf->luma_fast_dist = simple_translation_luma_fast_distortion;
                cand_bf->valid_luma_pred = 0;
            } else {
                cand_bf->valid_luma_pred = 1;
            }
        }
    }
}

// product_coding_loop.c:1209-:1293 fast_loop_core.
void fastLoopCore(MdCandBf* cand_bf, FlCtx* ctx, const PicBuf& input_pic, std::uint32_t input_origin_index) {
    std::uint64_t luma_fast_dist;
    // 10bit lambda is shifted left by 4 to account for the difference in range between 8bit/10bit SSE. The 10bit variance function
    // automatically shifts the variance so the variance of 10bit will match the range of 8bit. Therefore, the shifted lambda is not
    // needed, and as such, the shift is nullified here so the lambda/distortion are properly proportioned.
    const std::uint64_t full_lambda = ctx->hbd_md ? static_cast<std::uint64_t>(ctx->full_lambda_md[1]) >> 4
                                                  : ctx->full_lambda_md[0];
    MdCand* cand = cand_bf->cand;
    const PicBuf& pred = cand_bf->pred;
    // Prediction
    ctx->uv_intra_comp_only = false;
    ctx->predTable[isInterMode(cand->mode) || cand->use_intrabc ? 1 : 0](*ctx, *cand_bf);
    if (ctx->mds0_use_hadamard_blk) {
        std::uint32_t satd = 0;
        pipeline::hadamardPath(input_pic.buf, input_pic.stride, input_origin_index, pred.buf, pred.stride,
                               reinterpret_cast<std::int16_t*>(cand_bf->residual.buf), cand_bf->residual.stride,
                               ctx->txCoeffBuf, ctx->geom.bsize, ctx->hbd_md != 0, &satd);
        cand_bf->luma_fast_dist = satd;

        luma_fast_dist = cand_bf->luma_fast_dist << 4;
    } else {
        // Distortion
        std::uint32_t sse;
        if (!ctx->hbd_md) {
            VarianceFn vf = mefnVf(ctx->geom.bsize);
            const std::uint8_t* pred_y = pred.buf;
            const std::uint8_t* src_y = input_pic.buf + input_origin_index;
            cand_bf->luma_fast_dist = vf(pred_y, pred.stride, src_y, input_pic.stride, &sse);
        } else {
            const std::uint16_t* pred_y = reinterpret_cast<const std::uint16_t*>(pred.buf);
            const std::uint16_t* src_y = reinterpret_cast<const std::uint16_t*>(input_pic.buf) + input_origin_index;
            cand_bf->luma_fast_dist = mefnVfHbd10(pred_y, pred.stride, src_y, input_pic.stride, ctx->geom.bwidth,
                                                  ctx->geom.bheight, &sse);
        }
        // Shift variance by 4 because we use full lambda in the cost (since variance is proportional to sse)
        // and full lambda is set with the expectation the variance is a squared metric shifted by 4 (the same
        // shift is applied to sse in the full loop)
        luma_fast_dist = cand_bf->luma_fast_dist << 4;
    }
    if (ctx->pruning_method_th && ctx->pd_pass == 1) {
        if (ctx->pruning_method_th != (std::uint8_t)~0 &&
            (minU32(ctx->md_me_dist, ctx->md_pme_dist) / (ctx->geom.bwidth * ctx->geom.bheight)) >
                ctx->pruning_method_th) {
            if (ctx->per_class_dist_to_cost_th[cand_bf->cand->cand_class] != (std::uint16_t)~0 &&
                ctx->mds0_best_cost_per_class[cand_bf->cand->cand_class] != (std::uint64_t)~0) {
                const std::uint64_t distortion_cost =
                    static_cast<std::uint64_t>(rdCost(full_lambda, 0, luma_fast_dist));
                if ((100 * static_cast<std::int64_t>(static_cast<std::int64_t>(distortion_cost) -
                                                     static_cast<std::int64_t>(
                                                         ctx->mds0_best_cost_per_class[cand_bf->cand->cand_class]))) >
                    (static_cast<std::int64_t>(ctx->mds0_best_cost_per_class[cand_bf->cand->cand_class]) *
                     ctx->per_class_dist_to_cost_th[cand_bf->cand->cand_class])) {
                    *cand_bf->fast_cost = kMaxModeCost;
                    return;
                }
            }
        } else {
            if (ctx->dist_to_cost_th != (std::uint16_t)~0 && ctx->mds0_best_cost != (std::uint64_t)~0) {
                const std::uint64_t distortion_cost = static_cast<std::uint64_t>(rdCost(full_lambda, 0, luma_fast_dist));
                if ((100 * static_cast<std::int64_t>(static_cast<std::int64_t>(distortion_cost) -
                                                     static_cast<std::int64_t>(ctx->mds0_best_cost))) >
                    (static_cast<std::int64_t>(ctx->mds0_best_cost) * ctx->dist_to_cost_th)) {
                    *cand_bf->fast_cost = kMaxModeCost;
                    return;
                }
            }
        }
    }
    // Fast Cost
    if (ctx->shut_fast_rate) {
        *cand_bf->fast_cost = luma_fast_dist;
        cand_bf->fast_luma_rate = 0;
        cand_bf->fast_chroma_rate = 0;
    } else {
        *cand_bf->fast_cost = ctx->fastCostTable[isInterMode(cand->mode)](*ctx, *cand_bf, full_lambda, luma_fast_dist);
    }
    cand_bf->valid_luma_pred = 1;

    if (ctx->obmc_enabled && ctx->obmc_trans_face_off == 1) {
        obmcTransFaceOff(cand_bf, ctx, input_pic, input_origin_index);
    }
    // Init full cost in case we bypass stage1/stage2
    *cand_bf->full_cost = *cand_bf->fast_cost;
}

// product_coding_loop.c:1500-:1580 process_cand_itr: returns true if the
// candidate should be processed during the current MDS0 iteration.
bool processCandItr(const FlCtx& ctx, const MdCand& cand, std::uint8_t itr, l16_rdo::PredictionMode best_reg_intra_mode,
                    std::uint64_t best_reg_intra_cost, const std::uint64_t* regular_intra_cost) {
    const bool any_skip_angular = ctx.skip_angular_delta1_th != -1 || ctx.skip_angular_delta2_th != -1 ||
        ctx.skip_angular_delta3_th != -1;
    if (itr == 0) {
        if (ctx.reduce_filter_intra && any_skip_angular) {
            // Eval regular only if itr=0 (i.e. skip angular and skip filter)
            if (cand.angle_delta_y != 0 || cand.filter_intra_mode != 5 /* FILTER_INTRA_MODES */) {
                return false;
            }
        } else if (any_skip_angular) {
            // Eval regular and filter intra only if itr=0 (i.e. skip angular)
            if (cand.angle_delta_y != 0) {
                return false;
            }
        } else {  // if (ctx->cand_reduction_ctrls.reduce_filter_intra)
            // Eval regular and angular only if itr=0 (i.e. skip filter)
            if (cand.filter_intra_mode != 5 /* FILTER_INTRA_MODES */) {
                return false;
            }
        }
    } else {
        if (ctx.reduce_filter_intra && any_skip_angular) {
            // Eval angular and filter intra if itr=1 (i.e. skip regular)
            if (cand.angle_delta_y == 0 && cand.filter_intra_mode == 5 /* FILTER_INTRA_MODES */) {
                return false;
            }
        } else if (any_skip_angular) {
            // Eval angular only if itr=1 (i.e. skip regular and skip filter intra)
            if (cand.angle_delta_y == 0) {
                return false;
            }
        } else {  // if (ctx->cand_reduction_ctrls.reduce_filter_intra)
            // Eval filter intra only if itr=1 (i.e. skip regular and skip angular)
            if (cand.filter_intra_mode == 5 /* FILTER_INTRA_MODES */) {
                return false;
            }
        }
        // Use regular info to reduce angular processing
        // Eval the child-angular if the parent-angular is the best out of the regular modes (i.e. itr=0 eval)
        if (cand.filter_intra_mode == 5 /* FILTER_INTRA_MODES */) {
            if (best_reg_intra_mode != cand.mode) {
                if (ctx.skip_angular_delta1_th != -1 && (cand.angle_delta_y < 0 ? -cand.angle_delta_y : cand.angle_delta_y) == 1 &&
                    (((regular_intra_cost[cand.mode] - (best_reg_intra_cost > 1 ? best_reg_intra_cost : 1)) * 100) >
                     ctx.skip_angular_delta1_th * best_reg_intra_cost)) {
                    return false;
                } else if (ctx.skip_angular_delta2_th != -1 &&
                           (cand.angle_delta_y < 0 ? -cand.angle_delta_y : cand.angle_delta_y) == 2 &&
                           (((regular_intra_cost[cand.mode] - (best_reg_intra_cost > 1 ? best_reg_intra_cost : 1)) * 100) >
                            ctx.skip_angular_delta2_th * best_reg_intra_cost)) {
                    return false;
                } else if (ctx.skip_angular_delta3_th != -1 &&
                           (cand.angle_delta_y < 0 ? -cand.angle_delta_y : cand.angle_delta_y) == 3 &&
                           (((regular_intra_cost[cand.mode] - (best_reg_intra_cost > 1 ? best_reg_intra_cost : 1)) * 100) >
                            ctx.skip_angular_delta3_th * best_reg_intra_cost)) {
                    return false;
                }
            }
        } else {
            // Eval filter candidates
            // Always test FILTER_DC candidate
            if (cand.filter_intra_mode == 0 /* FILTER_DC_PRED */) {
                return true;
            }

            // Only test other filter modes if the corresponding non-filter mode was chosen during the first pass
            if (l16_rdo::fimodeToIntradir[cand.filter_intra_mode] != best_reg_intra_mode) {
                return false;
            }
        }
    }
    return true;
}

// filter.h:64-68 av1_make_interp_filters.
std::uint32_t av1MakeInterpFilters(std::uint32_t y_filter, std::uint32_t x_filter) {
    const std::uint16_t y16 = static_cast<std::uint16_t>(y_filter & 0xffff);
    const std::uint16_t x16 = static_cast<std::uint16_t>(x_filter & 0xffff);
    return y16 | (static_cast<std::uint32_t>(x16) << 16);
}

// filter.h:70-72 av1_broadcast_interp_filter.
std::uint32_t av1BroadcastInterpFilter(std::uint32_t filter) {
    return av1MakeInterpFilters(filter, filter);
}

// product_coding_loop.c:1448-:1464 md_stage_0_pd0.
void mdStage0Pd0(FlCtx* ctx, const PicBuf& input_pic, std::uint32_t input_origin_index, std::uint32_t blk_origin_index,
                 MdCand* fast_cand_array, std::uint32_t fast_cand_count) {
    std::uint32_t cand_buff_idx = 0;
    for (std::uint32_t cand_idx = 0; cand_idx < fast_cand_count; cand_idx++) {
        MdCandBf* cand_bf = ctx->cand_bf_ptr_array[cand_buff_idx];
        cand_bf->cand = &fast_cand_array[cand_idx];

        // Initialize tx_depth
        cand_bf->cand->tx_depth = 0;
        fastLoopCorePd0(cand_bf, ctx, input_pic, input_origin_index, blk_origin_index);
        if (*cand_bf->fast_cost < ctx->mds0_best_cost) {
            ctx->mds0_best_cost = *cand_bf->fast_cost;
            ctx->mds0_best_idx = cand_buff_idx;
            cand_buff_idx = !cand_buff_idx;
        }
    }
}

// product_coding_loop.c:1466-:1498 md_stage_0_light_pd1.
void mdStage0LightPd1(FlCtx* ctx, MdCandBf** cand_bf_ptr_array_base, MdCand* fast_cand_array,
                      std::uint32_t fast_cand_count, const PicBuf& input_pic, std::uint32_t input_origin_index,
                      std::uint32_t frame_interp_filter) {
    // Set MD Staging fast_loop_core settings
    ctx->mds_do_chroma = false;
    /* If the interpolation filter type is assigned at the picture level, use that value, OW use regular.
     * NB intra_bc always uses BILINEAR, but IBC is not allowed in LPD1. */
    const std::uint32_t default_interp_filter = (frame_interp_filter == kSwitchable)
        ? 0
        : av1BroadcastInterpFilter(frame_interp_filter);

    // 2nd fast loop: src-to-recon
    std::uint32_t cand_buff_idx = 0;

    for (std::uint32_t cand_idx = 0; cand_idx < fast_cand_count; cand_idx++) {
        MdCandBf* cand_bf = cand_bf_ptr_array_base[cand_buff_idx];
        cand_bf->cand = &fast_cand_array[cand_idx];

        cand_bf->cand->tx_depth = 0;
        cand_bf->cand->interp_filters = default_interp_filter;
        // Prediction
        fastLoopCoreLightPd1(cand_bf, ctx, input_pic, input_origin_index);

        if (*cand_bf->fast_cost < ctx->mds0_best_cost) {
            ctx->mds0_best_cost = *cand_bf->fast_cost;
            ctx->mds0_best_idx = cand_buff_idx;
            cand_buff_idx = !cand_buff_idx;
        }
    }
}

// md_process.c:29-:33 uni_psy_bias.
const std::uint8_t uniPsyBias[64] = {
    85, 85, 85, 85, 85, 85, 85, 85, 85, 85, 85, 85, 85, 85, 85, 85, 95, 95, 95, 95, 95, 95,
    95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95, 95,
    95, 95, 95, 95, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
};

// product_coding_loop.c:1582-:1714 md_stage_0.
void mdStage0(FlCtx* ctx, MdCandBf** cand_bf_ptr_array_base, MdCand* fast_candidate_array,
              std::uint32_t fast_cand_count, const PicBuf& input_pic, std::uint32_t input_origin_index,
              std::uint32_t cand_bf_start_index, std::uint32_t max_buffers, std::uint8_t unipred_bias,
              std::uint8_t picture_qp, std::uint32_t frame_interp_filter) {
    const std::uint8_t apply_unipred_bias = unipred_bias;
    // Set MD Staging fast_loop_core settings
    ctx->mds_do_ifs = (ctx->ifs_level == 1 /* IFS_MDS0 */);
    /* If the interpolation filter type is known, assign it, OW will be assigned in IFS search.
     * NB intra_bc always uses BILINEAR, but the IBC filters are updated automatically during prediction, so
     * no need for a special check here. */
    const std::uint32_t default_interp_filter = (frame_interp_filter == kSwitchable)
        ? 0
        : av1BroadcastInterpFilter(frame_interp_filter);
    ctx->mds_do_chroma = false;
    // 2nd fast loop: src-to-recon
    std::uint32_t highest_cost_index = cand_bf_start_index;

    // Process CLASS_0 candidates through iterations at mds0;
    // 1st iteration : evaluate the regular mode(s).
    //
    // 2nd iteration : evaluate the angular (pAngle!=0) mode(s) and filter-intra modes
    //    - skip pAngle !=0 of a given mode, if pAngle==0 of the same mode is not the best after the 1st iteration
    //      and if the cost to the best is significant
    //    - skip filter-intra mode if regular winner does not match (e.g. test filter-paeth if Paeth is best at first pass). Always test filter-DC.
    const std::uint8_t tot_itr = (ctx->target_class != 0 ||
        (!ctx->reduce_filter_intra && !(ctx->skip_angular_delta1_th != -1 || ctx->skip_angular_delta2_th != -1 ||
                                        ctx->skip_angular_delta3_th != -1)))
        ? 1
        : 2;
    std::uint64_t best_reg_intra_cost = kMaxCuCost;              // Derived at the 1st itr
    l16_rdo::PredictionMode best_reg_intra_mode = l16_rdo::MB_MODE_COUNT;  // INTRA_INVALID
    std::uint64_t regular_intra_cost[l16_rdo::PAETH_PRED + 1];
    for (unsigned i = 0; i < l16_rdo::PAETH_PRED + 1; i++) {
        regular_intra_cost[i] = kMaxCuCost;
    }

    std::uint32_t tot_processed_cand = 0;

    for (std::uint8_t itr = 0; itr < tot_itr; itr++) {
        for (std::uint32_t cand_idx = 0; cand_idx < fast_cand_count; cand_idx++) {
            if (fast_candidate_array[cand_idx].cand_class != ctx->target_class) {
                continue;
            }

            MdCandBf* cand_bf = cand_bf_ptr_array_base[highest_cost_index];
            MdCand* cand = cand_bf->cand = &fast_candidate_array[cand_idx];
            if (ctx->prune_using_best_mode && cand->cand_class == 0 && itr == 0) {
                // If (V better than DC), then skip H
                if (cand->mode == l16_rdo::H_PRED && best_reg_intra_mode == l16_rdo::V_PRED) {
                    continue;
                }
                // If DC better than H and better than V, then skip Smooth
                if ((cand->mode == l16_rdo::SMOOTH_PRED || cand->mode == l16_rdo::SMOOTH_V_PRED ||
                     cand->mode == l16_rdo::SMOOTH_H_PRED) &&
                    best_reg_intra_mode == l16_rdo::DC_PRED) {
                    continue;
                }
            }
            cand->tx_depth = 0;
            cand->interp_filters = default_interp_filter;
            // Check whether a candidate should be considered in the current iteration
            if (tot_itr > 1) {
                if (!processCandItr(*ctx, *cand, itr, best_reg_intra_mode, best_reg_intra_cost, regular_intra_cost)) {
                    continue;
                }
            }

            // Perform prediction and calculate cost
            fastLoopCore(cand_bf, ctx, input_pic, input_origin_index);

            tot_processed_cand++;

            if (apply_unipred_bias && isInterSinglerefMode(cand_bf->cand->mode)) {
                *cand_bf->fast_cost = (*cand_bf->fast_cost * uniPsyBias[picture_qp]) / 100;
            }
            if (*cand_bf->fast_cost < ctx->mds0_best_cost) {
                ctx->mds0_best_cost = *cand_bf->fast_cost;
                ctx->mds0_best_class = cand->cand_class;
                if (cand->cand_class == 0) {
                    ctx->mds0_best_class0_cost = *cand_bf->fast_cost;
                }
            }
            if (*cand_bf->fast_cost < ctx->mds0_best_cost_per_class[cand_bf->cand->cand_class]) {
                ctx->mds0_best_cost_per_class[cand_bf->cand->cand_class] = *cand_bf->fast_cost;
            }
            if (cand->cand_class == 0 && itr == 0 && cand->filter_intra_mode == 5 /* FILTER_INTRA_MODES */ &&
                ((ctx->prune_using_best_mode && ctx->intra_mode_end >= l16_rdo::H_PRED) || tot_itr > 1)) {
                regular_intra_cost[cand->mode] = *cand_bf->fast_cost;

                if (*cand_bf->fast_cost < best_reg_intra_cost) {
                    best_reg_intra_cost = *cand_bf->fast_cost;
                    best_reg_intra_mode = cand->mode;
                }
            }

            // Get the candidate buffer to use for processing the next candidate.
            // The buffer will be an empty buffer or the buffer of the candidate with the
            // highest cost so far.
            if (tot_processed_cand < max_buffers) {
                highest_cost_index++;
            } else {
                const std::uint64_t* fast_cost_array = ctx->fast_cost_array;
                const std::uint32_t buffer_index_start = cand_bf_start_index;
                const std::uint32_t buffer_index_end = buffer_index_start + max_buffers;
                if (max_buffers == 2) {
                    highest_cost_index = fast_cost_array[buffer_index_start] < fast_cost_array[buffer_index_start + 1]
                        ? buffer_index_start + 1
                        : buffer_index_start;
                } else {
                    highest_cost_index = buffer_index_start;
                    std::uint64_t highest_cost = fast_cost_array[buffer_index_start];
                    for (std::uint32_t buff = buffer_index_start + 1; buff < buffer_index_end; buff++) {
                        if (fast_cost_array[buff] > highest_cost) {
                            highest_cost_index = buff;
                            highest_cost = fast_cost_array[buff];
                        }
                    }
                }
            }
        }
    }

    //if pruning happened, update MDS1 count accordingly to not process invalid candidates in subsequent MD stages
    ctx->md_stage_1_count[ctx->target_class] =
        ctx->md_stage_1_count[ctx->target_class] < tot_processed_cand ? ctx->md_stage_1_count[ctx->target_class]
                                                                      : tot_processed_cand;

    // Set the cost of the scratch candidate to max to get discarded @ the sorting phase
    *(cand_bf_ptr_array_base[highest_cost_index]->fast_cost) = kMaxCuCost;
}

// product_coding_loop.c:1771-:1840 md_full_pel_search_large_lbd. We cannot
// use sparse_search_step with mpsad for search_position_start_x/
// search_position_end_x, so for x dimension we assume sparse_search_step
// is always 1.
void mdFullPelSearchLargeLbd(const pipeline::SvtMvCostParam& mv_cost_params, FlCtx* ctx, const PicBuf& input_pic,
                             const PicBuf& ref_pic, std::uint32_t input_origin_index, std::int16_t mvx,
                             std::int16_t mvy, std::int16_t search_position_start_x,
                             std::int16_t search_position_end_x, std::int16_t search_position_start_y,
                             std::int16_t search_position_end_y, std::int16_t sparse_search_step,
                             std::int16_t* best_mvx, std::int16_t* best_mvy, std::uint32_t* best_cost) {
    std::int32_t ref_origin_index = (ctx->blk_org_x + (mvx >> 3) + search_position_start_x) +
        (ctx->blk_org_y + (mvy >> 3) + search_position_start_y) * static_cast<std::int32_t>(ref_pic.stride);

    std::int16_t remain_search_area = 8 - ((search_position_end_x - search_position_start_x) % 8);
    remain_search_area = remain_search_area == 8 ? 0 : remain_search_area;
    search_position_end_x = search_position_end_x > search_position_end_x + remain_search_area
        ? search_position_end_x
        : static_cast<std::int16_t>(search_position_end_x + remain_search_area);
    const std::int32_t search_area_width_i = search_position_end_x - search_position_start_x;
    const std::uint32_t search_area_height = search_position_end_y - search_position_start_y + 1;
    assert(!(search_area_width_i & 7));
    if (search_area_width_i & ~0x7) {
        pipeline::svtPmeSadLoopKernel(&mv_cost_params,
                                      input_pic.buf + input_origin_index,
                                      input_pic.stride,
                                      ref_pic.buf + ref_origin_index,
                                      ref_pic.stride,
                                      ctx->geom.bheight,
                                      ctx->geom.bwidth,
                                      best_cost,
                                      best_mvx,
                                      best_mvy,
                                      search_position_start_x,
                                      search_position_start_y,
                                      static_cast<std::int16_t>(search_area_width_i & ~0x7),  // multiple of 8
                                      static_cast<std::int16_t>(search_area_height),
                                      sparse_search_step,
                                      mvx,
                                      mvy);
    }

    if (search_area_width_i & 7) {
        for (std::int32_t refinement_pos_y = search_position_start_y; refinement_pos_y <= search_position_end_y;
             refinement_pos_y = refinement_pos_y + sparse_search_step) {
            std::int32_t refinement_pos_x = search_position_start_x + (search_area_width_i & ~0x7);
            for (; refinement_pos_x <= search_position_end_x; refinement_pos_x++) {
                ref_origin_index = (ctx->blk_org_x + (mvx >> 3) + refinement_pos_x) +
                    (ctx->blk_org_y + (mvy >> 3) + refinement_pos_y) * static_cast<std::int32_t>(ref_pic.stride);

                const std::uint32_t cost = motion::nxmSadKernelHelper(
                    input_pic.buf + input_origin_index, input_pic.stride, ref_pic.buf + ref_origin_index,
                    ref_pic.stride, ctx->geom.bheight, ctx->geom.bwidth);

                Mv2 best_mv;
                best_mv.x = static_cast<std::int16_t>(mvx + (refinement_pos_x * 8));
                best_mv.y = static_cast<std::int16_t>(mvy + (refinement_pos_y * 8));
                const std::uint32_t cost_with_err =
                    cost + static_cast<std::uint32_t>(pipeline::fpMvErrCost(best_mv, mv_cost_params));
                if (cost_with_err < *best_cost) {
                    *best_mvx = best_mv.x;
                    *best_mvy = best_mv.y;
                    *best_cost = cost_with_err;
                }
            }
        }
    }
}

// product_coding_loop.c:1855-:1990 md_full_pel_search.
void mdFullPelSearch(FlCtx* ctx, const PicBuf& input_pic, const PicBuf& ref_pic, std::uint32_t input_origin_index,
                     DistortionType dist_type, std::int16_t mvx, std::int16_t mvy,
                     std::int16_t search_position_start_x, std::int16_t search_position_end_x,
                     std::int16_t search_position_start_y, std::int16_t search_position_end_y,
                     std::int16_t sparse_search_step, std::uint8_t is_sprs_lev0_performed, std::int16_t* best_mvx,
                     std::int16_t* best_mvy, std::uint32_t* best_cost, std::uint8_t hbd_md, std::uint8_t base_q_idx) {
    // Mvcost params
    pipeline::SvtMvCostParam mv_cost_params;
    const std::uint32_t rdmult = dist_type != DIST_SAD
        ? (hbd_md ? ctx->full_lambda_md[1] : ctx->full_lambda_md[0])
        : (hbd_md ? ctx->fast_lambda_md[1] : ctx->fast_lambda_md[0]);
    pipeline::InitMvCostParamsCtx init_ctx;
    init_ctx.sq_size = ctx->geom.sq_size;
    init_ctx.skip_diag_refinement = ctx->md_subpel_skip_diag_refinement;
    init_ctx.nmv_vec_cost = ctx->nmv_vec_cost;
    init_ctx.nmvcoststack0 = ctx->nmvcoststack0;
    init_ctx.nmvcoststack1 = ctx->nmvcoststack1;
    pipeline::svtInitMvCostParams(init_ctx, &mv_cost_params, &ctx->ref_mv, base_q_idx, rdmult, hbd_md);
    std::uint32_t cost;
    // Search area adjustment
    if ((ctx->blk_org_x + (mvx >> 3) + search_position_start_x) < (-ref_pic.border + 1)) {
        search_position_start_x =
            static_cast<std::int16_t>((-ref_pic.border + 1) - (ctx->blk_org_x + (mvx >> 3)));
    }

    if ((ctx->blk_org_x + static_cast<std::int32_t>(ctx->geom.bwidth) + (mvx >> 3) + search_position_end_x) >
        (ref_pic.border + static_cast<std::int32_t>(ref_pic.max_width) - 1)) {
        search_position_end_x = static_cast<std::int16_t>((ref_pic.border + static_cast<std::int32_t>(ref_pic.max_width) - 1) -
            (ctx->blk_org_x + static_cast<std::int32_t>(ctx->geom.bwidth) + (mvx >> 3)));
    }

    if ((ctx->blk_org_y + (mvy >> 3) + search_position_start_y) < (-ref_pic.border + 1)) {
        search_position_start_y =
            static_cast<std::int16_t>((-ref_pic.border + 1) - (ctx->blk_org_y + (mvy >> 3)));
    }

    if ((ctx->blk_org_y + static_cast<std::int32_t>(ctx->geom.bheight) + (mvy >> 3) + search_position_end_y) >
        (ref_pic.border + static_cast<std::int32_t>(ref_pic.max_height) - 1)) {
        search_position_end_y = static_cast<std::int16_t>((ref_pic.border + static_cast<std::int32_t>(ref_pic.max_height) - 1) -
            (ctx->blk_org_y + static_cast<std::int32_t>(ctx->geom.bheight) + (mvy >> 3)));
    }
    if (dist_type == DIST_SAD && ctx->enable_psad) {
        if (!hbd_md && (search_position_end_x - search_position_start_x) >= 7) {
            mdFullPelSearchLargeLbd(mv_cost_params,
                                    ctx,
                                    input_pic,
                                    ref_pic,
                                    input_origin_index,
                                    mvx,
                                    mvy,
                                    search_position_start_x,
                                    search_position_end_x,
                                    search_position_start_y,
                                    search_position_end_y,
                                    sparse_search_step,
                                    best_mvx,
                                    best_mvy,
                                    best_cost);
            return;
        }
    }
    for (std::int32_t refinement_pos_x = search_position_start_x; refinement_pos_x <= search_position_end_x;
         refinement_pos_x = refinement_pos_x + sparse_search_step) {
        for (std::int32_t refinement_pos_y = search_position_start_y; refinement_pos_y <= search_position_end_y;
             refinement_pos_y = refinement_pos_y + sparse_search_step) {
            // If sparse search level_1, and if search level_0 previously performed
            if (sparse_search_step == 2 && is_sprs_lev0_performed) {
                // If level_0 range
                if ((refinement_pos_x + (mvx >> 3)) >= ctx->sprs_lev0_start_x &&
                    (refinement_pos_x + (mvx >> 3)) <= ctx->sprs_lev0_end_x &&
                    (refinement_pos_y + (mvy >> 3)) >= ctx->sprs_lev0_start_y &&
                    (refinement_pos_y + (mvy >> 3)) <= ctx->sprs_lev0_end_y) {
                    // If level_0 position
                    if (refinement_pos_x % 4 == 0 && refinement_pos_y % 4 == 0) {
                        continue;
                    }
                }
            }
            const std::int32_t ref_origin_index = (ctx->blk_org_x + (mvx >> 3) + refinement_pos_x) +
                (ctx->blk_org_y + (mvy >> 3) + refinement_pos_y) * static_cast<std::int32_t>(ref_pic.stride);

            if (dist_type == DIST_VAR) {
                if (!hbd_md) {
                    VarianceFn vf = mefnVf(ctx->geom.bsize);
                    const std::uint8_t* pred_y = ref_pic.buf + ref_origin_index;
                    const std::uint8_t* src_y = input_pic.buf + input_origin_index;
                    std::uint32_t sse;
                    cost = vf(pred_y, static_cast<std::int32_t>(ref_pic.stride), src_y,
                              static_cast<std::int32_t>(input_pic.stride), &sse);
                } else {
                    const std::uint16_t* pred_y =
                        reinterpret_cast<const std::uint16_t*>(ref_pic.buf) + ref_origin_index;
                    const std::uint16_t* src_y =
                        reinterpret_cast<const std::uint16_t*>(input_pic.buf) + input_origin_index;
                    std::uint32_t sse;
                    cost = mefnVfHbd10(pred_y, ref_pic.stride, src_y, input_pic.stride, ctx->geom.bwidth,
                                       ctx->geom.bheight, &sse);
                }
            } else if (dist_type == DIST_SSD) {
                const std::uint64_t dist = hbd_md
                    ? picops::fullDistortionKernel16Bits(reinterpret_cast<const std::uint16_t*>(input_pic.buf),
                                                         input_origin_index, input_pic.stride,
                                                         reinterpret_cast<const std::uint16_t*>(ref_pic.buf),
                                                         ref_origin_index, ref_pic.stride, ctx->geom.bwidth,
                                                         ctx->geom.bheight)
                    : picops::spatialFullDistortion(input_pic.buf, input_origin_index, input_pic.stride, ref_pic.buf,
                                                    ref_origin_index, ref_pic.stride, ctx->geom.bwidth,
                                                    ctx->geom.bheight);
                cost = static_cast<std::uint32_t>(dist);
            } else {
                if (hbd_md) {
                    cost = motion::sad16b(reinterpret_cast<const std::uint16_t*>(input_pic.buf) + input_origin_index,
                                          input_pic.stride,
                                          reinterpret_cast<const std::uint16_t*>(ref_pic.buf) + ref_origin_index,
                                          ref_pic.stride, ctx->geom.bheight, ctx->geom.bwidth);
                } else {
                    cost = motion::nxmSadKernelHelper(input_pic.buf + input_origin_index, input_pic.stride,
                                                      ref_pic.buf + ref_origin_index, ref_pic.stride,
                                                      ctx->geom.bheight, ctx->geom.bwidth);
                }
            }
            Mv2 best_mv;
            best_mv.x = static_cast<std::int16_t>(mvx + (refinement_pos_x * 8));
            best_mv.y = static_cast<std::int16_t>(mvy + (refinement_pos_y * 8));
            cost += static_cast<std::uint32_t>(pipeline::fpMvErrCost(best_mv, mv_cost_params));
            if (cost < *best_cost) {
                *best_mvx = best_mv.x;
                *best_mvy = best_mv.y;
                *best_cost = cost;
            }
        }
    }
}

// mode_decision.h:206 MAX_MD_NSQ_SARCH_MVC_CNT... no: product_coding_loop.c
// :2019 #define MAX_MD_NSQ_SARCH_MVC_CNT 6.
constexpr std::uint32_t kMaxMdNsqSearchMvcCnt = 6;

// product_coding_loop.c:2021-:2190 md_nsq_motion_search.
void mdNsqMotionSearch(FlCtx* ctx, const PicBuf& input_pic, const PicBuf& ref_pic, std::uint32_t input_origin_index,
                       std::uint8_t rf, const MdMeSbResults* me_results, std::int16_t* me_mv_x, std::int16_t* me_mv_y,
                       const MdNsqCtrls& nsq_ctrls, std::uint8_t max_refs, std::uint8_t max_l0,
                       std::uint8_t enable_me_8x8, std::uint8_t enable_me_16x16,
                       std::uint8_t max_number_of_pus_per_sb, std::uint8_t max_cand, std::uint8_t base_q_idx) {
    const std::uint8_t list_idx = motion::amvp::getListIdx(rf);
    const std::uint8_t ref_idx = motion::amvp::getRefFrameIdx(rf);
    // Step 0: derive the MVC list for the NSQ search; 1 SQ MV (default MV for NSQ) and up to 4 sub-block MV(s) (e.g. if 16x8 then 2 8x8, if 32x8 then 4 8x8)
    std::int16_t mvc_x_array[kMaxMdNsqSearchMvcCnt];
    std::int16_t mvc_y_array[kMaxMdNsqSearchMvcCnt];
    std::int8_t mvc_count = 0;
    // The l16 is_me_data_present view over the carrier's arrays.
    l16_rdo::MeSbResults me_view;
    me_view.totalMeCandidateIndex = me_results->total_me_candidate_index;
    me_view.meCandidateArray = me_results->me_candidate_array;
    ctx->enable_psad = nsq_ctrls.enable_psad;
    // SQ MV (default MVC for NSQ)
    mvc_x_array[mvc_count] = *me_mv_x;
    mvc_y_array[mvc_count] = *me_mv_y;
    mvc_count++;
    if ((ctx->geom.bwidth != 4 && ctx->geom.bheight != 4) && ctx->geom.sq_size >= 16) {
        const std::uint8_t min_size = ctx->geom.bwidth < ctx->geom.bheight ? static_cast<std::uint8_t>(ctx->geom.bwidth)
                                                                           : static_cast<std::uint8_t>(ctx->geom.bheight);
        const std::uint32_t number_of_pus = enable_me_16x16
            ? (enable_me_8x8 ? max_number_of_pus_per_sb : MAX_SB64_PU_COUNT_NO_8X8)
            : MAX_SB64_PU_COUNT_WO_16X16;
        // Origin of the current block relative to its parent 64x64 block
        Position blk_geom_offset;
        blk_geom_offset.x = static_cast<std::uint32_t>(ctx->blk_org_x) - ctx->sb_origin_x - ctx->geom_offset_x;
        blk_geom_offset.y = static_cast<std::uint32_t>(ctx->blk_org_y) - ctx->sb_origin_y - ctx->geom_offset_y;
        // Derive the sub-block(s) MVs (additional MVC for NSQ)
        for (std::uint32_t block_index = 0; block_index < number_of_pus; block_index++) {
            if ((min_size == partitionWidth[block_index] || min_size == partitionHeight[block_index]) &&
                ((puSearchIndexMap[block_index][0] >= (unsigned)blk_geom_offset.x) &&
                 (puSearchIndexMap[block_index][0] < (unsigned)blk_geom_offset.x + ctx->geom.bwidth)) &&
                ((puSearchIndexMap[block_index][1] >= (unsigned)blk_geom_offset.y) &&
                 (puSearchIndexMap[block_index][1] < (unsigned)blk_geom_offset.y + ctx->geom.bheight)) &&
                l16_rdo::isMeDataPresent(block_index, block_index * max_cand, &me_view, list_idx, ref_idx)) {
                if (list_idx == 0) {
                    mvc_x_array[mvc_count] = static_cast<std::int16_t>(
                        me_results->me_mv_array[block_index * max_refs + ref_idx].x * 8);
                    mvc_y_array[mvc_count] = static_cast<std::int16_t>(
                        me_results->me_mv_array[block_index * max_refs + ref_idx].y * 8);
                } else {
                    mvc_x_array[mvc_count] = static_cast<std::int16_t>(
                        me_results->me_mv_array[block_index * max_refs + max_l0 + ref_idx].x * 8);
                    mvc_y_array[mvc_count] = static_cast<std::int16_t>(
                        me_results->me_mv_array[block_index * max_refs + max_l0 + ref_idx].y * 8);
                }
                std::uint8_t is_present = 0;
                for (std::int16_t mvc_index = 0; mvc_index < mvc_count; mvc_index++) {
                    if (mvc_x_array[mvc_count] == mvc_x_array[mvc_index] &&
                        mvc_y_array[mvc_count] == mvc_y_array[mvc_index]) {
                        is_present = 1;
                        break;
                    }
                }
                if (!is_present) {
                    mvc_count++;
                }
            }
        }
    }
    mvc_x_array[mvc_count] = 0;
    mvc_y_array[mvc_count] = 0;

    std::uint8_t is_present = 0;
    for (std::int16_t mvc_index = 0; mvc_index < mvc_count; mvc_index++) {
        if (mvc_x_array[mvc_count] == mvc_x_array[mvc_index] && mvc_y_array[mvc_count] == mvc_y_array[mvc_index]) {
            is_present = 1;
            break;
        }
    }
    if (!is_present) {
        mvc_count++;
    }
    // Search Center
    std::int16_t search_center_mvx = mvc_x_array[0];
    std::int16_t search_center_mvy = mvc_y_array[0];
    std::uint32_t search_center_cost = (std::uint32_t)~0;
    const std::uint8_t hbd = 0;  // EB_8_BIT_MD
    // -------
    // Use scaled references if resolution of the reference is different from that of the input
    // -------
    // (the ref PicBuf travels pre-resolved as the parameter - the scaled-ref
    // park; the reference's svt_aom_use_scaled_rec_refs_if_needed is a no-op
    // at equal resolutions).

    for (std::int16_t mvc_index = 0; mvc_index < mvc_count; mvc_index++) {
        // Round-up the search center to the closest integer
        mvc_x_array[mvc_index] = (mvc_x_array[mvc_index] + 4) & ~0x07;
        mvc_y_array[mvc_index] = (mvc_y_array[mvc_index] + 4) & ~0x07;
        mdFullPelSearch(ctx,
                        input_pic,
                        ref_pic,
                        input_origin_index,
                        nsq_ctrls.dist_type,
                        mvc_x_array[mvc_index],
                        mvc_y_array[mvc_index],
                        0,
                        0,
                        0,
                        0,
                        1,
                        0,
                        &search_center_mvx,
                        &search_center_mvy,
                        &search_center_cost,
                        hbd,
                        base_q_idx);
    }

    *me_mv_x = search_center_mvx;
    *me_mv_y = search_center_mvy;
    std::int16_t best_search_mvx = (std::int16_t)~0;
    std::int16_t best_search_mvy = (std::int16_t)~0;
    std::uint32_t best_search_cost = (std::uint32_t)~0;
    mdFullPelSearch(ctx,
                    input_pic,
                    ref_pic,
                    input_origin_index,
                    nsq_ctrls.dist_type,
                    search_center_mvx,
                    search_center_mvy,
                    -(nsq_ctrls.full_pel_search_width >> 1),
                    +(nsq_ctrls.full_pel_search_width >> 1),
                    -(nsq_ctrls.full_pel_search_height >> 1),
                    +(nsq_ctrls.full_pel_search_height >> 1),
                    4,
                    0,
                    &best_search_mvx,
                    &best_search_mvy,
                    &best_search_cost,
                    hbd,
                    base_q_idx);
    mdFullPelSearch(ctx,
                    input_pic,
                    ref_pic,
                    input_origin_index,
                    nsq_ctrls.dist_type,
                    best_search_mvx,
                    best_search_mvy,
                    -2,
                    +2,
                    -2,
                    +2,
                    2,
                    0,
                    &best_search_mvx,
                    &best_search_mvy,
                    &best_search_cost,
                    hbd,
                    base_q_idx);

    mdFullPelSearch(ctx,
                    input_pic,
                    ref_pic,
                    input_origin_index,
                    nsq_ctrls.dist_type,
                    best_search_mvx,
                    best_search_mvy,
                    -1,
                    +1,
                    -1,
                    +1,
                    1,
                    0,
                    &best_search_mvx,
                    &best_search_mvy,
                    &best_search_cost,
                    hbd,
                    base_q_idx);
    if (best_search_cost < search_center_cost) {
        *me_mv_x = best_search_mvx;
        *me_mv_y = best_search_mvy;
    }
}

// product_coding_loop.c:2270-:2457 md_sq_motion_search.
void mdSqMotionSearch(FlCtx* ctx, const PicBuf& input_pic, const PicBuf& ref_pic, std::uint32_t input_origin_index,
                      std::uint8_t rf, std::int16_t* me_mv_x, std::int16_t* me_mv_y, const MdSqCtrls& sq_ctrls,
                      std::int64_t picture_number, std::int64_t ref_pic_poc, std::uint8_t ref_is_intra,
                      std::uint32_t mfmv0_as_int, std::int16_t mfmv0_x, std::int16_t mfmv0_y,
                      std::uint8_t base_q_idx) {
    const std::uint8_t hbd_md = 0;  // EB_8_BIT_MD
    const std::uint8_t list_idx = motion::amvp::getListIdx(rf);
    const std::uint8_t ref_idx = motion::amvp::getRefFrameIdx(rf);
    // -------
    // Use scaled references if resolution of the reference is different from that of the input
    // -------
    // (the ref PicBuf travels pre-resolved - the scaled-ref park).

    std::uint16_t dist;
    {
        const std::int16_t diff16 = static_cast<std::int16_t>(picture_number - ref_pic_poc);
        dist = static_cast<std::uint16_t>(diff16 < 0 ? -diff16 : diff16);
    }
    std::uint8_t search_area_multiplier = 0;
    ctx->enable_psad = sq_ctrls.enable_psad;
    // Get pa_me distortion and MVs
    std::int16_t pa_me_mvx = (std::int16_t)~0;
    std::int16_t pa_me_mvy = (std::int16_t)~0;
    std::uint32_t pa_me_cost = (std::uint32_t)~0;
    mdFullPelSearch(ctx,
                    input_pic,
                    ref_pic,
                    input_origin_index,
                    sq_ctrls.dist_type,
                    *me_mv_x,
                    *me_mv_y,
                    0,
                    0,
                    0,
                    0,
                    1,
                    0,
                    &pa_me_mvx,
                    &pa_me_mvy,
                    &pa_me_cost,
                    hbd_md,
                    base_q_idx);
    // Identify potential high active block(s) and ME failure using 2 checks : (1) high ME_MV distortion, (2) active co - located block for non - intra ref(Temporal - MV(s)) or active surrounding block(s) for intra ref(Spatial - MV(s))
    if (ctx->geom.sq_size <= 64) {
        const std::uint32_t fast_lambda = ctx->hbd_md ? ctx->fast_lambda_md[1] : ctx->fast_lambda_md[0];

        // Check if pa_me distortion is above the per-pixel threshold.  Rate is set to 16.
        if (static_cast<std::uint64_t>(rdCost(fast_lambda, 16, pa_me_cost)) >
            static_cast<std::uint64_t>(rdCost(fast_lambda, 16,
                                              static_cast<std::uint64_t>(sq_ctrls.pame_distortion_th) *
                                                  ctx->geom.bwidth * ctx->geom.bheight))) {
            search_area_multiplier = !ref_is_intra
                ? checkTemporalMvSize(mfmv0_as_int, mfmv0_x, mfmv0_y)
                : checkSpatialMvSize(ctx->mvpArr[list_idx][ref_idx], ctx->mvpCnt[list_idx][ref_idx], me_mv_x, me_mv_y);
        }
    }

    // If high motion was detected, perform an expanded ME search
    if (search_area_multiplier) {
        std::int16_t best_search_mvx = (std::int16_t)~0;
        std::int16_t best_search_mvy = (std::int16_t)~0;
        std::uint32_t best_search_cost = (std::uint32_t)~0;

        dist = motion::getScaledPictureDistance(dist);

        // Sparse-search Level_0
        if (sq_ctrls.sprs_lev0_enabled) {
            const std::uint16_t sprs_lev0_w =
                static_cast<std::uint16_t>(sq_ctrls.sprs_lev0_multiplier *
                                           (sq_ctrls.sprs_lev0_w * search_area_multiplier * dist <
                                                    sq_ctrls.max_sprs_lev0_w
                                                ? sq_ctrls.sprs_lev0_w * search_area_multiplier * dist
                                                : sq_ctrls.max_sprs_lev0_w)) /
                100;
            const std::uint16_t sprs_lev0_h =
                static_cast<std::uint16_t>(sq_ctrls.sprs_lev0_multiplier *
                                           (sq_ctrls.sprs_lev0_h * search_area_multiplier * dist <
                                                    sq_ctrls.max_sprs_lev0_h
                                                ? sq_ctrls.sprs_lev0_h * search_area_multiplier * dist
                                                : sq_ctrls.max_sprs_lev0_h)) /
                100;
            const std::uint8_t sprs_lev0_step = sq_ctrls.sprs_lev0_step;

            // Derive start/end position of sparse search (must be a multiple of the step size)
            const std::int16_t search_position_start_x =
                -static_cast<std::int16_t>(((sprs_lev0_w >> 1) / sprs_lev0_step) * sprs_lev0_step);
            const std::int16_t search_position_end_x =
                +static_cast<std::int16_t>(((sprs_lev0_w >> 1) / sprs_lev0_step) * sprs_lev0_step);
            const std::int16_t search_position_start_y =
                -static_cast<std::int16_t>(((sprs_lev0_h >> 1) / sprs_lev0_step) * sprs_lev0_step);
            const std::int16_t search_position_end_y =
                +static_cast<std::int16_t>(((sprs_lev0_h >> 1) / sprs_lev0_step) * sprs_lev0_step);

            ctx->sprs_lev0_start_x = static_cast<std::int16_t>((*me_mv_x >> 3) + search_position_start_x);
            ctx->sprs_lev0_end_x = static_cast<std::int16_t>((*me_mv_x >> 3) + search_position_end_x);
            ctx->sprs_lev0_start_y = static_cast<std::int16_t>((*me_mv_y >> 3) + search_position_start_y);
            ctx->sprs_lev0_end_y = static_cast<std::int16_t>((*me_mv_y >> 3) + search_position_end_y);
            mdFullPelSearch(ctx,
                            input_pic,
                            ref_pic,
                            input_origin_index,
                            sq_ctrls.dist_type,
                            *me_mv_x,
                            *me_mv_y,
                            search_position_start_x,
                            search_position_end_x,
                            search_position_start_y,
                            search_position_end_y,
                            sprs_lev0_step,
                            0,
                            &best_search_mvx,
                            &best_search_mvy,
                            &best_search_cost,
                            hbd_md,
                            base_q_idx);
            *me_mv_x = best_search_mvx;
            *me_mv_y = best_search_mvy;
        }

        // Sparse-search Level_1
        if (sq_ctrls.sprs_lev1_enabled) {
            const std::uint16_t sprs_lev1_w =
                static_cast<std::uint16_t>(sq_ctrls.sprs_lev1_multiplier *
                                           (sq_ctrls.sprs_lev1_w * search_area_multiplier * dist <
                                                    sq_ctrls.max_sprs_lev1_w
                                                ? sq_ctrls.sprs_lev1_w * search_area_multiplier * dist
                                                : sq_ctrls.max_sprs_lev1_w)) /
                100;
            const std::uint16_t sprs_lev1_h =
                static_cast<std::uint16_t>(sq_ctrls.sprs_lev1_multiplier *
                                           (sq_ctrls.sprs_lev1_h * search_area_multiplier * dist <
                                                    sq_ctrls.max_sprs_lev1_h
                                                ? sq_ctrls.sprs_lev1_h * search_area_multiplier * dist
                                                : sq_ctrls.max_sprs_lev1_h)) /
                100;
            const std::uint8_t sprs_lev1_step = sq_ctrls.sprs_lev1_step;

            // Derive start/end position of sparse search (must be a multiple of the step size)
            std::int16_t search_position_start_x =
                -static_cast<std::int16_t>(((sprs_lev1_w >> 1) / sprs_lev1_step) * sprs_lev1_step);
            std::int16_t search_position_end_x =
                +static_cast<std::int16_t>(((sprs_lev1_w >> 1) / sprs_lev1_step) * sprs_lev1_step);
            std::int16_t search_position_start_y =
                -static_cast<std::int16_t>(((sprs_lev1_h >> 1) / sprs_lev1_step) * sprs_lev1_step);
            std::int16_t search_position_end_y =
                +static_cast<std::int16_t>(((sprs_lev1_h >> 1) / sprs_lev1_step) * sprs_lev1_step);

            search_position_start_x = (search_position_start_x % 4 == 0) ? search_position_start_x - 2
                                                                         : search_position_start_x;
            search_position_end_x = (search_position_end_x % 4 == 0) ? search_position_end_x + 2
                                                                     : search_position_end_x;
            search_position_start_y = (search_position_start_y % 4 == 0) ? search_position_start_y - 2
                                                                         : search_position_start_y;
            search_position_end_y = (search_position_end_y % 4 == 0) ? search_position_end_y + 2
                                                                     : search_position_end_y;
            mdFullPelSearch(ctx,
                            input_pic,
                            ref_pic,
                            input_origin_index,
                            sq_ctrls.dist_type,
                            *me_mv_x,
                            *me_mv_y,
                            search_position_start_x,
                            search_position_end_x,
                            search_position_start_y,
                            search_position_end_y,
                            sprs_lev1_step,
                            (sq_ctrls.sprs_lev0_enabled && sq_ctrls.sprs_lev0_step == 4) ? 1 : 0,
                            &best_search_mvx,
                            &best_search_mvy,
                            &best_search_cost,
                            hbd_md,
                            base_q_idx);
            *me_mv_x = best_search_mvx;
            *me_mv_y = best_search_mvy;
        }

        // Sparse-search Level_2
        if (sq_ctrls.sprs_lev2_enabled) {
            mdFullPelSearch(ctx,
                            input_pic,
                            ref_pic,
                            input_origin_index,
                            sq_ctrls.dist_type,
                            *me_mv_x,
                            *me_mv_y,
                            -static_cast<std::int16_t>(((sq_ctrls.sprs_lev2_w >> 1) / sq_ctrls.sprs_lev2_step) *
                                                       sq_ctrls.sprs_lev2_step),
                            +static_cast<std::int16_t>(((sq_ctrls.sprs_lev2_w >> 1) / sq_ctrls.sprs_lev2_step) *
                                                       sq_ctrls.sprs_lev2_step),
                            -static_cast<std::int16_t>(((sq_ctrls.sprs_lev2_h >> 1) / sq_ctrls.sprs_lev2_step) *
                                                       sq_ctrls.sprs_lev2_step),
                            +static_cast<std::int16_t>(((sq_ctrls.sprs_lev2_h >> 1) / sq_ctrls.sprs_lev2_step) *
                                                       sq_ctrls.sprs_lev2_step),
                            sq_ctrls.sprs_lev2_step,
                            0,
                            &best_search_mvx,
                            &best_search_mvy,
                            &best_search_cost,
                            hbd_md,
                            base_q_idx);
            *me_mv_x = best_search_mvx;
            *me_mv_y = best_search_mvy;
        }
    }
}

// pic_analysis_process.c:936-:942 svt_aom_eb_av1_var_offs (MAX_SB_SIZE
// entries of 128).
static const std::uint8_t kVarOffs[128] = {
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128};

// The svf slot (av1me.h:61-62): the subpel variance at the block dims - the
// landed analysis::subpel SUBPIX_VAR instances.
using SubpelVarFn = std::uint32_t (*)(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                      const std::uint8_t* b, int bStride, std::uint32_t* sse);
static const SubpelVarFn kMefnSvfTable[entropy::BLOCK_SIZES_ALL] = {
    analysis::subpel::subPixelVariance4x4,    // BLOCK_4X4
    analysis::subpel::subPixelVariance4x8,    // BLOCK_4X8
    analysis::subpel::subPixelVariance8x4,    // BLOCK_8X4
    analysis::subpel::subPixelVariance8x8,    // BLOCK_8X8
    analysis::subpel::subPixelVariance8x16,   // BLOCK_8X16
    analysis::subpel::subPixelVariance16x8,   // BLOCK_16X8
    analysis::subpel::subPixelVariance16x16,  // BLOCK_16X16
    analysis::subpel::subPixelVariance16x32,  // BLOCK_16X32
    analysis::subpel::subPixelVariance32x16,  // BLOCK_32X16
    analysis::subpel::subPixelVariance32x32,  // BLOCK_32X32
    analysis::subpel::subPixelVariance32x64,  // BLOCK_32X64
    analysis::subpel::subPixelVariance64x32,  // BLOCK_64X32
    analysis::subpel::subPixelVariance64x64,  // BLOCK_64X64
    analysis::subpel::subPixelVariance64x128, // BLOCK_64X128
    analysis::subpel::subPixelVariance128x64, // BLOCK_128X64
    analysis::subpel::subPixelVariance128x128,// BLOCK_128X128
    analysis::subpel::subPixelVariance4x16,   // BLOCK_4X16
    analysis::subpel::subPixelVariance16x4,   // BLOCK_16X4
    analysis::subpel::subPixelVariance8x32,   // BLOCK_8X32
    analysis::subpel::subPixelVariance32x8,   // BLOCK_32X8
    analysis::subpel::subPixelVariance16x64,  // BLOCK_16X64
    analysis::subpel::subPixelVariance64x16,  // BLOCK_64X16
};

// product_coding_loop.c:2582-:2681 md_subpel_search_fixed_stage.
std::uint32_t mdSubpelSearchFixedStage(FlCtx* ctx, const PicBuf& ref_pic, const std::uint8_t* src,
                                       const int src_stride, Mv2* me_mv, const MdSubPelCtrls& sub_ctrls) {
    const std::uint8_t* ref_buf = ref_pic.buf;
    const int blk_org_x = ctx->blk_org_x;
    const int blk_org_y = ctx->blk_org_y;
    const int blk_w = static_cast<int>(ctx->geom.bwidth);
    const int blk_h = static_cast<int>(ctx->geom.bheight);
    const int ref_stride = static_cast<int>(ref_pic.stride);
    const entropy::BlockSize bsize = ctx->geom.bsize;
    VarianceFn vf = mefnVf(bsize);
    SubpelVarFn svf = kMefnSvfTable[bsize];
    const int mv_x_fp = me_mv->x;
    const int mv_y_fp = me_mv->y;
    const std::uint32_t bias_fp = static_cast<std::uint32_t>(sub_ctrls.bias_fp);

    static const std::int8_t hpel_dx[4] = {4, -4, 0, 0};
    static const std::int8_t hpel_dy[4] = {0, 0, 4, -4};
    std::uint32_t best_var = 0xFFFFFFFFu;
    int best_dx = 0, best_dy = 0;

    const std::uint32_t th_normalizer = static_cast<std::uint32_t>(blk_w * blk_h) * sub_ctrls.abs_th_mult;
    // integer-pel baseline
    {
        const int fp_x = blk_org_x + (mv_x_fp >> 3);
        const int fp_y = blk_org_y + (mv_y_fp >> 3);
        std::uint32_t sse;
        best_var = vf(ref_buf + fp_x + fp_y * ref_stride, ref_stride, src, src_stride, &sse);

        if (best_var < th_normalizer) {
            return best_var;
        }

        // Exit subpel search if the variance of the full-pel predicted samples is low (i.e. where likely interpolation will not modify the integer samples)
        if (sub_ctrls.pred_variance_th) {
            const unsigned int var =
                vf(ref_buf + fp_x + fp_y * ref_stride, ref_stride, kVarOffs, 0, &sse);
            const int block_var = static_cast<int>(roundPowerOfTwo64(var, entropy::ebNumPelsLog2Lookup[bsize]));

            if (block_var < sub_ctrls.pred_variance_th) {
                return best_var;
            }
        }
    }
    // half-pel neighbors
    for (int i = 0; i < 4; ++i) {
        const int subx = (mv_x_fp + hpel_dx[i]) & 7;
        const int suby = (mv_y_fp + hpel_dy[i]) & 7;
        const int fp_x = blk_org_x + ((mv_x_fp + hpel_dx[i]) >> 3);
        const int fp_y = blk_org_y + ((mv_y_fp + hpel_dy[i]) >> 3);
        std::uint32_t sse;
        const std::uint32_t var = svf(ref_buf + fp_x + fp_y * ref_stride, ref_stride, subx, suby, src, src_stride,
                                      &sse);
        const std::uint32_t biased_var = (bias_fp && best_dx == 0 && best_dy == 0)
            ? static_cast<std::uint32_t>((static_cast<std::uint64_t>(var) * bias_fp) / 100)
            : var;
        if (biased_var < best_var) {
            best_var = var;
            best_dx = hpel_dx[i];
            best_dy = hpel_dy[i];
            if (best_var < th_normalizer) {
                me_mv->x = static_cast<std::int16_t>(mv_x_fp + best_dx);
                me_mv->y = static_cast<std::int16_t>(mv_y_fp + best_dy);
                return best_var;
            }
        }
    }

    // quarter-pel neighbors
    if (sub_ctrls.max_precision <= kQuarterPel) {
        static const std::int8_t qpel_dx[4] = {2, -2, 0, 0};
        static const std::int8_t qpel_dy[4] = {0, 0, 2, -2};

        for (int i = 0; i < 4; ++i) {
            const int tot_dx = best_dx + qpel_dx[i];
            const int tot_dy = best_dy + qpel_dy[i];
            const int subx = (mv_x_fp + tot_dx) & 7;
            const int suby = (mv_y_fp + tot_dy) & 7;
            const int fp_x = blk_org_x + ((mv_x_fp + tot_dx) >> 3);
            const int fp_y = blk_org_y + ((mv_y_fp + tot_dy) >> 3);
            std::uint32_t sse;
            const std::uint32_t var = svf(ref_buf + fp_x + fp_y * ref_stride, ref_stride, subx, suby, src,
                                          src_stride, &sse);
            const std::uint32_t biased_var = (bias_fp && best_dx == 0 && best_dy == 0)
                ? static_cast<std::uint32_t>((static_cast<std::uint64_t>(var) * bias_fp) / 100)
                : var;
            if (biased_var < best_var) {
                best_var = var;
                best_dx = tot_dx;
                best_dy = tot_dy;
                if (best_var < th_normalizer) {
                    me_mv->x = static_cast<std::int16_t>(mv_x_fp + best_dx);
                    me_mv->y = static_cast<std::int16_t>(mv_y_fp + best_dy);
                    return best_var;
                }
            }
        }
    }
    me_mv->x = static_cast<std::int16_t>(mv_x_fp + best_dx);
    me_mv->y = static_cast<std::int16_t>(mv_y_fp + best_dy);
    return best_var;
}

// product_coding_loop.c:2462-:2578 md_subpel_search.
int mdSubpelSearch(FlCtx* ctx, const PicBuf& input_pic, const PicBuf& ref_pic, SubpelStage search_stage,
                   const MdSubPelCtrls& md_subpel_ctrls, std::uint8_t rf, Mv2* me_mv, std::uint8_t allow_hp,
                   int mi_row, int mi_col, int cm_mi_rows, int cm_mi_cols, std::uint8_t base_q_idx,
                   MdSubpelSearchFn subpel_search_fn) {
    const std::int16_t me_mv_x = me_mv->x;
    const std::int16_t me_mv_y = me_mv->y;
    const std::uint8_t list_idx = motion::amvp::getListIdx(rf);
    const std::uint8_t ref_idx = motion::amvp::getRefFrameIdx(rf);

    // High level params
    MdSubpelParams ms_params_struct;
    std::memset(&ms_params_struct, 0, sizeof(ms_params_struct));
    MdSubpelParams* ms_params = &ms_params_struct;
    ms_params->search_stage = search_stage;
    ms_params->list_idx = list_idx;
    ms_params->ref_idx = ref_idx;
    ms_params->allow_hp = allow_hp;
    ms_params->forced_stop = md_subpel_ctrls.max_precision;
    // Maximum number of steps in logarithmic subpel search before giving up.
    ms_params->iters_per_step = md_subpel_ctrls.subpel_iters_per_step;
    // Derive mv_limits (TODO Hsan_Subpel should be derived under ctx @ eack block)
    // Set up limit values for MV components.
    // Mv beyond the range do not produce new/different prediction block.
    motion::MvLimits mv_limits;
    const int mi_width = l16_rdo::miSizeWide[static_cast<int>(ctx->geom.bsize)];
    const int mi_height = l16_rdo::miSizeHigh[static_cast<int>(ctx->geom.bsize)];
    mv_limits.row_min = -(((mi_row + mi_height) * 4 /* MI_SIZE */) + 4 /* AOM_INTERP_EXTEND */);
    mv_limits.col_min = -(((mi_col + mi_width) * 4 /* MI_SIZE */) + 4 /* AOM_INTERP_EXTEND */);
    mv_limits.row_max = (cm_mi_rows - mi_row) * 4 /* MI_SIZE */ + 4 /* AOM_INTERP_EXTEND */;
    mv_limits.col_max = (cm_mi_cols - mi_col) * 4 /* MI_SIZE */ + 4 /* AOM_INTERP_EXTEND */;
    motion::setMvSearchRange(&mv_limits, &ctx->ref_mv);
    motion::setSubpelMvSearchRange(&mv_limits, &ms_params->col_min, &ms_params->col_max, &ms_params->row_min,
                                   &ms_params->row_max, &ctx->ref_mv);
    // Mvcost params
    pipeline::InitMvCostParamsCtx init_ctx;
    init_ctx.sq_size = ctx->geom.sq_size;
    init_ctx.skip_diag_refinement = ctx->md_subpel_skip_diag_refinement;
    init_ctx.nmv_vec_cost = ctx->nmv_vec_cost;
    init_ctx.nmvcoststack0 = ctx->nmvcoststack0;
    init_ctx.nmvcoststack1 = ctx->nmvcoststack1;
    pipeline::svtInitMvCostParams(init_ctx, &ms_params->mv_cost_params, &ctx->ref_mv, base_q_idx,
                                  ctx->full_lambda_md[0], 0);  // 10BIT not supported
    // Subpel variance params
    ms_params->subpel_search_type = md_subpel_ctrls.subpel_search_type;
    ms_params->var_w = static_cast<int>(entropy::blockSizeWide[static_cast<int>(ctx->geom.bsize)]);
    ms_params->var_h = static_cast<int>(entropy::blockSizeHigh[static_cast<int>(ctx->geom.bsize)]);

    // Ref buffer
    const std::int32_t ref_origin_index = ctx->blk_org_x + (ctx->blk_org_y) * static_cast<std::int32_t>(ref_pic.stride);
    ms_params->ref_buf.buf = ref_pic.buf + ref_origin_index;
    ms_params->ref_buf.width = static_cast<int>(ref_pic.width);
    ms_params->ref_buf.height = static_cast<int>(ref_pic.height);
    ms_params->ref_buf.stride = static_cast<int>(ref_pic.stride);
    // Src buffer
    const std::uint32_t input_origin_index =
        static_cast<std::uint32_t>(ctx->blk_org_y) * input_pic.stride + static_cast<std::uint32_t>(ctx->blk_org_x);
    ms_params->src_buf.buf = input_pic.buf + input_origin_index;
    ms_params->src_buf.width = static_cast<int>(input_pic.width);
    ms_params->src_buf.height = static_cast<int>(input_pic.height);
    ms_params->src_buf.stride = static_cast<int>(input_pic.stride);
    Mv2 best_mv;
    // TODO: should use get_fullmv_from_mv instead of shifting
    best_mv.x = static_cast<std::int16_t>(me_mv_x >> 3);
    best_mv.y = static_cast<std::int16_t>(me_mv_y >> 3);
    Mv2 subpel_start_mv;
    subpel_start_mv.x = static_cast<std::int16_t>(best_mv.x * 8);  // GET_MV_SUBPEL
    subpel_start_mv.y = static_cast<std::int16_t>(best_mv.y * 8);

    // Assign which subpel search method to use: RTC (M9+) never selects
    // SUBPEL_TREE, so only the pruned search is reachable (the reference's
    // own CONFIG=0 comment) - the tree itself is the A3M park, threaded as
    // subpel_search_fn (the reference's int* not_used out drops with it).
    ms_params->pred_variance_th = md_subpel_ctrls.pred_variance_th;
    ms_params->abs_th_mult = md_subpel_ctrls.abs_th_mult;
    ms_params->round_dev_th = md_subpel_ctrls.round_dev_th;
    ms_params->skip_diag_refinement = md_subpel_ctrls.skip_diag_refinement;
    ms_params->bias_fp = md_subpel_ctrls.bias_fp;
    const int besterr = subpel_search_fn(*ctx, *ms_params, subpel_start_mv, &best_mv);
    me_mv->x = best_mv.x;
    me_mv->y = best_mv.y;

    return besterr;
}

// The l16 is_me_data_present view over the MdMeSbResults carrier.
static l16_rdo::MeSbResults meView(const MdMeSbResults* me_results) {
    l16_rdo::MeSbResults v;
    v.totalMeCandidateIndex = me_results->total_me_candidate_index;
    v.meCandidateArray = me_results->me_candidate_array;
    return v;
}

// product_coding_loop.c:2685-:2760 read_refine_me_mvs_light_pd1.
void readRefineMeMvsLightPd1(FlCtx* ctx, const PicBuf& input_pic, const MdMeSbResults* me_results,
                             std::uint32_t me_block_offset, std::uint32_t me_cand_offset, std::uint8_t max_l0,
                             std::uint8_t max_refs, std::uint8_t base_q_idx, ChooseBestMvPredFn choose_best_mv_pred,
                             SubpelTreeFn subpel_tree, const MdSubPelCtrls& sub_ctrls, int mi_row, int mi_col,
                             int cm_mi_rows, int cm_mi_cols) {
    // init best ME cost to MAX
    ctx->md_me_dist = (std::uint32_t)~0;

    const bool subpel_enabled = sub_ctrls.enabled != 0;

    const bool no_mv_stack = ctx->shut_fast_rate != 0;

    const l16_rdo::MeSbResults me_view = meView(me_results);
    for (int ref_it = 0; ref_it < ctx->totRefFrameTypes; ++ref_it) {
        const std::int32_t ref_pair = ctx->refFrameTypeArr[ref_it];
        int rf[2];
        motion::amvp::av1SetRefFrame(rf, static_cast<std::int8_t>(ref_pair));

        if (rf[1] == motion::amvp::kNoneFrame) {
            const std::uint8_t list = motion::amvp::getListIdx(static_cast<std::uint8_t>(rf[0]));
            const std::uint8_t ref = motion::amvp::getRefFrameIdx(static_cast<std::uint8_t>(rf[0]));

            if (l16_rdo::isMeDataPresent(me_block_offset, me_cand_offset, &me_view, list, ref) != 0) {
                const PicBuf& ref_pic = *ctx->refPicArr[list][ref];

                const Mv2* me_mv_array_base =
                    me_results->me_mv_array + (me_block_offset * max_refs + ref);
                const Mv2 mv_cand = me_mv_array_base[list ? max_l0 : 0];
                Mv2 me_mv;
                me_mv.x = static_cast<std::int16_t>(mv_cand.x * 8);
                me_mv.y = static_cast<std::int16_t>(mv_cand.y * 8);
                // can only skip if using dc only b/c otherwise need cost at candidate generation
                const bool skip_subpel = (ctx->isIntraBordered != 0 && ctx->useNeighboringModeEnabled != 0) ||
                    (ctx->geom.sq_size <= ctx->mdSubpelMinBlkSz);

                if (subpel_enabled && !skip_subpel) {
                    if (no_mv_stack) {
                        ctx->ref_mv.as_int = 0;
                    } else {
                        Mv2 best_pred_mv[2] = {};
                        std::uint8_t drl_index = 0;
                        (void)drl_index;
                        choose_best_mv_pred(*ctx, ref_pair, me_mv, best_pred_mv);
                        ctx->ref_mv.as_int = best_pred_mv[0].as_int;
                    }
                    if (ctx->mdSubpelSearchMethod == kSubpelFixedStageSearch) {
                        const std::uint8_t* src = input_pic.buf + ctx->blk_org_x +
                            (ctx->blk_org_y) * static_cast<std::int32_t>(input_pic.stride);
                        ctx->postSubpelMeMvCost[list][ref] =
                            static_cast<std::int32_t>(mdSubpelSearchFixedStage(ctx, ref_pic, src,
                                                                               static_cast<int>(input_pic.stride),
                                                                               &me_mv, sub_ctrls));
                    } else {
                        ctx->postSubpelMeMvCost[list][ref] =
                            mdSubpelSearch(ctx, input_pic, ref_pic, kSpelMe, sub_ctrls,
                                           static_cast<std::uint8_t>(rf[0]), &me_mv, 0 /* allow_hp: frm_hdr wall */,
                                           mi_row, mi_col, cm_mi_rows, cm_mi_cols, base_q_idx, subpel_tree);
                    }

                    if (static_cast<std::uint32_t>(ctx->postSubpelMeMvCost[list][ref]) < ctx->md_me_dist) {
                        ctx->md_me_dist = static_cast<std::uint32_t>(ctx->postSubpelMeMvCost[list][ref]);
                    }
                }
                ctx->sbMeMv[list][ref].as_int = me_mv.as_int;
                clipMvOnPicBoundary(ctx->blk_org_x, ctx->blk_org_y, static_cast<std::int32_t>(ctx->geom.bwidth),
                                    static_cast<std::int32_t>(ctx->geom.bheight),
                                    static_cast<std::int32_t>(ref_pic.max_width),
                                    static_cast<std::int32_t>(ref_pic.max_height), ref_pic.border,
                                    &ctx->sbMeMv[list][ref].x, &ctx->sbMeMv[list][ref].y);
            }
        }
    }
}

// product_coding_loop.c:2764-:2887 read_refine_me_mvs.
void readRefineMeMvs(FlCtx* ctx, const PicBuf& input_pic, const MdMeSbResults* me_results, std::uint8_t max_l0,
                     std::uint8_t max_refs, std::uint8_t max_cand, std::uint8_t base_q_idx,
                     ChooseBestMvPredFn choose_best_mv_pred, SubpelTreeFn subpel_tree,
                     const MdSubPelCtrls& sub_ctrls, const MdNsqCtrls& nsq_ctrls, const MdSqCtrls& sq_ctrls,
                     int mi_row, int mi_col, int cm_mi_rows, int cm_mi_cols, bool blk_avail_sqi,
                     bool parent_tested_n0) {
    const std::uint8_t hbd_md = 0;  // EB_8_BIT_MD

    // Update input origin
    const std::uint32_t input_origin_index =
        static_cast<std::uint32_t>(ctx->blk_org_y) * input_pic.stride + static_cast<std::uint32_t>(ctx->blk_org_x);

    const bool b_w_ne_h = ctx->geom.bwidth != ctx->geom.bheight;
    const bool md_nsq_me_enabled = nsq_ctrls.enabled != 0;
    const bool md_sq_me_enabled = sq_ctrls.enabled != 0;
    const bool do_subpel = sub_ctrls.enabled != 0;

    ctx->md_me_dist = (std::uint32_t)~0;
    const l16_rdo::MeSbResults me_view = meView(me_results);
    for (std::uint32_t ref_it = 0; ref_it < ctx->totRefFrameTypes; ++ref_it) {
        const std::int32_t ref_pair = ctx->refFrameTypeArr[ref_it];
        int rf[2];
        motion::amvp::av1SetRefFrame(rf, static_cast<std::int8_t>(ref_pair));

        if (rf[1] == motion::amvp::kNoneFrame) {
            const std::uint8_t list = motion::amvp::getListIdx(static_cast<std::uint8_t>(rf[0]));
            const std::uint8_t ref = motion::amvp::getRefFrameIdx(static_cast<std::uint8_t>(rf[0]));
            const PicBuf& ref_pic = *ctx->refPicArr[list][ref];

            // Get the ME MV
            if (l16_rdo::isMeDataPresent(ctx->meBlockOffset, ctx->meCandOffset, &me_view, list, ref) != 0) {
                Mv2 me_mv;
                if (blk_avail_sqi &&
                    // If NSQ then use the MV of SQ as default MV center
                    b_w_ne_h &&
                    // Not applicable for BLOCK_128X64 and BLOCK_64X128 as the 2nd part of each and BLOCK_128X128 do not share the same me_results
                    ctx->geom.bsize != entropy::BLOCK_64X128 && ctx->geom.bsize != entropy::BLOCK_128X64) {
                    me_mv.x = static_cast<std::int16_t>((ctx->sqSbMeMv[list][ref].x + 4) & ~0x07);
                    me_mv.y = static_cast<std::int16_t>((ctx->sqSbMeMv[list][ref].y + 4) & ~0x07);
                } else if (ctx->geom.bsize == entropy::BLOCK_4X4 && parent_tested_n0) {
                    me_mv.x = static_cast<std::int16_t>((ctx->sqSbMeMv[list][ref].x + 4) & ~0x07);
                    me_mv.y = static_cast<std::int16_t>((ctx->sqSbMeMv[list][ref].y + 4) & ~0x07);
                } else {
                    const Mv2* me_mv_array_base =
                        me_results->me_mv_array + (ctx->meBlockOffset * max_refs + ref);
                    const Mv2 mv_cand = me_mv_array_base[list ? max_l0 : 0];
                    me_mv.x = static_cast<std::int16_t>(mv_cand.x * 8);
                    me_mv.y = static_cast<std::int16_t>(mv_cand.y * 8);
                }
                clipMvOnPicBoundary(ctx->blk_org_x, ctx->blk_org_y, static_cast<std::int32_t>(ctx->geom.bwidth),
                                    static_cast<std::int32_t>(ctx->geom.bheight),
                                    static_cast<std::int32_t>(ref_pic.max_width),
                                    static_cast<std::int32_t>(ref_pic.max_height), ref_pic.border, &me_mv.x,
                                    &me_mv.y);
                // Set ref MV
                Mv2 best_pred_mv[2] = {};
                std::uint8_t drl_index = 0;
                (void)drl_index;
                choose_best_mv_pred(*ctx, ref_pair, me_mv, best_pred_mv);
                ctx->ref_mv.as_int = best_pred_mv[0].as_int;
                if (b_w_ne_h) {
                    if (md_nsq_me_enabled) {
                        mdNsqMotionSearch(ctx, input_pic, ref_pic, input_origin_index,
                                          static_cast<std::uint8_t>(rf[0]), me_results, &me_mv.x, &me_mv.y, nsq_ctrls,
                                          max_refs, max_l0, ctx->enableMe8x8, ctx->enableMe16x16, kSquarePuCount,
                                          max_cand, base_q_idx);
                    }
                } else if (md_sq_me_enabled) {
                    // The TPL mfmv0 triple and the pcs walls thread as
                    // carrier fields (the check_temporal_mv_size input).
                    mdSqMotionSearch(ctx, input_pic, ref_pic, input_origin_index,
                                     static_cast<std::uint8_t>(rf[0]), &me_mv.x, &me_mv.y, sq_ctrls,
                                     ctx->sqPictureNumber, ctx->sqRefPicPoc, ctx->sqRefIsIntra, ctx->sqMfmv0AsInt,
                                     ctx->sqMfmv0X, ctx->sqMfmv0Y, base_q_idx);
                }
                ctx->postSubpelMeMvCost[list][ref] = (std::int32_t)~0;
                ctx->fpMeMv[list][ref].as_int = me_mv.as_int;

                if (do_subpel) {
                    if (ctx->mdSubpelSearchMethod == kSubpelFixedStageSearch) {
                        const std::uint8_t* src = input_pic.buf + ctx->blk_org_x +
                            (ctx->blk_org_y) * static_cast<std::int32_t>(input_pic.stride);
                        ctx->postSubpelMeMvCost[list][ref] =
                            static_cast<std::int32_t>(mdSubpelSearchFixedStage(ctx, ref_pic, src,
                                                                               static_cast<int>(input_pic.stride),
                                                                               &me_mv, sub_ctrls));
                    } else {
                        ctx->postSubpelMeMvCost[list][ref] =
                            mdSubpelSearch(ctx, input_pic, ref_pic, kSpelMe, sub_ctrls,
                                           static_cast<std::uint8_t>(rf[0]), &me_mv, 0, mi_row, mi_col, cm_mi_rows,
                                           cm_mi_cols, base_q_idx, subpel_tree);
                    }
                    if (static_cast<std::uint32_t>(ctx->postSubpelMeMvCost[list][ref]) < ctx->md_me_dist) {
                        ctx->md_me_dist = static_cast<std::uint32_t>(ctx->postSubpelMeMvCost[list][ref]);
                    }
                } else if (ctx->updatedEnablePme != 0 || ctx->refPruningEnabled != 0) {
                    // If full-pel cost for ME MVs will be needed for other features, ensure it is computed when subpel is off
                    const std::int32_t ref_origin_index = (ctx->blk_org_x + (me_mv.x >> 3)) +
                        (ctx->blk_org_y + (me_mv.y >> 3)) * static_cast<std::int32_t>(ref_pic.stride);
                    VarianceFn vf = mefnVf(ctx->geom.bsize);
                    std::uint32_t sse;
                    const std::uint8_t* pred_y = ref_pic.buf + ref_origin_index;
                    const std::uint8_t* src_y = input_pic.buf + input_origin_index;
                    ctx->fpMeDist[list][ref] =
                        vf(pred_y, static_cast<std::int32_t>(ref_pic.stride), src_y,
                           static_cast<std::int32_t>(input_pic.stride), &sse);

                    pipeline::SvtMvCostParam mv_cost_params;
                    // Variance is computed for 8bit, so use 8bit lambda
                    const std::uint32_t rdmult = ctx->full_lambda_md[0];
                    pipeline::InitMvCostParamsCtx init_ctx;
                    init_ctx.sq_size = ctx->geom.sq_size;
                    init_ctx.skip_diag_refinement = ctx->md_subpel_skip_diag_refinement;
                    init_ctx.nmv_vec_cost = ctx->nmv_vec_cost;
                    init_ctx.nmvcoststack0 = ctx->nmvcoststack0;
                    init_ctx.nmvcoststack1 = ctx->nmvcoststack1;
                    pipeline::svtInitMvCostParams(init_ctx, &mv_cost_params, &ctx->ref_mv, base_q_idx, rdmult,
                                                  hbd_md);
                    Mv2 best_mv;
                    best_mv.as_int = me_mv.as_int;
                    ctx->fpMeDist[list][ref] +=
                        static_cast<std::uint32_t>(pipeline::fpMvErrCost(best_mv, mv_cost_params));
                }
                // Copy ME MV after subpel
                ctx->subMeMv[list][ref].as_int = me_mv.as_int;
                ctx->sbMeMv[list][ref].as_int = me_mv.as_int;
                clipMvOnPicBoundary(ctx->blk_org_x, ctx->blk_org_y, static_cast<std::int32_t>(ctx->geom.bwidth),
                                    static_cast<std::int32_t>(ctx->geom.bheight),
                                    static_cast<std::int32_t>(ref_pic.max_width),
                                    static_cast<std::int32_t>(ref_pic.max_height), ref_pic.border,
                                    &ctx->sbMeMv[list][ref].x, &ctx->sbMeMv[list][ref].y);
                if (ctx->shape == PART_N) {
                    ctx->sqSbMeMv[list][ref].as_int = ctx->sbMeMv[list][ref].as_int;
                }
            }
        }
    }
}

// product_coding_loop.c:2895-:2955 get_sb_tpl_inter_stats.
bool getSbTplInterStats(const TplPcsWindow& ppcs, std::uint32_t sb_origin_x, std::uint32_t sb_origin_y,
                        std::uint8_t* sb_inter_selection, std::uint8_t* sb_max_list0_ref_idx,
                        std::uint8_t* sb_max_list1_ref_idx) {
    // Check that TPL data is available and that INTRA was tested in TPL.
    // Note that not all INTRA modes may be tested in TPL.
    if (ppcs.tpl_enable && ppcs.tpl_src_data_ready &&
        (ppcs.temporal_layer_index < ppcs.hierarchical_levels || !ppcs.disable_intra_pred_nref)) {
        const int aligned16_width = (ppcs.aligned_width + 15) >> 4;
        const uint32_t mb_origin_x = sb_origin_x;
        const uint32_t mb_origin_y = sb_origin_y;
        const int tpl_blk_size = ppcs.dispenser_search_level == 0 ? 16
            : ppcs.dispenser_search_level == 1                                    ? 32
                                                                                  : 64;
        // tpl_src_stats_buffer is created for 16x16 always, so TPL dispenser is for larger block sizes
        // the step between blocks must be adjusted
        const int tpl_blk_step = ppcs.dispenser_search_level == 0 ? 1
            : ppcs.dispenser_search_level == 1                                    ? 2
                                                                                    : 4;

        // Get actual SB width (for cases of incomplete SBs)
        const int sb_cols = 1 > (ppcs.sb_width / tpl_blk_size) ? 1 : (ppcs.sb_width / tpl_blk_size);
        const int sb_rows = 1 > (ppcs.sb_height / tpl_blk_size) ? 1 : (ppcs.sb_height / tpl_blk_size);

        std::uint8_t tot_cnt = 0;
        std::uint8_t inter_cnt = 0;
        std::uint8_t max_list_ref_idx[2] = {0};

        // Loop over all blocks in the SB
        for (int i = 0; i < sb_rows; i++) {
            const TplSrcStatsWindow* tpl_src_stats_buffer =
                ppcs.tpl_src_stats + static_cast<std::size_t>(((mb_origin_y >> 4) + (i * tpl_blk_step)) *
                                                                  aligned16_width) +
                (mb_origin_x >> 4);
            for (int j = 0; j < sb_cols; j++) {
                tot_cnt++;
                if (static_cast<std::int32_t>(tpl_src_stats_buffer->best_mode) >= l16_rdo::NEARESTMV) {
                    std::uint8_t list_index = tpl_src_stats_buffer->best_rf_idx < 4 ? 0 : 1;
                    std::uint8_t ref_pic_index = tpl_src_stats_buffer->best_rf_idx >= 4
                        ? static_cast<std::uint8_t>(tpl_src_stats_buffer->best_rf_idx - 4)
                        : static_cast<std::uint8_t>(tpl_src_stats_buffer->best_rf_idx);

                    max_list_ref_idx[list_index] =
                        max_list_ref_idx[list_index] > ref_pic_index ? max_list_ref_idx[list_index] : ref_pic_index;
                    inter_cnt++;
                }

                tpl_src_stats_buffer += tpl_blk_step;
            }
        }

        *sb_inter_selection = static_cast<std::uint8_t>((inter_cnt * 100) / tot_cnt);
        *sb_max_list0_ref_idx = max_list_ref_idx[0];
        *sb_max_list1_ref_idx = max_list_ref_idx[1];
        return true;
    }
    return false;
}

// product_coding_loop.c:2957-:3045 perform_md_reference_pruning.
void performMdReferencePruning(FlCtx* ctx, const l16_rdo::RefPruningCtrls& pruning, const MdMeSbResults* me_results,
                               const TplPcsWindow* tpl_window, std::uint8_t picture_qp) {
    std::uint32_t early_inter_distortion_array[MAX_NUM_OF_REF_PIC_LIST * REF_LIST_MAX_DEPTH];
    std::memset(early_inter_distortion_array, 0xFE, sizeof(early_inter_distortion_array));
    std::uint32_t offset_tab[MAX_NUM_OF_REF_PIC_LIST][REF_LIST_MAX_DEPTH] = {{0}};
    std::memset(ctx->refFilteringRes, 0, sizeof(ctx->refFilteringRes));
    std::uint32_t min_dist = (std::uint32_t)~0;
    int use_tpl_info = 0;
    std::uint8_t sb_max_list0_ref_idx = 0;  // written whenever use_tpl_info becomes 1
    std::uint8_t sb_max_list1_ref_idx = 0;
    std::uint8_t sb_inter_selection = 0;
    if (pruning.useTplInfoOffset && tpl_window != nullptr && tpl_window->tpl_enable) {
        if (getSbTplInterStats(*tpl_window, ctx->sb_origin_x, ctx->sb_origin_y, &sb_inter_selection,
                               &sb_max_list0_ref_idx, &sb_max_list1_ref_idx)) {
            use_tpl_info = 1;
        }
    }
    const l16_rdo::MeSbResults me_view = meView(me_results);
    for (std::uint32_t ref_it = 0; ref_it < ctx->totRefFrameTypes; ++ref_it) {
        const std::int32_t ref_pair = ctx->refFrameTypeArr[ref_it];
        int rf[2];
        motion::amvp::av1SetRefFrame(rf, static_cast<std::int8_t>(ref_pair));

        if (rf[1] == motion::amvp::kNoneFrame) {
            std::uint32_t best_mvp_distortion = static_cast<std::uint32_t>(static_cast<std::int32_t>(~0));
            const std::uint8_t list_idx = motion::amvp::getListIdx(static_cast<std::uint8_t>(rf[0]));
            const std::uint8_t ref_idx = motion::amvp::getRefFrameIdx(static_cast<std::uint8_t>(rf[0]));
            // Only use TPL info if all references are tested
            if (use_tpl_info) {
                if ((list_idx == 0 && ref_idx > sb_max_list0_ref_idx) ||
                    (list_idx == 1 && ref_idx > sb_max_list1_ref_idx)) {
                    offset_tab[list_idx][ref_idx] += pruning.useTplInfoOffset;
                }
            }
            // Step 1: derive the best MVP in term of distortion
            best_mvp_distortion = ctx->bestFpMvpDist[list_idx][ref_idx];

            // Evaluate the PA_ME MVs (if available)
            std::uint32_t pa_me_distortion = (std::uint32_t)~0;  // any non zero value
            if (l16_rdo::isMeDataPresent(ctx->meBlockOffset, ctx->meCandOffset, &me_view, list_idx, ref_idx) != 0) {
                pa_me_distortion = ctx->fpMeDist[list_idx][ref_idx];
            }
            // early_inter_distortion_array
            early_inter_distortion_array[list_idx * REF_LIST_MAX_DEPTH + ref_idx] =
                pa_me_distortion < best_mvp_distortion ? pa_me_distortion : best_mvp_distortion;
            if (early_inter_distortion_array[list_idx * REF_LIST_MAX_DEPTH + ref_idx] < min_dist) {
                min_dist = early_inter_distortion_array[list_idx * REF_LIST_MAX_DEPTH + ref_idx];
            }
        }
    }
    const std::uint32_t th =
        (static_cast<std::uint32_t>(pruning.checkClosestMultiplier) * (ctx->geom.bheight * ctx->geom.bwidth) *
         picture_qp) /
        24;
    if (pruning.checkClosestMultiplier && early_inter_distortion_array[0] < th &&
        early_inter_distortion_array[REF_LIST_MAX_DEPTH] < th) {
        for (unsigned li = 0; li < MAX_NUM_OF_REF_PIC_LIST; li++) {
            for (unsigned ri = 0; ri < REF_LIST_MAX_DEPTH; ri++) {
                for (unsigned gi = 0; gi < TOT_INTER_GROUP; gi++) {
                    if (ri == 0 || pruning.maxDevToBest[gi] == (std::uint32_t)~0) {
                        ctx->refFilteringRes[gi][li][ri] = 1;
                    }
                }
            }
        }
    } else {
        // Sort early_inter_distortion_array
        const unsigned num_of_cand_to_sort = MAX_NUM_OF_REF_PIC_LIST * REF_LIST_MAX_DEPTH;
        std::uint32_t dev_to_the_best[MAX_NUM_OF_REF_PIC_LIST * REF_LIST_MAX_DEPTH] = {0};
        for (unsigned i = 0; i < num_of_cand_to_sort - 1; ++i) {
            const std::uint64_t hi = early_inter_distortion_array[i] > 1 ? early_inter_distortion_array[i] : 1;
            const std::uint64_t lo = min_dist > 1 ? min_dist : 1;
            dev_to_the_best[i] = static_cast<std::uint32_t>(((static_cast<std::int64_t>(hi) - static_cast<std::int64_t>(lo)) * 100) / static_cast<std::int64_t>(lo));
        }
        for (unsigned li = 0; li < MAX_NUM_OF_REF_PIC_LIST; li++) {
            for (unsigned ri = 0; ri < REF_LIST_MAX_DEPTH; ri++) {
                for (unsigned gi = 0; gi < TOT_INTER_GROUP; gi++) {
                    const std::uint32_t offset = offset_tab[li][ri];
                    const std::uint32_t pruning_th = (offset == (std::uint32_t)~0 || pruning.maxDevToBest[gi] == 0)
                        ? 0
                        : (pruning.maxDevToBest[gi] == (std::uint32_t)~0)
                        ? (std::uint32_t)~0
                        : (0 > (static_cast<std::int64_t>(pruning.maxDevToBest[gi]) - static_cast<std::int64_t>(offset))
                              ? 0
                              : static_cast<std::uint32_t>(static_cast<std::int64_t>(pruning.maxDevToBest[gi]) -
                                                           static_cast<std::int64_t>(offset)));

                    if (dev_to_the_best[li * REF_LIST_MAX_DEPTH + ri] < pruning_th) {
                        ctx->refFilteringRes[gi][li][ri] = 1;
                    }
                }
            }
        }
    }
}

}  // namespace mdc
}  // namespace pipeline
