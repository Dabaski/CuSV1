// A6C tests: the coding-loop pure helpers. Independent oracles throughout:
// the neighbour-array writes are checked against test-side transcriptions
// of the reference rules (which row/column lands where), the sort helpers
// against an insertion-sort oracle, the stage counts against a hand run
// of the reference formula, the MV helpers against the threshold bands,
// and the lambda chain against hand-computed reference values.
#include <doctest.h>
#include <cstdint>
#include <cstring>
#include <vector>

#include <coding_loop_helpers.h>
#include <picops.h>

namespace picops = pipeline::picops;

namespace cl = pipeline;
namespace amvp = motion::amvp;

// ---------------------------------------------------------------------------
// Test helper: allocate the NA storage from the port's own sizing rule
// (neighbor_arrays.c:32-:40) - the test owns the buffers, the ctor sets
// the sizes.
// ---------------------------------------------------------------------------
namespace {

struct NaBuffers {
    std::vector<std::uint8_t> left;
    std::vector<std::uint8_t> top;
    std::vector<std::uint8_t> top_left;
};

cl::NeighborArrayUnit MakeNa(NaBuffers& b, std::uint32_t max_w, std::uint32_t max_h, std::uint32_t unit_size,
                             std::uint8_t granularity_normal, std::uint8_t type_mask) {
    cl::NeighborArrayUnit na;
    cl::neighborArrayUnitCtor(&na, max_w, max_h, unit_size, granularity_normal, type_mask);
    b.left.assign(na.left_array_size * unit_size, 0xAA);
    b.top.assign(na.top_array_size * unit_size, 0xAA);
    b.top_left.assign(na.top_left_array_size * unit_size, 0xAA);
    na.left_array = b.left.empty() ? nullptr : b.left.data();
    na.top_array = b.top.empty() ? nullptr : b.top.data();
    na.top_left_array = b.top_left.empty() ? nullptr : b.top_left.data();
    return na;
}

}  // namespace

// trip 1: the NA ctor sizing arithmetic (neighbor_arrays.c:25-:52) - the
// granularity log2, the three size formulas and the mask gating.
TEST_CASE("l6 cl NA ctor sizes the three arrays from the mask") {
    cl::NeighborArrayUnit na;
    cl::neighborArrayUnitCtor(&na, 64, 48, 1, 4, cl::kNaFullMask);
    CHECK(na.unit_size == 1);
    CHECK(na.granularity_log2 == 2);  // svt_log2f(4)
    CHECK(na.left_array_size == 48 >> 2);
    CHECK(na.top_array_size == 64 >> 2);
    CHECK(na.top_left_array_size == (64 + 48) >> 2);

    cl::NeighborArrayUnit sample;
    cl::neighborArrayUnitCtor(&sample, 64, 48, 1, 1, cl::kNaFullMask);
    CHECK(sample.granularity_log2 == 0);
    CHECK(sample.left_array_size == 48);
    CHECK(sample.top_array_size == 64);
    CHECK(sample.top_left_array_size == 112);

    cl::NeighborArrayUnit partial;
    cl::neighborArrayUnitCtor(&partial, 64, 48, 1, 4, cl::kNaTopAndLeftOnlyMask);
    CHECK(partial.left_array_size == 12);
    CHECK(partial.top_array_size == 16);
    CHECK(partial.top_left_array_size == 0);
}

// trip 2: the reset fill (neighbor_arrays.c:54-:68): all three arrays
// receive NEIGHBOR_ARRAY_INVALID = ~0.
TEST_CASE("l6 cl NA reset fills with 0xFF and keeps the unit_size stride") {
    NaBuffers b;
    cl::NeighborArrayUnit na = MakeNa(b, 32, 32, 1, 4, cl::kNaFullMask);
    na.left_array[0] = 1;
    na.top_array[3] = 2;
    na.top_left_array[15] = 3;
    cl::neighborArrayUnitReset(&na);
    CHECK(na.left_array[0] == 0xFF);
    CHECK(na.top_array[3] == 0xFF);
    CHECK(na.top_left_array[15] == 0xFF);
    CHECK(na.left_array[7] == 0xFF);
}

// trip 3: the sample write (neighbor_arrays.h:132-:191), 8-bit. The rules:
// TOP = the SOURCE BOTTOM row at pic_origin_x; LEFT = the source right
// column gathered down pic_origin_y; TOPLEFT = the bottom row into the
// ring at (x, y+bh-1) forward + the right column into the ring at
// (x+bw-1, y) BACKWARD. The ring offset is left_array_size + (x>>g) -
// (y>>g). Oracle: an independent test-side transcription.
TEST_CASE("l6 cl sampleWrite 8bit lands the rows and columns at the reference offsets") {
    NaBuffers b;
    cl::NeighborArrayUnit na = MakeNa(b, 64, 64, 1, 1, cl::kNaFullMask);
    std::uint8_t src[12 * 12];
    for (int i = 0; i < 12 * 12; ++i) src[i] = static_cast<std::uint8_t>(i * 3 + 1);

    const std::uint32_t sx = 4, sy = 4, bw = 5, bh = 3;
    const std::uint32_t px = 9, py = 11;
    cl::neighborArrayUnitSampleWrite(&na, src, 12, sx, sy, px, py, bw, bh, cl::kNaFullMask);

    // Independent oracle: restate the three rules on the source window.
    const std::uint8_t* win = src + sy * 12 + sx;
    for (std::uint32_t i = 0; i < bw; ++i) {
        CHECK(na.top_array[px + i] == win[(bh - 1) * 12 + i]);
    }
    for (std::uint32_t i = 0; i < bh; ++i) {
        CHECK(na.left_array[py + i] == win[i * 12 + (bw - 1)]);
    }
    const std::uint32_t ring_off_bot = na.left_array_size + px - (py + (bh - 1));
    for (std::uint32_t i = 0; i < bw - 1; ++i) {
        CHECK(na.top_left_array[ring_off_bot + i] == win[(bh - 1) * 12 + i]);
    }
    const std::uint32_t ring_off_col = na.left_array_size + (px + (bw - 1)) - py;
    for (std::uint32_t i = 0; i < bh; ++i) {
        CHECK(na.top_left_array[ring_off_col - i] == win[i * 12 + (bw - 1)]);
    }
    // The overlap cell (the ring's bottom-right corner) is written by the
    // bottom-row memcpy FIRST and overwritten by the backward column walk
    // LAST, so it carries left_col[bh-1].
    CHECK(na.top_left_array[ring_off_bot + (bw - 1)] == win[(bh - 1) * 12 + (bw - 1)]);
    // Outside the written extent the arrays keep the preset.
    CHECK(na.top_array[px + bw] == 0xAA);
    CHECK(na.left_array[py + bh] == 0xAA);
}

// trip 4: the mask gating of the sample write: with TOP only, the left
// array and the ring stay preset.
TEST_CASE("l6 cl sampleWrite honors the mask per arm") {
    NaBuffers b;
    cl::NeighborArrayUnit na = MakeNa(b, 32, 32, 1, 1, cl::kNaTopMask);
    std::uint8_t src[6 * 6];
    for (int i = 0; i < 36; ++i) src[i] = static_cast<std::uint8_t>(i + 40);
    cl::neighborArrayUnitSampleWrite(&na, src, 6, 1, 1, 3, 5, 4, 3, cl::kNaTopMask);
    const std::uint8_t* win = src + 1 * 6 + 1;
    for (std::uint32_t i = 0; i < 4; ++i) {
        CHECK(na.top_array[3 + i] == win[2 * 6 + i]);
    }
    CHECK(na.top_array[10] == 0xAA);
    CHECK(na.left_array == nullptr);
    CHECK(na.top_left_array == nullptr);
}

