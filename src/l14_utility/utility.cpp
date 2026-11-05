#include "utility.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace l14_utility {

// ns_quarter_size_mult (utility.c:172-187) verbatim.
std::uint32_t nsQuarterSizeMult[9][2][4] = {
    // 9 means not used.

    //          |   h   |     |   v   |

    /*P=0*/ {{4, 9, 9, 9}, {4, 9, 9, 9}},
    /*P=1*/ {{4, 4, 9, 9}, {2, 2, 9, 9}},
    /*P=2*/ {{2, 2, 9, 9}, {4, 4, 9, 9}},

    /*P=7*/ {{4, 4, 4, 4}, {1, 1, 1, 1}},
    /*P=8*/ {{1, 1, 1, 1}, {4, 4, 4, 4}},

    /*P=3*/ {{2, 2, 4, 9}, {2, 2, 2, 9}},
    /*P=4*/ {{4, 2, 2, 9}, {2, 2, 2, 9}},
    /*P=5*/ {{2, 2, 2, 9}, {2, 2, 4, 9}},
    /*P=6*/ {{2, 2, 2, 9}, {4, 2, 2, 9}}};

// hvsize_to_bsize (utility.c:189-195) verbatim.
BlockSize hvsizeToBsize[/*H*/ 6][/*V*/ 6] = {
    {BLOCK_4X4, BLOCK_4X8, BLOCK_4X16, BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID},
    {BLOCK_8X4, BLOCK_8X8, BLOCK_8X16, BLOCK_8X32, BLOCK_INVALID, BLOCK_INVALID},
    {BLOCK_16X4, BLOCK_16X8, BLOCK_16X16, BLOCK_16X32, BLOCK_16X64, BLOCK_INVALID},
    {BLOCK_INVALID, BLOCK_32X8, BLOCK_32X16, BLOCK_32X32, BLOCK_32X64, BLOCK_INVALID},
    {BLOCK_INVALID, BLOCK_INVALID, BLOCK_64X16, BLOCK_64X32, BLOCK_64X64, BLOCK_64X128},
    {BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, BLOCK_INVALID, BLOCK_128X64, BLOCK_128X128}};

// get_num_ns_per_part (utility.c:197-200) verbatim.
std::uint32_t getNumNsPerPart(std::uint32_t part_it, std::uint32_t sq_size) {
    std::uint32_t tot_num_ns_per_part = part_it < 1 ? 1 : part_it < 3 ? 2 : part_it < 5 && sq_size < 128 ? 4 : 3;
    return tot_num_ns_per_part;
}

// ns_depth_offset (utility.c:203-213) verbatim.
const std::uint32_t nsDepthOffset[GEOM_TOT][6] = {{21, 5, 1, 1, kNotUsedValue, kNotUsedValue},
                                                  {41, 9, 1, 1, kNotUsedValue, kNotUsedValue},
                                                  {85, 21, 5, 1, kNotUsedValue, kNotUsedValue},
                                                  {105, 25, 5, 1, kNotUsedValue, kNotUsedValue},
                                                  {169, 41, 9, 1, kNotUsedValue, kNotUsedValue},
                                                  {425, 105, 25, 5, kNotUsedValue, kNotUsedValue},
                                                  {681, 169, 41, 9, 1, kNotUsedValue},
                                                  {849, 209, 49, 9, 1, kNotUsedValue},
                                                  {1101, 269, 61, 9, 1, kNotUsedValue},
                                                  {4421, 1101, 269, 61, 9, 1},
                                                  {2377, 593, 145, 33, 5, kNotUsedValue}};
// d1_depth_offset (utility.c:215-225) verbatim.
const std::uint32_t d1DepthOffset[GEOM_TOT][6] = {{1, 1, 1, 1, 1, kNotUsedValue},
                                                  {5, 5, 1, 1, 1, kNotUsedValue},
                                                  {1, 1, 1, 1, 1, kNotUsedValue},
                                                  {5, 5, 1, 1, 1, kNotUsedValue},
                                                  {5, 5, 5, 1, 1, kNotUsedValue},
                                                  {5, 5, 5, 5, 1, kNotUsedValue},
                                                  {5, 5, 5, 5, 1, kNotUsedValue},
                                                  {13, 13, 13, 5, 1, kNotUsedValue},
                                                  {25, 25, 25, 5, 1, kNotUsedValue},
                                                  {17, 25, 25, 25, 5, 1},
                                                  {5, 13, 13, 13, 5, kNotUsedValue}};

