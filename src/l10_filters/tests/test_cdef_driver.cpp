#include <doctest.h>
#include <cstdint>
#include <vector>

#include <cdef_driver.h>
// A8D: the enc_cdef.c / cdef_process.c / rest_process.c driver spans.
// S4: compute_cdef_dist (cdef_process.c:266-:290) and
// svt_pick_cdef_from_qp (enc_cdef.c:823-:873).
// The ac-quant qtx call site (svt_aom_ac_quant_qtx, inv_transforms.c:3484)
// is a wall: the landed table lives in l3_transforms, which l10 does not
// link (the LR3 av1_convolve court-order precedent - no new link lines
// without a court order). It is threaded as the AcQuantQtxFn callback.
// The pick's float quirks pinned below: the SCREEN-CONTENT branch casts
// WITHOUT rounding (:838-:844) while the inter/intra branches roundf
// (:846-:860); clamps y_f1/uv_f1 [0,15], y_f2/uv_f2 [0,3]; pack
// primary * CDEF_SEC_STRENGTHS + secondary.

namespace {

// svt_aom_ac_quant_qtx stand-in: qindex -> 4 * (qindex + 4), a strict
// monotone stand-in with no table behind it (the tests inject it; the
// values they assert are relative to THIS function).
std::int32_t testAcQuantQtx(std::int32_t qIndex, std::int32_t dcOrAc, std::int32_t bitDepth) {
    (void)dcOrAc;
    (void)bitDepth;
    return 4 * (qIndex + 4);
}

} // namespace

// SCREEN branch (truncating casts), q = 100: y_f1 = (int)0.7687... = 0,
// y_f2 = (int)1.4620 = 1, uv_f1 = (int)1.0951 = 1, uv_f2 = (int)1.1674 = 1
// -> pred_y = 0*4+1 = 1, pred_uv = 1*4+1 = 5.
TEST_CASE("pickCdefFromQp screen q=100 truncating pack") {
    l10_filters::cdef_driver::PickQpParams p;
    p.baseQIdx = 21; // testAcQuantQtx(21) = 100
    p.bitDepth = 8;
    p.isKeyFrame = false;
    p.intraOnly = false;
    p.isScreenContent = true;
    p.acQuantQtx = testAcQuantQtx;
    std::int32_t predY = -1, predUv = -1;
    l10_filters::cdef_driver::pickCdefFromQp(p, &predY, &predUv);
    CHECK(predY == 1);
    CHECK(predUv == 5);
}

// INTER branch (roundf), q = 100: y_f1 = roundf(0.6897) = 1, y_f2 =
// roundf(0.1725) = 0, uv_f1 = roundf(0.3481) = 0, uv_f2 = roundf(0.0864) = 0
// -> pred_y = 4, pred_uv = 0.
TEST_CASE("pickCdefFromQp inter q=100 roundf pack") {
    l10_filters::cdef_driver::PickQpParams p;
    p.baseQIdx = 21;
    p.bitDepth = 8;
    p.isKeyFrame = false;
    p.intraOnly = false;
    p.isScreenContent = false;
    p.acQuantQtx = testAcQuantQtx;
    std::int32_t predY = -1, predUv = -1;
    l10_filters::cdef_driver::pickCdefFromQp(p, &predY, &predUv);
    CHECK(predY == 4);
    CHECK(predUv == 0);
}

// INTRA branch (KEY_FRAME), q = 100: y_f1 = roundf(0.8596) = 1, y_f2 =
// roundf(0.3151) = 0, uv_f1 = roundf(1.1510) = 1, uv_f2 = roundf(0.0705) = 0
// -> pred_y = 4, pred_uv = 4.
TEST_CASE("pickCdefFromQp intra q=100 roundf pack") {
    l10_filters::cdef_driver::PickQpParams p;
    p.baseQIdx = 21;
    p.bitDepth = 8;
    p.isKeyFrame = true;
    p.intraOnly = false;
    p.isScreenContent = false;
    p.acQuantQtx = testAcQuantQtx;
    std::int32_t predY = -1, predUv = -1;
    l10_filters::cdef_driver::pickCdefFromQp(p, &predY, &predUv);
    CHECK(predY == 4);
    CHECK(predUv == 4);
}

