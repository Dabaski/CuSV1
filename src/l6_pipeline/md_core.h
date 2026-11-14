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

// me_sb_results.h:25 SQUARE_PU_COUNT + motion_estimation.h:97-:130: the
// three PU tables the NSQ MVC derivation walks (mechanically extracted;
// the RC1 runtime diff-verify test is the authority).
constexpr std::uint32_t kSquarePuCount = 85;
extern const std::uint32_t partitionWidth[kSquarePuCount];
extern const std::uint32_t partitionHeight[kSquarePuCount];
extern const std::uint32_t puSearchIndexMap[kSquarePuCount][2];

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
    // The chroma planes when the carrier models a full picture (0/null
    // when absent - the inverse-recon chain reads them).
    std::uint8_t* u_buf;
    std::uint8_t* v_buf;
    std::uint32_t u_stride;
    std::uint32_t v_stride;
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
    PicBuf recon;      // cand_bf->recon (the inverse-recon chain)
    PicBuf rec_coeff;  // cand_bf->rec_coeff (int32 through the byte-pointer convention)
    std::uint64_t* fast_cost;
    std::uint64_t* full_cost;
    std::uint64_t fast_luma_rate;
    std::uint64_t fast_chroma_rate;
    std::uint64_t luma_fast_dist;
    std::uint64_t full_dist;
    std::uint8_t valid_luma_pred;
    std::uint16_t y_has_coeff;   // cand_bf->y_has_coeff (the TU bitmask)
    std::uint8_t u_has_coeff;
    std::uint8_t v_has_coeff;
    std::uint16_t eob_y[16];
    std::uint16_t eob_u[16];
    std::uint16_t eob_v[16];
    std::uint8_t transform_type[16];   // cand->transform_type[txb_itr]
    std::uint8_t transform_type_uv;    // cand->transform_type_uv
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
    std::uint8_t mds_do_chroma;                    // ctx->mds_do_chroma (written)
    std::uint8_t mds_do_ifs;                       // ctx->mds_do_ifs (written)
    std::uint8_t target_class;                     // ctx->target_class (CandClass)
    std::uint8_t ifs_level;                        // ctx->ifs_ctrls.level
    std::uint32_t md_stage_1_count[kCandClassTotal];  // ctx->md_stage_1_count
    // The ME-search window (md_full_pel_search / nsq / sq / pme).
    Mv2 ref_mv;                                    // ctx->ref_mv
    std::uint8_t md_subpel_skip_diag_refinement;   // md_subpel_me_ctrls.skip_diag_refinement
    const std::int32_t* nmv_vec_cost;              // md_rate_est_ctx->nmv_vec_cost
    const std::int32_t* nmvcoststack0;             // md_rate_est_ctx->nmvcoststack[0]
    const std::int32_t* nmvcoststack1;
    std::int16_t sprs_lev0_start_x;                // ctx->sprs_lev0_* (the sparse box)
    std::int16_t sprs_lev0_end_x;
    std::int16_t sprs_lev0_start_y;
    std::int16_t sprs_lev0_end_y;
    std::uint32_t geom_offset_x;                   // ctx->geom_offset_x/y (derive_me_offsets)
    std::uint32_t geom_offset_y;
    std::uint32_t sb_ptr_index;                    // ctx->sb_ptr->index
    // ctx->mvp_array[list][ref] / ctx->mvp_count[list][ref] (mutable - the
    // build_single_ref_mvp_array writer; check_spatial_mv_size reads).
    Mv2* mvpArr[2][4];
    std::uint8_t mvpCnt[2][4];
    // The ME-MV bookkeeping the read_refine_me_mvs pair reads/writes
    // (mode_decision.h / md_process.h windows).
    Mv2 sbMeMv[2][4];                              // ctx->sb_me_mv
    Mv2 subMeMv[2][4];                             // ctx->sub_me_mv
    Mv2 fpMeMv[2][4];                              // ctx->fp_me_mv
    Mv2 sqSbMeMv[2][4];                            // ctx->sq_sb_me_mv
    std::int32_t postSubpelMeMvCost[2][4];         // ctx->post_subpel_me_mv_cost
    std::uint32_t fpMeDist[2][4];                  // ctx->fp_me_dist
    std::int32_t refFrameTypeArr[8];               // ctx->ref_frame_type_arr
    std::uint8_t totRefFrameTypes;                 // ctx->tot_ref_frame_types
    std::uint8_t isIntraBordered;                  // ctx->is_intra_bordered
    std::uint8_t useNeighboringModeEnabled;        // cand_reduction_ctrls.use_neighbouring_mode_ctrls.enabled
    std::uint8_t updatedEnablePme;                 // ctx->updated_enable_pme
    std::uint8_t shape;                            // ctx->shape (Part)
    std::uint32_t meSbAddr;                        // ctx->me_sb_addr (derive_me_offsets)
    std::uint32_t meBlockOffset;                   // ctx->me_block_offset
    std::uint32_t meCandOffset;                    // ctx->me_cand_offset
    std::uint32_t mdSubpelMinBlkSz;                // md_subpel_me_ctrls.min_blk_sz
    std::uint8_t mdSubpelEnabled;                  // md_subpel_me_ctrls.enabled
    std::uint8_t mdSubpelSearchMethod;             // md_subpel_me_ctrls.subpel_search_method
    // The per-(list,ref) resolved ref PicBufs (the get_ref_pic_buffer +
    // scaled-refs wall, pre-resolved by the caller - the S4/S5 park).
    const PicBuf* refPicArr[2][4];
    std::uint8_t refPruningEnabled;                // ref_pruning_ctrls.enabled
    // The md_sq_motion_search pcs walls threaded as scalars.
    std::int64_t sqPictureNumber;                  // pcs->picture_number
    std::int64_t sqRefPicPoc;                      // ppcs->ref_pic_poc_array[list][ref]
    std::uint8_t sqRefIsIntra;                     // the EbReferenceObject frame-type wall
    std::uint32_t sqMfmv0AsInt;                    // the TPL_MV_REF mfmv0 triple
    std::int16_t sqMfmv0X;
    std::int16_t sqMfmv0Y;
    std::uint8_t enableMe8x8;                      // ppcs->enable_me_8x8
    std::uint8_t enableMe16x16;                    // ppcs->enable_me_16x16
    // The ref-pruning state (perform_md_reference_pruning).
    std::uint32_t bestFpMvpDist[2][4];             // ctx->best_fp_mvp_dist
    std::uint8_t refFilteringRes[TOT_INTER_GROUP][2][4];  // ctx->ref_filtering_res .do_ref
    std::uint8_t bestFpMvpIdx[2][4];               // ctx->best_fp_mvp_idx
    // The PME state (pme_search).
    std::uint8_t validPmeMv[2][4];                 // ctx->valid_pme_mv
    Mv2 bestPmeMv[2][4];                           // ctx->best_pme_mv
    std::uint32_t pmeResDist[2][4];                // ctx->pme_res[list][ref].dist
    // The inverse-recon window (av1_perform_inverse_transform_recon*).
    std::uint8_t* cfl_temp_luma_recon;             // ctx->cfl_temp_luma_recon
    std::uint16_t* cfl_temp_luma_recon_16bit;      // ctx->cfl_temp_luma_recon16bit
    std::uint8_t uv_mode;                          // ctx->uv_ctrls.uv_mode (ChromaSearchMode)
    std::uint8_t fixed_partition;                  // ctx->fixed_partition
    std::uint8_t blk_y_has_coeff;                  // ctx->blk_ptr->y_has_coeff
    std::uint8_t blk_u_has_coeff;
    std::uint8_t blk_v_has_coeff;
    PicBuf bypass_recon_pic;                       // svt_aom_get_recon_pic (the wall, pre-resolved)
    PicBuf recon_tmp;                              // ctx->blk_ptr->recon_tmp
    std::uint32_t full_lambda_md[2];               // ctx->full_lambda_md[EB_8_BIT_MD / EB_10_BIT_MD]
    std::uint32_t fast_lambda_md[2];
    std::uint32_t full_sb_lambda_md[2];            // ctx->full_sb_lambda_md
    std::uint8_t qp_index;                         // ctx->qp_index
    std::uint8_t me_q_index;                       // ctx->me_q_index
    std::uint8_t hbd_pack_done;                    // ctx->hbd_pack_done (written)
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

