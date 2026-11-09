#include <doctest.h>
#include <analysis.h>
#include <temporal_filter.h>

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

TEST_CASE("downsample2d of a uniform 4x4 at step 2 decimates to 2x2") {
    // 1:1 target: svt_aom_downsample_2d_c (pic_analysis_process.c:134-164,
    // declaration twin Codec/motion_estimation.h:33-35). Derived mechanics:
    // input_stripe_stride = stride * decim_step (:144); half_decim_step =
    // decim_step >> 1 (:146); the input pointer is PRE-ADVANCED by
    // half_decim_step rows (:148) and prev_input_line is one row ABOVE the
    // current pointer (:151), so the summed 2x2 window is rows
    // {half-1, half} x cols {half-1, half} of each decim_step cell - at
    // step 2 that is the cell's own top-left 2x2 (0-phase); output =
    // (sum + 2) >> 2 (:157). Uniform 10s: sum = 40, (40 + 2) >> 2 = 10.
    std::uint8_t src[4 * 8];
    for (int i = 0; i < 4 * 8; ++i) {
        src[i] = 10;
    }
    std::uint8_t dst[2 * 2] = {0, 0, 0, 0};
    analysis::downsample2d(src, 8, 4, 4, dst, 2, 2);
    CHECK(dst[0] == 10);
    CHECK(dst[1] == 10);
    CHECK(dst[2] == 10);
    CHECK(dst[3] == 10);
}

TEST_CASE("downsample2d sums the 2x2 window with (+2)>>2 rounding") {
    // Hand-derived from :154-157: rows of the 4x4 hold the ramp
    // {0,4,8,12} / {1,5,9,13} / {2,6,10,14} / {3,7,11,15} at stride 8
    // (poison 99s in cols 4-7 are never read: the window reads cols
    // {half-1, half} = {0,1} then {2,3}). Every 2x2 sum is congruent 2
    // mod 4, so the +2 rounding term is DISCRIMINATED: out[0][0] =
    // (0+4+1+5+2)>>2 = 3 (a +2-less mutant gives 2); out[0][1] = 44>>2 = 11;
    // out[1][0] = 20>>2 = 5; out[1][1] = 52>>2 = 13. Non-uniform rows AND
    // columns, so a window mispairing (prev vs cur, hi-1 vs hi) fails.
    std::uint8_t src[4 * 8];
    static const std::uint8_t kRows[4][4] = {{0, 4, 8, 12}, {1, 5, 9, 13}, {2, 6, 10, 14}, {3, 7, 11, 15}};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            src[y * 8 + x] = kRows[y][x];
        }
        for (int x = 4; x < 8; ++x) {
            src[y * 8 + x] = 99;
        }
    }
    std::uint8_t dst[2 * 2] = {0, 0, 0, 0};
    analysis::downsample2d(src, 8, 4, 4, dst, 2, 2);
    CHECK(dst[0] == 3);
    CHECK(dst[1] == 11);
    CHECK(dst[2] == 5);
    CHECK(dst[3] == 13);
}

TEST_CASE("downsample2d at step 4 takes the MIDDLE 2x2 of each cell") {
    // The reference is generic over decim_step and the live sixteenth
    // caller passes 4 (pic_analysis_process.c:1530-1536). At step 4:
    // half_decim_step = 2, vi in {2,6}, hi in {2,6}, so the window rows are
    // {1,2} (prev = vi-1) and cols {1,2} WITHIN each 4x4 cell - NOT the
    // cell origin. Fixture src[y][x] = 8y + x at stride 8: out[0][0] =
    // (9+10+17+18+2)>>2 = 56>>2 = 14; out[0][1] = (13+14+21+22+2)>>2 = 18;
    // out[1][0] = (41+42+49+50+2)>>2 = 184>>2 = 46; out[1][1] =
    // (45+46+53+54+2)>>2 = 200>>2 = 50. A cell-origin mutant
    // (window at rows/cols {0,1}) gives out[0][0] = 20>>2 = 5 - killed.
    std::uint8_t src[8 * 8];
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            src[y * 8 + x] = (std::uint8_t)(y * 8 + x);
        }
    }
    std::uint8_t dst[2 * 2] = {0, 0, 0, 0};
    analysis::downsample2d(src, 8, 8, 8, dst, 2, 4);
    CHECK(dst[0] == 14);
    CHECK(dst[1] == 18);
    CHECK(dst[2] == 46);
    CHECK(dst[3] == 50);
}

TEST_CASE("downsample2d honors an independent stride") {
    // The per-row advances use input_stride / decim_stride as INDEPENDENT
    // parameters (:144, :159-160). Fixture: the same ramp as the rounding
    // case laid out at stride 12 with 99s in cols 4-11 (never read by the
    // faithful port), so the expected 2x2 is the same {3, 11, 5, 13}. A
    // stride-ignoring mutant (advance areaWidth * decimStep = 8 bytes per
    // row instead of 12) lands mid-row on the poison band: out differs.
    std::uint8_t src[4 * 12];
    static const std::uint8_t kRows[4][4] = {{0, 4, 8, 12}, {1, 5, 9, 13}, {2, 6, 10, 14}, {3, 7, 11, 15}};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            src[y * 12 + x] = kRows[y][x];
        }
        for (int x = 4; x < 12; ++x) {
            src[y * 12 + x] = 99;
        }
    }
    std::uint8_t dst[2 * 2] = {0, 0, 0, 0};
    analysis::downsample2d(src, 12, 4, 4, dst, 2, 2);
    CHECK(dst[0] == 3);
    CHECK(dst[1] == 11);
    CHECK(dst[2] == 5);
    CHECK(dst[3] == 13);
}

// AN7 fixture builder: a 64x64 block at stride 72 (poison 99s in cols
// 64-71 and in row 64 - the port must never read them). Block k = 8r+c:
// k == 0 is NON-UNIFORM (even block rows 10, odd block rows 20 - the
// sub-sampled rows the AN4 path reads are all 10s while the FULL path
// averages 10 and 20); every other block is uniform v = k+1.
static void buildB64Fixture(std::uint8_t* src) {
    for (int y = 0; y < 65; ++y) {
        for (int x = 0; x < 72; ++x) {
            src[y * 72 + x] = 99;
        }
    }
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            const int k = 8 * r + c;
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    src[(r * 8 + y) * 72 + c * 8 + x] =
                        (k == 0) ? (std::uint8_t)((y % 2 == 0) ? 10 : 20)
                                 : (std::uint8_t)(k + 1);
                }
            }
        }
    }
}

TEST_CASE("b64Variance SUB path: the fold pyramid on a fixture with one non-uniform 8x8") {
    // 1:1 target: compute_b64_variance (third_party/SVT-AV1/Source/Lib/Codec/
    // pic_analysis_process.c:318-442), static, single caller :610. The port
    // composes the landed AN primitives exactly as the reference composes
    // its own: the BLOCK_MEAN_PREC_SUB branch (:347-361) is SIXTEEN
    // intermVarFour8x8 calls (AN4), each filling FOUR consecutive 8x8
    // entries at blk_8x8_idx = 8*row + 4*colGroup, blk_offset advancing 32
    // per call; the 16x16 fold (:364-380) averages the four 8x8 blocks at
    // first_8x8_blk_idx = 16R + 2C {+0, +1, +8, +9} >> 2; the 32x32 fold
    // (:383-399) averages the four 16x16 blocks at first_16x16_blk_idx =
    // 8P + 2Q {+0, +1, +4, +5} >> 2; the 64x64 fold (:402-409) averages
    // the four 32x32 blocks >> 2. Every fold is independent per array
    // (means fold from means, squared-means from squared-means) - no level
    // recomputes from pixels. Variance = (sq - mean*mean) >> 16
    // (VARIANCE_PRECISION, :415-417/:423-426/:432-435/:439-441), uint16.
    //
    // sbVar layout: the reference writes pcs->variance[b64_idx] through the
    // EbMeTierZeroPu bases (me_context.h:47/:48/:52/:68) - 8x8 at 21+i,
    // 16x16 at 5+i, 32x32 at 1+i, 64x64 at 0 - so the fixture poisons a
    // 96-entry array and checks the placement, including that 85..95 stay
    // untouched (the reference writes 0..84 at most; MAX_ME_PU_COUNT 209,
    // me_sb_results.h:22).
    //
    // Fixture arithmetic (hand-derived, machine-checked against the
    // derivation script): block 0 samples 10s -> mean8[0] = 2560,
    // sq8[0] = 6553600; blocks k>=1 are uniform v=k+1 -> mean8[k] =
    // 256(k+1), sq8[k] = 65536(k+1)^2. The folds give
    // mean16[0] = (2560+512+2304+2560)>>2 = 1984 and for i>0
    // mean16[i] = 256*k1(i) + 1408 with k1(i) = 16R + 2C (the v-sum of any
    // 16x16 quadrant is 4*k1 + 22); sq16[0] = 65536*(100+4+81+100)>>2 =
    // 4669440 and sq16[i>0] = 16384*(4k1^2 + 44k1 + 186). Every variance
    // value below is (sq - mean^2) >> 16 with the truncations shown:
    // var16[0] = (4669440 - 1984^2)>>16 = 733184>>16 = 11 (11.1875);
    // var16[i>0] = 1064960>>16 = 16 (16.25, constant because every
    // quadrant's value multiset is {k+1, k+2, k+9, k+10}); var32[0] =
    // (19509248 - 3856^2)>>16 = 4640512>>16 = 70 (70.8125) while
    // var32[1..3] = 5324800>>16 = 81 (81.25); var64 = (91687936 -
    // 8356^2)>>16 = 21865200>>16 = 333 (333.66); all 64 var8 are 0
    // (block 0 samples a constant 10-half, the rest are uniform).
    std::uint8_t src[65 * 72];
    buildB64Fixture(src);
    std::uint64_t mean16[16];
    std::uint64_t sq16[16];
    std::uint64_t mean32[4];
    std::uint64_t sq32[4];
    std::uint64_t mean64 = 0;
    std::uint64_t sq64 = 0;
    std::uint16_t sbVar[96];
    for (int i = 0; i < 96; ++i) {
        sbVar[i] = 0xBEEF;
    }
    analysis::b64Variance(src, 72, analysis::BlockMeanPrec::Sub, true, sbVar,
                          mean16, sq16, mean32, sq32, &mean64, &sq64);

    static const std::uint64_t kMean16[16] = {1984, 1920, 2432, 2944, 5504, 6016,
                                              6528, 7040, 9600, 10112, 10624, 11136,
                                              13696, 14208, 14720, 15232};
    static const std::uint64_t kSq16[16] = {4669440, 4751360, 6979584, 9732096,
                                            31358976, 37257216, 43679744, 50626560,
                                            93224960, 103317504, 113934336, 125075456,
                                            188645376, 202932224, 217743360, 233078784};
    static const std::uint64_t kMean32[4] = {3856, 4736, 11904, 12928};
    static const std::uint64_t kSq32[4] = {19509248, 27754496, 147030016, 172457984};
    for (int i = 0; i < 16; ++i) {
        CHECK(mean16[i] == kMean16[i]);
        CHECK(sq16[i] == kSq16[i]);
    }
    for (int j = 0; j < 4; ++j) {
        CHECK(mean32[j] == kMean32[j]);
        CHECK(sq32[j] == kSq32[j]);
    }
    CHECK(mean64 == 8356);
    CHECK(sq64 == 91687936);

    // Variance placement: the EbMeTierZeroPu bases (me_context.h:47/:48/:52/:68).
    CHECK(sbVar[0] == 333);                                  // 64x64
    CHECK(sbVar[1] == 70);                                   // 32x32_0
    CHECK(sbVar[2] == 81);                                   // 32x32_1
    CHECK(sbVar[3] == 81);                                   // 32x32_2
    CHECK(sbVar[4] == 81);                                   // 32x32_3
    CHECK(sbVar[5] == 11);                                   // 16x16_0
    for (int i = 1; i < 16; ++i) {                           // 16x16_1..15
        CHECK(sbVar[5 + i] == 16);
    }
    for (int i = 0; i < 64; ++i) {                           // 8x8_0..63
        CHECK(sbVar[21 + i] == 0);
    }
    for (int i = 85; i < 96; ++i) {                          // untouched tail
        CHECK(sbVar[i] == 0xBEEF);
    }
}

