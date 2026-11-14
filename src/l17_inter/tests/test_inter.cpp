#include <doctest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "convolve.h"
#include "inter_prediction.h"

// MC1b - the A3 inter-prediction surface. Every expectation derives from the
// vendored reference (third_party/SVT-AV1/Source/Lib/Codec/inter_prediction.c,
// "ip"; enc_inter_prediction.c, "enc") with the line quoted. The data tables
// are additionally diff-verified entry-by-entry against the REFERENCE FILE
// ITSELF, parsed at runtime from REPO_ROOT (the l15 RC1 precedent).

namespace {

// definitions.h:883-:904, the BLOCK_SIZES_ALL order (22 names).
const char* kBlockNames[inter::kBlockSizesAll] = {
    "BLOCK_4X4",   "BLOCK_4X8",   "BLOCK_8X4",   "BLOCK_8X8",   "BLOCK_8X16",  "BLOCK_16X8",
    "BLOCK_16X16", "BLOCK_16X32", "BLOCK_32X16", "BLOCK_32X32", "BLOCK_32X64", "BLOCK_64X32",
    "BLOCK_64X64", "BLOCK_64X128", "BLOCK_128X64", "BLOCK_128X128", "BLOCK_4X16", "BLOCK_16X4",
    "BLOCK_8X32",  "BLOCK_32X8",  "BLOCK_16X64", "BLOCK_64X16",
};

std::string referenceTextEnc() {
    std::ifstream f(REPO_ROOT "/third_party/SVT-AV1/Source/Lib/Codec/enc_inter_prediction.c", std::ios::binary);
    REQUIRE(f.good());
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string referenceText() {
    std::ifstream f(REPO_ROOT "/third_party/SVT-AV1/Source/Lib/Codec/inter_prediction.c", std::ios::binary);
    REQUIRE(f.good());
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// The initializer body for the FIRST occurrence of `name`: the text between
// "= {" and the terminating "};". The reference writes one row per brace
// group; comments do not carry digits.
std::string tableBody(const std::string& text, const std::string& name) {
    const std::size_t decl = text.find(name);
    REQUIRE_MESSAGE(decl != std::string::npos, name.c_str());
    const std::size_t open = text.find("= {", decl);
    REQUIRE_MESSAGE(open != std::string::npos, name.c_str());
    const std::size_t close = text.find("};", open);
    REQUIRE_MESSAGE(close != std::string::npos, name.c_str());
    return text.substr(open + 3, close - open - 3);
}

std::vector<std::int32_t> bodyInts(const std::string& body) {
    std::vector<std::int32_t> out;
    const std::regex numRe(R"(-?\d+)");
    for (auto n = std::sregex_iterator(body.begin(), body.end(), numRe); n != std::sregex_iterator(); ++n)
        out.push_back(std::atoi(n->str().c_str()));
    return out;
}

int blockIndex(const std::string& blockName) {
    for (int i = 0; i < inter::kBlockSizesAll; ++i)
        if (blockName == kBlockNames[i]) return i;
    REQUIRE_MESSAGE(false, blockName.c_str());
    return -1;
}

// A plane with a 3-pixel border around a 4x4 region: buffer[r][c] = r*10+c.
// The sr convolve callers pass the offset pointer at (3, 3).
struct BorderedPlane {
    std::uint8_t buf[100];
    BorderedPlane() {
        for (int r = 0; r < 10; ++r)
            for (int c = 0; c < 10; ++c) buf[r * 10 + c] = static_cast<std::uint8_t>(r * 10 + c);
    }
    const std::uint8_t* at(int r, int c) const { return buf + r * 10 + c; }
};

// The single-ref sr convention: round_0 = round_1 = FILTER_BITS, bits = 0.
inter::ConvolveParams srConvParams() {
    inter::ConvolveParams p;
    std::memset(&p, 0, sizeof(p));
    p.round0 = convolve::filterBits;
    p.round1 = convolve::filterBits;
    return p;
}

// The Pack2dFn injectable-callee contract is a plain function pointer (the
// reference's own pattern), so the forwarding probe records through a
// namespace-static and the test passes a CAPTURELESS lambda.
struct PackSeen {
    const std::uint8_t* in8;
    std::uint32_t in8Stride;
    const std::uint8_t* inn;
    std::uint32_t innStride;
    std::uint16_t* out;
    std::uint32_t outStride;
    std::uint32_t w;
    std::uint32_t h;
};
PackSeen g_packSeen{};

void probePack2d(const std::uint8_t* i8, std::uint32_t s1, const std::uint8_t* i10, std::uint32_t s2,
                 std::uint16_t* o, std::uint32_t s3, std::uint32_t w, std::uint32_t hh) {
    g_packSeen.in8 = i8;
    g_packSeen.in8Stride = s1;
    g_packSeen.inn = i10;
    g_packSeen.innStride = s2;
    g_packSeen.out = o;
    g_packSeen.outStride = s3;
    g_packSeen.w = w;
    g_packSeen.h = hh;
    o[0] = 777;
}

} // namespace

// ---------------------------------------------------------------- T1a

TEST_CASE("is_masked_compound_type pins the masked pair (ip:37-39)") {
    CHECK(inter::CompoundAverage == 0);
    CHECK(inter::CompoundDistwtd == 1);
    CHECK(inter::CompoundWedge == 2);
    CHECK(inter::CompoundDiffwtd == 3);
    CHECK(inter::MaskedCompoundTypes == 2);
    CHECK(!inter::isMaskedCompoundType(inter::CompoundAverage));
    CHECK(!inter::isMaskedCompoundType(inter::CompoundDistwtd));
    CHECK(inter::isMaskedCompoundType(inter::CompoundWedge));
    CHECK(inter::isMaskedCompoundType(inter::CompoundDiffwtd));
}

TEST_CASE("div_mult matches the reference entry-for-entry (ip:32-34)") {
    const std::string text = referenceText();
    const std::vector<std::int32_t> ref = bodyInts(tableBody(text, "div_mult[32]"));
    REQUIRE(ref.size() == 32);
    int mismatches = 0;
    for (int i = 0; i < 32; ++i)
        if (inter::divMult[i] != ref[i]) ++mismatches;
    CHECK(mismatches == 0);
    CHECK(inter::divMult[0] == 0);
    CHECK(inter::divMult[1] == 16384);
    CHECK(inter::divMult[31] == 528);
}

TEST_CASE("get_relative_dist_enc wraps the order-hint difference (ip:276-285)") {
    CHECK(inter::getRelativeDistEnc(false, 8, 5, 250) == 0);
    CHECK(inter::getRelativeDistEnc(true, 8, 5, 250) == 11);
    CHECK(inter::getRelativeDistEnc(true, 8, 250, 5) == -11);
    CHECK(inter::getRelativeDistEnc(true, 4, 15, 0) == -1);
    CHECK(inter::getRelativeDistEnc(true, 4, 7, 0) == 7);
    CHECK(inter::getRelativeDistEnc(true, 8, 200, 0) == -56);
}

TEST_CASE("diffwtd_mask pins the blend-64 mask arithmetic (ip:71-80)") {
    std::uint8_t src0[2] = {200, 255};
    std::uint8_t src1[2] = {100, 0};
    std::uint8_t mask[2] = {0, 0};
    // mask_base 38 (:161, :164); DIFF_FACTOR 16 (definitions.h:1252-:1253);
    // AOM_BLEND_A64_MAX_ALPHA 64 (definitions.h:1247-:1248).
    // diff 100 -> 100/16 = 6 -> m = 44; diff 255 -> 15 -> m = 53.
    inter::diffwtdMask(mask, 0, 38, src0, 1, src1, 1, 1, 2);
    CHECK(mask[0] == 44);
    CHECK(mask[1] == 53);
}

TEST_CASE("diffwtd_mask INV and diff-0 cases (ip:71-80)") {
    {
        std::uint8_t src0[2] = {200, 255};
        std::uint8_t src1[2] = {100, 0};
        std::uint8_t mask[2] = {0, 0};
        inter::diffwtdMask(mask, 1, 38, src0, 1, src1, 1, 1, 2);
        CHECK(mask[0] == 20);
        CHECK(mask[1] == 11);
    }
    {
        std::uint8_t src0[1] = {100};
        std::uint8_t src1[1] = {100};
        std::uint8_t mask[1] = {0};
        inter::diffwtdMask(mask, 0, 38, src0, 1, src1, 1, 1, 1);
        CHECK(mask[0] == 38);
    }
}

TEST_CASE("diffwtd_mask independent src strides, dense mask (ip:77)") {
    std::uint8_t src0[6] = {200, 0, 0, 255, 0, 0}; // stride 3
    std::uint8_t src1[6] = {100, 0, 0, 0, 0, 0};   // stride 3
    std::uint8_t mask[2] = {0, 0};
    inter::diffwtdMask(mask, 0, 38, src0, 3, src1, 3, 2, 1);
    CHECK(mask[0] == 44);
    CHECK(mask[1] == 53);
}

TEST_CASE("build_compound_diffwtd_mask dispatches the two types (ip:157-169)") {
    std::uint8_t src0[2] = {200, 255};
    std::uint8_t src1[2] = {100, 0};
    std::uint8_t mask38[2] = {0, 0};
    std::uint8_t maskInv[2] = {0, 0};
    inter::buildCompoundDiffwtdMask(mask38, inter::Diffwtd38, src0, 1, src1, 1, 1, 2);
    inter::buildCompoundDiffwtdMask(maskInv, inter::Diffwtd38Inv, src0, 1, src1, 1, 1, 2);
    CHECK(mask38[0] == 44);
    CHECK(mask38[1] == 53);
    CHECK(maskInv[0] == 20);
    CHECK(maskInv[1] == 11);
}

// ---------------------------------------------------------------- T1b

TEST_CASE("diffwtd_mask_highbd bd==8 matches the 8-bit path (ip:82-140 vs :71-80)") {
    // The reference's highbd build (:147) casts the buffers to uint16_t*
    // (word samples) while the 8-bit build (:161) reads the byte plane, so
    // the derivation fixture supplies the SAME SAMPLE VALUES through two
    // separate arrays - not one buffer reinterpreted.
    std::uint16_t src0[2] = {200, 255};
    std::uint16_t src1[2] = {100, 0};
    std::uint8_t hbd[2] = {0, 0};
    std::uint8_t lbd[2] = {0, 0};
    inter::diffwtdMaskHighbd(hbd, 0, 38, src0, 1, src1, 1, 1, 2, 8);
    const std::uint8_t b0[2] = {200, 255};
    const std::uint8_t b1[2] = {100, 0};
    inter::diffwtdMask(lbd, 0, 38, b0, 1, b1, 1, 1, 2);
    CHECK(hbd[0] == lbd[0]);
    CHECK(hbd[1] == lbd[1]);
    CHECK(hbd[0] == 44);
    CHECK(hbd[1] == 53);
}

TEST_CASE("diffwtd_mask_highbd bd>8 shifts the difference (ip:113, :117)") {
    // bd_shift = bd - 8 = 2: diff = (|200-100| >> 2) / 16 = 25/16 = 1.
    std::uint16_t src0[2] = {200, 255};
    std::uint16_t src1[2] = {100, 0};
    std::uint8_t mask[2] = {0, 0};
    inter::diffwtdMaskHighbd(mask, 0, 38, src0, 1, src1, 1, 1, 2, 10);
    CHECK(mask[0] == 39);
    CHECK(mask[1] == 41); // (255 >> 2)/16 = 63/16 = 3 -> 38 + 3
}

TEST_CASE("build_compound_diffwtd_mask_highbd dispatches (ip:142-155)") {
    std::uint16_t src0[2] = {200, 255};
    std::uint16_t src1[2] = {100, 0};
    std::uint8_t mask38[2] = {0, 0};
    std::uint8_t maskInv[2] = {0, 0};
    inter::buildCompoundDiffwtdMaskHighbd(mask38, inter::Diffwtd38, src0, 1, src1, 1, 1, 2, 10);
    inter::buildCompoundDiffwtdMaskHighbd(maskInv, inter::Diffwtd38Inv, src0, 1, src1, 1, 1, 2, 10);
    CHECK(mask38[0] == 39);
    CHECK(mask38[1] == 41);
    CHECK(maskInv[0] == 64 - 39);
    CHECK(maskInv[1] == 64 - 41);
}

TEST_CASE("scaled_x/scaled_y pin the q4-offset round (ip:172-183)") {
    // 1.5x reference: x_scale_fp = 24576; off = 65536; tval >> 8.
    inter::ScaleFactors sf{24576, 24576, 1536, 1536, nullptr, nullptr};
    CHECK(inter::scaledX(5, &sf) == 736);
    CHECK(inter::scaledX(4, &sf) == 640);
    CHECK(inter::scaledX(-4, &sf) == -128);
    CHECK(inter::scaledY(5, &sf) == 736);
    // Unscaled factor: off = 0, val*16384 >> 8 = val << 6.
    inter::ScaleFactors one{16384, 16384, 1024, 1024, nullptr, nullptr};
    CHECK(inter::scaledX(5, &one) == 320);
}

TEST_CASE("unscaled_value shifts by SCALE_EXTRA_BITS (ip:186-189)") {
    inter::ScaleFactors sf{0, 0, 0, 0, nullptr, nullptr};
    CHECK(inter::unscaledValue(5, &sf) == 320);
    CHECK(inter::unscaledValue(-2, &sf) == -128);
}

TEST_CASE("get_fixed_point_scale_factor pins the rounding division (ip:191-197)") {
    CHECK(inter::getFixedPointScaleFactor(64, 64) == 16384);
    CHECK(inter::getFixedPointScaleFactor(192, 128) == 24576);
    CHECK(inter::getFixedPointScaleFactor(128, 192) == 10923);
    CHECK(inter::getFixedPointScaleFactor(16, 256) == 1024);
}

TEST_CASE("fixed_point_scale_to_coarse_point_scale rounds to q4 (ip:200-202)") {
    CHECK(inter::fixedPointScaleToCoarsePointScale(16384) == 1024);
    CHECK(inter::fixedPointScaleToCoarsePointScale(24576) == 1536);
    CHECK(inter::fixedPointScaleToCoarsePointScale(10923) == 683);
}

TEST_CASE("setup_scale_factors_for_frame pins the valid/invalid gate (ip:204-224)") {
    {
        inter::ScaleFactors sf{0, 0, 0, 0, nullptr, nullptr};
        inter::setupScaleFactorsForFrame(&sf, 64, 64, 64, 64);
        CHECK(sf.xScaleFp == 16384);
        CHECK(sf.yScaleFp == 16384);
        CHECK(sf.xStepQ4 == 1024);
        CHECK(sf.yStepQ4 == 1024);
        CHECK(sf.scaleValueX == &inter::unscaledValue);
        CHECK(sf.scaleValueY == &inter::unscaledValue);
    }
    {
        inter::ScaleFactors sf{0, 0, 0, 0, nullptr, nullptr};
        inter::setupScaleFactorsForFrame(&sf, 192, 192, 128, 128);
        CHECK(sf.xScaleFp == 24576);
        CHECK(sf.xStepQ4 == 1536);
        CHECK(sf.scaleValueX == &inter::scaledX);
        CHECK(sf.scaleValueY == &inter::scaledY);
    }
}

TEST_CASE("setup_scale_factors_for_frame invalid and upscale cases (ip:204-224)") {
    {
        // 2*this >= ref fails: 128 >= 256 is false -> REF_INVALID_SCALE (-1).
        inter::ScaleFactors sf{0, 0, 0, 0, nullptr, nullptr};
        inter::setupScaleFactorsForFrame(&sf, 256, 256, 64, 64);
        CHECK(sf.xScaleFp == -1);
        CHECK(sf.yScaleFp == -1);
    }
    {
        // Extreme upscale stays valid: 16x16 ref, 256x256 this.
        inter::ScaleFactors sf{0, 0, 0, 0, nullptr, nullptr};
        inter::setupScaleFactorsForFrame(&sf, 16, 16, 256, 256);
        CHECK(sf.xScaleFp == 1024);
        CHECK(sf.xStepQ4 == 64);
        CHECK(sf.scaleValueX == &inter::scaledX);
    }
}

TEST_CASE("has_scale and revert_scale_extra_bits pin the q10->q4 shift (ip:226-239)") {
    CHECK(!inter::hasScale(1024, 1024));
    CHECK(inter::hasScale(1024, 512));
    CHECK(inter::hasScale(2048, 1024));
    inter::SubpelParams sp{1024, 1024, 640, 256};
    inter::revertScaleExtraBits(&sp);
    CHECK(sp.subpelX == 10);
    CHECK(sp.subpelY == 4);
    CHECK(sp.xs == 16);
    CHECK(sp.ys == 16);
}

TEST_CASE("dist_wtd_comp_weight_assign pins the lookup walk (ip:293-330)") {
    int fwd = -1, bck = -1, use = -1;
    // :297-:300 - not compound -> disabled.
    inter::distWtdCompWeightAssign(true, 8, 0, 0, 5, 0, 0, &fwd, &bck, &use, 0);
    CHECK(use == 0);
    // :297-:300 - compound_idx nonzero -> disabled.
    inter::distWtdCompWeightAssign(true, 8, 0, 0, 5, 1, 0, &fwd, &bck, &use, 1);
    CHECK(use == 0);
    // Early exit (:311-:315): d0 = 5, d1 = 0, order = 0 -> lookup[0][3][0].
    inter::distWtdCompWeightAssign(true, 8, 0, 0, 5, 0, 0, &fwd, &bck, &use, 1);
    CHECK(use == 1);
    CHECK(fwd == 13);
    CHECK(bck == 3);
    // Same with orderIdx = 1: lookup[1][3] = {3, 13}.
    inter::distWtdCompWeightAssign(true, 8, 0, 0, 5, 0, 1, &fwd, &bck, &use, 1);
    CHECK(fwd == 3);
    CHECK(bck == 13);
    // Loop break at i = 0 (:318-:326): d0 = 2, d1 = 2, order = 1.
    inter::distWtdCompWeightAssign(true, 8, 2, 0, 4, 0, 0, &fwd, &bck, &use, 1);
    CHECK(fwd == 7);
    CHECK(bck == 9);
    // Loop runs to i = 3: d0 = 1, d1 = 7, order = 1 -> lookup[0][3][1] = 3.
    inter::distWtdCompWeightAssign(true, 8, 7, 0, 8, 0, 0, &fwd, &bck, &use, 1);
    CHECK(fwd == 3);
    CHECK(bck == 13);
    // Mirror: d0 = 7, d1 = 1, order = 0, orderIdx = 1 -> lookup[1][3][0] = 3.
    inter::distWtdCompWeightAssign(true, 8, 7, 6, 0, 0, 1, &fwd, &bck, &use, 1);
    CHECK(fwd == 3);
    CHECK(bck == 13);
    // MAX_FRAME_DISTANCE clamp (:304-:307): |200-0| wraps to -56 -> 31.
    inter::distWtdCompWeightAssign(true, 8, 0, 0, 200, 0, 0, &fwd, &bck, &use, 1);
    CHECK(fwd == 13);
    CHECK(bck == 3);
    // Disabled order hint still assigns (the flag only zeroes the distances).
    inter::distWtdCompWeightAssign(false, 8, 0, 0, 5, 0, 0, &fwd, &bck, &use, 1);
    CHECK(use == 1);
    CHECK(fwd == 3); // order = (0 <= 0) = 1 -> lookup[0][3][1]
    CHECK(bck == 13);
}

TEST_CASE("pack_block forwards to the injected pack2d callee (ip:26-30)") {
    // The svt_aom_pack2d_src dispatch is the PO1-parked A6 surface
    // (pic_operators.c:173); the facade is verified by argument forwarding.
    std::memset(&g_packSeen, 0, sizeof(g_packSeen));
    const std::uint8_t a8[4] = {1, 2, 3, 4};
    const std::uint8_t a10[4] = {5, 6, 7, 8};
    std::uint16_t dst[4] = {0, 0, 0, 0};
    inter::packBlock(a8, 2, a10, 2, dst, 2, 2, 2, probePack2d);
    CHECK(g_packSeen.in8 == a8);
    CHECK(g_packSeen.in8Stride == 2);
    CHECK(g_packSeen.inn == a10);
    CHECK(g_packSeen.innStride == 2);
    CHECK(g_packSeen.out == dst);
    CHECK(g_packSeen.outStride == 2);
    CHECK(g_packSeen.w == 2);
    CHECK(g_packSeen.h == 2);
    CHECK(dst[0] == 777);
}

// ---------------------------------------------------------------- T2

TEST_CASE("wedge primary tables match the reference (ip:1529-1543)") {
    const std::string text = referenceText();
    const std::uint8_t* ours[3] = {inter::wedgePrimaryObliqueOdd, inter::wedgePrimaryObliqueEven,
                                   inter::wedgePrimaryVertical};
    const char* names[3] = {"wedge_primary_oblique_odd", "wedge_primary_oblique_even", "wedge_primary_vertical"};
    for (int t = 0; t < 3; ++t) {
        INFO(names[t]);
        const std::vector<std::int32_t> ref = bodyInts(tableBody(text, names[t]));
        REQUIRE(ref.size() == inter::kMaskPrimarySize);
        int mismatches = 0;
        for (int i = 0; i < inter::kMaskPrimarySize; ++i)
            if (ours[t][i] != ref[i]) ++mismatches;
        CHECK(mismatches == 0);
    }
    CHECK(inter::wedgePrimaryObliqueOdd[27] == 0);
    CHECK(inter::wedgePrimaryObliqueOdd[28] == 1);
    CHECK(inter::wedgePrimaryObliqueOdd[36] == 64);
}

TEST_CASE("wedge_signflip_lookup matches the reference 22x16 (ip:1545-1942)") {
    const std::string body = tableBody(referenceText(), "wedge_signflip_lookup");
    const std::vector<std::int32_t> ref = bodyInts(body);
    REQUIRE(ref.size() == inter::kBlockSizesAll * inter::kMaxWedgeTypes);
    int mismatches = 0;
    for (int r = 0; r < inter::kBlockSizesAll; ++r)
        for (int c = 0; c < inter::kMaxWedgeTypes; ++c)
            if (inter::wedgeSignflipLookup[r][c] != ref[r * inter::kMaxWedgeTypes + c]) ++mismatches;
    CHECK(mismatches == 0);
    CHECK(inter::wedgeSignflipLookup[3][0] == 1);
    CHECK(inter::wedgeSignflipLookup[3][10] == 0);
    CHECK(inter::wedgeSignflipLookup[0][0] == 0);
}

TEST_CASE("wedge codebooks match the reference (ip:1944-1999)") {
    const std::string text = referenceText();
    const char* names[3] = {"wedge_codebook_16_hgtw", "wedge_codebook_16_hltw", "wedge_codebook_16_heqw"};
    const inter::WedgeCodeType* ours[3] = {inter::wedgeCodebook16Hgtw, inter::wedgeCodebook16Hltw,
                                           inter::wedgeCodebook16Heqw};
    // WEDGE_DIRECTION values, inter_prediction.h:70-:78.
    const std::regex rowRe(R"(\{(WEDGE_\w+),\s*(-?\d+),\s*(-?\d+)\})");
    for (int t = 0; t < 3; ++t) {
        INFO(names[t]);
        const std::string body = tableBody(text, names[t]);
        auto it = std::sregex_iterator(body.begin(), body.end(), rowRe);
        int rows = 0;
        for (; it != std::sregex_iterator(); ++it, ++rows) {
            const std::string dir = (*it)[1].str();
            const int dirVal = dir == "WEDGE_HORIZONTAL"  ? 0
                               : dir == "WEDGE_VERTICAL"  ? 1
                               : dir == "WEDGE_OBLIQUE27" ? 2
                               : dir == "WEDGE_OBLIQUE63" ? 3
                               : dir == "WEDGE_OBLIQUE117" ? 4 : 5;
            CHECK(ours[t][rows].direction == dirVal);
            CHECK(ours[t][rows].xOffset == std::atoi((*it)[2].str().c_str()));
            CHECK(ours[t][rows].yOffset == std::atoi((*it)[3].str().c_str()));
        }
        CHECK(rows == 16);
    }
    CHECK(inter::wedgeCodebook16Hgtw[0].direction == inter::WedgeOblique27);
    CHECK(inter::wedgeCodebook16Hgtw[0].xOffset == 4);
    CHECK(inter::wedgeCodebook16Hgtw[0].yOffset == 4);
}

TEST_CASE("wedge_params_lookup matches the reference (ip:2001-2024)") {
    const std::string body = tableBody(referenceText(), "wedge_params_lookup");
    // Rows: {bits, codebook-or-NULL, signflip-row-or-NULL, masks-or-NULL}.
    // The masks member is the parked runtime buffer; the parser maps the
    // codebook symbol and the signflip BLOCK_ row index.
    const std::regex rowRe(
        R"(\{(\d+),\s*(NULL|wedge_codebook_16_\w+),\s*(NULL|wedge_signflip_lookup\[BLOCK_\w+\]),)");
    auto it = std::sregex_iterator(body.begin(), body.end(), rowRe);
    int rows = 0;
    for (; it != std::sregex_iterator(); ++it, ++rows) {
        const int bits = std::atoi((*it)[1].str().c_str());
        const std::string cb = (*it)[2].str();
        const std::string sfRow = (*it)[3].str();
        CHECK(inter::wedgeParamsLookup[rows].bits == bits);
        if (cb == "NULL") {
            CHECK(inter::wedgeParamsLookup[rows].codebook == nullptr);
            CHECK(inter::wedgeParamsLookup[rows].signflip == nullptr);
        } else {
            if (cb == "wedge_codebook_16_hgtw")
                CHECK(inter::wedgeParamsLookup[rows].codebook == inter::wedgeCodebook16Hgtw);
            if (cb == "wedge_codebook_16_hltw")
                CHECK(inter::wedgeParamsLookup[rows].codebook == inter::wedgeCodebook16Hltw);
            if (cb == "wedge_codebook_16_heqw")
                CHECK(inter::wedgeParamsLookup[rows].codebook == inter::wedgeCodebook16Heqw);
            const std::size_t b = sfRow.find('[');
            const std::string blockName = sfRow.substr(b + 1, sfRow.size() - b - 2);
            CHECK(inter::wedgeParamsLookup[rows].signflip == inter::wedgeSignflipLookup[blockIndex(blockName)]);
        }
    }
    CHECK(rows == inter::kBlockSizesAll);
    CHECK(!inter::isInterIntraWedgeUsed(0));
    CHECK(inter::isInterIntraWedgeUsed(3));
    CHECK(!inter::isInterIntraWedgeUsed(12));
    CHECK(inter::getWedgeBitsLookup(3) == 4);
    CHECK(inter::getWedgeBitsLookup(12) == 0);
    CHECK(inter::getWedgeParamsBits(19) == 4);
    CHECK(inter::getWedgeParamsBits(0) == 0);
}

// ---------------------------------------------------------------- T3

// The T3 convolve fixture: a 16x16 plane, v(r,c) = r*16+c, big enough for
// the 8-tap two-pass reach (rows/cols 1..11) that the 10x10 BorderedPlane
// above cannot serve at 4x4.
struct Plane16 {
    std::uint8_t buf[256];
    Plane16() {
        for (int r = 0; r < 16; ++r)
            for (int c = 0; c < 16; ++c) buf[r * 16 + c] = static_cast<std::uint8_t>(r * 16 + c);
    }
    const std::uint8_t* at(int r, int c) const { return buf + r * 16 + c; }
    std::uint8_t v(int r, int c) const { return buf[r * 16 + c]; }
};

// The test-side transcription oracle for the two-pass sr convolve
// (ip:332-375): independent re-derivation of the two-pass structure against
// the l12 kernel accessor. Hardwires the sr convention round0 = round1 =
// FILTER_BITS (bits = 0), which every T3 case below runs under.
void oracleConvolve2dSr(const std::uint8_t* src, int srcStride, std::uint8_t* dst, int dstStride, int w, int h,
                        const convolve::InterpFilterParams* fx, const convolve::InterpFilterParams* fy,
                        int subpelXQ4, int subpelYQ4) {
    const int fo = fy->taps / 2 - 1;
    const int imH = h + fy->taps - 1;
    std::vector<std::int16_t> im(static_cast<std::size_t>(imH * w));
    const std::int16_t* xf = convolve::getInterpFilterSubpelKernel(*fx, subpelXQ4 & convolve::subpelMask);
    const std::uint8_t* sh = src - fo * srcStride;
    for (int y = 0; y < imH; ++y)
        for (int x = 0; x < w; ++x) {
            int sum = 1 << (8 + convolve::filterBits - 1);
            for (int k = 0; k < fx->taps; ++k) sum += xf[k] * sh[y * srcStride + x - fo + k];
            im[y * w + x] = static_cast<std::int16_t>((sum + (1 << (convolve::filterBits - 1))) >>
                                                      convolve::filterBits);
        }
    const std::int16_t* yf = convolve::getInterpFilterSubpelKernel(*fy, subpelYQ4 & convolve::subpelMask);
    const std::int16_t* sv = im.data() + fo * w;
    const int offsetBits = 8 + 2 * convolve::filterBits - convolve::filterBits;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int sum = 1 << offsetBits;
            for (int k = 0; k < fy->taps; ++k) sum += yf[k] * sv[(y - fo + k) * w + x];
            const int res = ((sum + (1 << (convolve::filterBits - 1))) >> convolve::filterBits) -
                ((1 << (offsetBits - convolve::filterBits)) + (1 << (offsetBits - convolve::filterBits - 1)));
            const int out = res < 0 ? 0 : (res > 255 ? 255 : res);
            dst[y * dstStride + x] = static_cast<std::uint8_t>(out);
        }
}

TEST_CASE("convolve_2d_sr: subpel-0 identity and the half-pel two-pass oracle (ip:332-375)") {
    Plane16 p;
    inter::ConvolveParams cp = srConvParams();
    std::uint8_t dst[16];
    const auto* fx = &convolve::interpFilterParamsList[static_cast<int>(convolve::InterpFilter::EIGHTTAP_REGULAR)];
    const auto* fy = fx;

    // Subpel 0/0: both kernels are the {0,0,0,128,0,0,0,0} delta, so the
    // two passes are the identity and dst is the source window.
    inter::convolve2dSr(p.at(4, 4), 16, dst, 4, 4, 4, fx, fy, 0, 0, &cp);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) CHECK(dst[y * 4 + x] == p.v(4 + y, 4 + x));