// product_coding_loop.c:1500-:1580 process_cand_itr (the PAETH_PRED + 1
// = 13-entry regular_intra_cost array).
bool processCandItr(const FlCtx& ctx, const MdCand& cand, std::uint8_t itr, l16_rdo::PredictionMode best_reg_intra_mode,
                    std::uint64_t best_reg_intra_cost, const std::uint64_t* regular_intra_cost);

// product_coding_loop.c:1448-:1464 md_stage_0_pd0 (the ctx->fast_cand_array
// + its count travel as parameters).
void mdStage0Pd0(FlCtx* ctx, const PicBuf& input_pic, std::uint32_t input_origin_index, std::uint32_t blk_origin_index,
                 MdCand* fast_cand_array, std::uint32_t fast_cand_count);

// filter.h:60-72 av1_make_interp_filters / av1_broadcast_interp_filter.
std::uint32_t av1MakeInterpFilters(std::uint32_t y_filter, std::uint32_t x_filter);
std::uint32_t av1BroadcastInterpFilter(std::uint32_t filter);

// product_coding_loop.c:1466-:1498 md_stage_0_light_pd1 (the frame-header
// interpolation_filter threads as frame_interp_filter).
void mdStage0LightPd1(FlCtx* ctx, MdCandBf** cand_bf_ptr_array_base, MdCand* fast_cand_array,
                      std::uint32_t fast_cand_count, const PicBuf& input_pic, std::uint32_t input_origin_index,
                      std::uint32_t frame_interp_filter);

