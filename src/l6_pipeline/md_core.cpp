// l6_pipeline::mdc - see md_core.h for the carrier conventions and the
// park list.
#include "md_core.h"

#include <cassert>

#include <md_slices.h>
#include <packunpack.h>

namespace pipeline {
namespace mdc {

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

}  // namespace mdc
}  // namespace pipeline
