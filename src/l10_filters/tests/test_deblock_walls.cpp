// A8W1 (the walls rung): the deblocking_filter.c walls :711-:1286 -
// svt_copy_buffer :711-:740 (26 nb), picture_sse_calculations :742-:770
// (26), try_filter_frame :772-:822 (45), search_filter_level :828-:953
// (113), me_based_dlf_skip :954-:1010 (51), svt_av1_pick_filter_level
// :1110-:1286 (158) = 419 nb, plus the disable_dlf_th table :27-:32 (6)
// = 425 nb. The EbPictureBufferDesc / PictureControlSet /
// SequenceControlSet / EbReferenceObject walls are threaded as carriers
// (LfSearchPic / DlfWalls / DlfRefStat); the EB_NEW/EB_DELETE temp-recon
// allocation and the svt_aom_get_recon_pic selector are PARKED (named in
// the header). The search mechanics are pinned with an SSE-callback hook
// driven by the trial level the port writes into the frame header - the
// full walk of every branch is hand-simulated and pinned exactly.
#include <cstring>
#include <vector>

#include <doctest.h>

#include "deblock_driver.h"

namespace {

using l10_filters::deblock::DeblockContext;
using l10_filters::deblock::DeblockMi;
using l10_filters::deblock::DeblockPlane;
using l10_filters::deblock::DlfRefStat;
using l10_filters::deblock::DlfSearchState;
using l10_filters::deblock::DlfWalls;
using l10_filters::deblock::LfFrameParams;
using l10_filters::deblock::LfSearchPic;
using l10_filters::deblock::RefFilterLevels;

int align8(int v) {
    return (v + 7) & ~7;
}

// LfSearchPic + backing store. Buffers are over-allocated (x2) so the
// 16-bit doubled copies stay in bounds.
struct PicMem {
    std::vector<std::uint8_t> y;
    std::vector<std::uint8_t> u;
    std::vector<std::uint8_t> v;
    LfSearchPic pic{};
};

PicMem makePic(int width, int height, int stride, int bitDepth) {
    PicMem m;
    const int rows = align8(height);
    m.y.assign(static_cast<std::size_t>(stride) * rows * 2, 0xCD);
    const int cs = (stride + 1) / 2;
    const int ch = (rows + 1) / 2;
    m.u.assign(static_cast<std::size_t>(cs) * ch * 2, 0xCD);
    m.v.assign(static_cast<std::size_t>(cs) * ch * 2, 0xCD);
    m.pic.bitDepth = bitDepth;
    m.pic.width = static_cast<std::uint16_t>(width);
    m.pic.height = static_cast<std::uint16_t>(height);
    m.pic.maxWidth = static_cast<std::uint16_t>(width);
    m.pic.maxHeight = static_cast<std::uint16_t>(height);
    m.pic.border = 32;
    m.pic.colorFormat = 1; // EB_YUV420
    m.pic.lumaSize = 12345;
    m.pic.chromaSize = 678;
    m.pic.packedFlag = false;
    m.pic.buffer[0] = m.y.data();
    m.pic.buffer[1] = m.u.data();
    m.pic.buffer[2] = m.v.data();
    m.pic.stride[0] = static_cast<std::uint16_t>(stride);
    m.pic.stride[1] = static_cast<std::uint16_t>(cs);
    m.pic.stride[2] = static_cast<std::uint16_t>(cs);
    m.pic.strideBitInc[0] = 3;
    m.pic.strideBitInc[1] = 4;
    m.pic.strideBitInc[2] = 5;
    return m;
}

// The SSE hook driven by the trial level: tryFilterFrame writes the
// candidate level into the frame header BEFORE calling the distortion
// kernel, so a hook reading that slot observes the search's trial levels.
const std::int64_t* g_fTable = nullptr;
const int* g_levelSlot = nullptr;
int g_fCalls = 0;

std::uint64_t fTableSse(const std::uint8_t*, std::uint32_t, std::uint32_t, const std::uint8_t*, std::int32_t,
                        std::uint32_t, std::uint32_t, std::uint32_t) {
    ++g_fCalls;
    int lvl = *g_levelSlot;
    if (lvl < 0) {
        lvl = 0;
    }
    if (lvl > 127) {
        lvl = 127;
    }
    return static_cast<std::uint64_t>(g_fTable[lvl]);
}

struct SseSpy {
    bool called = false;
    const std::uint8_t* in = nullptr;
    std::uint32_t inStride = 0;
    const std::uint8_t* rec = nullptr;
    std::uint32_t recStride = 0;
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    std::uint64_t result = 0;
};
SseSpy g_spy8;
SseSpy g_spy16;

void spyReset() {
    g_spy8 = SseSpy{};
    g_spy16 = SseSpy{};
    g_fCalls = 0;
}

std::uint64_t spySse8(const std::uint8_t* in, std::uint32_t, std::uint32_t inStride, const std::uint8_t* rec,
                      std::int32_t, std::uint32_t recStride, std::uint32_t w, std::uint32_t h) {
    g_spy8.called = true;
    g_spy8.in = in;
    g_spy8.inStride = inStride;
    g_spy8.rec = rec;
    g_spy8.recStride = recStride;
    g_spy8.w = w;
    g_spy8.h = h;
    return g_spy8.result;
}

std::uint64_t spySse16(const std::uint8_t* in, std::uint32_t, std::uint32_t inStride, const std::uint8_t* rec,
                       std::int32_t, std::uint32_t recStride, std::uint32_t w, std::uint32_t h) {
    g_spy16.called = true;
    g_spy16.in = in;
    g_spy16.inStride = inStride;
    g_spy16.rec = rec;
    g_spy16.recStride = recStride;
    g_spy16.w = w;
    g_spy16.h = h;
    return 777;
}

// Independent row-major SSE oracle (8-bit), the distortion the reference
// kernel computes.
std::uint64_t sseOracle8(const std::uint8_t* in, std::uint32_t inOff, std::uint32_t inStride,
                         const std::uint8_t* rec, std::int32_t recOff, std::uint32_t recStride,
                         std::uint32_t w, std::uint32_t h) {
    std::uint64_t sse = 0;
    const std::uint8_t* ip = in + inOff;
    const std::uint8_t* rp = rec + recOff;
    for (std::uint32_t r = 0; r < h; ++r) {
        for (std::uint32_t c = 0; c < w; ++c) {
            const int d = static_cast<int>(ip[c]) - static_cast<int>(rp[c]);
            sse += static_cast<std::uint64_t>(d * d);
        }
        ip += inStride;
        rp += recStride;
    }
    return sse;
}

// A 16x16-mi grid of 4x4 blocks (BLOCK_4X4, tx_depth 2 -> TX_4X4).
DeblockMi gCells[256];
const DeblockMi* gPtrs[256];

void gridReset4() {
    for (int i = 0; i < 256; ++i) {
        gCells[i] = DeblockMi{};
        gCells[i].bsize = 0; // BLOCK_4X4
        gCells[i].txDepth = 2;
        gPtrs[i] = &gCells[i];
    }
}

DeblockContext makeCtx() {
    DeblockContext ctx{};
    ctx.miGrid = gPtrs;
    ctx.miStride = 16;
    ctx.sbSizePx = 64;
    ctx.frameWidth = 64;
    ctx.frameHeight = 64;
    return ctx;
}

DlfWalls makeWalls() {
    DlfWalls w{};
    w.tune = 1; // TUNE_PSNR: neither sharpness branch
    w.sbSizePx = 64;
    w.alignedWidth = 64;
    w.alignedHeight = 64;
    w.inputResolution = 3;
    w.dist8 = &spySse8;
    w.dist16 = &spySse16;
    return w;
}

// A 64x64 16-bit luma plane whose x=8 column edge carries the filter4
// bd-10 absolute rows (cols 6/7 = 520/512, cols 8/9 = 560/568, the rest
// 512); every other boundary is uniform so only x=8 fires.
// ERRATA #12 (A8W1e): the staged fixtures mapped their uint16 rows at
// r * stride BYTES, but the port's - and the reference's (setup_pred_plane
// scales the OFFSET by is_16bit and passes the stride UNSCALED,
// deblocking_filter.c:124/:128) - DeblockPlane.stride semantic is
// ELEMENTS: a 16-bit plane's rows sit stride * 2 bytes apart. The staged
// mapping made the horizontal kernel's row-strided taps alias onto single
// period-columns (the 4-tap degenerated) while the column-addressed
// vertical writes only half-covered the rows. All three hbd fixtures read
// and write at r * stride * 2 bytes now.
PicMem makeHbdVertFixture() {
    PicMem m = makePic(64, 64, 80, 10);
    for (int r = 0; r < 64; ++r) {
        std::uint16_t* row = reinterpret_cast<std::uint16_t*>(m.pic.buffer[0] + r * 80 * 2);
        for (int c = 0; c < 64; ++c) {
            row[c] = 512;
        }
        row[6] = 520;
        row[7] = 512;
        row[8] = 560;
        row[9] = 568;
    }
    return m;
}

} // namespace

