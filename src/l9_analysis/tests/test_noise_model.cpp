#include <doctest.h>
#include <analysis.h>

#include <cmath>
#include <vector>

// AN17: the flat-block finder + the noise model init/update
// (third_party/SVT-AV1/Source/Lib/Codec/noise_model.c:27-:102 helper span,
// :399-:634 flat-block finder, :636-:1157 noise model). Composes the AN16
// landed solver primitives - never re-implements them.
//
// TOLERANCE DISCIPLINE (the AN16 discipline, decided up front): every
// quantity here is DOUBLE precision with a deterministic call order, so the
// port vs the reference equations is near-bit-exact; the practical harness
// tolerance is 1e-12 RELATIVE (|actual - expected| <= 1e-12 * max(1,
// |expected|)), except where a fixture crosses an intentional threshold
// boundary - those fixtures are placed FAR from the boundary so the
// classification is not tolerance-sensitive. All expected values are
// hand-derived closed forms over the reference's own equations, written as
// literal arithmetic in the assertions - never copied from the port output.

namespace {

constexpr double kTol = 1e-12;

bool near(double actual, double expected) {
    return std::fabs(actual - expected) <= kTol * std::max(1.0, std::fabs(expected));
}

}  // namespace

// ---------------------------------------------------------------------------
// T1: getBlockMean (noise_model.c:39-62, the GET_BLOCK_MEAN lowbd
// instantiation). Hand-derived: 4x4 buffer of 0..15, 2x2 block at (1,1) ->
// mean(5, 6, 9, 10) = 7.5; the clamp at the bottom-right corner (3,3) with
// block_size 2 gives max_h = max_w = 1 -> mean = 15.
// ---------------------------------------------------------------------------
TEST_CASE("getBlockMean averages the clamped block region") {
    std::vector<std::uint8_t> data = {
        0,  1,  2,  3,
        4,  5,  6,  7,
        8,  9, 10, 11,
        12, 13, 14, 15};
    CHECK(near(analysis::getBlockMean(data.data(), 4, 4, 4, 1, 1, 2), 7.5));
    CHECK(near(analysis::getBlockMean(data.data(), 4, 4, 4, 3, 3, 2), 15.0));
}

// ---------------------------------------------------------------------------
// T2: getNoiseVar (noise_model.c:66-92, the GET_NOISE_VAR macro's lowbd
// instantiation). Hand-derived: data - denoised residual n over the region;
// returns var/(w*h) - mean^2 with the CLAMPED area as the divisor. Fixture:
// 4x4 data all 130, denoised all 128 -> noise = 2 everywhere ->
// mean 2, var 4 - 4 = 0. Second region: residual +2 on row 0 and -2 on
// row 1 (2x2 block) -> mean 0, E[n^2] = 4 -> var = 4.
// ---------------------------------------------------------------------------
TEST_CASE("getNoiseVar returns the residual variance over the clamped region") {
    std::vector<std::uint8_t> data(16, 130);
    std::vector<std::uint8_t> den(16, 128);
    CHECK(near(analysis::getNoiseVar(data.data(), den.data(), 4, 4, 4, 0, 0, 2, 2), 0.0));
    std::vector<std::uint8_t> data2 = {
        130, 130, 128, 128,
        126, 126, 128, 128,
        128, 128, 128, 128,
        128, 128, 128, 128};
    CHECK(near(analysis::getNoiseVar(data2.data(), den.data(), 4, 4, 4, 0, 0, 2, 2), 4.0));
}

// ---------------------------------------------------------------------------
// T3: svt_aom_flat_block_finder_init (noise_model.c:399-454). Hand-derived
// for block_size = 4: A row (y,x) is [yd, xd, 1] with yd = (y-2)/2,
// xd = (x-2)/2. at_a_inv stores (A^T A)^{-1} row-major, so
// (A^T A) * at_a_inv = I, verified numerically below.
// ---------------------------------------------------------------------------
TEST_CASE("flatBlockFinderInit builds the design matrix and its pseudo-inverse") {
    analysis::AomFlatBlockFinder finder;
    REQUIRE(analysis::flatBlockFinderInit(&finder, 4, 8, 0) == 1);
    CHECK(finder.blockSize == 4);
    CHECK(finder.numParams == 3);
    CHECK(near(finder.normalization, 255.0));
    const int32_t n = 16;
    REQUIRE(finder.a.size() == 3 * n);
    for (int32_t y = 0; y < 4; ++y) {
        for (int32_t x = 0; x < 4; ++x) {
            const int32_t row = y * 4 + x;
            CHECK(near(finder.a[row * 3 + 0], (y - 2) / 2.0));
            CHECK(near(finder.a[row * 3 + 1], (x - 2) / 2.0));
            CHECK(near(finder.a[row * 3 + 2], 1.0));
        }
    }
    // M[i][j] = sum_r A[r][i] * A[r][j]; (M * at_a_inv)[i][k] ~= delta_ik.
    std::vector<double> m(9, 0.0);
    for (int32_t r = 0; r < n; ++r) {
        for (int32_t i = 0; i < 3; ++i) {
            for (int32_t j = 0; j < 3; ++j) {
                m[i * 3 + j] += finder.a[r * 3 + i] * finder.a[r * 3 + j];
            }
        }
    }
    for (int32_t i = 0; i < 3; ++i) {
        for (int32_t k = 0; k < 3; ++k) {
            double s = 0;
            for (int32_t j = 0; j < 3; ++j) {
                s += m[i * 3 + j] * finder.atAInv[j * 3 + k];
            }
            CHECK(near(s, i == k ? 1.0 : 0.0));
        }
    }
}

