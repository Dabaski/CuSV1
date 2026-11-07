#include "picops.h"

#include <cstddef>
#include <cstring>

// 1:1 port of svt_residual_kernel16bit_c (pic_operators.c:27-46). No clamp
// and no saturation anywhere in the reference body (:35): the uint16 planes
// are wrap-cast to int16, the subtraction happens in int, and the store
// truncates back to int16.
void pipeline::picops::residualKernel16bit(const std::uint16_t* input, std::uint32_t inputStride,
                                           const std::uint16_t* pred, std::uint32_t predStride,
                                           std::int16_t* residual, std::uint32_t residualStride,
                                           std::uint32_t areaWidth, std::uint32_t areaHeight) {
    std::uint32_t rowIndex = 0;
    while (rowIndex < areaHeight) {
        std::uint32_t columnIndex = 0;
        while (columnIndex < areaWidth) {
            residual[columnIndex] = static_cast<std::int16_t>(input[columnIndex]) -
                                    static_cast<std::int16_t>(pred[columnIndex]);
            ++columnIndex;
        }
        input += inputStride;
        pred += predStride;
        residual += residualStride;
        ++rowIndex;
    }
}

// 1:1 port of svt_residual_kernel8bit_c (pic_operators.c:52-70), the same
// no-clamp rule at :59.
void pipeline::picops::residualKernel8bit(const std::uint8_t* input, std::uint32_t inputStride,
                                          const std::uint8_t* pred, std::uint32_t predStride,
                                          std::int16_t* residual, std::uint32_t residualStride,
                                          std::uint32_t areaWidth, std::uint32_t areaHeight) {
    std::uint32_t rowIndex = 0;
    while (rowIndex < areaHeight) {
        std::uint32_t columnIndex = 0;
        while (columnIndex < areaWidth) {
            residual[columnIndex] = static_cast<std::int16_t>(input[columnIndex]) -
                                    static_cast<std::int16_t>(pred[columnIndex]);
            ++columnIndex;
        }
        input += inputStride;
        pred += predStride;
        residual += residualStride;
        ++rowIndex;
    }
}

// 1:1 port of svt_full_distortion_kernel32_bits_c (pic_operators.c:77-98).
// The int64 SQR accumulation of :86-87; slot order per :96-97.
void pipeline::picops::fullDistortionKernel32Bits(const std::int32_t* coeff, const std::int32_t* reconCoeff,
                                                  std::uint32_t stride, std::uint32_t areaWidth,
                                                  std::uint32_t areaHeight, std::uint64_t* distortionResult) {
    std::uint32_t rowIndex = 0;
    std::int64_t residualDistortion = 0;
    std::int64_t predictionDistortion = 0;

    while (rowIndex < areaHeight) {
        std::uint32_t columnIndex = 0;
        while (columnIndex < areaWidth) {
            const std::int64_t diff = static_cast<std::int64_t>(coeff[columnIndex]) -
                                      static_cast<std::int64_t>(reconCoeff[columnIndex]);
            residualDistortion += diff * diff;
            predictionDistortion += static_cast<std::int64_t>(coeff[columnIndex]) *
                                    static_cast<std::int64_t>(coeff[columnIndex]);
            ++columnIndex;
        }
        coeff += stride;
        reconCoeff += stride;
        ++rowIndex;
    }

    distortionResult[static_cast<std::size_t>(DistCalc::Residual)] = static_cast<std::uint64_t>(residualDistortion);
    distortionResult[static_cast<std::size_t>(DistCalc::Prediction)] = static_cast<std::uint64_t>(predictionDistortion);
}

