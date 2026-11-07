#include <cstdint>
#include <doctest.h>
#include <analysis.h>
#include <subpel.h>

namespace {

using SubPixFn = std::uint32_t (*)(const std::uint8_t*, int, int, int, const std::uint8_t*, int,
                                   std::uint32_t*);
using PlainFn = std::uint32_t (*)(const std::uint8_t*, int, const std::uint8_t*, int, std::uint32_t*);

// The reference's own VARIANCES(W, H) instance list, in its own order
// (C_DEFAULT/variance.c:208-:229 - 22 lines, each instantiating one VAR and
// one SUBPIX_VAR). plainFn is the landed PU1/AN8 VAR instance to compare
// against at offset (0, 0); nullptr for 16x16 and 8x8, whose landed shapes
// are the AN8 const-128 offs-folded forms (the subpel path there is
// compared against the varianceCore oracle instead - see the test).
struct SubPixSize {
    int w;
    int h;
    SubPixFn sub;
    PlainFn plain;
};

const SubPixSize kSubPixSizes[22] = {
    {128, 128, analysis::subpel::subPixelVariance128x128, analysis::variance128x128},
    {128, 64, analysis::subpel::subPixelVariance128x64, analysis::variance128x64},
    {64, 128, analysis::subpel::subPixelVariance64x128, analysis::variance64x128},
    {64, 64, analysis::subpel::subPixelVariance64x64, analysis::variance64x64},
    {64, 32, analysis::subpel::subPixelVariance64x32, analysis::variance64x32},
    {32, 64, analysis::subpel::subPixelVariance32x64, analysis::variance32x64},
    {32, 32, analysis::subpel::subPixelVariance32x32, analysis::variance32x32},
    {32, 16, analysis::subpel::subPixelVariance32x16, analysis::variance32x16},
    {16, 32, analysis::subpel::subPixelVariance16x32, analysis::variance16x32},
    {16, 16, analysis::subpel::subPixelVariance16x16, nullptr},
    {16, 8, analysis::subpel::subPixelVariance16x8, analysis::variance16x8},
    {8, 16, analysis::subpel::subPixelVariance8x16, analysis::variance8x16},
    {8, 8, analysis::subpel::subPixelVariance8x8, nullptr},
    {8, 4, analysis::subpel::subPixelVariance8x4, analysis::variance8x4},
    {4, 8, analysis::subpel::subPixelVariance4x8, analysis::variance4x8},
    {4, 4, analysis::subpel::subPixelVariance4x4, analysis::variance4x4},
    {4, 16, analysis::subpel::subPixelVariance4x16, analysis::variance4x16},
    {16, 4, analysis::subpel::subPixelVariance16x4, analysis::variance16x4},
    {8, 32, analysis::subpel::subPixelVariance8x32, analysis::variance8x32},
    {32, 8, analysis::subpel::subPixelVariance32x8, analysis::variance32x8},
    {16, 64, analysis::subpel::subPixelVariance16x64, analysis::variance16x64},
    {64, 16, analysis::subpel::subPixelVariance64x16, analysis::variance64x16},
};

using ObmcFn = std::uint32_t (*)(const std::uint8_t*, int, const std::int32_t*, const std::int32_t*,
                                 std::uint32_t*);
using ObmcSubFn = std::uint32_t (*)(const std::uint8_t*, int, int, int, const std::int32_t*,
                                    const std::int32_t*, std::uint32_t*);

// The reference's own OBMC instance list, in its own order
// (C_DEFAULT/variance.c:276-:335 - 22 OBMC_VAR lines, each followed by its
// OBMC_SUBPIX_VAR twin; BRIEF CORRECTION: 22 pairs, the list continues past
// :303 with 4x16, 16x4, 8x32, 32x8, 16x64, 64x16).
struct ObmcSize {
    int w;
    int h;
    ObmcFn obmc;
    ObmcSubFn obmcSub;
};

const ObmcSize kObmcSizes[22] = {
    {4, 4, analysis::subpel::obmcVariance4x4, analysis::subpel::obmcSubPixelVariance4x4},
    {4, 8, analysis::subpel::obmcVariance4x8, analysis::subpel::obmcSubPixelVariance4x8},
    {8, 4, analysis::subpel::obmcVariance8x4, analysis::subpel::obmcSubPixelVariance8x4},
    {8, 8, analysis::subpel::obmcVariance8x8, analysis::subpel::obmcSubPixelVariance8x8},
    {8, 16, analysis::subpel::obmcVariance8x16, analysis::subpel::obmcSubPixelVariance8x16},
    {16, 8, analysis::subpel::obmcVariance16x8, analysis::subpel::obmcSubPixelVariance16x8},
    {16, 16, analysis::subpel::obmcVariance16x16, analysis::subpel::obmcSubPixelVariance16x16},
    {16, 32, analysis::subpel::obmcVariance16x32, analysis::subpel::obmcSubPixelVariance16x32},
    {32, 16, analysis::subpel::obmcVariance32x16, analysis::subpel::obmcSubPixelVariance32x16},
    {32, 32, analysis::subpel::obmcVariance32x32, analysis::subpel::obmcSubPixelVariance32x32},
    {32, 64, analysis::subpel::obmcVariance32x64, analysis::subpel::obmcSubPixelVariance32x64},
    {64, 32, analysis::subpel::obmcVariance64x32, analysis::subpel::obmcSubPixelVariance64x32},
    {64, 64, analysis::subpel::obmcVariance64x64, analysis::subpel::obmcSubPixelVariance64x64},
    {64, 128, analysis::subpel::obmcVariance64x128, analysis::subpel::obmcSubPixelVariance64x128},
    {128, 64, analysis::subpel::obmcVariance128x64, analysis::subpel::obmcSubPixelVariance128x64},
    {128, 128, analysis::subpel::obmcVariance128x128, analysis::subpel::obmcSubPixelVariance128x128},
    {4, 16, analysis::subpel::obmcVariance4x16, analysis::subpel::obmcSubPixelVariance4x16},
    {16, 4, analysis::subpel::obmcVariance16x4, analysis::subpel::obmcSubPixelVariance16x4},
    {8, 32, analysis::subpel::obmcVariance8x32, analysis::subpel::obmcSubPixelVariance8x32},
    {32, 8, analysis::subpel::obmcVariance32x8, analysis::subpel::obmcSubPixelVariance32x8},
    {16, 64, analysis::subpel::obmcVariance16x64, analysis::subpel::obmcSubPixelVariance16x64},
    {64, 16, analysis::subpel::obmcVariance64x16, analysis::subpel::obmcSubPixelVariance64x16},
};

} // namespace