// ---------------------------------------------------------------------------
// T4: svt_aom_flat_block_finder_extract_block_c (noise_model.c:465-:508).
// Hand-derived clamped fixture: block_size 4, w = h = 4, offsx = 2, offsy =
// 0, so xi = 0..3 clamps to x = 2,3,3,3. Data depends only on x:
// {x==2: 10, x==3: 20}. The least-squares planar fit over [yd, xd, 1] has
// a1 = 0 (no y dependence), a2 = 6/255, a3 = 19/255 (normal equations
// solved by hand, using the clamped column repeats), so per column:
//   xd=-1   : plane = 13/255, block = 10/255 - 13/255 = -3/255
//   xd=-0.5 : plane = 16/255, block = 20/255 - 16/255 =  4/255
//   xd= 0   : plane = 19/255, block = 20/255 - 19/255 =  1/255
//   xd= 0.5 : plane = 22/255, block = 20/255 - 22/255 = -2/255
// (values divided by normalization 255 in normalized space).
// ---------------------------------------------------------------------------
TEST_CASE("extractBlock fits a plane and returns the clamped residual") {
    analysis::AomFlatBlockFinder finder;
    REQUIRE(analysis::flatBlockFinderInit(&finder, 4, 8, 0) == 1);
    std::vector<std::uint8_t> data = {
        0,  0, 10, 20,
        0,  0, 10, 20,
        0,  0, 10, 20,
        0,  0, 10, 20};
    std::vector<double> plane(16, 0.0), block(16, 0.0);
    analysis::flatBlockFinderExtractBlock(&finder, data.data(), 4, 4, 4, 2, 0,
                                          plane.data(), block.data());
    for (int32_t y = 0; y < 4; ++y) {
        CHECK(near(plane[y * 4 + 0], 13.0 / 255.0));
        CHECK(near(plane[y * 4 + 1], 16.0 / 255.0));
        CHECK(near(plane[y * 4 + 2], 19.0 / 255.0));
        CHECK(near(plane[y * 4 + 3], 22.0 / 255.0));
        CHECK(near(block[y * 4 + 0], -3.0 / 255.0));
        CHECK(near(block[y * 4 + 1], 4.0 / 255.0));
        CHECK(near(block[y * 4 + 2], 1.0 / 255.0));
        CHECK(near(block[y * 4 + 3], -2.0 / 255.0));
    }
}

// ---------------------------------------------------------------------------
// T5: svt_aom_flat_block_finder_run (noise_model.c:520-:634).
// T5a: a single 32x32 checkerboard block (128 +/- 10 by (x+y) parity).
// Hand-derived: the plane fit is exactly the mean (the pattern's sums
// against yd/xd vanish), so the residual is +/- 10/255; the central
// differences of a period-2 pattern are zero, so g_xx = g_xy = g_yy = 0 ->
// trace = 0 < k_trace, e1 = e2 = 0 -> ratio = 0, norm = 0; var =
// (10/255)^2 = 1.538e-3 > k_var = 0.005/1024 -> is_flat = 255. One block:
// top_nth_percentile = 1*90/100 = 0, threshold = its own score, the |= 1
// is a no-op on 255. run returns num_flat = 1.
// ---------------------------------------------------------------------------
TEST_CASE("flatBlockFinderRun marks a noise checkerboard flat") {
    analysis::AomFlatBlockFinder finder;
    REQUIRE(analysis::flatBlockFinderInit(&finder, 32, 8, 0) == 1);
    std::vector<std::uint8_t> data(32 * 32);
    for (int32_t y = 0; y < 32; ++y) {
        for (int32_t x = 0; x < 32; ++x) {
            data[y * 32 + x] = static_cast<std::uint8_t>(128 + (((x + y) % 2 == 0) ? 10 : -10));
        }
    }
    std::vector<std::uint8_t> flat(1, 0);
    CHECK(analysis::flatBlockFinderRun(&finder, data.data(), 32, 32, 32, flat.data()) == 1);
    CHECK(flat[0] == 255);
}

// ---------------------------------------------------------------------------
// T5b: the zero-variance edge (the reference's own behavior, derived from
// the code): a pure ramp block is exactly planar, so the residual is ~0,
// var ~ 0 which fails var > k_var -> is_flat = 0 AND score = 0. With one
// block, top_nth_percentile = 0 and score_threshold = scores[0].score = 0,
// so the union loop's `score >= score_threshold` is 0 >= 0 = TRUE: the
// block gets flat_blocks[0] |= 1 and num_flat = 1. Every zero-variance
// run therefore reports flat - pinned here because it is easy to "fix"
// incorrectly.
// ---------------------------------------------------------------------------
TEST_CASE("flatBlockFinderRun zero-score edge marks every block via the union") {
    analysis::AomFlatBlockFinder finder;
    REQUIRE(analysis::flatBlockFinderInit(&finder, 32, 8, 0) == 1);
    std::vector<std::uint8_t> data(32 * 32);
    for (int32_t y = 0; y < 32; ++y) {
        for (int32_t x = 0; x < 32; ++x) {
            data[y * 32 + x] = static_cast<std::uint8_t>(y * 8);
        }
    }
    std::vector<std::uint8_t> flat(1, 0);
    CHECK(analysis::flatBlockFinderRun(&finder, data.data(), 32, 32, 32, flat.data()) == 1);
    CHECK(flat[0] == 1);
}

