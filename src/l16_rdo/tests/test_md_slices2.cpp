// l16_rdo A4M tests - the mode_decision.c REMAINDER surface (the MD1 slice
// landed the SSIM/psy/MV-helper spans; this file carries the rest).
// Independent oracles transcribe the reference arithmetic; every expectation
// cites the reference line it derives from. Integer outputs pinned exactly;
// double outputs compared bit-exactly against the test-side transcription.
#include <doctest.h>
#include <cstdint>
#include <cmath>
#include <vector>
#include "md_slices2.h"

// The port's types at namespace scope (the l7 test-file convention).
using l16_rdo::MvLimits;
using l16_rdo::Mv;
using l16_rdo::mvCheckBounds;
using l16_rdo::deriveSsimThresholdFactorForFullMd;
using l16_rdo::validRefFrameType;
using l16_rdo::INPUT_SIZE_720p_RANGE;
using l16_rdo::INPUT_SIZE_1080p_RANGE;
using l16_rdo::INTRA_FRAME_MV;
using l16_rdo::LAST_FRAME_MV;
using l16_rdo::BWDREF_FRAME_MV;
using l16_rdo::ALTREF_FRAME_MV;
using l16_rdo::NONE_FRAME_MV;

// ---------------------------------------------------------------------------
// A4M-1: mv_check_bounds (mode_decision.c:3000-3003).
// ---------------------------------------------------------------------------
TEST_CASE("l16_rdo mvCheckBounds flags out-of-range full-pel components") {
    // mode_decision.c:3000-3003: the mv is tested at FULL-PEL resolution
    // ((mv->y >> 3) / (mv->x >> 3)) against the four MvLimits bounds.
    // The reference's MvLimits field order is col_min/col_max/row_min/row_max
    // (definitions.h:278-283); the tests construct the struct by name so the
    // field mapping is load-bearing.
    // Arithmetic shift on negatives: (-24 >> 3) = -3 (the mv fields are
    // int16, the >> is the reference's own arithmetic shift).
    MvLimits lim;
    lim.col_min = -10;
    lim.col_max = 10;
    lim.row_min = -5;
    lim.row_max = 5;

    // mv->y >> 3 = 0 inside [-5, 5]; mv->x >> 3 = 0 inside [-10, 10]: in bounds.
    Mv mv0;
    mv0.x = 0;
    mv0.y = 0;
    CHECK(mvCheckBounds(&lim, &mv0) == 0);
    // y at the boundary value itself (row_max = 5 -> mv->y = 40): in bounds.
    Mv mvB;
    mvB.x = 80;
    mvB.y = 40;
    CHECK(mvCheckBounds(&lim, &mvB) == 0);
    // y just past the row bound: (40 + 8) >> 3 = 6 > 5 -> out.
    Mv mvRow;
    mvRow.x = 0;
    mvRow.y = 48;
    CHECK(mvCheckBounds(&lim, &mvRow) == 1);
    // x just past the col bound: (80 + 8) >> 3 = 11 > 10 -> out.
    Mv mvCol;
    mvCol.x = 88;
    mvCol.y = 0;
    CHECK(mvCheckBounds(&lim, &mvCol) == 1);
    // Negative side: (-40 - 8) >> 3 = -6 < -5 -> out (arithmetic shift).
    Mv mvNeg;
    mvNeg.x = 0;
    mvNeg.y = -48;
    CHECK(mvCheckBounds(&lim, &mvNeg) == 1);
    // x negative side: (-80 - 8) >> 3 = -11 < -10 -> out.
    Mv mvNegCol;
    mvNegCol.x = -88;
    mvNegCol.y = 0;
    CHECK(mvCheckBounds(&lim, &mvNegCol) == 1);
    // y out while x in and vice versa are independent rows of the ||.
    Mv mvRowNeg;
    mvRowNeg.x = 0;
    mvRowNeg.y = -41;
    CHECK(mvCheckBounds(&lim, &mvRowNeg) == 1);
    // Both out: still 1.
    Mv mvBoth;
    mvBoth.x = 88;
    mvBoth.y = 48;
    CHECK(mvCheckBounds(&lim, &mvBoth) == 1);
}

