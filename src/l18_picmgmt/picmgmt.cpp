// l18_picmgmt - pcs.c spans (picture-parent-control-set layout helpers).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/pcs.c.
#include "picmgmt.h"

#include <cassert>
#include <cstdlib>
#include <cstring>

namespace l18_picmgmt {


// svt_aom_get_out_buffer_size (pcs.c:374-379): the raw 4:2:0 frame size / 4
// as the starting bitstream capacity; the entropy writer grows on demand.
std::uint32_t getOutBufferSize(std::uint32_t pictureWidth, std::uint32_t pictureHeight) {
    return pictureWidth * pictureHeight * 3 / 2 / 4; // raw 4:2:0 frame size / 4
}

// svt_aom_get_max_allocated_me_refs (pcs.c:88-93): verbatim arithmetic. The
// uint8_t params promote to int, so ref_count_used_list0 - 1 can go -1 and
// the final uint8_t store truncates (pinned by the 0,0 fixture).
void getMaxAllocatedMeRefs(std::uint8_t refCountUsedList0, std::uint8_t refCountUsedList1,
                           std::uint8_t* maxRefToAlloc, std::uint8_t* maxCandToAlloc) {
    *maxRefToAlloc = static_cast<std::uint8_t>(refCountUsedList0 + refCountUsedList1);
    *maxCandToAlloc = static_cast<std::uint8_t>(refCountUsedList0 + refCountUsedList1 +
                                                (refCountUsedList0 * refCountUsedList1) +
                                                (refCountUsedList0 - 1) + (refCountUsedList1 == 3 ? 1 : 0));
}

// set_restoration_unit_size (pcs.c:29-41): the (void) parameter casts are
// the reference's own lines :30-:33.
void setRestorationUnitSize(std::int32_t width, std::int32_t height, std::int32_t sx, std::int32_t sy,
                            RestorationInfo* rst) {
    (void)width;
    (void)height;
    (void)sx;
    (void)sy;

    std::int32_t s = 0;

    rst[0].restorationUnitSize = restorationUnitSizeMax;

    rst[1].restorationUnitSize = rst[0].restorationUnitSize >> s;
    rst[2].restorationUnitSize = rst[1].restorationUnitSize;
}

// segmentation_map_ctor (pcs.c:69-77) + segmentation_map_dctor (pcs.c:58-61).
EbErrorType segmentationMapCtor(SegmentationNeighborMap* segNeighborMap, std::uint16_t picWidth,
                                std::uint16_t picHeight) {
    const std::uint32_t numElements = static_cast<std::uint32_t>(picWidth >> miSizeLog2) *
        static_cast<std::uint32_t>(picHeight >> miSizeLog2);

    segNeighborMap->mapSize = numElements;
    segNeighborMap->data = static_cast<std::uint8_t*>(std::calloc(numElements, 1));
    return EB_ErrorNone;
}

void segmentationMapDctor(SegmentationNeighborMap* segNeighborMap) {
    std::free(segNeighborMap->data);
}

// b64_geom_init (pcs.c:1488-1510): verbatim walk. MIN(width - org_x,
// b64_size) at :1504-:1505; is_complete at :1506.
EbErrorType b64GeomInit(Scs* scs, std::uint16_t width, std::uint16_t height, B64Geom** b64Geoms) {
    const std::uint8_t b64Size = scs->b64Size;
    const std::uint16_t pictureB64Width = static_cast<std::uint16_t>(divideAndCeil(width, b64Size));
    const std::uint16_t pictureB64Height = static_cast<std::uint16_t>(divideAndCeil(height, b64Size));

    std::free(*b64Geoms);
    *b64Geoms = static_cast<B64Geom*>(
        std::malloc(static_cast<std::size_t>(pictureB64Width) * pictureB64Height * sizeof(B64Geom)));

    for (int b64Idx = 0; b64Idx < pictureB64Width * pictureB64Height; ++b64Idx) {
        B64Geom* b64Geom = &(*b64Geoms)[b64Idx];
        const std::uint16_t horizontalIndex = static_cast<std::uint16_t>(b64Idx % pictureB64Width);
        const std::uint16_t verticalIndex = static_cast<std::uint16_t>(b64Idx / pictureB64Width);
        b64Geom->orgX = static_cast<std::uint16_t>(horizontalIndex * b64Size);
        b64Geom->orgY = static_cast<std::uint16_t>(verticalIndex * b64Size);
        b64Geom->width =
            static_cast<std::uint8_t>(width - b64Geom->orgX < b64Size ? width - b64Geom->orgX : b64Size);
        b64Geom->height =
            static_cast<std::uint8_t>(height - b64Geom->orgY < b64Size ? height - b64Geom->orgY : b64Size);
        b64Geom->isCompleteB64 =
            (b64Geom->width == b64Size && b64Geom->height == b64Size) ? 1 : 0;
    }

    return EB_ErrorNone;
}

// alloc_sb_geoms (pcs.c:1512-1518) / free_sb_geoms (:1520-1524) /
// copy_sb_geoms (:1526-1530, the per-element loop verbatim) /
// sb_geom_init (:1532-1552).
EbErrorType allocSbGeoms(SbGeom** geom, int width, int height) {
    SbGeom* tmp;
    tmp = static_cast<SbGeom*>(std::malloc(sizeof(SbGeom) * static_cast<std::size_t>(width) * height));
    *geom = tmp;

    return EB_ErrorNone;
}

void freeSbGeoms(SbGeom* geom) {
    if (geom) {
        std::free(geom);
    }
}

void copySbGeoms(SbGeom* dstGeom, const SbGeom* srcGeom, std::uint16_t width, std::uint16_t height) {
    for (int i = 0; i < width * height; i++) {
        dstGeom[i] = srcGeom[i];
    }
}

EbErrorType sbGeomInit(Scs* scs, std::uint16_t width, std::uint16_t height, SbGeom** sbGeoms) {
    const std::uint16_t pictureSbWidth = static_cast<std::uint16_t>(divideAndCeil(width, scs->sbSize));
    const std::uint16_t pictureSbHeight = static_cast<std::uint16_t>(divideAndCeil(height, scs->sbSize));
    freeSbGeoms(*sbGeoms);
    const EbErrorType ret = allocSbGeoms(sbGeoms, pictureSbWidth, pictureSbHeight);
    if (ret != EB_ErrorNone) {
        return ret;
    }

    for (int sbIndex = 0; sbIndex < pictureSbWidth * pictureSbHeight; ++sbIndex) {
        SbGeom* sbGeom = &(*sbGeoms)[sbIndex];
        const std::uint16_t horIndex = static_cast<std::uint16_t>(sbIndex % pictureSbWidth);
        const std::uint16_t verIndex = static_cast<std::uint16_t>(sbIndex / pictureSbWidth);
        sbGeom->orgX = static_cast<std::uint16_t>(horIndex * scs->sbSize);
        sbGeom->orgY = static_cast<std::uint16_t>(verIndex * scs->sbSize);
        sbGeom->width = static_cast<std::uint8_t>(width - sbGeom->orgX < scs->sbSize ? width - sbGeom->orgX : scs->sbSize);
        sbGeom->height =
            static_cast<std::uint8_t>(height - sbGeom->orgY < scs->sbSize ? height - sbGeom->orgY : scs->sbSize);
    }

    return EB_ErrorNone;
}

// ---------------------------------------------------------------------------
// sequence_control_set.c spans.
// ---------------------------------------------------------------------------

// svt_aom_derive_input_resolution (sequence_control_set.c:120-139): the
// strict-< band ladder over the INPUT_SIZE_*_TH thresholds.
EbErrorType deriveInputResolution(ResolutionRange* inputResolution, std::uint32_t inputSize) {
    EbErrorType returnError = EB_ErrorNone;
    if (inputSize < inputSize240pTh) {
        *inputResolution = INPUT_SIZE_240p_RANGE;
    } else if (inputSize < inputSize360pTh) {
        *inputResolution = INPUT_SIZE_360p_RANGE;
    } else if (inputSize < inputSize480pTh) {
        *inputResolution = INPUT_SIZE_480p_RANGE;
    } else if (inputSize < inputSize720pTh) {
        *inputResolution = INPUT_SIZE_720p_RANGE;
    } else if (inputSize < inputSize1080pTh) {
        *inputResolution = INPUT_SIZE_1080p_RANGE;
    } else if (inputSize < inputSize4kTh) {
        *inputResolution = INPUT_SIZE_4K_RANGE;
    } else {
        *inputResolution = INPUT_SIZE_8K_RANGE;
    }

    return returnError;
}

// copy_sequence_control_set (sequence_control_set.c:163-:228): the carrier
// carries the deep-copied members; the reference's *dst = *src (:171)
// becomes the whole-carrier assignment.
EbErrorType copySequenceControlSet(Scs* dst, const Scs* src) {
    if (dst->sbGeom != nullptr) {
        freeSbGeoms(dst->sbGeom);
    }
    if (dst->b64Geom != nullptr) {
        std::free(dst->b64Geom);
    }
    // Copy the non-pointer members
    *dst = *src;

    dst->b64Geom = static_cast<B64Geom*>(
        std::malloc(sizeof(B64Geom) * static_cast<std::size_t>(dst->b64TotalCount)));
    std::memcpy(dst->b64Geom, src->b64Geom, sizeof(B64Geom) * dst->b64TotalCount);

    // allocate buffers and copy data preserving dst pointers
    allocSbGeoms(&dst->sbGeom, dst->pictureWidthInSb, dst->pictureHeightInSb);
    copySbGeoms(dst->sbGeom, src->sbGeom, dst->pictureWidthInSb, dst->pictureHeightInSb);

    if (src->frameScaleEvts.startFrameNums) {
        dst->frameScaleEvts.startFrameNums =
            static_cast<std::int64_t*>(std::malloc(sizeof(std::int64_t) * src->frameScaleEvts.evtNum));
        std::memcpy(dst->frameScaleEvts.startFrameNums,
                    src->frameScaleEvts.startFrameNums,
                    sizeof(std::int64_t) * src->frameScaleEvts.evtNum);
    }
    if (src->frameScaleEvts.resizeKfDenoms) {
        dst->frameScaleEvts.resizeKfDenoms =
            static_cast<std::uint32_t*>(std::malloc(sizeof(std::uint32_t) * src->frameScaleEvts.evtNum));
        std::memcpy(dst->frameScaleEvts.resizeKfDenoms,
                    src->frameScaleEvts.resizeKfDenoms,
                    sizeof(std::uint32_t) * src->frameScaleEvts.evtNum);
    }
    if (src->frameScaleEvts.resizeDenoms) {
        dst->frameScaleEvts.resizeDenoms =
            static_cast<std::uint32_t*>(std::malloc(sizeof(std::uint32_t) * src->frameScaleEvts.evtNum));
        std::memcpy(dst->frameScaleEvts.resizeDenoms,
                    src->frameScaleEvts.resizeDenoms,
                    sizeof(std::uint32_t) * src->frameScaleEvts.evtNum);
    }
    if (src->sframePosi.sframePosis) {
        dst->sframePosi.sframePosis =
            static_cast<std::uint64_t*>(std::malloc(sizeof(std::uint64_t) * src->sframePosi.sframeNum));
        std::memcpy(dst->sframePosi.sframePosis,
                    src->sframePosi.sframePosis,
                    sizeof(std::uint64_t) * src->sframePosi.sframeNum);
    }
    if (src->sframePosi.sframeQps) {
        dst->sframePosi.sframeQps = static_cast<std::uint8_t*>(std::malloc(sizeof(src->sframePosi.sframeQps[0]) *
                                                                          src->sframePosi.sframeQpNum));
        std::memcpy(dst->sframePosi.sframeQps,
                    src->sframePosi.sframeQps,
                    sizeof(src->sframePosi.sframeQps[0]) * src->sframePosi.sframeQpNum);
    }
    if (src->sframePosi.sframeQpOffsets) {
        dst->sframePosi.sframeQpOffsets = static_cast<std::int8_t*>(
            std::malloc(sizeof(src->sframePosi.sframeQpOffsets[0]) * src->sframePosi.sframeQpNum));
        std::memcpy(dst->sframePosi.sframeQpOffsets,
                    src->sframePosi.sframeQpOffsets,
                    sizeof(src->sframePosi.sframeQpOffsets[0]) * src->sframePosi.sframeQpNum);
    }

    // Continue this process for all other pointers within the struct...
    // (the reference's own comment :225 - the remaining SCS pointer members
    // are the named-parked full-field surface)

    return EB_ErrorNone;
}

// ---------------------------------------------------------------------------
// pic_buffer_desc.c spans: the layout math.
// ---------------------------------------------------------------------------

// svt_pic_buf_desc_calc (pic_buffer_desc.c:224-278): verbatim arithmetic.
std::uint32_t picBufDescCalc(PicBufferDesc* picBuf, const PicBufferDescInitData* d, std::uint32_t bufferSize[3],
                             std::uint32_t bufferBitIncSize[3], std::uint32_t* bytesPerPixelOut) {
    std::uint32_t bytesPerPixel =
        (d->bitDepth == EB_EIGHT_BIT) ? 1 : (d->bitDepth <= EB_SIXTEEN_BIT) ? 2 : 4;
    if (d->bitDepth > EB_EIGHT_BIT && d->bitDepth <= EB_SIXTEEN_BIT && d->splitMode) {
        bytesPerPixel = 1;
    }
    const std::uint16_t ssX = (d->colorFormat == EB_YUV444) ? 0 : 1;
    const std::uint16_t ssY = ((d->colorFormat == EB_YUV444 || d->colorFormat == EB_YUV422) ? 0 : 1);

    picBuf->width = d->maxWidth;
    picBuf->height = d->maxHeight;
    picBuf->maxWidth = d->maxWidth;
    picBuf->maxHeight = d->maxHeight;
    picBuf->bitDepth = d->bitDepth;
    picBuf->colorFormat = d->colorFormat;
    picBuf->yStride = static_cast<std::uint16_t>(d->maxWidth + 2 * d->border /*left + right border*/);
    picBuf->uStride = picBuf->vStride = static_cast<std::uint16_t>((picBuf->yStride + ssX) >> ssX);
    picBuf->border = d->border;
    picBuf->lumaSize = static_cast<std::uint32_t>(picBuf->yStride) * (d->maxHeight + (2 * d->border));
    picBuf->chromaSize =
        static_cast<std::uint32_t>(picBuf->uStride) * ((d->maxHeight + ssY + (2 * d->border)) >> ssY);
    picBuf->packedFlag = bytesPerPixel > 1 ? true : false;
    if (d->splitMode) {
        picBuf->yStrideBitInc = picBuf->yStride;
        picBuf->uStrideBitInc = picBuf->uStride;
        picBuf->vStrideBitInc = picBuf->vStride;
    }
    picBuf->bufferEnableMask = d->bufferEnableMask;

    std::uint32_t allocSz = 0;
    bufferSize[0] = bufferSize[1] = bufferSize[2] = 0;
    bufferBitIncSize[0] = bufferBitIncSize[1] = bufferBitIncSize[2] = 0;
    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Y_FLAG) {
        allocSz += bufferSize[0] = picBuf->lumaSize * bytesPerPixel;
        if (d->splitMode) {
            allocSz += bufferBitIncSize[0] = picBuf->lumaSize * bytesPerPixel;
        }
    }
    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Cb_FLAG) {
        allocSz += bufferSize[1] = picBuf->chromaSize * bytesPerPixel;
        if (d->splitMode) {
            allocSz += bufferBitIncSize[1] = picBuf->chromaSize * bytesPerPixel;
        }
    }
    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Cr_FLAG) {
        allocSz += bufferSize[2] = picBuf->chromaSize * bytesPerPixel;
        if (d->splitMode) {
            allocSz += bufferBitIncSize[2] = picBuf->chromaSize * bytesPerPixel;
        }
    }
    *bytesPerPixelOut = bytesPerPixel;
    return allocSz;
}

