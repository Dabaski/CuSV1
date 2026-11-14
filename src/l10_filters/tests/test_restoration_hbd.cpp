#include <doctest.h>
#include <cstdint>
#include <vector>

#include <restoration.h>

// A8D: the restoration.c highbd branches the LR1/LR2 assert-parks resolve.
// 1:1 port of third_party/SVT-AV1/Source/Lib/Codec/restoration.c:
//   extend_frame_highbd :128-:148, copy_tile_highbd :166-:171 (S1),
//   the selfguided highbd staging read :893-:899 and the apply highbd
//   read/write :941/:955-:956 (S2).
// The reference reaches these through CONVERT_TO_SHORTPTR on the uint8
// parameters (MC1 sseHighbd precedent: uint16 planes aliased through the
// uint8 pointer). At bit depth 8 the highbd path must agree with the lowbd
// path on widened input - the widening oracle below.

namespace {

// A stride-poisoned uint16 plane: content x height at stride, poison 0xBEEF
// everywhere else.
struct HbdPlane {
    std::vector<std::uint16_t> buf;
    std::int32_t stride = 0;
    std::int32_t x0 = 0; // content column 0 inside buf
    std::int32_t y0 = 0; // content row 0 inside buf
    std::int32_t width = 0;
    std::int32_t height = 0;

    HbdPlane(std::int32_t w, std::int32_t h, std::int32_t str, std::int32_t borders) {
        width = w;
        height = h;
        stride = str;
        x0 = borders;
        y0 = borders;
        buf.assign(str * (h + 2 * borders) + 8, 0xBEEF);
        for (std::int32_t r = 0; r < h; ++r)
            for (std::int32_t c = 0; c < w; ++c)
                at(r, c) = static_cast<std::uint16_t>(0x100 + r * w + c);
    }
    std::uint16_t& at(std::int32_t r, std::int32_t c) {
        return buf[(y0 + r) * stride + (x0 + c)];
    }
    std::uint16_t at(std::int32_t r, std::int32_t c) const {
        return buf[(y0 + r) * stride + (x0 + c)];
    }
};

} // namespace

// extend_frame_highbd (restoration.c:128-:148): the side borders replicate
// data_p[0] / data_p[width-1] per row; the top/bottom borders memcpy the
// extended first/last row (width + 2*border_horz uint16 each).
TEST_CASE("extendFrameHighbd 4x3 borders hand replication") {
    const std::int32_t bh = 2, bv = 2;
    HbdPlane p(4, 3, 16, bv + 1);
    // poison the borders around the content
    for (std::int32_t r = -bv; r < p.height + bv; ++r) {
        if (r >= 0 && r < p.height) {
            for (std::int32_t c = -bh; c < 0; ++c) p.at(r, c) = 0xBEEF;
            for (std::int32_t c = p.width; c < p.width + bh; ++c) p.at(r, c) = 0xBEEF;
        }
    }
    for (std::int32_t r = -bv; r < 0; ++r)
        for (std::int32_t c = -bh; c < p.width + bh; ++c) p.at(r, c) = 0xBEEF;
    for (std::int32_t r = p.height; r < p.height + bv; ++r)
        for (std::int32_t c = -bh; c < p.width + bh; ++c) p.at(r, c) = 0xBEEF;

    l10_filters::restoration::extendFrameHighbd(
        reinterpret_cast<std::uint8_t*>(&p.buf[p.y0 * p.stride + p.x0]), p.width, p.height, p.stride, bh, bv);

    // side borders per row: left = row's first pixel, right = row's last
    for (std::int32_t r = 0; r < p.height; ++r) {
        CHECK(p.at(r, -1) == p.at(r, 0));
        CHECK(p.at(r, -2) == p.at(r, 0));
        CHECK(p.at(r, p.width) == p.at(r, p.width - 1));
        CHECK(p.at(r, p.width + 1) == p.at(r, p.width - 1));
    }
    // top border: two rows copying the EXTENDED first row (incl. its side border)
    for (std::int32_t c = -bh; c < p.width + bh; ++c) {
        CHECK(p.at(-1, c) == p.at(0, c));
        CHECK(p.at(-2, c) == p.at(0, c));
    }
    // bottom border: two rows copying the EXTENDED last row
    for (std::int32_t c = -bh; c < p.width + bh; ++c) {
        CHECK(p.at(p.height, c) == p.at(p.height - 1, c));
        CHECK(p.at(p.height + 1, c) == p.at(p.height - 1, c));
    }
    // the corner cells top-left got the extended-row left border value
    CHECK(p.at(-1, -1) == p.at(0, -1));
    CHECK(p.at(p.height + 1, p.width + 1) == p.at(p.height - 1, p.width + 1));
}

