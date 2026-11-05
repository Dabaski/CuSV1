#include <doctest.h>
#include <restoration.h>
#include <convolve.h>

#include <algorithm>
#include <vector>

namespace {
// set_default_wiener (restoration.h:248-:256) with the WIENER_FILT_*_MIDV
// defaults (restoration.h:129-:133): a normalized 7-tap (sum 128, tap[7]=0),
// so wiener fixtures do not clip every output to 255.
const std::int16_t kWienerDefaultTaps[8] = {3, -7, 15, 106, 15, -7, 3, 0};
} // namespace

// LR1 (A8 campaign): 1:1 port of the restoration math core of
// third_party/SVT-AV1/Source/Lib/Codec/restoration.c: sgr_params :31-:49,
// boxsum1 :430-:495, boxsum2 :497-:584, boxsum :586-:594, svt_decode_xq
// :597-:608, x_by_xplus1 :610-:625, one_by_x :627-:630, extend_frame_lowbd
// :110-:126 + svt_extend_frame :150-:157, copy_tile_lowbd :159-:164 +
// copy_tile :173-:180.
// LR2 (A8 campaign): the selfguided family :632-:962 -
// selfguided_restoration_fast_internal :632-:764,
// selfguided_restoration_internal :766-:884,
// svt_av1_selfguided_restoration_c :886-:922,
// svt_apply_selfguided_restoration_c :924-:962 - fixture classes: the
// independent clamped-box oracle (three estimator regimes: saturated
// z >= 255, mid-variance, flat z == 0), the r==0 skip pair, the apply
// composition + both clip bounds.
// PARKED (named): the highbd stripe twins :987-:1029, the Av1Common-walled
// frame walkers :1112-:1149+ and the tile walkers :1150+. Coverage-only
// slice: moves no conformance count; enable_restoration stays 0.

TEST_CASE("sgrParams row 0 is {2,1},{140,3236}") {
    CHECK(l10_filters::restoration::sgrParams[0].r[0] == 2);
}

// LR3-S1: count_units_in_tile (restoration.c:71-:73): round-to-NEAREST
// division (models the right/bottom unit extending to up to 150%), floored
// at 1. Enumerated pins around the .5 boundary: exactly 1.5 rounds up, just
// below 1.5 rounds down; tiles under half a unit clamp to 1.
TEST_CASE("countUnitsInTile rounds to nearest and clamps at 1") {
    using l10_filters::restoration::countUnitsInTile;
    CHECK(countUnitsInTile(64, 8) == 1);   // tile < half unit -> 1
    CHECK(countUnitsInTile(64, 32) == 1);  // exactly half -> 1
    CHECK(countUnitsInTile(64, 33) == 1);  // just past half -> 1
    CHECK(countUnitsInTile(64, 95) == 1);  // 1.48 rounds down
    CHECK(countUnitsInTile(64, 96) == 2);  // exactly 1.5 rounds up
    CHECK(countUnitsInTile(64, 160) == 3); // exactly 2.5 rounds up
    CHECK(countUnitsInTile(64, 161) == 3);
    CHECK(countUnitsInTile(128, 600) == 5); // 4.69 -> 5
    CHECK(countUnitsInTile(256, 256) == 1);
}

// LR3-S2: whole_frame_rect (restoration.c:51-:62): the frame rect is
// ROUND_POWER_OF_TWO(frame_height / superres_upscaled_width, ss) with
// ss = is_uv && sub_*, top/left 0. Enumerated over the full
// (is_uv, sub_x, sub_y) grid with an inline oracle.
TEST_CASE("wholeFrameRect rounds frame dims by subsampling over the full flag grid") {
    using l10_filters::restoration::FrameSize;
    using l10_filters::restoration::wholeFrameRect;
    FrameSize fs{};
    fs.frameHeight = 1080;
    fs.superresUpscaledWidth = 1920;
    for (int isUv = 0; isUv <= 1; ++isUv)
        for (int sx = 0; sx <= 1; ++sx)
            for (int sy = 0; sy <= 1; ++sy) {
                const int ssx = (isUv && sx) ? 1 : 0;
                const int ssy = (isUv && sy) ? 1 : 0;
                const auto rect = wholeFrameRect(&fs, sx, sy, isUv);
                CHECK(rect.top == 0);
                CHECK(rect.left == 0);
                CHECK(rect.bottom == ((1080 + ((1 << ssy) >> 1)) >> ssy));
                CHECK(rect.right == ((1920 + ((1 << ssx) >> 1)) >> ssx));
            }
}

// LR3-S3: get_stripe_boundary_info (restoration.c:216-:234): copy_above = 0
// iff the stripe starts at the tile top; copy_below = 0 iff v_start + (full
// stripe height - first-stripe runit_offset) reaches the tile bottom. The
// reachable stripe lattice for a 60-high luma tile (ss_y = 0) is v = 0, 28,
// 60; for a 30-high chroma tile (ss_y = 1, full 32, offset 4) it is v = 0,
// 28, 60 too. Plus the tiny-tile case where one stripe is both first and
// last (both flags 0).
TEST_CASE("getStripeBoundaryInfo flags over the enumerated stripe lattice") {
    using l10_filters::restoration::getStripeBoundaryInfo;
    using l10_filters::restoration::Av1PixelRect;
    using l10_filters::restoration::RestorationTileLimits;
    Av1PixelRect tile{0, 0, 1920, 60};
    RestorationTileLimits lim{};
    lim.h_start = 0;
    lim.h_end = 64;
    int ca, cb;
    // Luma tile 60 high, ss_y = 0: full stripe 64, runit offset 8.
    lim.v_start = 0;  getStripeBoundaryInfo(&lim, &tile, 0, &ca, &cb); // first
    CHECK(ca == 0); CHECK(cb == 1);
    lim.v_start = 28; getStripeBoundaryInfo(&lim, &tile, 0, &ca, &cb); // last (28+32 >= 60)
    CHECK(ca == 1); CHECK(cb == 0);
    lim.v_start = 60; getStripeBoundaryInfo(&lim, &tile, 0, &ca, &cb); // past-bottom stripe
    CHECK(ca == 1); CHECK(cb == 0);
    lim.v_start = 8;  getStripeBoundaryInfo(&lim, &tile, 0, &ca, &cb); // mid-stripe: 8+64 >= 60 -> last
    CHECK(ca == 1); CHECK(cb == 0);
    // Mid-stripe on a tall tile (bottom 200): 8+64 and 28+64 both stay
    // inside -> neither boundary.
    Av1PixelRect tileMid{0, 0, 1920, 200};
    lim.v_start = 8;  getStripeBoundaryInfo(&lim, &tileMid, 0, &ca, &cb);
    CHECK(ca == 1); CHECK(cb == 1);
    lim.v_start = 28; getStripeBoundaryInfo(&lim, &tileMid, 0, &ca, &cb);
    CHECK(ca == 1); CHECK(cb == 1);
    // Chroma tile 30 high, ss_y = 1: full stripe 32, runit offset 4.
    Av1PixelRect tileC{0, 0, 960, 30};
    lim.v_start = 0;  getStripeBoundaryInfo(&lim, &tileC, 1, &ca, &cb);
    CHECK(ca == 0); CHECK(cb == 1);
    lim.v_start = 28; getStripeBoundaryInfo(&lim, &tileC, 1, &ca, &cb);
    CHECK(ca == 1); CHECK(cb == 0);
    // Tiny tile: the single stripe is both first and last.
    Av1PixelRect tileT{0, 0, 960, 8};
    lim.v_start = 0;  getStripeBoundaryInfo(&lim, &tileT, 0, &ca, &cb);
    CHECK(ca == 0); CHECK(cb == 0);
}

// LR3-S4: setup_processing_stripe_boundary (restoration.c:249-:332), the
// NON-opt copy_above path: rows v_start-3..v_start-1 (columns h_start-4 ..
// h_end+4) are replaced from rsb->stripe_boundary_above at buf rows
// rsb_row + max(i + RESTORATION_CTX_VERT, 0) = rsb_row + {0,0,1} for
// i = {-3,-2,-1}, offset buf_x0_off = h_start in the boundary buffer; the
// ORIGINAL rows are saved into rlbs->tmp_save_above[{0,1,2}]. Pixels
// outside the line window (columns h_start-4-1 and h_end+4) stay untouched.
TEST_CASE("setupProcessingStripeBoundary non-opt above: replace + save") {
    using l10_filters::restoration::setupProcessingStripeBoundary;
    using l10_filters::restoration::RestorationTileLimits;
    using l10_filters::restoration::RestorationStripeBoundaries;
    using l10_filters::restoration::RestorationLineBuffers;
    const int stride = 96, bufStride = 128;
    std::vector<std::uint8_t> data(40 * stride, 0);
    for (int i = 0; i < 40; ++i)
        for (int j = 0; j < stride; ++j) data[i * stride + j] = (std::uint8_t)(i * 7 + j);
    std::vector<std::uint8_t> above(20 * bufStride, 0);
    for (int i = 0; i < 20; ++i)
        for (int j = 0; j < bufStride; ++j) above[i * bufStride + j] = (std::uint8_t)(100 + i * 3 + j * 2);
    RestorationStripeBoundaries rsb{};
    rsb.stripe_boundary_above = above.data();
    rsb.stripe_boundary_stride = bufStride;
    RestorationLineBuffers rlbs{};
    RestorationTileLimits lim{8, 72, 16, 48};
    setupProcessingStripeBoundary(&lim, &rsb, 5, 0, 32, data.data(), stride, &rlbs, 1, 0, 0);
    // dst row v_start + i, cols [4, 76) == above row (5 + max(i+2,0)), from
    // buf offset h_start = 8: i=-3,-2 -> row 5; i=-1 -> row 6.
    for (int j = 0; j < 72; ++j) {
        CHECK(data[(16 - 3) * stride + 4 + j] == above[5 * bufStride + 8 + j]);
        CHECK(data[(16 - 2) * stride + 4 + j] == above[5 * bufStride + 8 + j]);
        CHECK(data[(16 - 1) * stride + 4 + j] == above[6 * bufStride + 8 + j]);
    }
    // Saved originals: tmp_save_above[k] = old row v_start + (k - 3). The
    // save arrays are RAW BYTE areas in uint16_t arrays - read as bytes.
    for (int k = 0; k < 3; ++k)
        for (int j = 0; j < 72; ++j)
            CHECK(reinterpret_cast<const std::uint8_t*>(rlbs.tmp_save_above[k])[j] ==
                  (std::uint8_t)((16 - 3 + k) * 7 + 4 + j));
    // Outside the line window: untouched.
    CHECK(data[(16 - 3) * stride + 3] == (std::uint8_t)(13 * 7 + 3));
    CHECK(data[(16 - 1) * stride + 4 + 72] == (std::uint8_t)(15 * 7 + 76));
    // The stripe body itself is untouched by setup.
    CHECK(data[16 * stride + 8] == (std::uint8_t)(16 * 7 + 8));
}