// svt_pic_buf_desc_assign (pic_buffer_desc.c:280-:323): verbatim walk.
void picBufDescAssign(PicBufferDesc* picBuf, const PicBufferDescInitData* d, std::uint8_t* base,
                      const std::uint32_t bufferSize[3], const std::uint32_t bufferBitIncSize[3],
                      std::uint32_t bytesPerPixel) {
    const std::uint16_t ssX = (d->colorFormat == EB_YUV444) ? 0 : 1;
    const std::uint16_t ssY = ((d->colorFormat == EB_YUV444 || d->colorFormat == EB_YUV422) ? 0 : 1);

    picBuf->bufferAlloc = base;
    std::uint32_t assignedSpace = 0;
    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Y_FLAG) {
        picBuf->yBuffer =
            base + (picBuf->border + (picBuf->yStride * picBuf->border)) * bytesPerPixel;
        assignedSpace += bufferSize[0];
        picBuf->yBufferBitInc = nullptr;
        if (d->splitMode) {
            picBuf->yBufferBitInc =
                base + assignedSpace +
                (picBuf->border + (picBuf->yStrideBitInc * picBuf->border)) * bytesPerPixel;
            assignedSpace += bufferBitIncSize[0];
        }
    }
    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Cb_FLAG) {
        picBuf->uBuffer =
            base + assignedSpace +
            ((picBuf->border >> ssX) + (picBuf->uStride * (picBuf->border >> ssY))) * bytesPerPixel;
        assignedSpace += bufferSize[1];
        picBuf->uBufferBitInc = nullptr;
        if (d->splitMode) {
            picBuf->uBufferBitInc =
                base + assignedSpace +
                ((picBuf->border >> ssX) +
                 (picBuf->uStrideBitInc * (picBuf->border >> ssY))) * bytesPerPixel;
            assignedSpace += bufferBitIncSize[1];
        }
    }
    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Cr_FLAG) {
        picBuf->vBuffer =
            base + assignedSpace +
            ((picBuf->border >> ssX) + (picBuf->vStride * (picBuf->border >> ssY))) * bytesPerPixel;
        assignedSpace += bufferSize[2];
        picBuf->vBufferBitInc = nullptr;
        if (d->splitMode) {
            picBuf->vBufferBitInc =
                base + assignedSpace +
                ((picBuf->border >> ssX) +
                 (picBuf->vStrideBitInc * (picBuf->border >> ssY))) * bytesPerPixel;
            assignedSpace += bufferBitIncSize[2];
        }
    }
}