// ---------------------------------------------------------------------------
// A4M-1: derive_ssim_threshold_factor_for_full_md (mode_decision.c:3875-3877).
// ---------------------------------------------------------------------------
TEST_CASE("l16_rdo deriveSsimThresholdFactorForFullMd splits at 1080p") {
    // mode_decision.c:3875-3877: input_resolution >= INPUT_SIZE_1080p_RANGE
    // (= 4, definitions.h:1813) -> 1.02 else 1.03. Enumerate the whole
    // ResolutionRange domain (definitions.h:1808-1815).
    CHECK(deriveSsimThresholdFactorForFullMd(l16_rdo::INPUT_SIZE_240p_RANGE) == 1.03);
    CHECK(deriveSsimThresholdFactorForFullMd(l16_rdo::INPUT_SIZE_360p_RANGE) == 1.03);
    CHECK(deriveSsimThresholdFactorForFullMd(l16_rdo::INPUT_SIZE_480p_RANGE) == 1.03);
    CHECK(deriveSsimThresholdFactorForFullMd(INPUT_SIZE_720p_RANGE) == 1.03);
    CHECK(deriveSsimThresholdFactorForFullMd(INPUT_SIZE_1080p_RANGE) == 1.02);
    CHECK(deriveSsimThresholdFactorForFullMd(l16_rdo::INPUT_SIZE_4K_RANGE) == 1.02);
    CHECK(deriveSsimThresholdFactorForFullMd(l16_rdo::INPUT_SIZE_8K_RANGE) == 1.02);
}

// ---------------------------------------------------------------------------
// A4M-2: valid_ref_frame_type (mode_decision.c:3489-3504) over the
// av1_set_ref_frame carry (inter_prediction.h:487-496).
// ---------------------------------------------------------------------------
TEST_CASE("l16_rdo validRefFrameType matches the ref-frame-type array walk") {
    // mode_decision.c:3489-3504: rf[0] == INTRA_FRAME short-circuits true;
    // otherwise rf must equal av1_set_ref_frame(ref_frame_type_arr[i]) for
    // some i < tot_ref_frame_types. The array here is the pcs carrier
    // (ref_frame_type_arr, md_process.h:944).
    // A live PCS array for a low-delay single-ref-ish frame:
    // {LAST_FRAME, ALTREF_FRAME, LAST|ALTREF compound (type 16 =
    // map row 8), BWDREF}.
    const l16_rdo::MvReferenceFrame arr[4] = {LAST_FRAME_MV, ALTREF_FRAME_MV, 16 /*LAST|ALTREF*/, BWDREF_FRAME_MV};

    // INTRA_FRAME first: skip validation (the :3492-3494 early-out).
    const l16_rdo::MvReferenceFrame rfIntra[2] = {INTRA_FRAME_MV, NONE_FRAME_MV};
    CHECK(validRefFrameType(rfIntra, arr, 4) == true);
    // INTRA_FRAME first even with a compound second slot: still the early-out.
    const l16_rdo::MvReferenceFrame rfIntra2[2] = {INTRA_FRAME_MV, ALTREF_FRAME_MV};
    CHECK(validRefFrameType(rfIntra2, arr, 4) == true);

    // LAST + NONE: type 0 of the array.
    const l16_rdo::MvReferenceFrame rfLast[2] = {LAST_FRAME_MV, NONE_FRAME_MV};
    CHECK(validRefFrameType(rfLast, arr, 4) == true);
    // ALTREF + NONE: type 1.
    const l16_rdo::MvReferenceFrame rfAlt[2] = {ALTREF_FRAME_MV, NONE_FRAME_MV};
    CHECK(validRefFrameType(rfAlt, arr, 4) == true);
    // LAST + ALTREF: type 9 = ref_frame_map[1] = {LAST, ALTREF}.
    const l16_rdo::MvReferenceFrame rfLastAlt[2] = {LAST_FRAME_MV, ALTREF_FRAME_MV};
    CHECK(validRefFrameType(rfLastAlt, arr, 4) == true);
    // BWDREF + NONE: type 3.
    const l16_rdo::MvReferenceFrame rfBwd[2] = {BWDREF_FRAME_MV, NONE_FRAME_MV};
    CHECK(validRefFrameType(rfBwd, arr, 4) == true);

    // Reversed pair (ALTREF first): no row of the map is (ALTREF, LAST) ->
    // false.
    const l16_rdo::MvReferenceFrame rfSwap[2] = {ALTREF_FRAME_MV, LAST_FRAME_MV};
    CHECK(validRefFrameType(rfSwap, arr, 4) == false);
    // LAST2 (not in the array): false.
    const l16_rdo::MvReferenceFrame rfLast2[2] = {2 /*LAST2_FRAME*/, NONE_FRAME_MV};
    CHECK(validRefFrameType(rfLast2, arr, 4) == false);
    // The compound pair (LAST, LAST3) = ref_frame_map row 13 (uni dir idx 1):
    // not in the array -> false.
    const l16_rdo::MvReferenceFrame rfLastLast3[2] = {LAST_FRAME_MV, 3 /*LAST3_FRAME*/};
    CHECK(validRefFrameType(rfLastLast3, arr, 4) == false);

    // tot_ref_frame_types = 0: only the INTRA early-out can pass.
    const l16_rdo::MvReferenceFrame rfX[2] = {LAST_FRAME_MV, NONE_FRAME_MV};
    CHECK(validRefFrameType(rfX, arr, 0) == false);
    CHECK(validRefFrameType(rfIntra, arr, 0) == true);

    // The truncated walk: with count = 3 the BWDREF row (index 3) is not
    // consulted.
    CHECK(validRefFrameType(rfBwd, arr, 3) == false);
    CHECK(validRefFrameType(rfBwd, arr, 4) == true);
}

