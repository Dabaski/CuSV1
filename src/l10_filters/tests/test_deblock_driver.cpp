#include <cstring>

#include <doctest.h>

#include "deblock_driver.h"

using l10_filters::deblock::LfFrameParams;
using l10_filters::deblock::LfInfoN;

TEST_CASE("loopFilterInit pins combine_vert_horz_lf to 1") {
    LfFrameParams hdr{};
    LfInfoN lfi{};
    l10_filters::deblock::loopFilterInit(hdr, lfi);
    CHECK(hdr.combineVertHorzLf == 1);
}

TEST_CASE("loopFilterInit tiers hev_thr as lvl>>4") {
    // deblocking_filter.c:94-96: hev_thr[lvl] = lvl >> 4, the 64-level
    // four-tier map (0..15 -> 0, 16..31 -> 1, 32..47 -> 2, 48..63 -> 3).
    LfFrameParams hdr{};
    LfInfoN lfi{};
    l10_filters::deblock::loopFilterInit(hdr, lfi);
    CHECK(lfi.thr.lfthr[63].hev_thr[0] == 3);
}

TEST_CASE("loopFilterInit derives the limits through updateSharpness") {
    // deblocking_filter.c:90-91: the sharpness limits come from the landed
    // DF1 updateSharpness (deblocking_common.c:568-589); the oracle is a
    // fresh call of that primitive at the same sharpness level.
    LfFrameParams hdr{};
    LfInfoN lfi{};
    l10_filters::deblock::loopFilterInit(hdr, lfi);
    LfInfoN oracle{};
    l10_filters::updateSharpness(&oracle.thr, 0);
    CHECK(lfi.thr.lfthr[20].lim[0] == oracle.thr.lfthr[20].lim[0]);
}

TEST_CASE("scaledBufferOffset is y*stride+x") {
    // deblocking_filter.c:101-108: the scale branches are commented out in
    // the pinned tree, so the live arithmetic is y * stride + x. The x/y
    // order discriminates a swap (3*64+5 = 197 would be x*stride+y).
    CHECK(l10_filters::deblock::scaledBufferOffset(3, 5, 64) == 323);
}

TEST_CASE("setupPredPlane maps the mi grid to the pixel offset") {
    // deblocking_filter.c:122-128 at subsampling 0: x = 4*miCol = 12 px,
    // y = 4*miRow = 8 px, so buf = src + 8*80 + 12. (BLOCK_8X8 = ordinal 3
    // has mi_size 2, so no odd-mi adjustment would fire even at ss=1.)
    std::uint8_t src[80 * 80] = {};
    l10_filters::deblock::DeblockPlane dst{};
    l10_filters::deblock::setupPredPlane(&dst, 3 /* BLOCK_8X8 */, src, 64, 64, 80, 2, 3, 0, 0, false);
    CHECK(dst.buf == src + 8 * 80 + 12);
    CHECK(dst.width == 64);
    CHECK(dst.height == 64);
    CHECK(dst.stride == 80);
}

TEST_CASE("setupPredPlane pulls the odd-mi chroma origin back one mi unit") {
    // deblocking_filter.c:115-120: with subsampling AND an odd mi unit AND
    // mi_size[bsize] == 1 (BLOCK_4X4 = ordinal 0), mi_row/mi_col step back
    // one unit before the pixel mapping: miCol 3 -> 2 -> x = (4*2)>>1 = 4,
    // miRow 3 -> 2 -> y = (4*2)>>1 = 4.
    std::uint8_t src[80 * 80] = {};
    l10_filters::deblock::DeblockPlane dst{};
    l10_filters::deblock::setupPredPlane(&dst, 0 /* BLOCK_4X4 */, src, 32, 32, 80, 3, 3, 1, 1, false);
    CHECK(dst.buf == src + 4 * 80 + 4);
}

TEST_CASE("setupDstPlanes fills the three planes from the threaded frame") {
    // deblocking_filter.c:145-193: plane 0 gets the full unpadded dims and
    // buffer; planes 1/2 get the >>1 dims and their own buffers, each at
    // the mi-mapped pixel offset for its subsampling.
    std::uint8_t y[80 * 80] = {}, u[40 * 40] = {}, v[40 * 40] = {};
    std::uint8_t* buffers[3] = {y, u, v};
    const int strides[3] = {80, 40, 40};
    l10_filters::deblock::DeblockPlane planes[3] = {};
    planes[0].subsamplingX = 0;
    planes[0].subsamplingY = 0;
    planes[1].subsamplingX = 1;
    planes[1].subsamplingY = 1;
    planes[2].subsamplingX = 1;
    planes[2].subsamplingY = 1;
    l10_filters::deblock::setupDstPlanes(planes, 3 /* BLOCK_8X8 */, buffers, strides, 64, 64, 4, 4, 0, 3);
    CHECK(planes[0].buf == y + 16 * 80 + 16);
    CHECK(planes[0].width == 64);
    CHECK(planes[0].height == 64);
    CHECK(planes[0].stride == 80);
    CHECK(planes[1].buf == u + 8 * 40 + 8);
    CHECK(planes[1].width == 32);
    CHECK(planes[1].height == 32);
    CHECK(planes[1].stride == 40);
    CHECK(planes[2].buf == v + 8 * 40 + 8);
}

TEST_CASE("setupDstPlanes honors the plane window") {
    // deblocking_filter.c:145: only planes in [planeStart, planeEnd) are
    // written; planes outside keep their prior fields.
    std::uint8_t y[80 * 80] = {}, u[40 * 40] = {}, v[40 * 40] = {};
    std::uint8_t* buffers[3] = {y, u, v};
    const int strides[3] = {80, 40, 40};
    l10_filters::deblock::DeblockPlane planes[3] = {};
    planes[0].subsamplingX = 0;
    planes[0].subsamplingY = 0;
    planes[1].subsamplingX = 1;
    planes[1].subsamplingY = 1;
    planes[2].subsamplingX = 1;
    planes[2].subsamplingY = 1;
    l10_filters::deblock::setupDstPlanes(planes, 3, buffers, strides, 64, 64, 0, 0, 1, 2);
    CHECK(planes[0].buf == nullptr);
    CHECK(planes[1].buf == u);
    CHECK(planes[2].buf == nullptr);
}

TEST_CASE("getTransformSize luma: tx_depth and the skip override, direction-mapped") {
    // deblocking_filter.c:200-202 + :210 at BLOCK_8X16 (ordinal 4):
    // depth 0 -> TX_8X16, depth 2 -> TX_4X4; VERT runs the filter
    // horizontally (txsize_horz_map[TX_8X16] = TX_8X8,
    // txsize_horz_map[TX_4X4] = TX_4X4); HORZ -> txsize_vert_map
    // [TX_8X16] = TX_16X16. isSkip forces depth 0 regardless of txDepth.
    l10_filters::deblock::DeblockPlane plane{};
    plane.subsamplingX = 0;
    plane.subsamplingY = 0;
    l10_filters::deblock::DeblockMi mi{};
    mi.bsize = 4;
    mi.txDepth = 0;
    using l10_filters::deblock::HORZ_EDGE;
    using l10_filters::deblock::VERT_EDGE;
    CHECK(l10_filters::deblock::getTransformSize(mi, VERT_EDGE, 0, plane, false) == l10_filters::deblock::TX_8X8);
    CHECK(l10_filters::deblock::getTransformSize(mi, HORZ_EDGE, 0, plane, false) == l10_filters::deblock::TX_16X16);
    mi.txDepth = 2;
    CHECK(l10_filters::deblock::getTransformSize(mi, VERT_EDGE, 0, plane, false) == l10_filters::deblock::TX_4X4);
    CHECK(l10_filters::deblock::getTransformSize(mi, HORZ_EDGE, 0, plane, false) == l10_filters::deblock::TX_4X4);
    CHECK(l10_filters::deblock::getTransformSize(mi, VERT_EDGE, 0, plane, true) == l10_filters::deblock::TX_8X8);
    CHECK(l10_filters::deblock::getTransformSize(mi, HORZ_EDGE, 0, plane, true) == l10_filters::deblock::TX_16X16);
}

