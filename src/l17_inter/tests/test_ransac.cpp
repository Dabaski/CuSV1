#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "ransac.h"

// A3W - the RANSAC surface. Every expectation derives from the vendored
// reference (third_party/SVT-AV1/Source/Lib/Codec/ransac.c, "rs";
// mathutils.h, "mu"; random.h, "rd") with the line quoted. The lcg /
// linsolve / least-squares helpers are the reference's own header-inline
// dependencies of ransac.c (random.h, mathutils.h), carried as part of this
// port because ransac.c cannot compile without them.

namespace {

// Independent transcription of lcg_next (rd:23-:26).
std::uint32_t oracleLcgNext(std::uint32_t* state) {
    *state = (std::uint32_t)(*state * 1103515245ULL + 12345);
    return *state;
}

// Independent transcription of lcg_pick (rd:56-:76).
void oracleLcgPick(int n, int k, int* out, std::uint32_t* seed) {
    for (int i = 0; i < k; i++) {
    resample:
        std::uint64_t v = ((std::uint64_t)oracleLcgNext(seed) * n) >> 32;
        int vi = (int)v;
        for (int j = 0; j < i; j++) {
            if (vi == out[j]) goto resample;
        }
        out[i] = vi;
    }
}

} // namespace

TEST_CASE("lcg_next follows the 1103515245/12345 recurrence (rd:23-:26)") {
    std::uint32_t state = 24;
    std::uint32_t oracleState = 24;
    int mismatches = 0;
    for (int i = 0; i < 10000; ++i) {
        if (l17_inter::lcgNext(&state) != oracleLcgNext(&oracleState)) ++mismatches;
    }
    CHECK(mismatches == 0);
    // Wrap-around exercise: the multiplication is modulo 2^32 (the
    // uint32_t cast, not promotion).
    state = 0xFFFFFFFFu;
    oracleState = 0xFFFFFFFFu;
    CHECK(l17_inter::lcgNext(&state) == oracleLcgNext(&oracleState));
}

TEST_CASE("lcg_rand16 / lcg_randint match their oracle transcriptions (rd:29-:41)") {
    std::uint32_t state = 12345, ostate = 12345;
    int mismatches = 0;
    for (int i = 0; i < 2000; ++i) {
        const std::uint32_t got16 = l17_inter::lcgRand16(&state);
        const std::uint32_t want16 = (oracleLcgNext(&ostate) / 65536) % 32768;
        if (got16 != want16) ++mismatches;
        if (state != ostate) ++mismatches;
        const std::uint32_t gotInt = l17_inter::lcgRandint(&state, 97);
        const std::uint32_t wantInt = (std::uint32_t)(((std::uint64_t)oracleLcgNext(&ostate) * 97) >> 32);
        if (gotInt != wantInt) ++mismatches;
        if (state != ostate) ++mismatches;
    }
    CHECK(mismatches == 0);
    // randint is bounded: n = 1 always yields 0.
    std::uint32_t s = 7;
    for (int i = 0; i < 100; ++i) CHECK(l17_inter::lcgRandint(&s, 1) == 0);
}

TEST_CASE("lcg_pick matches the oracle and produces distinct indices (rd:56-:76)") {
    int mismatches = 0;
    for (int trial = 0; trial < 200; ++trial) {
        const int n = 5 + (trial * 13) % 200;
        const int k = 1 + trial % 4;
        std::uint32_t seed = (std::uint32_t)n * 31 + trial, oseed = seed;
        int got[4] = {0}, want[4] = {0};
        l17_inter::lcgPick(n, k, got, &seed);
        oracleLcgPick(n, k, want, &oseed);
        for (int i = 0; i < k; ++i)
            if (got[i] != want[i]) ++mismatches;
        if (seed != oseed) ++mismatches;
    }
    CHECK(mismatches == 0);
}

// ---------------------------------------------------------------- linsolve