// ---------------------------------------------------------------------------
// A4M-3: get_tot_comp_types_bsize (mode_decision.c:124-126) +
// determine_compound_mode (:515-552) over to_av1_compound_lut (:513).
// ---------------------------------------------------------------------------
using l16_rdo::MdCompType;
using l16_rdo::MD_COMP_AVG;
using l16_rdo::MD_COMP_DIST;
using l16_rdo::MD_COMP_DIFF0;
using l16_rdo::MD_COMP_WEDGE;
using l16_rdo::MD_COMP_TYPES;
using l16_rdo::CompoundType;
using l16_rdo::COMPOUND_AVERAGE;
using l16_rdo::COMPOUND_DISTWTD;
using l16_rdo::COMPOUND_WEDGE;
using l16_rdo::COMPOUND_DIFFWTD;
using l16_rdo::getTotCompTypesBsize;
using l16_rdo::determineCompoundMode;
using l16_rdo::CompoundModeCarrier;

TEST_CASE("l16_rdo getTotCompTypesBsize caps at WEDGE where wedge bits are 0") {
    // mode_decision.c:124-126: (wedge_params_bits(bsize) == 0) ?
    // MIN(tot, MD_COMP_WEDGE) : tot. The wedge bits column
    // (inter_prediction.c:2001-2024 via the landed l17 twin) is 4 for
    // 8x8..32x32 and 8x32/32x8, 0 elsewhere - enumerate all 22 bsizes.
    const int totIn[2] = {MD_COMP_TYPES, MD_COMP_DIST};
    for (int b = 0; b < l16_rdo::BLOCK_SIZES_ALL; ++b) {
        const l16_rdo::BlockSize bs = (l16_rdo::BlockSize)b;
        const int bits = (bs >= l16_rdo::BLOCK_8X8 && bs <= l16_rdo::BLOCK_32X32) ||
                                 bs == l16_rdo::BLOCK_8X32 || bs == l16_rdo::BLOCK_32X8
                             ? 4
                             : 0;
        const int wantFull = bits == 0 ? l16_rdo::aomMin(MD_COMP_TYPES, MD_COMP_WEDGE) : MD_COMP_TYPES;
        const int wantDist = bits == 0 ? l16_rdo::aomMin(MD_COMP_DIST, MD_COMP_WEDGE) : MD_COMP_DIST;
        CHECK(getTotCompTypesBsize(totIn[0], bs) == wantFull);
        CHECK(getTotCompTypesBsize(totIn[1], bs) == wantDist);
    }
    // The 8x8 wedge shape keeps tot uncapped.
    CHECK(getTotCompTypesBsize(MD_COMP_TYPES, l16_rdo::BLOCK_8X8) == MD_COMP_TYPES);
    CHECK(getTotCompTypesBsize(MD_COMP_TYPES, l16_rdo::BLOCK_32X8) == MD_COMP_TYPES);
    // The 64x64 shape caps MD_COMP_TYPES (4) at MD_COMP_WEDGE (3).
    CHECK(getTotCompTypesBsize(MD_COMP_TYPES, l16_rdo::BLOCK_64X64) == MD_COMP_WEDGE);
    CHECK(getTotCompTypesBsize(MD_COMP_TYPES, l16_rdo::BLOCK_4X4) == MD_COMP_WEDGE);
    // MD_COMP_DIST stays MD_COMP_DIST everywhere (MIN(1, 3) = 1).
    CHECK(getTotCompTypesBsize(MD_COMP_DIST, l16_rdo::BLOCK_64X64) == MD_COMP_DIST);
}