TEST_CASE("bilinearFilters2t is the 8-row 2-tap table, row i = {128-16i, 16i}") {
    // 1:1 target: bilinear_filters_2t (third_party/SVT-AV1/Source/Lib/Codec/
    // filter.h:39-48), [BIL_SUBPEL_SHIFTS][2] = [8][2] (BIL_SUBPEL_BITS 3,
    // filter.h:35-36). BRIEF CORRECTION: the brief's "{128, 128} / 16-entry"
    // reading does not reproduce - row 0 is {128, 0} and the table is 8 rows.
    // 0-mismatch formula oracle over ALL 8 rows: row i = {128 - 16*i, 16*i}
    // (measured against filter.h:40-47: {128,0}, {112,16}, {96,32}, {80,48},
    // {64,64}, {48,80}, {32,96}, {16,112}); every row sums to FILTER_WEIGHT
    // 128 (FILTER_BITS 7, filter.h:22).
    for (int i = 0; i < 8; ++i) {
        CHECK(analysis::subpel::kBilinearFilters2t[i][0] == 128 - 16 * i);
        CHECK(analysis::subpel::kBilinearFilters2t[i][1] == 16 * i);
    }
    CHECK(analysis::subpel::kBilinearFilters2t[0][0] == 128);
    CHECK(analysis::subpel::kBilinearFilters2t[0][1] == 0);
}

TEST_CASE("filterBlock2dBilFirstPass horizontal 2x2 out of a 2x4 ramp, filter {112,16}") {
    // 1:1 target: aom_var_filter_block2d_bil_first_pass_c (C_DEFAULT/
    // variance.c:29-44). b[j] = ROUND_POWER_OF_TWO(a[0]*f0 + a[pixel_step]*f1,
    // FILTER_BITS) (variance.c:36) with ROUND_POWER_OF_TWO(v, 7) = (v + 64)>>7
    // (definitions.h:457; FILTER_BITS 7 = filter.h:22). BRIEF CORRECTION: the
    // rounding is (sum + 64) >> 7, NOT (sum + 1) >> 1.
    // a rows {10,30,50,70}, {60,80,100,120} at stride 4; ps = 1 (horizontal);
    // out 2x2 at stride 2; filter row 1 = {112, 16}. Buffer chosen so every
    // weighted sum hits mod-128 = 64, where the +64 rounding MOVES the result
    // (a dropped-rounding mutant returns one less per pixel):
    // b[0] = (10*112 + 30*16 + 64)>>7 = 1664>>7 = 13   (1600>>7 = 12)
    // b[1] = (30*112 + 50*16 + 64)>>7 = 4224>>7 = 33   (4160>>7 = 32)
    // b[2] = (60*112 + 80*16 + 64)>>7 = 8064>>7 = 63   (8000>>7 = 62)
    // b[3] = (80*112 + 100*16 + 64)>>7 = 10624>>7 = 83 (10560>>7 = 82)
    const std::uint8_t a[2 * 4] = {10, 30, 50, 70, 60, 80, 100, 120};
    std::uint16_t b[2 * 2] = {0, 0, 0, 0};
    analysis::subpel::filterBlock2dBilFirstPass(a, b, 4, 1, 2, 2,
                                                analysis::subpel::kBilinearFilters2t[1]);
    const std::uint16_t expected[4] = {13, 33, 63, 83};
    for (int i = 0; i < 4; ++i) {
        CHECK(b[i] == expected[i]);
    }
}

