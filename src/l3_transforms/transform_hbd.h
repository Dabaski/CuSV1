#pragma once

// TXH1: the A1 highbd transform twin surfaces. The reference's inverse
// transform path is ONE bd-parameterized core (inv_txfm2d_add_c,
// inv_transforms.c:2496-2579, uint16 buffers) shared by the 8-bit and hbd
// callers; the landed 8-bit port pinned bd=8 inside its own copy. This file
// carries the bd-parameterized surface: the stage-range generator, the
// bd-clamp helper family, the 2D core over uint16 buffers, the per-shape
// facades, the lossless iwht4x4 pair, the dispatcher/recon entry chain, and
// the fwd dispatch chain (wht_fwd_txfm + svt_av1_highbd_fwd_txfm + the
// default-shape per-size statics, which are behaviorally bd-free - the only
// fwd stage_range consumers are comments and NDEBUG-dead asserts).
// Deviations (named): the CONVERT_TO_SHORTPTR/CONVERT_TO_BYTEPTR pointer
// tagging is dropped (uint16 buffers passed directly); the recon entries
// return void (the reference's EbErrorType is always EB_ErrorNone);
// TxfmParamHb drops tx_set_type (its only consumer is a dead assert).

#include <cstdint>

#include "transform.h"

namespace transforms {

// svt_av1_gen_inv_stage_range (inv_transforms.c:44-86): fills stageRangeRow /
// stageRangeCol with the bd-dependent opt ranges (bd 8: 16/16, bd 10: 18/16,
// bd 12: 20/18); the real_range computations feed only NDEBUG-dead asserts
// and are kept as voided locals
void genInvStageRangeHb(std::int8_t* stageRangeCol, std::int8_t* stageRangeRow,
                        const Txfm2dFlipCfg& cfg, TxSize size, int bd);

// definitions.h:691-693 clamp64
std::int64_t clamp64Hb(std::int64_t value, std::int64_t low, std::int64_t high);

// definitions.h:704-713 clip_pixel_highbd
std::uint16_t clipPixelHighbd(int val, int bd);

// inv_transforms.c:2427-2440 check_range
std::int64_t checkRangeHb(std::int64_t input, int bd);

// inv_transforms.c:2442-2447 HIGHBD_WRAPLOW + highbd_clip_pixel_add
std::uint16_t highbdClipPixelAdd(std::uint16_t dest, std::int64_t trans, int bd);

// TXH1: TxfmParam (definitions.h:1087-1099); tx_set_type dropped (its only
// consumer is the dead av1_ext_tx_used assert)
struct TxfmParamHb {
    TxType txType;
    TxSize txSize;
    std::int32_t lossless;
    std::int32_t bd;
    std::int32_t isHbd;
    std::int32_t eob;
};

// inv_txfm2d_add_c (inv_transforms.c:2496-2579) with the port's full 16-type
// scope, bd-parameterized, over uint16 read/write buffers (the r/w split is
// the reference's own signature). txfmBuf is the caller-provided
// w*h + 2*max(w,h) scratch.
void invTxfm2dAddCoreHb(const std::int32_t* input, std::uint16_t* dstR, std::uint32_t strideR,
                        std::uint16_t* dstW, std::uint32_t strideW, const Txfm2dFlipCfg& cfg,
                        std::int32_t* txfmBuf, int bd);

// The per-shape facades (svt_av1_inv_txfm2d_add_*_c, inv_transforms.c:2591-
// 2764) in the reference's own read/write-split signature. The 64x64 facade
// mirrors the LANDED 4096 form (the FS5d named follow-up; the l6/GPU callers
// expand the compacted dqcoeff themselves), NOT the reference's compacting
// remap; 32x64/64x32/16x64/64x16 keep the landed compacting remaps.
void invTxfm2dAddHb4x4(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                       std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb8x8(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                       std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb16x16(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb32x32(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb64x64(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb4x8(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                       std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb8x4(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                       std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb8x16(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                        std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb16x8(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                        std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb16x32(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb32x16(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb4x16(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                        std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb16x4(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                        std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb8x32(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                        std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb32x8(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                        std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb64x32(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb32x64(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb16x64(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);
void invTxfm2dAddHb64x16(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd);

// The dispatcher over the 19 facades (highbd_inv_txfm_add,
// inv_transforms.c:3167-3236); the support predicate is the landed
// invTxfmSupported (the wrapper gates: DCT_DCT/IDTX at TX_32X32, DCT-only at
// TX_64X64)
void invTxfm2dAddHb(const std::int32_t* coeffs, std::uint16_t* dst, std::uint32_t stride,
                    TxSize size, TxType type, int bd);

// svt_av1_highbd_iwht4x4_16_add_c (inv_transforms.c:2783-2842) / _1_add_c
// (:2844-2873); the range_check_value calls are dead (DO_RANGE_CHECK_CLAMP
// undefined) and are dropped
void iwht4x4_16AddHb(const std::int32_t* input, std::uint16_t* dstR, std::int32_t strideR,
                     std::uint16_t* dstW, std::int32_t strideW, int bd);
void iwht4x4_1AddHb(const std::int32_t* input, std::uint16_t* dstR, std::int32_t strideR,
                    std::uint16_t* dstW, std::int32_t strideW, int bd);

// svt_av1_highbd_inv_txfm_add_4x4 (inv_transforms.c:2884-2899): lossless ->
// iwht4x4 (eob > 1: 16Add, else 1Add), else the regular 4x4 facade
void highbdInvTxfmAdd4x4Hb(const std::int32_t* input, std::uint16_t* dstR, std::int32_t strideR,
                           std::uint16_t* dstW, std::int32_t strideW, const TxfmParamHb& txfmParam);

// highbd_inv_txfm_add (inv_transforms.c:3167-3236)
void highbdInvTxfmAddHb(const std::int32_t* input, std::uint16_t* dstR, std::int32_t strideR,
                        std::uint16_t* dstW, std::int32_t strideW, const TxfmParamHb& txfmParam);

// svt_aom_inv_transform_recon (inv_transforms.c:3238-3265) / recon8bit
// (:3139-3165); void returns - the reference EbErrorType is always
// EB_ErrorNone (named deviation)
void invTransformReconHb(const std::int32_t* coeffBuffer, std::uint16_t* reconR, std::uint32_t reconStrideR,
                         std::uint16_t* reconW, std::uint32_t reconStrideW, TxSize txSize,
                         std::uint32_t bitDepth, TxType transformType, std::uint32_t eob,
                         bool lossless);
void invTransformRecon8bitHb(const std::int32_t* coeffBuffer, std::uint16_t* reconR,
                             std::uint32_t reconStrideR, std::uint16_t* reconW,
                             std::uint32_t reconStrideW, TxSize txSize, TxType transformType,
                             std::uint32_t eob, bool lossless);

// svt_av1_inv_txfm_add_c (inv_transforms.c:3267-3287): the uint8 staging
// entry (uint16 tmp[MAX_TX_SQUARE] at stride MAX_TX_SIZE)
void invTxfmAddStagingHb(const std::int32_t* dqcoeff, std::uint8_t* dstR, std::int32_t strideR,
                         std::uint8_t* dstW, std::int32_t strideW, const TxfmParamHb& txfmParam);

// svt_av1_gen_fwd_stage_range (transforms.c:148-160); behavioral consumers
// do not exist on the fwd side (the stage_range reads are comments and
// NDEBUG-dead asserts) - ported for table fidelity
void genFwdStageRangeHb(std::int8_t* stageRangeCol, std::int8_t* stageRangeRow,
                        const Txfm2dFlipCfg& cfg, int bd);

// TxCoeffShape (definitions.h:2045-2050)
enum class TxCoeffShapeHb { DEFAULT_SHAPE = 0, N2_SHAPE = 1, N4_SHAPE = 2, ONLY_DC_SHAPE = 3 };

// svt_av1_wht_fwd_txfm (transforms.c:4037-4057); the N2/N4 branches are
// PARKED with the N2/N4 PF-shape family (their callees are not landed in any
// shape) and assert
void whtFwdTxfmHb(std::int16_t* srcDiff, int bw, std::int32_t* coeff, TxSize txSize,
                  TxCoeffShapeHb pfShape, int bitDepth, int isHbd);

// svt_av1_highbd_fwd_txfm (transforms.c:3970-4035) + the default-shape
// per-size statics (:3726-3834); TX_4X4 is the reference's no-op (the hack
// comment); the behavior is bd-free
void highbdFwdTxfmHb(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride,
                     const TxfmParamHb& txfmParam);

}  // namespace transforms
