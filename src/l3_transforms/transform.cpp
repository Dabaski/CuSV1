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

// svt_av1_fdct32_new (transforms.c:422-760), cos_bit = 12
// (fwd_cos_bit_col/row[3][3]), stage_range unused
void fdct32(const std::int32_t input[32], std::int32_t output[32]) {
    const int8_t cosBit = 12;
    const std::int32_t* cospi = kCospi12;
    std::int32_t bf0[32];
    std::int32_t step[32];

    // stage 1
    for (int i = 0; i < 16; ++i) {
        bf0[i] = input[i] + input[31 - i];
        bf0[31 - i] = -input[31 - i] + input[i];
    }

    // stage 2
    step[0]  = bf0[0] + bf0[15];
    step[1]  = bf0[1] + bf0[14];
    step[2]  = bf0[2] + bf0[13];
    step[3]  = bf0[3] + bf0[12];
    step[4]  = bf0[4] + bf0[11];
    step[5]  = bf0[5] + bf0[10];
    step[6]  = bf0[6] + bf0[9];
    step[7]  = bf0[7] + bf0[8];
    step[8]  = -bf0[8] + bf0[7];
    step[9]  = -bf0[9] + bf0[6];
    step[10] = -bf0[10] + bf0[5];
    step[11] = -bf0[11] + bf0[4];
    step[12] = -bf0[12] + bf0[3];
    step[13] = -bf0[13] + bf0[2];
    step[14] = -bf0[14] + bf0[1];
    step[15] = -bf0[15] + bf0[0];
    step[16] = bf0[16];
    step[17] = bf0[17];
    step[18] = bf0[18];
    step[19] = bf0[19];
    step[20] = halfBtf(-cospi[32], bf0[20], cospi[32], bf0[27], cosBit);
    step[21] = halfBtf(-cospi[32], bf0[21], cospi[32], bf0[26], cosBit);
    step[22] = halfBtf(-cospi[32], bf0[22], cospi[32], bf0[25], cosBit);
    step[23] = halfBtf(-cospi[32], bf0[23], cospi[32], bf0[24], cosBit);
    step[24] = halfBtf(cospi[32], bf0[24], cospi[32], bf0[23], cosBit);
    step[25] = halfBtf(cospi[32], bf0[25], cospi[32], bf0[22], cosBit);
    step[26] = halfBtf(cospi[32], bf0[26], cospi[32], bf0[21], cosBit);
    step[27] = halfBtf(cospi[32], bf0[27], cospi[32], bf0[20], cosBit);
    step[28] = bf0[28];
    step[29] = bf0[29];
    step[30] = bf0[30];
    step[31] = bf0[31];

    // stage 3
    bf0[0]  = step[0] + step[7];
    bf0[1]  = step[1] + step[6];
    bf0[2]  = step[2] + step[5];
    bf0[3]  = step[3] + step[4];
    bf0[4]  = -step[4] + step[3];
    bf0[5]  = -step[5] + step[2];
    bf0[6]  = -step[6] + step[1];
    bf0[7]  = -step[7] + step[0];
    bf0[8]  = step[8];
    bf0[9]  = step[9];
    bf0[10] = halfBtf(-cospi[32], step[10], cospi[32], step[13], cosBit);
    bf0[11] = halfBtf(-cospi[32], step[11], cospi[32], step[12], cosBit);
    bf0[12] = halfBtf(cospi[32], step[12], cospi[32], step[11], cosBit);
    bf0[13] = halfBtf(cospi[32], step[13], cospi[32], step[10], cosBit);
    bf0[14] = step[14];
    bf0[15] = step[15];
    bf0[16] = step[16] + step[23];
    bf0[17] = step[17] + step[22];
    bf0[18] = step[18] + step[21];
    bf0[19] = step[19] + step[20];
    bf0[20] = -step[20] + step[19];
    bf0[21] = -step[21] + step[18];
    bf0[22] = -step[22] + step[17];
    bf0[23] = -step[23] + step[16];
    bf0[24] = -step[24] + step[31];
    bf0[25] = -step[25] + step[30];
    bf0[26] = -step[26] + step[29];
    bf0[27] = -step[27] + step[28];
    bf0[28] = step[28] + step[27];
    bf0[29] = step[29] + step[26];
    bf0[30] = step[30] + step[25];
    bf0[31] = step[31] + step[24];

    // stage 4
    step[0]  = bf0[0] + bf0[3];
    step[1]  = bf0[1] + bf0[2];
    step[2]  = -bf0[2] + bf0[1];
    step[3]  = -bf0[3] + bf0[0];
    step[4]  = bf0[4];
    step[5]  = halfBtf(-cospi[32], bf0[5], cospi[32], bf0[6], cosBit);
    step[6]  = halfBtf(cospi[32], bf0[6], cospi[32], bf0[5], cosBit);
    step[7]  = bf0[7];
    step[8]  = bf0[8] + bf0[11];
    step[9]  = bf0[9] + bf0[10];
    step[10] = -bf0[10] + bf0[9];
    step[11] = -bf0[11] + bf0[8];
    step[12] = -bf0[12] + bf0[15];
    step[13] = -bf0[13] + bf0[14];
    step[14] = bf0[14] + bf0[13];
    step[15] = bf0[15] + bf0[12];
    step[16] = bf0[16];
    step[17] = bf0[17];
    step[18] = halfBtf(-cospi[16], bf0[18], cospi[48], bf0[29], cosBit);
    step[19] = halfBtf(-cospi[16], bf0[19], cospi[48], bf0[28], cosBit);
    step[20] = halfBtf(-cospi[48], bf0[20], -cospi[16], bf0[27], cosBit);
    step[21] = halfBtf(-cospi[48], bf0[21], -cospi[16], bf0[26], cosBit);
    step[22] = bf0[22];
    step[23] = bf0[23];
    step[24] = bf0[24];
    step[25] = bf0[25];
    step[26] = halfBtf(cospi[48], bf0[26], -cospi[16], bf0[21], cosBit);
    step[27] = halfBtf(cospi[48], bf0[27], -cospi[16], bf0[20], cosBit);
    step[28] = halfBtf(cospi[16], bf0[28], cospi[48], bf0[19], cosBit);
    step[29] = halfBtf(cospi[16], bf0[29], cospi[48], bf0[18], cosBit);
    step[30] = bf0[30];
    step[31] = bf0[31];

    // stage 5
    bf0[0]  = halfBtf(cospi[32], step[0], cospi[32], step[1], cosBit);
    bf0[1]  = halfBtf(-cospi[32], step[1], cospi[32], step[0], cosBit);
    bf0[2]  = halfBtf(cospi[48], step[2], cospi[16], step[3], cosBit);
    bf0[3]  = halfBtf(cospi[48], step[3], -cospi[16], step[2], cosBit);
    bf0[4]  = step[4] + step[5];
    bf0[5]  = -step[5] + step[4];
    bf0[6]  = -step[6] + step[7];
    bf0[7]  = step[7] + step[6];
    bf0[8]  = step[8];
    bf0[9]  = halfBtf(-cospi[16], step[9], cospi[48], step[14], cosBit);
    bf0[10] = halfBtf(-cospi[48], step[10], -cospi[16], step[13], cosBit);
    bf0[11] = step[11];
    bf0[12] = step[12];
    bf0[13] = halfBtf(cospi[48], step[13], -cospi[16], step[10], cosBit);
    bf0[14] = halfBtf(cospi[16], step[14], cospi[48], step[9], cosBit);
    bf0[15] = step[15];
    bf0[16] = step[16] + step[19];
    bf0[17] = step[17] + step[18];
    bf0[18] = -step[18] + step[17];
    bf0[19] = -step[19] + step[16];
    bf0[20] = -step[20] + step[23];
    bf0[21] = -step[21] + step[22];
    bf0[22] = step[22] + step[21];
    bf0[23] = step[23] + step[20];
    bf0[24] = step[24] + step[27];
    bf0[25] = step[25] + step[26];
    bf0[26] = -step[26] + step[25];
    bf0[27] = -step[27] + step[24];
    bf0[28] = -step[28] + step[31];
    bf0[29] = -step[29] + step[30];
    bf0[30] = step[30] + step[29];
    bf0[31] = step[31] + step[28];

    // stage 6
    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = bf0[2];
    step[3]  = bf0[3];
    step[4]  = halfBtf(cospi[56], bf0[4], cospi[8], bf0[7], cosBit);
    step[5]  = halfBtf(cospi[24], bf0[5], cospi[40], bf0[6], cosBit);
    step[6]  = halfBtf(cospi[24], bf0[6], -cospi[40], bf0[5], cosBit);
    step[7]  = halfBtf(cospi[56], bf0[7], -cospi[8], bf0[4], cosBit);
    step[8]  = bf0[8] + bf0[9];
    step[9]  = -bf0[9] + bf0[8];
    step[10] = -bf0[10] + bf0[11];
    step[11] = bf0[11] + bf0[10];
    step[12] = bf0[12] + bf0[13];
    step[13] = -bf0[13] + bf0[12];
    step[14] = -bf0[14] + bf0[15];
    step[15] = bf0[15] + bf0[14];
    step[16] = bf0[16];
    step[17] = halfBtf(-cospi[8], bf0[17], cospi[56], bf0[30], cosBit);
    step[18] = halfBtf(-cospi[56], bf0[18], -cospi[8], bf0[29], cosBit);
    step[19] = bf0[19];
    step[20] = bf0[20];
    step[21] = halfBtf(-cospi[40], bf0[21], cospi[24], bf0[26], cosBit);
    step[22] = halfBtf(-cospi[24], bf0[22], -cospi[40], bf0[25], cosBit);
    step[23] = bf0[23];
    step[24] = bf0[24];
    step[25] = halfBtf(cospi[24], bf0[25], -cospi[40], bf0[22], cosBit);
    step[26] = halfBtf(cospi[40], bf0[26], cospi[24], bf0[21], cosBit);
    step[27] = bf0[27];
    step[28] = bf0[28];
    step[29] = halfBtf(cospi[56], bf0[29], -cospi[8], bf0[18], cosBit);
    step[30] = halfBtf(cospi[8], bf0[30], cospi[56], bf0[17], cosBit);
    step[31] = bf0[31];

    // stage 7
    bf0[0]  = step[0];
    bf0[1]  = step[1];
    bf0[2]  = step[2];
    bf0[3]  = step[3];
    bf0[4]  = step[4];
    bf0[5]  = step[5];
    bf0[6]  = step[6];
    bf0[7]  = step[7];
    bf0[8]  = halfBtf(cospi[60], step[8], cospi[4], step[15], cosBit);
    bf0[9]  = halfBtf(cospi[28], step[9], cospi[36], step[14], cosBit);
    bf0[10] = halfBtf(cospi[44], step[10], cospi[20], step[13], cosBit);
    bf0[11] = halfBtf(cospi[12], step[11], cospi[52], step[12], cosBit);
    bf0[12] = halfBtf(cospi[12], step[12], -cospi[52], step[11], cosBit);
    bf0[13] = halfBtf(cospi[44], step[13], -cospi[20], step[10], cosBit);
    bf0[14] = halfBtf(cospi[28], step[14], -cospi[36], step[9], cosBit);
    bf0[15] = halfBtf(cospi[60], step[15], -cospi[4], step[8], cosBit);
    bf0[16] = step[16] + step[17];
    bf0[17] = -step[17] + step[16];
    bf0[18] = -step[18] + step[19];
    bf0[19] = step[19] + step[18];
    bf0[20] = step[20] + step[21];
    bf0[21] = -step[21] + step[20];
    bf0[22] = -step[22] + step[23];
    bf0[23] = step[23] + step[22];
    bf0[24] = step[24] + step[25];
    bf0[25] = -step[25] + step[24];
    bf0[26] = -step[26] + step[27];
    bf0[27] = step[27] + step[26];
    bf0[28] = step[28] + step[29];
    bf0[29] = -step[29] + step[28];
    bf0[30] = -step[30] + step[31];
    bf0[31] = step[31] + step[30];

    // stage 8
    for (int i = 0; i < 16; ++i) {
        step[i] = bf0[i];
    }
    step[16] = halfBtf(cospi[62], bf0[16], cospi[2], bf0[31], cosBit);
    step[17] = halfBtf(cospi[30], bf0[17], cospi[34], bf0[30], cosBit);
    step[18] = halfBtf(cospi[46], bf0[18], cospi[18], bf0[29], cosBit);
    step[19] = halfBtf(cospi[14], bf0[19], cospi[50], bf0[28], cosBit);
    step[20] = halfBtf(cospi[54], bf0[20], cospi[10], bf0[27], cosBit);
    step[21] = halfBtf(cospi[22], bf0[21], cospi[42], bf0[26], cosBit);
    step[22] = halfBtf(cospi[38], bf0[22], cospi[26], bf0[25], cosBit);
    step[23] = halfBtf(cospi[6], bf0[23], cospi[58], bf0[24], cosBit);
    step[24] = halfBtf(cospi[6], bf0[24], -cospi[58], bf0[23], cosBit);
    step[25] = halfBtf(cospi[38], bf0[25], -cospi[26], bf0[22], cosBit);
    step[26] = halfBtf(cospi[22], bf0[26], -cospi[42], bf0[21], cosBit);
    step[27] = halfBtf(cospi[54], bf0[27], -cospi[10], bf0[20], cosBit);
    step[28] = halfBtf(cospi[14], bf0[28], -cospi[50], bf0[19], cosBit);
    step[29] = halfBtf(cospi[46], bf0[29], -cospi[18], bf0[18], cosBit);
    step[30] = halfBtf(cospi[30], bf0[30], -cospi[34], bf0[17], cosBit);
    step[31] = halfBtf(cospi[62], bf0[31], -cospi[2], bf0[16], cosBit);

    // stage 9 (butterfly permutation)
    output[0]  = step[0];
    output[1]  = step[16];
    output[2]  = step[8];
    output[3]  = step[24];
    output[4]  = step[4];
    output[5]  = step[20];
    output[6]  = step[12];
    output[7]  = step[28];
    output[8]  = step[2];
    output[9]  = step[18];
    output[10] = step[10];
    output[11] = step[26];
    output[12] = step[6];
    output[13] = step[22];
    output[14] = step[14];
    output[15] = step[30];
    output[16] = step[1];
    output[17] = step[17];
    output[18] = step[9];
    output[19] = step[25];
    output[20] = step[5];
    output[21] = step[21];
    output[22] = step[13];
    output[23] = step[29];
    output[24] = step[3];
    output[25] = step[19];
    output[26] = step[11];
    output[27] = step[27];
    output[28] = step[7];
    output[29] = step[23];
    output[30] = step[15];
    output[31] = step[31];
}

