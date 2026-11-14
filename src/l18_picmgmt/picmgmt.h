// l18_picmgmt - the A10 picture-management surface.
// Reference: third_party/SVT-AV1/Source/Lib/Codec/pic_buffer_desc.c,
// reference_object.c, sequence_control_set.c, svt_threads.c, pcs.c.
// Struct-free carrier pattern (the RE8A DeblockContext precedent): the
// reference pokes EbPictureBufferDesc / EbReferenceObject /
// SequenceControlSet / PictureParentControlSet; the mirror carriers below
// hold EXACTLY the fields each ported span consumes, and the OS-allocation
// and scheduler walls thread as parameters. No reference headers.
#pragma once

#include <cstddef>
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
// ---------------------------------------------------------------------------
// pic_buffer_desc.c spans. The shared enums (EbSvtAv1Formats.h:101-110,
// verbatim values) and the PICTURE_BUFFER_DESC_* flags (pic_buffer_desc.h:
// 23-29, verbatim).
// ---------------------------------------------------------------------------

enum EbBitDepth {
    EB_EIGHT_BIT = 8,
    EB_TEN_BIT = 10,
    EB_TWELVE_BIT = 12,
    EB_SIXTEEN_BIT = 16,  // Not supported
    EB_THIRTYTWO_BIT = 32, // Not supported
};

enum EbColorFormat { EB_YUV400, EB_YUV420, EB_YUV422, EB_YUV444 };

constexpr std::uint32_t PICTURE_BUFFER_DESC_Y_FLAG = 1u << 0;
constexpr std::uint32_t PICTURE_BUFFER_DESC_Cb_FLAG = 1u << 1;
constexpr std::uint32_t PICTURE_BUFFER_DESC_Cr_FLAG = 1u << 2;
constexpr std::uint32_t PICTURE_BUFFER_DESC_LUMA_MASK = PICTURE_BUFFER_DESC_Y_FLAG;
constexpr std::uint32_t PICTURE_BUFFER_DESC_CHROMA_MASK = PICTURE_BUFFER_DESC_Cb_FLAG | PICTURE_BUFFER_DESC_Cr_FLAG;
constexpr std::uint32_t PICTURE_BUFFER_DESC_FULL_MASK =
    PICTURE_BUFFER_DESC_Y_FLAG | PICTURE_BUFFER_DESC_Cb_FLAG | PICTURE_BUFFER_DESC_Cr_FLAG;

// EbPictureBufferDesc (pic_buffer_desc.h:34-103), minimal carry: the fields
// the ported layout spans write or read. The union views (buffer[],
// stride[]) are carried as their named scalar fields (named deviation: the
// unions exist for indexed access this layer's spans never take). The dctor
// pointer is not carried (the object-lifecycle shell is parked).
struct PicBufferDesc {
    std::uint8_t* yBuffer;        // pic_buffer_desc.h:41
    std::uint8_t* uBuffer;        // :42
    std::uint8_t* vBuffer;        // :43
    std::uint8_t* yBufferBitInc;  // :53
    std::uint8_t* uBufferBitInc;  // :54
    std::uint8_t* vBufferBitInc;  // :55
    std::uint16_t yStride;        // :64
    std::uint16_t uStride;        // :65
    std::uint16_t vStride;        // :66
    std::uint16_t yStrideBitInc;  // :75
    std::uint16_t uStrideBitInc;  // :76
    std::uint16_t vStrideBitInc;  // :77
    std::uint8_t* bufferAlloc;    // :85
    std::size_t bufferAllocSz;    // :86
    std::uint32_t lumaSize;       // :87
    std::uint32_t chromaSize;     // :88
    bool packedFlag;              // :89
    std::uint16_t border;         // :92
    std::uint16_t maxWidth;       // :93
    std::uint16_t maxHeight;      // :94
    std::uint16_t width;          // :95
    std::uint16_t height;         // :96
    EbBitDepth bitDepth;          // :97
    EbColorFormat colorFormat;    // :98
    std::uint32_t bufferEnableMask; // :100
};

// EbPictureBufferDescInitData (pic_buffer_desc.h:234-247), verbatim fields.
struct PicBufferDescInitData {
    std::uint16_t maxWidth;
    std::uint16_t maxHeight;
    EbBitDepth bitDepth;
    EbColorFormat colorFormat;
    std::uint32_t bufferEnableMask;
    std::int32_t restUnitsPerTile;
    std::uint16_t border;
    bool splitMode;
    std::uint8_t mfmv;
    bool is16bitPipeline;
    std::int32_t sbTotalCount;
};

// The SequenceControlSet carrier: grows with the ported spans. Today it
// holds exactly the fields the geometry inits (pcs.c:1488-:1552) and the
// SCS copy (sequence_control_set.c:163-:228) consume: b64_size
// (sequence_control_set.h:131), sb_size (:135), b64_total_count (:134),
// picture_width/height_in_sb (:136-:137), the sb_geom/b64_geom pointers
// (:68/:70) and the frame_scale_evts / sframe_posi members the copy
// deep-copies.
// ---------------------------------------------------------------------------

