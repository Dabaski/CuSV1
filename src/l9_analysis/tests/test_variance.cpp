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