// svt_aom_ss_size_lookup (common_utils.c:239-263) verbatim.
const BlockSize ssSizeLookup[BLOCK_SIZES_ALL][2][2] = {
    //  ss_x == 0    ss_x == 0        ss_x == 1      ss_x == 1
    //  ss_y == 0    ss_y == 1        ss_y == 0      ss_y == 1
    {{BLOCK_4X4, BLOCK_4X4}, {BLOCK_4X4, BLOCK_4X4}},
    {{BLOCK_4X8, BLOCK_4X4}, {BLOCK_INVALID, BLOCK_4X4}},
    {{BLOCK_8X4, BLOCK_INVALID}, {BLOCK_4X4, BLOCK_4X4}},
    {{BLOCK_8X8, BLOCK_8X4}, {BLOCK_4X8, BLOCK_4X4}},
    {{BLOCK_8X16, BLOCK_8X8}, {BLOCK_INVALID, BLOCK_4X8}},
    {{BLOCK_16X8, BLOCK_INVALID}, {BLOCK_8X8, BLOCK_8X4}},
    {{BLOCK_16X16, BLOCK_16X8}, {BLOCK_8X16, BLOCK_8X8}},
    {{BLOCK_16X32, BLOCK_16X16}, {BLOCK_INVALID, BLOCK_8X16}},
    {{BLOCK_32X16, BLOCK_INVALID}, {BLOCK_16X16, BLOCK_16X8}},
    {{BLOCK_32X32, BLOCK_32X16}, {BLOCK_16X32, BLOCK_16X16}},
    {{BLOCK_32X64, BLOCK_32X32}, {BLOCK_INVALID, BLOCK_16X32}},
    {{BLOCK_64X32, BLOCK_INVALID}, {BLOCK_32X32, BLOCK_32X16}},
    {{BLOCK_64X64, BLOCK_64X32}, {BLOCK_32X64, BLOCK_32X32}},
    {{BLOCK_64X128, BLOCK_64X64}, {BLOCK_INVALID, BLOCK_32X64}},
    {{BLOCK_128X64, BLOCK_INVALID}, {BLOCK_64X64, BLOCK_64X32}},
    {{BLOCK_128X128, BLOCK_128X64}, {BLOCK_64X128, BLOCK_64X64}},
    {{BLOCK_4X16, BLOCK_4X8}, {BLOCK_INVALID, BLOCK_4X8}},
    {{BLOCK_16X4, BLOCK_INVALID}, {BLOCK_8X4, BLOCK_8X4}},
    {{BLOCK_8X32, BLOCK_8X16}, {BLOCK_INVALID, BLOCK_4X16}},
    {{BLOCK_32X8, BLOCK_INVALID}, {BLOCK_16X8, BLOCK_16X4}},
    {{BLOCK_16X64, BLOCK_16X32}, {BLOCK_INVALID, BLOCK_8X32}},
    {{BLOCK_64X16, BLOCK_INVALID}, {BLOCK_32X16, BLOCK_32X8}}};

// get_plane_block_size (common_utils.h:135-140) verbatim.
BlockSize getPlaneBlockSize(BlockSize bsize, int subsampling_x, int subsampling_y) {
    if (bsize == BLOCK_INVALID) {
        return BLOCK_INVALID;
    }
    return ssSizeLookup[bsize][subsampling_x][subsampling_y];
}

// get_msb, portable #else body (definitions.h:628-644): returns
// (int32_t)floor(log2(n)). n must be > 0. svt_log2f aliases this
// (definitions.h:592).
static inline int getMsb(std::uint32_t n) {
    int log = 0;
    std::uint32_t value = n;
    int i;

    for (i = 4; i >= 0; --i) {
        const int shift = (1 << i);
        const std::uint32_t x = value >> shift;
        if (x != 0) {
            value = x;
            log += shift;
        }
    }
    return log;
}