// ---------------------------------------------------------------------------
// T5e: the sigmoid-weight-swap killer (w0 <-> w2, the var and trace
// coefficients). 20 blocks (10x2 grid of 8x8, image 80x16): 17 ramps
// (score exactly 0) plus three checkerboard-family blocks (plane =
// 128/255 mean exactly, robust sqrt: the checkerboards have trace = 0
// EXACTLY, the cross has g_xx != g_yy):
//   X = checkerboard +-10 at index 4:  var = 100/255^2 = 1.5381e-3,
//       trace = ratio = norm = 0 -> FLAT (255);
//       E = -6682*1.5381e-3 + 2.5694 = -7.7103 -> score 4.48e-4
//   Y = cross(3,2)  at index 9:  ratio 2.25 -> non-flat;
//       E = -6682*1.9995e-4 - 0.2056*2.25 + 13087*1.33324e-4
//         - 12434*9.2285e-5 + 2.5694 = 1.3681 -> score 0.79703
//   Z = checkerboard +-3 at index 15: var = 9/255^2 = 1.3841e-4 >
//       k_var -> FLAT (255); E = -0.92489 + 2.5694 = 1.64451
//       -> score 0.83815
// Correct order: [0 x17, 4.48e-4, 0.79703, 0.83815]; index 18 = 0.79703 ->
// the union marks {Y, Z}: flat = X:255, Y:1, Z:255, num_flat = 3. Under
// w0<->w2 (verified by running the mutation): X saturates to 1.0f
// (E' = 13087*1.5381e-3 + 2.5694 = 22.70), Y drops to 0.9362, Z rises to
// 0.9876 -> order [0 x17, 0.9362, 0.9876, 1.0], index 18 = 0.9876 -> marks
// {Z, X} only: Y flips 1 -> 0 - a different map, killed here.
// ---------------------------------------------------------------------------
TEST_CASE("flatBlockFinderRun weight swap flips the marked set") {
    analysis::AomFlatBlockFinder finder;
    REQUIRE(analysis::flatBlockFinderInit(&finder, 8, 8, 0) == 1);
    const int32_t w = 80, h = 16;
    std::vector<std::uint8_t> data(w * h, 0);
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            data[y * w + x] = static_cast<std::uint8_t>(y * 8);
        }
    }
    auto checkerBlock = [&](int32_t bx, int32_t by, int32_t d) {
        for (int32_t yi = 0; yi < 8; ++yi) {
            for (int32_t xi = 0; xi < 8; ++xi) {
                data[(by * 8 + yi) * w + (bx * 8 + xi)] =
                    static_cast<std::uint8_t>(128 + (((xi + yi) % 2 == 0) ? d : -d));
            }
        }
    };
    auto crossBlock = [&](int32_t bx, int32_t by, int32_t cx, int32_t cy) {
        for (int32_t yi = 0; yi < 8; ++yi) {
            for (int32_t xi = 0; xi < 8; ++xi) {
                const int32_t p = (xi < 2 || xi >= 6) ? -cx : cx;
                const int32_t q = (yi < 2 || yi >= 6) ? -cy : cy;
                data[(by * 8 + yi) * w + (bx * 8 + xi)] = static_cast<std::uint8_t>(128 + p + q);
            }
        }
    };
    checkerBlock(4, 0, 10);   // X, index 4
    crossBlock(9, 0, 3, 2);   // Y, index 9
    checkerBlock(5, 1, 3);    // Z, index 15
    std::vector<std::uint8_t> flat(20, 0);
    const int32_t expected[20] = {0, 0, 0, 0, 255, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 255, 0, 0, 0, 0};
    CHECK(analysis::flatBlockFinderRun(&finder, data.data(), w, h, w, flat.data()) == 3);
    for (int32_t i = 0; i < 20; ++i) {
        CHECK(flat[i] == expected[i]);
    }
}

// ---------------------------------------------------------------------------
// T5c: the 20-block percentile fixture (10x2 grid of 8x8 blocks, image
// 80x16) - the qsort-tie arbiter and the percentile-index killer.
//
// 17 ramp blocks (data = 8y, var ~ 0 -> score exactly 0) plus three
// "cross-checkerboard" blocks with residual r = p(x) + q(y), where
// p = [-a,-a,a,a,a,a,-a,-a] has ZERO sum and ZERO sum against xd =
// (x-4)/4 (verified by hand), so the plane fit is exactly the 128/255 mean
// and the residual is (p + q)/255 exactly. Interior features (bs = 8,
// 36 interior points):
//   g_xx = (2/3) cx^2 / 255^2, g_yy = (2/3) cy^2 / 255^2, g_xy = 0,
//   var = (cx^2 + cy^2) / 255^2, e1 = max(g_xx, g_yy), e2 = min,
//   ratio = e1/e2, norm = e1.
// Hand-derived scores (u = 1/255^2 = 1.53808e-5):
//   A = (cx,cy) = (3,1) at index 3:  trace = 6.667u < k_trace, ratio = 9
//       -> non-flat; E = -0.11425 -> score 0.47147
//   C = (3,2) at index 7:  trace = 8.667u < k_trace, ratio = 2.25
//       -> non-flat; E = 1.36811 -> score 0.79738
//   B = (3,3) at index 12: trace = 12u > k_trace -> non-flat;
//       E = 1.78214 -> score 0.85625
// Sorted ascending: [0 x17, 0.47147, 0.79738, 0.85625]; top_nth = 20*90/100
// = 18 -> threshold = 0.79738 -> the union marks exactly {C, B}:
//   flat = {0,0,0,0,1,0,0,1,0,0, 0,0,1,0,0,0,0,0,0,0}, num_flat = 2.
// The three scores are far apart in float, and the 17 zero scores are ties
// (qsort-unstable, but the union marks by score >= threshold, so the map
// is tie-invariant - pinned bit-for-bit). A w0<->w2 sigmoid-weight swap
// reorders to [0.02562 (B), 0.04215 (A), 0.07264 (C)] -> marks {A, C} -
// a different map, so this fixture kills the weight mutation; a percentile
// index of 17 (threshold 0) marks all 20, and 19 (threshold 0.85625)
// marks only {B} - both killed here too.
// ---------------------------------------------------------------------------
TEST_CASE("flatBlockFinderRun percentile union on a 20-block grid is tie-invariant") {
    analysis::AomFlatBlockFinder finder;
    REQUIRE(analysis::flatBlockFinderInit(&finder, 8, 8, 0) == 1);
    const int32_t w = 80, h = 16;
    std::vector<std::uint8_t> data(w * h, 0);
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            data[y * w + x] = static_cast<std::uint8_t>(y * 8);
        }
    }
    auto crossBlock = [&](int32_t bx, int32_t by, int32_t cx, int32_t cy) {
        for (int32_t yi = 0; yi < 8; ++yi) {
            for (int32_t xi = 0; xi < 8; ++xi) {
                const int32_t p = (xi < 2 || xi >= 6) ? -cx : cx;
                const int32_t q = (yi < 2 || yi >= 6) ? -cy : cy;
                data[(by * 8 + yi) * w + (bx * 8 + xi)] = static_cast<std::uint8_t>(128 + p + q);
            }
        }
    };
    crossBlock(3, 0, 3, 1);   // A, index 3
    crossBlock(7, 0, 3, 2);   // C, index 7
    crossBlock(2, 1, 3, 3);   // B, index 12
    std::vector<std::uint8_t> flat(20, 0);
    const int32_t expected[20] = {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
    CHECK(analysis::flatBlockFinderRun(&finder, data.data(), w, h, w, flat.data()) == 2);
    for (int32_t i = 0; i < 20; ++i) {
        CHECK(flat[i] == expected[i]);
    }
}

