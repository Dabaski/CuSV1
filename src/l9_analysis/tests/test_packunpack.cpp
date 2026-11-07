#include <array>
#include <cstdint>
#include <doctest.h>
#include <analysis.h>
#include <cdefault_extra.h>
#include <packunpack.h>

TEST_CASE("msbPack2d packs one pixel as in8<<2 plus the byte's top 2 bits") {
    // 1:1 target: svt_enc_msb_pack2_d
    // (third_party/SVT-AV1/Source/Lib/C_DEFAULT/pack_unpack_c.c:18-35):
    // out_pixel = (in8 << 2) | ((inn >> 6) & 3) - hand-checked:
    // 0xA5 = 165 -> 165 << 2 = 660; 0x80 >> 6 = 2 -> 660 | 2 = 662.
    std::uint8_t in8[1] = {0xA5};
    std::uint8_t inn[1] = {0x80};
    std::uint16_t out[1] = {0xFFFF};
    analysis::packunpack::msbPack2d(in8, 1, inn, 1, out, 1, 1, 1);
    CHECK(out[0] == 662);
}

TEST_CASE("msbPack2d honors three independent strides over a 2x2 block") {
    // Hand-computed from the :29-:32 form: out = (in8 << 2) | ((inn >> 6) & 3).
    // in8 rows {1, 2} and {3, 4} at stride 8; inn rows 0x40, 0x80, 0xC0, 0x00
    // (2-bit nibbles 1, 2, 3, 0) at stride 4; out at stride 4.
    // Expected out: {5, 10, 15, 16} at offsets 0, 1, 4, 5.
    std::array<std::uint8_t, 2 * 8> in8{};
    std::array<std::uint8_t, 2 * 4> inn{};
    std::array<std::uint16_t, 2 * 4> out{};
    in8.fill(0xEE);
    inn.fill(0xEE);
    out.fill(0xEEEE);
    in8[0] = 1; in8[1] = 2; in8[8] = 3; in8[9] = 4;
    inn[0] = 0x40; inn[1] = 0x80; inn[4] = 0xC0; inn[5] = 0x00;
    analysis::packunpack::msbPack2d(in8.data(), 8, inn.data(), 4, out.data(), 4, 2, 2);
    const std::uint16_t expected[4] = {5, 10, 15, 16};
    const std::size_t offsets[4] = {0, 1, 4, 5};
    for (int i = 0; i < 4; ++i) {
        CHECK(out[offsets[i]] == expected[i]);
    }
    CHECK(out[2] == 0xEEEE);
}

TEST_CASE("compressedPackMsb reads one quad MSB-first") {
    // 1:1 target: svt_compressed_packmsb_c
    // (third_party/SVT-AV1/Source/Lib/C_DEFAULT/pack_unpack_c.c:41-71):
    // four 2-bit pixels per inn byte, pix0 = (b >> 6) & 3 ... pix3 = b & 3.
    // inn = 0b00_01_10_11 = 0x1B -> nibbles {0, 1, 2, 3}; in8 {10, 20, 30, 40}
    // -> out {(10<<2)|0, (20<<2)|1, (30<<2)|2, (40<<2)|3} = {40, 81, 122, 163}.
    std::array<std::uint8_t, 4> in8{10, 20, 30, 40};
    std::array<std::uint8_t, 1> inn{0x1B};
    std::array<std::uint16_t, 4> out{};
    out.fill(0xEEEE);
    analysis::packunpack::compressedPackMsb(in8.data(), 4, inn.data(), 1,
                                            out.data(), 4, 4, 1);
    const std::uint16_t expected[4] = {40, 81, 122, 163};
    for (int i = 0; i < 4; ++i) {
        CHECK(out[i] == expected[i]);
    }
}

