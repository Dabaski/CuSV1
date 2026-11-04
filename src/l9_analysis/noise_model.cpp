// AN16: the noise strength solver - the 1:1 port of the SVT-AV1 noise-model
// solver span (third_party/SVT-AV1/Source/Lib/Codec/noise_model.c:104-397)
// plus the private linsolve dependency (Codec/mathutils.h:22-65) this span
// cannot be ported without. Host only, no GPU twin (parked, named - the
// solver is a 20-bin double-precision least-squares solve, a poor GPU fit
// and out of the slice's scope).

#include <analysis.h>

#include <algorithm>
#include <cassert>
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

// ---------------------------------------------------------------------------
// AN17: the flat-block finder + the noise model init/update
// (noise_model.c:27-:102 helper span, :399-:634 finder, :636-:1157 model).
// The highbd (uint16) instantiations of the GET_BLOCK_MEAN / GET_NOISE_VAR /
// EXTRACT_AR_ROW macro families are PARKED (8-bit discipline, named); the
// use_highbd dispatcher branches collapse with them.
// ---------------------------------------------------------------------------

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

// 1:1 port of get_block_mean_lowbd (noise_model.c:39-53 + :56-:62, the
// GET_BLOCK_MEAN macro's lowbd instantiation, dispatcher branch folded).
double getBlockMean(const uint8_t* data, int32_t w, int32_t h, int32_t stride,
                    int32_t x_o, int32_t y_o, int32_t block_size) {
    const int32_t max_h = std::min(h - y_o, block_size);
    const int32_t max_w = std::min(w - x_o, block_size);
    double block_mean = 0;
    for (int32_t y = 0; y < max_h; ++y) {
        for (int32_t x = 0; x < max_w; ++x) {
            block_mean += data[(y_o + y) * stride + x_o + x];
        }
    }
    return block_mean / (max_w * max_h);
}

// 1:1 port of get_noise_var_lowbd (noise_model.c:66-92 + :94-:102, the
// GET_NOISE_VAR macro's lowbd instantiation, dispatcher branch folded).
double getNoiseVar(const uint8_t* data, const uint8_t* denoised, int32_t stride,
                   int32_t w, int32_t h, int32_t x_o, int32_t y_o,
                   int32_t block_size_x, int32_t block_size_y) {
    const int32_t max_h = std::min(h - y_o, block_size_y);
    const int32_t max_w = std::min(w - x_o, block_size_x);
    double noise_var = 0;
    double noise_mean = 0;
    for (int32_t y = 0; y < max_h; ++y) {
        for (int32_t x = 0; x < max_w; ++x) {
            double noise = (double)data[(y_o + y) * stride + x_o + x] - denoised[(y_o + y) * stride + x_o + x];
            noise_mean += noise;
            noise_var += noise * noise;
        }
    }
    noise_mean /= (max_w * max_h);
    return noise_var / (max_w * max_h) - noise_mean * noise_mean;
}

// 1:1 port of svt_aom_flat_block_finder_init (noise_model.c:399-454). The
// malloc'd at_a_inv/A become vectors (named deviation).
int32_t flatBlockFinderInit(AomFlatBlockFinder* block_finder, int32_t block_size,
                            int32_t bit_depth, int32_t use_highbd) {
    const int32_t n = block_size * block_size;
    AomEquationSystem eqns;
    equationSystemInit(&eqns, kLowPolyNumParams);

    block_finder->atAInv.assign(kLowPolyNumParams * kLowPolyNumParams, 0.0);
    block_finder->a.assign(kLowPolyNumParams * n, 0.0);
    block_finder->blockSize = block_size;
    block_finder->numParams = kLowPolyNumParams;
    block_finder->normalization = (1 << bit_depth) - 1;
    block_finder->useHighbd = use_highbd;

    for (int32_t y = 0; y < block_size; ++y) {
        const double yd = ((double)y - block_size / 2.) / (block_size / 2.);
        for (int32_t x = 0; x < block_size; ++x) {
            const double xd = ((double)x - block_size / 2.) / (block_size / 2.);
            const double coords[3] = {yd, xd, 1};
            const int32_t row = y * block_size + x;
            block_finder->a[kLowPolyNumParams * row + 0] = yd;
            block_finder->a[kLowPolyNumParams * row + 1] = xd;
            block_finder->a[kLowPolyNumParams * row + 2] = 1;

            for (int i = 0; i < kLowPolyNumParams; ++i) {
                for (int j = 0; j < kLowPolyNumParams; ++j) {
                    eqns.a[kLowPolyNumParams * i + j] += coords[i] * coords[j];
                }
            }
        }
    }

    // Lazy inverse using existing equation solver.
    for (int i = 0; i < kLowPolyNumParams; ++i) {
        std::fill(eqns.b.begin(), eqns.b.end(), 0.0);
        eqns.b[i] = 1;
        equationSystemSolve(&eqns);

        for (int j = 0; j < kLowPolyNumParams; ++j) {
            block_finder->atAInv[j * kLowPolyNumParams + i] = eqns.x[j];
        }
    }
    equationSystemFree(&eqns);
    return 1;
}

