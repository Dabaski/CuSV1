// A3M: the pure motion-search helpers (av1me.c / motion_estimation.c /
// mcomp.c). Bodies land slice by slice under the nano-cycle.
#include "motion_search.h"

#include "motion.h"

namespace motion {

// rd_cost.c:47-54 svt_av1_get_mv_joint, verbatim.
int getMvJoint(const Mv* mv) {
    if (mv->y == 0) {
        return mv->x == 0 ? 0 /*MV_JOINT_ZERO*/ : 1 /*MV_JOINT_HNZVZ*/;
    } else {
        return mv->x == 0 ? 2 /*MV_JOINT_HZVNZ*/ : 3 /*MV_JOINT_HNZVNZ*/;
    }
}

// utility.h:101 CLIP3, the mcomp.h walk's clip.
static int clip3(int minVal, int maxVal, int a) {
    return a < minVal ? minVal : (a > maxVal ? maxVal : a);
}

// The reference's (Mv){{x, y}} compound literal; the port's helper.
static Mv mvOf(std::int16_t x, std::int16_t y) {
    Mv m;
    m.x = x;
    m.y = y;
    return m;
}

// mcomp.h:138-146 svt_mv_cost, verbatim (MV_LOW/MV_UPP = kMvLow/kMvUpp).
int mvCost(const Mv* mv, const int* jointCost, const int* const compCost[2]) {
    // The y-component (row component) of the MV is coded first, so the cost
    // is in the 0th idx.
    return jointCost[getMvJoint(mv)] + compCost[0][clip3(kMvLow, kMvUpp, mv->y)] +
        compCost[1][clip3(kMvLow, kMvUpp, mv->x)];
}

// utility.h ABS shape (the aom macro; local helper).
static int absInt(int v) {
    return v < 0 ? -v : v;
}

// av1me.c:93-95 get_buf_from_mv (the fullpel Buf2D form; the av1me.c-local
// twin of subpelBufFromMv - different arithmetic, no >>3).
static const std::uint8_t* bufFromMv(const std::uint8_t* buf, int stride, const Mv* mv) {
    return &buf[mv->y * stride + mv->x];
}

// av1me.c:195-208 svt_av1_get_mvpred_var, verbatim (the walls threaded).
int getMvpredVar(const SearchCtx& x, const Mv* bestMv, const Mv* centerMv, VarianceFn vf, int useMvcost) {
    Mv mv;
    mv.x = (std::int16_t)(bestMv->x * 8);
    mv.y = (std::int16_t)(bestMv->y * 8);
    std::uint32_t unused;
    if (x.approxInterRate) {
        return (int)vf(x.srcBuf, x.srcStride, bufFromMv(x.refBuf, x.refStride, bestMv), x.refStride, &unused) +
            (useMvcost ? mvErrCostLight(&mv, centerMv) : 0);
    } else {
        return (int)vf(x.srcBuf, x.srcStride, bufFromMv(x.refBuf, x.refStride, bestMv), x.refStride, &unused) +
            (useMvcost ? mvErrCost(&mv, centerMv, x.nmvVecCost, x.mvCostStack, x.errorperbit) : 0);
    }
}

// av1me.c:424-487 svt_av1_refining_search_sad, verbatim (the walls threaded).
std::uint32_t refiningSearchSad(SearchCtx& x, Mv* refMv, int errorPerBit, int searchRange, SadFn sdf,
                                SadMultiDFn sdx4df, const Mv* centerMv) {
    const Mv neighbors[4] = {mvOf(0, -1), mvOf(-1, 0), mvOf(1, 0), mvOf(0, 1)};
    const Mv fcenterMv = mvOf((std::int16_t)(centerMv->x >> 3), (std::int16_t)(centerMv->y >> 3));
    const std::uint8_t* bestAddress = bufFromMv(x.refBuf, x.refStride, refMv);
    std::uint32_t bestSad = sdf(x.srcBuf, x.srcStride, bestAddress, x.refStride) +
        (std::uint32_t)mvsadErrCost(x, refMv, &fcenterMv, errorPerBit);
    for (int i = 0; i < searchRange; i++) {
        int bestSite = -1;
        const int allIn = (refMv->y - 1) > x.mvLimits.row_min && (refMv->y + 1) < x.mvLimits.row_max &&
            (refMv->x - 1) > x.mvLimits.col_min && (refMv->x + 1) < x.mvLimits.col_max;

        if (allIn) {
            std::uint32_t sads[4];
            const std::uint8_t* positions[4] = {bestAddress - x.refStride, bestAddress - 1, bestAddress + 1,
                                                bestAddress + x.refStride};

            sdx4df(x.srcBuf, x.srcStride, positions, x.refStride, sads);

            for (int j = 0; j < 4; ++j) {
                if (sads[j] < bestSad) {
                    const Mv mv =
                        mvOf((std::int16_t)(refMv->x + neighbors[j].x), (std::int16_t)(refMv->y + neighbors[j].y));
                    sads[j] += (std::uint32_t)mvsadErrCost(x, &mv, &fcenterMv, errorPerBit);
                    if (sads[j] < bestSad) {
                        bestSad = sads[j];
                        bestSite = j;
                    }
                }
            }
        } else {
            for (int j = 0; j < 4; ++j) {
                const Mv mv =
                    mvOf((std::int16_t)(refMv->x + neighbors[j].x), (std::int16_t)(refMv->y + neighbors[j].y));

                if (isMvIn(&x.mvLimits, &mv)) {
                    std::uint32_t sad =
                        sdf(x.srcBuf, x.srcStride, bufFromMv(x.refBuf, x.refStride, &mv), x.refStride);
                    if (sad < bestSad) {
                        sad += (std::uint32_t)mvsadErrCost(x, &mv, &fcenterMv, errorPerBit);
                        if (sad < bestSad) {
                            bestSad = sad;
                            bestSite = j;
                        }
                    }
                }
            }
        }

        if (bestSite == -1) {
            break;
        } else {
            *x.secondBestMv = *refMv;
            refMv->y = (std::int16_t)(refMv->y + neighbors[bestSite].y);
            refMv->x = (std::int16_t)(refMv->x + neighbors[bestSite].x);
            bestAddress = bufFromMv(x.refBuf, x.refStride, refMv);
        }
    }

    return bestSad;
}

// av1me.c:212-289 exhaustive_mesh_search, verbatim (the x and fn_ptr walls
// threaded; clamp_mv is the landed amvp twin).
std::uint32_t exhaustiveMeshSearch(SearchCtx& x, Mv* refMv, Mv* bestMv, int range, int step, int sadPerBit,
                                   SadFn sdf, SadMultiDFn sdx4df, const Mv* centerMv) {
    Mv fcenterMv = *centerMv;
    std::uint32_t bestSad;
    int r, c, i;
    int startCol, endCol, startRow, endRow;
    const int colStep = (step > 1) ? step : 4;

    clampMv(&fcenterMv, x.mvLimits.col_min, x.mvLimits.col_max, x.mvLimits.row_min, x.mvLimits.row_max);
    *bestMv = fcenterMv;
    bestSad = sdf(x.srcBuf, x.srcStride, bufFromMv(x.refBuf, x.refStride, &fcenterMv), x.refStride) +
        (std::uint32_t)mvsadErrCost(x, &fcenterMv, refMv, sadPerBit);
    startRow = -range > (x.mvLimits.row_min - fcenterMv.y) ? -range : (x.mvLimits.row_min - fcenterMv.y);
    startCol = -range > (x.mvLimits.col_min - fcenterMv.x) ? -range : (x.mvLimits.col_min - fcenterMv.x);
    endRow = range < (x.mvLimits.row_max - fcenterMv.y) ? range : (x.mvLimits.row_max - fcenterMv.y);
    endCol = range < (x.mvLimits.col_max - fcenterMv.x) ? range : (x.mvLimits.col_max - fcenterMv.x);

    for (r = startRow; r <= endRow; r += step) {
        for (c = startCol; c <= endCol; c += colStep) {
            // Step > 1 means we are not checking every location in this pass.
            if (step > 1) {
                const Mv mv = mvOf((std::int16_t)(fcenterMv.x + c), (std::int16_t)(fcenterMv.y + r));
                std::uint32_t sad = sdf(x.srcBuf, x.srcStride, bufFromMv(x.refBuf, x.refStride, &mv), x.refStride);
                if (sad < bestSad) {
                    sad += (std::uint32_t)mvsadErrCost(x, &mv, refMv, sadPerBit);
                    if (sad < bestSad) {
                        bestSad = sad;
                        *x.secondBestMv = *bestMv;
                        *bestMv = mv;
                    }
                }
            } else {
                // 4 sads in a single call if we are checking every location
                if (c + 3 <= endCol) {
                    std::uint32_t sads[4];
                    const std::uint8_t* addrs[4];
                    for (i = 0; i < 4; ++i) {
                        const Mv mv = mvOf((std::int16_t)(fcenterMv.x + c + i), (std::int16_t)(fcenterMv.y + r));
                        addrs[i] = bufFromMv(x.refBuf, x.refStride, &mv);
                    }
                    sdx4df(x.srcBuf, x.srcStride, addrs, x.refStride, sads);

                    for (i = 0; i < 4; ++i) {
                        if (sads[i] < bestSad) {
                            const Mv mv = mvOf((std::int16_t)(fcenterMv.x + c + i), (std::int16_t)(fcenterMv.y + r));
                            const std::uint32_t sad =
                                sads[i] + (std::uint32_t)mvsadErrCost(x, &mv, refMv, sadPerBit);
                            if (sad < bestSad) {
                                bestSad = sad;
                                *x.secondBestMv = *bestMv;
                                *bestMv = mv;
                            }
                        }
                    }
                } else {
                    for (i = 0; i < endCol - c; ++i) {
                        const Mv mv = mvOf((std::int16_t)(fcenterMv.x + c + i), (std::int16_t)(fcenterMv.y + r));
                        std::uint32_t sad =
                            sdf(x.srcBuf, x.srcStride, bufFromMv(x.refBuf, x.refStride, &mv), x.refStride);
                        if (sad < bestSad) {
                            sad += (std::uint32_t)mvsadErrCost(x, &mv, refMv, sadPerBit);
                            if (sad < bestSad) {
                                bestSad = sad;
                                *x.secondBestMv = *bestMv;
                                *bestMv = mv;
                            }
                        }
                    }
                }
            }
        }
    }

    return bestSad;
}

// av1me.c:291-422 svt_av1_diamond_search_sad_c, verbatim (the x / cfg /
// fn_ptr walls threaded; NEW_DIAMOND_SEARCH is not defined in the pinned
// tree, so the reference's compiled form has no inner refinement loop).
std::uint32_t diamondSearchSad(SearchCtx& x, const SearchSiteConfig& cfg, Mv* refMv, Mv* bestMv, int searchParam,
                               int sadPerBit, int* num00, SadFn sdf, SadMultiDFn sdx4df, const Mv* centerMv) {
    int i, j, step;

    const int whatStride = x.srcStride;
    const int inWhatStride = x.refStride;
    const std::uint8_t* bestAddress;

    std::uint32_t bestsad;
    int bestSite = 0;
    int lastSite = 0;

    int refRow;
    int refCol;

    // search_param determines the length of the initial step and hence the
    // number of iterations (av1me.c:309-313).
    const SearchSite* ss = &cfg.ss[searchParam * cfg.searchesPerStep];
    const int totSteps = (cfg.ssCount / cfg.searchesPerStep) - searchParam;

    const Mv fcenterMv = mvOf((std::int16_t)(centerMv->x >> 3), (std::int16_t)(centerMv->y >> 3));
    amvp::clampMv(refMv, x.mvLimits.col_min, x.mvLimits.col_max, x.mvLimits.row_min, x.mvLimits.row_max);
    refRow = refMv->y;
    refCol = refMv->x;
    *num00 = 0;
    bestMv->y = (std::int16_t)refRow;
    bestMv->x = (std::int16_t)refCol;

    // Work out the start point for the search.
    const std::uint8_t* inWhat = x.refBuf + refRow * inWhatStride + refCol;
    bestAddress = inWhat;

    // Check the starting position.
    bestsad = sdf(x.srcBuf, whatStride, inWhat, inWhatStride) +
        (std::uint32_t)mvsadErrCost(x, bestMv, &fcenterMv, sadPerBit);

    i = 1;

    for (step = 0; step < totSteps; step++) {
        int allIn = 1;

        // All_in is true if every one of the points we are checking are
        // within the bounds of the image.
        allIn &= ((bestMv->y + ss[i].mv.y) > x.mvLimits.row_min);
        allIn &= ((bestMv->y + ss[i + 1].mv.y) < x.mvLimits.row_max);
        allIn &= ((bestMv->x + ss[i + 2].mv.x) > x.mvLimits.col_min);
        allIn &= ((bestMv->x + ss[i + 3].mv.x) < x.mvLimits.col_max);

        // If all the pixels are within the bounds we don't check whether the
        // search point is valid in this loop, otherwise we check each point
        // for validity.
        if (allIn) {
            std::uint32_t sadArray[4];

            for (j = 0; j < cfg.searchesPerStep; j += 4) {
                const std::uint8_t* blockOffset[4];

                for (int t = 0; t < 4; t++) {
                    blockOffset[t] = ss[i + t].offset + bestAddress;
                }

                sdx4df(x.srcBuf, whatStride, blockOffset, inWhatStride, sadArray);

                for (int t = 0; t < 4; t++, i++) {
                    if (sadArray[t] < bestsad) {
                        const Mv thisMv = mvOf((std::int16_t)(bestMv->x + ss[i].mv.x),
                                               (std::int16_t)(bestMv->y + ss[i].mv.y));
                        sadArray[t] += (std::uint32_t)mvsadErrCost(x, &thisMv, &fcenterMv, sadPerBit);
                        if (sadArray[t] < bestsad) {
                            bestsad = sadArray[t];
                            bestSite = i;
                        }
                    }
                }
            }
        } else {
            for (j = 0; j < cfg.searchesPerStep; j++) {
                // Trap illegal vectors.
                const Mv thisMv =
                    mvOf((std::int16_t)(bestMv->x + ss[i].mv.x), (std::int16_t)(bestMv->y + ss[i].mv.y));

                if (isMvIn(&x.mvLimits, &thisMv)) {
                    const std::uint8_t* const checkHere = ss[i].offset + bestAddress;
                    std::uint32_t thissad = sdf(x.srcBuf, whatStride, checkHere, inWhatStride);

                    if (thissad < bestsad) {
                        thissad += (std::uint32_t)mvsadErrCost(x, &thisMv, &fcenterMv, sadPerBit);
                        if (thissad < bestsad) {
                            bestsad = thissad;
                            bestSite = i;
                        }
                    }
                }
                i++;
            }
        }
        if (bestSite != lastSite) {
            *x.secondBestMv = *bestMv;
            bestMv->y = (std::int16_t)(bestMv->y + ss[bestSite].mv.y);
            bestMv->x = (std::int16_t)(bestMv->x + ss[bestSite].mv.x);
            bestAddress += ss[bestSite].offset;
            lastSite = bestSite;
        } else if (bestAddress == inWhat) {
            (*num00)++;
        }
    }
    return bestsad;
}

// definitions.h:464 ROUND_POWER_OF_TWO_64.
static std::int64_t roundPow2_64(std::int64_t value, int n) {
    return (value + (((std::int64_t)1 << n) >> 1)) >> n;
}

// definitions.h:457 ROUND_POWER_OF_TWO (the unsigned branch the mvsad walk
// takes).
static std::uint32_t roundPow2U(std::uint32_t value, int n) {
    return (value + (((std::uint32_t)1 << n) >> 1)) >> n;
}

// av1me.c:124 PIXEL_TRANSFORM_ERROR_SCALE.
constexpr int kPixelTransformErrorScale = 4;
// rd_cost.h:34 / md_rate_estimation.h:29 / restoration.h:342.
constexpr int kRddivBits = 7;
constexpr int kAv1ProbCostShift = 9;
constexpr int kRdEpbShift = 6;

// av1me.c:126-132 svt_aom_mv_err_cost_light, verbatim.
int mvErrCostLight(const Mv* mv, const Mv* ref) {
    const std::uint32_t factor = 50;
    const std::uint32_t absmvdiffx = (std::uint32_t)absInt((int)mv->x - (int)ref->x);
    const std::uint32_t absmvdiffy = (std::uint32_t)absInt((int)mv->y - (int)ref->y);
    const std::uint32_t mvRate = 1296 + (factor * (absmvdiffx + absmvdiffy));
    return (int)mvRate;
}

// av1me.c:134-140 mvsad_err_cost_light, verbatim (the *8 light form).
int mvsadErrCostLight(const Mv* mv, const Mv* ref) {
    const std::uint32_t factor = 50;
    const std::uint32_t absmvdiffx = (std::uint32_t)(absInt((int)mv->x - (int)ref->x) * 8);
    const std::uint32_t absmvdiffy = (std::uint32_t)(absInt((int)mv->y - (int)ref->y) * 8);
    const std::uint32_t mvRate = 1296 + (factor * (absmvdiffx + absmvdiffy));
    return (int)mvRate;
}

// av1me.c:142-150 svt_aom_mv_err_cost, verbatim.
int mvErrCost(const Mv* mv, const Mv* ref, const int* mvjcost, const int* const mvcost[2], int errorPerBit) {
    if (mvcost) {
        Mv diff;
        diff.x = (std::int16_t)(mv->x - ref->x);
        diff.y = (std::int16_t)(mv->y - ref->y);
        return (int)roundPow2_64((std::int64_t)mvCost(&diff, mvjcost, mvcost) * errorPerBit,
                                 kRddivBits + kAv1ProbCostShift - kRdEpbShift + kPixelTransformErrorScale);
    }
    return 0;
}

// av1me.c:152-160 mvsad_err_cost, verbatim (the x wall threaded as SearchCtx).
int mvsadErrCost(const SearchCtx& x, const Mv* mv, const Mv* ref, int sadPerBit) {
    if (x.approxInterRate) {
        return mvsadErrCostLight(mv, ref);
    }
    Mv diff;
    diff.x = (std::int16_t)((mv->x - ref->x) * 8);
    diff.y = (std::int16_t)((mv->y - ref->y) * 8);
    return (int)roundPow2U((std::uint32_t)mvCost(&diff, x.nmvVecCost, x.mvCostStack) * (std::uint32_t)sadPerBit,
                           kAv1ProbCostShift);
}

// mcomp.c:24-31 the SSE_LAMBDA defines (SSE_LAMBDA_LOWRES / _MIDRES / _HDRES).
constexpr int kSseLambdaLowres = 2;
constexpr int kSseLambdaMidres = 0;
constexpr int kSseLambdaHdres = 1;

// mcomp.c:42-72 svt_mv_err_cost, verbatim (abs -> absInt; the narrowing of
// the diff halves into the union's int16 fields preserved).
int mvErrCostMcomp(const Mv* mv, const Mv* refMv, const int* mvjcost, const int* const mvcost[2], int errorPerBit,
                   MvCostType mvCostType) {
    Mv diff;
    diff.x = (std::int16_t)(mv->x - refMv->x);
    diff.y = (std::int16_t)(mv->y - refMv->y);
    Mv absDiff;
    absDiff.x = (std::int16_t)absInt((int)diff.x);
    absDiff.y = (std::int16_t)absInt((int)diff.y);

    switch (mvCostType) {
    case MV_COST_ENTROPY:
        if (mvcost) {
            return (int)roundPow2_64((std::int64_t)mvCost(&diff, mvjcost, mvcost) * errorPerBit,
                                     kRddivBits + kAv1ProbCostShift - kRdEpbShift + kPixelTransformErrorScale);
        }
        return 0;
    case MV_COST_L1_LOWRES:
        return (kSseLambdaLowres * (absDiff.y + absDiff.x)) >> 3;
    case MV_COST_L1_MIDRES:
        return (kSseLambdaMidres * (absDiff.y + absDiff.x)) >> 3;
    case MV_COST_L1_HDRES:
        return (kSseLambdaHdres * (absDiff.y + absDiff.x)) >> 3;
    case MV_COST_OPT:
        return (int)roundPow2_64((std::int64_t)((absDiff.y + absDiff.x) << 8) * errorPerBit,
                                 kRddivBits + kAv1ProbCostShift - kRdEpbShift + kPixelTransformErrorScale);
    case MV_COST_NONE:
        return 0;
    default:
        // assert(0 && "Invalid rd_cost_type") - assert-parked; the release
        // build falls through to the reference's return 0.
        return 0;
    }
}

// mcomp.c:74-97 svt_mv_err_cost_, verbatim.
int mvErrCostFromParams(const Mv* mv, const MvCostParam* mvCostParams) {
    return mvErrCostMcomp(mv,
                          mvCostParams->refMv,
                          mvCostParams->mvjcost,
                          mvCostParams->mvcost,
                          mvCostParams->errorPerBit,
                          mvCostParams->mvCostType);
}

// mcomp.c:775-777 svt_aom_fp_mv_err_cost, verbatim.
int fpMvErrCost(const Mv* mv, const MvCostParam* mvCostParams) {
    return mvErrCostFromParams(mv, mvCostParams);
}

// mcomp.c:99-104 svt_get_subpel_part, verbatim.
int subpelPart(int x) {
    return x & 7;
}

// mcomp.c:106-110 svt_get_buf_from_mv, verbatim (the svt_buf_2d* wall
// threaded as the buf/stride pair).
const std::uint8_t* subpelBufFromMv(const std::uint8_t* buf, int stride, const Mv& mv) {
    const int offset = (mv.y >> 3) * stride + (mv.x >> 3);
    return &buf[offset];
}

// mcomp.c:156-174 svt_estimated_pref_error, verbatim (the
// SUBPEL_SEARCH_VAR_PARAMS + MSBuffers walls threaded as SubpelVarParams).
int estimatedPrefError(const Mv* thisMv, const SubpelVarParams& varParams, std::uint32_t* sse) {
    const std::uint8_t* src = varParams.srcBuf;
    const std::uint8_t* ref = subpelBufFromMv(varParams.refBuf, varParams.refStride, *thisMv);
    const int srcStride = varParams.srcStride;
    const int refStride = varParams.refStride;

    const int subpelXQ3 = subpelPart(thisMv->x);
    const int subpelYQ3 = subpelPart(thisMv->y);

    return (int)varParams.svf(ref, refStride, subpelXQ3, subpelYQ3, src, srcStride, sse);
}

// mcomp.c:248-254 get_best_diag_step, verbatim.
Mv getBestDiagStep(int stepSize, std::uint32_t leftCost, std::uint32_t rightCost, std::uint32_t upCost,
                   std::uint32_t downCost) {
    Mv diagStep;
    diagStep.x = (std::int16_t)(leftCost <= rightCost ? -stepSize : stepSize);
    diagStep.y = (std::int16_t)(upCost <= downCost ? -stepSize : stepSize);
    return diagStep;
}

// motion_estimation.c:1152-1160 svt_aom_get_scaled_picture_distance,
// verbatim (DIVIDE_AND_CEIL(x, y) = ((x) + ((y) - 1)) / (y), utility.h:97).
std::uint16_t getScaledPictureDistance(std::uint16_t dist) {
    return (std::uint16_t)((dist * 5 + 7) / 8);
}

// motion_estimation.c:1164-1168 search_area_multipliers, verbatim.
static const double searchAreaMultipliers[3][5] = {
    {1.0, 1.0, 3.0, 4.0, 5.0}, /* boost=1 */
    {1.0, 1.0, 2.5, 3.5, 4.5}, /* boost=2 */
    {1.0, 1.0, 2.0, 2.5, 3.5} /* boost=3 */
};

// motion_estimation.c:1170-1182 apply_me_sa_boost, verbatim.
void applyMeSaBoost(std::int16_t* width, std::int16_t* height, std::uint64_t hmeSad, int scClassMeBoost) {
    int index;
    if (hmeSad > 4 * 64 * 64) {
        index = 4;
    } else if (hmeSad > 3 * 64 * 64) {
        index = 3;
    } else if (hmeSad > 2 * 64 * 64) {
        index = 2;
    } else {
        index = 0;
    }

    const double mult = searchAreaMultipliers[scClassMeBoost - 1][index];

    *width = (std::int16_t)(*width * mult);
    *height = (std::int16_t)(*height * mult);
}

// lambda_rate_tables.h:20 COST_PRECISION.
constexpr int kCostPrecision = 8;

// motion_estimation.c:1540-1564 get_zz_sad, verbatim (the svt_nxm_sad_kernel
// rtcd call routed to the landed nxm helper).
std::uint32_t getZzSad(const RefPic& refPic, const MeCtx& meCtx, std::uint32_t sbOriginX, std::uint32_t sbOriginY,
                       std::uint32_t sbWidth, std::uint32_t sbHeight) {
    std::uint32_t zeroMvSad;
    const std::int16_t orgX = (std::int16_t)sbOriginX;
    const std::int16_t orgY = (std::int16_t)sbOriginY;
    const std::uint32_t subsampleSad = 1;

    const std::int32_t searchRegionIndex = orgX + (orgY)*refPic.yStride;

    zeroMvSad = nxmSadKernelHelper(meCtx.b64SrcPtr,
                                   meCtx.b64SrcStride << subsampleSad,
                                   &refPic.yBuffer[searchRegionIndex],
                                   refPic.yStride << subsampleSad,
                                   sbHeight >> subsampleSad,
                                   sbWidth);

    zeroMvSad = zeroMvSad << subsampleSad;

    return zeroMvSad;
}

// motion_estimation.c:1060-1126 check_00_center, verbatim (the EbPicture-
// BufferDesc wall threaded as RefPic; BLOCK_SIZE_64 = 64, definitions.h:2017).
std::uint64_t check00Center(const RefPic& refPic, const MeCtx& meCtx, std::uint32_t sbOriginX,
                            std::uint32_t sbOriginY, std::uint32_t sbWidth, std::uint32_t sbHeight,
                            std::int16_t* xSearchCenter, std::int16_t* ySearchCenter, std::uint32_t zzSad) {
    const std::int16_t orgX = (std::int16_t)sbOriginX;
    const std::int16_t orgY = (std::int16_t)sbOriginY;
    const int subsampleSad = 1;
    const std::int16_t padWidth = (std::int16_t)64 - 1;
    const std::int16_t padHeight = (std::int16_t)64 - 1;

    std::int32_t searchRegionIndex = orgX + (orgY)*refPic.yStride;
    std::uint64_t zeroMvSad;
    if (meCtx.meEarlyExitTh) {
        zeroMvSad = zzSad;
    } else {
        zeroMvSad = nxmSadKernelHelper(meCtx.b64SrcPtr,
                                       meCtx.b64SrcStride << subsampleSad,
                                       &refPic.yBuffer[searchRegionIndex],
                                       refPic.yStride << subsampleSad,
                                       sbHeight >> subsampleSad,
                                       sbWidth);
    }

    zeroMvSad = zeroMvSad << subsampleSad;

    // FIX
    // Correct the left edge of the Search Area if it is not on the reference
    // Picture
    *xSearchCenter = ((std::int16_t)(orgX + *xSearchCenter) < -padWidth) ? (std::int16_t)(-padWidth - orgX)
                                                                         : *xSearchCenter;
    // Correct the right edge of the Search Area if its not on the reference
    // Picture
    *xSearchCenter = ((std::int16_t)(orgX + *xSearchCenter) > (std::int16_t)refPic.width - 1)
        ? (std::int16_t)(*xSearchCenter - ((std::int16_t)(orgX + *xSearchCenter) - ((std::int16_t)refPic.width - 1)))
        : *xSearchCenter;
    // Correct the top edge of the Search Area if it is not on the reference
    // Picture
    *ySearchCenter = ((std::int16_t)(orgY + *ySearchCenter) < -padHeight) ? (std::int16_t)(-padHeight - orgY)
                                                                          : *ySearchCenter;
    // Correct the bottom edge of the Search Area if its not on the reference
    // Picture
    *ySearchCenter = ((std::int16_t)(orgY + *ySearchCenter) > (std::int16_t)refPic.height - 1)
        ? (std::int16_t)(*ySearchCenter - ((std::int16_t)(orgY + *ySearchCenter) - ((std::int16_t)refPic.height - 1)))
        : *ySearchCenter;
    ///

    const std::uint64_t zeroMvCost = zeroMvSad << kCostPrecision;
    searchRegionIndex = (std::int16_t)(orgX) + *xSearchCenter +
        ((std::int16_t)(orgY) + *ySearchCenter) * refPic.yStride;

    std::uint64_t hmeMvSad = nxmSadKernelHelper(meCtx.b64SrcPtr,
                                                meCtx.b64SrcStride << subsampleSad,
                                                &refPic.yBuffer[searchRegionIndex],
                                                refPic.yStride << subsampleSad,
                                                sbHeight >> subsampleSad,
                                                sbWidth);

    hmeMvSad = hmeMvSad << subsampleSad;
    const std::uint64_t hmeMvCost = hmeMvSad << kCostPrecision;
    const std::uint64_t searchCenterCost = zeroMvCost < hmeMvCost ? zeroMvCost : hmeMvCost;

    *xSearchCenter = (searchCenterCost == zeroMvCost) ? 0 : *xSearchCenter;
    *ySearchCenter = (searchCenterCost == zeroMvCost) ? 0 : *ySearchCenter;
    return hmeMvSad;
}

// motion_estimation.c:1566-1592 check_prehme_early_exit, verbatim.
bool checkPrehmeEarlyExit(MeCtx& meCtx, std::uint8_t listI, std::uint8_t refI, std::uint8_t srI) {
    SearchInfo* prehmeData = &meCtx.prehmeData[listI][refI][srI];

    if (meCtx.meEarlyExitTh) {
        if (meCtx.zzSad[listI][refI] < meCtx.meEarlyExitTh) {
            prehmeData->bestMv.as_int = 0;
            prehmeData->sad = 0;
            prehmeData->valid = 1;
            return true;
        }
    }

    if (meCtx.prehmeL1EarlyExit) {
        if (listI == 1 && meCtx.prehmeData[0][refI][srI].valid &&
            ((meCtx.prehmeData[0][refI][srI].sad < (32 * 32)) ||
             ((absInt(meCtx.prehmeData[0][refI][srI].bestMv.x) < 16) &&
              (absInt(meCtx.prehmeData[0][refI][srI].bestMv.y) < 16)))) {
            prehmeData->bestMv.x = (std::int16_t)-meCtx.prehmeData[0][refI][srI].bestMv.x;
            prehmeData->bestMv.y = (std::int16_t)-meCtx.prehmeData[0][refI][srI].bestMv.y;
            prehmeData->sad = meCtx.prehmeData[0][refI][srI].sad;
            prehmeData->valid = 1;
            return true;
        }
    }
    return false;
}

// motion_estimation.h:87-95 tab8x8, verbatim.
static const std::uint32_t kTab8x8[64] = {
    0,  1,  4,  5,  16, 17, 20, 21, 2,  3,  6,  7,  18, 19, 22, 23,
    8,  9,  12, 13, 24, 25, 28, 29, 10, 11, 14, 15, 26, 27, 30, 31,
    32, 33, 36, 37, 48, 49, 52, 53, 34, 35, 38, 39, 50, 51, 54, 55,
    40, 41, 44, 45, 56, 57, 60, 61, 42, 43, 46, 47, 58, 59, 62, 63};

// me_context.h:54 ME_TIER_ZERO_PU_8x8_0.
constexpr std::uint32_t kMeTierZeroPu8x8_0 = 21;

// motion_estimation.c:1414-1455 me_prune_ref, verbatim (the
// me_hme_prune_ctrls fields threaded into MeCtx).
void mePruneRef(MeCtx& meCtx) {
    const std::uint8_t numOfListToSearch = meCtx.numOfListToSearch;
    for (std::uint8_t listIndex = 0; listIndex < numOfListToSearch; ++listIndex) {
        const std::uint8_t numOfRefPicToSearch = meCtx.numOfRefPicToSearch[listIndex];
        // Ref Picture Loop
        for (std::uint8_t refPicIndex = 0; refPicIndex < numOfRefPicToSearch; ++refPicIndex) {
            meCtx.searchResults[listIndex][refPicIndex].hmeSad = 0;
            // Get hme results
            if (meCtx.searchResults[listIndex][refPicIndex].doRef == 0) {
                meCtx.searchResults[listIndex][refPicIndex].hmeSad = (std::uint64_t)kMaxSadValue * 64;
                continue;
            }
            const std::uint32_t* pBestSad8x8 = &meCtx.pSbBestSad[listIndex][refPicIndex][kMeTierZeroPu8x8_0];
            // 8x8   [64 partitions]
            for (std::uint32_t puIndex = 0; puIndex < 64; ++puIndex) {
                const std::uint32_t idx = kTab8x8[puIndex];
                meCtx.searchResults[listIndex][refPicIndex].hmeSad += pBestSad8x8[idx];
            }
        }
    }

    const std::uint16_t pruneRefTh = meCtx.pruneRefIfMeSadDevBiggerThanTh;
    if (meCtx.enableMeHmeRefPruning && pruneRefTh != (std::uint16_t)~0) {
        std::uint64_t best = (std::uint64_t)~0;
        for (int i = 0; i < kMaxNumOfRefPicList; ++i) {
            for (int j = 0; j < kRefListMaxDepth; ++j) {
                if (meCtx.searchResults[i][j].hmeSad < best) {
                    best = meCtx.searchResults[i][j].hmeSad;
                }
            }
        }
        for (std::uint32_t li = 0; li < kMaxNumOfRefPicList; li++) {
            for (std::uint32_t ri = 1; ri < kRefListMaxDepth; ri++) {
                // Prune references based on ME sad
                if ((meCtx.searchResults[li][ri].hmeSad - best) * 100 > ((std::uint64_t)pruneRefTh * best)) {
                    meCtx.searchResults[li][ri].doRef = 0;
                }
            }
        }
    }
}

// motion_estimation.c:1736-1766 get_worst_quadrant, verbatim (the
// svt_aom_assert_err wall is the named early-return deviation; the
// no-loop-for-VS2022 comment is the reference's own).
void getWorstQuadrant(const MeCtx& meCtx, std::uint32_t listIndex, std::uint32_t refPicIndex, std::uint8_t* bestW,
                      std::uint8_t* bestH) {
    if (meCtx.numHmeSaW != 2 || meCtx.numHmeSaH != 2) {
        // svt_aom_assert_err(0, "update other quadrant sizes") - the release
        // build's observable behavior is the early return.
        return;
    }
    std::uint64_t maxSad = 0;

    if (meCtx.hmeLevel0Sad[listIndex][refPicIndex][0][0] > maxSad) {
        maxSad = meCtx.hmeLevel0Sad[listIndex][refPicIndex][0][0];
        *bestW = 0;
        *bestH = 0;
    }
    if (meCtx.hmeLevel0Sad[listIndex][refPicIndex][1][0] > maxSad) {
        maxSad = meCtx.hmeLevel0Sad[listIndex][refPicIndex][1][0];
        *bestW = 1;
        *bestH = 0;
    }
    if (meCtx.hmeLevel0Sad[listIndex][refPicIndex][0][1] > maxSad) {
        maxSad = meCtx.hmeLevel0Sad[listIndex][refPicIndex][0][1];
        *bestW = 0;
        *bestH = 1;
    }
    if (meCtx.hmeLevel0Sad[listIndex][refPicIndex][1][1] > maxSad) {
        *bestW = 1;
        *bestH = 1;
    }
}

// motion_estimation.c:1673-1734 get_hme_l0_search_area, verbatim (the
// svt_aom_get_scaled_picture_distance call is the landed twin).
void getHmeL0SearchArea(MeCtx& meCtx, std::uint8_t listIndex, std::uint8_t refPicIndex, std::uint16_t dist,
                        std::int16_t* saWidth, std::int16_t* saHeight) {
    // Reduce HME search area for higher ref indices
    if (meCtx.enableMeSrAdjustment && meCtx.distanceBasedHmeResizing) {
        std::uint8_t isHor = 1;
        std::uint8_t isVer = 1;
        std::uint8_t isStill = 0;

        if (meCtx.reduceHmeL0SrThMin && meCtx.reduceHmeL0SrThMax) {
            if (listIndex || refPicIndex) {
                const std::int16_t l0Mvx = meCtx.xHmeLevel0SearchCenter[0][0][0][0];
                const std::int16_t l0Mvy = meCtx.yHmeLevel0SearchCenter[0][0][0][0];

                // Determine whether the computed motion from list0/ref_index0
                // is in vertical or horizontal direction
                isVer = (std::uint8_t)((absInt(l0Mvx) < meCtx.reduceHmeL0SrThMin) &&
                                       (absInt(l0Mvy) > meCtx.reduceHmeL0SrThMax));
                isHor = (std::uint8_t)((absInt(l0Mvx) > meCtx.reduceHmeL0SrThMax) &&
                                       (absInt(l0Mvy) < meCtx.reduceHmeL0SrThMin));
                isStill = (std::uint8_t)((absInt(l0Mvx) < (meCtx.reduceHmeL0SrThMin * 3)) &&
                                         (absInt(l0Mvy) < (meCtx.reduceHmeL0SrThMin * 3)));
            }
        }

        std::uint8_t xOffset = 1;
        std::uint8_t yOffset = 1;
        if (!isVer) {
            yOffset = 2;
        }
        if (!isHor) {
            xOffset = 2;
        }

        if (meCtx.enableMeSrAdjustment == 2) {
            if (isStill) {
                xOffset = 4;
                yOffset = 4;
            }
        }

        meCtx.hmeL0Sa.saMin.width = (std::uint16_t)(meCtx.hmeL0Sa.saMin.width / (xOffset + refPicIndex));
        meCtx.hmeL0Sa.saMin.height = (std::uint16_t)(meCtx.hmeL0Sa.saMin.height / (yOffset + refPicIndex));
        meCtx.hmeL0Sa.saMax.width = (std::uint16_t)(meCtx.hmeL0Sa.saMax.width / (xOffset + refPicIndex));
        meCtx.hmeL0Sa.saMax.height = (std::uint16_t)(meCtx.hmeL0Sa.saMax.height / (yOffset + refPicIndex));
    }

    const std::int32_t hmeSrFactor = getScaledPictureDistance(dist);

    // Derive the search area width and height, rounding the width up to the
    // nearest sixteenth
    std::int16_t searchAreaWidth = (std::int16_t)(meCtx.hmeL0Sa.saMin.width / meCtx.numHmeSaW);
    searchAreaWidth = (std::int16_t)std::min((((searchAreaWidth * hmeSrFactor) + 15) & ~0x0F),
                                             (((meCtx.hmeL0Sa.saMax.width / meCtx.numHmeSaW) + 15) & ~0x0F));
    std::int16_t searchAreaHeight = (std::int16_t)(meCtx.hmeL0Sa.saMin.height / meCtx.numHmeSaH);
    searchAreaHeight =
        (std::int16_t)std::min((searchAreaHeight * hmeSrFactor), meCtx.hmeL0Sa.saMax.height / meCtx.numHmeSaH);

    *saWidth = searchAreaWidth;
    *saHeight = searchAreaHeight;
}

// motion_estimation.c:2787-2829 init_me_hme_data, verbatim (the svt_memset
// walls become value fills over the carrier arrays; MAX_U32 = 0xFFFFFFFF,
// definitions.h:2182).
void initMeHmeData(MeCtx& meCtx) {
    // Initialize HME search centres to 0
    if (meCtx.enableHmeFlag) {
        for (int l = 0; l < kMaxNumOfRefPicList; ++l) {
            for (int r = 0; r < kMaxRefIdx; ++r) {
                for (int cx = 0; cx < 2; ++cx) {
                    for (int cy = 0; cy < 2; ++cy) {
                        meCtx.xHmeLevel0SearchCenter[l][r][cx][cy] = 0;
                        meCtx.yHmeLevel0SearchCenter[l][r][cx][cy] = 0;
                        meCtx.xHmeLevel1SearchCenter[l][r][cx][cy] = 0;
                        meCtx.yHmeLevel1SearchCenter[l][r][cx][cy] = 0;
                        meCtx.xHmeLevel2SearchCenter[l][r][cx][cy] = 0;
                        meCtx.yHmeLevel2SearchCenter[l][r][cx][cy] = 0;
                    }
                }
            }
        }
    }

    // R2R FIX: no winner integer MV is set in special case like initial
    // p_sb_best_mv for overlay case, then it sends dirty p_sb_best_mv to MD,
    // initializing it is necessary
    for (int l = 0; l < kMaxNumOfRefPicList; ++l) {
        for (int r = 0; r < kMaxRefIdx; ++r) {
            for (int i = 0; i < kSquarePuCount; ++i) {
                meCtx.pSbBestMv[l][r][i] = 0;
            }
        }
    }

    // init hme results buffer
    for (std::uint32_t li = 0; li < kMaxNumOfRefPicList; li++) {
        for (std::uint32_t ri = 0; ri < kRefListMaxDepth; ri++) {
            if (meCtx.meType != kMeMctf) {
                meCtx.searchResults[li][ri].listI = (std::uint8_t)li;
            }
            meCtx.searchResults[li][ri].refI = (std::uint8_t)ri;
            meCtx.searchResults[li][ri].doRef = 1;
            meCtx.searchResults[li][ri].hmeSad = 0xFFFFFFFFu;
            meCtx.reduceMeSrDivisor[li][ri] = 1;
            meCtx.zzSad[li][ri] = (std::uint32_t)~0;
            meCtx.prehmeData[li][ri][0].valid = 0;
            meCtx.prehmeData[li][ri][1].valid = 0;
        }
    }
    for (int l = 0; l < kMaxNumOfRefPicList; ++l) {
        for (int r = 0; r < kRefListMaxDepth; ++r) {
            for (int s = 0; s < 2; ++s) {
                meCtx.performedPhme[l][r][s] = 0;
            }
        }
    }
}

// motion_estimation.c:2831-2886 me_static_b64_bypass, verbatim (the
// me_ds_ref_array[0][0].picture_ptr wall threaded as MeCtx::dsRef00).
bool meStaticB64Bypass(MeCtx& meCtx, std::uint32_t b64OriginX, std::uint32_t b64OriginY) {
    if (!meCtx.meStaticB64Th) {
        return false;
    }

    // The bypass decision uses the list0/ref0 zero-motion SAD: if the block
    // is static against the primary reference, skip the full ME pipeline
    // (HME + integer search).
    const std::uint32_t l0r0Raw = getZzSad(meCtx.dsRef00, meCtx, b64OriginX, b64OriginY, meCtx.b64Width,
                                           meCtx.b64Height);
    if ((std::uint64_t)l0r0Raw * 64 * 64 >= (std::uint64_t)meCtx.meStaticB64Th * meCtx.b64Width * meCtx.b64Height) {
        return false;
    }

    // Static block against list0/ref0. Fill the zero-MV ME result for
    // list0/ref0 only, and disable every farther reference (do_ref = 0) so
    // mode decision skips it entirely. (The reference's own long comment is
    // preserved in abridged form: do_ref is re-initialized to 1 for every
    // reference by init_me_hme_data() at the top of each b64, so disabling
    // it here is per-b64 safe.)
    const std::uint32_t zzSad = (std::uint32_t)((std::uint64_t)l0r0Raw * 64 * 64 /
                                                (meCtx.b64Width * meCtx.b64Height));
    meCtx.zzSad[0][0] = zzSad;
    meCtx.searchResults[0][0].doRef = 1;
    // 64x64
    meCtx.pSbBestSad[0][0][0] = zzSad;  // RASTER_SCAN_CU_INDEX_64x64 = 0
    // 32x32
    const std::uint32_t sad32 = zzSad >> 2;
    for (int i = 1; i <= 4; i++) {
        meCtx.pSbBestSad[0][0][i] = sad32;
    }
    // 16x16
    const std::uint32_t sad16 = zzSad >> 4;
    for (int i = 5; i <= 20; i++) {
        meCtx.pSbBestSad[0][0][i] = sad16;
    }
    // 8x8
    const std::uint32_t sad8 = zzSad >> 6;
    for (int i = 21; i < kSquarePuCount; i++) {
        meCtx.pSbBestSad[0][0][i] = sad8;
    }
    // Disable every farther reference: do_ref = 0 => skipped in MD, stale
    // SAD never read.
    for (std::uint32_t listIndex = 0; listIndex < meCtx.numOfListToSearch; ++listIndex) {
        for (std::uint8_t refIdx = 0; refIdx < meCtx.numOfRefPicToSearch[listIndex]; ++refIdx) {
            if (listIndex != 0 || refIdx != 0) {
                meCtx.searchResults[listIndex][refIdx].doRef = 0;
            }
        }
    }
    return true;
}

// motion_estimation.c:2738-2785 compute_distortion, verbatim (the
// PictureParentControlSet walls threaded as PcsCtx; the
// scs->input_resolution <= INPUT_SIZE_480p_RANGE gate threaded as the
// 480pOrBelow flag).
void computeDistortion(const PcsCtx& pcs, std::uint32_t b64Index, const MeCtx& meCtx) {
    const B64Geom* b64Geom = &pcs.b64Geom[b64Index];
    const std::uint32_t b64Size = 64 * 64;
    std::uint32_t dist64x64 = 0, dist32x32 = 0, dist16x16 = 0, dist8x8 = 0;

    // 64x64
    { dist64x64 = meCtx.meDistortion[0]; }

    // 32x32
    for (unsigned i = 0; i < 4; i++) {
        dist32x32 += meCtx.meDistortion[1 + i];
    }

    // 16x16
    for (unsigned i = 0; i < 16; i++) {
        dist16x16 += meCtx.meDistortion[5 + i];
    }

    // 8x8
    for (unsigned i = 0; i < 64; i++) {
        dist8x8 += meCtx.meDistortion[21 + i];
    }

    const std::uint64_t meanDist8x8 = dist8x8 / 64;
    std::uint64_t sumOfsqDist8x8 = 0;
    for (unsigned i = 0; i < 64; i++) {
        const std::int64_t diff = ((std::int64_t)meCtx.meDistortion[21 + i] - (std::int64_t)meanDist8x8);
        sumOfsqDist8x8 += (std::uint64_t)(diff * diff);
    }

    pcs.me8x8CostVariance[b64Index] = (std::uint32_t)(sumOfsqDist8x8 / 64);
    // Compute the sum of the distortion of all 16 16x16 (720 and above) and
    // 64 8x8 (for lower resolutions) blocks in the SB
    pcs.rcMeDistortion[b64Index] = (pcs.inputResolution480pOrBelow) ? dist8x8 : dist16x16;
    const std::uint32_t pixNum = (std::uint32_t)(b64Geom->width * b64Geom->height);
    // Normalize
    pcs.me64x64Distortion[b64Index] = (dist64x64 * b64Size) / (pixNum);
    pcs.me32x32Distortion[b64Index] = (dist32x32 * b64Size) / (pixNum);
    pcs.me16x16Distortion[b64Index] = (dist16x16 * b64Size) / (pixNum);
    pcs.me8x8Distortion[b64Index] = (dist8x8 * b64Size) / (pixNum);
}

// motion_estimation.c:2289-2332 hme_prune_ref_and_adjust_sr, verbatim.
void hmePruneRefAndAdjustSr(MeCtx& meCtx) {
    const std::uint16_t pruneRefTh = meCtx.pruneRefIfHmeSadDevBiggerThanTh;
    if (meCtx.enableMeHmeRefPruning && (pruneRefTh != (std::uint16_t)~0)) {
        std::uint64_t best = (std::uint64_t)~0;
        for (int i = 0; i < kMaxNumOfRefPicList; ++i) {
            for (int j = 0; j < kRefListMaxDepth; ++j) {
                if (meCtx.searchResults[i][j].hmeSad < best) {
                    best = meCtx.searchResults[i][j].hmeSad;
                }
            }
        }
        // Prune references based on HME sad
        for (std::uint32_t li = 0; li < kMaxNumOfRefPicList; li++) {
            for (std::uint32_t ri = 1; ri < kRefListMaxDepth; ri++) {
                if ((meCtx.searchResults[li][ri].hmeSad - best) * 100 > ((std::uint64_t)pruneRefTh * best)) {
                    meCtx.searchResults[li][ri].doRef = 0;
                }
            }
        }
    }
    if (meCtx.enableMeSrAdjustment) {
        const std::uint16_t mvLengthTh = meCtx.reduceMeSrBasedOnMvLengthTh;
        const std::uint16_t stationaryHmeSadAbsTh = meCtx.stationaryHmeSadAbsTh;
        const std::uint16_t reduceMeSrBasedOnHmeSadAbsTh = meCtx.reduceMeSrBasedOnHmeSadAbsTh;
        // Reduce the ME search region if the hme sad is low
        for (std::uint32_t li = 0; li < kMaxNumOfRefPicList; li++) {
            for (std::uint32_t ri = 0; ri < kRefListMaxDepth; ri++) {
                if (absInt(meCtx.searchResults[li][ri].hmeScX) <= mvLengthTh &&
                    absInt(meCtx.searchResults[li][ri].hmeScY) <= mvLengthTh &&
                    meCtx.searchResults[li][ri].hmeSad < stationaryHmeSadAbsTh) {
                    meCtx.reduceMeSrDivisor[li][ri] = meCtx.stationaryMeSrDivisor;
                } else if (meCtx.searchResults[li][ri].hmeSad < reduceMeSrBasedOnHmeSadAbsTh) {
                    meCtx.reduceMeSrDivisor[li][ri] = meCtx.meSrDivisorForLowHmeSad;
                }
            }
        }
    }
}

// av1me.c:97-122 svt_av1_set_mv_search_range, verbatim
// (AOMMAX/AOMMIN -> std max/min on ints; MAX_FULL_PEL_VAL = kMaxFullPelVal).
void setMvSearchRange(MvLimits* mvLimits, const Mv* mv) {
    int colMin = (mv->x >> 3) - kMaxFullPelVal + !!(mv->x & 7);
    int rowMin = (mv->y >> 3) - kMaxFullPelVal + !!(mv->y & 7);
    int colMax = (mv->x >> 3) + kMaxFullPelVal;
    int rowMax = (mv->y >> 3) + kMaxFullPelVal;

    colMin = colMin > ((kMvLow >> 3) + 1) ? colMin : ((kMvLow >> 3) + 1);
    rowMin = rowMin > ((kMvLow >> 3) + 1) ? rowMin : ((kMvLow >> 3) + 1);
    colMax = colMax < ((kMvUpp >> 3) - 1) ? colMax : ((kMvUpp >> 3) - 1);
    rowMax = rowMax < ((kMvUpp >> 3) - 1) ? rowMax : ((kMvUpp >> 3) - 1);

    // Get intersection of UMV window and valid MV window to reduce # of
    // checks in diamond search.
    if (mvLimits->col_min < colMin) {
        mvLimits->col_min = colMin;
    }
    if (mvLimits->col_max > colMax) {
        mvLimits->col_max = colMax;
    }
    if (mvLimits->row_min < rowMin) {
        mvLimits->row_min = rowMin;
    }
    if (mvLimits->row_max > rowMax) {
        mvLimits->row_max = rowMax;
    }
}

// av1me.c:190-193 is_mv_in, verbatim.
int isMvIn(const MvLimits* mvLimits, const Mv* mv) {
    return (mv->x >= mvLimits->col_min) && (mv->x <= mvLimits->col_max) && (mv->y >= mvLimits->row_min) &&
        (mv->y <= mvLimits->row_max);
}

// av1me.c:694-707 set_subpel_mv_search_range, verbatim.
void setSubpelMvSearchRange(const MvLimits* mvLimits, int* colMin, int* colMax, int* rowMin, int* rowMax,
                            const Mv* refMv) {
    const int maxMv = kMaxFullPelVal * 8;
    const int minc = mvLimits->col_min * 8 > refMv->x - maxMv ? mvLimits->col_min * 8 : refMv->x - maxMv;
    const int maxc = mvLimits->col_max * 8 < refMv->x + maxMv ? mvLimits->col_max * 8 : refMv->x + maxMv;
    const int minr = mvLimits->row_min * 8 > refMv->y - maxMv ? mvLimits->row_min * 8 : refMv->y - maxMv;
    const int maxr = mvLimits->row_max * 8 < refMv->y + maxMv ? mvLimits->row_max * 8 : refMv->y + maxMv;

    *colMin = kMvLow + 1 > minc ? kMvLow + 1 : minc;
    *colMax = kMvUpp - 1 < maxc ? kMvUpp - 1 : maxc;
    *rowMin = kMvLow + 1 > minr ? kMvLow + 1 : minr;
    *rowMax = kMvUpp - 1 < maxr ? kMvUpp - 1 : maxr;
}

// motion_estimation.c:100-162 svt_ext_sad_calculation_8x8_16x16_c, verbatim
// (the internal compute8x4/compute8x8 kernels route to the landed nxm
// instances sad8x4 / sad8x8 - see the header's function-identity note).
void extSadCalculation8x8_16x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                                std::uint32_t refStride, std::uint32_t* pBestSad8x8, std::uint32_t* pBestSad16x16,
                                std::uint32_t* pBestMv8x8, std::uint32_t* pBestMv16x16, std::uint32_t mv,
                                std::uint32_t* pSad16x16, std::uint32_t* pSad8x8, bool subSad) {
    std::uint32_t sad16x16;

    if (subSad) {
        pSad8x8[0] = (sad8x4(src + 0 * srcStride + 0, 2 * srcStride, ref + 0 * refStride + 0, 2 * refStride)) << 1;
        pSad8x8[1] = (sad8x4(src + 0 * srcStride + 8, 2 * srcStride, ref + 0 * refStride + 8, 2 * refStride)) << 1;
        pSad8x8[2] =
            (sad8x4(src + 8 * srcStride + 0, 2 * srcStride, ref + 8 * refStride + 0, 2 * refStride)) << 1;
        pSad8x8[3] =
            (sad8x4(src + 8 * srcStride + 8, 2 * srcStride, ref + 8 * refStride + 8, 2 * refStride)) << 1;
    } else {
        pSad8x8[0] = sad8x8(src + 0 * srcStride + 0, srcStride, ref + 0 * refStride + 0, refStride);
        pSad8x8[1] = sad8x8(src + 0 * srcStride + 8, srcStride, ref + 0 * refStride + 8, refStride);
        pSad8x8[2] = sad8x8(src + 8 * srcStride + 0, srcStride, ref + 8 * refStride + 0, refStride);
        pSad8x8[3] = sad8x8(src + 8 * srcStride + 8, srcStride, ref + 8 * refStride + 8, refStride);
    }

    if (pSad8x8[0] < pBestSad8x8[0]) {
        pBestSad8x8[0] = pSad8x8[0];
        pBestMv8x8[0] = mv;
    }

    if (pSad8x8[1] < pBestSad8x8[1]) {
        pBestSad8x8[1] = pSad8x8[1];
        pBestMv8x8[1] = mv;
    }

    if (pSad8x8[2] < pBestSad8x8[2]) {
        pBestSad8x8[2] = pSad8x8[2];
        pBestMv8x8[2] = mv;
    }

    if (pSad8x8[3] < pBestSad8x8[3]) {
        pBestSad8x8[3] = pSad8x8[3];
        pBestMv8x8[3] = mv;
    }

    sad16x16 = pSad8x8[0] + pSad8x8[1] + pSad8x8[2] + pSad8x8[3];
    if (sad16x16 < pBestSad16x16[0]) {
        pBestSad16x16[0] = sad16x16;
        pBestMv16x16[0] = mv;
    }

    *pSad16x16 = sad16x16;
}

