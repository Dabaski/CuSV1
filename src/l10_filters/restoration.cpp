#include <algorithm>
#include <cassert>
#include <cstring>

#include "restoration.h"

#include "convolve.h"

namespace l10_filters {

namespace restoration {

namespace {

// definitions.h:457: ROUND_POWER_OF_TWO, the unsigned instantiation used
// by the selfguided estimation arithmetic.
std::uint32_t roundPowerOfTwoU32(std::uint32_t value, std::int32_t n) {
    return (value + (((1u << n) >> 1))) >> n;
}

// definitions.h:457: ROUND_POWER_OF_TWO on signed int (arithmetic shift;
// the apply-site call passes a possibly negative v).
std::int32_t roundPowerOfTwoI32(std::int32_t value, std::int32_t n) {
    return (value + (((1 << n) >> 1))) >> n;
}

// definitions.h:704-:714: clip_pixel_highbd, verbatim switch shape
// (case 8 / default, 10, 12).
std::uint16_t clipPixelHighbd(std::int32_t val, std::int32_t bd) {
    switch (bd) {
    case 8:
    default:
        return (std::uint16_t)(val > 255 ? 255 : (val < 0) ? 0 : val);
    case 10:
        return (std::uint16_t)(val > 1023 ? 1023 : (val < 0) ? 0 : val);
    case 12:
        return (std::uint16_t)(val > 4095 ? 4095 : (val < 0) ? 0 : val);
    }
}

} // namespace

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

// svt_decode_xq (restoration.c:597-:608). 1:1; the (1 << SGRPROJ_PRJ_BITS)
// base is the LR2 sgrprojPrjBits constant (LR6 refactor: was literal 7).
void decodeXq(const std::int32_t* xqd, std::int32_t* xq, const SgrParamsType* params) {
    if (params->r[0] == 0) {
        xq[0] = 0;
        xq[1] = (1 << sgrprojPrjBits) - xqd[1];
    } else if (params->r[1] == 0) {
        xq[0] = xqd[0];
        xq[1] = 0;
    } else {
        xq[0] = xqd[0];
        xq[1] = (1 << sgrprojPrjBits) - xq[0] - xqd[1];
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

// extend_frame_highbd (restoration.c:128-:148). 1:1: the uint8 param is the
// uint16-plane alias (CONVERT_TO_SHORTPTR folded to a reinterpret_cast; the
// MC1 sseHighbd precedent); the top/bottom copies scale by sizeof(uint16).
void extendFrameHighbd(std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t stride,
                       std::int32_t border_horz, std::int32_t border_vert) {
    auto* data16 = reinterpret_cast<std::uint16_t*>(data);
    std::uint16_t* data_p;
    std::int32_t   i, j;
    for (i = 0; i < height; ++i) {
        data_p = data16 + i * stride;
        for (j = -border_horz; j < 0; ++j) {
            data_p[j] = data_p[0];
        }
        for (j = width; j < width + border_horz; ++j) {
            data_p[j] = data_p[width - 1];
        }
    }
    data_p = data16 - border_horz;
    for (i = -border_vert; i < 0; ++i) {
        memcpy(data_p + i * stride, data_p, (width + 2 * border_horz) * sizeof(std::uint16_t));
    }
    for (i = height; i < height + border_vert; ++i) {
        memcpy(data_p + i * stride, data_p + (height - 1) * stride, (width + 2 * border_horz) * sizeof(std::uint16_t));
    }
}

// svt_extend_frame (restoration.c:150-:157). 1:1 shape; the highbd branch
// routes to the landed extend_frame_highbd (the LR1 park resolves).
void extendFrame(std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t stride,
                 std::int32_t border_horz, std::int32_t border_vert, std::int32_t highbd) {
    if (highbd) {
        extendFrameHighbd(data, width, height, stride, border_horz, border_vert);
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

// copy_tile_highbd (restoration.c:166-:171). 1:1: width ELEMENTS per row
// (width * sizeof(*dst) bytes).
void copyTileHighbd(std::int32_t width, std::int32_t height, const std::uint8_t* src, std::int32_t src_stride,
                    std::uint8_t* dst, std::int32_t dst_stride) {
    auto* src16 = reinterpret_cast<const std::uint16_t*>(src);
    auto* dst16 = reinterpret_cast<std::uint16_t*>(dst);
    for (std::int32_t i = 0; i < height; ++i) {
        memcpy(dst16 + i * dst_stride, src16 + i * src_stride, width * sizeof(*dst16));
    }
}

// copy_tile (restoration.c:173-:180). 1:1 shape; the highbd branch routes
// to the landed copy_tile_highbd (the LR1 park resolves).
void copyTile(std::int32_t width, std::int32_t height, const std::uint8_t* src, std::int32_t src_stride,
              std::uint8_t* dst, std::int32_t dst_stride, std::int32_t highbd) {
    if (highbd) {
        copyTileHighbd(width, height, src, src_stride, dst, dst_stride);
    } else {
        copyTileLowbd(width, height, src, src_stride, dst, dst_stride);
    }
}

// selfguided_restoration_fast_internal (restoration.c:632-:764). 1:1:
// radius-r boxsums over the extended view, the z/A/B estimation pass with
// the row stride 2 (i += 2), then the row-parity *6/*5 kernels; r == 2 is
// asserted at the filter stage. AOMMIN(z, 255) -> std::min(z, 255u)
// (matching uint32 types); comments stripped, one-line provenance header.
void selfguidedRestorationFastInternal(std::int32_t* dgd, std::int32_t width, std::int32_t height,
    std::int32_t dgd_stride, std::int32_t* dst, std::int32_t dst_stride, std::int32_t bit_depth,
    std::int32_t sgr_params_idx, std::int32_t radius_idx) {
    const SgrParamsType* const params     = &sgrParams[sgr_params_idx];
    const std::int32_t         r          = params->r[radius_idx];
    const std::int32_t         width_ext  = width + 2 * sgrprojBorderHorz;
    const std::int32_t         height_ext = height + 2 * sgrprojBorderVert;
    std::int32_t  buf_stride = ((width_ext + 3) & ~3) + 16;
    std::int32_t  a_[restorationProcUnitPels];
    std::int32_t  b_[restorationProcUnitPels];
    std::int32_t* A = a_;
    std::int32_t* B = b_;
    std::int32_t  i, j;

    assert(r <= maxRadius && "Need MAX_RADIUS >= r");
    assert(r <= sgrprojBorderVert - 1 && r <= sgrprojBorderHorz - 1 && "Need SGRPROJ_BORDER_* >= r+1");

    boxsum(dgd - dgd_stride * sgrprojBorderVert - sgrprojBorderHorz, width_ext, height_ext, dgd_stride, r, 0, B,
        buf_stride);
    boxsum(dgd - dgd_stride * sgrprojBorderVert - sgrprojBorderHorz, width_ext, height_ext, dgd_stride, r, 1, A,
        buf_stride);
    A += sgrprojBorderVert * buf_stride + sgrprojBorderHorz;
    B += sgrprojBorderVert * buf_stride + sgrprojBorderHorz;
    for (i = -1; i < height + 1; i += 2) {
        for (j = -1; j < width + 1; ++j) {
            const std::int32_t k = i * buf_stride + j;
            const std::int32_t n = (2 * r + 1) * (2 * r + 1);

            std::uint32_t a = (std::uint32_t)roundPowerOfTwoI32(A[k], 2 * (bit_depth - 8));
            std::uint32_t b = (std::uint32_t)roundPowerOfTwoI32(B[k], bit_depth - 8);

            std::uint32_t p = (a * n < b * b) ? 0u : a * n - b * b;

            const std::uint32_t s = (std::uint32_t)params->s[radius_idx];

            const std::uint32_t z = roundPowerOfTwoU32(p * s, sgrprojMtableBits);

            A[k] = xByXplus1[std::min(z, 255u)];

            B[k] = (std::int32_t)roundPowerOfTwoU32(
                (std::uint32_t)(sgrprojSgr - A[k]) * (std::uint32_t)B[k] * (std::uint32_t)oneByX[n - 1],
                sgrprojRecipBits);
        }
    }
    assert(r == 2);
    for (i = 0; i < height; ++i) {
        if (!(i & 1)) { // even row
            for (j = 0; j < width; ++j) {
                const std::int32_t k  = i * buf_stride + j;
                const std::int32_t l  = i * dgd_stride + j;
                const std::int32_t m  = i * dst_stride + j;
                const std::int32_t nb = 5;
                const std::int32_t a  = (A[k - buf_stride] + A[k + buf_stride]) * 6 +
                    (A[k - 1 - buf_stride] + A[k - 1 + buf_stride] + A[k + 1 - buf_stride] +
                        A[k + 1 + buf_stride]) * 5;
                const std::int32_t b = (B[k - buf_stride] + B[k + buf_stride]) * 6 +
                    (B[k - 1 - buf_stride] + B[k - 1 + buf_stride] + B[k + 1 - buf_stride] +
                        B[k + 1 + buf_stride]) * 5;
                const std::int32_t v = a * dgd[l] + b;
                dst[m]               = roundPowerOfTwoI32(v, sgrprojSgrBits + nb - sgrprojRstBits);
            }
        } else { // odd row
            for (j = 0; j < width; ++j) {
                const std::int32_t k  = i * buf_stride + j;
                const std::int32_t l  = i * dgd_stride + j;
                const std::int32_t m  = i * dst_stride + j;
                const std::int32_t nb = 4;
                const std::int32_t a  = A[k] * 6 + (A[k - 1] + A[k + 1]) * 5;
                const std::int32_t b  = B[k] * 6 + (B[k - 1] + B[k + 1]) * 5;
                const std::int32_t v  = a * dgd[l] + b;
                dst[m]                = roundPowerOfTwoI32(v, sgrprojSgrBits + nb - sgrprojRstBits);
            }
        }
    }
}

// selfguided_restoration_internal (restoration.c:766-:884). 1:1: the same
// estimation pass with the row stride 1, then the 3x3 *4/*3 kernel
// (nb = 5, no r == 2 assert).
void selfguidedRestorationInternal(std::int32_t* dgd, std::int32_t width, std::int32_t height,
    std::int32_t dgd_stride, std::int32_t* dst, std::int32_t dst_stride, std::int32_t bit_depth,
    std::int32_t sgr_params_idx, std::int32_t radius_idx) {
    const SgrParamsType* const params     = &sgrParams[sgr_params_idx];
    const std::int32_t         r          = params->r[radius_idx];
    const std::int32_t         width_ext  = width + 2 * sgrprojBorderHorz;
    const std::int32_t         height_ext = height + 2 * sgrprojBorderVert;
    std::int32_t  buf_stride = ((width_ext + 3) & ~3) + 16;
    std::int32_t  a_[restorationProcUnitPels];
    std::int32_t  b_[restorationProcUnitPels];
    std::int32_t* A = a_;
    std::int32_t* B = b_;
    std::int32_t  i, j;

    assert(r <= maxRadius && "Need MAX_RADIUS >= r");
    assert(r <= sgrprojBorderVert - 1 && r <= sgrprojBorderHorz - 1 && "Need SGRPROJ_BORDER_* >= r+1");

    boxsum(dgd - dgd_stride * sgrprojBorderVert - sgrprojBorderHorz, width_ext, height_ext, dgd_stride, r, 0, B,
        buf_stride);
    boxsum(dgd - dgd_stride * sgrprojBorderVert - sgrprojBorderHorz, width_ext, height_ext, dgd_stride, r, 1, A,
        buf_stride);
    A += sgrprojBorderVert * buf_stride + sgrprojBorderHorz;
    B += sgrprojBorderVert * buf_stride + sgrprojBorderHorz;
    for (i = -1; i < height + 1; ++i) {
        for (j = -1; j < width + 1; ++j) {
            const std::int32_t k = i * buf_stride + j;
            const std::int32_t n = (2 * r + 1) * (2 * r + 1);

            std::uint32_t a = (std::uint32_t)roundPowerOfTwoI32(A[k], 2 * (bit_depth - 8));
            std::uint32_t b = (std::uint32_t)roundPowerOfTwoI32(B[k], bit_depth - 8);

            std::uint32_t p = (a * n < b * b) ? 0u : a * n - b * b;

            const std::uint32_t s = (std::uint32_t)params->s[radius_idx];

            const std::uint32_t z = roundPowerOfTwoU32(p * s, sgrprojMtableBits);

            A[k] = xByXplus1[std::min(z, 255u)];

            B[k] = (std::int32_t)roundPowerOfTwoU32(
                (std::uint32_t)(sgrprojSgr - A[k]) * (std::uint32_t)B[k] * (std::uint32_t)oneByX[n - 1],
                sgrprojRecipBits);
        }
    }
    for (i = 0; i < height; ++i) {
        for (j = 0; j < width; ++j) {
            const std::int32_t k  = i * buf_stride + j;
            const std::int32_t l  = i * dgd_stride + j;
            const std::int32_t m  = i * dst_stride + j;
            const std::int32_t nb = 5;
            const std::int32_t a  = (A[k] + A[k - 1] + A[k + 1] + A[k - buf_stride] + A[k + buf_stride]) * 4 +
                (A[k - 1 - buf_stride] + A[k - 1 + buf_stride] + A[k + 1 - buf_stride] +
                    A[k + 1 + buf_stride]) * 3;
            const std::int32_t b = (B[k] + B[k - 1] + B[k + 1] + B[k - buf_stride] + B[k + buf_stride]) * 4 +
                (B[k - 1 - buf_stride] + B[k - 1 + buf_stride] + B[k + 1 - buf_stride] +
                    B[k + 1 + buf_stride]) * 3;
            const std::int32_t v = a * dgd[l] + b;
            dst[m]               = roundPowerOfTwoI32(v, sgrprojSgrBits + nb - sgrprojRstBits);
        }
    }
}

// svt_av1_selfguided_restoration_c (restoration.c:886-:922). 1:1: the
// dgd32 staging copy (rows/cols -3..+3 beyond the content window), then
// the r[0]/r[1] skip pair. The highbd staging branch reads the uint16
// plane aliased through the uint8 param (LR2 park resolves).
void selfguidedRestoration(const std::uint8_t* dgd8, std::int32_t width, std::int32_t height,
    std::int32_t dgd_stride, std::int32_t* flt0, std::int32_t* flt1, std::int32_t flt_stride,
    std::int32_t sgr_params_idx, std::int32_t bit_depth, std::int32_t highbd) {
    std::int32_t       dgd32_[restorationProcUnitPels];
    const std::int32_t dgd32_stride = width + 2 * sgrprojBorderHorz;
    std::int32_t*      dgd32        = dgd32_ + dgd32_stride * sgrprojBorderVert + sgrprojBorderHorz;

    if (highbd) {
        const std::uint16_t* dgd16 = reinterpret_cast<const std::uint16_t*>(dgd8);
        for (std::int32_t i = -sgrprojBorderVert; i < height + sgrprojBorderVert; ++i) {
            for (std::int32_t j = -sgrprojBorderHorz; j < width + sgrprojBorderHorz; ++j) {
                dgd32[i * dgd32_stride + j] = dgd16[i * dgd_stride + j];
            }
        }
    } else {
        for (std::int32_t i = -sgrprojBorderVert; i < height + sgrprojBorderVert; ++i) {
            for (std::int32_t j = -sgrprojBorderHorz; j < width + sgrprojBorderHorz; ++j) {
                dgd32[i * dgd32_stride + j] = dgd8[i * dgd_stride + j];
            }
        }
    }

    const SgrParamsType* const params = &sgrParams[sgr_params_idx];
    // If params->r == 0 we skip the corresponding filter. We only allow one of
    // the radii to be 0, as having both equal to 0 would be equivalent to
    // skipping SGR entirely.
    assert(!(params->r[0] == 0 && params->r[1] == 0));

    if (params->r[0] > 0) {
        selfguidedRestorationFastInternal(
            dgd32, width, height, dgd32_stride, flt0, flt_stride, bit_depth, sgr_params_idx, 0);
    }
    if (params->r[1] > 0) {
        selfguidedRestorationInternal(
            dgd32, width, height, dgd32_stride, flt1, flt_stride, bit_depth, sgr_params_idx, 1);
    }
}

// svt_apply_selfguided_restoration_c (restoration.c:924-:962). 1:1: both
// filters into tmpbuf (flt1 = flt0 + RESTORATION_UNITPELS_MAX), the
// decodeXq composition, then the per-pixel u/v accumulation, the
// ROUND_POWER_OF_TWO at SGRPROJ_PRJ_BITS + SGRPROJ_RST_BITS and the
// clip_pixel_highbd write. The highbd reads/writes go through the uint16
// alias (the LR2 park resolves).
void applySelfguidedRestoration(const std::uint8_t* dat8, std::int32_t width, std::int32_t height,
    std::int32_t stride, std::int32_t eps, const std::int32_t* xqd, std::uint8_t* dst8,
    std::int32_t dst_stride, std::int32_t* tmpbuf, std::int32_t bit_depth, std::int32_t highbd) {
    std::int32_t* flt0 = tmpbuf;
    std::int32_t* flt1 = flt0 + restorationUnitpelsMax;
    assert(width * height <= restorationUnitpelsMax);

    selfguidedRestoration(dat8, width, height, stride, flt0, flt1, width, eps, bit_depth, highbd);
    const SgrParamsType* const params = &sgrParams[eps];
    std::int32_t xq[2];
    decodeXq(xqd, xq, params);
    // The uint16 planes alias through the uint8 params with the port-wide
    // REAL-ADDRESS convention (A8R highbdPixelProjError precedent: cast
    // once, then index in elements - the reference's CONVERT_TO_SHORTPTR
    // half-address form becomes this).
    const std::uint16_t* dat16 = highbd ? reinterpret_cast<const std::uint16_t*>(dat8) : nullptr;
    std::uint16_t* dst16 = highbd ? reinterpret_cast<std::uint16_t*>(dst8) : nullptr;
    for (std::int32_t i = 0; i < height; ++i) {
        for (std::int32_t j = 0; j < width; ++j) {
            const std::int32_t k = i * width + j;

            const std::uint16_t pre_u = highbd ? dat16[i * stride + j] : dat8[i * stride + j];
            const std::int32_t  u     = (std::int32_t)pre_u << sgrprojRstBits;
            std::int32_t        v     = u << sgrprojPrjBits;
            // r == 0 => that filter was skipped in selfguidedRestoration
            // (flt[k] == u), so its blend term is skipped here too.
            if (params->r[0] > 0) {
                v += xq[0] * (flt0[k] - u);
            }
            if (params->r[1] > 0) {
                v += xq[1] * (flt1[k] - u);
            }
            const std::int16_t w = (std::int16_t)roundPowerOfTwoI32(v, sgrprojPrjBits + sgrprojRstBits);

            const std::uint16_t out = clipPixelHighbd(w, bit_depth);
            if (highbd) {
                dst16[i * dst_stride + j] = out;
            } else {
                dst8[i * dst_stride + j] = (std::uint8_t)out;
            }
        }
    }
}

// count_units_in_tile (restoration.c:71-:73): round-to-nearest division,
// floored at 1. 1:1: (tile + unit/2) / unit with the AOMMAX(.., 1) clamp.
std::int32_t countUnitsInTile(std::int32_t unit_size, std::int32_t tile_size) {
    const std::int32_t n = (tile_size + (unit_size >> 1)) / unit_size;
    return n > 1 ? n : 1;
}

// whole_frame_rect (restoration.c:51-:62). 1:1: ss_x/ss_y are the
// conjunction is_uv && sub_*; ROUND_POWER_OF_TWO at n in {0, 1}.
Av1PixelRect wholeFrameRect(FrameSize* frm_size, std::int32_t sub_x, std::int32_t sub_y, std::int32_t is_uv) {
    Av1PixelRect rect;

    const std::int32_t ss_x = is_uv && sub_x;
    const std::int32_t ss_y = is_uv && sub_y;

    rect.top    = 0;
    rect.bottom = roundPowerOfTwoI32(frm_size->frameHeight, ss_y);
    rect.left   = 0;
    rect.right  = roundPowerOfTwoI32(frm_size->superresUpscaledWidth, ss_x);
    return rect;
}

// get_stripe_boundary_info (restoration.c:216-:234). 1:1: the first stripe
// in a tile is runit_offset shorter, and the flags decide whether the
// boundary rows get overwritten by setup_processing_stripe_boundary.
void getStripeBoundaryInfo(const RestorationTileLimits* limits, const Av1PixelRect* tile_rect, std::int32_t ss_y,
    std::int32_t* copy_above, std::int32_t* copy_below) {
    *copy_above = 1;
    *copy_below = 1;

    const std::int32_t full_stripe_height = restorationProcUnitSize >> ss_y;
    const std::int32_t runit_offset       = restorationUnitOffset >> ss_y;

    const std::int32_t first_stripe_in_tile = (limits->v_start == tile_rect->top);
    const std::int32_t this_stripe_height   = full_stripe_height - (first_stripe_in_tile ? runit_offset : 0);
    const std::int32_t last_stripe_in_tile  = (limits->v_start + this_stripe_height >= tile_rect->bottom);

    if (first_stripe_in_tile) {
        *copy_above = 0;
    }
    if (last_stripe_in_tile) {
        *copy_below = 0;
    }
}

// setup_processing_stripe_boundary (restoration.c:249-:332). 1:1 with the
// REAL_PTR calls folded to identity (use_highbd asserted 0; the uint16
// line buffers and the << use_highbd line-size widening stay in the port).
void setupProcessingStripeBoundary(const RestorationTileLimits* limits, const RestorationStripeBoundaries* rsb,
    std::int32_t rsb_row, std::int32_t use_highbd, std::int32_t h, std::uint8_t* data8, std::int32_t data_stride,
    RestorationLineBuffers* rlbs, std::int32_t copy_above, std::int32_t copy_below, std::int32_t opt) {
    assert(use_highbd == 0 && "highbd stripe-boundary path parked (LR3)");
    // Offsets within the line buffers. The buffer logically starts at column
    // -RESTORATION_EXTRA_HORZ so the 1st column (at x0 - RESTORATION_EXTRA_HORZ)
    // has column x0 in the buffer.
    const std::int32_t buf_stride = rsb->stripe_boundary_stride;
    const std::int32_t buf_x0_off = limits->h_start;
    const std::int32_t line_width = (limits->h_end - limits->h_start) + 2 * restorationExtraHorz;
    const std::int32_t line_size  = line_width << use_highbd;

    const std::int32_t data_x0 = limits->h_start - restorationExtraHorz;

    if (!opt) {
        if (copy_above) {
            std::uint8_t* data8_tl = data8 + data_x0 + limits->v_start * data_stride;

            for (std::int32_t i = -restorationBorder; i < 0; ++i) {
                const std::int32_t buf_row = rsb_row + std::max(i + restorationCtxVert, 0);
                const std::int32_t buf_off = buf_x0_off + buf_row * buf_stride;
                const std::uint8_t* buf     = rsb->stripe_boundary_above + buf_off;
                std::uint8_t*       dst8    = data8_tl + i * data_stride;
                // Save old pixels, then replace with data from stripe_boundary_above
                std::memcpy(rlbs->tmp_save_above[i + restorationBorder], dst8, line_size);
                std::memcpy(dst8, buf, line_size);
            }
        }

        // Replace RESTORATION_BORDER pixels below the bottom of the stripe.
        // The second buffer row is repeated, so src_row gets the values 0, 1, 1
        // for i = 0, 1, 2.
        if (copy_below) {
            const std::int32_t stripe_end = limits->v_start + h;
            std::uint8_t*      data8_bl   = data8 + data_x0 + stripe_end * data_stride;

            for (std::int32_t i = 0; i < restorationBorder; ++i) {
                const std::int32_t buf_row = rsb_row + std::min(i, restorationCtxVert - 1);
                const std::int32_t buf_off = buf_x0_off + buf_row * buf_stride;
                const std::uint8_t* src     = rsb->stripe_boundary_below + buf_off;

                std::uint8_t* dst8 = data8_bl + i * data_stride;
                // Save old pixels, then replace with data from stripe_boundary_below
                std::memcpy(rlbs->tmp_save_below[i], dst8, line_size);
                std::memcpy(dst8, src, line_size);
            }
        }
    } else {
        if (copy_above) {
            std::uint8_t* data8_tl = data8 + data_x0 + limits->v_start * data_stride;

            // Only save and overwrite i=-RESTORATION_BORDER line.
            std::uint8_t* dst8 = data8_tl + (-restorationBorder) * data_stride;
            // Save old pixels, then replace with data from stripe_boundary_above
            std::memcpy(rlbs->tmp_save_above[0], dst8, line_size);
            std::memcpy(dst8, data8_tl + (-restorationBorder + 1) * data_stride, line_size);
        }

        if (copy_below) {
            const std::int32_t stripe_end = limits->v_start + h;
            std::uint8_t*      data8_bl   = data8 + data_x0 + stripe_end * data_stride;

            // Only save and overwrite i=2 line.
            std::uint8_t* dst8 = data8_bl + 2 * data_stride;
            // Save old pixels, then replace with data from stripe_boundary_below
            std::memcpy(rlbs->tmp_save_below[2], dst8, line_size);
            std::memcpy(dst8, data8_bl + (2 - 1) * data_stride, line_size);
        }
    }
}

// restore_processing_stripe_boundary (restoration.c:347-:397). 1:1 with the
// REAL_PTR calls folded to identity (use_highbd asserted 0).
void restoreProcessingStripeBoundary(const RestorationTileLimits* limits, const RestorationLineBuffers* rlbs,
    std::int32_t use_highbd, std::int32_t h, std::uint8_t* data8, std::int32_t data_stride, std::int32_t copy_above,
    std::int32_t copy_below, std::int32_t opt) {
    assert(use_highbd == 0 && "highbd stripe-boundary path parked (LR3)");
    const std::int32_t line_width = (limits->h_end - limits->h_start) + 2 * restorationExtraHorz;
    const std::int32_t line_size  = line_width << use_highbd;

    const std::int32_t data_x0 = limits->h_start - restorationExtraHorz;

    if (!opt) {
        if (copy_above) {
            std::uint8_t* data8_tl = data8 + data_x0 + limits->v_start * data_stride;
            for (std::int32_t i = -restorationBorder; i < 0; ++i) {
                std::uint8_t* dst8 = data8_tl + i * data_stride;
                std::memcpy(dst8, rlbs->tmp_save_above[i + restorationBorder], line_size);
            }
        }

        if (copy_below) {
            const std::int32_t stripe_bottom = limits->v_start + h;
            std::uint8_t*      data8_bl      = data8 + data_x0 + stripe_bottom * data_stride;

            for (std::int32_t i = 0; i < restorationBorder; ++i) {
                if (stripe_bottom + i >= limits->v_end + restorationBorder) {
                    break;
                }

                std::uint8_t* dst8 = data8_bl + i * data_stride;
                std::memcpy(dst8, rlbs->tmp_save_below[i], line_size);
            }
        }
    } else {
        if (copy_above) {
            std::uint8_t* data8_tl = data8 + data_x0 + limits->v_start * data_stride;

            // Only restore i=-RESTORATION_BORDER line.
            std::uint8_t* dst8 = data8_tl + (-restorationBorder) * data_stride;
            std::memcpy(dst8, rlbs->tmp_save_above[0], line_size);
        }

        if (copy_below) {
            const std::int32_t stripe_bottom = limits->v_start + h;
            std::uint8_t*      data8_bl      = data8 + data_x0 + stripe_bottom * data_stride;

            // Only restore i=2 line.
            std::uint8_t* dst8 = data8_bl + 2 * data_stride;
            std::memcpy(dst8, rlbs->tmp_save_below[2], line_size);
        }
    }
}

// sgrproj_filter_stripe (restoration.c:964-:985). 1:1: the plain-AOMMIN
// slice width rule, then the landed applySelfguidedRestoration per slice.
void sgrprojFilterStripe(const RestorationUnitInfo* rui, std::int32_t stripe_width, std::int32_t stripe_height,
    std::int32_t procunit_width, const std::uint8_t* src, std::int32_t src_stride, std::uint8_t* dst,
    std::int32_t dst_stride, std::int32_t* tmpbuf, std::int32_t bit_depth) {
    (void)bit_depth;
    assert(bit_depth == 8);

    for (std::int32_t j = 0; j < stripe_width; j += procunit_width) {
        std::int32_t w = std::min(procunit_width, stripe_width - j);
        applySelfguidedRestoration(src + j,
            w,
            stripe_height,
            src_stride,
            rui->sgrproj_info.ep,
            rui->sgrproj_info.xqd,
            dst + j,
            dst_stride,
            tmpbuf,
            bit_depth,
            0);
    }
}

// wiener_filter_stripe (restoration.c:399-:421). 1:1: the round-up slice
// width rule, then the landed wienerConvolveAddSrc per slice.
void wienerFilterStripe(const RestorationUnitInfo* rui, std::int32_t stripe_width, std::int32_t stripe_height,
    std::int32_t procunit_width, const std::uint8_t* src, std::int32_t src_stride, std::uint8_t* dst,
    std::int32_t dst_stride, std::int32_t* tmpbuf, std::int32_t bit_depth) {
    (void)tmpbuf;
    (void)bit_depth;
    assert(bit_depth == 8);
    const convolve::ConvolveParams conv_params = convolve::getConvParamsWiener(8);

    for (std::int32_t j = 0; j < stripe_width; j += procunit_width) {
        std::int32_t w     = std::min(procunit_width, (stripe_width - j + 15) & ~15);
        const std::uint8_t* src_p = src + j;
        std::uint8_t*       dst_p = dst + j;
        convolve::wienerConvolveAddSrc(src_p,
            src_stride,
            dst_p,
            dst_stride,
            rui->wiener_info.hfilter,
            rui->wiener_info.vfilter,
            w,
            stripe_height,
            &conv_params);
    }
}

// svt_av1_loop_restoration_filter_unit (restoration.c:1040-:1110). Growing
// with the LR3 slices: the RESTORE_NONE branch first.
void loopRestorationFilterUnit(std::uint8_t need_bounadaries, const RestorationTileLimits* limits,
    const RestorationUnitInfo* rui, const RestorationStripeBoundaries* rsb, RestorationLineBuffers* rlbs,
    const Av1PixelRect* tile_rect, std::int32_t tile_stripe0, std::int32_t ss_x, std::int32_t ss_y,
    std::int32_t highbd, std::int32_t bit_depth, std::uint8_t* data8, std::int32_t stride, std::uint8_t* dst8,
    std::int32_t dst_stride, std::int32_t* tmpbuf, std::int32_t optimized_lr) {
    assert(highbd == 0 && "highbd restoration path parked (LR3)");

    const RestorationType unit_rtype = rui->restoration_type;

    const std::int32_t unit_h   = limits->v_end - limits->v_start;
    const std::int32_t unit_w   = limits->h_end - limits->h_start;
    std::uint8_t* data8_tl = data8 + limits->v_start * stride + limits->h_start;
    std::uint8_t* dst8_tl  = dst8 + limits->v_start * dst_stride + limits->h_start;

    if (unit_rtype == RESTORE_NONE) {
        copyTile(unit_w, unit_h, data8_tl, stride, dst8_tl, dst_stride, highbd);
        return;
    }

    const std::int32_t procunit_width = restorationProcUnitSize >> ss_x;

    // Convolve the whole tile one stripe at a time
    RestorationTileLimits remaining_stripes = *limits;
    std::int32_t i = 0;
    while (i < unit_h) {
        std::int32_t copy_above, copy_below;
        remaining_stripes.v_start = limits->v_start + i;

        getStripeBoundaryInfo(&remaining_stripes, tile_rect, ss_y, &copy_above, &copy_below);

        const std::int32_t full_stripe_height = restorationProcUnitSize >> ss_y;
        const std::int32_t runit_offset       = restorationUnitOffset >> ss_y;

        // Work out where this stripe's boundaries are within
        // rsb->stripe_boundary_{above,below}
        const std::int32_t tile_stripe =
            (remaining_stripes.v_start - tile_rect->top + runit_offset) / full_stripe_height;
        const std::int32_t frame_stripe = tile_stripe0 + tile_stripe;
        const std::int32_t rsb_row      = restorationCtxVert * frame_stripe;
        (void)rsb_row; // consumed by the S11d need_boundaries path

        // Calculate this stripe's height, based on two rules:
        // * The topmost stripe in each tile is 8 luma pixels shorter than usual.
        // * We can't extend past the end of the current restoration unit
        const std::int32_t nominal_stripe_height = full_stripe_height - ((tile_stripe == 0) ? runit_offset : 0);
        const std::int32_t h =
            std::min(nominal_stripe_height, remaining_stripes.v_end - remaining_stripes.v_start);

        if (need_bounadaries) {
            setupProcessingStripeBoundary(
                &remaining_stripes, rsb, rsb_row, highbd, h, data8, stride, rlbs, copy_above, copy_below,
                optimized_lr);
        }

        if (unit_rtype == RESTORE_SGRPROJ) {
            sgrprojFilterStripe(rui, unit_w, h, procunit_width, data8_tl + i * stride, stride,
                dst8_tl + i * dst_stride, dst_stride, tmpbuf, bit_depth);
        } else {
            wienerFilterStripe(rui, unit_w, h, procunit_width, data8_tl + i * stride, stride,
                dst8_tl + i * dst_stride, dst_stride, tmpbuf, bit_depth);
        }
        if (need_bounadaries) {
            restoreProcessingStripeBoundary(
                &remaining_stripes, rlbs, highbd, h, data8, stride, copy_above, copy_below, optimized_lr);
        }

        i += h;
    }
}

} // namespace restoration

} // namespace l10_filters
