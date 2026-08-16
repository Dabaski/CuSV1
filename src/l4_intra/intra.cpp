#include "intra.h"

#include <cstdlib>

namespace intra {

int filtType(const NeighborContext& neighbors) {
    const bool aboveSmooth = neighbors.aboveMode == SMOOTH_PRED || neighbors.aboveMode == SMOOTH_V_PRED ||
                             neighbors.aboveMode == SMOOTH_H_PRED;
    const bool leftSmooth = neighbors.leftMode == SMOOTH_PRED || neighbors.leftMode == SMOOTH_V_PRED ||
                            neighbors.leftMode == SMOOTH_H_PRED;
    return (aboveSmooth || leftSmooth) ? 1 : 0;
}

int edgeFilterStrength(int bs0, int bs1, int delta, int type) {
    const int d        = std::abs(delta);
    int       strength = 0;

    const int blk_wh = bs0 + bs1;
    if (type == 0) {
        if (blk_wh <= 8) {
            if (d >= 56) {
                strength = 1;
            }
        } else if (blk_wh <= 12) {
            if (d >= 40) {
                strength = 1;
            }
        } else if (blk_wh <= 16) {
            if (d >= 40) {
                strength = 1;
            }
        } else if (blk_wh <= 24) {
            if (d >= 8) {
                strength = 1;
            }
            if (d >= 16) {
                strength = 2;
            }
            if (d >= 32) {
                strength = 3;
            }
        } else if (blk_wh <= 32) {
            if (d >= 1) {
                strength = 1;
            }
            if (d >= 4) {
                strength = 2;
            }
            if (d >= 32) {
                strength = 3;
            }
        } else {
            if (d >= 1) {
                strength = 3;
            }
        }
    } else {
        if (blk_wh <= 8) {
            if (d >= 40) {
                strength = 1;
            }
            if (d >= 64) {
                strength = 2;
            }
        } else if (blk_wh <= 16) {
            if (d >= 20) {
                strength = 1;
            }
            if (d >= 48) {
                strength = 2;
            }
        } else if (blk_wh <= 24) {
            if (d >= 4) {
                strength = 3;
            }
        } else {
            if (d >= 1) {
                strength = 3;
            }
        }
    }
    return strength;
}

int useIntraEdgeUpsample(int bs0, int bs1, int delta, int type) {
    const int d      = std::abs(delta);
    const int blk_wh = bs0 + bs1;
    if (d <= 0 || d >= 40) {
        return 0;
    }
    return type ? (blk_wh <= 8) : (blk_wh <= 16);
}

void filterIntraEdge(std::uint8_t* p, int sz, int strength) {
    if (!strength) {
        return;
    }

    static const int kernel[3][5] = {{0, 4, 8, 4, 0}, {0, 5, 6, 5, 0}, {2, 4, 4, 4, 2}};

    const int   filt = strength - 1;
    std::uint8_t edge[129];

    for (int i = 0; i < sz; i++) {
        edge[i] = p[i];
    }
    for (int i = 1; i < sz; i++) {
        int s = 0;
        for (int j = 0; j < 5; j++) {
            int k = i - 2 + j;
            k     = (k < 0) ? 0 : k;
            k     = (k > sz - 1) ? sz - 1 : k;
            s += edge[k] * kernel[filt][j];
        }
        s      = (s + 8) >> 4;
        p[i] = (std::uint8_t)s;
    }
}

namespace {

int clipPixelHighbd(int value, int bitDepth) {
    const int maxValue = (1 << bitDepth) - 1;
    if (value < 0) {
        return 0;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

int roundPowerOfTwo(int value, int bits) {
    return (value + (1 << (bits - 1))) >> bits;
}

}  // namespace

void drZ1(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above, const std::uint8_t* left,
          int upsampleAbove, int dx, int dy) {
    (void)left;
    (void)dy;

    const int maxBaseX = ((bw + bh) - 1) << upsampleAbove;
    const int fracBits = 6 - upsampleAbove;
    const int baseInc  = 1 << upsampleAbove;
    for (int r = 0, x = dx; r < bh; ++r, dst += stride, x += dx) {
        int base     = x >> fracBits;
        const int shift = ((x << upsampleAbove) & 0x3F) >> 1;

        if (base >= maxBaseX) {
            for (int i = r; i < bh; ++i) {
                for (int c = 0; c < bw; ++c) {
                    dst[i * stride + c] = above[maxBaseX];
                }
            }
            return;
        }

        for (int c = 0; c < bw; ++c, base += baseInc) {
            if (base < maxBaseX) {
                int val;
                val    = above[base] * (32 - shift) + above[base + 1] * shift;
                val    = roundPowerOfTwo(val, 5);
                dst[c] = (std::uint8_t)clipPixelHighbd(val, 8);
            } else {
                dst[c] = above[maxBaseX];
            }
        }
    }
}

void drZ2(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above, const std::uint8_t* left,
          int upsampleAbove, int upsampleLeft, int dx, int dy) {
    const int minBaseX  = -(1 << upsampleAbove);
    const int fracBitsX = 6 - upsampleAbove;
    const int fracBitsY = 6 - upsampleLeft;
    const int baseIncX  = 1 << upsampleAbove;
    for (int r = 0, x = -dx; r < bh; ++r, x -= dx, dst += stride) {
        int val;
        int base1 = x >> fracBitsX;
        int y     = (r << 6) - dy;
        for (int c = 0; c < bw; ++c, base1 += baseIncX, y -= dy) {
            if (base1 >= minBaseX) {
                int shift1 = ((x * (1 << upsampleAbove)) & 0x3F) >> 1;
                val        = above[base1] * (32 - shift1) + above[base1 + 1] * shift1;
                val        = roundPowerOfTwo(val, 5);
            } else {
                int base2  = y >> fracBitsY;
                int shift2 = ((y * (1 << upsampleLeft)) & 0x3F) >> 1;
                val        = left[base2] * (32 - shift2) + left[base2 + 1] * shift2;
                val        = roundPowerOfTwo(val, 5);
            }
            dst[c] = (std::uint8_t)clipPixelHighbd(val, 8);
        }
    }
}

void drZ3(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above, const std::uint8_t* left,
          int upsampleLeft, int dx, int dy) {
    (void)above;
    (void)dx;

    const int maxBaseY = (bw + bh - 1) << upsampleLeft;
    const int fracBits = 6 - upsampleLeft;
    const int baseInc  = 1 << upsampleLeft;
    for (int c = 0, y = dy; c < bw; ++c, y += dy) {
        int base = y >> fracBits, shift = ((y << upsampleLeft) & 0x3F) >> 1;

        for (int r = 0; r < bh; ++r, base += baseInc) {
            if (base < maxBaseY) {
                int val;
                val                 = left[base] * (32 - shift) + left[base + 1] * shift;
                val                 = roundPowerOfTwo(val, 5);
                dst[r * stride + c] = (std::uint8_t)clipPixelHighbd(val, 8);
            } else {
                for (; r < bh; ++r) {
                    dst[r * stride + c] = left[maxBaseY];
                }
                break;
            }
        }
    }
}

namespace {

const std::uint8_t smWeightArrays[128] = {
    // Unused, because we always offset by bs, which is at least 2.
    0, 0,
    // bs = 2
    255, 128,
    // bs = 4
    255, 149, 85, 64,
    // bs = 8
    255, 197, 146, 105, 73, 50, 37, 32,
    // bs = 16
    255, 225, 196, 170, 145, 123, 102, 84, 68, 54, 43, 33, 26, 20, 17, 16,
    // bs = 32
    255, 240, 225, 210, 196, 182, 169, 157, 145, 133, 122, 111, 101, 92, 83, 74,
    66, 59, 52, 45, 39, 34, 29, 25, 21, 17, 14, 12, 10, 9, 8, 8,
    // bs = 64
    255, 248, 240, 233, 225, 218, 210, 203, 196, 189, 182, 176, 169, 163, 156,
    150, 144, 138, 133, 127, 121, 116, 111, 106, 101, 96, 91, 86, 82, 77, 73, 69,
    65, 61, 57, 54, 50, 47, 44, 41, 38, 35, 32, 29, 27, 25, 22, 20, 18, 16, 15,
    13, 12, 10, 9, 8, 7, 6, 6, 5, 5, 4, 4, 4,
};

const std::uint16_t drIntraDerivative[90] = {
    0,    0, 0,
    1023, 0, 0,
    547,  0, 0,
    372,  0, 0, 0, 0,
    273,  0, 0,
    215,  0, 0,
    178,  0, 0,
    151,  0, 0,
    132,  0, 0,
    116,  0, 0,
    102,  0, 0, 0,
    90,   0, 0,
    80,   0, 0,
    71,   0, 0,
    64,   0, 0,
    57,   0, 0,
    51,   0, 0,
    45,   0, 0, 0,
    40,   0, 0,
    35,   0, 0,
    31,   0, 0,
    27,   0, 0,
    23,   0, 0,
    19,   0, 0,
    15,   0, 0, 0, 0,
    11,   0, 0,
    7,    0, 0,
    3,    0, 0,
};

}  // namespace

int getDx(int angle) {
    if (angle > 0 && angle < 90) {
        return drIntraDerivative[angle];
    } else if (angle > 90 && angle < 180) {
        return drIntraDerivative[180 - angle];
    } else {
        return 1;
    }
}

int getDy(int angle) {
    if (angle > 90 && angle < 180) {
        return drIntraDerivative[angle - 90];
    } else if (angle > 180 && angle < 270) {
        return drIntraDerivative[270 - angle];
    } else {
        return 1;
    }
}

void smoothPredict(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above,
                   const std::uint8_t* left) {
    const std::uint8_t belowPred = left[bh - 1];
    const std::uint8_t rightPred = above[bw - 1];
    const std::uint8_t* const smWeightsW = smWeightArrays + bw;
    const std::uint8_t* const smWeightsH = smWeightArrays + bh;
    const int  log2Scale = 1 + 8;
    const int scale     = (1 << 8);
    for (int r = 0; r < bh; ++r, dst += stride) {
        for (int c = 0; c < bw; ++c) {
            const std::uint8_t pixels[] = {above[c], belowPred, left[r], rightPred};
            const std::uint8_t weights[] = {smWeightsH[r],
                                            (std::uint8_t)(scale - smWeightsH[r]),
                                            smWeightsW[c],
                                            (std::uint8_t)(scale - smWeightsW[c])};
            int thisPred = 0;
            for (int i = 0; i < 4; ++i) {
                thisPred += weights[i] * pixels[i];
            }
            dst[c] = (std::uint8_t)roundPowerOfTwo(thisPred, log2Scale);
        }
    }
}

void smoothVPredict(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above,
                    const std::uint8_t* left) {
    const std::uint8_t belowPred  = left[bh - 1];
    const std::uint8_t* const smWeights = smWeightArrays + bh;
    const int  log2Scale = 8;
    const int scale      = (1 << 8);
    for (int r = 0; r < bh; ++r, dst += stride) {
        for (int c = 0; c < bw; ++c) {
            const std::uint8_t pixels[]  = {above[c], belowPred};
            const std::uint8_t weights[] = {smWeights[r], (std::uint8_t)(scale - smWeights[r])};
            int thisPred = 0;
            for (int i = 0; i < 2; ++i) {
                thisPred += weights[i] * pixels[i];
            }
            dst[c] = (std::uint8_t)roundPowerOfTwo(thisPred, log2Scale);
        }
    }
}

void smoothHPredict(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above,
                    const std::uint8_t* left) {
    const std::uint8_t rightPred   = above[bw - 1];
    const std::uint8_t* const smWeights = smWeightArrays + bw;
    const int  log2Scale = 8;
    const int scale      = (1 << 8);
    for (int r = 0; r < bh; ++r, dst += stride) {
        for (int c = 0; c < bw; ++c) {
            const std::uint8_t pixels[]  = {left[r], rightPred};
            const std::uint8_t weights[] = {smWeights[c], (std::uint8_t)(scale - smWeights[c])};
            int thisPred = 0;
            for (int i = 0; i < 2; ++i) {
                thisPred += weights[i] * pixels[i];
            }
            dst[c] = (std::uint8_t)roundPowerOfTwo(thisPred, log2Scale);
        }
    }
}

namespace {

const int kNeedLeft       = 1 << 1;
const int kNeedAbove      = 1 << 2;
const int kNeedAboveRight = 1 << 3;
const int kNeedAboveLeft  = 1 << 4;
const int kNeedBottomLeft = 1 << 5;

const int kExtendModes[13] = {
    kNeedAbove | kNeedLeft,
    kNeedAbove,
    kNeedLeft,
    kNeedAbove | kNeedAboveRight,
    kNeedLeft | kNeedAbove | kNeedAboveLeft,
    kNeedLeft | kNeedAbove | kNeedAboveLeft,
    kNeedLeft | kNeedAbove | kNeedAboveLeft,
    kNeedLeft | kNeedBottomLeft,
    kNeedAbove | kNeedAboveRight,
    kNeedLeft | kNeedAbove,
    kNeedLeft | kNeedAbove,
    kNeedLeft | kNeedAbove,
    kNeedLeft | kNeedAbove | kNeedAboveLeft,
};

const int kModeToAngle[13] = {0, 90, 180, 45, 135, 113, 157, 203, 67, 0, 0, 0, 0};

}  // namespace

void buildIntraPredictors(std::uint8_t* dst, int dstStride, int mode, int angleDelta, int txwpx, int txhpx,
                          std::uint8_t aboveLeft, const std::uint8_t* aboveRef, int nTopPx, int nTopRightPx,
                          const std::uint8_t* leftRef, int nLeftPx, int nBottomLeftPx,
                          const NeighborContext& neighbors) {
    std::uint8_t leftData[2 * 64 + 32];
    std::uint8_t aboveData[2 * 64 + 32];
    std::uint8_t* const aboveRow = aboveData + 16;
    std::uint8_t* const leftCol  = leftData + 16;

    int needLeft       = kExtendModes[mode] & kNeedLeft;
    int needAbove      = kExtendModes[mode] & kNeedAbove;
    int needAboveLeft  = kExtendModes[mode] & kNeedAboveLeft;
    int pAngle         = 0;
    const int isDrMode = mode >= V_PRED && mode <= D67_PRED;

    if (isDrMode) {
        pAngle = kModeToAngle[mode] + angleDelta * 3;
        if (pAngle <= 90) {
            needAbove = 1;
            needLeft = 0;
            needAboveLeft = 1;
        } else if (pAngle < 180) {
            needAbove = 1;
            needLeft = 1;
            needAboveLeft = 1;
        } else {
            needAbove = 0;
            needLeft = 1;
            needAboveLeft = 1;
        }
    }

    if ((!needAbove && nLeftPx == 0) || (!needLeft && nTopPx == 0)) {
        int val;
        if (needLeft) {
            val = (nTopPx > 0) ? aboveRef[0] : 129;
        } else {
            val = (nLeftPx > 0) ? leftRef[0] : 127;
        }
        for (int i = 0; i < txhpx; ++i) {
            for (int c = 0; c < txwpx; ++c) {
                dst[i * dstStride + c] = (std::uint8_t)val;
            }
        }
        return;
    }

    int i;
    if (needLeft) {
        const int needBottom = (isDrMode && pAngle > 180) || (!!(kExtendModes[mode] & kNeedBottomLeft) && !isDrMode);
        const int numLeftPixelsNeeded = txhpx + (needBottom ? txwpx : 0);
        i = 0;
        if (nLeftPx > 0) {
            for (; i < nLeftPx; i++) {
                leftCol[i] = leftRef[i];
            }
            if (needBottom && nBottomLeftPx > 0) {
                for (; i < txhpx + nBottomLeftPx; i++) {
                    leftCol[i] = leftRef[i];
                }
            }
            if (i < numLeftPixelsNeeded) {
                for (; i < numLeftPixelsNeeded; i++) {
                    leftCol[i] = leftCol[i - 1];
                }
            }
        } else {
            if (nTopPx > 0) {
                for (int j = 0; j < numLeftPixelsNeeded; j++) {
                    leftCol[j] = aboveRef[0];
                }
            } else {
                for (int j = 0; j < numLeftPixelsNeeded; j++) {
                    leftCol[j] = 129;
                }
            }
        }
    }

    if (needAbove) {
        const int needRight = (isDrMode && pAngle < 90) || (!!(kExtendModes[mode] & kNeedAboveRight) && !isDrMode);
        const int numTopPixelsNeeded = txwpx + (needRight ? txhpx : 0);
        if (nTopPx > 0) {
            for (int j = 0; j < nTopPx; j++) {
                aboveRow[j] = aboveRef[j];
            }
            i = nTopPx;
            if (needRight && nTopRightPx > 0) {
                for (int j = 0; j < nTopRightPx; j++) {
                    aboveRow[txwpx + j] = aboveRef[txwpx + j];
                }
                i += nTopRightPx;
            }
            if (i < numTopPixelsNeeded) {
                for (; i < numTopPixelsNeeded; i++) {
                    aboveRow[i] = aboveRow[i - 1];
                }
            }
        } else {
            if (nLeftPx > 0) {
                for (int j = 0; j < numTopPixelsNeeded; j++) {
                    aboveRow[j] = leftRef[0];
                }
            } else {
                for (int j = 0; j < numTopPixelsNeeded; j++) {
                    aboveRow[j] = 127;
                }
            }
        }
    }

    if (needAboveLeft) {
        if (nTopPx > 0 && nLeftPx > 0) {
            aboveRow[-1] = aboveLeft;
        } else if (nTopPx > 0) {
            aboveRow[-1] = aboveRef[0];
        } else if (nLeftPx > 0) {
            aboveRow[-1] = leftRef[0];
        } else {
            aboveRow[-1] = 128;
        }
        leftCol[-1] = aboveRow[-1];
    }

    if (isDrMode) {
        int upsampleAbove = 0;
        int upsampleLeft  = 0;

        const int needRight  = pAngle < 90;
        const int needBottom = pAngle > 180;

        if (pAngle != 90 && pAngle != 180) {
            const int abLe = needAboveLeft ? 1 : 0;
            if (needAbove && needLeft && (txwpx + txhpx >= 24)) {
                const int s = (leftCol[0] * 5) + (aboveRow[-1] * 6) + (aboveRow[0] * 5);
                aboveRow[-1] = (std::uint8_t)((s + 8) >> 4);
                leftCol[-1]  = aboveRow[-1];
            }
            if (needAbove && nTopPx > 0) {
                const int strength = edgeFilterStrength(txwpx, txhpx, pAngle - 90, filtType(neighbors));
                const int nPx      = nTopPx + abLe + (needRight ? txhpx : 0);
                filterIntraEdge(aboveRow - abLe, nPx, strength);
            }
            if (needLeft && nLeftPx > 0) {
                const int strength = edgeFilterStrength(txhpx, txwpx, pAngle - 180, filtType(neighbors));
                const int nPx      = nLeftPx + abLe + (needBottom ? txwpx : 0);
                filterIntraEdge(leftCol - abLe, nPx, strength);
            }
        }
        upsampleAbove = useIntraEdgeUpsample(txwpx, txhpx, pAngle - 90, 0);
        if (needAbove && upsampleAbove) {
            const int nPx = txwpx + (needRight ? txhpx : 0);
            upsampleIntraEdge(aboveRow, nPx);
        }
        upsampleLeft = useIntraEdgeUpsample(txhpx, txwpx, pAngle - 180, 0);
        if (needLeft && upsampleLeft) {
            const int nPx = txhpx + (needBottom ? txwpx : 0);
            upsampleIntraEdge(leftCol, nPx);
        }
        drPredictor(dst, dstStride, txwpx, txhpx, aboveRow, leftCol, upsampleAbove, upsampleLeft, pAngle);
        return;
    }

    if (mode == DC_PRED) {
        int sum = 0;
        if (nLeftPx > 0 && nTopPx > 0) {
            const int count = txwpx + txhpx;
            for (int j = 0; j < txwpx; j++) {
                sum += aboveRow[j];
            }
            for (int j = 0; j < txhpx; j++) {
                sum += leftCol[j];
            }
            sum = (sum + (count >> 1)) / count;
        } else if (nLeftPx > 0) {
            for (int j = 0; j < txhpx; j++) {
                sum += leftCol[j];
            }
            sum = (sum + (txhpx >> 1)) / txhpx;
        } else if (nTopPx > 0) {
            for (int j = 0; j < txwpx; j++) {
                sum += aboveRow[j];
            }
            sum = (sum + (txwpx >> 1)) / txwpx;
        } else {
            sum = 128;
        }
        for (int r = 0; r < txhpx; r++) {
            for (int c = 0; c < txwpx; c++) {
                dst[r * dstStride + c] = (std::uint8_t)sum;
            }
        }
    } else if (mode == V_PRED) {
        for (int r = 0; r < txhpx; r++) {
            for (int c = 0; c < txwpx; c++) {
                dst[r * dstStride + c] = aboveRow[c];
            }
        }
    } else if (mode == H_PRED) {
        for (int r = 0; r < txhpx; r++) {
            for (int c = 0; c < txwpx; c++) {
                dst[r * dstStride + c] = leftCol[r];
            }
        }
    } else if (mode == PAETH_PRED) {
        const std::uint8_t topLeft = aboveRow[-1];
        for (int r = 0; r < txhpx; ++r) {
            for (int c = 0; c < txwpx; ++c) {
                const int base = leftCol[r] + aboveRow[c] - topLeft;
                const int pLeft = std::abs(base - leftCol[r]);
                const int pTop = std::abs(base - aboveRow[c]);
                const int pTopLeft = std::abs(base - topLeft);
                dst[r * dstStride + c] =
                    (pLeft <= pTop && pLeft <= pTopLeft) ? leftCol[r] : (pTop <= pTopLeft) ? aboveRow[c] : topLeft;
            }
        }
    } else if (mode == SMOOTH_PRED) {
        smoothPredict(dst, dstStride, txwpx, txhpx, aboveRow, leftCol);
    } else if (mode == SMOOTH_V_PRED) {
        smoothVPredict(dst, dstStride, txwpx, txhpx, aboveRow, leftCol);
    } else if (mode == SMOOTH_H_PRED) {
        smoothHPredict(dst, dstStride, txwpx, txhpx, aboveRow, leftCol);
    }
}

void drPredictor(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above, const std::uint8_t* left,
                 int upsampleAbove, int upsampleLeft, int angle) {
    const int dx = getDx(angle);
    const int dy = getDy(angle);

    if (angle > 0 && angle < 90) {
        drZ1(dst, stride, bw, bh, above, left, upsampleAbove, dx, dy);
    } else if (angle > 90 && angle < 180) {
        drZ2(dst, stride, bw, bh, above, left, upsampleAbove, upsampleLeft, dx, dy);
    } else if (angle > 180 && angle < 270) {
        drZ3(dst, stride, bw, bh, above, left, upsampleLeft, dx, dy);
    } else if (angle == 90) {
        for (int r = 0; r < bh; ++r) {
            for (int c = 0; c < bw; ++c) {
                dst[r * stride + c] = above[c];
            }
        }
    } else if (angle == 180) {
        for (int r = 0; r < bh; ++r) {
            for (int c = 0; c < bw; ++c) {
                dst[r * stride + c] = left[r];
            }
        }
    }
}

void upsampleIntraEdge(std::uint8_t* p, int sz) {
    std::uint8_t in[129 + 3];
    in[0] = p[-1];
    in[1] = p[-1];
    for (int i = 0; i < sz; i++) {
        in[i + 2] = p[i];
    }
    in[sz + 2] = p[sz - 1];

    p[-2] = in[0];
    for (int i = 0; i < sz; i++) {
        int s = -in[i] + (9 * in[i + 1]) + (9 * in[i + 2]) - in[i + 3];
        s     = (s + 8) >> 4;
        if (s < 0) {
            s = 0;
        } else if (s > 255) {
            s = 255;
        }
        p[2 * i - 1] = (std::uint8_t)s;
        p[2 * i]     = in[i + 2];
    }
}

namespace {

int log2Floor(int value) {
    int bits = 0;
    while (value > 1) {
        value >>= 1;
        ++bits;
    }
    return bits;
}

}  // namespace

std::string drZ1CuSource() {
    return R"CUDA(
extern "C" __global__ void dr_z1(const unsigned char* above, const int* dx, const int* upsampleAbove,
                                 unsigned char* dst) {
    const int idx = threadIdx.x;
    const int r = idx >> 2;
    const int c = idx & 3;
    const int maxBaseX = (4 + 4 - 1) << (*upsampleAbove);
    const int fracBits = 6 - (*upsampleAbove);
    const int baseInc = 1 << (*upsampleAbove);
    const int xr = (*dx) * (r + 1);
    int base = (xr >> fracBits) + c * baseInc;
    if (base >= maxBaseX) {
        dst[idx] = above[maxBaseX];
        return;
    }
    const int shift = ((xr << (*upsampleAbove)) & 0x3F) >> 1;
    int val = above[base] * (32 - shift) + above[base + 1] * shift;
    val = (val + 16) >> 5;
    if (val > 255) val = 255;
    dst[idx] = (unsigned char)val;
}
)CUDA";
}

std::string drZ3CuSource() {
    return R"CUDA(
extern "C" __global__ void dr_z3(const unsigned char* left, const int* dy, const int* upsampleLeft,
                                 unsigned char* dst) {
    const int idx = threadIdx.x;
    const int r = idx >> 2;
    const int c = idx & 3;
    const int maxBaseY = (4 + 4 - 1) << (*upsampleLeft);
    const int fracBits = 6 - (*upsampleLeft);
    const int baseInc = 1 << (*upsampleLeft);
    const int yc = (*dy) * (c + 1);
    int base = (yc >> fracBits) + r * baseInc;
    if (base >= maxBaseY) {
        dst[idx] = left[maxBaseY];
        return;
    }
    const int shift = ((yc << (*upsampleLeft)) & 0x3F) >> 1;
    int val = left[base] * (32 - shift) + left[base + 1] * shift;
    val = (val + 16) >> 5;
    if (val > 255) val = 255;
    dst[idx] = (unsigned char)val;
}
)CUDA";
}

std::string drZ2CuSource() {
    return R"CUDA(
extern "C" __global__ void dr_z2(const unsigned char* above, const unsigned char* left,
                                 const int* dx, const int* dy,
                                 const int* upsampleAbove, const int* upsampleLeft,
                                 unsigned char* dst) {
    const int idx = threadIdx.x;
    const int r = idx >> 2;
    const int c = idx & 3;
    const int upA = *upsampleAbove;
    const int upL = *upsampleLeft;
    const int minBaseX = -(1 << upA);
    const int fracBitsX = 6 - upA;
    const int fracBitsY = 6 - upL;
    const int baseIncX = 1 << upA;
    const int xr = -(*dx) * (r + 1);
    int base1 = (xr >> fracBitsX) + c * baseIncX;
    int val;
    if (base1 >= minBaseX) {
        const int shift1 = ((xr * (1 << upA)) & 0x3F) >> 1;
        val = above[base1] * (32 - shift1) + above[base1 + 1] * shift1;
    } else {
        const int yc = (r << 6) - (*dy) * (c + 1);
        const int base2 = yc >> fracBitsY;
        const int shift2 = ((yc * (1 << upL)) & 0x3F) >> 1;
        val = left[base2] * (32 - shift2) + left[base2 + 1] * shift2;
    }
    val = (val + 16) >> 5;
    if (val > 255) val = 255;
    dst[idx] = (unsigned char)val;
}
)CUDA";
}

