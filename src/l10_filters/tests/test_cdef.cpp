#include <doctest.h>
#include <cdef.h>

// CDEF1 (A8 campaign): 1:1 port of the CDEF find_dir + filter kernel family
// of third_party/SVT-AV1/Source/Lib/Codec/cdef.c. Integer-only throughout
// (no float anywhere in this family). The 16-bit svt_cdef_filter_block_c is
// the SPEC kernel; the 8-bit twins replicate it minus the sentinel path and
// the derivation relationship is pinned by tests, not by shared code.

TEST_CASE("cdefSign returns 1 for zero") {
    CHECK(l10_filters::cdefSign(0) == 1);
}

TEST_CASE("cdefConstrain returns 0 on a zero threshold") {
    CHECK(l10_filters::cdefConstrain(0, 0, 3) == 0);
}

TEST_CASE("cdefConstrain damping shift is max(0, damping - get_msb(thr))") {
    // thr 16: get_msb = 4, damping 6 -> shift 2; |diff| 32: 16 - 8 = 8 (not 0, which a
    // shift of 1 would give: 16 - 16 = 0).
    CHECK(l10_filters::cdefConstrain(32, 16, 6) == 8);
}

TEST_CASE("cdefAdjustStrength returns 0 on zero variance") {
    CHECK(l10_filters::cdefAdjustStrength(15, 0) == 0);
}

TEST_CASE("cdefAdjustStrength scales by 4+i over 16 at var=64") {
    // var 64: var>>6 = 1, get_msb = 0, i = 0: (15*4+8)>>4 = 4.
    CHECK(l10_filters::cdefAdjustStrength(15, 64) == 4);
}

TEST_CASE("cdefAdjustStrength clamps i at 12") {
    // var 1<<20: var>>6 = 16384, get_msb = 14 -> i clamped to 12: (31*16+8)>>4 = 31.
    CHECK(l10_filters::cdefAdjustStrength(31, 1 << 20) == 31);
}

TEST_CASE("cdefFindDir on a flat block returns direction 0") {
    std::uint16_t img[64];
    for (int i = 0; i < 64; ++i) img[i] = 128;
    std::int32_t var = -1;
    CHECK(l10_filters::cdefFindDir(img, 8, &var, 0) == 0);
}

namespace {
// Mod-3 zero-mean line pattern: F(n) = 64*((n mod 3) - 1) in {-64, 0, 64}.
// Foreign directions mix all three residues along their lines and cancel;
// the aligned direction accumulates cnt*F per bucket.
int cdef1F(int n) {
    int m = n % 3;
    if (m < 0) m += 3;
    return 64 * (m - 1);
}
} // namespace

TEST_CASE("cdefFindDir direction sweep: dir 0") {
    std::uint16_t img[64];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) img[i * 8 + j] = static_cast<std::uint16_t>(128 + cdef1F(i + j));
    std::int32_t var = 0;
    CHECK(l10_filters::cdefFindDir(img, 8, &var, 0) == 0);
}

TEST_CASE("cdefFindDir direction sweep: dir 1") {
    std::uint16_t img[64];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) img[i * 8 + j] = static_cast<std::uint16_t>(128 + cdef1F(i + j / 2));
    std::int32_t var = 0;
    CHECK(l10_filters::cdefFindDir(img, 8, &var, 0) == 1);
}

TEST_CASE("cdefFindDir direction sweep: dir 2") {
    std::uint16_t img[64];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) img[i * 8 + j] = static_cast<std::uint16_t>(128 + cdef1F(i));
    std::int32_t var = 0;
    CHECK(l10_filters::cdefFindDir(img, 8, &var, 0) == 2);
}

TEST_CASE("cdefFindDir direction sweep: dir 3") {
    std::uint16_t img[64];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) img[i * 8 + j] = static_cast<std::uint16_t>(128 + cdef1F(i - j / 2));
    std::int32_t var = 0;
    CHECK(l10_filters::cdefFindDir(img, 8, &var, 0) == 3);
}

