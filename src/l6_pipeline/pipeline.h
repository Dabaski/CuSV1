#pragma once

#include <cstdint>
#include <cstddef>

#include <pixels.h>
#include <intra.h>
#include <motion.h>
#include <transform.h>
#include <entropy.h>

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
// same chain - fixed-point fwd+inv is lossy in general, so recon is NOT
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
                         std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType,
                         entropy::AomWriter* w = nullptr, entropy::EcFrameContext* fc = nullptr,
                         entropy::DcSignLevelCoeffNa* na = nullptr);

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
                         std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType,
                         entropy::AomWriter* w = nullptr, entropy::EcFrameContext* fc = nullptr,
                         entropy::DcSignLevelCoeffNa* na = nullptr);

// C7: 16x16 frame compositions. M1 availability: above iff by > 0, left iff
// bx > 0, above-left iff both, top-right iff by > 0 && bx + 1 < gridW, and
// the top-right extension carries REAL reconstructed samples (FR-series
// gather: above[B..2B-1] = recon[(py-1)][px+B..px+2B-1]). The 16x16 fwd/inv
// roundtrip is exact, so recon == source for every block.
void encodeFrameRecon16x16(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           intra::PredictionMode mode, int angleDelta, transforms::TxType txType);

// BSF1: optional key-frame luma symbol emission (D5: symbols only). When w
// != null, the frame emits, per block in raster order: the kf y mode symbol
// (writeKfLumaMode, entropy_coding.c:1026-1040) with the context pair from
// the DECIDED neighbor modes (getKfYModeCtx, :1004-1021; DC_PRED context
// when unavailable), the angle-delta symbol when the decided mode is
// directional (delta 0), and the filter-intra flag=0 symbol
// (writeFilterIntra with FILTER_INTRA_MODES) where filterIntraAllowed
// (mode_decision.c:108-119). fc is initialized via initDefaultEcFrameContext
// and returned ADAPTED (the caller reads the same stream back with it). The
// caller assigns w->ec.buf; the function resets the encoder, forces
// allow_update_cdf = 1 and finishes with odEcStopEncode. No partition/skip
// symbols (ECP1/ECP2) and no tile assembly (BSF3/BSF4) at this stage. GPU
// frame paths are unchanged (host-decision-path bookkeeping only).
void encodeFrameAuto16x16(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                          std::uint8_t* modes, transforms::TxType txType,
                          entropy::AomWriter* w = nullptr, entropy::EcFrameContext* fc = nullptr);

void encodeFrameRecon16x16Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                            intra::PredictionMode mode, int angleDelta, std::int32_t qindex,
                            transforms::TxType txType);

void encodeFrameAuto16x16Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                           std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType,
                           entropy::AomWriter* w = nullptr, entropy::EcFrameContext* fc = nullptr,
                           entropy::DcSignLevelCoeffNa* na = nullptr);

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
                           std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType,
                           entropy::AomWriter* w = nullptr, entropy::EcFrameContext* fc = nullptr,
                           entropy::DcSignLevelCoeffNa* na = nullptr);

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
                           std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType,
                           entropy::AomWriter* w = nullptr, entropy::EcFrameContext* fc = nullptr,
                           entropy::DcSignLevelCoeffNa* na = nullptr);

// D2 mode decision - THE POLICY IS THIS PROJECT'S, NOT SVT's: SVT's real
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

// ---- CH3: chroma (4:2:0) frame compositions --------------------------------
// The UV plane is its own plane (32x32 = the 4:2:0 decimation of a 64x64
// luma frame), blocks are UV-sized, availability is per-plane (chroma
// above/left mbmi). Dispatch: uv_mode folds via intra::uv2y (get_uv_mode,
// common_utils.h:130-133) and the builder never sees FI
// (enc_intra_prediction.c:641). The D2 policy scores the 13 folded UV
// candidates (uv modes 0..12; UV_CFL_PRED is NOT a candidate - the
// mode-decision CFL combine is out of scope, its prediction-surface fold is
// DC and DC_PRED is a candidate; policy named).
ModeDecision decideBlockModeUv16x16(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                                    int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                                    int nBottomLeftPx, std::uint8_t aboveLeft,
                                    const intra::NeighborContext& neighbors = intra::NeighborContext());

