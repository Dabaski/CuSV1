#include "pipeline.h"

#include <analysis.h>
#include <cstring>
#include <string>

namespace pipeline {

namespace {

void predictResidualTxfm(const pixels::Plane& plane, int px, int py, const std::uint8_t* aboveRef, int nTopPx,
                         int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx, int nBottomLeftPx,
                         std::uint8_t aboveLeft, intra::PredictionMode mode, int angleDelta,
                         transforms::TxType txType, std::int32_t coeffs[16], std::uint8_t pred[16]) {
    intra::buildIntraPredictors(pred, 4, mode, angleDelta, 4, 4, aboveLeft, aboveRef, nTopPx, nTopRightPx,
                                leftRef, nLeftPx, nBottomLeftPx);

    std::int16_t residual[16] = {0};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            residual[y * 4 + x] = static_cast<std::int16_t>(plane.at(px + x, py + y) - pred[y * 4 + x]);
        }
    }

    transforms::fwdTxfm2d4x4(residual, coeffs, 4, txType);
}

}  // namespace

std::string subtractCuSource() {
    return R"CUDA(
extern "C" __global__ void subtract_4x4_plane(const unsigned char* src, const int* srcStride, const int* px,
                                              const int* py, const unsigned char* pred, short* residual) {
    const int idx = threadIdx.x;
    const int r = idx >> 2;
    const int c = idx & 3;
    residual[idx] = (short)(src[(*py + r) * (*srcStride) + (*px + c)] - pred[idx]);
}

extern "C" __global__ void subtract_8x8_plane(const unsigned char* src, const int* srcStride, const int* px,
                                              const int* py, const unsigned char* pred, short* residual) {
    const int idx = threadIdx.x;
    const int r = idx >> 3;
    const int c = idx & 7;
    residual[idx] = (short)(src[(*py + r) * (*srcStride) + (*px + c)] - pred[idx]);
}

extern "C" __global__ void subtract_16x16_plane(const unsigned char* src, const int* srcStride, const int* px,
                                                const int* py, const unsigned char* pred, short* residual) {
    const int idx = threadIdx.x;
    const int r = idx >> 4;
    const int c = idx & 15;
    residual[idx] = (short)(src[(*py + r) * (*srcStride) + (*px + c)] - pred[idx]);
}

extern "C" __global__ void subtract_32x32_plane(const unsigned char* src, const int* srcStride, const int* px,
                                                const int* py, const unsigned char* pred, short* residual) {
    const int idx = threadIdx.x;
    const int r = idx >> 5;
    const int c = idx & 31;
    residual[idx] = (short)(src[(*py + r) * (*srcStride) + (*px + c)] - pred[idx]);
}

// L9: 4096 pixels > 1024-thread max -> 4 pixels per thread (p = idx + 1024k)
extern "C" __global__ void subtract_64x64_plane(const unsigned char* src, const int* srcStride, const int* px,
                                                const int* py, const unsigned char* pred, short* residual) {
    const int idx = threadIdx.x;
    for (int k = 0; k < 4; ++k) {
        const int p = idx + 1024 * k;
        const int r = p >> 6;
        const int c = p & 63;
        residual[p] = (short)(src[(*py + r) * (*srcStride) + (*px + c)] - pred[p]);
    }
}
)CUDA";
}

void encodeBlock4x4(const pixels::Plane& plane, int px, int py, const std::uint8_t* aboveRef, int nTopPx,
                    int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx, int nBottomLeftPx,
                    std::uint8_t aboveLeft, intra::PredictionMode mode, int angleDelta,
                    transforms::TxType txType, std::int32_t coeffs[16]) {
    std::uint8_t pred[16] = {0};
    predictResidualTxfm(plane, px, py, aboveRef, nTopPx, nTopRightPx, leftRef, nLeftPx, nBottomLeftPx,
                        aboveLeft, mode, angleDelta, txType, coeffs, pred);
}

void encodeRecon4x4(const pixels::Plane& plane, int px, int py, const std::uint8_t* aboveRef, int nTopPx,
                    int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx, int nBottomLeftPx,
                    std::uint8_t aboveLeft, intra::PredictionMode mode, int angleDelta,
                    transforms::TxType txType, std::int32_t coeffs[16], std::uint8_t recon[16],
                    const intra::NeighborContext& neighbors, bool disableEdgeFilter) {
    std::uint8_t pred[16] = {0};
    intra::buildIntraPredictors(pred, 4, mode, angleDelta, 4, 4, aboveLeft, aboveRef, nTopPx, nTopRightPx,
                                leftRef, nLeftPx, nBottomLeftPx, neighbors, -1, disableEdgeFilter);

    std::int16_t residual[16] = {0};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            residual[y * 4 + x] = static_cast<std::int16_t>(plane.at(px + x, py + y) - pred[y * 4 + x]);
        }
    }

    transforms::fwdTxfm2d4x4(residual, coeffs, 4, txType);
    for (int i = 0; i < 16; ++i) {
        recon[i] = pred[i];
    }
    transforms::invTxfm2dAdd4x4(coeffs, recon, 4, txType);
}

void encodeFrameRecon4x4(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                         intra::PredictionMode mode, int angleDelta, transforms::TxType txType) {
    const int gridW = src.width() / 4;
    const int gridH = src.height() / 4;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 4;
            const int py = by * 4;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 4 : 0;
            const int nLeftPx = hasLeft ? 4 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 4 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[8] = {0};  // 2*B: above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[4] = {0};
            if (hasTop) {
                for (int i = 0; i < 4 + nTopRightPx; ++i) {
                    above[i] = recon.at(px + i, py - 1);
                }
            }
            if (hasLeft) {
                for (int i = 0; i < 4; ++i) {
                    left[i] = recon.at(px - 1, py + i);
                }
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[16] = {0};
            intra::buildIntraPredictors(pred, 4, mode, angleDelta, 4, 4, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[16] = {0};
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    residual[y * 4 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 4 + x]);
                }
            }
            std::int32_t cb[16] = {0};
            transforms::fwdTxfm2d4x4(residual, cb, 4, txType);
            for (int i = 0; i < 16; ++i) {
                coeffs[(by * gridW + bx) * 16 + i] = cb[i];
            }

            std::uint8_t tmp[16] = {0};
            for (int i = 0; i < 16; ++i) {
                tmp[i] = pred[i];
            }
            transforms::invTxfm2dAdd4x4(cb, tmp, 4, txType);
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    recon.at(px + x, py + y) = tmp[y * 4 + x];
                }
            }
        }
    }
}

std::int64_t frameMse8(const pixels::Plane& a, const pixels::Plane& b) {
    std::int64_t sse = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const std::int64_t d = static_cast<std::int64_t>(a.at(x, y)) - static_cast<std::int64_t>(b.at(x, y));
            sse += d * d;
        }
    }
    return sse;
}

// D2 policy (ours): all 13 PredictionModes, SAD-scored, lowest wins,
// tie-break = lowest mode index. Primitives are 1:1 SVT.
//
// RE5 (the RD-lite wiring rung): the ranking is now RD-lite, composed from
// the landed 1:1 cost surfaces, in the reference's own fast-path shape:
//   rd = RDCOST(lambda, intraFastRate, variance << 4)
// - DISTORTION: the variance kernel, NOT psyDistortion. Cited:
//   product_coding_loop.c:1008-:1016 - cand_bf->luma_fast_dist =
//   fn_ptr->vf(...); luma_fast_dist <<= 4. The psy family only enters the
//   reference's fast path when ac_bias is enabled (psy_adjust_rate_light /
//   psy_full_dist at product_coding_loop.c:4386/:4778-:4810) and our
//   ac_bias config is 0. The kernel consumed is l9's varianceCore
//   (C_DEFAULT/variance.c:141-158, the VAR(W,H) core) + the :189
//   mean-correction (variance = sse - (sum*sum)/(w*h)), via the court-
//   ordered av1_pipeline -> av1_analysis link.
// - RATE: the landed l16 intraFastCost (rd_cost.c:526-:640) with the walls
//   threaded from the frame configuration: isISlice=true, skipModeCtx=0
//   (our named skip=0 emission convention), FI/palette/intrabc off (the
//   pinned CONFIG), the kf ctx pair from the DECIDED neighbor modes via
//   l7 getKfYModeCtx, the CHOSEN angle delta (RE6: the 7-delta enumeration
//   at 8x8+ directional candidates, 0 elsewhere - see rdliteDecide), hasUv
//   FALSE for the luma ranking (the chroma D2 stays SAD this rung - the
//   ratified rung boundary).
// - LAMBDA: rdliteLambda (ours, named; the rc-walled
//   svt_aom_compute_rd_mult chain stays parked).
// - TIE-BREAK: strict < on rd - the reference's candidate preference rule
//   (product_coding_loop.c:1458: if (*cand_bf->fast_cost <
//   ctx->mds0_best_cost)), first candidate in the enumeration order wins
//   ties, which is our lowest-mode-index order.
// The SAD stays computed as bookkeeping (the ModeDecision::sad field).
namespace {

// RE5: a valid uniform n-symbol cdf row (the named-deviation fill for the
// seven syntax families the minimal l7 slice does not carry - see the
// initRdliteFrameCtx comment).
void fillUniformCdfRow(l16_rdo::AomCdfProb* row, int n) {
    const std::int32_t p = CDF_PROB_TOP / n;
    for (int i = 0; i < n; ++i) {
        row[i] = (l16_rdo::AomCdfProb)AOM_ICDF(CDF_PROB_TOP - (i + 1) * p);
    }
    row[n] = (l16_rdo::AomCdfProb)AOM_ICDF(CDF_PROB_TOP);
}

int rdliteBsizeEnum(int B) {
    return B == 4   ? entropy::BLOCK_4X4
           : B == 8 ? entropy::BLOCK_8X8
           : B == 16 ? entropy::BLOCK_16X16
           : B == 32 ? entropy::BLOCK_32X32
                     : entropy::BLOCK_64X64;
}

// RE6: the reference's angle_delta_count walk (mode_decision.c:3250-:3253):
// av1_is_directional_mode(intra_mode) && angular_pred_level <= 2 &&
// use_angle_delta ? 7 : 1. OUR RUNG'S angular-pred-level policy: 1 (the full
// {-3..+3} set; the reference's level-2/3 delta skips at :3259-:3261 never
// fire at level 1 - named, ours). The bsize gate is av1_use_angle_delta
// (intra_prediction.h:214-216) = bsize >= BLOCK_8X8: at 4x4 NO delta variants
// (deltaCount = 1, delta = 0). Per-delta cost: the SAME composition -
// buildIntraPredictors at the delta -> varianceCore -> <<4 -> intraFastCost
// with cand.angleDelta[0] = the delta (rate_est.cpp:1629-:1632 consumes it
// through angleDeltaFacBits).
ModeDecision rdliteDecide(const RdliteFrameCtx& rctx, int B, const std::uint8_t* src,
                          const std::uint8_t* aboveRef, int nTopPx, int nTopRightPx,
                          const std::uint8_t* leftRef, int nLeftPx, int nBottomLeftPx,
                          std::uint8_t aboveLeft, const intra::NeighborContext& neighbors) {
    ModeDecision best{intra::DC_PRED, 0, 0, 0};
    bool haveBest = false;
    const int bsize = rdliteBsizeEnum(B);
    const bool useAngleDelta = B >= 8;  // av1_use_angle_delta (intra_prediction.h:214-216)
    for (int m = intra::DC_PRED; m <= intra::PAETH_PRED; ++m) {
        const bool directional = m >= intra::V_PRED && m <= intra::D67_PRED;
        const int deltaCount = (directional && useAngleDelta) ? 7 : 1;
        for (int deltaCtr = 0; deltaCtr < deltaCount; ++deltaCtr) {
            const int delta =
                deltaCount == 1 ? 0 : deltaCtr - MAX_ANGLE_DELTA;  // the :3256-:3258 walk
            std::uint8_t pred[64 * 64];
            intra::buildIntraPredictors(pred, B, m, delta, B, B, aboveLeft, aboveRef, nTopPx,
                                        nTopRightPx, leftRef, nLeftPx, nBottomLeftPx, neighbors);
            std::uint32_t sad = 0;
            switch (B) {
            case 4: sad = motion::sad4x4(src, 4, pred, 4); break;
            case 8: sad = motion::sad8x8(src, 8, pred, 8); break;
            case 16: sad = motion::sad16x16(src, 16, pred, 16); break;
            case 32: sad = motion::sad32x32(src, 32, pred, 32); break;
            default: sad = motion::sad64x64(src, 64, pred, 64); break;
            }

            std::uint32_t sse = 0;
            int sum = 0;
            analysis::varianceCore(src, B, pred, B, B, B, &sse, &sum);
            const std::uint64_t variance = sse - (std::uint32_t)(((std::int64_t)sum * sum) / (B * B));
            const std::uint64_t lumaFastDist = variance << 4;

            l16_rdo::IntraFastCandidate cand{};
            cand.mode = m;
            cand.uvMode = intra::DC_PRED;
            cand.angleDelta[0] = delta;
            cand.angleDelta[1] = 0;
            cand.filterIntraMode = l6_rdlite::kFilterIntraModes;
            cand.paletteSize[0] = -1;
            cand.paletteSize[1] = -1;

            int topCtx = 0, leftCtx = 0;
            entropy::getKfYModeCtx(nLeftPx > 0 ? 1 : 0, (int)neighbors.leftMode,
                                   nTopPx > 0 ? 1 : 0, (int)neighbors.aboveMode, &topCtx, &leftCtx);

            std::uint32_t lumaRate = 0, chromaRate = 0;
            l16_rdo::IntraFastCostArgs args{};
            args.me = &rctx.me;
            args.cand = &cand;
            args.bsize = bsize;
            args.bwidth = B;
            args.bheight = B;
            args.lambda = rctx.lambda;
            args.lumaDistortion = lumaFastDist;
            args.fastLumaRate = &lumaRate;
            args.fastChromaRate = &chromaRate;
            args.skipModeCtx = 0;
            args.intraLumaTopCtx = topCtx;
            args.intraLumaLeftCtx = leftCtx;
            args.isInterCtx = 0;
            args.hasUv = false;
            args.paletteModeCtx = 0;
            args.isISlice = true;
            args.skipModeFlag = false;
            args.filterIntraLevel = 0;
            args.allowScreenContentTools = 0;
            args.frameAllowIntrabc = false;
            args.enablePalette = 0;
            args.dvJointCost = nullptr;
            args.dvCostRow[0] = nullptr;
            args.dvCostRow[1] = nullptr;
            args.blkOrgX = 0;
            args.blkOrgY = 0;
            const std::int64_t rd = l16_rdo::intraFastCost(args);

            if (!haveBest || rd < (std::int64_t)best.rd) {
                best = ModeDecision{static_cast<intra::PredictionMode>(m), sad,
                                    static_cast<std::uint64_t>(rd), delta};
                haveBest = true;
            }
        }
    }
    return best;
}

}  // namespace

