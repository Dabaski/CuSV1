#include <doctest.h>
#include <analysis.h>

#include <cmath>
#include <vector>

// 1:1 target: the SVT-AV1 noise strength solver
// (third_party/SVT-AV1/Source/Lib/Codec/noise_model.c:104-397) plus its
// private linsolve dependency (Codec/mathutils.h:22-65, A13-classified,
// carried as the port's own copy in the l9 TU - named deviation). AN16.
//
// TOLERANCE DISCIPLINE (decided up front, per the AGENTS.md GPU/FP addendum,
// the AN12/AN13 discipline): every quantity here is DOUBLE precision with a
// deterministic call order, so the port vs the reference equations is
// near-bit-exact; the practical harness tolerance is 1e-12 RELATIVE
// (|actual - expected| <= 1e-12 * max(1, |expected|)). All expected values
// in this file are hand-derived closed forms over the reference's own
// equations (normal equations + the documented regularization), written as
// literal arithmetic in the assertions - never copied from the port output.
// One tolerance for the whole file; a divergence beyond it is a STOP and a
// finding, not a tolerance adjustment.

namespace {

constexpr double kTol = 1e-12;

bool near(double actual, double expected) {
    return std::fabs(actual - expected) <= kTol * std::max(1.0, std::fabs(expected));
}

}  // namespace

// ---------------------------------------------------------------------------
// T1: linsolve, a 3x3 with a known solution (mathutils.h:22-65).
// Hand-derived: A = [[2,1,0],[1,3,1],[0,1,2]], x = [1,2,3] gives
// b = A*x = [2*1+1*2, 1*1+3*2+1*3, 1*2+2*3] = [4, 10, 8].
// ---------------------------------------------------------------------------
TEST_CASE("linsolve solves a 3x3 with a known x") {
    std::vector<double> a = {2, 1, 0,
                             1, 3, 1,
                             0, 1, 2};
    std::vector<double> b = {4, 10, 8};
    std::vector<double> x(3, 0.0);
    CHECK(analysis::linsolve(3, a.data(), 3, b.data(), x.data()) == 1);
    CHECK(near(x[0], 1.0));
    CHECK(near(x[1], 2.0));
    CHECK(near(x[2], 3.0));
}

// ---------------------------------------------------------------------------
// T2: linsolve on a singular system returns 0 (mathutils.h:42-44/:53-56).
// Hand-derived singular: rows [1,2,3] and [2,4,6] are proportional.
// ---------------------------------------------------------------------------
TEST_CASE("linsolve reports singular as 0") {
    std::vector<double> a = {1, 2, 3,
                             2, 4, 6,
                             1, 1, 1};
    std::vector<double> b = {1, 2, 3};
    std::vector<double> x(3, 0.0);
    CHECK(analysis::linsolve(3, a.data(), 3, b.data(), x.data()) == 0);
}

// ---------------------------------------------------------------------------
// T3: linsolve pivot-swap path - the first pivot is 0, so the partial
// pivoting loop must swap rows before eliminating (mathutils.h:29-40).
// Hand-derived: A = [[0,1],[2,3]], b = [4,11]. Rows swap to [[2,3],[0,1]]:
// x1 = 4, then 2*x0 + 3*4 = 11 -> x0 = -0.5. (Without the swap the
// elimination would divide by A[0][0] = 0.)
// ---------------------------------------------------------------------------
TEST_CASE("linsolve pivots when the first pivot is zero") {
    std::vector<double> a = {0, 1,
                             2, 3};
    std::vector<double> b = {4, 11};
    std::vector<double> x(2, 0.0);
    CHECK(analysis::linsolve(2, a.data(), 2, b.data(), x.data()) == 1);
    CHECK(near(x[0], -0.5));
    CHECK(near(x[1], 4.0));
}

