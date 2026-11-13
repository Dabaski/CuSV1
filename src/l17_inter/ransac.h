#pragma once

#include <cstdint>

#include "warped_motion.h"

// l17_inter - the RANSAC surface (A3W). Ported 1:1 from
// third_party/SVT-AV1/Source/Lib/Codec/ransac.c (whole file) plus its
// header-inline dependencies random.h (the lcg family, rd:23-:76) and
// mathutils.h (linsolve + the least-squares trio, mu:22-:108), which
// ransac.c cannot compile without. Every ported function cites its
// reference span. namespace l17_inter per the slice brief.
namespace l17_inter {

// ransac.c:22-:33 - the RANSAC constants (verbatim).
constexpr int kMaxMinpts = 4;                      // MAX_MINPTS
constexpr int kMaxDegenerateIter = 10;             // MAX_DEGENERATE_ITER
constexpr int kMinptsMultiplier = 5;               // MINPTS_MULTIPLIER
constexpr double kInlierThresholdSquared = 1.5625; // (1.25 * 1.25), rs:26
constexpr int kNumTrials = 20;                     // rs:29
constexpr int kNumRefines = 5;                     // rs:32
constexpr int kMinTrials = 20;                     // rs:33

// ransac.h:22 - MIN_INLIER_PROB.
constexpr double kMinInlierProb = 0.1;

// global_motion.h:23-:24.
constexpr int kMaxCorners = 4096;
constexpr int kRansacNumMotions = 1;

// Correspondence, global_motion.h:26-:29 (the sample pair: source (x, y)
// and reference (rx, ry), integer pixel positions).
struct Correspondence {
    int x;
    int y;
    int rx;
    int ry;
};

// MotionModel, global_motion.h:38-:42.
struct MotionModel {
    double params[6]; // MAX_PARAMDIM, definitions.h:1748
    int* inliers;
    int num_inliers;
};

// kIdentityParams, ransac.h:24.
constexpr double kIdentityParams[6] = {0.0, 0.0, 1.0, 0.0, 0.0, 1.0};

// RANSAC_MOTION, ransac.h:26-:30.
struct RansacMotion {
    int num_inliers;
    double sse;
    int* inlier_indices;
};

// ---------- random.h (the lcg family) ----------

// lcg_next, rd:23-:26.
std::uint32_t lcgNext(std::uint32_t* state);

// lcg_rand16, rd:29-:31.
std::uint32_t lcgRand16(std::uint32_t* state);

// lcg_randint, rd:38-:41.
std::uint32_t lcgRandint(std::uint32_t* state, std::uint32_t n);

// lcg_randrange, rd:44-:47.
std::uint32_t lcgRandrange(std::uint32_t* state, std::uint32_t lo, std::uint32_t hi);

// lcg_pick, rd:56-:76.
void lcgPick(int n, int k, int* out, std::uint32_t* seed);

// ---------- mathutils.h ----------

// linsolve, mu:22-:65. Solves Ax = b; returns 0 on a near-singular system.
std::int32_t linsolve(std::int32_t n, double* a, std::int32_t stride, double* b, double* x);

// least_squares_init / _accumulate / _solve, mu:90-:108.
void leastSquaresInit(double* mat, double* y, int n);
void leastSquaresAccumulate(double* mat, double* y, const double* a, double b, int n);
std::int32_t leastSquaresSolve(double* mat, double* y, double* x, int n);

// ---------- ransac.c ----------

// The function-pointer typedefs, rs:32-:34 (ransac.h).
using FindTransformationFunc = bool (*)(const Correspondence* points, const int* indices, int numIndices,
                                        double* params);
using ScoreModelFunc = void (*)(const double* mat, const Correspondence* points, int numPoints,
                                RansacMotion* model);

// RansacModelInfo, rs:38-:60 (ransac.h).
struct RansacModelInfo {
    FindTransformationFunc findTransformation;
    ScoreModelFunc scoreModel;
    int minpts;
};

// compare_motions, rs:39-:56 (file-local static; exported for testability).
int compareMotions(const void* argA, const void* argB);

// is_better_motion, rs:58-:60 (file-local static; exported).
bool isBetterMotion(const RansacMotion* motionA, const RansacMotion* motionB);

// score_translation, rs:62-:84 (file-local static; exported).
void scoreTranslation(const double* mat, const Correspondence* points, int numPoints, RansacMotion* model);

// score_affine, rs:86-:108 (file-local static; exported).
void scoreAffine(const double* mat, const Correspondence* points, int numPoints, RansacMotion* model);

// find_translation, rs:110-:132 (file-local static; exported).
bool findTranslation(const Correspondence* points, const int* indices, int numIndices, double* params);

// find_rotzoom, rs:134-:173 (file-local static; exported).
bool findRotzoom(const Correspondence* points, const int* indices, int numIndices, double* params);

// find_affine, rs:175-:232 (file-local static; exported).
bool findAffine(const Correspondence* points, const int* indices, int numIndices, double* params);

// svt_aom_ransac, rs:428-:434. Returns true on success, false on error
// (too few points or a failed allocation; memAllocFailed distinguishes).
bool svtAomRansac(const Correspondence* matchedPoints, int npoints, TransformationType type,
                  MotionModel* motionModels, int numDesiredMotions, bool* memAllocFailed);

} // namespace l17_inter