TEST_CASE("unpackAnd2bCompress splits one quad into 8-bit plus a packed 2-bit byte") {
    // 1:1 target: svt_unpack_and_2bcompress_c
    // (third_party/SVT-AV1/Source/Lib/C_DEFAULT/pack_unpack_c.c:78-146):
    // out8 = (uint8_t)(in16 >> 2); the 2-bit field is (uint8_t)(in16 << 6)
    // shifted/masked into bits {7-6, 5-4, 3-2, 1-0} of the comp byte.
    // Hand-checked for in16 = {0x3AB, 0x155, 0x3FF, 0x040}:
    //   out8 = {234, 85, 255, 16}; comp = 0b11'01'11'00 = 0xDC.
    std::array<std::uint16_t, 4> in16{0x3AB, 0x155, 0x3FF, 0x040};
    std::array<std::uint8_t, 4> out8{};
    std::array<std::uint8_t, 1> out2{};
    out8.fill(0xEE);
    out2.fill(0xEE);
    analysis::packunpack::unpackAnd2bCompress(in16.data(), 4, out8.data(), 4,
                                              out2.data(), 1, 4, 1);
    const std::uint8_t expected8[4] = {234, 85, 255, 16};
    for (int i = 0; i < 4; ++i) {
        CHECK(out8[i] == expected8[i]);
    }
    CHECK(out2[0] == 0xDC);
}

TEST_CASE("unpackAnd2bCompress carries width remainders 1, 2 and 3 into a partial comp byte") {
    // 1:1 target: the w_rem branch of svt_unpack_and_2bcompress_c
    // (pack_unpack_c.c:119-144): up to 3 trailing pixels per row still write
    // their partial comp byte, pad slots ZERO. in16[j] = 0x300 + j: the
    // 2-bit field is the LOW pair, (uint8_t)(in16 << 6) = (in16 & 3) << 6,
    // so field(j) = j & 3; out8 = (0x300 + j) >> 2 = 192 + (j >> 2).
    //   w=5: comp = 0b00'01'10'11 = 0x1B, rem byte 0b00'00'00'00 = 0x00;
    //   w=6: rem byte 0b00'01'00'00 = 0x10; w=7: 0b00'01'10'00 = 0x18.
    const int widths[3] = {5, 6, 7};
    const std::uint8_t expectedComp[3] = {0x00, 0x10, 0x18};
    const std::uint8_t expected8[7] = {192, 192, 192, 192, 193, 193, 193};
    for (int w = 0; w < 3; ++w) {
        std::array<std::uint16_t, 7> in16{};
        std::array<std::uint8_t, 8> out8{};
        std::array<std::uint8_t, 4> out2{};
        out8.fill(0xEE);
        out2.fill(0xEE);
        for (int j = 0; j < widths[w]; ++j) {
            in16[j] = static_cast<std::uint16_t>(0x300 + j);
        }
        analysis::packunpack::unpackAnd2bCompress(in16.data(), 7, out8.data(), 8,
                                                  out2.data(), 2,
                                                  static_cast<std::uint32_t>(widths[w]), 1);
        for (int j = 0; j < widths[w]; ++j) {
            CHECK(out8[j] == expected8[j]);
        }
        CHECK(out2[0] == 0x1B);
        CHECK(out2[1] == expectedComp[w]);
        CHECK(out8[widths[w]] == 0xEE);
        CHECK(out2[2] == 0xEE);
    }
}

TEST_CASE("unpackCompressed10bit places all four col%4 phases top-aligned") {
    // 1:1 target: svt_c_unpack_compressed_10bit
    // (third_party/SVT-AV1/Source/Lib/C_DEFAULT/pack_unpack_c.c:152-175):
    // the byte's 2-bit field for col is lifted to the TOP of the output byte:
    // phase 0 keeps data & 0xC0, phases 1/2/3 shift left 2/4/6 then mask
    // 0xC0. inn = 0x1B (pairs 00, 01, 10, 11) -> {0x00, 0x40, 0x80, 0xC0}.
    // Two rows at distinct strides to pin the row*stride indexing (row 1
    // reads inn[1 * 2] = inn[2]).
    std::array<std::uint8_t, 4> inn{0x1B, 0x00, 0xFF, 0x00};
    std::array<std::uint8_t, 8> out{};
    out.fill(0xEE);
    analysis::packunpack::unpackCompressed10bit(inn.data(), 2, out.data(), 4, 4, 2);
    const std::uint8_t expected[4] = {0x00, 0x40, 0x80, 0xC0};
    for (int phase = 0; phase < 4; ++phase) {
        CHECK(out[phase] == expected[phase]);
        CHECK(out[4 + phase] == 0xC0);
    }
}

