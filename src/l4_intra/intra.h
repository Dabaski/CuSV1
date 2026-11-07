#pragma once

#include <cstdint>
#include <string>

namespace intra {

enum PredictionMode {
    DC_PRED = 0,
    V_PRED = 1,
    H_PRED = 2,
    D45_PRED = 3,
    D135_PRED = 4,
    D113_PRED = 5,
    D157_PRED = 6,
    D203_PRED = 7,
    D67_PRED = 8,
    SMOOTH_PRED = 9,
    SMOOTH_V_PRED = 10,
    SMOOTH_H_PRED = 11,
    PAETH_PRED = 12,
};

// definitions.h:1210-1227, verbatim order. UV_CFL_PRED is enumerated but the
// CFL-specific combine (cfl_alpha AC-from-luma) is OUT of scope (named
// deviation, CH-series): at the prediction surface SVT folds UV_CFL_PRED to
// DC_PRED (g_uv2y, common_utils.c:28).
enum UvPredictionMode {
    UV_DC_PRED = 0,
    UV_V_PRED = 1,
    UV_H_PRED = 2,
    UV_D45_PRED = 3,
    UV_D135_PRED = 4,
    UV_D113_PRED = 5,
    UV_D157_PRED = 6,
    UV_D203_PRED = 7,
    UV_D67_PRED = 8,
    UV_SMOOTH_PRED = 9,
    UV_SMOOTH_V_PRED = 10,
    UV_SMOOTH_H_PRED = 11,
    UV_PAETH_PRED = 12,
    UV_CFL_PRED = 13,
    UV_INTRA_MODES = 14,
    UV_MODE_INVALID = 15,
};

// get_uv_mode (common_utils.h:130-133) = g_uv2y[mode]
// (common_utils.c:14-31, verbatim table): UV modes fold to the luma
// primitive set, UV_CFL_PRED -> DC_PRED, sentinels -> INTRA_INVALID (25).
int uv2y(UvPredictionMode mode);

// Per-block neighbor mode history (the only cross-block state intra knows).
struct NeighborContext {
    PredictionMode aboveMode = DC_PRED;
    PredictionMode leftMode = DC_PRED;
};

// Chroma entry: folds uv_mode via uv2y and calls the size-generic builder
// with FILTER_INTRA_MODES (chroma never uses FI, enc_intra_prediction.c:641).
// The predictor math is plane-agnostic; this wrapper IS the chroma dispatch.
void buildIntraPredictorsUv(std::uint8_t* dst, int dstStride, UvPredictionMode mode, int angleDelta,
                            int txwpx, int txhpx, std::uint8_t aboveLeft, const std::uint8_t* aboveRef,
                            int nTopPx, int nTopRightPx, const std::uint8_t* leftRef, int nLeftPx,
                            int nBottomLeftPx, const NeighborContext& neighbors = NeighborContext(),
                            int disableEdgeFilter = 0);

// get_filt_type (enc_intra_prediction.c:20), luma: 1 iff either neighbor
// block uses a SMOOTH* mode (svt_aom_is_smooth, intra_prediction.c:128).
int filtType(const NeighborContext& neighbors);

// definitions.h:1295, verbatim order.
enum class FilterIntraMode {
    FILTER_DC_PRED = 0,
    FILTER_V_PRED = 1,
    FILTER_H_PRED = 2,
    FILTER_D157_PRED = 3,
    FILTER_PAETH_PRED = 4,
    FILTER_INTRA_MODES = 5,
};

// svt_av1_filter_intra_predictor_c (C_DEFAULT/filterintra_c.c:70), bw x bh.
// above points at samples[0]; the corner is at above[-1] (SVT convention).
void filterIntraPredictor(std::uint8_t* dst, int dstStride, const std::uint8_t* above,
                          const std::uint8_t* left, int mode, int bw, int bh);

void buildIntraPredictors(std::uint8_t* dst, int dstStride, int mode, int angleDelta, int txwpx, int txhpx,
                          std::uint8_t aboveLeft, const std::uint8_t* aboveRef, int nTopPx, int nTopRightPx,
                          const std::uint8_t* leftRef, int nLeftPx, int nBottomLeftPx,
                          const NeighborContext& neighbors = NeighborContext(), int filterIntraMode = -1,
                          bool disableEdgeFilter = false);

std::string drZ1CuSource();

std::string drZ2CuSource();

std::string drZ3CuSource();

std::string drPredictCuSource();

std::string smoothPredictCuSourceRef();

std::string predictBlockCuSource();

std::string predictBlock8x8CuSource();

std::string predictBlock16x16CuSource();

std::string predictBlock32x32CuSource();

std::string predictBlock64x64CuSource();

int edgeFilterStrength(int bs0, int bs1, int delta, int type);

int useIntraEdgeUpsample(int bs0, int bs1, int delta, int type);

void filterIntraEdge(std::uint8_t* p, int sz, int strength);

void upsampleIntraEdge(std::uint8_t* p, int sz);

// svt_av1_upsample_intra_edge_high_c
// (C_DEFAULT/intra_prediction_c.c:15-37), the hbd twin: uint16 edge, the
// same (s + 8) >> 4 kernel, clip_pixel_highbd(s, bd).
// (HB1 resolved the 8-bit-discipline park.)
void upsampleIntraEdgeHigh(std::uint16_t* p, int sz, int bd);

void drZ1(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above, const std::uint8_t* left,
          int upsampleAbove, int dx, int dy);

void drZ2(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above, const std::uint8_t* left,
          int upsampleAbove, int upsampleLeft, int dx, int dy);

void drZ3(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above, const std::uint8_t* left,
          int upsampleLeft, int dx, int dy);

// svt_av1_highbd_dr_prediction_z3_c
// (C_DEFAULT/intra_prediction_c.c:64-92), the hbd twin: uint16 dst/left,
// clip_pixel_highbd(val, bd). (HB1 resolved the 8-bit-discipline park.)
void hbdDrZ3(std::uint16_t* dst, std::ptrdiff_t stride, int bw, int bh, const std::uint16_t* above,
             const std::uint16_t* left, int upsampleLeft, int dx, int dy, int bd);

int getDx(int angle);

int getDy(int angle);

void smoothPredict(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above,
                   const std::uint8_t* left);

void smoothVPredict(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above,
                    const std::uint8_t* left);

void smoothHPredict(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above,
                    const std::uint8_t* left);

void drPredictor(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above, const std::uint8_t* left,
                 int upsampleAbove, int upsampleLeft, int angle);

// svt_cfl_predict_lbd_c (C_DEFAULT/cfl_c.c:26-40): CFL prediction by
// multiplying alpha (q3) to ac values (q3) and adding the dc chroma pred.
// predBufQ3 advances by CFL_BUF_LINE (32) per row.
void cflPredictLbd(const std::int16_t* predBufQ3, const std::uint8_t* pred, int predStride, std::uint8_t* dst,
                   int dstStride, int alphaQ3, int bitDepth, int width, int height);

// svt_cfl_predict_hbd_c (C_DEFAULT/cfl_c.c:46-59), the hbd twin: uint16
// pred + dst, the same signed q0 scaling and clip_pixel_highbd(.., bd).
// (HB1 resolved the 8-bit-discipline park.)
void cflPredictHbd(const std::int16_t* predBufQ3, const std::uint16_t* pred, int predStride, std::uint16_t* dst,
                   int dstStride, int alphaQ3, int bitDepth, int width, int height);

}  // namespace intra
