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

// ---- S7: cdef_seg_search (cdef_process.c:292-:666) ------------------------
// The fixtures pin the OBSERVABLE structure with deterministic constant
// planes: on a constant recon any CDEF filter returns the recon exactly
// (the constrain of equal diffs is 0 -> y = x), so every gi's tmp_dst is
// the gathered recon and the mse values are hand-derivable:
//   luma  (BLOCK_8X8, 64 blocks x 64 px x (104-100)^2 = 65536, x ss)
//   chroma (BLOCK_4X4, 64 blocks x 16 px x 16 = 16384 each)
//   the V plane FOLDS into the U row (pli < 2 vs else) -> 32768
// The gathered plane identity is pinned through the scratch buffers (the
// final inbuf body is the V plane's gather; in8 has no sentinel reset).

namespace {
using l10_filters::cdef_driver::CdefSegCtx;
using l10_filters::cdef_driver::CdefSearchControls;
using l10_filters::cdef_driver::CdefMb;
using l10_filters::cdef_driver::CdefFbEntry;
using l10_filters::cdef_driver::CdefDirData;
using l10_filters::cdef_driver::kCdefInbufSize;

// the test-side svt_sb_compute_cdef_list for an all-non-skip grid: 8x8
// units stepping 2 mi, count = (maxr/2)*(maxc/2)
std::int32_t testComputeCdefList(l10_filters::cdef_driver::CdefMb*, std::int32_t, std::int32_t miRow,
    std::int32_t miCol, l10_filters::CdefList* dlist, int bs) {
    (void)miRow;
    (void)miCol;
    const int kMiSize = bs == 15 || bs == 13 ? 32 : 16;
    const std::int32_t maxc = kMiSize, maxr = kMiSize;
    std::int32_t count = 0;
    for (std::int32_t r = 0; r < maxr; r += 2)
        for (std::int32_t c = 0; c < maxc; c += 2) {
            dlist[count].by = static_cast<std::uint8_t>(r >> 1);
            dlist[count].bx = static_cast<std::uint8_t>(c >> 1);
            ++count;
        }
    return count;
}

struct SegFixture {
    static constexpr std::int32_t kMiStride = 40;
    CdefMb miGrid[kMiStride * kMiStride];
    CdefFbEntry fbList[16];
    CdefDirData dirData[16];
    std::uint8_t skip[16];
    std::vector<std::uint16_t> tmpDst;
    std::vector<std::uint16_t> inbuf;
    std::vector<std::uint8_t> inbuf8;
    std::uint64_t msePlane0[16][l10_filters::kCdefTotalStrengths];
    std::uint64_t msePlane1[16][l10_filters::kCdefTotalStrengths];
    CdefSegCtx ctx;

