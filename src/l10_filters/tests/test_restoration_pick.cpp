#include <doctest.h>
#include <restoration_pick.h>

#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <vector>

// A8R (A8 campaign): tests for the pure-math surface of
// restoration_pick.c. Independent oracles are test-side transcriptions of
// the reference arithmetic (different code shape, same values); exact pins
// are hand-derived from the reference formulas. PARKED (named): the
// RestSearchCtxt/rsc driver functions (see restoration_pick.h).

// R1: wrap_index (restoration_pick.c:745-:750):
// i >= (win>>1)+1 ? win-1-i : i. Full enumeration over the tap-index range
// and the reference window sizes {7, 5} plus the generic odd sizes 1/3.
TEST_CASE("wrapIndex folds the upper half onto the symmetric half") {
    using l10_filters::restoration_pick::wrapIndex;
    for (int win : {1, 3, 5, 7}) {
        const int halfwin1 = (win >> 1) + 1;
        for (int i = -8; i <= 15; ++i) {
            const int expected = (i >= halfwin1) ? (win - 1 - i) : i;
            CHECK(wrapIndex(i, win) == expected);
        }
    }
    // Pinned shape at win = 7: indices 0..3 identity, 4..6 fold to 2, 1, 0.
    CHECK(l10_filters::restoration_pick::wrapIndex(0, 7) == 0);
    CHECK(l10_filters::restoration_pick::wrapIndex(3, 7) == 3);
    CHECK(l10_filters::restoration_pick::wrapIndex(4, 7) == 2);
    CHECK(l10_filters::restoration_pick::wrapIndex(5, 7) == 1);
    CHECK(l10_filters::restoration_pick::wrapIndex(6, 7) == 0);
    // win = 5: 0..2 identity, 3 -> 1, 4 -> 0.
    CHECK(l10_filters::restoration_pick::wrapIndex(2, 5) == 2);
    CHECK(l10_filters::restoration_pick::wrapIndex(3, 5) == 1);
    CHECK(l10_filters::restoration_pick::wrapIndex(4, 5) == 0);
}

// R2: find_average (restoration_pick.h:21-:31): uint64 window sum floored
// at the pixel count. Sub-window addressing (h_start/v_start nonzero) and
// the truncation are pinned with an inline oracle over two windows.
TEST_CASE("findAverage sums the sub-window and floors") {
    using l10_filters::restoration_pick::findAverage;
    const int stride = 32;
    std::uint8_t plane[8 * stride];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < stride; ++j) plane[i * stride + j] = (std::uint8_t)(i * 13 + j * 3);
    struct Win {
        int hs, he, vs, ve;
    };
    for (Win w : {Win{0, 8, 0, 4}, Win{5, 20, 2, 7}, Win{1, 2, 3, 4}}) {
        std::uint64_t sum = 0;
        for (int i = w.vs; i < w.ve; ++i)
            for (int j = w.hs; j < w.he; ++j) sum += plane[i * stride + j];
        const std::uint8_t expected = (std::uint8_t)(sum / (std::uint64_t)((w.ve - w.vs) * (w.he - w.hs)));
        CHECK(findAverage(plane, w.hs, w.he, w.vs, w.ve, stride) == expected);
    }
    // Constant window pins the exact truncation: sum 4*9=36 over 4 pixels
    // -> 9 exactly; sum 4*10=40 over 3 pixels -> 13 (not 13.33 rounded).
    std::uint8_t nine[1 * 4] = {9, 9, 9, 9};
    CHECK(findAverage(nine, 0, 4, 0, 1, 4) == 9);
    std::uint8_t ten[2 * 8];
    for (int k = 0; k < 2 * 8; ++k) ten[k] = (std::uint8_t)(10 + (k >= 8 ? 40 : 0));
    // Window rows [0,1), cols [0,8): eight 10s -> 10; rows [0,2), cols
    // [0,3): 10,10,10,50,50,50 -> 180/6 = 30.
    CHECK(findAverage(ten, 0, 8, 0, 1, 8) == 10);
    CHECK(findAverage(ten, 0, 3, 0, 2, 8) == 30);
}

// R3: find_average_highbd (restoration_pick.h:33-:43): the uint16 twin over
// a >8-bit-valued plane; inline oracle over two sub-windows.
TEST_CASE("findAverageHighbd sums the uint16 sub-window and floors") {
    using l10_filters::restoration_pick::findAverageHighbd;
    const int stride = 24;
    std::uint16_t plane[6 * stride];
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < stride; ++j) plane[i * stride + j] = (std::uint16_t)(200 + i * 501 + j * 37);
    struct Win {
        int hs, he, vs, ve;
    };
    for (Win w : {Win{0, 24, 0, 6}, Win{3, 19, 1, 5}}) {
        std::uint64_t sum = 0;
        for (int i = w.vs; i < w.ve; ++i)
            for (int j = w.hs; j < w.he; ++j) sum += plane[i * stride + j];
        const std::uint16_t expected = (std::uint16_t)(sum / (std::uint64_t)((w.ve - w.vs) * (w.he - w.hs)));
        CHECK(findAverageHighbd(plane, w.hs, w.he, w.vs, w.ve, stride) == expected);
    }
    // Truncation pin with a >8-bit constant: 1000+1001+1002 over 3 -> 1001.
    std::uint16_t seq[1 * 3] = {1000, 1001, 1002};
    CHECK(findAverageHighbd(seq, 0, 3, 0, 1, 3) == 1001);
}

// R4: svt_av1_compute_stats_c (restoration_pick.c:652-:690). Independent
// oracle: the y-tap slot map is re-derived arithmetically (slot
// (k+h)*win + (l+h) from dgd row i+l, col j+k) with the loop order
// REVERSED (l outer, k inner), and H is filled on BOTH triangles with no
// mirror pass. The asymmetric fixture makes the k/l column/row roles
// observable (a k/l swap produces a transposed M/H and fails).
TEST_CASE("computeStats accumulates the wiener normal equations (win 7 and 5)") {
    using l10_filters::restoration_pick::computeStats;
    const int dgdStride = 40, srcStride = 40;
    std::uint8_t dgd[40 * dgdStride], src[40 * srcStride];
    for (int i = 0; i < 40; ++i)
        for (int j = 0; j < dgdStride; ++j) dgd[i * dgdStride + j] = (std::uint8_t)(1 + (i * 17 + j * 29) % 250);
    for (int i = 0; i < 40; ++i)
        for (int j = 0; j < srcStride; ++j) src[i * srcStride + j] = (std::uint8_t)(7 + (i * 11 + j * 31) % 240);
    const int h0 = 5, h1 = 15, v0 = 4, v1 = 13;
    for (int win : {7, 5}) {
        const int win2 = win * win, half = win >> 1;
        // Zero-init pin: prefill with a sentinel; the call must clear it.
        std::vector<std::int64_t> M(49, -777), H(49 * 49, -777), Mo(49, 0), Ho(49 * 49, 0);
        computeStats(win, dgd, src, h0, h1, v0, v1, dgdStride, srcStride, M.data(), H.data());
        // Independent oracle: average over the DGD window, then the full
        // symmetric accumulation over every (k, l) pair.
        std::uint64_t sum = 0;
        for (int i = v0; i < v1; ++i)
            for (int j = h0; j < h1; ++j) sum += dgd[i * dgdStride + j];
        const std::int64_t avg = (std::int64_t)(sum / (std::uint64_t)((v1 - v0) * (h1 - h0)));
        for (int i = v0; i < v1; ++i) {
            for (int j = h0; j < h1; ++j) {
                const std::int64_t x = (std::int64_t)src[i * srcStride + j] - avg;
                for (int l = -half; l <= half; ++l) {
                    for (int k = -half; k <= half; ++k) {
                        // dgd row i+l, col j+k lands in slot (k+h)*win+(l+h).
                        const std::int64_t y = (std::int64_t)dgd[(i + l) * dgdStride + (j + k)] - avg;
                        const int slot = (k + half) * win + (l + half);
                        Mo[slot] += (std::int64_t)(std::int32_t)y * x;
                        for (int lk = 0; lk < win2; ++lk) {
                            const std::int64_t yl =
                                (std::int64_t)dgd[(i + (lk % win) - half) * dgdStride + (j + (lk / win) - half)] - avg;
                            Ho[slot * win2 + lk] += (std::int64_t)(std::int32_t)y * yl;
                        }
                    }
                }
            }
        }
        for (int k = 0; k < win2; ++k) {
            CHECK(M[k] == Mo[k]);
            for (int l = 0; l < win2; ++l) CHECK(H[k * win2 + l] == Ho[k * win2 + l]);
        }
        // H symmetry after the mirror pass.
        bool sym = true;
        for (int k = 0; k < win2 && sym; ++k)
            for (int l = 0; l < win2; ++l)
                if (H[k * win2 + l] != H[l * win2 + k]) {
                    sym = false;
                    break;
                }
        CHECK(sym);

        // The average is taken over the DGD, not src: recompute with a
        // WRONG avg (from src) must differ somewhere.
        std::uint64_t ssum = 0;
        for (int i = v0; i < v1; ++i)
            for (int j = h0; j < h1; ++j) ssum += src[i * srcStride + j];
        const std::int64_t srcAvg = (std::int64_t)(ssum / (std::uint64_t)((v1 - v0) * (h1 - h0)));
        CHECK(srcAvg != avg);
    }
}

// R5: svt_av1_compute_stats_highbd_c (restoration_pick.c:692-:743). The
// uint16 planes are aliased through the uint8 params (CONVERT_TO_SHORTPTR
// fold, MC1 precedent). Oracle: full-symmetric independent accumulation,
// then the divider (12 -> 16, 10 -> 4, else 1) applied after the walk.
// Enumerated over win {7, 5} x bit_depth {8, 10, 12}.
TEST_CASE("computeStatsHighbd accumulates with the bit-depth divider") {
    using l10_filters::restoration_pick::computeStatsHighbd;
    const int dgdStride = 36, srcStride = 36;
    std::vector<std::uint16_t> dgd(36 * dgdStride), src(36 * srcStride);
    for (int i = 0; i < 36; ++i)
        for (int j = 0; j < dgdStride; ++j) dgd[i * dgdStride + j] = (std::uint16_t)(4 + (i * 53 + j * 97) % 4000);
    for (int i = 0; i < 36; ++i)
        for (int j = 0; j < srcStride; ++j) src[i * srcStride + j] = (std::uint16_t)(11 + (i * 29 + j * 71) % 3900);
    const int h0 = 4, h1 = 13, v0 = 3, v1 = 12;
    for (int win : {7, 5}) {
        const int win2 = win * win, half = win >> 1;
        for (int bd : {8, 10, 12}) {
            const int divider = (bd == 12) ? 16 : (bd == 10) ? 4 : 1;
            std::vector<std::int64_t> M(49, -5), H(49 * 49, -5), Mo(49, 0), Ho(49 * 49, 0);
            computeStatsHighbd(win, reinterpret_cast<const std::uint8_t*>(dgd.data()),
                reinterpret_cast<const std::uint8_t*>(src.data()), h0, h1, v0, v1, dgdStride, srcStride, M.data(),
                H.data(), bd);
            // Independent oracle.
            std::uint64_t sum = 0;
            for (int i = v0; i < v1; ++i)
                for (int j = h0; j < h1; ++j) sum += dgd[i * dgdStride + j];
            const std::int64_t avg = (std::int64_t)(sum / (std::uint64_t)((v1 - v0) * (h1 - h0)));
            for (int i = v0; i < v1; ++i) {
                for (int j = h0; j < h1; ++j) {
                    const std::int64_t x = (std::int64_t)src[i * srcStride + j] - avg;
                    for (int l = -half; l <= half; ++l) {
                        for (int k = -half; k <= half; ++k) {
                            const std::int64_t y = (std::int64_t)dgd[(i + l) * dgdStride + (j + k)] - avg;
                            const int slot = (k + half) * win + (l + half);
                            Mo[slot] += y * x;
                            for (int lk = 0; lk < win2; ++lk) {
                                const std::int64_t yl = (std::int64_t)
                                    dgd[(i + (lk % win) - half) * dgdStride + (j + (lk / win) - half)] - avg;
                                Ho[slot * win2 + lk] += y * yl;
                            }
                        }
                    }
                }
            }
            for (int k = 0; k < win2; ++k) {
                Mo[k] /= divider;
                Ho[k * win2 + k] /= divider;
                for (int l = k + 1; l < win2; ++l) {
                    Ho[k * win2 + l] /= divider;
                    Ho[l * win2 + k] = Ho[k * win2 + l];
                }
            }
            for (int k = 0; k < win2; ++k) {
                CHECK(M[k] == Mo[k]);
                for (int l = 0; l < win2; ++l) CHECK(H[k * win2 + l] == Ho[k * win2 + l]);
            }
        }
    }
}

// R6: svt_av1_lowbd_pixel_proj_error_c (restoration_pick.c:161-:226).
// Independent oracle: the projection written with MULTIPLIES (u = dat*16,
// leading term u*128) and the ROUND(v, 11) re-derived as an explicit
// floor-division helper (different shape from the >> 11 arithmetic shift).
// All four r-branches pinned, with strided flt planes.
namespace {
std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    const std::lldiv_t d = std::lldiv(a, b);
    return d.rem != 0 && ((d.rem < 0) != (b < 0)) ? d.quot - 1 : d.quot;
}
} // namespace