// trip 5: the 16-bit sample write twin (neighbor_arrays.h:329-:373): the
// unit_size 2 walk, the same three rules in uint16 elements.
TEST_CASE("l6 cl sampleWrite 16bit twin matches the uint16 element rules") {
    NaBuffers b;
    cl::NeighborArrayUnit na = MakeNa(b, 64, 64, 2, 1, cl::kNaFullMask);
    std::vector<std::uint16_t> src(10 * 10);
    for (int i = 0; i < 10 * 10; ++i) src[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(i * 7 + 3);

    const std::uint32_t sx = 2, sy = 2, bw = 4, bh = 3;
    const std::uint32_t px = 6, py = 5;
    cl::neighborArrayUnit16bitSampleWrite(&na, src.data(), 10, sx, sy, px, py, bw, bh, cl::kNaFullMask);

    const std::uint16_t* top16 = reinterpret_cast<const std::uint16_t*>(na.top_array);
    const std::uint16_t* left16 = reinterpret_cast<const std::uint16_t*>(na.left_array);
    const std::uint16_t* ring16 = reinterpret_cast<const std::uint16_t*>(na.top_left_array);
    const std::uint16_t* win = src.data() + sy * 10 + sx;
    for (std::uint32_t i = 0; i < bw; ++i) {
        CHECK(top16[px + i] == win[(bh - 1) * 10 + i]);
    }
    for (std::uint32_t i = 0; i < bh; ++i) {
        CHECK(left16[py + i] == win[i * 10 + (bw - 1)]);
    }
    const std::uint32_t ring_off_bot = na.left_array_size + px - (py + (bh - 1));
    for (std::uint32_t i = 0; i < bw; ++i) {
        CHECK(ring16[ring_off_bot + i] == win[(bh - 1) * 10 + i]);
    }
}

// trip 6: update_recon_neighbor_array (neighbor_arrays.h:210-:232): top =
// src_top, left = src_left, ring bottom row = src_top, ring right column
// = src_left backward.
TEST_CASE("l6 cl updateReconNeighborArray writes top left and both ring arms") {
    NaBuffers b;
    cl::NeighborArrayUnit na = MakeNa(b, 32, 32, 1, 1, cl::kNaFullMask);
    const std::uint8_t top_row[6] = {1, 2, 3, 4, 5, 6};
    const std::uint8_t left_col[5] = {11, 12, 13, 14, 15};
    const std::uint32_t px = 4, py = 7, bw = 6, bh = 5;
    cl::updateReconNeighborArray(&na, top_row, left_col, px, py, bw, bh);

    for (std::uint32_t i = 0; i < bw; ++i) CHECK(na.top_array[px + i] == top_row[i]);
    for (std::uint32_t i = 0; i < bh; ++i) CHECK(na.left_array[py + i] == left_col[i]);
    const std::uint32_t ring_bot = na.left_array_size + px - (py + (bh - 1));
    for (std::uint32_t i = 0; i < bw - 1; ++i) CHECK(na.top_left_array[ring_bot + i] == top_row[i]);
    const std::uint32_t ring_col = na.left_array_size + (px + (bw - 1)) - py;
    for (std::uint32_t i = 0; i < bh; ++i) CHECK(na.top_left_array[ring_col - i] == left_col[i]);
    CHECK(na.top_left_array[ring_bot + (bw - 1)] == left_col[bh - 1]);  // the overlap cell
}

// trip 7: the 16-bit recon twin (neighbor_arrays.h:238-:258).
TEST_CASE("l6 cl updateReconNeighborArray16bit mirrors the uint16 rules") {
    NaBuffers b;
    cl::NeighborArrayUnit na = MakeNa(b, 32, 32, 2, 1, cl::kNaFullMask);
    const std::uint16_t top_row[6] = {1, 2, 3, 4, 5, 6};
    const std::uint16_t left_col[5] = {11, 12, 13, 14, 15};
    const std::uint32_t px = 4, py = 7, bw = 6, bh = 5;
    cl::updateReconNeighborArray16bit(&na, top_row, left_col, px, py, bw, bh);

    const std::uint16_t* top16 = reinterpret_cast<const std::uint16_t*>(na.top_array);
    const std::uint16_t* left16 = reinterpret_cast<const std::uint16_t*>(na.left_array);
    const std::uint16_t* ring16 = reinterpret_cast<const std::uint16_t*>(na.top_left_array);
    for (std::uint32_t i = 0; i < bw; ++i) CHECK(top16[px + i] == top_row[i]);
    for (std::uint32_t i = 0; i < bh; ++i) CHECK(left16[py + i] == left_col[i]);
    const std::uint32_t ring_bot = na.left_array_size + px - (py + (bh - 1));
    for (std::uint32_t i = 0; i < bw - 1; ++i) CHECK(ring16[ring_bot + i] == top_row[i]);
    const std::uint32_t ring_col = na.left_array_size + (px + (bw - 1)) - py;
    for (std::uint32_t i = 0; i < bh; ++i) CHECK(ring16[ring_col - i] == left_col[i]);
    CHECK(ring16[ring_bot + (bw - 1)] == left_col[bh - 1]);  // the overlap cell
}

// trip 8: copy_neigh_arr (neighbor_arrays.h:271-:299) + the _pu twin:
// TOP copies bw >> g entries at (org_x >> g); LEFT copies bh >> g; TOPLEFT
// copies ((bw + bh) >> g) - 1 entries from the ring offset (org_x, org_y +
// bh - 1). The generic version reads unit_size; the _pu bakes 1 and g = 2.
TEST_CASE("l6 cl copyNeighArr copies the three extents at the granular offsets") {
    NaBuffers srcb, dstb;
    cl::NeighborArrayUnit src = MakeNa(srcb, 64, 64, 1, 4, cl::kNaFullMask);
    cl::NeighborArrayUnit dst = MakeNa(dstb, 64, 64, 1, 4, cl::kNaFullMask);
    for (std::uint32_t i = 0; i < src.top_array_size; ++i) src.top_array[i] = static_cast<std::uint8_t>(i);
    for (std::uint32_t i = 0; i < src.left_array_size; ++i) src.left_array[i] = static_cast<std::uint8_t>(200 + i);
    for (std::uint32_t i = 0; i < src.top_left_array_size; ++i) src.top_left_array[i] = static_cast<std::uint8_t>(i + 77);

    const std::uint32_t org_x = 24, org_y = 20, bw = 32, bh = 16;
    cl::copyNeighArr(&src, &dst, org_x, org_y, bw, bh, cl::kNaFullMask);

    for (std::uint32_t i = 0; i < (bw >> 2); ++i) {
        CHECK(dst.top_array[(org_x >> 2) + i] == src.top_array[(org_x >> 2) + i]);
    }
    for (std::uint32_t i = 0; i < (bh >> 2); ++i) {
        CHECK(dst.left_array[(org_y >> 2) + i] == src.left_array[(org_y >> 2) + i]);
    }
    const std::uint32_t ring_off = src.left_array_size + (org_x >> 2) - ((org_y + (bh - 1)) >> 2);
    for (std::uint32_t i = 0; i < (((bw + bh) >> 2) - 1); ++i) {
        CHECK(dst.top_left_array[ring_off + i] == src.top_left_array[ring_off + i]);
    }
    CHECK(dst.top_array[(org_x >> 2) + (bw >> 2)] == 0xAA);
    CHECK(dst.left_array[(org_y >> 2) + (bh >> 2)] == 0xAA);
}

// trip 9: the _pu copy twin over a PU-granularity (4) NA, orgs not
// 4-aligned: the (org >> 2) offset and the byte counts pin the shift rule.
TEST_CASE("l6 cl copyNeighArrPu matches the pu offsets and counts") {
    NaBuffers srcb, dstb;
    cl::NeighborArrayUnit src = MakeNa(srcb, 64, 64, 1, 4, cl::kNaFullMask);
    cl::NeighborArrayUnit dst = MakeNa(dstb, 64, 64, 1, 4, cl::kNaFullMask);
    for (std::uint32_t i = 0; i < src.top_array_size; ++i) src.top_array[i] = static_cast<std::uint8_t>(i * 2 + 1);
    for (std::uint32_t i = 0; i < src.left_array_size; ++i) src.left_array[i] = static_cast<std::uint8_t>(i + 5);
    for (std::uint32_t i = 0; i < src.top_left_array_size; ++i) src.top_left_array[i] = static_cast<std::uint8_t>(i + 90);

    const std::uint32_t org_x = 17, org_y = 13, bw = 20, bh = 12;
    cl::copyNeighArrPu(&src, &dst, org_x, org_y, bw, bh, cl::kNaFullMask);

    for (std::uint32_t i = 0; i < (bw >> 2); ++i) {
        CHECK(dst.top_array[(org_x >> 2) + i] == src.top_array[(org_x >> 2) + i]);
    }
    for (std::uint32_t i = 0; i < (bh >> 2); ++i) {
        CHECK(dst.left_array[(org_y >> 2) + i] == src.left_array[(org_y >> 2) + i]);
    }
    const std::uint32_t ring_off = src.left_array_size + (org_x >> 2) - ((org_y + (bh - 1)) >> 2);
    for (std::uint32_t i = 0; i < (((bw + bh) >> 2) - 1); ++i) {
        CHECK(dst.top_left_array[ring_off + i] == src.top_left_array[ring_off + i]);
    }
}

// trip 10: the mode write (neighbor_arrays.h:383-:452): the generic fills
// count = dim >> granularity entries with the unit_size value; the _pu
// memset variant the same at g = 2.
TEST_CASE("l6 cl modeWrite and modeWritePu fill the right extents") {
    NaBuffers gb;
    cl::NeighborArrayUnit gen = MakeNa(gb, 64, 64, 1, 4, cl::kNaFullMask);
    const std::uint8_t value = 0x5C;
    cl::neighborArrayUnitModeWrite(&gen, &value, 16, 12, 24, 20, cl::kNaFullMask);
    for (std::uint32_t i = 0; i < (24 >> 2); ++i) CHECK(gen.top_array[(16 >> 2) + i] == value);
    for (std::uint32_t i = 0; i < (20 >> 2); ++i) CHECK(gen.left_array[(12 >> 2) + i] == value);
    const std::uint32_t ring_off = gen.left_array_size + (16 >> 2) - ((12 + (20 - 1)) >> 2);
    for (std::uint32_t i = 0; i < (((24 + 20) >> 2) - 1); ++i) {
        CHECK(gen.top_left_array[ring_off + i] == value);
    }

    NaBuffers pb;
    cl::NeighborArrayUnit pu = MakeNa(pb, 64, 64, 1, 4, cl::kNaFullMask);
    cl::neighborArrayUnitModeWritePu(&pu, &value, 16, 12, 24, 20, cl::kNaFullMask);
    for (std::uint32_t i = 0; i < (24 >> 2); ++i) CHECK(pu.top_array[(16 >> 2) + i] == value);
    for (std::uint32_t i = 0; i < (20 >> 2); ++i) CHECK(pu.left_array[(12 >> 2) + i] == value);
    const std::uint32_t pring = pu.left_array_size + (16 >> 2) - ((12 + (20 - 1)) >> 2);
    for (std::uint32_t i = 0; i < (((24 + 20) >> 2) - 1); ++i) {
        CHECK(pu.top_left_array[pring + i] == value);
    }
}

// trip 11: get_partition_subsize (common_utils.h:212-:223) over the full
// table: every (partition, square bsize) cell against an independent rest
// of the spec table, and BLOCK_INVALID for non-square inputs.
TEST_CASE("l6 cl getPartitionSubsize reproduces the subsize table and rejects non-square") {
    // Independent restatement of common_utils.c:313-:345 by row.
    const entropy::BlockSize none_row[6] = {entropy::BLOCK_4X4, entropy::BLOCK_8X8, entropy::BLOCK_16X16,
                                            entropy::BLOCK_32X32, entropy::BLOCK_64X64, entropy::BLOCK_128X128};
    const entropy::BlockSize horz_row[6] = {cl::kBlockInvalid, entropy::BLOCK_8X4, entropy::BLOCK_16X8,
                                            entropy::BLOCK_32X16, entropy::BLOCK_64X32, entropy::BLOCK_128X64};
    const entropy::BlockSize vert_row[6] = {cl::kBlockInvalid, entropy::BLOCK_4X8, entropy::BLOCK_8X16,
                                            entropy::BLOCK_16X32, entropy::BLOCK_32X64, entropy::BLOCK_64X128};
    const entropy::BlockSize split_row[6] = {cl::kBlockInvalid, entropy::BLOCK_4X4, entropy::BLOCK_8X8,
                                             entropy::BLOCK_16X16, entropy::BLOCK_32X32, entropy::BLOCK_64X64};
    const entropy::BlockSize horz4_row[6] = {cl::kBlockInvalid, cl::kBlockInvalid, entropy::BLOCK_16X4,
                                             entropy::BLOCK_32X8, entropy::BLOCK_64X16, cl::kBlockInvalid};
    const entropy::BlockSize vert4_row[6] = {cl::kBlockInvalid, cl::kBlockInvalid, entropy::BLOCK_4X16,
                                             entropy::BLOCK_8X32, entropy::BLOCK_16X64, cl::kBlockInvalid};
    const entropy::BlockSize ha_row[6] = {cl::kBlockInvalid, cl::kBlockInvalid, entropy::BLOCK_16X8,
                                          entropy::BLOCK_32X16, entropy::BLOCK_64X32, entropy::BLOCK_128X64};
    const entropy::BlockSize va_row[6] = {cl::kBlockInvalid, cl::kBlockInvalid, entropy::BLOCK_8X16,
                                          entropy::BLOCK_16X32, entropy::BLOCK_32X64, entropy::BLOCK_64X128};
    const entropy::BlockSize* rows[10] = {none_row, horz_row, vert_row, split_row, ha_row,
                                          ha_row, va_row, va_row, horz4_row, vert4_row};
    for (int part = 0; part < 10; ++part) {
        for (int sq = 0; sq < 6; ++sq) {
            const entropy::BlockSize bsize = static_cast<entropy::BlockSize>(sq == 0 ? entropy::BLOCK_4X4
                : sq == 1 ? entropy::BLOCK_8X8
                : sq == 2 ? entropy::BLOCK_16X16
                : sq == 3 ? entropy::BLOCK_32X32
                : sq == 4 ? entropy::BLOCK_64X64
                          : entropy::BLOCK_128X128);
            CHECK(cl::getPartitionSubsize(bsize, static_cast<entropy::PartitionType>(part)) == rows[part][sq]);
        }
    }
    // The non-square input folds through getSqrBsizeIdx = SQR_BLOCK_SIZES.
    CHECK(cl::getPartitionSubsize(entropy::BLOCK_8X16, entropy::PARTITION_SPLIT) == cl::kBlockInvalid);
    CHECK(cl::getPartitionSubsize(entropy::BLOCK_4X4, entropy::PARTITION_INVALID) == cl::kBlockInvalid);
}

// trip 12: partition_mi_offset (common_utils.h:239-:313): the exhaustive
// small-grid enumeration over (bsize in {8X8, 16X16, 32X32}, all 10 shapes
// x their nsi range) against an independent transcription of the offset
// rule; the returned subsize cross-checks get_partition_subsize.
TEST_CASE("l6 cl partitionMiOffset walks the shapes and offsets against the independent rule") {
    struct Case {
        entropy::BlockSize bsize;
        cl::Part shape;
        unsigned nsi;
        int want_row;  // the mi_row delta the independent rule derives
        int want_col;
        entropy::BlockSize want_sub;
    };
    const entropy::BlockSize b8 = entropy::BLOCK_8X8, b16 = entropy::BLOCK_16X16, b32 = entropy::BLOCK_32X32;
    std::vector<Case> cases;
    for (const entropy::BlockSize b : {b8, b16, b32}) {
        const int hbs = b == b8 ? 1 : b == b16 ? 2 : 4;
        const int q = hbs / 2;  // quarter_step; at 8x8 it is 0 -> H4/V4 invalid, never walked below
        cases.push_back({b, cl::PART_N, 0, 0, 0, b});
        cases.push_back({b, cl::PART_H, 0, 0, 0, cl::getPartitionSubsize(b, entropy::PARTITION_HORZ)});
        cases.push_back({b, cl::PART_H, 1, hbs, 0, cl::getPartitionSubsize(b, entropy::PARTITION_HORZ)});
        cases.push_back({b, cl::PART_V, 0, 0, 0, cl::getPartitionSubsize(b, entropy::PARTITION_VERT)});
        cases.push_back({b, cl::PART_V, 1, 0, hbs, cl::getPartitionSubsize(b, entropy::PARTITION_VERT)});
        cases.push_back({b, cl::PART_S, 0, 0, 0, cl::getPartitionSubsize(b, entropy::PARTITION_SPLIT)});
        cases.push_back({b, cl::PART_S, 1, 0, hbs, cl::getPartitionSubsize(b, entropy::PARTITION_SPLIT)});
        cases.push_back({b, cl::PART_S, 2, hbs, 0, cl::getPartitionSubsize(b, entropy::PARTITION_SPLIT)});
        cases.push_back({b, cl::PART_S, 3, hbs, hbs, cl::getPartitionSubsize(b, entropy::PARTITION_SPLIT)});
        cases.push_back({b, cl::PART_HA, 0, 0, 0, cl::getPartitionSubsize(b, entropy::PARTITION_SPLIT)});
        cases.push_back({b, cl::PART_HA, 1, 0, hbs, cl::getPartitionSubsize(b, entropy::PARTITION_SPLIT)});
        cases.push_back({b, cl::PART_HA, 2, hbs, 0, cl::getPartitionSubsize(b, entropy::PARTITION_HORZ_A)});
        cases.push_back({b, cl::PART_HB, 0, 0, 0, cl::getPartitionSubsize(b, entropy::PARTITION_HORZ_B)});
        cases.push_back({b, cl::PART_HB, 1, hbs, 0, cl::getPartitionSubsize(b, entropy::PARTITION_SPLIT)});
        cases.push_back({b, cl::PART_HB, 2, hbs, hbs, cl::getPartitionSubsize(b, entropy::PARTITION_SPLIT)});
        cases.push_back({b, cl::PART_VA, 0, 0, 0, cl::getPartitionSubsize(b, entropy::PARTITION_SPLIT)});
        cases.push_back({b, cl::PART_VA, 1, hbs, 0, cl::getPartitionSubsize(b, entropy::PARTITION_SPLIT)});
        cases.push_back({b, cl::PART_VA, 2, 0, hbs, cl::getPartitionSubsize(b, entropy::PARTITION_VERT_A)});
        cases.push_back({b, cl::PART_VB, 0, 0, 0, cl::getPartitionSubsize(b, entropy::PARTITION_VERT_B)});
        cases.push_back({b, cl::PART_VB, 1, 0, hbs, cl::getPartitionSubsize(b, entropy::PARTITION_SPLIT)});
        cases.push_back({b, cl::PART_VB, 2, hbs, hbs, cl::getPartitionSubsize(b, entropy::PARTITION_SPLIT)});
        if (q != 0) {
            for (unsigned nsi = 0; nsi < 4; ++nsi) {
                cases.push_back({b, cl::PART_H4, nsi, static_cast<int>(nsi) * q, 0,
                                 cl::getPartitionSubsize(b, entropy::PARTITION_HORZ_4)});
                cases.push_back({b, cl::PART_V4, nsi, 0, static_cast<int>(nsi) * q,
                                 cl::getPartitionSubsize(b, entropy::PARTITION_VERT_4)});
            }
        }
    }
    for (const Case& c : cases) {
        int row = 100, col = 200;  // sentinel bases
        const entropy::BlockSize sub = cl::partitionMiOffset(c.bsize, c.shape, c.nsi, &row, &col);
        CHECK(sub == c.want_sub);
        CHECK(row == 100 + c.want_row);
        CHECK(col == 200 + c.want_col);
    }
}

// trip 13: av1_get_max_uv_txsize (common_utils.h:135-:149) over all 22
// bsize x ss in {1,1}: the plane bsize folds through ss_size_lookup and
// blocksize_to_txsize, then the adjusted fold. Oracle: the independent
// formula (w/2, h/2 -> the tx whose dims match) derived from the sizes.
TEST_CASE("l6 cl av1GetMaxUvTxsize folds the uv plane size to the tx size") {
    for (int b = 0; b < static_cast<int>(entropy::BLOCK_SIZES_ALL); ++b) {
        const entropy::BlockSize bsize = static_cast<entropy::BlockSize>(b);
        const int w = entropy::blockSizeWide[bsize];
        const int h = entropy::blockSizeHigh[bsize];
        // The independent rule: uv plane is (ROUND_UV(w) / 2, ROUND_UV(h) / 2);
        // find the FIRST ss_size_lookup entry matching it, then the tx.
        // Directly restate ss_size_lookup's geometry: the (w/2, h/2) block.
        entropy::BlockSize plane = cl::kBlockInvalid;
        for (int p = 0; p < static_cast<int>(entropy::BLOCK_SIZES_ALL); ++p) {
            const entropy::BlockSize pb = static_cast<entropy::BlockSize>(p);
            const int pw = entropy::blockSizeWide[pb];
            const int ph = entropy::blockSizeHigh[pb];
            const int uw = (w & 7) == 0 ? w / 2 : (w + 7) / 2 / 2 * 2;
            const int uh = (h & 7) == 0 ? h / 2 : (h + 7) / 2 / 2 * 2;
            if (pw == uw && ph == uh) {
                plane = pb;
                break;
            }
        }
        entropy::TxSize want = entropy::TX_INVALID;
        if (plane != cl::kBlockInvalid) {
            // blocksize_to_txsize = the matching tx for the plane geometry.
            const int pw = entropy::blockSizeWide[plane];
            const int ph = entropy::blockSizeHigh[plane];
            entropy::TxSize t = entropy::TX_INVALID;
            for (int t_i = 0; t_i < static_cast<int>(entropy::TX_SIZES_ALL); ++t_i) {
                const entropy::TxSize ts = static_cast<entropy::TxSize>(t_i);
                if (entropy::txSizeWide[ts] == pw && entropy::txSizeHigh[ts] == ph) {
                    t = ts;
                    break;
                }
            }
            want = cl::av1GetAdjustedTxSize(t);
        }
        CHECK(cl::av1GetMaxUvTxsize(bsize, 1, 1) == want);
    }
}

// trip 14: build_tx_org (transforms.c:52-:90) - the full oracle: for every
// (bsize, is_inter, depth), the ported table equals an independent test
// walk of the same builder rules, and the k-count equals
// tx_blocks_per_depth.
TEST_CASE("l6 cl buildTxOrg reproduces the raster and quad orders over the full table") {
    cl::buildTxOrg();
    for (int bs = 0; bs < static_cast<int>(entropy::BLOCK_SIZES_ALL); ++bs) {
        const int bw = entropy::blockSizeWide[bs];
        const int bh = entropy::blockSizeHigh[bs];
        for (int depth = 0; depth <= 2; ++depth) {
            const entropy::TxSize txs = entropy::txDepthToTxSize[depth][bs];
            const int tw = entropy::txSizeWide[txs];
            const int th = entropy::txSizeHigh[txs];
            // INTRA (is_inter = 0): raster order.
            int k = 0;
            for (int y = 0; y < bh; y += th) {
                for (int x = 0; x < bw; x += tw) {
                    CHECK(cl::txOrg[bs][0][depth][k].x == static_cast<std::uint32_t>(x));
                    CHECK(cl::txOrg[bs][0][depth][k].y == static_cast<std::uint32_t>(y));
                    ++k;
                }
            }
            // The builder count is the tile count of the depth's tx size;
            // tx_blocks_per_depth can EXCEED it (the zero-padded tail the
            // md walk reads back as (0,0) origins) - the one divergence in
            // the whole grid is (BLOCK_8X8, depth 2): builder 1, table 4
            // (transforms.c:28 vs the built raster).
            CHECK(k == (bw / tw) * (bh / th));
            // INTER (is_inter = 1): depth 0 raster; deeper quad/Z order.
            k = 0;
            if (depth == 0) {
                for (int y = 0; y < bh; y += th) {
                    for (int x = 0; x < bw; x += tw) {
                        CHECK(cl::txOrg[bs][1][depth][k].x == static_cast<std::uint32_t>(x));
                        CHECK(cl::txOrg[bs][1][depth][k].y == static_cast<std::uint32_t>(y));
                        ++k;
                    }
                }
            } else {
                const entropy::TxSize ptxs = entropy::txDepthToTxSize[depth - 1][bs];
                const int ptw = entropy::txSizeWide[ptxs];
                const int pth = entropy::txSizeHigh[ptxs];
                for (int py = 0; py < bh; py += pth) {
                    for (int px = 0; px < bw; px += ptw) {
                        for (int y = 0; y < pth; y += th) {
                            for (int x = 0; x < ptw; x += tw) {
                                CHECK(cl::txOrg[bs][1][depth][k].x == static_cast<std::uint32_t>(px + x));
                                CHECK(cl::txOrg[bs][1][depth][k].y == static_cast<std::uint32_t>(py + y));
                                ++k;
                            }
                        }
                    }
                }
            }
        }
    }
}

// trip 15: mode_decision_update_neighbor_arrays_pd0 (:121-:144): the
// skip gate, the shape fold (PART_N / the boundary H/V note), and the
// recon write at (mi_col << 2, mi_row << 2).
TEST_CASE("l6 cl updateNeighborArraysPd0 gates and writes the recon edges") {
    NaBuffers yb;
    cl::NeighborArrayUnit recon_y = MakeNa(yb, 64, 64, 1, 1, cl::kNaFullMask);
    const std::uint8_t top[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const std::uint8_t left[8] = {11, 12, 13, 14, 15, 16, 17, 18};
    cl::MdBlk blk;
    std::memset(&blk, 0, sizeof(blk));
    blk.neigh_top_recon[0] = top;
    blk.neigh_left_recon[0] = left;

    cl::MdCtx ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.recon_neigh_y = &recon_y;
    cl::modeDecisionUpdateNeighborArraysPd0(ctx, entropy::PARTITION_NONE, entropy::BLOCK_8X8, 3, 5, blk);
    for (std::uint32_t i = 0; i < 8; ++i) CHECK(recon_y.top_array[(5 << 2) + i] == top[i]);
    for (std::uint32_t i = 0; i < 8; ++i) CHECK(recon_y.left_array[(3 << 2) + i] == left[i]);

    // The skip gate: no writes.
    cl::NeighborArrayUnit recon_y2 = MakeNa(yb, 64, 64, 1, 1, cl::kNaFullMask);
    ctx.recon_neigh_y = &recon_y2;
    ctx.skip_intra = 1;
    cl::modeDecisionUpdateNeighborArraysPd0(ctx, entropy::PARTITION_NONE, entropy::BLOCK_8X8, 3, 5, blk);
    CHECK(recon_y2.top_array[(5 << 2)] == 0xAA);
    ctx.skip_intra = 0;
    ctx.pd0_use_src_samples = 1;
    cl::modeDecisionUpdateNeighborArraysPd0(ctx, entropy::PARTITION_NONE, entropy::BLOCK_8X8, 3, 5, blk);
    CHECK(recon_y2.top_array[(5 << 2)] == 0xAA);
}

// trip 16: the big mode_decision_update_neighbor_arrays (:149-:418), the
// pinned-build live path (hbd_md = 0, effective_bit_depth_gt_8 = 0), over
// an exhaustive small-grid: bsize {8X8, 16X16, 32X32} x mi_row/col {0,1,2}
// x is_inter {0,1} x the ctrl flags. Oracle: the partition ctx value from
// the lookup tables + the dc-sign value spread + the txmode bw/bh.
TEST_CASE("l6 cl updateNeighborArrays grid matches the live-path carrier rules") {
    for (const entropy::BlockSize bsize : {entropy::BLOCK_8X8, entropy::BLOCK_16X16, entropy::BLOCK_32X32}) {
        for (int mi_row = 0; mi_row < 3; ++mi_row) {
            for (int mi_col = 0; mi_col < 3; ++mi_col) {
                for (int is_inter = 0; is_inter < 2; ++is_inter) {
                    NaBuffers pc_b, luma_dc_b, cb_dc_b, cr_dc_b, txfm_b, rec_y_b, rec_cb_b, rec_cr_b, td1_dc_b,
                        td1_rec_b, td2_rec_b;
                    cl::NeighborArrayUnit leaf_part = MakeNa(pc_b, 128, 128, 1, 4, cl::kNaFullMask);
                    cl::NeighborArrayUnit luma_dc = MakeNa(luma_dc_b, 128, 128, 1, 4, cl::kNaTopAndLeftOnlyMask);
                    cl::NeighborArrayUnit cb_dc = MakeNa(cb_dc_b, 128, 128, 1, 4, cl::kNaTopAndLeftOnlyMask);
                    cl::NeighborArrayUnit cr_dc = MakeNa(cr_dc_b, 128, 128, 1, 4, cl::kNaTopAndLeftOnlyMask);
                    cl::NeighborArrayUnit txfm = MakeNa(txfm_b, 128, 128, 1, 4, cl::kNaFullMask);
                    cl::NeighborArrayUnit rec_y = MakeNa(rec_y_b, 128, 128, 1, 1, cl::kNaFullMask);
                    cl::NeighborArrayUnit rec_cb = MakeNa(rec_cb_b, 128, 128, 1, 1, cl::kNaFullMask);
                    cl::NeighborArrayUnit rec_cr = MakeNa(rec_cr_b, 128, 128, 1, 1, cl::kNaFullMask);
                    cl::NeighborArrayUnit td1_dc = MakeNa(td1_dc_b, 128, 128, 1, 4, cl::kNaTopAndLeftOnlyMask);
                    cl::NeighborArrayUnit td1_rec = MakeNa(td1_rec_b, 128, 128, 1, 1, cl::kNaFullMask);
                    cl::NeighborArrayUnit td2_rec = MakeNa(td2_rec_b, 128, 128, 1, 1, cl::kNaFullMask);

                    cl::MdPcs pcs;
                    std::memset(&pcs, 0, sizeof(pcs));
                    pcs.mdleaf_partition_na[0] = &leaf_part;
                    pcs.mdleaf_partition_na[1] = &leaf_part;
                    pcs.md_tx_depth_1_luma_dc_sign_level_coeff_na[1] = &td1_dc;
                    pcs.md_tx_depth_1_luma_recon_na[1] = &td1_rec;
                    pcs.md_tx_depth_2_luma_recon_na[1] = &td2_rec;

                    std::uint8_t top[64], left[64];
                    for (int i = 0; i < 64; ++i) {
                        top[i] = static_cast<std::uint8_t>(i + 1);
                        left[i] = static_cast<std::uint8_t>(i + 100);
                    }
                    cl::MdCtx ctx;
                    std::memset(&ctx, 0, sizeof(ctx));
                    ctx.is_inter = static_cast<std::uint8_t>(is_inter);
                    ctx.txs_ctrls_enabled = 1;
                    ctx.update_skip_ctx_dc_sign_ctx = 1;  // the dc-sign NA writes (:180)
                    ctx.uv_mode = 0;  // CHROMA_MODE_0 <= CHROMA_MODE_1
                    ctx.hbd_md = 0;
                    ctx.effective_bit_depth_gt_8 = 0;
                    ctx.frm_hdr_tx_mode_select = 1;
                    ctx.blk.tx_depth = 0;
                    ctx.blk.quant_dc_y[0] = 7;
                    ctx.blk.quant_dc_u[0] = 9;
                    ctx.blk.quant_dc_v[0] = 11;
                    ctx.blk.neigh_top_recon[0] = top;
                    ctx.blk.neigh_left_recon[0] = left;
                    ctx.blk.neigh_top_recon[1] = top;
                    ctx.blk.neigh_left_recon[1] = left;
                    ctx.blk.neigh_top_recon[2] = top;
                    ctx.blk.neigh_left_recon[2] = left;
                    ctx.leaf_partition_na = &leaf_part;
                    ctx.luma_dc_sign_level_coeff_na = &luma_dc;
                    ctx.cb_dc_sign_level_coeff_na = &cb_dc;
                    ctx.cr_dc_sign_level_coeff_na = &cr_dc;
                    ctx.txfm_context_array = &txfm;
                    ctx.recon_neigh_y = &rec_y;
                    ctx.recon_neigh_cb = &rec_cb;
                    ctx.recon_neigh_cr = &rec_cr;

                    cl::modeDecisionUpdateNeighborArrays(pcs, ctx, bsize, mi_row, mi_col);

                    const int bwidth = entropy::blockSizeWide[bsize];
                    const int bheight = entropy::blockSizeHigh[bsize];
                    const int org_x = mi_col << 2;
                    const int org_y = mi_row << 2;
                    // The leaf partition ctx from the lookup (the pu write).
                    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(bwidth >> 2); ++i) {
                        CHECK(leaf_part.top_array[(org_x >> 2) + i] ==
                              cl::partitionContextLookupAbove[bsize]);
                    }
                    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(bheight >> 2); ++i) {
                        CHECK(leaf_part.left_array[(org_y >> 2) + i] == cl::partitionContextLookupLeft[bsize]);
                    }
                    // The luma dc-sign spread (tx_depth 0: one tx over the block).
                    const entropy::TxSize tx_size = entropy::txDepthToTxSize[0][bsize];
                    const int txw = entropy::txSizeWide[tx_size];
                    const int txh = entropy::txSizeHigh[tx_size];
                    const std::uint32_t txb_org_x = static_cast<std::uint32_t>(org_x) + cl::txOrg[bsize][is_inter][0][0].x;
                    const std::uint32_t txb_org_y = static_cast<std::uint32_t>(org_y) + cl::txOrg[bsize][is_inter][0][0].y;
                    (void)txb_org_y;
                    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(txw >> 2); ++i) {
                        CHECK(luma_dc.top_array[(txb_org_x >> 2) + i] == 7);
                        CHECK(td1_dc.top_array[(txb_org_x >> 2) + i] == 7);
                    }
                    // The recon edges at the block origin.
                    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(bwidth); ++i) {
                        CHECK(rec_y.top_array[org_x + i] == top[i]);
                    }
                    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(bheight); ++i) {
                        CHECK(rec_y.left_array[org_y + i] == left[i]);
                    }
                    // Chroma only when is_chroma_reference holds.
                    const bool has_chroma = amvp::isChromaReference(mi_row, mi_col, static_cast<int>(bsize), 1, 1);
                    if (has_chroma) {
                        const entropy::BlockSize bsize_uv = cl::getPlaneBlockSize(bsize, 1, 1);
                        const int bw_uv = entropy::blockSizeWide[bsize_uv];
                        const int bh_uv = entropy::blockSizeHigh[bsize_uv];
                        const int uv_x = ((org_x & ~7)) >> 1;
                        const int uv_y = ((org_y & ~7)) >> 1;
                        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(bw_uv); ++i) {
                            CHECK(rec_cb.top_array[uv_x + i] == top[i]);
                            CHECK(rec_cr.top_array[uv_x + i] == top[i]);
                        }
                        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(bh_uv); ++i) {
                            CHECK(rec_cb.left_array[uv_y + i] == left[i]);
                            CHECK(rec_cr.left_array[uv_y + i] == left[i]);
                        }
                        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(txw >> 3); ++i) {
                            CHECK(cb_dc.top_array[(((txb_org_x & ~7)) >> 1 >> 2) + i] == 9);
                            CHECK(cr_dc.top_array[(((txb_org_x & ~7)) >> 1 >> 2) + i] == 11);
                        }
                    } else {
                        CHECK(rec_cb.top_array[0] == 0xAA);
                    }
                    // The txfm bw/bh write (TX_MODE_SELECT live).
                    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(bwidth >> 2); ++i) {
                        CHECK(txfm.top_array[(org_x >> 2) + i] == static_cast<std::uint8_t>(txw));
                    }
                    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(bheight >> 2); ++i) {
                        CHECK(txfm.left_array[(org_y >> 2) + i] == static_cast<std::uint8_t>(txh));
                    }
                }
            }
        }
    }
}

