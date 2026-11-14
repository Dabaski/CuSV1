// l16_rdo A4M - the mode_decision.c REMAINDER surface (the spans MD1 did not
// land). Reference: third_party/SVT-AV1/Source/Lib/Codec/mode_decision.c
// (4,490 physical lines). Each span carries the ModeDecisionContext/pcs walls
// as carrier structs with the reference field names (the RE8A DeblockContext
// precedent); bodies verbatim. Struct-free, /W4 /WX clean, ASCII-only.
//
// Span map (grow per commit):
//   A4M-1  mv_check_bounds                        mode_decision.c:3000-3003
//   A4M-1  derive_ssim_threshold_factor_for_full_md  :3875-3877
//   A4M-2  av1_set_ref_frame (carry)              inter_prediction.h:487-496
//   A4M-2  valid_ref_frame_type                   mode_decision.c:3489-3504
#pragma once

#include <cstdint>

#include "rate_est.h"
#include "mode_config.h"
#include "md_slices.h"  // the MD1 carriers (InjectedMvCarrier, MeSbResults)

namespace l16_rdo {

// The ResolutionRange enumerators live in mode_config; hoist them so the
// md_slices2 surface spells the reference names at l16_rdo scope.
using mode_config::ResolutionRange;
using mode_config::INPUT_SIZE_240p_RANGE;
using mode_config::INPUT_SIZE_360p_RANGE;
using mode_config::INPUT_SIZE_480p_RANGE;
using mode_config::INPUT_SIZE_720p_RANGE;
using mode_config::INPUT_SIZE_1080p_RANGE;
using mode_config::INPUT_SIZE_4K_RANGE;
using mode_config::INPUT_SIZE_8K_RANGE;

// ---------------------------------------------------------------------------
// Carriers (the RE8A DeblockContext precedent): the consumed reference
// structs, mirrored field-by-field.
// ---------------------------------------------------------------------------

// Mv (mv.h:42-49, verbatim layout). MSVC C4201 fires on the anonymous struct
// under /WX; the amvp.h:231-240 twin carries the same pragma.
#pragma warning(push)
#pragma warning(disable : 4201)
union Mv {
    struct {
        std::int16_t x;
        std::int16_t y;
    };
    std::uint32_t as_int;  // faster equality tests and copies
};
#pragma warning(pop)

// MvLimits (definitions.h:278-283, verbatim).
struct MvLimits {
    std::int32_t col_min;
    std::int32_t col_max;
    std::int32_t row_min;
    std::int32_t row_max;
};

// ---------------------------------------------------------------------------
// A4M-1: mv_check_bounds (mode_decision.c:3000-3003). The scs/ctx walls
// thread as the plain pointers (the INLINE helper takes exactly these).
// ---------------------------------------------------------------------------
std::int32_t mvCheckBounds(const MvLimits* mv_limits, const Mv* mv);

// A4M-1: derive_ssim_threshold_factor_for_full_md (mode_decision.c:3875-3877).
// The scs wall threads as the input-resolution enum it reads.
double deriveSsimThresholdFactorForFullMd(ResolutionRange inputResolution);

// ---------------------------------------------------------------------------
// A4M-2: the reference-frame type surface. The landed rate_est.h spells
// INTRA_FRAME as a macro (= 0) and the MvRefFrameExt enum as MVX_*; the
// reference names here carry the _MV suffix (the MD-side int8 MvReferenceFrame
// domain, definitions.h:1357-1386 + :1593).
// ---------------------------------------------------------------------------
typedef std::int8_t MvReferenceFrame;  // definitions.h:1593

// definitions.h:1357-1386 + :1389 (REFS_PER_FRAME = 7) + :1377 + :1419.
constexpr MvReferenceFrame NONE_FRAME_MV = -1;
constexpr MvReferenceFrame INTRA_FRAME_MV = 0;
constexpr MvReferenceFrame LAST_FRAME_MV = 1;
constexpr MvReferenceFrame LAST2_FRAME_MV = 2;
constexpr MvReferenceFrame LAST3_FRAME_MV = 3;
constexpr MvReferenceFrame GOLDEN_FRAME_MV = 4;
constexpr MvReferenceFrame BWDREF_FRAME_MV = 5;
constexpr MvReferenceFrame ALTREF2_FRAME_MV = 6;
constexpr MvReferenceFrame ALTREF_FRAME_MV = 7;
constexpr int REF_FRAMES_MV = 8;             // definitions.h:1384 LAST_BWD_FRAME
constexpr int REFS_PER_FRAME_MV = 7;         // definitions.h:1389
constexpr int TOTAL_REFS_PER_FRAME_MV = 8;   // definitions.h:1377
constexpr int FWD_REFS_MV = 4;               // definitions.h:1376
constexpr int BWD_REFS_MV = 3;               // definitions.h:1378
constexpr int TOTAL_UNIDIR_COMP_REFS_MV = 9;  // definitions.h:1367
constexpr int TOTAL_COMP_REFS_MV = FWD_REFS_MV * BWD_REFS_MV + TOTAL_UNIDIR_COMP_REFS_MV;  // :1382 = 21
constexpr int MODE_CTX_REF_FRAMES_MV = TOTAL_REFS_PER_FRAME_MV + TOTAL_COMP_REFS_MV;       // :1419 = 29

// av1_set_ref_frame (inter_prediction.h:487-496). Module-boundary carry
// (l16_rdo cannot link l5_motion; the l7-l13 duplication precedent): the
// landed twin is l5 amvp.cpp:183; the ref_frame_map row set is
// inter_prediction.h:461-485.
void av1SetRefFrame(MvReferenceFrame* rf, std::int8_t refFrameType);

// valid_ref_frame_type (mode_decision.c:3489-3504): the pcs wall threads as
// the ref_frame_type_arr pointer + count (md_process.h:944). Deviation,
// named: the reference's rf parameter is non-const and never written - the
// port takes it const.
bool validRefFrameType(const MvReferenceFrame rf[2], const MvReferenceFrame* refFrameTypeArr,
                       std::uint8_t totRefFrameTypes);

// ---------------------------------------------------------------------------
// A4M-3: the compound-mode surface.
// ---------------------------------------------------------------------------

// MD_COMP_TYPE (definitions.h:1264-1270, verbatim order).
enum MdCompType {
    MD_COMP_AVG,
    MD_COMP_DIST,
    MD_COMP_DIFF0,
    MD_COMP_WEDGE,
    MD_COMP_TYPES,
};

// COMPOUND_TYPE (definitions.h:1238-1245, verbatim order).
enum CompoundType {
    COMPOUND_AVERAGE,
    COMPOUND_DISTWTD,
    COMPOUND_WEDGE,
    COMPOUND_DIFFWTD,
    COMPOUND_TYPES,
    MASKED_COMPOUND_TYPES = 2,
};

// MIN (the aom macro as a function; the l16 surface cannot spell the macro
// without clashing with the landed rate_est.h defines).
inline int aomMin(int a, int b) { return a < b ? a : b; }

// svt_aom_get_wedge_params_bits (inter_prediction.c:2064-2066) as the bits
// column of wedge_params_lookup (:2001-2024). Module-boundary carry (the
// landed twin is l17 inter_prediction.cpp:390 over its full lookup); the
// rows follow the rate_est.h BlockSize order.
extern const std::int32_t wedgeParamsBits[BLOCK_SIZES_ALL];

// get_tot_comp_types_bsize (mode_decision.c:124-126): the
// CONFIG_ENABLE_INTER_COMPOUND guard is LIVE (= 1, EbConfigMacros.h
// non-RTC block - the inter_symbols.h:18-22 finding), so the body is kept.
std::int32_t getTotCompTypesBsize(std::int32_t totCompTypes, l16_rdo::BlockSize bsize);

// The written BlockModeInfo fields of determine_compound_mode
// (mode_decision.c:515-552). The svt_aom_search_compound_diff_wedge wall
// (enc_inter_prediction.c:3718) threads as the called flag - its candidate
// mutation stays parked with the wall (named).
struct CompoundModeCarrier {
    CompoundType interinterCompType;  // block_mi.interinter_comp.type :522
    std::int32_t compGroupIdx;        // block_mi.comp_group_idx :525/:529/:533/:541
    std::int32_t compoundIdx;         // block_mi.compound_idx :526/:530/:534/:542
    std::int32_t maskType;            // block_mi.interinter_comp.mask_type :535
    bool searchDiffWedgeCalled;       // the :537/:544 wall call
};

// determine_compound_mode (mode_decision.c:515-552) over to_av1_compound_lut
// (:513). Deviation, named: the pcs/ctx/cand walls thread as the carrier +
// flag; the SVT_ERROR/assert(0) default arm is NDEBUG-dropped convention.
void determineCompoundMode(CompoundModeCarrier* blockMi, MdCompType curType);

// ---------------------------------------------------------------------------
// A4M-4: the OBMC motion-mode surface.
// ---------------------------------------------------------------------------

// TransformationType (definitions.h:1740-1747, verbatim order). The
// _TT suffix avoids the landed rate_est.h macro space.
enum TransformationType {
    IDENTITY_TT = 0,
    TRANSLATION_TT = 1,
    ROTZOOM_TT = 2,
    AFFINE_TT = 3,
    TRANS_TYPES_TT = 4,
};

// PredictionMode suffix aliases for the md_slices2 tests/surfaces (the
// landed rate_est.h enum PredictionMode owns the plain names; the
// inter-mode constants are the same values).
constexpr PredictionMode DC_PRED_PM = DC_PRED;
constexpr PredictionMode NEARESTMV_PM = NEARESTMV;
constexpr PredictionMode NEARMV_PM = NEARMV;
constexpr PredictionMode GLOBALMV_PM = GLOBALMV;
constexpr PredictionMode NEWMV_PM = NEWMV;
constexpr PredictionMode NEAREST_NEARESTMV_PM = NEAREST_NEARESTMV;
constexpr PredictionMode NEAR_NEARMV_PM = NEAR_NEARMV;
constexpr PredictionMode NEAREST_NEWMV_PM = NEAREST_NEWMV;
constexpr PredictionMode NEW_NEARESTMV_PM = NEW_NEARESTMV;
constexpr PredictionMode NEAR_NEWMV_PM = NEAR_NEWMV;
constexpr PredictionMode NEW_NEARMV_PM = NEW_NEARMV;
constexpr PredictionMode GLOBAL_GLOBALMV_PM = GLOBAL_GLOBALMV;
constexpr PredictionMode NEW_NEWMV_PM = NEW_NEWMV;
constexpr PredictionMode MB_MODE_COUNT_PM = MB_MODE_COUNT;

// is_motion_variation_allowed_bsize (inter_prediction.h:378-380).
bool isMotionVariationAllowedBsize(BlockSize bsize);

// is_inter_singleref_mode (definitions.h:1610-1612).
std::int32_t isInterSinglerefMode(PredictionMode mode);

// is_global_mv_block (inter_prediction.h:382-388). CONFIG_ENABLE_GLOBAL_
// MOTION is live (= 1, the non-RTC EbConfigMacros block), so the guard body
// is kept (the l7 inter_symbols.cpp:195-198 twin).
std::int32_t isGlobalMvBlock(PredictionMode mode, BlockSize bsize, TransformationType type);

// ObmcControls (md_process.h:123-135), the consumed fields only.
struct ObmcCtrlsCarrier {
    std::uint8_t enabled;        // :124
    std::uint8_t maxBlkSize;     // :128 max_blk_size (test cap)
    std::uint8_t transFaceOff;   // :133 trans_face_off
};

// svt_aom_obmc_motion_mode_allowed (mode_decision.c:228-270). The walls
// thread per the campaign pattern: obmc_ctrls as the carrier; the frm_hdr
// pair (is_motion_mode_switchable, force_integer_mv) as flags; the
// global-motion wmtype for rf0 as the enum (the reference's rf0 only selects
// the global_motion slot - the slot index itself is dropped, named); the
// blk_ptr wall as the overlappable-neighbor count
// (has_overlappable_candidates == neighbors > 0). The !CONFIG_ENABLE_OBMC &&
// !CONFIG_ENABLE_WARP early-out (:232-234) is DEAD under the pinned build
// (both = 1) and is not ported.
MotionMode obmcMotionModeAllowed(const ObmcCtrlsCarrier& obmcCtrls, bool isMotionModeSwitchable,
                                 std::int32_t forceIntegerMv, TransformationType gmTypeRf0,
                                 std::uint32_t overlappableNeighbors, BlockSize bsize,
                                 std::uint8_t situation, MvReferenceFrame rf1, PredictionMode mode);

// ---------------------------------------------------------------------------
// A4M-5: skip_compound_on_ref_types (mode_decision.c:1008-1048).
// ---------------------------------------------------------------------------

// get_list_idx (inter_prediction.h:505-509) over ref_type_to_list_idx
// (:505, REFS_PER_FRAME + 1 = 8 rows {0,0,0,0,0,1,1,1}). Module-boundary
// carry (the landed rate_est.h spells no twin).
std::uint8_t getListIdx(MvReferenceFrame refType);

// is_inter_compound_mode (definitions.h:1603-1608). CONFIG_ENABLE_INTER_
// COMPOUND is live (= 1), so the body is kept (the l7 inter_symbols.cpp
// :228-230 twin).
std::int32_t isInterCompoundMode(PredictionMode mode);

// The neighbor BlockModeInfo surface the walk reads (block_structures.h
// block_mi.mode/ref_frame).
struct SkipCompNdMi {
    PredictionMode mode;
    MvReferenceFrame refFrame[2];
};

// The xd wall (MacroBlockD, coding_unit.h): the two availability flags and
// the neighbor mbmi pointers, pointer-shaped like the l7 InterMbD precedent
// (NULL when the edge is unavailable).
struct SkipCompXdCarrier {
    std::int32_t leftAvailable;    // xd->left_available
    std::int32_t upAvailable;      // xd->up_available
    const SkipCompNdMi* leftMbmi;  // xd->left_mbmi->block_mi
    const SkipCompNdMi* aboveMbmi; // xd->above_mbmi->block_mi
};

// skip_compound_on_ref_types (mode_decision.c:1008-1048): the
// inter_comp_ctrls.skip_on_ref_info wall threads as the first parameter.
// The rf pair is the compound candidate's ref_frame (the caller's domain;
// rf[1] is a list-1 slot for every call site - the :1017 get_list_idx(rf[1])
// read is defined only there).
bool skipCompoundOnRefTypes(bool skipOnRefInfo, const SkipCompXdCarrier& xd, const MvReferenceFrame rf[2]);

// ---------------------------------------------------------------------------
// A4M-6: setup_pred_plane / svt_av1_setup_pred_block (mode_decision.c
// :2051-2080).
// ---------------------------------------------------------------------------

// Buf2D (definitions.h:270-276, verbatim).
struct Buf2D {
    std::uint8_t* buf;
    std::uint8_t* buf0;
    std::int32_t width;
    std::int32_t height;
    std::int32_t stride;
};

// MAX_PLANES (definitions.h:246) + MI_SIZE (definitions.h:354 = 1 <<
// MI_SIZE_LOG2; the landed rate_est.h MI_SIZE_LOG2 = 2 is the shift).
#define MAX_PLANES_MD 3
#define MI_SIZE_MD 4

// Yv12BufferConfig, the consumed fields only (aom yv12 buffer config; the
// SVT port reads y/u/v buffers, the two strides and the crop dims).
struct Yv12Lite {
    std::uint8_t* yBuffer;
    std::uint8_t* uBuffer;
    std::uint8_t* vBuffer;
    std::int32_t yStride;
    std::int32_t uvStride;
    std::int32_t yCropWidth;
    std::int32_t yCropHeight;
};

// setup_pred_plane (mode_decision.c:2051-2068): the INLINE helper exposed
// for testability (the DF1 cdefSign precedent); the scale is the no-scale
// call (the scaled_buffer_offset comment :2063).
void setupPredPlane(Buf2D* dst, BlockSize bsize, std::uint8_t* src, std::int32_t width, std::int32_t height,
                    std::int32_t stride, std::int32_t miRow, std::int32_t miCol, std::int32_t subsamplingX,
                    std::int32_t subsamplingY);

// svt_av1_setup_pred_block (mode_decision.c:2070-2080).
void setupPredBlock(BlockSize bsize, Buf2D dst[MAX_PLANES_MD], const Yv12Lite* src, std::int32_t miRow,
                    std::int32_t miCol);

// ---------------------------------------------------------------------------
// A4M-7: the variance-map + DC-only surface.
// ---------------------------------------------------------------------------

// Part (definitions.h:929-940, the MD ctx->shape enum, verbatim order).
enum Part {
    PART_N_MD,
    PART_H_MD,
    PART_V_MD,
    PART_H4_MD,
    PART_V4_MD,
    PART_HA_MD,
    PART_HB_MD,
    PART_VA_MD,
    PART_VB_MD,
    PART_S_MD,
};

// svt_aom_get_blk_var_map (product_coding_loop.c:8054-8099). Module-boundary
// carry (l16_rdo cannot link the l6 coding_loop surface; the l7-l13
// duplication precedent): svt_log2f is get_msb (definitions.h:592), the
// three LUTs are :8054-8056 verbatim, the out-of-range row is the :8064-8068
// sentinel.
void getBlkVarMap(std::int32_t blockSize, std::int32_t orgX, std::int32_t orgY, std::int32_t* blkIdx,
                  std::int32_t subIdx[4]);

// The is_dc_only_safe wall set (mode_decision.c:847-861): the pcs->scs->
// super_block_size / ctx->shape / ctx->blk_geom->sq_size / the two origins /
// ctx->sb_index and the ppcs->variance[sb_index] row. The intra_ctrls.
// prune_using_edge_info flag (md_process.h:785) threads as the first field.
struct DcOnlyCarrier {
    bool pruneUsingEdgeInfo;      // :849 ctx->intra_ctrls.prune_using_edge_info
    std::uint32_t superBlockSize; // :849 pcs->scs->super_block_size
    Part shape;                   // :849 ctx->shape
    std::int32_t sqSize;          // :849/:864 ctx->blk_geom->sq_size
    std::uint32_t blkOrgX;        // :857 ctx->blk_org_x
    std::uint32_t blkOrgY;        // :857 ctx->blk_org_y
    std::uint32_t sbOriginX;      // :857 ctx->sb_origin_x
    std::uint32_t sbOriginY;      // :857 ctx->sb_origin_y
    std::uint32_t sbIndex;        // :860 ctx->sb_index
    const std::uint16_t* sbVar;   // :860 pcs->ppcs->variance[sb_index]
};

// is_dc_only_safe (mode_decision.c:847-881): the 8x8 row returns
// blk_var < 2000 with no spread check; 16x16+ folds the four sub_idx
// entries and adds spread < 4000. Deviation, named: the reference returns
// uint8_t; the port returns int (the same 0/1).
std::int32_t isDcOnlySafe(const DcOnlyCarrier& c);

// ---------------------------------------------------------------------------
// A4M-8: choose_best_av1_mv_pred + its three carries.
// ---------------------------------------------------------------------------

// CandidateMv (mv.h:51-55, verbatim).
struct CandidateMv {
    Mv this_mv;
    Mv comp_mv;
    std::int32_t weight;
};

// MAX_REF_MV_STACK_SIZE (definitions.h:1343) / REF_CAT_LEVEL (:1344) /
// DRL_MODE_CONTEXTS (:1322). The _MD suffix avoids the landed macro space.
#define MAX_REF_MV_STACK_SIZE_MD 8
#define REF_CAT_LEVEL_MD 640
#define DRL_MODE_CONTEXTS_MD 3

// svt_av1_mv_bit_cost_light (rd_cost.c:62-68). Module-boundary carry: the
// landed rate_est trio has mvBitCost (:877) but not the light form.
std::int32_t mvBitCostLight(const Mv* mv, const Mv* ref);

// av1_drl_ctx (rd_cost.h:85-89).
std::uint8_t av1DrlCtx(const CandidateMv* refMvStack, std::int32_t refIdx);

// compound_ref0_mode / compound_ref1_mode (inter_prediction.h:312-343 /
// :345-376): the two 25-entry LUTs verbatim. Exposed for the drl-carry
// tests (the amvp.cpp:1203/1213 twins are file-local).
PredictionMode compoundRef0Mode(PredictionMode mode);
PredictionMode compoundRef1Mode(PredictionMode mode);

// svt_aom_get_av1_mv_pred_drl (adaptive_mv_pred.c:1407-1457).
// Module-boundary carry (the landed twin is l5 amvp.cpp:1283). Deviation,
// named: the ModeDecisionContext/blk_ptr walls thread as the two reads the
// body consumes - the ref_mv_stack array and the xd->ref_mv_count array;
// the :1426 assert is NDEBUG-dropped.
void getAv1MvPredDrl(const CandidateMv refMvStack[MODE_CTX_REF_FRAMES_MV][MAX_REF_MV_STACK_SIZE_MD],
                     const std::uint8_t* refMvCount, MvReferenceFrame refFrame, std::uint8_t isCompound,
                     PredictionMode mode, std::uint8_t drlIndex, Mv nearestmv[2], Mv nearmv[2], Mv refMv[2]);

// svt_aom_choose_best_av1_mv_pred (mode_decision.c:554-645). The walls
// thread per the campaign pattern: ctx->ref_mv_stack as the array,
// blk_ptr->av1xd->ref_mv_count as the count array, shut_fast_rate /
// approx_inter_rate as flags, md_rate_est_ctx as the three reads (nmv_vec_
// cost, nmvcoststack, drl_mode_fac_bits).
void chooseBestAv1MvPred(const CandidateMv refMvStack[MODE_CTX_REF_FRAMES_MV][MAX_REF_MV_STACK_SIZE_MD],
                         const std::uint8_t* refMvCount, bool shutFastRate, std::uint8_t approxInterRate,
                         const std::int32_t* nmvVecCost, const std::int32_t* const* nmvCostStack,
                         const std::int32_t drlModeFacBits[DRL_MODE_CONTEXTS_MD][2],
                         MvReferenceFrame refFrame, PredictionMode mode, Mv mv0, Mv mv1,
                         std::uint8_t* bestDrlIndex, Mv bestPredMv[2]);

// ---------------------------------------------------------------------------
// A4M-9: the TPL/SSIM lambda-tuning surface.
// ---------------------------------------------------------------------------

// Tune (definitions.h:1862-1869, verbatim).
enum Tune {
    TUNE_VQ = 0,
    TUNE_PSNR = 1,
    TUNE_SSIM = 2,
    TUNE_IQ = 3,
    TUNE_MS_SSIM = 4,
    TUNE_VMAF = 5,
};

// SUPERRES_INVALID_STATE (mode_decision.c:69).
#define SUPERRES_INVALID_STATE_MD 0x7fffffff

// coded_to_superres_mi (resize.h:71-73). Module-boundary carry (the landed
// twin is l6 src_ops.cpp:321); SCALE_NUMERATOR = 8.
std::int32_t codedToSuperresMi(std::int32_t miCol, std::int32_t denom);

// The ppcs/pcs wall set of the two lambda writers (mode_decision.c
// :4131-4174 + :4176-4236). The pic lambda pairs are the ed_ctx reads
// (EB_8_BIT_MD = 0 / EB_10_BIT_MD = 1, definitions.h:1857-1858).
struct SbLambdaPcs {
    std::int32_t superresDenom;          // ppcs->superres_denom
    std::int32_t enhancedUnscaledWidth;  // ppcs->enhanced_unscaled_pic->width
    std::uint8_t synthBlkSize;           // ppcs->tpl_ctrls.synth_blk_size
    std::int32_t miRows;                 // cm->mi_rows
    std::int32_t miCols;                 // cm->mi_cols
    Tune tune;                           // scs->static_config.tune
    bool blkLambdaTuning;                // ppcs->blk_lambda_tuning
    bool sbSize128;                      // scs->seq_header.sb_size == BLOCK_128X128
    const double* tplSbRdmultScalingFactors;   // pa_me_data->tpl_sb_rdmult_scaling_factors
    const double* ssimRdmultScalingFactors;    // pa_me_data->ssim_rdmult_scaling_factors
    std::uint32_t picFullLambda[2];      // ctx->ed_ctx->pic_full_lambda
    std::uint32_t picFastLambda[2];      // ctx->ed_ctx->pic_fast_lambda
};

// The ctx lambda surface both writers mutate.
struct MdLambdaCarrier {
    std::uint32_t fullLambdaMd[2];  // ctx->full_lambda_md[EB_8_BIT_MD / EB_10_BIT_MD]
    std::uint32_t fastLambdaMd[2];  // ctx->fast_lambda_md
};

// get_superblock_tpl_column_end (mode_decision.c:4115-4129): the ppcs wall
// threads as the two reads (sb_size == 128 flag, superres_denom).
std::int32_t getSuperblockTplColumnEnd(const SbLambdaPcs* ppcs, std::int32_t miCol, std::int32_t numMiW);

// aom_av1_set_ssim_rdmult (mode_decision.c:4131-4174). Deviation, named:
// the bsize travels as the enum (the reference reads ctx->blk_geom->bsize).
void aomAv1SetSsimRdmult(MdLambdaCarrier* ctx, const SbLambdaPcs* pcs, BlockSize bsize, std::int32_t miRow,
                         std::int32_t miCol);

// svt_aom_set_tuned_blk_lambda (mode_decision.c:4176-4236): bsize from the
// reference's ctx->blk_geom->bsize; blk_org_y/x -> mi_row/mi_col = >> 2.
void setTunedBlkLambda(MdLambdaCarrier* ctx, const SbLambdaPcs* pcs, BlockSize bsize, std::int32_t blkOrgX,
                       std::int32_t blkOrgY);

// ---------------------------------------------------------------------------
// A4M-10: the inter candidate-INJECTION surface (the inter search's
// definition). The candidate carry: exactly the fields the injectors write,
// each cited to its write site (the AN17 struct-carry precedent); Mv
// travels as its packed as_int form (the RE3 precedent).
// ---------------------------------------------------------------------------
struct InterInjectedCandidate {
    std::int32_t mode;        // block_mi.mode
    std::int32_t motionMode;  // block_mi.motion_mode
    bool skipModeAllowed;     // cand->skip_mode_allowed
    std::uint8_t drlIndex;    // cand->drl_index
    std::int32_t refFrame[2]; // block_mi.ref_frame[0/1]
    std::int32_t mv[2];       // block_mi.mv[0/1].as_int
    std::int32_t predMv[2];   // cand->pred_mv[0/1].as_int
    std::int32_t numProjRef;  // block_mi.num_proj_ref (the wm_sample_info wall)
    std::int32_t useIntrabc;  // block_mi.use_intrabc
    std::int32_t isInterintraUsed;  // block_mi.is_interintra_used
    std::int32_t compGroupIdx;      // block_mi.comp_group_idx
    std::int32_t compoundIdx;       // block_mi.compound_idx
    std::int32_t compoundType;      // block_mi.interinter_comp.type (CompoundType)
    std::int32_t transformType;     // cand->transform_type[0]
    std::int32_t transformTypeUv;   // cand->transform_type_uv
};

// inject_mvp_candidates_ii_light_pd1 (mode_decision.c:1366-1509). The walls
// thread per the campaign pattern: ref_mv_stack / ref_mv_count /
// ref_frame_type_arr / tot_ref_frame_types / max_can_count / the wm_sample_
// info num column / the near_count_ctrls trio / the frm_hdr skip-mode
// params (skip_mode_flag + the two ref_frame_idx slots) / the lossless-
// segment fold (the pinned CONFIG=0 -> the test passes false) / the
// lpd1_mvp_best_me_list flag + the me_results wall / the injected-mv
// carrier (the landed MD1 InjectedMvCarrier).
void injectMvpCandidatesIiLightPd1(InterInjectedCandidate* candArray, std::uint32_t* candTotCnt,
                                    std::uint32_t maxCanCount,
                                    const CandidateMv refMvStack[MODE_CTX_REF_FRAMES_MV][MAX_REF_MV_STACK_SIZE_MD],
                                    const std::uint8_t* refMvCount, const MvReferenceFrame* refFrameTypeArr,
                                    std::uint32_t totRefFrameTypes, bool allowBipred,
                                    const std::uint8_t* wmSampleInfoNum, std::uint8_t lpd1MvpBestMeList,
                                    const MeSbResults* meResults, std::uint32_t meBlockOffset,
                                    std::uint32_t meCandOffset, bool nearCountCtrlEnabled,
                                    std::uint8_t nearCount, std::uint8_t nearNearCount,
                                    std::uint8_t skipModeFlag, std::int32_t skipRefIdx0,
                                    std::int32_t skipRefIdx1, bool losslessSegment,
                                    InjectedMvCarrier& injected);

// av1_ref_frame_type (inter_prediction.h:447-459) over get_uni_comp_ref_idx
// (:428-441) and the comp_ref0/ref1 LUTs (:396-420). Module-boundary carry
// (the landed twin is l5 amvp.cpp:129-170; l16 cannot link l5).
std::int8_t av1RefFrameType(const std::int32_t* rf);

// The choose-best bundle: the ModeDecisionContext/md_rate_est_ctx fields
// svt_aom_choose_best_av1_mv_pred reads, bundled for the injector callers.
struct ChooseBestMvCtx {
    bool shutFastRate;                 // ctx->shut_fast_rate
    std::uint8_t approxInterRate;      // ctx->approx_inter_rate
    const std::int32_t* nmvVecCost;    // md_rate_est_ctx->nmv_vec_cost
    const std::int32_t* const* nmvCostStack;  // md_rate_est_ctx->nmvcoststack
    const std::int32_t (*drlModeFacBits)[2];  // md_rate_est_ctx->drl_mode_fac_bits[3][2]
};

// inject_new_candidates_light_pd1 (mode_decision.c:2410-2515). Walls
// threaded: me_results + the two offsets, ctx->sb_me_mv as two row
// pointers, the A4M-8 choose-best bundle, the wm_sample_info num column,
// reduce_unipred_candidates, allow_bipred, the is_intra_bordered x
// use_neighbouring_mode_ctrls.enabled conjunction, corrupted_mv_check, and
// the landed InjectedMvCarrier.
void injectNewCandidatesLightPd1(InterInjectedCandidate* candArray, std::uint32_t* candTotCnt,
                                  std::uint32_t maxCanCount, const MeSbResults* meResults,
                                  std::uint32_t meBlockOffset, std::uint32_t meCandOffset,
                                  const Mv* const sbMeMv[2],
                                  const CandidateMv refMvStack[MODE_CTX_REF_FRAMES_MV][MAX_REF_MV_STACK_SIZE_MD],
                                  const std::uint8_t* refMvCount, const ChooseBestMvCtx& chooseBest,
                                  const std::uint8_t* wmSampleInfoNum, std::uint8_t reduceUnipredCandidates,
                                  bool allowBipred, bool isIntraBordered, bool useNeighbouringEnabled,
                                  bool corruptedMvCheck, InjectedMvCarrier& injected);

// get_ref_frame_idx (inter_prediction.h:511-515) over ref_type_to_ref_idx
// (:511, 8 rows {0,0,1,2,3,0,1,2}). Module-boundary carry.
std::uint8_t getRefFrameIdx(MvReferenceFrame refType);

// The nested-injector wall counters (the inj_non_simple_modes / inj_comp_
// modes calls the pme injector makes thread as counts - the walls are
// named, their call sites pinned).
struct InjectorWallCalls {
    std::uint32_t nonSimpleModeCalls;  // the :2802 inj_non_simple_modes wall
    std::uint32_t compModeCalls;       // the :2847 inj_comp_modes wall
};

// inject_pme_candidates (mode_decision.c:2761-2859). Walls threaded: the
// ref_frame_type_arr + count, ctx->valid_pme_mv / ctx->best_pme_mv as the
// two [2][7] arrays, the A4M-8 choose-best bundle, the wm_sample_info num
// column, corrupted_mv_check, inter_comp_ctrls.do_pme, the landed
// InjectedMvCarrier, and the InjectorWallCalls counters. The
// determine_compound_mode(MD_COMP_AVG) call (:2841) runs through the landed
// A4M-3 twin on a temp carrier merged into the candidate.
void injectPmeCandidates(InterInjectedCandidate* candArray, std::uint32_t* candTotCnt,
                          std::uint32_t maxCanCount, const MvReferenceFrame* refFrameTypeArr,
                          std::uint32_t totRefFrameTypes, const std::uint8_t validPmeMv[2][REFS_PER_FRAME_MV],
                          const Mv bestPmeMv[2][REFS_PER_FRAME_MV], bool allowBipred,
                          const CandidateMv refMvStack[MODE_CTX_REF_FRAMES_MV][MAX_REF_MV_STACK_SIZE_MD],
                          const std::uint8_t* refMvCount, const ChooseBestMvCtx& chooseBest,
                          const std::uint8_t* wmSampleInfoNum, bool corruptedMvCheck, bool doPme,
                          InjectedMvCarrier& injected, InjectorWallCalls& wallCalls);

// inject_sframe_backup_candidate (mode_decision.c:3507-3539): the zero-MV
// BWD candidate; NO injected-list bookkeeping (the reference writes none).
void injectSframeBackupCandidate(InterInjectedCandidate* candArray, std::uint32_t* candTotCnt,
                                  std::uint32_t maxCanCount,
                                  const CandidateMv refMvStack[MODE_CTX_REF_FRAMES_MV][MAX_REF_MV_STACK_SIZE_MD],
                                  const std::uint8_t* refMvCount, const ChooseBestMvCtx& chooseBest,
                                  const std::uint8_t* wmSampleInfoNum, bool corruptedMvCheck);

// ---------------------------------------------------------------------------
// A4M-13: the pd0 injection pair.
// ---------------------------------------------------------------------------

// Pd0Level (definitions.h:741-750, verbatim).
enum Pd0Level {
    PD0_LVL_0_MD = 0,
    PD0_LVL_1_MD,
    PD0_LVL_2_MD,
    PD0_LVL_3_MD,
    PD0_LVL_4_MD,
    PD0_LVL_5_MD,
    PD0_LVL_6_MD,  // Lightest PD0 path, doesn't perform TX
    PD0_LEVELS_MD,
};

// ReferenceMode (definitions.h:1268-1273 verbatim order, the inter injector's
// frm_hdr->reference_mode).
enum ReferenceMode {
    SINGLE_REFERENCE = 0,
    COMPOUND_REFERENCE = 1,
    REFERENCE_MODE_SELECT = 2,
    REFERENCE_MODES_MD = 3,
};

// The MeSbResults surface the pd0 injector reads: the landed MD1 carrier
// carries the candidate side; this one adds the me_mv_array wall
// (md_process.h MeSbResults.me_mv_array).
struct MeSbResultsPd0 {
    const std::uint8_t* totalMeCandidateIndex;
    const MeCandidate* meCandidateArray;
    const Mv* meMvArray;
};

// inject_new_candidates_pd0 (mode_decision.c:2331-2408): the raw ME rows
// (no choose-best, no injected bookkeeping), the <<3 sub-pel lift, the
// pd0_level == 6 BI gate and the cand_total_cnt > 2 break.
void injectNewCandidatesPd0(InterInjectedCandidate* candArray, std::uint32_t* candTotCnt,
                             std::uint32_t maxCanCount, const MeSbResultsPd0* meResults,
                             std::uint32_t meBlockOffset, std::uint32_t meCandOffset, std::uint8_t maxRefs,
                             std::uint8_t maxL0, Pd0Level pd0Level, bool allowBipred);

// inject_inter_candidates_pd0 (mode_decision.c:2861-2871): the allow_bipred
// derivation (CONFIG_ENABLE_INTER_COMPOUND is live = 1) + the single
// inject_new_candidates_pd0 call.
void injectInterCandidatesPd0(InterInjectedCandidate* candArray, std::uint32_t* candTotCnt,
                               std::uint32_t maxCanCount, const MeSbResultsPd0* meResults,
                               std::uint32_t meBlockOffset, std::uint32_t meCandOffset, std::uint8_t maxRefs,
                               std::uint8_t maxL0, Pd0Level pd0Level, ReferenceMode referenceMode,
                               BlockSize bsize);

}  // namespace l16_rdo
