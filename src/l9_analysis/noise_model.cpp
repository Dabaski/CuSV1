// AN16: the noise strength solver - the 1:1 port of the SVT-AV1 noise-model
// solver span (third_party/SVT-AV1/Source/Lib/Codec/noise_model.c:104-397)
// plus the private linsolve dependency (Codec/mathutils.h:22-65) this span
// cannot be ported without. Host only, no GPU twin (parked, named - the
// solver is a 20-bin double-precision least-squares solve, a poor GPU fit
// and out of the slice's scope).

#include <analysis.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace analysis {

// Private copy of linsolve (mathutils.h:22-65). The reference keeps it
// static INLINE in a header so every includer carries its own copy; the
// port carries this one copy in the l9 TU and exposes it in the analysis
// namespace for the tests (named deviation). Verbatim: forward elimination
// with partial pivoting (largest magnitude to the diagonal), the
// tiny_near_zero = 1e-16 singularity guards, backward substitution.
int32_t linsolve(int32_t n, double* a, int32_t stride, double* b, double* x) {
    const double tinyNearZero = 1.0E-16;
    int32_t i, j, k;
    double c;
    // Forward elimination
    for (k = 0; k < n - 1; k++) {
        // Bring the largest magnitude to the diagonal position
        for (i = n - 1; i > k; i--) {
            if (fabs(a[(i - 1) * stride + k]) < fabs(a[i * stride + k])) {
                for (j = 0; j < n; j++) {
                    c                     = a[i * stride + j];
                    a[i * stride + j]     = a[(i - 1) * stride + j];
                    a[(i - 1) * stride + j] = c;
                }
                c        = b[i];
                b[i]     = b[i - 1];
                b[i - 1] = c;
            }
        }
        for (i = k; i < n - 1; i++) {
            if (fabs(a[k * stride + k]) < tinyNearZero) {
                return 0;
            }
            c = a[(i + 1) * stride + k] / a[k * stride + k];
            for (j = 0; j < n; j++) {
                a[(i + 1) * stride + j] -= c * a[k * stride + j];
            }
            b[i + 1] -= c * b[k];
        }
    }
    // Backward substitution
    for (i = n - 1; i >= 0; i--) {
        if (fabs(a[i * stride + i]) < tinyNearZero) {
            return 0;
        }
        c = 0;
        for (j = i + 1; j <= n - 1; j++) {
            c += a[i * stride + j] * x[j];
        }
        x[i] = (b[i] - c) / a[i * stride + i];
    }

    return 1;
}

// 1:1 port of num_coeffs (noise_model.c:181-190). The (n*n)/2 SQUARE branch
// is integer division in the reference and stays integer here.
int32_t numCoeffs(const AomNoiseModelParams params) {
    const int32_t n = 2 * params.lag + 1;
    switch (params.shape) {
    case AomNoiseShape::Diamond:
        return params.lag * (params.lag + 1);
    case AomNoiseShape::Square:
        return (n * n) / 2;
    }
    return 0;
}

// 1:1 port of equation_system_init (noise_model.c:131-143). The reference
// mallocs A/b/x and clears them; the port sizes the vectors (zero-filled)
// - allocation cannot fail, so the SVT_ERROR branch is dead (named
// deviation). Returns 1 as the reference does on success.
int32_t equationSystemInit(AomEquationSystem* eqns, int32_t n) {
    eqns->a.assign(static_cast<std::size_t>(n) * n, 0.0);
    eqns->b.assign(n, 0.0);
    eqns->x.assign(n, 0.0);
    eqns->n = n;
    return 1;
}

// 1:1 port of svt_aom_noise_strength_solver_init (noise_model.c:305-316).
int32_t noiseStrengthSolverInit(AomNoiseStrengthSolver* solver, int32_t numBins,
                                int32_t bitDepth) {
    if (!solver) {
        return 0;
    }
    *solver = AomNoiseStrengthSolver{};
    solver->numBins = numBins;
    solver->minIntensity = 0;
    solver->maxIntensity = (1 << bitDepth) - 1;
    solver->total = 0;
    solver->numEquations = 0;
    return equationSystemInit(&solver->eqns, numBins);
}

