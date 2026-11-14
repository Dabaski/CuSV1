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
using l16_rdo::LAST2_FRAME_MV;
using l16_rdo::LAST3_FRAME_MV;
using l16_rdo::GOLDEN_FRAME_MV;
using l16_rdo::ALTREF2_FRAME_MV;
using l16_rdo::NEAR_NEARMV_PM;
using l16_rdo::NEAREST_NEWMV_PM;
using l16_rdo::NEW_NEARESTMV_PM;
using l16_rdo::NEAR_NEWMV_PM;
using l16_rdo::NEW_NEARMV_PM;


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

// ---------------------------------------------------------------------------
// A4M-5: skip_compound_on_ref_types (mode_decision.c:1008-1048).
// ---------------------------------------------------------------------------
using l16_rdo::SkipCompXdCarrier;
using l16_rdo::SkipCompNdMi;
using l16_rdo::skipCompoundOnRefTypes;
using l16_rdo::getListIdx;
using l16_rdo::isInterCompoundMode;

TEST_CASE("l16_rdo skipCompoundOnRefTypes prunes compound on the neighbour walk") {
    // mode_decision.c:1008-1048. The xd wall threads pointer-shaped (the
    // l7 InterMbD precedent): a NULL left/above carrier stands for the
    // unavailable neighbour mbmi.
    const SkipCompNdMi leftNewmvLast = {NEWMV_PM, {LAST_FRAME_MV, NONE_FRAME_MV}};
    const SkipCompNdMi leftNewmvAlt = {NEWMV_PM, {ALTREF_FRAME_MV, NONE_FRAME_MV}};
    const SkipCompNdMi leftNewmvLast3 = {NEWMV_PM, {LAST3_FRAME_MV, NONE_FRAME_MV}};
    const SkipCompNdMi leftCompoundLastAlt = {NEW_NEWMV_PM, {LAST_FRAME_MV, ALTREF_FRAME_MV}};
    const SkipCompNdMi upNewmvLast = {NEWMV_PM, {LAST_FRAME_MV, NONE_FRAME_MV}};
    const SkipCompNdMi upCompoundLastAlt = {NEAR_NEARMV_PM, {LAST_FRAME_MV, ALTREF_FRAME_MV}};

    // rf = (LAST, ALTREF): lists (0, 1) - the walk is reached.
    const l16_rdo::MvReferenceFrame rfLastAlt[2] = {LAST_FRAME_MV, ALTREF_FRAME_MV};

    // The inter_comp_ctrls.skip_on_ref_info wall = 0 -> false unconditionally.
    {
        SkipCompXdCarrier xd;
        xd.leftAvailable = 1;
        xd.upAvailable = 1;
        xd.leftMbmi = &leftNewmvLast;
        xd.aboveMbmi = &upNewmvLast;
        CHECK(skipCompoundOnRefTypes(0, xd, rfLastAlt) == false);
    }

    // The same-list rule (:1016-1020): both refs from list 0 (fwd).
    {
        const l16_rdo::MvReferenceFrame rfFwdPair[2] = {LAST_FRAME_MV, GOLDEN_FRAME_MV};
        SkipCompXdCarrier xd;
        xd.leftAvailable = 0;
        xd.upAvailable = 0;
        xd.leftMbmi = nullptr;
        xd.aboveMbmi = nullptr;
        CHECK(skipCompoundOnRefTypes(1, xd, rfFwdPair) == true);
    }
    // ...and both from list 1 (bwd): (BWDREF, ALTREF2).
    {
        const l16_rdo::MvReferenceFrame rfBwdPair[2] = {BWDREF_FRAME_MV, ALTREF2_FRAME_MV};
        SkipCompXdCarrier xd;
        xd.leftAvailable = 0;
        xd.upAvailable = 0;
        xd.leftMbmi = nullptr;
        xd.aboveMbmi = nullptr;
        CHECK(skipCompoundOnRefTypes(1, xd, rfBwdPair) == true);
    }

    // No neighbour available at all (:1024-1026) -> false (do not skip).
    {
        SkipCompXdCarrier xd;
        xd.leftAvailable = 0;
        xd.upAvailable = 0;
        xd.leftMbmi = nullptr;
        xd.aboveMbmi = nullptr;
        CHECK(skipCompoundOnRefTypes(1, xd, rfLastAlt) == false);
    }

    // The left walk (:1028-1036).
    {
        SkipCompXdCarrier xd;
        xd.leftAvailable = 1;
        xd.upAvailable = 0;
        xd.leftMbmi = &leftNewmvLast;
        xd.aboveMbmi = nullptr;
        // Left single-ref with ref0 == rf[0]: matched -> false.
        CHECK(skipCompoundOnRefTypes(1, xd, rfLastAlt) == false);
    }
    {
        SkipCompXdCarrier xd;
        xd.leftAvailable = 1;
        xd.upAvailable = 0;
        xd.leftMbmi = &leftNewmvAlt;
        xd.aboveMbmi = nullptr;
        // Left single-ref with ref0 == rf[1] (the || arm): matched -> false.
        CHECK(skipCompoundOnRefTypes(1, xd, rfLastAlt) == false);
    }
    {
        SkipCompXdCarrier xd;
        xd.leftAvailable = 1;
        xd.upAvailable = 0;
        xd.leftMbmi = &leftNewmvLast3;
        xd.aboveMbmi = nullptr;
        // Left single-ref with ref0 = LAST3 (neither rf): no match; above
        // unavailable -> skip_comp stays true.
        CHECK(skipCompoundOnRefTypes(1, xd, rfLastAlt) == true);
    }
    {
        SkipCompXdCarrier xd;
        xd.leftAvailable = 1;
        xd.upAvailable = 0;
        xd.leftMbmi = &leftCompoundLastAlt;
        xd.aboveMbmi = nullptr;
        // Left compound with the EXACT pair: matched -> false.
        CHECK(skipCompoundOnRefTypes(1, xd, rfLastAlt) == false);
    }
    {
        // Left compound with the pair SWAPPED: (ALTREF, LAST) is not
        // (LAST, ALTREF) -> no match -> true.
        const SkipCompNdMi leftCompoundSwap = {NEW_NEWMV_PM, {ALTREF_FRAME_MV, LAST_FRAME_MV}};
        SkipCompXdCarrier xd;
        xd.leftAvailable = 1;
        xd.upAvailable = 0;
        xd.leftMbmi = &leftCompoundSwap;
        xd.aboveMbmi = nullptr;
        CHECK(skipCompoundOnRefTypes(1, xd, rfLastAlt) == true);
    }

    // The up walk (:1037-1045).
    {
        SkipCompXdCarrier xd;
        xd.leftAvailable = 0;
        xd.upAvailable = 1;
        xd.leftMbmi = nullptr;
        xd.aboveMbmi = &upNewmvLast;
        // Up single-ref ref0 == rf[0]: matched -> false.
        CHECK(skipCompoundOnRefTypes(1, xd, rfLastAlt) == false);
    }
    {
        SkipCompXdCarrier xd;
        xd.leftAvailable = 0;
        xd.upAvailable = 1;
        xd.leftMbmi = nullptr;
        xd.aboveMbmi = &upCompoundLastAlt;
        // Up compound NEAR_NEARMV with the exact pair: matched -> false.
        CHECK(skipCompoundOnRefTypes(1, xd, rfLastAlt) == false);
    }
    {
        // Both available: left misses, up hits -> false.
        SkipCompXdCarrier xd;
        xd.leftAvailable = 1;
        xd.upAvailable = 1;
        xd.leftMbmi = &leftNewmvLast3;
        xd.aboveMbmi = &upNewmvLast;
        CHECK(skipCompoundOnRefTypes(1, xd, rfLastAlt) == false);
    }
    {
        // Both available and both miss -> true.
        SkipCompXdCarrier xd;
        xd.leftAvailable = 1;
        xd.upAvailable = 1;
        xd.leftMbmi = &leftNewmvLast3;
        xd.aboveMbmi = &leftNewmvLast3;
        CHECK(skipCompoundOnRefTypes(1, xd, rfLastAlt) == true);
    }

    // get_list_idx (inter_prediction.h:505-509): the REFS_PER_FRAME+1 = 8
    // row table {0,0,0,0,0,1,1,1}; INTRA(0)..GOLDEN(4) list 0, BWD..ALT list 1.
    CHECK(getListIdx(INTRA_FRAME_MV) == 0);
    CHECK(getListIdx(LAST_FRAME_MV) == 0);
    CHECK(getListIdx(GOLDEN_FRAME_MV) == 0);
    CHECK(getListIdx(BWDREF_FRAME_MV) == 1);
    CHECK(getListIdx(ALTREF2_FRAME_MV) == 1);
    CHECK(getListIdx(ALTREF_FRAME_MV) == 1);

    // is_inter_compound_mode (definitions.h:1603-1608): the inclusive
    // NEAREST_NEARESTMV..NEW_NEWMV window.
    CHECK(isInterCompoundMode(NEAREST_NEARESTMV_PM) == 1);
    CHECK(isInterCompoundMode(NEAR_NEARMV_PM) == 1);
    CHECK(isInterCompoundMode(NEAREST_NEWMV_PM) == 1);
    CHECK(isInterCompoundMode(NEW_NEARESTMV_PM) == 1);
    CHECK(isInterCompoundMode(NEAR_NEWMV_PM) == 1);
    CHECK(isInterCompoundMode(NEW_NEARMV_PM) == 1);
    CHECK(isInterCompoundMode(GLOBAL_GLOBALMV_PM) == 1);
    CHECK(isInterCompoundMode(NEW_NEWMV_PM) == 1);
    CHECK(isInterCompoundMode(NEWMV_PM) == 0);
    CHECK(isInterCompoundMode(GLOBALMV_PM) == 0);
    CHECK(isInterCompoundMode(DC_PRED_PM) == 0);
}

