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

// ---------------------------------------------------------------------------
// S7: cdef_seg_search (cdef_process.c:292-:666) - the per-segment strength
// search over the 64x64 filter blocks.
// ---------------------------------------------------------------------------

// CdefFbEntry (pcs.h:157-:160): the per-fb dlist/count cache the SB=64
// apply reuses. dlist holds the max 8x8 sub-blocks of a 64x64 fb.
struct CdefFbEntry {
    std::int32_t cdefCount;
    CdefList dlist[64];
};

// CdefDirData (pcs.h:150-:153): the per-fb dir/var scratch.
struct CdefDirData {
    std::uint8_t dir[kCdefNblocks][kCdefNblocks];
    std::int32_t var[kCdefNblocks][kCdefNblocks];
};

// The threaded CdefSearchControls surface (pcs.h:554-:591): the fields
// cdef_seg_search and finish_cdef_search consume. default_first_pass_fs/
// uv and default_second_pass_fs/uv are the full TOTAL_STRENGTHS arrays
// (64).
struct CdefSearchControls {
    int firstPassFsNum;
    int defaultSecondPassFsNum;
    std::uint8_t defaultFirstPassFs[kCdefTotalStrengths];
    std::uint8_t defaultSecondPassFs[kCdefTotalStrengths];
    std::int8_t defaultFirstPassFsUv[kCdefTotalStrengths];
    std::int8_t defaultSecondPassFsUv[kCdefTotalStrengths];
    std::uint8_t subsamplingFactor;
    // the finish_cdef_search fields
    std::int8_t useReferenceCdefFs;
    std::int8_t predYF;
    std::int8_t predUvF;
    bool uvFromY;
    bool useQpStrength;
};

// The threaded CdefContext scratch (cdef_process.c:211-:222): the hoisted
// buffers. tmpDst is uint16 to accommodate hbd content (8 bit treats it
// as a uint8 buffer and uses half).
struct CdefSegScratch {
    std::uint16_t* tmpDst; // 1 << (MAX_SB_SIZE_LOG2 * 2)
    std::uint16_t* inbuf; // CDEF_INBUF_SIZE (16-bit sentinel path)
    std::uint8_t* inbuf8; // CDEF_INBUF_SIZE (native interior path)
};

// CDEF_INBUF_SIZE (cdef.h:41): CDEF_BSTRIDE * ((1 << MAX_SB_SIZE_LOG2) +
// 2 * CDEF_VBORDER) = 144 * 134.
constexpr int kCdefInbufSize = kCdefBstride * ((1 << 7) + 2 * 3);

// The mse_seg carrier (pcs.h:205): two arrays of pointers to
// TOTAL_STRENGTHS-wide rows, indexed [plane][fb][strength].
using CdefMseSeg = std::uint64_t (*[2])[kCdefTotalStrengths];

// The svt_sb_compute_cdef_list call-site wall (enc_cdef.c:153-:184, the
// mi-grid walk): threaded as a callback. (pcsMiGrid, miStride, miRow,
// miCol, dlist, bs) -> count. bs threads as the BlockSize ordinal.
using ComputeCdefListFn = std::int32_t (*)(CdefMb* miGrid, std::int32_t miStride, std::int32_t miRow,
    std::int32_t miCol, CdefList* dlist, int bs);

// CdefSegCtx: the threaded PictureControlSet / SequenceControlSet /
// PictureParentControlSet surface of cdef_seg_search, one named field per
// consumed reference field (the DeblockContext pattern).
struct CdefSegCtx {
    // pcs->mi_grid_base / pcs->mi_stride / cm->mi_rows / cm->mi_cols
    CdefMb* miGrid;
    std::int32_t miStride;
    std::int32_t miRows;
    std::int32_t miCols;
    // ppcs->aligned_width / aligned_height (the b64 segment geometry)
    std::int32_t alignedWidth;
    std::int32_t alignedHeight;
    // pcs->cdef_segments_column_count / row_count
    int cdefSegmentsColumnCount;
    int cdefSegmentsRowCount;
    // ppcs->cdef_search_ctrls
    CdefSearchControls ctrls;
    // scs->seq_header.sb_size == BLOCK_64X64, scs->static_config.
    // encoder_bit_depth, SVT_EFFECTIVE_IS_16BIT_PIPELINE
    bool sb64;
    int encoderBitDepth;
    bool is16bit;
    // frm_hdr->quantization_params.base_q_idx (the damping)
    std::int32_t baseQIdx;
    // pcs->cdef_input_recon / cdef_input_source (the plane pointers) and
    // the recon/input strides
    const std::uint8_t* src[3];
    const std::uint8_t* ref[3];
    std::int32_t strideSrc[3];
    std::int32_t strideRef[3];
    // pcs->mse_seg / skip_cdef_seg / cdef_dir_data / cdef_fb_list
    CdefMseSeg mseSeg;
    std::uint8_t* skipCdefSeg;
    CdefDirData* cdefDirData;
    CdefFbEntry* cdefFbList;
    // the CdefContext hoisted scratch
    CdefSegScratch scratch;
    // the svt_sb_compute_cdef_list call-site wall
    ComputeCdefListFn computeCdefList;
};

