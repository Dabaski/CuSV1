// TXH1: the A1 highbd transform twin surfaces (implementation).
// Provenance: inv_transforms.c/.h + transforms.c + definitions.h in the pinned
// third_party/SVT-AV1 snapshot (see transform_hbd.h for the span map and the
// named deviations).

#include "transform_hbd.h"

#include <cassert>

namespace transforms {

namespace {

constexpr int kMaxTxfmStageNum = 12;  // MAX_TXFM_STAGE_NUM (inv_transforms.h:25)
constexpr int kMaxTxSquare = 64 * 64;  // MAX_TX_SQUARE (definitions.h:414)
constexpr int kMaxTxSize = 64;         // MAX_TX_SIZE (definitions.h:411)

// inv_start_range (inv_transforms.h:217-237), "sum of fwd_shift_##"
const std::int8_t kInvStartRange[19] = {5, 6, 7, 7, 7, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7};

}  // namespace

// ---------------------------------------------------------------------------
// G1: the bd-clamp helper family + the stage-range generator
// ---------------------------------------------------------------------------

std::int64_t clamp64Hb(std::int64_t value, std::int64_t low, std::int64_t high) {
    return value < low ? low : (value > high ? high : value);
}

std::uint16_t clipPixelHighbd(int val, int bd) {
    switch (bd) {
    case 8:
    default:
        return static_cast<std::uint16_t>(val > 255 ? 255 : (val < 0 ? 0 : val));
    case 10:
        return static_cast<std::uint16_t>(val > 1023 ? 1023 : (val < 0 ? 0 : val));
    case 12:
        return static_cast<std::uint16_t>(val > 4095 ? 4095 : (val < 0 ? 0 : val));
    }
}

std::int64_t checkRangeHb(std::int64_t input, int bd) {
    // AV1 TX case
    // - 8 bit: signed 16 bit integer
    // - 10 bit: signed 18 bit integer
    // - 12 bit: signed 20 bit integer
    // - max quantization error = 1828 << (bd - 8)
    const std::int32_t intMax = (1 << (7 + bd)) - 1 + (914 << (bd - 7));
    const std::int32_t intMin = -intMax - 1;
    return clamp64Hb(input, intMin, intMax);
}

std::uint16_t highbdClipPixelAdd(std::uint16_t dest, std::int64_t trans, int bd) {
    trans = checkRangeHb(trans, bd);
    return clipPixelHighbd(static_cast<int>(dest + static_cast<std::int32_t>(trans)), bd);
}

void genInvStageRangeHb(std::int8_t* stageRangeCol, std::int8_t* stageRangeRow,
                        const Txfm2dFlipCfg& cfg, TxSize txSize, int bd) {
    const std::int32_t fwdShift = kInvStartRange[static_cast<int>(txSize)];
    const std::int8_t* shift = cfg.shift;
    std::int8_t optRangeRow, optRangeCol;
    if (bd == 8) {
        optRangeRow = 16;
        optRangeCol = 16;
    } else if (bd == 10) {
        optRangeRow = 18;
        optRangeCol = 16;
    } else {
        assert(bd == 12);
        optRangeRow = 20;
        optRangeCol = 18;
    }
    for (int i = 0; i < cfg.stageNumRow && i < kMaxTxfmStageNum; ++i) {
        const std::int32_t realRangeRow = cfg.stageRangeRow[i] + fwdShift + bd + 1;
        (void)realRangeRow;
        if (cfg.txfmTypeRow == TxfmTypeLs::ADST4 && i == 1) {
            // the adst4 may use 1 extra bit on top of opt_range_row at stage 1
            // so opt_range_col >= real_range_col will not hold
            stageRangeRow[i] = optRangeRow;
        } else {
            assert(optRangeRow >= realRangeRow);
            stageRangeRow[i] = optRangeRow;
        }
    }
    for (int i = 0; i < cfg.stageNumCol && i < kMaxTxfmStageNum; ++i) {
        const std::int32_t realRangeCol = cfg.stageRangeCol[i] + fwdShift + shift[0] + bd + 1;
        (void)realRangeCol;
        if (cfg.txfmTypeCol == TxfmTypeLs::ADST4 && i == 1) {
            stageRangeCol[i] = optRangeCol;
        } else {
            assert(optRangeCol >= realRangeCol);
            stageRangeCol[i] = optRangeCol;
        }
    }
}

namespace {

// clamp_value (inv_transforms.c:88-93)
std::int32_t clampValueHb(std::int32_t value, std::int8_t bit) {
    if (bit <= 0) {
        return value;
    }
    const std::int64_t maxValue = (1LL << (bit - 1)) - 1;
    const std::int64_t minValue = -(1LL << (bit - 1));
    return static_cast<std::int32_t>(clamp64Hb(value, minValue, maxValue));
}

// clamp_buf (inv_transforms.c:816-820)
void clampBufIvHb(std::int32_t* buf, int size, std::int8_t bit) {
    for (int i = 0; i < size; ++i) {
        buf[i] = clampValueHb(buf[i], bit);
    }
}

// svt_av1_round_shift_array_c (inv_transforms.c:2449-2464)
void roundShiftArrayIvHb(std::int32_t* arr, int size, int bit) {
    if (bit == 0) {
        return;
    }
    if (bit > 0) {
        for (int i = 0; i < size; ++i) {
            arr[i] = roundShift(arr[i], bit);
        }
    } else {
        for (int i = 0; i < size; ++i) {
            arr[i] = arr[i] * (1 << (-bit));
        }
    }
}

// the 1D table for the Hb core: the stage-range-parameterized kernels
using InvTxfmFnHb = void (*)(const std::int32_t*, std::int32_t*, const std::int8_t*);

// svt_aom_inv_txfm_type_to_func (inv_transforms.c:2391-2425) over the Hb
// kernel forms; INVALID maps to nullptr + assert
InvTxfmFnHb inv1dHb(TxfmTypeLs type) {
    switch (type) {
    case TxfmTypeLs::DCT4: return idct4Hb;
    case TxfmTypeLs::ADST4: return iadst4Hb;
    case TxfmTypeLs::DCT8: return idct8Hb;
    case TxfmTypeLs::ADST8: return iadst8Hb;
    case TxfmTypeLs::DCT16: return idct16Hb;
    case TxfmTypeLs::ADST16: return iadst16Hb;
    case TxfmTypeLs::DCT32: return idct32Hb;
    case TxfmTypeLs::ADST32: return iadst32Hb;
    case TxfmTypeLs::DCT64: return idct64Hb;
    case TxfmTypeLs::IDENTITY4: return invIdentity4Hb;
    case TxfmTypeLs::IDENTITY8: return invIdentity8Hb;
    case TxfmTypeLs::IDENTITY16: return invIdentity16Hb;
    case TxfmTypeLs::IDENTITY32: return invIdentity32Hb;
    case TxfmTypeLs::IDENTITY64: return invIdentity64Hb;
    default: return nullptr;
    }
}

// av1_get_max_eob (inv_transforms.h:129-137)
std::uint32_t av1GetMaxEob(TxSize txSize) {
    if (txSize == TxSize::TX_64X64 || txSize == TxSize::TX_64X32 || txSize == TxSize::TX_32X64) {
        return 1024;
    }
    if (txSize == TxSize::TX_16X64 || txSize == TxSize::TX_64X16) {
        return 512;
    }
    return static_cast<std::uint32_t>(txSizeWide(txSize) * txSizeHigh(txSize));
}

// the default-shape per-size highbd fwd statics (transforms.c:3726-3834);
// the bd argument is behaviorally dead on the fwd side and is dropped (named)
void highbdFwdTxfm64x64(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    assert(txfmParam->txType == TxType::DCT_DCT);
    (void)txfmParam;
    fwdTxfm2d64x64(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), TxType::DCT_DCT);
}

void highbdFwdTxfm32x64(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    assert(txfmParam->txType == TxType::DCT_DCT);
    fwdTxfm2d32x64(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), txfmParam->txType);
}

