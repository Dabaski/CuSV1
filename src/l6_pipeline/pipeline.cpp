#include "pipeline.h"

#include <string>

namespace pipeline {

namespace {

void predictResidualTxfm(const pixels::Plane& plane, int px, int py, const std::uint8_t* aboveRef, int nTopPx,
                         int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx, int nBottomLeftPx,
                         std::uint8_t aboveLeft, intra::PredictionMode mode, int angleDelta,
                         transforms::TxType txType, std::int32_t coeffs[16], std::uint8_t pred[16]) {
    intra::buildIntraPredictors(pred, 4, mode, angleDelta, 4, 4, aboveLeft, aboveRef, nTopPx, nTopRightPx,
                                leftRef, nLeftPx, nBottomLeftPx);

    std::int16_t residual[16] = {0};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            residual[y * 4 + x] = static_cast<std::int16_t>(plane.at(px + x, py + y) - pred[y * 4 + x]);
        }
    }

    transforms::fwdTxfm2d4x4(residual, coeffs, 4, txType);
}

}  // namespace

std::string subtractCuSource() {
    return R"CUDA(
extern "C" __global__ void subtract_4x4_plane(const unsigned char* src, const int* srcStride, const int* px,
                                              const int* py, const unsigned char* pred, short* residual) {
    const int idx = threadIdx.x;
    const int r = idx >> 2;
    const int c = idx & 3;
    residual[idx] = (short)(src[(*py + r) * (*srcStride) + (*px + c)] - pred[idx]);
}
)CUDA";
}

void encodeBlock4x4(const pixels::Plane& plane, int px, int py, const std::uint8_t* aboveRef, int nTopPx,
                    int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx, int nBottomLeftPx,
                    std::uint8_t aboveLeft, intra::PredictionMode mode, int angleDelta,
                    transforms::TxType txType, std::int32_t coeffs[16]) {
    std::uint8_t pred[16] = {0};
    predictResidualTxfm(plane, px, py, aboveRef, nTopPx, nTopRightPx, leftRef, nLeftPx, nBottomLeftPx,
                        aboveLeft, mode, angleDelta, txType, coeffs, pred);
}

void encodeRecon4x4(const pixels::Plane& plane, int px, int py, const std::uint8_t* aboveRef, int nTopPx,
                    int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx, int nBottomLeftPx,
                    std::uint8_t aboveLeft, intra::PredictionMode mode, int angleDelta,
                    transforms::TxType txType, std::int32_t coeffs[16], std::uint8_t recon[16],
                    const intra::NeighborContext& neighbors, bool disableEdgeFilter) {
    std::uint8_t pred[16] = {0};
    intra::buildIntraPredictors(pred, 4, mode, angleDelta, 4, 4, aboveLeft, aboveRef, nTopPx, nTopRightPx,
                                leftRef, nLeftPx, nBottomLeftPx, neighbors, -1, disableEdgeFilter);

    std::int16_t residual[16] = {0};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            residual[y * 4 + x] = static_cast<std::int16_t>(plane.at(px + x, py + y) - pred[y * 4 + x]);
        }
    }

    transforms::fwdTxfm2d4x4(residual, coeffs, 4, txType);
    for (int i = 0; i < 16; ++i) {
        recon[i] = pred[i];
    }
    transforms::invTxfm2dAdd4x4(coeffs, recon, 4, txType);
}

