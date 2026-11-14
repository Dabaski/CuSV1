// l16_rdo A4M - the mode_decision.c remainder surface.
// GREEN step lands per nano-slice; see md_slices2.h for the span map. Every
// body is a verbatim transcription of its cited reference span with the
// ModeDecisionContext/pcs walls threaded as carrier structs.
#include "md_slices2.h"

namespace l16_rdo {

// mv_check_bounds (mode_decision.c:3000-3003, verbatim).
std::int32_t mvCheckBounds(const MvLimits* mv_limits, const Mv* mv) {
    return (mv->y >> 3) < mv_limits->row_min || (mv->y >> 3) > mv_limits->row_max ||
        (mv->x >> 3) < mv_limits->col_min || (mv->x >> 3) > mv_limits->col_max;
}

// derive_ssim_threshold_factor_for_full_md (mode_decision.c:3875-3877,
// verbatim; the scs->input_resolution wall threads as the enum parameter).
double deriveSsimThresholdFactorForFullMd(ResolutionRange inputResolution) {
    return inputResolution >= INPUT_SIZE_1080p_RANGE ? 1.02 : 1.03;
}

}  // namespace l16_rdo