// LR3-S5: setup_processing_stripe_boundary, the NON-opt copy_below path:
// rows stripe_end..stripe_end+2 (stripe_end = v_start + h) are replaced from
// rsb->stripe_boundary_below at buf rows rsb_row + AOMMIN(i, CTX_VERT-1) =
// rsb_row + {0,1,1}, saved into tmp_save_below[{0,1,2}].
TEST_CASE("setupProcessingStripeBoundary non-opt below: replace + save") {
    using l10_filters::restoration::setupProcessingStripeBoundary;
    using l10_filters::restoration::RestorationTileLimits;
    using l10_filters::restoration::RestorationStripeBoundaries;
    using l10_filters::restoration::RestorationLineBuffers;
    const int stride = 96, bufStride = 128;
    std::vector<std::uint8_t> data(70 * stride, 0);
    for (int i = 0; i < 70; ++i)
        for (int j = 0; j < stride; ++j) data[i * stride + j] = (std::uint8_t)(i * 5 + j + 3);
    std::vector<std::uint8_t> below(20 * bufStride, 0);
    for (int i = 0; i < 20; ++i)
        for (int j = 0; j < bufStride; ++j) below[i * bufStride + j] = (std::uint8_t)(200 + i + j);
    RestorationStripeBoundaries rsb{};
    rsb.stripe_boundary_below = below.data();
    rsb.stripe_boundary_stride = bufStride;
    RestorationLineBuffers rlbs{};
    RestorationTileLimits lim{8, 72, 16, 48};
    setupProcessingStripeBoundary(&lim, &rsb, 5, 0, 32, data.data(), stride, &rlbs, 0, 1, 0);
    // stripe_end = 48: rows 48,49,50 get below rows 5,6,6 from offset 8.
    for (int j = 0; j < 72; ++j) {
        CHECK(data[48 * stride + 4 + j] == below[5 * bufStride + 8 + j]);
        CHECK(data[49 * stride + 4 + j] == below[6 * bufStride + 8 + j]);
        CHECK(data[50 * stride + 4 + j] == below[6 * bufStride + 8 + j]);
    }
    for (int k = 0; k < 3; ++k)
        for (int j = 0; j < 72; ++j)
            CHECK(reinterpret_cast<const std::uint8_t*>(rlbs.tmp_save_below[k])[j] ==
                  (std::uint8_t)((48 + k) * 5 + 4 + j + 3));
    // Above rows untouched (copy_above = 0), stripe body untouched.
    CHECK(data[15 * stride + 4] == (std::uint8_t)(15 * 5 + 7));
    CHECK(data[16 * stride + 8] == (std::uint8_t)(16 * 5 + 11));
}

// LR3-S6: setup_processing_stripe_boundary, the OPT (optimized_lr) path:
// only the single outermost line each side is saved+overwritten - above:
// row v_start-3 <- row v_start-2; below: row stripe_end+2 <- row
// stripe_end+1. The interior rows are untouched.
TEST_CASE("setupProcessingStripeBoundary opt: single outermost line each side") {
    using l10_filters::restoration::setupProcessingStripeBoundary;
    using l10_filters::restoration::RestorationTileLimits;
    using l10_filters::restoration::RestorationStripeBoundaries;
    using l10_filters::restoration::RestorationLineBuffers;
    const int stride = 96;
    std::vector<std::uint8_t> data(70 * stride, 0);
    for (int i = 0; i < 70; ++i)
        for (int j = 0; j < stride; ++j) data[i * stride + j] = (std::uint8_t)(i * 2 + j + 1);
    RestorationStripeBoundaries rsb{}; // boundary buffers never read on the opt path
    RestorationLineBuffers rlbs{};
    RestorationTileLimits lim{8, 72, 16, 48};
    setupProcessingStripeBoundary(&lim, &rsb, 5, 0, 32, data.data(), stride, &rlbs, 1, 1, 1);
    // Above: row 13 <- row 14; rows 14, 15 untouched.
    for (int j = 0; j < 72; ++j) CHECK(data[13 * stride + 4 + j] == (std::uint8_t)(14 * 2 + 4 + j + 1));
    CHECK(data[14 * stride + 4] == (std::uint8_t)(14 * 2 + 4 + 1));
    CHECK(data[15 * stride + 4] == (std::uint8_t)(15 * 2 + 4 + 1));
    for (int j = 0; j < 72; ++j)
        CHECK(reinterpret_cast<const std::uint8_t*>(rlbs.tmp_save_above[0])[j] ==
              (std::uint8_t)(13 * 2 + 4 + j + 1));
    // Below: stripe_end = 48; row 50 <- row 49; rows 48, 49 untouched.
    for (int j = 0; j < 72; ++j) CHECK(data[50 * stride + 4 + j] == (std::uint8_t)(49 * 2 + 4 + j + 1));
    CHECK(data[48 * stride + 4] == (std::uint8_t)(48 * 2 + 4 + 1));
    CHECK(data[49 * stride + 4] == (std::uint8_t)(49 * 2 + 4 + 1));
    for (int j = 0; j < 72; ++j)
        CHECK(reinterpret_cast<const std::uint8_t*>(rlbs.tmp_save_below[2])[j] ==
              (std::uint8_t)(50 * 2 + 4 + j + 1));
}

// LR3-S7: restore_processing_stripe_boundary (restoration.c:347-:397): the
// saved lines are written back. Roundtrip: setup (non-opt, both flags),
// scribble every touched row, restore -> the 3 above rows and the 3 below
// rows return to their pre-setup values. The opt path restores only the
// single outermost line each side.
TEST_CASE("restoreProcessingStripeBoundary roundtrip non-opt and opt") {
    using l10_filters::restoration::setupProcessingStripeBoundary;
    using l10_filters::restoration::restoreProcessingStripeBoundary;
    using l10_filters::restoration::RestorationTileLimits;
    using l10_filters::restoration::RestorationStripeBoundaries;
    using l10_filters::restoration::RestorationLineBuffers;
    const int stride = 96;
    std::vector<std::uint8_t> data(70 * stride, 0), orig(70 * stride, 0);
    for (int i = 0; i < 70; ++i)
        for (int j = 0; j < stride; ++j) {
            data[i * stride + j] = (std::uint8_t)(i * 2 + j + 1);
            orig[i * stride + j] = (std::uint8_t)(i * 2 + j + 1);
        }
    RestorationStripeBoundaries rsb{};
    std::vector<std::uint8_t> above(20 * 128, 0), below(20 * 128, 0);
    for (int i = 0; i < 20; ++i)
        for (int j = 0; j < 128; ++j) {
            above[i * 128 + j] = (std::uint8_t)(i + j);
            below[i * 128 + j] = (std::uint8_t)(i + j + 1);
        }
    rsb.stripe_boundary_above = above.data();
    rsb.stripe_boundary_below = below.data();
    rsb.stripe_boundary_stride = 128;
    RestorationLineBuffers rlbs{};
    RestorationTileLimits lim{8, 72, 16, 48};
    // Non-opt: all 3+3 rows.
    setupProcessingStripeBoundary(&lim, &rsb, 5, 0, 32, data.data(), stride, &rlbs, 1, 1, 0);
    for (int i = 13; i < 16; ++i)
        for (int j = 0; j < 72; ++j) data[i * stride + 4 + j] = 0xAA;
    for (int i = 48; i < 51; ++i)
        for (int j = 0; j < 72; ++j) data[i * stride + 4 + j] = 0xBB;
    restoreProcessingStripeBoundary(&lim, &rlbs, 0, 32, data.data(), stride, 1, 1, 0);
    for (int i = 13; i < 16; ++i)
        for (int j = 0; j < 72; ++j) CHECK(data[i * stride + 4 + j] == orig[i * stride + 4 + j]);
    for (int i = 48; i < 51; ++i)
        for (int j = 0; j < 72; ++j) CHECK(data[i * stride + 4 + j] == orig[i * stride + 4 + j]);
    // Opt: only the outermost lines.
    RestorationLineBuffers rlbs2{};
    setupProcessingStripeBoundary(&lim, &rsb, 5, 0, 32, data.data(), stride, &rlbs2, 1, 1, 1);
    for (int i = 13; i < 16; ++i)
        for (int j = 0; j < 72; ++j) data[i * stride + 4 + j] = 0xAA;
    for (int i = 48; i < 51; ++i)
        for (int j = 0; j < 72; ++j) data[i * stride + 4 + j] = 0xBB;
    restoreProcessingStripeBoundary(&lim, &rlbs2, 0, 32, data.data(), stride, 1, 1, 1);
    for (int j = 0; j < 72; ++j) CHECK(data[13 * stride + 4 + j] == orig[13 * stride + 4 + j]);
    for (int j = 0; j < 72; ++j) CHECK(data[14 * stride + 4 + j] == 0xAA); // interior not restored
    for (int j = 0; j < 72; ++j) CHECK(data[50 * stride + 4 + j] == orig[50 * stride + 4 + j]);
    for (int j = 0; j < 72; ++j) CHECK(data[49 * stride + 4 + j] == 0xBB); // interior not restored
}

// LR3-S8: the restore below-loop break rule (:369-:371): with h overshooting
// the unit (h = 34, stripe_bottom = 50 > v_end = 48), the guard
// stripe_bottom + i >= v_end + RESTORATION_BORDER fires at i = 1 (51 >= 51),
// so only i = 0 is restored; rows 51, 52 keep the scribble.
TEST_CASE("restoreProcessingStripeBoundary below loop breaks at v_end + border") {
    using l10_filters::restoration::setupProcessingStripeBoundary;
    using l10_filters::restoration::restoreProcessingStripeBoundary;
    using l10_filters::restoration::RestorationTileLimits;
    using l10_filters::restoration::RestorationStripeBoundaries;
    using l10_filters::restoration::RestorationLineBuffers;
    const int stride = 96;
    std::vector<std::uint8_t> data(70 * stride, 0), orig(70 * stride, 0);
    for (int i = 0; i < 70; ++i)
        for (int j = 0; j < stride; ++j) {
            data[i * stride + j] = (std::uint8_t)(i * 2 + j + 1);
            orig[i * stride + j] = (std::uint8_t)(i * 2 + j + 1);
        }
    RestorationStripeBoundaries rsb{};
    std::vector<std::uint8_t> below(20 * 128, 1);
    rsb.stripe_boundary_below = below.data();
    rsb.stripe_boundary_stride = 128;
    RestorationLineBuffers rlbs{};
    RestorationTileLimits lim{8, 72, 16, 48};
    setupProcessingStripeBoundary(&lim, &rsb, 5, 0, 34, data.data(), stride, &rlbs, 0, 1, 0);
    for (int i = 50; i < 53; ++i)
        for (int j = 0; j < 72; ++j) data[i * stride + 4 + j] = 0xCC;
    restoreProcessingStripeBoundary(&lim, &rlbs, 0, 34, data.data(), stride, 0, 1, 0);
    for (int j = 0; j < 72; ++j) CHECK(data[50 * stride + 4 + j] == orig[50 * stride + 4 + j]);
    for (int j = 0; j < 72; ++j) CHECK(data[51 * stride + 4 + j] == 0xCC); // break skipped i=1, 2
    for (int j = 0; j < 72; ++j) CHECK(data[52 * stride + 4 + j] == 0xCC);
}