TEST_CASE("copyBuffer replicates the header and copies the luma plane") {
    // deblocking_filter.c:711-740: the ten header fields dst<-src, the
    // stride/stride_bit_inc cell, and the row copies.
    PicMem src = makePic(16, 16, 24, 8);
    PicMem dst = makePic(16, 16, 24, 8);
    for (int i = 0; i < 24 * 16; ++i) {
        src.pic.buffer[0][i] = static_cast<std::uint8_t>(i * 7 + 3);
    }
    l10_filters::deblock::copyBuffer(src.pic, dst.pic, 0);
    CHECK(dst.pic.border == 32);
    CHECK(dst.pic.width == 16);
    CHECK(dst.pic.height == 16);
    CHECK(dst.pic.maxWidth == 16);
    CHECK(dst.pic.maxHeight == 16);
    CHECK(dst.pic.bitDepth == 8);
    CHECK(dst.pic.colorFormat == 1);
    CHECK(dst.pic.lumaSize == 12345);
    CHECK(dst.pic.chromaSize == 678);
    CHECK(dst.pic.packedFlag == false);
    CHECK(dst.pic.stride[0] == 24);
    CHECK(dst.pic.strideBitInc[0] == 3);
    for (int r = 0; r < 16; ++r) {
        CHECK(std::memcmp(dst.pic.buffer[0] + r * 24, src.pic.buffer[0] + r * 24, 16) == 0);
    }
}

TEST_CASE("copyBuffer aligns width and height up to multiples of 8") {
    // :724-725: copy_width = ALIGN_POWER_OF_TWO(width, 3), copy_height =
    // ALIGN_POWER_OF_TWO(height, 3) - width 5 copies 8 bytes, height 4
    // copies 8 rows.
    PicMem src = makePic(5, 4, 24, 8);
    PicMem dst = makePic(5, 4, 24, 8);
    for (int i = 0; i < 24 * 8; ++i) {
        src.pic.buffer[0][i] = static_cast<std::uint8_t>(i + 1);
    }
    l10_filters::deblock::copyBuffer(src.pic, dst.pic, 0);
    for (int r = 0; r < 8; ++r) {
        CHECK(std::memcmp(dst.pic.buffer[0] + r * 24, src.pic.buffer[0] + r * 24, 8) == 0);
    }
}

TEST_CASE("copyBuffer halves the chroma copy dims") {
    // :728-731: plane != 0 shifts both copy dims right one.
    PicMem src = makePic(16, 16, 24, 8);
    PicMem dst = makePic(16, 16, 24, 8);
    for (int i = 0; i < 12 * 8; ++i) {
        src.pic.buffer[1][i] = static_cast<std::uint8_t>(i + 9);
    }
    l10_filters::deblock::copyBuffer(src.pic, dst.pic, 1);
    for (int r = 0; r < 8; ++r) {
        CHECK(std::memcmp(dst.pic.buffer[1] + r * 12, src.pic.buffer[1] + r * 12, 8) == 0);
    }
    // The luma plane is untouched by a chroma copy.
    CHECK(dst.pic.buffer[0][0] == 0xCD);
}

TEST_CASE("copyBuffer doubles the 16-bit copy width and stride") {
    // :712 + :724 + :733: is_16bit = bit_depth > EB_EIGHT_BIT doubles the
    // per-row byte count and the row step.
    PicMem src = makePic(8, 8, 16, 16);
    PicMem dst = makePic(8, 8, 16, 16);
    std::uint16_t* s = reinterpret_cast<std::uint16_t*>(src.pic.buffer[0]);
    for (int i = 0; i < 16 * 8; ++i) {
        s[i] = static_cast<std::uint16_t>(i * 3 + 1);
    }
    l10_filters::deblock::copyBuffer(src.pic, dst.pic, 0);
    const std::uint16_t* d = reinterpret_cast<const std::uint16_t*>(dst.pic.buffer[0]);
    // copy_width = ALIGN(8,3) << 1 = 16 bytes = 8 elements; copy_stride =
    // 16 << 1 = 32 bytes = 16 elements.
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            CHECK(d[r * 16 + c] == s[r * 16 + c]);
        }
    }
    CHECK(dst.pic.stride[0] == 16);
    CHECK(dst.pic.bitDepth == 16);
}

TEST_CASE("copyBuffer leaves the destination buffer pointers alone") {
    PicMem src = makePic(8, 8, 16, 8);
    PicMem dst = makePic(8, 8, 16, 8);
    std::uint8_t* const y = dst.pic.buffer[0];
    std::uint8_t* const u = dst.pic.buffer[1];
    l10_filters::deblock::copyBuffer(src.pic, dst.pic, 0);
    CHECK(dst.pic.buffer[0] == y);
    CHECK(dst.pic.buffer[1] == u);
}

TEST_CASE("pictureSseCalculations dispatches the 8-bit kernel with the aligned dims") {
    // deblocking_filter.c:742-770: is_16bit selects the kernel AND the
    // input pic; the chroma dims are aligned >> ss; offsets are 0.
    PicMem input = makePic(64, 48, 80, 8);
    PicMem recon = makePic(64, 48, 80, 8);
    DlfWalls w = makeWalls();
    w.alignedWidth = 64;
    w.alignedHeight = 48;
    w.is16bitPipeline = false;
    w.ssX = 1;
    w.ssY = 1;
    w.inputPic8 = &input.pic;
    w.reconPic8 = &recon.pic;
    spyReset();
    g_spy8.result = 4242;
    const std::uint64_t sse = l10_filters::deblock::pictureSseCalculations(w, recon.pic, 0);
    CHECK(g_spy8.called);
    CHECK(!g_spy16.called);
    CHECK(g_spy8.in == input.pic.buffer[0]);
    CHECK(g_spy8.inStride == 80);
    CHECK(g_spy8.rec == recon.pic.buffer[0]);
    CHECK(g_spy8.recStride == 80);
    CHECK(g_spy8.w == 64);
    CHECK(g_spy8.h == 48);
    CHECK(sse == 4242);
    // Chroma plane: dims >> ss, the chroma buffers.
    spyReset();
    g_spy8.result = 7;
    const std::uint64_t sseU = l10_filters::deblock::pictureSseCalculations(w, recon.pic, 1);
    CHECK(g_spy8.w == 32);
    CHECK(g_spy8.h == 24);
    CHECK(g_spy8.in == input.pic.buffer[1]);
    CHECK(g_spy8.rec == recon.pic.buffer[1]);
    CHECK(sseU == 7);
}

