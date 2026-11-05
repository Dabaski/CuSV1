#include "global_motion.h"

#include <cmath>
#include <functional>

namespace motion {

namespace {

// Clamp as in the reference's utility.h clamp.
template <typename T>
T clamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

// global_motion.c:36-49 convert_to_params. Params -> the 6-entry integer
// model with the per-term clamps and decode factors.
void convertToParams(const double* params, std::int32_t* model) {
    model[0] = static_cast<std::int32_t>(params[0] * (1 << kGmTransPrecBits) + 0.5);
    model[1] = static_cast<std::int32_t>(params[1] * (1 << kGmTransPrecBits) + 0.5);
    model[0] = clamp(model[0], kGmTransMin, kGmTransMax) * kGmTransDecodeFactor;
    model[1] = clamp(model[1], kGmTransMin, kGmTransMax) * kGmTransDecodeFactor;
    for (int i = 2; i < 6; ++i) {
        const std::int32_t diagValue = ((i == 2 || i == 5) ? (1 << kGmAlphaPrecBits) : 0);
        model[i] = static_cast<std::int32_t>(params[i] * (1 << kGmAlphaPrecBits) + 0.5);
        model[i] = clamp(model[i] - diagValue, kGmAlphaMin, kGmAlphaMax);
        model[i] = (model[i] + diagValue) * kGmAlphaDecodeFactor;
    }
}

// global_motion.c:63-95 add_param_offset. param_type 0 = translation
// (index < 2), 1 = affine (alpha). is_one_centered for the diagonal terms
// (index 2 and 5). All arithmetic verbatim from the reference.
std::int32_t addParamOffset(int paramIndex, std::int32_t paramValue, std::int32_t offset) {
    const int scaleVals[2] = {kGmTransPrecDiff, kGmAlphaPrecDiff};
    const int clampVals[2] = {kGmTransMax, kGmAlphaMax};
    // type of param: 0 - translation, 1 - affine
    const int paramType      = (paramIndex < 2 ? 0 : 1);
    const int isOneCentered  = (paramIndex == 2 || paramIndex == 5);

    // Make parameter zero-centered and offset the shift that was done to make
    // it compatible with the warped model
    paramValue = (paramValue - (isOneCentered << kWarpedModelPrecBits)) >> scaleVals[paramType];
    // Add desired offset to the rescaled/zero-centered parameter
    paramValue += offset;
    // Clamp the parameter so it does not overflow the number of bits allotted
    // to it in the bitstream
    paramValue = static_cast<std::int32_t>(
        clamp(paramValue, -clampVals[paramType], clampVals[paramType]));
    // Rescale the parameter to WARPEDMODEL_PRECISION_BITS so it is compatible
    // with the warped motion library
    paramValue *= (1 << scaleVals[paramType]);
    // Undo the zero-centering step if necessary
    return paramValue + (isOneCentered << kWarpedModelPrecBits);
}

// global_motion.c:27-28 the erroradvantage thresholds.
constexpr double kErroradvTr[] = {0.65, 0.50, 0.45};
constexpr double kErroradvProdTr[] = {20000, 15000, 14000};

// global_motion.c:30-33 is_enough_erroradvantage. Both gates must hold.
bool isEnoughErroradvantage(double bestErroradvantage, int paramsCost, int erroradvType) {
    return bestErroradvantage < kErroradvTr[erroradvType] &&
        bestErroradvantage * paramsCost < kErroradvProdTr[erroradvType];
}

// global_motion.c:97-113 force_wmtype. The fallthrough chain zeroes terms
// below the forced type: IDENTITY clears [0..1] and pins the diagonal,
// TRANSLATION also pins [3], ROTZOOM mirrors [4]/[5] off [2]/[3], AFFINE
// touches nothing but the stored type.
void forceWmType(std::int32_t* wmmat, WmType wmtype) {
    switch (wmtype) {
    case WmTypeIdentity:
        wmmat[0] = 0;
        wmmat[1] = 0;
        [[fallthrough]];
    case WmTypeTranslation:
        wmmat[2] = 1 << kWarpedModelPrecBits;
        wmmat[3] = 0;
        [[fallthrough]];
    case WmTypeRotzoom:
        wmmat[4] = -wmmat[3];
        wmmat[5] = wmmat[2];
        [[fallthrough]];
    case WmTypeAffine:
        break;
    }
}

// global_motion.c:51-61 get_wmtype. The gates: if [5] == [2] == 1<<16 and
// [4] == [3] == 0, it is identity when [0] == [1] == 0, else translation.
// Else if [2] == [5] and [4] == -[3], rotzoom. Else affine.
WmType getWmtype(const std::int32_t* wmmat) {
    if (wmmat[5] == (1 << kWarpedModelPrecBits) && !wmmat[4] &&
        wmmat[2] == (1 << kWarpedModelPrecBits) && !wmmat[3]) {
        return (!wmmat[0] && !wmmat[1]) ? WmTypeIdentity : WmTypeTranslation;
    }
    if (wmmat[2] == wmmat[5] && wmmat[4] == -wmmat[3]) {
        return WmTypeRotzoom;
    }
    return WmTypeAffine;
}

// global_motion.c:117-209 svt_av1_refine_integerized_param, de-walled:
// GmControls->rfn_early_exit and pic_sad/params_cost arrive as scalars, and
// svt_av1_warp_error is injected as an error function over the live wmmat.
// max_trans_model_params[TRANS_TYPES] = {0, 2, 4, 6} (global_motion.c:121).
// GM_ERRORADV_TR_1 is the early-exit threshold index 1 in erroradv_tr.
std::int64_t refineIntegerizedParam(std::int32_t* wmmat, WmType wmtype, int nRefinements,
                                    const std::function<std::int64_t(const std::int32_t*)>& errFn,
                                    std::int64_t bestFrameError, bool rfnEarlyExit, double picSad,
                                    int paramsCost) {
    static const int maxTransModelParams[] = {0, 2, 4, 6};
    std::int64_t bestError;
    forceWmType(wmmat, wmtype);
    bestError = errFn(wmmat);
    if (bestError > bestFrameError) {
        bestError = bestFrameError;
    }
    if (rfnEarlyExit && !isEnoughErroradvantage((double)bestError / picSad, paramsCost, 1)) {
        return bestError;
    }
    std::int32_t step = 1 << (5 - 1);  // initial step = 16
    for (int i = 0; i < nRefinements; i++, step >>= 1) {
        for (int p = 0; p < maxTransModelParams[wmtype]; ++p) {
            // Skip searches for parameters that are forced to be 0.
            std::int32_t* param = wmmat + p;
            std::int32_t currParam = *param;
            std::int32_t bestParam = currParam;
            // Look to the left.
            *param = addParamOffset(p, currParam, -step);
            std::int64_t stepError = errFn(wmmat);
            if (stepError < bestError) {
                bestError = stepError;
                bestParam = *param;
            }
            // Look to the right.
            *param = addParamOffset(p, currParam, step);
            stepError = errFn(wmmat);
            if (stepError < bestError) {
                bestError = stepError;
                bestParam = *param;
            }
            *param = bestParam;
        }
    }
    forceWmType(wmmat, wmtype);
    return bestError;
}

}  // namespace motion
