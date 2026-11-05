#include <cstring>
#include <doctest.h>
#include <utility.h>

TEST_CASE("l14_utility scaffold boots") {
    CHECK(true);
}

TEST_CASE("GEOM_TOT is 11 geoms") {
    CHECK(l14_utility::GEOM_TOT == 11);
}

TEST_CASE("kNotUsedValue is 0") {
    CHECK(l14_utility::kNotUsedValue == 0);
}

TEST_CASE("BlockSize mirror carries BLOCK_INVALID 255") {
    CHECK(l14_utility::BLOCK_INVALID == 255);
    CHECK(l14_utility::BLOCK_128X128 == 15);
}

TEST_CASE("assertErr with a true condition returns") {
    l14_utility::assertErr(1, "unreachable");
    CHECK(true);
}

TEST_CASE("BlockGeom carries the builder-written fields") {
    l14_utility::BlockGeom g{};
    g.sq_size = 64;
    g.bwidth = 32;
    g.bheight = 16;
    g.bsize = l14_utility::BLOCK_32X16;
    g.bsize_uv = l14_utility::BLOCK_16X8;
    g.d1_depth_offset = 5;
    g.ns_depth_offset = 209;
    CHECK(g.ns_depth_offset == 209);
}

TEST_CASE("nsQuarterSizeMult carries the H4 row at part index 3") {
    CHECK(l14_utility::nsQuarterSizeMult[3][0][0] == 4);
    CHECK(l14_utility::nsQuarterSizeMult[3][1][0] == 1);
}

TEST_CASE("nsDepthOffset: a GEOM_2 32x32 subtree spans 21 blocks") {
    CHECK(l14_utility::nsDepthOffset[l14_utility::GEOM_2][1] == 21);
}

TEST_CASE("d1DepthOffset: a GEOM_1 32x32 emits 5 blocks before its first child") {
    CHECK(l14_utility::d1DepthOffset[l14_utility::GEOM_1][1] == 5);
}

TEST_CASE("getPlaneBlockSize 4:2:0 halves 8x8 to 4x4") {
    CHECK(l14_utility::getPlaneBlockSize(l14_utility::BLOCK_8X8, 1, 1) == l14_utility::BLOCK_4X4);
}

TEST_CASE("countTotalNumOfActiveBlks counts GEOM_0 as 21") {
    CHECK(l14_utility::countTotalNumOfActiveBlks(16, 64, 3, 1) == 21);
}

TEST_CASE("mdScanAllBlks walks GEOM_0 in the documented depth-first order") {
    // GEOM_0: 64 -> 32x4 -> 16x16, max_part 1, min_nsq 16: 21 blocks. The
    // md-scan emits each square's blocks then recurses into the four
    // quadrants in (x,y), (x+h,y), (x,y+h), (x+h,y+h) order
    // (utility.c:280-288), so the sq_size sequence is
    // 64 | 32 16 16 16 16 | 32 16 16 16 16 | 32 16 16 16 16 | 32 16 16 16 16.
    l14_utility::BlockGeom arr[21] = {};
    std::uint32_t idx = 0;
    l14_utility::mdScanAllBlks(l14_utility::GEOM_0, arr, &idx, 64, 0, 0, 16, 64, 3, 1);
    CHECK(idx == 21);
    const std::uint32_t expected[21] = {64, 32, 16, 16, 16, 16, 32, 16, 16, 16, 16,
                                        32, 16, 16, 16, 16, 32, 16, 16, 16, 16};
    for (std::uint32_t i = 0; i < 21; ++i) {
        CHECK(arr[i].sq_size == expected[i]);
    }
}

TEST_CASE("buildBlkGeom fills GEOM_0 idx 0 as the root 64x64") {
    l14_utility::BlockGeom arr[21] = {};
    l14_utility::buildBlkGeom(l14_utility::GEOM_0, arr);
    CHECK(arr[0].bwidth == 64);
    CHECK(arr[0].bheight == 64);
    CHECK(arr[0].bsize == l14_utility::BLOCK_64X64);
    CHECK(arr[0].bsize_uv == l14_utility::BLOCK_32X32);
    CHECK(arr[0].bwidth_uv == 32);
    CHECK(arr[0].d1_depth_offset == 1);
    CHECK(arr[0].ns_depth_offset == 21);
}

