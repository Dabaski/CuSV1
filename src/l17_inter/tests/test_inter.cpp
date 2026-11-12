#include <doctest.h>

#include <cstdint>
#include <cstring>
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

