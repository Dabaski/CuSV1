// l9_analysis::subpel - the subpel variance family (A14, SUB1).
// Reference: third_party/SVT-AV1/Source/Lib/C_DEFAULT/variance.c
// (the bilinear 2-tap passes, the SUBPIX_VAR/OBMC_VAR/OBMC_SUBPIX_VAR
// macro families and their instances; variance_c itself is landed - PU1).
// LANDED so far: the 2-tap bilinear filter table (S1).
#pragma once

#include <cstdint>

namespace analysis {
namespace subpel {

// 1:1 port of bilinear_filters_2t (third_party/SVT-AV1/Source/Lib/Codec/
// filter.h:39-48), [BIL_SUBPEL_SHIFTS][2] with BIL_SUBPEL_SHIFTS = 8
// (BIL_SUBPEL_BITS 3, filter.h:35-36). Row i = {128 - 16*i, 16*i}; every
// row sums to FILTER_WEIGHT 128 (FILTER_BITS 7, filter.h:22).
constexpr std::uint8_t kBilinearFilters2t[8][2] = {
    {128, 0},  {112, 16}, {96, 32},  {80, 48},
    {64, 64},  {48, 80},  {32, 96},  {16, 112},
};

// 1:1 port of aom_var_filter_block2d_bil_first_pass_c (C_DEFAULT/variance.c:
// 29-44, the :21-:28 comment): b[j] = ROUND_POWER_OF_TWO(a[0]*filter[0] +
// a[pixel_step]*filter[1], FILTER_BITS) (variance.c:36), uint8 -> uint16,
// advancing a by 1 per column and by (srcPixelsPerLine - outputWidth) per
// row (:38-:41). Exported (static in the reference - the AN8 statics
// precedent) so the composition tests can build the filtered reference.
void filterBlock2dBilFirstPass(const std::uint8_t* a, std::uint16_t* b,
                               unsigned int srcPixelsPerLine, unsigned int pixelStep,
                               unsigned int outputHeight, unsigned int outputWidth,
                               const std::uint8_t* filter);

// 1:1 port of aom_var_filter_block2d_bil_second_pass_c (C_DEFAULT/variance.c:
// 55-69, the :46-:54 comment): uint16 -> uint8, the same
// ROUND_POWER_OF_TWO form (:62), inputs at first-pass scale (<= 255, :50).
// Exported for the composition tests (the AN8 statics precedent).
void filterBlock2dBilSecondPass(const std::uint16_t* a, std::uint8_t* b,
                                unsigned int srcPixelsPerLine, unsigned int pixelStep,
                                unsigned int outputHeight, unsigned int outputWidth,
                                const std::uint8_t* filter);

// 1:1 port of the SUBPIX_VAR(W, H) macro instances svt_aom_sub_pixel_variance
// ##W##x##H##_c (C_DEFAULT/variance.c:192-202, instantiated by the 22
// VARIANCES(W, H) lines :208-:229): first pass over H+1 rows, second pass
// to temp2, then the plain VAR instance on temp2 (:201). xoffset/yoffset
// index kBilinearFilters2t - the reference's q3 subpel grid, 0..7.
// (16x16 and 8x8 route through varianceCore + the :189 formula instead of
// the landed analysis::variance16x16/8x8, whose AN8 shape is the const-128
// offs-folded form - named deviation, see subpel.cpp.)
// 1:1 port of the SUBPIX_VAR(W, H) macro instances svt_aom_sub_pixel_variance
// ##W##x##H##_c (C_DEFAULT/variance.c:192-202, instantiated by the 22
// VARIANCES(W, H) lines :208-:229): first pass over H+1 rows, second pass
// to temp2, then the plain VAR instance on temp2 (:201). xoffset/yoffset
// index kBilinearFilters2t - the reference's q3 subpel grid, 0..7.
// (16x16 and 8x8 route through varianceCore + the :189 formula instead of
// the landed analysis::variance16x16/8x8, whose AN8 shape is the const-128
// offs-folded form - named deviation, see subpel.cpp.)
std::uint32_t subPixelVariance4x4(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                  const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance8x4(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                  const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance4x8(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                  const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance8x8(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                  const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance8x16(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                   const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance16x8(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                   const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance16x16(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                    const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance16x32(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                    const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance32x16(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                    const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance32x32(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                    const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance32x64(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                    const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance64x32(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                    const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance64x64(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                    const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance64x128(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                     const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance128x64(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                     const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance128x128(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                      const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance4x16(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                   const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance16x4(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                   const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance8x32(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                   const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance32x8(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                   const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance16x64(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                    const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t subPixelVariance64x16(const std::uint8_t* a, int aStride, int xoffset, int yoffset,
                                    const std::uint8_t* b, int bStride, std::uint32_t* sse);

// 1:1 port of the obmc_variance core (C_DEFAULT/variance.c:231-249, static
// there - the AN8 statics precedent): diff = ROUND_POWER_OF_TWO_SIGNED(
// wsrc[j] - pre[j]*mask[j], 12) (:240), summed and squared, per row
// advancing pre by pre_stride and wsrc/mask by w.
void obmcVariance(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                  const std::int32_t* mask, int w, int h, std::uint32_t* sse, int* sum);

// 1:1 port of the OBMC_VAR(W, H) macro instances svt_aom_obmc_variance
// ##W##x##H##_c (variance.c:251-257) and the OBMC_SUBPIX_VAR(W, H) instances
// (:259-274, same first/second-pass preamble as SUBPIX_VAR then the OBMC_VAR
// instance on temp2, :270-273). The reference's own instance list is 22
// pairs (16 in :276-:322 + 4x16/16x4/8x32/32x8/16x64/64x16 in :324-:335);
// BRIEF CORRECTION: 22 of each, not 16.
std::uint32_t obmcVariance4x4(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                              const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance4x8(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                              const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance8x4(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                              const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance8x8(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                              const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance8x16(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                               const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance16x8(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                               const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance16x16(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                                const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance16x32(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                                const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance32x16(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                                const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance32x32(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                                const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance32x64(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                                const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance64x32(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                                const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance64x64(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                                const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance64x128(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                                 const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance128x64(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                                 const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance128x128(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                                  const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance4x16(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                               const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance16x4(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                               const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance8x32(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                               const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance32x8(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                               const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance16x64(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                                const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcVariance64x16(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                                const std::int32_t* mask, std::uint32_t* sse);

std::uint32_t obmcSubPixelVariance4x4(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                      const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance4x8(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                      const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance8x4(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                      const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance8x8(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                      const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance8x16(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                       const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance16x8(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                       const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance16x16(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                        const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance16x32(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                        const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance32x16(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                        const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance32x32(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                        const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance32x64(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                        const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance64x32(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                        const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance64x64(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                        const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance64x128(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                         const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance128x64(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                         const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance128x128(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                          const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance4x16(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                       const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance16x4(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                       const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance8x32(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                       const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance32x8(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                       const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance16x64(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                        const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);
std::uint32_t obmcSubPixelVariance64x16(const std::uint8_t* pre, int preStride, int xoffset, int yoffset,
                                        const std::int32_t* wsrc, const std::int32_t* mask, std::uint32_t* sse);

}
}
