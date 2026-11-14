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
