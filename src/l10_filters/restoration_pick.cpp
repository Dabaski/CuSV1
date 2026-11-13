#include "restoration_pick.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstring>

namespace l10_filters {

namespace restoration_pick {

// definitions.h:457: ROUND_POWER_OF_TWO on a possibly negative value
// (arithmetic shift; file-local, the restoration.cpp twin is in its own
// anonymous namespace).
std::int32_t roundPowerOfTwoI32(std::int32_t value, std::int32_t n) {
    return (value + (((1 << n) >> 1))) >> n;
}

// wrap_index (restoration_pick.c:745-:750). 1:1.
std::int32_t wrapIndex(std::int32_t i, std::int32_t wiener_win) {
    const std::int32_t wiener_halfwin1 = (wiener_win >> 1) + 1;
    return (i >= wiener_halfwin1 ? wiener_win - 1 - i : i);
}

// find_average (restoration_pick.h:21-:31). 1:1.
std::uint8_t findAverage(const std::uint8_t* src, std::int32_t h_start, std::int32_t h_end, std::int32_t v_start,
    std::int32_t v_end, std::int32_t stride) {
    std::uint64_t sum = 0;
    for (std::int32_t i = v_start; i < v_end; i++) {
        for (std::int32_t j = h_start; j < h_end; j++) {
            sum += src[i * stride + j];
        }
    }
    std::uint64_t avg = sum / ((std::uint64_t)(v_end - v_start) * (std::uint64_t)(h_end - h_start));
    return (std::uint8_t)avg;
}

// find_average_highbd (restoration_pick.h:33-:43). 1:1.
std::uint16_t findAverageHighbd(const std::uint16_t* src, std::int32_t h_start, std::int32_t h_end,
    std::int32_t v_start, std::int32_t v_end, std::int32_t stride) {
    std::uint64_t sum = 0;
    for (std::int32_t i = v_start; i < v_end; i++) {
        for (std::int32_t j = h_start; j < h_end; j++) {
            sum += src[i * stride + j];
        }
    }
    std::uint64_t avg = sum / ((std::uint64_t)(v_end - v_start) * (std::uint64_t)(h_end - h_start));
    return (std::uint16_t)avg;
}

// svt_av1_compute_stats_c (restoration_pick.c:652-:690). 1:1: the avg comes
// from the DGD window (find_average), y gathers the dgd window around
// (i, j) with the column offset k in the OUTER loop (slot
// (k+halfwin)*win + (l+halfwin)), M gets y[k]*x, H the upper triangle of
// y[k]*y[l], and H is mirrored after the walk.
void computeStats(std::int32_t wiener_win, const std::uint8_t* dgd, const std::uint8_t* src, std::int32_t h_start,
    std::int32_t h_end, std::int32_t v_start, std::int32_t v_end, std::int32_t dgd_stride, std::int32_t src_stride,
    std::int64_t* M, std::int64_t* H) {
    std::int32_t i, j, k, l;
    std::int16_t y[wienerWin2] = {0};
    const std::int32_t wiener_win2    = wiener_win * wiener_win;
    const std::int32_t wiener_halfwin = (wiener_win >> 1);
    std::uint8_t avg = findAverage(dgd, h_start, h_end, v_start, v_end, dgd_stride);

    std::memset(M, 0, sizeof(*M) * wiener_win2);
    std::memset(H, 0, sizeof(*H) * wiener_win2 * wiener_win2);
    for (i = v_start; i < v_end; i++) {
        for (j = h_start; j < h_end; j++) {
            const std::int16_t x = (std::int16_t)src[i * src_stride + j] - (std::int16_t)avg;
            std::int32_t idx = 0;
            for (k = -wiener_halfwin; k <= wiener_halfwin; k++) {
                for (l = -wiener_halfwin; l <= wiener_halfwin; l++) {
                    y[idx] = (std::int16_t)dgd[(i + l) * dgd_stride + (j + k)] - (std::int16_t)avg;
                    idx++;
                }
            }
            for (k = 0; k < wiener_win2; ++k) {
                M[k] += (std::int32_t)y[k] * x;
                for (l = k; l < wiener_win2; ++l) {
                    // H is a symmetric matrix, so we only need to fill out the upper
                    // triangle here. We can copy it down to the lower triangle outside
                    // the (i, j) loops.
                    H[k * wiener_win2 + l] += (std::int32_t)y[k] * y[l];
                }
            }
        }
    }
    for (k = 0; k < wiener_win2; ++k) {
        for (l = k + 1; l < wiener_win2; ++l) {
            H[l * wiener_win2 + k] = H[k * wiener_win2 + l];
        }
    }
}

// svt_av1_compute_stats_highbd_c (restoration_pick.c:692-:743). 1:1 with
// the CONVERT_TO_SHORTPTR reads folded to a reinterpret (the uint16 planes
// are aliased through the uint8 params, MC1 sseHighbd precedent) and the
// EbBitDepth parameter folded to its integer values (named deviations).
// The divider (16 at EB_TWELVE_BIT, 4 at EB_TEN_BIT, else 1) is applied to
// M and the H upper triangle after the walk, and H is mirrored from the
// DIVIDED upper triangle.
void computeStatsHighbd(std::int32_t wiener_win, const std::uint8_t* dgd8, const std::uint8_t* src8,
    std::int32_t h_start, std::int32_t h_end, std::int32_t v_start, std::int32_t v_end, std::int32_t dgd_stride,
    std::int32_t src_stride, std::int64_t* M, std::int64_t* H, std::int32_t bit_depth) {
    std::int32_t i, j, k, l;
    std::int32_t y[wienerWin2] = {0};
    const std::int32_t wiener_win2    = wiener_win * wiener_win;
    const std::int32_t wiener_halfwin = (wiener_win >> 1);
    const std::uint16_t* src = reinterpret_cast<const std::uint16_t*>(src8);
    const std::uint16_t* dgd = reinterpret_cast<const std::uint16_t*>(dgd8);
    std::uint16_t avg = findAverageHighbd(dgd, h_start, h_end, v_start, v_end, dgd_stride);

    std::uint8_t bit_depth_divider = 1;
    if (bit_depth == 12) {
        bit_depth_divider = 16;
    } else if (bit_depth == 10) {
        bit_depth_divider = 4;
    }

    std::memset(M, 0, sizeof(*M) * wiener_win2);
    std::memset(H, 0, sizeof(*H) * wiener_win2 * wiener_win2);
    for (i = v_start; i < v_end; i++) {
        for (j = h_start; j < h_end; j++) {
            const std::int32_t x = (std::int32_t)src[i * src_stride + j] - (std::int32_t)avg;
            std::int32_t idx = 0;
            for (k = -wiener_halfwin; k <= wiener_halfwin; k++) {
                for (l = -wiener_halfwin; l <= wiener_halfwin; l++) {
                    y[idx] = (std::int32_t)dgd[(i + l) * dgd_stride + (j + k)] - (std::int32_t)avg;
                    idx++;
                }
            }
            for (k = 0; k < wiener_win2; ++k) {
                M[k] += (std::int64_t)y[k] * x;
                for (l = k; l < wiener_win2; ++l) {
                    // H is a symmetric matrix, so we only need to fill out the upper
                    // triangle here. We can copy it down to the lower triangle outside
                    // the (i, j) loops.
                    H[k * wiener_win2 + l] += (std::int64_t)y[k] * y[l];
                }
            }
        }
    }
    for (k = 0; k < wiener_win2; ++k) {
        M[k] /= bit_depth_divider;
        H[k * wiener_win2 + k] /= bit_depth_divider;
        for (l = k + 1; l < wiener_win2; ++l) {
            H[k * wiener_win2 + l] /= bit_depth_divider;
            H[l * wiener_win2 + k] = H[k * wiener_win2 + l];
        }
    }
}

// svt_av1_lowbd_pixel_proj_error_c (restoration_pick.c:161-:226). 1:1: the
// four-branch chain keyed on params->r[0]/r[1]; per pixel
// u = dat << SGRPROJ_RST_BITS, v = u << SGRPROJ_PRJ_BITS plus the xq-weighted
// filter deviations, e = ROUND_POWER_OF_TWO(v, RST + PRJ) - src, err += e*e.
std::int64_t lowbdPixelProjError(const std::uint8_t* src8, std::int32_t width, std::int32_t height,
    std::int32_t src_stride, const std::uint8_t* dat8, std::int32_t dat_stride, std::int32_t* flt0,
    std::int32_t flt0_stride, std::int32_t* flt1, std::int32_t flt1_stride, const std::int32_t* xq,
    const restoration::SgrParamsType* params) {
    std::int32_t i, j;
    const std::uint8_t* src = src8;
    const std::uint8_t* dat = dat8;
    std::int64_t err = 0;
    if (params->r[0] > 0 && params->r[1] > 0) {
        for (i = 0; i < height; ++i) {
            for (j = 0; j < width; ++j) {
                const std::int32_t u = (std::int32_t)(dat[j] << restoration::sgrprojRstBits);
                std::int32_t v = u << restoration::sgrprojPrjBits;
                v += xq[0] * (flt0[j] - u) + xq[1] * (flt1[j] - u);
                const std::int32_t e = roundPowerOfTwoI32(v,
                                             restoration::sgrprojRstBits + restoration::sgrprojPrjBits) -
                    src[j];
                err += e * e;
            }
            dat += dat_stride;
            src += src_stride;
            flt0 += flt0_stride;
            flt1 += flt1_stride;
        }
    } else if (params->r[0] > 0) {
        for (i = 0; i < height; ++i) {
            for (j = 0; j < width; ++j) {
                const std::int32_t u = (std::int32_t)(dat[j] << restoration::sgrprojRstBits);
                std::int32_t v = u << restoration::sgrprojPrjBits;
                v += xq[0] * (flt0[j] - u);
                const std::int32_t e = roundPowerOfTwoI32(v,
                                             restoration::sgrprojRstBits + restoration::sgrprojPrjBits) -
                    src[j];
                err += e * e;
            }
            dat += dat_stride;
            src += src_stride;
            flt0 += flt0_stride;
        }
    } else if (params->r[1] > 0) {
        for (i = 0; i < height; ++i) {
            for (j = 0; j < width; ++j) {
                const std::int32_t u = (std::int32_t)(dat[j] << restoration::sgrprojRstBits);
                std::int32_t v = u << restoration::sgrprojPrjBits;
                v += xq[1] * (flt1[j] - u);
                const std::int32_t e = roundPowerOfTwoI32(v,
                                             restoration::sgrprojRstBits + restoration::sgrprojPrjBits) -
                    src[j];
                err += e * e;
            }
            dat += dat_stride;
            src += src_stride;
            flt1 += flt1_stride;
        }
    } else {
        for (i = 0; i < height; ++i) {
            for (j = 0; j < width; ++j) {
                const std::int32_t e = (std::int32_t)(dat[j]) - src[j];
                err += e * e;
            }
            dat += dat_stride;
            src += src_stride;
        }
    }

    return err;
}

// svt_av1_highbd_pixel_proj_error_c (restoration_pick.c:228-:299). 1:1
// with the CONVERT_TO_SHORTPTR reads folded to reinterprets (uint16 planes
// aliased through the uint8 params, named deviation). Truncating-shift
// form: v starts at 1 << (RST + PRJ - 1), e = (v >> (RST + PRJ)) + d - s.
std::int64_t highbdPixelProjError(const std::uint8_t* src8, std::int32_t width, std::int32_t height,
    std::int32_t src_stride, const std::uint8_t* dat8, std::int32_t dat_stride, std::int32_t* flt0,
    std::int32_t flt0_stride, std::int32_t* flt1, std::int32_t flt1_stride, const std::int32_t* xq,
    const restoration::SgrParamsType* params) {
    const std::uint16_t* src = reinterpret_cast<const std::uint16_t*>(src8);
    const std::uint16_t* dat = reinterpret_cast<const std::uint16_t*>(dat8);
    std::int32_t i, j;
    std::int64_t err = 0;
    const std::int32_t half = 1 << (restoration::sgrprojRstBits + restoration::sgrprojPrjBits - 1);
    if (params->r[0] > 0 && params->r[1] > 0) {
        std::int32_t xq0 = xq[0];
        std::int32_t xq1 = xq[1];
        for (i = 0; i < height; ++i) {
            for (j = 0; j < width; ++j) {
                const std::int32_t d = dat[j];
                const std::int32_t s = src[j];
                const std::int32_t u = (std::int32_t)(d << restoration::sgrprojRstBits);
                std::int32_t v0 = flt0[j] - u;
                std::int32_t v1 = flt1[j] - u;
                std::int32_t v = half;
                v += xq0 * v0;
                v += xq1 * v1;
                const std::int32_t e = (v >> (restoration::sgrprojRstBits + restoration::sgrprojPrjBits)) + d - s;
                err += e * e;
            }
            dat += dat_stride;
            flt0 += flt0_stride;
            flt1 += flt1_stride;
            src += src_stride;
        }
    } else if (params->r[0] > 0 || params->r[1] > 0) {
        std::int32_t exq;
        std::int32_t* flt;
        std::int32_t flt_stride;
        if (params->r[0] > 0) {
            exq = xq[0];
            flt = flt0;
            flt_stride = flt0_stride;
        } else {
            exq = xq[1];
            flt = flt1;
            flt_stride = flt1_stride;
        }
        for (i = 0; i < height; ++i) {
            for (j = 0; j < width; ++j) {
                const std::int32_t d = dat[j];
                const std::int32_t s = src[j];
                const std::int32_t u = (std::int32_t)(d << restoration::sgrprojRstBits);
                std::int32_t v = half;
                v += exq * (flt[j] - u);
                const std::int32_t e = (v >> (restoration::sgrprojRstBits + restoration::sgrprojPrjBits)) + d - s;
                err += e * e;
            }
            dat += dat_stride;
            flt += flt_stride;
            src += src_stride;
        }
    } else {
        for (i = 0; i < height; ++i) {
            for (j = 0; j < width; ++j) {
                const std::int32_t d = dat[j];
                const std::int32_t s = src[j];
                const std::int32_t e = d - s;
                err += e * e;
            }
            dat += dat_stride;
            src += src_stride;
        }
    }
    return err;
}

// get_pixel_proj_error (restoration_pick.c:301-:322). 1:1 in the
// CONFIG_ENABLE_HIGH_BIT_DEPTH=0 shape: use_highbitdepth is UNUSED and the
// lowbd path always runs (named deviation).
std::int64_t getPixelProjError(const std::uint8_t* src8, std::int32_t width, std::int32_t height,
    std::int32_t src_stride, const std::uint8_t* dat8, std::int32_t dat_stride, std::int32_t use_highbitdepth,
    std::int32_t* flt0, std::int32_t flt0_stride, std::int32_t* flt1, std::int32_t flt1_stride, std::int32_t* xqd,
    const restoration::SgrParamsType* params) {
    (void)use_highbitdepth;
    std::int32_t xq[2];
    restoration::decodeXq(xqd, xq, params);
    return lowbdPixelProjError(
        src8, width, height, src_stride, dat8, dat_stride, flt0, flt0_stride, flt1, flt1_stride, xq, params);
}

// encode_xq (restoration_pick.c:512-:524). 1:1; clamp is the CLIP macro
// shape (restoration.h:31).
void encodeXq(std::int32_t* xq, std::int32_t* xqd, const restoration::SgrParamsType* params) {
    const auto clip = [](std::int32_t v, std::int32_t lo, std::int32_t hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    };
    if (params->r[0] == 0) {
        xqd[0] = 0;
        xqd[1] = clip((1 << restoration::sgrprojPrjBits) - xq[1], sgrprojPrjMin1, sgrprojPrjMax1);
    } else if (params->r[1] == 0) {
        xqd[0] = clip(xq[0], sgrprojPrjMin0, sgrprojPrjMax0);
        xqd[1] = clip((1 << restoration::sgrprojPrjBits) - xqd[0], sgrprojPrjMin1, sgrprojPrjMax1);
    } else {
        xqd[0] = clip(xq[0], sgrprojPrjMin0, sgrprojPrjMax0);
        xqd[1] = clip((1 << restoration::sgrprojPrjBits) - xqd[0] - xq[1], sgrprojPrjMin1, sgrprojPrjMax1);
    }
}

// linsolve_wiener (restoration_pick.c:752-:796). 1:1: partial pivoting by
// llabs, the lossy forward elimination (c / 256 * A / cd * 256 truncation
// chain), back-substitution at WIENER_TAP_SCALE_FACTOR.
std::int32_t linsolveWiener(std::int32_t n, std::int64_t* A, std::int32_t stride, std::int64_t* b, std::int32_t* x) {
    for (std::int32_t k = 0; k < n - 1; k++) {
        // Partial pivoting: bring the row with the largest pivot to the top
        for (std::int32_t i = n - 1; i > k; i--) {
            // If row i has a better (bigger) pivot than row (i-1), swap them
            if (std::llabs(A[(i - 1) * stride + k]) < std::llabs(A[i * stride + k])) {
                for (std::int32_t j = 0; j < n; j++) {
                    const std::int64_t c = A[i * stride + j];
                    A[i * stride + j] = A[(i - 1) * stride + j];
                    A[(i - 1) * stride + j] = c;
                }
                const std::int64_t c = b[i];
                b[i] = b[i - 1];
                b[i - 1] = c;
            }
        }
        // Forward elimination (convert A to row-echelon form)
        for (std::int32_t i = k; i < n - 1; i++) {
            if (A[k * stride + k] == 0) {
                return 0;
            }
            const std::int64_t c = A[(i + 1) * stride + k];
            const std::int64_t cd = A[k * stride + k];
            for (std::int32_t j = 0; j < n; j++) {
                A[(i + 1) * stride + j] -= c / 256 * A[k * stride + j] / cd * 256;
            }
            b[i + 1] -= c * b[k] / cd;
        }
    }
    // Back-substitution
    for (std::int32_t i = n - 1; i >= 0; i--) {
        if (A[i * stride + i] == 0) {
            return 0;
        }
        std::int64_t c = 0;
        for (std::int32_t j = i + 1; j <= n - 1; j++) {
            c += A[i * stride + j] * x[j] / wienerTapScaleFactor;
        }
        // Store filter taps x in scaled form.
        x[i] = (std::int32_t)(wienerTapScaleFactor * (b[i] - c) / A[i * stride + i]);
    }

    return 1;
}

// finer_search_pixel_proj_error (restoration_pick.c:324-:420). 1:1 in the
// CONFIG_ENABLE_HIGH_BIT_DEPTH=0 shape (use_highbitdepth UNUSED).
std::int64_t finerSearchPixelProjError(const std::uint8_t* src8, std::int32_t width, std::int32_t height,
    std::int32_t src_stride, const std::uint8_t* dat8, std::int32_t dat_stride, std::int32_t use_highbitdepth,
    std::int32_t* flt0, std::int32_t flt0_stride, std::int32_t* flt1, std::int32_t flt1_stride,
    std::int32_t start_step, std::int32_t* xqd, std::int8_t do_refine, const restoration::SgrParamsType* params) {
    std::int64_t err = getPixelProjError(src8,
        width,
        height,
        src_stride,
        dat8,
        dat_stride,
        use_highbitdepth,
        flt0,
        flt0_stride,
        flt1,
        flt1_stride,
        xqd,
        params);
    if (!do_refine) {
        return err;
    }

    std::int64_t err2;
    const std::int32_t tap_min[] = {sgrprojPrjMin0, sgrprojPrjMin1};
    const std::int32_t tap_max[] = {sgrprojPrjMax0, sgrprojPrjMax1};
    for (std::int32_t s = start_step; s >= 1; s >>= 1) {
        for (std::int32_t p = 0; p < 2; ++p) {
            if ((params->r[0] == 0 && p == 0) || (params->r[1] == 0 && p == 1)) {
                continue;
            }
            std::int32_t skip = 0;
            do {
                if (xqd[p] - s >= tap_min[p]) {
                    xqd[p] -= s;
                    err2 = getPixelProjError(src8,
                        width,
                        height,
                        src_stride,
                        dat8,
                        dat_stride,
                        use_highbitdepth,
                        flt0,
                        flt0_stride,
                        flt1,
                        flt1_stride,
                        xqd,
                        params);
                    if (err2 > err) {
                        xqd[p] += s;
                    } else {
                        err = err2;
                        skip = 1;
                        // At the highest step size continue moving in the same direction
                        if (s == start_step) {
                            continue;
                        }
                    }
                }
                break;
            } while (1);
            if (skip) {
                break;
            }
            do {
                if (xqd[p] + s <= tap_max[p]) {
                    xqd[p] += s;
                    err2 = getPixelProjError(src8,
                        width,
                        height,
                        src_stride,
                        dat8,
                        dat_stride,
                        use_highbitdepth,
                        flt0,
                        flt0_stride,
                        flt1,
                        flt1_stride,
                        xqd,
                        params);
                    if (err2 > err) {
                        xqd[p] -= s;
                    } else {
                        err = err2;
                        // At the highest step size continue moving in the same direction
                        if (s == start_step) {
                            continue;
                        }
                    }
                }
                break;
            } while (1);
        }
    }

    return err;
}

// svt_get_proj_subspace_c (restoration_pick.c:422-:510). 1:1 with the
// CONVERT_TO_SHORTPTR reads folded to reinterprets (named deviation); rint
// is <cmath> rint (round-half-to-even, the C rint default rounding).
void getProjSubspace(const std::uint8_t* src8, std::int32_t width, std::int32_t height, std::int32_t src_stride,
    const std::uint8_t* dat8, std::int32_t dat_stride, std::int32_t use_highbitdepth, std::int32_t* flt0,
    std::int32_t flt0_stride, std::int32_t* flt1, std::int32_t flt1_stride, std::int32_t* xq,
    const restoration::SgrParamsType* params) {
    std::int32_t i, j;
    double H[2][2] = {{0, 0}, {0, 0}};
    double C[2] = {0, 0};
    double det;
    double x[2];
    const std::int32_t size = width * height;

    // Default
    xq[0] = 0;
    xq[1] = 0;
    if (!use_highbitdepth) {
        const std::uint8_t* src = src8;
        const std::uint8_t* dat = dat8;
        for (i = 0; i < height; ++i) {
            for (j = 0; j < width; ++j) {
                const double u = (double)(dat[i * dat_stride + j] << restoration::sgrprojRstBits);
                const double s = (double)(src[i * src_stride + j] << restoration::sgrprojRstBits) - u;
                const double f1 = (params->r[0] > 0) ? (double)flt0[i * flt0_stride + j] - u : 0;
                const double f2 = (params->r[1] > 0) ? (double)flt1[i * flt1_stride + j] - u : 0;
                H[0][0] += f1 * f1;
                H[1][1] += f2 * f2;
                H[0][1] += f1 * f2;
                C[0] += f1 * s;
                C[1] += f2 * s;
            }
        }
    } else {
        const std::uint16_t* src = reinterpret_cast<const std::uint16_t*>(src8);
        const std::uint16_t* dat = reinterpret_cast<const std::uint16_t*>(dat8);
        for (i = 0; i < height; ++i) {
            for (j = 0; j < width; ++j) {
                const double u = (double)(dat[i * dat_stride + j] << restoration::sgrprojRstBits);
                const double s = (double)(src[i * src_stride + j] << restoration::sgrprojRstBits) - u;
                const double f1 = (params->r[0] > 0) ? (double)flt0[i * flt0_stride + j] - u : 0;
                const double f2 = (params->r[1] > 0) ? (double)flt1[i * flt1_stride + j] - u : 0;
                H[0][0] += f1 * f1;
                H[1][1] += f2 * f2;
                H[0][1] += f1 * f2;
                C[0] += f1 * s;
                C[1] += f2 * s;
            }
        }
    }
    H[0][0] /= size;
    H[0][1] /= size;
    H[1][1] /= size;
    H[1][0] = H[0][1];
    C[0] /= size;
    C[1] /= size;
    if (params->r[0] == 0) {
        // H matrix is now only the scalar H[1][1]
        // C vector is now only the scalar C[1]
        det = H[1][1];
        if (det < 1e-8) {
            return; // ill-posed, return default values
        }
        x[0] = 0;
        x[1] = C[1] / det;

        xq[0] = 0;
        xq[1] = (std::int32_t)std::rint(x[1] * (1 << restoration::sgrprojPrjBits));
    } else if (params->r[1] == 0) {
        // H matrix is now only the scalar H[0][0]
        // C vector is now only the scalar C[0]
        det = H[0][0];
        if (det < 1e-8) {
            return; // ill-posed, return default values
        }
        x[0] = C[0] / det;
        x[1] = 0;

        xq[0] = (std::int32_t)std::rint(x[0] * (1 << restoration::sgrprojPrjBits));
        xq[1] = 0;
    } else {
        det = (H[0][0] * H[1][1] - H[0][1] * H[1][0]);
        if (det < 1e-8) {
            return; // ill-posed, return default values
        }
        x[0] = (H[1][1] * C[0] - H[0][1] * C[1]) / det;
        x[1] = (H[0][0] * C[1] - H[1][0] * C[0]) / det;

        xq[0] = (std::int32_t)std::rint(x[0] * (1 << restoration::sgrprojPrjBits));
        xq[1] = (std::int32_t)std::rint(x[1] * (1 << restoration::sgrprojPrjBits));
    }
}

// apply_sgr (restoration_pick.c:526-:552). 1:1: AOMMIN-truncated tiles,
// the landed selfguidedRestoration per tile.
void applySgr(std::int32_t sgr_params_idx, const std::uint8_t* dat8, std::int32_t width, std::int32_t height,
    std::int32_t dat_stride, std::int32_t use_highbd, std::int32_t bit_depth, std::int32_t pu_width,
    std::int32_t pu_height, std::int32_t* flt0, std::int32_t* flt1, std::int32_t flt_stride) {
    for (std::int32_t i = 0; i < height; i += pu_height) {
        const std::int32_t h = (pu_height < height - i) ? pu_height : height - i;
        std::int32_t* flt0_row = flt0 + i * flt_stride;
        std::int32_t* flt1_row = flt1 + i * flt_stride;
        const std::uint8_t* dat8_row = dat8 + i * dat_stride;

        // Iterate over the stripe in blocks of width pu_width
        for (std::int32_t j = 0; j < width; j += pu_width) {
            const std::int32_t w = (pu_width < width - j) ? pu_width : width - j;

            restoration::selfguidedRestoration(dat8_row + j,
                w,
                h,
                dat_stride,
                flt0_row + j,
                flt1_row + j,
                flt_stride,
                sgr_params_idx,
                bit_depth,
                use_highbd);
        }
    }
}

// search_selfguided_restoration (restoration_pick.c:554-:630). 1:1: the
// flt planes live in rstbuf (flt1 = flt0 + RESTORATION_UNITPELS_MAX), the
// stride rule ((width + 7) & ~7) + 8, the !!plane normalization, the
// end-exclusive ep loop, and the besterr == -1 first-accept.
restoration::SgrprojInfo searchSelfguidedRestoration(const std::uint8_t* dat8, std::int32_t width,
    std::int32_t height, std::int32_t dat_stride, const std::uint8_t* src8, std::int32_t src_stride,
    std::int32_t use_highbitdepth, std::int32_t bit_depth, std::int32_t pu_width, std::int32_t pu_height,
    std::int32_t* rstbuf, SgFilterCtrls* ctrls, std::int32_t plane) {
    std::int32_t* flt0 = rstbuf;
    std::int32_t* flt1 = flt0 + restoration::restorationUnitpelsMax;
    std::int32_t ep, bestep = 0;
    std::int64_t besterr = -1;
    std::int32_t exqd[2], bestxqd[2] = {0, 0};
    std::int32_t flt_stride = ((width + 7) & ~7) + 8;
    assert(pu_width == (restoration::restorationProcUnitSize >> 1) ||
           pu_width == restoration::restorationProcUnitSize);
    assert(pu_height == (restoration::restorationProcUnitSize >> 1) ||
           pu_height == restoration::restorationProcUnitSize);
    plane = !!plane; // plane > 0 ? 1 : 0;
    std::int8_t start_ep = ctrls->startEp[plane];
    std::int8_t end_ep = ctrls->endEp[plane];
    std::int8_t ep_inc = ctrls->epInc[plane];
    std::int8_t do_refine = ctrls->refine[plane];
    for (ep = start_ep; ep < end_ep; ep += ep_inc) {
        std::int32_t exq[2];
        applySgr(ep,
            dat8,
            width,
            height,
            dat_stride,
            use_highbitdepth,
            bit_depth,
            pu_width,
            pu_height,
            flt0,
            flt1,
            flt_stride);

        const restoration::SgrParamsType* const params = &restoration::sgrParams[ep];
        getProjSubspace(src8,
            width,
            height,
            src_stride,
            dat8,
            dat_stride,
            use_highbitdepth,
            flt0,
            flt_stride,
            flt1,
            flt_stride,
            exq,
            params);

        encodeXq(exq, exqd, params);
        std::int64_t err = finerSearchPixelProjError(src8,
            width,
            height,
            src_stride,
            dat8,
            dat_stride,
            use_highbitdepth,
            flt0,
            flt_stride,
            flt1,
            flt_stride,
            2,
            exqd,
            do_refine,
            params);
        if (besterr == -1 || err < besterr) {
            bestep = ep;
            besterr = err;
            bestxqd[0] = exqd[0];
            bestxqd[1] = exqd[1];
        }
    }

    restoration::SgrprojInfo ret;
    ret.ep = bestep;
    ret.xqd[0] = bestxqd[0];
    ret.xqd[1] = bestxqd[1];
    return ret;
}

// get_msb, portable #else body (definitions.h:628-:644): floor(log2(n)),
// n != 0. File-local (the l13/l16 per-module duplication precedent).
static std::int32_t getMsbPick(std::uint32_t n) {
    assert(n != 0);
    std::int32_t e = 31;
    std::int32_t m = 1 << 31;
    while (!(n & m)) {
        e--;
        m >>= 1;
    }
    return e;
}

// recenter_nonneg (entropy_coding.c:2842-:2850). 1:1.
static std::uint16_t recenterNonneg(std::uint16_t r, std::uint16_t v) {
    if (v > (r << 1)) {
        return v;
    } else if (v >= r) {
        return ((std::uint16_t)((v - r) << 1));
    } else {
        return ((std::uint16_t)(((r - v) << 1) - 1));
    }
}

// recenter_finite_nonneg (entropy_coding.c:2854-:2860). 1:1.
static std::uint16_t recenterFiniteNonneg(std::uint16_t n, std::uint16_t r, std::uint16_t v) {
    if ((r << 1) <= n) {
        return recenterNonneg(r, v);
    } else {
        return recenterNonneg((std::uint16_t)(n - 1 - r), (std::uint16_t)(n - 1 - v));
    }
}

// svt_aom_count_primitive_quniform (entropy_coding.c:2891-:2898). 1:1.
std::int32_t countPrimitiveQuniform(std::uint16_t n, std::uint16_t v) {
    if (n <= 1) {
        return 0;
    }
    const std::int32_t l = getMsbPick((std::uint32_t)(n - 1)) + 1;
    const std::int32_t m = (1 << l) - n;
    return v < m ? l - 1 : l;
}

// svt_aom_count_primitive_subexpfin (entropy_coding.c:2947-:2970). 1:1.
std::int32_t countPrimitiveSubexpfin(std::uint16_t n, std::uint16_t k, std::uint16_t v) {
    std::int32_t count = 0;
    std::int32_t i = 0;
    std::int32_t mk = 0;
    while (1) {
        std::int32_t b = (i ? k + i - 1 : k);
        std::int32_t a = (1 << b);
        if (n <= mk + 3 * a) {
            count += countPrimitiveQuniform((std::uint16_t)(n - mk), (std::uint16_t)(v - mk));
            break;
        } else {
            std::int32_t t = (v >= mk + a);
            count++;
            if (t) {
                i = i + 1;
                mk += a;
            } else {
                count += b;
                break;
            }
        }
    }
    return count;
}

// svt_aom_count_primitive_refsubexpfin (entropy_coding.c:2992-:2994). 1:1.
std::int32_t countPrimitiveRefsubexpfin(std::uint16_t n, std::uint16_t k, std::uint16_t ref, std::uint16_t v) {
    return countPrimitiveSubexpfin(n, k, recenterFiniteNonneg(n, ref, v));
}

// count_sgrproj_bits (restoration_pick.c:634-:650). 1:1.
std::int32_t countSgrprojBits(restoration::SgrprojInfo* sgrproj_info, restoration::SgrprojInfo* ref_sgrproj_info) {
    std::int32_t bits = sgrprojParamsBits;
    const restoration::SgrParamsType* params = &restoration::sgrParams[sgrproj_info->ep];
    if (params->r[0] > 0) {
        bits += countPrimitiveRefsubexpfin((std::uint16_t)(sgrprojPrjMax0 - sgrprojPrjMin0 + 1),
            (std::uint16_t)sgrprojPrjSubexpK, (std::uint16_t)(ref_sgrproj_info->xqd[0] - sgrprojPrjMin0),
            (std::uint16_t)(sgrproj_info->xqd[0] - sgrprojPrjMin0));
    }
    if (params->r[1] > 0) {
        bits += countPrimitiveRefsubexpfin((std::uint16_t)(sgrprojPrjMax1 - sgrprojPrjMin1 + 1),
            (std::uint16_t)sgrprojPrjSubexpK, (std::uint16_t)(ref_sgrproj_info->xqd[1] - sgrprojPrjMin1),
            (std::uint16_t)(sgrproj_info->xqd[1] - sgrprojPrjMin1));
    }
    return bits;
}

// count_wiener_bits (restoration_pick.c:1005-:1039). 1:1: the tap0 terms
// gated on wiener_win == WIENER_WIN, taps 1/2 unconditional, vfilter then
// hfilter order.
std::int32_t countWienerBits(std::int32_t wiener_win, restoration::WienerInfo* wiener_info,
    restoration::WienerInfo* ref_wiener_info) {
    std::int32_t bits = 0;
    if (wiener_win == wienerWin) {
        bits += countPrimitiveRefsubexpfin((std::uint16_t)(wienerFiltTap0Maxv - wienerFiltTap0Minv + 1),
            (std::uint16_t)wienerFiltTap0SubexpK, (std::uint16_t)(ref_wiener_info->vfilter[0] - wienerFiltTap0Minv),
            (std::uint16_t)(wiener_info->vfilter[0] - wienerFiltTap0Minv));
    }
    bits += countPrimitiveRefsubexpfin((std::uint16_t)(wienerFiltTap1Maxv - wienerFiltTap1Minv + 1),
        (std::uint16_t)wienerFiltTap1SubexpK, (std::uint16_t)(ref_wiener_info->vfilter[1] - wienerFiltTap1Minv),
        (std::uint16_t)(wiener_info->vfilter[1] - wienerFiltTap1Minv));
    bits += countPrimitiveRefsubexpfin((std::uint16_t)(wienerFiltTap2Maxv - wienerFiltTap2Minv + 1),
        (std::uint16_t)wienerFiltTap2SubexpK, (std::uint16_t)(ref_wiener_info->vfilter[2] - wienerFiltTap2Minv),
        (std::uint16_t)(wiener_info->vfilter[2] - wienerFiltTap2Minv));
    if (wiener_win == wienerWin) {
        bits += countPrimitiveRefsubexpfin((std::uint16_t)(wienerFiltTap0Maxv - wienerFiltTap0Minv + 1),
            (std::uint16_t)wienerFiltTap0SubexpK, (std::uint16_t)(ref_wiener_info->hfilter[0] - wienerFiltTap0Minv),
            (std::uint16_t)(wiener_info->hfilter[0] - wienerFiltTap0Minv));
    }
    bits += countPrimitiveRefsubexpfin((std::uint16_t)(wienerFiltTap1Maxv - wienerFiltTap1Minv + 1),
        (std::uint16_t)wienerFiltTap1SubexpK, (std::uint16_t)(ref_wiener_info->hfilter[1] - wienerFiltTap1Minv),
        (std::uint16_t)(wiener_info->hfilter[1] - wienerFiltTap1Minv));
    bits += countPrimitiveRefsubexpfin((std::uint16_t)(wienerFiltTap2Maxv - wienerFiltTap2Minv + 1),
        (std::uint16_t)wienerFiltTap2SubexpK, (std::uint16_t)(ref_wiener_info->hfilter[2] - wienerFiltTap2Minv),
        (std::uint16_t)(wiener_info->hfilter[2] - wienerFiltTap2Minv));
    return bits;
}

// update_a_sep_sym (restoration_pick.c:798-:848). 1:1: fix b, update a.
void updateASepSym(std::int32_t wiener_win, std::int64_t** mc, std::int64_t** hc, std::int32_t* a, std::int32_t* b) {
    std::int32_t i, j;
    std::int32_t S[wienerWin];
    std::int64_t A[wienerHalfwin1], B[wienerHalfwin1 * wienerHalfwin1];
    const std::int32_t wiener_win2 = wiener_win * wiener_win;
    const std::int32_t wiener_halfwin1 = (wiener_win >> 1) + 1;
    std::memset(A, 0, sizeof(A));
    std::memset(B, 0, sizeof(B));
    for (i = 0; i < wiener_win; i++) {
        for (j = 0; j < wiener_win; ++j) {
            const std::int32_t jj = wrapIndex(j, wiener_win);
            A[jj] += mc[i][j] * b[i] / wienerTapScaleFactor;
        }
    }
    for (i = 0; i < wiener_win; i++) {
        for (j = 0; j < wiener_win; j++) {
            std::int32_t k, l;
            for (k = 0; k < wiener_win; ++k) {
                for (l = 0; l < wiener_win; ++l) {
                    const std::int32_t kk = wrapIndex(k, wiener_win);
                    const std::int32_t ll = wrapIndex(l, wiener_win);
                    B[ll * wiener_halfwin1 + kk] += hc[j * wiener_win + i][k * wiener_win2 + l] * b[i] /
                        wienerTapScaleFactor * b[j] / wienerTapScaleFactor;
                }
            }
        }
    }
    // Normalization enforcement in the system of equations itself
    assert(wiener_halfwin1 <= wienerHalfwin1);
    std::int64_t a_halfwin_1 = A[wiener_halfwin1 - 1];
    for (i = 0; i < wiener_halfwin1 - 1; ++i) {
        A[i] -= a_halfwin_1 * 2 + B[i * wiener_halfwin1 + wiener_halfwin1 - 1] -
            2 * B[(wiener_halfwin1 - 1) * wiener_halfwin1 + (wiener_halfwin1 - 1)];
    }
    for (i = 0; i < wiener_halfwin1 - 1; ++i) {
        for (j = 0; j < wiener_halfwin1 - 1; ++j) {
            B[i * wiener_halfwin1 + j] -= 2 *
                (B[i * wiener_halfwin1 + (wiener_halfwin1 - 1)] + B[(wiener_halfwin1 - 1) * wiener_halfwin1 + j] -
                    2 * B[(wiener_halfwin1 - 1) * wiener_halfwin1 + (wiener_halfwin1 - 1)]);
        }
    }
    if (linsolveWiener(wiener_halfwin1 - 1, B, wiener_halfwin1, A, S)) {
        S[wiener_halfwin1 - 1] = (std::int32_t)wienerTapScaleFactor;
        for (i = wiener_halfwin1; i < wiener_win; ++i) {
            S[i] = S[wiener_win - 1 - i];
            S[wiener_halfwin1 - 1] -= 2 * S[i];
        }
        std::memcpy(a, S, wiener_win * sizeof(*a));
    }
}

// update_b_sep_sym (restoration_pick.c:850-:899). 1:1: fix a, update b.
void updateBSepSym(std::int32_t wiener_win, std::int64_t** Mc, std::int64_t** hc, std::int32_t* a, std::int32_t* b) {
    std::int32_t i, j;
    std::int32_t S[wienerWin];
    std::int64_t A[wienerHalfwin1], B[wienerHalfwin1 * wienerHalfwin1];
    const std::int32_t wiener_win2 = wiener_win * wiener_win;
    const std::int32_t wiener_halfwin1 = (wiener_win >> 1) + 1;
    std::memset(A, 0, sizeof(A));
    std::memset(B, 0, sizeof(B));
    for (i = 0; i < wiener_win; i++) {
        const std::int32_t ii = wrapIndex(i, wiener_win);
        for (j = 0; j < wiener_win; j++) {
            A[ii] += Mc[i][j] * a[j] / wienerTapScaleFactor;
        }
    }

    for (i = 0; i < wiener_win; i++) {
        for (j = 0; j < wiener_win; j++) {
            const std::int32_t ii = wrapIndex(i, wiener_win);
            const std::int32_t jj = wrapIndex(j, wiener_win);
            std::int32_t k, l;
            for (k = 0; k < wiener_win; ++k) {
                for (l = 0; l < wiener_win; ++l) {
                    B[jj * wiener_halfwin1 + ii] += hc[i * wiener_win + j][k * wiener_win2 + l] * a[k] /
                        wienerTapScaleFactor * a[l] / wienerTapScaleFactor;
                }
            }
        }
    }
    // Normalization enforcement in the system of equations itself
    std::int64_t a_halfwin_1 = A[wiener_halfwin1 - 1];
    for (i = 0; i < wiener_halfwin1 - 1; ++i) {
        A[i] -= a_halfwin_1 * 2 + B[i * wiener_halfwin1 + wiener_halfwin1 - 1] -
            2 * B[(wiener_halfwin1 - 1) * wiener_halfwin1 + (wiener_halfwin1 - 1)];
    }
    for (i = 0; i < wiener_halfwin1 - 1; ++i) {
        for (j = 0; j < wiener_halfwin1 - 1; ++j) {
            B[i * wiener_halfwin1 + j] -= 2 *
                (B[i * wiener_halfwin1 + (wiener_halfwin1 - 1)] + B[(wiener_halfwin1 - 1) * wiener_halfwin1 + j] -
                    2 * B[(wiener_halfwin1 - 1) * wiener_halfwin1 + (wiener_halfwin1 - 1)]);
        }
    }
    if (linsolveWiener(wiener_halfwin1 - 1, B, wiener_halfwin1, A, S)) {
        S[wiener_halfwin1 - 1] = (std::int32_t)wienerTapScaleFactor;
        for (i = wiener_halfwin1; i < wiener_win; ++i) {
            S[i] = S[wiener_win - 1 - i];
            S[wiener_halfwin1 - 1] -= 2 * S[i];
        }
        std::memcpy(b, S, wiener_win * sizeof(*b));
    }
}

// wiener_decompose_sep_sym (restoration_pick.c:901-:932). 1:1: the
// init_filt seeding (plane_off-shifted, 512x), the mc/hc row-pointer
// assembly over the flat M/H, and NUM_WIENER_ITERS-1 = 4 alternating
// iterations.
void wienerDecomposeSepSym(std::int32_t wiener_win, std::int64_t* M, std::int64_t* H, std::int32_t* a,
    std::int32_t* b) {
    static const std::int32_t init_filt[wienerWin] = {
        wienerFiltTap0Midv,
        wienerFiltTap1Midv,
        wienerFiltTap2Midv,
        // WIENER_FILT_TAP3_MIDV (restoration.h:132-:133):
        // WIENER_FILT_STEP - 2*(TAP0 + TAP1 + TAP2) = 106.
        wienerFiltStep - 2 * (wienerFiltTap0Midv + wienerFiltTap1Midv + wienerFiltTap2Midv),
        wienerFiltTap2Midv,
        wienerFiltTap1Midv,
        wienerFiltTap0Midv,
    };
    std::int64_t* hc[wienerWin2];
    std::int64_t* Mc[wienerWin];
    std::int32_t i, j, iter;
    const std::int32_t plane_off = (wienerWin - wiener_win) >> 1;
    const std::int32_t wiener_win2 = wiener_win * wiener_win;
    for (i = 0; i < wiener_win; i++) {
        a[i] = b[i] = (std::int32_t)(wienerTapScaleFactor / wienerFiltStep * init_filt[i + plane_off]);
    }
    for (i = 0; i < wiener_win; i++) {
        Mc[i] = M + i * wiener_win;
        for (j = 0; j < wiener_win; j++) {
            hc[i * wiener_win + j] = H + i * wiener_win * wiener_win2 + j * wiener_win;
        }
    }

    iter = 1;
    while (iter < numWienerIters) {
        updateASepSym(wiener_win, Mc, hc, a, b);
        updateBSepSym(wiener_win, Mc, hc, a, b);
        iter++;
    }
}

// compute_score (restoration_pick.c:934-:971). 1:1: a/b rebuilt from the
// finalized taps, the ab outer product, P/Q with the sequential
// /WIENER_FILT_STEP truncation chain, and the identity comparison at the
// M/H center.
std::int64_t computeScore(std::int32_t wiener_win, std::int64_t* M, std::int64_t* H,
    restoration::WienerKernel vfilt, restoration::WienerKernel hfilt) {
    std::int32_t ab[wienerWin * wienerWin];
    std::int16_t a[wienerWin], b[wienerWin];
    std::int64_t P = 0, Q = 0;
    std::int64_t i_p = 0, i_q = 0;
    std::int64_t score, i_score;
    std::int32_t i, k, l;
    const std::int32_t plane_off = (wienerWin - wiener_win) >> 1;
    const std::int32_t wiener_win2 = wiener_win * wiener_win;

    a[wienerHalfwin] = b[wienerHalfwin] = (std::int16_t)wienerFiltStep;
    for (i = 0; i < wienerHalfwin; ++i) {
        a[i] = a[wienerWin - i - 1] = vfilt[i];
        b[i] = b[wienerWin - i - 1] = hfilt[i];
        a[wienerHalfwin] -= 2 * a[i];
        b[wienerHalfwin] -= 2 * b[i];
    }
    std::memset(ab, 0, sizeof(ab));
    for (k = 0; k < wiener_win; ++k) {
        for (l = 0; l < wiener_win; ++l) {
            ab[k * wiener_win + l] = a[l + plane_off] * b[k + plane_off];
        }
    }
    for (k = 0; k < wiener_win2; ++k) {
        P += ab[k] * M[k] / wienerFiltStep / wienerFiltStep;
        for (l = 0; l < wiener_win2; ++l) {
            Q += ab[k] * H[k * wiener_win2 + l] * ab[l] / wienerFiltStep / wienerFiltStep / wienerFiltStep /
                wienerFiltStep;
        }
    }
    score = Q - 2 * P;

    i_p = M[wiener_win2 >> 1];
    i_q = H[(wiener_win2 >> 1) * wiener_win2 + (wiener_win2 >> 1)];
    i_score = i_q - 2 * i_p;

    return score - i_score;
}

// finalize_sym_filter (restoration_pick.c:973-:1003). 1:1: the
// round-half-away division, the per-win clamp order, the mirror and the
// implicit center.
void finalizeSymFilter(std::int32_t wiener_win, std::int32_t* f, restoration::WienerKernel fi) {
    std::int32_t i;
    const std::int32_t wiener_halfwin = (wiener_win >> 1);

    for (i = 0; i < wiener_halfwin; ++i) {
        const std::int64_t dividend = (std::int64_t)f[i] * wienerFiltStep;
        const std::int64_t divisor = wienerTapScaleFactor;
        // Perform this division with proper rounding rather than truncation
        if (dividend < 0) {
            fi[i] = (std::int16_t)((dividend - (divisor / 2)) / divisor);
        } else {
            fi[i] = (std::int16_t)((dividend + (divisor / 2)) / divisor);
        }
    }
    // Specialize for 7-tap filter
    const auto clip = [](std::int32_t v, std::int32_t lo, std::int32_t hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    };
    if (wiener_win == wienerWin) {
        fi[0] = (std::int16_t)clip(fi[0], wienerFiltTap0Minv, wienerFiltTap0Maxv);
        fi[1] = (std::int16_t)clip(fi[1], wienerFiltTap1Minv, wienerFiltTap1Maxv);
        fi[2] = (std::int16_t)clip(fi[2], wienerFiltTap2Minv, wienerFiltTap2Maxv);
    } else {
        fi[2] = (std::int16_t)clip(fi[1], wienerFiltTap2Minv, wienerFiltTap2Maxv);
        fi[1] = (std::int16_t)clip(fi[0], wienerFiltTap1Minv, wienerFiltTap1Maxv);
        fi[0] = 0;
    }
    // Satisfy filter constraints
    fi[wienerWin - 1] = fi[0];
    fi[wienerWin - 2] = fi[1];
    fi[wienerWin - 3] = fi[2];
    // The central element has an implicit +WIENER_FILT_STEP
    fi[3] = (std::int16_t)(-2 * (fi[0] + fi[1] + fi[2]));
}

} // namespace restoration_pick

} // namespace l10_filters