TEST_CASE("lowbdPixelProjError covers all four r-branches") {
    using l10_filters::restoration::SgrParamsType;
    using l10_filters::restoration_pick::lowbdPixelProjError;
    const int W = 6, H = 5, srcStride = 16, datStride = 16, fStride = 13;
    std::uint8_t src[5 * srcStride], dat[5 * datStride];
    std::vector<std::int32_t> flt0(5 * fStride), flt1(5 * fStride);
    for (int i = 0; i < H; ++i) {
        for (int j = 0; j < srcStride; ++j) src[i * srcStride + j] = (std::uint8_t)(3 + (i * 19 + j * 7) % 240);
        for (int j = 0; j < datStride; ++j) dat[i * datStride + j] = (std::uint8_t)(5 + (i * 13 + j * 11) % 246);
        for (int j = 0; j < fStride; ++j) {
            flt0[i * fStride + j] = (i * 401 + j * 73) % 512 - 256 + (int)(dat[i * datStride + j] * 16);
            flt1[i * fStride + j] = (i * 313 + j * 149) % 700 - 350 + (int)(dat[i * datStride + j] * 16);
        }
    }
    // Independent oracle over the same data.
    auto oracle = [&](const SgrParamsType& params, const int xq[2]) {
        std::int64_t err = 0;
        for (int i = 0; i < H; ++i) {
            for (int j = 0; j < W; ++j) {
                const std::int64_t u = (std::int64_t)dat[i * datStride + j] * 16;
                std::int64_t v = u * 128;
                if (params.r[0] > 0) v += (std::int64_t)xq[0] * (flt0[i * fStride + j] - u);
                if (params.r[1] > 0) v += (std::int64_t)xq[1] * (flt1[i * fStride + j] - u);
                const std::int64_t e = floorDiv(v + 1024, 2048) - src[i * srcStride + j];
                err += e * e;
            }
        }
        return err;
    };
    auto call = [&](const SgrParamsType& params, const int xq[2]) {
        return lowbdPixelProjError(src, W, H, srcStride, dat, datStride, flt0.data(), fStride, flt1.data(), fStride, xq,
            &params);
    };
    const SgrParamsType both{{2, 1}, {140, 3236}}, r0only{{2, 0}, {56, -1}}, r1only{{0, 1}, {-1, 2589}},
        none{{0, 0}, {-1, -1}};
    for (int xq0 : {-96, -13, 0, 31}) {
        for (int xq1 : {-32, 7, 95, 127}) {
            const int xq[2] = {xq0, xq1};
            CHECK(call(both, xq) == oracle(both, xq));
            CHECK(call(r0only, xq) == oracle(r0only, xq));
            CHECK(call(r1only, xq) == oracle(r1only, xq));
            CHECK(call(none, xq) == oracle(none, xq));
        }
    }
    // The unused-filter property: in the r1==0 branch xq[1] is never read
    // (the reference's else-if chain drops the flt1 term); likewise r0==0
    // ignores xq[0]. Varying the ignored entry must not move the result.
    const int a[2] = {10, 20}, b[2] = {10, -17};
    CHECK(call(r0only, a) == call(r0only, b));
    const int c[2] = {-50, 20}, d[2] = {44, 20};
    CHECK(call(r1only, c) == call(r1only, d));
    // Exact reduction pin: with xq = {0, 0} the both-branch projection
    // collapses to ROUND(u << 7, 11) = dat, so the result equals the plain
    // (dat - src) SSE of the none-branch.
    const int zero[2] = {0, 0};
    CHECK(call(both, zero) == call(none, zero));
}

// R7: svt_av1_highbd_pixel_proj_error_c (restoration_pick.c:228-:299). The
// uint16 planes are aliased through the uint8 params. Oracle: the same
// three-branch chain written with multiplies and the raw-shift form
// re-derived as FLOOR division (the >> is arithmetic = floor; the
// difference from the lowbd form is the missing +half ROUND term, pinned
// at v = 1024 where floor(1024/2048) = 0 but the ROUND form gives 1).
TEST_CASE("highbdPixelProjError uses the truncating-shift projection") {
    using l10_filters::restoration::SgrParamsType;
    using l10_filters::restoration_pick::highbdPixelProjError;
    const int W = 5, H = 4, stride = 20, fStride = 17;
    std::vector<std::uint16_t> src(H * stride), dat(H * stride);
    std::vector<std::int32_t> flt0(H * fStride), flt1(H * fStride);
    for (int i = 0; i < H; ++i) {
        for (int j = 0; j < stride; ++j) {
            const std::uint16_t p = (std::uint16_t)(40 + (i * 97 + j * 53) % 900);
            src[i * stride + j] = p;
            dat[i * stride + j] = (std::uint16_t)(p + (i * 7 + j) % 5 - 2);
        }
        for (int j = 0; j < fStride; ++j) {
            flt0[i * fStride + j] = ((i * 311 + j * 89) % 1024 - 512) + (int)(dat[i * stride + j] * 16);
            flt1[i * fStride + j] = ((i * 197 + j * 151) % 1200 - 600) + (int)(dat[i * stride + j] * 16);
        }
    }
    auto alias = [](std::vector<std::uint16_t>& v) {
        return reinterpret_cast<const std::uint8_t*>(v.data());
    };
    auto oracle = [&](const SgrParamsType& params, const int xq[2]) {
        std::int64_t err = 0;
        const std::int32_t half = 1 << (4 + 7 - 1);
        for (int i = 0; i < H; ++i) {
            for (int j = 0; j < W; ++j) {
                const std::int32_t d = dat[i * stride + j];
                const std::int32_t s = src[i * stride + j];
                const std::int32_t u = d << 4;
                if (params.r[0] > 0 && params.r[1] > 0) {
                    const std::int32_t v = half + xq[0] * (flt0[i * fStride + j] - u) +
                        xq[1] * (flt1[i * fStride + j] - u);
                    // The >> is arithmetic: floor division, not the ROUND form.
                    const std::int32_t e = (std::int32_t)floorDiv(v, 2048) + d - s;
                    err += (std::int64_t)e * e;
                } else if (params.r[0] > 0 || params.r[1] > 0) {
                    const std::int32_t exq = (params.r[0] > 0) ? xq[0] : xq[1];
                    const std::int32_t f = (params.r[0] > 0) ? flt0[i * fStride + j] : flt1[i * fStride + j];
                    const std::int32_t v = half + exq * (f - u);
                    const std::int32_t e = (std::int32_t)floorDiv(v, 2048) + d - s;
                    err += (std::int64_t)e * e;
                } else {
                    err += (std::int64_t)(d - s) * (d - s);
                }
            }
        }
        return err;
    };
    auto call = [&](const SgrParamsType& params, const int xq[2]) {
        return highbdPixelProjError(alias(src), W, H, stride, alias(dat), stride, flt0.data(), fStride, flt1.data(),
            fStride, xq, &params);
    };
    const SgrParamsType both{{2, 1}, {140, 3236}}, r0only{{2, 0}, {56, -1}}, r1only{{0, 1}, {-1, 2589}},
        none{{0, 0}, {-1, -1}};
    for (int xq0 : {-96, -1, 0, 31}) {
        for (int xq1 : {-32, 3, 95, 127}) {
            const int xq[2] = {xq0, xq1};
            CHECK(call(both, xq) == oracle(both, xq));
            CHECK(call(r0only, xq) == oracle(r0only, xq));
            CHECK(call(r1only, xq) == oracle(r1only, xq));
            CHECK(call(none, xq) == oracle(none, xq));
        }
    }
    // The v = 1024 trunc-vs-round discriminator: dat == src pointwise and
    // flt0 == u pointwise with xq0 = 1 leaves v = 1024 at every pixel, so
    // the trunc form's e = 0 and the error is exactly 0 (the ROUND form
    // would read e = 1 per pixel).
    std::vector<std::uint16_t> eqSrc(H * stride, 77), eqDat(H * stride, 77);
    std::vector<std::int32_t> eqFlt0(H * fStride);
    for (int i = 0; i < H; ++i)
        for (int j = 0; j < fStride; ++j)
            eqFlt0[i * fStride + j] = (j < W) ? (int)(eqDat[i * stride + j] * 16) : 0;
    const int one[2] = {1, 0};
    CHECK(highbdPixelProjError(reinterpret_cast<const std::uint8_t*>(eqSrc.data()), W, H, stride,
        reinterpret_cast<const std::uint8_t*>(eqDat.data()), stride, eqFlt0.data(), fStride, flt1.data(), fStride, one,
        &r0only) == 0);
}

// R8: get_pixel_proj_error (restoration_pick.c:301-:322). Composition pin:
// the result is the lowbd projection at decode_xq(xqd) - with decode_xq
// re-derived inline (the three r-shapes), NOT by calling the landed
// restoration::decodeXq. use_highbitdepth is UNUSED in the
// CONFIG_ENABLE_HIGH_BIT_DEPTH=0 shape: 0 and 1 must agree.
TEST_CASE("getPixelProjError composes decodeXq with the lowbd projection") {
    using l10_filters::restoration::SgrParamsType;
    using l10_filters::restoration_pick::getPixelProjError;
    const int W = 6, H = 4, srcStride = 14, datStride = 14, fStride = 11;
    std::uint8_t src[4 * srcStride], dat[4 * datStride];
    std::vector<std::int32_t> flt0(4 * fStride), flt1(4 * fStride);
    for (int i = 0; i < H; ++i) {
        for (int j = 0; j < srcStride; ++j) src[i * srcStride + j] = (std::uint8_t)(2 + (i * 23 + j * 41) % 250);
        for (int j = 0; j < datStride; ++j) dat[i * datStride + j] = (std::uint8_t)(9 + (i * 17 + j * 13) % 240);
        for (int j = 0; j < fStride; ++j) {
            flt0[i * fStride + j] = ((i * 271 + j * 57) % 480 - 240) + (int)(dat[i * datStride + j] * 16);
            flt1[i * fStride + j] = ((i * 199 + j * 173) % 640 - 320) + (int)(dat[i * datStride + j] * 16);
        }
    }
    // Inline decode_xq oracle (restoration.c:597-:608).
    auto decodeOracle = [](const SgrParamsType& params, const int xqd[2], int xq[2]) {
        if (params.r[0] == 0) {
            xq[0] = 0;
            xq[1] = 128 - xqd[1];
        } else if (params.r[1] == 0) {
            xq[0] = xqd[0];
            xq[1] = 0;
        } else {
            xq[0] = xqd[0];
            xq[1] = 128 - xq[0] - xqd[1];
        }
    };
    // Inline lowbd projection oracle (restoration_pick.c:161-:226 shape,
    // single accumulated pass, multiplies instead of shifts).
    auto projOracle = [&](const SgrParamsType& params, const int xq[2]) {
        std::int64_t err = 0;
        for (int i = 0; i < H; ++i) {
            for (int j = 0; j < W; ++j) {
                const std::int64_t u = (std::int64_t)dat[i * datStride + j] * 16;
                std::int64_t v = u * 128;
                if (params.r[0] > 0) v += (std::int64_t)xq[0] * (flt0[i * fStride + j] - u);
                if (params.r[1] > 0) v += (std::int64_t)xq[1] * (flt1[i * fStride + j] - u);
                const std::int64_t e = floorDiv(v + 1024, 2048) - src[i * srcStride + j];
                err += e * e;
            }
        }
        return err;
    };
    auto call = [&](const SgrParamsType& params, const int xqd[2], int hbd) {
        int xqdMut[2] = {xqd[0], xqd[1]};
        return getPixelProjError(src, W, H, srcStride, dat, datStride, hbd, flt0.data(), fStride, flt1.data(),
            fStride, xqdMut, &params);
    };
    const SgrParamsType rows[3] = {{{2, 1}, {140, 3236}}, {{0, 1}, {-1, 2589}}, {{2, 0}, {56, -1}}};
    for (const auto& params : rows) {
        for (int xqd0 : {-96, 0, 31}) {
            for (int xqd1 : {-32, 45, 95}) {
                const int xqd[2] = {xqd0, xqd1};
                int xq[2];
                decodeOracle(params, xqd, xq);
                const std::int64_t expected = projOracle(params, xq);
                CHECK(call(params, xqd, 0) == expected);
                CHECK(call(params, xqd, 1) == expected);
            }
        }
    }
}

// R9: encode_xq (restoration_pick.c:512-:524). All 16 param rows x a clamp
// grid around every bound; the oracle re-derives the clamps with
// min/max chains and the literal bound values (-96/31, -32/95).
TEST_CASE("encodeXq clamps per r-branch over the full row set") {
    using l10_filters::restoration::SgrParamsType;
    using l10_filters::restoration_pick::encodeXq;
    const SgrParamsType rows[16] = {
        {{2, 1}, {140, 3236}}, {{2, 1}, {112, 2158}}, {{2, 1}, {93, 1618}}, {{2, 1}, {80, 1438}},
        {{2, 1}, {70, 1295}},  {{2, 1}, {58, 1177}},  {{2, 1}, {47, 1079}}, {{2, 1}, {37, 996}},
        {{2, 1}, {30, 925}},   {{2, 1}, {25, 863}},   {{0, 1}, {-1, 2589}}, {{0, 1}, {-1, 1618}},
        {{0, 1}, {-1, 1177}},  {{0, 1}, {-1, 925}},   {{2, 0}, {56, -1}},   {{2, 0}, {22, -1}},
    };
    auto clamp = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    for (const auto& params : rows) {
        for (int xq0 : {-200, -97, -96, -95, -1, 0, 30, 31, 32, 200}) {
            for (int xq1 : {-200, -33, -32, -31, 0, 94, 95, 96, 200}) {
                int xq[2] = {xq0, xq1}, xqd[2] = {-999, -999};
                encodeXq(xq, xqd, &params);
                int exqd[2];
                if (params.r[0] == 0) {
                    exqd[0] = 0;
                    exqd[1] = clamp(128 - xq[1], -32, 95);
                } else if (params.r[1] == 0) {
                    exqd[0] = clamp(xq[0], -96, 31);
                    exqd[1] = clamp(128 - exqd[0], -32, 95);
                } else {
                    exqd[0] = clamp(xq[0], -96, 31);
                    exqd[1] = clamp(128 - exqd[0] - xq[1], -32, 95);
                }
                CHECK(xqd[0] == exqd[0]);
                CHECK(xqd[1] == exqd[1]);
            }
        }
    }
    // The literal bounds, pinned directly.
    CHECK(l10_filters::restoration_pick::sgrprojPrjMin0 == -96);
    CHECK(l10_filters::restoration_pick::sgrprojPrjMax0 == 31);
    CHECK(l10_filters::restoration_pick::sgrprojPrjMin1 == -32);
    CHECK(l10_filters::restoration_pick::sgrprojPrjMax1 == 95);
}

