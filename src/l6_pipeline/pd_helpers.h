// l6_pipeline - the pd_process.c pure / carrier-able helpers (A6, PD-series).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/pd_process.c (the spans
// cited per function below). The struct-free convention (PO1/SRC1/A6C and
// the RE8A DeblockContext / EMC1 carrier precedents): every wall the ported
// spans consume travels as a carrier struct with the reference field names;
// the bodies are verbatim.
//
// PARKED (named, not faked):
//   picture_decision_context ctor/dctor (:179-:216) - the EbPtr
//     allocation wall.
//   calc_ahd (:56-:85) - CONFIG_ENABLE_TEMPORAL_FILTERING-gated; consumes
//     the pcs picture_histogram + scs region walls (the TF windows).
//   prune_sframe_refs (:1005-:1043) + set_all_ref_frame_type
//     (:1046-:1117) - consume av1_set_ref_frame (block_structures.h, A6's
//     own file) and the ctx sframe walls; the set_ref_list_counts span
//     (PD4) carries the consumed pieces as scalars. svt_aom_is_valid_
//     unipred_ref / svt_get_ref_frame_type's other consumers are already
//     ported in l16_rdo (md_slices.h isValidUnipredRef - grep-verified).
//   the picture-decision driver av1_generate_rps_info (:1913-:3513) - the
//     DPB wall (assign_and_release_pa_refs, mutexes, pa-ref queues).
//   derive_tf_window_params (:3759+) + the TF-window walkers - the
//     enhanced_pic/noise-level walls (l9_analysis owns the TF kernels).
//   the mini-gop / picture-window drivers (:4010-:5764) - the
//     PictureDecisionContext mutation wall.
//   encode_pass / pd_process_picture entry drivers.
#pragma once

#include <cstdint>
#include <cstddef>