// md_process.c:29-:33 uni_psy_bias.
extern const std::uint8_t uniPsyBias[64];

// product_coding_loop.c:1855-:1990 md_full_pel_search. The pcs walls
// thread: frm_hdr base_q_idx -> base_q_idx; the ref-pic buffer travels as
// the PicBuf carrier (the scaled-ref machinery svt_aom_use_scaled_rec_
// refs_if_needed is the caller's concern - named park).
void mdFullPelSearch(FlCtx* ctx, const PicBuf& input_pic, const PicBuf& ref_pic, std::uint32_t input_origin_index,
                     DistortionType dist_type, std::int16_t mvx, std::int16_t mvy,
                     std::int16_t search_position_start_x, std::int16_t search_position_end_x,
                     std::int16_t search_position_start_y, std::int16_t search_position_end_y,
                     std::int16_t sparse_search_step, std::uint8_t is_sprs_lev0_performed, std::int16_t* best_mvx,
                     std::int16_t* best_mvy, std::uint32_t* best_cost, std::uint8_t hbd_md, std::uint8_t base_q_idx);

// product_coding_loop.c:1771-:1840 md_full_pel_search_large_lbd.
void mdFullPelSearchLargeLbd(const pipeline::SvtMvCostParam& mv_cost_params, FlCtx* ctx, const PicBuf& input_pic,
                             const PicBuf& ref_pic, std::uint32_t input_origin_index, std::int16_t mvx,
                             std::int16_t mvy, std::int16_t search_position_start_x,
                             std::int16_t search_position_end_x, std::int16_t search_position_start_y,
                             std::int16_t search_position_end_y, std::int16_t sparse_search_step,
                             std::int16_t* best_mvx, std::int16_t* best_mvy, std::uint32_t* best_cost);

// The MeSbResults carrier with the ME-MV array (me_sb_results.h:38-46; the
// l16 md_slices twin lacks me_mv_array - the NSQ/SQ spans read it).
struct MdMeSbResults {
    std::uint8_t* total_me_candidate_index;
    Mv2* me_mv_array;                       // [PU * max_refs + ref] (list-0) / + max_l0 + ref (list-1)
    l16_rdo::MeCandidate* me_candidate_array;
};