// LR3-S9: sgrproj_filter_stripe (restoration.c:964-:985): per procunit
// slice j, w = AOMMIN(procunit_width, stripe_width - j) - the PLAIN min, no
// round-up. The whole-stripe dst must equal per-slice direct calls to the
// landed applySelfguidedRestoration with the same w per slice. stripe_width
// 40 / procunit 32 -> slices w = 32 and w = 8.
TEST_CASE("sgrprojFilterStripe slices at the plain AOMMIN width rule") {
    using l10_filters::restoration::sgrprojFilterStripe;
    using l10_filters::restoration::RestorationUnitInfo;
    using l10_filters::restoration::applySelfguidedRestoration;
    const int stripeW = 40, stripeH = 6, procunit = 32, stride = stripeW + 16;
    std::vector<std::uint8_t> plane((stripeH + 8) * stride, 0), expD((stripeH + 8) * stride, 0x11),
        dst((stripeH + 8) * stride, 0x11);
    for (int i = 0; i < stripeH + 8; ++i)
        for (int j = 0; j < stride; ++j) plane[i * stride + j] = (std::uint8_t)(1 + (i * 13 + j * 7) % 251);
    const std::uint8_t* src = plane.data() + 4 * stride + 4;
    std::uint8_t* dstP = dst.data() + 4 * stride + 4;
    std::uint8_t* expP = expD.data() + 4 * stride + 4;
    RestorationUnitInfo rui{};
    rui.sgrproj_info.ep = 0;
    rui.sgrproj_info.xqd[0] = 5;
    rui.sgrproj_info.xqd[1] = 3;
    std::vector<std::int32_t> tmpbuf(2 * l10_filters::restoration::restorationUnitpelsMax, -424242);
    // Oracle: the two slices by hand, w = 32 then w = 8.
    applySelfguidedRestoration(src, 32, stripeH, stride, 0, rui.sgrproj_info.xqd, expP, stride, tmpbuf.data(), 8,
        0);
    applySelfguidedRestoration(src + 32, 8, stripeH, stride, 0, rui.sgrproj_info.xqd, expP + 32, stride,
        tmpbuf.data(), 8, 0);
    sgrprojFilterStripe(&rui, stripeW, stripeH, procunit, src, stride, dstP, stride, tmpbuf.data(), 8);
    for (int i = 0; i < stripeH; ++i)
        for (int j = 0; j < stripeW; ++j) CHECK(dstP[i * stride + j] == expP[i * stride + j]);
}

// LR3-S10: wiener_filter_stripe (restoration.c:399-:421): per procunit
// slice j, w = AOMMIN(procunit_width, (stripe_width - j + 15) & ~15) - the
// ROUND-UP rule, NOT sgrproj's plain min: at j = 32 the slice w is 16 even
// though only 8 stripe columns remain, so the convolve OVERFLOWS the stripe
// into dst cols 40..48. The comparison therefore covers [0, 48) - copying
// sgrproj's w rule leaves the overflow cols at the fill and fails here.
// Oracle: direct calls to the landed convolve::wienerConvolveAddSrc with
// the same w per slice and the same filter pointers.
TEST_CASE("wienerFilterStripe slices at the round-up width rule") {
    using l10_filters::restoration::wienerFilterStripe;
    using l10_filters::restoration::RestorationUnitInfo;
    using convolve::wienerConvolveAddSrc;
    using convolve::getConvParamsWiener;
    const int stripeW = 40, stripeH = 6, procunit = 32, stride = stripeW + 16;
    std::vector<std::uint8_t> plane((stripeH + 8) * stride, 0), expD((stripeH + 8) * stride, 0x11),
        dst((stripeH + 8) * stride, 0x11);
    for (int i = 0; i < stripeH + 8; ++i)
        for (int j = 0; j < stride; ++j) plane[i * stride + j] = (std::uint8_t)(1 + (i * 13 + j * 7) % 251);
    const std::uint8_t* src = plane.data() + 4 * stride + 4;
    std::uint8_t* dstP = dst.data() + 4 * stride + 4;
    std::uint8_t* expP = expD.data() + 4 * stride + 4;
    RestorationUnitInfo rui{};
    for (int k = 0; k < 8; ++k) {
        rui.wiener_info.hfilter[k] = kWienerDefaultTaps[k];
        rui.wiener_info.vfilter[k] = kWienerDefaultTaps[k];
    }
    const convolve::ConvolveParams cp = getConvParamsWiener(8);
    // Oracle: the two slices by hand, w = 32 then w = 16 (the round-up).
    wienerConvolveAddSrc(src, stride, expP, stride, rui.wiener_info.hfilter, rui.wiener_info.vfilter, 32, stripeH,
        &cp);
    wienerConvolveAddSrc(src + 32, stride, expP + 32, stride, rui.wiener_info.hfilter, rui.wiener_info.vfilter, 16,
        stripeH, &cp);
    wienerFilterStripe(&rui, stripeW, stripeH, procunit, src, stride, dstP, stride, nullptr, 8);
    // The overflow cols are written by the round-up slice: prove the fixture
    // really distinguishes the two w rules before comparing.
    int overflowWritten = 1;
    for (int i = 0; i < stripeH; ++i)
        for (int j = 40; j < 48; ++j)
            if (expP[i * stride + j] != 0x11) overflowWritten = 0;
    CHECK(overflowWritten == 0);
    for (int i = 0; i < stripeH; ++i)
        for (int j = 0; j < 48; ++j) CHECK(dstP[i * stride + j] == expP[i * stride + j]);
}

// LR3-S11a: svt_av1_loop_restoration_filter_unit (restoration.c:1040-:1110),
// the RESTORE_NONE branch: the unit rect is copied data8 -> dst8 via the
// landed copyTile and nothing else runs.
TEST_CASE("loopRestorationFilterUnit RESTORE_NONE copies the unit rect") {
    using l10_filters::restoration::loopRestorationFilterUnit;
    using l10_filters::restoration::RestorationTileLimits;
    using l10_filters::restoration::RestorationUnitInfo;
    using l10_filters::restoration::RestorationStripeBoundaries;
    using l10_filters::restoration::RestorationLineBuffers;
    using l10_filters::restoration::Av1PixelRect;
    using l10_filters::restoration::RESTORE_NONE;
    const int stride = 96;
    std::vector<std::uint8_t> data(60 * stride, 0), dst(60 * stride, 0x22);
    for (int i = 0; i < 60; ++i)
        for (int j = 0; j < stride; ++j) data[i * stride + j] = (std::uint8_t)(i * 3 + j);
    RestorationUnitInfo rui{};
    rui.restoration_type = RESTORE_NONE;
    RestorationTileLimits lim{8, 72, 16, 48};
    RestorationStripeBoundaries rsb{};
    RestorationLineBuffers rlbs{};
    Av1PixelRect tile{0, 0, 1920, 64};
    loopRestorationFilterUnit(0, &lim, &rui, &rsb, &rlbs, &tile, 0, 0, 0, 0, 8, data.data(), stride, dst.data(),
        stride, nullptr, 0);
    for (int i = 16; i < 48; ++i)
        for (int j = 8; j < 72; ++j) CHECK(dst[i * stride + j] == data[i * stride + j]);
    CHECK(dst[16 * stride + 7] == 0x22);  // outside the unit rect
    CHECK(dst[15 * stride + 8] == 0x22);
    CHECK(dst[48 * stride + 8] == 0x22);
    CHECK(dst[16 * stride + 72] == 0x22);
}

// LR3-S11b: the stripe walk, WIENER unit, need_boundaries = 0. A 64x64 unit
// at a tile with top 0 / bottom 64 splits into a 56-high first stripe (the
// runit_offset reduction) and an 8-high remainder: h = AOMMIN(nominal,
// v_end - v_start) with nominal = 64 - (tile_stripe == 0 ? 8 : 0). Oracle:
// direct wienerFilterStripe calls at the same rows with the same shapes.
TEST_CASE("loopRestorationFilterUnit walks wiener stripes 56+8") {
    using l10_filters::restoration::loopRestorationFilterUnit;
    using l10_filters::restoration::wienerFilterStripe;
    using l10_filters::restoration::RestorationTileLimits;
    using l10_filters::restoration::RestorationUnitInfo;
    using l10_filters::restoration::RestorationStripeBoundaries;
    using l10_filters::restoration::RestorationLineBuffers;
    using l10_filters::restoration::Av1PixelRect;
    using l10_filters::restoration::RESTORE_WIENER;
    const int stride = 96;
    std::vector<std::uint8_t> data(90 * stride, 0), dst(90 * stride, 0x22), expD(90 * stride, 0x22);
    for (int i = 0; i < 90; ++i)
        for (int j = 0; j < stride; ++j) data[i * stride + j] = (std::uint8_t)(1 + (i * 11 + j * 5) % 250);
    RestorationUnitInfo rui{};
    rui.restoration_type = RESTORE_WIENER;
    for (int k = 0; k < 8; ++k) {
        rui.wiener_info.hfilter[k] = kWienerDefaultTaps[k];
        rui.wiener_info.vfilter[k] = kWienerDefaultTaps[k];
    }
    RestorationTileLimits lim{8, 72, 16, 80};
    RestorationStripeBoundaries rsb{};
    RestorationLineBuffers rlbs{};
    Av1PixelRect tile{0, 0, 1920, 64};
    loopRestorationFilterUnit(0, &lim, &rui, &rsb, &rlbs, &tile, 0, 0, 0, 0, 8, data.data(), stride, expD.data(),
        stride, nullptr, 0);
    // Oracle: the two stripes by hand.
    std::uint8_t* dataTl = data.data() + 16 * stride + 8;
    std::uint8_t* dstP = dst.data() + 16 * stride + 8;
    wienerFilterStripe(&rui, 64, 56, 64, dataTl, stride, dstP, stride, nullptr, 8);
    wienerFilterStripe(&rui, 64, 8, 64, dataTl + 56 * stride, stride, dstP + 56 * stride, stride, nullptr, 8);
    for (int i = 16; i < 80; ++i)
        for (int j = 8; j < 72; ++j) CHECK(dst[i * stride + j] == expD[i * stride + j]);
    CHECK(dst[15 * stride + 8] == 0x22);
    CHECK(dst[80 * stride + 8] == 0x22);
}

