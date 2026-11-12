#pragma once

#include <cstdint>

// l17_inter - the A3 inter-prediction surface (inter_prediction.c and the
// struct-free parts of enc_inter_prediction.c). The scaffold carries the
// scale-factor bit constants (inter_prediction.c:21-24) and the namespace;
// the slices land under src/l17_inter (namespace inter).
namespace inter {

// inter_prediction.c:21-24.
constexpr int kSubpelBits = 4;
constexpr int kScaleSubpelBits = 10;
constexpr int kScaleSubpelShifts = 1 << kScaleSubpelBits;
constexpr int kScaleSubpelMask = kScaleSubpelShifts - 1;
constexpr int kScaleExtraBits = kScaleSubpelBits - kSubpelBits;

} // namespace inter
