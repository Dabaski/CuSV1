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

}  // namespace motion