namespace pipeline {
namespace pd {

// definitions.h:1637 INVALID_IDX; :1358-:1375 the MvReferenceFrame chain
// (NONE_FRAME -1, INTRA_FRAME 0, LAST_FRAME 1, ... ALTREF_FRAME 7,
// REF_FRAMES 8); INTER_REFS_PER_FRAME = ALTREF - LAST + 1 = 7.
constexpr std::int32_t kInvalidIdx = -1;
constexpr std::int32_t kIntraFrame = 0;
constexpr std::int32_t kLastFrame = 1;
constexpr std::int32_t kLast2Frame = 2;
constexpr std::int32_t kLast3Frame = 3;
constexpr std::int32_t kGoldenFrame = 4;
constexpr std::int32_t kBwdrefFrame = 5;
constexpr std::int32_t kAltref2Frame = 6;
constexpr std::int32_t kAltrefFrame = 7;
constexpr std::int32_t kRefFrames = 8;
constexpr std::int32_t kInterRefsPerFrame = 7;  // ALTREF - LAST + 1

// FrameType ordinals (definitions.h, the FrameHeader frame_type domain).
constexpr std::uint8_t kKeyFrame = 0;
constexpr std::uint8_t kInterFrame = 1;
constexpr std::uint8_t kIntraOnlyFrame = 2;
constexpr std::uint8_t kSFrame = 3;

// SliceType ordinals (the FrameHeader slice_type domain).
constexpr std::uint8_t kISlice = 0;
constexpr std::uint8_t kPSlice = 1;
constexpr std::uint8_t kBSlice = 2;

// ReferenceMode (definitions.h:1472-:1475).
constexpr std::uint8_t kSingleReference = 0;
constexpr std::uint8_t kCompoundReference = 1;
constexpr std::uint8_t kReferenceModeSelect = 2;

// PredStructure (Source/API/EbSvtAv1Enc.h:134).
constexpr std::uint8_t kPredAllIntra = 0;
constexpr std::uint8_t kPredLowDelay = 1;
constexpr std::uint8_t kPredRandomAccess = 2;

// EbSFrameMode (Source/API/EbSvtAv1Enc.h:161-:170).
constexpr std::uint8_t kSframeStrictBase = 1;
constexpr std::uint8_t kSframeNearestBase = 2;
constexpr std::uint8_t kSframeFlexibleBase = 3;
constexpr std::uint8_t kSframeDecPosiBase = 4;

// GmDsLevel (definitions.h:236-:244).
constexpr std::uint8_t kGmFull = 0;
constexpr std::uint8_t kGmDown = 1;
constexpr std::uint8_t kGmDown16 = 2;
constexpr std::uint8_t kGmAdapt0 = 3;
constexpr std::uint8_t kGmAdapt1 = 4;

// INVALID_REF = 0xF (mode_decision.h:206; the list1 table's fourth slot).
constexpr std::int32_t kInvalidRef = 0xF;

// SvtAv1FrameUpdateType ordinals (svt_av1.h; the A6C mirror's values).
constexpr std::uint8_t kKfUpdate = 0;
constexpr std::uint8_t kLfUpdate = 1;
constexpr std::uint8_t kGfUpdate = 2;
constexpr std::uint8_t kArfUpdate = 3;

// OrderHintInfo (the two consumed fields of sequence_control_set.h's
// seq_header.order_hint_info).
struct OrderHintInfo {
    std::uint8_t enable_order_hint;
    std::uint8_t order_hint_bits;
};

// SkipModeInfo (FrameHeader::skip_mode_params, the three consumed fields).
struct SkipModeInfo {
    std::uint8_t skip_mode_allowed;
    std::int32_t ref_frame_idx_0;
    std::int32_t ref_frame_idx_1;
};

// The PictureParentControlSet window svt_av1_setup_skip_mode_allowed
// (:104-:167) consumes: the scs order_hint_info, the frm_hdr slice_type +
// reference_mode, and the pcs cur/ref order hints (pcs.h:792-:793).
struct PdPcsSkipMode {
    OrderHintInfo order_hint_info;
    std::uint8_t slice_type;
    std::uint8_t reference_mode;
    std::uint32_t cur_order_hint;
    std::uint32_t ref_order_hint[kInterRefsPerFrame];
};

// svt_av1_setup_skip_mode_allowed (pd_process.c:104-:167). out receives
// the skip_mode_params write.
void setupSkipModeAllowed(const PdPcsSkipMode& pcs, SkipModeInfo* out);

// get_relative_dist (pd_process.c:87-:102). oh carries the
// OrderHintInfo; a/b are the two order hints.
std::int32_t getRelativeDist(const OrderHintInfo* oh, std::int32_t a, std::int32_t b);

// svt_aom_get_gm_needed_resolutions (pd_process.c:992-:996): the three
// downsampling needs per GmDsLevel.
void getGmNeededResolutions(std::uint8_t ds_lvl, bool* gm_need_full, bool* gm_need_quart, bool* gm_need_sixteen);

// The PictureParentControlSet/SequenceControlSet window
// svt_aom_is_pic_skipped (:998-:1003) consumes.
struct PdPcsSkipFlag {
    std::uint8_t is_ref;                  // pcs->is_ref
    std::uint8_t scs_rc_stat_gen_pass_mode;  // pcs->scs->rc_stat_gen_pass_mode
    std::uint8_t first_frame_in_minigop;  // pcs->first_frame_in_minigop
};

// svt_aom_is_pic_skipped (pd_process.c:998-:1003).
bool isPicSkipped(const PdPcsSkipFlag& pcs);

// svt_aom_get_max_allocated_me_refs (pcs.c:88-:93): the ME allocation
// sizing from the two used ref counts.
void getMaxAllocatedMeRefs(std::uint8_t ref_count_used_list0, std::uint8_t ref_count_used_list1,
                           std::uint8_t* max_ref_to_alloc, std::uint8_t* max_cand_to_alloc);

// svt_get_ref_frame_type (mode_decision.c:279-:284): the to_ref_frame
// table lookup. NAMED DUPLICATION: a file-local static twin already
// exists at coding_loop_helpers.cpp getRefFrameType (the l6 TUs do not
// share statics; the SYM1/l7-cannot-link-l13 precedent).
std::int32_t getRefFrameType(std::uint8_t list, std::uint8_t ref_idx);

}  // namespace pd
}  // namespace pipeline