    // Subpel 8/8 against the transcription oracle (8-tap reach, rows 1..11).
    std::uint8_t oracle[16];
    inter::convolve2dSr(p.at(4, 4), 16, dst, 4, 4, 4, fx, fy, 8, 8, &cp);
    oracleConvolve2dSr(p.at(4, 4), 16, oracle, 4, 4, 4, fx, fy, 8, 8);
    for (int i = 0; i < 16; ++i) CHECK(dst[i] == oracle[i]);
    // The half-pel result differs from the identity pin (the oracle run is a
    // real filter, not a copy).
    CHECK(dst[0] != p.v(4, 4));
}

TEST_CASE("convolve_y_sr: vertical-only, x filter not dereferenced (ip:377-403)") {
    Plane16 p;
    inter::ConvolveParams cp = srConvParams();
    std::uint8_t dst[16];
    const auto* fy = &convolve::interpFilterParamsList[static_cast<int>(convolve::InterpFilter::BILINEAR)];

    // Subpel 0: the {0,0,0,128,0,0,0,0} delta row -> identity. filterParamsX
    // is nullptr: the y path must not touch it.
    inter::convolveYSr(p.at(4, 4), 16, dst, 4, 4, 4, nullptr, fy, 0, 0, &cp);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) CHECK(dst[y * 4 + x] == p.v(4 + y, 4 + x));

    // Half-pel vertical: res = 64*a + 64*b over rows y and y+1 (k = 3, 4 of
    // the padded 8-tap row), no seed; dst = clip((res + 64) >> 7) = the ceil
    // half, (a+b+1) >> 1. The fixture max (264) does not reach the clip.
    inter::convolveYSr(p.at(4, 4), 16, dst, 4, 4, 4, nullptr, fy, 0, 8, &cp);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const int s = p.v(4 + y, 4 + x) + p.v(5 + y, 4 + x);
            CHECK(dst[y * 4 + x] == (s + 1) / 2);
        }
}

TEST_CASE("convolve_x_sr: horizontal-only, y filter not dereferenced (ip:405-432)") {
    Plane16 p;
    inter::ConvolveParams cp = srConvParams();
    std::uint8_t dst[16];
    const auto* fx = &convolve::interpFilterParamsList[static_cast<int>(convolve::InterpFilter::BILINEAR)];

    // Subpel 0: identity, filterParamsY nullptr (the x path must not touch it).
    inter::convolveXSR(p.at(4, 4), 16, dst, 4, 4, 4, fx, nullptr, 0, 0, &cp);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) CHECK(dst[y * 4 + x] == p.v(4 + y, 4 + x));

    // Half-pel horizontal: sum = 64*a + 64*b over cols x and x+1 (k = 3, 4),
    // no seed; the round0 stage gives ((a+b)+1) >> 1 (ceil) and the bits
    // stage is the identity at round1 = FILTER_BITS.
    inter::convolveXSR(p.at(4, 4), 16, dst, 4, 4, 4, fx, nullptr, 8, 0, &cp);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const int s = p.v(4 + y, 4 + x) + p.v(4 + y, 5 + x);
            CHECK(dst[y * 4 + x] == (s + 1) / 2);
        }
}

TEST_CASE("convolve_2d_copy_sr: plain copy with independent strides (ip:434-449)") {
    Plane16 p;
    inter::ConvolveParams cp = srConvParams();
    std::uint8_t dst[24];
    for (int i = 0; i < 24; ++i) dst[i] = 0xAA;
    const auto* fx = &convolve::interpFilterParamsList[static_cast<int>(convolve::InterpFilter::BILINEAR)];

    inter::convolve2dCopySr(p.at(4, 4), 16, dst, 6, 4, 4, fx, fx, 8, 8, &cp);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) CHECK(dst[y * 6 + x] == p.v(4 + y, 4 + x));
        CHECK(dst[y * 6 + 4] == 0xAA); // the stride gap is untouched
    }
    // The copy ignores the filters and subpels entirely (nullptr-safe).
    inter::convolve2dCopySr(p.at(4, 4), 16, dst, 6, 4, 4, nullptr, nullptr, 0, 0, &cp);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) CHECK(dst[y * 6 + x] == p.v(4 + y, 4 + x));
}

TEST_CASE("convolve_2d_scale: q10 stepping, 1:1 and 2x downsample anchors (ip:451-527)") {
    Plane16 p;
    inter::ConvolveParams cp = srConvParams();
    std::uint8_t dst[16];
    const auto* fx = &convolve::interpFilterParamsList[static_cast<int>(convolve::InterpFilter::BILINEAR)];

    // 1:1 (step 1<<10, subpels 0): each phase lands on kernel row 0 (the
    // delta), so the two passes are the identity on the source window.
    inter::convolve2dScale(p.at(4, 4), 16, dst, 4, 4, 4, fx, fx, 0, 1 << inter::kScaleSubpelBits, 0,
                           1 << inter::kScaleSubpelBits, &cp);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) CHECK(dst[y * 4 + x] == p.v(4 + y, 4 + x));

    // 2x downsample (step 1<<11): the phase stays on the delta kernel while
    // the integer position steps by 2, so dst[y][x] = src[2y][2x].
    inter::convolve2dScale(p.at(4, 4), 16, dst, 4, 4, 4, fx, fx, 0, 1 << (inter::kScaleSubpelBits + 1), 0,
                           1 << (inter::kScaleSubpelBits + 1), &cp);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) CHECK(dst[y * 4 + x] == p.v(4 + 2 * y, 4 + 2 * x));
}

TEST_CASE("convolve_2d_scale: the compound branches carry verbatim (ip:491-517)") {
    Plane16 p;
    inter::ConvolveParams cp = srConvParams();
    std::uint8_t dst[16];
    std::uint16_t dst16[16];
    const auto* fx = &convolve::interpFilterParamsList[static_cast<int>(convolve::InterpFilter::BILINEAR)];
    const int step = 1 << inter::kScaleSubpelBits;

    // isCompound + doAverage: dst16 holds the reference prediction at the
    // pre-correction scale; with dst16 = 384 the avg folds to p >> 1.
    for (int i = 0; i < 16; ++i) dst16[i] = 384;
    cp.isCompound = 1;
    cp.doAverage = 1;
    cp.dst = dst16;
    cp.dstStride = 4;
    inter::convolve2dScale(p.at(4, 4), 16, dst, 4, 4, 4, fx, fx, 0, step, 0, step, &cp);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) CHECK(dst[y * 4 + x] == p.v(4 + y, 4 + x) >> 1);

    // The dist-wtd fold: (dst16*fwd + res*bck) >> DIST_PRECISION_BITS; with
    // fwd = 12, bck = 4 the fold is p >> 2.
    for (int i = 0; i < 16; ++i) dst16[i] = 384;
    cp.useDistWtdCompAvg = 1;
    cp.fwdOffset = 12;
    cp.bckOffset = 4;
    inter::convolve2dScale(p.at(4, 4), 16, dst, 4, 4, 4, fx, fx, 0, step, 0, step, &cp);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) CHECK(dst[y * 4 + x] == p.v(4 + y, 4 + x) >> 2);

    // isCompound + !doAverage: the un-corrected residual lands in dst16
    // (res = 384 + p, STORED, not added - ip:505) and dst8 stays untouched.
    // dst16 is strided (dst16Stride = 4), so the pin indexes the 4x4 grid.
    for (int i = 0; i < 16; ++i) dst[i] = 0xAA;
    cp.useDistWtdCompAvg = 0;
    cp.doAverage = 0;
    inter::convolve2dScale(p.at(4, 4), 16, dst, 4, 4, 4, fx, fx, 0, step, 0, step, &cp);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) CHECK(dst16[y * 4 + x] == 384 + p.v(4 + y, 4 + x));
    for (int i = 0; i < 16; ++i) CHECK(dst[i] == 0xAA);
}

