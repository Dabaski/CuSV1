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
//
// 1:1 target of meanSquare8x8: svt_compute_mean_squared_values_c at 8x8
// (pic_analysis_process.c:220-240, declaration twin compute_mean.h:26).
// Derived arithmetic: uint64 accumulation of every pixel square, then
// (sumSq << VARIANCE_PRECISION) / (width * height), shift first, truncating
// division, no rounding term (:237). At 8x8: (sumSq << 16) / 64 = sumSq *
// 1024 exactly, so the ORDER is settled by the :237 citation; the uint64
// accumulator width IS test-discriminable (the all-255 case wraps a uint32
// accumulator at the shift).

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

TEST_CASE("meanSquare8x8 of a constant 3 block is 589824") {
    // hand-computed from the :237 derivation: sumSq = 64*9 = 576,
    // (576 << 16) / 64 = 589824 = 9 << 16 (the scout's sketch, confirmed).
    std::uint8_t src[8 * 8] = {0};
    for (int i = 0; i < 8 * 8; ++i) {
        src[i] = 3;
    }
    CHECK(analysis::meanSquare8x8(src, 8) == 589824);
}

TEST_CASE("meanSquare8x8 honors the src stride on a non-constant block") {
    // hand-computed: rows hold the ramp 0..63 laid out at stride 12, so the
    // multiset summed is 0..63 and sumSq = sum v^2 = 63*64*127/6 = 85344,
    // meanSq = (85344 << 16) / 64 = 85344 * 1024.
    std::uint8_t src[8 * 12] = {0};
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            src[y * 12 + x] = (std::uint8_t)(y * 8 + x);
        }
    }
    CHECK(analysis::meanSquare8x8(src, 12) == 87392256);
}

TEST_CASE("meanSquare8x8 of an all-255 block is 4261478400") {
    // hand-computed: sumSq = 64*65025 = 4161600, meanSq = 4161600 << 16 / 64
    // = 4161600 * 1024 = 4261478400. A uint32 accumulator would wrap at the
    // shift - (4161600 << 16) mod 2^32 = 2151677952, / 64 = 33619968, the
    // value the mutation check below captured - so this assertion
    // discriminates the uint64 accumulator width at the shift.
    std::uint8_t src[8 * 8];
    for (int i = 0; i < 8 * 8; ++i) {
        src[i] = 255;
    }
    CHECK(analysis::meanSquare8x8(src, 8) == 4261478400);
}

TEST_CASE("subMean8x8 subsamples rows where the sub-mean differs from the full mean") {
    // 1:1 target: svt_compute_sub_mean_8x8_c (pic_analysis_process.c:242-261,
    // declaration twin compute_mean.h:31). Loop trace of
    // for (vi = 0; skip < 8; skip = vi + vi) with skip = 0 pre-set: rows
    // {0,2,4,6} of the full 8-wide row = 32 px, then block_mean << 3 with NO
    // division (:257). Fixture: even rows 10, odd rows 200, stride 12 - the
    // sub-sum is 32*10 = 320 -> 2560, while the FULL mean8x8 of the same
    // block would be 26880 (a column-subsample port gives 26880 too), so the
    // assertion discriminates row-subsample from both.
    std::uint8_t src[9 * 12] = {0};
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 8; ++x) {
            src[y * 12 + x] = (std::uint8_t)((y % 2 == 0) ? 10 : 200);
        }
    }
    CHECK(analysis::subMean8x8(src, 12) == 2560);
}

TEST_CASE("subMeanSquareValues at (8,8) normalizes by the HALF area") {
    // 1:1 target: svt_aom_compute_sub_mean_squared_values_c
    // (pic_analysis_process.c:263-284, declaration twin compute_mean.h:34).
    // Same skip mechanics gated on input_area_height: rows {0,2,4,6} of
    // squares = 32 px, then block_mean << 11 with NO division (:281). The
    // comment there drops the /2 - (sumSq << 16)/(w*h) at 8x8 would be
    // sumSq << 10 - but the LIVE code shifts 11 = (sumSq << 16)/(w*h/2):
    // 32 = 2^5 divides 2^16 exactly, 16 - 5 = 11. Anchors for << 11: the
    // SSE2 twin svt_aom_compute_subd_mean_of_squared_values8x8_sse2_intrin
    // (ASM_SSE2/compute_mean_intrin_sse2.c:15-51) returns << 11 at :50;
    // the FULL-square twin returns << 10 at :113 - the 2x ratio IS the
    // half-area normalization; and the consumer variance formula
    // (meanSq - mean*mean) >> 16 (:415-441) is fed by BOTH paths of
    // compute_b64_variance, so the scales must agree. Fixture: sumSq =
    // 32*100 = 3200 -> 3200 << 11 = 6553600; the full meanSquare8x8 of the
    // same block would be 1313996800.
    std::uint8_t src[9 * 12] = {0};
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 8; ++x) {
            src[y * 12 + x] = (std::uint8_t)((y % 2 == 0) ? 10 : 200);
        }
    }
    CHECK(analysis::subMeanSquareValues(src, 12, 8, 8) == 6553600);
}