TEST_CASE("b64Variance FULL path diverges exactly where block 0 is non-uniform") {
    // The BLOCK_MEAN_PREC_FULL branch (:335-346) computes every 8x8 with
    // svt_compute_mean_8x8 + svt_compute_mean_square_values_8x8 (the AN1/AN2
    // ports). Block 0's full 64 px are half 10s half 20s: mean8[0] =
    // (32*10 + 32*20)<<8/64 = 960*4 = 3840 and sq8[0] = (32*100 + 32*400)
    // <<16/64 = 16000*1024 = 16384000 - DIFFERENT from the SUB values
    // (2560 / 6553600) exactly because block 0 is non-uniform (uniform
    // blocks give identical values on both paths, derived). Downstream:
    // mean16[0] = (3840+512+2304+2560)>>2 = 2304, sq16[0] =
    // (16384000+262144+5308416+6553600)>>2 = 7127040, var16[0] =
    // (7127040 - 2304^2)>>16 = 1818624>>16 = 27 (27.75); mean32[0] =
    // (2304+1920+5504+6016)>>2 = 3936, sq32[0] = (7127040+4751360+
    // 31358976+37257216)>>2 = 20123648; mean64 = (3936+4736+11904+12928)>>2
    // = 8376, sq64 = (20123648+27754496+147030016+172457984)>>2 =
    // 91841536, var64 = (91841536 - 8376^2)>>16 = 21684160>>16 = 330
    // (330.75); var8[0] = (16384000 - 3840^2)>>16 = 1638400>>16 = 25 while
    // every other var8 stays 0 (sbVar[22] == 0 vs sbVar[21] == 25 pins
    // that the divergence is exactly block 0). All quartets NOT containing
    // block 0 are identical to the SUB path (mean16[1], sq16[15],
    // mean32[3], sbVar[20], sbVar[4] re-asserted unchanged).
    std::uint8_t src[65 * 72];
    buildB64Fixture(src);
    std::uint64_t mean16[16];
    std::uint64_t sq16[16];
    std::uint64_t mean32[4];
    std::uint64_t sq32[4];
    std::uint64_t mean64 = 0;
    std::uint64_t sq64 = 0;
    std::uint16_t sbVar[96];
    for (int i = 0; i < 96; ++i) {
        sbVar[i] = 0xBEEF;
    }
    analysis::b64Variance(src, 72, analysis::BlockMeanPrec::Full, true, sbVar,
                          mean16, sq16, mean32, sq32, &mean64, &sq64);
    CHECK(mean16[0] == 2304);
    CHECK(sq16[0] == 7127040);
    CHECK(mean32[0] == 3936);
    CHECK(sq32[0] == 20123648);
    CHECK(mean64 == 8376);
    CHECK(sq64 == 91841536);
    CHECK(sbVar[0] == 330);
    CHECK(sbVar[5] == 27);
    CHECK(sbVar[21] == 25);
    CHECK(sbVar[22] == 0);
    CHECK(mean16[1] == 1920);
    CHECK(sq16[15] == 233078784);
    CHECK(mean32[3] == 12928);
    CHECK(sbVar[20] == 16);
    CHECK(sbVar[4] == 81);
}

TEST_CASE("b64Variance with subBlockVariances=false writes only the 64x64 entry") {
    // The reference gates the 8x8/16x16/32x32 variance loops on
    // scs->allintra || scs->static_config.aq_mode == 1 ||
    // scs->static_config.variance_octile (:412) and ALWAYS writes the 64x64
    // entry (:438-441). With the gate off the port must leave every
    // sub-block slot untouched (the reference's pcs->variance row keeps
    // whatever it held): the fixture poisons all 96 entries and only
    // sbVar[0] may move.
    std::uint8_t src[65 * 72];
    buildB64Fixture(src);
    std::uint64_t mean16[16];
    std::uint64_t sq16[16];
    std::uint64_t mean32[4];
    std::uint64_t sq32[4];
    std::uint64_t mean64 = 0;
    std::uint64_t sq64 = 0;
    std::uint16_t sbVar[96];
    for (int i = 0; i < 96; ++i) {
        sbVar[i] = 0xBEEF;
    }
    analysis::b64Variance(src, 72, analysis::BlockMeanPrec::Sub, false, sbVar,
                          mean16, sq16, mean32, sq32, &mean64, &sq64);
    CHECK(sbVar[0] == 333);
    CHECK(sbVar[1] == 0xBEEF);
    CHECK(sbVar[4] == 0xBEEF);
    CHECK(sbVar[5] == 0xBEEF);
    CHECK(sbVar[20] == 0xBEEF);
    CHECK(sbVar[21] == 0xBEEF);
    CHECK(sbVar[84] == 0xBEEF);
    CHECK(mean64 == 8356);
    CHECK(sq64 == 91687936);
}

TEST_CASE("calculateHistogram counts one pixel per bin on a 0..255 ramp") {
    // 1:1 target: calculate_histogram (pic_analysis_process.c:170-188). The
    // real name is calculate_histogram - NOT the brief's
    // svt_aom_calculate_histogram - and it has NO header declaration twin
    // (grep-verified: the only Codec occurrences are the :167 comment, the
    // :170 definition and the :560 caller; the function is file-local to
    // its .c). Mechanics: the outer loop steps vertical_index by decim_step
    // but selects the row by POINTER advance (src += stride * decim_step);
    // the inner loop steps the column by decim_step; the PIXEL VALUE is the
    // bin index (256 bins, uint32_t); sum accumulates the SAMPLED pixels
    // only (the caller rescales by decim_step^2 at :569 and pre-initializes
    // the bins to 1 at :545-547 - the function only ADDS on top, so the
    // test as caller initializes the bins to 0).
    //
    // Fixture (the brief's ramp): 16x16, stride 16, step 1, src[i] = i, so
    // every value 0..255 appears EXACTLY once. Hand-derived: every bin = 1;
    // sum of bins = 256 = the counted pixels; histogram[0] = histogram[255]
    // = 1; *sum = 0+1+...+255 = 255*256/2 = 32640. The two specific-bin
    // assertions carry the bin-index discrimination: a mutant that bins by
    // COLUMN index (histogram[hi]) would leave the bin sum at 256 and *sum
    // correct while histogram[0] reads 16 and histogram[255] reads 0.
    std::uint8_t src[16 * 16] = {0};
    for (int i = 0; i < 16 * 16; ++i) {
        src[i] = (std::uint8_t)i;
    }
    std::uint32_t histogram[256] = {0};
    std::uint64_t sum = 0;
    analysis::calculateHistogram(src, 16, 16, 16, 1, histogram, &sum);
    std::uint32_t binSum = 0;
    for (int b = 0; b < 256; ++b) {
        binSum += histogram[b];
    }
    CHECK(binSum == 256);
    CHECK(histogram[0] == 1);
    CHECK(histogram[255] == 1);
    CHECK(sum == 32640);
}

TEST_CASE("calculateHistogram honors stride != width") {
    // The row advance is src += stride * decim_step (:184) with stride an
    // INDEPENDENT parameter - a stride-ignoring mutant (src += width) reads
    // the wrong rows. Fixture: width 4, height 4, stride 8, step 1; rows
    // hold 0..15 in cols 0-3 and poison 99s in cols 4-7, so the mutant's
    // row 1 lands on the poison half-row: hand-derived for the faithful
    // port, bins 0..15 = 1 and every other bin 0, *sum = 0+1+...+15 = 120,
    // sum of bins = 16; for the mutant, histogram[99] = 4 and bins 8..15
    // stay 0, so the histogram[99] == 0 and histogram[15] == 1 assertions
    // kill it.
    std::uint8_t src[4 * 8] = {0};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            src[y * 8 + x] = (std::uint8_t)(y * 4 + x);
        }
        for (int x = 4; x < 8; ++x) {
            src[y * 8 + x] = 99;
        }
    }
    std::uint32_t histogram[256] = {0};
    std::uint64_t sum = 0;
    analysis::calculateHistogram(src, 4, 4, 8, 1, histogram, &sum);
    std::uint32_t binSum = 0;
    for (int b = 0; b < 256; ++b) {
        binSum += histogram[b];
    }
    CHECK(binSum == 16);
    CHECK(histogram[15] == 1);
    CHECK(histogram[99] == 0);
    CHECK(sum == 120);
}

