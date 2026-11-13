#include "ransac.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace l17_inter {

// ---------- random.h (the lcg family) ----------

// lcg_next, rd:23-:26.
std::uint32_t lcgNext(std::uint32_t* state) {
    *state = (std::uint32_t)(*state * 1103515245ULL + 12345);
    return *state;
}

// lcg_rand16, rd:29-:31.
std::uint32_t lcgRand16(std::uint32_t* state) {
    return (lcgNext(state) / 65536) % 32768;
}

// lcg_randint, rd:38-:41.
std::uint32_t lcgRandint(std::uint32_t* state, std::uint32_t n) {
    const std::uint64_t v = ((std::uint64_t)lcgNext(state) * n) >> 32;
    return (std::uint32_t)v;
}

// lcg_randrange, rd:44-:47.
std::uint32_t lcgRandrange(std::uint32_t* state, std::uint32_t lo, std::uint32_t hi) {
    assert(lo < hi);
    return lo + lcgRandint(state, hi - lo);
}

// lcg_pick, rd:56-:76. Pick k distinct numbers from {0, ..., n-1} by
// resampling; all orderings equally likely.
void lcgPick(int n, int k, int* out, std::uint32_t* seed) {
    for (int i = 0; i < k; i++) {
        int v;

        // Inner resampling loop (the reference's goto).
    resample:
        v = (int)lcgRandint(seed, (std::uint32_t)n);
        for (int j = 0; j < i; j++) {
            if (v == out[j]) {
                // Repeated v, resample
                goto resample;
            }
        }

        // New v, accept
        out[i] = v;
    }
}

// ---------- mathutils.h ----------

