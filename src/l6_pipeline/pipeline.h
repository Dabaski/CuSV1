#pragma once

#include <cstdint>
#include <cstddef>

#include <pixels.h>
#include <intra.h>
#include <motion.h>
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
                    transforms::TxType txType, std::int32_t coeffs[16], std::uint8_t recon[16],
                    const intra::NeighborContext& neighbors = intra::NeighborContext(),
                    bool disableEdgeFilter = false);

// Frame-level intra round trip: raster order over 4x4 blocks, each block
// predicting from RECONSTRUCTED neighbors only (never source pixels).
// Availability (see note): above iff by > 0; left iff bx > 0; above-left iff
// both; top-right iff by > 0 && bx + 1 < gridW; bottom-left NEVER (not yet
// reconstructed). SVT's full rules live in svt_aom_intra_has_top_right /
// bottom_left; this raster simplification is M1's scope. Mode/TxType uniform
// per run. coeffs: 16 per block, row-major block order.
void encodeFrameRecon4x4(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                         intra::PredictionMode mode, int angleDelta, transforms::TxType txType);

void encodeFrameRecon8x8(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                         intra::PredictionMode mode, int angleDelta, transforms::TxType txType);

// D3 frame-level mode decision: raster 4x4 loop, each block's mode chosen by
// decideBlockMode4x4 against RECONSTRUCTED neighbor edges (M1 availability),
// winner encoded via encodeRecon4x4; chosen modes recorded per block and fed
// into NeighborContext (filt_type live) for subsequent blocks. coeffs: 16
// per block, row-major block order; modes: one per block.
void encodeFrameAuto4x4(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                        std::uint8_t* modes, transforms::TxType txType);

// Sum of squared sample differences over the full frame (integer, exact).
std::int64_t frameMse8(const pixels::Plane& a, const pixels::Plane& b);

// D2 mode decision — THE POLICY IS THIS PROJECT'S, NOT SVT's: SVT's real
// mode decision is full RD with rate costs. The 1:1 guarantee covers every
// primitive (predict / transform / SAD); the policy (SAD-only, fixed
// candidate set of all 13 PredictionModes, deterministic tie-break = lowest
// mode index) is documented here and attributed to no one else.
// angleDelta = 0 and filter-intra off for all candidates (parked).
// neighbors: chosen neighbor modes for filt_type (default = no history).
struct ModeDecision {
    intra::PredictionMode mode;
    std::uint32_t sad;
};

ModeDecision decideBlockMode4x4(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                int nBottomLeftPx, std::uint8_t aboveLeft,
                                const intra::NeighborContext& neighbors = intra::NeighborContext());

// D3 policy generalized to 8x8 blocks: decideBlockMode8x8 scores all 13
// candidates with motion::sad8x8 (bit-exact with svt_nxm_sad_kernel_helper_c
// at 8x8); same tie-break = lowest mode index. Policy is ours.
ModeDecision decideBlockMode8x8(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                int nBottomLeftPx, std::uint8_t aboveLeft,
                                const intra::NeighborContext& neighbors = intra::NeighborContext());

void encodeFrameAuto8x8(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                        std::uint8_t* modes, transforms::TxType txType);

std::string subtractCuSource();

}  // namespace pipeline