// trip 17: md_update_all_neighbour_arrays (:661-:674) + _multiple
// (:676-:689): the shape/nsi fold + the tested gate + the mi-map gate.
TEST_CASE("l6 cl mdUpdateAllNeighbourArrays multiple walks the tested blocks and reports the mi-map gate") {
    NaBuffers rec_y_b, pc_b, dc_b, cb_b, cr_b, txfm_b;
    cl::NeighborArrayUnit rec_y = MakeNa(rec_y_b, 64, 64, 1, 1, cl::kNaFullMask);
    cl::NeighborArrayUnit leaf_part = MakeNa(pc_b, 64, 64, 1, 4, cl::kNaFullMask);
    cl::NeighborArrayUnit luma_dc = MakeNa(dc_b, 64, 64, 1, 4, cl::kNaTopAndLeftOnlyMask);
    cl::NeighborArrayUnit cb_dc = MakeNa(cb_b, 64, 64, 1, 4, cl::kNaTopAndLeftOnlyMask);
    cl::NeighborArrayUnit cr_dc = MakeNa(cr_b, 64, 64, 1, 4, cl::kNaTopAndLeftOnlyMask);
    cl::NeighborArrayUnit txfm = MakeNa(txfm_b, 64, 64, 1, 4, cl::kNaFullMask);
    std::uint8_t top[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    std::uint8_t left[8] = {11, 12, 13, 14, 15, 16, 17, 18};
    cl::MdBlk blks[4];
    std::memset(blks, 0, sizeof(blks));
    for (int i = 0; i < 4; ++i) {
        blks[i].neigh_top_recon[0] = top;
        blks[i].neigh_left_recon[0] = left;
    }
    cl::MdCtx ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.recon_neigh_y = &rec_y;
    ctx.leaf_partition_na = &leaf_part;
    ctx.luma_dc_sign_level_coeff_na = &luma_dc;
    ctx.cb_dc_sign_level_coeff_na = &cb_dc;
    ctx.cr_dc_sign_level_coeff_na = &cr_dc;
    ctx.txfm_context_array = &txfm;
    ctx.uv_mode = 3;  // > CHROMA_MODE_1: no chroma writes
    ctx.hbd_md = 0;
    ctx.effective_bit_depth_gt_8 = 0;

    cl::MdPcs pcs;
    std::memset(&pcs, 0, sizeof(pcs));

    // A PARTITION_HORZ 8x8 at mi (2,2): two 8x4 subs at (8,8) and (8,12);
    // only nsi 0 was tested, so nsi 1's write must not happen.
    const std::uint8_t tested[2] = {1, 0};
    bool mi_map_fired = false;
    cl::mdUpdateAllNeighbourArraysMultiple(pcs, ctx, blks, tested, entropy::PARTITION_HORZ,
                                           entropy::BLOCK_8X8, 2, 2, 2, &mi_map_fired);
    CHECK(rec_y.top_array[8] == top[0]);
    CHECK(rec_y.left_array[8] == left[0]);
    CHECK(rec_y.left_array[12] == 0xAA);  // nsi 1 skipped (untested)
    CHECK(mi_map_fired);  // shut_fast_rate = 0 -> the gate is live

    // The gate off: pd_pass 0 + shut_fast_rate + no ctx flags.
    cl::NeighborArrayUnit rec_y2 = MakeNa(rec_y_b, 64, 64, 1, 1, cl::kNaFullMask);
    ctx.recon_neigh_y = &rec_y2;
    ctx.pd_pass = 0;
    ctx.shut_fast_rate = 1;
    mi_map_fired = false;
    const bool gate = cl::mdUpdateAllNeighbourArrays(pcs, ctx, blks[0], entropy::PARTITION_NONE,
                                                     entropy::BLOCK_8X8, 0, 0, 0);
    CHECK(!gate);
    CHECK(!mi_map_fired);
}


// ---------------------------------------------------------------------------
// The sort / stage-count family
// ---------------------------------------------------------------------------

// trip 21: set_nics (product_coding_loop.c:1299-:1333) - the exact NIC
// walk over pic_type x the scaling numerators, with a hand-run oracle.
TEST_CASE("l6 cl setNics applies the base table the scalings and the qp weight") {
    cl::NicScalingCtrls ctrls;
    ctrls.stage1_scaling_num = 16;
    ctrls.stage2_scaling_num = 16;
    ctrls.stage3_scaling_num = 16;
    std::uint32_t m1[5], m2[5], m3[5];
    cl::setNics(ctrls, m1, m2, m3, 0, 51, 0);
    // Stage 1: base 64 -> *16/16 = 64 -> max(2,64) = 64 -> q-weight 16/16
    // -> 64.
    CHECK(m1[0] == 64);
    CHECK(m1[3] == 64);
    CHECK(m1[4] == 64);
    // Classes 1/2 are 0 in the I-slice row: *16/16 = 0 -> max(2, 0) = 2 ->
    // *16/16 = 2.
    CHECK(m1[1] == 2);
    CHECK(m1[2] == 2);
    CHECK(m2[0] == 32);
    CHECK(m2[1] == 2);
    CHECK(m3[0] == 16);
    CHECK(m3[1] == 2);

    // pic_type 2 (non-ref) with zero numerators: min nics 1 everywhere.
    ctrls.stage1_scaling_num = 0;
    ctrls.stage2_scaling_num = 0;
    ctrls.stage3_scaling_num = 0;
    cl::setNics(ctrls, m1, m2, m3, 2, 30, 0);
    CHECK(m2[0] == 1);
    CHECK(m3[0] == 1);
    CHECK(m1[1] == 1);
}

// trip 22: set_md_stage_counts (:1335-:1354): the pic-type fold and the
// bypass flags.
TEST_CASE("l6 cl setMdStageCounts derives the bypass flags from the staging mode") {
    cl::NicScalingCtrls ctrls;
    ctrls.stage1_scaling_num = 16;
    ctrls.stage2_scaling_num = 16;
    ctrls.stage3_scaling_num = 16;
    cl::MdStageCountsCtx ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.md_staging_mode = cl::MD_STAGING_MODE_2;
    cl::setMdStageCounts(ctrls, &ctx, 1, 42, 0);
    CHECK(ctx.bypass_md_stage_1 == 0);
    CHECK(ctx.bypass_md_stage_2 == 0);
    CHECK(ctx.md_stage_1_count[0] == 32);

    ctx.md_staging_mode = cl::MD_STAGING_MODE_1;
    cl::setMdStageCounts(ctrls, &ctx, 1, 42, 0);
    CHECK(ctx.bypass_md_stage_1 == 0);
    CHECK(ctx.bypass_md_stage_2 == 1);

    ctx.md_staging_mode = cl::MD_STAGING_MODE_0;
    cl::setMdStageCounts(ctrls, &ctx, 1, 42, 0);
    CHECK(ctx.bypass_md_stage_1 == 1);
    CHECK(ctx.bypass_md_stage_2 == 1);
}

// trip 23: sort_fast_cost_based_candidates (:1356-:1377): the selection
// sort is STABLE on ties (strict <) and sorts ascending from the start
// index; the oracle is an independent insertion sort.
TEST_CASE("l6 cl sortFastCostBasedCandidates sorts ascending and keeps ties stable") {
    const std::uint64_t costs[8] = {500, 100, 300, 100, 200, 400, 100, 50};
    const std::uint32_t start = 2;
    const std::uint32_t count = 5;
    std::uint32_t idx[8];
    cl::sortFastCostBasedCandidates(costs, start, count, idx);
    // Independent oracle over {2..6} = costs {300, 100, 200, 400, 100} ->
    // stable ascending {100@3, 100@5, 200@4, 300@2, 400@6}.
    const std::uint32_t want[5] = {3, 6, 4, 2, 5};
    for (std::uint32_t i = 0; i < count; ++i) {
        CHECK(idx[i] == want[i]);
    }
}

// trip 24: sort_full_cost_based_candidates (:1379-:1393): the same
// selection sort over the already-materialized index array.
TEST_CASE("l6 cl sortFullCostBasedCandidates sorts the given index array") {
    const std::uint64_t full[6] = {40, 10, 30, 20, 10, 50};
    std::uint32_t idx[6] = {5, 0, 3, 1, 2, 4};
    cl::sortFullCostBasedCandidates(full, 6, idx);
    // Oracle: costs at idx {50,40,20,10,30,10} -> ascending {10@1, 10@4,
    // 20@3, 30@2, 40@0, 50@5}.
    const std::uint32_t want[6] = {1, 4, 3, 2, 0, 5};
    for (int i = 0; i < 6; ++i) {
        CHECK(idx[i] == want[i]);
    }
}

// trip 25: construct_best_sorted_arrays_md_stage_3 (:1395-:1407): the
// class-order concatenation.
TEST_CASE("l6 cl constructBestSortedArraysMdStage3 concatenates per class") {
    const std::uint32_t c0[3] = {7, 8, 9};
    const std::uint32_t c1[2] = {3, 4};
    const std::uint32_t c2[1] = {5};
    const std::uint32_t c3[2] = {1, 2};
    const std::uint32_t c4[1] = {6};
    const std::uint32_t* per_class[5] = {c0, c1, c2, c3, c4};
    const std::uint32_t counts[5] = {3, 2, 1, 2, 1};
    std::uint32_t out[9];
    cl::constructBestSortedArraysMdStage3(per_class, counts, out, 9);
    const std::uint32_t want[9] = {7, 8, 9, 3, 4, 5, 1, 2, 6};
    for (int i = 0; i < 9; ++i) CHECK(out[i] == want[i]);
}

// trip 26: perform_ind_uv_search_last_mds (:1413-:1446): the intra-count
// walk, the UV_DC skip gate and the inter-vs-intra cost kill.
TEST_CASE("l6 cl performIndUvSearchLastMds counts non-dc intra candidates and applies the cost kill") {
    using cl::IndUvCandidate;
    IndUvCandidate cands[4];
    cands[0] = {l16_rdo::NEARESTMV, 0, entropy::UV_DC_PRED, 100};  // inter
    cands[1] = {l16_rdo::DC_PRED, 0, entropy::UV_DC_PRED, 50};     // intra dc
    cands[2] = {l16_rdo::V_PRED, 0, entropy::UV_V_PRED, 60};       // intra non-dc
    cands[3] = {l16_rdo::NEARESTMV, 0, entropy::UV_DC_PRED, 200};
    const std::uint32_t idx[4] = {0, 1, 2, 3};

    cl::IndUvCtrls ctrls;
    ctrls.skip_ind_uv_if_only_dc = 0;
    ctrls.inter_vs_intra_cost_th = 0;
    CHECK(cl::performIndUvSearchLastMds(cands, idx, 4, ctrls));
    ctrls.skip_ind_uv_if_only_dc = 1;
    CHECK(cl::performIndUvSearchLastMds(cands, idx, 4, ctrls));
    // All-intra-dc: nothing counts when the skip gate is on.
    IndUvCandidate dc_only[2] = {{l16_rdo::DC_PRED, 0, entropy::UV_DC_PRED, 10},
                                 {l16_rdo::DC_PRED, 0, entropy::UV_DC_PRED, 20}};
    const std::uint32_t idx2[2] = {0, 1};
    CHECK(!cl::performIndUvSearchLastMds(dc_only, idx2, 2, ctrls));
    // The cost kill: inter 100, intra 50 -> th 10 gives 1000 < 5000 ->
    // kill; th 5 gives 500 < 5000 false -> no kill.
    IndUvCandidate pair[2] = {{l16_rdo::NEARESTMV, 0, entropy::UV_DC_PRED, 100},
                              {l16_rdo::V_PRED, 0, entropy::UV_V_PRED, 50}};
    const std::uint32_t idx3[2] = {0, 1};
    ctrls.inter_vs_intra_cost_th = 0;
    CHECK(cl::performIndUvSearchLastMds(pair, idx3, 2, ctrls));
    ctrls.inter_vs_intra_cost_th = 10;
    CHECK(!cl::performIndUvSearchLastMds(pair, idx3, 2, ctrls));
    // use_intrabc makes a candidate inter.
    IndUvCandidate intrabc[1] = {{l16_rdo::DC_PRED, 1, entropy::UV_DC_PRED, 1}};
    const std::uint32_t idx4[1] = {0};
    CHECK(!cl::performIndUvSearchLastMds(intrabc, idx4, 1, ctrls));
}

// ---------------------------------------------------------------------------
// The MV / ME helpers
// ---------------------------------------------------------------------------

// trip 27: determine_best_references (:65-:119): the direction walk and
// the B-slice additions.
TEST_CASE("l6 cl determineBestReferences walks the me candidates and appends the b-slice refs") {
    using l16_rdo::MeCandidate;
    std::uint8_t counts[4] = {3, 3, 0, 0};
    MeCandidate cands[4];
    std::memset(cands, 0, sizeof(cands));
    cands[0].direction = 0;
    cands[0].refIdxL0 = 0;             // LAST via list 0 -> is_last_added
    cands[1].direction = 2;
    cands[1].refIdxL0 = 0;
    cands[1].refIdxL1 = 0;
    cands[1].ref0List = 0;
    cands[1].ref1List = 1;             // rf = {LAST, BWD} -> the uni pair 0
    cands[2].direction = 1;
    cands[2].refIdxL1 = 0;             // BWD -> is_bwd_added
    l16_rdo::MeSbResults sb;
    sb.totalMeCandidateIndex = counts;
    sb.meCandidateArray = cands;

    cl::BestRefPcs pcs;
    pcs.slice_type = 2;  // B_SLICE
    pcs.ref_list0_count_try = 1;
    pcs.ref_list1_count_try = 1;
    std::int32_t ref_arr[8];
    std::uint8_t tot = 0;
    cl::determineBestReferences(pcs, &sb, 0, 0, ref_arr, &tot);
    // cand0 -> LAST(1); cand1 -> {LAST,BWD} is a BI-directional pair ->
    // av1_ref_frame_type = TOTAL_REFS_PER_FRAME + FWD_RF_OFFSET(LAST) +
    // BWD_RF_OFFSET(BWD) * FWD_REFS = 8 + 0 + 0 = 8 (LAST_BWD_FRAME);
    // cand2 -> BWD(5); the b-slice appends LAST + BWD (already added).
    CHECK(tot == 3);
    CHECK(ref_arr[0] == amvp::kLastFrame);
    CHECK(ref_arr[2] == amvp::kBwdrefFrame);
    CHECK(ref_arr[1] == amvp::kLastBwdFrame);

    // The I-slice: no b-slice additions.
    pcs.slice_type = 0;
    std::uint8_t counts1[4] = {1, 1, 0, 0};
    MeCandidate one[2];
    std::memset(one, 0, sizeof(one));
    one[0].direction = 0;
    one[0].refIdxL0 = 2;  // LAST3
    sb.totalMeCandidateIndex = counts1;
    sb.meCandidateArray = one;
    cl::determineBestReferences(pcs, &sb, 0, 0, ref_arr, &tot);
    CHECK(tot == 1);
    CHECK(ref_arr[0] == amvp::kLast3Frame);
}

// trip 28: clip_mv_on_pic_boundary (:2195-:2212): the four boundary bands
// against hand-clamped values.
TEST_CASE("l6 cl clipMvOnPicBoundary clamps the four bands") {
    std::int16_t mvx = 0, mvy = 0;
    cl::clipMvOnPicBoundary(10, 10, 8, 8, 64, 64, 16, &mvx, &mvy);
    CHECK(mvx == 0);
    CHECK(mvy == 0);
    // Right: 60 + 20 + 8 = 88 > 64 + 16 -> mv = (64 - 60) * 8.
    mvx = 160;
    cl::clipMvOnPicBoundary(60, 10, 8, 8, 64, 64, 16, &mvx, &mvy);
    CHECK(mvx == (64 - 60) * 8);
    // Bottom: 60 + 20 + 8 > 80 -> mv = (64 - 60) * 8.
    mvy = 160;
    cl::clipMvOnPicBoundary(10, 60, 8, 8, 64, 64, 16, &mvx, &mvy);
    CHECK(mvy == (64 - 60) * 8);
    // Left: 4 + (-32) = -28 < -16 -> mv = (-4 - 8) * 8.
    mvx = -256;
    cl::clipMvOnPicBoundary(4, 10, 8, 8, 64, 64, 16, &mvx, &mvy);
    CHECK(mvx == (-4 - 8) * 8);
    // Top.
    mvy = -256;
    cl::clipMvOnPicBoundary(10, 4, 8, 8, 64, 64, 16, &mvx, &mvy);
    CHECK(mvy == (-4 - 8) * 8);
    // The border relaxes the bands: with border 40, blk 4 mv -256:
    // 4 - 32 = -28 < -40 false -> unclamped.
    mvx = -256;
    cl::clipMvOnPicBoundary(4, 4, 8, 8, 64, 64, 40, &mvx, &mvy);
    CHECK(mvx == -256);
}

// trip 29: check_spatial_mv_size (:2219-:2241): the threshold bands and
// the early return.
TEST_CASE("l6 cl checkSpatialMvSize returns the band of the worst mvp") {
    cl::Mv2 mvps[3];
    mvps[0] = {100, 100};
    mvps[1] = {600, 100};
    mvps[2] = {3000, 100};
    std::int16_t me_x = 100, me_y = 100;
    CHECK(cl::checkSpatialMvSize(mvps, 3, &me_x, &me_y) == 3);
    cl::Mv2 mid[2] = {mvps[0], {600, 100}};
    CHECK(cl::checkSpatialMvSize(mid, 2, &me_x, &me_y) == 2);
    cl::Mv2 low[2] = {{300, 100}, {100, 100}};
    CHECK(cl::checkSpatialMvSize(low, 2, &me_x, &me_y) == 1);
    cl::Mv2 exact[1] = {{2048, 512}};
    CHECK(cl::checkSpatialMvSize(exact, 1, &me_x, &me_y) == 2);  // x 2048 > MEDIUM 512
    // The me_mv itself drives the band (the mvp at the small band).
    std::int16_t big = 3000;
    CHECK(cl::checkSpatialMvSize(low, 2, &big, &me_y) == 3);
    // count 0: the loop never runs -> 0.
    CHECK(cl::checkSpatialMvSize(mvps, 0, &big, &me_y) == 0);
}

// trip 30: check_temporal_mv_size (:2248-:2265): the INVALID_MV gate and
// the two bands.
TEST_CASE("l6 cl checkTemporalMvSize bands the temporal mv") {
    CHECK(cl::checkTemporalMvSize(amvp::kInvalidMv, 0, 0) == 0);
    CHECK(cl::checkTemporalMvSize(0x00010001, 3000, 3000) == 2);
    CHECK(cl::checkTemporalMvSize(0x00010001, 1500, 100) == 1);
    CHECK(cl::checkTemporalMvSize(0x00010001, 1000, 1000) == 0);
    CHECK(cl::checkTemporalMvSize(0x00010001, -3000, 100) == 2);
}

// trip 31: derive_me_offsets (:1992-:2017): the 128-sb branch and the
// sb-index branch over a small grid.
TEST_CASE("l6 cl deriveMeOffsets folds the sb geometry over a grid") {
    for (std::uint32_t org_x = 0; org_x < 128; org_x += 16) {
        for (std::uint32_t org_y = 0; org_y < 128; org_y += 16) {
            cl::DeriveMeOffsetsInput in;
            in.sb_size_is_128 = 1;
            in.b64_size = 64;
            in.aligned_width = 128;
            in.enable_me_8x8 = 1;
            in.enable_me_16x16 = 1;
            in.sb_ptr_index = 77;
            in.blk_org_x = org_x;
            in.blk_org_y = org_y;
            in.bsize = entropy::BLOCK_8X8;
            in.max_cand = 23;
            cl::DeriveMeOffsetsCtx out;
            cl::deriveMeOffsets(in, &out);
            const std::uint32_t me_sb_x = org_x / 64;
            const std::uint32_t me_sb_y = org_y / 64;
            CHECK(out.me_sb_addr == me_sb_x + me_sb_y * 2);  // 128/64 = 2 sb wide
            CHECK(out.geom_offset_x == (me_sb_x & 1) * 64);
            CHECK(out.geom_offset_y == (me_sb_y & 1) * 64);
            CHECK(out.me_cand_offset == out.me_block_offset * 23);
        }
    }
    cl::DeriveMeOffsetsInput in;
    in.sb_size_is_128 = 0;
    in.sb_ptr_index = 9;
    in.blk_org_x = 0;
    in.blk_org_y = 0;
    in.bsize = entropy::BLOCK_16X16;
    in.enable_me_8x8 = 0;
    in.enable_me_16x16 = 1;
    in.max_cand = 23;
    cl::DeriveMeOffsetsCtx out;
    cl::deriveMeOffsets(in, &out);
    CHECK(out.me_sb_addr == 9);
    CHECK(out.geom_offset_x == 0);
    CHECK(out.geom_offset_y == 0);
    // 16x16 at (0,0): me_idx 3 -> me_idx_85[3] = 21 (the parent 32x32
    // slot); no conversions (offset < 21, me_16x16 on).
    CHECK(out.me_block_offset == 5);  // me_idx_85[3]=21 >= 21 -> conversion[0] = 5
    CHECK(out.me_cand_offset == 5 * 23);
}

// trip 32: the mv-cost chain (rd_cost.c:47-:60 + mcomp.h): the joint
// classification and the entropy/opt costs.
TEST_CASE("l6 cl mvErrCost joints and cost types against hand values") {
    cl::Mv2 zero = {0, 0};
    CHECK(cl::av1GetMvJoint(zero) == cl::MV_JOINT_ZERO);
    cl::Mv2 h_only = {100, 0};
    CHECK(cl::av1GetMvJoint(h_only) == cl::MV_JOINT_HNZVZ);
    cl::Mv2 v_only = {0, -64};
    CHECK(cl::av1GetMvJoint(v_only) == cl::MV_JOINT_HZVNZ);
    cl::Mv2 both = {8, 16};
    CHECK(cl::av1GetMvJoint(both) == cl::MV_JOINT_HNZVNZ);

    // MV_COST_OPT: ((|dy| + |dx|) << 8) * epb >> (7 + 9 - 6 + 4) = >> 14.
    const cl::Mv2 ref = {0, 0};
    const std::int32_t opt = cl::mvErrCost(both, ref, nullptr, nullptr, 64, cl::MV_COST_OPT);
    CHECK(opt == (((16 + 8) << 8) * 64) >> 14);

    // MV_COST_ENTROPY with a null mvcost -> 0.
    CHECK(cl::mvErrCost(both, ref, nullptr, nullptr, 64, cl::MV_COST_ENTROPY) == 0);
    const std::int32_t joint[4] = {0, 10, 20, 30};
    std::vector<std::int32_t> flat(33, 1);
    const std::int32_t* comp[2] = {flat.data() + 16, flat.data() + 16};
    // diff = {8, 16}: joint HNZVNZ (30) + y[16] + x[8] = 30 + 1 + 1.
    CHECK(cl::mvErrCost(both, ref, joint, comp, 1, cl::MV_COST_ENTROPY) == (32 + 8192) >> 14);
    // epb 1<<14 shifts the 32-unit cost back out.
    CHECK(cl::mvErrCost(both, ref, joint, comp, 16384, cl::MV_COST_ENTROPY) == 32);

    CHECK(cl::mvErrCost(both, ref, nullptr, nullptr, 0, cl::MV_COST_L1_LOWRES) == 6);
    CHECK(cl::mvErrCost(both, ref, nullptr, nullptr, 99, cl::MV_COST_NONE) == 0);
}

// trip 33: svt_init_mv_cost_params (:1842-:1853).
TEST_CASE("l6 cl svtInitMvCostParams folds the ctx fields") {
    const std::int32_t jcost[4] = {0, 0, 0, 0};
    const std::int32_t stack0[3] = {1, 1, 1};
    const std::int32_t stack1[3] = {2, 2, 2};
    cl::InitMvCostParamsCtx ctx;
    ctx.sq_size = 64;
    ctx.skip_diag_refinement = 3;
    ctx.nmv_vec_cost = jcost;
    ctx.nmvcoststack0 = stack0;
    ctx.nmvcoststack1 = stack1;
    cl::Mv2 ref_mv = {40, -80};
    cl::SvtMvCostParam params;
    std::memset(&params, 0, sizeof(params));
    cl::svtInitMvCostParams(ctx, &params, &ref_mv, 100, 1024, 0);
    CHECK(params.early_exit_th == 1020 - 16);
    CHECK(params.mv_cost_type == cl::MV_COST_OPT);
    CHECK(params.error_per_bit == 16);  // 1024 >> 6
    CHECK(params.mvcost[0] == stack0);
    CHECK(params.mvcost[1] == stack1);
    CHECK(params.full_ref_mv.x == (40 + 3 + 1) >> 3);   // GET_MV_RAWPEL
    CHECK(params.full_ref_mv.y == (-80 + 3 + 0) >> 3);
    ctx.skip_diag_refinement = 2;
    cl::svtInitMvCostParams(ctx, &params, &ref_mv, 100, 64, 0);
    CHECK(params.mv_cost_type == cl::MV_COST_ENTROPY);
    CHECK(params.error_per_bit == 1);
}

// trip 34: svt_pme_sad_loop_kernel (:1716-:1769): the col_num/search_step
// walk pinned against a hand oracle on a small window.
TEST_CASE("l6 cl svtPmeSadLoopKernel walks the sparse-x window with the cost oracle") {
    std::uint8_t src[8 * 8], ref[8 * 8];
    for (int i = 0; i < 64; ++i) {
        src[i] = static_cast<std::uint8_t>(i);
        ref[i] = static_cast<std::uint8_t>(i ^ 0x55);
    }
    std::uint32_t best_cost = 0xFFFFFFFFu;
    std::int16_t best_x = 0, best_y = 0;
    const cl::Mv2 ref_mv = {0, 0};
    cl::SvtMvCostParam params;
    std::memset(&params, 0, sizeof(params));
    params.ref_mv = &ref_mv;
    params.error_per_bit = 1;
    params.mv_cost_type = cl::MV_COST_OPT;
    cl::svtPmeSadLoopKernel(&params, src, 8, ref, 8, 8, 8, &best_cost, &best_x, &best_y, 0, 0, 16, 2, 2, 0, 0);

    // Independent oracle: the same walk restated.
    std::uint32_t want_cost = 0xFFFFFFFFu;
    std::int16_t want_x = 0, want_y = 0;
    std::int16_t col_num = 0;
    std::int16_t step_x = 1;
    for (std::int16_t ysi = 0; ysi < 2; ysi += 2) {
        for (std::int16_t xsi = 0; xsi < 16; xsi += step_x) {
            if (((16 - xsi) < 8) && col_num == 0) continue;
            if (col_num == 7) {
                col_num = 0;
                step_x = 2;
            } else {
                ++col_num;
                step_x = 1;
            }
            std::uint32_t cost = 0;
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    const int a = src[y * 8 + x];
                    const int b = ref[xsi + y * 8 + x];
                    cost += static_cast<std::uint32_t>(a > b ? a - b : b - a);
                }
            }
            cost += static_cast<std::uint32_t>(((((xsi) + (ysi)) << 8) * 1) >> 14);
            if (cost < want_cost) {
                want_cost = cost;
                want_x = xsi * 8;
                want_y = ysi * 8;
            }
        }
    }
    CHECK(best_cost == want_cost);
    CHECK(best_x == want_x);
    CHECK(best_y == want_y);
}