std::string drPredictCuSource() {
    return R"CUDA(
extern "C" __global__ void dr_predict_4x4(const unsigned char* above, const unsigned char* left,
                                          const int* dx, const int* dy,
                                          const int* upsampleAbove, const int* upsampleLeft,
                                          const int* angle, unsigned char* dst) {
    const int idx = threadIdx.x;
    const int r = idx >> 2;
    const int c = idx & 3;
    const int upA = *upsampleAbove;
    const int upL = *upsampleLeft;

    if (*angle == 90) {
        dst[idx] = above[c];
    } else if (*angle == 180) {
        dst[idx] = left[r];
    } else if (*angle < 90) {
        const int maxBaseX = (4 + 4 - 1) << upA;
        const int fracBits = 6 - upA;
        const int baseInc = 1 << upA;
        const int xr = (*dx) * (r + 1);
        int base = (xr >> fracBits) + c * baseInc;
        if (base >= maxBaseX) {
            dst[idx] = above[maxBaseX];
            return;
        }
        const int shift = ((xr << upA) & 0x3F) >> 1;
        int val = above[base] * (32 - shift) + above[base + 1] * shift;
        val = (val + 16) >> 5;
        if (val > 255) val = 255;
        dst[idx] = (unsigned char)val;
    } else if (*angle < 180) {
        const int minBaseX = -(1 << upA);
        const int fracBitsX = 6 - upA;
        const int fracBitsY = 6 - upL;
        const int baseIncX = 1 << upA;
        const int xr = -(*dx) * (r + 1);
        int base1 = (xr >> fracBitsX) + c * baseIncX;
        int val;
        if (base1 >= minBaseX) {
            const int shift1 = ((xr * (1 << upA)) & 0x3F) >> 1;
            val = above[base1] * (32 - shift1) + above[base1 + 1] * shift1;
        } else {
            const int yc = (r << 6) - (*dy) * (c + 1);
            const int base2 = yc >> fracBitsY;
            const int shift2 = ((yc * (1 << upL)) & 0x3F) >> 1;
            val = left[base2] * (32 - shift2) + left[base2 + 1] * shift2;
        }
        val = (val + 16) >> 5;
        if (val > 255) val = 255;
        dst[idx] = (unsigned char)val;
    } else {
        const int maxBaseY = (4 + 4 - 1) << upL;
        const int fracBits = 6 - upL;
        const int baseInc = 1 << upL;
        const int yc = (*dy) * (c + 1);
        int base = (yc >> fracBits) + r * baseInc;
        if (base >= maxBaseY) {
            dst[idx] = left[maxBaseY];
            return;
        }
        const int shift = ((yc << upL) & 0x3F) >> 1;
        int val = left[base] * (32 - shift) + left[base + 1] * shift;
        val = (val + 16) >> 5;
        if (val > 255) val = 255;
        dst[idx] = (unsigned char)val;
    }
}
)CUDA";
}

