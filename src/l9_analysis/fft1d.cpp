// AN12: the 1D FFT kernels, the first Phase-B slice.
// 1:1 ports of the SVT-AV1 1D FFT macros GEN_FFT_2/4/8/16/32
// (third_party/SVT-AV1/Source/Lib/Codec/fft_common.h:113-414). The macros
// are expanded verbatim for the host float path: T_VEC is float, load(x)
// is the point read, store(p, v) is the point write, and the add/sub/mul
// helpers are the float operators - so every expression below is the
// reference float expression graph, evaluated under the same IEEE-754
// single precision. THIS IS A FLOAT SLICE - the first in the layer; every
// previous l9 port was fixed-point integer math with bit-exactness by
// construction, here the contract is the reference's own float rounding,
// settled by the tolerance discipline in tests/test_fft1d.cpp (epsilon,
// documented there, named here to keep the two files pointing at the
// same decision).
//
// Real-input transform: n floats in, n floats out packed as
// [r_0..r_{n/2}, i_1..i_{n/2-1}] (fft_common.h:24-27); the consumer
// fft.c (AN13's driver) assembles the 2D transform on top of these.
// The 2D driver itself is NOT this slice (AN13).

#include "analysis.h"

#include <cstddef>

namespace analysis {

// 1:1 port of svt_aom_fft1d_2_float (fft_common.h:113-119, macro
// GEN_FFT_2 with the float instantiation fft.c:82).
void fft1d2(const float* input, float* output, std::int32_t stride) {
    const float i0 = input[0 * stride];
    const float i1 = input[1 * stride];
    output[0 * stride] = i0 + i1;
    output[1 * stride] = i0 - i1;
}

// 1:1 port of svt_aom_fft1d_4_float (fft_common.h:121-136, macro
// GEN_FFT_4 with the float instantiation fft.c:83).
void fft1d4(const float* input, float* output, std::int32_t stride) {
    const float k_weight0 = 0.0f;
    const float i0 = input[0 * stride];
    const float i1 = input[1 * stride];
    const float i2 = input[2 * stride];
    const float i3 = input[3 * stride];
    const float w0 = i0 + i2;
    const float w1 = i0 - i2;
    const float w2 = i1 + i3;
    const float w3 = i1 - i3;
    output[0 * stride] = w0 + w2;
    output[1 * stride] = w1;
    output[2 * stride] = w0 - w2;
    output[3 * stride] = k_weight0 - w3;
}

// 1:1 port of svt_aom_fft1d_8_float (fft_common.h:138-170, macro
// GEN_FFT_8 with the float instantiation fft.c:84).
void fft1d8(const float* input, float* output, std::int32_t stride) {
    const float k_weight0 = 0.0f;
    const float k_weight2 = 0.707107f;
    const float i0 = input[0 * stride];
    const float i1 = input[1 * stride];
    const float i2 = input[2 * stride];
    const float i3 = input[3 * stride];
    const float i4 = input[4 * stride];
    const float i5 = input[5 * stride];
    const float i6 = input[6 * stride];
    const float i7 = input[7 * stride];
    const float w0 = i0 + i4;
    const float w1 = i0 - i4;
    const float w2 = i2 + i6;
    const float w3 = i2 - i6;
    const float w4 = w0 + w2;
    const float w5 = w0 - w2;
    const float w7 = i1 + i5;
    const float w8 = i1 - i5;
    const float w9 = i3 + i7;
    const float w10 = i3 - i7;
    const float w11 = w7 + w9;
    const float w12 = w7 - w9;
    output[0 * stride] = w4 + w11;
    output[1 * stride] = w1 + k_weight2 * (w8 - w10);
    output[2 * stride] = w5;
    output[3 * stride] = w1 - k_weight2 * (w8 - w10);
    output[4 * stride] = w4 - w11;
    output[5 * stride] = (k_weight0 - w3) - k_weight2 * (w10 + w8);
    output[6 * stride] = k_weight0 - w12;
    output[7 * stride] = w3 - k_weight2 * (w10 + w8);
}

// 1:1 port of svt_aom_fft1d_16_float (fft_common.h:172-244, macro
// GEN_FFT_16 with the float instantiation fft.c:85).
void fft1d16(const float* input, float* output, std::int32_t stride) {
    const float k_weight0 = 0.0f;
    const float k_weight2 = 0.707107f;
    const float k_weight3 = 0.92388f;
    const float k_weight4 = 0.382683f;
    const float i0 = input[0 * stride];
    const float i1 = input[1 * stride];
    const float i2 = input[2 * stride];
    const float i3 = input[3 * stride];
    const float i4 = input[4 * stride];
    const float i5 = input[5 * stride];
    const float i6 = input[6 * stride];
    const float i7 = input[7 * stride];
    const float i8 = input[8 * stride];
    const float i9 = input[9 * stride];
    const float i10 = input[10 * stride];
    const float i11 = input[11 * stride];
    const float i12 = input[12 * stride];
    const float i13 = input[13 * stride];
    const float i14 = input[14 * stride];
    const float i15 = input[15 * stride];
    const float w0 = i0 + i8;
    const float w1 = i0 - i8;
    const float w2 = i4 + i12;
    const float w3 = i4 - i12;
    const float w4 = w0 + w2;
    const float w5 = w0 - w2;
    const float w7 = i2 + i10;
    const float w8 = i2 - i10;
    const float w9 = i6 + i14;
    const float w10 = i6 - i14;
    const float w11 = w7 + w9;
    const float w12 = w7 - w9;
    const float w14 = w4 + w11;
    const float w15 = w4 - w11;
    const float w16[2] = {w1 + k_weight2 * (w8 - w10),
                          (k_weight0 - w3) - k_weight2 * (w10 + w8)};
    const float w18[2] = {w1 - k_weight2 * (w8 - w10),
                          w3 - k_weight2 * (w10 + w8)};
    const float w19 = i1 + i9;
    const float w20 = i1 - i9;
    const float w21 = i5 + i13;
    const float w22 = i5 - i13;
    const float w23 = w19 + w21;
    const float w24 = w19 - w21;
    const float w26 = i3 + i11;
    const float w27 = i3 - i11;
    const float w28 = i7 + i15;
    const float w29 = i7 - i15;
    const float w30 = w26 + w28;
    const float w31 = w26 - w28;
    const float w33 = w23 + w30;
    const float w34 = w23 - w30;
    const float w35[2] = {w20 + k_weight2 * (w27 - w29),
                          (k_weight0 - w22) - k_weight2 * (w29 + w27)};
    const float w37[2] = {w20 - k_weight2 * (w27 - w29),
                          w22 - k_weight2 * (w29 + w27)};
    output[0 * stride] = w14 + w33;
    output[1 * stride] = w16[0] + k_weight3 * w35[0] + k_weight4 * w35[1];
    output[2 * stride] = w5 + k_weight2 * (w24 - w31);
    output[3 * stride] = w18[0] + k_weight4 * w37[0] + k_weight3 * w37[1];
    output[4 * stride] = w15;
    output[5 * stride] = w18[0] + (k_weight0 - k_weight4 * w37[0]) -
                          k_weight3 * w37[1];
    output[6 * stride] = w5 - k_weight2 * (w24 - w31);
    output[7 * stride] = w16[0] + (k_weight0 - k_weight3 * w35[0]) -
                         k_weight4 * w35[1];
    output[8 * stride] = w14 - w33;
    output[9 * stride] = w16[1] + k_weight3 * w35[1] - k_weight4 * w35[0];
    output[10 * stride] = (k_weight0 - w12) - k_weight2 * (w31 + w24);
    output[11 * stride] = w18[1] + k_weight4 * w37[1] - k_weight3 * w37[0];
    output[12 * stride] = k_weight0 - w34;
    output[13 * stride] = (k_weight0 - w18[1]) -
                          (k_weight3 * w37[0] - k_weight4 * w37[1]);
    output[14 * stride] = w12 - k_weight2 * (w31 + w24);
    output[15 * stride] = (k_weight0 - w16[1]) -
                          (k_weight4 * w35[0] - k_weight3 * w35[1]);
}

// 1:1 port of svt_aom_fft1d_32_float (fft_common.h:246-414, macro
// GEN_FFT_32 with the float instantiation fft.c:86).
void fft1d32(const float* input, float* output, std::int32_t stride) {
    const float k_weight0 = 0.0f;
    const float k_weight2 = 0.707107f;
    const float k_weight3 = 0.92388f;
    const float k_weight4 = 0.382683f;
    const float k_weight5 = 0.980785f;
    const float k_weight6 = 0.19509f;
    const float k_weight7 = 0.83147f;
    const float k_weight8 = 0.55557f;
    const float i0 = input[0 * stride];
    const float i1 = input[1 * stride];
    const float i2 = input[2 * stride];
    const float i3 = input[3 * stride];
    const float i4 = input[4 * stride];
    const float i5 = input[5 * stride];
    const float i6 = input[6 * stride];
    const float i7 = input[7 * stride];
    const float i8 = input[8 * stride];
    const float i9 = input[9 * stride];
    const float i10 = input[10 * stride];
    const float i11 = input[11 * stride];
    const float i12 = input[12 * stride];
    const float i13 = input[13 * stride];
    const float i14 = input[14 * stride];
    const float i15 = input[15 * stride];
    const float i16 = input[16 * stride];
    const float i17 = input[17 * stride];
    const float i18 = input[18 * stride];
    const float i19 = input[19 * stride];
    const float i20 = input[20 * stride];
    const float i21 = input[21 * stride];
    const float i22 = input[22 * stride];
    const float i23 = input[23 * stride];
    const float i24 = input[24 * stride];
    const float i25 = input[25 * stride];
    const float i26 = input[26 * stride];
    const float i27 = input[27 * stride];
    const float i28 = input[28 * stride];
    const float i29 = input[29 * stride];
    const float i30 = input[30 * stride];
    const float i31 = input[31 * stride];
    const float w0 = i0 + i16;
    const float w1 = i0 - i16;
    const float w2 = i8 + i24;
    const float w3 = i8 - i24;
    const float w4 = w0 + w2;
    const float w5 = w0 - w2;
    const float w7 = i4 + i20;
    const float w8 = i4 - i20;
    const float w9 = i12 + i28;
    const float w10 = i12 - i28;
    const float w11 = w7 + w9;
    const float w12 = w7 - w9;
    const float w14 = w4 + w11;
    const float w15 = w4 - w11;
    const float w16[2] = {w1 + k_weight2 * (w8 - w10),
                          (k_weight0 - w3) - k_weight2 * (w10 + w8)};
    const float w18[2] = {w1 - k_weight2 * (w8 - w10),
                          w3 - k_weight2 * (w10 + w8)};
    const float w19 = i2 + i18;
    const float w20 = i2 - i18;
    const float w21 = i10 + i26;
    const float w22 = i10 - i26;
    const float w23 = w19 + w21;
    const float w24 = w19 - w21;
    const float w26 = i6 + i22;
    const float w27 = i6 - i22;
    const float w28 = i14 + i30;
    const float w29 = i14 - i30;
    const float w30 = w26 + w28;
    const float w31 = w26 - w28;
    const float w33 = w23 + w30;
    const float w34 = w23 - w30;
    const float w35[2] = {w20 + k_weight2 * (w27 - w29),
                          (k_weight0 - w22) - k_weight2 * (w29 + w27)};
    const float w37[2] = {w20 - k_weight2 * (w27 - w29),
                          w22 - k_weight2 * (w29 + w27)};
    const float w38 = w14 + w33;
    const float w39 = w14 - w33;
    const float w40[2] = {w16[0] + k_weight3 * w35[0] + k_weight4 * w35[1],
                          w16[1] + k_weight3 * w35[1] - k_weight4 * w35[0]};
    const float w41[2] = {w5 + k_weight2 * (w24 - w31),
                          (k_weight0 - w12) - k_weight2 * (w31 + w24)};
    const float w42[2] = {w18[0] + k_weight4 * w37[0] + k_weight3 * w37[1],
                          w18[1] + k_weight4 * w37[1] - k_weight3 * w37[0]};
    const float w44[2] = {w18[0] + (k_weight0 - k_weight4 * w37[0]) -
                              k_weight3 * w37[1],
                          (k_weight0 - w18[1]) -
                              (k_weight3 * w37[0] - k_weight4 * w37[1])};
    const float w45[2] = {w5 - k_weight2 * (w24 - w31),
                          w12 - k_weight2 * (w31 + w24)};
    const float w46[2] = {w16[0] + (k_weight0 - k_weight3 * w35[0]) -
                              k_weight4 * w35[1],
                          (k_weight0 - w16[1]) -
                              (k_weight4 * w35[0] - k_weight3 * w35[1])};
    const float w47 = i1 + i17;
    const float w48 = i1 - i17;
    const float w49 = i9 + i25;
    const float w50 = i9 - i25;
    const float w51 = w47 + w49;
    const float w52 = w47 - w49;
    const float w54 = i5 + i21;
    const float w55 = i5 - i21;
    const float w56 = i13 + i29;
    const float w57 = i13 - i29;
    const float w58 = w54 + w56;
    const float w59 = w54 - w56;
    const float w61 = w51 + w58;
    const float w62 = w51 - w58;
    const float w63[2] = {w48 + k_weight2 * (w55 - w57),
                          (k_weight0 - w50) - k_weight2 * (w57 + w55)};
    const float w65[2] = {w48 - k_weight2 * (w55 - w57),
                          w50 - k_weight2 * (w57 + w55)};
    const float w66 = i3 + i19;
    const float w67 = i3 - i19;
    const float w68 = i11 + i27;
    const float w69 = i11 - i27;
    const float w70 = w66 + w68;
    const float w71 = w66 - w68;
    const float w73 = i7 + i23;
    const float w74 = i7 - i23;
    const float w75 = i15 + i31;
    const float w76 = i15 - i31;
    const float w77 = w73 + w75;
    const float w78 = w73 - w75;
    const float w80 = w70 + w77;
    const float w81 = w70 - w77;
    const float w82[2] = {w67 + k_weight2 * (w74 - w76),
                          (k_weight0 - w69) - k_weight2 * (w76 + w74)};
    const float w84[2] = {w67 - k_weight2 * (w74 - w76),
                          w69 - k_weight2 * (w76 + w74)};
    const float w85 = w61 + w80;
    const float w86 = w61 - w80;
    const float w87[2] = {w63[0] + k_weight3 * w82[0] + k_weight4 * w82[1],
                          w63[1] + k_weight3 * w82[1] - k_weight4 * w82[0]};
    const float w88[2] = {w52 + k_weight2 * (w71 - w78),
                          (k_weight0 - w59) - k_weight2 * (w78 + w71)};
    const float w89[2] = {w65[0] + k_weight4 * w84[0] + k_weight3 * w84[1],
                          w65[1] + k_weight4 * w84[1] - k_weight3 * w84[0]};
    const float w91[2] = {w65[0] + (k_weight0 - k_weight4 * w84[0]) -
                              k_weight3 * w84[1],
                          (k_weight0 - w65[1]) -
                              (k_weight3 * w84[0] - k_weight4 * w84[1])};
    const float w92[2] = {w52 - k_weight2 * (w71 - w78),
                          w59 - k_weight2 * (w78 + w71)};
    const float w93[2] = {w63[0] + (k_weight0 - k_weight3 * w82[0]) -
                              k_weight4 * w82[1],
                          (k_weight0 - w63[1]) -
                              (k_weight4 * w82[0] - k_weight3 * w82[1])};
    output[0 * stride] = w38 + w85;
    output[1 * stride] = w40[0] + k_weight5 * w87[0] + k_weight6 * w87[1];
    output[2 * stride] = w41[0] + k_weight3 * w88[0] + k_weight4 * w88[1];
    output[3 * stride] = w42[0] + k_weight7 * w89[0] + k_weight8 * w89[1];
    output[4 * stride] = w15 + k_weight2 * (w62 - w81);
    output[5 * stride] = w44[0] + k_weight8 * w91[0] + k_weight7 * w91[1];
    output[6 * stride] = w45[0] + k_weight4 * w92[0] + k_weight3 * w92[1];
    output[7 * stride] = w46[0] + k_weight6 * w93[0] + k_weight5 * w93[1];
    output[8 * stride] = w39;
    output[9 * stride] = w46[0] + (k_weight0 - k_weight6 * w93[0]) -
                         k_weight5 * w93[1];
    output[10 * stride] = w45[0] + (k_weight0 - k_weight4 * w92[0]) -
                          k_weight3 * w92[1];
    output[11 * stride] = w44[0] + (k_weight0 - k_weight8 * w91[0]) -
                          k_weight7 * w91[1];
    output[12 * stride] = w15 - k_weight2 * (w62 - w81);
    output[13 * stride] = w42[0] + (k_weight0 - k_weight7 * w89[0]) -
                          k_weight8 * w89[1];
    output[14 * stride] = w41[0] + (k_weight0 - k_weight3 * w88[0]) -
                          k_weight4 * w88[1];
    output[15 * stride] = w40[0] + (k_weight0 - k_weight5 * w87[0]) -
                          k_weight6 * w87[1];
    output[16 * stride] = w38 - w85;
    output[17 * stride] = w40[1] + k_weight5 * w87[1] - k_weight6 * w87[0];
    output[18 * stride] = w41[1] + k_weight3 * w88[1] - k_weight4 * w88[0];
    output[19 * stride] = w42[1] + k_weight7 * w89[1] - k_weight8 * w89[0];
    output[20 * stride] = (k_weight0 - w34) - k_weight2 * (w81 + w62);
    output[21 * stride] = w44[1] + k_weight8 * w91[1] - k_weight7 * w91[0];
    output[22 * stride] = w45[1] + k_weight4 * w92[1] - k_weight3 * w92[0];
    output[23 * stride] = w46[1] + k_weight6 * w93[1] - k_weight5 * w93[0];
    output[24 * stride] = k_weight0 - w86;
    output[25 * stride] = (k_weight0 - w46[1]) -
                          (k_weight5 * w93[0] - k_weight6 * w93[1]);
    output[26 * stride] = (k_weight0 - w45[1]) -
                          (k_weight3 * w92[0] - k_weight4 * w92[1]);
    output[27 * stride] = (k_weight0 - w44[1]) -
                          (k_weight7 * w91[0] - k_weight8 * w91[1]);
    output[28 * stride] = w34 - k_weight2 * (w81 + w62);
    output[29 * stride] = (k_weight0 - w42[1]) -
                          (k_weight8 * w89[0] - k_weight7 * w89[1]);
    output[30 * stride] = (k_weight0 - w41[1]) -
                          (k_weight4 * w88[0] - k_weight3 * w88[1]);
    output[31 * stride] = (k_weight0 - w40[1]) -
                          (k_weight6 * w87[0] - k_weight5 * w87[1]);
}

// 1:1 port of svt_aom_ifft1d_2_float (fft_common.h:416-422, macro
// GEN_IFFT_2 with the float instantiation fft.c:173).
void ifft1d2(const float* input, float* output, std::int32_t stride) {
    const float i0 = input[0 * stride];
    const float i1 = input[1 * stride];
    output[0 * stride] = i0 + i1;
    output[1 * stride] = i0 - i1;
}

// 1:1 port of svt_aom_ifft1d_4_float (fft_common.h:424-439, macro
// GEN_IFFT_4 with the float instantiation fft.c:174).
void ifft1d4(const float* input, float* output, std::int32_t stride) {
    const float k_weight0 = 0.0f;
    const float i0 = input[0 * stride];
    const float i1 = input[1 * stride];
    const float i2 = input[2 * stride];
    const float i3 = input[3 * stride];
    const float w2 = i0 + i2;
    const float w3 = i0 - i2;
    const float w4[2] = {i1 + i1, i3 - i3};
    const float w5[2] = {i1 - i1, (k_weight0 - i3) - i3};
    output[0 * stride] = w2 + w4[0];
    output[1 * stride] = w3 + w5[1];
    output[2 * stride] = w2 - w4[0];
    output[3 * stride] = w3 - w5[1];
}

// 1:1 port of svt_aom_ifft1d_8_float (fft_common.h:441-477, macro
// GEN_IFFT_8 with the float instantiation fft.c:175).
void ifft1d8(const float* input, float* output, std::int32_t stride) {
    const float k_weight0 = 0.0f;
    const float k_weight2 = 0.707107f;
    const float i0 = input[0 * stride];
    const float i1 = input[1 * stride];
    const float i2 = input[2 * stride];
    const float i3 = input[3 * stride];
    const float i4 = input[4 * stride];
    const float i5 = input[5 * stride];
    const float i6 = input[6 * stride];
    const float i7 = input[7 * stride];
    const float w6 = i0 + i4;
    const float w7 = i0 - i4;
    const float w8[2] = {i2 + i2, i6 - i6};
    const float w9[2] = {i2 - i2, (k_weight0 - i6) - i6};
    const float w10[2] = {w6 + w8[0], w8[1]};
    const float w11[2] = {w6 - w8[0], k_weight0 - w8[1]};
    const float w12[2] = {w7 + w9[1], k_weight0 - w9[0]};
    const float w13[2] = {w7 - w9[1], w9[0]};
    const float w14[2] = {i1 + i3, i7 - i5};
    const float w15[2] = {i1 - i3, (k_weight0 - i5) - i7};
    const float w16[2] = {i3 + i1, i5 - i7};
    const float w17[2] = {i3 - i1, (k_weight0 - i7) - i5};
    const float w18[2] = {w14[0] + w16[0], w14[1] + w16[1]};
    const float w19[2] = {w14[0] - w16[0], w14[1] - w16[1]};
    const float w20[2] = {w15[0] + w17[1], w15[1] - w17[0]};
    const float w21[2] = {w15[0] - w17[1], w15[1] + w17[0]};
    output[0 * stride] = w10[0] + w18[0];
    output[1 * stride] = w12[0] + k_weight2 * (w20[0] + w20[1]);
    output[2 * stride] = w11[0] + w19[1];
    output[3 * stride] = w13[0] - k_weight2 * (w21[0] - w21[1]);
    output[4 * stride] = w10[0] - w18[0];
    output[5 * stride] = w12[0] + (k_weight0 - k_weight2 * w20[0]) -
                         k_weight2 * w20[1];
    output[6 * stride] = w11[0] - w19[1];
    output[7 * stride] = w13[0] + k_weight2 * (w21[0] - w21[1]);
}

// 1:1 port of svt_aom_ifft1d_16_float (fft_common.h:479-573, macro
// GEN_IFFT_16 with the float instantiation fft.c:176). Stores 4 and 12
// consume w31[0]/w55[1] verbatim - the reference's own expression graph,
// kept exactly (north star: the pinned reference, quirks included).
void ifft1d16(const float* input, float* output, std::int32_t stride) {
    const float k_weight0 = 0.0f;
    const float k_weight2 = 0.707107f;
    const float k_weight3 = 0.92388f;
    const float k_weight4 = 0.382683f;
    const float i0 = input[0 * stride];
    const float i1 = input[1 * stride];
    const float i2 = input[2 * stride];
    const float i3 = input[3 * stride];
    const float i4 = input[4 * stride];
    const float i5 = input[5 * stride];
    const float i6 = input[6 * stride];
    const float i7 = input[7 * stride];
    const float i8 = input[8 * stride];
    const float i9 = input[9 * stride];
    const float i10 = input[10 * stride];
    const float i11 = input[11 * stride];
    const float i12 = input[12 * stride];
    const float i13 = input[13 * stride];
    const float i14 = input[14 * stride];
    const float i15 = input[15 * stride];
    const float w14 = i0 + i8;
    const float w15 = i0 - i8;
    const float w16[2] = {i4 + i4, i12 - i12};
    const float w17[2] = {i4 - i4, (k_weight0 - i12) - i12};
    const float w18[2] = {w14 + w16[0], w16[1]};
    const float w19[2] = {w14 - w16[0], k_weight0 - w16[1]};
    const float w20[2] = {w15 + w17[1], k_weight0 - w17[0]};
    const float w21[2] = {w15 - w17[1], w17[0]};
    const float w22[2] = {i2 + i6, i14 - i10};
    const float w23[2] = {i2 - i6, (k_weight0 - i10) - i14};
    const float w24[2] = {i6 + i2, i10 - i14};
    const float w25[2] = {i6 - i2, (k_weight0 - i14) - i10};
    const float w26[2] = {w22[0] + w24[0], w22[1] + w24[1]};
    const float w27[2] = {w22[0] - w24[0], w22[1] - w24[1]};
    const float w28[2] = {w23[0] + w25[1], w23[1] - w25[0]};
    const float w29[2] = {w23[0] - w25[1], w23[1] + w25[0]};
    const float w30[2] = {w18[0] + w26[0], w18[1] + w26[1]};
    const float w31[2] = {w18[0] - w26[0], w18[1] - w26[1]};
    const float w32[2] = {w20[0] + k_weight2 * (w28[0] + w28[1]),
                          w20[1] + k_weight2 * (w28[1] - w28[0])};
    const float w33[2] = {w20[0] + (k_weight0 - k_weight2 * w28[0]) -
                              k_weight2 * w28[1],
                          w20[1] + k_weight2 * (w28[0] - w28[1])};
    const float w34[2] = {w19[0] + w27[1], w19[1] - w27[0]};
    const float w35[2] = {w19[0] - w27[1], w19[1] + w27[0]};
    const float w36[2] = {w21[0] - k_weight2 * (w29[0] - w29[1]),
                          w21[1] - k_weight2 * (w29[1] + w29[0])};
    const float w37[2] = {w21[0] + k_weight2 * (w29[0] - w29[1]),
                          w21[1] + k_weight2 * (w29[1] + w29[0])};
    const float w38[2] = {i1 + i7, i15 - i9};
    const float w39[2] = {i1 - i7, (k_weight0 - i9) - i15};
    const float w40[2] = {i5 + i3, i11 - i13};
    const float w41[2] = {i5 - i3, (k_weight0 - i13) - i11};
    const float w42[2] = {w38[0] + w40[0], w38[1] + w40[1]};
    const float w43[2] = {w38[0] - w40[0], w38[1] - w40[1]};
    const float w44[2] = {w39[0] + w41[1], w39[1] - w41[0]};
    const float w45[2] = {w39[0] - w41[1], w39[1] + w41[0]};
    const float w46[2] = {i3 + i5, i13 - i11};
    const float w47[2] = {i3 - i5, (k_weight0 - i11) - i13};
    const float w48[2] = {i7 + i1, i9 - i15};
    const float w49[2] = {i7 - i1, (k_weight0 - i15) - i9};
    const float w50[2] = {w46[0] + w48[0], w46[1] + w48[1]};
    const float w51[2] = {w46[0] - w48[0], w46[1] - w48[1]};
    const float w52[2] = {w47[0] + w49[1], w47[1] - w49[0]};
    const float w53[2] = {w47[0] - w49[1], w47[1] + w49[0]};
    const float w54[2] = {w42[0] + w50[0], w42[1] + w50[1]};
    const float w55[2] = {w42[0] - w50[0], w42[1] - w50[1]};
    const float w56[2] = {w44[0] + k_weight2 * (w52[0] + w52[1]),
                          w44[1] + k_weight2 * (w52[1] - w52[0])};
    const float w57[2] = {w44[0] + (k_weight0 - k_weight2 * w52[0]) -
                              k_weight2 * w52[1],
                          w44[1] + k_weight2 * (w52[0] - w52[1])};
    const float w58[2] = {w43[0] + w51[1], w43[1] - w51[0]};
    const float w59[2] = {w43[0] - w51[1], w43[1] + w51[0]};
    const float w60[2] = {w45[0] - k_weight2 * (w53[0] - w53[1]),
                          w45[1] - k_weight2 * (w53[1] + w53[0])};
    const float w61[2] = {w45[0] + k_weight2 * (w53[0] - w53[1]),
                          w45[1] + k_weight2 * (w53[1] + w53[0])};
    output[0 * stride] = w30[0] + w54[0];
    output[1 * stride] = w32[0] + k_weight3 * w56[0] + k_weight4 * w56[1];
    output[2 * stride] = w34[0] + k_weight2 * (w58[0] + w58[1]);
    output[3 * stride] = w36[0] + k_weight4 * w60[0] + k_weight3 * w60[1];
    output[4 * stride] = w31[0] + w55[1];
    output[5 * stride] = w33[0] - (k_weight4 * w57[0] - k_weight3 * w57[1]);
    output[6 * stride] = w35[0] - k_weight2 * (w59[0] - w59[1]);
    output[7 * stride] = w37[0] - (k_weight3 * w61[0] - k_weight4 * w61[1]);
    output[8 * stride] = w30[0] - w54[0];
    output[9 * stride] = w32[0] + (k_weight0 - k_weight3 * w56[0]) -
                         k_weight4 * w56[1];
    output[10 * stride] = w34[0] + (k_weight0 - k_weight2 * w58[0]) -
                          k_weight2 * w58[1];
    output[11 * stride] = w36[0] + (k_weight0 - k_weight4 * w60[0]) -
                          k_weight3 * w60[1];
    output[12 * stride] = w31[0] - w55[1];
    output[13 * stride] = w33[0] + (k_weight4 * w57[0] - k_weight3 * w57[1]);
    output[14 * stride] = w35[0] + k_weight2 * (w59[0] - w59[1]);
    output[15 * stride] = w37[0] + (k_weight3 * w61[0] - k_weight4 * w61[1]);
}

// 1:1 port of svt_aom_ifft1d_32_float (fft_common.h:574-823, macro
// GEN_IFFT_32 with the float instantiation fft.c:177).
void ifft1d32(const float* input, float* output, std::int32_t stride) {
    const float k_weight0 = 0.0f;
    const float k_weight2 = 0.707107f;
    const float k_weight3 = 0.92388f;
    const float k_weight4 = 0.382683f;
    const float k_weight5 = 0.980785f;
    const float k_weight6 = 0.19509f;
    const float k_weight7 = 0.83147f;
    const float k_weight8 = 0.55557f;
    const float i0 = input[0 * stride];
    const float i1 = input[1 * stride];
    const float i2 = input[2 * stride];
    const float i3 = input[3 * stride];
    const float i4 = input[4 * stride];
    const float i5 = input[5 * stride];
    const float i6 = input[6 * stride];
    const float i7 = input[7 * stride];
    const float i8 = input[8 * stride];
    const float i9 = input[9 * stride];
    const float i10 = input[10 * stride];
    const float i11 = input[11 * stride];
    const float i12 = input[12 * stride];
    const float i13 = input[13 * stride];
    const float i14 = input[14 * stride];
    const float i15 = input[15 * stride];
    const float i16 = input[16 * stride];
    const float i17 = input[17 * stride];
    const float i18 = input[18 * stride];
    const float i19 = input[19 * stride];
    const float i20 = input[20 * stride];
    const float i21 = input[21 * stride];
    const float i22 = input[22 * stride];
    const float i23 = input[23 * stride];
    const float i24 = input[24 * stride];
    const float i25 = input[25 * stride];
    const float i26 = input[26 * stride];
    const float i27 = input[27 * stride];
    const float i28 = input[28 * stride];
    const float i29 = input[29 * stride];
    const float i30 = input[30 * stride];
    const float i31 = input[31 * stride];
    const float w30 = i0 + i16;
    const float w31 = i0 - i16;
    const float w32[2] = {i8 + i8, i24 - i24};
    const float w33[2] = {i8 - i8, (k_weight0 - i24) - i24};
    const float w34[2] = {w30 + w32[0], w32[1]};
    const float w35[2] = {w30 - w32[0], k_weight0 - w32[1]};
    const float w36[2] = {w31 + w33[1], k_weight0 - w33[0]};
    const float w37[2] = {w31 - w33[1], w33[0]};
    const float w38[2] = {i4 + i12, i28 - i20};
    const float w39[2] = {i4 - i12, (k_weight0 - i20) - i28};
    const float w40[2] = {i12 + i4, i20 - i28};
    const float w41[2] = {i12 - i4, (k_weight0 - i28) - i20};
    const float w42[2] = {w38[0] + w40[0], w38[1] + w40[1]};
    const float w43[2] = {w38[0] - w40[0], w38[1] - w40[1]};
    const float w44[2] = {w39[0] + w41[1], w39[1] - w41[0]};
    const float w45[2] = {w39[0] - w41[1], w39[1] + w41[0]};
    const float w46[2] = {w34[0] + w42[0], w34[1] + w42[1]};
    const float w47[2] = {w34[0] - w42[0], w34[1] - w42[1]};
    const float w48[2] = {w36[0] + k_weight2 * (w44[0] + w44[1]),
                          w36[1] + k_weight2 * (w44[1] - w44[0])};
    const float w49[2] = {w36[0] + (k_weight0 - k_weight2 * w44[0]) -
                              k_weight2 * w44[1],
                          w36[1] + k_weight2 * (w44[0] - w44[1])};
    const float w50[2] = {w35[0] + w43[1], w35[1] - w43[0]};
    const float w51[2] = {w35[0] - w43[1], w35[1] + w43[0]};
    const float w52[2] = {w37[0] - k_weight2 * (w45[0] - w45[1]),
                          w37[1] - k_weight2 * (w45[1] + w45[0])};
    const float w53[2] = {w37[0] + k_weight2 * (w45[0] - w45[1]),
                          w37[1] + k_weight2 * (w45[1] + w45[0])};
    const float w54[2] = {i2 + i14, i30 - i18};
    const float w55[2] = {i2 - i14, (k_weight0 - i18) - i30};
    const float w56[2] = {i10 + i6, i22 - i26};
    const float w57[2] = {i10 - i6, (k_weight0 - i26) - i22};
    const float w58[2] = {w54[0] + w56[0], w54[1] + w56[1]};
    const float w59[2] = {w54[0] - w56[0], w54[1] - w56[1]};
    const float w60[2] = {w55[0] + w57[1], w55[1] - w57[0]};
    const float w61[2] = {w55[0] - w57[1], w55[1] + w57[0]};
    const float w62[2] = {i6 + i10, i26 - i22};
    const float w63[2] = {i6 - i10, (k_weight0 - i22) - i26};
    const float w64[2] = {i14 + i2, i18 - i30};
    const float w65[2] = {i14 - i2, (k_weight0 - i30) - i18};
    const float w66[2] = {w62[0] + w64[0], w62[1] + w64[1]};
    const float w67[2] = {w62[0] - w64[0], w62[1] - w64[1]};
    const float w68[2] = {w63[0] + w65[1], w63[1] - w65[0]};
    const float w69[2] = {w63[0] - w65[1], w63[1] + w65[0]};
    const float w70[2] = {w58[0] + w66[0], w58[1] + w66[1]};
    const float w71[2] = {w58[0] - w66[0], w58[1] - w66[1]};
    const float w72[2] = {w60[0] + k_weight2 * (w68[0] + w68[1]),
                          w60[1] + k_weight2 * (w68[1] - w68[0])};
    const float w73[2] = {w60[0] + (k_weight0 - k_weight2 * w68[0]) -
                              k_weight2 * w68[1],
                          w60[1] + k_weight2 * (w68[0] - w68[1])};
    const float w74[2] = {w59[0] + w67[1], w59[1] - w67[0]};
    const float w75[2] = {w59[0] - w67[1], w59[1] + w67[0]};
    const float w76[2] = {w61[0] - k_weight2 * (w69[0] - w69[1]),
                          w61[1] - k_weight2 * (w69[1] + w69[0])};
    const float w77[2] = {w61[0] + k_weight2 * (w69[0] - w69[1]),
                          w61[1] + k_weight2 * (w69[1] + w69[0])};
    const float w78[2] = {w46[0] + w70[0], w46[1] + w70[1]};
    const float w79[2] = {w46[0] - w70[0], w46[1] - w70[1]};
    const float w80[2] = {w48[0] + k_weight3 * w72[0] + k_weight4 * w72[1],
                          w48[1] + k_weight3 * w72[1] - k_weight4 * w72[0]};
    const float w81[2] = {w48[0] + (k_weight0 - k_weight3 * w72[0]) -
                              k_weight4 * w72[1],
                          w48[1] + k_weight4 * w72[0] - k_weight3 * w72[1]};
    const float w82[2] = {w50[0] + k_weight2 * (w74[0] + w74[1]),
                          w50[1] + k_weight2 * (w74[1] - w74[0])};
    const float w83[2] = {w50[0] + (k_weight0 - k_weight2 * w74[0]) -
                              k_weight2 * w74[1],
                          w50[1] + k_weight2 * (w74[0] - w74[1])};
    const float w84[2] = {w52[0] + k_weight4 * w76[0] + k_weight3 * w76[1],
                          w52[1] + k_weight4 * w76[1] - k_weight3 * w76[0]};
    const float w85[2] = {w52[0] + (k_weight0 - k_weight4 * w76[0]) -
                              k_weight3 * w76[1],
                          w52[1] + k_weight3 * w76[0] - k_weight4 * w76[1]};
    const float w86[2] = {w47[0] + w71[1], w47[1] - w71[0]};
    const float w87[2] = {w47[0] - w71[1], w47[1] + w71[0]};
    const float w88[2] = {w49[0] - (k_weight4 * w73[0] - k_weight3 * w73[1]),
                          w49[1] + (k_weight0 - k_weight4 * w73[1]) -
                              k_weight3 * w73[0]};
    const float w89[2] = {w49[0] + (k_weight4 * w73[0] - k_weight3 * w73[1]),
                          w49[1] + k_weight4 * w73[1] + k_weight3 * w73[0]};
    const float w90[2] = {w51[0] - k_weight2 * (w75[0] - w75[1]),
                          w51[1] - k_weight2 * (w75[1] + w75[0])};
    const float w91[2] = {w51[0] + k_weight2 * (w75[0] - w75[1]),
                          w51[1] + k_weight2 * (w75[1] + w75[0])};
    const float w92[2] = {w53[0] - (k_weight3 * w77[0] - k_weight4 * w77[1]),
                          w53[1] + (k_weight0 - k_weight3 * w77[1]) -
                              k_weight4 * w77[0]};
    const float w93[2] = {w53[0] + (k_weight3 * w77[0] - k_weight4 * w77[1]),
                          w53[1] + k_weight3 * w77[1] + k_weight4 * w77[0]};
    const float w94[2] = {i1 + i15, i31 - i17};
    const float w95[2] = {i1 - i15, (k_weight0 - i17) - i31};
    const float w96[2] = {i9 + i7, i23 - i25};
    const float w97[2] = {i9 - i7, (k_weight0 - i25) - i23};
    const float w98[2] = {w94[0] + w96[0], w94[1] + w96[1]};
    const float w99[2] = {w94[0] - w96[0], w94[1] - w96[1]};
    const float w100[2] = {w95[0] + w97[1], w95[1] - w97[0]};
    const float w101[2] = {w95[0] - w97[1], w95[1] + w97[0]};
    const float w102[2] = {i5 + i11, i27 - i21};
    const float w103[2] = {i5 - i11, (k_weight0 - i21) - i27};
    const float w104[2] = {i13 + i3, i19 - i29};
    const float w105[2] = {i13 - i3, (k_weight0 - i29) - i19};
    const float w106[2] = {w102[0] + w104[0], w102[1] + w104[1]};
    const float w107[2] = {w102[0] - w104[0], w102[1] - w104[1]};
    const float w108[2] = {w103[0] + w105[1], w103[1] - w105[0]};
    const float w109[2] = {w103[0] - w105[1], w103[1] + w105[0]};
    const float w110[2] = {w98[0] + w106[0], w98[1] + w106[1]};
    const float w111[2] = {w98[0] - w106[0], w98[1] - w106[1]};
    const float w112[2] = {w100[0] + k_weight2 * (w108[0] + w108[1]),
                           w100[1] + k_weight2 * (w108[1] - w108[0])};
    const float w113[2] = {w100[0] + (k_weight0 - k_weight2 * w108[0]) -
                               k_weight2 * w108[1],
                           w100[1] + k_weight2 * (w108[0] - w108[1])};
    const float w114[2] = {w99[0] + w107[1], w99[1] - w107[0]};
    const float w115[2] = {w99[0] - w107[1], w99[1] + w107[0]};
    const float w116[2] = {w101[0] - k_weight2 * (w109[0] - w109[1]),
                           w101[1] - k_weight2 * (w109[1] + w109[0])};
    const float w117[2] = {w101[0] + k_weight2 * (w109[0] - w109[1]),
                           w101[1] + k_weight2 * (w109[1] + w109[0])};
    const float w118[2] = {i3 + i13, i29 - i19};
    const float w119[2] = {i3 - i13, (k_weight0 - i19) - i29};
    const float w120[2] = {i11 + i5, i21 - i27};
    const float w121[2] = {i11 - i5, (k_weight0 - i27) - i21};
    const float w122[2] = {w118[0] + w120[0], w118[1] + w120[1]};
    const float w123[2] = {w118[0] - w120[0], w118[1] - w120[1]};
    const float w124[2] = {w119[0] + w121[1], w119[1] - w121[0]};
    const float w125[2] = {w119[0] - w121[1], w119[1] + w121[0]};
    const float w126[2] = {i7 + i9, i25 - i23};
    const float w127[2] = {i7 - i9, (k_weight0 - i23) - i25};
    const float w128[2] = {i15 + i1, i17 - i31};
    const float w129[2] = {i15 - i1, (k_weight0 - i31) - i17};
    const float w130[2] = {w126[0] + w128[0], w126[1] + w128[1]};
    const float w131[2] = {w126[0] - w128[0], w126[1] - w128[1]};
    const float w132[2] = {w127[0] + w129[1], w127[1] - w129[0]};
    const float w133[2] = {w127[0] - w129[1], w127[1] + w129[0]};
    const float w134[2] = {w122[0] + w130[0], w122[1] + w130[1]};
    const float w135[2] = {w122[0] - w130[0], w122[1] - w130[1]};
    const float w136[2] = {w124[0] + k_weight2 * (w132[0] + w132[1]),
                           w124[1] + k_weight2 * (w132[1] - w132[0])};
    const float w137[2] = {w124[0] + (k_weight0 - k_weight2 * w132[0]) -
                               k_weight2 * w132[1],
                           w124[1] + k_weight2 * (w132[0] - w132[1])};
    const float w138[2] = {w123[0] + w131[1], w123[1] - w131[0]};
    const float w139[2] = {w123[0] - w131[1], w123[1] + w131[0]};
    const float w140[2] = {w125[0] - k_weight2 * (w133[0] - w133[1]),
                           w125[1] - k_weight2 * (w133[1] + w133[0])};
    const float w141[2] = {w125[0] + k_weight2 * (w133[0] - w133[1]),
                           w125[1] + k_weight2 * (w133[1] + w133[0])};
    const float w142[2] = {w110[0] + w134[0], w110[1] + w134[1]};
    const float w143[2] = {w110[0] - w134[0], w110[1] - w134[1]};
    const float w144[2] = {w112[0] + k_weight3 * w136[0] + k_weight4 * w136[1],
                           w112[1] + k_weight3 * w136[1] - k_weight4 * w136[0]};
    const float w145[2] = {w112[0] + (k_weight0 - k_weight3 * w136[0]) -
                               k_weight4 * w136[1],
                           w112[1] + k_weight4 * w136[0] - k_weight3 * w136[1]};
    const float w146[2] = {w114[0] + k_weight2 * (w138[0] + w138[1]),
                           w114[1] + k_weight2 * (w138[1] - w138[0])};
    const float w147[2] = {w114[0] + (k_weight0 - k_weight2 * w138[0]) -
                               k_weight2 * w138[1],
                           w114[1] + k_weight2 * (w138[0] - w138[1])};
    const float w148[2] = {w116[0] + k_weight4 * w140[0] + k_weight3 * w140[1],
                           w116[1] + k_weight4 * w140[1] - k_weight3 * w140[0]};
    const float w149[2] = {w116[0] + (k_weight0 - k_weight4 * w140[0]) -
                               k_weight3 * w140[1],
                           w116[1] + k_weight3 * w140[0] - k_weight4 * w140[1]};
    const float w150[2] = {w111[0] + w135[1], w111[1] - w135[0]};
    const float w151[2] = {w111[0] - w135[1], w111[1] + w135[0]};
    const float w152[2] = {w113[0] - (k_weight4 * w137[0] - k_weight3 * w137[1]),
                           w113[1] + (k_weight0 - k_weight4 * w137[1]) -
                               k_weight3 * w137[0]};
    const float w153[2] = {w113[0] + (k_weight4 * w137[0] - k_weight3 * w137[1]),
                           w113[1] + k_weight4 * w137[1] + k_weight3 * w137[0]};
    const float w154[2] = {w115[0] - k_weight2 * (w139[0] - w139[1]),
                           w115[1] - k_weight2 * (w139[1] + w139[0])};
    const float w155[2] = {w115[0] + k_weight2 * (w139[0] - w139[1]),
                           w115[1] + k_weight2 * (w139[1] + w139[0])};
    const float w156[2] = {w117[0] - (k_weight3 * w141[0] - k_weight4 * w141[1]),
                           w117[1] + (k_weight0 - k_weight3 * w141[1]) -
                               k_weight4 * w141[0]};
    const float w157[2] = {w117[0] + (k_weight3 * w141[0] - k_weight4 * w141[1]),
                           w117[1] + k_weight3 * w141[1] + k_weight4 * w141[0]};
    output[0 * stride] = w78[0] + w142[0];
    output[1 * stride] = w80[0] + k_weight5 * w144[0] + k_weight6 * w144[1];
    output[2 * stride] = w82[0] + k_weight3 * w146[0] + k_weight4 * w146[1];
    output[3 * stride] = w84[0] + k_weight7 * w148[0] + k_weight8 * w148[1];
    output[4 * stride] = w86[0] + k_weight2 * (w150[0] + w150[1]);
    output[5 * stride] = w88[0] + k_weight8 * w152[0] + k_weight7 * w152[1];
    output[6 * stride] = w90[0] + k_weight4 * w154[0] + k_weight3 * w154[1];
    output[7 * stride] = w92[0] + k_weight6 * w156[0] + k_weight5 * w156[1];
    output[8 * stride] = w79[0] + w143[1];
    output[9 * stride] = w81[0] - (k_weight6 * w145[0] - k_weight5 * w145[1]);
    output[10 * stride] = w83[0] - (k_weight4 * w147[0] - k_weight3 * w147[1]);
    output[11 * stride] = w85[0] - (k_weight8 * w149[0] - k_weight7 * w149[1]);
    output[12 * stride] = w87[0] - k_weight2 * (w151[0] - w151[1]);
    output[13 * stride] = w89[0] - (k_weight7 * w153[0] - k_weight8 * w153[1]);
    output[14 * stride] = w91[0] - (k_weight3 * w155[0] - k_weight4 * w155[1]);
    output[15 * stride] = w93[0] - (k_weight5 * w157[0] - k_weight6 * w157[1]);
    output[16 * stride] = w78[0] - w142[0];
    output[17 * stride] = w80[0] + (k_weight0 - k_weight5 * w144[0]) -
                          k_weight6 * w144[1];
    output[18 * stride] = w82[0] + (k_weight0 - k_weight3 * w146[0]) -
                          k_weight4 * w146[1];
    output[19 * stride] = w84[0] + (k_weight0 - k_weight7 * w148[0]) -
                          k_weight8 * w148[1];
    output[20 * stride] = w86[0] + (k_weight0 - k_weight2 * w150[0]) -
                          k_weight2 * w150[1];
    output[21 * stride] = w88[0] + (k_weight0 - k_weight8 * w152[0]) -
                          k_weight7 * w152[1];
    output[22 * stride] = w90[0] + (k_weight0 - k_weight4 * w154[0]) -
                          k_weight3 * w154[1];
    output[23 * stride] = w92[0] + (k_weight0 - k_weight6 * w156[0]) -
                          k_weight5 * w156[1];
    output[24 * stride] = w79[0] - w143[1];
    output[25 * stride] = w81[0] + (k_weight6 * w145[0] - k_weight5 * w145[1]);
    output[26 * stride] = w83[0] + (k_weight4 * w147[0] - k_weight3 * w147[1]);
    output[27 * stride] = w85[0] + (k_weight8 * w149[0] - k_weight7 * w149[1]);
    output[28 * stride] = w87[0] + k_weight2 * (w151[0] - w151[1]);
    output[29 * stride] = w89[0] + (k_weight7 * w153[0] - k_weight8 * w153[1]);
    output[30 * stride] = w91[0] + (k_weight3 * w155[0] - k_weight4 * w155[1]);
    output[31 * stride] = w93[0] + (k_weight5 * w157[0] - k_weight6 * w157[1]);
}

}  // namespace analysis
