#include <doctest.h>
#include <cstdint>
#include <vector>

#include <grain.h>

// A8D: the grainSynthesis.c hbd twin - add_noise_to_block_hbd :607-:709
// (the GR1 8-bit park resolves). The uint16 planes are the reference's own
// signature here (the (uint16_t*) casts happen at the driver call sites,
// grainSynthesis.c:1025-:1030, which stay parked with the driver's hbd
// branch). REFERENCE QUIRK pinned by the tests: the 8-bit twin's apply_cb/
// apply_cr admit chroma_scaling_from_luma alone (grainSynthesis.c:522-:523)
// while the hbd twin requires actual points (:624-:626) - with
// chroma_scaling_from_luma and no points the twins DIVERGE by design.
// At bit depth 8 and all point counts > 0 the hbd twin must equal the
// widened 8-bit twin (offsets, clamps and scale_lut all reduce: the
// << (bd-8) / - (1 << bd) / << (bd-8) terms are identities at bd 8).

namespace {

// makeParams + a fully-parameterized hbd fixture (l10 grain conventions).
l10_filters::grain::AomFilmGrain hbdParams(std::int32_t bitDepth, std::int32_t numY, std::int32_t numCb,
    std::int32_t numCr, std::int32_t csfl, std::int32_t scalingShift) {
    auto p = l10_filters::grain::makeParams();
    p.bit_depth = bitDepth;
    p.num_y_points = numY;
    p.num_cb_points = numCb;
    p.num_cr_points = numCr;
    p.chroma_scaling_from_luma = csfl;
    p.scaling_shift = scalingShift;
    p.cb_mult = 128; // -> cb_mult - 128 = 0
    p.cb_luma_mult = 64; // -> 0
    p.cb_offset = 0;
    p.cr_mult = 128;
    p.cr_luma_mult = 64;
    p.cr_offset = 0;
    p.clip_to_restricted_range = 0;
    return p;
}

} // namespace

// Widening oracle at bd 8: hbd twin == 8-bit twin on identical content
// (all point counts > 0, subsampling (1,1) to exercise the average path).
TEST_CASE("addNoiseToBlockHbd widens to addNoiseToBlock at bd 8") {
    using namespace l10_filters::grain;
    auto p = hbdParams(8, 2, 2, 2, 0, 6);
    p.scaling_points_y[0][0] = 40;
    p.scaling_points_y[0][1] = 120;
    p.scaling_points_y[1][0] = 200;
    p.scaling_points_y[1][1] = 90;
    p.scaling_points_cb[0][0] = 30;
    p.scaling_points_cb[0][1] = 60;
    p.scaling_points_cb[1][0] = 210;
    p.scaling_points_cb[1][1] = 140;
    p.scaling_points_cr[0][0] = 25;
    p.scaling_points_cr[0][1] = 30;
    p.scaling_points_cr[1][0] = 220;
    p.scaling_points_cr[1][1] = 170;

    const std::int32_t halfH = 2, halfW = 3;
    const std::int32_t ls = 16, cs = 10;
    // uint8 planes with poison padding
    std::vector<std::uint8_t> ly(4 * halfH * ls + 9, 0x77), lcb(4 * halfH * cs + 9, 0x77),
        lcr(4 * halfH * cs + 9, 0x77);
    std::vector<std::uint16_t> hy(4 * halfH * ls + 9, 0xBEEF), hcb(4 * halfH * cs + 9, 0xBEEF),
        hcr(4 * halfH * cs + 9, 0xBEEF);
    for (std::int32_t r = 0; r < 2 * halfH + 1; ++r)
        for (std::int32_t c = 0; c < 2 * halfW + 1; ++c) {
            const std::uint8_t v = static_cast<std::uint8_t>((37 * r + 11 * c + 60) & 0xFF);
            ly[r * ls + c] = v;
            hy[r * ls + c] = v;
        }
    for (std::int32_t r = 0; r < halfH + 1; ++r)
        for (std::int32_t c = 0; c < halfW + 1; ++c) {
            const std::uint8_t v = static_cast<std::uint8_t>((53 * r + 7 * c + 90) & 0xFF);
            lcb[r * cs + c] = v;
            lcr[r * cs + c] = static_cast<std::uint8_t>(v ^ 0x3C);
            hcb[r * cs + c] = v;
            hcr[r * cs + c] = static_cast<std::uint16_t>(v ^ 0x3C);
        }
    // grain blocks: arbitrary values within the int32 grain range
    std::vector<std::int32_t> yG((2 * halfH + 4) * (2 * halfW + 6)), cbG((halfH + 3) * (halfW + 5)),
        crG((halfH + 3) * (halfW + 5));
    for (std::size_t i = 0; i < yG.size(); ++i) yG[i] = static_cast<std::int32_t>((i * 73) % 511) - 255;
    for (std::size_t i = 0; i < cbG.size(); ++i) cbG[i] = static_cast<std::int32_t>((i * 131) % 501) - 250;
    for (std::size_t i = 0; i < crG.size(); ++i) crG[i] = static_cast<std::int32_t>((i * 197) % 521) - 260;

    initScalingLuts(&p, p.scaling_points_y, p.scaling_points_cb, p.scaling_points_cr);
    initGrainRange(8);

    addNoiseToBlock(&p, ly.data(), lcb.data(), lcr.data(), ls, cs, yG.data(), cbG.data(), crG.data(),
        static_cast<std::int32_t>(2 * halfW + 6), static_cast<std::int32_t>(halfW + 5), halfH, halfW, 8, 1, 1);
    addNoiseToBlockHbd(&p, hy.data(), hcb.data(), hcr.data(), ls, cs, yG.data(), cbG.data(), crG.data(),
        static_cast<std::int32_t>(2 * halfW + 6), static_cast<std::int32_t>(halfW + 5), halfH, halfW, 8, 1, 1);

    for (std::int32_t r = 0; r < halfH + 1; ++r)
        for (std::int32_t c = 0; c < halfW + 1; ++c) {
            CHECK(hcb[r * cs + c] == lcb[r * cs + c]);
            CHECK(hcr[r * cs + c] == lcr[r * cs + c]);
        }
    for (std::int32_t r = 0; r < 2 * halfH; ++r)
        for (std::int32_t c = 0; c < 2 * halfW; ++c)
            CHECK(hy[r * ls + c] == ly[r * ls + c]);
}

