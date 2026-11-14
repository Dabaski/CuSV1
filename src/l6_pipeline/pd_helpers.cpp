// l6_pipeline - the pd_process.c helpers (A6, PD-series). See
// pd_helpers.h for the park list and carrier conventions.
#include <pd_helpers.h>

// intra.h FIRST: rate_est.h:325 defines UV_INTRA_MODES as a macro (the
// same value as intra.h's enum constant); including intra.h after the
// rate_est chain breaks its UvPredictionMode enum parse. This order
// keeps both readable (the macro lands after the enumerator, the C4005
// pair is benign and already inside coding_loop_helpers.h's disabled
// window).
#include <intra.h>
#include <coding_loop_helpers.h>
#include <packunpack.h>
#include <mode_config.h>

#include <cassert>
#include <climits>
#include <cstring>
#include <intrin.h>

namespace pipeline {
namespace pd {

// get_relative_dist (pd_process.c:87-:102), verbatim.
std::int32_t getRelativeDist(const OrderHintInfo* oh, std::int32_t a, std::int32_t b) {
    if (!oh->enable_order_hint) {
        return 0;
    }

    const std::int32_t bits = oh->order_hint_bits;

    assert(bits >= 1);
    assert(a >= 0 && a < (1 << bits));
    assert(b >= 0 && b < (1 << bits));

    std::int32_t diff = a - b;
    const std::int32_t m = 1 << (bits - 1);
    diff = (diff & (m - 1)) - (diff & m);
    return diff;
}

// svt_av1_setup_skip_mode_allowed (pd_process.c:104-:167), verbatim over
// the carrier. The three early-exit gates reset the outputs and return;
// the bi-directional and forward-only branches write the LAST_FRAME-based
// indices.
void setupSkipModeAllowed(const PdPcsSkipMode& pcs, SkipModeInfo* out) {
    const OrderHintInfo* const order_hint_info = &pcs.order_hint_info;

    out->skip_mode_allowed = 0;
    out->ref_frame_idx_0 = kInvalidIdx;
    out->ref_frame_idx_1 = kInvalidIdx;

    const std::uint32_t* ref_order_hint = pcs.ref_order_hint;

    // If these conditions are true, skip mode is not allowed, so return early
    if (!order_hint_info->enable_order_hint || pcs.slice_type == kISlice /*frame_is_intra_only(cm)*/ ||
        pcs.reference_mode == kSingleReference) {
        return;
    }

    const std::int32_t cur_order_hint = static_cast<std::int32_t>(pcs.cur_order_hint);
    std::int32_t ref_order_hints[2] = {-1, INT_MAX};
    std::int32_t ref_idx[2] = {kInvalidIdx, kInvalidIdx};

    // Identify the nearest forward and backward references.
    for (std::int32_t i = 0; i < kInterRefsPerFrame; ++i) {
        const std::int32_t ref_hint = static_cast<std::int32_t>(ref_order_hint[i]);  // buf->order_hint;
        if (getRelativeDist(order_hint_info, ref_hint, cur_order_hint) < 0) {
            // Forward reference
            if (ref_order_hints[0] == -1 ||
                getRelativeDist(order_hint_info, ref_hint, ref_order_hints[0]) > 0) {
                ref_order_hints[0] = ref_hint;
                ref_idx[0] = i;
            }
        } else if (getRelativeDist(order_hint_info, ref_hint, cur_order_hint) > 0) {
            // Backward reference
            if (ref_order_hints[1] == INT_MAX ||
                getRelativeDist(order_hint_info, ref_hint, ref_order_hints[1]) < 0) {
                ref_order_hints[1] = ref_hint;
                ref_idx[1] = i;
            }
        }
    }

    if (ref_idx[0] != kInvalidIdx && ref_idx[1] != kInvalidIdx) {
        // == Bi-directional prediction ==
        out->skip_mode_allowed = 1;
        out->ref_frame_idx_0 = kLastFrame + (ref_idx[0] < ref_idx[1] ? ref_idx[0] : ref_idx[1]);
        out->ref_frame_idx_1 = kLastFrame + (ref_idx[0] > ref_idx[1] ? ref_idx[0] : ref_idx[1]);
    } else if (ref_idx[0] != kInvalidIdx && ref_idx[1] == kInvalidIdx) {
        // == Forward prediction only ==
        // Identify the second nearest forward reference.
        ref_order_hints[1] = -1;
        for (std::int32_t i = 0; i < kInterRefsPerFrame; ++i) {
            const std::int32_t ref_hint = static_cast<std::int32_t>(ref_order_hint[i]);  // buf->order_hint;
            if ((ref_order_hints[0] != -1 &&
                 getRelativeDist(order_hint_info, ref_hint, ref_order_hints[0]) < 0) &&
                (ref_order_hints[1] == -1 ||
                 getRelativeDist(order_hint_info, ref_hint, ref_order_hints[1]) > 0)) {
                // Second closest forward reference
                ref_order_hints[1] = ref_hint;
                ref_idx[1] = i;
            }
        }
        if (ref_order_hints[1] != -1) {
            out->skip_mode_allowed = 1;
            out->ref_frame_idx_0 = kLastFrame + (ref_idx[0] < ref_idx[1] ? ref_idx[0] : ref_idx[1]);
            out->ref_frame_idx_1 = kLastFrame + (ref_idx[0] > ref_idx[1] ? ref_idx[0] : ref_idx[1]);
        }
    }
}

// svt_aom_get_gm_needed_resolutions (pd_process.c:992-:996), verbatim.
void getGmNeededResolutions(std::uint8_t ds_lvl, bool* gm_need_full, bool* gm_need_quart, bool* gm_need_sixteen) {
    *gm_need_full = (ds_lvl == kGmFull) || (ds_lvl == kGmAdapt0);
    *gm_need_quart = (ds_lvl == kGmDown) || (ds_lvl == kGmAdapt0) || (ds_lvl == kGmAdapt1);
    *gm_need_sixteen = (ds_lvl == kGmDown16) || (ds_lvl == kGmAdapt1);
}

// svt_aom_is_pic_skipped (pd_process.c:998-:1003), verbatim.
bool isPicSkipped(const PdPcsSkipFlag& pcs) {
    if (!pcs.is_ref && pcs.scs_rc_stat_gen_pass_mode && !pcs.first_frame_in_minigop) {
        return true;
    }
    return false;
}

// svt_aom_get_max_allocated_me_refs (pcs.c:88-:93), verbatim.
void getMaxAllocatedMeRefs(std::uint8_t ref_count_used_list0, std::uint8_t ref_count_used_list1,
                           std::uint8_t* max_ref_to_alloc, std::uint8_t* max_cand_to_alloc) {
    *max_ref_to_alloc = ref_count_used_list0 + ref_count_used_list1;
    *max_cand_to_alloc = ref_count_used_list0 + ref_count_used_list1 +
        (ref_count_used_list0 * ref_count_used_list1) + (ref_count_used_list0 - 1) + (ref_count_used_list1 == 3 ? 1 : 0);
}

// svt_get_ref_frame_type (mode_decision.c:279-:284): to_ref_frame verbatim.
std::int32_t getRefFrameType(std::uint8_t list, std::uint8_t ref_idx) {
    static const std::int32_t to_ref_frame[2][4] = {{kLastFrame, kLast2Frame, kLast3Frame, kGoldenFrame},
                                                    {kBwdrefFrame, kAltref2Frame, kAltrefFrame, kInvalidRef}};
    return to_ref_frame[list][ref_idx];
}

// ---------------------------------------------------------------------------
// The ref-mgmt slot state machine (pd_process.c:1164-:1492).
// ---------------------------------------------------------------------------

// definitions.h:649-:654 svt_numbits, verbatim.
std::int32_t svtNumbits(unsigned int x) {
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return static_cast<std::int32_t>((x * 0x01010101u) >> 24);
}

// definitions.h:614-:618 svt_ctz, the MSVC branch.
std::int32_t svtCtz(unsigned long x) {
    unsigned long k;
    _BitScanForward(&k, x);
    return static_cast<std::int32_t>(k);
}

// ref_mgmt_reset_state (pd_process.c:1164-:1166), verbatim.
void refMgmtResetState(PdCtx* ctx) {
    std::memset(ctx->pic_id_per_dpb_slot, 0, sizeof(ctx->pic_id_per_dpb_slot));
}

// ref_mgmt_stored_mask (pd_process.c:1174-:1182), verbatim.
std::uint8_t refMgmtStoredMask(const PdCtx& ctx) {
    std::uint8_t m = 0;
    for (std::uint8_t i = 0; i < kRefFrames; ++i) {
        if (ctx.pic_id_per_dpb_slot[i] != 0) {
            m |= static_cast<std::uint8_t>(1u << i);
        }
    }
    return m;
}

// ref_mgmt_find_slot (pd_process.c:1193-:1203), verbatim.
std::uint8_t refMgmtFindSlot(const PdCtx& ctx, std::uint32_t pic_id) {
    if (pic_id == 0) {
        return static_cast<std::uint8_t>(kRefFrames);  // 0 = "no id" sentinel
    }
    for (std::uint8_t i = 0; i < kRefFrames; ++i) {
        if (ctx.pic_id_per_dpb_slot[i] == pic_id) {
            return i;
        }
    }
    return static_cast<std::uint8_t>(kRefFrames);
}

// apply_ref_clear (pd_process.c:1206-:1216), verbatim minus the SVT_ERROR
// channel (named deviation: the warn is a silent no-op).
void applyRefClear(PdPcsRefMgmt& pcs, PdCtx* ctx) {
    (void)pcs;
    const std::uint32_t pid = pcs.ref_mgmt.clear_id;
    const std::uint8_t slot = refMgmtFindSlot(*ctx, pid);
    if (slot >= kRefFrames) {
        return;
    }
    ctx->pic_id_per_dpb_slot[slot] = 0;
}

// exclusive_write_slots_mask_ld_cbr (pd_process.c:1227-:1259), verbatim.
std::uint8_t exclusiveWriteSlotsMaskLdCbr(const PdScsCfg& scs) {
    std::uint8_t mask = 0;
    const std::uint8_t hier = scs.hierarchical_levels;
    const std::uint8_t ld_reduce = scs.ld_reduce_ref_buffs;

    // TID-0: single-bit `1 << lay0_toggle` (toggle in {0,1,2}) only at
    // ld_reduce=0; backup bits (`| 0xf0` / `| 0xfc`) make it non-exclusive otherwise.
    if (ld_reduce == 0) {
        mask |= 0x07u;
    }
    // TID-1: always single-bit; slot depends on ld_reduce.
    if (hier >= 1) {
        switch (ld_reduce) {
        case 0:
            mask |= static_cast<std::uint8_t>((1u << kLay1Off) | (1u << (kLay1Off + 1)));
            break;
        case 1:
            mask |= static_cast<std::uint8_t>(1u << kLay1Off);
            break;
        case 2:
            mask |= static_cast<std::uint8_t>(1u << 1);
            break;
        default:
            assert(0 && "unhandled ld_reduce_ref_buffs");
            break;
        }
    }
    // TID-2: ld_reduce > 0 force-zeros TID-2 refresh, so only exclusive at ld_reduce=0.
    if (hier >= 2 && ld_reduce == 0) {
        mask |= static_cast<std::uint8_t>(1u << kLay2Off);
    }
    return mask;
}

// svt_aom_ref_mgmt_storeable_slots_mask (pd_process.c:1261-:1278),
// verbatim.
std::uint8_t refMgmtStoreableSlotsMask(const PdScsCfg& scs) {
    if (scs.rtc && scs.hierarchical_levels == 0) {
        return 0xF0u;
    }
    if (scs.pred_structure == kPredLowDelay && scs.hierarchical_levels >= 1) {
        return static_cast<std::uint8_t>(~exclusiveWriteSlotsMaskLdCbr(scs) & 0xFFu);
    }
    // RA / other paths: not LTR-eligible (rejected in enc_settings.c).
    return 0xFFu;
}

// apply_ref_store (pd_process.c:1291-:1327), verbatim minus the SVT_ERROR
// channel.
std::uint8_t applyRefStore(PdPcsRefMgmt& pcs, PdCtx* ctx) {
    const std::uint32_t pid = pcs.ref_mgmt.store_id;
    if (refMgmtFindSlot(*ctx, pid) < kRefFrames) {
        return static_cast<std::uint8_t>(kRefFrames);
    }
    // Enforce the simultaneous-hold cap to match buffer-pool sizing.
    const std::uint8_t held = static_cast<std::uint8_t>(svtNumbits(refMgmtStoredMask(*ctx)));
    if (held >= pcs.scs.max_managed_refs) {
        return static_cast<std::uint8_t>(kRefFrames);
    }
    const std::uint8_t storeable_mask = refMgmtStoreableSlotsMask(pcs.scs);
    const std::uint8_t free = static_cast<std::uint8_t>(storeable_mask & ~refMgmtStoredMask(*ctx));
    if (free == 0) {
        return static_cast<std::uint8_t>(kRefFrames);
    }
    const std::uint8_t slot = static_cast<std::uint8_t>(svtCtz(free));
    // Force-refresh the picked slot so the current frame's reconstruction
    // lands there, independent of what the branch's RPS chose.
    pcs.av1_ref_signal.refresh_frame_mask |= static_cast<std::uint8_t>(1u << slot);
    ctx->pic_id_per_dpb_slot[slot] = pid;
    return slot;
}

// apply_ref_use (pd_process.c:1338-:1357), verbatim.
bool applyRefUse(PdPcsRefMgmt& pcs, const PdCtx& ctx) {
    const std::uint32_t pid = pcs.ref_mgmt.use_id;
    const std::uint8_t slot = refMgmtFindSlot(ctx, pid);
    if (slot >= kRefFrames) {
        return false;
    }
    Av1RpsNode* rps = &pcs.av1_ref_signal;
    const std::uint64_t poc = ctx.dpb_picture_number[slot];
    /* AV1 ref positions LAST..ALT = INTER_REFS_PER_FRAME (7) entries. */
    for (std::int32_t i = 0; i < kInterRefsPerFrame; ++i) {
        rps->ref_dpb_index[i] = slot;
        rps->ref_poc_array[i] = poc;
    }
    pcs.ref_list0_count = 1;
    pcs.ref_list1_count = 0;
    return true;
}

// apply_ref_mgmt_events (pd_process.c:1380-:1480), verbatim.
void applyRefMgmtEvents(PdPcsRefMgmt& pcs, PdCtx* ctx) {
    std::uint8_t new_store_slot = static_cast<std::uint8_t>(kRefFrames);
    const bool have_event =
        pcs.ref_mgmt.store_id != 0 || pcs.ref_mgmt.clear_id != 0 || pcs.ref_mgmt.use_id != 0;
    bool events_ok = have_event;

    if (have_event) {
        if (pcs.is_overlay) {
            events_ok = false;
        } else {
            const bool is_base = pcs.temporal_layer_index == 0;
            if (!is_base) {
                events_ok = false;
            } else {
                // Reject pic_id collisions across same-frame events.
                const std::uint32_t s = pcs.ref_mgmt.store_id;
                const std::uint32_t c = pcs.ref_mgmt.clear_id;
                const std::uint32_t u = pcs.ref_mgmt.use_id;
                if ((s != 0 && s == c) || (s != 0 && s == u) || (c != 0 && c == u)) {
                    events_ok = false;
                }
            }
        }
        if (!events_ok) {
            pcs.ref_mgmt.store_id = pcs.ref_mgmt.clear_id = pcs.ref_mgmt.use_id = 0;
        }
    }

    // Phase 1: CLEAR - frees slots before STORE so freed slots can be reused.
    if (pcs.ref_mgmt.clear_id != 0) {
        applyRefClear(pcs, ctx);
        // apply_ref_clear() warns on miss; clear the field so downstream
        // consumers don't see a "phantom" CLEAR after the warning.
        pcs.ref_mgmt.clear_id = 0;
    }

    // Phase 2: STORE - claim a free slot from refresh_frame_mask.
    if (pcs.ref_mgmt.store_id != 0) {
        new_store_slot = applyRefStore(pcs, ctx);
        if (new_store_slot >= kRefFrames) {
            pcs.ref_mgmt.store_id = 0;
        }
    }

    // Phase 3: refresh guard - preserve all previously-STOREd slots from
    // overwrite. The new STORE's slot (if any) is excluded from the preserve
    // set so the current frame's data lands there. Always runs (even without
    // events) so a held slot from an earlier frame is never overwritten.
    {
        const std::uint8_t stored_mask = refMgmtStoredMask(*ctx);
        const std::uint8_t preserve_mask = (new_store_slot < kRefFrames)
            ? static_cast<std::uint8_t>(stored_mask & static_cast<std::uint8_t>(~(1u << new_store_slot)))
            : stored_mask;
        pcs.av1_ref_signal.refresh_frame_mask = static_cast<std::uint8_t>(
            pcs.av1_ref_signal.refresh_frame_mask & static_cast<std::uint8_t>(~preserve_mask));
    }

    // Phase 4: USE - override refs, then recovery-point refresh.
    if (pcs.ref_mgmt.use_id != 0) {
        if (applyRefUse(pcs, *ctx)) {
            // Recovery-point refresh: every non-STOREd slot gets the current
            // frame so future frames can only reference STOREd anchors or
            // this recovery point.
            std::uint8_t refresh_mask = static_cast<std::uint8_t>(0xFFu & ~refMgmtStoredMask(*ctx));
            if (new_store_slot < kRefFrames) {
                // The just-STOREd slot must also receive current frame data.
                refresh_mask |= static_cast<std::uint8_t>(1u << new_store_slot);
            }
            pcs.av1_ref_signal.refresh_frame_mask = refresh_mask;
        }
    }
}

// set_key_frame_rps (pd_process.c:1482-:1492), verbatim.
void setKeyFrameRps(PdPcsRefMgmt& pcs, PdCtx* pd_ctx) {
    pd_ctx->lay0_toggle = 0;
    pd_ctx->lay1_toggle = 0;
    // KF refreshes all 8 DPB slots, so all previously-STOREd refs are invalidated.
    refMgmtResetState(pd_ctx);

    pcs.show_frame = 1;
    pcs.has_show_existing = 0;
}

// ---------------------------------------------------------------------------
// PD3: the display params + sframe helpers.
// ---------------------------------------------------------------------------

// set_frame_display_params (pd_process.c:1134-:1162), verbatim over the
// carrier.
bool setFrameDisplayParams(PdPcsDisplay& pcs, const PdDisplayCtx& pd_ctx, std::uint32_t mini_gop_index) {
    if (pcs.pred_type == kPredLowDelay || pcs.is_overlay) {
        pcs.show_frame = 1;
        pcs.has_show_existing = 0;
    } else {
        // Decide on Show Mecanism
        if (pcs.slice_type == kISlice) {
            // 3 cases for I slice: 1:Key Frame treated above. 2: broken
            // MiniGop due to sc or intra refresh 3: complete miniGop due
            // to sc or intra refresh
            if (pd_ctx.mini_gop_length[mini_gop_index] < pcs.pred_struct_entry_count) {
                // Scene Change that breaks the mini gop and switch to LDP
                pcs.show_frame = 1;
                pcs.has_show_existing = 0;
            } else {
                pcs.show_frame = 0;
                pcs.has_show_existing = 0;
            }
        } else {
            // Handle b frame of Random Access out
            return false;
        }
    }
    return true;
}

// get_dist_to_s (pd_process.c:1496-:1509), verbatim.
std::int32_t getDistToS(const SFramePositions& sframe_posi, std::uint64_t picture_num,
                        std::int32_t* dist_to_next_s) {
    *dist_to_next_s = -1;
    for (std::uint32_t i = 0; i < sframe_posi.sframe_num; i++) {
        if (sframe_posi.sframe_posis[i] >= picture_num) {
            if (sframe_posi.sframe_posis[i] == picture_num) {
                *dist_to_next_s = (i < sframe_posi.sframe_num - 1)
                    ? static_cast<std::int32_t>(sframe_posi.sframe_posis[i + 1] - picture_num)
                    : -1;
            }
            return static_cast<std::int32_t>(sframe_posi.sframe_posis[i] - picture_num);
        }
    }
    return -1;  // all s-frame spots are expired
}

// get_sframe_qp (pd_process.c:1511-:1525), verbatim.
std::uint8_t getSframeQp(const SFramePositions& sframe_posi, std::uint64_t picture_num) {
    if (sframe_posi.sframe_qps == nullptr) {
        return 0;
    }
    if (sframe_posi.sframe_posis == nullptr) {
        // always return first QP if not use flexible S-Frame position list
        return sframe_posi.sframe_qps[0];
    }
    for (std::uint32_t i = 0; i < sframe_posi.sframe_num; i++) {
        if (sframe_posi.sframe_posis[i] == picture_num) {
            return sframe_posi.sframe_qps[i];
        }
    }
    return 0;  // not find the picture
}

// get_sframe_qp_offset (pd_process.c:1527-:1541), verbatim.
std::int8_t getSframeQpOffset(const SFramePositions& sframe_posi, std::uint64_t picture_num) {
    if (sframe_posi.sframe_qp_offsets == nullptr) {
        return 0;
    }
    if (sframe_posi.sframe_posis == nullptr) {
        // always return first QP offset if not use flexible S-Frame position list
        return sframe_posi.sframe_qp_offsets[0];
    }
    for (std::uint32_t i = 0; i < sframe_posi.sframe_num; i++) {
        if (sframe_posi.sframe_posis[i] == picture_num) {
            return sframe_posi.sframe_qp_offsets[i];
        }
    }
    return 0;  // not find the picture
}

// sframe_position_offset (pd_process.c:1565-:1570), verbatim.
std::int32_t sframePositionOffset(const PdScsCfg& scs, std::uint8_t sframe_mode) {
    return (sframe_mode == kSframeDecPosiBase && scs.pred_structure == kPredRandomAccess) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// PD4: the ref-usage / ref-list-count helpers.
// ---------------------------------------------------------------------------

// svt_aom_is_pic_used_as_ref (pd_process.c:1772-:1804), verbatim.
bool isPicUsedAsRef(std::uint32_t hierarchical_levels, std::uint32_t temporal_layer, std::uint32_t picture_index,
                    std::uint32_t referencing_scheme, bool is_overlay) {
    if (is_overlay) {
        return false;
    }

    // Frames below top layer are always used as ref
    if (temporal_layer < hierarchical_levels) {
        return true;
    }

    switch (hierarchical_levels) {
    case 0:
        return true;
    case 1:
        return referencing_scheme == 0 ? false : true;
    case 2:
        return referencing_scheme == 0 ? false : referencing_scheme == 1 ? true : (picture_index == 0);
    case 3:
        return referencing_scheme == 0 ? false : referencing_scheme == 1 ? true : (picture_index == 0);
    case 4:
        return referencing_scheme == 0 ? false
            : referencing_scheme == 1  ? true
                                       : (picture_index == 0 || picture_index == 8);
    case 5:
        return false;
    default:
        assert(0 && "Invalid hierarchical structure\n");
        break;
    }

    return true;
}

// svt_aom_is_incomp_mg_frame (pd_process.c:5006-:5008), verbatim.
bool isIncompMgFrame(std::uint8_t pred_type, std::uint8_t pred_structure) {
    return (pred_type == kPredLowDelay && pred_structure == kPredRandomAccess);
}

// set_ref_list_counts (pd_process.c:1806-:1901), verbatim over the
// carrier. The REF_FRAME_MINUS1 walk (LAST 0..ALT 6).
void setRefListCounts(PdPcsRefCounts& pcs) {
    if (pcs.slice_type == kISlice) {
        pcs.ref_list0_count = 0;
        pcs.ref_list1_count = 0;
        return;
    }

    const bool is_base = pcs.is_base != 0;

    // Get list0 count
    std::uint8_t list0_count = 1;
    bool breakout_flag = false;
    // When have duplicate refs in same list or get invalid ref, cap the count
    for (std::int32_t i = 1; i <= 3; ++i) {  // LAST2..GOLD
        if (breakout_flag) {
            break;
        }
        for (std::int32_t j = 0; j < i; ++j) {  // LAST..i-1
            if (pcs.ref_poc_array[i] == pcs.ref_poc_array[j]) {
                breakout_flag = true;
                break;
            }
        }
        // if no matching reference were found, increase the count
        if (!breakout_flag) {
            list0_count++;
        }
    }
    const std::uint8_t l0_cap = is_base ? pcs.base_ref_list0_count : pcs.non_base_ref_list0_count;
    pcs.ref_list0_count = list0_count < l0_cap ? list0_count : l0_cap;

    if (pcs.is_incomp_mg || pcs.is_overlay) {
        pcs.ref_list1_count = 0;
        return;
    }

    // Get list1 count
    std::uint8_t list1_count = 0;
    breakout_flag = false;
    // When have duplicate refs in both lists or get invalid ref, cap the count
    for (std::int32_t i = 4; i <= 6; ++i) {  // BWD..ALT
        if (breakout_flag) {
            break;
        }
        // BWD and LAST are allowed to have matching references, as in base layer
        for (std::int32_t j = (i == 4) ? 1 : 0; j < i; ++j) {
            if (j <= 3 && j + 1 > pcs.ref_list0_count) {
                continue;
            }
            // in S-Frame of RA mode, since the ref_list0 will be pruned in
            // set_all_ref_frame_type(), the rest frames in S-Frame miniGop
            // should not remove the duplicated ref_list1, which causes no
            // ref frames in ref_list1, skip the following check only for
            // S-Frame miniGOP
            if (pcs.pred_structure == kPredRandomAccess && pcs.picture_number < pcs.sframe_poc && j <= 3 &&
                pcs.ref_poc_array[j] == pcs.sframe_poc) {
                continue;
            }
            if (pcs.ref_poc_array[i] == pcs.ref_poc_array[j]) {
                breakout_flag = true;
                break;
            }
        }
        // if no matching reference were found, increase the count
        if (!breakout_flag) {
            list1_count++;
        }
    }
    const std::uint8_t l1_cap = is_base ? pcs.base_ref_list1_count : pcs.non_base_ref_list1_count;
    pcs.ref_list1_count = list1_count < l1_cap ? list1_count : l1_cap;
}

// update_ref_poc_array (pd_process.c:1903-:1911), verbatim over the
// flattened dpb column.
void updateRefPocArray(const std::uint8_t* ref_dpb_idx, std::uint64_t* ref_poc_array,
                       const std::uint64_t* dpb_picture_number) {
    ref_poc_array[0] = dpb_picture_number[ref_dpb_idx[0]];
    ref_poc_array[1] = dpb_picture_number[ref_dpb_idx[1]];
    ref_poc_array[2] = dpb_picture_number[ref_dpb_idx[2]];
    ref_poc_array[3] = dpb_picture_number[ref_dpb_idx[3]];
    ref_poc_array[4] = dpb_picture_number[ref_dpb_idx[4]];
    ref_poc_array[5] = dpb_picture_number[ref_dpb_idx[5]];
    ref_poc_array[6] = dpb_picture_number[ref_dpb_idx[6]];
}

// ---------------------------------------------------------------------------
// PD5: the picture-availability + TF-modulation helpers.
// ---------------------------------------------------------------------------

// avail_past_pictures (pd_process.c:3601-:3610), verbatim over the
// flattened buffer column.
std::int32_t availPastPictures(const std::uint64_t* buf_picture_numbers, std::uint32_t buf_size,
                               std::uint64_t input_pic) {
    // buffer has at least curr picture
    std::int32_t tot_past = 0;
    for (std::uint32_t pic = 0; pic < buf_size; pic++) {
        if (buf_picture_numbers[pic] < input_pic) {
            tot_past++;
        }
    }
    return tot_past;
}

// search_this_pic (pd_process.c:3616-:3625), verbatim.
std::int32_t searchThisPic(const std::uint64_t* buf_picture_numbers, std::uint32_t buf_size,
                           std::uint64_t input_pic) {
    std::int32_t index = -1;
    for (std::uint32_t pic = 0; pic < buf_size; pic++) {
        if (buf_picture_numbers[pic] == input_pic) {
            index = static_cast<std::int32_t>(pic);
            break;
        }
    }
    return index;
}

// svt_aom_is_delayed_intra (pd_process.c:3630-:3644), verbatim.
bool isDelayedIntra(const PdPcsDelayIntra& pcs) {
    if ((pcs.idr_flag || pcs.cra_flag) && pcs.pred_structure == kPredRandomAccess) {
        if (pcs.intra_period_length == 0 || pcs.end_of_sequence_flag) {
            return false;
        } else if (pcs.idr_flag ||
                   (pcs.cra_flag &&
                    pcs.pre_assignment_buffer_count < pcs.pred_struct_entry_count)) {
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

// ref_pics_modulation (pd_process.c:3653-:3755), verbatim over the
// carrier; the qp-th factors come from the landed l16 port
// (enc_mode_config.c:26-:54 via l16_rdo::getQpBasedThScalingFactors).
std::int32_t refPicsModulation(const PdPcsModulate& pcs, std::int32_t noise_levels_log1p_fp16) {
    std::int32_t offset = 0;

    if (pcs.slice_type == kISlice) {
        // Adjust number of filtering frames based on noise and
        // quantization factor (the FLOAT2FP(log1p(x), 16) thresholds).
        if (noise_levels_log1p_fp16 < 26572) {
            offset = 6;
        } else if (noise_levels_log1p_fp16 < 45426) {
            offset = 4;
        } else if (noise_levels_log1p_fp16 < 71998) {
            offset = 2;
        }
    } else if (pcs.temporal_layer_index == 0) {
        const std::int32_t ratio = noise_levels_log1p_fp16
            ? (pcs.filt_to_unfilt_diff * 100) / noise_levels_log1p_fp16
            : 0;
        switch (pcs.modulate_pics) {
        case 0:
            offset = 0;
            break;
        case 1:
            if (ratio < 100) {
                offset = 5;
            } else {
                offset = kTfMaxExtension;
            }
            break;
        case 2:
            if (ratio < 50) {
                offset = 3;
            } else if (ratio < 100) {
                offset = 5;
            } else {
                offset = kTfMaxExtension;
            }
            break;
        case 3:
            if (ratio < 50) {
                offset = 3;
            } else if (ratio < 100) {
                offset = 4;
            } else {
                offset = 5;
            }
            break;
        case 4:
            if (ratio < 50) {
                offset = 0;
            } else if (ratio < 100) {
                offset = 1;
            } else {
                offset = 2;
            }
            break;
        default:
            break;
        }
    } else {
        const std::int32_t ratio = noise_levels_log1p_fp16
            ? (pcs.filt_to_unfilt_diff * 100) / noise_levels_log1p_fp16
            : 0;
        switch (pcs.modulate_pics) {
        case 0:
            offset = 0;
            break;
        case 1:
            if (ratio < 25) {
                offset = 0;
            } else {
                offset = 1;
            }
            break;
        case 2:
            if (ratio < 50) {
                offset = 0;
            } else {
                offset = 1;
            }
            break;
        case 3:
            if (ratio < 75) {
                offset = 0;
            } else {
                offset = 1;
            }
            break;
        default:
            break;
        }
    }
    // Modulate offset using qp
    if (pcs.qp_opt) {
        std::uint32_t q_weight, q_weight_denom;
        l16_rdo::mode_config::getQpBasedThScalingFactors(pcs.qp_opt != 0, &q_weight, &q_weight_denom, pcs.scs_qp);
        offset = (offset * static_cast<std::int32_t>(q_weight) + (static_cast<std::int32_t>(q_weight_denom) >> 1)) /
            static_cast<std::int32_t>(q_weight_denom);
    }
    return offset;
}

// ---------------------------------------------------------------------------
// PO2 park resolution: the pic_operators.c remainder.
// ---------------------------------------------------------------------------

// svt_aom_un_pack2d (pic_operators.c:162-:171): both branches route to
// the landed PU1 kernel (the function-identity deviation named in the
// header).
void unpack2d(const std::uint16_t* in16_bit_buffer, std::uint32_t in_stride, std::uint8_t* out8_bit_buffer,
              std::uint32_t out8_stride, std::uint8_t* outn_bit_buffer, std::uint32_t outn_stride,
              std::uint32_t width, std::uint32_t height) {
    analysis::packunpack::msbUnPack2d(in16_bit_buffer, in_stride, out8_bit_buffer, outn_bit_buffer, out8_stride,
                                      outn_stride, width, height);
}

// svt_aom_pack2d_src (pic_operators.c:173-:182), same routing.
void pack2dSrc(std::uint8_t* in8_bit_buffer, std::uint32_t in8_stride, std::uint8_t* inn_bit_buffer,
               std::uint32_t inn_stride, std::uint16_t* out16_bit_buffer, std::uint32_t out_stride,
               std::uint32_t width, std::uint32_t height) {
    analysis::packunpack::msbPack2d(in8_bit_buffer, in8_stride, inn_bit_buffer, inn_stride, out16_bit_buffer,
                                    out_stride, width, height);
}

// svt_aom_compressed_pack_sb (pic_operators.c:184-:189), same routing.
void compressedPackSb(const std::uint8_t* in8_bit_buffer, std::uint32_t in8_stride,
                      const std::uint8_t* inn_bit_buffer, std::uint32_t inn_stride,
                      std::uint16_t* out16_bit_buffer, std::uint32_t out_stride, std::uint32_t width,
                      std::uint32_t height) {
    analysis::packunpack::compressedPackMsb(in8_bit_buffer, in8_stride, inn_bit_buffer, inn_stride,
                                            out16_bit_buffer, out_stride, width, height);
}

// svt_aom_pack_2d_pic (pic_operators.c:481-:508), verbatim over the
// carrier.
void pack2dPic(const PicPackDesc& input_picture, std::uint16_t* const packed[3]) {
    analysis::packunpack::compressedPackMsb(input_picture.y_buffer, input_picture.y_stride,
                                            input_picture.y_buffer_bit_inc, input_picture.y_stride_bit_inc >> 2,
                                            packed[0], input_picture.y_stride, input_picture.width,
                                            input_picture.height);

    analysis::packunpack::compressedPackMsb(input_picture.u_buffer, input_picture.v_stride,
                                            input_picture.u_buffer_bit_inc, input_picture.v_stride_bit_inc >> 2,
                                            packed[1], input_picture.v_stride, input_picture.width >> 1,
                                            input_picture.height >> 1);

    analysis::packunpack::compressedPackMsb(input_picture.v_buffer, input_picture.v_stride,
                                            input_picture.v_buffer_bit_inc, input_picture.v_stride_bit_inc >> 2,
                                            packed[2], input_picture.v_stride, input_picture.width >> 1,
                                            input_picture.height >> 1);
}

// svt_aom_convert_pic_8bit_to_16bit (pic_operators.c:510-:545),
// verbatim over the carrier.
void convertPic8bitTo16bit(const PicPackDesc& src_8bit, PicPackDesc& dst_16bit, std::uint32_t ss_x,
                           std::uint32_t ss_y) {
    // Y
    std::uint16_t* buffer_16bit = const_cast<std::uint16_t*>(reinterpret_cast<const std::uint16_t*>(dst_16bit.y_buffer));
    std::uint32_t stride_16bit = dst_16bit.y_stride;
    const std::uint8_t* buffer_8bit = src_8bit.y_buffer;
    std::uint32_t stride_8bit = src_8bit.y_stride;
    analysis::packunpack::convert8bitTo16bit(buffer_8bit, stride_8bit, buffer_16bit, stride_16bit, src_8bit.width,
                                             src_8bit.height);

    // Cb
    buffer_16bit = reinterpret_cast<std::uint16_t*>(const_cast<std::uint8_t*>(dst_16bit.u_buffer));
    stride_16bit = dst_16bit.u_stride;
    buffer_8bit = src_8bit.u_buffer;
    stride_8bit = src_8bit.u_stride;
    analysis::packunpack::convert8bitTo16bit(buffer_8bit, stride_8bit, buffer_16bit, stride_16bit,
                                             src_8bit.width >> ss_x, src_8bit.height >> ss_y);

    // Cr
    buffer_16bit = reinterpret_cast<std::uint16_t*>(const_cast<std::uint8_t*>(dst_16bit.v_buffer));
    stride_16bit = dst_16bit.v_stride;
    buffer_8bit = src_8bit.v_buffer;
    stride_8bit = src_8bit.v_stride;
    analysis::packunpack::convert8bitTo16bit(buffer_8bit, stride_8bit, buffer_16bit, stride_16bit,
                                             src_8bit.width >> ss_x, src_8bit.height >> ss_y);

    dst_16bit.width = src_8bit.width;
    dst_16bit.height = src_8bit.height;
}

// svt_aom_copy_buffer_info (pic_operators.c:547-:561), verbatim.
void copyBufferInfo(const PicPackDesc& src_ptr, PicPackDesc& dst_ptr) {
    dst_ptr.width = src_ptr.width;
    dst_ptr.height = src_ptr.height;
    dst_ptr.max_width = src_ptr.max_width;
    dst_ptr.max_height = src_ptr.max_height;
    dst_ptr.y_stride = src_ptr.y_stride;
    dst_ptr.u_stride = src_ptr.u_stride;
    dst_ptr.v_stride = src_ptr.v_stride;
    dst_ptr.border = src_ptr.border;
    dst_ptr.y_stride_bit_inc = src_ptr.y_stride_bit_inc;
    dst_ptr.u_stride_bit_inc = src_ptr.u_stride_bit_inc;
    dst_ptr.v_stride_bit_inc = src_ptr.v_stride_bit_inc;
    dst_ptr.luma_size = src_ptr.luma_size;
    dst_ptr.chroma_size = src_ptr.chroma_size;
}

// svt_aom_pack_highbd_pic (pic_operators.c:563-:603), verbatim over the
// carrier.
void packHighbdPic(const PicPackDesc& pic_ptr, std::uint16_t* const buffer_16bit[3], std::uint32_t ss_x,
                   std::uint32_t ss_y) {
    const std::uint16_t width = static_cast<std::uint16_t>(pic_ptr.y_stride);
    const std::uint16_t height = static_cast<std::uint16_t>(pic_ptr.border * 2 + pic_ptr.height);

    const std::uint32_t comp_stride_y = pic_ptr.y_stride / 4;

    analysis::packunpack::compressedPackMsb(
        pic_ptr.y_buffer - (pic_ptr.border + (pic_ptr.y_stride * pic_ptr.border)), pic_ptr.y_stride,
        pic_ptr.y_buffer_bit_inc - (pic_ptr.border / 4) - (comp_stride_y * pic_ptr.border), comp_stride_y,
        buffer_16bit[0 /*Y*/], pic_ptr.y_stride, width, height);

    if (buffer_16bit[1 /*U*/]) {
        const std::uint32_t comp_stride_cb = pic_ptr.u_stride / 4;
        analysis::packunpack::compressedPackMsb(
            pic_ptr.u_buffer - ((pic_ptr.border >> ss_x) + (pic_ptr.u_stride * (pic_ptr.border >> ss_y))),
            pic_ptr.u_stride,
            pic_ptr.u_buffer_bit_inc - ((pic_ptr.border >> ss_x) / 4 + (comp_stride_cb * (pic_ptr.border >> ss_y))),
            comp_stride_cb, buffer_16bit[1 /*U*/], pic_ptr.u_stride, (width + ss_x) >> ss_x,
            (height + ss_y) >> ss_y);
    }
    if (buffer_16bit[2 /*V*/]) {
        const std::uint32_t comp_stride_cr = pic_ptr.v_stride / 4;
        analysis::packunpack::compressedPackMsb(
            pic_ptr.v_buffer - ((pic_ptr.border >> ss_x) + (pic_ptr.v_stride * (pic_ptr.border >> ss_y))),
            pic_ptr.v_stride,
            pic_ptr.v_buffer_bit_inc - ((pic_ptr.border >> ss_x) / 4 + (comp_stride_cr * (pic_ptr.border >> ss_y))),
            comp_stride_cr, buffer_16bit[2 /*V*/], pic_ptr.v_stride, (width + ss_x) >> ss_x,
            (height + ss_y) >> ss_y);
    }
}

// svt_aom_unpack_highbd_pic (pic_operators.c:605-:645), verbatim over
// the carrier.
void unpackHighbdPic(const std::uint16_t* const buffer_highbd[3], PicPackDesc& pic_ptr, std::uint32_t ss_x,
                     std::uint32_t ss_y) {
    const std::uint16_t width = static_cast<std::uint16_t>(pic_ptr.y_stride);
    const std::uint16_t height = static_cast<std::uint16_t>(pic_ptr.border * 2 + pic_ptr.height);

    const std::uint32_t comp_stride_y = pic_ptr.y_stride / 4;
    const std::uint32_t comp_stride_cb = pic_ptr.u_stride / 4;
    const std::uint32_t comp_stride_cr = pic_ptr.v_stride / 4;

    analysis::packunpack::unpackAnd2bCompress(buffer_highbd[0 /*Y*/], pic_ptr.y_stride,
                                              const_cast<std::uint8_t*>(
                                                  pic_ptr.y_buffer - (pic_ptr.border + (pic_ptr.y_stride * pic_ptr.border))),
                                              pic_ptr.y_stride,
                                              const_cast<std::uint8_t*>(
                                                  pic_ptr.y_buffer_bit_inc - (pic_ptr.border / 4) - (comp_stride_y * pic_ptr.border)),
                                              comp_stride_y, width, height);

    if (buffer_highbd[1 /*U*/]) {
        analysis::packunpack::unpackAnd2bCompress(
            buffer_highbd[1 /*U*/], pic_ptr.u_stride,
            const_cast<std::uint8_t*>(pic_ptr.u_buffer -
                                      ((pic_ptr.border >> ss_x) + (pic_ptr.u_stride * (pic_ptr.border >> ss_y)))),
            pic_ptr.u_stride,
            const_cast<std::uint8_t*>(pic_ptr.u_buffer_bit_inc -
                                      ((pic_ptr.border >> ss_x) / 4 + (comp_stride_cb * (pic_ptr.border >> ss_y)))),
            comp_stride_cb, (width + ss_x) >> ss_x, (height + ss_y) >> ss_y);
    }

    if (buffer_highbd[2 /*V*/]) {
        analysis::packunpack::unpackAnd2bCompress(
            buffer_highbd[2 /*V*/], pic_ptr.v_stride,
            const_cast<std::uint8_t*>(pic_ptr.v_buffer -
                                      ((pic_ptr.border >> ss_x) + (pic_ptr.v_stride * (pic_ptr.border >> ss_y)))),
            pic_ptr.v_stride,
            const_cast<std::uint8_t*>(pic_ptr.v_buffer_bit_inc -
                                      ((pic_ptr.border >> ss_x) / 4 + (comp_stride_cr * (pic_ptr.border >> ss_y)))),
            comp_stride_cr, (width + ss_x) >> ss_x, (height + ss_y) >> ss_y);
    }
}

// ---------------------------------------------------------------------------
// CL2 park resolution: the coding_loop.c remainder.
// ---------------------------------------------------------------------------

// definitions.h:592 svt_log2f = get_msb (floor log2); the tx dims are
// powers of two so the shift is exact.
std::int32_t getMsbLog2(std::uint32_t n) {
    assert(n != 0);
    unsigned long first_set_bit;
    _BitScanReverse(&first_set_bit, n);
    return static_cast<std::int32_t>(first_set_bit);
}

// cfl_idx_to_alpha (intra_prediction.h:134-:141), verbatim over the
// macros (CFL_SIGN_U = ((js+1)*11)>>5, CFL_SIGN_V = (js+1) - 3*sign_u,
// CFL_IDX_U = idx>>4, CFL_IDX_V = idx & 15).
std::int32_t cflIdxToAlpha(std::int32_t alpha_idx, std::int32_t joint_sign, std::int32_t pred_type) {
    constexpr std::int32_t kCflSigns = 3;
    const std::int32_t alpha_sign = (pred_type == 0 /*CFL_PRED_U*/)
        ? (((joint_sign + 1) * 11) >> 5)
        : ((joint_sign + 1) - kCflSigns * (((joint_sign + 1) * 11) >> 5));
    if (alpha_sign == 0 /*CFL_SIGN_ZERO*/) {
        return 0;
    }
    const std::int32_t abs_alpha_q3 =
        (pred_type == 0) ? (alpha_idx >> kCflAlphabetSizeLog2) : (alpha_idx & ((1 << kCflAlphabetSizeLog2) - 1));
    return (alpha_sign == 2 /*CFL_SIGN_POS*/) ? abs_alpha_q3 + 1 : -abs_alpha_q3 - 1;
}

// svt_cfl_luma_subsampling_420_lbd_c (intra_prediction.c:421-:431),
// verbatim.
void cflLumaSubsampling420Lbd(const std::uint8_t* input, std::int32_t input_stride, std::int16_t* output_q3,
                              std::int32_t width, std::int32_t height) {
    for (std::int32_t j = 0; j < height; j += 2) {
        for (std::int32_t i = 0; i < width; i += 2) {
            const std::int32_t bot = i + input_stride;
            output_q3[i >> 1] = static_cast<std::int16_t>((input[i] + input[i + 1] + input[bot] + input[bot + 1]) << 1);
        }
        input += input_stride << 1;
        output_q3 += kCflBufLine;
    }
}

// svt_cfl_luma_subsampling_420_hbd_c (:437-:445), verbatim.
void cflLumaSubsampling420Hbd(const std::uint16_t* input, std::int32_t input_stride, std::int16_t* output_q3,
                              std::int32_t width, std::int32_t height) {
    for (std::int32_t j = 0; j < height; j += 2, input += input_stride << 1, output_q3 += kCflBufLine) {
        for (std::int32_t i = 0; i < width; i += 2) {
            const std::int32_t bot = i + input_stride;
            output_q3[i >> 1] = static_cast<std::int16_t>((input[i] + input[i + 1] + input[bot] + input[bot + 1]) << 1);
        }
    }
}

// svt_subtract_average_c (intra_prediction.c:451-:472), verbatim.
void subtractAverage(std::int16_t* pred_buf_q3, std::int32_t width, std::int32_t height,
                     std::int32_t round_offset, std::int32_t num_pel_log2) {
    std::int32_t sum_q3 = 0;
    std::int16_t* pred_buf = pred_buf_q3;
    for (std::int32_t j = 0; j < height; j++) {
        for (std::int32_t i = 0; i < width; i++) {
            sum_q3 += pred_buf[i];
        }
        pred_buf += kCflBufLine;
    }
    const std::int32_t avg_q3 = (sum_q3 + round_offset) >> num_pel_log2;
    for (std::int32_t j = 0; j < height; j++) {
        for (std::int32_t i = 0; i < width; i++) {
            pred_buf_q3[i] -= static_cast<std::int16_t>(avg_q3);
        }
        pred_buf_q3 += kCflBufLine;
    }
}

// av1_encode_generate_cfl_prediction (coding_loop.c:199-:287), verbatim
// over the carrier; the tx-size dimensions come from the landed l7
// tables and the predict twins from the landed l4.
void generateCflPrediction(const CflPredCarrier& c) {
    const std::uint32_t recon_luma_offset = (c.recon_origin_y * c.recon_y_stride) + c.recon_origin_x;

    // Down sample Luma
    if (c.is_16bit) {
        cflLumaSubsampling420Hbd(reinterpret_cast<const std::uint16_t*>(c.recon_y) + recon_luma_offset,
                                 c.recon_y_stride, c.pred_buf_q3,
                                 c.bwidth_uv == c.bwidth ? (c.bwidth_uv << 1) : c.bwidth,
                                 c.bheight_uv == c.bheight ? (c.bheight_uv << 1) : c.bheight);
    } else {
        cflLumaSubsampling420Lbd(c.recon_y + recon_luma_offset, c.recon_y_stride, c.pred_buf_q3,
                                 c.bwidth_uv == c.bwidth ? (c.bwidth_uv << 1) : c.bwidth,
                                 c.bheight_uv == c.bheight ? (c.bheight_uv << 1) : c.bheight);
    }

    const entropy::TxSize tx_size_uv = av1GetMaxUvTxsize(c.bsize, 1, 1);
    const std::int32_t tx_width_uv = entropy::txSizeWide[tx_size_uv];
    const std::int32_t tx_height_uv = entropy::txSizeHigh[tx_size_uv];

    const std::int32_t round_offset = (tx_width_uv * tx_height_uv) / 2;

    subtractAverage(c.pred_buf_q3, tx_width_uv, tx_height_uv, round_offset,
                    getMsbLog2(static_cast<std::uint32_t>(tx_width_uv)) +
                        getMsbLog2(static_cast<std::uint32_t>(tx_height_uv)));

    const std::int32_t alpha_q3_cb = cflIdxToAlpha(c.cfl_alpha_idx, c.cfl_alpha_signs, 0 /*CFL_PRED_U*/);
    const std::int32_t alpha_q3_cr = cflIdxToAlpha(c.cfl_alpha_idx, c.cfl_alpha_signs, 1 /*CFL_PRED_V*/);

    if (c.is_16bit) {
        intra::cflPredictHbd(c.pred_buf_q3, reinterpret_cast<const std::uint16_t*>(c.pred_u) + c.pred_cb_offset,
                             static_cast<int>(c.pred_u_stride),
                             reinterpret_cast<std::uint16_t*>(c.pred_u) + c.pred_cb_offset,
                             static_cast<int>(c.pred_u_stride), alpha_q3_cb, c.bit_depth, tx_width_uv,
                             tx_height_uv);
        intra::cflPredictHbd(c.pred_buf_q3, reinterpret_cast<const std::uint16_t*>(c.pred_v) + c.pred_cr_offset,
                             static_cast<int>(c.pred_v_stride),
                             reinterpret_cast<std::uint16_t*>(c.pred_v) + c.pred_cr_offset,
                             static_cast<int>(c.pred_v_stride), alpha_q3_cr, c.bit_depth, tx_width_uv,
                             tx_height_uv);
    } else {
        intra::cflPredictLbd(c.pred_buf_q3, c.pred_u + c.pred_cb_offset, static_cast<int>(c.pred_u_stride),
                             c.pred_u + c.pred_cb_offset, static_cast<int>(c.pred_u_stride), alpha_q3_cb, 8,
                             tx_width_uv, tx_height_uv);
        intra::cflPredictLbd(c.pred_buf_q3, c.pred_v + c.pred_cr_offset, static_cast<int>(c.pred_v_stride),
                             c.pred_v + c.pred_cr_offset, static_cast<int>(c.pred_v_stride), alpha_q3_cr, 8,
                             tx_width_uv, tx_height_uv);
    }
}

// svt_aom_store16bit_input_src (coding_loop.c:681-:718), verbatim over
// the flattened planes.
void store16bitInputSrc(const std::uint16_t* src_y, std::uint32_t src_y_stride, const std::uint16_t* src_u,
                        std::uint32_t src_u_stride, const std::uint16_t* src_v, std::uint32_t src_v_stride,
                        std::uint16_t* dst_y, std::uint32_t dst_y_stride, std::uint16_t* dst_u,
                        std::uint32_t dst_u_stride, std::uint16_t* dst_v, std::uint32_t dst_v_stride,
                        std::uint32_t sb_x, std::uint32_t sb_y, std::uint32_t sb_w, std::uint32_t sb_h) {
    const std::uint16_t* from_ptr = src_y;
    std::uint16_t* to_ptr = dst_y + sb_x + (sb_y * dst_y_stride);

    for (std::uint32_t row_it = 0; row_it < sb_h; row_it++) {
        std::memcpy(to_ptr + row_it * dst_y_stride, from_ptr + row_it * src_y_stride, sb_w * 2);
    }

    sb_x = sb_x / 2;
    sb_y = sb_y / 2;
    sb_w = sb_w / 2;
    sb_h = sb_h / 2;

    from_ptr = src_u;
    to_ptr = dst_u + sb_x + (sb_y * dst_u_stride);

    for (std::uint32_t row_it = 0; row_it < sb_h; row_it++) {
        std::memcpy(to_ptr + row_it * dst_u_stride, from_ptr + row_it * src_u_stride, sb_w * 2);
    }

    from_ptr = src_v;
    to_ptr = dst_v + sb_x + (sb_y * dst_v_stride);

    for (std::uint32_t row_it = 0; row_it < sb_h; row_it++) {
        std::memcpy(to_ptr + row_it * dst_v_stride, from_ptr + row_it * src_v_stride, sb_w * 2);
    }
}

// definitions.h:327 ROUND_UV.
std::uint32_t roundUv(std::uint32_t x) {
    return ((x >> 3) << 3);
}

// svt_aom_convert_recon_16bit_to_8bit (coding_loop.c:1075-:1121),
// verbatim over the carrier.
void convertRecon16bitTo8bit(const Recon16To8Carrier& c) {
    const std::uint16_t* dst_16bit =
        reinterpret_cast<const std::uint16_t*>(c.y16) + c.blk_org_x + (c.blk_org_y * c.y16_stride);
    const std::int32_t dst_stride_16bit = static_cast<std::int32_t>(c.y16_stride);
    std::uint8_t* dst = c.y8 + c.blk_org_x + (c.blk_org_y * c.y8_stride);
    std::int32_t dst_stride = static_cast<std::int32_t>(c.y8_stride);

    analysis::packunpack::convert16bitTo8bit(dst_16bit, dst_stride_16bit, dst, dst_stride, c.bwidth, c.bheight);

    // copy recon from 16bit to 8bit (the UV planes)
    const std::uint32_t pred_buf_x_offest = roundUv(c.blk_org_x) >> 1;
    const std::uint32_t pred_buf_y_offest = roundUv(c.blk_org_y) >> 1;

    dst_16bit = reinterpret_cast<const std::uint16_t*>(c.u16) + pred_buf_x_offest +
        (pred_buf_y_offest * c.u16_stride);
    const std::int32_t dst_stride_16bit_u = static_cast<std::int32_t>(c.u16_stride);
    dst = c.u8 + pred_buf_x_offest + (pred_buf_y_offest * c.u8_stride);
    dst_stride = static_cast<std::int32_t>(c.u8_stride);

    analysis::packunpack::convert16bitTo8bit(dst_16bit, dst_stride_16bit_u, dst, dst_stride, c.bwidth_uv,
                                             c.bheight_uv);

    dst_16bit = reinterpret_cast<const std::uint16_t*>(c.v16) +
        (pred_buf_x_offest + (pred_buf_y_offest * c.v16_stride));
    const std::int32_t dst_stride_16bit_v = static_cast<std::int32_t>(c.v16_stride);
    dst = c.v8 + pred_buf_x_offest + (pred_buf_y_offest * c.v8_stride);
    dst_stride = static_cast<std::int32_t>(c.v8_stride);

    analysis::packunpack::convert16bitTo8bit(dst_16bit, dst_stride_16bit_v, dst, dst_stride, c.bwidth_uv,
                                             c.bheight_uv);
}

// copy_recon (coding_loop.c:1346-:1425), verbatim over the carrier.
void copyRecon(const CopyReconCarrier& c) {
    if (c.hbd) {
        const std::uint32_t recon_luma_offset = (c.blk_org_y * c.ep_y_stride) + c.blk_org_x;
        std::uint16_t* ep_recon = reinterpret_cast<std::uint16_t*>(c.ep_y) + recon_luma_offset;
        const std::uint16_t* md_recon = reinterpret_cast<const std::uint16_t*>(c.md_y);

        for (std::uint32_t i = 0; i < c.bheight; i++) {
            std::memcpy(ep_recon + i * c.ep_y_stride, md_recon + i * c.md_y_stride, c.bwidth * sizeof(std::uint16_t));
        }

        if (c.has_uv) {
            const std::uint32_t round_origin_x = roundUv(c.blk_org_x);
            const std::uint32_t round_origin_y = roundUv(c.blk_org_y);

            // Cr
            const std::uint32_t recon_cr_offset = ((round_origin_y >> 1) * c.ep_v_stride) + (round_origin_x >> 1);
            std::uint16_t* ep_recon_cr = reinterpret_cast<std::uint16_t*>(c.ep_v) + recon_cr_offset;
            const std::uint16_t* md_recon_cr = reinterpret_cast<const std::uint16_t*>(c.md_v);

            for (std::uint32_t i = 0; i < c.bheight_uv; i++) {
                std::memcpy(ep_recon_cr + i * c.ep_v_stride, md_recon_cr + i * c.md_v_stride,
                            c.bwidth_uv * sizeof(std::uint16_t));
            }

            // Cb
            const std::uint32_t recon_cb_offset = ((round_origin_y >> 1) * c.ep_u_stride) + (round_origin_x >> 1);
            std::uint16_t* ep_recon_cb = reinterpret_cast<std::uint16_t*>(c.ep_u) + recon_cb_offset;
            const std::uint16_t* md_recon_cb = reinterpret_cast<const std::uint16_t*>(c.md_u);

            for (std::uint32_t i = 0; i < c.bheight_uv; i++) {
                std::memcpy(ep_recon_cb + i * c.ep_u_stride, md_recon_cb + i * c.md_u_stride,
                            c.bwidth_uv * sizeof(std::uint16_t));
            }
        }
    } else {
        const std::uint32_t recon_luma_offset = (c.blk_org_y * c.ep_y_stride) + c.blk_org_x;
        std::uint8_t* ep_recon = c.ep_y + recon_luma_offset;
        const std::uint8_t* md_recon = c.md_y;

        for (std::uint32_t i = 0; i < c.bheight; i++) {
            std::memcpy(ep_recon + i * c.ep_y_stride, md_recon + i * c.md_y_stride, c.bwidth * sizeof(std::uint8_t));
        }

        if (c.has_uv) {
            const std::uint32_t round_origin_x = roundUv(c.blk_org_x);
            const std::uint32_t round_origin_y = roundUv(c.blk_org_y);

            // Cr
            const std::uint32_t recon_cr_offset = ((round_origin_y >> 1) * c.ep_v_stride) + (round_origin_x >> 1);
            std::uint8_t* ep_recon_cr = c.ep_v + recon_cr_offset;
            const std::uint8_t* md_recon_cr = c.md_v;

            for (std::uint32_t i = 0; i < c.bheight_uv; i++) {
                std::memcpy(ep_recon_cr + i * c.ep_v_stride, md_recon_cr + i * c.md_v_stride,
                            c.bwidth_uv * sizeof(std::uint8_t));
            }

            // Cb
            const std::uint32_t recon_cb_offset = ((round_origin_y >> 1) * c.ep_u_stride) + (round_origin_x >> 1);
            std::uint8_t* ep_recon_cb = c.ep_u + recon_cb_offset;
            const std::uint8_t* md_recon_cb = c.md_u;

            for (std::uint32_t i = 0; i < c.bheight_uv; i++) {
                std::memcpy(ep_recon_cb + i * c.ep_u_stride, md_recon_cb + i * c.md_u_stride,
                            c.bwidth_uv * sizeof(std::uint8_t));
            }
        }
    }
}

}  // namespace pd
}  // namespace pipeline