// ---------------------------------------------------------------------------
// pic_buffer_desc.c spans: the ctor/update family and the aom link.
// ---------------------------------------------------------------------------

// svt_picture_buffer_desc_ctor_noy8b (pic_buffer_desc.c:35-:145): verbatim
// arithmetic. The svt_aom_assert_err stride-8 check (:55) is kept as a
// plain assert - dead under NDEBUG exactly as the reference's Release.
EbErrorType pictureBufferDescCtorNoy8b(PicBufferDesc* picBuf, const PicBufferDescInitData* d) {
    //for 10bit we force split mode + 2b-compressed
    std::uint32_t bytesPerPixel = 1;
    const std::uint16_t ssX = (d->colorFormat == EB_YUV444) ? 0 : 1;
    const std::uint16_t ssY = ((d->colorFormat == EB_YUV444 || d->colorFormat == EB_YUV422) ? 0 : 1);

    // Set the Picture Buffer Static variables
    picBuf->width = d->maxWidth;
    picBuf->height = d->maxHeight;
    picBuf->maxWidth = d->maxWidth;
    picBuf->maxHeight = d->maxHeight;
    picBuf->bitDepth = d->bitDepth;
    picBuf->colorFormat = d->colorFormat;
    picBuf->yStride = static_cast<std::uint16_t>(d->maxWidth + (2 * d->border) /*left + right border*/);

    assert(picBuf->yStride % 8 == 0);

    picBuf->uStride = picBuf->vStride = static_cast<std::uint16_t>((picBuf->yStride + ssX) >> ssX);
    picBuf->border = d->border;

    picBuf->lumaSize = static_cast<std::uint32_t>(picBuf->yStride) * (d->maxHeight + (2 * d->border));
    picBuf->chromaSize =
        static_cast<std::uint32_t>(picBuf->uStride) * ((d->maxHeight + ssY + (2 * d->border)) >> ssY);
    picBuf->packedFlag = false;

    if (d->splitMode) {
        picBuf->yStrideBitInc = picBuf->yStride;
        picBuf->uStrideBitInc = picBuf->uStride;
        picBuf->vStrideBitInc = picBuf->vStride;
    }
    picBuf->bufferEnableMask = d->bufferEnableMask;

    picBuf->yBuffer = nullptr;
    // Get frame size to alloc
    std::uint32_t allocSz = 0;
    std::uint32_t bufferSize[3] = {0};
    std::uint32_t bufferBitIncSize[3] = {0};
    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Y_FLAG) {
        if (d->splitMode) {
            allocSz += bufferBitIncSize[0] = picBuf->lumaSize * bytesPerPixel / 4;
        }
    }

    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Cb_FLAG) {
        allocSz += bufferSize[1] = picBuf->chromaSize * bytesPerPixel;
        if (d->splitMode) {
            allocSz += bufferBitIncSize[1] = picBuf->chromaSize * bytesPerPixel / 4;
        }
    }

    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Cr_FLAG) {
        allocSz += bufferSize[2] = picBuf->chromaSize * bytesPerPixel;
        if (d->splitMode) {
            allocSz += bufferBitIncSize[2] = picBuf->chromaSize * bytesPerPixel / 4;
        }
    }

    // Allocate the Picture Buffers (luma & chroma)
    if (allocSz) {
        picBuf->bufferAlloc = static_cast<std::uint8_t*>(std::malloc(allocSz));
    } else {
        picBuf->bufferAlloc = nullptr;
    }
    picBuf->bufferAllocSz = allocSz;
    std::uint32_t assignedSpace = 0;
    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Y_FLAG) {
        picBuf->yBufferBitInc = nullptr;
        if (d->splitMode) {
            picBuf->yBufferBitInc = picBuf->bufferAlloc + assignedSpace +
                ((picBuf->border + (picBuf->yStrideBitInc * picBuf->border)) / 4) * bytesPerPixel;
            assignedSpace += bufferBitIncSize[0];
        }
    }

    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Cb_FLAG) {
        picBuf->uBuffer = picBuf->bufferAlloc + assignedSpace +
            ((picBuf->border >> ssX) + (picBuf->uStride * (picBuf->border >> ssY))) * bytesPerPixel;
        assignedSpace += bufferSize[1];
        picBuf->uBufferBitInc = nullptr;
        if (d->splitMode) {
            picBuf->uBufferBitInc = picBuf->bufferAlloc + assignedSpace +
                (((picBuf->border >> ssX) + (picBuf->uStrideBitInc * (picBuf->border >> ssY))) / 4) *
                    bytesPerPixel;
            assignedSpace += bufferBitIncSize[1];
        }
    }

    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Cr_FLAG) {
        picBuf->vBuffer = picBuf->bufferAlloc + assignedSpace +
            ((picBuf->border >> ssX) + (picBuf->vStride * (picBuf->border >> ssY))) * bytesPerPixel;
        assignedSpace += bufferSize[2];
        picBuf->vBufferBitInc = nullptr;
        if (d->splitMode) {
            picBuf->vBufferBitInc = picBuf->bufferAlloc + assignedSpace +
                (((picBuf->border >> ssX) + (picBuf->vStrideBitInc * (picBuf->border >> ssY))) / 4) *
                    bytesPerPixel;
            assignedSpace += bufferBitIncSize[2];
        }
    }

    return EB_ErrorNone;
}