// count_total_num_of_active_blks (utility.c:291-327) verbatim; the MIN
// macro becomes std::min (utility.h:99).
std::uint32_t countTotalNumOfActiveBlks(std::uint8_t min_nsq_bsize, std::uint32_t max_sb, std::uint32_t max_depth,
                                        std::uint32_t max_part) {
    std::uint32_t depth_it, sq_it_y, sq_it_x, part_it, nsq_it;

    std::uint32_t depth_scan_idx = 0;

    for (depth_it = 0; depth_it < max_depth; depth_it++) {
        std::uint32_t tot_num_sq = 1 << depth_it;
        std::uint32_t sq_size = depth_it == 0 ? max_sb
                                : depth_it == 1 ? max_sb / 2
                                : depth_it == 2 ? max_sb / 4
                                : depth_it == 3 ? max_sb / 8
                                : depth_it == 4 ? max_sb / 16
                                                : max_sb / 32;

        std::uint32_t max_part_updated = sq_size == 128 ? std::min(max_part, (std::uint32_t)(max_part < 9 && max_part > 3 ? 3 : 7))
                                         : sq_size == 8 ? std::min(max_part, 3u)
                                         : sq_size == 4 ? 1
                                                        : max_part;
        if (sq_size <= min_nsq_bsize) {
            max_part_updated = 1;
        }
        for (sq_it_y = 0; sq_it_y < tot_num_sq; sq_it_y++) {
            for (sq_it_x = 0; sq_it_x < tot_num_sq; sq_it_x++) {
                for (part_it = 0; part_it < max_part_updated; part_it++) {
                    std::uint32_t tot_num_ns_per_part = getNumNsPerPart(part_it, sq_size);

                    for (nsq_it = 0; nsq_it < tot_num_ns_per_part; nsq_it++) {
                        depth_scan_idx++;
                    }
                }
            }
        }
    }

    return depth_scan_idx;
}

// md_scan_all_blks (utility.c:227-289) verbatim; MIN/MAX macros become
// std::min/std::max (utility.h:98-99).
void mdScanAllBlks(GeomIndex geom, BlockGeom* blk_geom, std::uint32_t* idx_mds, std::uint32_t sq_size, std::uint32_t x,
                   std::uint32_t y, std::uint8_t min_nsq_bsize, std::uint32_t max_sb, std::uint32_t max_depth,
                   std::uint32_t max_part) {
    // the input block is the parent square block of size sq_size located at pos (x,y)
    std::uint32_t part_it, nsq_it;

    std::uint32_t halfsize = sq_size / 2;
    std::uint32_t quartsize = sq_size / 4;

    std::uint32_t max_part_updated =
        sq_size == 128
            ? std::min(max_part, (std::uint32_t)(max_part < 9 && max_part > 3 ? 3 : 7))
            : sq_size == 8 ? std::min(max_part, 3u)
                           :

                           sq_size == 4 ? 1 : max_part;
    if (sq_size <= min_nsq_bsize) {
        max_part_updated = 1;
    }

    for (part_it = 0; part_it < max_part_updated; part_it++) {
        std::uint32_t tot_num_ns_per_part = getNumNsPerPart(part_it, sq_size);

        for (nsq_it = 0; nsq_it < tot_num_ns_per_part; nsq_it++) {
            std::uint8_t depth = sq_size == max_sb / 1 ? 0
                                  : sq_size == max_sb / 2 ? 1
                                  : sq_size == max_sb / 4 ? 2
                                  : sq_size == max_sb / 8 ? 3
                                  : sq_size == max_sb / 16 ? 4
                                                           : 5;

            blk_geom[*idx_mds].sq_size = (std::uint8_t)sq_size;

            // part_it >= 3 for 128x128 blocks corresponds to HA/HB/VA/VB shapes since H4/V4 are not allowed
            // for 128x128 blocks.  Therefore, need to offset part_it by 2 to not index H4/V4 shapes.
            std::uint32_t part_it_idx = part_it >= 3 && sq_size == 128 ? part_it + 2 : part_it;
            blk_geom[*idx_mds].d1_depth_offset = (std::uint16_t)d1DepthOffset[geom][depth];
            blk_geom[*idx_mds].ns_depth_offset = (std::uint16_t)nsDepthOffset[geom][depth];
            blk_geom[*idx_mds].bwidth = (std::uint8_t)(quartsize * nsQuarterSizeMult[part_it_idx][0][nsq_it]);
            blk_geom[*idx_mds].bheight = (std::uint8_t)(quartsize * nsQuarterSizeMult[part_it_idx][1][nsq_it]);
            blk_geom[*idx_mds].bsize =
                hvsizeToBsize[getMsb(blk_geom[*idx_mds].bwidth) - 2][getMsb(blk_geom[*idx_mds].bheight) - 2];
            blk_geom[*idx_mds].bwidth_uv = (std::uint8_t)std::max(4, (int)(blk_geom[*idx_mds].bwidth >> 1));
            blk_geom[*idx_mds].bheight_uv = (std::uint8_t)std::max(4, (int)(blk_geom[*idx_mds].bheight >> 1));

            blk_geom[*idx_mds].bsize_uv = getPlaneBlockSize(blk_geom[*idx_mds].bsize, 1, 1);
#if _DEBUG
            blk_geom[*idx_mds].mds_idx = (*idx_mds);
#endif
            (*idx_mds) = (*idx_mds) + 1;
        }
    }

    std::uint32_t min_size = max_sb >> (max_depth - 1);
    if (halfsize >= min_size) {
        mdScanAllBlks(geom, blk_geom, idx_mds, halfsize, x, y, min_nsq_bsize, max_sb, max_depth, max_part);
        mdScanAllBlks(
            geom, blk_geom, idx_mds, halfsize, x + halfsize, y, min_nsq_bsize, max_sb, max_depth, max_part);
        mdScanAllBlks(
            geom, blk_geom, idx_mds, halfsize, x, y + halfsize, min_nsq_bsize, max_sb, max_depth, max_part);
        mdScanAllBlks(
            geom, blk_geom, idx_mds, halfsize, x + halfsize, y + halfsize, min_nsq_bsize, max_sb, max_depth, max_part);
    }
}

