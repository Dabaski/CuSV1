#include <doctest.h>
#include <cstdint>
#include <vector>
#include <picops.h>

namespace picops = pipeline::picops;

// trip 1: svt_residual_kernel16bit_c (pic_operators.c:27-46) - the residual
// = (int16)input - (int16)pred with INDEPENDENT src/pred/residual strides;
// the row is only width wide, the stride gap is untouched.
TEST_CASE("l6 picops residual16bit subtracts with independent strides") {
    const std::uint16_t input[10] = {200, 100, 50, 990, 991,
                                     90, 60, 30, 992, 993};
    const std::uint16_t pred[12] = {255, 0, 128, 700, 701, 702,
                                    10, 10, 10, 703, 704, 705};
    std::int16_t residual[8] = {0};
    picops::residualKernel16bit(input, 5, pred, 6, residual, 4, 3, 2);
    CHECK(residual[0] == -55);
    CHECK(residual[1] == 100);
    CHECK(residual[2] == -78);
    CHECK(residual[3] == 0);   // the residual_stride gap column
    CHECK(residual[4] == 80);
    CHECK(residual[5] == 50);
    CHECK(residual[6] == 20);
    CHECK(residual[7] == 0);   // the last row's gap column
}

// trip 2: svt_residual_kernel8bit_c (pic_operators.c:52-70) - the same
// no-clamp subtract rule at 8-bit (:59).
TEST_CASE("l6 picops residual8bit subtracts with independent strides") {
    const std::uint8_t input[10] = {200, 100, 50, 9, 9,
                                    90, 60, 30, 8, 8};
    const std::uint8_t pred[12] = {255, 0, 128, 7, 7, 7,
                                   10, 10, 10, 6, 6, 6};
    std::int16_t residual[8] = {0};
    picops::residualKernel8bit(input, 5, pred, 6, residual, 4, 3, 2);
    CHECK(residual[0] == -55);
    CHECK(residual[1] == 100);
    CHECK(residual[2] == -78);
    CHECK(residual[3] == 0);
    CHECK(residual[4] == 80);
    CHECK(residual[5] == 50);
    CHECK(residual[6] == 20);
    CHECK(residual[7] == 0);
}

// trip 3: the (int16_t) wrap-cast rule of :35 - a uint16 plane value above
// 32767 wrap-casts to the negative int16 before the subtract (the reference
// cast, not a clamp).
TEST_CASE("l6 picops residual16bit wraps the uint16 cast, never clamps") {
    const std::uint16_t input[1] = {65535};
    const std::uint16_t pred[1] = {0};
    std::int16_t residual[1] = {0};
    picops::residualKernel16bit(input, 1, pred, 1, residual, 1, 1, 1);
    CHECK(residual[0] == -1);
    const std::uint16_t input2[1] = {40000};
    const std::uint16_t pred2[1] = {1};
    picops::residualKernel16bit(input2, 1, pred2, 1, residual, 1, 1, 1);
    CHECK(residual[0] == -25537);
}

// trip 4: svt_full_distortion_kernel32_bits_c (pic_operators.c:77-98).
// residual = SSE(coeff - recon_coeff), prediction = SSE(coeff), accumulated
// in int64 and stored into [DIST_CALC_RESIDUAL, DIST_CALC_PREDICTION]
// (definitions.h:1936-1938). Hand oracle on a 2x2 block with stride 4:
//   coeff = {3, -1, x, x | 2, 0, x, x}, recon = {1, 1, x, x | 2, 2, x, x}
//   residual = (3-1)^2 + (-1-1)^2 + (2-2)^2 + (0-2)^2 = 4 + 4 + 0 + 4 = 12
//   prediction = 9 + 1 + 4 + 0 = 14
TEST_CASE("l6 picops fullDistortion32 accumulates both sses with stride") {
    const std::int32_t coeff[8] = {3, -1, 77, 78, 2, 0, 79, 80};
    const std::int32_t recon[8] = {1, 1, 55, 56, 2, 2, 57, 58};
    std::uint64_t dist[2] = {111, 222};
    picops::fullDistortionKernel32Bits(coeff, recon, 4, 2, 2, dist);
    CHECK(dist[0] == 12);
    CHECK(dist[1] == 14);
}