void encodeFrameReconChroma16x16(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                                 intra::UvPredictionMode mode, int angleDelta, transforms::TxType txType);

void encodeFrameAutoChroma16x16(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                                std::uint8_t* modes, transforms::TxType txType);

void encodeFrameReconChroma16x16Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                                  intra::UvPredictionMode mode, int angleDelta, std::int32_t qindex,
                                  transforms::TxType txType);

void encodeFrameAutoChroma16x16Q(const pixels::Plane& src, pixels::Plane& recon, std::int32_t* coeffs,
                                 std::uint8_t* modes, std::int32_t qindex, transforms::TxType txType);

// ---- CS4: the chroma-emitting Q walk ---------------------------------------
// The size-generic UV D2 decision (the CH3 policy: 13 folded candidates via
// intra::uv2y, UV_CFL_PRED excluded, SAD at the UV size, tie = lowest index).
ModeDecision decideBlockModeUv(const std::uint8_t* src, const std::uint8_t* aboveRef, int nTopPx,
                               int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                               int nBottomLeftPx, std::uint8_t aboveLeft, int uvB,
                               const intra::NeighborContext& neighbors = intra::NeighborContext());

// The chroma-emitting Q loop over the 4:2:0 planes. Per luma block (lumaB in
// {8, 16, 32, 64}; the UV tx = lumaB/2 per the av1_get_max_uv_txsize map,
// common_utils.h:142-149) in raster order: [luma kf mode symbol -> uv_mode
// symbol (writeUvMode, the DECIDED chroma mode; ONE uv_mode per block - the
// V TU shares the U-plane decision, the walk's documented policy) -> the uv
// angle-delta symbol where gated (bsize >= 8X8 AND the folded mode
// directional)] then the LUMA chain -> the U chain -> the V chain (U BEFORE
// V, three separate per-plane NAs; luma whole-block txb_skip_ctx 0; chroma
// ctx_base + 7 per the :310-314 branch via the CS1 wrapper plumbing). CHROMA
// OWNERSHIP (is_chroma_reference, common_utils.h:315-320): 4:2:0, TRUE only
// at odd mi_row AND odd mi_col for 4x4 luma blocks - a single-4x4-TU frame
// codes NO chroma; the (1,1) owner carries the quad's UV TU at UV
// ((px & ~7) >> 1, (py & ~7) >> 1) (ROUND_UV, definitions.h:327). The
// modes/eobs arrays carry 0xFF/0xFFFF markers for non-referenced blocks. The
// lumaB=64 luma chain runs the TX_64X64 scan contract (the compacted
// 1024-position emission domain). No partition/skip symbols (the ratified
// TD5b-era shape extended). The v3 SPS/frame-header composition is NOT
// wired here (CS5).
void encodeFrameChromaQ(const pixels::Plane& srcY, const pixels::Plane& srcU,
                        const pixels::Plane& srcV, pixels::Plane& reconY, pixels::Plane& reconU,
                        pixels::Plane& reconV, std::int32_t* coeffsY, std::int32_t* coeffsU,
                        std::int32_t* coeffsV, std::uint8_t* modesY, std::uint8_t* modesU,
                        std::uint8_t* modesV, std::uint16_t* eobsY, std::uint16_t* eobsU,
                        std::uint16_t* eobsV, std::int32_t qindex, int lumaB,
                        transforms::TxType txType, entropy::AomWriter* w,
                        entropy::EcFrameContext* fc, entropy::DcSignLevelCoeffNa* naY,
                        entropy::DcSignLevelCoeffNa* naU, entropy::DcSignLevelCoeffNa* naV);

// Sum of squared sample differences over the full frame (integer, exact).
std::int64_t frameMse8(const pixels::Plane& a, const pixels::Plane& b);

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