TEST_CASE("pictureSseCalculations picks the 16-bit kernel and input pic") {
    PicMem input8 = makePic(64, 48, 80, 8);
    PicMem input16 = makePic(64, 48, 80, 16);
    PicMem recon16 = makePic(64, 48, 80, 16);
    DlfWalls w = makeWalls();
    w.is16bitPipeline = true;
    w.inputPic8 = &input8.pic;
    w.inputPic16 = &input16.pic;
    w.reconPic16 = &recon16.pic;
    spyReset();
    const std::uint64_t sse = l10_filters::deblock::pictureSseCalculations(w, recon16.pic, 0);
    CHECK(g_spy16.called);
    CHECK(!g_spy8.called);
    CHECK(g_spy16.in == input16.pic.buffer[0]);
    CHECK(g_spy16.rec == recon16.pic.buffer[0]);
    CHECK(sse == 777);
}

TEST_CASE("pictureSseCalculations returns the real plane SSE (oracle)") {
    // An independent row-major SSE oracle stands in for the landed l6
    // kernel: input 4x4 plane {1,2,3,4} vs recon zeros -> 1+4+9+16 = 30.
    // ERRATA #10 (A8W1e, revised): the staged fixture left BOTH planes at
    // makePic's 0xCD fill outside the four written bytes while the comment
    // assumed "input {1,2,3,4} vs recon zeros". Measured evidence: the
    // first build returned 164030 = EXACTLY (1..4 vs 205)^2 (the recon
    // unzeroed), and after zeroing the recon alone 504330 = EXACTLY
    // 30 + 12*205^2 (the input's rows 1-3 still 0xCD) - both values
    // VINDICATE the wall's buffer/stride/dim plumbing and pin the fixture
    // defect. Both planes are zeroed now; the window sees one differing
    // row -> 30.
    PicMem input = makePic(4, 4, 8, 8);
    PicMem recon = makePic(4, 4, 8, 8);
    std::memset(input.pic.buffer[0], 0, 8 * 8 * 2);
    std::memset(recon.pic.buffer[0], 0, 8 * 8 * 2);
    for (int i = 0; i < 4; ++i) {
        input.pic.buffer[0][i] = static_cast<std::uint8_t>(i + 1);
    }
    DlfWalls w = makeWalls();
    w.alignedWidth = 4;
    w.alignedHeight = 4;
    w.inputPic8 = &input.pic;
    w.reconPic8 = &recon.pic;
    w.dist8 = &sseOracle8;
    const std::uint64_t sse = l10_filters::deblock::pictureSseCalculations(w, recon.pic, 0);
    CHECK(sse == 30);
}

TEST_CASE("meBasedDlfSkip returns early on I slices and on the zero threshold") {
    // deblocking_filter.c:955-959 (I_SLICE) and :968-970 (row 0 of
    // disable_dlf_th is all zeros -> use_zero_strength_th == 0).
    DlfWalls w = makeWalls();
    w.intraSlice = true;
    const std::uint32_t sad[2] = {0, 0};
    w.rcMeDistortion = sad;
    w.b64TotalCount = 2;
    bool doY = false, doUv = false;
    l10_filters::deblock::meBasedDlfSkip(w, 100, &doY, &doUv);
    CHECK(doY);
    CHECK(doUv);
    // Zero threshold: strength lvl 0 row is all zeros.
    w.intraSlice = false;
    w.zeroFilterStrengthLvl = 0;
    doY = false;
    doUv = false;
    l10_filters::deblock::meBasedDlfSkip(w, 100, &doY, &doUv);
    CHECK(doY);
    CHECK(doUv);
}

TEST_CASE("meBasedDlfSkip mult arms separate through the zero-strength threshold") {
    // :962-966: mult = hierarchical ? (temporal_layer_index + 1)
    // : boosted ? 1 : leaf ? 3 : 2. With strength lvl 1, in_res 0 the
    // table entry is 100, so the pair (do_y, do_uv) flips at
    // avg >= 100*mult and avg >= 200*mult. avg = 250:
    // mult 1 -> (T,T), mult 2 -> (T,F), mult 3/4 -> (F,F).
    DlfWalls w = makeWalls();
    w.intraSlice = false;
    w.zeroFilterStrengthLvl = 1;
    w.inputResolution = 0;
    w.prevDlfDistTh = 0; // gate open
    const std::uint32_t sad[4] = {250, 250, 250, 250};
    w.rcMeDistortion = sad;
    w.b64TotalCount = 4;
    w.hierarchicalLevels = 0;
    bool doY, doUv;
    // boosted (mult 1)
    w.boosted = true;
    doY = doUv = true;
    l10_filters::deblock::meBasedDlfSkip(w, 0, &doY, &doUv);
    CHECK(doY);
    CHECK(doUv);
    // default (mult 2)
    w.boosted = false;
    w.leaf = false;
    doY = doUv = true;
    l10_filters::deblock::meBasedDlfSkip(w, 0, &doY, &doUv);
    CHECK(doY);
    CHECK(!doUv);
    // leaf (mult 3)
    w.leaf = true;
    doY = doUv = true;
    l10_filters::deblock::meBasedDlfSkip(w, 0, &doY, &doUv);
    CHECK(!doY);
    CHECK(!doUv);
    // hierarchical tl=0 (mult 1) behaves like boosted here.
    w.leaf = false;
    w.hierarchicalLevels = 1;
    w.temporalLayerIndex = 0;
    doY = doUv = true;
    l10_filters::deblock::meBasedDlfSkip(w, 0, &doY, &doUv);
    CHECK(doY);
    CHECK(doUv);
}

TEST_CASE("meBasedDlfSkip the prev_dlf_dist gate scales with mult (3 vs 4)") {
    // :978-1002: prev_dlf_dist averages the valid single-ref rows
    // (dlf_dist_dev >= 0 && tmp_layer_idx <= temporal_layer_index) and
    // the zeroing only applies when prev < prev_dlf_dist_th * mult. With
    // prev 3500 and th 1000, mult 3 stays closed and mult 4 opens.
    DlfWalls w = makeWalls();
    w.intraSlice = false;
    w.zeroFilterStrengthLvl = 1;
    w.inputResolution = 0;
    const std::uint32_t sad[2] = {0, 0};
    w.rcMeDistortion = sad;
    w.b64TotalCount = 2;
    w.hierarchicalLevels = 1;
    const DlfRefStat refs[2] = {{3500, 0}, {0, 9}}; // second row invalid (layer)
    w.dlfRefs = refs;
    w.dlfRefCount = 2;
    w.temporalLayerIndex = 3;
    bool doY, doUv;
    // leaf (mult 3): 3500 < 3000 false -> gate closed despite avg 0.
    // ERRATA #11 (A8W1e): the staged arm set hierarchicalLevels = 1 here,
    // but the reference's mult ternary (:963-:966) puts hierarchical
    // FIRST - with 1 the mult is tl+1 = 4 REGARDLESS of leaf, the gate
    // 3500 < 4000 opens and the zeroing fires (the first build's
    // false/false IS the reference behavior). The leaf arm is reachable
    // only at hierarchical_levels 0.
    w.hierarchicalLevels = 0;
    w.leaf = true;
    doY = doUv = false;
    l10_filters::deblock::meBasedDlfSkip(w, 1000, &doY, &doUv);
    CHECK(doY);
    CHECK(doUv);
    // hierarchical tl=3 (mult 4): 3500 < 4000 -> open -> zeroed.
    w.hierarchicalLevels = 1;
    w.leaf = false;
    doY = doUv = true;
    l10_filters::deblock::meBasedDlfSkip(w, 1000, &doY, &doUv);
    CHECK(!doY);
    CHECK(!doUv);
}