// INTRA_ONLY_FRAME routes to the intra branch too (the OR gate).
TEST_CASE("pickCdefFromQp intra-only frame uses the intra branch") {
    l10_filters::cdef_driver::PickQpParams p;
    p.baseQIdx = 21;
    p.bitDepth = 8;
    p.isKeyFrame = false;
    p.intraOnly = true;
    p.isScreenContent = false;
    p.acQuantQtx = testAcQuantQtx;
    std::int32_t predY = -1, predUv = -1;
    l10_filters::cdef_driver::pickCdefFromQp(p, &predY, &predUv);
    CHECK(predY == 4);
    CHECK(predUv == 4);
}

// Screen branch q = 3000: y_f1 = (int)71.35 -> clamp 15, y_f2 = (int)-49.5
// -> 0, uv_f1 = (int)-30.2 -> 0, uv_f2 = (int)0.2165 -> 0
// -> pred_y = 15*4 = 60, pred_uv = 0.
TEST_CASE("pickCdefFromQp screen clamps at the AV1 limits") {
    l10_filters::cdef_driver::PickQpParams p;
    p.baseQIdx = 746; // testAcQuantQtx(746) = 3000
    p.bitDepth = 8;
    p.isKeyFrame = false;
    p.intraOnly = false;
    p.isScreenContent = true;
    p.acQuantQtx = testAcQuantQtx;
    std::int32_t predY = -1, predUv = -1;
    l10_filters::cdef_driver::pickCdefFromQp(p, &predY, &predUv);
    CHECK(predY == 60);
    CHECK(predUv == 0);
}

// bd 10: q = acQuantQtx >> 2. Inject 400 (qIndex 96) -> q = 100: the INTER
// branch then matches the q=100 bd-8 case exactly.
TEST_CASE("pickCdefFromQp bd 10 shifts the qtx by bit_depth - 8") {
    l10_filters::cdef_driver::PickQpParams p;
    p.baseQIdx = 96; // testAcQuantQtx(96) = 400
    p.bitDepth = 10;
    p.isKeyFrame = false;
    p.intraOnly = false;
    p.isScreenContent = false;
    p.acQuantQtx = testAcQuantQtx;
    std::int32_t predY = -1, predUv = -1;
    l10_filters::cdef_driver::pickCdefFromQp(p, &predY, &predUv);
    CHECK(predY == 4);
    CHECK(predUv == 0);
}

