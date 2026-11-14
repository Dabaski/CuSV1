#include <psnr.h>

namespace l14_utility {

// variance - svt_psnr.c:19-:33 verbatim.
std::uint32_t variance(const std::uint8_t* a, std::int32_t a_stride, const std::uint8_t* b, std::int32_t b_stride,
                       std::int32_t w, std::int32_t h) {
    int i, j;
    std::uint32_t sse = 0;

    for (i = 0; i < h; ++i) {
        for (j = 0; j < w; ++j) {
            sse += (unsigned)((a[j] - b[j]) * (a[j] - b[j]));
        }

        a += a_stride;
        b += b_stride;
    }
    return sse;
}

// svt_aom_mse16x16_c - svt_psnr.c:35-:38 verbatim.
std::uint32_t mse16x16(const std::uint8_t* src_ptr, std::int32_t source_stride, const std::uint8_t* ref_ptr,
                       std::int32_t recon_stride) {
    return variance(src_ptr, source_stride, ref_ptr, recon_stride, 16, 16);
}

// svt_aom_get_sse - svt_psnr.c:40-:75 verbatim.
std::int64_t getSse(const std::uint8_t* a, std::int32_t a_stride, const std::uint8_t* b, std::int32_t b_stride,
                    std::int32_t width, std::int32_t height) {
    const std::int32_t dw = width & 15;
    const std::int32_t dh = height & 15;
    std::int64_t total_sse = 0;
    std::uint32_t sse;
    std::int32_t x, y;

    if (dw > 0) {
        sse = variance(&a[width - dw], a_stride, &b[width - dw], b_stride, dw, height);
        total_sse += sse;
    }

    if (dh > 0) {
        sse = variance(&a[(height - dh) * a_stride], a_stride, &b[(height - dh) * b_stride], b_stride, width - dw, dh);
        total_sse += sse;
    }

    for (y = 0; y < height / 16; ++y) {
        const std::uint8_t* pa = a;
        const std::uint8_t* pb = b;
        for (x = 0; x < width / 16; ++x) {
            sse = mse16x16(pa, a_stride, pb, b_stride);

            total_sse += sse;

            pa += 16;
            pb += 16;
        }

        a += 16 * a_stride;
        b += 16 * b_stride;
    }

    return total_sse;
}

}  // namespace l14_utility