TEST_CASE("getTransformSize chroma: max uv tx size, direction-mapped") {
    // deblocking_filter.c:203 + :210 at BLOCK_4X16 (ordinal 16), ss 1/1:
    // plane bsize = ss_lookup[16][1][1] = BLOCK_4X8, blocksize_to_txsize
    // = TX_4X8, av1_get_adjusted_tx_size leaves TX_4X8; horz_map[TX_4X8] =
    // TX_4X4, vert_map[TX_4X8] = TX_8X8.
    l10_filters::deblock::DeblockPlane plane{};
    plane.subsamplingX = 1;
    plane.subsamplingY = 1;
    l10_filters::deblock::DeblockMi mi{};
    mi.bsize = 16;
    using l10_filters::deblock::HORZ_EDGE;
    using l10_filters::deblock::VERT_EDGE;
    CHECK(l10_filters::deblock::getTransformSize(mi, VERT_EDGE, 1, plane, false) == l10_filters::deblock::TX_4X4);
    CHECK(l10_filters::deblock::getTransformSize(mi, HORZ_EDGE, 1, plane, false) == l10_filters::deblock::TX_8X8);
}

TEST_CASE("txsize_horz_map and txsize_vert_map enumerate all 19 shapes") {
    // deblocking_filter.c:34-76, independent transcriptions (TX_4X4=0,
    // TX_8X8=1, TX_16X16=2, TX_32X32=3, TX_64X64=4).
    static const int horzExpect[19] = {0, 1, 2, 3, 4, 0, 1, 1, 2, 2, 3, 3, 4, 0, 2, 1, 3, 2, 4};
    static const int vertExpect[19] = {0, 1, 2, 3, 4, 1, 0, 2, 1, 3, 2, 4, 3, 2, 0, 3, 1, 4, 2};
    for (int ts = 0; ts < 19; ++ts) {
        CHECK(l10_filters::deblock::kTxsizeHorzMap[ts] == horzExpect[ts]);
        CHECK(l10_filters::deblock::kTxsizeVertMap[ts] == vertExpect[ts]);
    }
}

TEST_CASE("av1GetMaxUvTxsize enumerates all 22 bsizes at 4:2:0") {
    // common_utils.h:142-149 over svt_aom_ss_size_lookup (common_utils.c
    // :239-263) at ssx=ssy=1, then blocksize_to_txsize (common_utils.c
    // :127-150) + the adjusted-size clamps (the 64-axis rows fold to the
    // 32-class): the whole 4:2:0 chroma domain, TX ordinals keyed to the
    // deblock TxSize enum.
    using namespace l10_filters::deblock;
    static const TxSize expect[22] = {
        TX_4X4, TX_4X4, TX_4X4, TX_4X4, TX_4X8, TX_8X4, TX_8X8, TX_8X16, TX_16X8,
        TX_16X16, TX_16X32, TX_32X16, TX_32X32, TX_32X32, TX_32X32, TX_32X32,
        TX_4X8, TX_8X4, TX_4X16, TX_16X4, TX_8X32, TX_32X8};
    for (int bsize = 0; bsize < 22; ++bsize) {
        CHECK(l10_filters::deblock::av1GetMaxUvTxSize(bsize, 1, 1) == expect[bsize]);
    }
}

TEST_CASE("getFilterLevelDeltaLf multi selects the plane/dir delta slot") {
    // deblocking_common.c:16 delta_lf_id_lut {{0,1},{2,2},{3,3}} + :50-55:
    // with delta_lf_multi the slot is the LUT cell; base levels are 0 here
    // so the result is the slot value clamped to [0, 63].
    l10_filters::deblock::LfFrameParams hdr{};
    hdr.deltaLfPresent = true;
    hdr.deltaLfMulti = true;
    const std::int32_t sbDeltaLf[4] = {10, 20, 30, 40};
    CHECK(l10_filters::deblock::getFilterLevelDeltaLf(hdr, 0, 0, sbDeltaLf, 0, 0, 0) == 10);
    CHECK(l10_filters::deblock::getFilterLevelDeltaLf(hdr, 1, 0, sbDeltaLf, 0, 0, 0) == 20);
    CHECK(l10_filters::deblock::getFilterLevelDeltaLf(hdr, 0, 1, sbDeltaLf, 0, 0, 0) == 30);
    CHECK(l10_filters::deblock::getFilterLevelDeltaLf(hdr, 1, 1, sbDeltaLf, 0, 0, 0) == 30);
    CHECK(l10_filters::deblock::getFilterLevelDeltaLf(hdr, 0, 2, sbDeltaLf, 0, 0, 0) == 40);
}

TEST_CASE("getFilterLevelDeltaLf single reads slot 0 with the plane base level") {
    // deblocking_common.c:53-63: the single-delta path reads sb_delta_lf[0]
    // and the base level per plane (dir 0/1 split for luma, U/V chroma).
    l10_filters::deblock::LfFrameParams hdr{};
    hdr.filterLevel[0] = 5;
    hdr.filterLevel[1] = 7;
    hdr.filterLevelU = 3;
    hdr.filterLevelV = 9;
    const std::int32_t sbDeltaLf[4] = {10, 99, 99, 99};
    CHECK(l10_filters::deblock::getFilterLevelDeltaLf(hdr, 0, 0, sbDeltaLf, 0, 0, 0) == 15);
    CHECK(l10_filters::deblock::getFilterLevelDeltaLf(hdr, 1, 0, sbDeltaLf, 0, 0, 0) == 17);
    CHECK(l10_filters::deblock::getFilterLevelDeltaLf(hdr, 0, 1, sbDeltaLf, 0, 0, 0) == 13);
    CHECK(l10_filters::deblock::getFilterLevelDeltaLf(hdr, 0, 2, sbDeltaLf, 0, 0, 0) == 19);
}

TEST_CASE("getFilterLevelDeltaLf mode/ref deltas scale with lvl_seg>>5") {
    // deblocking_common.c:72-79: scale = 1 << (lvl_seg >> 5), so the
    // ref/mode deltas DOUBLE at levels 32..63. refDeltas[1] = 1 and
    // modeDeltas[1] = 2 with predMode 13 (NEARESTMV -> mode_lf_lut[13]=1);
    // an INTRA_FRAME ref adds the ref term but never the mode term.
    l10_filters::deblock::LfFrameParams hdr{};
    hdr.modeRefDeltaEnabled = 1;
    hdr.refDeltas[0] = 3;
    hdr.refDeltas[1] = 1;
    hdr.modeDeltas[0] = 99; // unused: mode_lf_lut[13] = 1 selects [1]
    hdr.modeDeltas[1] = 2;
    const std::int32_t sbDeltaLf[4] = {0, 0, 0, 0};
    hdr.filterLevel[0] = 40;
    CHECK(l10_filters::deblock::getFilterLevelDeltaLf(hdr, 0, 0, sbDeltaLf, 0, 13, 1) == 46);
    hdr.filterLevel[0] = 20;
    CHECK(l10_filters::deblock::getFilterLevelDeltaLf(hdr, 0, 0, sbDeltaLf, 0, 13, 1) == 23);
    CHECK(l10_filters::deblock::getFilterLevelDeltaLf(hdr, 0, 0, sbDeltaLf, 0, 13, 0) == 23);
}

TEST_CASE("getFilterLevelDeltaLf seg feature and the level clamps") {
    // deblocking_common.c:64-70: the seg ALT_LF feature adds then clamps
    // to MAX_LOOP_FILTER (the seg_lvl_lf_lut [plane][dir] cell); the
    // delta side clamps at 0.
    l10_filters::deblock::LfFrameParams hdr{};
    hdr.filterLevel[0] = 40;
    hdr.segmentationEnabled = true;
    hdr.featureEnabled[3][1] = 1; // SEG_LVL_ALT_LF_Y_V at segment 3
    hdr.featureData[3][1] = 30;
    const std::int32_t sbDeltaLf[4] = {0, 0, 0, 0};
    CHECK(l10_filters::deblock::getFilterLevelDeltaLf(hdr, 0, 0, sbDeltaLf, 3, 0, 0) == 63);
    hdr.deltaLfMulti = true;
    // filterLevel[0] is still 40 in this case: -10 + 40 = 30 (no clamp).
    const std::int32_t neg[4] = {-10, 0, 0, 0};
    CHECK(l10_filters::deblock::getFilterLevelDeltaLf(hdr, 0, 0, neg, 0, 0, 0) == 30);
    // A deep negative does reach the floor: -100 + 40 clamps to 0.
    const std::int32_t deepNeg[4] = {-100, 0, 0, 0};
    CHECK(l10_filters::deblock::getFilterLevelDeltaLf(hdr, 0, 0, deepNeg, 0, 0, 0) == 0);
}

