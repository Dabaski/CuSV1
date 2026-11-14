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

}  // namespace l16_rdo
