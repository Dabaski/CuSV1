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

// ---------------------------------------------------------------------------
// The ref-mgmt slot state machine (pd_process.c:1164-:1492).
// ---------------------------------------------------------------------------

// pd_process.h:52-:56 DpbEntry (the consumed field).
struct DpbEntry {
    std::uint64_t picture_number;
};

// pcs.h:745-:749 RefMgmt.
struct RefMgmt {
    std::uint32_t store_id;
    std::uint32_t clear_id;
    std::uint32_t use_id;
};

// pred_structure.h:65-:69 Av1RpsNode (verbatim field set).
struct Av1RpsNode {
    std::uint8_t refresh_frame_mask;
    std::uint8_t ref_dpb_index[7];
    std::uint64_t ref_poc_array[7];
};

// The PictureDecisionContext window (pd_process.h:98-:99, :148 + the
// dpb). NAMED DEVIATION: the reference's dpb is a DpbEntry array; the
// carrier holds the consumed picture_number column flat.
struct PdCtx {
    std::uint32_t pic_id_per_dpb_slot[kRefFrames];
    std::uint64_t dpb_picture_number[kRefFrames];
    std::uint8_t lay0_toggle;
    std::uint8_t lay1_toggle;
};

// The SequenceControlSet window the family reads: static_config.rtc /
// hierarchical_levels / pred_structure / max_managed_refs (pcs.c
// static_config) + mrp_ctrls.ld_reduce_ref_buffs (definitions.h:111+).
struct PdScsCfg {
    std::uint8_t rtc;
    std::uint8_t hierarchical_levels;
    std::uint8_t pred_structure;
    std::uint8_t max_managed_refs;
    std::uint8_t ld_reduce_ref_buffs;
};

// The PictureParentControlSet window for the ref-mgmt family + the
// frm_hdr/pcs display fields set_key_frame_rps writes.
struct PdPcsRefMgmt {
    RefMgmt ref_mgmt;
    Av1RpsNode av1_ref_signal;
    std::uint64_t picture_number;
    std::uint8_t temporal_layer_index;
    std::uint8_t is_overlay;
    std::uint8_t ref_list0_count;
    std::uint8_t ref_list1_count;
    std::uint8_t show_frame;      // frm_hdr->show_frame
    std::uint8_t has_show_existing;  // pcs->has_show_existing
    PdScsCfg scs;
};

// LAY1_OFF / LAY2_OFF (pd_process.c:46-:47).
constexpr std::uint32_t kLay1Off = 3;
constexpr std::uint32_t kLay2Off = 5;

// definitions.h:649-:654 svt_numbits, verbatim.
std::int32_t svtNumbits(unsigned int x);

// definitions.h:614-:618 svt_ctz (the MSVC _BitScanForward branch; the
// GCC branch is __builtin_ctz on the same semantics).
std::int32_t svtCtz(unsigned long x);

// ref_mgmt_reset_state (pd_process.c:1164-:1166).
void refMgmtResetState(PdCtx* ctx);

// ref_mgmt_stored_mask (pd_process.c:1174-:1182).
std::uint8_t refMgmtStoredMask(const PdCtx& ctx);

// ref_mgmt_find_slot (pd_process.c:1193-:1203): returns kRefFrames on
// miss (id 0 is the no-id sentinel).
std::uint8_t refMgmtFindSlot(const PdCtx& ctx, std::uint32_t pic_id);

// apply_ref_clear (pd_process.c:1206-:1216). DEVIATION (named): the
// SVT_ERROR warn on miss becomes a silent no-op - the port has no error
// channel; the no-op + unchanged state is the observable behavior.
void applyRefClear(PdPcsRefMgmt& pcs, PdCtx* ctx);

// exclusive_write_slots_mask_ld_cbr (pd_process.c:1227-:1259).
std::uint8_t exclusiveWriteSlotsMaskLdCbr(const PdScsCfg& scs);

// svt_aom_ref_mgmt_storeable_slots_mask (pd_process.c:1261-:1278).
std::uint8_t refMgmtStoreableSlotsMask(const PdScsCfg& scs);

// apply_ref_store (pd_process.c:1291-:1327): returns the picked slot or
// kRefFrames on failure (duplicate id, cap, full safe pool). The
// SVT_ERROR warns become silent no-ops (the deviation above).
std::uint8_t applyRefStore(PdPcsRefMgmt& pcs, PdCtx* ctx);

// apply_ref_use (pd_process.c:1338-:1357).
bool applyRefUse(PdPcsRefMgmt& pcs, const PdCtx& ctx);

// apply_ref_mgmt_events (pd_process.c:1380-:1480): the CLEAR -> STORE ->
// refresh guard -> USE dispatcher.
void applyRefMgmtEvents(PdPcsRefMgmt& pcs, PdCtx* ctx);

// set_key_frame_rps (pd_process.c:1482-:1492).
void setKeyFrameRps(PdPcsRefMgmt& pcs, PdCtx* ctx);

// ---------------------------------------------------------------------------
// PD3: the display params + sframe helpers.
// ---------------------------------------------------------------------------

