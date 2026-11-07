#include "motion.h"

namespace motion {

std::uint32_t sad8x8(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                     std::uint32_t refStride) {
    std::uint32_t sadBlock = 0;

    for (std::uint32_t r = 0; r < 8; ++r) {
        for (std::uint32_t c = 0; c < 8; ++c) {
            const int d = (int)src[c] - (int)ref[c];
            sadBlock += (std::uint32_t)(d < 0 ? -d : d);
        }
        src += srcStride;
        ref += refStride;
    }

    return sadBlock;
}

std::string sad8x8CuSource() {
    return R"CUDA(
extern "C" __global__ void sad8x8_kernel(const unsigned char* src, const unsigned int* srcStride,
                                         const unsigned char* ref, const unsigned int* refStride,
                                         unsigned int* out) {
    unsigned int sad = 0;
    for (unsigned int r = 0; r < 8; ++r) {
        for (unsigned int c = 0; c < 8; ++c) {
            int d = (int)src[c] - (int)ref[c];
            if (d < 0) {
                d = -d;
            }
            sad += (unsigned int)d;
        }
        src += *srcStride;
        ref += *refStride;
    }
    out[0] = sad;
}
)CUDA";
}

// svt_nxm_sad_kernel_helper_c (C_DEFAULT/compute_sad_c.c:21) at 16x16 (C7)
std::uint32_t sad16x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride) {
    std::uint32_t sadBlock = 0;

    for (std::uint32_t r = 0; r < 16; ++r) {
        for (std::uint32_t c = 0; c < 16; ++c) {
            const int d = (int)src[c] - (int)ref[c];
            sadBlock += (std::uint32_t)(d < 0 ? -d : d);
        }
        src += srcStride;
        ref += refStride;
    }

    return sadBlock;
}

// svt_nxm_sad_kernel_helper_c (C_DEFAULT/compute_sad_c.c:21) at 32x32 (L5)
std::uint32_t sad32x32(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride) {
    std::uint32_t sadBlock = 0;

    for (std::uint32_t r = 0; r < 32; ++r) {
        for (std::uint32_t c = 0; c < 32; ++c) {
            const int d = (int)src[c] - (int)ref[c];
            sadBlock += (std::uint32_t)(d < 0 ? -d : d);
        }
        src += srcStride;
        ref += refStride;
    }

    return sadBlock;
}

// svt_nxm_sad_kernel_helper_c (C_DEFAULT/compute_sad_c.c:21) at 64x64 (L9)
std::uint32_t sad64x64(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride) {
    std::uint32_t sadBlock = 0;

    for (std::uint32_t r = 0; r < 64; ++r) {
        for (std::uint32_t c = 0; c < 64; ++c) {
            const int d = (int)src[c] - (int)ref[c];
            sadBlock += (std::uint32_t)(d < 0 ? -d : d);
        }
        src += srcStride;
        ref += refStride;
    }

    return sadBlock;
}

// svt_nxm_sad_kernel_helper_c (C_DEFAULT/compute_sad_c.c:21) at 4x4
std::uint32_t sad4x4(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                     std::uint32_t refStride) {
    std::uint32_t sad = 0;

    for (std::uint32_t r = 0; r < 4; ++r) {
        for (std::uint32_t c = 0; c < 4; ++c) {
            const int d = (int)src[c] - (int)ref[c];
            sad += (std::uint32_t)(d < 0 ? -d : d);
        }
        src += srcStride;
        ref += refStride;
    }

    return sad;
}

std::string sad4x4CuSource() {
    return R"CUDA(
extern "C" __global__ void sad4x4_kernel(const unsigned char* src, const unsigned int* srcStride,
                                         const unsigned char* ref, const unsigned int* refStride,
                                         unsigned int* out) {
    unsigned int sad = 0;
    for (unsigned int r = 0; r < 4; ++r) {
        for (unsigned int c = 0; c < 4; ++c) {
            int d = (int)src[c] - (int)ref[c];
            if (d < 0) {
                d = -d;
            }
            sad += (unsigned int)d;
        }
        src += *srcStride;
        ref += *refStride;
    }
    out[0] = sad;
}
)CUDA";
}

// svt_aom_sad_16b_kernel_c (C_DEFAULT/compute_sad_c.c:42-61, verbatim):
// EB_ABS_DIFF (utility.h:105) over width x height, strided both sides.
std::uint32_t sad16b(const std::uint16_t* src, std::uint32_t srcStride, const std::uint16_t* ref,
                     std::uint32_t refStride, std::uint32_t height, std::uint32_t width) {
    std::uint32_t sad = 0;

    for (std::uint32_t y = 0; y < height; y++) {
        for (std::uint32_t x = 0; x < width; x++) {
            const std::uint16_t a = src[x];
            const std::uint16_t b = ref[x];
            sad += a > b ? (a - b) : (b - a);
        }
        src += srcStride;
        ref += refStride;
    }

    return sad;
}