TEST_CASE("linsolve solves a known 3x3 system exactly (mu:22-:65)") {
    // A = [[2, 1, 1], [1, 3, 2], [1, 0, 0]], b = [4, 5, 6].
    // Row 3 pins x0 = 6; substituting: x1 + x2 = -8, 3x1 + 2x2 = -1
    // -> x = [6, 15, -23] (and the residual check below re-verifies).
    double A[9] = {2, 1, 1, 1, 3, 2, 1, 0, 0};
    double b[3] = {4, 5, 6};
    double x[3] = {0, 0, 0};
    CHECK(l17_inter::linsolve(3, A, 3, b, x) == 1);
    CHECK(std::fabs(x[0] - 6.0) < 1e-9);
    CHECK(std::fabs(x[1] - 15.0) < 1e-9);
    CHECK(std::fabs(x[2] + 23.0) < 1e-9);
    // Verify A.x = b (independent of the expected vector).
    const double r0 = 2 * x[0] + 1 * x[1] + 1 * x[2];
    const double r1 = 1 * x[0] + 3 * x[1] + 2 * x[2];
    const double r2 = 1 * x[0] + 0 * x[1] + 0 * x[2];
    CHECK(std::fabs(r0 - 4.0) < 1e-9);
    CHECK(std::fabs(r1 - 5.0) < 1e-9);
    CHECK(std::fabs(r2 - 6.0) < 1e-9);
}

TEST_CASE("linsolve solves a system that requires row pivoting") {
    // The zero in A[0][0] forces the partial-pivoting swap.
    double A[9] = {0, 1, 1, 1, 0, 0, 0, 2, 1};
    double b[3] = {2, 1, 3};
    double x[3] = {0, 0, 0};
    CHECK(l17_inter::linsolve(3, A, 3, b, x) == 1);
    const double r0 = 0 * x[0] + 1 * x[1] + 1 * x[2];
    const double r1 = 1 * x[0] + 0 * x[1] + 0 * x[2];
    const double r2 = 0 * x[0] + 2 * x[1] + 1 * x[2];
    CHECK(std::fabs(r0 - 2.0) < 1e-9);
    CHECK(std::fabs(r1 - 1.0) < 1e-9);
    CHECK(std::fabs(r2 - 3.0) < 1e-9);
}

TEST_CASE("linsolve reports failure on a singular system (mu:42-:44, :53-:56)") {
    // Rank-deficient: row 2 = row 0 + row 1.
    double A[9] = {1, 2, 3, 2, 4, 6, 1, 1, 1};
    double b[3] = {1, 2, 3};
    double x[3] = {0, 0, 0};
    CHECK(l17_inter::linsolve(3, A, 3, b, x) == 0);
    // NEAR-singular with the ENTIRE first column tiny (rows [1e-20, 1] and
    // [3e-20, 1]): partial pivoting cannot heal it (both candidates are
    // below 1e-16), so the tiny_near_zero guard (mu:23, mu:42-:44) must
    // reject. The (1,1) entries differ so the elimination leaves a NON-zero
    // second diagonal (1 - 1/3 = 2/3), which is what makes the
    // backward-substitution guard (mu:53-:56) unable to catch a weakened
    // forward guard - this fixture kills the pivot == 0 mutation on the
    // return code alone.
    double A2[4] = {1e-20, 1, 3e-20, 1};
    double b2[2] = {1, 1};
    double x2[2] = {0, 0};
    CHECK(l17_inter::linsolve(2, A2, 2, b2, x2) == 0);
}

// ---------------------------------------------------------------- least squares

TEST_CASE("least_squares accumulates A'A and A'b and recovers the fit (mu:90-:108)") {
    // Fit dx = h0 + h1*sx over exact data: x = 1..5, dx = 3x + 1.
    double mat[4], y[2], x[2];
    l17_inter::leastSquaresInit(mat, y, 2);
    for (int i = 1; i <= 5; ++i) {
        const double a[2] = {1, (double)i};
        l17_inter::leastSquaresAccumulate(mat, y, a, 3.0 * i + 1, 2);
    }
    // The accumulator equals the closed-form A'A and A'b (independent math):
    // A'A = [[5, 15], [15, 55]], A'b = [sum(3i+1)=50, sum(i*(3i+1))=180].
    // Checked BEFORE the solve - linsolve destroys A in place.
    CHECK(std::fabs(mat[0] - 5.0) < 1e-9);
    CHECK(std::fabs(mat[1] - 15.0) < 1e-9);
    CHECK(std::fabs(mat[2] - 15.0) < 1e-9);
    CHECK(std::fabs(mat[3] - 55.0) < 1e-9);
    CHECK(std::fabs(y[0] - 50.0) < 1e-9);
    CHECK(std::fabs(y[1] - 180.0) < 1e-9);
    CHECK(l17_inter::leastSquaresSolve(mat, y, x, 2) == 1);
    CHECK(std::fabs(x[0] - 1.0) < 1e-9);
    CHECK(std::fabs(x[1] - 3.0) < 1e-9);
}

