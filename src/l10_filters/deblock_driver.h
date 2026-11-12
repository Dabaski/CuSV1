#pragma once

#include <cstdint>

#include "filters.h"

// RE8a (A8 campaign, machinery rung): the deblocking DRIVER machinery of
// third_party/SVT-AV1/Source/Lib/Codec/deblocking_filter.c plus the two
// driver-side spans of deblocking_common.c the driver calls. THIS SLICE IS
// CONFORMANCE-FREE: every wall (PictureControlSet / SequenceControlSet /
// FrameHeader / MacroblockdPlane / EbPictureBufferDesc / EbReferenceObject)
// is threaded as a parameter or a struct field; the wiring rung (RE8b: the
// frame-header level + the post-recon filter pass + the goldens) comes
// later.
// LANDED so far: loopFilterInit (deblocking_filter.c:82-97).
// A8W1 (the walls rung): the deblocking_filter.c walls :711-:1286 land in
// groups G5-G9 - svt_copy_buffer :711-:740 + picture_sse_calculations
// :742-:770 + try_filter_frame :772-:822 (the SD copy / SSE pair, 101 nb),
// search_filter_level :828-:953 (113 nb, the search-based pick),
// me_based_dlf_skip :954-:1010 (55 nb, the ME-based skip heuristic) and
// svt_av1_pick_filter_level :1110-:1286 (158 nb, the search-based pick
// driver) plus the disable_dlf_th table :27-:32 (6 nb) = 425 nb. The
// EbPictureBufferDesc / PictureControlSet / SequenceControlSet /
// EbReferenceObject walls are threaded as LfSearchPic / DlfWalls /
// DlfRefStat carriers (the shapes the staged test_deblock_walls.cpp
// drives). PARKED (named): the EB_NEW/EB_DELETE temp-recon allocation
// (:1163-:1171, :1281-:1282) and the svt_aom_get_recon_pic selector
// (:796, :866) - the recon/temp pictures are threaded as fields and
// parameters; the compound-pair ref walk (av1_set_ref_frame + the
// EbReferenceObject deref) reduces to the DlfRefStat rows.
// Also carried locally (not linked): the small lookup tables the driver
// arithmetic consumes (mode_lf_lut, txsize_horz/vert_map, tx_size_wide/
// high, eb_tx_size_*_unit, block_size_wide/high, mi_size_wide/high,
// svt_aom_ss_size_lookup, blocksize_to_txsize, delta_lf_id_lut,
// seg_lvl_lf_lut, inter_frame_multiplier) - each cited to its reference
// span; av1_get_adjusted_tx_size (common_utils.h:98-110) and
// av1_get_max_uv_txsize (common_utils.h:142-149) are ported locally for
// the same reason.