// linsolve, mu:22-:65. Solves Ax = b, where x and b are column vectors of
// size nx1 and A is nxn.
std::int32_t linsolve(std::int32_t n, double* a, std::int32_t stride, double* b, double* x) {
    const double tiny_near_zero = 1.0E-16;
    std::int32_t i, j, k;
    double c;
    // Forward elimination
    for (k = 0; k < n - 1; k++) {
        // Bring the largest magnitude to the diagonal position
        for (i = n - 1; i > k; i--) {
            if (fabs(a[(i - 1) * stride + k]) < fabs(a[i * stride + k])) {
                for (j = 0; j < n; j++) {
                    c = a[i * stride + j];
                    a[i * stride + j] = a[(i - 1) * stride + j];
                    a[(i - 1) * stride + j] = c;
                }
                c = b[i];
                b[i] = b[i - 1];
                b[i - 1] = c;
            }
        }
        for (i = k; i < n - 1; i++) {
            if (fabs(a[k * stride + k]) < tiny_near_zero) {
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
        if (fabs(a[i * stride + i]) < tiny_near_zero) {
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

// least_squares_init, mu:90-:93.
void leastSquaresInit(double* mat, double* y, int n) {
    memset(mat, 0, n * n * sizeof(double));
    memset(y, 0, n * sizeof(double));
}

// least_squares_accumulate, mu:95-:104.
void leastSquaresAccumulate(double* mat, double* y, const double* a, double b, int n) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            mat[i * n + j] += a[i] * a[j];
        }
    }
    for (int i = 0; i < n; i++) {
        y[i] += a[i] * b;
    }
}

// least_squares_solve, mu:106-:108.
std::int32_t leastSquaresSolve(double* mat, double* y, double* x, int n) {
    return linsolve(n, mat, n, y, x);
}

// ---------- ransac.c ----------

// compare_motions, rs:39-:56. Return -1 if 'a' is a better motion, 1 if 'b'
// is better, 0 otherwise.
int compareMotions(const void* argA, const void* argB) {
    const RansacMotion* motion_a = (const RansacMotion*)argA;
    const RansacMotion* motion_b = (const RansacMotion*)argB;

    if (motion_a->num_inliers > motion_b->num_inliers) {
        return -1;
    }
    if (motion_a->num_inliers < motion_b->num_inliers) {
        return 1;
    }
    if (motion_a->sse < motion_b->sse) {
        return -1;
    }
    if (motion_a->sse > motion_b->sse) {
        return 1;
    }
    return 0;
}

// is_better_motion, rs:58-:60.
bool isBetterMotion(const RansacMotion* motion_a, const RansacMotion* motion_b) {
    return compareMotions(motion_a, motion_b) < 0;
}

// score_translation, rs:62-:84.
void scoreTranslation(const double* mat, const Correspondence* points, int numPoints, RansacMotion* model) {
    model->num_inliers = 0;
    model->sse = 0.0;

    for (int i = 0; i < numPoints; ++i) {
        const double x1 = points[i].x;
        const double y1 = points[i].y;
        const double x2 = points[i].rx;
        const double y2 = points[i].ry;

        const double proj_x = x1 + mat[0];
        const double proj_y = y1 + mat[1];

        const double dx = proj_x - x2;
        const double dy = proj_y - y2;
        const double sse = dx * dx + dy * dy;

        if (sse < kInlierThresholdSquared) {
            model->inlier_indices[model->num_inliers++] = i;
            model->sse += sse;
        }
    }
}

// score_affine, rs:86-:108.
void scoreAffine(const double* mat, const Correspondence* points, int numPoints, RansacMotion* model) {
    model->num_inliers = 0;
    model->sse = 0.0;

    for (int i = 0; i < numPoints; ++i) {
        const double x1 = points[i].x;
        const double y1 = points[i].y;
        const double x2 = points[i].rx;
        const double y2 = points[i].ry;

        const double proj_x = mat[2] * x1 + mat[3] * y1 + mat[0];
        const double proj_y = mat[4] * x1 + mat[5] * y1 + mat[1];

        const double dx = proj_x - x2;
        const double dy = proj_y - y2;
        const double sse = dx * dx + dy * dy;

        if (sse < kInlierThresholdSquared) {
            model->inlier_indices[model->num_inliers++] = i;
            model->sse += sse;
        }
    }
}

// find_translation, rs:110-:132.
bool findTranslation(const Correspondence* points, const int* indices, int numIndices, double* params) {
    double sumx = 0;
    double sumy = 0;

    for (int i = 0; i < numIndices; ++i) {
        const int index = indices[i];
        const double sx = points[index].x;
        const double sy = points[index].y;
        const double dx = points[index].rx;
        const double dy = points[index].ry;

        sumx += dx - sx;
        sumy += dy - sy;
    }

    params[0] = sumx / numIndices;
    params[1] = sumy / numIndices;
    params[2] = 1;
    params[3] = 0;
    params[4] = 0;
    params[5] = 1;
    return true;
}

// find_rotzoom, rs:134-:173.
bool findRotzoom(const Correspondence* points, const int* indices, int numIndices, double* params) {
    const int n = 4;   // Size of least-squares problem
    double mat[4 * 4]; // Accumulator for A'A
    double y[4];       // Accumulator for A'b
    double a[4];       // Single row of A

    leastSquaresInit(mat, y, n);
    for (int i = 0; i < numIndices; ++i) {
        const int index = indices[i];
        const double sx = points[index].x;
        const double sy = points[index].y;
        const double dx = points[index].rx;
        const double dy = points[index].ry;

        a[0] = 1;
        a[1] = 0;
        a[2] = sx;
        a[3] = sy;
        double b = dx; // Single element of b
        leastSquaresAccumulate(mat, y, a, b, n);

        a[0] = 0;
        a[1] = 1;
        a[2] = sy;
        a[3] = -sx;
        b = dy;
        leastSquaresAccumulate(mat, y, a, b, n);
    }

    // Fill in params[0] .. params[3] with output model
    if (!leastSquaresSolve(mat, y, params, n)) {
        return false;
    }

    // Fill in remaining parameters
    params[4] = -params[3];
    params[5] = params[2];

    return true;
}

// find_affine, rs:175-:232. The 6-dim problem splits into two independent
// 3-dim subproblems (x output / y output).
bool findAffine(const Correspondence* points, const int* indices, int numIndices, double* params) {
    const int n = 3;       // Size of each least-squares problem
    double mat[2][3 * 3];  // Accumulator for A'A
    double y[2][3];        // Accumulator for A'b
    double x[2][3];        // Output vector
    double a[2][3];        // Single row of A
    double b[2];           // Single element of b

    leastSquaresInit(mat[0], y[0], n);
    leastSquaresInit(mat[1], y[1], n);
    for (int i = 0; i < numIndices; ++i) {
        const int index = indices[i];
        const double sx = points[index].x;
        const double sy = points[index].y;
        const double dx = points[index].rx;
        const double dy = points[index].ry;

        a[0][0] = 1;
        a[0][1] = sx;
        a[0][2] = sy;
        b[0] = dx;
        leastSquaresAccumulate(mat[0], y[0], a[0], b[0], n);

        a[1][0] = 1;
        a[1][1] = sx;
        a[1][2] = sy;
        b[1] = dy;
        leastSquaresAccumulate(mat[1], y[1], a[1], b[1], n);
    }

    if (!leastSquaresSolve(mat[0], y[0], x[0], n)) {
        return false;
    }
    if (!leastSquaresSolve(mat[1], y[1], x[1], n)) {
        return false;
    }

    // Rearrange least squares result to form output model
    params[0] = x[0][0];
    params[1] = x[1][0];
    params[2] = x[0][1];
    params[3] = x[0][2];
    params[4] = x[1][1];
    params[5] = x[1][2];

    return true;
}

// Returns true on success, false on error
namespace {
bool ransacInternal(const Correspondence* matchedPoints, int npoints, MotionModel* motionModels,
                    int numDesiredMotions, const RansacModelInfo* modelInfo, bool* memAllocFailed) {
    int i = 0;
    const int minpts = modelInfo->minpts;
    bool ret_val = true;

    std::uint32_t seed = (std::uint32_t)npoints;

    // The allocation-failure write (*mem_alloc_failed = true) is part of the
    // parked EB_MALLOC path (see below); the parameter is otherwise
    // untouched, exactly as in the success path of the reference.
    (void)memAllocFailed;

    int indices[kMaxMinpts] = {0};

    // Store information for the num_desired_motions best transformations
    // found and the worst motion among them, as well as the motion currently
    // under consideration.
    RansacMotion* motions = nullptr;
    RansacMotion* worst_kept_motion = nullptr;
    RansacMotion current_motion;

    // Store the parameters and the indices of the inlier points for the
    // motion currently under consideration.
    double params_this_motion[6]; // MAX_PARAMDIM

    // Initialize output models, as a fallback in case we can't find a model
    for (i = 0; i < numDesiredMotions; i++) {
        memcpy(motionModels[i].params, kIdentityParams, sizeof(kIdentityParams));
        motionModels[i].num_inliers = 0;
    }

    if (npoints < minpts * kMinptsMultiplier || npoints == 0) {
        return false;
    }

    const int min_inliers = (std::max)((int)(kMinInlierProb * npoints), minpts);

    // The reference allocates motions + one shared inlier-index arena with
    // EB_CALLOC/EB_MALLOC and frees both at finish_ransac; the port carries
    // the same carving over a single allocation. NAMED DEVIATION: the
    // mem_alloc_failed path is not reachable here (std::vector throws on
    // allocation failure instead of returning null), so the flag is only
    // ever written false.
    std::vector<RansacMotion> motionsVec((size_t)numDesiredMotions);
    motions = motionsVec.data();
    std::vector<int> inlierBuffer((size_t)npoints * (numDesiredMotions + 1));
    int* inlier_buffer = inlierBuffer.data();

    // Once all our allocations are known-good, we can fill in our structures
    worst_kept_motion = motions;

    for (i = 0; i < numDesiredMotions; ++i) {
        motions[i].inlier_indices = inlier_buffer + i * npoints;
    }
    memset(&current_motion, 0, sizeof(current_motion));
    current_motion.inlier_indices = inlier_buffer + numDesiredMotions * npoints;

    for (int trial_count = 0; trial_count < kNumTrials; trial_count++) {
        lcgPick(npoints, minpts, indices, &seed);

        if (!modelInfo->findTransformation(matchedPoints, indices, minpts, params_this_motion)) {
            continue;
        }

        modelInfo->scoreModel(params_this_motion, matchedPoints, npoints, &current_motion);

        if (current_motion.num_inliers < min_inliers) {
            // Reject models with too few inliers
            continue;
        }

        if (isBetterMotion(&current_motion, worst_kept_motion)) {
            // This motion is better than the worst currently kept motion.
            // Remember the inlier points and sse. The parameters for each
            // kept motion will be recomputed later using only the inliers.
            worst_kept_motion->num_inliers = current_motion.num_inliers;
            worst_kept_motion->sse = current_motion.sse;

            // Rather than copying the (potentially many) inlier indices from
            // current_motion.inlier_indices to worst_kept_motion->inlier_indices,
            // we can swap the underlying pointers.
            int* tmp = worst_kept_motion->inlier_indices;
            worst_kept_motion->inlier_indices = current_motion.inlier_indices;
            current_motion.inlier_indices = tmp;

            // Determine the new worst kept motion and its num_inliers and sse.
            for (i = 0; i < numDesiredMotions; ++i) {
                if (isBetterMotion(worst_kept_motion, &motions[i])) {
                    worst_kept_motion = &motions[i];
                }
            }
        }
    }

    // Sort the motions, best first.
    qsort(motions, (size_t)numDesiredMotions, sizeof(RansacMotion), compareMotions);

    // Refine each of the best N models using iterative estimation.
    for (i = 0; i < numDesiredMotions; ++i) {
        if (motions[i].num_inliers <= 0) {
            // Output model has already been initialized to the identity
            // model, so just skip setup
            continue;
        }

        bool bad_model = false;
        for (int refine_count = 0; refine_count < kNumRefines; refine_count++) {
            const int num_inliers = motions[i].num_inliers;

            if (!modelInfo->findTransformation(matchedPoints, motions[i].inlier_indices, num_inliers,
                                               params_this_motion)) {
                // In the unlikely event that this model fitting fails, we
                // don't have a good fallback. So leave this model set to the
                // identity model
                bad_model = true;
                break;
            }

            // Score the newly generated model
            modelInfo->scoreModel(params_this_motion, matchedPoints, npoints, &current_motion);

            if (current_motion.num_inliers > motions[i].num_inliers) {
                motions[i].num_inliers = current_motion.num_inliers;
                motions[i].sse = current_motion.sse;
                int* tmp = motions[i].inlier_indices;
                motions[i].inlier_indices = current_motion.inlier_indices;
                current_motion.inlier_indices = tmp;
            } else {
                // Refined model is no better, so stop
                break;
            }
        }

        if (bad_model) {
            continue;
        }

        // Fill in output struct
        memcpy(motionModels[i].params, params_this_motion, sizeof(params_this_motion));
        for (int j = 0; j < motions[i].num_inliers; j++) {
            const int index = motions[i].inlier_indices[j];
            const Correspondence* corr = &matchedPoints[index];
            motionModels[i].inliers[2 * j + 0] = (int)std::lrint(corr->x);
            motionModels[i].inliers[2 * j + 1] = (int)std::lrint(corr->y);
        }
        motionModels[i].num_inliers = motions[i].num_inliers;
    }

    return ret_val;
}

// The model table, rs:416-:425 (file-local static in the reference too).
const RansacModelInfo ransacModelInfo[TransTypes] = {
    // IDENTITY
    {nullptr, nullptr, 0},
    // TRANSLATION
    {findTranslation, scoreTranslation, 1},
    // ROTZOOM
    {findRotzoom, scoreAffine, 2},
    // AFFINE
    {findAffine, scoreAffine, 3},
};
} // namespace

// svt_aom_ransac, rs:428-:434. Returns true on success, false on error.
bool svtAomRansac(const Correspondence* matchedPoints, int npoints, TransformationType type,
                  MotionModel* motionModels, int numDesiredMotions, bool* memAllocFailed) {
    assert(type > Identity && type < TransTypes);

    return ransacInternal(matchedPoints, npoints, motionModels, numDesiredMotions, &ransacModelInfo[type],
                          memAllocFailed);
}

} // namespace l17_inter