// ---------------------------------------------------------------------------
// A4M-6: setup_pred_plane / svt_av1_setup_pred_block (mode_decision.c
// :2051-2080).
// ---------------------------------------------------------------------------
using l16_rdo::Buf2D;
using l16_rdo::Yv12Lite;
using l16_rdo::setupPredPlane;
using l16_rdo::setupPredBlock;


TEST_CASE("l16_rdo setupPredPlane offsets the buffer and adjusts odd chroma mi") {
    // mode_decision.c:2051-2068. x = (MI_SIZE * mi_col) >> ssx,
    // y = (MI_SIZE * mi_row) >> ssy; the odd-mi step-back fires only when
    // the subsampling is set AND the mi count in that axis is 1.
    std::uint8_t mem[4096];
    for (int i = 0; i < 4096; ++i) {
        mem[i] = (std::uint8_t)(i * 7 + 3);
    }
    std::uint8_t* src = mem + 128;

    // Luma row: ss 0/0, 16x16, mi (3, 5), stride 64.
    // buf = src + (3*4)*64 + 5*4 = src + 768 + 20.
    Buf2D d;
    setupPredPlane(&d, l16_rdo::BLOCK_16X16, src, 16, 16, 64, 3, 5, 0, 0);
    CHECK(d.buf == src + (3 * MI_SIZE_MD) * 64 + 5 * MI_SIZE_MD);
    CHECK(d.buf0 == src);
    CHECK(d.width == 16);
    CHECK(d.height == 16);
    CHECK(d.stride == 64);

    // Chroma row (ss_y = 1) at an ODD mi_row on a mi_size_high == 1 shape
    // (4x4 and 16x4 are the two; miSizeHigh == 1). mi_row steps back one
    // BEFORE the scale: y = (4 * (mi_row - 1)) >> 1.
    setupPredPlane(&d, l16_rdo::BLOCK_4X4, src, 8, 8, 64, 5, 4, 1, 1);
    CHECK(d.buf == src + (((4 * (5 - 1)) >> 1) * 64) + ((4 * 4) >> 1));
    setupPredPlane(&d, l16_rdo::BLOCK_16X4, src, 8, 8, 64, 5, 4, 1, 1);
    CHECK(d.buf == src + (((4 * (5 - 1)) >> 1) * 64) + ((4 * 4) >> 1));

    // Same odd mi_row on a mi_size_high == 2 shape (8x8): NO step-back.
    setupPredPlane(&d, l16_rdo::BLOCK_8X8, src, 8, 8, 64, 5, 4, 1, 1);
    CHECK(d.buf == src + (((4 * 5) >> 1) * 64) + ((4 * 4) >> 1));

    // Chroma col (ss_x = 1) at an ODD mi_col on a mi_size_wide == 1 shape
    // (4x4 and 4x16): mi_col steps back.
    setupPredPlane(&d, l16_rdo::BLOCK_4X4, src, 8, 8, 64, 4, 7, 1, 1);
    CHECK(d.buf == src + (((4 * 4) >> 1) * 64) + ((4 * (7 - 1)) >> 1));
    setupPredPlane(&d, l16_rdo::BLOCK_4X16, src, 8, 8, 64, 4, 7, 1, 1);
    CHECK(d.buf == src + (((4 * 4) >> 1) * 64) + ((4 * (7 - 1)) >> 1));

    // Even mi + subsampling: no step-back on either axis.
    setupPredPlane(&d, l16_rdo::BLOCK_4X4, src, 8, 8, 64, 4, 6, 1, 1);
    CHECK(d.buf == src + (((4 * 4) >> 1) * 64) + ((4 * 6) >> 1));

    // The step-back is gated on the subsampling flag: odd mi on a
    // mi_size == 1 shape with ss 0 keeps the raw mi.
    setupPredPlane(&d, l16_rdo::BLOCK_4X4, src, 8, 8, 64, 5, 7, 0, 0);
    CHECK(d.buf == src + (4 * 5) * 64 + 4 * 7);
}