// ---------------------------------------------------------------------------
// The lambda chain
// ---------------------------------------------------------------------------

// trip 35: the rc_process.c:346-:392 chain + update_lambda against hand
// values.
TEST_CASE("l6 cl lambda chain rd multipliers and update_lambda against hand values") {
    CHECK(cl::defInterRdMultiplier(100) == 3.2 + 0.15);
    CHECK(cl::defArfRdMultiplier(100) == 3.25 + 0.15);
    CHECK(cl::defKfRdMultiplier(100) == 3.3 + 0.15);

    // q0 8-bit: dc q = 1168/16... the landed dcQuantQtx: q = dcQuantQtx(0,
    // 0, 0). KF: rdmult = (3.3 + .0015q) q^2.
    const int q0 = l15_rc::dcQuantQtx(0, 0, 0);
    CHECK(cl::computeRdMultBasedOnQindex(0, cl::SVT_AV1_KF_UPDATE, 0) ==
          static_cast<std::int32_t>((3.3 + 0.0015 * q0) * q0 * q0));
    const int q50 = l15_rc::dcQuantQtx(50, 0, 0);
    CHECK(cl::computeRdMultBasedOnQindex(0, cl::SVT_AV1_GF_UPDATE, 50) ==
          static_cast<std::int32_t>((3.25 + 0.0015 * q50) * q50 * q50));
    CHECK(cl::computeRdMultBasedOnQindex(0, cl::SVT_AV1_LF_UPDATE, 50) ==
          static_cast<std::int32_t>((3.2 + 0.0015 * q50) * q50 * q50));
    // The qindex clamp happens in dcQuantQtx (MAXQ) - over-range reads the
    // 255 row; the rdmult floor is 1.
    CHECK(cl::computeRdMultBasedOnQindex(0, cl::SVT_AV1_KF_UPDATE, 300) > 0);

    // update_lambda: the frame-type factor + the sb-modulation branches.
    cl::LambdaPcs p;
    p.update_type = cl::SVT_AV1_LF_UPDATE;
    p.frame_type = 1;  // inter
    p.temporal_layer_index = 0;
    p.hierarchical_levels = 4;
    p.rtc = 0;
    p.stats_based_sb_lambda_modulation = 0;
    p.delta_q_present = 0;
    p.r0_delta_qp_md = 0;
    p.base_q_idx = 50;
    // rdmult 1280, inter factor 150 -> (1280 * 150) >> 7 = 1500.
    CHECK(cl::updateLambda(p, 50, 50, 0, 1280) == 1500);
    // KF + rtc: the KF factor [0][KF_UPDATE] = 150 -> 1500, then the
    // boost *100 >> 7 = 1171.
    p.frame_type = 0;
    p.rtc = 1;
    CHECK(cl::updateLambda(p, 50, 50, 0, 1280) == (1500 * 100) >> 7);
    // The stats path (non-rtc, delta off): me_q 46 vs base 50 -> qdiff -4
    // -> factor 100.
    p.frame_type = 1;
    p.rtc = 0;
    p.stats_based_sb_lambda_modulation = 1;
    CHECK(cl::updateLambda(p, 50, 46, 0, 1280) == ((1500 * 100) >> 7));
    // qdiff +2 -> factor 135.
    CHECK(cl::updateLambda(p, 50, 52, 0, 1280) == ((1500 * 135) >> 7));
    // The delta_q path: qdiff -8 or worse -> 90.
    p.delta_q_present = 1;
    CHECK(cl::updateLambda(p, 40, 50, 0, 1280) == ((1500 * 90) >> 7));
    // The rtc-KF + stats path: the rtc KF boost first, then the rtc
    // stats branch (qdiff -4 -> factor 100).
    p.rtc = 1;
    p.frame_type = 0;
    p.delta_q_present = 0;
    CHECK(cl::updateLambda(p, 50, 46, 0, 1280) == (((1500 * 100) >> 7) * 100) >> 7);
}