TEST_CASE("buildBlkGeom GEOM_1 idx 6 is a 32x16 H block with 4:2:0 halving") {
    // GEOM_1 md-scan: idx 0 = 64x64, idx 1-2 = 64x32 (H), idx 3-4 = 32x64
    // (V), idx 5 = the (0,0) 32x32, idx 6-7 = its H halves.
    l14_utility::BlockGeom arr[41] = {};
    l14_utility::buildBlkGeom(l14_utility::GEOM_1, arr);
    CHECK(arr[6].bwidth == 32);
    CHECK(arr[6].bheight == 16);
    CHECK(arr[6].bsize == l14_utility::BLOCK_32X16);
    CHECK(arr[6].bsize_uv == l14_utility::BLOCK_16X8);
    CHECK(arr[6].bwidth_uv == 16);
    CHECK(arr[6].bheight_uv == 8);
}

TEST_CASE("buildBlkGeom GEOM_2 walk matches the coded-unit stats table over all 85 blocks") {
    // coded_unit_stats_array (utility.c:34-121) IS the GEOM_2 md-scan
    // enumeration: 85 entries, max_sb 64, max_depth 4, max_part 1,
    // min_nsq 16. Cross-check the builder's bwidth/bheight against the
    // table's size column and the walk depth (64>>d) against the table's
    // depth column, for EVERY index.
    l14_utility::BlockGeom arr[85] = {};
    l14_utility::buildBlkGeom(l14_utility::GEOM_2, arr);
    for (std::uint32_t i = 0; i < 85; ++i) {
        const l14_utility::CodedBlockStats* st = l14_utility::getCodedBlkStats(i);
        CHECK(arr[i].bwidth == st->size);
        CHECK(arr[i].bheight == st->size);
        std::uint32_t walk_depth = 0;
        while ((64u >> walk_depth) != arr[i].sq_size) {
            ++walk_depth;
        }
        CHECK(walk_depth == st->depth);
    }
}

namespace {

unsigned minOf(unsigned a, unsigned b) {
    return a < b ? a : b;
}

// Independent derivation of the offset semantics (utility.c:203-225)
// from the part rules (utility.c:197-200, :236-244): N = the blocks a
// square at sq emits; spans = the square's md-scan subtree span =
// N + 4 * spans(child depth).
unsigned nBlocks(unsigned sq, unsigned max_part, unsigned min_nsq) {
    unsigned mpu = sq == 128 ? minOf(max_part, (max_part < 9u && max_part > 3u ? 3u : 7u))
                   : sq == 8 ? minOf(max_part, 3u)
                   : sq == 4 ? 1u
                             : max_part;
    if (sq <= min_nsq) {
        mpu = 1;
    }
    unsigned n = 0;
    for (unsigned p = 0; p < mpu; ++p) {
        n += p < 1 ? 1 : p < 3 ? 2 : (p < 5 && sq < 128) ? 4 : 3;
    }
    return n;
}

unsigned spans(unsigned d, unsigned max_sb, unsigned max_depth, unsigned max_part, unsigned min_nsq) {
    const unsigned sq = max_sb >> d;
    const unsigned n = nBlocks(sq, max_part, min_nsq);
    if (d + 1 >= max_depth) {
        return n;
    }
    return n + 4 * spans(d + 1, max_sb, max_depth, max_part, min_nsq);
}

}  // namespace

TEST_CASE("offset tables equal the derived subtree spans for every geom and depth") {
    struct GeomParams {
        unsigned min_nsq;
        unsigned max_sb;
        unsigned max_depth;
        unsigned max_part;
    };
    const GeomParams params[11] = {
        {16, 64, 3, 1},  {16, 64, 3, 3},  {16, 64, 4, 1},  {16, 64, 4, 3},  {8, 64, 4, 3},
        {0, 64, 4, 3},   {0, 64, 5, 3},   {0, 64, 5, 5},   {0, 64, 5, 9},   {0, 128, 6, 9},
        {0, 128, 5, 5},
    };
    for (int g = 0; g < 11; ++g) {
        const GeomParams& gp = params[g];
        for (unsigned d = 0; d < gp.max_depth; ++d) {
            const unsigned sq = gp.max_sb >> d;
            CHECK(l14_utility::d1DepthOffset[g][d] == nBlocks(sq, gp.max_part, gp.min_nsq));
            CHECK(l14_utility::nsDepthOffset[g][d] ==
                  spans(d, gp.max_sb, gp.max_depth, gp.max_part, gp.min_nsq));
        }
    }
}