TEST_CASE("convolve_2d_for_intrabc: the routing forces the q4=8 phases (ip:1200-1241)") {
    Plane16 p;
    std::uint8_t dst[16], ref[16];
    const auto* bf = &convolve::interpFilterParamsList[static_cast<int>(convolve::InterpFilter::BILINEAR)];

    // Both subpels: 2d_sr is invoked with the LITERAL 8, 8 phases.
    inter::ConvolveParams cp = srConvParams();
    inter::convolve2dForIntrabc(p.at(4, 4), 16, dst, 4, 4, 4, 4, 4, &cp);
    inter::ConvolveParams cp2 = srConvParams();
    inter::convolve2dSr(p.at(4, 4), 16, ref, 4, 4, 4, bf, bf, 8, 8, &cp2);
    for (int i = 0; i < 16; ++i) CHECK(dst[i] == ref[i]);

    // x only: x_sr at phase 8 with a NULL y filter.
    cp = srConvParams();
    inter::convolve2dForIntrabc(p.at(4, 4), 16, dst, 4, 4, 4, 4, 0, &cp);
    cp2 = srConvParams();
    inter::convolveXSR(p.at(4, 4), 16, ref, 4, 4, 4, bf, nullptr, 8, 0, &cp2);
    for (int i = 0; i < 16; ++i) CHECK(dst[i] == ref[i]);

    // y only: y_sr at phase 8 with a NULL x filter.
    cp = srConvParams();
    inter::convolve2dForIntrabc(p.at(4, 4), 16, dst, 4, 4, 4, 0, 4, &cp);
    cp2 = srConvParams();
    inter::convolveYSr(p.at(4, 4), 16, ref, 4, 4, 4, nullptr, bf, 0, 8, &cp2);
    for (int i = 0; i < 16; ++i) CHECK(dst[i] == ref[i]);
}

TEST_CASE("inter_predictor: the dispatch matrix routes each subpel shape (ip:1397-1453)") {
    Plane16 p;
    std::uint8_t dst[16], ref[16];
    const auto regs =
        convolve::makeInterpFilters(convolve::InterpFilter::EIGHTTAP_REGULAR, convolve::InterpFilter::EIGHTTAP_REGULAR);
    const auto bil =
        convolve::makeInterpFilters(convolve::InterpFilter::BILINEAR, convolve::InterpFilter::BILINEAR);
    const auto* bf = &convolve::interpFilterParamsList[static_cast<int>(convolve::InterpFilter::BILINEAR)];
    inter::ScaleFactors sf;
    inter::setupScaleFactorsForFrame(&sf, 32, 32, 32, 32); // unscaled

    // (a) no-scale, both subpels 0 -> the copy path.
    inter::SubpelParams sp{1024, 1024, 0, 0};
    inter::ConvolveParams cp = srConvParams();
    inter::interPredictor(p.at(4, 4), 16, dst, 4, &sp, &sf, 4, 4, &cp, regs, 0);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) CHECK(dst[y * 4 + x] == p.v(4 + y, 4 + x));

    // (b) no-scale, subpel_x only: the q10 512 reverts to the q4 8 and the
    // x_sr path runs; compare against the direct call.
    sp = {1024, 1024, 512, 0};
    cp = srConvParams();
    inter::interPredictor(p.at(4, 4), 16, dst, 4, &sp, &sf, 4, 4, &cp, bil, 0);
    inter::ConvolveParams cp2 = srConvParams();
    inter::convolveXSR(p.at(4, 4), 16, ref, 4, 4, 4, bf, nullptr, 8, 0, &cp2);
    for (int i = 0; i < 16; ++i) CHECK(dst[i] == ref[i]);

    // (c) scaled 2x: the 2d_scale path consumes xs/ys as the q10 steps.
    inter::ScaleFactors sf2;
    inter::setupScaleFactorsForFrame(&sf2, 64, 64, 32, 32);
    sp = {2048, 2048, 0, 0};
    cp = srConvParams();
    inter::interPredictor(p.at(4, 4), 16, dst, 4, &sp, &sf2, 4, 4, &cp, bil, 0);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) CHECK(dst[y * 4 + x] == p.v(4 + 2 * y, 4 + 2 * x));

    // (d) intrabc with both subpels nonzero: the intrabc 2d path at (8, 8).
    sp = {1024, 1024, 512, 512};
    cp = srConvParams();
    inter::interPredictor(p.at(4, 4), 16, dst, 4, &sp, &sf, 4, 4, &cp, bil, 1);
    cp2 = srConvParams();
    inter::convolve2dSr(p.at(4, 4), 16, ref, 4, 4, 4, bf, bf, 8, 8, &cp2);
    for (int i = 0; i < 16; ++i) CHECK(dst[i] == ref[i]);
}

// ---------------------------------------------------------------- T4

TEST_CASE("wedge_compute_delta_squares clamps at the int16 bounds (enc:375-381)") {
    std::int16_t a[3] = {10, 0, 200};
    std::int16_t b[3] = {6, 200, 0};
    std::int16_t d[3] = {0, 0, 0};
    inter::wedgeComputeDeltaSquares(d, a, b, 3);
    CHECK(d[0] == 64);     // 100 - 36, in range
    CHECK(d[1] == -32768); // -40000 clamps to INT16_MIN
    CHECK(d[2] == 32767);  // 40000 clamps to INT16_MAX
}

TEST_CASE("wedge_sign_from_residuals: the do-while sum and the strict limit (enc:414-422)") {
    {
        const std::int16_t ds[4] = {10, -3, 5, 1};
        const std::uint8_t m[4] = {2, 4, 1, 3};
        // acc = 20 - 12 + 5 + 3 = 16.
        CHECK(inter::wedgeSignFromResiduals(ds, m, 4, 15) == 1);
        CHECK(inter::wedgeSignFromResiduals(ds, m, 4, 16) == 0); // strict >
    }
    {
        const std::int16_t ds[3] = {-10, -10, -10};
        const std::uint8_t m[3] = {1, 1, 1};
        CHECK(inter::wedgeSignFromResiduals(ds, m, 3, -31) == 1);
        CHECK(inter::wedgeSignFromResiduals(ds, m, 3, -30) == 0);
    }
    // The do-while runs once even for n = 1.
    const std::int16_t one[1] = {7};
    const std::uint8_t om[1] = {5};
    CHECK(inter::wedgeSignFromResiduals(one, om, 1, 34) == 1);
    CHECK(inter::wedgeSignFromResiduals(one, om, 1, 35) == 0);
}

TEST_CASE("model_rd_norm: the get_msb / xq chain pins the table walks (enc:1883-1937)") {
    // xsq = 0: tmp = 8, k = 0, xq = 0, a = 0 -> the pure row-0 entries.
    std::int32_t r = 0, d = 0;
    inter::modelRdNorm(0, &r, &d);
    CHECK(r == 65536); // rate_tab_q10[0]
    CHECK(d == 0);     // dist_tab_q10[0]

    // xsq = 40 = xsq_iq_q10[9]: tmp = 18, k = 1, xq = 9, a = 0 -> row 9.
    inter::modelRdNorm(40, &r, &d);
    CHECK(r == 4389); // rate_tab_q10[9]
    CHECK(d == 3);    // dist_tab_q10[9]

    // xsq = 44: xq stays 9, a = ((44-40) << 10) >> 3 = 512, b = 512 ->
    // the two-row interpolation averages the rows.
    inter::modelRdNorm(44, &r, &d);
    CHECK(r == (4389 * 512 + 4255 * 512) >> 10); // 4322
    CHECK(d == (3 * 512 + 4 * 512) >> 10);       // 3

    // NAMED REFERENCE QUIRK, untested: at the top xq (xsq = 245727) the
    // reference reads rate/dist_tab_q10[xq + 1] with xq = 103 - one past
    // the 104-entry tables. The port carries it verbatim; no assertion
    // pins a read the reference itself leaves out of bounds.
}

TEST_CASE("model_rd_from_var_lapndz and model_rd_from_sse (enc:1939-1981)") {
    std::uint32_t rate = 999;
    std::uint64_t dist = 999;

    // var = 0 zeroes both.
    inter::modelRdFromVarLapndz(0, 0, 4, reinterpret_cast<std::int32_t*>(&rate),
                                reinterpret_cast<std::int64_t*>(&dist));
    CHECK(rate == 0);
    CHECK(dist == 0);

    // var = 1, n_log2 = 0, qstep = 4: xsq_q10 = 16384 -> xq = 72, r_q10 =
    // 394, d_q10 = 680 -> rate = round(394, 1) = 197, dist = (680+512)>>10 = 1.
    inter::modelRdFromVarLapndz(1, 0, 4, reinterpret_cast<std::int32_t*>(&rate),
                                reinterpret_cast<std::int64_t*>(&dist));
    CHECK(rate == 197);
    CHECK(dist == 1);

    // var = 1024, qstep = 8: xsq_q10 = 64 -> xq = 12, a = 0 -> r = 4044,
    // d = 5 -> rate = 2022, dist = (1024*5 + 512)>>10 = 5.
    inter::modelRdFromVarLapndz(1024, 0, 8, reinterpret_cast<std::int32_t*>(&rate),
                                reinterpret_cast<std::int64_t*>(&dist));
    CHECK(rate == 2022);
    CHECK(dist == 5);

    // model_rd_from_sse, fast path: q = quantizer >> (bit_depth - 5);
    // sse = 1000, quantizer = 100, bd = 8 -> q = 12 < 120:
    // rate = (1000 * 268) >> 7 = 2093, dist = (1000 * 12) >> 8 << 4 = 736.
    inter::modelRdFromSse(6, 100, 8, 1000, &rate, &dist, 1);
    CHECK(rate == 2093);
    CHECK(dist == 736);

    // Fast path, q >= 120: rate zeroes, dist carries the squared-term fold.
    // quantizer = 1000 -> q = 125; sse = 8192: dist = (8192*125)>>8 << 4 = 64000.
    inter::modelRdFromSse(6, 1000, 8, 8192, &rate, &dist, 1);
    CHECK(rate == 0);
    CHECK(dist == 64000);

    // The non-fast path delegates to the lapndz model with the numPelsLog2
    // parameter (the reference's eb_num_pels_log2_lookup[bsize] read, :1977)
    // and applies the final << 4: sse = 1, numPelsLog2 = 0, quantizer = 32
    // -> q = 4, the var=1 anchor -> rate 197, dist 1 << 4 = 16.
    inter::modelRdFromSse(0, 32, 8, 1, &rate, &dist, 0);
    CHECK(rate == 197);
    CHECK(dist == 16);
}

TEST_CASE("sse8 and sse_highbd: the summed squared differences (enc:600-627)") {
    {
        const std::uint8_t a[8] = {10, 20, 30, 40, 11, 21, 31, 41}; // stride 4, two rows
        const std::uint8_t b[8] = {9, 18, 33, 36, 11, 19, 28, 40};
        // Row diffs {1,2,3,4}, row2 diffs {0,2,3,1}: sse = 1+4+9+16+0+4+9+1 = 44.
        CHECK(inter::sse8(a, 4, b, 4, 4, 2) == 44);
        CHECK(inter::sse8(a, 4, b, 4, 4, 0) == 0);
        // Identical buffers -> 0.
        CHECK(inter::sse8(a, 4, a, 4, 4, 2) == 0);
        // Independent strides: 2x2 from the row tails (offsets 2 and 6).
        CHECK(inter::sse8(a + 2, 4, b + 2, 4, 2, 2) ==
              (30 - 33) * (30 - 33) + (40 - 36) * (40 - 36) + (31 - 28) * (31 - 28) + (41 - 40) * (41 - 40));
    }
    {
        // The hbd twin aliases the uint16 planes through the uint8 params.
        const std::uint16_t a[4] = {300, 1000, 0, 4999};
        const std::uint16_t b[4] = {45, 744, 1023, 4999};
        // Diffs {255, 256, -1023, 0}: 65025 + 65536 + 1046529 + 0 = 1177090.
        CHECK(inter::sseHighbd(reinterpret_cast<const std::uint8_t*>(a), 2, reinterpret_cast<const std::uint8_t*>(b),
                               2, 2, 2) == 1177090);
    }
}

// ------------------------------------------------------------- A3E J1
// The jnt (compound) convolve family, inter_prediction.c:529-:714. The
// oracle below is an INDEPENDENT second transcription of the reference
// arithmetic: a horizontal pass into a plain 2-D array indexed [row][col]
// (the port runs the reference's flat im_block/stride layout), then the
// per-pixel vertical pass with the jnt compose. Configs sweep odd sizes,
// extreme subpels and both compose modes.

namespace jntoracle {

constexpr int kFb = convolve::filterBits; // 7

struct Cfg {
    int w, h;
    int subpelX, subpelY;
    int round0, round1;
    int fwd, bck;
    int useJnt;
    int doAverage;
};

// LCG fixture so every configuration sees different adversarial data.
std::uint32_t lcg(std::uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return (s >> 16) & 0xFF;
}

// Fills a (w+16)x(h+16) plane, src at (8,8); values deterministic.
void fillPlane(std::vector<std::uint8_t>& plane, int stride, int rows, std::uint32_t seedBase) {
    std::uint32_t seed = seedBase;
    plane.resize((size_t)stride * rows);
    for (size_t i = 0; i < plane.size(); ++i) plane[i] = (std::uint8_t)lcg(seed);
}

// The horizontal-then-vertical compose for jnt_convolve_2d (ip:529-:585).
void conv2d(int srcStride, const std::uint8_t* src, const Cfg& c,
            const convolve::InterpFilterParams* fx, const convolve::InterpFilterParams* fy,
            const std::vector<std::uint16_t>& dst16Init, int dst16Stride, std::vector<std::uint8_t>& out8,
            std::vector<std::uint16_t>* out16) {
    const int tapsX = fx->taps, tapsY = fy->taps;
    const int foH = tapsX / 2 - 1, foV = tapsY / 2 - 1;
    const int bd = 8;
    const int roundBits = 2 * kFb - c.round0 - c.round1;
    // Horizontal: rows h + tapsY - 1 (the reference runs the vertical
    // support through the same window), cols w. Row r of the temp holds
    // src row (r - foV); the vertical pass then reads rows y + k only.
    const int imH = c.h + tapsY - 1;
    std::vector<std::int32_t> im((size_t)imH * c.w);
    const std::int16_t* xf = convolve::getInterpFilterSubpelKernel(*fx, c.subpelX & convolve::subpelMask);
    for (int y = 0; y < imH; ++y) {
        for (int x = 0; x < c.w; ++x) {
            std::int32_t sum = 1 << (bd + kFb - 1);
            for (int k = 0; k < tapsX; ++k) sum += xf[k] * src[(y - foV) * srcStride + (x - foH + k)];
            im[(size_t)y * c.w + x] = (sum + ((1 << c.round0) >> 1)) >> c.round0;
        }
    }
    const int offsetBits = bd + 2 * kFb - c.round0;
    const int offC = (1 << (offsetBits - c.round1)) + (1 << (offsetBits - c.round1 - 1));
    const std::int16_t* yf = convolve::getInterpFilterSubpelKernel(*fy, c.subpelY & convolve::subpelMask);
    out8.assign((size_t)c.w * c.h, 0);
    if (out16) out16->assign((size_t)c.w * c.h, 0);
    for (int y = 0; y < c.h; ++y) {
        for (int x = 0; x < c.w; ++x) {
            std::int32_t sum = 1 << offsetBits;
            for (int k = 0; k < tapsY; ++k) sum += yf[k] * im[(size_t)(y + k) * c.w + x];
            const std::int32_t res = (sum + ((1 << c.round1) >> 1)) >> c.round1;
            if (c.doAverage) {
                std::int32_t t = dst16Init[(size_t)y * dst16Stride + x];
                if (c.useJnt) {
                    t = t * c.fwd + res * c.bck;
                    t >>= 4; // DIST_PRECISION_BITS, definitions.h:430
                } else {
                    t += res;
                    t >>= 1;
                }
                t -= offC;
                std::int32_t v = (t + ((1 << roundBits) >> 1)) >> roundBits;
                v = v < 0 ? 0 : (v > 255 ? 255 : v);
                out8[(size_t)y * c.w + x] = (std::uint8_t)v;
            } else {
                (*out16)[(size_t)y * c.w + x] = (std::uint16_t)res;
            }
        }
    }
}

// The vertical-only compose for jnt_convolve_y (ip:587-:630). The order
// quirk this pins: res is SCALED by (1 << bits) BEFORE the round_1, where
// bits = FILTER_BITS - round_0, then round_offset is ADDED back in.
void convY(int srcStride, const std::uint8_t* src, const Cfg& c, const convolve::InterpFilterParams* fy,
           const std::vector<std::uint16_t>& dst16Init, int dst16Stride, std::vector<std::uint8_t>& out8,
           std::vector<std::uint16_t>* out16) {
    const int tapsY = fy->taps;
    const int foV = tapsY / 2 - 1;
    const int bd = 8;
    const int bits = kFb - c.round0;
    const int offsetBits = bd + 2 * kFb - c.round0;
    const int roundOffset = (1 << (offsetBits - c.round1)) + (1 << (offsetBits - c.round1 - 1));
    const int roundBits = 2 * kFb - c.round0 - c.round1;
    const std::int16_t* yf = convolve::getInterpFilterSubpelKernel(*fy, c.subpelY & convolve::subpelMask);
    out8.assign((size_t)c.w * c.h, 0);
    if (out16) out16->assign((size_t)c.w * c.h, 0);
    for (int y = 0; y < c.h; ++y) {
        for (int x = 0; x < c.w; ++x) {
            std::int32_t res = 0;
            for (int k = 0; k < tapsY; ++k) res += yf[k] * src[(y - foV + k) * srcStride + x];
            res *= (1 << bits);
            res = ((res + ((1 << c.round1) >> 1)) >> c.round1) + roundOffset;
            if (c.doAverage) {
                std::int32_t t = dst16Init[(size_t)y * dst16Stride + x];
                if (c.useJnt) {
                    t = t * c.fwd + res * c.bck;
                    t >>= 4;
                } else {
                    t += res;
                    t >>= 1;
                }
                t -= roundOffset;
                std::int32_t v = (t + ((1 << roundBits) >> 1)) >> roundBits;
                v = v < 0 ? 0 : (v > 255 ? 255 : v);
                out8[(size_t)y * c.w + x] = (std::uint8_t)v;
            } else {
                (*out16)[(size_t)y * c.w + x] = (std::uint16_t)res;
            }
        }
    }
}

} // namespace jntoracle

// The horizontal-only compose for jnt_convolve_x (ip:632-:675). The order
// quirk this pins: res = (1 << bits) * ROUND(res, round_0) with
// bits = FILTER_BITS - round_1 - the ROUND comes FIRST here, opposite of
// the y twin, then round_offset is added.
void convX(int srcStride, const std::uint8_t* src, const jntoracle::Cfg& c, const convolve::InterpFilterParams* fx,
           const std::vector<std::uint16_t>& dst16Init, int dst16Stride, std::vector<std::uint8_t>& out8,
           std::vector<std::uint16_t>* out16) {
    using namespace jntoracle;
    const int tapsX = fx->taps;
    const int foH = tapsX / 2 - 1;
    const int bd = 8;
    const int bits = kFb - c.round1;
    const int offsetBits = bd + 2 * kFb - c.round0;
    const int roundOffset = (1 << (offsetBits - c.round1)) + (1 << (offsetBits - c.round1 - 1));
    const int roundBits = 2 * kFb - c.round0 - c.round1;
    const std::int16_t* xf = convolve::getInterpFilterSubpelKernel(*fx, c.subpelX & convolve::subpelMask);
    out8.assign((size_t)c.w * c.h, 0);
    if (out16) out16->assign((size_t)c.w * c.h, 0);
    for (int y = 0; y < c.h; ++y) {
        for (int x = 0; x < c.w; ++x) {
            std::int32_t res = 0;
            for (int k = 0; k < tapsX; ++k) res += xf[k] * src[y * srcStride + x - foH + k];
            res = (1 << bits) * ((res + ((1 << c.round0) >> 1)) >> c.round0);
            res += roundOffset;
            if (c.doAverage) {
                std::int32_t t = dst16Init[(size_t)y * dst16Stride + x];
                if (c.useJnt) {
                    t = t * c.fwd + res * c.bck;
                    t >>= 4;
                } else {
                    t += res;
                    t >>= 1;
                }
                t -= roundOffset;
                std::int32_t v = (t + ((1 << roundBits) >> 1)) >> roundBits;
                v = v < 0 ? 0 : (v > 255 ? 255 : v);
                out8[(size_t)y * c.w + x] = (std::uint8_t)v;
            } else {
                (*out16)[(size_t)y * c.w + x] = (std::uint16_t)res;
            }
        }
    }
}