TEST_CASE("l16_rdo setupPredBlock wires the three planes and offsets the luma") {
    // mode_decision.c:2070-2080. dst[0] = luma (buf/buf0/width/height/
    // stride via setup_pred_plane ss 0/0); dst[1]/dst[2] get the u/v
    // buffers and the uv_stride, and are NOT re-offset (the reference
    // passes the whole array to setup_pred_plane, which writes dst[0]'s
    // fields only).
    std::uint8_t memY[1024], memU[256], memV[256];
    Yv12Lite yv;
    yv.yBuffer = memY;
    yv.uBuffer = memU;
    yv.vBuffer = memV;
    yv.yStride = 32;
    yv.uvStride = 16;
    yv.yCropWidth = 20;
    yv.yCropHeight = 12;

    Buf2D dst[MAX_PLANES_MD];
    setupPredBlock(l16_rdo::BLOCK_16X16, dst, &yv, 2, 3);

    CHECK(dst[0].buf == memY + (2 * MI_SIZE_MD) * 32 + 3 * MI_SIZE_MD);
    CHECK(dst[0].buf0 == memY);
    CHECK(dst[0].width == 20);
    CHECK(dst[0].height == 12);
    CHECK(dst[0].stride == 32);
    // The chroma slots: buf wired, stride = uv_stride, the rest untouched
    // by the reference's own shape (setup_pred_plane writes dst[0] only).
    CHECK(dst[1].buf == memU);
    CHECK(dst[2].buf == memV);
    CHECK(dst[1].stride == 16);
    CHECK(dst[2].stride == 16);
}

// ---------------------------------------------------------------------------
// A4M-7: svt_aom_get_blk_var_map (product_coding_loop.c:8054-8099) +
// is_dc_only_safe (mode_decision.c:847-881).
// ---------------------------------------------------------------------------
using l16_rdo::getBlkVarMap;
using l16_rdo::isDcOnlySafe;
using l16_rdo::DcOnlyCarrier;
using l16_rdo::Part;
using l16_rdo::PART_N_MD;
using l16_rdo::PART_H_MD;

