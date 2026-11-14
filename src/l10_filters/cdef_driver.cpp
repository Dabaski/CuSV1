#include <cmath>
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

} // namespace cdef_driver
} // namespace l10_filters