// ---------------------------------------------------------------------------
// T5d: the k_trace / k_ratio killer, a 2-block 32x32 image (64x32). All
// three patterns are period-8, zero-sum and zero-moment against
// xd = (x - 16)/16 (the antisymmetric family (b,c,d,e,-e,-d,-c,-b) with
// -5c - 3d - e - 7b = 0), so every plane fit is exactly the 128/255 mean
// and the residuals are exact pattern/255.
// Block 0: r = p(x) - p(y), p = [0,-1,0,5,-5,0,1,0]. gx = [-0.5, 0, 3,
// -2.5, -2.5, 3, 0, -0.5]; per-cycle sum(gx^2) = 31, over the 30-wide
// interior window 3*31 + 30.5 = 123.5 -> g_xx = g_yy = 123.5/(30*255^2)
// = 6.3309e-5; sum(gx) = 3*0 + 1 = 1, sum(gy) = -1 -> g_xy =
// -1/58522500 = -1.70874e-8; var = (30*208 + 30*208 - 2*1)/900/255^2 =
// 2.13251e-4. trace = 1.26618e-4 < k_trace = 1.46484e-4, e1 = 6.33261e-5
// < k_norm = 7.8125e-5, ratio = 1.00054 < 1.25 -> FLAT, flat[0] = 255.
// Block 1: r = p2(x) + q(y), p2 = [1,1,-3,-3,3,3,-1,-1] (moment -5c - 3d
// - e - 7b = -5 + 9 + 3 - 7 = 0), q = [1,0,-1,-4,4,1,0,-1] (moment
// 0 + 3 + 4 - 7 = 0). gx(p2) = [1,-2,-2,3,3,-2,-2,1], per-cycle
// sum(gx^2) = 36, window 3*36 + 34 = 142 -> g_xx = 7.27925e-5;
// g_yy = 91.5/(30*255^2) = 4.6905e-5; g_xy = 2/58522500 = 3.41749e-8;
// var = 1.53787e-4. trace = 1.19698e-4 < k_trace, e1 = 7.27926e-5 <
// k_norm, ratio = 1.55192 > 1.25 -> NON-FLAT via the ratio gate;
// scores E(block1) = 1.88437 -> 0.86833 > E(block0) = 1.80824 -> 0.85919,
// so with top_nth = 2*90/100 = 1 the union threshold is 0.86833 and both
// blocks are marked: flat = {255, 1}, run = 2.
// Mutations killed here: k_trace 0.15 -> 0.05 (4.8828e-5 < block 0's
// trace 1.26618e-4 -> block 0 goes non-flat, flat[0] = 1); k_ratio 1.25
// -> 2.5 (block 1's ratio 1.55192 < 2.5 -> block 1 goes flat, flat[1] =
// 255). (The exactly-balanced g_xx == g_yy with g_xy == 0 case drives the
// reference's own sqrt(trace^2 - 4 det) into the NaN domain; both blocks
// here keep the discriminant safely positive.)
// ---------------------------------------------------------------------------
TEST_CASE("flatBlockFinderRun marks a balanced low-gradient block flat") {
    analysis::AomFlatBlockFinder finder;
    REQUIRE(analysis::flatBlockFinderInit(&finder, 32, 8, 0) == 1);
    const int32_t p[8] = {0, -1, 0, 5, -5, 0, 1, 0};
    const int32_t p2[8] = {1, 1, -3, -3, 3, 3, -1, -1};
    const int32_t q[8] = {1, 0, -1, -4, 4, 1, 0, -1};
    std::vector<std::uint8_t> data(64 * 32);
    for (int32_t y = 0; y < 32; ++y) {
        for (int32_t x = 0; x < 32; ++x) {
            data[y * 64 + x] = static_cast<std::uint8_t>(128 + p[x % 8] - p[y % 8]);
            data[y * 64 + 32 + x] = static_cast<std::uint8_t>(128 + p2[x % 8] + q[y % 8]);
        }
    }
    std::vector<std::uint8_t> flat(2, 0);
    CHECK(analysis::flatBlockFinderRun(&finder, data.data(), 64, 32, 64, flat.data()) == 2);
    CHECK(flat[0] == 255);
    CHECK(flat[1] == 1);
}

// ---------------------------------------------------------------------------
// T6: svt_aom_noise_model_init (noise_model.c:636-:697) + free (:699-:714).
// Hand-derived diamond lag 1: the coord walk runs y in [-1, 0]; for y = -1
// the max_x is lag = 1 and the diamond keeps |x| <= y + lag = 0 -> only
// x = 0; for y = 0 the max_x is -1 -> only x = -1. So coords = {(0,-1),
// (-1,0)} and n = num_coeffs = lag*(lag+1) = 2. Channels 1..2 get n + 1 =
// 3 equation columns. Gates: lag < 1 and lag > k_max_lag = 4 return 0.
// ---------------------------------------------------------------------------
TEST_CASE("noiseModelInit builds diamond coords and gates the lag") {
    analysis::AomNoiseModel model;
    analysis::AomNoiseModelParams params{analysis::AomNoiseShape::Diamond, 1, 8, 0};
    CHECK(analysis::noiseModelInit(&model, params) == 1);
    CHECK(model.n == 2);
    REQUIRE(model.coords.size() == 2);
    CHECK(model.coords[0][0] == 0);
    CHECK(model.coords[0][1] == -1);
    CHECK(model.coords[1][0] == -1);
    CHECK(model.coords[1][1] == 0);
    CHECK(model.latestState[0].eqns.n == 2);
    CHECK(model.combinedState[0].eqns.n == 2);
    CHECK(model.latestState[1].eqns.n == 3);
    CHECK(model.combinedState[2].eqns.n == 3);
    analysis::noiseModelFree(&model);
    CHECK(model.coords.empty());
    CHECK(model.latestState[0].eqns.a.empty());

    analysis::AomNoiseModelParams badLow{analysis::AomNoiseShape::Diamond, 0, 8, 0};
    CHECK(analysis::noiseModelInit(&model, badLow) == 0);
    analysis::AomNoiseModelParams badHigh{analysis::AomNoiseShape::Diamond, 5, 8, 0};
    CHECK(analysis::noiseModelInit(&model, badHigh) == 0);
}