// 1:1 port of svt_aom_noise_strength_solver_get_center (noise_model.c:
// 318-322).
double noiseStrengthSolverGetCenter(const AomNoiseStrengthSolver* solver, int32_t i) {
    const double range = solver->maxIntensity - solver->minIntensity;
    const int32_t n = solver->numBins;
    return ((double)i) / (n - 1) * range + solver->minIntensity;
}

// fclamp (definitions.h:696-698), private in the reference's Codec surface.
namespace {
double fclamp(double value, double low, double high) {
    return value < low ? low : (value > high ? high : value);
}
}  // namespace

// 1:1 port of noise_strength_solver_get_bin_index (noise_model.c:236-240).
double noiseStrengthSolverGetBinIndex(const AomNoiseStrengthSolver* solver, double value) {
    const double val = fclamp(value, solver->minIntensity, solver->maxIntensity);
    const double range = solver->maxIntensity - solver->minIntensity;
    return (solver->numBins - 1) * (val - solver->minIntensity) / range;
}

// 1:1 port of noise_strength_solver_get_value (noise_model.c:242-248).
double noiseStrengthSolverGetValue(const AomNoiseStrengthSolver* solver, double x) {
    const double bin = noiseStrengthSolverGetBinIndex(solver, x);
    const int32_t binI0 = static_cast<int32_t>(floor(bin));
    const int32_t binI1 = std::min(solver->numBins - 1, binI0 + 1);
    const double a = bin - binI0;
    return (1.0 - a) * solver->eqns.x[binI0] + a * solver->eqns.x[binI1];
}

// 1:1 port of svt_aom_noise_strength_solver_add_measurement (noise_model.c:
// 250-265). AOMMIN/AOMMAX fold to std::min (identical semantics on int32).
void noiseStrengthSolverAddMeasurement(AomNoiseStrengthSolver* solver, double blockMean,
                                       double noiseStd) {
    const double bin = noiseStrengthSolverGetBinIndex(solver, blockMean);
    const int32_t binI0 = static_cast<int32_t>(floor(bin));
    const int32_t binI1 = std::min(solver->numBins - 1, binI0 + 1);
    const double a = bin - binI0;
    const int32_t n = solver->numBins;
    solver->eqns.a[binI0 * n + binI0] += (1.0 - a) * (1.0 - a);
    solver->eqns.a[binI1 * n + binI0] += a * (1.0 - a);
    solver->eqns.a[binI1 * n + binI1] += a * a;
    solver->eqns.a[binI0 * n + binI1] += a * (1.0 - a);
    solver->eqns.b[binI0] += (1.0 - a) * noiseStd;
    solver->eqns.b[binI1] += a * noiseStd;
    solver->total += noiseStd;
    solver->numEquations++;
}

// 1:1 port of equation_system_solve (noise_model.c:145-166).
int32_t equationSystemSolve(AomEquationSystem* eqns) {
    const int32_t n = eqns->n;
    std::vector<double> b(eqns->b);
    std::vector<double> a(eqns->a);
    const int32_t ret = linsolve(n, a.data(), eqns->n, b.data(), eqns->x.data());
    if (ret == 0) {
        return 0;
    }
    return 1;
}

