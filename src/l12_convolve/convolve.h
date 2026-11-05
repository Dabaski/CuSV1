// l12_convolve - the convolve.c kernel family (A13, CV1).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/convolve.c (277 nb).
// LANDED: svt_aom_horz_scalar_product :30-:36 (exported, deviation),
// svt_aom_get_filter_base :54-:57, svt_aom_get_filter_offset :59-:61,
// svt_aom_convolve_add_src_horiz_hip :63-:82,
// svt_aom_convolve_add_src_vert_hip :84-:104 (with the uint16
// highbd_vert_scalar_product :46-:52 file-local),
// svt_av1_wiener_convolve_add_src_c :106-:153, vert_scalar_product
// :245-:251 (file-local), svt_aom_convolve_horiz :253-:268,
// svt_aom_convolve_vert :270-:286, svt_aom_convolve8_horiz_c :288-:298,
// svt_aom_convolve8_vert_c :300-:310, plus the support surface: the
// FILTER_BITS..SUBPEL_TAPS defines (filter.h:22-:27),
// WIENER_CLAMP_LIMIT (convolve.h:28), ROUND0/COMPOUND_ROUND1/
// WIENER_ROUND0_BITS (convolve.h:24-:26), WIENER_MAX_EXT_SIZE
// (convolve.c:28), MAX_SB_SIZE (definitions.h:338-:339), the minimal
// ConvolveParams (definitions.h:660-:671) and get_conv_params_wiener
// (convolve.h:72-:96), and the sub_pel_filters_8 table
// (inter_prediction.c:241-:256, transcribed, diff-verified in test).
// PARKED (named, port nothing): the highbd twins
// svt_aom_highbd_convolve_add_src_horiz_hip :155-:176,
// svt_aom_highbd_convolve_add_src_vert_hip :178-:198,
// svt_av1_highbd_wiener_convolve_add_src_c :200-:243 and their helper
// svt_aom_highbd_horz_scalar_product :38-:44 (unreachable at 8-bit).
// svt_memcpy does not appear in the ported spans (no memcpy sites); the
// wiener temp zeroing uses std::memset 1:1 with convolve.c:126.
#pragma once

#include <cstddef>
#include <cstdint>

namespace convolve {

// filter.h:22-:27.
constexpr std::int32_t filterBits = 7;
constexpr std::int32_t subpelBits = 4;
constexpr std::int32_t subpelMask = (1 << subpelBits) - 1;
constexpr std::int32_t subpelShifts = 1 << subpelBits;
constexpr std::int32_t subpelTaps = 8;

// definitions.h:451.
using InterpKernel = std::int16_t[8];

// sub_pel_filters_8 (inter_prediction.c:241-:256), the EIGHTTAP_REGULAR
// table, transcribed mechanically. TRANSCRIPTION CAVEAT: data table, not
// a formula; guarded only by the entry-by-entry diff test in
// tests/test_convolve.cpp. The ported functions are table-agnostic - they
// consume whatever InterpKernel rows the caller passes - so only this one
// table is carried: it is the row set the convolve8_* dispatch path
// consumes in the reference.
alignas(256) extern const InterpKernel subPelFilters8[16];

// convolve.c:54-:57: assumes a 256-byte aligned filter table.
const InterpKernel* getFilterBase(const std::int16_t* filter);

// convolve.c:59-:61.
std::int32_t getFilterOffset(const std::int16_t* f, const InterpKernel* base);

// convolve.c:30-:36 (static in the reference; exported for testability,
// named deviation).
std::int32_t horzScalarProduct(const std::uint8_t* a, const std::int16_t* b);

// convolve.h:28: WIENER_CLAMP_LIMIT(r0, bd).
constexpr std::int32_t wienerClampLimit(std::int32_t r0, std::int32_t bd) {
    return 1 << (bd + 1 + filterBits - r0);
}

// convolve.h:24-:26 (k-prefixed: the reference-named round0Bits/round1Bits
// function parameters must not hide these, C4459 /W4X).
constexpr std::int32_t kRound0Bits = 3;
constexpr std::int32_t kCompoundRound1Bits = 7;
constexpr std::int32_t kWienerRound0Bits = 3;

// definitions.h:660-:671, minimal carry: the wiener path consumes only
// round_0 / round_1.
struct ConvolveParams {
    std::int32_t round0;
    std::int32_t round1;
};

// convolve.h:72-:96 (the wiener conv-params helper; bd is the bit depth).
ConvolveParams getConvParamsWiener(std::int32_t bd);

// convolve.c:63-:82.
void convolveAddSrcHorizHip(const std::uint8_t* src, std::ptrdiff_t srcStride, std::uint16_t* dst,
                            std::ptrdiff_t dstStride, const InterpKernel* xFilters, std::int32_t x0Q4,
                            std::int32_t xStepQ4, std::int32_t w, std::int32_t h, std::int32_t round0Bits);

// convolve.c:84-:104.
void convolveAddSrcVertHip(const std::uint16_t* src, std::ptrdiff_t srcStride, std::uint8_t* dst,
                           std::ptrdiff_t dstStride, const InterpKernel* yFilters, std::int32_t y0Q4,
                           std::int32_t yStepQ4, std::int32_t w, std::int32_t h, std::int32_t round1Bits);

// convolve.c:28: WIENER_MAX_EXT_SIZE (the temp buffer row cap).
constexpr std::int32_t wienerMaxExtSize = 263;
// definitions.h:338-:339.
constexpr std::int32_t maxSbSize = 128;

// convolve.c:106-:153.
void wienerConvolveAddSrc(const std::uint8_t* src, std::ptrdiff_t srcStride, std::uint8_t* dst,
                          std::ptrdiff_t dstStride, const std::int16_t* filterX, const std::int16_t* filterY,
                          std::int32_t w, std::int32_t h, const ConvolveParams* convParams);

// convolve.c:253-:268.
void convolveHoriz(const std::uint8_t* src, std::ptrdiff_t srcStride, std::uint8_t* dst, std::ptrdiff_t dstStride,
                   const InterpKernel* xFilters, std::int32_t x0Q4, std::int32_t xStepQ4, std::int32_t w,
                   std::int32_t h);

// convolve.c:288-:298.
void convolve8Horiz(const std::uint8_t* src, std::ptrdiff_t srcStride, std::uint8_t* dst, std::ptrdiff_t dstStride,
                    const std::int16_t* filterX, std::int32_t xStepQ4, const std::int16_t* filterY,
                    std::int32_t yStepQ4, std::int32_t w, std::int32_t h);

// convolve.c:270-:286.
void convolveVert(const std::uint8_t* src, std::ptrdiff_t srcStride, std::uint8_t* dst, std::ptrdiff_t dstStride,
                  const InterpKernel* yFilters, std::int32_t y0Q4, std::int32_t yStepQ4, std::int32_t w,
                  std::int32_t h);

// convolve.c:300-:310.
void convolve8Vert(const std::uint8_t* src, std::ptrdiff_t srcStride, std::uint8_t* dst, std::ptrdiff_t dstStride,
                   const std::int16_t* filterX, std::int32_t xStepQ4, const std::int16_t* filterY,
                   std::int32_t yStepQ4, std::int32_t w, std::int32_t h);

} // namespace convolve