// compute_cdef_dist (cdef_process.c:266-:290): the is_16bit dispatch over
// the landed 16/8-bit walkers. Widening oracle at bd 8 plus the doffset
// placement (poison elsewhere) - doffset is a PIXEL offset, so the 16-bit
// branch reaches dst16 + doffset (byte offset 2*doffset) and the 8-bit
// branch dst8 + doffset. One dlist block (the {0,0} origin), src packed
// at the BLOCK width (the mse kernels' own layout).
TEST_CASE("computeCdefDist dispatches 16/8-bit with the doffset placement") {
    l10_filters::CdefList dlist[1] = {{0, 0}};
    const std::int32_t stride = 24, off = 5;

    // 8-bit plane: content at (0, off), poison elsewhere
    std::vector<std::uint8_t> p8((8 + 2) * stride + 16, 0xAB);
    for (std::int32_t r = 0; r < 8; ++r)
        for (std::int32_t c = 0; c < 8; ++c) p8[r * stride + off + c] = static_cast<std::uint8_t>(r * 8 + c);
    std::vector<std::uint8_t> s8(8 * 8, 0xCD);
    for (std::int32_t r = 0; r < 8; ++r)
        for (std::int32_t c = 0; c < 8; ++c) s8[r * 8 + c] = static_cast<std::uint8_t>(r * 8 + c);
    const std::uint64_t got8 = l10_filters::cdef_driver::computeCdefDist(p8.data(), off, stride, s8.data(), dlist, 1,
        l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1, false);
    CHECK(got8 == 0);

    // 16-bit plane: same logical content at ELEMENT offset off
    std::vector<std::uint16_t> p16((8 + 2) * stride + 16, 0xBEEF);
    for (std::int32_t r = 0; r < 8; ++r)
        for (std::int32_t c = 0; c < 8; ++c) p16[r * stride + off + c] = static_cast<std::uint16_t>(r * 8 + c);
    std::vector<std::uint16_t> s16(8 * 8, 0x1CE);
    for (std::int32_t r = 0; r < 8; ++r)
        for (std::int32_t c = 0; c < 8; ++c) s16[r * 8 + c] = static_cast<std::uint16_t>(r * 8 + c);
    const std::uint64_t got16 = l10_filters::cdef_driver::computeCdefDist(
        reinterpret_cast<const std::uint8_t*>(p16.data()), off, stride,
        reinterpret_cast<const std::uint8_t*>(s16.data()), dlist, 1, l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1,
        true);
    CHECK(got16 == 0);

    // a nonzero difference flows through both branches identically: the
    // 4x4 path with dst = src + 2 over 16 cells -> 16 * 4; the src packed
    // stride is 4 (the BLOCK_4X4 mse layout)
    for (std::int32_t r = 0; r < 4; ++r)
        for (std::int32_t c = 0; c < 4; ++c) {
            p8[r * stride + off + c] = static_cast<std::uint8_t>(r * 4 + c + 2);
            p16[r * stride + off + c] = static_cast<std::uint16_t>(r * 4 + c + 2);
        }
    std::vector<std::uint8_t> s4(16, 0xCD);
    for (std::int32_t r = 0; r < 4; ++r)
        for (std::int32_t c = 0; c < 4; ++c) s4[r * 4 + c] = static_cast<std::uint8_t>(r * 4 + c);
    std::vector<std::uint16_t> s416(16, 0x1CE);
    for (std::int32_t r = 0; r < 4; ++r)
        for (std::int32_t c = 0; c < 4; ++c) s416[r * 4 + c] = static_cast<std::uint16_t>(r * 4 + c);
    const std::uint64_t d8 = l10_filters::cdef_driver::computeCdefDist(p8.data(), off, stride, s4.data(), dlist, 1,
        l10_filters::CdefBlockSize::BLOCK_4X4, 0, 1, false);
    const std::uint64_t d16 = l10_filters::cdef_driver::computeCdefDist(
        reinterpret_cast<const std::uint8_t*>(p16.data()), off, stride,
        reinterpret_cast<const std::uint8_t*>(s416.data()), dlist, 1, l10_filters::CdefBlockSize::BLOCK_4X4, 0, 1,
        true);
    CHECK(d8 == 16 * 4);
    CHECK(d16 == 16 * 4);
}
namespace {
// BlockSize ordinals (definitions.h:883-:905 order).
constexpr int kBlock64x64 = 12;
constexpr int kBlock64x128 = 13;
constexpr int kBlock128x64 = 14;
constexpr int kBlock128x128 = 15;
} // namespace