// MdNsqMotionSearchCtrls (md_process.h:291-:302, the consumed fields).
struct MdNsqCtrls {
    std::uint8_t enabled;
    DistortionType dist_type;
    std::uint8_t full_pel_search_width;
    std::uint8_t full_pel_search_height;
    std::uint8_t enable_psad;
};

// MdSqMotionSearchCtrls (md_process.h:304-:352, the consumed fields).
struct MdSqCtrls {
    std::uint8_t enabled;
    DistortionType dist_type;
    std::uint16_t pame_distortion_th;
    std::uint8_t sprs_lev0_enabled;
    std::uint8_t sprs_lev0_step;
    std::uint16_t sprs_lev0_w;
    std::uint16_t sprs_lev0_h;
    std::uint16_t max_sprs_lev0_w;
    std::uint16_t max_sprs_lev0_h;
    std::int16_t sprs_lev0_multiplier;
    std::uint8_t sprs_lev1_enabled;
    std::uint8_t sprs_lev1_step;
    std::uint16_t sprs_lev1_w;
    std::uint16_t sprs_lev1_h;
    std::uint16_t max_sprs_lev1_w;
    std::uint16_t max_sprs_lev1_h;
    std::int16_t sprs_lev1_multiplier;
    std::uint8_t sprs_lev2_enabled;
    std::uint8_t sprs_lev2_step;
    std::uint16_t sprs_lev2_w;
    std::uint16_t sprs_lev2_h;
    std::uint8_t enable_psad;
};

// MdSubPelSearchCtrls (md_process.h:384-:418, the consumed fields).
struct MdSubPelCtrls {
    std::uint8_t enabled;
    SubpelSearchType subpel_search_type;
    SubpelForceStop max_precision;
    SubpelSearchMethods subpel_search_method;
    int subpel_iters_per_step;
    int pred_variance_th;
    std::uint8_t abs_th_mult;
    int round_dev_th;
    std::uint8_t skip_diag_refinement;
    std::uint8_t min_blk_sz;
    std::uint8_t mvp_th;
    int hp_mv_th;
    std::int32_t bias_fp;
};

// product_coding_loop.c:2582-:2681 md_subpel_search_fixed_stage (the ref
// PicBuf + the src block travel as parameters).
std::uint32_t mdSubpelSearchFixedStage(FlCtx* ctx, const PicBuf& ref_pic, const std::uint8_t* src,
                                       const int src_stride, Mv2* me_mv, const MdSubPelCtrls& sub_ctrls);

// The mcomp subpel-tree wall (svt_av1_find_best_sub_pixel_tree_pruned,
// mcomp.c - PARKED in A3M) threads as the callable; MdSubpelParams
// (mcomp.h:70-100 SUBPEL_MOTION_SEARCH_PARAMS) carries the consumed
// window. buf2d is the reference's struct svt_buf_2d.
struct MdBuf2d {
    const std::uint8_t* buf;
    int width;
    int height;
    int stride;
};
struct MdSubpelParams {
    SubpelStage search_stage;        // :96
    std::uint8_t list_idx;           // :80
    std::uint8_t ref_idx;            // :81
    std::uint8_t allow_hp;           // :82 frm_hdr.allow_high_precision_mv
    SubpelForceStop forced_stop;     // :90
    int iters_per_step;              // :92
    int col_min, col_max, row_min, row_max;  // :100 the subpel range outs
    pipeline::SvtMvCostParam mv_cost_params;  // :75
    SubpelSearchType subpel_search_type;      // :93 var_params
    int var_w;                                // :94 block_size_wide
    int var_h;                                // :95 block_size_high
    MdBuf2d ref_buf;                          // ms_buffers->ref
    MdBuf2d src_buf;                          // ms_buffers->src
    int pred_variance_th;                     // :97
    int abs_th_mult;                          // :98
    int round_dev_th;                         // :99
    std::uint8_t skip_diag_refinement;        // :100-ish
    std::int32_t bias_fp;                     // var_params.bias_fp
};