// ---------------------------------------------------------------------------
// T4a: num_coeffs, DIAMOND (noise_model.c:181-190). lag = 1: the basis is
// 2*lag+1 = 3 wide, and DIAMOND keeps lag*(lag+1) = 2 coefficients.
// ---------------------------------------------------------------------------
TEST_CASE("num_coeffs diamond lag 1") {
    analysis::AomNoiseModelParams params;
    params.shape = analysis::AomNoiseShape::Diamond;
    params.lag = 1;
    params.bitDepth = 8;
    params.useHighbd = 0;
    CHECK(analysis::numCoeffs(params) == 2);
}

// T4b: SQUARE at lag 1 -> (3*3)/2 = 4 and lag 2 -> (5*5)/2 = 12, both by
// the reference's integer division (9/2 and 25/2 truncate). The DIAMOND
// values at the same lags are 2 and 6 - the pair kills a shape-swap
// mutation.
TEST_CASE("num_coeffs square integer division") {
    analysis::AomNoiseModelParams p1;
    p1.shape = analysis::AomNoiseShape::Square;
    p1.lag = 1;
    p1.bitDepth = 8;
    p1.useHighbd = 0;
    CHECK(analysis::numCoeffs(p1) == 4);
    analysis::AomNoiseModelParams p2 = p1;
    p2.lag = 2;
    CHECK(analysis::numCoeffs(p2) == 12);
}

// ---------------------------------------------------------------------------
// T5a: solver init (noise_model.c:305-316): 20 bins at 8 bit -> min 0,
// max 255, and a zeroed 20x20 equation system.
// ---------------------------------------------------------------------------
TEST_CASE("noise strength solver init 20 bins 8 bit") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 20, 8) == 1);
    CHECK(solver.numBins == 20);
    CHECK(near(solver.minIntensity, 0.0));
    CHECK(near(solver.maxIntensity, 255.0));
    CHECK(solver.eqns.n == 20);
    CHECK(solver.eqns.a.size() == 400);
    CHECK(solver.eqns.b.size() == 20);
    CHECK(solver.eqns.x.size() == 20);
    CHECK(near(solver.eqns.a[9 * 20 + 9], 0.0));
    CHECK(near(solver.eqns.b[13], 0.0));
}

// ---------------------------------------------------------------------------
// T5b: get_center (noise_model.c:318-322): center(i) = i/(n-1)*range + min.
// Hand-derived at 20 bins / 8 bit: center(0) = 0, center(19) = 255,
// center(9) = (9/19)*255 = 120.78947...
// ---------------------------------------------------------------------------
TEST_CASE("get_center linear map") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 20, 8) == 1);
    CHECK(near(analysis::noiseStrengthSolverGetCenter(&solver, 0), 0.0));
    CHECK(near(analysis::noiseStrengthSolverGetCenter(&solver, 19), 255.0));
    CHECK(near(analysis::noiseStrengthSolverGetCenter(&solver, 9), (9.0 / 19.0) * 255.0));
    analysis::AomNoiseStrengthSolver ten;
    CHECK(analysis::noiseStrengthSolverInit(&ten, 20, 10) == 1);
    CHECK(near(analysis::noiseStrengthSolverGetCenter(&ten, 19), 1023.0));
    CHECK(near(analysis::noiseStrengthSolverGetCenter(&ten, 4), (4.0 / 19.0) * 1023.0));
}

// ---------------------------------------------------------------------------
// T6: get_bin_index (noise_model.c:236-240): fclamp to [min, max], then
// (n-1)*(val - min)/range. Hand-derived at 20 bins / 8 bit: -5 clamps to
// 0 -> 0; 300 clamps to 255 -> 19; 127.5 -> (19/255)*127.5 = 9.5.
// ---------------------------------------------------------------------------
TEST_CASE("get_bin_index fclamp and linear map") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 20, 8) == 1);
    CHECK(near(analysis::noiseStrengthSolverGetBinIndex(&solver, -5.0), 0.0));
    CHECK(near(analysis::noiseStrengthSolverGetBinIndex(&solver, 300.0), 19.0));
    CHECK(near(analysis::noiseStrengthSolverGetBinIndex(&solver, 0.0), 0.0));
    CHECK(near(analysis::noiseStrengthSolverGetBinIndex(&solver, 255.0), 19.0));
    CHECK(near(analysis::noiseStrengthSolverGetBinIndex(&solver, 127.5), 9.5));
}