TEST_CASE("l16_rdo determineCompoundMode writes the exact block_mi fields per type") {
    // mode_decision.c:515-552 with the candidate wall threaded as the
    // written-field carrier; the svt_aom_search_compound_diff_wedge wall
    // (:3718, enc_inter_prediction.c) threads as the called flag (its
    // candidate mutation stays parked with it - named in md_slices2.h).
    // to_av1_compound_lut (:513): AVG->AVERAGE, DIST->DISTWTD,
    // DIFF0->DIFFWTD, WEDGE->WEDGE - NOT the identity on MD_COMP (DIFF0
    // and WEDGE swap positions between the two enum orders).
    const int wantType[MD_COMP_TYPES] = {COMPOUND_AVERAGE, COMPOUND_DISTWTD, COMPOUND_DIFFWTD, COMPOUND_WEDGE};
    for (int t = 0; t < MD_COMP_TYPES; ++t) {
        CompoundModeCarrier c;
        c.interinterCompType = (CompoundType)0xBEEF;
        c.compGroupIdx = -2;
        c.compoundIdx = -2;
        c.maskType = -2;
        c.searchDiffWedgeCalled = false;
        determineCompoundMode(&c, (MdCompType)t);
        CHECK((int)c.interinterCompType == wantType[t]);
        const bool identityOrDiffers = wantType[t] == t || (int)c.interinterCompType != t;
        CHECK(identityOrDiffers);
        CHECK(c.searchDiffWedgeCalled == (t == MD_COMP_DIFF0 || t == MD_COMP_WEDGE));
        if (t == MD_COMP_AVG) {
            CHECK(c.compGroupIdx == 0);
            CHECK(c.compoundIdx == 1);
            CHECK(c.maskType == -2);  // untouched
        } else if (t == MD_COMP_DIST) {
            CHECK(c.compGroupIdx == 0);
            CHECK(c.compoundIdx == 0);
            CHECK(c.maskType == -2);  // untouched
        } else {
            CHECK(c.compGroupIdx == 1);
            CHECK(c.compoundIdx == 1);
            if (t == MD_COMP_DIFF0) {
                CHECK(c.maskType == 55);  // the :535 literal
            } else {
                CHECK(c.maskType == -2);  // WEDGE does not write mask_type
            }
        }
    }
    // The lut spell-out, pinned per slot (the reference array is
    // {COMPOUND_AVERAGE, COMPOUND_DISTWTD, COMPOUND_DIFFWTD, COMPOUND_WEDGE}).
    CHECK(COMPOUND_AVERAGE == 0);
    CHECK(COMPOUND_DISTWTD == 1);
    CHECK(COMPOUND_WEDGE == 2);
    CHECK(COMPOUND_DIFFWTD == 3);
}