TEST_CASE("countColorsWithThreshold passes a solid block with the count 1") {
    // 1:1 target: svt_av1_count_colors_with_threshold
    // (third_party/SVT-AV1/Source/Lib/Codec/pic_analysis_process.c:911-930):
    // a 256-entry has_color[] flag array, one bit per distinct pixel value,
    // early-exit FALSE once the count exceeds num_colors_threshold. A solid
    // block has one distinct value: the function returns true and reports
    // num_colors = 1 (the AA-aware caller additionally requires > 1,
    // :1118-1124 - that check lives at the caller, not here).
    std::uint8_t src[8 * 8];
    for (int i = 0; i < 8 * 8; ++i) {
        src[i] = 10;
    }
    int numColors = -1;
    CHECK(analysis::countColorsWithThreshold(src, 8, 8, 8, 4, &numColors));
}

TEST_CASE("countColorsWithThreshold exits early with the count at threshold + 1") {
    // The early exit is part of the contract (:922-925): the scan returns
    // false the moment the count exceeds the threshold, so a distinct value
    // planted LATER in scan order must never be counted. Fixture: row-major
    // order plants 20/30/40 at (0,1..3) and the 5th distinct value 50 at
    // (0,4) with threshold 4 - the faithful port stops there (count 5,
    // false); a mutant that scans on counts the 99 at (0,7) as a 6th color.
    std::uint8_t src[8 * 8];
    for (int i = 0; i < 8 * 8; ++i) {
        src[i] = 10;
    }
    src[1] = 20;
    src[2] = 30;
    src[3] = 40;
    src[4] = 50;
    src[7] = 99;
    int numColors = -1;
    const bool ok = analysis::countColorsWithThreshold(src, 8, 8, 8, 4, &numColors);
    CHECK_FALSE(ok);
    CHECK(numColors == 5);
}

TEST_CASE("isValidPaletteNbColors accepts a two-color block at threshold 4") {
    // 1:1 target: is_valid_palette_nb_colors
    // (pic_analysis_process.c:956-978, static): the same has_color[] scan as
    // count_colors_with_threshold PLUS the palette gate the caller relies
    // on: nb_colors <= 1 is NOT a valid palette (:973-975) - a valid
    // palette block has between 2 and nb_colors_threshold distinct colors.
    // Fixture: a 2x2 checkerboard of 10/20, 2 colors <= 4 -> true.
    std::uint8_t src[2 * 2] = {10, 20, 10, 20};
    CHECK(analysis::isValidPaletteNbColors(src, 2, 2, 2, 4));
}

TEST_CASE("isValidPaletteNbColors rejects a solid block (nb_colors <= 1)") {
    // The :973-975 palette gate: a one-color block is NOT a valid palette
    // even though it never trips the threshold. This pins the gate the
    // count_colors_with_threshold twin does NOT have (that one returns true
    // on a solid block - asserted in its own test above).
    std::uint8_t src[4 * 4];
    for (int i = 0; i < 4 * 4; ++i) {
        src[i] = 77;
    }
    CHECK_FALSE(analysis::isValidPaletteNbColors(src, 4, 4, 4, 4));
}

TEST_CASE("isValidPaletteNbColors rejects more colors than the threshold") {
    // The early exit (:967-969) is LOAD-BEARING for the boolean here: the
    // final gate is only nb_colors <= 1 (:973-975), so without the early
    // exit a 5-color block at threshold 4 would fall through to TRUE.
    // Fixture: a 2-color checkerboard (10/20) plus three planted values
    // 30/40/50 spread over the block - 5 distinct at threshold 4 -> false.
    std::uint8_t src[4 * 4];
    for (int i = 0; i < 4 * 4; ++i) {
        src[i] = ((i & 1) == 0) ? 10 : 20;
    }
    src[0] = 30;
    src[5] = 40;
    src[10] = 50;
    CHECK_FALSE(analysis::isValidPaletteNbColors(src, 4, 4, 4, 4));
}

TEST_CASE("findDominantValue breaks count ties toward the FIRST maximum") {
    // 1:1 target: svt_av1_find_dominant_value
    // (pic_analysis_process.c:984-1003): the update is STRICTLY greater
    // (:995), so on a count tie the value whose maximum count was reached
    // FIRST in scan order wins. The implementation landed alongside slice
    // 3's GREEN edit (over-written), so this case's RED evidence is its
    // mutation: >= in the update flips the winner here. Fixture (4x4,
    // row-major): 5 x5 (last at (3,1)), 9 x5 (last at (3,2)), 7 x4,
    // 3 x2 - counts tie at 5 and 5's maximum is reached first, so the
    // dominant is 5; a >= mutant updates to 9 at (3,2).
    std::uint8_t src[4 * 4] = {5, 5, 5, 9, 9, 9, 9, 5, 3, 3, 7, 7, 7, 5, 9, 7};
    CHECK(analysis::findDominantValue(src, 4, 4, 4) == 5);
}

TEST_CASE("dilateBlock overwrites through DIAGONALS and stays inside the block") {
    // 1:1 target: svt_av1_dilate_block (pic_analysis_process.c:1024-1076):
    // copy, then every dominant pixel writes the dominant value into all
    // EIGHT neighbours, each bound-gated, one pass (the source is read,
    // never the dilated buffer). The implementation landed alongside slice
    // 3's GREEN edit (over-written), so this case's RED evidence is its
    // mutation. Fixture 4x4 (D = 3 dominant at 11 px, n = 8): the target
    // (1,1) has ALL FOUR 4-adjacent neighbours non-dominant and only its
    // four diagonals dominant, so a faithful port overwrites it via the
    // corner dilates while a 4-direction-only mutant (or a no-dilation
    // copy) leaves it 8. Poison 99s fill src cols 4-7 (stride 8) and the
    // dilated buffer is 4x8 zero-initialised with dilated_stride 8: the
    // reference only ever writes rows/cols of the block, so cols 4-7 must
    // stay 0 (the detector passes dilated_stride = blk_w, :1140).
    std::uint8_t src[4 * 8] = {0};
    static const std::uint8_t kBlock[4][4] = {{3, 8, 3, 3}, {8, 8, 8, 3}, {3, 8, 3, 3}, {3, 3, 3, 3}};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            src[y * 8 + x] = kBlock[y][x];
        }
        for (int x = 4; x < 8; ++x) {
            src[y * 8 + x] = 99;
        }
    }
    std::uint8_t dilated[4 * 8] = {0};
    analysis::dilateBlock(src, 8, dilated, 8, 4, 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            CHECK(dilated[y * 8 + x] == 3);
        }
        for (int x = 4; x < 8; ++x) {
            CHECK(dilated[y * 8 + x] == 0);
        }
    }
}

TEST_CASE("variance16x16 of a constant 200 block is 0 with sse 1327104") {
    // 1:1 targets: the VAR(W,H) macro instance svt_aom_variance16x16_c
    // (third_party/SVT-AV1/Source/Lib/C_DEFAULT/variance.c:184-190, via
    // variance_c :141-158) called the way the detectors call it
    // (pic_analysis_process.c:950/:1129/:1150/:1376/:1417): b =
    // svt_aom_eb_av1_var_offs (pic_analysis_process.c:936-942, constant
    // 128) at ref_stride 0. variance_c: diff = a[j] - 128, sum += diff
    // (int), sse += diff*diff (uint32); the macro returns
    // sse - (uint32_t)(((int64_t)sum*sum) / 256). With b = const 128 the
    // offset cancels: var = sum((a-mean)^2) - the population variance sum.
    // Fixture: all 200 -> diff 72 each, sum = 18432, sse = 256*5184 =
    // 1327104, sum^2/256 = 1327104 exact, var = 0.
    std::uint8_t src[16 * 16];
    for (int i = 0; i < 16 * 16; ++i) {
        src[i] = 200;
    }
    std::uint32_t sse = 0;
    const std::uint32_t var = analysis::variance16x16(src, 16, &sse);
    CHECK(var == 0);
    CHECK(sse == 1327104);
}

TEST_CASE("variance16x16 truncates the sum-squared division") {
    // The :189 division ((int64_t)sum*sum) / 256 is INTEGER division. The
    // implementation landed in slice 8's GREEN edit (over-written), so this
    // case's RED evidence is its mutation (the division rounded instead of
    // truncated). Fixture: 100 px at 129 (+1) and 156 px at 127 (-1):
    // sum = -56, sum^2 = 3136, 3136/256 = 12.25 -> 12 truncated (a ceil
    // mutant uses 13), sse = 256, var = 256 - 12 = 244.
    std::uint8_t src[16 * 16];
    for (int i = 0; i < 16 * 16; ++i) {
        src[i] = 127;
    }
    for (int i = 0; i < 100; ++i) {
        src[i] = 129;
    }
    std::uint32_t sse = 0;
    const std::uint32_t var = analysis::variance16x16(src, 16, &sse);
    CHECK(var == 244);
    CHECK(sse == 256);
}

TEST_CASE("variance8x8 of a mixed block applies the same VAR arithmetic at 64 pels") {
    // 1:1 target: the VAR(W,H) instance svt_aom_variance8x8_c
    // (C_DEFAULT/variance.c:184-190) at W*H = 64, same const-128 offs call
    // (pic_analysis_process.c:1417). Fixture: 32 px at 130 (+2), 32 px at
    // 126 (-2): sum = 0, sse = 64*4 = 256, sum^2/64 = 0, var = 256.
    std::uint8_t src[8 * 8];
    for (int i = 0; i < 8 * 8; ++i) {
        src[i] = (i < 32) ? 130 : 126;
    }
    std::uint32_t sse = 0;
    const std::uint32_t var = analysis::variance8x8(src, 8, &sse);
    CHECK(var == 256);
    CHECK(sse == 256);
}