// SvtAv1SFramePositions (Source/API/EbSvtAv1Enc.h:199-:205, verbatim).
struct SFramePositions {
    std::uint32_t sframe_num;
    const std::uint64_t* sframe_posis;
    std::uint32_t sframe_qp_num;
    const std::uint8_t* sframe_qps;
    const std::int8_t* sframe_qp_offsets;
};

// The PictureParentControlSet/PredictionStructure window
// set_frame_display_params (:1134-:1162) consumes.
struct PdPcsDisplay {
    std::uint8_t pred_type;             // pcs->pred_struct_ptr->pred_type
    std::uint8_t is_overlay;
    std::uint8_t slice_type;
    std::uint32_t pred_struct_entry_count;  // pcs->pred_struct_ptr->pred_struct_entry_count
    std::uint8_t show_frame;            // frm_hdr->show_frame
    std::uint8_t has_show_existing;
};

// The PictureDecisionContext window (mini_gop_length, pd_process.h:88).
struct PdDisplayCtx {
    const std::uint32_t* mini_gop_length;
};

// set_frame_display_params (pd_process.c:1134-:1162). Returns false on
// the RA b-frame branch (the caller handles it); the SVT_LOG error path
// (:1155) is a named silent deviation.
bool setFrameDisplayParams(PdPcsDisplay& pcs, const PdDisplayCtx& pd_ctx, std::uint32_t mini_gop_index);

// get_dist_to_s (pd_process.c:1496-:1509): the distance to the nearest
// S-Frame; dist_to_next_s is filled only when picture_num IS an
// S-Frame (and stays -1 past the last spot).
std::int32_t getDistToS(const SFramePositions& sframe_posi, std::uint64_t picture_num,
                        std::int32_t* dist_to_next_s);

// get_sframe_qp (pd_process.c:1511-:1525): the null-qps and null-posis
// branches are kept (the raw pointers travel on the carrier).
std::uint8_t getSframeQp(const SFramePositions& sframe_posi, std::uint64_t picture_num);

// get_sframe_qp_offset (pd_process.c:1527-:1541), the int8 twin.
std::int8_t getSframeQpOffset(const SFramePositions& sframe_posi, std::uint64_t picture_num);

// sframe_position_offset (pd_process.c:1565-:1570): the decode-order
// offset for SFRAME_DEC_POSI_BASE under RANDOM_ACCESS.
std::int32_t sframePositionOffset(const PdScsCfg& scs, std::uint8_t sframe_mode);

// ---------------------------------------------------------------------------
// PD4: the ref-usage / ref-list-count helpers.
// ---------------------------------------------------------------------------

// svt_aom_is_pic_used_as_ref (pd_process.c:1772-:1804), the pure scalar
// form (hierarchical_levels / temporal_layer / picture_index /
// referencing_scheme / is_overlay).
bool isPicUsedAsRef(std::uint32_t hierarchical_levels, std::uint32_t temporal_layer, std::uint32_t picture_index,
                    std::uint32_t referencing_scheme, bool is_overlay);

// svt_aom_is_incomp_mg_frame (pd_process.c:5006-:5008): LOW_DELAY pred
// type under a RANDOM_ACCESS pred structure.
bool isIncompMgFrame(std::uint8_t pred_type, std::uint8_t pred_structure);

// The PictureParentControlSet window set_ref_list_counts
// (:1806-:1901) consumes. frame_is_boosted(pcs) (enc_mode_config.h:108-
// :110, the update_type fold) and svt_aom_is_incomp_mg_frame travel as
// the is_base / is_incomp_mg scalars (the pcs-wall deviation named).
struct PdPcsRefCounts {
    std::uint8_t slice_type;
    std::uint64_t ref_poc_array[7];  // Av1RpsNode.ref_poc_array
    std::uint8_t base_ref_list0_count;
    std::uint8_t base_ref_list1_count;
    std::uint8_t non_base_ref_list0_count;
    std::uint8_t non_base_ref_list1_count;
    std::uint8_t is_base;       // frame_is_boosted(pcs)
    std::uint8_t is_incomp_mg;  // svt_aom_is_incomp_mg_frame(pcs)
    std::uint8_t is_overlay;
    std::uint8_t pred_structure;  // scs->static_config.pred_structure
    std::uint64_t picture_number;
    std::uint64_t sframe_poc;     // ctx->sframe_poc
    std::uint8_t ref_list0_count;  // out
    std::uint8_t ref_list1_count;  // out
};

// set_ref_list_counts (pd_process.c:1806-:1901): the duplicate-POC walk
// + the MrpCtrls caps.
void setRefListCounts(PdPcsRefCounts& pcs);

// update_ref_poc_array (pd_process.c:1903-:1911): ref_dpb_idx + the dpb
// picture_number column (the DpbEntry flattening, the PdCtx deviation).
void updateRefPocArray(const std::uint8_t* ref_dpb_idx, std::uint64_t* ref_poc_array,
                       const std::uint64_t* dpb_picture_number);

}  // namespace pd
}  // namespace pipeline
