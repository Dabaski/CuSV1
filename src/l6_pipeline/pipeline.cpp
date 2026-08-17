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
                    transforms::TxType txType, std::int32_t coeffs[16], std::uint8_t recon[16]) {
    std::uint8_t pred[16] = {0};
    predictResidualTxfm(plane, px, py, aboveRef, nTopPx, nTopRightPx, leftRef, nLeftPx, nBottomLeftPx,
                        aboveLeft, mode, angleDelta, txType, coeffs, pred);
    for (int i = 0; i < 16; ++i) {
        recon[i] = pred[i];
    }
    transforms::invTxfm2dAdd4x4(coeffs, recon, 4, txType);
}

}  // namespace pipeline