// 1:1 port of svt_full_distortion_kernel16_bits_c (pic_operators.c:100-123).
// The reference casts the byte pointers to uint16_t* and adds the offsets
// in elements (:106-109); here the params are already uint16_t*.
std::uint64_t pipeline::picops::fullDistortionKernel16Bits(const std::uint16_t* input, std::uint32_t inputOffset,
                                                           std::uint32_t inputStride, const std::uint16_t* pred,
                                                           std::int32_t predOffset, std::uint32_t predStride,
                                                           std::uint32_t areaWidth, std::uint32_t areaHeight) {
    const std::uint16_t* input16 = input + inputOffset;
    const std::uint16_t* pred16 = pred + predOffset;
    std::uint32_t rowIndex = 0;
    std::uint64_t sseDistortion = 0;

    while (rowIndex < areaHeight) {
        std::uint32_t columnIndex = 0;
        while (columnIndex < areaWidth) {
            const std::int64_t diff = static_cast<std::int64_t>(input16[columnIndex]) -
                                      static_cast<std::int64_t>(pred16[columnIndex]);
            sseDistortion += static_cast<std::uint64_t>(diff * diff);
            ++columnIndex;
        }
        input16 += inputStride;
        pred16 += predStride;
        ++rowIndex;
    }

    return sseDistortion;
}

// 1:1 port of svt_full_distortion_kernel_cbf_zero32_bits_c
// (pic_operators.c:128-147): the prediction SSE lands in BOTH slots
// (:145-146).
void pipeline::picops::fullDistortionKernelCbfZero32Bits(const std::int32_t* coeff, std::uint32_t coeffStride,
                                                         std::uint64_t* distortionResult,
                                                         std::uint32_t areaWidth, std::uint32_t areaHeight) {
    std::uint32_t rowIndex = 0;
    std::int64_t predictionDistortion = 0;

    while (rowIndex < areaHeight) {
        std::uint32_t columnIndex = 0;
        while (columnIndex < areaWidth) {
            const std::int64_t c = static_cast<std::int64_t>(coeff[columnIndex]);
            predictionDistortion += c * c;
            ++columnIndex;
        }
        coeff += coeffStride;
        ++rowIndex;
    }

    distortionResult[static_cast<std::size_t>(DistCalc::Residual)] = static_cast<std::uint64_t>(predictionDistortion);
    distortionResult[static_cast<std::size_t>(DistCalc::Prediction)] = static_cast<std::uint64_t>(predictionDistortion);
}

// 1:1 port of svt_aom_picture_full_distortion32_bits_single
// (pic_operators.c:149-160); the RTCD indirection collapses to direct
// calls (named deviation).
void pipeline::picops::pictureFullDistortion32Single(const std::int32_t* coeff, const std::int32_t* reconCoeff,
                                                     std::uint32_t stride, std::uint32_t bwidth,
                                                     std::uint32_t bheight, std::uint64_t* distortion,
                                                     std::uint32_t cntNzCoeff) {
    distortion[0] = 0;
    distortion[1] = 0;

    if (cntNzCoeff) {
        fullDistortionKernel32Bits(coeff, reconCoeff, stride, bwidth, bheight, distortion);
    } else {
        fullDistortionKernelCbfZero32Bits(coeff, stride, distortion, bwidth, bheight);
    }
}

// 1:1 port of svt_aom_yv12_copy_y_c (pic_operators.c:194-215), flattened
// from Yv12BufferConfig to scalars (named deviation). The highbd branch
// reinterprets the same buffers as uint16 rows (:199-208); the 8-bit
// branch memcpy's width bytes per row (:210-214).
void pipeline::picops::yv12CopyY(const std::uint8_t* src, std::uint32_t srcStride, std::uint8_t* dst,
                                 std::uint32_t dstStride, std::uint32_t width, std::uint32_t height,
                                 bool highbd) {
    if (highbd) {
        const std::uint16_t* src16 = reinterpret_cast<const std::uint16_t*>(src);
        std::uint16_t* dst16 = reinterpret_cast<std::uint16_t*>(dst);
        for (std::uint32_t row = 0; row < height; ++row) {
            std::memcpy(dst16, src16, static_cast<std::size_t>(width) * sizeof(std::uint16_t));
            src16 += srcStride;
            dst16 += dstStride;
        }
        return;
    }

    for (std::uint32_t row = 0; row < height; ++row) {
        std::memcpy(dst, src, width);
        src += srcStride;
        dst += dstStride;
    }
}