// motion_estimation.c:164-200 svt_ext_sad_calculation_32x32_64x64_c,
// verbatim.
void extSadCalculation32x32_64x64(const std::uint32_t* pSad16x16, std::uint32_t* pBestSad32x32,
                                  std::uint32_t* pBestSad64x64, std::uint32_t* pBestMv32x32,
                                  std::uint32_t* pBestMv64x64, std::uint32_t mv, std::uint32_t* pSad32x32) {
    std::uint32_t sad32x32_0, sad32x32_1, sad32x32_2, sad32x32_3, sad64x64;

    pSad32x32[0] = sad32x32_0 = pSad16x16[0] + pSad16x16[1] + pSad16x16[2] + pSad16x16[3];
    if (sad32x32_0 < pBestSad32x32[0]) {
        pBestSad32x32[0] = sad32x32_0;
        pBestMv32x32[0] = mv;
    }

    pSad32x32[1] = sad32x32_1 = pSad16x16[4] + pSad16x16[5] + pSad16x16[6] + pSad16x16[7];
    if (sad32x32_1 < pBestSad32x32[1]) {
        pBestSad32x32[1] = sad32x32_1;
        pBestMv32x32[1] = mv;
    }

    pSad32x32[2] = sad32x32_2 = pSad16x16[8] + pSad16x16[9] + pSad16x16[10] + pSad16x16[11];
    if (sad32x32_2 < pBestSad32x32[2]) {
        pBestSad32x32[2] = sad32x32_2;
        pBestMv32x32[2] = mv;
    }

    pSad32x32[3] = sad32x32_3 = pSad16x16[12] + pSad16x16[13] + pSad16x16[14] + pSad16x16[15];
    if (sad32x32_3 < pBestSad32x32[3]) {
        pBestSad32x32[3] = sad32x32_3;
        pBestMv32x32[3] = mv;
    }
    sad64x64 = sad32x32_0 + sad32x32_1 + sad32x32_2 + sad32x32_3;
    if (sad64x64 < pBestSad64x64[0]) {
        pBestSad64x64[0] = sad64x64;
        pBestMv64x64[0] = mv;
    }
}