// 1:1 port of svt_aom_flat_block_finder_free (noise_model.c:456-463).
void flatBlockFinderFree(AomFlatBlockFinder* block_finder) {
    if (!block_finder) {
        return;
    }
    block_finder->a.clear();
    block_finder->atAInv.clear();
    *block_finder = AomFlatBlockFinder{};
}

// multiply_mat_1_n_3 / multiply_mat_3_3_1 / multiply_mat_n_3_1
// (noise_model.h:322-:366), private helpers of the extract path.
static void multiplyMat1N3(const double* m1, const double* m2, double* res, const int32_t inner_dim) {
    double sum0 = 0, sum1 = 0, sum2 = 0;
    int32_t inner_m3 = 0;

    for (int32_t inner = 0; inner < inner_dim; ++inner, inner_m3 += 3) {
        const double m1_inner = m1[inner];
        sum0 += m1_inner * m2[inner_m3 + 0];
        sum1 += m1_inner * m2[inner_m3 + 1];
        sum2 += m1_inner * m2[inner_m3 + 2];
    }

    *(res++) = sum0;
    *(res++) = sum1;
    *(res++) = sum2;
}

static void multiplyMat331(const double* m1, const double* m2, double* res) {
    double sum0, sum1, sum2;

    sum0 = m1[0 * 3 + 0] * m2[0 * 1 + 0];
    sum0 += m1[0 * 3 + 1] * m2[1 * 1 + 0];
    sum0 += m1[0 * 3 + 2] * m2[2 * 1 + 0];
    *(res++) = sum0;

    sum1 = m1[1 * 3 + 0] * m2[0 * 1 + 0];
    sum1 += m1[1 * 3 + 1] * m2[1 * 1 + 0];
    sum1 += m1[1 * 3 + 2] * m2[2 * 1 + 0];
    *(res++) = sum1;

    sum2 = m1[2 * 3 + 0] * m2[0 * 1 + 0];
    sum2 += m1[2 * 3 + 1] * m2[1 * 1 + 0];
    sum2 += m1[2 * 3 + 2] * m2[2 * 1 + 0];
    *(res++) = sum2;
}

static void multiplyMatN31(const double* m1, const double* m2, double* res, const int32_t m1_rows) {
    int32_t row_m3 = 0;

    for (int32_t row = 0; row < m1_rows; ++row, row_m3 += 3) {
        double sum = m1[row_m3 + 0] * m2[0 * 1 + 0];
        sum += m1[row_m3 + 1] * m2[1 * 1 + 0];
        sum += m1[row_m3 + 2] * m2[2 * 1 + 0];
        *(res++) = sum;
    }
}

// clamp (definitions.h), private in the reference's Codec surface.
namespace {
int32_t clampi(int32_t value, int32_t low, int32_t high) {
    return value < low ? low : (value > high ? high : value);
}
}  // namespace

// 1:1 port of svt_aom_flat_block_finder_extract_block_c (noise_model.c:
// 465-:508). The use_highbd branch (:477-:485) is PARKED with the highbd
// macro family - named deviation; the lowbd path is verbatim. kLowPolyNumParams
// is 3, so the #if (kLowPolyNumParams == 3) specialized matrix path (:495-
// :498) is the live one.
void flatBlockFinderExtractBlock(const AomFlatBlockFinder* block_finder, const uint8_t* const data,
                                 int32_t w, int32_t h, int32_t stride, int32_t offsx, int32_t offsy,
                                 double* plane, double* block) {
    const int32_t block_size = block_finder->blockSize;
    const int32_t n = block_size * block_size;
    const double* A = block_finder->a.data();
    const double* at_a_inv = block_finder->atAInv.data();
    const double recp_norm = 1 / block_finder->normalization;
    double plane_coords[kLowPolyNumParams];
    double at_a_inv__b[kLowPolyNumParams];

    for (int32_t yi = 0; yi < block_size; ++yi) {
        const int32_t y = clampi(offsy + yi, 0, h - 1);
        for (int32_t xi = 0; xi < block_size; ++xi) {
            const int32_t x = clampi(offsx + xi, 0, w - 1);
            block[yi * block_size + xi] = ((double)data[y * stride + x]) * recp_norm;
        }
    }
    multiplyMat1N3(block, A, at_a_inv__b, n);
    multiplyMat331(at_a_inv, at_a_inv__b, plane_coords);
    multiplyMatN31(A, plane_coords, plane, n);

    for (int32_t i = 0; i < n; ++i) {
        block[i] -= plane[i];
    }
}