void initRdliteFrameCtx(RdliteFrameCtx* ctx, std::int32_t qindex) {
    *ctx = RdliteFrameCtx{};

    entropy::EcFrameContext fc;
    entropy::initDefaultEcFrameContext(&fc, qindex);

    // The token families (estimateCoefficientsRate, the S7 span): every
    // family the l7 EcFrameContext carries.
    l16_rdo::FcTokenCdfs token;
    std::memcpy(token.eobFlagCdf16, fc.eob_flag_cdf16, sizeof(token.eobFlagCdf16));
    std::memcpy(token.eobFlagCdf32, fc.eob_flag_cdf32, sizeof(token.eobFlagCdf32));
    std::memcpy(token.eobFlagCdf64, fc.eob_flag_cdf64, sizeof(token.eobFlagCdf64));
    std::memcpy(token.eobFlagCdf128, fc.eob_flag_cdf128, sizeof(token.eobFlagCdf128));
    std::memcpy(token.eobFlagCdf256, fc.eob_flag_cdf256, sizeof(token.eobFlagCdf256));
    std::memcpy(token.eobFlagCdf512, fc.eob_flag_cdf512, sizeof(token.eobFlagCdf512));
    std::memcpy(token.eobFlagCdf1024, fc.eob_flag_cdf1024, sizeof(token.eobFlagCdf1024));
    std::memcpy(token.txbSkipCdf, fc.txb_skip_cdf, sizeof(token.txbSkipCdf));
    std::memcpy(token.coeffBaseEobCdf, fc.coeff_base_eob_cdf, sizeof(token.coeffBaseEobCdf));
    std::memcpy(token.coeffBaseCdf, fc.coeff_base_cdf, sizeof(token.coeffBaseCdf));
    std::memcpy(token.eobExtraCdf, fc.eob_extra_cdf, sizeof(token.eobExtraCdf));
    std::memcpy(token.dcSignCdf, fc.dc_sign_cdf, sizeof(token.dcSignCdf));
    std::memcpy(token.coeffBrCdf, fc.coeff_br_cdf, sizeof(token.coeffBrCdf));
    l16_rdo::estimateCoefficientsRate(&ctx->me, &token);

    // The syntax families (estimateSyntaxRate, the S8 span, the is_i_slice
    // path at flags 0): the carried families bridged, the seven
    // non-carried families uniform (the named deviation above).
    l16_rdo::FcSyntaxCdfs syn;
    std::memcpy(syn.partition_cdf, fc.partition_cdf, sizeof(syn.partition_cdf));
    std::memcpy(syn.skip_cdfs, fc.skip_cdfs, sizeof(syn.skip_cdfs));
    std::memcpy(syn.kf_y_cdf, fc.kf_y_cdf, sizeof(syn.kf_y_cdf));
    std::memcpy(syn.uv_mode_cdf, fc.uv_mode_cdf, sizeof(syn.uv_mode_cdf));
    std::memcpy(syn.angle_delta_cdf, fc.angle_delta_cdf, sizeof(syn.angle_delta_cdf));
    std::memcpy(syn.intra_ext_tx_cdf, fc.intra_ext_tx_cdf, sizeof(syn.intra_ext_tx_cdf));
    for (int i = 0; i < SKIP_CONTEXTS; ++i) {
        fillUniformCdfRow(syn.skip_mode_cdfs[i], 2);
    }
    for (int i = 0; i < BlockSize_GROUPS; ++i) {
        fillUniformCdfRow(syn.y_mode_cdf[i], entropy::INTRA_MODES);
    }
    for (int i = 0; i < SWITCHABLE_FILTER_CONTEXTS; ++i) {
        fillUniformCdfRow(syn.switchable_interp_cdf[i], SWITCHABLE_FILTERS);
    }
    fillUniformCdfRow(syn.cfl_sign_cdf, CFL_JOINT_SIGNS);
    for (int i = 0; i < CFL_ALPHA_CONTEXTS; ++i) {
        fillUniformCdfRow(syn.cfl_alpha_cdf[i], CFL_ALPHABET_SIZE);
    }
    for (int i = 0; i < MAX_TX_CATS; ++i) {
        for (int j = 0; j < TX_SIZE_CONTEXTS; ++j) {
            fillUniformCdfRow(syn.tx_size_cdf[i][j], MAX_TX_DEPTH + 1);
        }
    }
    for (int i = 0; i < TXFM_PARTITION_CONTEXTS; ++i) {
        fillUniformCdfRow(syn.txfm_partition_cdf[i], 2);
    }
    for (int s = 0; s < EXT_TX_SETS_INTER; ++s) {
        for (int t = 0; t < EXT_TX_SIZES; ++t) {
            fillUniformCdfRow(syn.inter_ext_tx_cdf[s][t], TX_TYPES);
        }
    }
    for (int i = 0; i < INTRA_INTER_CONTEXTS; ++i) {
        fillUniformCdfRow(syn.intra_inter_cdf[i], 2);
    }
    l16_rdo::estimateSyntaxRate(&ctx->me, 0, 0, 0, 0, &syn);
    l16_rdo::estimateIntraInterRate(&ctx->me, &syn);
    ctx->lambda = rdliteLambda(qindex);
}

std::uint32_t rdliteLambda(std::int32_t qindex) {
    return qindex <= 0 ? 0u : (std::uint32_t)qindex;
}

ModeDecision decideBlockMode4x4(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                int nBottomLeftPx, std::uint8_t aboveLeft,
                                const intra::NeighborContext& neighbors,
                                const RdliteFrameCtx& rdlite) {
    return rdliteDecide(rdlite, 4, src, aboveRef, nTopPx, nTopRightPx, leftRef, nLeftPx,
                        nBottomLeftPx, aboveLeft, neighbors);
}

void encodeFrameAuto4x4(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                        std::uint8_t* modes, transforms::TxType txType) {
    const int gridW = src.width() / 4;
    const int gridH = src.height() / 4;
    // RE5: one RD-lite decision context per frame, initialized from the
    // frame's own l7 context (the emission cdfs).
    RdliteFrameCtx rctx;
    initRdliteFrameCtx(&rctx, 0);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 4;
            const int py = by * 4;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 4 : 0;
            const int nLeftPx = hasLeft ? 4 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 4 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[8] = {0};  // 2*B: above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[4] = {0};
            if (hasTop) {
                for (int i = 0; i < 4 + nTopRightPx; ++i) {
                    above[i] = recon.at(px + i, py - 1);
                }
            }
            if (hasLeft) {
                for (int i = 0; i < 4; ++i) {
                    left[i] = recon.at(px - 1, py + i);
                }
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            // chosen neighbor modes feed filt_type (live at frame level);
            // unknown neighbors default to DC_PRED
            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[16] = {0};
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    srcBlk[y * 4 + x] = src.at(px + x, py + y);
                }
            }

            const ModeDecision d = decideBlockMode4x4(srcBlk, above, nTopPx, nTopRightPx, left,
                                                      nLeftPx, nBottomLeftPx, aboveLeft, nctx, rctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::int32_t cb[16] = {0};
            std::uint8_t blkRecon[16] = {0};
            encodeRecon4x4(src, px, py, above, nTopPx, nTopRightPx, left, nLeftPx, nBottomLeftPx,
                           aboveLeft, d.mode, d.angleDelta, txType, cb, blkRecon, nctx);
            for (int i = 0; i < 16; ++i) {
                coeffs[(by * gridW + bx) * 16 + i] = cb[i];
                recon.at(px + (i & 3), py + (i >> 2)) = blkRecon[i];
            }
        }
    }
}

// D2 policy generalized to 8x8: all 13 PredictionModes, sad8x8-scored
// (bit-exact with svt_nxm_sad_kernel_helper_c at 8x8), lowest wins,
// tie-break = lowest mode index. Primitives are 1:1 SVT.
ModeDecision decideBlockMode8x8(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                int nBottomLeftPx, std::uint8_t aboveLeft,
                                const intra::NeighborContext& neighbors,
                                const RdliteFrameCtx& rdlite) {
    return rdliteDecide(rdlite, 8, src, aboveRef, nTopPx, nTopRightPx, leftRef, nLeftPx,
                        nBottomLeftPx, aboveLeft, neighbors);
}

void encodeFrameRecon8x8(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                         intra::PredictionMode mode, int angleDelta, transforms::TxType txType) {
    const int gridW = src.width() / 8;
    const int gridH = src.height() / 8;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 8;
            const int py = by * 8;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 8 : 0;
            const int nLeftPx = hasLeft ? 8 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 8 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[16] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[16] = {0};
            if (hasTop) {
                for (int i = 0; i < 8 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 8; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[64] = {0};
            intra::buildIntraPredictors(pred, 8, mode, angleDelta, 8, 8, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[64] = {0};
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    residual[y * 8 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 8 + x]);

            std::int32_t cb[64] = {0};
            transforms::fwdTxfm2d8x8(residual, cb, 8, txType);
            for (int i = 0; i < 64; ++i) coeffs[(by * gridW + bx) * 64 + i] = cb[i];

            std::uint8_t tmp[64] = {0};
            for (int i = 0; i < 64; ++i) tmp[i] = pred[i];
            transforms::invTxfm2dAdd8x8(cb, tmp, 8, txType);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) recon.at(px + x, py + y) = tmp[y * 8 + x];
        }
    }
}

void encodeFrameAuto8x8(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                        std::uint8_t* modes, transforms::TxType txType) {
    const int gridW = src.width() / 8;
    const int gridH = src.height() / 8;
    // RE5: one RD-lite decision context per frame, initialized from the
    // frame's own l7 context (the emission cdfs).
    RdliteFrameCtx rctx;
    initRdliteFrameCtx(&rctx, 0);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 8;
            const int py = by * 8;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 8 : 0;
            const int nLeftPx = hasLeft ? 8 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 8 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[16] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[16] = {0};
            if (hasTop) {
                for (int i = 0; i < 8 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 8; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[64] = {0};
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) srcBlk[y * 8 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode8x8(srcBlk, above, nTopPx, nTopRightPx, left,
                                                      nLeftPx, nBottomLeftPx, aboveLeft, nctx, rctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[64] = {0};
            intra::buildIntraPredictors(pred, 8, d.mode, d.angleDelta, 8, 8, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);

            std::int16_t residual[64] = {0};
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    residual[y * 8 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 8 + x]);

            std::int32_t cb[64] = {0};
            transforms::fwdTxfm2d8x8(residual, cb, 8, txType);
            for (int i = 0; i < 64; ++i) coeffs[(by * gridW + bx) * 64 + i] = cb[i];

            std::uint8_t reconBlk[64] = {0};
            for (int i = 0; i < 64; ++i) reconBlk[i] = pred[i];
            transforms::invTxfm2dAdd8x8(cb, reconBlk, 8, txType);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) recon.at(px + x, py + y) = reconBlk[y * 8 + x];
        }
    }
}

