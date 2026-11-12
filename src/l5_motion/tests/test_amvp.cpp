// AMVP1 tests: the AMVP candidate machinery of adaptive_mv_pred.c.
// Every expectation is pinned to an integer and derives from the cited
// reference line (the span is quoted in the comment above the CHECK).
#include <doctest.h>
#include <amvp.h>

using namespace motion::amvp;

namespace {

// A synthetic 4x4-mi (16x16 px) mi grid, stride 4. Cell layout row-major:
//   0  1  2  3
//   4  5  6  7
//   8  9 10 11
//  12 13 14 15
struct Grid4 {
    MbModeInfo cells[16] = {};
    // The reference's mi grid is a grid of POINTERS (MbModeInfo**, each cell
    // pointing at an MbModeInfo - adaptive_mv_pred.c:1090-1091); the fixture
    // mirrors that shape.
    MbModeInfo* grid[16] = {};

    Grid4() {
        for (int i = 0; i < 16; ++i) {
            auto& c = cells[i];
            c.block_mi.mode = NEARESTMV;
            c.block_mi.ref_frame[0] = kIntraFrame;
            c.block_mi.ref_frame[1] = kNoneFrame;
            c.block_mi.use_intrabc = 0;
            c.bsize = BLOCK_8X8;
            c.partition = PARTITION_NONE;
            grid[i] = &cells[i];
        }
    }

    // Makes cell (r, c) an inter block on rf with the given mv.
    void inter(int r, int c, int rf0, int rf1, int mvx, int mvy, int mode = NEARESTMV) {
        MbModeInfo& m = cells[r * 4 + c];
        m.block_mi.mode = mode;
        m.block_mi.ref_frame[0] = rf0;
        m.block_mi.ref_frame[1] = rf1;
        m.block_mi.mv[0].x = static_cast<std::int16_t>(mvx);
        m.block_mi.mv[0].y = static_cast<std::int16_t>(mvy);
    }

    Av1Common cm() const {
        Av1Common c{};
        c.mi_rows = 4;
        c.mi_cols = 4;
        c.mi_stride = 4;
        for (int i = 0; i < kRefFrames; ++i) {
            c.ref_frame_sign_bias[i] = 0;
        }
        return c;
    }

    // The xd carrier for the block at (1,1), 8x8 (n8 = 2, n4 = 2).
    MacroBlockD xd11() {
        MacroBlockD x{};
        x.tile.mi_row_start = 0;
        x.tile.mi_row_end = 4;
        x.tile.mi_col_start = 0;
        x.tile.mi_col_end = 4;
        x.mi_stride = 4;
        x.mi_row = 1;
        x.mi_col = 1;
        x.n4_w = 2;
        x.n4_h = 2;
        x.n8_w = 2;
        x.n8_h = 2;
        x.up_available = 1;
        x.left_available = 1;
        x.is_sec_rect = 0;
        // coding_unit.h / svt_aom_init_xd (adaptive_mv_pred.c:1054-1057):
        // mb_to_top = -GET_MV_SUBPEL(mi_row*MI_SIZE) = -32,
        // mb_to_bottom = GET_MV_SUBPEL((mi_rows - bh - mi_row)*MI_SIZE) = 32,
        // left/right mirrored.
        x.mb_to_top_edge = -32;
        x.mb_to_bottom_edge = 32;
        x.mb_to_left_edge = -32;
        x.mb_to_right_edge = 32;
        x.mi = &grid[1 * 4 + 1];
        return x;
    }
};

// A 16x16-mi (64x64 px) grid, for the outer-offset / light-ctx scans.
struct Grid16 {
    MbModeInfo cells[256] = {};
    MbModeInfo* grid[256] = {};

    Grid16() {
        for (int i = 0; i < 256; ++i) {
            auto& c = cells[i];
            c.block_mi.mode = NEARESTMV;
            c.block_mi.ref_frame[0] = kIntraFrame;
            c.block_mi.ref_frame[1] = kNoneFrame;
            c.block_mi.use_intrabc = 0;
            c.bsize = BLOCK_8X8;
            c.partition = PARTITION_NONE;
            grid[i] = &cells[i];
        }
    }

    void inter(int r, int c, int rf0, int rf1, int mvx, int mvy) {
        MbModeInfo& m = cells[r * 16 + c];
        m.block_mi.mode = NEARESTMV;
        m.block_mi.ref_frame[0] = rf0;
        m.block_mi.ref_frame[1] = rf1;
        m.block_mi.mv[0].x = static_cast<std::int16_t>(mvx);
        m.block_mi.mv[0].y = static_cast<std::int16_t>(mvy);
    }

    Av1Common cm() const {
        Av1Common c{};
        c.mi_rows = 16;
        c.mi_cols = 16;
        c.mi_stride = 16;
        for (int i = 0; i < kRefFrames; ++i) {
            c.ref_frame_sign_bias[i] = 0;
        }
        return c;
    }

    // The xd carrier for the block at (3,3), 8x8.
    MacroBlockD xd33() {
        MacroBlockD x{};
        x.tile.mi_row_start = 0;
        x.tile.mi_row_end = 16;
        x.tile.mi_col_start = 0;
        x.tile.mi_col_end = 16;
        x.mi_stride = 16;
        x.mi_row = 3;
        x.mi_col = 3;
        x.n4_w = 2;
        x.n4_h = 2;
        x.n8_w = 2;
        x.n8_h = 2;
        x.up_available = 1;
        x.left_available = 1;
        x.is_sec_rect = 0;
        // init_xd edges at mi (3,3), bw=bh=2 mi: mb_to_top = -(3*4*8) = -96,
        // bottom = ((16-2-3)*4)*8 = 352, left = -96, right = 352.
        x.mb_to_top_edge = -96;
        x.mb_to_bottom_edge = 352;
        x.mb_to_left_edge = -96;
        x.mb_to_right_edge = 352;
        x.mi = &grid[3 * 16 + 3];
        return x;
    }
};

Mv mvOf(int x, int y) {
    Mv m{};
    m.x = static_cast<std::int16_t>(x);
    m.y = static_cast<std::int16_t>(y);
    return m;
}

}  // namespace

// ===========================================================================
// A1: MV primitives
// ===========================================================================

TEST_CASE("clampMv clamps both components into the box") {
    // mv.h:70-73: mv->x = clamp(mv->x, min_col, max_col); mv->y likewise.
    Mv m = mvOf(100, 50);
    clampMv(&m, 0, 7, -3, 3);
    CHECK(m.x == 7);
    CHECK(m.y == 3);
    Mv n = mvOf(-100, -50);
    clampMv(&n, 0, 7, -3, 3);
    CHECK(n.x == 0);
    CHECK(n.y == -3);
}

TEST_CASE("lowerMvPrecision drops the bottom bit only when allow_hp is 0") {
    // inter_prediction.h:207-213: x = (x + (x < 0)) & ~1 (and y likewise);
    // allow_hp returns early.
    Mv a = mvOf(5, 4);
    lowerMvPrecision(&a, 0);
    CHECK(a.x == 4);
    CHECK(a.y == 4);
    Mv b = mvOf(-5, -4);
    lowerMvPrecision(&b, 0);
    CHECK(b.x == -4);
    CHECK(b.y == -4);
    Mv c = mvOf(5, -5);
    lowerMvPrecision(&c, 1);
    CHECK(c.x == 5);
    CHECK(c.y == -5);
}

TEST_CASE("integerMvPrecision rounds to the nearest multiple of 8, ties toward zero") {
    // inter_prediction.h:201-205: x = (x + 3 + (x < 0)) & ~7.
    CHECK(([x = mvOf(4, 4)]() mutable {
        integerMvPrecision(&x);
        return x.x;
    }()) == 0);
    CHECK(([x = mvOf(5, 4)]() mutable {
        integerMvPrecision(&x);
        return x.x;
    }()) == 8);
    CHECK(([x = mvOf(7, 8)]() mutable {
        integerMvPrecision(&x);
        return x.x;
    }()) == 8);
    CHECK(([x = mvOf(8, 8)]() mutable {
        integerMvPrecision(&x);
        return x.x;
    }()) == 8);
    CHECK(([x = mvOf(-4, 8)]() mutable {
        integerMvPrecision(&x);
        return x.x;
    }()) == 0);
    CHECK(([x = mvOf(-5, 8)]() mutable {
        integerMvPrecision(&x);
        return x.x;
    }()) == -8);
    CHECK(([x = mvOf(-8, 8)]() mutable {
        integerMvPrecision(&x);
        return x.x;
    }()) == -8);
    CHECK(([x = mvOf(-9, 8)]() mutable {
        integerMvPrecision(&x);
        return x.x;
    }()) == -8);
}

