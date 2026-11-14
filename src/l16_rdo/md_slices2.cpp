// l16_rdo A4M - the mode_decision.c remainder surface.
// GREEN step lands per nano-slice; see md_slices2.h for the span map. Every
// body is a verbatim transcription of its cited reference span with the
// ModeDecisionContext/pcs walls threaded as carrier structs.
#include "md_slices2.h"
#include "md_slices.h"

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

}  // namespace l16_rdo
