// l8_bitstream/packet_helpers.h - the A7P packetization pure surface.
// Reference: third_party/SVT-AV1/Source/Lib/Codec/packetization_process.c,
// packetization_reorder_queue.c/h, bitstream_unit.c/h (pinned vendored
// tree). Struct-free ports over field-subset carriers (the RE8A
// DeblockContext / EMC1 mirror-carrier precedent). The thread/fifo/mutex
// shells, the EB_NEW/EB_FREE allocation shells and the EbObjectWrapper
// lifetimes are PARKED NAMED (the parks list lives in the test file header
// and the commit messages).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <entropy.h>

namespace bitstream {

// ---------------------------------------------------------------------------
// S2: the bitstream-unit carriers (bitstream_unit.h:24-29 + the Bitstream
// wrapper, ec_object.h). The EB_MALLOC_ARRAY/EB_REALLOC_ARRAY allocation
// shells are carried as std::vector ownership (the grain.h carrier
// precedent); the dctor/EB_NEW shells are PARKED NAMED.
// ---------------------------------------------------------------------------

// OutputBitstreamUnit (bitstream_unit.h:24-29, verbatim field subset):
// size = allocated buffer size, buffer_begin_av1 = the byte buffer,
// buffer_av1 = the write pointer. storage owns buffer_begin_av1 - the
// EB_MALLOC_ARRAY (bitstream_unit.c:36) / EB_REALLOC_ARRAY (:70) shells
// become vector resize; the output_bitstream_unit_dctor (:24-27) is parked
// (vector owns).
struct OutputBitstreamUnit {
    std::uint32_t size = 0;
    std::vector<std::uint8_t> storage;  // owns buffer_begin_av1
    std::uint8_t* buffer_begin_av1 = nullptr;
    std::uint8_t* buffer_av1 = nullptr;
};

// Bitstream (ec_object.h): output_bitstream_ptr flattened. The EB_NEW ctor
// shell (entropy_coding.c:1258-1262) is PARKED.
struct Bitstream {
    OutputBitstreamUnit* output_bitstream_ptr = nullptr;
};

// ---------------------------------------------------------------------------
// S2: the unit ctor/reset/realloc + the Bitstream size/get-bytes helpers.
// ---------------------------------------------------------------------------

// svt_aom_output_bitstream_unit_ctor (bitstream_unit.c:32-45): buffer_size
// 0 -> size 0 + null pointers; else allocate. Deviation named: zero-init
// (vector) where the reference's EB_MALLOC_ARRAY is uninitialized - the
// bytes past the write pointer are never read by the ported callers.
std::int32_t outputBitstreamUnitCtor(OutputBitstreamUnit* bitstream_ptr, std::uint32_t buffer_size);

// svt_aom_output_bitstream_reset (bitstream_unit.c:50-57): the write
// pointer back to the beginning.
std::int32_t outputBitstreamReset(OutputBitstreamUnit* bitstream_ptr);

// svt_realloc_output_bitstream_unit (bitstream_unit.c:64-74): grows to sz,
// preserving the write offset. Deviation named: EB_REALLOC_ARRAY (:70) ->
// vector resize + the pointer re-derivation (storage.data()); the
// offset-preserve arithmetic (:67-:71) is verbatim.
std::int32_t reallocOutputBitstreamUnit(OutputBitstreamUnit* output_bitstream_ptr, std::uint32_t sz);

// svt_aom_bitstream_reset (entropy_coding.c:1264-1266)
void bitstreamReset(Bitstream* bitstream_ptr);

// svt_aom_bitstream_get_bytes_count (entropy_coding.c:1268-1271):
// (int)(buffer_av1 - buffer_begin_av1).
int bitstreamGetBytesCount(const Bitstream* bitstream_ptr);

// svt_aom_bitstream_copy (entropy_coding.c:1273-1276): memcpy from the
// buffer beginning.
void bitstreamCopy(const Bitstream* bitstream_ptr, void* dest, int size);

// aom_start_encode (bitstream_unit.h:230-236) over the l7 AomWriter.
// Deviation named: buffer_parent (dropped in l7) is threaded as the call
// argument to ecEnsureCapacity instead.
void aomStartEncode(entropy::AomWriter* br, OutputBitstreamUnit* source);

// svt_aom_ec_ensure_capacity (bitstream_unit.c:210-228): grow the parent
// when offs + min_free exceeds its size, re-binding the borrowed pointers.
// Deviation named: w->buffer_parent (dropped in l7) is threaded as the
// parent argument.
std::int32_t ecEnsureCapacity(entropy::AomWriter* w, OutputBitstreamUnit* parent, std::uint32_t min_free);

// ---------------------------------------------------------------------------
// S1: the reorder-queue index math + the undisplayed-queue push/pop/sort.
// ---------------------------------------------------------------------------

// UNDISP_QUEUE_SIZE (definitions.h:1444) = REF_FRAMES = 8 (definitions.h:
// 1357-1367, the enum position: NONE_FRAME -1 .. ALTREF_FRAME 7, REF_FRAMES
// 8). Carried locally - l8 does not link l7 symbols for this (the wb-trio
// duplication precedent).
#define UNDISP_QUEUE_SIZE 8

// ---------------------------------------------------------------------------
// S1 carriers: field subsets of the reference structs the ported spans
// consume.
// ---------------------------------------------------------------------------

// EbBufferHeaderType (API/EbSvtAv1.h:80-114): the field subset the ported
// packetization spans consume. Deviation: p_buffer is vector-backed (the
// grain.h carrier precedent) so the encode_tu grow path
// (packetization_process.c:306-319) is real without EB_MALLOC.
struct EbBufferHeaderType {
    std::vector<std::uint8_t> storage;  // owns p_buffer
    std::uint8_t* p_buffer = nullptr;
    std::uint32_t n_filled_len = 0;
    std::uint32_t n_alloc_len = 0;
    std::int64_t dts = 0;
    std::int64_t pts = 0;
    std::uint32_t flags = 0;
};

// PacketizationReorderEntry (packetization_reorder_queue.h:26-56): the
// field subset the ported spans consume. output_stream_wrapper_ptr /
// ppcs_wrapper are flattened to the object pointers (the wrapper deref);
// the EB_NEW ctor / EB_DELETE dctor shells (packetization_reorder_queue.c:
// 15-26) are PARKED.
struct PacketizationReorderEntry {
    std::uint64_t picture_number = 0;
    EbBufferHeaderType* output_stream = nullptr;  // wrapper->object_ptr
    void* ppcs_wrapper = nullptr;
    bool show_frame = false;
};

// EncodeContext (encode_context.h:123-125 + :95-96): the field subset the
// ported packetization spans consume.
struct EncodeContext {
    PacketizationReorderEntry** packetization_reorder_queue = nullptr;
    std::uint32_t packetization_reorder_queue_size = 0;
    std::uint32_t packetization_reorder_queue_head_index = 0;
    EbBufferHeaderType* picture_decision_undisplayed_queue[UNDISP_QUEUE_SIZE] = {};
    std::uint32_t picture_decision_undisplayed_queue_count = 0;
};

// ---------------------------------------------------------------------------
// S1 functions.
// ---------------------------------------------------------------------------

// get_reorder_queue_pos (packetization_process.c:81-83, verbatim):
// (head_index + delta) % queue_size.
int getReorderQueuePos(const EncodeContext* enc_ctx, int delta);

// get_reorder_queue_entry (packetization_process.c:85-88): the entry at
// getReorderQueuePos.
PacketizationReorderEntry* getReorderQueueEntry(const EncodeContext* enc_ctx, int delta);

// pts_descend (packetization_process.c:117-123, verbatim): the qsort
// comparator over EbBufferHeaderType pts - (int)(bb->pts - ba->pts),
// descending.
int ptsDescend(const void* pa, const void* pb);

// push_undisplayed_frame (packetization_process.c:125-137). Deviation named:
// the overflow SVT_ERROR + EB_FREE + svt_release_object drop shells
// (:127-132) are parked; the observable queue arithmetic (no append when
// count >= UNDISP_QUEUE_SIZE) is threaded as the bool return (true =
// appended).
bool pushUndisplayedFrame(EncodeContext* enc_ctx, EbBufferHeaderType* wrapper);

// pop_undisplayed_frame (packetization_process.c:139-149): the live-count
// top of the undisplayed queue; empty -> nullptr (the SVT_ERROR shell
// parked).
EbBufferHeaderType* popUndisplayedFrame(EncodeContext* enc_ctx);

// sort_undisplayed_frame (packetization_process.c:151-156): qsort by
// ptsDescend over the live count.
void sortUndisplayedFrame(EncodeContext* enc_ctx);

// release_frames (packetization_process.c:377-390): bump each drained
// slot's picture_number by the queue size, clear the output wrapper and the
// ppcs hold, advance the head via getReorderQueuePos. Deviation named: the
// svt_release_object(ppcs) shell (:385) is parked - the nulling is kept.
void releaseFrames(EncodeContext* enc_ctx, int frames);

}  // namespace bitstream