TEST_CASE("clampMvRef bounds to the block's clamp box") {
    // adaptive_mv_pred.c:49-55 + inter_prediction.h:31 (MV_BORDER = 128):
    // left = mb_to_left - bw*8 - 128 etc. A full-frame 64x64 block at (0,0)
    // of a 64x64 frame has zero edges: box = [-640, 640] for bw = 64.
    MacroBlockD x{};
    x.mb_to_left_edge = 0;
    x.mb_to_right_edge = 0;
    x.mb_to_top_edge = 0;
    x.mb_to_bottom_edge = 0;
    Mv m = mvOf(700, -700);
    clampMvRef(&m, 64, 64, &x);
    CHECK(m.x == 640);
    CHECK(m.y == -640);
    Mv z = mvOf(0, 0);
    clampMvRef(&z, 64, 64, &x);
    CHECK(z.x == 0);
    CHECK(z.y == 0);
    // The setup_ref_mv_list call form: bw = xd->n8_w << MI_SIZE_LOG2
    // (adaptive_mv_pred.c:965). An 8x8 block (n8_w = 2 -> bw = 8) at mi (2,0)
    // of a 16x4-mi frame: x box [-64-64-128, 192+64+128] = [-256, 384],
    // y box [0-64-128, 0+64+128] = [-192, 192] (init_xd arithmetic).
    MacroBlockD y{};
    y.mb_to_left_edge = -64;
    y.mb_to_right_edge = 192;
    y.mb_to_top_edge = 0;
    y.mb_to_bottom_edge = 0;
    Mv q = mvOf(400, -300);
    clampMvRef(&q, 8, 8, &y);
    CHECK(q.x == 384);
    CHECK(q.y == -192);
}

TEST_CASE("MV_BORDER and INVALID_MV are the reference constants") {
    // inter_prediction.h:31 and mv.h:21.
    CHECK(kMvBorder == 128);
    CHECK(kInvalidMv == 0x80008000u);
}

// ===========================================================================
// A2: MV projection + precision conversion
// ===========================================================================

TEST_CASE("getMvProjection clamps den and num to MAX_FRAME_DISTANCE") {
    // inter_prediction.h:215-224 + inter_prediction.c:32-34 (div_mult) +
    // definitions.h:383-384 (MAX_FRAME_DISTANCE = 31). den 36 -> 31,
    // div_mult[31] = 528: row = RPOTS(-8*31*528, 14) = -8, col = +8.
    Mv ref = mvOf(8, -8);
    Mv out{};
    getMvProjection(&out, ref, 31, 36);
    CHECK(out.x == 8);
    CHECK(out.y == -8);
    Mv out2{};
    getMvProjection(&out2, ref, 31, 31);
    CHECK(out2.x == 8);
    CHECK(out2.y == -8);
    // num = -100 clamps to -31.
    Mv ref2 = mvOf(8, 8);
    Mv out3{};
    getMvProjection(&out3, ref2, -100, 31);
    CHECK(out3.x == -8);
    CHECK(out3.y == -8);
}

TEST_CASE("getMvProjection clamps to MV_LOW+1 / MV_UPP-1") {
    // inter_prediction.h:220-224: clamp_min = MV_LOW + 1 = -16383,
    // clamp_max = MV_UPP - 1 = 16383 (cabac_context_model.h:197-199).
    // ref 8000, num 31, den 3 (div_mult[3] = 5461):
    // 8000*31*5461 = 1,354,328,000 -> RPOTS(.., 14) = 82662 -> clamps to 16383.
    Mv ref = mvOf(8000, 8000);
    Mv out{};
    getMvProjection(&out, ref, 31, 3);
    CHECK(out.x == 16383);
    CHECK(out.y == 16383);
    // ref 10, num -3, den 3: -10*5461 = RPOTS -> -10 (no clamp).
    Mv ref2 = mvOf(10, 10);
    Mv out2{};
    getMvProjection(&out2, ref2, -3, 3);
    CHECK(out2.x == -10);
    CHECK(out2.y == -10);
}

TEST_CASE("convertToTransPrec matches the reference shifts") {
    // utility.h:217-223: allow_hp: RPOTS(coor, WARPEDMODEL_PREC_BITS - 3) =
    // RPOTS(coor, 13); else RPOTS(coor, 14) * 2.
    CHECK(convertToTransPrec(1, 8192) == 1);
    CHECK(convertToTransPrec(1, -8192) == -1);
    CHECK(convertToTransPrec(1, 100) == 0);
    CHECK(convertToTransPrec(0, 8192) == 2);
    CHECK(convertToTransPrec(0, -8192) == -2);
}

// ===========================================================================
// A3: ref-frame maps
// ===========================================================================

TEST_CASE("av1SetRefFrame expands single types and the 21 compound rows") {
    // inter_prediction.h:487-496 + ref_frame_map :461-485.
    int rf[2] = {0, 0};
    av1SetRefFrame(rf, kLastFrame);
    CHECK(rf[0] == kLastFrame);
    CHECK(rf[1] == kNoneFrame);
    av1SetRefFrame(rf, kAltrefFrame);
    CHECK(rf[0] == kAltrefFrame);
    CHECK(rf[1] == kNoneFrame);
    av1SetRefFrame(rf, kTotalRefsPerFrame + 0);  // { LAST, BWDREF }
    CHECK(rf[0] == kLastFrame);
    CHECK(rf[1] == kBwdrefFrame);
    av1SetRefFrame(rf, kTotalRefsPerFrame + 12);  // { LAST, LAST2 }
    CHECK(rf[0] == kLastFrame);
    CHECK(rf[1] == kLast2Frame);
    av1SetRefFrame(rf, kTotalRefsPerFrame + 20);  // { ALTREF2, ALTREF }
    CHECK(rf[0] == kAltref2Frame);
    CHECK(rf[1] == kAltrefFrame);
    av1SetRefFrame(rf, kTotalRefsPerFrame + 15);  // { BWDREF, ALTREF }
    CHECK(rf[0] == kBwdrefFrame);
    CHECK(rf[1] == kAltrefFrame);
}

TEST_CASE("getListIdx and getRefFrameIdx match the reference tables") {
    // inter_prediction.h:505-515.
    CHECK(getListIdx(1) == 0);
    CHECK(getListIdx(4) == 0);
    CHECK(getListIdx(5) == 1);
    CHECK(getListIdx(7) == 1);
    CHECK(getRefFrameIdx(1) == 0);
    CHECK(getRefFrameIdx(2) == 1);
    CHECK(getRefFrameIdx(3) == 2);
    CHECK(getRefFrameIdx(4) == 3);
    CHECK(getRefFrameIdx(5) == 0);
    CHECK(getRefFrameIdx(6) == 1);
    CHECK(getRefFrameIdx(7) == 2);
}

TEST_CASE("av1RefFrameType resolves singles, uni-compound and fwd*bwd pairs") {
    // inter_prediction.h:447-459 + comp_ref0/1 :396-426.
    int single[2] = {kLastFrame, kNoneFrame};
    CHECK(av1RefFrameType(single) == kLastFrame);
    int lastBwd[2] = {kLastFrame, kBwdrefFrame};
    CHECK(av1RefFrameType(lastBwd) == kTotalRefsPerFrame + 0);  // 8
    int last2Alt[2] = {kLast2Frame, kAltrefFrame};
    CHECK(av1RefFrameType(last2Alt) == kTotalRefsPerFrame + 1 + 2 * kFwdRefs);  // 17
    int last3Golden[2] = {kLast3Frame, kGoldenFrame};
    CHECK(av1RefFrameType(last3Golden) == kTotalRefsPerFrame + kFwdRefs * kBwdRefs + 6);  // 26
    int alt2Alt[2] = {kAltref2Frame, kAltrefFrame};
    CHECK(av1RefFrameType(alt2Alt) == kTotalRefsPerFrame + kFwdRefs * kBwdRefs + 8);  // 28
    int bwdAlt[2] = {kBwdrefFrame, kAltrefFrame};
    CHECK(av1RefFrameType(bwdAlt) == kTotalRefsPerFrame + kFwdRefs * kBwdRefs + 3);  // 23
}

// ===========================================================================
// A4: block/mi geometry tables + centers
// ===========================================================================

TEST_CASE("block geometry tables are the reference transcriptions") {
    // common_utils.c:286-290 (block_size_wide/high) and :292-293
    // (mi_size_wide/high): the full 22-row domain, exact.
    const std::uint8_t wide[] = {4,  4,  8,  8,   8,   16, 16, 16, 32, 32, 32,
                                 64, 64, 64, 128, 128, 4,  16, 8,  32, 16, 64};
    const std::uint8_t high[] = {4,  8,  4,   8,  16,  8,  16, 32, 16, 32, 64,
                                 32, 64, 128, 64, 128, 16, 4,  32, 8,  64, 16};
    const std::uint8_t miW[] = {1, 1, 2, 2, 2, 4, 4, 4, 8, 8, 8, 16, 16, 16, 32, 32, 1, 4, 2, 8, 4, 16};
    const std::uint8_t miH[] = {1, 2, 1, 2, 4, 2, 4, 8, 4, 8, 16, 8, 16, 32, 16, 32, 4, 1, 8, 2, 16, 4};
    for (int i = 0; i < BLOCK_SIZES_ALL; ++i) {
        CHECK(kBlockSizeWide[i] == wide[i]);
        CHECK(kBlockSizeHigh[i] == high[i]);
        CHECK(kMiSizeWide[i] == miW[i]);
        CHECK(kMiSizeHigh[i] == miH[i]);
    }
}

TEST_CASE("div_mult is the reference table") {
    // inter_prediction.c:32-34.
    const int d[] = {0,    16384, 8192, 5461, 4096, 3276, 2730, 2340, 2048, 1820, 1638,
                     1489, 1365,  1260, 1170, 1092, 1024, 963,  910,  862,  819,  780,
                     744,  712,   682,  655,  630,  606,  585,  564,  546,  528};
    for (int i = 0; i < 32; ++i) {
        CHECK(kDivMult[i] == d[i]);
    }
}