// ---------------------------------------------------------------------------
// T7: EXTRACT_AR_ROW lowbd (noise_model.c:719-:756), the AN17 extraction
// kernel. Hand-derived diamond lag-1 coords {(0,-1), (-1,0)} at (x,y) =
// (2,2) on a 4x4 grid (denoised constant 128): buffer[0] = the UP residual
// data[1][2] - 128 = 8, buffer[1] = the LEFT residual data[2][1] - 128 =
// 11, val = data[2][2] - 128 = 12 - the coordinate ORDER is pinned by
// making up != left (kills a coords transposition). Alt/luma column at
// sub_log2 = {1,1}: the 2x2 average of (alt_data - alt_denoised) over
// luma rows {4,5} x cols {4,5} = (10 + 20 + 30 + 40)/4 = 25 into
// buffer[num_coords]. With no alt_data, buffer[num_coords] is untouched.
// ---------------------------------------------------------------------------
TEST_CASE("extractArRow gathers diamond residuals and the averaged alt column") {
    const std::array<std::int32_t, 2> coords[2] = {{0, -1}, {-1, 0}};
    std::vector<std::uint8_t> data = {
        130, 131, 132, 133,
        134, 135, 136, 137,
        138, 139, 140, 141,
        142, 143, 144, 145};
    std::vector<std::uint8_t> den(16, 128);
    double buffer[3] = {99.0, 99.0, 99.0};
    const std::int32_t subLog2[2] = {1, 1};
    std::vector<std::uint8_t> altData(64, 0);
    altData[4 * 8 + 4] = 10;
    altData[4 * 8 + 5] = 20;
    altData[5 * 8 + 4] = 30;
    altData[5 * 8 + 5] = 40;
    std::vector<std::uint8_t> altDen(64, 0);

    const double val = analysis::extractArRow(coords, 2, data.data(), den.data(), 4, subLog2,
                                              altData.data(), altDen.data(), 8, 2, 2, buffer);
    CHECK(near(val, 12.0));
    CHECK(near(buffer[0], 8.0));
    CHECK(near(buffer[1], 11.0));
    CHECK(near(buffer[2], 25.0));

    double buffer2[3] = {77.0, 77.0, 77.0};
    const double val2 = analysis::extractArRow(coords, 2, data.data(), den.data(), 4, subLog2,
                                               nullptr, nullptr, 8, 2, 2, buffer2);
    CHECK(near(val2, 12.0));
    CHECK(near(buffer2[2], 77.0));
}

// ---------------------------------------------------------------------------
// T8: svt_av1_add_block_observations_internal_c (noise_model.c:759-:803).
// Hand-derived: buffer[i] = i + 1, val = 2, recp_sqr_norm = 0.5 ->
// buffer_norm[i] = (i + 1) / 2, b[i] += (i + 1) / 2 * 2 = i + 1,
// A[i][j] += buffer_norm[i] * buffer[j] = (i + 1)(j + 1) / 2. The n = 10
// fixture exercises the unrolled-by-8 main loop AND the 2-element tail;
// the n = 3 fixture exercises the tail-only path.
// ---------------------------------------------------------------------------
TEST_CASE("addBlockObservationsInternal accumulates the normal equations") {
    const uint32_t n = 10;
    std::vector<double> buffer(n), bufferNorm(n, 0.0), b(n, 0.0), a(n * n, 0.0);
    for (uint32_t i = 0; i < n; ++i) {
        buffer[i] = (double)(i + 1);
    }
    analysis::addBlockObservationsInternal(n, 2.0, 0.5, buffer.data(), bufferNorm.data(),
                                           b.data(), a.data());
    for (uint32_t i = 0; i < n; ++i) {
        CHECK(near(bufferNorm[i], (double)(i + 1) * 0.5));
        CHECK(near(b[i], (double)(i + 1)));
        for (uint32_t j = 0; j < n; ++j) {
            CHECK(near(a[i * n + j], (double)((i + 1) * (j + 1)) * 0.5));
        }
    }

    const uint32_t m = 3;
    std::vector<double> buf2 = {4.0, 5.0, 6.0}, bn2(3, 0.0), b2(3, 0.0), a2(9, 0.0);
    analysis::addBlockObservationsInternal(m, 1.0, 2.0, buf2.data(), bn2.data(), b2.data(), a2.data());
    CHECK(near(bn2[2], 12.0));
    CHECK(near(b2[2], 12.0));
    CHECK(near(a2[2 * 3 + 1], 60.0));  // buffer_norm[2] * buffer[1] = 12 * 5
}