// svt_aom_build_blk_geom (utility.c:332-413) verbatim; the SVT_LOG
// mismatch report becomes fprintf(stderr) (the assertErr deviation).
void buildBlkGeom(GeomIndex geom, BlockGeom* blk_geom) {
    std::uint32_t max_sb;
    std::uint32_t max_depth;
    std::uint32_t max_part;
    std::uint32_t max_block_count;
    std::uint32_t min_nsq_bsize;
    if (geom == GEOM_0) {
        max_sb = 64;
        max_depth = 3;
        max_part = 1;
        max_block_count = 21;
        min_nsq_bsize = 16;
    } else if (geom == GEOM_1) {
        max_sb = 64;
        max_depth = 3;
        max_part = 3;
        max_block_count = 41;
        min_nsq_bsize = 16;
    } else if (geom == GEOM_2) {
        max_sb = 64;
        max_depth = 4;
        max_part = 1;
        max_block_count = 85;
        min_nsq_bsize = 16;
    } else if (geom == GEOM_3) {
        max_sb = 64;
        max_depth = 4;
        max_part = 3;
        max_block_count = 105;
        min_nsq_bsize = 16;
    } else if (geom == GEOM_4) {
        max_sb = 64;
        max_depth = 4;
        max_part = 3;
        max_block_count = 169;
        min_nsq_bsize = 8;
    } else if (geom == GEOM_5) {
        max_sb = 64;
        max_depth = 4;
        max_part = 3;
        max_block_count = 425;
        min_nsq_bsize = 0;
    } else if (geom == GEOM_6) {
        max_sb = 64;
        max_depth = 5;
        max_part = 3;
        max_block_count = 681;
        min_nsq_bsize = 0;
    } else if (geom == GEOM_7) {
        max_sb = 64;
        max_depth = 5;
        max_part = 5;
        max_block_count = 849;
        min_nsq_bsize = 0;
    } else if (geom == GEOM_8) {
        max_sb = 64;
        max_depth = 5;
        max_part = 9;
        max_block_count = 1101;
        min_nsq_bsize = 0;
    } else if (geom == GEOM_9) {
        max_sb = 128;
        max_depth = 6;
        max_part = 9;
        max_block_count = 4421;
        min_nsq_bsize = 0;
    } else {
        max_sb = 128;
        max_depth = 5;
        max_part = 5;
        max_block_count = 2377;
        min_nsq_bsize = 0;
    }
    //(0)compute total number of blocks using the information provided
    std::uint32_t max_num_active_blocks =
        countTotalNumOfActiveBlks((std::uint8_t)min_nsq_bsize, max_sb, max_depth, max_part);
    if (max_num_active_blocks != max_block_count) {
        fprintf(stderr, " \n\n Error %i blocks\n\n ", max_num_active_blocks);
    }
    //(2) Construct md scan blk_geom_mds:  use info from dps
    std::uint32_t idx_mds = 0;
    mdScanAllBlks(geom, blk_geom, &idx_mds, max_sb, 0, 0, (std::uint8_t)min_nsq_bsize, max_sb, max_depth, max_part);
}