// ---------------------------------------------------------------------------
// T7: get_value (noise_model.c:242-248): the linear interpolation between
// neighbouring bin values, with x[] set directly (no solve involved).
// Hand-derived with x[i] = i: value at center(9) -> x[9] = 9 (a = 0);
// at 127.5 (bin 9.5) -> (x[9] + x[10]) / 2 = 9.5; at 300 (clamped to
// bin 19) -> x[19] = 19; at 255 (bin exactly 19, i1 = AOMMIN(19, 20))
// -> x[19] = 19.
// ---------------------------------------------------------------------------
TEST_CASE("get_value linear interpolation") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 20, 8) == 1);
    for (int32_t i = 0; i < 20; ++i) {
        solver.eqns.x[i] = static_cast<double>(i);
    }
    CHECK(near(analysis::noiseStrengthSolverGetValue(&solver, (9.0 / 19.0) * 255.0), 9.0));
    CHECK(near(analysis::noiseStrengthSolverGetValue(&solver, 127.5), 9.5));
    CHECK(near(analysis::noiseStrengthSolverGetValue(&solver, 300.0), 19.0));
    CHECK(near(analysis::noiseStrengthSolverGetValue(&solver, 255.0), 19.0));
}

// ---------------------------------------------------------------------------
// T8a: add_measurement at an exact bin center (noise_model.c:250-265).
// At a center the interpolation weight a = 0, so the 2x2 accumulation
// touches only A[i0][i0] += (1-a)^2 = 1 and b[i0] += (1-a)*std = std.
// Feeding 7.0 at center(9) twice: a[9*20+9] = 2, b[9] = 14, total = 14,
// num_equations = 2.
// ---------------------------------------------------------------------------
TEST_CASE("add_measurement at exact bin centers") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 20, 8) == 1);
    const double center9 = (9.0 / 19.0) * 255.0;
    analysis::noiseStrengthSolverAddMeasurement(&solver, center9, 7.0);
    analysis::noiseStrengthSolverAddMeasurement(&solver, center9, 7.0);
    CHECK(near(solver.eqns.a[9 * 20 + 9], 2.0));
    CHECK(near(solver.eqns.b[9], 14.0));
    CHECK(near(solver.total, 14.0));
    CHECK(solver.numEquations == 2);
}

// ---------------------------------------------------------------------------
// T8b: the constant-strength known answer through the full solve
// (add_measurement :250-265 + solve :267-303). Feed std = 7 twice per bin
// at every center. The regularization is a discrete Laplacian (row sums
// zero at every row: boundary rows fold the i_lo/i_hi subtraction onto the
// diagonal), so a CONSTANT vector is in its kernel, and the +mean/8192
// term and the +1/8192 diagonal then cancel exactly:
//   A*(7*1) = 14*1 + 0 + (7/8192)*1 = b.
// Hand-derived: the solution is x[i] = 7 exactly for every bin.
// ---------------------------------------------------------------------------
TEST_CASE("solve recovers a constant strength exactly") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 20, 8) == 1);
    for (int32_t i = 0; i < 20; ++i) {
        const double center = analysis::noiseStrengthSolverGetCenter(&solver, i);
        analysis::noiseStrengthSolverAddMeasurement(&solver, center, 7.0);
        analysis::noiseStrengthSolverAddMeasurement(&solver, center, 7.0);
    }
    CHECK(analysis::noiseStrengthSolverSolve(&solver) == 1);
    for (int32_t i = 0; i < 20; ++i) {
        CHECK(near(solver.eqns.x[i], 7.0));
    }
}