// IndexAndscore + compare_scores (noise_model.c:510-:518). File-local in
// the reference (the struct is anonymous); kept file-local here - the tests
// pin the run() output map, not the sort itself.
namespace {
struct IndexAndscore {
    int32_t index;
    float score;
};

int compareScores(const void* a, const void* b) {
    const float diff = ((IndexAndscore*)a)->score - ((IndexAndscore*)b)->score;
    return diff < 0 ? -1 : diff > 0;
}
}  // namespace

// 1:1 port of svt_aom_flat_block_finder_run (noise_model.c:520-:634).
int32_t flatBlockFinderRun(const AomFlatBlockFinder* block_finder, const uint8_t* const data, int32_t w,
                           int32_t h, int32_t stride, uint8_t* flat_blocks) {
    const int32_t block_size = block_finder->blockSize;
    const int32_t n = block_size * block_size;
    const double k_trace_threshold = 0.15 / (32 * 32);
    const double k_ratio_threshold = 1.25;
    const double k_norm_threshold = 0.08 / (32 * 32);
    const double k_var_threshold = 0.005 / (double)n;
    const int32_t num_blocks_w = (w + block_size - 1) / block_size;
    const int32_t num_blocks_h = (h + block_size - 1) / block_size;
    int32_t num_flat = 0;
    std::vector<double> plane(n);
    std::vector<double> block(n);
    std::vector<IndexAndscore> scores(num_blocks_w * num_blocks_h);

    for (int32_t by = 0; by < num_blocks_h; ++by) {
        for (int32_t bx = 0; bx < num_blocks_w; ++bx) {
            // Compute gradient covariance matrix.
            double g_xx = 0, g_xy = 0, g_yy = 0;
            double var = 0;
            double mean = 0;
            flatBlockFinderExtractBlock(
                block_finder, data, w, h, stride, bx * block_size, by * block_size, plane.data(), block.data());

            for (int32_t yi = 1; yi < block_size - 1; ++yi) {
                for (int32_t xi = 1; xi < block_size - 1; ++xi) {
                    const double gx = (block[yi * block_size + xi + 1] - block[yi * block_size + xi - 1]) / 2;
                    const double gy = (block[yi * block_size + xi + block_size] -
                                       block[yi * block_size + xi - block_size]) /
                        2;
                    g_xx += gx * gx;
                    g_xy += gx * gy;
                    g_yy += gy * gy;

                    mean += block[yi * block_size + xi];
                    var += block[yi * block_size + xi] * block[yi * block_size + xi];
                }
            }
            mean /= (block_size - 2) * (block_size - 2);

            // Normalize gradients by BlockSize.
            g_xx /= ((block_size - 2) * (block_size - 2));
            g_xy /= ((block_size - 2) * (block_size - 2));
            g_yy /= ((block_size - 2) * (block_size - 2));
            var = var / ((block_size - 2) * (block_size - 2)) - mean * mean;

            {
                const double trace = g_xx + g_yy;
                const double det = g_xx * g_yy - g_xy * g_xy;
                const double e1 = (trace + sqrt(trace * trace - 4 * det)) / 2.;
                const double e2 = (trace - sqrt(trace * trace - 4 * det)) / 2.;
                const double norm = e1;  // Spectral norm
                const double ratio = (e1 / std::max(e2, 1e-6));
                const int32_t is_flat = (trace < k_trace_threshold) && (ratio < k_ratio_threshold) &&
                    (norm < k_norm_threshold) && (var > k_var_threshold);
                // The weights combine the features into a sigmoid score for
                // flatness, in the order [{var}, {ratio}, {trace}, {norm},
                // offset] (noise_model.c:590-:597).
                const double weights[5] = {-6682, -0.2056, 13087, -12434, 2.5694};
                const float score = (float)(1.0 /
                                            (1 +
                                             exp(-(weights[0] * var + weights[1] * ratio + weights[2] * trace +
                                                   weights[3] * norm + weights[4]))));
                flat_blocks[by * num_blocks_w + bx] = is_flat ? 255 : 0;
                scores[by * num_blocks_w + bx].score = var > k_var_threshold ? score : 0;
                scores[by * num_blocks_w + bx].index = by * num_blocks_w + bx;
                num_flat += is_flat;
            }
        }
    }
    // Find the top-scored blocks (most likely to be flat) and set the flat
    // blocks be the union of the thresholded results and the top 10th
    // percentile of the scored results.
    qsort(scores.data(), num_blocks_w * num_blocks_h, sizeof(*scores.data()), &compareScores);
    const int32_t top_nth_percentile = num_blocks_w * num_blocks_h * 90 / 100;
    const float score_threshold = scores[top_nth_percentile].score;
    for (int32_t i = 0; i < num_blocks_w * num_blocks_h; ++i) {
        if (scores[i].score >= score_threshold) {
            num_flat += flat_blocks[scores[i].index] == 0;
            flat_blocks[scores[i].index] |= 1;
        }
    }
    return num_flat;
}

