#include "subpel.h"

#include <analysis.h>

namespace analysis {
namespace subpel {

// 1:1 port of aom_var_filter_block2d_bil_first_pass_c (C_DEFAULT/variance.c:
// 29-44). uint8 -> uint16, ROUND_POWER_OF_TWO(v, 7) = (v + 64) >> 7
// (definitions.h:457).
void filterBlock2dBilFirstPass(const std::uint8_t* a, std::uint16_t* b,
                               unsigned int srcPixelsPerLine, unsigned int pixelStep,
                               unsigned int outputHeight, unsigned int outputWidth,
                               const std::uint8_t* filter) {
    for (unsigned int i = 0; i < outputHeight; ++i) {
        for (unsigned int j = 0; j < outputWidth; ++j) {
            b[j] = static_cast<std::uint16_t>(
                (static_cast<int>(a[0]) * filter[0] + static_cast<int>(a[pixelStep]) * filter[1] + 64) >> 7);
            ++a;
        }

        a += srcPixelsPerLine - outputWidth;
        b += outputWidth;
    }
}

// 1:1 port of aom_var_filter_block2d_bil_second_pass_c (C_DEFAULT/variance.c:
// 55-69). uint16 -> uint8, the same (sum + 64) >> 7 form (:62).
void filterBlock2dBilSecondPass(const std::uint16_t* a, std::uint8_t* b,
                                unsigned int srcPixelsPerLine, unsigned int pixelStep,
                                unsigned int outputHeight, unsigned int outputWidth,
                                const std::uint8_t* filter) {
    for (unsigned int i = 0; i < outputHeight; ++i) {
        for (unsigned int j = 0; j < outputWidth; ++j) {
            b[j] = static_cast<std::uint8_t>(
                (static_cast<int>(a[0]) * filter[0] + static_cast<int>(a[pixelStep]) * filter[1] + 64) >> 7);
            ++a;
        }

        a += srcPixelsPerLine - outputWidth;
        b += outputWidth;
    }
}

// 1:1 port of the SUBPIX_VAR(W, H) macro (C_DEFAULT/variance.c:192-202).
// First pass: H+1 rows, W cols, pixel_step 1, xoffset filter row (:198).
// Second pass: over the packed fdata3 (stride W, step W, H rows, W cols),
// yoffset filter row (:199). Then the plain VAR instance on temp2 (:201).
#define SUB1_SUBPIX_VAR(W, H)                                                                          \
    std::uint32_t subPixelVariance##W##x##H(const std::uint8_t* a, int aStride, int xoffset,           \
                                            int yoffset, const std::uint8_t* b, int bStride,           \
                                            std::uint32_t* sse) {                                      \
        std::uint16_t fdata3[(H + 1) * W];                                                             \
        std::uint8_t temp2[H * W];                                                                     \
        filterBlock2dBilFirstPass(a, fdata3, static_cast<unsigned int>(aStride), 1, H + 1, W,          \
                                  kBilinearFilters2t[xoffset]);                                        \
        filterBlock2dBilSecondPass(fdata3, temp2, W, W, H, W, kBilinearFilters2t[yoffset]);            \
        return analysis::variance##W##x##H(temp2, W, b, bStride, sse);                                 \
    }

SUB1_SUBPIX_VAR(4, 4)
SUB1_SUBPIX_VAR(8, 4)

// The 16x16 and 8x8 SUBPIX instances: the reference chain calls
// svt_aom_variance16x16_c / svt_aom_variance8x8_c (:201), but the landed
// analysis:: forms for those two sizes are the AN8 const-128 offs-folded
// shapes (ref_stride 0 hardwired), which cannot express an arbitrary b.
// DEVIATION (named): this port inlines the VAR instance body instead -
// varianceCore + the :189 return - which is byte-for-byte what the landed
// general instances (e.g. variance4x4, analysis.cpp:294-299) run.
#define SUB1_SUBPIX_VAR_CORE(W, H)                                                                     \
    std::uint32_t subPixelVariance##W##x##H(const std::uint8_t* a, int aStride, int xoffset,           \
                                            int yoffset, const std::uint8_t* b, int bStride,           \
                                            std::uint32_t* sse) {                                      \
        std::uint16_t fdata3[(H + 1) * W];                                                             \
        std::uint8_t temp2[H * W];                                                                     \
        filterBlock2dBilFirstPass(a, fdata3, static_cast<unsigned int>(aStride), 1, H + 1, W,          \
                                  kBilinearFilters2t[xoffset]);                                        \
        filterBlock2dBilSecondPass(fdata3, temp2, W, W, H, W, kBilinearFilters2t[yoffset]);            \
        int sum;                                                                                       \
        analysis::varianceCore(temp2, W, b, bStride, W, H, sse, &sum);                                 \
        return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (W * H));       \
    }

SUB1_SUBPIX_VAR_CORE(16, 16)
SUB1_SUBPIX_VAR_CORE(8, 8)

// The remaining VARIANCES(W, H) SUBPIX instances, in the reference's own
// order (variance.c:208-:229).
SUB1_SUBPIX_VAR(128, 128)
SUB1_SUBPIX_VAR(128, 64)
SUB1_SUBPIX_VAR(64, 128)
SUB1_SUBPIX_VAR(64, 64)
SUB1_SUBPIX_VAR(64, 32)
SUB1_SUBPIX_VAR(32, 64)
SUB1_SUBPIX_VAR(32, 32)
SUB1_SUBPIX_VAR(32, 16)
SUB1_SUBPIX_VAR(16, 32)
SUB1_SUBPIX_VAR(16, 8)
SUB1_SUBPIX_VAR(8, 16)
SUB1_SUBPIX_VAR(4, 8)
SUB1_SUBPIX_VAR(4, 16)
SUB1_SUBPIX_VAR(16, 4)
SUB1_SUBPIX_VAR(8, 32)
SUB1_SUBPIX_VAR(32, 8)
SUB1_SUBPIX_VAR(16, 64)
SUB1_SUBPIX_VAR(64, 16)