// LR3-S11c: the dispatch - a RESTORE_SGRPROJ unit runs sgrprojFilterStripe,
// not wienerFilterStripe, even though BOTH filter sets are populated on the
// unit (a wiener misdispatch produces different pixels and fails).
TEST_CASE("loopRestorationFilterUnit dispatches SGRPROJ to the sgrproj stripe") {
    using l10_filters::restoration::loopRestorationFilterUnit;
    using l10_filters::restoration::sgrprojFilterStripe;
    using l10_filters::restoration::RestorationTileLimits;
    using l10_filters::restoration::RestorationUnitInfo;
    using l10_filters::restoration::RestorationStripeBoundaries;
    using l10_filters::restoration::RestorationLineBuffers;
    using l10_filters::restoration::Av1PixelRect;
    using l10_filters::restoration::RESTORE_SGRPROJ;
    using l10_filters::restoration::restorationUnitpelsMax;
    const int stride = 96;
    std::vector<std::uint8_t> data(90 * stride, 0), dst(90 * stride, 0x22), expD(90 * stride, 0x22);
    for (int i = 0; i < 90; ++i)
        for (int j = 0; j < stride; ++j) data[i * stride + j] = (std::uint8_t)(1 + (i * 11 + j * 5) % 250);
    RestorationUnitInfo rui{};
    rui.restoration_type = RESTORE_SGRPROJ;
    rui.sgrproj_info.ep = 0;
    rui.sgrproj_info.xqd[0] = 5;
    rui.sgrproj_info.xqd[1] = 3;
    for (int k = 0; k < 8; ++k) {
        rui.wiener_info.hfilter[k] = kWienerDefaultTaps[k];
        rui.wiener_info.vfilter[k] = kWienerDefaultTaps[k];
    }
    RestorationTileLimits lim{8, 72, 16, 80};
    RestorationStripeBoundaries rsb{};
    RestorationLineBuffers rlbs{};
    Av1PixelRect tile{0, 0, 1920, 64};
    std::vector<std::int32_t> tmpbuf(2 * restorationUnitpelsMax, -424242);
    loopRestorationFilterUnit(0, &lim, &rui, &rsb, &rlbs, &tile, 0, 0, 0, 0, 8, data.data(), stride, expD.data(),
        stride, tmpbuf.data(), 0);
    // Oracle: the two sgrproj stripes by hand.
    std::uint8_t* dataTl = data.data() + 16 * stride + 8;
    std::uint8_t* dstP = dst.data() + 16 * stride + 8;
    sgrprojFilterStripe(&rui, 64, 56, 64, dataTl, stride, dstP, stride, tmpbuf.data(), 8);
    sgrprojFilterStripe(&rui, 64, 8, 64, dataTl + 56 * stride, stride, dstP + 56 * stride, stride, tmpbuf.data(), 8);
    for (int i = 16; i < 80; ++i)
        for (int j = 8; j < 72; ++j) CHECK(dst[i * stride + j] == expD[i * stride + j]);
}

// LR3-S11d: need_boundaries = 1 - the walk composes
// setup_processing_stripe_boundary / stripe filter / restore per stripe with
// the rsb_row = RESTORATION_CTX_VERT * (tile_stripe0 + tile_stripe) math and
// the copy_above/copy_below flags from getStripeBoundaryInfo. Oracle: the
// same per-stripe sequence driven by hand on the same data plane. The
// boundary rows the stripes filter THROUGH are the replaced ones, so a
// wrong rsb_row or flag computation diverges the output.
TEST_CASE("loopRestorationFilterUnit need_boundaries composes setup/filter/restore") {
    using l10_filters::restoration::loopRestorationFilterUnit;
    using l10_filters::restoration::setupProcessingStripeBoundary;
    using l10_filters::restoration::restoreProcessingStripeBoundary;
    using l10_filters::restoration::wienerFilterStripe;
    using l10_filters::restoration::getStripeBoundaryInfo;
    using l10_filters::restoration::RestorationTileLimits;
    using l10_filters::restoration::RestorationUnitInfo;
    using l10_filters::restoration::RestorationStripeBoundaries;
    using l10_filters::restoration::RestorationLineBuffers;
    using l10_filters::restoration::Av1PixelRect;
    using l10_filters::restoration::RESTORE_WIENER;
    const int stride = 96, bufStride = 128;
    std::vector<std::uint8_t> data(90 * stride, 0), dst(90 * stride, 0x22), expD(90 * stride, 0x22);
    for (int i = 0; i < 90; ++i)
        for (int j = 0; j < stride; ++j) data[i * stride + j] = (std::uint8_t)(1 + (i * 11 + j * 5) % 250);
    RestorationUnitInfo rui{};
    rui.restoration_type = RESTORE_WIENER;
    for (int k = 0; k < 8; ++k) {
        rui.wiener_info.hfilter[k] = kWienerDefaultTaps[k];
        rui.wiener_info.vfilter[k] = kWienerDefaultTaps[k];
    }
    RestorationStripeBoundaries rsb{};
    std::vector<std::uint8_t> above(20 * bufStride, 0), below(20 * bufStride, 0);
    for (int i = 0; i < 20; ++i)
        for (int j = 0; j < bufStride; ++j) {
            above[i * bufStride + j] = (std::uint8_t)(30 + i + j);
            below[i * bufStride + j] = (std::uint8_t)(90 + i + j);
        }
    rsb.stripe_boundary_above = above.data();
    rsb.stripe_boundary_below = below.data();
    rsb.stripe_boundary_stride = bufStride;
    RestorationLineBuffers rlbs{};
    Av1PixelRect tile{0, 0, 1920, 200};
    RestorationTileLimits lim{8, 72, 16, 80};
    loopRestorationFilterUnit(1, &lim, &rui, &rsb, &rlbs, &tile, 0, 0, 0, 0, 8, data.data(), stride, dst.data(),
        stride, nullptr, 0);
    // Oracle: the walk by hand. Stripe 1: v_start 16, tile_stripe 0, rsb_row
    // 0, h 56, flags (1, 1) - the unit does NOT start at the tile top, and
    // the 200-high tile keeps both boundaries live. Stripe 2: v_start 72,
    // tile_stripe 1, rsb_row 2, h 8, flags (1, 1). Limits are ABSOLUTE, like
    // the walk's.
    RestorationLineBuffers orlbs{};
    std::uint8_t* dataTl = data.data() + 16 * stride + 8;
    std::uint8_t* dstP = expD.data() + 16 * stride + 8;
    RestorationTileLimits s1{8, 72, 16, 80};
    int ca, cb;
    getStripeBoundaryInfo(&s1, &tile, 0, &ca, &cb);
    CHECK(ca == 1); CHECK(cb == 1);
    setupProcessingStripeBoundary(&s1, &rsb, 0, 0, 56, data.data(), stride, &orlbs, ca, cb, 0);
    wienerFilterStripe(&rui, 64, 56, 64, dataTl, stride, dstP, stride, nullptr, 8);
    restoreProcessingStripeBoundary(&s1, &orlbs, 0, 56, data.data(), stride, ca, cb, 0);
    RestorationTileLimits s2{8, 72, 72, 80};
    getStripeBoundaryInfo(&s2, &tile, 0, &ca, &cb);
    CHECK(ca == 1); CHECK(cb == 1);
    setupProcessingStripeBoundary(&s2, &rsb, 2, 0, 8, data.data(), stride, &orlbs, ca, cb, 0);
    wienerFilterStripe(&rui, 64, 8, 64, dataTl + 56 * stride, stride, dstP + 56 * stride, stride, nullptr, 8);
    restoreProcessingStripeBoundary(&s2, &orlbs, 0, 8, data.data(), stride, ca, cb, 0);
    for (int i = 16; i < 80; ++i)
        for (int j = 8; j < 72; ++j) CHECK(dst[i * stride + j] == expD[i * stride + j]);
    // The data plane's boundary rows are restored after the walk.
    for (int i = 13; i < 19; ++i)
        for (int j = 4; j < 80; ++j) CHECK(data[i * stride + j] == (std::uint8_t)(1 + (i * 11 + j * 5) % 250));
}

namespace {
// Independent oracle for the boxsum family: the reference's vertical-then-
// horizontal prefix walk composes to a 2D box sum CLAMPED at the given
// array bounds (edge rows/cols get truncated sums; it never reads outside
// width/height). Derived by tracing restoration.c:436-:494.
void boxOracle(const std::int32_t* src, int w, int h, int stride, int r, int sqr, std::int32_t* out, int outStride) {
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            std::int32_t sum = 0;
            for (int y = (i - r > 0 ? i - r : 0); y <= (i + r < h - 1 ? i + r : h - 1); ++y)
                for (int x = (j - r > 0 ? j - r : 0); x <= (j + r < w - 1 ? j + r : w - 1); ++x) {
                    const std::int32_t v = src[y * stride + x];
                    sum += sqr ? v * v : v;
                }
            out[i * outStride + j] = sum;
        }
    }
}
} // namespace

// S2: boxsum1 radius-1, plain (sqr=0). All-ones 7x7: the clamped box count
// is 4 at corners, 6 at edge centers, 9 in the interior.
TEST_CASE("boxsum1 plain all-ones 7x7: clamped box geometry") {
    std::int32_t src[8 * 7];
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 8; ++j) src[i * 8 + j] = (j < 7) ? 1 : 9999;
    std::int32_t dst[8 * 7] = {0};
    l10_filters::restoration::boxsum1(src, 7, 7, 8, 0, dst, 8);
    CHECK(dst[0] == 4);       // corner
    CHECK(dst[3] == 6);       // top edge center
    CHECK(dst[3 * 8 + 0] == 6); // left edge center
    CHECK(dst[3 * 8 + 3] == 9); // interior
    CHECK(dst[6 * 8 + 6] == 4); // far corner
}