TEST_CASE("loopFilterFrameInit fills the flat table and honors the plane gates") {
    // deblocking_common.c:101-116 + :128-131: dir 0 takes filter_level[0],
    // dir 1 filter_level[1]; planes 1/2 are skipped when their level is 0;
    // with BOTH luma levels 0 plane 0 BREAKS, so a live U level (the hdr2
    // case) never lands - that check discriminates break-vs-continue.
    l10_filters::deblock::LfFrameParams hdr{};
    l10_filters::deblock::LfInfoN lfi{};
    hdr.filterLevel[0] = 20;
    hdr.filterLevel[1] = 7;
    l10_filters::deblock::loopFilterFrameInit(hdr, lfi, 0, 3);
    CHECK(lfi.lvl[0][5][0][2][0] == 20);
    CHECK(lfi.lvl[0][0][1][7][1] == 7);
    l10_filters::deblock::LfInfoN lfi2{};
    l10_filters::deblock::LfFrameParams hdr2{};
    hdr2.filterLevelU = 5;
    l10_filters::deblock::loopFilterFrameInit(hdr2, lfi2, 0, 3);
    CHECK(lfi2.lvl[1][0][0][0][0] == 0);
    CHECK(lfi2.thr.lfthr[20].lim[0] == 20); // the :99 sharpness refresh
}

TEST_CASE("loopFilterFrameInit builds the per-ref/per-mode delta table") {
    // deblocking_common.c:132-145: scale = 1 << (lvl_seg >> 5); INTRA only
    // at [ref][0]; refs 1..7 get refDeltas[ref] + modeDeltas[mode].
    l10_filters::deblock::LfFrameParams hdr{};
    l10_filters::deblock::LfInfoN lfi{};
    hdr.filterLevel[0] = 30;
    hdr.modeRefDeltaEnabled = 1;
    hdr.refDeltas[0] = 2;
    hdr.refDeltas[1] = -1;
    hdr.modeDeltas[0] = 1;
    hdr.modeDeltas[1] = 3;
    l10_filters::deblock::loopFilterFrameInit(hdr, lfi, 0, 3);
    CHECK(lfi.lvl[0][0][0][0][0] == 32);
    CHECK(lfi.lvl[0][0][0][1][0] == 30);
    CHECK(lfi.lvl[0][0][0][1][1] == 32);
}

TEST_CASE("loopFilterFrameInit shifts by the seg feature before the fill") {
    // deblocking_common.c:122-126: the seg ALT_LF data shifts lvl_seg with
    // its own clamp, then the flat/delta fill consumes the SHIFTED level.
    l10_filters::deblock::LfFrameParams hdr{};
    l10_filters::deblock::LfInfoN lfi{};
    hdr.filterLevel[0] = 40;
    hdr.segmentationEnabled = true;
    hdr.featureEnabled[2][1] = 1;
    hdr.featureData[2][1] = 50;
    l10_filters::deblock::loopFilterFrameInit(hdr, lfi, 0, 3);
    CHECK(lfi.lvl[0][2][0][5][1] == 63);
    CHECK(lfi.lvl[0][0][0][5][1] == 40);
}

namespace {
l10_filters::deblock::DeblockMi gCells[512];
const l10_filters::deblock::DeblockMi* gPtrs[512];

void gridReset() {
    // 16 mi x 16 mi (the full SB64 extent) plus headroom for the miStride
    // 32 superres fixture (offsets up to 15*32 + 31 = 511).
    for (int i = 0; i < 512; ++i) {
        gCells[i] = l10_filters::deblock::DeblockMi{};
        gCells[i].bsize = 6; // BLOCK_16X16
        gPtrs[i] = &gCells[i];
    }
}

l10_filters::deblock::DeblockContext ctxMake() {
    l10_filters::deblock::DeblockContext ctx{};
    ctx.miGrid = gPtrs;
    ctx.miStride = 16; // 16 mi wide (64 px) so the x=16 tier-14 site is in-row
    return ctx;
}
} // namespace

TEST_CASE("setLpfParameters early returns: bounds, null mbmi, non-edge, coord 0") {
    // deblocking_filter.c:225-230 (out of the plane window -> TX_4X4),
    // :249-251 (NULL mbmi -> TX_INVALID), :257-264 (not a tx boundary ->
    // ts, length 0), :279 (coord 0 skips the prev lookup, length stays 0).
    gridReset();
    l10_filters::deblock::DeblockContext ctx = ctxMake();
    ctx.hdr.filterLevel[0] = 20;
    ctx.lfi.lvl[0][0][0][0][0] = 20;
    l10_filters::deblock::DeblockPlane plane{};
    plane.width = 64;
    plane.height = 64;
    plane.stride = 80;
    l10_filters::deblock::DeblockingParams params{};
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 64, 0, 0, plane) ==
          l10_filters::deblock::TX_4X4);
    CHECK(params.filterLength == 0);
    gPtrs[2 * 16 + 2] = nullptr;
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 8, 8, 0, plane) ==
          l10_filters::deblock::kTxInvalid);
    gridReset();
    // BLOCK_16X16 at depth 0 -> TX_16X16; coord 8 is inside the 16-wide tx
    // (8 & 15 != 0) so no edge fires.
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 8, 0, 0, plane) ==
          l10_filters::deblock::TX_16X16);
    CHECK(params.filterLength == 0);
    // coord 0 (the plane's left edge): the level lookup happens but the
    // prev branch is skipped and the length stays 0.
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 0, 0, 0, plane) ==
          l10_filters::deblock::TX_16X16);
    CHECK(params.filterLength == 0);
}

TEST_CASE("setLpfParameters selects the filter length tier 4/8/14 on luma") {
    // deblocking_filter.c:312-317: min(ts, pv_ts) drives the tier - 4 when
    // min <= TX_4X4, else 8 iff min == TX_8X8, else 14 (luma). VERT edges
    // at the tx boundaries x=4 (TX_4X4 via depth 2), x=8 (TX_8X8 via
    // BLOCK_8X8), x=16 (TX_16X16 via BLOCK_16X16 depth 0).
    gridReset();
    l10_filters::deblock::DeblockContext ctx = ctxMake();
    l10_filters::deblock::LfFrameParams hdr{};
    l10_filters::deblock::LfInfoN lfi{};
    hdr.sharpnessLevel = 0;
    l10_filters::deblock::loopFilterInit(hdr, lfi);
    ctx.hdr = hdr;
    ctx.lfi = lfi;
    ctx.lfi.lvl[0][0][0][0][0] = 20;
    l10_filters::deblock::DeblockPlane plane{};
    plane.width = 64;
    plane.height = 64;
    plane.stride = 80;
    l10_filters::deblock::DeblockingParams params{};
    gCells[0].bsize = 6;
    gCells[0].txDepth = 2;
    gCells[1].bsize = 6;
    gCells[1].txDepth = 2;
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 4, 0, 0, plane) ==
          l10_filters::deblock::TX_4X4);
    CHECK(params.filterLength == 4);
    gridReset();
    gCells[1].bsize = 3; // BLOCK_8X8 depth 0 -> TX_8X8
    gCells[2].bsize = 3;
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 8, 0, 0, plane) ==
          l10_filters::deblock::TX_8X8);
    CHECK(params.filterLength == 8);
    gridReset();
    // BLOCK_16X16 depth 0 -> TX_16X16 on both sides of x=16.
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 16, 0, 0, plane) ==
          l10_filters::deblock::TX_16X16);
    CHECK(params.filterLength == 14);
}

TEST_CASE("setLpfParameters selects 6 on the chroma plane") {
    // deblocking_filter.c:316: plane != 0 forces the 6-tap chroma filter
    // when min(ts, pv_ts) > TX_4X4. At ss 1/1, x=16 maps to the odd chroma
    // mi unit 9; BLOCK_32X32 cells give chroma tx TX_16X16.
    gridReset();
    l10_filters::deblock::DeblockContext ctx = ctxMake();
    l10_filters::deblock::LfFrameParams hdr{};
    l10_filters::deblock::LfInfoN lfi{};
    l10_filters::deblock::loopFilterInit(hdr, lfi);
    ctx.hdr = hdr;
    ctx.lfi = lfi;
    ctx.lfi.lvl[1][0][0][0][0] = 20;
    // The ss=1 odd-mi rule (deblocking_filter.c:239-240) puts the read at
    // mi (1, 9) = offset 25 and the prev at offset 24 - the whole SB grid
    // carries BLOCK_32X32 so the row does not matter.
    for (int i = 0; i < 256; ++i) {
        gCells[i].bsize = 9; // BLOCK_32X32
    }
    l10_filters::deblock::DeblockPlane plane{};
    plane.width = 32;
    plane.height = 32;
    plane.stride = 40;
    plane.subsamplingX = 1;
    plane.subsamplingY = 1;
    l10_filters::deblock::DeblockingParams params{};
    CHECK(l10_filters::deblock::setLpfParameters(&params, 2, ctx, l10_filters::deblock::VERT_EDGE, 16, 0, 1, plane) ==
          l10_filters::deblock::TX_16X16);
    CHECK(params.filterLength == 6);
}