TEST_CASE("l16_rdo getBlkVarMap enumerates the four levels") {
    // product_coding_loop.c:8054-8099. lvl = 6 - get_msb(block_size):
    // 64 -> 0, 32 -> 1, 16 -> 2, 8 -> 3; the var_base_lut = {0, 1, 5, 21},
    // var_grid_lut = {1, 2, 4, 8}, var_log2_lut = {6, 5, 4, 3}.
    int blk = -1, sub[4] = {-1, -1, -1, -1};
    // 64x64 at SB origin: base 0, grid 1 -> blk_idx 0. The sub walk uses
    // the SUB-level shift (5): sx = sy = 0, sub base 1 grid 2.
    getBlkVarMap(64, 0, 0, &blk, sub);
    CHECK(blk == 0);
    CHECK(sub[0] == 1);
    CHECK(sub[1] == 2);
    CHECK(sub[2] == 3);
    CHECK(sub[3] == 4);
    // 64x64 in the right-bottom SB quadrant (org 32,32): blk_idx 0; the
    // sub sx/sy = 32>>5 = 1.
    getBlkVarMap(64, 32, 32, &blk, sub);
    CHECK(blk == 0);
    CHECK(sub[0] == 1 + (1 + 0) * 2 + (1 + 0));
    // 32x32 at org (32, 0): shift 5, grid 2, base 1 -> gx = 1, gy = 0 ->
    // blk_idx = 2. The sub shift is 4: sx = 2, sy = 0 (base 5, grid 4).
    getBlkVarMap(32, 32, 0, &blk, sub);
    CHECK(blk == 2);
    CHECK(sub[0] == 5 + 0 * 4 + 2);
    CHECK(sub[1] == 5 + 0 * 4 + 3);
    CHECK(sub[2] == 5 + 1 * 4 + 2);
    CHECK(sub[3] == 5 + 1 * 4 + 3);
    // 16x16 at org (16, 48): shift 4, grid 4, base 5 -> gx = 1, gy = 3 ->
    // blk_idx = 18. The sub shift is 3: sx = 2, sy = 6 (base 21, grid 8).
    getBlkVarMap(16, 16, 48, &blk, sub);
    CHECK(blk == 18);
    CHECK(sub[0] == 21 + (6 + 0) * 8 + (2 + 0));
    CHECK(sub[1] == 21 + (6 + 0) * 8 + (2 + 1));
    CHECK(sub[2] == 21 + (6 + 1) * 8 + (2 + 0));
    CHECK(sub[3] == 21 + (6 + 1) * 8 + (2 + 1));
    // 8x8 at org (24, 8): shift 3, grid 8, base 21 -> blk = 21 + 1*8 + 3
    // = 32; sub_lvl = 4 is OUT of the lut -> sub zeroed.
    getBlkVarMap(8, 24, 8, &blk, sub);
    CHECK(blk == 32);
    CHECK(sub[0] == 0);
    CHECK(sub[1] == 0);
    CHECK(sub[2] == 0);
    CHECK(sub[3] == 0);
    // Out-of-range block_size (4): the safe sentinel row.
    getBlkVarMap(4, 0, 0, &blk, sub);
    CHECK(blk == 0);
    CHECK(sub[0] == 0);
    CHECK(sub[3] == 0);
}

TEST_CASE("l16_rdo isDcOnlySafe gates on the SB/shape/size and variance spread") {
    // mode_decision.c:847-881. sbVar layout: the flat per-SB row
    // (pcs->ppcs->variance[sb_index]); the 8x8 row returns blk_var < 2000
    // with NO spread check; 16x16+ folds min/max over the four sub_idx
    // entries and requires spread < 4000 as well.
    std::uint16_t sbVar[64];
    for (int i = 0; i < 64; ++i) {
        sbVar[i] = (std::uint16_t)(100 + i);
    }

    // The all-allowed carrier: prune on, SB64, PART_N, 16x16 block at SB
    // origin. blk_idx = 5 + 0*4 + 0 = 5 -> var 105; subs at base 21:
    // 121..124 (row 0: 21,22,29,30? no: sub grid 8 -> 21+0, 21+1, 21+8,
    // 21+9 = 121,122,129,130). Wait: sub_idx = sub_base + (sy+i)*8 +
    // (sx+j) = 21+0, 21+1, 21+8, 21+9. sbVar[21] = 121, [22] = 122,
    // [29] = 129, [30] = 130.
    DcOnlyCarrier c;
    c.pruneUsingEdgeInfo = 1;
    c.superBlockSize = 64;
    c.shape = PART_N_MD;
    c.sqSize = 16;
    c.blkOrgX = 0;
    c.blkOrgY = 0;
    c.sbOriginX = 0;
    c.sbOriginY = 0;
    c.sbIndex = 0;
    c.sbVar = sbVar;
    // blk_var 105 < 2000; spread = 130 - 121 = 9 < 4000 -> safe.
    CHECK(isDcOnlySafe(c) == 1);

    // The first gate rows (:849-852), each flips to 0:
    c.pruneUsingEdgeInfo = 0;
    CHECK(isDcOnlySafe(c) == 0);
    c.pruneUsingEdgeInfo = 1;
    c.superBlockSize = 128;
    CHECK(isDcOnlySafe(c) == 0);
    c.superBlockSize = 64;
    c.shape = PART_H_MD;  // shape != PART_N
    CHECK(isDcOnlySafe(c) == 0);
    c.shape = PART_N_MD;

    // sq_size == 4: the no-variance early-out (:850-852).
    c.sqSize = 4;
    CHECK(isDcOnlySafe(c) == 0);
    c.sqSize = 16;

    // The 8x8 row (:864-866): blk_var < 2000 -> 1, >= 2000 -> 0, no spread.
    c.sqSize = 8;
    c.blkOrgX = 40;
    c.blkOrgY = 8;  // blk_idx = 21 + 1*8 + 5 = 34 -> var 134 < 2000 -> 1.
    CHECK(isDcOnlySafe(c) == 1);
    sbVar[34] = 2000;  // the boundary itself is NOT < 2000 -> 0.
    CHECK(isDcOnlySafe(c) == 0);
    sbVar[34] = 134;
    // Spread does not matter at 8x8: set the (unused) sub rows extreme.
    sbVar[60] = 65535;
    sbVar[61] = 65535;
    CHECK(isDcOnlySafe(c) == 1);

    // The 16x16 spread row (:869-881): blk_var < 2000 but spread >= 4000
    // -> 0. 16x16 at org (32, 16): blk_idx = 5 + 1*4 + 2 = 11 -> 111;
    // subs = 21 + (2..3)*8 + (4..5) = 41,42,49,50 -> 141,142,149,150.
    c.sqSize = 16;
    c.blkOrgX = 32;
    c.blkOrgY = 16;
    CHECK(isDcOnlySafe(c) == 1);
    sbVar[42] = 5200;
    sbVar[49] = 5300;
    sbVar[50] = 5400;
    // blk_var 111 < 2000; sbVar[41] = 141 stays the MIN: spread =
    // 5400 - 141 = 5259 >= 4000 -> 0.
    CHECK(isDcOnlySafe(c) == 0);
    sbVar[42] = 4000;
    sbVar[49] = 4100;
    sbVar[50] = 4140;
    // max = 4140, spread = 4140 - 141 = 3999 < 4000 -> 1 again.
    CHECK(isDcOnlySafe(c) == 1);
    // The blk_var boundary: < 2000 strict.
    sbVar[11] = 2000;
    CHECK(isDcOnlySafe(c) == 0);
    sbVar[11] = 1999;
    CHECK(isDcOnlySafe(c) == 1);
}

