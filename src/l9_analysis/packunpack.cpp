#include "packunpack.h"

namespace analysis {
namespace packunpack {

// 1:1 port of svt_enc_msb_pack2_d (third_party/SVT-AV1/Source/Lib/C_DEFAULT/
// pack_unpack_c.c:18-35).
void msbPack2d(const std::uint8_t* in8BitBuffer, std::uint32_t in8Stride,
               const std::uint8_t* innBitBuffer, std::uint32_t innStride,
               std::uint16_t* out16BitBuffer, std::uint32_t outStride,
               std::uint32_t width, std::uint32_t height) {
    for (std::uint64_t j = 0; j < height; j++) {
        for (std::uint64_t k = 0; k < width; k++) {
            const std::uint16_t outPixel =
                static_cast<std::uint16_t>(in8BitBuffer[k + j * in8Stride] << 2);
            const std::uint8_t nBitPixel =
                (innBitBuffer[k + j * innStride] >> 6) & 3;

            out16BitBuffer[k + j * outStride] = outPixel | nBitPixel;
        }
    }
}

// 1:1 port of svt_compressed_packmsb_c (pack_unpack_c.c:41-71).
void compressedPackMsb(const std::uint8_t* in8BitBuffer, std::uint32_t in8Stride,
                       const std::uint8_t* innBitBuffer, std::uint32_t innStride,
                       std::uint16_t* out16BitBuffer, std::uint32_t outStride,
                       std::uint32_t width, std::uint32_t height) {
    for (std::uint64_t row = 0; row < height; row++) {
        for (std::uint64_t kIdx = 0; kIdx < width / 4; kIdx++) {
            const std::uint8_t four2bitPels = innBitBuffer[kIdx + row * innStride];

            std::uint8_t nBitPixel = (four2bitPels >> 6) & 3;
            std::uint16_t outPixel = static_cast<std::uint16_t>(
                in8BitBuffer[kIdx * 4 + 0 + row * in8Stride] << 2);
            out16BitBuffer[kIdx * 4 + 0 + row * outStride] = outPixel | nBitPixel;

            nBitPixel = (four2bitPels >> 4) & 3;
            outPixel = static_cast<std::uint16_t>(
                in8BitBuffer[kIdx * 4 + 1 + row * in8Stride] << 2);
            out16BitBuffer[kIdx * 4 + 1 + row * outStride] = outPixel | nBitPixel;

            nBitPixel = (four2bitPels >> 2) & 3;
            outPixel = static_cast<std::uint16_t>(
                in8BitBuffer[kIdx * 4 + 2 + row * in8Stride] << 2);
            out16BitBuffer[kIdx * 4 + 2 + row * outStride] = outPixel | nBitPixel;

            nBitPixel = (four2bitPels >> 0) & 3;
            outPixel = static_cast<std::uint16_t>(
                in8BitBuffer[kIdx * 4 + 3 + row * in8Stride] << 2);
            out16BitBuffer[kIdx * 4 + 3 + row * outStride] = outPixel | nBitPixel;
        }
    }
}

// 1:1 port of svt_c_unpack_compressed_10bit (pack_unpack_c.c:152-175).
void unpackCompressed10bit(const std::uint8_t* innBitBuffer, std::uint32_t innStride,
                           std::uint8_t* inCompnBitBuffer, std::uint32_t outStride,
                           std::uint32_t width, std::uint32_t height) {
    for (std::uint32_t rowIndex = 0; rowIndex < height; rowIndex++) {
        for (std::uint32_t colIndex = 0; colIndex < width; colIndex++) {
            const std::uint32_t dataByte =
                innBitBuffer[rowIndex * innStride + colIndex / 4];

            std::uint8_t uncompressedByte = 0;

            if (colIndex % 4 == 0) {
                uncompressedByte = static_cast<std::uint8_t>(dataByte & 0xC0);
            } else if (colIndex % 4 == 1) {
                uncompressedByte = static_cast<std::uint8_t>((dataByte << 2) & 0xC0);
            } else if (colIndex % 4 == 2) {
                uncompressedByte = static_cast<std::uint8_t>((dataByte << 4) & 0xC0);
            } else {
                uncompressedByte = static_cast<std::uint8_t>((dataByte << 6) & 0xC0);
            }

            inCompnBitBuffer[rowIndex * outStride + colIndex] = uncompressedByte;
        }
    }
}

// 1:1 port of svt_unpack_and_2bcompress_c (pack_unpack_c.c:78-146).
void unpackAnd2bCompress(const std::uint16_t* in16bBuffer, std::uint32_t in16bStride,
                         std::uint8_t* out8bBuffer, std::uint32_t out8bStride,
                         std::uint8_t* out2bBuffer, std::uint32_t out2bStride,
                         std::uint32_t width, std::uint32_t height) {
    const std::uint32_t wM4 = (width / 4) * 4;
    const std::uint32_t wRem = width - wM4;

    for (std::uint32_t row = 0; row < height; row++) {
        std::uint32_t col = 0;
        for (col = 0; col < wM4; col += 4) {
            std::uint8_t compressedUnpackedPixel = 0;

            // +0
            std::uint16_t inPixel = in16bBuffer[col + 0 + row * in16bStride];
            out8bBuffer[col + 0 + row * out8bStride] = static_cast<std::uint8_t>(inPixel >> 2);
            std::uint8_t tmpPixel = static_cast<std::uint8_t>(inPixel << 6);
            compressedUnpackedPixel |= static_cast<std::uint8_t>((tmpPixel >> 0) & 0xC0);

            // +1
            inPixel = in16bBuffer[col + 1 + row * in16bStride];
            out8bBuffer[col + 1 + row * out8bStride] = static_cast<std::uint8_t>(inPixel >> 2);
            tmpPixel = static_cast<std::uint8_t>(inPixel << 6);
            compressedUnpackedPixel |= static_cast<std::uint8_t>((tmpPixel >> 2) & 0x30);

            // +2
            inPixel = in16bBuffer[col + 2 + row * in16bStride];
            out8bBuffer[col + 2 + row * out8bStride] = static_cast<std::uint8_t>(inPixel >> 2);
            tmpPixel = static_cast<std::uint8_t>(inPixel << 6);
            compressedUnpackedPixel |= static_cast<std::uint8_t>((tmpPixel >> 4) & 0x0C);

            // +3
            inPixel = in16bBuffer[col + 3 + row * in16bStride];
            out8bBuffer[col + 3 + row * out8bStride] = static_cast<std::uint8_t>(inPixel >> 2);
            tmpPixel = static_cast<std::uint8_t>(inPixel << 6);
            compressedUnpackedPixel |= static_cast<std::uint8_t>((tmpPixel >> 6) & 0x03);

            out2bBuffer[col / 4 + row * out2bStride] = compressedUnpackedPixel;
        }

        // Up to 3 pixels may remain.
        if (wRem > 0) {
            std::uint8_t compressedUnpackedPixel = 0;
            std::uint16_t inPixel = in16bBuffer[col + 0 + row * in16bStride];
            out8bBuffer[col + 0 + row * out8bStride] = static_cast<std::uint8_t>(inPixel >> 2);
            std::uint8_t tmpPixel = static_cast<std::uint8_t>(inPixel << 6);
            compressedUnpackedPixel |= static_cast<std::uint8_t>((tmpPixel >> 0) & 0xC0);

            if (wRem > 1) {
                inPixel = in16bBuffer[col + 1 + row * in16bStride];
                out8bBuffer[col + 1 + row * out8bStride] = static_cast<std::uint8_t>(inPixel >> 2);
                tmpPixel = static_cast<std::uint8_t>(inPixel << 6);
                compressedUnpackedPixel |= static_cast<std::uint8_t>((tmpPixel >> 2) & 0x30);
            }
            if (wRem > 2) {
                inPixel = in16bBuffer[col + 2 + row * in16bStride];
                out8bBuffer[col + 2 + row * out8bStride] = static_cast<std::uint8_t>(inPixel >> 2);
                tmpPixel = static_cast<std::uint8_t>(inPixel << 6);
                compressedUnpackedPixel |= static_cast<std::uint8_t>((tmpPixel >> 4) & 0x0C);
            }

            out2bBuffer[col / 4 + row * out2bStride] = compressedUnpackedPixel;
        }
    }
}

// 1:1 port of svt_enc_msb_un_pack2_d (pack_unpack_c.c:180-196).
void msbUnPack2d(const std::uint16_t* in16BitBuffer, std::uint32_t inStride,
                 std::uint8_t* out8BitBuffer, std::uint8_t* outnBitBuffer,
                 std::uint32_t out8Stride, std::uint32_t outnStride,
                 std::uint32_t width, std::uint32_t height) {
    for (std::uint64_t j = 0; j < height; j++) {
        for (std::uint64_t k = 0; k < width; k++) {
            const std::uint16_t inPixel = in16BitBuffer[k + j * inStride];
            out8BitBuffer[k + j * out8Stride] = static_cast<std::uint8_t>(inPixel >> 2);
            if (outnBitBuffer) {
                const std::uint8_t tmpPixel = static_cast<std::uint8_t>(inPixel << 6);
                outnBitBuffer[k + j * outnStride] = tmpPixel;
            }
        }
    }
}

// 1:1 port of svt_convert_8bit_to_16bit_c (pack_unpack_c.c:198-205).
void convert8bitTo16bit(const std::uint8_t* src, std::uint32_t srcStride,
                        std::uint16_t* dst, std::uint32_t dstStride,
                        std::uint32_t width, std::uint32_t height) {
    for (std::uint32_t j = 0; j < height; j++) {
        for (std::uint32_t k = 0; k < width; k++) {
            dst[k + j * dstStride] = src[k + j * srcStride];
        }
    }
}

// 1:1 port of svt_convert_16bit_to_8bit_c (pack_unpack_c.c:207-214).
void convert16bitTo8bit(const std::uint16_t* src, std::uint32_t srcStride,
                        std::uint8_t* dst, std::uint32_t dstStride,
                        std::uint32_t width, std::uint32_t height) {
    for (std::uint32_t j = 0; j < height; j++) {
        for (std::uint32_t k = 0; k < width; k++) {
            dst[k + j * dstStride] = static_cast<std::uint8_t>(src[k + j * srcStride]);
        }
    }
}

// 1:1 port of svt_aom_highbd_blend_a64_vmask_16bit_c
// (blend_a64_mask_c.c:15-37). AOM_BLEND_A64_ROUND_BITS = 6, MAX_ALPHA = 64
// (Codec/definitions.h:1247-1248); ROUND_POWER_OF_TWO(v, 6) =
// (v + (1 << 5)) >> 6 (definitions.h:457).
void blendA64Vmask16bit(std::uint16_t* dst, int dstStride,
                        const std::uint16_t* src0, int src0Stride,
                        const std::uint16_t* src1, int src1Stride,
                        const std::uint8_t* mask, int w, int h) {
    for (int i = 0; i < h; ++i) {
        const int m = mask[i];
        for (int j = 0; j < w; ++j) {
            const int blended = (m * src0[i * src0Stride + j] +
                                 (64 - m) * src1[i * src1Stride + j] + 32) >> 6;
            dst[i * dstStride + j] = static_cast<std::uint16_t>(blended);
        }
    }
}

}
}
