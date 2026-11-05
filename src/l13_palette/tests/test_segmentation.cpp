#include <cstdio>
#include <doctest.h>

#include "segmentation.h"

namespace seg = l13_palette::segmentation;

TEST_CASE("l13 segmentation scaffold boots") {
    CHECK(true);
}

TEST_CASE("kSegFeatureSigned over SEG_LVL_MAX (segmentation_params.c:16)") {
    const std::int32_t expect[seg::segLvlMax] = {1, 1, 1, 1, 1, 0, 0, 0};
    for (int j = 0; j < seg::segLvlMax; ++j) {
        CHECK(seg::kSegFeatureSigned[j] == expect[j]);
    }
}

TEST_CASE("kSegFeatureBits over SEG_LVL_MAX (segmentation_params.c:18)") {
    const std::int32_t expect[seg::segLvlMax] = {8, 6, 6, 6, 6, 3, 0, 0};
    for (int j = 0; j < seg::segLvlMax; ++j) {
        CHECK(seg::kSegFeatureBits[j] == expect[j]);
    }
}

TEST_CASE("calculateSegmentationData: one enabled feature sets last_active_seg_id "
          "(segmentation.c:260-271)") {
    seg::SegmentationParams sp = {};
    sp.feature_enabled[3][seg::segLvlAltQ] = 1;
    seg::calculateSegmentationData(&sp);
    CHECK(sp.last_active_seg_id == 3);
}

TEST_CASE("calculateSegmentationData: highest enabled segment id wins; "
          "all-disabled leaves last_active_seg_id untouched") {
    seg::SegmentationParams sp = {};
    sp.feature_enabled[1][seg::segLvlAltLfYV] = 1;
    sp.feature_enabled[6][seg::segLvlAltQ] = 1;
    sp.feature_enabled[4][seg::segLvlAltLfU] = 1;
    seg::calculateSegmentationData(&sp);
    CHECK(sp.last_active_seg_id == 6);

    seg::SegmentationParams sp2 = {};
    sp2.last_active_seg_id = 5;
    seg::calculateSegmentationData(&sp2);
    CHECK(sp2.last_active_seg_id == 5);
}

TEST_CASE("getVarianceForCu: the 8x8-family cases share one index "
          "(segmentation.c:27-32; ME_TIER_ZERO_PU_8x8_0 me_context.h:68 = 21)") {
    // Formula oracle: variance_ptr[i] = 1000 + i, so the result is
    // ((1000+i0) + (1000+i1)) >> 1 with i0 = i1 = 21 + (org_x>>3) + org_y.
    std::uint16_t var[100];
    for (int i = 0; i < 100; ++i) {
        var[i] = static_cast<std::uint16_t>(1000 + i);
    }
    const std::uint16_t expect = static_cast<std::uint16_t>((1000 + 45) + (1000 + 45)) >> 1;
    // org_x = 5 (>>3 = 0), org_y = 24 -> index 21 + 0 + 24 = 45.
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_4X4, 5, 24, var) == expect);
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_4X8, 5, 24, var) == expect);
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_8X4, 5, 24, var) == expect);
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_8X8, 5, 24, var) == expect);
}

TEST_CASE("getVarianceForCu: BLOCK_8X16 pairs the 8x8 window with +1 "
          "(segmentation.c:34-37 - the reference's own pairing, kept 1:1)") {
    std::uint16_t var[100];
    for (int i = 0; i < 100; ++i) {
        var[i] = static_cast<std::uint16_t>(1000 + i);
    }
    // org (8,8): i0 = 21 + 1 + 8 = 30, i1 = i0 + 1 = 31. (orgY > 0 so the
    // +1 is distinguishable from a stray +orgY.)
    const std::uint16_t expect = static_cast<std::uint16_t>(1000 + 30 + 1000 + 31) >> 1;
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_8X16, 8, 8, var) == expect);
}

TEST_CASE("getVarianceForCu: BLOCK_16X8 adds org_y to the second index "
          "(segmentation.c:39-42 - the reference's own pairing, kept 1:1)") {
    std::uint16_t var[100];
    for (int i = 0; i < 100; ++i) {
        var[i] = static_cast<std::uint16_t>(1000 + i);
    }
    // org (0,16): i0 = 21 + 0 + 16 = 37, i1 = i0 + org_y = 37 + 16 = 53.
    const std::uint16_t expect = static_cast<std::uint16_t>(1000 + 37 + 1000 + 53) >> 1;
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_16X8, 0, 16, var) == expect);
}