// S2 oracle fixture: distinct values, 7x9 grid, stride 12 (padding 9999
// proves no out-of-bounds reads), checked cell-by-cell against the oracle.
TEST_CASE("boxsum1 plain 7x9 matches the clamped-box oracle") {
    std::int32_t src[12 * 7];
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 12; ++j) src[i * 12 + j] = (j < 9) ? 1 + i * 9 + j : 9999;
    std::int32_t dst[12 * 7] = {0};
    std::int32_t exp[12 * 7] = {0};
    boxOracle(src, 9, 7, 12, 1, 0, exp, 12);
    l10_filters::restoration::boxsum1(src, 9, 7, 12, 0, dst, 12);
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 9; ++j) CHECK(dst[i * 12 + j] == exp[i * 12 + j]);
}

// S3: boxsum1 with sqr=1 on a fixture where the squared sum differs from
// the plain sum (values > 1).
TEST_CASE("boxsum1 sqr=1 7x9 matches the squared-box oracle") {
    std::int32_t src[12 * 7];
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 12; ++j) src[i * 12 + j] = (j < 9) ? 1 + i * 9 + j : 9999;
    std::int32_t dst[12 * 7] = {0};
    std::int32_t exp[12 * 7] = {0};
    boxOracle(src, 9, 7, 12, 1, 1, exp, 12);
    l10_filters::restoration::boxsum1(src, 9, 7, 12, 1, dst, 12);
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 9; ++j) CHECK(dst[i * 12 + j] == exp[i * 12 + j]);
    // The squared sum differs from the plain sum on this fixture.
    CHECK(dst[3 * 12 + 4] != 25);
}

// S4: boxsum2 radius-2. All-ones 7x7: the clamped 5x5 box count is 9 at
// corners, 15 at edge centers, 25 in the interior.
TEST_CASE("boxsum2 plain all-ones 7x7: clamped box geometry") {
    std::int32_t src[8 * 7];
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 8; ++j) src[i * 8 + j] = (j < 7) ? 1 : 9999;
    std::int32_t dst[8 * 7] = {0};
    l10_filters::restoration::boxsum2(src, 7, 7, 8, 0, dst, 8);
    CHECK(dst[0] == 9);         // corner: 3x3
    CHECK(dst[0 * 8 + 3] == 15); // top edge: 3x5
    CHECK(dst[3 * 8 + 0] == 15); // left edge: 5x3
    CHECK(dst[3 * 8 + 3] == 25); // interior: 5x5
    CHECK(dst[6 * 8 + 6] == 9);  // far corner: 3x3
}

// S4 oracle fixture: distinct values, 7x9 grid, checked against the oracle.
TEST_CASE("boxsum2 plain 7x9 matches the clamped-box oracle") {
    std::int32_t src[12 * 7];
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 12; ++j) src[i * 12 + j] = (j < 9) ? 1 + i * 9 + j : 9999;
    std::int32_t dst[12 * 7] = {0};
    std::int32_t exp[12 * 7] = {0};
    boxOracle(src, 9, 7, 12, 2, 0, exp, 12);
    l10_filters::restoration::boxsum2(src, 9, 7, 12, 0, dst, 12);
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 9; ++j) CHECK(dst[i * 12 + j] == exp[i * 12 + j]);
}

// S5: boxsum dispatch wrapper. On this fixture r=1 and r=2 differ; the
// dispatch must select boxsum1 for r=1 and boxsum2 for r=2.
TEST_CASE("boxsum dispatch selects radius 1 vs 2 correctly") {
    std::int32_t src[12 * 7];
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 12; ++j) src[i * 12 + j] = (j < 9) ? 1 + i * 9 + j : 9999;
    std::int32_t dst1[12 * 7] = {0};
    std::int32_t dst2[12 * 7] = {0};
    std::int32_t exp1[12 * 7] = {0};
    std::int32_t exp2[12 * 7] = {0};
    boxOracle(src, 9, 7, 12, 1, 0, exp1, 12);
    boxOracle(src, 9, 7, 12, 2, 0, exp2, 12);
    l10_filters::restoration::boxsum(src, 9, 7, 12, 1, 0, dst1, 12);
    l10_filters::restoration::boxsum(src, 9, 7, 12, 2, 0, dst2, 12);
    for (int i = 0; i < 7; ++i)
        for (int j = 0; j < 9; ++j) {
            CHECK(dst1[i * 12 + j] == exp1[i * 12 + j]);
            CHECK(dst2[i * 12 + j] == exp2[i * 12 + j]);
        }
    // The two radii genuinely differ here (so the dispatch is observable).
    CHECK(dst1[3 * 12 + 4] != dst2[3 * 12 + 4]);
}

// S6: svt_decode_xq (:597-:608), the params-aware version (BRIEF CORRECTION:
// not the spec-style 96/128-3x mapping). SGRPROJ_PRJ_BITS = 7, so the base
// is 128. Rows: 0 = both radii nonzero, 10 = r[0]==0, 14 = r[1]==0.
TEST_CASE("decodeXq both radii nonzero: xq1 = 128 - xq0 - xqd1") {
    const std::int32_t xqd[2] = {5, 3};
    std::int32_t xq[2] = {0, 0};
    l10_filters::restoration::decodeXq(xqd, xq, &l10_filters::restoration::sgrParams[0]);
    CHECK(xq[0] == 5);
    CHECK(xq[1] == 120);
}

TEST_CASE("decodeXq r[0]==0 (row 10): xq0 pinned to 0") {
    const std::int32_t xqd[2] = {5, 3};
    std::int32_t xq[2] = {0, 0};
    l10_filters::restoration::decodeXq(xqd, xq, &l10_filters::restoration::sgrParams[10]);
    CHECK(xq[0] == 0);
    CHECK(xq[1] == 125);
}

TEST_CASE("decodeXq r[1]==0 (row 14): xq1 pinned to 0") {
    const std::int32_t xqd[2] = {5, 3};
    std::int32_t xq[2] = {0, 0};
    l10_filters::restoration::decodeXq(xqd, xq, &l10_filters::restoration::sgrParams[14]);
    CHECK(xq[0] == 5);
    CHECK(xq[1] == 0);
}

TEST_CASE("decodeXq xqd[0]==0 edge: xq0 = 0, xq1 = 128 - xqd1") {
    const std::int32_t xqd[2] = {0, 3};
    std::int32_t xq[2] = {0, 0};
    l10_filters::restoration::decodeXq(xqd, xq, &l10_filters::restoration::sgrParams[0]);
    CHECK(xq[0] == 0);
    CHECK(xq[1] == 125);
}

// S7: x_by_xplus1 (:610-:625). Oracle derived against the extracted table:
// entry 0 = 1 (the named special case), entry 255 = 256 (the boundary),
// every other entry = floor((256*x + (x+1)/2) / (x+1)) with C integer
// division (0 mismatches over all 256 entries).
TEST_CASE("xByXplus1 boundaries: 0 -> 1 and 255 -> 256") {
    CHECK(l10_filters::restoration::xByXplus1[0] == 1);
    CHECK(l10_filters::restoration::xByXplus1[1] == 128);
    CHECK(l10_filters::restoration::xByXplus1[255] == 256);
}

TEST_CASE("xByXplus1 matches the rounding formula over all 256 entries") {
    for (int x = 0; x < 256; ++x) {
        std::int32_t f;
        if (x == 0) {
            f = 1;
        } else if (x == 255) {
            f = 256;
        } else {
            const std::int32_t t = (x + 1) / 2;
            f = (256 * x + t) / (x + 1);
        }
        CHECK(l10_filters::restoration::xByXplus1[x] == f);
    }
}

// S8: one_by_x (:627-:630), 25 entries = MAX_NELEM (MAX_RADIUS = 2).
// Oracle: floor((4096 + (x+1)/2) / (x+1)), 0 mismatches over all 25.
TEST_CASE("oneByX boundaries and length") {
    CHECK(l10_filters::restoration::oneByX[0] == 4096);
    CHECK(l10_filters::restoration::oneByX[24] == 164);
}

TEST_CASE("oneByX matches the rounding formula over all 25 entries") {
    for (int x = 0; x < 25; ++x) {
        const std::int32_t t = (x + 1) / 2;
        CHECK(l10_filters::restoration::oneByX[x] == (4096 + t) / (x + 1));
    }
}

// S9: extend_frame_lowbd (:110-:126). 8x4 tile with border_horz = 2,
// border_vert = 2 inside a sentinel-filled plane (stride 16). Side borders
// replicate the row-end pixels; the top/bottom borders copy the extended
// first/last row (width + 2*border_horz bytes).
TEST_CASE("extendFrameLowbd fills sides and copies the extended end rows") {
    std::uint8_t plane[8 * 16];
    for (int k = 0; k < 8 * 16; ++k) plane[k] = 0xEE;
    std::uint8_t* data = plane + 2 * 16 + 2;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j) data[i * 16 + j] = static_cast<std::uint8_t>(16 * (i + 1) + j);
    l10_filters::restoration::extendFrameLowbd(data, 8, 4, 16, 2, 2);
    // Side borders: each row's left two and right two bytes.
    CHECK(plane[2 * 16 + 0] == 16);
    CHECK(plane[2 * 16 + 1] == 16);
    CHECK(plane[2 * 16 + 10] == 23);
    CHECK(plane[2 * 16 + 11] == 23);
    CHECK(plane[5 * 16 + 0] == 64);
    CHECK(plane[5 * 16 + 11] == 71);
    // Top border rows: two full copies of extended row 0 (the copy spans
    // width + 2*border_horz bytes starting at data - border_horz).
    for (int j = 0; j < 12; ++j) CHECK(plane[0 * 16 + j] == plane[2 * 16 + j]);
    for (int j = 0; j < 12; ++j) CHECK(plane[1 * 16 + j] == plane[2 * 16 + j]);
    // Bottom border rows: two full copies of extended row 3.
    for (int j = 0; j < 12; ++j) CHECK(plane[6 * 16 + j] == plane[5 * 16 + j]);
    for (int j = 0; j < 12; ++j) CHECK(plane[7 * 16 + j] == plane[5 * 16 + j]);
    // The untouched far padding stays sentinel (the border copies span
    // cols 0..11 of their rows).
    CHECK(plane[0 * 16 + 15] == 0xEE);
    CHECK(plane[7 * 16 + 15] == 0xEE);
}

