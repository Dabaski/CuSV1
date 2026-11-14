// l6_pipeline - the pd_process.c helpers (A6, PD-series). See
// pd_helpers.h for the park list and carrier conventions.
#include <pd_helpers.h>

#include <cassert>
#include <climits>

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

}  // namespace pd
}  // namespace pipeline