TEST_CASE("meBasedDlfSkip averages only the valid reference rows") {
    // :984-999: dlf_dist_dev >= 0 and tmp_layer_idx <= temporal layer;
    // the average divides by tot_refs only when nonzero.
    DlfWalls w = makeWalls();
    w.intraSlice = false;
    w.zeroFilterStrengthLvl = 1;
    w.inputResolution = 0;
    const std::uint32_t sad[1] = {0};
    w.rcMeDistortion = sad;
    w.b64TotalCount = 1;
    w.hierarchicalLevels = 0;
    w.boosted = true; // mult 1: the gate is prev < 1000*1
    const DlfRefStat refs[3] = {{100, 0}, {-5, 0}, {200, 9}};
    w.dlfRefs = refs;
    w.dlfRefCount = 3;
    w.temporalLayerIndex = 3;
    // Valid rows: 100 only (the -5 dev and the layer-9 row are excluded)
    // -> prev = 100 < 1000 -> open -> zeroed at avg 0.
    bool doY = true, doUv = true;
    l10_filters::deblock::meBasedDlfSkip(w, 1000, &doY, &doUv);
    CHECK(!doY);
    // All rows invalid -> tot_refs 0 -> prev stays 0 -> still open.
    const DlfRefStat bad[2] = {{-1, 0}, {7, 9}};
    w.dlfRefs = bad;
    w.dlfRefCount = 2;
    doY = doUv = true;
    l10_filters::deblock::meBasedDlfSkip(w, 1000, &doY, &doUv);
    CHECK(!doY);
    // A prev above the scaled threshold closes the gate again.
    const DlfRefStat big[1] = {{5000, 0}};
    w.dlfRefs = big;
    w.dlfRefCount = 1;
    doY = doUv = false;
    l10_filters::deblock::meBasedDlfSkip(w, 1000, &doY, &doUv);
    CHECK(doY);
    CHECK(doUv);
}

TEST_CASE("disable_dlf_th enumerates all four rows by seven resolutions") {
    // deblocking_filter.c:29-32, independent transcription.
    static const std::uint32_t expect[4][7] = {{0, 0, 0, 0, 0, 0, 0},
                                               {100, 200, 500, 800, 1000, 1000, 1000},
                                               {900, 1000, 2000, 3000, 4000, 4000, 4000},
                                               {6000, 7000, 8000, 9000, 10000, 10000, 10000}};
    for (int lvl = 0; lvl < 4; ++lvl) {
        for (int res = 0; res < 7; ++res) {
            CHECK(l10_filters::deblock::kDisableDlfTh[lvl][res] == expect[lvl][res]);
        }
    }
}

TEST_CASE("tryFilterFrame filters at the trial level, measures SSE, restores") {
    // deblocking_filter.c:772-822: the trial level lands in the frame
    // header, the landed loopFilterFrame runs on [plane, plane+1), the
    // SSE is measured against the input pic, and the temp copy restores
    // the unfiltered recon when any level is nonzero. Fixture: cols 0-7
    // = 100, cols 8+ = 140, level 32 (lim 32, mblim 100, hev_thr 2). The
    // x=8 edge fires the 4-tap: cols 6..9 become 108/115/125/132 (the
    // outer taps move at hev=0 through the RPOT(filter1,1) & ~hev path).
    // Input flat 140: row SSE = 6*1600 + 1024 + 625 + 225 + 64 = 11538,
    // times 64 rows = 738432.
    gridReset4();
    PicMem input = makePic(64, 64, 80, 8);
    PicMem recon = makePic(64, 64, 80, 8);
    PicMem temp = makePic(64, 64, 80, 8);
    for (int r = 0; r < 64; ++r) {
        std::uint8_t* row = recon.pic.buffer[0] + r * 80;
        for (int c = 0; c < 64; ++c) {
            row[c] = c < 8 ? 100 : 140;
        }
        std::memset(input.pic.buffer[0] + r * 80, 140, 64);
        std::memcpy(temp.pic.buffer[0] + r * 80, row, 64);
    }
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.inputPic8 = &input.pic;
    w.reconPic8 = &recon.pic;
    w.is16bitPipeline = false;
    w.dist8 = &sseOracle8;
    LfFrameParams& hdr = ctx.hdr;
    hdr.filterLevel[1] = 7; // preserved by dir 0
    const std::int64_t err = l10_filters::deblock::tryFilterFrame(w, ctx, temp.pic, 32, 0, 0, 0);
    CHECK(err == 738432);
    CHECK(hdr.filterLevel[0] == 32);
    CHECK(hdr.filterLevel[1] == 7);
    // The recon was restored from the temp copy (the unfiltered state).
    CHECK(recon.pic.buffer[0][7] == 100);
    CHECK(recon.pic.buffer[0][8] == 140);
}

TEST_CASE("tryFilterFrame dir 2 writes both luma levels, dir 1 keeps level[0]") {
    gridReset4();
    PicMem input = makePic(64, 64, 80, 8);
    PicMem recon = makePic(64, 64, 80, 8);
    PicMem temp = makePic(64, 64, 80, 8);
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.inputPic8 = &input.pic;
    w.reconPic8 = &recon.pic;
    ctx.hdr.filterLevel[0] = 11;
    ctx.hdr.filterLevel[1] = 13;
    l10_filters::deblock::tryFilterFrame(w, ctx, temp.pic, 40, 0, 0, 2);
    CHECK(ctx.hdr.filterLevel[0] == 40);
    CHECK(ctx.hdr.filterLevel[1] == 40);
    ctx.hdr.filterLevel[0] = 11;
    ctx.hdr.filterLevel[1] = 13;
    l10_filters::deblock::tryFilterFrame(w, ctx, temp.pic, 40, 0, 0, 1);
    CHECK(ctx.hdr.filterLevel[0] == 11);
    CHECK(ctx.hdr.filterLevel[1] == 40);
}

TEST_CASE("tryFilterFrame chroma writes only the plane level and filters plane 1") {
    // plane 1: filter_level_u is the slot; the walk runs on [1,2) so the
    // luma buffer is untouched. Chroma 32x32 (aligned 64 >> ss 1), cols
    // 0-3 = 100 / cols 4+ = 140, level 32: the x=4 edge fires the 4-tap
    // (min(tx, pv_tx) == TX_4X4 even on chroma) and cols 2..5 become
    // 108/115/125/132. Row SSE = 2*1600 + 1024 + 625 + 225 + 64 = 5138,
    // times 32 rows = 164416.
    gridReset4();
    PicMem input = makePic(64, 64, 80, 8);
    PicMem recon = makePic(64, 64, 80, 8);
    PicMem temp = makePic(64, 64, 80, 8);
    for (int r = 0; r < 32; ++r) {
        std::uint8_t* row = recon.pic.buffer[1] + r * 40;
        for (int c = 0; c < 32; ++c) {
            row[c] = c < 4 ? 100 : 140;
        }
        std::memset(input.pic.buffer[1] + r * 40, 140, 32);
        std::memcpy(temp.pic.buffer[1] + r * 40, row, 32);
    }
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.inputPic8 = &input.pic;
    w.reconPic8 = &recon.pic;
    // ERRATA #13 (A8W1e): the staged fixture never set w.dist8 - makeWalls
    // installs the SSE spy whose result defaults to 0, so the wall
    // dutifully returned 0 (the first build's got). The oracle is wired.
    // ERRATA #13b: the fixture also never set w.ssX/w.ssY (makeWalls
    // defaults 0), but the reference's chroma window is the SUBSAMPLED
    // aligned dims (deblocking_filter.c:755-756: plane ? aligned >> ss :
    // aligned). At ss=0 the wall computed a 64-wide window over a
    // 40-stride chroma plane: 31 wrapped rows double-count the residues
    // (10,276 each), row 31's wrap lands in the 0xCD over-allocation
    // (5,138), rows 32+ read 0xCD-vs-0xCD - the second build's 323,694 =
    // 31*10276 + 5138 EXACTLY. ss = (1,1) restores the 32x32 window and
    // the staged 164416.
    w.dist8 = &sseOracle8;
    w.ssX = 1;
    w.ssY = 1;
    ctx.hdr.filterLevelU = 3;
    const std::int64_t err = l10_filters::deblock::tryFilterFrame(w, ctx, temp.pic, 32, 0, 1, 0);
    CHECK(ctx.hdr.filterLevelU == 32);
    CHECK(err == 164416);
    // The luma plane was not touched (still the makePic fill).
    CHECK(recon.pic.buffer[0][4] == 0xCD);
    // Restored.
    CHECK(recon.pic.buffer[1][4] == 140);
}