// ---------------------------------------------------------------- motion compare

TEST_CASE("compare_motions orders by inliers then sse (rs:39-:60)") {
    l17_inter::RansacMotion a{3, 2.0, nullptr};
    l17_inter::RansacMotion b{5, 2.0, nullptr};
    CHECK(l17_inter::compareMotions(&a, &b) == 1);  // fewer inliers -> worse
    CHECK(l17_inter::compareMotions(&b, &a) == -1); // more inliers -> better
    l17_inter::RansacMotion c{3, 1.5, nullptr};
    CHECK(l17_inter::compareMotions(&a, &c) == 1); // tie inliers, higher sse -> worse
    CHECK(l17_inter::compareMotions(&c, &a) == -1);
    l17_inter::RansacMotion d{3, 2.0, nullptr};
    CHECK(l17_inter::compareMotions(&a, &d) == 0); // identical -> tie
    CHECK(l17_inter::isBetterMotion(&b, &a) == true);
    CHECK(l17_inter::isBetterMotion(&a, &b) == false);
    CHECK(l17_inter::isBetterMotion(&a, &d) == false);
}

// ---------------------------------------------------------------- scoring

TEST_CASE("score_translation counts inliers under 1.25^2 and sums their sse (rs:62-:84)") {
    // True translation (3, -2). Points: two exact, one at (1,0) off (sse 1
    // < 1.5625: inlier), one at (2,0) off (sse 4: outlier), one at (0,2)
    // off (sse 4: outlier).
    l17_inter::Correspondence pts[5] = {{10, 10, 13, 8},
                                        {20, 30, 23, 28},
                                        {30, 40, 34, 38},  // (1, 0) off
                                        {40, 50, 45, 48},  // (2, 0) off
                                        {50, 60, 50, 62}}; // (0, 2) off
    const double mat[6] = {3.0, -2.0, 1.0, 0.0, 0.0, 1.0};
    l17_inter::RansacMotion model{0, 0.0, nullptr};
    int inlierIndices[5];
    model.inlier_indices = inlierIndices;
    l17_inter::scoreTranslation(mat, pts, 5, &model);
    CHECK(model.num_inliers == 3);
    CHECK(model.sse == 1.0); // only the (1,0) outlier contributes
    CHECK(model.inlier_indices[0] == 0);
    CHECK(model.inlier_indices[1] == 1);
    CHECK(model.inlier_indices[2] == 2);
}

TEST_CASE("score_affine projects through the full matrix (rs:86-:108)") {
    // 2x zoom + translate (1, 2): dest = (2x + 1, 2y + 2).
    l17_inter::Correspondence pts[4] = {{5, 5, 11, 12},
                                        {10, 20, 21, 42},
                                        {3, 7, 7, 16},   // (0, 0) off - exact
                                        {6, 9, 14, 21}}; // (1, 1) off, sse 2 -> outlier
    const double mat[6] = {1.0, 2.0, 2.0, 0.0, 0.0, 2.0};
    l17_inter::RansacMotion model{0, 0.0, nullptr};
    int inlierIndices[4];
    model.inlier_indices = inlierIndices;
    l17_inter::scoreAffine(mat, pts, 4, &model);
    CHECK(model.num_inliers == 3);
    CHECK(model.sse == 0.0);
    CHECK(model.inlier_indices[0] == 0);
    CHECK(model.inlier_indices[1] == 1);
    CHECK(model.inlier_indices[2] == 2);
}

// ---------------------------------------------------------------- finders

TEST_CASE("find_translation recovers the mean offset and identity diagonal (rs:110-:132)") {
    l17_inter::Correspondence pts[3] = {{10, 10, 13, 8}, {20, 30, 24, 27}, {30, 40, 32, 40}};
    const int indices[3] = {0, 1, 2};
    double params[6] = {0};
    CHECK(l17_inter::findTranslation(pts, indices, 3, params) == true);
    CHECK(std::fabs(params[0] - (3.0 + 4.0 + 2.0) / 3.0) < 1e-9);
    CHECK(std::fabs(params[1] - (-2.0 - 3.0 + 0.0) / 3.0) < 1e-9);
    CHECK(params[2] == 1.0);
    CHECK(params[3] == 0.0);
    CHECK(params[4] == 0.0);
    CHECK(params[5] == 1.0);
}