TEST_CASE("cdefFindDir direction sweep: dir 4") {
    std::uint16_t img[64];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) img[i * 8 + j] = static_cast<std::uint16_t>(128 + cdef1F(i - j));
    std::int32_t var = 0;
    CHECK(l10_filters::cdefFindDir(img, 8, &var, 0) == 4);
}

TEST_CASE("cdefFindDir direction sweep: dir 5") {
    std::uint16_t img[64];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) img[i * 8 + j] = static_cast<std::uint16_t>(128 + cdef1F(j - i / 2));
    std::int32_t var = 0;
    CHECK(l10_filters::cdefFindDir(img, 8, &var, 0) == 5);
}

TEST_CASE("cdefFindDir direction sweep: dir 6") {
    std::uint16_t img[64];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) img[i * 8 + j] = static_cast<std::uint16_t>(128 + cdef1F(j));
    std::int32_t var = 0;
    CHECK(l10_filters::cdefFindDir(img, 8, &var, 0) == 6);
}

TEST_CASE("cdefFindDir direction sweep: dir 7") {
    std::uint16_t img[64];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) img[i * 8 + j] = static_cast<std::uint16_t>(128 + cdef1F(i / 2 + j));
    std::int32_t var = 0;
    CHECK(l10_filters::cdefFindDir(img, 8, &var, 0) == 7);
}

TEST_CASE("cdefFindDir var = (best - orthogonal) >> 10, hand-derived") {
    // Vertical stripes 192/64 (period 2): x = +-64; partial[6][j] = +-512;
    // cost[6] = div_table[8] * 8 * 512^2 = 105 * 2,097,152 = 220,200,960
    // (div_table[8] = 105 = 840/8, the weight of an 8-pixel bucket);
    // cost[2] = 105 * sum(0^2) = 0; var = 220,200,960 >> 10 = 215,040.
    std::uint16_t img[64];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) img[i * 8 + j] = (j % 2 == 0) ? 192 : 64;
    std::int32_t var = 0;
    l10_filters::cdefFindDir(img, 8, &var, 0);
    CHECK(var == 215040);
}

TEST_CASE("cdefFindDir coeff_shift pin: 10-bit input, shift 2 == widened 8-bit") {
    // coeff_shift = bit_depth - 8; at 8-bit it is 0 (this pins that). A
    // 10-bit block whose >>2 equals an 8-bit block must give the same dir
    // and var as that 8-bit block run at shift 0.
    std::uint16_t img10[64];
    std::uint16_t img8[64];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) {
            img8[i * 8 + j] = static_cast<std::uint16_t>(128 + cdef1F(i - j / 2));
            img10[i * 8 + j] = static_cast<std::uint16_t>(img8[i * 8 + j] << 2);
        }
    std::int32_t var10 = 0, var8 = 0;
    const std::uint8_t d10 = l10_filters::cdefFindDir(img10, 8, &var10, 2);
    const std::uint8_t d8 = l10_filters::cdefFindDir(img8, 8, &var8, 0);
    CHECK(d10 == d8);
}

TEST_CASE("cdefFindDir coeff_shift pin: vars also equal") {
    std::uint16_t img10[64];
    std::uint16_t img8[64];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) {
            img8[i * 8 + j] = static_cast<std::uint16_t>(128 + cdef1F(i - j / 2));
            img10[i * 8 + j] = static_cast<std::uint16_t>(img8[i * 8 + j] << 2);
        }
    std::int32_t var10 = 0, var8 = 0;
    l10_filters::cdefFindDir(img10, 8, &var10, 2);
    l10_filters::cdefFindDir(img8, 8, &var8, 0);
    CHECK(var10 == var8);
}