TEST_CASE("block centers are mi*size + half - 1") {
    // adaptive_mv_pred.c:973-981.
    CHECK(blockCenterX(3, BLOCK_16X16) == 19);  // 3*4 + 16/2 - 1
    CHECK(blockCenterY(2, BLOCK_8X8) == 11);
    CHECK(blockCenterX(0, BLOCK_128X128) == 63);
}

// ===========================================================================
// A5: the stack add core
// ===========================================================================

TEST_CASE("addRefMvCandidate single ref: add, dedup, newmv and match counts") {
    // adaptive_mv_pred.c:57-128 (single branch :66-94).
    const int rf[2] = {kLastFrame, kNoneFrame};
    MbModeInfo cand{};
    cand.bsize = BLOCK_8X8;
    cand.partition = PARTITION_NONE;
    cand.block_mi.mode = NEARESTMV;
    cand.block_mi.ref_frame[0] = kLastFrame;
    cand.block_mi.ref_frame[1] = kNoneFrame;
    cand.block_mi.mv[0] = mvOf(10, 20);
    WarpedMotionParams gm[8];
    for (auto& g : gm) {
        g.wmtype = IDENTITY;
        g.wmmat[0] = 0;
        g.wmmat[1] = 0;
        g.wmmat[2] = 1 << kWarpedModelPrecBits;
        g.wmmat[3] = 0;
        g.wmmat[4] = 0;
        g.wmmat[5] = 1 << kWarpedModelPrecBits;
    }
    Mv gmMv[2] = {};
    CandidateMv stack[kMaxRefMvStackSize] = {};
    std::uint8_t count = 0, match = 0, newmv = 0;
    addRefMvCandidate(cand, rf, &count, &match, &newmv, stack, 4, gmMv, gm, 2);
    CHECK(count == 1);
    CHECK(stack[0].this_mv.x == 10);
    CHECK(stack[0].this_mv.y == 20);
    CHECK(stack[0].weight == 8);  // weight * len = 2 * 4
    CHECK(match == 1);
    CHECK(newmv == 0);
    // Second candidate, same mv, NEWMV: dedup (weight accumulates) (:77-88),
    // newmv_count increments (:89-91).
    MbModeInfo cand2 = cand;
    cand2.block_mi.mode = NEWMV;
    addRefMvCandidate(cand2, rf, &count, &match, &newmv, stack, 4, gmMv, gm, 2);
    CHECK(count == 1);
    CHECK(stack[0].weight == 16);
    CHECK(match == 2);
    CHECK(newmv == 1);
}

TEST_CASE("addRefMvCandidate uses the gm candidate for global mv blocks") {
    // adaptive_mv_pred.c:71-75 + inter_prediction.h:382-388
    // (is_global_mv_block: GLOBALMV mode && type > TRANSLATION && bsize
    // allows motion variation).
    const int rf[2] = {kLastFrame, kNoneFrame};
    MbModeInfo cand{};
    cand.bsize = BLOCK_8X8;
    cand.partition = PARTITION_NONE;
    cand.block_mi.mode = GLOBALMV;
    cand.block_mi.ref_frame[0] = kLastFrame;
    cand.block_mi.ref_frame[1] = kNoneFrame;
    cand.block_mi.mv[0] = mvOf(10, 20);
    WarpedMotionParams gm[8];
    for (auto& g : gm) {
        g.wmtype = IDENTITY;
        g.wmmat[0] = 0;
        g.wmmat[1] = 0;
        g.wmmat[2] = 1 << kWarpedModelPrecBits;
        g.wmmat[3] = 0;
        g.wmmat[4] = 0;
        g.wmmat[5] = 1 << kWarpedModelPrecBits;
    }
    gm[kLastFrame].wmtype = ROTZOOM;
    Mv gmMv[2] = {mvOf(100, 50), mvOf(0, 0)};
    CandidateMv stack[kMaxRefMvStackSize] = {};
    std::uint8_t count = 0, match = 0, newmv = 0;
    addRefMvCandidate(cand, rf, &count, &match, &newmv, stack, 4, gmMv, gm, 2);
    CHECK(stack[0].this_mv.x == 100);
    CHECK(stack[0].this_mv.y == 50);
    CHECK(newmv == 0);  // GLOBALMV is not a newmv mode (mode_decision.c:274-277)
    // TRANSLATION (1) does NOT trip the gate: the block mv is used.
    gm[kLastFrame].wmtype = TRANSLATION;
    CandidateMv stack2[kMaxRefMvStackSize] = {};
    std::uint8_t count2 = 0, match2 = 0, newmv2 = 0;
    addRefMvCandidate(cand, rf, &count2, &match2, &newmv2, stack2, 4, gmMv, gm, 2);
    CHECK(stack2[0].this_mv.x == 10);
    CHECK(stack2[0].this_mv.y == 20);
}

TEST_CASE("addRefMvCandidate skips intra candidates") {
    // adaptive_mv_pred.c:61-63: !is_inter_block returns early (for intrabc).
    const int rf[2] = {kLastFrame, kNoneFrame};
    MbModeInfo cand{};
    cand.bsize = BLOCK_8X8;
    cand.partition = PARTITION_NONE;
    cand.block_mi.mode = DC_PRED;
    cand.block_mi.ref_frame[0] = kIntraFrame;
    cand.block_mi.ref_frame[1] = kNoneFrame;
    WarpedMotionParams gm[8];
    for (auto& g : gm) {
        g.wmtype = IDENTITY;
    }
    Mv gmMv[2] = {};
    CandidateMv stack[kMaxRefMvStackSize] = {};
    std::uint8_t count = 0, match = 0, newmv = 0;
    addRefMvCandidate(cand, rf, &count, &match, &newmv, stack, 4, gmMv, gm, 2);
    CHECK(count == 0);
    CHECK(match == 0);
    CHECK(newmv == 0);
}

TEST_CASE("addRefMvCandidate compound: pair dedup and mismatch skip") {
    // adaptive_mv_pred.c:95-127 (compound branch).
    const int rf[2] = {kLastFrame, kBwdrefFrame};
    MbModeInfo cand{};
    cand.bsize = BLOCK_8X8;
    cand.partition = PARTITION_NONE;
    cand.block_mi.mode = NEAR_NEARMV;
    cand.block_mi.ref_frame[0] = kLastFrame;
    cand.block_mi.ref_frame[1] = kBwdrefFrame;
    cand.block_mi.mv[0] = mvOf(10, 20);
    cand.block_mi.mv[1] = mvOf(30, 40);
    WarpedMotionParams gm[8];
    for (auto& g : gm) {
        g.wmtype = IDENTITY;
    }
    Mv gmMv[2] = {};
    CandidateMv stack[kMaxRefMvStackSize] = {};
    std::uint8_t count = 0, match = 0, newmv = 0;
    addRefMvCandidate(cand, rf, &count, &match, &newmv, stack, 4, gmMv, gm, 2);
    CHECK(count == 1);
    CHECK(stack[0].this_mv.x == 10);
    CHECK(stack[0].this_mv.y == 20);
    CHECK(stack[0].comp_mv.x == 30);
    CHECK(stack[0].comp_mv.y == 40);
    CHECK(stack[0].weight == 8);
    CHECK(match == 1);
    CHECK(newmv == 0);
    // A compound candidate with a mismatching second ref is skipped
    // (:97: ref_frame[0] == rf[0] && ref_frame[1] == rf[1]).
    MbModeInfo cand2 = cand;
    cand2.block_mi.ref_frame[1] = kAltrefFrame;
    CandidateMv stack2[kMaxRefMvStackSize] = {};
    std::uint8_t count2 = 0, match2 = 0, newmv2 = 0;
    addRefMvCandidate(cand2, rf, &count2, &match2, &newmv2, stack2, 4, gmMv, gm, 2);
    CHECK(count2 == 0);
    CHECK(match2 == 0);
}

TEST_CASE("getBlockMv returns the indexed block mv") {
    // adaptive_mv_pred.c:40-42.
    MbModeInfo cand{};
    cand.block_mi.mv[0] = mvOf(1, 2);
    cand.block_mi.mv[1] = mvOf(3, 4);
    CHECK(getBlockMv(cand, 0).x == 1);
    CHECK(getBlockMv(cand, 1).x == 3);
}

// ===========================================================================
// A6 + A9: the scans and the builder driver
// ===========================================================================

TEST_CASE("scanRowMbmi weights the above candidate by weight*len") {
    // adaptive_mv_pred.c:130-184 with max_row_offset = -1, row_offset = -1:
    // inc = MIN(1, 2) = 1 -> weight = MAX(2, 1) = 2; len = MIN(n8, n8) = 2.
    Grid4 g;
    g.inter(0, 1, kLastFrame, kNoneFrame, 5, 7);
    Av1Common cm = g.cm();
    MacroBlockD x = g.xd11();
    const int rf[2] = {kLastFrame, kNoneFrame};
    WarpedMotionParams gm[8];
    for (auto& p : gm) {
        p.wmtype = IDENTITY;
    }
    Mv gmMv[2] = {};
    CandidateMv stack[kMaxRefMvStackSize] = {};
    std::uint8_t count = 0, match = 0, newmv = 0;
    std::int32_t processedRows = 0;
    scanRowMbmi(cm, x, 1, rf, -1, stack, &count, &match, &newmv, gmMv, gm, -1, &processedRows);
    CHECK(count == 1);
    CHECK(stack[0].this_mv.x == 5);
    CHECK(stack[0].this_mv.y == 7);
    CHECK(stack[0].weight == 4);  // 2 * 2
    CHECK(match == 1);
    CHECK(newmv == 0);
    CHECK(processedRows == 1);
}

