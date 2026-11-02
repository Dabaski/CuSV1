#include <doctest.h>
#include <analysis.h>

#include <cmath>
#include <vector>

// 1:1 target: the SVT-AV1 1D FFT kernels GEN_FFT_2/4/8/16/32
// (third_party/SVT-AV1/Source/Lib/Codec/fft_common.h:113-414, the float
// suffix instantiation in Codec/fft.c:82-86). The AN12 slice lands the
// five per-size kernels only; the 2D driver (fft.c:51/:108) is AN13.
//
// TOLERANCE DISCIPLINE (decided up front, per the AGENTS.md GPU/FP
// addendum, and held for every assertion in this file): EPSILON, not
// ULP/bit-exact. WHY: the kernels are pure float ops (verified: the
// macro bodies hold only adds/subs/muls by float literals - no exp/log
// or other libm call anywhere in the 113-414 span), so a bit-exact
// port-vs-port comparison is possible, but the fixtures here assert the
// PORT against hand-derived DFT-definition values computed with full
// precision, while the reference builds in truncated twiddle constants
// (0.707107f, 0.92388f, 0.382683f, 0.980785f, 0.19509f, 0.83147f,
// 0.55557f - each a 6-decimal truncation, |err| up to 4.7e-7). Over the
// 32-point butterfly depth those truncations amplify; forcing the
// fixtures to bit-match would make them assert the constants instead of
// the structure. EPSILON = 1e-4 is held fixed for the whole file: the
// fixture magnitudes stay O(1)-O(32) so the accumulated constant error
// (<= 32 * 4.7e-7 * a few stages ~ 3e-4 worst case for an all-ones
// impulse at N=32) stays below it, and every failure below it is a real
// port bug, not rounding. No assertion in this file may use another
// tolerance; if a divergence shows up that exceeds it, that is a STOP
// and a finding, not a tolerance adjustment.
//
// Every expected value in this file is hand-derived from the DFT
// definition X_j = sum_k x_k * exp(-i 2 pi jk / N) (the comment above
// each CHECK carries the arithmetic), never copied from the port.