// ---------------------------------------------------------------------------
// A4M-4: svt_aom_obmc_motion_mode_allowed (mode_decision.c:228-270) over the
// three inline-helper carries (inter_prediction.h:378-388,
// definitions.h:1610-1612).
// ---------------------------------------------------------------------------
using l16_rdo::MotionMode;
using l16_rdo::SIMPLE_TRANSLATION;
using l16_rdo::OBMC_CAUSAL;
using l16_rdo::WARPED_CAUSAL;
using l16_rdo::TransformationType;
using l16_rdo::IDENTITY_TT;
using l16_rdo::TRANSLATION_TT;
using l16_rdo::ROTZOOM_TT;
using l16_rdo::AFFINE_TT;
using l16_rdo::ObmcCtrlsCarrier;
using l16_rdo::obmcMotionModeAllowed;
using l16_rdo::isMotionVariationAllowedBsize;
using l16_rdo::isInterSinglerefMode;
using l16_rdo::isGlobalMvBlock;
using l16_rdo::NEARESTMV_PM;
using l16_rdo::NEARMV_PM;
using l16_rdo::GLOBALMV_PM;
using l16_rdo::NEWMV_PM;
using l16_rdo::NEAREST_NEARESTMV_PM;
using l16_rdo::NEW_NEWMV_PM;
using l16_rdo::GLOBAL_GLOBALMV_PM;
using l16_rdo::DC_PRED_PM;