TEST_CASE("filterBlock2dBilFirstPass vertical 2x2, pixel_step = stride, filter {80,48}") {
    // Same :36 form with pixel_step = 4 = the source stride (the vertical
    // pass the SUBPIX macro drives, variance.c:199). ps = 4 reads a[j] and
    // a[j + 4] = the pixel one row down, so outputHeight 2 rows consume H+1
    // = 3 source rows (the fdata3[(H + 1) * W] allocation, variance.c:195).
    // FIXTURE FIX from the first RED: a 2-row buffer was read out of bounds;
    // the vertical pass needs the extra row. rows {10..40}, {50..80},
    // {90..120}; filter row 3 = {80, 48}:
    // b[0] = (10*80 + 50*48 + 64)>>7 = 3264>>7 = 25
    // b[1] = (20*80 + 60*48 + 64)>>7 = 4544>>7 = 35
    // b[2] = (50*80 + 90*48 + 64)>>7 = 8384>>7 = 65
    // b[3] = (60*80 + 100*48 + 64)>>7 = 9664>>7 = 75
    const std::uint8_t a[3 * 4] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120};
    std::uint16_t b[2 * 2] = {0, 0, 0, 0};
    analysis::subpel::filterBlock2dBilFirstPass(a, b, 4, 4, 2, 2,
                                                analysis::subpel::kBilinearFilters2t[3]);
    const std::uint16_t expected[4] = {25, 35, 65, 75};
    for (int i = 0; i < 4; ++i) {
        CHECK(b[i] == expected[i]);
    }
}

TEST_CASE("filterBlock2dBilSecondPass 2x2 out of a 2x2 uint16 block, filter {64,64}") {
    // 1:1 target: aom_var_filter_block2d_bil_second_pass_c (C_DEFAULT/
    // variance.c:55-69, the :46-:54 comment): uint16 -> uint8, the same
    // (sum + 64) >> 7 form (:62). Inputs are first-pass-scale values
    // (<= 255, the :50 contract). FIXTURE FIX from the first RED: the
    // ps = 1 pass reads ADJACENT pixels (a[j], a[j+1]) within the row, so
    // the row itself is {101, 120, 200, 220} at stride 4 (the first RED's
    // zero-padded rows put 0s under a[j+1]) and the +64 shows up once
    // inside the sum, not twice. Filter row 4 = {64, 64}:
    // b[0] = (101*64 + 120*64 + 64)>>7 = 14208>>7 = 111 (14144>>7 = 110)
    // b[1] = (120*64 + 200*64 + 64)>>7 = 20544>>7 = 160 (20480>>7 = 160)
    // b[2] = (200*64 + 221*64 + 64)>>7 = 27008>>7 = 211 (26944>>7 = 210)
    // b[3] = (221*64 + 240*64 + 64)>>7 = 29568>>7 = 231 (29504>>7 = 230)
    // b[0]/b[2] land on exact integers via the +64; the dropped-rounding
    // mutant returns one less there.
    const std::uint16_t a[2 * 4] = {101, 120, 200, 220, 200, 221, 240, 250};
    std::uint8_t b[2 * 2] = {0, 0, 0, 0};
    analysis::subpel::filterBlock2dBilSecondPass(a, b, 4, 1, 2, 2,
                                                 analysis::subpel::kBilinearFilters2t[4]);
    const std::uint8_t expected[4] = {111, 160, 211, 231};
    for (int i = 0; i < 4; ++i) {
        CHECK(b[i] == expected[i]);
    }
}