void runJntFamily(void (*kern)(const std::uint8_t*, std::int32_t, std::uint8_t*, std::int32_t, std::int32_t,
                               std::int32_t, const convolve::InterpFilterParams*,
                               const convolve::InterpFilterParams*, std::int32_t, std::int32_t, inter::ConvolveParams*),
                  void (*oracle)(int, const std::uint8_t*, const jntoracle::Cfg&, const convolve::InterpFilterParams*,
                                 const std::vector<std::uint16_t>&, int, std::vector<std::uint8_t>&,
                                 std::vector<std::uint16_t>*),
                  const char* label) {
    using namespace jntoracle;
    const convolve::InterpFilterParams* reg = &convolve::interpFilterParamsList[(int)convolve::InterpFilter::EIGHTTAP_REGULAR];
    const Cfg cfgs[] = {
        {8, 8, 4, 4, 7, 7, 9, 7, 1, 1},  {8, 8, 4, 4, 7, 7, 9, 7, 1, 0},  {13, 5, 1, 15, 7, 7, 12, 4, 1, 1},
        {4, 8, 15, 1, 7, 7, 5, 11, 1, 0}, {1, 3, 8, 8, 7, 7, 9, 7, 1, 1}, {5, 4, 2, 13, 4, 7, 13, 3, 1, 1},
        {8, 8, 11, 6, 4, 7, 7, 9, 0, 1},
    };
    for (const Cfg& c : cfgs) {
        std::vector<std::uint8_t> plane;
        const int stride = c.w + 16;
        fillPlane(plane, stride, c.h + 16, 0xBEEFu + (std::uint32_t)c.w * 7 + c.h * 13);
        const std::uint8_t* src = plane.data() + 8 * stride + 8;
        std::vector<std::uint16_t> dst16Init((size_t)c.w * c.h);
        std::uint32_t dstSeed = 0xCAFEu + (std::uint32_t)c.h;
        for (size_t i = 0; i < dst16Init.size(); ++i) dst16Init[i] = (std::uint16_t)(lcg(dstSeed) * 131u);
        inter::ConvolveParams cp{};
        std::memset(&cp, 0, sizeof(cp));
        cp.round0 = c.round0;
        cp.round1 = c.round1;
        cp.useJntCompAvg = c.useJnt;
        cp.fwdOffset = c.fwd;
        cp.bckOffset = c.bck;
        cp.doAverage = c.doAverage;
        cp.dst = dst16Init.data();
        cp.dstStride = c.w;
        std::vector<std::uint8_t> dst8Buf((size_t)c.w * c.h + 8, 0xAB);
        kern(src, stride, dst8Buf.data(), c.w, c.w, c.h, reg, reg, c.subpelX, c.subpelY, &cp);
        std::vector<std::uint8_t> want8;
        std::vector<std::uint16_t> want16;
        oracle(stride, src, c, reg, dst16Init, c.w, want8, c.doAverage ? nullptr : &want16);
        int bad = 0;
        for (int y = 0; y < c.h; ++y)
            for (int x = 0; x < c.w; ++x) {
                const int g = c.doAverage ? dst8Buf[(size_t)y * c.w + x] : dst16Init[(size_t)y * c.w + x];
                const int v = c.doAverage ? want8[(size_t)y * c.w + x] : want16[(size_t)y * c.w + x];
                if (g != v) {
                    ++bad;
                    if (bad <= 3)
                        MESSAGE(std::string(label) << " cfg (w,h,sx,sy,r0,r1,jnt,avg)=(" << c.w << "," << c.h << ","
                                                   << c.subpelX << "," << c.subpelY << "," << c.round0 << ","
                                                   << c.round1 << "," << c.useJnt << "," << c.doAverage << ") at ("
                                                   << y << "," << x << "): got " << g << " want " << v);
                }
            }
        CHECK_MESSAGE(bad == 0, label);
    }
}

TEST_CASE("jnt_convolve_y pins the scale-before-round order (ip:587-630)") {
    runJntFamily(&inter::jntConvolveY, &jntoracle::convY, "jnt_y");
}

TEST_CASE("jnt_convolve_x pins the round-before-scale order (ip:632-675)") {
    runJntFamily(&inter::jntConvolveX, &convX, "jnt_x");
}

// The copy compose for jnt_convolve_2d_copy (ip:677-:714): res = src << bits
// with bits = 2*FILTER_BITS - round_1 - round_0, then + round_offset (as
// ConvBufType), compose, subtract, clip(round(tmp, bits)).
void convCopy(int srcStride, const std::uint8_t* src, const jntoracle::Cfg& c,
              const std::vector<std::uint16_t>& dst16Init, int dst16Stride, std::vector<std::uint8_t>& out8,
              std::vector<std::uint16_t>* out16) {
    using namespace jntoracle;
    const int bd = 8;
    const int bits = 2 * kFb - c.round1 - c.round0;
    const int offsetBits = bd + 2 * kFb - c.round0;
    const int roundOffset = (1 << (offsetBits - c.round1)) + (1 << (offsetBits - c.round1 - 1));
    out8.assign((size_t)c.w * c.h, 0);
    if (out16) out16->assign((size_t)c.w * c.h, 0);
    for (int y = 0; y < c.h; ++y) {
        for (int x = 0; x < c.w; ++x) {
            const int res = (int)(std::uint16_t)(src[y * srcStride + x] << bits) + roundOffset;
            if (c.doAverage) {
                std::int32_t t = dst16Init[(size_t)y * dst16Stride + x];
                if (c.useJnt) {
                    t = t * c.fwd + res * c.bck;
                    t >>= 4;
                } else {
                    t += res;
                    t >>= 1;
                }
                t -= roundOffset;
                std::int32_t v = (t + ((1 << bits) >> 1)) >> bits;
                v = v < 0 ? 0 : (v > 255 ? 255 : v);
                out8[(size_t)y * c.w + x] = (std::uint8_t)v;
            } else {
                (*out16)[(size_t)y * c.w + x] = (std::uint16_t)res;
            }
        }
    }
}

// ------------------------------------------------------------- A3E J2
// The highbd sr convolve family (ip:716-:906) + highbd intrabc
// (:1243-:1257). Primary oracle: the DERIVATION relationship - at bd=8
// with uint16 planes carrying 8-bit values, every highbd kernel must
// equal its landed 8-bit twin on the same data (the HB1 precedent).
// Secondary: an independent scalar transcription at bd=10 with the
// bd-shifted bias and the clip at (1 << bd) - 1.

namespace hbdoracle {

// LCG uint16 plane fill (values 0..(1<<bd)-1 by default).
std::uint32_t lcg16(std::uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return (s >> 16) & 0xFFFF;
}

void fillPlane16(std::vector<std::uint16_t>& plane, int stride, int rows, int bd, std::uint32_t seedBase) {
    std::uint32_t seed = seedBase;
    const std::uint32_t vmax = (1u << bd) - 1;
    plane.resize((size_t)stride * rows);
    for (size_t i = 0; i < plane.size(); ++i) plane[i] = (std::uint16_t)(lcg16(seed) & vmax);
}

// The 8-bit twin's view of the same data (the derivation fixture's
// reference side): copy the uint16 plane down to uint8 (all values < 256
// at bd=8) and call the landed kernel.
std::vector<std::uint8_t> downPlane(const std::vector<std::uint16_t>& p16, int stride, int rows) {
    std::vector<std::uint8_t> p8((size_t)stride * rows);
    for (size_t i = 0; i < p8.size(); ++i) p8[i] = (std::uint8_t)p16[i];
    return p8;
}

} // namespace hbdoracle

TEST_CASE("highbd_convolve_2d_copy_sr at bd=8 equals the 8-bit twin (ip:716-732 vs :434-449)") {
    using namespace hbdoracle;
    const struct {
        int w, h;
    } dims[] = {{8, 8}, {13, 5}, {4, 8}, {1, 3}, {16, 16}};
    for (const auto& d : dims) {
        std::vector<std::uint16_t> plane16;
        const int stride = d.w + 16;
        fillPlane16(plane16, stride, d.h + 16, 8, 0xA11CEu + (std::uint32_t)d.w * 5);
        const std::uint16_t* src = plane16.data() + 8 * stride + 8;
        inter::ConvolveParams cp{};
        std::memset(&cp, 0, sizeof(cp));
        cp.round0 = convolve::filterBits;
        cp.round1 = convolve::filterBits;
        std::vector<std::uint16_t> dst16((size_t)d.w * d.h, 0x1234);
        inter::highbdConvolve2dCopySr(src, stride, dst16.data(), d.w, d.w, d.h, nullptr, nullptr, 0, 0, &cp, 8);
        std::vector<std::uint8_t> plane8 = downPlane(plane16, stride, d.h + 16);
        std::vector<std::uint8_t> dst8((size_t)d.w * d.h, 0x56);
        inter::convolve2dCopySr(plane8.data() + 8 * stride + 8, stride, dst8.data(), d.w, d.w, d.h, nullptr, nullptr,
                                0, 0, &cp);
        int bad = 0;
        for (int y = 0; y < d.h; ++y)
            for (int x = 0; x < d.w; ++x)
                if (dst16[(size_t)y * d.w + x] != dst8[(size_t)y * d.w + x]) ++bad;
        CHECK_MESSAGE(bad == 0, "copy derivation mismatch");
        if (bad) {
            for (int y = 0; y < d.h; ++y)
                for (int x = 0; x < d.w; ++x) {
                    if (dst16[(size_t)y * d.w + x] != dst8[(size_t)y * d.w + x])
                        MESSAGE("copy deriv " << d.w << "x" << d.h << " at (" << y << "," << x << "): hbd "
                                              << dst16[(size_t)y * d.w + x] << " lb " << (int)dst8[(size_t)y * d.w + x]);
                }
        }
    }
}

// The independent scalar x-oracle for the highbd sr kernels: horizontal
// pass only, res = round(sum, r0), then round(res, FB - r0), clip at bd.
// At bd=8 with 8-bit-valued input this must also equal the 8-bit twin.
void convXHbd(int stride, const std::uint16_t* src, const jntoracle::Cfg& c,
              const convolve::InterpFilterParams* fx, std::vector<std::uint16_t>& out, int bd) {
    using namespace jntoracle;
    const int taps = fx->taps;
    const int foH = taps / 2 - 1;
    const int bits = kFb - c.round0;
    const std::int16_t* xf = convolve::getInterpFilterSubpelKernel(*fx, c.subpelX & convolve::subpelMask);
    out.assign((size_t)c.w * c.h, 0);
    for (int y = 0; y < c.h; ++y) {
        for (int x = 0; x < c.w; ++x) {
            std::int32_t res = 0;
            for (int k = 0; k < taps; ++k) res += xf[k] * src[y * stride + x - foH + k];
            res = (res + ((1 << c.round0) >> 1)) >> c.round0;
            std::int32_t v = (res + ((1 << bits) >> 1)) >> bits;
            v = v < 0 ? 0 : (v > ((1 << bd) - 1) ? ((1 << bd) - 1) : v);
            out[(size_t)y * c.w + x] = (std::uint16_t)v;
        }
    }
}

TEST_CASE("highbd_convolve_x_sr: derivation at bd=8, scalar oracle at bd=10 (ip:734-759)") {
    using namespace hbdoracle;
    const convolve::InterpFilterParams* reg = &convolve::interpFilterParamsList[(int)convolve::InterpFilter::EIGHTTAP_REGULAR];
    const jntoracle::Cfg cfgs[] = {{8, 8, 4, 0, 7, 7, 0, 0, 0, 0},
                                   {13, 5, 1, 0, 7, 7, 0, 0, 0, 0},
                                   {4, 8, 15, 0, 7, 7, 0, 0, 0, 0},
                                   {5, 4, 2, 0, 4, 7, 0, 0, 0, 0}};
    for (const auto& c : cfgs) {
        for (int bd : {8, 10}) {
            std::vector<std::uint16_t> plane16;
            const int stride = c.w + 16;
            fillPlane16(plane16, stride, c.h + 16, bd, 0xB0B5Eu + (std::uint32_t)c.w * 11 + bd);
            const std::uint16_t* src = plane16.data() + 8 * stride + 8;
            inter::ConvolveParams cp{};
            std::memset(&cp, 0, sizeof(cp));
            cp.round0 = c.round0;
            cp.round1 = c.round1;
            std::vector<std::uint16_t> got((size_t)c.w * c.h, 0x999);
            inter::highbdConvolveXsr(src, stride, got.data(), c.w, c.w, c.h, reg, nullptr, c.subpelX, 0, &cp, bd);
            std::vector<std::uint16_t> want;
            convXHbd(stride, src, c, reg, want, bd);
            if (bd == 8) {
                std::vector<std::uint8_t> plane8 = downPlane(plane16, stride, c.h + 16);
                std::vector<std::uint8_t> got8((size_t)c.w * c.h, 0x77);
                inter::convolveXSR(plane8.data() + 8 * stride + 8, stride, got8.data(), c.w, c.w, c.h, reg, nullptr,
                                   c.subpelX, 0, &cp);
                int bad8 = 0;
                for (int y = 0; y < c.h; ++y)
                    for (int x = 0; x < c.w; ++x)
                        if (got[(size_t)y * c.w + x] != got8[(size_t)y * c.w + x]) ++bad8;
                CHECK_MESSAGE(bad8 == 0, "x_sr derivation mismatch");
            }
            int bad = 0;
            for (int y = 0; y < c.h; ++y)
                for (int x = 0; x < c.w; ++x)
                    if (got[(size_t)y * c.w + x] != want[(size_t)y * c.w + x]) ++bad;
            CHECK_MESSAGE(bad == 0, "x_sr scalar mismatch");
            if (bad) {
                for (int y = 0; y < c.h; ++y)
                    for (int x = 0; x < c.w; ++x) {
                        if (got[(size_t)y * c.w + x] != want[(size_t)y * c.w + x])
                            MESSAGE("x_sr bd " << bd << " " << c.w << "x" << c.h << " sp " << c.subpelX << " at ("
                                               << y << "," << x << "): got " << got[(size_t)y * c.w + x] << " want "
                                               << want[(size_t)y * c.w + x]);
                    }
            }
        }
    }
}

TEST_CASE("highbd_convolve_y_sr: derivation at bd=8, scalar oracle at bd=10 (ip:761-785)") {
    using namespace hbdoracle;
    const convolve::InterpFilterParams* reg = &convolve::interpFilterParamsList[(int)convolve::InterpFilter::EIGHTTAP_REGULAR];
    const jntoracle::Cfg cfgs[] = {{8, 8, 0, 4, 7, 7, 0, 0, 0, 0},
                                   {13, 5, 0, 1, 7, 7, 0, 0, 0, 0},
                                   {4, 8, 0, 15, 7, 7, 0, 0, 0, 0},
                                   {5, 4, 0, 13, 4, 7, 0, 0, 0, 0}};
    for (const auto& c : cfgs) {
        for (int bd : {8, 10}) {
            std::vector<std::uint16_t> plane16;
            const int stride = c.w + 16;
            fillPlane16(plane16, stride, c.h + 16, bd, 0x0B0Bu + (std::uint32_t)c.h * 11 + bd);
            const std::uint16_t* src = plane16.data() + 8 * stride + 8;
            inter::ConvolveParams cp{};
            std::memset(&cp, 0, sizeof(cp));
            cp.round0 = c.round0;
            cp.round1 = c.round1;
            std::vector<std::uint16_t> got((size_t)c.w * c.h, 0x999);
            inter::highbdConvolveYsr(src, stride, got.data(), c.w, c.w, c.h, nullptr, reg, 0, c.subpelY, &cp, bd);
            // Independent scalar oracle: vertical pass, res rounded by
            // FILTER_BITS directly (the y path has no round_1 involvement).
            std::vector<std::uint16_t> want;
            {
                const int taps = reg->taps;
                const int foV = taps / 2 - 1;
                const std::int16_t* yf = convolve::getInterpFilterSubpelKernel(*reg, c.subpelY & convolve::subpelMask);
                want.assign((size_t)c.w * c.h, 0);
                for (int y = 0; y < c.h; ++y) {
                    for (int x = 0; x < c.w; ++x) {
                        std::int32_t res = 0;
                        for (int k = 0; k < taps; ++k) res += yf[k] * src[(y - foV + k) * stride + x];
                        std::int32_t v = (res + ((1 << convolve::filterBits) >> 1)) >> convolve::filterBits;
                        v = v < 0 ? 0 : (v > ((1 << bd) - 1) ? ((1 << bd) - 1) : v);
                        want[(size_t)y * c.w + x] = (std::uint16_t)v;
                    }
                }
            }
            if (bd == 8) {
                std::vector<std::uint8_t> plane8 = downPlane(plane16, stride, c.h + 16);
                std::vector<std::uint8_t> got8((size_t)c.w * c.h, 0x77);
                inter::convolveYSr(plane8.data() + 8 * stride + 8, stride, got8.data(), c.w, c.w, c.h, nullptr, reg, 0,
                                   c.subpelY, &cp);
                int bad8 = 0;
                for (int y = 0; y < c.h; ++y)
                    for (int x = 0; x < c.w; ++x)
                        if (got[(size_t)y * c.w + x] != got8[(size_t)y * c.w + x]) ++bad8;
                CHECK_MESSAGE(bad8 == 0, "y_sr derivation mismatch");
            }
            int bad = 0;
            for (int y = 0; y < c.h; ++y)
                for (int x = 0; x < c.w; ++x)
                    if (got[(size_t)y * c.w + x] != want[(size_t)y * c.w + x]) ++bad;
            CHECK_MESSAGE(bad == 0, "y_sr scalar mismatch");
        }
    }
}