// 1:1 port of svt_aom_noise_strength_solver_solve (noise_model.c:267-303).
int32_t noiseStrengthSolverSolve(AomNoiseStrengthSolver* solver) {
    // Add regularization proportional to the number of constraints
    const int32_t n = solver->numBins;
    const double kAlpha = 2.0 * (double)(solver->numEquations) / n;
    int32_t result = 0;
    double mean = 0;

    // Do this in a non-destructive manner so it is not confusing to the
    // caller. The reference mallocs a copy of A and swaps the eqns.A pointer
    // for the duration of the solve; the port holds the copy in a local
    // vector and assigns it (named deviation, same semantics).
    std::vector<double> oldA = solver->eqns.a;
    std::vector<double> regA = oldA;

    for (int32_t i = 0; i < n; ++i) {
        const int32_t iLo = std::max(0, i - 1);
        const int32_t iHi = std::min(n - 1, i + 1);
        regA[i * n + iLo] -= kAlpha;
        regA[i * n + i] += 2 * kAlpha;
        regA[i * n + iHi] -= kAlpha;
    }

    // Small regularization to give average noise strength
    mean = solver->total / solver->numEquations;
    for (int32_t i = 0; i < n; ++i) {
        regA[i * n + i] += 1.0 / 8192.;
        solver->eqns.b[i] += mean / 8192.;
    }
    solver->eqns.a = regA;
    result = equationSystemSolve(&solver->eqns);
    solver->eqns.a = oldA;

    return result;
}

// 1:1 port of svt_aom_noise_strength_lut_init (noise_model.c:214-225).
int32_t noiseStrengthLutInit(AomNoiseStrengthLut* lut, int32_t numPoints) {
    if (!lut) {
        return 0;
    }
    lut->points.assign(numPoints, {0.0, 0.0});
    lut->numPoints = numPoints;
    return 1;
}

// 1:1 port of svt_aom_noise_strength_lut_free (noise_model.c:227-234).
void noiseStrengthLutFree(AomNoiseStrengthLut* lut) {
    if (!lut) {
        return;
    }
    lut->points.clear();
    lut->numPoints = 0;
}

// 1:1 ports of the equation-system family (noise_model.c:104-129).
void equationSystemFree(AomEquationSystem* eqns) {
    if (!eqns) {
        return;
    }
    eqns->a.clear();
    eqns->b.clear();
    eqns->x.clear();
    eqns->n = 0;
}

void equationSystemClear(AomEquationSystem* eqns) {
    const int32_t n = eqns->n;
    std::fill(eqns->a.begin(), eqns->a.end(), 0.0);
    std::fill(eqns->x.begin(), eqns->x.end(), 0.0);
    std::fill(eqns->b.begin(), eqns->b.end(), 0.0);
    (void)n;
}

void equationSystemCopy(AomEquationSystem* dst, const AomEquationSystem* src) {
    const int32_t n = dst->n;
    std::copy(src->a.begin(), src->a.begin() + static_cast<std::ptrdiff_t>(n) * n, dst->a.begin());
    std::copy(src->x.begin(), src->x.begin() + n, dst->x.begin());
    std::copy(src->b.begin(), src->b.begin() + n, dst->b.begin());
    (void)n;
}

// 1:1 ports of noise_strength_solver_clear/copy (noise_model.c:168-178).
void noiseStrengthSolverClear(AomNoiseStrengthSolver* solver) {
    equationSystemClear(&solver->eqns);
    solver->numEquations = 0;
    solver->total = 0;
}

void noiseStrengthSolverCopy(AomNoiseStrengthSolver* dest, const AomNoiseStrengthSolver* src) {
    equationSystemCopy(&dest->eqns, &src->eqns);
    dest->numEquations = src->numEquations;
    dest->total = src->total;
}

// 1:1 port of noise_state_init (noise_model.c:192-201).
int32_t noiseStateInit(AomNoiseState* state, int32_t n, int32_t bitDepth) {
    const int32_t kNumBins = 20;
    if (!equationSystemInit(&state->eqns, n)) {
        return 0;
    }
    state->arGain = 1.0;
    state->numObservations = 0;
    return noiseStrengthSolverInit(&state->strengthSolver, kNumBins, bitDepth);
}

