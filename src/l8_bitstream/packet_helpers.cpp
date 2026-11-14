#include "packet_helpers.h"

#include <cstdlib>

namespace bitstream {

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