    // miRows/miCols in mi units; srcConst/refConst per plane (or a column
    // ramp when rampCol)
    SegFixture(std::int32_t miRows, std::int32_t miCols, std::uint16_t srcY, std::uint16_t srcU,
        std::uint16_t refY, std::uint16_t refU, bool rampCol = false, std::int32_t fbPx = 128) {
        for (auto& mb : miGrid) {
            mb.bsize = 12; // BLOCK_64X64
            mb.cdefStrength = 0;
        }
        for (int i = 0; i < 16; ++i) skip[i] = 0xEE;
        tmpDst.assign(1 << 14, 0x7ABC);
        inbuf.assign(kCdefInbufSize, 0x1234);
        inbuf8.assign(kCdefInbufSize, 0xAA);

        const std::int32_t lumaPx = 2 * fbPx, chromaPx = fbPx;
        // the plane buffers are 8-BIT (the native path reads bytes); the
        // 16-bit variant widens them (set16bitMode)
        srcBufY.assign((lumaPx + 8) * (lumaPx + 16), (std::uint8_t) srcY);
        srcBufU.assign((chromaPx + 8) * (chromaPx + 16), (std::uint8_t) srcU);
        srcBufV.assign((chromaPx + 8) * (chromaPx + 16), (std::uint8_t)(srcU + 11));
        refBufY.assign((lumaPx + 8) * (lumaPx + 16), (std::uint8_t) refY);
        refBufU.assign((chromaPx + 8) * (chromaPx + 16), (std::uint8_t) refU);
        refBufV.assign((chromaPx + 8) * (chromaPx + 16), (std::uint8_t)(refU + 11));
        if (rampCol) {
            for (std::int32_t r = 0; r < lumaPx + 8; ++r)
                for (std::int32_t c = 0; c < lumaPx + 16; ++c)
                    srcBufY[r * (lumaPx + 16) + c] = static_cast<std::uint8_t>(c + 1);
        }
        ctx.miGrid = miGrid;
        ctx.miStride = kMiStride;
        ctx.miRows = miRows;
        ctx.miCols = miCols;
        ctx.alignedWidth = miCols * 4;
        ctx.alignedHeight = miRows * 4;
        ctx.cdefSegmentsColumnCount = 1;
        ctx.cdefSegmentsRowCount = 1;
        ctx.sb64 = true;
        ctx.encoderBitDepth = 8;
        ctx.is16bit = false;
        ctx.baseQIdx = 0;
        ctx.src[0] = srcBufY.data();
        ctx.src[1] = srcBufU.data();
        ctx.src[2] = srcBufV.data();
        ctx.ref[0] = refBufY.data();
        ctx.ref[1] = refBufU.data();
        ctx.ref[2] = refBufV.data();
        ctx.strideSrc[0] = lumaPx + 16;
        ctx.strideSrc[1] = ctx.strideSrc[2] = chromaPx + 16;
        ctx.strideRef[0] = lumaPx + 16;
        ctx.strideRef[1] = ctx.strideRef[2] = chromaPx + 16;
        ctx.mseSeg[0] = msePlane0;
        ctx.mseSeg[1] = msePlane1;
        ctx.skipCdefSeg = skip;
        ctx.cdefDirData = dirData;
        ctx.cdefFbList = fbList;
        ctx.scratch.tmpDst = tmpDst.data();
        ctx.scratch.inbuf = inbuf.data();
        ctx.scratch.inbuf8 = inbuf8.data();
        ctx.computeCdefList = testComputeCdefList;
        for (int i = 0; i < 16; ++i) fbList[i].cdefCount = -1;
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < l10_filters::kCdefTotalStrengths; ++j) {
                msePlane0[i][j] = 0xAAAAAAAAAAAAAAAAull;
                msePlane1[i][j] = 0xAAAAAAAAAAAAAAAAull;
            }
    }

    std::vector<std::uint8_t> srcBufY, srcBufU, srcBufV, refBufY, refBufU, refBufV;
    std::vector<std::uint16_t> src16Y, src16U, src16V, ref16Y, ref16U, ref16V;

    CdefSearchControls& ctrls() { return ctx.ctrls; }

    // widen the plane buffers and re-point the carrier (for is16bit runs)
    void set16bitMode() {
        auto widen = [](const std::vector<std::uint8_t>& u8, std::vector<std::uint16_t>& u16) {
            u16.resize(u8.size());
            for (std::size_t i = 0; i < u8.size(); ++i) u16[i] = u8[i];
        };
        widen(srcBufY, src16Y);
        widen(srcBufU, src16U);
        widen(srcBufV, src16V);
        widen(refBufY, ref16Y);
        widen(refBufU, ref16U);
        widen(refBufV, ref16V);
        ctx.src[0] = reinterpret_cast<const std::uint8_t*>(src16Y.data());
        ctx.src[1] = reinterpret_cast<const std::uint8_t*>(src16U.data());
        ctx.src[2] = reinterpret_cast<const std::uint8_t*>(src16V.data());
        ctx.ref[0] = reinterpret_cast<const std::uint8_t*>(ref16Y.data());
        ctx.ref[1] = reinterpret_cast<const std::uint8_t*>(ref16U.data());
        ctx.ref[2] = reinterpret_cast<const std::uint8_t*>(ref16V.data());
        ctx.is16bit = true;
    }
};
} // namespace

TEST_CASE("cdefSegSearch constant frame mse structure + fb cache + dir/var") {
    SegFixture f(32, 32, 100, 66, 104, 70); // 128x128 px, 2x2 fbs
    auto& c = f.ctrls();
    c.firstPassFsNum = 1;
    c.defaultSecondPassFsNum = 1;
    for (int i = 0; i < 64; ++i) {
        c.defaultFirstPassFs[i] = 5;
        c.defaultSecondPassFs[i] = 7;
        c.defaultFirstPassFsUv[i] = 0;
        c.defaultSecondPassFsUv[i] = 0;
    }
    c.subsamplingFactor = 1;
    l10_filters::cdef_driver::cdefSegSearch(f.ctx, 0);

    for (std::uint32_t fb = 0; fb < 4; ++fb) {
        // luma: 64 blocks x 64 px x 16 = 65536, x ss 1
        CHECK(f.msePlane0[fb][0] == 65536);
        // chroma U then V folded: 16384 + 16384 = 32768
        CHECK(f.msePlane1[fb][0] == 32768);
        // second pass gi 1: same
        CHECK(f.msePlane0[fb][1] == 65536);
        CHECK(f.msePlane1[fb][1] == 32768);
        // skip flag cleared
        CHECK(f.skip[fb] == 0);
        // the SB=64 dlist cache filled: 64 entries, corners {0,0} and {7,7}
        CHECK(f.fbList[fb].cdefCount == 64);
        CHECK(f.fbList[fb].dlist[0].by == 0);
        CHECK(f.fbList[fb].dlist[0].bx == 0);
        CHECK(f.fbList[fb].dlist[63].by == 7);
        CHECK(f.fbList[fb].dlist[63].bx == 7);
        // dir/var: constant planes -> dir 0, var 0
        CHECK(f.dirData[fb].dir[0][0] == 0);
        CHECK(f.dirData[fb].var[0][0] == 0);
        CHECK(f.dirData[fb].var[7][7] == 0);
    }
    // the fb count equals the mi-grid call count: 4 calls (one per fb)
}