namespace {
constexpr int32_t kMaxLag = 4;  // noise_model.c:27
}  // namespace

// 1:1 port of svt_aom_noise_model_init (noise_model.c:636-:697). The
// malloc'd coords become a vector (named deviation).
int32_t noiseModelInit(AomNoiseModel* model, const AomNoiseModelParams params) {
    const int32_t n = numCoeffs(params);
    const int32_t lag = params.lag;
    const int32_t bit_depth = params.bitDepth;

    *model = AomNoiseModel{};
    if (params.lag < 1) {
        return 0;
    }
    if (params.lag > kMaxLag) {
        return 0;
    }
    model->params = params;

    for (int32_t c = 0; c < 3; ++c) {
        if (!noiseStateInit(&model->combinedState[c], n + (c > 0), bit_depth)) {
            noiseModelFree(model);
            return 0;
        }
        if (!noiseStateInit(&model->latestState[c], n + (c > 0), bit_depth)) {
            noiseModelFree(model);
            return 0;
        }
    }
    model->n = n;
    model->coords.resize(n);
    int32_t i = 0;
    for (int32_t y = -lag; y <= 0; ++y) {
        const int32_t max_x = y == 0 ? -1 : lag;
        for (int32_t x = -lag; x <= max_x; ++x) {
            switch (params.shape) {
            case AomNoiseShape::Diamond:
                if (abs(x) <= y + lag) {
                    model->coords[i][0] = x;
                    model->coords[i][1] = y;
                    ++i;
                }
                break;
            case AomNoiseShape::Square:
                model->coords[i][0] = x;
                model->coords[i][1] = y;
                ++i;
                break;
            }
        }
    }
    assert(i == n);
    return 1;
}

// 1:1 port of svt_aom_noise_model_free (noise_model.c:699-:714).
void noiseModelFree(AomNoiseModel* model) {
    if (!model) {
        return;
    }
    model->coords.clear();
    for (int32_t c = 0; c < 3; ++c) {
        equationSystemFree(&model->latestState[c].eqns);
        equationSystemFree(&model->combinedState[c].eqns);

        equationSystemFree(&model->latestState[c].strengthSolver.eqns);
        equationSystemFree(&model->combinedState[c].strengthSolver.eqns);
    }
    *model = AomNoiseModel{};
}

