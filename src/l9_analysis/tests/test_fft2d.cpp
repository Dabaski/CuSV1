#include <doctest.h>
#include <analysis.h>

#include <cmath>
#include <vector>

// 1:1 target: the SVT-AV1 2D FFT drivers GEN_FFT/GEN_IFFT instantiations and
// the svt_aom_fft_2d_gen / svt_aom_ifft_2d_gen drivers (third_party/SVT-AV1/
// Source/Lib/Codec/fft.c:51-106 fwd, :108-171 inv driver, :179-237 inv
// entries). AN13 composes the landed AN12 1D kernels - it never re-implements
// a butterfly.
//
// TOLERANCE DISCIPLINE: inherited verbatim from tests/test_fft1d.cpp
// (EPSILON = 1e-4 absolute, documented there; the ONE derived exception is
// the round-trip fixture, whose bound is the number of accumulated terms:
// here the 2D roundtrip re-sums across both axes, so its bound is
// n * n * kEpsilon). No assertion in this file may use another tolerance.
//
// Every expected value is hand-derived or derived by the INDEPENDENT
// instrument (python script over math.cos/math.sin, no dependency on the
// port; recorded in the AN13 worklog), never copied from the port.

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
// N=2: the trivial 2D driver (fft.c:88-90). Both unpack extras are false at
// n=2, the cross terms are absent, and the packing is plain interleaved
// complex in row-major order: F(y,x) = sum_{x',y'} f[y'][x'] exp(-2pi i
// (x x' + y y')/2) - all coefficients REAL (n=2 basis +-1), so the 2*n*n =
// 8-float output holds Re in the even slots and 0 in the odd slots.
// ---------------------------------------------------------------------------