// trip 36: compute_rd_mult / compute_fast_lambda + av1_lambda_assign_md
// (md_process.c:730-:765): the modulation chain against a hand run.
TEST_CASE("l6 cl av1LambdaAssignMd applies the modulation and weight chain") {
    cl::LambdaAssignPcs pcs;
    std::memset(&pcs, 0, sizeof(pcs));
    pcs.rtc = 0;
    pcs.stats_based_sb_lambda_modulation = 1;
    pcs.pcs_temporal_layer_index = 2;
    pcs.ref_intra_percentage = 20;  // < TH 50
    pcs.lambda_weight = 256;        // *2
    pcs.lambda_scale_factor = 128;  // *1
    pcs.update_type = cl::SVT_AV1_LF_UPDATE;
    pcs.ppcs_frame_type = 1;
    pcs.ppcs_temporal_layer_index = 0;
    pcs.ppcs_hierarchical_levels = 4;
    pcs.delta_q_present = 0;
    pcs.r0_delta_qp_md = 0;
    pcs.base_q_idx = 50;

    cl::LambdaAssignCtx ctx;
    ctx.qp_index = 50;
    ctx.me_q_index = 50;
    cl::av1LambdaAssignMd(pcs, &ctx);

    // Hand run: full[0] = updateLambda(computeRdMultBasedOnQindex(0, LF,
    // 50)) with me_q == base -> factor 128 -> rdmult. Then the intra
    // modulation *138>>7, then weight *256>>7 = *2, then scale *128>>7.
    const std::uint32_t rdmult0 = cl::computeRdMultBasedOnQindex(0, cl::SVT_AV1_LF_UPDATE, 50);
    const std::uint32_t base = cl::updateLambda(
        [&] {
            cl::LambdaPcs p;
            p.update_type = pcs.update_type;
            p.frame_type = 1;
            p.temporal_layer_index = 0;
            p.hierarchical_levels = 4;
            p.rtc = 0;
            p.stats_based_sb_lambda_modulation = 1;
            p.delta_q_present = 0;
            p.r0_delta_qp_md = 0;
            p.base_q_idx = 50;
            return p;
        }(),
        50, 50, 0, rdmult0);
    std::uint32_t want_full0 = ((base * 138) >> 7) * 256 >> 7;
    want_full0 = (want_full0 * 128) >> 7;
    CHECK(ctx.full_lambda_md[0] == want_full0);
    // fast[0] starts from the 8-bit sad table row 50.
    cl::LambdaPcs fp;
    fp.update_type = cl::SVT_AV1_LF_UPDATE;
    fp.frame_type = 1;
    fp.temporal_layer_index = 0;
    fp.hierarchical_levels = 4;
    fp.rtc = 0;
    fp.stats_based_sb_lambda_modulation = 1;
    fp.delta_q_present = 0;
    fp.r0_delta_qp_md = 0;
    fp.base_q_idx = 50;
    const std::uint32_t fast_base =
        cl::updateLambda(fp, 50, 50, 0, cl::lambdaModeDecision8BitSad[50]);
    std::uint32_t want_fast0 = ((fast_base * 138) >> 7) * 256 >> 7;
    want_fast0 = (want_fast0 * 128) >> 7;
    CHECK(ctx.fast_lambda_md[0] == want_fast0);
    // The 10-bit slot (:733, :753): under the pinned build's
    // CONFIG_ENABLE_HIGH_BIT_DEPTH=0 the bit_depth-1 rd-mult chain folds
    // through dcQuantQtx's default branch (q = -1) to the rdmult floor 1
    // (computeRdMultBasedOnQindex(1,..) == 1, pinned); the composition
    // then runs the same modulation chain at the bit_depth-1 factor row.
    const std::uint32_t base1 = cl::updateLambda(fp, 50, 50, 1, 1);
    std::uint32_t want_full1 = ((base1 * 138) >> 7) * 256 >> 7;
    want_full1 = (want_full1 * 128) >> 7;
    CHECK(ctx.full_lambda_md[1] == want_full1 * 16);
    const std::uint32_t fast_base1 = cl::updateLambda(fp, 50, 50, 1, cl::lambdaModeDecision8BitSad[50]);
    std::uint32_t want_fast1 = ((fast_base1 * 138) >> 7) * 256 >> 7;
    want_fast1 = (want_fast1 * 128) >> 7;
    CHECK(ctx.fast_lambda_md[1] == want_fast1 * 4);
    CHECK(ctx.full_sb_lambda_md[0] == ctx.full_lambda_md[0]);
}