// definitions.h:2036-2037 _MVXT / _MVYT.
static std::int16_t mvxt(std::uint32_t mv) {
    return (std::int16_t)(mv & 0xFFFF);
}
static std::int16_t mvyt(std::uint32_t mv) {
    return (std::int16_t)(mv >> 16);
}

// motion_estimation.c:202-316 svt_ext_eight_sad_calculation_8x8_16x16,
// verbatim (the internal 8x4/8x8 kernels route to the landed instances; the
// p_eight_sad8x8 param is (void)ed exactly as the reference does).
void extEightSadCalculation8x8_16x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                                     std::uint32_t refStride, std::uint32_t mv, std::uint32_t start16x16Pos,
                                     std::uint32_t* pBestSad8x8, std::uint32_t* pBestSad16x16,
                                     std::uint32_t* pBestMv8x8, std::uint32_t* pBestMv16x16,
                                     std::uint32_t pEightSad16x16[16][8], std::uint32_t pEightSad8x8[64][8],
                                     bool subSad) {
    const std::uint32_t start8x8Pos = 4 * start16x16Pos;
    std::int16_t xMv, yMv;

    (void)pEightSad8x8;

    pBestSad8x8 += start8x8Pos;
    pBestMv8x8 += start8x8Pos;
    pBestSad16x16 += start16x16Pos;
    pBestMv16x16 += start16x16Pos;
    if (subSad) {
        const std::uint32_t srcStrideSub = (srcStride << 1);
        const std::uint32_t refStrideSub = (refStride << 1);
        for (int searchIndex = 0; searchIndex < 8; searchIndex++) {
            std::uint32_t sad8x8_0 =
                (sad8x4(src, srcStrideSub, ref + searchIndex, refStrideSub)) << 1;
            if (sad8x8_0 < pBestSad8x8[0]) {
                pBestSad8x8[0] = sad8x8_0;
                xMv = (std::int16_t)(mvxt(mv) + (std::int16_t)searchIndex);
                yMv = mvyt(mv);
                pBestMv8x8[0] = ((std::uint32_t)yMv << 16) | ((std::uint16_t)xMv);
            }

            std::uint32_t sad8x8_1 =
                (sad8x4(src + 8, srcStrideSub, ref + 8 + searchIndex, refStrideSub)) << 1;
            if (sad8x8_1 < pBestSad8x8[1]) {
                pBestSad8x8[1] = sad8x8_1;
                xMv = (std::int16_t)(mvxt(mv) + (std::int16_t)searchIndex);
                yMv = mvyt(mv);
                pBestMv8x8[1] = ((std::uint32_t)yMv << 16) | ((std::uint16_t)xMv);
            }

            std::uint32_t sad8x8_2 = (sad8x4(src + (srcStride << 3),
                                             srcStrideSub,
                                             ref + (refStride << 3) + searchIndex,
                                             refStrideSub)) << 1;
            if (sad8x8_2 < pBestSad8x8[2]) {
                pBestSad8x8[2] = sad8x8_2;
                xMv = (std::int16_t)(mvxt(mv) + (std::int16_t)searchIndex);
                yMv = mvyt(mv);
                pBestMv8x8[2] = ((std::uint32_t)yMv << 16) | ((std::uint16_t)xMv);
            }

            std::uint32_t sad8x8_3 = (sad8x4(src + (srcStride << 3) + 8,
                                             srcStrideSub,
                                             ref + (refStride << 3) + 8 + searchIndex,
                                             refStrideSub)) << 1;
            if (sad8x8_3 < pBestSad8x8[3]) {
                pBestSad8x8[3] = sad8x8_3;
                xMv = (std::int16_t)(mvxt(mv) + (std::int16_t)searchIndex);
                yMv = mvyt(mv);
                pBestMv8x8[3] = ((std::uint32_t)yMv << 16) | ((std::uint16_t)xMv);
            }
            std::uint32_t sad16x16 = pEightSad16x16[start16x16Pos][searchIndex] = sad8x8_0 + sad8x8_1 + sad8x8_2 +
                sad8x8_3;
            if (sad16x16 < pBestSad16x16[0]) {
                pBestSad16x16[0] = sad16x16;
                xMv = (std::int16_t)(mvxt(mv) + (std::int16_t)searchIndex);
                yMv = mvyt(mv);
                pBestMv16x16[0] = ((std::uint32_t)yMv << 16) | ((std::uint16_t)xMv);
            }
        }
    } else {
        for (int searchIndex = 0; searchIndex < 8; searchIndex++) {
            std::uint32_t sad8x8_0 = sad8x8(src, srcStride, ref + searchIndex, refStride);
            if (sad8x8_0 < pBestSad8x8[0]) {
                pBestSad8x8[0] = sad8x8_0;
                xMv = (std::int16_t)(mvxt(mv) + (std::int16_t)searchIndex);
                yMv = mvyt(mv);
                pBestMv8x8[0] = ((std::uint32_t)yMv << 16) | ((std::uint16_t)xMv);
            }

            std::uint32_t sad8x8_1 = (sad8x8(src + 8, srcStride, ref + 8 + searchIndex, refStride));
            if (sad8x8_1 < pBestSad8x8[1]) {
                pBestSad8x8[1] = sad8x8_1;
                xMv = (std::int16_t)(mvxt(mv) + (std::int16_t)searchIndex);
                yMv = mvyt(mv);
                pBestMv8x8[1] = ((std::uint32_t)yMv << 16) | ((std::uint16_t)xMv);
            }

            std::uint32_t sad8x8_2 = (sad8x8(src + (srcStride << 3), srcStride,
                                             ref + (refStride << 3) + searchIndex, refStride));
            if (sad8x8_2 < pBestSad8x8[2]) {
                pBestSad8x8[2] = sad8x8_2;
                xMv = (std::int16_t)(mvxt(mv) + (std::int16_t)searchIndex);
                yMv = mvyt(mv);
                pBestMv8x8[2] = ((std::uint32_t)yMv << 16) | ((std::uint16_t)xMv);
            }

            std::uint32_t sad8x8_3 = (sad8x8(src + (srcStride << 3) + 8, srcStride,
                                             ref + (refStride << 3) + 8 + searchIndex, refStride));
            if (sad8x8_3 < pBestSad8x8[3]) {
                pBestSad8x8[3] = sad8x8_3;
                xMv = (std::int16_t)(mvxt(mv) + (std::int16_t)searchIndex);
                yMv = mvyt(mv);
                pBestMv8x8[3] = ((std::uint32_t)yMv << 16) | ((std::uint16_t)xMv);
            }
            std::uint32_t sad16x16 = pEightSad16x16[start16x16Pos][searchIndex] = sad8x8_0 + sad8x8_1 + sad8x8_2 +
                sad8x8_3;
            if (sad16x16 < pBestSad16x16[0]) {
                pBestSad16x16[0] = sad16x16;
                xMv = (std::int16_t)(mvxt(mv) + (std::int16_t)searchIndex);
                yMv = mvyt(mv);
                pBestMv16x16[0] = ((std::uint32_t)yMv << 16) | ((std::uint16_t)xMv);
            }
        }
    }
}

