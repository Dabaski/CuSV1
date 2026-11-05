#include <doctest.h>
#include <cstdint>
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