// The SvtAv1EncConfiguration carrier: the fields the reference-object
// param updates read off scs->static_config.
struct ScsStaticConfig {
    std::uint32_t encoderBitDepth;       // SvtAv1EncConfiguration.encoder_bit_depth
    EbColorFormat encoderColorFormat;    // .encoder_color_format
    std::int32_t superresMode;           // .superres_mode (SUPERRES_NONE = 0)
    std::int32_t resizeMode;             // .resize_mode (RESIZE_NONE = 0)
};

// SUPERRES_NONE / RESIZE_NONE (EbSvtAv1Enc.h:106/:124, the enum's first
// values).
constexpr std::int32_t superresNone = 0;
constexpr std::int32_t resizeNone = 0;
// TPL_PAD (encode_context.h:35).
constexpr std::uint16_t tplPad = 32;

struct Scs {
    std::uint8_t b64Size;             // sequence_control_set.h:131
    std::uint16_t sbSize;             // :135
    std::uint16_t b64TotalCount;      // :134
    std::uint16_t sbTotalCount;       // :138
    std::uint16_t pictureWidthInSb;   // :136
    std::uint16_t pictureHeightInSb;  // :137
    std::uint16_t maxInputLumaWidth;  // :113
    std::uint16_t maxInputLumaHeight; // :114
    std::uint16_t border;             // :125
    std::uint32_t encoderBitDepth;    // :127
    std::int32_t restUnitsPerTile;    // :143
    std::uint32_t superBlockSize;     // :253
    std::uint8_t mfmvEnabled;         // :102
    bool is16bitPipeline;             // :248
    ScsStaticConfig staticConfig;
    SbGeom* sbGeom;                   // :68
    B64Geom* b64Geom;                 // :70
    FrameScaleEvts frameScaleEvts;    // static_config.frame_scale_evts (EbSvtAv1Enc.h:192-197)
    SframePosi sframePosi;            // static_config.sframe_posi (EbSvtAv1Enc.h:199-205)
};

// EbReferenceObject (reference_object.h), minimal carry: the fields
// svt_reference_param_update / svt_reference_object_reset consume.
struct RefObject {
    PicBufferDesc* referencePicture;
    std::uint32_t miRows;
    std::uint32_t miCols;
};

// EbPaReferenceObject, minimal carry: the three picture pointers the PA
// param update drives.
struct PaRefObject {
    PicBufferDesc* inputPaddedPic;
    PicBufferDesc* quarterDownsampledPicturePtr;
    PicBufferDesc* sixteenthDownsampledPicturePtr;
};

