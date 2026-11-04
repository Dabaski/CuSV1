#include <doctest.h>
#include <restoration.h>

// LR1 (A8 campaign): 1:1 port of the restoration math core of
// third_party/SVT-AV1/Source/Lib/Codec/restoration.c: sgr_params :31-:49,
// boxsum1 :430-:495, boxsum2 :497-:584, boxsum :586-:594, svt_decode_xq
// :597-:608, x_by_xplus1 :610-:625, one_by_x :627-:630, extend_frame_lowbd
// :110-:126 + svt_extend_frame :150-:157, copy_tile_lowbd :159-:164 +
// copy_tile :173-:180. PARKED (named): the stripe machinery :216/:249-:397,
// wiener_filter_stripe :399, the selfguided family :632-:962 (LR2),
// sgrproj/wiener stripe + highbd :987-:1035, the filter_unit/frame walkers
// :1040+, the tile walkers :1124+. Coverage-only slice: moves no conformance
// count; enable_restoration stays 0.

TEST_CASE("sgrParams row 0 is {2,1},{140,3236}") {
    CHECK(l10_filters::restoration::sgrParams[0].r[0] == 2);
}

namespace {
// Independent oracle for the boxsum family: the reference's vertical-then-
// horizontal prefix walk composes to a 2D box sum CLAMPED at the given
// array bounds (edge rows/cols get truncated sums; it never reads outside
// width/height). Derived by tracing restoration.c:436-:494.
void boxOracle(const std::int32_t* src, int w, int h, int stride, int r, int sqr, std::int32_t* out, int outStride) {
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            std::int32_t sum = 0;
            for (int y = (i - r > 0 ? i - r : 0); y <= (i + r < h - 1 ? i + r : h - 1); ++y)
                for (int x = (j - r > 0 ? j - r : 0); x <= (j + r < w - 1 ? j + r : w - 1); ++x) {
                    const std::int32_t v = src[y * stride + x];
                    sum += sqr ? v * v : v;
                }
            out[i * outStride + j] = sum;
        }
    }
}
} // namespace

// S2: boxsum1 radius-1, plain (sqr=0). All-ones 7x7: the clamped box count
// is 4 at corners, 6 at edge centers, 9 in the interior.
TEST_CASE("boxsum1 plain all-ones 7x7: clamped box geometry") {
    std::int32_t src[8 * 7];
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 8; ++j) src[i * 8 + j] = (j < 7) ? 1 : 9999;
    std::int32_t dst[8 * 7] = {0};
    l10_filters::restoration::boxsum1(src, 7, 7, 8, 0, dst, 8);
    CHECK(dst[0] == 4);       // corner
    CHECK(dst[3] == 6);       // top edge center
    CHECK(dst[3 * 8 + 0] == 6); // left edge center
    CHECK(dst[3 * 8 + 3] == 9); // interior
    CHECK(dst[6 * 8 + 6] == 4); // far corner
}

// S2 oracle fixture: distinct values, 7x9 grid, stride 12 (padding 9999
// proves no out-of-bounds reads), checked cell-by-cell against the oracle.
TEST_CASE("boxsum1 plain 7x9 matches the clamped-box oracle") {
    std::int32_t src[12 * 7];
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 12; ++j) src[i * 12 + j] = (j < 9) ? 1 + i * 9 + j : 9999;
    std::int32_t dst[12 * 7] = {0};
    std::int32_t exp[12 * 7] = {0};
    boxOracle(src, 9, 7, 12, 1, 0, exp, 12);
    l10_filters::restoration::boxsum1(src, 9, 7, 12, 0, dst, 12);
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 9; ++j) CHECK(dst[i * 12 + j] == exp[i * 12 + j]);
}

// S3: boxsum1 with sqr=1 on a fixture where the squared sum differs from
// the plain sum (values > 1).
TEST_CASE("boxsum1 sqr=1 7x9 matches the squared-box oracle") {
    std::int32_t src[12 * 7];
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 12; ++j) src[i * 12 + j] = (j < 9) ? 1 + i * 9 + j : 9999;
    std::int32_t dst[12 * 7] = {0};
    std::int32_t exp[12 * 7] = {0};
    boxOracle(src, 9, 7, 12, 1, 1, exp, 12);
    l10_filters::restoration::boxsum1(src, 9, 7, 12, 1, dst, 12);
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 9; ++j) CHECK(dst[i * 12 + j] == exp[i * 12 + j]);
    // The squared sum differs from the plain sum on this fixture.
    CHECK(dst[3 * 12 + 4] != 25);
}