// ---------------------------------------------------------------------------
// T9: the ZERO-measurement bin through the k_alpha smoothing (solve
// :267-303), at 3 bins so the regularized system stays hand-derivable.
// ASYMMETRIC on purpose: one measurement of std 2 at center(0) and THREE
// of std 8 at center(2), nothing at bin 1. (A symmetric two-endpoint
// pattern is exactly k_alpha-INVARIANT - the closed-form derivative
// (1+e)[e*(s0+s2) - 2t]/det^2 with t = mean*e = (s0+s2)/2*e vanishes - so
// it cannot see a k_alpha mutation; measured: the symmetric version PASSED
// under k_alpha 2.0 -> 3.0 and was replaced.) Closed form over the
// reference's own equations (e = 1/8192, t = mean*e with mean = 26/4 = 6.5,
// k = 2*4/3 = 8/3, measurement diag d0 = 1, d2 = 3):
//   P = d0 + k + e, R = 2k + e, S = d2 + k + e
//   A = [[P,-k,0],[-k,R,-k],[0,-k,S]], b = [2+t, t, 24+t]
//   det = P*R*S - P*k*k - k*k*S
//   x1 = (P*S*t + P*k*(s2+t) + k*S*(s0+t)) / det   (Cramer, middle column)
// With t = 0 (the +mean/8192 term dropped) the value moves by O(e/k) >>
// the tolerance; dropping the +1/8192 diagonal or changing k_alpha moves
// x1 at O(1e-3)..O(1e-1).
// ---------------------------------------------------------------------------
TEST_CASE("zero-measurement bin is blended and the mean term moves it") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 3, 8) == 1);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 0.0, 2.0);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 255.0, 8.0);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 255.0, 8.0);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 255.0, 8.0);
    CHECK(analysis::noiseStrengthSolverSolve(&solver) == 1);
    const double e = 1.0 / 8192.0;
    const double t = 6.5 * e;
    const double k = 8.0 / 3.0;
    const double P = 1.0 + k + e;
    const double R = 2.0 * k + e;
    const double S = 3.0 + k + e;
    const double det = P * R * S - P * k * k - k * k * S;
    const double x1 = (P * S * t + P * k * (24.0 + t) + k * S * (2.0 + t)) / det;
    const double x1NoMean = (P * S * 0.0 + P * k * 24.0 + k * S * 2.0) / det;
    CHECK(near(solver.eqns.x[1], x1));
    CHECK(std::fabs(solver.eqns.x[1] - x1NoMean) > 100.0 * kTol);
}

// ---------------------------------------------------------------------------
// T10: fit_piecewise on an exactly linear strength curve collapses to the
// two endpoints (noise_model.c:353-397). The solved x is exactly 5.0
// everywhere (T8's kernel argument), so every interior point has zero
// residual against the segment through its neighbours and the greedy
// removal runs to num_points == 2. Hand-derived survivors:
// (0, 5) and (255, 5).
// ---------------------------------------------------------------------------
TEST_CASE("fit_piecewise collapses a linear curve to two endpoints") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 20, 8) == 1);
    for (int32_t i = 0; i < 20; ++i) {
        const double center = analysis::noiseStrengthSolverGetCenter(&solver, i);
        analysis::noiseStrengthSolverAddMeasurement(&solver, center, 5.0);
        analysis::noiseStrengthSolverAddMeasurement(&solver, center, 5.0);
    }
    CHECK(analysis::noiseStrengthSolverSolve(&solver) == 1);
    analysis::AomNoiseStrengthLut lut;
    CHECK(analysis::noiseStrengthSolverFitPiecewise(&solver, 20, &lut) == 1);
    CHECK(lut.numPoints == 2);
    CHECK(near(lut.points[0][0], 0.0));
    CHECK(near(lut.points[0][1], 5.0));
    CHECK(near(lut.points[1][0], 255.0));
    CHECK(near(lut.points[1][1], 5.0));
}