TEST_CASE("tryFilterFrame with both levels zero skips the restore copy") {
    gridReset4();
    PicMem input = makePic(64, 64, 80, 8);
    PicMem recon = makePic(64, 64, 80, 8);
    PicMem temp = makePic(64, 64, 80, 8);
    for (int i = 0; i < 80 * 64; ++i) {
        recon.pic.buffer[0][i] = static_cast<std::uint8_t>(i & 0xFF);
    }
    std::memset(temp.pic.buffer[0], 0xAA, 80 * 64);
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.inputPic8 = &input.pic;
    w.reconPic8 = &recon.pic;
    l10_filters::deblock::tryFilterFrame(w, ctx, temp.pic, 0, 0, 0, 2);
    // No level -> no copy: the recon keeps its own bytes, not the temp's.
    CHECK(recon.pic.buffer[0][0] == 0x00);
    CHECK(recon.pic.buffer[0][5] == 0x05);
}

TEST_CASE("searchFilterLevel seeds per plane and clamps to MAX_LOOP_FILTER") {
    // deblocking_filter.c:843-862: plane 0 dir<=1 takes last[dir], dir>1
    // the rounded average; plane 1/2 take last[2]/last[3]; the mid clamps
    // to [0, 63].
    gridReset4();
    PicMem input = makePic(64, 64, 80, 8);
    PicMem recon = makePic(64, 64, 80, 8);
    PicMem temp = makePic(64, 64, 80, 8);
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.inputPic8 = &input.pic;
    w.reconPic8 = &recon.pic;
    static std::int64_t table[128];
    for (int i = 0; i < 128; ++i) {
        table[i] = 0;
    }
    g_fTable = table;
    g_levelSlot = &ctx.hdr.filterLevel[0];
    w.dist8 = &fTableSse;
    w.earlyExitConvergence = 1;
    const std::int32_t last[4] = {40, 40, 50, 60};
    DlfSearchState state{};
    state.zeroFiltSse = -7;
    state.bestFiltSse = -7;
    CHECK(l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, last, nullptr, 0, 0) == 40);
    CHECK(l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, last, nullptr, 0, 1) == 40);
    CHECK(l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, last, nullptr, 0, 2) == 40);
    CHECK(l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, last, nullptr, 1, 0) == 50);
    CHECK(l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, last, nullptr, 2, 0) == 60);
    // dir>1 averages: (41 + 44 + 1) >> 1 = 43.
    const std::int32_t avg[4] = {41, 44, 0, 0};
    CHECK(l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, avg, nullptr, 0, 2) == 43);
    // Clamping: 200+200+1 >> 1 = 200 -> 63; chroma seeds clamp too.
    const std::int32_t big[4] = {200, 200, 70, 80};
    CHECK(l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, big, nullptr, 0, 2) == 63);
    CHECK(l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, big, nullptr, 1, 0) == 63);
    CHECK(l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, big, nullptr, 2, 0) == 63);
    const std::int32_t neg[4] = {-9, -9, -9, -9};
    CHECK(l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, neg, nullptr, 0, 0) == 0);
}

TEST_CASE("searchFilterLevel constant SSE: convergence exit and the pcs side effects") {
    gridReset4();
    PicMem input = makePic(64, 64, 80, 8);
    PicMem recon = makePic(64, 64, 80, 8);
    PicMem temp = makePic(64, 64, 80, 8);
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.inputPic8 = &input.pic;
    w.reconPic8 = &recon.pic;
    static std::int64_t table[128];
    for (int i = 0; i < 128; ++i) {
        table[i] = 0;
    }
    g_fTable = table;
    g_levelSlot = &ctx.hdr.filterLevel[0];
    w.dist8 = &fTableSse;
    w.earlyExitConvergence = 1;
    const std::int32_t last[4] = {40, 40, 50, 60};
    DlfSearchState state{};
    state.zeroFiltSse = -7;
    state.bestFiltSse = -7;
    double bestCost = -1.0;
    const std::int32_t best = l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, last, &bestCost, 0, 2);
    // Zero SSE everywhere: the low/high branches never accept, the first
    // plateau exits at ee=1, and the seed survives.
    CHECK(best == 40);
    CHECK(bestCost == 0.0);
    // ss_err[0] was never populated (level 0 never tried) -> zero_filt_sse
    // untouched; ss_err[filt_best] = 0 >= 0 -> best_filt_sse written.
    CHECK(state.zeroFiltSse == -7);
    CHECK(state.bestFiltSse == 0);
    // Chroma searches do not touch the pcs state (the plane 0 guard).
    DlfSearchState state2{};
    state2.zeroFiltSse = -7;
    state2.bestFiltSse = -7;
    g_levelSlot = &ctx.hdr.filterLevelU;
    l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state2, 0, last, nullptr, 1, 0);
    CHECK(state2.zeroFiltSse == -7);
    CHECK(state2.bestFiltSse == -7);
}

TEST_CASE("searchFilterLevel walks a decreasing SSE table to 63") {
    // F(L) = (63-L)*(63-L): every high strictly improves (bias stays 0 at
    // these magnitudes), the search walks up to the 63 cap, then
    // converges. Hand-simulated: 40 -> 50 -> 60 -> 63, then plateaus at
    // step 5/2/1 with no acceptance, exit at step 0; the last TRIED
    // level is 62.
    gridReset4();
    PicMem input = makePic(64, 64, 80, 8);
    PicMem recon = makePic(64, 64, 80, 8);
    PicMem temp = makePic(64, 64, 80, 8);
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.inputPic8 = &input.pic;
    w.reconPic8 = &recon.pic;
    static std::int64_t table[128];
    for (int i = 0; i < 128; ++i) {
        table[i] = static_cast<std::int64_t>(63 - i) * (63 - i);
    }
    g_fTable = table;
    g_levelSlot = &ctx.hdr.filterLevel[0];
    w.dist8 = &fTableSse;
    w.earlyExitConvergence = 0;
    w.txModeOnly4x4 = false;
    const std::int32_t last[4] = {40, 40, 50, 60};
    DlfSearchState state{};
    const std::int32_t best = l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, last, nullptr, 0, 2);
    CHECK(best == 63);
    CHECK(state.bestFiltSse == 0);
    // The header keeps the LAST TRIED level, not the best.
    CHECK(ctx.hdr.filterLevel[0] == 62);
    CHECK(ctx.hdr.filterLevel[1] == 62);
}

TEST_CASE("searchFilterLevel walks an increasing SSE table down to 0") {
    // F(L) = L*L: every low strictly improves; the walk 40 -> 30 -> 20 ->
    // 10 -> 0 then plateaus (highs all worse), exiting at step 0; the
    // last tried level is the iter-8 high (1).
    gridReset4();
    PicMem input = makePic(64, 64, 80, 8);
    PicMem recon = makePic(64, 64, 80, 8);
    PicMem temp = makePic(64, 64, 80, 8);
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.inputPic8 = &input.pic;
    w.reconPic8 = &recon.pic;
    static std::int64_t table[128];
    for (int i = 0; i < 128; ++i) {
        table[i] = static_cast<std::int64_t>(i) * i;
    }
    g_fTable = table;
    g_levelSlot = &ctx.hdr.filterLevel[0];
    w.dist8 = &fTableSse;
    w.earlyExitConvergence = 0;
    const std::int32_t last[4] = {40, 40, 50, 60};
    DlfSearchState state{};
    const std::int32_t best = l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, last, nullptr, 0, 2);
    CHECK(best == 0);
    CHECK(state.bestFiltSse == 0);
    CHECK(ctx.hdr.filterLevel[0] == 1);
}