TEST_CASE("msbUnPack2d splits 10-bit into 8-bit plus a top-aligned 2-bit byte") {
    // 1:1 target: svt_enc_msb_un_pack2_d
    // (third_party/SVT-AV1/Source/Lib/C_DEFAULT/pack_unpack_c.c:180-196):
    // out8 = (uint8_t)(in16 >> 2); the 2-bit byte is (uint8_t)(in16 << 6)
    // = (in16 & 3) << 6, the LOW pair top-aligned. Hand-checked:
    // 0x3AB -> 234 / 0xC0; 0x301 -> 192 / 0x40 (discriminates the low pair:
    // the top pair of 0x301 is 0b11, the low pair 0b01).
    std::array<std::uint16_t, 2> in16{0x3AB, 0x301};
    std::array<std::uint8_t, 2> out8{};
    std::array<std::uint8_t, 2> outn{};
    out8.fill(0xEE);
    outn.fill(0xEE);
    analysis::packunpack::msbUnPack2d(in16.data(), 2, out8.data(), outn.data(), 2, 2, 2, 1);
    const std::uint8_t expected8[2] = {234, 192};
    const std::uint8_t expectedN[2] = {0xC0, 0x40};
    for (int i = 0; i < 2; ++i) {
        CHECK(out8[i] == expected8[i]);
        CHECK(outn[i] == expectedN[i]);
    }
}

TEST_CASE("msbUnPack2d with a null 2-bit plane writes only the 8-bit output") {
    // 1:1 target: the outn_bit_buffer null guard of svt_enc_msb_un_pack2_d
    // (pack_unpack_c.c:190-193): the 2-bit write is skipped entirely.
    std::array<std::uint16_t, 2> in16{0x3AB, 0x301};
    std::array<std::uint8_t, 2> out8{};
    out8.fill(0xEE);
    analysis::packunpack::msbUnPack2d(in16.data(), 2, out8.data(), nullptr, 2, 2, 2, 1);
    CHECK(out8[0] == 234);
    CHECK(out8[1] == 192);
}

TEST_CASE("convert8bitTo16bit widens 2x2 across distinct strides") {
    // 1:1 target: svt_convert_8bit_to_16bit_c
    // (third_party/SVT-AV1/Source/Lib/C_DEFAULT/pack_unpack_c.c:198-205):
    // dst[k + j*dst_stride] = src[k + j*src_stride], plain widening.
    std::array<std::uint8_t, 6> src{1, 2, 0xEE, 0xEE, 3, 4};
    std::array<std::uint16_t, 6> dst{};
    dst.fill(0xEEEE);
    analysis::packunpack::convert8bitTo16bit(src.data(), 4, dst.data(), 3, 2, 2);
    CHECK(dst[0] == 1);
    CHECK(dst[1] == 2);
    CHECK(dst[3] == 3);
    CHECK(dst[4] == 4);
    CHECK(dst[2] == 0xEEEE);
}

TEST_CASE("convert16bitTo8bit truncates the low byte across distinct strides") {
    // 1:1 target: svt_convert_16bit_to_8bit_c
    // (third_party/SVT-AV1/Source/Lib/C_DEFAULT/pack_unpack_c.c:207-214):
    // dst = (uint8_t)src - TRUNCATION, not saturation: 0x1FF -> 0xFF,
    // 0x100 -> 0x00.
    std::array<std::uint16_t, 6> src{0x1FF, 0x100, 0xEEEE, 0xEEEE, 0x0AB, 0x35A};
    std::array<std::uint8_t, 6> dst{};
    dst.fill(0xEE);
    analysis::packunpack::convert16bitTo8bit(src.data(), 4, dst.data(), 3, 2, 2);
    CHECK(dst[0] == 0xFF);
    CHECK(dst[1] == 0x00);
    CHECK(dst[3] == 0xAB);
    CHECK(dst[4] == 0x5A);
    CHECK(dst[2] == 0xEE);
}