// propagate_cdef_strength (enc_cdef.c:875-:895): write the strength at the
// sb_index cell, then the 128-block extensions (the MI_SIZE_64X64 = 16 mi
// step). Enumerated over the four bsize classes on a stride-40 mi grid.
TEST_CASE("propagateCdefStrength bsize classes") {
    const std::int32_t stride = 40;
    for (int bsizeCase = 0; bsizeCase < 4; ++bsizeCase) {
        const int bsize = bsizeCase == 0 ? kBlock128x128
            : bsizeCase == 1            ? kBlock128x64
            : bsizeCase == 2            ? kBlock64x128
                                        : kBlock64x64;
        std::vector<l10_filters::cdef_driver::CdefMb> grid(stride * 28);
        for (auto& mb : grid) {
            mb.bsize = kBlock64x64;
            mb.cdefStrength = -7;
        }
        const std::int32_t sbIndex = 10 * stride + 10;
        l10_filters::cdef_driver::propagateCdefStrength(grid.data(), stride, sbIndex, bsize, 9);

        CHECK(grid[sbIndex].cdefStrength == 9);
        // right neighbor (sb + MI_SIZE_64X64 = 16)
        CHECK(grid[sbIndex + 16].cdefStrength == (bsizeCase == 0 || bsizeCase == 1 ? 9 : -7));
        // below neighbor (sb + MI_SIZE_64X64 * mi_stride)
        CHECK(grid[sbIndex + 16 * stride].cdefStrength == (bsizeCase == 0 || bsizeCase == 2 ? 9 : -7));
        // diagonal neighbor
        CHECK(grid[sbIndex + 16 * stride + 16].cdefStrength == (bsizeCase == 0 ? 9 : -7));
        // untouched cells stay poison (non-16 mi offsets)
        CHECK(grid[sbIndex + 1].cdefStrength == -7);
        CHECK(grid[sbIndex + stride].cdefStrength == -7);
    }
}

// ---- S6: copy_recon_enc (cdef_process.c:46-:150) + set_unscaled_input_16bit
// (cdef_process.c:31-:44). The EB_CALLOC_ALIGNED_ARRAY allocation wall is
// threaded as a caller-provided buffer_alloc; the set_unscaled callees
// (svt_aom_copy_buffer_info / svt_aom_convert_pic_8bit_to_16bit /
// svt_aom_pack_2d_pic) are threaded as callbacks (no fakes - the tests
// drive the dispatch, the kernels stay walled).

namespace {
using l10_filters::cdef_driver::CdefPic;
using l10_filters::cdef_driver::CdefScsWalls;

// a poison-initialized src picture, 16x8 content, 4:2:0, border 32
CdefPic makeSrcPic() {
    CdefPic s;
    s.width = 16;
    s.height = 8;
    s.maxWidth = 16;
    s.maxHeight = 8;
    s.border = 32;
    s.bitDepth = 8;
    s.colorFormat = 1;
    s.lumaSize = 4096;
    s.chromaSize = 1024;
    s.packedFlag = true;
    s.stride[0] = 64;
    s.stride[1] = 40;
    s.stride[2] = 40;
    s.strideBitInc[0] = 11;
    s.strideBitInc[1] = 7;
    s.strideBitInc[2] = 5;
    return s;
}

CdefPic makeDstPic() {
    CdefPic d;
    for (int i = 0; i < 3; ++i) {
        d.buffer[i] = nullptr;
        d.stride[i] = 0xEEEE;
        d.strideBitInc[i] = 0xEE;
    }
    d.width = 0xEEEE;
    d.height = 0xEEEE;
    d.maxWidth = 0xEEEE;
    d.maxHeight = 0xEEEE;
    d.border = 0xEEEE;
    d.bitDepth = 0xEE;
    d.colorFormat = 0xEE;
    d.lumaSize = 0xEEEE;
    d.chromaSize = 0xEEEE;
    d.packedFlag = false;
    d.bufferEnableMask = 0xEEEEEEEE;
    d.bufferAlloc = nullptr;
    d.bufferAllocSz = 0xEEEEEEEE;
    return d;
}

struct ScsWalls { /* kept for the initializer shape */
    bool monoChrome = false;
    int subsamplingX = 1;
    int subsamplingY = 1;
    bool is16bitPipeline = false;
};
} // namespace

