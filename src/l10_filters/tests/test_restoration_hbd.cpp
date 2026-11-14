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

namespace {

// Fill the extended selfguided window (rows -3..h+2, cols -3..w+2 around
// the content pointer) with one constant value, uint8 and widened uint16.
void fillConstWindow(std::uint8_t* l, std::uint16_t* hp, std::int32_t w, std::int32_t h, std::int32_t s,
    std::uint16_t v) {
    for (std::int32_t r = -3; r < h + 3; ++r)
        for (std::int32_t c = -3; c < w + 3; ++c) {
            l[r * s + c] = static_cast<std::uint8_t>(v & 0xFF);
            hp[r * s + c] = v;
        }
}

} // namespace

// svt_av1_selfguided_restoration_c (restoration.c:893-:899): the highbd
// staging branch reads dgd16 = CONVERT_TO_SHORTPTR(dgd8). The unpatched
// tree SKIPS the staging under NDEBUG (the assert compiles out) and the
// boxsums read the PREVIOUS lowbd call's stack residue - the test poisons
// that residue with a DIFFERENT constant content first (V=7 -> flt1 112,
// derived below) so the defect shows as 112 != 704.
//
// Constant-plane transcript at bd 8 (eps 0: r = {2,1}; the SLOW filter
// radius_idx 1 -> r = 1, s[1] = 140):
//   boxsum r=1 interior 3x3: a' = RPOT(9V^2, 0) = 9V^2, b' = RPOT(9V, 0) = 9V
//   p = a'*9 - b'^2 = 81V^2 - 81V^2 = 0 for ANY constant plane
//   -> z = RPOT(0*s, 20) = 0 -> A = xByXplus1[0] = 1
//   B = RPOT((256-1)*b'*oneByX[8]=455, 12)
//   flt1 = RPOT(32*(V+B), 9) (slow 3x3 kernel: a = 32*1, b = 32*B, nb = 5)
//   V=7:  b' = 63,  B = 1782,  flt1 = 112
//   V=44: b' = 396, B = 11216, flt1 = 704
TEST_CASE("selfguidedRestoration hbd staging reads its own window (residue poison)") {
    const std::int32_t w = 8, h = 6, s = 20;
    std::vector<std::uint8_t> low((h + 6) * s + 6, 0x5A);
    std::vector<std::uint16_t> high((h + 6) * s + 6, 0x5A5A);
    std::uint8_t* l = low.data() + 3 * s + 3;
    std::uint16_t* hp = high.data() + 3 * s + 3;
    std::vector<std::int32_t> flt0(w * h, 0x7ABCDEF0);
    std::vector<std::int32_t> flt1(w * h, 0x7ABCDEF0);

    // 1) lowbd V=7: stages the residue (all 7) and computes flt1 = 112.
    fillConstWindow(l, hp, w, h, s, 7);
    l10_filters::restoration::selfguidedRestoration(l, w, h, s, flt0.data(), flt1.data(), w, 0, 8, 0);
    CHECK(flt1[0] == 112);

    // 2) hbd V=44: MUST stage its own window (all 44) -> flt1 = 704. The
    // unpatched tree reads the step-1 residue -> 112 -> RED.
    fillConstWindow(l, hp, w, h, s, 44);
    l10_filters::restoration::selfguidedRestoration(reinterpret_cast<const std::uint8_t*>(hp), w, h, s, flt0.data(),
        flt1.data(), w, 0, 8, 1);
    CHECK(flt1[0] == 704);

    // 3) the widening oracle: the same content through the lowbd path now
    //    gives the same outputs (the hbd staging read is value-exact).
    l10_filters::restoration::selfguidedRestoration(l, w, h, s, flt0.data(), flt1.data(), w, 0, 8, 0);
    for (std::int32_t k = 0; k < w * h; ++k) {
        CHECK(flt1[k] == 704);
    }
}