TEST_CASE("setLpfParameters picks the live level and derives the limits") {
    // deblocking_filter.c:320 (level = curr ? curr : pv) + :324-329 (the
    // lfthr[level] scalar lanes). Also pins the lvl indexing axes: the
    // mode slot via mode_lf_lut (mode 13 = NEARESTMV -> slot 1) and the
    // ref axis (refFrame0 1).
    gridReset();
    l10_filters::deblock::DeblockContext ctx = ctxMake();
    l10_filters::deblock::LfFrameParams hdr{};
    l10_filters::deblock::LfInfoN lfi{};
    l10_filters::deblock::loopFilterInit(hdr, lfi);
    ctx.hdr = hdr;
    ctx.lfi = lfi;
    l10_filters::deblock::DeblockPlane plane{};
    plane.width = 64;
    plane.height = 64;
    plane.stride = 80;
    l10_filters::deblock::DeblockingParams params{};
    // curr seg 1 level 0, prev seg 0 level 20 -> level 20.
    ctx.lfi.lvl[0][0][0][0][0] = 20;
    ctx.lfi.lvl[0][1][0][0][0] = 0;
    gCells[3].segmentId = 1;
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 16, 0, 0, plane) ==
          l10_filters::deblock::TX_16X16);
    CHECK(params.filterLength == 14);
    CHECK(params.lim == 20);
    CHECK(params.mblim == 64); // 2*(20+2) + 20 at sharpness 0
    CHECK(params.hevThr == 1); // 20 >> 4
    // Reverse: curr seg 0 level 20, prev seg 1 level 0 -> still level 20.
    gridReset();
    ctx.lfi = lfi;
    ctx.lfi.lvl[0][0][0][0][0] = 20;
    ctx.lfi.lvl[0][1][0][0][0] = 0;
    gCells[4].segmentId = 1;
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 16, 0, 0, plane) ==
          l10_filters::deblock::TX_16X16);
    CHECK(params.filterLength == 14);
    CHECK(params.lim == 20);
    // Both zero: the gate fails, no filter.
    gridReset();
    ctx.lfi = lfi;
    ctx.lfi.lvl[0][0][0][0][0] = 0;
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 16, 0, 0, plane) ==
          l10_filters::deblock::TX_16X16);
    CHECK(params.filterLength == 0);
    // The mode slot: level 10 at [ref][mode_lf_lut[13]=1] reaches the
    // limits.
    gridReset();
    ctx.lfi = lfi;
    ctx.lfi.lvl[0][0][0][0][1] = 10;
    gCells[3].mode = 13; // NEARESTMV -> mode_lf_lut[13] = 1
    gCells[4].mode = 13;
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 16, 0, 0, plane) ==
          l10_filters::deblock::TX_16X16);
    CHECK(params.filterLength == 14);
    CHECK(params.lim == 10);
    // The ref axis: level 15 at [ref 1][slot 0].
    gridReset();
    ctx.lfi = lfi;
    ctx.lfi.lvl[0][0][0][1][0] = 15;
    gCells[3].refFrame0 = 1;
    gCells[4].refFrame0 = 1;
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 16, 0, 0, plane) ==
          l10_filters::deblock::TX_16X16);
    CHECK(params.lim == 15);
}

TEST_CASE("setLpfParameters skip gate: both-skipped filters only PU edges") {
    // deblocking_filter.c:311: (curr||pv) && (!pv_skip || !curr_skipped ||
    // pu_edge). REFERENCE PROPERTY (adjudicated): a skipped block forces
    // tx_depth 0 (:200-202), whose tx width equals the block width for
    // every bsize up to 64 - so for those, a fired tx edge is always a PU
    // edge and the gate never blocks. The ONE luma divergence is the 128
    // family (bsize 13/14/15): depth-0 tx is TX_64X64 inside a 128-wide
    // block, so coord 64 fires the tx edge while pu_edge stays false.
    gridReset();
    l10_filters::deblock::DeblockContext ctx = ctxMake();
    l10_filters::deblock::LfFrameParams hdr{};
    l10_filters::deblock::LfInfoN lfi{};
    l10_filters::deblock::loopFilterInit(hdr, lfi);
    ctx.hdr = hdr;
    ctx.lfi = lfi;
    ctx.lfi.lvl[0][0][0][1][0] = 20; // ref 1 (inter), mode slot 0
    l10_filters::deblock::DeblockPlane plane{};
    plane.width = 128;
    plane.height = 64;
    plane.stride = 160;
    l10_filters::deblock::DeblockingParams params{};
    // x=64 -> mi (0,16): curr gCells[16], prev gCells[15]; both
    // BLOCK_128X128, skipped, inter.
    gCells[15].bsize = 15;
    gCells[15].skip = true;
    gCells[15].refFrame0 = 1;
    gCells[16].bsize = 15;
    gCells[16].skip = true;
    gCells[16].refFrame0 = 1;
    // (a) both skipped, non-PU edge -> no filter.
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 64, 0, 0, plane) ==
          l10_filters::deblock::TX_64X64);
    CHECK(params.filterLength == 0);
    // (b) prev live -> the edge filters; min ts TX_64X64 -> tier 14.
    gCells[15].skip = false;
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 64, 0, 0, plane) ==
          l10_filters::deblock::TX_64X64);
    CHECK(params.filterLength == 14);
    CHECK(params.lim == 20);
    // (c) both skipped again but at the PU edge: coord 128 (plane 256
    // wide) satisfies 128 & 127 == 0, so pu_edge opens the gate.
    plane.width = 256;
    gCells[15].skip = true;
    gCells[31].bsize = 15;
    gCells[31].skip = true;
    gCells[31].refFrame0 = 1;
    gCells[32].bsize = 15;
    gCells[32].skip = true;
    gCells[32].refFrame0 = 1;
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 128, 0, 0, plane) ==
          l10_filters::deblock::TX_64X64);
    CHECK(params.filterLength == 14);
}

TEST_CASE("setLpfParameters delta-lf path replaces the table lookup") {
    // deblocking_filter.c:270-276 + :289-296: with delta_lf_present the
    // level comes from getFilterLevelDeltaLf (currDeltaLf), not the lvl
    // table - both for the current cell and the previous one.
    gridReset();
    l10_filters::deblock::DeblockContext ctx = ctxMake();
    l10_filters::deblock::LfFrameParams hdr{};
    l10_filters::deblock::LfInfoN lfi{};
    l10_filters::deblock::loopFilterInit(hdr, lfi);
    ctx.hdr = hdr;
    ctx.lfi = lfi;
    ctx.hdr.filterLevel[0] = 30;
    ctx.hdr.filterLevel[1] = 30; // the HORZ edge's base level (dir 1)
    ctx.hdr.deltaLfPresent = true;
    const std::int32_t deltaLf[4] = {10, 5, 0, 0};
    ctx.currDeltaLf = deltaLf;
    l10_filters::deblock::DeblockPlane plane{};
    plane.width = 64;
    plane.height = 64;
    plane.stride = 80;
    l10_filters::deblock::DeblockingParams params{};
    gCells[1].bsize = 3; // BLOCK_8X8 -> TX_8X8 at depth 0
    gCells[2].bsize = 3;
    // Single-delta: slot 0 -> curr 30+10 = 40, pv 40 -> tier 8, lim 40.
    CHECK(l10_filters::deblock::setLpfParameters(&params, 1, ctx, l10_filters::deblock::VERT_EDGE, 8, 0, 0, plane) ==
          l10_filters::deblock::TX_8X8);
    CHECK(params.filterLength == 8);
    CHECK(params.lim == 40);
    // Multi: dir 1 (HORZ edge) reads slot 1 -> 30+5 = 35, lim 35. The
    // horizontal read sits at mi (2,0) with prev at mi (1,0).
    ctx.hdr.deltaLfMulti = true;
    gCells[16].bsize = 3;
    gCells[32].bsize = 3;
    CHECK(l10_filters::deblock::setLpfParameters(&params, 16, ctx, l10_filters::deblock::HORZ_EDGE, 0, 8, 0, plane) ==
          l10_filters::deblock::TX_8X8);
    CHECK(params.filterLength == 8);
    CHECK(params.lim == 35);
}