// motion_estimation.c:318-349 svt_ext_all_sad_calculation_8x8_16x16_c,
// verbatim.
void extAllSadCalculation8x8_16x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                                   std::uint32_t refStride, std::uint32_t mv, std::uint32_t* pBestSad8x8,
                                   std::uint32_t* pBestSad16x16, std::uint32_t* pBestMv8x8,
                                   std::uint32_t* pBestMv16x16, std::uint32_t pEightSad16x16[16][8],
                                   std::uint32_t pEightSad8x8[64][8], bool subSad) {
    static const char offsets[16] = {0, 1, 4, 5, 2, 3, 6, 7, 8, 9, 12, 13, 10, 11, 14, 15};
    //---- 16x16 : 0, 1, 4, 5, 2, 3, 6, 7, 8, 9, 12, 13, 10, 11, 14, 15
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            const std::uint32_t blockIndex = 16 * y * srcStride + 16 * x;
            const std::uint32_t searchPositionIndex = 16 * y * refStride + 16 * x;
            extEightSadCalculation8x8_16x16(src + blockIndex,
                                            srcStride,
                                            ref + searchPositionIndex,
                                            refStride,
                                            mv,
                                            (std::uint32_t)offsets[4 * y + x],
                                            pBestSad8x8,
                                            pBestSad16x16,
                                            pBestMv8x8,
                                            pBestMv16x16,
                                            pEightSad16x16,
                                            pEightSad8x8,
                                            subSad);
        }
    }
}

