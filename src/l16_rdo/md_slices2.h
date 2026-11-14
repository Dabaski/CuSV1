// l16_rdo A4M - the mode_decision.c REMAINDER surface (the spans MD1 did not
// land). Reference: third_party/SVT-AV1/Source/Lib/Codec/mode_decision.c
// (4,490 physical lines). Each span carries the ModeDecisionContext/pcs walls
// as carrier structs with the reference field names (the RE8A DeblockContext
// precedent); bodies verbatim. Struct-free, /W4 /WX clean, ASCII-only.
//
// Span map (grow per commit):
//   A4M-1  mv_check_bounds                        mode_decision.c:3000-3003
//   A4M-1  derive_ssim_threshold_factor_for_full_md  :3875-3877
//   A4M-2  av1_set_ref_frame (carry)              inter_prediction.h:487-496
//   A4M-2  valid_ref_frame_type                   mode_decision.c:3489-3504
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

// ---------------------------------------------------------------------------
// A4M-2: the reference-frame type surface. The landed rate_est.h spells
// INTRA_FRAME as a macro (= 0) and the MvRefFrameExt enum as MVX_*; the
// reference names here carry the _MV suffix (the MD-side int8 MvReferenceFrame
// domain, definitions.h:1357-1386 + :1593).
// ---------------------------------------------------------------------------
typedef std::int8_t MvReferenceFrame;  // definitions.h:1593

// definitions.h:1357-1386 + :1389 (REFS_PER_FRAME = 7) + :1377 + :1419.
constexpr MvReferenceFrame NONE_FRAME_MV = -1;
constexpr MvReferenceFrame INTRA_FRAME_MV = 0;
constexpr MvReferenceFrame LAST_FRAME_MV = 1;
constexpr MvReferenceFrame LAST2_FRAME_MV = 2;
constexpr MvReferenceFrame LAST3_FRAME_MV = 3;
constexpr MvReferenceFrame GOLDEN_FRAME_MV = 4;
constexpr MvReferenceFrame BWDREF_FRAME_MV = 5;
constexpr MvReferenceFrame ALTREF2_FRAME_MV = 6;
constexpr MvReferenceFrame ALTREF_FRAME_MV = 7;
constexpr int REF_FRAMES_MV = 8;             // definitions.h:1384 LAST_BWD_FRAME
constexpr int TOTAL_REFS_PER_FRAME_MV = 8;   // definitions.h:1377
constexpr int FWD_REFS_MV = 4;               // definitions.h:1376
constexpr int BWD_REFS_MV = 3;               // definitions.h:1378
constexpr int TOTAL_UNIDIR_COMP_REFS_MV = 9;  // definitions.h:1367
constexpr int TOTAL_COMP_REFS_MV = FWD_REFS_MV * BWD_REFS_MV + TOTAL_UNIDIR_COMP_REFS_MV;  // :1382 = 21
constexpr int MODE_CTX_REF_FRAMES_MV = TOTAL_REFS_PER_FRAME_MV + TOTAL_COMP_REFS_MV;       // :1419 = 29

// av1_set_ref_frame (inter_prediction.h:487-496). Module-boundary carry
// (l16_rdo cannot link l5_motion; the l7-l13 duplication precedent): the
// landed twin is l5 amvp.cpp:183; the ref_frame_map row set is
// inter_prediction.h:461-485.
void av1SetRefFrame(MvReferenceFrame* rf, std::int8_t refFrameType);

// valid_ref_frame_type (mode_decision.c:3489-3504): the pcs wall threads as
// the ref_frame_type_arr pointer + count (md_process.h:944). Deviation,
// named: the reference's rf parameter is non-const and never written - the
// port takes it const.
bool validRefFrameType(const MvReferenceFrame rf[2], const MvReferenceFrame* refFrameTypeArr,
                       std::uint8_t totRefFrameTypes);

}  // namespace l16_rdo
