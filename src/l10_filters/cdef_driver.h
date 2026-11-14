#pragma once

#include <cstdint>

#include "cdef.h"

// A8D (A8 campaign): 1:1 port of the remaining PURE / carrier-able DRIVER
// spans of third_party/SVT-AV1/Source/Lib/Codec/enc_cdef.c, cdef_process.c
// and rest_process.c - the CDEF3 struct-walled driver chain resolves
// (the landed cdef.h PARKED list: svt_av1_cdef_frame, svt_pick_cdef_from_qp,
// propagate_cdef_strength, finish_cdef_search, and the cdef_process.c
// search/prep surfaces), plus the rest_process.c post-CDEF stage.
//
// LANDED (this header, per commit):
//   S4: compute_cdef_dist (cdef_process.c:266-:290) and
//       svt_pick_cdef_from_qp (enc_cdef.c:823-:873).
//
// The walls are threaded as named carriers one field per consumed
// reference field (the RE8A DeblockContext pattern). PARKED (named):
// the CdefContext/RestContext ctors/dctors and the FIFO/mutex machinery,
// the allocation walls (the pcs->cdef_* / context buffers are threaded as
// caller-provided storage), svt_sb_compute_cdef_list (the mi-grid walk,
// threaded as a callback), svt_av1_upscale_normative_rows (the resize
// driver, l11-walled), svt_aom_lambda_assign (the rc_process.c wall,
// threaded as precomputed lambdas), and anything untestable. No fakes.