// 1:1 port of svt_aom_yv12_copy_u_c (pic_operators.c:217-238), flattened
// (named deviation as for yv12CopyY).
void pipeline::picops::yv12CopyU(const std::uint8_t* src, std::uint32_t srcStride, std::uint8_t* dst,
                                 std::uint32_t dstStride, std::uint32_t width, std::uint32_t height,
                                 bool highbd) {
    if (highbd) {
        const std::uint16_t* src16 = reinterpret_cast<const std::uint16_t*>(src);
        std::uint16_t* dst16 = reinterpret_cast<std::uint16_t*>(dst);
        for (std::uint32_t row = 0; row < height; ++row) {
            std::memcpy(dst16, src16, static_cast<std::size_t>(width) * sizeof(std::uint16_t));
            src16 += srcStride;
            dst16 += dstStride;
        }
        return;
    }

    for (std::uint32_t row = 0; row < height; ++row) {
        std::memcpy(dst, src, width);
        src += srcStride;
        dst += dstStride;
    }
}

// 1:1 port of svt_aom_yv12_copy_v_c (pic_operators.c:240-261), flattened
// (named deviation as for yv12CopyY).
void pipeline::picops::yv12CopyV(const std::uint8_t* src, std::uint32_t srcStride, std::uint8_t* dst,
                                 std::uint32_t dstStride, std::uint32_t width, std::uint32_t height,
                                 bool highbd) {
    if (highbd) {
        const std::uint16_t* src16 = reinterpret_cast<const std::uint16_t*>(src);
        std::uint16_t* dst16 = reinterpret_cast<std::uint16_t*>(dst);
        for (std::uint32_t row = 0; row < height; ++row) {
            std::memcpy(dst16, src16, static_cast<std::size_t>(width) * sizeof(std::uint16_t));
            src16 += srcStride;
            dst16 += dstStride;
        }
        return;
    }

    for (std::uint32_t row = 0; row < height; ++row) {
        std::memcpy(dst, src, width);
        src += srcStride;
        dst += dstStride;
    }
}

// 1:1 port of svt_aom_generate_padding16_bit (pic_operators.c:348-388).
// memset16bit (pic_operators.h:39-45, the element loop) is inlined;
// svt_memcpy is the row loop (named substitutions, AN10 precedent).
// No null guard - the reference body has none (:348-388).
void pipeline::picops::generatePadding16Bit(std::uint16_t* srcPic, std::uint32_t srcStride,
                                            std::uint32_t originalSrcWidth, std::uint32_t originalSrcHeight,
                                            std::uint32_t paddingWidth, std::uint32_t paddingHeight) {
    std::uint32_t verticalIdx = originalSrcHeight;
    std::uint16_t* temp0 = srcPic;

    while (verticalIdx) {
        const std::uint16_t leftValue = temp0[0];
        const std::uint16_t rightValue = temp0[originalSrcWidth - 1];

        std::uint16_t* leftDst = temp0 - paddingWidth;
        for (std::uint32_t i = 0; i < paddingWidth; ++i) {
            leftDst[i] = leftValue;
        }
        std::uint16_t* rightDst = temp0 + originalSrcWidth;
        for (std::uint32_t i = 0; i < paddingWidth; ++i) {
            rightDst[i] = rightValue;
        }

        temp0 += srcStride;
        --verticalIdx;
    }

    // vertical padding (:373-387)
    verticalIdx = paddingHeight;
    std::uint16_t* topSrc = srcPic - paddingWidth;
    std::uint16_t* bottomSrc = srcPic - paddingWidth + (originalSrcHeight - 1) * srcStride;
    std::uint16_t* topDst = topSrc;
    std::uint16_t* bottomDst = bottomSrc;

    while (verticalIdx) {
        // top part data copy
        topDst -= srcStride;
        for (std::uint32_t i = 0; i < srcStride; ++i) {
            topDst[i] = topSrc[i];
        }
        // bottom part data copy
        bottomDst += srcStride;
        for (std::uint32_t i = 0; i < srcStride; ++i) {
            bottomDst[i] = bottomSrc[i];
        }
        --verticalIdx;
    }
}

