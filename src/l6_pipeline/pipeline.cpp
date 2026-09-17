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

extern "C" __global__ void subtract_8x8_plane(const unsigned char* src, const int* srcStride, const int* px,
                                              const int* py, const unsigned char* pred, short* residual) {
    const int idx = threadIdx.x;
    const int r = idx >> 3;
    const int c = idx & 7;
    residual[idx] = (short)(src[(*py + r) * (*srcStride) + (*px + c)] - pred[idx]);
}

extern "C" __global__ void subtract_16x16_plane(const unsigned char* src, const int* srcStride, const int* px,
                                                const int* py, const unsigned char* pred, short* residual) {
    const int idx = threadIdx.x;
    const int r = idx >> 4;
    const int c = idx & 15;
    residual[idx] = (short)(src[(*py + r) * (*srcStride) + (*px + c)] - pred[idx]);
}

extern "C" __global__ void subtract_32x32_plane(const unsigned char* src, const int* srcStride, const int* px,
                                                const int* py, const unsigned char* pred, short* residual) {
    const int idx = threadIdx.x;
    const int r = idx >> 5;
    const int c = idx & 31;
    residual[idx] = (short)(src[(*py + r) * (*srcStride) + (*px + c)] - pred[idx]);
}

// L9: 4096 pixels > 1024-thread max -> 4 pixels per thread (p = idx + 1024k)
extern "C" __global__ void subtract_64x64_plane(const unsigned char* src, const int* srcStride, const int* px,
                                                const int* py, const unsigned char* pred, short* residual) {
    const int idx = threadIdx.x;
    for (int k = 0; k < 4; ++k) {
        const int p = idx + 1024 * k;
        const int r = p >> 6;
        const int c = p & 63;
        residual[p] = (short)(src[(*py + r) * (*srcStride) + (*px + c)] - pred[p]);
    }
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

            std::uint8_t above[8] = {0};  // 2*B: above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[4] = {0};
            if (hasTop) {
                for (int i = 0; i < 4 + nTopRightPx; ++i) {
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

            std::uint8_t above[8] = {0};  // 2*B: above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[4] = {0};
            if (hasTop) {
                for (int i = 0; i < 4 + nTopRightPx; ++i) {
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

// D2 policy generalized to 8x8: all 13 PredictionModes, sad8x8-scored
// (bit-exact with svt_nxm_sad_kernel_helper_c at 8x8), lowest wins,
// tie-break = lowest mode index. Primitives are 1:1 SVT.
ModeDecision decideBlockMode8x8(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                int nBottomLeftPx, std::uint8_t aboveLeft,
                                const intra::NeighborContext& neighbors) {
    ModeDecision best{intra::DC_PRED, 0};
    bool haveBest = false;
    for (int m = intra::DC_PRED; m <= intra::PAETH_PRED; ++m) {
        std::uint8_t pred[64] = {0};
        intra::buildIntraPredictors(pred, 8, m, 0, 8, 8, aboveLeft, aboveRef, nTopPx, nTopRightPx,
                                    leftRef, nLeftPx, nBottomLeftPx, neighbors);
        const std::uint32_t sad = motion::sad8x8(src, 8, pred, 8);
        if (!haveBest || sad < best.sad) {
            best = ModeDecision{static_cast<intra::PredictionMode>(m), sad};
            haveBest = true;
        }
    }
    return best;
}

void encodeFrameRecon8x8(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                         intra::PredictionMode mode, int angleDelta, transforms::TxType txType) {
    const int gridW = src.width() / 8;
    const int gridH = src.height() / 8;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 8;
            const int py = by * 8;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 8 : 0;
            const int nLeftPx = hasLeft ? 8 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 8 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[16] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[16] = {0};
            if (hasTop) {
                for (int i = 0; i < 8 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 8; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[64] = {0};
            intra::buildIntraPredictors(pred, 8, mode, angleDelta, 8, 8, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[64] = {0};
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    residual[y * 8 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 8 + x]);

            std::int32_t cb[64] = {0};
            transforms::fwdTxfm2d8x8(residual, cb, 8, txType);
            for (int i = 0; i < 64; ++i) coeffs[(by * gridW + bx) * 64 + i] = cb[i];

            std::uint8_t tmp[64] = {0};
            for (int i = 0; i < 64; ++i) tmp[i] = pred[i];
            transforms::invTxfm2dAdd8x8(cb, tmp, 8, txType);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) recon.at(px + x, py + y) = tmp[y * 8 + x];
        }
    }
}

void encodeFrameAuto8x8(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                        std::uint8_t* modes, transforms::TxType txType) {
    const int gridW = src.width() / 8;
    const int gridH = src.height() / 8;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 8;
            const int py = by * 8;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 8 : 0;
            const int nLeftPx = hasLeft ? 8 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 8 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[16] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[16] = {0};
            if (hasTop) {
                for (int i = 0; i < 8 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 8; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[64] = {0};
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) srcBlk[y * 8 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode8x8(srcBlk, above, nTopPx, nTopRightPx, left,
                                                      nLeftPx, nBottomLeftPx, aboveLeft, nctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[64] = {0};
            intra::buildIntraPredictors(pred, 8, d.mode, 0, 8, 8, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);

            std::int16_t residual[64] = {0};
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    residual[y * 8 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 8 + x]);

            std::int32_t cb[64] = {0};
            transforms::fwdTxfm2d8x8(residual, cb, 8, txType);
            for (int i = 0; i < 64; ++i) coeffs[(by * gridW + bx) * 64 + i] = cb[i];

            std::uint8_t reconBlk[64] = {0};
            for (int i = 0; i < 64; ++i) reconBlk[i] = pred[i];
            transforms::invTxfm2dAdd8x8(cb, reconBlk, 8, txType);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) recon.at(px + x, py + y) = reconBlk[y * 8 + x];
        }
    }
}

// Q2: D3 loop with the FP quantizer wired at a fixed qindex
// (quantize_fp_helper_c at log_scale 0). qcoeff = coded coeffs; dqcoeff
// feeds the inverse, so recon carries real quantization loss.
// SCAN POLICY IS OURS: this run uses the fixed default scan (defaultScan4x4,
// the svt_aom_init_iscan up-right diagonal) for every block; SVT selects the
// scan per mode/tx type via get_scan_order (coefficients.h:40). Fixing the
// scan is a scope decision, attributed to no one else.
void encodeFrameAuto4x4Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                         std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType) {
    const int gridW = src.width() / 4;
    const int gridH = src.height() / 4;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[16];
    transforms::defaultScan4x4(scan);
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

            std::uint8_t above[8] = {0};  // 2*B: above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[4] = {0};
            if (hasTop) {
                for (int i = 0; i < 4 + nTopRightPx; ++i) {
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

            // predict -> residual -> fwd -> quantize fp -> inverse on dqcoeff
            std::uint8_t pred[16] = {0};
            intra::buildIntraPredictors(pred, 4, d.mode, 0, 4, 4, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);
            std::int16_t residual[16] = {0};
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    residual[y * 4 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 4 + x]);
                }
            }
            std::int32_t cb[16] = {0};
            transforms::fwdTxfm2d4x4(residual, cb, 4, txType);
            std::int32_t qc[16] = {0};
            std::int32_t dq[16] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp4x4(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 16; ++i) {
                coeffs[(by * gridW + bx) * 16 + i] = qc[i];
            }
            std::uint8_t blk[16] = {0};
            for (int i = 0; i < 16; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd4x4(dq, blk, 4, txType);
            for (int i = 0; i < 16; ++i) {
                recon.at(px + (i & 3), py + (i >> 2)) = blk[i];
            }
        }
    }
}