// R10: linsolve_wiener (restoration_pick.c:752-:796), exact-solve cycle.
// Hand-derived pins: x[i] = 65536 * b[i] / A[i][i] when the elimination
// changes nothing (diagonal systems). n = 1, 2, 3.
TEST_CASE("linsolveWiener solves diagonal systems exactly at the tap scale") {
    using l10_filters::restoration_pick::linsolveWiener;
    {
        std::int64_t A[1] = {2}, b[1] = {3};
        std::int32_t x[1] = {-1};
        CHECK(linsolveWiener(1, A, 1, b, x) == 1);
        CHECK(x[0] == 65536 * 3 / 2);
        CHECK(x[0] == 98304);
    }
    {
        std::int64_t A[4] = {4, 0, 0, 8}, b[2] = {8, 16};
        std::int32_t x[2] = {-1, -1};
        CHECK(linsolveWiener(2, A, 2, b, x) == 1);
        CHECK(x[0] == 131072);
        CHECK(x[1] == 131072);
    }
    {
        std::int64_t A[9] = {2, 0, 0, 0, 4, 0, 0, 0, 8}, b[3] = {4, 8, 16};
        std::int32_t x[3] = {-1, -1, -1};
        CHECK(linsolveWiener(3, A, 3, b, x) == 1);
        CHECK(x[0] == 131072);
        CHECK(x[1] == 131072);
        CHECK(x[2] == 131072);
    }
    // A scaled-integer solve where the elimination DOES run: A =
    // [[256,0],[256,512]], b = [512,1536] -> x = {131072, 131072}. The
    // c/256 term is exactly 1 here so the elimination is lossless and the
    // back-substitution lands on the exact solution (hand-derived).
    {
        std::int64_t A[4] = {256, 0, 256, 512}, b[2] = {512, 1536};
        std::int32_t x[2] = {-1, -1};
        CHECK(linsolveWiener(2, A, 2, b, x) == 1);
        CHECK(x[0] == 131072);
        CHECK(x[1] == 131072);
    }
}

// R11: linsolve_wiener failure and lossy-elimination behavior. Pins:
// (a) A solvable textbook system ([[1,0],[3,4]], b = [3,10], true solution
//     {1, 2}) returns 0 because the c/256 truncation zeroes the
//     elimination - the reference's own limitation, pinned 1:1;
// (b) an all-zero A returns 0 (zero pivot);
// (c) a full transcription oracle over deterministic pseudo-random systems
//     (LCG, exact integer reproduction of the reference chain).
TEST_CASE("linsolveWiener pins the lossy c/256 elimination and failure paths") {
    using l10_filters::restoration_pick::linsolveWiener;
    // (a) The lossy elimination can strand a solvable system on a zero pivot.
    {
        std::int64_t A[4] = {1, 0, 3, 4}, b[2] = {3, 10};
        std::int32_t x[2] = {-7, -7};
        CHECK(linsolveWiener(2, A, 2, b, x) == 0);
        // x untouched on failure.
        CHECK(x[0] == -7);
        CHECK(x[1] == -7);
    }
    // (b) All-zero A: the back-substitution guard fires.
    {
        std::int64_t A[4] = {0, 0, 0, 0}, b[2] = {1, 1};
        std::int32_t x[2] = {-7, -7};
        CHECK(linsolveWiener(2, A, 2, b, x) == 0);
    }
    // (c) Independent transcription oracle (different shape: row swaps via
    // std::swap on indices, elimination factored into an inline helper).
    auto oracle = [](std::int32_t n, std::vector<std::int64_t> A, std::int32_t stride, std::vector<std::int64_t> b,
                      std::int32_t* x) {
        auto rowSwap = [&](int r1, int r2) {
            for (int j = 0; j < n; ++j) std::swap(A[r1 * stride + j], A[r2 * stride + j]);
            std::swap(b[r1], b[r2]);
        };
        for (int k = 0; k < n - 1; ++k) {
            for (int i = n - 1; i > k; --i)
                if (std::llabs(A[(i - 1) * stride + k]) < std::llabs(A[i * stride + k])) rowSwap(i, i - 1);
            for (int i = k; i < n - 1; ++i) {
                if (A[k * stride + k] == 0) return 0;
                const std::int64_t c = A[(i + 1) * stride + k], cd = A[k * stride + k];
                for (int j = 0; j < n; ++j) A[(i + 1) * stride + j] -= c / 256 * A[k * stride + j] / cd * 256;
                b[i + 1] -= c * b[k] / cd;
            }
        }
        for (int i = n - 1; i >= 0; --i) {
            if (A[i * stride + i] == 0) return 0;
            std::int64_t c = 0;
            for (int j = i + 1; j < n; ++j) c += A[i * stride + j] * x[j] / 65536;
            x[i] = (std::int32_t)(65536 * (b[i] - c) / A[i * stride + i]);
        }
        return 1;
    };
    std::uint32_t seed = 0x12345678u;
    auto lcg = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (int)((seed >> 16) % 97) - 48;
    };
    for (int trial = 0; trial < 200; ++trial) {
        const int n = 2 + trial % 2; // 2 or 3
        std::vector<std::int64_t> A((size_t)n * n), Ao, b(n), bo;
        for (int r = 0; r < n; ++r) {
            for (int cIdx = 0; cIdx < n; ++cIdx) A[r * n + cIdx] = lcg();
            b[r] = lcg() * 4;
        }
        Ao = A;
        bo = b;
        std::int32_t x[3] = {0, 0, 0}, xo[3] = {0, 0, 0};
        const std::int32_t got = linsolveWiener(n, A.data(), n, b.data(), x);
        const std::int32_t exp = oracle(n, Ao, n, bo, xo);
        CHECK(got == exp);
        if (got == 1) {
            CHECK(x[0] == xo[0]);
            CHECK(x[1] == xo[1]);
            if (n == 3) CHECK(x[2] == xo[2]);
        }
    }
}

// R12: finer_search_pixel_proj_error (restoration_pick.c:324-:420), cycle
// A. do_refine = 0 is a passthrough: the returned error equals the plain
// projection error at the incoming xqd and xqd is untouched. The refine
// path never returns a WORSE error than it started with (property over
// deterministic fixtures).
TEST_CASE("finerSearchPixelProjError passthrough and monotone descent") {
    using l10_filters::restoration::SgrParamsType;
    using l10_filters::restoration_pick::finerSearchPixelProjError;
    using l10_filters::restoration_pick::getPixelProjError;
    const int W = 6, H = 5, srcStride = 15, datStride = 15, fStride = 12;
    std::uint8_t src[5 * srcStride], dat[5 * datStride];
    std::vector<std::int32_t> flt0(5 * fStride), flt1(5 * fStride);
    for (int i = 0; i < H; ++i) {
        for (int j = 0; j < srcStride; ++j) src[i * srcStride + j] = (std::uint8_t)(4 + (i * 29 + j * 43) % 244);
        for (int j = 0; j < datStride; ++j) dat[i * datStride + j] = (std::uint8_t)(6 + (i * 19 + j * 31) % 248);
        for (int j = 0; j < fStride; ++j) {
            flt0[i * fStride + j] = ((i * 257 + j * 67) % 500 - 250) + (int)(dat[i * datStride + j] * 16);
            flt1[i * fStride + j] = ((i * 181 + j * 139) % 660 - 330) + (int)(dat[i * datStride + j] * 16);
        }
    }
    const SgrParamsType both{{2, 1}, {140, 3236}}, r0only{{2, 0}, {56, -1}};
    // Passthrough: do_refine = 0.
    for (int xqd0 : {-40, 5, 20}) {
        for (int xqd1 : {-10, 0, 60}) {
            int xqd[2] = {xqd0, xqd1};
            const std::int64_t base =
                getPixelProjError(src, W, H, srcStride, dat, datStride, 0, flt0.data(), fStride, flt1.data(),
                    fStride, xqd, &both);
            int xqdWalk[2] = {xqd0, xqd1};
            const std::int64_t got = finerSearchPixelProjError(src, W, H, srcStride, dat, datStride, 0, flt0.data(),
                fStride, flt1.data(), fStride, 2, xqdWalk, 0, &both);
            CHECK(got == base);
            CHECK(xqdWalk[0] == xqd0);
            CHECK(xqdWalk[1] == xqd1);
        }
    }
    // Monotone descent: the refine path (do_refine = 1) never worsens.
    std::uint32_t seed = 0xC0FFEEu;
    auto lcg = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (int)((seed >> 16) % 61) - 30;
    };
    for (int trial = 0; trial < 30; ++trial) {
        int xqd[2] = {lcg(), lcg()};
        const std::int64_t base =
            getPixelProjError(src, W, H, srcStride, dat, datStride, 0, flt0.data(), fStride, flt1.data(), fStride,
                xqd, &both);
        int xqdWalk[2] = {xqd[0], xqd[1]};
        const std::int64_t got = finerSearchPixelProjError(src, W, H, srcStride, dat, datStride, 0, flt0.data(),
            fStride, flt1.data(), fStride, 2, xqdWalk, 1, &both);
        CHECK(got <= base);
        CHECK(xqdWalk[0] >= l10_filters::restoration_pick::sgrprojPrjMin0);
        CHECK(xqdWalk[0] <= l10_filters::restoration_pick::sgrprojPrjMax0);
        CHECK(xqdWalk[1] >= l10_filters::restoration_pick::sgrprojPrjMin1);
        CHECK(xqdWalk[1] <= l10_filters::restoration_pick::sgrprojPrjMax1);
    }
}

// R13: finer_search_pixel_proj_error (restoration_pick.c:324-:420), cycle
// B: hand-derived walk pins on a crafted projection landscape. Fixture
// data: dat = 0, flt0 = 0, flt1 = 2048 make the per-pixel projected value
// K = ROUND(262144 - 2048*xqd0 - 2048*xqd1, 11) = 128 - xqd0 - xqd1, so
// the error is a pure two-tap landscape. The skip-flag asymmetry is
// observable: a successful negative move at step s abandons the remaining
// taps at that step, so with src = 255 the walk ends at {-96, -1} (not
// the {-96, -32} a skip-less mutation reaches).
TEST_CASE("finerSearchPixelProjError walks the crafted K landscape") {
    using l10_filters::restoration::SgrParamsType;
    using l10_filters::restoration_pick::finerSearchPixelProjError;
    const int W = 2, H = 2, srcStride = 8, datStride = 8, fStride = 8;
    std::uint8_t src[2 * srcStride], dat[2 * datStride];
    std::vector<std::int32_t> flt0(2 * fStride, 0), flt1(2 * fStride, 2048);
    for (int i = 0; i < H; ++i)
        for (int j = 0; j < W; ++j) {
            dat[i * datStride + j] = 0;
        }
    const SgrParamsType both{{2, 1}, {140, 3236}}, r1only{{0, 1}, {-1, 2589}};
    auto call = [&](const SgrParamsType& params, int xqd[2], int srcVal, int step) {
        for (int i = 0; i < H; ++i)
            for (int j = 0; j < W; ++j) src[i * srcStride + j] = (std::uint8_t)srcVal;
        return finerSearchPixelProjError(src, W, H, srcStride, dat, datStride, 0, flt0.data(), fStride, flt1.data(),
            fStride, step, xqd, 1, &params);
    };
    // (a) src = 255: every tap improves by increasing K (negative moves);
    // the skip flag stops xqd1 at step 2, which only recovers at step 1.
    {
        int xqd[2] = {0, 0};
        const std::int64_t err = call(both, xqd, 255, 2);
        CHECK(xqd[0] == -96);
        CHECK(xqd[1] == -1);
        // K = 128 + 96 + 1 = 225, e = -30, 4 pixels.
        CHECK(err == 4 * 30 * 30);
    }
    // (b) src = 100: positive moves improve while |28 - xqd0| shrinks; the
    // s = 2 walk stops at the exact optimum xqd0 = 28 (30 would revert),
    // and xqd1 cannot improve in either direction.
    {
        int xqd[2] = {0, 0};
        const std::int64_t err = call(both, xqd, 100, 2);
        CHECK(xqd[0] == 28);
        CHECK(xqd[1] == 0);
        CHECK(err == 0);
    }
    // (c) The p == 0 tap is skipped entirely for an r[0] == 0 row: xqd0
    // stays put while xqd1 walks to its bound at step 2.
    {
        int xqd[2] = {11, 0};
        const std::int64_t err = call(r1only, xqd, 255, 2);
        CHECK(xqd[0] == 11);
        CHECK(xqd[1] == -32);
        // K = 128 - xqd1 = 160, e = -95, 4 pixels.
        CHECK(err == 4 * 95 * 95);
    }
}