// product_coding_loop.c:2462-:2578 md_subpel_search. The walls thread:
// the subpel tree as subpelSearchFn (the A3M park), xd->mi_row/mi_col as
// parameters, cm->mi_rows/mi_cols as cm_mi_rows/cm_mi_cols, the ref PicBuf
// pre-resolved (the scaled-ref park), frm_hdr base_q_idx as a parameter.
using MdSubpelSearchFn = int (*)(FlCtx& ctx, const MdSubpelParams& params, Mv2 subpel_start_mv, Mv2* best_mv);
int mdSubpelSearch(FlCtx* ctx, const PicBuf& input_pic, const PicBuf& ref_pic, SubpelStage search_stage,
                   const MdSubPelCtrls& md_subpel_ctrls, std::uint8_t rf, Mv2* me_mv, std::uint8_t allow_hp,
                   int mi_row, int mi_col, int cm_mi_rows, int cm_mi_cols, std::uint8_t base_q_idx,
                   MdSubpelSearchFn subpel_search_fn);

// The read_refine_me_mvs walls: choose_best_av1_mv_pred (the l16 threaded
// wall) writes the two best_pred_mv slots; the subpel TREE (fixed-stage vs
// the threaded pruned tree) routes through subpelTreeFn.
using ChooseBestMvPredFn = void (*)(FlCtx& ctx, std::int32_t ref_pair, Mv2 me_mv, Mv2* best_pred_mv);
using SubpelTreeFn = MdSubpelSearchFn;

// product_coding_loop.c:2685-:2760 read_refine_me_mvs_light_pd1. The pcs
// walls thread: me_results + max_l0/max_refs (the me_mv_array read), the
// per-ref resolved PicBufs on the carrier, me_block_offset/me_cand_offset
// as parameters (the caller's derive_me_offsets), frm_hdr base_q_idx.
void readRefineMeMvsLightPd1(FlCtx* ctx, const PicBuf& input_pic, const MdMeSbResults* me_results,
                             std::uint32_t me_block_offset, std::uint32_t me_cand_offset, std::uint8_t max_l0,
                             std::uint8_t max_refs, std::uint8_t base_q_idx, ChooseBestMvPredFn choose_best_mv_pred,
                             SubpelTreeFn subpel_tree, const MdSubPelCtrls& sub_ctrls, int mi_row, int mi_col,
                             int cm_mi_rows, int cm_mi_cols);

// product_coding_loop.c:2764-:2887 read_refine_me_mvs. The pc_tree walls
// thread: tested_blk[PART_N][0] as blk_avail_sqi, the parent's
// tested_blk[PART_N][0] as parent_tested_n0 (the 4x4 branch).
void readRefineMeMvs(FlCtx* ctx, const PicBuf& input_pic, const MdMeSbResults* me_results, std::uint8_t max_l0,
                     std::uint8_t max_refs, std::uint8_t max_cand, std::uint8_t base_q_idx,
                     ChooseBestMvPredFn choose_best_mv_pred, SubpelTreeFn subpel_tree,
                     const MdSubPelCtrls& sub_ctrls, const MdNsqCtrls& nsq_ctrls, const MdSqCtrls& sq_ctrls,
                     int mi_row, int mi_col, int cm_mi_rows, int cm_mi_cols, bool blk_avail_sqi,
                     bool parent_tested_n0);

// The TplSrcStats window (coding_unit.h:250-:258) - the consumed fields.
struct TplSrcStatsWindow {
    std::uint8_t best_mode;   // PredictionMode
    std::int32_t best_rf_idx;
};

// The ppcs window get_sb_tpl_inter_stats consumes (md_process.h /
// product_coding_loop.c:2895-:2955). The tpl_src_stats_buffer is the
// caller's flat [aligned16_height * aligned16_width] array; the walk
// indexes it with the SB origin.
struct TplPcsWindow {
    std::uint8_t tpl_enable;                  // ppcs->tpl_ctrls.enable
    std::uint8_t tpl_src_data_ready;          // ppcs->tpl_src_data_ready
    std::uint8_t disable_intra_pred_nref;     // ppcs->tpl_ctrls.disable_intra_pred_nref
    std::uint8_t temporal_layer_index;
    std::uint8_t hierarchical_levels;
    std::int32_t aligned_width;               // (ppcs->aligned_width + 15) >> 4 happens in the body
    std::uint8_t dispenser_search_level;      // tpl_ctrls.dispenser_search_level
    std::int32_t sb_width;                    // sb_geom[ctx->sb_index].width
    std::int32_t sb_height;
    const TplSrcStatsWindow* tpl_src_stats;   // the flat array base
    std::int32_t tpl_src_stride;              // aligned16_width
};