TEST_CASE("the pack/unpack pairs compose back to the 10-bit plane") {
    // 8-wide x 2 rows of 10-bit values (both bit pairs exercised): the three
    // identities over the pack_unpack_c.c kernels -
    //   (i)   msbUnPack2d -> msbPack2d == identity (:18-35 + :180-196),
    //   (ii)  unpackAnd2bCompress -> compressedPackMsb == identity
    //         (:41-71 + :78-146),
    //   (iii) the compressed 2-bit plane lifted by svt_c_unpack_compressed_
    //         _10bit (:152-175) equals msbUnPack2d's 2-bit plane.
    std::array<std::uint16_t, 2 * 16> in16{};
    std::array<std::uint8_t, 2 * 8> a8{};
    std::array<std::uint8_t, 2 * 8> an{};
    std::array<std::uint8_t, 2 * 2> b2{};
    std::array<std::uint8_t, 2 * 8> b8{};
    std::array<std::uint16_t, 2 * 16> back16{};
    std::array<std::uint8_t, 2 * 8> cn{};
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 8; ++c) {
            in16[r * 16 + c] = static_cast<std::uint16_t>((r * 256 + c * 37) & 0x3FF);
        }
    }
    analysis::packunpack::msbUnPack2d(in16.data(), 16, a8.data(), an.data(), 8, 8, 8, 2);
    analysis::packunpack::msbPack2d(a8.data(), 8, an.data(), 8, back16.data(), 16, 8, 2);
    for (int i = 0; i < 2 * 8; ++i) {
        CHECK(back16[i] == in16[i]);
    }
    analysis::packunpack::unpackAnd2bCompress(in16.data(), 16, b8.data(), 8, b2.data(), 2, 8, 2);
    analysis::packunpack::compressedPackMsb(b8.data(), 8, b2.data(), 2, back16.data(), 16, 8, 2);
    for (int i = 0; i < 2 * 8; ++i) {
        CHECK(back16[i] == in16[i]);
    }
    analysis::packunpack::unpackCompressed10bit(b2.data(), 2, cn.data(), 8, 8, 2);
    for (int i = 0; i < 2 * 8; ++i) {
        CHECK(cn[i] == an[i]);
    }
}

TEST_CASE("blendA64Vmask16bit with mask 0 yields src1") {
    // 1:1 target: svt_aom_highbd_blend_a64_vmask_16bit_c
    // (third_party/SVT-AV1/Source/Lib/C_DEFAULT/blend_a64_mask_c.c:15-37),
    // the only kernel of the file: dst = AOM_BLEND_A64(mask[i], src0, src1)
    // with AOM_BLEND_A64(a, v0, v1) = ROUND_POWER_OF_TWO(a*v0 +
    // (64 - a)*v1, 6) (Codec/definitions.h:1250-1251). mask 0 -> src1.
    std::array<std::uint16_t, 1> src0{77};
    std::array<std::uint16_t, 1> src1{33};
    std::array<std::uint8_t, 1> mask{0};
    std::array<std::uint16_t, 1> dst{};
    analysis::packunpack::blendA64Vmask16bit(dst.data(), 1, src0.data(), 1,
                                             src1.data(), 1, mask.data(), 1, 1);
    CHECK(dst[0] == 33);
}

TEST_CASE("blendA64Vmask16bit with mask 64 yields src0") {
    // Mask 64 = AOM_BLEND_A64_MAX_ALPHA: (64*v0 + 0*v1 + 32) >> 6 = v0.
    std::array<std::uint16_t, 1> src0{77};
    std::array<std::uint16_t, 1> src1{33};
    std::array<std::uint8_t, 1> mask{64};
    std::array<std::uint16_t, 1> dst{};
    analysis::packunpack::blendA64Vmask16bit(dst.data(), 1, src0.data(), 1,
                                             src1.data(), 1, mask.data(), 1, 1);
    CHECK(dst[0] == 77);
}