// motion_estimation.c:351-406 svt_ext_eight_sad_calculation_32x32_64x64_c,
// verbatim.
void extEightSadCalculation32x32_64x64(const std::uint32_t pSad16x16[16][8], std::uint32_t* pBestSad32x32,
                                       std::uint32_t* pBestSad64x64, std::uint32_t* pBestMv32x32,
                                       std::uint32_t* pBestMv64x64, std::uint32_t mv, std::uint32_t pSad32x32[4][8]) {
    for (int searchIndex = 0; searchIndex < 8; searchIndex++) {
        std::uint32_t sad32x32_0, sad32x32_1, sad32x32_2, sad32x32_3, sad64x64;

        pSad32x32[0][searchIndex] = sad32x32_0 = pSad16x16[0][searchIndex] + pSad16x16[1][searchIndex] +
            pSad16x16[2][searchIndex] + pSad16x16[3][searchIndex];
        if (sad32x32_0 < pBestSad32x32[0]) {
            pBestSad32x32[0] = sad32x32_0;
            const std::int16_t xMv = (std::int16_t)(mvxt(mv) + (std::int16_t)searchIndex);
            const std::int16_t yMv = mvyt(mv);
            pBestMv32x32[0] = ((std::uint32_t)yMv << 16) | ((std::uint16_t)xMv);
        }

        pSad32x32[1][searchIndex] = sad32x32_1 = pSad16x16[4][searchIndex] + pSad16x16[5][searchIndex] +
            pSad16x16[6][searchIndex] + pSad16x16[7][searchIndex];
        if (sad32x32_1 < pBestSad32x32[1]) {
            pBestSad32x32[1] = sad32x32_1;
            const std::int16_t xMv = (std::int16_t)(mvxt(mv) + (std::int16_t)searchIndex);
            const std::int16_t yMv = mvyt(mv);
            pBestMv32x32[1] = ((std::uint32_t)yMv << 16) | ((std::uint16_t)xMv);
        }

        pSad32x32[2][searchIndex] = sad32x32_2 = pSad16x16[8][searchIndex] + pSad16x16[9][searchIndex] +
            pSad16x16[10][searchIndex] + pSad16x16[11][searchIndex];
        if (sad32x32_2 < pBestSad32x32[2]) {
            pBestSad32x32[2] = sad32x32_2;
            const std::int16_t xMv = (std::int16_t)(mvxt(mv) + (std::int16_t)searchIndex);
            const std::int16_t yMv = mvyt(mv);
            pBestMv32x32[2] = ((std::uint32_t)yMv << 16) | ((std::uint16_t)xMv);
        }

        pSad32x32[3][searchIndex] = sad32x32_3 = pSad16x16[12][searchIndex] + pSad16x16[13][searchIndex] +
            pSad16x16[14][searchIndex] + pSad16x16[15][searchIndex];
        if (sad32x32_3 < pBestSad32x32[3]) {
            pBestSad32x32[3] = sad32x32_3;
            const std::int16_t xMv = (std::int16_t)(mvxt(mv) + (std::int16_t)searchIndex);
            const std::int16_t yMv = mvyt(mv);
            pBestMv32x32[3] = ((std::uint32_t)yMv << 16) | ((std::uint16_t)xMv);
        }

        sad64x64 = sad32x32_0 + sad32x32_1 + sad32x32_2 + sad32x32_3;
        if (sad64x64 < pBestSad64x64[0]) {
            pBestSad64x64[0] = sad64x64;
            const std::int16_t xMv = (std::int16_t)(mvxt(mv) + (std::int16_t)searchIndex);
            const std::int16_t yMv = mvyt(mv);
            pBestMv64x64[0] = ((std::uint32_t)yMv << 16) | ((std::uint16_t)xMv);
        }
    }
}

