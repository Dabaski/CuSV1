#include "transform.h"

namespace transforms {

namespace {

// svt_aom_eb_av1_cospi_arr_data[3], the cos_bit=13 row
// (svt_aom_eb_av1_cospi_arr_data[i][j] = round(cos(M_PI*j/128) * (1<<(cos_bit_min+i))))
const std::int32_t kCospi13[64] = {
    8192, 8190, 8182, 8170, 8153, 8130, 8103, 8071, 8035, 7993, 7946, 7895, 7839, 7779, 7713, 7643,
    7568, 7489, 7405, 7317, 7225, 7128, 7027, 6921, 6811, 6698, 6580, 6458, 6333, 6203, 6070, 5933,
    5793, 5649, 5501, 5351, 5197, 5040, 4880, 4717, 4551, 4383, 4212, 4038, 3862, 3683, 3503, 3320,
    3135, 2948, 2760, 2570, 2378, 2185, 1990, 1795, 1598, 1401, 1202, 1003, 803,  603,  402,  201,
};

// svt_aom_eb_av1_sinpi_arr_data[3], the cos_bit=13 row
// (svt_aom_eb_av1_sinpi_arr_data[i][j] = round((sqrt(2)*sin(j*Pi/9)*2/3)*(1<<(cos_bit_min+i))))
const std::int32_t kSinpi13[5] = {0, 2642, 4964, 6689, 7606};

// svt_aom_eb_av1_cospi_arr_data[2], the cos_bit=12 row (INV_COS_BIT,
// inv_transforms.h:24)
const std::int32_t kCospi12[64] = {
    4096, 4095, 4091, 4085, 4076, 4065, 4052, 4036, 4017, 3996, 3973, 3948, 3920, 3889, 3857, 3822,
    3784, 3745, 3703, 3659, 3612, 3564, 3513, 3461, 3406, 3349, 3290, 3229, 3166, 3102, 3035, 2967,
    2896, 2824, 2751, 2675, 2598, 2520, 2440, 2359, 2276, 2191, 2106, 2019, 1931, 1842, 1751, 1660,
    1567, 1474, 1380, 1285, 1189, 1092, 995,  897,  799,  700,  601,  501,  401,  301,  201,  101,
};

// svt_aom_eb_av1_sinpi_arr_data[2], the cos_bit=12 row
const std::int32_t kSinpi12[5] = {0, 1321, 2482, 3344, 3803};

// inv_transforms.c:88 clamp_value, fixed clamp bit (16 for 8-bit, per
// svt_av1_gen_inv_stage_range)
constexpr int kInvClampBit = 16;

std::int32_t clampValue(std::int32_t value, int bit) {
    if (bit <= 0) {
        return value;
    }
    const std::int64_t maxValue = (1LL << (bit - 1)) - 1;
    const std::int64_t minValue = -(1LL << (bit - 1));
    if (value < minValue) {
        return static_cast<std::int32_t>(minValue);
    }
    if (value > maxValue) {
        return static_cast<std::int32_t>(maxValue);
    }
    return value;
}

void clampBufIv(std::int32_t* buf, int size, int bit) {
    for (int i = 0; i < size; ++i) {
        buf[i] = clampValue(buf[i], bit);
    }
}

}  // namespace

std::int32_t cospi13(int index) {
    return kCospi13[index];
}

std::int32_t sinpi13(int index) {
    return kSinpi13[index];
}

std::int32_t halfBtf(std::int32_t w0, std::int32_t in0, std::int32_t w1, std::int32_t in1, int bit) {
    const std::int64_t result64    = static_cast<std::int64_t>(w0 * in0) + static_cast<std::int64_t>(w1 * in1);
    const std::int64_t intermediate = result64 + (1LL << (bit - 1));
    return static_cast<std::int32_t>(intermediate >> bit);
}

std::int32_t roundShift(std::int64_t value, int bit) {
    return static_cast<std::int32_t>((value + (1LL << (bit - 1))) >> bit);
}

// svt_av1_idct4_new (inv_transforms.c:97), cos_bit = 12 (INV_COS_BIT)
void idct4(const std::int32_t input[4], std::int32_t output[4]) {
    const int8_t cosBit = 12;
    const int8_t stageRange[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    std::int32_t bf0[4];
    std::int32_t bf1[4];
    std::int32_t step[4];

    bf1[0] = input[0];
    bf1[1] = input[2];
    bf1[2] = input[1];
    bf1[3] = input[3];

    for (int i = 0; i < 4; ++i) {
        bf0[i] = bf1[i];
    }
    step[0] = halfBtf(kCospi12[32], bf0[0], kCospi12[32], bf0[1], cosBit);
    step[1] = halfBtf(kCospi12[32], bf0[0], -kCospi12[32], bf0[1], cosBit);
    step[2] = halfBtf(kCospi12[48], bf0[2], -kCospi12[16], bf0[3], cosBit);
    step[3] = halfBtf(kCospi12[16], bf0[2], kCospi12[48], bf0[3], cosBit);

    output[0] = clampValue(step[0] + step[3], stageRange[3]);
    output[1] = clampValue(step[1] + step[2], stageRange[3]);
    output[2] = clampValue(step[1] - step[2], stageRange[3]);
    output[3] = clampValue(step[0] - step[3], stageRange[3]);
}

// svt_av1_iadst4_new (inv_transforms.c:729), cos_bit = 12 (incl. all-zero
// early-out; stage_range unused)
void iadst4(const std::int32_t input[4], std::int32_t output[4]) {
    const int bit   = 12;
    const std::int32_t x0 = input[0];
    const std::int32_t x1 = input[1];
    const std::int32_t x2 = input[2];
    const std::int32_t x3 = input[3];

    if (!(x0 | x1 | x2 | x3)) {
        output[0] = output[1] = output[2] = output[3] = 0;
        return;
    }

    std::int32_t s0 = kSinpi12[1] * x0;
    std::int32_t s1 = kSinpi12[2] * x0;
    std::int32_t s2 = kSinpi12[3] * x1;
    std::int32_t s3 = kSinpi12[4] * x2;
    std::int32_t s4 = kSinpi12[1] * x2;
    std::int32_t s5 = kSinpi12[2] * x3;
    std::int32_t s6 = kSinpi12[4] * x3;

    std::int32_t s7 = (x0 - x2) + x3;

    s0 = s0 + s3;
    s1 = s1 - s4;
    s3 = s2;
    s2 = kSinpi12[3] * s7;

    s0 = s0 + s5;
    s1 = s1 - s6;

    std::int32_t y0 = s0 + s3;
    std::int32_t y1 = s1 + s3;
    std::int32_t y2 = s2;
    std::int32_t y3 = s0 + s1;

    y3 = y3 - s3;

    output[0] = roundShift(y0, bit);
    output[1] = roundShift(y1, bit);
    output[2] = roundShift(y2, bit);
    output[3] = roundShift(y3, bit);
}

// svt_av1_idct8_new (inv_transforms.c:136), cos_bit = 12 (INV_COS_BIT).
// stage_range: {16,...} shim confirmed exact by gen_inv_range_8x8_dct gate
// line (B1) â€” all 6 stages clamp at bit 16 for bd=8.
void idct8(const std::int32_t input[8], std::int32_t output[8]) {
    const int8_t cosBit = 12;
    const int8_t stageRange[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    std::int32_t bf0[8];
    std::int32_t step[8];

    // stage 1: bit-reverse
    bf0[0] = input[0];
    bf0[1] = input[4];
    bf0[2] = input[2];
    bf0[3] = input[6];
    bf0[4] = input[1];
    bf0[5] = input[5];
    bf0[6] = input[3];
    bf0[7] = input[7];

    // stage 2
    step[0] = bf0[0];
    step[1] = bf0[1];
    step[2] = bf0[2];
    step[3] = bf0[3];
    step[4] = halfBtf(kCospi12[56], bf0[4], -kCospi12[8], bf0[7], cosBit);
    step[5] = halfBtf(kCospi12[24], bf0[5], -kCospi12[40], bf0[6], cosBit);
    step[6] = halfBtf(kCospi12[40], bf0[5], kCospi12[24], bf0[6], cosBit);
    step[7] = halfBtf(kCospi12[8], bf0[4], kCospi12[56], bf0[7], cosBit);

    // stage 3
    bf0[0] = halfBtf(kCospi12[32], step[0], kCospi12[32], step[1], cosBit);
    bf0[1] = halfBtf(kCospi12[32], step[0], -kCospi12[32], step[1], cosBit);
    bf0[2] = halfBtf(kCospi12[48], step[2], -kCospi12[16], step[3], cosBit);
    bf0[3] = halfBtf(kCospi12[16], step[2], kCospi12[48], step[3], cosBit);
    bf0[4] = clampValue(step[4] + step[5], stageRange[3]);
    bf0[5] = clampValue(step[4] - step[5], stageRange[3]);
    bf0[6] = clampValue(-step[6] + step[7], stageRange[3]);
    bf0[7] = clampValue(step[6] + step[7], stageRange[3]);

    // stage 4
    step[0] = clampValue(bf0[0] + bf0[3], stageRange[4]);
    step[1] = clampValue(bf0[1] + bf0[2], stageRange[4]);
    step[2] = clampValue(bf0[1] - bf0[2], stageRange[4]);
    step[3] = clampValue(bf0[0] - bf0[3], stageRange[4]);
    step[4] = bf0[4];
    step[5] = halfBtf(-kCospi12[32], bf0[5], kCospi12[32], bf0[6], cosBit);
    step[6] = halfBtf(kCospi12[32], bf0[5], kCospi12[32], bf0[6], cosBit);
    step[7] = bf0[7];

    // stage 5: final butterfly
    output[0] = clampValue(step[0] + step[7], stageRange[5]);
    output[1] = clampValue(step[1] + step[6], stageRange[5]);
    output[2] = clampValue(step[2] + step[5], stageRange[5]);
    output[3] = clampValue(step[3] + step[4], stageRange[5]);
    output[4] = clampValue(step[3] - step[4], stageRange[5]);
    output[5] = clampValue(step[2] - step[5], stageRange[5]);
    output[6] = clampValue(step[1] - step[6], stageRange[5]);
    output[7] = clampValue(step[0] - step[7], stageRange[5]);
}

// svt_av1_iadst8_new (inv_transforms.c:822), cos_bit = 12 (INV_COS_BIT).
// Unlike iadst4, iadst8 HAS clamp_value at stages 3 and 5 â€” mirrored exactly.
// stage_range: {16,...} shim confirmed exact by gen_inv_range_8x8_adst gate
// line (B1) â€” all 8 stages clamp at bit 16 for bd=8.
void iadst8(const std::int32_t input[8], std::int32_t output[8]) {
    const int8_t cosBit = 12;
    const int8_t stageRange[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    std::int32_t bf0[8];
    std::int32_t step[8];

    // stage 1: permutation
    bf0[0] = input[7];
    bf0[1] = input[0];
    bf0[2] = input[5];
    bf0[3] = input[2];
    bf0[4] = input[3];
    bf0[5] = input[4];
    bf0[6] = input[1];
    bf0[7] = input[6];

    // stage 2
    step[0] = halfBtf(kCospi12[4], bf0[0], kCospi12[60], bf0[1], cosBit);
    step[1] = halfBtf(kCospi12[60], bf0[0], -kCospi12[4], bf0[1], cosBit);
    step[2] = halfBtf(kCospi12[20], bf0[2], kCospi12[44], bf0[3], cosBit);
    step[3] = halfBtf(kCospi12[44], bf0[2], -kCospi12[20], bf0[3], cosBit);
    step[4] = halfBtf(kCospi12[36], bf0[4], kCospi12[28], bf0[5], cosBit);
    step[5] = halfBtf(kCospi12[28], bf0[4], -kCospi12[36], bf0[5], cosBit);
    step[6] = halfBtf(kCospi12[52], bf0[6], kCospi12[12], bf0[7], cosBit);
    step[7] = halfBtf(kCospi12[12], bf0[6], -kCospi12[52], bf0[7], cosBit);

    // stage 3: clamp butterfly
    bf0[0] = clampValue(step[0] + step[4], stageRange[3]);
    bf0[1] = clampValue(step[1] + step[5], stageRange[3]);
    bf0[2] = clampValue(step[2] + step[6], stageRange[3]);
    bf0[3] = clampValue(step[3] + step[7], stageRange[3]);
    bf0[4] = clampValue(step[0] - step[4], stageRange[3]);
    bf0[5] = clampValue(step[1] - step[5], stageRange[3]);
    bf0[6] = clampValue(step[2] - step[6], stageRange[3]);
    bf0[7] = clampValue(step[3] - step[7], stageRange[3]);

    // stage 4
    step[0] = bf0[0];
    step[1] = bf0[1];
    step[2] = bf0[2];
    step[3] = bf0[3];
    step[4] = halfBtf(kCospi12[16], bf0[4], kCospi12[48], bf0[5], cosBit);
    step[5] = halfBtf(kCospi12[48], bf0[4], -kCospi12[16], bf0[5], cosBit);
    step[6] = halfBtf(-kCospi12[48], bf0[6], kCospi12[16], bf0[7], cosBit);
    step[7] = halfBtf(kCospi12[16], bf0[6], kCospi12[48], bf0[7], cosBit);

    // stage 5: clamp butterfly
    bf0[0] = clampValue(step[0] + step[2], stageRange[5]);
    bf0[1] = clampValue(step[1] + step[3], stageRange[5]);
    bf0[2] = clampValue(step[0] - step[2], stageRange[5]);
    bf0[3] = clampValue(step[1] - step[3], stageRange[5]);
    bf0[4] = clampValue(step[4] + step[6], stageRange[5]);
    bf0[5] = clampValue(step[5] + step[7], stageRange[5]);
    bf0[6] = clampValue(step[4] - step[6], stageRange[5]);
    bf0[7] = clampValue(step[5] - step[7], stageRange[5]);

    // stage 6
    step[0] = bf0[0];
    step[1] = bf0[1];
    step[2] = halfBtf(kCospi12[32], bf0[2], kCospi12[32], bf0[3], cosBit);
    step[3] = halfBtf(kCospi12[32], bf0[2], -kCospi12[32], bf0[3], cosBit);
    step[4] = bf0[4];
    step[5] = bf0[5];
    step[6] = halfBtf(kCospi12[32], bf0[6], kCospi12[32], bf0[7], cosBit);
    step[7] = halfBtf(kCospi12[32], bf0[6], -kCospi12[32], bf0[7], cosBit);

    // stage 7: output permutation with negation
    output[0] = step[0];
    output[1] = -step[4];
    output[2] = step[6];
    output[3] = -step[2];
    output[4] = step[3];
    output[5] = -step[7];
    output[6] = step[5];
    output[7] = -step[1];
}

// svt_av1_idct16_new (inv_transforms.c:215-376), cos_bit = 12 (INV_COS_BIT);
// stage_range consumed at stages 3-7 (clamp_value on the butterfly adds),
// shim {16,...} proven by gen_inv_range_16x16_dct
void idct16(const std::int32_t input[16], std::int32_t output[16]) {
    const int8_t cosBit = 12;
    const int8_t stageRange[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    std::int32_t bf0[16];
    std::int32_t step[16];

    // stage 1 (even-odd interleave; no clamp)
    bf0[0]  = input[0];
    bf0[1]  = input[8];
    bf0[2]  = input[4];
    bf0[3]  = input[12];
    bf0[4]  = input[2];
    bf0[5]  = input[10];
    bf0[6]  = input[6];
    bf0[7]  = input[14];
    bf0[8]  = input[1];
    bf0[9]  = input[9];
    bf0[10] = input[5];
    bf0[11] = input[13];
    bf0[12] = input[3];
    bf0[13] = input[11];
    bf0[14] = input[7];
    bf0[15] = input[15];

    // stage 2 (half_btf; no clamp)
    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = bf0[2];
    step[3]  = bf0[3];
    step[4]  = bf0[4];
    step[5]  = bf0[5];
    step[6]  = bf0[6];
    step[7]  = bf0[7];
    step[8]  = halfBtf(kCospi12[60], bf0[8], -kCospi12[4], bf0[15], cosBit);
    step[9]  = halfBtf(kCospi12[28], bf0[9], -kCospi12[36], bf0[14], cosBit);
    step[10] = halfBtf(kCospi12[44], bf0[10], -kCospi12[20], bf0[13], cosBit);
    step[11] = halfBtf(kCospi12[12], bf0[11], -kCospi12[52], bf0[12], cosBit);
    step[12] = halfBtf(kCospi12[52], bf0[11], kCospi12[12], bf0[12], cosBit);
    step[13] = halfBtf(kCospi12[20], bf0[10], kCospi12[44], bf0[13], cosBit);
    step[14] = halfBtf(kCospi12[36], bf0[9], kCospi12[28], bf0[14], cosBit);
    step[15] = halfBtf(kCospi12[4], bf0[8], kCospi12[60], bf0[15], cosBit);

    // stage 3 (clamp at stage_range[3])
    bf0[0]  = step[0];
    bf0[1]  = step[1];
    bf0[2]  = step[2];
    bf0[3]  = step[3];
    bf0[4]  = halfBtf(kCospi12[56], step[4], -kCospi12[8], step[7], cosBit);
    bf0[5]  = halfBtf(kCospi12[24], step[5], -kCospi12[40], step[6], cosBit);
    bf0[6]  = halfBtf(kCospi12[40], step[5], kCospi12[24], step[6], cosBit);
    bf0[7]  = halfBtf(kCospi12[8], step[4], kCospi12[56], step[7], cosBit);
    bf0[8]  = clampValue(step[8] + step[9], stageRange[3]);
    bf0[9]  = clampValue(step[8] - step[9], stageRange[3]);
    bf0[10] = clampValue(-step[10] + step[11], stageRange[3]);
    bf0[11] = clampValue(step[10] + step[11], stageRange[3]);
    bf0[12] = clampValue(step[12] + step[13], stageRange[3]);
    bf0[13] = clampValue(step[12] - step[13], stageRange[3]);
    bf0[14] = clampValue(-step[14] + step[15], stageRange[3]);
    bf0[15] = clampValue(step[14] + step[15], stageRange[3]);

    // stage 4 (clamp at stage_range[4])
    step[0]  = halfBtf(kCospi12[32], bf0[0], kCospi12[32], bf0[1], cosBit);
    step[1]  = halfBtf(kCospi12[32], bf0[0], -kCospi12[32], bf0[1], cosBit);
    step[2]  = halfBtf(kCospi12[48], bf0[2], -kCospi12[16], bf0[3], cosBit);
    step[3]  = halfBtf(kCospi12[16], bf0[2], kCospi12[48], bf0[3], cosBit);
    step[4]  = clampValue(bf0[4] + bf0[5], stageRange[4]);
    step[5]  = clampValue(bf0[4] - bf0[5], stageRange[4]);
    step[6]  = clampValue(-bf0[6] + bf0[7], stageRange[4]);
    step[7]  = clampValue(bf0[6] + bf0[7], stageRange[4]);
    step[8]  = bf0[8];
    step[9]  = halfBtf(-kCospi12[16], bf0[9], kCospi12[48], bf0[14], cosBit);
    step[10] = halfBtf(-kCospi12[48], bf0[10], -kCospi12[16], bf0[13], cosBit);
    step[11] = bf0[11];
    step[12] = bf0[12];
    step[13] = halfBtf(-kCospi12[16], bf0[10], kCospi12[48], bf0[13], cosBit);
    step[14] = halfBtf(kCospi12[48], bf0[9], kCospi12[16], bf0[14], cosBit);
    step[15] = bf0[15];

    // stage 5 (clamp at stage_range[5])
    bf0[0]  = clampValue(step[0] + step[3], stageRange[5]);
    bf0[1]  = clampValue(step[1] + step[2], stageRange[5]);
    bf0[2]  = clampValue(step[1] - step[2], stageRange[5]);
    bf0[3]  = clampValue(step[0] - step[3], stageRange[5]);
    bf0[4]  = step[4];
    bf0[5]  = halfBtf(-kCospi12[32], step[5], kCospi12[32], step[6], cosBit);
    bf0[6]  = halfBtf(kCospi12[32], step[5], kCospi12[32], step[6], cosBit);
    bf0[7]  = step[7];
    bf0[8]  = clampValue(step[8] + step[11], stageRange[5]);
    bf0[9]  = clampValue(step[9] + step[10], stageRange[5]);
    bf0[10] = clampValue(step[9] - step[10], stageRange[5]);
    bf0[11] = clampValue(step[8] - step[11], stageRange[5]);
    bf0[12] = clampValue(-step[12] + step[15], stageRange[5]);
    bf0[13] = clampValue(-step[13] + step[14], stageRange[5]);
    bf0[14] = clampValue(step[13] + step[14], stageRange[5]);
    bf0[15] = clampValue(step[12] + step[15], stageRange[5]);

    // stage 6 (clamp at stage_range[6])
    step[0]  = clampValue(bf0[0] + bf0[7], stageRange[6]);
    step[1]  = clampValue(bf0[1] + bf0[6], stageRange[6]);
    step[2]  = clampValue(bf0[2] + bf0[5], stageRange[6]);
    step[3]  = clampValue(bf0[3] + bf0[4], stageRange[6]);
    step[4]  = clampValue(bf0[3] - bf0[4], stageRange[6]);
    step[5]  = clampValue(bf0[2] - bf0[5], stageRange[6]);
    step[6]  = clampValue(bf0[1] - bf0[6], stageRange[6]);
    step[7]  = clampValue(bf0[0] - bf0[7], stageRange[6]);
    step[8]  = bf0[8];
    step[9]  = bf0[9];
    step[10] = halfBtf(-kCospi12[32], bf0[10], kCospi12[32], bf0[13], cosBit);
    step[11] = halfBtf(-kCospi12[32], bf0[11], kCospi12[32], bf0[12], cosBit);
    step[12] = halfBtf(kCospi12[32], bf0[11], kCospi12[32], bf0[12], cosBit);
    step[13] = halfBtf(kCospi12[32], bf0[10], kCospi12[32], bf0[13], cosBit);
    step[14] = bf0[14];
    step[15] = bf0[15];

    // stage 7 (clamp at stage_range[7])
    output[0]  = clampValue(step[0] + step[15], stageRange[7]);
    output[1]  = clampValue(step[1] + step[14], stageRange[7]);
    output[2]  = clampValue(step[2] + step[13], stageRange[7]);
    output[3]  = clampValue(step[3] + step[12], stageRange[7]);
    output[4]  = clampValue(step[4] + step[11], stageRange[7]);
    output[5]  = clampValue(step[5] + step[10], stageRange[7]);
    output[6]  = clampValue(step[6] + step[9], stageRange[7]);
    output[7]  = clampValue(step[7] + step[8], stageRange[7]);
    output[8]  = clampValue(step[7] - step[8], stageRange[7]);
    output[9]  = clampValue(step[6] - step[9], stageRange[7]);
    output[10] = clampValue(step[5] - step[10], stageRange[7]);
    output[11] = clampValue(step[4] - step[11], stageRange[7]);
    output[12] = clampValue(step[3] - step[12], stageRange[7]);
    output[13] = clampValue(step[2] - step[13], stageRange[7]);
    output[14] = clampValue(step[1] - step[14], stageRange[7]);
    output[15] = clampValue(step[0] - step[15], stageRange[7]);
}

// svt_av1_iadst16_new (inv_transforms.c:927-1130), cos_bit = 12; stage_range
// consumed at stages 3/5/7; no all-zero early-out at 16
void iadst16(const std::int32_t input[16], std::int32_t output[16]) {
    const int8_t cosBit = 12;
    const int8_t stageRange[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    std::int32_t bf0[16];
    std::int32_t step[16];

    // stage 1 (reversed interleave; no clamp)
    bf0[0]  = input[15];
    bf0[1]  = input[0];
    bf0[2]  = input[13];
    bf0[3]  = input[2];
    bf0[4]  = input[11];
    bf0[5]  = input[4];
    bf0[6]  = input[9];
    bf0[7]  = input[6];
    bf0[8]  = input[7];
    bf0[9]  = input[8];
    bf0[10] = input[5];
    bf0[11] = input[10];
    bf0[12] = input[3];
    bf0[13] = input[12];
    bf0[14] = input[1];
    bf0[15] = input[14];

    // stage 2 (half_btf; no clamp)
    step[0]  = halfBtf(kCospi12[2], bf0[0], kCospi12[62], bf0[1], cosBit);
    step[1]  = halfBtf(kCospi12[62], bf0[0], -kCospi12[2], bf0[1], cosBit);
    step[2]  = halfBtf(kCospi12[10], bf0[2], kCospi12[54], bf0[3], cosBit);
    step[3]  = halfBtf(kCospi12[54], bf0[2], -kCospi12[10], bf0[3], cosBit);
    step[4]  = halfBtf(kCospi12[18], bf0[4], kCospi12[46], bf0[5], cosBit);
    step[5]  = halfBtf(kCospi12[46], bf0[4], -kCospi12[18], bf0[5], cosBit);
    step[6]  = halfBtf(kCospi12[26], bf0[6], kCospi12[38], bf0[7], cosBit);
    step[7]  = halfBtf(kCospi12[38], bf0[6], -kCospi12[26], bf0[7], cosBit);
    step[8]  = halfBtf(kCospi12[34], bf0[8], kCospi12[30], bf0[9], cosBit);
    step[9]  = halfBtf(kCospi12[30], bf0[8], -kCospi12[34], bf0[9], cosBit);
    step[10] = halfBtf(kCospi12[42], bf0[10], kCospi12[22], bf0[11], cosBit);
    step[11] = halfBtf(kCospi12[22], bf0[10], -kCospi12[42], bf0[11], cosBit);
    step[12] = halfBtf(kCospi12[50], bf0[12], kCospi12[14], bf0[13], cosBit);
    step[13] = halfBtf(kCospi12[14], bf0[12], -kCospi12[50], bf0[13], cosBit);
    step[14] = halfBtf(kCospi12[58], bf0[14], kCospi12[6], bf0[15], cosBit);
    step[15] = halfBtf(kCospi12[6], bf0[14], -kCospi12[58], bf0[15], cosBit);

    // stage 3 (clamp at stage_range[3])
    bf0[0]  = clampValue(step[0] + step[8], stageRange[3]);
    bf0[1]  = clampValue(step[1] + step[9], stageRange[3]);
    bf0[2]  = clampValue(step[2] + step[10], stageRange[3]);
    bf0[3]  = clampValue(step[3] + step[11], stageRange[3]);
    bf0[4]  = clampValue(step[4] + step[12], stageRange[3]);
    bf0[5]  = clampValue(step[5] + step[13], stageRange[3]);
    bf0[6]  = clampValue(step[6] + step[14], stageRange[3]);
    bf0[7]  = clampValue(step[7] + step[15], stageRange[3]);
    bf0[8]  = clampValue(step[0] - step[8], stageRange[3]);
    bf0[9]  = clampValue(step[1] - step[9], stageRange[3]);
    bf0[10] = clampValue(step[2] - step[10], stageRange[3]);
    bf0[11] = clampValue(step[3] - step[11], stageRange[3]);
    bf0[12] = clampValue(step[4] - step[12], stageRange[3]);
    bf0[13] = clampValue(step[5] - step[13], stageRange[3]);
    bf0[14] = clampValue(step[6] - step[14], stageRange[3]);
    bf0[15] = clampValue(step[7] - step[15], stageRange[3]);

    // stage 4 (half_btf on 8-15; no clamp)
    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = bf0[2];
    step[3]  = bf0[3];
    step[4]  = bf0[4];
    step[5]  = bf0[5];
    step[6]  = bf0[6];
    step[7]  = bf0[7];
    step[8]  = halfBtf(kCospi12[8], bf0[8], kCospi12[56], bf0[9], cosBit);
    step[9]  = halfBtf(kCospi12[56], bf0[8], -kCospi12[8], bf0[9], cosBit);
    step[10] = halfBtf(kCospi12[40], bf0[10], kCospi12[24], bf0[11], cosBit);
    step[11] = halfBtf(kCospi12[24], bf0[10], -kCospi12[40], bf0[11], cosBit);
    step[12] = halfBtf(-kCospi12[56], bf0[12], kCospi12[8], bf0[13], cosBit);
    step[13] = halfBtf(kCospi12[8], bf0[12], kCospi12[56], bf0[13], cosBit);
    step[14] = halfBtf(-kCospi12[24], bf0[14], kCospi12[40], bf0[15], cosBit);
    step[15] = halfBtf(kCospi12[40], bf0[14], kCospi12[24], bf0[15], cosBit);

    // stage 5 (clamp at stage_range[5])
    bf0[0]  = clampValue(step[0] + step[4], stageRange[5]);
    bf0[1]  = clampValue(step[1] + step[5], stageRange[5]);
    bf0[2]  = clampValue(step[2] + step[6], stageRange[5]);
    bf0[3]  = clampValue(step[3] + step[7], stageRange[5]);
    bf0[4]  = clampValue(step[0] - step[4], stageRange[5]);
    bf0[5]  = clampValue(step[1] - step[5], stageRange[5]);
    bf0[6]  = clampValue(step[2] - step[6], stageRange[5]);
    bf0[7]  = clampValue(step[3] - step[7], stageRange[5]);
    bf0[8]  = clampValue(step[8] + step[12], stageRange[5]);
    bf0[9]  = clampValue(step[9] + step[13], stageRange[5]);
    bf0[10] = clampValue(step[10] + step[14], stageRange[5]);
    bf0[11] = clampValue(step[11] + step[15], stageRange[5]);
    bf0[12] = clampValue(step[8] - step[12], stageRange[5]);
    bf0[13] = clampValue(step[9] - step[13], stageRange[5]);
    bf0[14] = clampValue(step[10] - step[14], stageRange[5]);
    bf0[15] = clampValue(step[11] - step[15], stageRange[5]);

    // stage 6 (half_btf on 4-7 and 12-15; no clamp)
    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = bf0[2];
    step[3]  = bf0[3];
    step[4]  = halfBtf(kCospi12[16], bf0[4], kCospi12[48], bf0[5], cosBit);
    step[5]  = halfBtf(kCospi12[48], bf0[4], -kCospi12[16], bf0[5], cosBit);
    step[6]  = halfBtf(-kCospi12[48], bf0[6], kCospi12[16], bf0[7], cosBit);
    step[7]  = halfBtf(kCospi12[16], bf0[6], kCospi12[48], bf0[7], cosBit);
    step[8]  = bf0[8];
    step[9]  = bf0[9];
    step[10] = bf0[10];
    step[11] = bf0[11];
    step[12] = halfBtf(kCospi12[16], bf0[12], kCospi12[48], bf0[13], cosBit);
    step[13] = halfBtf(kCospi12[48], bf0[12], -kCospi12[16], bf0[13], cosBit);
    step[14] = halfBtf(-kCospi12[48], bf0[14], kCospi12[16], bf0[15], cosBit);
    step[15] = halfBtf(kCospi12[16], bf0[14], kCospi12[48], bf0[15], cosBit);

    // stage 7 (clamp at stage_range[7])
    bf0[0]  = clampValue(step[0] + step[2], stageRange[7]);
    bf0[1]  = clampValue(step[1] + step[3], stageRange[7]);
    bf0[2]  = clampValue(step[0] - step[2], stageRange[7]);
    bf0[3]  = clampValue(step[1] - step[3], stageRange[7]);
    bf0[4]  = clampValue(step[4] + step[6], stageRange[7]);
    bf0[5]  = clampValue(step[5] + step[7], stageRange[7]);
    bf0[6]  = clampValue(step[4] - step[6], stageRange[7]);
    bf0[7]  = clampValue(step[5] - step[7], stageRange[7]);
    bf0[8]  = clampValue(step[8] + step[10], stageRange[7]);
    bf0[9]  = clampValue(step[9] + step[11], stageRange[7]);
    bf0[10] = clampValue(step[8] - step[10], stageRange[7]);
    bf0[11] = clampValue(step[9] - step[11], stageRange[7]);
    bf0[12] = clampValue(step[12] + step[14], stageRange[7]);
    bf0[13] = clampValue(step[13] + step[15], stageRange[7]);
    bf0[14] = clampValue(step[12] - step[14], stageRange[7]);
    bf0[15] = clampValue(step[13] - step[15], stageRange[7]);

    // stage 8 (half_btf cospi[32] on pairs (2,3),(6,7),(10,11),(14,15); no
    // clamp; stage counter intentionally not advanced in SVT)
    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = halfBtf(kCospi12[32], bf0[2], kCospi12[32], bf0[3], cosBit);
    step[3]  = halfBtf(kCospi12[32], bf0[2], -kCospi12[32], bf0[3], cosBit);
    step[4]  = bf0[4];
    step[5]  = bf0[5];
    step[6]  = halfBtf(kCospi12[32], bf0[6], kCospi12[32], bf0[7], cosBit);
    step[7]  = halfBtf(kCospi12[32], bf0[6], -kCospi12[32], bf0[7], cosBit);
    step[8]  = bf0[8];
    step[9]  = bf0[9];
    step[10] = halfBtf(kCospi12[32], bf0[10], kCospi12[32], bf0[11], cosBit);
    step[11] = halfBtf(kCospi12[32], bf0[10], -kCospi12[32], bf0[11], cosBit);
    step[12] = bf0[12];
    step[13] = bf0[13];
    step[14] = halfBtf(kCospi12[32], bf0[14], kCospi12[32], bf0[15], cosBit);
    step[15] = halfBtf(kCospi12[32], bf0[14], -kCospi12[32], bf0[15], cosBit);

    // stage 9 (final permutation; no clamp)
    output[0]  = step[0];
    output[1]  = -step[8];
    output[2]  = step[12];
    output[3]  = -step[4];
    output[4]  = step[6];
    output[5]  = -step[14];
    output[6]  = step[10];
    output[7]  = -step[2];
    output[8]  = step[3];
    output[9]  = -step[11];
    output[10] = step[15];
    output[11] = -step[7];
    output[12] = step[5];
    output[13] = -step[13];
    output[14] = step[9];
    output[15] = -step[1];
}

// svt_av1_fdct8_new (transforms.c:196), cos_bit = 13
void fdct8(const std::int32_t input[8], std::int32_t output[8]) {
    const int8_t cosBit = 13;
    std::int32_t bf0[8];
    std::int32_t step[8];

    bf0[0] = input[0] + input[7];
    bf0[1] = input[1] + input[6];
    bf0[2] = input[2] + input[5];
    bf0[3] = input[3] + input[4];
    bf0[4] = -input[4] + input[3];
    bf0[5] = -input[5] + input[2];
    bf0[6] = -input[6] + input[1];
    bf0[7] = -input[7] + input[0];

    step[0] = bf0[0] + bf0[3];
    step[1] = bf0[1] + bf0[2];
    step[2] = -bf0[2] + bf0[1];
    step[3] = -bf0[3] + bf0[0];
    step[4] = bf0[4];
    step[5] = halfBtf(-kCospi13[32], bf0[5], kCospi13[32], bf0[6], cosBit);
    step[6] = halfBtf(kCospi13[32], bf0[6], kCospi13[32], bf0[5], cosBit);
    step[7] = bf0[7];

    bf0[0] = step[0];
    bf0[1] = step[1];
    bf0[2] = step[2];
    bf0[3] = step[3];
    bf0[4] = step[4];
    bf0[5] = step[5];
    bf0[6] = step[6];
    bf0[7] = step[7];

    step[0] = halfBtf(kCospi13[32], bf0[0], kCospi13[32], bf0[1], cosBit);
    step[1] = halfBtf(-kCospi13[32], bf0[1], kCospi13[32], bf0[0], cosBit);
    step[2] = halfBtf(kCospi13[48], bf0[2], kCospi13[16], bf0[3], cosBit);
    step[3] = halfBtf(kCospi13[48], bf0[3], -kCospi13[16], bf0[2], cosBit);
    step[4] = bf0[4] + bf0[5];
    step[5] = -bf0[5] + bf0[4];
    step[6] = -bf0[6] + bf0[7];
    step[7] = bf0[7] + bf0[6];

    bf0[0] = step[0];
    bf0[1] = step[1];
    bf0[2] = step[2];
    bf0[3] = step[3];
    bf0[4] = step[4];
    bf0[5] = step[5];
    bf0[6] = step[6];
    bf0[7] = step[7];

    step[0] = bf0[0];
    step[1] = bf0[1];
    step[2] = bf0[2];
    step[3] = bf0[3];
    step[4] = halfBtf(kCospi13[56], bf0[4], kCospi13[8], bf0[7], cosBit);
    step[5] = halfBtf(kCospi13[24], bf0[5], kCospi13[40], bf0[6], cosBit);
    step[6] = halfBtf(kCospi13[24], bf0[6], -kCospi13[40], bf0[5], cosBit);
    step[7] = halfBtf(kCospi13[56], bf0[7], -kCospi13[8], bf0[4], cosBit);

    output[0] = step[0];
    output[1] = step[4];
    output[2] = step[2];
    output[3] = step[6];
    output[4] = step[1];
    output[5] = step[5];
    output[6] = step[3];
    output[7] = step[7];
}

// svt_av1_fadst8_new (transforms.c:1617), cos_bit = 13
void fadst8(const std::int32_t input[8], std::int32_t output[8]) {
    const int8_t cosBit = 13;
    std::int32_t bf0[8];
    std::int32_t step[8];

    bf0[0] = input[0];
    bf0[1] = -input[7];
    bf0[2] = -input[3];
    bf0[3] = input[4];
    bf0[4] = -input[1];
    bf0[5] = input[6];
    bf0[6] = input[2];
    bf0[7] = -input[5];

    step[0] = bf0[0];
    step[1] = bf0[1];
    step[2] = halfBtf(kCospi13[32], bf0[2], kCospi13[32], bf0[3], cosBit);
    step[3] = halfBtf(kCospi13[32], bf0[2], -kCospi13[32], bf0[3], cosBit);
    step[4] = bf0[4];
    step[5] = bf0[5];
    step[6] = halfBtf(kCospi13[32], bf0[6], kCospi13[32], bf0[7], cosBit);
    step[7] = halfBtf(kCospi13[32], bf0[6], -kCospi13[32], bf0[7], cosBit);

    bf0[0] = step[0] + step[2];
    bf0[1] = step[1] + step[3];
    bf0[2] = step[0] - step[2];
    bf0[3] = step[1] - step[3];
    bf0[4] = step[4] + step[6];
    bf0[5] = step[5] + step[7];
    bf0[6] = step[4] - step[6];
    bf0[7] = step[5] - step[7];

    step[0] = bf0[0];
    step[1] = bf0[1];
    step[2] = bf0[2];
    step[3] = bf0[3];
    step[4] = halfBtf(kCospi13[16], bf0[4], kCospi13[48], bf0[5], cosBit);
    step[5] = halfBtf(kCospi13[48], bf0[4], -kCospi13[16], bf0[5], cosBit);
    step[6] = halfBtf(-kCospi13[48], bf0[6], kCospi13[16], bf0[7], cosBit);
    step[7] = halfBtf(kCospi13[16], bf0[6], kCospi13[48], bf0[7], cosBit);

    bf0[0] = step[0] + step[4];
    bf0[1] = step[1] + step[5];
    bf0[2] = step[2] + step[6];
    bf0[3] = step[3] + step[7];
    bf0[4] = step[0] - step[4];
    bf0[5] = step[1] - step[5];
    bf0[6] = step[2] - step[6];
    bf0[7] = step[3] - step[7];

    step[0] = halfBtf(kCospi13[4], bf0[0], kCospi13[60], bf0[1], cosBit);
    step[1] = halfBtf(kCospi13[60], bf0[0], -kCospi13[4], bf0[1], cosBit);
    step[2] = halfBtf(kCospi13[20], bf0[2], kCospi13[44], bf0[3], cosBit);
    step[3] = halfBtf(kCospi13[44], bf0[2], -kCospi13[20], bf0[3], cosBit);
    step[4] = halfBtf(kCospi13[36], bf0[4], kCospi13[28], bf0[5], cosBit);
    step[5] = halfBtf(kCospi13[28], bf0[4], -kCospi13[36], bf0[5], cosBit);
    step[6] = halfBtf(kCospi13[52], bf0[6], kCospi13[12], bf0[7], cosBit);
    step[7] = halfBtf(kCospi13[12], bf0[6], -kCospi13[52], bf0[7], cosBit);

    output[0] = step[1];
    output[1] = step[6];
    output[2] = step[3];
    output[3] = step[4];
    output[4] = step[5];
    output[5] = step[2];
    output[6] = step[7];
    output[7] = step[0];
}

// cospi_arr(cos_bit) (inv_transforms.h): per-bit cospi table row. The host
// keeps the bit-13 (fwd) and bit-12 (INV_COS_BIT) rows.
const std::int32_t* cospiRow(int cosBit) {
    return cosBit == 13 ? kCospi13 : kCospi12;
}

// svt_av1_fdct16_new (transforms.c:268-420), cos_bit parameter (13 for the
// TX_16X16 col pass, 12 for the row pass per fwd_cos_bit_col/row[2][2])
void fdct16B(const std::int32_t input[16], std::int32_t output[16], int cosBit) {
    const std::int32_t* cospi = cospiRow(cosBit);
    std::int32_t bf0[16];
    std::int32_t step[16];

    // stage 1
    bf0[0]  = input[0] + input[15];
    bf0[1]  = input[1] + input[14];
    bf0[2]  = input[2] + input[13];
    bf0[3]  = input[3] + input[12];
    bf0[4]  = input[4] + input[11];
    bf0[5]  = input[5] + input[10];
    bf0[6]  = input[6] + input[9];
    bf0[7]  = input[7] + input[8];
    bf0[8]  = -input[8] + input[7];
    bf0[9]  = -input[9] + input[6];
    bf0[10] = -input[10] + input[5];
    bf0[11] = -input[11] + input[4];
    bf0[12] = -input[12] + input[3];
    bf0[13] = -input[13] + input[2];
    bf0[14] = -input[14] + input[1];
    bf0[15] = -input[15] + input[0];

    // stage 2
    step[0]  = bf0[0] + bf0[7];
    step[1]  = bf0[1] + bf0[6];
    step[2]  = bf0[2] + bf0[5];
    step[3]  = bf0[3] + bf0[4];
    step[4]  = -bf0[4] + bf0[3];
    step[5]  = -bf0[5] + bf0[2];
    step[6]  = -bf0[6] + bf0[1];
    step[7]  = -bf0[7] + bf0[0];
    step[8]  = bf0[8];
    step[9]  = bf0[9];
    step[10] = halfBtf(-cospi[32], bf0[10], cospi[32], bf0[13], cosBit);
    step[11] = halfBtf(-cospi[32], bf0[11], cospi[32], bf0[12], cosBit);
    step[12] = halfBtf(cospi[32], bf0[12], cospi[32], bf0[11], cosBit);
    step[13] = halfBtf(cospi[32], bf0[13], cospi[32], bf0[10], cosBit);
    step[14] = bf0[14];
    step[15] = bf0[15];

    // stage 3
    bf0[0]  = step[0] + step[3];
    bf0[1]  = step[1] + step[2];
    bf0[2]  = -step[2] + step[1];
    bf0[3]  = -step[3] + step[0];
    bf0[4]  = step[4];
    bf0[5]  = halfBtf(-cospi[32], step[5], cospi[32], step[6], cosBit);
    bf0[6]  = halfBtf(cospi[32], step[6], cospi[32], step[5], cosBit);
    bf0[7]  = step[7];
    bf0[8]  = step[8] + step[11];
    bf0[9]  = step[9] + step[10];
    bf0[10] = -step[10] + step[9];
    bf0[11] = -step[11] + step[8];
    bf0[12] = -step[12] + step[15];
    bf0[13] = -step[13] + step[14];
    bf0[14] = step[14] + step[13];
    bf0[15] = step[15] + step[12];

    // stage 4
    step[0]  = halfBtf(cospi[32], bf0[0], cospi[32], bf0[1], cosBit);
    step[1]  = halfBtf(-cospi[32], bf0[1], cospi[32], bf0[0], cosBit);
    step[2]  = halfBtf(cospi[48], bf0[2], cospi[16], bf0[3], cosBit);
    step[3]  = halfBtf(cospi[48], bf0[3], -cospi[16], bf0[2], cosBit);
    step[4]  = bf0[4] + bf0[5];
    step[5]  = -bf0[5] + bf0[4];
    step[6]  = -bf0[6] + bf0[7];
    step[7]  = bf0[7] + bf0[6];
    step[8]  = bf0[8];
    step[9]  = halfBtf(-cospi[16], bf0[9], cospi[48], bf0[14], cosBit);
    step[10] = halfBtf(-cospi[48], bf0[10], -cospi[16], bf0[13], cosBit);
    step[11] = bf0[11];
    step[12] = bf0[12];
    step[13] = halfBtf(cospi[48], bf0[13], -cospi[16], bf0[10], cosBit);
    step[14] = halfBtf(cospi[16], bf0[14], cospi[48], bf0[9], cosBit);
    step[15] = bf0[15];

    // stage 5
    bf0[0]  = step[0];
    bf0[1]  = step[1];
    bf0[2]  = step[2];
    bf0[3]  = step[3];
    bf0[4]  = halfBtf(cospi[56], step[4], cospi[8], step[7], cosBit);
    bf0[5]  = halfBtf(cospi[24], step[5], cospi[40], step[6], cosBit);
    bf0[6]  = halfBtf(cospi[24], step[6], -cospi[40], step[5], cosBit);
    bf0[7]  = halfBtf(cospi[56], step[7], -cospi[8], step[4], cosBit);
    bf0[8]  = step[8] + step[9];
    bf0[9]  = -step[9] + step[8];
    bf0[10] = -step[10] + step[11];
    bf0[11] = step[11] + step[10];
    bf0[12] = step[12] + step[13];
    bf0[13] = -step[13] + step[12];
    bf0[14] = -step[14] + step[15];
    bf0[15] = step[15] + step[14];

    // stage 6
    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = bf0[2];
    step[3]  = bf0[3];
    step[4]  = bf0[4];
    step[5]  = bf0[5];
    step[6]  = bf0[6];
    step[7]  = bf0[7];
    step[8]  = halfBtf(cospi[60], bf0[8], cospi[4], bf0[15], cosBit);
    step[9]  = halfBtf(cospi[28], bf0[9], cospi[36], bf0[14], cosBit);
    step[10] = halfBtf(cospi[44], bf0[10], cospi[20], bf0[13], cosBit);
    step[11] = halfBtf(cospi[12], bf0[11], cospi[52], bf0[12], cosBit);
    step[12] = halfBtf(cospi[12], bf0[12], -cospi[52], bf0[11], cosBit);
    step[13] = halfBtf(cospi[44], bf0[13], -cospi[20], bf0[10], cosBit);
    step[14] = halfBtf(cospi[28], bf0[14], -cospi[36], bf0[9], cosBit);
    step[15] = halfBtf(cospi[60], bf0[15], -cospi[4], bf0[8], cosBit);

    // stage 7
    output[0]  = step[0];
    output[1]  = step[8];
    output[2]  = step[4];
    output[3]  = step[12];
    output[4]  = step[2];
    output[5]  = step[10];
    output[6]  = step[6];
    output[7]  = step[14];
    output[8]  = step[1];
    output[9]  = step[9];
    output[10] = step[5];
    output[11] = step[13];
    output[12] = step[3];
    output[13] = step[11];
    output[14] = step[7];
    output[15] = step[15];
}

// svt_av1_fadst16_new (transforms.c:1714-1906), cos_bit parameter
void fadst16B(const std::int32_t input[16], std::int32_t output[16], int cosBit) {
    const std::int32_t* cospi = cospiRow(cosBit);
    std::int32_t bf0[16];
    std::int32_t step[16];

    // stage 1
    bf0[0]  = input[0];
    bf0[1]  = -input[15];
    bf0[2]  = -input[7];
    bf0[3]  = input[8];
    bf0[4]  = -input[3];
    bf0[5]  = input[12];
    bf0[6]  = input[4];
    bf0[7]  = -input[11];
    bf0[8]  = -input[1];
    bf0[9]  = input[14];
    bf0[10] = input[6];
    bf0[11] = -input[9];
    bf0[12] = input[2];
    bf0[13] = -input[13];
    bf0[14] = -input[5];
    bf0[15] = input[10];

    // stage 2
    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = halfBtf(cospi[32], bf0[2], cospi[32], bf0[3], cosBit);
    step[3]  = halfBtf(cospi[32], bf0[2], -cospi[32], bf0[3], cosBit);
    step[4]  = bf0[4];
    step[5]  = bf0[5];
    step[6]  = halfBtf(cospi[32], bf0[6], cospi[32], bf0[7], cosBit);
    step[7]  = halfBtf(cospi[32], bf0[6], -cospi[32], bf0[7], cosBit);
    step[8]  = bf0[8];
    step[9]  = bf0[9];
    step[10] = halfBtf(cospi[32], bf0[10], cospi[32], bf0[11], cosBit);
    step[11] = halfBtf(cospi[32], bf0[10], -cospi[32], bf0[11], cosBit);
    step[12] = bf0[12];
    step[13] = bf0[13];
    step[14] = halfBtf(cospi[32], bf0[14], cospi[32], bf0[15], cosBit);
    step[15] = halfBtf(cospi[32], bf0[14], -cospi[32], bf0[15], cosBit);

    // stage 3
    bf0[0]  = step[0] + step[2];
    bf0[1]  = step[1] + step[3];
    bf0[2]  = step[0] - step[2];
    bf0[3]  = step[1] - step[3];
    bf0[4]  = step[4] + step[6];
    bf0[5]  = step[5] + step[7];
    bf0[6]  = step[4] - step[6];
    bf0[7]  = step[5] - step[7];
    bf0[8]  = step[8] + step[10];
    bf0[9]  = step[9] + step[11];
    bf0[10] = step[8] - step[10];
    bf0[11] = step[9] - step[11];
    bf0[12] = step[12] + step[14];
    bf0[13] = step[13] + step[15];
    bf0[14] = step[12] - step[14];
    bf0[15] = step[13] - step[15];

    // stage 4
    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = bf0[2];
    step[3]  = bf0[3];
    step[4]  = halfBtf(cospi[16], bf0[4], cospi[48], bf0[5], cosBit);
    step[5]  = halfBtf(cospi[48], bf0[4], -cospi[16], bf0[5], cosBit);
    step[6]  = halfBtf(-cospi[48], bf0[6], cospi[16], bf0[7], cosBit);
    step[7]  = halfBtf(cospi[16], bf0[6], cospi[48], bf0[7], cosBit);
    step[8]  = bf0[8];
    step[9]  = bf0[9];
    step[10] = bf0[10];
    step[11] = bf0[11];
    step[12] = halfBtf(cospi[16], bf0[12], cospi[48], bf0[13], cosBit);
    step[13] = halfBtf(cospi[48], bf0[12], -cospi[16], bf0[13], cosBit);
    step[14] = halfBtf(-cospi[48], bf0[14], cospi[16], bf0[15], cosBit);
    step[15] = halfBtf(cospi[16], bf0[14], cospi[48], bf0[15], cosBit);

    // stage 5
    bf0[0]  = step[0] + step[4];
    bf0[1]  = step[1] + step[5];
    bf0[2]  = step[2] + step[6];
    bf0[3]  = step[3] + step[7];
    bf0[4]  = step[0] - step[4];
    bf0[5]  = step[1] - step[5];
    bf0[6]  = step[2] - step[6];
    bf0[7]  = step[3] - step[7];
    bf0[8]  = step[8] + step[12];
    bf0[9]  = step[9] + step[13];
    bf0[10] = step[10] + step[14];
    bf0[11] = step[11] + step[15];
    bf0[12] = step[8] - step[12];
    bf0[13] = step[9] - step[13];
    bf0[14] = step[10] - step[14];
    bf0[15] = step[11] - step[15];

    // stage 6
    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = bf0[2];
    step[3]  = bf0[3];
    step[4]  = bf0[4];
    step[5]  = bf0[5];
    step[6]  = bf0[6];
    step[7]  = bf0[7];
    step[8]  = halfBtf(cospi[8], bf0[8], cospi[56], bf0[9], cosBit);
    step[9]  = halfBtf(cospi[56], bf0[8], -cospi[8], bf0[9], cosBit);
    step[10] = halfBtf(cospi[40], bf0[10], cospi[24], bf0[11], cosBit);
    step[11] = halfBtf(cospi[24], bf0[10], -cospi[40], bf0[11], cosBit);
    step[12] = halfBtf(-cospi[56], bf0[12], cospi[8], bf0[13], cosBit);
    step[13] = halfBtf(cospi[8], bf0[12], cospi[56], bf0[13], cosBit);
    step[14] = halfBtf(-cospi[24], bf0[14], cospi[40], bf0[15], cosBit);
    step[15] = halfBtf(cospi[40], bf0[14], cospi[24], bf0[15], cosBit);

    // stage 7
    bf0[0]  = step[0] + step[8];
    bf0[1]  = step[1] + step[9];
    bf0[2]  = step[2] + step[10];
    bf0[3]  = step[3] + step[11];
    bf0[4]  = step[4] + step[12];
    bf0[5]  = step[5] + step[13];
    bf0[6]  = step[6] + step[14];
    bf0[7]  = step[7] + step[15];
    bf0[8]  = step[0] - step[8];
    bf0[9]  = step[1] - step[9];
    bf0[10] = step[2] - step[10];
    bf0[11] = step[3] - step[11];
    bf0[12] = step[4] - step[12];
    bf0[13] = step[5] - step[13];
    bf0[14] = step[6] - step[14];
    bf0[15] = step[7] - step[15];

    // stage 8
    step[0]  = halfBtf(cospi[2], bf0[0], cospi[62], bf0[1], cosBit);
    step[1]  = halfBtf(cospi[62], bf0[0], -cospi[2], bf0[1], cosBit);
    step[2]  = halfBtf(cospi[10], bf0[2], cospi[54], bf0[3], cosBit);
    step[3]  = halfBtf(cospi[54], bf0[2], -cospi[10], bf0[3], cosBit);
    step[4]  = halfBtf(cospi[18], bf0[4], cospi[46], bf0[5], cosBit);
    step[5]  = halfBtf(cospi[46], bf0[4], -cospi[18], bf0[5], cosBit);
    step[6]  = halfBtf(cospi[26], bf0[6], cospi[38], bf0[7], cosBit);
    step[7]  = halfBtf(cospi[38], bf0[6], -cospi[26], bf0[7], cosBit);
    step[8]  = halfBtf(cospi[34], bf0[8], cospi[30], bf0[9], cosBit);
    step[9]  = halfBtf(cospi[30], bf0[8], -cospi[34], bf0[9], cosBit);
    step[10] = halfBtf(cospi[42], bf0[10], cospi[22], bf0[11], cosBit);
    step[11] = halfBtf(cospi[22], bf0[10], -cospi[42], bf0[11], cosBit);
    step[12] = halfBtf(cospi[50], bf0[12], cospi[14], bf0[13], cosBit);
    step[13] = halfBtf(cospi[14], bf0[12], -cospi[50], bf0[13], cosBit);
    step[14] = halfBtf(cospi[58], bf0[14], cospi[6], bf0[15], cosBit);
    step[15] = halfBtf(cospi[6], bf0[14], -cospi[58], bf0[15], cosBit);

    // stage 9
    output[0]  = step[1];
    output[1]  = step[14];
    output[2]  = step[3];
    output[3]  = step[12];
    output[4]  = step[5];
    output[5]  = step[10];
    output[6]  = step[7];
    output[7]  = step[8];
    output[8]  = step[9];
    output[9]  = step[6];
    output[10] = step[11];
    output[11] = step[4];
    output[12] = step[13];
    output[13] = step[2];
    output[14] = step[15];
    output[15] = step[0];
}

void fdct16(const std::int32_t input[16], std::int32_t output[16]) {
    fdct16B(input, output, 13);
}

void fadst16(const std::int32_t input[16], std::int32_t output[16]) {
    fadst16B(input, output, 13);
}

// svt_av1_fdct4_new (transforms.c), cos_bit = 13
void fdct4(const std::int32_t input[4], std::int32_t output[4]) {
    const int8_t cosBit = 13;
    std::int32_t bf0[4];
    std::int32_t bf1[4];
    std::int32_t step[4];

    bf1[0] = input[0] + input[3];
    bf1[1] = input[1] + input[2];
    bf1[2] = -input[2] + input[1];
    bf1[3] = -input[3] + input[0];

    for (int i = 0; i < 4; ++i) {
        bf0[i] = bf1[i];
    }
    step[0] = halfBtf(kCospi13[32], bf0[0], kCospi13[32], bf0[1], cosBit);
    step[1] = halfBtf(-kCospi13[32], bf0[1], kCospi13[32], bf0[0], cosBit);
    step[2] = halfBtf(kCospi13[48], bf0[2], kCospi13[16], bf0[3], cosBit);
    step[3] = halfBtf(kCospi13[48], bf0[3], -kCospi13[16], bf0[2], cosBit);

    output[0] = step[0];
    output[1] = step[2];
    output[2] = step[1];
    output[3] = step[3];
}

// svt_av1_fadst4_new (transforms.c), cos_bit = 13 (incl. all-zero early-out)
void fadst4(const std::int32_t input[4], std::int32_t output[4]) {
    const int bit   = 13;
    const std::int32_t x0 = input[0];
    const std::int32_t x1 = input[1];
    const std::int32_t x2 = input[2];
    const std::int32_t x3 = input[3];

    if (!(x0 | x1 | x2 | x3)) {
        output[0] = output[1] = output[2] = output[3] = 0;
        return;
    }

    std::int32_t s0, s1, s2, s3, s4, s5, s6, s7;

    s0 = kSinpi13[1] * x0;
    s1 = kSinpi13[4] * x0;
    s2 = kSinpi13[2] * x1;
    s3 = kSinpi13[1] * x1;
    s4 = kSinpi13[3] * x2;
    s5 = kSinpi13[4] * x3;
    s6 = kSinpi13[2] * x3;
    s7 = x0 + x1;

    s7 = s7 - x3;

    std::int32_t y0 = s0 + s2;
    std::int32_t y1 = kSinpi13[3] * s7;
    std::int32_t y2 = s1 - s3;
    std::int32_t y3 = s4;

    y0 = y0 + s5;
    y2 = y2 + s6;

    s0 = y0 + y3;
    s1 = y1;
    s2 = y2 - y3;
    s3 = y2 - y0;

    s3 = s3 + y3;

    output[0] = roundShift(s0, bit);
    output[1] = roundShift(s1, bit);
    output[2] = roundShift(s2, bit);
    output[3] = roundShift(s3, bit);
}

namespace {

using TxfmFn = void (*)(const std::int32_t*, std::int32_t*);

TxfmFn fwd1d4(TxType type) {
    return type == TxType::DCT_DCT ? fdct4 : fadst4;
}

TxfmFn fwd1d8(TxType type) {
    return type == TxType::DCT_DCT ? fdct8 : fadst8;
}

using TxfmFnB = void (*)(const std::int32_t*, std::int32_t*, int);

TxfmFnB fwd1d16B(TxType type) {
    return type == TxType::DCT_DCT ? fdct16B : fadst16B;
}

}  // namespace

// svt_av1_transform_two_d_4x4_c / av1_tranform_two_d_core_c, TX_4X4 config:
// shift {2, 0, 0}, cos_bit 13/13, no flips (DCT_DCT / ADST_ADST both ud/lr=0)
void fwdTxfm2d4x4(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    TxfmFn txfm = fwd1d4(type);
    std::int32_t buf[4 * 4];
    std::int32_t tempIn[4];
    std::int32_t tempOut[4];

    for (std::uint32_t c = 0; c < 4; ++c) {
        for (std::uint32_t r = 0; r < 4; ++r) {
            tempIn[r] = input[r * stride + c];
        }
        // round_shift_array(..., -shift[0]) with shift[0] = 2 -> x4
        for (std::uint32_t i = 0; i < 4; ++i) {
            tempIn[i] *= (1 << 2);
        }
        txfm(tempIn, tempOut);
        for (std::uint32_t r = 0; r < 4; ++r) {
            buf[r * 4 + c] = tempOut[r];
        }
    }

    for (std::uint32_t r = 0; r < 4; ++r) {
        txfm(buf + r * 4, output + r * 4);
    }
}

// av1_tranform_two_d_core_c (transforms.c:2398) at TX_8X8: fwd_shift_8x8 =
// {2, -1, 0} (transforms.c:123), cos_bit 13/13 from fwd_cos_bit_col/row[1][1]
void fwdTxfm2d8x8(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    TxfmFn txfm = fwd1d8(type);
    std::int32_t buf[8 * 8];
    std::int32_t tempIn[8];
    std::int32_t tempOut[8];

    for (std::uint32_t c = 0; c < 8; ++c) {
        for (std::uint32_t r = 0; r < 8; ++r) {
            tempIn[r] = input[r * stride + c];
        }
        // round_shift_array(..., -shift[0]) with shift[0] = 2 -> x4
        for (std::uint32_t i = 0; i < 8; ++i) {
            tempIn[i] *= (1 << 2);
        }
        txfm(tempIn, tempOut);
        // round_shift_array(..., -shift[1]) with shift[1] = -1 -> >>1 rounding
        for (std::uint32_t i = 0; i < 8; ++i) {
            tempOut[i] = roundShift(tempOut[i], 1);
        }
        for (std::uint32_t r = 0; r < 8; ++r) {
            buf[r * 8 + c] = tempOut[r];
        }
    }

    for (std::uint32_t r = 0; r < 8; ++r) {
        txfm(buf + r * 8, output + r * 8);
        // round_shift_array(..., -shift[2]) with shift[2] = 0 -> no-op
    }
}

// av1_tranform_two_d_core_c (transforms.c:2398) at TX_16X16: fwd_shift_16x16
// = {2, -2, 0} (transforms.c:124), cos_bit col 13 / row 12 from
// fwd_cos_bit_col/row[2][2] (transforms.c:19-22)
void fwdTxfm2d16x16(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    TxfmFnB txfm = fwd1d16B(type);
    std::int32_t buf[16 * 16];
    std::int32_t tempIn[16];
    std::int32_t tempOut[16];

    for (std::uint32_t c = 0; c < 16; ++c) {
        for (std::uint32_t r = 0; r < 16; ++r) {
            tempIn[r] = input[r * stride + c];
        }
        // round_shift_array(..., -shift[0]) with shift[0] = 2 -> x4
        for (std::uint32_t i = 0; i < 16; ++i) {
            tempIn[i] *= (1 << 2);
        }
        txfm(tempIn, tempOut, 13);
        // round_shift_array(..., -shift[1]) with shift[1] = -2 -> >>2 rounding
        for (std::uint32_t i = 0; i < 16; ++i) {
            tempOut[i] = roundShift(tempOut[i], 2);
        }
        for (std::uint32_t r = 0; r < 16; ++r) {
            buf[r * 16 + c] = tempOut[r];
        }
    }

    for (std::uint32_t r = 0; r < 16; ++r) {
        txfm(buf + r * 16, output + r * 16, 12);
        // round_shift_array(..., -shift[2]) with shift[2] = 0 -> no-op
    }
}

namespace {

using InvTxfmFn = void (*)(const std::int32_t*, std::int32_t*);

InvTxfmFn inv1d4(TxType type) {
    return type == TxType::DCT_DCT ? idct4 : iadst4;
}

InvTxfmFn inv1d8(TxType type) {
    return type == TxType::DCT_DCT ? idct8 : iadst8;
}

InvTxfmFn inv1d16(TxType type) {
    return type == TxType::DCT_DCT ? idct16 : iadst16;
}

// svt_av1_round_shift_array_c (inv_transforms.c:2449)
void roundShiftArrayIv(std::int32_t* arr, int size, int bit) {
    if (bit == 0) {
        return;
    }
    if (bit > 0) {
        for (int i = 0; i < size; ++i) {
            arr[i] = roundShift(arr[i], bit);
        }
    } else {
        for (int i = 0; i < size; ++i) {
            arr[i] = arr[i] * (1 << (-bit));
        }
    }
}

void clipPixelAdd(std::uint8_t* dst, std::int32_t trans) {
    const int v = static_cast<int>(*dst) + trans;
    *dst = static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

}  // namespace

// svt_av1_inv_txfm2d_add_4x4_c / inv_txfm2d_add_c, TX_4X4: rows then columns,// inv_shift_4x4 = {0, -4}, cos_bit 12/12, no flips, clamp bit 16; add via
// clip_pixel_highbd(pred + round_shift(out, 4), 8)
void invTxfm2dAdd4x4(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    InvTxfmFn txfmRow = inv1d4(type);
    std::int32_t buf[4 * 4];
    std::int32_t tempIn[4];
    std::int32_t tempOut[4];

    for (std::uint32_t r = 0; r < 4; ++r) {
        for (std::uint32_t c = 0; c < 4; ++c) {
            tempIn[c] = coeffs[r * 4 + c];
        }
        clampBufIv(tempIn, 4, kInvClampBit);
        txfmRow(tempIn, buf + r * 4);
        roundShiftArrayIv(buf + r * 4, 4, 0);
    }

    for (std::uint32_t c = 0; c < 4; ++c) {
        for (std::uint32_t r = 0; r < 4; ++r) {
            tempIn[r] = buf[r * 4 + c];
        }
        clampBufIv(tempIn, 4, kInvClampBit);
        txfmRow(tempIn, tempOut);
        roundShiftArrayIv(tempOut, 4, 4);
        for (std::uint32_t r = 0; r < 4; ++r) {
            clipPixelAdd(dst + r * stride + c, tempOut[r]);
        }
    }
}

// svt_av1_inv_txfm2d_add_4x4_c / inv_txfm2d_add_c, TX_8X8: rows then columns,
// inv_shift_8x8 = {-1, -4}, cos_bit 12/12, no flips, clamp bits bd+8=16 and
// max(bd+6,16)=16; add via clip_pixel_highbd(pred + round_shift(out, 4), 8)
void invTxfm2dAdd8x8(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    InvTxfmFn txfmRow = inv1d8(type);
    std::int32_t buf[8 * 8];
    std::int32_t tempIn[8];
    std::int32_t tempOut[8];

    // rows: clamp 16, 1D, round_shift_array(-shift[0]) = +1 rounding >>1
    for (std::uint32_t r = 0; r < 8; ++r) {
        for (std::uint32_t c = 0; c < 8; ++c) {
            tempIn[c] = coeffs[r * 8 + c];
        }
        clampBufIv(tempIn, 8, kInvClampBit);
        txfmRow(tempIn, buf + r * 8);
        roundShiftArrayIv(buf + r * 8, 8, 1);
    }

    // columns: clamp 16, 1D, round_shift_array(-shift[1]) = +4, clip add
    for (std::uint32_t c = 0; c < 8; ++c) {
        for (std::uint32_t r = 0; r < 8; ++r) {
            tempIn[r] = buf[r * 8 + c];
        }
        clampBufIv(tempIn, 8, kInvClampBit);
        txfmRow(tempIn, tempOut);
        roundShiftArrayIv(tempOut, 8, 4);
        for (std::uint32_t r = 0; r < 8; ++r) {
            clipPixelAdd(dst + r * stride + c, tempOut[r]);
        }
    }
}

// svt_av1_inv_txfm2d_add_16x16_c / inv_txfm2d_add_c, TX_16X16: rows then
// columns, inv_shift_16x16 = {-2, -4}, cos_bit 12/12, no flips, clamp bits
// bd+8=16 and max(bd+6,16)=16; add via clip_pixel_highbd(pred +
// round_shift(out, 4), 8)
void invTxfm2dAdd16x16(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    InvTxfmFn txfmRow = inv1d16(type);
    std::int32_t buf[16 * 16];
    std::int32_t tempIn[16];
    std::int32_t tempOut[16];

    // rows: clamp 16, 1D, round_shift_array(-shift[0]) = +2 rounding >>2
    for (std::uint32_t r = 0; r < 16; ++r) {
        for (std::uint32_t c = 0; c < 16; ++c) {
            tempIn[c] = coeffs[r * 16 + c];
        }
        clampBufIv(tempIn, 16, kInvClampBit);
        txfmRow(tempIn, buf + r * 16);
        roundShiftArrayIv(buf + r * 16, 16, 2);
    }

    // columns: clamp 16, 1D, round_shift_array(-shift[1]) = +4, clip add
    for (std::uint32_t c = 0; c < 16; ++c) {
        for (std::uint32_t r = 0; r < 16; ++r) {
            tempIn[r] = buf[r * 16 + c];
        }
        clampBufIv(tempIn, 16, kInvClampBit);
        txfmRow(tempIn, tempOut);
        roundShiftArrayIv(tempOut, 16, 4);
        for (std::uint32_t r = 0; r < 16; ++r) {
            clipPixelAdd(dst + r * stride + c, tempOut[r]);
        }
    }
}

std::string fwdTxfmCuSource() {
    return R"CUDA(
__constant__ int kCospi[64] = {
    8192, 8190, 8182, 8170, 8153, 8130, 8103, 8071, 8035, 7993, 7946, 7895, 7839, 7779, 7713, 7643,
    7568, 7489, 7405, 7317, 7225, 7128, 7027, 6921, 6811, 6698, 6580, 6458, 6333, 6203, 6070, 5933,
    5793, 5649, 5501, 5351, 5197, 5040, 4880, 4717, 4551, 4383, 4212, 4038, 3862, 3683, 3503, 3320,
    3135, 2948, 2760, 2570, 2378, 2185, 1990, 1795, 1598, 1401, 1202, 1003, 803,  603,  402,  201,
};

__constant__ int k_Sinpi[5] = {0, 2642, 4964, 6689, 7606};

__device__ int d_half_btf(int w0, int in0, int w1, int in1, int bit) {
    long long result = (long long)(w0 * in0) + (long long)(w1 * in1);
    long long mid = result + (1LL << (bit - 1));
    return (int)(mid >> bit);
}

__device__ int d_round_shift(long long value, int bit) {
    return (int)((value + (1LL << (bit - 1))) >> bit);
}

__device__ void d_fdct4(const int* input, int* output) {
    const int bit = 13;
    int bf[4];
    int step[4];
    bf[0] = input[0] + input[3];
    bf[1] = input[1] + input[2];
    bf[2] = -input[2] + input[1];
    bf[3] = -input[3] + input[0];
    step[0] = d_half_btf(kCospi[32], bf[0], kCospi[32], bf[1], bit);
    step[1] = d_half_btf(-kCospi[32], bf[1], kCospi[32], bf[0], bit);
    step[2] = d_half_btf(kCospi[48], bf[2], kCospi[16], bf[3], bit);
    step[3] = d_half_btf(kCospi[48], bf[3], -kCospi[16], bf[2], bit);
    output[0] = step[0];
    output[1] = step[2];
    output[2] = step[1];
    output[3] = step[3];
}

__device__ void d_fadst4(const int* input, int* output) {
    const int bit = 13;
    int x0 = input[0];
    int x1 = input[1];
    int x2 = input[2];
    int x3 = input[3];
    if (!(x0 | x1 | x2 | x3)) {
        output[0] = output[1] = output[2] = output[3] = 0;
        return;
    }
    int s0 = k_Sinpi[1] * x0;
    int s1 = k_Sinpi[4] * x0;
    int s2 = k_Sinpi[2] * x1;
    int s3 = k_Sinpi[1] * x1;
    int s4 = k_Sinpi[3] * x2;
    int s5 = k_Sinpi[4] * x3;
    int s6 = k_Sinpi[2] * x3;
    int s7 = x0 + x1;
    s7 = s7 - x3;
    int y0 = s0 + s2;
    int y1 = k_Sinpi[3] * s7;
    int y2 = s1 - s3;
    int y3 = s4;
    y0 = y0 + s5;
    y2 = y2 + s6;
    s0 = y0 + y3;
    s1 = y1;
    s2 = y2 - y3;
    s3 = y2 - y0;
    s3 = s3 + y3;
    output[0] = d_round_shift(s0, bit);
    output[1] = d_round_shift(s1, bit);
    output[2] = d_round_shift(s2, bit);
    output[3] = d_round_shift(s3, bit);
}

extern "C" __global__ void fwd_txfm_2d_4x4(const short* input, const int* stride, const int* txType,
                                           int* output) {
    __shared__ int sbuf[16];
    const int t = threadIdx.x;
    int tmp[4];
    int o[4];
    for (int r = 0; r < 4; ++r) {
        tmp[r] = input[r * (*stride) + t] * 4;
    }
    if (*txType == 0) {
        d_fdct4(tmp, o);
    } else {
        d_fadst4(tmp, o);
    }
    for (int r = 0; r < 4; ++r) {
        sbuf[r * 4 + t] = o[r];
    }
    __syncthreads();
    for (int c = 0; c < 4; ++c) {
        tmp[c] = sbuf[t * 4 + c];
    }
    if (*txType == 0) {
        d_fdct4(tmp, o);
    } else {
        d_fadst4(tmp, o);
    }
    for (int c = 0; c < 4; ++c) {
        output[t * 4 + c] = o[c];
    }
}
)CUDA" R"CUDB1(
__device__ void d_fdct8(const int* input, int* output) {
    const int bit = 13;
    int bf0[8];
    int step[8];

    bf0[0] = input[0] + input[7];
    bf0[1] = input[1] + input[6];
    bf0[2] = input[2] + input[5];
    bf0[3] = input[3] + input[4];
    bf0[4] = -input[4] + input[3];
    bf0[5] = -input[5] + input[2];
    bf0[6] = -input[6] + input[1];
    bf0[7] = -input[7] + input[0];

    step[0] = bf0[0] + bf0[3];
    step[1] = bf0[1] + bf0[2];
    step[2] = -bf0[2] + bf0[1];
    step[3] = -bf0[3] + bf0[0];
    step[4] = bf0[4];
    step[5] = d_half_btf(-kCospi[32], bf0[5], kCospi[32], bf0[6], bit);
    step[6] = d_half_btf(kCospi[32], bf0[6], kCospi[32], bf0[5], bit);
    step[7] = bf0[7];

    bf0[0] = step[0];
    bf0[1] = step[1];
    bf0[2] = step[2];
    bf0[3] = step[3];
    bf0[4] = step[4];
    bf0[5] = step[5];
    bf0[6] = step[6];
    bf0[7] = step[7];

    step[0] = d_half_btf(kCospi[32], bf0[0], kCospi[32], bf0[1], bit);
    step[1] = d_half_btf(-kCospi[32], bf0[1], kCospi[32], bf0[0], bit);
    step[2] = d_half_btf(kCospi[48], bf0[2], kCospi[16], bf0[3], bit);
    step[3] = d_half_btf(kCospi[48], bf0[3], -kCospi[16], bf0[2], bit);
    step[4] = bf0[4] + bf0[5];
    step[5] = -bf0[5] + bf0[4];
    step[6] = -bf0[6] + bf0[7];
    step[7] = bf0[7] + bf0[6];

    bf0[0] = step[0];
    bf0[1] = step[1];
    bf0[2] = step[2];
    bf0[3] = step[3];
    bf0[4] = step[4];
    bf0[5] = step[5];
    bf0[6] = step[6];
    bf0[7] = step[7];

    step[0] = bf0[0];
    step[1] = bf0[1];
    step[2] = bf0[2];
    step[3] = bf0[3];
    step[4] = d_half_btf(kCospi[56], bf0[4], kCospi[8], bf0[7], bit);
    step[5] = d_half_btf(kCospi[24], bf0[5], kCospi[40], bf0[6], bit);
    step[6] = d_half_btf(kCospi[24], bf0[6], -kCospi[40], bf0[5], bit);
    step[7] = d_half_btf(kCospi[56], bf0[7], -kCospi[8], bf0[4], bit);

    output[0] = step[0];
    output[1] = step[4];
    output[2] = step[2];
    output[3] = step[6];
    output[4] = step[1];
    output[5] = step[5];
    output[6] = step[3];
    output[7] = step[7];
}

__device__ void d_fadst8(const int* input, int* output) {
    const int bit = 13;
    int bf0[8];
    int step[8];

    bf0[0] = input[0];
    bf0[1] = -input[7];
    bf0[2] = -input[3];
    bf0[3] = input[4];
    bf0[4] = -input[1];
    bf0[5] = input[6];
    bf0[6] = input[2];
    bf0[7] = -input[5];

    step[0] = bf0[0];
    step[1] = bf0[1];
    step[2] = d_half_btf(kCospi[32], bf0[2], kCospi[32], bf0[3], bit);
    step[3] = d_half_btf(kCospi[32], bf0[2], -kCospi[32], bf0[3], bit);
    step[4] = bf0[4];
    step[5] = bf0[5];
    step[6] = d_half_btf(kCospi[32], bf0[6], kCospi[32], bf0[7], bit);
    step[7] = d_half_btf(kCospi[32], bf0[6], -kCospi[32], bf0[7], bit);

    bf0[0] = step[0] + step[2];
    bf0[1] = step[1] + step[3];
    bf0[2] = step[0] - step[2];
    bf0[3] = step[1] - step[3];
    bf0[4] = step[4] + step[6];
    bf0[5] = step[5] + step[7];
    bf0[6] = step[4] - step[6];
    bf0[7] = step[5] - step[7];

    step[0] = bf0[0];
    step[1] = bf0[1];
    step[2] = bf0[2];
    step[3] = bf0[3];
    step[4] = d_half_btf(kCospi[16], bf0[4], kCospi[48], bf0[5], bit);
    step[5] = d_half_btf(kCospi[48], bf0[4], -kCospi[16], bf0[5], bit);
    step[6] = d_half_btf(-kCospi[48], bf0[6], kCospi[16], bf0[7], bit);
    step[7] = d_half_btf(kCospi[16], bf0[6], kCospi[48], bf0[7], bit);

    bf0[0] = step[0] + step[4];
    bf0[1] = step[1] + step[5];
    bf0[2] = step[2] + step[6];
    bf0[3] = step[3] + step[7];
    bf0[4] = step[0] - step[4];
    bf0[5] = step[1] - step[5];
    bf0[6] = step[2] - step[6];
    bf0[7] = step[3] - step[7];

    step[0] = d_half_btf(kCospi[4], bf0[0], kCospi[60], bf0[1], bit);
    step[1] = d_half_btf(kCospi[60], bf0[0], -kCospi[4], bf0[1], bit);
    step[2] = d_half_btf(kCospi[20], bf0[2], kCospi[44], bf0[3], bit);
    step[3] = d_half_btf(kCospi[44], bf0[2], -kCospi[20], bf0[3], bit);
    step[4] = d_half_btf(kCospi[36], bf0[4], kCospi[28], bf0[5], bit);
    step[5] = d_half_btf(kCospi[28], bf0[4], -kCospi[36], bf0[5], bit);
    step[6] = d_half_btf(kCospi[52], bf0[6], kCospi[12], bf0[7], bit);
    step[7] = d_half_btf(kCospi[12], bf0[6], -kCospi[52], bf0[7], bit);

    output[0] = step[1];
    output[1] = step[6];
    output[2] = step[3];
    output[3] = step[4];
    output[4] = step[5];
    output[5] = step[2];
    output[6] = step[7];
    output[7] = step[0];
}

// svt_av1_transform_two_d_core_c at TX_8X8: shift {2,-1,0}, cos_bit 13/13;
// rows then columns, rounding >>1 after the row pass (shift[1] = -1)
extern "C" __global__ void fwd_txfm_2d_8x8(const short* input, const int* stride, const int* txType,
                                           int* output) {
    __shared__ int sbuf[64];
    const int t = threadIdx.x;
    int tmp[8];
    int o[8];
    for (int r = 0; r < 8; ++r) {
        tmp[r] = input[r * (*stride) + t] * 4;
    }
    if (*txType == 0) {
        d_fdct8(tmp, o);
    } else {
        d_fadst8(tmp, o);
    }
    for (int r = 0; r < 8; ++r) {
        sbuf[r * 8 + t] = d_round_shift(o[r], 1);
    }
    __syncthreads();
    for (int c = 0; c < 8; ++c) {
        tmp[c] = sbuf[t * 8 + c];
    }
    if (*txType == 0) {
        d_fdct8(tmp, o);
    } else {
        d_fadst8(tmp, o);
    }
    for (int c = 0; c < 8; ++c) {
        output[t * 8 + c] = o[c];
    }
}
)CUDB1";
}

std::string invTxfmCuSource() {
    return R"CUDA(
__constant__ int kC12[64] = {
    4096, 4095, 4091, 4085, 4076, 4065, 4052, 4036, 4017, 3996, 3973, 3948, 3920, 3889, 3857, 3822,
    3784, 3745, 3703, 3659, 3612, 3564, 3513, 3461, 3406, 3349, 3290, 3229, 3166, 3102, 3035, 2967,
    2896, 2824, 2751, 2675, 2598, 2520, 2440, 2359, 2276, 2191, 2106, 2019, 1931, 1842, 1751, 1660,
    1567, 1474, 1380, 1285, 1189, 1092, 995,  897,  799,  700,  601,  501,  401,  301,  201,  101,
};

__constant__ int kS12[5] = {0, 1321, 2482, 3344, 3803};

__device__ int d_hb(int w0, int in0, int w1, int in1, int bit) {
    long long r = (long long)(w0 * in0) + (long long)(w1 * in1) + (1LL << (bit - 1));
    return (int)(r >> bit);
}

__device__ int d_rs(long long value, int bit) {
    return (int)((value + (1LL << (bit - 1))) >> bit);
}

__device__ int d_cv(int value, int bit) {
    if (bit <= 0) {
        return value;
    }
    const long long hi = (1LL << (bit - 1)) - 1;
    const long long lo = -(1LL << (bit - 1));
    if (value < lo) return (int)lo;
    if (value > hi) return (int)hi;
    return value;
}

__device__ void d_idct4i(const int* input, int* output) {
    const int bit = 12;
    int bf[4];
    int step[4];
    bf[0] = input[0];
    bf[1] = input[2];
    bf[2] = input[1];
    bf[3] = input[3];
    step[0] = d_hb(kC12[32], bf[0], kC12[32], bf[1], bit);
    step[1] = d_hb(kC12[32], bf[0], -kC12[32], bf[1], bit);
    step[2] = d_hb(kC12[48], bf[2], -kC12[16], bf[3], bit);
    step[3] = d_hb(kC12[16], bf[2], kC12[48], bf[3], bit);
    output[0] = d_cv(step[0] + step[3], 16);
    output[1] = d_cv(step[1] + step[2], 16);
    output[2] = d_cv(step[1] - step[2], 16);
    output[3] = d_cv(step[0] - step[3], 16);
}

__device__ void d_iadst4i(const int* input, int* output) {
    const int bit = 12;
    int x0 = input[0];
    int x1 = input[1];
    int x2 = input[2];
    int x3 = input[3];
    if (!(x0 | x1 | x2 | x3)) {
        output[0] = output[1] = output[2] = output[3] = 0;
        return;
    }
    int s0 = kS12[1] * x0;
    int s1 = kS12[2] * x0;
    int s2 = kS12[3] * x1;
    int s3 = kS12[4] * x2;
    int s4 = kS12[1] * x2;
    int s5 = kS12[2] * x3;
    int s6 = kS12[4] * x3;
    int s7 = (x0 - x2) + x3;
    s0 = s0 + s3;
    s1 = s1 - s4;
    s3 = s2;
    s2 = kS12[3] * s7;
    s0 = s0 + s5;
    s1 = s1 - s6;
    int y0 = s0 + s3;
    int y1 = s1 + s3;
    int y2 = s2;
    int y3 = s0 + s1;
    y3 = y3 - s3;
    output[0] = d_rs(y0, bit);
    output[1] = d_rs(y1, bit);
    output[2] = d_rs(y2, bit);
    output[3] = d_rs(y3, bit);
}

extern "C" __global__ void inv_txfm_2d_add_4x4(const int* coeffs, const int* txType, unsigned char* dst,
                                               const int* stride) {
    __shared__ int sbuf[16];
    const int t = threadIdx.x;
    int tmp[4];
    int o[4];
    for (int c = 0; c < 4; ++c) {
        tmp[c] = d_cv(coeffs[t * 4 + c], 16);
    }
    if (*txType == 0) {
        d_idct4i(tmp, o);
    } else {
        d_iadst4i(tmp, o);
    }
    for (int c = 0; c < 4; ++c) {
        sbuf[t * 4 + c] = o[c];
    }
    __syncthreads();
    for (int r = 0; r < 4; ++r) {
        tmp[r] = d_cv(sbuf[r * 4 + t], 16);
    }
    if (*txType == 0) {
        d_idct4i(tmp, o);
    } else {
        d_iadst4i(tmp, o);
    }
    for (int r = 0; r < 4; ++r) {
        int v = (int)dst[r * (*stride) + t] + d_rs(o[r], 4);
        if (v < 0) v = 0;
        else if (v > 255) v = 255;
        dst[r * (*stride) + t] = (unsigned char)v;
    }
}
)CUDA" R"CUDB1(
__device__ void d_idct8i(const int* input, int* output) {
    const int bit = 12;
    int bf0[8];
    int step[8];

    bf0[0] = input[0];
    bf0[1] = input[4];
    bf0[2] = input[2];
    bf0[3] = input[6];
    bf0[4] = input[1];
    bf0[5] = input[5];
    bf0[6] = input[3];
    bf0[7] = input[7];

    step[0] = bf0[0];
    step[1] = bf0[1];
    step[2] = bf0[2];
    step[3] = bf0[3];
    step[4] = d_hb(kC12[56], bf0[4], -kC12[8], bf0[7], bit);
    step[5] = d_hb(kC12[24], bf0[5], -kC12[40], bf0[6], bit);
    step[6] = d_hb(kC12[40], bf0[5], kC12[24], bf0[6], bit);
    step[7] = d_hb(kC12[8], bf0[4], kC12[56], bf0[7], bit);

    bf0[0] = d_hb(kC12[32], step[0], kC12[32], step[1], bit);
    bf0[1] = d_hb(kC12[32], step[0], -kC12[32], step[1], bit);
    bf0[2] = d_hb(kC12[48], step[2], -kC12[16], step[3], bit);
    bf0[3] = d_hb(kC12[16], step[2], kC12[48], step[3], bit);
    bf0[4] = d_cv(step[4] + step[5], 16);
    bf0[5] = d_cv(step[4] - step[5], 16);
    bf0[6] = d_cv(-step[6] + step[7], 16);
    bf0[7] = d_cv(step[6] + step[7], 16);

    step[0] = d_cv(bf0[0] + bf0[3], 16);
    step[1] = d_cv(bf0[1] + bf0[2], 16);
    step[2] = d_cv(bf0[1] - bf0[2], 16);
    step[3] = d_cv(bf0[0] - bf0[3], 16);
    step[4] = bf0[4];
    step[5] = d_hb(-kC12[32], bf0[5], kC12[32], bf0[6], bit);
    step[6] = d_hb(kC12[32], bf0[5], kC12[32], bf0[6], bit);
    step[7] = bf0[7];

    output[0] = d_cv(step[0] + step[7], 16);
    output[1] = d_cv(step[1] + step[6], 16);
    output[2] = d_cv(step[2] + step[5], 16);
    output[3] = d_cv(step[3] + step[4], 16);
    output[4] = d_cv(step[3] - step[4], 16);
    output[5] = d_cv(step[2] - step[5], 16);
    output[6] = d_cv(step[1] - step[6], 16);
    output[7] = d_cv(step[0] - step[7], 16);
}

__device__ void d_iadst8i(const int* input, int* output) {
    const int bit = 12;
    int bf0[8];
    int step[8];

    bf0[0] = input[7];
    bf0[1] = input[0];
    bf0[2] = input[5];
    bf0[3] = input[2];
    bf0[4] = input[3];
    bf0[5] = input[4];
    bf0[6] = input[1];
    bf0[7] = input[6];

    step[0] = d_hb(kC12[4], bf0[0], kC12[60], bf0[1], bit);
    step[1] = d_hb(kC12[60], bf0[0], -kC12[4], bf0[1], bit);
    step[2] = d_hb(kC12[20], bf0[2], kC12[44], bf0[3], bit);
    step[3] = d_hb(kC12[44], bf0[2], -kC12[20], bf0[3], bit);
    step[4] = d_hb(kC12[36], bf0[4], kC12[28], bf0[5], bit);
    step[5] = d_hb(kC12[28], bf0[4], -kC12[36], bf0[5], bit);
    step[6] = d_hb(kC12[52], bf0[6], kC12[12], bf0[7], bit);
    step[7] = d_hb(kC12[12], bf0[6], -kC12[52], bf0[7], bit);

    bf0[0] = d_cv(step[0] + step[4], 16);
    bf0[1] = d_cv(step[1] + step[5], 16);
    bf0[2] = d_cv(step[2] + step[6], 16);
    bf0[3] = d_cv(step[3] + step[7], 16);
    bf0[4] = d_cv(step[0] - step[4], 16);
    bf0[5] = d_cv(step[1] - step[5], 16);
    bf0[6] = d_cv(step[2] - step[6], 16);
    bf0[7] = d_cv(step[3] - step[7], 16);

    step[0] = bf0[0];
    step[1] = bf0[1];
    step[2] = bf0[2];
    step[3] = bf0[3];
    step[4] = d_hb(kC12[16], bf0[4], kC12[48], bf0[5], bit);
    step[5] = d_hb(kC12[48], bf0[4], -kC12[16], bf0[5], bit);
    step[6] = d_hb(-kC12[48], bf0[6], kC12[16], bf0[7], bit);
    step[7] = d_hb(kC12[16], bf0[6], kC12[48], bf0[7], bit);

    bf0[0] = d_cv(step[0] + step[2], 16);
    bf0[1] = d_cv(step[1] + step[3], 16);
    bf0[2] = d_cv(step[0] - step[2], 16);
    bf0[3] = d_cv(step[1] - step[3], 16);
    bf0[4] = d_cv(step[4] + step[6], 16);
    bf0[5] = d_cv(step[5] + step[7], 16);
    bf0[6] = d_cv(step[4] - step[6], 16);
    bf0[7] = d_cv(step[5] - step[7], 16);

    step[0] = bf0[0];
    step[1] = bf0[1];
    step[2] = d_hb(kC12[32], bf0[2], kC12[32], bf0[3], bit);
    step[3] = d_hb(kC12[32], bf0[2], -kC12[32], bf0[3], bit);
    step[4] = bf0[4];
    step[5] = bf0[5];
    step[6] = d_hb(kC12[32], bf0[6], kC12[32], bf0[7], bit);
    step[7] = d_hb(kC12[32], bf0[6], -kC12[32], bf0[7], bit);

    output[0] = step[0];
    output[1] = -step[4];
    output[2] = step[6];
    output[3] = -step[2];
    output[4] = step[3];
    output[5] = -step[7];
    output[6] = step[5];
    output[7] = -step[1];
}

// svt_av1_inv_txfm2d_add_c at TX_8X8: rows then columns, inv_shift_8x8 =
// {-1,-4} -> rounding >>1 after the row 1D and >>4 at the final add; clamp
// bit 16 at both 1D inputs
extern "C" __global__ void inv_txfm_2d_add_8x8(const int* coeffs, const int* txType,
                                               unsigned char* dst, const int* stride) {
    __shared__ int sbuf[64];
    const int t = threadIdx.x;
    int tmp[8];
    int o[8];
    for (int c = 0; c < 8; ++c) {
        tmp[c] = d_cv(coeffs[t * 8 + c], 16);
    }
    if (*txType == 0) {
        d_idct8i(tmp, o);
    } else {
        d_iadst8i(tmp, o);
    }
    for (int c = 0; c < 8; ++c) {
        sbuf[t * 8 + c] = d_rs(o[c], 1);
    }
    __syncthreads();
    for (int r = 0; r < 8; ++r) {
        tmp[r] = d_cv(sbuf[r * 8 + t], 16);
    }
    if (*txType == 0) {
        d_idct8i(tmp, o);
    } else {
        d_iadst8i(tmp, o);
    }
    for (int r = 0; r < 8; ++r) {
        int v = (int)dst[r * (*stride) + t] + d_rs(o[r], 4);
        if (v < 0) v = 0;
        else if (v > 255) v = 255;
        dst[r * (*stride) + t] = (unsigned char)v;
    }
}
)CUDB1";
}

// svt_aom_eb_av1 dc/ac QTX lookup tables (inv_transforms.c:3412 dc, :3357 ac),
// verbatim, 8-bit rows. QINDEX_RANGE from definitions.h:1643 (MAXQ 255 -
// MINQ 0 + 1).
#define QINDEX_RANGE 256
static const int16_t dc_qlookup_QTX[QINDEX_RANGE] = {
    4,   8,   8,   9,   10,  11,  12,  12,  13,   14,   15,   16,   17,   18,   19,   19,   20,  21,  22,  23,
    24,  25,  26,  26,  27,  28,  29,  30,  31,   32,   32,   33,   34,   35,   36,   37,   38,  38,  39,  40,
    41,  42,  43,  43,  44,  45,  46,  47,  48,   48,   49,   50,   51,   52,   53,   53,   54,  55,  56,  57,
    57,  58,  59,  60,  61,  62,  62,  63,  64,   65,   66,   66,   67,   68,   69,   70,   70,  71,  72,  73,
    74,  74,  75,  76,  77,  78,  78,  79,  80,   81,   81,   82,   83,   84,   85,   85,   87,  88,  90,  92,
    93,  95,  96,  98,  99,  101, 102, 104, 105,  107,  108,  110,  111,  113,  114,  116,  117, 118, 120, 121,
    123, 125, 127, 129, 131, 134, 136, 138, 140,  142,  144,  146,  148,  150,  152,  154,  156, 158, 161, 164,
    166, 169, 172, 174, 177, 180, 182, 185, 187,  190,  192,  195,  199,  202,  205,  208,  211, 214, 217, 220,
    223, 226, 230, 233, 237, 240, 243, 247, 250,  253,  257,  261,  265,  269,  272,  276,  280, 284, 288, 292,
    296, 300, 304, 309, 313, 317, 322, 326, 330,  335,  340,  344,  349,  354,  359,  364,  369, 374, 379, 384,
    389, 395, 400, 406, 411, 417, 423, 429, 435,  441,  447,  454,  461,  467,  475,  482,  489, 497, 505, 513,
    522, 530, 539, 549, 559, 569, 579, 590, 602,  614,  626,  640,  654,  668,  684,  700,  717, 736, 755, 775,
    796, 819, 843, 869, 896, 925, 955, 988, 1022, 1058, 1098, 1139, 1184, 1232, 1282, 1336,
};

static const int16_t ac_qlookup_QTX[QINDEX_RANGE] = {
    4,    8,    9,    10,   11,   12,   13,   14,   15,   16,   17,   18,   19,   20,   21,   22,   23,   24,   25,
    26,   27,   28,   29,   30,   31,   32,   33,   34,   35,   36,   37,   38,   39,   40,   41,   42,   43,   44,
    45,   46,   47,   48,   49,   50,   51,   52,   53,   54,   55,   56,   57,   58,   59,   60,   61,   62,   63,
    64,   65,   66,   67,   68,   69,   70,   71,   72,   73,   74,   75,   76,   77,   78,   79,   80,   81,   82,
    83,   84,   85,   86,   87,   88,   89,   90,   91,   92,   93,   94,   95,   96,   97,   98,   99,   100,  101,
    102,  104,  106,  108,  110,  112,  114,  116,  118,  120,  122,  124,  126,  128,  130,  132,  134,  136,  138,
    140,  142,  144,  146,  148,  150,  152,  155,  158,  161,  164,  167,  170,  173,  176,  179,  182,  185,  188,
    191,  194,  197,  200,  203,  207,  211,  215,  219,  223,  227,  231,  235,  239,  243,  247,  251,  255,  260,
    265,  270,  275,  280,  285,  290,  295,  300,  305,  311,  317,  323,  329,  335,  341,  347,  353,  359,  366,
    373,  380,  387,  394,  401,  408,  416,  424,  432,  440,  448,  456,  465,  474,  483,  492,  501,  510,  520,
    530,  540,  550,  560,  571,  582,  593,  604,  615,  627,  639,  651,  663,  676,  689,  702,  715,  729,  743,
    757,  771,  786,  801,  816,  832,  848,  864,  881,  898,  915,  933,  951,  969,  988,  1007, 1026, 1046, 1066,
    1087, 1108, 1129, 1151, 1173, 1196, 1219, 1243, 1267, 1292, 1317, 1343, 1369, 1396, 1423, 1451, 1479, 1508, 1537,
    1567, 1597, 1628, 1660, 1692, 1725, 1759, 1793, 1828,
};

// svt_aom_invert_quant (inv_transforms.c:3516), verbatim
void invertQuant(std::int16_t* quant, std::int16_t* shift, std::int32_t d) {
    std::uint32_t t;
    std::int32_t  l, m;
    t = static_cast<std::uint32_t>(d);
    for (l = 0; t > 1; l++) {
        t >>= 1;
    }
    m      = 1 + (1 << (16 + l)) / d;
    *quant = static_cast<std::int16_t>(m - (1 << 16));
    *shift = static_cast<std::int16_t>(1 << (16 - l));
}

// svt_aom_dc_quant_qtx (inv_transforms.c:3467) at EB_EIGHT_BIT, delta 0
std::int16_t dcQuantQtx(std::int32_t qindex) {
    const std::int32_t qClamped = qindex < 0 ? 0 : (qindex > 255 ? 255 : qindex);
    return dc_qlookup_QTX[qClamped];
}

// svt_aom_ac_quant_qtx (inv_transforms.c:3484) at EB_EIGHT_BIT, delta 0
std::int16_t acQuantQtx(std::int32_t qindex) {
    const std::int32_t qClamped = qindex < 0 ? 0 : (qindex > 255 ? 255 : qindex);
    return ac_qlookup_QTX[qClamped];
}

// svt_aom_get_qzbin_factor (inv_transforms.c:3501) at EB_EIGHT_BIT
std::int32_t qzbinFactor(std::int32_t q) {
    const std::int32_t quant = dcQuantQtx(q);
    return q == 0 ? 64 : (quant < 148 ? 84 : 80);
}

// luma rows of svt_av1_build_quantizer (md_config_process.c:106-135) at
// sharpness == 0
void buildQuantTables(std::int32_t qindex, QuantTables& tables) {
    const std::int32_t qzbinFactorVal     = qzbinFactor(qindex);
    const std::int32_t qroundingFactor = qindex == 0 ? 64 : 48;
    for (int i = 0; i < 2; ++i) {
        const std::int32_t quantQtx =
            i == 0 ? dcQuantQtx(qindex) : acQuantQtx(qindex);
        invertQuant(&tables.quant[i], &tables.quantShift[i], quantQtx);
        tables.quantFp[i]  = static_cast<std::int16_t>((1 << 16) / quantQtx);
        tables.roundFp[i]  = static_cast<std::int16_t>((64 * quantQtx) >> 7);
        tables.zbin[i]     = static_cast<std::int16_t>(roundShift(qzbinFactorVal * quantQtx, 7));
        tables.round[i]    = static_cast<std::int16_t>((qroundingFactor * quantQtx) >> 7);
        tables.dequant[i]  = static_cast<std::int16_t>(quantQtx);
    }
}

// default (up-right diagonal) scan, svt_aom_init_iscan formula
// (coefficients.c:345-363) at W=H=4: square, odd diagonal r-increasing,
// even r-decreasing
void defaultScan4x4(std::int16_t scan[16]) {
    const int W = 4, H = 4;
    int idx = 0;
    for (int d = 0; d < W + H - 1; ++d) {
        const int rlo = (d - (W - 1)) > 0 ? (d - (W - 1)) : 0;
        const int rhi = d < (H - 1) ? d : (H - 1);
        const int incr = (H > W) ? 1 : (W > H) ? 0 : (d & 1);
        if (incr) {
            for (int r = rlo; r <= rhi; ++r) {
                scan[idx++] = static_cast<std::int16_t>(r * W + (d - r));
            }
        } else {
            for (int r = rhi; r >= rlo; --r) {
                scan[idx++] = static_cast<std::int16_t>(r * W + (d - r));
            }
        }
    }
}

// quantize_fp_helper_c (full_loop.c:222) at log_scale 0, qm/iqm NULL branch.
// Size-parameterized over n_coeffs (the C helper's own parameter); the 4x4/8x8
// wrappers pass 16/64.
void quantizeFpN(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                 int nCoeffs, std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    int eobVal = -1;
    const std::int32_t rounding[2] = {tables.roundFp[0], tables.roundFp[1]};
    for (int i = 0; i < nCoeffs; ++i) {
        qcoeff[i] = 0;
        dqcoeff[i] = 0;
    }
    for (int i = 0; i < nCoeffs; ++i) {
        const int rc = scan[i];
        const std::int32_t thresh = tables.dequant[rc != 0];
        const std::int32_t coeffVal = coeff[rc];
        const std::int32_t coeffSign = coeffVal < 0 ? -1 : 0;
        std::int32_t absCoeff = (coeffVal ^ coeffSign) - coeffSign;
        std::int32_t tmp32 = 0;
        if ((absCoeff << (1 + 0)) >= thresh) {
            std::int64_t clamped = absCoeff + rounding[rc != 0];
            if (clamped < -32768) clamped = -32768;
            if (clamped > 32767) clamped = 32767;
            absCoeff = static_cast<std::int32_t>(clamped);
            tmp32 = static_cast<std::int32_t>((absCoeff * tables.quantFp[rc != 0]) >> (16 - 0));
            if (tmp32) {
                qcoeff[rc] = (tmp32 ^ coeffSign) - coeffSign;
                const std::int32_t absDqcoeff =
                    static_cast<std::int32_t>((static_cast<std::int64_t>(tmp32) *
                                               tables.dequant[rc != 0]) >>
                                              0);
                dqcoeff[rc] = (absDqcoeff ^ coeffSign) - coeffSign;
            }
        }
        if (tmp32) {
            eobVal = i;
        }
    }
    *eob = static_cast<std::uint16_t>(eobVal + 1);
}

void quantizeFp4x4(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                   std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    quantizeFpN(coeff, tables, scan, 16, qcoeff, dqcoeff, eob);
}

void quantizeFp8x8(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                   std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    quantizeFpN(coeff, tables, scan, 64, qcoeff, dqcoeff, eob);
}

// svt_aom_quantize_b_c (full_loop.c:31) at log_scale 0, qm/iqm NULL branch
// (wt = 1 << AOM_QM_BITS = 1 << 5, inv_transforms.h:27). Size-parameterized
// over n_coeffs like the C original.
void quantizeBN(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                int nCoeffs, std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    const std::int32_t zbins[2] = {tables.zbin[0], tables.zbin[1]};
    const std::int32_t nzbins[2] = {zbins[0] * -1, zbins[1] * -1};
    int nonZeroCount = nCoeffs;
    int eobVal = -1;
    for (int i = 0; i < nCoeffs; ++i) {
        qcoeff[i] = 0;
        dqcoeff[i] = 0;
    }

    // Pre-scan pass
    for (int i = nCoeffs - 1; i >= 0; i--) {
        const int rc = scan[i];
        const std::int32_t wt = (1 << 5);
        const std::int32_t coeffVal = coeff[rc] * wt;
        if (coeffVal < (zbins[rc != 0] * (1 << 5)) && coeffVal > (nzbins[rc != 0] * (1 << 5))) {
            nonZeroCount--;
        } else {
            break;
        }
    }

    // Quantization pass: All coefficients with index >= zero_flag are
    // skippable. Note: zero_flag can be zero.
    for (int i = 0; i < nonZeroCount; i++) {
        const int rc = scan[i];
        const std::int32_t coeffVal = coeff[rc];
        const std::int32_t coeffSign = coeffVal < 0 ? -1 : 0;
        const std::int32_t absCoeff = (coeffVal ^ coeffSign) - coeffSign;

        const std::int32_t wt = (1 << 5);
        if (absCoeff * wt >= (zbins[rc != 0] << 5)) {
            // full_loop.c:67: ROUND_POWER_OF_TWO(round_ptr[rc != 0], 0) is the
            // identity at log_scale 0 (roundShift(x, 0) would shift by 1<<-1,
            // UB) â€” add the round value directly
            std::int64_t tmp = absCoeff + tables.round[rc != 0];
            if (tmp < -32768) tmp = -32768;
            if (tmp > 32767) tmp = 32767;
            tmp *= wt;
            std::int32_t tmp32 = static_cast<std::int32_t>(
                ((((tmp * tables.quant[rc != 0]) >> 16) + tmp) * tables.quantShift[rc != 0]) >>
                (16 - 0 + 5));
            qcoeff[rc] = (tmp32 ^ coeffSign) - coeffSign;
            const std::int32_t absDqcoeff =
                static_cast<std::int32_t>((static_cast<std::int64_t>(tmp32) *
                                           tables.dequant[rc != 0]) >>
                                          0);
            dqcoeff[rc] = (absDqcoeff ^ coeffSign) - coeffSign;

            if (tmp32) {
                eobVal = i;
            }
        }
    }
    *eob = static_cast<std::uint16_t>(eobVal + 1);
}

void quantizeB4x4(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                  std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    quantizeBN(coeff, tables, scan, 16, qcoeff, dqcoeff, eob);
}

void quantizeB8x8(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                  std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    quantizeBN(coeff, tables, scan, 64, qcoeff, dqcoeff, eob);
}

// default (up-right diagonal) scan for 8x8, svt_aom_init_iscan formula
// (coefficients.c:345-363) at W=H=8
void defaultScan8x8(std::int16_t scan[64]) {
    const int W = 8, H = 8;
    int idx = 0;
    for (int d = 0; d < W + H - 1; ++d) {
        const int rlo = (d - (W - 1)) > 0 ? (d - (W - 1)) : 0;
        const int rhi = d < (H - 1) ? d : (H - 1);
        const int incr = (H > W) ? 1 : (W > H) ? 0 : (d & 1);
        if (incr) {
            for (int r = rlo; r <= rhi; ++r) {
                scan[idx++] = static_cast<std::int16_t>(r * W + (d - r));
            }
        } else {
            for (int r = rhi; r >= rlo; --r) {
                scan[idx++] = static_cast<std::int16_t>(r * W + (d - r));
            }
        }
    }
}

std::string quantCuSource() {
    return R"CUDA(
// quantize_fp_helper_c (full_loop.c:222) at log_scale 0, qm/iqm NULL branch;
// 16 threads, thread t handles scan position t (rc = scan[t] covers all 16
// raster positions exactly once). eob = highest nonzero scan position + 1.
extern "C" __global__ void quant_dequant_4x4(const int* coeff, const short* quantFp,
                                             const short* dequant, const short* roundFp,
                                             const short* scan, int* qcoeff, int* dqcoeff,
                                             unsigned short* eob) {
    __shared__ int eobPos[16];
    const int t = threadIdx.x;
    const int rc = scan[t];
    qcoeff[rc] = 0;
    dqcoeff[rc] = 0;
    const int thresh = dequant[rc != 0];
    const int coeffVal = coeff[rc];
    const int coeffSign = coeffVal < 0 ? -1 : 0;
    int absCoeff = (coeffVal ^ coeffSign) - coeffSign;
    int tmp32 = 0;
    if ((absCoeff << 1) >= thresh) {
        long long clamped = absCoeff + roundFp[rc != 0];
        if (clamped < -32768) clamped = -32768;
        if (clamped > 32767) clamped = 32767;
        absCoeff = (int)clamped;
        tmp32 = (int)((absCoeff * quantFp[rc != 0]) >> 16);
        if (tmp32) {
            qcoeff[rc] = (tmp32 ^ coeffSign) - coeffSign;
            const int absDq = (int)(((long long)tmp32 * dequant[rc != 0]) >> 0);
            dqcoeff[rc] = (absDq ^ coeffSign) - coeffSign;
        }
    }
    eobPos[t] = tmp32 ? t : -1;
    __syncthreads();
    if (t == 0) {
        int m = -1;
        for (int i = 0; i < 16; ++i) {
            if (eobPos[i] > m) m = eobPos[i];
        }
        *eob = (unsigned short)(m + 1);
    }
}

// TX_8X8 entry: same helper at n_coeffs=64, log_scale 0
// (av1_get_tx_scale_tab[TX_8X8] = 0); 64 threads, thread t = scan position t.
extern "C" __global__ void quant_dequant_8x8(const int* coeff, const short* quantFp,
                                             const short* dequant, const short* roundFp,
                                             const short* scan, int* qcoeff, int* dqcoeff,
                                             unsigned short* eob) {
    __shared__ int eobPos[64];
    const int t = threadIdx.x;
    const int rc = scan[t];
    qcoeff[rc] = 0;
    dqcoeff[rc] = 0;
    const int thresh = dequant[rc != 0];
    const int coeffVal = coeff[rc];
    const int coeffSign = coeffVal < 0 ? -1 : 0;
    int absCoeff = (coeffVal ^ coeffSign) - coeffSign;
    int tmp32 = 0;
    if ((absCoeff << 1) >= thresh) {
        long long clamped = absCoeff + roundFp[rc != 0];
        if (clamped < -32768) clamped = -32768;
        if (clamped > 32767) clamped = 32767;
        absCoeff = (int)clamped;
        tmp32 = (int)((absCoeff * quantFp[rc != 0]) >> 16);
        if (tmp32) {
            qcoeff[rc] = (tmp32 ^ coeffSign) - coeffSign;
            const int absDq = (int)(((long long)tmp32 * dequant[rc != 0]) >> 0);
            dqcoeff[rc] = (absDq ^ coeffSign) - coeffSign;
        }
    }
    eobPos[t] = tmp32 ? t : -1;
    __syncthreads();
    if (t == 0) {
        int m = -1;
        for (int i = 0; i < 64; ++i) {
            if (eobPos[i] > m) m = eobPos[i];
        }
        *eob = (unsigned short)(m + 1);
    }
}
)CUDA";
}

}  // namespace transforms