TEST_CASE("blendA64Vmask16bit rounds half up at mask 32 and indexes the mask per row") {
    // Two rows, masks {32, 64}, the mask PER ROW (:32-:34: m = mask[i]).
    // Row 0, m = 32, src0 = 1, src1 = 0: (32 + 0 + 32) >> 6 = 1 - the +32
    // rounding term rounds the exact-half 0.5 UP (a mutant without the +32
    // gives 0). Row 1, m = 64, src0 = 9: (576 + 32) >> 6 = 9 (a column-index
    // mutant reads mask[0] = 32 -> 8). Strides 3 to pin i*stride.
    std::array<std::uint16_t, 2 * 3> src0{1, 0xEE, 0xEE, 9, 0xEE, 0xEE};
    std::array<std::uint16_t, 2 * 3> src1{0, 0xEE, 0xEE, 6, 0xEE, 0xEE};
    std::array<std::uint8_t, 2> mask{32, 64};
    std::array<std::uint16_t, 2 * 3> dst{};
    dst.fill(0xEEEE);
    analysis::packunpack::blendA64Vmask16bit(dst.data(), 3, src0.data(), 3,
                                             src1.data(), 3, mask.data(), 1, 2);
    CHECK(dst[0] == 1);
    CHECK(dst[3] == 9);
    CHECK(dst[1] == 0xEEEE);
}

TEST_CASE("varianceCore sums the signed diffs and their squares over strided rows") {
    // 1:1 target: variance_c (third_party/SVT-AV1/Source/Lib/C_DEFAULT/
    // variance.c:141-158, static there; exported here - the AN8 statics
    // precedent). Hand-computed on a 3x2 block: diffs {0,10,20} and
    // {20,30,40} -> sum = 120, sse = 500 + 2900 = 3400.
    std::array<std::uint8_t, 2 * 4> a{10, 20, 30, 0xEE, 40, 50, 60, 0xEE};
    std::array<std::uint8_t, 2 * 5> b{10, 10, 10, 0xEE, 0xEE, 20, 20, 20, 0xEE, 0xEE};
    std::uint32_t sse = 0xEEEEEEEE;
    int sum = -1;
    analysis::varianceCore(a.data(), 4, b.data(), 5, 3, 2, &sse, &sum);
    CHECK(sse == 3400);
    CHECK(sum == 120);
}

TEST_CASE("variance4x4 matches an independent diff oracle on the general shape") {
    // 1:1 target: the VAR(4, 4) macro instance svt_aom_variance4x4_c
    // (C_DEFAULT/variance.c:184-190, VARIANCES(4, 4) at :222) - the GENERAL
    // two-buffer shape (the AN8 variance16x16/8x8 are the offs-folded
    // single-buffer shape, a different call form). Return:
    // *sse - (uint32_t)(((int64_t)sum * sum) / (W * H)).
    // Oracle (independent): a rows {200,100,200,100} x4 at stride 6, b
    // constant 100 at stride 0: diffs {100,0,100,0} per row -> sum 800,
    // sse 80000, var 80000 - 640000/16 = 40000.
    std::array<std::uint8_t, 4 * 6> a{200, 100, 200, 100, 0xEE, 0xEE,
                                      200, 100, 200, 100, 0xEE, 0xEE,
                                      200, 100, 200, 100, 0xEE, 0xEE,
                                      200, 100, 200, 100, 0xEE, 0xEE};
    // b at stride 0 is STILL INDEXED b[0..w-1] (the reference's offs is a
    // real array, pic_analysis_process.c:936-942) - four valid elements.
    std::array<std::uint8_t, 4> bConst{100, 100, 100, 100};
    std::uint32_t sse = 0xEEEEEEEE;
    std::uint64_t osum = 0;
    std::uint64_t osse = 0;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            const int d = static_cast<int>(a[r * 6 + c]) - 100;
            osum += d;
            osse += static_cast<std::uint64_t>(d) * d;
        }
    }
    const std::uint32_t expectedVar =
        static_cast<std::uint32_t>(osse - osum * osum / (4 * 4));
    const std::uint32_t var = analysis::variance4x4(a.data(), 6, bConst.data(), 0, &sse);
    CHECK(var == expectedVar);
    CHECK(var == 40000);
    CHECK(sse == 80000);
}