// The two-pass scalar oracle for highbd_convolve_2d_sr (ip:787-:829). The
// pinned quirks: the horizontal intermediate is stored through ConvBufType
// (uint16 truncation of the round_0 result), and the vertical res is an
// int32 (NOT the int16 cast the 8-bit twin applies) before the clip at bd.
void conv2dHbd(int stride, const std::uint16_t* src, const jntoracle::Cfg& c, const convolve::InterpFilterParams* fx,
               const convolve::InterpFilterParams* fy, std::vector<std::uint16_t>& out, int bd) {
    using namespace jntoracle;
    const int tapsX = fx->taps, tapsY = fy->taps;
    const int foH = tapsX / 2 - 1, foV = tapsY / 2 - 1;
    const int imH = c.h + tapsY - 1;
    std::vector<std::uint16_t> im((size_t)imH * c.w);
    const std::int16_t* xf = convolve::getInterpFilterSubpelKernel(*fx, c.subpelX & convolve::subpelMask);
    for (int y = 0; y < imH; ++y) {
        for (int x = 0; x < c.w; ++x) {
            std::int32_t sum = 1 << (bd + kFb - 1);
            for (int k = 0; k < tapsX; ++k) sum += xf[k] * src[(y - foV) * stride + (x - foH + k)];
            im[(size_t)y * c.w + x] = (std::uint16_t)((sum + ((1 << c.round0) >> 1)) >> c.round0);
        }
    }
    const int offsetBits = bd + 2 * kFb - c.round0;
    const int offC = (1 << (offsetBits - c.round1)) + (1 << (offsetBits - c.round1 - 1));
    const int bits = 2 * kFb - c.round0 - c.round1;
    const std::int16_t* yf = convolve::getInterpFilterSubpelKernel(*fy, c.subpelY & convolve::subpelMask);
    out.assign((size_t)c.w * c.h, 0);
    for (int y = 0; y < c.h; ++y) {
        for (int x = 0; x < c.w; ++x) {
            std::int32_t sum = 1 << offsetBits;
            for (int k = 0; k < tapsY; ++k) sum += yf[k] * im[(size_t)(y + k) * c.w + x];
            std::int32_t res = ((sum + ((1 << c.round1) >> 1)) >> c.round1) - offC;
            std::int32_t v = (res + ((1 << bits) >> 1)) >> bits;
            v = v < 0 ? 0 : (v > ((1 << bd) - 1) ? ((1 << bd) - 1) : v);
            out[(size_t)y * c.w + x] = (std::uint16_t)v;
        }
    }
}

TEST_CASE("highbd_convolve_2d_sr: derivation at bd=8, scalar oracle at bd=10/12 (ip:787-829)") {
    using namespace hbdoracle;
    const convolve::InterpFilterParams* reg = &convolve::interpFilterParamsList[(int)convolve::InterpFilter::EIGHTTAP_REGULAR];
    const jntoracle::Cfg cfgs[] = {{8, 8, 4, 4, 7, 7, 0, 0, 0, 0},
                                   {13, 5, 1, 15, 7, 7, 0, 0, 0, 0},
                                   {4, 8, 15, 1, 7, 7, 0, 0, 0, 0},
                                   {5, 4, 2, 13, 7, 7, 0, 0, 0, 0},
                                   {16, 16, 8, 8, 7, 7, 0, 0, 0, 0}};
    for (const auto& c : cfgs) {
        for (int bd : {8, 10, 12}) {
            std::vector<std::uint16_t> plane16;
            const int stride = c.w + 16;
            fillPlane16(plane16, stride, c.h + 16, bd, 0x2D51u + (std::uint32_t)c.w * 7 + bd * 3);
            const std::uint16_t* src = plane16.data() + 8 * stride + 8;
            inter::ConvolveParams cp{};
            std::memset(&cp, 0, sizeof(cp));
            cp.round0 = c.round0;
            cp.round1 = c.round1;
            std::vector<std::uint16_t> got((size_t)c.w * c.h, 0x999);
            inter::highbdConvolve2dSr(src, stride, got.data(), c.w, c.w, c.h, reg, reg, c.subpelX, c.subpelY, &cp, bd);
            std::vector<std::uint16_t> want;
            conv2dHbd(stride, src, c, reg, reg, want, bd);
            if (bd == 8) {
                std::vector<std::uint8_t> plane8 = downPlane(plane16, stride, c.h + 16);
                std::vector<std::uint8_t> got8((size_t)c.w * c.h, 0x77);
                inter::convolve2dSr(plane8.data() + 8 * stride + 8, stride, got8.data(), c.w, c.w, c.h, reg, reg,
                                    c.subpelX, c.subpelY, &cp);
                int bad8 = 0;
                for (int y = 0; y < c.h; ++y)
                    for (int x = 0; x < c.w; ++x)
                        if (got[(size_t)y * c.w + x] != got8[(size_t)y * c.w + x]) ++bad8;
                CHECK_MESSAGE(bad8 == 0, "2d_sr derivation mismatch");
            }
            int bad = 0;
            for (int y = 0; y < c.h; ++y)
                for (int x = 0; x < c.w; ++x)
                    if (got[(size_t)y * c.w + x] != want[(size_t)y * c.w + x]) ++bad;
            CHECK_MESSAGE(bad == 0, "2d_sr scalar mismatch");
            if (bad) {
                for (int y = 0; y < c.h; ++y)
                    for (int x = 0; x < c.w; ++x) {
                        if (got[(size_t)y * c.w + x] != want[(size_t)y * c.w + x])
                            MESSAGE("2d_sr bd " << bd << " " << c.w << "x" << c.h << " at (" << y << "," << x
                                                << "): got " << got[(size_t)y * c.w + x] << " want "
                                                << want[(size_t)y * c.w + x]);
                    }
            }
        }
    }
}

// The independent scalar oracle for highbd_convolve_2d_scale (ip:831-:906):
// two-pass with the SCALE_SUBPEL_BITS=10 stepping, the dst16 compound
// branches, and the bd clip.
struct ScaleCfg {
    int w, h;
    int subpelXQn, xStepQn, subpelYQn, yStepQn;
    int round0, round1;
    int fwd, bck, useJnt;
    int isCompound, doAverage;
};

void convScaleHbd(int stride, const std::uint16_t* src, const ScaleCfg& c,
                  const convolve::InterpFilterParams* fx, const convolve::InterpFilterParams* fy,
                  const std::vector<std::uint16_t>& dst16Init, std::vector<std::uint16_t>& out, int bd) {
    using namespace jntoracle;
    const int tapsX = fx->taps, tapsY = fy->taps;
    const int foH = tapsX / 2 - 1, foV = tapsY / 2 - 1;
    const int kSsb = 10; // SCALE_SUBPEL_BITS
    const int kSsm = (1 << kSsb) - 1;
    const int imH = (((c.h - 1) * c.yStepQn + c.subpelYQn) >> kSsb) + tapsY;
    std::vector<std::uint16_t> im((size_t)imH * c.w);
    for (int y = 0; y < imH; ++y) {
        int xQn = c.subpelXQn;
        for (int x = 0; x < c.w; ++x, xQn += c.xStepQn) {
            const std::uint16_t* srcX = src + (y - foV) * stride + (xQn >> kSsb);
            const int xIdx = (xQn & kSsm) >> (kSsb - convolve::subpelBits);
            const std::int16_t* xf = convolve::getInterpFilterSubpelKernel(*fx, xIdx);
            std::int32_t sum = 1 << (bd + kFb - 1);
            for (int k = 0; k < tapsX; ++k) sum += xf[k] * srcX[k - foH];
            im[(size_t)y * c.w + x] = (std::uint16_t)((sum + ((1 << c.round0) >> 1)) >> c.round0);
        }
    }
    const int offsetBits = bd + 2 * kFb - c.round0;
    const int offC = (1 << (offsetBits - c.round1)) + (1 << (offsetBits - c.round1 - 1));
    const int bits = 2 * kFb - c.round0 - c.round1;
    out.assign((size_t)c.w * c.h, 0);
    for (int x = 0; x < c.w; ++x) {
        int yQn = c.subpelYQn;
        for (int y = 0; y < c.h; ++y, yQn += c.yStepQn) {
            const std::uint16_t* srcY = im.data() + ((yQn >> kSsb) + foV) * c.w + x;
            const int yIdx = (yQn & kSsm) >> (kSsb - convolve::subpelBits);
            const std::int16_t* yf = convolve::getInterpFilterSubpelKernel(*fy, yIdx);
            std::int32_t sum = 1 << offsetBits;
            for (int k = 0; k < tapsY; ++k) sum += yf[k] * srcY[(k - foV) * c.w];
            const std::int32_t res = (sum + ((1 << c.round1) >> 1)) >> c.round1;
            auto clipBd = [&](std::int32_t v) {
                return v < 0 ? 0 : (v > ((1 << bd) - 1) ? ((1 << bd) - 1) : v);
            };
            if (c.isCompound) {
                if (c.doAverage) {
                    std::int32_t t = dst16Init[(size_t)y * c.w + x];
                    if (c.useJnt) {
                        t = t * c.fwd + res * c.bck;
                        t >>= 4;
                    } else {
                        t += res;
                        t >>= 1;
                    }
                    t -= offC;
                    out[(size_t)y * c.w + x] = (std::uint16_t)clipBd((t + ((1 << bits) >> 1)) >> bits);
                } else {
                    // dst16 write is observed through out when !doAverage
                    out[(size_t)y * c.w + x] = (std::uint16_t)res;
                }
            } else {
                std::int32_t t = res - offC;
                out[(size_t)y * c.w + x] = (std::uint16_t)clipBd((t + ((1 << bits) >> 1)) >> bits);
            }
        }
    }
}

TEST_CASE("highbd_convolve_2d_scale: scalar oracle over the qn band + compound branches (ip:831-906)") {
    using namespace hbdoracle;
    const convolve::InterpFilterParams* reg = &convolve::interpFilterParamsList[(int)convolve::InterpFilter::EIGHTTAP_REGULAR];
    // qn values: identity (16384), 2x downscale (32768), 0.5x (8192), and
    // a non-power-of-two 1.25x (20480), with in-range subpels.
    const ScaleCfg cfgs[] = {
        {8, 8, 512, 16384, 512, 16384, 7, 7, 9, 7, 1, 1, 0},
        {8, 8, 512, 16384, 512, 16384, 7, 7, 9, 7, 1, 1, 1},
        {8, 8, 512, 16384, 512, 16384, 7, 7, 0, 0, 0, 1, 1},
        {13, 5, 256, 32768, 256, 32768, 7, 7, 12, 4, 1, 1, 0},
        {5, 4, 1024, 8192, 1024, 8192, 7, 7, 5, 11, 1, 0, 1},
        {4, 4, 100, 20480, 700, 20480, 7, 7, 13, 3, 1, 1, 1},
    };
    for (const auto& c : cfgs) {
        for (int bd : {8, 10}) {
            std::vector<std::uint16_t> plane16;
            const int stride = c.w * ((c.xStepQn + 1023) / 1024) + 16;
            fillPlane16(plane16, stride, c.h * ((c.yStepQn + 1023) / 1024) + 16, bd, 0x5CA1Eu + (std::uint32_t)c.w);
            const std::uint16_t* src = plane16.data() + 8 * stride + 8;
            inter::ConvolveParams cp{};
            std::memset(&cp, 0, sizeof(cp));
            cp.round0 = c.round0;
            cp.round1 = c.round1;
            cp.isCompound = c.isCompound;
            cp.doAverage = c.doAverage;
            // The 2d_scale family reads use_dist_wtd_comp_avg (ip:504/:883),
            // NOT use_jnt_comp_avg - the flag choice is part of what this
            // test pins.
            cp.useDistWtdCompAvg = c.useJnt;
            cp.fwdOffset = c.fwd;
            cp.bckOffset = c.bck;
            std::vector<std::uint16_t> dst16Init((size_t)c.w * c.h);
            std::uint32_t seed = 0xD15Eu + (std::uint32_t)c.h;
            for (size_t i = 0; i < dst16Init.size(); ++i) dst16Init[i] = (std::uint16_t)(lcg16(seed) & 0xFFFF);
            cp.dst = dst16Init.data();
            cp.dstStride = c.w;
            std::vector<std::uint16_t> got((size_t)c.w * c.h, 0x999);
            if (c.isCompound && !c.doAverage) {
                inter::highbdConvolve2dScale(src, stride, nullptr, c.w, c.w, c.h, reg, reg, c.subpelXQn, c.xStepQn,
                                             c.subpelYQn, c.yStepQn, &cp, bd);
                std::memcpy(got.data(), dst16Init.data(), (size_t)c.w * c.h * sizeof(std::uint16_t));
            } else {
                inter::highbdConvolve2dScale(src, stride, got.data(), c.w, c.w, c.h, reg, reg, c.subpelXQn, c.xStepQn,
                                             c.subpelYQn, c.yStepQn, &cp, bd);
            }
            std::vector<std::uint16_t> want;
            convScaleHbd(stride, src, c, reg, reg, dst16Init, want, bd);
            int bad = 0;
            for (int y = 0; y < c.h; ++y)
                for (int x = 0; x < c.w; ++x)
                    if (got[(size_t)y * c.w + x] != want[(size_t)y * c.w + x]) ++bad;
            CHECK_MESSAGE(bad == 0, "2d_scale scalar mismatch");
            if (bad) {
                for (int y = 0; y < c.h; ++y)
                    for (int x = 0; x < c.w; ++x) {
                        if (got[(size_t)y * c.w + x] != want[(size_t)y * c.w + x])
                            MESSAGE("2d_scale bd " << bd << " " << c.w << "x" << c.h << " avg " << c.doAverage
                                                   << " at (" << y << "," << x << "): got " << got[(size_t)y * c.w + x]
                                                   << " want " << want[(size_t)y * c.w + x]);
                    }
            }
        }
    }
}

TEST_CASE("highbd_convolve_2d_for_intrabc dispatches like the 8-bit twin at bd=8 (ip:1243-1257)") {
    using namespace hbdoracle;
    // The reference's own guard (ip:1411/:1436) only reaches this path when
    // at least one subpel is nonzero; the (0,0) case is out of contract
    // (filter_params_y would be NULL there).
    const struct {
        int subpelX, subpelY;
    } cases[] = {{8, 8}, {8, 0}, {0, 8}, {4, 12}};
    for (const auto& cs : cases) {
        std::vector<std::uint16_t> plane16;
        const int stride = 8 + 16;
        fillPlane16(plane16, stride, 8 + 16, 8, 0x1ABCu + (std::uint32_t)cs.subpelX);
        const std::uint16_t* src = plane16.data() + 8 * stride + 8;
        inter::ConvolveParams cp{};
        std::memset(&cp, 0, sizeof(cp));
        cp.round0 = convolve::filterBits;
        cp.round1 = convolve::filterBits;
        std::vector<std::uint16_t> got((size_t)8 * 8, 0x999);
        inter::highbdConvolve2dForIntrabc(src, stride, got.data(), 8, 8, 8, cs.subpelX, cs.subpelY, &cp, 8);
        // The 8-bit twin under the same dispatch.
        std::vector<std::uint8_t> plane8 = downPlane(plane16, stride, 8 + 16);
        std::vector<std::uint8_t> got8((size_t)8 * 8, 0x77);
        inter::convolve2dForIntrabc(plane8.data() + 8 * stride + 8, stride, got8.data(), 8, 8, 8, cs.subpelX,
                                    cs.subpelY, &cp);
        int bad = 0;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                if (got[(size_t)y * 8 + x] != got8[(size_t)y * 8 + x]) ++bad;
        CHECK_MESSAGE(bad == 0, "intrabc derivation mismatch");
        if (bad) {
            MESSAGE("intrabc subpel (" << cs.subpelX << "," << cs.subpelY << ") diffs " << bad);
        }
    }
}

// ------------------------------------------------------------- A3E J3
// The highbd jnt (compound) family (ip:908-:1097). The dst16 OUTPUT
// (clip at bd) is the uint16 analogue of the 8-bit family's dst8; the
// staging CONV_BUF (conv_params->dst) carries the same res values. The
// derivation fixture: at bd=8 the dst16 output must equal the landed
// 8-bit jnt family's dst8 on the same data.