// S10: the svt_extend_frame entry (:150-:157), 8-bit dispatch. The highbd
// branch is named-parked (assert), unreachable in our emission
// (enable_restoration 0, 8-bit mono).
TEST_CASE("extendFrame dispatches highbd=0 to the lowbd body") {
    std::uint8_t a[8 * 16], b[8 * 16];
    for (int k = 0; k < 8 * 16; ++k) a[k] = b[k] = 0xEE;
    std::uint8_t* da = a + 2 * 16 + 2;
    std::uint8_t* db = b + 2 * 16 + 2;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j) {
            da[i * 16 + j] = static_cast<std::uint8_t>(16 * (i + 1) + j);
            db[i * 16 + j] = static_cast<std::uint8_t>(16 * (i + 1) + j);
        }
    l10_filters::restoration::extendFrameLowbd(db, 8, 4, 16, 2, 2);
    l10_filters::restoration::extendFrame(da, 8, 4, 16, 2, 2, 0);
    for (int k = 0; k < 8 * 16; ++k) CHECK(a[k] == b[k]);
}

// S11: copy_tile_lowbd (:159-:164) + the copy_tile dispatcher (:173-:180).
// 5x3 tile, src stride 12, dst stride 10; row padding must stay untouched.
TEST_CASE("copyTileLowbd copies width bytes per row, strides independent") {
    std::uint8_t src[3 * 12], dst[3 * 10];
    for (int k = 0; k < 3 * 12; ++k) src[k] = static_cast<std::uint8_t>(0x40 + k);
    for (int k = 0; k < 3 * 10; ++k) dst[k] = 0xEE;
    l10_filters::restoration::copyTileLowbd(5, 3, src, 12, dst, 10);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 5; ++j) CHECK(dst[i * 10 + j] == src[i * 12 + j]);
        CHECK(dst[i * 10 + 5] == 0xEE); // row padding untouched
        CHECK(dst[i * 10 + 9] == 0xEE);
    }
}

TEST_CASE("copyTile dispatches highbd=0 to the lowbd body") {
    std::uint8_t src[3 * 12], a[3 * 10], b[3 * 10];
    for (int k = 0; k < 3 * 12; ++k) src[k] = static_cast<std::uint8_t>(0x40 + k);
    for (int k = 0; k < 3 * 10; ++k) a[k] = b[k] = 0xEE;
    l10_filters::restoration::copyTileLowbd(5, 3, src, 12, a, 10);
    l10_filters::restoration::copyTile(5, 3, src, 12, b, 10, 0);
    for (int k = 0; k < 3 * 10; ++k) CHECK(a[k] == b[k]);
}

// LR2-S1: the SGR constant set the selfguided family consumes
// (restoration.h:41-:42, :94, :96, :98-:99, :110, :112-:113, :36, :52, :61,
// :75-:78, :80-:84). Transcribe-verify: the aggregate sizes are computed
// from the same sub-terms the reference macros use.
TEST_CASE("sgrproj constant set matches the reference macros") {
    using namespace l10_filters::restoration;
    CHECK(sgrprojBorderVert == 3);
    CHECK(sgrprojBorderHorz == 3);
    CHECK(sgrprojPrjBits == 7);
    CHECK(sgrprojRstBits == 4);
    CHECK(sgrprojSgrBits == 8);
    CHECK(sgrprojSgr == 256);
    CHECK(maxRadius == 2);
    CHECK(sgrprojMtableBits == 20);
    CHECK(sgrprojRecipBits == 12);
    CHECK(restorationProcUnitPels == (64 + 3 * 2 + 20) * (64 + 3 * 2 + 20));
    CHECK(restorationUnitpelsMax == (256 * 3 / 2 + 2 * 3 + 16) * (256 * 3 / 2 + 2 * 3 + 8));
}

// S1: table-identity fixture over ALL 16 rows (transcribe-verify against
// restoration.c:33-:48; the mutation-proof class for data tables).
TEST_CASE("sgrParams table identity over all 16 rows") {
    static const int kExpected[16][2][2] = {
        {{2, 1}, {140, 3236}},  {{2, 1}, {112, 2158}},  {{2, 1}, {93, 1618}},
        {{2, 1}, {80, 1438}},   {{2, 1}, {70, 1295}},   {{2, 1}, {58, 1177}},
        {{2, 1}, {47, 1079}},   {{2, 1}, {37, 996}},    {{2, 1}, {30, 925}},
        {{2, 1}, {25, 863}},    {{0, 1}, {-1, 2589}},   {{0, 1}, {-1, 1618}},
        {{0, 1}, {-1, 1177}},   {{0, 1}, {-1, 925}},    {{2, 0}, {56, -1}},
        {{2, 0}, {22, -1}},
    };
    for (int i = 0; i < 16; ++i) {
        CHECK(l10_filters::restoration::sgrParams[i].r[0] == kExpected[i][0][0]);
        CHECK(l10_filters::restoration::sgrParams[i].r[1] == kExpected[i][0][1]);
        CHECK(l10_filters::restoration::sgrParams[i].s[0] == kExpected[i][1][0]);
        CHECK(l10_filters::restoration::sgrParams[i].s[1] == kExpected[i][1][1]);
    }
}

// LR2 oracle for the selfguided family, derived by tracing
// restoration.c:632-:884. Independent structure: the LR1 clamped-box
// oracle (boxOracle above) replaces the prefix-walk boxsum, then the
// estimation and filter stages are re-derived per cell. iStep is the
// estimation row stride (2 = the fast variant, 1 = the slow variant); the
// filter kernel switches with it (restoration.c:737-:762 vs :870-:882).
// The bit_depth shifts are kept symbolic (exact at 8 where 2*(bd-8) and
// bd-8 fold to 0, matching the reference expressions).
namespace {

std::int32_t oracleRp2I32(std::int32_t v, std::int32_t n) { return (v + ((1 << n) >> 1)) >> n; }

void selfguidedOracle(const std::int32_t* dgd, int w, int h, int dgdStride, int iStep, int idx, int radiusIdx,
                      int bitDepth, std::int32_t* dst, int dstStride) {
    const l10_filters::restoration::SgrParamsType& pr = l10_filters::restoration::sgrParams[idx];
    const int r = pr.r[radiusIdx];
    const std::uint32_t s = (std::uint32_t)pr.s[radiusIdx];
    const int widthExt = w + 6, heightExt = h + 6;
    const int bs = ((widthExt + 3) & ~3) + 16;
    std::vector<std::int32_t> Abuf(bs * (heightExt + 2), 0), Bbuf(bs * (heightExt + 2), 0);
    const std::int32_t* dgdExt = dgd - dgdStride * 3 - 3;
    boxOracle(dgdExt, widthExt, heightExt, dgdStride, r, 1, Abuf.data(), bs);
    boxOracle(dgdExt, widthExt, heightExt, dgdStride, r, 0, Bbuf.data(), bs);
    std::int32_t* A = Abuf.data() + 3 * bs + 3;
    std::int32_t* B = Bbuf.data() + 3 * bs + 3;
    const std::uint32_t n = (std::uint32_t)((2 * r + 1) * (2 * r + 1));
    for (int i = -1; i < h + 1; i += iStep) {
        for (int j = -1; j < w + 1; ++j) {
            const int k = i * bs + j;
            std::uint32_t a = (std::uint32_t)oracleRp2I32(A[k], 2 * (bitDepth - 8));
            std::uint32_t b = (std::uint32_t)oracleRp2I32(B[k], bitDepth - 8);
            std::uint32_t p = (a * n < b * b) ? 0u : a * n - b * b;
            const std::uint32_t z = (p * s + ((1u << 20) >> 1)) >> 20;
            A[k] = l10_filters::restoration::xByXplus1[z < 255u ? z : 255u];
            B[k] = (std::int32_t)(((std::uint32_t)(256 - A[k]) * (std::uint32_t)B[k] *
                                      (std::uint32_t)l10_filters::restoration::oneByX[n - 1] +
                                  ((1u << 12) >> 1)) >>
                12);
        }
    }
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            const int k = i * bs + j;
            int nb, a, b;
            if (iStep == 2) {
                if (!(i & 1)) {
                    nb = 5;
                    a = (A[k - bs] + A[k + bs]) * 6 +
                        (A[k - 1 - bs] + A[k - 1 + bs] + A[k + 1 - bs] + A[k + 1 + bs]) * 5;
                    b = (B[k - bs] + B[k + bs]) * 6 +
                        (B[k - 1 - bs] + B[k - 1 + bs] + B[k + 1 - bs] + B[k + 1 + bs]) * 5;
                } else {
                    nb = 4;
                    a = A[k] * 6 + (A[k - 1] + A[k + 1]) * 5;
                    b = B[k] * 6 + (B[k - 1] + B[k + 1]) * 5;
                }
            } else {
                nb = 5;
                a = (A[k] + A[k - 1] + A[k + 1] + A[k - bs] + A[k + bs]) * 4 +
                    (A[k - 1 - bs] + A[k - 1 + bs] + A[k + 1 - bs] + A[k + 1 + bs]) * 3;
                b = (B[k] + B[k - 1] + B[k + 1] + B[k - bs] + B[k + bs]) * 4 +
                    (B[k - 1 - bs] + B[k - 1 + bs] + B[k + 1 - bs] + B[k + 1 + bs]) * 3;
            }
            const std::int32_t v = a * dgd[i * dgdStride + j] + b;
            const int shift = 8 + nb - 4;
            dst[i * dstStride + j] = (v + ((1 << shift) >> 1)) >> shift;
        }
    }
}

// LCG-filled uint8 plane of (h + 8) x (w + 8), content at offset (4, 4);
// the caller stages the 3-border window into int32 (the entry's shape).
void fillLcgPlane(std::vector<std::uint8_t>& plane, int h, int stride, std::uint32_t seed) {
    plane.assign((h + 8) * stride, 0);
    for (auto& px : plane) {
        seed = seed * 1103515245u + 12345u;
        px = (std::uint8_t)((seed >> 16) & 0xFF);
    }
}

void stageInt32(const std::uint8_t* content, int stride, int w, int h, std::vector<std::int32_t>& out) {
    out.assign((h + 6) * (w + 6), 0);
    for (int i = -3; i < h + 3; ++i)
        for (int j = -3; j < w + 3; ++j) out[(i + 3) * (w + 6) + (j + 3)] = content[i * stride + j];
}