// trip 5: svt_full_distortion_kernel16_bits_c (pic_operators.c:100-123).
// The uint8*/offset form is the SVT byte-buffer convention for 16-bit
// planes: the offsets are applied in uint16 ELEMENTS after the cast
// (:106-109). Hand oracle on 2x2 with input offset 2 stride 5, pred
// offset 1 stride 6:
//   input reads flat {2,3} and {7,8}; pred reads flat {1,2} and {7,8}
//   diffs (10-9, 20-22, 30-28, 40-41) -> 1 + 4 + 4 + 1 = 10
TEST_CASE("l6 picops fullDistortion16 sses with element offsets and strides") {
    const std::uint16_t input[12] = {0, 0, 10, 20, 0, 0, 0, 30, 40, 0, 0, 0};
    const std::uint16_t pred[14] = {0, 9, 22, 0, 0, 0, 0, 28, 41, 0, 0, 0, 0, 0};
    const std::uint64_t sse = picops::fullDistortionKernel16Bits(input, 2, 5, pred, 1, 6, 2, 2);
    CHECK(sse == 10);
}

// trip 6: svt_full_distortion_kernel_cbf_zero32_bits_c (pic_operators.c:
// 128-147). The cbf-zero quirk: BOTH result slots receive the SAME
// prediction SSE (:145-146) - the residual slot is not zeroed, it is the
// coefficient SSE. Hand oracle on 2x2 coeff {3,-1,x,x | 2,0,x,x}:
// SSE = 9 + 1 + 4 + 0 = 14 in BOTH slots.
TEST_CASE("l6 picops fullDistortionCbfZero writes the same sse to both slots") {
    const std::int32_t coeff[8] = {3, -1, 77, 78, 2, 0, 79, 80};
    std::uint64_t dist[2] = {111, 222};
    picops::fullDistortionKernelCbfZero32Bits(coeff, 4, dist, 2, 2);
    CHECK(dist[0] == 14);
    CHECK(dist[1] == 14);
}

// trip 7: svt_aom_picture_full_distortion32_bits_single (:149-160), the
// cnt_nz_coeff == 0 branch: zero both slots, then the cbf-zero kernel
// overwrites them with the SAME value (:152-153, :157-158).
TEST_CASE("l6 picops pictureFullDistortion32Single cbf path zeroes then fills both slots") {
    const std::int32_t coeff[8] = {3, -1, 77, 78, 2, 0, 79, 80};
    std::uint64_t dist[2] = {111, 222};
    picops::pictureFullDistortion32Single(coeff, nullptr, 4, 2, 2, dist, 0);
    CHECK(dist[0] == 14);
    CHECK(dist[1] == 14);
}

// trip 8: the cnt_nz_coeff != 0 branch (:155-156): the kernel32 values
// (residual 12, prediction 14 - trip 4's oracle), reached through the
// dispatcher with the zeroed-then-overwritten slots.
TEST_CASE("l6 picops pictureFullDistortion32Single kernel path overwrites the zeroes") {
    const std::int32_t coeff[8] = {3, -1, 77, 78, 2, 0, 79, 80};
    const std::int32_t recon[8] = {1, 1, 55, 56, 2, 2, 57, 58};
    std::uint64_t dist[2] = {0, 0};
    picops::pictureFullDistortion32Single(coeff, recon, 4, 2, 2, dist, 1);
    CHECK(dist[0] == 12);
    CHECK(dist[1] == 14);
}

// trip 9: svt_aom_yv12_copy_y_c (pic_operators.c:194-215) flattened from
// Yv12BufferConfig to scalars (named deviation; the Y field mapping is
// y_buffer/y_stride/y_width/y_height). 8-bit branch: width 3 rows 2,
// src stride 5 != dst stride 6, so the copy must NOT walk the dst with
// the src stride.
TEST_CASE("l6 picops yv12CopyY copies partial rows across differing strides") {
    const std::uint8_t src[10] = {1, 2, 3, 91, 92, 4, 5, 6, 93, 94};
    std::uint8_t dst[14] = {0};
    picops::yv12CopyY(src, 5, dst, 6, 3, 2, false);
    CHECK(dst[0] == 1);
    CHECK(dst[1] == 2);
    CHECK(dst[2] == 3);
    CHECK(dst[3] == 0);   // the src stride gap is not copied
    CHECK(dst[4] == 0);
    CHECK(dst[5] == 0);   // the dst stride gap column
    CHECK(dst[6] == 4);
    CHECK(dst[7] == 5);
    CHECK(dst[8] == 6);
}