TEST_CASE("getSbyPerpixelVariance16x16 rounds with +128 before >> 8") {
    // 1:1 target: svt_av1_get_sby_perpixel_variance
    // (pic_analysis_process.c:944-952) at BLOCK_16X16:
    // ROUND_POWER_OF_TWO(var, eb_num_pels_log2_lookup[BLOCK_16X16])
    // (definitions.h:457, common_utils.c:39 -> 8): (var + 128) >> 8. The
    // fixture pins the ROUNDING TERM exactly: 16 px at 130 (+2) and 16 px
    // at 126 (-2), the rest 128 -> sum = 0, sse = 32*4 = 128, var = 128;
    // (128 + 128) >> 8 = 1 while a bare >> 8 (no +128) gives 0.
    std::uint8_t src[16 * 16];
    for (int i = 0; i < 16 * 16; ++i) {
        src[i] = (i < 32) ? ((i < 16) ? 130 : 126) : 128;
    }
    CHECK(analysis::getSbyPerpixelVariance16x16(src, 16) == 1);
}

TEST_CASE("getSbyPerpixelVariance8x8 rounds with +32 before >> 6") {
    // Same :944-952 target at BLOCK_8X8: eb_num_pels_log2_lookup -> 6
    // (common_utils.c:39), ROUND_POWER_OF_TWO -> (var + 32) >> 6. The
    // implementation landed in slice 10's GREEN edit (over-written), so
    // this case's RED evidence is its mutation (the rounding term). The
    // fixture pins it: 4 px at 130 (+2) and 4 px at 126 (-2), the rest 128
    // -> sum = 0, sse = 8*4 = 32, var = 32; (32 + 32) >> 6 = 1 while a
    // bare >> 6 gives 0.
    std::uint8_t src[8 * 8];
    for (int i = 0; i < 8 * 8; ++i) {
        src[i] = (i < 8) ? ((i < 4) ? 130 : 126) : 128;
    }
    CHECK(analysis::getSbyPerpixelVariance8x8(src, 8) == 1);
}

TEST_CASE("isScreenContent classes a checkerboard frame screen at every class") {
    // 1:1 target: svt_aom_is_screen_content (pic_analysis_process.c:1354-1429).
    // TWO passes: 16x16 (color_thresh 4, var_thresh 0) feeding sc_class0..3
    // (:1386-1398), then 8x8 (color_thresh 4, var_thresh 16) feeding
    // sc_class4 (:1427-1428, the 18/20 multipliers). sc_class5 is NOT
    // written by this function (grep-verified :1386-1428); the port's
    // zero-init false there is named. Reference outputs are the
    // pcs->sc_class0..5 uint8_t fields (pcs.h:972-980); the port returns a
    // struct of bool (the assigned values are 0/1 either way).
    //
    // Synthetic (edge-dense, the verdict-flipping frame): 64x64 at stride
    // 64, checkerboard (x+y)&1 ? 255 : 0. Every 16x16 block: 2 colors ->
    // valid palette; variance: diffs from 128 are -128/+127, sum = -128,
    // sse = 128*16384 + 128*16129 = 4161664, sum^2/256 = 64, var = 4161600,
    // per-pixel (4161600+128)>>8 = 16256 > 0 -> counts_2. counts_1 =
    // counts_2 = 16 of 16 blocks. W*H = 4096:
    //   class0: 16*256*10 = 40960 > 4096; class1: class0 && 16*256*12 =
    //   49152 > 4096; class2/class3 via class1. 8x8 pass: every 8x8 block
    //   2 colors, var = (1040400+32)>>6 = 16256 > 16 -> counts_2 = 64;
    //   class4: 64*64*18 = 73728 > 4096 && 64*64*20 = 81920 > 4096.
    // Every class reads true. The smooth-frame counterpart is the next
    // case.
    std::uint8_t src[64 * 64];
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            src[y * 64 + x] = (std::uint8_t)(((x + y) & 1) ? 255 : 0);
        }
    }
    const analysis::ScClasses classes = analysis::isScreenContent(src, 64, 64, 64);
    CHECK(classes.scClass0);
    CHECK(classes.scClass1);
    CHECK(classes.scClass2);
    CHECK(classes.scClass3);
    CHECK(classes.scClass4);
}

TEST_CASE("isScreenContent classes a smooth ramp frame not screen at every class") {
    // The negative counterpart: 64x64 ramp src[y][x] = (y*16 + x) & 255.
    // Every 16x16 block holds 256 DISTINCT values (y*16+x is injective over
    // [0,15]^2 and the block offset only shifts the multiset mod 256), so
    // is_valid_palette_nb_colors exits at the 5th color -> counts_1 = 0;
    // every 8x8 block holds 64 distinct values (8 disjoint runs of 8, the
    // mod-256 wrap cannot collide: consecutive runs start 16 apart) ->
    // counts_1 = 0 there too. Every class formula gets 0 counts: all five
    // read false. This case's RED evidence is its mutation (the palette
    // test inside the 16x16 pass hardwired true): a mutant that skips the
    // palette gate fills counts_1 = 16 on the ramp and class0 flips true.
    std::uint8_t src[64 * 64];
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            src[y * 64 + x] = (std::uint8_t)((y * 16 + x) & 255);
        }
    }
    const analysis::ScClasses classes = analysis::isScreenContent(src, 64, 64, 64);
    CHECK_FALSE(classes.scClass0);
    CHECK_FALSE(classes.scClass1);
    CHECK_FALSE(classes.scClass2);
    CHECK_FALSE(classes.scClass3);
    CHECK_FALSE(classes.scClass4);
    CHECK_FALSE(classes.scClass5);
}

// AN8 AA-aware fixture: 64x64 at stride 72 (poison 99s fill cols 64-71 and
// rows 64-71 - the collect scan must never read them). 16x16 block grid:
// (0,0) + every block except the three named ones = checkerboard
// ((x+y)&1 ? 255 : 0); block (0,1) = the dilate fixture (all 10s except
// 20/30/40/50 at (0,1)/(0,2)/(1,0)/(1,2) of the block, each 8-adjacent to
// a 10); block (0,2) = the ramp (y*16+x) photo block; block (1,1) = solid
// 10s (neither palette nor photo - the n > 1 caller gate, :1124/:1158).
static void buildAaFixture(std::uint8_t* src) {
    for (int y = 0; y < 71; ++y) {
        for (int x = 0; x < 72; ++x) {
            src[y * 72 + x] = 99;
        }
    }
    for (int by = 0; by < 4; ++by) {
        for (int bx = 0; bx < 4; ++bx) {
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                    const int gy = by * 16 + y;
                    const int gx = bx * 16 + x;
                    std::uint8_t v = (std::uint8_t)(((gx + gy) & 1) ? 255 : 0);
                    if (by == 0 && bx == 1) {
                        // dilate fixture: 252 x 10 + four rare values
                        v = 10;
                        if (y == 0 && x == 1) {
                            v = 20;
                        }
                        if (y == 0 && x == 2) {
                            v = 30;
                        }
                        if (y == 1 && x == 0) {
                            v = 40;
                        }
                        if (y == 1 && x == 2) {
                            v = 50;
                        }
                    } else if (by == 0 && bx == 2) {
                        v = (std::uint8_t)((gy * 16 + gx) & 255);
                    } else if (by == 1 && bx == 1) {
                        v = 10;
                    }
                    src[gy * 72 + gx] = v;
                }
            }
        }
    }
}

TEST_CASE("scAaCollectCounts classifies the mixed fixture with regions and no fast skip") {
    // 1:1 target: svt_aom_sc_AA_collect_counts
    // (pic_analysis_process.c:1088-1190, static) as called for the 16x16
    // pass (:1261-1271: thresh 40/4/6, var_thresh 5, fast false). Per
    // block: count_colors_with_threshold(40) && n > 1 -> simple (n <= 4:
    // palette, +intrabc when var > 5) or complex (dilate with the dominant
    // value, then <= 6 colors && var > 5: palette + intrabc); otherwise
    // photo iff n > 40 (:1157-1161). region_id = ((r >= h2)?2:0) +
    // ((c >= w2)?1:0), w2 = h2 = 32 (:1106-1108).
    //
    // Hand-derived: checkerboard blocks (14 of 16) -> palette + intrabc
    // (2 colors <= 4, var 16256 > 5); the dilate block -> initial n = 5
    // (10/20/30/40/50) -> complex -> dilated to all 10s -> n = 1 <= 6 and
    // var: sum = -30108, sse = 3543596, sum^2/256 = 3540983, var = 2613,
    // per-pixel (2613+128)>>8 = 10 > 5 -> palette + intrabc; the ramp
    // block -> n = 41 > 40 -> photo; the solid block -> n = 1, not > 1,
    // not > 40 -> NOTHING. counts: palette = intrabc = 14, photo = 1.
    // Regions (block grid 4x4): region 0 holds (0,0) cb, (0,1) dilate,
    // (16,0) cb, (1,1) solid -> palette 3; region 1 holds the photo block
    // (0,2) -> palette 3, photo 1; regions 2/3 all cb -> palette 4 each.
    std::uint8_t src[71 * 72];
    buildAaFixture(src);
    std::uint8_t dilated[16 * 16];
    const analysis::ScAaCounts counts =
        analysis::scAaCollectCounts(src, 72, 64, 64, 16, 16, 40, 4, 6, 5, false, dilated);
    CHECK(counts.countPalette == 14);
    CHECK(counts.countIntrabc == 14);
    CHECK(counts.countPhoto == 1);
    CHECK(counts.regionPalette[0] == 3);
    CHECK(counts.regionPalette[1] == 3);
    CHECK(counts.regionPalette[2] == 4);
    CHECK(counts.regionPalette[3] == 4);
    CHECK(counts.regionPhoto[0] == 0);
    CHECK(counts.regionPhoto[1] == 1);
    CHECK(counts.regionPhoto[2] == 0);
    CHECK(counts.regionPhoto[3] == 0);
    CHECK(counts.regionIntrabc[0] == 3);
    CHECK(counts.regionIntrabc[1] == 3);
    CHECK(counts.regionIntrabc[2] == 4);
    CHECK(counts.regionIntrabc[3] == 4);
}

