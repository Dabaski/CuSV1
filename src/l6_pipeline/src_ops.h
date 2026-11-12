// l6_pipeline/src_ops.h - the SRC1 source-ops surface (A6).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/src_ops_process.c
// (2,014 nb). BRIEF CORRECTION (measured 2026-11-12, grep-verified): the
// brief's "plane copy/pad helpers, yuv interleave/deinterleave paths,
// 8/16-bit copy kernels" do NOT exist in this file - the only copy/pad
// content is one CALL of svt_aom_generate_padding (:1412-:1417, defined in
// pic_manager_process.c). The file is the TPL dispenser/synthesizer + the
// SSIM-rdmult driver. Ported here: the struct-free kernels and the
// TplStats-grid carriers (the RE8A DeblockContext carrier precedent).
// The EbPictureBufferDesc / PictureParentControlSet / EncodeContext /
// mutex-fifo walls are PARKED NAMED (the span map lives in the test file
// header and the commit message).
#pragma once

#include <cstdint>

#include "rdlite.h"

namespace pipeline {
namespace srcops {

// ---------------------------------------------------------------------------
// Carries: small verbatim table/helper ports the ported spans consume.
// ---------------------------------------------------------------------------

// mi_size_wide_log2 / mi_size_high_log2 (third_party/SVT-AV1/Source/Lib/
// Codec/common_utils.c:211-212, verbatim values). Consumed by
// getOverlapArea / tplModelUpdateB. The plain (non-log2) twins live in
// l16_rdo (miSizeWide/miSizeHigh, rate_est.cpp:1542); the log2 tables are
// not carried there.
extern const std::uint8_t miSizeWideLog2[l16_rdo::BLOCK_SIZES_ALL];
extern const std::uint8_t miSizeHighLog2[l16_rdo::BLOCK_SIZES_ALL];

// svt_log2f = get_msb (definitions.h:591-592, verbatim): the highest set
// bit. Carried locally - l16_rdo exposes getUnsignedBits, not get_msb;
// the l10 cdef.cpp / l13 palette.cpp get_msb duplication precedent.
std::int32_t svtLog2f(std::int32_t x);

// ---------------------------------------------------------------------------
// F1: the struct-free kernels.
// ---------------------------------------------------------------------------

// get_neighbor_samples_dc (src_ops_process.c:364-376, verbatim): the DC
// source-window load - the top-left corner into above0Row[-1] and
// left0Col[-1], the bsize-wide top row (memcpy) and the bsize-deep left
// column. The caller owns the [-1] slots (the reference's callers pass
// above0_row/left0_col pointing one element into their arrays, :622-:623).
void getNeighborSamplesDc(const std::uint8_t* src, std::uint32_t srcStride,
                          std::uint8_t* above0Row, std::uint8_t* left0Col,
                          std::uint8_t bsize);

// round_floor (src_ops_process.c:1453-1462, verbatim): floor division by
// bsizePix for possibly-negative refPos (the negative branch uses the
// -(1 + (-ref_pos - 1) / bsize_pix) form).
std::int32_t roundFloor(std::int32_t refPos, std::int32_t bsizePix);

// get_overlap_area (src_ops_process.c:1423-1451, verbatim): the overlap of
// one of the 4 sub-quadrants (block 0..3, row-major: (block >> 1) is the
// row half, (block & 0x01) the col half) of the ref-grid block at
// (gridPosRow, gridPosCol) against the block at (refPosRow, refPosCol).
// bw/bh derive from the mi_size_*_log2 tables. The default case is the
// reference's assert(0) and a 0 return.
std::int32_t getOverlapArea(std::int32_t gridPosRow, std::int32_t gridPosCol,
                            std::int32_t refPosRow, std::int32_t refPosCol,
                            std::int32_t block, l16_rdo::BlockSize bsize);

// delta_rate_cost (src_ops_process.c:1464-1490, verbatim): the TPL
// dependency-rate fold (the compute_rate path of tpl_model_update_b,
// :1522-:1524). TPL_DEP_COST_SCALE_LOG2 = 4 (definitions.h:68) and
// AV1_PROB_COST_SHIFT = 9 (rate_est.h:33) make the shift 10.
std::int64_t deltaRateCost(std::int64_t deltaRate, std::int64_t recrfDist,
                           std::int64_t srcrfDist, std::int32_t pixNum);

// svt_aom_get_perpixel_variance (src_ops_process.c:2151-2156): the
// SSIM-rdmult input variance (:2193, BLOCK_8X8). DEVIATION (named): the
// svt_aom_mefn_ptr[block_size].vf dispatch is replaced by the l9
// offs-folded instances (analysis::variance8x8 / variance16x16, the AN8
// per-size-wrapper precedent at pic_analysis_process.c:944-:952);
// AV1_VAR_OFFS (:2143-:2149, 128 x MAX_SB_SIZE) is folded inside those
// instances - the fold the AN8 wrapper pair documents. The remaining
// mefn table sizes are parked with the dispatch wall (assert + 0).
std::uint32_t perpixelVariance(const std::uint8_t* buf, std::uint32_t stride,
                               l16_rdo::BlockSize blockSize);

// ---------------------------------------------------------------------------
// F2: the TPL rate surface.
// ---------------------------------------------------------------------------

// rate_estimator (src_ops_process.c:263-276, verbatim): the TPL qcoeff
// rate estimate - (eob + 1) plus, per scanned coefficient,
// svt_log2f(level + 1) + (level > 0), shifted by AV1_PROB_COST_SHIFT.
// txSize is the 19-shape TxSize order (getScanOrder's index space);
// txType is DCT_DCT = 0 at both live call sites (:948/:1144 - the DCT_DCT
// constant of get_quantize_error, :239).
std::int32_t rateEstimator(const std::int32_t* qcoeff, std::int32_t eob,
                           std::int32_t txSize);

// ---------------------------------------------------------------------------
// F3: the TPL synthesizer carriers (the TplStats-grid walk).
// ---------------------------------------------------------------------------

// TplStatsCell: the mini-carry of TplStats (coding_unit.h, the 8 fields
// the synthesizer reads/writes) - the AN17 struct-carry precedent. Mv
// travels as its packed as_int form (x | y << 16, little-endian; the RE3
// IntraFastCandidate precedent).
struct TplStatsCell {
    std::int64_t srcrfDist;
    std::int64_t recrfDist;
    std::int64_t srcrfRate;
    std::int64_t recrfRate;
    std::int64_t mcDepRate;
    std::int64_t mcDepDist;
    std::uint32_t mv;         // Mv::as_int (mv.h:42-49)
    std::uint64_t refFramePoc;
};

// tpl_model_update_b (src_ops_process.c:1496-1549): propagates one
// current-frame TplStats cell into the reference frame's 4 overlapping
// grid quadrants (get_overlap_area + round_floor + delta_rate_cost
// compose). The walls thread per the campaign pattern: ref_pcs_ptr's
// pa_me_data->tpl_stats (TplStats**, an array of per-cell pointers)
// becomes one flat TplStatsCell array addressed by the same linear index
// the reference computes (NAMED DEVIATION: the grid carrier), and
// ref_cm->mi_rows/mi_cols + ref aligned_width + the tpl_ctrls flags
// become scalars.
struct TplModelUpdateBArgs {
    TplStatsCell* refGrid;        // pa_me_data->tpl_stats (flattened)
    const TplStatsCell* cur;      // tpl_stats_ptr
    std::int32_t miRow;           // the current block's mi origin
    std::int32_t miCol;
    l16_rdo::BlockSize bsize;
    std::int32_t synthBlkSize;    // pcs->tpl_ctrls.synth_blk_size (8/16/32)
    std::int32_t computeRate;     // pcs->tpl_ctrls.compute_rate
    std::int32_t refAlignedWidth; // ref_pcs_ptr->aligned_width
    std::int32_t refMiRows;       // ref_cm->mi_rows
    std::int32_t refMiCols;       // ref_cm->mi_cols
};
void tplModelUpdateB(const TplModelUpdateBArgs& a);

// One frame's TPL data in the sliding window (the pcs_array[i] carrier):
// the flattened grid + the fields tpl_model_update / the synthesizer /
// tpl_model_update_b read off the PCS and its Av1Common.
struct TplSwFrame {
    TplStatsCell* grid;           // pa_me_data->tpl_stats (flattened)
    std::uint64_t pictureNumber;  // pcs->picture_number
    std::int32_t alignedWidth;    // pcs->aligned_width
    std::int32_t miRows;          // pcs->av1_cm->mi_rows (modelUpdateB's bounds)
    std::int32_t miCols;          // pcs->av1_cm->mi_cols
};

// tpl_model_update (src_ops_process.c:1555-1582): walks the current
// frame's stepped grid cells and propagates each into the sliding-window
// frame whose picture_number matches the cell's ref_frame_poc. The `i`
// scan position is NOT reset between cells - the verbatim :1567-:1574
// structure (a failed lookup past the window blocks every later cell
// from matching a lower index); pinned by a fixture.
struct TplModelUpdateArgs {
    TplSwFrame* frames;        // pcs_array[0 .. framesInSw)
    std::int32_t frameIdx;
    std::uint8_t framesInSw;
    std::int32_t miRow;
    std::int32_t miCol;
    std::int32_t synthBlkSize;  // pcs_array[frame_idx]->tpl_ctrls.synth_blk_size
    std::int32_t computeRate;   // pcs_array[frame_idx]->tpl_ctrls.compute_rate
    l16_rdo::BlockSize bsize;   // the caller's per-SB bsize (synth-derived)
};
void tplModelUpdate(const TplModelUpdateArgs& a);

// tpl_mc_flow_synthesizer (src_ops_process.c:1589-1602): the per-frame
// grid walk. bsize = synth 32 ? BLOCK_32X32 : BLOCK_16X16 (:1592 - note
// synth 8 folds to BLOCK_16X16 here); the step is that bsize's mi size.
// mi_rows/mi_cols are pcs_array[frame_idx]->av1_cm's (threaded).
struct TplMcFlowSynthesizerArgs {
    TplSwFrame* frames;
    std::int32_t frameIdx;
    std::uint8_t framesInSw;
    std::int32_t synthBlkSize;
    std::int32_t computeRate;  // pcs_array[frame_idx]->tpl_ctrls.compute_rate
    std::int32_t miRows;       // pcs_array[frame_idx]->av1_cm->mi_rows
    std::int32_t miCols;       // pcs_array[frame_idx]->av1_cm->mi_cols
};
void tplMcFlowSynthesizer(const TplMcFlowSynthesizerArgs& a);

// ---------------------------------------------------------------------------
// F4: the RDO-driver carriers.
// ---------------------------------------------------------------------------

// coded_to_superres_mi (resize.h:71-73, verbatim).
std::int32_t codedToSuperresMi(std::int32_t miCol, std::int32_t denom);

// generate_lambda_scaling_factor (src_ops_process.c:188-234): the TPL
// rdmult-scaling-factor fill (called from svt_aom_generate_r0beta:1653 and
// the RDO wiring). The PCS/Av1Common walls thread per the campaign
// pattern: cm->mi_rows, enhanced_unscaled_pic->width,
// tpl_ctrls.synth_blk_size, pa_me_data's base_rdmult + tpl_stats grid +
// tpl_rdmult_scaling_factors, and pcs->r0.
struct GenerateLambdaScalingFactorArgs {
    const TplStatsCell* grid;   // pa_me_data->tpl_stats (flattened)
    double* factorsOut;         // pa_me_data->tpl_rdmult_scaling_factors
    std::int32_t miRows;        // cm->mi_rows
    std::int32_t unscaledWidth; // pcs->enhanced_unscaled_pic->width
    std::int32_t synthBlkSize;  // pcs->tpl_ctrls.synth_blk_size
    std::int32_t baseRdmult;    // pcs->pa_me_data->base_rdmult
    double r0;                  // pcs->r0
    std::int64_t mcDepCostBase; // the caller's mc_dep_cost_base
};
void generateLambdaScalingFactor(const GenerateLambdaScalingFactorArgs& a);

// The SbGeom org carry (pcs.h:1022-1026): the two fields the r0beta SB
// loop reads.
struct SbGeomOrg {
    std::uint16_t orgX;
    std::uint16_t orgY;
};

// svt_aom_generate_r0beta (src_ops_process.c:1604-1697): the r0/beta
// fill. The PCS/SCS walls thread; r0 + tpl_is_valid are in/out (r0 is
// OVERWRITTEN when cost_base != 0, :1636-:1644, including the outlier
// clamp to 1.0, :1639-:1641); the lambda fill runs at the end (the
// reference's :1653 call, composed from the landed carrier). The SB grid
// dims are DERIVED inside from aligned_width/aligned_height + sb_size
// (the reference's :1657-:1658 computation).
struct GenerateR0betaArgs {
    const TplStatsCell* grid;      // pa_me_data->tpl_stats (flattened)
    double* lambdaFactorsOut;      // pa_me_data->tpl_rdmult_scaling_factors
    double* tplBetaOut;            // pa_me_data->tpl_beta
    const SbGeomOrg* sbGeom;       // pcs->sb_geom (the org fields only)
    std::int32_t miRowsCur;        // cm->mi_rows (the first loop's bound)
    std::int32_t unscaledWidth;    // pcs->enhanced_unscaled_pic->width
    std::int32_t unscaledHeight;   // pcs->enhanced_unscaled_pic->height
    std::int32_t alignedWidth;     // pcs->aligned_width
    std::int32_t alignedHeight;    // pcs->aligned_height
    std::int32_t sbSize;           // scs->sb_size
    std::int32_t superresDenom;    // pcs->superres_denom
    std::int32_t synthBlkSize;     // pcs->tpl_ctrls.synth_blk_size
    std::int32_t baseRdmult;       // pcs->pa_me_data->base_rdmult
    double* r0;                    // pcs->r0 (in/out)
    std::int32_t* tplIsValid;      // pcs->tpl_is_valid (out)
};
void generateR0beta(const GenerateR0betaArgs& a);

// aom_av1_set_mb_ssim_rdmult_scaling (src_ops_process.c:2158-2236): the
// TUNE_SSIM/IQ/MS_SSIM rdmult scaling (the kernel-iter call at :2278-:2280).
// The walls thread: enhanced_pic's y_buffer + y_stride, cm->mi_cols/
// mi_rows, and the out array. The do_print branches (:2175-:2177,
// :2211-:2233) are dead in the reference (do_print = false const) -
// omitted (named deviation, the dead-branch DCE).
struct SetMbSsimRdmultScalingArgs {
    const std::uint8_t* yBuffer; // pcs->enhanced_pic->y_buffer
    std::int32_t yStride;        // pcs->enhanced_pic->y_stride
    std::int32_t miCols;         // cm->mi_cols
    std::int32_t miRows;         // cm->mi_rows
    double* factorsOut;          // pa_me_data->ssim_rdmult_scaling_factors
};
void setMbSsimRdmultScaling(const SetMbSsimRdmultScalingArgs& a);

}  // namespace srcops
}  // namespace pipeline