// coded_unit_stats_array (utility.c:34-121) verbatim.
static CodedBlockStats codedUnitStatsArray[] = {
    //   Depth       Size      SizeLog2     OriginX    OriginY   cu_num_in_depth   Index
    {0, 64, 6, 0, 0, 0, 0},  // 0
    {1, 32, 5, 0, 0, 0, 1},  // 1
    {2, 16, 4, 0, 0, 0, 1},  // 2
    {3, 8, 3, 0, 0, 0, 1},   // 3
    {3, 8, 3, 8, 0, 1, 1},   // 4
    {3, 8, 3, 0, 8, 8, 1},   // 5
    {3, 8, 3, 8, 8, 9, 1},   // 6
    {2, 16, 4, 16, 0, 1, 1}, // 7
    {3, 8, 3, 16, 0, 2, 1},  // 8
    {3, 8, 3, 24, 0, 3, 1},  // 9
    {3, 8, 3, 16, 8, 10, 1}, // 10
    {3, 8, 3, 24, 8, 11, 1}, // 11
    {2, 16, 4, 0, 16, 4, 1}, // 12
    {3, 8, 3, 0, 16, 16, 1}, // 13
    {3, 8, 3, 8, 16, 17, 1}, // 14
    {3, 8, 3, 0, 24, 24, 1}, // 15
    {3, 8, 3, 8, 24, 25, 1}, // 16
    {2, 16, 4, 16, 16, 5, 1},// 17
    {3, 8, 3, 16, 16, 18, 1},// 18
    {3, 8, 3, 24, 16, 19, 1},// 19
    {3, 8, 3, 16, 24, 26, 1},// 20
    {3, 8, 3, 24, 24, 27, 1},// 21
    {1, 32, 5, 32, 0, 1, 2}, // 22
    {2, 16, 4, 32, 0, 2, 2}, // 23
    {3, 8, 3, 32, 0, 4, 2},  // 24
    {3, 8, 3, 40, 0, 5, 2},  // 25
    {3, 8, 3, 32, 8, 12, 2}, // 26
    {3, 8, 3, 40, 8, 13, 2}, // 27
    {2, 16, 4, 48, 0, 3, 2}, // 28
    {3, 8, 3, 48, 0, 6, 2},  // 29
    {3, 8, 3, 56, 0, 7, 2},  // 30
    {3, 8, 3, 48, 8, 14, 2}, // 31
    {3, 8, 3, 56, 8, 15, 2}, // 32
    {2, 16, 4, 32, 16, 6, 2},// 33
    {3, 8, 3, 32, 16, 20, 2},// 34
    {3, 8, 3, 40, 16, 21, 2},// 35
    {3, 8, 3, 32, 24, 28, 2},// 36
    {3, 8, 3, 40, 24, 29, 2},// 37
    {2, 16, 4, 48, 16, 7, 2},// 38
    {3, 8, 3, 48, 16, 22, 2},// 39
    {3, 8, 3, 56, 16, 23, 2},// 40
    {3, 8, 3, 48, 24, 30, 2},// 41
    {3, 8, 3, 56, 24, 31, 2},// 42
    {1, 32, 5, 0, 32, 2, 3}, // 43
    {2, 16, 4, 0, 32, 8, 3}, // 44
    {3, 8, 3, 0, 32, 32, 3}, // 45
    {3, 8, 3, 8, 32, 33, 3}, // 46
    {3, 8, 3, 0, 40, 40, 3}, // 47
    {3, 8, 3, 8, 40, 41, 3}, // 48
    {2, 16, 4, 16, 32, 9, 3},// 49
    {3, 8, 3, 16, 32, 34, 3},// 50
    {3, 8, 3, 24, 32, 35, 3},// 51
    {3, 8, 3, 16, 40, 42, 3},// 52
    {3, 8, 3, 24, 40, 43, 3},// 53
    {2, 16, 4, 0, 48, 12, 3},// 54
    {3, 8, 3, 0, 48, 48, 3}, // 55
    {3, 8, 3, 8, 48, 49, 3}, // 56
    {3, 8, 3, 0, 56, 56, 3}, // 57
    {3, 8, 3, 8, 56, 57, 3}, // 58
    {2, 16, 4, 16, 48, 13, 3},// 59
    {3, 8, 3, 16, 48, 50, 3},// 60
    {3, 8, 3, 24, 48, 51, 3},// 61
    {3, 8, 3, 16, 56, 58, 3},// 62
    {3, 8, 3, 24, 56, 59, 3},// 63
    {1, 32, 5, 32, 32, 3, 4},// 64
    {2, 16, 4, 32, 32, 10, 4},// 65
    {3, 8, 3, 32, 32, 36, 4},// 66
    {3, 8, 3, 40, 32, 37, 4},// 67
    {3, 8, 3, 32, 40, 44, 4},// 68
    {3, 8, 3, 40, 40, 45, 4},// 69
    {2, 16, 4, 48, 32, 11, 4},// 70
    {3, 8, 3, 48, 32, 38, 4},// 71
    {3, 8, 3, 56, 32, 39, 4},// 72
    {3, 8, 3, 48, 40, 46, 4},// 73
    {3, 8, 3, 56, 40, 47, 4},// 74
    {2, 16, 4, 32, 48, 14, 4},// 75
    {3, 8, 3, 32, 48, 52, 4},// 76
    {3, 8, 3, 40, 48, 53, 4},// 77
    {3, 8, 3, 32, 56, 60, 4},// 78
    {3, 8, 3, 40, 56, 61, 4},// 79
    {2, 16, 4, 48, 48, 15, 4},// 80
    {3, 8, 3, 48, 48, 54, 4},// 81
    {3, 8, 3, 56, 48, 55, 4},// 82
    {3, 8, 3, 48, 56, 62, 4},// 83
    {3, 8, 3, 56, 56, 63, 4} // 84
};

