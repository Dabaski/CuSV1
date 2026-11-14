// l6_pipeline::mdc - the A4 md fast-loop + ME-search + recon surface
// (the product_coding_loop.c remainder + the md_process.c tail). The
// struct-free carrier convention (PO1/SRC1/RE8A/EMC1/A6C): every wall the
// ported spans consume travels as a carrier struct with the reference field
// names; the bodies are verbatim.
//
// PARKED (named, not faked):
//   (to be extended per slice)
#pragma once

#include <cstdint>
#include <cstddef>

#include <analysis.h>
#include <coding_loop_helpers.h>

#pragma warning(push)
#pragma warning(disable : 4005)
#include <md_slices.h>
#include <mode_config.h>
#pragma warning(pop)

namespace pipeline {
namespace mdc {

// ---------------------------------------------------------------------------
// Shared constants + enums (each cited)
// ---------------------------------------------------------------------------

// definitions.h:871 DistortionType.
enum DistortionType { DIST_SAD = 0, DIST_VAR = 1, DIST_SSD = 2 };
// definitions.h:818-827 InterpFilter. The k-prefixed members dodge the
// landed rate_est.h macros (SWITCHABLE_FILTERS = 3) that shadow the bare
// names in any TU seeing both headers.
enum InterpFilter {
    kEighttapRegular = 0,
    kEighttapSmooth = 1,
    kMultitapSharp = 2,
    kBilinear = 3,
    kInterpFiltersAll = 4,
    kSwitchableFilters = kBilinear,
    kSwitchable = kSwitchableFilters + 1,
};
// definitions.h:829-832 SUBPEL_STAGE.
enum SubpelStage { kSpelMe = 0, kSpelPme = 1 };
// definitions.h:834-839 SUBPEL_SEARCH_TYPE.
enum SubpelSearchType { kUse2TapsOrig = 0, kUse2Taps, kUse4Taps, kUse8Taps };
// definitions.h:841-845 SUBPEL_SEARCH_METHODS.
enum SubpelSearchMethods {
    kSubpelTree = 0,
    kSubpelTreePruned = 1,
    kSubpelFixedStageSearch = 2,
};
// definitions.h:847 SUBPEL_FORCE_STOP.
enum SubpelForceStop { kEighthPel = 0, kQuarterPel, kHalfPel, kFullPel };
// definitions.h:1890-1894 MotionMode (mode_decision.h reads the two).
enum MotionMode { kSimpleTranslation = 0, kObmcCausal = 1, kWarpedCausal = 2 };

// ---------------------------------------------------------------------------
// Carriers (the RE8A DeblockContext / A6C MdCtx precedent)
// ---------------------------------------------------------------------------

// The EbPictureBufferDesc window the spans read (y plane focus; the u/v
// planes arrive as their own PicBuf where a span needs them).
struct PicBuf {
    std::uint8_t* buf;        // the y_buffer (uint8; hbd paths reinterpret uint16)
    std::uint32_t stride;     // y_stride
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t max_width;  // :1875/:2197 the border-clamp bound
    std::uint32_t max_height;
    std::int32_t border;      // the reference border the clamps use
};

// The BlockGeom window (blk_geom) the spans consume.
struct MdGeom {
    entropy::BlockSize bsize;
    std::uint32_t bwidth;
    std::uint32_t bheight;
    std::uint32_t sq_size;
};

// The ModeDecisionCandidate window (block_mi + the cand fields the spans
// read/write). The mv halves travel as their packed as_int pairs (the RE3
// precedent).
struct MdCand {
    l16_rdo::PredictionMode mode;   // block_mi.mode
    std::uint8_t use_intrabc;       // block_mi.use_intrabc
    std::uint8_t cand_class;        // cand->cand_class
    std::int8_t angle_delta_y;      // block_mi.angle_delta[PLANE_TYPE_Y]
    std::uint8_t filter_intra_mode; // block_mi.filter_intra_mode (FILTER_INTRA_MODES sentinel = 5)
    std::uint8_t motion_mode;       // block_mi.motion_mode (MotionMode)
    std::uint8_t is_interintra_used;
    std::uint8_t num_proj_ref;      // block_mi.num_proj_ref
    std::int32_t ref_frame[2];      // block_mi.ref_frame[2]
    std::uint8_t tx_depth;          // block_mi.tx_depth
    std::uint32_t interp_filters;   // block_mi.interp_filters (InterpFilters)
    std::uint32_t mv_as_int[2];     // block_mi.mv[0/1].as_int
    std::uint32_t pred_mv_as_int[2];
};

// The ModeDecisionCandidateBuffer window. fast_cost/full_cost are the
// shared cost-array slots (the reference's uint64_t* members).
struct MdCandBf {
    MdCand* cand;
    PicBuf pred;
    PicBuf residual;   // cand_bf->residual (the hadamard path)
    std::uint64_t* fast_cost;
    std::uint64_t* full_cost;
    std::uint64_t fast_luma_rate;
    std::uint64_t fast_chroma_rate;
    std::uint64_t luma_fast_dist;
    std::uint64_t full_dist;
    std::uint8_t valid_luma_pred;
};

// The prediction / fast-cost callables: the landed-prediction walls
// threaded (the rate_est choose_best_av1_mv_pred precedent). The body
// keeps the WHICH-slot selection 1:1 (the two-entry tables).
struct FlCtx;
using PredictionFn = void (*)(FlCtx& ctx, MdCandBf& cand_bf);
using FastCostFn = std::uint64_t (*)(FlCtx& ctx, MdCandBf& cand_bf, std::uint64_t full_lambda,
                                     std::uint64_t luma_fast_dist);

// The ModeDecisionContext window the fast loops read/write (md_process.h
// :1127-:1193 + the ctrl structs).
struct FlCtx {
    MdGeom geom;                                   // ctx->blk_geom window
    std::int32_t blk_org_x;                        // ctx->blk_org_x
    std::int32_t blk_org_y;
    std::uint32_t sb_origin_x;                     // ctx->sb_origin_x
    std::uint32_t sb_origin_y;
    std::uint8_t hbd_md;                           // SVT_EFFECTIVE_HBD_MD input
    std::uint8_t shut_fast_rate;                   // ctx->shut_fast_rate
    std::uint8_t pd_pass;                          // PD_PASS_0 = 0 / PD_PASS_1 = 1
    std::uint8_t has_uv;                           // ctx->has_uv
    std::uint8_t bypass_encdec;                    // ctx->bypass_encdec
    std::uint8_t mds_subres_step;                  // ctx->mds_subres_step
    std::uint8_t uv_intra_comp_only;               // ctx->uv_intra_comp_only (written)
    std::uint8_t enable_psad;                      // ctx->enable_psad (written)
    std::uint32_t full_lambda_md[2];               // ctx->full_lambda_md[EB_8_BIT_MD / EB_10_BIT_MD]
    std::uint32_t fast_lambda_md[2];
    // mds0 state (md_process.h:1191-:1200 window).
    std::uint64_t mds0_best_cost;
    std::uint32_t mds0_best_idx;
    std::uint8_t mds0_best_class;
    std::uint64_t mds0_best_class0_cost;
    std::uint64_t mds0_best_cost_per_class[kCandClassTotal];
    std::uint32_t md_me_dist;                      // ctx->md_me_dist
    std::uint32_t md_pme_dist;                     // ctx->md_pme_dist
    // mds0_ctrls (md_process.h:804-:810).
    std::uint8_t pruning_method_th;                // mds0_ctrls.pruning_method_th
    std::uint16_t per_class_dist_to_cost_th[kCandClassTotal];
    std::uint16_t dist_to_cost_th;                 // mds0_ctrls.dist_to_cost_th
    std::uint8_t mds0_use_hadamard_blk;            // ctx->mds0_use_hadamard_blk
    // cand_reduction_ctrls.cand_elimination_ctrls (md_process.h:523-:533).
    std::uint32_t cand_elim_enabled;
    std::uint16_t dc_only_th;
    std::uint16_t skip_dc_th;
    // cand_reduction_ctrls.reduce_filter_intra (:820).
    std::uint8_t reduce_filter_intra;
    // intra_ctrls (md_process.h:778-:789).
    std::uint8_t intra_mode_end;
    std::uint8_t prune_using_best_mode;
    std::int8_t skip_angular_delta1_th;
    std::int8_t skip_angular_delta2_th;
    std::int8_t skip_angular_delta3_th;
    // obmc_ctrls (md_process.h:123-:138) - the consumed window.
    std::uint8_t obmc_enabled;
    std::uint8_t obmc_trans_face_off;
    std::uint8_t obmc_max_blk_size;
    // The candidate-buffer walk state (md_stage_0).
    MdCandBf** cand_bf_ptr_array;                  // ctx->cand_bf_ptr_array
    std::uint64_t* fast_cost_array;                // ctx->fast_cost_array
    std::int32_t* txCoeffBuf;                      // ctx->tx_coeffs->y_buffer (the hadamard coeffs)
    // The two dispatch tables (product_coding_loop.c:53-:62) - the callees
    // are the threaded prediction/fast-cost walls.
    PredictionFn predTable[2];
    FastCostFn fastCostTable[2];
    // The obmc_trans_face_off wall set: svt_aom_obmc_motion_mode_allowed
    // (mode_decision.c:228-:270) + svt_aom_inter_pu_prediction_av1_obmc
    // thread as callables; svt_aom_motion_mode_allowed (:1159-:1195) is the
    // LANDED l7 inter_symbols::motionModeAllowed - the carrier carries its
    // frame-header window so the ported body calls the landed function.
    using ObmcAllowedFn = bool (*)(const FlCtx& ctx, const MdCand& cand);
    using MotionModeAllowedFn = int (*)(const FlCtx& ctx, const MdCand& cand);
    ObmcAllowedFn obmcAllowedFn;
    MotionModeAllowedFn motionModeAllowedFn;
    PredictionFn obmcPredFn;
    // The rate-estimation motion-mode fac bits (md_rate_estimation.h: the
    // two [BLOCK_SIZES_ALL][...] tables, flat, bsize-row-major).
    const std::int32_t* motionModeFacBits1;   // [bsize][2]
    const std::int32_t* motionModeFacBits;    // [bsize][MOTION_MODES]
    // The svt_aom_motion_mode_allowed frame-header window (md_process.h:
    // the frm_hdr bits + the blk_ptr/window scalars the l7 helper reads).
    std::uint8_t is_motion_mode_switchable;
    std::uint8_t force_integer_mv;
    std::uint8_t allow_warped_motion;
    std::uint8_t gm_type_rf0;
    std::uint32_t overlappable_neighbors;
};

// av1me.h:50-51 AomVarianceFn: (a, a_stride, b, b_stride, sse) with a =
// pred, b = src at the fast-loop call sites. The dispatch is the
// svt_aom_mefn_ptr table (av1me.h:69) folded to a function - the C kernels
// are the landed analysis:: VAR instances (the fn_ptr-dispatch-as-direct-
// calls deviation, MC1's convention).
using VarianceFn = std::uint32_t (*)(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride,
                                     std::uint32_t* sse);

VarianceFn mefnVf(entropy::BlockSize bsize);

// product_coding_loop.c:966-:979 fast_loop_core_pd0.
void fastLoopCorePd0(MdCandBf* cand_bf, FlCtx* ctx, const PicBuf& input_pic, std::uint32_t input_origin_index,
                     std::uint32_t cu_origin_index);

// product_coding_loop.c:981-:1038 fast_loop_core_light_pd1 (the loc->
// input_origin_index flattens to the parameter).
void fastLoopCoreLightPd1(MdCandBf* cand_bf, FlCtx* ctx, const PicBuf& input_pic, std::uint32_t input_origin_index);

// product_coding_loop.c:1040-:1136 obmc_trans_face_off.
void obmcTransFaceOff(MdCandBf* cand_bf, FlCtx* ctx, const PicBuf& input_pic, std::uint32_t input_origin_index);

// product_coding_loop.c:1209-:1293 fast_loop_core (the BlockLocation*
// flattens to input_origin_index).
void fastLoopCore(MdCandBf* cand_bf, FlCtx* ctx, const PicBuf& input_pic, std::uint32_t input_origin_index);

// coding_unit.h:37 MAX_MODE_COST (the A6C constant) re-exported at the
// mdc surface for the test spans.
using pipeline::kMaxModeCost;
using pipeline::kMaxCuCost;

}  // namespace mdc
}  // namespace pipeline