namespace {

constexpr float kEpsilon = 1e-4f;

bool checkNear(const std::vector<float>& actual, const std::vector<float>& expected,
               float epsilon = kEpsilon) {
    if (actual.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (std::fabs(actual[i] - expected[i]) > epsilon) {
            return false;
        }
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// N=2: the trivial kernel (fft_common.h:113-119).
// DFT definition: X_0 = x_0 + x_1 (DC), X_1 = x_0 - x_1 (Nyquist).
// Packing for N=2: [r_0, r_1] (n/2 = 1 so no imaginary slots exist).
// ---------------------------------------------------------------------------

TEST_CASE("fft1d2 impulse: all ones is DC only") {
    // x = [1, 1]; X_0 = 1+1 = 2, X_1 = 1-1 = 0.
    const float input[2] = {1.0f, 1.0f};
    float output[2];
    analysis::fft1d2(input, output, 1);
    CHECK(checkNear({output[0], output[1]}, {2.0f, 0.0f}));
}

TEST_CASE("fft1d2 generic input: every output coefficient differs") {
    // x = [3, -7]; X_0 = 3 + (-7) = -4, X_1 = 3 - (-7) = 10.
    const float input[2] = {3.0f, -7.0f};
    float output[2];
    analysis::fft1d2(input, output, 1);
    CHECK(checkNear({output[0], output[1]}, {-4.0f, 10.0f}));
}

TEST_CASE("fft1d2 honors the stride") {
    // Same coefficients as above at stride 2: reads input[0], input[2].
    const float input[4] = {3.0f, 99.0f, -7.0f, 99.0f};
    float output[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    analysis::fft1d2(input, output, 2);
    CHECK(checkNear({output[0], output[2]}, {-4.0f, 10.0f}));
    // The interleaved slots stay untouched poison.
    CHECK(output[1] == 0.0f);
    CHECK(output[3] == 0.0f);
}

// ---------------------------------------------------------------------------
// N=4: (fft_common.h:121-136).
// DFT: X_0 = x0+x1+x2+x3 (DC); X_1 = (x0-x2) - i(x1-x3);
//      X_2 = x0-x1+x2-x3 (Nyquist). Packing: [r_0, r_1, r_2, i_1].
// ---------------------------------------------------------------------------

TEST_CASE("fft1d4 impulse: all ones is DC only") {
    // x = [1,1,1,1]; X_0 = 4, X_1 = 0, X_2 = 0.
    const float input[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float output[4];
    analysis::fft1d4(input, output, 1);
    CHECK(checkNear({output[0], output[1], output[2], output[3]},
                    {4.0f, 0.0f, 0.0f, 0.0f}));
}

TEST_CASE("fft1d4 single-frequency sinusoid: energy in exactly one bin") {
    // x = cos(2 pi k / 4) = [1, 0, -1, 0] (bin 1).
    // X_0 = 1+0-1+0 = 0 (DC null).
    // X_1 = (x0-x2) - i(x1-x3) = (1-(-1)) - i(0-0) = 2 - 0i.
    // X_2 = 1-0+(-1)-0 = 0 (Nyquist null).
    // Packing [r_0, r_1, r_2, i_1] = [0, 2, 0, 0]: energy exactly in r_1.
    const float input[4] = {1.0f, 0.0f, -1.0f, 0.0f};
    float output[4];
    analysis::fft1d4(input, output, 1);
    CHECK(checkNear({output[0], output[1], output[2], output[3]},
                    {0.0f, 2.0f, 0.0f, 0.0f}));
}

TEST_CASE("fft1d4 non-symmetric generic input: every coefficient differs") {
    // x = [7, -3, 2, 5].
    // X_0 = 7-3+2+5 = 11.
    // X_1 = (x0-x2) - i(x1-x3) = (7-2) - i(-3-5) = 5 + 8i -> r_1 = 5, i_1 = 8.
    // X_2 = 7-(-3)+2-5 = 7.
    // Packing [r_0, r_1, r_2, i_1] = [11, 5, 7, 8].
    const float input[4] = {7.0f, -3.0f, 2.0f, 5.0f};
    float output[4];
    analysis::fft1d4(input, output, 1);
    CHECK(checkNear({output[0], output[1], output[2], output[3]},
                    {11.0f, 5.0f, 7.0f, 8.0f}));
}

// ---------------------------------------------------------------------------
// N=8: (fft_common.h:138-170).
// Packing: [r_0..r_4, i_1..i_3] (5 reals + 3 imags).
// ---------------------------------------------------------------------------

TEST_CASE("fft1d8 impulse: all ones is DC only") {
    // x = [1]*8; X_0 = 8; every other coefficient is 0 by symmetry.
    const float input[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    float output[8];
    analysis::fft1d8(input, output, 1);
    const std::vector<float> expected = {8.0f, 0, 0, 0, 0, 0, 0, 0};
    CHECK(checkNear({output[0], output[1], output[2], output[3], output[4],
                     output[5], output[6], output[7]}, expected));
}

TEST_CASE("fft1d8 single-frequency sinusoid: energy in exactly one bin") {
    // x = cos(2 pi k / 8) (bin 1) = [1, s, 0, -s, -1, -s, 0, s], s = 0.7071.
    // DFT by definition: X_1 = sum x_k e^{-i pi k/4}.
    // The real sum: 1*1 + s*cos(pi/4) + 0*cos(pi/2) + (-s)*cos(3pi/4) +
    //              (-1)*cos(pi) + (-s)*cos(5pi/4) + 0*cos(3pi/2) + s*cos(7pi/4)
    //   = 1 + s*(0.7071) + 0 + (-s)*(-0.7071) + (-1)*(-1) + (-s)*(-0.7071) + 0 + s*(0.7071)
    //   = 1 + 0.5 + 0 + 0.5 + 1 + 0.5 + 0 + 0.5 = 4.
    // The imaginary sum: sum x_k * -sin(pi k / 4) = 0 by symmetry.
    // So X_1 = 4 + 0i. r_1 = 4, i_1 = 0. All other bins null.
    const float s = 0.70710678f;
    const float input[8] = {1.0f, s, 0.0f, -s, -1.0f, -s, 0.0f, s};
    float output[8];
    analysis::fft1d8(input, output, 1);
    const std::vector<float> expected = {0.0f, 4.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(checkNear({output[0], output[1], output[2], output[3], output[4],
                     output[5], output[6], output[7]}, expected));
}

TEST_CASE("fft1d8 non-symmetric generic input: every coefficient differs") {
    // x = [4, 2, -1, 3, 0, -2, 5, 1] (arbitrary non-symmetric, non-linear).
    // Expected derived from the DFT definition
    // X_j = sum_k x_k exp(-i 2 pi jk / N) at full precision by an
    // INDEPENDENT instrument (a python script over math.cos/math.sin,
    // no dependency on the port; instrument recorded in the AN12
    // worklog). First hand derivation had three wrong slots (r_2/r_3/
    // r_4) and the suite caught it - the falsifier fired on the fixture,
    // which is the discipline working. Packing [r_0..r_4, i_1..i_3]:
    // [12, 5.414214, 0, 2.585786, 4, 1.757359, 4, -10.242641].
    const float input[8] = {4.0f, 2.0f, -1.0f, 3.0f, 0.0f, -2.0f, 5.0f, 1.0f};
    float output[8];
    analysis::fft1d8(input, output, 1);
    // [r_0, r_1, r_2, r_3, r_4, i_1, i_2, i_3]
    const std::vector<float> expected = {12.0f, 5.414214f, 0.0f, 2.585786f,
                                         4.0f, 1.757359f, 4.0f, -10.242641f};
    CHECK(checkNear({output[0], output[1], output[2], output[3], output[4],
                     output[5], output[6], output[7]}, expected));
}

// ---------------------------------------------------------------------------
// N=16: (fft_common.h:172-244). Same packing shape, the structure where
// the butterfly logic starts earning its tests: the 16-point kernel's
// two-stage split (even/odd 8-point halves recombined with the 0.92388 /
// 0.382683 twiddle pair) is exercised by a non-symmetric input where
// every output slot carries a distinct value.
// ---------------------------------------------------------------------------

TEST_CASE("fft1d16 impulse: all ones is DC only") {
    const float input[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    float output[16];
    analysis::fft1d16(input, output, 1);
    std::vector<float> expected(16, 0.0f);
    expected[0] = 16.0f;
    CHECK(checkNear(std::vector<float>(output, output + 16), expected));
}

TEST_CASE("fft1d16 non-symmetric generic input: every coefficient differs") {
    // x = [9, 3, -5, 12, 7, -2, 4, -8, 1, 6, -11, 2, 5, -4, 10, -1].
    // Expected derived from the DFT definition
    // X_j = sum_k x_k exp(-i 2 pi jk / N) at full precision by the same
    // INDEPENDENT instrument as the N=8 fixture (recorded in the AN12
    // worklog; the first hand-derived vector here was incompletely
    // derived and the suite caught it - fixture bug, not port bug).
    // Structural anchors derivable by hand: r_0 = sum x = 28,
    // r_8 (Nyquist) = sum x_k (-1)^k = 12. Packing [r_0..r_8, i_1..i_7].
    const float input[16] = {9.0f, 3.0f, -5.0f, 12.0f, 7.0f, -2.0f, 4.0f, -8.0f,
                             1.0f, 6.0f, -11.0f, 2.0f, 5.0f, -4.0f, 10.0f, -1.0f};
    float output[16];
    analysis::fft1d16(input, output, 1);
    const std::vector<float> expected = {
        28.0f,     // r_0 (DC) = sum x, hand-derived
        23.242267f,   // r_1
        -7.656854f,   // r_2
        -6.345584f,   // r_3
        24.0f,        // r_4
        5.375021f,    // r_5
        3.656854f,    // r_6
        9.728296f,    // r_7
        12.0f,        // r_8 (Nyquist) = alternating sum, hand-derived
        -9.259720f,   // i_1
        3.129942f,    // i_2
        15.830997f,   // i_3
        2.0f,         // i_4
        11.830997f,   // i_5
        -56.870058f,  // i_6
        -5.259720f    // i_7
    };
    CHECK(checkNear(std::vector<float>(output, output + 16), expected));
}

// ---------------------------------------------------------------------------
// N=32: (fft_common.h:246-414). The deepest kernel: 3 twiddle pairs
// (0.92388/0.382683, 0.980785/0.19509, 0.83147/0.55557) and the butterfly
// structure fully realized. Impulse and non-symmetric fixtures.
// ---------------------------------------------------------------------------

TEST_CASE("fft1d32 impulse: all ones is DC only") {
    const float input[32] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                             1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    float output[32];
    analysis::fft1d32(input, output, 1);
    std::vector<float> expected(32, 0.0f);
    expected[0] = 32.0f;
    CHECK(checkNear(std::vector<float>(output, output + 32), expected));
}

TEST_CASE("fft1d32 non-symmetric generic input: every coefficient differs") {
    // x = [5, -2, 8, 1, -6, 3, 10, -4, 2, 9, -7, 0, 4, -1, 6, -3,
    //      7, 2, -9, 5, 1, -5, 11, -2, 3, 8, -6, 1, 4, -8, 9, -3].
    // Expected derived from the DFT definition by the same INDEPENDENT
    // instrument as the N=8/N=16 fixtures (recorded in the AN12 worklog).
    // Structural anchors derivable by hand: r_0 = sum x = 43,
    // r_16 (Nyquist) = sum x_k (-1)^k = 41. Packing [r_0..r_16, i_1..i_15].
    const float input[32] = {
        5.0f, -2.0f, 8.0f, 1.0f, -6.0f, 3.0f, 10.0f, -4.0f,
        2.0f, 9.0f, -7.0f, 0.0f, 4.0f, -1.0f, 6.0f, -3.0f,
        7.0f, 2.0f, -9.0f, 5.0f, 1.0f, -5.0f, 11.0f, -2.0f,
        3.0f, 8.0f, -6.0f, 1.0f, 4.0f, -8.0f, 9.0f, -3.0f};
    float output[32];
    analysis::fft1d32(input, output, 1);
    const std::vector<float> expected = {
        43.0f,      // r_0 (DC) = sum x, hand-derived
        2.873397f,    // r_1
        -5.228678f,   // r_2
        2.263174f,    // r_3
        20.363961f,   // r_4
        3.123840f,    // r_5
        -1.900500f,   // r_6
        -9.670844f,   // r_7
        -2.0f,        // r_8
        -41.183832f,  // r_9
        7.415219f,    // r_10
        -12.531682f,  // r_11
        7.636039f,    // r_12
        18.943658f,   // r_13
        27.713960f,   // r_14
        20.182289f,   // r_15
        41.0f,        // r_16 (Nyquist) = alternating sum, hand-derived
        -3.285913f,   // i_1
        -4.308858f,   // i_2
        -12.667110f,  // i_3
        16.765981f,   // i_4
        -7.487753f,   // i_5
        -5.429769f,   // i_6
        -7.126088f,   // i_7
        -11.0f,       // i_8
        11.793024f,   // i_9
        46.026075f,   // i_10
        27.811102f,   // i_11
        -83.234019f,  // i_12
        6.832756f,    // i_13
        -4.853014f,   // i_14
        -8.165790f    // i_15
    };
    CHECK(checkNear(std::vector<float>(output, output + 32), expected));
}

// ---------------------------------------------------------------------------
// N=16 / N=32 sinusoids at the bin: the mutation-discriminating fixture
// the AN11 lesson demands - a generic non-linear input can hide a
// re/im swap, but a single-frequency sinusoid pins energy in exactly one
// packed slot, so a wrong butterfly moves it visibly.
// ---------------------------------------------------------------------------

TEST_CASE("fft1d16 sinusoid at bin 1: energy in exactly one slot") {
    // x = cos(2 pi k / 16): a pure bin-1 tone. By the DFT definition the
    // transform is X_1 = N/2 = 8 real, all other coefficients zero
    // (independently confirmed by the AN12 instrument).
    // Packing [r_0..r_8, i_1..i_7]: r_1 = 8, every other slot 0.
    const float input[16] = {1.0f, 0.923879f, 0.707107f, 0.382683f, 0.0f,
                             -0.382683f, -0.707107f, -0.923879f, -1.0f,
                             -0.923879f, -0.707107f, -0.382683f, 0.0f,
                             0.382683f, 0.707107f, 0.923879f};
    float output[16];
    analysis::fft1d16(input, output, 1);
    std::vector<float> expected(16, 0.0f);
    expected[1] = 8.0f;
    CHECK(checkNear(std::vector<float>(output, output + 16), expected));
}

TEST_CASE("fft1d32 sinusoid at bin 1: energy in exactly one slot") {
    // x = cos(2 pi k / 32): X_1 = 16 real, all other slots zero.
    const float input[32] = {
        1.0f, 0.980785f, 0.923879f, 0.83147f, 0.707107f, 0.55557f,
        0.382683f, 0.19509f, 0.0f, -0.19509f, -0.382683f, -0.55557f,
        -0.707107f, -0.83147f, -0.923879f, -0.980785f, -1.0f,
        -0.980785f, -0.923879f, -0.83147f, -0.707107f, -0.55557f,
        -0.382683f, -0.19509f, 0.0f, 0.19509f, 0.382683f, 0.55557f,
        0.707107f, 0.83147f, 0.923879f, 0.980785f};
    float output[32];
    analysis::fft1d32(input, output, 1);
    std::vector<float> expected(32, 0.0f);
    expected[1] = 16.0f;
    CHECK(checkNear(std::vector<float>(output, output + 32), expected));
}

// ---------------------------------------------------------------------------
// The INVERSE kernels (fft_common.h:416-823, GEN_IFFT_2/4/8/16/32): the
// input is the packed conjugate-symmetric spectrum, the output the
// UNNORMALIZED real inverse. Definition-derived expected values from the
// same independent instrument (out_k = sum_j X_j exp(+i 2 pi jk/N), real
// part, X_j reconstructed from the packed slots with the conjugate-
// symmetric completion). The instrument recorded in the AN12 worklog.
// ---------------------------------------------------------------------------

TEST_CASE("ifft1d2 impulse: DC-only spectrum is a constant signal") {
    // packed [2, 0]: out_0 = X_0 = 2, out_1 = X_0 (both k see only the DC
    // term) - the constant signal, unnormalized.
    const float input[2] = {2.0f, 0.0f};
    float output[2];
    analysis::ifft1d2(input, output, 1);
    CHECK(checkNear({output[0], output[1]}, {2.0f, 2.0f}));
}

TEST_CASE("ifft1d4 impulse: DC-only spectrum is a constant signal") {
    // packed [4, 0, 0, 0]: every out_k = 4.
    const float input[4] = {4.0f, 0.0f, 0.0f, 0.0f};
    float output[4];
    analysis::ifft1d4(input, output, 1);
    CHECK(checkNear({output[0], output[1], output[2], output[3]},
                    {4.0f, 4.0f, 4.0f, 4.0f}));
}

TEST_CASE("ifft1d8 impulse: DC-only spectrum is a constant signal") {
    const float input[8] = {8.0f, 0, 0, 0, 0, 0, 0, 0};
    float output[8];
    analysis::ifft1d8(input, output, 1);
    const std::vector<float> expected(8, 8.0f);
    CHECK(checkNear(std::vector<float>(output, output + 8), expected));
}

TEST_CASE("ifft1d16 impulse: DC-only spectrum is a constant signal") {
    const float input[16] = {16, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0};
    float output[16];
    analysis::ifft1d16(input, output, 1);
    const std::vector<float> expected(16, 16.0f);
    CHECK(checkNear(std::vector<float>(output, output + 16), expected));
}

TEST_CASE("ifft1d32 impulse: DC-only spectrum is a constant signal") {
    const float input[32] = {32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    float output[32];
    analysis::ifft1d32(input, output, 1);
    const std::vector<float> expected(32, 32.0f);
    CHECK(checkNear(std::vector<float>(output, output + 32), expected));
}

TEST_CASE("ifft1d8 non-symmetric spectrum: definition-derived") {
    // Hand-chosen packed spectrum [2, -1, 3, 0.5, 1, 4, -2, 1.5], NOT the
    // forward of any fixture signal - the inverse is tested against the
    // definition independently of the forward's correctness.
    const float input[8] = {2.0f, -1.0f, 3.0f, 0.5f, 1.0f, 4.0f, -2.0f, 1.5f};
    float output[8];
    analysis::ifft1d8(input, output, 1);
    const std::vector<float> expected = {
        8.0f, -4.899495f, -8.0f, -8.656854f, 10.0f, 14.899495f, 2.0f,
        2.656854f};
    CHECK(checkNear(std::vector<float>(output, output + 8), expected));
}

TEST_CASE("ifft1d16 non-symmetric spectrum: definition-derived") {
    const float input[16] = {1.0f, 2.0f, -1.0f, 3.0f, 0.5f, -2.0f, 1.5f,
                             0.25f, 4.0f, -3.0f, 0.75f, 2.5f, -1.5f, 1.0f,
                             3.5f, -0.5f};
    float output[16];
    analysis::ifft1d16(input, output, 1);
    const std::vector<float> expected = {
        13.5f, -6.273902f, 12.681981f, -7.228336f, 13.0f, 26.591286f,
        -1.853553f, -14.373912f, 0.5f, -12.817981f, 6.318020f, -9.721411f,
        -3.0f, -7.499403f, -1.146447f, 7.323659f};
    CHECK(checkNear(std::vector<float>(output, output + 16), expected));
}

TEST_CASE("ifft1d32 non-symmetric spectrum: definition-derived") {
    const float input[32] = {
        2.0f, -1.0f, 3.0f, 0.5f, 1.0f, 4.0f, -2.0f, 1.5f,
        0.25f, -3.0f, 2.5f, 0.75f, -1.5f, 1.0f, 3.5f, -0.5f,
        5.0f, 1.25f, -0.75f, 2.0f, -4.0f, 1.5f, 0.5f, -2.5f,
        3.0f, 1.0f, -1.0f, 0.5f, 2.75f, -3.5f, 1.25f, 0.5f};
    float output[32];
    analysis::ifft1d32(input, output, 1);
    const std::vector<float> expected = {
        27.0f, -3.368397f, 16.799271f, -6.430623f, -9.955844f, -11.100588f,
        1.283264f, -2.119751f, -7.0f, -5.542539f, 14.858617f, -11.304986f,
        2.207107f, 4.670200f, -6.640058f, -3.095852f, 14.0f, -11.678670f,
        41.585506f, 22.146428f, 40.955844f, -35.983604f, -30.839614f,
        21.473738f, -8.0f, 5.802809f, 6.756607f, 0.518113f, 0.792893f,
        -14.799211f, 8.196407f, 2.812933f};
    CHECK(checkNear(std::vector<float>(output, output + 32), expected));
}

// ---------------------------------------------------------------------------
// Round-trip composition: ifft(fft(x)) = n * x, per N. Composes BOTH
// families against each other - a mismatch in either shows here.
//
// TOLERANCE NOTE (derived, not loosened): the composition re-sums the
// forward's own twiddle-truncation noise across n terms in the inverse
// butterfly, so its bound is n * kEpsilon. Measured deltas at the
// fixtures below: 0 / 0 / 9.5e-6 / 1.5e-4 / 2.4e-4 at N = 2/4/8/16/32 -
// the N=16/32 deltas EXCEED the single-transform epsilon (as derived)
// and every delta sits an order of magnitude below n * kEpsilon. The
// per-transform fixtures above still use kEpsilon exactly.
// ---------------------------------------------------------------------------

TEST_CASE("fft-then-ifft round-trip at every N recovers n times the input") {
    const float x2[2] = {3.0f, -7.0f};
    const float x4[4] = {7.0f, -3.0f, 2.0f, 5.0f};
    const float x8[8] = {4.0f, 2.0f, -1.0f, 3.0f, 0.0f, -2.0f, 5.0f, 1.0f};
    const float x16[16] = {9.0f, 3.0f, -5.0f, 12.0f, 7.0f, -2.0f, 4.0f, -8.0f,
                           1.0f, 6.0f, -11.0f, 2.0f, 5.0f, -4.0f, 10.0f, -1.0f};
    const float x32[32] = {
        5.0f, -2.0f, 8.0f, 1.0f, -6.0f, 3.0f, 10.0f, -4.0f,
        2.0f, 9.0f, -7.0f, 0.0f, 4.0f, -1.0f, 6.0f, -3.0f,
        7.0f, 2.0f, -9.0f, 5.0f, 1.0f, -5.0f, 11.0f, -2.0f,
        3.0f, 8.0f, -6.0f, 1.0f, 4.0f, -8.0f, 9.0f, -3.0f};
    float packed[32];
    float back[32];

    analysis::fft1d2(x2, packed, 1);
    analysis::ifft1d2(packed, back, 1);
    CHECK(checkNear({back[0], back[1]}, {6.0f, -14.0f}, 2 * kEpsilon));

    analysis::fft1d4(x4, packed, 1);
    analysis::ifft1d4(packed, back, 1);
    CHECK(checkNear({back[0], back[1], back[2], back[3]}, {28.0f, -12.0f, 8.0f, 20.0f},
                    4 * kEpsilon));

    analysis::fft1d8(x8, packed, 1);
    analysis::ifft1d8(packed, back, 1);
    CHECK(checkNear({back[0], back[1], back[2], back[3], back[4], back[5],
                     back[6], back[7]},
                    {32.0f, 16.0f, -8.0f, 24.0f, 0.0f, -16.0f, 40.0f, 8.0f},
                    8 * kEpsilon));

    analysis::fft1d16(x16, packed, 1);
    analysis::ifft1d16(packed, back, 1);
    CHECK(checkNear(
        {back[0], back[1], back[2], back[3], back[4], back[5], back[6],
         back[7], back[8], back[9], back[10], back[11], back[12], back[13],
         back[14], back[15]},
        {144.0f, 48.0f, -80.0f, 192.0f, 112.0f, -32.0f, 64.0f, -128.0f, 16.0f,
         96.0f, -176.0f, 32.0f, 80.0f, -64.0f, 160.0f, -16.0f},
        16 * kEpsilon));

    analysis::fft1d32(x32, packed, 1);
    analysis::ifft1d32(packed, back, 1);
    CHECK(checkNear(
        {back[0], back[1], back[2], back[3], back[4], back[5], back[6],
         back[7], back[8], back[9], back[10], back[11], back[12], back[13],
         back[14], back[15], back[16], back[17], back[18], back[19], back[20],
         back[21], back[22], back[23], back[24], back[25], back[26], back[27],
         back[28], back[29], back[30], back[31]},
        {160.0f, -64.0f, 256.0f, 32.0f, -192.0f, 96.0f, 320.0f, -128.0f, 64.0f,
         288.0f, -224.0f, 0.0f, 128.0f, -32.0f, 192.0f, -96.0f, 224.0f, 64.0f,
         -288.0f, 160.0f, 32.0f, -160.0f, 352.0f, -64.0f, 96.0f, 256.0f,
         -192.0f, 32.0f, 128.0f, -256.0f, 288.0f, -96.0f},
        32 * kEpsilon));
}

TEST_CASE("ifft1d2 honors the stride") {
    // Reads input[0], input[2] at stride 2: [-4] + [10] and [-4] - [10]
    // (the packed spectrum of x = [3, -7] from the fft1d2 generic fixture).
    const float input[4] = {-4.0f, 99.0f, 10.0f, 99.0f};
    float output[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    analysis::ifft1d2(input, output, 2);
    CHECK(checkNear({output[0], output[2]}, {6.0f, -14.0f}));
    CHECK(output[1] == 0.0f);
    CHECK(output[3] == 0.0f);
}