// svt_picture_buffer_desc_noy8b_update (pic_buffer_desc.c:152-:186).
EbErrorType pictureBufferDescNoy8bUpdate(PicBufferDesc* picBuf, const PicBufferDescInitData* d) {
    const std::uint16_t ssX = (d->colorFormat == EB_YUV444) ? 0 : 1;
    const std::uint16_t ssY = ((d->colorFormat == EB_YUV444 || d->colorFormat == EB_YUV422) ? 0 : 1);

    picBuf->width = d->maxWidth;
    picBuf->height = d->maxHeight;
    picBuf->maxWidth = d->maxWidth;
    picBuf->maxHeight = d->maxHeight;
    picBuf->bitDepth = d->bitDepth;
    picBuf->colorFormat = d->colorFormat;
    picBuf->yStride = static_cast<std::uint16_t>(d->maxWidth + (2 * d->border) /*left + right border*/);

    assert(picBuf->yStride % 8 == 0);

    picBuf->uStride = picBuf->vStride = static_cast<std::uint16_t>((picBuf->yStride + ssX) >> ssX);
    picBuf->border = d->border;

    picBuf->lumaSize = static_cast<std::uint32_t>(picBuf->yStride) * (d->maxHeight + (2 * d->border));
    picBuf->chromaSize =
        static_cast<std::uint32_t>(picBuf->uStride) * ((d->maxHeight + ssY + (2 * d->border)) >> ssY);
    picBuf->packedFlag = false;

    if (d->splitMode == true) {
        picBuf->yStrideBitInc = picBuf->yStride;
        picBuf->uStrideBitInc = picBuf->uStride;
        picBuf->vStrideBitInc = picBuf->vStride;
    }
    picBuf->bufferEnableMask = d->bufferEnableMask;

    return EB_ErrorNone;
}