// ---------------------------------------------------------------------------
// A4M-8: svt_aom_choose_best_av1_mv_pred (mode_decision.c:554-645) over the
// three carries: svt_av1_mv_bit_cost_light (rd_cost.c:62-68), av1_drl_ctx
// (rd_cost.h:85-89) and svt_aom_get_av1_mv_pred_drl (adaptive_mv_pred.c
// :1407-1457).
// ---------------------------------------------------------------------------
using l16_rdo::CandidateMv;
using l16_rdo::chooseBestAv1MvPred;
using l16_rdo::mvBitCostLight;
using l16_rdo::av1DrlCtx;
using l16_rdo::getAv1MvPredDrl;

TEST_CASE("l16_rdo mvBitCostLight pins the 1296 + 50x|diff| shape") {
    // rd_cost.c:62-68: factor 50, base 1296, the ABS component sum.
    Mv mv, ref;
    mv.x = 12;
    mv.y = -30;
    ref.x = 10;
    ref.y = -25;
    CHECK(mvBitCostLight(&mv, &ref) == 1296 + 50 * (2 + 5));
    CHECK(mvBitCostLight(&mv, &mv) == 1296);
    // The signs are absolute-differenced on both components.
    Mv a, b;
    a.x = -4;
    a.y = 4;
    b.x = 4;
    b.y = -4;
    CHECK(mvBitCostLight(&a, &b) == 1296 + 50 * (8 + 8));
}

TEST_CASE("l16_rdo av1DrlCtx pins the four weight-region rows") {
    // rd_cost.h:85-89: w_i >= REF_CAT_LEVEL(640) ? (w_j >= 640 ? 0 : 1)
    // : (w_j < 640 ? 2 : 0).
    CandidateMv stack[MAX_REF_MV_STACK_SIZE_MD];
    for (int i = 0; i < MAX_REF_MV_STACK_SIZE_MD; ++i) {
        stack[i].weight = 1000;
    }
    CHECK(av1DrlCtx(stack, 0) == 0);  // (1000, 1000)
    stack[1].weight = 100;
    CHECK(av1DrlCtx(stack, 0) == 1);  // (1000, 100)
    stack[0].weight = 100;
    CHECK(av1DrlCtx(stack, 0) == 2);  // (100, 100)
    stack[1].weight = 1000;
    CHECK(av1DrlCtx(stack, 0) == 0);  // (100, 1000)
    // The boundary itself is >= (REF_CAT_LEVEL included in the high band).
    stack[0].weight = 640;
    stack[1].weight = 639;
    CHECK(av1DrlCtx(stack, 0) == 1);
    stack[1].weight = 640;
    CHECK(av1DrlCtx(stack, 0) == 0);
}