// ---------------------------------------------------------------------------
// T9: add_block_observations (noise_model.c:805-:890), the y/x start-end
// walk over flat blocks. Hand-derived with a constant residual of 2
// (data = 130, denoised = 128, bit depth 8, recp_sqr_norm = 1/255^2):
// every observation contributes buffer_norm = 2/65025, b[i] += 4/65025 and
// A[i][j] += (2/65025) * 2 = 4/65025.
// Fixture 1: one 8x8 flat block, lag 1, no flat neighbours -> y in
// [1, 8), x in [1, 7) -> 7*6 = 42 observations; num_observations = 42;
// b[i] = A[i][j] = 168/65025.
// Fixture 2 (the :845-:849 flat-right-neighbour branch): a 2x1 grid of
// flat blocks on a 16x8 frame. Block 0 gets x_end = min(16 - 0 - 1, 8) =
// 8 (the flat right neighbour lifts the lag cap), x in [1, 8); block 1
// gets x_start = 0 (flat left neighbour), x_end = min(16 - 8 - 1, 7) = 7.
// Both contribute 7*7 = 49 -> 98 total; b[i] = 392/65025. Dropping the
// flat-right branch (always block - lag) gives 42 + 49 = 91 - killed.
// ---------------------------------------------------------------------------
TEST_CASE("addBlockObservations walks the flat-block start-end window") {
    analysis::AomNoiseModel model;
    analysis::AomNoiseModelParams params{analysis::AomNoiseShape::Diamond, 1, 8, 0};
    REQUIRE(analysis::noiseModelInit(&model, params) == 1);
    std::vector<std::uint8_t> data(64, 130), den(64, 128);
    const std::uint8_t flat1[1] = {1};
    const std::int32_t sub0[2] = {0, 0};
    CHECK(analysis::addBlockObservations(&model, 0, data.data(), den.data(), 8, 8, 8,
                                         sub0, nullptr, nullptr, 8, flat1, 8, 1, 1) == 1);
    CHECK(model.latestState[0].numObservations == 42);
    CHECK(near(model.latestState[0].eqns.b[0], 168.0 / 65025.0));
    CHECK(near(model.latestState[0].eqns.a[0 * 2 + 1], 168.0 / 65025.0));

    analysis::noiseModelFree(&model);
    REQUIRE(analysis::noiseModelInit(&model, params) == 1);
    std::vector<std::uint8_t> data2(128, 130), den2(128, 128);
    const std::uint8_t flat2[2] = {1, 1};
    CHECK(analysis::addBlockObservations(&model, 0, data2.data(), den2.data(), 16, 8, 16,
                                         sub0, nullptr, nullptr, 8, flat2, 8, 2, 1) == 1);
    CHECK(model.latestState[0].numObservations == 98);
    CHECK(near(model.latestState[0].eqns.b[1], 392.0 / 65025.0));
    analysis::noiseModelFree(&model);
}

// ---------------------------------------------------------------------------
// T10: add_noise_std_observations (noise_model.c:892-:956). The noise
// residual is a +/- 1 checkerboard (var = 1 level^2 exactly, mean 0), the
// block mean is 128, so with ar_gain 1:
//   luma (c=0): luma_strength = 0, corr = 0 -> uncorr_std =
//       sqrt(max(1/16, 1)) = 1 -> adjusted 1.
//   chroma (c=1, sub {1,1}): the 4x4 chroma region carries the same
//       residual; corr = coeffs[num_coords] = 0.5 and luma_strength =
//       luma_gain * get_value(luma_solver, 128) = 2 * 0.5 = 1 ->
//       uncorr_std = sqrt(max(1/16, 1 - (0.5*1)^2)) = sqrt(0.75).
//   Floor fixture: corr = 2, luma_strength = 2 -> (corr*luma)^2 = 16
//       dwarfs noise_var = 1 -> uncorr_std = sqrt(max(1/16, -15)) = 0.25,
//       pinning the AOMMAX floor.
// The measurement lands in the 20-bin strength solver at bin index
// 19*128/255 = 9.53725 -> i0 = 9, a = 0.537255; total gains the adjusted
// strength once.
// ---------------------------------------------------------------------------
TEST_CASE("addNoiseStdObservations feeds the strength solver with the adjusted std") {
    analysis::AomNoiseModel model;
    analysis::AomNoiseModelParams params{analysis::AomNoiseShape::Diamond, 1, 8, 0};
    REQUIRE(analysis::noiseModelInit(&model, params) == 1);
    const std::int32_t sub0[2] = {0, 0};
    std::vector<std::uint8_t> data(64), den(64, 128);
    for (int32_t i = 0; i < 64; ++i) {
        const int32_t x = i % 8, y = i / 8;
        data[i] = static_cast<std::uint8_t>(128 + ((x + y) % 2 == 0 ? 1 : -1));
    }
    const std::uint8_t flat[1] = {1};
    analysis::addNoiseStdObservations(&model, 0, model.latestState[0].eqns.x.data(),
                                      data.data(), den.data(), 8, 8, 8, sub0,
                                      nullptr, 8, flat, 8, 1, 1);
    CHECK(near(model.latestState[0].strengthSolver.total, 1.0));
    CHECK(model.latestState[0].strengthSolver.numEquations == 1);
    const double a = 19.0 * 128.0 / 255.0 - 9.0;
    CHECK(near(model.latestState[0].strengthSolver.eqns.b[9], (1.0 - a) * 1.0));
    CHECK(near(model.latestState[0].strengthSolver.eqns.b[10], a * 1.0));
    analysis::noiseModelFree(&model);

    // Chroma: corr * luma_strength = 0.5 * 1 -> sqrt(0.75).
    REQUIRE(analysis::noiseModelInit(&model, params) == 1);
    model.latestState[0].arGain = 2.0;
    model.latestState[0].strengthSolver.eqns.x[9] = 0.5;
    model.latestState[0].strengthSolver.eqns.x[10] = 0.5;
    model.latestState[1].eqns.x[2] = 0.5;
    const std::int32_t sub1[2] = {1, 1};
    std::vector<std::uint8_t> chroma(32, 0), chromaDen(32, 128);
    for (int32_t y = 0; y < 4; ++y) {
        for (int32_t x = 0; x < 4; ++x) {
            chroma[y * 8 + x] = static_cast<std::uint8_t>(128 + ((x + y) % 2 == 0 ? 1 : -1));
        }
    }
    std::vector<std::uint8_t> luma(64, 128);
    analysis::addNoiseStdObservations(&model, 1, model.latestState[1].eqns.x.data(),
                                      chroma.data(), chromaDen.data(), 8, 8, 8, sub1,
                                      luma.data(), 8, flat, 8, 1, 1);
    CHECK(near(model.latestState[1].strengthSolver.total, std::sqrt(0.75)));

    // Floor: corr = 2, luma_strength = 2 -> 0.25.
    analysis::noiseModelFree(&model);
    REQUIRE(analysis::noiseModelInit(&model, params) == 1);
    model.latestState[0].arGain = 2.0;
    model.latestState[0].strengthSolver.eqns.x[9] = 1.0;
    model.latestState[0].strengthSolver.eqns.x[10] = 1.0;
    model.latestState[1].eqns.x[2] = 2.0;
    analysis::addNoiseStdObservations(&model, 1, model.latestState[1].eqns.x.data(),
                                      chroma.data(), chromaDen.data(), 8, 8, 8, sub1,
                                      luma.data(), 8, flat, 8, 1, 1);
    CHECK(near(model.latestState[1].strengthSolver.total, 0.25));
    analysis::noiseModelFree(&model);
}

