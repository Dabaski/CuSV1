#pragma once

#include <cstdint>
#include <functional>

namespace motion {

// global_motion.c port (A3 / HM1). The pure math surface only: parameter
// quantization, type classification, integer refinement scaffolding and
// the erroradvantage gate. The FramePoints/PCS-walled drivers
// (correspondence_from_corners :213-237, correspondence_from_mvs :239-337,
// gm_compute_correspondence :341-361, determine_gm_params :363-368) and
// the warp_error refinement body (:117-209) are PARKED.

// definitions.h:1692, 1722-1738 - the GM precision constants (verbatim).
constexpr int kWarpedModelPrecBits = 16;
constexpr int kGmTransPrecBits = 6;
constexpr int kGmAbsTransBits = 12;
constexpr int kGmAbsTransOnlyBits = kGmAbsTransBits - kGmTransPrecBits + 3;
constexpr int kGmTransPrecDiff = kWarpedModelPrecBits - kGmTransPrecBits;
constexpr int kGmTransDecodeFactor = 1 << kGmTransPrecDiff;
constexpr int kGmAlphaPrecBits = 15;
constexpr int kGmAbsAlphaBits = 12;

// global_motion.c:51-61 get_wmtype. The 1:1 enum order follows
// definitions.h:1742-1745 (IDENTITY = 0 ... AFFINE = 3).
enum WmType { WmTypeIdentity = 0, WmTypeTranslation = 1, WmTypeRotzoom = 2, WmTypeAffine = 3 };

// global_motion.c:51-61 get_wmtype - classify the integer wmmat[6] into
// IDENTITY / TRANSLATION / ROTZOOM / AFFINE per the reference gates.
WmType getWmtype(const std::int32_t* wmmat);
constexpr int kGmAlphaPrecDiff = kWarpedModelPrecBits - kGmAlphaPrecBits;
constexpr int kGmAlphaDecodeFactor = 1 << kGmAlphaPrecDiff;
constexpr int kGmTransMax = 1 << kGmAbsTransBits;
constexpr int kGmAlphaMax = 1 << kGmAbsAlphaBits;
constexpr int kGmTransMin = -kGmTransMax;
constexpr int kGmAlphaMin = -kGmAlphaMax;

// global_motion.c:36-49 convert_to_params. Params -> the 6-entry integer
// model with the per-term clamps and decode factors.
void convertToParams(const double* params, std::int32_t* model);

// global_motion.c:63-95 add_param_offset - add an offset to an already
// decoded (wmmat-scale) parameter: zero-center, rescale to the param's own
// precision, clamp, re-rescale, undo the centering.
std::int32_t addParamOffset(int paramIndex, std::int32_t paramValue, std::int32_t offset);

// global_motion.c:97-113 force_wmtype - the fallthrough chain that zeroes
// terms below the forced type and pins/mirrors the diagonal terms.
void forceWmType(std::int32_t* wmmat, WmType wmtype);

// global_motion.c:30-33 is_enough_erroradvantage - the acceptance gate:
// ratio < tr[type] AND ratio * cost < prod_tr[type] must both hold.
bool isEnoughErroradvantage(double bestErroradvantage, int paramsCost, int erroradvType);

// global_motion.c:117-209 svt_av1_refine_integerized_param, de-walled:
// GmControls->rfn_early_exit and pic_sad/params_cost arrive as scalars and
// svt_av1_warp_error is injected as an error function over the live wmmat.
// Named deviations: the wmtype write-back (wm->wmtype = get_wmtype(wm)) is
// the caller's job, since the de-walled matrix is a plain array.
std::int64_t refineIntegerizedParam(std::int32_t* wmmat, WmType wmtype, int nRefinements,
                                    const std::function<std::int64_t(const std::int32_t*)>& errFn,
                                    std::int64_t bestFrameError, bool rfnEarlyExit, double picSad,
                                    int paramsCost);

}  // namespace motion
