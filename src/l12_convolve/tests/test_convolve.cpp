#include <doctest.h>
#include <convolve.h>

#include <algorithm>

// S1 RED: get_filter_base / get_filter_offset (convolve.c:54-:61) recover
// the subpel phase from the filter pointer's position inside a 256-byte
// aligned table: offset = (f - base) in InterpKernel rows = 16-byte units.
TEST_CASE("getFilterBase rounds a row-5 filter pointer down to the table base") {
    alignas(256) static const convolve::InterpKernel table[convolve::subpelShifts] = {};
    const std::int16_t* f = &table[5][0];
    const convolve::InterpKernel* base = convolve::getFilterBase(f);
    CHECK(base == table);
    const std::int32_t off = convolve::getFilterOffset(f, base);
    CHECK(off == 5);
}

// Range rule: a filter pointer may land on ANY row of a 256-aligned table;
// the offset must equal the row index over the whole enumerated set 0..15.
TEST_CASE("getFilterOffset equals the row index for every row of a 256-aligned table") {
    alignas(256) static const convolve::InterpKernel table[convolve::subpelShifts] = {};
    for (std::int32_t r = 0; r < convolve::subpelShifts; ++r) {
        const std::int16_t* f = &table[r][0];
        CHECK(convolve::getFilterOffset(f, convolve::getFilterBase(f)) == r);
    }
}

// Fixture (a), the arithmetic pinned on a NON-256-aligned pointer: a
// 16-aligned filter pointer landing at 0x90 inside a 256-byte window (the
// real WienerInfo shape, restoration.h:168-:169 DECLARE_ALIGNED(16)) must
// give base = the window bottom and offset = 0x90/16 = 9.
TEST_CASE("getFilterBase/Offset on a mid-window 16-aligned pointer") {
    alignas(256) static const unsigned char raw[512] = {};
    const std::int16_t* f = (const std::int16_t*)(raw + 0x90);
    CHECK(convolve::getFilterBase(f) == (const convolve::InterpKernel*)raw);
    CHECK(convolve::getFilterOffset(f, convolve::getFilterBase(f)) == 9);
}

// S2 RED: svt_aom_horz_scalar_product (convolve.c:30-:36) - sum of
// a[k] * b[k] over the 8 taps, in order.
TEST_CASE("horzScalarProduct accumulates a[k]*b[k] over 8 taps") {
    const std::uint8_t a[convolve::subpelTaps] = {1, 2, 3, 4, 5, 6, 7, 8};
    const std::int16_t b[convolve::subpelTaps] = {2, -2, 4, -4, 8, -8, 16, -16};
    CHECK(convolve::horzScalarProduct(a, b) == 2 - 4 + 12 - 16 + 40 - 48 + 112 - 128);
}