// svt_sad_loop_kernel_c (C_DEFAULT/compute_sad_c.c:63-101, verbatim).
// The ref advance per search row is src_stride_raw (:97 and the skip
// branch :81); skip_search_line skips EVEN search rows only for
// block_width == 16 && block_height <= 16 (:79-84). Updates are
// strictly-less, so the first minimum in scan order wins (:90).
void sadLoopKernel(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                   std::uint32_t refStride, std::uint32_t blockHeight, std::uint32_t blockWidth,
                   std::uint64_t* bestSad, std::int16_t* xSearchCenter, std::int16_t* ySearchCenter,
                   std::uint32_t srcStrideRaw, std::uint8_t skipSearchLine, std::int16_t searchAreaWidth,
                   std::int16_t searchAreaHeight) {
    *bestSad = 0xffffff;

    for (std::int16_t ySearchIndex = 0; ySearchIndex < searchAreaHeight; ySearchIndex++) {
        if (blockWidth == 16 && blockHeight <= 16 && skipSearchLine) {
            if ((ySearchIndex & 1) == 0) {
                ref += srcStrideRaw;
                continue;
            }
        }
        for (std::int16_t xSearchIndex = 0; xSearchIndex < searchAreaWidth; xSearchIndex++) {
            std::uint32_t sad = 0;
            const std::uint8_t* r = ref + xSearchIndex;
            for (std::uint32_t y = 0; y < blockHeight; y++) {
                for (std::uint32_t x = 0; x < blockWidth; x++) {
                    const std::uint8_t a = src[y * srcStride + x];
                    const std::uint8_t b = r[y * refStride + x];
                    sad += a > b ? (a - b) : (b - a);
                }
            }

            if (sad < *bestSad) {
                *bestSad        = sad;
                *xSearchCenter = xSearchIndex;
                *ySearchCenter = ySearchIndex;
            }
        }

        ref += srcStrideRaw;
    }
}

// obmc_sad (C_DEFAULT/sad_av1.c:18-34, verbatim): pre = predictor being
// evaluated; wsrc = target weighted prediction (*4096); mask = 2D
// weights scaled by 4096. sad += ROUND_POWER_OF_TWO(abs(wsrc - pre*mask), 12).
std::uint32_t obmcSad(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc, const std::int32_t* mask,
                      int width, int height) {
    std::uint32_t sad = 0;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int d = wsrc[x] - pre[x] * mask[x];
            sad += static_cast<std::uint32_t>(((d < 0 ? -d : d) + 2048) >> 12);
        }

        pre += preStride;
        wsrc += width;
        mask += width;
    }

    return sad;
}

// SW1: svt_nxm_sad_kernel_helper_c (C_DEFAULT/compute_sad_c.c:21-40,
// verbatim). The generic NxM SAD the sadMxN macro instances forward to.
std::uint32_t nxmSadKernelHelper(const std::uint8_t* src, std::uint32_t srcStride,
                                 const std::uint8_t* ref, std::uint32_t refStride,
                                 std::uint32_t height, std::uint32_t width) {
    std::uint32_t sad = 0;

    for (std::uint32_t y = 0; y < height; y++) {
        for (std::uint32_t x = 0; x < width; x++) {
            const int d = (int)src[x] - (int)ref[x];
            sad += (std::uint32_t)(d < 0 ? -d : d);
        }
        src += srcStride;
        ref += refStride;
    }

    return sad;
}

// SW1: the sadMxN macro instance at 4x8 (compute_sad_c.c:175): the nxm
// helper at height 8, width 4.
std::uint32_t sad4x8(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                     std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 8, 4);
}

// SW1: the sadMxN macro instance at 8x4 (compute_sad_c.c:171): the nxm
// helper at height 4, width 8.
std::uint32_t sad8x4(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                     std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 4, 8);
}

// SW1: the sadMxN macro instance at 128x128 (compute_sad_c.c:119).
std::uint32_t sad128x128(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                         std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 128, 128);
}

// SW1: the remaining sadMxN macro instances (compute_sad_c.c:123, :127,
// :135, :139, :147, :151, :159, :163, :182, :184, :186, :188, :190, :192).
// Each is the macro body: the nxm helper at (height=n, width=m).
std::uint32_t sad128x64(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                        std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 64, 128);
}

std::uint32_t sad64x128(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                        std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 128, 64);
}

std::uint32_t sad64x32(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 32, 64);
}

std::uint32_t sad32x64(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 64, 32);
}

std::uint32_t sad32x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 16, 32);
}

std::uint32_t sad16x32(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 32, 16);
}