// QW2: 8x8 quantized compositions. Both wire quantizeFp8x8 (n_coeffs=64,
// log_scale 0) after fwdTxfm2d8x8; qcoeff = coded coeffs; dqcoeff feeds
// invTxfm2dAdd8x8 onto the raw predictor, so recon carries real
// quantization loss. SCAN POLICY IS OURS: fixed defaultScan8x8 for every
// block; SVT selects per mode/tx type via get_scan_order (coefficients.h:40).
void encodeFrameRecon8x8Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                          intra::PredictionMode mode, int angleDelta, std::int32_t qindex,
                          transforms::TxType txType) {
    const int gridW = src.width() / 8;
    const int gridH = src.height() / 8;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[64];
    transforms::defaultScan8x8(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 8;
            const int py = by * 8;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 8 : 0;
            const int nLeftPx = hasLeft ? 8 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 8 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[16] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[16] = {0};
            if (hasTop) {
                for (int i = 0; i < 8 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 8; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[64] = {0};
            intra::buildIntraPredictors(pred, 8, mode, angleDelta, 8, 8, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[64] = {0};
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    residual[y * 8 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 8 + x]);

            std::int32_t cb[64] = {0};
            transforms::fwdTxfm2d8x8(residual, cb, 8, txType);
            std::int32_t qc[64] = {0};
            std::int32_t dq[64] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp8x8(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 64; ++i) coeffs[(by * gridW + bx) * 64 + i] = qc[i];

            std::uint8_t blk[64] = {0};
            for (int i = 0; i < 64; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd8x8(dq, blk, 8, txType);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) recon.at(px + x, py + y) = blk[y * 8 + x];
        }
    }
}

void encodeFrameAuto8x8Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                         std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType) {
    const int gridW = src.width() / 8;
    const int gridH = src.height() / 8;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[64];
    transforms::defaultScan8x8(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 8;
            const int py = by * 8;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 8 : 0;
            const int nLeftPx = hasLeft ? 8 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 8 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[16] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[16] = {0};
            if (hasTop) {
                for (int i = 0; i < 8 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 8; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[64] = {0};
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) srcBlk[y * 8 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode8x8(srcBlk, above, nTopPx, nTopRightPx, left,
                                                      nLeftPx, nBottomLeftPx, aboveLeft, nctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[64] = {0};
            intra::buildIntraPredictors(pred, 8, d.mode, 0, 8, 8, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);
            std::int16_t residual[64] = {0};
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    residual[y * 8 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 8 + x]);

            std::int32_t cb[64] = {0};
            transforms::fwdTxfm2d8x8(residual, cb, 8, txType);
            std::int32_t qc[64] = {0};
            std::int32_t dq[64] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp8x8(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 64; ++i) coeffs[(by * gridW + bx) * 64 + i] = qc[i];

            std::uint8_t blk[64] = {0};
            for (int i = 0; i < 64; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd8x8(dq, blk, 8, txType);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) recon.at(px + x, py + y) = blk[y * 8 + x];
        }
    }
}

// C7 policy (16x16): all 13 PredictionModes, sad16x16-scored
// (bit-exact with svt_nxm_sad_kernel_helper_c at 16x16), lowest wins,
// tie-break = lowest mode index. Primitives are 1:1 SVT.
ModeDecision decideBlockMode16x16(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                  int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                  int nBottomLeftPx, std::uint8_t aboveLeft,
                                  const intra::NeighborContext& neighbors) {
    ModeDecision best{intra::DC_PRED, 0};
    bool haveBest = false;
    for (int m = intra::DC_PRED; m <= intra::PAETH_PRED; ++m) {
        std::uint8_t pred[256] = {0};
        intra::buildIntraPredictors(pred, 16, m, 0, 16, 16, aboveLeft, aboveRef, nTopPx, nTopRightPx,
                                    leftRef, nLeftPx, nBottomLeftPx, neighbors);
        const std::uint32_t sad = motion::sad16x16(src, 16, pred, 16);
        if (!haveBest || sad < best.sad) {
            best = ModeDecision{static_cast<intra::PredictionMode>(m), sad};
            haveBest = true;
        }
    }
    return best;
}

// L5 policy (32x32): all 13 PredictionModes, sad32x32-scored
// (bit-exact with svt_nxm_sad_kernel_helper_c at 32x32), lowest wins,
// tie-break = lowest mode index. Primitives are 1:1 SVT.
ModeDecision decideBlockMode32x32(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                  int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                  int nBottomLeftPx, std::uint8_t aboveLeft,
                                  const intra::NeighborContext& neighbors) {
    ModeDecision best{intra::DC_PRED, 0};
    bool haveBest = false;
    for (int m = intra::DC_PRED; m <= intra::PAETH_PRED; ++m) {
        std::uint8_t pred[1024] = {0};
        intra::buildIntraPredictors(pred, 32, m, 0, 32, 32, aboveLeft, aboveRef, nTopPx, nTopRightPx,
                                    leftRef, nLeftPx, nBottomLeftPx, neighbors);
        const std::uint32_t sad = motion::sad32x32(src, 32, pred, 32);
        if (!haveBest || sad < best.sad) {
            best = ModeDecision{static_cast<intra::PredictionMode>(m), sad};
            haveBest = true;
        }
    }
    return best;
}

// L9 policy (64x64): all 13 PredictionModes, sad64x64-scored
// (bit-exact with svt_nxm_sad_kernel_helper_c at 64x64), lowest wins,
// tie-break = lowest mode index. Primitives are 1:1 SVT.
ModeDecision decideBlockMode64x64(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                  int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                  int nBottomLeftPx, std::uint8_t aboveLeft,
                                  const intra::NeighborContext& neighbors) {
    ModeDecision best{intra::DC_PRED, 0};
    bool haveBest = false;
    for (int m = intra::DC_PRED; m <= intra::PAETH_PRED; ++m) {
        std::uint8_t pred[4096] = {0};
        intra::buildIntraPredictors(pred, 64, m, 0, 64, 64, aboveLeft, aboveRef, nTopPx, nTopRightPx,
                                    leftRef, nLeftPx, nBottomLeftPx, neighbors);
        const std::uint32_t sad = motion::sad64x64(src, 64, pred, 64);
        if (!haveBest || sad < best.sad) {
            best = ModeDecision{static_cast<intra::PredictionMode>(m), sad};
            haveBest = true;
        }
    }
    return best;
}

// ---- C7: 16x16 frame compositions. M1 availability + REAL recon top-right
// gather (FR-series); 16x16 roundtrip is exact so recon == source.
void encodeFrameRecon16x16(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           intra::PredictionMode mode, int angleDelta, transforms::TxType txType) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictors(pred, 16, mode, angleDelta, 16, 16, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = cb[i];

            std::uint8_t tmp[256] = {0};
            for (int i = 0; i < 256; ++i) tmp[i] = pred[i];
            transforms::invTxfm2dAdd16x16(cb, tmp, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = tmp[y * 16 + x];
        }
    }
}