// 1:1 port of the EXTRACT_AR_ROW macro's lowbd instantiation
// (noise_model.c:719-:756). The highbd instantiation (:757) is PARKED -
// named deviation.
double extractArRow(const std::array<int32_t, 2>* coords,
                    int32_t numCoords,
                    const uint8_t* data, const uint8_t* denoised,
                    int32_t stride, const int32_t* sub_log2,
                    const uint8_t* altData, const uint8_t* altDenoised,
                    int32_t altStride, int32_t x, int32_t y,
                    double* buffer) {
    for (int32_t i = 0; i < numCoords; ++i) {
        const int32_t x_i = x + coords[i][0], y_i = y + coords[i][1];
        buffer[i] = (double)data[y_i * stride + x_i] - denoised[y_i * stride + x_i];
    }
    const double val = (double)data[y * stride + x] - denoised[y * stride + x];

    if (altData && altDenoised) {
        double avg_data = 0, avg_denoised = 0;
        int32_t num_samples = 0;
        for (int32_t dy_i = 0; dy_i < (1 << sub_log2[1]); dy_i++) {
            const int32_t y_up = (y << sub_log2[1]) + dy_i;
            for (int32_t dx_i = 0; dx_i < (1 << sub_log2[0]); dx_i++) {
                const int32_t x_up = (x << sub_log2[0]) + dx_i;
                avg_data += altData[y_up * altStride + x_up];
                avg_denoised += altDenoised[y_up * altStride + x_up];
                num_samples++;
            }
        }
        assert(num_samples > 0);
        buffer[numCoords] = (avg_data - avg_denoised) / num_samples;
    }
    return val;
}

// 1:1 port of svt_av1_add_block_observations_internal_c (noise_model.c:
// 759-:803).
void addBlockObservationsInternal(uint32_t n, const double val, const double recp_sqr_norm, const double* buffer,
                                  double* buffer_norm, double* b, double* A) {
    uint32_t i;
    for (i = 0; i + 8 - 1 < n; i += 8) {
        buffer_norm[i + 0] = buffer[i + 0] * recp_sqr_norm;
        buffer_norm[i + 1] = buffer[i + 1] * recp_sqr_norm;
        buffer_norm[i + 2] = buffer[i + 2] * recp_sqr_norm;
        buffer_norm[i + 3] = buffer[i + 3] * recp_sqr_norm;
        buffer_norm[i + 4] = buffer[i + 4] * recp_sqr_norm;
        buffer_norm[i + 5] = buffer[i + 5] * recp_sqr_norm;
        buffer_norm[i + 6] = buffer[i + 6] * recp_sqr_norm;
        buffer_norm[i + 7] = buffer[i + 7] * recp_sqr_norm;
        b[i + 0] += buffer_norm[i + 0] * val;
        b[i + 1] += buffer_norm[i + 1] * val;
        b[i + 2] += buffer_norm[i + 2] * val;
        b[i + 3] += buffer_norm[i + 3] * val;
        b[i + 4] += buffer_norm[i + 4] * val;
        b[i + 5] += buffer_norm[i + 5] * val;
        b[i + 6] += buffer_norm[i + 6] * val;
        b[i + 7] += buffer_norm[i + 7] * val;
    }
    for (; i < n; ++i) {
        buffer_norm[i] = buffer[i] * recp_sqr_norm;
        b[i] += buffer_norm[i] * val;
    }

    for (i = 0; i < n; ++i) {
        uint32_t j = 0;
        const double buffer_norm_i = buffer_norm[i];

        for (j = 0; j + 8 - 1 < n; j += 8) {
            A[i * n + j + 0] += (buffer_norm_i * buffer[j + 0]);
            A[i * n + j + 1] += (buffer_norm_i * buffer[j + 1]);
            A[i * n + j + 2] += (buffer_norm_i * buffer[j + 2]);
            A[i * n + j + 3] += (buffer_norm_i * buffer[j + 3]);
            A[i * n + j + 4] += (buffer_norm_i * buffer[j + 4]);
            A[i * n + j + 5] += (buffer_norm_i * buffer[j + 5]);
            A[i * n + j + 6] += (buffer_norm_i * buffer[j + 6]);
            A[i * n + j + 7] += (buffer_norm_i * buffer[j + 7]);
        }
        for (; j < n; ++j) {
            A[i * n + j] += (buffer_norm_i * buffer[j]);
        }
    }
}