// Q2: D3 loop with the FP quantizer wired at a fixed qindex
// (quantize_fp_helper_c at log_scale 0). qcoeff = coded coeffs; dqcoeff
// feeds the inverse, so recon carries real quantization loss.
// SCAN POLICY IS OURS: this run uses the fixed default scan (defaultScan4x4,
// the svt_aom_init_iscan up-right diagonal) for every block; SVT selects the
// scan per mode/tx type via get_scan_order (coefficients.h:40). Fixing the
// scan is a scope decision, attributed to no one else.
void encodeFrameAuto4x4Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                         std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType,
                         entropy::AomWriter* w, entropy::EcFrameContext* fc,
                         entropy::DcSignLevelCoeffNa* na) {
    const int gridW = src.width() / 4;
    const int gridH = src.height() / 4;
    // RE5: one RD-lite decision context per frame, initialized from the
    // frame's own l7 context (the emission cdfs).
    RdliteFrameCtx rctx;
    initRdliteFrameCtx(&rctx, qindex);
    // FS3: the bucket follows the frame's base_q_idx (spec init_coeff_cdfs).
    if (w && fc) {
        entropy::initDefaultEcFrameContext(fc, qindex);
        entropy::odEcEncReset(&w->ec);
        w->allow_update_cdf = 1;  // THE COUPLING (BSF4-fix): see encodeFrameAuto16x16Q.
        w->pos = 0;
    }
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[16];
    transforms::defaultScan4x4(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 4;
            const int py = by * 4;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 4 : 0;
            const int nLeftPx = hasLeft ? 4 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 4 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[8] = {0};  // 2*B: above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[4] = {0};
            if (hasTop) {
                for (int i = 0; i < 4 + nTopRightPx; ++i) {
                    above[i] = recon.at(px + i, py - 1);
                }
            }
            if (hasLeft) {
                for (int i = 0; i < 4; ++i) {
                    left[i] = recon.at(px - 1, py + i);
                }
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[16] = {0};
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    srcBlk[y * 4 + x] = src.at(px + x, py + y);
                }
            }

            const ModeDecision d = decideBlockMode4x4(srcBlk, above, nTopPx, nTopRightPx, left,
                                                      nLeftPx, nBottomLeftPx, aboveLeft, nctx, rctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            // predict -> residual -> fwd -> quantize fp -> inverse on dqcoeff
            std::uint8_t pred[16] = {0};
            intra::buildIntraPredictors(pred, 4, d.mode, d.angleDelta, 4, 4, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);
            std::int16_t residual[16] = {0};
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    residual[y * 4 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 4 + x]);
                }
            }
            std::int32_t cb[16] = {0};
            transforms::fwdTxfm2d4x4(residual, cb, 4, txType);
            std::int32_t qc[16] = {0};
            std::int32_t dq[16] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp4x4(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 16; ++i) {
                coeffs[(by * gridW + bx) * 16 + i] = qc[i];
            }

            // FS3: the token chain. A 4x4 frame emits NO partition symbol
            // (the 8x8-level walk in a 1x1-mi frame is forced-split; the 4x4
            // quadrant is coded directly) - the skip/kf/FI symbols only.
            if (w && fc && na) {
                entropy::writeSkip(w, fc, 0, 0);
                int topCtx = 0, leftCtx = 0;
                entropy::getKfYModeCtx(hasLeft ? 1 : 0, static_cast<int>(nctx.leftMode),
                                       hasTop ? 1 : 0, static_cast<int>(nctx.aboveMode),
                                       &topCtx, &leftCtx);
                entropy::writeKfLumaMode(w, fc, entropy::BLOCK_4X4,
                                         static_cast<entropy::PredictionMode>(d.mode), topCtx,
                                         leftCtx, d.angleDelta);
                if (entropy::filterIntraAllowed(1, entropy::BLOCK_4X4, 0,
                                                static_cast<std::uint32_t>(d.mode))) {
                    // The ADAPTING frame-context cdf (SVT entropy_coding.c:5052
                    // writes through frame_context->filter_intra_cdfs[bsize]):
                    // every conforming decoder adapts its cdf after each read,
                    // so a local default cdf diverges on the SECOND FI symbol
                    // of a frame (the d4/d32 decode-gate failure root cause).
                    // Default row: cabac_context_model.c:618 AOM_CDF2(4621)
                    // -> ICDF = 32768 - 4621 = 28147.
                    entropy::odEcWriteSymbol(w, 0, fc->filter_intra_cdfs[entropy::BLOCK_4X4], 2);
                }
                entropy::writeBlockCoeffs(w, fc, na, qc, scan, entropy::TX_4X4,
                                          entropy::BLOCK_4X4, eob, by, bx, 1,
                                          static_cast<entropy::PredictionMode>(d.mode),
                                          entropy::COMPONENT_LUMA);
            }

            std::uint8_t blk[16] = {0};
            for (int i = 0; i < 16; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd4x4(dq, blk, 4, txType);
            for (int i = 0; i < 16; ++i) {
                recon.at(px + (i & 3), py + (i >> 2)) = blk[i];
            }
        }
    }
    if (w) entropy::odEcStopEncode(w);
}

// QW2: 8x8 quantized compositions. Both wire quantizeFp8x8 (n_coeffs=64,
// log_scale 0) after fwdTxfm2d8x8; qcoeff = coded coeffs; dqcoeff feeds
// invTxfm2dAdd8x8 onto the raw predictor, so recon carries real
// quantization loss. SCAN POLICY IS OURS: fixed defaultScan8x8 for every
// block; SVT selects per mode/tx type via get_scan_order (coefficients.h:40).
void encodeFrameRecon8x8Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                          intra::PredictionMode mode, int angleDelta, std::int32_t qindex,
                          transforms::TxType txType) {
    const int gridW = src.width() / 8;
    const int gridH = src.height() / 8;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[64];
    transforms::defaultScan8x8(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 8;
            const int py = by * 8;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 8 : 0;
            const int nLeftPx = hasLeft ? 8 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 8 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[16] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[16] = {0};
            if (hasTop) {
                for (int i = 0; i < 8 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 8; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[64] = {0};
            intra::buildIntraPredictors(pred, 8, mode, angleDelta, 8, 8, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[64] = {0};
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    residual[y * 8 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 8 + x]);

            std::int32_t cb[64] = {0};
            transforms::fwdTxfm2d8x8(residual, cb, 8, txType);
            std::int32_t qc[64] = {0};
            std::int32_t dq[64] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp8x8(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 64; ++i) coeffs[(by * gridW + bx) * 64 + i] = qc[i];

            std::uint8_t blk[64] = {0};
            for (int i = 0; i < 64; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd8x8(dq, blk, 8, txType);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) recon.at(px + x, py + y) = blk[y * 8 + x];
        }
    }
}

void encodeFrameAuto8x8Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                         std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType,
                         entropy::AomWriter* w, entropy::EcFrameContext* fc,
                         entropy::DcSignLevelCoeffNa* na) {
    const int gridW = src.width() / 8;
    const int gridH = src.height() / 8;
    // RE5: one RD-lite decision context per frame, initialized from the
    // frame's own l7 context (the emission cdfs).
    RdliteFrameCtx rctx;
    initRdliteFrameCtx(&rctx, qindex);
    // FS3: the bucket follows the frame's base_q_idx (spec init_coeff_cdfs).
    if (w && fc) {
        entropy::initDefaultEcFrameContext(fc, qindex);
        entropy::odEcEncReset(&w->ec);
        w->allow_update_cdf = 1;  // THE COUPLING (BSF4-fix): see encodeFrameAuto16x16Q.
        w->pos = 0;
    }
    // FS5: the running partition contexts (coding_loop.c:1700-1713) across
    // the grid - above indexed by mi column, left by (mi_row & 15). For the
    // uniform PARTITION_NONE grids these coincide with fresh INVALID cells
    // (the lookup bit at the leaf's own bsl is 0 for every square size:
    // lookupLeft[32X32]=24 bit2=0, [16X16]=28 bit1=0, [8X8]=30 bit0=0), but
    // the wiring is the aom-faithful contract and pins the mixed-tree
    // surface.
    std::uint8_t pAboveRun[64];
    std::uint8_t pLeftRun[16];
    memset(pAboveRun, INVALID_NEIGHBOR_DATA, sizeof(pAboveRun));
    memset(pLeftRun, INVALID_NEIGHBOR_DATA, sizeof(pLeftRun));
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[64];
    transforms::defaultScan8x8(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 8;
            const int py = by * 8;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 8 : 0;
            const int nLeftPx = hasLeft ? 8 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 8 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[16] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[16] = {0};
            if (hasTop) {
                for (int i = 0; i < 8 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 8; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[64] = {0};
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) srcBlk[y * 8 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode8x8(srcBlk, above, nTopPx, nTopRightPx, left,
                                                      nLeftPx, nBottomLeftPx, aboveLeft, nctx, rctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[64] = {0};
            intra::buildIntraPredictors(pred, 8, d.mode, d.angleDelta, 8, 8, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);
            std::int16_t residual[64] = {0};
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    residual[y * 8 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 8 + x]);

            std::int32_t cb[64] = {0};
            transforms::fwdTxfm2d8x8(residual, cb, 8, txType);
            std::int32_t qc[64] = {0};
            std::int32_t dq[64] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp8x8(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 64; ++i) coeffs[(by * gridW + bx) * 64 + i] = qc[i];

            // FS3: the per-block symbol walk. An 8x8 frame reads its
            // partition symbol from the 4-symbol BLOCK_8X8 row
            // (svt_aom_partition_cdf_length, entropy_coding.c:922-930);
            // every block of the grid codes one.
            if (w && fc && na) {
                entropy::writePartition(w, fc, entropy::BLOCK_8X8, 1, 1, pAboveRun[bx],
                                        pLeftRun[by & 15], entropy::PARTITION_NONE);
                entropy::updatePartitionContext(pAboveRun, pLeftRun, by, bx, entropy::BLOCK_8X8);
                entropy::writeSkip(w, fc, 0, 0);
                int topCtx = 0, leftCtx = 0;
                entropy::getKfYModeCtx(hasLeft ? 1 : 0, static_cast<int>(nctx.leftMode),
                                       hasTop ? 1 : 0, static_cast<int>(nctx.aboveMode),
                                       &topCtx, &leftCtx);
                entropy::writeKfLumaMode(w, fc, entropy::BLOCK_8X8,
                                         static_cast<entropy::PredictionMode>(d.mode), topCtx,
                                         leftCtx, d.angleDelta);
                if (entropy::filterIntraAllowed(1, entropy::BLOCK_8X8, 0,
                                                static_cast<std::uint32_t>(d.mode))) {
                    // The ADAPTING frame-context cdf (SVT entropy_coding.c:5052;
                    // see the 4x4 site note). Default:
                    // cabac_context_model.c:618 AOM_CDF2(7866)
                    // -> ICDF = 32768 - 7866 = 24902.
                    entropy::odEcWriteSymbol(w, 0, fc->filter_intra_cdfs[entropy::BLOCK_8X8], 2);
                }
                entropy::writeBlockCoeffs(w, fc, na, qc, scan, entropy::TX_8X8,
                                          entropy::BLOCK_8X8, eob, by * 2, bx * 2, 1,
                                          static_cast<entropy::PredictionMode>(d.mode),
                                          entropy::COMPONENT_LUMA);
            }

            std::uint8_t blk[64] = {0};
            for (int i = 0; i < 64; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd8x8(dq, blk, 8, txType);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) recon.at(px + x, py + y) = blk[y * 8 + x];
        }
    }
    if (w) entropy::odEcStopEncode(w);
}

// C7 policy (16x16): all 13 PredictionModes, sad16x16-scored
// (bit-exact with svt_nxm_sad_kernel_helper_c at 16x16), lowest wins,
// tie-break = lowest mode index. Primitives are 1:1 SVT.
ModeDecision decideBlockMode16x16(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                int nBottomLeftPx, std::uint8_t aboveLeft,
                                const intra::NeighborContext& neighbors,
                                const RdliteFrameCtx& rdlite) {
    return rdliteDecide(rdlite, 16, src, aboveRef, nTopPx, nTopRightPx, leftRef, nLeftPx,
                        nBottomLeftPx, aboveLeft, neighbors);
}

// L5 policy (32x32): all 13 PredictionModes, sad32x32-scored
// (bit-exact with svt_nxm_sad_kernel_helper_c at 32x32), lowest wins,
// tie-break = lowest mode index. Primitives are 1:1 SVT.
ModeDecision decideBlockMode32x32(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                int nBottomLeftPx, std::uint8_t aboveLeft,
                                const intra::NeighborContext& neighbors,
                                const RdliteFrameCtx& rdlite) {
    return rdliteDecide(rdlite, 32, src, aboveRef, nTopPx, nTopRightPx, leftRef, nLeftPx,
                        nBottomLeftPx, aboveLeft, neighbors);
}

// L9 policy (64x64): all 13 PredictionModes, sad64x64-scored
// (bit-exact with svt_nxm_sad_kernel_helper_c at 64x64), lowest wins,
// tie-break = lowest mode index. Primitives are 1:1 SVT.
ModeDecision decideBlockMode64x64(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                int nBottomLeftPx, std::uint8_t aboveLeft,
                                const intra::NeighborContext& neighbors,
                                const RdliteFrameCtx& rdlite) {
    return rdliteDecide(rdlite, 64, src, aboveRef, nTopPx, nTopRightPx, leftRef, nLeftPx,
                        nBottomLeftPx, aboveLeft, neighbors);
}

// ---- C7: 16x16 frame compositions. M1 availability + REAL recon top-right
// gather (FR-series); 16x16 roundtrip is exact so recon == source.
void encodeFrameRecon16x16(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           intra::PredictionMode mode, int angleDelta, transforms::TxType txType) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictors(pred, 16, mode, angleDelta, 16, 16, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = cb[i];

            std::uint8_t tmp[256] = {0};
            for (int i = 0; i < 256; ++i) tmp[i] = pred[i];
            transforms::invTxfm2dAdd16x16(cb, tmp, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = tmp[y * 16 + x];
        }
    }
}