TEST_CASE("scanColMbmi weights the left candidate the same way") {
    // adaptive_mv_pred.c:186-239.
    Grid4 g;
    g.inter(1, 0, kLastFrame, kNoneFrame, 3, 4);
    Av1Common cm = g.cm();
    MacroBlockD x = g.xd11();
    const int rf[2] = {kLastFrame, kNoneFrame};
    WarpedMotionParams gm[8];
    for (auto& p : gm) {
        p.wmtype = IDENTITY;
    }
    Mv gmMv[2] = {};
    CandidateMv stack[kMaxRefMvStackSize] = {};
    std::uint8_t count = 0, match = 0, newmv = 0;
    std::int32_t processedCols = 0;
    scanColMbmi(cm, x, 1, rf, -1, stack, &count, &match, &newmv, gmMv, gm, -1, &processedCols);
    CHECK(count == 1);
    CHECK(stack[0].this_mv.x == 3);
    CHECK(stack[0].this_mv.y == 4);
    CHECK(stack[0].weight == 4);
    CHECK(match == 1);
    CHECK(processedCols == 1);
}

TEST_CASE("scanBlkMbmi adds the inside candidate with the 8x8 weight") {
    // adaptive_mv_pred.c:241-264: len = mi_size_wide[BLOCK_8X8] = 2,
    // weight = 2 -> weight*len = 4; outside candidates are skipped.
    Grid4 g;
    g.inter(0, 3, kLastFrame, kNoneFrame, 9, 9);
    MacroBlockD x = g.xd11();
    const int rf[2] = {kLastFrame, kNoneFrame};
    WarpedMotionParams gm[8];
    for (auto& p : gm) {
        p.wmtype = IDENTITY;
    }
    Mv gmMv[2] = {};
    CandidateMv stack[kMaxRefMvStackSize] = {};
    std::uint8_t match = 0, newmv = 0, count = 0;
    scanBlkMbmi(x, 1, 1, rf, -1, 2, stack, &match, &newmv, gmMv, gm, &count);
    CHECK(count == 1);
    CHECK(stack[0].this_mv.x == 9);
    CHECK(stack[0].weight == 4);
    // The top-left diagonal of an interior block IS inside; an intra there
    // is skipped by addRefMvCandidate.
    MacroBlockD x2 = g.xd11();
    CandidateMv stack2[kMaxRefMvStackSize] = {};
    std::uint8_t match2 = 0, newmv2 = 0, count2 = 0;
    scanBlkMbmi(x2, 1, 1, rf, -1, -1, stack2, &match2, &newmv2, gmMv, gm, &count2);
    CHECK(count2 == 0);
}

TEST_CASE("hasTopRight follows the mask and rect rules") {
    // adaptive_mv_pred.c:266-325.
    Grid4 g;
    MacroBlockD base = g.xd11();
    // A 64x64 SB root block (bs = 16): mask_row = mask_col = 0 -> 1.
    MacroBlockD x = base;
    x.n8_w = 16;
    x.n8_h = 16;
    CHECK(hasTopRight(BLOCK_64X64, x, 0, 0, 16) == 1);
    // The bottom-right leaf of a 64x64 split into four 32x32s (bs = 8 at
    // mi (8,8)): mask 8&8 -> 0.
    x.n8_w = 8;
    x.n8_h = 8;
    CHECK(hasTopRight(BLOCK_64X64, x, 8, 8, 8) == 0);
    // A horizontal rectangle with is_sec_rect = 0 never has a top right
    // (:274-278) - BLOCK_64X16 second row.
    MacroBlockD h = base;
    h.n8_w = 4;
    h.n8_h = 1;
    h.is_sec_rect = 1;
    CHECK(hasTopRight(BLOCK_64X64, h, 1, 1, 4) == 0);
    // A non-sec horizontal rectangle falls through to the mask logic
    // (:288-311). At mi (4,0): mask_row = 4, mask_col = 0 -> has_tr = 1;
    // the while loop breaks on mask_col & bs = 0.
    MacroBlockD h2 = base;
    h2.n8_w = 4;
    h2.n8_h = 1;
    h2.is_sec_rect = 0;
    CHECK(hasTopRight(BLOCK_64X64, h2, 4, 0, 4) == 1);
    // The left hand of two vertical rectangles always has a top right
    // (:282-286).
    MacroBlockD v = base;
    v.n8_w = 1;
    v.n8_h = 4;
    v.is_sec_rect = 0;
    CHECK(hasTopRight(BLOCK_64X64, v, 1, 1, 4) == 1);
    // bs beyond the SB span is 0 (:268-270).
    CHECK(hasTopRight(BLOCK_64X64, x, 0, 0, 32) == 0);
    // The PARTITION_VERT_A quirk (:316-322): an 8x8 block at mi (2,0) in a
    // VERT_A partition loses its top right (mask_row & bs = 2 & 2 != 0),
    // while the same block under PARTITION_NONE keeps it.
    Grid16 g16;
    MacroBlockD va = g16.xd33();
    va.mi = &g16.grid[2 * 16 + 0];
    va.n8_w = 2;
    va.n8_h = 2;
    va.is_sec_rect = 0;
    va.mi[0]->partition = PARTITION_VERT_A;
    CHECK(hasTopRight(BLOCK_64X64, va, 2, 0, 2) == 0);
    va.mi[0]->partition = PARTITION_NONE;
    CHECK(hasTopRight(BLOCK_64X64, va, 2, 0, 2) == 1);
}

TEST_CASE("sortMvpTable is a stable-weight bubble to the top") {
    // adaptive_mv_pred.c:450-465: while (len > 0) .. nr_len bubble.
    CandidateMv stack[kMaxRefMvStackSize] = {};
    stack[0] = {mvOf(1, 1), Mv{}, 2};
    stack[1] = {mvOf(2, 2), Mv{}, 8};
    stack[2] = {mvOf(3, 3), Mv{}, 4};
    stack[3] = {mvOf(4, 4), Mv{}, 8};
    std::uint8_t count = 4;
    sortMvpTable(stack, &count);
    CHECK(count == 4);
    CHECK(stack[0].this_mv.x == 2);
    CHECK(stack[1].this_mv.x == 4);
    CHECK(stack[2].this_mv.x == 3);
    CHECK(stack[3].this_mv.x == 1);
}

TEST_CASE("getRelativeDist folds the order-hint distance into signed range") {
    // adaptive_mv_pred.c:335-350.
    OrderHintInfo oh{};
    oh.enable_order_hint = 0;
    oh.order_hint_bits = 4;
    CHECK(getRelativeDist(oh, 5, 2) == 0);
    oh.enable_order_hint = 1;
    CHECK(getRelativeDist(oh, 5, 2) == 3);
    CHECK(getRelativeDist(oh, 2, 5) == -3);
    CHECK(getRelativeDist(oh, 0, 15) == 1);  // wraps to +1
    oh.order_hint_bits = 5;
    CHECK(getRelativeDist(oh, 0, 31) == 1);
}

TEST_CASE("findValidRow/ColOffset clamp to the tile") {
    // adaptive_mv_pred.c:327-333.
    TileInfo t{0, 4, 0, 4};
    CHECK(findValidRowOffset(t, 1, -6) == -1);
    CHECK(findValidColOffset(t, 1, -6) == -1);
    TileInfo t16{0, 16, 0, 16};
    CHECK(findValidRowOffset(t16, 3, -6) == -3);
    CHECK(findValidColOffset(t16, 3, -6) == -3);
}

TEST_CASE("isInside tests the tile bounds") {
    // adaptive_mv_pred.c:44-47.
    TileInfo t{0, 4, 0, 4};
    CHECK(isInside(t, 1, 1, -1, 0) == 1);
    CHECK(isInside(t, 1, 1, -1, 2) == 1);
    CHECK(isInside(t, 1, 1, -1, 3) == 0);
    CHECK(isInside(t, 1, 1, 3, 0) == 0);  // mi_row + 3 = 4 >= mi_row_end (exclusive)
    CHECK(isInside(t, 1, 1, 4, 0) == 0);
}