// bd 10 hand transcript (chromaSubsamp (0,0)). scaleLut interpolation
// rounding: the term is (1 << (bd-9)) >> (bd-8) = 0 at bd 10, so a
// CONSTANT LUT reproduces its entry exactly (measured with the
// instrumented probe). hbdParams sets cb_luma_mult = 64 RAW -> derived
// cbLumaMult = -64.
//   cb/cr: avg = 500 -> ((500*-64 + 0) >> 6) - 1024 = -1524 -> clamp 0
//     -> scaleLut(cb, 0, 10) = LUT[0] = 100 (constant single-point LUT)
//     -> value = (100*64 + 32) >> 6 = 100 -> cb = 100 + 100 = 200
//   luma: px 500 -> scaleLut(y, 500, 10): x = 125, LUT[125] = LUT[126] = 200
//     -> scale = 200 -> value = (200*10 + 32) >> 6 = 31 -> y = 531
//   luma grain 0: value = (200*0 + 32) >> 6 = 0 -> y unchanged
TEST_CASE("addNoiseToBlockHbd bd 10 hand transcript") {
    using namespace l10_filters::grain;
    auto p = hbdParams(10, 1, 1, 1, 0, 6);
    p.scaling_points_y[0][0] = 128;
    p.scaling_points_y[0][1] = 200;
    p.scaling_points_cb[0][0] = 128;
    p.scaling_points_cb[0][1] = 100;
    p.scaling_points_cr[0][0] = 128;
    p.scaling_points_cr[0][1] = 100;
    initScalingLuts(&p, p.scaling_points_y, p.scaling_points_cb, p.scaling_points_cr);
    initGrainRange(10);

    const std::int32_t halfH = 1, halfW = 1, ls = 8, cs = 8;
    // processed luma region must hold valid pixels (unguarded scaleLut)
    std::vector<std::uint16_t> y(2 * ls + 4, 0xBEEF), cb(cs + 4, 0xBEEF), cr(cs + 4, 0xBEEF);
    for (std::int32_t i = 0; i < 2 * halfH; ++i)
        for (std::int32_t j = 0; j < 2 * halfW; ++j) y[i * ls + j] = 500;
    cb[0] = 100;
    cr[0] = 100;
    std::int32_t yG[8] = {10, 0, 0, 0, 0, 0, 0, 0};
    std::int32_t cbG[4] = {64, 0, 0, 0};
    std::int32_t crG[4] = {64, 0, 0, 0};
    addNoiseToBlockHbd(&p, y.data(), cb.data(), cr.data(), ls, cs, yG, cbG, crG, 4, 2, halfH, halfW, 10, 0, 0);
    CHECK(cb[0] == 200);
    CHECK(cr[0] == 200);
    CHECK(y[0] == 531);
    CHECK(y[1] == 500);
    // untouched cells stay poison
    CHECK(y[4] == 0xBEEF);
    CHECK(cb[2] == 0xBEEF);
}