void runHbdJntFamily(int bd, void (*kern)(const std::uint16_t*, std::int32_t, std::uint16_t*, std::int32_t,
                                          std::int32_t, std::int32_t, const convolve::InterpFilterParams*,
                                          const convolve::InterpFilterParams*, std::int32_t, std::int32_t,
                                          inter::ConvolveParams*, std::int32_t),
                     const char* label) {
    using namespace jntoracle;
    const convolve::InterpFilterParams* reg = &convolve::interpFilterParamsList[(int)convolve::InterpFilter::EIGHTTAP_REGULAR];
    const Cfg cfgs[] = {
        {8, 8, 4, 4, 7, 7, 9, 7, 1, 1},   {8, 8, 4, 4, 7, 7, 9, 7, 1, 0},   {13, 5, 1, 15, 7, 7, 12, 4, 1, 1},
        {4, 8, 15, 1, 7, 7, 5, 11, 1, 0}, {5, 4, 2, 13, 4, 7, 13, 3, 1, 1}, {1, 3, 8, 8, 7, 7, 9, 7, 1, 1},
    };
    for (const Cfg& c : cfgs) {
        std::vector<std::uint16_t> plane16;
        const int stride = c.w + 16;
        const int vmax = (1 << bd) - 1;
        {
            std::uint32_t seed = 0xBEEFu + (std::uint32_t)c.w * 7 + c.h * 5 + bd;
            plane16.resize((size_t)stride * (c.h + 16));
            for (size_t i = 0; i < plane16.size(); ++i) plane16[i] = (std::uint16_t)(hbdoracle::lcg16(seed) & vmax);
        }
        const std::uint16_t* src = plane16.data() + 8 * stride + 8;
        std::vector<std::uint16_t> dst16Init((size_t)c.w * c.h);
        std::uint32_t dstSeed = 0xCAFEu + (std::uint32_t)c.h + bd;
        for (size_t i = 0; i < dst16Init.size(); ++i) dst16Init[i] = (std::uint16_t)(hbdoracle::lcg16(dstSeed) * 131u);
        inter::ConvolveParams cp{};
        std::memset(&cp, 0, sizeof(cp));
        cp.round0 = c.round0;
        cp.round1 = c.round1;
        cp.useJntCompAvg = c.useJnt;
        cp.fwdOffset = c.fwd;
        cp.bckOffset = c.bck;
        cp.doAverage = c.doAverage;
        cp.dst = dst16Init.data();
        cp.dstStride = c.w;
        std::vector<std::uint16_t> dst16Out((size_t)c.w * c.h + 8, 0xABCD);
        kern(src, stride, dst16Out.data(), c.w, c.w, c.h, reg, reg, c.subpelX, c.subpelY, &cp, bd);
        int bad = 0;
        if (bd == 8) {
            // Derivation: the 8-bit jnt family on the same data must give
            // the same values. When do_average=1 the observable is the
            // OUTPUT buffer (dst16Out vs the 8-bit dst8); when 0, it is
            // the staging CONV_BUF (both kernels write res there).
            std::vector<std::uint8_t> plane8((size_t)stride * (c.h + 16));
            for (size_t i = 0; i < plane8.size(); ++i) plane8[i] = (std::uint8_t)plane16[i];
            std::vector<std::uint16_t> dst16Copy = dst16Init;
            inter::ConvolveParams cp8 = cp;
            cp8.dst = dst16Copy.data();
            std::vector<std::uint8_t> dst8((size_t)c.w * c.h, 0);
            void (*kern8)(const std::uint8_t*, std::int32_t, std::uint8_t*, std::int32_t, std::int32_t, std::int32_t,
                          const convolve::InterpFilterParams*, const convolve::InterpFilterParams*, std::int32_t,
                          std::int32_t, inter::ConvolveParams*) = nullptr;
            if (std::string(label) == "hjnt_x") kern8 = &inter::jntConvolveX;
            else if (std::string(label) == "hjnt_y") kern8 = &inter::jntConvolveY;
            else if (std::string(label) == "hjnt_copy") kern8 = &inter::jntConvolve2dCopy;
            else kern8 = &inter::jntConvolve2d;
            kern8(plane8.data() + 8 * stride + 8, stride, dst8.data(), c.w, c.w, c.h, reg, reg, c.subpelX, c.subpelY,
                  &cp8);
            for (int y = 0; y < c.h; ++y)
                for (int x = 0; x < c.w; ++x) {
                    if (c.doAverage) {
                        if (dst16Out[(size_t)y * c.w + x] != dst8[(size_t)y * c.w + x]) ++bad;
                    } else if (dst16Init[(size_t)y * c.w + x] != dst16Copy[(size_t)y * c.w + x]) {
                        ++bad;
                    }
                }
            CHECK_MESSAGE(bad == 0, "hjnt bd8 derivation mismatch");
        } else {
            // bd=10 scalar oracle: the same compose, clipped at bd. This
            // is the SECOND transcription of the family's arithmetic.
            for (int y = 0; y < c.h; ++y) {
                for (int x = 0; x < c.w; ++x) {
                    std::int32_t res;
                    if (std::string(label) == "hjnt_x") {
                        const int tapsX = reg->taps, foH = tapsX / 2 - 1;
                        const int bits = kFb - c.round1;
                        const std::int16_t* xf =
                            convolve::getInterpFilterSubpelKernel(*reg, c.subpelX & convolve::subpelMask);
                        std::int32_t s = 0;
                        for (int k = 0; k < tapsX; ++k) s += xf[k] * src[y * stride + x - foH + k];
                        res = (1 << bits) * ((s + ((1 << c.round0) >> 1)) >> c.round0);
                        const int offsetBits = bd + 2 * kFb - c.round0;
                        res += (1 << (offsetBits - c.round1)) + (1 << (offsetBits - c.round1 - 1));
                    } else if (std::string(label) == "hjnt_y") {
                        const int tapsY = reg->taps, foV = tapsY / 2 - 1;
                        const int bits = kFb - c.round0;
                        const std::int16_t* yf =
                            convolve::getInterpFilterSubpelKernel(*reg, c.subpelY & convolve::subpelMask);
                        std::int32_t s = 0;
                        for (int k = 0; k < tapsY; ++k) s += yf[k] * src[(y - foV + k) * stride + x];
                        s *= (1 << bits);
                        const int offsetBits = bd + 2 * kFb - c.round0;
                        res = ((s + ((1 << c.round1) >> 1)) >> c.round1) +
                            ((1 << (offsetBits - c.round1)) + (1 << (offsetBits - c.round1 - 1)));
                    } else if (std::string(label) == "hjnt_copy") {
                        const int bits = 2 * kFb - c.round1 - c.round0;
                        const int offsetBits = bd + 2 * kFb - c.round0;
                        res = (int)(std::uint16_t)(src[y * stride + x] << bits) +
                            ((1 << (offsetBits - c.round1)) + (1 << (offsetBits - c.round1 - 1)));
                    } else {
                        // hjnt_2d: two-pass compose
                        const int tapsX = reg->taps, tapsY = reg->taps;
                        const int foH = tapsX / 2 - 1, foV = tapsY / 2 - 1;
                        const int imH = c.h + tapsY - 1;
                        std::vector<std::uint16_t> im((size_t)imH * c.w);
                        const std::int16_t* xf =
                            convolve::getInterpFilterSubpelKernel(*reg, c.subpelX & convolve::subpelMask);
                        for (int r = 0; r < imH; ++r) {
                            for (int xx = 0; xx < c.w; ++xx) {
                                std::int32_t s = 1 << (bd + kFb - 1);
                                for (int k = 0; k < tapsX; ++k)
                                    s += xf[k] * src[(r - foV) * stride + (xx - foH + k)];
                                im[(size_t)r * c.w + xx] =
                                    (std::uint16_t)((s + ((1 << c.round0) >> 1)) >> c.round0);
                            }
                        }
                        const int offsetBits = bd + 2 * kFb - c.round0;
                        const std::int16_t* yf =
                            convolve::getInterpFilterSubpelKernel(*reg, c.subpelY & convolve::subpelMask);
                        std::int32_t s = 1 << offsetBits;
                        for (int k = 0; k < tapsY; ++k) s += yf[k] * im[(size_t)(y + k) * c.w + x];
                        res = (s + ((1 << c.round1) >> 1)) >> c.round1;
                    }
                    if (c.doAverage) {
                        std::int32_t t = dst16Init[(size_t)y * c.w + x];
                        if (c.useJnt) {
                            t = t * c.fwd + res * c.bck;
                            t >>= 4;
                        } else {
                            t += res;
                            t >>= 1;
                        }
                        const int roundBits = 2 * kFb - c.round0 - c.round1;
                        // The single-axis/copy forms subtract roundOffset
                        // (computed from offsetBits with bd=8!); the 2d
                        // form subtracts the SAME expression. See the
                        // reference: round_offset uses offset_bits which
                        // carries bd, EXCEPT 2d's inline subtract also
                        // carries bd. Both are bd-carried here.
                        const int offsetBits = (std::string(label) == "hjnt_2d") ? bd + 2 * kFb - c.round0
                                                                                : bd + 2 * kFb - c.round0;
                        const int roundOffset =
                            (1 << (offsetBits - c.round1)) + (1 << (offsetBits - c.round1 - 1));
                        t -= roundOffset;
                        std::int32_t v = (t + ((1 << roundBits) >> 1)) >> roundBits;
                        v = v < 0 ? 0 : (v > vmax ? vmax : v);
                        if (dst16Out[(size_t)y * c.w + x] != (std::uint16_t)v) ++bad;
                    } else {
                        if (dst16Init[(size_t)y * c.w + x] != (std::uint16_t)res) ++bad;
                    }
                }
            }
            CHECK_MESSAGE(bad == 0, "hjnt bd10 scalar mismatch");
        }
        if (bad) {
            MESSAGE(std::string(label) << " bd " << bd << " cfg " << c.w << "x" << c.h << " avg " << c.doAverage
                                       << " diffs " << bad);
        }
    }
}

TEST_CASE("highbd_jnt_convolve_x: derivation at bd=8, scalar oracle at bd=10 (ip:908-951)") {
    runHbdJntFamily(8, &inter::highbdJntConvolveX, "hjnt_x");
    runHbdJntFamily(10, &inter::highbdJntConvolveX, "hjnt_x");
}

TEST_CASE("highbd_jnt_convolve_y: derivation at bd=8, scalar oracle at bd=10 (ip:953-996)") {
    runHbdJntFamily(8, &inter::highbdJntConvolveY, "hjnt_y");
    runHbdJntFamily(10, &inter::highbdJntConvolveY, "hjnt_y");
}

TEST_CASE("highbd_jnt_convolve_2d_copy: derivation at bd=8, scalar oracle at bd=10 (ip:998-1035)") {
    runHbdJntFamily(8, &inter::highbdJntConvolve2dCopy, "hjnt_copy");
    runHbdJntFamily(10, &inter::highbdJntConvolve2dCopy, "hjnt_copy");
}

TEST_CASE("highbd_jnt_convolve_2d: derivation at bd=8, scalar oracle at bd=10 (ip:1037-1097)") {
    runHbdJntFamily(8, &inter::highbdJntConvolve2d, "hjnt_2d");
    runHbdJntFamily(10, &inter::highbdJntConvolve2d, "hjnt_2d");
}

// ------------------------------------------------------------- A3E W1
// The wedge mask synthesis (ip:2034-:2230). Oracles:
// (a) shift_copy direct fixtures (edge replication both directions);
// (b) the SIGNFLIP CONSISTENCY oracle: for every wedge-carrying bsize and
//     wedge index, the average rule (init_wedge_signs :2136-:2170,
//     computed here independently over get_wedge_mask_inplace's row 0 +
//     column 0) must reproduce the PRECOMPUTED wedge_signflip_lookup
//     entry - the precomputed table was generated by exactly this rule;
// (c) the COMPLEMENT property: mask[sign 1][i] + mask[sign 0][i] == 64
//     pointwise over the contiguous window;
// (d) the VERTICAL/ OBLIQUE63 window content vs independent closed-form
//     reconstructions of the primary planes.

// ------------------------------------------------------------- A3E S2
// The inter_prediction.c tail utilities (:2401-:2592) + scale_chroma_bsize
// (intra_prediction.c:2492-:2544).

TEST_CASE("sum_squares_i16 and wedge_sse_from_residuals (ip:2533-2541, :2468-2477)") {
    const std::int16_t v[5] = {-3, 4, -5, 2, 0};
    CHECK(inter::sumSquaresI16(v, 5) == 9 + 16 + 25 + 4 + 0);
    // The reference's do/while(--n) contract requires N >= 1 (the real
    // callers pass N = bw * bh >= 64); n = 0 would not terminate.
    const std::int16_t r1[3] = {10, -20, 30};
    const std::int16_t d[3] = {5, -5, 2};
    const std::uint8_t m[3] = {64, 32, 16};
    // t = 64*10 + 64*5 = 960; 64*-20 + 32*-5 = -1440; 64*30 + 16*2 = 1952.
    // csse = 960^2 + 1440^2 + 1952^2 = 921600 + 2073600 + 3810304 = 6805504.
    // ROUND_POWER_OF_TWO(csse, 12) = (6805504 + 2048) >> 12 = 1662.
    CHECK(inter::wedgeSseFromResiduals(r1, d, m, 3) == 1662);
    // Clamping: a term beyond int16 saturates (the reference's clamp note).
    const std::int16_t r1b[1] = {300};
    const std::int16_t db[1] = {300};
    const std::uint8_t mb[1] = {64};
    // t = 64*300 + 64*300 = 38400 -> INT16_MAX = 32767.
    CHECK(inter::wedgeSseFromResiduals(r1b, db, mb, 1) == (32767 * 32767 + 2048) >> 12);
}

// ------------------------------------------------------------- A3E E2
// The curvfit RD-model family (enc_inter_prediction.c:197-:361). The two
// grid tables are RUNTIME-DIFF-VERIFIED against the reference file
// (parsed with strtod - a bit-identical transcription check, the RC1
// precedent extended to doubles). av1_model_rd_curvfit is checked
// against an independent test-side transcription over the full
// rcat/dcat band and a clamp sweep; model_rd_with_curvfit carries
// (quantizer, rdmult) as parameters and its skip rule is pinned by
// fixtures where the raw fit loses to the sse << 4 bound.

namespace curvfitoracle {

// The independent transcription of av1_model_rd_curvfit
// (enc:294-:312): clamps, the 0.5-step interpolation, the floor.
double modelRdCurvfit(int rcat, double sseNorm, double xqr, double* distBySse) {
    const double xStart = -15.5;
    const double xEnd = 16.5;
    const double xStep = 0.5;
    const double epsilon = 1e-6;
    const int dcat = (sseNorm > 16.0) ? 1 : 0;
    xqr = xqr < (xStart + xStep + epsilon) ? (xStart + xStep + epsilon) : xqr;
    xqr = xqr > (xEnd - xStep - epsilon) ? (xEnd - xStep - epsilon) : xqr;
    const double x = (xqr - xStart) / xStep;
    const int xi = (int)std::floor(x);
    const double* prate = &inter::interpRgridCurv[rcat][xi - 1];
    const double* pdist = &inter::interpDgridCurv[dcat][xi - 1];
    *distBySse = pdist[1];
    return prate[1];
}

} // namespace curvfitoracle

TEST_CASE("interp_rgrid_curv / interp_dgrid_curv match the reference entry-for-entry (enc:204-270)") {
    const std::string text = referenceTextEnc();
    const std::regex dblRe(R"(-?\d+\.\d+)");
    std::vector<double> ref;
    {
        const std::size_t rStart = text.find("interp_rgrid_curv[4][65]");
        REQUIRE(rStart != std::string::npos);
        const std::size_t dStart = text.find("interp_dgrid_curv[2][65]", rStart);
        REQUIRE(dStart != std::string::npos);
        const std::size_t dEnd = text.find("};", dStart);
        REQUIRE(dEnd != std::string::npos);
        const std::string body = text.substr(rStart, dEnd - rStart);
        for (auto n = std::sregex_iterator(body.begin(), body.end(), dblRe); n != std::sregex_iterator(); ++n)
            ref.push_back(std::strtod(n->str().c_str(), nullptr));
    }
    REQUIRE(ref.size() == 4 * 65 + 2 * 65);
    int mismatches = 0;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 65; ++c)
            if (inter::interpRgridCurv[r][c] != ref[r * 65 + c]) ++mismatches;
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 65; ++c)
            if (inter::interpDgridCurv[r][c] != ref[4 * 65 + r * 65 + c]) ++mismatches;
    CHECK(mismatches == 0);
    // Structural anchors: the r-grid ramps to 4096 at the end; the d-grid
    // starts at 16 and ends at 0.
    CHECK(inter::interpRgridCurv[0][64] == 4096.0);
    CHECK(inter::interpDgridCurv[0][0] == 16.0);
    CHECK(inter::interpDgridCurv[1][64] == 0.0);
}

TEST_CASE("av1_model_rd_curvfit matches the independent transcription over the bands (enc:294-312)") {
    // The bsize category table (enc:197-:198), entry-for-entry; it also
    // drives the rcat mapping for the oracle below.
    const int wantCats[inter::kBlockSizesAll] = {0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 0, 0, 1, 1, 2, 2};
    int catBad = 0;
    for (int i = 0; i < inter::kBlockSizesAll; ++i)
        if (inter::bsizeCurvfitModelCatLookup[i] != wantCats[i]) ++catBad;
    CHECK(catBad == 0);
    for (int bsize = 0; bsize < inter::kBlockSizesAll; ++bsize) {
        for (double sseNorm : {0.5, 16.0, 16.0001, 100.0, 1e6}) {
            for (double xqr : {-20.0, -15.0, -10.0, -3.3, 0.0, 1.5, 8.0, 16.4, 16.6, 100.0}) {
                double wantDist = 0, gotDist = 0;
                const double wantRate = curvfitoracle::modelRdCurvfit(wantCats[bsize], sseNorm, xqr, &wantDist);
                double gotRate = 0;
                inter::modelRdCurvfit(bsize, sseNorm, xqr, &gotRate, &gotDist);
                CHECK(gotRate == wantRate);
                CHECK(gotDist == wantDist);
            }
        }
    }
}

TEST_CASE("model_rd_with_curvfit: the skip rule and the zero-sse arm (enc:316-361)") {
    std::uint32_t rate = 0;
    std::uint64_t dist = 0;
    // sse = 0 -> both outputs 0.
    inter::modelRdWithCurvfit(3, 0, 64, 56, 200, &rate, &dist);
    CHECK(rate == 0);
    CHECK(dist == 0);
    // A large rdmult makes the fit's rate term dominate: the skip
    // override FIRES (rate 0, dist sse << 4). Measured shape:
    // sseNorm = 1, xqr = get_msb(0|1) = 0, xi = 31,
    // rate_i = 772.07 * 64 ~= 49413, rdFit ~= 49413 * 100000 >> 9 =
    // 9,651,000 >= rdSkip = (64 << 4) << 7 = 131,072.
    inter::modelRdWithCurvfit(3, 64, 64, 8, 100000, &rate, &dist);
    CHECK(rate == 0);
    CHECK(dist == (std::uint64_t)(64 << 4));
    // A mid qstep: the fit survives; the expectation is composed from the
    // transcription plus the RDCOST skip comparison (rd_cost.h:36-38).
    {
        const double sseNorm = 1000.0 / 64.0;
        // svt_log2f_safe(x) = get_msb(x | 1) (definitions.h:591); the
        // (uint32) cast of sse_norm floors it, then / (qstep * qstep).
        int msb = 0;
        std::uint32_t val = (std::uint32_t)sseNorm | 1;
        for (int i = 4; i >= 0; --i) {
            const std::uint32_t x = val >> (1 << i);
            if (x) {
                val = x;
                msb += (1 << i);
            }
        }
        const double xqr = (double)msb;
        double distBySse = 0;
        const double rateF = curvfitoracle::modelRdCurvfit(0, sseNorm, xqr, &distBySse);
        const double distF = distBySse * sseNorm;
        int rateI = (int)((rateF * 64) + 0.5);
        std::int64_t distI = (std::int64_t)((distF * 64) + 0.5);
        if (rateI == 0) {
            distI = 1000 << 4;
        } else {
            const std::int64_t rdFit = (((rateI * (std::int64_t)200) + ((1 << 9) >> 1)) >> 9) + (distI << 7);
            const std::int64_t rdSkip = (((0 * (std::int64_t)200) + ((1 << 9) >> 1)) >> 9) + (((std::int64_t)(1000 << 4)) << 7);
            if (rdFit >= rdSkip) {
                rateI = 0;
                distI = 1000 << 4;
            }
        }
        inter::modelRdWithCurvfit(0, 1000, 64, 8, 200, &rate, &dist);
        CHECK(rate == (std::uint32_t)rateI);
        CHECK(dist == (std::uint64_t)distI);
    }
    // The composed expectation at quantizer 1 / rdmult 200 as well (the
    // fit survives there: rdFit ~= 56,778 < rdSkip = 2,048,000).
    {
        const double sseNorm = 1000.0 / 64.0;
        int msb = 0;
        std::uint32_t val = (std::uint32_t)sseNorm | 1;
        for (int i = 4; i >= 0; --i) {
            const std::uint32_t x = val >> (1 << i);
            if (x) {
                val = x;
                msb += (1 << i);
            }
        }
        double distBySse = 0;
        const double rateF = curvfitoracle::modelRdCurvfit(0, sseNorm, (double)msb, &distBySse);
        int rateI = (int)((rateF * 64) + 0.5);
        std::int64_t distI = (std::int64_t)(((distBySse * sseNorm) * 64) + 0.5);
        if (rateI == 0) {
            distI = 1000 << 4;
        } else if ((((rateI * (std::int64_t)200) + 256) >> 9) + (distI << 7) >= (std::int64_t)(1000 << 4) << 7) {
            rateI = 0;
            distI = 1000 << 4;
        }
        inter::modelRdWithCurvfit(3, 1000, 64, 1, 200, &rate, &dist);
        CHECK(rate == (std::uint32_t)rateI);
        CHECK(dist == (std::uint64_t)distI);
    }
}
// ------------------------------------------------------------- A3E E1
// clamp_mv_to_umv_border_sb (enc_inter_prediction.c:55-:75). The xd
// mb_to_*_edge fields thread as the four int parameters (the AMVP1
// carrier precedent). The subsampling rule: the chroma MV is HALVED
// (mv * (1 << (1 - ss))) before the clamp, and the clamp box is widened
// by the subpel extent ((AOM_INTERP_EXTEND + bw) << SUBPEL_BITS).