// ---------------------------------------------------------------------------
// T11: ar_equation_system_solve (noise_model.c:958-:996). All values
// hand-derived.
// Luma: A = 8I (n=2), b = [4,4], num_observations = 4 -> x = [0.5, 0.5],
// var = mean(diag/obs) = (8/4 + 8/4)/2 = 2, sum_covar = (4*0.5 + 4*0.5)/4
// = 1, noise_var = 1, ar_gain = sqrt(2/1) = sqrt(2).
// Chroma correction: n = 3, is_chroma = 1, A = [[8,0,2],[0,8,0],[2,0,4]],
// b = [4,4,4], obs = 4 -> x = [2/7, 1/2, 6/7]; var = 2; sum_covar = (4 -
// 2*(6/7))*(2/7)/4 + 4*(1/2)/4 = 8/49 + 1/2 = 65/98; noise_var = 131/98;
// ar_gain = sqrt(2 / (131/98)) = 14/sqrt(131). Skipping the b correction
// gives 14/sqrt(119) - killed.
// Floor: A = [[8,2],[2,4]], b = [10,6], obs = 4 -> var = 1.5, sum_covar =
// 4 -> noise_var = max(-2.5, 1e-6) = 1e-6, ar_gain = sqrt(1.5e6).
// Singular system: solve fails -> returns 0 with ar_gain reset to 1.
// ---------------------------------------------------------------------------
TEST_CASE("arEquationSystemSolve derives the AR gain from the solved system") {
    analysis::AomNoiseState state;
    analysis::noiseStateInit(&state, 2, 8);
    state.eqns.a = {8, 0, 0, 8};
    state.eqns.b = {4, 4};
    state.numObservations = 4;
    CHECK(analysis::arEquationSystemSolve(&state, 0) == 1);
    CHECK(near(state.eqns.x[0], 0.5));
    CHECK(near(state.arGain, std::sqrt(2.0)));
    analysis::equationSystemFree(&state.eqns);
    analysis::noiseStrengthLutFree(nullptr);

    analysis::AomNoiseState chroma;
    analysis::noiseStateInit(&chroma, 3, 8);
    chroma.eqns.a = {8, 0, 2, 0, 8, 0, 2, 0, 4};
    chroma.eqns.b = {4, 4, 4};
    chroma.numObservations = 4;
    CHECK(analysis::arEquationSystemSolve(&chroma, 1) == 1);
    CHECK(near(chroma.eqns.x[0], 2.0 / 7.0));
    CHECK(near(chroma.eqns.x[2], 6.0 / 7.0));
    CHECK(near(chroma.arGain, 14.0 / std::sqrt(131.0)));
    analysis::equationSystemFree(&chroma.eqns);

    analysis::AomNoiseState floor;
    analysis::noiseStateInit(&floor, 2, 8);
    floor.eqns.a = {8, 2, 2, 4};
    floor.eqns.b = {10, 6};
    floor.numObservations = 4;
    CHECK(analysis::arEquationSystemSolve(&floor, 0) == 1);
    CHECK(near(floor.arGain, std::sqrt(1.5e6)));
    analysis::equationSystemFree(&floor.eqns);

    analysis::AomNoiseState sing;
    analysis::noiseStateInit(&sing, 2, 8);
    sing.eqns.a = {1, 2, 2, 4};
    sing.eqns.b = {1, 1};
    sing.numObservations = 4;
    CHECK(analysis::arEquationSystemSolve(&sing, 0) == 0);
    CHECK(near(sing.arGain, 1.0));
    analysis::equationSystemFree(&sing.eqns);
}