void encodeFrameAuto16x16(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                          std::uint8_t* modes, transforms::TxType txType, entropy::AomWriter* w,
                          entropy::EcFrameContext* fc) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    // BSF1 symbol emission (D5): kf y mode + angle delta + FI flag=0 only.
    if (w && fc) {
        entropy::initDefaultEcFrameContext(fc);
        entropy::odEcEncReset(&w->ec);
        w->allow_update_cdf = 1;  // THE COUPLING (BSF4-fix): SVT sets ec_writer.allow_update_cdf = !disable_cdf_update (ec_process.c:101). This 1 is only correct because the ratified structural config carries disable_cdf_update = 0 (resource_coordination_process.c:360). ANY future config with disable_cdf_update = 1 (should_disable_cdf_update, enc_mode_config.c:9559 - future work) must flip this emission with it, or the stream is unspecifiable: the writer adapts CDFs a conformant decoder (allow_update_cdf = 0) will not replay.
        w->pos = 0;
    }
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) srcBlk[y * 16 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode16x16(srcBlk, above, nTopPx, nTopRightPx, left,
                                                        nLeftPx, nBottomLeftPx, aboveLeft, nctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            if (w && fc) {
                int topCtx = 0, leftCtx = 0;
                entropy::getKfYModeCtx(hasLeft ? 1 : 0, static_cast<int>(nctx.leftMode),
                                       hasTop ? 1 : 0, static_cast<int>(nctx.aboveMode), &topCtx, &leftCtx);
                entropy::writeKfLumaMode(w, fc, entropy::BLOCK_16X16,
                                         static_cast<entropy::PredictionMode>(d.mode), topCtx, leftCtx, 0);
                if (entropy::filterIntraAllowed(1, entropy::BLOCK_16X16, 0,
                                                static_cast<std::uint32_t>(d.mode))) {
                    entropy::writeFilterIntra(w, fc, entropy::BLOCK_16X16, entropy::FILTER_INTRA_MODES);
                }
            }

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictors(pred, 16, d.mode, 0, 16, 16, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);

            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = cb[i];

            std::uint8_t reconBlk[256] = {0};
            for (int i = 0; i < 256; ++i) reconBlk[i] = pred[i];
            transforms::invTxfm2dAdd16x16(cb, reconBlk, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = reconBlk[y * 16 + x];
        }
    }
    if (w) entropy::odEcStopEncode(w);
}