void encodeFrameAuto16x16(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                          std::uint8_t* modes, transforms::TxType txType, entropy::AomWriter* w,
                          entropy::EcFrameContext* fc) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    // RE5: one RD-lite decision context per frame, initialized from the
    // frame's own l7 context (the emission cdfs).
    RdliteFrameCtx rctx;
    initRdliteFrameCtx(&rctx, 0);
    // BSF1 symbol emission (D5): kf y mode + angle delta + FI flag=0 only.
    if (w && fc) {
        // TD5b: the lossless variant carries q0 (CodedLossless) - bucket 0.
        entropy::initDefaultEcFrameContext(fc, 0);
        entropy::odEcEncReset(&w->ec);
        w->allow_update_cdf = 1;  // THE COUPLING (BSF4-fix): SVT sets ec_writer.allow_update_cdf = !disable_cdf_update (ec_process.c:101). This 1 is only correct because the ratified structural config carries disable_cdf_update = 0 (resource_coordination_process.c:360). ANY future config with disable_cdf_update = 1 (should_disable_cdf_update, enc_mode_config.c:9559 - future work) must flip this emission with it, or the stream is unspecifiable: the writer adapts CDFs a conformant decoder (allow_update_cdf = 0) will not replay.
        w->pos = 0;
    }
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) srcBlk[y * 16 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode16x16(srcBlk, above, nTopPx, nTopRightPx, left,
                                                        nLeftPx, nBottomLeftPx, aboveLeft, nctx, rctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            if (w && fc) {
                int topCtx = 0, leftCtx = 0;
                entropy::getKfYModeCtx(hasLeft ? 1 : 0, static_cast<int>(nctx.leftMode),
                                       hasTop ? 1 : 0, static_cast<int>(nctx.aboveMode), &topCtx, &leftCtx);
                entropy::writeKfLumaMode(w, fc, entropy::BLOCK_16X16,
                                         static_cast<entropy::PredictionMode>(d.mode), topCtx, leftCtx, d.angleDelta);
                if (entropy::filterIntraAllowed(1, entropy::BLOCK_16X16, 0,
                                                static_cast<std::uint32_t>(d.mode))) {
                    // The ADAPTING frame-context cdf (SVT entropy_coding.c:5052;
                    // see the 4x4 site note). Default:
                    // cabac_context_model.c:618 AOM_CDF2(12408)
                    // -> ICDF = 32768 - 12408 = 20360.
                    entropy::odEcWriteSymbol(w, 0, fc->filter_intra_cdfs[entropy::BLOCK_16X16], 2);
                }
            }

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictors(pred, 16, d.mode, d.angleDelta, 16, 16, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);

            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = cb[i];

            std::uint8_t reconBlk[256] = {0};
            for (int i = 0; i < 256; ++i) reconBlk[i] = pred[i];
            transforms::invTxfm2dAdd16x16(cb, reconBlk, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = reconBlk[y * 16 + x];
        }
    }
    if (w) entropy::odEcStopEncode(w);
}

void encodeFrameRecon16x16Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                            intra::PredictionMode mode, int angleDelta, std::int32_t qindex,
                            transforms::TxType txType) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[256];
    transforms::defaultScan16x16(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictors(pred, 16, mode, angleDelta, 16, 16, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            std::int32_t qc[256] = {0};
            std::int32_t dq[256] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp16x16(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = qc[i];

            std::uint8_t blk[256] = {0};
            for (int i = 0; i < 256; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd16x16(dq, blk, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = blk[y * 16 + x];
        }
    }
}

// C7: D2 policy loop at 16x16 with the FP quantizer wired at a fixed qindex.
// SCAN POLICY IS OURS: fixed defaultScan16x16 for every block (see
// encodeFrameAuto4x4Q); SVT selects per mode/tx type via get_scan_order.
void encodeFrameAuto16x16Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType,
                           entropy::AomWriter* w, entropy::EcFrameContext* fc,
                           entropy::DcSignLevelCoeffNa* na) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    // RE5: one RD-lite decision context per frame, initialized from the
    // frame's own l7 context (the emission cdfs).
    RdliteFrameCtx rctx;
    initRdliteFrameCtx(&rctx, qindex);
    // BSF1 symbol emission (D5): identical surface to the lossless variant.
    if (w && fc) {
        // TD5b: the bucket follows the frame's base_q_idx (spec init_coeff_cdfs).
        entropy::initDefaultEcFrameContext(fc, qindex);
        entropy::odEcEncReset(&w->ec);
        w->allow_update_cdf = 1;  // THE COUPLING (BSF4-fix): SVT sets ec_writer.allow_update_cdf = !disable_cdf_update (ec_process.c:101). This 1 is only correct because the ratified structural config carries disable_cdf_update = 0 (resource_coordination_process.c:360). ANY future config with disable_cdf_update = 1 (should_disable_cdf_update, enc_mode_config.c:9559 - future work) must flip this emission with it, or the stream is unspecifiable: the writer adapts CDFs a conformant decoder (allow_update_cdf = 0) will not replay.
        w->pos = 0;
    }
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[256];
    transforms::defaultScan16x16(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) srcBlk[y * 16 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode16x16(srcBlk, above, nTopPx, nTopRightPx, left,
                                                        nLeftPx, nBottomLeftPx, aboveLeft, nctx, rctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            if (w && fc) {
                int topCtx = 0, leftCtx = 0;
                entropy::getKfYModeCtx(hasLeft ? 1 : 0, static_cast<int>(nctx.leftMode),
                                       hasTop ? 1 : 0, static_cast<int>(nctx.aboveMode), &topCtx, &leftCtx);
                entropy::writeKfLumaMode(w, fc, entropy::BLOCK_16X16,
                                         static_cast<entropy::PredictionMode>(d.mode), topCtx, leftCtx, d.angleDelta);
                if (entropy::filterIntraAllowed(1, entropy::BLOCK_16X16, 0,
                                                static_cast<std::uint32_t>(d.mode))) {
                    // The ADAPTING frame-context cdf (SVT entropy_coding.c:5052;
                    // see the 4x4 site note). Default:
                    // cabac_context_model.c:618 AOM_CDF2(12408)
                    // -> ICDF = 32768 - 12408 = 20360.
                    entropy::odEcWriteSymbol(w, 0, fc->filter_intra_cdfs[entropy::BLOCK_16X16], 2);
                }
            }

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictors(pred, 16, d.mode, d.angleDelta, 16, 16, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);
            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            std::int32_t qc[256] = {0};
            std::int32_t dq[256] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp16x16(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = qc[i];

            // TS3: token emission (skip = 0 for all blocks - no skip decision
            // logic; the residual is coded for every block). reduced_tx_set=1,
            // decided intra_dir = d.mode.
            if (w && fc && na) {
                entropy::writeBlockCoeffs(w, fc, na, qc, scan, entropy::TX_16X16,
                                          entropy::BLOCK_16X16, eob, by * 4, bx * 4,
                                          1, static_cast<entropy::PredictionMode>(d.mode),
                                          entropy::COMPONENT_LUMA);
            }

            std::uint8_t blk[256] = {0};
            for (int i = 0; i < 256; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd16x16(dq, blk, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = blk[y * 16 + x];
        }
    }
    if (w) entropy::odEcStopEncode(w);
}

// ---- L6: 32x32 frame compositions. M1 availability + REAL recon top-right
// gather (FR-series); 32x32 roundtrip is exact so recon == source.
void encodeFrameRecon32x32(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           intra::PredictionMode mode, int angleDelta, transforms::TxType txType) {
    const int gridW = src.width() / 32;
    const int gridH = src.height() / 32;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 32;
            const int py = by * 32;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 32 : 0;
            const int nLeftPx = hasLeft ? 32 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 32 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[64] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[64] = {0};
            if (hasTop) {
                for (int i = 0; i < 32 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 32; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[1024] = {0};
            intra::buildIntraPredictors(pred, 32, mode, angleDelta, 32, 32, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[1024] = {0};
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    residual[y * 32 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 32 + x]);

            std::int32_t cb[1024] = {0};
            transforms::fwdTxfm2d32x32(residual, cb, 32, txType);
            for (int i = 0; i < 1024; ++i) coeffs[(by * gridW + bx) * 1024 + i] = cb[i];

            std::uint8_t tmp[1024] = {0};
            for (int i = 0; i < 1024; ++i) tmp[i] = pred[i];
            transforms::invTxfm2dAdd32x32(cb, tmp, 32, txType);
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) recon.at(px + x, py + y) = tmp[y * 32 + x];
        }
    }
}

void encodeFrameAuto32x32(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                          std::uint8_t* modes, transforms::TxType txType) {
    const int gridW = src.width() / 32;
    const int gridH = src.height() / 32;
    // RE5: one RD-lite decision context per frame, initialized from the
    // frame's own l7 context (the emission cdfs).
    RdliteFrameCtx rctx;
    initRdliteFrameCtx(&rctx, 0);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 32;
            const int py = by * 32;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 32 : 0;
            const int nLeftPx = hasLeft ? 32 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 32 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[64] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[64] = {0};
            if (hasTop) {
                for (int i = 0; i < 32 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 32; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[1024] = {0};
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) srcBlk[y * 32 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode32x32(srcBlk, above, nTopPx, nTopRightPx, left,
                                                        nLeftPx, nBottomLeftPx, aboveLeft, nctx, rctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[1024] = {0};
            intra::buildIntraPredictors(pred, 32, d.mode, d.angleDelta, 32, 32, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);

            std::int16_t residual[1024] = {0};
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    residual[y * 32 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 32 + x]);

            std::int32_t cb[1024] = {0};
            transforms::fwdTxfm2d32x32(residual, cb, 32, txType);
            for (int i = 0; i < 1024; ++i) coeffs[(by * gridW + bx) * 1024 + i] = cb[i];

            std::uint8_t reconBlk[1024] = {0};
            for (int i = 0; i < 1024; ++i) reconBlk[i] = pred[i];
            transforms::invTxfm2dAdd32x32(cb, reconBlk, 32, txType);
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) recon.at(px + x, py + y) = reconBlk[y * 32 + x];
        }
    }
}

void encodeFrameRecon32x32Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                            intra::PredictionMode mode, int angleDelta, std::int32_t qindex,
                            transforms::TxType txType) {
    const int gridW = src.width() / 32;
    const int gridH = src.height() / 32;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[1024];
    transforms::defaultScan32x32(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 32;
            const int py = by * 32;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 32 : 0;
            const int nLeftPx = hasLeft ? 32 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 32 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[64] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[64] = {0};
            if (hasTop) {
                for (int i = 0; i < 32 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 32; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[1024] = {0};
            intra::buildIntraPredictors(pred, 32, mode, angleDelta, 32, 32, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[1024] = {0};
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    residual[y * 32 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 32 + x]);

            std::int32_t cb[1024] = {0};
            transforms::fwdTxfm2d32x32(residual, cb, 32, txType);
            std::int32_t qc[1024] = {0};
            std::int32_t dq[1024] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp32x32(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 1024; ++i) coeffs[(by * gridW + bx) * 1024 + i] = qc[i];

            std::uint8_t blk[1024] = {0};
            for (int i = 0; i < 1024; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd32x32(dq, blk, 32, txType);
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) recon.at(px + x, py + y) = blk[y * 32 + x];
        }
    }
}

// L6: D2 policy loop at 32x32 with the FP quantizer wired at a fixed qindex
// (log_scale 1). SCAN POLICY IS OURS: fixed defaultScan32x32 for every block
// (see encodeFrameAuto4x4Q); SVT selects per mode/tx type via get_scan_order.
void encodeFrameAuto32x32Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType,
                           entropy::AomWriter* w, entropy::EcFrameContext* fc,
                           entropy::DcSignLevelCoeffNa* na) {
    const int gridW = src.width() / 32;
    const int gridH = src.height() / 32;
    // RE5: one RD-lite decision context per frame, initialized from the
    // frame's own l7 context (the emission cdfs).
    RdliteFrameCtx rctx;
    initRdliteFrameCtx(&rctx, qindex);
    // FS3: the bucket follows the frame's base_q_idx (spec init_coeff_cdfs).
    if (w && fc) {
        entropy::initDefaultEcFrameContext(fc, qindex);
        entropy::odEcEncReset(&w->ec);
        w->allow_update_cdf = 1;  // THE COUPLING (BSF4-fix): see encodeFrameAuto16x16Q.
        w->pos = 0;
    }
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[1024];
    transforms::defaultScan32x32(scan);
    // FS5: the running partition contexts (coding_loop.c:1700-1713) across
    // the grid - above indexed by mi column, left by (mi_row & 15).
    std::uint8_t pAboveRun[64];
    std::uint8_t pLeftRun[16];
    memset(pAboveRun, INVALID_NEIGHBOR_DATA, sizeof(pAboveRun));
    memset(pLeftRun, INVALID_NEIGHBOR_DATA, sizeof(pLeftRun));
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 32;
            const int py = by * 32;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 32 : 0;
            const int nLeftPx = hasLeft ? 32 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 32 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[64] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[64] = {0};
            if (hasTop) {
                for (int i = 0; i < 32 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 32; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[1024] = {0};
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) srcBlk[y * 32 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode32x32(srcBlk, above, nTopPx, nTopRightPx, left,
                                                        nLeftPx, nBottomLeftPx, aboveLeft, nctx, rctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[1024] = {0};
            intra::buildIntraPredictors(pred, 32, d.mode, d.angleDelta, 32, 32, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);
            std::int16_t residual[1024] = {0};
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    residual[y * 32 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 32 + x]);

            std::int32_t cb[1024] = {0};
            transforms::fwdTxfm2d32x32(residual, cb, 32, txType);
            std::int32_t qc[1024] = {0};
            std::int32_t dq[1024] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp32x32(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 1024; ++i) coeffs[(by * gridW + bx) * 1024 + i] = qc[i];

            // FS3: the per-block symbol walk. A 32x32 frame reads its
            // partition symbol from the 10-symbol BLOCK_32X32 row; every
            // block of the grid codes one. The tx-type symbol is NOT
            // written (DCTONLY at 32x32 intra: 1 type, entropy_coding.c:321-322
            // via the FS3-prep gate inside writeBlockCoeffs).
            if (w && fc && na) {
                entropy::writePartition(w, fc, entropy::BLOCK_32X32, 1, 1, pAboveRun[bx],
                                        pLeftRun[by & 15], entropy::PARTITION_NONE);
                entropy::updatePartitionContext(pAboveRun, pLeftRun, by, bx, entropy::BLOCK_32X32);
                entropy::writeSkip(w, fc, 0, 0);
                int topCtx = 0, leftCtx = 0;
                entropy::getKfYModeCtx(hasLeft ? 1 : 0, static_cast<int>(nctx.leftMode),
                                       hasTop ? 1 : 0, static_cast<int>(nctx.aboveMode),
                                       &topCtx, &leftCtx);
                entropy::writeKfLumaMode(w, fc, entropy::BLOCK_32X32,
                                         static_cast<entropy::PredictionMode>(d.mode), topCtx,
                                         leftCtx, d.angleDelta);
                if (entropy::filterIntraAllowed(1, entropy::BLOCK_32X32, 0,
                                                static_cast<std::uint32_t>(d.mode))) {
                    // The ADAPTING frame-context cdf (SVT entropy_coding.c:5052;
                    // see the 4x4 site note). Default:
                    // cabac_context_model.c:618-623 BLOCK_32X32 row
                    // AOM_CDF2(22343) -> ICDF = 32768 - 22343 = 10425.
                    entropy::odEcWriteSymbol(w, 0, fc->filter_intra_cdfs[entropy::BLOCK_32X32], 2);
                }
                entropy::writeBlockCoeffs(w, fc, na, qc, scan, entropy::TX_32X32,
                                          entropy::BLOCK_32X32, eob, by * 8, bx * 8, 1,
                                          static_cast<entropy::PredictionMode>(d.mode),
                                          entropy::COMPONENT_LUMA);
            }

            std::uint8_t blk[1024] = {0};
            for (int i = 0; i < 1024; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd32x32(dq, blk, 32, txType);
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) recon.at(px + x, py + y) = blk[y * 32 + x];
        }
    }
    if (w) entropy::odEcStopEncode(w);
}