TEST_CASE("l16_rdo getAv1MvPredDrl pins the mode x compound selection") {
    // adaptive_mv_pred.c:1407-1457. The ctx->ref_mv_stack wall threads as
    // the stack array; blk_ptr->av1xd->ref_mv_count as the count array.
    CandidateMv stack[29][MAX_REF_MV_STACK_SIZE_MD];
    for (int r = 0; r < 29; ++r) {
        for (int s = 0; s < MAX_REF_MV_STACK_SIZE_MD; ++s) {
            stack[r][s].this_mv.as_int = (std::uint32_t)((100 * (s + 1)) | ((200 * (s + 1)) << 16));
            stack[r][s].comp_mv.as_int = (std::uint32_t)((-300 * (s + 1)) | ((400 * (s + 1)) << 16));
            stack[r][s].weight = 1000;
        }
    }
    std::uint8_t count[29];
    for (int r = 0; r < 29; ++r) {
        count[r] = 3;
    }

    Mv nearest[2], near[2], ref[2];
    nearest[0].as_int = nearest[1].as_int = 0;
    near[0].as_int = near[1].as_int = 0;
    ref[0].as_int = ref[1].as_int = 0;
    // Unipred NEWMV: nearest = stack[0].this, near = stack[1].this; ref =
    // stack[drl].this when count > 1 (NEWMV); the [1] slots stay the
    // zero-init (nearestmv[1] is read but never written for unipred).
    getAv1MvPredDrl(stack, count, 1 /*LAST*/, 0, NEWMV_PM, 2, nearest, near, ref);
    CHECK(nearest[0].as_int == stack[1][0].this_mv.as_int);
    CHECK(nearest[1].as_int == 0);
    CHECK(near[0].as_int == stack[1][1].this_mv.as_int);
    CHECK(near[1].as_int == 0);
    CHECK(ref[0].as_int == stack[1][2].this_mv.as_int);
    CHECK(ref[1].as_int == 0);
    // Unipred NEWMV with count == 1: the ref stays nearestmv[0].
    count[1] = 1;
    getAv1MvPredDrl(stack, count, 1, 0, NEWMV_PM, 0, nearest, near, ref);
    CHECK(ref[0].as_int == stack[1][0].this_mv.as_int);
    count[1] = 3;
    // Unipred NEARMV at drl 0: near = stack[1].this (the else-if needs
    // drl > 0).
    getAv1MvPredDrl(stack, count, 1, 0, NEARMV_PM, 0, nearest, near, ref);
    CHECK(ref[0].as_int == stack[1][0].this_mv.as_int);
    CHECK(near[0].as_int == stack[1][1].this_mv.as_int);
    // Unipred NEARMV at drl 2: near = stack[3].this.
    getAv1MvPredDrl(stack, count, 1, 0, NEARMV_PM, 2, nearest, near, ref);
    CHECK(near[0].as_int == stack[1][3].this_mv.as_int);
    CHECK(ref[0].as_int == stack[1][0].this_mv.as_int);
    // Unipred GLOBALMV: nothing is written; ref stays the zero-init.
    nearest[0].as_int = 0xDEADBEEF;
    nearest[1].as_int = 0xCAFEBABE;
    near[0].as_int = 0x11111111;
    near[1].as_int = 0x22222222;
    getAv1MvPredDrl(stack, count, 1, 0, GLOBALMV_PM, 0, nearest, near, ref);
    CHECK(nearest[0].as_int == 0xDEADBEEF);
    CHECK(near[0].as_int == 0x11111111);
    CHECK(ref[0].as_int == 0xDEADBEEF);
    CHECK(ref[1].as_int == 0xCAFEBABE);
    // Compound NEAREST_NEARESTMV: nearest = stack[0] pair; near = stack
    // [drl+1] pair; ref = nearest pair (ref0/ref1 modes are NEAREST).
    getAv1MvPredDrl(stack, count, 16, 1, NEAREST_NEARESTMV_PM, 1, nearest, near, ref);
    CHECK(ref[0].as_int == stack[16][0].this_mv.as_int);
    CHECK(ref[1].as_int == stack[16][0].comp_mv.as_int);
    CHECK(near[0].as_int == stack[16][2].this_mv.as_int);
    CHECK(near[1].as_int == stack[16][2].comp_mv.as_int);
    // Compound NEW_NEWMV at drl 1: ref = stack[1] pair (both ref modes are
    // NEWMV, ref_mv_idx = drl).
    getAv1MvPredDrl(stack, count, 16, 1, NEW_NEWMV_PM, 1, nearest, near, ref);
    CHECK(ref[0].as_int == stack[16][1].this_mv.as_int);
    CHECK(ref[1].as_int == stack[16][1].comp_mv.as_int);
    // Compound NEAR_NEWMV at drl 0: the special case lifts ref_mv_idx to
    // 1 + drl; compound_ref0_mode(NEAR_NEWMV) = NEARMV (not NEWMV), so
    // ref[0] KEEPS the nearestmv slot (stack[0]); only ref[1] is lifted
    // (compound_ref1_mode = NEWMV -> stack[1].comp_mv).
    getAv1MvPredDrl(stack, count, 16, 1, NEAR_NEWMV_PM, 0, nearest, near, ref);
    CHECK(near[0].as_int == stack[16][1].this_mv.as_int);
    CHECK(ref[0].as_int == stack[16][0].this_mv.as_int);
    CHECK(ref[1].as_int == stack[16][1].comp_mv.as_int);
    // Compound NEW_NEARMV at drl 1: the mirror - ref[0] lifts (ref0 mode =
    // NEWMV, idx = 1 + drl = 2), ref[1] keeps nearest (ref1 mode = NEARMV).
    getAv1MvPredDrl(stack, count, 16, 1, NEW_NEARMV_PM, 1, nearest, near, ref);
    CHECK(ref[0].as_int == stack[16][2].this_mv.as_int);
    CHECK(ref[1].as_int == stack[16][0].comp_mv.as_int);
    // Compound GLOBAL_GLOBALMV: only ref = nearest pair.
    getAv1MvPredDrl(stack, count, 16, 1, GLOBAL_GLOBALMV_PM, 1, nearest, near, ref);
    CHECK(ref[0].as_int == stack[16][0].this_mv.as_int);
    CHECK(ref[1].as_int == stack[16][0].comp_mv.as_int);
}

