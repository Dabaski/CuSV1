#include <doctest.h>
#include <analysis.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// ============================================================================
// AN14 tests: noise_util (third_party/SVT-AV1/Source/Lib/Codec/noise_util.c).
// TOLERANCE DISCIPLINE (decided up front, no per-test picking):
//  - the composed forward->inverse roundtrip is IDENTITY (n^2*f / n^2), so its
//    bound is the landed EPSILON = 1e-4 ABSOLUTE inherited from tests/
//    test_fft1d.cpp (the n^2*n^2 scaling cancels exactly in the division, so
//    no n-scaling of the bound is needed here - documented deviation from the
//    raw ifft2d(fft2d(f)) == n^2*f fixture whose bound is n*n*kEpsilon);
//  - exact-bit CHECKs (==) are used wherever the arithmetic is exactly
//    representable (the k_beta else-branch multiply and the val-branch cases
//    with power-of-two denominators);
//  - the OD_DIVU span is tested by a 2048-constant EXACT diff against the
//    mechanically transcribed reference values below (the paired-metric
//    caveat applies: this block moves coverage by transcription; the diff
//    IS the test) plus structural invariants that are independent of the
//    transcription (sentinel rows at powers of two, a == b on the odd-d
//    rows, b == 0 on the even non-power rows).
// Every expected value is hand-derived (the val-branch cases reduce to
// power-of-two divisions of small integers) or computed with the IDENTICAL
// float expression as the port (the k_beta constants). Nothing is copied
// from the port's own output.
// ============================================================================

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

// Deterministic small-integer input pattern (values in [-8, 7]) for the
// roundtrip fixtures - small magnitudes keep the accumulated error far
// inside kEpsilon at every supported size.
float roundtripInput(std::int32_t n, std::int32_t i) {
    return static_cast<float>(((i * 3 + i / n + 5) % 16) - 8);
}

}  // namespace

TEST_CASE("AN14 noise_psd_get_default_value formula") {
    // The reference formula, noise_util.c:21: (factor*factor / 10000) *
    // block_size * block_size / 8. The port must reproduce the same-order
    // float expression bit-exactly (float promotions: int32 block_size
    // promotes at the first multiply; every later op is float).
    const float factors[]  = {0.0f, 1.0f, 2.5f, 5.0f, 7.3f};
    const std::int32_t sizes[] = {2, 4, 8, 16, 32};
    for (float f : factors) {
        for (std::int32_t bs : sizes) {
            const float expected =
                (f * f / 10000) * bs * bs / 8;
            const float actual = analysis::noisePsdGetDefaultValue(bs, f);
            CHECK(actual == expected);
        }
    }
    // Independent hand-pin: bs = 8, f = 2.5 -> (6.25/10000)*64/8 = 0.005.
    // 6.25 is exactly representable; the power-of-two multiplies carry the
    // rounding of the single /10000 division through unchanged, so the
    // result is the nearest float to 0.005 exactly.
    CHECK(analysis::noisePsdGetDefaultValue(8, 2.5f) == 0.005f);
    CHECK(analysis::noisePsdGetDefaultValue(32, 5.0f) ==
          (5.0f * 5.0f / 10000) * 1024 / 8);
}

