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

}  // namespace motion