// S4: boxsum2 radius-2. All-ones 7x7: the clamped 5x5 box count is 9 at
// corners, 15 at edge centers, 25 in the interior.
TEST_CASE("boxsum2 plain all-ones 7x7: clamped box geometry") {
    std::int32_t src[8 * 7];
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 8; ++j) src[i * 8 + j] = (j < 7) ? 1 : 9999;
    std::int32_t dst[8 * 7] = {0};
    l10_filters::restoration::boxsum2(src, 7, 7, 8, 0, dst, 8);
    CHECK(dst[0] == 9);         // corner: 3x3
    CHECK(dst[0 * 8 + 3] == 15); // top edge: 3x5
    CHECK(dst[3 * 8 + 0] == 15); // left edge: 5x3
    CHECK(dst[3 * 8 + 3] == 25); // interior: 5x5
    CHECK(dst[6 * 8 + 6] == 9);  // far corner: 3x3
}

// S4 oracle fixture: distinct values, 7x9 grid, checked against the oracle.
TEST_CASE("boxsum2 plain 7x9 matches the clamped-box oracle") {
    std::int32_t src[12 * 7];
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 12; ++j) src[i * 12 + j] = (j < 9) ? 1 + i * 9 + j : 9999;
    std::int32_t dst[12 * 7] = {0};
    std::int32_t exp[12 * 7] = {0};
    boxOracle(src, 9, 7, 12, 2, 0, exp, 12);
    l10_filters::restoration::boxsum2(src, 9, 7, 12, 0, dst, 12);
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 9; ++j) CHECK(dst[i * 12 + j] == exp[i * 12 + j]);
}

// S5: boxsum dispatch wrapper. On this fixture r=1 and r=2 differ; the
// dispatch must select boxsum1 for r=1 and boxsum2 for r=2.
TEST_CASE("boxsum dispatch selects radius 1 vs 2 correctly") {
    std::int32_t src[12 * 7];
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 12; ++j) src[i * 12 + j] = (j < 9) ? 1 + i * 9 + j : 9999;
    std::int32_t dst1[12 * 7] = {0};
    std::int32_t dst2[12 * 7] = {0};
    std::int32_t exp1[12 * 7] = {0};
    std::int32_t exp2[12 * 7] = {0};
    boxOracle(src, 9, 7, 12, 1, 0, exp1, 12);
    boxOracle(src, 9, 7, 12, 2, 0, exp2, 12);
    l10_filters::restoration::boxsum(src, 9, 7, 12, 1, 0, dst1, 12);
    l10_filters::restoration::boxsum(src, 9, 7, 12, 2, 0, dst2, 12);
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 9; ++j) {
            CHECK(dst1[i * 12 + j] == exp1[i * 12 + j]);
            CHECK(dst2[i * 12 + j] == exp2[i * 12 + j]);
        }
    // The two radii genuinely differ here (so the dispatch is observable).
    CHECK(dst1[3 * 12 + 4] != dst2[3 * 12 + 4]);
}

// S6: svt_decode_xq (:597-:608), the params-aware version (BRIEF CORRECTION:
// not the spec-style 96/128-3x mapping). SGRPROJ_PRJ_BITS = 7, so the base
// is 128. Rows: 0 = both radii nonzero, 10 = r[0]==0, 14 = r[1]==0.
TEST_CASE("decodeXq both radii nonzero: xq1 = 128 - xq0 - xqd1") {
    const std::int32_t xqd[2] = {5, 3};
    std::int32_t xq[2] = {0, 0};
    l10_filters::restoration::decodeXq(xqd, xq, &l10_filters::restoration::sgrParams[0]);
    CHECK(xq[0] == 5);
    CHECK(xq[1] == 120);
}

TEST_CASE("decodeXq r[0]==0 (row 10): xq0 pinned to 0") {
    const std::int32_t xqd[2] = {5, 3};
    std::int32_t xq[2] = {0, 0};
    l10_filters::restoration::decodeXq(xqd, xq, &l10_filters::restoration::sgrParams[10]);
    CHECK(xq[0] == 0);
    CHECK(xq[1] == 125);
}

