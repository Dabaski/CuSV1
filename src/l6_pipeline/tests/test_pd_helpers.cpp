// A6P tests: the pd_process.c pure/carrier helpers (PD-series).
// Independent oracles: the relative-dist wrap is checked against a
// test-side modular transcription, the skip-mode gate over the full
// order-hint bands, the ref-mgmt slot state machine over exhaustive
// 8-bit masks, the sframe tables over their full domains, the ref-list
// counts over every ref-count/bipred combination, and the noise/ratio
// bands of ref_pics_modulation.
#include <doctest.h>
#include <cstdint>
#include <cstring>
#include <climits>
#include <vector>

#include <pd_helpers.h>

namespace pd = pipeline::pd;

// ---------------------------------------------------------------------------
// PD1a: get_relative_dist (pd_process.c:87-:102)
// ---------------------------------------------------------------------------

// The reference arithmetic: diff = a - b; m = 1 << (bits-1);
// diff = (diff & (m-1)) - (diff & m). Oracle: restate it here
// independently (not by calling the port).
namespace {

int relativeDistOracle(int bits, int a, int b) {
    int diff = a - b;
    const int m = 1 << (bits - 1);
    diff = (diff & (m - 1)) - (diff & m);
    return diff;
}

}  // namespace

TEST_CASE("l6 pd get_relative_dist disabled order hint returns 0") {
    pd::OrderHintInfo oh;
    oh.enable_order_hint = 0;
    oh.order_hint_bits = 8;
    CHECK(pd::getRelativeDist(&oh, 5, 200) == 0);
    CHECK(pd::getRelativeDist(&oh, 0, 255) == 0);
}