// av1me.c:162-188 svt_av1_init3smotion_compensation, verbatim.
void init3smotionCompensation(SearchSiteConfig* cfg, int stride) {
    int len;
    int ssCount = 1;

    cfg->ss[0].mv.as_int = 0;
    cfg->ss[0].offset = 0;

    for (len = kMaxFirstStep; len > 0; len /= 2) {
        // Generate offsets for 8 search sites per step (av1me.c:170-177
        // order).
        const Mv ssMvs[8] = {mvOf(0, (std::int16_t)-len),
                             mvOf(0, (std::int16_t)len),
                             mvOf((std::int16_t)-len, 0),
                             mvOf((std::int16_t)len, 0),
                             mvOf((std::int16_t)-len, (std::int16_t)-len),
                             mvOf((std::int16_t)len, (std::int16_t)-len),
                             mvOf((std::int16_t)-len, (std::int16_t)len),
                             mvOf((std::int16_t)len, (std::int16_t)len)};
        for (int i = 0; i < 8; ++i) {
            SearchSite* const ss = &cfg->ss[ssCount++];
            ss->mv = ssMvs[i];
            ss->offset = ss->mv.y * stride + ss->mv.x;
        }
    }

    cfg->ssCount = ssCount;
    cfg->searchesPerStep = 8;
}