TEST_CASE("find_rotzoom recovers a known rotation+zoom (rs:134-:173)") {
    // dest = (tx + a*sx + b*sy, ty - b*sx + a*sy) with a = 1.5, b = 0.5,
    // tx = 2, ty = -3 (the complex-multiply form the LS rows encode); the
    // sx/sy values are even so every dest coordinate is an exact integer.
    const double a = 1.5, b = 0.5, tx = 2.0, ty = -3.0;
    l17_inter::Correspondence pts[4];
    const int sx[4] = {2, 8, 4, -2};
    const int sy[4] = {4, -2, 6, 2};
    for (int i = 0; i < 4; ++i) {
        pts[i].x = sx[i];
        pts[i].y = sy[i];
        pts[i].rx = (int)(tx + a * sx[i] + b * sy[i]);
        pts[i].ry = (int)(ty - b * sx[i] + a * sy[i]);
    }
    const int indices[4] = {0, 1, 2, 3};
    double params[6] = {0};
    CHECK(l17_inter::findRotzoom(pts, indices, 4, params) == true);
    CHECK(std::fabs(params[0] - tx) < 1e-9);
    CHECK(std::fabs(params[1] - ty) < 1e-9);
    CHECK(std::fabs(params[2] - a) < 1e-9);
    CHECK(std::fabs(params[3] - b) < 1e-9);
    // The rotzoom completion: params[4] = -params[3], params[5] = params[2].
    CHECK(std::fabs(params[4] + b) < 1e-9);
    CHECK(std::fabs(params[5] - a) < 1e-9);
}

TEST_CASE("find_affine recovers a known full affine via the two 3-dim subproblems (rs:175-:232)") {
    // dest_x = 1 + 2sx + 3sy; dest_y = 4 + 5sx + 6sy (integer-exact).
    l17_inter::Correspondence pts[3];
    const int sx[3] = {1, 3, 6};
    const int sy[3] = {2, 5, 9};
    for (int i = 0; i < 3; ++i) {
        pts[i].x = sx[i];
        pts[i].y = sy[i];
        pts[i].rx = 1 + 2 * sx[i] + 3 * sy[i];
        pts[i].ry = 4 + 5 * sx[i] + 6 * sy[i];
    }
    const int indices[3] = {0, 1, 2};
    double params[6] = {0};
    CHECK(l17_inter::findAffine(pts, indices, 3, params) == true);
    CHECK(std::fabs(params[0] - 1.0) < 1e-9);
    CHECK(std::fabs(params[1] - 4.0) < 1e-9);
    CHECK(std::fabs(params[2] - 2.0) < 1e-9);
    CHECK(std::fabs(params[3] - 3.0) < 1e-9);
    CHECK(std::fabs(params[4] - 5.0) < 1e-9);
    CHECK(std::fabs(params[5] - 6.0) < 1e-9);
}

TEST_CASE("find_rotzoom/find_affine fail on degenerate sample sets") {
    // Two identical points: the 4-dim LS system is singular.
    l17_inter::Correspondence pts[2] = {{5, 5, 9, 9}, {5, 5, 11, 7}};
    const int indices[2] = {0, 1};
    double params[6] = {0};
    CHECK(l17_inter::findRotzoom(pts, indices, 2, params) == false);
    // Collinear points: the two 3-dim systems are singular.
    l17_inter::Correspondence col[3] = {{1, 1, 2, 2}, {2, 2, 4, 4}, {3, 3, 6, 6}};
    const int ci[3] = {0, 1, 2};
    CHECK(l17_inter::findAffine(col, ci, 3, params) == false);
}
// ---------------------------------------------------------------- ransac

namespace {
// Build a correspondence set from a known translation with outliers whose
// reference points are ~1000 px away in DISTINCT directions (distinct so a
// translation fitted through one outlier cannot capture the others; they
// can never be inliers of the true model, and any model fitted through a
// single one of them scores below min_inliers).
void translationSet(l17_inter::Correspondence* pts, int npoints, int numOutliers, int tx, int ty) {
    const int outlierDx[4] = {1000, -1000, 500, -800};
    const int outlierDy[4] = {1000, 500, -1000, -900};
    for (int i = 0; i < npoints; ++i) {
        pts[i].x = 10 + 7 * i;
        pts[i].y = 20 - 3 * i;
        if (i < npoints - numOutliers) {
            pts[i].rx = pts[i].x + tx;
            pts[i].ry = pts[i].y + ty;
        } else {
            const int o = i - (npoints - numOutliers);
            pts[i].rx = pts[i].x + outlierDx[o];
            pts[i].ry = pts[i].y + outlierDy[o];
        }
    }
}
} // namespace