// ---------------------------------------------------------------------------
// setup_mds / setup_pc_tree
// ---------------------------------------------------------------------------

// trip 37: setup_mds (md_process.c:158-:185) over the GEOM_2 builder: the
// tree walks 8x8 min size, the split offsets match the md-scan spans, and
// the leaf mds_idx allocation covers the geom's 85 blocks.
TEST_CASE("l6 cl setupMds builds the flat tree over the geom table") {
    l14_utility::BlockGeom geoms[85];  // the GEOM_2 85-block walk (U1)
    l14_utility::buildBlkGeom(l14_utility::GEOM_2, geoms);
    cl::MdScan mds[85];
    std::memset(mds, 0, sizeof(mds));
    std::uint32_t mds_idx = 0;
    cl::setupMds(entropy::BLOCK_64X64, 8, geoms, mds, &mds_idx, 0);
    // The root is a 64x64 with 4 splits; each split is a 32x32 with 4
    // splits; each leaf a 16x16 with 4; each leaf a leaf a 8x8.
    CHECK(mds[0].bsize == entropy::BLOCK_64X64);
    CHECK(mds[0].has_split == 1);
    // The 64x64 split stride: blocks_to_skip = 16 + 4 + 1 = 21.
    for (int i = 0; i < 4; ++i) {
        CHECK(mds[0].split[i] == static_cast<std::uint32_t>(i * 21 + 1));
        CHECK(mds[mds[0].split[i]].bsize == entropy::BLOCK_32X32);
        CHECK(mds[mds[0].split[i]].has_split == 1);
        for (int j = 0; j < 4; ++j) {
            const std::uint32_t s2 = mds[mds[0].split[i]].split[j];
            CHECK(mds[s2].bsize == entropy::BLOCK_16X16);
            for (int k = 0; k < 4; ++k) {
                const std::uint32_t s3 = mds[s2].split[k];
                CHECK(mds[s3].bsize == entropy::BLOCK_8X8);
                CHECK(mds[s3].has_split == 0);
            }
        }
    }
    // The walk consumed the whole 85-block geom.
    CHECK(mds_idx == 85);
    CHECK(mds[0].mds_idx == 0);
}