// copy_tile_highbd (restoration.c:166-:171): width ELEMENTS per row (the
// *sizeof(*dst) quirk); different src/dst strides; no padding reads.
TEST_CASE("copyTileHighbd 5x4 stride-quirk") {
    const std::int32_t w = 5, h = 4;
    std::vector<std::uint16_t> src((h - 1) * 9 + w + 24, 0xBEEF);
    std::vector<std::uint16_t> dst((h - 1) * 11 + w + 28, 0xBEEF);
    std::uint16_t* sm = src.data() + 2 * 9 + 3;
    std::uint16_t* d = dst.data() + 2 * 11 + 5;
    for (std::int32_t r = 0; r < h; ++r)
        for (std::int32_t c = 0; c < w; ++c) sm[r * 9 + c] = static_cast<std::uint16_t>(0x700 + r * w + c);
    const std::uint16_t* s = sm;

    l10_filters::restoration::copyTileHighbd(
        w, h, reinterpret_cast<const std::uint8_t*>(s), 9, reinterpret_cast<std::uint8_t*>(d), 11);

    for (std::int32_t r = 0; r < h; ++r) {
        for (std::int32_t c = 0; c < w; ++c) {
            CHECK(d[r * 11 + c] == static_cast<std::uint16_t>(0x700 + r * w + c));
        }
        // one column past the row: still poison (the copy width is exact)
        CHECK(d[r * 11 + w] == 0xBEEF);
    }
}

// The widening oracle: at bit depth 8 the highbd dispatchers agree with the
// lowbd path on widened input. Extend first (border writes), copy second.
TEST_CASE("extendFrame/copyTile highbd dispatch widens to lowbd at bd 8") {
    const std::int32_t w = 6, h = 5, bh = 3, bv = 3, s = 16;
    // (h + 2bv) rows x s cols with the content at row bv, col bh; the calls
    // touch rows [-bv, h+bv) x cols [-bh, w+bh) around the content pointer.
    std::vector<std::uint8_t> low((h + 2 * bv) * s + 2 * bh, 0xAB);
    std::vector<std::uint16_t> high((h + 2 * bv) * s + 2 * bh, 0xBEEF);
    std::uint8_t* l = low.data() + bv * s + bh;
    std::uint16_t* hp = high.data() + bv * s + bh;
    for (std::int32_t r = 0; r < h; ++r)
        for (std::int32_t c = 0; c < w; ++c) {
            const std::uint8_t v = static_cast<std::uint8_t>(11 * r + 7 * c + 1);
            l[r * s + c] = v;
            hp[r * s + c] = v;
        }
    l10_filters::restoration::extendFrame(l, w, h, s, bh, bv, 0);
    l10_filters::restoration::extendFrame(reinterpret_cast<std::uint8_t*>(hp), w, h, s, bh, bv, 1);
    for (std::int32_t r = -bv; r < h + bv; ++r)
        for (std::int32_t c = -bh; c < w + bh; ++c)
            CHECK(hp[r * s + c] == l[r * s + c]);

    // copy_tile through both dispatchers into fresh planes
    std::vector<std::uint8_t> dl((h - 1) * 10 + w + 16, 0xAB);
    std::vector<std::uint16_t> dh((h - 1) * 10 + w + 16, 0xBEEF);
    std::uint8_t* dl0 = dl.data() + 2;
    std::uint16_t* dh0 = dh.data() + 2;
    l10_filters::restoration::copyTile(w, h, l, s, dl0, 10, 0);
    l10_filters::restoration::copyTile(
        w, h, reinterpret_cast<const std::uint8_t*>(hp), s, reinterpret_cast<std::uint8_t*>(dh0), 10, 1);
    for (std::int32_t r = 0; r < h; ++r)
        for (std::int32_t c = 0; c < w; ++c)
            CHECK(dh0[r * 10 + c] == dl0[r * 10 + c]);
}
