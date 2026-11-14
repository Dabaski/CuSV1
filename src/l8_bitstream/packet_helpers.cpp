#include "packet_helpers.h"

#include "bitstream.h"

#include <cassert>
#include <cstdlib>
#include <cstring>

namespace bitstream {

// MEM_VALUE_T (definitions.h:556)
typedef std::int32_t MEM_VALUE_T;

void memPutLe16(void* vmem, MEM_VALUE_T val) {
    std::uint8_t* mem = (std::uint8_t*)vmem;
    mem[0] = (std::uint8_t)((val >> 0) & 0xff);
    mem[1] = (std::uint8_t)((val >> 8) & 0xff);
}

void memPutLe24(void* vmem, MEM_VALUE_T val) {
    std::uint8_t* mem = (std::uint8_t*)vmem;
    mem[0] = (std::uint8_t)((val >> 0) & 0xff);
    mem[1] = (std::uint8_t)((val >> 8) & 0xff);
    mem[2] = (std::uint8_t)((val >> 16) & 0xff);
}

void memPutLe32(void* vmem, MEM_VALUE_T val) {
    std::uint8_t* mem = (std::uint8_t*)vmem;
    mem[0] = (std::uint8_t)((val >> 0) & 0xff);
    mem[1] = (std::uint8_t)((val >> 8) & 0xff);
    mem[2] = (std::uint8_t)((val >> 16) & 0xff);
    mem[3] = (std::uint8_t)((val >> 24) & 0xff);
}

void memPutVarsize(std::uint8_t* const dst, const int sz, const int val) {
    switch (sz) {
    case 1:
        dst[0] = (std::uint8_t)(val & 0xff);
        break;
    case 2:
        memPutLe16(dst, val);
        break;
    case 3:
        memPutLe24(dst, val);
        break;
    case 4:
        memPutLe32(dst, val);
        break;
    default:
        assert(0 && "Invalid size");
        break;
    }
}

std::uint32_t writeObuMetadata(const SvtMetadataT* metadata, std::uint8_t* const dst) {
    if (!metadata || !metadata->payload) {
        return 0;
    }
    AomWriteBitBuffer wb{dst, 0};
    std::uint32_t size = 0;
    wbWriteLiteral(&wb, (std::int32_t)metadata->type, 8);
    for (std::size_t i = 0; i < metadata->sz; ++i) {
        wbWriteLiteral(&wb, metadata->payload[i], 8);
    }
    wbAddTrailingBits(&wb);
    size = wbBytesWritten(&wb);
    return size;
}

std::uint32_t writeTileGroupHeader(std::uint8_t* const dst, int startTile, int endTile, int tiles_log2,
                                   int tile_start_and_end_present_flag) {
    AomWriteBitBuffer wb{dst, 0};
    std::uint32_t size = 0;

    if (!tiles_log2) {
        return size;
    }
    wbWriteBit(&wb, tile_start_and_end_present_flag);

    if (tile_start_and_end_present_flag) {
        wbWriteLiteral(&wb, startTile, tiles_log2);
        wbWriteLiteral(&wb, endTile, tiles_log2);
    }

    size = wbBytesWritten(&wb);
    return size;
}

std::int32_t writeMetadataAv1(Bitstream* bitstream_ptr, SvtMetadataArrayT* metadata, EbAv1MetadataType type) {
    std::int32_t return_error = EB_ErrorNone;
    if (!metadata || !metadata->metadata_array) {
        return EB_ErrorBadParameter;
    }

    OutputBitstreamUnit* output_bitstream_ptr = bitstream_ptr->output_bitstream_ptr;
    std::uint8_t* data = output_bitstream_ptr->buffer_av1;

    for (std::size_t i = 0; i < metadata->sz; i++) {
        SvtMetadataT* current_metadata = metadata->metadata_array[i];
        if (current_metadata && current_metadata->payload && current_metadata->type == (std::uint32_t)type) {
            // Phase 1: measure header + payload sizes
            std::uint32_t obu_header_size = writeObuHeader(OBU_METADATA, 0, data);
            std::uint32_t obu_payload_size = writeObuMetadata(current_metadata, data + obu_header_size);
            std::size_t length_field_size = ulebSizeInBytes(obu_payload_size);

            // Phase 2: write at correct offsets (re-write payload after LEB128)
            // OBU header already at data[0]
            std::size_t coded_size;
            std::int32_t ret = ulebEncode(obu_payload_size, sizeof(obu_payload_size), data + obu_header_size,
                                          &coded_size);
            assert(ret == 0 && coded_size == length_field_size);
            if (ret != 0 || coded_size != length_field_size) {
                return EB_ErrorBadParameter;
            }
            writeObuMetadata(current_metadata, data + obu_header_size + length_field_size);

            data += obu_header_size + length_field_size + obu_payload_size;
        }
    }
    output_bitstream_ptr->buffer_av1 = data;
    return return_error;
}

std::int32_t encodeSpsAv1(Bitstream* bitstream_ptr, std::uint32_t (*writeSequenceHeaderObuFn)(std::uint8_t* dst)) {
    std::int32_t return_error = EB_ErrorNone;
    OutputBitstreamUnit* output_bitstream_ptr = bitstream_ptr->output_bitstream_ptr;
    std::uint8_t* data = output_bitstream_ptr->buffer_av1;
    const std::uint8_t enhancement_layers_count = 0;  // cm->enhancement_layers_count
    (void)enhancement_layers_count;  // threaded into the payload callback's ratified walk

    // Phase 1: measure
    std::uint32_t obu_header_size = writeObuHeader(OBU_SEQUENCE_HEADER, 0, data);
    std::uint32_t obu_payload_size = writeSequenceHeaderObuFn(data + obu_header_size);
    std::size_t length_field_size = ulebSizeInBytes(obu_payload_size);

    // Phase 2: write at correct offsets (re-write payload after LEB128)
    std::size_t coded_size;
    std::int32_t ret = ulebEncode(obu_payload_size, sizeof(obu_payload_size), data + obu_header_size, &coded_size);
    assert(ret == 0 && coded_size == length_field_size);
    if (ret != 0 || coded_size != length_field_size) {
        return EB_ErrorBadParameter;
    }
    writeSequenceHeaderObuFn(data + obu_header_size + length_field_size);

    data += obu_header_size + length_field_size + obu_payload_size;
    output_bitstream_ptr->buffer_av1 = data;
    return return_error;
}

std::uint32_t countFramesInNextTu(const EncodeContext* enc_ctx, std::uint32_t* data_size) {
    std::uint32_t i = 0;
    *data_size = 0;
    do {
        const PacketizationReorderEntry* queue_entry_ptr = getReorderQueueEntry(enc_ctx, (int)i);
        // not a completed td
        if (!queue_entry_ptr->output_stream) {
            return 0;
        }

        const EbBufferHeaderType* output_stream_ptr = queue_entry_ptr->output_stream;
        *data_size += output_stream_ptr->n_filled_len;

        i++;
        // we have a td when we got a displable frame
        if (queue_entry_ptr->show_frame) {
            break;
        }
        if (output_stream_ptr->flags & EB_BUFFERFLAG_EOS) {
            break;
        }

    } while (i < enc_ctx->packetization_reorder_queue_size);
    return i;
}

void clearEosFlag(EbBufferHeaderType* output_stream_ptr) {
    output_stream_ptr->flags &= ~EB_BUFFERFLAG_EOS;
}

void setEosFlag(EbBufferHeaderType* output_stream_ptr) {
    output_stream_ptr->flags |= EB_BUFFERFLAG_EOS;
}

std::int32_t copyDataFromBitstream(EncodeContext* enc_ctx, Bitstream* bitstream_ptr,
                                   EbBufferHeaderType* output_stream_ptr) {
    std::int32_t return_error = EB_ErrorNone;
    int size = bitstreamGetBytesCount(bitstream_ptr);

    // CHECK_REPORT_ERROR guard (packetization_process.c:354-356): threaded
    // as the return - the app-callback error_handler + while(1) halt are
    // parked.
    if (!(size + output_stream_ptr->n_filled_len < output_stream_ptr->n_alloc_len)) {
        (void)enc_ctx;
        return EB_ENC_EC_ERROR2;
    }

    void* dest = output_stream_ptr->p_buffer + output_stream_ptr->n_filled_len;
    bitstreamCopy(bitstream_ptr, dest, size);
    output_stream_ptr->n_filled_len += size;

    return return_error;
}

std::size_t metadataSize(SvtMetadataArrayT* metadata, EbAv1MetadataType type) {
    std::size_t sz = 0;
    if (!metadata || !metadata->metadata_array || metadata->sz == 0) {
        return 0;
    } else {
        for (std::size_t i = 0; i < metadata->sz; i++) {
            SvtMetadataT* current_metadata = metadata->metadata_array[i];
            if (current_metadata && current_metadata->payload && current_metadata->type == (std::uint32_t)type) {
                sz += current_metadata->sz + 1  // obu type
                    + 1                         // trailing byte
                    + 1                         // header size
                    + 1;                        // length field size
            }
        }
    }
    return sz;
}

std::int32_t encodeTu(EncodeContext* enc_ctx, int frames, std::uint32_t total_bytes,
                      EbBufferHeaderType* output_stream_ptr) {    total_bytes += TD_SIZE;
    if (total_bytes > output_stream_ptr->n_alloc_len) {
        // The EB_MALLOC + min(total, alloc) svt_memcpy pre-copy + EB_FREE
        // shells (:306-319) become vector resize - content preservation is
        // equivalent; the bytes beyond n_filled_len are overwritten by the
        // backward walk for every reachable frames >= 1.
        output_stream_ptr->storage.resize(total_bytes);
        output_stream_ptr->p_buffer = output_stream_ptr->storage.data();
        output_stream_ptr->n_alloc_len = total_bytes;
    }
    std::uint8_t* dst = output_stream_ptr->p_buffer + total_bytes;
    // we use last frame's output_stream_ptr to hold entire tu, so we need copy backward.
    for (int i = frames - 1; i >= 0; i--) {
        PacketizationReorderEntry* queue_entry_ptr = getReorderQueueEntry(enc_ctx, i);
        EbBufferHeaderType* src_stream_ptr = queue_entry_ptr->output_stream;
        std::uint32_t size = src_stream_ptr->n_filled_len;
        dst -= size;
        memmove(dst, src_stream_ptr->p_buffer, size);
        // 1. The last frame is a displayable frame, others are undisplayed.
        // 2. We do not push alt ref frame since the overlay frame will carry the pts.
        // 3. Release alt ref stream buffer here for it will not be sent out
        if (i != frames - 1 && !queue_entry_ptr->is_alt_ref) {
            pushUndisplayedFrame(enc_ctx, src_stream_ptr);
        }
        // the alt-ref EB_FREE + svt_release_object shells (:335-336) are
        // parked - no observable carrier state change.
    }
    if (frames > 1) {
        sortUndisplayedFrame(enc_ctx);
    }
    dst -= TD_SIZE;
    encodeTdAv1(dst);
    output_stream_ptr->n_filled_len = total_bytes;
    output_stream_ptr->flags |= EB_BUFFERFLAG_HAS_TD;
    return EB_ErrorNone;
}

void encodeShowExisting(EncodeContext* enc_ctx, PacketizationReorderEntry* queue_entry_ptr,
                        EbBufferHeaderType* output_stream_ptr) {
    std::uint8_t* dst = output_stream_ptr->p_buffer;

    encodeTdAv1(dst);
    output_stream_ptr->n_filled_len = TD_SIZE;

    copyDataFromBitstream(enc_ctx, queue_entry_ptr->bitstream_ptr, output_stream_ptr);

    output_stream_ptr->flags |= (EB_BUFFERFLAG_SHOW_EXT | EB_BUFFERFLAG_HAS_TD);
}

std::int32_t writeFrameHeaderAv1(Bitstream* bitstream_ptr,
                                 int show_existing,
                                 std::uint32_t (*writeFrameHeaderObuFn)(std::uint8_t* dst, int show_existing,
                                                                        int appendTrailingBits),
                                 int n_log2_tiles,
                                 std::uint8_t tile_size_bytes_minus_1,
                                 const TileEcInfo* tiles,
                                 int tile_cnt) {
    std::int32_t return_error = EB_ErrorNone;
    OutputBitstreamUnit* output_bitstream_ptr = bitstream_ptr->output_bitstream_ptr;
    std::uint8_t* data = output_bitstream_ptr->buffer_av1;

    ObuType obu_type = show_existing ? OBU_FRAME_HEADER : OBU_FRAME;
    int tile_start_and_end_present_flag = 0;

    // Phase 1: Measure header sizes by writing to data (will be overwritten in phase 2).
    std::uint32_t obu_header_size = writeObuHeader(obu_type, 0, data);
    std::uint32_t frame_hdr_size = writeFrameHeaderObuFn(data + obu_header_size, show_existing, show_existing);
    std::uint32_t tg_hdr_size =
        writeTileGroupHeader(data + obu_header_size + frame_hdr_size, 0, 0, n_log2_tiles, tile_start_and_end_present_flag);
    std::uint32_t hdr_payload_size = frame_hdr_size + tg_hdr_size;

    // Compute tile data size (tile size prefixes + tile data).
    std::uint32_t tile_data_size = 0;
    if (!show_existing) {
        for (int tile_idx = 0; tile_idx < tile_cnt; tile_idx++) {
            tile_data_size += tiles[tile_idx].pos;
            if (tile_idx != tile_cnt - 1 && tile_cnt > 1) {
                tile_data_size += tile_size_bytes_minus_1 + 1;
            }
        }
    }

    // Compute exact OBU payload size and LEB128 field size.
    std::uint32_t obu_payload_size = hdr_payload_size + tile_data_size;
    std::size_t length_field_size = ulebSizeInBytes(obu_payload_size);

    // Ensure buffer is large enough for the complete OBU.
    std::uint32_t total_obu_size = obu_header_size + (std::uint32_t)length_field_size + obu_payload_size;
    std::uint32_t buf_needed =
        total_obu_size + (std::uint32_t)(output_bitstream_ptr->buffer_av1 - output_bitstream_ptr->buffer_begin_av1);
    if (output_bitstream_ptr->size < buf_needed) {
        reallocOutputBitstreamUnit(output_bitstream_ptr, buf_needed + 1);
        data = output_bitstream_ptr->buffer_av1;
    }

    // Phase 2: Write everything at the correct offsets - no memmove needed.
    // OBU header is already at data[0] from phase 1 (same content, same position).

    // LEB128 size field right after OBU header.
    std::size_t coded_size;
    ulebEncode(obu_payload_size, sizeof(obu_payload_size), data + obu_header_size, &coded_size);

    // Re-write frame header + tile group header at the correct offset (after LEB128).
    std::uint32_t write_offset = obu_header_size + (std::uint32_t)length_field_size;
    writeFrameHeaderObuFn(data + write_offset, show_existing, show_existing);
    write_offset += frame_hdr_size;
    writeTileGroupHeader(data + write_offset, 0, 0, n_log2_tiles, tile_start_and_end_present_flag);
    write_offset += tg_hdr_size;

    // Copy tile data.
    if (!show_existing) {
        for (int tile_idx = 0; tile_idx < tile_cnt; tile_idx++) {
            std::int32_t tile_size = (std::int32_t)tiles[tile_idx].pos;
            std::uint8_t tile_size_bytes = 0;
            if (tile_idx != tile_cnt - 1 && tile_cnt > 1) {
                tile_size_bytes = tile_size_bytes_minus_1 + 1;
                memPutVarsize(data + write_offset, tile_size_bytes, tile_size - 1);
            }
            memcpy(data + write_offset + tile_size_bytes, tiles[tile_idx].tile_data, tile_size);
            write_offset += (std::uint32_t)(tile_size + tile_size_bytes);
        }
    }

    data += total_obu_size;
    output_bitstream_ptr->buffer_av1 = data;
    return return_error;
}

std::int32_t outputBitstreamUnitCtor(OutputBitstreamUnit* bitstream_ptr, std::uint32_t buffer_size) {
    if (buffer_size) {
        bitstream_ptr->size = buffer_size;
        bitstream_ptr->storage.resize(bitstream_ptr->size);  // EB_MALLOC_ARRAY (bitstream_unit.c:36) -> vector
        bitstream_ptr->buffer_begin_av1 = bitstream_ptr->storage.data();
        bitstream_ptr->buffer_av1 = bitstream_ptr->buffer_begin_av1;
    } else {
        bitstream_ptr->size = 0;
        bitstream_ptr->buffer_begin_av1 = nullptr;
        bitstream_ptr->buffer_av1 = nullptr;
    }
    return 0;  // EB_ErrorNone
}

std::int32_t outputBitstreamReset(OutputBitstreamUnit* bitstream_ptr) {
    // Reset the write ptr to the beginning of the buffer
    bitstream_ptr->buffer_av1 = bitstream_ptr->buffer_begin_av1;
    return 0;  // EB_ErrorNone
}

std::int32_t reallocOutputBitstreamUnit(OutputBitstreamUnit* output_bitstream_ptr, std::uint32_t sz) {
    if (output_bitstream_ptr && sz > 0) {
        // Must add offset to realloc'd buffer to save any previously written bits
        std::uint64_t offset = output_bitstream_ptr->buffer_av1 - output_bitstream_ptr->buffer_begin_av1;
        output_bitstream_ptr->size = sz;
        output_bitstream_ptr->storage.resize(sz);  // EB_REALLOC_ARRAY (bitstream_unit.c:70) -> vector
        output_bitstream_ptr->buffer_begin_av1 = output_bitstream_ptr->storage.data();
        output_bitstream_ptr->buffer_av1 = output_bitstream_ptr->buffer_begin_av1 + offset;
    }
    return 0;  // EB_ErrorNone
}

void bitstreamReset(Bitstream* bitstream_ptr) {
    outputBitstreamReset(bitstream_ptr->output_bitstream_ptr);
}

int bitstreamGetBytesCount(const Bitstream* bitstream_ptr) {
    const OutputBitstreamUnit* unit = bitstream_ptr->output_bitstream_ptr;
    return (int)(unit->buffer_av1 - unit->buffer_begin_av1);
}

void bitstreamCopy(const Bitstream* bitstream_ptr, void* dest, int size) {
    const OutputBitstreamUnit* unit = bitstream_ptr->output_bitstream_ptr;
    memcpy(dest, unit->buffer_begin_av1, size);
}

void aomStartEncode(entropy::AomWriter* br, OutputBitstreamUnit* source) {
    br->pos = 0;
    // Borrow tile buffer: EC writes directly to OutputBitstreamUnit's buffer
    br->ec.buf = source->buffer_begin_av1;
    entropy::odEcEncReset(&br->ec);
}

std::int32_t ecEnsureCapacity(entropy::AomWriter* w, OutputBitstreamUnit* parent, std::uint32_t min_free) {
    std::int32_t ret = 0;  // EB_ErrorNone
    entropy::OdEcEnc* enc = &w->ec;
    std::uint32_t offs = (std::uint32_t)(enc->ptr - enc->buf);
    std::uint32_t needed = offs + min_free;
    if (needed > parent->size) {
        // Realloc through the OutputBitstreamUnit that owns the buffer
        ret = reallocOutputBitstreamUnit(parent, needed);
        if (ret != 0) {
            enc->error = -1;
        } else {
            // Update EC's borrowed pointers
            enc->buf = parent->buffer_begin_av1;
            enc->ptr = enc->buf + offs;
        }
    }
    return ret;
}

int getReorderQueuePos(const EncodeContext* enc_ctx, int delta) {
    return (enc_ctx->packetization_reorder_queue_head_index + delta) % enc_ctx->packetization_reorder_queue_size;
}

PacketizationReorderEntry* getReorderQueueEntry(const EncodeContext* enc_ctx, int delta) {
    int pos = getReorderQueuePos(enc_ctx, delta);
    return enc_ctx->packetization_reorder_queue[pos];
}

int ptsDescend(const void* pa, const void* pb) {
    EbBufferHeaderType* a = *(EbBufferHeaderType**)pa;
    EbBufferHeaderType* b = *(EbBufferHeaderType**)pb;
    return (int)(b->pts - a->pts);
}

bool pushUndisplayedFrame(EncodeContext* enc_ctx, EbBufferHeaderType* wrapper) {
    if (enc_ctx->picture_decision_undisplayed_queue_count >= UNDISP_QUEUE_SIZE) {
        // the SVT_ERROR + EB_FREE + svt_release_object drop shells
        // (packetization_process.c:127-132) are parked - no append.
        return false;
    }
    std::uint32_t count                               = enc_ctx->picture_decision_undisplayed_queue_count;
    enc_ctx->picture_decision_undisplayed_queue[count++] = wrapper;
    enc_ctx->picture_decision_undisplayed_queue_count    = count;
    return true;
}

EbBufferHeaderType* popUndisplayedFrame(EncodeContext* enc_ctx) {
    std::uint32_t count = enc_ctx->picture_decision_undisplayed_queue_count;
    if (!count) {
        // the SVT_ERROR shell (packetization_process.c:142-144) is parked.
        return nullptr;
    }
    count--;
    EbBufferHeaderType* ret                            = enc_ctx->picture_decision_undisplayed_queue[count];
    enc_ctx->picture_decision_undisplayed_queue_count = count;
    return ret;
}

void sortUndisplayedFrame(EncodeContext* enc_ctx) {
    qsort(&enc_ctx->picture_decision_undisplayed_queue[0],
          enc_ctx->picture_decision_undisplayed_queue_count,
          sizeof(EbBufferHeaderType*),
          ptsDescend);
}

void releaseFrames(EncodeContext* enc_ctx, int frames) {
    for (int i = 0; i < frames; i++) {
        PacketizationReorderEntry* queue_entry_ptr = getReorderQueueEntry(enc_ctx, i);
        // Reset the Reorder Queue Entry
        queue_entry_ptr->picture_number += enc_ctx->packetization_reorder_queue_size;
        queue_entry_ptr->output_stream = nullptr;
        // the svt_release_object(ppcs) shell (packetization_process.c:385)
        // is parked - the hold is dropped by nulling.
        queue_entry_ptr->ppcs_wrapper = nullptr;
    }
    enc_ctx->packetization_reorder_queue_head_index = (std::uint32_t)getReorderQueuePos(enc_ctx, frames);
}

}  // namespace bitstream
