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

// ---------------------------------------------------------------------------
// A4M-3: the compound-mode surface.
// ---------------------------------------------------------------------------

// MD_COMP_TYPE (definitions.h:1264-1270, verbatim order).
enum MdCompType {
    MD_COMP_AVG,
    MD_COMP_DIST,
    MD_COMP_DIFF0,
    MD_COMP_WEDGE,
    MD_COMP_TYPES,
};

// COMPOUND_TYPE (definitions.h:1238-1245, verbatim order).
enum CompoundType {
    COMPOUND_AVERAGE,
    COMPOUND_DISTWTD,
    COMPOUND_WEDGE,
    COMPOUND_DIFFWTD,
    COMPOUND_TYPES,
    MASKED_COMPOUND_TYPES = 2,
};

// MIN (the aom macro as a function; the l16 surface cannot spell the macro
// without clashing with the landed rate_est.h defines).
inline int aomMin(int a, int b) { return a < b ? a : b; }

// svt_aom_get_wedge_params_bits (inter_prediction.c:2064-2066) as the bits
// column of wedge_params_lookup (:2001-2024). Module-boundary carry (the
// landed twin is l17 inter_prediction.cpp:390 over its full lookup); the
// rows follow the rate_est.h BlockSize order.
extern const std::int32_t wedgeParamsBits[BLOCK_SIZES_ALL];

// get_tot_comp_types_bsize (mode_decision.c:124-126): the
// CONFIG_ENABLE_INTER_COMPOUND guard is LIVE (= 1, EbConfigMacros.h
// non-RTC block - the inter_symbols.h:18-22 finding), so the body is kept.
std::int32_t getTotCompTypesBsize(std::int32_t totCompTypes, l16_rdo::BlockSize bsize);

// The written BlockModeInfo fields of determine_compound_mode
// (mode_decision.c:515-552). The svt_aom_search_compound_diff_wedge wall
// (enc_inter_prediction.c:3718) threads as the called flag - its candidate
// mutation stays parked with the wall (named).
struct CompoundModeCarrier {
    CompoundType interinterCompType;  // block_mi.interinter_comp.type :522
    std::int32_t compGroupIdx;        // block_mi.comp_group_idx :525/:529/:533/:541
    std::int32_t compoundIdx;         // block_mi.compound_idx :526/:530/:534/:542
    std::int32_t maskType;            // block_mi.interinter_comp.mask_type :535
    bool searchDiffWedgeCalled;       // the :537/:544 wall call
};

// determine_compound_mode (mode_decision.c:515-552) over to_av1_compound_lut
// (:513). Deviation, named: the pcs/ctx/cand walls thread as the carrier +
// flag; the SVT_ERROR/assert(0) default arm is NDEBUG-dropped convention.
void determineCompoundMode(CompoundModeCarrier* blockMi, MdCompType curType);

// ---------------------------------------------------------------------------
// A4M-4: the OBMC motion-mode surface.
// ---------------------------------------------------------------------------

// TransformationType (definitions.h:1740-1747, verbatim order). The
// _TT suffix avoids the landed rate_est.h macro space.
enum TransformationType {
    IDENTITY_TT = 0,
    TRANSLATION_TT = 1,
    ROTZOOM_TT = 2,
    AFFINE_TT = 3,
    TRANS_TYPES_TT = 4,
};

// PredictionMode suffix aliases for the md_slices2 tests/surfaces (the
// landed rate_est.h enum PredictionMode owns the plain names; the
// inter-mode constants are the same values).
constexpr PredictionMode DC_PRED_PM = DC_PRED;
constexpr PredictionMode NEARESTMV_PM = NEARESTMV;
constexpr PredictionMode NEARMV_PM = NEARMV;
constexpr PredictionMode GLOBALMV_PM = GLOBALMV;
constexpr PredictionMode NEWMV_PM = NEWMV;
constexpr PredictionMode NEAREST_NEARESTMV_PM = NEAREST_NEARESTMV;
constexpr PredictionMode NEAR_NEARMV_PM = NEAR_NEARMV;
constexpr PredictionMode NEAREST_NEWMV_PM = NEAREST_NEWMV;
constexpr PredictionMode NEW_NEARESTMV_PM = NEW_NEARESTMV;
constexpr PredictionMode NEAR_NEWMV_PM = NEAR_NEWMV;
constexpr PredictionMode NEW_NEARMV_PM = NEW_NEARMV;
constexpr PredictionMode GLOBAL_GLOBALMV_PM = GLOBAL_GLOBALMV;
constexpr PredictionMode NEW_NEWMV_PM = NEW_NEWMV;
constexpr PredictionMode MB_MODE_COUNT_PM = MB_MODE_COUNT;

// is_motion_variation_allowed_bsize (inter_prediction.h:378-380).
bool isMotionVariationAllowedBsize(BlockSize bsize);

// is_inter_singleref_mode (definitions.h:1610-1612).
std::int32_t isInterSinglerefMode(PredictionMode mode);

// is_global_mv_block (inter_prediction.h:382-388). CONFIG_ENABLE_GLOBAL_
// MOTION is live (= 1, the non-RTC EbConfigMacros block), so the guard body
// is kept (the l7 inter_symbols.cpp:195-198 twin).
std::int32_t isGlobalMvBlock(PredictionMode mode, BlockSize bsize, TransformationType type);

// ObmcControls (md_process.h:123-135), the consumed fields only.
struct ObmcCtrlsCarrier {
    std::uint8_t enabled;        // :124
    std::uint8_t maxBlkSize;     // :128 max_blk_size (test cap)
    std::uint8_t transFaceOff;   // :133 trans_face_off
};

// svt_aom_obmc_motion_mode_allowed (mode_decision.c:228-270). The walls
// thread per the campaign pattern: obmc_ctrls as the carrier; the frm_hdr
// pair (is_motion_mode_switchable, force_integer_mv) as flags; the
// global-motion wmtype for rf0 as the enum (the reference's rf0 only selects
// the global_motion slot - the slot index itself is dropped, named); the
// blk_ptr wall as the overlappable-neighbor count
// (has_overlappable_candidates == neighbors > 0). The !CONFIG_ENABLE_OBMC &&
// !CONFIG_ENABLE_WARP early-out (:232-234) is DEAD under the pinned build
// (both = 1) and is not ported.
MotionMode obmcMotionModeAllowed(const ObmcCtrlsCarrier& obmcCtrls, bool isMotionModeSwitchable,
                                 std::int32_t forceIntegerMv, TransformationType gmTypeRf0,
                                 std::uint32_t overlappableNeighbors, BlockSize bsize,
                                 std::uint8_t situation, MvReferenceFrame rf1, PredictionMode mode);

}  // namespace l16_rdo
