#include <cmath>
#include <cstring>
#include <cstdint>

#include "cdef_driver.h"

namespace l10_filters {
namespace cdef_driver {

// definitions.h:687-style clamp (the reference uses the AOM clamp macro).
static inline std::int32_t clampI32(std::int32_t v, std::int32_t low, std::int32_t high) {
    return v < low ? low : (v > high ? high : v);
}

// compute_cdef_dist (cdef_process.c:266-:290). 1:1 dispatch; the dst
// doffset is a pixel offset (element offset at 16 bit, byte offset at 8).
std::uint64_t computeCdefDist(const std::uint8_t* dst, std::int32_t doffset, std::int32_t dstride,
    const std::uint8_t* src, const CdefList* dlist, std::int32_t cdefCount, CdefBlockSize bsize,
    std::int32_t coeffShift, std::uint8_t subsamplingFactor, bool is16bit) {
    std::uint64_t currMse = 0;
    if (is16bit) {
        currMse = cdefComputeDist16bit(reinterpret_cast<const std::uint16_t*>(dst) + doffset, dstride,
            reinterpret_cast<const std::uint16_t*>(src), dlist, cdefCount, bsize, coeffShift, subsamplingFactor);
    } else {
        currMse = cdefComputeDist8bit(dst + doffset, dstride, src, dlist, cdefCount, bsize, coeffShift,
            subsamplingFactor);
    }
    return currMse;
}

// svt_pick_cdef_from_qp (enc_cdef.c:823-:873). 1:1: the screen branch
// truncates (plain (int32_t) casts), the inter/intra branches roundf; the
// qtx call site is threaded (AcQuantQtxFn).
void pickCdefFromQp(const PickQpParams& p, std::int32_t* predYStrength, std::int32_t* predUvStrength) {
    const std::int32_t q = p.acQuantQtx(p.baseQIdx, 0, p.bitDepth) >> (p.bitDepth - 8);

    std::int32_t yF1 = 0, yF2 = 0;
    std::int32_t uvF1 = 0, uvF2 = 0;

    const bool isIntra = p.isKeyFrame || p.intraOnly;

    if (p.isScreenContent) {
        yF1 = (std::int32_t)(5.88217781e-06 * q * q + 6.10391455e-03 * q + 9.95043102e-02);

        yF2 = (std::int32_t)(-7.79934857e-06 * q * q + 6.58957830e-03 * q + 8.81045025e-01);

        uvF1 = (std::int32_t)(-6.79500136e-06 * q * q + 1.02695586e-02 * q + 1.36126802e-01);

        uvF2 = (std::int32_t)(-9.99613695e-08 * q * q - 1.79361339e-05 * q + 1.17022324e+0);
    } else if (!isIntra) {
        yF1 = (std::int32_t)roundf(q * q * -0.0000023593946f + q * 0.0068615186f + 0.02709886f);

        yF2 = (std::int32_t)roundf(q * q * -0.00000057629734f + q * 0.0013993345f + 0.03831067f);

        uvF1 = (std::int32_t)roundf(q * q * -0.0000007095069f + q * 0.0034628846f + 0.00887099f);

        uvF2 = (std::int32_t)roundf(q * q * 0.00000023874085f + q * 0.00028223585f + 0.05576307f);
    } else { // Intra
        yF1 = (std::int32_t)roundf(q * q * 0.0000033731974f + q * 0.008070594f + 0.0187634f);

        yF2 = (std::int32_t)roundf(q * q * 0.0000029167343f + q * 0.0027798624f + 0.0079405f);

        uvF1 = (std::int32_t)roundf(q * q * -0.0000130790995f + q * 0.012892405f - 0.00748388f);

        uvF2 = (std::int32_t)roundf(q * q * 0.0000032651783f + q * 0.00035520183f + 0.00228092f);
    }

    // Clamp to AV1 limits
    yF1 = clampI32(yF1, 0, 15);
    yF2 = clampI32(yF2, 0, 3);
    uvF1 = clampI32(uvF1, 0, 15);
    uvF2 = clampI32(uvF2, 0, 3);

    // Pack primary + secondary
    *predYStrength = yF1 * kCdefSecStrengths + yF2;
    *predUvStrength = uvF1 * kCdefSecStrengths + uvF2;
}

// propagate_cdef_strength (enc_cdef.c:875-:895). 1:1 over the flat grid;
// MI_SIZE_64X64 = 16 (definitions.h:376) is the 64x64 mi step.
void propagateCdefStrength(CdefMb* miGrid, std::int32_t miStride, std::int32_t sbIndex, int bsize,
    std::int8_t strength) {
    constexpr std::int32_t kMiSize64x64 = 16;
    miGrid[sbIndex].cdefStrength = strength;
    switch (bsize) {
    case 15: // BLOCK_128X128
        miGrid[sbIndex + kMiSize64x64].cdefStrength = strength;
        miGrid[sbIndex + kMiSize64x64 * miStride].cdefStrength = strength;
        miGrid[sbIndex + kMiSize64x64 * miStride + kMiSize64x64].cdefStrength = strength;
        break;
    case 14: // BLOCK_128X64
        miGrid[sbIndex + kMiSize64x64].cdefStrength = strength;
        break;
    case 13: // BLOCK_64X128
        miGrid[sbIndex + kMiSize64x64 * miStride].cdefStrength = strength;
        break;
    default:
        break;
    }
}

// copy_recon_enc (cdef_process.c:46-:150). 1:1; the EB_CALLOC_ALIGNED_
// ARRAY allocation wall is threaded as bufferAlloc.
void copyReconEnc(const CdefScsWalls& scs, const CdefPic& src, CdefPic& dst, int numPlanes, bool skipCopy,
    std::uint8_t* bufferAlloc) {
    dst.border = src.border;
    dst.width = src.width;
    dst.height = src.height;
    dst.maxWidth = src.maxWidth;
    dst.maxHeight = src.maxHeight;
    dst.bitDepth = src.bitDepth;
    dst.colorFormat = src.colorFormat;

    dst.stride[0] = src.stride[0];
    dst.stride[1] = src.stride[1];
    dst.stride[2] = src.stride[2];

    dst.lumaSize = src.lumaSize;
    dst.chromaSize = src.chromaSize;
    dst.packedFlag = src.packedFlag;

    dst.strideBitInc[0] = src.strideBitInc[0];
    dst.strideBitInc[1] = src.strideBitInc[1];
    dst.strideBitInc[2] = src.strideBitInc[2];

    dst.bufferEnableMask = scs.monoChrome ? kPicBufLumaMask : kPicBufFullMask;

    const int ssX = scs.subsamplingX;
    const int ssY = scs.subsamplingY;
    const std::uint32_t bytesPerPixel = scs.is16bitPipeline ? 2 : 1;

    // Get frame size to alloc
    std::uint32_t allocSz = 0;
    std::uint32_t bufferSize[3] = {0};
    if (dst.bufferEnableMask & kPicBufYFlag) {
        allocSz += bufferSize[0] = dst.lumaSize * bytesPerPixel;
    }

    if (dst.bufferEnableMask & kPicBufCbFlag) {
        allocSz += bufferSize[1] = dst.chromaSize * bytesPerPixel;
    }

    if (dst.bufferEnableMask & kPicBufCrFlag) {
        allocSz += bufferSize[2] = dst.chromaSize * bytesPerPixel;
    }

    dst.bufferAlloc = bufferAlloc;
    dst.bufferAllocSz = allocSz;
    std::uint32_t assignedSpace = 0;
    if (dst.bufferEnableMask & kPicBufYFlag) {
        dst.buffer[0] = dst.bufferAlloc + (dst.border + (dst.stride[0] * dst.border)) * bytesPerPixel;
        assignedSpace += bufferSize[0];
    } else {
        dst.buffer[0] = nullptr;
    }

    if (dst.bufferEnableMask & kPicBufCbFlag) {
        dst.buffer[1] = dst.bufferAlloc + assignedSpace +
            ((dst.border >> ssX) + (dst.stride[1] * (dst.border >> ssY))) * bytesPerPixel;
        assignedSpace += bufferSize[1];
    } else {
        dst.buffer[1] = nullptr;
    }

    if (dst.bufferEnableMask & kPicBufCrFlag) {
        dst.buffer[2] = dst.bufferAlloc + assignedSpace +
            ((dst.border >> ssX) + (dst.stride[2] * (dst.border >> ssY))) * bytesPerPixel;
        assignedSpace += bufferSize[2];
    } else {
        dst.buffer[2] = nullptr;
    }

    const int useHighbd = scs.is16bitPipeline ? 1 : 0;

    if (!skipCopy) {
        for (int plane = 0; plane < numPlanes; ++plane) {
            const std::uint8_t* srcBuf = src.buffer[plane];
            std::uint8_t* dstBuf = dst.buffer[plane];
            const std::int32_t srcStride = src.stride[plane];
            const std::int32_t dstStride = dst.stride[plane];

            const int subX = plane ? scs.subsamplingX : 0;
            const int subY = plane ? scs.subsamplingY : 0;

            const std::int32_t height = (src.height + subY) >> subY;
            for (int row = 0; row < height; ++row) {
                memcpy(dstBuf, srcBuf, ((src.width + subX) >> subX) * sizeof(*srcBuf) << useHighbd);
                srcBuf += srcStride << useHighbd;
                dstBuf += dstStride << useHighbd;
            }
        }
    }
}

// set_unscaled_input_16bit (cdef_process.c:31-:44). 1:1 dispatch; the
// three pic-buffer callees are threaded.
void setUnscaledInput16bit(const CdefScsWalls& scs, const CdefPic& input, CdefPic& output, CopyBufferInfoFn copyInfo,
    Convert8To16Fn convert8To16, Pack2dPicFn pack2dPic) {
    copyInfo(input, output);
    if (input.bitDepth == 8) {
        convert8To16(input, output, scs.subsamplingX, scs.subsamplingY);
    } else {
        pack2dPic(input, output);
    }
}

} // namespace cdef_driver
} // namespace l10_filters