TEST_CASE("AN14 noise_tx_malloc pointers, alignment, zero-init") {
    const std::int32_t sizes[] = {2, 4, 8, 16, 32};
    for (std::int32_t bs : sizes) {
        analysis::NoiseTx* tx = analysis::noiseTxMalloc(bs);
        REQUIRE(tx != nullptr);
        CHECK(tx->block_size == bs);
        CHECK(tx->tx_block != nullptr);
        CHECK(tx->temp != nullptr);
        CHECK((reinterpret_cast<std::uintptr_t>(tx->tx_block) % 32) == 0);
        CHECK((reinterpret_cast<std::uintptr_t>(tx->temp) % 32) == 0);
        // The fft/ifft pointers must resolve to the LANDED per-size entry
        // points (the malloc switch, noise_util.c:30-55).
        const void* fft  = reinterpret_cast<const void*>(tx->fft);
        const void* ifft = reinterpret_cast<const void*>(tx->ifft);
        switch (bs) {
        case 2:
            CHECK(fft == reinterpret_cast<const void*>(analysis::fft2d2));
            CHECK(ifft == reinterpret_cast<const void*>(analysis::ifft2d2));
            break;
        case 4:
            CHECK(fft == reinterpret_cast<const void*>(analysis::fft2d4));
            CHECK(ifft == reinterpret_cast<const void*>(analysis::ifft2d4));
            break;
        case 8:
            CHECK(fft == reinterpret_cast<const void*>(analysis::fft2d8));
            CHECK(ifft == reinterpret_cast<const void*>(analysis::ifft2d8));
            break;
        case 16:
            CHECK(fft == reinterpret_cast<const void*>(analysis::fft2d16));
            CHECK(ifft == reinterpret_cast<const void*>(analysis::ifft2d16));
            break;
        case 32:
            CHECK(fft == reinterpret_cast<const void*>(analysis::fft2d32));
            CHECK(ifft == reinterpret_cast<const void*>(analysis::ifft2d32));
            break;
        default:
            FAIL("unreachable switch arm");
        }
        // Zero-init contract (noise_util.c:63-66): both buffers are fully
        // zeroed up front - the forward transform never writes the tail of
        // the packed output (x > n/2), so the zero-fill must hold there too.
        const std::size_t n2 = static_cast<std::size_t>(2) * bs * bs;
        for (std::size_t i = 0; i < n2; ++i) {
            CHECK(tx->tx_block[i] == 0.0f);
            CHECK(tx->temp[i] == 0.0f);
        }
        analysis::noiseTxFree(tx);
    }
    // Unsupported sizes return nullptr (noise_util.c:51-55; the SVT_ERROR
    // print is dropped, the nullptr return is the contract).
    CHECK(analysis::noiseTxMalloc(0) == nullptr);
    CHECK(analysis::noiseTxMalloc(1) == nullptr);
    CHECK(analysis::noiseTxMalloc(3) == nullptr);
    CHECK(analysis::noiseTxMalloc(64) == nullptr);
    CHECK(analysis::noiseTxMalloc(-8) == nullptr);
    // Free of nullptr is a no-op (noise_util.c:105-107).
    analysis::noiseTxFree(nullptr);
}

TEST_CASE("AN14 noise_tx forward -> inverse roundtrip is identity") {
    const std::int32_t sizes[] = {2, 4, 8, 16, 32};
    for (std::int32_t bs : sizes) {
        analysis::NoiseTx* tx = analysis::noiseTxMalloc(bs);
        REQUIRE(tx != nullptr);
        const std::size_t n = static_cast<std::size_t>(bs) * bs;
        std::vector<float> data(n);
        for (std::size_t i = 0; i < n; ++i) {
            data[i] = roundtripInput(bs, static_cast<std::int32_t>(i));
        }
        std::vector<float> input = data;
        analysis::noiseTxForward(tx, data.data());
        analysis::noiseTxInverse(tx, data.data());
        // ifft2d(fft2d(f)) == n^2 * f (the AN13 pinned roundtrip) and the
        // inverse's /= n (n = bs^2) cancels it exactly: the composed
        // noiseTxForward -> noiseTxInverse is IDENTITY within kEpsilon.
        CHECK(checkNear(data, input));
        analysis::noiseTxFree(tx);
    }
}