void highbdFwdTxfm64x32(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    assert(txfmParam->txType == TxType::DCT_DCT);
    fwdTxfm2d64x32(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), txfmParam->txType);
}

void highbdFwdTxfm16x64(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    assert(txfmParam->txType == TxType::DCT_DCT);
    (void)txfmParam;
    fwdTxfm2d16x64(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), TxType::DCT_DCT);
}

void highbdFwdTxfm64x16(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    assert(txfmParam->txType == TxType::DCT_DCT);
    (void)txfmParam;
    fwdTxfm2d64x16(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), TxType::DCT_DCT);
}

void highbdFwdTxfm32x32(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    fwdTxfm2d32x32(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), txfmParam->txType);
}

void highbdFwdTxfm16x16(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    fwdTxfm2d16x16(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), txfmParam->txType);
}

void highbdFwdTxfm8x8(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    fwdTxfm2d8x8(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), txfmParam->txType);
}

void highbdFwdTxfm4x8(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    fwdTxfm2d4x8(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), txfmParam->txType);
}

void highbdFwdTxfm8x4(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    fwdTxfm2d8x4(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), txfmParam->txType);
}

void highbdFwdTxfm8x16(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    fwdTxfm2d8x16(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), txfmParam->txType);
}

void highbdFwdTxfm16x8(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    fwdTxfm2d16x8(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), txfmParam->txType);
}

void highbdFwdTxfm16x32(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    fwdTxfm2d16x32(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), txfmParam->txType);
}

void highbdFwdTxfm32x16(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    fwdTxfm2d32x16(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), txfmParam->txType);
}

void highbdFwdTxfm4x16(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    fwdTxfm2d4x16(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), txfmParam->txType);
}

void highbdFwdTxfm16x4(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    fwdTxfm2d16x4(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), txfmParam->txType);
}

void highbdFwdTxfm8x32(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    fwdTxfm2d8x32(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), txfmParam->txType);
}

void highbdFwdTxfm32x8(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride, const TxfmParamHb* txfmParam) {
    fwdTxfm2d32x8(srcDiff, coeff, static_cast<std::uint32_t>(diffStride), txfmParam->txType);
}

// the per-size highbd inv statics (inv_transforms.c:2901-3137) are inlined
// into the dispatcher's switch below: each case is the facade call with the
// r/w pointers threaded (the reference's CONVERT_TO_SHORTPTR is dropped)

}  // namespace

// ---------------------------------------------------------------------------
// G3: the bd-parameterized 2D core over uint16 buffers
// ---------------------------------------------------------------------------

