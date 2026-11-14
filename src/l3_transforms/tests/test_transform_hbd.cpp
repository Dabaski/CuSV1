// TXH1: the A1 highbd transform twin surfaces (inv side, G1-G6).
// RED cycle 1: staged before any transform_hbd implementation exists; the
// judge-side build of av1_l3_transforms_tests must fail to link/compile.
//
// Test-side transcription oracle (the RE2 house pattern): the oracle below is
// transcribed from the pinned reference text, NOT from the port:
//   third_party/SVT-AV1/Source/Lib/Codec/inv_transforms.c
//     clamp_value :88-93, idct4_new :97-134, idct8_new :136-213,
//     iadst4_new :729-814, iadst8_new :822-925, gen_inv_stage_range :44-86,
//     round_shift_array_c :2449-2464, inv_txfm2d_add_c :2496-2579,
//     check_range :2427-2440, highbd_clip_pixel_add :2444-2447
//   third_party/SVT-AV1/Source/Lib/Codec/definitions.h
//     clamp64 :691-693, clip_pixel_highbd :704-713
// PAIRED-METRIC CAVEAT (RC1/AN14 class): a transcription oracle shares the
// transcription-error class with the port. Mitigations: (1) the bd=8
// derivation fixtures compare the hbd twin against the LANDED 8-bit surface
// that the GATE already validates against SVT C; (2) single-step arithmetic
// absolutes (check_range/clip bounds, gen_inv_stage_range opt values) are
// formula-pinned; (3) the named mutations below are each killed by a specific
// fixture.
// NAMED LIMITATION: the 16x16/32x32/64x64/rect shapes' bd-10/12 CLAMP-FIRING
// paths are covered by derivation + table absolutes + mutations only (no
// independent decoder tree is available in this worktree to golden them).

#include <doctest.h>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>
#include <vector>
#include <transform.h>
#include <transform_hbd.h>