// cdef_seg_search (cdef_process.c:292-:666): for each 64x64 filter block
// in the segment and each plane, search the allowable strength pairs:
// the 16-bit sentinel gather (the incremental VERY_LARGE reset logic) or
// the native 8-bit halo gather, the per-plane_bsize subsampling cap, the
// first/second pass strength loops over the landed fb filters, and the
// mse_seg accumulation (V folds into the U row - the pli < 2 vs else
// quirk). The chroma -1 mask gate pins default_mse_uv * 64 and skips the
// filter call.
void cdefSegSearch(CdefSegCtx& ctx, std::uint32_t segmentIndex);

// ---------------------------------------------------------------------------
// S8: svt_av1_cdef_frame (enc_cdef.c:300-:697) - the frame apply driver.
// ---------------------------------------------------------------------------

// CDEF_MAX_STRENGTHS (definitions.h:1440).
constexpr int kCdefMaxStrengths = 16;

// CdefFrameCtx: the threaded SequenceControlSet / PictureControlSet /
// PictureParentControlSet surface, one named field per consumed reference
// field. The persistent scratch (pcs->cdef_row_cdef / cdef_linebuf /
// cdef_colbuf, the grow-on-demand allocation wall) and the stack src/src8
// buffers (CDEF_INBUF_SIZE each) are threaded as caller-provided storage
// (named deviation - the allocation walls are parked).
struct CdefFrameCtx {
    // cm: mi_rows / mi_cols / mi_stride; pcs->mi_grid_base (the mbmi
    // reads: bsize + cdef_strength)
    std::int32_t miRows;
    std::int32_t miCols;
    std::int32_t miStride;
    CdefMb* miGrid;
    // frm_hdr->cdef_params (the strengths + damping)
    std::uint8_t cdefYStrength[kCdefMaxStrengths];
    std::uint8_t cdefUvStrength[kCdefMaxStrengths];
    std::int32_t cdefDamping;
    // ppcs->nb_cdef_strengths
    std::int32_t nbCdefStrengths;
    // ppcs->cdef_search_ctrls gates
    bool useReferenceCdefFs;
    bool useQpStrength;
    // scs->super_block_size / static_config.encoder_bit_depth /
    // SVT_EFFECTIVE_IS_16BIT_PIPELINE / av1_num_planes
    std::uint32_t sbSize;
    int encoderBitDepth;
    bool is16bit;
    int numPlanes;
    // the recon pic (svt_aom_get_recon_pic threaded): buffer + stride
    std::uint8_t* reconBuf[3];
    std::uint32_t reconStride[3];
    // the persistent apply scratch: row flags (caller-provided, at least
    // (nhfb+2)*2 bytes) and the per-plane halo buffers
    std::uint8_t* rowCdef;
    std::uint16_t* linebuf[3];
    std::uint16_t* colbuf[3];
    // the search scratch: pcs->cdef_fb_list (the SB=64 dlist cache) and
    // pcs->cdef_dir_data
    CdefFbEntry* cdefFbList;
    CdefDirData* cdefDirData;
    // the stack buffers of the reference (CDEF_INBUF_SIZE each; the
    // 16-bit sentinel buffer is used only on the !is_16bit... rather the
    // non-native path, the 8-bit mirror only on the native path)
    std::uint16_t* src16;
    std::uint8_t* src8;
    // the svt_sb_compute_cdef_list call-site wall (the non-cache path)
    ComputeCdefListFn computeCdefList;
};

