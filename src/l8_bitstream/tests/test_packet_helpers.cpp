#include <doctest.h>

#include <cstring>

#include "packet_helpers.h"

// PK1 - the A7P packetization pure-surface tests. Independent oracles +
// exhaustive enumerations + poison-filled carriers. The parks list (the
// named walls this file does NOT test, per packet_helpers.h):
//   - packetization_reorder_queue.c:15-26 (EB_NEW/EB_DELETE entry ctor/dctor
//     shells)
//   - the packetization kernel driver packetization_process.c:459-959
//     (fifos/mutexes/EbObjectWrapper lifetimes; the superres recode policy
//     :483-587; the RC/first-pass accounting :839-859; speed control
//     :880-885; release_references_eos :393-438)
//   - collect_frames_info :272-298 (the svt_av1_get_time timing wall)
//   - print_detailed_frame_info :159-270 (DETAILED_FRAME_OUTPUT debug log)
//   - the push/pop overflow SVT_ERROR + EB_FREE + svt_release_object drop
//     shells :127-132/:142-144
//   - bitstream_unit.c's od_ec_enc family (landed in l7_entropy - NOT
//     re-ported); output_bitstream_unit_dctor :24-27 (vector owns);
//     svt_aom_bitstream_ctor EB_NEW shell (entropy_coding.c:1258-1262)

namespace bitstream {}

TEST_CASE("PK1 S2: output bitstream unit ctor sizes and reset rewinds") {
    // svt_aom_output_bitstream_unit_ctor (bitstream_unit.c:32-45): size 0 ->
    // null buffer + size 0; else the allocation. svt_aom_output_bitstream_
    // reset (:50-57): the write pointer back to the beginning. Poison: the
    // reset must not move the BEGIN pointer or the size.
    bitstream::OutputBitstreamUnit unit{};
    CHECK(bitstream::outputBitstreamUnitCtor(&unit, 0) == 0);  // EB_ErrorNone
    CHECK(unit.size == 0);
    CHECK(unit.buffer_begin_av1 == nullptr);
    CHECK(unit.buffer_av1 == nullptr);
    CHECK(bitstream::outputBitstreamUnitCtor(&unit, 16) == 0);
    CHECK(unit.size == 16);
    REQUIRE(unit.buffer_begin_av1 != nullptr);
    CHECK(unit.buffer_av1 == unit.buffer_begin_av1);
    unit.buffer_av1 += 5;  // pretend 5 bytes were written
    CHECK(bitstream::outputBitstreamReset(&unit) == 0);
    CHECK(unit.buffer_av1 == unit.buffer_begin_av1);
    CHECK(unit.size == 16);
    // the zero ctor's branch is not re-entered by reset (write ptr == begin)
    CHECK(bitstream::outputBitstreamReset(&unit) == 0);
}

TEST_CASE("PK1 S2: realloc preserves the write offset; bytes-count/copy/readback") {
    // svt_realloc_output_bitstream_unit (bitstream_unit.c:64-74): offset =
    // buffer_av1 - buffer_begin_av1 survives the grow (sz > 0 gate :65);
    // sz == 0 is a no-op. The Bitstream helpers (entropy_coding.c:
    // 1264-1276): reset -> outputBitstreamReset, get_bytes_count =
    // (int)(buffer_av1 - buffer_begin_av1), copy = memcpy from the
    // beginning.
    bitstream::OutputBitstreamUnit unit{};
    REQUIRE(bitstream::outputBitstreamUnitCtor(&unit, 8) == 0);
    unit.buffer_av1 = unit.buffer_begin_av1 + 6;
    for (int i = 0; i < 6; ++i) unit.buffer_begin_av1[i] = (std::uint8_t)(0x10 * (i + 1));
    CHECK(bitstream::reallocOutputBitstreamUnit(&unit, 32) == 0);
    CHECK(unit.size == 32);
    REQUIRE(unit.buffer_begin_av1 != nullptr);
    CHECK(unit.buffer_av1 == unit.buffer_begin_av1 + 6);  // offset preserved
    for (int i = 0; i < 6; ++i) CHECK(unit.buffer_begin_av1[i] == (std::uint8_t)(0x10 * (i + 1)));
    // no-op: sz == 0
    CHECK(bitstream::reallocOutputBitstreamUnit(&unit, 0) == 0);
    CHECK(unit.size == 32);
    // null unit: no-op, no crash
    CHECK(bitstream::reallocOutputBitstreamUnit(nullptr, 16) == 0);
    // the Bitstream wrapper helpers
    bitstream::Bitstream bs{};
    bs.output_bitstream_ptr = &unit;
    CHECK(bitstream::bitstreamGetBytesCount(&bs) == 6);
    unsigned char out[16];
    memset(out, 0x55, sizeof(out));
    bitstream::bitstreamCopy(&bs, out, 6);
    for (int i = 0; i < 6; ++i) CHECK(out[i] == (std::uint8_t)(0x10 * (i + 1)));
    CHECK(out[6] == 0x55);  // the copy extent ends at size
    bitstream::bitstreamReset(&bs);
    CHECK(unit.buffer_av1 == unit.buffer_begin_av1);
    CHECK(bitstream::bitstreamGetBytesCount(&bs) == 0);
}