TEST_CASE("clamp_mv_to_umv_border_sb clamps the doubled MV into the subpel box (enc:55-75)") {
    // Luma (ss 0): spel_left = (4 + bw) << 4; box = edges +/- extent.
    // mb_to_left = -48, mb_to_right = 48, mb_to_top = -48, mb_to_bottom = 48
    // (an 8x8 block at frame edge semantics), bw = bh = 8.
    {
        inter::Mv mv{1000, -2000};
        const inter::Mv c = inter::clampMvToUmvBorderSb(-48, 48, -48, 48, &mv, 8, 8, 0, 0);
        // spel_left = (4 + 8) << 4 = 192; left limit = -48*2 - 192 = -288.
        // spel_right = 192 - 16 = 176; right limit = 48*2 + 176 = 272.
        // spel_top = 192; top limit = -48*2 - 192 = -288.
        // spel_bottom = 176; bottom limit = 48*2 + 176 = 272.
        // The luma MV DOUBLES first (1 << (1 - ss_x) = 2 at ss 0):
        // 2000 clamps to the right bound, -4000 to the top bound.
        CHECK(c.x == 272);
        CHECK(c.y == -288);
    }
    {
        inter::Mv mv{100, 100};
        const inter::Mv c = inter::clampMvToUmvBorderSb(-48, 48, -48, 48, &mv, 8, 8, 0, 0);
        CHECK(c.x == 200); // 100 * 2 = 200, inside [-288, 272]: the clamp leaves it
        CHECK(c.y == 200);
    }
    // The LEFT bound specifically: a deeply negative luma MV clamps to
    // -48*2 - spel_left = -288 (spel_left, not spel_right - the extents
    // differ by SUBPEL_SHIFTS).
    {
        inter::Mv mv{-5000, 0};
        const inter::Mv c = inter::clampMvToUmvBorderSb(-48, 48, -48, 48, &mv, 8, 8, 0, 0);
        CHECK(c.x == -288);
    }
    // Chroma (ss 1): 1 << (1 - ss) = 1, so the MV is unchanged and the
    // box edges stay at their unscaled values.
    {
        inter::Mv mv{500, -500};
        const inter::Mv c = inter::clampMvToUmvBorderSb(-48, 48, -48, 48, &mv, 8, 8, 1, 1);
        CHECK(c.x == 224);  // 48 + (192 - 16)
        CHECK(c.y == -240); // -48 - 192
    }
}

TEST_CASE("find_ref_dv picks the in-tile delay offset (ip:2401-2412)") {
    // Tile starting at mi_row 8, mib_size 16, current mi_row 8 (row 0 of
    // the tile): mi_row - mib_size < start -> the LEFT/-x delay form.
    inter::Mv dv{0, 0};
    inter::findRefDv(&dv, 8, 16, 8);
    CHECK(dv.x == (-4 * 16 - 256) * 8);
    CHECK(dv.y == 0);
    // Current mi_row 24 (deep in the tile): the ABOVE form.
    inter::Mv dv2{9, 9};
    inter::findRefDv(&dv2, 8, 16, 24);
    CHECK(dv2.x == 0);
    CHECK(dv2.y == (-4 * 16) * 8);
}

TEST_CASE("skip_u4x4_pred_in_obmc pins the one-sided 4xN rule (ip:2414-2436)") {
    // DISABLE_CHROMA_U8X8_OBMC = 0 (inter_prediction.h:41): dir==0 only.
    // plane bsizes 4X4 (0), 8X4 (2), 4X8 (1): luma (ss 0) -> dir==0;
    // chroma (ss 1) maps 8X8->4X4 etc.
    CHECK(inter::skipU4x4PredInObmc(0, 0, 0, 0) == 1);  // luma 4x4, above
    CHECK(inter::skipU4x4PredInObmc(0, 1, 0, 0) == 0);  // luma 4x4, left
    CHECK(inter::skipU4x4PredInObmc(1, 1, 0, 0) == 0);  // luma 4x8, left
    CHECK(inter::skipU4x4PredInObmc(2, 0, 0, 0) == 1);  // luma 8x4, above
    // 4:2:0: 8x8 -> chroma 4x4.
    CHECK(inter::skipU4x4PredInObmc(0, 0, 1, 1) == 1);
    CHECK(inter::skipU4x4PredInObmc(3, 0, 1, 1) == 1);  // 8x8 -> chroma 4x4, above
    CHECK(inter::skipU4x4PredInObmc(3, 1, 1, 1) == 0);  // 8x8 -> chroma 4x4, left
    CHECK(inter::skipU4x4PredInObmc(6, 0, 1, 1) == 0);  // 16x16 -> chroma 8x8: default 0
}

TEST_CASE("scale_chroma_bsize maps the subsampled plane sizes (intra_prediction.c:2492-2544)") {
    // 4x4 luma at 4:2:0 -> 8x8 chroma (the AV1 min-8 rule).
    CHECK(inter::scaleChromaBsize(0, 1, 1) == 3);  // BLOCK_8X8
    CHECK(inter::scaleChromaBsize(0, 1, 0) == 2);  // BLOCK_8X4
    CHECK(inter::scaleChromaBsize(0, 0, 1) == 1);  // BLOCK_4X8
    CHECK(inter::scaleChromaBsize(0, 0, 0) == 0);  // unchanged
    // 4x8: ssx folds both branches to 8x8.
    CHECK(inter::scaleChromaBsize(1, 1, 1) == 3);
    CHECK(inter::scaleChromaBsize(1, 1, 0) == 3);
    CHECK(inter::scaleChromaBsize(1, 0, 1) == 1);  // stays 4x8
    // 8x4: ssx keeps 8x4; ssy folds to 8x8.
    CHECK(inter::scaleChromaBsize(2, 1, 1) == 3);
    CHECK(inter::scaleChromaBsize(2, 1, 0) == 2);
    CHECK(inter::scaleChromaBsize(2, 0, 1) == 3);
    // 4x16 -> 8x16 (4) at ssx; stays at ssy.
    CHECK(inter::scaleChromaBsize(16, 1, 1) == 4);
    CHECK(inter::scaleChromaBsize(16, 1, 0) == 4);
    CHECK(inter::scaleChromaBsize(16, 0, 1) == 16);
    // 16x4 -> 16x8 (5) at 4:2:0, stays 16x4 (17) at ssx-only.
    CHECK(inter::scaleChromaBsize(17, 1, 1) == 5);
    CHECK(inter::scaleChromaBsize(17, 1, 0) == 17);
    CHECK(inter::scaleChromaBsize(17, 0, 1) == 5);
    // Sizes >= 8x8 are untouched.
    CHECK(inter::scaleChromaBsize(3, 1, 1) == 3);
    CHECK(inter::scaleChromaBsize(6, 1, 1) == 6);
}

TEST_CASE("mode_context_analyzer folds the compound context (ip:2576-2592)") {
    // rf[1] <= INTRA_FRAME (0): the context passes through.
    const std::int32_t rfIntra[2] = {1, 0};
    CHECK(inter::modeContextAnalyzer(5, rfIntra) == 5);
    const std::int32_t rfNone[2] = {1, -1};
    CHECK(inter::modeContextAnalyzer(7, rfNone) == 7);
    // Compound: comp_ctx = map[refmv_ctx >> 1][min(newmv_ctx, 4)].
    // mode_context bitfield: newmv_ctx = ctx & 7, refmv_ctx = (ctx >> 4) & 15.
    const std::int32_t rf2[2] = {1, 2};
    CHECK(inter::modeContextAnalyzer(0, rf2) == 0);          // newmv 0, refmv 0 -> map[0][0] = 0
    CHECK(inter::modeContextAnalyzer(3, rf2) == 1);          // newmv 3, refmv 0 -> map[0][3] = 1
    CHECK(inter::modeContextAnalyzer((2 << 4) | 4, rf2) == 4); // refmv 2 -> map[1][4] = 4
    CHECK(inter::modeContextAnalyzer((3 << 4) | 2, rf2) == 3); // refmv 3 -> map[1][2] = 3
    CHECK(inter::modeContextAnalyzer((5 << 4) | 7, rf2) == 7); // refmv 5 -> map[2][4] = 7 (newmv clamped 4)
}

TEST_CASE("get_obmc_mask returns the six length tables (ip:2543-2574)") {
    const std::uint8_t m1[1] = {64};
    const std::uint8_t m2[2] = {45, 64};
    const std::uint8_t m4[4] = {39, 50, 59, 64};
    const std::uint8_t m8[8] = {36, 42, 48, 53, 57, 61, 64, 64};
    const std::uint8_t m16[16] = {34, 37, 40, 43, 46, 49, 52, 54, 56, 58, 60, 61, 64, 64, 64, 64};
    const std::uint8_t m32[32] = {33, 35, 36, 38, 40, 41, 43, 44, 45, 47, 48, 50, 51, 52, 53, 55,
                                  56, 57, 58, 59, 60, 60, 61, 62, 64, 64, 64, 64, 64, 64, 64, 64};
    CHECK(std::memcmp(inter::getObmcMask(1), m1, 1) == 0);
    CHECK(std::memcmp(inter::getObmcMask(2), m2, 2) == 0);
    CHECK(std::memcmp(inter::getObmcMask(4), m4, 4) == 0);
    CHECK(std::memcmp(inter::getObmcMask(8), m8, 8) == 0);
    CHECK(std::memcmp(inter::getObmcMask(16), m16, 16) == 0);
    CHECK(std::memcmp(inter::getObmcMask(32), m32, 32) == 0);
    // Each table is monotone nondecreasing and ends at 64.
    for (int len : {1, 2, 4, 8, 16, 32}) {
        const std::uint8_t* m = inter::getObmcMask(len);
        for (int i = 1; i < len; ++i) CHECK(m[i] >= m[i - 1]);
        CHECK(m[len - 1] == 64);
    }
}

TEST_CASE("wedge mask synthesis: signflip consistency, complement, window content (ip:2077-2223)") {
    // init must run before the masks are readable.
    inter::initWedgeMasks();
    const int wedgeCarriers[] = {3, 4, 5, 6, 7, 8, 9, 18, 19};
    // (b) signflip consistency: avg rule (ip:2136-:2170, the reference's
    // own generator for the precomputed table) vs wedge_signflip_lookup.
    int signflipBad = 0;
    // The GENERATOR read: init_wedge_signs (:2136-:2170) ran with signflip
    // ZEROED, so its get_wedge_mask_inplace(w, 0) hit the PRIMARY plane
    // [0][dir]; the runtime call incorporates the table's own signflip
    // (neg ^ wsignflip), a fixed point. The oracle therefore reads the
    // primary plane directly through the wedgeMaskOblData view.
    const std::uint8_t* obl = inter::wedgeMaskOblData(); // [neg][dir][64*64]
    for (int bsize : wedgeCarriers) {
        const inter::WedgeCodeType* cb = nullptr;
        if (bsize == 3 || bsize == 6 || bsize == 9) cb = inter::wedgeCodebook16Heqw;
        else if (bsize == 4 || bsize == 7 || bsize == 18) cb = inter::wedgeCodebook16Hgtw;
        else cb = inter::wedgeCodebook16Hltw;
        for (int w = 0; w < (1 << inter::getWedgeBitsLookup(bsize)); ++w) {
            const int bw = inter::kBlockSizeWide[bsize];
            const int bh = inter::kBlockSizeHigh[bsize];
            const int woff = (cb[w].xOffset * bw) >> 3;
            const int hoff = (cb[w].yOffset * bh) >> 3;
            const std::uint8_t* mask = obl + cb[w].direction * 64 * 64 + (32 - hoff) * 64 + (32 - woff);
            int avg = 0;
            for (int i = 0; i < bw; ++i) avg += mask[i];
            for (int i = 1; i < bh; ++i) avg += mask[i * 64]; // MASK_PRIMARY_STRIDE
            avg = (avg + (bw + bh - 1) / 2) / (bw + bh - 1);
            const int flip = avg < 32;
            if (flip != inter::wedgeSignflipLookup[bsize][w]) {
                ++signflipBad;
                MESSAGE("signflip mismatch bsize " << bsize << " w " << w << ": computed " << flip << " table "
                                                   << (int)inter::wedgeSignflipLookup[bsize][w]);
            }
        }
    }
    CHECK(signflipBad == 0);
    // (c) complement property over the contiguous windows: BLOCK_16X16.
    int compBad = 0;
    for (int w = 0; w < 16; ++w) {
        const std::uint8_t* m0 = inter::getContiguousSoftMask(w, 0, 6);
        const std::uint8_t* m1 = inter::getContiguousSoftMask(w, 1, 6);
        for (int i = 0; i < 256; ++i)
            if (m0[i] + m1[i] != 64) ++compBad;
        CHECK(m0 != m1);
    }
    CHECK(compBad == 0);
    // (d) VERTICAL window content: BLOCK_16X16, codebook heqw index 6 =
    // {WEDGE_VERTICAL, 2, 4} and index 7 = {WEDGE_VERTICAL, 6, 4}. The
    // vertical primary plane is the same row replicated; the window at
    // (woff, hoff) = ((x*16)>>3, (y*16)>>3) reads 16 rows of it.
    {
        const inter::WedgeCodeType* cb = inter::wedgeCodebook16Heqw;
        // The runtime window incorporates the table's signflip
        // (neg ^ wsignflip): a flipped entry's sign-0 mask is the
        // complement of the primary plane.
        for (int wi : {6, 7}) {
            const std::uint8_t* mask = inter::getContiguousSoftMask(wi, 0, 6);
            const int woff = (cb[wi].xOffset * 16) >> 3;
            const int flip = inter::wedgeSignflipLookup[6][wi];
            // The vertical plane is the 64-entry primary ROW replicated;
            // only the column offset applies.
            for (int r = 0; r < 16; ++r)
                for (int c = 0; c < 16; ++c) {
                    const int primary = inter::wedgePrimaryVertical[32 - woff + c];
                    CHECK(mask[r * 16 + c] == (flip ? 64 - primary : primary));
                }
        }
        // (d2) OBLIQUE63 window vs the independent closed-form row
        // reconstruction. The codebook index 1 = {WEDGE_OBLIQUE63, 4, 4}:
        // window at (woff, hoff) = (8, 8); window row r is PLANE row
        // (32 - hoff + r) = 24 + r, and plane row p is
        // shift(primary_even if p even, 16 - p/2) / shift(primary_odd,
        // 15 - (p-1)/2). flip 1 -> complement.
        const std::uint8_t* mask = inter::getContiguousSoftMask(1, 0, 6);
        auto shiftRow = [&](const std::uint8_t* primary, int shift, int c) {
            if (shift >= 0) return c < shift ? primary[0] : primary[c - shift];
            shift = -shift;
            return c >= 64 - shift ? primary[63] : primary[c + shift];
        };
        for (int r = 0; r < 16; ++r) {
            const int planeRow = 32 - 8 + r;
            const bool even = (planeRow % 2) == 0;
            const int shift = even ? (16 - planeRow / 2) : (15 - (planeRow - 1) / 2);
            const std::uint8_t* primary = even ? inter::wedgePrimaryObliqueEven : inter::wedgePrimaryObliqueOdd;
            for (int c = 0; c < 16; ++c) {
                const int p = shiftRow(primary, shift, 32 - 8 + c);
                CHECK(mask[r * 16 + c] == 64 - p); // flip 1 -> complement
            }
        }
    }
}
TEST_CASE("shift_copy replicates edges in both directions (ip:2053-2062)") {
    std::uint8_t src[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    std::uint8_t dst[8] = {0};
    inter::shiftCopy(src, dst, 3, 8);
    const std::uint8_t wantPos[8] = {10, 10, 10, 10, 20, 30, 40, 50};
    CHECK(std::memcmp(dst, wantPos, 8) == 0);
    std::memset(dst, 0, 8);
    inter::shiftCopy(src, dst, -2, 8);
    const std::uint8_t wantNeg[8] = {30, 40, 50, 60, 70, 80, 80, 80};
    CHECK(std::memcmp(dst, wantNeg, 8) == 0);
    std::memset(dst, 0, 8);
    inter::shiftCopy(src, dst, 0, 8);
    CHECK(std::memcmp(dst, src, 8) == 0);
}
// A3E J4 note: svt_inter_predictor_light_pd1 (ip:1288-:1395) and
// svt_highbd_inter_predictor (ip:1455-:1523) dispatch the svt_aom_convolve
// / svt_aom_convolveHbd function-pointer tables (:1115/:1099) as direct
// calls (the MC1 named deviation); ALL EIGHT slots of each table are now
// live - the four jnt slots resolve with J1/J3. The packed 10-bit buffer
// contract (PACKED_BUFFER_SIZE, definitions.h:344-:346; the 8-aligned
// stride, ALIGN_POWER_OF_TWO :996; INTERPOLATION_OFFSET = 8) is exercised
// through an injectable
// packer (the PO1-parked svt_aom_pack2d_src threading), so the
// offset/stride arithmetic is tested end to end.

namespace dispatchoracle {

// A real (in8 | inBit << 8) packer, the PO1-parked svt_aom_pack2d_src's
// arithmetic at the tested shapes.
void testPack2d(const std::uint8_t* in8, std::uint32_t in8Stride, const std::uint8_t* innBit, std::uint32_t innStride,
                std::uint16_t* out16, std::uint32_t outStride, std::uint32_t width, std::uint32_t height) {
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            out16[y * outStride + x] =
                (std::uint16_t)(in8[y * in8Stride + x] | ((std::uint16_t)innBit[y * innStride + x] << 8));
        }
    }
}

// Counts pack2d invocations (the plumbing probe).
int g_packCalls = 0;
void countingPack2d(const std::uint8_t* in8, std::uint32_t in8Stride, const std::uint8_t* innBit,
                    std::uint32_t innStride, std::uint16_t* out16, std::uint32_t outStride, std::uint32_t width,
                    std::uint32_t height) {
    ++g_packCalls;
    testPack2d(in8, in8Stride, innBit, innStride, out16, outStride, width, height);
}

} // namespace dispatchoracle