TEST_CASE("subPixelVariance4x4 at (0,0) equals the landed variance4x4 instance") {
    // 1:1 target: the SUBPIX_VAR(4, 4) instance svt_aom_sub_pixel_variance4x4_c
    // (C_DEFAULT/variance.c:192-202, instantiated at :223). At xoffset = 0
    // and yoffset = 0 the filter row is {128, 0} (filter.h:40), so
    // b[j] = (a[0]*128 + a[pixel_step]*0 + 64)>>7 = a[0] exactly in BOTH
    // passes and temp2 reproduces the 4x4 window of a verbatim - the subpel
    // path degenerates to the plain VAR instance (variance.c:201 calls
    // svt_aom_variance4x4_c on temp2). Equality of var AND sse.
    const std::uint8_t a[5 * 5] = {200, 10, 130, 60, 0,
                                   15, 240, 70, 110, 0,
                                   90, 35, 180, 5, 0,
                                   60, 125, 40, 255, 0,
                                   0, 0, 0, 0, 0};
    const std::uint8_t b[5 * 5] = {30, 90, 200, 15, 0,
                                   120, 60, 5, 240, 0,
                                   10, 200, 130, 80, 0,
                                   220, 15, 90, 40, 0,
                                   0, 0, 0, 0, 0};
    std::uint32_t sseSub = 0;
    std::uint32_t ssePlain = 0;
    const std::uint32_t varSub =
        analysis::subpel::subPixelVariance4x4(a, 5, 0, 0, b, 5, &sseSub);
    const std::uint32_t varPlain = analysis::variance4x4(a, 5, b, 5, &ssePlain);
    CHECK(varSub == varPlain);
    CHECK(sseSub == ssePlain);
}

TEST_CASE("subPixelVariance4x4 at (2,0) over a 10s ramp, b = zeros: 33800 / sse 157904") {
    // Hand-computed from the reference chain (variance.c:192-202). a rows
    // {10,20,30,40} .. {130,140,150,160} (stride 4), xoffset = 2 -> filter
    // row {96, 32} (filter.h:42), yoffset = 0 -> {128, 0} (verbatim rows).
    // First pass: b[r][c] = (a[r][c]*96 + a[r][c+1]*32 + 64)>>7; with
    // a[r][c] = 10*(4r + c + 1) every pixel is exact: temp2[r][c] =
    // 13 + 40r + 10c (rows 13/23/33/43, 53/63/73/83, 93/103/113/123,
    // 133/143/153/163). Second pass is the {128, 0} identity. vs b = zeros:
    // sum = 112 + 272 + 432 + 592 = 1408; sse = 3636 + 18996 + 47156 +
    // 88116 = 157904; var = 157904 - (1408*1408)/16 = 157904 - 123904 =
    // 34000. (First RED draft asserted 33800: a hand-division slip -
    // 1,982,464/16 is 123,904; the port's temp2 and sse matched the hand
    // computation exactly, the quotient was the error.)
    const std::uint8_t a[5 * 4] = {10, 20, 30, 40,
                                   50, 60, 70, 80,
                                   90, 100, 110, 120,
                                   130, 140, 150, 160,
                                   170, 180, 190, 200};
    const std::uint8_t b[4 * 4] = {0};
    std::uint32_t sse = 0xFFFFFFFF;
    const std::uint32_t var = analysis::subpel::subPixelVariance4x4(a, 4, 2, 0, b, 4, &sse);
    CHECK(var == 34000);
    CHECK(sse == 157904);
}

TEST_CASE("subPixelVariance8x4 at (0,2): vertical-only path over a 5x8 ramp, b = zeros: 828") {
    // 1:1 target: SUBPIX_VAR(8, 4) (variance.c:192-202). a[r][c] = 8r + c + 1
    // (5 rows x 8 cols, stride 8), xoffset = 0 -> {128, 0} first pass is the
    // identity, yoffset = 2 -> {96, 32} second pass over the W-strided
    // fdata3 (args W, W, H, W - variance.c:199):
    // temp2[r][c] = ((8r+c+1)*96 + (8r+c+9)*32 + 64)>>7 = 8r + c + 3.
    // Rows {3..10}, {11..18}, {19..26}, {27..34}; b = zeros:
    // sum = 52 + 116 + 180 + 244 = 592; sse = 380 + 1724 + 4092 + 7484 =
    // 13680; var = 13680 - 592*592/32 = 13680 - 10952 = 2728. (Two RED
    // drafts asserted 828/12996 with rows {4..11}: the temp2 +1 slip and
    // two hand sum-of-squares slips - the port's values are the arithmetic.)
    const std::uint8_t a[5 * 8] = {1, 2, 3, 4, 5, 6, 7, 8,
                                   9, 10, 11, 12, 13, 14, 15, 16,
                                   17, 18, 19, 20, 21, 22, 23, 24,
                                   25, 26, 27, 28, 29, 30, 31, 32,
                                   33, 34, 35, 36, 37, 38, 39, 40};
    const std::uint8_t b[4 * 8] = {0};
    std::uint32_t sse = 0;
    const std::uint32_t var = analysis::subpel::subPixelVariance8x4(a, 8, 0, 2, b, 8, &sse);
    CHECK(var == 2728);
    CHECK(sse == 13680);
}