// static av1_fadst32_new (transforms.c:1908-2314), cos_bit = 12, stage_range
// unused
void fadst32(const std::int32_t input[32], std::int32_t output[32]) {
    const int8_t cosBit = 12;
    const std::int32_t* cospi = kCospi12;
    std::int32_t bf0[32];
    std::int32_t step[32];

    // stage 1 (reversed interleave)
    bf0[0]  = input[31];
    bf0[1]  = input[0];
    bf0[2]  = input[29];
    bf0[3]  = input[2];
    bf0[4]  = input[27];
    bf0[5]  = input[4];
    bf0[6]  = input[25];
    bf0[7]  = input[6];
    bf0[8]  = input[23];
    bf0[9]  = input[8];
    bf0[10] = input[21];
    bf0[11] = input[10];
    bf0[12] = input[19];
    bf0[13] = input[12];
    bf0[14] = input[17];
    bf0[15] = input[14];
    bf0[16] = input[15];
    bf0[17] = input[16];
    bf0[18] = input[13];
    bf0[19] = input[18];
    bf0[20] = input[11];
    bf0[21] = input[20];
    bf0[22] = input[9];
    bf0[23] = input[22];
    bf0[24] = input[7];
    bf0[25] = input[24];
    bf0[26] = input[5];
    bf0[27] = input[26];
    bf0[28] = input[3];
    bf0[29] = input[28];
    bf0[30] = input[1];
    bf0[31] = input[30];

    // stage 2
    for (int k = 0; k < 16; ++k) {
        const int c1 = 4 * k + 1;
        step[2 * k]     = halfBtf(cospi[c1], bf0[2 * k], cospi[64 - c1], bf0[2 * k + 1], cosBit);
        step[2 * k + 1] = halfBtf(-cospi[c1], bf0[2 * k + 1], cospi[64 - c1], bf0[2 * k], cosBit);
    }

    // stage 3
    for (int i = 0; i < 16; ++i) {
        bf0[i] = step[i] + step[i + 16];
        bf0[i + 16] = -step[i + 16] + step[i];
    }

    // stage 4
    for (int i = 0; i < 16; ++i) step[i] = bf0[i];
    {
        step[16] = halfBtf(cospi[4], bf0[16], cospi[60], bf0[17], cosBit);
        step[17] = halfBtf(-cospi[4], bf0[17], cospi[60], bf0[16], cosBit);
        step[18] = halfBtf(cospi[20], bf0[18], cospi[44], bf0[19], cosBit);
        step[19] = halfBtf(-cospi[20], bf0[19], cospi[44], bf0[18], cosBit);
        step[20] = halfBtf(cospi[36], bf0[20], cospi[28], bf0[21], cosBit);
        step[21] = halfBtf(-cospi[36], bf0[21], cospi[28], bf0[20], cosBit);
        step[22] = halfBtf(cospi[52], bf0[22], cospi[12], bf0[23], cosBit);
        step[23] = halfBtf(-cospi[52], bf0[23], cospi[12], bf0[22], cosBit);
        step[24] = halfBtf(-cospi[60], bf0[24], cospi[4], bf0[25], cosBit);
        step[25] = halfBtf(cospi[60], bf0[25], cospi[4], bf0[24], cosBit);
        step[26] = halfBtf(-cospi[44], bf0[26], cospi[20], bf0[27], cosBit);
        step[27] = halfBtf(cospi[44], bf0[27], cospi[20], bf0[26], cosBit);
        step[28] = halfBtf(-cospi[28], bf0[28], cospi[36], bf0[29], cosBit);
        step[29] = halfBtf(cospi[28], bf0[29], cospi[36], bf0[28], cosBit);
        step[30] = halfBtf(-cospi[12], bf0[30], cospi[52], bf0[31], cosBit);
        step[31] = halfBtf(cospi[12], bf0[31], cospi[52], bf0[30], cosBit);
    }

    // stage 5
    for (int i = 0; i < 8; ++i) {
        bf0[i] = step[i] + step[i + 8];
        bf0[i + 8] = -step[i + 8] + step[i];
        bf0[16 + i] = step[16 + i] + step[24 + i];
        bf0[24 + i] = -step[24 + i] + step[16 + i];
    }

    // stage 6
    for (int i = 0; i < 8; ++i) step[i] = bf0[i];
    step[8]  = halfBtf(cospi[8], bf0[8], cospi[56], bf0[9], cosBit);
    step[9]  = halfBtf(-cospi[8], bf0[9], cospi[56], bf0[8], cosBit);
    step[10] = halfBtf(cospi[40], bf0[10], cospi[24], bf0[11], cosBit);
    step[11] = halfBtf(-cospi[40], bf0[11], cospi[24], bf0[10], cosBit);
    step[12] = halfBtf(-cospi[56], bf0[12], cospi[8], bf0[13], cosBit);
    step[13] = halfBtf(cospi[56], bf0[13], cospi[8], bf0[12], cosBit);
    step[14] = halfBtf(-cospi[24], bf0[14], cospi[40], bf0[15], cosBit);
    step[15] = halfBtf(cospi[24], bf0[15], cospi[40], bf0[14], cosBit);
    for (int i = 16; i < 24; ++i) step[i] = bf0[i];
    step[24] = halfBtf(cospi[8], bf0[24], cospi[56], bf0[25], cosBit);
    step[25] = halfBtf(-cospi[8], bf0[25], cospi[56], bf0[24], cosBit);
    step[26] = halfBtf(cospi[40], bf0[26], cospi[24], bf0[27], cosBit);
    step[27] = halfBtf(-cospi[40], bf0[27], cospi[24], bf0[26], cosBit);
    step[28] = halfBtf(-cospi[56], bf0[28], cospi[8], bf0[29], cosBit);
    step[29] = halfBtf(cospi[56], bf0[29], cospi[8], bf0[28], cosBit);
    step[30] = halfBtf(-cospi[24], bf0[30], cospi[40], bf0[31], cosBit);
    step[31] = halfBtf(cospi[24], bf0[31], cospi[40], bf0[30], cosBit);

    // stage 7
    for (int q = 0; q < 4; ++q) {
        const int base = 8 * q;
        for (int i = 0; i < 4; ++i) {
            bf0[base + i] = step[base + i] + step[base + 4 + i];
            bf0[base + 4 + i] = -step[base + 4 + i] + step[base + i];
        }
    }

    // stage 8 (half_btf pairs at 4-7, 12-15, 20-23, 28-31; the first four of
    // each 8-group are copies)
    for (int q = 0; q < 4; ++q) {
        const int base = 8 * q;
        step[base]     = bf0[base];
        step[base + 1] = bf0[base + 1];
        step[base + 2] = bf0[base + 2];
        step[base + 3] = bf0[base + 3];
        step[base + 4] = halfBtf(cospi[16], bf0[base + 4], cospi[48], bf0[base + 5], cosBit);
        step[base + 5] = halfBtf(-cospi[16], bf0[base + 5], cospi[48], bf0[base + 4], cosBit);
        step[base + 6] = halfBtf(-cospi[48], bf0[base + 6], cospi[16], bf0[base + 7], cosBit);
        step[base + 7] = halfBtf(cospi[48], bf0[base + 7], cospi[16], bf0[base + 6], cosBit);
    }

    // stage 9
    for (int q = 0; q < 8; ++q) {
        const int base = 4 * q;
        bf0[base]     = step[base] + step[base + 2];
        bf0[base + 1] = step[base + 1] + step[base + 3];
        bf0[base + 2] = -step[base + 2] + step[base];
        bf0[base + 3] = -step[base + 3] + step[base + 1];
    }

    // stage 10
    for (int q = 0; q < 8; ++q) {
        const int base = 4 * q;
        step[base]     = bf0[base];
        step[base + 1] = bf0[base + 1];
        step[base + 2] = halfBtf(cospi[32], bf0[base + 2], cospi[32], bf0[base + 3], cosBit);
        step[base + 3] = halfBtf(-cospi[32], bf0[base + 3], cospi[32], bf0[base + 2], cosBit);
    }

    // stage 11 (final permutation)
    output[0]  = step[0];
    output[1]  = -step[16];
    output[2]  = step[24];
    output[3]  = -step[8];
    output[4]  = step[12];
    output[5]  = -step[28];
    output[6]  = step[20];
    output[7]  = -step[4];
    output[8]  = step[6];
    output[9]  = -step[22];
    output[10] = step[30];
    output[11] = -step[14];
    output[12] = step[10];
    output[13] = -step[26];
    output[14] = step[18];
    output[15] = -step[2];
    output[16] = step[3];
    output[17] = -step[19];
    output[18] = step[27];
    output[19] = -step[11];
    output[20] = step[15];
    output[21] = -step[31];
    output[22] = step[23];
    output[23] = -step[7];
    output[24] = step[5];
    output[25] = -step[21];
    output[26] = step[29];
    output[27] = -step[13];
    output[28] = step[9];
    output[29] = -step[25];
    output[30] = step[17];
    output[31] = -step[1];
}