// C_DEFAULT/picture_operators_c.c:22-28
void pipeline::picops::memcpyC(void* dst, const void* src, std::size_t size) {
    std::memcpy(dst, src, size);
}

void pipeline::picops::memsetC(void* dst, int c, std::size_t size) {
    std::memset(dst, c, size);
}

// C_DEFAULT/picture_operators_c.c:30-49 (verbatim)
void pipeline::picops::pictureCopy(const std::uint8_t* src, std::uint32_t srcStride, std::uint8_t* dst,
                 std::uint32_t dstStride, std::uint32_t areaWidth, std::uint32_t areaHeight,
                 std::uint32_t bytesPerSample) {
    std::uint32_t sampleCount = 0;
    const std::uint32_t sampleTotalCount = areaWidth * areaHeight;
    const std::uint32_t copyLength = areaWidth * bytesPerSample;

    const std::uint32_t srcStrideBytes = srcStride * bytesPerSample;
    const std::uint32_t dstStrideBytes = dstStride * bytesPerSample;

    while (sampleCount < sampleTotalCount) {
        memcpyC(dst, src, copyLength);
        src += srcStrideBytes;
        dst += dstStrideBytes;
        sampleCount += areaWidth;
    }
}

// hadamard_col8 (C_DEFAULT/picture_operators_c.c:119-146, verbatim; no >>1)
static void hadamardCol8(const std::int16_t* srcDiff, std::ptrdiff_t srcStride, std::int16_t* coeff) {
    const std::int16_t b0 = srcDiff[0 * srcStride] + srcDiff[1 * srcStride];
    const std::int16_t b1 = srcDiff[0 * srcStride] - srcDiff[1 * srcStride];
    const std::int16_t b2 = srcDiff[2 * srcStride] + srcDiff[3 * srcStride];
    const std::int16_t b3 = srcDiff[2 * srcStride] - srcDiff[3 * srcStride];
    const std::int16_t b4 = srcDiff[4 * srcStride] + srcDiff[5 * srcStride];
    const std::int16_t b5 = srcDiff[4 * srcStride] - srcDiff[5 * srcStride];
    const std::int16_t b6 = srcDiff[6 * srcStride] + srcDiff[7 * srcStride];
    const std::int16_t b7 = srcDiff[6 * srcStride] - srcDiff[7 * srcStride];

    const std::int16_t c0 = b0 + b2;
    const std::int16_t c1 = b1 + b3;
    const std::int16_t c2 = b0 - b2;
    const std::int16_t c3 = b1 - b3;
    const std::int16_t c4 = b4 + b6;
    const std::int16_t c5 = b5 + b7;
    const std::int16_t c6 = b4 - b6;
    const std::int16_t c7 = b5 - b7;

    coeff[0] = c0 + c4;
    coeff[7] = c1 + c5;
    coeff[3] = c2 + c6;
    coeff[4] = c3 + c7;
    coeff[2] = c0 - c4;
    coeff[6] = c1 - c5;
    coeff[1] = c2 - c6;
    coeff[5] = c3 - c7;
}