// product_coding_loop.c:2895-:2955 get_sb_tpl_inter_stats. The
// CONFIG_ENABLE_TPL=0 stub (product_coding_loop.c:2897-:2900) is dead
// under the pinned default build (EbConfigMacros.h:229-:231 CONFIG=1) -
// the body is the live code.
bool getSbTplInterStats(const TplPcsWindow& ppcs, std::uint32_t sb_origin_x, std::uint32_t sb_origin_y,
                        std::uint8_t* sb_inter_selection, std::uint8_t* sb_max_list0_ref_idx,
                        std::uint8_t* sb_max_list1_ref_idx);

// product_coding_loop.c:2957-:3045 perform_md_reference_pruning. The
// pcs walls thread: ppcs->picture_qp, the tpl window (the
// get_sb_tpl_inter_stats input), me_results + the me offsets.
void performMdReferencePruning(FlCtx* ctx, const l16_rdo::RefPruningCtrls& pruning, const MdMeSbResults* me_results,
                               const TplPcsWindow* tpl_window, std::uint8_t picture_qp);

// The ref_mv_stack window (mode_decision.h CandidateMv this_mv column):
// one pointer per frame_type (0..8), each pointing at the [0..3] this_mv
// array the NEAREST/NEAR walk reads.
struct MdRefMvStack {
    const Mv2* thisMv[9];  // [frame_type] -> the [0..3] array
};

// md_process.h:65-:80 InterCandGroup - the PRED_ME_GROUP ordinal only
// (the isValidUnipredRef input).
constexpr std::uint8_t kPredMeGroup = 5;

// MdPmeCtrls (md_process.h:354-:382, the consumed fields).
struct MdPmeCtrls {
    std::uint8_t enabled;
    DistortionType dist_type;
    int early_check_mv_th_multiplier;
    std::uint8_t full_pel_search_width;
    std::uint8_t full_pel_search_height;
    int pre_fp_pme_to_me_cost_th;
    int pre_fp_pme_to_me_mv_th;
    int post_fp_pme_to_me_cost_th;
    int post_fp_pme_to_me_mv_th;
    std::uint8_t enable_psad;
    std::uint8_t sa_q_weight;
};

// product_coding_loop.c:3050-:3141 build_single_ref_mvp_array.
void buildSingleRefMvpArray(FlCtx* ctx, const PicBuf& input_pic, std::uint8_t base_q_idx,
                            const MdRefMvStack& ref_mv_stack);

// product_coding_loop.c:3150-:3325 pme_search. The walls thread: the qp
// scaling inputs (pme_qp_based_th_scaling enable + static_config.qp),
// mrp_ctrls.pme_ref0_only + pcs->temporal_layer_index, the subpel TREE
// via SubpelTreeFn, the choose_best_av1_mv_pred wall, the l16
// isValidUnipredRef carriers and the av1xd/cm mi windows.
void pmeSearch(FlCtx* ctx, const PicBuf& input_pic, const MdMeSbResults* me_results, const MdPmeCtrls& pme_ctrls,
               const MdSubPelCtrls& subpel_pme_ctrls, std::uint8_t base_q_idx, std::uint8_t qp,
               std::uint8_t pme_qp_th_scaling, std::uint8_t pme_ref0_only, std::uint8_t pcs_temporal_layer_index,
               const l16_rdo::RefPruningCtrls& pruning, const l16_rdo::RefFilteringRes filtering,
               SubpelTreeFn subpel_tree, const MdSubPelCtrls& subpel_me_ctrls, ChooseBestMvPredFn choose_best_mv_pred,
               int mi_row, int mi_col, int cm_mi_rows, int cm_mi_cols);