TEST_CASE("PK1 S2: start_encode binds the EC; ensure_capacity grows mid-stream") {
    // aom_start_encode (bitstream_unit.h:230-236): pos = 0, the EC borrows
    // the unit's buffer, state reset (rng 0x8000, cnt -9, low 0).
    // svt_aom_ec_ensure_capacity (bitstream_unit.c:210-228): needed =
    // offs + min_free; grow + re-bind the borrowed pointers past the gate.
    // Oracle: a 200-symbol bool roundtrip through TWO forced growths,
    // decoded with the l7 reader at the same f sequence.
    bitstream::OutputBitstreamUnit unit{};
    REQUIRE(bitstream::outputBitstreamUnitCtor(&unit, 8) == 0);
    entropy::AomWriter w{};
    bitstream::aomStartEncode(&w, &unit);
    CHECK(w.pos == 0);
    CHECK(w.ec.buf == unit.buffer_begin_av1);
    CHECK(w.ec.rng == 0x8000);
    CHECK(w.ec.cnt == -9);
    CHECK(w.ec.low == 0);
    CHECK(w.ec.error == 0);
    static const unsigned fs[3] = {16384, 8192, 24576};  // 1/2, 1/4, 3/4
    static const int vals[3] = {1, 0, 1};
    const int n = 200;
    int grew = 0;
    for (int i = 0; i < n; ++i) {
        entropy::odEcEncodeBoolQ15(&w.ec, vals[i % 3], fs[i % 3]);
        if (i % 8 == 7) {
            const std::uint32_t before = unit.size;
            CHECK(bitstream::ecEnsureCapacity(&w, &unit, 16) == 0);
            CHECK(w.ec.error == 0);
            if (unit.size != before) ++grew;
            CHECK(w.ec.buf == unit.buffer_begin_av1);
        }
    }
    CHECK(grew >= 2);  // the 8-byte start capacity cannot hold 25 bytes
    entropy::odEcStopEncode(&w);
    CHECK(w.pos > 0);
    // decode the same f/val sequence
    entropy::OdEcDec dec{};
    entropy::odEcDecInit(&dec, unit.buffer_begin_av1, w.pos);
    for (int i = 0; i < n; ++i)
        CHECK(entropy::odEcDecodeBoolQ15(&dec, fs[i % 3]) == vals[i % 3]);
    // capacity satisfied -> no grow, error stays clean
    const std::uint32_t szBefore = unit.size;
    CHECK(bitstream::ecEnsureCapacity(&w, &unit, 1) == 0);
    CHECK(unit.size == szBefore);
    CHECK(w.ec.error == 0);
}

TEST_CASE("PK1 S1: reorder queue pos wraps head+delta modulo size") {
    // get_reorder_queue_pos (packetization_process.c:81-83):
    // (head_index + delta) % queue_size.
    bitstream::EncodeContext ctx{};
    ctx.packetization_reorder_queue_size = 8;
    ctx.packetization_reorder_queue_head_index = 6;
    CHECK(bitstream::getReorderQueuePos(&ctx, 0) == 6);
    CHECK(bitstream::getReorderQueuePos(&ctx, 3) == 1);   // 9 % 8
    CHECK(bitstream::getReorderQueuePos(&ctx, -2) == 4);  // 4
    ctx.packetization_reorder_queue_head_index = 0;
    CHECK(bitstream::getReorderQueuePos(&ctx, 8) == 0);   // full wrap
    CHECK(bitstream::getReorderQueuePos(&ctx, 9) == 1);
}