void invTxfm2dAddCoreHb(const std::int32_t* input, std::uint16_t* dstR, std::uint32_t strideR,
                        std::uint16_t* dstW, std::uint32_t strideW, const Txfm2dFlipCfg& cfg,
                        std::int32_t* txfmBuf, int bd) {
    const int txfmSizeCol = txSizeWide(cfg.txSize);
    const int txfmSizeRow = txSizeHigh(cfg.txSize);
    const std::int8_t* shift = cfg.shift;
    const int rectType = rectTxLogRatio(txfmSizeCol, txfmSizeRow);

    const InvTxfmFnHb txfmCol = inv1dHb(cfg.txfmTypeCol);
    const InvTxfmFnHb txfmRow = inv1dHb(cfg.txfmTypeRow);
    assert(txfmCol != nullptr);
    assert(txfmRow != nullptr);

    std::int8_t stageRangeCol[kMaxTxfmStageNum] = {0};
    std::int8_t stageRangeRow[kMaxTxfmStageNum] = {0};
    genInvStageRangeHb(stageRangeCol, stageRangeRow, cfg, cfg.txSize, bd);

    const int bufOffset = txfmSizeRow > txfmSizeCol ? txfmSizeRow : txfmSizeCol;
    std::int32_t* tempIn = txfmBuf;
    std::int32_t* tempOut = tempIn + bufOffset;
    std::int32_t* buf = tempOut + bufOffset;
    std::int32_t* bufPtr = buf;

    // Rows
    for (int r = 0; r < txfmSizeRow; ++r) {
        if (rectType == 1 || rectType == -1) {
            for (int c = 0; c < txfmSizeCol; ++c) {
                tempIn[c] = roundShift(static_cast<std::int64_t>(input[c]) * 2896, 12);
            }
        } else {
            for (int c = 0; c < txfmSizeCol; ++c) {
                tempIn[c] = input[c];
            }
        }
        clampBufIvHb(tempIn, txfmSizeCol, static_cast<std::int8_t>(bd + 8));
        txfmRow(tempIn, bufPtr, stageRangeRow);
        roundShiftArrayIvHb(bufPtr, txfmSizeCol, -shift[0]);
        input += txfmSizeCol;
        bufPtr += txfmSizeCol;
    }

    // Columns
    for (int c = 0; c < txfmSizeCol; ++c) {
        if (cfg.lrFlip == 0) {
            for (int r = 0; r < txfmSizeRow; ++r) {
                tempIn[r] = buf[r * txfmSizeCol + c];
            }
        } else {
            // flip left right (inv_transforms.c:2558-2563)
            for (int r = 0; r < txfmSizeRow; ++r) {
                tempIn[r] = buf[r * txfmSizeCol + (txfmSizeCol - c - 1)];
            }
        }
        clampBufIvHb(tempIn, txfmSizeRow, static_cast<std::int8_t>(bd + 6 > 16 ? bd + 6 : 16));
        txfmCol(tempIn, tempOut, stageRangeCol);
        roundShiftArrayIvHb(tempOut, txfmSizeRow, -shift[1]);
        if (cfg.udFlip == 0) {
            for (int r = 0; r < txfmSizeRow; ++r) {
                dstW[r * strideW + c] = highbdClipPixelAdd(dstR[r * strideR + c], tempOut[r], bd);
            }
        } else {
            // flip upside down (inv_transforms.c:2572-2576)
            for (int r = 0; r < txfmSizeRow; ++r) {
                dstW[r * strideW + c] =
                    highbdClipPixelAdd(dstR[r * strideR + c], tempOut[txfmSizeRow - r - 1], bd);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// G4: the per-shape facades (the reference's own r/w-split signature)
// ---------------------------------------------------------------------------

void invTxfm2dAddHb4x4(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                       std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_4X4, cfg);
    std::int32_t txfmBuf[4 * 4 + 2 * 4];
    invTxfm2dAddCoreHb(coeffs, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

void invTxfm2dAddHb8x8(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                       std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_8X8, cfg);
    std::int32_t txfmBuf[8 * 8 + 2 * 8];
    invTxfm2dAddCoreHb(coeffs, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

void invTxfm2dAddHb16x16(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_16X16, cfg);
    std::int32_t txfmBuf[16 * 16 + 2 * 16];
    invTxfm2dAddCoreHb(coeffs, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

void invTxfm2dAddHb32x32(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_32X32, cfg);
    std::int32_t txfmBuf[32 * 32 + 2 * 32];
    invTxfm2dAddCoreHb(coeffs, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

// The LANDED 4096 facade contract (the FS5d named follow-up): the l6/GPU
// callers expand the compacted dqcoeff themselves and pass a full 64x64
// block. The Hb twin mirrors the landed contract, NOT the reference's
// compacting remap.
void invTxfm2dAddHb64x64(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_64X64, cfg);
    std::int32_t txfmBuf[64 * 64 + 2 * 64];
    invTxfm2dAddCoreHb(coeffs, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

void invTxfm2dAddHb4x8(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                       std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_4X8, cfg);
    std::int32_t txfmBuf[4 * 8 + 2 * 8];
    invTxfm2dAddCoreHb(coeffs, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

void invTxfm2dAddHb8x4(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                       std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_8X4, cfg);
    std::int32_t txfmBuf[8 * 4 + 2 * 8];
    invTxfm2dAddCoreHb(coeffs, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

void invTxfm2dAddHb8x16(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                        std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_8X16, cfg);
    std::int32_t txfmBuf[8 * 16 + 2 * 16];
    invTxfm2dAddCoreHb(coeffs, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

void invTxfm2dAddHb16x8(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                        std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_16X8, cfg);
    std::int32_t txfmBuf[16 * 8 + 2 * 16];
    invTxfm2dAddCoreHb(coeffs, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

void invTxfm2dAddHb16x32(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_16X32, cfg);
    std::int32_t txfmBuf[16 * 32 + 2 * 32];
    invTxfm2dAddCoreHb(coeffs, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

void invTxfm2dAddHb32x16(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_32X16, cfg);
    std::int32_t txfmBuf[32 * 16 + 2 * 32];
    invTxfm2dAddCoreHb(coeffs, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

void invTxfm2dAddHb4x16(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                        std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_4X16, cfg);
    std::int32_t txfmBuf[4 * 16 + 2 * 16];
    invTxfm2dAddCoreHb(coeffs, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

void invTxfm2dAddHb16x4(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                        std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_16X4, cfg);
    std::int32_t txfmBuf[16 * 4 + 2 * 16];
    invTxfm2dAddCoreHb(coeffs, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

void invTxfm2dAddHb8x32(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                        std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_8X32, cfg);
    std::int32_t txfmBuf[8 * 32 + 2 * 32];
    invTxfm2dAddCoreHb(coeffs, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

void invTxfm2dAddHb32x8(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                        std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_32X8, cfg);
    std::int32_t txfmBuf[32 * 8 + 2 * 32];
    invTxfm2dAddCoreHb(coeffs, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

// svt_av1_inv_txfm2d_add_64x32_c (inv_transforms.c:2676-2690): the 32x32
// compacted input is remapped into the top-left of a 64-wide, 32-tall block
// with the rest zeroed (eob UNUSED)
void invTxfm2dAddHb64x32(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_64X32, cfg);
    std::int32_t modInput[64 * 32];
    for (int row = 0; row < 32; ++row) {
        for (int col = 0; col < 32; ++col) {
            modInput[row * 64 + col] = coeffs[row * 32 + col];
            modInput[row * 64 + col + 32] = 0;
        }
    }
    std::int32_t txfmBuf[64 * 32 + 2 * 64];
    invTxfm2dAddCoreHb(modInput, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

// svt_av1_inv_txfm2d_add_32x64_c (inv_transforms.c:2692-2704): the 32x32
// compacted input is copied contiguously into the top half of a 32-wide,
// 64-tall block with the bottom half zeroed (eob UNUSED)
void invTxfm2dAddHb32x64(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_32X64, cfg);
    std::int32_t modInput[32 * 64];
    for (int i = 0; i < 32 * 32; ++i) {
        modInput[i] = coeffs[i];
        modInput[i + 32 * 32] = 0;
    }
    std::int32_t txfmBuf[32 * 64 + 2 * 64];
    invTxfm2dAddCoreHb(modInput, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

// svt_av1_inv_txfm2d_add_16x64_c (inv_transforms.c:2706-2718): the 16x32
// compacted input is copied contiguously into the top half of a 16-wide,
// 64-tall block with the bottom half zeroed (eob UNUSED)
void invTxfm2dAddHb16x64(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_16X64, cfg);
    std::int32_t modInput[16 * 64];
    for (int i = 0; i < 16 * 32; ++i) {
        modInput[i] = coeffs[i];
        modInput[i + 16 * 32] = 0;
    }
    std::int32_t txfmBuf[16 * 64 + 2 * 64];
    invTxfm2dAddCoreHb(modInput, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

// svt_av1_inv_txfm2d_add_64x16_c (inv_transforms.c:2720-2734): the 32x16
// compacted input is remapped into the left half of a 64-wide, 16-tall block
// with the rest zeroed (eob UNUSED)
void invTxfm2dAddHb64x16(const std::int32_t* coeffs, std::uint16_t* dstR, std::int32_t strideR,
                         std::uint16_t* dstW, std::int32_t strideW, TxType type, int bd) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_64X16, cfg);
    std::int32_t modInput[64 * 16];
    for (int row = 0; row < 16; ++row) {
        for (int col = 0; col < 32; ++col) {
            modInput[row * 64 + col] = coeffs[row * 32 + col];
            modInput[row * 64 + col + 32] = 0;
        }
    }
    std::int32_t txfmBuf[16 * 64 + 2 * 64];
    invTxfm2dAddCoreHb(modInput, dstR, static_cast<std::uint32_t>(strideR), dstW,
                       static_cast<std::uint32_t>(strideW), cfg, txfmBuf, bd);
}

// ---------------------------------------------------------------------------
// G6: the dispatcher + entry chain
// ---------------------------------------------------------------------------

// highbd_inv_txfm_add (inv_transforms.c:3167-3236) over the 19 facades; the
// per-size statics are inlined as the facade calls (the support gate is the
// landed invTxfmSupported)
void invTxfm2dAddHb(const std::int32_t* coeffs, std::uint16_t* dst, std::uint32_t stride,
                    TxSize size, TxType type, int bd) {
    assert(invTxfmSupported(size, type));
    switch (size) {
    case TxSize::TX_32X32: invTxfm2dAddHb32x32(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_16X16: invTxfm2dAddHb16x16(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_8X8: invTxfm2dAddHb8x8(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_4X8: invTxfm2dAddHb4x8(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_8X4: invTxfm2dAddHb8x4(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_8X16: invTxfm2dAddHb8x16(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_16X8: invTxfm2dAddHb16x8(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_16X32: invTxfm2dAddHb16x32(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_32X16: invTxfm2dAddHb32x16(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_64X64: invTxfm2dAddHb64x64(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_32X64: invTxfm2dAddHb32x64(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_64X32: invTxfm2dAddHb64x32(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_16X64: invTxfm2dAddHb16x64(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_64X16: invTxfm2dAddHb64x16(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_4X4: invTxfm2dAddHb4x4(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_16X4: invTxfm2dAddHb16x4(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_4X16: invTxfm2dAddHb4x16(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_8X32: invTxfm2dAddHb8x32(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    case TxSize::TX_32X8: invTxfm2dAddHb32x8(coeffs, dst, static_cast<std::int32_t>(stride), dst, static_cast<std::int32_t>(stride), type, bd); break;
    default: assert(0); break;
    }
}

void iwht4x4_16AddHb(const std::int32_t* input, std::uint16_t* dstR, std::int32_t strideR,
                     std::uint16_t* dstW, std::int32_t strideW, int bd) {
    // 4-point reversible, orthonormal inverse Walsh-Hadamard; UNIT_QUANT_SHIFT
    // = 2 (transforms.h:25); the range_check_value calls are dead
    // (DO_RANGE_CHECK_CLAMP undefined) and are dropped
    std::int32_t output[16];
    std::int32_t a1, b1, c1, d1, e1;
    const std::int32_t* ip = input;
    std::int32_t* op = output;

    for (int i = 0; i < 4; i++) {
        a1 = ip[0] >> 2;
        c1 = ip[1] >> 2;
        d1 = ip[2] >> 2;
        b1 = ip[3] >> 2;
        a1 += c1;
        d1 -= b1;
        e1 = (a1 - d1) >> 1;
        b1 = e1 - b1;
        c1 = e1 - c1;
        a1 -= b1;
        d1 += c1;
        op[0] = a1;
        op[1] = b1;
        op[2] = c1;
        op[3] = d1;
        ip += 4;
        op += 4;
    }

    ip = output;
    for (int i = 0; i < 4; i++) {
        a1 = ip[4 * 0];
        c1 = ip[4 * 1];
        d1 = ip[4 * 2];
        b1 = ip[4 * 3];
        a1 += c1;
        d1 -= b1;
        e1 = (a1 - d1) >> 1;
        b1 = e1 - b1;
        c1 = e1 - c1;
        a1 -= b1;
        d1 += c1;

        dstW[strideW * 0] = highbdClipPixelAdd(dstR[strideR * 0], a1, bd);
        dstW[strideW * 1] = highbdClipPixelAdd(dstR[strideR * 1], b1, bd);
        dstW[strideW * 2] = highbdClipPixelAdd(dstR[strideR * 2], c1, bd);
        dstW[strideW * 3] = highbdClipPixelAdd(dstR[strideR * 3], d1, bd);

        ip++;
        dstR++;
        dstW++;
    }
}

void iwht4x4_1AddHb(const std::int32_t* in, std::uint16_t* dstR, std::int32_t dstStrideR,
                    std::uint16_t* dstW, std::int32_t dstStrideW, int bd) {
    std::int32_t a1, e1;
    std::int32_t tmp[4];
    const std::int32_t* ip = in;
    std::int32_t* op = tmp;

    a1 = ip[0] >> 2;
    e1 = a1 >> 1;
    a1 -= e1;
    op[0] = a1;
    op[1] = op[2] = op[3] = e1;

    ip = tmp;
    for (int i = 0; i < 4; i++) {
        e1 = ip[0] >> 1;
        a1 = ip[0] - e1;
        dstW[dstStrideW * 0] = highbdClipPixelAdd(dstR[dstStrideR * 0], a1, bd);
        dstW[dstStrideW * 1] = highbdClipPixelAdd(dstR[dstStrideR * 1], e1, bd);
        dstW[dstStrideW * 2] = highbdClipPixelAdd(dstR[dstStrideR * 2], e1, bd);
        dstW[dstStrideW * 3] = highbdClipPixelAdd(dstR[dstStrideR * 3], e1, bd);
        ip++;
        dstR++;
        dstW++;
    }
}

// svt_av1_highbd_inv_txfm_add_4x4 (inv_transforms.c:2884-2899)
void highbdInvTxfmAdd4x4Hb(const std::int32_t* input, std::uint16_t* dstR, std::int32_t strideR,
                           std::uint16_t* dstW, std::int32_t strideW, const TxfmParamHb& txfmParam) {
    const std::int32_t eob = txfmParam.eob;
    const std::int32_t bd = txfmParam.bd;
    const std::int32_t lossless = txfmParam.lossless;
    if (lossless) {
        assert(txfmParam.txType == TxType::DCT_DCT);
        if (eob > 1) {
            iwht4x4_16AddHb(input, dstR, strideR, dstW, strideW, bd);
        } else {
            iwht4x4_1AddHb(input, dstR, strideR, dstW, strideW, bd);
        }
        return;
    }
    invTxfm2dAddHb4x4(input, dstR, strideR, dstW, strideW, txfmParam.txType, bd);
}

// highbd_inv_txfm_add (inv_transforms.c:3167-3236): the per-size statics
// (2901-3137) inlined as facade calls with the r/w pointers threaded
void highbdInvTxfmAddHb(const std::int32_t* input, std::uint16_t* dstR, std::int32_t strideR,
                        std::uint16_t* dstW, std::int32_t strideW, const TxfmParamHb& txfmParam) {
    const TxSize txSize = txfmParam.txSize;
    const std::int32_t bd = txfmParam.bd;
    const TxType txType = txfmParam.txType;
    switch (txSize) {
    case TxSize::TX_32X32:
        // the wrapper switch takes DCT_DCT or IDTX only (:2924-2932); the
        // default asserts and does NOT call
        switch (txType) {
        case TxType::DCT_DCT:
        case TxType::IDTX:
            invTxfm2dAddHb32x32(input, dstR, strideR, dstW, strideW, txType, bd);
            break;
        default:
            assert(0);
            break;
        }
        break;
    case TxSize::TX_16X16:
        invTxfm2dAddHb16x16(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_8X8:
        invTxfm2dAddHb8x8(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_4X8:
        invTxfm2dAddHb4x8(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_8X4:
        invTxfm2dAddHb8x4(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_8X16:
        invTxfm2dAddHb8x16(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_16X8:
        invTxfm2dAddHb16x8(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_16X32:
        invTxfm2dAddHb16x32(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_32X16:
        invTxfm2dAddHb32x16(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_64X64:
        assert(txType == TxType::DCT_DCT);
        invTxfm2dAddHb64x64(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_32X64:
        invTxfm2dAddHb32x64(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_64X32:
        invTxfm2dAddHb64x32(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_16X64:
        invTxfm2dAddHb16x64(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_64X16:
        invTxfm2dAddHb64x16(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_4X4:
        // this is like av1_short_idct4x4 but has a special case around eob<=1
        // which is significant (not just an optimization) for the lossless
        // case
        highbdInvTxfmAdd4x4Hb(input, dstR, strideR, dstW, strideW, txfmParam);
        break;
    case TxSize::TX_16X4:
        invTxfm2dAddHb16x4(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_4X16:
        invTxfm2dAddHb4x16(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_8X32:
        invTxfm2dAddHb8x32(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    case TxSize::TX_32X8:
        invTxfm2dAddHb32x8(input, dstR, strideR, dstW, strideW, txType, bd);
        break;
    default:
        assert(0 && "Invalid transform size");
        break;
    }
}

void invTransformReconHb(const std::int32_t* coeffBuffer, std::uint16_t* reconR, std::uint32_t reconStrideR,
                         std::uint16_t* reconW, std::uint32_t reconStrideW, TxSize txSize,
                         std::uint32_t bitDepth, TxType transformType, std::uint32_t eob,
                         bool lossless) {
    TxfmParamHb txfmParam;
    txfmParam.txType = transformType;
    txfmParam.txSize = txSize;
    txfmParam.eob = static_cast<std::int32_t>(eob);
    txfmParam.lossless = lossless ? 1 : 0;
    txfmParam.bd = static_cast<std::int32_t>(bitDepth);
    txfmParam.isHbd = 1;

    if (reconR != reconW) {
        // When output pointers to read and write are differents,
        // then kernel copy also all buffer from read to write,
        // and cannot be limited by End Of Buffer calculations.
        txfmParam.eob = static_cast<std::int32_t>(av1GetMaxEob(txSize));
    }

    highbdInvTxfmAddHb(coeffBuffer, reconR, static_cast<std::int32_t>(reconStrideR), reconW,
                       static_cast<std::int32_t>(reconStrideW), txfmParam);
}

void invTransformRecon8bitHb(const std::int32_t* coeffBuffer, std::uint16_t* reconR,
                             std::uint32_t reconStrideR, std::uint16_t* reconW,
                             std::uint32_t reconStrideW, TxSize txSize, TxType transformType,
                             std::uint32_t eob, bool lossless) {
    TxfmParamHb txfmParam;
    txfmParam.txType = transformType;
    txfmParam.txSize = txSize;
    txfmParam.eob = static_cast<std::int32_t>(eob);
    txfmParam.lossless = lossless ? 1 : 0;
    txfmParam.bd = 8;
    txfmParam.isHbd = 1;

    if (reconR != reconW) {
        txfmParam.eob = static_cast<std::int32_t>(av1GetMaxEob(txSize));
    }

    highbdInvTxfmAddHb(coeffBuffer, reconR, static_cast<std::int32_t>(reconStrideR), reconW,
                       static_cast<std::int32_t>(reconStrideW), txfmParam);
}

void invTxfmAddStagingHb(const std::int32_t* dqcoeff, std::uint8_t* dstR, std::int32_t strideR,
                         std::uint8_t* dstW, std::int32_t strideW, const TxfmParamHb& txfmParam) {
    const TxSize txSize = txfmParam.txSize;
    std::uint16_t tmp[kMaxTxSquare];
    const std::int32_t tmpStride = kMaxTxSize;
    const std::int32_t w = txSizeWide(txSize);
    const std::int32_t h = txSizeHigh(txSize);
    for (std::int32_t r = 0; r < h; ++r) {
        for (std::int32_t c = 0; c < w; ++c) {
            tmp[r * tmpStride + c] = dstR[r * strideR + c];
        }
    }

    highbdInvTxfmAddHb(dqcoeff, tmp, tmpStride, tmp, tmpStride, txfmParam);

    for (std::int32_t r = 0; r < h; ++r) {
        for (std::int32_t c = 0; c < w; ++c) {
            dstW[r * strideW + c] = static_cast<std::uint8_t>(tmp[r * tmpStride + c]);
        }
    }
}

// ---------------------------------------------------------------------------
// G7: the fwd dispatch chain
// ---------------------------------------------------------------------------

void genFwdStageRangeHb(std::int8_t* stageRangeCol, std::int8_t* stageRangeRow,
                        const Txfm2dFlipCfg& cfg, int bd) {
    // Take the shift from the larger dimension in the rectangular case.
    const std::int8_t* shift = cfg.shift;
    for (int i = 0; i < cfg.stageNumCol && i < kMaxTxfmStageNum; ++i) {
        stageRangeCol[i] = static_cast<std::int8_t>(cfg.stageRangeCol[i] + shift[0] + bd + 1);
    }
    for (int i = 0; i < cfg.stageNumRow && i < kMaxTxfmStageNum; ++i) {
        stageRangeRow[i] =
            static_cast<std::int8_t>(cfg.stageRangeRow[i] + shift[0] + shift[1] + bd + 1);
    }
}

// svt_av1_wht_fwd_txfm (transforms.c:4037-4057)
void whtFwdTxfmHb(std::int16_t* srcDiff, int bw, std::int32_t* coeff, TxSize txSize,
                  TxCoeffShapeHb pfShape, int bitDepth, int isHbd) {
    TxfmParamHb txfmParam;
    txfmParam.txType = TxType::DCT_DCT;
    txfmParam.txSize = txSize;
    txfmParam.lossless = 0;
    txfmParam.bd = bitDepth;
    txfmParam.isHbd = isHbd;
    switch (pfShape) {
    case TxCoeffShapeHb::N4_SHAPE:
    case TxCoeffShapeHb::N2_SHAPE:
        // PARKED with the N2/N4 PF-shape family (the highbd statics
        // transforms.c:3505-3725 and dispatchers :3836-3968 route into
        // svt_av1_fwd_txfm2d_*_N2/_N4, whose 1D kernel family :4059-5858 and
        // :5860-7454 is not landed in any shape)
        assert(0);
        break;
    default:
        highbdFwdTxfmHb(srcDiff, coeff, bw, txfmParam);
        break;
    }
}

// svt_av1_highbd_fwd_txfm (transforms.c:3970-4035)
void highbdFwdTxfmHb(std::int16_t* srcDiff, std::int32_t* coeff, int diffStride,
                     const TxfmParamHb& txfmParam) {
    const TxSize txSize = txfmParam.txSize;
    switch (txSize) {
    case TxSize::TX_64X64: highbdFwdTxfm64x64(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_32X64: highbdFwdTxfm32x64(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_64X32: highbdFwdTxfm64x32(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_16X64: highbdFwdTxfm16x64(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_64X16: highbdFwdTxfm64x16(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_32X32: highbdFwdTxfm32x32(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_16X16: highbdFwdTxfm16x16(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_8X8: highbdFwdTxfm8x8(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_4X8: highbdFwdTxfm4x8(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_8X4: highbdFwdTxfm8x4(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_8X16: highbdFwdTxfm8x16(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_16X8: highbdFwdTxfm16x8(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_16X32: highbdFwdTxfm16x32(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_32X16: highbdFwdTxfm32x16(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_4X4:
        // hack highbd_fwd_txfm_4x4 (transforms.c:4016-4018): the reference
        // no-ops the 4x4 case in this dispatcher
        break;
    case TxSize::TX_4X16: highbdFwdTxfm4x16(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_16X4: highbdFwdTxfm16x4(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_8X32: highbdFwdTxfm8x32(srcDiff, coeff, diffStride, &txfmParam); break;
    case TxSize::TX_32X8: highbdFwdTxfm32x8(srcDiff, coeff, diffStride, &txfmParam); break;
    default:
        assert(0);
        break;
    }
}

// ---------------------------------------------------------------------------
// QH1: the A1 highbd quantizer surface. The pinned tree is
// CONFIG_ENABLE_HIGH_BIT_DEPTH=0 (Source/API/EbConfigMacros.h:55-57), so the
// reference's 10/12-bit table arms and their tables are compiled out there;
// this port carries the CONFIG=1 surface (the TXH1 hbd-twin precedent) and
// keeps the -1 default arm.
// ---------------------------------------------------------------------------

// inv_transforms.c:3429-3444, CONFIG=1 surface
static const int16_t dc_qlookup_10_QTX[256] = {
    4,    9,    10,   13,   15,   17,   20,   22,   25,   28,   31,   34,   37,   40,   43,   47,   50,   53,   57,
    60,   64,   68,   71,   75,   78,   82,   86,   90,   93,   97,   101,  105,  109,  113,  116,  120,  124,  128,
    132,  136,  140,  143,  147,  151,  155,  159,  163,  166,  170,  174,  178,  182,  185,  189,  193,  197,  200,
    204,  208,  212,  215,  219,  223,  226,  230,  233,  237,  241,  244,  248,  251,  255,  259,  262,  266,  269,
    273,  276,  280,  283,  287,  290,  293,  297,  300,  304,  307,  310,  314,  317,  321,  324,  327,  331,  334,
    337,  343,  350,  356,  362,  369,  375,  381,  387,  394,  400,  406,  412,  418,  424,  430,  436,  442,  448,
    454,  460,  466,  472,  478,  484,  490,  499,  507,  516,  525,  533,  542,  550,  559,  567,  576,  584,  592,
    601,  609,  617,  625,  634,  644,  655,  666,  676,  687,  698,  708,  718,  729,  739,  749,  759,  770,  782,
    795,  807,  819,  831,  844,  856,  868,  880,  891,  906,  920,  933,  947,  961,  975,  988,  1001, 1015, 1030,
    1045, 1061, 1076, 1090, 1105, 1120, 1137, 1153, 1170, 1186, 1202, 1218, 1236, 1253, 1271, 1288, 1306, 1323, 1342,
    1361, 1379, 1398, 1416, 1436, 1456, 1476, 1496, 1516, 1537, 1559, 1580, 1601, 1624, 1647, 1670, 1692, 1717, 1741,
    1766, 1791, 1817, 1844, 1871, 1900, 1929, 1958, 1990, 2021, 2054, 2088, 2123, 2159, 2197, 2236, 2276, 2319, 2363,
    2410, 2458, 2508, 2561, 2616, 2675, 2737, 2802, 2871, 2944, 3020, 3102, 3188, 3280, 3375, 3478, 3586, 3702, 3823,
    3953, 4089, 4236, 4394, 4559, 4737, 4929, 5130, 5347,
};

// inv_transforms.c:3446-3463, CONFIG=1 surface
static const int16_t dc_qlookup_12_QTX[256] = {
    4,     12,    18,    25,    33,    41,    50,    60,    70,    80,    91,    103,   115,   127,   140,   153,
    166,   180,   194,   208,   222,   237,   251,   266,   281,   296,   312,   327,   343,   358,   374,   390,
    405,   421,   437,   453,   469,   484,   500,   516,   532,   548,   564,   580,   596,   611,   627,   643,
    659,   674,   690,   706,   721,   737,   752,   768,   783,   798,   814,   829,   844,   859,   874,   889,
    904,   919,   934,   949,   964,   978,   993,   1008,  1022,  1037,  1051,  1065,  1080,  1094,  1108,  1122,
    1136,  1151,  1165,  1179,  1192,  1206,  1220,  1234,  1248,  1261,  1275,  1288,  1302,  1315,  1329,  1342,
    1368,  1393,  1419,  1444,  1469,  1494,  1519,  1544,  1569,  1594,  1618,  1643,  1668,  1692,  1717,  1741,
    1765,  1789,  1814,  1838,  1862,  1885,  1909,  1933,  1957,  1992,  2027,  2061,  2096,  2130,  2165,  2199,
    2233,  2267,  2300,  2334,  2367,  2400,  2434,  2467,  2499,  2532,  2575,  2618,  2661,  2704,  2746,  2788,
    2830,  2872,  2913,  2954,  2995,  3036,  3076,  3127,  3177,  3226,  3275,  3324,  3373,  3421,  3469,  3517,
    3565,  3621,  3677,  3733,  3788,  3843,  3897,  3951,  4005,  4058,  4119,  4181,  4241,  4301,  4361,  4420,
    4479,  4546,  4612,  4677,  4742,  4807,  4871,  4942,  5013,  5083,  5153,  5222,  5291,  5367,  5442,  5517,
    5591,  5665,  5745,  5825,  5905,  5984,  6063,  6149,  6234,  6319,  6404,  6495,  6587,  6678,  6769,  6867,
    6966,  7064,  7163,  7269,  7376,  7483,  7599,  7715,  7832,  7958,  8085,  8214,  8352,  8492,  8635,  8788,
    8945,  9104,  9275,  9450,  9639,  9832,  10031, 10245, 10465, 10702, 10946, 11210, 11482, 11776, 12081, 12409,
    12750, 13118, 13501, 13913, 14343, 14807, 15290, 15812, 16356, 16943, 17575, 18237, 18949, 19718, 20521, 21387,
};

// inv_transforms.c:3375-3390, CONFIG=1 surface
static const int16_t ac_qlookup_10_QTX[256] = {
    4,    9,    11,   13,   16,   18,   21,   24,   27,   30,   33,   37,   40,   44,   48,   51,   55,   59,   63,
    67,   71,   75,   79,   83,   88,   92,   96,   100,  105,  109,  114,  118,  122,  127,  131,  136,  140,  145,
    149,  154,  158,  163,  168,  172,  177,  181,  186,  190,  195,  199,  204,  208,  213,  217,  222,  226,  231,
    235,  240,  244,  249,  253,  258,  262,  267,  271,  275,  280,  284,  289,  293,  297,  302,  306,  311,  315,
    319,  324,  328,  332,  337,  341,  345,  349,  354,  358,  362,  367,  371,  375,  379,  384,  388,  392,  396,
    401,  409,  417,  425,  433,  441,  449,  458,  466,  474,  482,  490,  498,  506,  514,  523,  531,  539,  547,
    555,  563,  571,  579,  588,  596,  604,  616,  628,  640,  652,  664,  676,  688,  700,  713,  725,  737,  749,
    761,  773,  785,  797,  809,  825,  841,  857,  873,  889,  905,  922,  938,  954,  970,  986,  1002, 1018, 1038,
    1058, 1078, 1098, 1118, 1138, 1158, 1178, 1198, 1218, 1242, 1266, 1290, 1314, 1338, 1362, 1386, 1411, 1435, 1463,
    1491, 1519, 1547, 1575, 1603, 1631, 1663, 1695, 1727, 1759, 1791, 1823, 1859, 1895, 1931, 1967, 2003, 2039, 2079,
    2119, 2159, 2199, 2239, 2283, 2327, 2371, 2415, 2459, 2507, 2555, 2603, 2651, 2703, 2755, 2807, 2859, 2915, 2971,
    3027, 3083, 3143, 3203, 3263, 3327, 3391, 3455, 3523, 3591, 3659, 3731, 3803, 3876, 3952, 4028, 4104, 4184, 4264,
    4348, 4432, 4516, 4604, 4692, 4784, 4876, 4972, 5068, 5168, 5268, 5372, 5476, 5584, 5692, 5804, 5916, 6032, 6148,
    6268, 6388, 6512, 6640, 6768, 6900, 7036, 7172, 7312,
};

// inv_transforms.c:3392-3409, CONFIG=1 surface
static const int16_t ac_qlookup_12_QTX[256] = {
    4,     13,    19,    27,    35,    44,    54,    64,    75,    87,    99,    112,   126,   139,   154,   168,
    183,   199,   214,   230,   247,   263,   280,   297,   314,   331,   349,   366,   384,   402,   420,   438,
    456,   475,   493,   511,   530,   548,   567,   586,   604,   623,   642,   660,   679,   698,   716,   735,
    753,   772,   791,   809,   828,   846,   865,   884,   902,   920,   939,   957,   976,   994,   1012,  1030,
    1049,  1067,  1085,  1103,  1121,  1139,  1157,  1175,  1193,  1211,  1229,  1246,  1264,  1282,  1299,  1317,
    1335,  1352,  1370,  1387,  1405,  1422,  1440,  1457,  1474,  1491,  1509,  1526,  1543,  1560,  1577,  1595,
    1627,  1660,  1693,  1725,  1758,  1791,  1824,  1856,  1889,  1922,  1954,  1987,  2020,  2052,  2085,  2118,
    2150,  2183,  2216,  2248,  2281,  2313,  2346,  2378,  2411,  2459,  2508,  2556,  2605,  2653,  2701,  2750,
    2798,  2847,  2895,  2943,  2992,  3040,  3088,  3137,  3185,  3234,  3298,  3362,  3426,  3491,  3555,  3619,
    3684,  3748,  3812,  3876,  3941,  4005,  4069,  4149,  4230,  4310,  4390,  4470,  4550,  4631,  4711,  4791,
    4871,  4967,  5064,  5160,  5256,  5352,  5448,  5544,  5641,  5737,  5849,  5961,  6073,  6185,  6297,  6410,
    6522,  6650,  6778,  6906,  7034,  7162,  7290,  7435,  7579,  7723,  7867,  8011,  8155,  8315,  8475,  8635,
    8795,  8956,  9132,  9308,  9484,  9660,  9836,  10028, 10220, 10412, 10604, 10812, 11020, 11228, 11437, 11661,
    11885, 12109, 12333, 12573, 12813, 13053, 13309, 13565, 13821, 14093, 14365, 14637, 14925, 15213, 15502, 15806,
    16110, 16414, 16734, 17054, 17390, 17726, 18062, 18414, 18766, 19134, 19502, 19886, 20270, 20670, 21070, 21486,
    21902, 22334, 22766, 23214, 23662, 24126, 24590, 25070, 25551, 26047, 26559, 27071, 27599, 28143, 28687, 29247,
};

// svt_aom_dc_quant_qtx (inv_transforms.c:3467-3482); delta folded 0 (the
// landed 8-bit twin's fold, named deviation)
std::int16_t dcQuantQtxBd(std::int32_t qindex, int bd) {
    const std::int32_t qClamped = qindex < 0 ? 0 : (qindex > 255 ? 255 : qindex);
    switch (bd) {
    case 8: return dcQuantQtx(qClamped);
    case 10: return dc_qlookup_10_QTX[qClamped];
    case 12: return dc_qlookup_12_QTX[qClamped];
    default:
        assert(0 && "bit_depth should be EB_EIGHT_BIT, EB_TEN_BIT or EB_TWELVE_BIT");
        return -1;
    }
}

// svt_aom_ac_quant_qtx (inv_transforms.c:3484-3499); delta folded 0
std::int16_t acQuantQtxBd(std::int32_t qindex, int bd) {
    const std::int32_t qClamped = qindex < 0 ? 0 : (qindex > 255 ? 255 : qindex);
    switch (bd) {
    case 8: return acQuantQtx(qClamped);
    case 10: return ac_qlookup_10_QTX[qClamped];
    case 12: return ac_qlookup_12_QTX[qClamped];
    default:
        assert(0 && "bit_depth should be EB_EIGHT_BIT, EB_TEN_BIT or EB_TWELVE_BIT");
        return -1;
    }
}

// svt_aom_get_qzbin_factor (inv_transforms.c:3501-3514)
std::int32_t qzbinFactorBd(std::int32_t q, int bd) {
    const std::int32_t quant = dcQuantQtxBd(q, bd);
    switch (bd) {
    case 8: return q == 0 ? 64 : (quant < 148 ? 84 : 80);
    case 10: return q == 0 ? 64 : (quant < 592 ? 84 : 80);
    case 12: return q == 0 ? 64 : (quant < 2368 ? 84 : 80);
    default:
        assert(0 && "bit_depth should be EB_EIGHT_BIT, EB_TEN_BIT or EB_TWELVE_BIT");
        return -1;
    }
}

// the luma rows of svt_av1_build_quantizer (md_config_process.c:101-135) with
// the bit depth threaded; deltas 0 and sharpness 0 (the landed 8-bit twin's
// folds, named deviations). roundShift (transform.h:20) is the reference's
// ROUND_POWER_OF_TWO at positive bits.
void buildQuantTablesBd(std::int32_t qindex, int bd, QuantTables& tables) {
    const std::int32_t qzbinFactorVal = qzbinFactorBd(qindex, bd);
    const std::int32_t qroundingFactor = qindex == 0 ? 64 : 48;
    for (int i = 0; i < 2; ++i) {
        const std::int32_t quantQtx = i == 0 ? dcQuantQtxBd(qindex, bd) : acQuantQtxBd(qindex, bd);
        invertQuant(&tables.quant[i], &tables.quantShift[i], quantQtx);
        tables.quantFp[i]  = static_cast<std::int16_t>((1 << 16) / quantQtx);
        tables.roundFp[i]  = static_cast<std::int16_t>((64 * quantQtx) >> 7);
        tables.zbin[i]     = static_cast<std::int16_t>(roundShift(qzbinFactorVal * quantQtx, 7));
        tables.round[i]    = static_cast<std::int16_t>((qroundingFactor * quantQtx) >> 7);
        tables.dequant[i]  = static_cast<std::int16_t>(quantQtx);
    }
}

}  // namespace transforms