TEST_CASE("searchFilterLevel the bias-toward-lower acceptance and the final best_err") {
    // The quirk chain: ss_err[filt_low] < best_err + bias accepts a level
    // that is NOT better (filt_best moves, best_err does not); at the end
    // best_err = ss_err[filt_best] OVERWRITES the tracked best, and that
    // overwritten value is what lands in pcs->best_filt_sse and
    // best_cost_ret. Table: F[40]=100000 (seed), F[30]=100400 (accepted
    // via the 485 halved bias), F[50]=200000, and every other visited
    // neighbor worse. Hand-simulated walk: 40 -> 30 accepted, then
    // plateaus at 30 through steps 5/2/1, return 30, best_err = 100400;
    // the last tried level is the iter-5 high (31).
    gridReset4();
    PicMem input = makePic(64, 64, 80, 8);
    PicMem recon = makePic(64, 64, 80, 8);
    PicMem temp = makePic(64, 64, 80, 8);
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.inputPic8 = &input.pic;
    w.reconPic8 = &recon.pic;
    static std::int64_t table[128];
    for (int i = 0; i < 128; ++i) {
        table[i] = 150000;
    }
    table[40] = 100000;
    table[30] = 100400;
    table[50] = 200000;
    table[20] = 101000;
    table[25] = 100300;
    table[35] = 100100;
    table[28] = 100200;
    table[32] = 100050;
    table[29] = 100250;
    table[31] = 100050;
    g_fTable = table;
    g_levelSlot = &ctx.hdr.filterLevel[0];
    w.dist8 = &fTableSse;
    w.earlyExitConvergence = 0;
    w.txModeOnly4x4 = false;
    const std::int32_t last[4] = {40, 40, 50, 60};
    DlfSearchState state{};
    state.zeroFiltSse = -7;
    state.bestFiltSse = -7;
    double bestCost = -1.0;
    spyReset();
    const std::int32_t best = l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, last, &bestCost, 0, 2);
    CHECK(best == 30);
    CHECK(bestCost == 100400.0);
    CHECK(state.bestFiltSse == 100400);
    CHECK(state.zeroFiltSse == -7); // level 0 never tried
    // The last tried level was the iter-5 high (31).
    CHECK(ctx.hdr.filterLevel[0] == 31);
    CHECK(ctx.hdr.filterLevel[1] == 31);
    // The ss_err cache: exactly one evaluation per tried level (seed 40;
    // lows 30/20/25/28/29; highs 50/35/32/31).
    CHECK(g_fCalls == 10);
}

TEST_CASE("searchFilterLevel tx_mode ONLY_4X4 doubles the bias (no halving)") {
    // :887-889: the bias halves when tx_mode != ONLY_4X4; with ONLY_4X4
    // the full bias applies. Seed error 100000, step 10, mid 40: bias
    // 970 (halved 485). F[30] = 100500: rejected at 485 (< 100485 false),
    // accepted at 970 (< 100970) -> the ONLY_4X4 case returns 30, the
    // halved case does not move (returns 40 after the ee=1 plateau).
    gridReset4();
    PicMem input = makePic(64, 64, 80, 8);
    PicMem recon = makePic(64, 64, 80, 8);
    PicMem temp = makePic(64, 64, 80, 8);
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.inputPic8 = &input.pic;
    w.reconPic8 = &recon.pic;
    static std::int64_t table[128];
    for (int i = 0; i < 128; ++i) {
        table[i] = 150000;
    }
    table[40] = 100000;
    table[30] = 100500;
    table[50] = 200000;
    g_fTable = table;
    g_levelSlot = &ctx.hdr.filterLevel[0];
    w.dist8 = &fTableSse;
    w.earlyExitConvergence = 1;
    const std::int32_t last[4] = {40, 40, 50, 60};
    DlfSearchState state{};
    w.txModeOnly4x4 = true;
    CHECK(l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, last, nullptr, 0, 2) == 30);
    w.txModeOnly4x4 = false;
    CHECK(l10_filters::deblock::searchFilterLevel(w, ctx, temp.pic, &state, 0, last, nullptr, 0, 2) == 40);
}

TEST_CASE("pickFilterLevel derives the sharpness level") {
    // deblocking_filter.c:1114-1136: CLIP3(0,7); key+VQ adds 2 capped at
    // 7; IQ/MS_SSIM cap by base_q_idx tiers (<=112 -> 7, <=160 -> 1,
    // else 0). Pinned through MINIMAL_LPF, which only writes the luma
    // zeros after the sharpness derivation.
    gridReset4();
    PicMem recon = makePic(64, 64, 80, 8);
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.reconPic8 = &recon.pic;
    w.pickMethod = l10_filters::deblock::LpfPickMinimalLpf;
    w.sharpness = 20;
    l10_filters::deblock::pickFilterLevel(w, ctx, 0);
    CHECK(ctx.hdr.sharpnessLevel == 7);
    w.sharpness = -3;
    l10_filters::deblock::pickFilterLevel(w, ctx, 0);
    CHECK(ctx.hdr.sharpnessLevel == 0);
}

TEST_CASE("pickFilterLevel VQ keyframe raises sharpness by 2 capped at 7") {
    gridReset4();
    PicMem recon = makePic(64, 64, 80, 8);
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.reconPic8 = &recon.pic;
    w.pickMethod = l10_filters::deblock::LpfPickMinimalLpf;
    w.tune = 0; // TUNE_VQ
    w.isKeyFrame = true;
    w.sharpness = 6;
    l10_filters::deblock::pickFilterLevel(w, ctx, 0);
    CHECK(ctx.hdr.sharpnessLevel == 7);
    w.sharpness = 2;
    l10_filters::deblock::pickFilterLevel(w, ctx, 0);
    CHECK(ctx.hdr.sharpnessLevel == 4);
}

TEST_CASE("pickFilterLevel IQ/MS_SSIM caps sharpness by the q tiers") {
    gridReset4();
    PicMem recon = makePic(64, 64, 80, 8);
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.reconPic8 = &recon.pic;
    w.pickMethod = l10_filters::deblock::LpfPickMinimalLpf;
    w.tune = 3; // TUNE_IQ
    w.isKeyFrame = false;
    w.sharpness = 5;
    w.baseQIdx = 100;
    l10_filters::deblock::pickFilterLevel(w, ctx, 0);
    CHECK(ctx.hdr.sharpnessLevel == 5); // max 7 does not bind
    w.baseQIdx = 130;
    l10_filters::deblock::pickFilterLevel(w, ctx, 0);
    CHECK(ctx.hdr.sharpnessLevel == 1);
    w.baseQIdx = 200;
    l10_filters::deblock::pickFilterLevel(w, ctx, 0);
    CHECK(ctx.hdr.sharpnessLevel == 0);
    w.tune = 4; // TUNE_MS_SSIM, same branch
    w.sharpness = 5;
    w.baseQIdx = 100;
    l10_filters::deblock::pickFilterLevel(w, ctx, 0);
    CHECK(ctx.hdr.sharpnessLevel == 5);
}

