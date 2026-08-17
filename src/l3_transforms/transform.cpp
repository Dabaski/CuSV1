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

namespace {

using InvTxfmFn = void (*)(const std::int32_t*, std::int32_t*);

InvTxfmFn inv1d4(TxType type) {
    return type == TxType::DCT_DCT ? idct4 : iadst4;
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

// svt_av1_inv_txfm2d_add_4x4_c / inv_txfm2d_add_c, TX_4X4: rows then columns,
// inv_shift_4x4 = {0, -4}, cos_bit 12/12, no flips, clamp bit 16; add via
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
)CUDA";
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
)CUDA";
}

}  // namespace transforms