// trip 38: setup_pc_tree (md_process.c:187-:211): the flat tree + the
// tested-row bases, min size 32 (one split level).
TEST_CASE("l6 cl setupPcTree builds the tested row bases") {
    cl::PcTree tree[5];
    std::memset(tree, 0, sizeof(tree));
    cl::setupPcTree(tree, 40, 7, entropy::BLOCK_64X64, 32);
    CHECK(tree[0].bsize == entropy::BLOCK_64X64);
    CHECK(tree[0].index == 7);
    CHECK(tree[0].tested_blk_base == 40);
    CHECK(tree[0].has_split == 1);
    for (int i = 0; i < 4; ++i) {
        CHECK(tree[0].split[i] == static_cast<std::uint32_t>(i + 1));
        CHECK(tree[tree[0].split[i]].bsize == entropy::BLOCK_32X32);
        CHECK(tree[tree[0].split[i]].index == i);
        CHECK(tree[tree[0].split[i]].tested_blk_base == 40 + static_cast<std::uint32_t>(i + 1));
        CHECK(tree[tree[0].split[i]].has_split == 0);
    }
}

// ---------------------------------------------------------------------------
// coding_loop.c misc
// ---------------------------------------------------------------------------

// trip 39: svt_aom_residual_kernel (coding_loop.c:69-:91): the hbd fold
// over the PO1 kernels, the 16-bit element offsets.
TEST_CASE("l6 cl residualKernel dispatches the hbd fold with element offsets") {
    const std::uint8_t input8[12] = {200, 100, 50, 9, 9, 9, 90, 60, 30, 8, 8, 8};
    const std::uint8_t pred8[12] = {255, 0, 128, 7, 7, 7, 10, 10, 10, 6, 6, 6};
    std::int16_t residual[8] = {0};
    cl::residualKernel(input8, 0, 5, pred8, 0, 6, residual, 0, 4, false, 3, 2);
    CHECK(residual[0] == -55);
    CHECK(residual[2] == -78);

    // hbd: the uint16 planes with an offset in ELEMENTS.
    const std::uint16_t input16[6] = {0, 0, 40000, 500, 0, 0};
    const std::uint16_t pred16[6] = {0, 0, 40001, 600, 0, 0};
    std::int16_t res16[1] = {0};
    cl::residualKernel(reinterpret_cast<const std::uint8_t*>(input16), 2, 5,
                       reinterpret_cast<const std::uint8_t*>(pred16), 2, 6, res16, 0, 1, true, 1, 1);
    CHECK(res16[0] == static_cast<std::int16_t>(40000) - static_cast<std::int16_t>(40001));
}