void encodeFrameRecon16x16Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                            intra::PredictionMode mode, int angleDelta, std::int32_t qindex,
                            transforms::TxType txType) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[256];
    transforms::defaultScan16x16(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictors(pred, 16, mode, angleDelta, 16, 16, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            std::int32_t qc[256] = {0};
            std::int32_t dq[256] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp16x16(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = qc[i];

            std::uint8_t blk[256] = {0};
            for (int i = 0; i < 256; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd16x16(dq, blk, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = blk[y * 16 + x];
        }
    }
}

// C7: D2 policy loop at 16x16 with the FP quantizer wired at a fixed qindex.
// SCAN POLICY IS OURS: fixed defaultScan16x16 for every block (see
// encodeFrameAuto4x4Q); SVT selects per mode/tx type via get_scan_order.
void encodeFrameAuto16x16Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType,
                           entropy::AomWriter* w, entropy::EcFrameContext* fc) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    // BSF1 symbol emission (D5): identical surface to the lossless variant.
    if (w && fc) {
        entropy::initDefaultEcFrameContext(fc);
        entropy::odEcEncReset(&w->ec);
        w->allow_update_cdf = 1;  // THE COUPLING (BSF4-fix): SVT sets ec_writer.allow_update_cdf = !disable_cdf_update (ec_process.c:101). This 1 is only correct because the ratified structural config carries disable_cdf_update = 0 (resource_coordination_process.c:360). ANY future config with disable_cdf_update = 1 (should_disable_cdf_update, enc_mode_config.c:9559 - future work) must flip this emission with it, or the stream is unspecifiable: the writer adapts CDFs a conformant decoder (allow_update_cdf = 0) will not replay.
        w->pos = 0;
    }
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[256];
    transforms::defaultScan16x16(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) srcBlk[y * 16 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode16x16(srcBlk, above, nTopPx, nTopRightPx, left,
                                                        nLeftPx, nBottomLeftPx, aboveLeft, nctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            if (w && fc) {
                int topCtx = 0, leftCtx = 0;
                entropy::getKfYModeCtx(hasLeft ? 1 : 0, static_cast<int>(nctx.leftMode),
                                       hasTop ? 1 : 0, static_cast<int>(nctx.aboveMode), &topCtx, &leftCtx);
                entropy::writeKfLumaMode(w, fc, entropy::BLOCK_16X16,
                                         static_cast<entropy::PredictionMode>(d.mode), topCtx, leftCtx, 0);
                if (entropy::filterIntraAllowed(1, entropy::BLOCK_16X16, 0,
                                                static_cast<std::uint32_t>(d.mode))) {
                    entropy::writeFilterIntra(w, fc, entropy::BLOCK_16X16, entropy::FILTER_INTRA_MODES);
                }
            }

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictors(pred, 16, d.mode, 0, 16, 16, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);
            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            std::int32_t qc[256] = {0};
            std::int32_t dq[256] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp16x16(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = qc[i];

            std::uint8_t blk[256] = {0};
            for (int i = 0; i < 256; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd16x16(dq, blk, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = blk[y * 16 + x];
        }
    }
    if (w) entropy::odEcStopEncode(w);
}

// ---- L6: 32x32 frame compositions. M1 availability + REAL recon top-right
// gather (FR-series); 32x32 roundtrip is exact so recon == source.
void encodeFrameRecon32x32(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           intra::PredictionMode mode, int angleDelta, transforms::TxType txType) {
    const int gridW = src.width() / 32;
    const int gridH = src.height() / 32;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 32;
            const int py = by * 32;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 32 : 0;
            const int nLeftPx = hasLeft ? 32 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 32 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[64] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[64] = {0};
            if (hasTop) {
                for (int i = 0; i < 32 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 32; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[1024] = {0};
            intra::buildIntraPredictors(pred, 32, mode, angleDelta, 32, 32, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[1024] = {0};
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    residual[y * 32 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 32 + x]);

            std::int32_t cb[1024] = {0};
            transforms::fwdTxfm2d32x32(residual, cb, 32, txType);
            for (int i = 0; i < 1024; ++i) coeffs[(by * gridW + bx) * 1024 + i] = cb[i];

            std::uint8_t tmp[1024] = {0};
            for (int i = 0; i < 1024; ++i) tmp[i] = pred[i];
            transforms::invTxfm2dAdd32x32(cb, tmp, 32, txType);
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) recon.at(px + x, py + y) = tmp[y * 32 + x];
        }
    }
}

void encodeFrameAuto32x32(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                          std::uint8_t* modes, transforms::TxType txType) {
    const int gridW = src.width() / 32;
    const int gridH = src.height() / 32;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 32;
            const int py = by * 32;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 32 : 0;
            const int nLeftPx = hasLeft ? 32 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 32 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[64] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[64] = {0};
            if (hasTop) {
                for (int i = 0; i < 32 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 32; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[1024] = {0};
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) srcBlk[y * 32 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode32x32(srcBlk, above, nTopPx, nTopRightPx, left,
                                                        nLeftPx, nBottomLeftPx, aboveLeft, nctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[1024] = {0};
            intra::buildIntraPredictors(pred, 32, d.mode, 0, 32, 32, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);

            std::int16_t residual[1024] = {0};
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    residual[y * 32 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 32 + x]);

            std::int32_t cb[1024] = {0};
            transforms::fwdTxfm2d32x32(residual, cb, 32, txType);
            for (int i = 0; i < 1024; ++i) coeffs[(by * gridW + bx) * 1024 + i] = cb[i];

            std::uint8_t reconBlk[1024] = {0};
            for (int i = 0; i < 1024; ++i) reconBlk[i] = pred[i];
            transforms::invTxfm2dAdd32x32(cb, reconBlk, 32, txType);
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) recon.at(px + x, py + y) = reconBlk[y * 32 + x];
        }
    }
}

void encodeFrameRecon32x32Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                            intra::PredictionMode mode, int angleDelta, std::int32_t qindex,
                            transforms::TxType txType) {
    const int gridW = src.width() / 32;
    const int gridH = src.height() / 32;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[1024];
    transforms::defaultScan32x32(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 32;
            const int py = by * 32;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 32 : 0;
            const int nLeftPx = hasLeft ? 32 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 32 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[64] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[64] = {0};
            if (hasTop) {
                for (int i = 0; i < 32 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 32; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[1024] = {0};
            intra::buildIntraPredictors(pred, 32, mode, angleDelta, 32, 32, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[1024] = {0};
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    residual[y * 32 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 32 + x]);

            std::int32_t cb[1024] = {0};
            transforms::fwdTxfm2d32x32(residual, cb, 32, txType);
            std::int32_t qc[1024] = {0};
            std::int32_t dq[1024] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp32x32(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 1024; ++i) coeffs[(by * gridW + bx) * 1024 + i] = qc[i];

            std::uint8_t blk[1024] = {0};
            for (int i = 0; i < 1024; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd32x32(dq, blk, 32, txType);
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) recon.at(px + x, py + y) = blk[y * 32 + x];
        }
    }
}

// L6: D2 policy loop at 32x32 with the FP quantizer wired at a fixed qindex
// (log_scale 1). SCAN POLICY IS OURS: fixed defaultScan32x32 for every block
// (see encodeFrameAuto4x4Q); SVT selects per mode/tx type via get_scan_order.
void encodeFrameAuto32x32Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType) {
    const int gridW = src.width() / 32;
    const int gridH = src.height() / 32;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[1024];
    transforms::defaultScan32x32(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 32;
            const int py = by * 32;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 32 : 0;
            const int nLeftPx = hasLeft ? 32 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 32 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[64] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[64] = {0};
            if (hasTop) {
                for (int i = 0; i < 32 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 32; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[1024] = {0};
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) srcBlk[y * 32 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode32x32(srcBlk, above, nTopPx, nTopRightPx, left,
                                                        nLeftPx, nBottomLeftPx, aboveLeft, nctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[1024] = {0};
            intra::buildIntraPredictors(pred, 32, d.mode, 0, 32, 32, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);
            std::int16_t residual[1024] = {0};
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    residual[y * 32 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 32 + x]);

            std::int32_t cb[1024] = {0};
            transforms::fwdTxfm2d32x32(residual, cb, 32, txType);
            std::int32_t qc[1024] = {0};
            std::int32_t dq[1024] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp32x32(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 1024; ++i) coeffs[(by * gridW + bx) * 1024 + i] = qc[i];

            std::uint8_t blk[1024] = {0};
            for (int i = 0; i < 1024; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd32x32(dq, blk, 32, txType);
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) recon.at(px + x, py + y) = blk[y * 32 + x];
        }
    }
}

// ---- L9: 64x64 frame compositions. M1 availability + REAL recon top-right
// gather (FR-series); the 64x64 fwd has net shift 0 - (2-2-2) and the inverse
// -(2+4): roundtrip is lossy like 8x8/32x32 (recon != source).
void encodeFrameRecon64x64(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           intra::PredictionMode mode, int angleDelta, transforms::TxType txType) {
    (void)txType;  // DCT-only at 64x64
    const int gridW = src.width() / 64;
    const int gridH = src.height() / 64;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 64;
            const int py = by * 64;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 64 : 0;
            const int nLeftPx = hasLeft ? 64 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 64 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[128] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[128] = {0};
            if (hasTop) {
                for (int i = 0; i < 64 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 64; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[4096] = {0};
            intra::buildIntraPredictors(pred, 64, mode, angleDelta, 64, 64, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[4096] = {0};
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    residual[y * 64 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 64 + x]);

            std::int32_t cb[4096] = {0};
            transforms::fwdTxfm2d64x64(residual, cb, 64, txType);
            for (int i = 0; i < 4096; ++i) coeffs[(by * gridW + bx) * 4096 + i] = cb[i];

            std::uint8_t tmp[4096] = {0};
            for (int i = 0; i < 4096; ++i) tmp[i] = pred[i];
            transforms::invTxfm2dAdd64x64(cb, tmp, 64, txType);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) recon.at(px + x, py + y) = tmp[y * 64 + x];
        }
    }
}