// ---- L9: 64x64 frame compositions. M1 availability + REAL recon top-right
// gather (FR-series); the 64x64 fwd has net shift 0 - (2-2-2) and the inverse
// -(2+4): roundtrip is lossy like 8x8/32x32 (recon != source).
void encodeFrameRecon64x64(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           intra::PredictionMode mode, int angleDelta, transforms::TxType txType) {
    (void)txType;  // DCT-only at 64x64
    const int gridW = src.width() / 64;
    const int gridH = src.height() / 64;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 64;
            const int py = by * 64;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 64 : 0;
            const int nLeftPx = hasLeft ? 64 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 64 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[128] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[128] = {0};
            if (hasTop) {
                for (int i = 0; i < 64 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 64; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[4096] = {0};
            intra::buildIntraPredictors(pred, 64, mode, angleDelta, 64, 64, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[4096] = {0};
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    residual[y * 64 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 64 + x]);

            std::int32_t cb[4096] = {0};
            transforms::fwdTxfm2d64x64(residual, cb, 64, txType);
            for (int i = 0; i < 4096; ++i) coeffs[(by * gridW + bx) * 4096 + i] = cb[i];

            std::uint8_t tmp[4096] = {0};
            for (int i = 0; i < 4096; ++i) tmp[i] = pred[i];
            transforms::invTxfm2dAdd64x64(cb, tmp, 64, txType);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) recon.at(px + x, py + y) = tmp[y * 64 + x];
        }
    }
}

void encodeFrameAuto64x64(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                          std::uint8_t* modes, transforms::TxType txType) {
    const int gridW = src.width() / 64;
    const int gridH = src.height() / 64;
    // RE5: one RD-lite decision context per frame, initialized from the
    // frame's own l7 context (the emission cdfs).
    RdliteFrameCtx rctx;
    initRdliteFrameCtx(&rctx, 0);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 64;
            const int py = by * 64;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 64 : 0;
            const int nLeftPx = hasLeft ? 64 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 64 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[128] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[128] = {0};
            if (hasTop) {
                for (int i = 0; i < 64 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 64; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[4096] = {0};
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) srcBlk[y * 64 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode64x64(srcBlk, above, nTopPx, nTopRightPx, left,
                                                        nLeftPx, nBottomLeftPx, aboveLeft, nctx, rctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[4096] = {0};
            intra::buildIntraPredictors(pred, 64, d.mode, d.angleDelta, 64, 64, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);

            std::int16_t residual[4096] = {0};
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    residual[y * 64 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 64 + x]);

            std::int32_t cb[4096] = {0};
            transforms::fwdTxfm2d64x64(residual, cb, 64, txType);
            for (int i = 0; i < 4096; ++i) coeffs[(by * gridW + bx) * 4096 + i] = cb[i];

            std::uint8_t reconBlk[4096] = {0};
            for (int i = 0; i < 4096; ++i) reconBlk[i] = pred[i];
            transforms::invTxfm2dAdd64x64(cb, reconBlk, 64, txType);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) recon.at(px + x, py + y) = reconBlk[y * 64 + x];
        }
    }
}

void encodeFrameRecon64x64Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                            intra::PredictionMode mode, int angleDelta, std::int32_t qindex,
                            transforms::TxType txType) {
    const int gridW = src.width() / 64;
    const int gridH = src.height() / 64;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[4096];
    transforms::defaultScan64x64(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 64;
            const int py = by * 64;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 64 : 0;
            const int nLeftPx = hasLeft ? 64 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 64 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[128] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[128] = {0};
            if (hasTop) {
                for (int i = 0; i < 64 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 64; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[4096] = {0};
            intra::buildIntraPredictors(pred, 64, mode, angleDelta, 64, 64, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[4096] = {0};
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    residual[y * 64 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 64 + x]);

            std::int32_t cb[4096] = {0};
            transforms::fwdTxfm2d64x64(residual, cb, 64, txType);
            std::int32_t qc[4096] = {0};
            std::int32_t dq[4096] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp64x64(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 4096; ++i) coeffs[(by * gridW + bx) * 4096 + i] = qc[i];

            std::uint8_t blk[4096] = {0};
            for (int i = 0; i < 4096; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd64x64(dq, blk, 64, txType);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) recon.at(px + x, py + y) = blk[y * 64 + x];
        }
    }
}

// L9: D2 policy loop at 64x64 with the FP quantizer wired at a fixed qindex
// (log_scale 2). SCAN POLICY IS OURS: fixed defaultScan64x64 for every block
// (see encodeFrameAuto4x4Q); SVT selects per mode/tx type via get_scan_order.
void encodeFrameAuto64x64Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType,
                           entropy::AomWriter* w, entropy::EcFrameContext* fc,
                           entropy::DcSignLevelCoeffNa* na) {
    const int gridW = src.width() / 64;
    const int gridH = src.height() / 64;
    // RE5: one RD-lite decision context per frame, initialized from the
    // frame's own l7 context (the emission cdfs).
    RdliteFrameCtx rctx;
    initRdliteFrameCtx(&rctx, qindex);
    // FS4: the bucket follows the frame's base_q_idx (spec init_coeff_cdfs).
    if (w && fc) {
        entropy::initDefaultEcFrameContext(fc, qindex);
        entropy::odEcEncReset(&w->ec);
        w->allow_update_cdf = 1;  // THE COUPLING (BSF4-fix): see encodeFrameAuto16x16Q.
        w->pos = 0;
    }
    // FS5: the running partition contexts (coding_loop.c:1700-1713) across
    // the grid - above indexed by mi column, left by (mi_row & 15).
    std::uint8_t pAboveRun[64];
    std::uint8_t pLeftRun[16];
    memset(pAboveRun, INVALID_NEIGHBOR_DATA, sizeof(pAboveRun));
    memset(pLeftRun, INVALID_NEIGHBOR_DATA, sizeof(pLeftRun));
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[4096];
    transforms::defaultScan64x64(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 64;
            const int py = by * 64;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 64 : 0;
            const int nLeftPx = hasLeft ? 64 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 64 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[128] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[128] = {0};
            if (hasTop) {
                for (int i = 0; i < 64 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 64; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[4096] = {0};
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) srcBlk[y * 64 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode64x64(srcBlk, above, nTopPx, nTopRightPx, left,
                                                        nLeftPx, nBottomLeftPx, aboveLeft, nctx, rctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[4096] = {0};
            intra::buildIntraPredictors(pred, 64, d.mode, d.angleDelta, 64, 64, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);
            std::int16_t residual[4096] = {0};
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    residual[y * 64 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 64 + x]);

            std::int32_t cb[4096] = {0};
            transforms::fwdTxfm2d64x64(residual, cb, 64, txType);
            std::int32_t qc[4096] = {0};
            std::int32_t dq[4096] = {0};
            std::uint16_t eob = 0;

            // FS4: the emission case runs the TX_64X64 SCAN CONTRACT (the FS2
            // settle): the fwd64 output's top-left 32x32 is COMPACTED into a
            // 32-wide buffer (the vendored fwd wrapper,
            // transforms.c:2700-2707), quantized at n_coeffs=1024 with the
            // normative 1024-position token scan, and the inverse consumes
            // the COMPACTED dqcoeff expanded back into the 64-wide zeroed
            // array (inv_transforms.c:2615-2628). The legacy (non-emission)
            // callers keep the 4096-wide facade path (the GPU twin pins it).
            std::int32_t qcTok[1024] = {0};
            std::int32_t dqTok[1024] = {0};
            std::uint16_t eobTok = 0;
            if (w && fc && na) {
                std::int32_t cbTok[1024] = {0};
                for (int r = 0; r < 32; ++r)
                    for (int c = 0; c < 32; ++c) cbTok[r * 32 + c] = cb[r * 64 + c];
                std::int16_t scanTok[1024];
                transforms::defaultScan32x32(scanTok);
                transforms::quantizeFp64x64Token(cbTok, qt, scanTok, qcTok, dqTok, &eobTok);
            } else {
                transforms::quantizeFp64x64(cb, qt, scan, qc, dq, &eob);
            }
            if (w && fc && na) {
                for (int i = 0; i < 1024; ++i) coeffs[(by * gridW + bx) * 4096 + i] = qcTok[i];
                for (int i = 1024; i < 4096; ++i) coeffs[(by * gridW + bx) * 4096 + i] = 0;
            } else {
                for (int i = 0; i < 4096; ++i) coeffs[(by * gridW + bx) * 4096 + i] = qc[i];
            }

            std::uint8_t blk[4096] = {0};
            for (int i = 0; i < 4096; ++i) blk[i] = pred[i];
            if (w && fc && na) {
                std::int32_t dq64[4096] = {0};
                for (int r = 0; r < 32; ++r)
                    for (int c = 0; c < 32; ++c) dq64[r * 64 + c] = dqTok[r * 32 + c];
                transforms::invTxfm2dAdd64x64(dq64, blk, 64, txType);
            } else {
                transforms::invTxfm2dAdd64x64(dq, blk, 64, txType);
            }
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) recon.at(px + x, py + y) = blk[y * 64 + x];

            // FS4: the per-block symbol walk. A 64x64 frame reads its
            // partition symbol from the 10-symbol BLOCK_64X64 row. DCTONLY at
            // 64x64 intra -> NO tx-type symbol (entropy_coding.c:321-322 via
            // the FS3-prep gate); FI is dead at 64x64 (bsize > 32x32,
            // mode_decision.c:108-120).
            if (w && fc && na) {
                entropy::writePartition(w, fc, entropy::BLOCK_64X64, 1, 1, pAboveRun[bx],
                                        pLeftRun[by & 15], entropy::PARTITION_NONE);
                entropy::updatePartitionContext(pAboveRun, pLeftRun, by, bx, entropy::BLOCK_64X64);
                entropy::writeSkip(w, fc, 0, 0);
                int topCtx = 0, leftCtx = 0;
                entropy::getKfYModeCtx(hasLeft ? 1 : 0, static_cast<int>(nctx.leftMode),
                                       hasTop ? 1 : 0, static_cast<int>(nctx.aboveMode),
                                       &topCtx, &leftCtx);
                entropy::writeKfLumaMode(w, fc, entropy::BLOCK_64X64,
                                         static_cast<entropy::PredictionMode>(d.mode), topCtx,
                                         leftCtx, d.angleDelta);
                std::int16_t scanTok[1024];
                transforms::defaultScan32x32(scanTok);
                entropy::writeBlockCoeffs(w, fc, na, qcTok, scanTok, entropy::TX_64X64,
                                          entropy::BLOCK_64X64, eobTok, by * 16, bx * 16, 1,
                                          static_cast<entropy::PredictionMode>(d.mode),
                                          entropy::COMPONENT_LUMA);
            }
        }
    }
    if (w) entropy::odEcStopEncode(w);
}
// The UV plane is its own plane; per-plane availability (the chroma
// above/left mbmi of enc_intra_prediction.c:28-33 maps to the same
// hasTop/hasLeft raster logic on the UV grid). Dispatch: uv_mode folds via
// intra::uv2y (get_uv_mode, common_utils.h:130-133); the builder never sees
// FI (enc_intra_prediction.c:641). The D2 policy scores the 13 folded UV
// candidates (uv modes 0..12; UV_CFL_PRED is NOT a candidate - policy
// named; its prediction-surface fold is DC and DC_PRED is a candidate).
ModeDecision decideBlockModeUv16x16(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                    int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                    int nBottomLeftPx, std::uint8_t aboveLeft,
                                    const intra::NeighborContext& neighbors) {
    ModeDecision best{intra::DC_PRED, 0};
    bool haveBest = false;
    for (int m = 0; m < intra::UV_CFL_PRED; ++m) {
        std::uint8_t pred[256] = {0};
        intra::buildIntraPredictorsUv(pred, 16, static_cast<intra::UvPredictionMode>(m), 0, 16, 16,
                                      aboveLeft, aboveRef, nTopPx, nTopRightPx, leftRef, nLeftPx,
                                      nBottomLeftPx, neighbors);
        const std::uint32_t sad = motion::sad16x16(src, 16, pred, 16);
        if (!haveBest || sad < best.sad) {
            best = ModeDecision{static_cast<intra::PredictionMode>(m), sad};
            haveBest = true;
        }
    }
    return best;
}