// motion_estimation.c:2332-2334 z_to_raster, verbatim.
static const std::uint8_t kZToRaster[85] = {
    0,  1,  2,  3,  4,  5,  6,  9,  10, 7,  8,  11, 12, 13, 14, 17, 18, 15, 16, 19, 20, 21, 22, 29, 30, 23, 24, 31, 32,
    37, 38, 45, 46, 39, 40, 47, 48, 25, 26, 33, 34, 27, 28, 35, 36, 41, 42, 49, 50, 43, 44, 51, 52, 53, 54, 61, 62, 55,
    56, 63, 64, 69, 70, 77, 78, 71, 72, 79, 80, 57, 58, 65, 66, 59, 60, 67, 68, 73, 74, 81, 82, 75, 76, 83, 84};

// motion_estimation.c:2334-2443 construct_me_candidate_array_mrp_off,
// verbatim (the asserts are fixture-enumerated; the pcs walls threaded).
void constructMeCandidateArrayMrpOff(PcsCtx& pcs, MeCtx& meCtx, std::uint32_t numOfListToSearch) {
    // This function should only be called if there is one ref frame in each
    // list (the asserts are enumeration-tested).
    const std::uint8_t refPicIdx = 0;

    // Set whether the reference from each list is allowed
    std::uint8_t blkDoRefOrg[kMaxNumOfRefPicList];
    blkDoRefOrg[0] = meCtx.searchResults[0][0].doRef;
    blkDoRefOrg[1] = (numOfListToSearch == 1) ? 0 : meCtx.searchResults[1][0].doRef;

    if (numOfListToSearch < 2 || !meCtx.searchResults[1][0].doRef) {
        numOfListToSearch = 1;
    }
    const std::uint32_t mePruneTh =
        (blkDoRefOrg[0] && blkDoRefOrg[1]) ? (std::uint32_t)meCtx.pruneMeCandidatesTh : 0;

    // Set the count to 1 for all PUs using memset, which is faster than
    // setting at the end of each loop.  The count will only need to be
    // updated if both reference frames are allowed.
    const std::uint8_t numberOfPus = pcs.enableMe16x16
        ? (pcs.enableMe8x8 ? pcs.maxNumberOfPusPerSb : kMaxSb64PuCountNo8x8)
        : kMaxSb64PuCountWo16x16;
    for (int i = 0; i < numberOfPus; ++i) {
        pcs.sbResults->totalMeCandidateIndex[i] = 1;
    }

    for (std::uint8_t nIdx = 0; nIdx < pcs.maxNumberOfPusPerSb; ++nIdx) {
        const std::uint8_t puIndex = kZToRaster[nIdx];
        std::uint8_t meCandOffset = 0;

        const std::uint8_t useMePu = pcs.enableMe16x16
            ? (std::uint8_t)(pcs.enableMe8x8 || nIdx < kMaxSb64PuCountNo8x8)
            : (std::uint8_t)(nIdx < kMaxSb64PuCountWo16x16);
        MeCandidate* meCandidateArray = nullptr;
        if (useMePu) {
            meCandidateArray = &pcs.sbResults->meCandidateArray[puIndex * pcs.maxCand];
        }
        std::uint8_t blkDoRef[kMaxNumOfRefPicList] = {blkDoRefOrg[0], blkDoRefOrg[1]};
        const std::uint32_t bestMeDist =
            (blkDoRefOrg[0] && blkDoRefOrg[1])
                ? (meCtx.pSbBestSad[0][0][nIdx] < meCtx.pSbBestSad[1][0][nIdx] ? meCtx.pSbBestSad[0][0][nIdx]
                                                                              : meCtx.pSbBestSad[1][0][nIdx])
                : (blkDoRefOrg[0] ? meCtx.pSbBestSad[0][0][nIdx] : meCtx.pSbBestSad[1][0][nIdx]);

        meCtx.meDistortion[puIndex] = bestMeDist;
        std::int8_t minDistList = -1;
        // If both refs have a candidate, use only the best one for unipred
        if (meCtx.useBestUnipredCandOnly && blkDoRef[0] && blkDoRef[1]) {
            minDistList = meCtx.pSbBestSad[0][0][nIdx] < meCtx.pSbBestSad[1][0][nIdx] ? 0 : 1;
        }
        // Unipred candidates
        for (int listIndex = 0;
             (std::uint32_t)listIndex < numOfListToSearch && (useMePu || meCandOffset == 0); ++listIndex) {
            // ME was skipped, so do not add this Unipred candidate
            if (blkDoRef[listIndex] == 0) {
                continue;
            }

            if (mePruneTh > 0) {
                const std::uint32_t currentToBestDistDistance =
                    (meCtx.pSbBestSad[listIndex][0][nIdx] - bestMeDist) * 100;
                if (currentToBestDistDistance > (bestMeDist * mePruneTh)) {
                    blkDoRef[listIndex] = 0;
                    continue;
                }
            }
            if (minDistList != -1 && minDistList != listIndex) {
                // Need to save the MV in case bipred is injected
                if (useMePu) {
                    pcs.sbResults
                        ->meMvArray[puIndex * pcs.maxRefs + (listIndex ? pcs.maxL0 : 0) + refPicIdx]
                        .as_int = meCtx.pSbBestMv[listIndex][0][nIdx];
                }
                continue;
            }
            if (useMePu) {
                meCandidateArray[meCandOffset].direction = (std::uint8_t)listIndex;
                meCandidateArray[meCandOffset].refIdxL0 = refPicIdx;
                meCandidateArray[meCandOffset].refIdxL1 = refPicIdx;
                meCandidateArray[meCandOffset].ref0List =
                    (std::uint8_t)(listIndex == 0 ? listIndex : 24);
                meCandidateArray[meCandOffset].ref1List =
                    (std::uint8_t)(listIndex == 1 ? listIndex : 24);

                pcs.sbResults
                    ->meMvArray[puIndex * pcs.maxRefs + (listIndex ? pcs.maxL0 : 0) + refPicIdx]
                    .as_int = meCtx.pSbBestMv[listIndex][0][nIdx];
            }

            meCandOffset++;
        }

        // Can have up to one bipred cand (LAST ,BWD)
        if (blkDoRef[0] && blkDoRef[1] && useMePu) {
            // If get here, will have 3 candidates, since both unipred
            // directions are valid
            meCandidateArray[meCandOffset].direction = 2;  // BI_PRED
            meCandidateArray[meCandOffset].refIdxL0 = refPicIdx;
            meCandidateArray[meCandOffset].refIdxL1 = refPicIdx;
            meCandidateArray[meCandOffset].ref0List = 0;  // REFERENCE_PIC_LIST_0
            meCandidateArray[meCandOffset].ref1List = 1;  // REFERENCE_PIC_LIST_1

            // store total me candidate count
            pcs.sbResults->totalMeCandidateIndex[puIndex] = (std::uint8_t)(meCandOffset + 1);
        }
    }
}