// 1:1 port of the obmc_variance core (C_DEFAULT/variance.c:231-249).
// ROUND_POWER_OF_TWO_SIGNED(v, 12): v < 0 ? -((-v + 2048) >> 12)
// : (v + 2048) >> 12 (definitions.h:460-461).
void obmcVariance(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc,
                  const std::int32_t* mask, int w, int h, std::uint32_t* sse, int* sum) {
    *sse = 0;
    *sum = 0;

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            const int v = wsrc[j] - pre[j] * mask[j];
            const int diff = v < 0 ? -((-v + 2048) >> 12) : (v + 2048) >> 12;
            *sum += diff;
            *sse += static_cast<std::uint32_t>(diff * diff);
        }

        pre += preStride;
        wsrc += w;
        mask += w;
    }
}

// 1:1 port of the OBMC_VAR(W, H) macro (C_DEFAULT/variance.c:251-257).
#define SUB1_OBMC_VAR(W, H)                                                                            \
    std::uint32_t obmcVariance##W##x##H(const std::uint8_t* pre, int preStride,                        \
                                        const std::int32_t* wsrc, const std::int32_t* mask,            \
                                        std::uint32_t* sse) {                                          \
        int sum;                                                                                       \
        obmcVariance(pre, preStride, wsrc, mask, W, H, sse, &sum);               \
        return *sse - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (W * H));       \
    }

SUB1_OBMC_VAR(4, 4)

// 1:1 port of the OBMC_SUBPIX_VAR(W, H) macro (C_DEFAULT/variance.c:259-274):
// the SUBPIX preamble on pre, then the OBMC_VAR instance on temp2 (:273).
#define SUB1_OBMC_SUBPIX_VAR(W, H)                                                                     \
    std::uint32_t obmcSubPixelVariance##W##x##H(const std::uint8_t* pre, int preStride,                \
                                                int xoffset, int yoffset, const std::int32_t* wsrc,    \
                                                const std::int32_t* mask, std::uint32_t* sse) {        \
        std::uint16_t fdata3[(H + 1) * W];                                                             \
        std::uint8_t temp2[H * W];                                                                     \
        filterBlock2dBilFirstPass(pre, fdata3, static_cast<unsigned int>(preStride), 1, H + 1, W,      \
                                  kBilinearFilters2t[xoffset]);                                        \
        filterBlock2dBilSecondPass(fdata3, temp2, W, W, H, W, kBilinearFilters2t[yoffset]);            \
        return obmcVariance##W##x##H(temp2, W, wsrc, mask, sse);                                       \
    }

SUB1_OBMC_SUBPIX_VAR(4, 4)

// The OBMC instance list, in the reference's own order (variance.c:276-:335).
SUB1_OBMC_VAR(4, 8)
SUB1_OBMC_SUBPIX_VAR(4, 8)
SUB1_OBMC_VAR(8, 4)
SUB1_OBMC_SUBPIX_VAR(8, 4)
SUB1_OBMC_VAR(8, 8)
SUB1_OBMC_SUBPIX_VAR(8, 8)
SUB1_OBMC_VAR(8, 16)
SUB1_OBMC_SUBPIX_VAR(8, 16)
SUB1_OBMC_VAR(16, 8)
SUB1_OBMC_SUBPIX_VAR(16, 8)
SUB1_OBMC_VAR(16, 16)
SUB1_OBMC_SUBPIX_VAR(16, 16)
SUB1_OBMC_VAR(16, 32)
SUB1_OBMC_SUBPIX_VAR(16, 32)
SUB1_OBMC_VAR(32, 16)
SUB1_OBMC_SUBPIX_VAR(32, 16)
SUB1_OBMC_VAR(32, 32)
SUB1_OBMC_SUBPIX_VAR(32, 32)
SUB1_OBMC_VAR(32, 64)
SUB1_OBMC_SUBPIX_VAR(32, 64)
SUB1_OBMC_VAR(64, 32)
SUB1_OBMC_SUBPIX_VAR(64, 32)
SUB1_OBMC_VAR(64, 64)
SUB1_OBMC_SUBPIX_VAR(64, 64)
SUB1_OBMC_VAR(64, 128)
SUB1_OBMC_SUBPIX_VAR(64, 128)
SUB1_OBMC_VAR(128, 64)
SUB1_OBMC_SUBPIX_VAR(128, 64)
SUB1_OBMC_VAR(128, 128)
SUB1_OBMC_SUBPIX_VAR(128, 128)
SUB1_OBMC_VAR(4, 16)
SUB1_OBMC_SUBPIX_VAR(4, 16)
SUB1_OBMC_VAR(16, 4)
SUB1_OBMC_SUBPIX_VAR(16, 4)
SUB1_OBMC_VAR(8, 32)
SUB1_OBMC_SUBPIX_VAR(8, 32)
SUB1_OBMC_VAR(32, 8)
SUB1_OBMC_SUBPIX_VAR(32, 8)
SUB1_OBMC_VAR(16, 64)
SUB1_OBMC_SUBPIX_VAR(16, 64)
SUB1_OBMC_VAR(64, 16)
SUB1_OBMC_SUBPIX_VAR(64, 16)

}
}