// svt_av1_cdef_frame (enc_cdef.c:300-:697): the frame apply driver -
// per 64x64 fb: the strength-pair read (the nb_cdef_strengths > 1
// per-SB index), the dlist cache vs the compute_cdef_list callback, the
// zero-strength skip, the per-plane 8-bit cdefApplyFb path or the 16-bit
// sentinel copy dance (copy_sb8_16 body + linebuf/colbuf halos + the
// frame-edge VERY_LARGE fills + cdefFilterFb), the curr/prev row-flag
// swap.
void cdefFrame(CdefFrameCtx& ctx);

// ---------------------------------------------------------------------------
// S9: finish_cdef_search (enc_cdef.c:897-:1094) - the search finisher.
// ---------------------------------------------------------------------------

// CdefFinishCtx: the threaded PictureControlSet / PictureParentControlSet
// / FrameHeader surface. The svt_aom_lambda_assign call site
// (rc_process.c:466, the rc wall) is threaded as the precomputed
// fullLambda (named deviation); the RDCOST macro (rd_cost.h:36-38,
// ROUND_POWER_OF_TWO(R*RM, AV1_PROB_COST_SHIFT=9) + D << RDDIV_BITS=7)
// and av1_cost_literal (md_rate_estimation.h:31, n << 9) are carried
// locally (l10 does not link l16's rd surface).
struct CdefFinishCtx {
    // frm_hdr->cdef_params (write) + quantization_params.base_q_idx
    std::uint8_t cdefBits;
    std::uint8_t cdefYStrength[kCdefMaxStrengths];
    std::uint8_t cdefUvStrength[kCdefMaxStrengths];
    std::uint8_t cdefDamping;
    std::int32_t baseQIdx;
    // ppcs->nb_cdef_strengths (write), ppcs->scs->allintra,
    // ppcs->sc_class1 / sc_class5
    std::int32_t nbCdefStrengths;
    bool allintra;
    std::uint8_t scClass1;
    std::uint8_t scClass5;
    // pcs->ppcs->cdef_search_ctrls
    CdefSearchControls ctrls;
    // pcs->ppcs->cdef_recon_ctrls.zero_fs_cost_bias (0 = off)
    std::uint16_t zeroFsCostBias;
    // the zero_fs_cost_bias 16-bit check:
    // SVT_EFFECTIVE_BIT_DEPTH(encoder_bit_depth) > EB_EIGHT_BIT
    bool is16bitPipeline;
    // pcs walls: the mi grid + stride (the per-SB reads/writes),
    // cm->mi_rows/cols, skip_cdef_seg, the persistent scratch
    // (pcs->cdef_sb_index), mse_seg, the per-sb row-pointer scratch
    // (pcs->cdef_mse_ptr[2], caller-provided arrays sized sbCount) and the
    // cdef_dist_dev write
    CdefMb* miGrid;
    std::int32_t miStride;
    std::int32_t miRows;
    std::int32_t miCols;
    std::uint8_t* skipCdefSeg;
    std::int32_t* sbIndex;
    CdefMseSeg mseSeg;
    std::uint64_t** sbMsePtr[2];
    std::int32_t cdefDistDev;
    // the use_qp_strength path forwards to pickCdefFromQp with the SAME
    // walls this carrier holds: the frame-type gate, the bit depth and the
    // ac-quant qtx call-site wall
    bool qpIsKeyFrame;
    bool qpIntraOnly;
    AcQuantQtxFn acQuantQtx;
    // the svt_aom_lambda_assign wall (full_lambda only is consumed)
    std::uint32_t fullLambda;
};

// finish_cdef_search (enc_cdef.c:897-:1094): the use_qp_strength path
// (the pickCdefFromQp integration), the use_reference_cdef_fs early
// return, the per-SB mse pointer gather (the 128-block skip conditions +
// skip_cdef_seg), the zero_fs_cost_bias in-place mse scaling (16-bit vs
// 8-bit branch values), the zero-cost RDCOST, the 4-iteration
// nb_strength_bits RDO over the landed cdefJointStrengthSearchDual, the
// cdef_dist_dev derivation, the per-SB best-gi assignment through the
// landed propagateCdefStrength, and the filter_map remap.
void finishCdefSearch(CdefFinishCtx& ctx);

} // namespace cdef_driver
} // namespace l10_filters
