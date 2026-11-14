// l16_rdo A4M - the mode_decision.c remainder surface.
// GREEN step lands per nano-slice; see md_slices2.h for the span map. Every
// body is a verbatim transcription of its cited reference span with the
// ModeDecisionContext/pcs walls threaded as carrier structs.
#include "md_slices2.h"
#include "md_slices.h"

#include <cmath>

namespace l16_rdo {

// mv_check_bounds (mode_decision.c:3000-3003, verbatim).
std::int32_t mvCheckBounds(const MvLimits* mv_limits, const Mv* mv) {
    return (mv->y >> 3) < mv_limits->row_min || (mv->y >> 3) > mv_limits->row_max ||
        (mv->x >> 3) < mv_limits->col_min || (mv->x >> 3) > mv_limits->col_max;
}

// derive_ssim_threshold_factor_for_full_md (mode_decision.c:3875-3877,
// verbatim; the scs->input_resolution wall threads as the enum parameter).
double deriveSsimThresholdFactorForFullMd(ResolutionRange inputResolution) {
    return inputResolution >= INPUT_SIZE_1080p_RANGE ? 1.02 : 1.03;
}

// ---------------------------------------------------------------------------
// A4M-2: the reference-frame type surface.
// ---------------------------------------------------------------------------

// inter_prediction.h:461-485 ref_frame_map (TOTAL_COMP_REFS = 21 pairs),
// verbatim (module-boundary carry; the landed twin is l5 amvp.cpp:173-180).
static const MvReferenceFrame kRefFrameMap[TOTAL_COMP_REFS_MV][2] = {
    {LAST_FRAME_MV, BWDREF_FRAME_MV},    {LAST2_FRAME_MV, BWDREF_FRAME_MV},
    {LAST3_FRAME_MV, BWDREF_FRAME_MV},   {GOLDEN_FRAME_MV, BWDREF_FRAME_MV},
    {LAST_FRAME_MV, ALTREF2_FRAME_MV},   {LAST2_FRAME_MV, ALTREF2_FRAME_MV},
    {LAST3_FRAME_MV, ALTREF2_FRAME_MV},  {GOLDEN_FRAME_MV, ALTREF2_FRAME_MV},
    {LAST_FRAME_MV, ALTREF_FRAME_MV},    {LAST2_FRAME_MV, ALTREF_FRAME_MV},
    {LAST3_FRAME_MV, ALTREF_FRAME_MV},   {GOLDEN_FRAME_MV, ALTREF_FRAME_MV},
    {LAST_FRAME_MV, LAST2_FRAME_MV},     {LAST_FRAME_MV, LAST3_FRAME_MV},
    {LAST_FRAME_MV, GOLDEN_FRAME_MV},    {BWDREF_FRAME_MV, ALTREF_FRAME_MV},
    {LAST2_FRAME_MV, LAST3_FRAME_MV},    {LAST2_FRAME_MV, GOLDEN_FRAME_MV},
    {LAST3_FRAME_MV, GOLDEN_FRAME_MV},   {BWDREF_FRAME_MV, ALTREF2_FRAME_MV},
    {ALTREF2_FRAME_MV, ALTREF_FRAME_MV}};

// av1_set_ref_frame (inter_prediction.h:487-496, verbatim; the assert is
// NDEBUG-dropped).
void av1SetRefFrame(MvReferenceFrame* rf, std::int8_t refFrameType) {
    if (refFrameType >= TOTAL_REFS_PER_FRAME_MV) {
        rf[0] = kRefFrameMap[refFrameType - TOTAL_REFS_PER_FRAME_MV][0];
        rf[1] = kRefFrameMap[refFrameType - TOTAL_REFS_PER_FRAME_MV][1];
    } else {
        rf[0] = refFrameType;
        rf[1] = NONE_FRAME_MV;
    }
}

// valid_ref_frame_type (mode_decision.c:3489-3504, verbatim).
bool validRefFrameType(const MvReferenceFrame rf[2], const MvReferenceFrame* refFrameTypeArr,
                       std::uint8_t totRefFrameTypes) {
    // INTRA_FRAME is added in candidates sometimes, skip validation
    if (rf[0] == INTRA_FRAME_MV) {
        return true;
    }

    for (std::uint8_t i = 0; i < totRefFrameTypes; i++) {
        MvReferenceFrame rfInArr[2];
        av1SetRefFrame(rfInArr, refFrameTypeArr[i]);
        if (rf[0] == rfInArr[0] && rf[1] == rfInArr[1]) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// A4M-3: the compound-mode surface.
// ---------------------------------------------------------------------------

// The wedge_params_lookup bits column (inter_prediction.c:2001-2024,
// transcribed via the landed l17 twin inter_prediction.cpp:354-377); rows in
// the rate_est.h BlockSize order. Wedge is signalable at 8x8..32x32 and the
// 8x32/32x8 rects only.
const std::int32_t wedgeParamsBits[BLOCK_SIZES_ALL] = {
    0,  // BLOCK_4X4
    0,  // BLOCK_4X8
    0,  // BLOCK_8X4
    4,  // BLOCK_8X8
    4,  // BLOCK_8X16
    4,  // BLOCK_16X8
    4,  // BLOCK_16X16
    4,  // BLOCK_16X32
    4,  // BLOCK_32X16
    4,  // BLOCK_32X32
    0,  // BLOCK_32X64
    0,  // BLOCK_64X32
    0,  // BLOCK_64X64
    0,  // BLOCK_64X128
    0,  // BLOCK_128X64
    0,  // BLOCK_128X128
    0,  // BLOCK_4X16
    0,  // BLOCK_16X4
    4,  // BLOCK_8X32
    4,  // BLOCK_32X8
    0,  // BLOCK_16X64
    0,  // BLOCK_64X16
};

// get_tot_comp_types_bsize (mode_decision.c:124-126, verbatim).
std::int32_t getTotCompTypesBsize(std::int32_t totCompTypes, BlockSize bsize) {
    return (wedgeParamsBits[bsize] == 0) ? aomMin(totCompTypes, MD_COMP_WEDGE) : totCompTypes;
}

// to_av1_compound_lut (mode_decision.c:513, verbatim).
static const CompoundType toAv1CompoundLut[MD_COMP_TYPES] = {COMPOUND_AVERAGE, COMPOUND_DISTWTD,
                                                             COMPOUND_DIFFWTD, COMPOUND_WEDGE};

// determine_compound_mode (mode_decision.c:515-552, verbatim over the
// carrier; the search wall threads as the flag).
void determineCompoundMode(CompoundModeCarrier* blockMi, MdCompType curType) {
    blockMi->interinterCompType = toAv1CompoundLut[curType];  // :522
    switch (curType) {
    case MD_COMP_AVG:
        blockMi->compGroupIdx = 0;  // :525
        blockMi->compoundIdx = 1;   // :526
        break;
    case MD_COMP_DIST:
        blockMi->compGroupIdx = 0;  // :529
        blockMi->compoundIdx = 0;   // :530
        break;
    case MD_COMP_DIFF0:
        blockMi->compGroupIdx = 1;            // :533
        blockMi->compoundIdx = 1;             // :534
        blockMi->maskType = 55;                 // :535 (block_mi.interinter_comp.mask_type)
        blockMi->searchDiffWedgeCalled = true;  // :537 the wall (CONFIG live)
        break;
    case MD_COMP_WEDGE:
        blockMi->compGroupIdx = 1;              // :541
        blockMi->compoundIdx = 1;               // :542
        blockMi->searchDiffWedgeCalled = true;  // :544 the wall (CONFIG live)
        break;
    default:
        // SVT_ERROR("not used comp type\n") + assert(0) (:548-549) -
        // NDEBUG-dropped convention.
        break;
    }
}

// ---------------------------------------------------------------------------
// A4M-4: the OBMC motion-mode surface.
// ---------------------------------------------------------------------------

// is_motion_variation_allowed_bsize (inter_prediction.h:378-380, verbatim).
bool isMotionVariationAllowedBsize(BlockSize bsize) {
    return (blockSizeWide[bsize] >= 8 && blockSizeHigh[bsize] >= 8);
}

// is_inter_singleref_mode (definitions.h:1610-1612, verbatim).
std::int32_t isInterSinglerefMode(PredictionMode mode) {
    return mode >= SINGLE_INTER_MODE_START && mode < SINGLE_INTER_MODE_END;
}

// is_global_mv_block (inter_prediction.h:382-388, verbatim; the CONFIG_
// ENABLE_GLOBAL_MOTION guard is live at 1 in the pinned build, so the body
// is kept - the l7 inter_symbols.cpp:195-198 twin).
std::int32_t isGlobalMvBlock(PredictionMode mode, BlockSize bsize, TransformationType type) {
    return (mode == GLOBALMV || mode == GLOBAL_GLOBALMV) && type > TRANSLATION_TT &&
        isMotionVariationAllowedBsize(bsize);
}

// svt_aom_obmc_motion_mode_allowed (mode_decision.c:228-270, verbatim over
// the carriers; the :232-234 CONFIG early-out is dead under the pinned
// build and the :262 has_overlappable_candidates wall threads as the
// neighbor-count > 0 test).
MotionMode obmcMotionModeAllowed(const ObmcCtrlsCarrier& obmcCtrls, bool isMotionModeSwitchable,
                                 std::int32_t forceIntegerMv, TransformationType gmTypeRf0,
                                 std::uint32_t overlappableNeighbors, BlockSize bsize,
                                 std::uint8_t situation, MvReferenceFrame rf1, PredictionMode mode) {
    if (obmcCtrls.transFaceOff && !situation) {  // :235-237
        return SIMPLE_TRANSLATION;
    }
    // check if should cap the max block size for obmc (:239-243)
    if (blockSizeWide[bsize] > obmcCtrls.maxBlkSize || blockSizeHigh[bsize] > obmcCtrls.maxBlkSize) {
        return SIMPLE_TRANSLATION;
    }
    if (!obmcCtrls.enabled) {  // :244-246
        return SIMPLE_TRANSLATION;
    }
    if (!isMotionModeSwitchable) {  // :249-251 (frm_hdr->is_motion_mode_switchable)
        return SIMPLE_TRANSLATION;
    }
    if (forceIntegerMv == 0) {  // :253-258
        if (isGlobalMvBlock(mode, bsize, gmTypeRf0)) {
            return SIMPLE_TRANSLATION;
        }
    }
    if (isMotionVariationAllowedBsize(bsize) && isInterSinglerefMode(mode) &&
        rf1 != INTRA_FRAME_MV && !(rf1 > INTRA_FRAME_MV)) {  // :259-260
        if (!overlappableNeighbors) {  // :262 check_num_overlappable_neighbors
            return SIMPLE_TRANSLATION;
        }
        return OBMC_CAUSAL;  // :266
    } else {
        return SIMPLE_TRANSLATION;  // :268
    }
}

// ---------------------------------------------------------------------------
// A4M-5: skip_compound_on_ref_types.
// ---------------------------------------------------------------------------

// ref_type_to_list_idx (inter_prediction.h:505, verbatim).
static const std::uint8_t kRefTypeToListIdx[REFS_PER_FRAME_MV + 1] = {0, 0, 0, 0, 0, 1, 1, 1};

// get_list_idx (inter_prediction.h:507-509, verbatim).
std::uint8_t getListIdx(MvReferenceFrame refType) {
    return kRefTypeToListIdx[refType];
}

// is_inter_compound_mode (definitions.h:1603-1608, verbatim; the CONFIG_
// ENABLE_INTER_COMPOUND guard is live at 1 - the l7 inter_symbols.cpp twin).
std::int32_t isInterCompoundMode(PredictionMode mode) {
    return mode >= NEAREST_NEARESTMV && mode <= NEW_NEWMV;
}

// skip_compound_on_ref_types (mode_decision.c:1008-1048, verbatim over the
// carriers).
bool skipCompoundOnRefTypes(bool skipOnRefInfo, const SkipCompXdCarrier& xd, const MvReferenceFrame rf[2]) {
    if (!skipOnRefInfo) {  // :1009-1011 (ctx->inter_comp_ctrls.skip_on_ref_info)
        return false;
    }

    // If both references are from the same list, skip compound (:1015-1020)
    const std::uint8_t listIdx0 = getListIdx(rf[0]);
    const std::uint8_t listIdx1 = getListIdx(rf[1]);
    if (listIdx0 == listIdx1) {
        return true;
    }

    // Skip compound unless neighbours selected the ref frames (:1022-1026)
    bool skipComp = true;
    if (!xd.leftAvailable && !xd.upAvailable) {
        return false;
    }

    if (xd.leftAvailable) {  // :1028-1036
        const SkipCompNdMi* const leftMi = xd.leftMbmi;
        if ((isInterSinglerefMode(leftMi->mode) &&
             (leftMi->refFrame[0] == rf[0] || leftMi->refFrame[0] == rf[1])) ||
            (isInterCompoundMode(leftMi->mode) &&
             (leftMi->refFrame[0] == rf[0] && leftMi->refFrame[1] == rf[1]))) {
            return false;
        }
    }
    if (xd.upAvailable) {  // :1037-1045
        const SkipCompNdMi* const aboveMi = xd.aboveMbmi;
        if ((isInterSinglerefMode(aboveMi->mode) &&
             (aboveMi->refFrame[0] == rf[0] || aboveMi->refFrame[0] == rf[1])) ||
            (isInterCompoundMode(aboveMi->mode) &&
             (aboveMi->refFrame[0] == rf[0] && aboveMi->refFrame[1] == rf[1]))) {
            return false;
        }
    }

    return skipComp;  // :1047
}

// ---------------------------------------------------------------------------
// A4M-6: setup_pred_plane / svt_av1_setup_pred_block.
// ---------------------------------------------------------------------------

// setup_pred_plane (mode_decision.c:2051-2068, verbatim).
void setupPredPlane(Buf2D* dst, BlockSize bsize, std::uint8_t* src, std::int32_t width, std::int32_t height,
                    std::int32_t stride, std::int32_t miRow, std::int32_t miCol, std::int32_t subsamplingX,
                    std::int32_t subsamplingY) {
    // Offset the buffer pointer
    if (subsamplingY && (miRow & 0x01) && (miSizeHigh[bsize] == 1)) {
        miRow -= 1;
    }
    if (subsamplingX && (miCol & 0x01) && (miSizeWide[bsize] == 1)) {
        miCol -= 1;
    }

    const std::int32_t x = (MI_SIZE_MD * miCol) >> subsamplingX;
    const std::int32_t y = (MI_SIZE_MD * miRow) >> subsamplingY;
    dst->buf = src + (y * stride + x);  // scaled_buffer_offset(x, y, stride, scale)
    dst->buf0 = src;
    dst->width = width;
    dst->height = height;
    dst->stride = stride;
}

// svt_av1_setup_pred_block (mode_decision.c:2070-2080, verbatim).
void setupPredBlock(BlockSize bsize, Buf2D dst[MAX_PLANES_MD], const Yv12Lite* src, std::int32_t miRow,
                    std::int32_t miCol) {
    dst[0].buf = src->yBuffer;
    dst[0].stride = src->yStride;
    dst[1].buf = src->uBuffer;
    dst[2].buf = src->vBuffer;
    dst[1].stride = dst[2].stride = src->uvStride;

    setupPredPlane(dst, bsize, dst[0].buf, src->yCropWidth, src->yCropHeight, dst[0].stride, miRow, miCol, 0, 0);
}

// ---------------------------------------------------------------------------
// A4M-7: the variance-map + DC-only surface.
// ---------------------------------------------------------------------------

// var_log2_lut / var_grid_lut / var_base_lut (product_coding_loop.c
// :8054-8056, verbatim).
static const std::uint8_t kVarLog2Lut[4] = {6, 5, 4, 3};
static const std::uint8_t kVarGridLut[4] = {1, 2, 4, 8};
static const std::uint8_t kVarBaseLut[4] = {0, 1, 5, 21};

// get_msb (definitions.h:628-644, portable body; the file-local rate_est.cpp
// twin is not exported, so the l7-l13 duplication precedent applies).
static std::int32_t getMsbA4m(std::uint32_t n) {
    std::int32_t log = 0;
    std::uint32_t value = n;
    for (std::int32_t i = 4; i >= 0; --i) {
        const std::int32_t shift = (1 << i);
        const std::uint32_t x = value >> shift;
        if (x != 0) {
            value = x;
            log += shift;
        }
    }
    return log;
}

// svt_aom_get_blk_var_map (product_coding_loop.c:8058-8099, verbatim;
// svt_log2f == get_msb, definitions.h:592).
void getBlkVarMap(std::int32_t blockSize, std::int32_t orgX, std::int32_t orgY, std::int32_t* blkIdx,
                  std::int32_t subIdx[4]) {
    // Map block size to level: 64->0, 32->1, 16->2, 8->3
    const std::int32_t lvl = 6 - getMsbA4m(blockSize);  // svt_log2f == get_msb (definitions.h:592)

    // Valid range is block_size in [8, 64], i.e. lvl in [0, 3].
    // Anything outside that range has no variance data; return safe sentinel values.
    if (lvl < 0 || lvl > 3) {
        *blkIdx = 0;
        subIdx[0] = subIdx[1] = subIdx[2] = subIdx[3] = 0;
        return;
    }

    // Parent block
    const std::int32_t shift = kVarLog2Lut[lvl];
    const std::int32_t grid = kVarGridLut[lvl];
    const std::int32_t base = kVarBaseLut[lvl];

    const std::int32_t gx = orgX >> shift;
    const std::int32_t gy = orgY >> shift;

    *blkIdx = base + gy * grid + gx;

    // Sub-blocks: only valid when lvl < 3 (i.e. block_size >= 16).
    // For block_size == 8 (lvl == 3), sub_lvl would be 4 which is out of bounds.
    // Callers must not use sub_idx[] when block_size < 16.
    const std::int32_t subLvl = lvl + 1;
    if (subLvl < 4) {
        const std::int32_t subShift = kVarLog2Lut[subLvl];
        const std::int32_t subBase = kVarBaseLut[subLvl];
        const std::int32_t subGrid = kVarGridLut[subLvl];

        const std::int32_t sx = orgX >> subShift;
        const std::int32_t sy = orgY >> subShift;

        subIdx[0] = subBase + (sy + 0) * subGrid + (sx + 0);
        subIdx[1] = subBase + (sy + 0) * subGrid + (sx + 1);
        subIdx[2] = subBase + (sy + 1) * subGrid + (sx + 0);
        subIdx[3] = subBase + (sy + 1) * subGrid + (sx + 1);
    } else {
        subIdx[0] = subIdx[1] = subIdx[2] = subIdx[3] = 0;
    }
}

// is_dc_only_safe (mode_decision.c:847-881, verbatim over the carrier;
// the :858 MIN/MAX folds inline, the :849-852 asserts NDEBUG-dropped).
std::int32_t isDcOnlySafe(const DcOnlyCarrier& c) {
    // Early exit if pruning not enabled, SB-128, NSQ, or 4x4 (no variance available)
    if (!c.pruneUsingEdgeInfo || c.superBlockSize == 128 || c.shape != PART_N_MD || c.sqSize == 4) {
        return 0;
    }

    // Block variance lookup
    std::int32_t blkIdx;
    std::int32_t subIdx[4];
    const std::int32_t blkOrgX = (std::int32_t)(c.blkOrgX - c.sbOriginX);  // :857 Position blk_org
    const std::int32_t blkOrgY = (std::int32_t)(c.blkOrgY - c.sbOriginY);
    getBlkVarMap(c.sqSize, blkOrgX, blkOrgY, &blkIdx, subIdx);

    const std::uint16_t* sbVar = c.sbVar;  // :860 ppcs->variance[ctx->sb_index]
    const std::uint32_t blkVar = sbVar[blkIdx];

    // For 8x8, we do not have 4x4 sub-variance, skip spread check
    if (c.sqSize == 8) {
        return (blkVar < 2000);
    }

    // For 16x16 and above, compute spread from sub-blocks
    std::uint32_t minVar = 0xFFFFFFFFu;  // UINT32_MAX
    std::uint32_t maxVar = 0;

    for (int i = 0; i < 4; i++) {
        const std::uint32_t v = sbVar[subIdx[i]];
        minVar = v < minVar ? v : minVar;  // MIN (:874)
        maxVar = v > maxVar ? v : maxVar;  // MAX (:875)
    }

    const std::uint32_t spreadVar = maxVar - minVar;

    return (blkVar < 2000 && spreadVar < 4000);
}

// ---------------------------------------------------------------------------
// A4M-8: choose_best_av1_mv_pred + its three carries.
// ---------------------------------------------------------------------------

// svt_av1_mv_bit_cost_light (rd_cost.c:62-68, verbatim).
std::int32_t mvBitCostLight(const Mv* mv, const Mv* ref) {
    const std::uint32_t factor = 50;
    const std::uint32_t absmvdiffx = mv->x > ref->x ? (std::uint32_t)(mv->x - ref->x)
                                                    : (std::uint32_t)(ref->x - mv->x);  // ABS
    const std::uint32_t absmvdiffy = mv->y > ref->y ? (std::uint32_t)(mv->y - ref->y)
                                                    : (std::uint32_t)(ref->y - mv->y);
    const std::uint32_t mv_rate = 1296 + (factor * (absmvdiffx + absmvdiffy));
    return mv_rate;
}

// av1_drl_ctx (rd_cost.h:85-89, verbatim).
std::uint8_t av1DrlCtx(const CandidateMv* refMvStack, std::int32_t refIdx) {
    return refMvStack[refIdx].weight >= REF_CAT_LEVEL_MD
        ? refMvStack[refIdx + 1].weight >= REF_CAT_LEVEL_MD ? 0 : 1
        : refMvStack[refIdx + 1].weight < REF_CAT_LEVEL_MD ? 2 : 0;
}

// compound_ref0_mode (inter_prediction.h:312-343, the LUT verbatim; the
// asserts are fixture-pinned by the compound-domain test).
PredictionMode compoundRef0Mode(PredictionMode mode) {
    static const PredictionMode lut[MB_MODE_COUNT] = {
        MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT,
        MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT,
        MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT,
        NEARESTMV,     // NEAREST_NEARESTMV
        NEARMV,        // NEAR_NEARMV
        NEARESTMV,     // NEAREST_NEWMV
        NEWMV,         // NEW_NEARESTMV
        NEARMV,        // NEAR_NEWMV
        NEWMV,         // NEW_NEARMV
        GLOBALMV,      // GLOBAL_GLOBALMV
        NEWMV,         // NEW_NEWMV
    };
    return lut[mode];
}

// compound_ref1_mode (inter_prediction.h:345-376, the LUT verbatim).
PredictionMode compoundRef1Mode(PredictionMode mode) {
    static const PredictionMode lut[MB_MODE_COUNT] = {
        MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT,
        MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT,
        MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT,
        NEARESTMV,     // NEAREST_NEARESTMV
        NEARMV,        // NEAR_NEARMV
        NEWMV,         // NEAREST_NEWMV
        NEARESTMV,     // NEW_NEARESTMV
        NEWMV,         // NEAR_NEWMV
        NEARMV,        // NEW_NEARMV
        GLOBALMV,      // GLOBAL_GLOBALMV
        NEWMV,         // NEW_NEWMV
    };
    return lut[mode];
}

// svt_aom_get_av1_mv_pred_drl (adaptive_mv_pred.c:1407-1457, verbatim over
// the two wall reads).
void getAv1MvPredDrl(const CandidateMv refMvStack[MODE_CTX_REF_FRAMES_MV][MAX_REF_MV_STACK_SIZE_MD],
                     const std::uint8_t* refMvCount, MvReferenceFrame refFrame, std::uint8_t isCompound,
                     PredictionMode mode, std::uint8_t drlIndex, Mv nearestmv[2], Mv nearmv[2], Mv refMv[2]) {
    if (!isCompound && mode != GLOBALMV) {  // :1413-1417
        nearestmv[0] = refMvStack[refFrame][0].this_mv;
        nearmv[0] = refMvStack[refFrame][1].this_mv;
    }

    if (isCompound && mode != GLOBAL_GLOBALMV) {  // :1419-1424
        const std::int32_t refMvIdx = drlIndex + 1;
        nearestmv[0] = refMvStack[refFrame][0].this_mv;
        nearestmv[1] = refMvStack[refFrame][0].comp_mv;
        nearmv[0] = refMvStack[refFrame][refMvIdx].this_mv;
        nearmv[1] = refMvStack[refFrame][refMvIdx].comp_mv;
    } else if (drlIndex > 0 && mode == NEARMV) {  // :1425-1429
        const Mv curMv = refMvStack[refFrame][1 + drlIndex].this_mv;
        nearmv[0] = curMv;
    }

    refMv[0] = nearestmv[0];  // :1431-1432
    refMv[1] = nearestmv[1];

    if (isCompound) {  // :1434-1449
        std::int32_t refMvIdx = drlIndex;
        // Special case: NEAR_NEWMV and NEW_NEARMV modes use
        // 1 + mbmi->ref_mv_idx (like NEARMV) instead of
        // mbmi->ref_mv_idx (like NEWMV)
        if (mode == NEAR_NEWMV || mode == NEW_NEARMV) {
            refMvIdx = 1 + drlIndex;
        }

        if (compoundRef0Mode(mode) == NEWMV) {
            refMv[0] = refMvStack[refFrame][refMvIdx].this_mv;
        }

        if (compoundRef1Mode(mode) == NEWMV) {
            refMv[1] = refMvStack[refFrame][refMvIdx].comp_mv;
        }
    } else {
        if (mode == NEWMV) {  // :1451-1455
            if (refMvCount[refFrame] > 1) {
                refMv[0] = refMvStack[refFrame][drlIndex].this_mv;
            }
        }
    }
}

// svt_aom_choose_best_av1_mv_pred (mode_decision.c:554-645, verbatim over
// the carriers).
void chooseBestAv1MvPred(const CandidateMv refMvStack[MODE_CTX_REF_FRAMES_MV][MAX_REF_MV_STACK_SIZE_MD],
                         const std::uint8_t* refMvCount, bool shutFastRate, std::uint8_t approxInterRate,
                         const std::int32_t* nmvVecCost, const std::int32_t* const* nmvCostStack,
                         const std::int32_t drlModeFacBits[DRL_MODE_CONTEXTS_MD][2],
                         MvReferenceFrame refFrame, PredictionMode mode, Mv mv0, Mv mv1,
                         std::uint8_t* bestDrlIndex, Mv bestPredMv[2]) {
    if (shutFastRate) {  // :560-562
        return;
    }
    if (approxInterRate > 1) {  // :563-568
        *bestDrlIndex = 0;
        bestPredMv[0] = refMvStack[refFrame][0].this_mv;
        bestPredMv[1] = refMvStack[refFrame][0].comp_mv;
        return;
    }
    const std::int16_t mv0x = mv0.x;
    const std::int16_t mv0y = mv0.y;
    const std::int16_t mv1x = mv1.x;
    const std::int16_t mv1y = mv1.y;

    const std::uint8_t isCompound = (std::uint8_t)isInterCompoundMode(mode);  // :574

    std::uint8_t maxDrlIndex;
    Mv nearestmv[2] = {{{0}}, {{0}}};
    Mv nearmv[2];
    Mv refMv[2];
    Mv mv;

    maxDrlIndex = getMaxDrlIndex(refMvCount[refFrame], mode);  // :584

    if (maxDrlIndex == 1) {  // :587-591
        *bestDrlIndex = 0;
        bestPredMv[0] = refMvStack[refFrame][0].this_mv;
        bestPredMv[1] = refMvStack[refFrame][0].comp_mv;
    } else {
        std::uint32_t bestMvCost = 0xFFFFFFFF;
        for (std::uint8_t drli = 0; drli < maxDrlIndex; drli++) {  // :595-643
            getAv1MvPredDrl(refMvStack, refMvCount, refFrame, isCompound, mode, drli, nearestmv, nearmv, refMv);

            // compute the rate for this drli Cand
            mv.y = mv0y;  // :599-600
            mv.x = mv0x;
            std::uint32_t mvRate = 0;
            if (approxInterRate) {
                mvRate = (std::uint32_t)mvBitCostLight(&mv, &refMv[0]);
            } else {
                mvRate = (std::uint32_t)mvBitCost((std::int32_t)mv.as_int, (std::int32_t)refMv[0].as_int,
                                                  nmvVecCost, nmvCostStack, MV_COST_WEIGHT);
            }

            if (isCompound) {  // :609-621
                mv.y = mv1y;
                mv.x = mv1x;
                if (approxInterRate) {
                    mvRate += (std::uint32_t)mvBitCostLight(&mv, &refMv[1]);
                } else {
                    mvRate += (std::uint32_t)mvBitCost((std::int32_t)mv.as_int, (std::int32_t)refMv[1].as_int,
                                                       nmvVecCost, nmvCostStack, MV_COST_WEIGHT);
                }
            }

            const std::int32_t newMv = (mode == NEWMV || mode == NEW_NEWMV);  // :623
            if (newMv) {
                for (std::int32_t idx = 0; idx < 2; ++idx) {  // :626-635
                    if (refMvCount[refFrame] > idx + 1) {
                        const std::uint8_t drl1Ctx = av1DrlCtx(&refMvStack[refFrame][0], idx);
                        mvRate += drlModeFacBits[drl1Ctx][drli != idx];
                        if (drli == idx) {
                            break;
                        }
                    }
                }
            }

            if (mvRate < bestMvCost) {  // :637-642
                bestMvCost = mvRate;
                *bestDrlIndex = drli;
                bestPredMv[0] = refMv[0];
                bestPredMv[1] = refMv[1];
            }
        }
    }
}

// ---------------------------------------------------------------------------
// A4M-9: the TPL/SSIM lambda-tuning surface.
// ---------------------------------------------------------------------------

// coded_to_superres_mi (resize.h:71-73, verbatim).
std::int32_t codedToSuperresMi(std::int32_t miCol, std::int32_t denom) {
    return (miCol * denom + 8 / 2) / 8;  // SCALE_NUMERATOR = 8
}

// get_superblock_tpl_column_end (mode_decision.c:4115-4129, verbatim over
// the carrier; miSizeWide is the landed rate_est table).
std::int32_t getSuperblockTplColumnEnd(const SbLambdaPcs* ppcs, std::int32_t miCol, std::int32_t numMiW) {
    const std::int32_t mibSizeLog2 = ppcs->sbSize128 ? 5 : 4;  // :4116
    // Find the start column of this superblock.
    const std::int32_t sbMiColStart = (miCol >> mibSizeLog2) << mibSizeLog2;  // :4118
    // Same but in superres upscaled dimension.
    const std::int32_t sbMiColStartSr = codedToSuperresMi(sbMiColStart, ppcs->superresDenom);  // :4120
    // Width of this superblock in mi units.
    const std::int32_t sbMiWidth = miSizeWide[ppcs->sbSize128 ? BLOCK_128X128 : BLOCK_64X64];  // :4122
    // Same but in superres upscaled dimension.
    const std::int32_t sbMiWidthSr = codedToSuperresMi(sbMiWidth, ppcs->superresDenom);  // :4124
    // Superblock end in mi units.
    const std::int32_t sbMiEnd = sbMiColStartSr + sbMiWidthSr;  // :4126
    // Superblock end in TPL units.
    return (sbMiEnd + numMiW - 1) / numMiW;  // :4128
}

// aom_av1_set_ssim_rdmult (mode_decision.c:4131-4174, verbatim over the
// carriers; the pow/exp are the libm calls the reference makes).
void aomAv1SetSsimRdmult(MdLambdaCarrier* ctx, const SbLambdaPcs* pcs, BlockSize bsize, std::int32_t miRow,
                         std::int32_t miCol) {
    const std::int32_t bsizeBase = BLOCK_16X16;  // :4135
    const std::int32_t numMiW = miSizeWide[bsizeBase];
    const std::int32_t numMiH = miSizeHigh[bsizeBase];
    const std::int32_t numCols = (pcs->miCols + numMiW - 1) / numMiW;
    const std::int32_t numRows = (pcs->miRows + numMiH - 1) / numMiH;
    const std::int32_t numBcols = (miSizeWide[bsize] + numMiW - 1) / numMiW;
    const std::int32_t numBrows = (miSizeHigh[bsize] + numMiH - 1) / numMiH;
    std::int32_t row, col;
    double numOfMi = 0.0;
    double geomMeanOfScale = 1.0;
    for (row = miRow / numMiW; row < numRows && row < miRow / numMiW + numBrows; ++row) {  // :4145
        for (col = miCol / numMiH; col < numCols && col < miCol / numMiH + numBcols; ++col) {  // :4146
            const std::int32_t index = row * numCols + col;
            geomMeanOfScale *= pcs->ssimRdmultScalingFactors[index];
            numOfMi += 1.0;
        }
    }
    geomMeanOfScale = std::pow(geomMeanOfScale, (1.0 / numOfMi));  // :4152
    if (!pcs->blkLambdaTuning) {
        ctx->fullLambdaMd[0] = (std::uint32_t)((double)pcs->picFullLambda[0] * geomMeanOfScale + 0.5);
        ctx->fullLambdaMd[1] = (std::uint32_t)((double)pcs->picFullLambda[1] * geomMeanOfScale + 0.5);
        ctx->fastLambdaMd[0] = (std::uint32_t)((double)pcs->picFastLambda[0] * geomMeanOfScale + 0.5);
        ctx->fastLambdaMd[1] = (std::uint32_t)((double)pcs->picFastLambda[1] * geomMeanOfScale + 0.5);
    } else {
        ctx->fullLambdaMd[0] = (std::uint32_t)((double)ctx->fullLambdaMd[0] * geomMeanOfScale + 0.5);
        ctx->fullLambdaMd[1] = (std::uint32_t)((double)ctx->fullLambdaMd[1] * geomMeanOfScale + 0.5);
        ctx->fastLambdaMd[0] = (std::uint32_t)((double)ctx->fastLambdaMd[0] * geomMeanOfScale + 0.5);
        ctx->fastLambdaMd[1] = (std::uint32_t)((double)ctx->fastLambdaMd[1] * geomMeanOfScale + 0.5);
    }
}

// svt_aom_set_tuned_blk_lambda (mode_decision.c:4176-4236, verbatim over
// the carriers).
void setTunedBlkLambda(MdLambdaCarrier* ctx, const SbLambdaPcs* pcs, BlockSize bsize, std::int32_t blkOrgX,
                       std::int32_t blkOrgY) {
    const std::int32_t miRow = blkOrgY / 4;  // :4181-4182 (ctx->blk_org_y / 4)
    const std::int32_t miCol = blkOrgX / 4;

    const std::int32_t miColSr = codedToSuperresMi(miCol, pcs->superresDenom);  // :4184
    const std::int32_t miColsSr = ((pcs->enhancedUnscaledWidth + 15) / 16) << 2;  // :4185
    const std::int32_t blockMiWidthSr = codedToSuperresMi(miSizeWide[bsize], pcs->superresDenom);  // :4186
    const std::int32_t bsizeBase = pcs->synthBlkSize == 32 ? BLOCK_32X32 : BLOCK_16X16;  // :4187
    const std::int32_t numMiW = miSizeWide[bsizeBase];
    const std::int32_t numMiH = miSizeHigh[bsizeBase];
    const std::int32_t numCols = (miColsSr + numMiW - 1) / numMiW;
    const std::int32_t numRows = (pcs->miRows + numMiH - 1) / numMiH;
    const std::int32_t numBcols = (blockMiWidthSr + numMiW - 1) / numMiW;
    const std::int32_t numBrows = (miSizeHigh[bsize] + numMiH - 1) / numMiH;

    // This is required because the end col of superblock may be off by 1 in case
    // of superres.
    const std::int32_t sbBcolEnd = getSuperblockTplColumnEnd(pcs, miCol, numMiW);  // :4197
    std::int32_t row, col;
    std::int32_t baseBlockCount = 0;
    double geomMeanOfScale = 0.0;
    for (row = miRow / numMiW; row < numRows && row < miRow / numMiW + numBrows; ++row) {  // :4201
        for (col = miColSr / numMiH; col < numCols && col < miColSr / numMiH + numBcols && col < sbBcolEnd;
             ++col) {  // :4202
            const std::int32_t index = row * numCols + col;
            geomMeanOfScale += std::log(pcs->tplSbRdmultScalingFactors[index]);
            ++baseBlockCount;
        }
    }
    // When superres is on, base_block_count could be zero.
    // This function's counterpart in AOM, av1_get_hier_tpl_rdmult, will encounter division by zero
    if (baseBlockCount == 0) {  // :4211-4219
        // return a large number to indicate invalid state
        ctx->fullLambdaMd[0] = SUPERRES_INVALID_STATE_MD;
        ctx->fullLambdaMd[1] = SUPERRES_INVALID_STATE_MD;
        ctx->fastLambdaMd[0] = SUPERRES_INVALID_STATE_MD;
        ctx->fastLambdaMd[1] = SUPERRES_INVALID_STATE_MD;
        return;
    }

    geomMeanOfScale = std::exp(geomMeanOfScale / baseBlockCount);  // :4221

    ctx->fullLambdaMd[0] = (std::uint32_t)((double)pcs->picFullLambda[0] * geomMeanOfScale + 0.5);
    ctx->fullLambdaMd[1] = (std::uint32_t)((double)pcs->picFullLambda[1] * geomMeanOfScale + 0.5);
    ctx->fastLambdaMd[0] = (std::uint32_t)((double)pcs->picFastLambda[0] * geomMeanOfScale + 0.5);
    ctx->fastLambdaMd[1] = (std::uint32_t)((double)pcs->picFastLambda[1] * geomMeanOfScale + 0.5);
    if (pcs->tune == TUNE_SSIM || pcs->tune == TUNE_IQ || pcs->tune == TUNE_MS_SSIM) {  // :4232-4235
        aomAv1SetSsimRdmult(ctx, pcs, bsize, miRow, miCol);
    }
}

// ---------------------------------------------------------------------------
// A4M-10: the inter candidate-INJECTION surface.
// ---------------------------------------------------------------------------

// INC_MD_CAND_CNT (mode_decision.c:61-68) - increments cnt only when
// cnt + 1 < max_can_count; else SVT_ERROR (the count is not incremented;
// the port's deviation: no console print). The file-local rate_est.cpp
// twin is not exported (the l7-l13 duplication precedent).
static inline void incMdCandCntA4m(std::uint32_t& cnt, std::uint32_t maxCanCount) {
    if (cnt + 1 < maxCanCount) {
        cnt++;
    }
}

// inject_mvp_candidates_ii_light_pd1 (mode_decision.c:1366-1509, verbatim
// over the carriers).
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
                                    InjectedMvCarrier& injected) {
    std::uint32_t candIdx = *candTotCnt;  // :1369
    InterInjectedCandidate* candArray_ = candArray;

    // all of ref pairs: (1)single-ref List0  (2)single-ref List1  (3)compound Bi-Dir List0-List1
    for (std::uint32_t refIt = 0; refIt < totRefFrameTypes; ++refIt) {  // :1374
        const MvReferenceFrame refPair = refFrameTypeArr[refIt];
        MvReferenceFrame rf[2];
        av1SetRefFrame(rf, (std::int8_t)refPair);

        // single ref/list
        if (rf[1] == NONE_FRAME_MV) {  // :1380
            const MvReferenceFrame frameType = rf[0];
            const std::uint8_t listIdx = getListIdx(rf[0]);
            if (lpd1MvpBestMeList) {  // :1383-1392
                const std::uint8_t totalMeCnt = meResults->totalMeCandidateIndex[meBlockOffset];
                const MeCandidate* meBlockResults = &meResults->meCandidateArray[meCandOffset];
                const MeCandidate* meBlockResultsPtr = &meBlockResults[0];
                const std::uint8_t interDirection = meBlockResultsPtr->direction;
                if (totalMeCnt && listIdx != interDirection) {
                    continue;
                }
            }
            // NEAREST
            // Don't check if MV is already injected b/c NEAREST is the first INTER MV injected
            Mv toInjMv;
            toInjMv.as_int = refMvStack[frameType][0].this_mv.as_int;  // :1395

            InterInjectedCandidate* cand = &candArray_[candIdx];  // :1397
            cand->mode = NEARESTMV;                    // :1398
            cand->motionMode = SIMPLE_TRANSLATION;     // :1399
            cand->skipModeAllowed = false;             // :1400
            cand->drlIndex = 0;                        // :1401
            cand->refFrame[0] = rf[0];                 // :1402
            cand->refFrame[1] = rf[1];                 // :1403
            cand->mv[0] = (std::int32_t)toInjMv.as_int;  // :1404
            cand->numProjRef = wmSampleInfoNum[frameType];  // :1405
            cand->useIntrabc = 0;                      // :1406
            cand->isInterintraUsed = 0;                // :1407
            incMdCandCntA4m(candIdx, maxCanCount);     // :1408

            injected.injectedMvs[injected.injectedMvCount][0] = (std::int32_t)toInjMv.as_int;  // :1410
            injected.injectedRefTypes[injected.injectedMvCount] = frameType;  // :1411
            ++injected.injectedMvCount;  // :1412
            // NEAR
            const std::uint8_t maxDrlIndex = getMaxDrlIndex(refMvCount[frameType], NEARMV);  // :1414
            std::uint8_t capMaxDrlIndex = 0;
            if (nearCountCtrlEnabled) {  // :1416-1418
                capMaxDrlIndex = (std::uint8_t)aomMin(nearCount, maxDrlIndex);
            }
            for (std::uint8_t drli = 0; drli < capMaxDrlIndex; drli++) {  // :1419
                toInjMv.as_int = refMvStack[frameType][1 + drli].this_mv.as_int;  // :1420

                if ((injected.injectedMvCount == 0 ||
                     mvIsAlreadyInjected(injected, (std::int32_t)toInjMv.as_int, (std::int32_t)toInjMv.as_int,
                                         (std::uint8_t)frameType) == false)) {  // :1422-1423
                    cand = &candArray_[candIdx];  // :1424
                    cand->mode = NEARMV;          // :1425
                    cand->motionMode = SIMPLE_TRANSLATION;  // :1426
                    cand->skipModeAllowed = false;  // :1427
                    cand->drlIndex = drli;        // :1428
                    cand->useIntrabc = 0;         // :1429
                    cand->isInterintraUsed = 0;   // :1430
                    cand->refFrame[0] = rf[0];    // :1431
                    cand->refFrame[1] = rf[1];    // :1432
                    cand->mv[0] = (std::int32_t)toInjMv.as_int;  // :1433
                    cand->numProjRef = wmSampleInfoNum[frameType];  // :1434
                    incMdCandCntA4m(candIdx, maxCanCount);  // :1435

                    injected.injectedMvs[injected.injectedMvCount][0] = (std::int32_t)toInjMv.as_int;  // :1437
                    injected.injectedRefTypes[injected.injectedMvCount] = frameType;  // :1438
                    ++injected.injectedMvCount;  // :1439
                }
            }
        } else if (allowBipred) {  // :1442
            // NEAREST_NEAREST
            // Don't check if MV is already injected b/c NEAREST_NEAREST is the first bipred INTER candidate injected
            Mv toInjMv0;
            toInjMv0.as_int = refMvStack[refPair][0].this_mv.as_int;  // :1445
            Mv toInjMv1;
            toInjMv1.as_int = refMvStack[refPair][0].comp_mv.as_int;  // :1446
            const bool isSkipMode = !losslessSegment && skipModeFlag &&  // :1447-1449
                (rf[0] == (MvReferenceFrame)skipRefIdx0) && (rf[1] == (MvReferenceFrame)skipRefIdx1);
            InterInjectedCandidate* cand = &candArray_[candIdx];  // :1450
            cand->mode = NEAREST_NEARESTMV;    // :1451
            cand->motionMode = SIMPLE_TRANSLATION;  // :1452
            cand->skipModeAllowed = isSkipMode;  // :1453
            cand->mv[0] = (std::int32_t)toInjMv0.as_int;  // :1454
            cand->mv[1] = (std::int32_t)toInjMv1.as_int;  // :1455
            cand->drlIndex = 0;                // :1456
            cand->useIntrabc = 0;              // :1457
            cand->isInterintraUsed = 0;        // :1458
            cand->refFrame[0] = rf[0];         // :1459
            cand->refFrame[1] = rf[1];         // :1460
            cand->compGroupIdx = 0;            // :1461
            cand->compoundIdx = 1;             // :1462
            cand->compoundType = COMPOUND_AVERAGE;  // :1463

            incMdCandCntA4m(candIdx, maxCanCount);  // :1465

            injected.injectedMvs[injected.injectedMvCount][0] = (std::int32_t)toInjMv0.as_int;  // :1467
            injected.injectedMvs[injected.injectedMvCount][1] = (std::int32_t)toInjMv1.as_int;  // :1468
            injected.injectedRefTypes[injected.injectedMvCount] = refPair;  // :1469
            ++injected.injectedMvCount;  // :1470

            // NEAR_NEAR
            const std::uint8_t maxDrlIndex = getMaxDrlIndex(refMvCount[refPair], NEAR_NEARMV);  // :1473
            std::uint8_t capMaxDrlIndex = 0;
            if (nearCountCtrlEnabled) {  // :1475-1477
                capMaxDrlIndex = (std::uint8_t)aomMin(nearNearCount, maxDrlIndex);
            }
            for (std::uint8_t drli = 0; drli < capMaxDrlIndex; drli++) {  // :1478
                toInjMv0.as_int = refMvStack[refPair][1 + drli].this_mv.as_int;  // :1479
                toInjMv1.as_int = refMvStack[refPair][1 + drli].comp_mv.as_int;  // :1480
                if ((injected.injectedMvCount == 0 ||
                     mvIsAlreadyInjected(injected, (std::int32_t)toInjMv0.as_int, (std::int32_t)toInjMv1.as_int,
                                         (std::uint8_t)refPair) == false)) {  // :1481-1482
                    cand = &candArray_[candIdx];  // :1483
                    cand->mode = NEAR_NEARMV;     // :1484
                    cand->motionMode = SIMPLE_TRANSLATION;  // :1485
                    cand->skipModeAllowed = false;  // :1486
                    cand->useIntrabc = 0;         // :1487
                    cand->isInterintraUsed = 0;   // :1488
                    cand->mv[0] = (std::int32_t)toInjMv0.as_int;  // :1489
                    cand->mv[1] = (std::int32_t)toInjMv1.as_int;  // :1490
                    cand->drlIndex = drli;        // :1491
                    cand->refFrame[0] = rf[0];    // :1492
                    cand->refFrame[1] = rf[1];    // :1493
                    cand->compGroupIdx = 0;       // :1494
                    cand->compoundIdx = 1;        // :1495
                    cand->compoundType = COMPOUND_AVERAGE;  // :1496

                    incMdCandCntA4m(candIdx, maxCanCount);  // :1498
                    injected.injectedMvs[injected.injectedMvCount][0] = (std::int32_t)toInjMv0.as_int;  // :1499
                    injected.injectedMvs[injected.injectedMvCount][1] = (std::int32_t)toInjMv1.as_int;  // :1500
                    injected.injectedRefTypes[injected.injectedMvCount] = refPair;  // :1501
                    ++injected.injectedMvCount;  // :1502
                }
            }
        }
    }
    // update tot Candidate count
    *candTotCnt = candIdx;  // :1508
}

// ---------------------------------------------------------------------------
// A4M-11: inject_new_candidates_light_pd1 over the av1_ref_frame_type carry.
// ---------------------------------------------------------------------------

// comp_ref0 / comp_ref1 (inter_prediction.h:396-407 / :409-420, verbatim).
static std::int32_t compRef0A4m(std::int32_t refIdx) {
    static const std::int32_t lut[TOTAL_UNIDIR_COMP_REFS_MV] = {LAST_FRAME_MV, LAST_FRAME_MV, LAST_FRAME_MV,
                                                                BWDREF_FRAME_MV, LAST2_FRAME_MV, LAST2_FRAME_MV,
                                                                LAST3_FRAME_MV, BWDREF_FRAME_MV, ALTREF2_FRAME_MV};
    return lut[refIdx];
}

static std::int32_t compRef1A4m(std::int32_t refIdx) {
    static const std::int32_t lut[TOTAL_UNIDIR_COMP_REFS_MV] = {LAST2_FRAME_MV, LAST3_FRAME_MV, GOLDEN_FRAME_MV,
                                                                ALTREF_FRAME_MV, LAST3_FRAME_MV,
                                                                GOLDEN_FRAME_MV, GOLDEN_FRAME_MV,
                                                                ALTREF2_FRAME_MV, ALTREF_FRAME_MV};
    return lut[refIdx];
}

// get_uni_comp_ref_idx (inter_prediction.h:428-441, verbatim).
static std::int8_t getUniCompRefIdxA4m(const std::int32_t* rf) {
    if (rf[1] <= INTRA_FRAME_MV) {
        return -1;
    }
    if ((rf[0] < BWDREF_FRAME_MV) && (rf[1] >= BWDREF_FRAME_MV)) {
        return -1;
    }
    for (std::int8_t refIdx = 0; refIdx < TOTAL_UNIDIR_COMP_REFS_MV; ++refIdx) {
        if (rf[0] == compRef0A4m(refIdx) && rf[1] == compRef1A4m(refIdx)) {
            return refIdx;
        }
    }
    return -1;
}

// av1_ref_frame_type (inter_prediction.h:447-459, verbatim; FWD_RF_OFFSET =
// ref - LAST_FRAME, BWD_RF_OFFSET = ref - BWDREF_FRAME, definitions.h
// :1393-1394).
std::int8_t av1RefFrameType(const std::int32_t* rf) {
    if (rf[1] > INTRA_FRAME_MV) {
        const std::int8_t uniCompRefIdx = getUniCompRefIdxA4m(rf);
        if (uniCompRefIdx >= 0) {
            return (std::int8_t)(TOTAL_REFS_PER_FRAME_MV + FWD_REFS_MV * BWD_REFS_MV + uniCompRefIdx);
        } else {
            return (std::int8_t)(TOTAL_REFS_PER_FRAME_MV + (rf[0] - LAST_FRAME_MV) +
                                 (rf[1] - BWDREF_FRAME_MV) * FWD_REFS_MV);
        }
    }
    return (std::int8_t)rf[0];
}

// inject_new_candidates_light_pd1 (mode_decision.c:2410-2515, verbatim over
// the carriers; BI_PRED = 2, REF_LIST_0 = 0).
void injectNewCandidatesLightPd1(InterInjectedCandidate* candArray, std::uint32_t* candTotCnt,
                                  std::uint32_t maxCanCount, const MeSbResults* meResults,
                                  std::uint32_t meBlockOffset, std::uint32_t meCandOffset,
                                  const Mv* const sbMeMv[2],
                                  const CandidateMv refMvStack[MODE_CTX_REF_FRAMES_MV][MAX_REF_MV_STACK_SIZE_MD],
                                  const std::uint8_t* refMvCount, const ChooseBestMvCtx& chooseBest,
                                  const std::uint8_t* wmSampleInfoNum, std::uint8_t reduceUnipredCandidates,
                                  bool allowBipred, bool isIntraBordered, bool useNeighbouringEnabled,
                                  bool corruptedMvCheck, InjectedMvCarrier& injected) {
    Mv bestPredMv[2];
    bestPredMv[0].as_int = 0;  // :2415 {{{0}}
    bestPredMv[1].as_int = 0;
    std::uint32_t candTotalCnt = *candTotCnt;  // :2416
    const std::uint8_t totalMeCnt = meResults->totalMeCandidateIndex[meBlockOffset];  // :2418
    const MeCandidate* meBlockResults = &meResults->meCandidateArray[meCandOffset];  // :2419

    for (std::uint8_t meCandidateIndex = 0; meCandidateIndex < totalMeCnt; ++meCandidateIndex) {  // :2421
        const MeCandidate* meBlockResultsPtr = &meBlockResults[meCandidateIndex];
        const std::uint8_t interDirection = meBlockResultsPtr->direction;
        const std::uint8_t list0RefIndex = meBlockResultsPtr->refIdxL0;
        const std::uint8_t list1RefIndex = meBlockResultsPtr->refIdxL1;

        if (reduceUnipredCandidates >= 2) {  // :2427-2435
            if ((totalMeCnt > 1) && (interDirection != 2)) {
                continue;
            }
        } else if (reduceUnipredCandidates) {
            if ((totalMeCnt > 3) && (interDirection != 2)) {
                continue;
            }
        }

        // NEWMV
        if (interDirection < 2 /* BI_PRED */) {  // :2440
            const std::uint8_t listIdx = interDirection;
            const std::uint8_t refIdx = interDirection ? list1RefIndex : list0RefIndex;
            const Mv toInjMv = sbMeMv[listIdx][refIdx];  // :2443
            const std::uint8_t toInjectRefType = (std::uint8_t)getRefFrameType(listIdx, refIdx);  // :2444
            if (injected.injectedMvCount == 0 ||
                mvIsAlreadyInjected(injected, (std::int32_t)toInjMv.as_int, (std::int32_t)toInjMv.as_int,
                                    toInjectRefType) == false) {  // :2445-2446
                std::uint8_t drlIndex = 0;
                chooseBestAv1MvPred(refMvStack, refMvCount, chooseBest.shutFastRate, chooseBest.approxInterRate,
                                    chooseBest.nmvVecCost, chooseBest.nmvCostStack, chooseBest.drlModeFacBits,
                                    (MvReferenceFrame)toInjectRefType, NEWMV, toInjMv, Mv{0, 0}, &drlIndex,
                                    bestPredMv);  // :2448-2449
                if (!corruptedMvCheck || isValidMvDiff((std::int32_t)bestPredMv[0].as_int,
                                                       (std::int32_t)bestPredMv[1].as_int,
                                                       (std::int32_t)toInjMv.as_int,
                                                       (std::int32_t)toInjMv.as_int, 0)) {  // :2450
                    InterInjectedCandidate* cand = &candArray[candTotalCnt];
                    cand->useIntrabc = 0;        // :2452
                    cand->isInterintraUsed = 0;  // :2453
                    cand->skipModeAllowed = false;  // :2454
                    cand->mode = NEWMV;          // :2455
                    cand->motionMode = SIMPLE_TRANSLATION;  // :2456
                    cand->drlIndex = drlIndex;   // :2457
                    cand->mv[0] = (std::int32_t)toInjMv.as_int;  // :2458
                    cand->refFrame[0] = toInjectRefType;  // :2459
                    cand->refFrame[1] = NONE_FRAME_MV;  // :2460
                    cand->predMv[0] = (std::int32_t)bestPredMv[0].as_int;  // :2461
                    cand->numProjRef = wmSampleInfoNum[toInjectRefType];  // :2462
                    incMdCandCntA4m(candTotalCnt, maxCanCount);  // :2463
                    // Add the injected MV to the list of injected MVs
                    injected.injectedMvs[injected.injectedMvCount][0] = (std::int32_t)toInjMv.as_int;  // :2465
                    injected.injectedRefTypes[injected.injectedMvCount] = toInjectRefType;  // :2466
                    ++injected.injectedMvCount;  // :2467
                }
            }
        } else if (allowBipred && interDirection == 2 &&
                   !(isIntraBordered && useNeighbouringEnabled)) {  // :2470-2471
            // NEW_NEWMV
            const Mv toInjMv0 = sbMeMv[meBlockResultsPtr->ref0List][list0RefIndex];  // :2475
            const Mv toInjMv1 = sbMeMv[meBlockResultsPtr->ref1List][list1RefIndex];  // :2476
            const MvReferenceFrame rf[2] = {(MvReferenceFrame)getRefFrameType(meBlockResultsPtr->ref0List,
                                                                              list0RefIndex),
                                            (MvReferenceFrame)getRefFrameType(meBlockResultsPtr->ref1List,
                                                                              list1RefIndex)};  // :2477-2478
            const std::int32_t rfInt[2] = {rf[0], rf[1]};
            const std::uint8_t toInjectRefType = (std::uint8_t)av1RefFrameType(rfInt);  // :2479
            if ((injected.injectedMvCount == 0 ||
                 mvIsAlreadyInjected(injected, (std::int32_t)toInjMv0.as_int, (std::int32_t)toInjMv1.as_int,
                                     toInjectRefType) == false)) {  // :2480-2481
                std::uint8_t drlIndex = 0;
                chooseBestAv1MvPred(refMvStack, refMvCount, chooseBest.shutFastRate, chooseBest.approxInterRate,
                                    chooseBest.nmvVecCost, chooseBest.nmvCostStack, chooseBest.drlModeFacBits,
                                    (MvReferenceFrame)toInjectRefType, NEW_NEWMV, toInjMv0, toInjMv1, &drlIndex,
                                    bestPredMv);  // :2483-2484
                if (!corruptedMvCheck || isValidMvDiff((std::int32_t)bestPredMv[0].as_int,
                                                       (std::int32_t)bestPredMv[1].as_int,
                                                       (std::int32_t)toInjMv0.as_int,
                                                       (std::int32_t)toInjMv1.as_int, 1)) {  // :2485
                    InterInjectedCandidate* cand = &candArray[candTotalCnt];
                    cand->useIntrabc = 0;        // :2487
                    cand->isInterintraUsed = 0;  // :2488
                    cand->skipModeAllowed = false;  // :2489
                    cand->drlIndex = drlIndex;   // :2490
                    cand->mv[0] = (std::int32_t)toInjMv0.as_int;  // :2491
                    cand->mv[1] = (std::int32_t)toInjMv1.as_int;  // :2492
                    cand->mode = NEW_NEWMV;      // :2493
                    cand->motionMode = SIMPLE_TRANSLATION;  // :2494
                    cand->refFrame[0] = rf[0];   // :2495
                    cand->refFrame[1] = rf[1];   // :2496
                    cand->predMv[0] = (std::int32_t)bestPredMv[0].as_int;  // :2497
                    cand->predMv[1] = (std::int32_t)bestPredMv[1].as_int;  // :2498
                    cand->compGroupIdx = 0;      // :2499
                    cand->compoundIdx = 1;       // :2500
                    cand->compoundType = COMPOUND_AVERAGE;  // :2501
                    incMdCandCntA4m(candTotalCnt, maxCanCount);  // :2502

                    // Add the injected MV to the list of injected MVs
                    injected.injectedMvs[injected.injectedMvCount][0] = (std::int32_t)toInjMv0.as_int;  // :2505
                    injected.injectedMvs[injected.injectedMvCount][1] = (std::int32_t)toInjMv1.as_int;  // :2506
                    injected.injectedRefTypes[injected.injectedMvCount] = toInjectRefType;  // :2507
                    ++injected.injectedMvCount;  // :2508
                }
            }
        }
    }
    // update the total number of candidates injected
    *candTotCnt = candTotalCnt;  // :2514
}

// ---------------------------------------------------------------------------
// A4M-12: inject_pme_candidates + inject_sframe_backup_candidate.
// ---------------------------------------------------------------------------

// ref_type_to_ref_idx (inter_prediction.h:511, verbatim).
static const std::uint8_t kRefTypeToRefIdx[REFS_PER_FRAME_MV + 1] = {0, 0, 1, 2, 3, 0, 1, 2};

// get_ref_frame_idx (inter_prediction.h:513-515, verbatim).
std::uint8_t getRefFrameIdx(MvReferenceFrame refType) {
    return kRefTypeToRefIdx[refType];
}

// inject_pme_candidates (mode_decision.c:2761-2859, verbatim over the
// carriers).
void injectPmeCandidates(InterInjectedCandidate* candArray, std::uint32_t* candTotCnt,
                          std::uint32_t maxCanCount, const MvReferenceFrame* refFrameTypeArr,
                          std::uint32_t totRefFrameTypes, const std::uint8_t validPmeMv[2][REFS_PER_FRAME_MV],
                          const Mv bestPmeMv[2][REFS_PER_FRAME_MV], bool allowBipred,
                          const CandidateMv refMvStack[MODE_CTX_REF_FRAMES_MV][MAX_REF_MV_STACK_SIZE_MD],
                          const std::uint8_t* refMvCount, const ChooseBestMvCtx& chooseBest,
                          const std::uint8_t* wmSampleInfoNum, bool corruptedMvCheck, bool doPme,
                          InjectedMvCarrier& injected, InjectorWallCalls& wallCalls) {
    Mv bestPredMv[2];
    bestPredMv[0].as_int = 0;  // :2764 {{{0}}
    bestPredMv[1].as_int = 0;
    std::uint32_t candTotalCnt = *candTotCnt;  // :2765
    for (std::uint32_t refIt = 0; refIt < totRefFrameTypes; ++refIt) {  // :2766
        const MvReferenceFrame refPair = refFrameTypeArr[refIt];
        MvReferenceFrame rf[2];
        av1SetRefFrame(rf, (std::int8_t)refPair);

        // single ref/list
        if (rf[1] == NONE_FRAME_MV) {  // :2772
            const MvReferenceFrame frameType = rf[0];
            const std::uint8_t listIdx = getListIdx(rf[0]);
            const std::uint8_t refIdx = getRefFrameIdx(rf[0]);

            if (validPmeMv[listIdx][refIdx]) {  // :2777
                const Mv toInjMv = bestPmeMv[listIdx][refIdx];  // :2778
                if ((injected.injectedMvCount == 0 ||
                     mvIsAlreadyInjected(injected, (std::int32_t)toInjMv.as_int, (std::int32_t)toInjMv.as_int,
                                         (std::uint8_t)frameType) == false)) {  // :2779-2780
                    std::uint8_t drlIndex = 0;
                    chooseBestAv1MvPred(refMvStack, refMvCount, chooseBest.shutFastRate,
                                        chooseBest.approxInterRate, chooseBest.nmvVecCost,
                                        chooseBest.nmvCostStack, chooseBest.drlModeFacBits, frameType, NEWMV,
                                        toInjMv, Mv{0, 0}, &drlIndex, bestPredMv);  // :2782-2783
                    if (!corruptedMvCheck || isValidMvDiff((std::int32_t)bestPredMv[0].as_int,
                                                           (std::int32_t)bestPredMv[1].as_int,
                                                           (std::int32_t)toInjMv.as_int,
                                                           (std::int32_t)toInjMv.as_int, 0)) {  // :2784
                        InterInjectedCandidate* cand = &candArray[candTotalCnt];
                        cand->useIntrabc = 0;        // :2786
                        cand->skipModeAllowed = false;  // :2787
                        cand->mode = NEWMV;          // :2788
                        cand->motionMode = SIMPLE_TRANSLATION;  // :2789
                        cand->isInterintraUsed = 0;  // :2790
                        cand->drlIndex = drlIndex;   // :2791
                        cand->mv[0] = (std::int32_t)toInjMv.as_int;  // :2792
                        cand->refFrame[0] = rf[0];   // :2793
                        cand->refFrame[1] = rf[1];   // :2794
                        cand->predMv[0] = (std::int32_t)bestPredMv[0].as_int;  // :2795
                        cand->numProjRef = wmSampleInfoNum[frameType];  // :2796
                        incMdCandCntA4m(candTotalCnt, maxCanCount);  // :2797

                        const bool enableIi = true;    // :2799
                        const bool enableObmc = true;  // :2800
                        const bool enableWarp = true;  // :2801
                        (void)enableIi;
                        (void)enableObmc;
                        (void)enableWarp;
                        // inj_non_simple_modes(pcs, ctx, &cand_total_cnt, enable_ii, enable_warp,
                        // enable_obmc) - the wall (mode_decision.c:2802)
                        ++wallCalls.nonSimpleModeCalls;
                        injected.injectedMvs[injected.injectedMvCount][0] = (std::int32_t)toInjMv.as_int;  // :2803
                        injected.injectedRefTypes[injected.injectedMvCount] = frameType;  // :2804
                        ++injected.injectedMvCount;  // :2805
                    }
                }
            }
        } else if (allowBipred) {  // :2809
            const std::uint8_t refIdx0 = getRefFrameIdx(rf[0]);
            const std::uint8_t refIdx1 = getRefFrameIdx(rf[1]);
            const std::uint8_t listIdx0 = getListIdx(rf[0]);
            const std::uint8_t listIdx1 = getListIdx(rf[1]);

            if (validPmeMv[listIdx0][refIdx0] && validPmeMv[listIdx1][refIdx1]) {  // :2815
                const Mv toInjMv0 = bestPmeMv[listIdx0][refIdx0];  // :2816
                const Mv toInjMv1 = bestPmeMv[listIdx1][refIdx1];  // :2817
                const std::int32_t rfInt[2] = {(std::int32_t)getRefFrameType(listIdx0, refIdx0),
                                               (std::int32_t)getRefFrameType(listIdx1, refIdx1)};  // :2818-2821
                const std::uint8_t toInjectRefType = (std::uint8_t)av1RefFrameType(rfInt);
                if ((injected.injectedMvCount == 0 ||
                     mvIsAlreadyInjected(injected, (std::int32_t)toInjMv0.as_int, (std::int32_t)toInjMv1.as_int,
                                         toInjectRefType) == false)) {  // :2822-2823
                    std::uint8_t drlIndex = 0;
                    chooseBestAv1MvPred(refMvStack, refMvCount, chooseBest.shutFastRate,
                                        chooseBest.approxInterRate, chooseBest.nmvVecCost,
                                        chooseBest.nmvCostStack, chooseBest.drlModeFacBits,
                                        (MvReferenceFrame)toInjectRefType, NEW_NEWMV, toInjMv0, toInjMv1,
                                        &drlIndex, bestPredMv);  // :2825-2826
                    if (!corruptedMvCheck || isValidMvDiff((std::int32_t)bestPredMv[0].as_int,
                                                           (std::int32_t)bestPredMv[1].as_int,
                                                           (std::int32_t)toInjMv0.as_int,
                                                           (std::int32_t)toInjMv1.as_int, 1)) {  // :2827
                        InterInjectedCandidate* cand = &candArray[candTotalCnt];
                        cand->useIntrabc = 0;        // :2829
                        cand->skipModeAllowed = false;  // :2830
                        cand->drlIndex = drlIndex;   // :2831
                        cand->mv[0] = (std::int32_t)toInjMv0.as_int;  // :2832
                        cand->mv[1] = (std::int32_t)toInjMv1.as_int;  // :2833
                        cand->mode = NEW_NEWMV;      // :2834
                        cand->motionMode = SIMPLE_TRANSLATION;  // :2835
                        cand->isInterintraUsed = 0;  // :2836
                        cand->refFrame[0] = rf[0];   // :2837
                        cand->refFrame[1] = rf[1];   // :2838
                        cand->predMv[0] = (std::int32_t)bestPredMv[0].as_int;  // :2839
                        cand->predMv[1] = (std::int32_t)bestPredMv[1].as_int;  // :2840
                        // determine_compound_mode(pcs, ctx, cand, MD_COMP_AVG) :2841 - the
                        // landed A4M-3 twin on the carrier merged into the candidate.
                        CompoundModeCarrier cmc;
                        cmc.maskType = -1;
                        cmc.searchDiffWedgeCalled = false;
                        determineCompoundMode(&cmc, MD_COMP_AVG);
                        cand->compGroupIdx = cmc.compGroupIdx;
                        cand->compoundIdx = cmc.compoundIdx;
                        cand->compoundType = cmc.interinterCompType;
                        (void)cmc.searchDiffWedgeCalled;
                        incMdCandCntA4m(candTotalCnt, maxCanCount);  // :2842

                        if (doPme) {  // :2844-2848
                            // ctx->cmp_store.pred0_cnt/pred1_cnt = 0 +
                            // inj_comp_modes(pcs, ctx, &cand_total_cnt) - the wall
                            // (mode_decision.c:2847)
                            ++wallCalls.compModeCalls;
                        }
                        injected.injectedMvs[injected.injectedMvCount][0] = (std::int32_t)toInjMv0.as_int;  // :2849
                        injected.injectedMvs[injected.injectedMvCount][1] = (std::int32_t)toInjMv1.as_int;  // :2850
                        injected.injectedRefTypes[injected.injectedMvCount] = toInjectRefType;  // :2851
                        ++injected.injectedMvCount;  // :2852
                    }
                }
            }
        }
    }
    (*candTotCnt) = candTotalCnt;  // :2858
}

// inject_sframe_backup_candidate (mode_decision.c:3507-3539, verbatim over
// the carriers; REF_LIST_1 = 1).
void injectSframeBackupCandidate(InterInjectedCandidate* candArray, std::uint32_t* candTotCnt,
                                  std::uint32_t maxCanCount,
                                  const CandidateMv refMvStack[MODE_CTX_REF_FRAMES_MV][MAX_REF_MV_STACK_SIZE_MD],
                                  const std::uint8_t* refMvCount, const ChooseBestMvCtx& chooseBest,
                                  const std::uint8_t* wmSampleInfoNum, bool corruptedMvCheck) {
    Mv bestPredMv[2];
    bestPredMv[0].as_int = 0;  // :3510
    bestPredMv[1].as_int = 0;
    std::uint32_t candTotalCnt = *candTotCnt;  // :3511
    candArray[candTotalCnt].drlIndex = 0;      // :3512
    Mv zeroMv;
    zeroMv.as_int = 0;
    chooseBestAv1MvPred(refMvStack, refMvCount, chooseBest.shutFastRate, chooseBest.approxInterRate,
                        chooseBest.nmvVecCost, chooseBest.nmvCostStack, chooseBest.drlModeFacBits,
                        (MvReferenceFrame)getRefFrameType(1 /* REF_LIST_1 */, 0), NEWMV, zeroMv, zeroMv,
                        &candArray[candTotalCnt].drlIndex, bestPredMv);  // :3513-3519
    if (!corruptedMvCheck || isValidMvDiff((std::int32_t)bestPredMv[0].as_int,
                                           (std::int32_t)bestPredMv[1].as_int, 0, 0, 0)) {  // :3520
        InterInjectedCandidate* cand = &candArray[candTotalCnt];
        cand->useIntrabc = 0;        // :3522
        cand->skipModeAllowed = false;  // :3523
        cand->mode = NEWMV;          // :3524
        cand->motionMode = SIMPLE_TRANSLATION;  // :3525
        cand->mv[0] = 0;             // :3526 (Mv){{0, 0}}
        cand->refFrame[0] = (MvReferenceFrame)getRefFrameType(1 /* REF_LIST_1 */, 0);  // :3527
        cand->refFrame[1] = NONE_FRAME_MV;  // :3528
        cand->transformType = 0;     // :3529 DCT_DCT
        cand->transformTypeUv = 0;   // :3530 DCT_DCT
        cand->predMv[0] = (std::int32_t)bestPredMv[0].as_int;  // :3531
        cand->isInterintraUsed = 0;  // :3532
        cand->numProjRef = wmSampleInfoNum[(std::uint8_t)getRefFrameType(1, 0)];  // :3534
        incMdCandCntA4m(candTotalCnt, maxCanCount);  // :3535
        // update the total number of candidates injected
        (*candTotCnt) = candTotalCnt;  // :3537 (INSIDE the if)
    }
}

}  // namespace l16_rdo