// md_process.c:767-:791 svt_aom_reset_mode_decision. The tile-group walk
// resets the neighbour arrays; the hbd_md/bypass_encdec writes land on
// the carrier; the pcs-owned [slot][tile] NA arrays travel as the
// per-slot views (the tile dimension is flattened, the A6C carrier
// convention).
struct MdTileGroupInfo {
    std::uint16_t tile_group_tile_start_y;
    std::uint16_t tile_group_tile_end_y;
    std::uint16_t tile_group_tile_start_x;
    std::uint16_t tile_group_tile_end_x;
};
struct MdPcsNa {
    NeighborArrayUnit* mdleaf_partition_na[kNaTotCnt];
    NeighborArrayUnit* md_luma_recon_na[kNaTotCnt];
    NeighborArrayUnit* md_tx_depth_1_luma_recon_na[kNaTotCnt];
    NeighborArrayUnit* md_tx_depth_2_luma_recon_na[kNaTotCnt];
    NeighborArrayUnit* md_cb_recon_na[kNaTotCnt];
    NeighborArrayUnit* md_cr_recon_na[kNaTotCnt];
    NeighborArrayUnit* md_luma_recon_na_16bit[kNaTotCnt];
    NeighborArrayUnit* md_tx_depth_1_luma_recon_na_16bit[kNaTotCnt];
    NeighborArrayUnit* md_tx_depth_2_luma_recon_na_16bit[kNaTotCnt];
    NeighborArrayUnit* md_cb_recon_na_16bit[kNaTotCnt];
    NeighborArrayUnit* md_cr_recon_na_16bit[kNaTotCnt];
    NeighborArrayUnit* md_y_dcs_na[kNaTotCnt];
    NeighborArrayUnit* md_tx_depth_1_luma_dc_sign_level_coeff_na[kNaTotCnt];
    NeighborArrayUnit* md_cb_dc_sign_level_coeff_na[kNaTotCnt];
    NeighborArrayUnit* md_cr_dc_sign_level_coeff_na[kNaTotCnt];
    NeighborArrayUnit* md_txfm_context_array[kNaTotCnt];
};
void resetModeDecision(FlCtx* ctx, const MdTileGroupInfo& tile_group, int tile_cols, std::uint8_t pcs_hbd_md,
                       std::uint8_t pic_bypass_encdec, const MdPcsNa& nas, std::uint32_t segment_index);

// md_process.c:796-:812 svt_aom_mode_decision_configure_sb: the qp
// selection (delta_q_present || r0_delta_qp_md -> sb_qp else base_q_idx),
// me_q_index, the landed av1LambdaAssignMd and the hbd_pack_done clear.
void modeDecisionConfigureSb(FlCtx* ctx, std::uint8_t sb_qp, std::uint8_t me_sb_qp, const LambdaAssignPcs& pcs);

// ---------------------------------------------------------------------------
// The inverse-transform recon chain (full_loop.c:1755-:1789 wrapper +
// inv_transforms.c:3139-:3165 recon8bit + product_coding_loop.c:695-:953).
// ---------------------------------------------------------------------------

// inv_transforms.h:129-:137 av1_get_max_eob (txSize2d is the landed l6
// extern).
int av1GetMaxEob(entropy::TxSize tx_size);

// svt_aom_inv_transform_recon_wrapper (full_loop.c:1755-:1789): the 8-bit
// branch builds the TxfmParamHb and routes through the landed
// invTxfmAddStagingHb (the recon_r != recon_w eob forcing from
// inv_transforms.c:3154-:3159 folded in); the hbd branch routes through
// the landed invTransformReconHb at EB_TEN_BIT.
void invTransformReconWrapper(const std::int32_t* rec_coeff_buffer, std::uint32_t coeff_offset, std::uint8_t* pred_buffer,
                              std::uint32_t pred_offset, std::uint32_t pred_stride, std::uint8_t* rec_buffer,
                              std::uint32_t rec_offset, std::uint32_t rec_stride, bool hbd, entropy::TxSize txsize,
                              entropy::TxType transform_type, std::uint32_t eob, bool lossless);

