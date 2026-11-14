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