// trip 10: the YV12_FLAG_HIGHBITDEPTH branch of :194-215 (:199-208): the
// same buffers reinterpreted as uint16, copying width ELEMENTS (not
// bytes) per row, strides advanced in uint16 units.
TEST_CASE("l6 picops yv12CopyY highbd copies uint16 elements per row") {
    const std::uint16_t src16[10] = {300, 301, 302, 91, 92, 400, 401, 402, 93, 94};
    std::uint16_t dst16[16] = {0};
    picops::yv12CopyY(reinterpret_cast<const std::uint8_t*>(src16), 5,
                      reinterpret_cast<std::uint8_t*>(dst16), 6, 3, 2, true);
    CHECK(dst16[0] == 300);
    CHECK(dst16[1] == 301);
    CHECK(dst16[2] == 302);
    CHECK(dst16[3] == 0);
    CHECK(dst16[5] == 0);
    CHECK(dst16[6] == 400);
    CHECK(dst16[7] == 401);
    CHECK(dst16[8] == 402);
}

// trip 11: svt_aom_yv12_copy_u_c (:217-238), flattened - the U field
// mapping is u_buffer/uv_stride/uv_width/uv_height; the body is the same
// stride-copy, tested minimally per function (the U/V wrappers select
// their own fields in the reference).
TEST_CASE("l6 picops yv12CopyU copies the uv plane with uv dims") {
    const std::uint8_t src[10] = {10, 11, 12, 91, 92, 13, 14, 15, 93, 94};
    std::uint8_t dst[14] = {0};
    picops::yv12CopyU(src, 5, dst, 6, 3, 2, false);
    CHECK(dst[0] == 10);
    CHECK(dst[1] == 11);
    CHECK(dst[2] == 12);
    CHECK(dst[3] == 0);
    CHECK(dst[6] == 13);
    CHECK(dst[7] == 14);
    CHECK(dst[8] == 15);
}

// trip 12: svt_aom_yv12_copy_v_c (:240-261), flattened - minimal
// discriminating fixture per ported function.
TEST_CASE("l6 picops yv12CopyV copies the uv plane with uv dims") {
    const std::uint8_t src[10] = {20, 21, 22, 91, 92, 23, 24, 25, 93, 94};
    std::uint8_t dst[14] = {0};
    picops::yv12CopyV(src, 5, dst, 6, 3, 2, false);
    CHECK(dst[0] == 20);
    CHECK(dst[1] == 21);
    CHECK(dst[2] == 22);
    CHECK(dst[3] == 0);
    CHECK(dst[6] == 23);
    CHECK(dst[7] == 24);
    CHECK(dst[8] == 25);
}

// trip 13: svt_aom_generate_padding16_bit (pic_operators.c:348-388) - the
// uint16 plane padding AN10's register named excluded. Horizontal first:
// left pad = row[0], right pad = row[width-1] in uint16 ELEMENTS; then
// vertical: copy the fully padded row0/rowN (stride ELEMENTS) up/down.
// Fixture: 4x2 content, pad 2x1, stride 8; four rows in one buffer, src_pic
// at the row-1 content start (&buf[10]).
TEST_CASE("l6 picops generatePadding16Bit pads left right then replicates rows") {
    std::uint16_t buf[32];
    for (int i = 0; i < 32; ++i) buf[i] = 999;
    buf[10] = 1; buf[11] = 2; buf[12] = 3; buf[13] = 4;
    buf[18] = 5; buf[19] = 6; buf[20] = 7; buf[21] = 8;
    picops::generatePadding16Bit(&buf[10], 8, 4, 2, 2, 1);
    CHECK(buf[8] == 1);    // row1 left pad = row[0]
    CHECK(buf[9] == 1);
    CHECK(buf[12] == 3);   // content untouched
    CHECK(buf[13] == 4);
    CHECK(buf[14] == 4);   // row1 right pad = row[3]
    CHECK(buf[15] == 4);
    CHECK(buf[0] == 1);    // row0 = copy of padded row1
    CHECK(buf[3] == 2);
    CHECK(buf[7] == 4);
    CHECK(buf[16] == 5);   // row2 left pad
    CHECK(buf[23] == 8);   // row2 right pad
    CHECK(buf[24] == 5);   // row3 = copy of padded row2
    CHECK(buf[27] == 6);
    CHECK(buf[31] == 8);
}