// svt_picture_buffer_desc_update (pic_buffer_desc.c:192-:219): note what is
// NOT written - bit_depth, color_format, packed_flag, buffer_enable_mask.
EbErrorType pictureBufferDescUpdate(PicBufferDesc* picBuf, const PicBufferDescInitData* d) {
    const std::uint16_t ssX = (d->colorFormat == EB_YUV444) ? 0 : 1;
    const std::uint16_t ssY = ((d->colorFormat == EB_YUV444 || d->colorFormat == EB_YUV422) ? 0 : 1);

    picBuf->width = d->maxWidth;
    picBuf->height = d->maxHeight;
    picBuf->maxWidth = d->maxWidth;
    picBuf->maxHeight = d->maxHeight;
    picBuf->yStride = static_cast<std::uint16_t>(d->maxWidth + (2 * d->border) /*left + right border*/);
    picBuf->uStride = picBuf->vStride = static_cast<std::uint16_t>((picBuf->yStride + ssX) >> ssX);
    picBuf->border = d->border;

    picBuf->lumaSize = static_cast<std::uint32_t>(picBuf->yStride) * (d->maxHeight + (2 * d->border));
    picBuf->chromaSize =
        static_cast<std::uint32_t>(picBuf->uStride) * ((d->maxHeight + ssY + (2 * d->border)) >> ssY);

    if (d->splitMode) {
        picBuf->yStrideBitInc = picBuf->yStride;
        picBuf->uStrideBitInc = picBuf->uStride;
        picBuf->vStrideBitInc = picBuf->vStride;
    }

    return EB_ErrorNone;
}