namespace {

// ---------------------------------------------------------------------------
// oracle: independent transcription of the reference pieces the fixtures need
// ---------------------------------------------------------------------------
namespace oracle {

const std::int32_t kCospi12[64] = {
    4096, 4095, 4091, 4085, 4076, 4065, 4052, 4036, 4017, 3996, 3973, 3948, 3920, 3889, 3857, 3822,
    3784, 3745, 3703, 3659, 3612, 3564, 3513, 3461, 3406, 3349, 3290, 3229, 3166, 3102, 3035, 2967,
    2896, 2824, 2751, 2675, 2598, 2520, 2440, 2359, 2276, 2191, 2106, 2019, 1931, 1842, 1751, 1660,
    1567, 1474, 1380, 1285, 1189, 1092, 995,  897,  799,  700,  601,  501,  401,  301,  201,  101};

const std::int32_t kSinpi12[5] = {0, 1321, 2482, 3344, 3803};

// inv_start_range (inv_transforms.h:217-237)
const std::int8_t kInvStartRange[19] = {5, 6, 7, 7, 7, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7};

std::int64_t clamp64(std::int64_t value, std::int64_t low, std::int64_t high) {
    return value < low ? low : (value > high ? high : value);
}

// clamp_value (inv_transforms.c:88-93)
std::int32_t clampValue(std::int32_t value, std::int8_t bit) {
    if (bit <= 0) {
        return value;
    }
    const std::int64_t maxValue = (1LL << (bit - 1)) - 1;
    const std::int64_t minValue = -(1LL << (bit - 1));
    return static_cast<std::int32_t>(clamp64(value, minValue, maxValue));
}

void clampBuf(std::int32_t* buf, int size, std::int8_t bit) {
    for (int i = 0; i < size; ++i) {
        buf[i] = clampValue(buf[i], bit);
    }
}

// clip_pixel_highbd (definitions.h:704-713)
std::uint16_t clipPixelHighbd(int val, int bd) {
    switch (bd) {
    case 10: return static_cast<std::uint16_t>(val < 0 ? 0 : (val > 1023 ? 1023 : val));
    case 12: return static_cast<std::uint16_t>(val < 0 ? 0 : (val > 4095 ? 4095 : val));
    case 8:
    default: return static_cast<std::uint16_t>(val < 0 ? 0 : (val > 255 ? 255 : val));
    }
}

// check_range (inv_transforms.c:2427-2440)
std::int64_t checkRange(std::int64_t input, int bd) {
    const std::int32_t intMax = (1 << (7 + bd)) - 1 + (914 << (bd - 7));
    const std::int32_t intMin = -intMax - 1;
    return clamp64(input, intMin, intMax);
}

// highbd_clip_pixel_add (inv_transforms.c:2444-2447)
std::uint16_t highbdClipPixelAdd(std::uint16_t dest, std::int64_t trans, int bd) {
    trans = checkRange(trans, bd);
    return clipPixelHighbd(static_cast<int>(dest + static_cast<std::int32_t>(trans)), bd);
}

// round_shift (inv_transforms.h:265-268) / round_shift_array_c (:2449-2464)
std::int32_t roundShift(std::int64_t value, int bit) {
    return static_cast<std::int32_t>((value + (1LL << (bit - 1))) >> bit);
}

void roundShiftArray(std::int32_t* arr, int size, int bit) {
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

std::int32_t halfBtf(std::int32_t w0, std::int32_t in0, std::int32_t w1, std::int32_t in1, int bit) {
    const std::int64_t result64 = static_cast<std::int64_t>(w0 * in0) + static_cast<std::int64_t>(w1 * in1);
    const std::int64_t intermediate = result64 + (1LL << (bit - 1));
    return static_cast<std::int32_t>(intermediate >> bit);
}

// svt_av1_idct4_new (inv_transforms.c:97-134)
void idct4(const std::int32_t input[4], std::int32_t output[4], const std::int8_t* stageRange) {
    const std::int32_t* cospi = kCospi12;
    std::int32_t bf0[4];
    std::int32_t bf1[4];

    bf1[0] = input[0];
    bf1[1] = input[2];
    bf1[2] = input[1];
    bf1[3] = input[3];

    for (int i = 0; i < 4; ++i) bf0[i] = bf1[i];
    bf1[0] = halfBtf(cospi[32], bf0[0], cospi[32], bf0[1], 12);
    bf1[1] = halfBtf(cospi[32], bf0[0], -cospi[32], bf0[1], 12);
    bf1[2] = halfBtf(cospi[48], bf0[2], -cospi[16], bf0[3], 12);
    bf1[3] = halfBtf(cospi[16], bf0[2], cospi[48], bf0[3], 12);

    output[0] = clampValue(bf1[0] + bf1[3], stageRange[3]);
    output[1] = clampValue(bf1[1] + bf1[2], stageRange[3]);
    output[2] = clampValue(bf1[1] - bf1[2], stageRange[3]);
    output[3] = clampValue(bf1[0] - bf1[3], stageRange[3]);
}

// svt_av1_iadst4_new (inv_transforms.c:729-814); stage_range unused
void iadst4(const std::int32_t input[4], std::int32_t output[4], const std::int8_t* stageRange) {
    (void)stageRange;
    const std::int32_t* sinpi = kSinpi12;
    std::int32_t s0, s1, s2, s3, s4, s5, s6, s7;

    std::int32_t x0 = input[0];
    std::int32_t x1 = input[1];
    std::int32_t x2 = input[2];
    std::int32_t x3 = input[3];

    if (!(x0 | x1 | x2 | x3)) {
        output[0] = output[1] = output[2] = output[3] = 0;
        return;
    }

    s0 = sinpi[1] * x0;
    s1 = sinpi[2] * x0;
    s2 = sinpi[3] * x1;
    s3 = sinpi[4] * x2;
    s4 = sinpi[1] * x2;
    s5 = sinpi[2] * x3;
    s6 = sinpi[4] * x3;

    s7 = (x0 - x2) + x3;

    s0 = s0 + s3;
    s1 = s1 - s4;
    s3 = s2;
    s2 = sinpi[3] * s7;

    s0 = s0 + s5;
    s1 = s1 - s6;

    x0 = s0 + s3;
    x1 = s1 + s3;
    x2 = s2;
    x3 = s0 + s1;

    x3 = x3 - s3;

    output[0] = roundShift(x0, 12);
    output[1] = roundShift(x1, 12);
    output[2] = roundShift(x2, 12);
    output[3] = roundShift(x3, 12);
}

// svt_av1_idct8_new (inv_transforms.c:136-213). The reference swaps bf0/bf1
// between stages (bf0=step reads the previous bf1=step result); the oracle
// carries one explicit buffer per stage generation
void idct8(const std::int32_t input[8], std::int32_t output[8], const std::int8_t* stageRange) {
    const std::int32_t* cospi = kCospi12;
    std::int32_t a[8];  // stage 1
    std::int32_t b[8];  // stage 2
    std::int32_t c[8];  // stage 3
    std::int32_t d[8];  // stage 4

    // stage 1
    a[0] = input[0];
    a[1] = input[4];
    a[2] = input[2];
    a[3] = input[6];
    a[4] = input[1];
    a[5] = input[5];
    a[6] = input[3];
    a[7] = input[7];

    // stage 2
    b[0] = a[0];
    b[1] = a[1];
    b[2] = a[2];
    b[3] = a[3];
    b[4] = halfBtf(cospi[56], a[4], -cospi[8], a[7], 12);
    b[5] = halfBtf(cospi[24], a[5], -cospi[40], a[6], 12);
    b[6] = halfBtf(cospi[40], a[5], cospi[24], a[6], 12);
    b[7] = halfBtf(cospi[8], a[4], cospi[56], a[7], 12);

    // stage 3
    c[0] = halfBtf(cospi[32], b[0], cospi[32], b[1], 12);
    c[1] = halfBtf(cospi[32], b[0], -cospi[32], b[1], 12);
    c[2] = halfBtf(cospi[48], b[2], -cospi[16], b[3], 12);
    c[3] = halfBtf(cospi[16], b[2], cospi[48], b[3], 12);
    c[4] = clampValue(b[4] + b[5], stageRange[3]);
    c[5] = clampValue(b[4] - b[5], stageRange[3]);
    c[6] = clampValue(-b[6] + b[7], stageRange[3]);
    c[7] = clampValue(b[6] + b[7], stageRange[3]);

    // stage 4
    d[0] = clampValue(c[0] + c[3], stageRange[4]);
    d[1] = clampValue(c[1] + c[2], stageRange[4]);
    d[2] = clampValue(c[1] - c[2], stageRange[4]);
    d[3] = clampValue(c[0] - c[3], stageRange[4]);
    d[4] = c[4];
    d[5] = halfBtf(-cospi[32], c[5], cospi[32], c[6], 12);
    d[6] = halfBtf(cospi[32], c[5], cospi[32], c[6], 12);
    d[7] = c[7];

    // stage 5
    output[0] = clampValue(d[0] + d[7], stageRange[5]);
    output[1] = clampValue(d[1] + d[6], stageRange[5]);
    output[2] = clampValue(d[2] + d[5], stageRange[5]);
    output[3] = clampValue(d[3] + d[4], stageRange[5]);
    output[4] = clampValue(d[3] - d[4], stageRange[5]);
    output[5] = clampValue(d[2] - d[5], stageRange[5]);
    output[6] = clampValue(d[1] - d[6], stageRange[5]);
    output[7] = clampValue(d[0] - d[7], stageRange[5]);
}

// svt_av1_iadst8_new (inv_transforms.c:822-925); one explicit buffer per
// stage generation (same pointer-swap discipline as idct8 above)
void iadst8(const std::int32_t input[8], std::int32_t output[8], const std::int8_t* stageRange) {
    const std::int32_t* cospi = kCospi12;
    std::int32_t a[8];  // stage 1
    std::int32_t b[8];  // stage 2
    std::int32_t c[8];  // stage 3
    std::int32_t d[8];  // stage 4
    std::int32_t e[8];  // stage 5
    std::int32_t f[8];  // stage 6

    // stage 1
    a[0] = input[7];
    a[1] = input[0];
    a[2] = input[5];
    a[3] = input[2];
    a[4] = input[3];
    a[5] = input[4];
    a[6] = input[1];
    a[7] = input[6];

    // stage 2
    b[0] = halfBtf(cospi[4], a[0], cospi[60], a[1], 12);
    b[1] = halfBtf(cospi[60], a[0], -cospi[4], a[1], 12);
    b[2] = halfBtf(cospi[20], a[2], cospi[44], a[3], 12);
    b[3] = halfBtf(cospi[44], a[2], -cospi[20], a[3], 12);
    b[4] = halfBtf(cospi[36], a[4], cospi[28], a[5], 12);
    b[5] = halfBtf(cospi[28], a[4], -cospi[36], a[5], 12);
    b[6] = halfBtf(cospi[52], a[6], cospi[12], a[7], 12);
    b[7] = halfBtf(cospi[12], a[6], -cospi[52], a[7], 12);

    // stage 3
    c[0] = clampValue(b[0] + b[4], stageRange[3]);
    c[1] = clampValue(b[1] + b[5], stageRange[3]);
    c[2] = clampValue(b[2] + b[6], stageRange[3]);
    c[3] = clampValue(b[3] + b[7], stageRange[3]);
    c[4] = clampValue(b[0] - b[4], stageRange[3]);
    c[5] = clampValue(b[1] - b[5], stageRange[3]);
    c[6] = clampValue(b[2] - b[6], stageRange[3]);
    c[7] = clampValue(b[3] - b[7], stageRange[3]);

    // stage 4
    d[0] = c[0];
    d[1] = c[1];
    d[2] = c[2];
    d[3] = c[3];
    d[4] = halfBtf(cospi[16], c[4], cospi[48], c[5], 12);
    d[5] = halfBtf(cospi[48], c[4], -cospi[16], c[5], 12);
    d[6] = halfBtf(-cospi[48], c[6], cospi[16], c[7], 12);
    d[7] = halfBtf(cospi[16], c[6], cospi[48], c[7], 12);

    // stage 5
    e[0] = clampValue(d[0] + d[2], stageRange[5]);
    e[1] = clampValue(d[1] + d[3], stageRange[5]);
    e[2] = clampValue(d[0] - d[2], stageRange[5]);
    e[3] = clampValue(d[1] - d[3], stageRange[5]);
    e[4] = clampValue(d[4] + d[6], stageRange[5]);
    e[5] = clampValue(d[5] + d[7], stageRange[5]);
    e[6] = clampValue(d[4] - d[6], stageRange[5]);
    e[7] = clampValue(d[5] - d[7], stageRange[5]);

    // stage 6 (the stage counter is intentionally not advanced in SVT)
    f[0] = e[0];
    f[1] = e[1];
    f[2] = halfBtf(cospi[32], e[2], cospi[32], e[3], 12);
    f[3] = halfBtf(cospi[32], e[2], -cospi[32], e[3], 12);
    f[4] = e[4];
    f[5] = e[5];
    f[6] = halfBtf(cospi[32], e[6], cospi[32], e[7], 12);
    f[7] = halfBtf(cospi[32], e[6], -cospi[32], e[7], 12);

    // stage 7
    output[0] = f[0];
    output[1] = -f[4];
    output[2] = f[6];
    output[3] = -f[2];
    output[4] = f[3];
    output[5] = -f[7];
    output[6] = f[5];
    output[7] = -f[1];
}

// minimal cfg for the square fixture shapes (a transcription of the fields
// inv_txfm2d_add_c consumes; the fixture sizes are square so rect_type is 0
// and the new_inv_sqrt2 pre-multiply is dead - NAMED oracle scope)
struct OCfg {
    int w;
    int h;
    const std::int8_t* shift;
    int stageNumRow;
    int stageNumCol;
    void (*rowFn)(const std::int32_t*, std::int32_t*, const std::int8_t*);
    void (*colFn)(const std::int32_t*, std::int32_t*, const std::int8_t*);
};

// gen_inv_stage_range (inv_transforms.c:44-86); the ADST4 stage-1 branch
// writes the same opt value as the else branch, so the oracle keeps the flat
// writes
void genInvStageRange(std::int8_t* col, std::int8_t* row, const OCfg& cfg, int bd) {
    std::int8_t optRangeRow, optRangeCol;
    if (bd == 8) {
        optRangeRow = 16;
        optRangeCol = 16;
    } else if (bd == 10) {
        optRangeRow = 18;
        optRangeCol = 16;
    } else {
        optRangeRow = 20;
        optRangeCol = 18;
    }
    (void)cfg;
    for (int i = 0; i < 12; ++i) {
        row[i] = optRangeRow;
        col[i] = optRangeCol;
    }
}

// inv_txfm2d_add_c (inv_transforms.c:2496-2579) at the square fixture sizes
// with ud_flip = lr_flip = 0 and rect_type = 0
void invTxfm2dAdd(const std::int32_t* input, const std::uint16_t* outR, std::uint32_t strideR,
                  std::uint16_t* outW, std::uint32_t strideW, const OCfg& cfg, int bd) {
    std::int8_t stageRangeCol[12] = {0};
    std::int8_t stageRangeRow[12] = {0};
    genInvStageRange(stageRangeCol, stageRangeRow, cfg, bd);

    const int bufOffset = cfg.w > cfg.h ? cfg.w : cfg.h;
    std::int32_t txfmBuf[64 * 64 + 2 * 64];
    std::int32_t* tempIn = txfmBuf;
    std::int32_t* tempOut = tempIn + bufOffset;
    std::int32_t* buf = tempOut + bufOffset;

    for (int r = 0; r < cfg.h; ++r) {
        for (int c = 0; c < cfg.w; ++c) {
            tempIn[c] = input[c];
        }
        clampBuf(tempIn, cfg.w, static_cast<std::int8_t>(bd + 8));
        cfg.rowFn(tempIn, buf + r * cfg.w, stageRangeRow);
        roundShiftArray(buf + r * cfg.w, cfg.w, -cfg.shift[0]);
        input += cfg.w;
    }

    for (int c = 0; c < cfg.w; ++c) {
        for (int r = 0; r < cfg.h; ++r) {
            tempIn[r] = buf[r * cfg.w + c];
        }
        clampBuf(tempIn, cfg.h, static_cast<std::int8_t>(bd + 6 > 16 ? bd + 6 : 16));
        cfg.colFn(tempIn, tempOut, stageRangeCol);
        roundShiftArray(tempOut, cfg.h, -cfg.shift[1]);
        for (int r = 0; r < cfg.h; ++r) {
            outW[r * strideW + c] =
                highbdClipPixelAdd(outR[r * strideR + c], tempOut[r], bd);
        }
    }
}

// svt_av1_highbd_iwht4x4_16_add_c (inv_transforms.c:2783-2842); the
// range_check_value calls are dead (DO_RANGE_CHECK_CLAMP undefined) and are
// dropped, named
void iwht4x4_16Add(const std::int32_t* input, std::uint16_t* destR, std::int32_t strideR,
                   std::uint16_t* destW, std::int32_t strideW, int bd) {
    std::int32_t output[16];
    std::int32_t a1, b1, c1, d1, e1;
    const std::int32_t* ip = input;
    std::int32_t* op = output;

    for (int i = 0; i < 4; i++) {
        a1 = ip[0] >> 2;
        c1 = ip[1] >> 2;
        d1 = ip[2] >> 2;
        b1 = ip[3] >> 2;
        a1 += c1;
        d1 -= b1;
        e1 = (a1 - d1) >> 1;
        b1 = e1 - b1;
        c1 = e1 - c1;
        a1 -= b1;
        d1 += c1;
        op[0] = a1;
        op[1] = b1;
        op[2] = c1;
        op[3] = d1;
        ip += 4;
        op += 4;
    }

    ip = output;
    for (int i = 0; i < 4; i++) {
        a1 = ip[4 * 0];
        c1 = ip[4 * 1];
        d1 = ip[4 * 2];
        b1 = ip[4 * 3];
        a1 += c1;
        d1 -= b1;
        e1 = (a1 - d1) >> 1;
        b1 = e1 - b1;
        c1 = e1 - c1;
        a1 -= b1;
        d1 += c1;

        destW[strideW * 0] = highbdClipPixelAdd(destR[strideR * 0], a1, bd);
        destW[strideW * 1] = highbdClipPixelAdd(destR[strideR * 1], b1, bd);
        destW[strideW * 2] = highbdClipPixelAdd(destR[strideR * 2], c1, bd);
        destW[strideW * 3] = highbdClipPixelAdd(destR[strideR * 3], d1, bd);

        ip++;
        destR++;
        destW++;
    }
}

// svt_av1_highbd_iwht4x4_1_add_c (inv_transforms.c:2844-2873)
void iwht4x4_1Add(const std::int32_t* in, std::uint16_t* destR, std::int32_t destStrideR,
                  std::uint16_t* destW, std::int32_t destStrideW, int bd) {
    std::int32_t a1, e1;
    std::int32_t tmp[4];
    const std::int32_t* ip = in;
    std::int32_t* op = tmp;

    a1 = ip[0] >> 2;
    e1 = a1 >> 1;
    a1 -= e1;
    op[0] = a1;
    op[1] = op[2] = op[3] = e1;

    ip = tmp;
    for (int i = 0; i < 4; i++) {
        e1 = ip[0] >> 1;
        a1 = ip[0] - e1;
        destW[destStrideW * 0] = highbdClipPixelAdd(destR[destStrideR * 0], a1, bd);
        destW[destStrideW * 1] = highbdClipPixelAdd(destR[destStrideR * 1], e1, bd);
        destW[destStrideW * 2] = highbdClipPixelAdd(destR[destStrideR * 2], e1, bd);
        destW[destStrideW * 3] = highbdClipPixelAdd(destR[destStrideR * 3], e1, bd);
        ip++;
        destR++;
        destW++;
    }
}

}  // namespace oracle

// ---------------------------------------------------------------------------
// fixture helpers
// ---------------------------------------------------------------------------

// fixed adversarial coefficients: the bd-10 / bd-12 check_range maxima
// (check_range comment, inv_transforms.c:2428-2432)
constexpr std::int32_t kAdvBd10 = 138383;  // (1<<17)-1 + (914<<3)
constexpr std::int32_t kAdvBd12 = 553535;  // (1<<19)-1 + (914<<5)

transforms::TxSize kAllShapes[19] = {
    transforms::TxSize::TX_4X4,   transforms::TxSize::TX_8X8,   transforms::TxSize::TX_16X16,
    transforms::TxSize::TX_32X32, transforms::TxSize::TX_64X64, transforms::TxSize::TX_4X8,
    transforms::TxSize::TX_8X4,   transforms::TxSize::TX_8X16,  transforms::TxSize::TX_16X8,
    transforms::TxSize::TX_16X32, transforms::TxSize::TX_32X16, transforms::TxSize::TX_32X64,
    transforms::TxSize::TX_64X32, transforms::TxSize::TX_4X16,  transforms::TxSize::TX_16X4,
    transforms::TxSize::TX_8X32,  transforms::TxSize::TX_32X8,  transforms::TxSize::TX_16X64,
    transforms::TxSize::TX_64X16};

int shapeW(transforms::TxSize s) {
    return transforms::txSizeWide(s);
}

int shapeH(transforms::TxSize s) {
    return transforms::txSizeHigh(s);
}

// deterministic small spread: coeff[i] = (i*7 % 17) - 8, dest[i] = i % 151.
// The magnitudes keep dest + |trans| under 255 at every shape so the clip
// bound cannot differ across bd 8/10/12 in the invariance fixtures.
void fillSmall(std::int32_t* coeff, std::uint16_t* dst, int n) {
    for (int i = 0; i < n; ++i) {
        coeff[i] = static_cast<std::int32_t>((i * 7) % 17) - 8;
        dst[i] = static_cast<std::uint16_t>(i % 151);
    }
}

// DC impulse: coeff[0] = c, everything else 0. A single-basis projection
// keeps every intermediate and trans magnitude at or below round_shift of
// 1.414*c, so dest + trans stays under 255 for c <= 20 and dst <= 200 at
// every shape.
void fillDcImpulse(std::int32_t* coeff, std::uint16_t* dst, int n, std::int32_t c) {
    for (int i = 0; i < n; ++i) {
        coeff[i] = (i == 0) ? c : 0;
        dst[i] = static_cast<std::uint16_t>(i % 201);
    }
}

}  // namespace

// ===========================================================================
// G1: gen_inv_stage_range + the bd-clamp helper family
// ===========================================================================

TEST_CASE("genInvStageRangeHb bd=8 TX_8X8 DCT_DCT: flat 16 row, flat 16 col") {
    transforms::Txfm2dFlipCfg cfg;
    transforms::invTransformConfig(transforms::TxType::DCT_DCT, transforms::TxSize::TX_8X8, cfg);
    std::int8_t col[12] = {0};
    std::int8_t row[12] = {0};
    transforms::genInvStageRangeHb(col, row, cfg, transforms::TxSize::TX_8X8, 8);
    for (int i = 0; i < cfg.stageNumRow; ++i) {
        CHECK(row[i] == 16);
    }
    for (int i = 0; i < cfg.stageNumCol; ++i) {
        CHECK(col[i] == 16);
    }
}

TEST_CASE("genInvStageRangeHb bd=10 TX_8X8 DCT_DCT: flat 18 row, flat 16 col") {
    transforms::Txfm2dFlipCfg cfg;
    transforms::invTransformConfig(transforms::TxType::DCT_DCT, transforms::TxSize::TX_8X8, cfg);
    std::int8_t col[12] = {0};
    std::int8_t row[12] = {0};
    transforms::genInvStageRangeHb(col, row, cfg, transforms::TxSize::TX_8X8, 10);
    for (int i = 0; i < cfg.stageNumRow; ++i) {
        CHECK(row[i] == 18);
    }
    for (int i = 0; i < cfg.stageNumCol; ++i) {
        CHECK(col[i] == 16);
    }
}

TEST_CASE("genInvStageRangeHb bd=12 TX_8X8 DCT_DCT: flat 20 row, flat 18 col") {
    transforms::Txfm2dFlipCfg cfg;
    transforms::invTransformConfig(transforms::TxType::DCT_DCT, transforms::TxSize::TX_8X8, cfg);
    std::int8_t col[12] = {0};
    std::int8_t row[12] = {0};
    transforms::genInvStageRangeHb(col, row, cfg, transforms::TxSize::TX_8X8, 12);
    for (int i = 0; i < cfg.stageNumRow; ++i) {
        CHECK(row[i] == 20);
    }
    for (int i = 0; i < cfg.stageNumCol; ++i) {
        CHECK(col[i] == 18);
    }
}

TEST_CASE("genInvStageRangeHb bd=10 TX_4X4 ADST_ADST: stageNumRow 7, all 18") {
    transforms::Txfm2dFlipCfg cfg;
    transforms::invTransformConfig(transforms::TxType::ADST_ADST, transforms::TxSize::TX_4X4, cfg);
    // DCT4=4 DCT8=6 DCT16=8 DCT32=10 DCT64=12 ADST4=7 ADST8=8 ADST16=10
    // ADST32=12 IDENTITY*=1 (kAv1TxfmStageNumList, transform.cpp:3647)
    CHECK(cfg.stageNumRow == 7);
    std::int8_t col[12] = {0};
    std::int8_t row[12] = {0};
    transforms::genInvStageRangeHb(col, row, cfg, transforms::TxSize::TX_4X4, 10);
    for (int i = 0; i < 7; ++i) {
        // the ADST4 stage-1 exception branch writes opt_range_row like the
        // else branch (inv_transforms.c:64-71), so the written array is flat
        CHECK(row[i] == 18);
        CHECK(col[i] == 16);
    }
}

TEST_CASE("checkRangeHb bd=8 bound is 34595 and clamps both ways") {
    // int_max = (1<<15)-1 + (914<<1) = 32767 + 1828 = 34595;
    // int_min = -int_max - 1 = -34596 (inv_transforms.c:2433-2434): the
    // negative bound is one BEYOND the positive, so -34596 is in range
    CHECK(transforms::checkRangeHb(34595, 8) == 34595);
    CHECK(transforms::checkRangeHb(34596, 8) == 34595);
    CHECK(transforms::checkRangeHb(-34596, 8) == -34596);
    CHECK(transforms::checkRangeHb(-34597, 8) == -34596);
}

TEST_CASE("checkRangeHb bd=10 bound is 138383 and clamps both ways") {
    // int_max = (1<<17)-1 + (914<<3) = 131071 + 7312 = 138383;
    // int_min = -138384 (in range at the boundary)
    CHECK(transforms::checkRangeHb(138383, 10) == 138383);
    CHECK(transforms::checkRangeHb(138384, 10) == 138383);
    CHECK(transforms::checkRangeHb(-138384, 10) == -138384);
    CHECK(transforms::checkRangeHb(-138385, 10) == -138384);
}

TEST_CASE("checkRangeHb bd=12 bound is 553535 and clamps both ways") {
    // int_max = (1<<19)-1 + (914<<5) = 524287 + 29248 = 553535;
    // int_min = -553536 (in range at the boundary)
    CHECK(transforms::checkRangeHb(553535, 12) == 553535);
    CHECK(transforms::checkRangeHb(553536, 12) == 553535);
    CHECK(transforms::checkRangeHb(-553536, 12) == -553536);
    CHECK(transforms::checkRangeHb(-553537, 12) == -553536);
}

TEST_CASE("clipPixelHighbd clamps to 255/1023/4095 and 0") {
    CHECK(transforms::clipPixelHighbd(100, 8) == 100);
    CHECK(transforms::clipPixelHighbd(300, 8) == 255);
    CHECK(transforms::clipPixelHighbd(-1, 8) == 0);
    CHECK(transforms::clipPixelHighbd(1000, 10) == 1000);
    CHECK(transforms::clipPixelHighbd(1024, 10) == 1023);
    CHECK(transforms::clipPixelHighbd(-7, 10) == 0);
    CHECK(transforms::clipPixelHighbd(4096, 12) == 4095);
    CHECK(transforms::clipPixelHighbd(3000, 12) == 3000);
}

TEST_CASE("highbdClipPixelAdd wraps through check_range then clips") {
    // dest 100 + trans 600000 at bd 10: trans clamps to 138383, then
    // 100 + 138383 = 138483 clips to 1023
    CHECK(transforms::highbdClipPixelAdd(100, 600000, 10) == 1023);
    // dest 1000 + trans -2000 at bd 10 clips to 0
    CHECK(transforms::highbdClipPixelAdd(1000, -2000, 10) == 0);
    // dest 500 + trans 400 at bd 10 stays in range
    CHECK(transforms::highbdClipPixelAdd(500, 400, 10) == 900);
}

// ===========================================================================
// G2: the stage-range-parameterized 1D inverse kernels
// ===========================================================================

TEST_CASE("idct4Hb with the 16 shim equals landed idct4 (derivation)") {
    const std::int32_t in[4] = {100, 50, -20, 8};
    const std::int8_t shim[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    std::int32_t got[4] = {0};
    std::int32_t landed[4] = {0};
    transforms::idct4Hb(in, got, shim);
    transforms::idct4(in, landed);
    for (int i = 0; i < 4; ++i) {
        CHECK(got[i] == landed[i]);
    }
}

TEST_CASE("idct4Hb bd=10 clamp escape: stage-3 clamp at 18, landed 16 would bite") {
    // input {60000,0,0,0}: the stage-2 outputs are halfBtf(cospi12[32]=2896,
    // 60000, 2896, 0, 12) = 42423 for indices 0,1 (clamped at 16 by the landed
    // shim, unclamped at 18); the oracle decides the exact values
    const std::int32_t in[4] = {60000, 0, 0, 0};
    const std::int8_t row18[8] = {18, 18, 18, 18, 18, 18, 18, 18};
    std::int32_t got[4] = {0};
    std::int32_t want[4] = {0};
    transforms::idct4Hb(in, got, row18);
    oracle::idct4(in, want, row18);
    for (int i = 0; i < 4; ++i) {
        CHECK(got[i] == want[i]);
    }
    // the landed 16-shim clamps the stage-2 result at 32767: the hbd array
    // must differ (the fixture is discriminating)
    const std::int8_t shim[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    std::int32_t landed[4] = {0};
    transforms::idct4(in, landed);
    bool differs = false;
    for (int i = 0; i < 4; ++i) {
        if (got[i] != landed[i]) differs = true;
    }
    CHECK(differs);
}

TEST_CASE("idct8Hb with the 16 shim equals landed idct8 (derivation)") {
    const std::int32_t in[8] = {900, -450, 220, 80, -60, 30, -15, 7};
    const std::int8_t shim[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    std::int32_t got[8] = {0};
    std::int32_t landed[8] = {0};
    transforms::idct8Hb(in, got, shim);
    transforms::idct8(in, landed);
    for (int i = 0; i < 8; ++i) {
        CHECK(got[i] == landed[i]);
    }
}

TEST_CASE("idct8Hb bd=10 clamp escape matches the oracle") {
    const std::int32_t in[8] = {90000, -45000, 22000, 8000, -6000, 3000, -1500, 700};
    const std::int8_t row18[8] = {18, 18, 18, 18, 18, 18, 18, 18};
    std::int32_t got[8] = {0};
    std::int32_t want[8] = {0};
    transforms::idct8Hb(in, got, row18);
    oracle::idct8(in, want, row18);
    for (int i = 0; i < 8; ++i) {
        CHECK(got[i] == want[i]);
    }
}

TEST_CASE("iadst4Hb with the 16 shim equals landed iadst4 (derivation)") {
    const std::int32_t in[4] = {120, -40, 9, 3};
    const std::int8_t shim[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    std::int32_t got[4] = {0};
    std::int32_t landed[4] = {0};
    transforms::iadst4Hb(in, got, shim);
    transforms::iadst4(in, landed);
    for (int i = 0; i < 4; ++i) {
        CHECK(got[i] == landed[i]);
    }
}

TEST_CASE("iadst4Hb consumes no stage_range value (oracle equality at 18)") {
    const std::int32_t in[4] = {40000, -9000, 700, 300};
    const std::int8_t row18[8] = {18, 18, 18, 18, 18, 18, 18, 18};
    std::int32_t got[4] = {0};
    std::int32_t want[4] = {0};
    transforms::iadst4Hb(in, got, row18);
    oracle::iadst4(in, want, row18);
    for (int i = 0; i < 4; ++i) {
        CHECK(got[i] == want[i]);
    }
}

TEST_CASE("iadst8Hb with the 16 shim equals landed iadst8 (derivation)") {
    const std::int32_t in[8] = {-220, 90, -30, 15, -8, 4, -2, 1};
    const std::int8_t shim[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    std::int32_t got[8] = {0};
    std::int32_t landed[8] = {0};
    transforms::iadst8Hb(in, got, shim);
    transforms::iadst8(in, landed);
    for (int i = 0; i < 8; ++i) {
        CHECK(got[i] == landed[i]);
    }
}

TEST_CASE("iadst8Hb bd=10 clamp escape matches the oracle") {
    const std::int32_t in[8] = {80000, -30000, 12000, 5000, -2000, 900, -400, 200};
    const std::int8_t row18[8] = {18, 18, 18, 18, 18, 18, 18, 18};
    std::int32_t got[8] = {0};
    std::int32_t want[8] = {0};
    transforms::iadst8Hb(in, got, row18);
    oracle::iadst8(in, want, row18);
    for (int i = 0; i < 8; ++i) {
        CHECK(got[i] == want[i]);
    }
}

TEST_CASE("idct16Hb with the 16 shim equals landed idct16 (derivation)") {
    const std::int32_t in[16] = {500, -250, 120, -60, 30, -15, 8, -4,
                                 2,   -1,    7,   -3,  5,  -2,  1, 1};
    const std::int8_t shim[12] = {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
    std::int32_t got[16] = {0};
    std::int32_t landed[16] = {0};
    transforms::idct16Hb(in, got, shim);
    transforms::idct16(in, landed);
    for (int i = 0; i < 16; ++i) {
        CHECK(got[i] == landed[i]);
    }
}

TEST_CASE("idct16Hb bd=12 row array (20) equals the landed body on in-range data") {
    // no clamp fires for this input at either 16 or 20: the derivation holds
    // across the array swap
    const std::int32_t in[16] = {1000, -500, 250, -125, 60, -30, 15, -8, 4, -2, 1, 1, 2, -1, 1, -1};
    const std::int8_t row20[12] = {20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20};
    std::int32_t got[16] = {0};
    std::int32_t landed[16] = {0};
    transforms::idct16Hb(in, got, row20);
    transforms::idct16(in, landed);
    for (int i = 0; i < 16; ++i) {
        CHECK(got[i] == landed[i]);
    }
}

TEST_CASE("idct32Hb with the 16 shim equals landed idct32 (derivation)") {
    std::int32_t in[32];
    std::int32_t got[32] = {0};
    std::int32_t landed[32] = {0};
    for (int i = 0; i < 32; ++i) {
        in[i] = ((i * 13) % 97) - 48;
    }
    const std::int8_t shim[12] = {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
    transforms::idct32Hb(in, got, shim);
    transforms::idct32(in, landed);
    for (int i = 0; i < 32; ++i) {
        CHECK(got[i] == landed[i]);
    }
}

TEST_CASE("idct64Hb with the 16 shim equals landed idct64 (derivation)") {
    std::int32_t in[64];
    std::int32_t got[64] = {0};
    std::int32_t landed[64] = {0};
    for (int i = 0; i < 64; ++i) {
        in[i] = ((i * 29) % 211) - 105;
    }
    const std::int8_t shim[12] = {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
    transforms::idct64Hb(in, got, shim);
    transforms::idct64(in, landed);
    for (int i = 0; i < 64; ++i) {
        CHECK(got[i] == landed[i]);
    }
}

TEST_CASE("iadst16Hb with the 16 shim equals landed iadst16 (derivation)") {
    std::int32_t in[16];
    std::int32_t got[16] = {0};
    std::int32_t landed[16] = {0};
    for (int i = 0; i < 16; ++i) {
        in[i] = ((i * 11) % 83) - 41;
    }
    const std::int8_t shim[12] = {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
    transforms::iadst16Hb(in, got, shim);
    transforms::iadst16(in, landed);
    for (int i = 0; i < 16; ++i) {
        CHECK(got[i] == landed[i]);
    }
}

TEST_CASE("iadst32Hb with the 16 shim equals landed iadst32 (derivation)") {
    std::int32_t in[32];
    std::int32_t got[32] = {0};
    std::int32_t landed[32] = {0};
    for (int i = 0; i < 32; ++i) {
        in[i] = ((i * 17) % 127) - 63;
    }
    const std::int8_t shim[12] = {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
    transforms::iadst32Hb(in, got, shim);
    transforms::iadst32(in, landed);
    for (int i = 0; i < 32; ++i) {
        CHECK(got[i] == landed[i]);
    }
}

TEST_CASE("identityHb wrappers with the 16 shim equal the landed identities") {
    // the reference iidentity*_c ignore stage_range; the wrappers must too
    const std::int8_t shim[12] = {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
    {
        const std::int32_t in[4] = {7, -3, 2, -1};
        std::int32_t got[4] = {0};
        std::int32_t landed[4] = {0};
        transforms::invIdentity4Hb(in, got, shim);
        transforms::invIdentity4(in, landed, 0);
        for (int i = 0; i < 4; ++i) CHECK(got[i] == landed[i]);
    }
    {
        const std::int32_t in[8] = {7, -3, 2, -1, 5, -2, 1, 0};
        std::int32_t got[8] = {0};
        std::int32_t landed[8] = {0};
        transforms::invIdentity8Hb(in, got, shim);
        transforms::invIdentity8(in, landed, 0);
        for (int i = 0; i < 8; ++i) CHECK(got[i] == landed[i]);
    }
    {
        const std::int32_t in[16] = {7, -3, 2, -1, 5, -2, 1, 0, 3, -1, 1, 0, 2, -1, 1, 0};
        std::int32_t got[16] = {0};
        std::int32_t landed[16] = {0};
        transforms::invIdentity16Hb(in, got, shim);
        transforms::invIdentity16(in, landed, 0);
        for (int i = 0; i < 16; ++i) CHECK(got[i] == landed[i]);
    }
    {
        std::int32_t in[32];
        std::int32_t got[32] = {0};
        std::int32_t landed[32] = {0};
        for (int i = 0; i < 32; ++i) in[i] = i - 16;
        transforms::invIdentity32Hb(in, got, shim);
        transforms::invIdentity32(in, landed, 0);
        for (int i = 0; i < 32; ++i) CHECK(got[i] == landed[i]);
    }
    {
        std::int32_t in[64];
        std::int32_t got[64] = {0};
        std::int32_t landed[64] = {0};
        for (int i = 0; i < 64; ++i) in[i] = i - 32;
        transforms::invIdentity64Hb(in, got, shim);
        transforms::invIdentity64(in, landed, 0);
        for (int i = 0; i < 64; ++i) CHECK(got[i] == landed[i]);
    }
}

// ===========================================================================
// G3: the bd-parameterized 2D core (uint16 buffers)
// ===========================================================================

TEST_CASE("invTxfm2dAddCoreHb bd=8 derivation: 4x4 DCT_DCT uint16 equals landed uint8 widened") {
    std::int32_t coeffs[16];
    std::uint8_t dst8[4 * 4];
    std::uint16_t dst16[4 * 4];
    fillSmall(coeffs, dst16, 16);
    for (int i = 0; i < 16; ++i) dst8[i] = static_cast<std::uint8_t>(dst16[i]);
    transforms::invTxfm2dAdd4x4(coeffs, dst8, 4, transforms::TxType::DCT_DCT);
    transforms::invTxfm2dAddHb4x4(coeffs, dst16, 4, dst16, 4, transforms::TxType::DCT_DCT, 8);
    for (int i = 0; i < 16; ++i) {
        CHECK(dst16[i] == dst8[i]);
    }
}

TEST_CASE("invTxfm2dAddCoreHb bd=8 derivation: 8x8 ADST_ADST uint16 equals landed uint8 widened") {
    std::int32_t coeffs[64];
    std::uint8_t dst8[8 * 8];
    std::uint16_t dst16[8 * 8];
    fillSmall(coeffs, dst16, 64);
    for (int i = 0; i < 64; ++i) dst8[i] = static_cast<std::uint8_t>(dst16[i]);
    transforms::invTxfm2dAdd8x8(coeffs, dst8, 8, transforms::TxType::ADST_ADST);
    transforms::invTxfm2dAddHb8x8(coeffs, dst16, 8, dst16, 8, transforms::TxType::ADST_ADST, 8);
    for (int i = 0; i < 64; ++i) {
        CHECK(dst16[i] == dst8[i]);
    }
}

TEST_CASE("invTxfm2dAddCoreHb bd=8 derivation: r/w split equals in-place") {
    // the reference core reads outR and writes outW separately; with r == w it
    // must equal the landed in-place 8-bit result
    std::int32_t coeffs[64];
    std::uint8_t dst8[8 * 8];
    std::uint16_t inPlace[8 * 8];
    std::uint16_t split[8 * 8];
    fillSmall(coeffs, inPlace, 64);
    for (int i = 0; i < 64; ++i) {
        dst8[i] = static_cast<std::uint8_t>(inPlace[i]);
        split[i] = inPlace[i];
    }
    transforms::invTxfm2dAdd8x8(coeffs, dst8, 8, transforms::TxType::DCT_DCT);
    transforms::invTxfm2dAddHb8x8(coeffs, split, 8, split, 8, transforms::TxType::DCT_DCT, 8);
    for (int i = 0; i < 64; ++i) {
        CHECK(split[i] == dst8[i]);
    }
}

TEST_CASE("invTxfm2dAddHb 4x4 DCT_DCT bd=10 adversarial matches the oracle") {
    // input at the bd-10 check_range maximum: fires the row pre-clamp
    // (138383 > 131071 = 2^17-1), the col pre-clamp (16), and the final
    // 1023 clip; mutation M6 (row pre-clamp fixed 16) dies here.
    // FIXTURE FIX (TXH1 landing): the port is called with the reference's own
    // r/w-split signature (output_r/output_w, inv_transforms.c:2496) reading
    // dstR - the pre-fix call read the 0xEEEE sentinel as dest, a different
    // input than the oracle's dstR, so the comparison was apples-to-oranges
    // (and at 4x4 it passed only because both sides saturated at 1023).
    std::int32_t coeffs[16] = {0};
    std::uint16_t dstR[4 * 4];
    std::uint16_t gotW[4 * 4];
    std::uint16_t wantW[4 * 4];
    for (int i = 0; i < 16; ++i) {
        dstR[i] = static_cast<std::uint16_t>(i * 37 % 300);
        gotW[i] = 0xEEEE;
        wantW[i] = 0xEEEE;
    }
    coeffs[0] = kAdvBd10;
    transforms::invTxfm2dAddHb4x4(coeffs, dstR, 4, gotW, 4, transforms::TxType::DCT_DCT, 10);

    oracle::OCfg cfg4;
    cfg4.w = 4;
    cfg4.h = 4;
    static const std::int8_t shift44[2] = {0, -4};
    cfg4.shift = shift44;
    cfg4.stageNumRow = 4;
    cfg4.stageNumCol = 4;
    cfg4.rowFn = oracle::idct4;
    cfg4.colFn = oracle::idct4;
    oracle::invTxfm2dAdd(coeffs, dstR, 4, wantW, 4, cfg4, 10);
    for (int i = 0; i < 16; ++i) {
        CHECK(gotW[i] == wantW[i]);
    }
}

TEST_CASE("invTxfm2dAddHb 4x4 DCT_DCT bd=12 adversarial matches the oracle") {
    // fires the row pre-clamp (553535 > 524287), the row kernel clamp at 20,
    // the col pre-clamp at 18, the col kernel clamp at 18 and the 4095 clip
    std::int32_t coeffs[16] = {0};
    std::uint16_t dstR[4 * 4];
    std::uint16_t gotW[4 * 4];
    std::uint16_t wantW[4 * 4];
    for (int i = 0; i < 16; ++i) {
        dstR[i] = static_cast<std::uint16_t>(i * 53 % 900);
        gotW[i] = 0xEEEE;
        wantW[i] = 0xEEEE;
    }
    coeffs[0] = kAdvBd12;
    transforms::invTxfm2dAddHb4x4(coeffs, dstR, 4, gotW, 4, transforms::TxType::DCT_DCT, 12);

    oracle::OCfg cfg4;
    cfg4.w = 4;
    cfg4.h = 4;
    static const std::int8_t shift44[2] = {0, -4};
    cfg4.shift = shift44;
    cfg4.stageNumRow = 4;
    cfg4.stageNumCol = 4;
    cfg4.rowFn = oracle::idct4;
    cfg4.colFn = oracle::idct4;
    oracle::invTxfm2dAdd(coeffs, dstR, 4, wantW, 4, cfg4, 12);
    for (int i = 0; i < 16; ++i) {
        CHECK(gotW[i] == wantW[i]);
    }
}

TEST_CASE("diagnostic: idct8Hb@18 == oracle idct8 on the 8x8-fixture row input") {
    // the 8x8 DCT bd=10 fixture's row-pass input after the bd+8 pre-clamp:
    // {131071 x8}. This fixture pins the ROW KERNEL alone on that exact
    // input; a divergence here is a kernel divergence, not core plumbing.
    const std::int32_t in[8] = {131071, 131071, 131071, 131071, 131071, 131071, 131071, 131071};
    const std::int8_t row18[12] = {18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18};
    std::int32_t got[8] = {0};
    std::int32_t want[8] = {0};
    transforms::idct8Hb(in, got, row18);
    oracle::idct8(in, want, row18);
    for (int i = 0; i < 8; ++i) {
        CHECK(got[i] == want[i]);
    }
    // the row shift (-shift[0] = +1): both sides through the same divider
    for (int i = 0; i < 8; ++i) {
        got[i] = oracle::roundShift(got[i], 1);
        want[i] = oracle::roundShift(want[i], 1);
        CHECK(got[i] == want[i]);
    }
}

TEST_CASE("diagnostic: idct8Hb@16 == oracle idct8 on the fixture buf columns") {
    // the buf the row pass leaves is row-constant; each column is the same
    // value repeated 8x. Pin the COL KERNEL against the oracle per column,
    // with the same 16-bit pre-clamp and the (-shift[1]) = +4 divider.
    const std::int32_t in[8] = {131071, 131071, 131071, 131071, 131071, 131071, 131071, 131071};
    const std::int8_t row18[12] = {18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18};
    const std::int8_t col16[12] = {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
    std::int32_t rowOut[8] = {0};
    oracle::idct8(in, rowOut, row18);
    for (int i = 0; i < 8; ++i) {
        rowOut[i] = oracle::roundShift(rowOut[i], 1);
    }
    for (int c = 0; c < 8; ++c) {
        std::int32_t col[8];
        for (int r = 0; r < 8; ++r) {
            col[r] = rowOut[c];  // buf[r*8 + c] == rowOut[c] for every r
        }
        oracle::clampBuf(col, 8, 16);
        std::int32_t got[8] = {0};
        std::int32_t want[8] = {0};
        transforms::idct8Hb(col, got, col16);
        oracle::idct8(col, want, col16);
        for (int r = 0; r < 8; ++r) {
            CHECK(got[r] == want[r]);
            got[r] = oracle::roundShift(got[r], 4);
            want[r] = oracle::roundShift(want[r], 4);
            CHECK(got[r] == want[r]);
        }
    }
}

TEST_CASE("diagnostic: 8x8 DCT bd=10 with dstR=0 and split r/w reveals the clipped trans") {
    // with a zero read buffer the output IS clip(check_range(trans), 0, 1023):
    // each cell directly exposes one side's trans value. The r/w split also
    // removes any in-place artifact.
    std::int32_t coeffs[8 * 8];
    std::uint16_t zeroR[8 * 8];
    std::uint16_t gotW[8 * 8];
    std::uint16_t wantW[8 * 8];
    for (int i = 0; i < 64; ++i) {
        coeffs[i] = kAdvBd10;
        zeroR[i] = 0;
        gotW[i] = 0xEEEE;
        wantW[i] = 0xEEEE;
    }
    transforms::invTxfm2dAddHb8x8(coeffs, zeroR, 8, gotW, 8, transforms::TxType::DCT_DCT, 10);

    oracle::OCfg cfg8;
    cfg8.w = 8;
    cfg8.h = 8;
    static const std::int8_t shift88[2] = {-1, -4};
    cfg8.shift = shift88;
    cfg8.stageNumRow = 6;
    cfg8.stageNumCol = 6;
    cfg8.rowFn = oracle::idct8;
    cfg8.colFn = oracle::idct8;
    oracle::invTxfm2dAdd(coeffs, zeroR, 8, wantW, 8, cfg8, 10);
    for (int i = 0; i < 64; ++i) {
        CHECK(gotW[i] == wantW[i]);
    }
}

TEST_CASE("invTxfm2dAddHb 8x8 DCT_DCT bd=10 adversarial matches the oracle") {
    // every coefficient at the bd-10 maximum: the idct8 stage 3/4/5 clamps
    // at 18 fire alongside the pre-clamps; mutation M-clamp-bit dies here.
    // FIXTURE FIX (TXH1 landing): the port reads dstR through the reference's
    // own r/w-split signature - the pre-fix call read the 0xEEEE sentinel as
    // dest (gotW in-place), a different input than the oracle's dstR, so
    // every comparison degraded to clip(61182 + trans) vs clip(dstR + trans).
    std::int32_t coeffs[8 * 8];
    std::uint16_t dstR[8 * 8];
    std::uint16_t gotW[8 * 8];
    std::uint16_t wantW[8 * 8];
    for (int i = 0; i < 64; ++i) {
        coeffs[i] = kAdvBd10;
        dstR[i] = static_cast<std::uint16_t>(i * 29 % 700);
        gotW[i] = 0xEEEE;
        wantW[i] = 0xEEEE;
    }
    transforms::invTxfm2dAddHb8x8(coeffs, dstR, 8, gotW, 8, transforms::TxType::DCT_DCT, 10);

    oracle::OCfg cfg8;
    cfg8.w = 8;
    cfg8.h = 8;
    static const std::int8_t shift88[2] = {-1, -4};
    cfg8.shift = shift88;
    cfg8.stageNumRow = 6;
    cfg8.stageNumCol = 6;
    cfg8.rowFn = oracle::idct8;
    cfg8.colFn = oracle::idct8;
    oracle::invTxfm2dAdd(coeffs, dstR, 8, wantW, 8, cfg8, 10);
    for (int i = 0; i < 64; ++i) {
        CHECK(gotW[i] == wantW[i]);
    }
}

TEST_CASE("invTxfm2dAddHb 8x8 ADST_ADST bd=10 adversarial matches the oracle") {
    std::int32_t coeffs[8 * 8];
    std::uint16_t dstR[8 * 8];
    std::uint16_t gotW[8 * 8];
    std::uint16_t wantW[8 * 8];
    for (int i = 0; i < 64; ++i) {
        coeffs[i] = (i % 2 == 0 ? kAdvBd10 : -kAdvBd10);
        dstR[i] = static_cast<std::uint16_t>(i * 41 % 1000);
        gotW[i] = 0xEEEE;
        wantW[i] = 0xEEEE;
    }
    transforms::invTxfm2dAddHb8x8(coeffs, dstR, 8, gotW, 8, transforms::TxType::ADST_ADST, 10);

    oracle::OCfg cfg8;
    cfg8.w = 8;
    cfg8.h = 8;
    static const std::int8_t shift88[2] = {-1, -4};
    cfg8.shift = shift88;
    cfg8.stageNumRow = 8;
    cfg8.stageNumCol = 8;
    cfg8.rowFn = oracle::iadst8;
    cfg8.colFn = oracle::iadst8;
    oracle::invTxfm2dAdd(coeffs, dstR, 8, wantW, 8, cfg8, 10);
    for (int i = 0; i < 64; ++i) {
        CHECK(gotW[i] == wantW[i]);
    }
}

TEST_CASE("invTxfm2dAddHb small-input bd invariance: 10 and 12 equal 8") {
    // DC impulse: no clamp fires and the clip bound is never reached for
    // any bd, so the outputs must be identical across bd 8/10/12 (the bd
    // threading must not touch the math)
    for (int shape = 0; shape < 19; ++shape) {
        const transforms::TxSize size = kAllShapes[shape];
        const int w = shapeW(size);
        const int h = shapeH(size);
        const int n = w * h;
        std::int32_t coeffs[64 * 64];
        std::uint16_t base[64 * 64];
        std::uint16_t b10[64 * 64];
        std::uint16_t b12[64 * 64];
        fillDcImpulse(coeffs, base, n, 20);
        std::memcpy(b10, base, sizeof(std::uint16_t) * n);
        std::memcpy(b12, base, sizeof(std::uint16_t) * n);
        transforms::invTxfm2dAddHb(coeffs, b10, w, size, transforms::TxType::DCT_DCT, 10);
        transforms::invTxfm2dAddHb(coeffs, b12, w, size, transforms::TxType::DCT_DCT, 12);
        transforms::invTxfm2dAddHb(coeffs, base, w, size, transforms::TxType::DCT_DCT, 8);
        for (int i = 0; i < n; ++i) {
            CHECK(b10[i] == base[i]);
            CHECK(b12[i] == base[i]);
        }
    }
}

TEST_CASE("invTxfm2dAddHb small-input bd invariance holds for ADST_ADST at 4x4 and 8x8") {
    for (int k = 0; k < 2; ++k) {
        const transforms::TxSize size = (k == 0) ? transforms::TxSize::TX_4X4 : transforms::TxSize::TX_8X8;
        const int n = (k == 0) ? 16 : 64;
        std::int32_t coeffs[64];
        std::uint16_t base[64];
        std::uint16_t b10[64];
        std::uint16_t b12[64];
        fillDcImpulse(coeffs, base, n, 20);
        std::memcpy(b10, base, sizeof(std::uint16_t) * n);
        std::memcpy(b12, base, sizeof(std::uint16_t) * n);
        transforms::invTxfm2dAddHb(coeffs, b10, k == 0 ? 4 : 8, size, transforms::TxType::ADST_ADST, 10);
        transforms::invTxfm2dAddHb(coeffs, b12, k == 0 ? 4 : 8, size, transforms::TxType::ADST_ADST, 12);
        transforms::invTxfm2dAddHb(coeffs, base, k == 0 ? 4 : 8, size, transforms::TxType::ADST_ADST, 8);
        for (int i = 0; i < n; ++i) {
            CHECK(b10[i] == base[i]);
            CHECK(b12[i] == base[i]);
        }
    }
}

// ===========================================================================
// G4: the per-shape hbd facades (derivation at bd 8, all 19 shapes)
// ===========================================================================

TEST_CASE("invTxfm2dAddHb per-shape derivation at bd=8 equals the landed facades") {
    for (int shape = 0; shape < 19; ++shape) {
        const transforms::TxSize size = kAllShapes[shape];
        const int w = shapeW(size);
        const int h = shapeH(size);
        const int n = w * h;
        std::int32_t coeffs[64 * 64];
        std::uint8_t dst8[64 * 64];
        std::uint16_t dst16[64 * 64];
        fillSmall(coeffs, dst16, n);
        for (int i = 0; i < n; ++i) dst8[i] = static_cast<std::uint8_t>(dst16[i]);
        transforms::invTxfm2dAdd(size, coeffs, dst8, w, transforms::TxType::DCT_DCT);
        transforms::invTxfm2dAddHb(coeffs, dst16, w, size, transforms::TxType::DCT_DCT, 8);
        for (int i = 0; i < n; ++i) {
            CHECK(dst16[i] == dst8[i]);
        }
    }
}

TEST_CASE("invTxfm2dAddHb per-shape derivation at bd=8, ADST_ADST squares and IDTX") {
    const transforms::TxType types[3] = {transforms::TxType::ADST_ADST,
                                         transforms::TxType::DCT_ADST,
                                         transforms::TxType::IDTX};
    for (int t = 0; t < 3; ++t) {
        for (int shape = 0; shape < 19; ++shape) {
            const transforms::TxSize size = kAllShapes[shape];
            if (!transforms::invTxfmSupported(size, types[t])) {
                continue;
            }
            const int w = shapeW(size);
            const int h = shapeH(size);
            const int n = w * h;
            std::int32_t coeffs[64 * 64];
            std::uint8_t dst8[64 * 64];
            std::uint16_t dst16[64 * 64];
            fillSmall(coeffs, dst16, n);
            for (int i = 0; i < n; ++i) dst8[i] = static_cast<std::uint8_t>(dst16[i]);
            transforms::invTxfm2dAdd(size, coeffs, dst8, w, types[t]);
            transforms::invTxfm2dAddHb(coeffs, dst16, w, size, types[t], 8);
            for (int i = 0; i < n; ++i) {
                CHECK(dst16[i] == dst8[i]);
            }
        }
    }
}

// ===========================================================================
// G5: the iwht4x4 pair + the 4x4 lossless entry
// ===========================================================================

TEST_CASE("iwht4x4_16AddHb DC-only input spreads uniformly (hand pin)") {
    // input {64,0,0,0} at UNIT_QUANT_SHIFT 2: both WHT passes leave every
    // output at 4, so every dest position gains 4
    std::int32_t coeffs[16] = {0};
    std::uint16_t dst[4 * 4];
    for (int i = 0; i < 16; ++i) dst[i] = 10;
    coeffs[0] = 64;
    transforms::iwht4x4_16AddHb(coeffs, dst, 4, dst, 4, 8);
    for (int i = 0; i < 16; ++i) {
        CHECK(dst[i] == 14);
    }
}

TEST_CASE("iwht4x4_1AddHb DC-only input adds 1 everywhere (hand pin)") {
    // input {16,0,0,0}: first pass tmp = {2,2,2,2}, second pass adds 1 per
    // position
    std::int32_t coeffs[16] = {0};
    std::uint16_t dst[4 * 4];
    for (int i = 0; i < 16; ++i) dst[i] = 10;
    coeffs[0] = 16;
    transforms::iwht4x4_1AddHb(coeffs, dst, 4, dst, 4, 8);
    for (int i = 0; i < 16; ++i) {
        CHECK(dst[i] == 11);
    }
}

TEST_CASE("iwht4x4 pair matches the oracle at bd 8/10/12 with adversarial coeffs") {
    const int bds[3] = {8, 10, 12};
    for (int b = 0; b < 3; ++b) {
        const int bd = bds[b];
        std::int32_t coeffs[16];
        std::uint16_t dstR[4 * 4];
        std::uint16_t gotW[4 * 4];
        std::uint16_t wantW[4 * 4];
        for (int i = 0; i < 16; ++i) {
            coeffs[i] = ((i * 31) % 701) - 350;
            dstR[i] = static_cast<std::uint16_t>(i * 67 % 1300);
            gotW[i] = 0xEEEE;
            wantW[i] = 0xEEEE;
        }
        coeffs[0] = (bd == 10) ? kAdvBd10 : (bd == 12 ? kAdvBd12 : 100000);
        transforms::iwht4x4_16AddHb(coeffs, dstR, 4, gotW, 4, bd);
        oracle::iwht4x4_16Add(coeffs, dstR, 4, wantW, 4, bd);
        for (int i = 0; i < 16; ++i) {
            CHECK(gotW[i] == wantW[i]);
        }

        std::uint16_t got1[4 * 4];
        std::uint16_t want1[4 * 4];
        for (int i = 0; i < 16; ++i) {
            got1[i] = 0xEEEE;
            want1[i] = 0xEEEE;
        }
        transforms::iwht4x4_1AddHb(coeffs, dstR, 4, got1, 4, bd);
        oracle::iwht4x4_1Add(coeffs, dstR, 4, want1, 4, bd);
        for (int i = 0; i < 16; ++i) {
            CHECK(got1[i] == want1[i]);
        }
    }
}

TEST_CASE("highbdInvTxfmAdd4x4Hb lossless routing: eob>1 -> 16Add, eob<=1 -> 1Add") {
    std::int32_t coeffs[16] = {0};
    coeffs[0] = 64;
    transforms::TxfmParamHb p;
    p.txType = transforms::TxType::DCT_DCT;
    p.txSize = transforms::TxSize::TX_4X4;
    p.lossless = 1;
    p.bd = 8;
    p.isHbd = 1;
    p.eob = 4;
    std::uint16_t dst[4 * 4];
    for (int i = 0; i < 16; ++i) dst[i] = 10;
    transforms::highbdInvTxfmAdd4x4Hb(coeffs, dst, 4, dst, 4, p);
    for (int i = 0; i < 16; ++i) {
        CHECK(dst[i] == 14);  // the 16Add hand pin
    }
    p.eob = 1;
    // FIXTURE FIX (TXH1 landing): the eob<=1 leg needs its own DC value. The
    // pre-fix fixture reused coeffs[0]=64, where 1Add gives +4 per position
    // (tmp={8,8,8,8}, second pass +4) - the same 14 as 16Add, so the pin 11
    // could never fire and the routing was not discriminated. coeffs[0]=16:
    // 16Add gives 12 (row {2,2,2,2}, col +2) and 1Add gives 11 (hand pin).
    coeffs[0] = 16;
    for (int i = 0; i < 16; ++i) dst[i] = 10;
    transforms::highbdInvTxfmAdd4x4Hb(coeffs, dst, 4, dst, 4, p);
    for (int i = 0; i < 16; ++i) {
        CHECK(dst[i] == 11);  // the 1Add hand pin (16Add on this input gives 12)
    }
}

TEST_CASE("highbdInvTxfmAdd4x4Hb lossless=0 routes to the regular 4x4 facade") {
    std::int32_t coeffs[16];
    std::uint8_t dst8[4 * 4];
    std::uint16_t dst[4 * 4];
    fillSmall(coeffs, dst, 16);
    for (int i = 0; i < 16; ++i) dst8[i] = static_cast<std::uint8_t>(dst[i]);
    transforms::TxfmParamHb p;
    p.txType = transforms::TxType::DCT_DCT;
    p.txSize = transforms::TxSize::TX_4X4;
    p.lossless = 0;
    p.bd = 8;
    p.isHbd = 1;
    p.eob = 16;
    transforms::invTxfm2dAdd4x4(coeffs, dst8, 4, transforms::TxType::DCT_DCT);
    transforms::highbdInvTxfmAdd4x4Hb(coeffs, dst, 4, dst, 4, p);
    for (int i = 0; i < 16; ++i) {
        CHECK(dst[i] == dst8[i]);
    }
}

// ===========================================================================
// G6: the dispatcher, the recon entries and the uint8 staging entry
// ===========================================================================

TEST_CASE("highbdInvTxfmAddHb routes every admissible shape to the right facade") {
    for (int shape = 0; shape < 19; ++shape) {
        const transforms::TxSize size = kAllShapes[shape];
        const int w = shapeW(size);
        const int h = shapeH(size);
        const int n = w * h;
        std::int32_t coeffs[64 * 64];
        std::uint16_t viaFacade[64 * 64];
        std::uint16_t viaDispatch[64 * 64];
        fillSmall(coeffs, viaFacade, n);
        std::memcpy(viaDispatch, viaFacade, sizeof(std::uint16_t) * n);
        transforms::invTxfm2dAddHb(coeffs, viaFacade, w, size, transforms::TxType::DCT_DCT, 10);
        transforms::TxfmParamHb p;
        p.txType = transforms::TxType::DCT_DCT;
        p.txSize = size;
        p.lossless = 0;
        p.bd = 10;
        p.isHbd = 1;
        p.eob = n;
        transforms::highbdInvTxfmAddHb(coeffs, viaDispatch, w, viaDispatch, w, p);
        for (int i = 0; i < n; ++i) {
            CHECK(viaDispatch[i] == viaFacade[i]);
        }
    }
}

TEST_CASE("highbdInvTxfmAddHb honors the 32x32 DCT_DCT/IDTX and 64x64 DCT-only gates") {
    // TX_32X32 + ADST_ADST is outside the wrapper switch (inv_transforms.c:
    // 2924-2932): the dispatcher's support predicate must refuse it
    CHECK(transforms::invTxfmSupported(transforms::TxSize::TX_32X32, transforms::TxType::ADST_ADST) == false);
    CHECK(transforms::invTxfmSupported(transforms::TxSize::TX_64X64, transforms::TxType::IDTX) == false);
    CHECK(transforms::invTxfmSupported(transforms::TxSize::TX_32X32, transforms::TxType::IDTX) == true);
    CHECK(transforms::invTxfmSupported(transforms::TxSize::TX_64X64, transforms::TxType::DCT_DCT) == true);
}

TEST_CASE("invTransformReconHb builds the TxfmParam and routes (bd 10)") {
    std::int32_t coeffs[64];
    std::uint16_t recon[8 * 8];
    std::uint16_t viaRecon[8 * 8];
    fillSmall(coeffs, recon, 64);
    std::memcpy(viaRecon, recon, sizeof(std::uint16_t) * 64);
    transforms::invTxfm2dAddHb(coeffs, recon, 8, transforms::TxSize::TX_8X8,
                               transforms::TxType::DCT_DCT, 10);
    transforms::invTransformReconHb(coeffs, viaRecon, 8, viaRecon, 8, transforms::TxSize::TX_8X8,
                                    10, transforms::TxType::DCT_DCT, 64, false);
    for (int i = 0; i < 64; ++i) {
        CHECK(viaRecon[i] == recon[i]);
    }
}

TEST_CASE("invTransformRecon8bitHb pins bd=8 and routes") {
    std::int32_t coeffs[64];
    std::uint16_t recon[8 * 8];
    std::uint16_t viaRecon[8 * 8];
    fillSmall(coeffs, recon, 64);
    std::memcpy(viaRecon, recon, sizeof(std::uint16_t) * 64);
    transforms::invTxfm2dAddHb(coeffs, recon, 8, transforms::TxSize::TX_8X8,
                               transforms::TxType::DCT_DCT, 8);
    transforms::invTransformRecon8bitHb(coeffs, viaRecon, 8, viaRecon, 8,
                                        transforms::TxSize::TX_8X8, transforms::TxType::DCT_DCT,
                                        64, false);
    for (int i = 0; i < 64; ++i) {
        CHECK(viaRecon[i] == recon[i]);
    }
}

TEST_CASE("invTxfmAddStagingHb: uint8 buffers stage through uint16 and equal the direct path") {
    std::int32_t coeffs[16];
    std::uint8_t dst8[4 * 4];
    std::uint8_t staged[4 * 4];
    std::uint16_t dst16[4 * 4];
    fillSmall(coeffs, dst16, 16);
    for (int i = 0; i < 16; ++i) {
        dst8[i] = static_cast<std::uint8_t>(dst16[i]);
        staged[i] = static_cast<std::uint8_t>(dst16[i]);
    }
    transforms::TxfmParamHb p;
    p.txType = transforms::TxType::DCT_DCT;
    p.txSize = transforms::TxSize::TX_4X4;
    p.lossless = 0;
    p.bd = 8;
    p.isHbd = 1;
    p.eob = 16;
    transforms::invTxfm2dAdd4x4(coeffs, dst8, 4, transforms::TxType::DCT_DCT);
    transforms::invTxfmAddStagingHb(coeffs, staged, 4, staged, 4, p);
    for (int i = 0; i < 16; ++i) {
        CHECK(staged[i] == dst8[i]);
    }
}

// ===========================================================================
// G7: the fwd dispatch chain (gen_fwd_stage_range + the default-shape fwd)
// ===========================================================================

TEST_CASE("genFwdStageRangeHb TX_8X8 DCT_DCT: cfg absolutes + the bd ladder") {
    // the cfg absolutes are the landed TX1 fwd tables (gate-pinned); the
    // outputs are the reference formula absolutes transforms.c:154/:158:
    // col[i] = stage_range_col[i] + shift[0] + bd + 1
    // row[i] = stage_range_row[i] + shift[0] + shift[1] + bd + 1
    transforms::Txfm2dFlipCfg cfg;
    transforms::fwdTransformConfig(transforms::TxType::DCT_DCT, transforms::TxSize::TX_8X8, cfg);
    CHECK(cfg.shift[0] == 2);
    CHECK(cfg.shift[1] == -1);
    CHECK(cfg.shift[2] == 0);
    CHECK(cfg.stageNumRow == 6);
    CHECK(cfg.stageNumCol == 6);
    const std::int8_t wantRow[6] = {3, 4, 5, 5, 5, 5};
    const std::int8_t wantCol[6] = {0, 1, 2, 3, 3, 3};
    for (int i = 0; i < 6; ++i) {
        CHECK(cfg.stageRangeRow[i] == wantRow[i]);
        CHECK(cfg.stageRangeCol[i] == wantCol[i]);
    }
    // bd 8: col = {11,12,13,14,14,14} row = {13,14,15,15,15,15}
    {
        std::int8_t col[12] = {0};
        std::int8_t row[12] = {0};
        transforms::genFwdStageRangeHb(col, row, cfg, 8);
        const std::int8_t wantCol8[6] = {11, 12, 13, 14, 14, 14};
        const std::int8_t wantRow8[6] = {13, 14, 15, 15, 15, 15};
        for (int i = 0; i < 6; ++i) {
            CHECK(col[i] == wantCol8[i]);
            CHECK(row[i] == wantRow8[i]);
        }
    }
    // bd 10: every entry +2 vs bd 8 (the bd term is linear)
    {
        std::int8_t col[12] = {0};
        std::int8_t row[12] = {0};
        transforms::genFwdStageRangeHb(col, row, cfg, 10);
        const std::int8_t wantCol10[6] = {13, 14, 15, 16, 16, 16};
        const std::int8_t wantRow10[6] = {15, 16, 17, 17, 17, 17};
        for (int i = 0; i < 6; ++i) {
            CHECK(col[i] == wantCol10[i]);
            CHECK(row[i] == wantRow10[i]);
        }
    }
    // bd 12: every entry +4 vs bd 8
    {
        std::int8_t col[12] = {0};
        std::int8_t row[12] = {0};
        transforms::genFwdStageRangeHb(col, row, cfg, 12);
        const std::int8_t wantCol12[6] = {15, 16, 17, 18, 18, 18};
        const std::int8_t wantRow12[6] = {17, 18, 19, 19, 19, 19};
        for (int i = 0; i < 6; ++i) {
            CHECK(col[i] == wantCol12[i]);
            CHECK(row[i] == wantRow12[i]);
        }
    }
}

TEST_CASE("highbdFwdTxfmHb bd-free derivation: 8x8 DCT_DCT at bd 10 equals landed fwdTxfm2d8x8") {
    // the default-shape highbd fwd statics route into the landed fwd 2D
    // kernels; the behavior is bd-free (the only fwd stage_range consumers
    // are comments and NDEBUG-dead asserts - transforms.c:3726-3834)
    std::int16_t srcDiff[64];
    for (int i = 0; i < 64; ++i) {
        srcDiff[i] = static_cast<std::int16_t>(((i * 37) % 251) - 125);
    }
    std::int32_t landed[64];
    std::int32_t got[64];
    transforms::fwdTxfm2d8x8(srcDiff, landed, 8, transforms::TxType::DCT_DCT);
    transforms::TxfmParamHb p;
    p.txType = transforms::TxType::DCT_DCT;
    p.txSize = transforms::TxSize::TX_8X8;
    p.lossless = 0;
    p.bd = 10;
    p.isHbd = 1;
    p.eob = 64;
    transforms::highbdFwdTxfmHb(srcDiff, got, 8, p);
    for (int i = 0; i < 64; ++i) {
        CHECK(got[i] == landed[i]);
    }
}

TEST_CASE("highbdFwdTxfmHb TX_4X4 is the reference no-op (the hack comment)") {
    // transforms.c:4016-4018: case TX_4X4: //hack highbd_fwd_txfm_4x4(...);
    // break - the dispatcher leaves coeff untouched
    std::int16_t srcDiff[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    std::int32_t coeff[16];
    for (int i = 0; i < 16; ++i) coeff[i] = -12345 + i;
    transforms::TxfmParamHb p;
    p.txType = transforms::TxType::DCT_DCT;
    p.txSize = transforms::TxSize::TX_4X4;
    p.lossless = 0;
    p.bd = 8;
    p.isHbd = 1;
    p.eob = 16;
    transforms::highbdFwdTxfmHb(srcDiff, coeff, 4, p);
    for (int i = 0; i < 16; ++i) {
        CHECK(coeff[i] == -12345 + i);
    }
}

TEST_CASE("whtFwdTxfmHb DEFAULT_SHAPE routes into the default fwd path") {
    // svt_av1_wht_fwd_txfm (transforms.c:4037-4057): the DEFAULT_SHAPE branch
    // calls highbd_fwd_txfm; the N2/N4 branches are PARKED (named in the
    // header) and assert - untestable in a Release NDEBUG build
    std::int16_t srcDiff[64];
    for (int i = 0; i < 64; ++i) {
        srcDiff[i] = static_cast<std::int16_t>(((i * 53) % 199) - 99);
    }
    std::int32_t landed[64];
    std::int32_t got[64];
    transforms::fwdTxfm2d8x8(srcDiff, landed, 8, transforms::TxType::DCT_DCT);
    transforms::whtFwdTxfmHb(srcDiff, 8, got, transforms::TxSize::TX_8X8,
                             transforms::TxCoeffShapeHb::DEFAULT_SHAPE, 10, 1);
    for (int i = 0; i < 64; ++i) {
        CHECK(got[i] == landed[i]);
    }
}

// ---------------------------------------------------------------------------
// QH1: the A1 highbd quantizer surface. Test-side expectations for the four
// 10/12-bit QTX lookup tables are PARSED FROM THE REFERENCE at runtime (the
// RC1 mechanical pattern: zero hand-typed table values; PAIRED-METRIC CAVEAT
// stated). The pinned tree is CONFIG_ENABLE_HIGH_BIT_DEPTH=0
// (Source/API/EbConfigMacros.h:55-57), so the reference's 10/12 table arms
// are compiled out there and the default arm returns -1; this port carries
// the CONFIG=1 surface (the TXH1 hbd-twin precedent) and reproduces the -1
// default arm for invalid bd.
// ---------------------------------------------------------------------------
namespace qh1 {

std::string refText(const char* rel) {
    std::ifstream f(std::string(REPO_ROOT "/") + rel, std::ios::binary);
    REQUIRE_MESSAGE(f.good(), rel);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::vector<int> tableNumbers(const std::string& text, const std::string& decl, int expected) {
    const std::size_t at = text.find(decl);
    REQUIRE_MESSAGE(at != std::string::npos, decl);
    const std::size_t start = at + decl.size();
    const std::size_t end = text.find("};", start);
    REQUIRE(end != std::string::npos);
    const std::string body = text.substr(start, end - start);
    std::vector<int> ref;
    std::regex numRe(R"(-?\d+)");
    for (auto n = std::regex_iterator<std::string::const_iterator>(body.begin(), body.end(), numRe);
         n != std::regex_iterator<std::string::const_iterator>(); ++n) {
        ref.push_back(std::atoi(n->str().c_str()));
    }
    REQUIRE((int)ref.size() == expected);
    return ref;
}

}  // namespace qh1

TEST_CASE("dcQuantQtxBd/acQuantQtxBd match inv_transforms.c entry-for-entry at bd 8/10/12") {
    const std::string text = qh1::refText("/third_party/SVT-AV1/Source/Lib/Codec/inv_transforms.c");
    const std::vector<int> dc8 =
        qh1::tableNumbers(text, "static const int16_t dc_qlookup_QTX[QINDEX_RANGE] = {", 256);
    const std::vector<int> dc10 =
        qh1::tableNumbers(text, "static const int16_t dc_qlookup_10_QTX[QINDEX_RANGE] = {", 256);
    const std::vector<int> dc12 =
        qh1::tableNumbers(text, "static const int16_t dc_qlookup_12_QTX[QINDEX_RANGE] = {", 256);
    const std::vector<int> ac8 =
        qh1::tableNumbers(text, "static const int16_t ac_qlookup_QTX[QINDEX_RANGE] = {", 256);
    const std::vector<int> ac10 =
        qh1::tableNumbers(text, "static const int16_t ac_qlookup_10_QTX[QINDEX_RANGE] = {", 256);
    const std::vector<int> ac12 =
        qh1::tableNumbers(text, "static const int16_t ac_qlookup_12_QTX[QINDEX_RANGE] = {", 256);

    int bad = 0;
    for (int q = 0; q < 256; ++q) {
        if (transforms::dcQuantQtxBd(q, 8) != dc8[q]) ++bad;
        if (transforms::dcQuantQtxBd(q, 10) != dc10[q]) ++bad;
        if (transforms::dcQuantQtxBd(q, 12) != dc12[q]) ++bad;
        if (transforms::acQuantQtxBd(q, 8) != ac8[q]) ++bad;
        if (transforms::acQuantQtxBd(q, 10) != ac10[q]) ++bad;
        if (transforms::acQuantQtxBd(q, 12) != ac12[q]) ++bad;
    }
    CHECK(bad == 0);
}

TEST_CASE("dcQuantQtxBd/acQuantQtxBd clamp the qindex and return -1 off the bd switch") {
    // svt_aom_dc_quant_qtx (inv_transforms.c:3468): clamp(qindex + delta, 0,
    // MAXQ) with delta folded 0 (the landed 8-bit twin's fold); the default
    // arm is assert + return -1 (:3478-3481, :3495-3498)
    CHECK(transforms::dcQuantQtxBd(-5, 8) == 4);
    CHECK(transforms::dcQuantQtxBd(300, 12) == 21387);
    CHECK(transforms::acQuantQtxBd(-1, 10) == 4);
    CHECK(transforms::acQuantQtxBd(999, 12) == 29247);
    CHECK(transforms::dcQuantQtxBd(4, 9) == -1);
    CHECK(transforms::acQuantQtxBd(4, 16) == -1);
}

TEST_CASE("qzbinFactorBd matches svt_aom_get_qzbin_factor over all q x bd") {
    // svt_aom_get_qzbin_factor (inv_transforms.c:3501-3514): q == 0 -> 64;
    // else the dc-quant threshold split 148 / 592 / 2368 -> 84 / 80. The
    // test-side oracle is the formula transcribed independently from the
    // reference text, evaluated over the RUNTIME-PARSED dc tables (the
    // thresholds are the only literals and each is formula-pinned here).
    const std::string text = qh1::refText("/third_party/SVT-AV1/Source/Lib/Codec/inv_transforms.c");
    const std::vector<int> dc8 =
        qh1::tableNumbers(text, "static const int16_t dc_qlookup_QTX[QINDEX_RANGE] = {", 256);
    const std::vector<int> dc10 =
        qh1::tableNumbers(text, "static const int16_t dc_qlookup_10_QTX[QINDEX_RANGE] = {", 256);
    const std::vector<int> dc12 =
        qh1::tableNumbers(text, "static const int16_t dc_qlookup_12_QTX[QINDEX_RANGE] = {", 256);
    int bad = 0;
    for (int q = 0; q < 256; ++q) {
        const int want8 = q == 0 ? 64 : (dc8[q] < 148 ? 84 : 80);
        const int want10 = q == 0 ? 64 : (dc10[q] < 592 ? 84 : 80);
        const int want12 = q == 0 ? 64 : (dc12[q] < 2368 ? 84 : 80);
        if (transforms::qzbinFactorBd(q, 8) != want8) ++bad;
        if (transforms::qzbinFactorBd(q, 10) != want10) ++bad;
        if (transforms::qzbinFactorBd(q, 12) != want12) ++bad;
    }
    CHECK(bad == 0);
    // the threshold boundaries themselves - the arms fire on BOTH sides of
    // every threshold: bd 8 dc8[131]=146 < 148, dc8[132]=148; bd 10
    // dc10[131]=584 < 592, dc10[132]=592; bd 12 dc12[132]=2367 < 2368,
    // dc12[133]=2400
    CHECK(transforms::qzbinFactorBd(131, 8) == 84);
    CHECK(transforms::qzbinFactorBd(132, 8) == 80);
    CHECK(transforms::qzbinFactorBd(131, 10) == 84);
    CHECK(transforms::qzbinFactorBd(132, 10) == 80);
    CHECK(transforms::qzbinFactorBd(132, 12) == 84);
    CHECK(transforms::qzbinFactorBd(133, 12) == 80);
    CHECK(transforms::qzbinFactorBd(0, 10) == 64);
    CHECK(transforms::qzbinFactorBd(0, 12) == 64);
    CHECK(transforms::qzbinFactorBd(4, 7) == -1);
}

namespace qh1 {

// svt_aom_invert_quant (inv_transforms.c:3516-3526), transcribed from the
// reference text for the buildQuantTablesBd oracle
void invertQuantOracle(std::int16_t* quant, std::int16_t* shift, std::int32_t d) {
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

// the luma rows of svt_av1_build_quantizer (md_config_process.c:106-135) at
// deltas 0 / sharpness 0 - the SAME folds the landed 8-bit twin names - as
// an independent oracle over the runtime-parsed tables
void buildQuantTablesOracle(int q, int bd, const std::vector<int>& dc,
                            const std::vector<int>& ac, transforms::QuantTables& t) {
    const int quantQtx[2] = {dc[q], ac[q]};
    const int qzbin = q == 0 ? 64 : (dc[q] < (bd == 8 ? 148 : bd == 10 ? 592 : 2368) ? 84 : 80);
    const int qroundingFactor = q == 0 ? 64 : 48;
    for (int i = 0; i < 2; ++i) {
        std::int16_t qv, sv;
        invertQuantOracle(&qv, &sv, quantQtx[i]);
        t.quant[i] = qv;
        t.quantShift[i] = sv;
        t.quantFp[i] = static_cast<std::int16_t>((1 << 16) / quantQtx[i]);
        t.roundFp[i] = static_cast<std::int16_t>((64 * quantQtx[i]) >> 7);
        t.zbin[i] = static_cast<std::int16_t>((qzbin * quantQtx[i] + 64) >> 7);
        t.round[i] = static_cast<std::int16_t>((qroundingFactor * quantQtx[i]) >> 7);
        t.dequant[i] = static_cast<std::int16_t>(quantQtx[i]);
    }
}

}  // namespace qh1

TEST_CASE("buildQuantTablesBd at bd 8 equals the landed buildQuantTables for all qindex") {
    transforms::QuantTables got, want;
    int bad = 0;
    for (int q = 0; q < 256; ++q) {
        transforms::buildQuantTablesBd(q, 8, got);
        transforms::buildQuantTables(q, want);
        for (int i = 0; i < 2; ++i) {
            if (got.quant[i] != want.quant[i]) ++bad;
            if (got.quantShift[i] != want.quantShift[i]) ++bad;
            if (got.quantFp[i] != want.quantFp[i]) ++bad;
            if (got.roundFp[i] != want.roundFp[i]) ++bad;
            if (got.zbin[i] != want.zbin[i]) ++bad;
            if (got.round[i] != want.round[i]) ++bad;
            if (got.dequant[i] != want.dequant[i]) ++bad;
        }
    }
    CHECK(bad == 0);
}

TEST_CASE("buildQuantTablesBd at bd 10/12 matches the md_config_process.c oracle") {
    const std::string text = qh1::refText("/third_party/SVT-AV1/Source/Lib/Codec/inv_transforms.c");
    const std::vector<int> dc10 =
        qh1::tableNumbers(text, "static const int16_t dc_qlookup_10_QTX[QINDEX_RANGE] = {", 256);
    const std::vector<int> ac10 =
        qh1::tableNumbers(text, "static const int16_t ac_qlookup_10_QTX[QINDEX_RANGE] = {", 256);
    const std::vector<int> dc12 =
        qh1::tableNumbers(text, "static const int16_t dc_qlookup_12_QTX[QINDEX_RANGE] = {", 256);
    const std::vector<int> ac12 =
        qh1::tableNumbers(text, "static const int16_t ac_qlookup_12_QTX[QINDEX_RANGE] = {", 256);
    transforms::QuantTables got, want;
    int bad = 0;
    for (int q = 0; q < 256; ++q) {
        transforms::buildQuantTablesBd(q, 10, got);
        qh1::buildQuantTablesOracle(q, 10, dc10, ac10, want);
        for (int i = 0; i < 2; ++i) {
            if (got.quant[i] != want.quant[i]) ++bad;
            if (got.quantShift[i] != want.quantShift[i]) ++bad;
            if (got.quantFp[i] != want.quantFp[i]) ++bad;
            if (got.roundFp[i] != want.roundFp[i]) ++bad;
            if (got.zbin[i] != want.zbin[i]) ++bad;
            if (got.round[i] != want.round[i]) ++bad;
            if (got.dequant[i] != want.dequant[i]) ++bad;
        }
        transforms::buildQuantTablesBd(q, 12, got);
        qh1::buildQuantTablesOracle(q, 12, dc12, ac12, want);
        for (int i = 0; i < 2; ++i) {
            if (got.quant[i] != want.quant[i]) ++bad;
            if (got.quantShift[i] != want.quantShift[i]) ++bad;
            if (got.quantFp[i] != want.quantFp[i]) ++bad;
            if (got.roundFp[i] != want.roundFp[i]) ++bad;
            if (got.zbin[i] != want.zbin[i]) ++bad;
            if (got.round[i] != want.round[i]) ++bad;
            if (got.dequant[i] != want.dequant[i]) ++bad;
        }
    }
    CHECK(bad == 0);
    // saturation absolutes at the extreme qindices (12-bit zbin rounds up
    // through int16 range comfortably; the fp reciprocal of the largest
    // 12-bit quant is the smallest)
    transforms::buildQuantTablesBd(255, 12, got);
    CHECK(got.dequant[0] == 21387);
    CHECK(got.dequant[1] == 29247);
    CHECK(got.quantFp[1] == (1 << 16) / 29247);
    CHECK(got.zbin[0] == static_cast<std::int16_t>((80 * 21387 + 64) >> 7));
}

namespace qh1 {

// svt_aom_highbd_quantize_b_c (full_loop.c:85-136), qm/iqm NULL-folded
// (wt = iwt = 1 << AOM_QM_BITS = 32, inv_transforms.h:27) - an independent
// transcription of the reference text. NO int16 clamp on the round add
// (that clamp exists only in the lowbd svt_aom_quantize_b_c, :67) and the
// pre-scan COLLECTS every out-of-bin index (no early break).
void quantizeBOracle(const std::int32_t* coeff, const transforms::QuantTables& tables,
                     const std::int16_t* scan, int nCoeffs, int logScale,
                     std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    const int zbins[2] = {
        (int)(((tables.zbin[0] < 0 ? 0 : tables.zbin[0]) + ((1 << logScale) >> 1)) >> logScale),
        (int)(((tables.zbin[1] < 0 ? 0 : tables.zbin[1]) + ((1 << logScale) >> 1)) >> logScale)};
    // ROUND_POWER_OF_TWO(zbin, log_scale) - zbin is non-negative
    int eobVal = -1;
    for (int i = 0; i < nCoeffs; ++i) {
        qcoeff[i] = 0;
        dqcoeff[i] = 0;
    }
    std::int32_t idxArr[4096];
    int idx = 0;
    for (int i = 0; i < nCoeffs; ++i) {
        const int rc = scan[i];
        const std::int32_t wt = 32;
        const std::int32_t coeffVal = coeff[rc] * wt;
        if (coeffVal >= (zbins[rc != 0] * (1 << 5)) || coeffVal <= (zbins[rc != 0] * -(1 << 5))) {
            idxArr[idx++] = i;
        }
    }
    for (int i = 0; i < idx; ++i) {
        const int rc = scan[idxArr[i]];
        const std::int32_t coeffVal = coeff[rc];
        const int coeffSign = coeffVal < 0 ? -1 : 0;
        const std::int32_t absCoeff = (coeffVal ^ coeffSign) - coeffSign;
        const std::int64_t tmp1 = absCoeff + (int)(((tables.round[rc != 0] < 0 ? 0 : tables.round[rc != 0]) +
                                                    ((1 << logScale) >> 1)) >> logScale);
        const std::int64_t tmpw = tmp1 * 32;
        const std::int64_t tmp2 = ((tmpw * tables.quant[rc != 0]) >> 16) + tmpw;
        const std::int32_t absQcoeff =
            (std::int32_t)((tmp2 * tables.quantShift[rc != 0]) >> (16 - logScale + 5));
        qcoeff[rc] = (absQcoeff ^ coeffSign) - coeffSign;
        // (dequant * iwt + (1 << (AOM_QM_BITS - 1))) >> AOM_QM_BITS == dequant
        // at iwt = 32 (exact for any dequant)
        const std::int32_t dequant = tables.dequant[rc != 0];
        const std::int32_t absDqcoeff = (std::int32_t)(((std::int64_t)absQcoeff * dequant) >> logScale);
        dqcoeff[rc] = (absDqcoeff ^ coeffSign) - coeffSign;
        if (absQcoeff) {
            eobVal = idxArr[i];
        }
    }
    *eob = static_cast<std::uint16_t>(eobVal + 1);
}

std::uint32_t lcgState = 0x1234u;
std::int32_t lcg(int bound) {  // deterministic, non-cryptographic
    lcgState = lcgState * 1664525u + 1013904223u;
    return (std::int32_t)((lcgState >> 8) % (unsigned)bound);
}

}  // namespace qh1

TEST_CASE("quantizeBHb equals the landed quantizeBN wherever the lowbd clamp is dead") {
    // derivation: identical tables (bd 8), coefficients with
    // abs_coeff + round <= 32767 (round <= (48*1336)>>7 = 501 at qindex 255,
    // so |coeff| <= 32000 keeps the lowbd INT16 clamp dead) - the two
    // algorithms must agree bit-for-bit on qcoeff, dqcoeff and eob
    const int sizes[3] = {16, 64, 256};
    void (*defaultScan[3])(std::int16_t*);
    defaultScan[0] = transforms::defaultScan4x4;
    defaultScan[1] = transforms::defaultScan8x8;
    defaultScan[2] = transforms::defaultScan16x16;
    int bad = 0;
    for (int qi = 0; qi < 3; ++qi) {
        transforms::QuantTables tables;
        transforms::buildQuantTables(qh1::lcg(256), tables);
        for (int s = 0; s < 3; ++s) {
            std::int16_t scan[256];
            defaultScan[s](scan);
            const int n = sizes[s];
            for (int trial = 0; trial < 8; ++trial) {
                std::int32_t coeff[256];
                for (int i = 0; i < n; ++i) {
                    coeff[i] = qh1::lcg(64001) - 32000;
                }
                std::int32_t qcHb[256], qcLb[256], dqHb[256], dqLb[256];
                std::uint16_t eobHb, eobLb;
                transforms::quantizeBHb(coeff, tables, scan, n, 0, qcHb, dqHb, &eobHb);
                if (s == 0) {
                    transforms::quantizeB4x4(coeff, tables, scan, qcLb, dqLb, &eobLb);
                } else if (s == 1) {
                    transforms::quantizeB8x8(coeff, tables, scan, qcLb, dqLb, &eobLb);
                } else {
                    transforms::quantizeB16x16(coeff, tables, scan, qcLb, dqLb, &eobLb);
                }
                for (int i = 0; i < n; ++i) {
                    if (qcHb[i] != qcLb[i]) ++bad;
                    if (dqHb[i] != dqLb[i]) ++bad;
                }
                if (eobHb != eobLb) ++bad;
                // the transcription oracle must agree on EVERY trial
                std::int32_t qcOr[256], dqOr[256];
                std::uint16_t eobOr;
                qh1::quantizeBOracle(coeff, tables, scan, n, 0, qcOr, dqOr, &eobOr);
                for (int i = 0; i < n; ++i) {
                    if (qcHb[i] != qcOr[i]) ++bad;
                    if (dqHb[i] != dqOr[i]) ++bad;
                }
                if (eobHb != eobOr) ++bad;
            }
        }
    }
    CHECK(bad == 0);
}

TEST_CASE("quantizeBHb drops the lowbd INT16 clamp - the divergence is load-bearing") {
    // at qindex 255 bd 8: round = (48*1336)>>7 = 501, so a coefficient of
    // 33000 pushes abs_coeff + round to 33501 - the lowbd quantize_b clamps
    // it to 32767 (:67) while the highbd path (full_loop.c:122) does NOT
    transforms::QuantTables tables;
    transforms::buildQuantTables(255, tables);
    std::int16_t scan[16];
    transforms::defaultScan4x4(scan);
    std::int32_t coeff[16] = {0};
    coeff[0] = 33000;
    std::int32_t qcHb[16], qcLb[16], dqHb[16], dqLb[16], qcOr[16], dqOr[16];
    std::uint16_t eobHb, eobLb, eobOr;
    transforms::quantizeBHb(coeff, tables, scan, 16, 0, qcHb, dqHb, &eobHb);
    transforms::quantizeB4x4(coeff, tables, scan, qcLb, dqLb, &eobLb);
    qh1::quantizeBOracle(coeff, tables, scan, 16, 0, qcOr, dqOr, &eobOr);
    CHECK(qcHb[0] == qcOr[0]);
    CHECK(qcHb[0] != qcLb[0]);  // the no-clamp difference is observable
    // the lowbd clamp (32767 on the round add, :67) yields a strictly
    // smaller quantized magnitude - the pipeline is monotonic in tmp
    CHECK(qcLb[0] < qcHb[0]);
    CHECK(dqLb[0] < dqHb[0]);
    CHECK(dqHb[0] == dqOr[0]);
    CHECK(eobHb == 1);
}

TEST_CASE("quantizeBHb saturation, zbin boundaries and eob semantics") {
    transforms::QuantTables tables;
    transforms::buildQuantTables(255, tables);
    std::int16_t scan[64];
    transforms::defaultScan8x8(scan);
    // adversarial saturation: magnitudes up to 2^25 (coeff*32 stays inside
    // int32, mirroring the reference's own int32 pre-scan product)
    std::int32_t coeff[64];
    const std::int32_t big[8] = {1 << 24, -(1 << 24), 16777213, -16777213,
                                 tables.zbin[1], -tables.zbin[1], tables.zbin[1] - 1, -(tables.zbin[1] - 1)};
    for (int i = 0; i < 64; ++i) {
        coeff[i] = big[i % 8];
    }
    std::int32_t qc[64], dq[64], qcOr[64], dqOr[64];
    std::uint16_t eob, eobOr;
    transforms::quantizeBHb(coeff, tables, scan, 64, 0, qc, dq, &eob);
    qh1::quantizeBOracle(coeff, tables, scan, 64, 0, qcOr, dqOr, &eobOr);
    int bad = 0;
    for (int i = 0; i < 64; ++i) {
        if (qc[i] != qcOr[i]) ++bad;
        if (dq[i] != dqOr[i]) ++bad;
    }
    CHECK(bad == 0);
    CHECK(eob == eobOr);
    // all-zero block: eob 0
    std::int32_t zero[64] = {0};
    std::uint16_t eobZero;
    transforms::quantizeBHb(zero, tables, scan, 64, 0, qc, dq, &eobZero);
    CHECK(eobZero == 0);
    // a single out-of-bin coefficient at scan position k gives eob k+1
    std::int32_t single[64] = {0};
    single[scan[37]] = 100000;
    std::uint16_t eobSingle;
    transforms::quantizeBHb(single, tables, scan, 64, 0, qc, dq, &eobSingle);
    CHECK(eobSingle == 38);
    // log_scale 1 and 2 paths against the oracle (the 32x32/64x64 rounding):
    // fresh non-aliased buffers
    std::int32_t coeff32[1024], qc32[1024], dq32[1024], qcOr32[1024], dqOr32[1024];
    std::int16_t scan32[1024];
    transforms::defaultScan32x32(scan32);
    for (int i = 0; i < 1024; ++i) {
        coeff32[i] = qh1::lcg(2 << 20) - (1 << 20);
    }
    std::uint16_t eob32, eobOr32;
    transforms::quantizeBHb(coeff32, tables, scan32, 1024, 1, qc32, dq32, &eob32);
    qh1::quantizeBOracle(coeff32, tables, scan32, 1024, 1, qcOr32, dqOr32, &eobOr32);
    bad = 0;
    for (int i = 0; i < 1024; ++i) {
        if (qc32[i] != qcOr32[i]) ++bad;
        if (dq32[i] != dqOr32[i]) ++bad;
    }
    CHECK(bad == 0);
    CHECK(eob32 == eobOr32);
    std::int32_t coeff64[4096], qc64[4096], dq64[4096], qcOr64[4096], dqOr64[4096];
    std::int16_t scan64[4096];
    transforms::defaultScan64x64(scan64);
    for (int i = 0; i < 4096; ++i) {
        coeff64[i] = qh1::lcg(2 << 20) - (1 << 20);
    }
    std::uint16_t eob64, eobOr64;
    transforms::quantizeBHb(coeff64, tables, scan64, 4096, 2, qc64, dq64, &eob64);
    qh1::quantizeBOracle(coeff64, tables, scan64, 4096, 2, qcOr64, dqOr64, &eobOr64);
    bad = 0;
    for (int i = 0; i < 4096; ++i) {
        if (qc64[i] != qcOr64[i]) ++bad;
        if (dq64[i] != dqOr64[i]) ++bad;
    }
    CHECK(bad == 0);
    CHECK(eob64 == eobOr64);
}

namespace qh1 {

// highbd_quantize_fp_helper_c (full_loop.c:329-397), qm/iqm NULL branch
// (:367-395), transcribed from the reference text. NO int16 clamp on the
// round add (the lowbd quantize_fp_helper_c clamps at :245); the threshold
// is (abs_coeff << (1 + log_scale)) >= dequant and the else branch zeroes
// qcoeff/dqcoeff per position (the scan is a permutation, so every position
// is written exactly once)
void quantizeFpOracle(const std::int32_t* coeff, const transforms::QuantTables& tables,
                      const std::int16_t* scan, int count, int logScale,
                      std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob) {
    const int shift = 16 - logScale;
    int eobVal = -1;
    const int logScaledRoundArr[2] = {
        (int)(((tables.roundFp[0] < 0 ? 0 : tables.roundFp[0]) + ((1 << logScale) >> 1)) >> logScale),
        (int)(((tables.roundFp[1] < 0 ? 0 : tables.roundFp[1]) + ((1 << logScale) >> 1)) >> logScale)};
    for (int i = 0; i < count; ++i) {
        const int rc = scan[i];
        const int rc01 = (rc != 0);
        const std::int32_t coeffVal = coeff[rc];
        const int coeffSign = coeffVal < 0 ? -1 : 0;
        const int absCoeff = (coeffVal ^ coeffSign) - coeffSign;
        if ((absCoeff << (1 + logScale)) >= tables.dequant[rc01]) {
            const std::int64_t tmp = (std::int64_t)absCoeff + logScaledRoundArr[rc01];
            const int absQcoeff = (int)((tmp * tables.quantFp[rc01]) >> shift);
            qcoeff[rc] = (absQcoeff ^ coeffSign) - coeffSign;
            const std::int32_t absDqcoeff =
                (std::int32_t)(((std::int64_t)absQcoeff * tables.dequant[rc01]) >> logScale);
            if (absQcoeff) {
                eobVal = i;
            }
            dqcoeff[rc] = (absDqcoeff ^ coeffSign) - coeffSign;
        } else {
            qcoeff[rc] = 0;
            dqcoeff[rc] = 0;
        }
    }
    *eob = static_cast<std::uint16_t>(eobVal + 1);
}

}  // namespace qh1

TEST_CASE("quantizeFpHb equals the landed quantizeFpN wherever the lowbd clamp is dead") {
    // derivation: identical tables (bd 8), coefficients with
    // abs_coeff + roundFp <= 32767 (roundFp <= (64*1336)>>7 = 668 at
    // qindex 255, so |coeff| <= 32000 keeps the lowbd clamp dead)
    struct FpCase { int n; int logScale; void (*scan)(std::int16_t*); void (*lowbd)(
        const std::int32_t*, const transforms::QuantTables&, const std::int16_t*, std::int32_t*,
        std::int32_t*, std::uint16_t*); };
    const FpCase cases[4] = {
        {16, 0, transforms::defaultScan4x4, transforms::quantizeFp4x4},
        {64, 0, transforms::defaultScan8x8, transforms::quantizeFp8x8},
        {256, 0, transforms::defaultScan16x16, transforms::quantizeFp16x16},
        {1024, 1, transforms::defaultScan32x32, transforms::quantizeFp32x32},
    };
    int bad = 0;
    for (int qi = 0; qi < 3; ++qi) {
        transforms::QuantTables tables;
        transforms::buildQuantTables(qh1::lcg(256), tables);
        for (int c = 0; c < 4; ++c) {
            std::int16_t scan[1024];
            cases[c].scan(scan);
            for (int trial = 0; trial < 4; ++trial) {
                std::int32_t coeff[1024];
                for (int i = 0; i < cases[c].n; ++i) {
                    coeff[i] = qh1::lcg(64001) - 32000;
                }
                std::int32_t qcHb[1024], qcLb[1024], dqHb[1024], dqLb[1024], qcOr[1024], dqOr[1024];
                std::uint16_t eobHb, eobLb, eobOr;
                transforms::quantizeFpHb(coeff, tables, scan, cases[c].n, cases[c].logScale, qcHb,
                                         dqHb, &eobHb);
                cases[c].lowbd(coeff, tables, scan, qcLb, dqLb, &eobLb);
                qh1::quantizeFpOracle(coeff, tables, scan, cases[c].n, cases[c].logScale, qcOr,
                                      dqOr, &eobOr);
                for (int i = 0; i < cases[c].n; ++i) {
                    if (qcHb[i] != qcLb[i]) ++bad;
                    if (dqHb[i] != dqLb[i]) ++bad;
                    if (qcHb[i] != qcOr[i]) ++bad;
                    if (dqHb[i] != dqOr[i]) ++bad;
                }
                if (eobHb != eobLb) ++bad;
                if (eobHb != eobOr) ++bad;
            }
        }
    }
    CHECK(bad == 0);
}

TEST_CASE("quantizeFpHb drops the lowbd INT16 clamp - the divergence is load-bearing") {
    // at qindex 255 bd 8: roundFp = (64*1336)>>7 = 668, so a coefficient of
    // 33000 pushes abs_coeff + roundFp to 33668 - the lowbd quantize_fp
    // clamps it to 32767 (full_loop.c:245) while the highbd path (:382) does
    // NOT; the quant pipeline then divides both down, so the observable is
    // the strict magnitude ordering
    transforms::QuantTables tables;
    transforms::buildQuantTables(255, tables);
    std::int16_t scan[16];
    transforms::defaultScan4x4(scan);
    std::int32_t coeff[16] = {0};
    coeff[0] = 33000;
    std::int32_t qcHb[16], qcLb[16], dqHb[16], dqLb[16], qcOr[16], dqOr[16];
    std::uint16_t eobHb, eobLb, eobOr;
    transforms::quantizeFpHb(coeff, tables, scan, 16, 0, qcHb, dqHb, &eobHb);
    transforms::quantizeFp4x4(coeff, tables, scan, qcLb, dqLb, &eobLb);
    qh1::quantizeFpOracle(coeff, tables, scan, 16, 0, qcOr, dqOr, &eobOr);
    CHECK(qcHb[0] == qcOr[0]);
    CHECK(qcHb[0] != qcLb[0]);
    CHECK(qcLb[0] < qcHb[0]);
    CHECK(dqHb[0] == dqOr[0]);
    CHECK(eobHb == 1);
}

TEST_CASE("quantizeFpHb saturation and the log_scale 2 path") {
    transforms::QuantTables tables;
    transforms::buildQuantTables(255, tables);
    std::int16_t scan[64];
    transforms::defaultScan8x8(scan);
    // adversarial saturation: magnitudes up to 2^24 keep the reference's own
    // (abs_coeff << (1 + log_scale)) shift inside int32 at every log_scale
    std::int32_t coeff[64];
    const std::int32_t big[8] = {1 << 24, -(1 << 24), 16777213, -16777213,
                                 tables.dequant[1], -tables.dequant[1], tables.dequant[1] - 1,
                                 -(tables.dequant[1] - 1)};
    for (int i = 0; i < 64; ++i) {
        coeff[i] = big[i % 8];
    }
    std::int32_t qc[64], dq[64], qcOr[64], dqOr[64];
    std::uint16_t eob, eobOr;
    transforms::quantizeFpHb(coeff, tables, scan, 64, 0, qc, dq, &eob);
    qh1::quantizeFpOracle(coeff, tables, scan, 64, 0, qcOr, dqOr, &eobOr);
    int bad = 0;
    for (int i = 0; i < 64; ++i) {
        if (qc[i] != qcOr[i]) ++bad;
        if (dq[i] != dqOr[i]) ++bad;
    }
    CHECK(bad == 0);
    CHECK(eob == eobOr);
    // all-zero block: eob 0
    std::int32_t zero[64] = {0};
    std::uint16_t eobZero;
    transforms::quantizeFpHb(zero, tables, scan, 64, 0, qc, dq, &eobZero);
    CHECK(eobZero == 0);
    // log_scale 2 at 64x64 against the oracle
    std::int32_t coeff64[4096], qc64[4096], dq64[4096], qcOr64[4096], dqOr64[4096];
    std::int16_t scan64[4096];
    transforms::defaultScan64x64(scan64);
    for (int i = 0; i < 4096; ++i) {
        coeff64[i] = qh1::lcg(2 << 20) - (1 << 20);
    }
    std::uint16_t eob64, eobOr64;
    transforms::quantizeFpHb(coeff64, tables, scan64, 4096, 2, qc64, dq64, &eob64);
    qh1::quantizeFpOracle(coeff64, tables, scan64, 4096, 2, qcOr64, dqOr64, &eobOr64);
    bad = 0;
    for (int i = 0; i < 4096; ++i) {
        if (qc64[i] != qcOr64[i]) ++bad;
        if (dq64[i] != dqOr64[i]) ++bad;
    }
    CHECK(bad == 0);
    CHECK(eob64 == eobOr64);
}