// CS4: the size-generic UV D2 decision (the same CH3 policy; SAD at the UV
// size).
ModeDecision decideBlockModeUv(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                               int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                               int nBottomLeftPx, std::uint8_t aboveLeft, int uvB,
                               const intra::NeighborContext& neighbors) {
    ModeDecision best{intra::DC_PRED, 0};
    bool haveBest = false;
    for (int m = 0; m < intra::UV_CFL_PRED; ++m) {
        std::uint8_t pred[1024] = {0};
        intra::buildIntraPredictorsUv(pred, uvB, static_cast<intra::UvPredictionMode>(m), 0, uvB,
                                      uvB, aboveLeft, aboveRef, nTopPx, nTopRightPx, leftRef,
                                      nLeftPx, nBottomLeftPx, neighbors);
        std::uint32_t sad = 0;
        if (uvB == 4) sad = motion::sad4x4(src, uvB, pred, uvB);
        else if (uvB == 8) sad = motion::sad8x8(src, uvB, pred, uvB);
        else if (uvB == 16) sad = motion::sad16x16(src, uvB, pred, uvB);
        else sad = motion::sad32x32(src, uvB, pred, uvB);
        if (!haveBest || sad < best.sad) {
            best = ModeDecision{static_cast<intra::PredictionMode>(m), sad};
            haveBest = true;
        }
    }
    return best;
}

// The decode_partition leaf order (aom decodeframe.c:1392-1397): the DFS
// visits the four quadrants TL, TR, BL, BR at every level, so the leaf
// sequence of a single-64x64-SB walk = the Z-order curve of the leaf grid.
// The SPLIT nodes themselves emit their partition symbol at the FIRST leaf
// visited inside them (the TL leaf), which is where the origin guards
// (miRow % 8 == 0 / miRow % 4 == 0) fire in this order.
static void zorderLeaves(int size, int x0, int y0, int grid, int* out, int* n) {
    if (size == 1) {
        out[(*n)++] = y0 * grid + x0;
        return;
    }
    const int h = size / 2;
    zorderLeaves(h, x0, y0, grid, out, n);
    zorderLeaves(h, x0 + h, y0, grid, out, n);
    zorderLeaves(h, x0, y0 + h, grid, out, n);
    zorderLeaves(h, x0 + h, y0 + h, grid, out, n);
}

