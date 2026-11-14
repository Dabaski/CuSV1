// l18_picmgmt - pcs.c spans (picture-parent-control-set layout helpers).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/pcs.c.
#include "picmgmt.h"

#include <cstdlib>

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

} // namespace l18_picmgmt