TEST_CASE("buildBlkGeom GEOM_9 depth-0 prefix applies the 128 part-index shift") {
    // 128x128 skips H4/V4 (part_it >= 3 indexes HA/HB/VA/VB via the +2
    // shift, utility.c:259-261). Depth-0 emits 17 blocks: 128x128, H
    // 128x64 x2, V 64x128 x2, then the HA/HB/VA/VB triples.
    l14_utility::BlockGeom arr[4421] = {};
    l14_utility::buildBlkGeom(l14_utility::GEOM_9, arr);
    CHECK(arr[0].bwidth == 128);
    CHECK(arr[0].bheight == 128);
    CHECK(arr[0].bsize == l14_utility::BLOCK_128X128);
    CHECK(arr[0].bsize_uv == l14_utility::BLOCK_64X64);
    const std::uint8_t expW[17] = {128, 128, 128, 64, 64,  64,  64,  128, 128,
                                   64,  64,  64,  64, 64,  64,  64,  64};
    const std::uint8_t expH[17] = {128, 64, 64, 128, 128, 64, 64, 64,  64,
                                   64,  64, 64, 64,  128, 128, 64, 64};
    for (std::uint32_t i = 0; i < 17; ++i) {
        CHECK(arr[i].bwidth == expW[i]);
        CHECK(arr[i].bheight == expH[i]);
    }
}

TEST_CASE("buildBlkGeom GEOM_8 depth-0 emits all nine shapes in order") {
    // 64x64, max_part 9: NONE, H, V, H4, V4, HA, HB, VA, VB = 25 blocks
    // (= d1DepthOffset[GEOM_8][0]). Widths/heights derived from
    // nsQuarterSizeMult rows at quart = 16 (utility.c:177-187).
    l14_utility::BlockGeom arr[1101] = {};
    l14_utility::buildBlkGeom(l14_utility::GEOM_8, arr);
    const std::uint8_t expW[25] = {64, 64, 64, 32, 32, 64, 64, 64, 64, 16, 16, 16, 16,
                                   32, 32, 64, 64, 32, 32, 32, 32, 32, 32, 32, 32};
    const std::uint8_t expH[25] = {64, 32, 32, 64, 64, 16, 16, 16, 16, 64, 64, 64, 64,
                                   32, 32, 32, 32, 32, 32, 32, 32, 64, 64, 32, 32};
    for (std::uint32_t i = 0; i < 25; ++i) {
        CHECK(arr[i].bwidth == expW[i]);
        CHECK(arr[i].bheight == expH[i]);
    }
}

TEST_CASE("miniGopStats entries satisfy length == end-start+1 == 1<<levels") {
    // mini_gop_stats_array (utility.c:130-163), 31 entries. The
    // hierarchical bisection invariant, checked for EVERY entry.
    for (std::uint32_t i = 0; i < 31; ++i) {
        const l14_utility::MiniGopStats* st = l14_utility::getMiniGopStats(i);
        CHECK(st->length == st->end_index - st->start_index + 1);
        CHECK(st->length == (1u << st->hierarchical_levels));
    }
}

TEST_CASE("getCodedBlkStats corners match the reference rows") {
    // Row 0 ({0, 64, 6, 0, 0, 0, 0}) and row 84 ({3, 8, 3, 56, 56, 63, 4}),
    // utility.c:36 and :120.
    const l14_utility::CodedBlockStats* first = l14_utility::getCodedBlkStats(0);
    CHECK(first->depth == 0);
    CHECK(first->size == 64);
    CHECK(first->size_log2 == 6);
    CHECK(first->org_x == 0);
    CHECK(first->org_y == 0);
    CHECK(first->cu_num_in_depth == 0);
    CHECK(first->parent32x32_index == 0);
    const l14_utility::CodedBlockStats* last = l14_utility::getCodedBlkStats(84);
    CHECK(last->depth == 3);
    CHECK(last->size == 8);
    CHECK(last->size_log2 == 3);
    CHECK(last->org_x == 56);
    CHECK(last->org_y == 56);
    CHECK(last->cu_num_in_depth == 63);
    CHECK(last->parent32x32_index == 4);
}

TEST_CASE("buildBlkGeom is idempotent (two builds are byte-identical)") {
    l14_utility::BlockGeom a[4421] = {};
    l14_utility::BlockGeom b[4421] = {};
    l14_utility::buildBlkGeom(l14_utility::GEOM_9, a);
    l14_utility::buildBlkGeom(l14_utility::GEOM_9, b);
    CHECK(memcmp(a, b, sizeof(a)) == 0);
    l14_utility::buildBlkGeom(l14_utility::GEOM_8, a);
    l14_utility::buildBlkGeom(l14_utility::GEOM_8, b);
    CHECK(memcmp(a, b, 1101 * sizeof(l14_utility::BlockGeom)) == 0);
}