// 1:1 port of add_block_observations (noise_model.c:805-:890). The
// EB_MALLOC_ALIGNED buffers become vectors and their failure branches are
// dead (named deviation); the use_highbd ternary is PARKED with the highbd
// macro family - the lowbd path is taken unconditionally.
int32_t addBlockObservations(AomNoiseModel* noise_model, int32_t c, const uint8_t* const data,
                             const uint8_t* const denoised, int32_t w, int32_t h, int32_t stride,
                             const int32_t* sub_log2, const uint8_t* const alt_data,
                             const uint8_t* const alt_denoised, int32_t alt_stride,
                             const uint8_t* const flat_blocks, int32_t block_size, int32_t num_blocks_w,
                             int32_t num_blocks_h) {
    const int32_t lag = noise_model->params.lag;
    const int32_t num_coords = noise_model->n;
    const double normalization = (1 << noise_model->params.bitDepth) - 1;
    const double recp_sqr_norm = 1 / (normalization * normalization);
    double* A = noise_model->latestState[c].eqns.a.data();
    double* b = noise_model->latestState[c].eqns.b.data();
    const int32_t n = noise_model->latestState[c].eqns.n;
    std::vector<double> buffer(num_coords + 1, 0.0);
    std::vector<double> buffer_norm(n, 0.0);

    for (int32_t by = 0; by < num_blocks_h; ++by) {
        const int32_t y_o = by * (block_size >> sub_log2[1]);
        for (int32_t bx = 0; bx < num_blocks_w; ++bx) {
            const int32_t x_o = bx * (block_size >> sub_log2[0]);
            if (!flat_blocks[by * num_blocks_w + bx]) {
                continue;
            }
            int32_t y_start = (by > 0 && flat_blocks[(by - 1) * num_blocks_w + bx]) ? 0 : lag;
            int32_t x_start = (bx > 0 && flat_blocks[by * num_blocks_w + bx - 1]) ? 0 : lag;
            int32_t y_end = std::min((h >> sub_log2[1]) - by * (block_size >> sub_log2[1]), block_size >> sub_log2[1]);
            int32_t x_end = std::min((w >> sub_log2[0]) - bx * (block_size >> sub_log2[0]) - lag,
                                     (bx + 1 < num_blocks_w && flat_blocks[by * num_blocks_w + bx + 1])
                                         ? (block_size >> sub_log2[0])
                                         : ((block_size >> sub_log2[0]) - lag));
            for (int32_t y = y_start; y < y_end; ++y) {
                for (int32_t x = x_start; x < x_end; ++x) {
                    const double val = extractArRow(noise_model->coords.data(),
                                                    num_coords,
                                                    data,
                                                    denoised,
                                                    stride,
                                                    sub_log2,
                                                    alt_data,
                                                    alt_denoised,
                                                    alt_stride,
                                                    x + x_o,
                                                    y + y_o,
                                                    buffer.data());

                    addBlockObservationsInternal(n, val, recp_sqr_norm, buffer.data(), buffer_norm.data(), b, A);
                }
            }
            // There is situation when x_start is greater than x_end,
            // use max() to cap negative result and do not increment
            // num_observations
            noise_model->latestState[c].numObservations += std::max((y_end - y_start), 0) *
                std::max((x_end - x_start), 0);
        }
    }
    return 1;
}

// 1:1 port of add_noise_std_observations (noise_model.c:892-:956). The
// use_highbd arguments are PARKED with the highbd macro family - named
// deviation.
void addNoiseStdObservations(AomNoiseModel* noise_model, int32_t c, const double* coeffs,
                             const uint8_t* const data, const uint8_t* const denoised, int32_t w, int32_t h,
                             int32_t stride, const int32_t* sub_log2, const uint8_t* const alt_data,
                             int32_t alt_stride, const uint8_t* const flat_blocks, int32_t block_size,
                             int32_t num_blocks_w, int32_t num_blocks_h) {
    const int32_t num_coords = noise_model->n;
    AomNoiseStrengthSolver* noise_strength_solver = &noise_model->latestState[c].strengthSolver;

    const AomNoiseStrengthSolver* noise_strength_luma = &noise_model->latestState[0].strengthSolver;
    const double luma_gain = noise_model->latestState[0].arGain;
    const double noise_gain = noise_model->latestState[c].arGain;
    for (int32_t by = 0; by < num_blocks_h; ++by) {
        const int32_t y_o = by * (block_size >> sub_log2[1]);
        for (int32_t bx = 0; bx < num_blocks_w; ++bx) {
            const int32_t x_o = bx * (block_size >> sub_log2[0]);
            if (!flat_blocks[by * num_blocks_w + bx]) {
                continue;
            }
            const int32_t num_samples_h = std::min((h >> sub_log2[1]) - by * (block_size >> sub_log2[1]),
                                                   block_size >> sub_log2[1]);
            const int32_t num_samples_w = std::min((w >> sub_log2[0]) - bx * (block_size >> sub_log2[0]),
                                                   (block_size >> sub_log2[0]));
            // Make sure that we have a reasonable amount of samples to consider the
            // block
            if (num_samples_w * num_samples_h > block_size) {
                const double block_mean = getBlockMean(alt_data ? alt_data : data,
                                                       w,
                                                       h,
                                                       alt_data ? alt_stride : stride,
                                                       x_o << sub_log2[0],
                                                       y_o << sub_log2[1],
                                                       block_size);
                const double noise_var = getNoiseVar(data,
                                                     denoised,
                                                     stride,
                                                     w >> sub_log2[0],
                                                     h >> sub_log2[1],
                                                     x_o,
                                                     y_o,
                                                     block_size >> sub_log2[0],
                                                     block_size >> sub_log2[1]);
                // We want to remove the part of the noise that came from being
                // correlated with luma. Note that the noise solver for luma must
                // have already been run.
                const double luma_strength = c > 0
                    ? luma_gain * noiseStrengthSolverGetValue(noise_strength_luma, block_mean)
                    : 0;
                const double corr = c > 0 ? coeffs[num_coords] : 0;
                // Chroma noise:
                //    N(0, noise_var) = N(0, uncorr_var) + corr * N(0, luma_strength^2)
                // The uncorrelated component:
                //   uncorr_var = noise_var - (corr * luma_strength)^2
                // But don't allow fully correlated noise (hence the max), since the
                // synthesis cannot model it.
                const double uncorr_std = sqrt(std::max(noise_var / 16, noise_var - pow(corr * luma_strength, 2)));
                // After we've removed correlation with luma, undo the gain that will
                // come from running the IIR filter.
                const double adjusted_strength = uncorr_std / noise_gain;
                noiseStrengthSolverAddMeasurement(noise_strength_solver, block_mean, adjusted_strength);
            }
        }
    }
}