// motion_estimation.c:2445-2496 construct_me_candidate_array_single_ref,
// verbatim.
void constructMeCandidateArraySingleRef(PcsCtx& pcs, MeCtx& meCtx, std::uint32_t numOfListToSearch) {
    const std::uint8_t refPicIdx = 0;

    // Set whether the reference from each list is allowed
    const std::uint8_t blkDoRef = meCtx.searchResults[0][0].doRef;

    if (numOfListToSearch < 2 || !meCtx.searchResults[1][0].doRef) {
        numOfListToSearch = 1;
    }

    const std::uint8_t numberOfPus = pcs.enableMe16x16
        ? (pcs.enableMe8x8 ? pcs.maxNumberOfPusPerSb : kMaxSb64PuCountNo8x8)
        : kMaxSb64PuCountWo16x16;
    for (int i = 0; i < numberOfPus; ++i) {
        pcs.sbResults->totalMeCandidateIndex[i] = 1;
    }

    for (std::uint8_t nIdx = 0; nIdx < pcs.maxNumberOfPusPerSb; ++nIdx) {
        const std::uint8_t puIndex = kZToRaster[nIdx];

        const std::uint8_t useMePu = pcs.enableMe16x16
            ? (std::uint8_t)(pcs.enableMe8x8 || nIdx < kMaxSb64PuCountNo8x8)
            : (std::uint8_t)(nIdx < kMaxSb64PuCountWo16x16);
        MeCandidate* meCandidateArray = nullptr;
        if (useMePu) {
            meCandidateArray = &pcs.sbResults->meCandidateArray[puIndex * pcs.maxCand];
        }
        meCtx.meDistortion[puIndex] = meCtx.pSbBestSad[0][0][nIdx];

        // ME was skipped, so do not add this Unipred candidate
        if (blkDoRef == 0) {
            continue;
        }

        if (useMePu) {
            meCandidateArray[0].direction = 0;  // REF_LIST_0
            meCandidateArray[0].refIdxL0 = refPicIdx;
            meCandidateArray[0].refIdxL1 = refPicIdx;
            meCandidateArray[0].ref0List = 0;
            meCandidateArray[0].ref1List = 0;

            pcs.sbResults->meMvArray[puIndex * pcs.maxRefs + refPicIdx].as_int =
                meCtx.pSbBestMv[0][0][nIdx];
        }
    }
}

// motion_estimation.c:2498-2634 construct_me_candidate_array, verbatim
// (the pcs->scs->mrp_ctrls.only_l_bwd wall threaded as PcsCtx::onlyLBwd).
void constructMeCandidateArray(PcsCtx& pcs, MeCtx& meCtx, std::uint32_t numOfListToSearch) {
    for (std::uint32_t nIdx = 0; nIdx < pcs.maxNumberOfPusPerSb; ++nIdx) {
        std::uint8_t puIndex = (nIdx > 4) ? kZToRaster[nIdx] : (std::uint8_t)nIdx;
        std::uint8_t meCandOffset = 0;

        const std::uint8_t useMePu = pcs.enableMe16x16
            ? (std::uint8_t)(pcs.enableMe8x8 || nIdx < kMaxSb64PuCountNo8x8)
            : (std::uint8_t)(nIdx < kMaxSb64PuCountWo16x16);
        MeCandidate* meCandidateArray = nullptr;
        if (useMePu) {
            meCandidateArray = &pcs.sbResults->meCandidateArray[puIndex * pcs.maxCand];
        }
        std::uint8_t blkDoRef[kMaxNumOfRefPicList][kMaxRefIdx];
        std::uint32_t currentToBestDistDistance;
        const std::uint32_t mePruneTh = (std::uint32_t)meCtx.pruneMeCandidatesTh;  // to change to 32bit
        std::uint32_t bestMeDist = (std::uint32_t)~0;

        // Determine the best ME distortion
        for (std::uint32_t listIndex = 0; listIndex < numOfListToSearch; listIndex++) {
            const std::uint8_t numOfRefPicToSearch = meCtx.numOfRefPicToSearch[listIndex];
            for (std::uint32_t refPic = 0; refPic < numOfRefPicToSearch; refPic++) {
                blkDoRef[listIndex][refPic] = meCtx.searchResults[listIndex][refPic].doRef;
                if (blkDoRef[listIndex][refPic] == 0) {
                    continue;
                }

                bestMeDist = meCtx.pSbBestSad[listIndex][refPic][nIdx] < bestMeDist
                    ? meCtx.pSbBestSad[listIndex][refPic][nIdx]
                    : bestMeDist;
            }
        }

        meCtx.meDistortion[puIndex] = bestMeDist;
        // Unipred candidates
        for (std::uint32_t listIndex = 0;
             listIndex < numOfListToSearch && (useMePu || meCandOffset == 0); ++listIndex) {
            const std::uint8_t numOfRefPicToSearch = meCtx.numOfRefPicToSearch[listIndex];

            for (std::uint32_t refPicIndex = 0;
                 (refPicIndex < numOfRefPicToSearch) && (useMePu || (meCandOffset == 0)); ++refPicIndex) {
                // ME was skipped, so do not add this Unipred candidate
                if (blkDoRef[listIndex][refPicIndex] == 0) {
                    continue;
                }

                if (mePruneTh > 0) {
                    currentToBestDistDistance =
                        (meCtx.pSbBestSad[listIndex][refPicIndex][nIdx] - bestMeDist) * 100;
                    if (currentToBestDistDistance > (bestMeDist * mePruneTh)) {
                        blkDoRef[listIndex][refPicIndex] = 0;
                        continue;
                    }
                }
                if (useMePu) {
                    meCandidateArray[meCandOffset].direction = (std::uint8_t)listIndex;
                    meCandidateArray[meCandOffset].refIdxL0 = (std::uint8_t)refPicIndex;
                    meCandidateArray[meCandOffset].refIdxL1 = (std::uint8_t)refPicIndex;
                    meCandidateArray[meCandOffset].ref0List =
                        (std::uint8_t)(listIndex == 0 ? listIndex : 24);
                    meCandidateArray[meCandOffset].ref1List =
                        (std::uint8_t)(listIndex == 1 ? listIndex : 24);

                    pcs.sbResults
                        ->meMvArray[puIndex * pcs.maxRefs +
                                    (listIndex ? pcs.maxL0 : 0) + refPicIndex]
                        .as_int = meCtx.pSbBestMv[listIndex][refPicIndex][nIdx];
                }
                meCandOffset++;
            }
        }
        if (numOfListToSearch == 2 && useMePu) {
            // 1st set of BIPRED cand
            // (LAST ,BWD), (LAST,ALT ), (LAST,ALT2 )
            // (LAST2,BWD), (LAST2,ALT), (LAST2,ALT2)
            // (LAST3,BWD), (LAST3,ALT), (LAST3,ALT2)
            // (GOLD ,BWD), (GOLD,ALT ), (GOLD,ALT2 )
            for (std::uint32_t firstListRefPictIdx = 0;
                 firstListRefPictIdx < meCtx.numOfRefPicToSearch[0];
                 firstListRefPictIdx++) {
                for (std::uint32_t secondListRefPictIdx = 0;
                     secondListRefPictIdx < meCtx.numOfRefPicToSearch[1];
                     secondListRefPictIdx++) {
                    if (pcs.onlyLBwd && (firstListRefPictIdx > 0 || secondListRefPictIdx > 0)) {
                        continue;
                    }
                    if (blkDoRef[0][firstListRefPictIdx] && blkDoRef[1][secondListRefPictIdx]) {
                        meCandidateArray[meCandOffset].direction = 2;  // BI_PRED
                        meCandidateArray[meCandOffset].refIdxL0 = (std::uint8_t)firstListRefPictIdx;
                        meCandidateArray[meCandOffset].refIdxL1 = (std::uint8_t)secondListRefPictIdx;
                        meCandidateArray[meCandOffset].ref0List = 0;  // REFERENCE_PIC_LIST_0
                        meCandidateArray[meCandOffset].ref1List = 1;  // REFERENCE_PIC_LIST_1
                        meCandOffset++;
                    }
                }
            }
            if (!pcs.onlyLBwd) {
                // 2nd set of BIPRED cand: (LAST,LAST2) (LAST,LAST3) (LAST,GOLD)
                for (std::uint32_t firstListRefPictIdx = 1;
                     firstListRefPictIdx < meCtx.numOfRefPicToSearch[0];
                     firstListRefPictIdx++) {
                    if (blkDoRef[0][0] && blkDoRef[0][firstListRefPictIdx]) {
                        meCandidateArray[meCandOffset].direction = 2;  // BI_PRED
                        meCandidateArray[meCandOffset].refIdxL0 = 0;
                        meCandidateArray[meCandOffset].refIdxL1 = (std::uint8_t)firstListRefPictIdx;
                        meCandidateArray[meCandOffset].ref0List = 0;  // REFERENCE_PIC_LIST_0
                        meCandidateArray[meCandOffset].ref1List = 0;  // REFERENCE_PIC_LIST_0
                        meCandOffset++;
                    }
                }
            }

            // 3rd set of BIPRED cand: (BWD, ALT)
            if (!pcs.onlyLBwd) {
                if (meCtx.numOfRefPicToSearch[1] == 3 && blkDoRef[1][0] && blkDoRef[1][2]) {
                    {
                        meCandidateArray[meCandOffset].direction = 2;  // BI_PRED
                        meCandidateArray[meCandOffset].refIdxL0 = 0;
                        meCandidateArray[meCandOffset].refIdxL1 = 2;
                        meCandidateArray[meCandOffset].ref0List = 1;  // REFERENCE_PIC_LIST_1
                        meCandidateArray[meCandOffset].ref1List = 1;  // REFERENCE_PIC_LIST_1
                        meCandOffset++;
                    }
                }
            }
        }

        // store total me candidate count
        if (useMePu) {
            pcs.sbResults->totalMeCandidateIndex[puIndex] = meCandOffset;
        }
    }
}
}  // namespace motion
