#include "transform.h"
#include <cassert>

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

// svt_aom_eb_av1_cospi_arr_data[1], the cos_bit=10 row (fwd_cos_bit_row[4][4]
// = 10, the TX_64X64 row pass - the only pass below 12). VERBATIM row 0 of
// svt_aom_eb_av1_cospi_arr_data (7-row table, cos_bit_min = 10).
const std::int32_t kCospi10[64] = {
    1024, 1024, 1023, 1021, 1019, 1016, 1013, 1009, 1004, 999, 993, 987, 980, 972, 964, 955,
    946, 936, 926, 915, 903, 891, 878, 865, 851, 837, 822, 807, 792, 775, 759, 742,
    724, 706, 688, 669, 650, 630, 610, 590, 569, 548, 526, 505, 483, 460, 438, 415,
    392, 369, 345, 321, 297, 273, 249, 224, 200, 175, 150, 125, 100, 75, 50, 25};

// svt_aom_eb_av1_cospi_arr_data[1], the cos_bit=11 row (fwd_cos_bit_row[3][4]
// and [4][3]: the TX_32X64 / TX_64X32 row passes). VERBATIM row 1 of
// svt_aom_eb_av1_cospi_arr_data (inv_transforms.c:3295-3298).
const std::int32_t kCospi11[64] = {
    2048, 2047, 2046, 2042, 2038, 2033, 2026, 2018, 2009, 1998, 1987, 1974, 1960, 1945, 1928, 1911,
    1892, 1872, 1851, 1829, 1806, 1782, 1757, 1730, 1703, 1674, 1645, 1615, 1583, 1551, 1517, 1483,
    1448, 1412, 1375, 1338, 1299, 1260, 1220, 1179, 1138, 1096, 1053, 1009, 965,  921,  876,  830,
    784,  737,  690,  642,  595,  546,  498,  449,  400,  350,  301,  251,  201,  151,  100,  50};

// inv_transforms.c:88 clamp_value, fixed clamp bit (16 for 8-bit, per
// svt_av1_gen_inv_stage_range)
constexpr int kInvClampBit = 16;

// cospi_arr(cos_bit) (inv_transforms.h): per-bit cospi table row. The host
// keeps the bit-13 (fwd), bit-12 (INV_COS_BIT), bit-11 (the TX_32X64/TX_64X32
// row passes, fwd_cos_bit_row[3][4]/[4][3]) and bit-10 (TX_64X64 row pass,
// fwd_cos_bit_row[4][4]) rows.
const std::int32_t* cospiRow(int cosBit) {
    if (cosBit == 13) return kCospi13;
    if (cosBit == 11) return kCospi11;
    if (cosBit == 10) return kCospi10;
    return kCospi12;
}

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