// svt_recon_picture_buffer_desc_update (pic_buffer_desc.c:417-:442).
EbErrorType reconPictureBufferDescUpdate(PicBufferDesc* picBuf, const PicBufferDescInitData* d) {
    const std::uint16_t ssX = (d->colorFormat == EB_YUV444) ? 0 : 1;
    const std::uint16_t ssY = ((d->colorFormat == EB_YUV444 || d->colorFormat == EB_YUV422) ? 0 : 1);

    picBuf->width = d->maxWidth;
    picBuf->height = d->maxHeight;
    picBuf->maxWidth = d->maxWidth;
    picBuf->maxHeight = d->maxHeight;
    picBuf->bitDepth = d->bitDepth;
    picBuf->colorFormat = d->colorFormat;
    picBuf->yStride = static_cast<std::uint16_t>(d->maxWidth + (2 * d->border) /*left + right borders*/);
    picBuf->uStride = picBuf->vStride = static_cast<std::uint16_t>((picBuf->yStride + ssX) >> ssX);
    picBuf->border = d->border;

    picBuf->lumaSize = static_cast<std::uint32_t>(picBuf->yStride) * (d->maxHeight + (2 * d->border));
    picBuf->chromaSize =
        static_cast<std::uint32_t>(picBuf->uStride) * ((d->maxHeight + ssY + (2 * d->border)) >> ssY);
    picBuf->packedFlag = (d->bitDepth > EB_EIGHT_BIT) ? true : false;

    picBuf->bufferEnableMask = d->bufferEnableMask;

    return EB_ErrorNone;
}