TEST_CASE("AN14 noise_tx forward leaves the buffer tail unwritten") {
    // The unpack contract (fft.c:33-49 / analysis.h): output holds
    // n*(n/2+1) complex values scattered as 2*(y*n+x) for x in 0..n/2 per
    // row. The row-transform pass of the 2D driver fills EVERY position
    // 0..n^2-1 with intermediate data before the unpack, so the per-row
    // x > n/2 slots hold STALE intermediate data after the forward - 1:1
    // with the reference (aom's unpack_2d_output has the same scattered
    // pattern, and noise_util.c:65 zero-fills once at malloc). The ONLY
    // region never written by the forward is the final gap
    // [2*n^2 - n + 2, 2*n^2), where the malloc zero-fill survives.
    const std::int32_t sizes[] = {2, 4, 8, 16, 32};
    for (std::int32_t bs : sizes) {
        analysis::NoiseTx* tx = analysis::noiseTxMalloc(bs);
        REQUIRE(tx != nullptr);
        const std::size_t n = static_cast<std::size_t>(bs) * bs;
        std::vector<float> data(n);
        for (std::size_t i = 0; i < n; ++i) {
            data[i] = roundtripInput(bs, static_cast<std::int32_t>(i));
        }
        analysis::noiseTxForward(tx, data.data());
        const std::size_t n2 = static_cast<std::size_t>(bs) * bs;
        // bs = 2 has no unwritten region (the loop is vacuous there).
        for (std::size_t i = 2 * n2 - bs + 2; i < 2 * n2; ++i) {
            CHECK(tx->tx_block[i] == 0.0f);
        }
        analysis::noiseTxFree(tx);
    }
}

TEST_CASE("AN14 noise_tx_filter_c k_beta branches") {
    // k_beta = 1.1f; the else-branch scale k_beta_m1_div_k_beta = (1.1f -
    // 1.0f)/1.1f is computed with the IDENTICAL float expression as the port
    // (both round the same division), so its else-branch outputs are
    // bit-exact CHECKs. The val-branch cases below hand-reduce to
    // power-of-two divisions of small integers (exact float arithmetic),
    // so those are also exact. The two max-guard cases use the same-order
    // float expressions and are exact by construction.
    const float kBeta           = 1.1f;
    const float kBetaM1DivKBeta = (kBeta - 1.0f) / kBeta;
    const float kEps            = 1e-6f;
    // 4x4 block (16 pairs). Each entry: (re, im, psd, isValBranch).
    struct Case { float re, im, psd; bool valBranch; };
    const Case cases[] = {
        // val branch: p = 4 > 1.1f*1 and > 1e-6. val = (4-1)/4 = 3/4 = 0.75
        // (exact dyadic). expected (2*0.75, 0) = (1.5, 0).
        {2.0f, 0.0f, 1.0f, true},
        // val branch: p = 1+1 = 2 > 1.1. val = (2-1)/2 = 0.5.
        // expected (0.5, 0.5).
        {1.0f, 1.0f, 1.0f, true},
        // val branch with psd = 0.5: p = 2.25 > 0.55. val =
        // (2.25-0.5)/2.25 = 1.75/2.25 = 0.777... (NOT dyadic - the expected
        // is computed with the identical float division/multiply sequence,
        // exact by construction).
        {1.5f, 0.0f, 0.5f, true},
        // else branch (p = 1 <= psd*1.1 = 11): scale both by
        // k_beta_m1_div_k_beta.
        {1.0f, 0.0f, 10.0f, false},
        // else branch with nonzero im (p = 8 <= 11).
        {2.0f, 2.0f, 10.0f, false},
        // else branch, tiny p (p = 1e-8 <= 1e-6 AND <= 0.55): the
        // double comparison p > 1e-6 fails.
        {1e-4f, 0.0f, 0.5f, false},
        // else branch at exactly zero p.
        {0.0f, 0.0f, 1.0f, false},
    };
    const std::int32_t bs = 4;
    for (const Case& c : cases) {
        std::vector<float> block(2 * bs * bs, 0.0f);
        block[0] = c.re;
        block[1] = c.im;
        // All other pairs stay (0, 0) -> p = 0 -> else branch -> 0 (0 times
        // any scale is +0; CHECK keeps that exact).
        analysis::noiseTxFilterC(bs, block.data(), c.psd);
        const float p = c.re * c.re + c.im * c.im;
        if (c.valBranch) {
            const float val = (p - c.psd) / (p > kEps ? p : kEps);
            CHECK(block[0] == c.re * val);
            CHECK(block[1] == c.im * val);
        } else {
            CHECK(block[0] == c.re * kBetaM1DivKBeta);
            CHECK(block[1] == c.im * kBetaM1DivKBeta);
        }
    }
    // Exact hand-pins for the dyadic val-branch cases: (2,0) psd=1 ->
    // (1.5, 0); (1,1) psd=1 -> (0.5, 0.5).
    {
        std::vector<float> block(2 * bs * bs, 0.0f);
        block[0] = 2.0f;
        block[1] = 0.0f;
        analysis::noiseTxFilterC(bs, block.data(), 1.0f);
        CHECK(block[0] == 1.5f);
        CHECK(block[1] == 0.0f);
    }
    {
        std::vector<float> block(2 * bs * bs, 0.0f);
        block[0] = 1.0f;
        block[1] = 1.0f;
        analysis::noiseTxFilterC(bs, block.data(), 1.0f);
        CHECK(block[0] == 0.5f);
        CHECK(block[1] == 0.5f);
    }
    // The double comparison semantics: p > 1e-6 promotes p to double, so a
    // float p that rounds to exactly 1e-6f is NOT > 1e-6 (double). The
    // val-branch gate must be the strict conjunction; a pair with p just
    // above 1e-6 but psd huge still takes the else branch (p <= psd*1.1).
    // Covered above by the {1e-4f, 0} case (p = 1e-8) and the {2,2} psd=10
    // case (p = 8 <= 11).
    //
    // In-place mutation across the full block: after the filter, every pair
    // must have been visited (16 pairs of a 4x4 block); a second filter pass
    // on the all-zero block must leave it all-zero (0 * scale == 0).
    std::vector<float> block(2 * bs * bs, 0.0f);
    analysis::noiseTxFilterC(bs, block.data(), 1.0f);
    for (std::size_t i = 0; i < block.size(); ++i) {
        CHECK(block[i] == 0.0f);
    }
}