// svt_apply_selfguided_restoration_c highbd (:941 read, :955-:956 write).
// eps 11 (r = {0,1}, s[1] = 1618), xqd = {0,0} -> xq = {0, 128}.
// Constant-plane transcript at bd 10 (the SLOW filter, radius_idx 1 ->
// r = 1, interior 3x3 boxsums; the estimation pass shifts a'/b' by bd but
// the B NEW product uses the RAW boxsum AND is uint32 arithmetic - the
// reference's own wrap at 2^32 is pinned here):
//   A_raw = 9V^2, B_raw = 9V; a' = RPOT(A_raw, 2(bd-8)), b' = RPOT(B_raw, bd-8)
//   p = a'*9 - b'^2 = 0 for a constant plane (both shifts divide exactly)
//   -> z = 0 -> A = xByXplus1[0] = 1
//   P = (255 * B_raw * 455) mod 2^32   (uint32 product, reference wrap)
//   B = RPOT(P, 12)
//   flt1 = RPOT(32*(V + B), 9)   (slow 3x3 kernel: a = 32*1, b = 32*B, nb = 5)
//   v2 = u<<7 + 128*(flt1-u) = 128*flt1 exactly (xq1 = 2^7, u = V<<4)
//   out = clip(RPOT(128*flt1, 11), bd)
//   V=44:    P = 11486475,        B = 2804?  no: 255*396*455 = 45945900,
//            B = RPOT(45945900,12) = 11216, flt1 = 704,   out = 44
//   V=300:   P = 313267500,       B = 76481, flt1 = 4803,  out = 300
//   V=5000:  P = 5221125000 mod 2^32 = 926157704, B = 226120,
//            flt1 = 14445, out = 903   <- the wrap pin
//   V=10000: P = 10442250000 mod 2^32 = 1852315408, B = 452226,
//            flt1 = 28891, out = clip(1802, 10) = 1023 <- the clip pin
// A truncating staging read (300&0xFF = 44) reproduces 44, not 300; the
// V=44 plane truncated to 232 produces 1023, not 44; V=10000 truncated to
// 16 produces 16, not 1023.
TEST_CASE("applySelfguidedRestoration hbd read/write bd 10 transcript + poison") {
    const std::int32_t w = 4, h = 3, s = 16;
    std::vector<std::uint8_t> low((h + 6) * s + 6, 0x5A);
    std::vector<std::uint16_t> high((h + 6) * s + 6, 0x5A5A);
    std::uint8_t* l = low.data() + 3 * s + 3;
    std::uint16_t* hp = high.data() + 3 * s + 3;
    std::vector<std::int32_t> flt0(w * h, 0);
    std::vector<std::int32_t> flt1(w * h, 0);
    std::vector<std::int32_t> tmpbuf(2 * l10_filters::restoration::restorationUnitpelsMax, 0x7ABCDEF0);
    const std::int32_t xqd[2] = {0, 0};

    // residue poison: lowbd constant V=7
    fillConstWindow(l, hp, w, h, s, 7);
    l10_filters::restoration::selfguidedRestoration(l, w, h, s, flt0.data(), flt1.data(), w, 0, 8, 0);

    const std::uint16_t vs[4] = {44, 300, 5000, 10000};
    const std::int32_t expected[4] = {44, 300, 903, 1023};
    for (int pass = 0; pass < 4; ++pass) {
        fillConstWindow(l, hp, w, h, s, vs[pass]);
        std::vector<std::uint16_t> dst((h - 1) * s + w + 8, 0xBEEF);
        std::uint16_t* dp = dst.data() + 2;
        l10_filters::restoration::applySelfguidedRestoration(reinterpret_cast<const std::uint8_t*>(hp), w, h, s, 11,
            xqd, reinterpret_cast<std::uint8_t*>(dp), s, tmpbuf.data(), 10, 1);
        for (std::int32_t r = 0; r < h; ++r)
            for (std::int32_t c = 0; c < w; ++c)
                CHECK(dp[r * s + c] == expected[pass]);
    }
}