TEST_CASE("decodeXq r[1]==0 (row 14): xq1 pinned to 0") {
    const std::int32_t xqd[2] = {5, 3};
    std::int32_t xq[2] = {0, 0};
    l10_filters::restoration::decodeXq(xqd, xq, &l10_filters::restoration::sgrParams[14]);
    CHECK(xq[0] == 5);
    CHECK(xq[1] == 0);
}

TEST_CASE("decodeXq xqd[0]==0 edge: xq0 = 0, xq1 = 128 - xqd1") {
    const std::int32_t xqd[2] = {0, 3};
    std::int32_t xq[2] = {0, 0};
    l10_filters::restoration::decodeXq(xqd, xq, &l10_filters::restoration::sgrParams[0]);
    CHECK(xq[0] == 0);
    CHECK(xq[1] == 125);
}

// S7: x_by_xplus1 (:610-:625). Oracle derived against the extracted table:
// entry 0 = 1 (the named special case), entry 255 = 256 (the boundary),
// every other entry = floor((256*x + (x+1)/2) / (x+1)) with C integer
// division (0 mismatches over all 256 entries).
TEST_CASE("xByXplus1 boundaries: 0 -> 1 and 255 -> 256") {
    CHECK(l10_filters::restoration::xByXplus1[0] == 1);
    CHECK(l10_filters::restoration::xByXplus1[1] == 128);
    CHECK(l10_filters::restoration::xByXplus1[255] == 256);
}

TEST_CASE("xByXplus1 matches the rounding formula over all 256 entries") {
    for (int x = 0; x < 256; ++x) {
        std::int32_t f;
        if (x == 0) {
            f = 1;
        } else if (x == 255) {
            f = 256;
        } else {
            const std::int32_t t = (x + 1) / 2;
            f = (256 * x + t) / (x + 1);
        }
        CHECK(l10_filters::restoration::xByXplus1[x] == f);
    }
}

// S8: one_by_x (:627-:630), 25 entries = MAX_NELEM (MAX_RADIUS = 2).
// Oracle: floor((4096 + (x+1)/2) / (x+1)), 0 mismatches over all 25.
TEST_CASE("oneByX boundaries and length") {
    CHECK(l10_filters::restoration::oneByX[0] == 4096);
    CHECK(l10_filters::restoration::oneByX[24] == 164);
}

TEST_CASE("oneByX matches the rounding formula over all 25 entries") {
    for (int x = 0; x < 25; ++x) {
        const std::int32_t t = (x + 1) / 2;
        CHECK(l10_filters::restoration::oneByX[x] == (4096 + t) / (x + 1));
    }
}

// S9: extend_frame_lowbd (:110-:126). 8x4 tile with border_horz = 2,
// border_vert = 2 inside a sentinel-filled plane (stride 16). Side borders
// replicate the row-end pixels; the top/bottom borders copy the extended
// first/last row (width + 2*border_horz bytes).
TEST_CASE("extendFrameLowbd fills sides and copies the extended end rows") {
    std::uint8_t plane[8 * 16];
    for (int k = 0; k < 8 * 16; ++k) plane[k] = 0xEE;
    std::uint8_t* data = plane + 2 * 16 + 2;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j) data[i * 16 + j] = static_cast<std::uint8_t>(16 * (i + 1) + j);
    l10_filters::restoration::extendFrameLowbd(data, 8, 4, 16, 2, 2);
    // Side borders: each row's left two and right two bytes.
    CHECK(plane[2 * 16 + 0] == 16);
    CHECK(plane[2 * 16 + 1] == 16);
    CHECK(plane[2 * 16 + 10] == 23);
    CHECK(plane[2 * 16 + 11] == 23);
    CHECK(plane[5 * 16 + 0] == 64);
    CHECK(plane[5 * 16 + 11] == 71);
    // Top border rows: two full copies of extended row 0 (the copy spans
    // width + 2*border_horz bytes starting at data - border_horz).
    for (int j = 0; j < 12; ++j) CHECK(plane[0 * 16 + j] == plane[2 * 16 + j]);
    for (int j = 0; j < 12; ++j) CHECK(plane[1 * 16 + j] == plane[2 * 16 + j]);
    // Bottom border rows: two full copies of extended row 3.
    for (int j = 0; j < 12; ++j) CHECK(plane[6 * 16 + j] == plane[5 * 16 + j]);
    for (int j = 0; j < 12; ++j) CHECK(plane[7 * 16 + j] == plane[5 * 16 + j]);
    // The untouched far padding stays sentinel (the border copies span
    // cols 0..11 of their rows).
    CHECK(plane[0 * 16 + 15] == 0xEE);
    CHECK(plane[7 * 16 + 15] == 0xEE);
}