TEST_CASE("all 22 SUBPIX instances: at (0,0) each equals its plain variance instance") {
    // 1:1 target: the VARIANCES(W, H) enumeration (variance.c:208-:229) -
    // BRIEF CORRECTION: 22 SUBPIX instances, not 16. 0-mismatch formula
    // oracle over ALL 22: at xoffset = yoffset = 0 the filter rows are both
    // {128, 0}, both passes are the identity, and the SUBPIX chain returns
    // the plain VAR instance's answer on the unmodified a window
    // (variance.c:201). Buffers: 129x128 max, stride W+1, LCG fill, a != b.
    for (int s = 0; s < 22; ++s) {
        const int w = kSubPixSizes[s].w;
        const int h = kSubPixSizes[s].h;
        const int stride = w + 1;
        static std::uint8_t a[129 * 128];
        static std::uint8_t b[129 * 128];
        unsigned seed = 12345u + 7u * static_cast<unsigned>(s);
        for (int i = 0; i < stride * h; ++i) {
            seed = seed * 1103515245u + 12345u;
            a[i] = static_cast<std::uint8_t>((seed >> 16) & 0xFF);
            seed = seed * 1103515245u + 12345u;
            b[i] = static_cast<std::uint8_t>((seed >> 16) & 0xFF);
        }
        std::uint32_t sseSub = 0;
        std::uint32_t ssePlain = 0;
        const std::uint32_t varSub =
            kSubPixSizes[s].sub(a, stride, 0, 0, b, stride, &sseSub);
        std::uint32_t varPlain;
        if (kSubPixSizes[s].plain) {
            varPlain = kSubPixSizes[s].plain(a, stride, b, stride, &ssePlain);
        } else {
            // The 16x16 / 8x8 deviation path: the same body the landed
            // instances run - varianceCore + the :189 return.
            int sum;
            analysis::varianceCore(a, stride, b, stride, w, h, &ssePlain, &sum);
            varPlain = ssePlain - static_cast<std::uint32_t>(static_cast<std::int64_t>(sum) * sum / (w * h));
        }
        CHECK_MESSAGE(varSub == varPlain, "var mismatch at size ", w, "x", h);
        CHECK_MESSAGE(sseSub == ssePlain, "sse mismatch at size ", w, "x", h);
    }
}

TEST_CASE("subPixelVariance4x4 at (4,2) over the 10s ramp, b = zeros: 34000 / sse 194000") {
    // Full 2-D chain, both offsets nonzero (variance.c:192-202). a rows
    // {10,20,30,40} .. {170,180,190,200} PLUS a defined row {210..240}:
    // with y != 0 the second pass consumes fdata3 row H, whose c = 3 tap
    // reads a[20] (the SUBPIX caller contract - H+1 rows must exist).
    // x = 4 -> {64, 64}: fdata3[r][c] = (a[r][c] + a[r][c+1] + 1)>>1 ...
    // = 5*(8r + 2c + 3) exact: rows {15,25,35,45}, {55,...}, {95,...},
    // {135,...}, {175,185,194,205}. y = 2 -> {96, 32}:
    // temp2[r][c] = (fdata3[r][c]*96 + fdata3[r+1][c]*32 + 64)>>7 =
    // 25 + 40r + 10c (rows 25/35/45/55 .. 145/155/165/175). b = zeros:
    // sum = 160 + 320 + 480 + 640 = 1600; sse = 6900 + 26100 + 58100 +
    // 102900 = 194000; var = 194000 - 1600*1600/16 = 194000 - 160000 =
    // 34000.
    const std::uint8_t a[6 * 4] = {10, 20, 30, 40,
                                   50, 60, 70, 80,
                                   90, 100, 110, 120,
                                   130, 140, 150, 160,
                                   170, 180, 190, 200,
                                   210, 220, 230, 240};
    const std::uint8_t b[4 * 4] = {0};
    std::uint32_t sse = 0;
    const std::uint32_t var = analysis::subpel::subPixelVariance4x4(a, 4, 4, 2, b, 4, &sse);
    CHECK(var == 34000);
    CHECK(sse == 194000);
}