TEST_CASE("ransac recovers a known translation with 4/24 outliers (rs:428-:434)") {
    const int npoints = 24;
    l17_inter::Correspondence pts[24];
    translationSet(pts, npoints, 4, 3, -2);
    l17_inter::MotionModel models[1];
    int inliers[2 * 24];
    models[0].inliers = inliers;
    bool memAllocFailed = false;
    const bool ok = l17_inter::svtAomRansac(pts, npoints, l17_inter::Translation, models, 1, &memAllocFailed);
    CHECK(ok == true);
    CHECK(memAllocFailed == false);
    CHECK(models[0].num_inliers == 20);
    CHECK(std::fabs(models[0].params[0] - 3.0) < 1e-9);
    CHECK(std::fabs(models[0].params[1] + 2.0) < 1e-9);
    CHECK(std::fabs(models[0].params[2] - 1.0) < 1e-9);
    CHECK(std::fabs(models[0].params[5] - 1.0) < 1e-9);
    // The reported inliers are exactly the 20 non-outlier source points.
    int badInliers = 0;
    for (int j = 0; j < models[0].num_inliers; ++j) {
        const int ix = models[0].inliers[2 * j];
        const int iy = models[0].inliers[2 * j + 1];
        bool found = false;
        for (int i = 0; i < npoints - 4; ++i)
            if (pts[i].x == ix && pts[i].y == iy) found = true;
        if (!found) ++badInliers;
    }
    CHECK(badInliers == 0);
}

TEST_CASE("ransac recovers a known rotzoom with 4/24 outliers") {
    const int npoints = 24;
    l17_inter::Correspondence pts[24];
    // dest = (2 + 1.25sx + 0.25sy, -3 - 0.25sx + 1.25sy) with all source
    // coordinates multiples of 4, so every dest is an exact integer and the
    // refit is exact. The spread is 2-D (NOT collinear: the affine family's
    // 3-dim subproblems are rank-deficient on a line, pinned by the
    // find_affine degenerate case above).
    const double a = 1.25, b = 0.25, tx = 2.0, ty = -3.0;
    for (int i = 0; i < npoints; ++i) {
        pts[i].x = 8 + 4 * i;
        pts[i].y = 4 * ((3 * i) % 11);
        if (i < npoints - 4) {
            pts[i].rx = (int)(tx + a * pts[i].x + b * pts[i].y);
            pts[i].ry = (int)(ty - b * pts[i].x + a * pts[i].y);
        } else {
            pts[i].rx = pts[i].x + 1000;
            pts[i].ry = pts[i].y + 1000;
        }
    }
    l17_inter::MotionModel models[1];
    int inliers[2 * 24];
    models[0].inliers = inliers;
    bool memAllocFailed = false;
    const bool ok = l17_inter::svtAomRansac(pts, npoints, l17_inter::Rotzoom, models, 1, &memAllocFailed);
    CHECK(ok == true);
    CHECK(models[0].num_inliers == 20);
    CHECK(std::fabs(models[0].params[0] - tx) < 1e-9);
    CHECK(std::fabs(models[0].params[1] - ty) < 1e-9);
    CHECK(std::fabs(models[0].params[2] - a) < 1e-9);
    CHECK(std::fabs(models[0].params[3] - b) < 1e-9);
    CHECK(std::fabs(models[0].params[4] + b) < 1e-9);
    CHECK(std::fabs(models[0].params[5] - a) < 1e-9);
}

TEST_CASE("ransac recovers a known affine with 4/24 outliers") {
    const int npoints = 24;
    l17_inter::Correspondence pts[24];
    // dest_x = 1 + 2sx + 3sy; dest_y = 4 + 5sx + 6sy, on the same 2-D spread.
    for (int i = 0; i < npoints; ++i) {
        pts[i].x = 8 + 4 * i;
        pts[i].y = 4 * ((3 * i) % 11);
        if (i < npoints - 4) {
            pts[i].rx = 1 + 2 * pts[i].x + 3 * pts[i].y;
            pts[i].ry = 4 + 5 * pts[i].x + 6 * pts[i].y;
        } else {
            pts[i].rx = pts[i].x + 1000;
            pts[i].ry = pts[i].y + 1000;
        }
    }
    l17_inter::MotionModel models[1];
    int inliers[2 * 24];
    models[0].inliers = inliers;
    bool memAllocFailed = false;
    const bool ok = l17_inter::svtAomRansac(pts, npoints, l17_inter::Affine, models, 1, &memAllocFailed);
    CHECK(ok == true);
    CHECK(models[0].num_inliers == 20);
    CHECK(std::fabs(models[0].params[0] - 1.0) < 1e-9);
    CHECK(std::fabs(models[0].params[1] - 4.0) < 1e-9);
    CHECK(std::fabs(models[0].params[2] - 2.0) < 1e-9);
    CHECK(std::fabs(models[0].params[3] - 3.0) < 1e-9);
    CHECK(std::fabs(models[0].params[4] - 5.0) < 1e-9);
    CHECK(std::fabs(models[0].params[5] - 6.0) < 1e-9);
}