TEST_CASE("cdefSegSearch chroma -1 mask gate pins default_mse_uv * 64") {
    SegFixture f(32, 32, 100, 66, 104, 70);
    auto& c = f.ctrls();
    c.firstPassFsNum = 1;
    c.defaultSecondPassFsNum = 1;
    for (int i = 0; i < 64; ++i) {
        c.defaultFirstPassFs[i] = 5;
        c.defaultSecondPassFs[i] = 7;
        c.defaultFirstPassFsUv[i] = -1;
        c.defaultSecondPassFsUv[i] = -1;
    }
    c.subsamplingFactor = 1;
    l10_filters::cdef_driver::cdefSegSearch(f.ctx, 0);

    for (std::uint32_t fb = 0; fb < 4; ++fb) {
        // both chroma gis pinned to default_mse_uv * 64 = 1040400 * 64
        CHECK(f.msePlane1[fb][0] == 1040400ull * 64);
        CHECK(f.msePlane1[fb][1] == 1040400ull * 64);
        // luma still searched
        CHECK(f.msePlane0[fb][0] == 65536);
        CHECK(f.msePlane0[fb][1] == 65536);
    }
}

TEST_CASE("cdefSegSearch 16-bit path matches the native 8-bit path") {
    // identical constant fixture, is16bit on: the widened planes carry the
    // same content and the sentinel gather + cdefFilterFb path must land
    // the same mse values
    SegFixture f(32, 32, 100, 66, 104, 70);
    f.set16bitMode();
    auto& c = f.ctrls();
    c.firstPassFsNum = 1;
    c.defaultSecondPassFsNum = 1;
    for (int i = 0; i < 64; ++i) {
        c.defaultFirstPassFs[i] = 5;
        c.defaultSecondPassFs[i] = 7;
        c.defaultFirstPassFsUv[i] = 0;
        c.defaultSecondPassFsUv[i] = 0;
    }
    c.subsamplingFactor = 1;
    l10_filters::cdef_driver::cdefSegSearch(f.ctx, 0);
    for (std::uint32_t fb = 0; fb < 4; ++fb) {
        CHECK(f.msePlane0[fb][0] == 65536);
        CHECK(f.msePlane1[fb][0] == 32768);
        CHECK(f.msePlane0[fb][1] == 65536);
        CHECK(f.msePlane1[fb][1] == 32768);
    }
}

TEST_CASE("cdefSegSearch 128 block: skip geometry + big-fb list") {
    SegFixture f(32, 32, 100, 66, 104, 70);
    // one 128x128 block: every mi cell bsize 15
    for (auto& mb : f.miGrid) mb.bsize = 15;
    f.ctx.sb64 = false; // SB=128: no dlist cache write
    auto& c = f.ctrls();
    c.firstPassFsNum = 1;
    c.defaultSecondPassFsNum = 1;
    for (int i = 0; i < 64; ++i) {
        c.defaultFirstPassFs[i] = 5;
        c.defaultSecondPassFs[i] = 7;
        c.defaultFirstPassFsUv[i] = 0;
        c.defaultSecondPassFsUv[i] = 0;
    }
    c.subsamplingFactor = 1;
    l10_filters::cdef_driver::cdefSegSearch(f.ctx, 0);

    // fb (0,0) processed: the b64 count = (32/2)*(32/2) = 256 through the
    // test callback; the other three fbs are skipped by the bsize condition
    // BEFORE the flag write, so their flags stay at the fixture poison
    CHECK(f.skip[0] == 0);
    CHECK(f.skip[1] == 0xEE);
    CHECK(f.skip[2] == 0xEE);
    CHECK(f.skip[3] == 0xEE);
    // the fb cache NOT written (sb64 false)
    CHECK(f.fbList[0].cdefCount == -1);
    // luma mse: 256 blocks x 64 px x 16 = 262144 (the 128 fb covers 256
    // 8x8 luma units)
    CHECK(f.msePlane0[0][0] == 262144);
    // chroma: 256 4x4 units x 16 px x 16 = 65536 each, V folds -> 131072
    CHECK(f.msePlane1[0][0] == 131072);
}