// svt_av1_idct4_new (inv_transforms.c:97), cos_bit = 12 (INV_COS_BIT). TXH1:
// the stage_range shim became a parameter - the landed 2-arg form below is the
// {16,...} bd-8 overload (kInvClampBit), the Hb form consumes the
// gen_inv_stage_range arrays (bd 10: row 18, bd 12: row 20)
void idct4Hb(const std::int32_t input[4], std::int32_t output[4], const std::int8_t* stageRange) {
    const int8_t cosBit = 12;
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

// TXH1: the landed 2-arg bd-8 overload (the {16,...} shim array)
void idct4(const std::int32_t input[4], std::int32_t output[4]) {
    static constexpr std::int8_t stageRange[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    idct4Hb(input, output, stageRange);
}

// svt_av1_iadst4_new (inv_transforms.c:729), cos_bit = 12 (incl. all-zero
// early-out; stage_range unused). TXH1: the stage_range parameter exists for
// signature uniformity with the Hb 1D table and is (void)'d like the reference
void iadst4Hb(const std::int32_t input[4], std::int32_t output[4], const std::int8_t* stageRange) {
    (void)stageRange;
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

// TXH1: the landed 2-arg bd-8 overload (stage_range unused in the reference)
void iadst4(const std::int32_t input[4], std::int32_t output[4]) {
    static constexpr std::int8_t stageRange[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    iadst4Hb(input, output, stageRange);
}

// svt_av1_idct8_new (inv_transforms.c:136), cos_bit = 12 (INV_COS_BIT).
// stage_range: {16,...} shim confirmed exact by gen_inv_range_8x8_dct gate
// line (B1) - all 6 stages clamp at bit 16 for bd=8. TXH1: the shim became a
// parameter; the 2-arg form below is the bd-8 overload
void idct8Hb(const std::int32_t input[8], std::int32_t output[8], const std::int8_t* stageRange) {
    const int8_t cosBit = 12;
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

// TXH1: the landed 2-arg bd-8 overload (the {16,...} shim array)
void idct8(const std::int32_t input[8], std::int32_t output[8]) {
    static constexpr std::int8_t stageRange[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    idct8Hb(input, output, stageRange);
}

// svt_av1_iadst8_new (inv_transforms.c:822), cos_bit = 12 (INV_COS_BIT).
// Unlike iadst4, iadst8 HAS clamp_value at stages 3 and 5 -" mirrored exactly.
// stage_range: {16,...} shim confirmed exact by gen_inv_range_8x8_adst gate
// line (B1) -" all 8 stages clamp at bit 16 for bd=8.
void iadst8Hb(const std::int32_t input[8], std::int32_t output[8], const std::int8_t* stageRange) {
    const int8_t cosBit = 12;
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

// TXH1: the landed 2-arg bd-8 overload (the {16,...} shim array)
void iadst8(const std::int32_t input[8], std::int32_t output[8]) {
    static constexpr std::int8_t stageRange[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    iadst8Hb(input, output, stageRange);
}

// svt_av1_idct16_new (inv_transforms.c:215-376), cos_bit = 12 (INV_COS_BIT);
// stage_range consumed at stages 3-7 (clamp_value on the butterfly adds),
// shim {16,...} proven by gen_inv_range_16x16_dct. TXH1: the shim became a
// parameter; the 2-arg form below is the bd-8 overload
void idct16Hb(const std::int32_t input[16], std::int32_t output[16], const std::int8_t* stageRange) {
    const int8_t cosBit = 12;
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

// TXH1: the landed 2-arg bd-8 overload (the {16,...} shim array)
void idct16(const std::int32_t input[16], std::int32_t output[16]) {
    static constexpr std::int8_t stageRange[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    idct16Hb(input, output, stageRange);
}

// svt_av1_iadst16_new (inv_transforms.c:927-1130), cos_bit = 12; stage_range
// consumed at stages 3/5/7; no all-zero early-out at 16. TXH1: the shim became
// a parameter; the 2-arg form below is the bd-8 overload
void iadst16Hb(const std::int32_t input[16], std::int32_t output[16], const std::int8_t* stageRange) {
    const int8_t cosBit = 12;
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

// TXH1: the landed 2-arg bd-8 overload (the {16,...} shim array)
void iadst16(const std::int32_t input[16], std::int32_t output[16]) {
    static constexpr std::int8_t stageRange[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    iadst16Hb(input, output, stageRange);
}

// svt_av1_fdct8_new (transforms.c:196) with the reference's cos_bit parameter
// (TX1: fwd_cos_bit_row[1][3] = 12 reaches the row pass at TX_8X32)
void fdct8B(const std::int32_t input[8], std::int32_t output[8], int cosBit) {
    const std::int32_t* cospi = cospiRow(cosBit);
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
    step[5] = halfBtf(-cospi[32], bf0[5], cospi[32], bf0[6], cosBit);
    step[6] = halfBtf(cospi[32], bf0[6], cospi[32], bf0[5], cosBit);
    step[7] = bf0[7];

    bf0[0] = step[0];
    bf0[1] = step[1];
    bf0[2] = step[2];
    bf0[3] = step[3];
    bf0[4] = step[4];
    bf0[5] = step[5];
    bf0[6] = step[6];
    bf0[7] = step[7];

    step[0] = halfBtf(cospi[32], bf0[0], cospi[32], bf0[1], cosBit);
    step[1] = halfBtf(-cospi[32], bf0[1], cospi[32], bf0[0], cosBit);
    step[2] = halfBtf(cospi[48], bf0[2], cospi[16], bf0[3], cosBit);
    step[3] = halfBtf(cospi[48], bf0[3], -cospi[16], bf0[2], cosBit);
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
    step[4] = halfBtf(cospi[56], bf0[4], cospi[8], bf0[7], cosBit);
    step[5] = halfBtf(cospi[24], bf0[5], cospi[40], bf0[6], cosBit);
    step[6] = halfBtf(cospi[24], bf0[6], -cospi[40], bf0[5], cosBit);
    step[7] = halfBtf(cospi[56], bf0[7], -cospi[8], bf0[4], cosBit);

    output[0] = step[0];
    output[1] = step[4];
    output[2] = step[2];
    output[3] = step[6];
    output[4] = step[1];
    output[5] = step[5];
    output[6] = step[3];
    output[7] = step[7];
}

// svt_av1_fdct8_new (transforms.c:196) at the square config: cos_bit = 13
void fdct8(const std::int32_t input[8], std::int32_t output[8]) {
    fdct8B(input, output, 13);
}

// svt_av1_fadst8_new (transforms.c:1617) with the reference's cos_bit
// parameter (TX1: fwd_cos_bit_row[1][3] = 12 reaches the ADST8 row pass at
// TX_8X32 + DCT_ADST / ADST_ADST)
void fadst8B(const std::int32_t input[8], std::int32_t output[8], int cosBit) {
    const std::int32_t* cospi = cospiRow(cosBit);
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
    step[2] = halfBtf(cospi[32], bf0[2], cospi[32], bf0[3], cosBit);
    step[3] = halfBtf(cospi[32], bf0[2], -cospi[32], bf0[3], cosBit);
    step[4] = bf0[4];
    step[5] = bf0[5];
    step[6] = halfBtf(cospi[32], bf0[6], cospi[32], bf0[7], cosBit);
    step[7] = halfBtf(cospi[32], bf0[6], -cospi[32], bf0[7], cosBit);

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
    step[4] = halfBtf(cospi[16], bf0[4], cospi[48], bf0[5], cosBit);
    step[5] = halfBtf(cospi[48], bf0[4], -cospi[16], bf0[5], cosBit);
    step[6] = halfBtf(-cospi[48], bf0[6], cospi[16], bf0[7], cosBit);
    step[7] = halfBtf(cospi[16], bf0[6], cospi[48], bf0[7], cosBit);

    bf0[0] = step[0] + step[4];
    bf0[1] = step[1] + step[5];
    bf0[2] = step[2] + step[6];
    bf0[3] = step[3] + step[7];
    bf0[4] = step[0] - step[4];
    bf0[5] = step[1] - step[5];
    bf0[6] = step[2] - step[6];
    bf0[7] = step[3] - step[7];

    step[0] = halfBtf(cospi[4], bf0[0], cospi[60], bf0[1], cosBit);
    step[1] = halfBtf(cospi[60], bf0[0], -cospi[4], bf0[1], cosBit);
    step[2] = halfBtf(cospi[20], bf0[2], cospi[44], bf0[3], cosBit);
    step[3] = halfBtf(cospi[44], bf0[2], -cospi[20], bf0[3], cosBit);
    step[4] = halfBtf(cospi[36], bf0[4], cospi[28], bf0[5], cosBit);
    step[5] = halfBtf(cospi[28], bf0[4], -cospi[36], bf0[5], cosBit);
    step[6] = halfBtf(cospi[52], bf0[6], cospi[12], bf0[7], cosBit);
    step[7] = halfBtf(cospi[12], bf0[6], -cospi[52], bf0[7], cosBit);

    output[0] = step[1];
    output[1] = step[6];
    output[2] = step[3];
    output[3] = step[4];
    output[4] = step[5];
    output[5] = step[2];
    output[6] = step[7];
    output[7] = step[0];
}

// svt_av1_fadst8_new (transforms.c:1617) at the square config: cos_bit = 13
void fadst8(const std::int32_t input[8], std::int32_t output[8]) {
    fadst8B(input, output, 13);
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
// svt_av1_fdct32_new (transforms.c:422) with the cos_bit parameter it takes in
// the reference (TX1: fwd_cos_bit_row[3][2] = 13 and [3][4]/[4][3] = 11 reach
// the row pass at TX_32X16 / TX_32X64 / TX_64X32; the square TX_32X32 uses 12)
void fdct32B(const std::int32_t input[32], std::int32_t output[32], int cosBit) {
    const std::int32_t* cospi = cospiRow(cosBit);
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

// svt_av1_fdct32_new (transforms.c:422) at the square TX_32X32 config:
// cos_bit = 12 (fwd_cos_bit_col/row[3][3])
void fdct32(const std::int32_t input[32], std::int32_t output[32]) {
    fdct32B(input, output, 12);
}

// static av1_fadst32_new (transforms.c:1908-2314), cos_bit = 12, stage_range
// unused
// svt_av1_fdct64_new (transforms.c:762), cos_bit = 13
// (fwd_cos_bit_col[4][4]); VERBATIM body with the SVT pointer-swap
// structure preserved (bf0/bf1 alternation), ported mechanically from the
// committed extract svt_gen.c (only mechanical renames: half_btf -> halfBtf)
void fdct64(const std::int32_t input[64], std::int32_t output[64]) {
    fdct64B(input, output, 13);
}

void fdct64B(const std::int32_t input[64], std::int32_t output[64], int cosBit) {
    const std::int32_t* cospi = cospiRow(cosBit);
    std::int32_t step[64];
    std::int32_t* bf0;
    std::int32_t* bf1;


    // stage 0;

    // stage 1;
    bf1     = output;
    bf1[0]  = input[0] + input[63];
    bf1[1]  = input[1] + input[62];
    bf1[2]  = input[2] + input[61];
    bf1[3]  = input[3] + input[60];
    bf1[4]  = input[4] + input[59];
    bf1[5]  = input[5] + input[58];
    bf1[6]  = input[6] + input[57];
    bf1[7]  = input[7] + input[56];
    bf1[8]  = input[8] + input[55];
    bf1[9]  = input[9] + input[54];
    bf1[10] = input[10] + input[53];
    bf1[11] = input[11] + input[52];
    bf1[12] = input[12] + input[51];
    bf1[13] = input[13] + input[50];
    bf1[14] = input[14] + input[49];
    bf1[15] = input[15] + input[48];
    bf1[16] = input[16] + input[47];
    bf1[17] = input[17] + input[46];
    bf1[18] = input[18] + input[45];
    bf1[19] = input[19] + input[44];
    bf1[20] = input[20] + input[43];
    bf1[21] = input[21] + input[42];
    bf1[22] = input[22] + input[41];
    bf1[23] = input[23] + input[40];
    bf1[24] = input[24] + input[39];
    bf1[25] = input[25] + input[38];
    bf1[26] = input[26] + input[37];
    bf1[27] = input[27] + input[36];
    bf1[28] = input[28] + input[35];
    bf1[29] = input[29] + input[34];
    bf1[30] = input[30] + input[33];
    bf1[31] = input[31] + input[32];
    bf1[32] = -input[32] + input[31];
    bf1[33] = -input[33] + input[30];
    bf1[34] = -input[34] + input[29];
    bf1[35] = -input[35] + input[28];
    bf1[36] = -input[36] + input[27];
    bf1[37] = -input[37] + input[26];
    bf1[38] = -input[38] + input[25];
    bf1[39] = -input[39] + input[24];
    bf1[40] = -input[40] + input[23];
    bf1[41] = -input[41] + input[22];
    bf1[42] = -input[42] + input[21];
    bf1[43] = -input[43] + input[20];
    bf1[44] = -input[44] + input[19];
    bf1[45] = -input[45] + input[18];
    bf1[46] = -input[46] + input[17];
    bf1[47] = -input[47] + input[16];
    bf1[48] = -input[48] + input[15];
    bf1[49] = -input[49] + input[14];
    bf1[50] = -input[50] + input[13];
    bf1[51] = -input[51] + input[12];
    bf1[52] = -input[52] + input[11];
    bf1[53] = -input[53] + input[10];
    bf1[54] = -input[54] + input[9];
    bf1[55] = -input[55] + input[8];
    bf1[56] = -input[56] + input[7];
    bf1[57] = -input[57] + input[6];
    bf1[58] = -input[58] + input[5];
    bf1[59] = -input[59] + input[4];
    bf1[60] = -input[60] + input[3];
    bf1[61] = -input[61] + input[2];
    bf1[62] = -input[62] + input[1];
    bf1[63] = -input[63] + input[0];

    // stage 2
        bf0     = output;
    bf1     = step;
    bf1[0]  = bf0[0] + bf0[31];
    bf1[1]  = bf0[1] + bf0[30];
    bf1[2]  = bf0[2] + bf0[29];
    bf1[3]  = bf0[3] + bf0[28];
    bf1[4]  = bf0[4] + bf0[27];
    bf1[5]  = bf0[5] + bf0[26];
    bf1[6]  = bf0[6] + bf0[25];
    bf1[7]  = bf0[7] + bf0[24];
    bf1[8]  = bf0[8] + bf0[23];
    bf1[9]  = bf0[9] + bf0[22];
    bf1[10] = bf0[10] + bf0[21];
    bf1[11] = bf0[11] + bf0[20];
    bf1[12] = bf0[12] + bf0[19];
    bf1[13] = bf0[13] + bf0[18];
    bf1[14] = bf0[14] + bf0[17];
    bf1[15] = bf0[15] + bf0[16];
    bf1[16] = -bf0[16] + bf0[15];
    bf1[17] = -bf0[17] + bf0[14];
    bf1[18] = -bf0[18] + bf0[13];
    bf1[19] = -bf0[19] + bf0[12];
    bf1[20] = -bf0[20] + bf0[11];
    bf1[21] = -bf0[21] + bf0[10];
    bf1[22] = -bf0[22] + bf0[9];
    bf1[23] = -bf0[23] + bf0[8];
    bf1[24] = -bf0[24] + bf0[7];
    bf1[25] = -bf0[25] + bf0[6];
    bf1[26] = -bf0[26] + bf0[5];
    bf1[27] = -bf0[27] + bf0[4];
    bf1[28] = -bf0[28] + bf0[3];
    bf1[29] = -bf0[29] + bf0[2];
    bf1[30] = -bf0[30] + bf0[1];
    bf1[31] = -bf0[31] + bf0[0];
    bf1[32] = bf0[32];
    bf1[33] = bf0[33];
    bf1[34] = bf0[34];
    bf1[35] = bf0[35];
    bf1[36] = bf0[36];
    bf1[37] = bf0[37];
    bf1[38] = bf0[38];
    bf1[39] = bf0[39];
    bf1[40] = halfBtf(-cospi[32], bf0[40], cospi[32], bf0[55], cosBit);
    bf1[41] = halfBtf(-cospi[32], bf0[41], cospi[32], bf0[54], cosBit);
    bf1[42] = halfBtf(-cospi[32], bf0[42], cospi[32], bf0[53], cosBit);
    bf1[43] = halfBtf(-cospi[32], bf0[43], cospi[32], bf0[52], cosBit);
    bf1[44] = halfBtf(-cospi[32], bf0[44], cospi[32], bf0[51], cosBit);
    bf1[45] = halfBtf(-cospi[32], bf0[45], cospi[32], bf0[50], cosBit);
    bf1[46] = halfBtf(-cospi[32], bf0[46], cospi[32], bf0[49], cosBit);
    bf1[47] = halfBtf(-cospi[32], bf0[47], cospi[32], bf0[48], cosBit);
    bf1[48] = halfBtf(cospi[32], bf0[48], cospi[32], bf0[47], cosBit);
    bf1[49] = halfBtf(cospi[32], bf0[49], cospi[32], bf0[46], cosBit);
    bf1[50] = halfBtf(cospi[32], bf0[50], cospi[32], bf0[45], cosBit);
    bf1[51] = halfBtf(cospi[32], bf0[51], cospi[32], bf0[44], cosBit);
    bf1[52] = halfBtf(cospi[32], bf0[52], cospi[32], bf0[43], cosBit);
    bf1[53] = halfBtf(cospi[32], bf0[53], cospi[32], bf0[42], cosBit);
    bf1[54] = halfBtf(cospi[32], bf0[54], cospi[32], bf0[41], cosBit);
    bf1[55] = halfBtf(cospi[32], bf0[55], cospi[32], bf0[40], cosBit);
    bf1[56] = bf0[56];
    bf1[57] = bf0[57];
    bf1[58] = bf0[58];
    bf1[59] = bf0[59];
    bf1[60] = bf0[60];
    bf1[61] = bf0[61];
    bf1[62] = bf0[62];
    bf1[63] = bf0[63];

    // stage 3
        bf0     = step;
    bf1     = output;
    bf1[0]  = bf0[0] + bf0[15];
    bf1[1]  = bf0[1] + bf0[14];
    bf1[2]  = bf0[2] + bf0[13];
    bf1[3]  = bf0[3] + bf0[12];
    bf1[4]  = bf0[4] + bf0[11];
    bf1[5]  = bf0[5] + bf0[10];
    bf1[6]  = bf0[6] + bf0[9];
    bf1[7]  = bf0[7] + bf0[8];
    bf1[8]  = -bf0[8] + bf0[7];
    bf1[9]  = -bf0[9] + bf0[6];
    bf1[10] = -bf0[10] + bf0[5];
    bf1[11] = -bf0[11] + bf0[4];
    bf1[12] = -bf0[12] + bf0[3];
    bf1[13] = -bf0[13] + bf0[2];
    bf1[14] = -bf0[14] + bf0[1];
    bf1[15] = -bf0[15] + bf0[0];
    bf1[16] = bf0[16];
    bf1[17] = bf0[17];
    bf1[18] = bf0[18];
    bf1[19] = bf0[19];
    bf1[20] = halfBtf(-cospi[32], bf0[20], cospi[32], bf0[27], cosBit);
    bf1[21] = halfBtf(-cospi[32], bf0[21], cospi[32], bf0[26], cosBit);
    bf1[22] = halfBtf(-cospi[32], bf0[22], cospi[32], bf0[25], cosBit);
    bf1[23] = halfBtf(-cospi[32], bf0[23], cospi[32], bf0[24], cosBit);
    bf1[24] = halfBtf(cospi[32], bf0[24], cospi[32], bf0[23], cosBit);
    bf1[25] = halfBtf(cospi[32], bf0[25], cospi[32], bf0[22], cosBit);
    bf1[26] = halfBtf(cospi[32], bf0[26], cospi[32], bf0[21], cosBit);
    bf1[27] = halfBtf(cospi[32], bf0[27], cospi[32], bf0[20], cosBit);
    bf1[28] = bf0[28];
    bf1[29] = bf0[29];
    bf1[30] = bf0[30];
    bf1[31] = bf0[31];
    bf1[32] = bf0[32] + bf0[47];
    bf1[33] = bf0[33] + bf0[46];
    bf1[34] = bf0[34] + bf0[45];
    bf1[35] = bf0[35] + bf0[44];
    bf1[36] = bf0[36] + bf0[43];
    bf1[37] = bf0[37] + bf0[42];
    bf1[38] = bf0[38] + bf0[41];
    bf1[39] = bf0[39] + bf0[40];
    bf1[40] = -bf0[40] + bf0[39];
    bf1[41] = -bf0[41] + bf0[38];
    bf1[42] = -bf0[42] + bf0[37];
    bf1[43] = -bf0[43] + bf0[36];
    bf1[44] = -bf0[44] + bf0[35];
    bf1[45] = -bf0[45] + bf0[34];
    bf1[46] = -bf0[46] + bf0[33];
    bf1[47] = -bf0[47] + bf0[32];
    bf1[48] = -bf0[48] + bf0[63];
    bf1[49] = -bf0[49] + bf0[62];
    bf1[50] = -bf0[50] + bf0[61];
    bf1[51] = -bf0[51] + bf0[60];
    bf1[52] = -bf0[52] + bf0[59];
    bf1[53] = -bf0[53] + bf0[58];
    bf1[54] = -bf0[54] + bf0[57];
    bf1[55] = -bf0[55] + bf0[56];
    bf1[56] = bf0[56] + bf0[55];
    bf1[57] = bf0[57] + bf0[54];
    bf1[58] = bf0[58] + bf0[53];
    bf1[59] = bf0[59] + bf0[52];
    bf1[60] = bf0[60] + bf0[51];
    bf1[61] = bf0[61] + bf0[50];
    bf1[62] = bf0[62] + bf0[49];
    bf1[63] = bf0[63] + bf0[48];

    // stage 4
        bf0     = output;
    bf1     = step;
    bf1[0]  = bf0[0] + bf0[7];
    bf1[1]  = bf0[1] + bf0[6];
    bf1[2]  = bf0[2] + bf0[5];
    bf1[3]  = bf0[3] + bf0[4];
    bf1[4]  = -bf0[4] + bf0[3];
    bf1[5]  = -bf0[5] + bf0[2];
    bf1[6]  = -bf0[6] + bf0[1];
    bf1[7]  = -bf0[7] + bf0[0];
    bf1[8]  = bf0[8];
    bf1[9]  = bf0[9];
    bf1[10] = halfBtf(-cospi[32], bf0[10], cospi[32], bf0[13], cosBit);
    bf1[11] = halfBtf(-cospi[32], bf0[11], cospi[32], bf0[12], cosBit);
    bf1[12] = halfBtf(cospi[32], bf0[12], cospi[32], bf0[11], cosBit);
    bf1[13] = halfBtf(cospi[32], bf0[13], cospi[32], bf0[10], cosBit);
    bf1[14] = bf0[14];
    bf1[15] = bf0[15];
    bf1[16] = bf0[16] + bf0[23];
    bf1[17] = bf0[17] + bf0[22];
    bf1[18] = bf0[18] + bf0[21];
    bf1[19] = bf0[19] + bf0[20];
    bf1[20] = -bf0[20] + bf0[19];
    bf1[21] = -bf0[21] + bf0[18];
    bf1[22] = -bf0[22] + bf0[17];
    bf1[23] = -bf0[23] + bf0[16];
    bf1[24] = -bf0[24] + bf0[31];
    bf1[25] = -bf0[25] + bf0[30];
    bf1[26] = -bf0[26] + bf0[29];
    bf1[27] = -bf0[27] + bf0[28];
    bf1[28] = bf0[28] + bf0[27];
    bf1[29] = bf0[29] + bf0[26];
    bf1[30] = bf0[30] + bf0[25];
    bf1[31] = bf0[31] + bf0[24];
    bf1[32] = bf0[32];
    bf1[33] = bf0[33];
    bf1[34] = bf0[34];
    bf1[35] = bf0[35];
    bf1[36] = halfBtf(-cospi[16], bf0[36], cospi[48], bf0[59], cosBit);
    bf1[37] = halfBtf(-cospi[16], bf0[37], cospi[48], bf0[58], cosBit);
    bf1[38] = halfBtf(-cospi[16], bf0[38], cospi[48], bf0[57], cosBit);
    bf1[39] = halfBtf(-cospi[16], bf0[39], cospi[48], bf0[56], cosBit);
    bf1[40] = halfBtf(-cospi[48], bf0[40], -cospi[16], bf0[55], cosBit);
    bf1[41] = halfBtf(-cospi[48], bf0[41], -cospi[16], bf0[54], cosBit);
    bf1[42] = halfBtf(-cospi[48], bf0[42], -cospi[16], bf0[53], cosBit);
    bf1[43] = halfBtf(-cospi[48], bf0[43], -cospi[16], bf0[52], cosBit);
    bf1[44] = bf0[44];
    bf1[45] = bf0[45];
    bf1[46] = bf0[46];
    bf1[47] = bf0[47];
    bf1[48] = bf0[48];
    bf1[49] = bf0[49];
    bf1[50] = bf0[50];
    bf1[51] = bf0[51];
    bf1[52] = halfBtf(cospi[48], bf0[52], -cospi[16], bf0[43], cosBit);
    bf1[53] = halfBtf(cospi[48], bf0[53], -cospi[16], bf0[42], cosBit);
    bf1[54] = halfBtf(cospi[48], bf0[54], -cospi[16], bf0[41], cosBit);
    bf1[55] = halfBtf(cospi[48], bf0[55], -cospi[16], bf0[40], cosBit);
    bf1[56] = halfBtf(cospi[16], bf0[56], cospi[48], bf0[39], cosBit);
    bf1[57] = halfBtf(cospi[16], bf0[57], cospi[48], bf0[38], cosBit);
    bf1[58] = halfBtf(cospi[16], bf0[58], cospi[48], bf0[37], cosBit);
    bf1[59] = halfBtf(cospi[16], bf0[59], cospi[48], bf0[36], cosBit);
    bf1[60] = bf0[60];
    bf1[61] = bf0[61];
    bf1[62] = bf0[62];
    bf1[63] = bf0[63];

    // stage 5
        bf0     = step;
    bf1     = output;
    bf1[0]  = bf0[0] + bf0[3];
    bf1[1]  = bf0[1] + bf0[2];
    bf1[2]  = -bf0[2] + bf0[1];
    bf1[3]  = -bf0[3] + bf0[0];
    bf1[4]  = bf0[4];
    bf1[5]  = halfBtf(-cospi[32], bf0[5], cospi[32], bf0[6], cosBit);
    bf1[6]  = halfBtf(cospi[32], bf0[6], cospi[32], bf0[5], cosBit);
    bf1[7]  = bf0[7];
    bf1[8]  = bf0[8] + bf0[11];
    bf1[9]  = bf0[9] + bf0[10];
    bf1[10] = -bf0[10] + bf0[9];
    bf1[11] = -bf0[11] + bf0[8];
    bf1[12] = -bf0[12] + bf0[15];
    bf1[13] = -bf0[13] + bf0[14];
    bf1[14] = bf0[14] + bf0[13];
    bf1[15] = bf0[15] + bf0[12];
    bf1[16] = bf0[16];
    bf1[17] = bf0[17];
    bf1[18] = halfBtf(-cospi[16], bf0[18], cospi[48], bf0[29], cosBit);
    bf1[19] = halfBtf(-cospi[16], bf0[19], cospi[48], bf0[28], cosBit);
    bf1[20] = halfBtf(-cospi[48], bf0[20], -cospi[16], bf0[27], cosBit);
    bf1[21] = halfBtf(-cospi[48], bf0[21], -cospi[16], bf0[26], cosBit);
    bf1[22] = bf0[22];
    bf1[23] = bf0[23];
    bf1[24] = bf0[24];
    bf1[25] = bf0[25];
    bf1[26] = halfBtf(cospi[48], bf0[26], -cospi[16], bf0[21], cosBit);
    bf1[27] = halfBtf(cospi[48], bf0[27], -cospi[16], bf0[20], cosBit);
    bf1[28] = halfBtf(cospi[16], bf0[28], cospi[48], bf0[19], cosBit);
    bf1[29] = halfBtf(cospi[16], bf0[29], cospi[48], bf0[18], cosBit);
    bf1[30] = bf0[30];
    bf1[31] = bf0[31];
    bf1[32] = bf0[32] + bf0[39];
    bf1[33] = bf0[33] + bf0[38];
    bf1[34] = bf0[34] + bf0[37];
    bf1[35] = bf0[35] + bf0[36];
    bf1[36] = -bf0[36] + bf0[35];
    bf1[37] = -bf0[37] + bf0[34];
    bf1[38] = -bf0[38] + bf0[33];
    bf1[39] = -bf0[39] + bf0[32];
    bf1[40] = -bf0[40] + bf0[47];
    bf1[41] = -bf0[41] + bf0[46];
    bf1[42] = -bf0[42] + bf0[45];
    bf1[43] = -bf0[43] + bf0[44];
    bf1[44] = bf0[44] + bf0[43];
    bf1[45] = bf0[45] + bf0[42];
    bf1[46] = bf0[46] + bf0[41];
    bf1[47] = bf0[47] + bf0[40];
    bf1[48] = bf0[48] + bf0[55];
    bf1[49] = bf0[49] + bf0[54];
    bf1[50] = bf0[50] + bf0[53];
    bf1[51] = bf0[51] + bf0[52];
    bf1[52] = -bf0[52] + bf0[51];
    bf1[53] = -bf0[53] + bf0[50];
    bf1[54] = -bf0[54] + bf0[49];
    bf1[55] = -bf0[55] + bf0[48];
    bf1[56] = -bf0[56] + bf0[63];
    bf1[57] = -bf0[57] + bf0[62];
    bf1[58] = -bf0[58] + bf0[61];
    bf1[59] = -bf0[59] + bf0[60];
    bf1[60] = bf0[60] + bf0[59];
    bf1[61] = bf0[61] + bf0[58];
    bf1[62] = bf0[62] + bf0[57];
    bf1[63] = bf0[63] + bf0[56];

    // stage 6
        bf0     = output;
    bf1     = step;
    bf1[0]  = halfBtf(cospi[32], bf0[0], cospi[32], bf0[1], cosBit);
    bf1[1]  = halfBtf(-cospi[32], bf0[1], cospi[32], bf0[0], cosBit);
    bf1[2]  = halfBtf(cospi[48], bf0[2], cospi[16], bf0[3], cosBit);
    bf1[3]  = halfBtf(cospi[48], bf0[3], -cospi[16], bf0[2], cosBit);
    bf1[4]  = bf0[4] + bf0[5];
    bf1[5]  = -bf0[5] + bf0[4];
    bf1[6]  = -bf0[6] + bf0[7];
    bf1[7]  = bf0[7] + bf0[6];
    bf1[8]  = bf0[8];
    bf1[9]  = halfBtf(-cospi[16], bf0[9], cospi[48], bf0[14], cosBit);
    bf1[10] = halfBtf(-cospi[48], bf0[10], -cospi[16], bf0[13], cosBit);
    bf1[11] = bf0[11];
    bf1[12] = bf0[12];
    bf1[13] = halfBtf(cospi[48], bf0[13], -cospi[16], bf0[10], cosBit);
    bf1[14] = halfBtf(cospi[16], bf0[14], cospi[48], bf0[9], cosBit);
    bf1[15] = bf0[15];
    bf1[16] = bf0[16] + bf0[19];
    bf1[17] = bf0[17] + bf0[18];
    bf1[18] = -bf0[18] + bf0[17];
    bf1[19] = -bf0[19] + bf0[16];
    bf1[20] = -bf0[20] + bf0[23];
    bf1[21] = -bf0[21] + bf0[22];
    bf1[22] = bf0[22] + bf0[21];
    bf1[23] = bf0[23] + bf0[20];
    bf1[24] = bf0[24] + bf0[27];
    bf1[25] = bf0[25] + bf0[26];
    bf1[26] = -bf0[26] + bf0[25];
    bf1[27] = -bf0[27] + bf0[24];
    bf1[28] = -bf0[28] + bf0[31];
    bf1[29] = -bf0[29] + bf0[30];
    bf1[30] = bf0[30] + bf0[29];
    bf1[31] = bf0[31] + bf0[28];
    bf1[32] = bf0[32];
    bf1[33] = bf0[33];
    bf1[34] = halfBtf(-cospi[8], bf0[34], cospi[56], bf0[61], cosBit);
    bf1[35] = halfBtf(-cospi[8], bf0[35], cospi[56], bf0[60], cosBit);
    bf1[36] = halfBtf(-cospi[56], bf0[36], -cospi[8], bf0[59], cosBit);
    bf1[37] = halfBtf(-cospi[56], bf0[37], -cospi[8], bf0[58], cosBit);
    bf1[38] = bf0[38];
    bf1[39] = bf0[39];
    bf1[40] = bf0[40];
    bf1[41] = bf0[41];
    bf1[42] = halfBtf(-cospi[40], bf0[42], cospi[24], bf0[53], cosBit);
    bf1[43] = halfBtf(-cospi[40], bf0[43], cospi[24], bf0[52], cosBit);
    bf1[44] = halfBtf(-cospi[24], bf0[44], -cospi[40], bf0[51], cosBit);
    bf1[45] = halfBtf(-cospi[24], bf0[45], -cospi[40], bf0[50], cosBit);
    bf1[46] = bf0[46];
    bf1[47] = bf0[47];
    bf1[48] = bf0[48];
    bf1[49] = bf0[49];
    bf1[50] = halfBtf(cospi[24], bf0[50], -cospi[40], bf0[45], cosBit);
    bf1[51] = halfBtf(cospi[24], bf0[51], -cospi[40], bf0[44], cosBit);
    bf1[52] = halfBtf(cospi[40], bf0[52], cospi[24], bf0[43], cosBit);
    bf1[53] = halfBtf(cospi[40], bf0[53], cospi[24], bf0[42], cosBit);
    bf1[54] = bf0[54];
    bf1[55] = bf0[55];
    bf1[56] = bf0[56];
    bf1[57] = bf0[57];
    bf1[58] = halfBtf(cospi[56], bf0[58], -cospi[8], bf0[37], cosBit);
    bf1[59] = halfBtf(cospi[56], bf0[59], -cospi[8], bf0[36], cosBit);
    bf1[60] = halfBtf(cospi[8], bf0[60], cospi[56], bf0[35], cosBit);
    bf1[61] = halfBtf(cospi[8], bf0[61], cospi[56], bf0[34], cosBit);
    bf1[62] = bf0[62];
    bf1[63] = bf0[63];

    // stage 7
        bf0     = step;
    bf1     = output;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = halfBtf(cospi[56], bf0[4], cospi[8], bf0[7], cosBit);
    bf1[5]  = halfBtf(cospi[24], bf0[5], cospi[40], bf0[6], cosBit);
    bf1[6]  = halfBtf(cospi[24], bf0[6], -cospi[40], bf0[5], cosBit);
    bf1[7]  = halfBtf(cospi[56], bf0[7], -cospi[8], bf0[4], cosBit);
    bf1[8]  = bf0[8] + bf0[9];
    bf1[9]  = -bf0[9] + bf0[8];
    bf1[10] = -bf0[10] + bf0[11];
    bf1[11] = bf0[11] + bf0[10];
    bf1[12] = bf0[12] + bf0[13];
    bf1[13] = -bf0[13] + bf0[12];
    bf1[14] = -bf0[14] + bf0[15];
    bf1[15] = bf0[15] + bf0[14];
    bf1[16] = bf0[16];
    bf1[17] = halfBtf(-cospi[8], bf0[17], cospi[56], bf0[30], cosBit);
    bf1[18] = halfBtf(-cospi[56], bf0[18], -cospi[8], bf0[29], cosBit);
    bf1[19] = bf0[19];
    bf1[20] = bf0[20];
    bf1[21] = halfBtf(-cospi[40], bf0[21], cospi[24], bf0[26], cosBit);
    bf1[22] = halfBtf(-cospi[24], bf0[22], -cospi[40], bf0[25], cosBit);
    bf1[23] = bf0[23];
    bf1[24] = bf0[24];
    bf1[25] = halfBtf(cospi[24], bf0[25], -cospi[40], bf0[22], cosBit);
    bf1[26] = halfBtf(cospi[40], bf0[26], cospi[24], bf0[21], cosBit);
    bf1[27] = bf0[27];
    bf1[28] = bf0[28];
    bf1[29] = halfBtf(cospi[56], bf0[29], -cospi[8], bf0[18], cosBit);
    bf1[30] = halfBtf(cospi[8], bf0[30], cospi[56], bf0[17], cosBit);
    bf1[31] = bf0[31];
    bf1[32] = bf0[32] + bf0[35];
    bf1[33] = bf0[33] + bf0[34];
    bf1[34] = -bf0[34] + bf0[33];
    bf1[35] = -bf0[35] + bf0[32];
    bf1[36] = -bf0[36] + bf0[39];
    bf1[37] = -bf0[37] + bf0[38];
    bf1[38] = bf0[38] + bf0[37];
    bf1[39] = bf0[39] + bf0[36];
    bf1[40] = bf0[40] + bf0[43];
    bf1[41] = bf0[41] + bf0[42];
    bf1[42] = -bf0[42] + bf0[41];
    bf1[43] = -bf0[43] + bf0[40];
    bf1[44] = -bf0[44] + bf0[47];
    bf1[45] = -bf0[45] + bf0[46];
    bf1[46] = bf0[46] + bf0[45];
    bf1[47] = bf0[47] + bf0[44];
    bf1[48] = bf0[48] + bf0[51];
    bf1[49] = bf0[49] + bf0[50];
    bf1[50] = -bf0[50] + bf0[49];
    bf1[51] = -bf0[51] + bf0[48];
    bf1[52] = -bf0[52] + bf0[55];
    bf1[53] = -bf0[53] + bf0[54];
    bf1[54] = bf0[54] + bf0[53];
    bf1[55] = bf0[55] + bf0[52];
    bf1[56] = bf0[56] + bf0[59];
    bf1[57] = bf0[57] + bf0[58];
    bf1[58] = -bf0[58] + bf0[57];
    bf1[59] = -bf0[59] + bf0[56];
    bf1[60] = -bf0[60] + bf0[63];
    bf1[61] = -bf0[61] + bf0[62];
    bf1[62] = bf0[62] + bf0[61];
    bf1[63] = bf0[63] + bf0[60];

    // stage 8
        bf0     = output;
    bf1     = step;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = bf0[4];
    bf1[5]  = bf0[5];
    bf1[6]  = bf0[6];
    bf1[7]  = bf0[7];
    bf1[8]  = halfBtf(cospi[60], bf0[8], cospi[4], bf0[15], cosBit);
    bf1[9]  = halfBtf(cospi[28], bf0[9], cospi[36], bf0[14], cosBit);
    bf1[10] = halfBtf(cospi[44], bf0[10], cospi[20], bf0[13], cosBit);
    bf1[11] = halfBtf(cospi[12], bf0[11], cospi[52], bf0[12], cosBit);
    bf1[12] = halfBtf(cospi[12], bf0[12], -cospi[52], bf0[11], cosBit);
    bf1[13] = halfBtf(cospi[44], bf0[13], -cospi[20], bf0[10], cosBit);
    bf1[14] = halfBtf(cospi[28], bf0[14], -cospi[36], bf0[9], cosBit);
    bf1[15] = halfBtf(cospi[60], bf0[15], -cospi[4], bf0[8], cosBit);
    bf1[16] = bf0[16] + bf0[17];
    bf1[17] = -bf0[17] + bf0[16];
    bf1[18] = -bf0[18] + bf0[19];
    bf1[19] = bf0[19] + bf0[18];
    bf1[20] = bf0[20] + bf0[21];
    bf1[21] = -bf0[21] + bf0[20];
    bf1[22] = -bf0[22] + bf0[23];
    bf1[23] = bf0[23] + bf0[22];
    bf1[24] = bf0[24] + bf0[25];
    bf1[25] = -bf0[25] + bf0[24];
    bf1[26] = -bf0[26] + bf0[27];
    bf1[27] = bf0[27] + bf0[26];
    bf1[28] = bf0[28] + bf0[29];
    bf1[29] = -bf0[29] + bf0[28];
    bf1[30] = -bf0[30] + bf0[31];
    bf1[31] = bf0[31] + bf0[30];
    bf1[32] = bf0[32];
    bf1[33] = halfBtf(-cospi[4], bf0[33], cospi[60], bf0[62], cosBit);
    bf1[34] = halfBtf(-cospi[60], bf0[34], -cospi[4], bf0[61], cosBit);
    bf1[35] = bf0[35];
    bf1[36] = bf0[36];
    bf1[37] = halfBtf(-cospi[36], bf0[37], cospi[28], bf0[58], cosBit);
    bf1[38] = halfBtf(-cospi[28], bf0[38], -cospi[36], bf0[57], cosBit);
    bf1[39] = bf0[39];
    bf1[40] = bf0[40];
    bf1[41] = halfBtf(-cospi[20], bf0[41], cospi[44], bf0[54], cosBit);
    bf1[42] = halfBtf(-cospi[44], bf0[42], -cospi[20], bf0[53], cosBit);
    bf1[43] = bf0[43];
    bf1[44] = bf0[44];
    bf1[45] = halfBtf(-cospi[52], bf0[45], cospi[12], bf0[50], cosBit);
    bf1[46] = halfBtf(-cospi[12], bf0[46], -cospi[52], bf0[49], cosBit);
    bf1[47] = bf0[47];
    bf1[48] = bf0[48];
    bf1[49] = halfBtf(cospi[12], bf0[49], -cospi[52], bf0[46], cosBit);
    bf1[50] = halfBtf(cospi[52], bf0[50], cospi[12], bf0[45], cosBit);
    bf1[51] = bf0[51];
    bf1[52] = bf0[52];
    bf1[53] = halfBtf(cospi[44], bf0[53], -cospi[20], bf0[42], cosBit);
    bf1[54] = halfBtf(cospi[20], bf0[54], cospi[44], bf0[41], cosBit);
    bf1[55] = bf0[55];
    bf1[56] = bf0[56];
    bf1[57] = halfBtf(cospi[28], bf0[57], -cospi[36], bf0[38], cosBit);
    bf1[58] = halfBtf(cospi[36], bf0[58], cospi[28], bf0[37], cosBit);
    bf1[59] = bf0[59];
    bf1[60] = bf0[60];
    bf1[61] = halfBtf(cospi[60], bf0[61], -cospi[4], bf0[34], cosBit);
    bf1[62] = halfBtf(cospi[4], bf0[62], cospi[60], bf0[33], cosBit);
    bf1[63] = bf0[63];

    // stage 9
        bf0     = step;
    bf1     = output;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = bf0[4];
    bf1[5]  = bf0[5];
    bf1[6]  = bf0[6];
    bf1[7]  = bf0[7];
    bf1[8]  = bf0[8];
    bf1[9]  = bf0[9];
    bf1[10] = bf0[10];
    bf1[11] = bf0[11];
    bf1[12] = bf0[12];
    bf1[13] = bf0[13];
    bf1[14] = bf0[14];
    bf1[15] = bf0[15];
    bf1[16] = halfBtf(cospi[62], bf0[16], cospi[2], bf0[31], cosBit);
    bf1[17] = halfBtf(cospi[30], bf0[17], cospi[34], bf0[30], cosBit);
    bf1[18] = halfBtf(cospi[46], bf0[18], cospi[18], bf0[29], cosBit);
    bf1[19] = halfBtf(cospi[14], bf0[19], cospi[50], bf0[28], cosBit);
    bf1[20] = halfBtf(cospi[54], bf0[20], cospi[10], bf0[27], cosBit);
    bf1[21] = halfBtf(cospi[22], bf0[21], cospi[42], bf0[26], cosBit);
    bf1[22] = halfBtf(cospi[38], bf0[22], cospi[26], bf0[25], cosBit);
    bf1[23] = halfBtf(cospi[6], bf0[23], cospi[58], bf0[24], cosBit);
    bf1[24] = halfBtf(cospi[6], bf0[24], -cospi[58], bf0[23], cosBit);
    bf1[25] = halfBtf(cospi[38], bf0[25], -cospi[26], bf0[22], cosBit);
    bf1[26] = halfBtf(cospi[22], bf0[26], -cospi[42], bf0[21], cosBit);
    bf1[27] = halfBtf(cospi[54], bf0[27], -cospi[10], bf0[20], cosBit);
    bf1[28] = halfBtf(cospi[14], bf0[28], -cospi[50], bf0[19], cosBit);
    bf1[29] = halfBtf(cospi[46], bf0[29], -cospi[18], bf0[18], cosBit);
    bf1[30] = halfBtf(cospi[30], bf0[30], -cospi[34], bf0[17], cosBit);
    bf1[31] = halfBtf(cospi[62], bf0[31], -cospi[2], bf0[16], cosBit);
    bf1[32] = bf0[32] + bf0[33];
    bf1[33] = -bf0[33] + bf0[32];
    bf1[34] = -bf0[34] + bf0[35];
    bf1[35] = bf0[35] + bf0[34];
    bf1[36] = bf0[36] + bf0[37];
    bf1[37] = -bf0[37] + bf0[36];
    bf1[38] = -bf0[38] + bf0[39];
    bf1[39] = bf0[39] + bf0[38];
    bf1[40] = bf0[40] + bf0[41];
    bf1[41] = -bf0[41] + bf0[40];
    bf1[42] = -bf0[42] + bf0[43];
    bf1[43] = bf0[43] + bf0[42];
    bf1[44] = bf0[44] + bf0[45];
    bf1[45] = -bf0[45] + bf0[44];
    bf1[46] = -bf0[46] + bf0[47];
    bf1[47] = bf0[47] + bf0[46];
    bf1[48] = bf0[48] + bf0[49];
    bf1[49] = -bf0[49] + bf0[48];
    bf1[50] = -bf0[50] + bf0[51];
    bf1[51] = bf0[51] + bf0[50];
    bf1[52] = bf0[52] + bf0[53];
    bf1[53] = -bf0[53] + bf0[52];
    bf1[54] = -bf0[54] + bf0[55];
    bf1[55] = bf0[55] + bf0[54];
    bf1[56] = bf0[56] + bf0[57];
    bf1[57] = -bf0[57] + bf0[56];
    bf1[58] = -bf0[58] + bf0[59];
    bf1[59] = bf0[59] + bf0[58];
    bf1[60] = bf0[60] + bf0[61];
    bf1[61] = -bf0[61] + bf0[60];
    bf1[62] = -bf0[62] + bf0[63];
    bf1[63] = bf0[63] + bf0[62];

    // stage 10
        bf0     = output;
    bf1     = step;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = bf0[4];
    bf1[5]  = bf0[5];
    bf1[6]  = bf0[6];
    bf1[7]  = bf0[7];
    bf1[8]  = bf0[8];
    bf1[9]  = bf0[9];
    bf1[10] = bf0[10];
    bf1[11] = bf0[11];
    bf1[12] = bf0[12];
    bf1[13] = bf0[13];
    bf1[14] = bf0[14];
    bf1[15] = bf0[15];
    bf1[16] = bf0[16];
    bf1[17] = bf0[17];
    bf1[18] = bf0[18];
    bf1[19] = bf0[19];
    bf1[20] = bf0[20];
    bf1[21] = bf0[21];
    bf1[22] = bf0[22];
    bf1[23] = bf0[23];
    bf1[24] = bf0[24];
    bf1[25] = bf0[25];
    bf1[26] = bf0[26];
    bf1[27] = bf0[27];
    bf1[28] = bf0[28];
    bf1[29] = bf0[29];
    bf1[30] = bf0[30];
    bf1[31] = bf0[31];
    bf1[32] = halfBtf(cospi[63], bf0[32], cospi[1], bf0[63], cosBit);
    bf1[33] = halfBtf(cospi[31], bf0[33], cospi[33], bf0[62], cosBit);
    bf1[34] = halfBtf(cospi[47], bf0[34], cospi[17], bf0[61], cosBit);
    bf1[35] = halfBtf(cospi[15], bf0[35], cospi[49], bf0[60], cosBit);
    bf1[36] = halfBtf(cospi[55], bf0[36], cospi[9], bf0[59], cosBit);
    bf1[37] = halfBtf(cospi[23], bf0[37], cospi[41], bf0[58], cosBit);
    bf1[38] = halfBtf(cospi[39], bf0[38], cospi[25], bf0[57], cosBit);
    bf1[39] = halfBtf(cospi[7], bf0[39], cospi[57], bf0[56], cosBit);
    bf1[40] = halfBtf(cospi[59], bf0[40], cospi[5], bf0[55], cosBit);
    bf1[41] = halfBtf(cospi[27], bf0[41], cospi[37], bf0[54], cosBit);
    bf1[42] = halfBtf(cospi[43], bf0[42], cospi[21], bf0[53], cosBit);
    bf1[43] = halfBtf(cospi[11], bf0[43], cospi[53], bf0[52], cosBit);
    bf1[44] = halfBtf(cospi[51], bf0[44], cospi[13], bf0[51], cosBit);
    bf1[45] = halfBtf(cospi[19], bf0[45], cospi[45], bf0[50], cosBit);
    bf1[46] = halfBtf(cospi[35], bf0[46], cospi[29], bf0[49], cosBit);
    bf1[47] = halfBtf(cospi[3], bf0[47], cospi[61], bf0[48], cosBit);
    bf1[48] = halfBtf(cospi[3], bf0[48], -cospi[61], bf0[47], cosBit);
    bf1[49] = halfBtf(cospi[35], bf0[49], -cospi[29], bf0[46], cosBit);
    bf1[50] = halfBtf(cospi[19], bf0[50], -cospi[45], bf0[45], cosBit);
    bf1[51] = halfBtf(cospi[51], bf0[51], -cospi[13], bf0[44], cosBit);
    bf1[52] = halfBtf(cospi[11], bf0[52], -cospi[53], bf0[43], cosBit);
    bf1[53] = halfBtf(cospi[43], bf0[53], -cospi[21], bf0[42], cosBit);
    bf1[54] = halfBtf(cospi[27], bf0[54], -cospi[37], bf0[41], cosBit);
    bf1[55] = halfBtf(cospi[59], bf0[55], -cospi[5], bf0[40], cosBit);
    bf1[56] = halfBtf(cospi[7], bf0[56], -cospi[57], bf0[39], cosBit);
    bf1[57] = halfBtf(cospi[39], bf0[57], -cospi[25], bf0[38], cosBit);
    bf1[58] = halfBtf(cospi[23], bf0[58], -cospi[41], bf0[37], cosBit);
    bf1[59] = halfBtf(cospi[55], bf0[59], -cospi[9], bf0[36], cosBit);
    bf1[60] = halfBtf(cospi[15], bf0[60], -cospi[49], bf0[35], cosBit);
    bf1[61] = halfBtf(cospi[47], bf0[61], -cospi[17], bf0[34], cosBit);
    bf1[62] = halfBtf(cospi[31], bf0[62], -cospi[33], bf0[33], cosBit);
    bf1[63] = halfBtf(cospi[63], bf0[63], -cospi[1], bf0[32], cosBit);

    // stage 11
    bf0     = step;
    bf1     = output;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[32];
    bf1[2]  = bf0[16];
    bf1[3]  = bf0[48];
    bf1[4]  = bf0[8];
    bf1[5]  = bf0[40];
    bf1[6]  = bf0[24];
    bf1[7]  = bf0[56];
    bf1[8]  = bf0[4];
    bf1[9]  = bf0[36];
    bf1[10] = bf0[20];
    bf1[11] = bf0[52];
    bf1[12] = bf0[12];
    bf1[13] = bf0[44];
    bf1[14] = bf0[28];
    bf1[15] = bf0[60];
    bf1[16] = bf0[2];
    bf1[17] = bf0[34];
    bf1[18] = bf0[18];
    bf1[19] = bf0[50];
    bf1[20] = bf0[10];
    bf1[21] = bf0[42];
    bf1[22] = bf0[26];
    bf1[23] = bf0[58];
    bf1[24] = bf0[6];
    bf1[25] = bf0[38];
    bf1[26] = bf0[22];
    bf1[27] = bf0[54];
    bf1[28] = bf0[14];
    bf1[29] = bf0[46];
    bf1[30] = bf0[30];
    bf1[31] = bf0[62];
    bf1[32] = bf0[1];
    bf1[33] = bf0[33];
    bf1[34] = bf0[17];
    bf1[35] = bf0[49];
    bf1[36] = bf0[9];
    bf1[37] = bf0[41];
    bf1[38] = bf0[25];
    bf1[39] = bf0[57];
    bf1[40] = bf0[5];
    bf1[41] = bf0[37];
    bf1[42] = bf0[21];
    bf1[43] = bf0[53];
    bf1[44] = bf0[13];
    bf1[45] = bf0[45];
    bf1[46] = bf0[29];
    bf1[47] = bf0[61];
    bf1[48] = bf0[3];
    bf1[49] = bf0[35];
    bf1[50] = bf0[19];
    bf1[51] = bf0[51];
    bf1[52] = bf0[11];
    bf1[53] = bf0[43];
    bf1[54] = bf0[27];
    bf1[55] = bf0[59];
    bf1[56] = bf0[7];
    bf1[57] = bf0[39];
    bf1[58] = bf0[23];
    bf1[59] = bf0[55];
    bf1[60] = bf0[15];
    bf1[61] = bf0[47];
    bf1[62] = bf0[31];
    bf1[63] = bf0[63];
}

// svt_av1_idct64_new (inv_transforms.c:1567), cos_bit = 12; clamp_value at
// the audited stages via stageRange (gen_inv_range_64x64_dct = 12 x 16);
// VERBATIM body ported mechanically from svt_gen.c (clamp_value ->
// clampValue). TXH1: the shim became a parameter; the 2-arg form below is
// the bd-8 overload
void idct64Hb(const std::int32_t input[64], std::int32_t output[64], const std::int8_t* stageRange) {
    const int8_t cosBit = 12;
    const std::int32_t* cospi = kCospi12;
    std::int32_t step[64];
    std::int32_t* bf0;
    std::int32_t* bf1;
    int32_t stage = 0;
    // clampValue2Arg shim: the verbatim body calls clamp_value(x, stage_range[stage]);
    // clampValue(value, bit) is the same two-arg form here


    // stage 0;

    // stage 1;
    stage++;
    bf1     = output;
    bf1[0]  = input[0];
    bf1[1]  = input[32];
    bf1[2]  = input[16];
    bf1[3]  = input[48];
    bf1[4]  = input[8];
    bf1[5]  = input[40];
    bf1[6]  = input[24];
    bf1[7]  = input[56];
    bf1[8]  = input[4];
    bf1[9]  = input[36];
    bf1[10] = input[20];
    bf1[11] = input[52];
    bf1[12] = input[12];
    bf1[13] = input[44];
    bf1[14] = input[28];
    bf1[15] = input[60];
    bf1[16] = input[2];
    bf1[17] = input[34];
    bf1[18] = input[18];
    bf1[19] = input[50];
    bf1[20] = input[10];
    bf1[21] = input[42];
    bf1[22] = input[26];
    bf1[23] = input[58];
    bf1[24] = input[6];
    bf1[25] = input[38];
    bf1[26] = input[22];
    bf1[27] = input[54];
    bf1[28] = input[14];
    bf1[29] = input[46];
    bf1[30] = input[30];
    bf1[31] = input[62];
    bf1[32] = input[1];
    bf1[33] = input[33];
    bf1[34] = input[17];
    bf1[35] = input[49];
    bf1[36] = input[9];
    bf1[37] = input[41];
    bf1[38] = input[25];
    bf1[39] = input[57];
    bf1[40] = input[5];
    bf1[41] = input[37];
    bf1[42] = input[21];
    bf1[43] = input[53];
    bf1[44] = input[13];
    bf1[45] = input[45];
    bf1[46] = input[29];
    bf1[47] = input[61];
    bf1[48] = input[3];
    bf1[49] = input[35];
    bf1[50] = input[19];
    bf1[51] = input[51];
    bf1[52] = input[11];
    bf1[53] = input[43];
    bf1[54] = input[27];
    bf1[55] = input[59];
    bf1[56] = input[7];
    bf1[57] = input[39];
    bf1[58] = input[23];
    bf1[59] = input[55];
    bf1[60] = input[15];
    bf1[61] = input[47];
    bf1[62] = input[31];
    bf1[63] = input[63];
    //range_check_buf(stage, input, bf1, size, stageRange[stage]);

    // stage 2
    stage++;
    bf0     = output;
    bf1     = step;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = bf0[4];
    bf1[5]  = bf0[5];
    bf1[6]  = bf0[6];
    bf1[7]  = bf0[7];
    bf1[8]  = bf0[8];
    bf1[9]  = bf0[9];
    bf1[10] = bf0[10];
    bf1[11] = bf0[11];
    bf1[12] = bf0[12];
    bf1[13] = bf0[13];
    bf1[14] = bf0[14];
    bf1[15] = bf0[15];
    bf1[16] = bf0[16];
    bf1[17] = bf0[17];
    bf1[18] = bf0[18];
    bf1[19] = bf0[19];
    bf1[20] = bf0[20];
    bf1[21] = bf0[21];
    bf1[22] = bf0[22];
    bf1[23] = bf0[23];
    bf1[24] = bf0[24];
    bf1[25] = bf0[25];
    bf1[26] = bf0[26];
    bf1[27] = bf0[27];
    bf1[28] = bf0[28];
    bf1[29] = bf0[29];
    bf1[30] = bf0[30];
    bf1[31] = bf0[31];
    bf1[32] = halfBtf(cospi[63], bf0[32], -cospi[1], bf0[63], cosBit);
    bf1[33] = halfBtf(cospi[31], bf0[33], -cospi[33], bf0[62], cosBit);
    bf1[34] = halfBtf(cospi[47], bf0[34], -cospi[17], bf0[61], cosBit);
    bf1[35] = halfBtf(cospi[15], bf0[35], -cospi[49], bf0[60], cosBit);
    bf1[36] = halfBtf(cospi[55], bf0[36], -cospi[9], bf0[59], cosBit);
    bf1[37] = halfBtf(cospi[23], bf0[37], -cospi[41], bf0[58], cosBit);
    bf1[38] = halfBtf(cospi[39], bf0[38], -cospi[25], bf0[57], cosBit);
    bf1[39] = halfBtf(cospi[7], bf0[39], -cospi[57], bf0[56], cosBit);
    bf1[40] = halfBtf(cospi[59], bf0[40], -cospi[5], bf0[55], cosBit);
    bf1[41] = halfBtf(cospi[27], bf0[41], -cospi[37], bf0[54], cosBit);
    bf1[42] = halfBtf(cospi[43], bf0[42], -cospi[21], bf0[53], cosBit);
    bf1[43] = halfBtf(cospi[11], bf0[43], -cospi[53], bf0[52], cosBit);
    bf1[44] = halfBtf(cospi[51], bf0[44], -cospi[13], bf0[51], cosBit);
    bf1[45] = halfBtf(cospi[19], bf0[45], -cospi[45], bf0[50], cosBit);
    bf1[46] = halfBtf(cospi[35], bf0[46], -cospi[29], bf0[49], cosBit);
    bf1[47] = halfBtf(cospi[3], bf0[47], -cospi[61], bf0[48], cosBit);
    bf1[48] = halfBtf(cospi[61], bf0[47], cospi[3], bf0[48], cosBit);
    bf1[49] = halfBtf(cospi[29], bf0[46], cospi[35], bf0[49], cosBit);
    bf1[50] = halfBtf(cospi[45], bf0[45], cospi[19], bf0[50], cosBit);
    bf1[51] = halfBtf(cospi[13], bf0[44], cospi[51], bf0[51], cosBit);
    bf1[52] = halfBtf(cospi[53], bf0[43], cospi[11], bf0[52], cosBit);
    bf1[53] = halfBtf(cospi[21], bf0[42], cospi[43], bf0[53], cosBit);
    bf1[54] = halfBtf(cospi[37], bf0[41], cospi[27], bf0[54], cosBit);
    bf1[55] = halfBtf(cospi[5], bf0[40], cospi[59], bf0[55], cosBit);
    bf1[56] = halfBtf(cospi[57], bf0[39], cospi[7], bf0[56], cosBit);
    bf1[57] = halfBtf(cospi[25], bf0[38], cospi[39], bf0[57], cosBit);
    bf1[58] = halfBtf(cospi[41], bf0[37], cospi[23], bf0[58], cosBit);
    bf1[59] = halfBtf(cospi[9], bf0[36], cospi[55], bf0[59], cosBit);
    bf1[60] = halfBtf(cospi[49], bf0[35], cospi[15], bf0[60], cosBit);
    bf1[61] = halfBtf(cospi[17], bf0[34], cospi[47], bf0[61], cosBit);
    bf1[62] = halfBtf(cospi[33], bf0[33], cospi[31], bf0[62], cosBit);
    bf1[63] = halfBtf(cospi[1], bf0[32], cospi[63], bf0[63], cosBit);
    //range_check_buf(stage, input, bf1, size, stageRange[stage]);

    // stage 3
    stage++;
    bf0     = step;
    bf1     = output;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = bf0[4];
    bf1[5]  = bf0[5];
    bf1[6]  = bf0[6];
    bf1[7]  = bf0[7];
    bf1[8]  = bf0[8];
    bf1[9]  = bf0[9];
    bf1[10] = bf0[10];
    bf1[11] = bf0[11];
    bf1[12] = bf0[12];
    bf1[13] = bf0[13];
    bf1[14] = bf0[14];
    bf1[15] = bf0[15];
    bf1[16] = halfBtf(cospi[62], bf0[16], -cospi[2], bf0[31], cosBit);
    bf1[17] = halfBtf(cospi[30], bf0[17], -cospi[34], bf0[30], cosBit);
    bf1[18] = halfBtf(cospi[46], bf0[18], -cospi[18], bf0[29], cosBit);
    bf1[19] = halfBtf(cospi[14], bf0[19], -cospi[50], bf0[28], cosBit);
    bf1[20] = halfBtf(cospi[54], bf0[20], -cospi[10], bf0[27], cosBit);
    bf1[21] = halfBtf(cospi[22], bf0[21], -cospi[42], bf0[26], cosBit);
    bf1[22] = halfBtf(cospi[38], bf0[22], -cospi[26], bf0[25], cosBit);
    bf1[23] = halfBtf(cospi[6], bf0[23], -cospi[58], bf0[24], cosBit);
    bf1[24] = halfBtf(cospi[58], bf0[23], cospi[6], bf0[24], cosBit);
    bf1[25] = halfBtf(cospi[26], bf0[22], cospi[38], bf0[25], cosBit);
    bf1[26] = halfBtf(cospi[42], bf0[21], cospi[22], bf0[26], cosBit);
    bf1[27] = halfBtf(cospi[10], bf0[20], cospi[54], bf0[27], cosBit);
    bf1[28] = halfBtf(cospi[50], bf0[19], cospi[14], bf0[28], cosBit);
    bf1[29] = halfBtf(cospi[18], bf0[18], cospi[46], bf0[29], cosBit);
    bf1[30] = halfBtf(cospi[34], bf0[17], cospi[30], bf0[30], cosBit);
    bf1[31] = halfBtf(cospi[2], bf0[16], cospi[62], bf0[31], cosBit);
    bf1[32] = clampValue(bf0[32] + bf0[33], stageRange[stage]);
    bf1[33] = clampValue(bf0[32] - bf0[33], stageRange[stage]);
    bf1[34] = clampValue(-bf0[34] + bf0[35], stageRange[stage]);
    bf1[35] = clampValue(bf0[34] + bf0[35], stageRange[stage]);
    bf1[36] = clampValue(bf0[36] + bf0[37], stageRange[stage]);
    bf1[37] = clampValue(bf0[36] - bf0[37], stageRange[stage]);
    bf1[38] = clampValue(-bf0[38] + bf0[39], stageRange[stage]);
    bf1[39] = clampValue(bf0[38] + bf0[39], stageRange[stage]);
    bf1[40] = clampValue(bf0[40] + bf0[41], stageRange[stage]);
    bf1[41] = clampValue(bf0[40] - bf0[41], stageRange[stage]);
    bf1[42] = clampValue(-bf0[42] + bf0[43], stageRange[stage]);
    bf1[43] = clampValue(bf0[42] + bf0[43], stageRange[stage]);
    bf1[44] = clampValue(bf0[44] + bf0[45], stageRange[stage]);
    bf1[45] = clampValue(bf0[44] - bf0[45], stageRange[stage]);
    bf1[46] = clampValue(-bf0[46] + bf0[47], stageRange[stage]);
    bf1[47] = clampValue(bf0[46] + bf0[47], stageRange[stage]);
    bf1[48] = clampValue(bf0[48] + bf0[49], stageRange[stage]);
    bf1[49] = clampValue(bf0[48] - bf0[49], stageRange[stage]);
    bf1[50] = clampValue(-bf0[50] + bf0[51], stageRange[stage]);
    bf1[51] = clampValue(bf0[50] + bf0[51], stageRange[stage]);
    bf1[52] = clampValue(bf0[52] + bf0[53], stageRange[stage]);
    bf1[53] = clampValue(bf0[52] - bf0[53], stageRange[stage]);
    bf1[54] = clampValue(-bf0[54] + bf0[55], stageRange[stage]);
    bf1[55] = clampValue(bf0[54] + bf0[55], stageRange[stage]);
    bf1[56] = clampValue(bf0[56] + bf0[57], stageRange[stage]);
    bf1[57] = clampValue(bf0[56] - bf0[57], stageRange[stage]);
    bf1[58] = clampValue(-bf0[58] + bf0[59], stageRange[stage]);
    bf1[59] = clampValue(bf0[58] + bf0[59], stageRange[stage]);
    bf1[60] = clampValue(bf0[60] + bf0[61], stageRange[stage]);
    bf1[61] = clampValue(bf0[60] - bf0[61], stageRange[stage]);
    bf1[62] = clampValue(-bf0[62] + bf0[63], stageRange[stage]);
    bf1[63] = clampValue(bf0[62] + bf0[63], stageRange[stage]);
    //range_check_buf(stage, input, bf1, size, stageRange[stage]);

    // stage 4
    stage++;
    bf0     = output;
    bf1     = step;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = bf0[4];
    bf1[5]  = bf0[5];
    bf1[6]  = bf0[6];
    bf1[7]  = bf0[7];
    bf1[8]  = halfBtf(cospi[60], bf0[8], -cospi[4], bf0[15], cosBit);
    bf1[9]  = halfBtf(cospi[28], bf0[9], -cospi[36], bf0[14], cosBit);
    bf1[10] = halfBtf(cospi[44], bf0[10], -cospi[20], bf0[13], cosBit);
    bf1[11] = halfBtf(cospi[12], bf0[11], -cospi[52], bf0[12], cosBit);
    bf1[12] = halfBtf(cospi[52], bf0[11], cospi[12], bf0[12], cosBit);
    bf1[13] = halfBtf(cospi[20], bf0[10], cospi[44], bf0[13], cosBit);
    bf1[14] = halfBtf(cospi[36], bf0[9], cospi[28], bf0[14], cosBit);
    bf1[15] = halfBtf(cospi[4], bf0[8], cospi[60], bf0[15], cosBit);
    bf1[16] = clampValue(bf0[16] + bf0[17], stageRange[stage]);
    bf1[17] = clampValue(bf0[16] - bf0[17], stageRange[stage]);
    bf1[18] = clampValue(-bf0[18] + bf0[19], stageRange[stage]);
    bf1[19] = clampValue(bf0[18] + bf0[19], stageRange[stage]);
    bf1[20] = clampValue(bf0[20] + bf0[21], stageRange[stage]);
    bf1[21] = clampValue(bf0[20] - bf0[21], stageRange[stage]);
    bf1[22] = clampValue(-bf0[22] + bf0[23], stageRange[stage]);
    bf1[23] = clampValue(bf0[22] + bf0[23], stageRange[stage]);
    bf1[24] = clampValue(bf0[24] + bf0[25], stageRange[stage]);
    bf1[25] = clampValue(bf0[24] - bf0[25], stageRange[stage]);
    bf1[26] = clampValue(-bf0[26] + bf0[27], stageRange[stage]);
    bf1[27] = clampValue(bf0[26] + bf0[27], stageRange[stage]);
    bf1[28] = clampValue(bf0[28] + bf0[29], stageRange[stage]);
    bf1[29] = clampValue(bf0[28] - bf0[29], stageRange[stage]);
    bf1[30] = clampValue(-bf0[30] + bf0[31], stageRange[stage]);
    bf1[31] = clampValue(bf0[30] + bf0[31], stageRange[stage]);
    bf1[32] = bf0[32];
    bf1[33] = halfBtf(-cospi[4], bf0[33], cospi[60], bf0[62], cosBit);
    bf1[34] = halfBtf(-cospi[60], bf0[34], -cospi[4], bf0[61], cosBit);
    bf1[35] = bf0[35];
    bf1[36] = bf0[36];
    bf1[37] = halfBtf(-cospi[36], bf0[37], cospi[28], bf0[58], cosBit);
    bf1[38] = halfBtf(-cospi[28], bf0[38], -cospi[36], bf0[57], cosBit);
    bf1[39] = bf0[39];
    bf1[40] = bf0[40];
    bf1[41] = halfBtf(-cospi[20], bf0[41], cospi[44], bf0[54], cosBit);
    bf1[42] = halfBtf(-cospi[44], bf0[42], -cospi[20], bf0[53], cosBit);
    bf1[43] = bf0[43];
    bf1[44] = bf0[44];
    bf1[45] = halfBtf(-cospi[52], bf0[45], cospi[12], bf0[50], cosBit);
    bf1[46] = halfBtf(-cospi[12], bf0[46], -cospi[52], bf0[49], cosBit);
    bf1[47] = bf0[47];
    bf1[48] = bf0[48];
    bf1[49] = halfBtf(-cospi[52], bf0[46], cospi[12], bf0[49], cosBit);
    bf1[50] = halfBtf(cospi[12], bf0[45], cospi[52], bf0[50], cosBit);
    bf1[51] = bf0[51];
    bf1[52] = bf0[52];
    bf1[53] = halfBtf(-cospi[20], bf0[42], cospi[44], bf0[53], cosBit);
    bf1[54] = halfBtf(cospi[44], bf0[41], cospi[20], bf0[54], cosBit);
    bf1[55] = bf0[55];
    bf1[56] = bf0[56];
    bf1[57] = halfBtf(-cospi[36], bf0[38], cospi[28], bf0[57], cosBit);
    bf1[58] = halfBtf(cospi[28], bf0[37], cospi[36], bf0[58], cosBit);
    bf1[59] = bf0[59];
    bf1[60] = bf0[60];
    bf1[61] = halfBtf(-cospi[4], bf0[34], cospi[60], bf0[61], cosBit);
    bf1[62] = halfBtf(cospi[60], bf0[33], cospi[4], bf0[62], cosBit);
    bf1[63] = bf0[63];
    //range_check_buf(stage, input, bf1, size, stageRange[stage]);

    // stage 5
    stage++;
    bf0     = step;
    bf1     = output;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = halfBtf(cospi[56], bf0[4], -cospi[8], bf0[7], cosBit);
    bf1[5]  = halfBtf(cospi[24], bf0[5], -cospi[40], bf0[6], cosBit);
    bf1[6]  = halfBtf(cospi[40], bf0[5], cospi[24], bf0[6], cosBit);
    bf1[7]  = halfBtf(cospi[8], bf0[4], cospi[56], bf0[7], cosBit);
    bf1[8]  = clampValue(bf0[8] + bf0[9], stageRange[stage]);
    bf1[9]  = clampValue(bf0[8] - bf0[9], stageRange[stage]);
    bf1[10] = clampValue(-bf0[10] + bf0[11], stageRange[stage]);
    bf1[11] = clampValue(bf0[10] + bf0[11], stageRange[stage]);
    bf1[12] = clampValue(bf0[12] + bf0[13], stageRange[stage]);
    bf1[13] = clampValue(bf0[12] - bf0[13], stageRange[stage]);
    bf1[14] = clampValue(-bf0[14] + bf0[15], stageRange[stage]);
    bf1[15] = clampValue(bf0[14] + bf0[15], stageRange[stage]);
    bf1[16] = bf0[16];
    bf1[17] = halfBtf(-cospi[8], bf0[17], cospi[56], bf0[30], cosBit);
    bf1[18] = halfBtf(-cospi[56], bf0[18], -cospi[8], bf0[29], cosBit);
    bf1[19] = bf0[19];
    bf1[20] = bf0[20];
    bf1[21] = halfBtf(-cospi[40], bf0[21], cospi[24], bf0[26], cosBit);
    bf1[22] = halfBtf(-cospi[24], bf0[22], -cospi[40], bf0[25], cosBit);
    bf1[23] = bf0[23];
    bf1[24] = bf0[24];
    bf1[25] = halfBtf(-cospi[40], bf0[22], cospi[24], bf0[25], cosBit);
    bf1[26] = halfBtf(cospi[24], bf0[21], cospi[40], bf0[26], cosBit);
    bf1[27] = bf0[27];
    bf1[28] = bf0[28];
    bf1[29] = halfBtf(-cospi[8], bf0[18], cospi[56], bf0[29], cosBit);
    bf1[30] = halfBtf(cospi[56], bf0[17], cospi[8], bf0[30], cosBit);
    bf1[31] = bf0[31];
    bf1[32] = clampValue(bf0[32] + bf0[35], stageRange[stage]);
    bf1[33] = clampValue(bf0[33] + bf0[34], stageRange[stage]);
    bf1[34] = clampValue(bf0[33] - bf0[34], stageRange[stage]);
    bf1[35] = clampValue(bf0[32] - bf0[35], stageRange[stage]);
    bf1[36] = clampValue(-bf0[36] + bf0[39], stageRange[stage]);
    bf1[37] = clampValue(-bf0[37] + bf0[38], stageRange[stage]);
    bf1[38] = clampValue(bf0[37] + bf0[38], stageRange[stage]);
    bf1[39] = clampValue(bf0[36] + bf0[39], stageRange[stage]);
    bf1[40] = clampValue(bf0[40] + bf0[43], stageRange[stage]);
    bf1[41] = clampValue(bf0[41] + bf0[42], stageRange[stage]);
    bf1[42] = clampValue(bf0[41] - bf0[42], stageRange[stage]);
    bf1[43] = clampValue(bf0[40] - bf0[43], stageRange[stage]);
    bf1[44] = clampValue(-bf0[44] + bf0[47], stageRange[stage]);
    bf1[45] = clampValue(-bf0[45] + bf0[46], stageRange[stage]);
    bf1[46] = clampValue(bf0[45] + bf0[46], stageRange[stage]);
    bf1[47] = clampValue(bf0[44] + bf0[47], stageRange[stage]);
    bf1[48] = clampValue(bf0[48] + bf0[51], stageRange[stage]);
    bf1[49] = clampValue(bf0[49] + bf0[50], stageRange[stage]);
    bf1[50] = clampValue(bf0[49] - bf0[50], stageRange[stage]);
    bf1[51] = clampValue(bf0[48] - bf0[51], stageRange[stage]);
    bf1[52] = clampValue(-bf0[52] + bf0[55], stageRange[stage]);
    bf1[53] = clampValue(-bf0[53] + bf0[54], stageRange[stage]);
    bf1[54] = clampValue(bf0[53] + bf0[54], stageRange[stage]);
    bf1[55] = clampValue(bf0[52] + bf0[55], stageRange[stage]);
    bf1[56] = clampValue(bf0[56] + bf0[59], stageRange[stage]);
    bf1[57] = clampValue(bf0[57] + bf0[58], stageRange[stage]);
    bf1[58] = clampValue(bf0[57] - bf0[58], stageRange[stage]);
    bf1[59] = clampValue(bf0[56] - bf0[59], stageRange[stage]);
    bf1[60] = clampValue(-bf0[60] + bf0[63], stageRange[stage]);
    bf1[61] = clampValue(-bf0[61] + bf0[62], stageRange[stage]);
    bf1[62] = clampValue(bf0[61] + bf0[62], stageRange[stage]);
    bf1[63] = clampValue(bf0[60] + bf0[63], stageRange[stage]);
    //range_check_buf(stage, input, bf1, size, stageRange[stage]);

    // stage 6
    stage++;
    bf0     = output;
    bf1     = step;
    bf1[0]  = halfBtf(cospi[32], bf0[0], cospi[32], bf0[1], cosBit);
    bf1[1]  = halfBtf(cospi[32], bf0[0], -cospi[32], bf0[1], cosBit);
    bf1[2]  = halfBtf(cospi[48], bf0[2], -cospi[16], bf0[3], cosBit);
    bf1[3]  = halfBtf(cospi[16], bf0[2], cospi[48], bf0[3], cosBit);
    bf1[4]  = clampValue(bf0[4] + bf0[5], stageRange[stage]);
    bf1[5]  = clampValue(bf0[4] - bf0[5], stageRange[stage]);
    bf1[6]  = clampValue(-bf0[6] + bf0[7], stageRange[stage]);
    bf1[7]  = clampValue(bf0[6] + bf0[7], stageRange[stage]);
    bf1[8]  = bf0[8];
    bf1[9]  = halfBtf(-cospi[16], bf0[9], cospi[48], bf0[14], cosBit);
    bf1[10] = halfBtf(-cospi[48], bf0[10], -cospi[16], bf0[13], cosBit);
    bf1[11] = bf0[11];
    bf1[12] = bf0[12];
    bf1[13] = halfBtf(-cospi[16], bf0[10], cospi[48], bf0[13], cosBit);
    bf1[14] = halfBtf(cospi[48], bf0[9], cospi[16], bf0[14], cosBit);
    bf1[15] = bf0[15];
    bf1[16] = clampValue(bf0[16] + bf0[19], stageRange[stage]);
    bf1[17] = clampValue(bf0[17] + bf0[18], stageRange[stage]);
    bf1[18] = clampValue(bf0[17] - bf0[18], stageRange[stage]);
    bf1[19] = clampValue(bf0[16] - bf0[19], stageRange[stage]);
    bf1[20] = clampValue(-bf0[20] + bf0[23], stageRange[stage]);
    bf1[21] = clampValue(-bf0[21] + bf0[22], stageRange[stage]);
    bf1[22] = clampValue(bf0[21] + bf0[22], stageRange[stage]);
    bf1[23] = clampValue(bf0[20] + bf0[23], stageRange[stage]);
    bf1[24] = clampValue(bf0[24] + bf0[27], stageRange[stage]);
    bf1[25] = clampValue(bf0[25] + bf0[26], stageRange[stage]);
    bf1[26] = clampValue(bf0[25] - bf0[26], stageRange[stage]);
    bf1[27] = clampValue(bf0[24] - bf0[27], stageRange[stage]);
    bf1[28] = clampValue(-bf0[28] + bf0[31], stageRange[stage]);
    bf1[29] = clampValue(-bf0[29] + bf0[30], stageRange[stage]);
    bf1[30] = clampValue(bf0[29] + bf0[30], stageRange[stage]);
    bf1[31] = clampValue(bf0[28] + bf0[31], stageRange[stage]);
    bf1[32] = bf0[32];
    bf1[33] = bf0[33];
    bf1[34] = halfBtf(-cospi[8], bf0[34], cospi[56], bf0[61], cosBit);
    bf1[35] = halfBtf(-cospi[8], bf0[35], cospi[56], bf0[60], cosBit);
    bf1[36] = halfBtf(-cospi[56], bf0[36], -cospi[8], bf0[59], cosBit);
    bf1[37] = halfBtf(-cospi[56], bf0[37], -cospi[8], bf0[58], cosBit);
    bf1[38] = bf0[38];
    bf1[39] = bf0[39];
    bf1[40] = bf0[40];
    bf1[41] = bf0[41];
    bf1[42] = halfBtf(-cospi[40], bf0[42], cospi[24], bf0[53], cosBit);
    bf1[43] = halfBtf(-cospi[40], bf0[43], cospi[24], bf0[52], cosBit);
    bf1[44] = halfBtf(-cospi[24], bf0[44], -cospi[40], bf0[51], cosBit);
    bf1[45] = halfBtf(-cospi[24], bf0[45], -cospi[40], bf0[50], cosBit);
    bf1[46] = bf0[46];
    bf1[47] = bf0[47];
    bf1[48] = bf0[48];
    bf1[49] = bf0[49];
    bf1[50] = halfBtf(-cospi[40], bf0[45], cospi[24], bf0[50], cosBit);
    bf1[51] = halfBtf(-cospi[40], bf0[44], cospi[24], bf0[51], cosBit);
    bf1[52] = halfBtf(cospi[24], bf0[43], cospi[40], bf0[52], cosBit);
    bf1[53] = halfBtf(cospi[24], bf0[42], cospi[40], bf0[53], cosBit);
    bf1[54] = bf0[54];
    bf1[55] = bf0[55];
    bf1[56] = bf0[56];
    bf1[57] = bf0[57];
    bf1[58] = halfBtf(-cospi[8], bf0[37], cospi[56], bf0[58], cosBit);
    bf1[59] = halfBtf(-cospi[8], bf0[36], cospi[56], bf0[59], cosBit);
    bf1[60] = halfBtf(cospi[56], bf0[35], cospi[8], bf0[60], cosBit);
    bf1[61] = halfBtf(cospi[56], bf0[34], cospi[8], bf0[61], cosBit);
    bf1[62] = bf0[62];
    bf1[63] = bf0[63];
    //range_check_buf(stage, input, bf1, size, stageRange[stage]);

    // stage 7
    stage++;
    bf0     = step;
    bf1     = output;
    bf1[0]  = clampValue(bf0[0] + bf0[3], stageRange[stage]);
    bf1[1]  = clampValue(bf0[1] + bf0[2], stageRange[stage]);
    bf1[2]  = clampValue(bf0[1] - bf0[2], stageRange[stage]);
    bf1[3]  = clampValue(bf0[0] - bf0[3], stageRange[stage]);
    bf1[4]  = bf0[4];
    bf1[5]  = halfBtf(-cospi[32], bf0[5], cospi[32], bf0[6], cosBit);
    bf1[6]  = halfBtf(cospi[32], bf0[5], cospi[32], bf0[6], cosBit);
    bf1[7]  = bf0[7];
    bf1[8]  = clampValue(bf0[8] + bf0[11], stageRange[stage]);
    bf1[9]  = clampValue(bf0[9] + bf0[10], stageRange[stage]);
    bf1[10] = clampValue(bf0[9] - bf0[10], stageRange[stage]);
    bf1[11] = clampValue(bf0[8] - bf0[11], stageRange[stage]);
    bf1[12] = clampValue(-bf0[12] + bf0[15], stageRange[stage]);
    bf1[13] = clampValue(-bf0[13] + bf0[14], stageRange[stage]);
    bf1[14] = clampValue(bf0[13] + bf0[14], stageRange[stage]);
    bf1[15] = clampValue(bf0[12] + bf0[15], stageRange[stage]);
    bf1[16] = bf0[16];
    bf1[17] = bf0[17];
    bf1[18] = halfBtf(-cospi[16], bf0[18], cospi[48], bf0[29], cosBit);
    bf1[19] = halfBtf(-cospi[16], bf0[19], cospi[48], bf0[28], cosBit);
    bf1[20] = halfBtf(-cospi[48], bf0[20], -cospi[16], bf0[27], cosBit);
    bf1[21] = halfBtf(-cospi[48], bf0[21], -cospi[16], bf0[26], cosBit);
    bf1[22] = bf0[22];
    bf1[23] = bf0[23];
    bf1[24] = bf0[24];
    bf1[25] = bf0[25];
    bf1[26] = halfBtf(-cospi[16], bf0[21], cospi[48], bf0[26], cosBit);
    bf1[27] = halfBtf(-cospi[16], bf0[20], cospi[48], bf0[27], cosBit);
    bf1[28] = halfBtf(cospi[48], bf0[19], cospi[16], bf0[28], cosBit);
    bf1[29] = halfBtf(cospi[48], bf0[18], cospi[16], bf0[29], cosBit);
    bf1[30] = bf0[30];
    bf1[31] = bf0[31];
    bf1[32] = clampValue(bf0[32] + bf0[39], stageRange[stage]);
    bf1[33] = clampValue(bf0[33] + bf0[38], stageRange[stage]);
    bf1[34] = clampValue(bf0[34] + bf0[37], stageRange[stage]);
    bf1[35] = clampValue(bf0[35] + bf0[36], stageRange[stage]);
    bf1[36] = clampValue(bf0[35] - bf0[36], stageRange[stage]);
    bf1[37] = clampValue(bf0[34] - bf0[37], stageRange[stage]);
    bf1[38] = clampValue(bf0[33] - bf0[38], stageRange[stage]);
    bf1[39] = clampValue(bf0[32] - bf0[39], stageRange[stage]);
    bf1[40] = clampValue(-bf0[40] + bf0[47], stageRange[stage]);
    bf1[41] = clampValue(-bf0[41] + bf0[46], stageRange[stage]);
    bf1[42] = clampValue(-bf0[42] + bf0[45], stageRange[stage]);
    bf1[43] = clampValue(-bf0[43] + bf0[44], stageRange[stage]);
    bf1[44] = clampValue(bf0[43] + bf0[44], stageRange[stage]);
    bf1[45] = clampValue(bf0[42] + bf0[45], stageRange[stage]);
    bf1[46] = clampValue(bf0[41] + bf0[46], stageRange[stage]);
    bf1[47] = clampValue(bf0[40] + bf0[47], stageRange[stage]);
    bf1[48] = clampValue(bf0[48] + bf0[55], stageRange[stage]);
    bf1[49] = clampValue(bf0[49] + bf0[54], stageRange[stage]);
    bf1[50] = clampValue(bf0[50] + bf0[53], stageRange[stage]);
    bf1[51] = clampValue(bf0[51] + bf0[52], stageRange[stage]);
    bf1[52] = clampValue(bf0[51] - bf0[52], stageRange[stage]);
    bf1[53] = clampValue(bf0[50] - bf0[53], stageRange[stage]);
    bf1[54] = clampValue(bf0[49] - bf0[54], stageRange[stage]);
    bf1[55] = clampValue(bf0[48] - bf0[55], stageRange[stage]);
    bf1[56] = clampValue(-bf0[56] + bf0[63], stageRange[stage]);
    bf1[57] = clampValue(-bf0[57] + bf0[62], stageRange[stage]);
    bf1[58] = clampValue(-bf0[58] + bf0[61], stageRange[stage]);
    bf1[59] = clampValue(-bf0[59] + bf0[60], stageRange[stage]);
    bf1[60] = clampValue(bf0[59] + bf0[60], stageRange[stage]);
    bf1[61] = clampValue(bf0[58] + bf0[61], stageRange[stage]);
    bf1[62] = clampValue(bf0[57] + bf0[62], stageRange[stage]);
    bf1[63] = clampValue(bf0[56] + bf0[63], stageRange[stage]);
    //range_check_buf(stage, input, bf1, size, stageRange[stage]);

    // stage 8
    stage++;
    bf0     = output;
    bf1     = step;
    bf1[0]  = clampValue(bf0[0] + bf0[7], stageRange[stage]);
    bf1[1]  = clampValue(bf0[1] + bf0[6], stageRange[stage]);
    bf1[2]  = clampValue(bf0[2] + bf0[5], stageRange[stage]);
    bf1[3]  = clampValue(bf0[3] + bf0[4], stageRange[stage]);
    bf1[4]  = clampValue(bf0[3] - bf0[4], stageRange[stage]);
    bf1[5]  = clampValue(bf0[2] - bf0[5], stageRange[stage]);
    bf1[6]  = clampValue(bf0[1] - bf0[6], stageRange[stage]);
    bf1[7]  = clampValue(bf0[0] - bf0[7], stageRange[stage]);
    bf1[8]  = bf0[8];
    bf1[9]  = bf0[9];
    bf1[10] = halfBtf(-cospi[32], bf0[10], cospi[32], bf0[13], cosBit);
    bf1[11] = halfBtf(-cospi[32], bf0[11], cospi[32], bf0[12], cosBit);
    bf1[12] = halfBtf(cospi[32], bf0[11], cospi[32], bf0[12], cosBit);
    bf1[13] = halfBtf(cospi[32], bf0[10], cospi[32], bf0[13], cosBit);
    bf1[14] = bf0[14];
    bf1[15] = bf0[15];
    bf1[16] = clampValue(bf0[16] + bf0[23], stageRange[stage]);
    bf1[17] = clampValue(bf0[17] + bf0[22], stageRange[stage]);
    bf1[18] = clampValue(bf0[18] + bf0[21], stageRange[stage]);
    bf1[19] = clampValue(bf0[19] + bf0[20], stageRange[stage]);
    bf1[20] = clampValue(bf0[19] - bf0[20], stageRange[stage]);
    bf1[21] = clampValue(bf0[18] - bf0[21], stageRange[stage]);
    bf1[22] = clampValue(bf0[17] - bf0[22], stageRange[stage]);
    bf1[23] = clampValue(bf0[16] - bf0[23], stageRange[stage]);
    bf1[24] = clampValue(-bf0[24] + bf0[31], stageRange[stage]);
    bf1[25] = clampValue(-bf0[25] + bf0[30], stageRange[stage]);
    bf1[26] = clampValue(-bf0[26] + bf0[29], stageRange[stage]);
    bf1[27] = clampValue(-bf0[27] + bf0[28], stageRange[stage]);
    bf1[28] = clampValue(bf0[27] + bf0[28], stageRange[stage]);
    bf1[29] = clampValue(bf0[26] + bf0[29], stageRange[stage]);
    bf1[30] = clampValue(bf0[25] + bf0[30], stageRange[stage]);
    bf1[31] = clampValue(bf0[24] + bf0[31], stageRange[stage]);
    bf1[32] = bf0[32];
    bf1[33] = bf0[33];
    bf1[34] = bf0[34];
    bf1[35] = bf0[35];
    bf1[36] = halfBtf(-cospi[16], bf0[36], cospi[48], bf0[59], cosBit);
    bf1[37] = halfBtf(-cospi[16], bf0[37], cospi[48], bf0[58], cosBit);
    bf1[38] = halfBtf(-cospi[16], bf0[38], cospi[48], bf0[57], cosBit);
    bf1[39] = halfBtf(-cospi[16], bf0[39], cospi[48], bf0[56], cosBit);
    bf1[40] = halfBtf(-cospi[48], bf0[40], -cospi[16], bf0[55], cosBit);
    bf1[41] = halfBtf(-cospi[48], bf0[41], -cospi[16], bf0[54], cosBit);
    bf1[42] = halfBtf(-cospi[48], bf0[42], -cospi[16], bf0[53], cosBit);
    bf1[43] = halfBtf(-cospi[48], bf0[43], -cospi[16], bf0[52], cosBit);
    bf1[44] = bf0[44];
    bf1[45] = bf0[45];
    bf1[46] = bf0[46];
    bf1[47] = bf0[47];
    bf1[48] = bf0[48];
    bf1[49] = bf0[49];
    bf1[50] = bf0[50];
    bf1[51] = bf0[51];
    bf1[52] = halfBtf(-cospi[16], bf0[43], cospi[48], bf0[52], cosBit);
    bf1[53] = halfBtf(-cospi[16], bf0[42], cospi[48], bf0[53], cosBit);
    bf1[54] = halfBtf(-cospi[16], bf0[41], cospi[48], bf0[54], cosBit);
    bf1[55] = halfBtf(-cospi[16], bf0[40], cospi[48], bf0[55], cosBit);
    bf1[56] = halfBtf(cospi[48], bf0[39], cospi[16], bf0[56], cosBit);
    bf1[57] = halfBtf(cospi[48], bf0[38], cospi[16], bf0[57], cosBit);
    bf1[58] = halfBtf(cospi[48], bf0[37], cospi[16], bf0[58], cosBit);
    bf1[59] = halfBtf(cospi[48], bf0[36], cospi[16], bf0[59], cosBit);
    bf1[60] = bf0[60];
    bf1[61] = bf0[61];
    bf1[62] = bf0[62];
    bf1[63] = bf0[63];
    //range_check_buf(stage, input, bf1, size, stageRange[stage]);

    // stage 9
    stage++;
    bf0     = step;
    bf1     = output;
    bf1[0]  = clampValue(bf0[0] + bf0[15], stageRange[stage]);
    bf1[1]  = clampValue(bf0[1] + bf0[14], stageRange[stage]);
    bf1[2]  = clampValue(bf0[2] + bf0[13], stageRange[stage]);
    bf1[3]  = clampValue(bf0[3] + bf0[12], stageRange[stage]);
    bf1[4]  = clampValue(bf0[4] + bf0[11], stageRange[stage]);
    bf1[5]  = clampValue(bf0[5] + bf0[10], stageRange[stage]);
    bf1[6]  = clampValue(bf0[6] + bf0[9], stageRange[stage]);
    bf1[7]  = clampValue(bf0[7] + bf0[8], stageRange[stage]);
    bf1[8]  = clampValue(bf0[7] - bf0[8], stageRange[stage]);
    bf1[9]  = clampValue(bf0[6] - bf0[9], stageRange[stage]);
    bf1[10] = clampValue(bf0[5] - bf0[10], stageRange[stage]);
    bf1[11] = clampValue(bf0[4] - bf0[11], stageRange[stage]);
    bf1[12] = clampValue(bf0[3] - bf0[12], stageRange[stage]);
    bf1[13] = clampValue(bf0[2] - bf0[13], stageRange[stage]);
    bf1[14] = clampValue(bf0[1] - bf0[14], stageRange[stage]);
    bf1[15] = clampValue(bf0[0] - bf0[15], stageRange[stage]);
    bf1[16] = bf0[16];
    bf1[17] = bf0[17];
    bf1[18] = bf0[18];
    bf1[19] = bf0[19];
    bf1[20] = halfBtf(-cospi[32], bf0[20], cospi[32], bf0[27], cosBit);
    bf1[21] = halfBtf(-cospi[32], bf0[21], cospi[32], bf0[26], cosBit);
    bf1[22] = halfBtf(-cospi[32], bf0[22], cospi[32], bf0[25], cosBit);
    bf1[23] = halfBtf(-cospi[32], bf0[23], cospi[32], bf0[24], cosBit);
    bf1[24] = halfBtf(cospi[32], bf0[23], cospi[32], bf0[24], cosBit);
    bf1[25] = halfBtf(cospi[32], bf0[22], cospi[32], bf0[25], cosBit);
    bf1[26] = halfBtf(cospi[32], bf0[21], cospi[32], bf0[26], cosBit);
    bf1[27] = halfBtf(cospi[32], bf0[20], cospi[32], bf0[27], cosBit);
    bf1[28] = bf0[28];
    bf1[29] = bf0[29];
    bf1[30] = bf0[30];
    bf1[31] = bf0[31];
    bf1[32] = clampValue(bf0[32] + bf0[47], stageRange[stage]);
    bf1[33] = clampValue(bf0[33] + bf0[46], stageRange[stage]);
    bf1[34] = clampValue(bf0[34] + bf0[45], stageRange[stage]);
    bf1[35] = clampValue(bf0[35] + bf0[44], stageRange[stage]);
    bf1[36] = clampValue(bf0[36] + bf0[43], stageRange[stage]);
    bf1[37] = clampValue(bf0[37] + bf0[42], stageRange[stage]);
    bf1[38] = clampValue(bf0[38] + bf0[41], stageRange[stage]);
    bf1[39] = clampValue(bf0[39] + bf0[40], stageRange[stage]);
    bf1[40] = clampValue(bf0[39] - bf0[40], stageRange[stage]);
    bf1[41] = clampValue(bf0[38] - bf0[41], stageRange[stage]);
    bf1[42] = clampValue(bf0[37] - bf0[42], stageRange[stage]);
    bf1[43] = clampValue(bf0[36] - bf0[43], stageRange[stage]);
    bf1[44] = clampValue(bf0[35] - bf0[44], stageRange[stage]);
    bf1[45] = clampValue(bf0[34] - bf0[45], stageRange[stage]);
    bf1[46] = clampValue(bf0[33] - bf0[46], stageRange[stage]);
    bf1[47] = clampValue(bf0[32] - bf0[47], stageRange[stage]);
    bf1[48] = clampValue(-bf0[48] + bf0[63], stageRange[stage]);
    bf1[49] = clampValue(-bf0[49] + bf0[62], stageRange[stage]);
    bf1[50] = clampValue(-bf0[50] + bf0[61], stageRange[stage]);
    bf1[51] = clampValue(-bf0[51] + bf0[60], stageRange[stage]);
    bf1[52] = clampValue(-bf0[52] + bf0[59], stageRange[stage]);
    bf1[53] = clampValue(-bf0[53] + bf0[58], stageRange[stage]);
    bf1[54] = clampValue(-bf0[54] + bf0[57], stageRange[stage]);
    bf1[55] = clampValue(-bf0[55] + bf0[56], stageRange[stage]);
    bf1[56] = clampValue(bf0[55] + bf0[56], stageRange[stage]);
    bf1[57] = clampValue(bf0[54] + bf0[57], stageRange[stage]);
    bf1[58] = clampValue(bf0[53] + bf0[58], stageRange[stage]);
    bf1[59] = clampValue(bf0[52] + bf0[59], stageRange[stage]);
    bf1[60] = clampValue(bf0[51] + bf0[60], stageRange[stage]);
    bf1[61] = clampValue(bf0[50] + bf0[61], stageRange[stage]);
    bf1[62] = clampValue(bf0[49] + bf0[62], stageRange[stage]);
    bf1[63] = clampValue(bf0[48] + bf0[63], stageRange[stage]);
    //range_check_buf(stage, input, bf1, size, stageRange[stage]);

    // stage 10
    stage++;
    bf0     = output;
    bf1     = step;
    bf1[0]  = clampValue(bf0[0] + bf0[31], stageRange[stage]);
    bf1[1]  = clampValue(bf0[1] + bf0[30], stageRange[stage]);
    bf1[2]  = clampValue(bf0[2] + bf0[29], stageRange[stage]);
    bf1[3]  = clampValue(bf0[3] + bf0[28], stageRange[stage]);
    bf1[4]  = clampValue(bf0[4] + bf0[27], stageRange[stage]);
    bf1[5]  = clampValue(bf0[5] + bf0[26], stageRange[stage]);
    bf1[6]  = clampValue(bf0[6] + bf0[25], stageRange[stage]);
    bf1[7]  = clampValue(bf0[7] + bf0[24], stageRange[stage]);
    bf1[8]  = clampValue(bf0[8] + bf0[23], stageRange[stage]);
    bf1[9]  = clampValue(bf0[9] + bf0[22], stageRange[stage]);
    bf1[10] = clampValue(bf0[10] + bf0[21], stageRange[stage]);
    bf1[11] = clampValue(bf0[11] + bf0[20], stageRange[stage]);
    bf1[12] = clampValue(bf0[12] + bf0[19], stageRange[stage]);
    bf1[13] = clampValue(bf0[13] + bf0[18], stageRange[stage]);
    bf1[14] = clampValue(bf0[14] + bf0[17], stageRange[stage]);
    bf1[15] = clampValue(bf0[15] + bf0[16], stageRange[stage]);
    bf1[16] = clampValue(bf0[15] - bf0[16], stageRange[stage]);
    bf1[17] = clampValue(bf0[14] - bf0[17], stageRange[stage]);
    bf1[18] = clampValue(bf0[13] - bf0[18], stageRange[stage]);
    bf1[19] = clampValue(bf0[12] - bf0[19], stageRange[stage]);
    bf1[20] = clampValue(bf0[11] - bf0[20], stageRange[stage]);
    bf1[21] = clampValue(bf0[10] - bf0[21], stageRange[stage]);
    bf1[22] = clampValue(bf0[9] - bf0[22], stageRange[stage]);
    bf1[23] = clampValue(bf0[8] - bf0[23], stageRange[stage]);
    bf1[24] = clampValue(bf0[7] - bf0[24], stageRange[stage]);
    bf1[25] = clampValue(bf0[6] - bf0[25], stageRange[stage]);
    bf1[26] = clampValue(bf0[5] - bf0[26], stageRange[stage]);
    bf1[27] = clampValue(bf0[4] - bf0[27], stageRange[stage]);
    bf1[28] = clampValue(bf0[3] - bf0[28], stageRange[stage]);
    bf1[29] = clampValue(bf0[2] - bf0[29], stageRange[stage]);
    bf1[30] = clampValue(bf0[1] - bf0[30], stageRange[stage]);
    bf1[31] = clampValue(bf0[0] - bf0[31], stageRange[stage]);
    bf1[32] = bf0[32];
    bf1[33] = bf0[33];
    bf1[34] = bf0[34];
    bf1[35] = bf0[35];
    bf1[36] = bf0[36];
    bf1[37] = bf0[37];
    bf1[38] = bf0[38];
    bf1[39] = bf0[39];
    bf1[40] = halfBtf(-cospi[32], bf0[40], cospi[32], bf0[55], cosBit);
    bf1[41] = halfBtf(-cospi[32], bf0[41], cospi[32], bf0[54], cosBit);
    bf1[42] = halfBtf(-cospi[32], bf0[42], cospi[32], bf0[53], cosBit);
    bf1[43] = halfBtf(-cospi[32], bf0[43], cospi[32], bf0[52], cosBit);
    bf1[44] = halfBtf(-cospi[32], bf0[44], cospi[32], bf0[51], cosBit);
    bf1[45] = halfBtf(-cospi[32], bf0[45], cospi[32], bf0[50], cosBit);
    bf1[46] = halfBtf(-cospi[32], bf0[46], cospi[32], bf0[49], cosBit);
    bf1[47] = halfBtf(-cospi[32], bf0[47], cospi[32], bf0[48], cosBit);
    bf1[48] = halfBtf(cospi[32], bf0[47], cospi[32], bf0[48], cosBit);
    bf1[49] = halfBtf(cospi[32], bf0[46], cospi[32], bf0[49], cosBit);
    bf1[50] = halfBtf(cospi[32], bf0[45], cospi[32], bf0[50], cosBit);
    bf1[51] = halfBtf(cospi[32], bf0[44], cospi[32], bf0[51], cosBit);
    bf1[52] = halfBtf(cospi[32], bf0[43], cospi[32], bf0[52], cosBit);
    bf1[53] = halfBtf(cospi[32], bf0[42], cospi[32], bf0[53], cosBit);
    bf1[54] = halfBtf(cospi[32], bf0[41], cospi[32], bf0[54], cosBit);
    bf1[55] = halfBtf(cospi[32], bf0[40], cospi[32], bf0[55], cosBit);
    bf1[56] = bf0[56];
    bf1[57] = bf0[57];
    bf1[58] = bf0[58];
    bf1[59] = bf0[59];
    bf1[60] = bf0[60];
    bf1[61] = bf0[61];
    bf1[62] = bf0[62];
    bf1[63] = bf0[63];
    //range_check_buf(stage, input, bf1, size, stageRange[stage]);

    // stage 11
    stage++;
    bf0     = step;
    bf1     = output;
    bf1[0]  = clampValue(bf0[0] + bf0[63], stageRange[stage]);
    bf1[1]  = clampValue(bf0[1] + bf0[62], stageRange[stage]);
    bf1[2]  = clampValue(bf0[2] + bf0[61], stageRange[stage]);
    bf1[3]  = clampValue(bf0[3] + bf0[60], stageRange[stage]);
    bf1[4]  = clampValue(bf0[4] + bf0[59], stageRange[stage]);
    bf1[5]  = clampValue(bf0[5] + bf0[58], stageRange[stage]);
    bf1[6]  = clampValue(bf0[6] + bf0[57], stageRange[stage]);
    bf1[7]  = clampValue(bf0[7] + bf0[56], stageRange[stage]);
    bf1[8]  = clampValue(bf0[8] + bf0[55], stageRange[stage]);
    bf1[9]  = clampValue(bf0[9] + bf0[54], stageRange[stage]);
    bf1[10] = clampValue(bf0[10] + bf0[53], stageRange[stage]);
    bf1[11] = clampValue(bf0[11] + bf0[52], stageRange[stage]);
    bf1[12] = clampValue(bf0[12] + bf0[51], stageRange[stage]);
    bf1[13] = clampValue(bf0[13] + bf0[50], stageRange[stage]);
    bf1[14] = clampValue(bf0[14] + bf0[49], stageRange[stage]);
    bf1[15] = clampValue(bf0[15] + bf0[48], stageRange[stage]);
    bf1[16] = clampValue(bf0[16] + bf0[47], stageRange[stage]);
    bf1[17] = clampValue(bf0[17] + bf0[46], stageRange[stage]);
    bf1[18] = clampValue(bf0[18] + bf0[45], stageRange[stage]);
    bf1[19] = clampValue(bf0[19] + bf0[44], stageRange[stage]);
    bf1[20] = clampValue(bf0[20] + bf0[43], stageRange[stage]);
    bf1[21] = clampValue(bf0[21] + bf0[42], stageRange[stage]);
    bf1[22] = clampValue(bf0[22] + bf0[41], stageRange[stage]);
    bf1[23] = clampValue(bf0[23] + bf0[40], stageRange[stage]);
    bf1[24] = clampValue(bf0[24] + bf0[39], stageRange[stage]);
    bf1[25] = clampValue(bf0[25] + bf0[38], stageRange[stage]);
    bf1[26] = clampValue(bf0[26] + bf0[37], stageRange[stage]);
    bf1[27] = clampValue(bf0[27] + bf0[36], stageRange[stage]);
    bf1[28] = clampValue(bf0[28] + bf0[35], stageRange[stage]);
    bf1[29] = clampValue(bf0[29] + bf0[34], stageRange[stage]);
    bf1[30] = clampValue(bf0[30] + bf0[33], stageRange[stage]);
    bf1[31] = clampValue(bf0[31] + bf0[32], stageRange[stage]);
    bf1[32] = clampValue(bf0[31] - bf0[32], stageRange[stage]);
    bf1[33] = clampValue(bf0[30] - bf0[33], stageRange[stage]);
    bf1[34] = clampValue(bf0[29] - bf0[34], stageRange[stage]);
    bf1[35] = clampValue(bf0[28] - bf0[35], stageRange[stage]);
    bf1[36] = clampValue(bf0[27] - bf0[36], stageRange[stage]);
    bf1[37] = clampValue(bf0[26] - bf0[37], stageRange[stage]);
    bf1[38] = clampValue(bf0[25] - bf0[38], stageRange[stage]);
    bf1[39] = clampValue(bf0[24] - bf0[39], stageRange[stage]);
    bf1[40] = clampValue(bf0[23] - bf0[40], stageRange[stage]);
    bf1[41] = clampValue(bf0[22] - bf0[41], stageRange[stage]);
    bf1[42] = clampValue(bf0[21] - bf0[42], stageRange[stage]);
    bf1[43] = clampValue(bf0[20] - bf0[43], stageRange[stage]);
    bf1[44] = clampValue(bf0[19] - bf0[44], stageRange[stage]);
    bf1[45] = clampValue(bf0[18] - bf0[45], stageRange[stage]);
    bf1[46] = clampValue(bf0[17] - bf0[46], stageRange[stage]);
    bf1[47] = clampValue(bf0[16] - bf0[47], stageRange[stage]);
    bf1[48] = clampValue(bf0[15] - bf0[48], stageRange[stage]);
    bf1[49] = clampValue(bf0[14] - bf0[49], stageRange[stage]);
    bf1[50] = clampValue(bf0[13] - bf0[50], stageRange[stage]);
    bf1[51] = clampValue(bf0[12] - bf0[51], stageRange[stage]);
    bf1[52] = clampValue(bf0[11] - bf0[52], stageRange[stage]);
    bf1[53] = clampValue(bf0[10] - bf0[53], stageRange[stage]);
    bf1[54] = clampValue(bf0[9] - bf0[54], stageRange[stage]);
    bf1[55] = clampValue(bf0[8] - bf0[55], stageRange[stage]);
    bf1[56] = clampValue(bf0[7] - bf0[56], stageRange[stage]);
    bf1[57] = clampValue(bf0[6] - bf0[57], stageRange[stage]);
    bf1[58] = clampValue(bf0[5] - bf0[58], stageRange[stage]);
    bf1[59] = clampValue(bf0[4] - bf0[59], stageRange[stage]);
    bf1[60] = clampValue(bf0[3] - bf0[60], stageRange[stage]);
    bf1[61] = clampValue(bf0[2] - bf0[61], stageRange[stage]);
    bf1[62] = clampValue(bf0[1] - bf0[62], stageRange[stage]);
    bf1[63] = clampValue(bf0[0] - bf0[63], stageRange[stage]);
}

// TXH1: the landed 2-arg bd-8 overload (the {16,...} shim array)
void idct64(const std::int32_t input[64], std::int32_t output[64]) {
    static constexpr std::int8_t stageRange[12] = {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
    idct64Hb(input, output, stageRange);
}

// static av1_fadst32_new (transforms.c:1908-2314) with the reference's
// cos_bit parameter (TX1: fwd_cos_bit_row[3][2] = 13 at the TX_32X16 ADST row
// pass, [3][4]/[4][3] = 11 at the TX_32X64 / TX_64X32 ADST row passes)
void fadst32B(const std::int32_t input[32], std::int32_t output[32], int cosBit) {
    const std::int32_t* cospi = cospiRow(cosBit);
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

// static av1_fadst32_new (transforms.c:1908-2314) at the square config:
// cos_bit = 12
void fadst32(const std::int32_t input[32], std::int32_t output[32]) {
    fadst32B(input, output, 12);
}

// svt_av1_idct32_new (inv_transforms.c:378-727), cos_bit = 12 (INV_COS_BIT);
// stage_range audit (verified against the verbatim source, L3): clamp_value
// on the butterfly adds at stages 3-9 only; stage 1-2 range checks are
// commented out in SVT itself. TXH1: the shim became a parameter; the 2-arg
// form below is the bd-8 overload
void idct32Hb(const std::int32_t input[32], std::int32_t output[32], const std::int8_t* stageRange) {
    const int8_t cosBit = 12;
    const std::int32_t* cospi = kCospi12;
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

// TXH1: the landed 2-arg bd-8 overload (the {16,...} shim array)
void idct32(const std::int32_t input[32], std::int32_t output[32]) {
    static constexpr std::int8_t stageRange[10] = {16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
    idct32Hb(input, output, stageRange);
}

// static av1_iadst32_new (inv_transforms.c:1132-1565), cos_bit = 12;
// stage_range audit (L3): clamp_buf on EVERY stage (SVT applies clamp_buf to
// the whole 32-vector after each stage: stage 0 input clamp :1141, stages
// 1-11 :1179,:1218,:1256,:1295,:1333,:1372,:1410,:1449,:1487,:1526,:1564);
// NO all-zero early-out at 32. TXH1: the fixed-16 clamps became
// stage_range-parameterized (stage 0 stays implicit - the 2D core's pre-clamp
// bit equals opt_range at every bd, so the reference's stage-0 clamp is dead
// in context; named deviation shared with the landed form)
void iadst32Hb(const std::int32_t input[32], std::int32_t output[32], const std::int8_t* stageRange) {
    const int8_t cosBit = 12;
    const std::int32_t* cospi = kCospi12;
    std::int32_t bf0[32];
    std::int32_t step[32];

    // stage 0: clamp_buf on the input (the host callers pre-clamp to 16 in
    // the 2D core; the helper's own clamp_buf(input, 32, 16) is a no-op
    // there - kept implicit, values arrive within range)
    // stage 1 (fixed sign-permutation; clamp at stageRange[1])
    static const int idxTab[32] = {0, -32, -16, 16, -8, 24, 8, -24, -4, 28, 12, -20, 4, -28, -12, 20,
                                   -2, 30, 14, -18, 6, -26, -10, 22, 2, -30, -14, 18, -6, 26, 10, -22};
    for (int i = 0; i < 32; ++i) {
        const int j = idxTab[i];
        bf0[i] = j >= 0 ? input[j] : -input[-1 - j];
    }
    clampBufIv(bf0, 32, stageRange[1]);

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
    clampBufIv(step, 32, stageRange[2]);

    // stage 3 (pairwise sum/diff on (0,2),(1,3)... groups of 2 within 4)
    for (int q = 0; q < 8; ++q) {
        const int base = 4 * q;
        bf0[base]     = step[base] + step[base + 2];
        bf0[base + 1] = step[base + 1] + step[base + 3];
        bf0[base + 2] = step[base] - step[base + 2];
        bf0[base + 3] = step[base + 1] - step[base + 3];
    }
    clampBufIv(bf0, 32, stageRange[3]);

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
    clampBufIv(step, 32, stageRange[4]);

    // stage 5 (group-of-8 sum/diff: [0..3]+/-[4..7])
    for (int q = 0; q < 4; ++q) {
        const int base = 8 * q;
        for (int i = 0; i < 4; ++i) {
            bf0[base + i] = step[base + i] + step[base + 4 + i];
            bf0[base + 4 + i] = step[base + i] - step[base + 4 + i];
        }
    }
    clampBufIv(bf0, 32, stageRange[5]);

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
    clampBufIv(step, 32, stageRange[6]);

    // stage 7 (group-of-16 sum/diff: [0..7]+/-[8..15], [16..23]+/-[24..31])
    for (int h = 0; h < 2; ++h) {
        const int base = 16 * h;
        for (int i = 0; i < 8; ++i) {
            bf0[base + i] = step[base + i] + step[base + 8 + i];
            bf0[base + 8 + i] = step[base + i] - step[base + 8 + i];
        }
    }
    clampBufIv(bf0, 32, stageRange[7]);

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
    clampBufIv(step, 32, stageRange[8]);

    // stage 9 (group-of-32 sum/diff: [0..15]+/-[16..31])
    for (int i = 0; i < 16; ++i) {
        bf0[i] = step[i] + step[16 + i];
        bf0[16 + i] = step[i] - step[16 + i];
    }
    clampBufIv(bf0, 32, stageRange[9]);

    // stage 10 (full 32 half_btf sweep, cospi pairs (1,63),(5,59)...(61,3))
    for (int k = 0; k < 16; ++k) {
        const int c1 = 4 * k + 1;
        step[2 * k]     = halfBtf(cospi[c1], bf0[2 * k], cospi[64 - c1], bf0[2 * k + 1], cosBit);
        step[2 * k + 1] = halfBtf(cospi[64 - c1], bf0[2 * k], -cospi[c1], bf0[2 * k + 1], cosBit);
    }
    clampBufIv(step, 32, stageRange[10]);

    // stage 11 (final permutation; clamp)
    static const int permTab[32] = {1, 30, 3, 28, 5, 26, 7, 24, 9, 22, 11, 20, 13, 18, 15, 16,
                                    17, 14, 19, 12, 21, 10, 23, 8, 25, 6, 27, 4, 29, 2, 31, 0};
    for (int i = 0; i < 32; ++i) output[i] = step[permTab[i]];
    clampBufIv(output, 32, stageRange[11]);
}

// TXH1: the landed 2-arg bd-8 overload (fixed-16 clamps at every stage)
void iadst32(const std::int32_t input[32], std::int32_t output[32]) {
    static constexpr std::int8_t stageRange[12] = {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
    iadst32Hb(input, output, stageRange);
}

// svt_av1_fdct4_new (transforms.c:162) with the reference's cos_bit parameter
// (TX1: fwd_cos_bit_row[0][2] = 12 reaches the row pass at TX_4X16)
void fdct4B(const std::int32_t input[4], std::int32_t output[4], int cosBit) {
    const std::int32_t* cospi = cospiRow(cosBit);
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
    step[0] = halfBtf(cospi[32], bf0[0], cospi[32], bf0[1], cosBit);
    step[1] = halfBtf(-cospi[32], bf0[1], cospi[32], bf0[0], cosBit);
    step[2] = halfBtf(cospi[48], bf0[2], cospi[16], bf0[3], cosBit);
    step[3] = halfBtf(cospi[48], bf0[3], -cospi[16], bf0[2], cosBit);

    output[0] = step[0];
    output[1] = step[2];
    output[2] = step[1];
    output[3] = step[3];
}

// svt_av1_fdct4_new (transforms.c:162) at the square config: cos_bit = 13
void fdct4(const std::int32_t input[4], std::int32_t output[4]) {
    fdct4B(input, output, 13);
}

// sinpi_arr(cos_bit) (inv_transforms.h:249): per-bit sinpi table row, the
// fadst4 counterpart of cospiRow (13 = kSinpi13, 12 = kSinpi12)
const std::int32_t* sinpiRow(int cosBit) {
    if (cosBit == 13) return kSinpi13;
    return kSinpi12;
}

// TX1: tx_size_wide (common_utils.c:116-118), indexed by TxSize in the
// definitions.h:955-974 order
int txSizeWide(TxSize size) {
    static const int kWide[19] = {4, 8, 16, 32, 64, 4, 8, 8, 16, 16, 32, 32, 64, 4, 16, 8, 32, 16, 64};
    return kWide[static_cast<int>(size)];
}

// TX1: tx_size_high (common_utils.c:120-122)
int txSizeHigh(TxSize size) {
    static const int kHigh[19] = {4, 8, 16, 32, 64, 8, 4, 16, 8, 32, 16, 64, 32, 16, 4, 32, 8, 64, 16};
    return kHigh[static_cast<int>(size)];
}

// TX1: get_rect_tx_log_ratio (inv_transforms.h:293-315); every TxSize admits
// a ratio in {0, +-1, +-2} and nothing else, so the assert(0) fallback of the
// reference is unreachable through the TxSize-typed surface
int rectTxLogRatio(int col, int row) {
    if (col == row) {
        return 0;
    }
    if (col > row) {
        if (col == row * 2) {
            return 1;
        }
        if (col == row * 4) {
            return 2;
        }
        return 0;  // Invalid (reference asserts)
    }
    if (row == col * 2) {
        return -1;
    }
    if (row == col * 4) {
        return -2;
    }
    return 0;  // Invalid (reference asserts)
}

namespace {

// TX1: fwd_shift_4x4 .. fwd_shift_64x16 (transforms.c:122-140) and the
// fwd_txfm_shift_ls[TX_SIZES_ALL] list (:142-146), indexed by TxSize
const std::int8_t kFwdShift4x4[3]   = {2, 0, 0};
const std::int8_t kFwdShift8x8[3]   = {2, -1, 0};
const std::int8_t kFwdShift16x16[3] = {2, -2, 0};
const std::int8_t kFwdShift32x32[3] = {2, -4, 0};
const std::int8_t kFwdShift64x64[3] = {0, -2, -2};
const std::int8_t kFwdShift4x8[3]   = {2, -1, 0};
const std::int8_t kFwdShift8x4[3]   = {2, -1, 0};
const std::int8_t kFwdShift8x16[3]  = {2, -2, 0};
const std::int8_t kFwdShift16x8[3]  = {2, -2, 0};
const std::int8_t kFwdShift16x32[3] = {2, -4, 0};
const std::int8_t kFwdShift32x16[3] = {2, -4, 0};
const std::int8_t kFwdShift32x64[3] = {0, -2, -2};
const std::int8_t kFwdShift64x32[3] = {2, -4, -2};
const std::int8_t kFwdShift4x16[3]  = {2, -1, 0};
const std::int8_t kFwdShift16x4[3]  = {2, -1, 0};
const std::int8_t kFwdShift8x32[3]  = {2, -2, 0};
const std::int8_t kFwdShift32x8[3]  = {2, -2, 0};
const std::int8_t kFwdShift16x64[3] = {0, -2, 0};
const std::int8_t kFwdShift64x16[3] = {2, -4, 0};

const std::int8_t* kFwdTxfmShiftLs[19] = {
    kFwdShift4x4,  kFwdShift8x8,  kFwdShift16x16, kFwdShift32x32, kFwdShift64x64, kFwdShift4x8,   kFwdShift8x4,
    kFwdShift8x16, kFwdShift16x8,  kFwdShift16x32, kFwdShift32x16, kFwdShift32x64, kFwdShift64x32, kFwdShift4x16,
    kFwdShift16x4, kFwdShift8x32,  kFwdShift32x8,  kFwdShift16x64, kFwdShift64x16,
};

// TX1: fwd_cos_bit_col / fwd_cos_bit_row (transforms.c:19-22), [txw_idx][txh_idx]
const std::int8_t kFwdCosBitCol[5][5] = {
    {13, 13, 13, 0, 0}, {13, 13, 13, 12, 0}, {13, 13, 13, 12, 13}, {0, 13, 13, 12, 13}, {0, 0, 13, 12, 13}};
const std::int8_t kFwdCosBitRow[5][5] = {
    {13, 13, 12, 0, 0}, {13, 13, 13, 12, 0}, {13, 13, 12, 13, 12}, {0, 12, 13, 12, 11}, {0, 0, 12, 11, 10}};

// TX1: tx_size_wide_log2 / tx_size_high_log2 (common_utils.c:125-132)
const std::int8_t kTxSizeWideLog2[19] = {2, 3, 4, 5, 6, 2, 3, 3, 4, 4, 5, 5, 6, 2, 4, 3, 5, 4, 6};
const std::int8_t kTxSizeHighLog2[19] = {2, 3, 4, 5, 6, 3, 2, 4, 3, 5, 4, 6, 5, 4, 2, 5, 3, 6, 4};

// TX1/TX2: TxType1D (the four separable 1D classes) and vtx_tab/htx_tab
// (inv_transforms.h:45-62 / :63-80), TX2: all 16 rows verbatim
enum TxType1D { DCT_1D = 0, ADST_1D = 1, FLIPADST_1D = 2, IDTX_1D = 3 };

const std::int8_t kVtxTab[16] = {DCT_1D,    ADST_1D, DCT_1D,    ADST_1D, FLIPADST_1D, DCT_1D,
                                 FLIPADST_1D, ADST_1D, FLIPADST_1D, IDTX_1D, DCT_1D,    IDTX_1D,
                                 ADST_1D,   IDTX_1D, FLIPADST_1D, IDTX_1D};
const std::int8_t kHtxTab[16] = {DCT_1D,    DCT_1D,    ADST_1D,   ADST_1D, DCT_1D,      FLIPADST_1D,
                                 FLIPADST_1D, FLIPADST_1D, ADST_1D,  IDTX_1D, IDTX_1D,     DCT_1D,
                                 IDTX_1D,   ADST_1D,   IDTX_1D,   FLIPADST_1D};

// TX1: av1_txfm_type_ls (inv_transforms.h:191-196), [size_log2][TxType1D]
const TxfmTypeLs kAv1TxfmTypeLs[5][4] = {
    {TxfmTypeLs::DCT4, TxfmTypeLs::ADST4, TxfmTypeLs::ADST4, TxfmTypeLs::IDENTITY4},
    {TxfmTypeLs::DCT8, TxfmTypeLs::ADST8, TxfmTypeLs::ADST8, TxfmTypeLs::IDENTITY8},
    {TxfmTypeLs::DCT16, TxfmTypeLs::ADST16, TxfmTypeLs::ADST16, TxfmTypeLs::IDENTITY16},
    {TxfmTypeLs::DCT32, TxfmTypeLs::ADST32, TxfmTypeLs::ADST32, TxfmTypeLs::IDENTITY32},
    {TxfmTypeLs::DCT64, TxfmTypeLs::INVALID, TxfmTypeLs::INVALID, TxfmTypeLs::IDENTITY64},
};

// TX1: av1_txfm_stage_num_list (inv_transforms.h:197-212), indexed by TxfmType
const std::int8_t kAv1TxfmStageNumList[14] = {4, 6, 8, 10, 12, 7, 8, 10, 12, 1, 1, 1, 1, 1};

// TX1: the fwd range_mult2 tables (transforms.c:92-105) and the
// fwd_txfm_range_mult2_list[TXFM_TYPES] (:107-120), indexed by TxfmType
const std::int8_t kFdct4RangeMult2[4]    = {0, 2, 3, 3};
const std::int8_t kFdct8RangeMult2[6]    = {0, 2, 4, 5, 5, 5};
const std::int8_t kFdct16RangeMult2[8]   = {0, 2, 4, 6, 7, 7, 7, 7};
const std::int8_t kFdct32RangeMult2[10]  = {0, 2, 4, 6, 8, 9, 9, 9, 9, 9};
const std::int8_t kFdct64RangeMult2[12]  = {0, 2, 4, 6, 8, 10, 11, 11, 11, 11, 11, 11};
const std::int8_t kFadst4RangeMult2[7]   = {0, 2, 4, 3, 3, 3, 3};
const std::int8_t kFadst8RangeMult2[8]   = {0, 0, 1, 3, 3, 5, 5, 5};
const std::int8_t kFadst16RangeMult2[10] = {0, 0, 1, 3, 3, 5, 5, 7, 7, 7};
const std::int8_t kFadst32RangeMult2[12] = {0, 0, 1, 3, 3, 5, 5, 7, 7, 9, 9, 9};
const std::int8_t kFidtx4RangeMult2[1]   = {1};
const std::int8_t kFidtx8RangeMult2[1]   = {2};
const std::int8_t kFidtx16RangeMult2[1]  = {3};
const std::int8_t kFidtx32RangeMult2[1]  = {4};
const std::int8_t kFidtx64RangeMult2[1]  = {5};

const std::int8_t* kFwdTxfmRangeMult2List[14] = {kFdct4RangeMult2,  kFdct8RangeMult2,  kFdct16RangeMult2,
                                                 kFdct32RangeMult2, kFdct64RangeMult2, kFadst4RangeMult2,
                                                 kFadst8RangeMult2, kFadst16RangeMult2, kFadst32RangeMult2,
                                                 kFidtx4RangeMult2, kFidtx8RangeMult2, kFidtx16RangeMult2,
                                                 kFidtx32RangeMult2, kFidtx64RangeMult2};

// TX2: set_flip_cfg (inv_transforms.h:179-181) over the full 16-type list:
// get_flip_cfg (:139-177) verbatim - 0/0 for DCT_DCT..ADST_ADST and for
// IDTX/V_DCT/H_DCT/V_ADST/H_ADST; ud=1 lr=0 for FLIPADST_DCT, FLIPADST_ADST
// and V_FLIPADST; ud=0 lr=1 for DCT_FLIPADST, ADST_FLIPADST and H_FLIPADST;
// FLIPADST_FLIPADST flips both. The default assert(0) branch is unreachable
// through the 16-value enum (the reference holds it for out-of-range casts).
void setFlipCfg(Txfm2dFlipCfg& cfg, TxType type) {
    cfg.udFlip = 0;
    cfg.lrFlip = 0;
    switch (type) {
    case TxType::FLIPADST_DCT:
    case TxType::FLIPADST_ADST:
    case TxType::V_FLIPADST:
        cfg.udFlip = 1;
        break;
    case TxType::DCT_FLIPADST:
    case TxType::ADST_FLIPADST:
    case TxType::H_FLIPADST:
        cfg.lrFlip = 1;
        break;
    case TxType::FLIPADST_FLIPADST:
        cfg.udFlip = 1;
        cfg.lrFlip = 1;
        break;
    default:
        break;
    }
}

// TX1: set_fwd_txfm_non_scale_range (transforms.c:2471-2492)
void setFwdTxfmNonScaleRange(Txfm2dFlipCfg& cfg) {
    for (int i = 0; i < 12; ++i) {
        cfg.stageRangeCol[i] = 0;
        cfg.stageRangeRow[i] = 0;
    }
    if (cfg.txfmTypeCol == TxfmTypeLs::INVALID) {
        return;
    }
    const std::int8_t* rangeMult2Col = kFwdTxfmRangeMult2List[static_cast<int>(cfg.txfmTypeCol)];
    const int stageNumCol = cfg.stageNumCol < 12 ? cfg.stageNumCol : 12;
    for (int i = 0; i < stageNumCol; ++i) {
        cfg.stageRangeCol[i] = (rangeMult2Col[i] + 1) >> 1;
    }
    if (cfg.txfmTypeRow != TxfmTypeLs::INVALID) {
        const std::int8_t* rangeMult2Row = kFwdTxfmRangeMult2List[static_cast<int>(cfg.txfmTypeRow)];
        const int stageNumRow = cfg.stageNumRow < 12 ? cfg.stageNumRow : 12;
        for (int i = 0; i < stageNumRow; ++i) {
            cfg.stageRangeRow[i] = (rangeMult2Col[cfg.stageNumCol - 1] + rangeMult2Row[i] + 1) >> 1;
        }
    }
}

// TX1: inv_shift_4x4 .. inv_shift_64x16 (inv_transforms.c:18-36) and the
// svt_aom_inv_txfm_shift_ls[TX_SIZES_ALL] list (:38-42)
const std::int8_t kInvShift4x4[2]   = {0, -4};
const std::int8_t kInvShift8x8[2]   = {-1, -4};
const std::int8_t kInvShift16x16[2] = {-2, -4};
const std::int8_t kInvShift32x32[2] = {-2, -4};
const std::int8_t kInvShift64x64[2] = {-2, -4};
const std::int8_t kInvShift4x8[2]   = {0, -4};
const std::int8_t kInvShift8x4[2]   = {0, -4};
const std::int8_t kInvShift8x16[2]  = {-1, -4};
const std::int8_t kInvShift16x8[2]  = {-1, -4};
const std::int8_t kInvShift16x32[2] = {-1, -4};
const std::int8_t kInvShift32x16[2] = {-1, -4};
const std::int8_t kInvShift32x64[2] = {-1, -4};
const std::int8_t kInvShift64x32[2] = {-1, -4};
const std::int8_t kInvShift4x16[2]  = {-1, -4};
const std::int8_t kInvShift16x4[2]  = {-1, -4};
const std::int8_t kInvShift8x32[2]  = {-2, -4};
const std::int8_t kInvShift32x8[2]  = {-2, -4};
const std::int8_t kInvShift16x64[2] = {-2, -4};
const std::int8_t kInvShift64x16[2] = {-2, -4};

const std::int8_t* kInvTxfmShiftLs[19] = {
    kInvShift4x4,  kInvShift8x8,  kInvShift16x16, kInvShift32x32, kInvShift64x64, kInvShift4x8,   kInvShift8x4,
    kInvShift8x16, kInvShift16x8, kInvShift16x32, kInvShift32x16, kInvShift32x64, kInvShift64x32, kInvShift4x16,
    kInvShift16x4, kInvShift8x32, kInvShift32x8,  kInvShift16x64, kInvShift64x16,
};

// TX1: inv_cos_bit_col / inv_cos_bit_row (inv_transforms.h:32-43): every
// reachable entry is INV_COS_BIT = 12; the 0s mark impossible (txw,txh) pairs
const std::int8_t kInvCosBitCol[5][5] = {
    {12, 12, 12, 0, 0}, {12, 12, 12, 12, 0}, {12, 12, 12, 12, 12}, {0, 12, 12, 12, 12}, {0, 0, 12, 12, 12}};
const std::int8_t kInvCosBitRow[5][5] = {
    {12, 12, 12, 0, 0}, {12, 12, 12, 12, 0}, {12, 12, 12, 12, 12}, {0, 12, 12, 12, 12}, {0, 0, 12, 12, 12}};

// TX1: iadst4_range (inv_transforms.h:214), the ADST4 inverse stage-range
// special case get_inv_txfm_cfg copies into the cfg
const std::int8_t kIadst4Range[7] = {0, 1, 0, 0, 0, 0, 0};

}  // namespace

// TX1: svt_aom_transform_config (transforms.c:2494-2510)
void fwdTransformConfig(TxType type, TxSize size, Txfm2dFlipCfg& cfg) {
    cfg.txSize = size;
    setFlipCfg(cfg, type);
    const int txType1dCol = kVtxTab[static_cast<int>(type)];
    const int txType1dRow = kHtxTab[static_cast<int>(type)];
    const int txwIdx = kTxSizeWideLog2[static_cast<int>(size)] - 2;
    const int txhIdx = kTxSizeHighLog2[static_cast<int>(size)] - 2;
    cfg.shift       = kFwdTxfmShiftLs[static_cast<int>(size)];
    cfg.cosBitCol   = kFwdCosBitCol[txwIdx][txhIdx];
    cfg.cosBitRow   = kFwdCosBitRow[txwIdx][txhIdx];
    cfg.txfmTypeCol = kAv1TxfmTypeLs[txhIdx][txType1dCol];
    cfg.txfmTypeRow = kAv1TxfmTypeLs[txwIdx][txType1dRow];
    cfg.stageNumCol = kAv1TxfmStageNumList[static_cast<int>(cfg.txfmTypeCol)];
    cfg.stageNumRow = kAv1TxfmStageNumList[static_cast<int>(cfg.txfmTypeRow)];
    setFwdTxfmNonScaleRange(cfg);
}

// TX1: svt_av1_get_inv_txfm_cfg (inv_transforms.c:2470-2494). The ADST4 rows
// get the iadst4_range stage-range copy (:2485-2491); the inverse 1D kernels
// bake the bd-8 range shim, so the cfg copy is bookkeeping here.
void invTransformConfig(TxType type, TxSize size, Txfm2dFlipCfg& cfg) {
    cfg.txSize = size;
    setFlipCfg(cfg, type);
    for (int i = 0; i < 12; ++i) {
        cfg.stageRangeCol[i] = 0;
        cfg.stageRangeRow[i] = 0;
    }
    const int txType1dCol = kVtxTab[static_cast<int>(type)];
    const int txType1dRow = kHtxTab[static_cast<int>(type)];
    const int txwIdx = kTxSizeWideLog2[static_cast<int>(size)] - 2;
    const int txhIdx = kTxSizeHighLog2[static_cast<int>(size)] - 2;
    cfg.shift       = kInvTxfmShiftLs[static_cast<int>(size)];
    cfg.cosBitCol   = kInvCosBitCol[txwIdx][txhIdx];
    cfg.cosBitRow   = kInvCosBitRow[txwIdx][txhIdx];
    cfg.txfmTypeCol = kAv1TxfmTypeLs[txhIdx][txType1dCol];
    if (cfg.txfmTypeCol == TxfmTypeLs::ADST4) {
        for (int i = 0; i < 7; ++i) {
            cfg.stageRangeCol[i] = kIadst4Range[i];
        }
    }
    cfg.txfmTypeRow = kAv1TxfmTypeLs[txwIdx][txType1dRow];
    if (cfg.txfmTypeRow == TxfmTypeLs::ADST4) {
        for (int i = 0; i < 7; ++i) {
            cfg.stageRangeRow[i] = kIadst4Range[i];
        }
    }
    cfg.stageNumCol = kAv1TxfmStageNumList[static_cast<int>(cfg.txfmTypeCol)];
    cfg.stageNumRow = kAv1TxfmStageNumList[static_cast<int>(cfg.txfmTypeRow)];
}

// svt_av1_fadst4_new (transforms.c) with the reference's cos_bit parameter
// (TX1: fwd_cos_bit_row[0][2] = 12 reaches the ADST4 row pass at
// TX_4X16 + ADST_ADST); all-zero early-out preserved
void fadst4B(const std::int32_t input[4], std::int32_t output[4], int cosBit) {
    const std::int32_t* sinpi = sinpiRow(cosBit);
    const std::int32_t x0 = input[0];
    const std::int32_t x1 = input[1];
    const std::int32_t x2 = input[2];
    const std::int32_t x3 = input[3];

    if (!(x0 | x1 | x2 | x3)) {
        output[0] = output[1] = output[2] = output[3] = 0;
        return;
    }

    std::int32_t s0, s1, s2, s3, s4, s5, s6, s7;

    s0 = sinpi[1] * x0;
    s1 = sinpi[4] * x0;
    s2 = sinpi[2] * x1;
    s3 = sinpi[1] * x1;
    s4 = sinpi[3] * x2;
    s5 = sinpi[4] * x3;
    s6 = sinpi[2] * x3;
    s7 = x0 + x1;

    s7 = s7 - x3;

    std::int32_t y0 = s0 + s2;
    std::int32_t y1 = sinpi[3] * s7;
    std::int32_t y2 = s1 - s3;
    std::int32_t y3 = s4;

    y0 = y0 + s5;
    y2 = y2 + s6;

    s0 = y0 + y3;
    s1 = y1;
    s2 = y2 - y3;
    s3 = y2 - y0;

    s3 = s3 + y3;

    output[0] = roundShift(s0, cosBit);
    output[1] = roundShift(s1, cosBit);
    output[2] = roundShift(s2, cosBit);
    output[3] = roundShift(s3, cosBit);
}

// svt_av1_fadst4_new (transforms.c) at the square config: cos_bit = 13
void fadst4(const std::int32_t input[4], std::int32_t output[4]) {
    fadst4B(input, output, 13);
}

// TX2: svt_av1_fidentity4_c (transforms.c:2318-2325): round_shift(in*5793,12);
// cos_bit and stage_range are unused in the reference ((void)'d); the 5793/12
// constants are new_sqrt2/new_sqrt2_bits (inv_transforms.h:253-255)
void fwdIdentity4(const std::int32_t input[4], std::int32_t output[4], int cosBit) {
    (void)cosBit;
    for (int i = 0; i < 4; ++i) {
        output[i] = roundShift(static_cast<std::int64_t>(input[i]) * 5793, 12);
    }
}

// TX2: svt_av1_fidentity8_c (transforms.c:2327-2333): output = input * 2
void fwdIdentity8(const std::int32_t input[8], std::int32_t output[8], int cosBit) {
    (void)cosBit;
    for (int i = 0; i < 8; ++i) {
        output[i] = input[i] * 2;
    }
}

// TX2: svt_av1_fidentity16_c (transforms.c:2335-2342): round_shift(in*2*5793,12)
void fwdIdentity16(const std::int32_t input[16], std::int32_t output[16], int cosBit) {
    (void)cosBit;
    for (int i = 0; i < 16; ++i) {
        output[i] = roundShift(static_cast<std::int64_t>(input[i]) * 2 * 5793, 12);
    }
}

// TX2: svt_av1_fidentity32_c (transforms.c:2344-2350): output = input * 4
void fwdIdentity32(const std::int32_t input[32], std::int32_t output[32], int cosBit) {
    (void)cosBit;
    for (int i = 0; i < 32; ++i) {
        output[i] = input[i] * 4;
    }
}

// TX2: av1_fidentity64_c (transforms.c:2352-2359): round_shift(in*4*5793,12)
void fwdIdentity64(const std::int32_t input[64], std::int32_t output[64], int cosBit) {
    (void)cosBit;
    for (int i = 0; i < 64; ++i) {
        output[i] = roundShift(static_cast<std::int64_t>(input[i]) * 4 * 5793, 12);
    }
}

// TX2: svt_av1_iidentity4_c (inv_transforms.c:2346-2355): round_shift(5793*in,12)
void invIdentity4(const std::int32_t input[4], std::int32_t output[4], int cosBit) {
    (void)cosBit;
    for (int i = 0; i < 4; ++i) {
        output[i] = roundShift(static_cast<std::int64_t>(5793) * input[i], 12);
    }
}

// TX2: svt_av1_iidentity8_c (inv_transforms.c:2357-2363): output = input * 2
void invIdentity8(const std::int32_t input[8], std::int32_t output[8], int cosBit) {
    (void)cosBit;
    for (int i = 0; i < 8; ++i) {
        output[i] = static_cast<std::int32_t>(static_cast<std::int64_t>(input[i]) * 2);
    }
}

// TX2: svt_av1_iidentity16_c (inv_transforms.c:2365-2372): round_shift(5793*2*in,12)
void invIdentity16(const std::int32_t input[16], std::int32_t output[16], int cosBit) {
    (void)cosBit;
    for (int i = 0; i < 16; ++i) {
        output[i] = roundShift(static_cast<std::int64_t>(5793) * 2 * input[i], 12);
    }
}

// TX2: svt_av1_iidentity32_c (inv_transforms.c:2374-2380): output = input * 4
void invIdentity32(const std::int32_t input[32], std::int32_t output[32], int cosBit) {
    (void)cosBit;
    for (int i = 0; i < 32; ++i) {
        output[i] = static_cast<std::int32_t>(static_cast<std::int64_t>(input[i]) * 4);
    }
}

// TX2: av1_iidentity64_c (inv_transforms.c:2382-2389): round_shift(5793*4*in,12)
void invIdentity64(const std::int32_t input[64], std::int32_t output[64], int cosBit) {
    (void)cosBit;
    for (int i = 0; i < 64; ++i) {
        output[i] = roundShift(static_cast<std::int64_t>(5793) * 4 * input[i], 12);
    }
}

// TXH1: the identity Hb wrappers (the reference iidentity*_c carry a
// stage_range parameter without consuming it)
void invIdentity4Hb(const std::int32_t input[4], std::int32_t output[4], const std::int8_t* stageRange) {
    (void)stageRange;
    invIdentity4(input, output, 0);
}

void invIdentity8Hb(const std::int32_t input[8], std::int32_t output[8], const std::int8_t* stageRange) {
    (void)stageRange;
    invIdentity8(input, output, 0);
}

void invIdentity16Hb(const std::int32_t input[16], std::int32_t output[16], const std::int8_t* stageRange) {
    (void)stageRange;
    invIdentity16(input, output, 0);
}

void invIdentity32Hb(const std::int32_t input[32], std::int32_t output[32], const std::int8_t* stageRange) {
    (void)stageRange;
    invIdentity32(input, output, 0);
}

void invIdentity64Hb(const std::int32_t input[64], std::int32_t output[64], const std::int8_t* stageRange) {
    (void)stageRange;
    invIdentity64(input, output, 0);
}

namespace {

using TxfmFn = void (*)(const std::int32_t*, std::int32_t*);

using TxfmFnB = void (*)(const std::int32_t*, std::int32_t*, int);



}  // namespace

// av1_ext_tx_used (common_utils.c:197-204), restricted to the two rows an
// intra + reduced_tx_set=1 block can select: row 0 = EXT_TX_SET_DCTONLY
// (index 0 only) and row 2 = EXT_TX_SET_DTT4_IDTX (indices 0,1,2,3,9 - the
// four separable types plus IDTX=9 per definitions.h:1031-1047). Rows 1, 3, 4
// and 5 are unreachable here: they need is_inter or use_reduced_set=0
// (get_ext_tx_set_type, common_utils.h:59-77).
static const bool kExtTxUsed[2][4] = {
    {true, false, false, false},   // EXT_TX_SET_DCTONLY
    {true, true, true, true},      // EXT_TX_SET_DTT4_IDTX
};

// g_intra_mode_to_tx_type (mode_decision.c:2959-2973), by position. The
// oblique slots are labelled D117/D153/D207/D63 here and D113/D157/D203/D67 in
// aom/dav1d; the values are identical in both trees, and the caller supplies the
// mode already folded into this 0..12 luma-space space.
static const TxType kIntraModeToTxType[13] = {
    TxType::DCT_DCT,    // 0 DC
    TxType::ADST_DCT,   // 1 V
    TxType::DCT_ADST,   // 2 H
    TxType::DCT_DCT,    // 3 D45
    TxType::ADST_ADST,  // 4 D135
    TxType::ADST_DCT,   // 5 D117
    TxType::DCT_ADST,   // 6 D153
    TxType::DCT_ADST,   // 7 D207
    TxType::ADST_DCT,   // 8 D63
    TxType::ADST_ADST,  // 9 SMOOTH
    TxType::ADST_DCT,   // 10 SMOOTH_V
    TxType::DCT_ADST,   // 11 SMOOTH_H
    TxType::ADST_ADST,  // 12 PAETH
};

TxType intraUvTxType(int mode, TxSizeSqUp sqrUp) {
    // if (txsize_sqr_up_map[tx_size] > TX_32X32) return DCT_DCT;
    if (sqrUp > TxSizeSqUp::TX_32X32) return TxType::DCT_DCT;
    const TxType type = kIntraModeToTxType[mode];
    // get_ext_tx_set_type (common_utils.h:59-77) at is_inter=0,
    // use_reduced_set=1: DCTONLY when sqr_up >= TX_32X32, else DTT4_IDTX.
    const int setRow = (sqrUp >= TxSizeSqUp::TX_32X32) ? 0 : 1;
    return !kExtTxUsed[setRow][static_cast<int>(type)] ? TxType::DCT_DCT : type;
}

// svt_av1_transform_two_d_4x4_c (transforms.c:2581-2589): config + core, the
// reference's own square-facade shape (TX2: replaces the TX0-era inline fold;
// the landed DCT_DCT/ADST outputs are unchanged - the core computes the same
// values, the l3 suite and the golden gate are the guard)
void fwdTxfm2d4x4(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_4X4, cfg);
    std::int32_t buf[4 * 4];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_transform_two_d_8x8_c (transforms.c:2571-2579): config + core, the
// reference's own square-facade shape (TX2: replaces the TX0-era inline fold;
// the landed DCT/ADST outputs are unchanged - the suite and gate are the guard)
void fwdTxfm2d8x8(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_8X8, cfg);
    std::int32_t buf[8 * 8];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_transform_two_d_16x16_c (transforms.c:2561-2569): config + core, the
// reference's own square-facade shape (TX2: replaces the TX0-era inline fold;
// the landed DCT/ADST outputs are unchanged - the suite and gate are the guard)
void fwdTxfm2d16x16(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_16X16, cfg);
    std::int32_t buf[16 * 16];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_transform_two_d_32x32_c (transforms.c:2551-2559): config + core, the
// reference's own square-facade shape (TX2: replaces the TX0-era inline fold;
// the landed DCT outputs are unchanged - the suite and gate are the guard)
void fwdTxfm2d32x32(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_32X32, cfg);
    std::int32_t buf[32 * 32];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_transform_two_d_64x64_c (transforms.c:2541-2549): config + core, the
// reference's own square-facade shape (TX2: replaces the TX0-era inline fold;
// the landed DCT outputs are unchanged - the suite and gate are the guard;
// DCT-only at 64x64, inv_transforms.h:196, is the type table's job now)
void fwdTxfm2d64x64(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_64X64, cfg);
    std::int32_t buf[64 * 64];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

namespace {

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

// svt_av1_inv_txfm2d_add_4x4_c (inv_transforms.c:2591-2595): facade = config +
// core, the reference's own shape (TX2: replaces the TX0-era inline fold; the
// landed DCT/ADST outputs are unchanged - the suite and gate are the guard)
void invTxfm2dAdd4x4(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_4X4, cfg);
    std::int32_t txfmBuf[4 * 4 + 2 * 4];
    invTxfm2dAddCore(coeffs, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_8x8_c (inv_transforms.c:2597-2601): facade = config +
// core, the reference's own shape (TX2: replaces the TX0-era inline fold; the
// landed DCT/ADST outputs are unchanged - the suite and gate are the guard)
void invTxfm2dAdd8x8(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_8X8, cfg);
    std::int32_t txfmBuf[8 * 8 + 2 * 8];
    invTxfm2dAddCore(coeffs, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_16x16_c (inv_transforms.c:2603-2607): facade = config
// + core, the reference's own shape (TX2: replaces the TX0-era inline fold;
// the landed DCT/ADST outputs are unchanged - the suite and gate are the guard)
void invTxfm2dAdd16x16(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_16X16, cfg);
    std::int32_t txfmBuf[16 * 16 + 2 * 16];
    invTxfm2dAddCore(coeffs, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_32x32_c (inv_transforms.c:2609-2613): facade = config
// + core, the reference's own shape (TX2: replaces the TX0-era inline fold;
// the landed DCT outputs are unchanged - the suite and gate are the guard;
// the DCT_DCT/IDTX-only wrapper gate lives in invTxfmSupported)
void invTxfm2dAdd32x32(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_32X32, cfg);
    std::int32_t txfmBuf[32 * 32 + 2 * 32];
    invTxfm2dAddCore(coeffs, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_64x64_c is the COMPACTING facade in the reference
// (inv_transforms.c:2615-2628 remaps a 32x32 block into the top-left of a
// 64x64 area); the LANDED port facade is the 4096 form instead - the l6/GPU
// callers expand the compacted dqcoeff themselves and pass a full 64x64 block
// (the FS5d named follow-up in AGENTS.md). TX2 keeps that contract and only
// routes the body through the core (the landed DCT outputs are unchanged -
// the suite and gate are the guard).
void invTxfm2dAdd64x64(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_64X64, cfg);
    std::int32_t txfmBuf[64 * 64 + 2 * 64];
    invTxfm2dAddCore(coeffs, dst, stride, cfg, txfmBuf);
}

// ===================================================================
// TX1: the non-square 2D transform drivers (src/l3_transforms, A1).
// Config machinery + fwd/inv cores + the 14 non-square shapes, ported from
// the pinned third_party/SVT-AV1/Source/Lib/Codec/transforms.c and
// inv_transforms.c. Host only (GPU twins out of scope).
// ===================================================================

namespace {

// svt_aom_fwd_txfm_type_to_func (transforms.c:2361-2394) over the landed 1D
// kernels: every fwd 1D class is cos_bit-parameterized here (the B variants);
// TX2 adds the IDENTITY* kernels (fidentity, transforms.c:2381-2390); INVALID
// maps to nullptr + assert
using TxfmFnCB = void (*)(const std::int32_t*, std::int32_t*, int);

TxfmFnCB fwd1d(TxfmTypeLs type) {
    switch (type) {
    case TxfmTypeLs::DCT4: return fdct4B;
    case TxfmTypeLs::ADST4: return fadst4B;
    case TxfmTypeLs::DCT8: return fdct8B;
    case TxfmTypeLs::ADST8: return fadst8B;
    case TxfmTypeLs::DCT16: return fdct16B;
    case TxfmTypeLs::ADST16: return fadst16B;
    case TxfmTypeLs::DCT32: return fdct32B;
    case TxfmTypeLs::ADST32: return fadst32B;
    case TxfmTypeLs::DCT64: return fdct64B;
    case TxfmTypeLs::IDENTITY4: return fwdIdentity4;
    case TxfmTypeLs::IDENTITY8: return fwdIdentity8;
    case TxfmTypeLs::IDENTITY16: return fwdIdentity16;
    case TxfmTypeLs::IDENTITY32: return fwdIdentity32;
    case TxfmTypeLs::IDENTITY64: return fwdIdentity64;
    default: return nullptr;
    }
}

// svt_av1_round_shift_array_c (transforms.c:2449 / inv_transforms.c:2449)
void roundShiftArrayTx1(std::int32_t* arr, int size, int bit) {
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

}  // namespace

// av1_tranform_two_d_core_c (transforms.c:2398-2469) with the port's full
// 16-type scope (TX2): the ud_flip read and lr_flip write branches live
// (:2430-2438 read, :2443-2452 write), bit_depth fixed 8. The reference reuses
// the output buffer as scratch; the port keeps local temps (same values, the
// scratch is invisible). The gen_fwd_stage_range arrays (transforms.c:148-160)
// are NOT computed here: the landed fwd 1D kernels take no stage_range and
// clamp nothing (the reference (void)s it too), so the arrays had no consumer;
// the cfg's non-scale ranges remain and are test-pinned.
void fwdTxfm2dCore(const std::int16_t* input, std::uint32_t stride, std::int32_t* output,
                   const Txfm2dFlipCfg& cfg, std::int32_t* buf) {
    const int txfmSizeCol = txSizeWide(cfg.txSize);
    const int txfmSizeRow = txSizeHigh(cfg.txSize);
    const std::int8_t* shift = cfg.shift;
    const int rectType = rectTxLogRatio(txfmSizeCol, txfmSizeRow);

    const TxfmFnCB txfmCol = fwd1d(cfg.txfmTypeCol);
    const TxfmFnCB txfmRow = fwd1d(cfg.txfmTypeRow);
    assert(txfmCol != nullptr);
    assert(txfmRow != nullptr);

    std::int32_t tempIn[64];
    std::int32_t tempOut[64];

    // Columns
    for (int c = 0; c < txfmSizeCol; ++c) {
        if (cfg.udFlip == 0) {
            for (int r = 0; r < txfmSizeRow; ++r) {
                tempIn[r] = input[r * stride + c];
            }
        } else {
            for (int r = 0; r < txfmSizeRow; ++r) {
                // flip upside down (transforms.c:2435-2438)
                tempIn[r] = input[(txfmSizeRow - r - 1) * stride + c];
            }
        }
        roundShiftArrayTx1(tempIn, txfmSizeRow, -shift[0]);
        txfmCol(tempIn, tempOut, cfg.cosBitCol);
        roundShiftArrayTx1(tempOut, txfmSizeRow, -shift[1]);
        if (cfg.lrFlip == 0) {
            for (int r = 0; r < txfmSizeRow; ++r) {
                buf[r * txfmSizeCol + c] = tempOut[r];
            }
        } else {
            for (int r = 0; r < txfmSizeRow; ++r) {
                // flip from left to right (transforms.c:2448-2451)
                buf[r * txfmSizeCol + (txfmSizeCol - c - 1)] = tempOut[r];
            }
        }
    }

    // Rows
    for (int r = 0; r < txfmSizeRow; ++r) {
        txfmRow(buf + r * txfmSizeCol, output + r * txfmSizeCol, cfg.cosBitRow);
        roundShiftArrayTx1(output + r * txfmSizeCol, txfmSizeCol, -shift[2]);
        if (rectType == 1 || rectType == -1) {
            // Multiply everything by sqrt2 when the size ratio is 2:1
            // (transforms.c:2460-2467; new_sqrt2 = 5793, new_sqrt2_bits = 12,
            // inv_transforms.h:253-255)
            for (int c = 0; c < txfmSizeCol; ++c) {
                output[r * txfmSizeCol + c] =
                    roundShift(static_cast<std::int64_t>(output[r * txfmSizeCol + c]) * 5793, 12);
            }
        }
    }
}

// svt_av1_fwd_txfm2d_4x8_c (transforms.c:2790-2797)
void fwdTxfm2d4x8(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_4X8, cfg);
    std::int32_t buf[4 * 8];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_fwd_txfm2d_8x4_c (transforms.c:2781-2788)
void fwdTxfm2d8x4(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_8X4, cfg);
    std::int32_t buf[8 * 4];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_fwd_txfm2d_8x16_c (transforms.c:2736-2743)
void fwdTxfm2d8x16(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_8X16, cfg);
    std::int32_t buf[8 * 16];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_fwd_txfm2d_16x8_c (transforms.c:2727-2734)
void fwdTxfm2d16x8(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_16X8, cfg);
    std::int32_t buf[16 * 8];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_fwd_txfm2d_16x32_c (transforms.c:2718-2725)
void fwdTxfm2d16x32(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_16X32, cfg);
    std::int32_t buf[16 * 32];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_fwd_txfm2d_32x16_c (transforms.c:2709-2716)
void fwdTxfm2d32x16(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_32X16, cfg);
    std::int32_t buf[32 * 16];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_fwd_txfm2d_32x64_c (transforms.c:2616-2625)
void fwdTxfm2d32x64(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_32X64, cfg);
    std::int32_t buf[32 * 64];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_fwd_txfm2d_64x32_c (transforms.c:2594-2602)
void fwdTxfm2d64x32(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_64X32, cfg);
    std::int32_t buf[64 * 32];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_fwd_txfm2d_4x16_c (transforms.c:2772-2779)
void fwdTxfm2d4x16(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_4X16, cfg);
    std::int32_t buf[4 * 16];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_fwd_txfm2d_16x4_c (transforms.c:2763-2770)
void fwdTxfm2d16x4(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_16X4, cfg);
    std::int32_t buf[16 * 4];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_fwd_txfm2d_8x32_c (transforms.c:2754-2761)
void fwdTxfm2d8x32(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_8X32, cfg);
    std::int32_t buf[8 * 32];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_fwd_txfm2d_32x8_c (transforms.c:2745-2752)
void fwdTxfm2d32x8(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_32X8, cfg);
    std::int32_t buf[32 * 8];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_fwd_txfm2d_16x64_c (transforms.c:2655-2664)
void fwdTxfm2d16x64(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_16X64, cfg);
    std::int32_t buf[16 * 64];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

// svt_av1_fwd_txfm2d_64x16_c (transforms.c:2633-2641)
void fwdTxfm2d64x16(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, TxSize::TX_64X16, cfg);
    std::int32_t buf[64 * 16];
    fwdTxfm2dCore(input, stride, output, cfg, buf);
}

namespace {

// svt_aom_inv_txfm_type_to_func (inv_transforms.c:2391-2425) over the landed
// inverse 1D kernels (all cos_bit 12 = INV_COS_BIT, matching
// inv_cos_bit_col/row); TX2 adds the IDENTITY* kernels (iidentity,
// inv_transforms.c:2411-2420); INVALID maps to nullptr + assert
using InvTxfmFnTx1 = void (*)(const std::int32_t*, std::int32_t*);

// TX2: the identity kernels carry the reference's (in, out, cos_bit) signature
// (cos_bit unused); the inv core's 1D table is 2-arg, so wrap
void invIdentity4Tx1(const std::int32_t* in, std::int32_t* out) { invIdentity4(in, out, 0); }
void invIdentity8Tx1(const std::int32_t* in, std::int32_t* out) { invIdentity8(in, out, 0); }
void invIdentity16Tx1(const std::int32_t* in, std::int32_t* out) { invIdentity16(in, out, 0); }
void invIdentity32Tx1(const std::int32_t* in, std::int32_t* out) { invIdentity32(in, out, 0); }
void invIdentity64Tx1(const std::int32_t* in, std::int32_t* out) { invIdentity64(in, out, 0); }

InvTxfmFnTx1 inv1dTx1(TxfmTypeLs type) {
    switch (type) {
    case TxfmTypeLs::DCT4: return idct4;
    case TxfmTypeLs::ADST4: return iadst4;
    case TxfmTypeLs::DCT8: return idct8;
    case TxfmTypeLs::ADST8: return iadst8;
    case TxfmTypeLs::DCT16: return idct16;
    case TxfmTypeLs::ADST16: return iadst16;
    case TxfmTypeLs::DCT32: return idct32;
    case TxfmTypeLs::ADST32: return iadst32;
    case TxfmTypeLs::DCT64: return idct64;
    case TxfmTypeLs::IDENTITY4: return invIdentity4Tx1;
    case TxfmTypeLs::IDENTITY8: return invIdentity8Tx1;
    case TxfmTypeLs::IDENTITY16: return invIdentity16Tx1;
    case TxfmTypeLs::IDENTITY32: return invIdentity32Tx1;
    case TxfmTypeLs::IDENTITY64: return invIdentity64Tx1;
    default: return nullptr;
    }
}

// highbd_clip_pixel_add (inv_transforms.c:2444-2447) at bd = 8: the
// HIGHBD_WRAPLOW range check is dead at 8-bit magnitudes, leaving
// clip_pixel_highbd(dest + trans, 8)
void clipPixelAddTx1(std::uint8_t* dst, std::int32_t trans) {
    const int v = static_cast<int>(*dst) + trans;
    *dst = static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

}  // namespace

// inv_txfm2d_add_c (inv_transforms.c:2496-2579) with the port's full 16-type
// scope (TX2): the lr_flip column read and ud_flip row write branches live
// (:2554-2563 read, :2567-2577 write), bd fixed 8, the new_inv_sqrt2
// pre-multiply live for the 2:1 shapes (rect_type +-1, inv_transforms.c:2534-
// 2538; new_inv_sqrt2 = 2896, new_sqrt2_bits = 12). gen_inv_stage_range
// (inv_transforms.c:44-86) is NOT computed here: at bd=8 it writes opt_range
// 16 into every stage, which is exactly the {16,...} shim the landed inverse
// 1D kernels bake in (B1 gate lines), so the arrays had no consumer.
void invTxfm2dAddCore(const std::int32_t* input, std::uint8_t* dst, std::uint32_t stride,
                      const Txfm2dFlipCfg& cfg, std::int32_t* txfmBuf) {
    const int txfmSizeCol = txSizeWide(cfg.txSize);
    const int txfmSizeRow = txSizeHigh(cfg.txSize);
    const std::int8_t* shift = cfg.shift;
    const int rectType = rectTxLogRatio(txfmSizeCol, txfmSizeRow);

    const InvTxfmFnTx1 txfmCol = inv1dTx1(cfg.txfmTypeCol);
    const InvTxfmFnTx1 txfmRow = inv1dTx1(cfg.txfmTypeRow);
    assert(txfmCol != nullptr);
    assert(txfmRow != nullptr);

    const int bufOffset = txfmSizeRow > txfmSizeCol ? txfmSizeRow : txfmSizeCol;
    std::int32_t* tempIn = txfmBuf;
    std::int32_t* tempOut = tempIn + bufOffset;
    std::int32_t* buf = tempOut + bufOffset;
    std::int32_t* bufPtr = buf;

    // Rows
    for (int r = 0; r < txfmSizeRow; ++r) {
        if (rectType == 1 || rectType == -1) {
            for (int c = 0; c < txfmSizeCol; ++c) {
                tempIn[c] = roundShift(static_cast<std::int64_t>(input[c]) * 2896, 12);
            }
        } else {
            for (int c = 0; c < txfmSizeCol; ++c) {
                tempIn[c] = input[c];
            }
        }
        clampBufIv(tempIn, txfmSizeCol, kInvClampBit);
        txfmRow(tempIn, bufPtr);
        roundShiftArrayIv(bufPtr, txfmSizeCol, -shift[0]);
        input += txfmSizeCol;
        bufPtr += txfmSizeCol;
    }

    // Columns
    for (int c = 0; c < txfmSizeCol; ++c) {
        if (cfg.lrFlip == 0) {
            for (int r = 0; r < txfmSizeRow; ++r) {
                tempIn[r] = buf[r * txfmSizeCol + c];
            }
        } else {
            // flip left right (inv_transforms.c:2558-2563)
            for (int r = 0; r < txfmSizeRow; ++r) {
                tempIn[r] = buf[r * txfmSizeCol + (txfmSizeCol - c - 1)];
            }
        }
        clampBufIv(tempIn, txfmSizeRow, kInvClampBit);
        txfmCol(tempIn, tempOut);
        roundShiftArrayIv(tempOut, txfmSizeRow, -shift[1]);
        if (cfg.udFlip == 0) {
            for (int r = 0; r < txfmSizeRow; ++r) {
                clipPixelAddTx1(dst + r * stride + c, tempOut[r]);
            }
        } else {
            // flip upside down (inv_transforms.c:2572-2576)
            for (int r = 0; r < txfmSizeRow; ++r) {
                clipPixelAddTx1(dst + r * stride + c, tempOut[txfmSizeRow - r - 1]);
            }
        }
    }
}

// svt_av1_inv_txfm2d_add_4x8_c (inv_transforms.c:2630-2635); the tx_size and
// lossless parameters the facade threads are UNUSED in the reference
void invTxfm2dAdd4x8(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_4X8, cfg);
    std::int32_t txfmBuf[4 * 8 + 2 * 8];
    invTxfm2dAddCore(coeffs, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_8x4_c (inv_transforms.c:2637-2642)
void invTxfm2dAdd8x4(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_8X4, cfg);
    std::int32_t txfmBuf[8 * 4 + 2 * 8];
    invTxfm2dAddCore(coeffs, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_8x16_c (inv_transforms.c:2644-2650); eob UNUSED
void invTxfm2dAdd8x16(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_8X16, cfg);
    std::int32_t txfmBuf[8 * 16 + 2 * 16];
    invTxfm2dAddCore(coeffs, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_16x8_c (inv_transforms.c:2652-2658); eob UNUSED
void invTxfm2dAdd16x8(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_16X8, cfg);
    std::int32_t txfmBuf[16 * 8 + 2 * 16];
    invTxfm2dAddCore(coeffs, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_16x32_c (inv_transforms.c:2660-2666); eob UNUSED
void invTxfm2dAdd16x32(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_16X32, cfg);
    std::int32_t txfmBuf[16 * 32 + 2 * 32];
    invTxfm2dAddCore(coeffs, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_32x16_c (inv_transforms.c:2668-2674); eob UNUSED
void invTxfm2dAdd32x16(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_32X16, cfg);
    std::int32_t txfmBuf[32 * 16 + 2 * 32];
    invTxfm2dAddCore(coeffs, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_4x16_c (inv_transforms.c:2736-2742)
void invTxfm2dAdd4x16(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_4X16, cfg);
    std::int32_t txfmBuf[4 * 16 + 2 * 16];
    invTxfm2dAddCore(coeffs, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_16x4_c (inv_transforms.c:2743-2749)
void invTxfm2dAdd16x4(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_16X4, cfg);
    std::int32_t txfmBuf[16 * 4 + 2 * 16];
    invTxfm2dAddCore(coeffs, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_8x32_c (inv_transforms.c:2750-2756); eob UNUSED
void invTxfm2dAdd8x32(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_8X32, cfg);
    std::int32_t txfmBuf[8 * 32 + 2 * 32];
    invTxfm2dAddCore(coeffs, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_32x8_c (inv_transforms.c:2758-2764); eob UNUSED
void invTxfm2dAdd32x8(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_32X8, cfg);
    std::int32_t txfmBuf[32 * 8 + 2 * 32];
    invTxfm2dAddCore(coeffs, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_64x32_c (inv_transforms.c:2676-2690): the 32x32
// compacted input is remapped into the top-left of a 64-wide, 32-tall block
// with the rest zeroed, then the facade runs (eob UNUSED)
void invTxfm2dAdd64x32(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_64X32, cfg);
    std::int32_t modInput[64 * 32];
    for (int row = 0; row < 32; ++row) {
        for (int col = 0; col < 32; ++col) {
            modInput[row * 64 + col] = coeffs[row * 32 + col];
            modInput[row * 64 + col + 32] = 0;
        }
    }
    std::int32_t txfmBuf[64 * 32 + 2 * 64];
    invTxfm2dAddCore(modInput, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_32x64_c (inv_transforms.c:2692-2704): the 32x32
// compacted input is copied contiguously into the top half of a 32-wide,
// 64-tall block with the bottom half zeroed, then the facade runs (eob UNUSED)
void invTxfm2dAdd32x64(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_32X64, cfg);
    std::int32_t modInput[32 * 64];
    for (int i = 0; i < 32 * 32; ++i) {
        modInput[i] = coeffs[i];
        modInput[i + 32 * 32] = 0;
    }
    std::int32_t txfmBuf[32 * 64 + 2 * 64];
    invTxfm2dAddCore(modInput, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_16x64_c (inv_transforms.c:2706-2718): the 16x32
// compacted input is copied contiguously into the top half of a 16-wide,
// 64-tall block with the bottom half zeroed, then the facade runs (eob UNUSED)
void invTxfm2dAdd16x64(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_16X64, cfg);
    std::int32_t modInput[16 * 64];
    for (int i = 0; i < 16 * 32; ++i) {
        modInput[i] = coeffs[i];
        modInput[i + 16 * 32] = 0;
    }
    std::int32_t txfmBuf[16 * 64 + 2 * 64];
    invTxfm2dAddCore(modInput, dst, stride, cfg, txfmBuf);
}

// svt_av1_inv_txfm2d_add_64x16_c (inv_transforms.c:2720-2734): the 32x16
// compacted input is remapped into the left half of a 64-wide, 16-tall block
// with the rest zeroed, then the facade runs (eob UNUSED)
void invTxfm2dAdd64x16(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, TxSize::TX_64X16, cfg);
    std::int32_t modInput[64 * 16];
    for (int row = 0; row < 16; ++row) {
        for (int col = 0; col < 32; ++col) {
            modInput[row * 64 + col] = coeffs[row * 32 + col];
            modInput[row * 64 + col + 32] = 0;
        }
    }
    std::int32_t txfmBuf[16 * 64 + 2 * 64];
    invTxfm2dAddCore(modInput, dst, stride, cfg, txfmBuf);
}

// TX1/TX2: the fwd dispatch's admissible predicate (av1_estimate_transform_default,
// transforms.c:3185-3360): the type table gates first (av1_txfm_type_ls), then
// the dispatch asserts: DCT_DCT only at TX_64X32/32X64/64X16/16X64 and TX_64X64
// (:3193, :3208, :3223, :3238 and the 64x64 case); DCT_DCT or IDTX at
// TX_32X16/16X32/32X8/8X32/32X32 (:3254, :3266, :3286, :3298, :3336); TX2 adds
// the six one-axis types at TX_32X32 through the transform_two_d_32x32_c
// fallback branch (:3329-3331, live at the default build's
// CONFIG_ENABLE_NON_DCT_LARGE_TX=1, EbConfigMacros.h:246); the small shapes
// (both axes <= 16) carry no assert, so every cfg-valid type is admitted
bool fwdTxfmSupported(TxSize size, TxType type) {
    Txfm2dFlipCfg cfg;
    fwdTransformConfig(type, size, cfg);
    if (cfg.txfmTypeCol == TxfmTypeLs::INVALID || cfg.txfmTypeRow == TxfmTypeLs::INVALID) {
        return false;
    }
    const int w = txSizeWide(size);
    const int h = txSizeHigh(size);
    if (w < 32 && h < 32) {
        return true;
    }
    if (w >= 64 || h >= 64) {
        // TX_64X32/32X64/64X16/16X64 and TX_64X64: the asserts name DCT_DCT
        // only (transforms.c:3193, :3208, :3223, :3238 and the 64x64 case)
        return type == TxType::DCT_DCT;
    }
    if (type == TxType::DCT_DCT || type == TxType::IDTX) {
        return true;
    }
    if (w == 32 && h == 32) {
        return type == TxType::V_DCT || type == TxType::H_DCT || type == TxType::V_ADST ||
               type == TxType::H_ADST || type == TxType::V_FLIPADST || type == TxType::H_FLIPADST;
    }
    return false;
}

// TX1/TX2: the inv dispatch's admissible predicate (highbd_inv_txfm_add +
// wrappers, inv_transforms.c:3167-3236): the non-square wrappers carry no type
// assert, so the gate is the type table alone except at the squares TX_32X32
// (the wrapper switch takes DCT_DCT or IDTX, :2924-2932 - TX2 adds IDTX) and
// TX_64X64 (DCT_DCT, :2940)
bool invTxfmSupported(TxSize size, TxType type) {
    Txfm2dFlipCfg cfg;
    invTransformConfig(type, size, cfg);
    if (cfg.txfmTypeCol == TxfmTypeLs::INVALID || cfg.txfmTypeRow == TxfmTypeLs::INVALID) {
        return false;
    }
    const int w = txSizeWide(size);
    const int h = txSizeHigh(size);
    if (w == 32 && h == 32) {
        return type == TxType::DCT_DCT || type == TxType::IDTX;
    }
    if (w == 64 && h == 64) {
        return type == TxType::DCT_DCT;
    }
    return true;
}

// TX1: av1_estimate_transform_default's switch (transforms.c:3185-3360) over
// the named drivers; the svt_handle_transform* energy/repack calls and the
// N2/N4/ONLY_DC siblings are PARKED (RDO plumbing, precision variants)
void fwdTxfm2d(TxSize size, const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type) {
    assert(fwdTxfmSupported(size, type));
    switch (size) {
    case TxSize::TX_64X32: fwdTxfm2d64x32(input, output, stride, type); break;
    case TxSize::TX_32X64: fwdTxfm2d32x64(input, output, stride, type); break;
    case TxSize::TX_64X16: fwdTxfm2d64x16(input, output, stride, type); break;
    case TxSize::TX_16X64: fwdTxfm2d16x64(input, output, stride, type); break;
    case TxSize::TX_32X16: fwdTxfm2d32x16(input, output, stride, type); break;
    case TxSize::TX_16X32: fwdTxfm2d16x32(input, output, stride, type); break;
    case TxSize::TX_16X8: fwdTxfm2d16x8(input, output, stride, type); break;
    case TxSize::TX_8X16: fwdTxfm2d8x16(input, output, stride, type); break;
    case TxSize::TX_32X8: fwdTxfm2d32x8(input, output, stride, type); break;
    case TxSize::TX_8X32: fwdTxfm2d8x32(input, output, stride, type); break;
    case TxSize::TX_16X4: fwdTxfm2d16x4(input, output, stride, type); break;
    case TxSize::TX_4X16: fwdTxfm2d4x16(input, output, stride, type); break;
    case TxSize::TX_8X4: fwdTxfm2d8x4(input, output, stride, type); break;
    case TxSize::TX_4X8: fwdTxfm2d4x8(input, output, stride, type); break;
    case TxSize::TX_64X64: fwdTxfm2d64x64(input, output, stride, type); break;
    case TxSize::TX_32X32: fwdTxfm2d32x32(input, output, stride, type); break;
    case TxSize::TX_16X16: fwdTxfm2d16x16(input, output, stride, type); break;
    case TxSize::TX_8X8: fwdTxfm2d8x8(input, output, stride, type); break;
    case TxSize::TX_4X4: fwdTxfm2d4x4(input, output, stride, type); break;
    default: assert(0); break;
    }
}

// TX1: highbd_inv_txfm_add's switch (inv_transforms.c:3167-3236) over the
// named facades
void invTxfm2dAdd(TxSize size, const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type) {
    assert(invTxfmSupported(size, type));
    switch (size) {
    case TxSize::TX_32X32: invTxfm2dAdd32x32(coeffs, dst, stride, type); break;
    case TxSize::TX_16X16: invTxfm2dAdd16x16(coeffs, dst, stride, type); break;
    case TxSize::TX_8X8: invTxfm2dAdd8x8(coeffs, dst, stride, type); break;
    case TxSize::TX_4X8: invTxfm2dAdd4x8(coeffs, dst, stride, type); break;
    case TxSize::TX_8X4: invTxfm2dAdd8x4(coeffs, dst, stride, type); break;
    case TxSize::TX_8X16: invTxfm2dAdd8x16(coeffs, dst, stride, type); break;
    case TxSize::TX_16X8: invTxfm2dAdd16x8(coeffs, dst, stride, type); break;
    case TxSize::TX_16X32: invTxfm2dAdd16x32(coeffs, dst, stride, type); break;
    case TxSize::TX_32X16: invTxfm2dAdd32x16(coeffs, dst, stride, type); break;
    case TxSize::TX_64X64: invTxfm2dAdd64x64(coeffs, dst, stride, type); break;
    case TxSize::TX_32X64: invTxfm2dAdd32x64(coeffs, dst, stride, type); break;
    case TxSize::TX_64X32: invTxfm2dAdd64x32(coeffs, dst, stride, type); break;
    case TxSize::TX_16X64: invTxfm2dAdd16x64(coeffs, dst, stride, type); break;
    case TxSize::TX_64X16: invTxfm2dAdd64x16(coeffs, dst, stride, type); break;
    case TxSize::TX_4X4: invTxfm2dAdd4x4(coeffs, dst, stride, type); break;
    case TxSize::TX_16X4: invTxfm2dAdd16x4(coeffs, dst, stride, type); break;
    case TxSize::TX_4X16: invTxfm2dAdd4x16(coeffs, dst, stride, type); break;
    case TxSize::TX_8X32: invTxfm2dAdd8x32(coeffs, dst, stride, type); break;
    case TxSize::TX_32X8: invTxfm2dAdd32x8(coeffs, dst, stride, type); break;
    default: assert(0); break;
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
    // RE7 (court order): the per-pass tx-type selection. The forward runs
    // columns first then rows; ADST_DCT = column ADST + row DCT, DCT_ADST =
    // the mirror, ADST_ADST = both (inv_transforms.h:45-80 per-pass tabs;
    // the svtd_is_adst_col/row pair). Types 1/3 take fadst in the column
    // pass, 2/3 in the row pass; DCT_DCT (0) takes fdct in both, and
    // ADST_ADST keeps the old else-branch behavior exactly (the l3 goldens).
    const int colAdst = (*txType == 1) || (*txType == 3);
    const int rowAdst = (*txType == 2) || (*txType == 3);
    int tmp[4];
    int o[4];
    for (int r = 0; r < 4; ++r) {
        tmp[r] = input[r * (*stride) + t] * 4;
    }
    if (colAdst) {
        d_fadst4(tmp, o);
    } else {
        d_fdct4(tmp, o);
    }
    for (int r = 0; r < 4; ++r) {
        sbuf[r * 4 + t] = o[r];
    }
    __syncthreads();
    for (int c = 0; c < 4; ++c) {
        tmp[c] = sbuf[t * 4 + c];
    }
    if (rowAdst) {
        d_fadst4(tmp, o);
    } else {
        d_fdct4(tmp, o);
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
    // RE7 (court order): the per-pass selection, the fwd 4x4 note.
    const int colAdst = (*txType == 1) || (*txType == 3);
    const int rowAdst = (*txType == 2) || (*txType == 3);
    int tmp[8];
    int o[8];
    for (int r = 0; r < 8; ++r) {
        tmp[r] = input[r * (*stride) + t] * 4;
    }
    if (colAdst) {
        d_fadst8(tmp, o);
    } else {
        d_fdct8(tmp, o);
    }
    for (int r = 0; r < 8; ++r) {
        sbuf[r * 8 + t] = d_round_shift(o[r], 1);
    }
    __syncthreads();
    for (int c = 0; c < 8; ++c) {
        tmp[c] = sbuf[t * 8 + c];
    }
    if (rowAdst) {
        d_fadst8(tmp, o);
    } else {
        d_fdct8(tmp, o);
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
    // RE7 (court order): the per-pass selection, the fwd 4x4 note.
    const int colAdst = (*txType == 1) || (*txType == 3);
    const int rowAdst = (*txType == 2) || (*txType == 3);
    int tmp[16];
    int o[16];
    for (int r = 0; r < 16; ++r) {
        tmp[r] = input[r * (*stride) + t] * 4;
    }
    if (colAdst) {
        d_fadst16(tmp, o, 13);
    } else {
        d_fdct16(tmp, o, 13);
    }
    for (int r = 0; r < 16; ++r) {
        sbuf[r * 16 + t] = d_round_shift(o[r], 2);
    }
    __syncthreads();
    for (int c = 0; c < 16; ++c) {
        tmp[c] = sbuf[t * 16 + c];
    }
    if (rowAdst) {
        d_fadst16(tmp, o, 12);
    } else {
        d_fdct16(tmp, o, 12);
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
    s += R"CUDB5(
__constant__ int kC10f[64] = {
    1024, 1024, 1023, 1021, 1019, 1016, 1013, 1009, 1004, 999, 993, 987, 980, 972, 964, 955,
    946, 936, 926, 915, 903, 891, 878, 865, 851, 837, 822, 807, 792, 775, 759, 742,
    724, 706, 688, 669, 650, 630, 610, 590, 569, 548, 526, 505, 483, 460, 438, 415,
    392, 369, 345, 321, 297, 273, 249, 224, 200, 175, 150, 125, 100, 75, 50, 25};

__device__ void d_fdct64(const int* input, int* output, int bit) {
    const int* cospi = (bit == 13) ? kCospi : kC10f;
    int* bf0;
    int* bf1;
    int step[64];


    // stage 0;

    // stage 1;
    bf1     = output;
    bf1[0]  = input[0] + input[63];
    bf1[1]  = input[1] + input[62];
    bf1[2]  = input[2] + input[61];
    bf1[3]  = input[3] + input[60];
    bf1[4]  = input[4] + input[59];
    bf1[5]  = input[5] + input[58];
    bf1[6]  = input[6] + input[57];
    bf1[7]  = input[7] + input[56];
    bf1[8]  = input[8] + input[55];
    bf1[9]  = input[9] + input[54];
    bf1[10] = input[10] + input[53];
    bf1[11] = input[11] + input[52];
    bf1[12] = input[12] + input[51];
    bf1[13] = input[13] + input[50];
    bf1[14] = input[14] + input[49];
    bf1[15] = input[15] + input[48];
    bf1[16] = input[16] + input[47];
    bf1[17] = input[17] + input[46];
    bf1[18] = input[18] + input[45];
    bf1[19] = input[19] + input[44];
    bf1[20] = input[20] + input[43];
    bf1[21] = input[21] + input[42];
    bf1[22] = input[22] + input[41];
    bf1[23] = input[23] + input[40];
    bf1[24] = input[24] + input[39];
    bf1[25] = input[25] + input[38];
    bf1[26] = input[26] + input[37];
    bf1[27] = input[27] + input[36];
    bf1[28] = input[28] + input[35];
    bf1[29] = input[29] + input[34];
    bf1[30] = input[30] + input[33];
    bf1[31] = input[31] + input[32];
    bf1[32] = -input[32] + input[31];
    bf1[33] = -input[33] + input[30];
    bf1[34] = -input[34] + input[29];
    bf1[35] = -input[35] + input[28];
    bf1[36] = -input[36] + input[27];
    bf1[37] = -input[37] + input[26];
    bf1[38] = -input[38] + input[25];
    bf1[39] = -input[39] + input[24];
    bf1[40] = -input[40] + input[23];
    bf1[41] = -input[41] + input[22];
    bf1[42] = -input[42] + input[21];
    bf1[43] = -input[43] + input[20];
    bf1[44] = -input[44] + input[19];
    bf1[45] = -input[45] + input[18];
    bf1[46] = -input[46] + input[17];
    bf1[47] = -input[47] + input[16];
    bf1[48] = -input[48] + input[15];
    bf1[49] = -input[49] + input[14];
    bf1[50] = -input[50] + input[13];
    bf1[51] = -input[51] + input[12];
    bf1[52] = -input[52] + input[11];
    bf1[53] = -input[53] + input[10];
    bf1[54] = -input[54] + input[9];
    bf1[55] = -input[55] + input[8];
    bf1[56] = -input[56] + input[7];
    bf1[57] = -input[57] + input[6];
    bf1[58] = -input[58] + input[5];
    bf1[59] = -input[59] + input[4];
    bf1[60] = -input[60] + input[3];
    bf1[61] = -input[61] + input[2];
    bf1[62] = -input[62] + input[1];
    bf1[63] = -input[63] + input[0];

    // stage 2
        bf0     = output;
    bf1     = step;
    bf1[0]  = bf0[0] + bf0[31];
    bf1[1]  = bf0[1] + bf0[30];
    bf1[2]  = bf0[2] + bf0[29];
    bf1[3]  = bf0[3] + bf0[28];
    bf1[4]  = bf0[4] + bf0[27];
    bf1[5]  = bf0[5] + bf0[26];
    bf1[6]  = bf0[6] + bf0[25];
    bf1[7]  = bf0[7] + bf0[24];
    bf1[8]  = bf0[8] + bf0[23];
    bf1[9]  = bf0[9] + bf0[22];
    bf1[10] = bf0[10] + bf0[21];
    bf1[11] = bf0[11] + bf0[20];
    bf1[12] = bf0[12] + bf0[19];
    bf1[13] = bf0[13] + bf0[18];
    bf1[14] = bf0[14] + bf0[17];
    bf1[15] = bf0[15] + bf0[16];
    bf1[16] = -bf0[16] + bf0[15];
    bf1[17] = -bf0[17] + bf0[14];
    bf1[18] = -bf0[18] + bf0[13];
    bf1[19] = -bf0[19] + bf0[12];
    bf1[20] = -bf0[20] + bf0[11];
    bf1[21] = -bf0[21] + bf0[10];
    bf1[22] = -bf0[22] + bf0[9];
    bf1[23] = -bf0[23] + bf0[8];
    bf1[24] = -bf0[24] + bf0[7];
    bf1[25] = -bf0[25] + bf0[6];
    bf1[26] = -bf0[26] + bf0[5];
    bf1[27] = -bf0[27] + bf0[4];
    bf1[28] = -bf0[28] + bf0[3];
    bf1[29] = -bf0[29] + bf0[2];
    bf1[30] = -bf0[30] + bf0[1];
    bf1[31] = -bf0[31] + bf0[0];
    bf1[32] = bf0[32];
    bf1[33] = bf0[33];
    bf1[34] = bf0[34];
    bf1[35] = bf0[35];
    bf1[36] = bf0[36];
    bf1[37] = bf0[37];
    bf1[38] = bf0[38];
    bf1[39] = bf0[39];
    bf1[40] = d_half_btf(-cospi[32], bf0[40], cospi[32], bf0[55], bit);
    bf1[41] = d_half_btf(-cospi[32], bf0[41], cospi[32], bf0[54], bit);
    bf1[42] = d_half_btf(-cospi[32], bf0[42], cospi[32], bf0[53], bit);
    bf1[43] = d_half_btf(-cospi[32], bf0[43], cospi[32], bf0[52], bit);
    bf1[44] = d_half_btf(-cospi[32], bf0[44], cospi[32], bf0[51], bit);
    bf1[45] = d_half_btf(-cospi[32], bf0[45], cospi[32], bf0[50], bit);
    bf1[46] = d_half_btf(-cospi[32], bf0[46], cospi[32], bf0[49], bit);
    bf1[47] = d_half_btf(-cospi[32], bf0[47], cospi[32], bf0[48], bit);
    bf1[48] = d_half_btf(cospi[32], bf0[48], cospi[32], bf0[47], bit);
    bf1[49] = d_half_btf(cospi[32], bf0[49], cospi[32], bf0[46], bit);
    bf1[50] = d_half_btf(cospi[32], bf0[50], cospi[32], bf0[45], bit);
    bf1[51] = d_half_btf(cospi[32], bf0[51], cospi[32], bf0[44], bit);
    bf1[52] = d_half_btf(cospi[32], bf0[52], cospi[32], bf0[43], bit);
    bf1[53] = d_half_btf(cospi[32], bf0[53], cospi[32], bf0[42], bit);
    bf1[54] = d_half_btf(cospi[32], bf0[54], cospi[32], bf0[41], bit);
    bf1[55] = d_half_btf(cospi[32], bf0[55], cospi[32], bf0[40], bit);
    bf1[56] = bf0[56];
    bf1[57] = bf0[57];
    bf1[58] = bf0[58];
    bf1[59] = bf0[59];
    bf1[60] = bf0[60];
    bf1[61] = bf0[61];
    bf1[62] = bf0[62];
    bf1[63] = bf0[63];

    // stage 3
        bf0     = step;
    bf1     = output;
    bf1[0]  = bf0[0] + bf0[15];
    bf1[1]  = bf0[1] + bf0[14];
    bf1[2]  = bf0[2] + bf0[13];
    bf1[3]  = bf0[3] + bf0[12];
    bf1[4]  = bf0[4] + bf0[11];
    bf1[5]  = bf0[5] + bf0[10];
    bf1[6]  = bf0[6] + bf0[9];
    bf1[7]  = bf0[7] + bf0[8];
    bf1[8]  = -bf0[8] + bf0[7];
    bf1[9]  = -bf0[9] + bf0[6];
    bf1[10] = -bf0[10] + bf0[5];
    bf1[11] = -bf0[11] + bf0[4];
    bf1[12] = -bf0[12] + bf0[3];
    bf1[13] = -bf0[13] + bf0[2];
    bf1[14] = -bf0[14] + bf0[1];
    bf1[15] = -bf0[15] + bf0[0];
    bf1[16] = bf0[16];
    bf1[17] = bf0[17];
    bf1[18] = bf0[18];
    bf1[19] = bf0[19];
    bf1[20] = d_half_btf(-cospi[32], bf0[20], cospi[32], bf0[27], bit);
    bf1[21] = d_half_btf(-cospi[32], bf0[21], cospi[32], bf0[26], bit);
    bf1[22] = d_half_btf(-cospi[32], bf0[22], cospi[32], bf0[25], bit);
    bf1[23] = d_half_btf(-cospi[32], bf0[23], cospi[32], bf0[24], bit);
    bf1[24] = d_half_btf(cospi[32], bf0[24], cospi[32], bf0[23], bit);
    bf1[25] = d_half_btf(cospi[32], bf0[25], cospi[32], bf0[22], bit);
    bf1[26] = d_half_btf(cospi[32], bf0[26], cospi[32], bf0[21], bit);
    bf1[27] = d_half_btf(cospi[32], bf0[27], cospi[32], bf0[20], bit);
    bf1[28] = bf0[28];
    bf1[29] = bf0[29];
    bf1[30] = bf0[30];
    bf1[31] = bf0[31];
    bf1[32] = bf0[32] + bf0[47];
    bf1[33] = bf0[33] + bf0[46];
    bf1[34] = bf0[34] + bf0[45];
    bf1[35] = bf0[35] + bf0[44];
    bf1[36] = bf0[36] + bf0[43];
    bf1[37] = bf0[37] + bf0[42];
    bf1[38] = bf0[38] + bf0[41];
    bf1[39] = bf0[39] + bf0[40];
    bf1[40] = -bf0[40] + bf0[39];
    bf1[41] = -bf0[41] + bf0[38];
    bf1[42] = -bf0[42] + bf0[37];
    bf1[43] = -bf0[43] + bf0[36];
    bf1[44] = -bf0[44] + bf0[35];
    bf1[45] = -bf0[45] + bf0[34];
    bf1[46] = -bf0[46] + bf0[33];
    bf1[47] = -bf0[47] + bf0[32];
    bf1[48] = -bf0[48] + bf0[63];
    bf1[49] = -bf0[49] + bf0[62];
    bf1[50] = -bf0[50] + bf0[61];
    bf1[51] = -bf0[51] + bf0[60];
    bf1[52] = -bf0[52] + bf0[59];
    bf1[53] = -bf0[53] + bf0[58];
    bf1[54] = -bf0[54] + bf0[57];
    bf1[55] = -bf0[55] + bf0[56];
    bf1[56] = bf0[56] + bf0[55];
    bf1[57] = bf0[57] + bf0[54];
    bf1[58] = bf0[58] + bf0[53];
    bf1[59] = bf0[59] + bf0[52];
    bf1[60] = bf0[60] + bf0[51];
    bf1[61] = bf0[61] + bf0[50];
    bf1[62] = bf0[62] + bf0[49];
    bf1[63] = bf0[63] + bf0[48];

    // stage 4
        bf0     = output;
    bf1     = step;
    bf1[0]  = bf0[0] + bf0[7];
    bf1[1]  = bf0[1] + bf0[6];
    bf1[2]  = bf0[2] + bf0[5];
    bf1[3]  = bf0[3] + bf0[4];
    bf1[4]  = -bf0[4] + bf0[3];
    bf1[5]  = -bf0[5] + bf0[2];
    bf1[6]  = -bf0[6] + bf0[1];
    bf1[7]  = -bf0[7] + bf0[0];
    bf1[8]  = bf0[8];
    bf1[9]  = bf0[9];
    bf1[10] = d_half_btf(-cospi[32], bf0[10], cospi[32], bf0[13], bit);
    bf1[11] = d_half_btf(-cospi[32], bf0[11], cospi[32], bf0[12], bit);
    bf1[12] = d_half_btf(cospi[32], bf0[12], cospi[32], bf0[11], bit);
    bf1[13] = d_half_btf(cospi[32], bf0[13], cospi[32], bf0[10], bit);
    bf1[14] = bf0[14];
    bf1[15] = bf0[15];
    bf1[16] = bf0[16] + bf0[23];
    bf1[17] = bf0[17] + bf0[22];
    bf1[18] = bf0[18] + bf0[21];
    bf1[19] = bf0[19] + bf0[20];
    bf1[20] = -bf0[20] + bf0[19];
    bf1[21] = -bf0[21] + bf0[18];
    bf1[22] = -bf0[22] + bf0[17];
    bf1[23] = -bf0[23] + bf0[16];
    bf1[24] = -bf0[24] + bf0[31];
    bf1[25] = -bf0[25] + bf0[30];
    bf1[26] = -bf0[26] + bf0[29];
    bf1[27] = -bf0[27] + bf0[28];
    bf1[28] = bf0[28] + bf0[27];
    bf1[29] = bf0[29] + bf0[26];
    bf1[30] = bf0[30] + bf0[25];
    bf1[31] = bf0[31] + bf0[24];
    bf1[32] = bf0[32];
    bf1[33] = bf0[33];
    bf1[34] = bf0[34];
    bf1[35] = bf0[35];
    bf1[36] = d_half_btf(-cospi[16], bf0[36], cospi[48], bf0[59], bit);
    bf1[37] = d_half_btf(-cospi[16], bf0[37], cospi[48], bf0[58], bit);
    bf1[38] = d_half_btf(-cospi[16], bf0[38], cospi[48], bf0[57], bit);
    bf1[39] = d_half_btf(-cospi[16], bf0[39], cospi[48], bf0[56], bit);
    bf1[40] = d_half_btf(-cospi[48], bf0[40], -cospi[16], bf0[55], bit);
    bf1[41] = d_half_btf(-cospi[48], bf0[41], -cospi[16], bf0[54], bit);
    bf1[42] = d_half_btf(-cospi[48], bf0[42], -cospi[16], bf0[53], bit);
    bf1[43] = d_half_btf(-cospi[48], bf0[43], -cospi[16], bf0[52], bit);
    bf1[44] = bf0[44];
    bf1[45] = bf0[45];
    bf1[46] = bf0[46];
    bf1[47] = bf0[47];
    bf1[48] = bf0[48];
    bf1[49] = bf0[49];
    bf1[50] = bf0[50];
    bf1[51] = bf0[51];
    bf1[52] = d_half_btf(cospi[48], bf0[52], -cospi[16], bf0[43], bit);
    bf1[53] = d_half_btf(cospi[48], bf0[53], -cospi[16], bf0[42], bit);
    bf1[54] = d_half_btf(cospi[48], bf0[54], -cospi[16], bf0[41], bit);
    bf1[55] = d_half_btf(cospi[48], bf0[55], -cospi[16], bf0[40], bit);
    bf1[56] = d_half_btf(cospi[16], bf0[56], cospi[48], bf0[39], bit);
    bf1[57] = d_half_btf(cospi[16], bf0[57], cospi[48], bf0[38], bit);
    bf1[58] = d_half_btf(cospi[16], bf0[58], cospi[48], bf0[37], bit);
    bf1[59] = d_half_btf(cospi[16], bf0[59], cospi[48], bf0[36], bit);
    bf1[60] = bf0[60];
    bf1[61] = bf0[61];
    bf1[62] = bf0[62];
    bf1[63] = bf0[63];
)CUDB5";
    s += R"CUDB5(
    // stage 5
        bf0     = step;
    bf1     = output;
    bf1[0]  = bf0[0] + bf0[3];
    bf1[1]  = bf0[1] + bf0[2];
    bf1[2]  = -bf0[2] + bf0[1];
    bf1[3]  = -bf0[3] + bf0[0];
    bf1[4]  = bf0[4];
    bf1[5]  = d_half_btf(-cospi[32], bf0[5], cospi[32], bf0[6], bit);
    bf1[6]  = d_half_btf(cospi[32], bf0[6], cospi[32], bf0[5], bit);
    bf1[7]  = bf0[7];
    bf1[8]  = bf0[8] + bf0[11];
    bf1[9]  = bf0[9] + bf0[10];
    bf1[10] = -bf0[10] + bf0[9];
    bf1[11] = -bf0[11] + bf0[8];
    bf1[12] = -bf0[12] + bf0[15];
    bf1[13] = -bf0[13] + bf0[14];
    bf1[14] = bf0[14] + bf0[13];
    bf1[15] = bf0[15] + bf0[12];
    bf1[16] = bf0[16];
    bf1[17] = bf0[17];
    bf1[18] = d_half_btf(-cospi[16], bf0[18], cospi[48], bf0[29], bit);
    bf1[19] = d_half_btf(-cospi[16], bf0[19], cospi[48], bf0[28], bit);
    bf1[20] = d_half_btf(-cospi[48], bf0[20], -cospi[16], bf0[27], bit);
    bf1[21] = d_half_btf(-cospi[48], bf0[21], -cospi[16], bf0[26], bit);
    bf1[22] = bf0[22];
    bf1[23] = bf0[23];
    bf1[24] = bf0[24];
    bf1[25] = bf0[25];
    bf1[26] = d_half_btf(cospi[48], bf0[26], -cospi[16], bf0[21], bit);
    bf1[27] = d_half_btf(cospi[48], bf0[27], -cospi[16], bf0[20], bit);
    bf1[28] = d_half_btf(cospi[16], bf0[28], cospi[48], bf0[19], bit);
    bf1[29] = d_half_btf(cospi[16], bf0[29], cospi[48], bf0[18], bit);
    bf1[30] = bf0[30];
    bf1[31] = bf0[31];
    bf1[32] = bf0[32] + bf0[39];
    bf1[33] = bf0[33] + bf0[38];
    bf1[34] = bf0[34] + bf0[37];
    bf1[35] = bf0[35] + bf0[36];
    bf1[36] = -bf0[36] + bf0[35];
    bf1[37] = -bf0[37] + bf0[34];
    bf1[38] = -bf0[38] + bf0[33];
    bf1[39] = -bf0[39] + bf0[32];
    bf1[40] = -bf0[40] + bf0[47];
    bf1[41] = -bf0[41] + bf0[46];
    bf1[42] = -bf0[42] + bf0[45];
    bf1[43] = -bf0[43] + bf0[44];
    bf1[44] = bf0[44] + bf0[43];
    bf1[45] = bf0[45] + bf0[42];
    bf1[46] = bf0[46] + bf0[41];
    bf1[47] = bf0[47] + bf0[40];
    bf1[48] = bf0[48] + bf0[55];
    bf1[49] = bf0[49] + bf0[54];
    bf1[50] = bf0[50] + bf0[53];
    bf1[51] = bf0[51] + bf0[52];
    bf1[52] = -bf0[52] + bf0[51];
    bf1[53] = -bf0[53] + bf0[50];
    bf1[54] = -bf0[54] + bf0[49];
    bf1[55] = -bf0[55] + bf0[48];
    bf1[56] = -bf0[56] + bf0[63];
    bf1[57] = -bf0[57] + bf0[62];
    bf1[58] = -bf0[58] + bf0[61];
    bf1[59] = -bf0[59] + bf0[60];
    bf1[60] = bf0[60] + bf0[59];
    bf1[61] = bf0[61] + bf0[58];
    bf1[62] = bf0[62] + bf0[57];
    bf1[63] = bf0[63] + bf0[56];

    // stage 6
        bf0     = output;
    bf1     = step;
    bf1[0]  = d_half_btf(cospi[32], bf0[0], cospi[32], bf0[1], bit);
    bf1[1]  = d_half_btf(-cospi[32], bf0[1], cospi[32], bf0[0], bit);
    bf1[2]  = d_half_btf(cospi[48], bf0[2], cospi[16], bf0[3], bit);
    bf1[3]  = d_half_btf(cospi[48], bf0[3], -cospi[16], bf0[2], bit);
    bf1[4]  = bf0[4] + bf0[5];
    bf1[5]  = -bf0[5] + bf0[4];
    bf1[6]  = -bf0[6] + bf0[7];
    bf1[7]  = bf0[7] + bf0[6];
    bf1[8]  = bf0[8];
    bf1[9]  = d_half_btf(-cospi[16], bf0[9], cospi[48], bf0[14], bit);
    bf1[10] = d_half_btf(-cospi[48], bf0[10], -cospi[16], bf0[13], bit);
    bf1[11] = bf0[11];
    bf1[12] = bf0[12];
    bf1[13] = d_half_btf(cospi[48], bf0[13], -cospi[16], bf0[10], bit);
    bf1[14] = d_half_btf(cospi[16], bf0[14], cospi[48], bf0[9], bit);
    bf1[15] = bf0[15];
    bf1[16] = bf0[16] + bf0[19];
    bf1[17] = bf0[17] + bf0[18];
    bf1[18] = -bf0[18] + bf0[17];
    bf1[19] = -bf0[19] + bf0[16];
    bf1[20] = -bf0[20] + bf0[23];
    bf1[21] = -bf0[21] + bf0[22];
    bf1[22] = bf0[22] + bf0[21];
    bf1[23] = bf0[23] + bf0[20];
    bf1[24] = bf0[24] + bf0[27];
    bf1[25] = bf0[25] + bf0[26];
    bf1[26] = -bf0[26] + bf0[25];
    bf1[27] = -bf0[27] + bf0[24];
    bf1[28] = -bf0[28] + bf0[31];
    bf1[29] = -bf0[29] + bf0[30];
    bf1[30] = bf0[30] + bf0[29];
    bf1[31] = bf0[31] + bf0[28];
    bf1[32] = bf0[32];
    bf1[33] = bf0[33];
    bf1[34] = d_half_btf(-cospi[8], bf0[34], cospi[56], bf0[61], bit);
    bf1[35] = d_half_btf(-cospi[8], bf0[35], cospi[56], bf0[60], bit);
    bf1[36] = d_half_btf(-cospi[56], bf0[36], -cospi[8], bf0[59], bit);
    bf1[37] = d_half_btf(-cospi[56], bf0[37], -cospi[8], bf0[58], bit);
    bf1[38] = bf0[38];
    bf1[39] = bf0[39];
    bf1[40] = bf0[40];
    bf1[41] = bf0[41];
    bf1[42] = d_half_btf(-cospi[40], bf0[42], cospi[24], bf0[53], bit);
    bf1[43] = d_half_btf(-cospi[40], bf0[43], cospi[24], bf0[52], bit);
    bf1[44] = d_half_btf(-cospi[24], bf0[44], -cospi[40], bf0[51], bit);
    bf1[45] = d_half_btf(-cospi[24], bf0[45], -cospi[40], bf0[50], bit);
    bf1[46] = bf0[46];
    bf1[47] = bf0[47];
    bf1[48] = bf0[48];
    bf1[49] = bf0[49];
    bf1[50] = d_half_btf(cospi[24], bf0[50], -cospi[40], bf0[45], bit);
    bf1[51] = d_half_btf(cospi[24], bf0[51], -cospi[40], bf0[44], bit);
    bf1[52] = d_half_btf(cospi[40], bf0[52], cospi[24], bf0[43], bit);
    bf1[53] = d_half_btf(cospi[40], bf0[53], cospi[24], bf0[42], bit);
    bf1[54] = bf0[54];
    bf1[55] = bf0[55];
    bf1[56] = bf0[56];
    bf1[57] = bf0[57];
    bf1[58] = d_half_btf(cospi[56], bf0[58], -cospi[8], bf0[37], bit);
    bf1[59] = d_half_btf(cospi[56], bf0[59], -cospi[8], bf0[36], bit);
    bf1[60] = d_half_btf(cospi[8], bf0[60], cospi[56], bf0[35], bit);
    bf1[61] = d_half_btf(cospi[8], bf0[61], cospi[56], bf0[34], bit);
    bf1[62] = bf0[62];
    bf1[63] = bf0[63];

    // stage 7
        bf0     = step;
    bf1     = output;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = d_half_btf(cospi[56], bf0[4], cospi[8], bf0[7], bit);
    bf1[5]  = d_half_btf(cospi[24], bf0[5], cospi[40], bf0[6], bit);
    bf1[6]  = d_half_btf(cospi[24], bf0[6], -cospi[40], bf0[5], bit);
    bf1[7]  = d_half_btf(cospi[56], bf0[7], -cospi[8], bf0[4], bit);
    bf1[8]  = bf0[8] + bf0[9];
    bf1[9]  = -bf0[9] + bf0[8];
    bf1[10] = -bf0[10] + bf0[11];
    bf1[11] = bf0[11] + bf0[10];
    bf1[12] = bf0[12] + bf0[13];
    bf1[13] = -bf0[13] + bf0[12];
    bf1[14] = -bf0[14] + bf0[15];
    bf1[15] = bf0[15] + bf0[14];
    bf1[16] = bf0[16];
    bf1[17] = d_half_btf(-cospi[8], bf0[17], cospi[56], bf0[30], bit);
    bf1[18] = d_half_btf(-cospi[56], bf0[18], -cospi[8], bf0[29], bit);
    bf1[19] = bf0[19];
    bf1[20] = bf0[20];
    bf1[21] = d_half_btf(-cospi[40], bf0[21], cospi[24], bf0[26], bit);
    bf1[22] = d_half_btf(-cospi[24], bf0[22], -cospi[40], bf0[25], bit);
    bf1[23] = bf0[23];
    bf1[24] = bf0[24];
    bf1[25] = d_half_btf(cospi[24], bf0[25], -cospi[40], bf0[22], bit);
    bf1[26] = d_half_btf(cospi[40], bf0[26], cospi[24], bf0[21], bit);
    bf1[27] = bf0[27];
    bf1[28] = bf0[28];
    bf1[29] = d_half_btf(cospi[56], bf0[29], -cospi[8], bf0[18], bit);
    bf1[30] = d_half_btf(cospi[8], bf0[30], cospi[56], bf0[17], bit);
    bf1[31] = bf0[31];
    bf1[32] = bf0[32] + bf0[35];
    bf1[33] = bf0[33] + bf0[34];
    bf1[34] = -bf0[34] + bf0[33];
    bf1[35] = -bf0[35] + bf0[32];
    bf1[36] = -bf0[36] + bf0[39];
    bf1[37] = -bf0[37] + bf0[38];
    bf1[38] = bf0[38] + bf0[37];
    bf1[39] = bf0[39] + bf0[36];
    bf1[40] = bf0[40] + bf0[43];
    bf1[41] = bf0[41] + bf0[42];
    bf1[42] = -bf0[42] + bf0[41];
    bf1[43] = -bf0[43] + bf0[40];
    bf1[44] = -bf0[44] + bf0[47];
    bf1[45] = -bf0[45] + bf0[46];
    bf1[46] = bf0[46] + bf0[45];
    bf1[47] = bf0[47] + bf0[44];
    bf1[48] = bf0[48] + bf0[51];
    bf1[49] = bf0[49] + bf0[50];
    bf1[50] = -bf0[50] + bf0[49];
    bf1[51] = -bf0[51] + bf0[48];
    bf1[52] = -bf0[52] + bf0[55];
    bf1[53] = -bf0[53] + bf0[54];
    bf1[54] = bf0[54] + bf0[53];
    bf1[55] = bf0[55] + bf0[52];
    bf1[56] = bf0[56] + bf0[59];
    bf1[57] = bf0[57] + bf0[58];
    bf1[58] = -bf0[58] + bf0[57];
    bf1[59] = -bf0[59] + bf0[56];
    bf1[60] = -bf0[60] + bf0[63];
    bf1[61] = -bf0[61] + bf0[62];
    bf1[62] = bf0[62] + bf0[61];
    bf1[63] = bf0[63] + bf0[60];
)CUDB5";
    s += R"CUDB5(
    // stage 8
        bf0     = output;
    bf1     = step;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = bf0[4];
    bf1[5]  = bf0[5];
    bf1[6]  = bf0[6];
    bf1[7]  = bf0[7];
    bf1[8]  = d_half_btf(cospi[60], bf0[8], cospi[4], bf0[15], bit);
    bf1[9]  = d_half_btf(cospi[28], bf0[9], cospi[36], bf0[14], bit);
    bf1[10] = d_half_btf(cospi[44], bf0[10], cospi[20], bf0[13], bit);
    bf1[11] = d_half_btf(cospi[12], bf0[11], cospi[52], bf0[12], bit);
    bf1[12] = d_half_btf(cospi[12], bf0[12], -cospi[52], bf0[11], bit);
    bf1[13] = d_half_btf(cospi[44], bf0[13], -cospi[20], bf0[10], bit);
    bf1[14] = d_half_btf(cospi[28], bf0[14], -cospi[36], bf0[9], bit);
    bf1[15] = d_half_btf(cospi[60], bf0[15], -cospi[4], bf0[8], bit);
    bf1[16] = bf0[16] + bf0[17];
    bf1[17] = -bf0[17] + bf0[16];
    bf1[18] = -bf0[18] + bf0[19];
    bf1[19] = bf0[19] + bf0[18];
    bf1[20] = bf0[20] + bf0[21];
    bf1[21] = -bf0[21] + bf0[20];
    bf1[22] = -bf0[22] + bf0[23];
    bf1[23] = bf0[23] + bf0[22];
    bf1[24] = bf0[24] + bf0[25];
    bf1[25] = -bf0[25] + bf0[24];
    bf1[26] = -bf0[26] + bf0[27];
    bf1[27] = bf0[27] + bf0[26];
    bf1[28] = bf0[28] + bf0[29];
    bf1[29] = -bf0[29] + bf0[28];
    bf1[30] = -bf0[30] + bf0[31];
    bf1[31] = bf0[31] + bf0[30];
    bf1[32] = bf0[32];
    bf1[33] = d_half_btf(-cospi[4], bf0[33], cospi[60], bf0[62], bit);
    bf1[34] = d_half_btf(-cospi[60], bf0[34], -cospi[4], bf0[61], bit);
    bf1[35] = bf0[35];
    bf1[36] = bf0[36];
    bf1[37] = d_half_btf(-cospi[36], bf0[37], cospi[28], bf0[58], bit);
    bf1[38] = d_half_btf(-cospi[28], bf0[38], -cospi[36], bf0[57], bit);
    bf1[39] = bf0[39];
    bf1[40] = bf0[40];
    bf1[41] = d_half_btf(-cospi[20], bf0[41], cospi[44], bf0[54], bit);
    bf1[42] = d_half_btf(-cospi[44], bf0[42], -cospi[20], bf0[53], bit);
    bf1[43] = bf0[43];
    bf1[44] = bf0[44];
    bf1[45] = d_half_btf(-cospi[52], bf0[45], cospi[12], bf0[50], bit);
    bf1[46] = d_half_btf(-cospi[12], bf0[46], -cospi[52], bf0[49], bit);
    bf1[47] = bf0[47];
    bf1[48] = bf0[48];
    bf1[49] = d_half_btf(cospi[12], bf0[49], -cospi[52], bf0[46], bit);
    bf1[50] = d_half_btf(cospi[52], bf0[50], cospi[12], bf0[45], bit);
    bf1[51] = bf0[51];
    bf1[52] = bf0[52];
    bf1[53] = d_half_btf(cospi[44], bf0[53], -cospi[20], bf0[42], bit);
    bf1[54] = d_half_btf(cospi[20], bf0[54], cospi[44], bf0[41], bit);
    bf1[55] = bf0[55];
    bf1[56] = bf0[56];
    bf1[57] = d_half_btf(cospi[28], bf0[57], -cospi[36], bf0[38], bit);
    bf1[58] = d_half_btf(cospi[36], bf0[58], cospi[28], bf0[37], bit);
    bf1[59] = bf0[59];
    bf1[60] = bf0[60];
    bf1[61] = d_half_btf(cospi[60], bf0[61], -cospi[4], bf0[34], bit);
    bf1[62] = d_half_btf(cospi[4], bf0[62], cospi[60], bf0[33], bit);
    bf1[63] = bf0[63];

    // stage 9
        bf0     = step;
    bf1     = output;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = bf0[4];
    bf1[5]  = bf0[5];
    bf1[6]  = bf0[6];
    bf1[7]  = bf0[7];
    bf1[8]  = bf0[8];
    bf1[9]  = bf0[9];
    bf1[10] = bf0[10];
    bf1[11] = bf0[11];
    bf1[12] = bf0[12];
    bf1[13] = bf0[13];
    bf1[14] = bf0[14];
    bf1[15] = bf0[15];
    bf1[16] = d_half_btf(cospi[62], bf0[16], cospi[2], bf0[31], bit);
    bf1[17] = d_half_btf(cospi[30], bf0[17], cospi[34], bf0[30], bit);
    bf1[18] = d_half_btf(cospi[46], bf0[18], cospi[18], bf0[29], bit);
    bf1[19] = d_half_btf(cospi[14], bf0[19], cospi[50], bf0[28], bit);
    bf1[20] = d_half_btf(cospi[54], bf0[20], cospi[10], bf0[27], bit);
    bf1[21] = d_half_btf(cospi[22], bf0[21], cospi[42], bf0[26], bit);
    bf1[22] = d_half_btf(cospi[38], bf0[22], cospi[26], bf0[25], bit);
    bf1[23] = d_half_btf(cospi[6], bf0[23], cospi[58], bf0[24], bit);
    bf1[24] = d_half_btf(cospi[6], bf0[24], -cospi[58], bf0[23], bit);
    bf1[25] = d_half_btf(cospi[38], bf0[25], -cospi[26], bf0[22], bit);
    bf1[26] = d_half_btf(cospi[22], bf0[26], -cospi[42], bf0[21], bit);
    bf1[27] = d_half_btf(cospi[54], bf0[27], -cospi[10], bf0[20], bit);
    bf1[28] = d_half_btf(cospi[14], bf0[28], -cospi[50], bf0[19], bit);
    bf1[29] = d_half_btf(cospi[46], bf0[29], -cospi[18], bf0[18], bit);
    bf1[30] = d_half_btf(cospi[30], bf0[30], -cospi[34], bf0[17], bit);
    bf1[31] = d_half_btf(cospi[62], bf0[31], -cospi[2], bf0[16], bit);
    bf1[32] = bf0[32] + bf0[33];
    bf1[33] = -bf0[33] + bf0[32];
    bf1[34] = -bf0[34] + bf0[35];
    bf1[35] = bf0[35] + bf0[34];
    bf1[36] = bf0[36] + bf0[37];
    bf1[37] = -bf0[37] + bf0[36];
    bf1[38] = -bf0[38] + bf0[39];
    bf1[39] = bf0[39] + bf0[38];
    bf1[40] = bf0[40] + bf0[41];
    bf1[41] = -bf0[41] + bf0[40];
    bf1[42] = -bf0[42] + bf0[43];
    bf1[43] = bf0[43] + bf0[42];
    bf1[44] = bf0[44] + bf0[45];
    bf1[45] = -bf0[45] + bf0[44];
    bf1[46] = -bf0[46] + bf0[47];
    bf1[47] = bf0[47] + bf0[46];
    bf1[48] = bf0[48] + bf0[49];
    bf1[49] = -bf0[49] + bf0[48];
    bf1[50] = -bf0[50] + bf0[51];
    bf1[51] = bf0[51] + bf0[50];
    bf1[52] = bf0[52] + bf0[53];
    bf1[53] = -bf0[53] + bf0[52];
    bf1[54] = -bf0[54] + bf0[55];
    bf1[55] = bf0[55] + bf0[54];
    bf1[56] = bf0[56] + bf0[57];
    bf1[57] = -bf0[57] + bf0[56];
    bf1[58] = -bf0[58] + bf0[59];
    bf1[59] = bf0[59] + bf0[58];
    bf1[60] = bf0[60] + bf0[61];
    bf1[61] = -bf0[61] + bf0[60];
    bf1[62] = -bf0[62] + bf0[63];
    bf1[63] = bf0[63] + bf0[62];

    // stage 10
        bf0     = output;
    bf1     = step;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = bf0[4];
    bf1[5]  = bf0[5];
    bf1[6]  = bf0[6];
    bf1[7]  = bf0[7];
    bf1[8]  = bf0[8];
    bf1[9]  = bf0[9];
    bf1[10] = bf0[10];
    bf1[11] = bf0[11];
    bf1[12] = bf0[12];
    bf1[13] = bf0[13];
    bf1[14] = bf0[14];
    bf1[15] = bf0[15];
    bf1[16] = bf0[16];
    bf1[17] = bf0[17];
    bf1[18] = bf0[18];
    bf1[19] = bf0[19];
    bf1[20] = bf0[20];
    bf1[21] = bf0[21];
    bf1[22] = bf0[22];
    bf1[23] = bf0[23];
    bf1[24] = bf0[24];
    bf1[25] = bf0[25];
    bf1[26] = bf0[26];
    bf1[27] = bf0[27];
    bf1[28] = bf0[28];
    bf1[29] = bf0[29];
    bf1[30] = bf0[30];
    bf1[31] = bf0[31];
    bf1[32] = d_half_btf(cospi[63], bf0[32], cospi[1], bf0[63], bit);
    bf1[33] = d_half_btf(cospi[31], bf0[33], cospi[33], bf0[62], bit);
    bf1[34] = d_half_btf(cospi[47], bf0[34], cospi[17], bf0[61], bit);
    bf1[35] = d_half_btf(cospi[15], bf0[35], cospi[49], bf0[60], bit);
    bf1[36] = d_half_btf(cospi[55], bf0[36], cospi[9], bf0[59], bit);
    bf1[37] = d_half_btf(cospi[23], bf0[37], cospi[41], bf0[58], bit);
    bf1[38] = d_half_btf(cospi[39], bf0[38], cospi[25], bf0[57], bit);
    bf1[39] = d_half_btf(cospi[7], bf0[39], cospi[57], bf0[56], bit);
    bf1[40] = d_half_btf(cospi[59], bf0[40], cospi[5], bf0[55], bit);
    bf1[41] = d_half_btf(cospi[27], bf0[41], cospi[37], bf0[54], bit);
    bf1[42] = d_half_btf(cospi[43], bf0[42], cospi[21], bf0[53], bit);
    bf1[43] = d_half_btf(cospi[11], bf0[43], cospi[53], bf0[52], bit);
    bf1[44] = d_half_btf(cospi[51], bf0[44], cospi[13], bf0[51], bit);
    bf1[45] = d_half_btf(cospi[19], bf0[45], cospi[45], bf0[50], bit);
    bf1[46] = d_half_btf(cospi[35], bf0[46], cospi[29], bf0[49], bit);
    bf1[47] = d_half_btf(cospi[3], bf0[47], cospi[61], bf0[48], bit);
    bf1[48] = d_half_btf(cospi[3], bf0[48], -cospi[61], bf0[47], bit);
    bf1[49] = d_half_btf(cospi[35], bf0[49], -cospi[29], bf0[46], bit);
    bf1[50] = d_half_btf(cospi[19], bf0[50], -cospi[45], bf0[45], bit);
    bf1[51] = d_half_btf(cospi[51], bf0[51], -cospi[13], bf0[44], bit);
    bf1[52] = d_half_btf(cospi[11], bf0[52], -cospi[53], bf0[43], bit);
    bf1[53] = d_half_btf(cospi[43], bf0[53], -cospi[21], bf0[42], bit);
    bf1[54] = d_half_btf(cospi[27], bf0[54], -cospi[37], bf0[41], bit);
    bf1[55] = d_half_btf(cospi[59], bf0[55], -cospi[5], bf0[40], bit);
    bf1[56] = d_half_btf(cospi[7], bf0[56], -cospi[57], bf0[39], bit);
    bf1[57] = d_half_btf(cospi[39], bf0[57], -cospi[25], bf0[38], bit);
    bf1[58] = d_half_btf(cospi[23], bf0[58], -cospi[41], bf0[37], bit);
    bf1[59] = d_half_btf(cospi[55], bf0[59], -cospi[9], bf0[36], bit);
    bf1[60] = d_half_btf(cospi[15], bf0[60], -cospi[49], bf0[35], bit);
    bf1[61] = d_half_btf(cospi[47], bf0[61], -cospi[17], bf0[34], bit);
    bf1[62] = d_half_btf(cospi[31], bf0[62], -cospi[33], bf0[33], bit);
    bf1[63] = d_half_btf(cospi[63], bf0[63], -cospi[1], bf0[32], bit);
)CUDB5";
    s += R"CUDB5(
    // stage 11
    bf0     = step;
    bf1     = output;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[32];
    bf1[2]  = bf0[16];
    bf1[3]  = bf0[48];
    bf1[4]  = bf0[8];
    bf1[5]  = bf0[40];
    bf1[6]  = bf0[24];
    bf1[7]  = bf0[56];
    bf1[8]  = bf0[4];
    bf1[9]  = bf0[36];
    bf1[10] = bf0[20];
    bf1[11] = bf0[52];
    bf1[12] = bf0[12];
    bf1[13] = bf0[44];
    bf1[14] = bf0[28];
    bf1[15] = bf0[60];
    bf1[16] = bf0[2];
    bf1[17] = bf0[34];
    bf1[18] = bf0[18];
    bf1[19] = bf0[50];
    bf1[20] = bf0[10];
    bf1[21] = bf0[42];
    bf1[22] = bf0[26];
    bf1[23] = bf0[58];
    bf1[24] = bf0[6];
    bf1[25] = bf0[38];
    bf1[26] = bf0[22];
    bf1[27] = bf0[54];
    bf1[28] = bf0[14];
    bf1[29] = bf0[46];
    bf1[30] = bf0[30];
    bf1[31] = bf0[62];
    bf1[32] = bf0[1];
    bf1[33] = bf0[33];
    bf1[34] = bf0[17];
    bf1[35] = bf0[49];
    bf1[36] = bf0[9];
    bf1[37] = bf0[41];
    bf1[38] = bf0[25];
    bf1[39] = bf0[57];
    bf1[40] = bf0[5];
    bf1[41] = bf0[37];
    bf1[42] = bf0[21];
    bf1[43] = bf0[53];
    bf1[44] = bf0[13];
    bf1[45] = bf0[45];
    bf1[46] = bf0[29];
    bf1[47] = bf0[61];
    bf1[48] = bf0[3];
    bf1[49] = bf0[35];
    bf1[50] = bf0[19];
    bf1[51] = bf0[51];
    bf1[52] = bf0[11];
    bf1[53] = bf0[43];
    bf1[54] = bf0[27];
    bf1[55] = bf0[59];
    bf1[56] = bf0[7];
    bf1[57] = bf0[39];
    bf1[58] = bf0[23];
    bf1[59] = bf0[55];
    bf1[60] = bf0[15];
    bf1[61] = bf0[47];
    bf1[62] = bf0[31];
    bf1[63] = bf0[63];
}
)CUDB5";
    s += R"CUDB6(
// svt_av1_transform_two_d_core_c at TX_64X64 (L9): shift {0,-2,-2}
// (transforms.c:126), cos_bit col 13 / row 10 (fwd_cos_bit_col/row[4][4]);
// col pass NO pre-shift (shift[0] = 0), >>2 rounding post-col AND >>2
// post-row. DCT-ONLY at 64x64. 64 threads, thread t = column in the col
// pass then row in the row pass.
extern "C" __global__ void fwd_txfm_2d_64x64(const short* input, const int* stride, const int* txType,
                                             int* output) {
    __shared__ int sbuf[4096];
    const int t = threadIdx.x;
    int tmp[64];
    int o[64];
    for (int r = 0; r < 64; ++r) {
        tmp[r] = input[r * (*stride) + t];
    }
    // DCT-ONLY at TX_64X64 (av1_txfm_type_ls[4] = {DCT64, INVALID, INVALID,
    // IDENTITY64}); *txType always 0 here, the branch is kept for shape
    (void)txType;
    d_fdct64(tmp, o, 13);  // cos_bit col 13; shift[0] = 0 -> no pre-shift
    for (int r = 0; r < 64; ++r) {
        sbuf[r * 64 + t] = d_round_shift(o[r], 2);  // -shift[1] = 2 rounding
    }
    __syncthreads();
    for (int c = 0; c < 64; ++c) {
        tmp[c] = sbuf[t * 64 + c];
    }
    d_fdct64(tmp, o, 10);  // cos_bit row 10
    for (int c = 0; c < 64; ++c) {
        output[t * 64 + c] = d_round_shift(o[c], 2);  // -shift[2] = 2 rounding
    }
})CUDB6";
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

__device__ int d_round_shift(long long value, int bit) {
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
    output[0] = d_round_shift(y0, bit);
    output[1] = d_round_shift(y1, bit);
    output[2] = d_round_shift(y2, bit);
    output[3] = d_round_shift(y3, bit);
}

extern "C" __global__ void inv_txfm_2d_add_4x4(const int* coeffs, const int* txType, unsigned char* dst,
                                               const int* stride) {
    __shared__ int sbuf[16];
    const int t = threadIdx.x;
    // RE7 (court order): the per-pass selection. The inverse runs ROWS first
    // then columns (the landed inv order; the svtd_inv2daddN_t (row, col)
    // arg order): DCT_ADST = row ADST + column DCT, ADST_DCT = the mirror,
    // ADST_ADST = both. ADST_ADST keeps the old else-branch behavior exactly.
    const int rowAdst = (*txType == 2) || (*txType == 3);
    const int colAdst = (*txType == 1) || (*txType == 3);
    int tmp[4];
    int o[4];
    for (int c = 0; c < 4; ++c) {
        tmp[c] = d_cv(coeffs[t * 4 + c], 16);
    }
    if (rowAdst) {
        d_iadst4i(tmp, o);
    } else {
        d_idct4i(tmp, o);
    }
    for (int c = 0; c < 4; ++c) {
        sbuf[t * 4 + c] = o[c];
    }
    __syncthreads();
    for (int r = 0; r < 4; ++r) {
        tmp[r] = d_cv(sbuf[r * 4 + t], 16);
    }
    if (colAdst) {
        d_iadst4i(tmp, o);
    } else {
        d_idct4i(tmp, o);
    }
    for (int r = 0; r < 4; ++r) {
        int v = (int)dst[r * (*stride) + t] + d_round_shift(o[r], 4);
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
    // RE7 (court order): the per-pass selection, the inv 4x4 note.
    const int rowAdst = (*txType == 2) || (*txType == 3);
    const int colAdst = (*txType == 1) || (*txType == 3);
    int tmp[8];
    int o[8];
    for (int c = 0; c < 8; ++c) {
        tmp[c] = d_cv(coeffs[t * 8 + c], 16);
    }
    if (rowAdst) {
        d_iadst8i(tmp, o);
    } else {
        d_idct8i(tmp, o);
    }
    for (int c = 0; c < 8; ++c) {
        sbuf[t * 8 + c] = d_round_shift(o[c], 1);
    }
    __syncthreads();
    for (int r = 0; r < 8; ++r) {
        tmp[r] = d_cv(sbuf[r * 8 + t], 16);
    }
    if (colAdst) {
        d_iadst8i(tmp, o);
    } else {
        d_idct8i(tmp, o);
    }
    for (int r = 0; r < 8; ++r) {
        int v = (int)dst[r * (*stride) + t] + d_round_shift(o[r], 4);
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
    // RE7 (court order): the per-pass selection, the inv 4x4 note.
    const int rowAdst = (*txType == 2) || (*txType == 3);
    const int colAdst = (*txType == 1) || (*txType == 3);
    int tmp[16];
    int o[16];
    for (int c = 0; c < 16; ++c) {
        tmp[c] = d_cv(coeffs[t * 16 + c], 16);
    }
    if (rowAdst) {
        d_iadst16i(tmp, o);
    } else {
        d_idct16i(tmp, o);
    }
    for (int c = 0; c < 16; ++c) {
        sbuf[t * 16 + c] = d_round_shift(o[c], 2);
    }
    __syncthreads();
    for (int r = 0; r < 16; ++r) {
        tmp[r] = d_cv(sbuf[r * 16 + t], 16);
    }
    if (colAdst) {
        d_iadst16i(tmp, o);
    } else {
        d_idct16i(tmp, o);
    }
    for (int r = 0; r < 16; ++r) {
        int v = (int)dst[r * (*stride) + t] + d_round_shift(o[r], 4);
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
        sbuf[t * 32 + c] = d_round_shift(o[c], 2);
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
        int v = (int)dst[r * (*stride) + t] + d_round_shift(o[r], 4);
        if (v < 0) v = 0;
        else if (v > 255) v = 255;
        dst[r * (*stride) + t] = (unsigned char)v;
    }
}
)CUDB4";
    s += R"CUDB5(
__device__ void d_idct64i(const int* input, int* output) {
    const int bit = 12;
    const int* cospi = kC12;
    const int cr = 16;
    int* bf0;
    int* bf1;
    int step[64];
    int stage = 0;



    // stage 0;

    // stage 1;
    stage++;
    bf1     = output;
    bf1[0]  = input[0];
    bf1[1]  = input[32];
    bf1[2]  = input[16];
    bf1[3]  = input[48];
    bf1[4]  = input[8];
    bf1[5]  = input[40];
    bf1[6]  = input[24];
    bf1[7]  = input[56];
    bf1[8]  = input[4];
    bf1[9]  = input[36];
    bf1[10] = input[20];
    bf1[11] = input[52];
    bf1[12] = input[12];
    bf1[13] = input[44];
    bf1[14] = input[28];
    bf1[15] = input[60];
    bf1[16] = input[2];
    bf1[17] = input[34];
    bf1[18] = input[18];
    bf1[19] = input[50];
    bf1[20] = input[10];
    bf1[21] = input[42];
    bf1[22] = input[26];
    bf1[23] = input[58];
    bf1[24] = input[6];
    bf1[25] = input[38];
    bf1[26] = input[22];
    bf1[27] = input[54];
    bf1[28] = input[14];
    bf1[29] = input[46];
    bf1[30] = input[30];
    bf1[31] = input[62];
    bf1[32] = input[1];
    bf1[33] = input[33];
    bf1[34] = input[17];
    bf1[35] = input[49];
    bf1[36] = input[9];
    bf1[37] = input[41];
    bf1[38] = input[25];
    bf1[39] = input[57];
    bf1[40] = input[5];
    bf1[41] = input[37];
    bf1[42] = input[21];
    bf1[43] = input[53];
    bf1[44] = input[13];
    bf1[45] = input[45];
    bf1[46] = input[29];
    bf1[47] = input[61];
    bf1[48] = input[3];
    bf1[49] = input[35];
    bf1[50] = input[19];
    bf1[51] = input[51];
    bf1[52] = input[11];
    bf1[53] = input[43];
    bf1[54] = input[27];
    bf1[55] = input[59];
    bf1[56] = input[7];
    bf1[57] = input[39];
    bf1[58] = input[23];
    bf1[59] = input[55];
    bf1[60] = input[15];
    bf1[61] = input[47];
    bf1[62] = input[31];
    bf1[63] = input[63];
    //range_check_buf(stage, input, bf1, size, cr);

    // stage 2
    stage++;
    bf0     = output;
    bf1     = step;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = bf0[4];
    bf1[5]  = bf0[5];
    bf1[6]  = bf0[6];
    bf1[7]  = bf0[7];
    bf1[8]  = bf0[8];
    bf1[9]  = bf0[9];
    bf1[10] = bf0[10];
    bf1[11] = bf0[11];
    bf1[12] = bf0[12];
    bf1[13] = bf0[13];
    bf1[14] = bf0[14];
    bf1[15] = bf0[15];
    bf1[16] = bf0[16];
    bf1[17] = bf0[17];
    bf1[18] = bf0[18];
    bf1[19] = bf0[19];
    bf1[20] = bf0[20];
    bf1[21] = bf0[21];
    bf1[22] = bf0[22];
    bf1[23] = bf0[23];
    bf1[24] = bf0[24];
    bf1[25] = bf0[25];
    bf1[26] = bf0[26];
    bf1[27] = bf0[27];
    bf1[28] = bf0[28];
    bf1[29] = bf0[29];
    bf1[30] = bf0[30];
    bf1[31] = bf0[31];
    bf1[32] = d_half_btf(cospi[63], bf0[32], -cospi[1], bf0[63], bit);
    bf1[33] = d_half_btf(cospi[31], bf0[33], -cospi[33], bf0[62], bit);
    bf1[34] = d_half_btf(cospi[47], bf0[34], -cospi[17], bf0[61], bit);
    bf1[35] = d_half_btf(cospi[15], bf0[35], -cospi[49], bf0[60], bit);
    bf1[36] = d_half_btf(cospi[55], bf0[36], -cospi[9], bf0[59], bit);
    bf1[37] = d_half_btf(cospi[23], bf0[37], -cospi[41], bf0[58], bit);
    bf1[38] = d_half_btf(cospi[39], bf0[38], -cospi[25], bf0[57], bit);
    bf1[39] = d_half_btf(cospi[7], bf0[39], -cospi[57], bf0[56], bit);
    bf1[40] = d_half_btf(cospi[59], bf0[40], -cospi[5], bf0[55], bit);
    bf1[41] = d_half_btf(cospi[27], bf0[41], -cospi[37], bf0[54], bit);
    bf1[42] = d_half_btf(cospi[43], bf0[42], -cospi[21], bf0[53], bit);
    bf1[43] = d_half_btf(cospi[11], bf0[43], -cospi[53], bf0[52], bit);
    bf1[44] = d_half_btf(cospi[51], bf0[44], -cospi[13], bf0[51], bit);
    bf1[45] = d_half_btf(cospi[19], bf0[45], -cospi[45], bf0[50], bit);
    bf1[46] = d_half_btf(cospi[35], bf0[46], -cospi[29], bf0[49], bit);
    bf1[47] = d_half_btf(cospi[3], bf0[47], -cospi[61], bf0[48], bit);
    bf1[48] = d_half_btf(cospi[61], bf0[47], cospi[3], bf0[48], bit);
    bf1[49] = d_half_btf(cospi[29], bf0[46], cospi[35], bf0[49], bit);
    bf1[50] = d_half_btf(cospi[45], bf0[45], cospi[19], bf0[50], bit);
    bf1[51] = d_half_btf(cospi[13], bf0[44], cospi[51], bf0[51], bit);
    bf1[52] = d_half_btf(cospi[53], bf0[43], cospi[11], bf0[52], bit);
    bf1[53] = d_half_btf(cospi[21], bf0[42], cospi[43], bf0[53], bit);
    bf1[54] = d_half_btf(cospi[37], bf0[41], cospi[27], bf0[54], bit);
    bf1[55] = d_half_btf(cospi[5], bf0[40], cospi[59], bf0[55], bit);
    bf1[56] = d_half_btf(cospi[57], bf0[39], cospi[7], bf0[56], bit);
    bf1[57] = d_half_btf(cospi[25], bf0[38], cospi[39], bf0[57], bit);
    bf1[58] = d_half_btf(cospi[41], bf0[37], cospi[23], bf0[58], bit);
    bf1[59] = d_half_btf(cospi[9], bf0[36], cospi[55], bf0[59], bit);
    bf1[60] = d_half_btf(cospi[49], bf0[35], cospi[15], bf0[60], bit);
    bf1[61] = d_half_btf(cospi[17], bf0[34], cospi[47], bf0[61], bit);
    bf1[62] = d_half_btf(cospi[33], bf0[33], cospi[31], bf0[62], bit);
    bf1[63] = d_half_btf(cospi[1], bf0[32], cospi[63], bf0[63], bit);
    //range_check_buf(stage, input, bf1, size, cr);

    // stage 3
    stage++;
    bf0     = step;
    bf1     = output;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = bf0[4];
    bf1[5]  = bf0[5];
    bf1[6]  = bf0[6];
    bf1[7]  = bf0[7];
    bf1[8]  = bf0[8];
    bf1[9]  = bf0[9];
    bf1[10] = bf0[10];
    bf1[11] = bf0[11];
    bf1[12] = bf0[12];
    bf1[13] = bf0[13];
    bf1[14] = bf0[14];
    bf1[15] = bf0[15];
    bf1[16] = d_half_btf(cospi[62], bf0[16], -cospi[2], bf0[31], bit);
    bf1[17] = d_half_btf(cospi[30], bf0[17], -cospi[34], bf0[30], bit);
    bf1[18] = d_half_btf(cospi[46], bf0[18], -cospi[18], bf0[29], bit);
    bf1[19] = d_half_btf(cospi[14], bf0[19], -cospi[50], bf0[28], bit);
    bf1[20] = d_half_btf(cospi[54], bf0[20], -cospi[10], bf0[27], bit);
    bf1[21] = d_half_btf(cospi[22], bf0[21], -cospi[42], bf0[26], bit);
    bf1[22] = d_half_btf(cospi[38], bf0[22], -cospi[26], bf0[25], bit);
    bf1[23] = d_half_btf(cospi[6], bf0[23], -cospi[58], bf0[24], bit);
    bf1[24] = d_half_btf(cospi[58], bf0[23], cospi[6], bf0[24], bit);
    bf1[25] = d_half_btf(cospi[26], bf0[22], cospi[38], bf0[25], bit);
    bf1[26] = d_half_btf(cospi[42], bf0[21], cospi[22], bf0[26], bit);
    bf1[27] = d_half_btf(cospi[10], bf0[20], cospi[54], bf0[27], bit);
    bf1[28] = d_half_btf(cospi[50], bf0[19], cospi[14], bf0[28], bit);
    bf1[29] = d_half_btf(cospi[18], bf0[18], cospi[46], bf0[29], bit);
    bf1[30] = d_half_btf(cospi[34], bf0[17], cospi[30], bf0[30], bit);
    bf1[31] = d_half_btf(cospi[2], bf0[16], cospi[62], bf0[31], bit);
    bf1[32] = d_cv(bf0[32] + bf0[33], cr);
    bf1[33] = d_cv(bf0[32] - bf0[33], cr);
    bf1[34] = d_cv(-bf0[34] + bf0[35], cr);
    bf1[35] = d_cv(bf0[34] + bf0[35], cr);
    bf1[36] = d_cv(bf0[36] + bf0[37], cr);
    bf1[37] = d_cv(bf0[36] - bf0[37], cr);
    bf1[38] = d_cv(-bf0[38] + bf0[39], cr);
    bf1[39] = d_cv(bf0[38] + bf0[39], cr);
    bf1[40] = d_cv(bf0[40] + bf0[41], cr);
    bf1[41] = d_cv(bf0[40] - bf0[41], cr);
    bf1[42] = d_cv(-bf0[42] + bf0[43], cr);
    bf1[43] = d_cv(bf0[42] + bf0[43], cr);
    bf1[44] = d_cv(bf0[44] + bf0[45], cr);
    bf1[45] = d_cv(bf0[44] - bf0[45], cr);
    bf1[46] = d_cv(-bf0[46] + bf0[47], cr);
    bf1[47] = d_cv(bf0[46] + bf0[47], cr);
    bf1[48] = d_cv(bf0[48] + bf0[49], cr);
    bf1[49] = d_cv(bf0[48] - bf0[49], cr);
    bf1[50] = d_cv(-bf0[50] + bf0[51], cr);
    bf1[51] = d_cv(bf0[50] + bf0[51], cr);
    bf1[52] = d_cv(bf0[52] + bf0[53], cr);
    bf1[53] = d_cv(bf0[52] - bf0[53], cr);
    bf1[54] = d_cv(-bf0[54] + bf0[55], cr);
    bf1[55] = d_cv(bf0[54] + bf0[55], cr);
    bf1[56] = d_cv(bf0[56] + bf0[57], cr);
    bf1[57] = d_cv(bf0[56] - bf0[57], cr);
    bf1[58] = d_cv(-bf0[58] + bf0[59], cr);
    bf1[59] = d_cv(bf0[58] + bf0[59], cr);
    bf1[60] = d_cv(bf0[60] + bf0[61], cr);
    bf1[61] = d_cv(bf0[60] - bf0[61], cr);
    bf1[62] = d_cv(-bf0[62] + bf0[63], cr);
    bf1[63] = d_cv(bf0[62] + bf0[63], cr);
    //range_check_buf(stage, input, bf1, size, cr);
)CUDB5";
    s += R"CUDB5(
    // stage 4
    stage++;
    bf0     = output;
    bf1     = step;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = bf0[4];
    bf1[5]  = bf0[5];
    bf1[6]  = bf0[6];
    bf1[7]  = bf0[7];
    bf1[8]  = d_half_btf(cospi[60], bf0[8], -cospi[4], bf0[15], bit);
    bf1[9]  = d_half_btf(cospi[28], bf0[9], -cospi[36], bf0[14], bit);
    bf1[10] = d_half_btf(cospi[44], bf0[10], -cospi[20], bf0[13], bit);
    bf1[11] = d_half_btf(cospi[12], bf0[11], -cospi[52], bf0[12], bit);
    bf1[12] = d_half_btf(cospi[52], bf0[11], cospi[12], bf0[12], bit);
    bf1[13] = d_half_btf(cospi[20], bf0[10], cospi[44], bf0[13], bit);
    bf1[14] = d_half_btf(cospi[36], bf0[9], cospi[28], bf0[14], bit);
    bf1[15] = d_half_btf(cospi[4], bf0[8], cospi[60], bf0[15], bit);
    bf1[16] = d_cv(bf0[16] + bf0[17], cr);
    bf1[17] = d_cv(bf0[16] - bf0[17], cr);
    bf1[18] = d_cv(-bf0[18] + bf0[19], cr);
    bf1[19] = d_cv(bf0[18] + bf0[19], cr);
    bf1[20] = d_cv(bf0[20] + bf0[21], cr);
    bf1[21] = d_cv(bf0[20] - bf0[21], cr);
    bf1[22] = d_cv(-bf0[22] + bf0[23], cr);
    bf1[23] = d_cv(bf0[22] + bf0[23], cr);
    bf1[24] = d_cv(bf0[24] + bf0[25], cr);
    bf1[25] = d_cv(bf0[24] - bf0[25], cr);
    bf1[26] = d_cv(-bf0[26] + bf0[27], cr);
    bf1[27] = d_cv(bf0[26] + bf0[27], cr);
    bf1[28] = d_cv(bf0[28] + bf0[29], cr);
    bf1[29] = d_cv(bf0[28] - bf0[29], cr);
    bf1[30] = d_cv(-bf0[30] + bf0[31], cr);
    bf1[31] = d_cv(bf0[30] + bf0[31], cr);
    bf1[32] = bf0[32];
    bf1[33] = d_half_btf(-cospi[4], bf0[33], cospi[60], bf0[62], bit);
    bf1[34] = d_half_btf(-cospi[60], bf0[34], -cospi[4], bf0[61], bit);
    bf1[35] = bf0[35];
    bf1[36] = bf0[36];
    bf1[37] = d_half_btf(-cospi[36], bf0[37], cospi[28], bf0[58], bit);
    bf1[38] = d_half_btf(-cospi[28], bf0[38], -cospi[36], bf0[57], bit);
    bf1[39] = bf0[39];
    bf1[40] = bf0[40];
    bf1[41] = d_half_btf(-cospi[20], bf0[41], cospi[44], bf0[54], bit);
    bf1[42] = d_half_btf(-cospi[44], bf0[42], -cospi[20], bf0[53], bit);
    bf1[43] = bf0[43];
    bf1[44] = bf0[44];
    bf1[45] = d_half_btf(-cospi[52], bf0[45], cospi[12], bf0[50], bit);
    bf1[46] = d_half_btf(-cospi[12], bf0[46], -cospi[52], bf0[49], bit);
    bf1[47] = bf0[47];
    bf1[48] = bf0[48];
    bf1[49] = d_half_btf(-cospi[52], bf0[46], cospi[12], bf0[49], bit);
    bf1[50] = d_half_btf(cospi[12], bf0[45], cospi[52], bf0[50], bit);
    bf1[51] = bf0[51];
    bf1[52] = bf0[52];
    bf1[53] = d_half_btf(-cospi[20], bf0[42], cospi[44], bf0[53], bit);
    bf1[54] = d_half_btf(cospi[44], bf0[41], cospi[20], bf0[54], bit);
    bf1[55] = bf0[55];
    bf1[56] = bf0[56];
    bf1[57] = d_half_btf(-cospi[36], bf0[38], cospi[28], bf0[57], bit);
    bf1[58] = d_half_btf(cospi[28], bf0[37], cospi[36], bf0[58], bit);
    bf1[59] = bf0[59];
    bf1[60] = bf0[60];
    bf1[61] = d_half_btf(-cospi[4], bf0[34], cospi[60], bf0[61], bit);
    bf1[62] = d_half_btf(cospi[60], bf0[33], cospi[4], bf0[62], bit);
    bf1[63] = bf0[63];
    //range_check_buf(stage, input, bf1, size, cr);

    // stage 5
    stage++;
    bf0     = step;
    bf1     = output;
    bf1[0]  = bf0[0];
    bf1[1]  = bf0[1];
    bf1[2]  = bf0[2];
    bf1[3]  = bf0[3];
    bf1[4]  = d_half_btf(cospi[56], bf0[4], -cospi[8], bf0[7], bit);
    bf1[5]  = d_half_btf(cospi[24], bf0[5], -cospi[40], bf0[6], bit);
    bf1[6]  = d_half_btf(cospi[40], bf0[5], cospi[24], bf0[6], bit);
    bf1[7]  = d_half_btf(cospi[8], bf0[4], cospi[56], bf0[7], bit);
    bf1[8]  = d_cv(bf0[8] + bf0[9], cr);
    bf1[9]  = d_cv(bf0[8] - bf0[9], cr);
    bf1[10] = d_cv(-bf0[10] + bf0[11], cr);
    bf1[11] = d_cv(bf0[10] + bf0[11], cr);
    bf1[12] = d_cv(bf0[12] + bf0[13], cr);
    bf1[13] = d_cv(bf0[12] - bf0[13], cr);
    bf1[14] = d_cv(-bf0[14] + bf0[15], cr);
    bf1[15] = d_cv(bf0[14] + bf0[15], cr);
    bf1[16] = bf0[16];
    bf1[17] = d_half_btf(-cospi[8], bf0[17], cospi[56], bf0[30], bit);
    bf1[18] = d_half_btf(-cospi[56], bf0[18], -cospi[8], bf0[29], bit);
    bf1[19] = bf0[19];
    bf1[20] = bf0[20];
    bf1[21] = d_half_btf(-cospi[40], bf0[21], cospi[24], bf0[26], bit);
    bf1[22] = d_half_btf(-cospi[24], bf0[22], -cospi[40], bf0[25], bit);
    bf1[23] = bf0[23];
    bf1[24] = bf0[24];
    bf1[25] = d_half_btf(-cospi[40], bf0[22], cospi[24], bf0[25], bit);
    bf1[26] = d_half_btf(cospi[24], bf0[21], cospi[40], bf0[26], bit);
    bf1[27] = bf0[27];
    bf1[28] = bf0[28];
    bf1[29] = d_half_btf(-cospi[8], bf0[18], cospi[56], bf0[29], bit);
    bf1[30] = d_half_btf(cospi[56], bf0[17], cospi[8], bf0[30], bit);
    bf1[31] = bf0[31];
    bf1[32] = d_cv(bf0[32] + bf0[35], cr);
    bf1[33] = d_cv(bf0[33] + bf0[34], cr);
    bf1[34] = d_cv(bf0[33] - bf0[34], cr);
    bf1[35] = d_cv(bf0[32] - bf0[35], cr);
    bf1[36] = d_cv(-bf0[36] + bf0[39], cr);
    bf1[37] = d_cv(-bf0[37] + bf0[38], cr);
    bf1[38] = d_cv(bf0[37] + bf0[38], cr);
    bf1[39] = d_cv(bf0[36] + bf0[39], cr);
    bf1[40] = d_cv(bf0[40] + bf0[43], cr);
    bf1[41] = d_cv(bf0[41] + bf0[42], cr);
    bf1[42] = d_cv(bf0[41] - bf0[42], cr);
    bf1[43] = d_cv(bf0[40] - bf0[43], cr);
    bf1[44] = d_cv(-bf0[44] + bf0[47], cr);
    bf1[45] = d_cv(-bf0[45] + bf0[46], cr);
    bf1[46] = d_cv(bf0[45] + bf0[46], cr);
    bf1[47] = d_cv(bf0[44] + bf0[47], cr);
    bf1[48] = d_cv(bf0[48] + bf0[51], cr);
    bf1[49] = d_cv(bf0[49] + bf0[50], cr);
    bf1[50] = d_cv(bf0[49] - bf0[50], cr);
    bf1[51] = d_cv(bf0[48] - bf0[51], cr);
    bf1[52] = d_cv(-bf0[52] + bf0[55], cr);
    bf1[53] = d_cv(-bf0[53] + bf0[54], cr);
    bf1[54] = d_cv(bf0[53] + bf0[54], cr);
    bf1[55] = d_cv(bf0[52] + bf0[55], cr);
    bf1[56] = d_cv(bf0[56] + bf0[59], cr);
    bf1[57] = d_cv(bf0[57] + bf0[58], cr);
    bf1[58] = d_cv(bf0[57] - bf0[58], cr);
    bf1[59] = d_cv(bf0[56] - bf0[59], cr);
    bf1[60] = d_cv(-bf0[60] + bf0[63], cr);
    bf1[61] = d_cv(-bf0[61] + bf0[62], cr);
    bf1[62] = d_cv(bf0[61] + bf0[62], cr);
    bf1[63] = d_cv(bf0[60] + bf0[63], cr);
    //range_check_buf(stage, input, bf1, size, cr);

    // stage 6
    stage++;
    bf0     = output;
    bf1     = step;
    bf1[0]  = d_half_btf(cospi[32], bf0[0], cospi[32], bf0[1], bit);
    bf1[1]  = d_half_btf(cospi[32], bf0[0], -cospi[32], bf0[1], bit);
    bf1[2]  = d_half_btf(cospi[48], bf0[2], -cospi[16], bf0[3], bit);
    bf1[3]  = d_half_btf(cospi[16], bf0[2], cospi[48], bf0[3], bit);
    bf1[4]  = d_cv(bf0[4] + bf0[5], cr);
    bf1[5]  = d_cv(bf0[4] - bf0[5], cr);
    bf1[6]  = d_cv(-bf0[6] + bf0[7], cr);
    bf1[7]  = d_cv(bf0[6] + bf0[7], cr);
    bf1[8]  = bf0[8];
    bf1[9]  = d_half_btf(-cospi[16], bf0[9], cospi[48], bf0[14], bit);
    bf1[10] = d_half_btf(-cospi[48], bf0[10], -cospi[16], bf0[13], bit);
    bf1[11] = bf0[11];
    bf1[12] = bf0[12];
    bf1[13] = d_half_btf(-cospi[16], bf0[10], cospi[48], bf0[13], bit);
    bf1[14] = d_half_btf(cospi[48], bf0[9], cospi[16], bf0[14], bit);
    bf1[15] = bf0[15];
    bf1[16] = d_cv(bf0[16] + bf0[19], cr);
    bf1[17] = d_cv(bf0[17] + bf0[18], cr);
    bf1[18] = d_cv(bf0[17] - bf0[18], cr);
    bf1[19] = d_cv(bf0[16] - bf0[19], cr);
    bf1[20] = d_cv(-bf0[20] + bf0[23], cr);
    bf1[21] = d_cv(-bf0[21] + bf0[22], cr);
    bf1[22] = d_cv(bf0[21] + bf0[22], cr);
    bf1[23] = d_cv(bf0[20] + bf0[23], cr);
    bf1[24] = d_cv(bf0[24] + bf0[27], cr);
    bf1[25] = d_cv(bf0[25] + bf0[26], cr);
    bf1[26] = d_cv(bf0[25] - bf0[26], cr);
    bf1[27] = d_cv(bf0[24] - bf0[27], cr);
    bf1[28] = d_cv(-bf0[28] + bf0[31], cr);
    bf1[29] = d_cv(-bf0[29] + bf0[30], cr);
    bf1[30] = d_cv(bf0[29] + bf0[30], cr);
    bf1[31] = d_cv(bf0[28] + bf0[31], cr);
    bf1[32] = bf0[32];
    bf1[33] = bf0[33];
    bf1[34] = d_half_btf(-cospi[8], bf0[34], cospi[56], bf0[61], bit);
    bf1[35] = d_half_btf(-cospi[8], bf0[35], cospi[56], bf0[60], bit);
    bf1[36] = d_half_btf(-cospi[56], bf0[36], -cospi[8], bf0[59], bit);
    bf1[37] = d_half_btf(-cospi[56], bf0[37], -cospi[8], bf0[58], bit);
    bf1[38] = bf0[38];
    bf1[39] = bf0[39];
    bf1[40] = bf0[40];
    bf1[41] = bf0[41];
    bf1[42] = d_half_btf(-cospi[40], bf0[42], cospi[24], bf0[53], bit);
    bf1[43] = d_half_btf(-cospi[40], bf0[43], cospi[24], bf0[52], bit);
    bf1[44] = d_half_btf(-cospi[24], bf0[44], -cospi[40], bf0[51], bit);
    bf1[45] = d_half_btf(-cospi[24], bf0[45], -cospi[40], bf0[50], bit);
    bf1[46] = bf0[46];
    bf1[47] = bf0[47];
    bf1[48] = bf0[48];
    bf1[49] = bf0[49];
    bf1[50] = d_half_btf(-cospi[40], bf0[45], cospi[24], bf0[50], bit);
    bf1[51] = d_half_btf(-cospi[40], bf0[44], cospi[24], bf0[51], bit);
    bf1[52] = d_half_btf(cospi[24], bf0[43], cospi[40], bf0[52], bit);
    bf1[53] = d_half_btf(cospi[24], bf0[42], cospi[40], bf0[53], bit);
    bf1[54] = bf0[54];
    bf1[55] = bf0[55];
    bf1[56] = bf0[56];
    bf1[57] = bf0[57];
    bf1[58] = d_half_btf(-cospi[8], bf0[37], cospi[56], bf0[58], bit);
    bf1[59] = d_half_btf(-cospi[8], bf0[36], cospi[56], bf0[59], bit);
    bf1[60] = d_half_btf(cospi[56], bf0[35], cospi[8], bf0[60], bit);
    bf1[61] = d_half_btf(cospi[56], bf0[34], cospi[8], bf0[61], bit);
    bf1[62] = bf0[62];
    bf1[63] = bf0[63];
    //range_check_buf(stage, input, bf1, size, cr);
)CUDB5";
    s += R"CUDB5(
    // stage 7
    stage++;
    bf0     = step;
    bf1     = output;
    bf1[0]  = d_cv(bf0[0] + bf0[3], cr);
    bf1[1]  = d_cv(bf0[1] + bf0[2], cr);
    bf1[2]  = d_cv(bf0[1] - bf0[2], cr);
    bf1[3]  = d_cv(bf0[0] - bf0[3], cr);
    bf1[4]  = bf0[4];
    bf1[5]  = d_half_btf(-cospi[32], bf0[5], cospi[32], bf0[6], bit);
    bf1[6]  = d_half_btf(cospi[32], bf0[5], cospi[32], bf0[6], bit);
    bf1[7]  = bf0[7];
    bf1[8]  = d_cv(bf0[8] + bf0[11], cr);
    bf1[9]  = d_cv(bf0[9] + bf0[10], cr);
    bf1[10] = d_cv(bf0[9] - bf0[10], cr);
    bf1[11] = d_cv(bf0[8] - bf0[11], cr);
    bf1[12] = d_cv(-bf0[12] + bf0[15], cr);
    bf1[13] = d_cv(-bf0[13] + bf0[14], cr);
    bf1[14] = d_cv(bf0[13] + bf0[14], cr);
    bf1[15] = d_cv(bf0[12] + bf0[15], cr);
    bf1[16] = bf0[16];
    bf1[17] = bf0[17];
    bf1[18] = d_half_btf(-cospi[16], bf0[18], cospi[48], bf0[29], bit);
    bf1[19] = d_half_btf(-cospi[16], bf0[19], cospi[48], bf0[28], bit);
    bf1[20] = d_half_btf(-cospi[48], bf0[20], -cospi[16], bf0[27], bit);
    bf1[21] = d_half_btf(-cospi[48], bf0[21], -cospi[16], bf0[26], bit);
    bf1[22] = bf0[22];
    bf1[23] = bf0[23];
    bf1[24] = bf0[24];
    bf1[25] = bf0[25];
    bf1[26] = d_half_btf(-cospi[16], bf0[21], cospi[48], bf0[26], bit);
    bf1[27] = d_half_btf(-cospi[16], bf0[20], cospi[48], bf0[27], bit);
    bf1[28] = d_half_btf(cospi[48], bf0[19], cospi[16], bf0[28], bit);
    bf1[29] = d_half_btf(cospi[48], bf0[18], cospi[16], bf0[29], bit);
    bf1[30] = bf0[30];
    bf1[31] = bf0[31];
    bf1[32] = d_cv(bf0[32] + bf0[39], cr);
    bf1[33] = d_cv(bf0[33] + bf0[38], cr);
    bf1[34] = d_cv(bf0[34] + bf0[37], cr);
    bf1[35] = d_cv(bf0[35] + bf0[36], cr);
    bf1[36] = d_cv(bf0[35] - bf0[36], cr);
    bf1[37] = d_cv(bf0[34] - bf0[37], cr);
    bf1[38] = d_cv(bf0[33] - bf0[38], cr);
    bf1[39] = d_cv(bf0[32] - bf0[39], cr);
    bf1[40] = d_cv(-bf0[40] + bf0[47], cr);
    bf1[41] = d_cv(-bf0[41] + bf0[46], cr);
    bf1[42] = d_cv(-bf0[42] + bf0[45], cr);
    bf1[43] = d_cv(-bf0[43] + bf0[44], cr);
    bf1[44] = d_cv(bf0[43] + bf0[44], cr);
    bf1[45] = d_cv(bf0[42] + bf0[45], cr);
    bf1[46] = d_cv(bf0[41] + bf0[46], cr);
    bf1[47] = d_cv(bf0[40] + bf0[47], cr);
    bf1[48] = d_cv(bf0[48] + bf0[55], cr);
    bf1[49] = d_cv(bf0[49] + bf0[54], cr);
    bf1[50] = d_cv(bf0[50] + bf0[53], cr);
    bf1[51] = d_cv(bf0[51] + bf0[52], cr);
    bf1[52] = d_cv(bf0[51] - bf0[52], cr);
    bf1[53] = d_cv(bf0[50] - bf0[53], cr);
    bf1[54] = d_cv(bf0[49] - bf0[54], cr);
    bf1[55] = d_cv(bf0[48] - bf0[55], cr);
    bf1[56] = d_cv(-bf0[56] + bf0[63], cr);
    bf1[57] = d_cv(-bf0[57] + bf0[62], cr);
    bf1[58] = d_cv(-bf0[58] + bf0[61], cr);
    bf1[59] = d_cv(-bf0[59] + bf0[60], cr);
    bf1[60] = d_cv(bf0[59] + bf0[60], cr);
    bf1[61] = d_cv(bf0[58] + bf0[61], cr);
    bf1[62] = d_cv(bf0[57] + bf0[62], cr);
    bf1[63] = d_cv(bf0[56] + bf0[63], cr);
    //range_check_buf(stage, input, bf1, size, cr);

    // stage 8
    stage++;
    bf0     = output;
    bf1     = step;
    bf1[0]  = d_cv(bf0[0] + bf0[7], cr);
    bf1[1]  = d_cv(bf0[1] + bf0[6], cr);
    bf1[2]  = d_cv(bf0[2] + bf0[5], cr);
    bf1[3]  = d_cv(bf0[3] + bf0[4], cr);
    bf1[4]  = d_cv(bf0[3] - bf0[4], cr);
    bf1[5]  = d_cv(bf0[2] - bf0[5], cr);
    bf1[6]  = d_cv(bf0[1] - bf0[6], cr);
    bf1[7]  = d_cv(bf0[0] - bf0[7], cr);
    bf1[8]  = bf0[8];
    bf1[9]  = bf0[9];
    bf1[10] = d_half_btf(-cospi[32], bf0[10], cospi[32], bf0[13], bit);
    bf1[11] = d_half_btf(-cospi[32], bf0[11], cospi[32], bf0[12], bit);
    bf1[12] = d_half_btf(cospi[32], bf0[11], cospi[32], bf0[12], bit);
    bf1[13] = d_half_btf(cospi[32], bf0[10], cospi[32], bf0[13], bit);
    bf1[14] = bf0[14];
    bf1[15] = bf0[15];
    bf1[16] = d_cv(bf0[16] + bf0[23], cr);
    bf1[17] = d_cv(bf0[17] + bf0[22], cr);
    bf1[18] = d_cv(bf0[18] + bf0[21], cr);
    bf1[19] = d_cv(bf0[19] + bf0[20], cr);
    bf1[20] = d_cv(bf0[19] - bf0[20], cr);
    bf1[21] = d_cv(bf0[18] - bf0[21], cr);
    bf1[22] = d_cv(bf0[17] - bf0[22], cr);
    bf1[23] = d_cv(bf0[16] - bf0[23], cr);
    bf1[24] = d_cv(-bf0[24] + bf0[31], cr);
    bf1[25] = d_cv(-bf0[25] + bf0[30], cr);
    bf1[26] = d_cv(-bf0[26] + bf0[29], cr);
    bf1[27] = d_cv(-bf0[27] + bf0[28], cr);
    bf1[28] = d_cv(bf0[27] + bf0[28], cr);
    bf1[29] = d_cv(bf0[26] + bf0[29], cr);
    bf1[30] = d_cv(bf0[25] + bf0[30], cr);
    bf1[31] = d_cv(bf0[24] + bf0[31], cr);
    bf1[32] = bf0[32];
    bf1[33] = bf0[33];
    bf1[34] = bf0[34];
    bf1[35] = bf0[35];
    bf1[36] = d_half_btf(-cospi[16], bf0[36], cospi[48], bf0[59], bit);
    bf1[37] = d_half_btf(-cospi[16], bf0[37], cospi[48], bf0[58], bit);
    bf1[38] = d_half_btf(-cospi[16], bf0[38], cospi[48], bf0[57], bit);
    bf1[39] = d_half_btf(-cospi[16], bf0[39], cospi[48], bf0[56], bit);
    bf1[40] = d_half_btf(-cospi[48], bf0[40], -cospi[16], bf0[55], bit);
    bf1[41] = d_half_btf(-cospi[48], bf0[41], -cospi[16], bf0[54], bit);
    bf1[42] = d_half_btf(-cospi[48], bf0[42], -cospi[16], bf0[53], bit);
    bf1[43] = d_half_btf(-cospi[48], bf0[43], -cospi[16], bf0[52], bit);
    bf1[44] = bf0[44];
    bf1[45] = bf0[45];
    bf1[46] = bf0[46];
    bf1[47] = bf0[47];
    bf1[48] = bf0[48];
    bf1[49] = bf0[49];
    bf1[50] = bf0[50];
    bf1[51] = bf0[51];
    bf1[52] = d_half_btf(-cospi[16], bf0[43], cospi[48], bf0[52], bit);
    bf1[53] = d_half_btf(-cospi[16], bf0[42], cospi[48], bf0[53], bit);
    bf1[54] = d_half_btf(-cospi[16], bf0[41], cospi[48], bf0[54], bit);
    bf1[55] = d_half_btf(-cospi[16], bf0[40], cospi[48], bf0[55], bit);
    bf1[56] = d_half_btf(cospi[48], bf0[39], cospi[16], bf0[56], bit);
    bf1[57] = d_half_btf(cospi[48], bf0[38], cospi[16], bf0[57], bit);
    bf1[58] = d_half_btf(cospi[48], bf0[37], cospi[16], bf0[58], bit);
    bf1[59] = d_half_btf(cospi[48], bf0[36], cospi[16], bf0[59], bit);
    bf1[60] = bf0[60];
    bf1[61] = bf0[61];
    bf1[62] = bf0[62];
    bf1[63] = bf0[63];
    //range_check_buf(stage, input, bf1, size, cr);

    // stage 9
    stage++;
    bf0     = step;
    bf1     = output;
    bf1[0]  = d_cv(bf0[0] + bf0[15], cr);
    bf1[1]  = d_cv(bf0[1] + bf0[14], cr);
    bf1[2]  = d_cv(bf0[2] + bf0[13], cr);
    bf1[3]  = d_cv(bf0[3] + bf0[12], cr);
    bf1[4]  = d_cv(bf0[4] + bf0[11], cr);
    bf1[5]  = d_cv(bf0[5] + bf0[10], cr);
    bf1[6]  = d_cv(bf0[6] + bf0[9], cr);
    bf1[7]  = d_cv(bf0[7] + bf0[8], cr);
    bf1[8]  = d_cv(bf0[7] - bf0[8], cr);
    bf1[9]  = d_cv(bf0[6] - bf0[9], cr);
    bf1[10] = d_cv(bf0[5] - bf0[10], cr);
    bf1[11] = d_cv(bf0[4] - bf0[11], cr);
    bf1[12] = d_cv(bf0[3] - bf0[12], cr);
    bf1[13] = d_cv(bf0[2] - bf0[13], cr);
    bf1[14] = d_cv(bf0[1] - bf0[14], cr);
    bf1[15] = d_cv(bf0[0] - bf0[15], cr);
    bf1[16] = bf0[16];
    bf1[17] = bf0[17];
    bf1[18] = bf0[18];
    bf1[19] = bf0[19];
    bf1[20] = d_half_btf(-cospi[32], bf0[20], cospi[32], bf0[27], bit);
    bf1[21] = d_half_btf(-cospi[32], bf0[21], cospi[32], bf0[26], bit);
    bf1[22] = d_half_btf(-cospi[32], bf0[22], cospi[32], bf0[25], bit);
    bf1[23] = d_half_btf(-cospi[32], bf0[23], cospi[32], bf0[24], bit);
    bf1[24] = d_half_btf(cospi[32], bf0[23], cospi[32], bf0[24], bit);
    bf1[25] = d_half_btf(cospi[32], bf0[22], cospi[32], bf0[25], bit);
    bf1[26] = d_half_btf(cospi[32], bf0[21], cospi[32], bf0[26], bit);
    bf1[27] = d_half_btf(cospi[32], bf0[20], cospi[32], bf0[27], bit);
    bf1[28] = bf0[28];
    bf1[29] = bf0[29];
    bf1[30] = bf0[30];
    bf1[31] = bf0[31];
    bf1[32] = d_cv(bf0[32] + bf0[47], cr);
    bf1[33] = d_cv(bf0[33] + bf0[46], cr);
    bf1[34] = d_cv(bf0[34] + bf0[45], cr);
    bf1[35] = d_cv(bf0[35] + bf0[44], cr);
    bf1[36] = d_cv(bf0[36] + bf0[43], cr);
    bf1[37] = d_cv(bf0[37] + bf0[42], cr);
    bf1[38] = d_cv(bf0[38] + bf0[41], cr);
    bf1[39] = d_cv(bf0[39] + bf0[40], cr);
    bf1[40] = d_cv(bf0[39] - bf0[40], cr);
    bf1[41] = d_cv(bf0[38] - bf0[41], cr);
    bf1[42] = d_cv(bf0[37] - bf0[42], cr);
    bf1[43] = d_cv(bf0[36] - bf0[43], cr);
    bf1[44] = d_cv(bf0[35] - bf0[44], cr);
    bf1[45] = d_cv(bf0[34] - bf0[45], cr);
    bf1[46] = d_cv(bf0[33] - bf0[46], cr);
    bf1[47] = d_cv(bf0[32] - bf0[47], cr);
    bf1[48] = d_cv(-bf0[48] + bf0[63], cr);
    bf1[49] = d_cv(-bf0[49] + bf0[62], cr);
    bf1[50] = d_cv(-bf0[50] + bf0[61], cr);
    bf1[51] = d_cv(-bf0[51] + bf0[60], cr);
    bf1[52] = d_cv(-bf0[52] + bf0[59], cr);
    bf1[53] = d_cv(-bf0[53] + bf0[58], cr);
    bf1[54] = d_cv(-bf0[54] + bf0[57], cr);
    bf1[55] = d_cv(-bf0[55] + bf0[56], cr);
    bf1[56] = d_cv(bf0[55] + bf0[56], cr);
    bf1[57] = d_cv(bf0[54] + bf0[57], cr);
    bf1[58] = d_cv(bf0[53] + bf0[58], cr);
    bf1[59] = d_cv(bf0[52] + bf0[59], cr);
    bf1[60] = d_cv(bf0[51] + bf0[60], cr);
    bf1[61] = d_cv(bf0[50] + bf0[61], cr);
    bf1[62] = d_cv(bf0[49] + bf0[62], cr);
    bf1[63] = d_cv(bf0[48] + bf0[63], cr);
    //range_check_buf(stage, input, bf1, size, cr);
)CUDB5";
    s += R"CUDB5(
    // stage 10
    stage++;
    bf0     = output;
    bf1     = step;
    bf1[0]  = d_cv(bf0[0] + bf0[31], cr);
    bf1[1]  = d_cv(bf0[1] + bf0[30], cr);
    bf1[2]  = d_cv(bf0[2] + bf0[29], cr);
    bf1[3]  = d_cv(bf0[3] + bf0[28], cr);
    bf1[4]  = d_cv(bf0[4] + bf0[27], cr);
    bf1[5]  = d_cv(bf0[5] + bf0[26], cr);
    bf1[6]  = d_cv(bf0[6] + bf0[25], cr);
    bf1[7]  = d_cv(bf0[7] + bf0[24], cr);
    bf1[8]  = d_cv(bf0[8] + bf0[23], cr);
    bf1[9]  = d_cv(bf0[9] + bf0[22], cr);
    bf1[10] = d_cv(bf0[10] + bf0[21], cr);
    bf1[11] = d_cv(bf0[11] + bf0[20], cr);
    bf1[12] = d_cv(bf0[12] + bf0[19], cr);
    bf1[13] = d_cv(bf0[13] + bf0[18], cr);
    bf1[14] = d_cv(bf0[14] + bf0[17], cr);
    bf1[15] = d_cv(bf0[15] + bf0[16], cr);
    bf1[16] = d_cv(bf0[15] - bf0[16], cr);
    bf1[17] = d_cv(bf0[14] - bf0[17], cr);
    bf1[18] = d_cv(bf0[13] - bf0[18], cr);
    bf1[19] = d_cv(bf0[12] - bf0[19], cr);
    bf1[20] = d_cv(bf0[11] - bf0[20], cr);
    bf1[21] = d_cv(bf0[10] - bf0[21], cr);
    bf1[22] = d_cv(bf0[9] - bf0[22], cr);
    bf1[23] = d_cv(bf0[8] - bf0[23], cr);
    bf1[24] = d_cv(bf0[7] - bf0[24], cr);
    bf1[25] = d_cv(bf0[6] - bf0[25], cr);
    bf1[26] = d_cv(bf0[5] - bf0[26], cr);
    bf1[27] = d_cv(bf0[4] - bf0[27], cr);
    bf1[28] = d_cv(bf0[3] - bf0[28], cr);
    bf1[29] = d_cv(bf0[2] - bf0[29], cr);
    bf1[30] = d_cv(bf0[1] - bf0[30], cr);
    bf1[31] = d_cv(bf0[0] - bf0[31], cr);
    bf1[32] = bf0[32];
    bf1[33] = bf0[33];
    bf1[34] = bf0[34];
    bf1[35] = bf0[35];
    bf1[36] = bf0[36];
    bf1[37] = bf0[37];
    bf1[38] = bf0[38];
    bf1[39] = bf0[39];
    bf1[40] = d_half_btf(-cospi[32], bf0[40], cospi[32], bf0[55], bit);
    bf1[41] = d_half_btf(-cospi[32], bf0[41], cospi[32], bf0[54], bit);
    bf1[42] = d_half_btf(-cospi[32], bf0[42], cospi[32], bf0[53], bit);
    bf1[43] = d_half_btf(-cospi[32], bf0[43], cospi[32], bf0[52], bit);
    bf1[44] = d_half_btf(-cospi[32], bf0[44], cospi[32], bf0[51], bit);
    bf1[45] = d_half_btf(-cospi[32], bf0[45], cospi[32], bf0[50], bit);
    bf1[46] = d_half_btf(-cospi[32], bf0[46], cospi[32], bf0[49], bit);
    bf1[47] = d_half_btf(-cospi[32], bf0[47], cospi[32], bf0[48], bit);
    bf1[48] = d_half_btf(cospi[32], bf0[47], cospi[32], bf0[48], bit);
    bf1[49] = d_half_btf(cospi[32], bf0[46], cospi[32], bf0[49], bit);
    bf1[50] = d_half_btf(cospi[32], bf0[45], cospi[32], bf0[50], bit);
    bf1[51] = d_half_btf(cospi[32], bf0[44], cospi[32], bf0[51], bit);
    bf1[52] = d_half_btf(cospi[32], bf0[43], cospi[32], bf0[52], bit);
    bf1[53] = d_half_btf(cospi[32], bf0[42], cospi[32], bf0[53], bit);
    bf1[54] = d_half_btf(cospi[32], bf0[41], cospi[32], bf0[54], bit);
    bf1[55] = d_half_btf(cospi[32], bf0[40], cospi[32], bf0[55], bit);
    bf1[56] = bf0[56];
    bf1[57] = bf0[57];
    bf1[58] = bf0[58];
    bf1[59] = bf0[59];
    bf1[60] = bf0[60];
    bf1[61] = bf0[61];
    bf1[62] = bf0[62];
    bf1[63] = bf0[63];
    //range_check_buf(stage, input, bf1, size, cr);

    // stage 11
    stage++;
    bf0     = step;
    bf1     = output;
    bf1[0]  = d_cv(bf0[0] + bf0[63], cr);
    bf1[1]  = d_cv(bf0[1] + bf0[62], cr);
    bf1[2]  = d_cv(bf0[2] + bf0[61], cr);
    bf1[3]  = d_cv(bf0[3] + bf0[60], cr);
    bf1[4]  = d_cv(bf0[4] + bf0[59], cr);
    bf1[5]  = d_cv(bf0[5] + bf0[58], cr);
    bf1[6]  = d_cv(bf0[6] + bf0[57], cr);
    bf1[7]  = d_cv(bf0[7] + bf0[56], cr);
    bf1[8]  = d_cv(bf0[8] + bf0[55], cr);
    bf1[9]  = d_cv(bf0[9] + bf0[54], cr);
    bf1[10] = d_cv(bf0[10] + bf0[53], cr);
    bf1[11] = d_cv(bf0[11] + bf0[52], cr);
    bf1[12] = d_cv(bf0[12] + bf0[51], cr);
    bf1[13] = d_cv(bf0[13] + bf0[50], cr);
    bf1[14] = d_cv(bf0[14] + bf0[49], cr);
    bf1[15] = d_cv(bf0[15] + bf0[48], cr);
    bf1[16] = d_cv(bf0[16] + bf0[47], cr);
    bf1[17] = d_cv(bf0[17] + bf0[46], cr);
    bf1[18] = d_cv(bf0[18] + bf0[45], cr);
    bf1[19] = d_cv(bf0[19] + bf0[44], cr);
    bf1[20] = d_cv(bf0[20] + bf0[43], cr);
    bf1[21] = d_cv(bf0[21] + bf0[42], cr);
    bf1[22] = d_cv(bf0[22] + bf0[41], cr);
    bf1[23] = d_cv(bf0[23] + bf0[40], cr);
    bf1[24] = d_cv(bf0[24] + bf0[39], cr);
    bf1[25] = d_cv(bf0[25] + bf0[38], cr);
    bf1[26] = d_cv(bf0[26] + bf0[37], cr);
    bf1[27] = d_cv(bf0[27] + bf0[36], cr);
    bf1[28] = d_cv(bf0[28] + bf0[35], cr);
    bf1[29] = d_cv(bf0[29] + bf0[34], cr);
    bf1[30] = d_cv(bf0[30] + bf0[33], cr);
    bf1[31] = d_cv(bf0[31] + bf0[32], cr);
    bf1[32] = d_cv(bf0[31] - bf0[32], cr);
    bf1[33] = d_cv(bf0[30] - bf0[33], cr);
    bf1[34] = d_cv(bf0[29] - bf0[34], cr);
    bf1[35] = d_cv(bf0[28] - bf0[35], cr);
    bf1[36] = d_cv(bf0[27] - bf0[36], cr);
    bf1[37] = d_cv(bf0[26] - bf0[37], cr);
    bf1[38] = d_cv(bf0[25] - bf0[38], cr);
    bf1[39] = d_cv(bf0[24] - bf0[39], cr);
    bf1[40] = d_cv(bf0[23] - bf0[40], cr);
    bf1[41] = d_cv(bf0[22] - bf0[41], cr);
    bf1[42] = d_cv(bf0[21] - bf0[42], cr);
    bf1[43] = d_cv(bf0[20] - bf0[43], cr);
    bf1[44] = d_cv(bf0[19] - bf0[44], cr);
    bf1[45] = d_cv(bf0[18] - bf0[45], cr);
    bf1[46] = d_cv(bf0[17] - bf0[46], cr);
    bf1[47] = d_cv(bf0[16] - bf0[47], cr);
    bf1[48] = d_cv(bf0[15] - bf0[48], cr);
    bf1[49] = d_cv(bf0[14] - bf0[49], cr);
    bf1[50] = d_cv(bf0[13] - bf0[50], cr);
    bf1[51] = d_cv(bf0[12] - bf0[51], cr);
    bf1[52] = d_cv(bf0[11] - bf0[52], cr);
    bf1[53] = d_cv(bf0[10] - bf0[53], cr);
    bf1[54] = d_cv(bf0[9] - bf0[54], cr);
    bf1[55] = d_cv(bf0[8] - bf0[55], cr);
    bf1[56] = d_cv(bf0[7] - bf0[56], cr);
    bf1[57] = d_cv(bf0[6] - bf0[57], cr);
    bf1[58] = d_cv(bf0[5] - bf0[58], cr);
    bf1[59] = d_cv(bf0[4] - bf0[59], cr);
    bf1[60] = d_cv(bf0[3] - bf0[60], cr);
    bf1[61] = d_cv(bf0[2] - bf0[61], cr);
    bf1[62] = d_cv(bf0[1] - bf0[62], cr);
    bf1[63] = d_cv(bf0[0] - bf0[63], cr);
}
)CUDB5";
    s += R"CUDB6(
// svt_av1_inv_txfm2d_add_c at TX_64X64 (L9): inv_shift_64x64 = {-2,-4}
// (inv_transforms.c:22), cos_bit 12/12, clamp bit 16 at both 1D inputs;
// rounding >>2 after the row 1D and >>4 at the final add. DCT-ONLY.
// 64 threads, thread t = row in the row pass then column in the col pass.
extern "C" __global__ void inv_txfm_2d_add_64x64(const int* coeffs, const int* txType,
                                                 unsigned char* dst, const int* stride) {
    (void)txType;  // DCT-ONLY at TX_64X64
    __shared__ int sbuf[4096];
    const int t = threadIdx.x;
    int tmp[64];
    int o[64];
    for (int c = 0; c < 64; ++c) {
        tmp[c] = d_cv(coeffs[t * 64 + c], 16);
    }
    d_idct64i(tmp, o);
    for (int c = 0; c < 64; ++c) {
        sbuf[t * 64 + c] = d_round_shift(o[c], 2);  // -shift[0] = 2
    }
    __syncthreads();
    for (int r = 0; r < 64; ++r) {
        tmp[r] = d_cv(sbuf[r * 64 + t], 16);
    }
    d_idct64i(tmp, o);
    for (int r = 0; r < 64; ++r) {
        int v = (int)dst[r * (*stride) + t] + d_round_shift(o[r], 4);  // -shift[1] = 4
        if (v < 0) v = 0;
        else if (v > 255) v = 255;
        dst[r * (*stride) + t] = (unsigned char)v;
    }
})CUDB6";
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

// TX_64X64 entries (L9): same helpers at n_coeffs=4096, log_scale 2
// (av1_get_tx_scale_tab[TX_64X64] = 2, full_loop.c:22)
void quantizeFp64x64(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                     std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    quantizeFpN(coeff, tables, scan, 4096, 2, qcoeff, dqcoeff, eob);
}

void quantizeB64x64(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                    std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    quantizeBN(coeff, tables, scan, 4096, 2, qcoeff, dqcoeff, eob);
}

// FS4: the TX_64X64 EMISSION-domain quantize - n_coeffs=1024 (the compacted
// 32-wide quadrant; full_loop.c:1262 / inv_transforms.h:129-137), log_scale 2.
void quantizeFp64x64Token(const std::int32_t* coeff, const QuantTables& tables,
                          const std::int16_t* scan, std::int32_t* qcoeff, std::int32_t* dqcoeff,
                          std::uint16_t* eob) {
    quantizeFpN(coeff, tables, scan, 1024, 2, qcoeff, dqcoeff, eob);
}

// default (up-right diagonal) scan for 64x64, svt_aom_init_iscan formula
// (coefficients.c:345-363) at W=H=64
void defaultScan64x64(std::int16_t scan[4096]) {
    const int W = 64, H = 64;
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

// TX_64X64 entry (L9): same fp helper at n_coeffs=4096, log_scale 2
// (av1_get_tx_scale_tab[TX_64X64] = 2, full_loop.c:22) - escalated arithmetic:
// rounding = ROUND_POWER_OF_TWO(roundFp, 2), threshold << 3, quant >> 14
// (16 - log_scale), dq >> 2; 4096 coeffs / 1024 threads = 4 scan positions
// per thread (t, t+1024, t+2048, t+3072); eobPos[1024] shared (4096 B).
extern "C" __global__ void quant_dequant_64x64(const int* coeff, const short* quantFp,
                                               const short* dequant, const short* roundFp,
                                               const short* scan, int* qcoeff, int* dqcoeff,
                                               unsigned short* eob) {
    __shared__ int eobPos[1024];
    const int t = threadIdx.x;
    int bestPos = -1;
    for (int k = 0; k < 4; ++k) {
        const int pos = t + 1024 * k;
        const int rc = scan[pos];
        qcoeff[rc] = 0;
        dqcoeff[rc] = 0;
        const int rounding = (roundFp[rc != 0] + 3) >> 2;  // ROUND_POWER_OF_TWO(round, 2)
        const int thresh = dequant[rc != 0];
        const int coeffVal = coeff[rc];
        const int coeffSign = coeffVal < 0 ? -1 : 0;
        int absCoeff = (coeffVal ^ coeffSign) - coeffSign;
        int tmp32 = 0;
        if ((absCoeff << 3) >= thresh) {
            long long clamped = absCoeff + rounding;
            if (clamped < -32768) clamped = -32768;
            if (clamped > 32767) clamped = 32767;
            absCoeff = (int)clamped;
            tmp32 = (int)((absCoeff * quantFp[rc != 0]) >> 14);
            if (tmp32) {
                qcoeff[rc] = (tmp32 ^ coeffSign) - coeffSign;
                const int absDq = (int)(((long long)tmp32 * dequant[rc != 0]) >> 2);
                dqcoeff[rc] = (absDq ^ coeffSign) - coeffSign;
            }
        }
        if (tmp32 && pos > bestPos) bestPos = pos;
    }
    eobPos[t] = bestPos;
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