TEST_CASE("ransac rejects too-few-points input and leaves the identity fallback (rs:261-:263, :255-:259)") {
    // Translation needs minpts * MINPTS_MULTIPLIER = 5 points; 4 is too few.
    l17_inter::Correspondence pts[4] = {{0, 0, 1, 1}, {1, 0, 2, 1}, {0, 1, 1, 2}, {1, 1, 2, 2}};
    l17_inter::MotionModel models[1];
    int inliers[2 * 4];
    models[0].inliers = inliers;
    bool memAllocFailed = false;
    CHECK(l17_inter::svtAomRansac(pts, 4, l17_inter::Translation, models, 1, &memAllocFailed) == false);
    CHECK(memAllocFailed == false);
    CHECK(models[0].num_inliers == 0);
    CHECK(models[0].params[0] == 0.0);
    CHECK(models[0].params[2] == 1.0);
    CHECK(models[0].params[5] == 1.0);
    // npoints == 0: the same rejection.
    CHECK(l17_inter::svtAomRansac(pts, 0, l17_inter::Translation, models, 1, &memAllocFailed) == false);
}

TEST_CASE("ransac is deterministic for a fixed input (seed = npoints, rs:241)") {
    const int npoints = 24;
    l17_inter::Correspondence pts[24];
    translationSet(pts, npoints, 4, 3, -2);
    l17_inter::MotionModel m1[1], m2[1];
    int in1[2 * 24], in2[2 * 24];
    m1[0].inliers = in1;
    m2[0].inliers = in2;
    bool alloc1 = false, alloc2 = false;
    CHECK(l17_inter::svtAomRansac(pts, npoints, l17_inter::Translation, m1, 1, &alloc1));
    CHECK(l17_inter::svtAomRansac(pts, npoints, l17_inter::Translation, m2, 1, &alloc2));
    CHECK(m1[0].num_inliers == m2[0].num_inliers);
    for (int i = 0; i < 6; ++i) CHECK(m1[0].params[i] == m2[0].params[i]);
    for (int j = 0; j < 2 * m1[0].num_inliers; ++j) CHECK(in1[j] == in2[j]);
}

TEST_CASE("ransac with all-unique offsets leaves both slots at identity (min_inliers gate, rs:265-:305)") {
    // min_inliers = max((int)(0.1*npoints), minpts) = max(2, 1) = 2. Every
    // point here has a UNIQUE offset differing from every other by 2 px
    // (sse 4 > 1.5625), so each single-point trial model scores exactly
    // itself as its one inlier and is REJECTED. Both output slots must stay
    // at the identity fallback. This is the fixture that kills the
    // min_inliers = minpts mutation (which admits them: num_inliers == 1).
    const int npoints = 20;
    l17_inter::Correspondence pts[20];
    for (int i = 0; i < npoints; ++i) {
        pts[i].x = 10 + 7 * i;
        pts[i].y = 20 - 3 * i;
        pts[i].rx = pts[i].x + 2 * i; // offsets (0, 2, 4, ...): pairwise >= 2 px
        pts[i].ry = pts[i].y;
    }
    l17_inter::MotionModel models[2];
    int in0[2 * 20], in1[2 * 20];
    models[0].inliers = in0;
    models[1].inliers = in1;
    bool memAllocFailed = false;
    CHECK(l17_inter::svtAomRansac(pts, npoints, l17_inter::Translation, models, 2, &memAllocFailed) == true);
    CHECK(models[0].num_inliers == 0);
    CHECK(models[0].params[0] == 0.0);
    CHECK(models[0].params[2] == 1.0);
    CHECK(models[0].params[5] == 1.0);
    CHECK(models[1].num_inliers == 0);
    CHECK(models[1].params[2] == 1.0);
    CHECK(models[1].params[5] == 1.0);
}