// 1:1 port of set_chroma_coefficient_fallback_soln (noise_model.c:203-212).
void setChromaCoefficientFallbackSoln(AomEquationSystem* eqns) {
    const double kTolerance = 1e-6;
    const int32_t last = eqns->n - 1;
    // Set all of the AR coefficients to zero, but try to solve for
    // correlation with the luma channel
    std::fill(eqns->x.begin(), eqns->x.end(), 0.0);
    if (fabs(eqns->a[last * eqns->n + last]) > kTolerance) {
        eqns->x[last] = eqns->b[last] / eqns->a[last * eqns->n + last];
    }
}

// 1:1 port of update_piecewise_linear_residual (noise_model.c:327-351).
// The dx = 255. denominator is HARDCODED in the reference - it is NOT
// max_intensity (a 10-bit solver still divides by 255); ported as-is.
static void updatePiecewiseLinearResidual(const AomNoiseStrengthSolver* solver,
                                          const AomNoiseStrengthLut* lut, double* residual,
                                          int32_t start, int32_t end) {
    const double dx = 255. / solver->numBins;
    for (int32_t i = std::max(start, 1); i < std::min(end, lut->numPoints - 1); ++i) {
        const int32_t lower = std::max(
            0, static_cast<int32_t>(floor(noiseStrengthSolverGetBinIndex(solver, lut->points[i - 1][0]))));
        const int32_t upper =
            std::min(solver->numBins - 1,
                     static_cast<int32_t>(ceil(noiseStrengthSolverGetBinIndex(solver, lut->points[i + 1][0]))));
        double r = 0;
        for (int32_t j = lower; j <= upper; ++j) {
            const double x = noiseStrengthSolverGetCenter(solver, j);
            if (x < lut->points[i - 1][0]) {
                continue;
            }
            if (x >= lut->points[i + 1][0]) {
                continue;
            }
            const double y = solver->eqns.x[j];
            const double a = (x - lut->points[i - 1][0]) / (lut->points[i + 1][0] - lut->points[i - 1][0]);
            const double estimateY = lut->points[i - 1][1] * (1.0 - a) + lut->points[i + 1][1] * a;
            r += fabs(y - estimateY);
        }
        residual[i] = r * dx;
    }
}

// 1:1 port of svt_aom_noise_strength_solver_fit_piecewise (noise_model.c:
// 353-397). The reference's memmove becomes vector::erase (named
// deviation, same effect).
int32_t noiseStrengthSolverFitPiecewise(const AomNoiseStrengthSolver* solver,
                                        int32_t maxOutputPoints, AomNoiseStrengthLut* lut) {
    // The tolerance is normalized to be give consistent results between
    // different bit-depths.
    const double kTolerance = solver->maxIntensity * 0.00625 / 255.0;
    if (!noiseStrengthLutInit(lut, solver->numBins)) {
        return 0;
    }
    for (int32_t i = 0; i < solver->numBins; ++i) {
        lut->points[i][0] = noiseStrengthSolverGetCenter(solver, i);
        lut->points[i][1] = solver->eqns.x[i];
    }
    if (maxOutputPoints < 0) {
        maxOutputPoints = solver->numBins;
    }
    std::vector<double> residual(solver->numBins, 0.0);

    updatePiecewiseLinearResidual(solver, lut, residual.data(), 0, solver->numBins);

    // Greedily remove points if there are too many or if it doesn't hurt
    // local approximation (never remove the end points)
    while (lut->numPoints > 2) {
        int32_t minIndex = 1;
        for (int32_t j = 1; j < lut->numPoints - 1; ++j) {
            if (residual[j] < residual[minIndex]) {
                minIndex = j;
            }
        }
        const double dx = lut->points[minIndex + 1][0] - lut->points[minIndex - 1][0];
        const double avgResidual = residual[minIndex] / dx;
        if (lut->numPoints <= maxOutputPoints && avgResidual > kTolerance) {
            break;
        }
        lut->points.erase(lut->points.begin() + minIndex);
        lut->numPoints--;

        updatePiecewiseLinearResidual(solver, lut, residual.data(), minIndex - 1, minIndex + 1);
    }
    return 1;
}

}  // namespace analysis