TEST_CASE("setupRefMvList end-to-end: the 3-neighbor grid, MFMV off") {
    // adaptive_mv_pred.c:651-971 over the Grid4 fixture: ROW-1 adds {5,7}
    // (weight 4), COL-1 adds {3,4} (4), top-right adds {9,9} (4); REF_CAT_LEVEL
    // (640) lifts the three; top-left is intra; row3/col3 skipped
    // (|row_offset| = 3 > |max_row_offset| = 1); nearest_match = 2 with
    // newmv 0 -> mode_ctx = 5 | (5 << 4) = 85; light scan skipped (count >= 2);
    // clamp leaves the small mvs inside the +/-224 box.
    Grid4 g;
    g.inter(0, 1, kLastFrame, kNoneFrame, 5, 7);
    g.inter(1, 0, kLastFrame, kNoneFrame, 3, 4);
    g.inter(0, 3, kLastFrame, kNoneFrame, 9, 9);
    Av1Common cm = g.cm();
    MacroBlockD x = g.xd11();
    WarpedMotionParams gm[8];
    for (auto& p : gm) {
        p.wmtype = IDENTITY;
        p.wmmat[0] = 0;
        p.wmmat[1] = 0;
        p.wmmat[2] = 1 << kWarpedModelPrecBits;
        p.wmmat[3] = 0;
        p.wmmat[4] = 0;
        p.wmmat[5] = 1 << kWarpedModelPrecBits;
    }
    Mv gmMv[2] = {};
    MvRefSetup setup{};
    setup.sbSize = BLOCK_64X64;
    setup.useRefFrameMvs = 0;
    setup.allowHighPrecisionMv = 1;
    setup.sb64SqNo4xnGeom = 0;
    setup.tplMvs = nullptr;
    setup.curOrderHint = 0;
    for (int l = 0; l < 2; ++l) {
        for (int i = 0; i < 4; ++i) {
            setup.refOrderHints[l][i] = 0;
        }
    }
    setup.orderHintInfo.enable_order_hint = 0;
    setup.orderHintInfo.order_hint_bits = 4;
    CandidateMv stack[kMaxRefMvStackSize] = {};
    std::uint8_t count = 0;
    std::int16_t modeCtx = 0;
    Mv mvRef0[64] = {};
    setupRefMvList(setup, cm, x, kLastFrame, &count, stack, gmMv, gm, 1, 1, 0, mvRef0, &modeCtx);
    CHECK(count == 3);
    CHECK(stack[0].this_mv.x == 5);
    CHECK(stack[0].this_mv.y == 7);
    CHECK(stack[0].weight == 4 + kRefCatLevel);
    CHECK(stack[1].this_mv.x == 3);
    CHECK(stack[1].this_mv.y == 4);
    CHECK(stack[1].weight == 4 + kRefCatLevel);
    CHECK(stack[2].this_mv.x == 9);
    CHECK(stack[2].this_mv.y == 9);
    CHECK(stack[2].weight == 4 + kRefCatLevel);
    CHECK(modeCtx == 85);
}

TEST_CASE("setupRefMvList with MFMV: tpl candidates join with weight 2") {
    // adaptive_mv_pred.c:756-860 + add_tpl_ref_mv :352-447. The block at
    // (1,1) with n4 = 2: the main tpl loop steps blk_row AND blk_col by 2
    // over ends of 2, so ONLY the (0,0) unit is visited - tpl[0], INVALID
    // (is_available = 0 -> GLOBALMV bit 8 set). Both tpl candidates come
    // from the extension samples: {2,-2} is outside the SB border
    // (check_sb_border: col 1-2 = -1 -> skipped), {2,2} hits tpl[3] (the
    // (1,1) tpl-grid unit) projecting {24,30} (RPOTS(48*2*4096, 14) = 24.5
    // -> 24) added weight 2, and {0,2} hits tpl[1] (the (0,1) tpl-grid
    // unit) projecting {100,100} (div_mult[2]: 100*2*8192 >> 14 = 100)
    // added weight 2. Nothing dedups - each projection is new.
    // mode_ctx gains the GLOBALMV bit: 85 | 8 = 93.
    Grid4 g;
    g.inter(0, 1, kLastFrame, kNoneFrame, 5, 7);
    g.inter(1, 0, kLastFrame, kNoneFrame, 3, 4);
    g.inter(0, 3, kLastFrame, kNoneFrame, 9, 9);
    Av1Common cm = g.cm();
    MacroBlockD x = g.xd11();
    TplMvRef tpl[4] = {};
    tpl[0].mfmv0 = mvOf(0, 0);
    tpl[0].mfmv0.as_int = kInvalidMv;
    tpl[1].mfmv0 = mvOf(100, 100);
    tpl[1].ref_frame_offset = 2;
    tpl[2].mfmv0 = mvOf(0, 0);
    tpl[2].mfmv0.as_int = kInvalidMv;
    tpl[3].mfmv0 = mvOf(48, 60);  // RPOTS(48*2*4096, 14) = 24.5 -> 24; 50 would give exactly 25
    tpl[3].ref_frame_offset = 4;
    WarpedMotionParams gm[8];
    for (auto& p : gm) {
        p.wmtype = IDENTITY;
        p.wmmat[0] = 0;
        p.wmmat[1] = 0;
        p.wmmat[2] = 1 << kWarpedModelPrecBits;
        p.wmmat[3] = 0;
        p.wmmat[4] = 0;
        p.wmmat[5] = 1 << kWarpedModelPrecBits;
    }
    Mv gmMv[2] = {};
    MvRefSetup setup{};
    setup.sbSize = BLOCK_64X64;
    setup.useRefFrameMvs = 1;
    setup.allowHighPrecisionMv = 1;
    setup.sb64SqNo4xnGeom = 0;
    setup.tplMvs = tpl;
    setup.curOrderHint = 4;  // cur_offset_0 = dist(4, refOrderHints[0][0]=2) = +2
    setup.refOrderHints[0][0] = 2;
    setup.orderHintInfo.enable_order_hint = 1;
    setup.orderHintInfo.order_hint_bits = 4;
    CandidateMv stack[kMaxRefMvStackSize] = {};
    std::uint8_t count = 0;
    std::int16_t modeCtx = 0;
    Mv mvRef0[64] = {};
    setupRefMvList(setup, cm, x, kLastFrame, &count, stack, gmMv, gm, 1, 1, 0, mvRef0, &modeCtx);
    CHECK(count == 5);
    CHECK(stack[0].this_mv.x == 5);
    CHECK(stack[0].weight == 4 + kRefCatLevel);
    CHECK(stack[1].this_mv.x == 3);
    CHECK(stack[2].this_mv.x == 9);
    CHECK(stack[3].this_mv.x == 24);
    CHECK(stack[3].this_mv.y == 30);
    CHECK(stack[3].weight == 2);  // extension-only add ({2,2} sample)
    CHECK(stack[4].this_mv.x == 100);
    CHECK(stack[4].this_mv.y == 100);
    CHECK(stack[4].weight == 2);  // extension-only add ({0,2} sample)
    CHECK(modeCtx == 93);
}

// ===========================================================================
// A10: the light scan
// ===========================================================================

TEST_CASE("scanRowColLight single ref: fills from the neighbors then gm") {
    // adaptive_mv_pred.c:469-648 (single branch :577-647): the ROW=-1 rescan
    // adds {5,7} (weight 2, dedup against the stack), the COL=-1 rescan adds
    // {3,4}; the gm fill loop stops at MAX_MV_REF_CANDIDATES.
    Grid4 g;
    g.inter(0, 1, kLastFrame, kNoneFrame, 5, 7);
    g.inter(1, 0, kLastFrame, kNoneFrame, 3, 4);
    Av1Common cm = g.cm();
    MacroBlockD x = g.xd11();
    const int rf[2] = {kLastFrame, kNoneFrame};
    Mv gmMv[2] = {mvOf(7, 7), mvOf(7, 7)};
    CandidateMv stack[kMaxRefMvStackSize] = {};
    std::uint8_t count = 0;
    scanRowColLight(cm, x, 1, 1, rf, stack, &count, gmMv, -1, -1);
    CHECK(count == 2);
    CHECK(stack[0].this_mv.x == 5);
    CHECK(stack[0].weight == 2);
    CHECK(stack[1].this_mv.x == 3);
    CHECK(stack[1].weight == 2);
    CHECK(stack[2].this_mv.as_int == 0u);
}

TEST_CASE("scanRowColLight negates across sign bias") {
    // adaptive_mv_pred.c:498-501/:590-594: a candidate on a frame with a
    // different sign bias has its mv negated.
    Grid4 g;
    g.cells[1].block_mi.ref_frame[0] = kBwdrefFrame;
    g.cells[1].block_mi.ref_frame[1] = kNoneFrame;
    g.cells[1].block_mi.mv[0] = mvOf(5, 7);
    Av1Common cm = g.cm();
    cm.ref_frame_sign_bias[kBwdrefFrame] = 1;
    MacroBlockD x = g.xd11();
    const int rf[2] = {kLastFrame, kNoneFrame};
    Mv gmMv[2] = {};
    CandidateMv stack[kMaxRefMvStackSize] = {};
    std::uint8_t count = 0;
    scanRowColLight(cm, x, 1, 1, rf, stack, &count, gmMv, -1, -1);
    CHECK(count == 1);
    CHECK(stack[0].this_mv.x == -5);
    CHECK(stack[0].this_mv.y == -7);
}