// svt_recon_picture_buffer_desc_ctor (pic_buffer_desc.c:450-:516).
EbErrorType reconPictureBufferDescCtor(PicBufferDesc* picBuf, const PicBufferDescInitData* d) {
    const std::uint16_t ssX = (d->colorFormat == EB_YUV444) ? 0 : 1;
    const std::uint16_t ssY = ((d->colorFormat == EB_YUV444 || d->colorFormat == EB_YUV422) ? 0 : 1);

    std::uint32_t bytesPerPixel = (d->bitDepth == EB_EIGHT_BIT) ? 1 : 2;

    // Set the Picture Buffer Static variables
    picBuf->width = d->maxWidth;
    picBuf->height = d->maxHeight;
    picBuf->maxWidth = d->maxWidth;
    picBuf->maxHeight = d->maxHeight;
    picBuf->bitDepth = d->bitDepth;
    picBuf->colorFormat = d->colorFormat;
    picBuf->yStride = static_cast<std::uint16_t>(d->maxWidth + (2 * d->border) /*left + right border*/);
    picBuf->uStride = picBuf->vStride = static_cast<std::uint16_t>((picBuf->yStride + ssX) >> ssX);
    picBuf->border = d->border;

    picBuf->lumaSize = static_cast<std::uint32_t>(picBuf->yStride) * (d->maxHeight + (2 * d->border));
    picBuf->chromaSize =
        static_cast<std::uint32_t>(picBuf->uStride) * ((d->maxHeight + ssY + (2 * d->border)) >> ssY);
    picBuf->packedFlag = (d->bitDepth > EB_EIGHT_BIT) ? true : false;

    picBuf->bufferEnableMask = d->bufferEnableMask;

    // Get frame size to alloc
    std::uint32_t allocSz = 0;
    std::uint32_t bufferSize[3] = {0};
    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Y_FLAG) {
        allocSz += bufferSize[0] = picBuf->lumaSize * bytesPerPixel;
    }

    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Cb_FLAG) {
        allocSz += bufferSize[1] = picBuf->chromaSize * bytesPerPixel;
    }

    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Cr_FLAG) {
        allocSz += bufferSize[2] = picBuf->chromaSize * bytesPerPixel;
    }

    // Allocate the Picture Buffers (luma & chroma) - the zero-fill is part
    // of the reference contract (EB_CALLOC_ALIGNED_ARRAY, :493).
    picBuf->bufferAlloc = static_cast<std::uint8_t*>(std::calloc(allocSz, 1));
    picBuf->bufferAllocSz = allocSz;
    std::uint32_t assignedSpace = 0;
    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Y_FLAG) {
        picBuf->yBuffer = picBuf->bufferAlloc +
            (picBuf->border + (picBuf->yStride * picBuf->border)) * bytesPerPixel;
        assignedSpace += bufferSize[0];
    }

    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Cb_FLAG) {
        picBuf->uBuffer = picBuf->bufferAlloc + assignedSpace +
            ((picBuf->border >> ssX) + (picBuf->uStride * (picBuf->border >> ssY))) * bytesPerPixel;
        assignedSpace += bufferSize[1];
    }

    if (d->bufferEnableMask & PICTURE_BUFFER_DESC_Cr_FLAG) {
        picBuf->vBuffer = picBuf->bufferAlloc + assignedSpace +
            ((picBuf->border >> ssX) + (picBuf->vStride * (picBuf->border >> ssY))) * bytesPerPixel;
        assignedSpace += bufferSize[2];
    }

    return EB_ErrorNone;
}