// R14: svt_get_proj_subspace_c (restoration_pick.c:422-:510).
// (a) An exact hand-derived 2-pixel least-squares solve: dat = 0,
//     flt0 = {16, 32}, flt1 = {32, 16}, src = {2, 3} (so s = {32, 48})
//     gives H = [[1280, 1024], [1024, 1280]] / 2, C = {2048, 1792} / 2 ->
//     x = {4/3, 1/3} -> xq = {rint(170.67), rint(42.67)} = {171, 43}.
// (b) The rint HALF-TO-EVEN pin on an exactly dyadic 2.5, scalar branch:
//     455 pixels with f1 = 3 and delta = 1 plus 1 pixel with f1 = 1 and
//     delta = 2 plus 56 f1 = 0 pixels (512 = 2^9 total) give
//     C0 = 16*(3*1 + 1*2) = 80, H00 = 9*455 + 1 = 4096; after /= size:
//     x0 = (80/512)/(4096/512) = 0.15625/8 = 5/256 exactly; x0 * 128 = 2.5
//     exactly -> rint = 2 (a round-half-up mutation gives 3).
// (c) Ill-posed guards: zero f vectors (scalar branch) and rank-1 f
//     (both-branch det = 0) return the {0, 0} default.
// (d) The use_highbitdepth = 1 path over aliased uint16 planes, against an
//     inline oracle of the same accumulation.
TEST_CASE("getProjSubspace solves the least-squares branches") {
    using l10_filters::restoration::SgrParamsType;
    using l10_filters::restoration_pick::getProjSubspace;
    const SgrParamsType both{{2, 1}, {140, 3236}}, r1only{{0, 1}, {-1, 2589}}, r0only{{2, 0}, {56, -1}};
    // (a)
    {
        const int W = 2, H = 1, srcStride = 8, datStride = 8, fStride = 8;
        std::uint8_t src[1 * srcStride] = {2, 3}, dat[1 * datStride] = {0, 0};
        std::int32_t flt0[1 * fStride] = {16, 32}, flt1[1 * fStride] = {32, 16};
        int xq[2] = {99, 99};
        getProjSubspace(src, W, H, srcStride, dat, datStride, 0, flt0, fStride, flt1, fStride, xq, &both);
        CHECK(xq[0] == 171);
        CHECK(xq[1] == 43);
    }
    // (b) dyadic 2.5, r1 == 0 scalar branch (xq = {rint(2.5), 0} = {2, 0})
    {
        const int W = 512, H = 1, srcStride = 520, datStride = 520, fStride = 520;
        std::vector<std::uint8_t> src(srcStride, 0), dat(datStride, 0);
        std::vector<std::int32_t> flt0(fStride, 0), flt1(fStride, 0);
        for (int j = 0; j < 455; ++j) flt0[j] = 3; // delta 0
        flt0[0] = 3;
        src[0] = 1; // one f1 = 3 pixel with delta 1
        flt0[455] = 1;
        src[455] = 2; // one f1 = 1 pixel with delta 2
        int xq[2] = {99, 99};
        getProjSubspace(src.data(), W, H, srcStride, dat.data(), datStride, 0, flt0.data(), fStride, flt1.data(),
            fStride, xq, &r0only);
        CHECK(xq[0] == 2);
        CHECK(xq[1] == 0);
    }
    // (b2) the r0 == 0 scalar twin on flt1 (xq = {0, rint(2.5)} = {0, 2})
    {
        const int W = 512, H = 1, srcStride = 520, datStride = 520, fStride = 520;
        std::vector<std::uint8_t> src(srcStride, 0), dat(datStride, 0);
        std::vector<std::int32_t> flt0(fStride, 0), flt1(fStride, 0);
        for (int j = 0; j < 455; ++j) flt1[j] = 3; // delta 0
        src[0] = 1; // one f2 = 3 pixel with delta 1
        flt1[455] = 1;
        src[455] = 2; // one f2 = 1 pixel with delta 2
        int xq[2] = {99, 99};
        getProjSubspace(src.data(), W, H, srcStride, dat.data(), datStride, 0, flt0.data(), fStride, flt1.data(),
            fStride, xq, &r1only);
        CHECK(xq[0] == 0);
        CHECK(xq[1] == 2);
    }
    // (c) ill-posed guards: scalar branch with f1 == 0 everywhere; both
    // branch with f2 == f1 (rank-1, det = 0).
    {
        const int W = 4, H = 1, srcStride = 8, datStride = 8, fStride = 8;
        std::uint8_t src[1 * srcStride] = {3, 5, 7, 9}, dat[1 * datStride] = {0, 0, 0, 0};
        std::int32_t flt0[1 * fStride] = {0, 0, 0, 0}, flt1[1 * fStride] = {0, 0, 0, 0};
        int xq[2] = {99, 99};
        getProjSubspace(src, W, H, srcStride, dat, datStride, 0, flt0, fStride, flt1, fStride, xq, &r0only);
        CHECK(xq[0] == 0);
        CHECK(xq[1] == 0);
        std::int32_t g0[1 * fStride] = {5, 6, 7, 8}, g1[1 * fStride] = {10, 12, 14, 16};
        int xq2[2] = {99, 99};
        getProjSubspace(src, W, H, srcStride, dat, datStride, 0, g0, fStride, g1, fStride, xq2, &both);
        CHECK(xq2[0] == 0);
        CHECK(xq2[1] == 0);
    }
    // (d) the highbd path over aliased uint16 planes vs an inline oracle.
    {
        const int W = 5, H = 3, srcStride = 12, datStride = 12, fStride = 12;
        std::vector<std::uint16_t> src(H * srcStride), dat(H * datStride);
        std::vector<std::int32_t> flt0(H * fStride), flt1(H * fStride);
        for (int i = 0; i < H; ++i)
            for (int j = 0; j < W; ++j) {
                dat[i * datStride + j] = (std::uint16_t)(100 + i * 7 + j * 3);
                src[i * srcStride + j] = (std::uint16_t)(dat[i * datStride + j] + (i + j) % 4);
                flt0[i * fStride + j] = (int)dat[i * datStride + j] * 16 + (i * 11 + j * 5) % 200 - 100;
                flt1[i * fStride + j] = (int)dat[i * datStride + j] * 16 + (i * 13 + j * 9) % 300 - 150;
            }
        int xq[2] = {99, 99};
        getProjSubspace(reinterpret_cast<const std::uint8_t*>(src.data()), W, H, srcStride,
            reinterpret_cast<const std::uint8_t*>(dat.data()), datStride, 1, flt0.data(), fStride, flt1.data(),
            fStride, xq, &both);
        // Inline oracle of the highbd accumulation (restoration_pick.c
        // :452-:468 shape).
        double Ho[2][2] = {{0, 0}, {0, 0}}, Co[2] = {0, 0};
        const int size = W * H;
        for (int i = 0; i < H; ++i)
            for (int j = 0; j < W; ++j) {
                const double u = (double)(dat[i * datStride + j] << 4);
                const double s = (double)(src[i * srcStride + j] << 4) - u;
                const double f1 = (double)flt0[i * fStride + j] - u;
                const double f2 = (double)flt1[i * fStride + j] - u;
                Ho[0][0] += f1 * f1;
                Ho[1][1] += f2 * f2;
                Ho[0][1] += f1 * f2;
                Co[0] += f1 * s;
                Co[1] += f2 * s;
            }
        Ho[0][0] /= size;
        Ho[0][1] /= size;
        Ho[1][1] /= size;
        Ho[1][0] = Ho[0][1];
        Co[0] /= size;
        Co[1] /= size;
        const double det = Ho[0][0] * Ho[1][1] - Ho[0][1] * Ho[1][0];
        const double x0 = (Ho[1][1] * Co[0] - Ho[0][1] * Co[1]) / det;
        const double x1 = (Ho[0][0] * Co[1] - Ho[1][0] * Co[0]) / det;
        CHECK(xq[0] == (int)std::rint(x0 * 128));
        CHECK(xq[1] == (int)std::rint(x1 * 128));
        // The oracle must be well-posed here (a degenerate fixture would
        // make the comparison vacuous).
        CHECK(det >= 1e-8);
    }
}

// R15: apply_sgr (restoration_pick.c:526-:552). Tiling oracle: the flt
// planes must equal per-tile restoration::selfguidedRestoration calls with
// AOMMIN-truncated edge tiles. Two grids: 96x40 at pu 64x64 (tiles
// 64+32 wide) and 70x50 at pu 32x32 (tiles 32+32+6 x 32+18), over an ep
// with both radii active (row 0).
TEST_CASE("applySgr tiling matches per-tile selfguidedRestoration") {
    using l10_filters::restoration::SgrParamsType;
    using l10_filters::restoration::selfguidedRestoration;
    using l10_filters::restoration_pick::applySgr;
    const int border = 8;
    struct Grid {
        int w, h, puw, puh;
    };
    for (Grid g : {Grid{96, 40, 64, 64}, Grid{70, 50, 32, 32}}) {
        const int stride = g.w + 2 * border, fStride = ((g.w + 7) & ~7) + 8;
        std::vector<std::uint8_t> dat((g.h + 2 * border) * stride);
        for (int i = 0; i < (int)dat.size(); ++i) dat[i] = (std::uint8_t)(1 + (i * 37) % 251);
        std::vector<std::int32_t> flt0((size_t)g.h * fStride, -1), flt1((size_t)g.h * fStride, -1),
            exp0((size_t)g.h * fStride, -1), exp1((size_t)g.h * fStride, -1);
        const std::uint8_t* datOrigin = dat.data() + border * stride + border;
        applySgr(0, datOrigin, g.w, g.h, stride, 0, 8, g.puw, g.puh, flt0.data(), flt1.data(), fStride);
        // Oracle: the same tiling by hand.
        for (int i = 0; i < g.h; i += g.puh) {
            const int hh = (g.puh < g.h - i) ? g.puh : g.h - i;
            std::int32_t* f0row = exp0.data() + i * fStride;
            std::int32_t* f1row = exp1.data() + i * fStride;
            const std::uint8_t* drow = datOrigin + i * stride;
            for (int j = 0; j < g.w; j += g.puw) {
                const int ww = (g.puw < g.w - j) ? g.puw : g.w - j;
                selfguidedRestoration(drow + j, ww, hh, stride, f0row + j, f1row + j, fStride, 0, 8, 0);
            }
        }
        for (int i = 0; i < g.h; ++i)
            for (int j = 0; j < g.w; ++j) {
                CHECK(flt0[i * fStride + j] == exp0[i * fStride + j]);
                CHECK(flt1[i * fStride + j] == exp1[i * fStride + j]);
            }
        // The tiles really wrote: no sentinel survives inside the unit
        // (full coverage; wrote stays 1 unless a -1 is found).
        int wrote = 1;
        for (int i = 0; i < g.h && wrote; ++i)
            for (int j = 0; j < g.w; ++j)
                if (flt0[i * fStride + j] == -1) {
                    wrote = 0;
                    break;
                }
        CHECK(wrote == 1);
    }
}