std::string smoothPredictCuSourceRef() {
    return R"CUDA(
__constant__ unsigned char sm_w[128] = {
    0, 0,
    255, 128,
    255, 149, 85, 64,
    255, 197, 146, 105, 73, 50, 37, 32,
    255, 225, 196, 170, 145, 123, 102, 84, 68, 54, 43, 33, 26, 20, 17, 16,
    255, 240, 225, 210, 196, 182, 169, 157, 145, 133, 122, 111, 101, 92, 83, 74,
    66, 59, 52, 45, 39, 34, 29, 25, 21, 17, 14, 12, 10, 9, 8, 8,
    255, 248, 240, 233, 225, 218, 210, 203, 196, 189, 182, 176, 169, 163, 156,
    150, 144, 138, 133, 127, 121, 116, 111, 106, 101, 96, 91, 86, 82, 77, 73, 69,
    65, 61, 57, 54, 50, 47, 44, 41, 38, 35, 32, 29, 27, 25, 22, 20, 18, 16, 15,
    13, 12, 10, 9, 8, 7, 6, 6, 5, 5, 4, 4, 4,
};

extern "C" __global__ void smooth_4x4(const unsigned char* above, const unsigned char* left, unsigned char* dst) {
    const int idx = threadIdx.x;
    const int r = idx >> 2;
    const int c = idx & 3;
    const int belowPred = left[3];
    const int rightPred = above[3];
    const int wH = sm_w[4 + r];
    const int wW = sm_w[4 + c];
    const unsigned char pixels[4] = {above[c], (unsigned char)belowPred, left[r], (unsigned char)rightPred};
    const int weights[4] = {wH, 256 - wH, wW, 256 - wW};
    int val = 0;
    for (int i = 0; i < 4; ++i) {
        val += weights[i] * pixels[i];
    }
    val = (val + 256) >> 9;
    dst[idx] = (unsigned char)val;
}

extern "C" __global__ void smooth_v_4x4(const unsigned char* above, const unsigned char* left, unsigned char* dst) {
    const int idx = threadIdx.x;
    const int r = idx >> 2;
    const int c = idx & 3;
    const int belowPred = left[3];
    const int wH = sm_w[4 + r];
    int val = wH * above[c] + (256 - wH) * belowPred;
    val = (val + 128) >> 8;
    dst[idx] = (unsigned char)val;
}

extern "C" __global__ void smooth_h_4x4(const unsigned char* above, const unsigned char* left, unsigned char* dst) {
    const int idx = threadIdx.x;
    const int r = idx >> 2;
    const int c = idx & 3;
    const int rightPred = above[3];
    const int wW = sm_w[4 + c];
    int val = wW * left[r] + (256 - wW) * rightPred;
    val = (val + 128) >> 8;
    dst[idx] = (unsigned char)val;
}
)CUDA";
}

