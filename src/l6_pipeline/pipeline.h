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

// Q2: same D3 loop with the FP quantizer wired at a fixed qindex
// (quantize_fp_helper_c at log_scale 0): qcoeff = coded coeffs, dqcoeff
// feeds the inverse, so recon carries real quantization loss.
void encodeFrameAuto4x4Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                         std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType);

// QW2: 8x8 round trip with the FP quantizer wired at a fixed qindex
// (quantize_fp_helper_c at n_coeffs=64, log_scale 0 per
// av1_get_tx_scale_tab[TX_8X8] = 0). qcoeff = coded coeffs; dqcoeff feeds
// invTxfm2dAdd8x8 onto the raw predictor.
void encodeFrameRecon8x8Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                          intra::PredictionMode mode, int angleDelta, std::int32_t qindex,
                          transforms::TxType txType);

// QW2: D3 policy loop at 8x8 with the FP quantizer wired at a fixed qindex.
// SCAN POLICY IS OURS: fixed defaultScan8x8 for every block (see
// encodeFrameAuto4x4Q); SVT selects per mode/tx type via get_scan_order.
void encodeFrameAuto8x8Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                         std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType);

// C7: 16x16 frame compositions. M1 availability: above iff by > 0, left iff
// bx > 0, above-left iff both, top-right iff by > 0 && bx + 1 < gridW, and
// the top-right extension carries REAL reconstructed samples (FR-series
// gather: above[B..2B-1] = recon[(py-1)][px+B..px+2B-1]). The 16x16 fwd/inv
// roundtrip is exact, so recon == source for every block.
void encodeFrameRecon16x16(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           intra::PredictionMode mode, int angleDelta, transforms::TxType txType);

void encodeFrameAuto16x16(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                          std::uint8_t* modes, transforms::TxType txType);

void encodeFrameRecon16x16Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                            intra::PredictionMode mode, int angleDelta, std::int32_t qindex,
                            transforms::TxType txType);

void encodeFrameAuto16x16Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType);

// L6: 32x32 frame compositions. M1 availability + FR-series REAL recon
// top-right gather (above[B..2B-1] = recon[(py-1)][px+B..px+2B-1]); the
// 32x32 fwd/inv roundtrip is exact, so recon == source for every block.
void encodeFrameRecon32x32(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           intra::PredictionMode mode, int angleDelta, transforms::TxType txType);

void encodeFrameAuto32x32(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                          std::uint8_t* modes, transforms::TxType txType);

void encodeFrameRecon32x32Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                            intra::PredictionMode mode, int angleDelta, std::int32_t qindex,
                            transforms::TxType txType);

void encodeFrameAuto32x32Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType);

// L9: 64x64 frame compositions. M1 availability + FR-series REAL recon
// top-right gather (above[B..2B-1] = recon[(py-1)][px+B..px+2B-1]). DCT-only
// at 64x64. The 64x64 fwd net shift is 0 (2-2-2) - the roundtrip is lossy
// like 8x8/32x32 (recon != source).
void encodeFrameRecon64x64(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           intra::PredictionMode mode, int angleDelta, transforms::TxType txType);

void encodeFrameAuto64x64(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                          std::uint8_t* modes, transforms::TxType txType);

void encodeFrameRecon64x64Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                            intra::PredictionMode mode, int angleDelta, std::int32_t qindex,
                            transforms::TxType txType);

void encodeFrameAuto64x64Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType);

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

// C7 policy generalized to 16x16 blocks: same D2 policy scored with
// motion::sad16x16 (bit-exact with svt_nxm_sad_kernel_helper_c at 16x16).
ModeDecision decideBlockMode16x16(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                  int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                  int nBottomLeftPx, std::uint8_t aboveLeft,
                                  const intra::NeighborContext& neighbors = intra::NeighborContext());

// L5 policy generalized to 32x32 blocks: same D2 policy scored with
// motion::sad32x32 (bit-exact with svt_nxm_sad_kernel_helper_c at 32x32).
ModeDecision decideBlockMode32x32(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                  int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                  int nBottomLeftPx, std::uint8_t aboveLeft,
                                  const intra::NeighborContext& neighbors = intra::NeighborContext());

// L9 policy generalized to 64x64 blocks: same D2 policy scored with
// motion::sad64x64 (bit-exact with svt_nxm_sad_kernel_helper_c at 64x64).
ModeDecision decideBlockMode64x64(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                  int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                  int nBottomLeftPx, std::uint8_t aboveLeft,
                                  const intra::NeighborContext& neighbors = intra::NeighborContext());

void encodeFrameAuto8x8(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                        std::uint8_t* modes, transforms::TxType txType);

std::string subtractCuSource();

}  // namespace pipeline