TEST_CASE("l16_rdo chooseBestAv1MvPred walks the DRL cost loop") {
    // mode_decision.c:554-645. Hand-computed expectations from the
    // reference arithmetic (the independent oracle): the light cost is
    // 1296 + 50*|diff| (rd_cost.c:62-68); the full cost is
    // ROUND_POWER_OF_TWO(mv_cost*108, 7) with mv_cost = joint + comp rows;
    // the NEW-mode drl bits add drl_mode_fac_bits[drl_ctx][drli != idx].
    CandidateMv stack[29][MAX_REF_MV_STACK_SIZE_MD];
    for (int r = 0; r < 29; ++r) {
        for (int s = 0; s < MAX_REF_MV_STACK_SIZE_MD; ++s) {
            stack[r][s].this_mv.as_int = 0;
            stack[r][s].comp_mv.as_int = 0;
            stack[r][s].weight = 1000;
        }
    }
    // ref frame slot 1 (LAST): stack[0] = (0,0), stack[1] = (8,0),
    // stack[2] = (16,0).
    stack[1][0].this_mv.x = 0;
    stack[1][0].this_mv.y = 0;
    stack[1][1].this_mv.x = 8;
    stack[1][1].this_mv.y = 0;
    stack[1][2].this_mv.x = 16;
    stack[1][2].this_mv.y = 0;
    stack[1][0].comp_mv.x = 4;
    stack[1][0].comp_mv.y = 4;
    stack[1][1].comp_mv.x = 12;
    stack[1][1].comp_mv.y = 4;
    stack[1][2].comp_mv.x = 20;
    stack[1][2].comp_mv.y = 4;
    // The compound slot (ref frame type 16 = LAST|ALTREF): mirror rows so
    // the compound loop discriminates (stack[16][1] holds the injected
    // pair).
    stack[16][0].this_mv.x = 0;
    stack[16][0].this_mv.y = 0;
    stack[16][1].this_mv.x = 8;
    stack[16][1].this_mv.y = 0;
    stack[16][2].this_mv.x = 16;
    stack[16][2].this_mv.y = 0;
    stack[16][0].comp_mv.x = 4;
    stack[16][0].comp_mv.y = 4;
    stack[16][1].comp_mv.x = 12;
    stack[16][1].comp_mv.y = 4;
    stack[16][2].comp_mv.x = 20;
    stack[16][2].comp_mv.y = 4;
    std::uint8_t count[29];
    for (int r = 0; r < 29; ++r) {
        count[r] = 3;
    }
    // The cost tables: joint {100, 200, 300, 400}; comp rows |i|.
    std::vector<std::int32_t> joint = {100, 200, 300, 400};
    std::vector<std::int32_t> compRow(2 * MV_UPP + 1);
    for (int i = -MV_UPP; i <= MV_UPP; ++i) {
        compRow[i + MV_UPP] = i < 0 ? -i : i;
    }
    const std::int32_t* compPtr[2] = {compRow.data() + MV_UPP, compRow.data() + MV_UPP};
    // drl_mode_fac_bits[ctx][b]: all ctx rows {10, 20}.
    const std::int32_t drlFac[DRL_MODE_CONTEXTS_MD][2] = {{10, 20}, {10, 20}, {10, 20}};

    std::uint8_t drl = 0xAA;
    Mv pred[2];
    pred[0].as_int = 0xAAAAAAAA;
    pred[1].as_int = 0xBBBBBBBB;
    Mv mv0;
    mv0.as_int = 0;
    Mv mv1;
    mv1.as_int = 0;

    // Branch 1 (:560-562): shut_fast_rate -> outputs untouched.
    chooseBestAv1MvPred(stack, count, true, 0, joint.data(), compPtr, drlFac, 1, NEWMV_PM, mv0, mv1, &drl, pred);
    CHECK(drl == 0xAA);
    CHECK(pred[0].as_int == 0xAAAAAAAA);

    // Branch 2 (:563-568): approx_inter_rate > 1 -> drl 0 + stack[0] pair.
    chooseBestAv1MvPred(stack, count, false, 2, joint.data(), compPtr, drlFac, 1, NEWMV_PM,
                                 mv0, mv1, &drl, pred);
    CHECK(drl == 0);
    CHECK(pred[0].as_int == stack[1][0].this_mv.as_int);
    CHECK(pred[1].as_int == stack[1][0].comp_mv.as_int);

    // Branch 3 (:587-591): max_drl_index == 1 (NEWMV, count 1) -> drl 0 +
    // stack[0].
    count[1] = 1;
    chooseBestAv1MvPred(stack, count, false, 0, joint.data(), compPtr, drlFac, 1, NEWMV_PM,
                                 mv0, mv1, &drl, pred);
    CHECK(drl == 0);
    CHECK(pred[0].as_int == stack[1][0].this_mv.as_int);
    CHECK(pred[1].as_int == stack[1][0].comp_mv.as_int);
    count[1] = 3;

    // The light-cost loop (approx_inter_rate == 1), NEWMV, mv0 = stack[1].
    // Per-drli (ref = stack[drli].this_mv, compound_ref0 NEWMV arm):
    //   drl 0: 1296 + 50*8 + fac[0][0] = 1296+400+10 = 1706
    //   drl 1: 1296 + 50*0 + fac[0][1] + fac[0][0] = 1296+30 = 1326
    //   drl 2: 1296 + 50*16 + fac[0][1] + fac[0][1] = 1296+800+40 = 2136
    mv0 = stack[1][1].this_mv;
    mv1.as_int = 0;
    drl = 0xAA;
    pred[0].as_int = 0xAAAAAAAA;
    pred[1].as_int = 0xBBBBBBBB;
    chooseBestAv1MvPred(stack, count, false, 1, joint.data(), compPtr, drlFac, 1, NEWMV_PM,
                                 mv0, mv1, &drl, pred);
    CHECK(drl == 1);
    CHECK(pred[0].as_int == stack[1][1].this_mv.as_int);
    CHECK(pred[1].as_int == 0);  // the unipred ref_mv[1] stays the zero-init

    // mv0 = stack[0] (the nearest): drl 0 wins (1296+10).
    mv0 = stack[1][0].this_mv;
    chooseBestAv1MvPred(stack, count, false, 1, joint.data(), compPtr, drlFac, 1, NEWMV_PM,
                                 mv0, mv1, &drl, pred);
    CHECK(drl == 0);
    CHECK(pred[0].as_int == stack[1][0].this_mv.as_int);

    // The full-cost loop (approx_inter_rate == 0), NEWMV, mv0 = stack[1]:
    //   drl 0: mv_cost = 100 + 8 = 108 -> rate (108*108+64)>>7 = 91; +10
    //          = 101
    //   drl 1: mv_cost = 100 + 0 = 100 -> rate 84; +20+10 = 114
    //   drl 2: mv_cost = 100 + 8 = 108 -> rate 91; +20+20 = 131
    mv0 = stack[1][1].this_mv;
    chooseBestAv1MvPred(stack, count, false, 0, joint.data(), compPtr, drlFac, 1, NEWMV_PM,
                                 mv0, mv1, &drl, pred);
    CHECK(drl == 1);
    CHECK(pred[0].as_int == stack[1][1].this_mv.as_int);
    // mv0 = stack[0]: drl 0 wins (94 vs 121 vs 156).
    mv0 = stack[1][0].this_mv;
    chooseBestAv1MvPred(stack, count, false, 0, joint.data(), compPtr, drlFac, 1, NEWMV_PM,
                                 mv0, mv1, &drl, pred);
    CHECK(drl == 0);

    // The compound NEW_NEWMV loop (light cost): both components cost.
    // mv0 = stack[1].this (8,0), mv1 = stack[1].comp (12,4):
    //   drl 1: (1296 + 0) + (1296 + 0) + 30 = 2646+30 = 2676? The
    //   reference: mv_rate = cost(mv0) then += cost(mv1) then += fac -> the
    //   two 1296 bases BOTH count.
    //   drl 1 total = 1296 + 1296 + 30 = 2622+... = 2622 + 0? Recompute:
    //   1296 + 0 + 1296 + 0 + 30 = 2622.
    //   drl 0: 1296 + 50*8 + 1296 + 50*8 + 10 = 1296+400+1296+400+10 = 3402
    //   drl 2: 1296 + 50*8 + 1296 + 50*8 + 40 = 3432
    mv0 = stack[1][1].this_mv;
    mv1 = stack[1][1].comp_mv;
    chooseBestAv1MvPred(stack, count, false, 1, joint.data(), compPtr, drlFac, 16, NEW_NEWMV_PM,
                                 mv0, mv1, &drl, pred);
    CHECK(drl == 1);
    CHECK(pred[0].as_int == stack[16][1].this_mv.as_int);
    CHECK(pred[1].as_int == stack[16][1].comp_mv.as_int);

    // max_drl_index == 0 (NEARESTMV): the loop body never runs and BOTH
    // outputs keep their input values (:587 the == 1 test, the else loop
    // bounds drli < 0).
    drl = 0x5A;
    pred[0].as_int = 0x1357;
    chooseBestAv1MvPred(stack, count, false, 1, joint.data(), compPtr, drlFac, 1, NEARESTMV_PM,
                        mv0, mv1, &drl, pred);
    CHECK(drl == 0x5A);
    CHECK(pred[0].as_int == 0x1357);

    // The exact-tie row (the strict < at :637): NEARMV never overrides
    // ref_mv (both drl slots cost against stack[0]) and NEARMV carries no
    // drl fac bits (new_mv false) - with mv0 = stack[0] both slots cost
    // exactly 1296 at approx_inter_rate 1. The reference keeps the FIRST
    // slot (drl 0); a <= tie-break would flip to drl 1.
    mv0 = stack[1][0].this_mv;
    mv1.as_int = 0;
    drl = 0xAA;
    pred[0].as_int = 0xAAAAAAAA;
    chooseBestAv1MvPred(stack, count, false, 1, joint.data(), compPtr, drlFac, 1, NEARMV_PM,
                        mv0, mv1, &drl, pred);
    CHECK(drl == 0);
    CHECK(pred[0].as_int == stack[1][0].this_mv.as_int);
}