// svt_aom_get_coded_blk_stats (utility.c:126-128) verbatim.
const CodedBlockStats* getCodedBlkStats(std::uint32_t cu_idx) {
    return &codedUnitStatsArray[cu_idx];
}

// mini_gop_stats_array (utility.c:130-163) verbatim.
static const MiniGopStats miniGopStatsArray[] = {
    // hierarchical_levels    start_index    end_index    Length
    {5, 0, 31, 32},  // 0
    {4, 0, 15, 16},  // 1
    {3, 0, 7, 8},    // 2
    {2, 0, 3, 4},    // 3
    {1, 0, 1, 2},    // 4
    {1, 2, 3, 2},    // 5
    {2, 4, 7, 4},    // 6
    {1, 4, 5, 2},    // 7
    {1, 6, 7, 2},    // 8
    {3, 8, 15, 8},   // 9
    {2, 8, 11, 4},   // 10
    {1, 8, 9, 2},    // 11
    {1, 10, 11, 2},  // 12
    {2, 12, 15, 4},  // 13
    {1, 12, 13, 2},  // 14
    {1, 14, 15, 2},  // 15
    {4, 16, 31, 16}, // 16
    {3, 16, 23, 8},  // 17
    {2, 16, 19, 4},  // 18
    {1, 16, 17, 2},  // 19
    {1, 18, 19, 2},  // 20
    {2, 20, 23, 4},  // 21
    {1, 20, 21, 2},  // 22
    {1, 22, 23, 2},  // 23
    {3, 24, 31, 8},  // 24
    {2, 24, 27, 4},  // 25
    {1, 24, 25, 2},  // 26
    {1, 26, 27, 2},  // 27
    {2, 28, 31, 4},  // 28
    {1, 28, 29, 2},  // 29
    {1, 30, 31, 2}   // 30
};

// svt_aom_get_mini_gop_stats (utility.c:168-170) verbatim.
const MiniGopStats* getMiniGopStats(std::uint32_t mini_gop_index) {
    return &miniGopStatsArray[mini_gop_index];
}

// svt_aom_assert_err (utility.c:27-32): assert(condition), then the
// SVT_ERROR report - here fprintf(stderr, "\n %s \n", err_msg). In a
// NDEBUG build the assert compiles away and the report remains, exactly
// as in the reference.
void assertErr(std::uint32_t condition, const char* err_msg) {
    assert(condition);
    if (!condition) {
        fprintf(stderr, "\n %s \n", err_msg);
    }
}

}  // namespace l14_utility