TEST_CASE("filterBlockPlaneVert filters every interior tx boundary (composition)") {
    // deblocking_filter.c:340-461 over a uniform BLOCK_8X8 depth-0 grid:
    // every visit returns TX_8X8 (advance 2 mi = 8 px); the interior
    // boundaries x = 8..56 fire with length 8 and the lfthr[20] limits
    // (lim 20, mblim 64, thresh 1); coord 0 and the OOB tail do not
    // filter. The expected buffer is the per-edge composition: the landed
    // lpfVertical8 at rows 4b..4b+7 for b = 0..15 (row_step = 1 mi = 4 px
    // bands overlap by 4 rows; the walk applies them in band order) at
    // each visited column. Input: a soft 4-unit step at the x=32 boundary
    // plus a small intra-block ramp so a spurious edge would diverge.
    gridReset();
    for (int i = 0; i < 256; ++i) {
        gCells[i].bsize = 3; // BLOCK_8X8
    }
    l10_filters::deblock::DeblockContext ctx = ctxMake();
    l10_filters::deblock::LfFrameParams hdr{};
    l10_filters::deblock::LfInfoN lfi{};
    l10_filters::deblock::loopFilterInit(hdr, lfi);
    ctx.hdr = hdr;
    ctx.lfi = lfi;
    ctx.lfi.lvl[0][0][0][0][0] = 20;
    ctx.sbSizePx = 64;
    static std::uint8_t src[80 * 80];
    static std::uint8_t got[80 * 80];
    static std::uint8_t expected[80 * 80];
    for (int r = 0; r < 80; ++r) {
        for (int c = 0; c < 80; ++c) {
            const std::uint8_t v =
                c < 64 ? static_cast<std::uint8_t>((c < 32 ? 100 : 96) + (c & 7)) : static_cast<std::uint8_t>(r + c);
            src[r * 80 + c] = v;
            got[r * 80 + c] = v;
            expected[r * 80 + c] = v;
        }
    }
    l10_filters::deblock::DeblockPlane plane{};
    plane.buf = got;
    plane.width = 64;
    plane.height = 64;
    plane.stride = 80;
    l10_filters::deblock::filterBlockPlaneVert(ctx, 0, plane, 0, 0);
    for (int b = 0; b < 16; ++b) {
        for (int col = 8; col < 64; col += 8) {
            l10_filters::lpfVertical8(expected + b * 4 * 80 + col, 80, 64, 20, 1);
        }
    }
    CHECK(std::memcmp(got, expected, sizeof(got)) == 0);
    CHECK(got[10 * 80 + 30] != src[10 * 80 + 30]); // the step was smoothed
}

namespace {
// Fills a plane buffer with the vert-test pattern (soft step at the
// x=stepCol boundary + intra-block ramp; padding = r+c beyond width).
void fillVertPattern(std::uint8_t* buf, int width, int stride, int stepCol, int base0, int base1) {
    for (int r = 0; r < stride; ++r) {
        for (int c = 0; c < stride; ++c) {
            buf[r * stride + c] =
                c < width ? static_cast<std::uint8_t>((c < stepCol ? base0 : base1) + (c & 7))
                          : static_cast<std::uint8_t>(r + c);
        }
    }
}
} // namespace

TEST_CASE("filterBlockPlaneVert advances by the returned ts unit (tier 4 walk)") {
    // deblocking_filter.c:454-458: advance_units = eb_tx_size_wide_unit
    // [tx_size]. Depth-2 BLOCK_16X16 cells return TX_4X4 -> 1 mi (4 px)
    // per visit, so EVERY 4-px column is visited and the tier-4 filter
    // fires at each interior 4-boundary. A wrong advance (e.g. a fixed 2)
    // would skip the x=4 boundary and leave cols 2..5 unfiltered.
    gridReset();
    for (int i = 0; i < 256; ++i) {
        gCells[i].bsize = 6;
        gCells[i].txDepth = 2;
    }
    l10_filters::deblock::DeblockContext ctx = ctxMake();
    l10_filters::deblock::LfFrameParams hdr{};
    l10_filters::deblock::LfInfoN lfi{};
    l10_filters::deblock::loopFilterInit(hdr, lfi);
    ctx.hdr = hdr;
    ctx.lfi = lfi;
    ctx.lfi.lvl[0][0][0][0][0] = 20;
    ctx.sbSizePx = 64;
    static std::uint8_t src[80 * 80];
    static std::uint8_t got[80 * 80];
    static std::uint8_t expected[80 * 80];
    fillVertPattern(src, 64, 80, 4, 100, 96);
    std::memcpy(got, src, sizeof(got));
    std::memcpy(expected, src, sizeof(expected));
    l10_filters::deblock::DeblockPlane plane{};
    plane.buf = got;
    plane.width = 64;
    plane.height = 64;
    plane.stride = 80;
    l10_filters::deblock::filterBlockPlaneVert(ctx, 0, plane, 0, 0);
    for (int b = 0; b < 16; ++b) {
        for (int col = 4; col < 64; col += 4) {
            l10_filters::lpfVertical4(expected + b * 4 * 80 + col, 80, 64, 20, 1);
        }
    }
    CHECK(std::memcmp(got, expected, sizeof(got)) == 0);
    // The x=4 edge was touched: the 4-column tap span (cols 2..5) differs
    // from the source somewhere in that row.
    CHECK(std::memcmp(got + 10 * 80 + 2, src + 10 * 80 + 2, 4) != 0);
}

TEST_CASE("filterBlockPlaneVert survives NULL cells via the TX_INVALID reset") {
    // deblocking_filter.c:392-395: a TX_INVALID visit resets the length
    // to 0, sets tx_size to TX_4X4, and ADVANCES by the 4x4 unit - the
    // rest of that band shifts phase by 4 px (adjudicated). To keep the
    // oracle exact, the poison sits on the LAST visited column of the
    // band (x=56, mi (2,14) curr / (2,13) prev): the follow-up 4-px visit
    // lands on a non-edge (60 & 7 != 0) and the band ends, so exactly one
    // (band, column) site is lost.
    l10_filters::deblock::LfFrameParams hdr{};
    l10_filters::deblock::LfInfoN lfi{};
    l10_filters::deblock::loopFilterInit(hdr, lfi);
    static std::uint8_t src[80 * 80];
    static std::uint8_t got[80 * 80];
    static std::uint8_t expected[80 * 80];
    fillVertPattern(src, 64, 80, 32, 100, 96);
    // Case 1: NULL current cell at (2,14) - offset 46.
    gridReset();
    for (int i = 0; i < 256; ++i) {
        gCells[i].bsize = 3;
    }
    gPtrs[2 * 16 + 14] = nullptr;
    l10_filters::deblock::DeblockContext ctx = ctxMake();
    ctx.hdr = hdr;
    ctx.lfi = lfi;
    ctx.lfi.lvl[0][0][0][0][0] = 20;
    ctx.sbSizePx = 64;
    std::memcpy(got, src, sizeof(got));
    std::memcpy(expected, src, sizeof(expected));
    l10_filters::deblock::DeblockPlane plane{};
    plane.buf = got;
    plane.width = 64;
    plane.height = 64;
    plane.stride = 80;
    l10_filters::deblock::filterBlockPlaneVert(ctx, 0, plane, 0, 0);
    for (int b = 0; b < 16; ++b) {
        for (int col = 8; col < 64; col += 8) {
            if (b == 2 && col == 56) {
                continue; // the poisoned site
            }
            l10_filters::lpfVertical8(expected + b * 4 * 80 + col, 80, 64, 20, 1);
        }
    }
    CHECK(std::memcmp(got, expected, sizeof(got)) == 0);
    // Case 2: NULL previous cell at (2,13) - offset 45 - poisons the same
    // site through the prev lookup.
    gridReset();
    for (int i = 0; i < 256; ++i) {
        gCells[i].bsize = 3;
    }
    gPtrs[2 * 16 + 13] = nullptr;
    l10_filters::deblock::DeblockContext ctx2 = ctxMake();
    ctx2.hdr = hdr;
    ctx2.lfi = lfi;
    ctx2.lfi.lvl[0][0][0][0][0] = 20;
    ctx2.sbSizePx = 64;
    std::memcpy(got, src, sizeof(got));
    std::memcpy(expected, src, sizeof(expected));
    plane.buf = got;
    l10_filters::deblock::filterBlockPlaneVert(ctx2, 0, plane, 0, 0);
    for (int b = 0; b < 16; ++b) {
        for (int col = 8; col < 64; col += 8) {
            if (b == 2 && col == 56) {
                continue;
            }
            l10_filters::lpfVertical8(expected + b * 4 * 80 + col, 80, 64, 20, 1);
        }
    }
    CHECK(std::memcmp(got, expected, sizeof(got)) == 0);
}