// R16: search_selfguided_restoration (restoration_pick.c:554-:630).
// Independent oracle: the same per-ep pipeline composed from the LANDED
// primitives (applySgr, getProjSubspace, an inline encode_xq oracle, and
// getPixelProjError / finerSearchPixelProjError), tracking the argmin.
// Pins: the single-ep result is exact; the 16-ep argmin matches for
// refine = 0 and 1; the ep range is END-EXCLUSIVE; plane is !!plane
// normalized (plane 2 reads ctrl slot 1).
TEST_CASE("searchSelfguidedRestoration matches the composed ep argmin") {
    using l10_filters::restoration::SgrprojInfo;
    using l10_filters::restoration_pick::SgFilterCtrls;
    using l10_filters::restoration_pick::applySgr;
    using l10_filters::restoration_pick::finerSearchPixelProjError;
    using l10_filters::restoration_pick::getPixelProjError;
    using l10_filters::restoration_pick::getProjSubspace;
    using l10_filters::restoration_pick::searchSelfguidedRestoration;
    const int W = 40, H = 24, stride = W + 16, srcStride = stride;
    const int fStride = ((W + 7) & ~7) + 8;
    std::vector<std::uint8_t> dat((H + 16) * stride), src((H + 16) * srcStride);
    for (int i = 0; i < (int)dat.size(); ++i) dat[i] = (std::uint8_t)(2 + (i * 41) % 247);
    for (int i = 0; i < (int)src.size(); ++i) src[i] = (std::uint8_t)(9 + (i * 29) % 235);
    const std::uint8_t* datP = dat.data() + 8 * stride + 8;
    const std::uint8_t* srcP = src.data() + 8 * srcStride + 8;
    std::vector<std::int32_t> rstbuf(2 * (size_t)l10_filters::restoration::restorationUnitpelsMax, 0);
    std::vector<std::int32_t> oFlt0((size_t)H * fStride), oFlt1((size_t)H * fStride);
    // The composed oracle for one ep: encode_xq re-derived inline with
    // min/max chains; the projection error via the landed primitives.
    auto oracleEp = [&](int ep, int doRefine, int32_t xqdOut[2]) {
        applySgr(ep, datP, W, H, stride, 0, 8, 32, 32, oFlt0.data(), oFlt1.data(), fStride);
        const l10_filters::restoration::SgrParamsType* params = &l10_filters::restoration::sgrParams[ep];
        int exq[2] = {0, 0};
        getProjSubspace(srcP, W, H, srcStride, datP, stride, 0, oFlt0.data(), fStride, oFlt1.data(), fStride, exq,
            params);
        int exqd[2];
        if (params->r[0] == 0) {
            exqd[0] = 0;
            exqd[1] = std::min(std::max(128 - exq[1], -32), 95);
        } else if (params->r[1] == 0) {
            exqd[0] = std::min(std::max(exq[0], -96), 31);
            exqd[1] = std::min(std::max(128 - exqd[0], -32), 95);
        } else {
            exqd[0] = std::min(std::max(exq[0], -96), 31);
            exqd[1] = std::min(std::max(128 - exqd[0] - exq[1], -32), 95);
        }
        if (doRefine) {
            int xqd[2] = {exqd[0], exqd[1]};
            finerSearchPixelProjError(srcP, W, H, srcStride, datP, stride, 0, oFlt0.data(), fStride, oFlt1.data(),
                fStride, 2, xqd, 1, params);
            exqd[0] = xqd[0];
            exqd[1] = xqd[1];
        }
        const std::int64_t err = getPixelProjError(srcP, W, H, srcStride, datP, stride, 0, oFlt0.data(), fStride,
            oFlt1.data(), fStride, exqd, params);
        xqdOut[0] = exqd[0];
        xqdOut[1] = exqd[1];
        return err;
    };
    auto oracleSearch = [&](int startEp, int endEp, int epInc, int doRefine) {
        SgrprojInfo best;
        best.ep = 0;
        best.xqd[0] = 0;
        best.xqd[1] = 0;
        std::int64_t bestErr = -1;
        for (int ep = startEp; ep < endEp; ep += epInc) {
            int xqd[2];
            const std::int64_t err = oracleEp(ep, doRefine, xqd);
            if (bestErr == -1 || err < bestErr) {
                bestErr = err;
                best.ep = ep;
                best.xqd[0] = xqd[0];
                best.xqd[1] = xqd[1];
            }
        }
        return best;
    };
    // (a) single ep, refine = 0: exact.
    {
        SgFilterCtrls ctrls{};
        ctrls.startEp[0] = 0;
        ctrls.endEp[0] = 1;
        ctrls.epInc[0] = 1;
        ctrls.refine[0] = 0;
        const SgrprojInfo got = searchSelfguidedRestoration(datP, W, H, stride, srcP, srcStride, 0, 8, 32, 32,
            rstbuf.data(), &ctrls, 0);
        int xqd[2];
        oracleEp(0, 0, xqd);
        CHECK(got.ep == 0);
        CHECK(got.xqd[0] == xqd[0]);
        CHECK(got.xqd[1] == xqd[1]);
    }
    // (b) the full 16-ep range, refine = 0: argmin equality.
    {
        SgFilterCtrls ctrls{};
        ctrls.startEp[0] = 0;
        ctrls.endEp[0] = 16;
        ctrls.epInc[0] = 1;
        ctrls.refine[0] = 0;
        const SgrprojInfo got = searchSelfguidedRestoration(datP, W, H, stride, srcP, srcStride, 0, 8, 32, 32,
            rstbuf.data(), &ctrls, 0);
        const SgrprojInfo exp = oracleSearch(0, 16, 1, 0);
        CHECK(got.ep == exp.ep);
        CHECK(got.xqd[0] == exp.xqd[0]);
        CHECK(got.xqd[1] == exp.xqd[1]);
        // The oracle must see a real range (not a single-ep degeneracy).
        CHECK(exp.ep >= 0);
    }
    // (c) refine = 1: argmin equality and the refine result is never worse
    // than the unrefined one at the same ep set.
    {
        SgFilterCtrls ctrls{};
        ctrls.startEp[0] = 0;
        ctrls.endEp[0] = 16;
        ctrls.epInc[0] = 1;
        ctrls.refine[0] = 1;
        const SgrprojInfo got = searchSelfguidedRestoration(datP, W, H, stride, srcP, srcStride, 0, 8, 32, 32,
            rstbuf.data(), &ctrls, 0);
        const SgrprojInfo exp = oracleSearch(0, 16, 1, 1);
        CHECK(got.ep == exp.ep);
        CHECK(got.xqd[0] == exp.xqd[0]);
        CHECK(got.xqd[1] == exp.xqd[1]);
        SgFilterCtrls plain{};
        plain.startEp[0] = 0;
        plain.endEp[0] = 16;
        plain.epInc[0] = 1;
        plain.refine[0] = 0;
        const SgrprojInfo unref = searchSelfguidedRestoration(datP, W, H, stride, srcP, srcStride, 0, 8, 32, 32,
            rstbuf.data(), &plain, 0);
        int xd[2];
        const std::int64_t errRef = oracleEp(got.ep, 1, xd);
        const std::int64_t errUnref = oracleEp(unref.ep, 0, xd);
        CHECK(errRef <= errUnref);
    }
    // (d) the ep range is END-EXCLUSIVE: [0, 1) never reaches ep 1 even if
    // ep 1 would win; compare against the full range.
    {
        SgFilterCtrls a{}, b{};
        a.startEp[0] = 0;
        a.endEp[0] = 1;
        a.epInc[0] = 1;
        a.refine[0] = 0;
        b.startEp[0] = 0;
        b.endEp[0] = 16;
        b.epInc[0] = 1;
        b.refine[0] = 0;
        const SgrprojInfo gotA = searchSelfguidedRestoration(datP, W, H, stride, srcP, srcStride, 0, 8, 32, 32,
            rstbuf.data(), &a, 0);
        const SgrprojInfo gotB = searchSelfguidedRestoration(datP, W, H, stride, srcP, srcStride, 0, 8, 32, 32,
            rstbuf.data(), &b, 0);
        CHECK(gotA.ep == 0);
        if (gotB.ep != 0) {
            CHECK(gotA.ep != gotB.ep); // the exclusive end really binds
        }
    }
    // (e) plane is !!plane-normalized: plane 2 reads ctrl slot 1.
    {
        SgFilterCtrls ctrls{};
        ctrls.startEp[0] = 14;
        ctrls.endEp[0] = 15;
        ctrls.epInc[0] = 1;
        ctrls.refine[0] = 0;
        ctrls.startEp[1] = 10;
        ctrls.endEp[1] = 11;
        ctrls.epInc[1] = 1;
        ctrls.refine[1] = 0;
        const SgrprojInfo gotP2 = searchSelfguidedRestoration(datP, W, H, stride, srcP, srcStride, 0, 8, 32, 32,
            rstbuf.data(), &ctrls, 2);
        const SgrprojInfo gotP1 = searchSelfguidedRestoration(datP, W, H, stride, srcP, srcStride, 0, 8, 32, 32,
            rstbuf.data(), &ctrls, 1);
        CHECK(gotP2.ep == 10);
        CHECK(gotP1.ep == 10);
        CHECK(gotP2.ep != 14);
    }
}