void encodeFrameAuto64x64(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                          std::uint8_t* modes, transforms::TxType txType) {
    const int gridW = src.width() / 64;
    const int gridH = src.height() / 64;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 64;
            const int py = by * 64;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 64 : 0;
            const int nLeftPx = hasLeft ? 64 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 64 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[128] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[128] = {0};
            if (hasTop) {
                for (int i = 0; i < 64 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 64; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[4096] = {0};
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) srcBlk[y * 64 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode64x64(srcBlk, above, nTopPx, nTopRightPx, left,
                                                        nLeftPx, nBottomLeftPx, aboveLeft, nctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[4096] = {0};
            intra::buildIntraPredictors(pred, 64, d.mode, 0, 64, 64, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);

            std::int16_t residual[4096] = {0};
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    residual[y * 64 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 64 + x]);

            std::int32_t cb[4096] = {0};
            transforms::fwdTxfm2d64x64(residual, cb, 64, txType);
            for (int i = 0; i < 4096; ++i) coeffs[(by * gridW + bx) * 4096 + i] = cb[i];

            std::uint8_t reconBlk[4096] = {0};
            for (int i = 0; i < 4096; ++i) reconBlk[i] = pred[i];
            transforms::invTxfm2dAdd64x64(cb, reconBlk, 64, txType);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) recon.at(px + x, py + y) = reconBlk[y * 64 + x];
        }
    }
}