// svt_aom_hadamard_8x8_c (C_DEFAULT/picture_operators_c.c:150-174, verbatim)
void pipeline::picops::hadamard8x8(const std::int16_t* srcDiff, std::ptrdiff_t srcStride, std::int32_t* coeff) {
    std::int16_t buffer[64];
    std::int16_t buffer2[64];
    std::int16_t* tmpBuf = &buffer[0];
    for (int idx = 0; idx < 8; ++idx) {
        hadamardCol8(srcDiff, srcStride, tmpBuf);
        tmpBuf += 8;
        ++srcDiff;
    }

    tmpBuf = &buffer[0];
    for (int idx = 0; idx < 8; ++idx) {
        hadamardCol8(tmpBuf, 8, buffer2 + 8 * idx);
        ++tmpBuf;
    }

    for (int idx = 0; idx < 64; ++idx) {
        coeff[idx] = static_cast<std::int32_t>(buffer2[idx]);
    }
}

// svt_aom_hadamard_16x16_c (C_DEFAULT/picture_operators_c.c:265-292, verbatim)
void pipeline::picops::hadamard16x16(const std::int16_t* srcDiff, std::ptrdiff_t srcStride, std::int32_t* coeff) {
    for (int idx = 0; idx < 4; ++idx) {
        const std::int16_t* srcPtr = srcDiff + (idx >> 1) * 8 * srcStride + (idx & 0x01) * 8;
        hadamard8x8(srcPtr, srcStride, coeff + idx * 64);
    }

    for (int idx = 0; idx < 64; ++idx) {
        const std::int32_t a0 = coeff[0];
        const std::int32_t a1 = coeff[64];
        const std::int32_t a2 = coeff[128];
        const std::int32_t a3 = coeff[192];

        const std::int32_t b0 = (a0 + a1) >> 1;
        const std::int32_t b1 = (a0 - a1) >> 1;
        const std::int32_t b2 = (a2 + a3) >> 1;
        const std::int32_t b3 = (a2 - a3) >> 1;

        coeff[0]   = b0 + b2;
        coeff[64]  = b1 + b3;
        coeff[128] = b0 - b2;
        coeff[192] = b1 - b3;

        ++coeff;
    }
}

// svt_aom_hadamard_32x32_c (C_DEFAULT/picture_operators_c.c:294-321, verbatim)
void pipeline::picops::hadamard32x32(const std::int16_t* srcDiff, std::ptrdiff_t srcStride, std::int32_t* coeff) {
    for (int idx = 0; idx < 4; ++idx) {
        const std::int16_t* srcPtr = srcDiff + (idx >> 1) * 16 * srcStride + (idx & 0x01) * 16;
        hadamard16x16(srcPtr, srcStride, coeff + idx * 256);
    }

    for (int idx = 0; idx < 256; ++idx) {
        const std::int32_t a0 = coeff[0];
        const std::int32_t a1 = coeff[256];
        const std::int32_t a2 = coeff[512];
        const std::int32_t a3 = coeff[768];

        const std::int32_t b0 = (a0 + a1) >> 2;
        const std::int32_t b1 = (a0 - a1) >> 2;
        const std::int32_t b2 = (a2 + a3) >> 2;
        const std::int32_t b3 = (a2 - a3) >> 2;

        coeff[0]   = b0 + b2;
        coeff[256] = b1 + b3;
        coeff[512] = b0 - b2;
        coeff[768] = b1 - b3;

        ++coeff;
    }
}

// C_DEFAULT/picture_operators_c.c:323-335 (verbatim)
void pipeline::picops::copyWxh8bit(std::uint8_t* src, std::uint32_t srcStride, std::uint8_t* dst,
                                   std::uint32_t dstStride, std::uint32_t height, std::uint32_t width) {
    for (std::uint32_t j = 0; j < height; j++) {
        memcpyC(dst + j * dstStride, src + j * srcStride, width);
    }
}

void pipeline::picops::copyWxh16bit(std::uint16_t* src, std::uint32_t srcStride, std::uint16_t* dst,
                                    std::uint32_t dstStride, std::uint32_t height, std::uint32_t width) {
    for (std::uint32_t j = 0; j < height; j++) {
        memcpyC(dst + j * dstStride, src + j * srcStride, width * 2);
    }
}