// S3 RED: svt_aom_convolve_add_src_horiz_hip (convolve.c:63-:82). One
// output pixel, phase 0, filter row {0,2,-10,122,18,-4,0,0}: the port
// reads src[-3..+4] around the current pixel and adds the rounding term
// src_c << 7 + (1 << 14) before the >> round0_bits.
TEST_CASE("convolveAddSrcHorizHip single pixel phase 0") {
    const std::uint8_t padded[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    const std::uint8_t* src = padded + 3; // current pixel at padded[3] = 40
    alignas(256) static const convolve::InterpKernel filt = {0, 2, -10, 122, 18, -4, 0, 0};
    std::uint16_t dst[1] = {0xABCD};
    convolve::convolveAddSrcHorizHip(src, 8, dst, 1, &filt, 0, 16, 1, 1, 3);
    // Oracle (convolve.c:71-:76): sum = sum_k src[k-3]*f[k] + src[0]<<7
    // + (1<<14); dst = clamp((sum + (1<<2)) >> 3, 0, (1<<13) - 1).
    const std::int32_t sum = 20 * 2 + 30 * -10 + 40 * 122 + 50 * 18 + 60 * -4 + (40 << 7) + (1 << 14);
    const std::int32_t want = (sum + (1 << 2)) >> 3;
    CHECK(dst[0] == want);
}

// S3a RED: the transcribed sub_pel_filters_8 table
// (inter_prediction.c:241-:256) diffed entry-by-entry against a second
// independent transcription in this test. TRANSCRIPTION CAVEAT (AN14
// OD_DIVU precedent): the port's table is a mechanical transcription, not
// a formula; this diff is the only guard against a typo.
TEST_CASE("subPelFilters8 matches the independent transcription, all 128 entries") {
    static const std::int16_t ref[convolve::subpelShifts][convolve::subpelTaps] = {
        {0, 0, 0, 128, 0, 0, 0, 0},    {0, 2, -6, 126, 8, -2, 0, 0},   {0, 2, -10, 122, 18, -4, 0, 0},
        {0, 2, -12, 116, 28, -8, 2, 0}, {0, 2, -14, 110, 38, -10, 2, 0}, {0, 2, -14, 102, 48, -12, 2, 0},
        {0, 2, -16, 94, 58, -12, 2, 0}, {0, 2, -14, 84, 66, -12, 2, 0},  {0, 2, -14, 76, 76, -14, 2, 0},
        {0, 2, -12, 66, 84, -14, 2, 0}, {0, 2, -12, 58, 94, -16, 2, 0},  {0, 2, -12, 48, 102, -14, 2, 0},
        {0, 2, -10, 38, 110, -14, 2, 0}, {0, 2, -8, 28, 116, -12, 2, 0}, {0, 0, -4, 18, 122, -10, 2, 0},
        {0, 0, -2, 8, 126, -6, 2, 0},
    };
    for (std::int32_t r = 0; r < convolve::subpelShifts; ++r)
        for (std::int32_t k = 0; k < convolve::subpelTaps; ++k)
            CHECK(convolve::subPelFilters8[r][k] == ref[r][k]);
}

namespace {
// Independent oracle for convolve_add_src_horiz_hip, derived from the
// reference accumulation (convolve.c:63-:82): taps at src[(xq4>>4)-3+k],
// rounding = src[(xq4>>4)] << 7 + (1 << 14), then
// ROUND_POWER_OF_TWO(sum, round0_bits) clamped to
// [0, WIENER_CLAMP_LIMIT(round0_bits, 8) - 1].
void addSrcHorizOracle(const std::uint8_t* src, std::ptrdiff_t srcStride, const std::int16_t* filt,
                       std::int32_t x0Q4, std::int32_t xStepQ4, std::int32_t w, std::int32_t h,
                       std::int32_t round0Bits, std::uint16_t* dst, std::ptrdiff_t dstStride) {
    const std::int32_t limit = (1 << (8 + 1 + 7 - round0Bits)) - 1;
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            const std::int32_t xq4 = x0Q4 + xStepQ4 * x;
            const std::int32_t col = (xq4 >> 4) - 3;
            std::int32_t sum = 0;
            for (std::int32_t k = 0; k < convolve::subpelTaps; ++k)
                sum += src[y * srcStride + col + k] * filt[k];
            sum += (std::int32_t)src[y * srcStride + col + 3] << 7;
            sum += 1 << (8 + 7 - 1);
            std::int32_t v = (sum + ((1 << round0Bits) >> 1)) >> round0Bits;
            if (v < 0)
                v = 0;
            if (v > limit)
                v = limit;
            dst[y * dstStride + x] = (std::uint16_t)v;
        }
    }
}
} // namespace