TEST_CASE("copyReconEnc full-mask field copies + plane setup + copy loop") {
    CdefPic s = makeSrcPic();
    CdefPic d = makeDstPic();
    ScsWalls scsInit;
    CdefScsWalls scs{scsInit.monoChrome, scsInit.subsamplingX, scsInit.subsamplingY, scsInit.is16bitPipeline};
    // plane buffers: src planes carry recognizable rows, dst planes live in
    // a caller-provided alloc
    std::vector<std::uint8_t> srcBuf(8192, 0x55);
    s.buffer[0] = srcBuf.data() + 100;
    s.buffer[1] = srcBuf.data() + 200;
    s.buffer[2] = srcBuf.data() + 300;
    std::vector<std::uint8_t> alloc(8192, 0xAB);
    l10_filters::cdef_driver::copyReconEnc(scs, s, d, 3, false, alloc.data());

    // the 12 header field copies
    CHECK(d.border == 32);
    CHECK(d.width == 16);
    CHECK(d.height == 8);
    CHECK(d.maxWidth == 16);
    CHECK(d.maxHeight == 8);
    CHECK(d.bitDepth == 8);
    CHECK(d.colorFormat == 1);
    CHECK(d.stride[0] == 64);
    CHECK(d.stride[1] == 40);
    CHECK(d.stride[2] == 40);
    CHECK(d.lumaSize == 4096);
    CHECK(d.chromaSize == 1024);
    CHECK(d.packedFlag == true);
    CHECK(d.strideBitInc[0] == 11);
    CHECK(d.strideBitInc[1] == 7);
    CHECK(d.strideBitInc[2] == 5);
    // mono chrome 0 -> FULL mask (7)
    CHECK(d.bufferEnableMask == 7);
    // alloc size = luma + 2 * chroma (bpp 1)
    CHECK(d.bufferAllocSz == 4096 + 2 * 1024);
    // plane pointers: y = alloc + (border + y_stride*border); u = alloc +
    // luma + (border>>1 + u_stride*(border>>1)); v = u + chroma
    CHECK(d.buffer[0] == alloc.data() + (32 + 64 * 32));
    CHECK(d.buffer[1] == alloc.data() + 4096 + (16 + 40 * 16));
    CHECK(d.buffer[2] == alloc.data() + 5120 + (16 + 40 * 16));
    // the copy loop: luma rows 8 x 16 bytes; chroma rows 4 x 8 bytes
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 16; ++c) CHECK(d.buffer[0][r * 64 + c] == 0x55);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 8; ++c) {
            CHECK(d.buffer[1][r * 40 + c] == 0x55);
            CHECK(d.buffer[2][r * 40 + c] == 0x55);
        }
    // beyond the row width: untouched alloc poison
    CHECK(d.buffer[0][16] == 0xAB);
    CHECK(d.buffer[1][8] == 0xAB);
}

TEST_CASE("copyReconEnc mono-chrome mask + null chroma planes") {
    CdefPic s = makeSrcPic();
    CdefPic d = makeDstPic();
    ScsWalls scsInit;
    CdefScsWalls scs{scsInit.monoChrome, scsInit.subsamplingX, scsInit.subsamplingY, scsInit.is16bitPipeline};
    scs.monoChrome = true;
    std::vector<std::uint8_t> srcBuf(8192, 0x55);
    s.buffer[0] = srcBuf.data() + 100;
    std::vector<std::uint8_t> alloc(8192, 0xAB);
    // the real caller passes num_planes = 1 for mono chrome (the copy loop
    // walks num_planes regardless of the enable mask)
    l10_filters::cdef_driver::copyReconEnc(scs, s, d, 1, false, alloc.data());
    CHECK(d.bufferEnableMask == 1);
    CHECK(d.bufferAllocSz == 4096);
    CHECK(d.buffer[0] == alloc.data() + (32 + 64 * 32));
    CHECK(d.buffer[1] == nullptr);
    CHECK(d.buffer[2] == nullptr);
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 16; ++c) CHECK(d.buffer[0][r * 64 + c] == 0x55);
}