// ---------------------------------------------------------------------------
// T11a: a STRONG kink SURVIVES the fit (noise_model.c:377-394). Symmetric
// 3-bin family, stds (1, c, 1) fed once at each center: the solved values
// are u = x[0] = x[2] and v = x[1], hand-derived from the regularized
// system (e = 1/8192, t = mean*e = ((c+2)/3)e, k_alpha = 2*3/3 = 2,
// P = S = 3+e, R = 5+e):
//   v = ((c+t)*P + 2k*(1+t)) / (R*P - 2k^2),  u = (1 + t + k*v) / P
// The fit's survival test is avg_residual > k_tolerance with
// avg = r*(255/num_bins)/(points[2][0] - points[0][0]) = r/3 for the full
// 3-bin span (dx = 255/num_bins = 85 inside the residual, /255 for the
// lut span). c = 20: r = v - u ~ (c-1)/7 = 2.714 -> avg 0.905 >> 0.00625,
// so the kink is kept: num_points stays 3 with the exact values.
// ---------------------------------------------------------------------------
namespace {
void symmetric3ClosedForm(double c, double* u, double* v) {
    const double e = 1.0 / 8192.0;
    const double t = ((c + 2.0) / 3.0) * e;
    const double k = 2.0;
    const double P = 3.0 + e;
    const double R = 5.0 + e;
    *v = ((c + t) * P + 2.0 * k * (1.0 + t)) / (R * P - 2.0 * k * k);
    *u = (1.0 + t + k * (*v)) / P;
}
}  // namespace

TEST_CASE("fit_piecewise keeps a strong kink at three points") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 3, 8) == 1);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 0.0, 1.0);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 127.5, 20.0);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 255.0, 1.0);
    CHECK(analysis::noiseStrengthSolverSolve(&solver) == 1);
    double u = 0;
    double v = 0;
    symmetric3ClosedForm(20.0, &u, &v);
    analysis::AomNoiseStrengthLut lut;
    CHECK(analysis::noiseStrengthSolverFitPiecewise(&solver, 3, &lut) == 1);
    CHECK(lut.numPoints == 3);
    CHECK(near(lut.points[0][0], 0.0));
    CHECK(near(lut.points[0][1], u));
    CHECK(near(lut.points[1][0], 127.5));
    CHECK(near(lut.points[1][1], v));
    CHECK(near(lut.points[2][0], 255.0));
    CHECK(near(lut.points[2][1], u));
}

// ---------------------------------------------------------------------------
// T11b/T11c: the k_tolerance boundary (noise_model.c:357/:386), pinned
// from both sides with the symmetric 3-bin family where avg = r/3 and
// r ~ (c-1)/7. c = 1.2: r = 0.02857, avg = 0.009524 > 0.00625 -> the kink
// SURVIVES (and a doubled tolerance 0.0125 would remove it - this fixture
// kills that mutation). c = 1.1: r = 0.014304, avg = 0.004768 <= 0.00625
// -> the point is REMOVED to 2 (and a halved tolerance 0.003125 would keep
// it - kills that mutation).
// ---------------------------------------------------------------------------
TEST_CASE("fit_piecewise tolerance boundary keeps a 1.2 kink") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 3, 8) == 1);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 0.0, 1.0);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 127.5, 1.2);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 255.0, 1.0);
    CHECK(analysis::noiseStrengthSolverSolve(&solver) == 1);
    analysis::AomNoiseStrengthLut lut;
    CHECK(analysis::noiseStrengthSolverFitPiecewise(&solver, 3, &lut) == 1);
    CHECK(lut.numPoints == 3);
}

TEST_CASE("fit_piecewise tolerance boundary removes a 1.1 kink") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 3, 8) == 1);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 0.0, 1.0);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 127.5, 1.1);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 255.0, 1.0);
    CHECK(analysis::noiseStrengthSolverSolve(&solver) == 1);
    analysis::AomNoiseStrengthLut lut;
    CHECK(analysis::noiseStrengthSolverFitPiecewise(&solver, 3, &lut) == 1);
    CHECK(lut.numPoints == 2);
}