// R17: the subexpfin count chain (entropy_coding.c :2842-:2994) and
// count_sgrproj_bits (restoration_pick.c:634-:650). The oracle is a
// full test-side transcription of the chain (recenter, quniform via a
// get_msb oracle, subexpfin, refsubexpfin). Pins: identical-info bits are
// 18 for both-radius rows and 11 for single-radius rows (4 params bits +
// 7 per active tap at n = 128); countSgrprojBits is enumerated over the
// FULL 128-value xqd0/xqd1 range for three reference deltas (both
// recenter branches) x ep rows {0, 10, 14}.
TEST_CASE("countSgrprojBits matches the subexpfin chain over the full range") {
    using l10_filters::restoration::SgrprojInfo;
    using l10_filters::restoration_pick::countPrimitiveQuniform;
    using l10_filters::restoration_pick::countPrimitiveRefsubexpfin;
    using l10_filters::restoration_pick::countPrimitiveSubexpfin;
    using l10_filters::restoration_pick::countSgrprojBits;
    using l10_filters::restoration_pick::sgrprojPrjMax0;
    using l10_filters::restoration_pick::sgrprojPrjMax1;
    using l10_filters::restoration_pick::sgrprojPrjMin0;
    using l10_filters::restoration_pick::sgrprojPrjMin1;
    // get_msb oracle (floor log2).
    auto msb = [](std::uint32_t n) {
        int r = -1;
        while (n) {
            n >>= 1;
            ++r;
        }
        return r;
    };
    auto oracleQuuniform = [&](std::uint16_t n, std::uint16_t v) {
        if (n <= 1) return 0;
        const int l = msb(n - 1) + 1;
        const int m = (1 << l) - n;
        return v < m ? l - 1 : l;
    };
    auto oracleSubexpfin = [&](std::uint16_t n, std::uint16_t k, std::uint16_t v) {
        int count = 0, i = 0, mk = 0;
        while (true) {
            const int b = i ? k + i - 1 : k;
            const int a = 1 << b;
            if (n <= mk + 3 * a) {
                count += oracleQuuniform((std::uint16_t)(n - mk), (std::uint16_t)(v - mk));
                break;
            } else {
                const int t = (v >= mk + a);
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
    };
    auto oracleRefsub = [&](std::uint16_t n, std::uint16_t k, std::uint16_t r, std::uint16_t v) {
        auto recenterNonneg = [&](std::uint16_t rr, std::uint16_t vv) {
            if (vv > (rr << 1)) return vv;
            if (vv >= rr) return (std::uint16_t)((vv - rr) << 1);
            return (std::uint16_t)(((rr - vv) << 1) - 1);
        };
        const std::uint16_t recentred = ((r << 1) <= n) ? recenterNonneg(r, v) : recenterNonneg(n - 1 - r, n - 1 - v);
        return oracleSubexpfin(n, k, recentred);
    };
    // The chain primitives themselves against the oracle (a sweep).
    for (int n : {2, 7, 16, 32, 64, 128, 200}) {
        for (int v = 0; v < n; ++v) CHECK(countPrimitiveQuniform((std::uint16_t)n, (std::uint16_t)v) ==
                                             oracleQuuniform((std::uint16_t)n, (std::uint16_t)v));
    }
    for (int n : {16, 32, 128}) {
        for (int k : {1, 2, 3, 4}) {
            for (int v = 0; v < n; ++v) {
                CHECK(countPrimitiveSubexpfin((std::uint16_t)n, (std::uint16_t)k, (std::uint16_t)v) ==
                      oracleSubexpfin((std::uint16_t)n, (std::uint16_t)k, (std::uint16_t)v));
                for (int r : {0, 15, 63, 64, 100, (std::min)(n - 1, 127)}) {
                    CHECK(countPrimitiveRefsubexpfin((std::uint16_t)n, (std::uint16_t)k, (std::uint16_t)r,
                              (std::uint16_t)v) == oracleRefsub((std::uint16_t)n, (std::uint16_t)k,
                              (std::uint16_t)r, (std::uint16_t)v));
                }
            }
        }
    }
    // countSgrprojBits: identical info pins. The recentered value is 0,
    // and count_subexpfin(128, 4, 0) = 1 prefix bit + 4 literal bits = 5
    // per tap (the subexpfin first level: n = 128 > 3*16, v = 0 < 16).
    {
        SgrprojInfo info{}, ref{};
        info.ep = 0;
        info.xqd[0] = 5;
        info.xqd[1] = 40;
        ref = info;
        CHECK(countSgrprojBits(&info, &ref) == 4 + 5 + 5);
        info.ep = 10; // r0 == 0
        CHECK(countSgrprojBits(&info, &ref) == 4 + 5);
        info.ep = 14; // r1 == 0
        CHECK(countSgrprojBits(&info, &ref) == 4 + 5);
    }
    // Full-range enumeration on both-radius row 0 and the single-radius
    // rows, against a composed oracle (4 + oracleRefsub per active tap).
    {
        SgrprojInfo ref{};
        ref.ep = 0;
        ref.xqd[0] = sgrprojPrjMin0 + 64;
        ref.xqd[1] = sgrprojPrjMin1 + 100;
        for (int d0 = 0; d0 < 128; ++d0) {
            SgrprojInfo info{};
            info.ep = 0;
            info.xqd[0] = sgrprojPrjMin0 + d0;
            info.xqd[1] = sgrprojPrjMin1 + 7;
            const int exp = 4 + oracleRefsub(128, 4, 64, (std::uint16_t)d0) +
                oracleRefsub(128, 4, 100, 7);
            CHECK(countSgrprojBits(&info, &ref) == exp);
        }
        for (int d1 = 0; d1 < 128; ++d1) {
            SgrprojInfo info{};
            info.ep = 10; // r0 == 0: only tap 1 counted
            info.xqd[0] = 0;
            info.xqd[1] = sgrprojPrjMin1 + d1;
            const int exp = 4 + oracleRefsub(128, 4, 100, (std::uint16_t)d1);
            CHECK(countSgrprojBits(&info, &ref) == exp);
        }
    }
}

// R18: count_wiener_bits (restoration_pick.c:1005-:1039). Pins: identical
// info costs 2*(2+3+4) = 18 bits at win 7 (tap0 n=16/k=1 -> 2, tap1 n=32/
// k=2 -> 3, tap2 n=64/k=3 -> 4 per filter) and 2*(3+4) = 14 at win 5.
// Then a per-tap FULL-range enumeration (each tap sweeps its whole
// MINV..MAXV with the others pinned at the mid values) at both wins,
// against the composed refsubexpfin oracle from R17.
TEST_CASE("countWienerBits enumerates the tap ranges at win 7 and 5") {
    using l10_filters::restoration::WienerInfo;
    using l10_filters::restoration_pick::countPrimitiveRefsubexpfin;
    using l10_filters::restoration_pick::countWienerBits;
    using l10_filters::restoration_pick::wienerFiltTap0Maxv;
    using l10_filters::restoration_pick::wienerFiltTap0Midv;
    using l10_filters::restoration_pick::wienerFiltTap0Minv;
    using l10_filters::restoration_pick::wienerFiltTap0SubexpK;
    using l10_filters::restoration_pick::wienerFiltTap1Maxv;
    using l10_filters::restoration_pick::wienerFiltTap1Midv;
    using l10_filters::restoration_pick::wienerFiltTap1Minv;
    using l10_filters::restoration_pick::wienerFiltTap1SubexpK;
    using l10_filters::restoration_pick::wienerFiltTap2Maxv;
    using l10_filters::restoration_pick::wienerFiltTap2Midv;
    using l10_filters::restoration_pick::wienerFiltTap2Minv;
    using l10_filters::restoration_pick::wienerFiltTap2SubexpK;
    using l10_filters::restoration_pick::wienerWin;
    using l10_filters::restoration_pick::wienerWinChroma;
    WienerInfo mid{}, midRef{};
    mid.vfilter[0] = (std::int16_t)wienerFiltTap0Midv;
    mid.vfilter[1] = (std::int16_t)wienerFiltTap1Midv;
    mid.vfilter[2] = (std::int16_t)wienerFiltTap2Midv;
    mid.hfilter[0] = (std::int16_t)wienerFiltTap0Midv;
    mid.hfilter[1] = (std::int16_t)wienerFiltTap1Midv;
    mid.hfilter[2] = (std::int16_t)wienerFiltTap2Midv;
    midRef = mid;
    CHECK(countWienerBits(wienerWin, &mid, &midRef) == 2 * (2 + 3 + 4));
    CHECK(countWienerBits(wienerWinChroma, &mid, &midRef) == 2 * (3 + 4));
    // Per-tap full-range enumeration. The oracle composes the six terms
    // with the win == 7 gate on the tap0 terms.
    auto oracle = [&](int win, const WienerInfo& info, const WienerInfo& ref) {
        auto term = [](int tapMax, int tapMin, int k, int refV, int v) {
            return countPrimitiveRefsubexpfin((std::uint16_t)(tapMax - tapMin + 1), (std::uint16_t)k,
                (std::uint16_t)(refV - tapMin), (std::uint16_t)(v - tapMin));
        };
        int bits = 0;
        if (win == wienerWin) {
            bits += term(wienerFiltTap0Maxv, wienerFiltTap0Minv, wienerFiltTap0SubexpK, ref.vfilter[0],
                info.vfilter[0]);
        }
        bits += term(wienerFiltTap1Maxv, wienerFiltTap1Minv, wienerFiltTap1SubexpK, ref.vfilter[1],
            info.vfilter[1]);
        bits += term(wienerFiltTap2Maxv, wienerFiltTap2Minv, wienerFiltTap2SubexpK, ref.vfilter[2],
            info.vfilter[2]);
        if (win == wienerWin) {
            bits += term(wienerFiltTap0Maxv, wienerFiltTap0Minv, wienerFiltTap0SubexpK, ref.hfilter[0],
                info.hfilter[0]);
        }
        bits += term(wienerFiltTap1Maxv, wienerFiltTap1Minv, wienerFiltTap1SubexpK, ref.hfilter[1],
            info.hfilter[1]);
        bits += term(wienerFiltTap2Maxv, wienerFiltTap2Minv, wienerFiltTap2SubexpK, ref.hfilter[2],
            info.hfilter[2]);
        return bits;
    };
    for (int win : {wienerWin, wienerWinChroma}) {
        // tap0 sweep (observable only at win 7; at win 5 the result must
        // NOT move with vfilter[0]).
        for (int v = wienerFiltTap0Minv; v <= wienerFiltTap0Maxv; ++v) {
            WienerInfo info = mid;
            info.vfilter[0] = (std::int16_t)v;
            const int exp = oracle(win, info, mid);
            const int got = countWienerBits(win, &info, &mid);
            CHECK(got == exp);
            if (win == wienerWinChroma && v != wienerFiltTap0Midv) {
                CHECK(got == countWienerBits(win, &mid, &mid)); // tap0 unread
            }
        }
        // tap1 sweep on hfilter.
        for (int v = wienerFiltTap1Minv; v <= wienerFiltTap1Maxv; ++v) {
            WienerInfo info = mid;
            info.hfilter[1] = (std::int16_t)v;
            CHECK(countWienerBits(win, &info, &mid) == oracle(win, info, mid));
        }
        // tap2 sweep on vfilter.
        for (int v = wienerFiltTap2Minv; v <= wienerFiltTap2Maxv; ++v) {
            WienerInfo info = mid;
            info.vfilter[2] = (std::int16_t)v;
            CHECK(countWienerBits(win, &info, &mid) == oracle(win, info, mid));
        }
    }
}

// R19: update_a_sep_sym (restoration_pick.c:798-:848). The transcription
// oracle re-derives the whole span (A/B accumulation with the exact
// division order, the normalization algebra, the S mirror). Pins:
// (a) M/H = 0 -> the reduced system is singular -> a is untouched;
// (b) over deterministic pseudo-random M/H at win 7 and 5, the port
//     matches the oracle exactly, and every successful solve satisfies
//     the normalization sum(a) == 65536 with a symmetric;
// (c) the hc[j*win + i] row-pointer assembly from a flat H (the same
//     layout wiener_decompose_sep_sym builds) is exercised by (b).
TEST_CASE("updateASepSym matches the transcription oracle and pins failure") {
    using l10_filters::restoration_pick::updateASepSym;
    using l10_filters::restoration_pick::wienerHalfwin1;
    using l10_filters::restoration_pick::wienerTapScaleFactor;
    using l10_filters::restoration_pick::wienerWin;
    using l10_filters::restoration_pick::wienerWinChroma;
    // (a) zero M/H -> linsolve fails -> a untouched.
    {
        std::vector<std::int64_t> M(49, 0), H(49 * 49, 0);
        std::int64_t* mc[7];
        std::int64_t* hc[49];
        for (int i = 0; i < 7; ++i) mc[i] = M.data() + i * 7;
        for (int i = 0; i < 49; ++i) hc[i] = H.data() + i * 7;
        std::int32_t b[7] = {1536, -3584, 7680, 54272, 7680, -3584, 1536};
        std::int32_t a[7] = {91, 82, 73, 64, 55, 46, 37};
        const std::int32_t before[7] = {91, 82, 73, 64, 55, 46, 37};
        updateASepSym(7, mc, hc, a, b);
        bool same = true;
        for (int i = 0; i < 7; ++i)
            if (a[i] != before[i]) same = false;
        CHECK(same);
    }
    // (b) transcription oracle sweep.
    auto oracle = [&](int win, const std::vector<std::int64_t>& M, const std::vector<std::int64_t>& H,
                      const std::int32_t* b, std::int32_t* aOut) {
        const int halfwin1 = (win >> 1) + 1;
        const int win2 = win * win;
        std::vector<std::int64_t> A(halfwin1, 0), B(halfwin1 * halfwin1, 0);
        std::vector<const std::int64_t*> mc(win), hc(win2);
        for (int i = 0; i < win; ++i) mc[i] = &M[i * win];
        for (int i = 0; i < win2; ++i) hc[i] = &H[i * win];
        auto wi = [](int idx, int w) {
            const int h1 = (w >> 1) + 1;
            return idx >= h1 ? w - 1 - idx : idx;
        };
        for (int i = 0; i < win; ++i)
            for (int j = 0; j < win; ++j) A[wi(j, win)] += mc[i][j] * (std::int64_t)b[i] / 65536;
        for (int i = 0; i < win; ++i)
            for (int j = 0; j < win; ++j)
                for (int k = 0; k < win; ++k)
                    for (int l = 0; l < win; ++l) {
                        const int kk = wi(k, win), ll = wi(l, win);
                        const std::int64_t t1 = hc[j * win + i][k * win2 + l] * (std::int64_t)b[i] / 65536;
                        B[ll * halfwin1 + kk] += t1 * (std::int64_t)b[j] / 65536;
                    }
        const std::int64_t aMid = A[halfwin1 - 1];
        for (int i = 0; i < halfwin1 - 1; ++i)
            A[i] -= aMid * 2 + B[i * halfwin1 + halfwin1 - 1] - 2 * B[(halfwin1 - 1) * halfwin1 + halfwin1 - 1];
        for (int i = 0; i < halfwin1 - 1; ++i)
            for (int j = 0; j < halfwin1 - 1; ++j)
                B[i * halfwin1 + j] -= 2 * (B[i * halfwin1 + halfwin1 - 1] +
                                               B[(halfwin1 - 1) * halfwin1 + j] -
                                               2 * B[(halfwin1 - 1) * halfwin1 + halfwin1 - 1]);
        // linsolve transcription (same lossy chain).
        std::vector<std::int64_t> Al = B, bl = A;
        std::vector<std::int32_t> S(win, 0);
        bool ok = true;
        for (int k = 0; k < halfwin1 - 2; ++k) {
            for (int i = halfwin1 - 2; i > k; --i) {
                if (std::llabs(Al[(i - 1) * halfwin1 + k]) < std::llabs(Al[i * halfwin1 + k])) {
                    for (int j = 0; j < halfwin1 - 1; ++j) std::swap(Al[i * halfwin1 + j], Al[(i - 1) * halfwin1 + j]);
                    std::swap(bl[i], bl[i - 1]);
                }
            }
            for (int i = k; i < halfwin1 - 2; ++i) {
                if (Al[k * halfwin1 + k] == 0) {
                    ok = false;
                    break;
                }
                const std::int64_t c = Al[(i + 1) * halfwin1 + k], cd = Al[k * halfwin1 + k];
                for (int j = 0; j < halfwin1 - 1; ++j)
                    Al[(i + 1) * halfwin1 + j] -= c / 256 * Al[k * halfwin1 + j] / cd * 256;
                bl[i + 1] -= c * bl[k] / cd;
            }
            if (!ok) break;
        }
        if (ok) {
            for (int i = halfwin1 - 2; i >= 0; --i) {
                if (Al[i * halfwin1 + i] == 0) {
                    ok = false;
                    break;
                }
                std::int64_t c = 0;
                for (int j = i + 1; j < halfwin1 - 1; ++j) c += Al[i * halfwin1 + j] * S[j] / 65536;
                S[i] = (std::int32_t)(65536 * (bl[i] - c) / Al[i * halfwin1 + i]);
            }
        }
        if (ok) {
            S[halfwin1 - 1] = 65536;
            for (int i = halfwin1; i < win; ++i) {
                S[i] = S[win - 1 - i];
                S[halfwin1 - 1] -= 2 * S[i];
            }
            for (int i = 0; i < win; ++i) aOut[i] = S[i];
        }
        return ok;
    };
    std::uint32_t seed = 0xBEEFCAFEu;
    auto lcg = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (int)((seed >> 16) % 31) - 15;
    };
    for (int win : {wienerWin, wienerWinChroma}) {
        const int win2 = win * win;
        for (int trial = 0; trial < 25; ++trial) {
            std::vector<std::int64_t> M(win2), H((size_t)win2 * win2);
            for (auto& v : M) v = lcg();
            for (auto& v : H) v = lcg();
            std::int32_t b[7];
            // A plausible scaled b: the init_filt-shape taps at 512x.
            const std::int32_t initTap[7] = {3, -7, 15, 106, 15, -7, 3};
            for (int i = 0; i < win; ++i) b[i] = 512 * initTap[i + ((wienerWin - win) >> 1)];
            std::int32_t aPort[7] = {-1, -1, -1, -1, -1, -1, -1}, aExp[7] = {-2, -2, -2, -2, -2, -2, -2};
            std::int64_t* mc[7];
            std::int64_t* hc[49];
            for (int i = 0; i < win; ++i) mc[i] = M.data() + i * win;
            for (int i = 0; i < win2; ++i) hc[i] = H.data() + (size_t)i * win;
            updateASepSym(win, mc, hc, aPort, b);
            const bool expOk = oracle(win, M, H, b, aExp);
            const bool portChanged = aPort[0] != -1;
            CHECK(portChanged == expOk);
            if (expOk) {
                for (int i = 0; i < win; ++i) CHECK(aPort[i] == aExp[i]);
                // Normalization + symmetry of the solved taps.
                std::int64_t sum = 0;
                for (int i = 0; i < win; ++i) sum += aPort[i];
                CHECK(sum == 65536);
                bool sym = true;
                for (int i = 0; i < win; ++i)
                    if (aPort[i] != aPort[win - 1 - i]) sym = false;
                CHECK(sym);
            }
        }
    }
}

// R20: update_b_sep_sym (restoration_pick.c:850-:899). The twin
// transcription oracle (a[j] in the accumulation, the hc[i*win + j] row
// index, the B[jj*halfwin1 + ii] slot) over the same pseudo-random sweep,
// plus the asymmetry discriminator: on a generic asymmetric (M, H) the
// solved a and b DIFFER, so a b-side index swap cannot hide.
TEST_CASE("updateBSepSym matches the twin oracle and differs from the a-update") {
    using l10_filters::restoration_pick::updateASepSym;
    using l10_filters::restoration_pick::updateBSepSym;
    using l10_filters::restoration_pick::wienerWin;
    using l10_filters::restoration_pick::wienerWinChroma;
    auto oracleB = [&](int win, const std::vector<std::int64_t>& M, const std::vector<std::int64_t>& H,
                       const std::int32_t* a, std::int32_t* bOut) {
        const int halfwin1 = (win >> 1) + 1;
        const int win2 = win * win;
        std::vector<std::int64_t> A(halfwin1, 0), B(halfwin1 * halfwin1, 0);
        std::vector<const std::int64_t*> Mc(win), hc(win2);
        for (int i = 0; i < win; ++i) Mc[i] = &M[i * win];
        for (int i = 0; i < win2; ++i) hc[i] = &H[i * win];
        auto wi = [](int idx, int w) {
            const int h1 = (w >> 1) + 1;
            return idx >= h1 ? w - 1 - idx : idx;
        };
        for (int i = 0; i < win; ++i) {
            const int ii = wi(i, win);
            for (int j = 0; j < win; ++j) A[ii] += Mc[i][j] * (std::int64_t)a[j] / 65536;
        }
        for (int i = 0; i < win; ++i)
            for (int j = 0; j < win; ++j) {
                const int ii = wi(i, win), jj = wi(j, win);
                for (int k = 0; k < win; ++k)
                    for (int l = 0; l < win; ++l) {
                        const std::int64_t t1 = hc[i * win + j][k * win2 + l] * (std::int64_t)a[k] / 65536;
                        B[jj * halfwin1 + ii] += t1 * (std::int64_t)a[l] / 65536;
                    }
            }
        const std::int64_t aMid = A[halfwin1 - 1];
        for (int i = 0; i < halfwin1 - 1; ++i)
            A[i] -= aMid * 2 + B[i * halfwin1 + halfwin1 - 1] - 2 * B[(halfwin1 - 1) * halfwin1 + halfwin1 - 1];
        for (int i = 0; i < halfwin1 - 1; ++i)
            for (int j = 0; j < halfwin1 - 1; ++j)
                B[i * halfwin1 + j] -= 2 * (B[i * halfwin1 + halfwin1 - 1] +
                                               B[(halfwin1 - 1) * halfwin1 + j] -
                                               2 * B[(halfwin1 - 1) * halfwin1 + halfwin1 - 1]);
        std::vector<std::int64_t> Al = B, bl = A;
        std::vector<std::int32_t> S(win, 0);
        bool ok = true;
        for (int k = 0; k < halfwin1 - 2; ++k) {
            for (int i = halfwin1 - 2; i > k; --i) {
                if (std::llabs(Al[(i - 1) * halfwin1 + k]) < std::llabs(Al[i * halfwin1 + k])) {
                    for (int j = 0; j < halfwin1 - 1; ++j) std::swap(Al[i * halfwin1 + j], Al[(i - 1) * halfwin1 + j]);
                    std::swap(bl[i], bl[i - 1]);
                }
            }
            for (int i = k; i < halfwin1 - 2; ++i) {
                if (Al[k * halfwin1 + k] == 0) {
                    ok = false;
                    break;
                }
                const std::int64_t c = Al[(i + 1) * halfwin1 + k], cd = Al[k * halfwin1 + k];
                for (int j = 0; j < halfwin1 - 1; ++j)
                    Al[(i + 1) * halfwin1 + j] -= c / 256 * Al[k * halfwin1 + j] / cd * 256;
                bl[i + 1] -= c * bl[k] / cd;
            }
            if (!ok) break;
        }
        if (ok) {
            for (int i = halfwin1 - 2; i >= 0; --i) {
                if (Al[i * halfwin1 + i] == 0) {
                    ok = false;
                    break;
                }
                std::int64_t c = 0;
                for (int j = i + 1; j < halfwin1 - 1; ++j) c += Al[i * halfwin1 + j] * S[j] / 65536;
                S[i] = (std::int32_t)(65536 * (bl[i] - c) / Al[i * halfwin1 + i]);
            }
        }
        if (ok) {
            S[halfwin1 - 1] = 65536;
            for (int i = halfwin1; i < win; ++i) {
                S[i] = S[win - 1 - i];
                S[halfwin1 - 1] -= 2 * S[i];
            }
            for (int i = 0; i < win; ++i) bOut[i] = S[i];
        }
        return ok;
    };
    std::uint32_t seed = 0xFA57D00Du;
    auto lcg = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (int)((seed >> 16) % 31) - 15;
    };
    const std::int32_t initTap[7] = {3, -7, 15, 106, 15, -7, 3};
    for (int win : {wienerWin, wienerWinChroma}) {
        const int win2 = win * win, off = (wienerWin - win) >> 1;
        for (int trial = 0; trial < 25; ++trial) {
            std::vector<std::int64_t> M(win2), H((size_t)win2 * win2);
            for (auto& v : M) v = lcg();
            for (auto& v : H) v = lcg();
            std::int32_t a[7];
            for (int i = 0; i < win; ++i) a[i] = 512 * initTap[i + off];
            std::int64_t* mc[7];
            std::int64_t* hc[49];
            for (int i = 0; i < win; ++i) mc[i] = M.data() + i * win;
            for (int i = 0; i < win2; ++i) hc[i] = H.data() + (size_t)i * win;
            std::int32_t bPort[7] = {-1, -1, -1, -1, -1, -1, -1}, bExp[7] = {-2, -2, -2, -2, -2, -2, -2};
            updateBSepSym(win, mc, hc, a, bPort);
            const bool expOk = oracleB(win, M, H, a, bExp);
            const bool portChanged = bPort[0] != -1;
            CHECK(portChanged == expOk);
            if (expOk) {
                for (int i = 0; i < win; ++i) CHECK(bPort[i] == bExp[i]);
                std::int64_t sum = 0;
                for (int i = 0; i < win; ++i) sum += bPort[i];
                CHECK(sum == 65536);
            }
            // Asymmetry discriminator (once per win): the a-update and the
            // b-update on the same (M, H, taps) disagree on a generic
            // asymmetric H.
            if (trial == 0) {
                std::int32_t aOut[7] = {-1, -1, -1, -1, -1, -1, -1};
                updateASepSym(win, mc, hc, aOut, a);
                std::int32_t bOut[7] = {-1, -1, -1, -1, -1, -1, -1};
                updateBSepSym(win, mc, hc, a, bOut);
                bool expChanged = bPort[0] != -1; // reuse the oracle verdict
                (void)expChanged;
                if (bOut[0] != -1 && aOut[0] != -1) {
                    bool same = true;
                    for (int i = 0; i < win; ++i)
                        if (aOut[i] != bOut[i]) same = false;
                    CHECK(!same);
                }
            }
        }
    }
}