TEST_CASE("scAaCollectCounts fast detection halves the visits and doubles the counts") {
    // The fast-detection mechanics (:1096-1104, :1177-1187): multiplier 2;
    // odd block rows start at column blk_w and BOTH loops step 2*blk_w -
    // a checkerboard of visits (r=0: c {0,32}; r=16: c {16,48}; ...); the
    // counts then multiply by 2 (regions too, :1182-1186). Hand-derived on
    // the same fixture: visited = (0,0) cb, (0,32) photo, (16,16) solid,
    // (16,48) cb, (32,0) cb, (32,32) cb, (48,16) cb, (48,48) cb ->
    // palette = intrabc = 6, photo = 1; regions pre-multiplier: palette
    // {1,1,2,2} (regions 2/3 hold TWO visited cb blocks each - (32,0)/
    // (48,16) and (32,32)/(48,48)), photo {0,1,0,0} - after x2: palette/
    // intrabc 12, photo 2, regions {2,2,4,4} / {0,2,0,0}. (The first
    // write of this case asserted {4,4,4,4}/{0,2,0,2} - a hand-derivation
    // error caught by the run; the implementation was not touched.)
    std::uint8_t src[71 * 72];
    buildAaFixture(src);
    std::uint8_t dilated[16 * 16];
    const analysis::ScAaCounts counts =
        analysis::scAaCollectCounts(src, 72, 64, 64, 16, 16, 40, 4, 6, 5, true, dilated);
    CHECK(counts.countPalette == 12);
    CHECK(counts.countIntrabc == 12);
    CHECK(counts.countPhoto == 2);
    CHECK(counts.regionPalette[0] == 2);
    CHECK(counts.regionPalette[1] == 2);
    CHECK(counts.regionPalette[2] == 4);
    CHECK(counts.regionPalette[3] == 4);
    CHECK(counts.regionPhoto[1] == 2);
    CHECK(counts.regionPhoto[3] == 0);
}

TEST_CASE("isScreenContentAaAware classes the checkerboard frame screen at every class") {
    // 1:1 target: svt_aom_is_screen_content_antialiasing_aware
    // (pic_analysis_process.c:1207-1350): two sc_AA_collect_counts calls -
    // 16x16 with complex_final 6 / var_thresh 5 (:1261-1271) and 8x8 with
    // complex_final_8 = 8 / var_thresh_8 = 50 (:1277-1290) - then the
    // class formulas (:1297-1321): class0 = (palette16 - photo16/16)*256*10
    // > area; class1 = class0 && (intrabc16 - photo16/16)*256*12 > area;
    // class2 = class1 || (palette16*256*15 > 4*area && intrabc16*256*30 >
    // area); class3 = class1 || (palette16*256*8 > area &&
    // intrabc16*256*50 > area); region_area = area >> 2; a region passes
    // when regionPalette*64*10 > regionArea && regionIntrabc*64*25 >
    // regionArea; class4 = pass >= 3 && palette8*64*5 > area; class5 =
    // pass >= 3 && palette8*64*10 > area && intrabc8*64*23 > area.
    // blk_area16/8 are the :1208-1215 enum values 256/64.
    //
    // Checkerboard 64x64 (fast_detection false): every 16x16 block simple
    // palette + intrabc (2 colors <= 4, var 16256 > 5) -> palette16 =
    // intrabc16 = 16, photo16 = 0; every 8x8 the same at var_thresh 50 ->
    // palette8 = intrabc8 = 64, all four regions hold 16 palette + 16
    // intrabc 8x8 blocks -> 16*640 = 10240 > 1024 and 16*1600 = 25600 >
    // 1024 -> pass = 4. class0: 16*2560 = 40960 > 4096; class1: 16*3072 =
    // 49152 > 4096; class2/3 via class1; class4: 4 >= 3 && 64*320 = 20480
    // > 4096; class5: 4 >= 3 && 40960 > 4096 && 64*1472 = 94208 > 4096.
    // ALL SIX true.
    std::uint8_t src[64 * 64];
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            src[y * 64 + x] = (std::uint8_t)(((x + y) & 1) ? 255 : 0);
        }
    }
    const analysis::ScClasses classes = analysis::isScreenContentAaAware(src, 64, 64, 64, false);
    CHECK(classes.scClass0);
    CHECK(classes.scClass1);
    CHECK(classes.scClass2);
    CHECK(classes.scClass3);
    CHECK(classes.scClass4);
    CHECK(classes.scClass5);
}

TEST_CASE("isScreenContentAaAware classes the smooth ramp frame not screen") {
    // The negative counterpart on the ramp (256 distinct values per 16x16
    // block, 64 per 8x8): every block is PHOTO (count_colors_with_threshold
    // exits at 41 > 40, :1157-1161) -> palette16 = intrabc16 = 0,
    // photo16 = 16; palette8 = intrabc8 = 0. class0 = (0 - 16/16)*2560 =
    // -2560 > 4096 FALSE; class1/2/3 false; the 8x8 regions hold 0 palette
    // -> pass = 0 -> class4/class5 false. ALL SIX false.
    std::uint8_t src[64 * 64];
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            src[y * 64 + x] = (std::uint8_t)((y * 16 + x) & 255);
        }
    }
    const analysis::ScClasses classes = analysis::isScreenContentAaAware(src, 64, 64, 64, false);
    CHECK_FALSE(classes.scClass0);
    CHECK_FALSE(classes.scClass1);
    CHECK_FALSE(classes.scClass2);
    CHECK_FALSE(classes.scClass3);
    CHECK_FALSE(classes.scClass4);
    CHECK_FALSE(classes.scClass5);
}

TEST_CASE("isInputLumaDominant accepts a fully neutral chroma frame") {
    // 1:1 target: svt_aom_is_input_luma_dominant
    // (third_party/SVT-AV1/Source/Lib/Codec/pic_analysis_process.c:1440-1493)
    // with the constants :1431-1438: FRAME_LUMA_DOMINANT_SAMPLE_STEP 8,
    // CORE_THR 16 (core_thr_sq 256), TAIL_THR 18 (324), MIN_CORE_PCT 85,
    // MIN_TAIL_PCT 95, NEUTRAL_THR 6, UV_DIFF_THR 4, MIN_NEUTRAL_PCT 75.
    // The live caller (pd_process.c:4813) passes pcs->
    // chroma_downsampled_pic; the port threads the chroma planes it needs
    // (u/v pointers + strides + the picture width/height the reference
    // reads). The reference's null/EB_YUV400/u_buffer/v_buffer guard
    // (:1441-1443) becomes the monochrome flag - the port cannot receive
    // a null buffer. Sampling: every 8th chroma pixel over
    // (width>>1) x (height>>1), du = u-128, dv = v-128, uv = u-v;
    // chroma_mag_sq = du*du + dv*dv (uint32); core when <= 256, tail when
    // <= 324, neutral when |du| <= 6 && |dv| <= 6 && |uv| <= 4; return
    // sample_cnt && core*100 >= sample*85 && (tail*100 >= sample*95 ||
    // neutral*100 >= sample*75) (:1490-1492).
    //
    // Luma-heavy fixture: u = v = 128 everywhere -> every sample neutral
    // (and core, and tail): 80x64 -> uv grid 40x32, samples at
    // {0,8,16,24,32} x {0,8,16,24} = 20; core = tail = neutral = 20:
    // core 2000 >= 1700, tail 2000 >= 1900 -> true.
    std::uint8_t u[32 * 48];
    std::uint8_t v[32 * 48];
    for (int i = 0; i < 32 * 48; ++i) {
        u[i] = 128;
        v[i] = 128;
    }
    CHECK(analysis::isInputLumaDominant(u, v, 48, 48, 80, 64, false));
}

TEST_CASE("isInputLumaDominant rejects a chroma-heavy frame") {
    // The negative counterpart: u = 200, v = 60 everywhere -> du = 72,
    // dv = -68, uv = 140: chroma_mag_sq = 5184 + 4624 = 9808 > 324, so
    // core = tail = neutral = 0 of 20 samples -> every gate fails. The
    // implementation is complete (slice 1's GREEN wrote the full 1:1
    // function), so this case's RED evidence is its mutation (the
    // >= comparisons flipped to >).
    std::uint8_t u[32 * 48];
    std::uint8_t v[32 * 48];
    for (int i = 0; i < 32 * 48; ++i) {
        u[i] = 200;
        v[i] = 60;
    }
    CHECK_FALSE(analysis::isInputLumaDominant(u, v, 48, 48, 80, 64, false));
}

// AN9 boundary fixture helper: an 80x64 picture -> uv grid 40x32, samples
// at (8i, 8j) for i in 0..4, j in 0..3 = 20 samples. Per-sample classes:
// neutral u=v=128 (mag 0); core-only u=140,v=128 (du 12, mag 144 <= 256,
// |du| > 6 so NOT neutral); tail-only u=145,v=128 (du 17, mag 289 in
// (256, 324]); outside u=200,v=100 (mag 5968 > 324). The classes nest:
// neutral -> core -> tail.
static void buildLumaDominantFixture(std::uint8_t* u, std::uint8_t* v, int uStride, int vStride,
                                     const int* coreOnly, int coreOnlyN,
                                     const int* tailOnly, int tailOnlyN,
                                     const int* outside, int outsideN) {
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 48; ++x) {
            u[y * uStride + x] = 128;
            v[y * vStride + x] = 128;
        }
    }
    for (int i = 0; i < coreOnlyN; ++i) {
        const int sx = (coreOnly[i] % 5) * 8;
        const int sy = (coreOnly[i] / 5) * 8;
        u[sy * uStride + sx] = 140;
    }
    for (int i = 0; i < tailOnlyN; ++i) {
        const int sx = (tailOnly[i] % 5) * 8;
        const int sy = (tailOnly[i] / 5) * 8;
        u[sy * uStride + sx] = 145;
    }
    for (int i = 0; i < outsideN; ++i) {
        const int sx = (outside[i] % 5) * 8;
        const int sy = (outside[i] / 5) * 8;
        u[sy * uStride + sx] = 200;
        v[sy * vStride + sx] = 100;
    }
}