TEST_CASE("fft2d2 impulse: all-ones image is DC only") {
    // f = [[1,1],[1,1]]: F(0,0) = 4, every other coefficient 0.
    const float input[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    std::vector<float> temp(4, 0.0f);
    std::vector<float> output(8, -99.0f);  // poison: the tail must not be written
    analysis::fft2d2(input, temp.data(), output.data());
    const std::vector<float> expected = {4.0f, 0.0f, 0.0f, 0.0f,
                                         0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(checkNear(output, expected));
}

TEST_CASE("fft2d4 rank-1: energy in exactly one bin") {
    // f[y][x] = a[y]*b[x] where a = b = cos(2 pi k / 4) = [1, 0, -1, 0] (bin 1).
    // 1D packed A = fft1d4(a) = [0, 2, 0, 0] (bin 1) and B = fft1d4(b) = [0, 2, 0, 0].
    // 2D DFT of rank-1: F(y,x) = A(y) * B(x) (separability). So F(1,1) = 4,
    // all other coefficients 0.
    // The unpack contract writes n*(n/2+1) complex pairs:
    // x = 0..n/2 = 0..2 for all y = 0..3 -> 12 pairs = 24 floats.
    // The tail floats are NOT written: slots {6,7,14,15,22,23,30,31} = -99.
    // F(1,1) real at 2*(1*4+1) = 10: output[10] = 4, output[11] = 0.
    const float input[16] = {
        1.0f, 0.0f, -1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        -1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> temp(16, 0.0f);
    std::vector<float> output(32, -99.0f);
    analysis::fft2d4(input, temp.data(), output.data());
    // All 24 unpack-written floats: expected 4 at index 10, 0 everywhere else.
    // Slots 6,7,14,15 are NOT unpack-written but ARE written by the working
    // passes (pass 3 writes the full n*n = 16 floats), so they hold pass-3
    // values (0 for this fixture) - NOT assertable as poison. Only the slots
    // written by NO pass at all stay poison: 22,23,30,31 (>= n*n = 16).
    std::vector<float> expected(32, 0.0f);
    // F(1,1) = 4 at slot 10; the y_extra mirror writes F(3,1) = conj-symmetric
    // counterpart = 4 at slot 2*((4-1)*4+1) = 26.
    expected[10] = 4.0f;
    expected[26] = 4.0f;
    expected[22] = -99.0f;
    expected[23] = -99.0f;
    expected[30] = -99.0f;
    expected[31] = -99.0f;
    CHECK(checkNear(output, expected, kEpsilon));
    // The 4 never-written tail floats stay poison.
    CHECK(output[22] == -99.0f);
    CHECK(output[23] == -99.0f);
    CHECK(output[30] == -99.0f);
    CHECK(output[31] == -99.0f);
}

// ---------------------------------------------------------------------------
// N=8: all-ones 8x8 image -> F(0,0) = 64, every other coefficient 0.
// Written pairs: y = 0..7, x = 0..4 (n/2 = 4): 40 pairs = 80 floats.
// Slots >= n*n = 64 and NOT unpack-written stay poison: the only such slots
// are x = 5..7 rows: 2*(y*8+x) for x in {5,6,7} = floats 10..15, 26..31 per
// pair-block - i.e. slots {10,11,12,13,14,15,26,27,28,29,30,31,...} wait:
// x=5 gives 2*(y*8+5) = 10+16y... careful: the poison set is exactly the
// floats of pairs (y,5),(y,6),(y,7) for y=0..7 -> 6 floats per row * 8 rows
// = 48 floats. Of those, slots < 64 were written by the working passes
// (pass 1/3 write the full n*n=64 floats); slots >= 64 stay poison.
// ---------------------------------------------------------------------------

TEST_CASE("fft2d8 impulse: all-ones image is DC only") {
    const float input[64] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                             1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                             1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                             1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                             1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                             1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                             1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                             1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    std::vector<float> temp(64, 0.0f);
    std::vector<float> output(128, -99.0f);
    analysis::fft2d8(input, temp.data(), output.data());
    // F(0,0) = 64 at slot 2*(0*8+0) = 0. All other unpack-written slots are 0.
    std::vector<float> expected(128, 0.0f);
    expected[0] = 64.0f;
    // The never-written tail: pairs (y,5),(y,6),(y,7) for y=4..7 - slots
    // 2*(y*8+x) >= 64 that no pass writes. (For y=0..3 the same pairs are
    // written by the working passes, which touch the full n*n=64 floats.)
    for (std::int32_t y = 4; y < 8; ++y) {
        for (std::int32_t x = 5; x < 8; ++x) {
            expected[2 * (y * 8 + x)] = -99.0f;
            expected[2 * (y * 8 + x) + 1] = -99.0f;
        }
    }
    CHECK(checkNear(output, expected, kEpsilon));
}

TEST_CASE("fft2d4 then ifft2d4 roundtrip recovers n*n times the input") {
    // The inverse driver carries NO scaling (fft.c:108-171 has zero
    // arithmetic divisions) and the 1D ifft kernels are unnormalized
    // (AN12: ifft1d2([2,0]) = [2,2] = 2*ones), so the 2D roundtrip recovers
    // n*n * f. The caller divides (noise_util.c:96-102 does ifft then
    // data[i] /= n) - the port stays 1:1 unnormalized.
    const float input[16] = {
        1.0f, 0.0f, -1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        -1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> temp(16, 0.0f);
    std::vector<float> spec(32, 0.0f);
    analysis::fft2d4(input, temp.data(), spec.data());

    std::vector<float> temp2(16, 0.0f);
    std::vector<float> output(16, 0.0f);
    analysis::ifft2d4(spec.data(), temp2.data(), output.data());

    std::vector<float> expected(16, 0.0f);
    for (std::size_t i = 0; i < 16; ++i) expected[i] = 16.0f * input[i];
    CHECK(checkNear(output, expected, 16.0f * kEpsilon));
}

TEST_CASE("roundtrip recovers n*n times the input at every N") {
    // Generic non-symmetric 2D fixture per size, driven by the same
    // independent instrument discipline: expected = n*n * input, tolerance
    // n*n * kEpsilon (the composition re-sums the forward's twiddle noise
    // across both axes).
    std::vector<float> input2 = {3.0f, -7.0f, 2.0f, 5.0f};
    {
        std::vector<float> temp(4, 0.0f), spec(8, 0.0f), temp2(4, 0.0f), out(4, 0.0f);
        analysis::fft2d2(input2.data(), temp.data(), spec.data());
        analysis::ifft2d2(spec.data(), temp2.data(), out.data());
        std::vector<float> exp(4);
        for (std::size_t i = 0; i < 4; ++i) exp[i] = 4.0f * input2[i];
        CHECK(checkNear(out, exp, 4.0f * kEpsilon));
    }
    std::vector<float> input8(64);
    for (std::size_t i = 0; i < 64; ++i) input8[i] = static_cast<float>((i * 7 + 3) % 13) - 6.0f;
    {
        std::vector<float> temp(64, 0.0f), spec(128, 0.0f), temp2(64, 0.0f), out(64, 0.0f);
        analysis::fft2d8(input8.data(), temp.data(), spec.data());
        analysis::ifft2d8(spec.data(), temp2.data(), out.data());
        std::vector<float> exp(64);
        for (std::size_t i = 0; i < 64; ++i) exp[i] = 64.0f * input8[i];
        CHECK(checkNear(out, exp, 64.0f * kEpsilon));
    }
    std::vector<float> input16(256);
    for (std::size_t i = 0; i < 256; ++i) input16[i] = static_cast<float>((i * 5 + 11) % 17) - 8.0f;
    {
        std::vector<float> temp(256, 0.0f), spec(512, 0.0f), temp2(256, 0.0f), out(256, 0.0f);
        analysis::fft2d16(input16.data(), temp.data(), spec.data());
        analysis::ifft2d16(spec.data(), temp2.data(), out.data());
        std::vector<float> exp(256);
        for (std::size_t i = 0; i < 256; ++i) exp[i] = 256.0f * input16[i];
        CHECK(checkNear(out, exp, 256.0f * kEpsilon));
    }
    std::vector<float> input32(1024);
    for (std::size_t i = 0; i < 1024; ++i) input32[i] = static_cast<float>((i * 3 + 29) % 19) - 9.0f;
    {
        std::vector<float> temp(1024, 0.0f), spec(2048, 0.0f), temp2(1024, 0.0f), out(1024, 0.0f);
        analysis::fft2d32(input32.data(), temp.data(), spec.data());
        analysis::ifft2d32(spec.data(), temp2.data(), out.data());
        std::vector<float> exp(1024);
        for (std::size_t i = 0; i < 1024; ++i) exp[i] = 1024.0f * input32[i];
        CHECK(checkNear(out, exp, 1024.0f * kEpsilon));
    }
}

// ---------------------------------------------------------------------------
// N=16: all-ones 16x16 image -> F(0,0) = 256, every other coefficient 0.
// Written pairs: y = 0..15, x = 0..8 (n/2 = 8). Working passes touch the
// full n*n = 256 floats, so poison lives only where 2*(y*16+x) >= 256:
// y = 8..15, x = 9..15.
// ---------------------------------------------------------------------------

TEST_CASE("fft2d16 impulse: all-ones image is DC only") {
    const std::vector<float> input(256, 1.0f);
    std::vector<float> temp(256, 0.0f);
    std::vector<float> output(512, -99.0f);
    analysis::fft2d16(input.data(), temp.data(), output.data());
    std::vector<float> expected(512, 0.0f);
    expected[0] = 256.0f;
    for (std::int32_t y = 8; y < 16; ++y) {
        for (std::int32_t x = 9; x < 16; ++x) {
            expected[2 * (y * 16 + x)] = -99.0f;
            expected[2 * (y * 16 + x) + 1] = -99.0f;
        }
    }
    CHECK(checkNear(output, expected, kEpsilon));
}

// ---------------------------------------------------------------------------
// N=32: all-ones 32x32 image -> F(0,0) = 1024, every other coefficient 0.
// Written pairs: y = 0..31, x = 0..16 (n/2 = 16). Working passes touch the
// full n*n = 1024 floats, so poison lives only where 2*(y*32+x) >= 1024:
// y = 16..31, x = 17..31.
// ---------------------------------------------------------------------------

TEST_CASE("fft2d32 impulse: all-ones image is DC only") {
    const std::vector<float> input(1024, 1.0f);
    std::vector<float> temp(1024, 0.0f);
    std::vector<float> output(2048, -99.0f);
    analysis::fft2d32(input.data(), temp.data(), output.data());
    std::vector<float> expected(2048, 0.0f);
    expected[0] = 1024.0f;
    for (std::int32_t y = 16; y < 32; ++y) {
        for (std::int32_t x = 17; x < 32; ++x) {
            expected[2 * (y * 32 + x)] = -99.0f;
            expected[2 * (y * 32 + x) + 1] = -99.0f;
        }
    }
    CHECK(checkNear(output, expected, kEpsilon));
}