// Fixture (b) + range rule: the horiz add-src kernel over the WHOLE
// enumerated phase set x0_q4 = 0..15 (one row of the 256-aligned table per
// phase), full-dst assert against the independent oracle, w=4 h=2.
TEST_CASE("convolveAddSrcHorizHip full-dst oracle over all 16 phases") {
    std::uint8_t buf[2 * 16];
    for (std::int32_t i = 0; i < 2 * 16; ++i)
        buf[i] = (std::uint8_t)((i * 37 + 11) % 251);
    const std::uint8_t* src = buf + 3;
    std::uint16_t got[2 * 4];
    std::uint16_t want[2 * 4];
    for (std::int32_t r = 0; r < convolve::subpelShifts; ++r) {
        convolve::convolveAddSrcHorizHip(src, 16, got, 4, convolve::subPelFilters8, r, 16, 4, 2, 3);
        addSrcHorizOracle(src, 16, convolve::subPelFilters8[r], r, 16, 4, 2, 3, want, 4);
        CHECK(std::equal(got, got + 8, want));
    }
}

// S4 RED: svt_aom_convolve_add_src_vert_hip (convolve.c:84-:104). One
// output pixel, phase 0, the default wiener row {3,-7,15,-22,15,-7,3,0}
// (restoration.h:249-:256 with the :129-:131 tap mids; taps sum to 0).
// The vert rounding term SUBTRACTS (1 << (bd + round1_bits - 1)).
TEST_CASE("convolveAddSrcVertHip single pixel phase 0") {
    const std::uint16_t rows[8] = {2560, 2560, 2560, 3072, 2560, 2560, 2560, 2560};
    const std::uint16_t* src = rows + 3; // center row = rows[3] = 3072
    alignas(256) static const convolve::InterpKernel filt = {3, -7, 15, -22, 15, -7, 3, 0};
    std::uint8_t dst[1] = {0xAB};
    convolve::convolveAddSrcVertHip(src, 1, dst, 1, &filt, 0, 16, 1, 1, 11);
    // Oracle (convolve.c:93-:98): sum = sum_k rows[k]*f[k] + 3072<<7
    // - (1 << 18); dst = clip_pixel((sum + (1<<10)) >> 11).
    const std::int32_t sum = 2560 * (3 - 7 + 15 + 15 - 7 + 3) + 3072 * (-22) + (3072 << 7) - (1 << 18);
    const std::int32_t want = (sum + (1 << 10)) >> 11;
    CHECK(dst[0] == want);
}

namespace {
// Independent oracle for convolve_add_src_vert_hip (convolve.c:84-:104):
// taps at src[(yq4>>4)-3+k rows], rounding = src[(yq4>>4)] << 7
// - (1 << (8 + round1_bits - 1)), then ROUND_POWER_OF_TWO + clip_pixel.
void addSrcVertOracle(const std::uint16_t* src, std::ptrdiff_t srcStride, const std::int16_t* filt,
                      std::int32_t y0Q4, std::int32_t yStepQ4, std::int32_t w, std::int32_t h,
                      std::int32_t round1Bits, std::uint8_t* dst, std::ptrdiff_t dstStride) {
    for (std::int32_t x = 0; x < w; ++x) {
        for (std::int32_t y = 0; y < h; ++y) {
            const std::int32_t yq4 = y0Q4 + yStepQ4 * y;
            const std::int32_t row = (yq4 >> 4) - 3;
            std::int32_t sum = 0;
            for (std::int32_t k = 0; k < convolve::subpelTaps; ++k)
                sum += src[x + (row + k) * srcStride] * filt[k];
            sum += (std::int32_t)src[x + (row + 3) * srcStride] << 7;
            sum -= 1 << (8 + round1Bits - 1);
            std::int32_t v = (sum + ((1 << round1Bits) >> 1)) >> round1Bits;
            if (v < 0)
                v = 0;
            if (v > 255)
                v = 255;
            dst[y * dstStride + x] = (std::uint8_t)v;
        }
    }
}
} // namespace