// pic_operators.h:103-:160 svt_av1_picture_copy_y / _cb / _cr (the
// copyWxh inline wrappers; the 16-bit casts through the byte buffers).
void pictureCopyY(const PicBuf& src, std::uint32_t src_origin_index, const PicBuf& dst, std::uint32_t dst_origin_index,
                  std::uint32_t width, std::uint32_t height, bool hbd);
void pictureCopyCb(const PicBuf& src, std::uint32_t src_origin_index, const PicBuf& dst, std::uint32_t dst_origin_index,
                   std::uint32_t width, std::uint32_t height, bool hbd);
void pictureCopyCr(const PicBuf& src, std::uint32_t src_origin_index, const PicBuf& dst, std::uint32_t dst_origin_index,
                   std::uint32_t width, std::uint32_t height, bool hbd);

// product_coding_loop.c:695-:752 av1_perform_inverse_transform_recon_luma.
void av1PerformInverseTransformReconLuma(FlCtx* ctx, MdCandBf* cand_bf);

// product_coding_loop.c:754-:953 av1_perform_inverse_transform_recon.
void av1PerformInverseTransformRecon(FlCtx* ctx, MdCandBf* cand_bf, const MdGeom& blk_geom);

// product_coding_loop.c:2021-:2190 md_nsq_motion_search. The pcs walls
// thread: pa_me_data max_refs/max_l0/max_cand, enable_me_8x8/16x16,
// max_number_of_pus_per_sb (= SQUARE_PU_COUNT, resource_coordination_
// process.c); the ref pic travels pre-resolved (the scaled-ref park).
void mdNsqMotionSearch(FlCtx* ctx, const PicBuf& input_pic, const PicBuf& ref_pic, std::uint32_t input_origin_index,
                       std::uint8_t rf, const MdMeSbResults* me_results, std::int16_t* me_mv_x, std::int16_t* me_mv_y,
                       const MdNsqCtrls& nsq_ctrls, std::uint8_t max_refs, std::uint8_t max_l0,
                       std::uint8_t enable_me_8x8, std::uint8_t enable_me_16x16,
                       std::uint8_t max_number_of_pus_per_sb, std::uint8_t max_cand, std::uint8_t base_q_idx);

// product_coding_loop.c:2270-:2457 md_sq_motion_search. The pcs walls
// thread: picture_number - ref_pic_poc dist (the two int64s), the ref
// frame-type gate as ref_is_intra (the EbReferenceObject frame-type wall),
// the TPL mfmv0 triple (the check_temporal_mv_size input) and the pre-
// resolved ref PicBuf.
void mdSqMotionSearch(FlCtx* ctx, const PicBuf& input_pic, const PicBuf& ref_pic, std::uint32_t input_origin_index,
                      std::uint8_t rf, std::int16_t* me_mv_x, std::int16_t* me_mv_y, const MdSqCtrls& sq_ctrls,
                      std::int64_t picture_number, std::int64_t ref_pic_poc, std::uint8_t ref_is_intra,
                      std::uint32_t mfmv0_as_int, std::int16_t mfmv0_x, std::int16_t mfmv0_y, std::uint8_t base_q_idx);

// product_coding_loop.c:1582-:1714 md_stage_0. The pcs walls thread:
// scs->vq_ctrls.sharpness_ctrls.unipred_bias && pcs->ppcs->is_noise_level
// fold into unipred_bias; pcs->ppcs->picture_qp into picture_qp; the
// frm_hdr interpolation_filter into frame_interp_filter.
void mdStage0(FlCtx* ctx, MdCandBf** cand_bf_ptr_array_base, MdCand* fast_candidate_array,
              std::uint32_t fast_cand_count, const PicBuf& input_pic, std::uint32_t input_origin_index,
              std::uint32_t cand_bf_start_index, std::uint32_t max_buffers, std::uint8_t unipred_bias,
              std::uint8_t picture_qp, std::uint32_t frame_interp_filter);

// coding_unit.h:37 MAX_MODE_COST (the A6C constant) re-exported at the
// mdc surface for the test spans.
using pipeline::kMaxModeCost;
using pipeline::kMaxCuCost;

}  // namespace mdc
}  // namespace pipeline