void encodeFrameRecon4x4(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                         intra::PredictionMode mode, int angleDelta, transforms::TxType txType) {
    const int gridW = src.width() / 4;
    const int gridH = src.height() / 4;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 4;
            const int py = by * 4;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 4 : 0;
            const int nLeftPx = hasLeft ? 4 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 4 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[4] = {0};
            std::uint8_t left[4] = {0};
            if (hasTop) {
                for (int i = 0; i < 4; ++i) {
                    above[i] = recon.at(px + i, py - 1);
                }
            }
            if (hasLeft) {
                for (int i = 0; i < 4; ++i) {
                    left[i] = recon.at(px - 1, py + i);
                }
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[16] = {0};
            intra::buildIntraPredictors(pred, 4, mode, angleDelta, 4, 4, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[16] = {0};
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    residual[y * 4 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 4 + x]);
                }
            }
            std::int32_t cb[16] = {0};
            transforms::fwdTxfm2d4x4(residual, cb, 4, txType);
            for (int i = 0; i < 16; ++i) {
                coeffs[(by * gridW + bx) * 16 + i] = cb[i];
            }

            std::uint8_t tmp[16] = {0};
            for (int i = 0; i < 16; ++i) {
                tmp[i] = pred[i];
            }
            transforms::invTxfm2dAdd4x4(cb, tmp, 4, txType);
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    recon.at(px + x, py + y) = tmp[y * 4 + x];
                }
            }
        }
    }
}

std::int64_t frameMse8(const pixels::Plane& a, const pixels::Plane& b) {
    std::int64_t sse = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const std::int64_t d = static_cast<std::int64_t>(a.at(x, y)) - static_cast<std::int64_t>(b.at(x, y));
            sse += d * d;
        }
    }
    return sse;
}

// D2 policy (ours): all 13 PredictionModes, SAD-scored, lowest wins,
// tie-break = lowest mode index. Primitives are 1:1 SVT.
ModeDecision decideBlockMode4x4(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                int nBottomLeftPx, std::uint8_t aboveLeft,
                                const intra::NeighborContext& neighbors) {
    ModeDecision best{intra::DC_PRED, 0};
    bool haveBest = false;
    for (int m = intra::DC_PRED; m <= intra::PAETH_PRED; ++m) {
        std::uint8_t pred[16] = {0};
        intra::buildIntraPredictors(pred, 4, m, 0, 4, 4, aboveLeft, aboveRef, nTopPx, nTopRightPx,
                                    leftRef, nLeftPx, nBottomLeftPx, neighbors);
        const std::uint32_t sad = motion::sad4x4(src, 4, pred, 4);
        if (!haveBest || sad < best.sad) {
            best = ModeDecision{static_cast<intra::PredictionMode>(m), sad};
            haveBest = true;
        }
    }
    return best;
}

void encodeFrameAuto4x4(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                        std::uint8_t* modes, transforms::TxType txType) {
    const int gridW = src.width() / 4;
    const int gridH = src.height() / 4;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 4;
            const int py = by * 4;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 4 : 0;
            const int nLeftPx = hasLeft ? 4 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 4 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[4] = {0};
            std::uint8_t left[4] = {0};
            if (hasTop) {
                for (int i = 0; i < 4; ++i) {
                    above[i] = recon.at(px + i, py - 1);
                }
            }
            if (hasLeft) {
                for (int i = 0; i < 4; ++i) {
                    left[i] = recon.at(px - 1, py + i);
                }
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            // chosen neighbor modes feed filt_type (live at frame level);
            // unknown neighbors default to DC_PRED
            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[16] = {0};
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    srcBlk[y * 4 + x] = src.at(px + x, py + y);
                }
            }

            const ModeDecision d = decideBlockMode4x4(srcBlk, above, nTopPx, nTopRightPx, left,
                                                      nLeftPx, nBottomLeftPx, aboveLeft, nctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::int32_t cb[16] = {0};
            std::uint8_t blkRecon[16] = {0};
            encodeRecon4x4(src, px, py, above, nTopPx, nTopRightPx, left, nLeftPx, nBottomLeftPx,
                           aboveLeft, d.mode, 0, txType, cb, blkRecon, nctx);
            for (int i = 0; i < 16; ++i) {
                coeffs[(by * gridW + bx) * 16 + i] = cb[i];
                recon.at(px + (i & 3), py + (i >> 2)) = blkRecon[i];
            }
        }
    }
}

}  // namespace pipeline