TEST_CASE("cdefSegSearch inbuf sentinel reset + V-plane residency") {
    // single 64x64 fb, 16-bit path: the incremental reset zeroes the whole
    // buffer to 0x7F7F, then the gathers write the body; the LAST gather
    // is the V plane (pli 2), so the resident body is the V plane content
    SegFixture f(16, 16, 55, 66, 59, 70);
    f.set16bitMode();
    auto& c = f.ctrls();
    c.firstPassFsNum = 1;
    c.defaultSecondPassFsNum = 1;
    for (int i = 0; i < 64; ++i) {
        c.defaultFirstPassFs[i] = 5;
        c.defaultSecondPassFs[i] = 7;
        c.defaultFirstPassFsUv[i] = 0;
        c.defaultSecondPassFsUv[i] = 0;
    }
    c.subsamplingFactor = 1;
    l10_filters::cdef_driver::cdefSegSearch(f.ctx, 0);

    const std::uint16_t sentinel = 0x7F7F;
    // Geometry for the SINGLE-fb frame (nhfb = nvfb = 1 -> roff = boff = 0,
    // toff = loff = 0): each gather body is (nvb<<miHighL2) x (nhb<<miWideL2)
    // = 32 x 32 chroma / 64 x 64 luma, landing at buffer rows 3.., cols 8..
    // from the in origin (inbuf + 3*144 + 8). Sequence:
    //   pli 0 luma: full reset (134x134 from inbuf[0]) then a 64-wide gather
    //   pli 1 U:    ysize 40 < ysizePrev 72 -> reset AGAIN (72 rows x 64
    //               cols from the in origin - which wipes the whole luma
    //               body), then a 32-wide gather
    //   pli 2 V:    no reset, 32-wide over U
    // Final state at row 3: cols 8..39 = V (77); cols 40+ sentinel.
    CHECK(f.inbuf[0] == sentinel);
    CHECK(f.inbuf[2 * 144 + 100] == sentinel);
    for (int j = 0; j < 32; ++j) {
        CHECK(f.inbuf[3 * 144 + 8 + j] == 77); // srcV = srcU + 11 = 66 + 11
    }
    for (int j = 32; j < 64; ++j) {
        CHECK(f.inbuf[3 * 144 + 8 + j] == sentinel);
    }
    CHECK(f.inbuf[3 * 144 + 8 + 64] == sentinel);
    CHECK(f.inbuf[(3 + 41) * 144 + 20] == sentinel);
}

TEST_CASE("cdefSegSearch native 8-bit in8 gather has NO sentinel reset") {
    SegFixture f(16, 16, 55, 66, 59, 70);
    f.ctx.is16bit = false; // native path
    auto& c = f.ctrls();
    c.firstPassFsNum = 1;
    c.defaultSecondPassFsNum = 1;
    for (int i = 0; i < 64; ++i) {
        c.defaultFirstPassFs[i] = 5;
        c.defaultSecondPassFs[i] = 7;
        c.defaultFirstPassFsUv[i] = 0;
        c.defaultSecondPassFsUv[i] = 0;
    }
    c.subsamplingFactor = 1;
    l10_filters::cdef_driver::cdefSegSearch(f.ctx, 0);

    // in8 is pre-filled 0xAA and the native gather never resets it. With
    // roff = boff = 0 (single fb), the luma gather is 64 wide (rows 3..74,
    // cols 8..71), U and V 32 wide (rows 3..42, cols 8..39); the FINAL
    // body is the V plane content and the luma leftovers survive around
    // it (55); only cells outside BOTH gathers keep the 0xAA pre-fill.
    CHECK(f.inbuf8[3 * 144 + 8] == 77);
    CHECK(f.inbuf8[3 * 144 + 8 + 31] == 77);
    // one col past the 32-wide chroma body: the luma leftover (55)
    CHECK(f.inbuf8[3 * 144 + 8 + 32] == 55);
    CHECK(f.inbuf8[3 * 144 + 8 + 63] == 55);
    // outside the luma gather too: the pre-fill survives
    CHECK(f.inbuf8[0] == 0xAA);
    CHECK(f.inbuf8[(3 + 72) * 144 + 20] == 0xAA);
}