TEST_CASE("scanRowColLight compound: comp_list from ref ids and the gm fill") {
    // adaptive_mv_pred.c:479-576 (compound branch): ref_id collects the two
    // matching-ref mvs from the ROW=-1 scan ({5,7} this, {3,4} comp) and the
    // COL=-1 id {9,9}; the ref_diff rows are only consumed while
    // comp_idx < MAX_MV_REF_CANDIDATES = 2, so list 1's second slot takes the
    // diff fallback {5,7} (the above candidate's mv[0]) and the third row is
    // never written; count 0 -> only MAX_MV_REF_CANDIDATES (2) rows land on
    // the stack with weight 2 (:567-574).
    Grid4 g;
    g.inter(0, 1, kLastFrame, kBwdrefFrame, 5, 7);   // above: ids both lists
    g.cells[1].block_mi.mv[1] = mvOf(3, 4);
    g.inter(1, 0, kLastFrame, kNoneFrame, 9, 9);     // left: id list 0
    Av1Common cm = g.cm();
    MacroBlockD x = g.xd11();
    const int rf[2] = {kLastFrame, kBwdrefFrame};
    Mv gmMv[2] = {mvOf(7, 7), mvOf(7, 7)};
    CandidateMv stack[kMaxRefMvStackSize] = {};
    std::uint8_t count = 0;
    scanRowColLight(cm, x, 1, 1, rf, stack, &count, gmMv, -1, -1);
    CHECK(count == 2);
    CHECK(stack[0].this_mv.x == 5);
    CHECK(stack[0].this_mv.y == 7);
    CHECK(stack[0].comp_mv.x == 3);
    CHECK(stack[0].comp_mv.y == 4);
    CHECK(stack[0].weight == 2);
    CHECK(stack[1].this_mv.x == 9);
    CHECK(stack[1].comp_mv.x == 5);
    CHECK(stack[1].comp_mv.y == 7);
    CHECK(stack[1].weight == 2);
}

// ===========================================================================
// A11: global-motion MV evaluation
// ===========================================================================

TEST_CASE("gmGetMotionVectorEnc IDENTITY is the zero mv") {
    // adaptive_mv_pred.c:987-990.
    WarpedMotionParams gm{};
    gm.wmtype = IDENTITY;
    Mv r = gmGetMotionVectorEnc(gm, 1, BLOCK_8X8, 0, 0, 0);
    CHECK(r.as_int == 0u);
}

TEST_CASE("gmGetMotionVectorEnc TRANSLATION keeps the spec's swapped wmmat read") {
    // adaptive_mv_pred.c:992-1014: res.y = wmmat[0] >> GM_TRANS_ONLY_PREC_DIFF
    // (13) and res.x = wmmat[1] >> 13 - the AV1 spec bug the reference
    // documents (see also https://crbug.com/aomedia/3328).
    WarpedMotionParams gm{};
    gm.wmtype = TRANSLATION;
    gm.wmmat[0] = 65536;
    gm.wmmat[1] = 131072;
    Mv r = gmGetMotionVectorEnc(gm, 1, BLOCK_8X8, 0, 0, 0);
    CHECK(r.y == 8);
    CHECK(r.x == 16);
    // is_integer rounds both to multiples of 8 (inter_prediction.h:201-205).
    Mv r2 = gmGetMotionVectorEnc(gm, 1, BLOCK_8X8, 0, 0, 1);
    CHECK(r2.y == 8);
    CHECK(r2.x == 16);
}

TEST_CASE("gmGetMotionVectorEnc ROTZOOM evaluates the block center") {
    // adaptive_mv_pred.c:1015-1030 + block_center_x/y :973-981. For BLOCK_8X8
    // at mi (0,0): center = (3,3). wmmat[2] = 2^16 + 8192: xc = 8192*3 = 24576
    // -> convert_to_trans_prec(hp) = RPOTS(24576, 13) = 3; wmmat[5] = 2^16 ->
    // yc = 0.
    WarpedMotionParams gm{};
    gm.wmtype = ROTZOOM;
    gm.wmmat[0] = 0;
    gm.wmmat[1] = 0;
    gm.wmmat[2] = (1 << kWarpedModelPrecBits) + 8192;
    gm.wmmat[3] = 0;
    gm.wmmat[4] = 0;
    gm.wmmat[5] = 1 << kWarpedModelPrecBits;
    Mv r = gmGetMotionVectorEnc(gm, 1, BLOCK_8X8, 0, 0, 0);
    CHECK(r.x == 3);
    CHECK(r.y == 0);
    // allow_hp = 0: RPOTS(24576, 14) * 2 = 4.
    Mv r2 = gmGetMotionVectorEnc(gm, 0, BLOCK_8X8, 0, 0, 0);
    CHECK(r2.x == 4);
    CHECK(r2.y == 0);
}

// ===========================================================================
// A12: the light inter-mode context
// ===========================================================================

TEST_CASE("computeInterModeCtxLight nearest_match 2 gives 85") {
    // adaptive_mv_pred.c:1138-1327 over the Grid4 fixture: row_match 2
    // (above + top-right), col_match 1, nearest_match = 2 -> mode_ctx =
    // 5 | (5 << REFMV_OFFSET) = 85. The top-left is intra and does not
    // change the counters; the outer scan is skipped once both matched
    // (:1236).
    Grid4 g;
    g.inter(0, 1, kLastFrame, kNoneFrame, 5, 7);
    g.inter(1, 0, kLastFrame, kNoneFrame, 3, 4);
    g.inter(0, 3, kLastFrame, kNoneFrame, 9, 9);
    Av1Common cm = g.cm();
    MacroBlockD x = g.xd11();
    CHECK(computeInterModeCtxLight(x, cm, 1, 1, kLastFrame, BLOCK_64X64) == 85);
}

TEST_CASE("computeInterModeCtxLight outer scans and the ctx table") {
    // adaptive_mv_pred.c:1236-1327 over the Grid16 fixture (block at (3,3),
    // max offsets -3). At (3,3) the mask is (3,3): 3 & 2 = 2 closes
    // has_top_right, so the (2,5) cell is never read (the staged comment's
    // "top-right" term is actually the outer row candidate); row_match 3
    // (above + top-left + outer row), col_match 1 (outer col),
    // nearest_match = 1, newmv 0 -> ctx = 3 | (4 << 4) = 67.
    Grid16 g;
    g.inter(2, 3, kLastFrame, kNoneFrame, 5, 7);   // above-left
    g.inter(2, 5, kLastFrame, kNoneFrame, 9, 9);   // (never read: has_tr = 0)
    g.inter(3, 2, kIntraFrame, kNoneFrame, 0, 0);  // left intra
    g.inter(2, 2, kLastFrame, kNoneFrame, 1, 2);   // top-left inter (quirk: row_match AFTER nearest)
    g.inter(0, 4, kLastFrame, kNoneFrame, 2, 2);   // outer row-3 candidate (at row_mi[1])
    g.inter(4, 0, kLastFrame, kNoneFrame, 4, 4);   // outer col-3 candidate
    Av1Common cm = g.cm();
    MacroBlockD x = g.xd33();
    CHECK(computeInterModeCtxLight(x, cm, 3, 3, kLastFrame, BLOCK_64X64) == 67);
    // Variant: the top-right candidate is NEWMV -> newmv_count 1, left stays
    // intra -> nearest_match = 1 -> ctx = 2 | (4 << 4) = 66. Two geometry
    // constraints pin the block position: has_top_right must be 1 (mask_col
    // & bs 2 = 0 -> mi_col even; at (3,3) the 3 & 2 = 2 mask closed it) and
    // the outer col scan must pass (|col_offset -3| <= |max_col_offset| ->
    // mi_col >= 3; at (3,1) max_col_offset clamps to -1 and the outer col
    // candidate is unreachable, giving 50 - the G9 re-derivation's own
    // error, caught by this build). (3,4) satisfies both: mask_row 3,
    // mask_col 4. inter() resets mode, so the NEWMV assert follows inter().
    Grid16 g2;
    g2.inter(2, 4, kLastFrame, kNoneFrame, 5, 7);  // above
    g2.inter(2, 6, kLastFrame, kNoneFrame, 9, 9);  // top-right read: xd->mi[-mi_stride + n8_w] = (2, 4+2)
    g2.cells[2 * 16 + 6].block_mi.mode = NEWMV;
    g2.inter(2, 3, kLastFrame, kNoneFrame, 1, 2);  // top-left
    g2.inter(0, 5, kLastFrame, kNoneFrame, 2, 2);  // outer row-3 candidate (row_mi[1])
    g2.inter(4, 1, kLastFrame, kNoneFrame, 4, 4);  // outer col-3 candidate
    MacroBlockD x2 = g2.xd33();
    x2.mi_col = 4;
    x2.mi = &g2.grid[3 * 16 + 4];
    x2.mb_to_left_edge = -128;
    x2.mb_to_right_edge = 320;
    CHECK(computeInterModeCtxLight(x2, cm, 3, 4, kLastFrame, BLOCK_64X64) == 66);
    // Variant: no nearest match (all intra), one outer row inter ->
    // nearest 0, ref_match 1 -> ctx = 1 | (1 << 4) = 17.
    Grid16 g3;
    g3.inter(0, 4, kLastFrame, kNoneFrame, 2, 2);
    MacroBlockD x3 = g3.xd33();
    CHECK(computeInterModeCtxLight(x3, cm, 3, 3, kLastFrame, BLOCK_64X64) == 17);
}

TEST_CASE("countRefMatch counts matching refs and newmv modes") {
    // adaptive_mv_pred.c:1128-1136.
    BlockModeInfo bmi{};
    bmi.mode = NEWMV;
    bmi.ref_frame[0] = kLastFrame;
    bmi.ref_frame[1] = kNoneFrame;
    std::uint8_t match = 0, newmv = 0;
    countRefMatch(bmi, kLastFrame, &match, &newmv);
    CHECK(match == 1);
    CHECK(newmv == 1);
    bmi.mode = NEARESTMV;
    countRefMatch(bmi, kBwdrefFrame, &match, &newmv);
    CHECK(match == 1);
    CHECK(newmv == 1);
}