TEST_CASE("the remaining VAR instances match the oracle at every reference size") {
    // 1:1 targets: the 19 remaining VAR(W, H) instances of the VARIANCES
    // enumeration (C_DEFAULT/variance.c:208-229, minus the landed 16x16,
    // 8x8 and the C2 4x4): 128x128, 128x64, 64x128, 64x64, 64x32, 32x64,
    // 32x32, 32x16, 16x32, 16x8, 8x16, 8x4, 4x8, 4x16, 16x4, 8x32, 32x8,
    // 16x64, 64x16 - EVERY size the reference admits, each against the
    // independent diff oracle over the same buffers.
    const int sizes[19][2] = {{128, 128}, {128, 64}, {64, 128}, {64, 64},
                              {64, 32},   {32, 64},  {32, 32},  {32, 16},
                              {16, 32},   {16, 8},   {8, 16},   {8, 4},
                              {4, 8},     {4, 16},   {16, 4},   {8, 32},
                              {32, 8},    {16, 64},  {64, 16}};
    std::array<std::uint8_t, 128 * 133> aBuf{};
    std::array<std::uint8_t, 128 * 128> bBuf{};
    for (int r = 0; r < 128; ++r) {
        for (int c = 0; c < 133; ++c) {
            aBuf[r * 133 + c] = static_cast<std::uint8_t>((r * 7 + c * 3) & 0xFF);
        }
    }
    for (int r = 0; r < 128; ++r) {
        for (int c = 0; c < 128; ++c) {
            bBuf[r * 128 + c] = static_cast<std::uint8_t>((r * 11 + c) & 0xFF);
        }
    }
    auto dispatch = [](int w, int h, const std::uint8_t* a, int aStride,
                       const std::uint8_t* b, int bStride, std::uint32_t* sse)
        -> std::uint32_t {
        switch (w * 1000 + h) {
        case 128 * 1000 + 128: return analysis::variance128x128(a, aStride, b, bStride, sse);
        case 128 * 1000 + 64: return analysis::variance128x64(a, aStride, b, bStride, sse);
        case 64 * 1000 + 128: return analysis::variance64x128(a, aStride, b, bStride, sse);
        case 64 * 1000 + 64: return analysis::variance64x64(a, aStride, b, bStride, sse);
        case 64 * 1000 + 32: return analysis::variance64x32(a, aStride, b, bStride, sse);
        case 32 * 1000 + 64: return analysis::variance32x64(a, aStride, b, bStride, sse);
        case 32 * 1000 + 32: return analysis::variance32x32(a, aStride, b, bStride, sse);
        case 32 * 1000 + 16: return analysis::variance32x16(a, aStride, b, bStride, sse);
        case 16 * 1000 + 32: return analysis::variance16x32(a, aStride, b, bStride, sse);
        case 16 * 1000 + 8: return analysis::variance16x8(a, aStride, b, bStride, sse);
        case 8 * 1000 + 16: return analysis::variance8x16(a, aStride, b, bStride, sse);
        case 8 * 1000 + 4: return analysis::variance8x4(a, aStride, b, bStride, sse);
        case 4 * 1000 + 8: return analysis::variance4x8(a, aStride, b, bStride, sse);
        case 4 * 1000 + 16: return analysis::variance4x16(a, aStride, b, bStride, sse);
        case 16 * 1000 + 4: return analysis::variance16x4(a, aStride, b, bStride, sse);
        case 8 * 1000 + 32: return analysis::variance8x32(a, aStride, b, bStride, sse);
        case 32 * 1000 + 8: return analysis::variance32x8(a, aStride, b, bStride, sse);
        case 16 * 1000 + 64: return analysis::variance16x64(a, aStride, b, bStride, sse);
        case 64 * 1000 + 16: return analysis::variance64x16(a, aStride, b, bStride, sse);
        default: REQUIRE(false); return 0;
        }
    };
    for (const auto& wh : sizes) {
        const int w = wh[0];
        const int h = wh[1];
        std::uint64_t osum = 0;
        std::uint64_t osse = 0;
        for (int r = 0; r < h; ++r) {
            for (int c = 0; c < w; ++c) {
                const int d = static_cast<int>(aBuf[r * 133 + c]) -
                              static_cast<int>(bBuf[r * 128 + c]);
                osum += d;
                osse += static_cast<std::uint64_t>(d) * d;
            }
        }
        const std::uint32_t expectedVar =
            static_cast<std::uint32_t>(osse - osum * osum / (w * h));
        std::uint32_t sse = 0;
        const std::uint32_t var = dispatch(w, h, aBuf.data(), 133, bBuf.data(), 128, &sse);
        CHECK(var == expectedVar);
    }
}