TEST_CASE("pickFilterLevel MINIMAL_LPF zeroes only the luma levels") {
    // :1137-1138: filter_level[0] = filter_level[1] = 0; u/v untouched.
    gridReset4();
    PicMem recon = makePic(64, 64, 80, 8);
    DeblockContext ctx = makeCtx();
    ctx.hdr.filterLevelU = 9;
    ctx.hdr.filterLevelV = 11;
    DlfWalls w = makeWalls();
    w.reconPic8 = &recon.pic;
    w.pickMethod = l10_filters::deblock::LpfPickMinimalLpf;
    ctx.hdr.filterLevel[0] = 30;
    ctx.hdr.filterLevel[1] = 31;
    l10_filters::deblock::pickFilterLevel(w, ctx, 0);
    CHECK(ctx.hdr.filterLevel[0] == 0);
    CHECK(ctx.hdr.filterLevel[1] == 0);
    CHECK(ctx.hdr.filterLevelU == 9);
    CHECK(ctx.hdr.filterLevelV == 11);
}

TEST_CASE("pickFilterLevel FROM_Q delegates with the me-skip effects") {
    // :1139-1145: pick_by_q over the threaded q; the do_y/do_uv come from
    // me_based_dlf_skip(pcs, 0, ...). No refs -> the min-ref lanes stay
    // 63 so the guess survives; q = 0 at 8-bit inter gives guess
    // RPOT(650707, 18) = 2 and chroma 1. ERRATA (A8W1e): the fixture
    // never set ctx.encoderBitDepth (makeCtx zero-inits it to 0), but
    // by_q switches on the encoder bit depth - without 8 the port hits
    // the assert+return arm (:1066-:1070) and writes nothing. Set to 8.
    gridReset4();
    PicMem recon = makePic(64, 64, 80, 8);
    DeblockContext ctx = makeCtx();
    ctx.encoderBitDepth = 8;
    DlfWalls w = makeWalls();
    w.reconPic8 = &recon.pic;
    w.pickMethod = l10_filters::deblock::LpfPickFromQ;
    w.isKeyFrame = false;
    w.inputResolution = 3;
    w.boosted = false;
    w.zeroFilterStrengthLvl = 0; // me-skip early return: do_y/do_uv true
    w.intraSlice = false;
    const std::uint32_t sad[1] = {9999};
    w.rcMeDistortion = sad;
    w.b64TotalCount = 1;
    l10_filters::deblock::pickFilterLevel(w, ctx, 0);
    CHECK(ctx.hdr.filterLevel[0] == 2);
    CHECK(ctx.hdr.filterLevel[1] == 2);
    CHECK(ctx.hdr.filterLevelU == 1);
    CHECK(ctx.hdr.filterLevelV == 1);
    // The me-skip zeroing: strength lvl 1, in_res 0 (table 100), avg 0
    // < 100*mult -> do_y/do_uv false -> all four levels 0.
    w.zeroFilterStrengthLvl = 1;
    const std::uint32_t zero[1] = {0};
    w.rcMeDistortion = zero;
    l10_filters::deblock::pickFilterLevel(w, ctx, 0);
    CHECK(ctx.hdr.filterLevel[0] == 0);
    CHECK(ctx.hdr.filterLevelU == 0);
}

TEST_CASE("pickFilterLevel DLF search: dlf_avg seeds and the ref-avg chroma branch") {
    // :1173-1203 the dlf_avg average over single-ref rows;
    // :1213-1262 with use_ref_avg_y on, not boosted, and a live
    // prev_dlf_dist (>= 5) the luma keeps the averaged levels and the
    // chroma takes last[2]/last[3]. The F-table hook must never fire
    // (no search runs).
    gridReset4();
    PicMem recon = makePic(64, 64, 80, 8);
    PicMem temp = makePic(64, 64, 80, 8);
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.reconPic8 = &recon.pic;
    w.pickMethod = l10_filters::deblock::LpfPickFromFullImage;
    w.dlfAvg = true;
    static const RefFilterLevels rows[2] = {{20, 30, 10, 12}, {10, 20, 6, 8}};
    w.refLevels = rows;
    w.refLevelCount = 2;
    w.useRefAvgY = true;
    w.useRefAvgUv = true;
    w.boosted = false;
    w.intraSlice = false;
    w.zeroFilterStrengthLvl = 0;
    static const DlfRefStat dlfRefs[1] = {{100, 0}};
    w.dlfRefs = dlfRefs;
    w.dlfRefCount = 1;
    w.temporalLayerIndex = 3;
    static std::int64_t table[128];
    for (int i = 0; i < 128; ++i) {
        table[i] = 0;
    }
    g_fTable = table;
    g_levelSlot = &ctx.hdr.filterLevel[0];
    w.dist8 = &fTableSse;
    spyReset();
    l10_filters::deblock::pickFilterLevel(w, ctx, 0);
    CHECK(ctx.hdr.filterLevel[0] == 15);
    CHECK(ctx.hdr.filterLevel[1] == 25);
    CHECK(ctx.hdr.filterLevelU == 8);
    CHECK(ctx.hdr.filterLevelV == 10);
    CHECK(g_fCalls == 0);
}

TEST_CASE("pickFilterLevel DLF search: the prev_dlf_dist < 5 luma kill") {
    // :1225-1252: with ref avg usable and the refs' DLF improvement under
    // 5, both luma levels drop to 0, and :1254 cascades the chroma to 0.
    gridReset4();
    PicMem recon = makePic(64, 64, 80, 8);
    PicMem temp = makePic(64, 64, 80, 8);
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.reconPic8 = &recon.pic;
    w.pickMethod = l10_filters::deblock::LpfPickFromFullImage;
    w.dlfAvg = true;
    static const RefFilterLevels rows[1] = {{40, 40, 10, 10}};
    w.refLevels = rows;
    w.refLevelCount = 1;
    w.useRefAvgY = true;
    w.useRefAvgUv = true;
    w.boosted = false;
    w.intraSlice = false;
    w.zeroFilterStrengthLvl = 0;
    static const DlfRefStat dlfRefs[1] = {{2, 0}};
    w.dlfRefs = dlfRefs;
    w.dlfRefCount = 1;
    w.temporalLayerIndex = 3;
    static std::int64_t table[128];
    for (int i = 0; i < 128; ++i) {
        table[i] = 0;
    }
    g_fTable = table;
    g_levelSlot = &ctx.hdr.filterLevel[0];
    w.dist8 = &fTableSse;
    l10_filters::deblock::pickFilterLevel(w, ctx, 0);
    CHECK(ctx.hdr.filterLevel[0] == 0);
    CHECK(ctx.hdr.filterLevel[1] == 0);
    CHECK(ctx.hdr.filterLevelU == 0);
    CHECK(ctx.hdr.filterLevelV == 0);
}

TEST_CASE("pickFilterLevel DLF search: boosted luma searches, chroma searches follow") {
    // ERRATA (A8W1e): three staged defects corrected. (1) inputPic8 was
    // never set - the luma search's tryFilterFrame -> SSE call
    // dereferences it (null crash before any assertion); the temp pic is
    // also wired here (the DLF branch reads the walls' tempPic8, errata
    // #7). (2) The staged luma endpoint 60 missed the step-2 refinement:
    // at mid 60 (best_err 9, bias (9>>8)*2 = 0) the high 62 carries
    // F(62) = 1 < 9 and IS accepted; the search returns 62, and the pick
    // driver OVERWRITES the header with the RETURN after the search
    // (:1217) - so the header holds 62/62, not the last TRIED 61 (that
    // last-tried observable belongs to the DIRECT searchFilterLevel
    // tests). (3) The staged chroma expectations (U = 60) assumed the
    // SSE hook observes filterLevelU, but g_levelSlot reads
    // filterLevel[0] - which the chroma searches never write: both
    // chroma walks see the constant table[62] = 1, never accept, and
    // return their seeds (last[2] = last[3] = 0). Verified walk: luma
    // evals = seed + 15 highs (4..60) + 63 + low 58 + high 62 + low 61
    // = 20; each chroma walk = seed + highs 4/6/5 = 4; total 28.
    gridReset4();
    PicMem input = makePic(64, 64, 80, 8);
    PicMem recon = makePic(64, 64, 80, 8);
    PicMem temp = makePic(64, 64, 80, 8);
    DeblockContext ctx = makeCtx();
    DlfWalls w = makeWalls();
    w.inputPic8 = &input.pic;
    w.reconPic8 = &recon.pic;
    w.tempPic8 = &temp.pic;
    w.pickMethod = l10_filters::deblock::LpfPickFromFullImage;
    w.boosted = true;
    w.intraSlice = false;
    w.zeroFilterStrengthLvl = 0;
    w.earlyExitConvergence = 0;
    w.txModeOnly4x4 = false;
    static std::int64_t table[128];
    for (int i = 0; i < 128; ++i) {
        table[i] = static_cast<std::int64_t>(63 - i) * (63 - i);
    }
    table[63] = 1000; // the 63 cap is worse than 62: rejected at mid 60/62
    g_fTable = table;
    g_levelSlot = &ctx.hdr.filterLevel[0];
    w.dist8 = &fTableSse;
    spyReset();
    l10_filters::deblock::pickFilterLevel(w, ctx, 0);
    CHECK(ctx.hdr.filterLevel[0] == 62);
    CHECK(ctx.hdr.filterLevel[1] == 62);
    CHECK(ctx.hdr.filterLevelU == 0);
    CHECK(ctx.hdr.filterLevelV == 0);
    CHECK(g_fCalls == 28);
}