TEST_CASE("copyReconEnc skip_copy leaves the dst planes poisoned") {
    CdefPic s = makeSrcPic();
    CdefPic d = makeDstPic();
    ScsWalls scsInit;
    CdefScsWalls scs{scsInit.monoChrome, scsInit.subsamplingX, scsInit.subsamplingY, scsInit.is16bitPipeline};
    std::vector<std::uint8_t> alloc(8192, 0xAB);
    l10_filters::cdef_driver::copyReconEnc(scs, s, d, 3, true, alloc.data());
    CHECK(d.buffer[0] == alloc.data() + (32 + 64 * 32));
    CHECK(d.bufferAllocSz == 6144);
    CHECK(d.buffer[0][0] == 0xAB);
    CHECK(d.buffer[1][0] == 0xAB);
}

TEST_CASE("copyReconEnc 16-bit pipeline doubles bytes and offsets") {
    CdefPic s = makeSrcPic();
    CdefPic d = makeDstPic();
    ScsWalls scsInit;
    CdefScsWalls scs{scsInit.monoChrome, scsInit.subsamplingX, scsInit.subsamplingY, scsInit.is16bitPipeline};
    scs.is16bitPipeline = true;
    std::vector<std::uint8_t> srcBuf(16384, 0x55);
    s.buffer[0] = srcBuf.data() + 100;
    s.buffer[1] = srcBuf.data() + 200;
    s.buffer[2] = srcBuf.data() + 300;
    std::vector<std::uint8_t> alloc(16384, 0xAB);
    l10_filters::cdef_driver::copyReconEnc(scs, s, d, 3, false, alloc.data());
    // bpp 2: y offset (32 + 64*32) * 2; u = luma*2 + (16 + 40*16)*2
    CHECK(d.buffer[0] == alloc.data() + (32 + 64 * 32) * 2);
    CHECK(d.buffer[1] == alloc.data() + 8192 + (16 + 40 * 16) * 2);
    CHECK(d.buffer[2] == alloc.data() + 10240 + (16 + 40 * 16) * 2);
    CHECK(d.bufferAllocSz == 8192 + 2 * 2048);
    // copy width: ((16 + sub) >> sub) * 1 << 1 = 32 bytes per luma row;
    // the dst row advance is dst_stride << 1 = 128 bytes = 64 uint16
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 16; ++c) {
            const std::uint16_t got = reinterpret_cast<std::uint16_t*>(d.buffer[0])[r * 64 + c];
            CHECK(got == 0x5555);
        }
}

// set_unscaled_input_16bit (cdef_process.c:31-:44): copy_buffer_info
// always, then the 8-bit->16bit convert OR the pack_2d path.
namespace {
int gCopyInfoCalls, gConvertCalls, gPackCalls;
void testCopyInfo(const CdefPic&, CdefPic&) { ++gCopyInfoCalls; }
void testConvert(const CdefPic&, CdefPic&, int, int) { ++gConvertCalls; }
void testPack(const CdefPic&, CdefPic&) { ++gPackCalls; }
} // namespace

TEST_CASE("setUnscaledInput16bit dispatches on the input bit depth") {
    CdefPic in = makeSrcPic();
    CdefPic out = makeDstPic();
    ScsWalls scsInit;
    CdefScsWalls scs{scsInit.monoChrome, scsInit.subsamplingX, scsInit.subsamplingY, scsInit.is16bitPipeline};
    gCopyInfoCalls = gConvertCalls = gPackCalls = 0;

    in.bitDepth = 8;
    l10_filters::cdef_driver::setUnscaledInput16bit(
        scs, in, out, testCopyInfo, testConvert, testPack);
    CHECK(gCopyInfoCalls == 1);
    CHECK(gConvertCalls == 1);
    CHECK(gPackCalls == 0);

    in.bitDepth = 10;
    l10_filters::cdef_driver::setUnscaledInput16bit(
        scs, in, out, testCopyInfo, testConvert, testPack);
    CHECK(gCopyInfoCalls == 2);
    CHECK(gConvertCalls == 1);
    CHECK(gPackCalls == 1);
}