// ---------------------------------------------------------------------------
// T12: the dx = 255./num_bins HARDCODE in update_piecewise_linear_residual
// (noise_model.c:329) is NOT max_intensity/num_bins. At 10 bits the two
// differ (85 vs 341 for 3 bins) and the survival decision flips:
// stds (1, 2, 1) -> r = 0.142857; correct avg = r*85/1023 = 0.011867
// <= tol 0.025078 -> REMOVED to 2 points; the naive dx = 1023/3 gives
// avg = 0.047619 > tol -> would keep 3.
// ---------------------------------------------------------------------------
TEST_CASE("fit_piecewise residual dx is hardcoded 255 not max_intensity") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 3, 10) == 1);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 0.0, 1.0);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 511.5, 2.0);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 1023.0, 1.0);
    CHECK(analysis::noiseStrengthSolverSolve(&solver) == 1);
    analysis::AomNoiseStrengthLut lut;
    CHECK(analysis::noiseStrengthSolverFitPiecewise(&solver, 3, &lut) == 1);
    CHECK(lut.numPoints == 2);
}

// ---------------------------------------------------------------------------
// T13: add_measurement at a NON-center intensity (noise_model.c:250-265):
// block_mean halfway between center(4) and center(5) gives bin = 4.5, so
// a = 0.5 and the 2x2 weights are (1-a)^2 = 0.25 on the diagonal pair and
// a*(1-a) = 0.25 on both cross terms; b gets (1-a)*std = 4.5 and a*std =
// 4.5 (std = 9). Hand-derived exactly.
// ---------------------------------------------------------------------------
TEST_CASE("add_measurement fractional a accumulates the 2x2 weights") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 20, 8) == 1);
    const double mid45 = (4.5 / 19.0) * 255.0;
    analysis::noiseStrengthSolverAddMeasurement(&solver, mid45, 9.0);
    CHECK(near(solver.eqns.a[4 * 20 + 4], 0.25));
    CHECK(near(solver.eqns.a[5 * 20 + 4], 0.25));
    CHECK(near(solver.eqns.a[5 * 20 + 5], 0.25));
    CHECK(near(solver.eqns.a[4 * 20 + 5], 0.25));
    CHECK(near(solver.eqns.b[4], 4.5));
    CHECK(near(solver.eqns.b[5], 4.5));
    CHECK(near(solver.total, 9.0));
    CHECK(solver.numEquations == 1);
}

// ---------------------------------------------------------------------------
// T14: the clear/copy/free family (noise_model.c:104-129 for the equation
// system, :168-178 for the solver). clear zeroes A/b/x (and the solver's
// counters) but keeps the geometry (num_bins, min/max, eqns.n); copy
// duplicates everything; free empties the vectors and zeroes n.
// ---------------------------------------------------------------------------
TEST_CASE("solver and equation system clear copy free") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 3, 8) == 1);
    analysis::noiseStrengthSolverAddMeasurement(&solver, 0.0, 2.0);
    analysis::noiseStrengthSolverClear(&solver);
    CHECK(near(solver.eqns.a[0], 0.0));
    CHECK(near(solver.eqns.b[0], 0.0));
    CHECK(near(solver.total, 0.0));
    CHECK(solver.numEquations == 0);
    CHECK(solver.numBins == 3);
    CHECK(solver.eqns.n == 3);

    analysis::noiseStrengthSolverAddMeasurement(&solver, 0.0, 2.0);
    analysis::AomNoiseStrengthSolver dest;
    CHECK(analysis::noiseStrengthSolverInit(&dest, 3, 8) == 1);
    analysis::noiseStrengthSolverCopy(&dest, &solver);
    CHECK(near(dest.eqns.a[0], 1.0));
    CHECK(near(dest.eqns.b[0], 2.0));
    CHECK(near(dest.total, 2.0));
    CHECK(dest.numEquations == 1);

    analysis::equationSystemFree(&dest.eqns);
    CHECK(dest.eqns.n == 0);
    CHECK(dest.eqns.a.empty());
}

