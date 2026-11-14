#include "packet_helpers.h"

#include <cstdlib>
#include <cstring>

namespace bitstream {

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
