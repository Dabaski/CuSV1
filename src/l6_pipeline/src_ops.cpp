// l6_pipeline/src_ops.cpp - the SRC1 source-ops surface (A6).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/src_ops_process.c
// (2,014 nb). The struct-free kernels + the TplStats-grid carriers; the
// span map + parks live in src_ops.h and tests/test_src_ops.cpp.
#include "src_ops.h"

#include <analysis.h>

#include <cassert>
#include <cstring>

// TPL_DEP_COST_SCALE_LOG2 (definitions.h:68) - local to this TU; not in
// the landed l7/l16 surfaces.
#define TPL_DEP_COST_SCALE_LOG2 4
// MI_SIZE (definitions.h:354) = 1 << MI_SIZE_LOG2 (rate_est.h:788).
#define MI_SIZE (1 << MI_SIZE_LOG2)
// SCALE_NUMERATOR (definitions.h:1430).
#define SCALE_NUMERATOR 8

namespace pipeline {
namespace srcops {

// ---------------------------------------------------------------------------
// Carries.
// ---------------------------------------------------------------------------

// mi_size_wide_log2 / mi_size_high_log2 (common_utils.c:211-212, verbatim).
const std::uint8_t miSizeWideLog2[l16_rdo::BLOCK_SIZES_ALL] = {0, 0, 1, 1, 1, 2, 2, 2,
                                                               3, 3, 3, 4, 4, 4, 5, 5,
                                                               0, 2, 1, 3, 2, 4};
const std::uint8_t miSizeHighLog2[l16_rdo::BLOCK_SIZES_ALL] = {0, 1, 0, 1, 2, 1, 2, 3,
                                                               2, 3, 4, 3, 4, 5, 4, 5,
                                                               2, 0, 3, 1, 4, 2};

// svt_log2f = get_msb (definitions.h:591-592 -> the portable body :628-:644,
// verbatim; the GNUC/MSVC builtin variants :596-:612 are the same function).
std::int32_t svtLog2f(std::int32_t x) {
    std::int32_t  log   = 0;
    std::uint32_t value = (std::uint32_t)x;
    std::int32_t  i;

    assert(x != 0);

    for (i = 4; i >= 0; --i) {
        const std::int32_t  shift = (1 << i);
        const std::uint32_t xv    = value >> shift;

        if (xv != 0) {
            value = xv;
            log += shift;
        }
    }
    return log;
}

namespace {
// GET_MV_RAWPEL (mv.h:57) + get_fullmv_from_mv (mv.h:60-62): the 1/8-pel
// Mv -> full-pel fold. Mv travels as its packed as_int form (x | y << 16,
// little-endian; the RE3 IntraFastCandidate precedent).
struct FullMvPair {
    std::int16_t x;
    std::int16_t y;
};
FullMvPair getFullmvFromMv(std::uint32_t mvPacked) {
    const std::int16_t subpelX = (std::int16_t)(mvPacked & 0xFFFF);
    const std::int16_t subpelY = (std::int16_t)(mvPacked >> 16);
    const FullMvPair fullMv = {(std::int16_t)((subpelX + 3 + (subpelX >= 0)) >> 3),
                               (std::int16_t)((subpelY + 3 + (subpelY >= 0)) >> 3)};
    return fullMv;
}
}  // namespace

// ---------------------------------------------------------------------------
// F1: the struct-free kernels.
// ---------------------------------------------------------------------------

void getNeighborSamplesDc(const std::uint8_t* src, std::uint32_t srcStride,
                          std::uint8_t* above0Row, std::uint8_t* left0Col,
                          std::uint8_t bsize) {
    // top left
    above0Row[-1] = left0Col[-1] = src[-(std::int32_t)srcStride - 1];
    // top
    memcpy(above0Row, src - srcStride, bsize);
    // left
    const std::uint8_t* readPtr = src - 1;
    for (std::uint32_t idx = 0; idx < bsize; ++idx) {
        left0Col[idx] = *readPtr;
        readPtr += srcStride;
    }
}

std::int32_t roundFloor(std::int32_t refPos, std::int32_t bsizePix) {
    std::int32_t round;
    if (refPos < 0) {
        round = -(1 + (-refPos - 1) / bsizePix);
    } else {
        round = refPos / bsizePix;
    }

    return round;
}

std::int32_t getOverlapArea(std::int32_t gridPosRow, std::int32_t gridPosCol,
                            std::int32_t refPosRow, std::int32_t refPosCol,
                            std::int32_t block, l16_rdo::BlockSize bsize) {
    std::int32_t width = 0, height = 0;
    const std::int32_t bw = 4 << miSizeWideLog2[bsize];
    const std::int32_t bh = 4 << miSizeHighLog2[bsize];

    switch (block) {
    case 0:
        width  = gridPosCol + bw - refPosCol;
        height = gridPosRow + bh - refPosRow;
        break;
    case 1:
        width  = refPosCol + bw - gridPosCol;
        height = gridPosRow + bh - refPosRow;
        break;
    case 2:
        width  = gridPosCol + bw - refPosCol;
        height = refPosRow + bh - gridPosRow;
        break;
    case 3:
        width  = refPosCol + bw - gridPosCol;
        height = refPosRow + bh - gridPosRow;
        break;
    default:
        assert(0);
    }

    return width * height;
}

std::int64_t deltaRateCost(std::int64_t deltaRate, std::int64_t recrfDist,
                           std::int64_t srcrfDist, std::int32_t pixNum) {
    const double beta      = (double)srcrfDist / recrfDist;
    std::int64_t rateCost  = deltaRate;

    if (srcrfDist <= 128) {
        return rateCost;
    }

    const double dr = (double)(deltaRate >> (TPL_DEP_COST_SCALE_LOG2 + AV1_PROB_COST_SHIFT)) / pixNum;

    const double logDen = std::log(beta) / std::log(2.0) + 2.0 * dr;

    if (logDen > std::log(10.0) / std::log(2.0)) {
        rateCost = (std::int64_t)((std::log(1.0 / beta) * pixNum) / std::log(2.0) / 2.0);
        rateCost <<= (TPL_DEP_COST_SCALE_LOG2 + AV1_PROB_COST_SHIFT);
        return rateCost;
    }

    const double num = std::pow(2.0, logDen);
    const double den = num * beta + (1 - beta) * beta;

    rateCost = (std::int64_t)((pixNum * std::log(num / den)) / std::log(2.0) / 2.0);

    rateCost <<= (TPL_DEP_COST_SCALE_LOG2 + AV1_PROB_COST_SHIFT);

    return rateCost;
}

std::uint32_t perpixelVariance(const std::uint8_t* buf, std::uint32_t stride,
                               l16_rdo::BlockSize blockSize) {
    std::uint32_t sse = 0;
    std::uint32_t var = 0;
    // DEVIATION (named in src_ops.h): the reference's
    // svt_aom_mefn_ptr[block_size].vf(buf, stride, AV1_VAR_OFFS, 0, &sse)
    // dispatch (:2153-:2154) is replaced by the l9 offs-folded instances;
    // only the two sizes the ported caller (:2193, BLOCK_8X8) needs are
    // wired - the rest of the mefn table is parked with the dispatch wall.
    switch (blockSize) {
    case l16_rdo::BLOCK_8X8: var = analysis::variance8x8(buf, (std::int32_t)stride, &sse); break;
    case l16_rdo::BLOCK_16X16: var = analysis::variance16x16(buf, (std::int32_t)stride, &sse); break;
    default: assert(0); return 0;
    }
    return ROUND_POWER_OF_TWO(var, l16_rdo::ebNumPelsLog2Lookup[blockSize]);
}

// ---------------------------------------------------------------------------
// F2: the TPL rate surface.
// ---------------------------------------------------------------------------

std::int32_t rateEstimator(const std::int32_t* qcoeff, std::int32_t eob,
                           std::int32_t txSize) {
    const auto& scanOrder = l16_rdo::getScanOrder(txSize, 0 /* DCT_DCT */);

    assert((1 << l16_rdo::ebNumPelsLog2Lookup[entropy::txsizeToBsize[txSize]]) >= eob);

    std::int32_t rateCost = eob + 1;

    for (std::int32_t idx = 0; idx < eob; ++idx) {
        const std::int32_t absLevel = std::abs(qcoeff[scanOrder.scan[idx]]);
        rateCost += svtLog2f(absLevel + 1) + (absLevel > 0);
    }

    return (rateCost << AV1_PROB_COST_SHIFT);
}

// ---------------------------------------------------------------------------
// F3: the TPL synthesizer carriers.
// ---------------------------------------------------------------------------

void tplModelUpdateB(const TplModelUpdateBArgs& a) {
    const FullMvPair fullMv    = getFullmvFromMv(a.cur->mv);
    const std::int32_t refPosRow = a.miRow * MI_SIZE + fullMv.y;
    const std::int32_t refPosCol = a.miCol * MI_SIZE + fullMv.x;

    const std::int32_t bw        = 4 << miSizeWideLog2[a.bsize];
    const std::int32_t bh        = 4 << miSizeHighLog2[a.bsize];
    const std::int32_t miHeight  = l16_rdo::miSizeHigh[a.bsize];
    const std::int32_t miWidth   = l16_rdo::miSizeWide[a.bsize];
    const std::int32_t pixNum    = bw * bh;
    const std::int32_t shift     = a.synthBlkSize == 8 ? 1 : a.synthBlkSize == 16 ? 2 : 3;
    const std::int32_t miColsSr  = ((a.refAlignedWidth + 15) / 16) << 2;

    // top-left on grid block location in pixel
    const std::int32_t gridPosRowBase = roundFloor(refPosRow, bh) * bh;
    const std::int32_t gridPosColBase = roundFloor(refPosCol, bw) * bw;

    const std::int64_t curDepDist = a.cur->recrfDist - a.cur->srcrfDist;
    const std::int64_t mcDepDist  = a.cur->mcDepDist * (a.cur->recrfDist - a.cur->srcrfDist) /
        a.cur->recrfDist;
    const std::int64_t deltaRate  = a.cur->recrfRate - a.cur->srcrfRate;
    const std::int64_t mcDepRate  = a.computeRate
        ? deltaRateCost(a.cur->mcDepRate, a.cur->recrfDist, a.cur->srcrfDist, pixNum)
        : 0;

    for (std::int32_t block = 0; block < 4; ++block) {
        const std::int32_t gridPosRow = gridPosRowBase + bh * (block >> 1);
        const std::int32_t gridPosCol = gridPosColBase + bw * (block & 0x01);

        if (gridPosRow >= 0 && gridPosRow < a.refMiRows * MI_SIZE && gridPosCol >= 0 &&
            gridPosCol < a.refMiCols * MI_SIZE) {
            const std::int32_t overlapArea =
                getOverlapArea(gridPosRow, gridPosCol, refPosRow, refPosCol, block, a.bsize);
            const std::int32_t refMiRow = roundFloor(gridPosRow, bh) * miHeight;
            const std::int32_t refMiCol = roundFloor(gridPosCol, bw) * miWidth;
            const std::int32_t step     = 1 << (shift);

            for (std::int32_t idy = 0; idy < miHeight; idy += step) {
                for (std::int32_t idx = 0; idx < miWidth; idx += step) {
                    TplStatsCell* refTplStatsPtr =
                        &a.refGrid[((refMiRow + idy) >> shift) * (miColsSr >> shift) +
                                   ((refMiCol + idx) >> shift)];
                    refTplStatsPtr->mcDepDist += ((curDepDist + mcDepDist) * overlapArea) / pixNum;
                    refTplStatsPtr->mcDepRate += ((deltaRate + mcDepRate) * overlapArea) / pixNum;
                    assert(overlapArea >= 0);
                }
            }
        }
    }
}

void tplModelUpdate(const TplModelUpdateArgs& a) {
    const std::int32_t miHeight = l16_rdo::miSizeHigh[a.bsize];
    const std::int32_t miWidth  = l16_rdo::miSizeWide[a.bsize];
    TplSwFrame* pcs             = &a.frames[a.frameIdx];

    const l16_rdo::BlockSize blockSize = a.synthBlkSize == 8 ? l16_rdo::BLOCK_8X8
        : a.synthBlkSize == 16                               ? l16_rdo::BLOCK_16X16
                                                             : l16_rdo::BLOCK_32X32;
    const std::int32_t shift      = a.synthBlkSize == 8 ? 1 : a.synthBlkSize == 16 ? 2 : 3;
    const std::int32_t step       = 1 << (shift);
    const std::int32_t miColsSr   = ((pcs->alignedWidth + 15) / 16) << 2;
    std::int32_t i                = 0;

    for (std::int32_t idy = 0; idy < miHeight; idy += step) {
        for (std::int32_t idx = 0; idx < miWidth; idx += step) {
            TplStatsCell* tplStatsPtr =
                &pcs->grid[(((a.miRow + idy) >> shift) * (miColsSr >> shift)) +
                           ((a.miCol + idx) >> shift)];

            while (i < a.framesInSw && a.frames[i].pictureNumber != tplStatsPtr->refFramePoc) {
                i++;
            }
            if (i < a.framesInSw) {
                TplModelUpdateBArgs b;
                b.refGrid         = a.frames[i].grid;
                b.cur             = tplStatsPtr;
                b.miRow           = a.miRow + idy;
                b.miCol           = a.miCol + idx;
                b.bsize           = blockSize;
                b.synthBlkSize    = a.synthBlkSize;
                b.computeRate     = a.computeRate;
                b.refAlignedWidth = a.frames[i].alignedWidth;
                b.refMiRows       = a.frames[i].miRows;
                b.refMiCols       = a.frames[i].miCols;
                tplModelUpdateB(b);
            }
        }
    }
}

void tplMcFlowSynthesizer(const TplMcFlowSynthesizerArgs& a) {
    const l16_rdo::BlockSize bsize =
        a.synthBlkSize == 32 ? l16_rdo::BLOCK_32X32 : l16_rdo::BLOCK_16X16;
    const std::int32_t miHeight = l16_rdo::miSizeHigh[bsize];
    const std::int32_t miWidth  = l16_rdo::miSizeWide[bsize];

    for (std::int32_t miRow = 0; miRow < a.miRows; miRow += miHeight) {
        for (std::int32_t miCol = 0; miCol < a.miCols; miCol += miWidth) {
            TplModelUpdateArgs u;
            u.frames       = a.frames;
            u.frameIdx     = a.frameIdx;
            u.framesInSw   = a.framesInSw;
            u.miRow        = miRow;
            u.miCol        = miCol;
            u.synthBlkSize = a.synthBlkSize;
            u.bsize        = bsize;
            u.computeRate  = a.computeRate;
            tplModelUpdate(u);
        }
    }
}

// ---------------------------------------------------------------------------
// F4: the RDO-driver carriers.
// ---------------------------------------------------------------------------

std::int32_t codedToSuperresMi(std::int32_t miCol, std::int32_t denom) {
    return (miCol * denom + SCALE_NUMERATOR / 2) / SCALE_NUMERATOR;
}

void generateLambdaScalingFactor(const GenerateLambdaScalingFactorArgs& a) {
    const std::int32_t tplSynthSizeOffset =
        a.synthBlkSize == 8 ? 1 : a.synthBlkSize == 16 ? 2 : 3;
    const std::int32_t step       = 1 << (tplSynthSizeOffset);
    const std::int32_t miColsSr   = ((a.unscaledWidth + 15) / 16) << 2;
    const std::int32_t blockSize  = a.synthBlkSize == 32 ? l16_rdo::BLOCK_32X32 : l16_rdo::BLOCK_16X16;
    const std::int32_t numMiW     = l16_rdo::miSizeWide[blockSize];
    const std::int32_t numMiH     = l16_rdo::miSizeHigh[blockSize];
    const std::int32_t numCols    = (miColsSr + numMiW - 1) / numMiW;
    const std::int32_t numRows    = (a.miRows + numMiH - 1) / numMiH;
    const std::int32_t stride     = miColsSr >> tplSynthSizeOffset;
    const double c                = 1.2;

    for (std::int32_t row = 0; row < numRows; row++) {
        for (std::int32_t col = 0; col < numCols; col++) {
            std::int64_t   recrfDistSum   = 0;
            std::int64_t   mcDepDeltaSum  = 0;
            const std::int32_t index      = row * numCols + col;
            for (std::int32_t miRow = row * numMiH; miRow < (row + 1) * numMiH; miRow += step) {
                for (std::int32_t miCol = col * numMiW; miCol < (col + 1) * numMiW; miCol += step) {
                    if (miRow >= a.miRows || miCol >= miColsSr) {
                        continue;
                    }

                    const std::int32_t index1 = (miRow >> (tplSynthSizeOffset)) * stride +
                        (miCol >> (tplSynthSizeOffset));
                    const TplStatsCell* tplStatsPtr = &a.grid[index1];
                    const std::int64_t mcDepDelta   = RDCOST(
                        a.baseRdmult, tplStatsPtr->mcDepRate, tplStatsPtr->mcDepDist);
                    recrfDistSum += tplStatsPtr->recrfDist;
                    mcDepDeltaSum += mcDepDelta;
                }
            }
            double scalingFactors = c;
            if (a.mcDepCostBase && (recrfDistSum > 0)) {
                const double rk = ((double)(recrfDistSum << (RDDIV_BITS))) /
                    ((recrfDistSum << RDDIV_BITS) + mcDepDeltaSum);
                scalingFactors += rk / a.r0;
            }
            a.factorsOut[index] = scalingFactors;
        }
    }
}

void generateR0beta(const GenerateR0betaArgs& a) {
    std::int64_t   recrfDistBaseSum   = 0;
    std::int64_t   mcDepDeltaBaseSum  = 0;
    std::int64_t   mcDepCostBase      = 0;
    const std::int32_t shift         = a.synthBlkSize == 8 ? 1 : a.synthBlkSize == 16 ? 2 : 3;
    const std::int32_t step          = 1 << (shift);
    const std::int32_t colStepSr     = codedToSuperresMi(step, a.superresDenom);
    // Super-res upscaled size should be used here.
    const std::int32_t miColsSr      = ((a.unscaledWidth + 15) / 16) << 2;  // picture column boundary
    const std::int32_t miRows        = ((a.unscaledHeight + 15) / 16) << 2;  // picture row boundary
    std::int64_t   count             = 0;
    std::int64_t   maxDist            = 0;

    for (std::int32_t row = 0; row < a.miRowsCur; row += step) {
        for (std::int32_t col = 0; col < miColsSr; col += colStepSr) {
            const TplStatsCell* tplStatsPtr =
                &a.grid[(row >> shift) * (miColsSr >> shift) + (col >> shift)];
            const std::int64_t mcDepDelta = RDCOST(
                a.baseRdmult, tplStatsPtr->mcDepRate, tplStatsPtr->mcDepDist);
            recrfDistBaseSum += tplStatsPtr->recrfDist;
            mcDepDeltaBaseSum += mcDepDelta;
            count++;
            if (mcDepDelta > maxDist) {
                maxDist = mcDepDelta;
            }
        }
    }

    mcDepCostBase = (recrfDistBaseSum << RDDIV_BITS) + mcDepDeltaBaseSum;
    if (mcDepCostBase != 0) {
        *a.r0 = ((double)(recrfDistBaseSum << (RDDIV_BITS))) / mcDepCostBase;
        // If there are outlier blocks responsible for most of the propagation, set r0 to 1.0 to indicate
        // no error propagation, as the result may not be reliable.
        if (maxDist > (mcDepDeltaBaseSum / count) * 100 && maxDist > (mcDepDeltaBaseSum * 9 / 10)) {
            *a.r0 = 1.0;
        }
        *a.tplIsValid = 1;
    } else {
        *a.tplIsValid = 0;
    }

    GenerateLambdaScalingFactorArgs l;
    l.grid          = a.grid;
    l.factorsOut    = a.lambdaFactorsOut;
    l.miRows        = a.miRowsCur;
    l.unscaledWidth = a.unscaledWidth;
    l.synthBlkSize  = a.synthBlkSize;
    l.baseRdmult    = a.baseRdmult;
    l.r0            = *a.r0;
    l.mcDepCostBase = mcDepCostBase;
    generateLambdaScalingFactor(l);

    // If superres scale down is on, should use scaled width instead of full size
    const std::int32_t  sbMiSz           = (std::int32_t)(a.sbSize >> 2);
    const std::uint32_t pictureSbWidth   = (std::uint32_t)((a.alignedWidth + a.sbSize - 1) / a.sbSize);
    const std::uint32_t pictureSbHeight  = (std::uint32_t)((a.alignedHeight + a.sbSize - 1) / a.sbSize);
    const std::int32_t  miHigh           = sbMiSz;  // sb size in 4x4 units
    const std::int32_t  miWide           = sbMiSz;
    for (std::uint32_t sbY = 0; sbY < pictureSbHeight; ++sbY) {
        for (std::uint32_t sbX = 0; sbX < pictureSbWidth; ++sbX) {
            const std::uint16_t miRow = (std::uint16_t)(a.sbGeom[sbY * pictureSbWidth + sbX].orgY >> 2);
            const std::uint16_t miCol = (std::uint16_t)(a.sbGeom[sbY * pictureSbWidth + sbX].orgX >> 2);
            std::int64_t   recrfDistSum   = 0;
            std::int64_t   mcDepDeltaSum  = 0;
            const std::int32_t miColSr     = codedToSuperresMi(miCol, a.superresDenom);
            const std::int32_t miColEndSr  = codedToSuperresMi(miCol + miWide, a.superresDenom);
            const std::int32_t rowStep     = step;

            // loop all mb in the sb
            for (std::int32_t row = miRow; row < miRow + miHigh; row += rowStep) {
                for (std::int32_t col = miColSr; col < miColEndSr; col += colStepSr) {
                    if (row >= miRows || col >= miColsSr) {
                        continue;
                    }

                    const std::int32_t index = (row >> shift) * (miColsSr >> shift) + (col >> shift);
                    const TplStatsCell* tplStatsPtr = &a.grid[index];
                    const std::int64_t mcDepDelta   = RDCOST(
                        a.baseRdmult, tplStatsPtr->mcDepRate, tplStatsPtr->mcDepDist);
                    recrfDistSum += tplStatsPtr->recrfDist;
                    mcDepDeltaSum += mcDepDelta;
                }
            }
            double beta = 1.0;
            if (recrfDistSum > 0) {
                const double rk = ((double)(recrfDistSum << (RDDIV_BITS))) /
                    ((recrfDistSum << RDDIV_BITS) + mcDepDeltaSum);
                beta = (*a.r0 / rk);
                assert(beta > 0.0);
            }
            a.tplBetaOut[sbY * pictureSbWidth + sbX] = beta;
        }
    }
}

void setMbSsimRdmultScaling(const SetMbSsimRdmultScalingArgs& a) {
    const std::int32_t blockSize = l16_rdo::BLOCK_16X16;

    const std::int32_t numMiW = l16_rdo::miSizeWide[blockSize];
    const std::int32_t numMiH = l16_rdo::miSizeHigh[blockSize];
    const std::int32_t numCols = (a.miCols + numMiW - 1) / numMiW;
    const std::int32_t numRows = (a.miRows + numMiH - 1) / numMiH;
    double logSum  = 0.0;

    const double factorA = 67.035434;
    const double factorB = -0.0021489;
    const double factorC = 17.492222;
    // The reference's do_print branches (:2175-:2177, :2211-:2233) are dead
    // (do_print = false const) - omitted (named deviation, dead-branch DCE).

    // Loop through each 16x16 block.
    for (std::int32_t row = 0; row < numRows; ++row) {
        for (std::int32_t col = 0; col < numCols; ++col) {
            double    var       = 0.0, numOfVar = 0.0;
            const std::int32_t index = row * numCols + col;

            // Loop through each 8x8 block.
            for (std::int32_t miRow = row * numMiH; miRow < a.miRows && miRow < (row + 1) * numMiH; miRow += 2) {
                for (std::int32_t miCol = col * numMiW; miCol < a.miCols && miCol < (col + 1) * numMiW; miCol += 2) {
                    const std::int32_t rowOffsetY = miRow << 2;
                    const std::int32_t colOffsetY = miCol << 2;

                    const std::uint8_t* buf = a.yBuffer + rowOffsetY * a.yStride + colOffsetY;

                    var += perpixelVariance(buf, a.yStride, l16_rdo::BLOCK_8X8);
                    numOfVar += 1.0;
                }
            }
            var = var / numOfVar;

            // Curve fitting with an exponential model on all 16x16 blocks from the
            // midres dataset.
            var = factorA * (1 - std::exp(factorB * var)) + factorC;
            // As per the above computation, var will be in the range of
            // [17.492222, 84.527656], assuming the data type is of infinite
            // precision (:2203-:2207).
            assert(var > 17.0 && var < 85.0);
            a.factorsOut[index] = var;
            logSum += std::log(var);
        }
    }
    logSum = std::exp(logSum / (double)(numRows * numCols));
    for (std::int32_t row = 0; row < numRows; ++row) {
        for (std::int32_t col = 0; col < numCols; ++col) {
            const std::int32_t index = row * numCols + col;
            a.factorsOut[index] /= logSum;
        }
    }
}

}  // namespace srcops
}  // namespace pipeline
