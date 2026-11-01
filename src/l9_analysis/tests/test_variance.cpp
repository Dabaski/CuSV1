#include <doctest.h>
#include <analysis.h>

// 1:1 target: svt_compute_mean_c at 8x8
// (third_party/SVT-AV1/Source/Lib/Codec/pic_analysis_process.c:194-216,
// declaration twin Codec/compute_mean.h:21; VARIANCE_PRECISION = 16 at
// pic_analysis_process.c:36).
//
// Derived arithmetic (see the AN1 commit message for the line-by-line
// derivation): uint64 accumulation of every pixel, then
// (sum << (VARIANCE_PRECISION >> 1)) / (width * height), shift first,
// truncating division, no rounding term. At 8x8: (sum << 8) / 64 = sum * 4
// exactly, so the shift/divide ORDER is settled by the citation
// (pic_analysis_process.c:211), not by any 8x8 test.

TEST_CASE("mean8x8 of a constant 128 block is 32768") {
    std::uint8_t src[8 * 8] = {0};
    for (int i = 0; i < 8 * 8; ++i) {
        src[i] = 128;
    }
    CHECK(analysis::mean8x8(src, 8) == 32768);
}

TEST_CASE("mean8x8 honors the src stride on a non-constant block") {
    // hand-computed: rows hold the ramp 0..63 laid out at stride 12,
    // sum = sum_y sum_x (8y + x) = 64*28 + 28*8 = 2016, mean = 2016*256/64.
    std::uint8_t src[8 * 12] = {0};
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            src[y * 12 + x] = (std::uint8_t)(y * 8 + x);
        }
    }
    CHECK(analysis::mean8x8(src, 12) == 8064);
}

TEST_CASE("mean8x8 of an all-255 block is 65280") {
    // hand-computed: sum = 64*255 = 16320, mean = 16320*256/64.
    std::uint8_t src[8 * 8];
    for (int i = 0; i < 8 * 8; ++i) {
        src[i] = 255;
    }
    CHECK(analysis::mean8x8(src, 8) == 65280);
}