// CS1: svt_av1_build_compound_diffwtd_mask_d16_c
// (C_DEFAULT/inter_prediction_c.c:30-43) with diffwtd_mask_d16 (:15-28).
// round = 2*FILTER_BITS(7) - round_0 - round_1 + (bd-8); m = clamp(38 +
// (|src0-src1| >> round)/16, 0, 64); mask = m, or 64-m for the INV type.
// round_0/round_1 are threaded as parameters (named deviation: the
// ConvolveParams struct-walled caller is not ported). Fixture: 8-bit,
// round_0=3, round_1=7 -> round=4; src0 = {0, 256, 1024, 65535}, src1 = 0
// -> diffs>>4 = {0,16,64,4095} -> /16 = {0,1,4,255} -> m = {38,39,42,64}.
TEST_CASE("diffwtdMaskD16 builds 38 and inverted masks from clamped m") {
    std::array<std::uint16_t, 4> a = {0, 256, 1024, 65535};
    std::array<std::uint16_t, 4> b = {0, 0, 0, 0};
    std::array<std::uint8_t, 4> mask = {0, 0, 0, 0};
    analysis::buildCompoundDiffwtdMaskD16(mask.data(), 0, a.data(), 2, b.data(), 2, 2, 2, 3, 7, 8);
    CHECK(mask[0] == 38);
    CHECK(mask[1] == 39);
    CHECK(mask[2] == 42);
    CHECK(mask[3] == 64);
    std::array<std::uint8_t, 4> maskInv = {0, 0, 0, 0};
    analysis::buildCompoundDiffwtdMaskD16(maskInv.data(), 1, a.data(), 2, b.data(), 2, 2, 2, 3, 7, 8);
    CHECK(maskInv[0] == 26);
    CHECK(maskInv[1] == 25);
    CHECK(maskInv[2] == 22);
    CHECK(maskInv[3] == 0);
}

