// l12_convolve - the convolve.c kernel family (A13, CV1) PLUS the IP1
// interp-filter-params machinery + upsampled_pred (A3/A14, this slice).
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
//
// IP1 additions (the interp-filter-params machinery + the subpel upsample
// driver, transcribed 1:1, diff-verified in tests/test_interpfilters.cpp):
// - the five remaining InterpKernel tables: sub_pel_filters_4
//   (inter_prediction.c:257-:272), sub_pel_filters_8sharp (:1133-:1148),
//   sub_pel_filters_8smooth (:1150-:1165), bilinear_filters
//   (:1166-:1181), sub_pel_filters_4smooth (:1182-:1197) - joining the
//   CV1 sub_pel_filters_8 (:241-:256).
// - InterpFilter (definitions.h:818-:827), SubpelSearchType
//   (definitions.h:834-:839), InterpFilterParams (definitions.h:849-:854),
//   interpFilterParamsList (inter_prediction.h:80-:84), interp4tap
//   (:124-:126, taps stays 8 - the reference's zero-padded 4-tap quirk),
//   extractInterpFilter (filter.h:60-:62), makeInterpFilters
//   (filter.h:64-:68), getInterpFilterSubpelKernel (filter.h:77-:80),
//   getInterpFilterParamsWithBlockSize (inter_prediction.h:128-:137),
//   getConvolveFilterParams (:139-:145).
// - getFilter (variance.c:71-:85, exported, DF1 precedent) and
//   upsampledPred (variance.c:88-:137; the brief's span map said
//   inter_prediction.c :88-:141 - WRONG FILE, the definition is in
//   C_DEFAULT/variance.c; its xd/cm/mi_row/mi_col/mv struct params are
//   kept as void* call-shape slots and voided, named deviation).
// The interp tables are consumed ONLY via the convolve8_* path (which
// takes kernel pointers directly), so the 256-byte alignment is kept for
// 1:1 fidelity with the reference's DECLARE_ALIGNED(256), not for the
// getFilterBase trick.
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

// sub_pel_filters_4 (inter_prediction.c:257-:272), transcribed. Same
// TRANSCRIPTION CAVEAT as subPelFilters8 above; diff-verified in
// tests/test_interpfilters.cpp.
alignas(256) extern const InterpKernel subPelFilters4[16];

// sub_pel_filters_8sharp (inter_prediction.c:1133-:1148), transcribed.
alignas(256) extern const InterpKernel subPelFilters8sharp[16];

// sub_pel_filters_8smooth (inter_prediction.c:1150-:1165), transcribed.
alignas(256) extern const InterpKernel subPelFilters8smooth[16];

// bilinear_filters (inter_prediction.c:1166-:1181), transcribed.
alignas(256) extern const InterpKernel bilinearFilters[16];

// sub_pel_filters_4smooth (inter_prediction.c:1182-:1197), transcribed.
alignas(256) extern const InterpKernel subPelFilters4smooth[16];

// definitions.h:818-:827, verbatim order (the enum values are the
// reference's; the params list indexes by them).
enum class InterpFilter {
    EIGHTTAP_REGULAR,
    EIGHTTAP_SMOOTH,
    MULTITAP_SHARP,
    BILINEAR,
    INTERP_FILTERS_ALL,
    SWITCHABLE_FILTERS = BILINEAR,
    SWITCHABLE = SWITCHABLE_FILTERS + 1,
    EXTRA_FILTERS = INTERP_FILTERS_ALL - SWITCHABLE_FILTERS,
};

// definitions.h:834-:839, the SUBPEL_SEARCH_TYPE list consumed by getFilter.
enum class SubpelSearchType {
    USE_2_TAPS_ORIG = 0,
    USE_2_TAPS,
    USE_4_TAPS,
    USE_8_TAPS,
};

// definitions.h:849-:854.
struct InterpFilterParams {
    const std::int16_t* filterPtr;
    std::uint16_t taps;
    std::uint16_t subpelShifts;
    InterpFilter interpFilter;
};

// The reference's inline array bound SWITCHABLE_FILTERS + 1
// (inter_prediction.h:80), named so callers and tests share it.
constexpr int interpFilterParamsListSize = static_cast<int>(InterpFilter::SWITCHABLE_FILTERS) + 1;

// inter_prediction.h:80-:84 (static const per TU in the reference; one
// shared const definition here, named deviation).
extern const InterpFilterParams interpFilterParamsList[interpFilterParamsListSize];

// inter_prediction.h:124-:126. NOTE the reference's own quirk, kept: the
// two 4-tap entries carry taps = SUBPEL_TAPS = 8 (the tables are
// zero-padded to the 8-tap row stride).
extern const InterpFilterParams interp4tap[2];

// filter.h:60-:62. The packed InterpFilters uint32: y in the low 16 bits,
// x in the high 16 (av1_make_interp_filters, filter.h:64-:68); a nonzero
// x_filter selects the high half.
InterpFilter extractInterpFilter(std::uint32_t filters, std::int32_t xFilter);

// filter.h:77-:80. No SUBPEL_MASK here - the caller's phase index is used
// as-is (the reference's own unmasked pointer arithmetic).
const std::int16_t* getInterpFilterSubpelKernel(const InterpFilterParams filterParams, std::int32_t subpel);

// inter_prediction.h:128-:137. At w <= 4 REGULAR/SHARP return the 4-tap
// REGULAR entry, SMOOTH the 4-tap SMOOTH entry; BILINEAR falls through to
// its 8-tap params_list entry (no 4-tap bilinear branch in the reference).
InterpFilterParams getInterpFilterParamsWithBlockSize(InterpFilter interpFilter, std::int32_t w);

// filter.h:64-:68: y in the low 16 bits, x in the high 16.
std::uint32_t makeInterpFilters(InterpFilter yFilter, InterpFilter xFilter);

// inter_prediction.h:139-:145. x is the high half of the packed filters,
// y the low half; params_x gates on w, params_y on h.
void getConvolveFilterParams(std::uint32_t interpFilters, InterpFilterParams* paramsX, InterpFilterParams* paramsY,
                             std::int32_t w, std::int32_t h);

// variance.c:71-:85 (static INLINE file-local in the reference; exported
// for testability, DF1 cdefSign precedent). Asserts subpelSearch >=
// USE_2_TAPS; USE_2_TAPS_ORIG returns nullptr after the reference's
// assert(0).
const InterpFilterParams* getFilter(std::int32_t subpelSearch);

// variance.c:88-:137 (svt_aom_upsampled_pred_c; the brief's span map named
// inter_prediction.c :88-:141 - WRONG FILE, the definition lives in
// C_DEFAULT/variance.c). The reference's xd/cm/mi_row/mi_col/mv struct
// params are kept as call-shape slots and (void)ed - the body voids all
// of them too; they are struct-walled, named deviation.
void upsampledPred(void* xd, void* cm, std::int32_t miRow, std::int32_t miCol, void* mv, std::uint8_t* compPred,
                   std::int32_t width, std::int32_t height, std::int32_t subpelXQ3, std::int32_t subpelYQ3,
                   const std::uint8_t* ref, std::int32_t refStride, std::int32_t subpelSearch);

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