// The twins-diverge quirk: chroma_scaling_from_luma with NO cb/cr points
// - the 8-bit twin applies grain to cb/cr (:522-:523), the hbd twin does
// not (:624-:626). Same inputs both sides at bd 8; luma applies on BOTH.
TEST_CASE("addNoiseToBlockHbd apply gate: CSFL without points") {
    using namespace l10_filters::grain;
    auto p = hbdParams(8, 1, 0, 0, 1, 6);
    p.scaling_points_y[0][0] = 100;
    p.scaling_points_y[0][1] = 200;
    initScalingLuts(&p, p.scaling_points_y, p.scaling_points_cb, p.scaling_points_cr);
    initGrainRange(8);

    const std::int32_t halfH = 1, halfW = 1, ls = 8, cs = 8;
    std::vector<std::uint8_t> ly(2 * ls + 4, 0x77), lcb(cs + 4, 0x77), lcr(cs + 4, 0x77);
    std::vector<std::uint16_t> hy(2 * ls + 4, 0xBEEF), hcb(cs + 4, 0xBEEF), hcr(cs + 4, 0xBEEF);
    // processed regions must hold valid pixels (unguarded scaleLut)
    for (std::int32_t i = 0; i < 2 * halfH; ++i)
        for (std::int32_t j = 0; j < 2 * halfW; ++j) {
            ly[i * ls + j] = 150;
            hy[i * ls + j] = 150;
        }
    for (std::int32_t i = 0; i < halfH; ++i)
        for (std::int32_t j = 0; j < halfW; ++j) {
            lcb[i * cs + j] = 90;
            lcr[i * cs + j] = 90;
            hcb[i * cs + j] = 90;
            hcr[i * cs + j] = 90;
        }
    std::int32_t yG[8] = {20, 0, 0, 0, 0, 0, 0, 0};
    std::int32_t cG[4] = {64, 0, 0, 0};
    addNoiseToBlock(&p, ly.data(), lcb.data(), lcr.data(), ls, cs, yG, cG, cG, 4, 2, halfH, halfW, 8, 0, 0);
    addNoiseToBlockHbd(&p, hy.data(), hcb.data(), hcr.data(), ls, cs, yG, cG, cG, 4, 2, halfH, halfW, 8, 0, 0);
    // hbd: cb/cr untouched (the gate needs points), luma applied
    CHECK(hcb[0] == 90);
    CHECK(hcr[0] == 90);
    // lowbd: cb/cr applied through the CSFL clause
    CHECK(lcb[0] != 90);
    CHECK(lcr[0] != 90);
    // both apply luma
    CHECK(hy[0] != 150);
    CHECK(ly[0] != 150);
}

// 4:2:0 subsampled average path at bd 10 with a TWO-point cb LUT so the
// output depends on WHICH average is computed (a constant LUT would hide
// the addressing). Note scaleLut's x index is (idx >> (bd-8)) - the 8-bit
// scale - so the LUT region hit depends on index >> 2 at bd 10.
// cb_mult 128 (-> 0), cb_luma_mult 384 (-> 256), cb_offset 128
// (-> (128 << 2) - 1024 = -512): index = (avg * 256) >> 6 - 512 = 4*avg-512.
//   cb LUT points {128,100},{255,150}: delta_x = 127, delta_y = 50,
//   delta = 50 * ((65536 + 63) / 127) = 50 * 516 = 25800
//   subsampled avg = (100 + 200 + 1) >> 1 = 150 -> index = 88 ->
//   x = 22 -> LUT[22] = 100 (the pre-128 fill) -> scale 100
//   -> value = (100*64 + 32) >> 6 = 100 -> cb = 100 + 100 = 200
//   a DIRECT (non-subsampled) read would use luma[j] = 300 -> index 688 ->
//   x = 172 -> LUT[172] = 100 + (44*25800 + 32768 >> 16) = 117 ->
//   scale 117 -> value 117 -> cb = 217 (the mutant's value).
TEST_CASE("addNoiseToBlockHbd subsampled average at bd 10") {
    using namespace l10_filters::grain;
    auto p = hbdParams(10, 1, 1, 1, 0, 6);
    p.cb_luma_mult = 384;
    p.cb_offset = 128;
    p.cr_luma_mult = 384;
    p.cr_offset = 128;
    p.scaling_points_y[0][0] = 128;
    p.scaling_points_y[0][1] = 200;
    p.scaling_points_cb[0][0] = 128;
    p.scaling_points_cb[0][1] = 100;
    p.scaling_points_cb[1][0] = 255;
    p.scaling_points_cb[1][1] = 150;
    p.scaling_points_cr[0][0] = 128;
    p.scaling_points_cr[0][1] = 100;
    p.scaling_points_cr[1][0] = 255;
    p.scaling_points_cr[1][1] = 150;
    initScalingLuts(&p, p.scaling_points_y, p.scaling_points_cb, p.scaling_points_cr);
    initGrainRange(10);

    const std::int32_t halfH = 1, halfW = 1, ls = 8, cs = 8;
    std::vector<std::uint16_t> y(4 * ls + 4, 0xBEEF), cb(cs + 4, 0xBEEF), cr(cs + 4, 0xBEEF);
    // valid pixels over the processed region (unguarded scaleLut)
    for (std::int32_t i = 0; i < 2 * halfH; ++i)
        for (std::int32_t j = 0; j < 2 * halfW; ++j) y[i * ls + j] = 300;
    y[0] = 100;
    y[1] = 200; // avg = (100 + 200 + 1) >> 1 = 150
    cb[0] = 100;
    cr[0] = 100;
    std::int32_t yG[8] = {10, 0, 0, 0, 0, 0, 0, 0};
    std::int32_t cG[4] = {64, 0, 0, 0};
    addNoiseToBlockHbd(&p, y.data(), cb.data(), cr.data(), ls, cs, yG, cG, cG, 4, 2, halfH, halfW, 10, 1, 1);
    CHECK(cb[0] == 200);
    CHECK(cr[0] == 200);
}