// HB1: svt_aom_variance_highbd_c (C_DEFAULT/variance.c:162-181), the hbd
// twin of the landed varianceCore shape: sad + sse accumulate over int
// diffs, return *sse - ((int64_t)sad*sad)/(w*h). Hand oracle on 4x4,
// a_stride 5, b_stride 7, b = constant 5 (so diff = a - 5):
//   diffs {5,15,25,35 | 10,20,30,40 | 0,10,20,30 | 7,17,27,37}
//   sum = 328, sse = 8936, 328*328/16 = 6724 -> var = 2212.
// The uint16 inputs are 10-bit values (<= 1023); no bd parameter exists in
// the reference - the 10/12-bit distinction is the input VALUE range.
TEST_CASE("varianceHighbd matches the sum-of-squares oracle at 10 and 12 bit") {
    const std::uint16_t a[20] = {10, 20, 30, 40, 1023, 15, 25, 35, 45, 1022,
                                 5,  15, 25, 35, 1021, 12, 22, 32, 42, 1020};
    const std::uint16_t b[28] = {5, 5, 5, 5, 0, 0, 9, 5, 5, 5, 5, 0, 0, 9,
                                 5, 5, 5, 5, 0, 0, 9, 5, 5, 5, 5, 0, 0, 9};
    std::uint32_t sse = 0;
    const std::uint32_t var = analysis::packunpack::varianceHighbd(a, 5, b, 7, 4, 4, &sse);
    CHECK(sse == 8936);
    CHECK(var == 2212);

    // independent uint64 re-derivation at 10-bit values (all <= 1023)
    std::vector<std::uint16_t> a10(8 * 11), b10(8 * 11);
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 10; ++c) {
            a10[r * 11 + c] = static_cast<std::uint16_t>((r * 91 + c * 137) % 1024);
            b10[r * 11 + c] = static_cast<std::uint16_t>((r * 53 + c * 7 + 5) % 1024);
        }
    }
    std::uint32_t sse10 = 0;
    const std::uint32_t var10 = analysis::packunpack::varianceHighbd(a10.data(), 11, b10.data(), 11, 10, 8, &sse10);
    std::uint64_t osum = 0, osse = 0;
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 10; ++c) {
            const std::int64_t d = static_cast<std::int64_t>(a10[r * 11 + c]) - b10[r * 11 + c];
            osum += d;
            osse += static_cast<std::uint64_t>(d) * static_cast<std::uint64_t>(d);
        }
    }
    CHECK(sse10 == static_cast<std::uint32_t>(osse));
    CHECK(var10 == static_cast<std::uint32_t>(osse - osum * osum / 80));

    // 12-bit values (all <= 4095)
    std::vector<std::uint16_t> a12(16 * 6), b12(16 * 6);
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 5; ++c) {
            a12[r * 6 + c] = static_cast<std::uint16_t>((r * 401 + c * 1297) % 4096);
            b12[r * 6 + c] = static_cast<std::uint16_t>((r * 977 + c * 301) % 4096);
        }
    }
    std::uint32_t sse12 = 0;
    const std::uint32_t var12 = analysis::packunpack::varianceHighbd(a12.data(), 6, b12.data(), 6, 5, 16, &sse12);
    osum = 0;
    osse = 0;
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 5; ++c) {
            const std::int64_t d = static_cast<std::int64_t>(a12[r * 6 + c]) - b12[r * 6 + c];
            osum += d;
            osse += static_cast<std::uint64_t>(d) * static_cast<std::uint64_t>(d);
        }
    }
    CHECK(sse12 == static_cast<std::uint32_t>(osse));
    CHECK(var12 == static_cast<std::uint32_t>(osse - osum * osum / 80));
}

// HB1: svt_aom_highbd_mse16x16_c (C_DEFAULT/variance.c:337-352). The
// reference takes uint8_t* + CONVERT_TO_SHORTPTR staging; the port takes
// plain uint16 params (the CS1/LR1 precedent) - that signature difference
// is pinned by this test compiling without any staging. 16x16 fixed, the
// accumulation is uint64 with a (uint32_t) cast at the return.
// Hand oracle: src = constant 100, ref = constant 90 -> 256 diffs of 10
// -> tsse = 256 * 100 = 25600. Strides 17/19 read past each row's 16.
TEST_CASE("highbdMse16x16 matches the 256-element diff oracle with uint16 params") {
    std::vector<std::uint16_t> src(16 * 17 + 3), ref(16 * 19 + 3);
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; ++c) {
            src[r * 17 + c] = 100;
            ref[r * 19 + c] = 90;
        }
    }
    CHECK(analysis::packunpack::highbdMse16x16(src.data(), 17, ref.data(), 19) == 25600);

    // independent 256-element re-derivation at 12-bit values
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; ++c) {
            src[r * 17 + c] = static_cast<std::uint16_t>((r * 703 + c * 1541) % 4096);
            ref[r * 19 + c] = static_cast<std::uint16_t>((r * 1237 + c * 667) % 4096);
        }
    }
    std::uint64_t osse = 0;
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; ++c) {
            const std::int64_t d = static_cast<std::int64_t>(src[r * 17 + c]) - ref[r * 19 + c];
            osse += static_cast<std::uint64_t>(d) * static_cast<std::uint64_t>(d);
        }
    }
    CHECK(analysis::packunpack::highbdMse16x16(src.data(), 17, ref.data(), 19) == static_cast<std::uint32_t>(osse));
}