// ===========================================================================
// A13: the mvp-table driver
// ===========================================================================

TEST_CASE("generateAv1MvpTable fills per-ref stacks and contexts") {
    // adaptive_mv_pred.c:1329-1405: the ref loop, gm_mv from the (IDENTITY)
    // global motion, setupRefMvList per ref. LAST gets the 3-neighbor stack
    // and ctx 85; LAST_BWD (the {LAST,BWD} pair) matches nothing -> ctx 0
    // and the light scan's compound path fills MAX_MV_REF_CANDIDATES (2) rows
    // from the comp_list: list 0 = the two ref ids, list 1 = the diff
    // fallbacks (the same mvs) - weight 2 each.
    Grid4 g;
    g.inter(0, 1, kLastFrame, kNoneFrame, 5, 7);
    g.inter(1, 0, kLastFrame, kNoneFrame, 3, 4);
    g.inter(0, 3, kLastFrame, kNoneFrame, 9, 9);
    Av1Common cm = g.cm();
    MacroBlockD x = g.xd11();
    WarpedMotionParams gm[kRefFrames];
    for (auto& p : gm) {
        p.wmtype = IDENTITY;
        p.wmmat[0] = 0;
        p.wmmat[1] = 0;
        p.wmmat[2] = 1 << kWarpedModelPrecBits;
        p.wmmat[3] = 0;
        p.wmmat[4] = 0;
        p.wmmat[5] = 1 << kWarpedModelPrecBits;
    }
    MvRefSetup setup{};
    setup.sbSize = BLOCK_64X64;
    setup.useRefFrameMvs = 0;
    setup.allowHighPrecisionMv = 1;
    setup.sb64SqNo4xnGeom = 0;
    setup.tplMvs = nullptr;
    setup.curOrderHint = 0;
    setup.orderHintInfo.enable_order_hint = 0;
    setup.orderHintInfo.order_hint_bits = 4;
    CandidateMv stack[kModeCtxRefFrames][kMaxRefMvStackSize] = {};
    std::int16_t ctx[kModeCtxRefFrames] = {};
    std::uint8_t counts[kModeCtxRefFrames] = {};
    const int refs[3] = {kLastFrame, kBwdrefFrame, kLastBwdFrame};
    generateAv1MvpTable(x, cm, setup, gm, 1, 0, BLOCK_8X8, 4, 4, refs, 3, 1, true, stack, ctx, counts);
    CHECK(counts[kLastFrame] == 3);
    CHECK(stack[kLastFrame][0].this_mv.x == 5);
    CHECK(stack[kLastFrame][0].weight == 4 + kRefCatLevel);
    CHECK(ctx[kLastFrame] == 85);
    CHECK(counts[kLastBwdFrame] == 2);
    CHECK(stack[kLastBwdFrame][0].this_mv.x == 5);
    CHECK(stack[kLastBwdFrame][0].comp_mv.x == 5);
    CHECK(stack[kLastBwdFrame][0].weight == 2);
    CHECK(stack[kLastBwdFrame][1].this_mv.x == 3);
    CHECK(stack[kLastBwdFrame][1].comp_mv.x == 3);
    CHECK(ctx[kLastBwdFrame] == 0);
}

// ===========================================================================
// A14: the DRL / nearest-near assembly
// ===========================================================================

TEST_CASE("getAv1MvPredDrl single-ref selection") {
    // adaptive_mv_pred.c:1407-1457 with the 3-neighbor stack.
    Grid4 g;
    Av1Common cm = g.cm();
    MacroBlockD x = g.xd11();
    CandidateMv stack[kModeCtxRefFrames][kMaxRefMvStackSize] = {};
    stack[kLastFrame][0] = {mvOf(5, 7), Mv{}, 644};
    stack[kLastFrame][1] = {mvOf(3, 4), Mv{}, 644};
    stack[kLastFrame][2] = {mvOf(9, 9), Mv{}, 644};
    x.ref_mv_count[kLastFrame] = 3;
    Mv nearest[2] = {};
    Mv near[2] = {};
    Mv refMv[2] = {};
    getAv1MvPredDrl(stack, x, kLastFrame, 0, NEARESTMV, 0, nearest, near, refMv);
    CHECK(nearest[0].x == 5);
    CHECK(near[0].x == 3);
    CHECK(refMv[0].x == 5);
    // NEWMV with drl 2 reads stack[2] (:1451-1455, count > 1).
    Mv nearest2[2] = {};
    Mv near2[2] = {};
    Mv refMv2[2] = {};
    getAv1MvPredDrl(stack, x, kLastFrame, 0, NEWMV, 2, nearest2, near2, refMv2);
    CHECK(nearest2[0].x == 5);
    CHECK(refMv2[0].x == 9);
    // GLOBALMV leaves the caller's nearestmv untouched (:1413 gate).
    Mv nearest3[2] = {mvOf(1, 1), mvOf(1, 1)};
    Mv near3[2] = {};
    Mv refMv3[2] = {};
    getAv1MvPredDrl(stack, x, kLastFrame, 0, GLOBALMV, 0, nearest3, near3, refMv3);
    CHECK(nearest3[0].x == 1);
    CHECK(refMv3[0].x == 1);
}

TEST_CASE("getAv1MvPredDrl compound selection") {
    // adaptive_mv_pred.c:1419-1449.
    Grid4 g;
    Av1Common cm = g.cm();
    MacroBlockD x = g.xd11();
    CandidateMv stack[kModeCtxRefFrames][kMaxRefMvStackSize] = {};
    stack[kLastBwdFrame][0] = {mvOf(5, 7), mvOf(6, 8), 644};
    stack[kLastBwdFrame][1] = {mvOf(3, 4), mvOf(4, 5), 644};
    stack[kLastBwdFrame][2] = {mvOf(9, 9), mvOf(8, 8), 644};
    x.ref_mv_count[kLastBwdFrame] = 3;
    Mv nearest[2] = {};
    Mv near[2] = {};
    Mv refMv[2] = {};
    getAv1MvPredDrl(stack, x, kLastBwdFrame, 1, NEAREST_NEARESTMV, 0, nearest, near, refMv);
    CHECK(nearest[0].x == 5);
    CHECK(nearest[1].x == 6);
    CHECK(near[0].x == 3);
    CHECK(near[1].x == 4);
    CHECK(refMv[0].x == 5);
    CHECK(refMv[1].x == 6);
    // NEAR_NEWMV drl 1: near = stack[2]; ref_mv[1] = stack[1+1].comp_mv
    // (compound_ref1_mode(NEAR_NEWMV) == NEWMV, inter_prediction.h:345-376).
    Mv nearest2[2] = {};
    Mv near2[2] = {};
    Mv refMv2[2] = {};
    getAv1MvPredDrl(stack, x, kLastBwdFrame, 1, NEAR_NEWMV, 1, nearest2, near2, refMv2);
    CHECK(near2[0].x == 9);
    CHECK(refMv2[1].x == 8);
    // NEW_NEWMV: ref_mv_idx = drl_index (not 1 + drl).
    Mv nearest3[2] = {};
    Mv near3[2] = {};
    Mv refMv3[2] = {};
    getAv1MvPredDrl(stack, x, kLastBwdFrame, 1, NEW_NEWMV, 0, nearest3, near3, refMv3);
    CHECK(refMv3[0].x == 5);
    CHECK(refMv3[1].x == 6);
}

TEST_CASE("getRefMvFromStack and findBestRefMvsFromStack") {
    // adaptive_mv_pred.c:2002-2038.
    Grid4 g;
    Av1Common cm = g.cm();
    MacroBlockD x = g.xd11();
    CandidateMv stack[kModeCtxRefFrames][kMaxRefMvStackSize] = {};
    stack[kLastBwdFrame][0] = {mvOf(5, 7), mvOf(6, 8), 0};
    stack[kLastFrame][0] = {mvOf(5, 7), Mv{}, 0};
    x.ref_mv_count[kLastFrame] = 1;
    const int comp[2] = {kLastFrame, kBwdrefFrame};
    CHECK(getRefMvFromStack(0, comp, 0, stack, x).x == 5);
    CHECK(getRefMvFromStack(1, comp, 0, stack, x).x == 6);
    const int single[2] = {kLastFrame, kNoneFrame};
    CHECK(getRefMvFromStack(0, single, 0, stack, x).x == 5);
    // idx >= count -> INVALID_MV (:2020-2025).
    CHECK(getRefMvFromStack(0, single, 1, stack, x).as_int == kInvalidMv);
    // findBestRefMvsFromStack lowers the precision (allow_hp = 0):
    // {5,7} -> {4,6}, {3,4} -> {2,4}.
    stack[kLastFrame][1] = {mvOf(3, 4), Mv{}, 0};
    x.ref_mv_count[kLastFrame] = 2;
    Mv nearest = mvOf(0, 0);
    Mv near = mvOf(0, 0);
    findBestRefMvsFromStack(stack, x, kLastFrame, &nearest, &near);
    CHECK(nearest.x == 4);
    CHECK(nearest.y == 6);
    CHECK(near.x == 2);
    CHECK(near.y == 4);
}