TEST_CASE("PK1 S1: queue entry resolves the wrapped slot pointer") {
    // get_reorder_queue_entry (packetization_process.c:85-88): the entry
    // pointer at get_reorder_queue_pos(delta). Poison-filled carriers: the
    // slots the call must not touch stay poisoned.
    static bitstream::PacketizationReorderEntry slots[4];
    bitstream::PacketizationReorderEntry* queue[4] = {&slots[0], &slots[1], &slots[2], &slots[3]};
    for (int i = 0; i < 4; ++i) {
        memset(&slots[i], 0, sizeof(slots[i]));
        slots[i].picture_number = 100 + i;
    }
    bitstream::EncodeContext ctx{};
    ctx.packetization_reorder_queue = queue;
    ctx.packetization_reorder_queue_size = 4;
    ctx.packetization_reorder_queue_head_index = 2;
    CHECK(bitstream::getReorderQueueEntry(&ctx, 0) == &slots[2]);
    CHECK(bitstream::getReorderQueueEntry(&ctx, 2) == &slots[0]);  // 4 % 4
    CHECK(bitstream::getReorderQueueEntry(&ctx, 3) == &slots[1]);  // 5 % 4
    CHECK(bitstream::getReorderQueueEntry(&ctx, 3)->picture_number == 101);
}

TEST_CASE("PK1 S1: pts_descend orders descending and truncates int-diffs") {
    // pts_descend (packetization_process.c:117-123): (int)(bb->pts -
    // ba->pts). Independent oracle: the comparator's own sign convention -
    // negative when a's pts is LARGER (a sorts before b), positive when
    // smaller, 0 on ties. The (int) cast truncates 64-bit diffs: a 2^32+1
    // gap reads as +1 (the reference quirk, pinned verbatim).
    // Contract: qsort hands the comparator POINTERS INTO an array of
    // wrapper pointers, so the args are EbBufferHeaderType** slots.
    bitstream::EbBufferHeaderType a{}, b{};
    bitstream::EbBufferHeaderType* sa[2] = {&a, &b};
    a.pts = 30;
    b.pts = 10;
    CHECK(bitstream::ptsDescend(&sa[0], &sa[1]) < 0);   // 30 before 10
    CHECK(bitstream::ptsDescend(&sa[1], &sa[0]) > 0);
    a.pts = b.pts = 77;
    CHECK(bitstream::ptsDescend(&sa[0], &sa[1]) == 0);
    a.pts = 1;
    b.pts = ((std::int64_t)1 << 32) + 2;  // diff 2^32+1 -> (int) 1
    CHECK(bitstream::ptsDescend(&sa[0], &sa[1]) == 1);
}

TEST_CASE("PK1 S1: push_undisplayed appends until UNDISP_QUEUE_SIZE") {
    // push_undisplayed_frame (packetization_process.c:125-137): append at
    // the live count, count++. Overflow at UNDISP_QUEUE_SIZE = 8: the drop
    // shells (:127-132) are parked - the observable arithmetic is "no
    // append", threaded as the false return. Poison check: the overflow
    // call must not write past the live region.
    static bitstream::EbBufferHeaderType frames[10];
    memset(frames, 0, sizeof(frames));
    for (int i = 0; i < 10; ++i) frames[i].pts = i;
    bitstream::EncodeContext ctx{};
    ctx.picture_decision_undisplayed_queue_count = 0;
    for (int i = 0; i < 8; ++i) {
        CHECK(bitstream::pushUndisplayedFrame(&ctx, &frames[i]));
        CHECK(ctx.picture_decision_undisplayed_queue_count == (std::uint32_t)(i + 1));
    }
    // the queue is exactly the pushed sequence
    for (int i = 0; i < 8; ++i)
        CHECK(ctx.picture_decision_undisplayed_queue[i] == &frames[i]);
    // overflow: no append - the live top slot and the count stay untouched
    // (the array is exactly UNDISP_QUEUE_SIZE, writes past [7] would hit
    // the adjacent count field, as in the reference's guard)
    CHECK_FALSE(bitstream::pushUndisplayedFrame(&ctx, &frames[8]));
    CHECK(ctx.picture_decision_undisplayed_queue_count == 8);
    CHECK(ctx.picture_decision_undisplayed_queue[7] == &frames[7]);
    CHECK_FALSE(bitstream::pushUndisplayedFrame(&ctx, &frames[9]));
    CHECK(ctx.picture_decision_undisplayed_queue_count == 8);
    CHECK(ctx.picture_decision_undisplayed_queue[7] == &frames[7]);
}

