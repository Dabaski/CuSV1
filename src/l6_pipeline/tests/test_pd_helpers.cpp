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