// S10: the svt_extend_frame entry (:150-:157), 8-bit dispatch. The highbd
// branch is named-parked (assert), unreachable in our emission
// (enable_restoration 0, 8-bit mono).
TEST_CASE("extendFrame dispatches highbd=0 to the lowbd body") {
    std::uint8_t a[8 * 16], b[8 * 16];
    for (int k = 0; k < 8 * 16; ++k) a[k] = b[k] = 0xEE;
    std::uint8_t* da = a + 2 * 16 + 2;
    std::uint8_t* db = b + 2 * 16 + 2;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j) {
            da[i * 16 + j] = static_cast<std::uint8_t>(16 * (i + 1) + j);
            db[i * 16 + j] = static_cast<std::uint8_t>(16 * (i + 1) + j);
        }
    l10_filters::restoration::extendFrameLowbd(db, 8, 4, 16, 2, 2);
    l10_filters::restoration::extendFrame(da, 8, 4, 16, 2, 2, 0);
    for (int k = 0; k < 8 * 16; ++k) CHECK(a[k] == b[k]);
}

// S11: copy_tile_lowbd (:159-:164) + the copy_tile dispatcher (:173-:180).
// 5x3 tile, src stride 12, dst stride 10; row padding must stay untouched.
TEST_CASE("copyTileLowbd copies width bytes per row, strides independent") {
    std::uint8_t src[3 * 12], dst[3 * 10];
    for (int k = 0; k < 3 * 12; ++k) src[k] = static_cast<std::uint8_t>(0x40 + k);
    for (int k = 0; k < 3 * 10; ++k) dst[k] = 0xEE;
    l10_filters::restoration::copyTileLowbd(5, 3, src, 12, dst, 10);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 5; ++j) CHECK(dst[i * 10 + j] == src[i * 12 + j]);
        CHECK(dst[i * 10 + 5] == 0xEE); // row padding untouched
        CHECK(dst[i * 10 + 9] == 0xEE);
    }
}

TEST_CASE("copyTile dispatches highbd=0 to the lowbd body") {
    std::uint8_t src[3 * 12], a[3 * 10], b[3 * 10];
    for (int k = 0; k < 3 * 12; ++k) src[k] = static_cast<std::uint8_t>(0x40 + k);
    for (int k = 0; k < 3 * 10; ++k) a[k] = b[k] = 0xEE;
    l10_filters::restoration::copyTileLowbd(5, 3, src, 12, a, 10);
    l10_filters::restoration::copyTile(5, 3, src, 12, b, 10, 0);
    for (int k = 0; k < 3 * 10; ++k) CHECK(a[k] == b[k]);
}

// S1: table-identity fixture over ALL 16 rows (transcribe-verify against
// restoration.c:33-:48; the mutation-proof class for data tables).
TEST_CASE("sgrParams table identity over all 16 rows") {
    static const int kExpected[16][2][2] = {
        {{2, 1}, {140, 3236}},  {{2, 1}, {112, 2158}},  {{2, 1}, {93, 1618}},
        {{2, 1}, {80, 1438}},   {{2, 1}, {70, 1295}},   {{2, 1}, {58, 1177}},
        {{2, 1}, {47, 1079}},   {{2, 1}, {37, 996}},    {{2, 1}, {30, 925}},
        {{2, 1}, {25, 863}},    {{0, 1}, {-1, 2589}},   {{0, 1}, {-1, 1618}},
        {{0, 1}, {-1, 1177}},   {{0, 1}, {-1, 925}},    {{2, 0}, {56, -1}},
        {{2, 0}, {22, -1}},
    };
    for (int i = 0; i < 16; ++i) {
        CHECK(l10_filters::restoration::sgrParams[i].r[0] == kExpected[i][0][0]);
        CHECK(l10_filters::restoration::sgrParams[i].r[1] == kExpected[i][0][1]);
        CHECK(l10_filters::restoration::sgrParams[i].s[0] == kExpected[i][1][0]);
        CHECK(l10_filters::restoration::sgrParams[i].s[1] == kExpected[i][1][1]);
    }
}