TEST_CASE("subPixelVariance4x4 equals variance4x4 of the manually filtered block, all 64 offsets") {
    // The COMPOSITION property (variance.c:198-201): the macro must be
    // first-pass(xoffset) -> second-pass(yoffset) -> variance, with the
    // offsets routed to the two passes and the strides as written. Asserted
    // over the FULL 8x8 = 64 (xoffset, yoffset) grid the reference admits
    // (BIL_SUBPEL_SHIFTS = 8; BRIEF CORRECTION: the brief's {0,4,8,12} grid
    // indexes an 8-row table - 8 and 12 are out of range). Non-ramp buffer
    // so the per-offset results actually move.
    const std::uint8_t a[5 * 5] = {200, 10, 130, 60, 0,
                                   15, 240, 70, 110, 0,
                                   90, 35, 180, 5, 0,
                                   60, 125, 40, 255, 0,
                                   30, 85, 160, 220, 0};
    const std::uint8_t b[5 * 5] = {30, 90, 200, 15, 0,
                                   120, 60, 5, 240, 0,
                                   10, 200, 130, 80, 0,
                                   220, 15, 90, 40, 0,
                                   95, 175, 20, 140, 0};
    std::uint16_t fd[5 * 4];
    std::uint8_t t2[4 * 4];
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            analysis::subpel::filterBlock2dBilFirstPass(a, fd, 5, 1, 5, 4,
                                                        analysis::subpel::kBilinearFilters2t[x]);
            analysis::subpel::filterBlock2dBilSecondPass(fd, t2, 4, 4, 4, 4,
                                                         analysis::subpel::kBilinearFilters2t[y]);
            std::uint32_t sseExp = 0;
            std::uint32_t sseGot = 0;
            const std::uint32_t varExp = analysis::variance4x4(t2, 4, b, 5, &sseExp);
            const std::uint32_t varGot =
                analysis::subpel::subPixelVariance4x4(a, 5, x, y, b, 5, &sseGot);
            CHECK_MESSAGE(varGot == varExp, "var mismatch at (", x, ",", y, ")");
            CHECK_MESSAGE(sseGot == sseExp, "sse mismatch at (", x, ",", y, ")");
        }
    }
}

TEST_CASE("obmcVariance4x4 at mask 4096 degenerates to the plain variance of the residual") {
    // The masked path's algebraic degeneration (:240): with mask = 4096
    // (12-bit 1.0) and wsrc[j] = (pre[j] + c[j]) * 4096, diff =
    // RPOTS((pre+c)*4096 - pre*4096, 12) = c[j], so the OBMC variance is
    // the plain variance of c against a zero buffer - compared against the
    // landed variance4x4 instance. c sums 20, squares 44:
    // var = 44 - 20*20/16 = 44 - 25 = 19; sse = 44.
    const std::uint8_t pre[4 * 4] = {5, 9, 2, 7, 1, 8, 3, 6, 4, 0, 9, 2, 7, 1, 8, 3};
    const std::uint8_t c[4 * 4] = {1, 0, 3, 2, 0, 1, 1, 0, 2, 3, 0, 1, 0, 2, 1, 3};
    const std::uint8_t zeros[4 * 4] = {0};
    std::int32_t wsrc[16];
    std::int32_t mask[16];
    for (int i = 0; i < 16; ++i) {
        wsrc[i] = (pre[i] + c[i]) * 4096;
        mask[i] = 4096;
    }
    std::uint32_t sseObmc = 0;
    std::uint32_t ssePlain = 0;
    const std::uint32_t varObmc =
        analysis::subpel::obmcVariance4x4(pre, 4, wsrc, mask, &sseObmc);
    const std::uint32_t varPlain = analysis::variance4x4(c, 4, zeros, 4, &ssePlain);
    CHECK(varObmc == 19);
    CHECK(sseObmc == 44);
    CHECK(varObmc == varPlain);
}