TEST_CASE("isInputLumaDominant: core at exactly 85 pct with tail and neutral short is FALSE") {
    // Boundary fixture A (the implementation is complete; this case's RED
    // evidence is its mutation - the neutral threshold lowered to 65 makes
    // this TRUE). 14 neutral + 3 core-only + 1 tail-only + 2 outside:
    // core = 17 of 20 -> 17*100 = 1700 >= 20*85 = 1700 (the boundary is
    // INCLUSIVE, :1490); tail = 18 -> 1800 >= 1900 FALSE (90% < 95%);
    // neutral = 14 -> 1400 >= 1500 FALSE (70% < 75%). Verdict FALSE: a
    // mutant dropping the tail-or-neutral requirement (or loosening
    // either threshold) flips this to TRUE.
    std::uint8_t u[32 * 48];
    std::uint8_t v[32 * 48];
    static const int kCoreOnly[3] = {0, 1, 2};
    static const int kTailOnly[1] = {3};
    static const int kOutside[2] = {4, 5};
    buildLumaDominantFixture(u, v, 48, 48, kCoreOnly, 3, kTailOnly, 1, kOutside, 2);
    CHECK_FALSE(analysis::isInputLumaDominant(u, v, 48, 48, 80, 64, false));
}

TEST_CASE("isInputLumaDominant: neutral at exactly 75 pct with core at exactly 85 pct is TRUE") {
    // Boundary fixture B: 15 neutral + 2 core-only + 1 tail-only + 2
    // outside -> core = 17 of 20 (85%, INCLUSIVE: 1700 >= 1700), tail =
    // 18 (90%, fails), neutral = 15 (75%, INCLUSIVE: 1500 >= 1500) ->
    // TRUE via the :1491-1492 neutral branch. Both inclusive boundaries
    // are load-bearing here: this case's mutation (>= -> > in the return)
    // flips it to FALSE at EITHER boundary.
    std::uint8_t u[32 * 48];
    std::uint8_t v[32 * 48];
    static const int kCoreOnly[2] = {0, 1};
    static const int kTailOnly[1] = {2};
    static const int kOutside[2] = {3, 4};
    buildLumaDominantFixture(u, v, 48, 48, kCoreOnly, 2, kTailOnly, 1, kOutside, 2);
    CHECK(analysis::isInputLumaDominant(u, v, 48, 48, 80, 64, false));
}

TEST_CASE("isInputLumaDominant: the core gate is necessary even with a passing tail") {
    // Boundary fixture C: 14 neutral + 2 core-only + 4 tail-only + 0
    // outside -> core = 16 of 20 (80% < 85%: the :1490 core gate FAILS),
    // tail = 20 of 20 (100% >= 95%: the tail branch PASSES). The verdict
    // must still be FALSE - the core gate is a NECESSARY conjunct, not
    // another alternative. This case's mutation: the core conjunct
    // dropped -> TRUE.
    std::uint8_t u[32 * 48];
    std::uint8_t v[32 * 48];
    static const int kCoreOnly[2] = {0, 1};
    static const int kTailOnly[4] = {2, 3, 4, 5};
    buildLumaDominantFixture(u, v, 48, 48, kCoreOnly, 2, kTailOnly, 4, nullptr, 0);
    CHECK_FALSE(analysis::isInputLumaDominant(u, v, 48, 48, 80, 64, false));
}

TEST_CASE("isInputLumaDominant: the EB_YUV400 guard returns false without sampling") {
    // The reference's :1441-1443 guard: color_format == EB_YUV400 ->
    // false BEFORE any sampling (threaded here as the monochrome flag).
    // On the fully-neutral chroma planes the sampler itself would return
    // true, so this case's mutation (the guard neutralized) is caught:
    // the port must not fall through to the sampling.
    std::uint8_t u[32 * 48];
    std::uint8_t v[32 * 48];
    for (int i = 0; i < 32 * 48; ++i) {
        u[i] = 128;
        v[i] = 128;
    }
    CHECK_FALSE(analysis::isInputLumaDominant(u, v, 48, 48, 80, 64, true));
}

TEST_CASE("isInputLumaDominant: a one-pixel-wide picture has no chroma samples") {
    // The :1448-1450 guard: uv_w = width >> 1 == 0 -> false. (Removing
    // the guard is behavior-preserving here - the empty sample loop also
    // yields false via the sample_cnt conjunct - so this case pins the
    // observable behavior, not the guard's existence.)
    std::uint8_t u[8 * 8];
    std::uint8_t v[8 * 8];
    for (int i = 0; i < 8 * 8; ++i) {
        u[i] = 200;
        v[i] = 60;
    }
    CHECK_FALSE(analysis::isInputLumaDominant(u, v, 8, 8, 1, 64, false));
}

TEST_CASE("calculateHistogram decimates rows and columns by decim_step") {
    // Both loops step by decim_step (:179-:180): at step 2 on a 4x4 the
    // sampled pixels are (0,0), (0,2), (2,0), (2,2). Fixture src[y][x] =
    // 10y + x, stride 4: sampled values 0, 2, 20, 22 - hand-derived:
    // sum of bins = 4, histogram[2] = histogram[20] = histogram[22] = 1,
    // the odd columns' bins (1, 3, 21, 23) stay 0, *sum = 0+2+20+22 = 44.
    // An inner-loop mutant that ignores the decimation (hi += 1) samples 8
    // pixels with *sum = 92 and fills bins 0..3 and 20..23, so the binSum,
    // histogram[22] and *sum assertions all kill it.
    std::uint8_t src[4 * 4] = {0};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            src[y * 4 + x] = (std::uint8_t)(y * 10 + x);
        }
    }
    std::uint32_t histogram[256] = {0};
    std::uint64_t sum = 0;
    analysis::calculateHistogram(src, 4, 4, 4, 2, histogram, &sum);
    std::uint32_t binSum = 0;
    for (int b = 0; b < 256; ++b) {
        binSum += histogram[b];
    }
    CHECK(binSum == 4);
    CHECK(histogram[2] == 1);
    CHECK(histogram[20] == 1);
    CHECK(histogram[22] == 1);
    CHECK(histogram[1] == 0);
    CHECK(sum == 44);
}

// AN20 S1: the FIRSTPASS_STATS struct (firstpass.h:30-50) + the pure
// accumulation pair svt_av1_twopass_zero_stats / svt_av1_accumulate_stats
// (firstpass.c:133-146). The struct: double frame, coded_error, duration,
// count + StatStruct{uint64 poc, total_num_bits; uint8 qindex,
// worst_qindex, temporal_layer_index} (definitions.h:2155-2161). The
// output_stats TWO_PASS wall (firstpass.c:83) stays parked.
TEST_CASE("twopassZeroStats zeroes frame coded_error count and sets duration 1.0") {
    analysis::FirstpassStats s{};
    s.frame = 5.0;
    s.codedError = 6.0;
    s.count = 7.0;
    s.duration = 99.0;
    analysis::twopassZeroStats(&s);
    CHECK_EQ(s.frame, 0.0);
    CHECK_EQ(s.codedError, 0.0);
    CHECK_EQ(s.count, 0.0);
    CHECK_EQ(s.duration, 1.0);
}

// AN20 S2: the memset(&section->stat_struct, 0, sizeof(StatStruct)) tail
// (firstpass.c:138) - every StatStruct field goes to zero, not just the
// doubles.
TEST_CASE("twopassZeroStats clears the stat_struct fields") {
    analysis::FirstpassStats s{};
    s.statStruct.poc = 9;
    s.statStruct.totalNumBits = 10;
    s.statStruct.qindex = 11;
    s.statStruct.worstQindex = 12;
    s.statStruct.temporalLayerIndex = 13;
    analysis::twopassZeroStats(&s);
    CHECK_EQ(s.statStruct.poc + s.statStruct.totalNumBits + s.statStruct.qindex +
                 s.statStruct.worstQindex + s.statStruct.temporalLayerIndex,
             0);
}

// AN20 S3: svt_av1_accumulate_stats adds frame->frame into
// section->frame (firstpass.c:142).
TEST_CASE("accumulateStats adds the frame field") {
    analysis::FirstpassStats section{};
    section.frame = 1.5;
    analysis::FirstpassStats frame{};
    frame.frame = 2.25;
    analysis::accumulateStats(&section, &frame);
    CHECK_EQ(section.frame, 3.75);
}

// AN20 S4: the remaining three accumulations (firstpass.c:143-145):
// coded_error += , count += , duration += - the two-entry fold. The
// stat_struct stays untouched (M6's omission is caught here).
TEST_CASE("accumulateStats adds coded_error count and duration") {
    analysis::FirstpassStats section{};
    section.codedError = 0.5;
    section.count = 1.0;
    section.duration = 1.0;
    analysis::FirstpassStats frame{};
    frame.codedError = 0.25;
    frame.count = 1.0;
    frame.duration = 2.0;
    frame.statStruct.poc = 77;
    analysis::accumulateStats(&section, &frame);
    CHECK_EQ(section.codedError, 0.75);
    CHECK_EQ(section.count, 2.0);
    CHECK_EQ(section.duration, 3.0);
    CHECK_EQ(section.statStruct.poc, 0);
}

// AM1 FLOAT TOLERANCE POLICY (stated once for this file, the AN12
// EPSILON precedent): the tier-map calculators return exact float
// literals and hold equality under doctest::Approx; the two arithmetic
// float results (qp amount at qp 34, gate mid-zone t) use an inline
// 1e-6 RELATIVE band - tighter than the 1e-4 ABSOLUTE band used for the
// temporal helpers in test_temporal.cpp, since this file's float graphs
// are short closed forms with no accumulation noise.
//
// AM1 V1: vmaf_get_spatial_amount (pic_analysis_process.c:1642-1652):
// MAD-tier caps. First tier: avg_mad < 2 -> 0.15f.
TEST_CASE("vmafGetSpatialAmount lowest tier is 0.15") {
    CHECK_EQ(analysis::vmafGetSpatialAmount(0), doctest::Approx(0.15f));
}

// AM1 V2: the FULL tier enumeration over the uint32 range
// (pic_analysis_process.c:1643-1651): < 2 -> 0.15, < 5 -> 0.22,
// < 12 -> 0.28, else 0.30. Every boundary is pinned, both sides.
TEST_CASE("vmafGetSpatialAmount tier enumeration over the full mad range") {
    const std::uint32_t mad[8] = {0, 1, 2, 4, 5, 11, 12, 1000000};
    const float expect[8] = {0.15f, 0.15f, 0.22f, 0.22f, 0.28f, 0.28f, 0.30f, 0.30f};
    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(analysis::vmafGetSpatialAmount(mad[i]), doctest::Approx(expect[i]));
    }
}

