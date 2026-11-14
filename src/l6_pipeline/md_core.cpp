// l6_pipeline::mdc - see md_core.h for the carrier conventions and the
// park list.
#include "md_core.h"

#include <cassert>

#include <md_slices.h>

namespace pipeline {
namespace mdc {

// definitions.h:457 ROUND_POWER_OF_TWO (the local-helper precedent).
static std::int64_t roundPowerOfTwo64(std::int64_t value, int n) {
    return (value + (((std::int64_t)1 << n) >> 1)) >> n;
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

}  // namespace mdc
}  // namespace pipeline