// ---------------------------------------------------------------------------
// T12: noise_model_update (noise_model.c:1019-:1145).
// Gates: block_size <= 1 and block_size < lag*2 + 1 ->
// InvalidArgument; fewer than two flat blocks -> InsufficientFlatBlocks.
//
// The OK fixture: a 16x8 frame, two flat 8x8 blocks, lag-1 diamond, with
// residual r(y,x) = y levels (data = 128 + y, denoised = 128). Hand-
// derived: the walk covers 7 y-rows x (7 + 7) x-columns = 98 observations;
// buffer[0] = y - 1, buffer[1] = y, val = y, so (with recp_sqr_norm =
// 1/255^2) A00 = 1274c, A01 = 1568c, A11 = 1960c and b = [1568c, 1960c]
// where c = 1/255^2 - b is exactly A[:,1], so the exact solution is
// x = [0, 1] (the elimination is exact in binary: the pivot factor is
// 1274/1568 = 0.8125, and b0 - factor*b1 == A01 - factor*A11 identically).
// var = (13c + 20c)/2 = 16.5/65025 = 2.537486e-4; sum_covar = 20c/... =
// 1960/(98*65025) = 3.075738e-4; var - sum_covar < 0 -> noise_var = 1e-6;
// ar_gain = sqrt(2.537486e-4 / 1e-6) = sqrt(16.5/(65025*1e-6)) = 15.92949.
// The strength pass: noise_var over the block = Var(y) = 17.5 - 3.5^2 =
// 5.25 exactly, block_mean = 131.5 for both blocks, uncorr_std =
// sqrt(5.25) (luma_strength = 0), adjusted = sqrt(5.25) / ar_gain ->
// solver total = 2*sqrt(5.25)/ar_gain. A second identical update must
// reproduce every latest-state value bit-for-bit (determinism).
// ---------------------------------------------------------------------------
TEST_CASE("noiseModelUpdate gates and composes the full per-channel update") {
    analysis::AomNoiseModel model;
    analysis::AomNoiseModelParams params{analysis::AomNoiseShape::Diamond, 1, 8, 0};
    REQUIRE(analysis::noiseModelInit(&model, params) == 1);
    std::vector<std::uint8_t> y(128, 0), den128(128, 128);
    for (int32_t row = 0; row < 8; ++row) {
        for (int32_t col = 0; col < 16; ++col) {
            y[row * 16 + col] = static_cast<std::uint8_t>(128 + row);
        }
    }
    const std::uint8_t* dataArr[3] = {y.data(), y.data(), y.data()};
    const std::uint8_t* denArr[3] = {den128.data(), den128.data(), den128.data()};
    const std::int32_t strides[3] = {16, 16, 16};
    const std::int32_t sub[2] = {0, 0};
    const std::uint8_t flatOne[2] = {1, 0};
    const std::uint8_t flatBoth[2] = {1, 1};

    CHECK(analysis::noiseModelUpdate(&model, dataArr, denArr, 16, 8, strides, sub,
                                     flatBoth, 1) == analysis::AomNoiseStatus::InvalidArgument);
    analysis::noiseModelFree(&model);
    analysis::AomNoiseModelParams lag2{analysis::AomNoiseShape::Diamond, 2, 8, 0};
    REQUIRE(analysis::noiseModelInit(&model, lag2) == 1);
    CHECK(analysis::noiseModelUpdate(&model, dataArr, denArr, 16, 8, strides, sub,
                                     flatBoth, 3) == analysis::AomNoiseStatus::InvalidArgument);
    CHECK(analysis::noiseModelUpdate(&model, dataArr, denArr, 16, 8, strides, sub,
                                     flatOne, 8) == analysis::AomNoiseStatus::InsufficientFlatBlocks);
    analysis::noiseModelFree(&model);

    REQUIRE(analysis::noiseModelInit(&model, params) == 1);
    CHECK(analysis::noiseModelUpdate(&model, dataArr, denArr, 16, 8, strides, sub,
                                     flatBoth, 8) == analysis::AomNoiseStatus::Ok);
    CHECK(model.latestState[0].numObservations == 98);
    CHECK(std::fabs(model.latestState[0].eqns.x[0]) < 1e-9);
    CHECK(near(model.latestState[0].eqns.x[1], 1.0));
    const double arGainExpected = std::sqrt(16.5 / 65025.0 / 1e-6);
    CHECK(near(model.latestState[0].arGain, arGainExpected));
    CHECK(near(model.latestState[0].strengthSolver.total,
               2.0 * std::sqrt(5.25) / arGainExpected));
    CHECK(model.combinedState[0].numObservations == 98);
    CHECK(near(model.combinedState[0].eqns.x[1], 1.0));

    const double x0 = model.latestState[0].eqns.x[0];
    const double x1 = model.latestState[0].eqns.x[1];
    const double gain = model.latestState[0].arGain;
    CHECK(analysis::noiseModelUpdate(&model, dataArr, denArr, 16, 8, strides, sub,
                                     flatBoth, 8) == analysis::AomNoiseStatus::Ok);
    CHECK(model.latestState[0].eqns.x[0] == x0);
    CHECK(model.latestState[0].eqns.x[1] == x1);
    CHECK(model.latestState[0].arGain == gain);
    analysis::noiseModelFree(&model);
}

// ---------------------------------------------------------------------------
// T13: svt_aom_noise_model_save_latest (noise_model.c:1147-:1157). Hand-
// derived: per channel the combined AR eqns (a/b/x) and the combined
// strength-solver eqns are overwritten from the latest, plus
// num_equations, num_observations and ar_gain - the strength solver's b
// and x are the PRE-solve values of the latest solver (save_latest does
// not re-solve), unlike noise_model_update which re-solves the combined
// solver after copying.
// ---------------------------------------------------------------------------
TEST_CASE("noiseModelSaveLatest copies the latest state into combined") {
    analysis::AomNoiseModel model;
    analysis::AomNoiseModelParams params{analysis::AomNoiseShape::Diamond, 1, 8, 0};
    REQUIRE(analysis::noiseModelInit(&model, params) == 1);
    for (int32_t c = 0; c < 3; ++c) {
        model.latestState[c].eqns.a[0] = 1.0 + c;
        model.latestState[c].eqns.b[0] = 2.0 + c;
        model.latestState[c].eqns.x[0] = 3.0 + c;
        model.latestState[c].strengthSolver.eqns.b[0] = 4.0 + c;
        model.latestState[c].strengthSolver.eqns.x[0] = 5.0 + c;
        model.latestState[c].strengthSolver.numEquations = 6 + c;
        model.latestState[c].numObservations = 7 + c;
        model.latestState[c].arGain = 8.0 + c;
    }
    analysis::noiseModelSaveLatest(&model);
    for (int32_t c = 0; c < 3; ++c) {
        CHECK(model.combinedState[c].eqns.a[0] == 1.0 + c);
        CHECK(model.combinedState[c].eqns.b[0] == 2.0 + c);
        CHECK(model.combinedState[c].eqns.x[0] == 3.0 + c);
        CHECK(model.combinedState[c].strengthSolver.eqns.b[0] == 4.0 + c);
        CHECK(model.combinedState[c].strengthSolver.eqns.x[0] == 5.0 + c);
        CHECK(model.combinedState[c].strengthSolver.numEquations == 6 + c);
        CHECK(model.combinedState[c].numObservations == 7 + c);
        CHECK(model.combinedState[c].arGain == 8.0 + c);
    }
    analysis::noiseModelFree(&model);
}