// C_DEFAULT/picture_operators_c.c:53-71 (verbatim)
std::uint64_t pipeline::picops::spatialFullDistortion(const std::uint8_t* input, std::uint32_t inputOffset,
                                                      std::uint32_t inputStride, const std::uint8_t* recon,
                                                      std::uint32_t reconOffset, std::uint32_t reconStride,
                                                      std::uint32_t areaWidth, std::uint32_t areaHeight) {
    std::uint64_t spatialDistortion = 0;
    input += inputOffset;
    recon += reconOffset;

    for (std::uint32_t rowIndex = 0; rowIndex < areaHeight; ++rowIndex) {
        std::uint32_t columnIndex = 0;
        while (columnIndex < areaWidth) {
            const long long diff = static_cast<long long>(input[columnIndex]) - static_cast<long long>(recon[columnIndex]);
            spatialDistortion += static_cast<std::uint64_t>(diff * diff);
            ++columnIndex;
        }

        input += inputStride;
        recon += reconStride;
    }
    return spatialDistortion;
}

// hadamard_col4 (C_DEFAULT/picture_operators_c.c:73-83, verbatim)
static void hadamardCol4(const std::int16_t* srcDiff, std::ptrdiff_t srcStride, std::int16_t* coeff) {
    const std::int16_t b0 = (srcDiff[0 * srcStride] + srcDiff[1 * srcStride]) >> 1;
    const std::int16_t b1 = (srcDiff[0 * srcStride] - srcDiff[1 * srcStride]) >> 1;
    const std::int16_t b2 = (srcDiff[2 * srcStride] + srcDiff[3 * srcStride]) >> 1;
    const std::int16_t b3 = (srcDiff[2 * srcStride] - srcDiff[3 * srcStride]) >> 1;

    coeff[0] = b0 + b2;
    coeff[1] = b1 + b3;
    coeff[2] = b0 - b2;
    coeff[3] = b1 - b3;
}

// svt_aom_hadamard_4x4_c (C_DEFAULT/picture_operators_c.c:85-115, verbatim)
void pipeline::picops::hadamard4x4(const std::int16_t* srcDiff, std::ptrdiff_t srcStride, std::int32_t* coeff) {
    std::int16_t buffer[16];
    std::int16_t buffer2[16];
    std::int16_t* tmpBuf = &buffer[0];
    for (int idx = 0; idx < 4; ++idx) {
        hadamardCol4(srcDiff, srcStride, tmpBuf);
        tmpBuf += 4;
        ++srcDiff;
    }

    tmpBuf = &buffer[0];
    for (int idx = 0; idx < 4; ++idx) {
        hadamardCol4(tmpBuf, 4, buffer2 + 4 * idx);
        ++tmpBuf;
    }

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            coeff[i * 4 + j] = static_cast<std::int32_t>(buffer2[j * 4 + i]);
        }
    }
}

// hadamard_highbd_col8_first_pass (C_DEFAULT/picture_operators_c.c:176-203,
// verbatim; HB1 resolved the 8-bit-discipline park). int16 arithmetic, the
// landed hadamardCol8 shape without its no->>1 difference - identical body.
static void hadamardHighbdCol8FirstPass(const std::int16_t* srcDiff, std::ptrdiff_t srcStride,
                                        std::int16_t* coeff) {
    const std::int16_t b0 = srcDiff[0 * srcStride] + srcDiff[1 * srcStride];
    const std::int16_t b1 = srcDiff[0 * srcStride] - srcDiff[1 * srcStride];
    const std::int16_t b2 = srcDiff[2 * srcStride] + srcDiff[3 * srcStride];
    const std::int16_t b3 = srcDiff[2 * srcStride] - srcDiff[3 * srcStride];
    const std::int16_t b4 = srcDiff[4 * srcStride] + srcDiff[5 * srcStride];
    const std::int16_t b5 = srcDiff[4 * srcStride] - srcDiff[5 * srcStride];
    const std::int16_t b6 = srcDiff[6 * srcStride] + srcDiff[7 * srcStride];
    const std::int16_t b7 = srcDiff[6 * srcStride] - srcDiff[7 * srcStride];

    const std::int16_t c0 = b0 + b2;
    const std::int16_t c1 = b1 + b3;
    const std::int16_t c2 = b0 - b2;
    const std::int16_t c3 = b1 - b3;
    const std::int16_t c4 = b4 + b6;
    const std::int16_t c5 = b5 + b7;
    const std::int16_t c6 = b4 - b6;
    const std::int16_t c7 = b5 - b7;

    coeff[0] = c0 + c4;
    coeff[7] = c1 + c5;
    coeff[3] = c2 + c6;
    coeff[4] = c3 + c7;
    coeff[2] = c0 - c4;
    coeff[6] = c1 - c5;
    coeff[1] = c2 - c6;
    coeff[5] = c3 - c7;
}