TEST_CASE("buildBlkGeom GEOM_8 clamps chroma dims at 4 for 8x4 and 4x8") {
    // GEOM_8 md-scan: the (0,0) 8x8 square sits at idx 75 (its subtree =
    // 25 + 4*25 + 4*25 = 225 root-side blocks before it: 25 + 25 + 25 =
    // 75). idx 76-77 = its H halves (8x4), idx 78-79 = its V halves
    // (4x8). bwidth_uv/bheight_uv clamp at MAX(4, dim>>1)
    // (utility.c:268-269); 8x4 -> BLOCK_8X4 whose 4:2:0 lookup is
    // BLOCK_4X4 (common_utils.c:244).
    l14_utility::BlockGeom arr[1101] = {};
    l14_utility::buildBlkGeom(l14_utility::GEOM_8, arr);
    CHECK(arr[76].bwidth == 8);
    CHECK(arr[76].bheight == 4);
    CHECK(arr[76].bsize == l14_utility::BLOCK_8X4);
    CHECK(arr[76].bsize_uv == l14_utility::BLOCK_4X4);
    CHECK(arr[76].bwidth_uv == 4);
    CHECK(arr[76].bheight_uv == 4);
    CHECK(arr[78].bwidth == 4);
    CHECK(arr[78].bheight == 8);
    CHECK(arr[78].bsize == l14_utility::BLOCK_4X8);
    // the first 4x4 leaf: the (0,0) 8x8 square spans idx 75-79 (5 blocks)
    // and its four 4x4 children start at idx 80.
    CHECK(arr[80].bwidth == 4);
    CHECK(arr[80].bheight == 4);
    CHECK(arr[80].bsize == l14_utility::BLOCK_4X4);
    CHECK(arr[80].bwidth_uv == 4);
    CHECK(arr[80].bheight_uv == 4);
}

TEST_CASE("countTotalNumOfActiveBlks matches every geom's documented max_block_count") {
    // (min_nsq_bsize, max_sb, max_depth, max_part, max_block_count) per
    // geom - utility.c:338-404, the reference's own parameter table.
    struct GeomParams {
        int geom;
        unsigned min_nsq;
        unsigned max_sb;
        unsigned max_depth;
        unsigned max_part;
        unsigned count;
    };
    const GeomParams params[11] = {
        {0, 16, 64, 3, 1, 21},    {1, 16, 64, 3, 3, 41},    {2, 16, 64, 4, 1, 85},
        {3, 16, 64, 4, 3, 105},   {4, 8, 64, 4, 3, 169},    {5, 0, 64, 4, 3, 425},
        {6, 0, 64, 5, 3, 681},    {7, 0, 64, 5, 5, 849},    {8, 0, 64, 5, 9, 1101},
        {9, 0, 128, 6, 9, 4421},  {10, 0, 128, 5, 5, 2377},
    };
    for (const GeomParams& gp : params) {
        CHECK(l14_utility::countTotalNumOfActiveBlks((std::uint8_t)gp.min_nsq, gp.max_sb, gp.max_depth, gp.max_part) ==
              gp.count);
    }
}

TEST_CASE("hvsizeToBsize corners are 4x4 and 128x128") {
    CHECK(l14_utility::hvsizeToBsize[5][5] == l14_utility::BLOCK_128X128);
    CHECK(l14_utility::hvsizeToBsize[0][0] == l14_utility::BLOCK_4X4);
}

TEST_CASE("getNumNsPerPart: PARTITION_NONE is one block") {
    CHECK(l14_utility::getNumNsPerPart(0, 64) == 1);
}

TEST_CASE("getNumNsPerPart over the full admitted domain") {
    // part_it 0..8 (the ns_quarter_size_mult rows), every sq_size the
    // builder admits (4..128). Expected: p<1 -> 1; p<3 -> 2; p<5 && sq<128
    // -> 4; else 3 (utility.c:198).
    for (std::uint32_t p = 0; p < 9; ++p) {
        const std::uint32_t sizes[6] = {4, 8, 16, 32, 64, 128};
        for (std::uint32_t si = 0; si < 6; ++si) {
            const std::uint32_t sq = sizes[si];
            const std::uint32_t expected = p < 1 ? 1 : p < 3 ? 2 : (p < 5 && sq < 128) ? 4 : 3;
            CHECK(l14_utility::getNumNsPerPart(p, sq) == expected);
        }
    }
}