// 1:1 port of ar_equation_system_solve (noise_model.c:958-:996).
int32_t arEquationSystemSolve(AomNoiseState* state, int32_t is_chroma) {
    const int32_t ret = equationSystemSolve(&state->eqns);
    state->arGain = 1.0;
    if (!ret) {
        return ret;
    }

    // Update the AR gain from the equation system as it will be used to fit
    // the noise strength as a function of intensity.  In the Yule-Walker
    // equations, the diagonal should be the variance of the correlated noise.
    // In the case of the least squares estimate, there will be some variability
    // in the diagonal. So use the mean of the diagonal as the estimate of
    // overall variance (this works for least squares or Yule-Walker formulation).
    double var = 0;
    const int32_t n = state->eqns.n;
    for (int32_t i = 0; i < (state->eqns.n - is_chroma); ++i) {
        var += state->eqns.a[i * n + i] / state->numObservations;
    }
    var /= (n - is_chroma);

    // Keep track of E(Y^2) = <b, x> + E(X^2)
    // In the case that we are using chroma and have an estimate of correlation
    // with luma we adjust that estimate slightly to remove the correlated bits by
    // subtracting out the last column of a scaled by our correlation estimate
    // from b. E(y^2) = <b - A(:, end)*x(end), x>
    double sum_covar = 0;
    for (int32_t i = 0; i < state->eqns.n - is_chroma; ++i) {
        double bi = state->eqns.b[i];
        if (is_chroma) {
            bi -= state->eqns.a[i * n + (n - 1)] * state->eqns.x[n - 1];
        }
        sum_covar += (bi * state->eqns.x[i]) / state->numObservations;
    }
    // Now, get an estimate of the variance of uncorrelated noise signal and use
    // it to determine the gain of the AR filter.
    const double noise_var = std::max(var - sum_covar, 1e-6);
    state->arGain = std::max(1.0, sqrt(std::max(var / noise_var, 1e-6)));
    return ret;
}