// hadamard_highbd_col8_second_pass (C_DEFAULT/picture_operators_c.c:
// 207-234, verbatim). INT32 arithmetic - the first-pass output is 16-bit
// ([-32760, 32760]) and the 8-bit int16 second pass would overflow.
static void hadamardHighbdCol8SecondPass(const std::int16_t* srcDiff, std::ptrdiff_t srcStride,
                                         std::int32_t* coeff) {
    const std::int32_t b0 = srcDiff[0 * srcStride] + srcDiff[1 * srcStride];
    const std::int32_t b1 = srcDiff[0 * srcStride] - srcDiff[1 * srcStride];
    const std::int32_t b2 = srcDiff[2 * srcStride] + srcDiff[3 * srcStride];
    const std::int32_t b3 = srcDiff[2 * srcStride] - srcDiff[3 * srcStride];
    const std::int32_t b4 = srcDiff[4 * srcStride] + srcDiff[5 * srcStride];
    const std::int32_t b5 = srcDiff[4 * srcStride] - srcDiff[5 * srcStride];
    const std::int32_t b6 = srcDiff[6 * srcStride] + srcDiff[7 * srcStride];
    const std::int32_t b7 = srcDiff[6 * srcStride] - srcDiff[7 * srcStride];

    const std::int32_t c0 = b0 + b2;
    const std::int32_t c1 = b1 + b3;
    const std::int32_t c2 = b0 - b2;
    const std::int32_t c3 = b1 - b3;
    const std::int32_t c4 = b4 + b6;
    const std::int32_t c5 = b5 + b7;
    const std::int32_t c6 = b4 - b6;
    const std::int32_t c7 = b5 - b7;

    coeff[0] = c0 + c4;
    coeff[7] = c1 + c5;
    coeff[3] = c2 + c6;
    coeff[4] = c3 + c7;
    coeff[2] = c0 - c4;
    coeff[6] = c1 - c5;
    coeff[1] = c2 - c6;
    coeff[5] = c3 - c7;
}

// svt_aom_highbd_hadamard_8x8_c (C_DEFAULT/picture_operators_c.c:238-262,
// verbatim). Same signature as hadamard8x8; the second pass is the int32
// twin above.
void pipeline::picops::highbdHadamard8x8(const std::int16_t* srcDiff, std::ptrdiff_t srcStride,
                                         std::int32_t* coeff) {
    std::int16_t buffer[64];
    std::int32_t buffer2[64];
    std::int16_t* tmpBuf = &buffer[0];
    for (int idx = 0; idx < 8; ++idx) {
        hadamardHighbdCol8FirstPass(srcDiff, srcStride, tmpBuf);
        tmpBuf += 8;
        ++srcDiff;
    }

    tmpBuf = &buffer[0];
    for (int idx = 0; idx < 8; ++idx) {
        hadamardHighbdCol8SecondPass(tmpBuf, 8, buffer2 + 8 * idx);
        ++tmpBuf;
    }

    for (int idx = 0; idx < 64; ++idx) {
        coeff[idx] = static_cast<std::int32_t>(buffer2[idx]);
    }
}