namespace l10_filters {
namespace cdef_driver {

// compute_cdef_dist (cdef_process.c:266-:290): the is_16bit dispatch over
// the landed 16-bit / 8-bit block-level dist walkers (cdef.h
// cdefComputeDist16bit/cdefComputeDist8bit). doffset is a PIXEL offset:
// the 16-bit branch reaches ((uint16_t*)dst) + doffset, the 8-bit branch
// dst + doffset bytes.
std::uint64_t computeCdefDist(const std::uint8_t* dst, std::int32_t doffset, std::int32_t dstride,
    const std::uint8_t* src, const CdefList* dlist, std::int32_t cdefCount, CdefBlockSize bsize,
    std::int32_t coeffShift, std::uint8_t subsamplingFactor, bool is16bit);

// The svt_aom_ac_quant_qtx call-site wall (inv_transforms.c:3484): the
// landed qtx tables live in l3_transforms, which l10 does not link; the
// call site is threaded (named deviation, the LR3 scoped-court-order
// precedent). (qindex, dc_or_ac, EbBitDepth) -> int16.
using AcQuantQtxFn = std::int32_t (*)(std::int32_t qIndex, std::int32_t dcOrAc, std::int32_t bitDepth);

// The threaded walls of svt_pick_cdef_from_qp (enc_cdef.c:823-:873):
// frm_hdr->quantization_params.base_q_idx, ppcs->enhanced_pic->bit_depth
// (EbBitDepth values 8/10/12), the frame-type gate
// (KEY_FRAME || INTRA_ONLY_FRAME -> the intra branch) and the
// is_screen_content argument.
struct PickQpParams {
    std::int32_t baseQIdx;
    std::int32_t bitDepth;
    bool isKeyFrame;
    bool intraOnly;
    bool isScreenContent;
    AcQuantQtxFn acQuantQtx;
};

// svt_pick_cdef_from_qp (enc_cdef.c:823-:873): the qp-driven strength
// prediction. q = ac_quant_qtx(base_q_idx, 0, bit_depth) >> (bit_depth -
// 8); the SCREEN branch casts WITHOUT rounding (:838-:844), the inter and
// intra branches roundf (:846-:860); clamps y_f1/uv_f1 [0,15] and
// y_f2/uv_f2 [0,3]; packs primary * CDEF_SEC_STRENGTHS + secondary.
void pickCdefFromQp(const PickQpParams& p, std::int32_t* predYStrength, std::int32_t* predUvStrength);

// CdefMb: the threaded MbModeInfo surface the CDEF driver reads and
// writes - bsize (the definitions.h:883-:905 BlockSize ordinals) and
// cdef_strength (block_structures.h:103, int8). The mi grid is threaded
// as a FLAT array + stride (named deviation: the reference's jagged
// MbModeInfo** cells become one owned CdefMb array, the DeblockContext
// precedent).
struct CdefMb {
    int bsize;
    std::int8_t cdefStrength;
};

// propagate_cdef_strength (enc_cdef.c:875-:895): write the strength at
// the sb_index cell, then the 128-wide extensions: BLOCK_128X128 fans to
// right + below + diagonal (MI_SIZE_64X64 = 16 mi steps), BLOCK_128X64 to
// right, BLOCK_64X128 to below, everything else only the base cell.
void propagateCdefStrength(CdefMb* miGrid, std::int32_t miStride, std::int32_t sbIndex, int bsize,
    std::int8_t strength);

// ---------------------------------------------------------------------------
// S6: cdef_process.c prep surfaces (the set/copy/recon trio)
// ---------------------------------------------------------------------------

// CdefPic: the threaded EbPictureBufferDesc surface (pic_buffer_desc.h
// :40-:97), verbatim fields; the unions are flattened (y_buffer ==
// buffer[0] etc.). bitDepth/colorFormat thread as ints (named deviation,
// the LfSearchPic precedent).
struct CdefPic {
    std::uint8_t* buffer[3];
    std::uint16_t stride[3];
    std::uint8_t strideBitInc[3];
    std::uint8_t* bufferAlloc;
    std::size_t bufferAllocSz;
    std::uint32_t lumaSize;
    std::uint32_t chromaSize;
    bool packedFlag;
    std::uint16_t border;
    std::uint16_t maxWidth;
    std::uint16_t maxHeight;
    std::uint16_t width;
    std::uint16_t height;
    int bitDepth;
    int colorFormat;
    std::uint32_t bufferEnableMask;
};

// PICTURE_BUFFER_DESC_* (pic_buffer_desc.h:23-:28).
constexpr std::uint32_t kPicBufYFlag = 1 << 0;
constexpr std::uint32_t kPicBufCbFlag = 1 << 1;
constexpr std::uint32_t kPicBufCrFlag = 1 << 2;
constexpr std::uint32_t kPicBufLumaMask = kPicBufYFlag;
constexpr std::uint32_t kPicBufFullMask = kPicBufYFlag | kPicBufCbFlag | kPicBufCrFlag;

// The threaded SequenceControlSet surface of copy_recon_enc /
// set_unscaled_input_16bit: seq_header.color_config.mono_chrome,
// subsampling_x/y and is_16bit_pipeline.
struct CdefScsWalls {
    bool monoChrome;
    int subsamplingX;
    int subsamplingY;
    bool is16bitPipeline;
};

// copy_recon_enc (cdef_process.c:46-:150): the 12 header field copies
// dst <- src, the mono-chrome buffer-enable mask, the per-plane
// alloc-size accumulation, the plane pointer setup (border arithmetic
// scaled by the subsampling and bytes-per-pixel) and the row copy loop -
// copy_width = ((width + sub_x) >> sub_x) * sizeof(*src_buf) <<
// use_highbd. The EB_CALLOC_ALIGNED_ARRAY allocation wall is threaded as
// the caller-provided bufferAlloc (named deviation).
void copyReconEnc(const CdefScsWalls& scs, const CdefPic& src, CdefPic& dst, int numPlanes, bool skipCopy,
    std::uint8_t* bufferAlloc);

// The three callees of set_unscaled_input_16bit, threaded (the
// pic-buffer walls; the pack_2d dispatch callees are PU1's landed
// packunpack in l9, unlinkable from l10).
using CopyBufferInfoFn = void (*)(const CdefPic& input, CdefPic& output);
using Convert8To16Fn = void (*)(const CdefPic& input, CdefPic& output, int ssX, int ssY);
using Pack2dPicFn = void (*)(const CdefPic& input, CdefPic& output);

// set_unscaled_input_16bit (cdef_process.c:31-:44): copy_buffer_info
// always, then convert_pic_8bit_to_16bit at EB_EIGHT_BIT else pack_2d_pic.
void setUnscaledInput16bit(const CdefScsWalls& scs, const CdefPic& input, CdefPic& output, CopyBufferInfoFn copyInfo,
    Convert8To16Fn convert8To16, Pack2dPicFn pack2dPic);

} // namespace cdef_driver
} // namespace l10_filters