TEST_CASE("l16_rdo obmcMotionModeAllowed walks the SIMPLE_TRANSLATION gate ladder") {
    // The all-OBMC-allowed carrier: enabled, max_blk_size = 64, no face-off,
    // switchable, sub-pel (force_integer_mv = 0), non-global gm type,
    // overlappable neighbors present.
    ObmcCtrlsCarrier obmc;
    obmc.enabled = 1;
    obmc.maxBlkSize = 64;
    obmc.transFaceOff = 0;
    const bool switchable = true;
    const int forceInt = 0;
    const std::uint32_t overl = 2;

    // The allowed row: 16x16 unipred NEWMV, rf1 = NONE_FRAME -> OBMC_CAUSAL.
    CHECK(obmcMotionModeAllowed(obmc, switchable, forceInt, TRANSLATION_TT, overl,
                                l16_rdo::BLOCK_16X16, 0, NONE_FRAME_MV,
                                NEWMV_PM) == OBMC_CAUSAL);

    // Gate 1 (:235-237): trans_face_off && situation == 0 -> SIMPLE even in
    // the all-allowed carrier.
    obmc.transFaceOff = 1;
    CHECK(obmcMotionModeAllowed(obmc, switchable, forceInt, TRANSLATION_TT, overl,
                                l16_rdo::BLOCK_16X16, 0, NONE_FRAME_MV,
                                NEWMV_PM) == SIMPLE_TRANSLATION);
    // ...but situation 1 bypasses the face-off gate.
    CHECK(obmcMotionModeAllowed(obmc, switchable, forceInt, TRANSLATION_TT, overl,
                                l16_rdo::BLOCK_16X16, 1, NONE_FRAME_MV,
                                NEWMV_PM) == OBMC_CAUSAL);
    obmc.transFaceOff = 0;

    // Gate 2 (:240-243): the max_blk_size cap, on both axes. Shapes whose
    // wide or high EXCEEDS 64 are capped; 16x64/64x16 (exactly 64 in one
    // axis) are not (the gate is strictly >).
    CHECK(obmcMotionModeAllowed(obmc, switchable, forceInt, TRANSLATION_TT, overl,
                                l16_rdo::BLOCK_128X128, 0, NONE_FRAME_MV,
                                NEWMV_PM) == SIMPLE_TRANSLATION);
    CHECK(obmcMotionModeAllowed(obmc, switchable, forceInt, TRANSLATION_TT, overl,
                                l16_rdo::BLOCK_64X128, 0, NONE_FRAME_MV,
                                NEWMV_PM) == SIMPLE_TRANSLATION);
    CHECK(obmcMotionModeAllowed(obmc, switchable, forceInt, TRANSLATION_TT, overl,
                                l16_rdo::BLOCK_128X64, 0, NONE_FRAME_MV,
                                NEWMV_PM) == SIMPLE_TRANSLATION);
    CHECK(obmcMotionModeAllowed(obmc, switchable, forceInt, TRANSLATION_TT, overl,
                                l16_rdo::BLOCK_16X64, 0, NONE_FRAME_MV,
                                NEWMV_PM) == OBMC_CAUSAL);
    CHECK(obmcMotionModeAllowed(obmc, switchable, forceInt, TRANSLATION_TT, overl,
                                l16_rdo::BLOCK_64X16, 0, NONE_FRAME_MV,
                                NEWMV_PM) == OBMC_CAUSAL);

    // Gate 3 (:244-246): obmc_ctrls.enabled == 0.
    obmc.enabled = 0;
    CHECK(obmcMotionModeAllowed(obmc, switchable, forceInt, TRANSLATION_TT, overl,
                                l16_rdo::BLOCK_16X16, 0, NONE_FRAME_MV,
                                NEWMV_PM) == SIMPLE_TRANSLATION);
    obmc.enabled = 1;

    // Gate 4 (:249-251): !is_motion_mode_switchable.
    CHECK(obmcMotionModeAllowed(obmc, false, forceInt, TRANSLATION_TT, overl,
                                l16_rdo::BLOCK_16X16, 0, NONE_FRAME_MV,
                                NEWMV_PM) == SIMPLE_TRANSLATION);

    // Gate 5 (:253-258): force_integer_mv == 0 and the candidate is a
    // GLOBALMV-mode candidate with a > TRANSLATION gm type.
    CHECK(obmcMotionModeAllowed(obmc, switchable, 0, ROTZOOM_TT, overl,
                                l16_rdo::BLOCK_16X16, 0, NONE_FRAME_MV,
                                GLOBALMV_PM) == SIMPLE_TRANSLATION);
    CHECK(obmcMotionModeAllowed(obmc, switchable, 0, AFFINE_TT, overl,
                                l16_rdo::BLOCK_16X16, 0, NONE_FRAME_MV,
                                GLOBAL_GLOBALMV_PM) == SIMPLE_TRANSLATION);
    // The IDENTITY / TRANSLATION gm types do not trip the gate.
    CHECK(obmcMotionModeAllowed(obmc, switchable, 0, IDENTITY_TT, overl,
                                l16_rdo::BLOCK_16X16, 0, NONE_FRAME_MV,
                                GLOBALMV_PM) == OBMC_CAUSAL);
    CHECK(obmcMotionModeAllowed(obmc, switchable, 0, TRANSLATION_TT, overl,
                                l16_rdo::BLOCK_16X16, 0, NONE_FRAME_MV,
                                GLOBALMV_PM) == OBMC_CAUSAL);
    // force_integer_mv == 1 skips the gm gate entirely: GLOBALMV at AFFINE
    // proceeds.
    CHECK(obmcMotionModeAllowed(obmc, switchable, 1, AFFINE_TT, overl,
                                l16_rdo::BLOCK_16X16, 0, NONE_FRAME_MV,
                                GLOBALMV_PM) == OBMC_CAUSAL);

    // Gate 6 (:259-269): the bsize/mode/rf1/overlappable conjunction.
    // Non-variation-allowed bsize (4x4).
    CHECK(obmcMotionModeAllowed(obmc, switchable, forceInt, TRANSLATION_TT, overl,
                                l16_rdo::BLOCK_4X4, 0, NONE_FRAME_MV,
                                NEWMV_PM) == SIMPLE_TRANSLATION);
    // Compound mode (not single-ref).
    CHECK(obmcMotionModeAllowed(obmc, switchable, forceInt, TRANSLATION_TT, overl,
                                l16_rdo::BLOCK_16X16, 0, ALTREF_FRAME_MV,
                                NEW_NEWMV_PM) == SIMPLE_TRANSLATION);
    // rf1 = INTRA_FRAME (0) fails the !(rf1 > INTRA_FRAME) arm.
    CHECK(obmcMotionModeAllowed(obmc, switchable, forceInt, TRANSLATION_TT, overl,
                                l16_rdo::BLOCK_16X16, 0, INTRA_FRAME_MV,
                                NEWMV_PM) == SIMPLE_TRANSLATION);
    // No overlappable neighbors.
    CHECK(obmcMotionModeAllowed(obmc, switchable, forceInt, TRANSLATION_TT, 0,
                                l16_rdo::BLOCK_16X16, 0, NONE_FRAME_MV,
                                NEWMV_PM) == SIMPLE_TRANSLATION);
    // NEARESTMV (single-ref) is allowed too.
    CHECK(obmcMotionModeAllowed(obmc, switchable, forceInt, TRANSLATION_TT, overl,
                                l16_rdo::BLOCK_8X8, 0, NONE_FRAME_MV,
                                NEARESTMV_PM) == OBMC_CAUSAL);
}