// Fixture (c) + range rule: the vert add-src kernel on a TRANSPOSED
// fixture (16 rows x 4 cols uint16) over the whole enumerated phase set
// y0_q4 = 0..15, full-dst assert against the independent oracle.
TEST_CASE("convolveAddSrcVertHip transposed full-dst oracle over all 16 phases") {
    std::uint16_t bufT[16 * 4];
    for (std::int32_t i = 0; i < 16 * 4; ++i)
        bufT[i] = (std::uint16_t)((i * 97 + 531) % 4096 + 1024);
    const std::uint16_t* src = bufT + 3 * 4;
    std::uint8_t got[2 * 4];
    std::uint8_t want[2 * 4];
    for (std::int32_t r = 0; r < convolve::subpelShifts; ++r) {
        convolve::convolveAddSrcVertHip(src, 4, got, 4, convolve::subPelFilters8, r, 16, 4, 2, 11);
        addSrcVertOracle(src, 4, convolve::subPelFilters8[r], r, 16, 4, 2, 11, want, 4);
        CHECK(std::equal(got, got + 8, want));
    }
}

// S8 RED: get_conv_params_wiener (convolve.h:72-:96) over the enumerated
// bit-depth set {8, 10, 12}: round_0 = WIENER_ROUND0_BITS 3 and
// round_1 = 2*FILTER_BITS - round_0 = 11 until the intermediate range
// bd + FILTER_BITS - round_0 + 2 exceeds 16, then both adjust by the
// overflow.
TEST_CASE("getConvParamsWiener round pair over bd in {8,10,12}") {
    const convolve::ConvolveParams p8 = convolve::getConvParamsWiener(8);
    CHECK(p8.round0 == 3);
    CHECK(p8.round1 == 11);
    const convolve::ConvolveParams p10 = convolve::getConvParamsWiener(10);
    CHECK(p10.round0 == 3);
    CHECK(p10.round1 == 11);
    const convolve::ConvolveParams p12 = convolve::getConvParamsWiener(12);
    CHECK(p12.round0 == 5);
    CHECK(p12.round1 == 9);
}

// S5 RED: svt_av1_wiener_convolve_add_src_c (convolve.c:106-:153)
// end-to-end. Fixture (d): the sum-0 default wiener row
// {3,-7,15,-22,15,-7,3,0} (restoration.h:249-:256, :129-:131) on both axes
// over a FLAT image reproduces the input: the horiz pass adds the center
// pixel and the vert bias cancels the horiz bias; DC gain is exactly 1.
TEST_CASE("wienerConvolveAddSrc flat image is DC-exact with the default wiener row") {
    alignas(256) static const convolve::InterpKernel wfilt = {3, -7, 15, -22, 15, -7, 3, 0};
    constexpr std::int32_t w = 8;
    constexpr std::int32_t h = 4;
    std::uint8_t buf[(h + 6) * (w + 6)];
    for (std::int32_t i = 0; i < (h + 6) * (w + 6); ++i)
        buf[i] = 200;
    std::uint8_t dst[h * w] = {};
    const convolve::ConvolveParams cp = convolve::getConvParamsWiener(8);
    convolve::wienerConvolveAddSrc(buf + 3 * (w + 6) + 3, w + 6, dst, w, &wfilt[0], &wfilt[0], w, h, &cp);
    for (std::int32_t i = 0; i < h * w; ++i)
        CHECK(dst[i] == 200);
}