TEST_CASE("svt_highbd_inter_predictor dispatches all eight table slots (ip:1455-1523)") {
    using namespace dispatchoracle;
    const convolve::InterpFilterParams* reg = &convolve::interpFilterParamsList[(int)convolve::InterpFilter::EIGHTTAP_REGULAR];
    // Un-scaled arm: (subpelX, subpelY) select the slot; is_compound
    // selects the jnt column. subpels here are POST-revert (q4) values.
    const struct {
        int subpelX, subpelY, isCompound;
        const char* slot;
    } cases[] = {
        {0, 0, 0, "2d_copy_sr"}, {0, 4, 0, "y_sr"},  {4, 0, 0, "x_sr"}, {4, 4, 0, "2d_sr"},
        {0, 0, 1, "jnt_2d_copy"}, {0, 4, 1, "jnt_y"}, {4, 0, 1, "jnt_x"}, {4, 4, 1, "jnt_2d"},
    };
    for (const auto& cs : cases) {
        std::vector<std::uint16_t> plane16;
        const int stride = 8 + 16;
        hbdoracle::fillPlane16(plane16, stride, 8 + 16, 8, 0xD15A5Eu + (std::uint32_t)cs.subpelX * 3 + cs.subpelY);
        const std::uint16_t* src = plane16.data() + 8 * stride + 8;
        // The dispatcher's incoming subpels are Q10 (revert_scale_extra_bits
        // shifts >> SCALE_EXTRA_BITS = 6), so q4 4 arrives as 256.
        inter::SubpelParams sp{inter::kScaleSubpelShifts, inter::kScaleSubpelShifts, cs.subpelX * 64, cs.subpelY * 64};
        inter::ScaleFactors sf;
        inter::setupScaleFactorsForFrame(&sf, 8, 8, 8, 8); // identity, not scaled
        inter::ConvolveParams cp{};
        std::memset(&cp, 0, sizeof(cp));
        cp.round0 = convolve::filterBits;
        cp.round1 = convolve::filterBits;
        cp.isCompound = cs.isCompound;
        std::vector<std::uint16_t> staging((size_t)8 * 8, 0);
        cp.dst = staging.data();
        cp.dstStride = 8;
        std::vector<std::uint16_t> got((size_t)8 * 8, 0x999);
        inter::highbdInterPredictor(src, stride, got.data(), 8, &sp, &sf, 8, 8, &cp,
                                    convolve::makeInterpFilters(convolve::InterpFilter::EIGHTTAP_REGULAR,
                                                                convolve::InterpFilter::EIGHTTAP_REGULAR),
                                    0, 8);
        // The direct-call twin of the same slot, run by hand.
        inter::ConvolveParams cp2 = cp;
        std::vector<std::uint16_t> staging2((size_t)8 * 8, 0);
        cp2.dst = staging2.data();
        std::vector<std::uint16_t> want((size_t)8 * 8, 0x999);
        const std::uint16_t* s = src;
        if (std::string(cs.slot) == "2d_copy_sr") {
            inter::highbdConvolve2dCopySr(s, stride, want.data(), 8, 8, 8, reg, reg, 0, 0, &cp2, 8);
        } else if (std::string(cs.slot) == "y_sr") {
            inter::highbdConvolveYsr(s, stride, want.data(), 8, 8, 8, reg, reg, 0, cs.subpelY, &cp2, 8);
        } else if (std::string(cs.slot) == "x_sr") {
            inter::highbdConvolveXsr(s, stride, want.data(), 8, 8, 8, reg, reg, cs.subpelX, 0, &cp2, 8);
        } else if (std::string(cs.slot) == "2d_sr") {
            inter::highbdConvolve2dSr(s, stride, want.data(), 8, 8, 8, reg, reg, cs.subpelX, cs.subpelY, &cp2, 8);
        } else if (std::string(cs.slot) == "jnt_2d_copy") {
            inter::highbdJntConvolve2dCopy(s, stride, want.data(), 8, 8, 8, reg, reg, 0, 0, &cp2, 8);
        } else if (std::string(cs.slot) == "jnt_y") {
            inter::highbdJntConvolveY(s, stride, want.data(), 8, 8, 8, reg, reg, 0, cs.subpelY, &cp2, 8);
        } else if (std::string(cs.slot) == "jnt_x") {
            inter::highbdJntConvolveX(s, stride, want.data(), 8, 8, 8, reg, reg, cs.subpelX, 0, &cp2, 8);
        } else {
            inter::highbdJntConvolve2d(s, stride, want.data(), 8, 8, 8, reg, reg, cs.subpelX, cs.subpelY, &cp2, 8);
        }
        int bad = 0;
        const bool jntSlot = cs.isCompound != 0;
        for (int i = 0; i < 64; ++i) {
            // The jnt slots with do_average=0 write the staging CONV_BUF
            // (conv_params->dst), NOT the out param - compare staging for
            // those, the out param for the sr slots.
            const int g = jntSlot ? staging[(size_t)i] : got[(size_t)i];
            const int v = jntSlot ? staging2[(size_t)i] : want[(size_t)i];
            if (g != v) ++bad;
        }
        CHECK_MESSAGE(bad == 0, "dispatch slot mismatch");
        if (bad) MESSAGE("slot " << std::string(cs.slot) << " diffs " << bad);
    }
}

TEST_CASE("svt_inter_predictor_pd0 routes scaled to 2d_scale(REGULAR) and unscaled to the copy/jnt-copy slots (ip:1261-1286)") {
    using namespace dispatchoracle;
    const convolve::InterpFilterParams* reg = &convolve::interpFilterParamsList[(int)convolve::InterpFilter::EIGHTTAP_REGULAR];
    // Unscaled, single-ref: the [0][0][0] copy_sr slot.
    {
        std::vector<std::uint16_t> plane16;
        const int stride = 8 + 16;
        hbdoracle::fillPlane16(plane16, stride, 8 + 16, 8, 0x0D0Au);
        std::vector<std::uint8_t> plane8((size_t)stride * 24);
        for (size_t i = 0; i < plane8.size(); ++i) plane8[i] = (std::uint8_t)plane16[i];
        inter::SubpelParams sp{inter::kScaleSubpelShifts, inter::kScaleSubpelShifts, 0, 0};
        inter::ConvolveParams cp{};
        std::memset(&cp, 0, sizeof(cp));
        cp.round0 = convolve::filterBits;
        cp.round1 = convolve::filterBits;
        std::vector<std::uint8_t> got((size_t)8 * 8, 0x99);
        inter::interPredictorPd0(plane8.data() + 8 * stride + 8, stride, got.data(), 8, 8, 8, &sp, &cp);
        std::vector<std::uint8_t> want((size_t)8 * 8, 0x77);
        inter::convolve2dCopySr(plane8.data() + 8 * stride + 8, stride, want.data(), 8, 8, 8, reg, reg, 0, 0, &cp);
        int bad = 0;
        for (int i = 0; i < 64; ++i)
            if (got[i] != want[i]) ++bad;
        CHECK_MESSAGE(bad == 0, "pd0 unscaled copy slot");
        // is_compound=1 selects jnt_convolve_2d_copy.
        inter::ConvolveParams cpc{};
        std::memset(&cpc, 0, sizeof(cpc));
        cpc.round0 = convolve::filterBits;
        cpc.round1 = convolve::filterBits;
        cpc.isCompound = 1;
        std::vector<std::uint16_t> staging((size_t)8 * 8, 0);
        cpc.dst = staging.data();
        cpc.dstStride = 8;
        std::vector<std::uint8_t> gotC((size_t)8 * 8, 0x99);
        inter::interPredictorPd0(plane8.data() + 8 * stride + 8, stride, gotC.data(), 8, 8, 8, &sp, &cpc);
        std::vector<std::uint16_t> stagingW((size_t)8 * 8, 0);
        cpc.dst = stagingW.data();
        std::vector<std::uint8_t> wantC((size_t)8 * 8, 0x77);
        inter::jntConvolve2dCopy(plane8.data() + 8 * stride + 8, stride, wantC.data(), 8, 8, 8, reg, reg, 0, 0, &cpc);
        bad = 0;
        // do_average=0: the jnt-copy slot writes the staging CONV_BUF.
        for (int i = 0; i < 64; ++i)
            if (staging[i] != stagingW[i]) ++bad;
        CHECK_MESSAGE(bad == 0, "pd0 unscaled jnt-copy slot");
    }
    // Scaled arm: REGULAR filters through convolve_2d_scale; identity sf
    // (xs = ys = SCALE_SUBPEL_SHIFTS) must reproduce the unscaled pixels.
    {
        std::vector<std::uint8_t> plane8((size_t)24 * 24);
        std::uint32_t seed = 0x0D00u;
        for (size_t i = 0; i < plane8.size(); ++i) plane8[i] = (std::uint8_t)jntoracle::lcg(seed);
        inter::ScaleFactors sf;
        inter::setupScaleFactorsForFrame(&sf, 8, 8, 8, 8);
        inter::SubpelParams sp{inter::kScaleSubpelShifts, inter::kScaleSubpelShifts, 0, 0};
        inter::ConvolveParams cp{};
        std::memset(&cp, 0, sizeof(cp));
        cp.round0 = convolve::filterBits;
        cp.round1 = convolve::filterBits;
        std::vector<std::uint8_t> got((size_t)8 * 8, 0x99);
        inter::interPredictorPd0(plane8.data() + 8 * 24 + 8, 24, got.data(), 8, 8, 8, &sp, &cp);
        int bad = 0;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                if (got[(size_t)y * 8 + x] != plane8[(size_t)(8 + y) * 24 + 8 + x]) ++bad;
        CHECK_MESSAGE(bad == 0, "pd0 scaled identity arm");
        if (bad) MESSAGE("pd0 scaled identity diffs " << bad);
    }
}

TEST_CASE("svt_inter_predictor_light_pd1 packs the 10-bit reference and dispatches the hbd slots (ip:1288-1395)") {
    using namespace dispatchoracle;
    g_packCalls = 0;
    const convolve::InterpFilterParams* reg = &convolve::interpFilterParamsList[(int)convolve::InterpFilter::EIGHTTAP_REGULAR];
    // 8-bit arm (bd=8): the src_2b pointer is NULL, the plain convolve
    // table routes [sx!=0][sy!=0][is_compound].
    {
        std::vector<std::uint8_t> plane8((size_t)24 * 24);
        std::uint32_t seed = 0x1D1u;
        for (size_t i = 0; i < plane8.size(); ++i) plane8[i] = (std::uint8_t)jntoracle::lcg(seed);
        inter::SubpelParams sp{inter::kScaleSubpelShifts, inter::kScaleSubpelShifts, 4 * 64, 4 * 64};
        inter::ConvolveParams cp{};
        std::memset(&cp, 0, sizeof(cp));
        cp.round0 = convolve::filterBits;
        cp.round1 = convolve::filterBits;
        std::vector<std::uint8_t> got((size_t)8 * 8, 0x99);
        inter::interPredictorLightPd1(plane8.data() + 8 * 24 + 8, nullptr, 24, got.data(), 8, 8, 8,
                                      convolve::makeInterpFilters(convolve::InterpFilter::EIGHTTAP_REGULAR,
                                                                  convolve::InterpFilter::EIGHTTAP_REGULAR),
                                      &sp, &cp, 8, nullptr);
        std::vector<std::uint8_t> want((size_t)8 * 8, 0x77);
        inter::convolve2dSr(plane8.data() + 8 * 24 + 8, 24, want.data(), 8, 8, 8, reg, reg, 4, 4, &cp);
        int bad = 0;
        for (int i = 0; i < 64; ++i)
            if (got[i] != want[i]) ++bad;
        CHECK_MESSAGE(bad == 0, "light_pd1 8-bit 2d_sr slot");
    }
    // 10-bit arm: src_2b non-NULL triggers the pack path (probe counts 1)
    // and the hbd dispatch; the packed buffer feeds highbdJntConvolve2d.
    {
        std::vector<std::uint8_t> plane8((size_t)24 * 24);
        std::vector<std::uint8_t> plane2((size_t)24 * 24);
        std::uint32_t seed = 0x2D2u;
        for (size_t i = 0; i < plane8.size(); ++i) {
            plane8[i] = (std::uint8_t)jntoracle::lcg(seed);
            plane2[i] = (std::uint8_t)((jntoracle::lcg(seed)) & 3); // top 2 bits
        }
        inter::SubpelParams sp{inter::kScaleSubpelShifts, inter::kScaleSubpelShifts, 4 * 64, 4 * 64};
        inter::ConvolveParams cp{};
        std::memset(&cp, 0, sizeof(cp));
        cp.round0 = convolve::filterBits;
        cp.round1 = convolve::filterBits;
        cp.isCompound = 1;
        std::vector<std::uint16_t> staging((size_t)8 * 8, 0);
        cp.dst = staging.data();
        cp.dstStride = 8;
        std::vector<std::uint16_t> got((size_t)8 * 8, 0x999);
        inter::interPredictorLightPd1(plane8.data() + 8 * 24 + 8, plane2.data() + 8 * 24 + 8, 24, (std::uint8_t*)got.data(),
                                      8, 8, 8,
                                      convolve::makeInterpFilters(convolve::InterpFilter::EIGHTTAP_REGULAR,
                                                                  convolve::InterpFilter::EIGHTTAP_REGULAR),
                                      &sp, &cp, 10, &countingPack2d);
        CHECK(g_packCalls == 1);
        // The hand-run twin: pack the same window with the SAME offsets
        // (INTERPOLATION_OFFSET = 8 around the 8x8 block, stride aligned
        // to 8: 8*1 + 16 = 24 -> already 8-aligned) and run the slot.
        // src_stride16 = w*1 + 16 = 24.
        std::vector<std::uint16_t> packed((size_t)24 * 24, 0);
        // The kernel packs from src - offset - offset*stride over
        // (w + 16) x (h + 16), i.e. the full 24x24 window.
        testPack2d(plane8.data(), 24, plane2.data(), 24, packed.data(), 24, 24, 24);
        const std::uint16_t* src10 = packed.data() + 8 * 24 + 8;
        inter::ConvolveParams cp2 = cp;
        std::vector<std::uint16_t> staging2((size_t)8 * 8, 0);
        cp2.dst = staging2.data();
        std::vector<std::uint16_t> want((size_t)8 * 8, 0x999);
        inter::highbdJntConvolve2d(src10, 24, want.data(), 8, 8, 8, reg, reg, 4, 4, &cp2, 10);
        int bad = 0;
        for (int i = 0; i < 64; ++i)
            if (got[i] != want[i]) ++bad;
        CHECK_MESSAGE(bad == 0, "light_pd1 10-bit jnt_2d slot");
        if (bad) MESSAGE("light_pd1 10-bit diffs " << bad);
    }
}

TEST_CASE("jnt_convolve_2d_copy pins the shift-compose-copy path (ip:677-714)") {
    using namespace jntoracle;
    const convolve::InterpFilterParams* reg = &convolve::interpFilterParamsList[(int)convolve::InterpFilter::EIGHTTAP_REGULAR];
    const Cfg cfgs[] = {
        {8, 8, 4, 4, 7, 7, 9, 7, 1, 1},   {8, 8, 4, 4, 7, 7, 9, 7, 1, 0},  {8, 8, 4, 4, 7, 7, 0, 0, 0, 1},
        {13, 5, 1, 15, 7, 7, 12, 4, 1, 1}, {4, 8, 15, 1, 7, 7, 5, 11, 1, 0}, {5, 4, 2, 13, 4, 7, 13, 3, 1, 1},
    };
    for (const Cfg& c : cfgs) {
        std::vector<std::uint8_t> plane;
        const int stride = c.w + 16;
        fillPlane(plane, stride, c.h + 16, 0x5EEDu + (std::uint32_t)c.w * 3 + c.h);
        const std::uint8_t* src = plane.data() + 8 * stride + 8;
        std::vector<std::uint16_t> dst16Init((size_t)c.w * c.h);
        std::uint32_t dstSeed = 0xF00Du + (std::uint32_t)c.h;
        for (size_t i = 0; i < dst16Init.size(); ++i) dst16Init[i] = (std::uint16_t)(lcg(dstSeed) * 131u);
        inter::ConvolveParams cp{};
        std::memset(&cp, 0, sizeof(cp));
        cp.round0 = c.round0;
        cp.round1 = c.round1;
        cp.useJntCompAvg = c.useJnt;
        cp.fwdOffset = c.fwd;
        cp.bckOffset = c.bck;
        cp.doAverage = c.doAverage;
        cp.dst = dst16Init.data();
        cp.dstStride = c.w;
        std::vector<std::uint8_t> dst8Buf((size_t)c.w * c.h + 8, 0xAB);
        inter::jntConvolve2dCopy(src, stride, dst8Buf.data(), c.w, c.w, c.h, reg, reg, c.subpelX, c.subpelY, &cp);
        std::vector<std::uint8_t> want8;
        std::vector<std::uint16_t> want16;
        convCopy(stride, src, c, dst16Init, c.w, want8, c.doAverage ? nullptr : &want16);
        int bad = 0;
        for (int y = 0; y < c.h; ++y)
            for (int x = 0; x < c.w; ++x) {
                const int g = c.doAverage ? dst8Buf[(size_t)y * c.w + x] : dst16Init[(size_t)y * c.w + x];
                const int v = c.doAverage ? want8[(size_t)y * c.w + x] : want16[(size_t)y * c.w + x];
                if (g != v) {
                    ++bad;
                    if (bad <= 3)
                        MESSAGE("jnt_copy cfg (w,h,r0,r1,jnt,avg)=(" << c.w << "," << c.h << "," << c.round0 << ","
                                                                     << c.round1 << "," << c.useJnt << "," << c.doAverage
                                                                     << ") at (" << y << "," << x << "): got " << g
                                                                     << " want " << v);
                }
            }
        CHECK_MESSAGE(bad == 0, "jnt_copy mismatch");
    }
}

TEST_CASE("jnt_convolve_2d matches the independent compose oracle over the config band (ip:529-585)") {
    using namespace jntoracle;
    const convolve::InterpFilterParams* reg = &convolve::interpFilterParamsList[(int)convolve::InterpFilter::EIGHTTAP_REGULAR];
    const Cfg cfgs[] = {
        {8, 8, 4, 4, 7, 7, 9, 7, 1, 1},   {8, 8, 4, 4, 7, 7, 9, 7, 1, 0},
        {8, 8, 4, 4, 7, 7, 0, 0, 0, 1},   {13, 5, 1, 15, 7, 7, 12, 4, 1, 1},
        {4, 8, 15, 1, 7, 7, 5, 11, 1, 0}, {1, 3, 8, 8, 7, 7, 9, 7, 1, 1},
        {5, 4, 2, 13, 4, 7, 13, 3, 1, 1}, {8, 8, 11, 6, 4, 7, 7, 9, 0, 1},
    };
    for (const Cfg& c : cfgs) {
        std::vector<std::uint8_t> plane;
        const int stride = c.w + 16;
        fillPlane(plane, stride, c.h + 16, 0xBEEFu + (std::uint32_t)c.w * 7 + c.h);
        const std::uint8_t* src = plane.data() + 8 * stride + 8;
        std::vector<std::uint16_t> dst16Init((size_t)c.w * c.h);
        std::uint32_t dstSeed = 0xCAFEu + (std::uint32_t)c.h;
        for (size_t i = 0; i < dst16Init.size(); ++i) dst16Init[i] = (std::uint16_t)(lcg(dstSeed) * 131u);
        inter::ConvolveParams cp{};
        std::memset(&cp, 0, sizeof(cp));
        cp.round0 = c.round0;
        cp.round1 = c.round1;
        cp.useJntCompAvg = c.useJnt;
        cp.fwdOffset = c.fwd;
        cp.bckOffset = c.bck;
        cp.doAverage = c.doAverage;
        cp.dst = dst16Init.data();
        cp.dstStride = c.w;
        std::vector<std::uint8_t> got8;
        std::vector<std::uint16_t> got16;
        std::vector<std::uint8_t> dst8Buf((size_t)c.w * c.h + 8, 0xAB);
        inter::jntConvolve2d(src, stride, dst8Buf.data(), c.w, c.w, c.h, reg, reg, c.subpelX, c.subpelY, &cp);
        if (c.doAverage) {
            got8.assign(dst8Buf.begin(), dst8Buf.begin() + c.w * c.h);
        } else {
            got16.assign(dst16Init.begin(), dst16Init.begin() + c.w * c.h);
        }
        std::vector<std::uint8_t> want8;
        std::vector<std::uint16_t> want16;
        conv2d(stride, src, c, reg, reg, dst16Init, c.w, want8, c.doAverage ? nullptr : &want16);
        int bad = 0;
        for (int y = 0; y < c.h; ++y)
            for (int x = 0; x < c.w; ++x) {
                if (c.doAverage) {
                    if (got8[(size_t)y * c.w + x] != want8[(size_t)y * c.w + x]) ++bad;
                } else if (got16[(size_t)y * c.w + x] != want16[(size_t)y * c.w + x]) {
                    ++bad;
                }
            }
        CHECK_MESSAGE(bad == 0, "jnt_2d mismatch cfg");
        if (bad) {
            for (int y = 0; y < c.h; ++y)
                for (int x = 0; x < c.w; ++x) {
                    const int g = c.doAverage ? got8[(size_t)y * c.w + x] : got16[(size_t)y * c.w + x];
                    const int v = c.doAverage ? want8[(size_t)y * c.w + x] : want16[(size_t)y * c.w + x];
                    if (g != v) MESSAGE("cfg (w,h,sp,r0,r1,jnt,avg)=(" << c.w << "," << c.h << "," << c.subpelX << ","
                                                                       << c.round0 << "," << c.round1 << "," << c.useJnt
                                                                       << "," << c.doAverage << ") at (" << y << "," << x
                                                                       << "): got " << g << " want " << v);
                }
        }
    }
}