TEST_CASE("l16_rdo the three obmc helper carries pin their domains") {
    // is_motion_variation_allowed_bsize (inter_prediction.h:378-380): both
    // axes >= 8 - enumerate all 22 bsizes against blockSizeWide/High.
    for (int b = 0; b < l16_rdo::BLOCK_SIZES_ALL; ++b) {
        const l16_rdo::BlockSize bs = (l16_rdo::BlockSize)b;
        const int want = (l16_rdo::blockSizeWide[bs] >= 8 && l16_rdo::blockSizeHigh[bs] >= 8) ? 1 : 0;
        CHECK((int)isMotionVariationAllowedBsize(bs) == want);
    }
    // is_inter_singleref_mode (definitions.h:1610-1612): NEARESTMV(13) ..
    // NEAREST_NEARESTMV(17) exclusive; check both neighbors and both ends.
    CHECK(isInterSinglerefMode(DC_PRED_PM) == 0);
    CHECK(isInterSinglerefMode(NEARESTMV_PM) == 1);
    CHECK(isInterSinglerefMode(NEARMV_PM) == 1);
    CHECK(isInterSinglerefMode(NEWMV_PM) == 1);
    CHECK(isInterSinglerefMode(NEAREST_NEARESTMV_PM) == 0);
    CHECK(isInterSinglerefMode(NEW_NEWMV_PM) == 0);
    // is_global_mv_block (inter_prediction.h:382-388): the GLOBALMV/GLOBAL_
    // GLOBALMV + type > TRANSLATION + bsize conjunction. CONFIG_ENABLE_
    // GLOBAL_MOTION is live (= 1), so the guard body is kept.
    CHECK(isGlobalMvBlock(GLOBALMV_PM, l16_rdo::BLOCK_16X16, ROTZOOM_TT) == 1);
    CHECK(isGlobalMvBlock(GLOBAL_GLOBALMV_PM, l16_rdo::BLOCK_16X16, AFFINE_TT) == 1);
    CHECK(isGlobalMvBlock(NEWMV_PM, l16_rdo::BLOCK_16X16, ROTZOOM_TT) == 0);
    CHECK(isGlobalMvBlock(GLOBALMV_PM, l16_rdo::BLOCK_16X16, TRANSLATION_TT) == 0);
    CHECK(isGlobalMvBlock(GLOBALMV_PM, l16_rdo::BLOCK_16X16, IDENTITY_TT) == 0);
    CHECK(isGlobalMvBlock(GLOBALMV_PM, l16_rdo::BLOCK_4X4, ROTZOOM_TT) == 0);
    CHECK(isGlobalMvBlock(GLOBALMV_PM, l16_rdo::BLOCK_8X4, ROTZOOM_TT) == 0);
    // is_motion_variation_allowed_bsize has NO upper bound: 64x64 passes.
    CHECK(isGlobalMvBlock(GLOBALMV_PM, l16_rdo::BLOCK_64X64, ROTZOOM_TT) == 1);
}