TEST_CASE("PK1 S1: pop_undisplayed drains LIFO from the live count") {
    // pop_undisplayed_frame (packetization_process.c:139-149): the top of
    // the live region, count--; empty -> nullptr (the SVT_ERROR shell
    // parked). Independent oracle: LIFO against a manually driven index.
    static bitstream::EbBufferHeaderType frames[4];
    memset(frames, 0, sizeof(frames));
    bitstream::EncodeContext ctx{};
    CHECK(bitstream::popUndisplayedFrame(&ctx) == nullptr);  // empty
    CHECK(ctx.picture_decision_undisplayed_queue_count == 0);
    for (int i = 0; i < 4; ++i) {
        frames[i].pts = 50 - i;
        CHECK(bitstream::pushUndisplayedFrame(&ctx, &frames[i]));
    }
    for (int i = 3; i >= 0; --i) {
        CHECK(bitstream::popUndisplayedFrame(&ctx) == &frames[i]);
        CHECK(ctx.picture_decision_undisplayed_queue_count == (std::uint32_t)i);
    }
    CHECK(bitstream::popUndisplayedFrame(&ctx) == nullptr);
    CHECK(ctx.picture_decision_undisplayed_queue_count == 0);
}

TEST_CASE("PK1 S1: sort_undisplayed orders the live region by descending pts") {
    // sort_undisplayed_frame (packetization_process.c:151-156): qsort with
    // pts_descend over picture_decision_undisplayed_queue[0..count).
    // Independent oracle: a sorted-ness check (every adjacent pair obeys
    // the comparator), not a permutation snapshot - qsort is not stable.
    static bitstream::EbBufferHeaderType frames[6];
    memset(frames, 0, sizeof(frames));
    static const std::int64_t pts[6] = {3, 90, 7, 41, 12, 66};
    bitstream::EncodeContext ctx{};
    for (int i = 0; i < 6; ++i) {
        frames[i].pts = pts[i];
        CHECK(bitstream::pushUndisplayedFrame(&ctx, &frames[i]));
    }
    bitstream::sortUndisplayedFrame(&ctx);
    CHECK(ctx.picture_decision_undisplayed_queue_count == 6);
    std::int64_t last = (std::int64_t)1 << 62;
    for (std::uint32_t i = 0; i < 6; ++i) {
        CHECK(ctx.picture_decision_undisplayed_queue[i]->pts <= last);
        last = ctx.picture_decision_undisplayed_queue[i]->pts;
    }
    CHECK(last == 3);  // the minimum landed last (max moved first)
    // count == 0 sort is a no-op
    bitstream::EncodeContext empty{};
    bitstream::sortUndisplayedFrame(&empty);
    CHECK(empty.picture_decision_undisplayed_queue_count == 0);
}

TEST_CASE("PK1 S1: release_frames bumps picture numbers and advances the head") {
    // release_frames (packetization_process.c:377-390): each drained slot
    // gets picture_number += queue_size, output wrapper + ppcs hold
    // cleared, then head_index = get_reorder_queue_pos(frames). The
    // svt_release_object(ppcs) shell (:385) is parked - the nulling is
    // pinned. Exhaustive over a size-4 queue: every head x drained-slot
    // pair. The queue invariant (packetization_process.c:677-683): slot s
    // holds decode order s mod size.
    static bitstream::PacketizationReorderEntry slots[4];
    static bitstream::EbBufferHeaderType frames[4];
    memset(frames, 0, sizeof(frames));
    void* ppcsSentinel = (void*)0x77;
    for (int head = 0; head < 4; ++head) {
        bitstream::PacketizationReorderEntry* queue[4] = {&slots[0], &slots[1], &slots[2], &slots[3]};
        std::uint64_t before[4];
        for (int s = 0; s < 4; ++s) {
            memset(&slots[s], 0, sizeof(slots[s]));
            slots[s].picture_number = (std::uint64_t)s;  // slot s holds order s
            slots[s].output_stream = &frames[s];
            slots[s].ppcs_wrapper = ppcsSentinel;
            before[s] = slots[s].picture_number;
        }
        bitstream::EncodeContext ctx{};
        ctx.packetization_reorder_queue = queue;
        ctx.packetization_reorder_queue_size = 4;
        ctx.packetization_reorder_queue_head_index = (std::uint32_t)head;
        bitstream::releaseFrames(&ctx, 3);
        CHECK(ctx.packetization_reorder_queue_head_index == (std::uint32_t)((head + 3) % 4));
        for (int s = 0; s < 4; ++s) {
            const bool drained = (s == head || s == (head + 1) % 4 || s == (head + 2) % 4);
            if (drained) {
                CHECK(slots[s].picture_number == before[s] + 4);
                CHECK(slots[s].output_stream == nullptr);
                CHECK(slots[s].ppcs_wrapper == nullptr);
            } else {
                CHECK(slots[s].picture_number == before[s]);
                CHECK(slots[s].output_stream == &frames[s]);
                CHECK(slots[s].ppcs_wrapper == ppcsSentinel);
            }
        }
    }
}
