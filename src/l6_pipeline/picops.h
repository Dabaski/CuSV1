// l6_pipeline - the pic_operators kernel family (A6, PO1).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/pic_operators.c
// (the residual/full-distortion/yv12-copy spans). The pad family
// :266-479 is AN10's (l9_analysis), EXCEPT generate_padding16_bit
// :348-388 which AN10's register named excluded - landed here. The
// pack/unpack dispatchers :162-189 and the EbPictureBufferDesc-walled
// drivers :481-645 are PARKED (PO1 report); their callee bodies are
// C_DEFAULT/pack_unpack_c.c (PU1, analysis::packunpack).
#pragma once

#include <cstdint>
#include <cstddef>

namespace pipeline {
namespace picops {

// 1:1 port of svt_residual_kernel16bit_c (pic_operators.c:27-46). NO
// saturation/clamp: residual[col] = (int16)input[col] - (int16)pred[col]
// (:35), the uint16 planes wrap-cast to int16 and subtracted; strides
// are independent and the residual rows are only area_width wide.
void residualKernel16bit(const std::uint16_t* input, std::uint32_t inputStride,
                         const std::uint16_t* pred, std::uint32_t predStride,
                         std::int16_t* residual, std::uint32_t residualStride,
                         std::uint32_t areaWidth, std::uint32_t areaHeight);

// 1:1 port of svt_residual_kernel8bit_c (pic_operators.c:52-70), the same
// no-clamp subtract rule at 8-bit (:59); the residual element type is
// still int16.
void residualKernel8bit(const std::uint8_t* input, std::uint32_t inputStride,
                        const std::uint8_t* pred, std::uint32_t predStride,
                        std::int16_t* residual, std::uint32_t residualStride,
                        std::uint32_t areaWidth, std::uint32_t areaHeight);

// DistCalcType (third_party/SVT-AV1/Source/Lib/Codec/definitions.h:1934-1939):
// the two result slots of the full-distortion kernels.
enum class DistCalc : std::uint32_t { Residual = 0, Prediction = 1, Total = 2 };

// 1:1 port of svt_full_distortion_kernel32_bits_c (pic_operators.c:77-98).
// residual = SSE(coeff - reconCoeff), prediction = SSE(coeff), int64
// accumulation (utility.h:107 SQR) written to
// distortionResult[DIST_CALC_RESIDUAL] / [DIST_CALC_PREDICTION] (:96-97);
// both coeff planes advance by the same stride.
void fullDistortionKernel32Bits(const std::int32_t* coeff, const std::int32_t* reconCoeff,
                                std::uint32_t stride, std::uint32_t areaWidth,
                                std::uint32_t areaHeight, std::uint64_t* distortionResult);

// 1:1 port of svt_full_distortion_kernel16_bits_c (pic_operators.c:100-123).
// The uint8_t* params + input_offset/pred_offset are SVT's byte-buffer
// convention for 16-bit planes - the reference casts to uint16_t* and
// advances by the offsets in ELEMENTS (:106-109), pred_offset stays
// int32 while input_offset is uint32 exactly as declared. DEVIATION
// (named): the byte-pointer cast is replaced by uint16_t* params; the
// offsets, strides, widths and the plain uint16 subtract are unchanged.
std::uint64_t fullDistortionKernel16Bits(const std::uint16_t* input, std::uint32_t inputOffset,
                                         std::uint32_t inputStride, const std::uint16_t* pred,
                                         std::int32_t predOffset, std::uint32_t predStride,
                                         std::uint32_t areaWidth, std::uint32_t areaHeight);

// svt_memcpy_c / svt_memset_c (C_DEFAULT/picture_operators_c.c:22-28):
// thin wrappers around memcpy/memset.
void memcpyC(void* dst, const void* src, std::size_t size);
void memsetC(void* dst, int c, std::size_t size);

// svt_aom_picture_copy_kernel (C_DEFAULT/picture_operators_c.c:30-49):
// row-by-row copy honoring strides, bytes_per_sample = 1 always.
void pictureCopy(const std::uint8_t* src, std::uint32_t srcStride, std::uint8_t* dst,
                 std::uint32_t dstStride, std::uint32_t areaWidth, std::uint32_t areaHeight,
                 std::uint32_t bytesPerSample);

// svt_spatial_full_distortion_kernel_c (C_DEFAULT/picture_operators_c.c:53-71):
// sum over the area of (input - recon)^2, with offsets applied to both planes.
std::uint64_t spatialFullDistortion(const std::uint8_t* input, std::uint32_t inputOffset,
                                    std::uint32_t inputStride, const std::uint8_t* recon,
                                    std::uint32_t reconOffset, std::uint32_t reconStride,
                                    std::uint32_t areaWidth, std::uint32_t areaHeight);

// svt_aom_hadamard_4x4_c (C_DEFAULT/picture_operators_c.c:85-115): two
// butterfly passes (col4 with >>1) plus the SSE2-style final transpose.
void hadamard4x4(const std::int16_t* srcDiff, std::ptrdiff_t srcStride, std::int32_t* coeff);

// svt_aom_hadamard_8x8_c (C_DEFAULT/picture_operators_c.c:150-174): two
// col8 butterfly passes (no >>1), no final transpose.
void hadamard8x8(const std::int16_t* srcDiff, std::ptrdiff_t srcStride, std::int32_t* coeff);

// svt_aom_highbd_hadamard_8x8_c (C_DEFAULT/picture_operators_c.c:238-262),
// the hbd twin: the SAME signature, an int16 first pass
// (hadamard_highbd_col8_first_pass, :176-203) and an INT32 second pass
// (hadamard_highbd_col8_second_pass, :207-234) - the 8-bit second pass
// would overflow int16 at the hbd 13-bit input range.
// (HB1 resolved the 8-bit-discipline park.)
void highbdHadamard8x8(const std::int16_t* srcDiff, std::ptrdiff_t srcStride, std::int32_t* coeff);

// svt_aom_hadamard_16x16_c (C_DEFAULT/picture_operators_c.c:265-292): four
// 8x8 quadrant transforms, then a fold over offsets {0,64,128,192} with >>1.
void hadamard16x16(const std::int16_t* srcDiff, std::ptrdiff_t srcStride, std::int32_t* coeff);

// svt_aom_hadamard_32x32_c (C_DEFAULT/picture_operators_c.c:294-321): four
// 16x16 quadrant transforms, then a fold over offsets {0,256,512,768} with >>2.
void hadamard32x32(const std::int16_t* srcDiff, std::ptrdiff_t srcStride, std::int32_t* coeff);

// svt_av1_copy_wxh_8bit_c / _16bit_c (C_DEFAULT/picture_operators_c.c:323-335):
// height row copies of width elements, honoring both strides.
void copyWxh8bit(std::uint8_t* src, std::uint32_t srcStride, std::uint8_t* dst, std::uint32_t dstStride,
                 std::uint32_t height, std::uint32_t width);
void copyWxh16bit(std::uint16_t* src, std::uint32_t srcStride, std::uint16_t* dst, std::uint32_t dstStride,
                  std::uint32_t height, std::uint32_t width);

// 1:1 port of svt_full_distortion_kernel_cbf_zero32_bits_c
// (pic_operators.c:128-147): prediction = SSE(coeff), and BOTH result
// slots receive that SAME value (:145-146) - the residual slot is not
// zeroed.
void fullDistortionKernelCbfZero32Bits(const std::int32_t* coeff, std::uint32_t coeffStride,
                                       std::uint64_t* distortionResult, std::uint32_t areaWidth,
                                       std::uint32_t areaHeight);

// 1:1 port of svt_aom_picture_full_distortion32_bits_single
// (pic_operators.c:149-160): zero both slots, then dispatch on
// cnt_nz_coeff - the kernel32 path (nonzero) or the cbf-zero path (zero).
// DEVIATION (named): the reference calls the RTCD pointers
// svt_full_distortion_kernel32_bits / _cbf_zero32_bits
// (common_dsp_rtcd.h:154-155) which resolve to the same C bodies; the
// port calls our two functions directly. The recon_coeff param is
// nullptr in the cbf path exactly as the reference's caller passes it.
void pictureFullDistortion32Single(const std::int32_t* coeff, const std::int32_t* reconCoeff,
                                   std::uint32_t stride, std::uint32_t bwidth, std::uint32_t bheight,
                                   std::uint64_t* distortion, std::uint32_t cntNzCoeff);

// 1:1 port of svt_aom_yv12_copy_y_c (pic_operators.c:194-215), flattened
// from Yv12BufferConfig to scalars - NAMED DEVIATION: the struct fields
// (y_buffer/y_stride/y_width/y_height, pic_buffer_desc.h Yv12BufferConfig)
// become scalar args, the YV12_FLAG_HIGHBITDEPTH branch
// (pic_buffer_desc.h:103) becomes the highbd flag, and the
// CONVERT_TO_SHORTPTR re-cast becomes the uint16 path over the same
// buffers; svt_memcpy (common_dsp_rtcd.h:1082, the RTCD wrapper over
// plain memcpy) becomes std::memcpy.
void yv12CopyY(const std::uint8_t* src, std::uint32_t srcStride, std::uint8_t* dst,
               std::uint32_t dstStride, std::uint32_t width, std::uint32_t height, bool highbd);

// 1:1 port of svt_aom_yv12_copy_u_c (pic_operators.c:217-238), flattened
// the same way (u_buffer/uv_stride/uv_width/uv_height).
void yv12CopyU(const std::uint8_t* src, std::uint32_t srcStride, std::uint8_t* dst,
               std::uint32_t dstStride, std::uint32_t width, std::uint32_t height, bool highbd);

// 1:1 port of svt_aom_yv12_copy_v_c (pic_operators.c:240-261), flattened
// the same way (v_buffer/uv_stride/uv_width/uv_height).
void yv12CopyV(const std::uint8_t* src, std::uint32_t srcStride, std::uint8_t* dst,
               std::uint32_t dstStride, std::uint32_t width, std::uint32_t height, bool highbd);

// 1:1 port of svt_aom_generate_padding16_bit (pic_operators.c:348-388).
// AN10's register (docs/todo.md section A0) named this function EXCLUDED,
// so it was never in the l9_analysis pad family - it is struct-free and
// lands here beside its pic_operators.c siblings. Horizontal first: left
// pad = row[0], right pad = row[width-1], both memset16bit-style element
// loops over padding_width ELEMENTS (:363-371); then vertical: copy the
// fully padded top/bottom rows up/down, src_stride ELEMENTS per copy
// (:373-387). NOTE (the reference's own TODO, :347): this body is not
// null-guarded, unlike the 8-bit generate_padding - ported as-is.
void generatePadding16Bit(std::uint16_t* srcPic, std::uint32_t srcStride,
                          std::uint32_t originalSrcWidth, std::uint32_t originalSrcHeight,
                          std::uint32_t paddingWidth, std::uint32_t paddingHeight);

}
}