// EbTplReferenceObject, minimal carry.
struct TplRefObject {
    PicBufferDesc* refPicturePtr;
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

// ---------------------------------------------------------------------------
// pic_buffer_desc.c spans: the layout math.
// ---------------------------------------------------------------------------

// svt_pic_buf_desc_calc (pic_buffer_desc.c:224-278): compute all static
// descriptor fields and return the total backing size (+ per-plane sizes and
// bytes-per-pixel for picBufDescAssign). No allocation. File-local static in
// the reference; exported for direct testability (named deviation). The
// svt_aom_assert_err stride-8 check (:55 twin lives in the ctors) is not
// part of this shared calc - the reference's calc has no assert.
std::uint32_t picBufDescCalc(PicBufferDesc* picBuf, const PicBufferDescInitData* d, std::uint32_t bufferSize[3],
                             std::uint32_t bufferBitIncSize[3], std::uint32_t* bytesPerPixelOut);

// svt_pic_buf_desc_assign (pic_buffer_desc.c:280-:323): point the
// descriptor's y/u/v(+bit_inc) into base. File-local static in the
// reference; exported (named deviation).
void picBufDescAssign(PicBufferDesc* picBuf, const PicBufferDescInitData* d, std::uint8_t* base,
                      const std::uint32_t bufferSize[3], const std::uint32_t bufferBitIncSize[3],
                      std::uint32_t bytesPerPixel);

// Yv12BufferConfig (pic_buffer_desc.h:105-190), minimal carry: the fields
// the two link functions write.
constexpr std::int32_t YV12_FLAG_HIGHBITDEPTH = 8; // pic_buffer_desc.h:104

struct Yv12BufferConfig {
    std::int32_t yWidth;
    std::int32_t uvWidth;
    std::int32_t yHeight;
    std::int32_t uvHeight;
    std::int32_t yCropWidth;
    std::int32_t uvCropWidth;
    std::int32_t yCropHeight;
    std::int32_t uvCropHeight;
    std::int32_t yStride;
    std::int32_t uvStride;
    std::uint8_t* yBuffer;
    std::uint8_t* uBuffer;
    std::uint8_t* vBuffer;
    std::int32_t border;
    std::int32_t subsamplingX;
    std::int32_t subsamplingY;
    std::int32_t flags;
};

// svt_picture_buffer_desc_ctor_noy8b (pic_buffer_desc.c:35-:145): the
// 8-bit-biased ctor with the /4 bit-inc quirk (:80/:87/:94 and the /4
// pointer offsets :112/:124/:137) and y_buffer NEVER assigned (:72).
// bytes_per_pixel stays 1 even at 10-bit input (:39 is never recomputed).
// packed_flag forced false (:63). EB_MALLOC_ALIGNED_ARRAY is the standard
// heap here (named deviation; no alignment guarantee).
EbErrorType pictureBufferDescCtorNoy8b(PicBufferDesc* picBuf, const PicBufferDescInitData* d);

// svt_picture_buffer_desc_noy8b_update (:152-:186): the layout update with
// bit_depth/color_format/packed_flag=false/buffer_enable_mask writes.
EbErrorType pictureBufferDescNoy8bUpdate(PicBufferDesc* picBuf, const PicBufferDescInitData* d);

// svt_picture_buffer_desc_update (:192-:219): the layout update that does
// NOT touch bit_depth/color_format/packed_flag/buffer_enable_mask.
EbErrorType pictureBufferDescUpdate(PicBufferDesc* picBuf, const PicBufferDescInitData* d);

// svt_recon_picture_buffer_desc_update (:417-:442): the recon variant sets
// bit_depth/color_format/packed_flag=(depth>8)/buffer_enable_mask and does
// NOT touch the bit-inc strides.
EbErrorType reconPictureBufferDescUpdate(PicBufferDesc* picBuf, const PicBufferDescInitData* d);

// svt_recon_picture_buffer_desc_ctor (:450-:516): bytes_per_pixel = 1 (8-bit)
// or 2 (else, :456), the EB_CALLOC_ALIGNED_ARRAY zero-fill (:493, standard
// heap here - named deviation), the y/u/v pointer walk (:496-:512), NO
// split-mode handling (the bit-inc pointers are never written).
EbErrorType reconPictureBufferDescCtor(PicBufferDesc* picBuf, const PicBufferDescInitData* d);

// svt_aom_link_eb_to_aom_buffer_desc_8bit (:518-:547): the field copy with
// uv_width = width / 2 (integer division) and hardcoded subsampling 1/1.
void linkEbToAomBufferDesc8bit(PicBufferDesc* picBuffDsc, Yv12BufferConfig* aomBuffDsc);

// svt_aom_link_eb_to_aom_buffer_desc (:549-:631): the general twin with
// pad_right/pad_bottom crop arithmetic and the 16-bit branch whose pointers
// go through CONVERT_TO_BYTEPTR = (uint8_t*)((uintptr_t)(x) >> 1)
// (definitions.h) and flags = YV12_FLAG_HIGHBITDEPTH (:629).
void linkEbToAomBufferDesc(PicBufferDesc* picBuffDsc, Yv12BufferConfig* aomBuffDsc, std::uint16_t padRight,
                           std::uint16_t padBottom, bool is16bit);

// ---------------------------------------------------------------------------
// reference_object.c spans.
// ---------------------------------------------------------------------------

// initialize_samples_neighboring_reference_picture_8bit
// (reference_object.c:21-:44): zero the one-pixel ring (top row, bottom
// row, left column, right column) around the samples. File-local static in
// the reference; exported for direct testability (named deviation).
void initializeSamplesNeighboringReferencePicture8bit(std::uint8_t* reconSamplesBufferPtr, std::uint16_t stride,
                                                      std::uint16_t reconWidth, std::uint16_t reconHeight);

// initialize_samples_neighboring_reference_picture (:46-:55): the y/u/v
// wrapper with the >>1 chroma dims. File-local static; exported (named
// deviation).
void initializeSamplesNeighboringReferencePicture(PicBufferDesc* refPic);

// svt_reference_param_update (:83-:125): the SCS-derived init data (the
// super_block_size + 32 padding, the superres/resize bump), the 10-bit
// split fork, the ring zeroing on the 8-bit path and the mi grid.
EbErrorType referenceParamUpdate(RefObject* refObject, const Scs* scs);

// svt_reference_object_reset (:204-:209).
EbErrorType referenceObjectReset(RefObject* refObject, const Scs* scs);

// svt_pa_reference_param_update (:239-:288): the three PA init datas
// (luma-only quarter/sixteenth, mask 0 for the padded input).
EbErrorType paReferenceParamUpdate(PaRefObject* paRefObj, const Scs* scs);

// svt_tpl_reference_param_update (:348-:372): the TPL_PAD 32 luma-only
// init data.
EbErrorType tplReferenceParamUpdate(TplRefObject* tplRefObj, const Scs* scs);

} // namespace l18_picmgmt
