// l16_rdo A4M - the mode_decision.c remainder surface.
// GREEN step lands per nano-slice; see md_slices2.h for the span map. Every
// body is a verbatim transcription of its cited reference span with the
// ModeDecisionContext/pcs walls threaded as carrier structs.
#include "md_slices2.h"

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

}  // namespace l16_rdo
