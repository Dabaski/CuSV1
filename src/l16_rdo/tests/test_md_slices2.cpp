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
using l16_rdo::INPUT_SIZE_720p_RANGE;
using l16_rdo::INPUT_SIZE_1080p_RANGE;

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