// 1:1 port of noise_model_update (noise_model.c:1019-:1145). The SVT_ERROR
// logging is dropped; the status branches are kept verbatim.
AomNoiseStatus noiseModelUpdate(AomNoiseModel* const noise_model, const uint8_t* const data[3],
                                const uint8_t* const denoised[3], int32_t w, int32_t h,
                                const int32_t* stride, const int32_t* chroma_sub_log2,
                                const uint8_t* const flat_blocks, int32_t block_size) {
    const int32_t num_blocks_w = (w + block_size - 1) / block_size;
    const int32_t num_blocks_h = (h + block_size - 1) / block_size;
    int32_t num_blocks = 0;
    int32_t i = 0, channel = 0;

    if (block_size <= 1) {
        return AomNoiseStatus::InvalidArgument;
    }

    if (block_size < noise_model->params.lag * 2 + 1) {
        return AomNoiseStatus::InvalidArgument;
    }

    // Clear the latest equation system
    for (i = 0; i < 3; ++i) {
        equationSystemClear(&noise_model->latestState[i].eqns);
        noise_model->latestState[i].numObservations = 0;
        noiseStrengthSolverClear(&noise_model->latestState[i].strengthSolver);
    }

    // Check that we have enough flat blocks
    for (i = 0; i < num_blocks_h * num_blocks_w; ++i) {
        if (flat_blocks[i]) {
            num_blocks++;
        }
    }

    if (num_blocks <= 1) {
        return AomNoiseStatus::InsufficientFlatBlocks;
    }

    for (channel = 0; channel < 3; ++channel) {
        const int32_t no_subsampling[2] = {0, 0};
        const uint8_t* alt_data = channel > 0 ? data[0] : 0;
        const uint8_t* alt_denoised = channel > 0 ? denoised[0] : 0;
        const int32_t* sub = channel > 0 ? chroma_sub_log2 : no_subsampling;
        const int32_t is_chroma = channel != 0;
        if (!data[channel] || !denoised[channel]) {
            break;
        }
        if (!addBlockObservations(noise_model,
                                  channel,
                                  data[channel],
                                  denoised[channel],
                                  w,
                                  h,
                                  stride[channel],
                                  sub,
                                  alt_data,
                                  alt_denoised,
                                  stride[0],
                                  flat_blocks,
                                  block_size,
                                  num_blocks_w,
                                  num_blocks_h)) {
            return AomNoiseStatus::InternalError;
        }

        if (!arEquationSystemSolve(&noise_model->latestState[channel], is_chroma)) {
            if (is_chroma) {
                setChromaCoefficientFallbackSoln(&noise_model->latestState[channel].eqns);
            } else {
                return AomNoiseStatus::InsufficientNoisePixels;
            }
        }

        addNoiseStdObservations(noise_model,
                                channel,
                                noise_model->latestState[channel].eqns.x.data(),
                                data[channel],
                                denoised[channel],
                                w,
                                h,
                                stride[channel],
                                sub,
                                alt_data,
                                stride[0],
                                flat_blocks,
                                block_size,
                                num_blocks_w,
                                num_blocks_h);

        if (!noiseStrengthSolverSolve(&noise_model->latestState[channel].strengthSolver)) {
            return AomNoiseStatus::InternalError;
        }

        // Check noise characteristics and return if error.
        //    if (channel == 0 &&
        //        noise_model->combined_state[channel].strength_solver.num_equations >
        //            0 &&
        //        is_noise_model_different(noise_model)) {
        //      y_model_different = 1;
        //    }

        noise_model->combinedState[channel].numObservations = noise_model->latestState[channel].numObservations;
        equationSystemCopy(&noise_model->combinedState[channel].eqns, &noise_model->latestState[channel].eqns);
        if (!arEquationSystemSolve(&noise_model->combinedState[channel], is_chroma)) {
            if (is_chroma) {
                setChromaCoefficientFallbackSoln(&noise_model->combinedState[channel].eqns);
            } else {
                return AomNoiseStatus::InternalError;
            }
        }

        noiseStrengthSolverCopy(&noise_model->combinedState[channel].strengthSolver,
                                &noise_model->latestState[channel].strengthSolver);

        if (!noiseStrengthSolverSolve(&noise_model->combinedState[channel].strengthSolver)) {
            return AomNoiseStatus::InternalError;
        }
    }

    return AomNoiseStatus::Ok;
}

// 1:1 port of svt_aom_noise_model_save_latest (noise_model.c:1147-:1157).
void noiseModelSaveLatest(AomNoiseModel* noise_model) {
    for (int32_t c = 0; c < 3; c++) {
        equationSystemCopy(&noise_model->combinedState[c].eqns, &noise_model->latestState[c].eqns);
        equationSystemCopy(&noise_model->combinedState[c].strengthSolver.eqns,
                           &noise_model->latestState[c].strengthSolver.eqns);
        noise_model->combinedState[c].strengthSolver.numEquations =
            noise_model->latestState[c].strengthSolver.numEquations;
        noise_model->combinedState[c].numObservations = noise_model->latestState[c].numObservations;
        noise_model->combinedState[c].arGain = noise_model->latestState[c].arGain;
    }
}

}  // namespace analysis