TEST_CASE("getVarianceForCu: the 16x16-family cases share one index "
          "(segmentation.c:44-48; ME_TIER_ZERO_PU_16x16_0 me_context.h:52 = 5)") {
    std::uint16_t var[100];
    for (int i = 0; i < 100; ++i) {
        var[i] = static_cast<std::uint16_t>(1000 + i);
    }
    // org (17,8): i0 = i1 = 5 + (17>>4) + (8>>2) = 5 + 1 + 2 = 8.
    const std::uint16_t expect = static_cast<std::uint16_t>(1000 + 8 + 1000 + 8) >> 1;
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_4X16, 17, 8, var) == expect);
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_16X4, 17, 8, var) == expect);
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_16X16, 17, 8, var) == expect);
}

TEST_CASE("getVarianceForCu: BLOCK_16X32 pairs the 16x16 window with +1 "
          "(segmentation.c:50-53)") {
    std::uint16_t var[100];
    for (int i = 0; i < 100; ++i) {
        var[i] = static_cast<std::uint16_t>(1000 + i);
    }
    // org (0,0): i0 = 5, i1 = 6.
    const std::uint16_t expect = static_cast<std::uint16_t>(1000 + 5 + 1000 + 6) >> 1;
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_16X32, 0, 0, var) == expect);
}

TEST_CASE("getVarianceForCu: BLOCK_32X16 adds (org_y>>2) to the second index "
          "(segmentation.c:55-58)") {
    std::uint16_t var[100];
    for (int i = 0; i < 100; ++i) {
        var[i] = static_cast<std::uint16_t>(1000 + i);
    }
    // org (0,16): i0 = 5 + 0 + 4 = 9, i1 = i0 + (16>>2) = 9 + 4 = 13.
    const std::uint16_t expect = static_cast<std::uint16_t>(1000 + 9 + 1000 + 13) >> 1;
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_32X16, 0, 16, var) == expect);
}

TEST_CASE("getVarianceForCu: the 32x32-family cases share one index "
          "(segmentation.c:60-64; ME_TIER_ZERO_PU_32x32_0 me_context.h:48 = 1)") {
    std::uint16_t var[100];
    for (int i = 0; i < 100; ++i) {
        var[i] = static_cast<std::uint16_t>(1000 + i);
    }
    // org (33,16): i0 = i1 = 1 + (33>>5) + (16>>4) = 1 + 1 + 1 = 3.
    const std::uint16_t expect = static_cast<std::uint16_t>(1000 + 3 + 1000 + 3) >> 1;
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_8X32, 33, 16, var) == expect);
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_32X8, 33, 16, var) == expect);
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_32X32, 33, 16, var) == expect);
}

TEST_CASE("getVarianceForCu: BLOCK_32X64 pairs the 32x32 window with +1 "
          "(segmentation.c:66-69)") {
    std::uint16_t var[100];
    for (int i = 0; i < 100; ++i) {
        var[i] = static_cast<std::uint16_t>(1000 + i);
    }
    // org (0,0): i0 = 1, i1 = 2.
    const std::uint16_t expect = static_cast<std::uint16_t>(1000 + 1 + 1000 + 2) >> 1;
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_32X64, 0, 0, var) == expect);
}

TEST_CASE("getVarianceForCu: BLOCK_64X32 adds (org_y>>4) to the second index "
          "(segmentation.c:71-74)") {
    std::uint16_t var[100];
    for (int i = 0; i < 100; ++i) {
        var[i] = static_cast<std::uint16_t>(1000 + i);
    }
    // org (0,32): i0 = 1 + 0 + 2 = 3, i1 = i0 + (32>>4) = 3 + 2 = 5.
    const std::uint16_t expect = static_cast<std::uint16_t>(1000 + 3 + 1000 + 5) >> 1;
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_64X32, 0, 32, var) == expect);
}

TEST_CASE("getVarianceForCu: 64x64 and every other size fall to index 0 "
          "(segmentation.c:76-81, the default branch)") {
    std::uint16_t var[100];
    for (int i = 0; i < 100; ++i) {
        var[i] = static_cast<std::uint16_t>(1000 + i);
    }
    // index0 = index1 = 0 -> (1000 + 1000) >> 1 = 1000.
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_64X64, 40, 40, var) == 1000);
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_16X64, 40, 40, var) == 1000);
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_64X16, 40, 40, var) == 1000);
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_64X128, 40, 40, var) == 1000);
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_128X64, 40, 40, var) == 1000);
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_128X128, 40, 40, var) == 1000);
    CHECK(seg::getVarianceForCu(seg::BlockSize::BLOCK_INVALID, 40, 40, var) == 1000);
}