// trip 40: av1_copy_frame_mvs (coding_loop.c:1038-:1070): the ROUND/
    // POWER_OF_TWO halves, the side gate and the REFMVS_LIMIT band.
TEST_CASE("l6 cl av1CopyFrameMvs rounds the halves and gates the side and limit") {
    std::int32_t rf[6 * 6];
    std::uint32_t packed[6 * 6];
    for (int i = 0; i < 36; ++i) {
        rf[i] = 77;
        packed[i] = 0xFFFFFFFFu;
    }
    cl::CopyFrameMvsMi mi;
    mi.ref_frame[0] = amvp::kLastFrame;
    mi.ref_frame[1] = amvp::kAltrefFrame;  // side 1 -> skipped
    mi.mv[0][0] = 100;
    mi.mv[0][1] = -200;
    mi.mv[1][0] = 5000;
    mi.mv[1][1] = 5000;
    const std::int8_t side[16] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // ALTREF(7) -> 1
    cl::av1CopyFrameMvs(mi, 2, 2, 4, 4, rf, packed, 3, side);
    // x_mis/y_mis = 2 -> a 2x2 write at mvs + (1)*3 + (1) = base + 4.
    for (int h = 0; h < 2; ++h) {
        for (int w = 0; w < 2; ++w) {
            const int i = 4 + h * 3 + w;
            CHECK(rf[i] == amvp::kLastFrame);
            const std::uint32_t want_packed = static_cast<std::uint32_t>(static_cast<std::uint16_t>(100)) |
                (static_cast<std::uint32_t>(static_cast<std::uint16_t>(-200)) << 16);
            CHECK(packed[i] == want_packed);
        }
    }
    CHECK(rf[0] == 77);  // outside the written window

    // Over the limit: |mv| > 4095 -> the slot stays NONE_FRAME.
    cl::CopyFrameMvsMi big;
    big.ref_frame[0] = amvp::kLastFrame;
    big.ref_frame[1] = -1;
    big.mv[0][0] = 5000;
    big.mv[0][1] = 0;
    big.mv[1][0] = 0;
    big.mv[1][1] = 0;
    cl::av1CopyFrameMvs(big, 0, 0, 2, 2, rf, packed, 3, side);
    CHECK(rf[0] == -1);
    CHECK(packed[0] == 0);
}

// trip 41: copy_qcoeffs (coding_loop.c:1429-:1465): the y/u/v gates and
// the memcpy extents.
TEST_CASE("l6 cl copyQcoeffs copies per plane gated by has_coeff") {
    cl::CopyQcoeffsCtx ctx;
    ctx.txb_itr = 0;
    ctx.has_uv = 1;
    std::int32_t ep_y[64] = {0};
    std::int32_t md_y[64];
    for (int i = 0; i < 64; ++i) md_y[i] = i + 1;
    std::int32_t ep_u[16] = {0}, md_u[16] = {21, 22, 23, 24};
    std::int32_t ep_v[16] = {0}, md_v[16] = {31, 32, 33, 34};
    cl::copyQcoeffs(ctx, entropy::BLOCK_8X8, 0, ep_y, md_y, ep_u, md_u, ep_v, md_v, 0x1, 0x1, 0x0);
    CHECK(ep_y[0] == 1);
    CHECK(ep_y[15] == 16);
    CHECK(ep_u[0] == 21);
    CHECK(ep_u[3] == 24);
    CHECK(ep_v[0] == 0);  // v_has_coeff bit 0 clear

    // txb_itr 1 with tx_depth 0: uv_pass = (0 && 1) ? 0 : 1 = 1 -> the uv
    // copies happen (the 128x128 exception only bites at a real depth).
    ctx.txb_itr = 1;
    std::int32_t ep_y2[64] = {0};
    std::int32_t ep_u2[16] = {0};
    cl::copyQcoeffs(ctx, entropy::BLOCK_8X8, 0, ep_y2, md_y, ep_u2, md_u, ep_v, md_v, 0x2, 0x2, 0x2);
    CHECK(ep_y2[0] == 1);
    CHECK(ep_u2[0] == 21);

    // txb_itr 1 with tx_depth 1: uv_pass = 0 -> no uv copies.
    std::int32_t ep_u3[16] = {0};
    cl::copyQcoeffs(ctx, entropy::BLOCK_8X8, 1, ep_y2, md_y, ep_u3, md_u, ep_v, md_v, 0x2, 0x2, 0x2);
    CHECK(ep_u3[0] == 0);
}

// trip 42: update_coeff_cdf (coding_loop.c:1468-:1593): the txb-origin
// fold, the skip-path bits (the landed costSkipTxb composition) and the
// dc-sign NA writes for luma + chroma.
TEST_CASE("l6 cl updateCoeffCdf reads the txb ctx writes the dc-sign arrays and estimates skip bits") {
    cl::buildTxOrg();
    l16_rdo::MdRateEstimationContext me;
    std::memset(&me, 0, sizeof(me));
    l16_rdo::FcTokenCdfs fc;
    std::memset(&fc, 0, sizeof(fc));
    l16_rdo::FcSyntaxCdfs fs;
    std::memset(&fs, 0, sizeof(fs));



    NaBuffers lb, cb, crb;
    cl::NeighborArrayUnit luma_na = MakeNa(lb, 64, 64, 1, 4, cl::kNaTopAndLeftOnlyMask);
    cl::NeighborArrayUnit cb_na = MakeNa(cb, 64, 64, 1, 4, cl::kNaTopAndLeftOnlyMask);
    cl::NeighborArrayUnit cr_na = MakeNa(crb, 64, 64, 1, 4, cl::kNaTopAndLeftOnlyMask);

    std::uint8_t above[64], left[64];
    std::memset(above, 0, sizeof(above));
    std::memset(left, 0, sizeof(left));

    cl::UpdateCoeffCdfCarrier c;
    std::memset(&c, 0, sizeof(c));
    c.tx_depth = 0;
    c.txb_itr = 0;
    c.has_uv = 1;
    c.is_inter = 0;
    c.bsize = entropy::BLOCK_16X16;
    c.tx_size = entropy::txDepthToTxSize[0][entropy::BLOCK_16X16];
    c.tx_size_uv = cl::av1GetMaxUvTxsize(entropy::BLOCK_16X16, 1, 1);
    c.eob_y = 0;  // the skip path
    c.eob_u = 0;
    c.eob_v = 0;
    c.quant_dc_y = 5;
    c.quant_dc_u = 6;
    c.quant_dc_v = 7;
    c.blk_org_x = 32;
    c.blk_org_y = 16;
    c.md_rate_est_ctx = &me;
    c.ec_ctx = &fc;
    c.ec_syntax_ctx = &fs;
    c.ep_luma_na_above = above;
    c.ep_luma_na_left = left;
    c.ep_cb_na_above = above;
    c.ep_cb_na_left = left;
    c.ep_cr_na_above = above;
    c.ep_cr_na_left = left;
    c.u_has_coeff = 1;  // block_has_coeff -> the estimate runs; every eob 0
                        // routes its plane through the skip branch

    c.luma_txb_w_unit = entropy::txSizeWide[c.tx_size] >> 2;
    c.luma_txb_h_unit = entropy::txSizeHigh[c.tx_size] >> 2;
    c.ep_luma_dc_sign_na_write = &luma_na;
    c.ep_cb_dc_sign_na_write = &cb_na;
    c.ep_cr_dc_sign_na_write = &cr_na;

    std::uint64_t bits_y = 777, bits_cb = 777, bits_cr = 777;
    cl::updateCoeffCdf(c, &bits_y, &bits_cb, &bits_cr);
    // The skip-path bits: the landed costSkipTxb composition per plane.
    const std::uint64_t want_y = static_cast<std::uint64_t>(
        l16_rdo::costSkipTxb(&me, 1, &fc, c.tx_size, 0, 0));
    CHECK(bits_y == want_y);
    const std::uint64_t want_uv = static_cast<std::uint64_t>(
        l16_rdo::costSkipTxb(&me, 1, &fc, c.tx_size_uv, 1, 0));
    CHECK(bits_cb == want_uv);
    CHECK(bits_cr == want_uv);

    // The dc-sign writes: luma at the txb origin (32, 16), chroma at
    // ((32&~7)>>1, (16&~7)>>1) = (16, 8).
    for (std::uint32_t i = 0; i < 4; ++i) {
        CHECK(luma_na.top_array[(32 >> 2) + i] == 5);
    }
    for (std::uint32_t i = 0; i < 4; ++i) {
        CHECK(luma_na.left_array[(16 >> 2) + i] == 5);
    }
    for (std::uint32_t i = 0; i < 2; ++i) {
        CHECK(cb_na.top_array[(16 >> 2) + i] == 6);
        CHECK(cr_na.top_array[(16 >> 2) + i] == 7);
        CHECK(cb_na.left_array[(8 >> 2) + i] == 6);
        CHECK(cr_na.left_array[(8 >> 2) + i] == 7);
    }
    // Outside the written extents: untouched.
    CHECK(luma_na.top_array[(32 >> 2) + 4] == 0xAA);
    CHECK(cb_na.left_array[(8 >> 2) + 2] == 0xAA);
}

// trip 43: satd (common_dsp_rtcd.c:48-:55) + hadamard_path
// (:1138-:1207): the 16x16 composition against the landed PO1 primitives.
TEST_CASE("l6 cl hadamardPath composes residual hadamard and satd over the tile walk") {
    const std::uint32_t stride = 40;
    std::vector<std::uint8_t> input(stride * 40), pred(stride * 40);
    for (std::uint32_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<std::uint8_t>(i * 7 + 1);
        pred[i] = static_cast<std::uint8_t>((i * 13 + 5) & 0xFF);
    }
    std::vector<std::int16_t> residual(40 * 40, 0);
    std::vector<std::int32_t> coeff(256, 0);
    std::uint32_t satd_cost = 0;
    cl::hadamardPath(input.data(), stride, 3, pred.data(), stride, residual.data(), 40, coeff.data(),
                     entropy::BLOCK_16X16, false, &satd_cost);

    // Composition oracle: one 16x16 tile (stepr = stepc = 4 over a 4x4 mi
    // grid): residual at the shifted origins, hadamard16x16, satd.
    std::vector<std::int16_t> ref_res(40 * 40, 0);
    picops::residualKernel8bit(input.data() + 3, stride, pred.data(), stride, ref_res.data(), 40, 16, 16);
    std::vector<std::int32_t> ref_coeff(256, 0);
    picops::hadamard16x16(ref_res.data(), 40, ref_coeff.data());
    std::vector<std::int64_t> wide(256);
    std::int32_t want = 0;
    for (int i = 0; i < 256; ++i) {
        wide[static_cast<std::size_t>(i)] = ref_coeff[static_cast<std::size_t>(i)];
        want += static_cast<std::int32_t>(wide[static_cast<std::size_t>(i)] < 0
                                              ? -wide[static_cast<std::size_t>(i)]
                                              : wide[static_cast<std::size_t>(i)]);
    }
    CHECK(satd_cost == static_cast<std::uint32_t>(want));
    // The residual buffer carries the tile the port produced.
    CHECK(residual[0] == ref_res[0]);
    CHECK(residual[39] == ref_res[39]);

    // satd itself: hand values.
    const std::int64_t v[4] = {-3, 2, -1, 4};
    CHECK(cl::satd(v, 4) == 10);
}