std::string predictBlockCuSource() {
    return R"CUDA(
__constant__ unsigned char sm_w4[4] = {255, 149, 85, 64};

__constant__ unsigned short der[90] = {
    0,    0, 0,
    1023, 0, 0,
    547,  0, 0,
    372,  0, 0, 0, 0,
    273,  0, 0,
    215,  0, 0,
    178,  0, 0,
    151,  0, 0,
    132,  0, 0,
    116,  0, 0,
    102,  0, 0, 0,
    90,   0, 0,
    80,   0, 0,
    71,   0, 0,
    64,   0, 0,
    57,   0, 0,
    51,   0, 0,
    45,   0, 0, 0,
    40,   0, 0,
    35,   0, 0,
    31,   0, 0,
    27,   0, 0,
    23,   0, 0,
    19,   0, 0,
    15,   0, 0, 0, 0,
    11,   0, 0,
    7,    0, 0,
    3,    0, 0,
};

__device__ int abs_int(int v) {
    return v < 0 ? -v : v;
}

__device__ int get_dx(int angle) {
    if (angle > 0 && angle < 90) {
        return der[angle];
    } else if (angle > 90 && angle < 180) {
        return der[180 - angle];
    }
    return 1;
}

__device__ int get_dy(int angle) {
    if (angle > 90 && angle < 180) {
        return der[angle - 90];
    } else if (angle > 180 && angle < 270) {
        return der[270 - angle];
    }
    return 1;
}

__device__ int use_up(int bs0, int bs1, int delta) {
    const int d = abs_int(delta);
    const int blkWh = bs0 + bs1;
    if (d <= 0 || d >= 40) {
        return 0;
    }
    return blkWh <= 16;
}

__device__ int filt_str(int bs0, int bs1, int delta, int type) {
    const int d = abs_int(delta);
    int strength = 0;
    const int blkWh = bs0 + bs1;
    if (type == 0) {
        if (blkWh <= 8) {
            if (d >= 56) strength = 1;
        } else if (blkWh <= 12) {
            if (d >= 40) strength = 1;
        } else if (blkWh <= 16) {
            if (d >= 40) strength = 1;
        } else if (blkWh <= 24) {
            if (d >= 8) strength = 1;
            if (d >= 16) strength = 2;
            if (d >= 32) strength = 3;
        } else if (blkWh <= 32) {
            if (d >= 1) strength = 1;
            if (d >= 4) strength = 2;
            if (d >= 32) strength = 3;
        } else {
            if (d >= 1) strength = 3;
        }
    } else {
        if (blkWh <= 8) {
            if (d >= 40) strength = 1;
            if (d >= 64) strength = 2;
        } else if (blkWh <= 16) {
            if (d >= 20) strength = 1;
            if (d >= 48) strength = 2;
        } else if (blkWh <= 24) {
            if (d >= 4) strength = 3;
        } else {
            if (d >= 1) strength = 3;
        }
    }
    return strength;
}

__device__ int edge_tap(int filt, int j) {
    if (filt == 1) {
        const int k[5] = {0, 5, 6, 5, 0};
        return k[j];
    } else if (filt == 2) {
        const int k[5] = {2, 4, 4, 4, 2};
        return k[j];
    }
    const int k[5] = {0, 4, 8, 4, 0};
    return k[j];
}

__device__ void filt_edge(unsigned char* p, int sz, int strength) {
    if (!strength) {
        return;
    }
    unsigned char edge[64];
    for (int i = 0; i < sz; ++i) {
        edge[i] = p[i];
    }
    const int filt = strength - 1;
    for (int i = 1; i < sz; ++i) {
        int s = 0;
        for (int j = 0; j < 5; ++j) {
            int k = i - 2 + j;
            if (k < 0) k = 0;
            else if (k > sz - 1) k = sz - 1;
            s += edge[k] * edge_tap(filt, j);
        }
        s = (s + 8) >> 4;
        p[i] = (unsigned char)s;
    }
}

__device__ void upsamp(unsigned char* p, int sz) {
    unsigned char in[24];
    in[0] = p[-1];
    in[1] = p[-1];
    for (int i = 0; i < sz; ++i) {
        in[i + 2] = p[i];
    }
    in[sz + 2] = p[sz - 1];
    p[-2] = in[0];
    for (int i = 0; i < sz; ++i) {
        int s = -(int)in[i] + 9 * in[i + 1] + 9 * in[i + 2] - in[i + 3];
        s = (s + 8) >> 4;
        if (s < 0) s = 0;
        else if (s > 255) s = 255;
        p[2 * i - 1] = (unsigned char)s;
        p[2 * i] = in[i + 2];
    }
}

extern "C" __global__ void predict_block_4x4(
    const int* mode, const int* angleDelta,
    const int* aboveMode, const int* leftMode,
    const unsigned char* aboveRef, const int* nTopPx, const int* nTopRightPx,
    const unsigned char* leftRef, const int* nLeftPx, const int* nBottomLeftPx,
    const int* aboveLeft, unsigned char* dst) {
    __shared__ unsigned char aboveData[64];
    __shared__ unsigned char leftData[64];
    __shared__ int kDc;
    __shared__ int kAngle;
    __shared__ int kUpA;
    __shared__ int kUpL;

    const int idx = threadIdx.x;
    unsigned char* aboveRow = aboveData + 8;
    unsigned char* leftCol = leftData + 8;

    if (idx == 0) {
        int m = *mode;
        int needLeft = 0;
        int needAbove = 0;
        int needAboveLeft = 0;
        int pAngle = 0;
        int isDr = (m >= 3 && m <= 8) ? 1 : 0;
        if (m == 0) {
            needLeft = 1;
            needAbove = 1;
        } else if (m == 1) {
            needAbove = 1;
        } else if (m == 2) {
            needLeft = 1;
        } else if (m == 12) {
            needLeft = 1;
            needAbove = 1;
            needAboveLeft = 1;
        } else if (m == 9 || m == 10 || m == 11) {
            needLeft = 1;
            needAbove = 1;
        }
        if (isDr) {
            if (m == 3) pAngle = 45;
            else if (m == 4) pAngle = 135;
            else if (m == 5) pAngle = 113;
            else if (m == 6) pAngle = 157;
            else if (m == 7) pAngle = 203;
            else pAngle = 67;
            pAngle += (*angleDelta) * 3;
            if (pAngle <= 90) {
                needAbove = 1;
                needLeft = 0;
                needAboveLeft = 1;
            } else if (pAngle < 180) {
                needAbove = 1;
                needLeft = 1;
                needAboveLeft = 1;
            } else {
                needAbove = 0;
                needLeft = 1;
                needAboveLeft = 1;
            }
        }
        const int needRight = isDr ? (pAngle < 90) : 0;
        const int needBottom = isDr ? (pAngle > 180) : 0;
        int i;
        if (needLeft) {
            const int numLeft = 4 + (needBottom ? 4 : 0);
            if (*nLeftPx > 0) {
                for (i = 0; i < *nLeftPx; ++i) {
                    leftCol[i] = leftRef[i];
                }
                if (needBottom && *nBottomLeftPx > 0) {
                    for (; i < 4 + *nBottomLeftPx; ++i) {
                        leftCol[i] = leftRef[i];
                    }
                }
                for (; i < numLeft; ++i) {
                    leftCol[i] = leftCol[i - 1];
                }
            } else {
                const unsigned char v = (*nTopPx > 0) ? aboveRef[0] : 129;
                for (i = 0; i < numLeft; ++i) {
                    leftCol[i] = v;
                }
            }
        }
        if (needAbove) {
            const int numTop = 4 + (needRight ? 4 : 0);
            if (*nTopPx > 0) {
                for (i = 0; i < *nTopPx; ++i) {
                    aboveRow[i] = aboveRef[i];
                }
                if (needRight && *nTopRightPx > 0) {
                    for (i = 4; i < 4 + *nTopRightPx; ++i) {
                        aboveRow[i] = aboveRef[i];
                    }
                }
                for (; i < numTop; ++i) {
                    aboveRow[i] = aboveRow[i - 1];
                }
            } else {
                const unsigned char v = (*nLeftPx > 0) ? leftRef[0] : 127;
                for (i = 0; i < numTop; ++i) {
                    aboveRow[i] = v;
                }
            }
        }
        if (needAboveLeft) {
            aboveRow[-1] = (unsigned char)(*aboveLeft);
            leftCol[-1] = aboveRow[-1];
        }
        if (isDr) {
            int upAbove = 0;
            int upLeft  = 0;
            const int kFiltType =
                (((*aboveMode >= 9 && *aboveMode <= 11) || (*leftMode >= 9 && *leftMode <= 11)) ? 1 : 0);
            const int abLe = needAboveLeft ? 1 : 0;
            if (needAbove && *nTopPx > 0) {
                const int strength = filt_str(4, 4, pAngle - 90, kFiltType);
                const int nPx      = *nTopPx + abLe + (needRight ? 4 : 0);
                filt_edge(aboveRow - abLe, nPx, strength);
            }
            if (needLeft && *nLeftPx > 0) {
                const int strength = filt_str(4, 4, pAngle - 180, kFiltType);
                const int nPx      = *nLeftPx + abLe + (needBottom ? 4 : 0);
                filt_edge(leftCol - abLe, nPx, strength);
            }
            upAbove = use_up(4, 4, pAngle - 90);
            if (needAbove && upAbove) {
                upsamp(aboveRow, 4 + (needRight ? 4 : 0));
            }
            upLeft = use_up(4, 4, pAngle - 180);
            if (needLeft && upLeft) {
                upsamp(leftCol, 4 + (needBottom ? 4 : 0));
            }
            kAngle = pAngle;
            kUpA = upAbove;
            kUpL = upLeft;
        }
        if (m == 0) {
            int sum = 0;
            if (*nLeftPx > 0 && *nTopPx > 0) {
                int count = 8;
                for (i = 0; i < 4; ++i) {
                    sum += aboveRow[i];
                }
                for (i = 0; i < 4; ++i) {
                    sum += leftCol[i];
                }
                kDc = (sum + (count >> 1)) / count;
            } else if (*nLeftPx > 0) {
                for (i = 0; i < 4; ++i) {
                    sum += leftCol[i];
                }
                kDc = (sum + (4 >> 1)) / 4;
            } else if (*nTopPx > 0) {
                for (i = 0; i < 4; ++i) {
                    sum += aboveRow[i];
                }
                kDc = (sum + (4 >> 1)) / 4;
            } else {
                kDc = 128;
            }
        }
    }
    __syncthreads();
    int m = *mode;
    if (m == 0) {
        dst[idx] = (unsigned char)kDc;
    } else if (m == 1) {
        dst[idx] = aboveRow[idx & 3];
    } else if (m == 2) {
        dst[idx] = leftCol[idx >> 2];
    } else if (m == 12) {
        const int r = idx >> 2;
        const int c = idx & 3;
        const int base = leftCol[r] + aboveRow[c] - aboveRow[-1];
        const int pLeft = (base > leftCol[r]) ? base - leftCol[r] : leftCol[r] - base;
        const int pTop = (base > aboveRow[c]) ? base - aboveRow[c] : aboveRow[c] - base;
        const int pTopLeft = (base > aboveRow[-1]) ? base - aboveRow[-1] : aboveRow[-1] - base;
        dst[idx] = (pLeft <= pTop && pLeft <= pTopLeft) ? leftCol[r]
                   : (pTop <= pTopLeft) ? aboveRow[c] : aboveRow[-1];
    } else if (m == 9 || m == 10 || m == 11) {
        const int r = idx >> 2;
        const int c = idx & 3;
        const int below = leftCol[3];
        const int right = aboveRow[3];
        if (m == 9) {
            const int wH = sm_w4[r];
            const int wW = sm_w4[c];
            int val = wH * aboveRow[c] + (256 - wH) * below + wW * leftCol[r] + (256 - wW) * right;
            val = (val + 256) >> 9;
            dst[idx] = (unsigned char)val;
        } else if (m == 10) {
            const int wH = sm_w4[r];
            int val = wH * aboveRow[c] + (256 - wH) * below;
            val = (val + 128) >> 8;
            dst[idx] = (unsigned char)val;
        } else {
            const int wW = sm_w4[c];
            int val = wW * leftCol[r] + (256 - wW) * right;
            val = (val + 128) >> 8;
            dst[idx] = (unsigned char)val;
        }
    } else if (m >= 3 && m <= 8) {
        const int r = idx >> 2;
        const int c = idx & 3;
        const int angle = kAngle;
        const int upA = kUpA;
        const int upL = kUpL;
        const int dx = get_dx(angle);
        const int dy = get_dy(angle);
        if (angle == 90) {
            dst[idx] = aboveRow[c];
        } else if (angle == 180) {
            dst[idx] = leftCol[r];
        } else if (angle < 90) {
            const int maxBaseX = 7 << upA;
            const int fracBits = 6 - upA;
            const int baseInc = 1 << upA;
            const int xr = dx * (r + 1);
            int base = (xr >> fracBits) + c * baseInc;
            if (base >= maxBaseX) {
                dst[idx] = aboveRow[maxBaseX];
            } else {
                const int shift = ((xr << upA) & 0x3F) >> 1;
                int val = aboveRow[base] * (32 - shift) + aboveRow[base + 1] * shift;
                val = (val + 16) >> 5;
                if (val > 255) val = 255;
                dst[idx] = (unsigned char)val;
            }
        } else if (angle < 180) {
            const int minBaseX = -(1 << upA);
            const int fracBitsX = 6 - upA;
            const int fracBitsY = 6 - upL;
            const int baseIncX = 1 << upA;
            const int xr = -dx * (r + 1);
            int base1 = (xr >> fracBitsX) + c * baseIncX;
            int val;
            if (base1 >= minBaseX) {
                const int shift1 = ((xr * (1 << upA)) & 0x3F) >> 1;
                val = aboveRow[base1] * (32 - shift1) + aboveRow[base1 + 1] * shift1;
            } else {
                const int yc = (r << 6) - dy * (c + 1);
                const int base2 = yc >> fracBitsY;
                const int shift2 = ((yc * (1 << upL)) & 0x3F) >> 1;
                val = leftCol[base2] * (32 - shift2) + leftCol[base2 + 1] * shift2;
            }
            val = (val + 16) >> 5;
            if (val > 255) val = 255;
            dst[idx] = (unsigned char)val;
        } else {
            const int maxBaseY = 7 << upL;
            const int fracBits = 6 - upL;
            const int baseInc = 1 << upL;
            const int yc = dy * (c + 1);
            int base = (yc >> fracBits) + r * baseInc;
            if (base >= maxBaseY) {
                dst[idx] = leftCol[maxBaseY];
            } else {
                const int shift = ((yc << upL) & 0x3F) >> 1;
                int val = leftCol[base] * (32 - shift) + leftCol[base + 1] * shift;
                val = (val + 16) >> 5;
                if (val > 255) val = 255;
                dst[idx] = (unsigned char)val;
            }
        }
    }
}
)CUDA";
}

}  // namespace intra