namespace l10_filters {
namespace deblock {

// MAX_LOOP_FILTER (definitions.h:1652) - the DF1 header's constant reused.
constexpr int kMaxLoopFilter = 63;

// MAX_SEGMENTS (definitions.h:1674), REF_FRAMES (definitions.h:1367),
// MAX_MODE_LF_DELTAS (definitions.h:389), MAX_PLANES (definitions.h:683:
// PLANE_Y, PLANE_U, PLANE_V).
constexpr int kMaxSegments = 8;
constexpr int kRefFrames = 8;
constexpr int kMaxModeLfDeltas = 2;
constexpr int kMaxPlanes = 3;

// SEG_LVL_MAX (segmentation_params.h:26); SEG_LVL_ALT_LF_Y_V/H/U/V are
// features 1/2/3/4 of that enum (segmentation_params.h:19-22).
constexpr int kSegLvlMax = 8;

// MI_SIZE (definitions.h:353-354): MI_SIZE_LOG2 = 2, MI_SIZE = 4.
constexpr int kMiSize = 4;

// TX_INVALID (definitions.h:979).
constexpr int kTxInvalid = 255;

// TxSize - the 19-value shape list in definitions.h:955-974 order (the
// same order as l3_transforms::TxSize; carried locally because the driver
// works in int ordinals and l10 does not link l3/l7/l14/l16).
enum TxSize {
    TX_4X4,
    TX_8X8,
    TX_16X16,
    TX_32X32,
    TX_64X64,
    TX_4X8,
    TX_8X4,
    TX_8X16,
    TX_16X8,
    TX_16X32,
    TX_32X16,
    TX_32X64,
    TX_64X32,
    TX_4X16,
    TX_16X4,
    TX_8X32,
    TX_32X8,
    TX_16X64,
    TX_64X16,
};

// EdgeDir (deblocking_common.h:22).
enum EdgeDir { VERT_EDGE = 0, HORZ_EDGE = 1, NUM_EDGE_DIRS };

// DeblockMi - the threaded MbModeInfo surface: the six fields the driver
// reads (mbmi->bsize, block_mi.mode, block_mi.ref_frame[0],
// block_mi.tx_depth, segment_id, block_mi.skip - deblocking_filter.c
// :252-254 and :269-276/:288-300). Ordinals: bsize/mode/refFrame0 are the
// definitions.h enum orders (BlockSize :883-905, PredictionMode
// :1167-1206, MvReferenceFrame :1359+); the l7/l14/l16 ports share them.
struct DeblockMi {
    int bsize;
    int mode;
    int refFrame0;
    int txDepth;
    int segmentId;
    bool skip;
};

// DeblockPlane - the threaded MacroblockdPlane surface the driver reads:
// the Buf2D dst fields the walks consume (dst.buf/width/height/stride -
// Buf2D is written by setup_pred_plane, deblocking_filter.c:124-128) plus
// the plane geometry fields (pd->subsampling_x/y, pd->is_16bit). buf0 is
// NOT carried (named deviation: the deblocking driver never reads it; the
// reference writes it at deblocking_filter.c:125).
struct DeblockPlane {
    std::uint8_t* buf;
    int width;
    int height;
    int stride;
    int subsamplingX;
    int subsamplingY;
    bool is16bit;
};

// LfFrameParams - the threaded FrameHeader surface the driver reads: the
// LoopFilter struct (definitions.h:1656-1672: filter_level[2],
// filter_level_u/v, sharpness_level, mode_ref_delta_enabled, ref_deltas
// [REF_FRAMES], mode_deltas[MAX_MODE_LF_DELTAS], combine_vert_horz_lf),
// the delta_lf_params gate pair (frm_hdr->delta_lf_params.delta_lf_present
// / delta_lf_multi) and the SegmentationParams fields the level
// derivation consumes (segmentation_enabled, feature_enabled[MAX_SEGMENTS]
// [SEG_LVL_MAX], feature_data[...] - segmentation_params.h:57-60).
struct LfFrameParams {
    int filterLevel[2];
    int filterLevelU;
    int filterLevelV;
    int sharpnessLevel;
    int combineVertHorzLf;
    std::uint8_t modeRefDeltaEnabled;
    std::int8_t refDeltas[kRefFrames];
    std::int8_t modeDeltas[kMaxModeLfDeltas];
    bool deltaLfPresent;
    bool deltaLfMulti;
    bool segmentationEnabled;
    std::int16_t featureEnabled[kMaxSegments][kSegLvlMax];
    std::int16_t featureData[kMaxSegments][kSegLvlMax];
};

// LfInfoN - the threaded LoopFilterInfoN (definitions.h:1684-1688): the
// landed DF1 lfthr table (l10_filters::LoopFilterInfoN) + the lvl table
// the frame-init driver fills (uint8 lvl[MAX_PLANES][MAX_SEGMENTS][2]
// [REF_FRAMES][MAX_MODE_LF_DELTAS], definitions.h:1686).
struct LfInfoN {
    LoopFilterInfoN thr;
    std::uint8_t lvl[kMaxPlanes][kMaxSegments][2][kRefFrames][kMaxModeLfDeltas];
};

// DeblockContext - the threaded PictureControlSet/SequenceControlSet
// surface, one named field per consumed reference field:
//   hdr            <- ppcs->frm_hdr (loop_filter_params + delta_lf_params
//                     + the segmentation fields the level derivation reads)
//   lfi            <- ppcs->lf_info (lfthr + lvl)
//   miGrid/miStride<- pcs->mi_grid_base / pcs->mi_stride (NULL cells =
//                     the reference's unsetup mbmi -> TX_INVALID)
//   currDeltaLf    <- pcs->ppcs->curr_delta_lf (the sb_delta_lf array)
//   sbSizePx       <- scs->sb_size (64 / 128; the walks' sb branch and
//                     the frame walker's sb grid)
//   is16bit        <- SVT_EFFECTIVE_IS_16BIT_PIPELINE(scs->is_16bit_pipeline)
//   superresOrResize <- ppcs->frame_superres_enabled || frame_resize_enabled
//   frameWidth/Height <- ppcs->enhanced_pic->width/height (the superres
//                     last-partial-SB fixup)
struct DeblockContext {
    LfFrameParams hdr;
    LfInfoN lfi;
    const DeblockMi* const* miGrid;
    int miStride;
    const std::int32_t* currDeltaLf;
    int sbSizePx;
    bool is16bit;
    bool superresOrResize;
    int frameWidth;
    int frameHeight;
    // The encoder bit depth (scs->static_config.encoder_bit_depth), read
    // by the walk dispatches to pick the 8-bit vs hbd kernel family.
    int encoderBitDepth;
};

// Av1DeblockingParameters (deblocking_common.h:24-31): the scalar limit
// form - the reference stores const uint8_t* into the SIMD_WIDTH arrays;
// the landed DF1 kernels take scalars (the [0] lane, the DF1 convention).
struct DeblockingParams {
    std::uint32_t filterLength;
    std::uint8_t lim;
    std::uint8_t mblim;
    std::uint8_t hevThr;
};

// svt_av1_loop_filter_init (deblocking_filter.c:82-97): combine_vert_horz_lf
// = 1, the sharpness limits (the landed DF1 updateSharpness) and the
// hev_thr tier fill (lvl >> 4 into every SIMD_WIDTH lane).
void loopFilterInit(LfFrameParams& hdr, LfInfoN& lfi);

// scaled_buffer_offset (deblocking_filter.c:101-108): the live form of the
// reference - the scale-factor branches are commented out in the pinned
// tree, so the arithmetic is y * stride + x.
std::int32_t scaledBufferOffset(std::int32_t xOffset, std::int32_t yOffset, std::int32_t stride);

// setup_pred_plane (deblocking_filter.c:110-129): the dst-plane pointer
// arithmetic. The chroma odd-mi adjustment pulls mi_row/mi_col back one
// 4x4 unit when the plane is subsampled AND the mi unit is odd AND the
// block is exactly one mi unit wide/high (mi_size_high/wide[bsize] == 1).
// The x/y pixel offsets are (MI_SIZE * mi) >> subsampling and the byte
// offset is scaled by is_16bit. buf0 is not written (see DeblockPlane).
void setupPredPlane(DeblockPlane* dst, int bsize, std::uint8_t* src, int width, int height, int stride, int miRow,
                    int miCol, int subsamplingX, int subsamplingY, bool is16bit);

// svt_av1_setup_dst_planes (deblocking_filter.c:131-193): the three-plane
// dispatch. The EbPictureBufferDesc wall is threaded as three raw plane
// buffers + strides; the SequenceControlSet crop dims (the reference's
// max_input_luma_width - max_input_pad_right / max_input_luma_height -
// max_input_pad_bottom, the UNPADDED frame dims per AV1 spec 7.14.2) are
// threaded as frameWidth/frameHeight - the chroma planes get the >>1 dims.
// Only planes in [planeStart, planeEnd) are written, capped at MAX_PLANES
// (AOMMIN(plane_end, 3), deblocking_filter.c:145); each plane keeps its
// pre-set subsampling/is16bit and gets buf/width/height/stride.
void setupDstPlanes(DeblockPlane planes[kMaxPlanes], int bsize, std::uint8_t* const buffers[kMaxPlanes],
                    const int strides[kMaxPlanes], int frameWidth, int frameHeight, int miRow, int miCol,
                    int planeStart, int planeEnd);

// get_transform_size (deblocking_filter.c:196-211): the plane- and
// direction-aware tx size. Luma (plane 0 = COMPONENT_LUMA):
// tx_depth_to_tx_size[is_skip ? 0 : tx_depth][bsize] (the skip path
// forces depth 0, the block's max tx size); chroma: av1_get_max_uv_txsize
// (common_utils.h:142-149 = blocksize_to_txsize[get_plane_block_size(..)]
// through av1_get_adjusted_tx_size, common_utils.h:98-110). VERT_EDGE
// selects txsize_horz_map, HORZ_EDGE txsize_vert_map (the filter runs
// perpendicular to the edge, deblocking_filter.c:34-76). Both functions
// exported (named deviation: static INLINE/static in the reference;
// cdefSign testability precedent).
int getTransformSize(const DeblockMi& mbmi, EdgeDir edgeDir, int plane, const DeblockPlane& planePtr, bool isSkip);

// txsize_horz_map / txsize_vert_map (deblocking_filter.c:34-76): the
// direction folds, exported as data (the l7 blockSizeWide pattern) so the
// full 19-row domain is enumerable in tests.
extern const std::uint8_t kTxsizeHorzMap[19];
extern const std::uint8_t kTxsizeVertMap[19];

// av1_get_max_uv_txsize (common_utils.h:142-149).
int av1GetMaxUvTxSize(int bsize, int subsamplingX, int subsamplingY);

// svt_aom_get_filter_level_delta_lf (deblocking_common.c:46-81): the
// delta-lf level for one edge. The sb_delta_lf slot comes through
// delta_lf_id_lut when delta_lf_multi (slot 0 otherwise); the base level
// is the per-plane filter level (dir 0/1 split for luma, U/V for chroma);
// the seg ALT_LF feature add+clamp runs through seg_lvl_lf_lut; then the
// mode/ref delta block scales by 1 << (lvl_seg >> 5) and clamps again.
// The FrameHeader/SegmentationParams walls are threaded as LfFrameParams;
// pred_mode/ref_frame_0 are ordinals (definitions.h PredictionMode /
// MvReferenceFrame orders). Exported (named deviation: the reference is
// an extern function whose walls are pointers into pcs->ppcs).
std::uint8_t getFilterLevelDeltaLf(const LfFrameParams& hdr, int dirIdx, int plane, const std::int32_t* sbDeltaLf,
                                   std::uint8_t segId, int predMode, int refFrame0);

// svt_av1_loop_filter_frame_init (deblocking_common.c:86-149): fills the
// lvl table for every (plane, segment, direction) the plane gates admit
// (plane 0 breaks out when BOTH luma levels are 0; planes 1/2 are skipped
// when their own level is 0 - a plane 1 level with both luma levels 0 is
// therefore never filled). dir 0 takes filter_level[plane] (the vertical
// luma level), dir 1 the second luma level / the U/V levels; the seg
// ALT_LF feature shifts+clamps; then either the flat memset (deltas off)
// or the per-ref/per-mode delta build (scale = 1 << (lvl_seg >> 5),
// INTRA_FRAME only at [ref][0]). Refreshes the sharpness limits first
// (:99, the landed updateSharpness).
void loopFilterFrameInit(LfFrameParams& hdr, LfInfoN& lfi, int planeStart, int planeEnd);

// set_lpf_parameters (deblocking_filter.c:215-334): THE per-edge
// machinery both walks call. Returns the direction-mapped tx size for
// the advance, or kTxInvalid (an unsetup/NULL mbmi - the walks then
// force filter_length 0 and advance by the 4x4 unit). filter_length is
// reset to 0 and only set when a real edge fires:
//   - out of the plane window (width <= x || height <= y): early return
//     TX_4X4, length stays 0 (:225-230);
//   - NULL mbmi -> TX_INVALID (:249-251); NULL prev mbmi at coord != 0
//     -> TX_INVALID (:281-283);
//   - not a tx boundary (coord & (tx_size_wide/high[ts]-1)): return ts,
//     length stays 0 (:257-264);
//   - coord == 0: the plane's left/top edge - the prev lookup is skipped
//     and the length stays 0 (:279);
//   - a real edge: the level lookup (delta-lf path or the lvl table via
//     mode_lf_lut), the (curr || pv) && (!pv_skip || !curr_skipped ||
//     pu_edge) gate, then the tier: 4 if min(ts, pv_ts) <= TX_4X4, else
//     6 on chroma, else 8 iff min == TX_8X8, else 14; level = curr ?
//     curr : pv; the limits come from lfthr[level] (:303-329).
// Exported (named deviation: static in the reference; testability
// precedent). mode_step is the reference's uint64_t grid step (1 <<
// scale_horz for vertical edges, mi_stride << scale_vert for horizontal).
int setLpfParameters(DeblockingParams* params, std::uint64_t modeStep, const DeblockContext& ctx, EdgeDir edgeDir,
                     std::uint32_t x, std::uint32_t y, int plane, const DeblockPlane& planePtr);

// svt_av1_filter_block_plane_vert (deblocking_filter.c:340-466): the
// vertical-edge walk over one superblock. The y outer loop steps 1 mi
// (row_step = MI_SIZE >> MI_SIZE_LOG2); the x inner loop runs to the sb
// mi span (16 for SB64 / 32 for SB128, halved by the plane subsampling)
// and advances by eb_tx_size_wide_unit[tx_size] mi per visit. Each visit
// derives the edge parameters through setLpfParameters (mode_step = 1 <<
// scale_horz), dispatches the landed DF1/DF2 kernel by filter_length
// (4/6/8/14), and advances the pointer by that many MI_SIZE pixels.
// The superres/resize fixup (:358-375) narrows the last partial SB's
// ranges from the threaded frame dims (scaled by the subsampling for
// chroma). The is_16bit branches (highbd_lpf_vertical_*) are PARKED with
// the hbd kernel family - the DF1 8-bit discipline (assert on is16bit).
void filterBlockPlaneVert(const DeblockContext& ctx, int plane, const DeblockPlane& planePtr, std::uint32_t miRow,
                          std::uint32_t miCol);

// svt_av1_filter_block_plane_horz (deblocking_filter.c:467-603): the
// horizontal-edge twin. The x outer loop steps 1 mi (col_step); the y
// inner loop advances by eb_tx_size_high_unit[tx_size] mi per visit and
// the pointer by that many rows (advance_units * dst_stride * MI_SIZE).
// mode_step for the prev lookup is mi_stride << scale_vert (:518-520).
// Same superres fixup, same TX_INVALID reset, same 4/6/8/14 dispatch
// into the landed lpfHorizontal* kernels; highbd branches PARKED (the
// DF1 8-bit discipline, assert on is16bit).
void filterBlockPlaneHorz(const DeblockContext& ctx, int plane, const DeblockPlane& planePtr, std::uint32_t miRow,
                          std::uint32_t miCol);

// svt_aom_loop_filter_sb (deblocking_filter.c:604-683): the per-SB
// driver. Builds the three MacroblockdPlane geometry records
// (subsampling 0/1/1, is_16bit from the threaded bit depth OR the
// ctx's 16-bit-pipeline flag), then per plane in [planeStart, planeEnd):
// the level gates (plane 0 BREAKS when both luma levels are 0 - planes
// 1/2 are then never reached even with live U/V levels; planes 1/2 are
// SKIPPED on their own zero), and with combine_vert_horz_lf the order
// vert(this SB) -> horz(LEFT SB, when miCol >= max_mib) -> horz(this SB
// when last_col); otherwise vert then horz at this SB. Each walk gets a
// fresh setupDstPlanes at its own (mi_row, mi_col). The EbPicture-
// BufferDesc wall is threaded as the three plane buffers + strides +
// the bit depth; plane_type is not carried (the walks take the index).
void loopFilterSb(std::uint8_t* const buffers[kMaxPlanes], const int strides[kMaxPlanes], int bitDepth,
                  DeblockContext& ctx, int miRow, int miCol, int planeStart, int planeEnd, bool lastCol);

// svt_av1_loop_filter_frame (deblocking_filter.c:684-710): the frame
// driver. Computes the SB raster dims from the threaded aligned
// width/height ((aligned + sb_size - 1) / sb_size, :687-688), runs
// loopFilterFrameInit FIRST (:690 - the level tables refresh before any
// walk), then - gated on the threaded runDlf flag (the reference's
// cdef/restoration/is_ref/recon/stat_report disjunction :691-694 is a
// ppcs/scs wall, named deviation) - the row-major SB loop: each SB calls
// loopFilterSb at (sb_origin_y >> 2, sb_origin_x >> 2) in mi units with
// end_of_row on the last column (:697-707).
void loopFilterFrame(std::uint8_t* const buffers[kMaxPlanes], const int strides[kMaxPlanes], int bitDepth,
                     DeblockContext& ctx, int alignedWidth, int alignedHeight, int planeStart, int planeEnd,
                     bool runDlf);

// The EbReferenceObject wall for the q-pick: one row per SINGLE-ref
// reference carrying that ref's saved filter levels (the av1_set_ref_frame
// / compound-pair skip in the reference's ref loop is the caller's; the
// min reduction over the surviving rows stays inside the port).
struct RefFilterLevels {
    std::int32_t fl0;
    std::int32_t fl1;
    std::int32_t flu;
    std::int32_t flv;
};

// The threaded walls of svt_av1_pick_filter_level_by_q
// (deblocking_filter.c:1016-1104).
struct PickQParams {
    bool isKeyFrame;   // frm_hdr->frame_type == KEY_FRAME
    int inputResolution; // pcs->ppcs->input_resolution (0..6, the
                       // inter_frame_multiplier index)
    bool boosted;      // frame_is_boosted(pcs->ppcs) - the pcs wall
    bool doY;          // the me_based_dlf_skip EFFECT (:954-1010 parked;
                       // its do_y/do_uv outputs threaded)
    bool doUv;
    int bitDepth;      // 8/10/12 (EbSvtAv1Formats.h:100-102)
    const RefFilterLevels* refs; // the single-ref rows (min-reduced here)
    int refCount;      // pcs->ppcs->tot_ref_frame_types (surviving rows)
};

// svt_av1_pick_filter_level_by_q (deblocking_filter.c:1016-1104): the
// q-driven level pick. q = the frame's AC QTX dequant (the
// svt_aom_ac_quant_qtx(qindex, 0, bit_depth) call is threaded as the
// `q` parameter - the 8-bit table is l3_transforms::acQuantQtx's landed
// surface, the 10/12-bit tables are NOT ported; named deviation). The
// linear-fit guesses: 8-bit key RPOT(q*17563 - 421574, 18) / inter
// RPOT(q*inter_frame_multiplier[in_res] + 650707, 18) (the multiplier
// table {6017x3, 12034x4}, :28); 10-bit RPOT(q*20723 + 4060632, 20);
// 12-bit RPOT(q*20723 + 16242526, 22); a high-depth keyframe loses 4
// (:1072-1075). chroma = guess / 2 (C truncation). doY/doUv zero the
// guesses. The four outputs: (min_ref_level || boosted) ?
// clamp(guess, 0, MAX_LOOP_FILTER) : 0 - with NO refs the min stays at
// its MAX_LOOP_FILTER init, so the guess path survives (the reference's
// own zero-ref quirk). Unknown bit depths return without writing
// (the reference's assert+return, :1066-1070).
void pickFilterLevelByQ(const PickQParams& params, std::int32_t q, std::int32_t filterLevel[4]);

// ---------------------------------------------------------------------------
// A8W1 walls rung (G5-G9): the deblocking_filter.c search walls :711-:1286.
// ---------------------------------------------------------------------------

// LfSearchPic - the threaded EbPictureBufferDesc surface the walls read
// and copy. Field types follow the test's static_casts; the enums
// (bit_depth / color_format) thread as ints (named deviation).
struct LfSearchPic {
    std::uint16_t width;
    std::uint16_t height;
    std::uint16_t maxWidth;
    std::uint16_t maxHeight;
    std::uint32_t border;
    int bitDepth; // > 8 = the 16-bit copy path (deblocking_filter.c:712)
    int colorFormat;
    std::uint32_t lumaSize;
    std::uint32_t chromaSize;
    bool packedFlag;
    std::uint8_t* buffer[kMaxPlanes];
    std::uint16_t stride[kMaxPlanes];
    std::uint8_t strideBitInc[kMaxPlanes];
};

// DlfRefStat - the threaded EbReferenceObject row the ref loops consume:
// dlf_dist_dev + tmp_layer_idx (deblocking_filter.c:991, :1238). The
// av1_set_ref_frame / compound-pair skip is the caller's (the rows are
// the surviving SINGLE-ref entries).
struct DlfRefStat {
    std::int32_t dlfDistDev;
    int tmpLayerIdx;
};

// The threaded EbSpatialFullDistType kernel signature
// (deblocking_filter.c:758-759; the call passes the plane buffers,
// offset 0, the strides, the aligned dims).
using DlfSseFn = std::uint64_t (*)(const std::uint8_t*, std::uint32_t, std::uint32_t, const std::uint8_t*,
    std::int32_t, std::uint32_t, std::uint32_t, std::uint32_t);

// LpfPickMethod (deblocking_filter.h:24-33) - the REFERENCE enum order
// (FROM_FULL_IMAGE 0 / FROM_SUBIMAGE 1 / FROM_Q 2 / MINIMAL_LPF 3); the
// pick driver's method >= FROM_Q fold must NOT be reordered.
enum LpfPickMethod {
    LpfPickFromFullImage = 0,
    LpfPickFromSubimage = 1,
    LpfPickFromQ = 2,
    LpfPickMinimalLpf = 3,
};

// DlfWalls - the threaded PictureControlSet / SequenceControlSet /
// FrameHeader / EbReferenceObject walls the five wall functions consume,
// one named field per consumed reference field. The DLF-search branch
// (:1146-:1283) is compiled-in here (CONFIG_ENABLE_DLF_SEARCH is 0 in
// the pinned EbConfigMacros.h:116 - the span is carried and testable
// through LpfPickFromFullImage, named deviation). DlfSearchState is
// defined below; the walls' searchState field needs only the pointer.
struct DlfSearchState;
struct DlfWalls {
    int tune;                    // static_config.tune (VQ=0, PSNR=1, IQ=3, MS_SSIM=4)
    int sbSizePx;                // scs->super_block_size
    int alignedWidth;            // ppcs->aligned_width
    int alignedHeight;           // ppcs->aligned_height
    int inputResolution;         // ppcs->input_resolution
    DlfSseFn dist8;              // svt_spatial_full_distortion_kernel
    DlfSseFn dist16;             // svt_full_distortion_kernel16_bits
    bool is16bitPipeline;        // SVT_EFFECTIVE_IS_16BIT_PIPELINE
    int ssX;                     // scs->subsampling_x
    int ssY;                     // scs->subsampling_y
    LfSearchPic* inputPic8;      // ppcs->enhanced_pic
    LfSearchPic* inputPic16;     // pcs->input_frame16bit
    LfSearchPic* reconPic8;      // svt_aom_get_recon_pic(pcs, &, false) - PARKED selector
    LfSearchPic* reconPic16;     // svt_aom_get_recon_pic(pcs, &, true)
    // ERRATA #7/#9 (A8W1e): the temp recon picture (the reference EB_NEWs
    // it at :1163-:1171 - PARKED, threaded instead) and the pcs
    // zero_filt_sse/best_filt_sse pair (deblocking_filter.c:938-:946
    // write; dlf_process.c:90-:122 reads - the future dlf_process rung
    // owns persistence). Null searchState skips the plane-0 writes.
    LfSearchPic* tempPic8;
    LfSearchPic* tempPic16;
    DlfSearchState* searchState;
    const std::uint32_t* rcMeDistortion; // ppcs->rc_me_distortion
    int b64TotalCount;           // pcs->b64_total_count
    bool intraSlice;             // pcs->slice_type == I_SLICE
    int zeroFilterStrengthLvl;   // dlf_ctrls.zero_filter_strength_lvl
    int prevDlfDistTh;           // dlf_ctrls.prev_dlf_dist_th (:1205)
    int hierarchicalLevels;      // ppcs->hierarchical_levels
    bool boosted;                // frame_is_boosted(ppcs)
    bool leaf;                   // frame_is_leaf(ppcs)
    int temporalLayerIndex;      // pcs->temporal_layer_index
    const DlfRefStat* dlfRefs;   // the surviving single-ref rows
    int dlfRefCount;             // ppcs->tot_ref_frame_types
    int earlyExitConvergence;    // dlf_ctrls.early_exit_convergence
    bool txModeOnly4x4;          // frm_hdr->tx_mode == ONLY_4X4
    LpfPickMethod pickMethod;    // dlf_ctrls.pick_method
    int sharpness;               // static_config.sharpness
    bool isKeyFrame;             // frm_hdr->frame_type == KEY_FRAME
    int baseQIdx;                // frm_hdr->quantization_params.base_q_idx
    bool dlfAvg;                 // dlf_ctrls.dlf_avg
    const RefFilterLevels* refLevels; // the refs' saved filter levels
    int refLevelCount;           // the surviving single-ref rows
    bool useRefAvgY;             // dlf_ctrls.use_ref_avg_y
    bool useRefAvgUv;            // dlf_ctrls.use_ref_avg_uv
};

// DlfSearchState - the threaded pcs->zero_filt_sse / best_filt_sse pair
// the plane-0 search writes (deblocking_filter.c:938-946).
struct DlfSearchState {
    std::int64_t zeroFiltSse;
    std::int64_t bestFiltSse;
};

// disable_dlf_th (deblocking_filter.c:27-32): [zero_filter_strength_lvl]
// [input_resolution], exported as data (the l7 blockSizeWide pattern).
extern const std::uint32_t kDisableDlfTh[4][7];

// svt_copy_buffer (deblocking_filter.c:711-740): the ten header fields
// dst<-src, the stride/stride_bit_inc cell, then the row copies -
// copy_width = ALIGN_POWER_OF_TWO(width, 3) << is_16bit (is_16bit =
// bit_depth > 8), copy_height = ALIGN_POWER_OF_TWO(height, 3), both
// >>1 for a chroma plane, copy_stride = stride[plane] << is_16bit.
void copyBuffer(const LfSearchPic& src, LfSearchPic& dst, int plane);

// picture_sse_calculations (deblocking_filter.c:742-770): is_16bit
// selects the kernel AND the input pic; the aligned dims >> ss for
// chroma; offsets 0; dispatches the threaded SSE kernel.
std::uint64_t pictureSseCalculations(const DlfWalls& walls, const LfSearchPic& reconPic, int plane);

// me_based_dlf_skip (deblocking_filter.c:954-1010): the ME-based skip
// heuristic; writes *doY/*doUv. The ref-frame wall reduces to the
// DlfRefStat rows (dlf_dist_dev >= 0 && tmp_layer_idx <= temporal
// layer); the prev_dlf_dist_th comes through the parameter (the
// reference's uint16_t form).
void meBasedDlfSkip(const DlfWalls& walls, std::uint16_t prevDlfDistTh, bool* doY, bool* doUv);

// try_filter_frame (deblocking_filter.c:772-822): sets the trial level
// into the frame header (dir 0/1 keep the other luma level; chroma
// writes only the plane slot), runs the landed loopFilterFrame on
// [plane, plane+1), measures the SSE, restores the recon from the temp
// copy when any level is nonzero. partial_frame is carried for
// signature parity (the reference (void)s it, :781).
std::int64_t tryFilterFrame(const DlfWalls& walls, DeblockContext& ctx, const LfSearchPic& tempPic,
    std::int32_t filtLevel, std::int32_t partialFrame, int plane, int dir);

// search_filter_level (deblocking_filter.c:828-952): the seeded
// hill-climb over MAX_LOOP_FILTER+1 with the ss_err cache, the bias
// toward lower levels (halved when tx_mode != ONLY_4X4), the
// convergence exit, and the plane-0 pcs state side effects (threaded as
// DlfSearchState*, written only when plane == 0). ERRATA (A8W1e): the
// staged decl took const LfSearchPic& tempPic, but :873 WRITES the temp
// (svt_copy_buffer(recon, temp, plane) - recon is src, temp is dst);
// the decl is corrected to LfSearchPic& (the staged tests pass non-const
// lvalues, which bind identically).
std::int32_t searchFilterLevel(const DlfWalls& walls, DeblockContext& ctx, LfSearchPic& tempPic,
    DlfSearchState* state, std::int32_t partialFrame, const std::int32_t lastFrameFilterLevel[4],
    double* bestCostRet, int plane, int dir);

// dlf_search padding (deblocking_filter.c:1148-1151): sb_size + 32,
// + sb_size when superres or resize is on. Factored out of the pick
// driver for testability (named deviation: inline in the reference).
int dlfSearchPadding(int sbSizePx, bool superresOrResize);

// svt_av1_pick_filter_level (deblocking_filter.c:1110-1286): the pick
// driver. Derives the sharpness level (CLIP3(0,7); the VQ-keyframe +2
// capped at 7; the IQ/MS_SSIM base_q_idx tiers), then dispatches on the
// pick method: MINIMAL_LPF zeroes the luma levels, >= FROM_Q delegates
// to the landed pickFilterLevelByQ (the threaded dequant `q` is the
// svt_aom_ac_quant_qtx call site), else the DLF search block
// (:1146-:1283). srcBuffer is unused in the reference (:1113) - not
// carried.
void pickFilterLevel(const DlfWalls& walls, DeblockContext& ctx, std::int32_t q);

} // namespace deblock
} // namespace l10_filters