std::uint32_t sad16x8(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                      std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 8, 16);
}

std::uint32_t sad8x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                      std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 16, 8);
}

std::uint32_t sad4x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                      std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 16, 4);
}

std::uint32_t sad16x4(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                      std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 4, 16);
}

std::uint32_t sad8x32(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                      std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 32, 8);
}

std::uint32_t sad32x8(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                      std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 8, 32);
}

std::uint32_t sad16x64(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 64, 16);
}

std::uint32_t sad64x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride) {
    return nxmSadKernelHelper(src, srcStride, ref, refStride, 16, 64);
}

// SW1: the sadMxNx4D macro instances (compute_sad_c.c:109-116 + the
// instance lines :118-193). Each is the macro body: sad_array[i] =
// the sadMxN instance on ref_array[i], i = 0..3, in index order.
void sad128x128x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                   std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad128x128(src, srcStride, refArray[i], refStride);
}

void sad128x64x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                  std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad128x64(src, srcStride, refArray[i], refStride);
}

void sad64x128x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                  std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad64x128(src, srcStride, refArray[i], refStride);
}

void sad64x64x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad64x64(src, srcStride, refArray[i], refStride);
}

void sad64x32x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad64x32(src, srcStride, refArray[i], refStride);
}

void sad32x64x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad32x64(src, srcStride, refArray[i], refStride);
}

void sad32x32x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad32x32(src, srcStride, refArray[i], refStride);
}

void sad32x16x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad32x16(src, srcStride, refArray[i], refStride);
}

void sad16x32x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad16x32(src, srcStride, refArray[i], refStride);
}

void sad16x16x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad16x16(src, srcStride, refArray[i], refStride);
}

void sad16x8x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad16x8(src, srcStride, refArray[i], refStride);
}

void sad8x16x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad8x16(src, srcStride, refArray[i], refStride);
}

void sad8x8x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
               std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad8x8(src, srcStride, refArray[i], refStride);
}

void sad8x4x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
               std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad8x4(src, srcStride, refArray[i], refStride);
}

void sad4x8x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
               std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad4x8(src, srcStride, refArray[i], refStride);
}

void sad4x4x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
               std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad4x4(src, srcStride, refArray[i], refStride);
}

void sad4x16x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad4x16(src, srcStride, refArray[i], refStride);
}

void sad16x4x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad16x4(src, srcStride, refArray[i], refStride);
}

void sad8x32x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad8x32(src, srcStride, refArray[i], refStride);
}

void sad32x8x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad32x8(src, srcStride, refArray[i], refStride);
}

void sad16x64x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad16x64(src, srcStride, refArray[i], refStride);
}

void sad64x16x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray) {
    for (int i = 0; i < 4; ++i) sadArray[i] = sad64x16(src, srcStride, refArray[i], refStride);
}

// SW1: the OBMCSADMxN macro instances (sad_av1.c:36-40 + :43-64). Each
// is the macro body: obmc_sad(ref, ref_stride, wsrc, mask, m, n) - the
// landed obmcSad core at the instance's (width, height).
std::uint32_t obmcSad128x128(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                             const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 128, 128);
}

std::uint32_t obmcSad128x64(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                            const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 128, 64);
}

std::uint32_t obmcSad64x128(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                            const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 64, 128);
}

std::uint32_t obmcSad64x64(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 64, 64);
}

std::uint32_t obmcSad64x32(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 64, 32);
}

std::uint32_t obmcSad32x64(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 32, 64);
}

std::uint32_t obmcSad32x32(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 32, 32);
}

std::uint32_t obmcSad32x16(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 32, 16);
}

std::uint32_t obmcSad16x32(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 16, 32);
}

std::uint32_t obmcSad16x16(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 16, 16);
}

std::uint32_t obmcSad16x8(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                          const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 16, 8);
}

std::uint32_t obmcSad8x16(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                          const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 8, 16);
}

std::uint32_t obmcSad8x8(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                         const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 8, 8);
}

std::uint32_t obmcSad8x4(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                         const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 8, 4);
}

std::uint32_t obmcSad4x8(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                         const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 4, 8);
}

std::uint32_t obmcSad4x4(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                         const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 4, 4);
}

std::uint32_t obmcSad4x16(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                          const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 4, 16);
}

std::uint32_t obmcSad16x4(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                          const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 16, 4);
}

std::uint32_t obmcSad8x32(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                          const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 8, 32);
}

std::uint32_t obmcSad32x8(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                          const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 32, 8);
}

std::uint32_t obmcSad16x64(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 16, 64);
}

std::uint32_t obmcSad64x16(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask) {
    return obmcSad(ref, refStride, wsrc, mask, 64, 16);
}

}  // namespace motion