void encodeFrameRecon64x64Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                            intra::PredictionMode mode, int angleDelta, std::int32_t qindex,
                            transforms::TxType txType) {
    const int gridW = src.width() / 64;
    const int gridH = src.height() / 64;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[4096];
    transforms::defaultScan64x64(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 64;
            const int py = by * 64;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 64 : 0;
            const int nLeftPx = hasLeft ? 64 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 64 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[128] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[128] = {0};
            if (hasTop) {
                for (int i = 0; i < 64 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 64; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[4096] = {0};
            intra::buildIntraPredictors(pred, 64, mode, angleDelta, 64, 64, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[4096] = {0};
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    residual[y * 64 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 64 + x]);

            std::int32_t cb[4096] = {0};
            transforms::fwdTxfm2d64x64(residual, cb, 64, txType);
            std::int32_t qc[4096] = {0};
            std::int32_t dq[4096] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp64x64(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 4096; ++i) coeffs[(by * gridW + bx) * 4096 + i] = qc[i];

            std::uint8_t blk[4096] = {0};
            for (int i = 0; i < 4096; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd64x64(dq, blk, 64, txType);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) recon.at(px + x, py + y) = blk[y * 64 + x];
        }
    }
}

// L9: D2 policy loop at 64x64 with the FP quantizer wired at a fixed qindex
// (log_scale 2). SCAN POLICY IS OURS: fixed defaultScan64x64 for every block
// (see encodeFrameAuto4x4Q); SVT selects per mode/tx type via get_scan_order.
void encodeFrameAuto64x64Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType) {
    const int gridW = src.width() / 64;
    const int gridH = src.height() / 64;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[4096];
    transforms::defaultScan64x64(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 64;
            const int py = by * 64;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 64 : 0;
            const int nLeftPx = hasLeft ? 64 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 64 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[128] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[128] = {0};
            if (hasTop) {
                for (int i = 0; i < 64 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 64; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[4096] = {0};
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) srcBlk[y * 64 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockMode64x64(srcBlk, above, nTopPx, nTopRightPx, left,
                                                        nLeftPx, nBottomLeftPx, aboveLeft, nctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[4096] = {0};
            intra::buildIntraPredictors(pred, 64, d.mode, 0, 64, 64, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, nBottomLeftPx, nctx);
            std::int16_t residual[4096] = {0};
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    residual[y * 64 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 64 + x]);

            std::int32_t cb[4096] = {0};
            transforms::fwdTxfm2d64x64(residual, cb, 64, txType);
            std::int32_t qc[4096] = {0};
            std::int32_t dq[4096] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp64x64(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 4096; ++i) coeffs[(by * gridW + bx) * 4096 + i] = qc[i];

            std::uint8_t blk[4096] = {0};
            for (int i = 0; i < 4096; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd64x64(dq, blk, 64, txType);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) recon.at(px + x, py + y) = blk[y * 64 + x];
        }
    }
}

// ---- CH3: chroma (4:2:0) frame compositions --------------------------------
// The UV plane is its own plane; per-plane availability (the chroma
// above/left mbmi of enc_intra_prediction.c:28-33 maps to the same
// hasTop/hasLeft raster logic on the UV grid). Dispatch: uv_mode folds via
// intra::uv2y (get_uv_mode, common_utils.h:130-133); the builder never sees
// FI (enc_intra_prediction.c:641). The D2 policy scores the 13 folded UV
// candidates (uv modes 0..12; UV_CFL_PRED is NOT a candidate - policy
// named; its prediction-surface fold is DC and DC_PRED is a candidate).
ModeDecision decideBlockModeUv16x16(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                    int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                    int nBottomLeftPx, std::uint8_t aboveLeft,
                                    const intra::NeighborContext& neighbors) {
    ModeDecision best{intra::DC_PRED, 0};
    bool haveBest = false;
    for (int m = 0; m < intra::UV_CFL_PRED; ++m) {
        std::uint8_t pred[256] = {0};
        intra::buildIntraPredictorsUv(pred, 16, static_cast<intra::UvPredictionMode>(m), 0, 16, 16,
                                      aboveLeft, aboveRef, nTopPx, nTopRightPx, leftRef, nLeftPx,
                                      nBottomLeftPx, neighbors);
        const std::uint32_t sad = motion::sad16x16(src, 16, pred, 16);
        if (!haveBest || sad < best.sad) {
            best = ModeDecision{static_cast<intra::PredictionMode>(m), sad};
            haveBest = true;
        }
    }
    return best;
}

void encodeFrameReconChroma16x16(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                                 intra::UvPredictionMode mode, int angleDelta, transforms::TxType txType) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};  // above[B..2B-1] = REAL recon top-right (M1: row above fully reconstructed)
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictorsUv(pred, 16, mode, angleDelta, 16, 16, aboveLeft, above,
                                          nTopPx, nTopRightPx, left, nLeftPx, nBottomLeftPx);

            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = cb[i];

            std::uint8_t blk[256] = {0};
            for (int i = 0; i < 256; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd16x16(cb, blk, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = blk[y * 16 + x];
        }
    }
}

