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

// Per-block neighbor mode history (the only cross-block state intra knows).
struct NeighborContext {
    PredictionMode aboveMode = DC_PRED;
    PredictionMode leftMode = DC_PRED;
};

// get_filt_type (enc_intra_prediction.c:20), luma: 1 iff either neighbor
// block uses a SMOOTH* mode (svt_aom_is_smooth, intra_prediction.c:128).
int filtType(const NeighborContext& neighbors);

void buildIntraPredictors(std::uint8_t* dst, int dstStride, int mode, int angleDelta, int txwpx, int txhpx,
                          std::uint8_t aboveLeft, const std::uint8_t* aboveRef, int nTopPx, int nTopRightPx,
                          const std::uint8_t* leftRef, int nLeftPx, int nBottomLeftPx,
                          const NeighborContext& neighbors = NeighborContext());

std::string drZ1CuSource();

std::string drZ2CuSource();

std::string drZ3CuSource();

std::string drPredictCuSource();

std::string smoothPredictCuSourceRef();

std::string predictBlockCuSource();

int edgeFilterStrength(int bs0, int bs1, int delta, int type);

int useIntraEdgeUpsample(int bs0, int bs1, int delta, int type);

void filterIntraEdge(std::uint8_t* p, int sz, int strength);

void upsampleIntraEdge(std::uint8_t* p, int sz);

void drZ1(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above, const std::uint8_t* left,
          int upsampleAbove, int dx, int dy);

void drZ2(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above, const std::uint8_t* left,
          int upsampleAbove, int upsampleLeft, int dx, int dy);

void drZ3(std::uint8_t* dst, int stride, int bw, int bh, const std::uint8_t* above, const std::uint8_t* left,
          int upsampleLeft, int dx, int dy);

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

}  // namespace intra