// svt_av1_idct32_new (inv_transforms.c:378-727), cos_bit = 12 (INV_COS_BIT);
// stage_range audit (verified against the verbatim source, L3): clamp_value
// on the butterfly adds at stages 3-9 only; stage 1-2 range checks are
// commented out in SVT itself
void idct32(const std::int32_t input[32], std::int32_t output[32]) {
    const int8_t cosBit = 12;
    const std::int32_t* cospi = kCospi12;
    const int8_t stageRange[10] = {16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
    std::int32_t bf0[32];
    std::int32_t step[32];

    // stage 1 (even-odd interleave; no clamp)
    bf0[0]  = input[0];
    bf0[1]  = input[16];
    bf0[2]  = input[8];
    bf0[3]  = input[24];
    bf0[4]  = input[4];
    bf0[5]  = input[20];
    bf0[6]  = input[12];
    bf0[7]  = input[28];
    bf0[8]  = input[2];
    bf0[9]  = input[18];
    bf0[10] = input[10];
    bf0[11] = input[26];
    bf0[12] = input[6];
    bf0[13] = input[22];
    bf0[14] = input[14];
    bf0[15] = input[30];
    bf0[16] = input[1];
    bf0[17] = input[17];
    bf0[18] = input[9];
    bf0[19] = input[25];
    bf0[20] = input[5];
    bf0[21] = input[21];
    bf0[22] = input[13];
    bf0[23] = input[29];
    bf0[24] = input[3];
    bf0[25] = input[19];
    bf0[26] = input[11];
    bf0[27] = input[27];
    bf0[28] = input[7];
    bf0[29] = input[23];
    bf0[30] = input[15];
    bf0[31] = input[31];

    // stage 2 (half_btf 16-31; no clamp)
    for (int i = 0; i < 16; ++i) step[i] = bf0[i];
    step[16] = halfBtf(cospi[62], bf0[16], -cospi[2], bf0[31], cosBit);
    step[17] = halfBtf(cospi[30], bf0[17], -cospi[34], bf0[30], cosBit);
    step[18] = halfBtf(cospi[46], bf0[18], -cospi[18], bf0[29], cosBit);
    step[19] = halfBtf(cospi[14], bf0[19], -cospi[50], bf0[28], cosBit);
    step[20] = halfBtf(cospi[54], bf0[20], -cospi[10], bf0[27], cosBit);
    step[21] = halfBtf(cospi[22], bf0[21], -cospi[42], bf0[26], cosBit);
    step[22] = halfBtf(cospi[38], bf0[22], -cospi[26], bf0[25], cosBit);
    step[23] = halfBtf(cospi[6], bf0[23], -cospi[58], bf0[24], cosBit);
    step[24] = halfBtf(cospi[58], bf0[23], cospi[6], bf0[24], cosBit);
    step[25] = halfBtf(cospi[26], bf0[22], cospi[38], bf0[25], cosBit);
    step[26] = halfBtf(cospi[42], bf0[21], cospi[22], bf0[26], cosBit);
    step[27] = halfBtf(cospi[10], bf0[20], cospi[54], bf0[27], cosBit);
    step[28] = halfBtf(cospi[50], bf0[19], cospi[14], bf0[28], cosBit);
    step[29] = halfBtf(cospi[18], bf0[18], cospi[46], bf0[29], cosBit);
    step[30] = halfBtf(cospi[34], bf0[17], cospi[30], bf0[30], cosBit);
    step[31] = halfBtf(cospi[2], bf0[16], cospi[62], bf0[31], cosBit);

    // stage 3 (clamp 16-31 at stageRange[3])
    for (int i = 0; i < 8; ++i) bf0[i] = step[i];
    bf0[8]  = halfBtf(cospi[60], step[8], -cospi[4], step[15], cosBit);
    bf0[9]  = halfBtf(cospi[28], step[9], -cospi[36], step[14], cosBit);
    bf0[10] = halfBtf(cospi[44], step[10], -cospi[20], step[13], cosBit);
    bf0[11] = halfBtf(cospi[12], step[11], -cospi[52], step[12], cosBit);
    bf0[12] = halfBtf(cospi[52], step[11], cospi[12], step[12], cosBit);
    bf0[13] = halfBtf(cospi[20], step[10], cospi[44], step[13], cosBit);
    bf0[14] = halfBtf(cospi[36], step[9], cospi[28], step[14], cosBit);
    bf0[15] = halfBtf(cospi[4], step[8], cospi[60], step[15], cosBit);
    bf0[16] = clampValue(step[16] + step[17], stageRange[3]);
    bf0[17] = clampValue(step[16] - step[17], stageRange[3]);
    bf0[18] = clampValue(-step[18] + step[19], stageRange[3]);
    bf0[19] = clampValue(step[18] + step[19], stageRange[3]);
    bf0[20] = clampValue(step[20] + step[21], stageRange[3]);
    bf0[21] = clampValue(step[20] - step[21], stageRange[3]);
    bf0[22] = clampValue(-step[22] + step[23], stageRange[3]);
    bf0[23] = clampValue(step[22] + step[23], stageRange[3]);
    bf0[24] = clampValue(step[24] + step[25], stageRange[3]);
    bf0[25] = clampValue(step[24] - step[25], stageRange[3]);
    bf0[26] = clampValue(-step[26] + step[27], stageRange[3]);
    bf0[27] = clampValue(step[26] + step[27], stageRange[3]);
    bf0[28] = clampValue(step[28] + step[29], stageRange[3]);
    bf0[29] = clampValue(step[28] - step[29], stageRange[3]);
    bf0[30] = clampValue(-step[30] + step[31], stageRange[3]);
    bf0[31] = clampValue(step[30] + step[31], stageRange[3]);

    // stage 4 (clamp 8-15 at stageRange[4])
    step[0] = bf0[0];
    step[1] = bf0[1];
    step[2] = bf0[2];
    step[3] = bf0[3];
    step[4] = halfBtf(cospi[56], bf0[4], -cospi[8], bf0[7], cosBit);
    step[5] = halfBtf(cospi[24], bf0[5], -cospi[40], bf0[6], cosBit);
    step[6] = halfBtf(cospi[40], bf0[5], cospi[24], bf0[6], cosBit);
    step[7] = halfBtf(cospi[8], bf0[4], cospi[56], bf0[7], cosBit);
    step[8]  = clampValue(bf0[8] + bf0[9], stageRange[4]);
    step[9]  = clampValue(bf0[8] - bf0[9], stageRange[4]);
    step[10] = clampValue(-bf0[10] + bf0[11], stageRange[4]);
    step[11] = clampValue(bf0[10] + bf0[11], stageRange[4]);
    step[12] = clampValue(bf0[12] + bf0[13], stageRange[4]);
    step[13] = clampValue(bf0[12] - bf0[13], stageRange[4]);
    step[14] = clampValue(-bf0[14] + bf0[15], stageRange[4]);
    step[15] = clampValue(bf0[14] + bf0[15], stageRange[4]);
    step[16] = bf0[16];
    step[17] = halfBtf(-cospi[8], bf0[17], cospi[56], bf0[30], cosBit);
    step[18] = halfBtf(-cospi[56], bf0[18], -cospi[8], bf0[29], cosBit);
    step[19] = bf0[19];
    step[20] = bf0[20];
    step[21] = halfBtf(-cospi[40], bf0[21], cospi[24], bf0[26], cosBit);
    step[22] = halfBtf(-cospi[24], bf0[22], -cospi[40], bf0[25], cosBit);
    step[23] = bf0[23];
    step[24] = bf0[24];
    step[25] = halfBtf(-cospi[40], bf0[22], cospi[24], bf0[25], cosBit);
    step[26] = halfBtf(cospi[24], bf0[21], cospi[40], bf0[26], cosBit);
    step[27] = bf0[27];
    step[28] = bf0[28];
    step[29] = halfBtf(-cospi[8], bf0[18], cospi[56], bf0[29], cosBit);
    step[30] = halfBtf(cospi[56], bf0[17], cospi[8], bf0[30], cosBit);
    step[31] = bf0[31];

    // stage 5 (clamp 4-7 and 16-31 at stageRange[5])
    bf0[0] = halfBtf(cospi[32], step[0], cospi[32], step[1], cosBit);
    bf0[1] = halfBtf(cospi[32], step[0], -cospi[32], step[1], cosBit);
    bf0[2] = halfBtf(cospi[48], step[2], -cospi[16], step[3], cosBit);
    bf0[3] = halfBtf(cospi[16], step[2], cospi[48], step[3], cosBit);
    bf0[4]  = clampValue(step[4] + step[5], stageRange[5]);
    bf0[5]  = clampValue(step[4] - step[5], stageRange[5]);
    bf0[6]  = clampValue(-step[6] + step[7], stageRange[5]);
    bf0[7]  = clampValue(step[6] + step[7], stageRange[5]);
    bf0[8] = step[8];
    bf0[9]  = halfBtf(-cospi[16], step[9], cospi[48], step[14], cosBit);
    bf0[10] = halfBtf(-cospi[48], step[10], -cospi[16], step[13], cosBit);
    bf0[11] = step[11];
    bf0[12] = step[12];
    bf0[13] = halfBtf(-cospi[16], step[10], cospi[48], step[13], cosBit);
    bf0[14] = halfBtf(cospi[48], step[9], cospi[16], step[14], cosBit);
    bf0[15] = step[15];
    bf0[16] = clampValue(step[16] + step[19], stageRange[5]);
    bf0[17] = clampValue(step[17] + step[18], stageRange[5]);
    bf0[18] = clampValue(step[17] - step[18], stageRange[5]);
    bf0[19] = clampValue(step[16] - step[19], stageRange[5]);
    bf0[20] = clampValue(-step[20] + step[23], stageRange[5]);
    bf0[21] = clampValue(-step[21] + step[22], stageRange[5]);
    bf0[22] = clampValue(step[21] + step[22], stageRange[5]);
    bf0[23] = clampValue(step[20] + step[23], stageRange[5]);
    bf0[24] = clampValue(step[24] + step[27], stageRange[5]);
    bf0[25] = clampValue(step[25] + step[26], stageRange[5]);
    bf0[26] = clampValue(step[25] - step[26], stageRange[5]);
    bf0[27] = clampValue(step[24] - step[27], stageRange[5]);
    bf0[28] = clampValue(-step[28] + step[31], stageRange[5]);
    bf0[29] = clampValue(-step[29] + step[30], stageRange[5]);
    bf0[30] = clampValue(step[29] + step[30], stageRange[5]);
    bf0[31] = clampValue(step[28] + step[31], stageRange[5]);

    // stage 6 (clamp 0-3, 8-15 at stageRange[6])
    step[0]  = clampValue(bf0[0] + bf0[3], stageRange[6]);
    step[1]  = clampValue(bf0[1] + bf0[2], stageRange[6]);
    step[2]  = clampValue(bf0[1] - bf0[2], stageRange[6]);
    step[3]  = clampValue(bf0[0] - bf0[3], stageRange[6]);
    step[4] = bf0[4];
    step[5] = halfBtf(-cospi[32], bf0[5], cospi[32], bf0[6], cosBit);
    step[6] = halfBtf(cospi[32], bf0[5], cospi[32], bf0[6], cosBit);
    step[7] = bf0[7];
    step[8]  = clampValue(bf0[8] + bf0[11], stageRange[6]);
    step[9]  = clampValue(bf0[9] + bf0[10], stageRange[6]);
    step[10] = clampValue(bf0[9] - bf0[10], stageRange[6]);
    step[11] = clampValue(bf0[8] - bf0[11], stageRange[6]);
    step[12] = clampValue(-bf0[12] + bf0[15], stageRange[6]);
    step[13] = clampValue(-bf0[13] + bf0[14], stageRange[6]);
    step[14] = clampValue(bf0[13] + bf0[14], stageRange[6]);
    step[15] = clampValue(bf0[12] + bf0[15], stageRange[6]);
    step[16] = bf0[16];
    step[17] = bf0[17];
    step[18] = halfBtf(-cospi[16], bf0[18], cospi[48], bf0[29], cosBit);
    step[19] = halfBtf(-cospi[16], bf0[19], cospi[48], bf0[28], cosBit);
    step[20] = halfBtf(-cospi[48], bf0[20], -cospi[16], bf0[27], cosBit);
    step[21] = halfBtf(-cospi[48], bf0[21], -cospi[16], bf0[26], cosBit);
    step[22] = bf0[22];
    step[23] = bf0[23];
    step[24] = bf0[24];
    step[25] = bf0[25];
    step[26] = halfBtf(-cospi[16], bf0[21], cospi[48], bf0[26], cosBit);
    step[27] = halfBtf(-cospi[16], bf0[20], cospi[48], bf0[27], cosBit);
    step[28] = halfBtf(cospi[48], bf0[19], cospi[16], bf0[28], cosBit);
    step[29] = halfBtf(cospi[48], bf0[18], cospi[16], bf0[29], cosBit);
    step[30] = bf0[30];
    step[31] = bf0[31];

    // stage 7 (clamp 0-7, 16-31 at stageRange[7])
    bf0[0]  = clampValue(step[0] + step[7], stageRange[7]);
    bf0[1]  = clampValue(step[1] + step[6], stageRange[7]);
    bf0[2]  = clampValue(step[2] + step[5], stageRange[7]);
    bf0[3]  = clampValue(step[3] + step[4], stageRange[7]);
    bf0[4]  = clampValue(step[3] - step[4], stageRange[7]);
    bf0[5]  = clampValue(step[2] - step[5], stageRange[7]);
    bf0[6]  = clampValue(step[1] - step[6], stageRange[7]);
    bf0[7]  = clampValue(step[0] - step[7], stageRange[7]);
    bf0[8] = step[8];
    bf0[9] = step[9];
    bf0[10] = halfBtf(-cospi[32], step[10], cospi[32], step[13], cosBit);
    bf0[11] = halfBtf(-cospi[32], step[11], cospi[32], step[12], cosBit);
    bf0[12] = halfBtf(cospi[32], step[11], cospi[32], step[12], cosBit);
    bf0[13] = halfBtf(cospi[32], step[10], cospi[32], step[13], cosBit);
    bf0[14] = step[14];
    bf0[15] = step[15];
    bf0[16] = clampValue(step[16] + step[23], stageRange[7]);
    bf0[17] = clampValue(step[17] + step[22], stageRange[7]);
    bf0[18] = clampValue(step[18] + step[21], stageRange[7]);
    bf0[19] = clampValue(step[19] + step[20], stageRange[7]);
    bf0[20] = clampValue(step[19] - step[20], stageRange[7]);
    bf0[21] = clampValue(step[18] - step[21], stageRange[7]);
    bf0[22] = clampValue(step[17] - step[22], stageRange[7]);
    bf0[23] = clampValue(step[16] - step[23], stageRange[7]);
    bf0[24] = clampValue(-step[24] + step[31], stageRange[7]);
    bf0[25] = clampValue(-step[25] + step[30], stageRange[7]);
    bf0[26] = clampValue(-step[26] + step[29], stageRange[7]);
    bf0[27] = clampValue(-step[27] + step[28], stageRange[7]);
    bf0[28] = clampValue(step[27] + step[28], stageRange[7]);
    bf0[29] = clampValue(step[26] + step[29], stageRange[7]);
    bf0[30] = clampValue(step[25] + step[30], stageRange[7]);
    bf0[31] = clampValue(step[24] + step[31], stageRange[7]);

    // stage 8 (clamp 0-15 at stageRange[8]; SVT sign pattern: + for 0-7,
    // bf0[15-i] - bf0[i] for 8-15)
    for (int i = 0; i < 8; ++i) {
        step[i] = clampValue(bf0[i] + bf0[15 - i], stageRange[8]);
        step[8 + i] = clampValue(bf0[7 - i] - bf0[8 + i], stageRange[8]);
    }
    for (int i = 0; i < 16; ++i) step[16 + i] = bf0[16 + i];
    step[20] = halfBtf(-cospi[32], bf0[20], cospi[32], bf0[27], cosBit);
    step[21] = halfBtf(-cospi[32], bf0[21], cospi[32], bf0[26], cosBit);
    step[22] = halfBtf(-cospi[32], bf0[22], cospi[32], bf0[25], cosBit);
    step[23] = halfBtf(-cospi[32], bf0[23], cospi[32], bf0[24], cosBit);
    step[24] = halfBtf(cospi[32], bf0[23], cospi[32], bf0[24], cosBit);
    step[25] = halfBtf(cospi[32], bf0[22], cospi[32], bf0[25], cosBit);
    step[26] = halfBtf(cospi[32], bf0[21], cospi[32], bf0[26], cosBit);
    step[27] = halfBtf(cospi[32], bf0[20], cospi[32], bf0[27], cosBit);

    // stage 9 (clamp everywhere at stageRange[9])
    for (int i = 0; i < 16; ++i) {
        output[i] = clampValue(step[i] + step[31 - i], stageRange[9]);
        output[31 - i] = clampValue(step[i] - step[31 - i], stageRange[9]);
    }
}

// static av1_iadst32_new (inv_transforms.c:1132-1565), cos_bit = 12;
// stage_range audit (L3): clamp_buf on EVERY stage (SVT applies clamp_buf to
// the whole 32-vector after each stage: stage 0 input clamp :1141, stages
// 1-11 :1179,:1218,:1256,:1295,:1333,:1372,:1410,:1449,:1487,:1526,:1564);
// NO all-zero early-out at 32
void iadst32(const std::int32_t input[32], std::int32_t output[32]) {
    const int8_t cosBit = 12;
    const std::int32_t* cospi = kCospi12;
    std::int32_t bf0[32];
    std::int32_t step[32];

    // stage 0: clamp_buf on the input (the host callers pre-clamp to 16 in
    // the 2D core; the helper's own clamp_buf(input, 32, 16) is a no-op
    // there - kept implicit, values arrive within range)
    // stage 1 (fixed sign-permutation; clamp 16)
    static const int idxTab[32] = {0, -32, -16, 16, -8, 24, 8, -24, -4, 28, 12, -20, 4, -28, -12, 20,
                                   -2, 30, 14, -18, 6, -26, -10, 22, 2, -30, -14, 18, -6, 26, 10, -22};
    for (int i = 0; i < 32; ++i) {
        const int j = idxTab[i];
        bf0[i] = j >= 0 ? input[j] : -input[-1 - j];
    }
    clampBufIv(bf0, 32, kInvClampBit);

    // stage 2 (verbatim pattern: copies at 0,1,4,5,8,9,12,13,16,17,20,21,24,25,
    // 28,29; cospi32 pairs at (2,3),(6,7),(10,11),(14,15),(18,19),(22,23),
    // (26,27),(30,31); clamp)
    for (int q = 0; q < 16; ++q) {
        const int base = 2 * q;
        if (base % 4 == 0) {
            step[base]     = bf0[base];
            step[base + 1] = bf0[base + 1];
        } else {
            step[base]     = halfBtf(cospi[32], bf0[base], cospi[32], bf0[base + 1], cosBit);
            step[base + 1] = halfBtf(cospi[32], bf0[base], -cospi[32], bf0[base + 1], cosBit);
        }
    }
    clampBufIv(step, 32, kInvClampBit);

    // stage 3 (pairwise sum/diff on (0,2),(1,3)... groups of 2 within 4)
    for (int q = 0; q < 8; ++q) {
        const int base = 4 * q;
        bf0[base]     = step[base] + step[base + 2];
        bf0[base + 1] = step[base + 1] + step[base + 3];
        bf0[base + 2] = step[base] - step[base + 2];
        bf0[base + 3] = step[base + 1] - step[base + 3];
    }
    clampBufIv(bf0, 32, kInvClampBit);

    // stage 4 (half_btf groups at 4-7, 12-15, 20-23, 28-31; copies elsewhere)
    for (int q = 0; q < 4; ++q) {
        const int base = 8 * q;
        step[base]     = bf0[base];
        step[base + 1] = bf0[base + 1];
        step[base + 2] = bf0[base + 2];
        step[base + 3] = bf0[base + 3];
        step[base + 4] = halfBtf(cospi[16], bf0[base + 4], cospi[48], bf0[base + 5], cosBit);
        step[base + 5] = halfBtf(cospi[48], bf0[base + 4], -cospi[16], bf0[base + 5], cosBit);
        step[base + 6] = halfBtf(-cospi[48], bf0[base + 6], cospi[16], bf0[base + 7], cosBit);
        step[base + 7] = halfBtf(cospi[16], bf0[base + 6], cospi[48], bf0[base + 7], cosBit);
    }
    clampBufIv(step, 32, kInvClampBit);

    // stage 5 (group-of-8 sum/diff: [0..3]+/-[4..7])
    for (int q = 0; q < 4; ++q) {
        const int base = 8 * q;
        for (int i = 0; i < 4; ++i) {
            bf0[base + i] = step[base + i] + step[base + 4 + i];
            bf0[base + 4 + i] = step[base + i] - step[base + 4 + i];
        }
    }
    clampBufIv(bf0, 32, kInvClampBit);

    // stage 6 (half_btf groups at 8-15 and 24-31; copies elsewhere)
    for (int i = 0; i < 8; ++i) step[i] = bf0[i];
    step[8]  = halfBtf(cospi[8], bf0[8], cospi[56], bf0[9], cosBit);
    step[9]  = halfBtf(cospi[56], bf0[8], -cospi[8], bf0[9], cosBit);
    step[10] = halfBtf(cospi[40], bf0[10], cospi[24], bf0[11], cosBit);
    step[11] = halfBtf(cospi[24], bf0[10], -cospi[40], bf0[11], cosBit);
    step[12] = halfBtf(-cospi[56], bf0[12], cospi[8], bf0[13], cosBit);
    step[13] = halfBtf(cospi[8], bf0[12], cospi[56], bf0[13], cosBit);
    step[14] = halfBtf(-cospi[24], bf0[14], cospi[40], bf0[15], cosBit);
    step[15] = halfBtf(cospi[40], bf0[14], cospi[24], bf0[15], cosBit);
    for (int i = 16; i < 24; ++i) step[i] = bf0[i];
    step[24] = halfBtf(cospi[8], bf0[24], cospi[56], bf0[25], cosBit);
    step[25] = halfBtf(cospi[56], bf0[24], -cospi[8], bf0[25], cosBit);
    step[26] = halfBtf(cospi[40], bf0[26], cospi[24], bf0[27], cosBit);
    step[27] = halfBtf(cospi[24], bf0[26], -cospi[40], bf0[27], cosBit);
    step[28] = halfBtf(-cospi[56], bf0[28], cospi[8], bf0[29], cosBit);
    step[29] = halfBtf(cospi[8], bf0[28], cospi[56], bf0[29], cosBit);
    step[30] = halfBtf(-cospi[24], bf0[30], cospi[40], bf0[31], cosBit);
    step[31] = halfBtf(cospi[40], bf0[30], cospi[24], bf0[31], cosBit);
    clampBufIv(step, 32, kInvClampBit);

    // stage 7 (group-of-16 sum/diff: [0..7]+/-[8..15], [16..23]+/-[24..31])
    for (int h = 0; h < 2; ++h) {
        const int base = 16 * h;
        for (int i = 0; i < 8; ++i) {
            bf0[base + i] = step[base + i] + step[base + 8 + i];
            bf0[base + 8 + i] = step[base + i] - step[base + 8 + i];
        }
    }
    clampBufIv(bf0, 32, kInvClampBit);

    // stage 8 (half_btf at 16-31; copies at 0-15)
    for (int i = 0; i < 16; ++i) step[i] = bf0[i];
    step[16] = halfBtf(cospi[4], bf0[16], cospi[60], bf0[17], cosBit);
    step[17] = halfBtf(cospi[60], bf0[16], -cospi[4], bf0[17], cosBit);
    step[18] = halfBtf(cospi[20], bf0[18], cospi[44], bf0[19], cosBit);
    step[19] = halfBtf(cospi[44], bf0[18], -cospi[20], bf0[19], cosBit);
    step[20] = halfBtf(cospi[36], bf0[20], cospi[28], bf0[21], cosBit);
    step[21] = halfBtf(cospi[28], bf0[20], -cospi[36], bf0[21], cosBit);
    step[22] = halfBtf(cospi[52], bf0[22], cospi[12], bf0[23], cosBit);
    step[23] = halfBtf(cospi[12], bf0[22], -cospi[52], bf0[23], cosBit);
    step[24] = halfBtf(-cospi[60], bf0[24], cospi[4], bf0[25], cosBit);
    step[25] = halfBtf(cospi[4], bf0[24], cospi[60], bf0[25], cosBit);
    step[26] = halfBtf(-cospi[44], bf0[26], cospi[20], bf0[27], cosBit);
    step[27] = halfBtf(cospi[20], bf0[26], cospi[44], bf0[27], cosBit);
    step[28] = halfBtf(-cospi[28], bf0[28], cospi[36], bf0[29], cosBit);
    step[29] = halfBtf(cospi[36], bf0[28], cospi[28], bf0[29], cosBit);
    step[30] = halfBtf(-cospi[12], bf0[30], cospi[52], bf0[31], cosBit);
    step[31] = halfBtf(cospi[52], bf0[30], cospi[12], bf0[31], cosBit);
    clampBufIv(step, 32, kInvClampBit);

    // stage 9 (group-of-32 sum/diff: [0..15]+/-[16..31])
    for (int i = 0; i < 16; ++i) {
        bf0[i] = step[i] + step[16 + i];
        bf0[16 + i] = step[i] - step[16 + i];
    }
    clampBufIv(bf0, 32, kInvClampBit);

    // stage 10 (full 32 half_btf sweep, cospi pairs (1,63),(5,59)...(61,3))
    for (int k = 0; k < 16; ++k) {
        const int c1 = 4 * k + 1;
        step[2 * k]     = halfBtf(cospi[c1], bf0[2 * k], cospi[64 - c1], bf0[2 * k + 1], cosBit);
        step[2 * k + 1] = halfBtf(cospi[64 - c1], bf0[2 * k], -cospi[c1], bf0[2 * k + 1], cosBit);
    }
    clampBufIv(step, 32, kInvClampBit);

    // stage 11 (final permutation; clamp)
    static const int permTab[32] = {1, 30, 3, 28, 5, 26, 7, 24, 9, 22, 11, 20, 13, 18, 15, 16,
                                    17, 14, 19, 12, 21, 10, 23, 8, 25, 6, 27, 4, 29, 2, 31, 0};
    for (int i = 0; i < 32; ++i) output[i] = step[permTab[i]];
    clampBufIv(output, 32, kInvClampBit);
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

// av1_tranform_two_d_core_c (transforms.c:2398) at TX_32X32: fwd_shift_32x32
// = {2, -4, 0} (transforms.c:125), cos_bit col 12 / row 12 from
// fwd_cos_bit_col/row[3][3] (transforms.c:19-22)
void fwdTxfm2d32x32(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    TxfmFn txfm = type == TxType::DCT_DCT ? fdct32 : fadst32;
    std::int32_t buf[32 * 32];
    std::int32_t tempIn[32];
    std::int32_t tempOut[32];

    for (std::uint32_t c = 0; c < 32; ++c) {
        for (std::uint32_t r = 0; r < 32; ++r) {
            tempIn[r] = input[r * stride + c];
        }
        // round_shift_array(..., -shift[0]) with shift[0] = 2 -> x4
        for (std::uint32_t i = 0; i < 32; ++i) {
            tempIn[i] *= (1 << 2);
        }
        txfm(tempIn, tempOut);
        // round_shift_array(..., -shift[1]) with shift[1] = -4 -> >>4 rounding
        for (std::uint32_t i = 0; i < 32; ++i) {
            tempOut[i] = roundShift(tempOut[i], 4);
        }
        for (std::uint32_t r = 0; r < 32; ++r) {
            buf[r * 32 + c] = tempOut[r];
        }
    }

    for (std::uint32_t r = 0; r < 32; ++r) {
        txfm(buf + r * 32, output + r * 32);
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

InvTxfmFn inv1d32(TxType type) {
    return type == TxType::DCT_DCT ? idct32 : iadst32;
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

// svt_av1_inv_txfm2d_add_32x32_c / inv_txfm2d_add_c, TX_32X32: rows then
// columns, inv_shift_32x32 = {-2, -4}, cos_bit 12/12, no flips, clamp bits
// bd+8=16 and max(bd+6,16)=16; add via clip_pixel_highbd(pred +
// round_shift(out, 4), 8)
void invTxfm2dAdd32x32(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    InvTxfmFn txfmRow = inv1d32(type);
    std::int32_t buf[32 * 32];
    std::int32_t tempIn[32];
    std::int32_t tempOut[32];

    // rows: clamp 16, 1D, round_shift_array(-shift[0]) = +2 rounding >>2
    for (std::uint32_t r = 0; r < 32; ++r) {
        for (std::uint32_t c = 0; c < 32; ++c) {
            tempIn[c] = coeffs[r * 32 + c];
        }
        clampBufIv(tempIn, 32, kInvClampBit);
        txfmRow(tempIn, buf + r * 32);
        roundShiftArrayIv(buf + r * 32, 32, 2);
    }

    // columns: clamp 16, 1D, round_shift_array(-shift[1]) = +4, clip add
    for (std::uint32_t c = 0; c < 32; ++c) {
        for (std::uint32_t r = 0; r < 32; ++r) {
            tempIn[r] = buf[r * 32 + c];
        }
        clampBufIv(tempIn, 32, kInvClampBit);
        txfmRow(tempIn, tempOut);
        roundShiftArrayIv(tempOut, 32, 4);
        for (std::uint32_t r = 0; r < 32; ++r) {
            clipPixelAdd(dst + r * stride + c, tempOut[r]);
        }
    }
}

std::string fwdTxfmCuSource() {
    // built with += per chunk: adjacent raw literals concatenated would
    // exceed the MSVC combined-literal limit (~32KB) once the 32-point
    // device functions landed (C2026); += keeps each literal small.
    std::string s;
    s += R"CUDA(
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
)CUDA";
    s += R"CUDB1(
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
    s += R"CUDB2(
__constant__ int kC12f[64] = {
    4096, 4095, 4091, 4085, 4076, 4065, 4052, 4036, 4017, 3996, 3973, 3948, 3920, 3889, 3857, 3822,
    3784, 3745, 3703, 3659, 3612, 3564, 3513, 3461, 3406, 3349, 3290, 3229, 3166, 3102, 3035, 2967,
    2896, 2824, 2751, 2675, 2598, 2520, 2440, 2359, 2276, 2191, 2106, 2019, 1931, 1842, 1751, 1660,
    1567, 1474, 1380, 1285, 1189, 1092, 995,  897,  799,  700,  601,  501,  401,  301,  201,  101,
};

// svt_av1_fdct32_new (transforms.c:422-760) at cos_bit 12
__device__ void d_fdct32(const int* input, int* output) {
    const int bit = 12;
    const int* cospi = kC12f;
    int bf0[32];
    int step[32];

    for (int i = 0; i < 16; ++i) {
        bf0[i] = input[i] + input[31 - i];
        bf0[31 - i] = -input[31 - i] + input[i];
    }

    step[0]  = bf0[0] + bf0[15];
    step[1]  = bf0[1] + bf0[14];
    step[2]  = bf0[2] + bf0[13];
    step[3]  = bf0[3] + bf0[12];
    step[4]  = bf0[4] + bf0[11];
    step[5]  = bf0[5] + bf0[10];
    step[6]  = bf0[6] + bf0[9];
    step[7]  = bf0[7] + bf0[8];
    step[8]  = -bf0[8] + bf0[7];
    step[9]  = -bf0[9] + bf0[6];
    step[10] = -bf0[10] + bf0[5];
    step[11] = -bf0[11] + bf0[4];
    step[12] = -bf0[12] + bf0[3];
    step[13] = -bf0[13] + bf0[2];
    step[14] = -bf0[14] + bf0[1];
    step[15] = -bf0[15] + bf0[0];
    step[16] = bf0[16];
    step[17] = bf0[17];
    step[18] = bf0[18];
    step[19] = bf0[19];
    step[20] = d_half_btf(-cospi[32], bf0[20], cospi[32], bf0[27], bit);
    step[21] = d_half_btf(-cospi[32], bf0[21], cospi[32], bf0[26], bit);
    step[22] = d_half_btf(-cospi[32], bf0[22], cospi[32], bf0[25], bit);
    step[23] = d_half_btf(-cospi[32], bf0[23], cospi[32], bf0[24], bit);
    step[24] = d_half_btf(cospi[32], bf0[24], cospi[32], bf0[23], bit);
    step[25] = d_half_btf(cospi[32], bf0[25], cospi[32], bf0[22], bit);
    step[26] = d_half_btf(cospi[32], bf0[26], cospi[32], bf0[21], bit);
    step[27] = d_half_btf(cospi[32], bf0[27], cospi[32], bf0[20], bit);
    step[28] = bf0[28];
    step[29] = bf0[29];
    step[30] = bf0[30];
    step[31] = bf0[31];

    bf0[0]  = step[0] + step[7];
    bf0[1]  = step[1] + step[6];
    bf0[2]  = step[2] + step[5];
    bf0[3]  = step[3] + step[4];
    bf0[4]  = -step[4] + step[3];
    bf0[5]  = -step[5] + step[2];
    bf0[6]  = -step[6] + step[1];
    bf0[7]  = -step[7] + step[0];
    bf0[8]  = step[8];
    bf0[9]  = step[9];
    bf0[10] = d_half_btf(-cospi[32], step[10], cospi[32], step[13], bit);
    bf0[11] = d_half_btf(-cospi[32], step[11], cospi[32], step[12], bit);
    bf0[12] = d_half_btf(cospi[32], step[12], cospi[32], step[11], bit);
    bf0[13] = d_half_btf(cospi[32], step[13], cospi[32], step[10], bit);
    bf0[14] = step[14];
    bf0[15] = step[15];
    bf0[16] = step[16] + step[23];
    bf0[17] = step[17] + step[22];
    bf0[18] = step[18] + step[21];
    bf0[19] = step[19] + step[20];
    bf0[20] = -step[20] + step[19];
    bf0[21] = -step[21] + step[18];
    bf0[22] = -step[22] + step[17];
    bf0[23] = -step[23] + step[16];
    bf0[24] = -step[24] + step[31];
    bf0[25] = -step[25] + step[30];
    bf0[26] = -step[26] + step[29];
    bf0[27] = -step[27] + step[28];
    bf0[28] = step[28] + step[27];
    bf0[29] = step[29] + step[26];
    bf0[30] = step[30] + step[25];
    bf0[31] = step[31] + step[24];

    step[0]  = bf0[0] + bf0[3];
    step[1]  = bf0[1] + bf0[2];
    step[2]  = -bf0[2] + bf0[1];
    step[3]  = -bf0[3] + bf0[0];
    step[4]  = bf0[4];
    step[5]  = d_half_btf(-cospi[32], bf0[5], cospi[32], bf0[6], bit);
    step[6]  = d_half_btf(cospi[32], bf0[6], cospi[32], bf0[5], bit);
    step[7]  = bf0[7];
    step[8]  = bf0[8] + bf0[11];
    step[9]  = bf0[9] + bf0[10];
    step[10] = -bf0[10] + bf0[9];
    step[11] = -bf0[11] + bf0[8];
    step[12] = -bf0[12] + bf0[15];
    step[13] = -bf0[13] + bf0[14];
    step[14] = bf0[14] + bf0[13];
    step[15] = bf0[15] + bf0[12];
    step[16] = bf0[16];
    step[17] = bf0[17];
    step[18] = d_half_btf(-cospi[16], bf0[18], cospi[48], bf0[29], bit);
    step[19] = d_half_btf(-cospi[16], bf0[19], cospi[48], bf0[28], bit);
    step[20] = d_half_btf(-cospi[48], bf0[20], -cospi[16], bf0[27], bit);
    step[21] = d_half_btf(-cospi[48], bf0[21], -cospi[16], bf0[26], bit);
    step[22] = bf0[22];
    step[23] = bf0[23];
    step[24] = bf0[24];
    step[25] = bf0[25];
    step[26] = d_half_btf(cospi[48], bf0[26], -cospi[16], bf0[21], bit);
    step[27] = d_half_btf(cospi[48], bf0[27], -cospi[16], bf0[20], bit);
    step[28] = d_half_btf(cospi[16], bf0[28], cospi[48], bf0[19], bit);
    step[29] = d_half_btf(cospi[16], bf0[29], cospi[48], bf0[18], bit);
    step[30] = bf0[30];
    step[31] = bf0[31];

    bf0[0]  = d_half_btf(cospi[32], step[0], cospi[32], step[1], bit);
    bf0[1]  = d_half_btf(-cospi[32], step[1], cospi[32], step[0], bit);
    bf0[2]  = d_half_btf(cospi[48], step[2], cospi[16], step[3], bit);
    bf0[3]  = d_half_btf(cospi[48], step[3], -cospi[16], step[2], bit);
    bf0[4]  = step[4] + step[5];
    bf0[5]  = -step[5] + step[4];
    bf0[6]  = -step[6] + step[7];
    bf0[7]  = step[7] + step[6];
    bf0[8]  = step[8];
    bf0[9]  = d_half_btf(-cospi[16], step[9], cospi[48], step[14], bit);
    bf0[10] = d_half_btf(-cospi[48], step[10], -cospi[16], step[13], bit);
    bf0[11] = step[11];
    bf0[12] = step[12];
    bf0[13] = d_half_btf(cospi[48], step[13], -cospi[16], step[10], bit);
    bf0[14] = d_half_btf(cospi[16], step[14], cospi[48], step[9], bit);
    bf0[15] = step[15];
    bf0[16] = step[16] + step[19];
    bf0[17] = step[17] + step[18];
    bf0[18] = -step[18] + step[17];
    bf0[19] = -step[19] + step[16];
    bf0[20] = -step[20] + step[23];
    bf0[21] = -step[21] + step[22];
    bf0[22] = step[22] + step[21];
    bf0[23] = step[23] + step[20];
    bf0[24] = step[24] + step[27];
    bf0[25] = step[25] + step[26];
    bf0[26] = -step[26] + step[25];
    bf0[27] = -step[27] + step[24];
    bf0[28] = -step[28] + step[31];
    bf0[29] = -step[29] + step[30];
    bf0[30] = step[30] + step[29];
    bf0[31] = step[31] + step[28];

    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = bf0[2];
    step[3]  = bf0[3];
    step[4]  = d_half_btf(cospi[56], bf0[4], cospi[8], bf0[7], bit);
    step[5]  = d_half_btf(cospi[24], bf0[5], cospi[40], bf0[6], bit);
    step[6]  = d_half_btf(cospi[24], bf0[6], -cospi[40], bf0[5], bit);
    step[7]  = d_half_btf(cospi[56], bf0[7], -cospi[8], bf0[4], bit);
    step[8]  = bf0[8] + bf0[9];
    step[9]  = -bf0[9] + bf0[8];
    step[10] = -bf0[10] + bf0[11];
    step[11] = bf0[11] + bf0[10];
    step[12] = bf0[12] + bf0[13];
    step[13] = -bf0[13] + bf0[12];
    step[14] = -bf0[14] + bf0[15];
    step[15] = bf0[15] + bf0[14];
    step[16] = bf0[16];
    step[17] = d_half_btf(-cospi[8], bf0[17], cospi[56], bf0[30], bit);
    step[18] = d_half_btf(-cospi[56], bf0[18], -cospi[8], bf0[29], bit);
    step[19] = bf0[19];
    step[20] = bf0[20];
    step[21] = d_half_btf(-cospi[40], bf0[21], cospi[24], bf0[26], bit);
    step[22] = d_half_btf(-cospi[24], bf0[22], -cospi[40], bf0[25], bit);
    step[23] = bf0[23];
    step[24] = bf0[24];
    step[25] = d_half_btf(cospi[24], bf0[25], -cospi[40], bf0[22], bit);
    step[26] = d_half_btf(cospi[40], bf0[26], cospi[24], bf0[21], bit);
    step[27] = bf0[27];
    step[28] = bf0[28];
    step[29] = d_half_btf(cospi[56], bf0[29], -cospi[8], bf0[18], bit);
    step[30] = d_half_btf(cospi[8], bf0[30], cospi[56], bf0[17], bit);
    step[31] = bf0[31];

    bf0[0]  = step[0];
    bf0[1]  = step[1];
    bf0[2]  = step[2];
    bf0[3]  = step[3];
    bf0[4]  = step[4];
    bf0[5]  = step[5];
    bf0[6]  = step[6];
    bf0[7]  = step[7];
    bf0[8]  = d_half_btf(cospi[60], step[8], cospi[4], step[15], bit);
    bf0[9]  = d_half_btf(cospi[28], step[9], cospi[36], step[14], bit);
    bf0[10] = d_half_btf(cospi[44], step[10], cospi[20], step[13], bit);
    bf0[11] = d_half_btf(cospi[12], step[11], cospi[52], step[12], bit);
    bf0[12] = d_half_btf(cospi[12], step[12], -cospi[52], step[11], bit);
    bf0[13] = d_half_btf(cospi[44], step[13], -cospi[20], step[10], bit);
    bf0[14] = d_half_btf(cospi[28], step[14], -cospi[36], step[9], bit);
    bf0[15] = d_half_btf(cospi[60], step[15], -cospi[4], step[8], bit);
    bf0[16] = step[16] + step[17];
    bf0[17] = -step[17] + step[16];
    bf0[18] = -step[18] + step[19];
    bf0[19] = step[19] + step[18];
    bf0[20] = step[20] + step[21];
    bf0[21] = -step[21] + step[20];
    bf0[22] = -step[22] + step[23];
    bf0[23] = step[23] + step[22];
    bf0[24] = step[24] + step[25];
    bf0[25] = -step[25] + step[24];
    bf0[26] = -step[26] + step[27];
    bf0[27] = step[27] + step[26];
    bf0[28] = step[28] + step[29];
    bf0[29] = -step[29] + step[28];
    bf0[30] = -step[30] + step[31];
    bf0[31] = step[31] + step[30];

    for (int i = 0; i < 16; ++i) step[i] = bf0[i];
    step[16] = d_half_btf(cospi[62], bf0[16], cospi[2], bf0[31], bit);
    step[17] = d_half_btf(cospi[30], bf0[17], cospi[34], bf0[30], bit);
    step[18] = d_half_btf(cospi[46], bf0[18], cospi[18], bf0[29], bit);
    step[19] = d_half_btf(cospi[14], bf0[19], cospi[50], bf0[28], bit);
    step[20] = d_half_btf(cospi[54], bf0[20], cospi[10], bf0[27], bit);
    step[21] = d_half_btf(cospi[22], bf0[21], cospi[42], bf0[26], bit);
    step[22] = d_half_btf(cospi[38], bf0[22], cospi[26], bf0[25], bit);
    step[23] = d_half_btf(cospi[6], bf0[23], cospi[58], bf0[24], bit);
    step[24] = d_half_btf(cospi[6], bf0[24], -cospi[58], bf0[23], bit);
    step[25] = d_half_btf(cospi[38], bf0[25], -cospi[26], bf0[22], bit);
    step[26] = d_half_btf(cospi[22], bf0[26], -cospi[42], bf0[21], bit);
    step[27] = d_half_btf(cospi[54], bf0[27], -cospi[10], bf0[20], bit);
    step[28] = d_half_btf(cospi[14], bf0[28], -cospi[50], bf0[19], bit);
    step[29] = d_half_btf(cospi[46], bf0[29], -cospi[18], bf0[18], bit);
    step[30] = d_half_btf(cospi[30], bf0[30], -cospi[34], bf0[17], bit);
    step[31] = d_half_btf(cospi[62], bf0[31], -cospi[2], bf0[16], bit);

    output[0]  = step[0];
    output[1]  = step[16];
    output[2]  = step[8];
    output[3]  = step[24];
    output[4]  = step[4];
    output[5]  = step[20];
    output[6]  = step[12];
    output[7]  = step[28];
    output[8]  = step[2];
    output[9]  = step[18];
    output[10] = step[10];
    output[11] = step[26];
    output[12] = step[6];
    output[13] = step[22];
    output[14] = step[14];
    output[15] = step[30];
    output[16] = step[1];
    output[17] = step[17];
    output[18] = step[9];
    output[19] = step[25];
    output[20] = step[5];
    output[21] = step[21];
    output[22] = step[13];
    output[23] = step[29];
    output[24] = step[3];
    output[25] = step[19];
    output[26] = step[11];
    output[27] = step[27];
    output[28] = step[7];
    output[29] = step[23];
    output[30] = step[15];
    output[31] = step[31];
}

// static av1_fadst32_new (transforms.c:1908-2314) at cos_bit 12
)CUDB2";
    s += R"CUDB3(
__device__ void d_fadst32(const int* input, int* output) {
    const int bit = 12;
    const int* cospi = kC12f;
    int bf0[32];
    int step[32];

    static const int idxTab[32] = {0, -32, -16, 16, -8, 24, 8, -24, -4, 28, 12, -20, 4, -28, -12, 20,
                                   -2, 30, 14, -18, 6, -26, -10, 22, 2, -30, -14, 18, -6, 26, 10, -22};
    for (int i = 0; i < 32; ++i) {
        const int j = idxTab[i];
        bf0[i] = j >= 0 ? input[j] : -input[-1 - j];
    }

    for (int k = 0; k < 16; ++k) {
        const int c1 = 4 * k + 1;
        step[2 * k]     = d_half_btf(cospi[c1], bf0[2 * k], cospi[64 - c1], bf0[2 * k + 1], bit);
        step[2 * k + 1] = d_half_btf(-cospi[c1], bf0[2 * k + 1], cospi[64 - c1], bf0[2 * k], bit);
    }

    for (int i = 0; i < 16; ++i) {
        bf0[i] = step[i] + step[i + 16];
        bf0[i + 16] = -step[i + 16] + step[i];
    }

    for (int i = 0; i < 16; ++i) step[i] = bf0[i];
    step[16] = d_half_btf(cospi[4], bf0[16], cospi[60], bf0[17], bit);
    step[17] = d_half_btf(-cospi[4], bf0[17], cospi[60], bf0[16], bit);
    step[18] = d_half_btf(cospi[20], bf0[18], cospi[44], bf0[19], bit);
    step[19] = d_half_btf(-cospi[20], bf0[19], cospi[44], bf0[18], bit);
    step[20] = d_half_btf(cospi[36], bf0[20], cospi[28], bf0[21], bit);
    step[21] = d_half_btf(-cospi[36], bf0[21], cospi[28], bf0[20], bit);
    step[22] = d_half_btf(cospi[52], bf0[22], cospi[12], bf0[23], bit);
    step[23] = d_half_btf(-cospi[52], bf0[23], cospi[12], bf0[22], bit);
    step[24] = d_half_btf(-cospi[60], bf0[24], cospi[4], bf0[25], bit);
    step[25] = d_half_btf(cospi[60], bf0[25], cospi[4], bf0[24], bit);
    step[26] = d_half_btf(-cospi[44], bf0[26], cospi[20], bf0[27], bit);
    step[27] = d_half_btf(cospi[44], bf0[27], cospi[20], bf0[26], bit);
    step[28] = d_half_btf(-cospi[28], bf0[28], cospi[36], bf0[29], bit);
    step[29] = d_half_btf(cospi[28], bf0[29], cospi[36], bf0[28], bit);
    step[30] = d_half_btf(-cospi[12], bf0[30], cospi[52], bf0[31], bit);
    step[31] = d_half_btf(cospi[12], bf0[31], cospi[52], bf0[30], bit);

    for (int i = 0; i < 8; ++i) {
        bf0[i] = step[i] + step[i + 8];
        bf0[i + 8] = -step[i + 8] + step[i];
        bf0[16 + i] = step[16 + i] + step[24 + i];
        bf0[24 + i] = -step[24 + i] + step[16 + i];
    }

    for (int i = 0; i < 8; ++i) step[i] = bf0[i];
    step[8]  = d_half_btf(cospi[8], bf0[8], cospi[56], bf0[9], bit);
    step[9]  = d_half_btf(cospi[56], bf0[8], -cospi[8], bf0[9], bit);
    step[10] = d_half_btf(cospi[40], bf0[10], cospi[24], bf0[11], bit);
    step[11] = d_half_btf(cospi[24], bf0[10], -cospi[40], bf0[11], bit);
    step[12] = d_half_btf(-cospi[56], bf0[12], cospi[8], bf0[13], bit);
    step[13] = d_half_btf(cospi[56], bf0[13], cospi[8], bf0[12], bit);
    step[14] = d_half_btf(-cospi[24], bf0[14], cospi[40], bf0[15], bit);
    step[15] = d_half_btf(cospi[24], bf0[15], cospi[40], bf0[14], bit);
    for (int i = 16; i < 24; ++i) step[i] = bf0[i];
    step[24] = d_half_btf(cospi[8], bf0[24], cospi[56], bf0[25], bit);
    step[25] = d_half_btf(-cospi[8], bf0[25], cospi[56], bf0[24], bit);
    step[26] = d_half_btf(cospi[40], bf0[26], cospi[24], bf0[27], bit);
    step[27] = d_half_btf(-cospi[40], bf0[27], cospi[24], bf0[26], bit);
    step[28] = d_half_btf(-cospi[56], bf0[28], cospi[8], bf0[29], bit);
    step[29] = d_half_btf(cospi[56], bf0[29], cospi[8], bf0[28], bit);
    step[30] = d_half_btf(-cospi[24], bf0[30], cospi[40], bf0[31], bit);
    step[31] = d_half_btf(cospi[24], bf0[31], cospi[40], bf0[30], bit);

    for (int q = 0; q < 4; ++q) {
        const int base = 8 * q;
        for (int i = 0; i < 4; ++i) {
            bf0[base + i] = step[base + i] + step[base + 4 + i];
            bf0[base + 4 + i] = -step[base + 4 + i] + step[base + i];
        }
    }

    for (int q = 0; q < 4; ++q) {
        const int base = 8 * q;
        step[base]     = bf0[base];
        step[base + 1] = bf0[base + 1];
        step[base + 2] = bf0[base + 2];
        step[base + 3] = bf0[base + 3];
        step[base + 4] = d_half_btf(cospi[16], bf0[base + 4], cospi[48], bf0[base + 5], bit);
        step[base + 5] = d_half_btf(-cospi[16], bf0[base + 5], cospi[48], bf0[base + 4], bit);
        step[base + 6] = d_half_btf(-cospi[48], bf0[base + 6], cospi[16], bf0[base + 7], bit);
        step[base + 7] = d_half_btf(cospi[48], bf0[base + 7], cospi[16], bf0[base + 6], bit);
    }

    for (int q = 0; q < 8; ++q) {
        const int base = 4 * q;
        bf0[base]     = step[base] + step[base + 2];
        bf0[base + 1] = step[base + 1] + step[base + 3];
        bf0[base + 2] = -step[base + 2] + step[base];
        bf0[base + 3] = -step[base + 3] + step[base + 1];
    }

    for (int q = 0; q < 8; ++q) {
        const int base = 4 * q;
        step[base]     = bf0[base];
        step[base + 1] = bf0[base + 1];
        step[base + 2] = d_half_btf(cospi[32], bf0[base + 2], cospi[32], bf0[base + 3], bit);
        step[base + 3] = d_half_btf(-cospi[32], bf0[base + 3], cospi[32], bf0[base + 2], bit);
    }

    output[0]  = step[0];
    output[1]  = -step[16];
    output[2]  = step[24];
    output[3]  = -step[8];
    output[4]  = step[12];
    output[5]  = -step[28];
    output[6]  = step[20];
    output[7]  = -step[4];
    output[8]  = step[6];
    output[9]  = -step[22];
    output[10] = step[30];
    output[11] = -step[14];
    output[12] = step[10];
    output[13] = -step[26];
    output[14] = step[18];
    output[15] = -step[2];
    output[16] = step[3];
    output[17] = -step[19];
    output[18] = step[27];
    output[19] = -step[11];
    output[20] = step[15];
    output[21] = -step[31];
    output[22] = step[23];
    output[23] = -step[7];
    output[24] = step[5];
    output[25] = -step[21];
    output[26] = step[29];
    output[27] = -step[13];
    output[28] = step[9];
    output[29] = -step[25];
    output[30] = step[17];
    output[31] = -step[1];
}

// svt_av1_fdct16_new (transforms.c:268-420) with the cos_bit parameter
// (cospi_arr: bit 13 -> kCospi, bit 12 -> kC12f)
__device__ void d_fdct16(const int* input, int* output, int bit) {
    const int* cospi = (bit == 13) ? kCospi : kC12f;
    int bf0[16];
    int step[16];

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
    step[10] = d_half_btf(-cospi[32], bf0[10], cospi[32], bf0[13], bit);
    step[11] = d_half_btf(-cospi[32], bf0[11], cospi[32], bf0[12], bit);
    step[12] = d_half_btf(cospi[32], bf0[12], cospi[32], bf0[11], bit);
    step[13] = d_half_btf(cospi[32], bf0[13], cospi[32], bf0[10], bit);
    step[14] = bf0[14];
    step[15] = bf0[15];

    bf0[0]  = step[0] + step[3];
    bf0[1]  = step[1] + step[2];
    bf0[2]  = -step[2] + step[1];
    bf0[3]  = -step[3] + step[0];
    bf0[4]  = step[4];
    bf0[5]  = d_half_btf(-cospi[32], step[5], cospi[32], step[6], bit);
    bf0[6]  = d_half_btf(cospi[32], step[6], cospi[32], step[5], bit);
    bf0[7]  = step[7];
    bf0[8]  = step[8] + step[11];
    bf0[9]  = step[9] + step[10];
    bf0[10] = -step[10] + step[9];
    bf0[11] = -step[11] + step[8];
    bf0[12] = -step[12] + step[15];
    bf0[13] = -step[13] + step[14];
    bf0[14] = step[14] + step[13];
    bf0[15] = step[15] + step[12];

    step[0]  = d_half_btf(cospi[32], bf0[0], cospi[32], bf0[1], bit);
    step[1]  = d_half_btf(-cospi[32], bf0[1], cospi[32], bf0[0], bit);
    step[2]  = d_half_btf(cospi[48], bf0[2], cospi[16], bf0[3], bit);
    step[3]  = d_half_btf(cospi[48], bf0[3], -cospi[16], bf0[2], bit);
    step[4]  = bf0[4] + bf0[5];
    step[5]  = -bf0[5] + bf0[4];
    step[6]  = -bf0[6] + bf0[7];
    step[7]  = bf0[7] + bf0[6];
    step[8]  = bf0[8];
    step[9]  = d_half_btf(-cospi[16], bf0[9], cospi[48], bf0[14], bit);
    step[10] = d_half_btf(-cospi[48], bf0[10], -cospi[16], bf0[13], bit);
    step[11] = bf0[11];
    step[12] = bf0[12];
    step[13] = d_half_btf(cospi[48], bf0[13], -cospi[16], bf0[10], bit);
    step[14] = d_half_btf(cospi[16], bf0[14], cospi[48], bf0[9], bit);
    step[15] = bf0[15];

    bf0[0]  = step[0];
    bf0[1]  = step[1];
    bf0[2]  = step[2];
    bf0[3]  = step[3];
    bf0[4]  = d_half_btf(cospi[56], step[4], cospi[8], step[7], bit);
    bf0[5]  = d_half_btf(cospi[24], step[5], cospi[40], step[6], bit);
    bf0[6]  = d_half_btf(cospi[24], step[6], -cospi[40], step[5], bit);
    bf0[7]  = d_half_btf(cospi[56], step[7], -cospi[8], step[4], bit);
    bf0[8]  = step[8] + step[9];
    bf0[9]  = -step[9] + step[8];
    bf0[10] = -step[10] + step[11];
    bf0[11] = step[11] + step[10];
    bf0[12] = step[12] + step[13];
    bf0[13] = -step[13] + step[12];
    bf0[14] = -step[14] + step[15];
    bf0[15] = step[15] + step[14];

    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = bf0[2];
    step[3]  = bf0[3];
    step[4]  = bf0[4];
    step[5]  = bf0[5];
    step[6]  = bf0[6];
    step[7]  = bf0[7];
    step[8]  = d_half_btf(cospi[60], bf0[8], cospi[4], bf0[15], bit);
    step[9]  = d_half_btf(cospi[28], bf0[9], cospi[36], bf0[14], bit);
    step[10] = d_half_btf(cospi[44], bf0[10], cospi[20], bf0[13], bit);
    step[11] = d_half_btf(cospi[12], bf0[11], cospi[52], bf0[12], bit);
    step[12] = d_half_btf(cospi[12], bf0[12], -cospi[52], bf0[11], bit);
    step[13] = d_half_btf(cospi[44], bf0[13], -cospi[20], bf0[10], bit);
    step[14] = d_half_btf(cospi[28], bf0[14], -cospi[36], bf0[9], bit);
    step[15] = d_half_btf(cospi[60], bf0[15], -cospi[4], bf0[8], bit);

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

)CUDB3";
    s += R"CUDB4(
// svt_av1_fadst16_new (transforms.c:1714-1906)
__device__ void d_fadst16(const int* input, int* output, int bit) {
    const int* cospi = (bit == 13) ? kCospi : kC12f;
    int bf0[16];
    int step[16];

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

    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = d_half_btf(cospi[32], bf0[2], cospi[32], bf0[3], bit);
    step[3]  = d_half_btf(cospi[32], bf0[2], -cospi[32], bf0[3], bit);
    step[4]  = bf0[4];
    step[5]  = bf0[5];
    step[6]  = d_half_btf(cospi[32], bf0[6], cospi[32], bf0[7], bit);
    step[7]  = d_half_btf(cospi[32], bf0[6], -cospi[32], bf0[7], bit);
    step[8]  = bf0[8];
    step[9]  = bf0[9];
    step[10] = d_half_btf(cospi[32], bf0[10], cospi[32], bf0[11], bit);
    step[11] = d_half_btf(cospi[32], bf0[10], -cospi[32], bf0[11], bit);
    step[12] = bf0[12];
    step[13] = bf0[13];
    step[14] = d_half_btf(cospi[32], bf0[14], cospi[32], bf0[15], bit);
    step[15] = d_half_btf(cospi[32], bf0[14], -cospi[32], bf0[15], bit);

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

    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = bf0[2];
    step[3]  = bf0[3];
    step[4]  = d_half_btf(cospi[16], bf0[4], cospi[48], bf0[5], bit);
    step[5]  = d_half_btf(cospi[48], bf0[4], -cospi[16], bf0[5], bit);
    step[6]  = d_half_btf(-cospi[48], bf0[6], cospi[16], bf0[7], bit);
    step[7]  = d_half_btf(cospi[16], bf0[6], cospi[48], bf0[7], bit);
    step[8]  = bf0[8];
    step[9]  = bf0[9];
    step[10] = bf0[10];
    step[11] = bf0[11];
    step[12] = d_half_btf(cospi[16], bf0[12], cospi[48], bf0[13], bit);
    step[13] = d_half_btf(cospi[48], bf0[12], -cospi[16], bf0[13], bit);
    step[14] = d_half_btf(-cospi[48], bf0[14], cospi[16], bf0[15], bit);
    step[15] = d_half_btf(cospi[16], bf0[14], cospi[48], bf0[15], bit);

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

    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = bf0[2];
    step[3]  = bf0[3];
    step[4]  = bf0[4];
    step[5]  = bf0[5];
    step[6]  = bf0[6];
    step[7]  = bf0[7];
    step[8]  = d_half_btf(cospi[8], bf0[8], cospi[56], bf0[9], bit);
    step[9]  = d_half_btf(cospi[56], bf0[8], -cospi[8], bf0[9], bit);
    step[10] = d_half_btf(cospi[40], bf0[10], cospi[24], bf0[11], bit);
    step[11] = d_half_btf(cospi[24], bf0[10], -cospi[40], bf0[11], bit);
    step[12] = d_half_btf(-cospi[56], bf0[12], cospi[8], bf0[13], bit);
    step[13] = d_half_btf(cospi[8], bf0[12], cospi[56], bf0[13], bit);
    step[14] = d_half_btf(-cospi[24], bf0[14], cospi[40], bf0[15], bit);
    step[15] = d_half_btf(cospi[40], bf0[14], cospi[24], bf0[15], bit);

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

    step[0]  = d_half_btf(cospi[2], bf0[0], cospi[62], bf0[1], bit);
    step[1]  = d_half_btf(cospi[62], bf0[0], -cospi[2], bf0[1], bit);
    step[2]  = d_half_btf(cospi[10], bf0[2], cospi[54], bf0[3], bit);
    step[3]  = d_half_btf(cospi[54], bf0[2], -cospi[10], bf0[3], bit);
    step[4]  = d_half_btf(cospi[18], bf0[4], cospi[46], bf0[5], bit);
    step[5]  = d_half_btf(cospi[46], bf0[4], -cospi[18], bf0[5], bit);
    step[6]  = d_half_btf(cospi[26], bf0[6], cospi[38], bf0[7], bit);
    step[7]  = d_half_btf(cospi[38], bf0[6], -cospi[26], bf0[7], bit);
    step[8]  = d_half_btf(cospi[34], bf0[8], cospi[30], bf0[9], bit);
    step[9]  = d_half_btf(cospi[30], bf0[8], -cospi[34], bf0[9], bit);
    step[10] = d_half_btf(cospi[42], bf0[10], cospi[22], bf0[11], bit);
    step[11] = d_half_btf(cospi[22], bf0[10], -cospi[42], bf0[11], bit);
    step[12] = d_half_btf(cospi[50], bf0[12], cospi[14], bf0[13], bit);
    step[13] = d_half_btf(cospi[14], bf0[12], -cospi[50], bf0[13], bit);
    step[14] = d_half_btf(cospi[58], bf0[14], cospi[6], bf0[15], bit);
    step[15] = d_half_btf(cospi[6], bf0[14], -cospi[58], bf0[15], bit);

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

// svt_av1_transform_two_d_core_c at TX_16X16 (C7): shift {2,-2,0}
// (transforms.c:124), cos_bit col 13 / row 12 (fwd_cos_bit_col/row[2][2]);
// col pass x4 pre-shift, rounding >>2 post-col, row pass no post-shift.
// 16 threads, thread t = column for the col pass then row for the row pass.
extern "C" __global__ void fwd_txfm_2d_16x16(const short* input, const int* stride, const int* txType,
                                             int* output) {
    __shared__ int sbuf[256];
    const int t = threadIdx.x;
    int tmp[16];
    int o[16];
    for (int r = 0; r < 16; ++r) {
        tmp[r] = input[r * (*stride) + t] * 4;
    }
    if (*txType == 0) {
        d_fdct16(tmp, o, 13);
    } else {
        d_fadst16(tmp, o, 13);
    }
    for (int r = 0; r < 16; ++r) {
        sbuf[r * 16 + t] = d_round_shift(o[r], 2);
    }
    __syncthreads();
    for (int c = 0; c < 16; ++c) {
        tmp[c] = sbuf[t * 16 + c];
    }
    if (*txType == 0) {
        d_fdct16(tmp, o, 12);
    } else {
        d_fadst16(tmp, o, 12);
    }
    for (int c = 0; c < 16; ++c) {
        output[t * 16 + c] = o[c];
    }
}

// svt_av1_transform_two_d_core_c at TX_32X32 (L6): shift {2,-4,0}
// (transforms.c:125), cos_bit col 12 / row 12 (fwd_cos_bit_col/row[3][3]);
// col pass x4 pre-shift, rounding >>4 post-col, row pass no post-shift.
// 32 threads, thread t = column for the col pass then row for the row pass.
extern "C" __global__ void fwd_txfm_2d_32x32(const short* input, const int* stride, const int* txType,
                                             int* output) {
    __shared__ int sbuf[1024];
    const int t = threadIdx.x;
    int tmp[32];
    int o[32];
    for (int r = 0; r < 32; ++r) {
        tmp[r] = input[r * (*stride) + t] * 4;
    }
    if (*txType == 0) {
        d_fdct32(tmp, o);
    } else {
        d_fadst32(tmp, o);
    }
    for (int r = 0; r < 32; ++r) {
        sbuf[r * 32 + t] = d_round_shift(o[r], 4);
    }
    __syncthreads();
    for (int c = 0; c < 32; ++c) {
        tmp[c] = sbuf[t * 32 + c];
    }
    if (*txType == 0) {
        d_fdct32(tmp, o);
    } else {
        d_fadst32(tmp, o);
    }
    for (int c = 0; c < 32; ++c) {
        output[t * 32 + c] = o[c];
    }
}
)CUDB4";
    return s;
}

std::string invTxfmCuSource() {
    // built with += per chunk (see fwdTxfmCuSource note)
    std::string s;
    s += R"CUDA(
__constant__ int kC12[64] = {
    4096, 4095, 4091, 4085, 4076, 4065, 4052, 4036, 4017, 3996, 3973, 3948, 3920, 3889, 3857, 3822,
    3784, 3745, 3703, 3659, 3612, 3564, 3513, 3461, 3406, 3349, 3290, 3229, 3166, 3102, 3035, 2967,
    2896, 2824, 2751, 2675, 2598, 2520, 2440, 2359, 2276, 2191, 2106, 2019, 1931, 1842, 1751, 1660,
    1567, 1474, 1380, 1285, 1189, 1092, 995,  897,  799,  700,  601,  501,  401,  301,  201,  101,
};

__constant__ int kS12[5] = {0, 1321, 2482, 3344, 3803};

__device__ int d_half_btf(int w0, int in0, int w1, int in1, int bit) {
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
    step[0] = d_half_btf(kC12[32], bf[0], kC12[32], bf[1], bit);
    step[1] = d_half_btf(kC12[32], bf[0], -kC12[32], bf[1], bit);
    step[2] = d_half_btf(kC12[48], bf[2], -kC12[16], bf[3], bit);
    step[3] = d_half_btf(kC12[16], bf[2], kC12[48], bf[3], bit);
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
)CUDA";
    s += R"CUDB1(
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
    step[4] = d_half_btf(kC12[56], bf0[4], -kC12[8], bf0[7], bit);
    step[5] = d_half_btf(kC12[24], bf0[5], -kC12[40], bf0[6], bit);
    step[6] = d_half_btf(kC12[40], bf0[5], kC12[24], bf0[6], bit);
    step[7] = d_half_btf(kC12[8], bf0[4], kC12[56], bf0[7], bit);

    bf0[0] = d_half_btf(kC12[32], step[0], kC12[32], step[1], bit);
    bf0[1] = d_half_btf(kC12[32], step[0], -kC12[32], step[1], bit);
    bf0[2] = d_half_btf(kC12[48], step[2], -kC12[16], step[3], bit);
    bf0[3] = d_half_btf(kC12[16], step[2], kC12[48], step[3], bit);
    bf0[4] = d_cv(step[4] + step[5], 16);
    bf0[5] = d_cv(step[4] - step[5], 16);
    bf0[6] = d_cv(-step[6] + step[7], 16);
    bf0[7] = d_cv(step[6] + step[7], 16);

    step[0] = d_cv(bf0[0] + bf0[3], 16);
    step[1] = d_cv(bf0[1] + bf0[2], 16);
    step[2] = d_cv(bf0[1] - bf0[2], 16);
    step[3] = d_cv(bf0[0] - bf0[3], 16);
    step[4] = bf0[4];
    step[5] = d_half_btf(-kC12[32], bf0[5], kC12[32], bf0[6], bit);
    step[6] = d_half_btf(kC12[32], bf0[5], kC12[32], bf0[6], bit);
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

    step[0] = d_half_btf(kC12[4], bf0[0], kC12[60], bf0[1], bit);
    step[1] = d_half_btf(kC12[60], bf0[0], -kC12[4], bf0[1], bit);
    step[2] = d_half_btf(kC12[20], bf0[2], kC12[44], bf0[3], bit);
    step[3] = d_half_btf(kC12[44], bf0[2], -kC12[20], bf0[3], bit);
    step[4] = d_half_btf(kC12[36], bf0[4], kC12[28], bf0[5], bit);
    step[5] = d_half_btf(kC12[28], bf0[4], -kC12[36], bf0[5], bit);
    step[6] = d_half_btf(kC12[52], bf0[6], kC12[12], bf0[7], bit);
    step[7] = d_half_btf(kC12[12], bf0[6], -kC12[52], bf0[7], bit);

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
    step[4] = d_half_btf(kC12[16], bf0[4], kC12[48], bf0[5], bit);
    step[5] = d_half_btf(kC12[48], bf0[4], -kC12[16], bf0[5], bit);
    step[6] = d_half_btf(-kC12[48], bf0[6], kC12[16], bf0[7], bit);
    step[7] = d_half_btf(kC12[16], bf0[6], kC12[48], bf0[7], bit);

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
    step[2] = d_half_btf(kC12[32], bf0[2], kC12[32], bf0[3], bit);
    step[3] = d_half_btf(kC12[32], bf0[2], -kC12[32], bf0[3], bit);
    step[4] = bf0[4];
    step[5] = bf0[5];
    step[6] = d_half_btf(kC12[32], bf0[6], kC12[32], bf0[7], bit);
    step[7] = d_half_btf(kC12[32], bf0[6], -kC12[32], bf0[7], bit);

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
    s += R"CUDB2(
// svt_av1_idct16_new (inv_transforms.c:215-376) at cos_bit 12: stage_range
// consumed at stages 3-7 only (clamp_value at 16), stages 1-2 unclamped
__device__ void d_idct16i(const int* input, int* output) {
    const int bit = 12;
    int bf0[16];
    int step[16];

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

    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = bf0[2];
    step[3]  = bf0[3];
    step[4]  = bf0[4];
    step[5]  = bf0[5];
    step[6]  = bf0[6];
    step[7]  = bf0[7];
    step[8]  = d_half_btf(kC12[60], bf0[8], -kC12[4], bf0[15], bit);
    step[9]  = d_half_btf(kC12[28], bf0[9], -kC12[36], bf0[14], bit);
    step[10] = d_half_btf(kC12[44], bf0[10], -kC12[20], bf0[13], bit);
    step[11] = d_half_btf(kC12[12], bf0[11], -kC12[52], bf0[12], bit);
    step[12] = d_half_btf(kC12[52], bf0[11], kC12[12], bf0[12], bit);
    step[13] = d_half_btf(kC12[20], bf0[10], kC12[44], bf0[13], bit);
    step[14] = d_half_btf(kC12[36], bf0[9], kC12[28], bf0[14], bit);
    step[15] = d_half_btf(kC12[4], bf0[8], kC12[60], bf0[15], bit);

    bf0[0]  = step[0];
    bf0[1]  = step[1];
    bf0[2]  = step[2];
    bf0[3]  = step[3];
    bf0[4]  = d_half_btf(kC12[56], step[4], -kC12[8], step[7], bit);
    bf0[5]  = d_half_btf(kC12[24], step[5], -kC12[40], step[6], bit);
    bf0[6]  = d_half_btf(kC12[40], step[5], kC12[24], step[6], bit);
    bf0[7]  = d_half_btf(kC12[8], step[4], kC12[56], step[7], bit);
    bf0[8]  = d_cv(step[8] + step[9], 16);
    bf0[9]  = d_cv(step[8] - step[9], 16);
    bf0[10] = d_cv(-step[10] + step[11], 16);
    bf0[11] = d_cv(step[10] + step[11], 16);
    bf0[12] = d_cv(step[12] + step[13], 16);
    bf0[13] = d_cv(step[12] - step[13], 16);
    bf0[14] = d_cv(-step[14] + step[15], 16);
    bf0[15] = d_cv(step[14] + step[15], 16);

    step[0]  = d_half_btf(kC12[32], bf0[0], kC12[32], bf0[1], bit);
    step[1]  = d_half_btf(kC12[32], bf0[0], -kC12[32], bf0[1], bit);
    step[2]  = d_half_btf(kC12[48], bf0[2], -kC12[16], bf0[3], bit);
    step[3]  = d_half_btf(kC12[16], bf0[2], kC12[48], bf0[3], bit);
    step[4]  = d_cv(bf0[4] + bf0[5], 16);
    step[5]  = d_cv(bf0[4] - bf0[5], 16);
    step[6]  = d_cv(-bf0[6] + bf0[7], 16);
    step[7]  = d_cv(bf0[6] + bf0[7], 16);
    step[8]  = bf0[8];
    step[9]  = d_half_btf(-kC12[16], bf0[9], kC12[48], bf0[14], bit);
    step[10] = d_half_btf(-kC12[48], bf0[10], -kC12[16], bf0[13], bit);
    step[11] = bf0[11];
    step[12] = bf0[12];
    step[13] = d_half_btf(-kC12[16], bf0[10], kC12[48], bf0[13], bit);
    step[14] = d_half_btf(kC12[48], bf0[9], kC12[16], bf0[14], bit);
    step[15] = bf0[15];

    bf0[0]  = d_cv(step[0] + step[3], 16);
    bf0[1]  = d_cv(step[1] + step[2], 16);
    bf0[2]  = d_cv(step[1] - step[2], 16);
    bf0[3]  = d_cv(step[0] - step[3], 16);
    bf0[4]  = step[4];
    bf0[5]  = d_half_btf(-kC12[32], step[5], kC12[32], step[6], bit);
    bf0[6]  = d_half_btf(kC12[32], step[5], kC12[32], step[6], bit);
    bf0[7]  = step[7];
    bf0[8]  = d_cv(step[8] + step[11], 16);
    bf0[9]  = d_cv(step[9] + step[10], 16);
    bf0[10] = d_cv(step[9] - step[10], 16);
    bf0[11] = d_cv(step[8] - step[11], 16);
    bf0[12] = d_cv(-step[12] + step[15], 16);
    bf0[13] = d_cv(-step[13] + step[14], 16);
    bf0[14] = d_cv(step[13] + step[14], 16);
    bf0[15] = d_cv(step[12] + step[15], 16);

    step[0]  = d_cv(bf0[0] + bf0[7], 16);
    step[1]  = d_cv(bf0[1] + bf0[6], 16);
    step[2]  = d_cv(bf0[2] + bf0[5], 16);
    step[3]  = d_cv(bf0[3] + bf0[4], 16);
    step[4]  = d_cv(bf0[3] - bf0[4], 16);
    step[5]  = d_cv(bf0[2] - bf0[5], 16);
    step[6]  = d_cv(bf0[1] - bf0[6], 16);
    step[7]  = d_cv(bf0[0] - bf0[7], 16);
    step[8]  = bf0[8];
    step[9]  = bf0[9];
    step[10] = d_half_btf(-kC12[32], bf0[10], kC12[32], bf0[13], bit);
    step[11] = d_half_btf(-kC12[32], bf0[11], kC12[32], bf0[12], bit);
    step[12] = d_half_btf(kC12[32], bf0[11], kC12[32], bf0[12], bit);
    step[13] = d_half_btf(kC12[32], bf0[10], kC12[32], bf0[13], bit);
    step[14] = bf0[14];
    step[15] = bf0[15];

    output[0]  = d_cv(step[0] + step[15], 16);
    output[1]  = d_cv(step[1] + step[14], 16);
    output[2]  = d_cv(step[2] + step[13], 16);
    output[3]  = d_cv(step[3] + step[12], 16);
    output[4]  = d_cv(step[4] + step[11], 16);
    output[5]  = d_cv(step[5] + step[10], 16);
    output[6]  = d_cv(step[6] + step[9], 16);
    output[7]  = d_cv(step[7] + step[8], 16);
    output[8]  = d_cv(step[7] - step[8], 16);
    output[9]  = d_cv(step[6] - step[9], 16);
    output[10] = d_cv(step[5] - step[10], 16);
    output[11] = d_cv(step[4] - step[11], 16);
    output[12] = d_cv(step[3] - step[12], 16);
    output[13] = d_cv(step[2] - step[13], 16);
    output[14] = d_cv(step[1] - step[14], 16);
    output[15] = d_cv(step[0] - step[15], 16);
}

// svt_av1_iadst16_new (inv_transforms.c:927-1130) at cos_bit 12; stage_range
// consumed at stages 3/5/7; NO all-zero early-out at 16
__device__ void d_iadst16i(const int* input, int* output) {
    const int bit = 12;
    int bf0[16];
    int step[16];

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

    step[0]  = d_half_btf(kC12[2], bf0[0], kC12[62], bf0[1], bit);
    step[1]  = d_half_btf(kC12[62], bf0[0], -kC12[2], bf0[1], bit);
    step[2]  = d_half_btf(kC12[10], bf0[2], kC12[54], bf0[3], bit);
    step[3]  = d_half_btf(kC12[54], bf0[2], -kC12[10], bf0[3], bit);
    step[4]  = d_half_btf(kC12[18], bf0[4], kC12[46], bf0[5], bit);
    step[5]  = d_half_btf(kC12[46], bf0[4], -kC12[18], bf0[5], bit);
    step[6]  = d_half_btf(kC12[26], bf0[6], kC12[38], bf0[7], bit);
    step[7]  = d_half_btf(kC12[38], bf0[6], -kC12[26], bf0[7], bit);
    step[8]  = d_half_btf(kC12[34], bf0[8], kC12[30], bf0[9], bit);
    step[9]  = d_half_btf(kC12[30], bf0[8], -kC12[34], bf0[9], bit);
    step[10] = d_half_btf(kC12[42], bf0[10], kC12[22], bf0[11], bit);
    step[11] = d_half_btf(kC12[22], bf0[10], -kC12[42], bf0[11], bit);
    step[12] = d_half_btf(kC12[50], bf0[12], kC12[14], bf0[13], bit);
    step[13] = d_half_btf(kC12[14], bf0[12], -kC12[50], bf0[13], bit);
    step[14] = d_half_btf(kC12[58], bf0[14], kC12[6], bf0[15], bit);
    step[15] = d_half_btf(kC12[6], bf0[14], -kC12[58], bf0[15], bit);

    bf0[0]  = d_cv(step[0] + step[8], 16);
    bf0[1]  = d_cv(step[1] + step[9], 16);
    bf0[2]  = d_cv(step[2] + step[10], 16);
    bf0[3]  = d_cv(step[3] + step[11], 16);
    bf0[4]  = d_cv(step[4] + step[12], 16);
    bf0[5]  = d_cv(step[5] + step[13], 16);
    bf0[6]  = d_cv(step[6] + step[14], 16);
    bf0[7]  = d_cv(step[7] + step[15], 16);
    bf0[8]  = d_cv(step[0] - step[8], 16);
    bf0[9]  = d_cv(step[1] - step[9], 16);
    bf0[10] = d_cv(step[2] - step[10], 16);
    bf0[11] = d_cv(step[3] - step[11], 16);
    bf0[12] = d_cv(step[4] - step[12], 16);
    bf0[13] = d_cv(step[5] - step[13], 16);
    bf0[14] = d_cv(step[6] - step[14], 16);
    bf0[15] = d_cv(step[7] - step[15], 16);

    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = bf0[2];
    step[3]  = bf0[3];
    step[4]  = bf0[4];
    step[5]  = bf0[5];
    step[6]  = bf0[6];
    step[7]  = bf0[7];
    step[8]  = d_half_btf(kC12[8], bf0[8], kC12[56], bf0[9], bit);
    step[9]  = d_half_btf(kC12[56], bf0[8], -kC12[8], bf0[9], bit);
    step[10] = d_half_btf(kC12[40], bf0[10], kC12[24], bf0[11], bit);
    step[11] = d_half_btf(kC12[24], bf0[10], -kC12[40], bf0[11], bit);
    step[12] = d_half_btf(-kC12[56], bf0[12], kC12[8], bf0[13], bit);
    step[13] = d_half_btf(kC12[8], bf0[12], kC12[56], bf0[13], bit);
    step[14] = d_half_btf(-kC12[24], bf0[14], kC12[40], bf0[15], bit);
    step[15] = d_half_btf(kC12[40], bf0[14], kC12[24], bf0[15], bit);

    bf0[0]  = d_cv(step[0] + step[4], 16);
    bf0[1]  = d_cv(step[1] + step[5], 16);
    bf0[2]  = d_cv(step[2] + step[6], 16);
    bf0[3]  = d_cv(step[3] + step[7], 16);
    bf0[4]  = d_cv(step[0] - step[4], 16);
    bf0[5]  = d_cv(step[1] - step[5], 16);
    bf0[6]  = d_cv(step[2] - step[6], 16);
    bf0[7]  = d_cv(step[3] - step[7], 16);
    bf0[8]  = d_cv(step[8] + step[12], 16);
    bf0[9]  = d_cv(step[9] + step[13], 16);
    bf0[10] = d_cv(step[10] + step[14], 16);
    bf0[11] = d_cv(step[11] + step[15], 16);
    bf0[12] = d_cv(step[8] - step[12], 16);
    bf0[13] = d_cv(step[9] - step[13], 16);
    bf0[14] = d_cv(step[10] - step[14], 16);
    bf0[15] = d_cv(step[11] - step[15], 16);

    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = bf0[2];
    step[3]  = bf0[3];
    step[4]  = d_half_btf(kC12[16], bf0[4], kC12[48], bf0[5], bit);
    step[5]  = d_half_btf(kC12[48], bf0[4], -kC12[16], bf0[5], bit);
    step[6]  = d_half_btf(-kC12[48], bf0[6], kC12[16], bf0[7], bit);
    step[7]  = d_half_btf(kC12[16], bf0[6], kC12[48], bf0[7], bit);
    step[8]  = bf0[8];
    step[9]  = bf0[9];
    step[10] = bf0[10];
    step[11] = bf0[11];
    step[12] = d_half_btf(kC12[16], bf0[12], kC12[48], bf0[13], bit);
    step[13] = d_half_btf(kC12[48], bf0[12], -kC12[16], bf0[13], bit);
    step[14] = d_half_btf(-kC12[48], bf0[14], kC12[16], bf0[15], bit);
    step[15] = d_half_btf(kC12[16], bf0[14], kC12[48], bf0[15], bit);

    bf0[0]  = d_cv(step[0] + step[2], 16);
    bf0[1]  = d_cv(step[1] + step[3], 16);
    bf0[2]  = d_cv(step[0] - step[2], 16);
    bf0[3]  = d_cv(step[1] - step[3], 16);
    bf0[4]  = d_cv(step[4] + step[6], 16);
    bf0[5]  = d_cv(step[5] + step[7], 16);
    bf0[6]  = d_cv(step[4] - step[6], 16);
    bf0[7]  = d_cv(step[5] - step[7], 16);
    bf0[8]  = d_cv(step[8] + step[10], 16);
    bf0[9]  = d_cv(step[9] + step[11], 16);
    bf0[10] = d_cv(step[8] - step[10], 16);
    bf0[11] = d_cv(step[9] - step[11], 16);
    bf0[12] = d_cv(step[12] + step[14], 16);
    bf0[13] = d_cv(step[13] + step[15], 16);
    bf0[14] = d_cv(step[12] - step[14], 16);
    bf0[15] = d_cv(step[13] - step[15], 16);

    step[0]  = bf0[0];
    step[1]  = bf0[1];
    step[2]  = d_half_btf(kC12[32], bf0[2], kC12[32], bf0[3], bit);
    step[3]  = d_half_btf(kC12[32], bf0[2], -kC12[32], bf0[3], bit);
    step[4]  = bf0[4];
    step[5]  = bf0[5];
    step[6]  = d_half_btf(kC12[32], bf0[6], kC12[32], bf0[7], bit);
    step[7]  = d_half_btf(kC12[32], bf0[6], -kC12[32], bf0[7], bit);
    step[8]  = bf0[8];
    step[9]  = bf0[9];
    step[10] = d_half_btf(kC12[32], bf0[10], kC12[32], bf0[11], bit);
    step[11] = d_half_btf(kC12[32], bf0[10], -kC12[32], bf0[11], bit);
    step[12] = bf0[12];
    step[13] = bf0[13];
    step[14] = d_half_btf(kC12[32], bf0[14], kC12[32], bf0[15], bit);
    step[15] = d_half_btf(kC12[32], bf0[14], -kC12[32], bf0[15], bit);

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

// svt_av1_inv_txfm2d_add_c at TX_16X16 (C7): inv_shift_16x16 = {-2,-4} ->
// rounding >>2 after the row 1D and >>4 at the final add; clamp bit 16 at
// both 1D inputs; 16 threads, thread t = row for the row pass then column
// for the col pass.
extern "C" __global__ void inv_txfm_2d_add_16x16(const int* coeffs, const int* txType,
                                                 unsigned char* dst, const int* stride) {
    __shared__ int sbuf[256];
    const int t = threadIdx.x;
    int tmp[16];
    int o[16];
    for (int c = 0; c < 16; ++c) {
        tmp[c] = d_cv(coeffs[t * 16 + c], 16);
    }
    if (*txType == 0) {
        d_idct16i(tmp, o);
    } else {
        d_iadst16i(tmp, o);
    }
    for (int c = 0; c < 16; ++c) {
        sbuf[t * 16 + c] = d_rs(o[c], 2);
    }
    __syncthreads();
    for (int r = 0; r < 16; ++r) {
        tmp[r] = d_cv(sbuf[r * 16 + t], 16);
    }
    if (*txType == 0) {
        d_idct16i(tmp, o);
    } else {
        d_iadst16i(tmp, o);
    }
    for (int r = 0; r < 16; ++r) {
        int v = (int)dst[r * (*stride) + t] + d_rs(o[r], 4);
        if (v < 0) v = 0;
        else if (v > 255) v = 255;
        dst[r * (*stride) + t] = (unsigned char)v;
    }
}

)CUDB2";
    s += R"CUDB3(
// svt_av1_idct32_new (inv_transforms.c:378-727) at cos_bit 12: clamp at
// stages 3-9 only (the L3-audited pattern)
__device__ void d_idct32i(const int* input, int* output) {
    const int bit = 12;
    const int* cospi = kC12;
    int bf0[32];
    int step[32];
    const int cr = 16;  // clamp range

    bf0[0]  = input[0];
    bf0[1]  = input[16];
    bf0[2]  = input[8];
    bf0[3]  = input[24];
    bf0[4]  = input[4];
    bf0[5]  = input[20];
    bf0[6]  = input[12];
    bf0[7]  = input[28];
    bf0[8]  = input[2];
    bf0[9]  = input[18];
    bf0[10] = input[10];
    bf0[11] = input[26];
    bf0[12] = input[6];
    bf0[13] = input[22];
    bf0[14] = input[14];
    bf0[15] = input[30];
    bf0[16] = input[1];
    bf0[17] = input[17];
    bf0[18] = input[9];
    bf0[19] = input[25];
    bf0[20] = input[5];
    bf0[21] = input[21];
    bf0[22] = input[13];
    bf0[23] = input[29];
    bf0[24] = input[3];
    bf0[25] = input[19];
    bf0[26] = input[11];
    bf0[27] = input[27];
    bf0[28] = input[7];
    bf0[29] = input[23];
    bf0[30] = input[15];
    bf0[31] = input[31];

    for (int i = 0; i < 16; ++i) step[i] = bf0[i];
    step[16] = d_half_btf(cospi[62], bf0[16], -cospi[2], bf0[31], bit);
    step[17] = d_half_btf(cospi[30], bf0[17], -cospi[34], bf0[30], bit);
    step[18] = d_half_btf(cospi[46], bf0[18], -cospi[18], bf0[29], bit);
    step[19] = d_half_btf(cospi[14], bf0[19], -cospi[50], bf0[28], bit);
    step[20] = d_half_btf(cospi[54], bf0[20], -cospi[10], bf0[27], bit);
    step[21] = d_half_btf(cospi[22], bf0[21], -cospi[42], bf0[26], bit);
    step[22] = d_half_btf(cospi[38], bf0[22], -cospi[26], bf0[25], bit);
    step[23] = d_half_btf(cospi[6], bf0[23], -cospi[58], bf0[24], bit);
    step[24] = d_half_btf(cospi[58], bf0[23], cospi[6], bf0[24], bit);
    step[25] = d_half_btf(cospi[26], bf0[22], cospi[38], bf0[25], bit);
    step[26] = d_half_btf(cospi[42], bf0[21], cospi[22], bf0[26], bit);
    step[27] = d_half_btf(cospi[10], bf0[20], cospi[54], bf0[27], bit);
    step[28] = d_half_btf(cospi[50], bf0[19], cospi[14], bf0[28], bit);
    step[29] = d_half_btf(cospi[18], bf0[18], cospi[46], bf0[29], bit);
    step[30] = d_half_btf(cospi[34], bf0[17], cospi[30], bf0[30], bit);
    step[31] = d_half_btf(cospi[2], bf0[16], cospi[62], bf0[31], bit);

    for (int i = 0; i < 8; ++i) bf0[i] = step[i];
    bf0[8]  = d_half_btf(cospi[60], step[8], -cospi[4], step[15], bit);
    bf0[9]  = d_half_btf(cospi[28], step[9], -cospi[36], step[14], bit);
    bf0[10] = d_half_btf(cospi[44], step[10], -cospi[20], step[13], bit);
    bf0[11] = d_half_btf(cospi[12], step[11], -cospi[52], step[12], bit);
    bf0[12] = d_half_btf(cospi[52], step[11], cospi[12], step[12], bit);
    bf0[13] = d_half_btf(cospi[20], step[10], cospi[44], step[13], bit);
    bf0[14] = d_half_btf(cospi[36], step[9], cospi[28], step[14], bit);
    bf0[15] = d_half_btf(cospi[4], step[8], cospi[60], step[15], bit);
    bf0[16] = d_cv(step[16] + step[17], cr);
    bf0[17] = d_cv(step[16] - step[17], cr);
    bf0[18] = d_cv(-step[18] + step[19], cr);
    bf0[19] = d_cv(step[18] + step[19], cr);
    bf0[20] = d_cv(step[20] + step[21], cr);
    bf0[21] = d_cv(step[20] - step[21], cr);
    bf0[22] = d_cv(-step[22] + step[23], cr);
    bf0[23] = d_cv(step[22] + step[23], cr);
    bf0[24] = d_cv(step[24] + step[25], cr);
    bf0[25] = d_cv(step[24] - step[25], cr);
    bf0[26] = d_cv(-step[26] + step[27], cr);
    bf0[27] = d_cv(step[26] + step[27], cr);
    bf0[28] = d_cv(step[28] + step[29], cr);
    bf0[29] = d_cv(step[28] - step[29], cr);
    bf0[30] = d_cv(-step[30] + step[31], cr);
    bf0[31] = d_cv(step[30] + step[31], cr);

    step[0] = bf0[0];
    step[1] = bf0[1];
    step[2] = bf0[2];
    step[3] = bf0[3];
    step[4] = d_half_btf(cospi[56], bf0[4], -cospi[8], bf0[7], bit);
    step[5] = d_half_btf(cospi[24], bf0[5], -cospi[40], bf0[6], bit);
    step[6] = d_half_btf(cospi[40], bf0[5], cospi[24], bf0[6], bit);
    step[7] = d_half_btf(cospi[8], bf0[4], cospi[56], bf0[7], bit);
    step[8]  = d_cv(bf0[8] + bf0[9], cr);
    step[9]  = d_cv(bf0[8] - bf0[9], cr);
    step[10] = d_cv(-bf0[10] + bf0[11], cr);
    step[11] = d_cv(bf0[10] + bf0[11], cr);
    step[12] = d_cv(bf0[12] + bf0[13], cr);
    step[13] = d_cv(bf0[12] - bf0[13], cr);
    step[14] = d_cv(-bf0[14] + bf0[15], cr);
    step[15] = d_cv(bf0[14] + bf0[15], cr);
    step[16] = bf0[16];
    step[17] = d_half_btf(-cospi[8], bf0[17], cospi[56], bf0[30], bit);
    step[18] = d_half_btf(-cospi[56], bf0[18], -cospi[8], bf0[29], bit);
    step[19] = bf0[19];
    step[20] = bf0[20];
    step[21] = d_half_btf(-cospi[40], bf0[21], cospi[24], bf0[26], bit);
    step[22] = d_half_btf(-cospi[24], bf0[22], -cospi[40], bf0[25], bit);
    step[23] = bf0[23];
    step[24] = bf0[24];
    step[25] = d_half_btf(-cospi[40], bf0[22], cospi[24], bf0[25], bit);
    step[26] = d_half_btf(cospi[24], bf0[21], cospi[40], bf0[26], bit);
    step[27] = bf0[27];
    step[28] = bf0[28];
    step[29] = d_half_btf(-cospi[8], bf0[18], cospi[56], bf0[29], bit);
    step[30] = d_half_btf(cospi[56], bf0[17], cospi[8], bf0[30], bit);
    step[31] = bf0[31];

    bf0[0] = d_half_btf(cospi[32], step[0], cospi[32], step[1], bit);
    bf0[1] = d_half_btf(cospi[32], step[0], -cospi[32], step[1], bit);
    bf0[2] = d_half_btf(cospi[48], step[2], -cospi[16], step[3], bit);
    bf0[3] = d_half_btf(cospi[16], step[2], cospi[48], step[3], bit);
    bf0[4]  = d_cv(step[4] + step[5], cr);
    bf0[5]  = d_cv(step[4] - step[5], cr);
    bf0[6]  = d_cv(-step[6] + step[7], cr);
    bf0[7]  = d_cv(step[6] + step[7], cr);
    bf0[8] = step[8];
    bf0[9]  = d_half_btf(-cospi[16], step[9], cospi[48], step[14], bit);
    bf0[10] = d_half_btf(-cospi[48], step[10], -cospi[16], step[13], bit);
    bf0[11] = step[11];
    bf0[12] = step[12];
    bf0[13] = d_half_btf(-cospi[16], step[10], cospi[48], step[13], bit);
    bf0[14] = d_half_btf(cospi[48], step[9], cospi[16], step[14], bit);
    bf0[15] = step[15];
    bf0[16] = d_cv(step[16] + step[19], cr);
    bf0[17] = d_cv(step[17] + step[18], cr);
    bf0[18] = d_cv(step[17] - step[18], cr);
    bf0[19] = d_cv(step[16] - step[19], cr);
    bf0[20] = d_cv(-step[20] + step[23], cr);
    bf0[21] = d_cv(-step[21] + step[22], cr);
    bf0[22] = d_cv(step[21] + step[22], cr);
    bf0[23] = d_cv(step[20] + step[23], cr);
    bf0[24] = d_cv(step[24] + step[27], cr);
    bf0[25] = d_cv(step[25] + step[26], cr);
    bf0[26] = d_cv(step[25] - step[26], cr);
    bf0[27] = d_cv(step[24] - step[27], cr);
    bf0[28] = d_cv(-step[28] + step[31], cr);
    bf0[29] = d_cv(-step[29] + step[30], cr);
    bf0[30] = d_cv(step[29] + step[30], cr);
    bf0[31] = d_cv(step[28] + step[31], cr);

    step[0]  = d_cv(bf0[0] + bf0[3], cr);
    step[1]  = d_cv(bf0[1] + bf0[2], cr);
    step[2]  = d_cv(bf0[1] - bf0[2], cr);
    step[3]  = d_cv(bf0[0] - bf0[3], cr);
    step[4] = bf0[4];
    step[5] = d_half_btf(-cospi[32], bf0[5], cospi[32], bf0[6], bit);
    step[6] = d_half_btf(cospi[32], bf0[5], cospi[32], bf0[6], bit);
    step[7] = bf0[7];
    step[8]  = d_cv(bf0[8] + bf0[11], cr);
    step[9]  = d_cv(bf0[9] + bf0[10], cr);
    step[10] = d_cv(bf0[9] - bf0[10], cr);
    step[11] = d_cv(bf0[8] - bf0[11], cr);
    step[12] = d_cv(-bf0[12] + bf0[15], cr);
    step[13] = d_cv(-bf0[13] + bf0[14], cr);
    step[14] = d_cv(bf0[13] + bf0[14], cr);
    step[15] = d_cv(bf0[12] + bf0[15], cr);
    step[16] = bf0[16];
    step[17] = bf0[17];
    step[18] = d_half_btf(-cospi[16], bf0[18], cospi[48], bf0[29], bit);
    step[19] = d_half_btf(-cospi[16], bf0[19], cospi[48], bf0[28], bit);
    step[20] = d_half_btf(-cospi[48], bf0[20], -cospi[16], bf0[27], bit);
    step[21] = d_half_btf(-cospi[48], bf0[21], -cospi[16], bf0[26], bit);
    step[22] = bf0[22];
    step[23] = bf0[23];
    step[24] = bf0[24];
    step[25] = bf0[25];
    step[26] = d_half_btf(-cospi[16], bf0[21], cospi[48], bf0[26], bit);
    step[27] = d_half_btf(-cospi[16], bf0[20], cospi[48], bf0[27], bit);
    step[28] = d_half_btf(cospi[48], bf0[19], cospi[16], bf0[28], bit);
    step[29] = d_half_btf(cospi[48], bf0[18], cospi[16], bf0[29], bit);
    step[30] = bf0[30];
    step[31] = bf0[31];

    bf0[0]  = d_cv(step[0] + step[7], cr);
    bf0[1]  = d_cv(step[1] + step[6], cr);
    bf0[2]  = d_cv(step[2] + step[5], cr);
    bf0[3]  = d_cv(step[3] + step[4], cr);
    bf0[4]  = d_cv(step[3] - step[4], cr);
    bf0[5]  = d_cv(step[2] - step[5], cr);
    bf0[6]  = d_cv(step[1] - step[6], cr);
    bf0[7]  = d_cv(step[0] - step[7], cr);
    bf0[8] = step[8];
    bf0[9] = step[9];
    bf0[10] = d_half_btf(-cospi[32], step[10], cospi[32], step[13], bit);
    bf0[11] = d_half_btf(-cospi[32], step[11], cospi[32], step[12], bit);
    bf0[12] = d_half_btf(cospi[32], step[11], cospi[32], step[12], bit);
    bf0[13] = d_half_btf(cospi[32], step[10], cospi[32], step[13], bit);
    bf0[14] = step[14];
    bf0[15] = step[15];
    bf0[16] = d_cv(step[16] + step[23], cr);
    bf0[17] = d_cv(step[17] + step[22], cr);
    bf0[18] = d_cv(step[18] + step[21], cr);
    bf0[19] = d_cv(step[19] + step[20], cr);
    bf0[20] = d_cv(step[19] - step[20], cr);
    bf0[21] = d_cv(step[18] - step[21], cr);
    bf0[22] = d_cv(step[17] - step[22], cr);
    bf0[23] = d_cv(step[16] - step[23], cr);
    bf0[24] = d_cv(-step[24] + step[31], cr);
    bf0[25] = d_cv(-step[25] + step[30], cr);
    bf0[26] = d_cv(-step[26] + step[29], cr);
    bf0[27] = d_cv(-step[27] + step[28], cr);
    bf0[28] = d_cv(step[27] + step[28], cr);
    bf0[29] = d_cv(step[26] + step[29], cr);
    bf0[30] = d_cv(step[25] + step[30], cr);
    bf0[31] = d_cv(step[24] + step[31], cr);

    for (int i = 0; i < 8; ++i) {
        step[i] = d_cv(bf0[i] + bf0[15 - i], cr);
        step[8 + i] = d_cv(bf0[7 - i] - bf0[8 + i], cr);
    }
    for (int i = 0; i < 16; ++i) step[16 + i] = bf0[16 + i];
    step[20] = d_half_btf(-cospi[32], bf0[20], cospi[32], bf0[27], bit);
    step[21] = d_half_btf(-cospi[32], bf0[21], cospi[32], bf0[26], bit);
    step[22] = d_half_btf(-cospi[32], bf0[22], cospi[32], bf0[25], bit);
    step[23] = d_half_btf(-cospi[32], bf0[23], cospi[32], bf0[24], bit);
    step[24] = d_half_btf(cospi[32], bf0[23], cospi[32], bf0[24], bit);
    step[25] = d_half_btf(cospi[32], bf0[22], cospi[32], bf0[25], bit);
    step[26] = d_half_btf(cospi[32], bf0[21], cospi[32], bf0[26], bit);
    step[27] = d_half_btf(cospi[32], bf0[20], cospi[32], bf0[27], bit);

    for (int i = 0; i < 16; ++i) {
        output[i] = d_cv(step[i] + step[31 - i], cr);
        output[31 - i] = d_cv(step[i] - step[31 - i], cr);
    }
}

)CUDB3";
    s += R"CUDB4(
// static av1_iadst32_new (inv_transforms.c:1132-1565) at cos_bit 12: clamp
// every stage (the L3-audited pattern)
__device__ void d_iadst32i(const int* input, int* output) {
    const int bit = 12;
    const int* cospi = kC12;
    int bf0[32];
    int step[32];
    const int cr = 16;

    static const int idxTab[32] = {0, -32, -16, 16, -8, 24, 8, -24, -4, 28, 12, -20, 4, -28, -12, 20,
                                   -2, 30, 14, -18, 6, -26, -10, 22, 2, -30, -14, 18, -6, 26, 10, -22};
    for (int i = 0; i < 32; ++i) {
        const int j = idxTab[i];
        bf0[i] = j >= 0 ? input[j] : -input[-1 - j];
    }
    for (int i = 0; i < 32; ++i) bf0[i] = d_cv(bf0[i], cr);

    for (int q = 0; q < 16; ++q) {
        const int base = 2 * q;
        if (base % 4 == 0) {
            step[base]     = bf0[base];
            step[base + 1] = bf0[base + 1];
        } else {
            step[base]     = d_half_btf(cospi[32], bf0[base], cospi[32], bf0[base + 1], bit);
            step[base + 1] = d_half_btf(cospi[32], bf0[base], -cospi[32], bf0[base + 1], bit);
        }
    }
    for (int i = 0; i < 32; ++i) step[i] = d_cv(step[i], cr);

    for (int q = 0; q < 8; ++q) {
        const int base = 4 * q;
        bf0[base]     = step[base] + step[base + 2];
        bf0[base + 1] = step[base + 1] + step[base + 3];
        bf0[base + 2] = step[base] - step[base + 2];
        bf0[base + 3] = step[base + 1] - step[base + 3];
    }
    for (int i = 0; i < 32; ++i) bf0[i] = d_cv(bf0[i], cr);

    for (int q = 0; q < 4; ++q) {
        const int base = 8 * q;
        step[base]     = bf0[base];
        step[base + 1] = bf0[base + 1];
        step[base + 2] = bf0[base + 2];
        step[base + 3] = bf0[base + 3];
        step[base + 4] = d_half_btf(cospi[16], bf0[base + 4], cospi[48], bf0[base + 5], bit);
        step[base + 5] = d_half_btf(cospi[48], bf0[base + 4], -cospi[16], bf0[base + 5], bit);
        step[base + 6] = d_half_btf(-cospi[48], bf0[base + 6], cospi[16], bf0[base + 7], bit);
        step[base + 7] = d_half_btf(cospi[16], bf0[base + 6], cospi[48], bf0[base + 7], bit);
    }
    for (int i = 0; i < 32; ++i) step[i] = d_cv(step[i], cr);

    for (int q = 0; q < 4; ++q) {
        const int base = 8 * q;
        for (int i = 0; i < 4; ++i) {
            bf0[base + i] = step[base + i] + step[base + 4 + i];
            bf0[base + 4 + i] = step[base + i] - step[base + 4 + i];
        }
    }
    for (int i = 0; i < 32; ++i) bf0[i] = d_cv(bf0[i], cr);

    for (int i = 0; i < 8; ++i) step[i] = bf0[i];
    step[8]  = d_half_btf(cospi[8], bf0[8], cospi[56], bf0[9], bit);
    step[9]  = d_half_btf(cospi[56], bf0[8], -cospi[8], bf0[9], bit);
    step[10] = d_half_btf(cospi[40], bf0[10], cospi[24], bf0[11], bit);
    step[11] = d_half_btf(cospi[24], bf0[10], -cospi[40], bf0[11], bit);
    step[12] = d_half_btf(-cospi[56], bf0[12], cospi[8], bf0[13], bit);
    step[13] = d_half_btf(cospi[8], bf0[12], cospi[56], bf0[13], bit);
    step[14] = d_half_btf(-cospi[24], bf0[14], cospi[40], bf0[15], bit);
    step[15] = d_half_btf(cospi[40], bf0[14], cospi[24], bf0[15], bit);
    for (int i = 16; i < 24; ++i) step[i] = bf0[i];
    step[24] = d_half_btf(cospi[8], bf0[24], cospi[56], bf0[25], bit);
    step[25] = d_half_btf(cospi[56], bf0[24], -cospi[8], bf0[25], bit);
    step[26] = d_half_btf(cospi[40], bf0[26], cospi[24], bf0[27], bit);
    step[27] = d_half_btf(cospi[24], bf0[26], -cospi[40], bf0[27], bit);
    step[28] = d_half_btf(-cospi[56], bf0[28], cospi[8], bf0[29], bit);
    step[29] = d_half_btf(cospi[8], bf0[28], cospi[56], bf0[29], bit);
    step[30] = d_half_btf(-cospi[24], bf0[30], cospi[40], bf0[31], bit);
    step[31] = d_half_btf(cospi[40], bf0[30], cospi[24], bf0[31], bit);
    for (int i = 0; i < 32; ++i) step[i] = d_cv(step[i], cr);

    for (int h = 0; h < 2; ++h) {
        const int base = 16 * h;
        for (int i = 0; i < 8; ++i) {
            bf0[base + i] = step[base + i] + step[base + 8 + i];
            bf0[base + 8 + i] = step[base + i] - step[base + 8 + i];
        }
    }
    for (int i = 0; i < 32; ++i) bf0[i] = d_cv(bf0[i], cr);

    for (int i = 0; i < 16; ++i) step[i] = bf0[i];
    step[16] = d_half_btf(cospi[4], bf0[16], cospi[60], bf0[17], bit);
    step[17] = d_half_btf(cospi[60], bf0[16], -cospi[4], bf0[17], bit);
    step[18] = d_half_btf(cospi[20], bf0[18], cospi[44], bf0[19], bit);
    step[19] = d_half_btf(cospi[44], bf0[18], -cospi[20], bf0[19], bit);
    step[20] = d_half_btf(cospi[36], bf0[20], cospi[28], bf0[21], bit);
    step[21] = d_half_btf(cospi[28], bf0[20], -cospi[36], bf0[21], bit);
    step[22] = d_half_btf(cospi[52], bf0[22], cospi[12], bf0[23], bit);
    step[23] = d_half_btf(cospi[12], bf0[22], -cospi[52], bf0[23], bit);
    step[24] = d_half_btf(-cospi[60], bf0[24], cospi[4], bf0[25], bit);
    step[25] = d_half_btf(cospi[4], bf0[24], cospi[60], bf0[25], bit);
    step[26] = d_half_btf(-cospi[44], bf0[26], cospi[20], bf0[27], bit);
    step[27] = d_half_btf(cospi[20], bf0[26], cospi[44], bf0[27], bit);
    step[28] = d_half_btf(-cospi[28], bf0[28], cospi[36], bf0[29], bit);
    step[29] = d_half_btf(cospi[36], bf0[28], cospi[28], bf0[29], bit);
    step[30] = d_half_btf(-cospi[12], bf0[30], cospi[52], bf0[31], bit);
    step[31] = d_half_btf(cospi[52], bf0[30], cospi[12], bf0[31], bit);
    for (int i = 0; i < 32; ++i) step[i] = d_cv(step[i], cr);

    for (int i = 0; i < 16; ++i) {
        bf0[i] = d_cv(step[i] + step[16 + i], cr);
        bf0[16 + i] = d_cv(step[i] - step[16 + i], cr);
    }

    for (int k = 0; k < 16; ++k) {
        const int c1 = 4 * k + 1;
        step[2 * k]     = d_half_btf(cospi[c1], bf0[2 * k], cospi[64 - c1], bf0[2 * k + 1], bit);
        step[2 * k + 1] = d_half_btf(cospi[64 - c1], bf0[2 * k], -cospi[c1], bf0[2 * k + 1], bit);
    }
    for (int i = 0; i < 32; ++i) step[i] = d_cv(step[i], cr);

    static const int permTab[32] = {1, 30, 3, 28, 5, 26, 7, 24, 9, 22, 11, 20, 13, 18, 15, 16,
                                    17, 14, 19, 12, 21, 10, 23, 8, 25, 6, 27, 4, 29, 2, 31, 0};
    for (int i = 0; i < 32; ++i) output[i] = d_cv(step[permTab[i]], cr);
}

// svt_av1_inv_txfm2d_add_c at TX_32X32 (L6): inv_shift_32x32 = {-2,-4} ->
// rounding >>2 after the row 1D and >>4 at the final add; clamp bit 16 at
// both 1D inputs; 32 threads, thread t = row for the row pass then column
// for the col pass.
extern "C" __global__ void inv_txfm_2d_add_32x32(const int* coeffs, const int* txType,
                                                 unsigned char* dst, const int* stride) {
    __shared__ int sbuf[1024];
    const int t = threadIdx.x;
    int tmp[32];
    int o[32];
    for (int c = 0; c < 32; ++c) {
        tmp[c] = d_cv(coeffs[t * 32 + c], 16);
    }
    if (*txType == 0) {
        d_idct32i(tmp, o);
    } else {
        d_iadst32i(tmp, o);
    }
    for (int c = 0; c < 32; ++c) {
        sbuf[t * 32 + c] = d_rs(o[c], 2);
    }
    __syncthreads();
    for (int r = 0; r < 32; ++r) {
        tmp[r] = d_cv(sbuf[r * 32 + t], 16);
    }
    if (*txType == 0) {
        d_idct32i(tmp, o);
    } else {
        d_iadst32i(tmp, o);
    }
    for (int r = 0; r < 32; ++r) {
        int v = (int)dst[r * (*stride) + t] + d_rs(o[r], 4);
        if (v < 0) v = 0;
        else if (v > 255) v = 255;
        dst[r * (*stride) + t] = (unsigned char)v;
    }
}
)CUDB4";
    return s;
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

// quantize_fp_helper_c (full_loop.c:222), qm/iqm NULL branch.
// Size-parameterized over n_coeffs AND log_scale (the C helper's own
// parameters): rounding = ROUND_POWER_OF_TWO(round_ptr, log_scale) (:228),
// threshold (abs_coeff << (1+log_scale)) (:244), tmp32 >> (16-log_scale)
// (:246), dq >> log_scale (:249). The 4x4/8x8/16x16 wrappers pass log_scale 0
// (av1_get_tx_scale_tab = 0, full_loop.c:22); the 32x32 wrapper passes 1.
void quantizeFpN(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                 int nCoeffs, int logScale, std::int32_t* qcoeff, std::int32_t* dqcoeff,
                 std::uint16_t* eob) {
    int eobVal = -1;
    // ROUND_POWER_OF_TWO(round_ptr[k], logScale) (full_loop.c:228, macro
    // definitions.h:457; logScale 0 degenerates to the identity - the Q-audit
    // finding)
    const std::int32_t rounding[2] = {
        static_cast<std::int32_t>((tables.roundFp[0] + ((1 << logScale) >> 1)) >> logScale),
        static_cast<std::int32_t>((tables.roundFp[1] + ((1 << logScale) >> 1)) >> logScale)};
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
        if ((absCoeff << (1 + logScale)) >= thresh) {
            std::int64_t clamped = absCoeff + rounding[rc != 0];
            if (clamped < -32768) clamped = -32768;
            if (clamped > 32767) clamped = 32767;
            absCoeff = static_cast<std::int32_t>(clamped);
            tmp32 = static_cast<std::int32_t>((absCoeff * tables.quantFp[rc != 0]) >> (16 - logScale));
            if (tmp32) {
                qcoeff[rc] = (tmp32 ^ coeffSign) - coeffSign;
                const std::int32_t absDqcoeff =
                    static_cast<std::int32_t>((static_cast<std::int64_t>(tmp32) *
                                               tables.dequant[rc != 0]) >>
                                              logScale);
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
    quantizeFpN(coeff, tables, scan, 16, 0, qcoeff, dqcoeff, eob);
}

void quantizeFp8x8(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                   std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    quantizeFpN(coeff, tables, scan, 64, 0, qcoeff, dqcoeff, eob);
}

// svt_aom_quantize_b_c (full_loop.c:31) at log_scale 0, qm/iqm NULL branch
// (wt = 1 << AOM_QM_BITS = 1 << 5, inv_transforms.h:27). Size-parameterized
// over n_coeffs AND log_scale (the C original's parameters): zbins
// ROUND_POWER_OF_TWO(zbin, log_scale) (full_loop.c:36), round add
// ROUND_POWER_OF_TWO(round, log_scale) (:67), tmp32
// >> (16 - log_scale + AOM_QM_BITS) (:69-70), dq >> log_scale (:74).
void quantizeBN(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                int nCoeffs, int logScale, std::int32_t* qcoeff, std::int32_t* dqcoeff,
                std::uint16_t* eob) {
    // ROUND_POWER_OF_TWO(zbin, logScale) (definitions.h:457; identity at 0)
    const std::int32_t zbins[2] = {
        static_cast<std::int32_t>((tables.zbin[0] + ((1 << logScale) >> 1)) >> logScale),
        static_cast<std::int32_t>((tables.zbin[1] + ((1 << logScale) >> 1)) >> logScale)};
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
            // full_loop.c:67: ROUND_POWER_OF_TWO(round_ptr[rc != 0], logScale)
            // (identity at logScale 0 - the Q-audit finding)
            std::int64_t tmp =
                absCoeff + ((tables.round[rc != 0] + ((1 << logScale) >> 1)) >> logScale);
            if (tmp < -32768) tmp = -32768;
            if (tmp > 32767) tmp = 32767;
            tmp *= wt;
            std::int32_t tmp32 = static_cast<std::int32_t>(
                ((((tmp * tables.quant[rc != 0]) >> 16) + tmp) * tables.quantShift[rc != 0]) >>
                (16 - logScale + 5));
            qcoeff[rc] = (tmp32 ^ coeffSign) - coeffSign;
            const std::int32_t absDqcoeff =
                static_cast<std::int32_t>((static_cast<std::int64_t>(tmp32) *
                                           tables.dequant[rc != 0]) >>
                                          logScale);
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
    quantizeBN(coeff, tables, scan, 16, 0, qcoeff, dqcoeff, eob);
}

void quantizeB8x8(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                  std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    quantizeBN(coeff, tables, scan, 64, 0, qcoeff, dqcoeff, eob);
}

// TX_16X16 entries (C6): same helpers at n_coeffs=256, log_scale 0
void quantizeFp16x16(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                     std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    quantizeFpN(coeff, tables, scan, 256, 0, qcoeff, dqcoeff, eob);
}

void quantizeB16x16(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                    std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    quantizeBN(coeff, tables, scan, 256, 0, qcoeff, dqcoeff, eob);
}

// TX_32X32 entries (L5): same helpers at n_coeffs=1024, log_scale 1
void quantizeFp32x32(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                     std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    quantizeFpN(coeff, tables, scan, 1024, 1, qcoeff, dqcoeff, eob);
}

void quantizeB32x32(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                    std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    quantizeBN(coeff, tables, scan, 1024, 1, qcoeff, dqcoeff, eob);
}

// default (up-right diagonal) scan for 32x32, svt_aom_init_iscan formula
// (coefficients.c:345-363) at W=H=32
void defaultScan32x32(std::int16_t scan[1024]) {
    const int W = 32, H = 32;
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

// default (up-right diagonal) scan for 16x16, svt_aom_init_iscan formula
// (coefficients.c:345-363) at W=H=16
void defaultScan16x16(std::int16_t scan[256]) {
    const int W = 16, H = 16;
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

// TX_16X16 entry (C6): same fp helper at n_coeffs=256, log_scale 0
// (av1_get_tx_scale_tab[TX_16X16] = 0); 256 threads, thread t = scan
// position t; eobPos[256] shared reduction (1024 B), zeroing mirrors the
// helper's memset semantics.
extern "C" __global__ void quant_dequant_16x16(const int* coeff, const short* quantFp,
                                               const short* dequant, const short* roundFp,
                                               const short* scan, int* qcoeff, int* dqcoeff,
                                               unsigned short* eob) {
    __shared__ int eobPos[256];
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
        for (int i = 0; i < 256; ++i) {
            if (eobPos[i] > m) m = eobPos[i];
        }
        *eob = (unsigned short)(m + 1);
    }
}

// TX_32X32 entry (L5): same fp helper at n_coeffs=1024, log_scale 1
// (av1_get_tx_scale_tab[TX_32X32] = 1, full_loop.c:22) - the escalated
// arithmetic per full_loop.c:228/:244/:246/:249: rounding =
// ROUND_POWER_OF_TWO(roundFp, 1) (precomputed here per-thread on the table
// value), threshold << 2, quant >> 15 (16 - log_scale), dq >> 1; 1024
// threads, thread t = scan position t; eobPos[1024] shared (4096 B).
extern "C" __global__ void quant_dequant_32x32(const int* coeff, const short* quantFp,
                                               const short* dequant, const short* roundFp,
                                               const short* scan, int* qcoeff, int* dqcoeff,
                                               unsigned short* eob) {
    __shared__ int eobPos[1024];
    const int t = threadIdx.x;
    const int rc = scan[t];
    qcoeff[rc] = 0;
    dqcoeff[rc] = 0;
    const int rounding = (roundFp[rc != 0] + 1) >> 1;  // ROUND_POWER_OF_TWO(round, 1)
    const int thresh = dequant[rc != 0];
    const int coeffVal = coeff[rc];
    const int coeffSign = coeffVal < 0 ? -1 : 0;
    int absCoeff = (coeffVal ^ coeffSign) - coeffSign;
    int tmp32 = 0;
    if ((absCoeff << 2) >= thresh) {
        long long clamped = absCoeff + rounding;
        if (clamped < -32768) clamped = -32768;
        if (clamped > 32767) clamped = 32767;
        absCoeff = (int)clamped;
        tmp32 = (int)((absCoeff * quantFp[rc != 0]) >> 15);
        if (tmp32) {
            qcoeff[rc] = (tmp32 ^ coeffSign) - coeffSign;
            const int absDq = (int)(((long long)tmp32 * dequant[rc != 0]) >> 1);
            dqcoeff[rc] = (absDq ^ coeffSign) - coeffSign;
        }
    }
    eobPos[t] = tmp32 ? t : -1;
    __syncthreads();
    if (t == 0) {
        int m = -1;
        for (int i = 0; i < 1024; ++i) {
            if (eobPos[i] > m) m = eobPos[i];
        }
        *eob = (unsigned short)(m + 1);
    }
}
)CUDA";
}

}  // namespace transforms

