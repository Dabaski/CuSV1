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