// Mid-variance fixture generator: base 128 + (lcg % amp) - (amp / 2),
// clamped to [0, 255]. MEASURED FINDING: full-range LCG saturates the SGR
// estimator (z >= 255 everywhere -> A[k] = 256, B[k] = 0), which collapses
// BOTH kernels to dst = 16*dgd, so the fast/slow pair is indistinguishable
// there; amp 45 (Var ~ 500) gives fast z ~ 42 / slow z ~ 128 at idx 0 -
// both kernels live.
void fillMidAmpPlane(std::vector<std::uint8_t>& plane, int h, int stride, int amp, std::uint32_t seed) {
    plane.assign((h + 8) * stride, 0);
    std::uint32_t s = seed;
    for (auto& px : plane) {
        s = s * 1103515245u + 12345u;
        int v = 128 + (int)(s % (std::uint32_t)amp) - amp / 2;
        px = (std::uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
}

} // namespace

// LR2-S2: selfguided_restoration_fast_internal (:632-:764) vs the
// independent oracle, LCG 9x6, idx 0 radius 0 (r = 2, s = 140). The
// fixture also pins the -1 estimate-loop border (e): the staged buffer's
// border row/col feed the filter, so a border skip mutation is observable.
TEST_CASE("selfguidedRestorationFastInternal matches the oracle on LCG 9x6 (idx 0)") {
    using l10_filters::restoration::selfguidedRestorationFastInternal;
    const int w = 9, h = 6, stride = w + 8;
    std::vector<std::uint8_t> plane;
    fillLcgPlane(plane, h, stride, 12345u);
    const std::uint8_t* content = plane.data() + 4 * stride + 4;
    std::vector<std::int32_t> dgd32;
    stageInt32(content, stride, w, h, dgd32);
    const std::int32_t* dgdC = dgd32.data() + 3 * (w + 6) + 3;
    std::vector<std::int32_t> dst(w * h, -777), exp(w * h, -777);
    selfguidedOracle(dgdC, w, h, w + 6, 2, 0, 0, 8, exp.data(), w);
    selfguidedRestorationFastInternal(
        const_cast<std::int32_t*>(dgdC), w, h, w + 6, dst.data(), w, 8, 0, 0);
    for (int i = 0; i < h; ++i)
        for (int j = 0; j < w; ++j) CHECK(dst[i * w + j] == exp[i * w + j]);
}

// LR2-S3a: selfguided_restoration_internal (:766-:884) vs the oracle,
// LCG 9x6, idx 0 radius 1 (r = 1, s = 3236 - the realistic radius for
// radius_idx 1; every sgr row with r[1] > 0 carries r[1] = 1).
TEST_CASE("selfguidedRestorationInternal matches the oracle on LCG 9x6 (idx 0, r=1)") {
    using l10_filters::restoration::selfguidedRestorationInternal;
    const int w = 9, h = 6, stride = w + 8;
    std::vector<std::uint8_t> plane;
    fillLcgPlane(plane, h, stride, 777u);
    const std::uint8_t* content = plane.data() + 4 * stride + 4;
    std::vector<std::int32_t> dgd32;
    stageInt32(content, stride, w, h, dgd32);
    const std::int32_t* dgdC = dgd32.data() + 3 * (w + 6) + 3;
    std::vector<std::int32_t> dst(w * h, -777), exp(w * h, -777);
    selfguidedOracle(dgdC, w, h, w + 6, 1, 0, 1, 8, exp.data(), w);
    selfguidedRestorationInternal(const_cast<std::int32_t*>(dgdC), w, h, w + 6, dst.data(), w, 8, 0, 1);
    for (int i = 0; i < h; ++i)
        for (int j = 0; j < w; ++j) CHECK(dst[i * w + j] == exp[i * w + j]);
}

// LR2-S3b (discriminating-power pins, verified by the S7 mutation runs):
// (1) the slow path is generic over r - called directly with radius_idx 0
// (r = 2, s = 140), the whole r range the sgr table admits for the slow
// call ({1, 2}); (2) the fast/slow pair split: on the same staged input
// the two kernels produce DIFFERENT output (if a port collapsed one into
// the other, this fails).
TEST_CASE("selfguidedRestorationInternal matches the oracle at r=2 (radius_idx 0)") {
    using l10_filters::restoration::selfguidedRestorationInternal;
    const int w = 9, h = 6, stride = w + 8;
    std::vector<std::uint8_t> plane;
    fillLcgPlane(plane, h, stride, 4242u);
    const std::uint8_t* content = plane.data() + 4 * stride + 4;
    std::vector<std::int32_t> dgd32;
    stageInt32(content, stride, w, h, dgd32);
    const std::int32_t* dgdC = dgd32.data() + 3 * (w + 6) + 3;
    std::vector<std::int32_t> dst(w * h, -777), exp(w * h, -777);
    selfguidedOracle(dgdC, w, h, w + 6, 1, 0, 0, 8, exp.data(), w);
    selfguidedRestorationInternal(const_cast<std::int32_t*>(dgdC), w, h, w + 6, dst.data(), w, 8, 0, 0);
    for (int i = 0; i < h; ++i)
        for (int j = 0; j < w; ++j) CHECK(dst[i * w + j] == exp[i * w + j]);
}

TEST_CASE("fast and slow kernels differ on the same staged input (mid-variance)") {
    using l10_filters::restoration::selfguidedRestorationFastInternal;
    using l10_filters::restoration::selfguidedRestorationInternal;
    const int w = 9, h = 6, stride = w + 8;
    std::vector<std::uint8_t> plane;
    fillMidAmpPlane(plane, h, stride, 45, 999u);
    const std::uint8_t* content = plane.data() + 4 * stride + 4;
    std::vector<std::int32_t> dgd32;
    stageInt32(content, stride, w, h, dgd32);
    const std::int32_t* dgdC = dgd32.data() + 3 * (w + 6) + 3;
    std::vector<std::int32_t> dstF(w * h, -777), dstS(w * h, -777);
    selfguidedRestorationFastInternal(const_cast<std::int32_t*>(dgdC), w, h, w + 6, dstF.data(), w, 8, 0, 0);
    selfguidedRestorationInternal(const_cast<std::int32_t*>(dgdC), w, h, w + 6, dstS.data(), w, 8, 0, 1);
    int diffs = 0;
    for (int i = 0; i < h; ++i)
        for (int j = 0; j < w; ++j)
            if (dstF[i * w + j] != dstS[i * w + j]) ++diffs;
    CHECK(diffs > 0);
}

// LR2-S3c: the mid-variance regime for the fast path vs the oracle (the
// S2 fixture pins the saturated z >= 255 regime; this one pins the live
// estimator: 0 < z < 255, A[k] strictly inside (1, 256), B[k] != 0).
TEST_CASE("selfguidedRestorationFastInternal matches the oracle on mid-variance 9x6 (idx 0)") {
    using l10_filters::restoration::selfguidedRestorationFastInternal;
    const int w = 9, h = 6, stride = w + 8;
    std::vector<std::uint8_t> plane;
    fillMidAmpPlane(plane, h, stride, 45, 31337u);
    const std::uint8_t* content = plane.data() + 4 * stride + 4;
    std::vector<std::int32_t> dgd32;
    stageInt32(content, stride, w, h, dgd32);
    const std::int32_t* dgdC = dgd32.data() + 3 * (w + 6) + 3;
    std::vector<std::int32_t> dst(w * h, -777), exp(w * h, -777);
    selfguidedOracle(dgdC, w, h, w + 6, 2, 0, 0, 8, exp.data(), w);
    selfguidedRestorationFastInternal(const_cast<std::int32_t*>(dgdC), w, h, w + 6, dst.data(), w, 8, 0, 0);
    for (int i = 0; i < h; ++i)
        for (int j = 0; j < w; ++j) CHECK(dst[i * w + j] == exp[i * w + j]);
}

// LR2-S3d (fixture (c)): the flat-input z == 0 saturation. Hand-derived:
// constant 100 gives p = 0, z = 0, A[k] = xByXplus1[0] = 1 (the [1, 256]
// saturation floor), B[k] = ((256-1)*n*c*oneByX[n-1] + 2048) >> 12 ->
// fast (n=25): 25525, dst = (32*100 + 32*25525 + 256) >> 9 = 1602 on every
// cell; slow (n=9, oneByX[8] = 455): 25494, dst = 1600 on every cell.
// An AOMMIN(z, 255) -> 255 mutation (A[k] = 256, B[k] = 0, dst = 16*c =
// 1600) fails both halves.
TEST_CASE("flat input: z=0 saturation pins A[k]=1, fast dst=1602, slow dst=1600") {
    using l10_filters::restoration::selfguidedRestorationFastInternal;
    using l10_filters::restoration::selfguidedRestorationInternal;
    const int w = 9, h = 6, stride = w + 8;
    std::vector<std::uint8_t> plane((h + 8) * stride, 100);
    const std::uint8_t* content = plane.data() + 4 * stride + 4;
    std::vector<std::int32_t> dgd32;
    stageInt32(content, stride, w, h, dgd32);
    const std::int32_t* dgdC = dgd32.data() + 3 * (w + 6) + 3;
    std::vector<std::int32_t> dstF(w * h, -777), dstS(w * h, -777);
    selfguidedRestorationFastInternal(const_cast<std::int32_t*>(dgdC), w, h, w + 6, dstF.data(), w, 8, 0, 0);
    selfguidedRestorationInternal(const_cast<std::int32_t*>(dgdC), w, h, w + 6, dstS.data(), w, 8, 0, 1);
        for (int i = 0; i < h; ++i)
            for (int j = 0; j < w; ++j) {
                CHECK(dstF[i * w + j] == 1602);
                CHECK(dstS[i * w + j] == 1600);
            }
}

// LR2-S4: the entry svt_av1_selfguided_restoration_c (:886-:922). idx 0:
// both radii live, flt0 = fast oracle / flt1 = slow oracle on the same
// dat8 (the entry's own staging must agree with the manual stageInt32 the
// oracle consumes).
TEST_CASE("selfguidedRestoration entry matches both oracles at idx 0") {
    using l10_filters::restoration::selfguidedRestoration;
    const int w = 9, h = 6, stride = w + 8;
    std::vector<std::uint8_t> plane;
    fillMidAmpPlane(plane, h, stride, 45, 5150u);
    const std::uint8_t* content = plane.data() + 4 * stride + 4;
    std::vector<std::int32_t> dgd32;
    stageInt32(content, stride, w, h, dgd32);
    const std::int32_t* dgdC = dgd32.data() + 3 * (w + 6) + 3;
    std::vector<std::int32_t> expF(w * h, -777), expS(w * h, -777);
    selfguidedOracle(dgdC, w, h, w + 6, 2, 0, 0, 8, expF.data(), w);
    selfguidedOracle(dgdC, w, h, w + 6, 1, 0, 1, 8, expS.data(), w);
    std::vector<std::int32_t> flt0(w * h, -777), flt1(w * h, -777);
    selfguidedRestoration(content, w, h, stride, flt0.data(), flt1.data(), w, 0, 8, 0);
    for (int i = 0; i < h; ++i)
        for (int j = 0; j < w; ++j) {
            CHECK(flt0[i * w + j] == expF[i * w + j]);
            CHECK(flt1[i * w + j] == expS[i * w + j]);
        }
}

// LR2-S4 (fixture (b)): r[idx] == 0 skips that filter and leaves flt[idx]
// UNTOUCHED - sgr row 10 has r[0] == 0 (flt0 sentinel stays), row 14 has
// r[1] == 0 (flt1 sentinel stays).
TEST_CASE("selfguidedRestoration r==0 rows leave the paired flt untouched") {
    using l10_filters::restoration::selfguidedRestoration;
    const int w = 9, h = 6, stride = w + 8;
    const std::int32_t sentinel = -424242;
    std::vector<std::uint8_t> plane;
    fillMidAmpPlane(plane, h, stride, 45, 6160u);
    const std::uint8_t* content = plane.data() + 4 * stride + 4;
    std::vector<std::int32_t> dgd32;
    stageInt32(content, stride, w, h, dgd32);
    const std::int32_t* dgdC = dgd32.data() + 3 * (w + 6) + 3;
    std::vector<std::int32_t> expS(w * h, -777), expF(w * h, -777);
    selfguidedOracle(dgdC, w, h, w + 6, 1, 10, 1, 8, expS.data(), w);
    selfguidedOracle(dgdC, w, h, w + 6, 2, 14, 0, 8, expF.data(), w);
    std::vector<std::int32_t> f0(w * h, sentinel), f1(w * h, sentinel);
    selfguidedRestoration(content, w, h, stride, f0.data(), f1.data(), w, 10, 8, 0);
    for (int k = 0; k < w * h; ++k) {
        CHECK(f0[k] == sentinel);
        CHECK(f1[k] == expS[k]);
    }
    std::fill(f0.begin(), f0.end(), sentinel);
    std::fill(f1.begin(), f1.end(), sentinel);
    selfguidedRestoration(content, w, h, stride, f0.data(), f1.data(), w, 14, 8, 0);
    for (int k = 0; k < w * h; ++k) {
        CHECK(f0[k] == expF[k]);
        CHECK(f1[k] == sentinel);
    }
}

// LR2 apply oracle (restoration.c:924-:962 re-derived): stage, run the
// selfguided oracle into flt0/flt1 ONLY where r > 0 (the skipped flt stays
// sentinel and is never read), the decodeXq composition (the LR1-verified
// port primitive), then the u/v accumulation, the ROUND_POWER_OF_TWO at
// SGRPROJ_PRJ_BITS + SGRPROJ_RST_BITS = 11 and the 8-bit clip. wOut
// optionally dumps the pre-clip w for fixture calibration.
void applyOracle(const std::uint8_t* dat8, int w, int h, int stride, int eps, const std::int32_t* xqd,
    std::uint8_t* dst8, int dstStride, std::int32_t* wOut) {
    using namespace l10_filters::restoration;
    const SgrParamsType& pr = sgrParams[eps];
    std::vector<std::int32_t> dgd32;
    stageInt32(dat8, stride, w, h, dgd32);
    const std::int32_t* dgdC = dgd32.data() + 3 * (w + 6) + 3;
    const std::int32_t sentinel = -424242;
    std::vector<std::int32_t> flt0(w * h, sentinel), flt1(w * h, sentinel);
    if (pr.r[0] > 0) selfguidedOracle(dgdC, w, h, w + 6, 2, eps, 0, 8, flt0.data(), w);
    if (pr.r[1] > 0) selfguidedOracle(dgdC, w, h, w + 6, 1, eps, 1, 8, flt1.data(), w);
    std::int32_t xq[2];
    decodeXq(xqd, xq, &pr);
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            const int k = i * w + j;
            const std::int32_t u = (std::int32_t)dat8[i * stride + j] << 4;
            std::int32_t v = u << 7;
            if (pr.r[0] > 0) v += xq[0] * (flt0[k] - u);
            if (pr.r[1] > 0) v += xq[1] * (flt1[k] - u);
            const std::int32_t wv = (v + (1 << 10)) >> 11;
            if (wOut) wOut[k] = wv;
            dst8[i * dstStride + j] =
                (std::uint8_t)(wv < 0 ? 0 : (wv > 255 ? 255 : wv));
        }
    }
}