// svt_aom_link_eb_to_aom_buffer_desc_8bit (pic_buffer_desc.c:518-:547).
void linkEbToAomBufferDesc8bit(PicBufferDesc* picBuffDsc, Yv12BufferConfig* aomBuffDsc) {
    // Forces an 8 bit version
    // Note: Not all fields are connected. Add more connections as needed.
    aomBuffDsc->yBuffer = picBuffDsc->yBuffer;
    aomBuffDsc->uBuffer = picBuffDsc->uBuffer;
    aomBuffDsc->vBuffer = picBuffDsc->vBuffer;

    aomBuffDsc->yWidth = picBuffDsc->width;
    aomBuffDsc->uvWidth = picBuffDsc->width / 2;

    aomBuffDsc->yHeight = picBuffDsc->height;
    aomBuffDsc->uvHeight = picBuffDsc->height / 2;

    aomBuffDsc->yStride = picBuffDsc->yStride;
    aomBuffDsc->uvStride = picBuffDsc->uStride;

    aomBuffDsc->border = picBuffDsc->border;

    aomBuffDsc->subsamplingX = 1;
    aomBuffDsc->subsamplingY = 1;

    aomBuffDsc->yCropWidth = aomBuffDsc->yWidth;
    aomBuffDsc->uvCropWidth = aomBuffDsc->uvWidth;
    aomBuffDsc->yCropHeight = aomBuffDsc->yHeight;
    aomBuffDsc->uvCropHeight = aomBuffDsc->uvHeight;

    aomBuffDsc->flags = 0;
}

// CONVERT_TO_BYTEPTR (definitions.h): (uint8_t*)(((uintptr_t)(x)) >> 1).
static std::uint8_t* convertToByteptr(const std::uint8_t* x) {
    return reinterpret_cast<std::uint8_t*>(reinterpret_cast<std::uintptr_t>(x) >> 1);
}

// svt_aom_link_eb_to_aom_buffer_desc (pic_buffer_desc.c:549-:631). The
// is_16bit parameter is consumed only by the reference's (void) cast :551 -
// the branch decision reads bit_depth directly; the parameter stays 1:1.
void linkEbToAomBufferDesc(PicBufferDesc* picBuffDsc, Yv12BufferConfig* aomBuffDsc, std::uint16_t padRight,
                           std::uint16_t padBottom, bool is16bit) {
    (void)is16bit;

    const std::int32_t ssX = 1, ssY = 1;
    //NOTe:  Not all fileds are connected. add more connections as needed.
    if (picBuffDsc->bitDepth == EB_EIGHT_BIT) {
        aomBuffDsc->yBuffer = picBuffDsc->yBuffer;
        aomBuffDsc->uBuffer = picBuffDsc->uBuffer;
        aomBuffDsc->vBuffer = picBuffDsc->vBuffer;

        aomBuffDsc->yWidth = picBuffDsc->width;
        aomBuffDsc->uvWidth = (picBuffDsc->width + ssX) >> ssX;

        aomBuffDsc->yHeight = picBuffDsc->height;
        aomBuffDsc->uvHeight = (picBuffDsc->height + ssY) >> ssY;

        aomBuffDsc->yStride = picBuffDsc->yStride;
        aomBuffDsc->uvStride = picBuffDsc->uStride;

        aomBuffDsc->border = picBuffDsc->border;

        aomBuffDsc->subsamplingX = ssX;
        aomBuffDsc->subsamplingY = ssY;

        aomBuffDsc->yCropWidth = aomBuffDsc->yWidth - padRight;
        aomBuffDsc->uvCropWidth = (aomBuffDsc->yCropWidth + ssX) >> ssX;
        aomBuffDsc->yCropHeight = aomBuffDsc->yHeight - padBottom;
        aomBuffDsc->uvCropHeight = (aomBuffDsc->yCropHeight + ssY) >> ssY;

        aomBuffDsc->flags = 0;
    } else {
        aomBuffDsc->yBuffer = convertToByteptr(picBuffDsc->yBuffer);
        aomBuffDsc->uBuffer = convertToByteptr(picBuffDsc->uBuffer);
        aomBuffDsc->vBuffer = convertToByteptr(picBuffDsc->vBuffer);

        aomBuffDsc->yWidth = picBuffDsc->width;
        aomBuffDsc->uvWidth = (picBuffDsc->width + ssX) >> ssX;

        aomBuffDsc->yHeight = picBuffDsc->height;
        aomBuffDsc->uvHeight = (picBuffDsc->height + ssY) >> ssY;

        aomBuffDsc->yStride = picBuffDsc->yStride;
        aomBuffDsc->uvStride = picBuffDsc->uStride;

        aomBuffDsc->border = picBuffDsc->border;

        aomBuffDsc->subsamplingX = ssX;
        aomBuffDsc->subsamplingY = ssY;

        aomBuffDsc->yCropWidth = aomBuffDsc->yWidth - padRight;
        aomBuffDsc->uvCropWidth = (aomBuffDsc->yCropWidth + ssX) >> ssX;
        aomBuffDsc->yCropHeight = aomBuffDsc->yHeight - padBottom;
        aomBuffDsc->uvCropHeight = (aomBuffDsc->yCropHeight + ssY) >> ssY;
        aomBuffDsc->flags = YV12_FLAG_HIGHBITDEPTH;
    }
}

} // namespace l18_picmgmt
