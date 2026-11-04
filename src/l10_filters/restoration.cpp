#include <cassert>
#include <cstring>

#include "restoration.h"

namespace l10_filters {

namespace restoration {

// sgr_params (restoration.c:31-49): 16 rows indexed by sgr_params_idx.
// Rows 10-13 have r[0] == 0 (first pass skipped); rows 14-15 have r[1] == 0
// (second pass skipped) with s == -1 (invalid).
const SgrParamsType sgrParams[16] = {
    {{2, 1}, {140, 3236}}, // 0  { 2, 12, 1, 4  }
    {{2, 1}, {112, 2158}}, // 1  { 2, 15, 1, 6  }
    {{2, 1}, {93, 1618}},  // 2  { 2, 18, 1, 8  }
    {{2, 1}, {80, 1438}},  // 3  { 2, 21, 1, 9  }
    {{2, 1}, {70, 1295}},  // 4  { 2, 24, 1, 10 }
    {{2, 1}, {58, 1177}},  // 5  { 2, 29, 1, 11 }
    {{2, 1}, {47, 1079}},  // 6  { 2, 36, 1, 12 }
    {{2, 1}, {37, 996}},   // 7  { 2, 45, 1, 13 }
    {{2, 1}, {30, 925}},   // 8  { 2, 56, 1, 14 }
    {{2, 1}, {25, 863}},   // 9  { 2, 68, 1, 15 }
    {{0, 1}, {-1, 2589}},  // 10 { 0, 0,  1, 5  }
    {{0, 1}, {-1, 1618}},  // 11 { 0, 0,  1, 8  }
    {{0, 1}, {-1, 1177}},  // 12 { 0, 0,  1, 11 }
    {{0, 1}, {-1, 925}},   // 13 { 0, 0,  1, 14 }
    {{2, 0}, {56, -1}},    // 14 { 2, 30, 0, 0  }
    {{2, 0}, {22, -1}},    // 15 { 2, 75, 0, 0  }
};

// boxsum1 (restoration.c:430-:495). 1:1: the vertical 3-tap walk writes
// dst with 2-sums at the outermost rows, then the horizontal 3-tap walk
// truncates the outermost cols; the sqr flag squares each input first.
void boxsum1(std::int32_t* src, std::int32_t width, std::int32_t height, std::int32_t src_stride, std::int32_t sqr,
             std::int32_t* dst, std::int32_t dst_stride) {
    std::int32_t i, j, a, b, c;

    // Vertical sum over 3-pixel regions, from src into dst.
    if (!sqr) {
        for (j = 0; j < width; ++j) {
            a = src[j];
            b = src[src_stride + j];
            c = src[2 * src_stride + j];

            dst[j] = a + b;
            for (i = 1; i < height - 2; ++i) {
                dst[i * dst_stride + j] = a + b + c;
                a                       = b;
                b                       = c;
                c                       = src[(i + 2) * src_stride + j];
            }
            dst[i * dst_stride + j]       = a + b + c;
            dst[(i + 1) * dst_stride + j] = b + c;
        }
    } else {
        for (j = 0; j < width; ++j) {
            a = src[j] * src[j];
            b = src[src_stride + j] * src[src_stride + j];
            c = src[2 * src_stride + j] * src[2 * src_stride + j];

            dst[j] = a + b;
            for (i = 1; i < height - 2; ++i) {
                dst[i * dst_stride + j] = a + b + c;
                a                       = b;
                b                       = c;
                c                       = src[(i + 2) * src_stride + j] * src[(i + 2) * src_stride + j];
            }
            dst[i * dst_stride + j]       = a + b + c;
            dst[(i + 1) * dst_stride + j] = b + c;
        }
    }

    // Horizontal sum over 3-pixel regions of dst
    for (i = 0; i < height; ++i) {
        a = dst[i * dst_stride];
        b = dst[i * dst_stride + 1];
        c = dst[i * dst_stride + 2];

        dst[i * dst_stride] = a + b;
        for (j = 1; j < width - 2; ++j) {
            dst[i * dst_stride + j] = a + b + c;
            a                       = b;
            b                       = c;
            c                       = dst[i * dst_stride + (j + 2)];
        }
        dst[i * dst_stride + j]       = a + b + c;
        dst[i * dst_stride + (j + 1)] = b + c;
    }
}

// boxsum2 (restoration.c:497-:584). 1:1: vertical 5-tap walk (3-sums at
// row 0, 4-sums at row 1), then horizontal 5-tap walk (3-sums at col 0,
// 4-sums at col 1).
void boxsum2(std::int32_t* src, std::int32_t width, std::int32_t height, std::int32_t src_stride, std::int32_t sqr,
             std::int32_t* dst, std::int32_t dst_stride) {
    std::int32_t i, j, a, b, c, d, e;

    // Vertical sum over 5-pixel regions, from src into dst.
    if (!sqr) {
        for (j = 0; j < width; ++j) {
            a = src[j];
            b = src[src_stride + j];
            c = src[2 * src_stride + j];
            d = src[3 * src_stride + j];
            e = src[4 * src_stride + j];

            dst[j]              = a + b + c;
            dst[dst_stride + j] = a + b + c + d;
        for (i = 2; i < height - 3; ++i) {
                dst[i * dst_stride + j] = a + b + c + d + e;
                a                       = b;
                b                       = c;
                c                       = d;
                d                       = e;
                e                       = src[(i + 3) * src_stride + j];
            }
            dst[i * dst_stride + j]       = a + b + c + d + e;
            dst[(i + 1) * dst_stride + j] = b + c + d + e;
            dst[(i + 2) * dst_stride + j] = c + d + e;
        }
    } else {
        for (j = 0; j < width; ++j) {
            a = src[j] * src[j];
            b = src[src_stride + j] * src[src_stride + j];
            c = src[2 * src_stride + j] * src[2 * src_stride + j];
            d = src[3 * src_stride + j] * src[3 * src_stride + j];
            e = src[4 * src_stride + j] * src[4 * src_stride + j];

            dst[j]              = a + b + c;
            dst[dst_stride + j] = a + b + c + d;
            for (i = 2; i < height - 3; ++i) {
                dst[i * dst_stride + j] = a + b + c + d + e;
                a                       = b;
                b                       = c;
                c                       = d;
                d                       = e;
                e                       = src[(i + 3) * src_stride + j] * src[(i + 3) * src_stride + j];
            }
            dst[i * dst_stride + j]       = a + b + c + d + e;
            dst[(i + 1) * dst_stride + j] = b + c + d + e;
            dst[(i + 2) * dst_stride + j] = c + d + e;
        }
    }

    // Horizontal sum over 5-pixel regions of dst
    for (i = 0; i < height; ++i) {
        a = dst[i * dst_stride];
        b = dst[i * dst_stride + 1];
        c = dst[i * dst_stride + 2];
        d = dst[i * dst_stride + 3];
        e = dst[i * dst_stride + 4];

        dst[i * dst_stride]     = a + b + c;
        dst[i * dst_stride + 1] = a + b + c + d;
        for (j = 2; j < width - 3; ++j) {
            dst[i * dst_stride + j] = a + b + c + d + e;
            a                       = b;
            b                       = c;
            c                       = d;
            d                       = e;
            e                       = dst[i * dst_stride + (j + 3)];
        }
        dst[i * dst_stride + j]       = a + b + c + d + e;
        dst[i * dst_stride + (j + 1)] = b + c + d + e;
        dst[i * dst_stride + (j + 2)] = c + d + e;
    }
}

// boxsum (restoration.c:586-:594). 1:1 dispatch; the invalid-r assert(0)
// is unreachable in our emission (r comes from sgr_params r values 0/1/2).
void boxsum(std::int32_t* src, std::int32_t width, std::int32_t height, std::int32_t src_stride, std::int32_t r,
            std::int32_t sqr, std::int32_t* dst, std::int32_t dst_stride) {
    if (r == 1) {
        boxsum1(src, width, height, src_stride, sqr, dst, dst_stride);
    } else if (r == 2) {
        boxsum2(src, width, height, src_stride, sqr, dst, dst_stride);
    } else {
        assert(0 && "Invalid value of r in self-guided filter");
    }
}

// svt_decode_xq (restoration.c:597-:608). 1:1.
void decodeXq(const std::int32_t* xqd, std::int32_t* xq, const SgrParamsType* params) {
    if (params->r[0] == 0) {
        xq[0] = 0;
        xq[1] = (1 << 7) - xqd[1];
    } else if (params->r[1] == 0) {
        xq[0] = xqd[0];
        xq[1] = 0;
    } else {
        xq[0] = xqd[0];
        xq[1] = (1 << 7) - xq[0] - xqd[1];
    }
}

// x_by_xplus1 (restoration.c:610-:625): floor((256*x + (x+1)/2) / (x+1))
// with C integer division; entry 0 special-cased to 1, entry 255 to 256.
const std::int32_t xByXplus1[256] = {
    1, 128, 171, 192, 205, 213, 219, 224,
    228, 230, 233, 235, 236, 238, 239, 240,
    241, 242, 243, 243, 244, 244, 245, 245,
    246, 246, 247, 247, 247, 247, 248, 248,
    248, 248, 249, 249, 249, 249, 249, 250,
    250, 250, 250, 250, 250, 250, 251, 251,
    251, 251, 251, 251, 251, 251, 251, 251,
    252, 252, 252, 252, 252, 252, 252, 252,
    252, 252, 252, 252, 252, 252, 252, 252,
    252, 253, 253, 253, 253, 253, 253, 253,
    253, 253, 253, 253, 253, 253, 253, 253,
    253, 253, 253, 253, 253, 253, 253, 253,
    253, 253, 253, 253, 253, 253, 254, 254,
    254, 254, 254, 254, 254, 254, 254, 254,
    254, 254, 254, 254, 254, 254, 254, 254,
    254, 254, 254, 254, 254, 254, 254, 254,
    254, 254, 254, 254, 254, 254, 254, 254,
    254, 254, 254, 254, 254, 254, 254, 254,
    254, 254, 254, 254, 254, 254, 254, 254,
    254, 254, 254, 254, 254, 254, 254, 254,
    254, 254, 254, 254, 254, 254, 254, 254,
    254, 254, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 256,
};

// one_by_x (restoration.c:627-:630): 25 entries (MAX_NELEM = 25).
const std::int32_t oneByX[25] = {
    4096, 2048, 1365, 1024, 819, 683, 585, 512, 455, 410, 372, 341, 315,
    293,  273,  256,  241,  228, 216, 205, 195, 186, 178, 171, 164,
};

// extend_frame_lowbd (restoration.c:110-:126). 1:1; svt_memcpy -> memcpy.
void extendFrameLowbd(std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t stride,
                      std::int32_t border_horz, std::int32_t border_vert) {
    std::uint8_t* data_p;
    std::int32_t  i;
    for (i = 0; i < height; ++i) {
        data_p = data + i * stride;
        memset(data_p - border_horz, data_p[0], border_horz);
        memset(data_p + width, data_p[width - 1], border_horz);
    }
    data_p = data - border_horz;
    for (i = -border_vert; i < 0; ++i) {
        memcpy(data_p + i * stride, data_p, width + 2 * border_horz);
    }
    for (i = height; i < height + border_vert; ++i) {
        memcpy(data_p + i * stride, data_p + (height - 1) * stride, width + 2 * border_horz);
    }
}

// svt_extend_frame (restoration.c:150-:157). 1:1 shape; the highbd branch
// is named-parked (extend_frame_highbd :128-:148 not ported, unreachable
// in our emission).
void extendFrame(std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t stride,
                 std::int32_t border_horz, std::int32_t border_vert, std::int32_t highbd) {
    if (highbd) {
        assert(0 && "highbd restoration path parked (LR1)");
    } else {
        extendFrameLowbd(data, width, height, stride, border_horz, border_vert);
    }
}

// copy_tile_lowbd (restoration.c:159-:164). 1:1; svt_memcpy -> memcpy.
void copyTileLowbd(std::int32_t width, std::int32_t height, const std::uint8_t* src, std::int32_t src_stride,
                   std::uint8_t* dst, std::int32_t dst_stride) {
    for (std::int32_t i = 0; i < height; ++i) {
        memcpy(dst + i * dst_stride, src + i * src_stride, width);
    }
}

// copy_tile (restoration.c:173-:180). 1:1 shape; the highbd branch is
// named-parked (copy_tile_highbd :166-:171 not ported).
void copyTile(std::int32_t width, std::int32_t height, const std::uint8_t* src, std::int32_t src_stride,
              std::uint8_t* dst, std::int32_t dst_stride, std::int32_t highbd) {
    if (highbd) {
        assert(0 && "highbd restoration path parked (LR1)");
    } else {
        copyTileLowbd(width, height, src, src_stride, dst, dst_stride);
    }
}

} // namespace restoration

} // namespace l10_filters