TEST_CASE("obmcVariance4x4 hand-computed residuals: 89 / sse 89") {
    // 1:1 target: OBMC_VAR(4, 4) (variance.c:251-257) over the obmc_variance
    // core (:231-249): diff = ROUND_POWER_OF_TWO_SIGNED(wsrc[j] -
    // pre[j]*mask[j], 12) (:240; definitions.h:460-461 - the signed form,
    // NEGATIVE residuals round away differently than the unsigned macro).
    // pre = 100 everywhere (4x4, stride 4); mask = 1024 everywhere;
    // wsrc[j] = 100*1024 + d[j]*4096 with
    // d = {2,-2,4,-4,1,0,0,0,-1,3,-3,5,0,0,0,0} -> diff = d exactly
    // (RPOTS(4096d, 12) = d for both signs: (4096d + 2048)>>12 = d + 0.5
    // -> d, negated form symmetric). The 6th pixel carries a HALF-BIN
    // negative residual (wsrc - pre*mask = -6144 = -1.5*4096, RPOTS -> -2,
    // a dropped-signed-form mutant returns -1). sse = 4+4+16+16+1+4+1+9+9+25
    // = 89; sum = (2+4+1+3+5) - (2+4+2+1+3) = 15 - 12 = 3; var =
    // 89 - 9/16 = 89 - 0 = 89.
    const std::uint8_t pre[4 * 4] = {100, 100, 100, 100, 100, 100, 100, 100,
                                     100, 100, 100, 100, 100, 100, 100, 100};
    const std::int32_t d[16] = {2, -2, 4, -4, 1, 0, 0, 0, -1, 3, -3, 5, 0, 0, 0, 0};
    std::int32_t wsrc[16];
    std::int32_t mask[16];
    for (int i = 0; i < 16; ++i) {
        wsrc[i] = 100 * 1024 + d[i] * 4096;
        mask[i] = 1024;
    }
    wsrc[5] = 100 * 1024 - 6144; // the -1.5-bin residual
    std::uint32_t sse = 0;
    const std::uint32_t var = analysis::subpel::obmcVariance4x4(pre, 4, wsrc, mask, &sse);
    CHECK(var == 89);
    CHECK(sse == 89);
}

TEST_CASE("all 22 OBMC pairs: degenerate == plain variance; subpel(0,0) == obmc") {
    // 0-mismatch oracle over ALL 22 OBMC_VAR + 22 OBMC_SUBPIX instances
    // (variance.c:276-:335). (a) With mask = 4096 and wsrc = (pre + c)<<12
    // the core's diff = c (the S11 degeneration), so obmcVarianceWxH must
    // equal the plain variance of c vs zeros - the varianceCore oracle
    // (uniform over all sizes). (b) At xoffset = yoffset = 0 the SUBPIX
    // preamble is the identity, so obmcSubPixelVarianceWxH(0,0) must equal
    // obmcVarianceWxH (:270-:273).
    for (int s = 0; s < 22; ++s) {
        const int w = kObmcSizes[s].w;
        const int h = kObmcSizes[s].h;
        const int stride = w + 1;
        static std::uint8_t pre[129 * 128];
        static std::uint8_t c[129 * 128];
        static std::uint8_t zeros[129 * 128] = {0};
        static std::int32_t wsrc[129 * 128];
        static std::int32_t mask[129 * 128];
        unsigned seed = 987u + 11u * static_cast<unsigned>(s);
        for (int i = 0; i < stride * h; ++i) {
            seed = seed * 1103515245u + 12345u;
            pre[i] = static_cast<std::uint8_t>((seed >> 16) & 0xFF);
            seed = seed * 1103515245u + 12345u;
            c[i] = static_cast<std::uint8_t>((seed >> 14) & 3);
        }
        // FIXTURE FIX from the first RED: wsrc/mask are PACKED w-wide rows
        // (the core advances them by w, variance.c:246-247) while pre/c are
        // strided - build wsrc from the strided layout into the packed one
        // (the first RED built wsrc straight from the strided indices, so
        // rows 1.. mixed stride padding into the diffs).
        for (int r = 0; r < h; ++r) {
            for (int j = 0; j < w; ++j) {
                const int i = r * stride + j;
                wsrc[r * w + j] = (pre[i] + c[i]) * 4096;
                mask[r * w + j] = 4096;
            }
        }
        std::uint32_t sseObmc = 0;
        std::uint32_t sseCore = 0;
        const std::uint32_t varObmc = kObmcSizes[s].obmc(pre, stride, wsrc, mask, &sseObmc);
        int sumC;
        analysis::varianceCore(c, stride, zeros, stride, w, h, &sseCore, &sumC);
        const std::uint32_t varPlain =
            sseCore - static_cast<std::uint32_t>(static_cast<std::int64_t>(sumC) * sumC / (w * h));
        CHECK_MESSAGE(varObmc == varPlain, "degenerate var mismatch at size ", w, "x", h);
        CHECK_MESSAGE(sseObmc == sseCore, "degenerate sse mismatch at size ", w, "x", h);

        std::uint32_t sseSub = 0;
        std::uint32_t sseObmc2 = 0;
        const std::uint32_t varSub =
            kObmcSizes[s].obmcSub(pre, stride, 0, 0, wsrc, mask, &sseSub);
        const std::uint32_t varObmcAgain = kObmcSizes[s].obmc(pre, stride, wsrc, mask, &sseObmc2);
        CHECK_MESSAGE(varSub == varObmcAgain, "subpel(0,0) var mismatch at size ", w, "x", h);
        CHECK_MESSAGE(sseSub == sseObmc2, "subpel(0,0) sse mismatch at size ", w, "x", h);
    }
}