TEST_CASE("l6 picops scaffold boots") {
    CHECK(true);
}

// CS1: svt_aom_picture_copy_kernel (C_DEFAULT/picture_operators_c.c:30-49).
// Copies area_width x area_height rows row-by-row honoring strides.
// Fixture: 3 rows x 4 cols, src stride 6, dst stride 8; padding bytes
// in dst beyond area_width must stay 9.
TEST_CASE("pictureCopy honors strides and leaves dst padding untouched") {
    std::uint8_t src[3 * 6] = {0};
    std::uint8_t dst[3 * 8];
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 8; ++x) dst[y * 8 + x] = 9;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x) src[y * 6 + x] = static_cast<std::uint8_t>(y * 10 + x + 1);
    pipeline::picops::pictureCopy(src, 6, dst, 8, 4, 3, 1);
    CHECK(dst[0] == 1);
    CHECK(dst[3] == 4);
    CHECK(dst[4] == 9);   // dst padding col 4 row 0
    CHECK(dst[8] == 11);  // row 1 col 0
    CHECK(dst[16] == 21); // row 2 col 0
    CHECK(dst[19] == 24); // row 2 col 3 (copied)
    CHECK(dst[23] == 9);  // dst padding col 7 row 2
}

// CS1: svt_spatial_full_distortion_kernel_c (C_DEFAULT/picture_operators_c.c:53-71).
// Sum of squared per-pixel differences with byte/element offsets applied
// to both planes. Fixture: 4x2, input ramp 1..8, recon zeros -> SSE 204.
// Second call applies input_offset = 2 to a 4x2 area inside a wider
// values 3,4,5,6 and 9,10,11,12 used -> SSE = 86 + 446 = 532.
TEST_CASE("spatialFullDistortion sums squared diffs, honoring offsets") {
    std::uint8_t input[2 * 4] = {1, 2, 3, 4, 5, 6, 7, 8};
    std::uint8_t recon[2 * 4] = {0};
    CHECK(pipeline::picops::spatialFullDistortion(input, 0, 4, recon, 0, 4, 4, 2) == 204);

    std::uint8_t input2[2 * 6] = {0};
    for (int i = 0; i < 12; ++i) input2[i] = static_cast<std::uint8_t>(i + 1);
    CHECK(pipeline::picops::spatialFullDistortion(input2, 2, 6, recon, 0, 4, 4, 2) == 532);
}

// CS1: svt_aom_hadamard_4x4_c (C_DEFAULT/picture_operators_c.c:85-115).
// Oracle derived with an independent Python replica of the reference
// algorithm (col4 butterflies with >>1, second pass stride 4, final
// SSE2-style transpose). Fixture: 4x4 ramp 1..16, stride 4.
TEST_CASE("hadamard4x4 ramp 1..16 matches replica") {
    std::int16_t src[16];
    for (int i = 0; i < 16; ++i) src[i] = static_cast<std::int16_t>(i + 1);
    std::int32_t coeff[16] = {0};
    pipeline::picops::hadamard4x4(src, 4, coeff);
    std::int32_t want[16] = {34, -8, -16, 0, -2, 0, 0, 0, -4, 0, 0, 0, 0, 0, 0, 0};
    CHECK(std::vector<std::int32_t>(coeff, coeff + 16) == std::vector<std::int32_t>(want, want + 16));
}

// CS1: svt_aom_hadamard_8x8_c (C_DEFAULT/picture_operators_c.c:150-174).
// col8 butterflies without >>1, second pass stride 8, no transpose (output
// stays in buffer2 order). Oracle from the independent Python replica.
// Fixture: 8x8 ramp 1..64, stride 8.
TEST_CASE("hadamard8x8 ramp 1..64 matches replica") {
    std::int16_t src[64];
    for (int i = 0; i < 64; ++i) src[i] = static_cast<std::int16_t>(i + 1);
    std::int32_t coeff[64] = {0};
    pipeline::picops::hadamard8x8(src, 8, coeff);
    std::int32_t want[64] = {2080, 0, -128, -64, 0, 0, 0, -32, 0, 0, 0, 0, 0, 0, 0, 0,
                             -1024, 0, 0, 0, 0, 0, 0, 0, -512, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0, -256, 0, 0, 0, 0, 0, 0, 0};
    CHECK(std::vector<std::int32_t>(coeff, coeff + 64) == std::vector<std::int32_t>(want, want + 64));
}