TEST_CASE("filterBlockPlaneVert superres fixup narrows the last partial SB") {
    // deblocking_filter.c:358-368: at miCol == (width/sb*sb)>>2 the x
    // range becomes ((width % sb) + MI_SIZE - 1) >> MI_SIZE_LOG2. With
    // frameWidth 68 the last SB walks ONE mi column (curr_x = 64 only),
    // so the x=68 boundary is never visited even though its cells would
    // fire a tier-4 edge.
    gridReset();
    // miStride 32: bands 8..15 read cells 256..511, so the whole grid
    // carries the depth-2 shape.
    for (int i = 0; i < 512; ++i) {
        gCells[i].bsize = 6;
        gCells[i].txDepth = 2;
    }
    l10_filters::deblock::DeblockContext ctx = ctxMake();
    l10_filters::deblock::LfFrameParams hdr{};
    l10_filters::deblock::LfInfoN lfi{};
    l10_filters::deblock::loopFilterInit(hdr, lfi);
    ctx.hdr = hdr;
    ctx.lfi = lfi;
    ctx.lfi.lvl[0][0][0][0][0] = 20;
    ctx.sbSizePx = 64;
    ctx.superresOrResize = true;
    ctx.frameWidth = 68;
    ctx.frameHeight = 64;
    ctx.miStride = 32; // a 68-px frame has a wider mi stride; offsets stay in the 512-cell grid
    static std::uint8_t src[96 * 96];
    static std::uint8_t got[96 * 96];
    static std::uint8_t expected[96 * 96];
    // Gentle steps (1 unit) at BOTH x=64 and x=68 so every filter4 tap
    // moves at every row (2*|p0-q0| = 16 <= limit 20); stride 96.
    for (int r = 0; r < 96; ++r) {
        for (int c = 0; c < 96; ++c) {
            const std::uint8_t v = c < 68 ? static_cast<std::uint8_t>((c < 64 ? 100 : 99) + (c & 7))
                                          : static_cast<std::uint8_t>(r + c);
            src[r * 96 + c] = v;
            got[r * 96 + c] = v;
            expected[r * 96 + c] = v;
        }
    }
    l10_filters::deblock::DeblockPlane plane{};
    // The SB-LOCAL buf contract (setup_dst_planes -> setup_pred_plane
    // pre-offsets the buffer to the SB's frame origin, deblocking_filter
    // .c:124): the SB at frame x = 64 gets buf = got + 64, while
    // width/height stay the FRAME dims for the bounds check.
    plane.buf = got + 64;
    plane.width = 68;
    plane.height = 64;
    plane.stride = 96;
    l10_filters::deblock::filterBlockPlaneVert(ctx, 0, plane, 0, 16);
    for (int b = 0; b < 16; ++b) {
        l10_filters::lpfVertical4(expected + b * 4 * 96 + 64, 96, 64, 20, 1);
    }
    int firstDiff = -1;
    for (int i = 0; i < 96 * 96; ++i) {
        if (got[i] != expected[i]) {
            firstDiff = i;
            break;
        }
    }
    CHECK(firstDiff == -1);
    // Span probes: the x=64 tap span (cols 62..65) differs from the
    // source somewhere; the x=68 tap span (cols 66..69) is byte-identical
    // - the site was never visited.
    CHECK(std::memcmp(got + 10 * 96 + 62, src + 10 * 96 + 62, 4) != 0);
    CHECK(std::memcmp(got + 10 * 96 + 66, src + 10 * 96 + 66, 4) == 0);
}

namespace {
// The SB-walker fixture: a 128x64 luma frame (two 64x64 SBs), 64x32
// chroma, stride 160/80, a soft vert step at x=64 and a soft horz step
// at y=32 (both 1-unit so every tap moves), tiny intra-block ramps, and
// a uniform BLOCK_8X8 depth-0 grid at miStride 32 (512 cells).
struct SbFixture {
    static constexpr int kLStride = 160;
    static constexpr int kCStride = 80;
    static constexpr int kH = 80;
    std::uint8_t ySrc[kLStride * kH];
    std::uint8_t yGot[kLStride * kH];
    std::uint8_t yExp[kLStride * kH];
    std::uint8_t uSrc[kCStride * 40];
    std::uint8_t uGot[kCStride * 40];
    std::uint8_t vSrc[kCStride * 40];
    std::uint8_t vGot[kCStride * 40];
    std::uint8_t* buffers[3];
    int strides[3];
    l10_filters::deblock::DeblockContext ctx;
    l10_filters::deblock::DeblockPlane pd[3];

    void init() {
        for (int r = 0; r < kH; ++r) {
            for (int c = 0; c < kLStride; ++c) {
                std::uint8_t v;
                if (c < 128) {
                    v = static_cast<std::uint8_t>((c < 64 ? 100 : 99) + (c & 7) + (r < 32 ? 0 : -1) + (r & 7) * 0);
                } else {
                    v = static_cast<std::uint8_t>(r + c);
                }
                ySrc[r * kLStride + c] = v;
            }
        }
        // the horz step: rows >= 32 sit one lower - apply after the base
        for (int r = 0; r < kH; ++r) {
            for (int c = 0; c < 128; ++c) {
                if (r >= 32) {
                    ySrc[r * kLStride + c] = static_cast<std::uint8_t>(ySrc[r * kLStride + c] - 1);
                }
            }
        }
        std::memcpy(yGot, ySrc, sizeof(yGot));
        std::memcpy(yExp, ySrc, sizeof(yExp));
        for (int i = 0; i < kCStride * 40; ++i) {
            uSrc[i] = static_cast<std::uint8_t>(i & 0xFF);
            uGot[i] = uSrc[i];
            vSrc[i] = static_cast<std::uint8_t>((i * 7) & 0xFF);
            vGot[i] = vSrc[i];
        }
        buffers[0] = yGot;
        buffers[1] = uGot;
        buffers[2] = vGot;
        strides[0] = kLStride;
        strides[1] = kCStride;
        strides[2] = kCStride;
        gridReset();
        for (int i = 0; i < 512; ++i) {
            gCells[i].bsize = 3; // BLOCK_8X8
        }
        ctx = ctxMake();
        ctx.miStride = 32;
        l10_filters::deblock::LfFrameParams hdr{};
        l10_filters::deblock::LfInfoN lfi{};
        l10_filters::deblock::loopFilterInit(hdr, lfi);
        ctx.hdr = hdr;
        ctx.lfi = lfi;
        // The SB walker's plane gates read the FRAME-HEADER levels
        // (deblocking_filter.c:630-636), distinct from the lvl tables the
        // per-edge derivation consumes - both must be live for a walk.
        ctx.hdr.filterLevel[0] = 20;
        ctx.hdr.filterLevel[1] = 20;
        ctx.lfi.lvl[0][0][0][0][0] = 20;
        ctx.lfi.lvl[0][0][1][0][0] = 20;
        ctx.sbSizePx = 64;
        ctx.frameWidth = 128;
        ctx.frameHeight = 64;
    }
};
} // namespace