// AM1 V3: vmaf_get_qp_amount (pic_analysis_process.c:1654-1659): qp = 0
// is full strength 0.5f.
TEST_CASE("vmafGetQpAmount zero qp is 0.5") {
    CHECK_EQ(analysis::vmafGetQpAmount(0), doctest::Approx(0.5f));
}

// AM1 V4: the FULL qp range enumeration (pic_analysis_process.c:1655-1658):
// qp >= 35 holds the 0.3 floor (35 and far above), qp < 34 + the formula
// 0.5 - (qp/35)*0.2 at qp = 34 (0.30571429, the 1e-4 AM1 band), 0 at the
// top (0.5). The range 0..255 is covered by its three regimes.
TEST_CASE("vmafGetQpAmount full range enumeration") {
    CHECK_EQ(analysis::vmafGetQpAmount(0), doctest::Approx(0.5f));
    CHECK_EQ(analysis::vmafGetQpAmount(34), doctest::Approx(0.30571429f).epsilon(1e-6));
    CHECK_EQ(analysis::vmafGetQpAmount(35), doctest::Approx(0.3f));
    CHECK_EQ(analysis::vmafGetQpAmount(51), doctest::Approx(0.3f));
}

// AM1 V5: vmaf_get_coherence_factor (pic_analysis_process.c:1661-1669):
// gcoh < 0.40 -> 0.80, < 0.60 -> 0.9, else 1.0. The three-tier map is
// one behavior; the boundaries both sides are pinned.
TEST_CASE("vmafGetCoherenceFactor tier map over the full gcoh range") {
    CHECK_EQ(analysis::vmafGetCoherenceFactor(0.2f), doctest::Approx(0.80f));
    CHECK_EQ(analysis::vmafGetCoherenceFactor(0.39f), doctest::Approx(0.80f));
    CHECK_EQ(analysis::vmafGetCoherenceFactor(0.40f), doctest::Approx(0.9f));
    CHECK_EQ(analysis::vmafGetCoherenceFactor(0.5f), doctest::Approx(0.9f));
    CHECK_EQ(analysis::vmafGetCoherenceFactor(0.59f), doctest::Approx(0.9f));
    CHECK_EQ(analysis::vmafGetCoherenceFactor(0.60f), doctest::Approx(1.0f));
    CHECK_EQ(analysis::vmafGetCoherenceFactor(0.9f), doctest::Approx(1.0f));
}

// AM1 V6: vmaf_compute_combined_amount (pic_analysis_process.c:1671-1678).
// The pcs->scs->static_config.qp wall is threaded as the baseQp param
// (named deviation). combined = ((per_qp + spatial) / 2) * coh_factor.
TEST_CASE("vmafComputeCombinedAmount blends qp spatial and coherence") {
    // qp 0 -> 0.5, mad 0 -> 0.15, gcoh 1.0 -> 1.0: (0.65)/2 = 0.325.
    CHECK_EQ(analysis::vmafComputeCombinedAmount(0, 0, 1.0f), doctest::Approx(0.325f));
    // the coherence factor scales: gcoh 0.2 -> 0.325 * 0.8 = 0.26.
    CHECK_EQ(analysis::vmafComputeCombinedAmount(0, 0, 0.2f), doctest::Approx(0.26f));
    // qp 35 -> 0.3, mad 12 -> 0.30, gcoh 0.5 -> 0.9: (0.6)/2 * 0.9 = 0.27.
    CHECK_EQ(analysis::vmafComputeCombinedAmount(35, 12, 0.5f), doctest::Approx(0.27f));
}

// AM1 V7: vmaf_get_delta_clip (pic_analysis_process.c:1741-1754): the
// QP-budget tiers, tightened by 4 on busy frames.
TEST_CASE("vmafGetDeltaClip low qp not busy is 8") {
    CHECK_EQ(analysis::vmafGetDeltaClip(0, 0), 8);
}

// AM1 V8: the FULL delta-clip enumeration over the qp range
// (pic_analysis_process.c:1743-1753): <= 42 -> 8, <= 51 -> 9,
// <= 57 -> 10, else 12; busy subtracts 4 (both boolean cases, every
// boundary both sides).
TEST_CASE("vmafGetDeltaClip full qp and busy enumeration") {
    const std::int32_t qp[7] = {0, 42, 43, 51, 52, 57, 58};
    const std::int32_t base[7] = {8, 8, 9, 9, 10, 10, 12};
    for (int i = 0; i < 7; ++i) {
        CHECK_EQ(analysis::vmafGetDeltaClip(qp[i], 0), base[i]);
        CHECK_EQ(analysis::vmafGetDeltaClip(qp[i], 1), base[i] - 4);
    }
}

// AM1 V9: vmaf_get_noise_gate (pic_analysis_process.c:1701-1726). The
// pcs->enhanced_pic wall is threaded as (y, width, height, stride)
// (named deviation). Noise via the temporal:: estimator chain
// (noiseLog1pFp16 is the LANDED temporal_filtering.c:568-586 port), then
// the gate: <= 40000 -> 1.0, >= 80000 -> 0.3, else the linear 1 - t*0.7.
TEST_CASE("vmafGetNoiseGate unreliable clean and noisy branches") {
    // (a) 4x4 plane: interior 2x2 < SMOOTH_THRESHOLD -> noise -65536 < 0
    // -> gate 1.0 (no reduction).
    std::uint8_t tiny[4 * 4];
    for (int i = 0; i < 16; ++i) {
        tiny[i] = 100;
    }
    tiny[1 * 4 + 1] = 150;
    CHECK_EQ(analysis::vmafGetNoiseGate(tiny, 4, 4, 4), doctest::Approx(1.0f));
    // (b) clean flat plane: noise 0, log1p 0 <= gate_start -> 1.0.
    std::uint8_t flat[8 * 8];
    for (int i = 0; i < 64; ++i) {
        flat[i] = 100;
    }
    CHECK_EQ(analysis::vmafGetNoiseGate(flat, 8, 8, 8), doctest::Approx(1.0f));
    // (c) mid-zone: the 8x8 delta plane (noise 136895 fp16) has log1p in
    // (40000, 80000); the expected gate replicates the formula from the
    // REAL estimator values (the thresholds and the linear map are what
    // is under test here).
    std::uint8_t src[8 * 8];
    for (int i = 0; i < 64; ++i) {
        src[i] = 100;
    }
    src[2 * 8 + 2] = 110;
    const int32_t nl = analysis::temporal::noiseLog1pFp16(
        analysis::temporal::vmafEstimateNoiseFp16(src, 8, 8, 8));
    CHECK(nl > 40000);
    CHECK(nl < 80000);
    const float t = (float)(nl - 40000) / (float)(80000 - 40000);
    CHECK_EQ(analysis::vmafGetNoiseGate(src, 8, 8, 8),
             doctest::Approx(1.0f - t * (1.0f - 0.3f)).epsilon(1e-6));
    // (d) noisy checkerboard: noise 27926580 fp16 -> log1p 909052 >=
    // gate_end -> the 0.3 floor.
    std::uint8_t cb[8 * 8];
    for (int i = 0; i < 64; ++i) {
        cb[i] = (i + (i / 8)) % 2 ? 255 : 0;
    }
    CHECK_EQ(analysis::vmafGetNoiseGate(cb, 8, 8, 8), doctest::Approx(0.3f));
}

// AM1 V10: vmaf_preprocess_frame (pic_analysis_process.c:1842-1900), the
// driver. Threading (named deviations): the pa_ctx wall - the hring[5]
// and blur_plane buffers and the :1857-1881 grow logic - is caller-owned
// (params); pcs->vmaf_sharpening_amount and pcs->vmaf_max_delta become
// out-params; scs->static_config.qp becomes baseQp. The composition
// ORDER is the behavior under test: amounts -> gate -> blur -> busy
// count on the PRE-unsharp plane -> delta clip -> unsharp in place.
TEST_CASE("vmafPreprocessFrame composition order and outputs") {
    const int w = 8, h = 8, stride = 8;
    std::uint8_t plane[w * h];
    std::uint32_t seed = 12345;
    for (int i = 0; i < w * h; ++i) {
        seed = seed * 1103515245u + 12345u;
        plane[i] = (std::uint8_t)(40 + ((seed >> 16) & 0x3F));
    }
    std::uint8_t orig[w * h];
    for (int i = 0; i < w * h; ++i) {
        orig[i] = plane[i];
    }
    std::int16_t ring0[w + 4], ring1[w + 4], ring2[w + 4], ring3[w + 4], ring4[w + 4];
    std::int16_t* hring[5] = {ring0, ring1, ring2, ring3, ring4};
    std::uint8_t blurPlane[w * h];
    int sharpAmount = -1;
    std::int32_t deltaClip = -1;
    const std::uint32_t qp = 0;
    analysis::vmafPreprocessFrame(plane, stride, w, h, hring, blurPlane, qp, &sharpAmount, &deltaClip);

    // expected sharp: the landed calculator chain (each piece pinned by
    // V1-V9/T1-T10) + the reference's two int casts.
    const std::uint32_t mad = analysis::temporal::vmafComputeAvgMad(plane, w, h, stride);
    const float gcoh = analysis::temporal::vmafComputeGradientCoherence(plane, w, h, stride);
    int expectSharp = (int)(analysis::vmafComputeCombinedAmount(qp, mad, gcoh) * 32768.0f);
    expectSharp = (int)(expectSharp * analysis::vmafGetNoiseGate(plane, w, h, stride));
    CHECK_EQ(sharpAmount, expectSharp);

    // expected busy/clip: the count runs on the PRE-unsharp plane vs the
    // blur plane (pic_analysis_process.c:1888-1896, threshold 12, target
    // = count * 85 / 100).
    std::uint8_t expectBlur[w * h];
    analysis::temporal::vmafBoxBlurFrame(orig, stride, expectBlur, w, h, hring);
    const std::uint32_t flat = analysis::temporal::vmafCountDetailLe(orig, expectBlur, w, h, stride, 12);
    const int busy = (flat < (std::uint32_t)(w * h) * 85 / 100) ? 1 : 0;
    CHECK_EQ(deltaClip, analysis::vmafGetDeltaClip((std::int32_t)qp, busy));

    // expected final plane: unsharp with the gated amount and the clip.
    std::uint8_t expectPlane[w * h];
    for (int i = 0; i < w * h; ++i) {
        expectPlane[i] = orig[i];
    }
    for (int y = 0; y < h; ++y) {
        analysis::temporal::vmafApplyUnsharpRow(expectPlane + y * stride, expectBlur + y * w,
                                                expectPlane + y * stride, w, expectSharp, deltaClip);
    }
    for (int i = 0; i < w * h; ++i) {
        CHECK_EQ(plane[i], expectPlane[i]);
    }

    // second case: the T10 delta plane has mid-zone noise (log1p ~73866,
    // gate ~0.41), so the gate application is observable in sharpAmount.
    std::uint8_t plane2[w * h];
    for (int i = 0; i < w * h; ++i) {
        plane2[i] = 100;
    }
    plane2[2 * w + 2] = 110;
    std::uint8_t orig2[w * h];
    for (int i = 0; i < w * h; ++i) {
        orig2[i] = plane2[i];
    }
    std::uint8_t blurPlane2[w * h];
    int sharpAmount2 = -1;
    std::int32_t deltaClip2 = -1;
    analysis::vmafPreprocessFrame(plane2, stride, w, h, hring, blurPlane2, qp, &sharpAmount2, &deltaClip2);
    int expectSharp2 = (int)(analysis::vmafComputeCombinedAmount(
                                 qp, analysis::temporal::vmafComputeAvgMad(orig2, w, h, stride),
                                 analysis::temporal::vmafComputeGradientCoherence(orig2, w, h, stride)) *
                             32768.0f);
    expectSharp2 = (int)(expectSharp2 * analysis::vmafGetNoiseGate(orig2, w, h, stride));
    CHECK(expectSharp2 < (int)(analysis::vmafComputeCombinedAmount(
                                   qp, analysis::temporal::vmafComputeAvgMad(orig2, w, h, stride),
                                   analysis::temporal::vmafComputeGradientCoherence(orig2, w, h, stride)) *
                               32768.0f));
    CHECK_EQ(sharpAmount2, expectSharp2);
}