// ---------------------------------------------------------------------------
// T15: lut init/free edges (noise_model.c:214-234): a null lut returns 0;
// a valid init sets num_points and zero-fills; free resets the count.
// ---------------------------------------------------------------------------
TEST_CASE("lut init and free edges") {
    CHECK(analysis::noiseStrengthLutInit(nullptr, 4) == 0);
    analysis::AomNoiseStrengthLut lut;
    CHECK(analysis::noiseStrengthLutInit(&lut, 4) == 1);
    CHECK(lut.numPoints == 4);
    CHECK(lut.points.size() == 4);
    CHECK(near(lut.points[2][0], 0.0));
    CHECK(near(lut.points[2][1], 0.0));
    analysis::noiseStrengthLutFree(&lut);
    CHECK(lut.numPoints == 0);
    CHECK(lut.points.empty());
}

// ---------------------------------------------------------------------------
// T16a: noise_state_init (noise_model.c:192-201): k_num_bins = 20,
// ar_gain = 1.0, num_observations = 0, eqns sized n, strength solver
// 20 bins at the bit depth.
// ---------------------------------------------------------------------------
TEST_CASE("noise state init") {
    analysis::AomNoiseState state;
    CHECK(analysis::noiseStateInit(&state, 2, 8) == 1);
    CHECK(near(state.arGain, 1.0));
    CHECK(state.numObservations == 0);
    CHECK(state.eqns.n == 2);
    CHECK(state.strengthSolver.numBins == 20);
    CHECK(near(state.strengthSolver.maxIntensity, 255.0));
}

// ---------------------------------------------------------------------------
// T16b: set_chroma_coefficient_fallback_soln (noise_model.c:203-212).
// n = 2: x zeroed, then x[1] = b[1]/A[1][1] only when |A[1][1]| > 1e-6.
// A[1][1] = 4, b[1] = 8 -> x = [0, 2]. A[1][1] = 1e-8 -> x = [0, 0].
// ---------------------------------------------------------------------------
TEST_CASE("chroma fallback solution") {
    analysis::AomEquationSystem eqns;
    eqns.n = 2;
    eqns.a.assign(4, 0.0);
    eqns.b.assign(2, 0.0);
    eqns.x.assign(2, 7.0);
    eqns.a[3] = 4.0;
    eqns.b[1] = 8.0;
    analysis::setChromaCoefficientFallbackSoln(&eqns);
    CHECK(near(eqns.x[0], 0.0));
    CHECK(near(eqns.x[1], 2.0));

    eqns.x.assign(2, 7.0);
    eqns.a[3] = 1e-8;
    analysis::setChromaCoefficientFallbackSoln(&eqns);
    CHECK(near(eqns.x[0], 0.0));
    CHECK(near(eqns.x[1], 0.0));
}

// ---------------------------------------------------------------------------
// T17: equation_system_solve (noise_model.c:145-166): solves a known 2x2
// (A = [[2,1],[1,3]], x = [3,1] -> b = [7,6]), leaves A and b unchanged,
// and returns 0 on a singular system (A = [[1,2],[2,4]]).
// ---------------------------------------------------------------------------
TEST_CASE("equation system solve non destructive and singular") {
    analysis::AomEquationSystem eqns;
    eqns.n = 2;
    eqns.a = {2, 1, 1, 3};
    eqns.b = {7, 6};
    eqns.x.assign(2, 0.0);
    CHECK(analysis::equationSystemSolve(&eqns) == 1);
    CHECK(near(eqns.x[0], 3.0));
    CHECK(near(eqns.x[1], 1.0));
    CHECK(near(eqns.a[0], 2.0));
    CHECK(near(eqns.a[3], 3.0));
    CHECK(near(eqns.b[1], 6.0));

    analysis::AomEquationSystem sing;
    sing.n = 2;
    sing.a = {1, 2, 2, 4};
    sing.b = {1, 2};
    sing.x.assign(2, 0.0);
    CHECK(analysis::equationSystemSolve(&sing) == 0);
}