TEST_CASE("loopFilterSb walks vert then the left SB's horz then last-col horz") {
    // deblocking_filter.c:639-664 at combine_vert_horz_lf: the SECOND SB
    // (miCol 16, last_col) filters vert(0,16), then horz(0,0) (the LEFT
    // SB), then horz(0,16). The crossing of the two soft edges makes the
    // order observable. The U/V planes stay untouched (their levels 0).
    SbFixture f;
    f.init();
    f.ctx.hdr.combineVertHorzLf = 1;
    l10_filters::deblock::loopFilterSb(f.buffers, f.strides, 8, f.ctx, 0, 16, 0, 3, true);
    // Composition in the walk order: vert(0,16), horz(0,0), horz(0,16).
    for (int b = 0; b < 16; ++b) {
        for (int col = 64; col < 128; col += 8) {
            l10_filters::lpfVertical8(f.yExp + b * 4 * SbFixture::kLStride + col, SbFixture::kLStride, 64, 20, 1);
        }
    }
    for (int col = 0; col < 64; col += 4) {
        for (int row = 8; row < 64; row += 8) {
            l10_filters::lpfHorizontal8(f.yExp + row * SbFixture::kLStride + col, SbFixture::kLStride, 64, 20, 1);
        }
    }
    for (int col = 64; col < 128; col += 4) {
        for (int row = 8; row < 64; row += 8) {
            l10_filters::lpfHorizontal8(f.yExp + row * SbFixture::kLStride + col, SbFixture::kLStride, 64, 20, 1);
        }
    }
    int firstDiff = -1;
    for (int i = 0; i < SbFixture::kLStride * SbFixture::kH; ++i) {
        if (f.yGot[i] != f.yExp[i]) {
            firstDiff = i;
            break;
        }
    }
    CHECK(firstDiff == -1);
    CHECK(std::memcmp(f.uGot, f.uSrc, sizeof(f.uGot)) == 0);
    CHECK(std::memcmp(f.vGot, f.vSrc, sizeof(f.vGot)) == 0);
}

TEST_CASE("loopFilterSb skips the own-SB horz without last_col") {
    // deblocking_filter.c:660-664: the horz(this SB) call runs only on
    // the last SB of the row. Without last_col the right half keeps its
    // horizontal edges unfiltered.
    SbFixture f;
    f.init();
    f.ctx.hdr.combineVertHorzLf = 1;
    l10_filters::deblock::loopFilterSb(f.buffers, f.strides, 8, f.ctx, 0, 16, 0, 3, false);
    for (int b = 0; b < 16; ++b) {
        for (int col = 64; col < 128; col += 8) {
            l10_filters::lpfVertical8(f.yExp + b * 4 * SbFixture::kLStride + col, SbFixture::kLStride, 64, 20, 1);
        }
    }
    for (int col = 0; col < 64; col += 4) {
        for (int row = 8; row < 64; row += 8) {
            l10_filters::lpfHorizontal8(f.yExp + row * SbFixture::kLStride + col, SbFixture::kLStride, 64, 20, 1);
        }
    }
    CHECK(std::memcmp(f.yGot, f.yExp, sizeof(f.yGot)) == 0);
}

TEST_CASE("loopFilterSb first SB has no left neighbor to horz-filter") {
    // deblocking_filter.c:648: mi_col - max_mib_size < 0 skips the left
    // SB's horz walk; SB (0,0) without last_col filters vert only.
    SbFixture f;
    f.init();
    f.ctx.hdr.combineVertHorzLf = 1;
    l10_filters::deblock::loopFilterSb(f.buffers, f.strides, 8, f.ctx, 0, 0, 0, 3, false);
    for (int b = 0; b < 16; ++b) {
        for (int col = 8; col < 64; col += 8) {
            l10_filters::lpfVertical8(f.yExp + b * 4 * SbFixture::kLStride + col, SbFixture::kLStride, 64, 20, 1);
        }
    }
    CHECK(std::memcmp(f.yGot, f.yExp, sizeof(f.yGot)) == 0);
}

TEST_CASE("loopFilterSb breaks at plane 0 when both luma levels are 0") {
    // deblocking_filter.c:630-632: the plane-0 gate is a BREAK - a live
    // U level (5) never reaches its walk, and the luma planes stay
    // untouched too.
    SbFixture f;
    f.init();
    f.ctx.hdr.combineVertHorzLf = 1;
    f.ctx.hdr.filterLevel[0] = 0;
    f.ctx.hdr.filterLevel[1] = 0;
    f.ctx.hdr.filterLevelU = 5;
    f.ctx.hdr.filterLevelV = 0;
    l10_filters::deblock::loopFilterSb(f.buffers, f.strides, 8, f.ctx, 0, 16, 0, 3, true);
    CHECK(std::memcmp(f.yGot, f.ySrc, sizeof(f.yGot)) == 0);
    CHECK(std::memcmp(f.uGot, f.uSrc, sizeof(f.uGot)) == 0);
}

TEST_CASE("loopFilterFrame walks the SB raster with end_of_row flags") {
    // deblocking_filter.c:684-710 over the 128x64 two-SB fixture: the
    // raster is SB(0,0) then SB(0,16, last_col) - the composition is the
    // ordered sequence vert(0,0); vert(0,16); horz(0,0) the left SB;
    // horz(0,16) the last-col SB. loopFilterFrameInit runs FIRST, so the
    // lvl tables are the frame-init flat fill (level 20 from the header,
    // deltas off) - the same values the fixture set by hand.
    SbFixture f;
    f.init();
    f.ctx.hdr.combineVertHorzLf = 1;
    l10_filters::deblock::loopFilterFrame(f.buffers, f.strides, 8, f.ctx, 128, 64, 0, 3, true);
    // SB(0,0), last_col = false: vert only.
    for (int b = 0; b < 16; ++b) {
        for (int col = 8; col < 64; col += 8) {
            l10_filters::lpfVertical8(f.yExp + b * 4 * SbFixture::kLStride + col, SbFixture::kLStride, 64, 20, 1);
        }
    }
    // SB(0,16), last_col = true: vert, then horz(left), then horz(own).
    for (int b = 0; b < 16; ++b) {
        for (int col = 64; col < 128; col += 8) {
            l10_filters::lpfVertical8(f.yExp + b * 4 * SbFixture::kLStride + col, SbFixture::kLStride, 64, 20, 1);
        }
    }
    for (int col = 0; col < 64; col += 4) {
        for (int row = 8; row < 64; row += 8) {
            l10_filters::lpfHorizontal8(f.yExp + row * SbFixture::kLStride + col, SbFixture::kLStride, 64, 20, 1);
        }
    }
    for (int col = 64; col < 128; col += 4) {
        for (int row = 8; row < 64; row += 8) {
            l10_filters::lpfHorizontal8(f.yExp + row * SbFixture::kLStride + col, SbFixture::kLStride, 64, 20, 1);
        }
    }
    int firstDiff = -1;
    for (int i = 0; i < SbFixture::kLStride * SbFixture::kH; ++i) {
        if (f.yGot[i] != f.yExp[i]) {
            firstDiff = i;
            break;
        }
    }
    CHECK(firstDiff == -1);
    CHECK(std::memcmp(f.uGot, f.uSrc, sizeof(f.uGot)) == 0);
}

TEST_CASE("loopFilterFrame runDlf=false gates the whole SB loop") {
    // deblocking_filter.c:691-694 threaded as runDlf: false skips the SB
    // raster entirely (the frame-init refresh still ran - the lvl table
    // side effect is not observable here and the buffers stay untouched).
    SbFixture f;
    f.init();
    f.ctx.hdr.combineVertHorzLf = 1;
    l10_filters::deblock::loopFilterFrame(f.buffers, f.strides, 8, f.ctx, 128, 64, 0, 3, false);
    CHECK(std::memcmp(f.yGot, f.ySrc, sizeof(f.yGot)) == 0);
    CHECK(std::memcmp(f.uGot, f.uSrc, sizeof(f.uGot)) == 0);
}

namespace {
// The probe q values are ac_qlookup_*_QTX cells read from the pinned
// reference (inv_transforms.c:3357+: 8-bit [0]=4 [1]=8 [63]=70 [128]=176
// [255]=1828; 10-bit [63]=92 [255]=7312; 12-bit [63]=865 [255]=29247).
l10_filters::deblock::PickQParams pickParams(bool key, int inRes, int depth) {
    l10_filters::deblock::PickQParams p{};
    p.isKeyFrame = key;
    p.inputResolution = inRes;
    p.boosted = false;
    p.doY = true;
    p.doUv = true;
    p.bitDepth = depth;
    return p;
}
} // namespace