// AM1 P1: sub_sample_luma_generate_pixel_intensity_histogram_bins
// (pic_analysis_process.c:527-589), the scs/pcs walls threaded (named
// deviations): the region counts + scene_change_detection become
// params, the pcs->picture_histogram[4][4][256] and
// average_intensity_per_region[4][4] become caller-owned arrays, the
// EbPictureBufferDesc wall becomes (y, width, height, stride). Single
// region covering a 4x4 plane at decim 1 (scene-change mode), uniform
// 50: bins init to 1 then the AN5 histogram adds; the rescale multiplies
// every bin by 16*decim^2; avg = (sum + area/2) / area as uint8; the
// total sum is rescaled by decim^2 per region then divided by w*h.
TEST_CASE("subSampleLumaHistogramBins single region decim 1 uniform plane") {
    std::uint8_t plane[4 * 4];
    for (int i = 0; i < 16; ++i) {
        plane[i] = 50;
    }
    std::uint32_t* histograms[4][4] = {};
    std::uint32_t bins[256];
    histograms[0][0] = bins;
    std::uint64_t avgIntensity[4][4] = {};
    std::uint64_t sumAvg = 777;
    analysis::subSampleLumaHistogramBins(1, 1, true, plane, 4, 4, 4, histograms, avgIntensity, &sumAvg);
    // bin 50: 1 + 16 samples = 17, rescaled *16 -> 272; all others 16.
    CHECK_EQ(bins[50], 272u);
    CHECK_EQ(bins[0], 16u);
    CHECK_EQ(bins[255], 16u);
    // sum = 16 samples * 50 = 800; avg = (800 + (16 >> 1)) / 16 = 50
    // (uint8 narrowing, the reference's).
    CHECK_EQ(avgIntensity[0][0], 50u);
    // total: 800 * 1 / 16 = 50.
    CHECK_EQ(sumAvg, 50u);
}

// AM1 P2: the multi-region grid with the LAST-REGION remainder offsets
// (:549-557) and decim 4 (scene_change_detection false): 2x2 regions on
// a 5x5 plane (values y*10+x) -> region dims 2x2 plus width/height
// offset 1 on the last row/col region. decim 4 samples only each
// region's top-left pixel. Hand-derived: sums 0/2/20/22 sampled, x16;
// avgs (0+2)/4=0, (32+3)/6=5, (320+3)/6=53, (352+4)/9=39; total
// 704/25 = 28.
TEST_CASE("subSampleLumaHistogramBins region grid remainder offsets and decim 4") {
    std::uint8_t plane[5 * 5];
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            plane[y * 5 + x] = (std::uint8_t)(y * 10 + x);
        }
    }
    std::uint32_t* histograms[4][4] = {};
    std::uint32_t bins[4][256];
    histograms[0][0] = bins[0];
    histograms[1][0] = bins[1];
    histograms[0][1] = bins[2];
    histograms[1][1] = bins[3];
    std::uint64_t avgIntensity[4][4] = {};
    std::uint64_t sumAvg = 0;
    analysis::subSampleLumaHistogramBins(2, 2, false, plane, 5, 5, 5, histograms, avgIntensity, &sumAvg);
    CHECK_EQ(avgIntensity[0][0], 0u);
    CHECK_EQ(avgIntensity[1][0], 5u);
    CHECK_EQ(avgIntensity[0][1], 53u);
    CHECK_EQ(avgIntensity[1][1], 39u);
    CHECK_EQ(sumAvg, 28u);
    // one sampled pixel per region: each touched bin is 2*256 (the
    // rescale literal is * 4 * 4 * decim * decim = 256 at decim 4), the
    // untouched are 256.
    CHECK_EQ(bins[0][0], 512u);
    CHECK_EQ(bins[1][2], 512u);
    CHECK_EQ(bins[2][20], 512u);
    CHECK_EQ(bins[3][22], 512u);
    CHECK_EQ(bins[0][50], 256u);
}

// AM1 P3: compute_picture_spatial_statistics (:597-617). Threading
// (named deviations): pcs->b64_geom[].org_x/org_y become the orgX/orgY
// arrays, pcs->b64_total_count the count param, the EbPictureBufferDesc
// wall becomes (y, yStride), pcs->variance[b64_idx][0] (RASTER_SCAN_
// CU_INDEX_64x64 = 0, definitions.h:2068) - the sbVar rows keep the
// AN7 layout (85 entries, 64x64 at base 0). pic_avg_variance = the
// truncating mean of the 64x64 entries.
TEST_CASE("computePictureSpatialStatistics quadrant plane fold value") {
    // 64x64, four 32x32 quadrants of constants {100, 120, 140, 160}:
    // every 8x8 block is constant, so every fold level is exact (no
    // truncation): the 64x64 mean = 130 << 8 = 33280, the 64x64 meanSq =
    // avg(v^2) << 16 = 17400 << 16 = 1,140,326,400; variance =
    // (1,140,326,400 - 33280^2) >> 16 = (500 << 16) >> 16 = 500 - the
    // exact population variance of {100, 120, 140, 160}.
    std::uint8_t plane[64 * 64];
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const std::uint8_t v[4] = {100, 120, 140, 160};
            const int q = (y >= 32 ? 2 : 0) + (x >= 32 ? 1 : 0);
            plane[y * 64 + x] = v[q];
        }
    }
    std::uint16_t sbVar[85] = {};
    std::uint16_t orgX[1] = {0};
    std::uint16_t orgY[1] = {0};
    std::uint16_t picAvg = 777;
    analysis::computePictureSpatialStatistics(plane, 64, analysis::BlockMeanPrec::Sub,
                                              false, 1, orgX, orgY, sbVar, &picAvg);
    CHECK_EQ(picAvg, 500);
    CHECK_EQ(sbVar[0], 500);
}


// AM1 P4: svt_aom_gathering_picture_statistics (:624-643), the driver.
// Threading: scs->calc_hist / calculate_variance become the two flags;
// the picture-desc walls become the sub-callee params; pcs->avg_luma
// (uint64, pcs.h:802) and pcs->pic_avg_variance become out-params.
// avg_luma = INVALID_LUMA = 256 (definitions.h:93) unless the histogram
// path overwrites it. Both flags false: the else branches.
TEST_CASE("gatheringPictureStatistics both flags false gives INVALID_LUMA and zero variance") {
    std::uint64_t avgLuma = 5;
    std::uint16_t picAvg = 6;
    analysis::gatheringPictureStatistics(false, false,
                                         1, 1, true, nullptr, 0, 0, 0, nullptr, nullptr,
                                         nullptr, 0, analysis::BlockMeanPrec::Sub, false, 0, nullptr, nullptr,
                                         nullptr, &avgLuma, &picAvg);
    CHECK_EQ(avgLuma, 256u);
    CHECK_EQ(picAvg, 0u);
}

// AM1 P5: the driver's histogram path: calc_hist true overwrites
// avg_luma with the regions' mean (P1's 50), the variance else-branch
// still zeroes pic_avg_variance.
TEST_CASE("gatheringPictureStatistics histogram path overwrites avgLuma") {
    std::uint8_t plane[4 * 4];
    for (int i = 0; i < 16; ++i) {
        plane[i] = 50;
    }
    std::uint32_t* histograms[4][4] = {};
    std::uint32_t bins[256];
    histograms[0][0] = bins;
    std::uint64_t avgIntensity[4][4] = {};
    std::uint64_t avgLuma = 5;
    std::uint16_t picAvg = 6;
    analysis::gatheringPictureStatistics(true, false,
                                         1, 1, true, plane, 4, 4, 4, histograms, avgIntensity,
                                         nullptr, 0, analysis::BlockMeanPrec::Sub, false, 0, nullptr, nullptr,
                                         nullptr, &avgLuma, &picAvg);
    CHECK_EQ(avgLuma, 50u);
    CHECK_EQ(picAvg, 0u);
    // and the histogram bins themselves flow through (the P1 value):
    CHECK_EQ(bins[50], 272u);
}