// CS4: the chroma-emitting Q loop (the signature comment in pipeline.h).
void encodeFrameChromaQ(const pixels::Plane& srcY, const pixels::Plane& srcU,
                        const pixels::Plane& srcV, pixels::Plane& reconY, pixels::Plane& reconU,
                        pixels::Plane& reconV, std::int32_t* coeffsY, std::int32_t* coeffsU,
                        std::int32_t* coeffsV, std::uint8_t* modesY, std::uint8_t* modesU,
                        std::uint8_t* modesV, std::uint16_t* eobsY, std::uint16_t* eobsU,
                        std::uint16_t* eobsV, std::int32_t qindex, int lumaB,
                        transforms::TxType txType, entropy::AomWriter* w,
                        entropy::EcFrameContext* fc, entropy::DcSignLevelCoeffNa* naY,
                        entropy::DcSignLevelCoeffNa* naU, entropy::DcSignLevelCoeffNa* naV) {
    const int uvB = lumaB / 2;
    const int grid = srcY.width() / lumaB;
    // RE5: one RD-lite decision context per frame, initialized from the
    // frame's own l7 context (the emission cdfs).
    RdliteFrameCtx rctx;
    initRdliteFrameCtx(&rctx, qindex);
    const entropy::TxSize ltxs = (lumaB == 8) ? entropy::TX_8X8
                               : (lumaB == 16) ? entropy::TX_16X16
                               : (lumaB == 32) ? entropy::TX_32X32 : entropy::TX_64X64;
    const entropy::TxSize utxs = (uvB == 4) ? entropy::TX_4X4
                               : (uvB == 8) ? entropy::TX_8X8
                               : (uvB == 16) ? entropy::TX_16X16 : entropy::TX_32X32;
    const entropy::BlockSize lbs = (lumaB == 8) ? entropy::BLOCK_8X8
                                 : (lumaB == 16) ? entropy::BLOCK_16X16
                                 : (lumaB == 32) ? entropy::BLOCK_32X32 : entropy::BLOCK_64X64;
    const entropy::BlockSize leafBs = lbs;  // the leaf coding block's bsize
    const entropy::BlockSize ubs = (uvB == 4) ? entropy::BLOCK_4X4
                                 : (uvB == 8) ? entropy::BLOCK_8X8
                                 : (uvB == 16) ? entropy::BLOCK_16X16 : entropy::BLOCK_32X32;
    const int ltxW = lumaB / 4, utxW = uvB / 4;
    const int cfl = (lumaB <= 32) ? 1 : 0;
    if (w && fc) {
        entropy::initDefaultEcFrameContext(fc, qindex);
        entropy::odEcEncReset(&w->ec);
        w->allow_update_cdf = 1;  // THE COUPLING (BSF4-fix)
        w->pos = 0;
    }
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t lscan[1024], uscan[1024];
    if (lumaB == 8) transforms::defaultScan8x8(lscan);
    else if (lumaB == 16) transforms::defaultScan16x16(lscan);
    else if (lumaB == 32) transforms::defaultScan32x32(lscan);
    else transforms::defaultScan32x32(lscan);  // the TX_64X64 token scan (1024 positions)
    if (uvB == 4) transforms::defaultScan4x4(uscan);
    else if (uvB == 8) transforms::defaultScan8x8(uscan);
    else if (uvB == 16) transforms::defaultScan16x16(uscan);
    else transforms::defaultScan32x32(uscan);
    // FS5a: the running partition contexts (the FS-series machinery; the
    // chroma loop REUSES the luma loops' writePartition/writeSkip/
    // updatePartitionContext, the decode-verified ECP1/ECP2 surfaces)
    std::uint8_t pAboveRun[64];
    std::uint8_t pLeftRun[16];
    memset(pAboveRun, INVALID_NEIGHBOR_DATA, sizeof(pAboveRun));
    memset(pLeftRun, INVALID_NEIGHBOR_DATA, sizeof(pLeftRun));
    // the SB root partition symbol (the 64x64 tree root; SPLIT when the
    // grid descends below 64, NONE when the root IS the block)
    if (w && fc) {
        entropy::writePartition(w, fc, entropy::BLOCK_64X64, 1, 1, pAboveRun[0], pLeftRun[0],
                                lumaB < 64 ? entropy::PARTITION_SPLIT : entropy::PARTITION_NONE);
    }
    // the tree-order leaf iteration (the RT6 fix): the leaves are visited in
    // the decode_partition DFS order (the same iteration the l7 read twin and
    // the generator's svtd_cs4_uv_q_drive use) - the interior partition
    // symbols then land at the node entries (the first leaf of each node =
    // the TL leaf in this order), matching the decoder's recursive walk
    static int zord[64];
    int zn = 0;
    zorderLeaves(grid, 0, 0, grid, zord, &zn);
    // RT10b: the inverse map, leaf index -> Z-order visit position, for the
    // bottom-left availability gate (decode-order dependent on that axis).
    int zPos[64];
    for (int i = 0; i < zn; ++i) zPos[zord[i]] = i;
    for (int t = 0; t < grid * grid; ++t) {
        const int bidx = zord[t];
        const int by = bidx / grid, bx = bidx % grid;
            const int px = bx * lumaB, py = by * lumaB;
            const int miRow = py / 4, miCol = px / 4;
            const int bwMi = lumaB / 4;
            // is_chroma_reference (common_utils.h:315-320), 4:2:0
            const bool chromaRef =
                ((miRow & 0x01) || !(bwMi & 0x01)) && ((miCol & 0x01) || !(bwMi & 0x01));
            const int uvX = ((px >> 3) << 3) >> 1;  // ROUND_UV >> 1 (definitions.h:327)
            const int uvY = ((py >> 3) << 3) >> 1;
            const int uvmiCol = uvX / 4, uvmiRow = uvY / 4;
            // ---- luma: edges (M1 + the REAL recon top-right gather) ----
            const bool hasTop = by > 0, hasLeft = bx > 0;
            const int nTopPx = hasTop ? lumaB : 0;
            const int nLeftPx = hasLeft ? lumaB : 0;
            const int nTopRightPx = (hasTop && bx + 1 < grid) ? lumaB : 0;
            // RT10b: the BOTTOM-LEFT extension, previously a literal 0 at both
            // the decide* and buildIntraPredictors call sites. The caller
            // supplies AVAILABILITY; whether the extension is USED is derived
            // inside the builder, which mutates ONE variable in sequence
            // (enc_intra_prediction.c build_intra_predictors: seed from
            // extend_modes & NEED_BOTTOMLEFT, force 0 for filter-intra, then
            // OVERWRITE with is_dr_mode && p_angle > 180) - so the predicate is
            // deliberately NOT duplicated here. Measured in 7fb280d: with 0 the
            // gather never runs and the extension is memset repetition, where
            // dav1d held a real 11-value ramp at all 16 positions.
            //
            // Unlike the top-right (structural, per de5321a) this axis IS
            // decode-order dependent: the below-left leaf is (by+1, bx-1),
            // available exactly when its Z-order position precedes this leaf's.
            const int nBottomLeftPx = bottomLeftPx(bidx, by, bx, grid, zPos, lumaB);
            std::uint8_t above[129] = {0};
            std::uint8_t left[129] = {0};
            if (hasTop)
                for (int i = 0; i < lumaB + nTopRightPx; ++i) above[i] = reconY.at(px + i, py - 1);
            if (hasLeft)
                for (int i = 0; i < lumaB; ++i) left[i] = reconY.at(px - 1, py + i);
            // the extension continues the left column downward
            if (nBottomLeftPx)
                for (int j = 0; j < nBottomLeftPx; ++j)
                    left[lumaB + j] = reconY.at(px - 1, py + lumaB + j);
            const std::uint8_t aboveLeft = (hasTop && hasLeft) ? reconY.at(px - 1, py - 1) : 0;
            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modesY[(by - 1) * grid + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modesY[by * grid + bx - 1])
                                : intra::DC_PRED;
            std::uint8_t srcBlk[4096] = {0};
            for (int y = 0; y < lumaB; ++y)
                for (int x = 0; x < lumaB; ++x) srcBlk[y * lumaB + x] = srcY.at(px + x, py + y);
            ModeDecision d{intra::DC_PRED, 0};
            if (lumaB == 8)
                d = decideBlockMode8x8(srcBlk, above, nTopPx, nTopRightPx, left, nLeftPx, nBottomLeftPx,
                                       aboveLeft, nctx, rctx);
            else if (lumaB == 16)
                d = decideBlockMode16x16(srcBlk, above, nTopPx, nTopRightPx, left, nLeftPx, nBottomLeftPx,
                                         aboveLeft, nctx, rctx);
            else if (lumaB == 32)
                d = decideBlockMode32x32(srcBlk, above, nTopPx, nTopRightPx, left, nLeftPx, nBottomLeftPx,
                                         aboveLeft, nctx, rctx);
            else
                d = decideBlockMode64x64(srcBlk, above, nTopPx, nTopRightPx, left, nLeftPx, nBottomLeftPx,
                                         aboveLeft, nctx, rctx);
            modesY[bidx] = static_cast<std::uint8_t>(d.mode);
            std::uint8_t pred[4096] = {0};
            intra::buildIntraPredictors(pred, lumaB, d.mode, d.angleDelta, lumaB, lumaB, aboveLeft, above,
                                        nTopPx, nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);
            std::int16_t residual[4096];
            for (int i = 0; i < lumaB * lumaB; ++i) {
                const int x = i % lumaB, y = i / lumaB;
                residual[i] = static_cast<std::int16_t>(srcY.at(px + x, py + y) - pred[i]);
            }
            std::int32_t cb[4096] = {0};
            std::int32_t qc[4096] = {0}, dq[4096] = {0};
            std::uint16_t eob = 0;
            if (lumaB == 8) {
                transforms::fwdTxfm2d8x8(residual, cb, lumaB, txType);
                transforms::quantizeFp8x8(cb, qt, lscan, qc, dq, &eob);
            } else if (lumaB == 16) {
                transforms::fwdTxfm2d16x16(residual, cb, lumaB, txType);
                transforms::quantizeFp16x16(cb, qt, lscan, qc, dq, &eob);
            } else if (lumaB == 32) {
                transforms::fwdTxfm2d32x32(residual, cb, lumaB, txType);
                transforms::quantizeFp32x32(cb, qt, lscan, qc, dq, &eob);
            } else {
                transforms::fwdTxfm2d64x64(residual, cb, lumaB, txType);
            }
            if (lumaB == 64) {
                if (w && fc) {
                    std::int32_t cbTok[1024] = {0};
                    for (int r = 0; r < 32; ++r)
                        for (int c = 0; c < 32; ++c) cbTok[r * 32 + c] = cb[r * 64 + c];
                    transforms::quantizeFp64x64Token(cbTok, qt, lscan, qc, dq, &eob);
                } else {
                    transforms::quantizeFp64x64(cb, qt, lscan, qc, dq, &eob);
                }
            }
            for (int i = 0; i < lumaB * lumaB; ++i) coeffsY[bidx * lumaB * lumaB + i] = qc[i];
            eobsY[bidx] = eob;
            // symbols: the partition surfaces (the SB tree + the leaf NONE)
            // + the skip flag, THEN the luma kf mode (+ the angle delta when
            // gated), then uv_mode + the uv angle-delta (chroma-referenced
            // only) - the aom decode order (decodemv.c:787 the skip read is
            // unconditional for intra blocks)
            if (w && fc) {
                // the interior partition symbols (the SB tree levels between
                // the root and the leaf; only for the deeper geometries)
                if (lumaB < 32 && (miRow % 8) == 0 && (miCol % 8) == 0) {
                    entropy::writePartition(w, fc, entropy::BLOCK_32X32, 1, 1, pAboveRun[miCol],
                                            pLeftRun[miRow & 15], entropy::PARTITION_SPLIT);
                }
                if (lumaB < 16 && (miRow % 4) == 0 && (miCol % 4) == 0) {
                    entropy::writePartition(w, fc, entropy::BLOCK_16X16, 1, 1, pAboveRun[miCol],
                                            pLeftRun[miRow & 15], entropy::PARTITION_SPLIT);
                }
                // the leaf partition symbol (NONE at the coding block's bsize;
                // skipped for lumaB == 64 where the SB root IS the leaf)
                if (lumaB < 64) {
                    entropy::writePartition(w, fc, leafBs, 1, 1, pAboveRun[miCol],
                                            pLeftRun[miRow & 15], entropy::PARTITION_NONE);
                }
                entropy::updatePartitionContext(pAboveRun, pLeftRun, miRow, miCol, leafBs);
                // the skip symbol (ECP2; skip=0, the ctx from the above/left
                // skip flags - all coded skip=0 -> ctx 0 for every block)
                entropy::writeSkip(w, fc, 0, 0);
            }
            if (w && fc) {
                int topCtx = 0, leftCtx = 0;
                entropy::getKfYModeCtx(hasLeft ? 1 : 0, static_cast<int>(nctx.leftMode),
                                       hasTop ? 1 : 0, static_cast<int>(nctx.aboveMode), &topCtx,
                                       &leftCtx);
                entropy::writeKfLumaMode(w, fc, lbs, static_cast<entropy::PredictionMode>(d.mode),
                                         topCtx, leftCtx, d.angleDelta);
            }
            std::uint8_t uvMode = 0xFF;
            if (chromaRef) {
                // the UV decision on the U plane (the CH3 D2 policy); the V
                // TU shares the decided mode (ONE uv_mode symbol per block)
                std::uint8_t uvAbove[65] = {0};
                std::uint8_t uvLeft[65] = {0};
                const bool uvHasTop = uvY > 0, uvHasLeft = uvX > 0;
                const int uvGrid = 32 / uvB;
                const int uRow = uvY / uvB, uCol = uvX / uvB;
                const int nTopUv = uvHasTop ? uvB : 0;
                const int nLeftUv = uvHasLeft ? uvB : 0;
                const int nTrUv = (uvHasTop && uCol + 1 < uvGrid) ? uvB : 0;
                if (uvHasTop)
                    for (int i = 0; i < uvB + nTrUv; ++i) uvAbove[i] = reconU.at(uvX + i, uvY - 1);
                if (uvHasLeft)
                    for (int i = 0; i < uvB; ++i) uvLeft[i] = reconU.at(uvX - 1, uvY + i);
                const std::uint8_t uvAl = (uvHasTop && uvHasLeft) ? reconU.at(uvX - 1, uvY - 1) : 0;
                intra::NeighborContext uvNctx;
                uvNctx.aboveMode =
                    uvHasTop
                        ? static_cast<intra::PredictionMode>(modesU[(uRow - 1) * uvGrid + uCol])
                        : intra::DC_PRED;
                uvNctx.leftMode =
                    uvHasLeft ? static_cast<intra::PredictionMode>(modesU[uRow * uvGrid + uCol - 1])
                              : intra::DC_PRED;
                std::uint8_t uvSrc[1024] = {0};
                for (int y = 0; y < uvB; ++y)
                    for (int x = 0; x < uvB; ++x) uvSrc[y * uvB + x] = srcU.at(uvX + x, uvY + y);
                const ModeDecision dUv =
                    decideBlockModeUv(uvSrc, uvAbove, nTopUv, nTrUv, uvLeft, nLeftUv, 0, uvAl, uvB,
                                      uvNctx);
                uvMode = static_cast<std::uint8_t>(dUv.mode);
                modesU[uRow * uvGrid + uCol] = uvMode;
                modesV[uRow * uvGrid + uCol] = uvMode;  // ONE uv_mode per block
                if (w && fc) {
                    entropy::writeUvMode(w, fc, cfl, static_cast<entropy::PredictionMode>(d.mode),
                                         static_cast<entropy::UvPredictionMode>(dUv.mode));
                    entropy::writeUvAngleDelta(w, fc, lbs,
                                               static_cast<entropy::UvPredictionMode>(dUv.mode), 0);
                }
            } else {
                eobsU[bidx] = 0xFFFF;
                eobsV[bidx] = 0xFFFF;
                modesU[bidx] = 0xFF;
                modesV[bidx] = 0xFF;
            }
            // the filter-intra flag surface (the RT7-b fix): the decoder
            // reads it after the uv surface for every DC-decided luma block
            // at bsize <= 32x32 (aom decodemv.c:843, dav1d decode.c:1144) -
            // the CS4 walk omitted it and c8/c4 desynced at the first
            // DC-decided leaf (c8 b12 = mi(12,0), c4 b5 = mi(0,10), the
            // measured divergence sites). The FS-series semantic: the D2
            // never picks FI, the flag is 0 (the FILTER_INTRA_MODES
            // sentinel).
            if (w && fc &&
                entropy::filterIntraAllowed(1, lbs, 0,
                                            static_cast<std::uint32_t>(d.mode))) {
                // The ADAPTING frame-context cdf (SVT entropy_coding.c:5052;
                // see the 4x4 site note). Default rows (cabac_context_model.c:618-623):
                // BLOCK_4X4(28147) BLOCK_8X8(24902) BLOCK_16X16(20360)
                // BLOCK_32X32(10425).
                entropy::odEcWriteSymbol(w, 0, fc->filter_intra_cdfs[lbs], 2);
            }
            // ---- the LUMA chain (U BEFORE V: the luma chain first, the
            // ratified CS4 walk order) ----
            if (w && fc) {
                entropy::writeBlockCoeffs(w, fc, naY, qc, lscan, ltxs, lbs, eob, miRow, miCol, 1,
                                          static_cast<entropy::PredictionMode>(d.mode),
                                          entropy::COMPONENT_LUMA);
            }
            // ---- the U chain then the V chain (U BEFORE V) ----
            if (chromaRef) {
                const int uRow = uvY / uvB, uCol = uvX / uvB;
                const int uvGrid = 32 / uvB;
                const bool uvHasTop = uvY > 0, uvHasLeft = uvX > 0;
                const int nTopUv = uvHasTop ? uvB : 0;
                const int nLeftUv = uvHasLeft ? uvB : 0;
                // RT21: the ABOVE-RIGHT count, previously structural-only
                // (uvHasTop && uCol + 1 < uvGrid). The owner leaf (by-1, bx+1)
                // may walk LATER in Z-order than this leaf; gathering its
                // samples then read the zero-initialised recon, and the D45
                // leaf 41 (c4) predicted 0 where the reference reconstructs
                // 255. topRightPx is the SAME named rule the luma bottom-left
                // call site uses at :1884 - not a second transcription - with
                // uvB as the block size; the uRow == by / uCol == bx /
                // uvGrid == grid identity is the CS7 one commented below, so
                // the (by-1, bx+1) owner index IS the UV-grid owner index.
                const int nTrUv = topRightPx(bidx, by, bx, grid, zPos, uvB);
                // CS7: the CHROMA BOTTOM-LEFT extension, previously a literal 0
                // in the nBottomLeftPx position of the builder call below. The
                // SAME named rule the luma call site uses at :1884 - not a
                // second transcription of the expression - with uvB as the block
                // size. UV leaf indices ARE the luma leaf indices: uvB =
                // lumaB/2 and the UV origin is (px & ~7) >> 1, so uRow == by
                // and uCol == bx and uvGrid == grid, which is why no second
                // Z-order map is needed and none is built.
                //
                // The builder still owns the USE predicate: buildIntraPredictors
                // mutates one variable in sequence (intra.cpp:541-547 - seed
                // from kExtendModes & kNeedBottomLeft, force 0 for filter-intra,
                // then OVERWRITE with isDrMode && pAngle > 180). A second copy
                // of that in this walk would be reference logic free to drift.
                // This call site supplies AVAILABILITY and the gathered bytes;
                // nothing here mentions D203.
                //
                // NO y + th >= h TERM, and its absence is not an omission. dav1d
                // adds that to have_bottomleft (ipred_prepare_tmpl.c:145-149),
                // but for a whole-leaf block in a leaf-aligned frame "touches
                // the plane bottom" is exactly "is on the last row", which is
                // exactly bottomLeftPx's own by + 1 >= grid early return. The
                // one assumption that would make the term live is a frame whose
                // dimensions are not leaf-aligned, or a partial block; this walk
                // produces neither (every geometry is width / lumaB blocks
                // exactly), so the equivalence is structural rather than a
                // fixture coincidence. A hypothetical overrun would be a padded
                // READ - pixels::Plane allocates height + 2*padding rows - never
                // a memory-safety issue, and no artifact was ever at risk.
                const int nBottomLeftUv = bottomLeftPx(bidx, by, bx, grid, zPos, uvB);
                for (int comp = 0; comp < 2; ++comp) {
                    const pixels::Plane& csrc = comp ? srcV : srcU;
                    pixels::Plane& crecon = comp ? reconV : reconU;
                    std::int32_t* ccoeffs = comp ? coeffsV : coeffsU;
                    std::uint16_t* ceobs = comp ? eobsV : eobsU;
                    entropy::DcSignLevelCoeffNa* cna = comp ? naV : naU;
                    std::uint8_t cAbove[65] = {0};
                    std::uint8_t cLeft[65] = {0};
                    if (uvHasTop)
                        for (int i = 0; i < uvB + nTrUv; ++i)
                            cAbove[i] = crecon.at(uvX + i, uvY - 1);
                    if (uvHasLeft)
                        for (int i = 0; i < uvB; ++i) cLeft[i] = crecon.at(uvX - 1, uvY + i);
                    // CS7: the extension continues the left column downward
                    // (svt_gen.c:9230-9234), per plane - U and V are separate
                    // recon buffers, so the gather is inside the comp loop and
                    // is not hoisted with the other edge counts. cLeft[65] holds
                    // 2*uvB <= 64, which is what the builder indexes: with
                    // nLeftPx = uvB it reads leftRef[uvB .. uvB+nBottomLeftPx).
                    if (nBottomLeftUv)
                        for (int j = 0; j < nBottomLeftUv; ++j)
                            cLeft[uvB + j] = crecon.at(uvX - 1, uvY + uvB + j);
                    const std::uint8_t cAl = (uvHasTop && uvHasLeft) ? crecon.at(uvX - 1, uvY - 1) : 0;
                    intra::NeighborContext cNctx;
                    // the shared uv_mode history (the U-plane neighbor
                    // modes: ONE uv_mode per block, the V TU shares it)
                    cNctx.aboveMode =
                        uvHasTop
                            ? static_cast<intra::PredictionMode>(modesU[(uRow - 1) * uvGrid + uCol])
                            : intra::DC_PRED;
                    cNctx.leftMode =
                        uvHasLeft
                            ? static_cast<intra::PredictionMode>(modesU[uRow * uvGrid + uCol - 1])
                            : intra::DC_PRED;
                    std::uint8_t cPred[1024] = {0};
                    intra::buildIntraPredictorsUv(
                        cPred, uvB,
                        static_cast<intra::UvPredictionMode>(modesU[uRow * uvGrid + uCol]), 0, uvB,
                        uvB, cAl, cAbove, nTopUv, nTrUv, cLeft, nLeftUv, nBottomLeftUv, cNctx);
                    std::int16_t cRes[1024];
                    for (int i = 0; i < uvB * uvB; ++i) {
                        const int x = i % uvB, y = i / uvB;
                        cRes[i] = static_cast<std::int16_t>(csrc.at(uvX + x, uvY + y) - cPred[i]);
                    }
                    std::int32_t cCb[1024] = {0};
                    std::int32_t cQc[1024] = {0}, cDq[1024] = {0};
                    std::uint16_t cEob = 0;
                    // The intra-chroma tx type is DERIVED from the decided UV
                    // mode, never transmitted (decodetxb.c:156-159 reads the
                    // tx_type for AOM_PLANE_Y only). The same decided UV mode
                    // feeds the U and V chains - the shared-mode walk policy.
                    const transforms::TxType cTxType = transforms::intraUvTxType(
                        intra::uv2y(static_cast<intra::UvPredictionMode>(modesU[uRow * uvGrid + uCol])),
                        uvB == 4    ? transforms::TxSizeSqUp::TX_4X4
                        : uvB == 8  ? transforms::TxSizeSqUp::TX_8X8
                        : uvB == 16 ? transforms::TxSizeSqUp::TX_16X16
                                    : transforms::TxSizeSqUp::TX_32X32);
                    if (uvB == 4) {
                        transforms::fwdTxfm2d4x4(cRes, cCb, uvB, cTxType);
                        transforms::quantizeFp4x4(cCb, qt, uscan, cQc, cDq, &cEob);
                    } else if (uvB == 8) {
                        transforms::fwdTxfm2d8x8(cRes, cCb, uvB, cTxType);
                        transforms::quantizeFp8x8(cCb, qt, uscan, cQc, cDq, &cEob);
                    } else if (uvB == 16) {
                        transforms::fwdTxfm2d16x16(cRes, cCb, uvB, cTxType);
                        transforms::quantizeFp16x16(cCb, qt, uscan, cQc, cDq, &cEob);
                    } else {
                        transforms::fwdTxfm2d32x32(cRes, cCb, uvB, cTxType);
                        transforms::quantizeFp32x32(cCb, qt, uscan, cQc, cDq, &cEob);
                    }
                    for (int i = 0; i < uvB * uvB; ++i) ccoeffs[bidx * uvB * uvB + i] = cQc[i];
                    ceobs[bidx] = cEob;
                    if (w && fc) {
                        entropy::writeBlockCoeffs(w, fc, cna, cQc, uscan, utxs, ubs, cEob,
                                                  uvmiRow, uvmiCol, 1, entropy::DC_PRED,
                                                  entropy::COMPONENT_CHROMA);
                    }
                    if (uvB == 4) transforms::invTxfm2dAdd4x4(cDq, cPred, uvB, cTxType);
                    else if (uvB == 8) transforms::invTxfm2dAdd8x8(cDq, cPred, uvB, cTxType);
                    else if (uvB == 16) transforms::invTxfm2dAdd16x16(cDq, cPred, uvB, cTxType);
                    else transforms::invTxfm2dAdd32x32(cDq, cPred, uvB, cTxType);
                    for (int y = 0; y < uvB; ++y)
                        for (int x = 0; x < uvB; ++x)
                            crecon.at(uvX + x, uvY + y) = cPred[y * uvB + x];
                }
            }
            if (lumaB == 64 && w && fc) {
                // the emission-domain dq expansion before inv64 (the FS2/FS5d
                // contract)
                std::int32_t dq64[4096] = {0};
                for (int r = 0; r < 32; ++r)
                    for (int c = 0; c < 32; ++c) dq64[r * 64 + c] = dq[r * 32 + c];
                std::uint8_t blk[4096] = {0};
                for (int i = 0; i < 4096; ++i) blk[i] = pred[i];
                transforms::invTxfm2dAdd64x64(dq64, blk, 64, txType);
                for (int y = 0; y < 64; ++y)
                    for (int x = 0; x < 64; ++x) reconY.at(px + x, py + y) = blk[y * 64 + x];
            } else {
                std::uint8_t blk[4096] = {0};
                for (int i = 0; i < lumaB * lumaB; ++i) blk[i] = pred[i];
                if (lumaB == 8) transforms::invTxfm2dAdd8x8(dq, blk, lumaB, txType);
                else if (lumaB == 16) transforms::invTxfm2dAdd16x16(dq, blk, lumaB, txType);
                else if (lumaB == 32) transforms::invTxfm2dAdd32x32(dq, blk, lumaB, txType);
                else transforms::invTxfm2dAdd64x64(dq, blk, lumaB, txType);
                for (int y = 0; y < lumaB; ++y)
                    for (int x = 0; x < lumaB; ++x) reconY.at(px + x, py + y) = blk[y * lumaB + x];
            }
            (void)ltxW;
            (void)utxW;
    }
    if (w) entropy::odEcStopEncode(w);
}