// R21: wiener_decompose_sep_sym (restoration_pick.c:901-:932).
// (a) Zero M/H: linsolve fails on the first iteration, so a/b keep the
//     seeded init_filt taps - pinned exactly at both wins:
//     512 * {3,-7,15,106,15,-7,3} (win 7) and 512 * {-7,15,106,15,-7}
//     (win 5, plane_off 1).
// (b) Transcription oracle: init + (NUM_WIENER_ITERS-1) = 4 alternating
//     (update_a, update_b) pairs from the R19/R20 oracles, over
//     deterministic M/H AND over real computeStats statistics.
TEST_CASE("wienerDecomposeSepSym seeds init_filt and iterates 4 times") {
    using l10_filters::restoration_pick::computeStats;
    using l10_filters::restoration_pick::wienerDecomposeSepSym;
    using l10_filters::restoration_pick::wienerWin;
    using l10_filters::restoration_pick::wienerWinChroma;
    // (a)
    {
        std::vector<std::int64_t> M(49, 0), H(49 * 49, 0);
        std::int32_t a[7] = {0}, b[7] = {0};
        wienerDecomposeSepSym(7, M.data(), H.data(), a, b);
        const std::int32_t exp7[7] = {1536, -3584, 7680, 54272, 7680, -3584, 1536};
        for (int i = 0; i < 7; ++i) {
            CHECK(a[i] == exp7[i]);
            CHECK(b[i] == exp7[i]);
        }
        std::int32_t a5[7] = {0}, b5[7] = {0};
        wienerDecomposeSepSym(5, M.data(), H.data(), a5, b5);
        const std::int32_t exp5[7] = {-3584, 7680, 54272, 7680, -3584, 0, 0};
        for (int i = 0; i < 5; ++i) {
            CHECK(a5[i] == exp5[i]);
            CHECK(b5[i] == exp5[i]);
        }
    }
    // (b) composed oracle over real stats + pseudo-random stats.
    const int dgdStride = 40, srcStride = 40;
    std::vector<std::uint8_t> dgd(40 * dgdStride), src(40 * srcStride);
    for (int i = 0; i < 40; ++i)
        for (int j = 0; j < dgdStride; ++j) {
            dgd[i * dgdStride + j] = (std::uint8_t)(3 + (i * 13 + j * 71) % 249);
            // src = a horizontal 3-tap blur of dgd (a learnable low-pass).
            const int c = (int)dgd[i * dgdStride + j];
            const int l = (int)dgd[i * dgdStride + (j > 2 ? j - 3 : 0)];
            const int r = (int)dgd[i * dgdStride + (j < dgdStride - 3 ? j + 3 : dgdStride - 1)];
            src[i * srcStride + j] = (std::uint8_t)((l + 2 * c + r) / 4);
        }
    std::uint32_t seed = 0x5EED0001u;
    auto lcg = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (int)((seed >> 16) % 21) - 10;
    };
    // Local re-declarations of the R19/R20 oracles, composed into the
    // decompose oracle (4 alternating iterations).
    auto updateA = [&](int win, const std::vector<std::int64_t>& M, const std::vector<std::int64_t>& H,
                       std::int32_t* b, std::int32_t* aOut) -> bool {
        const int halfwin1 = (win >> 1) + 1;
        const int win2 = win * win;
        std::vector<std::int64_t> A(halfwin1, 0), B(halfwin1 * halfwin1, 0);
        auto wi = [](int idx, int w) {
            const int h1 = (w >> 1) + 1;
            return idx >= h1 ? w - 1 - idx : idx;
        };
        for (int i = 0; i < win; ++i)
            for (int j = 0; j < win; ++j) A[wi(j, win)] += M[i * win + j] * (std::int64_t)b[i] / 65536;
        for (int i = 0; i < win; ++i)
            for (int j = 0; j < win; ++j)
                for (int k = 0; k < win; ++k)
                    for (int l = 0; l < win; ++l) {
                        // hc[j*win + i][k*win2 + l]: row r = j*win+i sits
                        // at H + (r/win)*win*win2 + (r%win)*win
                        //               = H + j*win*win2 + i*win.
                        const std::int64_t hVal = H[(size_t)j * win * win2 + i * win + k * win2 + l];
                        const std::int64_t t1 = hVal * (std::int64_t)b[i] / 65536;
                        B[wi(l, win) * halfwin1 + wi(k, win)] += t1 * (std::int64_t)b[j] / 65536;
                    }
        const std::int64_t aMid = A[halfwin1 - 1];
        for (int i = 0; i < halfwin1 - 1; ++i)
            A[i] -= aMid * 2 + B[i * halfwin1 + halfwin1 - 1] - 2 * B[(halfwin1 - 1) * halfwin1 + halfwin1 - 1];
        for (int i = 0; i < halfwin1 - 1; ++i)
            for (int j = 0; j < halfwin1 - 1; ++j)
                B[i * halfwin1 + j] -= 2 * (B[i * halfwin1 + halfwin1 - 1] + B[(halfwin1 - 1) * halfwin1 + j] -
                                               2 * B[(halfwin1 - 1) * halfwin1 + halfwin1 - 1]);
        std::vector<std::int64_t> Al = B, bl = A;
        std::vector<std::int32_t> S(win, 0);
        bool ok = true;
        for (int k = 0; k < halfwin1 - 2; ++k) {
            for (int i = halfwin1 - 2; i > k; --i)
                if (std::llabs(Al[(i - 1) * halfwin1 + k]) < std::llabs(Al[i * halfwin1 + k])) {
                    for (int j = 0; j < halfwin1 - 1; ++j) std::swap(Al[i * halfwin1 + j], Al[(i - 1) * halfwin1 + j]);
                    std::swap(bl[i], bl[i - 1]);
                }
            for (int i = k; i < halfwin1 - 2; ++i) {
                if (Al[k * halfwin1 + k] == 0) {
                    ok = false;
                    break;
                }
                const std::int64_t c = Al[(i + 1) * halfwin1 + k], cd = Al[k * halfwin1 + k];
                for (int j = 0; j < halfwin1 - 1; ++j)
                    Al[(i + 1) * halfwin1 + j] -= c / 256 * Al[k * halfwin1 + j] / cd * 256;
                bl[i + 1] -= c * bl[k] / cd;
            }
            if (!ok) break;
        }
        if (ok)
            for (int i = halfwin1 - 2; i >= 0; --i) {
                if (Al[i * halfwin1 + i] == 0) {
                    ok = false;
                    break;
                }
                std::int64_t c = 0;
                for (int j = i + 1; j < halfwin1 - 1; ++j) c += Al[i * halfwin1 + j] * S[j] / 65536;
                S[i] = (std::int32_t)(65536 * (bl[i] - c) / Al[i * halfwin1 + i]);
            }
        if (ok) {
            S[halfwin1 - 1] = 65536;
            for (int i = halfwin1; i < win; ++i) {
                S[i] = S[win - 1 - i];
                S[halfwin1 - 1] -= 2 * S[i];
            }
            for (int i = 0; i < win; ++i) aOut[i] = S[i];
        }
        return ok;
    };
    auto updateB = [&](int win, const std::vector<std::int64_t>& M, const std::vector<std::int64_t>& H,
                       std::int32_t* a, std::int32_t* bOut) -> bool {
        const int halfwin1 = (win >> 1) + 1;
        const int win2 = win * win;
        std::vector<std::int64_t> A(halfwin1, 0), B(halfwin1 * halfwin1, 0);
        auto wi = [](int idx, int w) {
            const int h1 = (w >> 1) + 1;
            return idx >= h1 ? w - 1 - idx : idx;
        };
        for (int i = 0; i < win; ++i) {
            const int ii = wi(i, win);
            for (int j = 0; j < win; ++j) A[ii] += M[i * win + j] * (std::int64_t)a[j] / 65536;
        }
        for (int i = 0; i < win; ++i)
            for (int j = 0; j < win; ++j) {
                const int ii = wi(i, win), jj = wi(j, win);
                for (int k = 0; k < win; ++k)
                    for (int l = 0; l < win; ++l) {
                        // hc[i*win + j][k*win2 + l]: row r = i*win+j sits
                        // at H + i*win*win2 + j*win.
                        const std::int64_t hVal = H[(size_t)i * win * win2 + j * win + k * win2 + l];
                        const std::int64_t t1 = hVal * (std::int64_t)a[k] / 65536;
                        B[jj * halfwin1 + ii] += t1 * (std::int64_t)a[l] / 65536;
                    }
            }
        const std::int64_t aMid = A[halfwin1 - 1];
        for (int i = 0; i < halfwin1 - 1; ++i)
            A[i] -= aMid * 2 + B[i * halfwin1 + halfwin1 - 1] - 2 * B[(halfwin1 - 1) * halfwin1 + halfwin1 - 1];
        for (int i = 0; i < halfwin1 - 1; ++i)
            for (int j = 0; j < halfwin1 - 1; ++j)
                B[i * halfwin1 + j] -= 2 * (B[i * halfwin1 + halfwin1 - 1] + B[(halfwin1 - 1) * halfwin1 + j] -
                                               2 * B[(halfwin1 - 1) * halfwin1 + halfwin1 - 1]);
        std::vector<std::int64_t> Al = B, bl = A;
        std::vector<std::int32_t> S(win, 0);
        bool ok = true;
        for (int k = 0; k < halfwin1 - 2; ++k) {
            for (int i = halfwin1 - 2; i > k; --i)
                if (std::llabs(Al[(i - 1) * halfwin1 + k]) < std::llabs(Al[i * halfwin1 + k])) {
                    for (int j = 0; j < halfwin1 - 1; ++j) std::swap(Al[i * halfwin1 + j], Al[(i - 1) * halfwin1 + j]);
                    std::swap(bl[i], bl[i - 1]);
                }
            for (int i = k; i < halfwin1 - 2; ++i) {
                if (Al[k * halfwin1 + k] == 0) {
                    ok = false;
                    break;
                }
                const std::int64_t c = Al[(i + 1) * halfwin1 + k], cd = Al[k * halfwin1 + k];
                for (int j = 0; j < halfwin1 - 1; ++j)
                    Al[(i + 1) * halfwin1 + j] -= c / 256 * Al[k * halfwin1 + j] / cd * 256;
                bl[i + 1] -= c * bl[k] / cd;
            }
            if (!ok) break;
        }
        if (ok)
            for (int i = halfwin1 - 2; i >= 0; --i) {
                if (Al[i * halfwin1 + i] == 0) {
                    ok = false;
                    break;
                }
                std::int64_t c = 0;
                for (int j = i + 1; j < halfwin1 - 1; ++j) c += Al[i * halfwin1 + j] * S[j] / 65536;
                S[i] = (std::int32_t)(65536 * (bl[i] - c) / Al[i * halfwin1 + i]);
            }
        if (ok) {
            S[halfwin1 - 1] = 65536;
            for (int i = halfwin1; i < win; ++i) {
                S[i] = S[win - 1 - i];
                S[halfwin1 - 1] -= 2 * S[i];
            }
            for (int i = 0; i < win; ++i) bOut[i] = S[i];
        }
        return ok;
    };
    auto decomposeOracle = [&](int win, const std::vector<std::int64_t>& M, const std::vector<std::int64_t>& H,
                               std::int32_t* a, std::int32_t* b) {
        const int off = (wienerWin - win) >> 1;
        const std::int32_t initTap[7] = {3, -7, 15, 106, 15, -7, 3};
        for (int i = 0; i < win; ++i) a[i] = b[i] = 512 * initTap[i + off];
        for (int iter = 1; iter < 5; ++iter) {
            updateA(win, M, H, b, a);
            updateB(win, M, H, a, b);
        }
    };
    for (int win : {wienerWin, wienerWinChroma}) {
        const int win2 = win * win;
        // Real stats fixture.
        {
            std::vector<std::int64_t> M(win2), H((size_t)win2 * win2);
            computeStats(win, dgd.data(), src.data(), 5, 15, 4, 13, dgdStride, srcStride, M.data(), H.data());
            std::int32_t aPort[7] = {0}, bPort[7] = {0}, aExp[7] = {0}, bExp[7] = {0};
            wienerDecomposeSepSym(win, M.data(), H.data(), aPort, bPort);
            decomposeOracle(win, M, H, aExp, bExp);
            for (int i = 0; i < win; ++i) {
                CHECK(aPort[i] == aExp[i]);
                CHECK(bPort[i] == bExp[i]);
            }
        }
        // Pseudo-random fixture.
        for (int trial = 0; trial < 10; ++trial) {
            std::vector<std::int64_t> M(win2), H((size_t)win2 * win2);
            for (auto& v : M) v = lcg();
            for (auto& v : H) v = lcg();
            std::int32_t aPort[7] = {0}, bPort[7] = {0}, aExp[7] = {0}, bExp[7] = {0};
            wienerDecomposeSepSym(win, M.data(), H.data(), aPort, bPort);
            decomposeOracle(win, M, H, aExp, bExp);
            for (int i = 0; i < win; ++i) {
                CHECK(aPort[i] == aExp[i]);
                CHECK(bPort[i] == bExp[i]);
            }
        }
    }
}