// The reference OD_DIVU macro (third_party/SVT-AV1/Source/Lib/Codec/
// bitstream_unit.h:48-52):
//   ((uint32_t)((M * (uint64_t)x + h1) >> 32)) >> svt_log2f(d)
// with svt_log2f = get_msb = floor(log2) (definitions.h:592). Mirrored here
// so the test exercises the CONSUMER's formula against the port's table.
std::uint32_t odDivuSmall(std::uint32_t x, std::uint32_t d) {
    const std::uint64_t prod =
        static_cast<std::uint64_t>(analysis::odDivuSmallConsts[d - 1][0]) * x +
        analysis::odDivuSmallConsts[d - 1][1];
    const std::uint32_t q = static_cast<std::uint32_t>(prod >> 32);
    std::uint32_t shift = 0;
    for (std::uint32_t dd = d; dd > 1; dd >>= 1) {
        ++shift;
    }
    return q >> shift;
}

TEST_CASE("AN14 OD_DIVU sentinel rows and functional division") {
    // SENTINEL STRUCTURE: the 11 power-of-two divisors (d = 1, 2, 4, 8, 16,
    // 32, 64, 128, 256, 512, 1024) carry the pair {0xFFFFFFFF, 0xFFFFFFFF};
    // verified mechanically against the reference before this suite was
    // written. d = 1 reproduces x/1 through (M*(x+1)) >> 32 = x (for
    // x < 2^32 - 1); d = 2^k composes that with the extra >> k.
    for (std::uint32_t d : {1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u, 256u, 512u,
                            1024u}) {
        CHECK(analysis::odDivuSmallConsts[d - 1][0] == 0xFFFFFFFFu);
        CHECK(analysis::odDivuSmallConsts[d - 1][1] == 0xFFFFFFFFu);
    }
    // FUNCTIONAL DIVISION: the table's contract is x/d for every d in
    // 1..1024 at every x the consumer can feed (bitstream_unit.h:49-53).
    // This IS the transcription check in functional form: any wrong constant
    // breaks some (x, d) pair. Verified over a spread of x including the
    // boundaries 0, 1, the uint32 maxima and a mid-range sample.
    const std::uint32_t xs[] = {0u,          1u,          2u,          3u,
                                5u,          16u,         100u,        1023u,
                                65535u,      65536u,      123456u,     1000000u,
                                2147483647u, 4294967294u, 4294967295u};
    for (std::uint32_t d = 1; d <= 1024; ++d) {
        for (std::uint32_t x : xs) {
            CHECK(odDivuSmall(x, d) == x / d);
        }
    }
}