void encodeFrameReconChroma16x16(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                                 intra::UvPredictionMode mode, int angleDelta, transforms::TxType txType) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictorsUv(pred, 16, mode, angleDelta, 16, 16, aboveLeft, above,
                                          nTopPx, nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = cb[i];

            std::uint8_t blk[256] = {0};
            for (int i = 0; i < 256; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd16x16(cb, blk, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = blk[y * 16 + x];
        }
    }
}

void encodeFrameAutoChroma16x16(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                                std::uint8_t* modes, transforms::TxType txType) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            // neighbor modes are UV modes (numerically the folded luma
            // values); the chroma smooth check (svt_aom_is_smooth on
            // uv_mode, intra_prediction.c:139-140) is numerically the luma
            // smooth set because UV_SMOOTH_* folds to SMOOTH_*.
            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) srcBlk[y * 16 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockModeUv16x16(srcBlk, above, nTopPx, nTopRightPx, left,
                                                          nLeftPx, nBottomLeftPx, aboveLeft, nctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictorsUv(pred, 16, static_cast<intra::UvPredictionMode>(d.mode), 0,
                                          16, 16, aboveLeft, above, nTopPx, nTopRightPx, left,
                                          nLeftPx, nBottomLeftPx, nctx);
            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = cb[i];

            std::uint8_t blk[256] = {0};
            for (int i = 0; i < 256; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd16x16(cb, blk, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = blk[y * 16 + x];
        }
    }
}

void encodeFrameReconChroma16x16Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                                  intra::UvPredictionMode mode, int angleDelta, std::int32_t qindex,
                                  transforms::TxType txType) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[256];
    transforms::defaultScan16x16(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictorsUv(pred, 16, mode, angleDelta, 16, 16, aboveLeft, above,
                                          nTopPx, nTopRightPx, left, nLeftPx, nBottomLeftPx);
            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            std::int32_t qc[256] = {0};
            std::int32_t dq[256] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp16x16(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = qc[i];

            std::uint8_t blk[256] = {0};
            for (int i = 0; i < 256; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd16x16(dq, blk, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = blk[y * 16 + x];
        }
    }
}

void encodeFrameAutoChroma16x16Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                                 std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[256];
    transforms::defaultScan16x16(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) srcBlk[y * 16 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockModeUv16x16(srcBlk, above, nTopPx, nTopRightPx, left,
                                                          nLeftPx, nBottomLeftPx, aboveLeft, nctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictorsUv(pred, 16, static_cast<intra::UvPredictionMode>(d.mode), 0,
                                          16, 16, aboveLeft, above, nTopPx, nTopRightPx, left,
                                          nLeftPx, nBottomLeftPx, nctx);
            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            std::int32_t qc[256] = {0};
            std::int32_t dq[256] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp16x16(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = qc[i];

            std::uint8_t blk[256] = {0};
            for (int i = 0; i < 256; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd16x16(dq, blk, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = blk[y * 16 + x];
        }
    }
}

int bottomLeftPx(int bIdx, int by, int bx, int grid, const int* zPos, int blockPx) {
    if (bx <= 0 || by + 1 >= grid) return 0;
    const int blIdx = (by + 1) * grid + (bx - 1);
    return zPos[blIdx] < zPos[bIdx] ? blockPx : 0;
}

// RT21: the above-right twin, the mirror of the rule above. The structural
// conjuncts live in the guards exactly as in bottomLeftPx; the Z-order
// comparison is the availability fact the structural-only gather at
// encodeFrameChromaQ's builder site was missing (the gather read the
// zero-initialised recon whenever the owner leaf walked later).
int topRightPx(int bIdx, int by, int bx, int grid, const int* zPos, int blockPx) {
    if (bx + 1 >= grid || by <= 0) return 0;
    const int trIdx = (by - 1) * grid + (bx + 1);
    return zPos[trIdx] < zPos[bIdx] ? blockPx : 0;
}

}  // namespace pipeline