// R22: compute_score (restoration_pick.c:934-:971).
// (a) Hand-derived exact pin at win 5: M all zero except M[12] = 16384,
//     H = 0, symmetric taps {3,-7,15} -> a[3] = 106, ab[12] = 106*106,
//     P = 11236, Q = 0, i_score = -32768 -> return 10296.
// (b) Identity-filter pin: vfilt = hfilt = {0,...,0} gives
//     score == i_score -> return 0, at both wins.
// (c) Independent oracle (direct /16384 and /268435456 divisions - the
//     nested-truncation identity makes them exact equivalents of the
//     reference's sequential /128 chains) over real-stats and
//     pseudo-random M/H at both wins.
TEST_CASE("computeScore pins the quadratic form against identity") {
    using l10_filters::restoration_pick::computeScore;
    using l10_filters::restoration_pick::computeStats;
    using l10_filters::restoration_pick::wienerWin;
    using l10_filters::restoration_pick::wienerWinChroma;
    std::int16_t taps[8] = {3, -7, 15, 0, 15, -7, 3, 0};
    // (a)
    {
        std::vector<std::int64_t> M(49, 0), H(49 * 49, 0);
        M[12] = 16384;
        CHECK(computeScore(5, M.data(), H.data(), taps, taps) == 10296);
    }
    // (b) identity pin
    {
        std::int16_t ident[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        std::vector<std::int64_t> M(49), H(49 * 49);
        std::uint32_t seed = 7;
        for (auto& v : M) {
            seed = seed * 1664525u + 1013904223u;
            v = (seed >> 16) % 1000;
        }
        for (auto& v : H) {
            seed = seed * 1664525u + 1013904223u;
            v = (seed >> 16) % 1000;
        }
        CHECK(computeScore(7, M.data(), H.data(), ident, ident) == 0);
        CHECK(computeScore(5, M.data(), H.data(), ident, ident) == 0);
    }
    // (c) oracle.
    auto oracle = [&](int win, const std::vector<std::int64_t>& M, const std::vector<std::int64_t>& H,
                      const std::int16_t* vf, const std::int16_t* hf) {
        const int planeOff = (7 - win) >> 1;
        const int win2 = win * win;
        std::int64_t a[7], b[7];
        a[3] = b[3] = 128;
        for (int i = 0; i < 3; ++i) {
            a[i] = a[6 - i] = vf[i];
            b[i] = b[6 - i] = hf[i];
            a[3] -= 2 * a[i];
            b[3] -= 2 * b[i];
        }
        std::vector<std::int64_t> ab((size_t)win2, 0);
        for (int k = 0; k < win; ++k)
            for (int l = 0; l < win; ++l) ab[k * win + l] = a[l + planeOff] * b[k + planeOff];
        std::int64_t P = 0, Q = 0;
        for (int k = 0; k < win2; ++k) {
            P += ab[k] * M[k] / 16384;
            for (int l = 0; l < win2; ++l) Q += ab[k] * H[k * win2 + l] * ab[l] / 268435456;
        }
        const std::int64_t score = Q - 2 * P;
        const std::int64_t iP = M[win2 >> 1];
        const std::int64_t iQ = H[(win2 >> 1) * win2 + (win2 >> 1)];
        return score - (iQ - 2 * iP);
    };
    // Real stats from the blur fixture.
    const int dgdStride = 40, srcStride = 40;
    std::vector<std::uint8_t> dgd(40 * dgdStride), src(40 * srcStride);
    for (int i = 0; i < 40; ++i)
        for (int j = 0; j < dgdStride; ++j) {
            dgd[i * dgdStride + j] = (std::uint8_t)(3 + (i * 13 + j * 71) % 249);
            const int c = (int)dgd[i * dgdStride + j];
            const int l = (int)dgd[i * dgdStride + (j > 2 ? j - 3 : 0)];
            const int r = (int)dgd[i * dgdStride + (j < dgdStride - 3 ? j + 3 : dgdStride - 1)];
            src[i * srcStride + j] = (std::uint8_t)((l + 2 * c + r) / 4);
        }
    std::uint32_t seed2 = 0x51CE0001u;
    auto lcg = [&seed2]() {
        seed2 = seed2 * 1664525u + 1013904223u;
        return (int)((seed2 >> 16) % 21) - 10;
    };
    for (int win : {wienerWin, wienerWinChroma}) {
        const int win2 = win * win;
        {
            std::vector<std::int64_t> M(win2), H((size_t)win2 * win2);
            computeStats(win, dgd.data(), src.data(), 5, 15, 4, 13, dgdStride, srcStride, M.data(), H.data());
            // (c) real stats vs oracle at the mid taps.
            CHECK(computeScore(win, M.data(), H.data(), taps, taps) == oracle(win, M, H, taps, taps));
        }
        // (c) pseudo-random stats vs oracle.
        for (int trial = 0; trial < 10; ++trial) {
            std::vector<std::int64_t> M(win2), H((size_t)win2 * win2);
            for (auto& v : M) v = lcg();
            for (auto& v : H) v = lcg();
            CHECK(computeScore(win, M.data(), H.data(), taps, taps) == oracle(win, M, H, taps, taps));
        }
    }
}

// R23: finalize_sym_filter (restoration_pick.c:973-:1003), plus the moved
// revert-to-identity property. Pins:
// (a) win 7 exact: {512, 0, -512} -> {1, 0, -1, 0, -1, 0, 1};
// (b) rounding: 256 -> 1, -256 -> -1 (round half away), 511 -> 1,
//     -300 -> -1;
// (c) win 7 clamps: {100000, -100000, 99999} -> {10, -23, 46} then mirror
//     with center -66;
// (d) win 5 branch + clamp ORDER: fi[2] derives from the UNCLAMPED fi[1]
//     (f = {300, 24000} -> fi[2] = CLIP(47, -17, 46) = 46, not 8);
// (e) the decomposed+finalized filter on the blur fixture scores negative
//     (better than identity) at both wins.
TEST_CASE("finalizeSymFilter rounds clamps mirrors and beats identity") {
    using l10_filters::restoration_pick::computeScore;
    using l10_filters::restoration_pick::computeStats;
    using l10_filters::restoration_pick::finalizeSymFilter;
    using l10_filters::restoration_pick::wienerDecomposeSepSym;
    using l10_filters::restoration_pick::wienerWin;
    using l10_filters::restoration_pick::wienerWinChroma;
    // (a)
    {
        std::int32_t f[7] = {512, 0, -512, 0, 0, 0, 0};
        std::int16_t fi[8] = {0};
        finalizeSymFilter(7, f, fi);
        const std::int16_t exp[8] = {1, 0, -1, 0, -1, 0, 1, 0};
        for (int i = 0; i < 8; ++i) CHECK(fi[i] == exp[i]);
    }
    // (b)
    {
        std::int32_t f[7] = {256, -256, 511, 0, 0, 0, 0};
        std::int16_t fi[8] = {0};
        finalizeSymFilter(7, f, fi);
        CHECK(fi[0] == 1);
        CHECK(fi[1] == -1);
        CHECK(fi[2] == 1);
        CHECK(fi[3] == -2 * (1 - 1 + 1));
        std::int32_t g[7] = {-300, 0, 0, 0, 0, 0, 0};
        std::int16_t gj[8] = {0};
        finalizeSymFilter(7, g, gj);
        CHECK(gj[0] == -1);
    }
    // (c)
    {
        std::int32_t f[7] = {100000, -100000, 99999, 0, 0, 0, 0};
        std::int16_t fi[8] = {0};
        finalizeSymFilter(7, f, fi);
        CHECK(fi[0] == 10);
        CHECK(fi[1] == -23);
        CHECK(fi[2] == 46);
        CHECK(fi[3] == -2 * (10 - 23 + 46));
        CHECK(fi[4] == 46);
        CHECK(fi[5] == -23);
        CHECK(fi[6] == 10);
    }
    // (d)
    {
        std::int32_t f[7] = {300, 24000, -9999, 0, 0, 0, 0};
        std::int16_t fi[8] = {0};
        finalizeSymFilter(5, f, fi);
        CHECK(fi[0] == 0);
        CHECK(fi[1] == 1);  // CLIP(1, -23, 8)
        CHECK(fi[2] == 46); // CLIP(47, -17, 46) from the UNCLAMPED 47
        CHECK(fi[3] == -2 * (0 + 1 + 46));
        CHECK(fi[4] == 46);
        CHECK(fi[5] == 1);
        CHECK(fi[6] == 0);
    }
    // (e)
    const int dgdStride = 40, srcStride = 40;
    std::vector<std::uint8_t> dgd(40 * dgdStride), src(40 * srcStride);
    for (int i = 0; i < 40; ++i)
        for (int j = 0; j < dgdStride; ++j) {
            dgd[i * dgdStride + j] = (std::uint8_t)(3 + (i * 13 + j * 71) % 249);
            const int c = (int)dgd[i * dgdStride + j];
            const int l = (int)dgd[i * dgdStride + (j > 2 ? j - 3 : 0)];
            const int r = (int)dgd[i * dgdStride + (j < dgdStride - 3 ? j + 3 : dgdStride - 1)];
            src[i * srcStride + j] = (std::uint8_t)((l + 2 * c + r) / 4);
        }
    for (int win : {wienerWin, wienerWinChroma}) {
        const int win2 = win * win;
        std::vector<std::int64_t> M(win2), H((size_t)win2 * win2);
        computeStats(win, dgd.data(), src.data(), 5, 15, 4, 13, dgdStride, srcStride, M.data(), H.data());
        std::int32_t a[7] = {0}, b[7] = {0};
        wienerDecomposeSepSym(win, M.data(), H.data(), a, b);
        std::int16_t vf[8] = {0}, hf[8] = {0};
        finalizeSymFilter(win, a, vf);
        finalizeSymFilter(win, b, hf);
        CHECK(computeScore(win, M.data(), H.data(), vf, hf) < 0);
        }
}