TEST_CASE("cdefFilterBlock flat passthrough at zero strength") {
    // Stride must be CDEF_BSTRIDE: the kernel hardcodes s = CDEF_BSTRIDE
    // (cdef.c:197) for its tap reads. 12 rows = 8 block rows + the +-2
    // tap margins; in points at the block origin (buffer row 2, col 2).
    static std::uint16_t in[12 * 144];
    for (int i = 0; i < 12 * 144; ++i) in[i] = 128;
    std::uint16_t dst[64];
    for (int i = 0; i < 64; ++i) dst[i] = 0xABCD;
    l10_filters::cdefFilterBlock(nullptr, dst, 8, in + 2 * 144 + 2, 0, 0, 0, 0, 0,
                                l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    CHECK(dst[4 * 8 + 4] == 128);
}

TEST_CASE("cdefFilterBlock pri impulse, taps {4,2}, dir 6 -> 144") {
    static std::uint16_t in[12 * 144];
    for (int i = 0; i < 12 * 144; ++i) in[i] = 128;
    // Center (4,4); the +-1-row vertical neighbours (dir 6, k = 0) get +32.
    in[(2 + 3) * 144 + 6] = 160;
    in[(2 + 5) * 144 + 6] = 160;
    std::uint16_t dst[64];
    l10_filters::cdefFilterBlock(nullptr, dst, 8, in + 2 * 144 + 2, 64, 0, 6, 0, 0,
                                l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    // sum = 4*32 + 4*32 = 256; y = 128 + ((8+256)>>4) = 144; max 160, min 128.
    CHECK(dst[4 * 8 + 4] == 144);
}

TEST_CASE("cdefFilterBlock pri impulse, taps {3,3} via strength bit, dir 6 -> 140") {
    static std::uint16_t in[12 * 144];
    for (int i = 0; i < 12 * 144; ++i) in[i] = 128;
    in[(2 + 3) * 144 + 6] = 160;
    in[(2 + 5) * 144 + 6] = 160;
    std::uint16_t dst[64];
    // pri_strength 65: bit 0 set -> taps {3,3}.
    l10_filters::cdefFilterBlock(nullptr, dst, 8, in + 2 * 144 + 2, 65, 0, 6, 0, 0,
                                l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    // sum = 3*32 + 3*32 = 192; y = 128 + ((8+192)>>4) = 140.
    CHECK(dst[4 * 8 + 4] == 140);
}

TEST_CASE("cdefFilterBlock sec impulse, taps {2,1}, dir 6 -> 130") {
    static std::uint16_t in[12 * 144];
    for (int i = 0; i < 12 * 144; ++i) in[i] = 128;
    // The four +-1 diagonal neighbours (sec k=0: dir+2 and dir-2 offsets),
    // each +3: constrain(3, 15, 1): shift = max(0, 1-3) = 0, bound = 15-3
    // = 12 -> 3.
    in[(2 + 3) * 144 + 7] = 131;
    in[(2 + 5) * 144 + 5] = 131;
    in[(2 + 5) * 144 + 7] = 131;
    in[(2 + 3) * 144 + 5] = 131;
    std::uint16_t dst[64];
    // pri 0 (no primary contribution), sec 15 with damping 1 (shift 0).
    l10_filters::cdefFilterBlock(nullptr, dst, 8, in + 2 * 144 + 2, 0, 15, 6, 0, 1,
                                l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    // sum = 2*(3+3+3+3) = 24; y = 128 + ((8+24)>>4) = 130.
    CHECK(dst[4 * 8 + 4] == 130);
}

TEST_CASE("cdefFilterBlock sentinel: guarded max clamps to 208, not the sentinel") {
    static std::uint16_t in[12 * 144];
    for (int i = 0; i < 12 * 144; ++i) in[i] = 128;
    const int cx = 2 + 4, cy = 2 + 4;
    in[cy * 144 + cx] = 200;
    // All 12 taps = 208 ...
    const int taps[][2] = {{3, 4}, {5, 4}, {2, 4}, {6, 4}, {3, 5}, {5, 3},
                           {5, 5}, {3, 3}, {2, 6}, {6, 2}, {6, 6}, {2, 2}};
    for (int t = 0; t < 12; ++t) in[(2 + taps[t][0]) * 144 + 2 + taps[t][1]] = 208;
    // ... except one +-1-row pri tap, which is the off-frame sentinel.
    in[(2 + 5) * 144 + 6] = l10_filters::kCdefVeryLarge;
    std::uint16_t dst[64];
    l10_filters::cdefFilterBlock(nullptr, dst, 8, in + 2 * 144 + 2, 64, 64, 6, 0, 0,
                                l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    // sum = 192 - 4*8 = 160; y = 200 + (168>>4) = 210 > true max 208 ->
    // clamped to 208. An unguarded max (32639) would let 210 through.
    CHECK(dst[4 * 8 + 4] == 208);
}

TEST_CASE("cdefFilterBlock round half away from zero on a negative sum") {
    static std::uint16_t in[12 * 144];
    for (int i = 0; i < 12 * 144; ++i) in[i] = 128;
    in[(2 + 3) * 144 + 6] = 122;  // diff -6, pri k=0 tap 4 -> sum -24
    std::uint16_t dst[64];
    l10_filters::cdefFilterBlock(nullptr, dst, 8, in + 2 * 144 + 2, 64, 0, 6, 0, 0,
                                l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    // y = 128 + ((8 - 24 - 1) >> 4) = 128 - 2 = 126; dropping the -(sum<0)
    // term would give (8-24)>>4 = -1 -> 127.
    CHECK(dst[4 * 8 + 4] == 126);
}

TEST_CASE("cdefFilterBlock subsampling 2 skips odd dst rows") {
    static std::uint16_t in[12 * 144];
    for (int i = 0; i < 12 * 144; ++i) in[i] = 128;
    std::uint16_t dst[64];
    for (int i = 0; i < 64; ++i) dst[i] = 0xABCD;
    l10_filters::cdefFilterBlock(nullptr, dst, 8, in + 2 * 144 + 2, 0, 0, 0, 0, 0,
                                l10_filters::CdefBlockSize::BLOCK_8X8, 0, 2);
    // Rows are written at input-row indices i = 0,2,4,6: dst row 1 untouched.
    CHECK(dst[1 * 8 + 0] == 0xABCD);
}

TEST_CASE("cdefFilterBlock subsampling 2 writes even dst rows") {
    static std::uint16_t in[12 * 144];
    for (int i = 0; i < 12 * 144; ++i) in[i] = 128;
    std::uint16_t dst[64];
    for (int i = 0; i < 64; ++i) dst[i] = 0xABCD;
    l10_filters::cdefFilterBlock(nullptr, dst, 8, in + 2 * 144 + 2, 0, 0, 0, 0, 0,
                                l10_filters::CdefBlockSize::BLOCK_8X8, 0, 2);
    CHECK(dst[6 * 8 + 7] == 128);
}

TEST_CASE("cdefFilterBlock writes 8-bit dst when dst8 is given") {
    static std::uint16_t in[12 * 144];
    for (int i = 0; i < 12 * 144; ++i) in[i] = 128;
    in[(2 + 3) * 144 + 6] = 160;
    in[(2 + 5) * 144 + 6] = 160;
    std::uint8_t dst[64];
    l10_filters::cdefFilterBlock(dst, nullptr, 8, in + 2 * 144 + 2, 64, 0, 6, 0, 0,
                                l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    CHECK(dst[4 * 8 + 4] == 144);
}

TEST_CASE("cdefFilterBlock8bit flat passthrough at zero strength") {
    static std::uint8_t in[12 * 144];
    for (int i = 0; i < 12 * 144; ++i) in[i] = 128;
    std::uint8_t dst[64];
    for (int i = 0; i < 64; ++i) dst[i] = 0xAA;
    l10_filters::cdefFilterBlock8bit(dst, 8, in + 2 * 144 + 2, 0, 0, 0, 0,
                                    l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    CHECK(dst[4 * 8 + 4] == 128);
}

TEST_CASE("cdefFilterBlock8bit == SPEC kernel on widened sentinel-free input") {
    // The derivation pin: the 16-bit kernel is the spec; the interior 8-bit
    // twin must reproduce it exactly when no sentinel is present.
    static std::uint8_t in8[12 * 144];
    static std::uint16_t in16[12 * 144];
    std::uint32_t seed = 0x2545F491;
    for (int i = 0; i < 12 * 144; ++i) {
        seed = seed * 1103515245u + 12345u;
        in8[i] = static_cast<std::uint8_t>((seed >> 16) & 0xFF);
        in16[i] = in8[i];
    }
    std::uint8_t dst8[64];
    std::uint16_t dst16[64];
    l10_filters::cdefFilterBlock8bit(dst8, 8, in8 + 2 * 144 + 2, 31, 3, 3, 5,
                                    l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    l10_filters::cdefFilterBlock(nullptr, dst16, 8, in16 + 2 * 144 + 2, 31, 3, 3, 5, 5,
                                l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    bool same = true;
    for (int i = 0; i < 64; ++i) same = same && (dst8[i] == static_cast<std::uint8_t>(dst16[i]));
    CHECK(same);
}

TEST_CASE("cdefFindDir8bit == widened find_dir, dir") {
    std::uint8_t img8[64];
    std::uint16_t img16[64];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) {
            img8[i * 8 + j] = static_cast<std::uint8_t>(128 + cdef1F(j - i / 2));
            img16[i * 8 + j] = img8[i * 8 + j];
        }
    std::int32_t v8 = 0, v16 = 0;
    const std::uint8_t d8 = l10_filters::cdefFindDir8bit(img8, 8, &v8, 0);
    const std::uint8_t d16 = l10_filters::cdefFindDir(img16, 8, &v16, 0);
    CHECK(d8 == d16);
}

TEST_CASE("cdefFindDir8bit == widened find_dir, var") {
    std::uint8_t img8[64];
    std::uint16_t img16[64];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) {
            img8[i * 8 + j] = static_cast<std::uint8_t>(128 + cdef1F(j - i / 2));
            img16[i * 8 + j] = img8[i * 8 + j];
        }
    std::int32_t v8 = 0, v16 = 0;
    l10_filters::cdefFindDir8bit(img8, 8, &v8, 0);
    l10_filters::cdefFindDir(img16, 8, &v16, 0);
    CHECK(v8 == v16);
}

TEST_CASE("cdefTapOffFrame: a tap above the frame with edge_top is off-frame") {
    CHECK(l10_filters::cdefTapOffFrame(-1, 0, 8, 8, 1, 0, 0, 0));
}

TEST_CASE("cdefFilterBlock8bitBounded with all edges clear == interior kernel") {
    static std::uint8_t in[12 * 144];
    std::uint32_t seed = 0x2545F491;
    for (int i = 0; i < 12 * 144; ++i) {
        seed = seed * 1103515245u + 12345u;
        in[i] = static_cast<std::uint8_t>((seed >> 16) & 0xFF);
    }
    std::uint8_t dstB[64], dstI[64];
    l10_filters::cdefFilterBlock8bitBounded(dstB, 8, in + 2 * 144 + 2, 31, 3, 3, 5,
                                           l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1, 0, 0, 0, 0);
    l10_filters::cdefFilterBlock8bit(dstI, 8, in + 2 * 144 + 2, 31, 3, 3, 5,
                                     l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    bool same = true;
    for (int i = 0; i < 64; ++i) same = same && (dstB[i] == dstI[i]);
    CHECK(same);
}

TEST_CASE("cdefFilterBlock8bitBounded top+left corner == 16-bit kernel with sentinels") {
    // The derivation pin: excluding an off-frame tap by geometry equals the
    // 16-bit sentinel treatment (constrain 0, max guarded, min never wins).
    static std::uint8_t in8[12 * 144];
    static std::uint16_t in16[12 * 144];
    for (int r = 0; r < 12; ++r)
        for (int c = 0; c < 144; ++c) {
            const bool off = r < 2 || c < 2;  // frame origin at buffer (2,2)
            const std::uint32_t seed = static_cast<std::uint32_t>(r * 131 + c * 17 + 7);
            in8[r * 144 + c] = off ? 0xEE : static_cast<std::uint8_t>((seed * 2654435761u >> 16) & 0xFF);
            in16[r * 144 + c] = off ? l10_filters::kCdefVeryLarge : in8[r * 144 + c];
        }
    std::uint8_t dstB[64];
    std::uint16_t dst16[64];
    l10_filters::cdefFilterBlock8bitBounded(dstB, 8, in8 + 2 * 144 + 2, 31, 3, 6, 4,
                                           l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1, 1, 1, 0, 0);
    l10_filters::cdefFilterBlock(nullptr, dst16, 8, in16 + 2 * 144 + 2, 31, 3, 6, 4, 4,
                                l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    bool same = true;
    for (int i = 0; i < 64; ++i) same = same && (dstB[i] == static_cast<std::uint8_t>(dst16[i]));
    CHECK(same);
}

TEST_CASE("cdefFilterBlock8bitBounded bottom+right corner == 16-bit kernel with sentinels") {
    static std::uint8_t in8[12 * 144];
    static std::uint16_t in16[12 * 144];
    for (int r = 0; r < 12; ++r)
        for (int c = 0; c < 144; ++c) {
            const bool off = r >= 10 || c >= 10;  // frame = rows/cols 2..9
            const std::uint32_t seed = static_cast<std::uint32_t>(r * 131 + c * 17 + 7);
            in8[r * 144 + c] = off ? 0xEE : static_cast<std::uint8_t>((seed * 2654435761u >> 16) & 0xFF);
            in16[r * 144 + c] = off ? l10_filters::kCdefVeryLarge : in8[r * 144 + c];
        }
    std::uint8_t dstB[64];
    std::uint16_t dst16[64];
    l10_filters::cdefFilterBlock8bitBounded(dstB, 8, in8 + 2 * 144 + 2, 31, 3, 6, 4,
                                           l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1, 0, 0, 1, 1);
    l10_filters::cdefFilterBlock(nullptr, dst16, 8, in16 + 2 * 144 + 2, 31, 3, 6, 4, 4,
                                l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    bool same = true;
    for (int i = 0; i < 64; ++i) same = same && (dstB[i] == static_cast<std::uint8_t>(dst16[i]));
    CHECK(same);
}

TEST_CASE("cdefFindDirList walks the dlist and writes dir[by][bx]") {
    static std::uint16_t in[16 * 144];
    for (int i = 0; i < 16 * 144; ++i) in[i] = 128;
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) {
            in[i * 144 + j] = static_cast<std::uint16_t>(128 + cdef1F(j));          // dir 6 at (0,0)
            in[(8 + i) * 144 + 8 + j] = static_cast<std::uint16_t>(128 + cdef1F(i));  // dir 2 at (1,1)
        }
    l10_filters::CdefList dlist[2] = {{0, 0}, {1, 1}};
    std::int32_t var[16][16] = {{0}};
    std::uint8_t dir[16][16] = {{0}};
    l10_filters::cdefFindDirList(in, dlist, var, 2, 0, dir);
    CHECK(dir[0][0] == 6);
}

TEST_CASE("cdefFindDirList dual pairing writes the second entry") {
    static std::uint16_t in[16 * 144];
    for (int i = 0; i < 16 * 144; ++i) in[i] = 128;
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) {
            in[i * 144 + j] = static_cast<std::uint16_t>(128 + cdef1F(j));
            in[(8 + i) * 144 + 8 + j] = static_cast<std::uint16_t>(128 + cdef1F(i));
        }
    l10_filters::CdefList dlist[2] = {{0, 0}, {1, 1}};
    std::int32_t var[16][16] = {{0}};
    std::uint8_t dir[16][16] = {{0}};
    l10_filters::cdefFindDirList(in, dlist, var, 2, 0, dir);
    CHECK(dir[1][1] == 2);
}

TEST_CASE("cdefFindDirList odd count runs the tail through the single kernel") {
    static std::uint16_t in[16 * 144];
    for (int i = 0; i < 16 * 144; ++i) in[i] = 128;
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) {
            in[i * 144 + j] = static_cast<std::uint16_t>(128 + cdef1F(j));
            in[i * 144 + 8 + j] = static_cast<std::uint16_t>(128 + cdef1F(i + j / 2));  // dir 1 at (0,1)
        }
    l10_filters::CdefList dlist[3] = {{0, 0}, {1, 1}, {0, 1}};
    std::int32_t var[16][16] = {{0}};
    std::uint8_t dir[16][16] = {{0}};
    l10_filters::cdefFindDirList(in, dlist, var, 3, 0, dir);
    CHECK(dir[0][1] == 1);
}

TEST_CASE("cdefFindDir8bitList walks the dlist on 8-bit input") {
    static std::uint8_t in[16 * 144];
    for (int i = 0; i < 16 * 144; ++i) in[i] = 128;
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            in[i * 144 + j] = static_cast<std::uint8_t>(128 + cdef1F(i));  // dir 2 at (0,0)
    l10_filters::CdefList dlist[2] = {{0, 0}, {1, 1}};
    std::int32_t var[16][16] = {{0}};
    std::uint8_t dir[16][16] = {{0}};
    l10_filters::cdefFindDir8bitList(in, dlist, var, 2, 0, dir);
    CHECK(dir[0][0] == 2);
}

TEST_CASE("cdefFilterBlock sec taps are asymmetric: only the up-right diag fires") {
    // dir 6: s0 (dir+2, k=0) sits up-right at block (3,5); the dir-2 k=0
    // taps sit down-right. Only (3,5) is raised: sum = 2*constrain(5,15,1)
    // = 10; y = 128 + (18>>4) = 129. Swapping dir+2/dir-2 moves the read to
    // (5,5) and gives 128.
    static std::uint16_t in[12 * 144];
    for (int i = 0; i < 12 * 144; ++i) in[i] = 128;
    in[(2 + 3) * 144 + 7] = 133;
    std::uint16_t dst[64];
    l10_filters::cdefFilterBlock(nullptr, dst, 8, in + 2 * 144 + 2, 0, 15, 6, 0, 1,
                                l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    CHECK(dst[4 * 8 + 4] == 129);
}

TEST_CASE("cdefFindDir div_table weighting: dir 6 wins by the edge weights") {
    // Derived with an independent PowerShell replica of the reference cost
    // walk (verified to reproduce the 215040 var fixture exactly): cost[6]
    // = 36,126,720 beats cost[0] = 36,114,432; swapping div_table[1]/[2]
    // (840/420) flips the winner to dir 0.
    static const std::uint16_t img[64] = {
        128, 128, 128, 64, 128, 128, 128, 192,
        192, 64, 128, 192, 128, 192, 192, 128,
        192, 64, 128, 128, 64, 192, 192, 192,
        192, 64, 192, 128, 128, 192, 128, 64,
        128, 192, 128, 64, 64, 128, 192, 64,
        192, 64, 128, 64, 64, 192, 64, 128,
        192, 128, 192, 192, 192, 128, 128, 64,
        192, 192, 128, 128, 64, 192, 64, 64};
    std::int32_t var = 0;
    CHECK(l10_filters::cdefFindDir(img, 8, &var, 0) == 6);
}