// ===========================================================================
// A15: warped-motion samples
// ===========================================================================

TEST_CASE("av1FindSamples collects the above/left sample coordinates") {
    // adaptive_mv_pred.c:1594-1750. Above block 16x16 (n4_w = 4) over the
    // 8x8 current block at (1,1): the "<=" branch (:1628-1647) with
    // col_offset = -1 % 4 = -1; record_samples :1594-1605 with bw = 16:
    // x = -1*4 + 16/2 - 1 = 3, y = -1*8 - 1 = -9 -> pts = {24, -72};
    // the left block likewise gives {-72, 24}.
    Grid4 g;
    g.cells[1].bsize = BLOCK_16X16;
    g.inter(0, 1, kLastFrame, kNoneFrame, 2, 3);
    g.cells[4].bsize = BLOCK_16X16;
    g.inter(1, 0, kLastFrame, kNoneFrame, 2, 3);
    Av1Common cm = g.cm();
    MacroBlockD x = g.xd11();
    int pts[kSamplesArraySize] = {};
    int ptsInref[kSamplesArraySize] = {};
    std::uint8_t n = av1FindSamples(cm, BLOCK_64X64, x, kLastFrame, pts, ptsInref);
    CHECK(n == 2);
    CHECK(pts[0] == 24);
    CHECK(pts[1] == -72);
    CHECK(ptsInref[0] == 26);   // 24 + mv.x
    CHECK(ptsInref[1] == -69);  // -72 + mv.y
    CHECK(pts[2] == -72);
    CHECK(pts[3] == 24);
    CHECK(ptsInref[2] == -70);
    CHECK(ptsInref[3] == 27);
    // An above row of two 4x4s: the ">" branch scans per mi unit
    // (:1648-1666): only the inter one counts. For a 4x4 above block
    // (bw = bh = 4): x = 0*4 + 4/2 - 1 = 1, y = -1*4/2 - 1 = -3 ->
    // pts = {8, -24}.
    Grid4 g2;
    g2.cells[1].bsize = BLOCK_4X4;
    g2.inter(0, 1, kLastFrame, kNoneFrame, 2, 3);
    g2.cells[2].bsize = BLOCK_4X4;
    Av1Common cm2 = g2.cm();
    MacroBlockD x2 = g2.xd11();
    int pts2[kSamplesArraySize] = {};
    int ptsInref2[kSamplesArraySize] = {};
    std::uint8_t n2 = av1FindSamples(cm2, BLOCK_64X64, x2, kLastFrame, pts2, ptsInref2);
    CHECK(n2 == 1);
    CHECK(pts2[0] == 8);
    CHECK(pts2[1] == -24);
}

// ===========================================================================
// A16: overlappable neighbors
// ===========================================================================

TEST_CASE("countOverlappableNbAbove counts inter neighbors above") {
    // adaptive_mv_pred.c:1830-1862.
    Grid4 g;
    g.inter(0, 1, kLastFrame, kNoneFrame, 5, 7);
    Av1Common cm = g.cm();
    MacroBlockD x = g.xd11();
    CHECK(countOverlappableNbAbove(cm, x, 1, 0xFFFFFFFFu) == 1);
    // The 4x4 pair shuffle (:1842-1858): a 4x4 above candidate is treated
    // as the second block of its pair.
    Grid4 g2;
    g2.cells[1].bsize = BLOCK_4X4;
    g2.inter(0, 1, kLastFrame, kNoneFrame, 5, 7);
    g2.cells[2].bsize = BLOCK_4X4;
    MacroBlockD x2 = g2.xd11();
    CHECK(countOverlappableNbAbove(cm, x2, 1, 0xFFFFFFFFu) == 1);
}

TEST_CASE("countOverlappableNeighbors gates on the block size and sums") {
    // adaptive_mv_pred.c:1893-1906 + inter_prediction.h:378-380: blocks
    // narrower than 8 px are never motion-variation candidates -> 0. The
    // 8x8 sum is above (cells[1]) + left (cells[8], two rows down the
    // left column of the block).
    Grid4 g;
    g.inter(0, 1, kLastFrame, kNoneFrame, 5, 7);
    g.inter(1, 0, kLastFrame, kNoneFrame, 3, 4);
    g.inter(2, 0, kLastFrame, kNoneFrame, 8, 8);
    Av1Common cm = g.cm();
    MacroBlockD x = g.xd11();
    CHECK(countOverlappableNeighbors(cm, x, BLOCK_4X4, 1, 1) == 0);
    CHECK(countOverlappableNeighbors(cm, x, BLOCK_8X8, 1, 1) == 2);
}

TEST_CASE("isMotionVariationAllowedBsize and isNeighborOverlappable") {
    // inter_prediction.h:378-380, :242-244.
    CHECK(isMotionVariationAllowedBsize(BLOCK_4X4) == false);
    CHECK(isMotionVariationAllowedBsize(BLOCK_8X8) == true);
    MbModeInfo intra{};
    intra.block_mi.ref_frame[0] = kIntraFrame;
    CHECK(isNeighborOverlappable(intra) == 0);
    MbModeInfo inter{};
    inter.block_mi.ref_frame[0] = kLastFrame;
    CHECK(isNeighborOverlappable(inter) == 1);
}

TEST_CASE("isGlobalMvBlock follows the reference gates") {
    // inter_prediction.h:382-388 (CONFIG_ENABLE_GLOBAL_MOTION = 1 in the
    // default tree): GLOBALMV/GLOBAL_GLOBALMV && type > TRANSLATION &&
    // bsize allows motion variation.
    CHECK(isGlobalMvBlock(GLOBALMV, BLOCK_8X8, ROTZOOM) == 1);
    CHECK(isGlobalMvBlock(GLOBAL_GLOBALMV, BLOCK_16X16, AFFINE) == 1);
    CHECK(isGlobalMvBlock(GLOBALMV, BLOCK_8X8, TRANSLATION) == 0);
    CHECK(isGlobalMvBlock(NEARESTMV, BLOCK_8X8, ROTZOOM) == 0);
    CHECK(isGlobalMvBlock(GLOBALMV, BLOCK_4X4, ROTZOOM) == 0);
}

TEST_CASE("hasOverlappableCandidates reads the threaded counter") {
    // adaptive_mv_pred.h:47-49.
    CHECK(hasOverlappableCandidates(0) == false);
    CHECK(hasOverlappableCandidates(3) == true);
}

// ===========================================================================
// A17: intrabc DV validity
// ===========================================================================

TEST_CASE("isDvValid gates subpel, tile edges and the SB delay") {
    // adaptive_mv_pred.c:1908-2000 over a 64x64-mi tile, block at (32,8),
    // mib_size_log2 = 4 (a 64x64 SB). The staged fixture's (8,8) sat in
    // SB row 0: active_sb64 = 0 makes the delay gate (src_sb64 >=
    // active_sb64 - 4 = -4) reject EVERY DV, so no valid case existed -
    // caught by this build. (32,8) is SB row 2 (active_sb64 = 8), which
    // admits a source two SB rows up.
    Grid16 g;
    MacroBlockD x = g.xd33();
    x.tile.mi_row_end = 64;
    x.tile.mi_col_end = 64;
    x.mi_row = 32;
    x.mi_col = 8;
    // Subpel DVs are rejected (:1913-1917).
    CHECK(isDvValid(mvOf(4, 0), x, 32, 8, BLOCK_8X8, 4) == 0);
    // Above the tile top edge (:1919-1925): src_top = 32*32 - 1032 = -8 < 0.
    CHECK(isDvValid(mvOf(0, -1032), x, 32, 8, BLOCK_8X8, 4) == 0);
    // A valid full-pel DV two SB rows up: edges, delay and both wavefront
    // gates pass (:1919-1997) -> 1. src_bottom = 1088 - 640 = 448 px
    // (SB row 0, src_sb64 = 0 < 8 - 4); wf: 0 >= 0 - 4 + 5*2 = 6 is false;
    // SW wavefront: 0 > 0 + 2 is false.
    CHECK(isDvValid(mvOf(0, -640), x, 32, 8, BLOCK_8X8, 4) == 1);
    // A DV one SB down trips the INTRABC_DELAY_SB64 check (:1965-1976):
    // src_sb64 = 2*4 + 1 = 9 >= active_sb64 - 4 = 4 -> 0.
    CHECK(isDvValid(mvOf(0, 256), x, 32, 8, BLOCK_8X8, 4) == 0);
}

TEST_CASE("isChromaReference follows the subsampled-grid rule") {
    // common_utils.h:315-320.
    CHECK(isChromaReference(0, 0, BLOCK_8X8, 1, 1) == true);
    CHECK(isChromaReference(0, 0, BLOCK_4X4, 1, 1) == false);
    CHECK(isChromaReference(1, 1, BLOCK_4X4, 1, 1) == true);
}

TEST_CASE("checkSbBorder rejects offsets outside the SB") {
    // inter_prediction.h:226-237.
    CHECK(checkSbBorder(1, 1, 2, -2) == 0);
    CHECK(checkSbBorder(1, 1, 2, 2) == 1);
    CHECK(checkSbBorder(0, 0, 0, 0) == 1);
}