// LR2-S5: svt_apply_selfguided_restoration_c (:924-:962) vs the
// composition oracle, mid-variance 9x6, eps 0, xqd {5, 3}. The dst plane
// is sentinel-prefilled and the border ring must stay untouched (dst
// stride independence).
TEST_CASE("applySelfguidedRestoration matches the composition oracle (eps 0, xqd 5,3)") {
    using l10_filters::restoration::applySelfguidedRestoration;
    const int w = 9, h = 6, stride = w + 8;
    std::vector<std::uint8_t> plane;
    fillMidAmpPlane(plane, h, stride, 45, 7170u);
    const std::uint8_t* content = plane.data() + 4 * stride + 4;
    const std::int32_t xqd[2] = {5, 3};
    std::vector<std::uint8_t> expD((h + 8) * stride, 0xEE), dst((h + 8) * stride, 0xEE);
    applyOracle(content, w, h, stride, 0, xqd, expD.data() + 4 * stride + 4, stride, nullptr);
    std::vector<std::int32_t> tmpbuf(2 * l10_filters::restoration::restorationUnitpelsMax, -424242);
    applySelfguidedRestoration(content, w, h, stride, 0, xqd, dst.data() + 4 * stride + 4, stride, tmpbuf.data(),
        8, 0);
    for (int i = 0; i < h; ++i)
        for (int j = 0; j < w; ++j) CHECK(dst[i * stride + j] == expD[i * stride + j]);
    CHECK(dst[0] == 0xEE);
    CHECK(dst[(h + 8) * stride - 1] == 0xEE);
    CHECK(dst[4 * stride + 3] == 0xEE);
    CHECK(dst[(h + 3) * stride + 4 + w] == 0xEE);
}

// LR2-S5b: the clip bounds. A single bright outlier (255 at (2,3)) in a 0
// field: the SGR estimate pulls flt far below u there, so the xqd extremes
// drive the pre-clip w past both bounds - xqd {-96, 95} (xq0 = -96,
// xq1 = 33) pushes w > 255 at the outlier; xqd {-96, -32} (xq1 = 256)
// pushes w < 0. The calibration CHECKs pin that the fixture really fires
// both bounds.
TEST_CASE("applySelfguidedRestoration clips w > 255 at the outlier (xqd -96,95)") {
    using l10_filters::restoration::applySelfguidedRestoration;
    const int w = 9, h = 6, stride = w + 8;
    std::vector<std::uint8_t> plane((h + 8) * stride, 0);
    plane[(4 + 2) * stride + (4 + 3)] = 255;
    const std::uint8_t* content = plane.data() + 4 * stride + 4;
    const std::int32_t xqd[2] = {-96, 95};
    std::vector<std::int32_t> wPre(w * h, 0);
    std::vector<std::uint8_t> expD((h + 8) * stride, 0xEE), dst((h + 8) * stride, 0xEE);
    applyOracle(content, w, h, stride, 0, xqd, expD.data() + 4 * stride + 4, stride, wPre.data());
    int maxW = wPre[0];
    for (int k = 1; k < w * h; ++k) maxW = (wPre[k] > maxW ? wPre[k] : maxW);
    CHECK(maxW > 255);
    std::vector<std::int32_t> tmpbuf(2 * l10_filters::restoration::restorationUnitpelsMax, -424242);
    applySelfguidedRestoration(content, w, h, stride, 0, xqd, dst.data() + 4 * stride + 4, stride, tmpbuf.data(),
        8, 0);
    for (int i = 0; i < h; ++i)
        for (int j = 0; j < w; ++j) CHECK(dst[i * stride + j] == expD[i * stride + j]);
}

TEST_CASE("applySelfguidedRestoration clips w < 0 at the dark outlier (xqd -96,-32)") {
    using l10_filters::restoration::applySelfguidedRestoration;
    const int w = 9, h = 6, stride = w + 8;
    // A single dark outlier (0 at (2,3)) in a 255 field: the SGR estimate
    // pulls flt far ABOVE u there (flt0 - u ~ +30, flt1 = 0 with u = 0),
    // so xq0 = -96 drives the pre-clip w negative.
    std::vector<std::uint8_t> plane((h + 8) * stride, 255);
    plane[(4 + 2) * stride + (4 + 3)] = 0;
    const std::uint8_t* content = plane.data() + 4 * stride + 4;
    const std::int32_t xqd[2] = {-96, -32};
    std::vector<std::int32_t> wPre(w * h, 0);
    std::vector<std::uint8_t> expD((h + 8) * stride, 0xEE), dst((h + 8) * stride, 0xEE);
    applyOracle(content, w, h, stride, 0, xqd, expD.data() + 4 * stride + 4, stride, wPre.data());
    int minW = wPre[0];
    for (int k = 1; k < w * h; ++k) minW = (wPre[k] < minW ? wPre[k] : minW);
    CHECK(minW < 0);
    std::vector<std::int32_t> tmpbuf(2 * l10_filters::restoration::restorationUnitpelsMax, -424242);
    applySelfguidedRestoration(content, w, h, stride, 0, xqd, dst.data() + 4 * stride + 4, stride, tmpbuf.data(),
        8, 0);
    for (int i = 0; i < h; ++i)
        for (int j = 0; j < w; ++j) CHECK(dst[i * stride + j] == expD[i * stride + j]);
}

// LR2-S5c: the r==0 blend skip - eps 10 has r[0] == 0, so flt0 is never
// computed and never read; the sentinel-prefilled tmpbuf proves no read
// (a read would leak -424242 into dst and diverge from the oracle).
TEST_CASE("applySelfguidedRestoration eps 10 never reads the skipped flt0") {
    using l10_filters::restoration::applySelfguidedRestoration;
    const int w = 9, h = 6, stride = w + 8;
    std::vector<std::uint8_t> plane;
    fillMidAmpPlane(plane, h, stride, 45, 8180u);
    const std::uint8_t* content = plane.data() + 4 * stride + 4;
    const std::int32_t xqd[2] = {5, 3};
    std::vector<std::uint8_t> expD((h + 8) * stride, 0xEE), dst((h + 8) * stride, 0xEE);
    applyOracle(content, w, h, stride, 10, xqd, expD.data() + 4 * stride + 4, stride, nullptr);
    std::vector<std::int32_t> tmpbuf(2 * l10_filters::restoration::restorationUnitpelsMax, -424242);
    applySelfguidedRestoration(content, w, h, stride, 10, xqd, dst.data() + 4 * stride + 4, stride, tmpbuf.data(),
        8, 0);
    for (int i = 0; i < h; ++i)
        for (int j = 0; j < w; ++j) CHECK(dst[i * stride + j] == expD[i * stride + j]);
}