TEST_CASE("dlfSearchPadding adds the superres/resize extra band") {
    // :1148-1151: padding = sb_size + 32, + sb_size when superres or
    // resize is on.
    CHECK(l10_filters::deblock::dlfSearchPadding(64, false) == 96);
    CHECK(l10_filters::deblock::dlfSearchPadding(64, true) == 160);
    CHECK(l10_filters::deblock::dlfSearchPadding(128, false) == 160);
}

TEST_CASE("filterBlockPlaneVert dispatches the hbd 4-tap kernel at bd=10") {
    // The walk's is_16bit branch calls highbd_lpf_vertical_4 with the
    // threaded encoder bit depth. Fixture: every row carries the bd-10
    // absolute rows around the x=8 edge; the walk fires at every 4-row
    // band, so all 64 rows end at (529, 530, 542, 559) in cols 6..9.
    gridReset4();
    PicMem pic = makeHbdVertFixture();
    DeblockPlane plane{};
    plane.buf = pic.pic.buffer[0];
    plane.width = 64;
    plane.height = 64;
    plane.stride = 80;
    plane.subsamplingX = 0;
    plane.subsamplingY = 0;
    plane.is16bit = true;
    DeblockContext ctx = makeCtx();
    ctx.encoderBitDepth = 10;
    ctx.hdr.filterLevel[0] = 32;
    l10_filters::deblock::loopFilterInit(ctx.hdr, ctx.lfi);
    ctx.lfi.lvl[0][0][0][0][0] = 32;
    l10_filters::deblock::filterBlockPlaneVert(ctx, 0, plane, 0, 0);
    for (int r = 0; r < 64; ++r) {
        const std::uint16_t* row = reinterpret_cast<const std::uint16_t*>(pic.pic.buffer[0] + r * 80 * 2);
        CHECK(row[6] == 529);
        CHECK(row[7] == 530);
        CHECK(row[8] == 542);
        CHECK(row[9] == 559);
        CHECK(row[5] == 512);
        CHECK(row[10] == 512);
    }
}

TEST_CASE("filterBlockPlaneHorz dispatches the hbd 4-tap kernel at bd=10") {
    // The horizontal twin: rows 6..9 carry the absolute values; the y=8
    // edge fires across every column.
    gridReset4();
    PicMem pic = makePic(64, 64, 80, 10);
    for (int r = 0; r < 64; ++r) {
        std::uint16_t* row = reinterpret_cast<std::uint16_t*>(pic.pic.buffer[0] + r * 80 * 2);
        for (int c = 0; c < 64; ++c) {
            row[c] = 512;
        }
    }
    for (int c = 0; c < 64; ++c) {
        reinterpret_cast<std::uint16_t*>(pic.pic.buffer[0] + 6 * 80 * 2)[c] = 520;
        reinterpret_cast<std::uint16_t*>(pic.pic.buffer[0] + 7 * 80 * 2)[c] = 512;
        reinterpret_cast<std::uint16_t*>(pic.pic.buffer[0] + 8 * 80 * 2)[c] = 560;
        reinterpret_cast<std::uint16_t*>(pic.pic.buffer[0] + 9 * 80 * 2)[c] = 568;
    }
    DeblockPlane plane{};
    plane.buf = pic.pic.buffer[0];
    plane.width = 64;
    plane.height = 64;
    plane.stride = 80;
    plane.subsamplingX = 0;
    plane.subsamplingY = 0;
    plane.is16bit = true;
    DeblockContext ctx = makeCtx();
    ctx.encoderBitDepth = 10;
    ctx.hdr.filterLevel[0] = 32;
    l10_filters::deblock::loopFilterInit(ctx.hdr, ctx.lfi);
    ctx.lfi.lvl[0][0][0][0][0] = 32;
    // ERRATA #14 (A8W1e): the staged fixture set only the VERT dir slot
    // (lvl[0][0][0][0][0]); the horz walk reads the dir-1 slot
    // lvl[0][0][1][0][0], which stayed 0 -> filter_length 0 -> the
    // unfiltered got values. The horizontal-edge level is set too.
    ctx.lfi.lvl[0][0][1][0][0] = 32;
    l10_filters::deblock::filterBlockPlaneHorz(ctx, 0, plane, 0, 0);
    for (int c = 0; c < 64; ++c) {
        CHECK(reinterpret_cast<const std::uint16_t*>(pic.pic.buffer[0] + 6 * 80 * 2)[c] == 529);
        CHECK(reinterpret_cast<const std::uint16_t*>(pic.pic.buffer[0] + 7 * 80 * 2)[c] == 530);
        CHECK(reinterpret_cast<const std::uint16_t*>(pic.pic.buffer[0] + 8 * 80 * 2)[c] == 542);
        CHECK(reinterpret_cast<const std::uint16_t*>(pic.pic.buffer[0] + 9 * 80 * 2)[c] == 559);
        CHECK(reinterpret_cast<const std::uint16_t*>(pic.pic.buffer[0] + 5 * 80 * 2)[c] == 512);
        CHECK(reinterpret_cast<const std::uint16_t*>(pic.pic.buffer[0] + 10 * 80 * 2)[c] == 512);
    }
}

TEST_CASE("loopFilterSb propagates the storage bit depth into the hbd dispatch") {
    // bitDepth > 8 flips the planes' is_16bit, which routes the walks to
    // the hbd kernels. The fixture is row-uniform so the horizontal pass
    // is a no-op and the expectation equals the vertical walk's.
    gridReset4();
    PicMem pic = makeHbdVertFixture();
    DeblockContext ctx = makeCtx();
    ctx.encoderBitDepth = 10;
    ctx.hdr.filterLevel[0] = 32;
    ctx.hdr.filterLevel[1] = 32;
    l10_filters::deblock::loopFilterInit(ctx.hdr, ctx.lfi);
    ctx.hdr.combineVertHorzLf = 0;
    ctx.lfi.lvl[0][0][0][0][0] = 32;
    std::uint8_t* bufs[3] = {pic.pic.buffer[0], pic.pic.buffer[1], pic.pic.buffer[2]};
    const int strides[3] = {80, 40, 40};
    l10_filters::deblock::loopFilterSb(bufs, strides, 10, ctx, 0, 0, 0, 1, true);
    for (int r = 0; r < 64; ++r) {
        const std::uint16_t* row = reinterpret_cast<const std::uint16_t*>(pic.pic.buffer[0] + r * 80 * 2);
        CHECK(row[6] == 529);
        CHECK(row[7] == 530);
        CHECK(row[8] == 542);
        CHECK(row[9] == 559);
    }
}