TEST_CASE("pickFilterLevelByQ 8-bit formulas over the probe set") {
    // deblocking_filter.c:1055-1059 at one live ref row (truthy mins):
    // inter RPOT(q*6017+650707, 18) at in_res 0; key
    // RPOT(q*17563-421574, 18); the in_res multiplier
    // inter_frame_multiplier (deblocking_filter.c:28); chroma = guess/2;
    // the negative guess clamps to 0 (key q=4).
    using l10_filters::deblock::pickFilterLevelByQ;
    std::int32_t out[4];
    l10_filters::deblock::RefFilterLevels ref{10, 10, 10, 10};
    l10_filters::deblock::PickQParams p = pickParams(false, 0, 8);
    p.refs = &ref;
    p.refCount = 1;
    pickFilterLevelByQ(p, 4, out);
    CHECK(out[0] == 3);
    CHECK(out[2] == 1);
    pickFilterLevelByQ(p, 70, out);
    CHECK(out[0] == 4);
    CHECK(out[2] == 2);
    pickFilterLevelByQ(p, 176, out);
    CHECK(out[0] == 7);
    CHECK(out[2] == 3);
    pickFilterLevelByQ(p, 1828, out);
    CHECK(out[0] == 44);
    CHECK(out[2] == 22);
    l10_filters::deblock::PickQParams k = pickParams(true, 0, 8);
    k.refs = &ref;
    k.refCount = 1;
    pickFilterLevelByQ(k, 4, out);
    CHECK(out[0] == 0); // the negative guess clamps at the floor
    pickFilterLevelByQ(k, 176, out);
    CHECK(out[0] == 10);
    CHECK(out[2] == 5);
    l10_filters::deblock::PickQParams r3 = pickParams(false, 3, 8); // the 12034 tier
    r3.refs = &ref;
    r3.refCount = 1;
    pickFilterLevelByQ(r3, 70, out);
    CHECK(out[0] == 6);
    CHECK(out[2] == 3);
}

TEST_CASE("pickFilterLevelByQ high-depth formulas and the key -4") {
    // deblocking_filter.c:1060-1075: 10-bit RPOT(q*20723+4060632, 20),
    // 12-bit RPOT(q*20723+16242526, 22); a KEY frame at a high effective
    // depth loses 4 more.
    using l10_filters::deblock::pickFilterLevelByQ;
    std::int32_t out[4];
    l10_filters::deblock::RefFilterLevels ref{10, 10, 10, 10};
    l10_filters::deblock::PickQParams k10 = pickParams(true, 0, 10);
    k10.refs = &ref;
    k10.refCount = 1;
    pickFilterLevelByQ(k10, 92, out);
    CHECK(out[0] == 2);
    CHECK(out[2] == 1);
    pickFilterLevelByQ(k10, 7312, out);
    CHECK(out[0] == 63);
    CHECK(out[2] == 63);
    l10_filters::deblock::PickQParams i10 = pickParams(false, 0, 10);
    i10.refs = &ref;
    i10.refCount = 1;
    pickFilterLevelByQ(i10, 92, out);
    CHECK(out[0] == 6);
    CHECK(out[2] == 3);
    l10_filters::deblock::PickQParams k12 = pickParams(true, 0, 12);
    k12.refs = &ref;
    k12.refCount = 1;
    pickFilterLevelByQ(k12, 865, out);
    CHECK(out[0] == 4);
    CHECK(out[2] == 2);
}

TEST_CASE("pickFilterLevelByQ min-ref/boosted/doY/doUv gating") {
    // deblocking_filter.c:1023-1042 + :1078-1087 + :1089-1103: the min
    // over the single-ref rows gates each output ((min || boosted) ?
    // clamp(guess) : 0); NO refs leaves the MAX_LOOP_FILTER init
    // truthy (the zero-ref quirk); doY/doUv zero the guesses.
    using l10_filters::deblock::pickFilterLevelByQ;
    std::int32_t out[4];
    l10_filters::deblock::PickQParams p = pickParams(false, 0, 8);
    p.refs = nullptr;
    p.refCount = 0;
    pickFilterLevelByQ(p, 1828, out);
    CHECK(out[0] == 44);
    CHECK(out[1] == 44);
    CHECK(out[2] == 22);
    CHECK(out[3] == 22);
    l10_filters::deblock::RefFilterLevels zero{0, 0, 0, 0};
    p.refs = &zero;
    p.refCount = 1;
    pickFilterLevelByQ(p, 1828, out);
    CHECK(out[0] == 0);
    CHECK(out[1] == 0);
    CHECK(out[2] == 0);
    CHECK(out[3] == 0);
    l10_filters::deblock::RefFilterLevels mixed{10, 12, 5, 7};
    p.refs = &mixed;
    p.refCount = 1;
    pickFilterLevelByQ(p, 1828, out);
    CHECK(out[0] == 44);
    CHECK(out[1] == 44);
    CHECK(out[2] == 22);
    CHECK(out[3] == 22);
    l10_filters::deblock::RefFilterLevels pairA{0, 5, 0, 5};
    l10_filters::deblock::RefFilterLevels pairB{10, 3, 10, 3};
    const l10_filters::deblock::RefFilterLevels two[2] = {pairA, pairB};
    p.refs = two;
    p.refCount = 2;
    pickFilterLevelByQ(p, 1828, out);
    CHECK(out[0] == 0);  // min(0, 10) = 0, not boosted -> the zero path
    CHECK(out[1] == 44); // min(5, 3) = 3 -> truthy -> the guess path
    CHECK(out[2] == 0);
    CHECK(out[3] == 22);
    l10_filters::deblock::RefFilterLevels live{10, 10, 10, 10};
    p.boosted = true;
    p.refs = &zero;
    p.refCount = 1;
    pickFilterLevelByQ(p, 1828, out);
    CHECK(out[0] == 44); // 0 || boosted -> the guess path
    p.boosted = false;
    p.doY = false;
    p.refs = &live;
    p.refCount = 1;
    pickFilterLevelByQ(p, 1828, out);
    CHECK(out[0] == 0);
    CHECK(out[1] == 0);
    CHECK(out[2] == 22);
    p.doY = true;
    p.doUv = false;
    pickFilterLevelByQ(p, 1828, out);
    CHECK(out[0] == 44);
    CHECK(out[2] == 0);
    CHECK(out[3] == 0);
}

TEST_CASE("filterBlockPlaneHorz filters every interior tx boundary (composition)") {
    // deblocking_filter.c:467-598 over a uniform BLOCK_8X8 depth-0 grid:
    // the x outer loop visits every mi column (col_step 1), the y inner
    // loop advances 2 mi (8 px) per visit; interior boundaries y = 8..56
    // fire with length 8 and the lfthr[20] limits; coord 0 and the OOB
    // tail do not filter. Expected = the per-edge composition of the
    // landed lpfHorizontal8 at columns c*4 for c = 0..15 and edge rows
    // 8..56 step 8. Input: a soft 4-unit step at the y=32 boundary plus a
    // small intra-block ramp.
    gridReset();
    for (int i = 0; i < 256; ++i) {
        gCells[i].bsize = 3;
    }
    l10_filters::deblock::DeblockContext ctx = ctxMake();
    l10_filters::deblock::LfFrameParams hdr{};
    l10_filters::deblock::LfInfoN lfi{};
    l10_filters::deblock::loopFilterInit(hdr, lfi);
    ctx.hdr = hdr;
    ctx.lfi = lfi;
    ctx.lfi.lvl[0][0][1][0][0] = 20; // HORZ edge (dir 1)
    ctx.sbSizePx = 64;
    static std::uint8_t src[80 * 80];
    static std::uint8_t got[80 * 80];
    static std::uint8_t expected[80 * 80];
    for (int r = 0; r < 80; ++r) {
        for (int c = 0; c < 80; ++c) {
            const std::uint8_t v =
                c < 64 ? static_cast<std::uint8_t>((r < 32 ? 100 : 96) + (r & 7)) : static_cast<std::uint8_t>(r + c);
            src[r * 80 + c] = v;
            got[r * 80 + c] = v;
            expected[r * 80 + c] = v;
        }
    }
    l10_filters::deblock::DeblockPlane plane{};
    plane.buf = got;
    plane.width = 64;
    plane.height = 64;
    plane.stride = 80;
    l10_filters::deblock::filterBlockPlaneHorz(ctx, 0, plane, 0, 0);
    for (int col = 0; col < 64; col += 4) {
        for (int row = 8; row < 64; row += 8) {
            l10_filters::lpfHorizontal8(expected + row * 80 + col, 80, 64, 20, 1);
        }
    }
    CHECK(std::memcmp(got, expected, sizeof(got)) == 0);
    CHECK(got[30 * 80 + 10] != src[30 * 80 + 10]); // the step was smoothed
}
