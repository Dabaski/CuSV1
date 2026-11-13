#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "warped_motion.h"

// A3W - the warped-motion surface. Every expectation derives from the vendored
// reference (third_party/SVT-AV1/Source/Lib/Codec/warped_motion.c, "wm";
// common_utils.c, "cu") with the line quoted. The data tables are additionally
// diff-verified entry-by-entry against the REFERENCE FILE ITSELF, parsed at
// runtime from REPO_ROOT (the l17 MC1b / l15 RC1 precedent).

namespace {

std::string readFile(const char* rel) {
    std::ifstream f(std::string(REPO_ROOT "/") + rel, std::ios::binary);
    REQUIRE_MESSAGE(f.good(), rel);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::vector<std::int32_t> bodyInts(const std::string& body) {
    std::vector<std::int32_t> out;
    const std::regex numRe(R"(-?\d+)");
    for (auto n = std::sregex_iterator(body.begin(), body.end(), numRe); n != std::sregex_iterator(); ++n)
        out.push_back(std::atoi(n->str().c_str()));
    return out;
}

// The initializer body between "= {" and a terminator token (the FIRST
// occurrence after the declaration).
std::string tableBody(const std::string& text, const std::string& decl, const std::string& terminator) {
    const std::size_t at = text.find(decl);
    REQUIRE_MESSAGE(at != std::string::npos, decl.c_str());
    const std::size_t open = text.find("= {", at);
    REQUIRE_MESSAGE(open != std::string::npos, decl.c_str());
    const std::size_t close = text.find(terminator, open);
    REQUIRE_MESSAGE(close != std::string::npos, terminator.c_str());
    return text.substr(open + 3, close - open - 3);
}

} // namespace

// ---------------------------------------------------------------- constants

TEST_CASE("warped constants pin the reference macro values (wm.h:22-:35, definitions.h:430/:435)") {
    CHECK(l17_inter::kWarpedModelPrecBits == 16);
    CHECK(l17_inter::kWarpedModelTransClamp == 128 << 16);
    CHECK(l17_inter::kWarpedModelNondiagaffineClamp == 1 << 13);
    CHECK(l17_inter::kWarpedPixelPrecBits == 6);
    CHECK(l17_inter::kWarpedPixelPrecShifts == 64);
    CHECK(l17_inter::kWarpParamReduceBits == 6);
    CHECK(l17_inter::kWarpeddiffPrecBits == 10);
    CHECK(l17_inter::kFilterBits == 7);
    CHECK(l17_inter::kDistPrecisionBits == 4);
}

// ---------------------------------------------------------------- block tables

TEST_CASE("block size tables match common_utils.c entry-for-entry (cu:286-:291)") {
    const std::string text = readFile("third_party/SVT-AV1/Source/Lib/Codec/common_utils.c");
    const std::vector<std::int32_t> wide =
        bodyInts(tableBody(text, "block_size_wide[BLOCK_SIZES_ALL]", "};"));
    const std::vector<std::int32_t> high =
        bodyInts(tableBody(text, "block_size_high[BLOCK_SIZES_ALL]", "};"));
    REQUIRE(wide.size() == l17_inter::kBlockSizesAll);
    REQUIRE(high.size() == l17_inter::kBlockSizesAll);
    int mismatches = 0;
    for (int i = 0; i < l17_inter::kBlockSizesAll; ++i) {
        if (l17_inter::blockSizeWide[i] != wide[i]) ++mismatches;
        if (l17_inter::blockSizeHigh[i] != high[i]) ++mismatches;
    }
    CHECK(mismatches == 0);
    CHECK(l17_inter::blockSizeWide[l17_inter::BLOCK_64X64] == 64);
    CHECK(l17_inter::blockSizeHigh[l17_inter::BLOCK_64X64] == 64);
    // The non-square tail (BLOCK_4X16..BLOCK_64X16): the transpose region.
    CHECK(l17_inter::blockSizeWide[l17_inter::BLOCK_4X16] == 4);
    CHECK(l17_inter::blockSizeHigh[l17_inter::BLOCK_4X16] == 16);
}

// ---------------------------------------------------------------- div_lut

TEST_CASE("div_lut matches the reference entry-for-entry (wm:295-:313)") {
    const std::string text = readFile("third_party/SVT-AV1/Source/Lib/Codec/warped_motion.c");
    const std::vector<std::int32_t> ref = bodyInts(tableBody(text, "div_lut[DIV_LUT_NUM + 1]", "};"));
    REQUIRE(ref.size() == 257);
    int mismatches = 0;
    for (int i = 0; i < 257; ++i)
        if (l17_inter::divLut[i] != ref[i]) ++mismatches;
    CHECK(mismatches == 0);
    CHECK(l17_inter::divLut[0] == 16384);
    CHECK(l17_inter::divLut[256] == 8192);
}

// Independent approximation property: div_lut[i] ~ 2^22/(256+i) to the
// nearest entry (half-ULP), i.e. |y*(256+i) - 2^22| <= 256 for every i.
// This is derived arithmetic, not a transcription, so it detects a
// transcription error the diff-verify could mask only if BOTH erred
// identically (the paired-metric caveat is stated: the diff-verify is a
// transcription check).
TEST_CASE("div_lut satisfies the 2^22/(256+i) approximation property") {
    for (int i = 0; i <= 256; ++i) {
        const std::int64_t lhs = (std::int64_t)l17_inter::divLut[i] * (256 + i);
        CHECK(lhs <= (1 << 22) + 256);
        CHECK(lhs >= (1 << 22) - 256);
    }
}

// ---------------------------------------------------------------- resolve_divisor

// Independent transcription of the reference arithmetic (wm:334-:349), used
// as the oracle; the port must agree with it AND with the 1/D property below.
namespace {
std::int32_t oracleGetMsb32(std::uint32_t n) {
    std::int32_t d = 31;
    while (d >= 0 && ((n & (1u << d)) == 0)) --d;
    return d;
}

std::int16_t oracleResolveDivisor32(std::uint32_t d, std::int16_t* shift) {
    *shift = (std::int16_t)oracleGetMsb32(d);
    const std::int32_t e = (std::int32_t)(d - (1u << *shift));
    std::int32_t f;
    if (*shift > 8) {
        f = (e + (1 << (*shift - 9))) >> (*shift - 8); // ROUND_POWER_OF_TWO(e, shift-8)
    } else {
        f = e << (8 - *shift);
    }
    *shift += 14;
    return l17_inter::divLut[f];
}

std::int16_t oracleResolveDivisor64(std::uint64_t d, std::int16_t* shift) {
    *shift = (std::int16_t)((d >> 32) ? oracleGetMsb32((std::uint32_t)(d >> 32)) + 32
                                      : oracleGetMsb32((std::uint32_t)d));
    const std::int64_t e = (std::int64_t)(d - ((std::uint64_t)1 << *shift));
    std::int64_t f;
    if (*shift > 8) {
        f = (e + (((std::int64_t)1 << (*shift - 8)) >> 1)) >> (*shift - 8);
    } else {
        f = e << (8 - *shift);
    }
    *shift += 14;
    return l17_inter::divLut[f];
}
} // namespace

TEST_CASE("resolve_divisor_32 matches the oracle over the dense low domain and the msb boundaries") {
    // Dense: every D in [1, 65536].
    std::uint32_t checked = 0;
    for (std::uint32_t d = 1; d <= 65536; ++d) {
        std::int16_t shift = -1, oshift = -1;
        const std::int16_t got = l17_inter::resolveDivisor32(d, &shift);
        const std::int16_t want = oracleResolveDivisor32(d, &oshift);
        ++checked;
        if (got != want || shift != oshift) {
            CHECK_MESSAGE(false, "dense-domain mismatch at D=" << d << ": got " << got << "/" << shift
                                                               << " want " << want << "/" << oshift);
            break;
        }
    }
    CHECK_EQ(checked, 65536u);
    // Boundary band: for every msb s, {2^s, 2^s+1, 2^s + 2^(s-1), 2^(s+1)-1,
    // and the f==256 corner family 2^(s+1) - 2^(s-9)}. s stops at 30: at
    // s = 31 the reference's own ROUND_POWER_OF_TWO(e, s-8) overflows int32
    // for e > 2^31 - 2^22 (D above ~4290772991) - a latent UB in the
    // REFERENCE (unreachable in the encoder: resolve_divisor_32 consumes
    // abs(mat[2]), a 16.16 fixed-point value far below 2^31), so the tests
    // cover the defined domain only; the finding is named in the slice
    // report. The s=30 max, 2^31-1, still reaches the f==256 corner.
    for (int s = 0; s <= 30; ++s) {
        std::vector<std::uint32_t> ds = {1u << s, (1u << s) + 1};
        if (s >= 1) ds.push_back((1u << s) + (1u << (s - 1)));
        ds.push_back((1u << (s + 1)) - 1);
        if (s >= 9) ds.push_back((1u << (s + 1)) - (1u << (s - 9)));
        for (std::uint32_t d : ds) {
            std::int16_t shift = -1, oshift = -1;
            const std::int16_t got = l17_inter::resolveDivisor32(d, &shift);
            const std::int16_t want = oracleResolveDivisor32(d, &oshift);
            CHECK(got == want);
            CHECK(shift == oshift);
        }
    }
    // The f==256 corner pinned by value: D = 1023 -> e = 511, f = 256,
    // div_lut[256] = 8192, shift = 9 + 14 = 23. Contrast D = 768 -> e = 256,
    // f = 128 -> div_lut[128] = 10923 (same shift).
    std::int16_t shift = -1;
    CHECK(l17_inter::resolveDivisor32(1023, &shift) == 8192);
    CHECK(shift == 23);
    shift = -1;
    CHECK(l17_inter::resolveDivisor32(768, &shift) == 10923);
    CHECK(shift == 23);
}

TEST_CASE("resolve_divisor_32 satisfies the 1/D ~ y/2^shift property") {
    // |y*D - 2^shift| <= 2^shift/32 (the error bound derived from the
    // half-ULP LUT rounding plus the 8-bit f quantisation; see the derivation
    // in the slice report). Checked over the dense domain and the boundaries.
    auto checkProperty = [](std::uint64_t d) {
        std::int16_t shift = -1;
        const std::int32_t y = l17_inter::resolveDivisor32((std::uint32_t)d, &shift);
        const std::int64_t lhs = (std::int64_t)y * (std::int64_t)d;
        const std::int64_t rhs = (std::int64_t)1 << shift;
        const std::int64_t diff = lhs - rhs;
        CHECK_MESSAGE(diff <= rhs / 32, "D=" << d << " y=" << y << " shift=" << shift << " diff=" << diff);
        CHECK_MESSAGE(diff >= -rhs / 32, "D=" << d << " y=" << y << " shift=" << shift << " diff=" << diff);
    };
    for (std::uint32_t d = 1; d <= 65536; ++d) checkProperty(d);
    // msb band 0..30 (see the reference-UB note on the boundary-band case).
    for (int s = 0; s <= 30; ++s) {
        checkProperty((std::uint64_t)1 << s);
        checkProperty(((std::uint64_t)1 << s) + 1);
        checkProperty(((std::uint64_t)1 << (s + 1)) - 1);
    }
}

TEST_CASE("resolve_divisor_64 matches the oracle including the D>>32 branch") {
    const std::vector<std::uint64_t> ds = {
        1, 2, 3, 255, 256, 257, 511, 512, 513, 767, 768, 769, 65535, 65536, 65537,
        (std::uint64_t)1 << 31, ((std::uint64_t)1 << 31) + 1, ((std::uint64_t)1 << 32) - 1,
        (std::uint64_t)1 << 32, ((std::uint64_t)1 << 32) + 1, ((std::uint64_t)1 << 40) + 12345,
        ((std::uint64_t)1 << 62) + 7, ((std::uint64_t)1 << 63) + ((std::uint64_t)1 << 21),
    };
    for (std::uint64_t d : ds) {
        std::int16_t shift = -1, oshift = -1;
        const std::int16_t got = l17_inter::resolveDivisor64(d, &shift);
        const std::int16_t want = oracleResolveDivisor64(d, &oshift);
        CHECK(got == want);
        CHECK(shift == oshift);
    }
    // D = 2^32: msb comes from the >>32 branch (get_msb(1) + 32 = 32),
    // e = 0 -> f = 0, div_lut[0] = 16384, shift = 32 + 14 = 46.
    std::int16_t shift = -1;
    CHECK(l17_inter::resolveDivisor64((std::uint64_t)1 << 32, &shift) == 16384);
    CHECK(shift == 46);
}

// ---------------------------------------------------------------- mult shift

// Independent transcription of the ACTIVE branch (wm:276-:288, the
// USE_LIMITED_PREC_MULT=0 build, wm:218) with the reference macro bodies
// (definitions.h:460-:468, :687-:693) spelled out.
namespace {
std::int32_t oracleClamp32(std::int32_t v, std::int32_t lo, std::int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

namespace {
std::int64_t oracleRoundPow2Signed64(std::int64_t v, int n) {
    if (v < 0) return -((-v + (((std::int64_t)1 << n) >> 1)) >> n);
    return (v + (((std::int64_t)1 << n) >> 1)) >> n;
}

std::int64_t oracleClamp64(std::int64_t v, std::int64_t lo, std::int64_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

std::int32_t oracleMultShiftNdiag(std::int64_t pX, std::int16_t iDet, int shift) {
    const std::int64_t v = pX * (std::int64_t)iDet;
    return (std::int32_t)oracleClamp64(oracleRoundPow2Signed64(v, shift),
                                       -l17_inter::kWarpedModelNondiagaffineClamp + 1,
                                       l17_inter::kWarpedModelNondiagaffineClamp - 1);
}

std::int32_t oracleMultShiftDiag(std::int64_t pX, std::int16_t iDet, int shift) {
    const std::int64_t v = pX * (std::int64_t)iDet;
    return (std::int32_t)oracleClamp64(oracleRoundPow2Signed64(v, shift),
                                       (1 << l17_inter::kWarpedModelPrecBits) - l17_inter::kWarpedModelNondiagaffineClamp + 1,
                                       (1 << l17_inter::kWarpedModelPrecBits) + l17_inter::kWarpedModelNondiagaffineClamp - 1);
}
} // namespace

TEST_CASE("get_mult_shift_ndiag/diag match the active-branch oracle over the sweep grid") {
    const std::vector<std::int64_t> pxs = {0, 1, -1, 255, -255, 1 << 15, -(1 << 15), (1 << 23) + 12345,
                                           -((1 << 23) + 12345), (std::int64_t)1 << 33, -((std::int64_t)1 << 33)};
    const std::vector<std::int16_t> idets = {1, -1, 255, -255, std::numeric_limits<std::int16_t>::max(),
                                             std::numeric_limits<std::int16_t>::min()};
    const std::vector<int> shifts = {0, 1, 5, 15, 16, 23, 31, 40};
    int mismatches = 0;
    for (std::int64_t px : pxs)
        for (std::int16_t id : idets)
            for (int sh : shifts) {
                if (l17_inter::getMultShiftNdiag(px, id, sh) != oracleMultShiftNdiag(px, id, sh)) ++mismatches;
                if (l17_inter::getMultShiftDiag(px, id, sh) != oracleMultShiftDiag(px, id, sh)) ++mismatches;
            }
    CHECK(mismatches == 0);
}

TEST_CASE("get_mult_shift pins the clamp bands and the half-way rounding") {
    // ndiag band: [-8191, 8191] (-CLAMP+1 .. CLAMP-1).
    CHECK(l17_inter::getMultShiftNdiag((std::int64_t)1 << 40, 32767, 0) == 8191);
    CHECK(l17_inter::getMultShiftNdiag(-((std::int64_t)1 << 40), 32767, 0) == -8191);
    CHECK(l17_inter::getMultShiftNdiag(8192, 1, 0) == 8191);      // exactly past the top edge
    CHECK(l17_inter::getMultShiftNdiag(8191, 1, 0) == 8191);      // rounds to the top edge
    CHECK(l17_inter::getMultShiftNdiag(8190, 1, 0) == 8190);      // below the edge: unclamped
    CHECK(l17_inter::getMultShiftNdiag(-8192, 1, 0) == -8191);
    CHECK(l17_inter::getMultShiftNdiag(-8191, 1, 0) == -8191);
    CHECK(l17_inter::getMultShiftNdiag(-8190, 1, 0) == -8190);
    // diag band: [65536-8191, 65536+8191] = [57345, 73727].
    CHECK(l17_inter::getMultShiftDiag((std::int64_t)1 << 40, 32767, 0) == 73727);
    CHECK(l17_inter::getMultShiftDiag(-((std::int64_t)1 << 40), 32767, 0) == 57345);
    CHECK(l17_inter::getMultShiftDiag(65536, 1, 0) == 65536);     // the identity point is interior
    CHECK(l17_inter::getMultShiftDiag(73728, 1, 0) == 73727);
    CHECK(l17_inter::getMultShiftDiag(57344, 1, 0) == 57345);
    // Half-way rounding, away from zero at .5 (ROUND_POWER_OF_TWO_SIGNED_64):
    // v = 2^14, shift 15 -> 1; v = -2^14, shift 15 -> -1 (not 0).
    CHECK(l17_inter::getMultShiftNdiag(1 << 14, 1, 15) == 1);
    CHECK(l17_inter::getMultShiftNdiag(-(1 << 14), 1, 15) == -1);
    // Just below half rounds toward zero.
    CHECK(l17_inter::getMultShiftNdiag((1 << 14) - 1, 1, 15) == 0);
    CHECK(l17_inter::getMultShiftNdiag(-((1 << 14) - 1), 1, 15) == 0);
}

// ---------------------------------------------------------------- affine gates

TEST_CASE("is_affine_valid is the mat[2] > 0 predicate (wm:351-:354)") {
    l17_inter::WarpedMotionParams wm;
    std::memset(&wm, 0, sizeof(wm));
    wm.wmmat[2] = 1;
    CHECK(l17_inter::isAffineValid(&wm));
    wm.wmmat[2] = 65536;
    CHECK(l17_inter::isAffineValid(&wm));
    wm.wmmat[2] = 0;
    CHECK(!l17_inter::isAffineValid(&wm));
    wm.wmmat[2] = -1;
    CHECK(!l17_inter::isAffineValid(&wm));
    // Other wmmat entries are irrelevant to the predicate.
    wm.wmmat[2] = 65536;
    wm.wmmat[0] = -1234567;
    wm.wmmat[5] = -1;
    CHECK(l17_inter::isAffineValid(&wm));
}

// Independent transcription of wm:356-:363.
namespace {
bool oracleShearAllowed(int alpha, int beta, int gamma, int delta) {
    if ((4 * std::abs(alpha) + 7 * std::abs(beta) >= (1 << 16)) ||
        (4 * std::abs(gamma) + 4 * std::abs(delta) >= (1 << 16))) {
        return false;
    }
    return true;
}
} // namespace

TEST_CASE("is_affine_shear_allowed rejects exactly on the 4|a|+7|b| / 4|g|+4|d| bands") {
    // Exact boundary pins (the thresholds are strict >= 65536).
    CHECK(l17_inter::isAffineShearAllowed(0, 9362, 0, 0) == true);  // 7*9362 = 65534 < 2^16
    CHECK(l17_inter::isAffineShearAllowed(0, 9363, 0, 0) == false);  // 7*9363 = 65541 >= 2^16
    CHECK(l17_inter::isAffineShearAllowed(16383, 0, 0, 0) == true); // 4*16383 = 65532
    CHECK(l17_inter::isAffineShearAllowed(16384, 0, 0, 0) == false); // 4*16384 = 65536
    CHECK(l17_inter::isAffineShearAllowed(0, 0, 8191, 8192) == true); // 4*(8191+8192) = 65532
    CHECK(l17_inter::isAffineShearAllowed(0, 0, 8192, 8192) == false); // 4*(8192+8192) = 65536
    CHECK(l17_inter::isAffineShearAllowed(0, 0, 16384, 0) == false);
    CHECK(l17_inter::isAffineShearAllowed(0, 0, 0, 16384) == false);
    CHECK(l17_inter::isAffineShearAllowed(0, 0, -16384, 0) == false);
    CHECK(l17_inter::isAffineShearAllowed(-16384, 9362, 0, 0) == false); // 4*16384 + 0 >= 2^16 regardless of beta
    // Mixed-term coupling: alpha alone is under the edge, alpha+beta is not.
    // a=8000: 4*8000 = 32000, so 7|b| < 33536 -> |b| <= 4790 allowed.
    CHECK(l17_inter::isAffineShearAllowed(8000, 4790, 0, 0) == true); // 32000 + 33530 = 65530 < 2^16
    CHECK(l17_inter::isAffineShearAllowed(8000, 4791, 0, 0) == false); // 32000 + 33537 = 65537 >= 2^16
    // Grid enumeration against the oracle (mismatch count, the AN8 pattern).
    int mismatches = 0;
    for (int a = -17000; a <= 17000; a += 317)
        for (int b = -10000; b <= 10000; b += 311)
            for (int g = -17000; g <= 17000; g += 307)
                for (int d = -9000; d <= 9000; d += 313)
                    if (l17_inter::isAffineShearAllowed((std::int16_t)a, (std::int16_t)b, (std::int16_t)g,
                                                        (std::int16_t)d) != oracleShearAllowed(a, b, g, d))
                        ++mismatches;
    CHECK(mismatches == 0);
}

// ---------------------------------------------------------------- find affine

// Independent transcription of find_affine_int (wm:365-:471) with bw/bh
// threaded (the blockSizeWide/High table read is the caller-side consume).
// The resolve_divisor_64 / get_mult_shift foundations are the cycle-2/3
// oracle-verified surfaces.
namespace {
std::int32_t oracleLsSquare(std::int32_t a) { return (a * a * 4 + a * 4 * 8 + 8 * 8 * 2) >> (2 + 2); }

std::int32_t oracleLsProduct1(std::int32_t a, std::int32_t b) {
    return (a * b * 4 + ((a) + (b)) * 2 * 8 + 8 * 8) >> (2 + 2);
}

std::int32_t oracleLsProduct2(std::int32_t a, std::int32_t b) {
    return (a * b * 4 + ((a) + (b)) * 2 * 8 + 8 * 8 * 2) >> (2 + 2);
}

struct AffineOracleResult {
    int detZero;
    std::int32_t wmmat[6];
};

AffineOracleResult oracleFindAffineInt(int np, const std::int32_t* pts1, const std::int32_t* pts2, int bw, int bh,
                                       std::int16_t mvx, std::int16_t mvy, int miRow, int miCol) {
    std::int32_t A[2][2] = {{0, 0}, {0, 0}};
    std::int32_t bx[2] = {0, 0};
    std::int32_t by[2] = {0, 0};
    const int rsuy = (std::max(bh, 4) / 2 - 1);
    const int rsux = (std::max(bw, 4) / 2 - 1);
    const int suy = rsuy * 8;
    const int sux = rsux * 8;
    const int duy = suy + mvy;
    const int dux = sux + mvx;
    const int isuy = (miRow * 4 + rsuy);
    const int isux = (miCol * 4 + rsux);

    for (int i = 0; i < np; ++i) {
        const int dx = pts2[i * 2] - dux;
        const int dy = pts2[i * 2 + 1] - duy;
        const int sx = pts1[i * 2] - sux;
        const int sy = pts1[i * 2 + 1] - suy;
        if (std::abs(sx - dx) < 256 && std::abs(sy - dy) < 256) {
            A[0][0] += oracleLsSquare(sx);
            A[0][1] += oracleLsProduct1(sx, sy);
            A[1][1] += oracleLsSquare(sy);
            bx[0] += oracleLsProduct2(sx, dx);
            bx[1] += oracleLsProduct1(sy, dx);
            by[0] += oracleLsProduct1(sx, dy);
            by[1] += oracleLsProduct2(sy, dy);
        }
    }

    AffineOracleResult r = {0, {0, 0, 1 << 16, 0, 0, 1 << 16}};
    const std::int64_t det = (std::int64_t)A[0][0] * A[1][1] - (std::int64_t)A[0][1] * A[0][1];
    if (det == 0) {
        r.detZero = 1;
        return r;
    }
    std::int16_t shift;
    std::int16_t iDet = (std::int16_t)(l17_inter::resolveDivisor64((std::uint64_t)std::llabs(det), &shift) *
                                       (det < 0 ? -1 : 1));
    shift = (std::int16_t)(shift - 16);
    if (shift < 0) {
        iDet = (std::int16_t)(iDet << (-shift));
        shift = 0;
    }

    std::int64_t pX[2], pY[2];
    pX[0] = (std::int64_t)A[1][1] * bx[0] - (std::int64_t)A[0][1] * bx[1];
    pX[1] = -(std::int64_t)A[0][1] * bx[0] + (std::int64_t)A[0][0] * bx[1];
    pY[0] = (std::int64_t)A[1][1] * by[0] - (std::int64_t)A[0][1] * by[1];
    pY[1] = -(std::int64_t)A[0][1] * by[0] + (std::int64_t)A[0][0] * by[1];

    r.wmmat[2] = l17_inter::getMultShiftDiag(pX[0], iDet, shift);
    r.wmmat[3] = l17_inter::getMultShiftNdiag(pX[1], iDet, shift);
    r.wmmat[4] = l17_inter::getMultShiftNdiag(pY[0], iDet, shift);
    r.wmmat[5] = l17_inter::getMultShiftDiag(pY[1], iDet, shift);

    const std::int32_t vx =
        mvx * (1 << 13) - (isux * (r.wmmat[2] - (1 << 16)) + isuy * r.wmmat[3]);
    const std::int32_t vy =
        mvy * (1 << 13) - (isux * r.wmmat[4] + isuy * (r.wmmat[5] - (1 << 16)));
    r.wmmat[0] = oracleClamp32(vx, -(128 << 16), (128 << 16) - 1);
    r.wmmat[1] = oracleClamp32(vy, -(128 << 16), (128 << 16) - 1);
    return r;
}

// Synthetic neighborhood samples, the shape the reference's callers feed in:
// record_samples (adaptive_mv_pred.c:1594-:1605) emits the neighbor block
// centers RELATIVE to the current block's top-left, at 1/8-PEL precision
// (x*8, y*8). Fixture: a 64x64 block with 8x8 above-row neighbors at
// y = -33 px and left-column neighbors at x = -5 px (the record_samples
// formulas: sign_r*max(bh,4)/2 - 1 and sign_c*max(bw,4)/2 - 1).
void ringPoints(std::int32_t* pts, int bx, int by) {
    (void)bx;
    (void)by; // samples are block-relative; the mi origin enters via miRow/miCol
    const std::int32_t offsPx[8][2] = {{3, -33},  {19, -33}, {35, -33}, {51, -33},
                                       {63, -33}, {-5, 3},   {-5, 35},  {-5, 63}};
    for (int i = 0; i < 8; ++i) {
        pts[i * 2] = offsPx[i][0] * 8;
        pts[i * 2 + 1] = offsPx[i][1] * 8;
    }
}
} // namespace

TEST_CASE("find_projection recovers a known rotzoom within sub-pixel error and matches the oracle") {
    // Ground truth: rotation+zoom about the block center (c=1.02, s=0.01)
    // plus translation (3, -2) px, sampled at 1/8-pel. The current block's
    // mv is the model's own prediction at the (rsux, rsuy) = (31, 31) center
    // (the reference's "the center pixel of the block has exactly the same
    // motion vector as transmitted"), derived from the same ground truth.
    const double c = 1.02, s = 0.01, cx = 31.0, cy = 31.0;
    const double tx = 3.0, ty = -2.0;
    std::int32_t pts1[16], pts2[16];
    ringPoints(pts1, 0, 0);
    for (int i = 0; i < 8; ++i) {
        const double x = pts1[i * 2] / 8.0, y = pts1[i * 2 + 1] / 8.0;
        pts2[i * 2] = (std::int32_t)std::lround(8.0 * (c * (x - cx) - s * (y - cy) + cx + tx));
        pts2[i * 2 + 1] = (std::int32_t)std::lround(8.0 * (s * (x - cx) + c * (y - cy) + cy + ty));
    }
    const std::int16_t mvx =
        (std::int16_t)std::lround(8.0 * (c * (cx - cx) - s * (cy - cy) + cx + tx - cx));
    const std::int16_t mvy =
        (std::int16_t)std::lround(8.0 * (s * (cx - cx) + c * (cy - cy) + cy + ty - cy));
    l17_inter::WarpedMotionParams wm = l17_inter::defaultWarpParams;
    l17_inter::Mv mv{mvx, mvy};
    // mi_row = mi_col = 0: the vx/vy constant-term reconstruction is in
    // ABSOLUTE pixel coordinates (isux = mi_col*4 + rsux, wm:380-:381), so
    // the fixture places the block at the frame origin where the relative
    // sample coordinates and the absolute model domain coincide.
    const bool failed = l17_inter::findProjection(8, pts1, pts2, l17_inter::BLOCK_64X64, mv, &wm, 0, 0);
    const AffineOracleResult oracle = oracleFindAffineInt(8, pts1, pts2, 64, 64, mvx, mvy, 0, 0);
    CHECK(failed == false);
    CHECK(oracle.detZero == 0);
    CHECK(wm.wmmat[2] == oracle.wmmat[2]);
    CHECK(wm.wmmat[3] == oracle.wmmat[3]);
    CHECK(wm.wmmat[4] == oracle.wmmat[4]);
    CHECK(wm.wmmat[5] == oracle.wmmat[5]);
    CHECK(wm.wmmat[0] == oracle.wmmat[0]);
    CHECK(wm.wmmat[1] == oracle.wmmat[1]);
    // Semantic recovery: the 16.16 model maps each source point (pixels)
    // within 1 px of the known destination. The comparison is done in the
    // 1/8-pel domain (dest_eighth = pts2 exactly; pred_eighth derived from
    // the 16.16 model scaled by 8) so no integer truncation pollutes the
    // error. The div_lut 14-bit precision bounds the recovered coefficients
    // at ~0.2% relative; the measured max is reported.
    int badPts = 0;
    std::int64_t maxErr = 0; // in 1/8-px units, scaled by 2^16
    for (int i = 0; i < 8; ++i) {
        const std::int64_t srcX = pts1[i * 2], srcY = pts1[i * 2 + 1]; // eighths
        const std::int64_t px = (std::int64_t)wm.wmmat[2] * srcX + (std::int64_t)wm.wmmat[3] * srcY +
            (std::int64_t)wm.wmmat[0] * 8; // 16.16 eighths
        const std::int64_t py = (std::int64_t)wm.wmmat[4] * srcX + (std::int64_t)wm.wmmat[5] * srcY +
            (std::int64_t)wm.wmmat[1] * 8;
        const std::int64_t errX = std::llabs(px - ((std::int64_t)pts2[i * 2] << 16));
        const std::int64_t errY = std::llabs(py - ((std::int64_t)pts2[i * 2 + 1] << 16));
        if (errX > maxErr) maxErr = errX;
        if (errY > maxErr) maxErr = errY;
        if (errX > (1LL << 16) || errY > (1LL << 16)) ++badPts;
    }
    CHECK(badPts == 0);
    CHECK_MESSAGE(maxErr <= (1LL << 16), "max prediction error (16.16 eighths = 1/8-px units) = " << maxErr);
    // And the model is inside the model-domain clamps.
    CHECK(wm.wmmat[2] > 0);
}

TEST_CASE("find_projection on a consistent pure translation recovers the identity coefficient") {
    // Global translation (5, -3) px with every sample moving by exactly mv:
    // pts_inref = pts + mv (the record_samples convention). The LS residual
    // dx - sx is then identically zero, so the recovered linear part is the
    // identity up to the div_lut quantization (14-bit, ~2%), and the
    // translation terms carry the mv-driven vx/vy.
    std::int32_t pts1[16], pts2[16];
    ringPoints(pts1, 0, 0);
    for (int i = 0; i < 8; ++i) {
        pts2[i * 2] = pts1[i * 2] + 40;
        pts2[i * 2 + 1] = pts1[i * 2 + 1] - 24;
    }
    l17_inter::WarpedMotionParams wm = l17_inter::defaultWarpParams;
    l17_inter::Mv mv{40, -24};
    const bool failed = l17_inter::findProjection(8, pts1, pts2, l17_inter::BLOCK_64X64, mv, &wm, 8, 8);
    const AffineOracleResult oracle = oracleFindAffineInt(8, pts1, pts2, 64, 64, 40, -24, 8, 8);
    CHECK(failed == false);
    CHECK(oracle.detZero == 0);
    // Linear part: identity within the div_lut relative precision (~2% of
    // 2^16 ~ 1200; the bound 1<<11 is the derived worst case, measured max
    // in the slice report).
    CHECK(std::llabs((std::int64_t)wm.wmmat[2] - (1 << 16)) <= (1 << 11));
    CHECK(std::llabs((std::int64_t)wm.wmmat[5] - (1 << 16)) <= (1 << 11));
    CHECK(std::llabs((std::int64_t)wm.wmmat[3]) <= (1 << 11));
    CHECK(std::llabs((std::int64_t)wm.wmmat[4]) <= (1 << 11));
    CHECK(wm.wmmat[2] == oracle.wmmat[2]);
    CHECK(wm.wmmat[0] == oracle.wmmat[0]);
    CHECK(wm.wmmat[1] == oracle.wmmat[1]);
    // Semantic: predicts every sample within 1 px, measured in the 1/8-pel
    // domain (the isux/isuy lever on the ~0.2% coefficient error is small;
    // the measured max is reported).
    std::int64_t maxErr = 0;
    for (int i = 0; i < 8; ++i) {
        const std::int64_t srcX = pts1[i * 2], srcY = pts1[i * 2 + 1];
        const std::int64_t px = (std::int64_t)wm.wmmat[2] * srcX + (std::int64_t)wm.wmmat[3] * srcY +
            (std::int64_t)wm.wmmat[0] * 8;
        const std::int64_t py = (std::int64_t)wm.wmmat[4] * srcX + (std::int64_t)wm.wmmat[5] * srcY +
            (std::int64_t)wm.wmmat[1] * 8;
        maxErr = std::max(maxErr, std::llabs(px - ((std::int64_t)pts2[i * 2] << 16)));
        maxErr = std::max(maxErr, std::llabs(py - ((std::int64_t)pts2[i * 2 + 1] << 16)));
    }
    CHECK_MESSAGE(maxErr <= (1LL << 16), "max translation prediction error (16.16 eighths) = " << maxErr);
}

TEST_CASE("find_projection fails on an empty sample set (the exact det == 0 path)") {
    // FINDING (named in the slice report): the quantized LS accumulation
    // never produces det == 0 for any NON-EMPTY integer sample set - the
    // LS_SQUARE floor keeps A00/A11 >= 4 per sample and the LS_PRODUCT1
    // constant (LS_STEP^2 vs 2*LS_STEP^2) keeps A01 off the singular
    // manifold, so identical or collinear points are NOT singular in this
    // arithmetic. The det == 0 guard (wm:436-:438) is reachable through the
    // public surface only for np == 0.
    std::int32_t pts1[2] = {40, 40};
    std::int32_t pts2[2] = {44, 42};
    l17_inter::WarpedMotionParams wm = l17_inter::defaultWarpParams;
    l17_inter::Mv mv{0, 0};
    CHECK(l17_inter::findProjection(0, pts1, pts2, l17_inter::BLOCK_64X64, mv, &wm, 8, 8) == true);
}

TEST_CASE("find_projection matches the oracle on a negative-determinant sample set") {
    // Single sample at (100, -100) eighths: A01 goes negative and
    // det = S(100)*S(-100) - P1(100,-100)^2 = 2702*2302 - 2498^2 = -19880
    // < 0, exercising the i_det sign branch (wm:439).
    std::int32_t pts1[2] = {800, -800};
    std::int32_t pts2[2] = {800, -800};
    l17_inter::WarpedMotionParams wm = l17_inter::defaultWarpParams;
    l17_inter::Mv mv{0, 0};
    const bool failed = l17_inter::findProjection(1, pts1, pts2, l17_inter::BLOCK_64X64, mv, &wm, 8, 8);
    const AffineOracleResult oracle = oracleFindAffineInt(1, pts1, pts2, 64, 64, 0, 0, 8, 8);
    CHECK(oracle.detZero == 0);
    CHECK(failed == (oracle.detZero != 0));
    CHECK(wm.wmmat[2] == oracle.wmmat[2]);
    CHECK(wm.wmmat[3] == oracle.wmmat[3]);
    CHECK(wm.wmmat[4] == oracle.wmmat[4]);
    CHECK(wm.wmmat[5] == oracle.wmmat[5]);
    CHECK(wm.wmmat[0] == oracle.wmmat[0]);
    CHECK(wm.wmmat[1] == oracle.wmmat[1]);
}

TEST_CASE("find_projection fails when the recovered model violates the shear gate") {
    // A strong rotation (45 degrees) about the block center: the recovered
    // alpha/beta exceed the fast warp filter's 8-tap constraint.
    const double ang = 0.7853981633974483;
    const double c = std::cos(ang), s = std::sin(ang), cx = 64.0, cy = 64.0;
    std::int32_t pts1[16], pts2[16];
    ringPoints(pts1, 64, 64);
    for (int i = 0; i < 8; ++i) {
        const double x = pts1[i * 2], y = pts1[i * 2 + 1];
        pts2[i * 2] = (std::int32_t)std::lround(c * (x - cx) - s * (y - cy) + cx);
        pts2[i * 2 + 1] = (std::int32_t)std::lround(s * (x - cx) + c * (y - cy) + cy);
    }
    l17_inter::WarpedMotionParams wm = l17_inter::defaultWarpParams;
    l17_inter::Mv mv{0, 0};
    CHECK(l17_inter::findProjection(8, pts1, pts2, l17_inter::BLOCK_64X64, mv, &wm, 16, 16) == true);
}

TEST_CASE("find_affine_int honors the LS_MV_MAX sample filter (wm:411)") {
    // One far outlier (|sx - dx| >= 256 eighths) must be excluded from the
    // accumulation: the result equals the oracle's on the filtered set, and
    // differs from including it.
    std::int32_t pts1[18], pts2[18];
    ringPoints(pts1, 0, 0);
    for (int i = 0; i < 8; ++i) {
        pts2[i * 2] = pts1[i * 2] + 40;
        pts2[i * 2 + 1] = pts1[i * 2 + 1] - 24;
    }
    // Outlier: neighbor 600 px right with mv pointing 200 px back:
    // sx - dx = (4800-248) - (3200-288) = 1640 eighths >= 256.
    pts1[16] = 600 * 8;
    pts1[17] = 0;
    pts2[16] = 400 * 8;
    pts2[17] = 0;
    l17_inter::WarpedMotionParams wm = l17_inter::defaultWarpParams;
    l17_inter::Mv mv{40, -24};
    CHECK(l17_inter::findProjection(9, pts1, pts2, l17_inter::BLOCK_64X64, mv, &wm, 8, 8) == false);
    const AffineOracleResult oracle = oracleFindAffineInt(9, pts1, pts2, 64, 64, 40, -24, 8, 8);
    CHECK(wm.wmmat[2] == oracle.wmmat[2]);
    CHECK(wm.wmmat[0] == oracle.wmmat[0]);
    // The outlier-free reference solve: the same answer as the 8-point case.
    l17_inter::WarpedMotionParams wm8 = l17_inter::defaultWarpParams;
    CHECK(l17_inter::findProjection(8, pts1, pts2, l17_inter::BLOCK_64X64, mv, &wm8, 8, 8) == false);
    CHECK(wm.wmmat[2] == wm8.wmmat[2]);
    CHECK(wm.wmmat[0] == wm8.wmmat[0]);
}

// Independent transcription of svt_get_shear_params (wm:907-:933). The
// resolveDivisor32 foundation is the cycle-2 oracle-verified surface; the
// macro bodies are the definitions.h:456-:468 / :687-:693 shapes.
namespace {

std::int32_t oracleRoundPow2Signed32(std::int32_t v, int n) {
    if (v < 0) return -((-v + ((1 << n) >> 1)) >> n);
    return (v + ((1 << n) >> 1)) >> n;
}

struct ShearResult {
    int ok;
    std::int16_t alpha, beta, gamma, delta;
};

ShearResult oracleGetShearParams(const std::int32_t* mat) {
    ShearResult r = {0, 0, 0, 0, 0};
    if (!(mat[2] > 0)) {
        // The reference returns 0 before writing any shear field; the fields
        // stay untouched in both the port and the oracle (zeroed here).
        r.ok = 0;
        return r;
    }
    std::int16_t alpha = (std::int16_t)oracleClamp32(mat[2] - (1 << 16), -32768, 32767);
    std::int16_t beta = (std::int16_t)oracleClamp32(mat[3], -32768, 32767);
    std::int16_t shift;
    std::int16_t y = (std::int16_t)(l17_inter::resolveDivisor32((std::uint32_t)std::abs(mat[2]), &shift) *
                                    (mat[2] < 0 ? -1 : 1));
    std::int64_t v = ((std::int64_t)mat[4] * (1 << 16)) * y;
    std::int16_t gamma = (std::int16_t)oracleClamp32((std::int32_t)oracleRoundPow2Signed64(v, shift), -32768, 32767);
    v = ((std::int64_t)mat[3] * mat[4]) * y;
    std::int16_t delta =
        (std::int16_t)oracleClamp32(mat[5] - (std::int32_t)oracleRoundPow2Signed64(v, shift) - (1 << 16), -32768, 32767);
    alpha = (std::int16_t)(oracleRoundPow2Signed32(alpha, 6) * 64);
    beta = (std::int16_t)(oracleRoundPow2Signed32(beta, 6) * 64);
    gamma = (std::int16_t)(oracleRoundPow2Signed32(gamma, 6) * 64);
    delta = (std::int16_t)(oracleRoundPow2Signed32(delta, 6) * 64);
    r.ok = oracleShearAllowed(alpha, beta, gamma, delta);
    r.alpha = alpha;
    r.beta = beta;
    r.gamma = gamma;
    r.delta = delta;
    return r;
}
} // namespace

TEST_CASE("get_shear_params on the identity matrix is exact and accepted (wm:907-:933)") {
    l17_inter::WarpedMotionParams wm = l17_inter::defaultWarpParams;
    CHECK(l17_inter::getShearParams(&wm) == true);
    CHECK(wm.alpha == 0);
    CHECK(wm.beta == 0);
    CHECK(wm.gamma == 0);
    CHECK(wm.delta == 0);
}

TEST_CASE("get_shear_params rejects mat[2] <= 0 and the shear-violating zoom (wm:907-:933)") {
    // mat[2] = 0 -> is_affine_valid fails before anything else.
    l17_inter::WarpedMotionParams wm = l17_inter::defaultWarpParams;
    wm.wmmat[2] = 0;
    CHECK(l17_inter::getShearParams(&wm) == false);
    wm.wmmat[2] = -65536;
    CHECK(l17_inter::getShearParams(&wm) == false);
    // 1.5x zoom: alpha clamps to INT16_MAX, rounds back up to 32768, and the
    // int16_t store WRAPS to -32768 (the reference's own narrowing, the SEG1
    // POW2-wrap quirk family). Every step is direct arithmetic
    // (clamp -> round-half-away -> re-scale -> wrap -> gate):
    // 32767 -> (32767+32)>>6 = 512 -> 512*64 = 32768 -> -32768.
    wm.wmmat[2] = 98304; // 1.5 << 16
    wm.wmmat[5] = 98304;
    CHECK(l17_inter::getShearParams(&wm) == false);
    CHECK(wm.alpha == -32768);
    CHECK(wm.delta == -32768);
    CHECK(wm.beta == 0);
    CHECK(wm.gamma == 0);
}

TEST_CASE("get_shear_params matches the transcription oracle over the wmmat grid") {
    // Grid over the four consumed matrix entries around 1<<16 and beyond,
    // including negatives for [3]/[4] and mat[2] <= 0.
    int mismatches = 0;
    for (std::int32_t m2 = -(1 << 17); m2 <= (1 << 17) + 512; m2 += 5119) {
        for (std::int32_t m3 = -(1 << 14); m3 <= (1 << 14); m3 += 1667) {
            for (std::int32_t m4 = -(1 << 14); m4 <= (1 << 14); m4 += 1693) {
                for (std::int32_t m5 = (1 << 16) - (1 << 12); m5 <= (1 << 16) + (1 << 12); m5 += 499) {
                    std::int32_t mat[6] = {0, 0, m2, m3, m4, m5};
                    l17_inter::WarpedMotionParams wm = l17_inter::defaultWarpParams;
                    std::memcpy(wm.wmmat, mat, sizeof(mat));
                    const bool got = l17_inter::getShearParams(&wm);
                    const ShearResult want = oracleGetShearParams(mat);
                    if (got != (want.ok != 0) || wm.alpha != want.alpha || wm.beta != want.beta ||
                        wm.gamma != want.gamma || wm.delta != want.delta) {
                        ++mismatches;
                        CHECK_MESSAGE(mismatches < 5, "shear mismatch at m2=" << m2 << " m3=" << m3
                                                                              << " m4=" << m4 << " m5=" << m5);
                    }
                }
            }
        }
    }
    CHECK(mismatches == 0);
}

// ---------------------------------------------------------------- warp filter

TEST_CASE("warped_filter matches the reference entry-for-entry (wm:57-:216, the ==6 branch)") {
    const std::string text = readFile("third_party/SVT-AV1/Source/Lib/Codec/warped_motion.c");
    const std::size_t start = text.find("svt_aom_warped_filter[WARPEDPIXEL_PREC_SHIFTS * 3 + 1][8] = {");
    REQUIRE(start != std::string::npos);
    const std::size_t ifLine = text.find("#if WARPEDPIXEL_PREC_BITS == 6", start);
    REQUIRE(ifLine != std::string::npos);
    const std::size_t bodyStart = text.find("\n", ifLine);
    const std::size_t elif = text.find("#elif", bodyStart);
    REQUIRE(elif != std::string::npos);
    std::string body = text.substr(bodyStart, elif - bodyStart);
    // Strip line comments and normalise the reference's spaced negatives.
    body = std::regex_replace(body, std::regex("//[^\\r\\n]*"), std::string(""));
    body = std::regex_replace(body, std::regex("-\\s+(\\d)"), std::string("-$1"));
    const std::vector<std::int32_t> ref = bodyInts(body);
    REQUIRE(ref.size() == 1544);
    int mismatches = 0;
    for (int r = 0; r < 193; ++r)
        for (int c = 0; c < 8; ++c)
            if (l17_inter::warpedFilter[r][c] != (std::int16_t)ref[r * 8 + c]) ++mismatches;
    CHECK(mismatches == 0);
    // Pin rows: row 0 = {0, 0, 127, 1, ...} (the [-1,0) band start); row 64
    // is the first row of the [0,1) band; the dummy row 192 replicates 191.
    CHECK(l17_inter::warpedFilter[0][2] == 127);
    CHECK(l17_inter::warpedFilter[0][3] == 1);
    CHECK(l17_inter::warpedFilter[64][3] == 127); // first row of the [0,1) band
    CHECK(l17_inter::warpedFilter[192][5] == l17_inter::warpedFilter[191][5]);
    bool dummyEquals = true;
    for (int c = 0; c < 8; ++c)
        if (l17_inter::warpedFilter[192][c] != l17_inter::warpedFilter[191][c]) dummyEquals = false;
    CHECK(dummyEquals);
}
// ---------------------------------------------------------------- warp affine

// Independent per-pixel warp oracle: the reference is a two-pass separable
// filter over 8x8 tiles; this oracle re-derives the horizontal stage for
// every output row directly from the frame (a different execution structure
// over the same arithmetic). The subpel ramp phases are per wm:613-:638 /
// :641-:681: sx(l) = sx4 + beta*(k+4) + alpha*(l+4) (the reference
// accumulates from l = -4, and sx4 is pre-adjusted by -4*alpha - 4*beta).
namespace {
void oracleWarpAffine(const std::int32_t* mat, const std::uint8_t* ref, int width, int height, int stride,
                      std::uint8_t* pred, int pCol, int pRow, int pWidth, int pHeight, int pStride, int ssX, int ssY,
                      const inter::ConvolveParams* convParams, std::int16_t alpha, std::int16_t beta,
                      std::int16_t gamma, std::int16_t delta) {
    const int bd = 8;
    const int reduceBitsHoriz = convParams->round0;
    const int reduceBitsVert = convParams->isCompound ? convParams->round1 : 2 * 7 - reduceBitsHoriz;
    const int offsetBitsHoriz = bd + 7 - 1;
    const int offsetBitsVert = bd + 2 * 7 - reduceBitsHoriz;
    const int roundBits = 2 * 7 - convParams->round0 - convParams->round1;
    const int offsetBits = bd + 2 * 7 - convParams->round0;
    auto rpot = [](std::int64_t v, int n) { return (std::int64_t)((v + (((std::int64_t)1 << n) >> 1)) >> n); };
    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    auto clipPixel = [](int v) { return (std::uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); };

    for (int r = 0; r < pHeight; ++r) {
        for (int c = 0; c < pWidth; ++c) {
            const int i = pRow + 8 * (r / 8);
            const int j = pCol + 8 * (c / 8);
            const int k = r - (i - pRow) - 4;
            const int l = c - (j - pCol) - 4;
            const std::int32_t srcX = (j + 4) << ssX;
            const std::int32_t srcY = (i + 4) << ssY;
            const std::int32_t dstX = mat[2] * srcX + mat[3] * srcY + mat[0];
            const std::int32_t dstY = mat[4] * srcX + mat[5] * srcY + mat[1];
            const std::int32_t x4 = dstX >> ssX;
            const std::int32_t y4 = dstY >> ssY;
            const std::int32_t ix4 = x4 >> 16;
            std::int32_t sx4 = x4 & ((1 << 16) - 1);
            const std::int32_t iy4 = y4 >> 16;
            std::int32_t sy4 = y4 & ((1 << 16) - 1);
            sx4 += alpha * (-4) + beta * (-4);
            sy4 += gamma * (-4) + delta * (-4);
            sx4 &= ~((1 << 6) - 1);
            sy4 &= ~((1 << 6) - 1);

            const int vOffs = (int)rpot(sy4 + delta * (k + 4) + gamma * (l + 4), 10) + 64;
            const std::int16_t* vCoeffs = l17_inter::warpedFilter[vOffs];
            std::int64_t sum = (std::int64_t)1 << offsetBitsVert;
            for (int m = 0; m < 8; ++m) {
                // The vertical pass consumes tmp[(k+m+4)], i.e. the horizontal
                // row k_h = (k+m+4) - 7 = k+m-3 (the 15-row tmp buffer is
                // indexed k+7 for horizontal k in [-7, 8)).
                const int kr = k + m - 3;
                const int iy = clampi(iy4 + kr, 0, height - 1);
                const int sx = sx4 + beta * (kr + 4) + alpha * (l + 4);
                const int hOffs = (int)rpot(sx, 10) + 64;
                const std::int16_t* hCoeffs = l17_inter::warpedFilter[hOffs];
                const int ix = ix4 + l - 3;
                std::int64_t hSum = 1 << offsetBitsHoriz;
                for (int p = 0; p < 8; ++p) {
                    const int sampleX = clampi(ix + p, 0, width - 1);
                    hSum += ref[iy * stride + sampleX] * hCoeffs[p];
                }
                sum += rpot(hSum, reduceBitsHoriz) * vCoeffs[m];
            }
            if (convParams->isCompound) {
                inter::ConvBufType* p =
                    &convParams->dst[r * convParams->dstStride + c];
                const std::int32_t s32 = (std::int32_t)rpot(sum, reduceBitsVert);
                if (convParams->doAverage) {
                    std::uint8_t* dst8 = &pred[r * pStride + c];
                    std::int32_t tmp32 = *p;
                    if (convParams->useJntCompAvg) {
                        tmp32 = tmp32 * convParams->fwdOffset + s32 * convParams->bckOffset;
                        tmp32 = tmp32 >> l17_inter::kDistPrecisionBits;
                    } else {
                        tmp32 += s32;
                        tmp32 = tmp32 >> 1;
                    }
                    tmp32 = tmp32 - (1 << (offsetBits - convParams->round1)) -
                        (1 << (offsetBits - convParams->round1 - 1));
                    *dst8 = clipPixel((int)rpot(tmp32, roundBits));
                } else {
                    *p = (inter::ConvBufType)s32;
                }
            } else {
                const std::int32_t out = (std::int32_t)rpot(sum, reduceBitsVert);
                pred[r * pStride + c] = clipPixel(out - (1 << (bd - 1)) - (1 << bd));
            }
        }
    }
}

// The 32x32 test frame: a non-linear pattern (no separable structure the
// filter could exploit), values spanning the full 8-bit range.
struct WarpFrame {
    static constexpr int kWidth = 32;
    static constexpr int kHeight = 32;
    std::uint8_t buf[kWidth * kHeight];
    WarpFrame() {
        for (int r = 0; r < kHeight; ++r)
            for (int c = 0; c < kWidth; ++c)
                buf[r * kWidth + c] = (std::uint8_t)((r * 37 + c * 91 + (r * c) / 3 + 11) & 0xFF);
    }
};

inter::ConvolveParams warpConvParams(inter::ConvBufType* dst, int dstStride, int isCompound, int doAverage,
                                     int round0, int round1) {
    inter::ConvolveParams p;
    std::memset(&p, 0, sizeof(p));
    p.dst = dst;
    p.dstStride = dstStride;
    p.isCompound = isCompound;
    p.doAverage = doAverage;
    p.round0 = round0;
    p.round1 = round1;
    return p;
}

// Compare the port's output block against the oracle's, count mismatches.
int compareOutputs(const std::uint8_t* got, const std::uint8_t* want, int n) {
    int mismatches = 0;
    for (int i = 0; i < n; ++i)
        if (got[i] != want[i]) ++mismatches;
    return mismatches;
}
} // namespace

TEST_CASE("warp_affine identity matches the per-pixel oracle and approximates the frame") {
    WarpFrame frame;
    std::uint8_t got[16 * 16] = {0};
    std::uint8_t want[16 * 16] = {0};
    const std::int32_t mat[6] = {0, 0, 1 << 16, 0, 0, 1 << 16};
    inter::ConvolveParams cp = warpConvParams(nullptr, 0, 0, 0, 5, 9);
    l17_inter::warpAffineC(mat, frame.buf, WarpFrame::kWidth, WarpFrame::kHeight, WarpFrame::kWidth, got, 8, 8, 16, 16,
                           16, 0, 0, &cp, 0, 0, 0, 0);
    oracleWarpAffine(mat, frame.buf, WarpFrame::kWidth, WarpFrame::kHeight, WarpFrame::kWidth, want, 8, 8, 16, 16, 16,
                     0, 0, &cp, 0, 0, 0, 0);
    int firstDiff = -1;
    for (int i = 0; i < 256; ++i)
        if (got[i] != want[i]) { firstDiff = i; break; }
    CHECK_MESSAGE(firstDiff < 0, "first diff at " << firstDiff << " got=" << (firstDiff >= 0 ? got[firstDiff] : 0) << " want=" << (firstDiff >= 0 ? want[firstDiff] : 0));
    // Semantic: the identity warp reproduces the frame within +-2 (the two
    // 7/1-tap phases round, and the block interior stays inside the frame).
    int maxDiff = 0;
    for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 16; ++c) {
            const int d = std::abs((int)got[r * 16 + c] - (int)frame.buf[(8 + r) * 32 + (8 + c)]);
            if (d > maxDiff) maxDiff = d;
        }
        // Bound measured on this fixture: the 7/1 tap phases round up (+1) per
    // stage and the two stages compose to at most 3.
    CHECK_MESSAGE(maxDiff <= 3, "identity warp max |diff| = " << maxDiff);
}

TEST_CASE("warp_affine translation/rotzoom/shear all match the per-pixel oracle") {
    WarpFrame frame;
    // Translation by (2.5, 1.25) px: mat[0] = 2.5<<16, mat[1] = 1.25<<16.
    {
        const std::int32_t mat[6] = {163840, 81920, 1 << 16, 0, 0, 1 << 16};
        std::uint8_t got[16 * 16] = {0}, want[16 * 16] = {0};
        inter::ConvolveParams cp = warpConvParams(nullptr, 0, 0, 0, 5, 9);
        l17_inter::warpAffineC(mat, frame.buf, 32, 32, 32, got, 8, 8, 16, 16, 16, 0, 0, &cp, 0, 0, 0, 0);
        oracleWarpAffine(mat, frame.buf, 32, 32, 32, want, 8, 8, 16, 16, 16, 0, 0, &cp, 0, 0, 0, 0);
        CHECK(compareOutputs(got, want, 16 * 16) == 0);
    }
    // Rotzoom (c = 1.01, s = 0.008): the shear params are derived through
    // the cycle-5 verified getShearParams surface (the warp_plane flow).
    {
        l17_inter::WarpedMotionParams wm = l17_inter::defaultWarpParams;
        wm.wmmat[2] = 66200;  // ~1.01 << 16
        wm.wmmat[3] = -524;   // ~-0.008 << 16
        wm.wmmat[4] = 524;
        wm.wmmat[5] = 66200;
        wm.wmmat[0] = 16384;  // small absolute translation
        wm.wmmat[1] = -8192;
        REQUIRE(l17_inter::getShearParams(&wm));
        std::uint8_t got[16 * 16] = {0}, want[16 * 16] = {0};
        inter::ConvolveParams cp = warpConvParams(nullptr, 0, 0, 0, 5, 9);
        l17_inter::warpAffineC(wm.wmmat, frame.buf, 32, 32, 32, got, 8, 8, 16, 16, 16, 0, 0, &cp, wm.alpha, wm.beta,
                               wm.gamma, wm.delta);
        oracleWarpAffine(wm.wmmat, frame.buf, 32, 32, 32, want, 8, 8, 16, 16, 16, 0, 0, &cp, wm.alpha, wm.beta,
                         wm.gamma, wm.delta);
        CHECK(compareOutputs(got, want, 16 * 16) == 0);
    }
    // Shear: alpha = 0, beta = 2000, gamma = 1000, delta = 0 (inside the
    // 4|g|+4|d| / 4|a|+7|b| gate).
    {
        const std::int32_t mat[6] = {0, 0, 1 << 16, 2000, 1000, 1 << 16};
        std::uint8_t got[16 * 16] = {0}, want[16 * 16] = {0};
        inter::ConvolveParams cp = warpConvParams(nullptr, 0, 0, 0, 5, 9);
        l17_inter::warpAffineC(mat, frame.buf, 32, 32, 32, got, 8, 8, 16, 16, 16, 0, 0, &cp, 0, 2000, 1000, 0);
        oracleWarpAffine(mat, frame.buf, 32, 32, 32, want, 8, 8, 16, 16, 16, 0, 0, &cp, 0, 2000, 1000, 0);
        CHECK(compareOutputs(got, want, 16 * 16) == 0);
    }
}
TEST_CASE("warp_affine exercises the frame-edge clamps and the 8x8 crop paths") {
    WarpFrame frame;
    // Negative translation: the block at (0,0) reads left of / above the
    // frame -> the sample_x / iy clamps fire on both stages.
    {
        const std::int32_t mat[6] = {-393216, -589824, 1 << 16, 0, 0, 1 << 16}; // (-6, -9) px
        std::uint8_t got[16 * 16] = {0}, want[16 * 16] = {0};
        inter::ConvolveParams cp = warpConvParams(nullptr, 0, 0, 0, 5, 9);
        l17_inter::warpAffineC(mat, frame.buf, 32, 32, 32, got, 0, 0, 16, 16, 16, 0, 0, &cp, 0, 0, 0, 0);
        oracleWarpAffine(mat, frame.buf, 32, 32, 32, want, 0, 0, 16, 16, 16, 0, 0, &cp, 0, 0, 0, 0);
        CHECK(compareOutputs(got, want, 16 * 16) == 0);
    }
    // Large positive translation: reads past the right/bottom frame edge.
    {
        const std::int32_t mat[6] = {1638400, 1966080, 1 << 16, 0, 0, 1 << 16}; // (25, 30) px
        std::uint8_t got[16 * 16] = {0}, want[16 * 16] = {0};
        inter::ConvolveParams cp = warpConvParams(nullptr, 0, 0, 0, 5, 9);
        l17_inter::warpAffineC(mat, frame.buf, 32, 32, 32, got, 16, 16, 16, 16, 16, 0, 0, &cp, 0, 0, 0, 0);
        oracleWarpAffine(mat, frame.buf, 32, 32, 32, want, 16, 16, 16, 16, 16, 0, 0, &cp, 0, 0, 0, 0);
        CHECK(compareOutputs(got, want, 16 * 16) == 0);
    }
    // 12x12 block at (4,4): p_width/p_height not multiples of 8 -> the
    // AOMMIN(4, p_row + p_height - i - 4) crop bounds fire.
    {
        const std::int32_t mat[6] = {81920, 40960, 1 << 16, 0, 0, 1 << 16}; // (1.25, 0.625) px
        std::uint8_t got[12 * 12] = {0}, want[12 * 12] = {0};
        inter::ConvolveParams cp = warpConvParams(nullptr, 0, 0, 0, 5, 9);
        l17_inter::warpAffineC(mat, frame.buf, 32, 32, 32, got, 4, 4, 12, 12, 12, 0, 0, &cp, 0, 0, 0, 0);
        oracleWarpAffine(mat, frame.buf, 32, 32, 32, want, 4, 4, 12, 12, 12, 0, 0, &cp, 0, 0, 0, 0);
        CHECK(compareOutputs(got, want, 12 * 12) == 0);
    }
    // Subsampled (chroma) plane: ss_x = ss_y = 1, 8x8 block at (4,4).
    {
        const std::int32_t mat[6] = {327680, 163840, 1 << 16, 0, 0, 1 << 16}; // (5, 2.5) px in luma coords
        std::uint8_t got[8 * 8] = {0}, want[8 * 8] = {0};
        inter::ConvolveParams cp = warpConvParams(nullptr, 0, 0, 0, 5, 9);
        l17_inter::warpAffineC(mat, frame.buf, 32, 32, 32, got, 4, 4, 8, 8, 8, 1, 1, &cp, 0, 0, 0, 0);
        oracleWarpAffine(mat, frame.buf, 32, 32, 32, want, 4, 4, 8, 8, 8, 1, 1, &cp, 0, 0, 0, 0);
        CHECK(compareOutputs(got, want, 8 * 8) == 0);
    }
    // Compound, both the write-through (do_average = 0) and the blend
    // (do_average = 1, including the jnt weight path).
    {
        const std::int32_t mat[6] = {98304, 49152, 1 << 16, 0, 0, 1 << 16}; // (1.5, 0.75) px
        inter::ConvBufType convBuf[20 * 20];
        std::uint8_t pred[16 * 16];
        std::uint8_t got[16 * 16], want[16 * 16];
        // do_average = 0: the conv buffer receives the rounded sums.
        std::memset(convBuf, 0xAB, sizeof(convBuf));
        inter::ConvolveParams cp0 = warpConvParams(convBuf + 4 * 20 + 4, 20, 1, 0, 5, 7);
        std::memset(pred, 0, sizeof(pred));
        l17_inter::warpAffineC(mat, frame.buf, 32, 32, 32, pred, 8, 8, 16, 16, 16, 0, 0, &cp0, 0, 0, 0, 0);
        // A second oracle run on a separate buffer for the comparison.
        inter::ConvBufType convWant[20 * 20];
        std::memset(convWant, 0xAB, sizeof(convWant));
        inter::ConvolveParams cpW = warpConvParams(convWant + 4 * 20 + 4, 20, 1, 0, 5, 7);
        std::memset(want, 0, sizeof(want));
        oracleWarpAffine(mat, frame.buf, 32, 32, 32, want, 8, 8, 16, 16, 16, 0, 0, &cpW, 0, 0, 0, 0);
        int mismatches = 0;
        for (int r = 0; r < 16; ++r)
            for (int c = 0; c < 16; ++c)
                if (convBuf[(4 + r) * 20 + (4 + c)] != convWant[(4 + r) * 20 + (4 + c)]) ++mismatches;
        CHECK(mismatches == 0);
        // do_average = 1 with the jnt weights: pred blends the conv buffer.
        for (int i = 0; i < 16 * 16; ++i) pred[i] = (std::uint8_t)((i * 17 + 3) & 0xFF);
        inter::ConvolveParams cp1 = warpConvParams(convBuf + 4 * 20 + 4, 20, 1, 1, 5, 7);
        cp1.useJntCompAvg = 1;
        cp1.fwdOffset = 10;
        cp1.bckOffset = 6;
        l17_inter::warpAffineC(mat, frame.buf, 32, 32, 32, pred, 8, 8, 16, 16, 16, 0, 0, &cp1, 0, 0, 0, 0);
        for (int i = 0; i < 16 * 16; ++i) got[i] = pred[i];
        for (int i = 0; i < 16 * 16; ++i) pred[i] = (std::uint8_t)((i * 17 + 3) & 0xFF);
        std::memset(want, 0, sizeof(want));
        oracleWarpAffine(mat, frame.buf, 32, 32, 32, want, 8, 8, 16, 16, 16, 0, 0, &cp1, 0, 0, 0, 0);
        CHECK(compareOutputs(got, want, 16 * 16) == 0);
    }
}

TEST_CASE("warp_plane expands the ROTZOOM model and dispatches (wm:686-:717)") {
    WarpFrame frame;
    // ROTZOOM: warp_plane mirrors wmmat[5] = wmmat[2], wmmat[4] = -wmmat[3]
    // before dispatching; the output must equal a direct warp_affine call
    // with the expanded matrix and the wm's shear params. [4]/[5] are set to
    // the expanded values BEFORE getShearParams (the caller contract,
    // wm:516-:519); warp_plane's re-mirror is then idempotent.
    l17_inter::WarpedMotionParams wm = l17_inter::defaultWarpParams;
    wm.wmtype = l17_inter::Rotzoom;
    wm.wmmat[2] = 66200;
    wm.wmmat[3] = -524;
    wm.wmmat[4] = -524; // = -wmmat[3] (the expansion warp_plane will re-apply)
    wm.wmmat[5] = 66200;
    wm.wmmat[0] = 16384;
    wm.wmmat[1] = -8192;
    REQUIRE(l17_inter::getShearParams(&wm));
    std::uint8_t got[16 * 16], want[16 * 16];
    inter::ConvolveParams cp = warpConvParams(nullptr, 0, 0, 0, 5, 9);
    l17_inter::warpPlane(&wm, frame.buf, 32, 32, 32, got, 8, 8, 16, 16, 16, 0, 0, &cp);
    // Independent expansion on a test-side copy.
    l17_inter::WarpedMotionParams expanded = wm;
    expanded.wmmat[5] = expanded.wmmat[2];
    expanded.wmmat[4] = (std::int32_t)(-(expanded.wmmat[3]));
    oracleWarpAffine(expanded.wmmat, frame.buf, 32, 32, 32, want, 8, 8, 16, 16, 16, 0, 0, &cp, expanded.alpha,
                     expanded.beta, expanded.gamma, expanded.delta);
    CHECK(compareOutputs(got, want, 16 * 16) == 0);
}
// ---------------------------------------------------------------- select samples

// Independent transcription of svt_aom_select_samples (wm:935-:955).
namespace {
std::int32_t aomMaxOracle(std::int32_t a, std::int32_t b) { return a > b ? a : b; }

std::uint8_t oracleSelectSamples(l17_inter::Mv mv, std::int32_t* pts, std::int32_t* ptsInref, int len, int bwidth,
                                 int bheight) {
    const int thresh = oracleClamp32(aomMaxOracle(bwidth, bheight), 16, 112);
    std::uint8_t ret = 0;
    for (int i = 0; i < len; ++i) {
        const int diff = std::abs(ptsInref[2 * i] - pts[2 * i] - mv.x) +
            std::abs(ptsInref[2 * i + 1] - pts[2 * i + 1] - mv.y);
        if (diff > thresh) continue;
        if (ret != i) {
            pts[2 * ret] = pts[2 * i];
            pts[2 * ret + 1] = pts[2 * i + 1];
            ptsInref[2 * ret] = ptsInref[2 * i];
            ptsInref[2 * ret + 1] = ptsInref[2 * i + 1];
        }
        ++ret;
    }
    return ret > 1 ? ret : 1;
}
} // namespace

TEST_CASE("select_samples keeps diff == thresh and drops thresh+1 (wm:938-:946)") {
    // BLOCK_64X64: thresh = clamp(64, 16, 112) = 64.
    std::int32_t pts[4], inref[4];
    l17_inter::Mv mv{0, 0};
    pts[0] = 0; inref[0] = 64; pts[1] = 0; inref[1] = 0; // diff = 64 == thresh: kept
    pts[2] = 0; inref[2] = 65; pts[3] = 0; inref[3] = 0; // diff = 65 > thresh: dropped
    const std::uint8_t n = l17_inter::selectSamples(mv, pts, inref, 2, l17_inter::BLOCK_64X64);
    CHECK(n == 1);
    CHECK(pts[0] == 0);
    CHECK(inref[0] == 64);
}

TEST_CASE("select_samples compacts in order and floors at one sample") {
    l17_inter::Mv mv{8, -8};
    std::int32_t pts[12], inref[12];
    // Three in-threshold samples (diff 0) separated by two far outliers.
    for (int i = 0; i < 5; ++i) {
        pts[2 * i] = 10 * i;
        pts[2 * i + 1] = 20 * i;
        inref[2 * i] = 10 * i + 8;
        inref[2 * i + 1] = 20 * i - 8;
    }
    pts[10] = 500; inref[10] = 1500; // outlier x: diff 1000
    pts[11] = 500; inref[11] = 500;
    const std::uint8_t n = l17_inter::selectSamples(mv, pts, inref, 6, l17_inter::BLOCK_16X16);
    CHECK(n == 5);
    // Compaction preserved the inlier order.
    for (int i = 0; i < 5; ++i) {
        CHECK(pts[2 * i] == 10 * i);
        CHECK(pts[2 * i + 1] == 20 * i);
        CHECK(inref[2 * i] == 10 * i + 8);
        CHECK(inref[2 * i + 1] == 20 * i - 8);
    }
    // All outliers: still returns 1 (the AOMMAX(ret, 1) floor).
    std::int32_t p2[4] = {0, 0, 0, 0}, r2[4] = {1000, 1000, 1000, 1000};
    CHECK(l17_inter::selectSamples(mv, p2, r2, 2, l17_inter::BLOCK_16X16) == 1);
}

TEST_CASE("select_samples matches the oracle over a dense grid (wm:935-:955)") {
    // The threshold depends on max(bw, bh) clamped to [16, 112]: sweep block
    // sizes whose max dimension covers the clamp bands and sample the diff
    // space around each threshold.
    const l17_inter::BlockSize sizes[] = {l17_inter::BLOCK_4X4,    l17_inter::BLOCK_8X8, l17_inter::BLOCK_16X16,
                                          l17_inter::BLOCK_32X32, l17_inter::BLOCK_64X64,
                                          l17_inter::BLOCK_128X128, l17_inter::BLOCK_4X16,
                                          l17_inter::BLOCK_16X4, l17_inter::BLOCK_64X16};
    int mismatches = 0;
    for (l17_inter::BlockSize bs : sizes) {
        for (int d = 0; d <= 130; ++d) {
            std::int32_t pts[8], inref[8];
            // 4 samples at diffs d, d+1, 0, 2*d (asymmetric x/y split).
            const int splits[4][2] = {{d, 0}, {d + 1, 0}, {0, 0}, {d / 2, d - d / 2}};
            for (int i = 0; i < 4; ++i) {
                pts[2 * i] = 7 * i;
                pts[2 * i + 1] = 11 * i;
                inref[2 * i] = 7 * i + splits[i][0];
                inref[2 * i + 1] = 11 * i + splits[i][1];
            }
            l17_inter::Mv mv{3, -5};
            std::int32_t ptsW[8], inrefW[8];
            std::memcpy(ptsW, pts, sizeof(pts));
            std::memcpy(inrefW, inref, sizeof(inref));
            const std::uint8_t got = l17_inter::selectSamples(mv, pts, inref, 4, bs);
            const std::uint8_t want =
                oracleSelectSamples(mv, ptsW, inrefW, 4, l17_inter::blockSizeWide[bs], l17_inter::blockSizeHigh[bs]);
            if (got != want || std::memcmp(pts, ptsW, sizeof(pts)) != 0 ||
                std::memcmp(inref, inrefW, sizeof(inref)) != 0)
                ++mismatches;
        }
    }
    CHECK(mismatches == 0);
    // The clamp edges pinned: BLOCK_128X128 (max 128 -> thresh 112) and
    // BLOCK_4X4 (max 4 -> thresh 16). Two samples each: kept+kept -> 2,
    // kept+dropped -> 1 (the second sample's diff is over by exactly one).
    std::int32_t pts[4] = {0, 0, 1, 1}, inref[4] = {112, 0, 0, 0};
    CHECK(l17_inter::selectSamples(l17_inter::Mv{0, 0}, pts, inref, 2, l17_inter::BLOCK_128X128) == 2);
    std::int32_t pts2[4] = {0, 0, 1, 1}, inref2[4] = {113, 0, 0, 0};
    CHECK(l17_inter::selectSamples(l17_inter::Mv{0, 0}, pts2, inref2, 2, l17_inter::BLOCK_128X128) == 1);
    std::int32_t p3[4] = {0, 0, 1, 1}, r3[4] = {16, 0, 0, 0};
    CHECK(l17_inter::selectSamples(l17_inter::Mv{0, 0}, p3, r3, 2, l17_inter::BLOCK_4X4) == 2);
    std::int32_t p4[4] = {0, 0, 1, 1}, r4[4] = {17, 0, 0, 0};
    CHECK(l17_inter::selectSamples(l17_inter::Mv{0, 0}, p4, r4, 2, l17_inter::BLOCK_4X4) == 1);
}