TEST_CASE("subMeanSquareValues at odd height sums ceil(height/2) rows") {
    // The generic loop's condition is skip < input_area_height with skip
    // stepping 0,2,4,... - at height 9 the summed rows are {0,2,4,6,8},
    // i.e. ceil(height/2) = 5 rows (skip reaches 8 < 9 and 10 >= 9). On the
    // fixture row 8 is an even row of 10s: sumSq = 5*8*100 = 4000 ->
    // 4000 << 11 = 8192000. Pins the loop mechanics the (8,8)-only
    // assertion cannot see (the only live reference caller is (8,8),
    // pic_analysis_process.c:293-294; this exercises the admitted range).
    std::uint8_t src[9 * 12] = {0};
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 8; ++x) {
            src[y * 12 + x] = (std::uint8_t)((y % 2 == 0) ? 10 : 200);
        }
    }
    CHECK(analysis::subMeanSquareValues(src, 12, 8, 9) == 8192000);
}

TEST_CASE("subMeanSquareValues keeps the hardwired << 11 at (4,4)") {
    // The reference shifts 11 UNCONDITIONALLY (pic_analysis_process.c:281) -
    // it does not derive the shift from (w,h). At (4,4) a comment-faithful
    // port ((sumSq << 16) / (w*h/2) = sumSq << 13) would differ, so this
    // assertion pins the code's behavior, not the comment's intent. On the
    // fixture rows {0,2} are even rows of 10s, 4 px wide: sumSq = 8*100 =
    // 800 -> 800 << 11 = 1638400 (the comment-faithful value would be
    // 6553600).
    std::uint8_t src[9 * 12] = {0};
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 8; ++x) {
            src[y * 12 + x] = (std::uint8_t)((y % 2 == 0) ? 10 : 200);
        }
    }
    CHECK(analysis::subMeanSquareValues(src, 12, 4, 4) == 1638400);
}

TEST_CASE("intermVarFour8x8 fills the four mean/sq pairs left to right along one row origin") {
    // 1:1 target: svt_compute_interm_var_four8x8_c
    // (pic_analysis_process.c:286-313, declaration twin compute_mean.h:40).
    // The function CALLS the AN3 primitives, it does not recompute them: four
    // pairs of svt_compute_sub_mean_8x8_c +
    // svt_aom_compute_sub_mean_squared_values_c(src, stride, 8, 8) at
    // block_index = 0, 8, 16, 24 (:290-:312) - FOUR SIDE-BY-SIDE 8x8 BLOCKS
    // along ONE row origin, all reading rows {0,2,4,6}. The reference's own
    // "(0,1)..(0,4)" comments are meaningless labels and a "16x16 quadrants"
    // reading is NOT the mechanics: the live caller (compute_b64_variance,
    // pic_analysis_process.c:350-359) passes the frame-strided buffer and
    // advances blk_offset by 32 per call, so the four blocks are adjacent
    // along a row (offset 16 on a literal 16x16 tile at stride 16 would read
    // row 1; no caller does that).
    //
    // Fixture: 32-wide strip, stride 32, 8 rows plus a poison 9th row. Block
    // k (columns 8k..8k+7) is constant v_k = {10, 20, 30, 40} on ALL 8 rows,
    // so the even-row subsampling cannot shift the values; the poison row 8
    // (99s) is never read and catches over-reads. Hand-derived from the AN3
    // primitives: mean[k] = (32 * v_k) << 3 = 256 * v_k and
    // meanSq[k] = (32 * v_k^2) << 11 = 65536 * v_k^2, i.e. 2560 / 5120 /
    // 7680 / 10240 and 6553600 / 26214400 / 58982400 / 104857600 - four
    // DISTINCT values per array, so an offset/order mutation (quadrant
    // reading, reversed order, same-offset) fails by construction.
    std::uint8_t src[9 * 32] = {0};
    for (int y = 0; y < 8; ++y) {
        for (int k = 0; k < 4; ++k) {
            for (int x = 0; x < 8; ++x) {
                src[y * 32 + k * 8 + x] = (std::uint8_t)(10 * (k + 1));
            }
        }
    }
    for (int x = 0; x < 32; ++x) {
        src[8 * 32 + x] = 99;
    }
    std::uint64_t mean[4] = {0, 0, 0, 0};
    std::uint64_t meanSq[4] = {0, 0, 0, 0};
    analysis::intermVarFour8x8(src, 32, mean, meanSq);
    CHECK(mean[0] == 2560);
    CHECK(meanSq[0] == 6553600);
    CHECK(mean[1] == 5120);
    CHECK(meanSq[1] == 26214400);
    CHECK(mean[2] == 7680);
    CHECK(meanSq[2] == 58982400);
    CHECK(mean[3] == 10240);
    CHECK(meanSq[3] == 104857600);
}
