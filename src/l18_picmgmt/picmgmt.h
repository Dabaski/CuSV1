// l18_picmgmt - the A10 picture-management surface.
// Reference: third_party/SVT-AV1/Source/Lib/Codec/pic_buffer_desc.c,
// reference_object.c, sequence_control_set.c, svt_threads.c, pcs.c.
// Struct-free carrier pattern (the RE8A DeblockContext precedent): the
// reference pokes EbPictureBufferDesc / EbReferenceObject /
// SequenceControlSet / PictureParentControlSet; the mirror carriers below
// hold EXACTLY the fields each ported span consumes, and the OS-allocation
// and scheduler walls thread as parameters. No reference headers.
#pragma once

#include <cstdint>

namespace l18_picmgmt {

// ---------------------------------------------------------------------------
// pcs.c spans. The shared enums/constants (verbatim values).
// ---------------------------------------------------------------------------

// ResolutionRange (definitions.h:1808-1817, verbatim). Duplicated from
// l16_rdo/mode_config.h - l18 cannot link l16 (the SYM1 duplication
// precedent).
enum ResolutionRange {
    INPUT_SIZE_240p_RANGE = 0,
    INPUT_SIZE_360p_RANGE = 1,
    INPUT_SIZE_480p_RANGE = 2,
    INPUT_SIZE_720p_RANGE = 3,
    INPUT_SIZE_1080p_RANGE = 4,
    INPUT_SIZE_4K_RANGE = 5,
    INPUT_SIZE_8K_RANGE = 6,
    INPUT_SIZE_COUNT = 7,
};

// The resolution thresholds (definitions.h:1819-1824), verbatim.
constexpr std::uint32_t inputSize240pTh = 0x28500;   // 0.165 Million
constexpr std::uint32_t inputSize360pTh = 0x4CE00;   // 0.315 Million
constexpr std::uint32_t inputSize480pTh = 0xA1400;   // 0.661 Million
constexpr std::uint32_t inputSize720pTh = 0x16DA00;  // 1.5 Million
constexpr std::uint32_t inputSize1080pTh = 0x535200; // 5.46 Million
constexpr std::uint32_t inputSize4kTh = 0x140A000;   // 21 Million

// SvtAv1FrameScaleEvts (EbSvtAv1Enc.h:192-197), verbatim fields.
struct FrameScaleEvts {
    std::uint32_t evtNum;
    std::int64_t* startFrameNums;
    std::uint32_t* resizeKfDenoms;
    std::uint32_t* resizeDenoms;
};

// SvtAv1SFramePositions (EbSvtAv1Enc.h:199-205), verbatim fields.
struct SframePosi {
    std::uint32_t sframeNum;
    std::uint64_t* sframePosis;
    std::uint32_t sframeQpNum;
    std::uint8_t* sframeQps;
    std::int8_t* sframeQpOffsets;
};

// EbErrorType (EbSvtAv1.h:122-140): only the values the ported spans return.
enum EbErrorType {
    EB_ErrorNone = 0,
    EB_ErrorSemaphoreUnresponsive = static_cast<int>(0x80002021),
    EB_ErrorDestroySemaphoreFailed = static_cast<int>(0x80002022),
    EB_ErrorMutexUnresponsive = static_cast<int>(0x80002031),
    EB_ErrorDestroyMutexFailed = static_cast<int>(0x80002032),
};

// MI_SIZE_LOG2 (definitions.h:353).
constexpr std::int32_t miSizeLog2 = 2;
// RESTORATION_UNITSIZE_MAX (restoration.h:80).
constexpr std::int32_t restorationUnitSizeMax = 256;

// RestorationInfo (restoration.h:224-226), minimal carry: only the one field
// set_restoration_unit_size writes (named deviation: the
// frame_restoration_type and the svt_av1_alloc_restoration_struct fields are
// not carried).
struct RestorationInfo {
    std::int32_t restorationUnitSize;
};

// SegmentationNeighborMap (segmentation_params.h:29-33), verbatim fields.
struct SegmentationNeighborMap {
    std::uint8_t* data;
    std::uint32_t mapSize;
};

// svt_aom_get_out_buffer_size (pcs.c:374-379): the raw 4:2:0 frame size / 4
// as the starting bitstream capacity; the entropy writer grows on demand.
std::uint32_t getOutBufferSize(std::uint32_t pictureWidth, std::uint32_t pictureHeight);

// svt_aom_get_max_allocated_me_refs (pcs.c:88-93): the ME results
// allocation sizes; the list0 - 1 term is integer arithmetic and wraps in
// the uint8_t out-param.
void getMaxAllocatedMeRefs(std::uint8_t refCountUsedList0, std::uint8_t refCountUsedList1,
                           std::uint8_t* maxRefToAlloc, std::uint8_t* maxCandToAlloc);

// set_restoration_unit_size (pcs.c:29-41): file-local static in the
// reference; exported for direct testability (named deviation, LR1 boxsum1
// precedent). The width/height/sx/sy parameters are (void)-cast in the
// reference and stay in the signature 1:1.
void setRestorationUnitSize(std::int32_t width, std::int32_t height, std::int32_t sx, std::int32_t sy,
                            RestorationInfo* rst);

// segmentation_map_ctor (pcs.c:69-77). The reference's EB_CALLOC_ARRAY is
// the standard calloc here (named deviation: carrier objects use the
// standard heap); segmentation_map_dctor (pcs.c:58-61) frees it.
EbErrorType segmentationMapCtor(SegmentationNeighborMap* segNeighborMap, std::uint16_t picWidth,
                                std::uint16_t picHeight);
void segmentationMapDctor(SegmentationNeighborMap* segNeighborMap);

// B64Geom (pcs.h:406-412), verbatim fields.
struct B64Geom {
    std::uint16_t orgX;
    std::uint16_t orgY;
    std::uint8_t width;
    std::uint8_t height;
    std::uint8_t isCompleteB64;
};

// SbGeom (pcs.h:414-418), verbatim fields.
struct SbGeom {
    std::uint16_t orgX;
    std::uint16_t orgY;
    std::uint8_t width;
    std::uint8_t height;
};

// ---------------------------------------------------------------------------
// The SequenceControlSet carrier: grows with the ported spans. Today it
// holds exactly the fields the geometry inits (pcs.c:1488-:1552) and the
// SCS copy (sequence_control_set.c:163-:228) consume: b64_size
// (sequence_control_set.h:131), sb_size (:135), b64_total_count (:134),
// picture_width/height_in_sb (:136-:137), the sb_geom/b64_geom pointers
// (:68/:70) and the frame_scale_evts / sframe_posi members the copy
// deep-copies.
// ---------------------------------------------------------------------------

struct Scs {
    std::uint8_t b64Size;             // sequence_control_set.h:131
    std::uint16_t sbSize;             // :135
    std::uint16_t b64TotalCount;      // :134
    std::uint16_t pictureWidthInSb;   // :136
    std::uint16_t pictureHeightInSb;  // :137
    SbGeom* sbGeom;                   // :68
    B64Geom* b64Geom;                 // :70
    FrameScaleEvts frameScaleEvts;    // static_config.frame_scale_evts (EbSvtAv1Enc.h:192-197)
    SframePosi sframePosi;            // static_config.sframe_posi (EbSvtAv1Enc.h:199-205)
};

// DIVIDE_AND_CEIL (utility.h): ((x) + ((y) - 1)) / (y).
constexpr std::uint32_t divideAndCeil(std::uint32_t x, std::uint32_t y) {
    return (x + (y - 1)) / y;
}

// b64_geom_init (pcs.c:1488-1510). The reference's EB_MALLOC_ARRAY /
// EB_FREE_ARRAY are the standard heap here (named deviation). Returns the
// malloc'd grid in *b64Geoms.
EbErrorType b64GeomInit(Scs* scs, std::uint16_t width, std::uint16_t height, B64Geom** b64Geoms);

// alloc_sb_geoms (pcs.c:1512-1518), free_sb_geoms (:1520-1524),
// copy_sb_geoms (:1526-1530, the per-element loop, NOT memcpy), and
// sb_geom_init (:1532-1552).
EbErrorType allocSbGeoms(SbGeom** geom, int width, int height);
void freeSbGeoms(SbGeom* geom);
void copySbGeoms(SbGeom* dstGeom, const SbGeom* srcGeom, std::uint16_t width, std::uint16_t height);
EbErrorType sbGeomInit(Scs* scs, std::uint16_t width, std::uint16_t height, SbGeom** sbGeoms);

// ---------------------------------------------------------------------------
// sequence_control_set.c spans.
// ---------------------------------------------------------------------------

// svt_aom_derive_input_resolution (sequence_control_set.c:120-139): the
// strict-< band ladder over the definitions.h:1819-1824 thresholds.
EbErrorType deriveInputResolution(ResolutionRange* inputResolution, std::uint32_t inputSize);

// copy_sequence_control_set (sequence_control_set.c:163-:228): free the dst
// geometry arrays, whole-copy the carrier scalars (*dst = *src, :171),
// deep-copy b64_geom (:173-:174), sb_geom (:177-:178), and the three
// frame_scale_evts (:180-:200) + three sframe_posi (:201-:223) arrays when
// the src pointer is non-null. The carrier's non-carried SCS members are
// named-parked (the full SCS field surface).
EbErrorType copySequenceControlSet(Scs* dst, const Scs* src);

} // namespace l18_picmgmt
