// l14_utility - the utility.c kernel surface (A13, U1): the block
// geometry builder + md scan + struct-free helpers.
// Reference: third_party/SVT-AV1/Source/Lib/Codec/utility.c.
// LANDED so far: GeomIndex, BlockSize, BlockGeom, the offset/size
// tables, getNumNsPerPart, mdScanAllBlks, countTotalNumOfActiveBlks,
// buildBlkGeom, the coded/mini-GOP stats getters, getPlaneBlockSize
// (+ ssSizeLookup, the builder's bsize_uv dependency), assertErr.
// Parked: svt_fixed_point_test_breakpoint (utility.c:415-419, behind
// FIXED_POINT_ASSERT_TEST which is 0 under NDEBUG per utility.h:232-238).
#pragma once

#include <cstdint>

namespace l14_utility {

// GeomIndex - utility.h:30-43 verbatim (the block-geometry configs
// svt_aom_build_blk_geom admits).
enum GeomIndex {
    GEOM_0,  // 64x64  -> 16x16  NSQ: OFF
    GEOM_1,  // 64x64  -> 16x16  NSQ: ON (only H & V shapes, but not 16x8 and 8x16)
    GEOM_2,  // 64x64  -> 8x8    NSQ: OFF
    GEOM_3,  // 64x64  -> 8x8    NSQ: ON (only H & V shapes, but not 8x4 and 4x8 and not 16x8 and 8x16)
    GEOM_4,  // 64x64  -> 8x8    NSQ: ON (only H & V shapes, but not 8x4 and 4x8)
    GEOM_5,  // 64x64  -> 8x8    NSQ: ON (only H & V shapes)
    GEOM_6,  // 64x64  -> 4x4    NSQ: ON (only H & V shapes)
    GEOM_7,  // 64x64  -> 4x4    NSQ: ON (only H, V, H4, V4 shapes)
    GEOM_8,  // 64x64  -> 4x4    NSQ: ON (all shapes)
    GEOM_9,  // 128x128 -> 4x4   NSQ: ON (all shapes)
    GEOM_10, // 128x128 -> 8x8   NSQ: ON (only H, V, H4, V4 shapes)
    GEOM_TOT
};

// BlockSize - definitions.h:882-909 verbatim order + the BLOCK_SIZES /
// BLOCK_INVALID / BLOCK_LARGEST aliases. Deviation: l0_core carries a
// project-minimal 2-value BlockSize; this layer mirrors the full SVT
// enum because hvsize_to_bsize (utility.c:189-195) and
// svt_aom_ss_size_lookup (common_utils.c:239-263) index by it and
// BLOCK_INVALID (definitions.h:907 = 255) is a carried value.
enum BlockSize {
    BLOCK_4X4,
    BLOCK_4X8,
    BLOCK_8X4,
    BLOCK_8X8,
    BLOCK_8X16,
    BLOCK_16X8,
    BLOCK_16X16,
    BLOCK_16X32,
    BLOCK_32X16,
    BLOCK_32X32,
    BLOCK_32X64,
    BLOCK_64X32,
    BLOCK_64X64,
    BLOCK_64X128,
    BLOCK_128X64,
    BLOCK_128X128,
    BLOCK_4X16,
    BLOCK_16X4,
    BLOCK_8X32,
    BLOCK_32X8,
    BLOCK_16X64,
    BLOCK_64X16,
    BLOCK_SIZES_ALL,
    BLOCK_SIZES   = BLOCK_4X16,
    BLOCK_INVALID = 255,
    BLOCK_LARGEST = (BLOCK_SIZES - 1)
};

// svt_aom_assert_err - utility.c:27-32 verbatim shape: assert the
// condition, then report. Deviations: SVT_ERROR (svt_log.h, the Globals
// log facility) becomes fprintf(stderr, "\n %s \n", ...); err_msg is
// const char* so C++ string literals bind (the reference takes char*).
void assertErr(std::uint32_t condition, const char* err_msg);

// BlockGeom - utility.h:45-62: the field set the builder writes, in the
// reference's order and widths (the mds_idx field keeps the reference's
// #if _DEBUG guard - the Release build omits it, as the reference does).
struct BlockGeom {
    std::uint8_t sq_size;  // size of parent square

    std::uint8_t bwidth;    // block width
    std::uint8_t bheight;   // block height
    std::uint8_t bwidth_uv;   // block width for Chroma 4:2:0
    std::uint8_t bheight_uv;  // block height for Chroma 4:2:0
    BlockSize bsize;          // bloc size
    BlockSize bsize_uv;       // bloc size for Chroma 4:2:0