void encodeFrameAutoChroma16x16(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                                std::uint8_t* modes, transforms::TxType txType) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            // neighbor modes are UV modes (numerically the folded luma
            // values); the chroma smooth check (svt_aom_is_smooth on
            // uv_mode, intra_prediction.c:139-140) is numerically the luma
            // smooth set because UV_SMOOTH_* folds to SMOOTH_*.
            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) srcBlk[y * 16 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockModeUv16x16(srcBlk, above, nTopPx, nTopRightPx, left,
                                                          nLeftPx, nBottomLeftPx, aboveLeft, nctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictorsUv(pred, 16, static_cast<intra::UvPredictionMode>(d.mode), 0,
                                          16, 16, aboveLeft, above, nTopPx, nTopRightPx, left,
                                          nLeftPx, nBottomLeftPx, nctx);
            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = cb[i];

            std::uint8_t blk[256] = {0};
            for (int i = 0; i < 256; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd16x16(cb, blk, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = blk[y * 16 + x];
        }
    }
}

void encodeFrameReconChroma16x16Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                                  intra::UvPredictionMode mode, int angleDelta, std::int32_t qindex,
                                  transforms::TxType txType) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[256];
    transforms::defaultScan16x16(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictorsUv(pred, 16, mode, angleDelta, 16, 16, aboveLeft, above,
                                          nTopPx, nTopRightPx, left, nLeftPx, nBottomLeftPx);
            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            std::int32_t qc[256] = {0};
            std::int32_t dq[256] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp16x16(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = qc[i];

            std::uint8_t blk[256] = {0};
            for (int i = 0; i < 256; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd16x16(dq, blk, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = blk[y * 16 + x];
        }
    }
}

void encodeFrameAutoChroma16x16Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                                 std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType) {
    const int gridW = src.width() / 16;
    const int gridH = src.height() / 16;
    transforms::QuantTables qt;
    transforms::buildQuantTables(qindex, qt);
    std::int16_t scan[256];
    transforms::defaultScan16x16(scan);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const bool hasAboveLeft = hasTop && hasLeft;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < gridW) ? 16 : 0;
            const int nBottomLeftPx = 0;

            std::uint8_t above[32] = {0};
            std::uint8_t left[32] = {0};
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) left[i] = recon.at(px - 1, py + i);
            }
            const std::uint8_t aboveLeft =
                hasAboveLeft ? recon.at(px - 1, py - 1) : static_cast<std::uint8_t>(0);

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modes[(by - 1) * gridW + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modes[by * gridW + bx - 1])
                                : intra::DC_PRED;

            std::uint8_t srcBlk[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) srcBlk[y * 16 + x] = src.at(px + x, py + y);

            const ModeDecision d = decideBlockModeUv16x16(srcBlk, above, nTopPx, nTopRightPx, left,
                                                          nLeftPx, nBottomLeftPx, aboveLeft, nctx);
            modes[by * gridW + bx] = static_cast<std::uint8_t>(d.mode);

            std::uint8_t pred[256] = {0};
            intra::buildIntraPredictorsUv(pred, 16, static_cast<intra::UvPredictionMode>(d.mode), 0,
                                          16, 16, aboveLeft, above, nTopPx, nTopRightPx, left,
                                          nLeftPx, nBottomLeftPx, nctx);
            std::int16_t residual[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    residual[y * 16 + x] =
                        static_cast<std::int16_t>(src.at(px + x, py + y) - pred[y * 16 + x]);

            std::int32_t cb[256] = {0};
            transforms::fwdTxfm2d16x16(residual, cb, 16, txType);
            std::int32_t qc[256] = {0};
            std::int32_t dq[256] = {0};
            std::uint16_t eob = 0;
            transforms::quantizeFp16x16(cb, qt, scan, qc, dq, &eob);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = qc[i];

            std::uint8_t blk[256] = {0};
            for (int i = 0; i < 256; ++i) blk[i] = pred[i];
            transforms::invTxfm2dAdd16x16(dq, blk, 16, txType);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) recon.at(px + x, py + y) = blk[y * 16 + x];
        }
    }
}

}  // namespace pipeline