TEST_CASE("obmcSubPixelVariance4x4 at (4,0), constant residual 2: sse 64, var 0") {
    // 1:1 target: OBMC_SUBPIX_VAR(4, 4) (variance.c:259-274). pre = the
    // 10s ramp (+ the defined 6th row for the fdata3 row-H read); x = 4 ->
    // {64, 64}: temp2 = the verified half-pel H output
    // {15,25,35,45; 55,65,75,85; 95,105,115,125; 135,145,155,165};
    // y = 0 identity. mask = 4096, wsrc[j] = (temp2[j] + 2)*4096 -> the
    // core's diff = 2 everywhere: sse = 16*4 = 64; sum = 32;
    // var = 64 - 32*32/16 = 64 - 64 = 0 (constant residual, nonzero sse).
    const std::uint8_t pre[6 * 4] = {10, 20, 30, 40,
                                     50, 60, 70, 80,
                                     90, 100, 110, 120,
                                     130, 140, 150, 160,
                                     170, 180, 190, 200,
                                     210, 220, 230, 240};
    const int t2[16] = {15, 25, 35, 45, 55, 65, 75, 85, 95, 105, 115, 125, 135, 145, 155, 165};
    std::int32_t wsrc[16];
    std::int32_t mask[16];
    for (int i = 0; i < 16; ++i) {
        wsrc[i] = (t2[i] + 2) * 4096;
        mask[i] = 4096;
    }
    std::uint32_t sse = 0;
    const std::uint32_t var =
        analysis::subpel::obmcSubPixelVariance4x4(pre, 4, 4, 0, wsrc, mask, &sse);
    CHECK(var == 0);
    CHECK(sse == 64);
}

TEST_CASE("obmcSubPixelVariance4x4 equals obmcVariance4x4 of the filtered pre, all 64 offsets") {
    // The OBMC SUBPIX preamble wiring (variance.c:270-273): offsets routed
    // to the two passes, temp2 handed to the OBMC_VAR instance - asserted
    // as the composition equality over the FULL 8x8 offset grid on the
    // non-ramp buffer (offset-mutant sensitive, unlike the ramp).
    const std::uint8_t pre[5 * 5] = {200, 10, 130, 60, 0,
                                     15, 240, 70, 110, 0,
                                     90, 35, 180, 5, 0,
                                     60, 125, 40, 255, 0,
                                     30, 85, 160, 220, 0};
    const std::int32_t wsrc[16] = {771, -4096, 12, 90000, -5, 4096, 123456, -700,
                                   0, 99, -1234, 4096, 300, -77, 8192, 4096};
    const std::int32_t mask[16] = {4096, 1024, 2048, 512, 3072, 1, 4096, 256,
                                   1024, 2048, 512, 4096, 64, 4096, 128, 3072};
    std::uint16_t fd[5 * 4];
    std::uint8_t t2[4 * 4];
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            analysis::subpel::filterBlock2dBilFirstPass(pre, fd, 5, 1, 5, 4,
                                                        analysis::subpel::kBilinearFilters2t[x]);
            analysis::subpel::filterBlock2dBilSecondPass(fd, t2, 4, 4, 4, 4,
                                                         analysis::subpel::kBilinearFilters2t[y]);
            std::uint32_t sseExp = 0;
            std::uint32_t sseGot = 0;
            const std::uint32_t varExp =
                analysis::subpel::obmcVariance4x4(t2, 4, wsrc, mask, &sseExp);
            const std::uint32_t varGot =
                analysis::subpel::obmcSubPixelVariance4x4(pre, 5, x, y, wsrc, mask, &sseGot);
            CHECK_MESSAGE(varGot == varExp, "obmc var mismatch at (", x, ",", y, ")");
            CHECK_MESSAGE(sseGot == sseExp, "obmc sse mismatch at (", x, ",", y, ")");
        }
    }
}
