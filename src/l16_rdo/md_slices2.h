// l16_rdo A4M - the mode_decision.c REMAINDER surface (the spans MD1 did not
// land). Reference: third_party/SVT-AV1/Source/Lib/Codec/mode_decision.c
// (4,490 physical lines). Each span carries the ModeDecisionContext/pcs walls
// as carrier structs with the reference field names (the RE8A DeblockContext
// precedent); bodies verbatim. Struct-free, /W4 /WX clean, ASCII-only.
//
// Span map (grow per commit):
//   A4M-1  mv_check_bounds                        mode_decision.c:3000-3003
//   A4M-1  derive_ssim_threshold_factor_for_full_md  :3875-3877
#pragma once

#include <cstdint>

#include "rate_est.h"
#include "mode_config.h"

namespace l16_rdo {

// The ResolutionRange enumerators live in mode_config; hoist them so the
// md_slices2 surface spells the reference names at l16_rdo scope.
using mode_config::ResolutionRange;
using mode_config::INPUT_SIZE_240p_RANGE;
using mode_config::INPUT_SIZE_360p_RANGE;
using mode_config::INPUT_SIZE_480p_RANGE;
using mode_config::INPUT_SIZE_720p_RANGE;
using mode_config::INPUT_SIZE_1080p_RANGE;
using mode_config::INPUT_SIZE_4K_RANGE;
using mode_config::INPUT_SIZE_8K_RANGE;

// ---------------------------------------------------------------------------
// Carriers (the RE8A DeblockContext precedent): the consumed reference
// structs, mirrored field-by-field.
// ---------------------------------------------------------------------------

// Mv (mv.h:42-49, verbatim layout). MSVC C4201 fires on the anonymous struct
// under /WX; the amvp.h:231-240 twin carries the same pragma.
#pragma warning(push)
#pragma warning(disable : 4201)
union Mv {
    struct {
        std::int16_t x;
        std::int16_t y;
    };
    std::uint32_t as_int;  // faster equality tests and copies
};
#pragma warning(pop)

// MvLimits (definitions.h:278-283, verbatim).
struct MvLimits {
    std::int32_t col_min;
    std::int32_t col_max;
    std::int32_t row_min;
    std::int32_t row_max;
};

// ---------------------------------------------------------------------------
// A4M-1: mv_check_bounds (mode_decision.c:3000-3003). The scs/ctx walls
// thread as the plain pointers (the INLINE helper takes exactly these).
// ---------------------------------------------------------------------------
std::int32_t mvCheckBounds(const MvLimits* mv_limits, const Mv* mv);

// A4M-1: derive_ssim_threshold_factor_for_full_md (mode_decision.c:3875-3877).
// The scs wall threads as the input-resolution enum it reads.
double deriveSsimThresholdFactorForFullMd(ResolutionRange inputResolution);

}  // namespace l16_rdo
