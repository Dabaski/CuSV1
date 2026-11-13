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

}  // namespace transforms
