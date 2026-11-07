// l9_analysis - the pack/unpack + blend surface (A14, PU1).
// Reference: third_party/SVT-AV1/Source/Lib/C_DEFAULT/pack_unpack_c.c
// (all seven kernels) + blend_a64_mask_c.c (the vmask kernel). The missing
// VAR(W, H) instances of C_DEFAULT/variance.c landed in analysis.{h,cpp}
// beside the AN8 offs-folded shapes - the AN8 family home, per brief.
// l9_analysis - the pack/unpack + blend surface (A14, PU1).
// Reference: third_party/SVT-AV1/Source/Lib/C_DEFAULT/pack_unpack_c.c +
// blend_a64_mask_c.c (+ the missing VAR instances of variance.c).
#pragma once

#include <cstdint>

namespace analysis {
namespace packunpack {

// 1:1 port of svt_enc_msb_pack2_d (third_party/SVT-AV1/Source/Lib/C_DEFAULT/
// pack_unpack_c.c:18-35): packs 8-bit + per-pixel 2-bit data (the 2 MSBs of
// each inn byte) into 10-bit data held in uint16.
void msbPack2d(const std::uint8_t* in8BitBuffer, std::uint32_t in8Stride,
               const std::uint8_t* innBitBuffer, std::uint32_t innStride,
               std::uint16_t* out16BitBuffer, std::uint32_t outStride,
               std::uint32_t width, std::uint32_t height);

// 1:1 port of svt_compressed_packmsb_c (pack_unpack_c.c:41-71): 8-bit +
// COMPRESSED 2-bit data (4 2-bit pixels per inn byte, MSB first) into
// 10-bit uint16. The k_idx loop runs width / 4 - no remainder handling
// (the reference's own contract; callers keep width a multiple of 4).
void compressedPackMsb(const std::uint8_t* in8BitBuffer, std::uint32_t in8Stride,
                       const std::uint8_t* innBitBuffer, std::uint32_t innStride,
                       std::uint16_t* out16BitBuffer, std::uint32_t outStride,
                       std::uint32_t width, std::uint32_t height);

// 1:1 port of svt_unpack_and_2bcompress_c (pack_unpack_c.c:78-146): 16-bit
// down to 8-bit (top truncation, >> 2) plus a COMPRESSED 2-bit plane (4
// pixels per byte, MSB first). Width need not be a multiple of 4: the
// trailing 1..3 pixels of each row still write their partial comp byte.
void unpackAnd2bCompress(const std::uint16_t* in16bBuffer, std::uint32_t in16bStride,
                         std::uint8_t* out8bBuffer, std::uint32_t out8bStride,
                         std::uint8_t* out2bBuffer, std::uint32_t out2bStride,
                         std::uint32_t width, std::uint32_t height);

// 1:1 port of svt_c_unpack_compressed_10bit (pack_unpack_c.c:152-175):
// compressed 2-bit data (4 pixels per byte) to the UNCOMPRESSED per-pixel
// form - each output byte carries the pixel's 2-bit field top-aligned
// (bits 7-6), the same layout msbUnPack2d writes.
void unpackCompressed10bit(const std::uint8_t* innBitBuffer, std::uint32_t innStride,
                           std::uint8_t* inCompnBitBuffer, std::uint32_t outStride,
                           std::uint32_t width, std::uint32_t height);

// 1:1 port of svt_enc_msb_un_pack2_d (pack_unpack_c.c:180-196): 10-bit uint16
// down to 8-bit (>> 2) plus the per-pixel 2-bit plane, each byte carrying the
// LOW pair of the input top-aligned ((in16 & 3) << 6). outnBitBuffer may be
// null - the reference guards the write on it (:190-:193).
void msbUnPack2d(const std::uint16_t* in16BitBuffer, std::uint32_t inStride,
                 std::uint8_t* out8BitBuffer, std::uint8_t* outnBitBuffer,
                 std::uint32_t out8Stride, std::uint32_t outnStride,
                 std::uint32_t width, std::uint32_t height);

// 1:1 port of svt_convert_8bit_to_16bit_c (pack_unpack_c.c:198-205): the
// plain widening copy.
void convert8bitTo16bit(const std::uint8_t* src, std::uint32_t srcStride,
                        std::uint16_t* dst, std::uint32_t dstStride,
                        std::uint32_t width, std::uint32_t height);

// 1:1 port of svt_convert_16bit_to_8bit_c (pack_unpack_c.c:207-214): the
// narrowing copy, truncating to the low byte.
void convert16bitTo8bit(const std::uint16_t* src, std::uint32_t srcStride,
                        std::uint8_t* dst, std::uint32_t dstStride,
                        std::uint32_t width, std::uint32_t height);

// 1:1 port of svt_aom_highbd_blend_a64_vmask_16bit_c
// (third_party/SVT-AV1/Source/Lib/C_DEFAULT/blend_a64_mask_c.c:15-37) - the
// only kernel of the file, a vertical-mask 16-bit blend: the mask is PER ROW
// (m = mask[i], constant across the row). AOM_BLEND_A64(a, v0, v1) =
// ROUND_POWER_OF_TWO(a*v0 + (AOM_BLEND_A64_MAX_ALPHA - a)*v1,
// AOM_BLEND_A64_ROUND_BITS) (Codec/definitions.h:1250-1251; MAX_ALPHA = 64,
// ROUND_BITS = 6, :1247-1248). NAMED JUDGMENT: the kernel carries "highbd"
// in its reference name but is struct-free and the brief assigns the file
// for a 1:1 port with a blend fixture, so it is ported here; the genuinely
// highbd variance/mse wrappers of variance.c landed below in HB1
// (varianceHighbd, highbdMse16x16 - the park is resolved). The (void)bd
// parameter (:18) is dropped (it participates in no arithmetic) and the
// debug asserts (:21-:29) are dropped - named deviations.
void blendA64Vmask16bit(std::uint16_t* dst, int dstStride,
                        const std::uint16_t* src0, int src0Stride,
                        const std::uint16_t* src1, int src1Stride,
                        const std::uint8_t* mask, int w, int h);

// HB1: svt_aom_variance_highbd_c (C_DEFAULT/variance.c:162-181), the hbd
// twin of varianceCore: sad + sse accumulate the int16-range diffs, the
// return is *sse - ((int64_t)sad*sad)/(w*h) (the population variance).
// No bd parameter exists in the reference - callers feed 10/12-bit VALUES.
std::uint32_t varianceHighbd(const std::uint16_t* a, int aStride, const std::uint16_t* b, int bStride, int w, int h,
                             std::uint32_t* sse);

// HB1: svt_aom_highbd_mse16x16_c (C_DEFAULT/variance.c:337-352), the hbd
// twin of the mse entry: 16x16 fixed, uint64 tsse accumulation, the
// (uint32_t) cast at the return. DEVIATION (named, the CS1/LR1 precedent):
// the reference takes uint8_t* + CONVERT_TO_SHORTPTR staging; the port
// takes plain uint16 params - no staging, same arithmetic.
std::uint32_t highbdMse16x16(const std::uint16_t* src, std::int32_t srcStride, const std::uint16_t* ref,
                             std::int32_t refStride);

}
}