// CS1: svt_aom_hadamard_16x16_c (C_DEFAULT/picture_operators_c.c:265-292).
// Four 8x8 quadrant passes + fold at >>1 over {0,64,128,192}. Oracle from
// the independent Python replica. Fixture: 16x16 ramp 1..256, stride 16.
TEST_CASE("hadamard16x16 ramp 1..256 matches replica") {
    std::int16_t src[256];
    for (int i = 0; i < 256; ++i) src[i] = static_cast<std::int16_t>(i + 1);
    std::int32_t coeff[256] = {0};
    pipeline::picops::hadamard16x16(src, 16, coeff);
    CHECK(coeff[0] == 16448);
    CHECK(coeff[2] == -256);
    CHECK(coeff[3] == -128);
    CHECK(coeff[7] == -64);
    CHECK(coeff[16] == -4096);
    CHECK(coeff[24] == -2048);
    CHECK(coeff[56] == -1024);
    CHECK(coeff[64] == -512);
    CHECK(coeff[128] == -8192);
    CHECK(coeff[1] == 0);
    CHECK(coeff[255] == 0);
}

// CS1: svt_aom_hadamard_32x32_c (C_DEFAULT/picture_operators_c.c:294-321).
// Four 16x16 quadrant passes + fold at >>2 over {0,256,512,768}. Oracle
// from the independent Python replica. Fixture uses small magnitudes
// ((c % 8) - (r % 4), range -3..7) because the reference's own int16
// intermediate buffers are only specified for residual-range inputs
// (comments at :117 and :268 say 9-bit src_diff); a 1..1024 ramp would
// wrap inside the reference's int16 buffers.
TEST_CASE("hadamard32x32 small-magnitude fixture matches replica") {
    std::int16_t src[1024];
    for (int r = 0; r < 32; ++r) {
        for (int c = 0; c < 32; ++c) {
            src[r * 32 + c] = static_cast<std::int16_t>((c % 8) - (r % 4));
        }
    }
    std::int32_t coeff[1024] = {0};
    pipeline::picops::hadamard32x32(src, 32, coeff);
    CHECK(coeff[0] == 256);
    CHECK(coeff[2] == -256);
    CHECK(coeff[3] == -128);
    CHECK(coeff[7] == -64);
    CHECK(coeff[24] == 128);
    CHECK(coeff[56] == 64);
    CHECK(coeff[1] == 0);
    CHECK(coeff[255] == 0);
    CHECK(coeff[1023] == 0);
}

// CS1: svt_av1_copy_wxh_8bit_c (C_DEFAULT/picture_operators_c.c:323-328)
// and svt_av1_copy_wxh_16bit_c (:330-335): row-by-row w-wide copies
// honoring both strides. Fixture: 3 rows x 5 cols in a stride-7 buffer.
TEST_CASE("copyWxh8bit/16bit copy rows honoring strides") {
    std::uint8_t src8[3 * 7] = {0}, dst8[3 * 7];
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 7; ++x) dst8[y * 7 + x] = 200;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x) src8[y * 7 + x] = static_cast<std::uint8_t>(y + 1);
    pipeline::picops::copyWxh8bit(src8, 7, dst8, 7, 3, 5);
    CHECK(dst8[0] == 1);
    CHECK(dst8[5] == 200);  // beyond w, untouched
    CHECK(dst8[7] == 2);    // row 1
    CHECK(dst8[14] == 3);   // row 2

    std::uint16_t src16[3 * 7] = {0}, dst16[3 * 7];
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 7; ++x) dst16[y * 7 + x] = 60000;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x) src16[y * 7 + x] = static_cast<std::uint16_t>(1000 * (y + 1) + x);
    pipeline::picops::copyWxh16bit(src16, 7, dst16, 7, 3, 5);
    CHECK(dst16[0] == 1000);
    CHECK(dst16[4] == 1004);
    CHECK(dst16[5] == 60000); // beyond w, untouched
    CHECK(dst16[7] == 2000);  // row 1
}