// exhaustive over bits=1..8 against the modular oracle.
TEST_CASE("l6 pd get_relative_dist wrap bands match the modular oracle") {
    for (int bits = 1; bits <= 8; ++bits) {
        const int range = 1 << bits;
        const int step = range <= 16 ? 1 : 3;
        for (int a = 0; a < range; a += step) {
            for (int b = 0; b < range; b += step) {
                pd::OrderHintInfo oh;
                oh.enable_order_hint = 1;
                oh.order_hint_bits = static_cast<std::uint8_t>(bits);
                const int got = pd::getRelativeDist(&oh, a, b);
                const int want = relativeDistOracle(bits, a, b);
                MESSAGE("bits=", bits, " a=", a, " b=", b, " got=", got, " want=", want);
                CHECK(got == want);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// PD1a: svt_av1_setup_skip_mode_allowed (pd_process.c:104-:167)
// ---------------------------------------------------------------------------

// Independent transcription of the reference algorithm (the same rules,
// restated on the raw scalars - not a call into the port).
namespace {

struct SkipInfoOracle {
    int allowed;
    int idx0;
    int idx1;
};

SkipInfoOracle setupSkipModeAllowedOracle(const pd::OrderHintInfo& oh, int slice_type, int reference_mode,
                                          unsigned cur_order_hint, const unsigned* ref_order_hint) {
    const int INVALID = -1;
    SkipInfoOracle out{0, INVALID, INVALID};
    if (!oh.enable_order_hint || slice_type == 0 /*I_SLICE*/ || reference_mode == 0 /*SINGLE_REFERENCE*/) {
        return out;
    }
    const int cur = static_cast<int>(cur_order_hint);
    int ref_hints[2] = {-1, INT_MAX};
    int ref_idx[2] = {INVALID, INVALID};
    for (int i = 0; i < 7; ++i) {
        const int ref_hint = static_cast<int>(ref_order_hint[i]);
        const int d = relativeDistOracle(oh.order_hint_bits, ref_hint, cur);
        if (d < 0) {
            if (ref_hints[0] == -1 || relativeDistOracle(oh.order_hint_bits, ref_hint, ref_hints[0]) > 0) {
                ref_hints[0] = ref_hint;
                ref_idx[0] = i;
            }
        } else if (d > 0) {
            if (ref_hints[1] == INT_MAX || relativeDistOracle(oh.order_hint_bits, ref_hint, ref_hints[1]) < 0) {
                ref_hints[1] = ref_hint;
                ref_idx[1] = i;
            }
        }
    }
    if (ref_idx[0] != INVALID && ref_idx[1] != INVALID) {
        out.allowed = 1;
        out.idx0 = 1 /*LAST_FRAME*/ + (ref_idx[0] < ref_idx[1] ? ref_idx[0] : ref_idx[1]);
        out.idx1 = 1 + (ref_idx[0] > ref_idx[1] ? ref_idx[0] : ref_idx[1]);
    } else if (ref_idx[0] != INVALID && ref_idx[1] == INVALID) {
        ref_hints[1] = -1;
        for (int i = 0; i < 7; ++i) {
            const int ref_hint = static_cast<int>(ref_order_hint[i]);
            if ((ref_hints[0] != -1 && relativeDistOracle(oh.order_hint_bits, ref_hint, ref_hints[0]) < 0) &&
                (ref_hints[1] == -1 || relativeDistOracle(oh.order_hint_bits, ref_hint, ref_hints[1]) > 0)) {
                ref_hints[1] = ref_hint;
                ref_idx[1] = i;
            }
        }
        if (ref_hints[1] != -1) {
            out.allowed = 1;
            out.idx0 = 1 + (ref_idx[0] < ref_idx[1] ? ref_idx[0] : ref_idx[1]);
            out.idx1 = 1 + (ref_idx[0] > ref_idx[1] ? ref_idx[0] : ref_idx[1]);
        }
    }
    return out;
}

pd::PdPcsSkipMode makeSkipPcs(const pd::OrderHintInfo& oh, unsigned cur, const unsigned* refs) {
    pd::PdPcsSkipMode pcs{};
    pcs.order_hint_info = oh;
    pcs.slice_type = pd::kBSlice;
    pcs.reference_mode = pd::kReferenceModeSelect;
    pcs.cur_order_hint = cur;
    std::memcpy(pcs.ref_order_hint, refs, sizeof(pcs.ref_order_hint));
    return pcs;
}

}  // namespace

TEST_CASE("l6 pd setup_skip_mode_allowed early-exit gates") {
    // order hint disabled
    {
        pd::OrderHintInfo oh{0, 8};
        unsigned refs[7] = {200, 200, 200, 200, 200, 200, 200};
        pd::PdPcsSkipMode pcs = makeSkipPcs(oh, 100, refs);
        pd::SkipModeInfo out{9, 9, 9};
        pd::setupSkipModeAllowed(pcs, &out);
        CHECK(out.skip_mode_allowed == 0);
        CHECK(out.ref_frame_idx_0 == pd::kInvalidIdx);
        CHECK(out.ref_frame_idx_1 == pd::kInvalidIdx);
    }
    // I slice
    {
        pd::OrderHintInfo oh{1, 8};
        unsigned refs[7] = {200, 200, 200, 200, 200, 200, 200};
        pd::PdPcsSkipMode pcs = makeSkipPcs(oh, 100, refs);
        pcs.slice_type = pd::kISlice;
        pd::SkipModeInfo out{9, 9, 9};
        pd::setupSkipModeAllowed(pcs, &out);
        CHECK(out.skip_mode_allowed == 0);
    }
    // single reference
    {
        pd::OrderHintInfo oh{1, 8};
        unsigned refs[7] = {200, 200, 200, 200, 200, 200, 200};
        pd::PdPcsSkipMode pcs = makeSkipPcs(oh, 100, refs);
        pcs.reference_mode = pd::kSingleReference;
        pd::SkipModeInfo out{9, 9, 9};
        pd::setupSkipModeAllowed(pcs, &out);
        CHECK(out.skip_mode_allowed == 0);
    }
}

// Full order-hint band: every ref_order_hint configuration at bits=2 (4^7
// x all four current hints) against the transcription oracle.
TEST_CASE("l6 pd setup_skip_mode_allowed matches the oracle over the full bits=2 band") {
    pd::OrderHintInfo oh{1, 2};
    const unsigned range = 4;
    for (unsigned cur = 0; cur < range; ++cur) {
        unsigned refs[7];
        for (unsigned cfg = 0; cfg < 4u * 4u * 4u * 4u * 4u * 4u * 4u; ++cfg) {
            unsigned c = cfg;
            for (int i = 0; i < 7; ++i) {
                refs[i] = c % range;
                c /= range;
            }
            const SkipInfoOracle want = setupSkipModeAllowedOracle(oh, pd::kBSlice, pd::kReferenceModeSelect, cur, refs);
            pd::PdPcsSkipMode pcs = makeSkipPcs(oh, cur, refs);
            pd::SkipModeInfo out{9, 9, 9};
            pd::setupSkipModeAllowed(pcs, &out);
            CHECK(out.skip_mode_allowed == want.allowed);
            CHECK(out.ref_frame_idx_0 == want.idx0);
            CHECK(out.ref_frame_idx_1 == want.idx1);
        }
    }
}

TEST_CASE("l6 pd setup_skip_mode_allowed bits=8 boundary bands") {
    pd::OrderHintInfo oh{1, 8};
    // cur = 128 (the wrap midpoint): hint 127 is nearest forward, 129
    // nearest backward.
    {
        unsigned refs[7] = {128, 128, 128, 127, 128, 129, 128};
        const SkipInfoOracle want = setupSkipModeAllowedOracle(oh, pd::kBSlice, pd::kReferenceModeSelect, 128, refs);
        CHECK(want.allowed == 1);
        CHECK(want.idx0 == pd::kLastFrame + 3);
        CHECK(want.idx1 == pd::kLastFrame + 5);
        pd::PdPcsSkipMode pcs = makeSkipPcs(oh, 128, refs);
        pd::SkipModeInfo out{9, 9, 9};
        pd::setupSkipModeAllowed(pcs, &out);
        CHECK(out.skip_mode_allowed == 1);
        CHECK(out.ref_frame_idx_0 == pd::kLastFrame + 3);
        CHECK(out.ref_frame_idx_1 == pd::kLastFrame + 5);
    }
    // forward only: two forward refs at 120 and 126, cur 128 -> second
    // nearest is 120.
    {
        unsigned refs[7] = {128, 126, 128, 128, 120, 128, 128};
        const SkipInfoOracle want = setupSkipModeAllowedOracle(oh, pd::kBSlice, pd::kReferenceModeSelect, 128, refs);
        CHECK(want.allowed == 1);
        CHECK(want.idx0 == pd::kLastFrame + 1);
        CHECK(want.idx1 == pd::kLastFrame + 4);
        pd::PdPcsSkipMode pcs = makeSkipPcs(oh, 128, refs);
        pd::SkipModeInfo out{9, 9, 9};
        pd::setupSkipModeAllowed(pcs, &out);
        CHECK(out.skip_mode_allowed == 1);
        CHECK(out.ref_frame_idx_0 == pd::kLastFrame + 1);
        CHECK(out.ref_frame_idx_1 == pd::kLastFrame + 4);
    }
}

// ---------------------------------------------------------------------------
// PD1b: svt_aom_get_gm_needed_resolutions (pd_process.c:992-:996),
// svt_aom_is_pic_skipped (:998-:1003), svt_aom_get_max_allocated_me_refs
// (pcs.c:88-:93), svt_get_ref_frame_type (mode_decision.c:279-:284)
// ---------------------------------------------------------------------------

// Full GmDsLevel domain (0..4) - the three booleans per level, restated
// as the truth table.
TEST_CASE("l6 pd get_gm_needed_resolutions truth table over the full domain") {
    // level: full, quart, sixteen (the reference expressions)
    const int table[5][3] = {
        {1, 0, 0},  // GM_FULL
        {0, 1, 0},  // GM_DOWN
        {0, 0, 1},  // GM_DOWN16
        {1, 1, 0},  // GM_ADAPT_0
        {0, 1, 1},  // GM_ADAPT_1
    };
    for (int lvl = 0; lvl <= 4; ++lvl) {
        bool full = false, quart = false, sixteen = false;
        pd::getGmNeededResolutions(static_cast<std::uint8_t>(lvl), &full, &quart, &sixteen);
        CHECK(full == (table[lvl][0] != 0));
        CHECK(quart == (table[lvl][1] != 0));
        CHECK(sixteen == (table[lvl][2] != 0));
    }
}

// 2^3 combination sweep over the is_pic_skipped gate.
TEST_CASE("l6 pd is_pic_skipped gate over all flag combinations") {
    for (int mask = 0; mask < 8; ++mask) {
        const bool is_ref = (mask & 1) != 0;
        const bool rc_stat = (mask & 2) != 0;
        const bool first_frame = (mask & 4) != 0;
        pd::PdPcsSkipFlag pcs{};
        pcs.is_ref = is_ref ? 1 : 0;
        pcs.scs_rc_stat_gen_pass_mode = rc_stat ? 1 : 0;
        pcs.first_frame_in_minigop = first_frame ? 1 : 0;
        const bool want = !is_ref && rc_stat && !first_frame;
        CHECK(pd::isPicSkipped(pcs) == want);
    }
}

// The me-refs allocator over the FULL 4x4 count domain (the callers cap
// the counts at 4, pcs.h ref_count_used_list{0,1}); restated formula.
TEST_CASE("l6 pd get_max_allocated_me_refs over the full 4x4 domain") {
    for (int l0 = 0; l0 <= 4; ++l0) {
        for (int l1 = 0; l1 <= 4; ++l1) {
            std::uint8_t max_ref = 0, max_cand = 0;
            pd::getMaxAllocatedMeRefs(static_cast<std::uint8_t>(l0), static_cast<std::uint8_t>(l1), &max_ref,
                                      &max_cand);
            const int want_ref = l0 + l1;
            // The reference's uint8_t operands promote to int, so
            // (l0 - 1) is -1 at l0 == 0 and the uint8 assignment wraps it
            // to 255 - the oracle models the promotion + truncation.
            const int want_cand = l0 + l1 + (l0 * l1) + (l0 - 1) + (l1 == 3 ? 1 : 0);
            MESSAGE("l0=", l0, " l1=", l1, " ref=", static_cast<int>(max_ref), " want=", want_ref,
                    " cand=", static_cast<int>(max_cand), " want=", want_cand);
            CHECK(max_ref == want_ref);
            CHECK(max_cand == static_cast<std::uint8_t>(want_cand));
        }
    }
}

// svt_get_ref_frame_type (mode_decision.c:279-:284): the to_ref_frame
// table, restated. list0 = {LAST, LAST2, LAST3, GOLD}, list1 = {BWD,
// ALT2, ALT, INVALID_REF}. All eight (list, idx) pairs enumerated.
TEST_CASE("l6 pd get_ref_frame_type table over the full 2x4 domain") {
    const int table[2][4] = {
        {pd::kLastFrame, pd::kLast2Frame, pd::kLast3Frame, pd::kGoldenFrame},
        {pd::kBwdrefFrame, pd::kAltref2Frame, pd::kAltrefFrame, pd::kInvalidRef},
    };
    for (int list = 0; list < 2; ++list) {
        for (int idx = 0; idx < 4; ++idx) {
            CHECK(pd::getRefFrameType(static_cast<std::uint8_t>(list), static_cast<std::uint8_t>(idx)) ==
                  table[list][idx]);
        }
    }
}

// ---------------------------------------------------------------------------
// PD2: the ref-mgmt slot state machine (pd_process.c:1164-:1492)
// ---------------------------------------------------------------------------

namespace {

// Build a PdCtx whose pic_id_per_dpb_slot encodes an 8-bit mask (slot i
// holds pic id 100+i iff bit i is set) and whose dpb pocs are 1000+i.
pd::PdCtx makeCtxFromMask(unsigned mask) {
    pd::PdCtx ctx{};
    for (int i = 0; i < pd::kRefFrames; ++i) {
        if ((mask >> i) & 1u) {
            ctx.pic_id_per_dpb_slot[i] = 100u + static_cast<std::uint32_t>(i);
            ctx.dpb_picture_number[i] = 1000u + static_cast<std::uint32_t>(i);
        } else {
            ctx.pic_id_per_dpb_slot[i] = 0;
            ctx.dpb_picture_number[i] = 0;
        }
    }
    return ctx;
}

// Independent restatement of the exclusive LD-CBR mask
// (pd_process.c:1227-:1259).
unsigned exclusiveMaskOracle(int hier, int ld_reduce) {
    unsigned mask = 0;
    if (ld_reduce == 0) {
        mask |= 0x07u;
    }
    if (hier >= 1) {
        switch (ld_reduce) {
        case 0: mask |= (1u << 3) | (1u << 4); break;
        case 1: mask |= (1u << 3); break;
        case 2: mask |= (1u << 1); break;
        default: break;  // the reference asserts and falls through
        }
    }
    if (hier >= 2 && ld_reduce == 0) {
        mask |= (1u << 5);
    }
    return mask;
}

// Independent restatement of svt_aom_ref_mgmt_storeable_slots_mask
// (:1261-:1278).
unsigned storeableOracle(int rtc, int hier, int pred, int ld_reduce) {
    if (rtc && hier == 0) {
        return 0xF0u;
    }
    if (pred == pd::kPredLowDelay && hier >= 1) {
        return (~exclusiveMaskOracle(hier, ld_reduce)) & 0xFFu;
    }
    return 0xFFu;
}

}  // namespace

// Exhaustive 8-bit sweep: stored_mask derives from pic_id_per_dpb_slot,
// find_slot hits every held id and misses id 0 and a free id.
TEST_CASE("l6 pd ref_mgmt stored_mask + find_slot over all 8-bit masks") {
    for (unsigned mask = 0; mask < 256; ++mask) {
        pd::PdCtx ctx = makeCtxFromMask(mask);
        CHECK(pd::refMgmtStoredMask(ctx) == mask);
        for (int i = 0; i < pd::kRefFrames; ++i) {
            const std::uint32_t id = ctx.pic_id_per_dpb_slot[i];
            const std::uint8_t got = pd::refMgmtFindSlot(ctx, id);
            const std::uint8_t want = (id != 0) ? static_cast<std::uint8_t>(i) : static_cast<std::uint8_t>(pd::kRefFrames);
            CHECK(got == want);
        }
        // id 0 is the no-id sentinel -> REF_FRAMES; a free id misses.
        CHECK(pd::refMgmtFindSlot(ctx, 0) == pd::kRefFrames);
        if (mask == 0) {
            CHECK(pd::refMgmtFindSlot(ctx, 777) == pd::kRefFrames);
        }
    }
}

// Full config domain for the exclusive LD-CBR mask: hier 0..4 x ld_reduce
// 0..3 against the restated oracle.
TEST_CASE("l6 pd exclusive_write_slots_mask_ld_cbr over the full config domain") {
    for (int hier = 0; hier <= 4; ++hier) {
        for (int ld_reduce = 0; ld_reduce <= 3; ++ld_reduce) {
            pd::PdScsCfg scs{};
            scs.hierarchical_levels = static_cast<std::uint8_t>(hier);
            scs.ld_reduce_ref_buffs = static_cast<std::uint8_t>(ld_reduce);
            const unsigned want = exclusiveMaskOracle(hier, ld_reduce);
            CHECK(pd::exclusiveWriteSlotsMaskLdCbr(scs) == want);
        }
    }
}

// Full config domain for the storeable mask: rtc x hier x pred_structure
// x ld_reduce (120 combinations) against the restated oracle.
TEST_CASE("l6 pd ref_mgmt_storeable_slots_mask over the full config domain") {
    for (int rtc = 0; rtc <= 1; ++rtc) {
        for (int hier = 0; hier <= 4; ++hier) {
            for (int pred = 0; pred <= 2; ++pred) {
                for (int ld_reduce = 0; ld_reduce <= 3; ++ld_reduce) {
                    pd::PdScsCfg scs{};
                    scs.rtc = static_cast<std::uint8_t>(rtc);
                    scs.hierarchical_levels = static_cast<std::uint8_t>(hier);
                    scs.pred_structure = static_cast<std::uint8_t>(pred);
                    scs.ld_reduce_ref_buffs = static_cast<std::uint8_t>(ld_reduce);
                    const unsigned want = storeableOracle(rtc, hier, pred, ld_reduce);
                    CHECK(pd::refMgmtStoreableSlotsMask(scs) == want);
                }
            }
        }
    }
}

TEST_CASE("l6 pd apply_ref_clear frees only the held id") {
    pd::PdCtx ctx = makeCtxFromMask(0x2C);  // slots 2,3,5 held
    pd::PdPcsRefMgmt pcs{};
    pcs.picture_number = 42;
    // known id
    pcs.ref_mgmt.clear_id = 100u + 3;
    pd::applyRefClear(pcs, &ctx);
    CHECK(ctx.pic_id_per_dpb_slot[3] == 0);
    CHECK(pd::refMgmtStoredMask(ctx) == 0x24);
    // unknown id: no-op
    pcs.ref_mgmt.clear_id = 777;
    pd::applyRefClear(pcs, &ctx);
    CHECK(pd::refMgmtStoredMask(ctx) == 0x24);
    // id 0 sentinel: no-op
    pcs.ref_mgmt.clear_id = 0;
    pd::applyRefClear(pcs, &ctx);
    CHECK(pd::refMgmtStoredMask(ctx) == 0x24);
}

// Exhaustive mask sweep of apply_ref_store on one fixed storeable
// config, plus the failure modes (duplicate id, cap, full pool).
TEST_CASE("l6 pd apply_ref_store over every stored-slot mask combination") {
    const int kStoreable = 0xC0;  // rtc=0, hier=2, LD, ld_reduce=0 -> ~0x3F & 0xFF
    for (unsigned mask = 0; mask < 256; ++mask) {
        pd::PdCtx ctx = makeCtxFromMask(mask);
        pd::PdPcsRefMgmt pcs{};
        pcs.picture_number = 7;
        pcs.scs.rtc = 0;
        pcs.scs.hierarchical_levels = 2;
        pcs.scs.pred_structure = pd::kPredLowDelay;
        pcs.scs.ld_reduce_ref_buffs = 0;
        pcs.scs.max_managed_refs = 4;
        pcs.ref_mgmt.store_id = 500;
        pcs.av1_ref_signal.refresh_frame_mask = 0x00;
        // Independent outcome restatement.
        std::uint8_t want_slot = static_cast<std::uint8_t>(pd::kRefFrames);
        bool dup = false;
        for (int i = 0; i < pd::kRefFrames; ++i) {
            if (((mask >> i) & 1u) && ctx.pic_id_per_dpb_slot[i] == 500) dup = true;
        }
        const unsigned held = [mask] {
            unsigned c = 0;
            for (int i = 0; i < 8; ++i) c += (mask >> i) & 1u;
            return c;
        }();
        const unsigned free = static_cast<unsigned>(kStoreable) & ~mask;
        if (!dup && held < pcs.scs.max_managed_refs && free != 0) {
            unsigned long k;
            _BitScanForward(&k, free);
            want_slot = static_cast<std::uint8_t>(k);
        }
        const std::uint8_t got = pd::applyRefStore(pcs, &ctx);
        CHECK(got == want_slot);
        if (want_slot < pd::kRefFrames) {
            CHECK((pcs.av1_ref_signal.refresh_frame_mask & (1u << want_slot)) != 0);
            CHECK(ctx.pic_id_per_dpb_slot[want_slot] == 500);
        } else if (dup) {
            // duplicate: no state change
            CHECK(pd::refMgmtStoredMask(ctx) == mask);
            CHECK(pcs.av1_ref_signal.refresh_frame_mask == 0x00);
        }
    }
}

TEST_CASE("l6 pd apply_ref_store cap + full-pool failure modes") {
    // cap: 4 held >= max_managed_refs 4
    {
        pd::PdCtx ctx = makeCtxFromMask(0x0F);
        pd::PdPcsRefMgmt pcs{};
        pcs.scs.max_managed_refs = 4;
        pcs.scs.rtc = 0;
        pcs.scs.hierarchical_levels = 2;
        pcs.scs.pred_structure = pd::kPredLowDelay;
        pcs.scs.ld_reduce_ref_buffs = 0;
        pcs.ref_mgmt.store_id = 900;
        CHECK(pd::applyRefStore(pcs, &ctx) == pd::kRefFrames);
        CHECK(pd::refMgmtStoredMask(ctx) == 0x0F);
    }
    // full safe pool: storeable 0xF0 all held (mask 0xF0), max 8
    {
        pd::PdCtx ctx = makeCtxFromMask(0xF0);
        pd::PdPcsRefMgmt pcs{};
        pcs.scs.rtc = 1;
        pcs.scs.hierarchical_levels = 0;
        pcs.scs.max_managed_refs = 8;
        pcs.ref_mgmt.store_id = 900;
        CHECK(pd::applyRefStore(pcs, &ctx) == pd::kRefFrames);
    }
}

TEST_CASE("l6 pd apply_ref_use redirects all seven positions") {
    for (int slot = 0; slot < pd::kRefFrames; ++slot) {
        pd::PdCtx ctx = makeCtxFromMask(1u << slot);
        pd::PdPcsRefMgmt pcs{};
        pcs.ref_mgmt.use_id = 100u + slot;
        pcs.ref_list0_count = 5;
        pcs.ref_list1_count = 3;
        CHECK(pd::applyRefUse(pcs, ctx));
        for (int i = 0; i < 7; ++i) {
            CHECK(pcs.av1_ref_signal.ref_dpb_index[i] == slot);
            CHECK(pcs.av1_ref_signal.ref_poc_array[i] == 1000u + slot);
        }
        CHECK(pcs.ref_list0_count == 1);
        CHECK(pcs.ref_list1_count == 0);
    }
    // unknown id: false, state unchanged
    {
        pd::PdCtx ctx = makeCtxFromMask(0x0F);
        pd::PdPcsRefMgmt pcs{};
        pcs.ref_mgmt.use_id = 777;
        pcs.ref_list0_count = 5;
        pcs.ref_list1_count = 3;
        CHECK(!pd::applyRefUse(pcs, ctx));
        CHECK(pcs.ref_list0_count == 5);
        CHECK(pcs.ref_list1_count == 3);
    }
}

// The dispatcher: phase order, the three gates, the refresh guard and
// the USE recovery mask - over every event-presence pattern x
// overlay/base x held masks.
TEST_CASE("l6 pd apply_ref_mgmt_events phase order + gates") {
    // a) clear frees a slot the same-frame store then reuses. The safe
    // pool must contain the freed slot and every lower pool slot must be
    // held: rtc=1/hier=0 -> storeable 0xF0, all four held, clear slot 4.
    {
        pd::PdCtx ctx = makeCtxFromMask(0xF0);
        pd::PdPcsRefMgmt pcs{};
        pcs.scs.rtc = 1;
        pcs.scs.hierarchical_levels = 0;
        pcs.scs.max_managed_refs = 8;
        pcs.temporal_layer_index = 0;
        pcs.ref_mgmt.clear_id = 104;   // slot 4
        pcs.ref_mgmt.store_id = 500;   // new id
        pd::applyRefMgmtEvents(pcs, &ctx);
        CHECK(ctx.pic_id_per_dpb_slot[4] == 500);  // reused slot 4
        CHECK(pcs.ref_mgmt.clear_id == 0);
        CHECK(pcs.ref_mgmt.store_id == 500);
        CHECK((pcs.av1_ref_signal.refresh_frame_mask & 0x10) != 0);
    }
    // b) refresh guard: held slot 0 locked out of refresh_frame_mask
    {
        pd::PdCtx ctx = makeCtxFromMask(0x01);
        pd::PdPcsRefMgmt pcs{};
        pcs.temporal_layer_index = 0;
        pcs.av1_ref_signal.refresh_frame_mask = 0xFF;
        pd::applyRefMgmtEvents(pcs, &ctx);
        CHECK(pcs.av1_ref_signal.refresh_frame_mask == 0xFE);
    }
    // c) duplicate pic_id across events: all scrubbed
    {
        pd::PdCtx ctx = makeCtxFromMask(0x00);
        pd::PdPcsRefMgmt pcs{};
        pcs.temporal_layer_index = 0;
        pcs.ref_mgmt.clear_id = 9;
        pcs.ref_mgmt.store_id = 9;
        pcs.ref_mgmt.use_id = 0;
        pd::applyRefMgmtEvents(pcs, &ctx);
        CHECK(pcs.ref_mgmt.clear_id == 0);
        CHECK(pcs.ref_mgmt.store_id == 0);
        CHECK(pcs.ref_mgmt.use_id == 0);
    }
    // d) events on a non-base frame: scrubbed
    {
        pd::PdCtx ctx = makeCtxFromMask(0x00);
        pd::PdPcsRefMgmt pcs{};
        pcs.temporal_layer_index = 1;
        pcs.ref_mgmt.store_id = 5;
        pd::applyRefMgmtEvents(pcs, &ctx);
        CHECK(pcs.ref_mgmt.store_id == 0);
        CHECK(pd::refMgmtStoredMask(ctx) == 0);
    }
    // e) events on an overlay frame: scrubbed
    {
        pd::PdCtx ctx = makeCtxFromMask(0x00);
        pd::PdPcsRefMgmt pcs{};
        pcs.temporal_layer_index = 0;
        pcs.is_overlay = 1;
        pcs.ref_mgmt.use_id = 5;
        pd::applyRefMgmtEvents(pcs, &ctx);
        CHECK(pcs.ref_mgmt.use_id == 0);
    }
    // f) USE recovery: every non-STOREd slot + the new store slot gets
    // the current frame.
    {
        pd::PdCtx ctx = makeCtxFromMask(0x02);  // slot 1 held
        ctx.dpb_picture_number[6] = 6000;       // the slot STORE will pick
        pd::PdPcsRefMgmt pcs{};
        pcs.scs.rtc = 0;
        pcs.scs.hierarchical_levels = 2;
        pcs.scs.pred_structure = pd::kPredLowDelay;
        pcs.scs.ld_reduce_ref_buffs = 0;
        pcs.scs.max_managed_refs = 4;
        pcs.temporal_layer_index = 0;
        pcs.ref_mgmt.store_id = 500;  // new id -> the safe pool's slot 6
        pcs.ref_mgmt.use_id = 101;    // the pre-held id (slot 1) - a
                                      // distinct id, else the collision
                                      // gate scrubs everything
        pd::applyRefMgmtEvents(pcs, &ctx);
        // storeable 0xC0 -> lowest free slot is 6
        CHECK(ctx.pic_id_per_dpb_slot[6] == 500);
        // recovery refresh = 0xFF & ~stored(0x42) | (1<<6)
        CHECK(pcs.av1_ref_signal.refresh_frame_mask == 0xFD);
        // the seven positions resolve to the USEd slot 1 with its dpb poc
        for (int i = 0; i < 7; ++i) {
            CHECK(pcs.av1_ref_signal.ref_dpb_index[i] == 1);
            CHECK(pcs.av1_ref_signal.ref_poc_array[i] == 1001u);
        }
    }
    // g) event-less frame still runs the refresh guard
    {
        pd::PdCtx ctx = makeCtxFromMask(0x81);
        pd::PdPcsRefMgmt pcs{};
        pcs.temporal_layer_index = 0;
        pcs.av1_ref_signal.refresh_frame_mask = 0xFF;
        pd::applyRefMgmtEvents(pcs, &ctx);
        CHECK(pcs.av1_ref_signal.refresh_frame_mask == 0x7E);
    }
}

TEST_CASE("l6 pd set_key_frame_rps resets the ref-mgmt state") {
    pd::PdCtx ctx = makeCtxFromMask(0x55);
    ctx.lay0_toggle = 2;
    ctx.lay1_toggle = 1;
    pd::PdPcsRefMgmt pcs{};
    pcs.show_frame = false;
    pcs.has_show_existing = true;
    pd::setKeyFrameRps(pcs, &ctx);
    CHECK(ctx.lay0_toggle == 0);
    CHECK(ctx.lay1_toggle == 0);
    CHECK(pd::refMgmtStoredMask(ctx) == 0);
    CHECK(pcs.show_frame);
    CHECK(!pcs.has_show_existing);
}

// ---------------------------------------------------------------------------
// PD3: set_frame_display_params (pd_process.c:1134-:1162) + the sframe
// helpers (:1496-:1571)
// ---------------------------------------------------------------------------

// The display branch matrix: pred_type x is_overlay x slice_type x
// broken/complete mini-gop.
TEST_CASE("l6 pd set_frame_display_params branch matrix") {
    for (int pred_type = 0; pred_type <= 2; ++pred_type) {
        for (int overlay = 0; overlay <= 1; ++overlay) {
            for (int slice = 0; slice <= 2; ++slice) {
                for (int broken = 0; broken <= 1; ++broken) {
                    pd::PdPcsDisplay pcs{};
                    pcs.pred_type = static_cast<std::uint8_t>(pred_type);
                    pcs.is_overlay = static_cast<std::uint8_t>(overlay);
                    pcs.slice_type = static_cast<std::uint8_t>(slice);
                    pcs.show_frame = 7;
                    pcs.has_show_existing = 7;
                    // entry_count 8; mini_gop_length 4 (broken) or 8 (complete)
                    pcs.pred_struct_entry_count = 8;
                    std::uint32_t mg_length[4] = {broken ? 4u : 8u, 0, 0, 0};
                    pd::PdDisplayCtx ctx{};
                    ctx.mini_gop_length = mg_length;
                    const bool handled =
                        pd::setFrameDisplayParams(pcs, ctx, 0 /*mini_gop_index*/);
                    // Oracle: LD or overlay -> show, handled; I-slice ->
                    // broken -> show/handled, complete -> no-show/handled;
                    // non-I non-LD non-overlay -> return false untouched.
                    if (pred_type == pd::kPredLowDelay || overlay) {
                        CHECK(handled);
                        CHECK(pcs.show_frame == 1);
                        CHECK(pcs.has_show_existing == 0);
                    } else if (slice == pd::kISlice) {
                        CHECK(handled);
                        if (broken) {
                            CHECK(pcs.show_frame == 1);
                            CHECK(pcs.has_show_existing == 0);
                        } else {
                            CHECK(pcs.show_frame == 0);
                            CHECK(pcs.has_show_existing == 0);
                        }
                    } else {
                        CHECK(!handled);
                        CHECK(pcs.show_frame == 7);  // untouched
                        CHECK(pcs.has_show_existing == 7);
                    }
                }
            }
        }
    }
}

// get_dist_to_s over the position-band domain: expired spots, a gap, the
// exact spot with and without a next spot, and the multi-spot walk.
TEST_CASE("l6 pd get_dist_to_s position bands") {
    // all expired
    {
        pd::SFramePositions posi{};
        std::uint64_t spots[2] = {3, 7};
        posi.sframe_num = 2;
        posi.sframe_posis = spots;
        std::int32_t next = 99;
        CHECK(pd::getDistToS(posi, 10, &next) == -1);
        CHECK(next == -1);
    }
    // gap: current before the first spot
    {
        pd::SFramePositions posi{};
        std::uint64_t spots[2] = {10, 14};
        posi.sframe_num = 2;
        posi.sframe_posis = spots;
        std::int32_t next = 99;
        CHECK(pd::getDistToS(posi, 8, &next) == 2);
        CHECK(next == -1);  // not an s-frame itself
    }
    // exact match with a next spot
    {
        pd::SFramePositions posi{};
        std::uint64_t spots[2] = {10, 14};
        posi.sframe_num = 2;
        posi.sframe_posis = spots;
        std::int32_t next = 99;
        CHECK(pd::getDistToS(posi, 10, &next) == 0);
        CHECK(next == 4);
    }
    // exact match on the LAST spot -> dist_to_next stays -1
    {
        pd::SFramePositions posi{};
        std::uint64_t spots[2] = {10, 14};
        posi.sframe_num = 2;
        posi.sframe_posis = spots;
        std::int32_t next = 99;
        CHECK(pd::getDistToS(posi, 14, &next) == 0);
        CHECK(next == -1);
    }
    // unsorted spots: the walk returns at the FIRST position >=
    // picture_num - spots[0] = 20 wins over spots[1] = 5.
    {
        pd::SFramePositions posi{};
        std::uint64_t spots[3] = {20, 5, 30};
        posi.sframe_num = 3;
        posi.sframe_posis = spots;
        std::int32_t next = 99;
        CHECK(pd::getDistToS(posi, 5, &next) == 15);
        CHECK(next == -1);
    }
}

// get_sframe_qp / get_sframe_qp_offset over the null/found/miss domains.
TEST_CASE("l6 pd sframe qp tables over the null + lookup domains") {
    pd::SFramePositions posi{};
    std::uint64_t spots[3] = {2, 5, 9};
    std::uint8_t qps[3] = {31, 42, 55};
    std::int8_t offs[3] = {-2, 0, 3};
    posi.sframe_num = 3;
    posi.sframe_posis = spots;
    posi.sframe_qps = qps;
    posi.sframe_qp_offsets = offs;
    // null qps -> 0
    {
        pd::SFramePositions n{};
        n.sframe_num = 3;
        n.sframe_posis = spots;
        CHECK(pd::getSframeQp(n, 5) == 0);
        CHECK(pd::getSframeQpOffset(n, 5) == 0);
    }
    // null posis -> always the first entry
    {
        pd::SFramePositions n{};
        n.sframe_num = 3;
        n.sframe_qps = qps;
        n.sframe_qp_offsets = offs;
        CHECK(pd::getSframeQp(n, 12345) == 31);
        CHECK(pd::getSframeQpOffset(n, 12345) == -2);
    }
    // found / not found
    CHECK(pd::getSframeQp(posi, 2) == 31);
    CHECK(pd::getSframeQp(posi, 5) == 42);
    CHECK(pd::getSframeQp(posi, 9) == 55);
    CHECK(pd::getSframeQp(posi, 6) == 0);
    CHECK(pd::getSframeQpOffset(posi, 2) == -2);
    CHECK(pd::getSframeQpOffset(posi, 5) == 0);
    CHECK(pd::getSframeQpOffset(posi, 9) == 3);
    CHECK(pd::getSframeQpOffset(posi, 6) == 0);
}

// sframe_position_offset over the full mode x pred-structure domain.
TEST_CASE("l6 pd sframe_position_offset full domain") {
    for (int mode = 0; mode <= 4; ++mode) {
        for (int pred = 0; pred <= 2; ++pred) {
            pd::PdScsCfg scs{};
            scs.pred_structure = static_cast<std::uint8_t>(pred);
            const std::uint8_t mode_val = static_cast<std::uint8_t>(mode);  // EbSFrameMode ordinals 0..4
            const int want = (mode_val == pd::kSframeDecPosiBase && pred == pd::kPredRandomAccess) ? 1 : 0;
            CHECK(pd::sframePositionOffset(scs, mode_val) == want);
        }
    }
}

// ---------------------------------------------------------------------------
// PD4: svt_aom_is_pic_used_as_ref (:1772-:1804) + set_ref_list_counts
// (:1806-:1901) + update_ref_poc_array (:1903-:1911)
// ---------------------------------------------------------------------------

// is_pic_used_as_ref: full hier x scheme x index x overlay domain with
// the reference switch restated.
TEST_CASE("l6 pd is_pic_used_as_ref over the full hier/scheme domain") {
    for (int hier = 0; hier <= 6; ++hier) {
        for (unsigned tl = 0; tl <= 4; ++tl) {
            for (unsigned idx : {0u, 1u, 8u}) {
                for (unsigned scheme = 0; scheme <= 2; ++scheme) {
                    for (int overlay = 0; overlay <= 1; ++overlay) {
                        bool want;
                        if (overlay) {
                            want = false;
                        } else if (tl < static_cast<unsigned>(hier)) {
                            want = true;
                        } else {
                            switch (hier) {
                            case 0: want = true; break;
                            case 1: want = scheme != 0; break;
                            case 2:
                            case 3: want = scheme == 0 ? false : (scheme == 1 ? true : idx == 0); break;
                            case 4:
                                want = scheme == 0 ? false : (scheme == 1 ? true : (idx == 0 || idx == 8));
                                break;
                            case 5: want = false; break;
                            default: want = true; break;  // the assert branch falls to true
                            }
                        }
                        CHECK(pd::isPicUsedAsRef(static_cast<std::uint32_t>(hier), tl, idx, scheme,
                                                 overlay != 0) == want);
                    }
                }
            }
        }
    }
}

// set_ref_list_counts: every POC-alphabet configuration (3^7) x the mrp
// cap domain x the base flag, against a restated walk. The S-frame skip
// branch is exercised in its own case below.
namespace {

// Independent transcription of the reference walk (pd_process.c:1806-:1901).
struct RefCounts {
    int list0;
    int list1;
};

RefCounts refListCountsOracle(int slice_type, const std::uint64_t* poc, int base_list0, int base_list1,
                              int nonbase_list0, int nonbase_list1, bool is_base, bool incomp_mg, bool overlay,
                              int pred_structure, std::uint64_t picture_number, std::uint64_t sframe_poc) {
    if (slice_type == pd::kISlice) {
        return {0, 0};
    }
    int list0_count = 1;
    bool breakout = false;
    for (int i = 1; i <= 3; ++i) {  // LAST2..GOLD
        if (breakout) break;
        for (int j = 0; j < i; ++j) {
            if (poc[i] == poc[j]) {
                breakout = true;
                break;
            }
        }
        if (!breakout) list0_count++;
    }
    const int l0 = list0_count < (is_base ? base_list0 : nonbase_list0)
        ? list0_count
        : (is_base ? base_list0 : nonbase_list0);
    if (incomp_mg || overlay) {
        return {l0, 0};
    }
    int list1_count = 0;
    breakout = false;
    for (int i = 4; i <= 6; ++i) {  // BWD..ALT
        if (breakout) break;
        for (int j = (i == 4) ? 1 : 0; j < i; ++j) {
            if (j <= 3 && j + 1 > l0) continue;
            if (pred_structure == pd::kPredRandomAccess && picture_number < sframe_poc && j <= 3 &&
                poc[j] == sframe_poc) {
                continue;
            }
            if (poc[i] == poc[j]) {
                breakout = true;
                break;
            }
        }
        if (!breakout) list1_count++;
    }
    const int l1 = list1_count < (is_base ? base_list1 : nonbase_list1)
        ? list1_count
        : (is_base ? base_list1 : nonbase_list1);
    return {l0, l1};
}

pd::PdPcsRefCounts makeCountsPcs(int slice_type, const std::uint64_t* poc, int base_list0, int base_list1,
                                 int nonbase_list0, int nonbase_list1, bool is_base, bool incomp_mg, bool overlay,
                                 int pred_structure, std::uint64_t picture_number, std::uint64_t sframe_poc) {
    pd::PdPcsRefCounts pcs{};
    pcs.slice_type = static_cast<std::uint8_t>(slice_type);
    std::memcpy(pcs.ref_poc_array, poc, sizeof(pcs.ref_poc_array));
    pcs.base_ref_list0_count = static_cast<std::uint8_t>(base_list0);
    pcs.base_ref_list1_count = static_cast<std::uint8_t>(base_list1);
    pcs.non_base_ref_list0_count = static_cast<std::uint8_t>(nonbase_list0);
    pcs.non_base_ref_list1_count = static_cast<std::uint8_t>(nonbase_list1);
    pcs.is_base = is_base ? 1 : 0;
    pcs.is_incomp_mg = incomp_mg ? 1 : 0;
    pcs.is_overlay = static_cast<std::uint8_t>(overlay);
    pcs.pred_structure = static_cast<std::uint8_t>(pred_structure);
    pcs.picture_number = picture_number;
    pcs.sframe_poc = sframe_poc;
    return pcs;
}

}  // namespace

TEST_CASE("l6 pd set_ref_list_counts over the POC-alphabet x cap domain") {
    const std::uint64_t alphabet[3] = {0, 77, 1234};
    for (unsigned cfg = 0; cfg < 3u * 3u * 3u * 3u * 3u * 3u * 3u; ++cfg) {
        std::uint64_t poc[7];
        unsigned c = cfg;
        for (int i = 0; i < 7; ++i) {
            poc[i] = alphabet[c % 3];
            c /= 3;
        }
        for (int cap = 0; cap <= 4; ++cap) {
            for (int is_base = 0; is_base <= 1; ++is_base) {
                const pd::PdPcsRefCounts pcs = makeCountsPcs(
                    pd::kBSlice, poc, cap + 1, cap + 1, cap, cap, is_base != 0, false, false,
                    pd::kPredLowDelay, 1000, 0);
                const RefCounts want = refListCountsOracle(
                    pd::kBSlice, poc, cap + 1, cap + 1, cap, cap, is_base != 0, false, false,
                    pd::kPredLowDelay, 1000, 0);
                pd::PdPcsRefCounts out = pcs;
                pd::setRefListCounts(out);
                CHECK(out.ref_list0_count == want.list0);
                CHECK(out.ref_list1_count == want.list1);
            }
        }
    }
}

TEST_CASE("l6 pd set_ref_list_counts I-slice + incomp-mg + overlay branches") {
    std::uint64_t poc[7] = {10, 11, 12, 13, 14, 15, 16};
    // I slice: (0, 0)
    {
        pd::PdPcsRefCounts out = makeCountsPcs(pd::kISlice, poc, 4, 4, 4, 4, true, false, false,
                                               pd::kPredLowDelay, 0, 0);
        pd::setRefListCounts(out);
        CHECK(out.ref_list0_count == 0);
        CHECK(out.ref_list1_count == 0);
    }
    // incomplete MG frame: list1 forced 0
    {
        pd::PdPcsRefCounts out = makeCountsPcs(pd::kBSlice, poc, 4, 4, 4, 4, true, true, false,
                                               pd::kPredRandomAccess, 0, 0);
        pd::setRefListCounts(out);
        CHECK(out.ref_list0_count == 4);
        CHECK(out.ref_list1_count == 0);
    }
    // overlay: list1 forced 0
    {
        pd::PdPcsRefCounts out = makeCountsPcs(pd::kBSlice, poc, 4, 4, 4, 4, true, false, true,
                                               pd::kPredLowDelay, 0, 0);
        pd::setRefListCounts(out);
        CHECK(out.ref_list0_count == 4);
        CHECK(out.ref_list1_count == 0);
    }
    // is_incomp_mg_frame predicate itself: LOW_DELAY pred_type under RA
    // pred structure
    CHECK(pd::isIncompMgFrame(pd::kPredLowDelay, pd::kPredRandomAccess));
    CHECK(!pd::isIncompMgFrame(pd::kPredLowDelay, pd::kPredLowDelay));
    CHECK(!pd::isIncompMgFrame(pd::kPredRandomAccess, pd::kPredRandomAccess));
}

TEST_CASE("l6 pd set_ref_list_counts s-frame RA skip branch") {
    // RA + picture_number < sframe_poc + a GOLD-position poc equal to the
    // sframe poc: the duplicate is SKIPPED, list1 keeps counting.
    std::uint64_t poc[7] = {50, 51, 52, 53, 54, 55, 56};
    poc[3] = 500;  // GOLD == sframe_poc
    poc[4] = 500;  // BWD duplicates GOLD -> skipped by the s-frame branch
    {
        pd::PdPcsRefCounts out = makeCountsPcs(pd::kBSlice, poc, 4, 4, 4, 4, true, false, false,
                                               pd::kPredRandomAccess, 100, 500);
        pd::setRefListCounts(out);
        // Oracle: j = 3 (GOLD) has poc 500 == sframe_poc and RA s-frame
        // gate -> continue; j = 0..2 do not match 500; so BWD counts.
        CHECK(out.ref_list1_count >= 1);
        const RefCounts want = refListCountsOracle(pd::kBSlice, poc, 4, 4, 4, 4, true, false, false,
                                                   pd::kPredRandomAccess, 100, 500);
        CHECK(out.ref_list1_count == want.list1);
    }
    // Same array OUTSIDE the s-frame window (picture_number > sframe_poc):
    // the dup is a real breakout.
    {
        pd::PdPcsRefCounts out = makeCountsPcs(pd::kBSlice, poc, 4, 4, 4, 4, true, false, false,
                                               pd::kPredRandomAccess, 600, 500);
        pd::setRefListCounts(out);
        const RefCounts want = refListCountsOracle(pd::kBSlice, poc, 4, 4, 4, 4, true, false, false,
                                                   pd::kPredRandomAccess, 600, 500);
        CHECK(out.ref_list1_count == want.list1);
        CHECK(out.ref_list1_count == 0);
    }
}

// update_ref_poc_array: the seven reads over a swept dpb.
TEST_CASE("l6 pd update_ref_poc_array reads the dpb positions") {
    std::uint64_t dpb_poc[8];
    for (int i = 0; i < 8; ++i) dpb_poc[i] = 3000u + static_cast<std::uint32_t>(i) * 7u;
    for (unsigned mask = 0; mask < 256; ++mask) {
        std::uint8_t idx[7];
        unsigned c = mask;
        for (int i = 0; i < 7; ++i) {
            idx[i] = static_cast<std::uint8_t>(c % 8);
            c /= 8;
        }
        std::uint64_t poc[7] = {0, 0, 0, 0, 0, 0, 0};
        pd::updateRefPocArray(idx, poc, dpb_poc);
        for (int i = 0; i < 7; ++i) {
            CHECK(poc[i] == dpb_poc[idx[i]]);
        }
    }
}