    std::uint16_t d1_depth_offset;  // offset to the next d1 sq block
    std::uint16_t ns_depth_offset;  // offset to the next nsq block (skip remaining d2 blocks)
#if _DEBUG
    std::uint32_t mds_idx;
#endif
};

// ns_quarter_size_mult - utility.c:172-187 verbatim: [part][h+v][nsq]
// quarter-size multipliers; the row order is P=0,1,2,7,8,3,4,5,6 (the
// comment rows of the reference), so part index 3 is H4 and 4 is V4 and
// the 9 entries mean "not used".
extern std::uint32_t nsQuarterSizeMult[9][2][4];

// hvsize_to_bsize - utility.c:189-195 verbatim: [log2(width)-2][log2(height)-2].
extern BlockSize hvsizeToBsize[6][6];

// get_num_ns_per_part - utility.c:197-200: blocks a partition shape
// emits (NONE 1, H/V 2, H4/V4 4 below 128, HA/HB/VA/VB 3).
std::uint32_t getNumNsPerPart(std::uint32_t part_it, std::uint32_t sq_size);

// ns_depth_offset - utility.c:203-213 verbatim: [geom][depth], the
// md-scan span of a square at that depth (offset to the next subtree);
// NOT_USED_VALUE marks depths the geom never reaches.
extern const std::uint32_t nsDepthOffset[GEOM_TOT][6];

// d1_depth_offset - utility.c:215-225 verbatim: [geom][depth], the
// offset from a parent square to its first quadrant child.
extern const std::uint32_t d1DepthOffset[GEOM_TOT][6];

// get_plane_block_size - common_utils.h:135-140 via the
// svt_aom_ss_size_lookup table (common_utils.c:239-263), the dependency
// the builder needs for bsize_uv (utility.c:271).
extern const BlockSize ssSizeLookup[BLOCK_SIZES_ALL][2][2];
BlockSize getPlaneBlockSize(BlockSize bsize, int subsampling_x, int subsampling_y);

// count_total_num_of_active_blks - utility.c:291-327: the block count
// the builder's md-scan admits for (min_nsq_bsize, max_sb, max_depth,
// max_part); build_blk_geom checks it against the geom's documented
// max_block_count.
std::uint32_t countTotalNumOfActiveBlks(std::uint8_t min_nsq_bsize, std::uint32_t max_sb, std::uint32_t max_depth,
                                        std::uint32_t max_part);

// md_scan_all_blks - utility.c:227-289: the md-scan walk. File-local
// static in the reference; exported here (statics exported precedent)
// so the scan-order contract is testable directly.
void mdScanAllBlks(GeomIndex geom, BlockGeom* blk_geom, std::uint32_t* idx_mds, std::uint32_t sq_size,
                   std::uint32_t x, std::uint32_t y, std::uint8_t min_nsq_bsize, std::uint32_t max_sb,
                   std::uint32_t max_depth, std::uint32_t max_part);

// svt_aom_build_blk_geom - utility.c:332-413: fill BlockGeom for every
// md-scan index of the geom. Deviation: the mismatch report SVT_LOG
// becomes fprintf(stderr) (the assertErr logging deviation).
void buildBlkGeom(GeomIndex geom, BlockGeom* blk_geom);

// CodedBlockStats - utility.h:71-79 verbatim.
struct CodedBlockStats {
    std::uint8_t depth;
    std::uint8_t size;
    std::uint8_t size_log2;
    std::uint8_t org_x;
    std::uint8_t org_y;
    std::uint8_t cu_num_in_depth;
    std::uint8_t parent32x32_index;
};

// svt_aom_get_coded_blk_stats - utility.c:126-128 (the table is
// utility.c:34-121).
const CodedBlockStats* getCodedBlkStats(std::uint32_t cu_idx);

// MiniGopStats - utility.h:174-179 verbatim.
struct MiniGopStats {
    std::uint8_t hierarchical_levels;
    std::uint8_t start_index;
    std::uint8_t end_index;
    std::uint8_t length;
};

// svt_aom_get_mini_gop_stats - utility.c:168-170 (the table is
// utility.c:130-163).
const MiniGopStats* getMiniGopStats(std::uint32_t mini_gop_index);

// get_msb, portable #else body (definitions.h:628-:644): returns
// (int32_t)floor(log2(n)). n must be > 0. Declared here (MU2 refactor:
// was static in utility.cpp) so mathutil.h's svtLog2fSafe shares it.
int getMsb(std::uint32_t n);

// NOT_USED_VALUE - utility.h:95 (#define 0); marks unused offset columns.
constexpr std::uint32_t kNotUsedValue = 0;

}  // namespace l14_utility