TEST_CASE("findSegmentQps: uniform 4096 variance window - the gather, step 1, "
          "the POW2 uint16 wrap and the delta chain (segmentation.c:273-312)") {
    // All entries 4096 = 2^12: min = max = 4096 -> logs 12/12, diff 0 <= 8
    // -> step_size 1. avg_var = 4096 -> svt_log2f_safe = get_msb(4097) = 12.
    std::uint16_t window[85];
    for (int i = 0; i < 85; ++i) {
        window[i] = 4096;
    }
    const std::uint16_t* windows[1] = {window};
    seg::SegmentationParams sp = {};
    seg::findSegmentQps(&sp, 1, windows);

    // variance_bin_edge[i] = 1 << (min_log + step * (8 - i)), i.e. edge[7]
    // = 1<<13 = 8192 first; 1<<15 wraps to int16 -32768, 1<<16 and above
    // wrap to 0.
    const std::int16_t expectEdge[seg::maxSegments] = {0, 0, 0, 0, 0, -32768, 16384, 8192};
    static_assert(seg::maxSegments == 8, "reference shape");
    for (int i = 0; i < seg::maxSegments; ++i) {
        CHECK(sp.variance_bin_edge[i] == expectEdge[i]);
    }
    // feature_data[i][ALT_Q] = 2 * (MAX(1, bin_center) - avg_log) for
    // bin_center 6..13, avg_log 12 -> {-12,-10,-8,-6,-4,-2,0,2}; the
    // segment-0 clamp does not fire (2 >= 0).
    const std::int16_t expectDelta[seg::maxSegments] = {2, 0, -2, -4, -6, -8, -10, -12};
    for (int i = 0; i < seg::maxSegments; ++i) {
        CHECK(sp.feature_data[i][seg::segLvlAltQ] == expectDelta[i]);
    }
}

TEST_CASE("findSegmentQps: the avoid-lossless clamp zeroes a negative segment-0 delta "
          "(segmentation.c:309-312)") {
    // min 2 -> min_log 1, max 521 -> get_msb(522) = 9, diff 8 <= 8 -> step 1.
    // local_avg = 2 + 63*521 = 32825 -> >>6 = 512, avg_log = get_msb(513) = 9.
    // bin_center 1..8 -> deltas {-16,-14,-12,-10,-8,-6,-4,-2}; segment 0's
    // -2 clamps to 0.
    std::uint16_t window[85] = {};
    window[21] = 2;
    for (int i = 22; i < 85; ++i) {
        window[i] = 521;
    }
    const std::uint16_t* windows[1] = {window};
    seg::SegmentationParams sp = {};
    seg::findSegmentQps(&sp, 1, windows);
    const std::int16_t expectDelta[seg::maxSegments] = {0, -4, -6, -8, -10, -12, -14, -16};
    for (int i = 0; i < seg::maxSegments; ++i) {
        CHECK(sp.feature_data[i][seg::segLvlAltQ] == expectDelta[i]);
    }
    const std::int16_t expectEdge[seg::maxSegments] = {512, 256, 128, 64, 32, 16, 8, 4};
    for (int i = 0; i < seg::maxSegments; ++i) {
        CHECK(sp.variance_bin_edge[i] == expectEdge[i]);
    }
}