// Fixture (d), non-flat: the full 2-stage independent oracle. Stage 1 is
// the horiz oracle into a maxSbSize-strided uint16 staging buffer
// (intermediate_height = ((h-1)*16 + y0q4) >> 4 + 7 rows), stage 2 reads
// rows y..y+7 of it (row intermediate_height is the zeroed sentinel) and
// clips to 0..255. This pins the staging contract end-to-end.
TEST_CASE("wienerConvolveAddSrc non-flat matches the 2-stage oracle") {
    alignas(256) static const convolve::InterpKernel wfilt = {3, -7, 15, -22, 15, -7, 3, 0};
    constexpr std::int32_t w = 8;
    constexpr std::int32_t h = 4;
    constexpr std::int32_t stride = w + 6;
    std::uint8_t buf[stride * (h + 6)];
    for (std::int32_t i = 0; i < stride * (h + 6); ++i)
        buf[i] = (std::uint8_t)((i * 53 + 7) % 233);
    std::uint8_t got[h * w] = {};
    std::uint8_t want[h * w] = {};
    const std::uint8_t* src = buf + 3 * stride + 3;

    convolve::ConvolveParams cp = convolve::getConvParamsWiener(8);
    convolve::wienerConvolveAddSrc(src, stride, got, w, &wfilt[0], &wfilt[0], w, h, &cp);

    // Stage 1: horiz add-src into temp (stride 128), rows 0..h+5.
    const std::int32_t intermediateHeight = h + 6;
    std::uint16_t temp[convolve::maxSbSize * (intermediateHeight + 1)] = {};
    addSrcHorizOracle(src - stride * 3, stride, &wfilt[0], 0, 16, w, intermediateHeight, cp.round0, temp,
                      convolve::maxSbSize);
    // Stage 2: vert add-src over rows y..y+7 (row h+6 stays the zeroed
    // sentinel), rounding = temp[y+3] << 7 - (1 << 18).
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            std::int32_t sum = 0;
            for (std::int32_t k = 0; k < convolve::subpelTaps; ++k)
                sum += temp[(y + k) * convolve::maxSbSize + x] * wfilt[k];
            sum += (std::int32_t)temp[(y + 3) * convolve::maxSbSize + x] << 7;
            sum -= 1 << (8 + cp.round1 - 1);
            std::int32_t v = (sum + ((1 << cp.round1) >> 1)) >> cp.round1;
            if (v < 0)
                v = 0;
            if (v > 255)
                v = 255;
            want[y * w + x] = (std::uint8_t)v;
        }
    }
    CHECK(std::equal(got, got + h * w, want));
}

// S6 RED: svt_aom_convolve8_horiz_c (convolve.c:288-:298) at phase 0: the
// {0,0,0,128,0,0,0,0} row is the no-op center tap and the plain kernel's
// ROUND_POWER_OF_TWO(sum, FILTER_BITS) = (sum + 64) >> 7 reproduces the
// input exactly ((128*s + 64) >> 7 = s).
TEST_CASE("convolve8Horiz phase 0 reproduces the input") {
    constexpr std::int32_t w = 8;
    constexpr std::int32_t h = 2;
    constexpr std::int32_t stride = w + 6;
    std::uint8_t buf[stride * h];
    for (std::int32_t i = 0; i < stride * h; ++i)
        buf[i] = (std::uint8_t)((i * 41 + 3) % 241);
    std::uint8_t dst[h * w] = {};
    convolve::convolve8Horiz(buf + 3, stride, dst, w, &convolve::subPelFilters8[0][0], 16, nullptr, 0, w, h);
    for (std::int32_t y = 0; y < h; ++y)
        for (std::int32_t x = 0; x < w; ++x)
            CHECK(dst[y * w + x] == buf[y * stride + 3 + x]);
}

namespace {
// Independent oracle for the plain convolve kernels (convolve.c:253-:268
// horiz, :270-:286 vert): sum of src taps * filter row, then
// clip_pixel(ROUND_POWER_OF_TWO(sum, FILTER_BITS)); no add-src term.
void plainHorizOracle(const std::uint8_t* src, std::ptrdiff_t srcStride, const std::int16_t* filt,
                      std::int32_t x0Q4, std::int32_t xStepQ4, std::int32_t w, std::int32_t h, std::uint8_t* dst,
                      std::ptrdiff_t dstStride) {
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            const std::int32_t xq4 = x0Q4 + xStepQ4 * x;
            const std::int32_t col = (xq4 >> 4) - 3;
            std::int32_t sum = 0;
            for (std::int32_t k = 0; k < convolve::subpelTaps; ++k)
                sum += src[y * srcStride + col + k] * filt[k];
            std::int32_t v = (sum + ((1 << 7) >> 1)) >> 7;
            if (v < 0)
                v = 0;
            if (v > 255)
                v = 255;
            dst[y * dstStride + x] = (std::uint8_t)v;
        }
    }
}
} // namespace

