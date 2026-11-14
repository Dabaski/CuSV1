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

// ---------------------------------------------------------------------------
// S3: the OBU composition the packetizer applies (entropy_coding.c
// :3678-3948 + the definitions.h little-endian puts). The scs/pcs walls of
// encode_sps_av1 / write_frame_header_av1 are threaded as the payload-writer
// callbacks (the EMC1 carrier precedent); both walks are called TWICE by the
// reference (phase-1 measure + phase-2 rewrite) so a callback must be
// idempotent - that is the reference's own structure, pinned by the tests.
// ---------------------------------------------------------------------------

// EbErrorType slice (API/EbSvtAv1.h:122-131): the values the ported
// packetization spans return.
enum EbErrorType {
    EB_ErrorNone = 0,
    EB_ErrorBadParameter = (std::int32_t)0x80001005,
};

// EbAv1MetadataType (API/EbSvtAv1Metadata.h:26-34, verbatim)
enum EbAv1MetadataType {
    EB_AV1_METADATA_TYPE_AOM_RESERVED_0 = 0,
    EB_AV1_METADATA_TYPE_HDR_CLL = 1,
    EB_AV1_METADATA_TYPE_HDR_MDCV = 2,
    EB_AV1_METADATA_TYPE_SCALABILITY = 3,
    EB_AV1_METADATA_TYPE_ITUT_T35 = 4,
    EB_AV1_METADATA_TYPE_TIMECODE = 5,
    EB_AV1_METADATA_TYPE_FRAME_SIZE = 6,
};

// SvtMetadataT (API/EbSvtAv1Metadata.h:37-41, verbatim field layout)
struct SvtMetadataT {
    std::uint32_t type = 0;
    std::uint8_t* payload = nullptr;
    std::size_t sz = 0;
};

// SvtMetadataArrayT (API/EbSvtAv1Metadata.h:44-47, verbatim field layout:
// sz + the metadata_array pointer). The backing array is test/caller-owned
// - the svt_metadata_alloc/free + EB_REALLOC_ARRAY_NO_CHECK growth shells
// (metadata_handle.c:64-107) are PARKED; the null-array state stays real
// (the :3807 gate reads it).
struct SvtMetadataArrayT {
    std::size_t sz = 0;
    SvtMetadataT** metadata_array = nullptr;
};

// TileEcInfo - the pcs->ec_info[tile_idx]->ec subset consumed by
// svt_aom_write_frame_header_av1 (entropy_coding.c:3864-3914): the tile's
// coded byte count (ec_writer.pos) and its data buffer
// (ec_output_bitstream_ptr->buffer_begin_av1).
struct TileEcInfo {
    std::uint32_t pos = 0;
    const std::uint8_t* tile_data = nullptr;
};

// mem_put_le16 / le24 / le32 (definitions.h:562-584, verbatim shifts;
// MEM_VALUE_T = int32_t, definitions.h:556)
void memPutLe16(void* vmem, std::int32_t val);
void memPutLe24(void* vmem, std::int32_t val);
void memPutLe32(void* vmem, std::int32_t val);

// mem_put_varsize (entropy_coding.c:32-50, verbatim switch 1..4; the
// default assert(0) kept - NDEBUG-dropped convention)
void memPutVarsize(std::uint8_t* const dst, const int sz, const int val);

// write_obu_metadata (entropy_coding.c:3678-3690): the 8-bit type + the
// payload bytes + add_trailing_bits; null metadata / null payload -> 0.
// Deviation named: the file-local wbAddTrailingBits (:3668-3675, static in
// the reference) is exported from bitstream.h for this call.
std::uint32_t writeObuMetadata(const SvtMetadataT* metadata, std::uint8_t* const dst);

// write_tile_group_header (entropy_coding.c:3765-3782, verbatim):
// tiles_log2 == 0 -> 0 bytes written; else the present flag bit and, when
// set, the start/end literals.
std::uint32_t writeTileGroupHeader(std::uint8_t* const dst, int startTile, int endTile, int tiles_log2,
                                   int tile_start_and_end_present_flag);

// svt_aom_write_metadata_av1 (entropy_coding.c:3804-3838): for every
// matching-type entry compose the metadata OBU at the write pointer
// (header + uleb + payload) and advance buffer_av1; the null-array gate
// returns EB_ErrorBadParameter.
std::int32_t writeMetadataAv1(Bitstream* bitstream_ptr, SvtMetadataArrayT* metadata,
                              EbAv1MetadataType type);

// svt_aom_encode_sps_av1 (entropy_coding.c:3925-3948) over carriers: the
// SequenceControlSet wall is threaded as the SPS-payload writer callback
// (the landed writeSequenceHeaderObu / writeSequenceHeaderObuV3 are the two
// ratified instantiations); enhancement_layers_count stays const 0 (:3929).
// PRECONDITION (the reference's own): the payload walk must fit the buffer
// at the write pointer (the EC ensure-capacity discipline).
std::int32_t encodeSpsAv1(Bitstream* bitstream_ptr,
                          std::uint32_t (*writeSequenceHeaderObuFn)(std::uint8_t* dst));

// svt_aom_write_frame_header_av1 (entropy_coding.c:3843-3920) over
// carriers: the scs/pcs frame-header wall is threaded as the callback
// (called twice: phase-1 measure :3858 / phase-2 rewrite :3896 - the
// idempotency is the reference's own structure); the tile size prefixes +
// data copies and the :3880-3885 capacity gate are verbatim. PRECONDITION:
// the phase-1 writes must fit the buffer at the write pointer (the
// reference has the same unguarded order - the ensure comes after the
// measure; the caller discipline is ecEnsureCapacity before each walk).
std::int32_t writeFrameHeaderAv1(Bitstream* bitstream_ptr,
                                 int show_existing,
                                 std::uint32_t (*writeFrameHeaderObuFn)(std::uint8_t* dst, int show_existing,
                                                                        int appendTrailingBits),
                                 int n_log2_tiles,
                                 std::uint8_t tile_size_bytes_minus_1,
                                 const TileEcInfo* tiles,
                                 int tile_cnt);

}  // namespace bitstream