// HB1: the hadamard hbd family (C_DEFAULT/picture_operators_c.c):
// hadamard_highbd_col8_first_pass (:176-203, int16, the landed col8
// shape), hadamard_highbd_col8_second_pass (:207-234, int32 - the 8-bit
// second pass would overflow here), and svt_aom_highbd_hadamard_8x8_c
// (:238-262). The hbd signature is EXACTLY the landed hadamard8x8's:
// (const int16_t* src_diff, ptrdiff_t src_stride, int32_t* coeff).
// Folding-shift oracle: the first pass writes fp(slot s, col c) to
// buffer[8*c + s]; the second pass group idx reads buffer[idx + 8k] =
// fp(slot idx, col k), so
//   coeff[8*fslot + j] = sum_k S[j][k] * fp(fslot, k)
//                      = sum_k sum_r S[j][k] * S[fslot][r] * src[r][k]
// with the butterfly sign table S below (independent arithmetic; the x8
// index is the FIRST-PASS SLOT and the sum runs over COLUMNS - the
// 8*idx + j of buffer2 pairs idx with the first-pass slot).
TEST_CASE("highbdHadamard8x8 equals the 8-bit transform on small inputs") {
    // |values| <= 100 keeps BOTH paths inside int16 in the second pass,
    // so the hbd and 8-bit results must be bit-identical.
    std::int16_t src[8 * 10];
    for (int i = 0; i < 80; ++i) {
        src[i] = static_cast<std::int16_t>(((i * 37) % 201) - 100);
    }
    std::int32_t coeffHbd[64] = {0};
    std::int32_t coeff8[64] = {0};
    picops::highbdHadamard8x8(src, 8, coeffHbd);
    picops::hadamard8x8(src, 8, coeff8);
    bool allOk = true;
    int firstDiff = -1;
    for (int i = 0; i < 64; ++i) {
        if (coeffHbd[i] != coeff8[i] && firstDiff < 0) {
            firstDiff = i;
        }
        allOk = allOk && (coeffHbd[i] == coeff8[i]);
    }
    CHECK(firstDiff < 0);
    CHECK(allOk);
}

TEST_CASE("highbdHadamard8x8 matches the folding-shift oracle at 13-bit range") {
    static const int S[8][8] = {
        {1, 1, 1, 1, 1, 1, 1, 1},
        {1, 1, -1, -1, -1, -1, 1, 1},
        {1, 1, 1, 1, -1, -1, -1, -1},
        {1, 1, -1, -1, 1, 1, -1, -1},
        {1, -1, -1, 1, 1, -1, -1, 1},
        {1, -1, -1, 1, -1, 1, 1, -1},
        {1, -1, 1, -1, -1, 1, -1, 1},
        {1, -1, 1, -1, 1, -1, 1, -1},
    };
    // stride 10 rows over 8 columns - the stride gap columns are never read
    std::int16_t src[8 * 10];
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            src[r * 10 + c] = static_cast<std::int16_t>(((r * 173 + c * 941) % 8191) - 4095);
        }
    }
    std::int32_t coeff[64] = {0};
    picops::highbdHadamard8x8(src, 10, coeff);
    bool allOk = true;
    for (int fslot = 0; fslot < 8; ++fslot) {
        for (int j = 0; j < 8; ++j) {
            std::int64_t o = 0;
            for (int k = 0; k < 8; ++k) {
                for (int r = 0; r < 8; ++r) {
                    o += static_cast<std::int64_t>(S[j][k]) * S[fslot][r] * src[r * 10 + k];
                }
            }
            allOk = allOk && (coeff[8 * fslot + j] == o);
        }
    }
    CHECK(allOk);
    // the documented dynamic range pin: all-4095 input -> coeff[0] = 262080
    // (int32); the 8-bit second pass would overflow int16 long before.
    std::int16_t maxSrc[64];
    for (int i = 0; i < 64; ++i) {
        maxSrc[i] = 4095;
    }
    std::int32_t maxCoeff[64] = {0};
    picops::highbdHadamard8x8(maxSrc, 8, maxCoeff);
    CHECK(maxCoeff[0] == 262080);
    // single-impulse overflow pin: src[0][0] = 4096 -> every coefficient
    // 4096 (S[j][0] = 1 for all j); an int16 second pass would wrap these
    // to -32768 once the accumulated magnitude crosses it - the fold-shift
    // point the int32 pass exists for.
    std::int16_t impulse[64] = {0};
    impulse[0] = 4096;
    std::int32_t impCoeff[64] = {0};
    picops::highbdHadamard8x8(impulse, 8, impCoeff);
    CHECK(impCoeff[0] == 4096);
    CHECK(impCoeff[8] == 4096);
    CHECK(impCoeff[63] == 4096);
}