// Range rule for the plain horiz kernel: every phase 0..15, full-dst
// against the independent oracle.
TEST_CASE("convolve8Horiz oracle sweep over all 16 phases") {
    constexpr std::int32_t w = 8;
    constexpr std::int32_t h = 2;
    constexpr std::int32_t stride = w + 6;
    std::uint8_t buf[stride * h];
    for (std::int32_t i = 0; i < stride * h; ++i)
        buf[i] = (std::uint8_t)((i * 71 + 29) % 241);
    std::uint8_t got[h * w];
    std::uint8_t want[h * w];
    for (std::int32_t r = 0; r < convolve::subpelShifts; ++r) {
        convolve::convolve8Horiz(buf + 3, stride, got, w, &convolve::subPelFilters8[r][0], 16, nullptr, 0, w, h);
        plainHorizOracle(buf + 3, stride, convolve::subPelFilters8[r], r, 16, w, h, want, w);
        CHECK(std::equal(got, got + h * w, want));
    }
}

// S7 RED: svt_aom_convolve8_vert_c (convolve.c:300-:310) at phase 0: same
// no-op center tap on the vertical axis; the vert kernel walks
// column-major and writes dst[y * dst_stride].
TEST_CASE("convolve8Vert phase 0 reproduces the input") {
    constexpr std::int32_t w = 4;
    constexpr std::int32_t h = 2;
    constexpr std::int32_t stride = w;
    std::uint8_t buf[stride * (h + 6)];
    for (std::int32_t i = 0; i < stride * (h + 6); ++i)
        buf[i] = (std::uint8_t)((i * 29 + 17) % 241);
    std::uint8_t dst[h * w] = {};
    convolve::convolve8Vert(buf + 3 * stride, stride, dst, w, nullptr, 0, &convolve::subPelFilters8[0][0], 16, w, h);
    for (std::int32_t y = 0; y < h; ++y)
        for (std::int32_t x = 0; x < w; ++x)
            CHECK(dst[y * w + x] == buf[(y + 3) * stride + x]);
}

// Range rule for the plain vert kernel: every phase 0..15, full-dst
// against the transposed plain oracle.
TEST_CASE("convolve8Vert oracle sweep over all 16 phases") {
    constexpr std::int32_t w = 4;
    constexpr std::int32_t h = 2;
    constexpr std::int32_t stride = w;
    std::uint8_t buf[stride * (h + 6)];
    for (std::int32_t i = 0; i < stride * (h + 6); ++i)
        buf[i] = (std::uint8_t)((i * 113 + 61) % 241);
    std::uint8_t got[h * w];
    std::uint8_t want[h * w];
    for (std::int32_t r = 0; r < convolve::subpelShifts; ++r) {
        convolve::convolve8Vert(buf + 3 * stride, stride, got, w, nullptr, 0, &convolve::subPelFilters8[r][0], 16,
                                w, h);
        for (std::int32_t y = 0; y < h; ++y) {
            for (std::int32_t x = 0; x < w; ++x) {
                std::int32_t sum = 0;
                for (std::int32_t k = 0; k < convolve::subpelTaps; ++k)
                    sum += buf[(y + k) * stride + x] * convolve::subPelFilters8[r][k];
                std::int32_t v = (sum + 64) >> 7;
                if (v < 0)
                    v = 0;
                if (v > 255)
                    v = 255;
                want[y * w + x] = (std::uint8_t)v;
            }
        }
        CHECK(std::equal(got, got + h * w, want));
    }
}
