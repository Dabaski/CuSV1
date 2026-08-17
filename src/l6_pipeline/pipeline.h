#pragma once

#include <cstdint>
#include <cstddef>

#include <pixels.h>
#include <intra.h>
#include <transform.h>

namespace pipeline {

// Compose the 4x4 host block path, mirroring the SVT chain:
//   build_intra_predictors -> av1_subtract_block (int16, no clamping)
//   -> av1_tranform_two_d_core_c (4x4, shift {2,0,0})
// src window is taken from the luma Plane at (px, py); edges are supplied by
// the caller following buildIntraPredictors' convention.
void encodeBlock4x4(const pixels::Plane& plane, int px, int py, const std::uint8_t* aboveRef, int nTopPx,
                    int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx, int nBottomLeftPx,
                    std::uint8_t aboveLeft, intra::PredictionMode mode, int angleDelta,
                    transforms::TxType txType, std::int32_t coeffs[16]);

// Round trip: encode (predict + residual + fwd 2D) then reconstruct
// (invTxfm2dAdd4x4 onto the same predictor). recon = SVT's recon for the
// same chain — fixed-point fwd+inv is lossy in general, so recon is NOT
// guaranteed to equal the source block.
void encodeRecon4x4(const pixels::Plane& plane, int px, int py, const std::uint8_t* aboveRef, int nTopPx,
                    int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx, int nBottomLeftPx,
                    std::uint8_t aboveLeft, intra::PredictionMode mode, int angleDelta,
                    transforms::TxType txType, std::int32_t coeffs[16], std::uint8_t recon[16]);

// Frame-level intra round trip: raster order over 4x4 blocks, each block
// predicting from RECONSTRUCTED neighbors only (never source pixels).
// Availability (see note): above iff by > 0; left iff bx > 0; above-left iff
// both; top-right iff by > 0 && bx + 1 < gridW; bottom-left NEVER (not yet
// reconstructed). SVT's full rules live in svt_aom_intra_has_top_right /
// bottom_left; this raster simplification is M1's scope. Mode/TxType uniform
// per run. coeffs: 16 per block, row-major block order.
void encodeFrameRecon4x4(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                         intra::PredictionMode mode, int angleDelta, transforms::TxType txType);

std::string subtractCuSource();

}  // namespace pipeline