// ---------------------------------------------------------------------------
// T18: solver_solve's contract on the caller's data (noise_model.c:267-303):
// A is restored bit-for-bit after the solve, and b carries the reference's
// own PERSISTENT +mean/8192 write (NAMED QUIRK - the reference modifies
// solver->eqns.b in place, outside the copied-A protection; a second solve
// would accumulate again). Feed std 6 once per bin center (20 bins,
// mean = 6), solve, then check A == the accumulated measurement matrix
// (identity, all centers exact) and b[i] == 6 + 6/8192.
// ---------------------------------------------------------------------------
TEST_CASE("solver solve restores A and persists the mean term in b") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 20, 8) == 1);
    for (int32_t i = 0; i < 20; ++i) {
        analysis::noiseStrengthSolverAddMeasurement(
            &solver, analysis::noiseStrengthSolverGetCenter(&solver, i), 6.0);
    }
    CHECK(analysis::noiseStrengthSolverSolve(&solver) == 1);
    for (int32_t i = 0; i < 20; ++i) {
        CHECK(near(solver.eqns.b[i], 6.0 + 6.0 / 8192.0));
        for (int32_t j = 0; j < 20; ++j) {
            CHECK(near(solver.eqns.a[i * 20 + j], i == j ? 1.0 : 0.0));
        }
    }
}

// ---------------------------------------------------------------------------
// T19: the greedy removal takes the SMALLEST-residual interior point first
// and recomputes only around the removed index (noise_model.c:379-393).
// eqns.x is set directly (fit_piecewise consumes x; the reference signature
// is const) to the hand-chosen profile {1, 4.5, 8, 1} at 4 bins
// (centers 0, 85, 170, 255; residual dx = 255/4 = 63.75):
//   initial residual[1]: segment (0,1)-(170,8) estimates 4.5 at x=85 ->
//     r = 0 -> residual[1] = 0
//   initial residual[2]: segment (85,4.5)-(255,1) estimates 2.75 at x=170
//     -> r = |8 - 2.75| = 5.25 -> residual[2] = 334.6875
// Bin 1 (residual 0) is removed FIRST; then the windowed recompute
// (start = 0, end = 2) rebuilds residual[1] over the merged segment
// (0,1)-(255,1): estimates 1 at x=85 and x=170 -> r = |4.5-1| + |8-1|
// = 10.5 -> residual[1] = 669.375; avg = 669.375/255 = 2.625 > tol with
// num_points 3 <= max 4 -> break. Final: (0,1), (170,8), (255,1).
// A largest-first order mutation removes bin 2 first and cannot reach
// this shape; skipping the recompute leaves residual[1] = 0 and collapses
// to 2 points.
// ---------------------------------------------------------------------------
TEST_CASE("fit removes the smallest residual first and recomputes around it") {
    analysis::AomNoiseStrengthSolver solver;
    CHECK(analysis::noiseStrengthSolverInit(&solver, 4, 8) == 1);
    solver.eqns.x = {1.0, 4.5, 8.0, 1.0};
    analysis::AomNoiseStrengthLut lut;
    CHECK(analysis::noiseStrengthSolverFitPiecewise(&solver, 4, &lut) == 1);
    CHECK(lut.numPoints == 3);
    CHECK(near(lut.points[0][0], 0.0));
    CHECK(near(lut.points[0][1], 1.0));
    CHECK(near(lut.points[1][0], 170.0));
    CHECK(near(lut.points[1][1], 8.0));
    CHECK(near(lut.points[2][0], 255.0));
    CHECK(near(lut.points[2][1], 1.0));
}