TEST_CASE("findSegmentQps: multi-SB average truncation and the all-zero window "
          "(segmentation.c:279-291 gather, :290 division, svt_log2f_safe |1 guard)") {
    // Two windows, all-4 and all-5: local_avg 256->4 and 320->5, mean 4
    // (integer), avg_log = get_msb(5) = 2. min 4 -> 2, max 5 -> get_msb(6) = 2,
    // diff 0 -> step 1, bin_edge 3, bin_center 1.
    std::uint16_t w0[85], w1[85];
    for (int i = 0; i < 85; ++i) {
        w0[i] = 4;
        w1[i] = 5;
    }
    const std::uint16_t* windows[2] = {w0, w1};
    seg::SegmentationParams sp = {};
    seg::findSegmentQps(&sp, 2, windows);
    const std::int16_t expectEdge[seg::maxSegments] = {1024, 512, 256, 128, 64, 32, 16, 8};
    const std::int16_t expectDelta[seg::maxSegments] = {12, 10, 8, 6, 4, 2, 0, -2};
    for (int i = 0; i < seg::maxSegments; ++i) {
        CHECK(sp.variance_bin_edge[i] == expectEdge[i]);
        CHECK(sp.feature_data[i][seg::segLvlAltQ] == expectDelta[i]);
    }

    // All-zero: avg_log = get_msb(0|1) = 0 (the |1 guard), min/max logs 0,
    // bin_edge 1..8, deltas all positive (no clamp).
    std::uint16_t wz[85] = {};
    const std::uint16_t* windowsZ[1] = {wz};
    seg::SegmentationParams spz = {};
    seg::findSegmentQps(&spz, 1, windowsZ);
    const std::int16_t expectEdgeZ[seg::maxSegments] = {256, 128, 64, 32, 16, 8, 4, 2};
    const std::int16_t expectDeltaZ[seg::maxSegments] = {14, 12, 10, 8, 6, 4, 2, 2};
    for (int i = 0; i < seg::maxSegments; ++i) {
        CHECK(spz.variance_bin_edge[i] == expectEdgeZ[i]);
        CHECK(spz.feature_data[i][seg::segLvlAltQ] == expectDeltaZ[i]);
    }
}

TEST_CASE("aqSegmentId: the highest segment whose bin holds the variance and whose "
          "q_index stays positive (segmentation.c:144-157, extracted walk)") {
    // The clamp-fixture params: edges {512,256,128,64,32,16,8,4}, deltas
    // {0,-4,-6,-8,-10,-12,-14,-16}.
    std::uint16_t window[85] = {};
    window[21] = 2;
    for (int i = 22; i < 85; ++i) {
        window[i] = 521;
    }
    const std::uint16_t* windows[1] = {window};
    seg::SegmentationParams sp = {};
    seg::findSegmentQps(&sp, 1, windows);

    // base 17: segment 7's bin (edge 4) holds 3 and q = 17 - 16 > 0 -> id 7.
    CHECK(seg::aqSegmentId(3, &sp, 17) == 7);
    // base 0: every bin holds 3 but every q is 0 or negative, so the walk
    // runs to the end and the default segment 0 stands.
    CHECK(seg::aqSegmentId(3, &sp, 0) == 0);
    // base 17, above every edge: nothing matches, default 0.
    CHECK(seg::aqSegmentId(600, &sp, 17) == 0);
}

TEST_CASE("aqSegmentId: wrapped negative edges are still bins for variance 0 "
          "(the S10 uniform-4096 params)") {
    std::uint16_t window[85];
    for (int i = 0; i < 85; ++i) {
        window[i] = 4096;
    }
    const std::uint16_t* windows[1] = {window};
    seg::SegmentationParams sp = {};
    seg::findSegmentQps(&sp, 1, windows);
    // i=7..4 match bin-wise but their deltas are negative; i=5's wrapped
    // -32768 edge rejects 0; i=3's edge 0 holds it and q = 5 - 4 = 1 > 0.
    CHECK(seg::aqSegmentId(0, &sp, 5) == 3);
}

TEST_CASE("calculateSegmentationData: seg_id_pre_skip boundary at SEG_LVL_REF_FRAME "
          "(segmentation.c:265-267, j >= SEG_LVL_REF_FRAME)") {
    seg::SegmentationParams sp = {};
    sp.feature_enabled[2][seg::segLvlRefFrame] = 1;
    seg::calculateSegmentationData(&sp);
    CHECK(sp.seg_id_pre_skip == 1);

    seg::SegmentationParams sp2 = {};
    sp2.feature_enabled[2][seg::segLvlAltLfV] = 1;
    seg::calculateSegmentationData(&sp2);
    CHECK(sp2.seg_id_pre_skip == 0);
}

TEST_CASE("kSegFeatureMax over SEG_LVL_MAX (segmentation_params.c:20-21; "
          "MAXQ definitions.h:1642, MAX_LOOP_FILTER definitions.h:1652)") {
    const std::int32_t expect[seg::segLvlMax] = {255, 63, 63, 63, 63, 7, 0, 0};
    for (int j = 0; j < seg::segLvlMax; ++j) {
        CHECK(seg::kSegFeatureMax[j] == expect[j]);
    }
}
