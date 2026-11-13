// l6_pipeline - the coding-loop pure helpers (A6C). Bodies verbatim from
// the cited spans; the carrier convention (PO1/SRC1/RE8A/EMC1) holds the
// consumed wall fields. See coding_loop_helpers.h for the park list.
#include "coding_loop_helpers.h"

#include <cassert>
#include <cstring>
#include <intrin.h>

#include <picops.h>
#include <rc_arith.h>
#include <md_slices.h>
#include <mode_config.h>

namespace pipeline {

// definitions.h:607-612 get_msb (the _MSC_VER body), file-local (the
// AN16 fclamp precedent).
static std::int32_t getMsb(std::uint32_t n) {
    unsigned long first_set_bit;
    assert(n != 0);
    _BitScanReverse(&first_set_bit, n);
    return static_cast<std::int32_t>(first_set_bit);
}

// definitions.h:457 ROUND_POWER_OF_TWO (the LR2/AN9 local-helper
// precedent).
static std::int64_t roundPowerOfTwo64(std::int64_t value, int n) {
    return (value + (((std::int64_t)1 << n) >> 1)) >> n;
}
static std::int32_t roundPowerOfTwo32(std::int32_t value, int n) {
    return (value + (((std::int32_t)1 << n) >> 1)) >> n;
}

// ---------------------------------------------------------------------------
// The neighbour array unit
// ---------------------------------------------------------------------------

// svt_aom_neighbor_array_unit_ctor (neighbor_arrays.c:25-52), the sizing +
// granularity arithmetic.
void neighborArrayUnitCtor(NeighborArrayUnit* na_unit_ptr, std::uint32_t max_picture_width,
                           std::uint32_t max_picture_height, std::uint32_t unit_size,
                           std::uint8_t granularity_normal, std::uint8_t type_mask) {
    na_unit_ptr->unit_size = static_cast<std::uint8_t>(unit_size);
    na_unit_ptr->granularity_log2 = static_cast<std::uint8_t>(getMsb(granularity_normal));

    na_unit_ptr->left_array_size = (type_mask & kNaLeftMask)
        ? max_picture_height >> na_unit_ptr->granularity_log2
        : 0;
    na_unit_ptr->top_array_size = (type_mask & kNaTopMask)
        ? max_picture_width >> na_unit_ptr->granularity_log2
        : 0;
    na_unit_ptr->top_left_array_size = (type_mask & kNaTopLeftMask)
        ? (max_picture_width + max_picture_height) >> na_unit_ptr->granularity_log2
        : 0;
}

// svt_aom_neighbor_array_unit_reset (neighbor_arrays.c:54-68): the fill
// value NEIGHBOR_ARRAY_INVALID = ~0 (neighbor_arrays.h:30).
void neighborArrayUnitReset(NeighborArrayUnit* na_unit_ptr) {
    if (na_unit_ptr->left_array) {
        std::memset(na_unit_ptr->left_array, kNaInvalidFill,
                    na_unit_ptr->unit_size * na_unit_ptr->left_array_size);
    }
    if (na_unit_ptr->top_array) {
        std::memset(na_unit_ptr->top_array, kNaInvalidFill,
                    na_unit_ptr->unit_size * na_unit_ptr->top_array_size);
    }
    if (na_unit_ptr->top_left_array) {
        std::memset(na_unit_ptr->top_left_array, kNaInvalidFill,
                    na_unit_ptr->unit_size * na_unit_ptr->top_left_array_size);
    }
}

// neighbor_arrays.h:59-71.
std::uint32_t getNeighborArrayUnitLeftIndex(const NeighborArrayUnit* na_unit_ptr, std::uint32_t loc_y) {
    return loc_y >> na_unit_ptr->granularity_log2;
}

std::uint32_t getNeighborArrayUnitTopIndex(const NeighborArrayUnit* na_unit_ptr, std::uint32_t loc_x) {
    return loc_x >> na_unit_ptr->granularity_log2;
}

std::uint32_t getNeighborArrayUnitTopLeftIndex(const NeighborArrayUnit* na_unit_ptr, std::int32_t loc_x,
                                               std::int32_t loc_y) {
    return na_unit_ptr->left_array_size + (loc_x >> na_unit_ptr->granularity_log2) -
        (loc_y >> na_unit_ptr->granularity_log2);
}

// neighbor_arrays.h:132-191 (the CONFIG=0 fold: unit_size 1).
void neighborArrayUnitSampleWrite(NeighborArrayUnit* na_unit_ptr, const std::uint8_t* src_ptr,
                                  std::uint32_t stride, std::uint32_t src_origin_x, std::uint32_t src_origin_y,
                                  std::uint32_t pic_origin_x, std::uint32_t pic_origin_y, std::uint32_t bw,
                                  std::uint32_t bh, std::uint8_t mask) {
    src_ptr += ((src_origin_y * stride) + src_origin_x);

    if (mask & kNaTopMask) {
        const std::uint8_t* read_ptr = src_ptr + ((bh - 1) * stride);
        std::uint8_t* dst_ptr = na_unit_ptr->top_array + getNeighborArrayUnitTopIndex(na_unit_ptr, pic_origin_x);
        for (std::uint32_t idx = 0; idx < bw; ++idx) {
            *dst_ptr = *read_ptr;
            ++dst_ptr;
            ++read_ptr;
        }
    }

    if (mask & kNaLeftMask) {
        const std::uint8_t* read_ptr = src_ptr + (bw - 1);
        std::uint8_t* dst_ptr = na_unit_ptr->left_array + getNeighborArrayUnitLeftIndex(na_unit_ptr, pic_origin_y);
        for (std::uint32_t idx = 0; idx < bh; ++idx) {
            *dst_ptr++ = *read_ptr;
            read_ptr += stride;
        }
    }

    if (mask & kNaTopLeftMask) {
        const std::uint8_t* read_ptr = src_ptr + ((bh - 1) * stride);
        std::uint8_t* dst_ptr =
            na_unit_ptr->top_left_array +
            getNeighborArrayUnitTopLeftIndex(na_unit_ptr, static_cast<std::int32_t>(pic_origin_x),
                                             static_cast<std::int32_t>(pic_origin_y + (bh - 1)));
        std::memcpy(dst_ptr, read_ptr, bw);

        read_ptr = src_ptr + (bw - 1);
        dst_ptr =
            na_unit_ptr->top_left_array +
            getNeighborArrayUnitTopLeftIndex(na_unit_ptr, static_cast<std::int32_t>(pic_origin_x + (bw - 1)),
                                             static_cast<std::int32_t>(pic_origin_y));
        for (std::uint32_t idx = 0; idx < bh; ++idx) {
            *dst_ptr-- = *read_ptr;
            read_ptr += stride;
        }
    }
}

// neighbor_arrays.h:329-373, the 16-bit twin.
void neighborArrayUnit16bitSampleWrite(NeighborArrayUnit* na_unit_ptr, const std::uint16_t* src_ptr,
                                       std::uint32_t stride, std::uint32_t src_origin_x,
                                       std::uint32_t src_origin_y, std::uint32_t pic_origin_x,
                                       std::uint32_t pic_origin_y, std::uint32_t bw, std::uint32_t bh,
                                       std::uint8_t mask) {
    std::uint16_t* dst_ptr = nullptr;
    const std::uint16_t* read_ptr = nullptr;

    src_ptr += ((src_origin_y * stride) + src_origin_x);

    if (mask & kNaTopMask) {
        read_ptr = src_ptr + ((bh - 1) * stride);
        dst_ptr = reinterpret_cast<std::uint16_t*>(na_unit_ptr->top_array) +
            getNeighborArrayUnitTopIndex(na_unit_ptr, pic_origin_x);
        std::memcpy(dst_ptr, read_ptr, bw * sizeof(std::uint16_t));
    }

    if (mask & kNaLeftMask) {
        read_ptr = src_ptr + (bw - 1);
        dst_ptr = reinterpret_cast<std::uint16_t*>(na_unit_ptr->left_array) +
            getNeighborArrayUnitLeftIndex(na_unit_ptr, pic_origin_y);
        for (std::uint32_t idx = 0; idx < bh; ++idx) {
            *dst_ptr++ = *read_ptr;
            read_ptr += stride;
        }
    }

    if (mask & kNaTopLeftMask) {
        read_ptr = src_ptr + ((bh - 1) * stride);
        dst_ptr = reinterpret_cast<std::uint16_t*>(na_unit_ptr->top_left_array) +
            getNeighborArrayUnitTopLeftIndex(na_unit_ptr, static_cast<std::int32_t>(pic_origin_x),
                                             static_cast<std::int32_t>(pic_origin_y + (bh - 1)));
        std::memcpy(dst_ptr, read_ptr, bw * sizeof(std::uint16_t));

        read_ptr = src_ptr + (bw - 1);
        dst_ptr = reinterpret_cast<std::uint16_t*>(na_unit_ptr->top_left_array) +
            getNeighborArrayUnitTopLeftIndex(na_unit_ptr, static_cast<std::int32_t>(pic_origin_x + (bw - 1)),
                                             static_cast<std::int32_t>(pic_origin_y));
        for (std::uint32_t idx = 0; idx < bh; ++idx) {
            *dst_ptr-- = *read_ptr;
            read_ptr += stride;
        }
    }
}

// neighbor_arrays.h:210-232.
void updateReconNeighborArray(NeighborArrayUnit* na_unit_ptr, const std::uint8_t* src_ptr_top,
                              const std::uint8_t* src_ptr_left, std::uint32_t pic_origin_x,
                              std::uint32_t pic_origin_y, std::uint32_t bw, std::uint32_t bh) {
    std::uint8_t* dst_ptr = na_unit_ptr->top_array + getNeighborArrayUnitTopIndex(na_unit_ptr, pic_origin_x);
    std::memcpy(dst_ptr, src_ptr_top, bw);

    dst_ptr = na_unit_ptr->left_array + getNeighborArrayUnitLeftIndex(na_unit_ptr, pic_origin_y);
    std::memcpy(dst_ptr, src_ptr_left, bh);

    dst_ptr = na_unit_ptr->top_left_array +
        getNeighborArrayUnitTopLeftIndex(na_unit_ptr, static_cast<std::int32_t>(pic_origin_x),
                                         static_cast<std::int32_t>(pic_origin_y + (bh - 1)));
    std::memcpy(dst_ptr, src_ptr_top, bw);

    const std::uint8_t* read_ptr = src_ptr_left;
    dst_ptr = na_unit_ptr->top_left_array +
        getNeighborArrayUnitTopLeftIndex(na_unit_ptr, static_cast<std::int32_t>(pic_origin_x + (bw - 1)),
                                         static_cast<std::int32_t>(pic_origin_y));
    for (std::uint32_t idx = 0; idx < bh; ++idx) {
        *dst_ptr-- = *read_ptr++;
    }
}

// neighbor_arrays.h:238-258.
void updateReconNeighborArray16bit(NeighborArrayUnit* na_unit_ptr, const std::uint16_t* src_ptr_top,
                                   const std::uint16_t* src_ptr_left, std::uint32_t pic_origin_x,
                                   std::uint32_t pic_origin_y, std::uint32_t bw, std::uint32_t bh) {
    std::uint16_t* dst_ptr = reinterpret_cast<std::uint16_t*>(na_unit_ptr->top_array) +
        getNeighborArrayUnitTopIndex(na_unit_ptr, pic_origin_x);
    std::memcpy(dst_ptr, src_ptr_top, bw * sizeof(std::uint16_t));

    dst_ptr = reinterpret_cast<std::uint16_t*>(na_unit_ptr->left_array) +
        getNeighborArrayUnitLeftIndex(na_unit_ptr, pic_origin_y);
    std::memcpy(dst_ptr, src_ptr_left, bh * sizeof(std::uint16_t));

    dst_ptr = reinterpret_cast<std::uint16_t*>(na_unit_ptr->top_left_array) +
        getNeighborArrayUnitTopLeftIndex(na_unit_ptr, static_cast<std::int32_t>(pic_origin_x),
                                         static_cast<std::int32_t>(pic_origin_y + (bh - 1)));
    std::memcpy(dst_ptr, src_ptr_top, bw * sizeof(std::uint16_t));

    const std::uint16_t* read_ptr = src_ptr_left;
    dst_ptr = reinterpret_cast<std::uint16_t*>(na_unit_ptr->top_left_array) +
        getNeighborArrayUnitTopLeftIndex(na_unit_ptr, static_cast<std::int32_t>(pic_origin_x + (bw - 1)),
                                         static_cast<std::int32_t>(pic_origin_y));
    for (std::uint32_t idx = 0; idx < bh; ++idx) {
        *dst_ptr-- = *read_ptr++;
    }
}

// neighbor_arrays.h:271-299 (the generic copy).
void copyNeighArr(const NeighborArrayUnit* na_src, NeighborArrayUnit* na_dst, std::uint32_t org_x,
                  std::uint32_t org_y, std::uint32_t bw, std::uint32_t bh, std::uint8_t mask) {
    std::uint8_t* dst_ptr = nullptr;
    const std::uint8_t* src_ptr = nullptr;
    std::uint32_t count = 0;
    const std::uint32_t na_unit_size = na_src->unit_size;

    if (mask & kNaTopMask) {
        src_ptr = na_src->top_array + getNeighborArrayUnitTopIndex(na_src, org_x);
        dst_ptr = na_dst->top_array + getNeighborArrayUnitTopIndex(na_dst, org_x);
        count = bw >> na_src->granularity_log2;
        std::memcpy(dst_ptr, src_ptr, na_unit_size * count);
    }
    if (mask & kNaLeftMask) {
        src_ptr = na_src->left_array + getNeighborArrayUnitLeftIndex(na_src, org_y);
        dst_ptr = na_dst->left_array + getNeighborArrayUnitLeftIndex(na_dst, org_y);
        count = bh >> na_src->granularity_log2;
        std::memcpy(dst_ptr, src_ptr, na_unit_size * count);
    }
    if (mask & kNaTopLeftMask) {
        src_ptr = na_src->top_left_array +
            getNeighborArrayUnitTopLeftIndex(na_src, static_cast<std::int32_t>(org_x),
                                             static_cast<std::int32_t>(org_y + (bh - 1)));
        dst_ptr = na_dst->top_left_array +
            getNeighborArrayUnitTopLeftIndex(na_dst, static_cast<std::int32_t>(org_x),
                                             static_cast<std::int32_t>(org_y + (bh - 1)));
        count = ((bw + bh) >> na_src->granularity_log2) - 1;
        std::memcpy(dst_ptr, src_ptr, na_unit_size * count);
    }
}

// neighbor_arrays.h:306-323 (the Shape-B copy).
void copyNeighArrPu(const NeighborArrayUnit* na_src, NeighborArrayUnit* na_dst, std::uint32_t org_x,
                    std::uint32_t org_y, std::uint32_t bw, std::uint32_t bh, std::uint8_t mask) {
    if (mask & kNaTopMask) {
        const std::uint32_t off = org_x >> 2;
        const std::uint32_t bytes = bw >> 2;
        std::memcpy(na_dst->top_array + off, na_src->top_array + off, bytes);
    }
    if (mask & kNaLeftMask) {
        const std::uint32_t off = org_y >> 2;
        const std::uint32_t bytes = bh >> 2;
        std::memcpy(na_dst->left_array + off, na_src->left_array + off, bytes);
    }
    if (mask & kNaTopLeftMask) {
        const std::uint32_t off =
            na_src->left_array_size + (org_x >> 2) - ((org_y + (bh - 1)) >> 2);
        const std::uint32_t bytes = ((bw + bh) >> 2) - 1;
        std::memcpy(na_dst->top_left_array + off, na_src->top_left_array + off, bytes);
    }
}

// neighbor_arrays.h:383-422 (the generic mode write).
void neighborArrayUnitModeWrite(NeighborArrayUnit* na_unit_ptr, const void* value, std::uint32_t org_x,
                                std::uint32_t org_y, std::uint32_t bw, std::uint32_t bh, std::uint8_t mask) {
    const std::uint8_t* value_bytes = static_cast<const std::uint8_t*>(value);
    std::uint8_t* dst_ptr = nullptr;
    std::uint32_t count = 0;
    std::uint32_t na_offset = 0;
    const std::uint32_t na_unit_size = na_unit_ptr->unit_size;

    if (mask & kNaTopMask) {
        na_offset = getNeighborArrayUnitTopIndex(na_unit_ptr, org_x);
        dst_ptr = na_unit_ptr->top_array + na_offset * na_unit_size;
        count = bw >> na_unit_ptr->granularity_log2;
        for (std::uint32_t idx = 0; idx < count; ++idx) {
            std::memcpy(dst_ptr, value_bytes, na_unit_size);
            dst_ptr += na_unit_size;
        }
    }
    if (mask & kNaLeftMask) {
        na_offset = getNeighborArrayUnitLeftIndex(na_unit_ptr, org_y);
        dst_ptr = na_unit_ptr->left_array + na_offset * na_unit_size;
        count = bh >> na_unit_ptr->granularity_log2;
        for (std::uint32_t idx = 0; idx < count; ++idx) {
            std::memcpy(dst_ptr, value_bytes, na_unit_size);
            dst_ptr += na_unit_size;
        }
    }
    if (mask & kNaTopLeftMask) {
        na_offset = getNeighborArrayUnitTopLeftIndex(na_unit_ptr, static_cast<std::int32_t>(org_x),
                                                     static_cast<std::int32_t>(org_y + (bh - 1)));
        dst_ptr = na_unit_ptr->top_left_array + na_offset * na_unit_size;
        count = ((bw + bh) >> na_unit_ptr->granularity_log2) - 1;
        for (std::uint32_t idx = 0; idx < count; ++idx) {
            std::memcpy(dst_ptr, value_bytes, na_unit_size);
            dst_ptr += na_unit_size;
        }
    }
}

// neighbor_arrays.h:431-452 (the Shape-B mode write).
void neighborArrayUnitModeWritePu(NeighborArrayUnit* na_unit_ptr, const void* value, std::uint32_t org_x,
                                  std::uint32_t org_y, std::uint32_t bw, std::uint32_t bh, std::uint8_t mask) {
    const std::uint8_t v = *static_cast<const std::uint8_t*>(value);

    if (mask & kNaTopMask) {
        std::uint8_t* dst = na_unit_ptr->top_array + (org_x >> 2);
        const std::uint32_t count = bw >> 2;
        std::memset(dst, v, count);
    }
    if (mask & kNaLeftMask) {
        std::uint8_t* dst = na_unit_ptr->left_array + (org_y >> 2);
        const std::uint32_t count = bh >> 2;
        std::memset(dst, v, count);
    }
    if (mask & kNaTopLeftMask) {
        const std::uint32_t off =
            na_unit_ptr->left_array_size + (org_x >> 2) - ((org_y + (bh - 1)) >> 2);
        std::uint8_t* dst = na_unit_ptr->top_left_array + off;
        const std::uint32_t count = ((bw + bh) >> 2) - 1;
        std::memset(dst, v, count);
    }
}

// ---------------------------------------------------------------------------
// common_utils tables + helpers
// ---------------------------------------------------------------------------

// common_utils.c:265-283 (verbatim values).
const std::uint8_t numNsPerShape[9] = {1, 2, 2, 4, 4, 3, 3, 3, 3};
const std::uint32_t nsBlkOffsetMd[9] = {0, 1, 3, 5, 9, 13, 16, 19, 22};
const std::uint32_t nsBlkOffset128Md[9] = {0, 1, 3, 0 /*H4 not allowed*/, 0 /*V4 not allowed*/, 5, 8, 11, 14};
const entropy::PartitionType fromShapeToPart[10] = {
    entropy::PARTITION_NONE, entropy::PARTITION_HORZ, entropy::PARTITION_VERT, entropy::PARTITION_HORZ_4,
    entropy::PARTITION_VERT_4, entropy::PARTITION_HORZ_A, entropy::PARTITION_HORZ_B, entropy::PARTITION_VERT_A,
    entropy::PARTITION_VERT_B, entropy::PARTITION_SPLIT};
const Part fromPartToShape[11] = {PART_N, PART_H, PART_V, PART_S, PART_HA, PART_HB, PART_VA, PART_VB,
                                  PART_H4, PART_V4, PART_TOTAL};

// common_utils.c:313-345 (verbatim).
const entropy::BlockSize subsizeLookup[10][6] = {
    {entropy::BLOCK_4X4, entropy::BLOCK_8X8, entropy::BLOCK_16X16, entropy::BLOCK_32X32, entropy::BLOCK_64X64,
     entropy::BLOCK_128X128},
    {kBlockInvalid, entropy::BLOCK_8X4, entropy::BLOCK_16X8, entropy::BLOCK_32X16, entropy::BLOCK_64X32,
     entropy::BLOCK_128X64},
    {kBlockInvalid, entropy::BLOCK_4X8, entropy::BLOCK_8X16, entropy::BLOCK_16X32, entropy::BLOCK_32X64,
     entropy::BLOCK_64X128},
    {kBlockInvalid, entropy::BLOCK_4X4, entropy::BLOCK_8X8, entropy::BLOCK_16X16, entropy::BLOCK_32X32,
     entropy::BLOCK_64X64},
    {kBlockInvalid, kBlockInvalid, entropy::BLOCK_16X8, entropy::BLOCK_32X16, entropy::BLOCK_64X32,
     entropy::BLOCK_128X64},
    {kBlockInvalid, kBlockInvalid, entropy::BLOCK_16X8, entropy::BLOCK_32X16, entropy::BLOCK_64X32,
     entropy::BLOCK_128X64},
    {kBlockInvalid, kBlockInvalid, entropy::BLOCK_8X16, entropy::BLOCK_16X32, entropy::BLOCK_32X64,
     entropy::BLOCK_64X128},
    {kBlockInvalid, kBlockInvalid, entropy::BLOCK_8X16, entropy::BLOCK_16X32, entropy::BLOCK_32X64,
     entropy::BLOCK_64X128},
    {kBlockInvalid, kBlockInvalid, entropy::BLOCK_16X4, entropy::BLOCK_32X8, entropy::BLOCK_64X16,
     kBlockInvalid},
    {kBlockInvalid, kBlockInvalid, entropy::BLOCK_4X16, entropy::BLOCK_8X32, entropy::BLOCK_16X64,
     kBlockInvalid}};

// common_utils.h:194-204 get_sqr_bsize_idx.
std::int32_t getSqrBsizeIdx(entropy::BlockSize bsize) {
    switch (bsize) {
    case entropy::BLOCK_4X4: return 0;
    case entropy::BLOCK_8X8: return 1;
    case entropy::BLOCK_16X16: return 2;
    case entropy::BLOCK_32X32: return 3;
    case entropy::BLOCK_64X64: return 4;
    case entropy::BLOCK_128X128: return 5;
    default: return 6;  // SQR_BLOCK_SIZES (common_utils.h:175)
    }
}

// common_utils.h:212-223 get_partition_subsize.
entropy::BlockSize getPartitionSubsize(entropy::BlockSize bsize, entropy::PartitionType partition) {
    if (partition == entropy::PARTITION_INVALID) {
        return kBlockInvalid;
    }
    const std::int32_t sqr_bsize_idx = getSqrBsizeIdx(bsize);
    return sqr_bsize_idx >= 6 ? kBlockInvalid : subsizeLookup[partition][sqr_bsize_idx];
}

// common_utils.h:239-313 partition_mi_offset (verbatim walk; the
// mi_size_wide/high tables come from the l5 amvp mirror, common_utils.c:
// 292-293).
entropy::BlockSize partitionMiOffset(entropy::BlockSize bsize, Part shape, unsigned nsi, int* mi_row, int* mi_col) {
    const int hbs = motion::amvp::kMiSizeWide[bsize] >> 1;
    const int quarter_step = motion::amvp::kMiSizeWide[bsize] >> 2;
    entropy::PartitionType sub_bsize_part = entropy::PARTITION_INVALID;
    switch (shape) {
    case PART_N:
        assert(nsi == 0);
        return bsize;
    case PART_H:
        assert(nsi < 2);
        if (nsi) {
            *mi_row += hbs;
        }
        sub_bsize_part = entropy::PARTITION_HORZ;
        break;
    case PART_V:
        assert(nsi < 2);
        if (nsi) {
            *mi_col += hbs;
        }
        sub_bsize_part = entropy::PARTITION_VERT;
        break;
    case PART_HA:
        assert(nsi < 3);
        if (nsi) {
            *mi_col += nsi == 1 ? hbs : 0;
            *mi_row += nsi == 1 ? 0 : hbs;
        }
        sub_bsize_part = nsi < 2 ? entropy::PARTITION_SPLIT : entropy::PARTITION_HORZ_A;
        break;
    case PART_HB:
        assert(nsi < 3);
        if (nsi) {
            *mi_col += nsi == 1 ? 0 : hbs;
            *mi_row += hbs;
        }
        sub_bsize_part = nsi == 0 ? entropy::PARTITION_HORZ_B : entropy::PARTITION_SPLIT;
        break;
    case PART_VA:
        assert(nsi < 3);
        if (nsi) {
            *mi_col += nsi == 1 ? 0 : hbs;
            *mi_row += nsi == 1 ? hbs : 0;
        }
        sub_bsize_part = nsi < 2 ? entropy::PARTITION_SPLIT : entropy::PARTITION_VERT_A;
        break;
    case PART_VB:
        assert(nsi < 3);
        if (nsi) {
            *mi_col += hbs;
            *mi_row += nsi == 1 ? 0 : hbs;
        }
        sub_bsize_part = nsi == 0 ? entropy::PARTITION_VERT_B : entropy::PARTITION_SPLIT;
        break;
    case PART_H4:
        assert(nsi < 4);
        *mi_row += static_cast<int>(nsi) * quarter_step;
        sub_bsize_part = entropy::PARTITION_HORZ_4;
        break;
    case PART_V4:
        assert(nsi < 4);
        *mi_col += static_cast<int>(nsi) * quarter_step;
        sub_bsize_part = entropy::PARTITION_VERT_4;
        break;
    case PART_S:
        assert(nsi < 4);
        *mi_col += (nsi & 1) * hbs;
        *mi_row += (nsi >> 1) * hbs;
        sub_bsize_part = entropy::PARTITION_SPLIT;
        break;
    default:
        assert(0 && "invalid shape");
        break;
    }
    return getPartitionSubsize(bsize, sub_bsize_part);
}

// common_utils.c:239-263 (verbatim).
const entropy::BlockSize ssSizeLookup[22][2][2] = {
    {{entropy::BLOCK_4X4, entropy::BLOCK_4X4}, {entropy::BLOCK_4X4, entropy::BLOCK_4X4}},
    {{entropy::BLOCK_4X8, entropy::BLOCK_4X4}, {kBlockInvalid, entropy::BLOCK_4X4}},
    {{entropy::BLOCK_8X4, kBlockInvalid}, {entropy::BLOCK_4X4, entropy::BLOCK_4X4}},
    {{entropy::BLOCK_8X8, entropy::BLOCK_4X8}, {entropy::BLOCK_4X8, entropy::BLOCK_4X4}},
    {{entropy::BLOCK_8X16, entropy::BLOCK_8X8}, {kBlockInvalid, entropy::BLOCK_4X8}},
    {{entropy::BLOCK_16X8, kBlockInvalid}, {entropy::BLOCK_8X8, entropy::BLOCK_8X4}},
    {{entropy::BLOCK_16X16, entropy::BLOCK_16X8}, {entropy::BLOCK_8X16, entropy::BLOCK_8X8}},
    {{entropy::BLOCK_16X32, entropy::BLOCK_16X16}, {kBlockInvalid, entropy::BLOCK_8X16}},
    {{entropy::BLOCK_32X16, kBlockInvalid}, {entropy::BLOCK_16X16, entropy::BLOCK_16X8}},
    {{entropy::BLOCK_32X32, entropy::BLOCK_32X16}, {entropy::BLOCK_16X32, entropy::BLOCK_16X16}},
    {{entropy::BLOCK_32X64, entropy::BLOCK_32X32}, {kBlockInvalid, entropy::BLOCK_16X32}},
    {{entropy::BLOCK_64X32, kBlockInvalid}, {entropy::BLOCK_32X32, entropy::BLOCK_32X16}},
    {{entropy::BLOCK_64X64, entropy::BLOCK_64X32}, {entropy::BLOCK_32X64, entropy::BLOCK_32X32}},
    {{entropy::BLOCK_64X128, entropy::BLOCK_64X64}, {kBlockInvalid, entropy::BLOCK_32X64}},
    {{entropy::BLOCK_128X64, kBlockInvalid}, {entropy::BLOCK_64X64, entropy::BLOCK_64X32}},
    {{entropy::BLOCK_128X128, entropy::BLOCK_128X64}, {entropy::BLOCK_64X128, entropy::BLOCK_64X64}},
    {{entropy::BLOCK_4X16, entropy::BLOCK_4X8}, {kBlockInvalid, entropy::BLOCK_4X8}},
    {{entropy::BLOCK_16X4, kBlockInvalid}, {entropy::BLOCK_8X4, entropy::BLOCK_8X4}},
    {{entropy::BLOCK_8X32, entropy::BLOCK_8X16}, {kBlockInvalid, entropy::BLOCK_4X16}},
    {{entropy::BLOCK_32X8, kBlockInvalid}, {entropy::BLOCK_16X8, entropy::BLOCK_16X4}},
    {{entropy::BLOCK_16X64, entropy::BLOCK_16X32}, {kBlockInvalid, entropy::BLOCK_8X32}},
    {{entropy::BLOCK_64X16, kBlockInvalid}, {entropy::BLOCK_32X16, entropy::BLOCK_32X8}}};

// common_utils.c:214-237 (verbatim).
const entropy::TxSize blocksizeToTxsize[22] = {
    entropy::TX_4X4, entropy::TX_4X8, entropy::TX_8X4, entropy::TX_8X8, entropy::TX_8X16, entropy::TX_16X8,
    entropy::TX_16X16, entropy::TX_16X32, entropy::TX_32X16, entropy::TX_32X32, entropy::TX_32X64,
    entropy::TX_64X32, entropy::TX_64X64, entropy::TX_64X64, entropy::TX_64X64, entropy::TX_64X64,
    entropy::TX_4X16, entropy::TX_16X4, entropy::TX_8X32, entropy::TX_32X8, entropy::TX_16X64, entropy::TX_64X16};

// common_utils.h:135-140 get_plane_block_size.
entropy::BlockSize getPlaneBlockSize(entropy::BlockSize bsize, int subsampling_x, int subsampling_y) {
    if (bsize == kBlockInvalid) {
        return kBlockInvalid;
    }
    return ssSizeLookup[bsize][subsampling_x][subsampling_y];
}

// common_utils.h:100-113 av1_get_adjusted_tx_size.
entropy::TxSize av1GetAdjustedTxSize(entropy::TxSize tx_size) {
    switch (tx_size) {
    case entropy::TX_64X64:
    case entropy::TX_64X32:
    case entropy::TX_32X64: return entropy::TX_32X32;
    case entropy::TX_64X16: return entropy::TX_32X16;
    case entropy::TX_16X64: return entropy::TX_16X32;
    default: return tx_size;
    }
}

// common_utils.h:142-149 av1_get_max_uv_txsize.
entropy::TxSize av1GetMaxUvTxsize(entropy::BlockSize bsize, int subsampling_x, int subsampling_y) {
    const entropy::BlockSize plane_bsize = getPlaneBlockSize(bsize, subsampling_x, subsampling_y);
    entropy::TxSize uv_tx = entropy::TX_INVALID;
    if (plane_bsize < entropy::BLOCK_SIZES_ALL) {
        uv_tx = blocksizeToTxsize[plane_bsize];
    }
    return av1GetAdjustedTxSize(uv_tx);
}

// common_utils.c:43-61 (verbatim).
const entropy::TxSize ebMaxTxsizeLookup[22] = {
    entropy::TX_4X4, entropy::TX_4X4, entropy::TX_4X4, entropy::TX_8X8, entropy::TX_8X8, entropy::TX_8X8,
    entropy::TX_16X16, entropy::TX_16X16, entropy::TX_16X16, entropy::TX_32X32, entropy::TX_32X32,
    entropy::TX_32X32, entropy::TX_64X64, entropy::TX_64X64, entropy::TX_64X64, entropy::TX_64X64,
    entropy::TX_4X4, entropy::TX_4X4, entropy::TX_8X8, entropy::TX_8X8, entropy::TX_16X16, entropy::TX_16X16};

// coefficients.h:44-46 (verbatim).
const std::int32_t txSize2d[7] = {16, 64, 256, 1024, 4096, 32, 32};

// The partition-context lookup, the entropy.cpp:635-:637 values (the
// definitions.h:1551-:1574 table).
const std::uint8_t partitionContextLookupAbove[entropy::BLOCK_SIZES_ALL] = {
    31, 31, 30, 30, 30, 28, 28, 28, 24, 24, 24, 16, 16, 16, 0, 0, 31, 28, 30, 24, 28, 16};
const std::uint8_t partitionContextLookupLeft[entropy::BLOCK_SIZES_ALL] = {
    31, 30, 31, 30, 28, 30, 28, 24, 28, 24, 16, 24, 16, 0, 16, 0, 28, 31, 24, 30, 16, 28};

// ---------------------------------------------------------------------------
// tx_org (transforms.c:52-90)
// ---------------------------------------------------------------------------

// transforms.h:41 (the table the builder fills).
Position txOrg[22][2][3][16];

// transforms.c:52-90 svt_aom_build_tx_org (verbatim walk).
void buildTxOrg() {
    std::memset(txOrg, 0, sizeof(txOrg));
    for (int bs = 0; bs < entropy::BLOCK_SIZES_ALL; ++bs) {
        const int bw = entropy::blockSizeWide[bs];
        const int bh = entropy::blockSizeHigh[bs];
        for (int depth = 0; depth <= static_cast<int>(kMaxVartxDepth); ++depth) {
            const entropy::TxSize txs = entropy::txDepthToTxSize[depth][bs];
            const int tw = entropy::txSizeWide[txs];
            const int th = entropy::txSizeHigh[txs];
            int k = 0;
            for (int y = 0; y < bh; y += th) {
                for (int x = 0; x < bw; x += tw) {
                    txOrg[bs][0][depth][k].x = static_cast<std::uint32_t>(x);
                    txOrg[bs][0][depth][k].y = static_cast<std::uint32_t>(y);
                    ++k;
                }
            }
            k = 0;
            if (depth == 0) {
                for (int y = 0; y < bh; y += th) {
                    for (int x = 0; x < bw; x += tw) {
                        txOrg[bs][1][depth][k].x = static_cast<std::uint32_t>(x);
                        txOrg[bs][1][depth][k].y = static_cast<std::uint32_t>(y);
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
                                txOrg[bs][1][depth][k].x = static_cast<std::uint32_t>(px + x);
                                txOrg[bs][1][depth][k].y = static_cast<std::uint32_t>(py + y);
                                ++k;
                            }
                        }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// The md neighbour-update family
// ---------------------------------------------------------------------------

// is_chroma_reference (common_utils.h:315-320) via the l5 amvp mirror.
static bool hasChromaRef(int mi_row, int mi_col, entropy::BlockSize bsize) {
    return motion::amvp::isChromaReference(mi_row, mi_col, static_cast<int>(bsize), 1, 1) ? true : false;
}

// mode_decision_update_neighbor_arrays_pd0 (product_coding_loop.c:121-:144).
void modeDecisionUpdateNeighborArraysPd0(const MdCtx& ctx, entropy::PartitionType partition,
                                         entropy::BlockSize bsize, int mi_row, int mi_col, const MdBlk& blk) {
    if (ctx.skip_intra || ctx.pd0_use_src_samples) {
        return;
    }

    const Part shape = fromPartToShape[partition];
    int row = mi_row;
    int col = mi_col;
    const entropy::BlockSize sub_bsize = partitionMiOffset(bsize, shape, 0, &row, &col);

    const int bwidth = entropy::blockSizeWide[sub_bsize];
    const int bheight = entropy::blockSizeHigh[sub_bsize];
    updateReconNeighborArray(ctx.recon_neigh_y, blk.neigh_top_recon[0], blk.neigh_left_recon[0],
                             static_cast<std::uint32_t>(col << kMiSizeLog2),
                             static_cast<std::uint32_t>(row << kMiSizeLog2),
                             static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight));
}

// mode_decision_update_neighbor_arrays (product_coding_loop.c:149-:418).
// The pinned build folds SVT_EFFECTIVE_BIT_DEPTH (EbConfigMacros.h:301) and
// SVT_EFFECTIVE_HBD_MD (:303); the folds thread as the carrier's
// effective_bit_depth_gt_8 / hbd_md fields so both branch arms stay
// testable (the EMC1 fork precedent).
void modeDecisionUpdateNeighborArrays(const MdPcs& pcs, const MdCtx& ctx, entropy::BlockSize bsize, int mi_row,
                                      int mi_col) {
    const int bwidth = entropy::blockSizeWide[bsize];
    const int bheight = entropy::blockSizeHigh[bsize];
    const int org_x = mi_col << kMiSizeLog2;
    const int org_y = mi_row << kMiSizeLog2;
    const int blk_origin_x_uv = (org_x & ~(kRoundUvX - 1)) >> 1;  // ROUND_UV(org_x) >> 1
    const int blk_origin_y_uv = (org_y & ~(kRoundUvX - 1)) >> 1;
    const entropy::BlockSize bsize_uv = getPlaneBlockSize(bsize, 1, 1);
    const int bwidth_uv = entropy::blockSizeWide[bsize_uv];
    const int bheight_uv = entropy::blockSizeHigh[bsize_uv];
    const bool has_chroma = hasChromaRef(mi_row, mi_col, bsize);

    const int is_inter = ctx.is_inter;

    std::uint8_t above_ctx = partitionContextLookupAbove[bsize];
    std::uint8_t left_ctx = partitionContextLookupLeft[bsize];
    neighborArrayUnitModeWritePu(ctx.leaf_partition_na, &above_ctx, static_cast<std::uint32_t>(org_x),
                                 static_cast<std::uint32_t>(org_y), static_cast<std::uint32_t>(bwidth),
                                 static_cast<std::uint32_t>(bheight), kNaTopMask);
    neighborArrayUnitModeWritePu(ctx.leaf_partition_na, &left_ctx, static_cast<std::uint32_t>(org_x),
                                 static_cast<std::uint32_t>(org_y), static_cast<std::uint32_t>(bwidth),
                                 static_cast<std::uint32_t>(bheight), kNaLeftMask);
    if (ctx.update_skip_ctx_dc_sign_ctx) {
        const std::uint8_t tx_depth = ctx.blk.tx_depth;
        const std::uint16_t txb_count = entropy::txBlocksPerDepth[bsize][tx_depth];
        const entropy::TxSize tx_size = entropy::txDepthToTxSize[tx_depth][bsize];
        const int tx_width = entropy::txSizeWide[tx_size];
        const int tx_height = entropy::txSizeHigh[tx_size];
        const entropy::TxSize tx_size_uv = av1GetMaxUvTxsize(bsize, 1, 1);
        const int tx_width_uv = entropy::txSizeWide[tx_size_uv];
        const int tx_height_uv = entropy::txSizeHigh[tx_size_uv];
        for (std::uint8_t txb_itr = 0; txb_itr < txb_count; ++txb_itr) {
            const std::uint32_t txb_org_x = static_cast<std::uint32_t>(org_x) +
                txOrg[bsize][is_inter][tx_depth][txb_itr].x;
            const std::uint32_t txb_org_y = static_cast<std::uint32_t>(org_y) +
                txOrg[bsize][is_inter][tx_depth][txb_itr].y;
            const std::uint8_t dc_sign_level_coeff = ctx.blk.quant_dc_y[txb_itr];
            neighborArrayUnitModeWritePu(ctx.luma_dc_sign_level_coeff_na, &dc_sign_level_coeff, txb_org_x,
                                         txb_org_y, static_cast<std::uint32_t>(tx_width),
                                         static_cast<std::uint32_t>(tx_height), kNaTopAndLeftOnlyMask);

            neighborArrayUnitModeWritePu(pcs.md_tx_depth_1_luma_dc_sign_level_coeff_na[kMdNeighborArrayIndex],
                                         &dc_sign_level_coeff, txb_org_x, txb_org_y,
                                         static_cast<std::uint32_t>(tx_width),
                                         static_cast<std::uint32_t>(tx_height), kNaTopAndLeftOnlyMask);

            if (has_chroma && ctx.uv_mode <= kChromaMode1 && (tx_depth == 0 || txb_itr == 0)) {
                const std::uint8_t dc_sign_level_coeff_cb = ctx.blk.quant_dc_u[txb_itr];
                neighborArrayUnitModeWritePu(
                    ctx.cb_dc_sign_level_coeff_na, &dc_sign_level_coeff_cb,
                    (txb_org_x & ~(kRoundUvX - 1)) >> 1, (txb_org_y & ~(kRoundUvX - 1)) >> 1,
                    static_cast<std::uint32_t>(tx_width_uv), static_cast<std::uint32_t>(tx_height_uv),
                    kNaTopAndLeftOnlyMask);

                const std::uint8_t dc_sign_level_coeff_cr = ctx.blk.quant_dc_v[txb_itr];
                neighborArrayUnitModeWritePu(
                    ctx.cr_dc_sign_level_coeff_na, &dc_sign_level_coeff_cr,
                    (txb_org_x & ~(kRoundUvX - 1)) >> 1, (txb_org_y & ~(kRoundUvX - 1)) >> 1,
                    static_cast<std::uint32_t>(tx_width_uv), static_cast<std::uint32_t>(tx_height_uv),
                    kNaTopAndLeftOnlyMask);
            }
        }
    }
    if (ctx.frm_hdr_tx_mode_select) {
        const entropy::TxSize tx_size = entropy::txDepthToTxSize[ctx.blk.tx_depth][bsize];
        const std::uint8_t bw = static_cast<std::uint8_t>(entropy::txSizeWide[tx_size]);
        const std::uint8_t bh = static_cast<std::uint8_t>(entropy::txSizeHigh[tx_size]);

        neighborArrayUnitModeWritePu(ctx.txfm_context_array, &bw, static_cast<std::uint32_t>(org_x),
                                     static_cast<std::uint32_t>(org_y), static_cast<std::uint32_t>(bwidth),
                                     static_cast<std::uint32_t>(bheight), kNaTopMask);

        neighborArrayUnitModeWritePu(ctx.txfm_context_array, &bh, static_cast<std::uint32_t>(org_x),
                                     static_cast<std::uint32_t>(org_y), static_cast<std::uint32_t>(bwidth),
                                     static_cast<std::uint32_t>(bheight), kNaLeftMask);
    }
    if (!ctx.skip_intra || ctx.inter_intra_comp_ctrls_enabled) {
        if (ctx.effective_bit_depth_gt_8 && ctx.bypass_encdec && !ctx.hbd_md && ctx.pd_pass == 1) {
            // copy HBD (:245-:290)
            updateReconNeighborArray16bit(ctx.luma_recon_na_16bit, ctx.blk.neigh_top_recon_16bit[0],
                                          ctx.blk.neigh_left_recon_16bit[0],
                                          static_cast<std::uint32_t>(org_x), static_cast<std::uint32_t>(org_y),
                                          static_cast<std::uint32_t>(bwidth),
                                          static_cast<std::uint32_t>(bheight));
            if (ctx.txs_ctrls_enabled) {
                updateReconNeighborArray16bit(pcs.md_tx_depth_1_luma_recon_na_16bit[kMdNeighborArrayIndex],
                                              ctx.blk.neigh_top_recon_16bit[0],
                                              ctx.blk.neigh_left_recon_16bit[0],
                                              static_cast<std::uint32_t>(org_x),
                                              static_cast<std::uint32_t>(org_y),
                                              static_cast<std::uint32_t>(bwidth),
                                              static_cast<std::uint32_t>(bheight));
                updateReconNeighborArray16bit(pcs.md_tx_depth_2_luma_recon_na_16bit[kMdNeighborArrayIndex],
                                              ctx.blk.neigh_top_recon_16bit[0],
                                              ctx.blk.neigh_left_recon_16bit[0],
                                              static_cast<std::uint32_t>(org_x),
                                              static_cast<std::uint32_t>(org_y),
                                              static_cast<std::uint32_t>(bwidth),
                                              static_cast<std::uint32_t>(bheight));
            }
            if (has_chroma && ctx.uv_mode <= kChromaMode1) {
                updateReconNeighborArray16bit(ctx.cb_recon_na_16bit, ctx.blk.neigh_top_recon_16bit[1],
                                              ctx.blk.neigh_left_recon_16bit[1],
                                              static_cast<std::uint32_t>(blk_origin_x_uv),
                                              static_cast<std::uint32_t>(blk_origin_y_uv),
                                              static_cast<std::uint32_t>(bwidth_uv),
                                              static_cast<std::uint32_t>(bheight_uv));
                updateReconNeighborArray16bit(ctx.cr_recon_na_16bit, ctx.blk.neigh_top_recon_16bit[2],
                                              ctx.blk.neigh_left_recon_16bit[2],
                                              static_cast<std::uint32_t>(blk_origin_x_uv),
                                              static_cast<std::uint32_t>(blk_origin_y_uv),
                                              static_cast<std::uint32_t>(bwidth_uv),
                                              static_cast<std::uint32_t>(bheight_uv));
            }

            // copy 8 bit (:292-:333)
            updateReconNeighborArray(ctx.recon_neigh_y, ctx.blk.neigh_top_recon[0], ctx.blk.neigh_left_recon[0],
                                     static_cast<std::uint32_t>(org_x), static_cast<std::uint32_t>(org_y),
                                     static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight));
            if (ctx.txs_ctrls_enabled) {
                updateReconNeighborArray(pcs.md_tx_depth_1_luma_recon_na[kMdNeighborArrayIndex],
                                         ctx.blk.neigh_top_recon[0], ctx.blk.neigh_left_recon[0],
                                         static_cast<std::uint32_t>(org_x), static_cast<std::uint32_t>(org_y),
                                         static_cast<std::uint32_t>(bwidth),
                                         static_cast<std::uint32_t>(bheight));
                updateReconNeighborArray(pcs.md_tx_depth_2_luma_recon_na[kMdNeighborArrayIndex],
                                         ctx.blk.neigh_top_recon[0], ctx.blk.neigh_left_recon[0],
                                         static_cast<std::uint32_t>(org_x), static_cast<std::uint32_t>(org_y),
                                         static_cast<std::uint32_t>(bwidth),
                                         static_cast<std::uint32_t>(bheight));
            }
            if (has_chroma && ctx.uv_mode <= kChromaMode1) {
                updateReconNeighborArray(ctx.recon_neigh_cb, ctx.blk.neigh_top_recon[1],
                                         ctx.blk.neigh_left_recon[1],
                                         static_cast<std::uint32_t>(blk_origin_x_uv),
                                         static_cast<std::uint32_t>(blk_origin_y_uv),
                                         static_cast<std::uint32_t>(bwidth_uv),
                                         static_cast<std::uint32_t>(bheight_uv));
                updateReconNeighborArray(ctx.recon_neigh_cr, ctx.blk.neigh_top_recon[2],
                                         ctx.blk.neigh_left_recon[2],
                                         static_cast<std::uint32_t>(blk_origin_x_uv),
                                         static_cast<std::uint32_t>(blk_origin_y_uv),
                                         static_cast<std::uint32_t>(bwidth_uv),
                                         static_cast<std::uint32_t>(bheight_uv));
            }
        } else if (!ctx.hbd_md) {
            updateReconNeighborArray(ctx.recon_neigh_y, ctx.blk.neigh_top_recon[0], ctx.blk.neigh_left_recon[0],
                                     static_cast<std::uint32_t>(org_x), static_cast<std::uint32_t>(org_y),
                                     static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight));
            if (ctx.txs_ctrls_enabled) {
                updateReconNeighborArray(pcs.md_tx_depth_1_luma_recon_na[kMdNeighborArrayIndex],
                                         ctx.blk.neigh_top_recon[0], ctx.blk.neigh_left_recon[0],
                                         static_cast<std::uint32_t>(org_x), static_cast<std::uint32_t>(org_y),
                                         static_cast<std::uint32_t>(bwidth),
                                         static_cast<std::uint32_t>(bheight));
                updateReconNeighborArray(pcs.md_tx_depth_2_luma_recon_na[kMdNeighborArrayIndex],
                                         ctx.blk.neigh_top_recon[0], ctx.blk.neigh_left_recon[0],
                                         static_cast<std::uint32_t>(org_x), static_cast<std::uint32_t>(org_y),
                                         static_cast<std::uint32_t>(bwidth),
                                         static_cast<std::uint32_t>(bheight));
            }
            if (has_chroma && ctx.uv_mode <= kChromaMode1) {
                updateReconNeighborArray(ctx.recon_neigh_cb, ctx.blk.neigh_top_recon[1],
                                         ctx.blk.neigh_left_recon[1],
                                         static_cast<std::uint32_t>(blk_origin_x_uv),
                                         static_cast<std::uint32_t>(blk_origin_y_uv),
                                         static_cast<std::uint32_t>(bwidth_uv),
                                         static_cast<std::uint32_t>(bheight_uv));
                updateReconNeighborArray(ctx.recon_neigh_cr, ctx.blk.neigh_top_recon[2],
                                         ctx.blk.neigh_left_recon[2],
                                         static_cast<std::uint32_t>(blk_origin_x_uv),
                                         static_cast<std::uint32_t>(blk_origin_y_uv),
                                         static_cast<std::uint32_t>(bwidth_uv),
                                         static_cast<std::uint32_t>(bheight_uv));
            }
        } else {
            updateReconNeighborArray16bit(ctx.luma_recon_na_16bit, ctx.blk.neigh_top_recon_16bit[0],
                                          ctx.blk.neigh_left_recon_16bit[0],
                                          static_cast<std::uint32_t>(org_x), static_cast<std::uint32_t>(org_y),
                                          static_cast<std::uint32_t>(bwidth),
                                          static_cast<std::uint32_t>(bheight));
            if (ctx.txs_ctrls_enabled) {
                updateReconNeighborArray16bit(pcs.md_tx_depth_1_luma_recon_na_16bit[kMdNeighborArrayIndex],
                                              ctx.blk.neigh_top_recon_16bit[0],
                                              ctx.blk.neigh_left_recon_16bit[0],
                                              static_cast<std::uint32_t>(org_x),
                                              static_cast<std::uint32_t>(org_y),
                                              static_cast<std::uint32_t>(bwidth),
                                              static_cast<std::uint32_t>(bheight));
                updateReconNeighborArray16bit(pcs.md_tx_depth_2_luma_recon_na_16bit[kMdNeighborArrayIndex],
                                              ctx.blk.neigh_top_recon_16bit[0],
                                              ctx.blk.neigh_left_recon_16bit[0],
                                              static_cast<std::uint32_t>(org_x),
                                              static_cast<std::uint32_t>(org_y),
                                              static_cast<std::uint32_t>(bwidth),
                                              static_cast<std::uint32_t>(bheight));
            }
            if (has_chroma && ctx.uv_mode <= kChromaMode1) {
                updateReconNeighborArray16bit(ctx.cb_recon_na_16bit, ctx.blk.neigh_top_recon_16bit[1],
                                              ctx.blk.neigh_left_recon_16bit[1],
                                              static_cast<std::uint32_t>(blk_origin_x_uv),
                                              static_cast<std::uint32_t>(blk_origin_y_uv),
                                              static_cast<std::uint32_t>(bwidth_uv),
                                              static_cast<std::uint32_t>(bheight_uv));
                updateReconNeighborArray16bit(ctx.cr_recon_na_16bit, ctx.blk.neigh_top_recon_16bit[2],
                                              ctx.blk.neigh_left_recon_16bit[2],
                                              static_cast<std::uint32_t>(blk_origin_x_uv),
                                              static_cast<std::uint32_t>(blk_origin_y_uv),
                                              static_cast<std::uint32_t>(bwidth_uv),
                                              static_cast<std::uint32_t>(bheight_uv));
            }
        }
    }
}

// svt_aom_copy_neighbour_arrays (product_coding_loop.c:421-:657). The
// src/dst slots travel on the MdPcs carrier.
void copyNeighbourArrays(const MdPcs& pcs, const MdCtx& ctx, std::uint32_t src_idx, std::uint32_t dst_idx,
                         entropy::BlockSize bsize, int mi_row, int mi_col) {
    const int bwidth = entropy::blockSizeWide[bsize];
    const int bheight = entropy::blockSizeHigh[bsize];
    const int blk_org_x = mi_col << kMiSizeLog2;
    const int blk_org_y = mi_row << kMiSizeLog2;
    const int blk_org_x_uv = (blk_org_x & ~(kRoundUvX - 1)) >> 1;
    const int blk_org_y_uv = (blk_org_y & ~(kRoundUvX - 1)) >> 1;
    const entropy::BlockSize bsize_uv = getPlaneBlockSize(bsize, 1, 1);
    const int bwidth_uv = entropy::blockSizeWide[bsize_uv];
    const int bheight_uv = entropy::blockSizeHigh[bsize_uv];
    const bool has_chroma = hasChromaRef(mi_row, mi_col, bsize);

    copyNeighArrPu(pcs.mdleaf_partition_na[src_idx], pcs.mdleaf_partition_na[dst_idx],
                   static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                   static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight),
                   kNaTopAndLeftOnlyMask);

    if (ctx.effective_bit_depth_gt_8 && ctx.bypass_encdec && !ctx.hbd_md && ctx.pd_pass == 1) {
        // Copy 10bit arrays (:447-:487)
        copyNeighArr(pcs.md_luma_recon_na_16bit[src_idx], pcs.md_luma_recon_na_16bit[dst_idx],
                     static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                     static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight), kNaFullMask);
        if (ctx.txs_ctrls_enabled) {
            copyNeighArr(pcs.md_tx_depth_1_luma_recon_na_16bit[src_idx],
                         pcs.md_tx_depth_1_luma_recon_na_16bit[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                         static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight), kNaFullMask);
            copyNeighArr(pcs.md_tx_depth_2_luma_recon_na_16bit[src_idx],
                         pcs.md_tx_depth_2_luma_recon_na_16bit[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                         static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight), kNaFullMask);
        }
        if (has_chroma && ctx.uv_mode <= kChromaMode1) {
            copyNeighArr(pcs.md_cb_recon_na_16bit[src_idx], pcs.md_cb_recon_na_16bit[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x_uv), static_cast<std::uint32_t>(blk_org_y_uv),
                         static_cast<std::uint32_t>(bwidth_uv), static_cast<std::uint32_t>(bheight_uv),
                         kNaFullMask);

            copyNeighArr(pcs.md_cr_recon_na_16bit[src_idx], pcs.md_cr_recon_na_16bit[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x_uv), static_cast<std::uint32_t>(blk_org_y_uv),
                         static_cast<std::uint32_t>(bwidth_uv), static_cast<std::uint32_t>(bheight_uv),
                         kNaFullMask);
        }

        // Copy 8bit arrays (:489-:529)
        copyNeighArr(pcs.md_luma_recon_na[src_idx], pcs.md_luma_recon_na[dst_idx],
                     static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                     static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight), kNaFullMask);
        if (ctx.txs_ctrls_enabled) {
            copyNeighArr(pcs.md_tx_depth_1_luma_recon_na[src_idx], pcs.md_tx_depth_1_luma_recon_na[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                         static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight), kNaFullMask);
            copyNeighArr(pcs.md_tx_depth_2_luma_recon_na[src_idx], pcs.md_tx_depth_2_luma_recon_na[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                         static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight), kNaFullMask);
        }
        if (has_chroma && ctx.uv_mode <= kChromaMode1) {
            copyNeighArr(pcs.md_cb_recon_na[src_idx], pcs.md_cb_recon_na[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x_uv), static_cast<std::uint32_t>(blk_org_y_uv),
                         static_cast<std::uint32_t>(bwidth_uv), static_cast<std::uint32_t>(bheight_uv),
                         kNaFullMask);

            copyNeighArr(pcs.md_cr_recon_na[src_idx], pcs.md_cr_recon_na[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x_uv), static_cast<std::uint32_t>(blk_org_y_uv),
                         static_cast<std::uint32_t>(bwidth_uv), static_cast<std::uint32_t>(bheight_uv),
                         kNaFullMask);
        }
    } else if (!ctx.hbd_md) {
        copyNeighArr(pcs.md_luma_recon_na[src_idx], pcs.md_luma_recon_na[dst_idx],
                     static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                     static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight), kNaFullMask);
        if (ctx.txs_ctrls_enabled) {
            copyNeighArr(pcs.md_tx_depth_1_luma_recon_na[src_idx], pcs.md_tx_depth_1_luma_recon_na[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                         static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight), kNaFullMask);
            copyNeighArr(pcs.md_tx_depth_2_luma_recon_na[src_idx], pcs.md_tx_depth_2_luma_recon_na[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                         static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight), kNaFullMask);
        }
        if (has_chroma && ctx.uv_mode <= kChromaMode1) {
            copyNeighArr(pcs.md_cb_recon_na[src_idx], pcs.md_cb_recon_na[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x_uv), static_cast<std::uint32_t>(blk_org_y_uv),
                         static_cast<std::uint32_t>(bwidth_uv), static_cast<std::uint32_t>(bheight_uv),
                         kNaFullMask);

            copyNeighArr(pcs.md_cr_recon_na[src_idx], pcs.md_cr_recon_na[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x_uv), static_cast<std::uint32_t>(blk_org_y_uv),
                         static_cast<std::uint32_t>(bwidth_uv), static_cast<std::uint32_t>(bheight_uv),
                         kNaFullMask);
        }
    } else {
        copyNeighArr(pcs.md_luma_recon_na_16bit[src_idx], pcs.md_luma_recon_na_16bit[dst_idx],
                     static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                     static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight), kNaFullMask);
        if (ctx.txs_ctrls_enabled) {
            copyNeighArr(pcs.md_tx_depth_1_luma_recon_na_16bit[src_idx],
                         pcs.md_tx_depth_1_luma_recon_na_16bit[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                         static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight), kNaFullMask);
            copyNeighArr(pcs.md_tx_depth_2_luma_recon_na_16bit[src_idx],
                         pcs.md_tx_depth_2_luma_recon_na_16bit[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                         static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight), kNaFullMask);
        }
        if (has_chroma && ctx.uv_mode <= kChromaMode1) {
            copyNeighArr(pcs.md_cb_recon_na_16bit[src_idx], pcs.md_cb_recon_na_16bit[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x_uv), static_cast<std::uint32_t>(blk_org_y_uv),
                         static_cast<std::uint32_t>(bwidth_uv), static_cast<std::uint32_t>(bheight_uv),
                         kNaFullMask);

            copyNeighArr(pcs.md_cr_recon_na_16bit[src_idx], pcs.md_cr_recon_na_16bit[dst_idx],
                         static_cast<std::uint32_t>(blk_org_x_uv), static_cast<std::uint32_t>(blk_org_y_uv),
                         static_cast<std::uint32_t>(bwidth_uv), static_cast<std::uint32_t>(bheight_uv),
                         kNaFullMask);
        }
    }

    // (:615-:656)
    copyNeighArrPu(pcs.md_y_dcs_na[src_idx], pcs.md_y_dcs_na[dst_idx],
                   static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                   static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight),
                   kNaTopAndLeftOnlyMask);

    copyNeighArrPu(pcs.md_tx_depth_1_luma_dc_sign_level_coeff_na[src_idx],
                   pcs.md_tx_depth_1_luma_dc_sign_level_coeff_na[dst_idx],
                   static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                   static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight),
                   kNaTopAndLeftOnlyMask);
    if (has_chroma && ctx.uv_mode <= kChromaMode1) {
        copyNeighArrPu(pcs.md_cb_dc_sign_level_coeff_na[src_idx], pcs.md_cb_dc_sign_level_coeff_na[dst_idx],
                       static_cast<std::uint32_t>(blk_org_x_uv), static_cast<std::uint32_t>(blk_org_y_uv),
                       static_cast<std::uint32_t>(bwidth_uv), static_cast<std::uint32_t>(bheight_uv),
                       kNaTopAndLeftOnlyMask);

        copyNeighArrPu(pcs.md_cr_dc_sign_level_coeff_na[src_idx], pcs.md_cr_dc_sign_level_coeff_na[dst_idx],
                       static_cast<std::uint32_t>(blk_org_x_uv), static_cast<std::uint32_t>(blk_org_y_uv),
                       static_cast<std::uint32_t>(bwidth_uv), static_cast<std::uint32_t>(bheight_uv),
                       kNaTopAndLeftOnlyMask);
    }

    copyNeighArrPu(pcs.md_txfm_context_array[src_idx], pcs.md_txfm_context_array[dst_idx],
                   static_cast<std::uint32_t>(blk_org_x), static_cast<std::uint32_t>(blk_org_y),
                   static_cast<std::uint32_t>(bwidth), static_cast<std::uint32_t>(bheight),
                   kNaTopAndLeftOnlyMask);
}

// md_update_all_neighbour_arrays (product_coding_loop.c:661-:674). The
// svt_aom_update_mi_map call is named-parked; the returned bool is the
// gate the parked call sits behind (:670-:671).
bool mdUpdateAllNeighbourArrays(const MdPcs& pcs, const MdCtx& ctx_base, const MdBlk& blk,
                                entropy::PartitionType partition, entropy::BlockSize bsize, int mi_row,
                                int mi_col, unsigned nsi) {
    const Part shape = fromPartToShape[partition];
    int row = mi_row;
    int col = mi_col;
    const entropy::BlockSize sub_bsize = partitionMiOffset(bsize, shape, nsi, &row, &col);

    MdCtx ctx = ctx_base;
    ctx.blk = blk;
    modeDecisionUpdateNeighborArrays(pcs, ctx, sub_bsize, row, col);
    const bool mi_map_gate = ctx_base.pd_pass == 1 || !ctx_base.shut_fast_rate ||
        ctx_base.update_skip_ctx_dc_sign_ctx || ctx_base.update_skip_coeff_ctx ||
        ctx_base.use_neighbouring_mode_ctrls_enabled;
    return mi_map_gate;
}

// md_update_all_neighbour_arrays_multiple (:676-:689). NAMED DEVIATION:
// the reference's shape_block_cnt read (num_ns_per_shape[shape],
// common_utils.c:265) is OUT OF BOUNDS for split partitions - the
// 9-entry table has no PART_S slot (PART_S = 9, definitions.h:929-:940)
// - so the walk's count threads as the CALLER's num_ns_per_shape[shape]
// read; the in-bounds shapes (N/H/V/H4/V4/HA/HB/VA/VB) behave 1:1.
void mdUpdateAllNeighbourArraysMultiple(const MdPcs& pcs, const MdCtx& ctx_base, const MdBlk* blks,
                                        const std::uint8_t* tested_blk, entropy::PartitionType partition,
                                        entropy::BlockSize bsize, int mi_row, int mi_col,
                                        std::uint32_t shape_block_cnt, bool* mi_map_fired) {
    for (std::uint32_t blk_it = 0; blk_it < shape_block_cnt; ++blk_it) {
        if (tested_blk[blk_it]) {
            const bool fired = mdUpdateAllNeighbourArrays(pcs, ctx_base, blks[blk_it], partition, bsize, mi_row,
                                                          mi_col, blk_it);
            if (mi_map_fired && fired) {
                *mi_map_fired = true;
            }
        }
    }
}

// svt_aom_reset_mode_decision_neighbor_arrays (md_process.c:692-:720).
// The carrier's hbd_md is SVT_EFFECTIVE_HBD_MD(pcs->hbd_md); the 16-bit
// gate folds per EbConfigMacros.h:301-:304 under the pinned build.
void resetModeDecisionNeighborArrays(const MdPcs& pcs) {
    for (std::uint8_t depth = 0; depth < kNaTotCnt; ++depth) {
        neighborArrayUnitReset(pcs.mdleaf_partition_na[depth]);
        if (pcs.hbd_md != 1) {  // != EB_10_BIT_MD
            neighborArrayUnitReset(pcs.md_luma_recon_na[depth]);
            neighborArrayUnitReset(pcs.md_tx_depth_1_luma_recon_na[depth]);
            neighborArrayUnitReset(pcs.md_tx_depth_2_luma_recon_na[depth]);
            neighborArrayUnitReset(pcs.md_cb_recon_na[depth]);
            neighborArrayUnitReset(pcs.md_cr_recon_na[depth]);
        }
        if (pcs.hbd_md > 0 ||
            (pcs.scs_encoder_bit_depth > 8 && pcs.pic_bypass_encdec)) {
            neighborArrayUnitReset(pcs.md_luma_recon_na_16bit[depth]);
            neighborArrayUnitReset(pcs.md_tx_depth_1_luma_recon_na_16bit[depth]);
            neighborArrayUnitReset(pcs.md_tx_depth_2_luma_recon_na_16bit[depth]);
            neighborArrayUnitReset(pcs.md_cb_recon_na_16bit[depth]);
            neighborArrayUnitReset(pcs.md_cr_recon_na_16bit[depth]);
        }

        neighborArrayUnitReset(pcs.md_y_dcs_na[depth]);
        neighborArrayUnitReset(pcs.md_tx_depth_1_luma_dc_sign_level_coeff_na[depth]);
        neighborArrayUnitReset(pcs.md_cb_dc_sign_level_coeff_na[depth]);
        neighborArrayUnitReset(pcs.md_cr_dc_sign_level_coeff_na[depth]);
        neighborArrayUnitReset(pcs.md_txfm_context_array[depth]);
    }
}

// encode_pass_update_recon_sample_neighbour_arrays (coding_loop.c:96-:184).
void encodePassUpdateReconSampleNeighbourArrays(NeighborArrayUnit* lumaReconSampleNeighborArray,
                                                NeighborArrayUnit* cbReconSampleNeighborArray,
                                                NeighborArrayUnit* crReconSampleNeighborArray,
                                                const std::uint8_t* recon_y, std::uint32_t recon_y_stride,
                                                const std::uint8_t* recon_u, std::uint32_t recon_u_stride,
                                                const std::uint8_t* recon_v, std::uint32_t recon_v_stride,
                                                std::uint32_t org_x, std::uint32_t org_y, std::uint32_t width,
                                                std::uint32_t height, std::uint32_t bwidth_uv,
                                                std::uint32_t bheight_uv, std::uint32_t component_mask,
                                                bool is_16bit) {
    const std::uint32_t round_origin_x = (org_x & ~(kRoundUvX - 1));
    const std::uint32_t round_origin_y = (org_y & ~(kRoundUvX - 1));

    if (is_16bit) {
        if (component_mask & kPictureBufferDescLumaMask) {
            neighborArrayUnit16bitSampleWrite(lumaReconSampleNeighborArray,
                                              reinterpret_cast<const std::uint16_t*>(recon_y), recon_y_stride,
                                              org_x, org_y, org_x, org_y, width, height, kNaFullMask);
        }

        if (component_mask & kPictureBufferDescChromaMask) {
            neighborArrayUnit16bitSampleWrite(cbReconSampleNeighborArray,
                                              reinterpret_cast<const std::uint16_t*>(recon_u), recon_u_stride,
                                              round_origin_x >> 1, round_origin_y >> 1, round_origin_x >> 1,
                                              round_origin_y >> 1, bwidth_uv, bheight_uv, kNaFullMask);

            neighborArrayUnit16bitSampleWrite(crReconSampleNeighborArray,
                                              reinterpret_cast<const std::uint16_t*>(recon_v), recon_v_stride,
                                              round_origin_x >> 1, round_origin_y >> 1, round_origin_x >> 1,
                                              round_origin_y >> 1, bwidth_uv, bheight_uv, kNaFullMask);
        }
    } else {
        if (component_mask & kPictureBufferDescLumaMask) {
            neighborArrayUnitSampleWrite(lumaReconSampleNeighborArray, recon_y, recon_y_stride, org_x, org_y,
                                         org_x, org_y, width, height, kNaFullMask);
        }

        if (component_mask & kPictureBufferDescChromaMask) {
            neighborArrayUnitSampleWrite(cbReconSampleNeighborArray, recon_u, recon_u_stride,
                                         round_origin_x >> 1, round_origin_y >> 1, round_origin_x >> 1,
                                         round_origin_y >> 1, bwidth_uv, bheight_uv, kNaFullMask);

            neighborArrayUnitSampleWrite(crReconSampleNeighborArray, recon_v, recon_v_stride,
                                         round_origin_x >> 1, round_origin_y >> 1, round_origin_x >> 1,
                                         round_origin_y >> 1, bwidth_uv, bheight_uv, kNaFullMask);
        }
    }
}

// ---------------------------------------------------------------------------
// The sort / stage-count family
// ---------------------------------------------------------------------------

// definitions.h:788-:796 (verbatim).
const std::uint32_t mdStageNics[kNicsPicType][kCandClassTotal] = {
    {64, 0, 0, 64, 64},   // I SLICE
    {32, 32, 32, 32, 32}, // REF FRAMES
    {16, 16, 16, 16, 16}, // NON-REF FRAMES
};

// product_coding_loop.c:47 DIVIDE_AND_ROUND.
static std::uint32_t divideAndRound(std::uint32_t x, std::uint32_t y) {
    return (x + (y >> 1)) / y;
}

// svt_aom_set_nics (product_coding_loop.c:1299-:1333). The qp weights come
// from svt_aom_get_qp_based_th_scaling_factors (enc_mode_config.c:26-54)
// via the landed l16_rdo::mode_config mirror.
void setNics(const NicScalingCtrls& scaling_ctrls, std::uint32_t mds1_count[kCandClassTotal],
             std::uint32_t mds2_count[kCandClassTotal], std::uint32_t mds3_count[kCandClassTotal],
             std::uint8_t pic_type, std::uint32_t qp, std::uint8_t nic_max_qp_based_th_scaling) {
    for (std::uint32_t cidx = 0; cidx < kCandClassTotal; ++cidx) {
        mds1_count[cidx] = mdStageNics[pic_type][cidx];
        mds2_count[cidx] = mdStageNics[pic_type][cidx] >> 1;
        mds3_count[cidx] = mdStageNics[pic_type][cidx] >> 2;
    }

    const std::uint8_t min_mds1_nics = (pic_type < 2 && scaling_ctrls.stage1_scaling_num) ? 2 : 1;
    const std::uint8_t min_mds2_nics = (pic_type < 2 && scaling_ctrls.stage2_scaling_num) ? 2 : 1;
    const std::uint8_t min_mds3_nics = (pic_type < 2 && scaling_ctrls.stage3_scaling_num) ? 2 : 1;

    const std::uint32_t stage1_num = scaling_ctrls.stage1_scaling_num;
    const std::uint32_t stage2_num = scaling_ctrls.stage2_scaling_num;
    const std::uint32_t stage3_num = scaling_ctrls.stage3_scaling_num;
    const std::uint32_t scale_denum = kMdStageNicsScalDenum;
    for (std::uint32_t cidx = 0; cidx < kCandClassTotal; ++cidx) {
        mds1_count[cidx] = min_mds1_nics > divideAndRound(mds1_count[cidx] * stage1_num, scale_denum)
            ? min_mds1_nics
            : divideAndRound(mds1_count[cidx] * stage1_num, scale_denum);
        mds2_count[cidx] = min_mds2_nics > divideAndRound(mds2_count[cidx] * stage2_num, scale_denum)
            ? min_mds2_nics
            : divideAndRound(mds2_count[cidx] * stage2_num, scale_denum);
        mds3_count[cidx] = min_mds3_nics > divideAndRound(mds3_count[cidx] * stage3_num, scale_denum)
            ? min_mds3_nics
            : divideAndRound(mds3_count[cidx] * stage3_num, scale_denum);
    }
    std::uint32_t q_weight = 0;
    std::uint32_t q_weight_denom = 0;
    l16_rdo::mode_config::getQpBasedThScalingFactors(nic_max_qp_based_th_scaling != 0, &q_weight, &q_weight_denom,
                                                     qp);
    for (std::uint32_t cidx = 0; cidx < kCandClassTotal; ++cidx) {
        mds1_count[cidx] = min_mds1_nics > divideAndRound(mds1_count[cidx] * q_weight, q_weight_denom)
            ? min_mds1_nics
            : divideAndRound(mds1_count[cidx] * q_weight, q_weight_denom);
        mds2_count[cidx] = min_mds2_nics > divideAndRound(mds2_count[cidx] * q_weight, q_weight_denom)
            ? min_mds2_nics
            : divideAndRound(mds2_count[cidx] * q_weight, q_weight_denom);
        mds3_count[cidx] = min_mds3_nics > divideAndRound(mds3_count[cidx] * q_weight, q_weight_denom)
            ? min_mds3_nics
            : divideAndRound(mds3_count[cidx] * q_weight, q_weight_denom);
    }
}

// set_md_stage_counts (product_coding_loop.c:1335-:1354).
void setMdStageCounts(const NicScalingCtrls& scaling_ctrls, MdStageCountsCtx* ctx, std::uint8_t pic_type,
                      std::uint32_t qp, std::uint8_t nic_max_qp_based_th_scaling) {
    setNics(scaling_ctrls, ctx->md_stage_1_count, ctx->md_stage_2_count, ctx->md_stage_3_count, pic_type, qp,
            nic_max_qp_based_th_scaling);

    ctx->bypass_md_stage_1 =
        (ctx->md_staging_mode == MD_STAGING_MODE_1 || ctx->md_staging_mode == MD_STAGING_MODE_2) ? 0 : 1;
    ctx->bypass_md_stage_2 = (ctx->md_staging_mode == MD_STAGING_MODE_2) ? 0 : 1;
}

// sort_fast_cost_based_candidates (product_coding_loop.c:1356-:1377).
void sortFastCostBasedCandidates(const std::uint64_t* buffer_fast_costs, std::uint32_t input_buffer_start_idx,
                                 std::uint32_t input_buffer_count, std::uint32_t* cand_buff_indices) {
    const std::uint32_t input_buffer_end_idx = input_buffer_start_idx + input_buffer_count - 1;
    std::uint32_t buffer_index = 0;
    std::uint32_t k = 0;
    for (buffer_index = input_buffer_start_idx; buffer_index <= input_buffer_end_idx; ++buffer_index, ++k) {
        cand_buff_indices[k] = buffer_index;
    }
    for (std::uint32_t i = 0; i < input_buffer_count - 1; ++i) {
        for (std::uint32_t j = i + 1; j < input_buffer_count; ++j) {
            if (buffer_fast_costs[cand_buff_indices[j]] < buffer_fast_costs[cand_buff_indices[i]]) {
                buffer_index = cand_buff_indices[i];
                cand_buff_indices[i] = cand_buff_indices[j];
                cand_buff_indices[j] = buffer_index;
            }
        }
    }
}

// sort_full_cost_based_candidates (product_coding_loop.c:1379-:1393).
void sortFullCostBasedCandidates(const std::uint64_t* buffer_full_costs, std::uint32_t num_of_cand_to_sort,
                                 std::uint32_t* cand_buff_indices) {
    std::uint32_t index = 0;
    for (std::uint32_t i = 0; i < num_of_cand_to_sort - 1; ++i) {
        for (std::uint32_t j = i + 1; j < num_of_cand_to_sort; ++j) {
            if (buffer_full_costs[cand_buff_indices[j]] < buffer_full_costs[cand_buff_indices[i]]) {
                index = cand_buff_indices[i];
                cand_buff_indices[i] = cand_buff_indices[j];
                cand_buff_indices[j] = index;
            }
        }
    }
}

// construct_best_sorted_arrays_md_stage_3 (:1395-:1407).
void constructBestSortedArraysMdStage3(const std::uint32_t* const* cand_buff_indices_per_class,
                                       const std::uint32_t* md_stage_3_count,
                                       std::uint32_t* best_candidate_index_array,
                                       std::uint32_t md_stage_3_total_count) {
    std::uint32_t best_candi = 0;
    for (std::uint32_t class_i = 0; class_i < kCandClassTotal; ++class_i) {
        for (std::uint32_t candi = 0; candi < md_stage_3_count[class_i]; ++candi) {
            best_candidate_index_array[best_candi++] = cand_buff_indices_per_class[class_i][candi];
        }
    }
    (void)md_stage_3_total_count;
    assert(best_candi == md_stage_3_total_count);
}

// perform_ind_uv_search_last_mds (:1413-:1446). is_inter_mode =
// l16_rdo::isInterMode (rate_est.cpp, mode >= NEARESTMV); MAX_MODE_COST =
// coding_unit.h:37.
bool performIndUvSearchLastMds(const IndUvCandidate* buffer_candidates, const std::uint32_t* best_cand_idx_array,
                               std::uint32_t mds3_cand_count, const IndUvCtrls& ctrls) {
    std::uint16_t mds3_intra_count = 0;
    std::uint64_t best_intra_cost = kMaxModeCost;
    std::uint64_t best_inter_cost = kMaxModeCost;
    for (std::uint32_t i = 0; i < mds3_cand_count; ++i) {
        const std::uint32_t id = best_cand_idx_array[i];
        const bool is_inter =
            (l16_rdo::isInterMode(buffer_candidates[id].mode) || buffer_candidates[id].use_intrabc) ? true : false;
        mds3_intra_count += !is_inter &&
                (!ctrls.skip_ind_uv_if_only_dc || buffer_candidates[id].uv_mode != entropy::UV_DC_PRED)
            ? 1
            : 0;
        if (is_inter) {
            if (buffer_candidates[id].full_cost < best_inter_cost) {
                best_inter_cost = buffer_candidates[id].full_cost;
            }
        } else {
            if (buffer_candidates[id].full_cost < best_intra_cost) {
                best_intra_cost = buffer_candidates[id].full_cost;
            }
        }
    }

    if (ctrls.inter_vs_intra_cost_th &&
        (best_inter_cost * ctrls.inter_vs_intra_cost_th) < (best_intra_cost * 100)) {
        mds3_intra_count = 0;
    }

    return mds3_intra_count > 0;
}

// ---------------------------------------------------------------------------
// The MV / ME helpers
// ---------------------------------------------------------------------------

// mode_decision.c:279-:284 to_ref_frame + :282-:284 svt_get_ref_frame_type.
static const std::int32_t toRefFrame[2][4] = {
    {motion::amvp::kLastFrame, motion::amvp::kLast2Frame, motion::amvp::kLast3Frame, motion::amvp::kGoldenFrame},
    {motion::amvp::kBwdrefFrame, motion::amvp::kAltref2Frame, motion::amvp::kAltrefFrame,
     kInvalidRef}};
static std::int32_t getRefFrameType(std::uint8_t list, std::uint8_t ref_idx) {
    return toRefFrame[list][ref_idx];
}

// determine_best_references (product_coding_loop.c:65-:119). REF_LIST_0 =
// 0, REF_LIST_1 = 1 (definitions.h:2033-:2035).
void determineBestReferences(const BestRefPcs& pcs, const l16_rdo::MeSbResults* sb_results,
                             std::uint32_t me_block_offset, std::uint32_t me_cand_offset,
                             std::int32_t* ref_arr, std::uint8_t* tot_ref) {
    const std::uint8_t total_me_cnt = sb_results->totalMeCandidateIndex[me_block_offset];
    const l16_rdo::MeCandidate* me_results = &sb_results->meCandidateArray[me_cand_offset];

    std::uint8_t is_last_added = 0;
    std::uint8_t is_bwd_added = 0;
    std::uint8_t is_last_bwd_added = 0;

    std::uint8_t ri = 0;
    for (std::uint8_t me_index = 0; me_index < total_me_cnt; ++me_index) {
        const l16_rdo::MeCandidate* cand = &me_results[me_index];
        if (cand->direction == 0) {
            ref_arr[ri++] = getRefFrameType(0, cand->refIdxL0);

            is_last_added = (cand->refIdxL0 == 0) ? 1 : is_last_added;
        } else if (cand->direction == 1) {
            ref_arr[ri++] = getRefFrameType(1, cand->refIdxL1);

            is_bwd_added = (cand->refIdxL1 == 0) ? 1 : is_bwd_added;
        } else if (cand->direction == 2) {
            std::int32_t rf[2];
            rf[0] = getRefFrameType(cand->ref0List, cand->refIdxL0);
            rf[1] = getRefFrameType(cand->ref1List, cand->refIdxL1);

            {
                ref_arr[ri++] = motion::amvp::av1RefFrameType(rf);
                is_last_bwd_added =
                    (rf[0] == motion::amvp::kLastFrame && rf[1] == motion::amvp::kBwdrefFrame) ? 1
                                                                                              : is_last_bwd_added;
            }
        } else {
            assert(0 && "corrupted me resutls");
        }
    }

    if (pcs.slice_type == 2) {  // B_SLICE
        if (!is_last_added && pcs.ref_list0_count_try) {
            ref_arr[ri++] = motion::amvp::kLastFrame;
        }
        if (!is_bwd_added && pcs.ref_list1_count_try) {
            ref_arr[ri++] = motion::amvp::kBwdrefFrame;
        }
        if (!is_last_bwd_added && pcs.ref_list0_count_try && pcs.ref_list1_count_try) {
            ref_arr[ri++] = motion::amvp::kLastBwdFrame;
        }
    }
    *tot_ref = ri;
}

// clip_mv_on_pic_boundary (product_coding_loop.c:2195-:2212).
void clipMvOnPicBoundary(std::int32_t blk_org_x, std::int32_t blk_org_y, std::int32_t bwidth, std::int32_t bheight,
                         std::int32_t ref_max_width, std::int32_t ref_max_height, std::int32_t ref_border,
                         std::int16_t* mvx, std::int16_t* mvy) {
    if (blk_org_x + (*mvx >> 3) + bwidth > ref_max_width + ref_border) {
        *mvx = static_cast<std::int16_t>((ref_max_width - blk_org_x) * 8);
    }

    if (blk_org_y + (*mvy >> 3) + bheight > ref_max_height + ref_border) {
        *mvy = static_cast<std::int16_t>((ref_max_height - blk_org_y) * 8);
    }

    if (blk_org_x + (*mvx >> 3) < -ref_border) {
        *mvx = static_cast<std::int16_t>((-blk_org_x - bwidth) * 8);
    }

    if (blk_org_y + (*mvy >> 3) < -ref_border) {
        *mvy = static_cast<std::int16_t>((-blk_org_y - bheight) * 8);
    }
}

// check_spatial_mv_size (product_coding_loop.c:2219-:2241).
std::uint8_t checkSpatialMvSize(const Mv2* mvp_array, std::uint8_t mvp_count, const std::int16_t* me_mv_x,
                                const std::int16_t* me_mv_y) {
    std::uint8_t search_area_multiplier = 0;

    for (std::int8_t mvp_index = 0; mvp_index < static_cast<std::int8_t>(mvp_count); ++mvp_index) {
        if (mvp_array[mvp_index].x > kHighSpatialMvTh || mvp_array[mvp_index].y > kHighSpatialMvTh ||
            *me_mv_x > kHighSpatialMvTh || *me_mv_y > kHighSpatialMvTh) {
            search_area_multiplier = 3 > search_area_multiplier ? 3 : search_area_multiplier;
            return search_area_multiplier;
        } else if (mvp_array[mvp_index].x > kMediumSpatialMvTh || mvp_array[mvp_index].y > kMediumSpatialMvTh ||
                   *me_mv_x > kMediumSpatialMvTh || *me_mv_y > kMediumSpatialMvTh) {
            search_area_multiplier = 2 > search_area_multiplier ? 2 : search_area_multiplier;
        } else if (mvp_array[mvp_index].x > kLowSpatialMvTh || mvp_array[mvp_index].y > kLowSpatialMvTh ||
                   *me_mv_x > kLowSpatialMvTh || *me_mv_y > kLowSpatialMvTh) {
            search_area_multiplier = 1 > search_area_multiplier ? 1 : search_area_multiplier;
        }
    }
    return search_area_multiplier;
}

// check_temporal_mv_size (product_coding_loop.c:2248-:2265): the
// prev_frame_mvs TPL_MV_REF window flattened; INVALID_MV = mv.h:21.
std::uint8_t checkTemporalMvSize(std::uint32_t mfmv0_as_int, std::int16_t mfmv0_x, std::int16_t mfmv0_y) {
    std::uint8_t search_area_multiplier = 0;

    if (mfmv0_as_int != motion::amvp::kInvalidMv) {
        const std::int32_t abs_y = mfmv0_y < 0 ? -mfmv0_y : mfmv0_y;
        const std::int32_t abs_x = mfmv0_x < 0 ? -mfmv0_x : mfmv0_x;
        if (abs_y > kMediumTemporalMvTh || abs_x > kMediumTemporalMvTh) {
            search_area_multiplier = 2 > search_area_multiplier ? 2 : search_area_multiplier;
        } else if (abs_y > kLowTemporalMvTh || abs_x > kLowTemporalMvTh) {
            search_area_multiplier = 1 > search_area_multiplier ? 1 : search_area_multiplier;
        }
    }

    return search_area_multiplier;
}

// derive_me_offsets (product_coding_loop.c:1992-:2017) over the landed
// l16_rdo::getMeBlockOffset (mode_decision.c:132-:189).
void deriveMeOffsets(const DeriveMeOffsetsInput& in, DeriveMeOffsetsCtx* ctx) {
    ctx->geom_offset_x = 0;
    ctx->geom_offset_y = 0;

    if (in.sb_size_is_128) {
        const std::uint32_t me_sb_size = in.b64_size;
        const std::uint32_t me_pic_width_in_sb =
            (in.aligned_width + in.b64_size - 1) / me_sb_size;
        const std::uint32_t me_sb_x = in.blk_org_x / me_sb_size;
        const std::uint32_t me_sb_y = in.blk_org_y / me_sb_size;
        ctx->me_sb_addr = me_sb_x + me_sb_y * me_pic_width_in_sb;
        ctx->geom_offset_x = (me_sb_x & 0x1) * me_sb_size;
        ctx->geom_offset_y = (me_sb_y & 0x1) * me_sb_size;
        ctx->me_block_offset = l16_rdo::getMeBlockOffset(in.blk_org_x, in.blk_org_y, static_cast<l16_rdo::BlockSize>(in.bsize), in.enable_me_8x8,
                                                         in.enable_me_16x16);
    } else {
        ctx->me_sb_addr = in.sb_ptr_index;

        ctx->me_block_offset = l16_rdo::getMeBlockOffset(in.blk_org_x, in.blk_org_y, static_cast<l16_rdo::BlockSize>(in.bsize), in.enable_me_8x8,
                                                         in.enable_me_16x16);
    }

    assert(ctx->me_block_offset != static_cast<std::uint32_t>(-1));
    ctx->me_cand_offset = ctx->me_block_offset * in.max_cand;
}

// rd_cost.c:47-53 svt_av1_get_mv_joint.
MvJointType av1GetMvJoint(const Mv2& mv) {
    if (mv.y == 0) {
        return mv.x == 0 ? MV_JOINT_ZERO : MV_JOINT_HNZVZ;
    }
    return mv.x == 0 ? MV_JOINT_HZVNZ : MV_JOINT_HNZVNZ;
}

// rd_cost.c:55-60 mv_cost / mcomp.h:138-142 svt_mv_cost. MV_LOW/MV_UPP =
// cabac_context_model.h:197-199 (kMvLow/kMvUpp, the l5 carrier).
static std::int32_t clampIntLocal(std::int32_t value, std::int32_t low, std::int32_t high) {
    return value < low ? low : (value > high ? high : value);
}
std::int32_t mvCost(const Mv2& mv, const std::int32_t* joint_cost, const std::int32_t* const* comp_cost) {
    return joint_cost[av1GetMvJoint(mv)] + comp_cost[0][clampIntLocal(mv.y, motion::amvp::kMvLow, motion::amvp::kMvUpp)] +
        comp_cost[1][clampIntLocal(mv.x, motion::amvp::kMvLow, motion::amvp::kMvUpp)];
}

// mcomp.h:42-72 svt_mv_err_cost (verbatim; ROUND_POWER_OF_TWO_64 via the
// local round helper).
std::int32_t mvErrCost(const Mv2& mv, const Mv2& ref_mv, const std::int32_t* mvjcost,
                       const std::int32_t* const* mvcost, std::int32_t error_per_bit, MvCostType mv_cost_type) {
    const std::int32_t diff_x = mv.x - ref_mv.x;
    const std::int32_t diff_y = mv.y - ref_mv.y;
    const std::int32_t abs_diff_x = diff_x < 0 ? -diff_x : diff_x;
    const std::int32_t abs_diff_y = diff_y < 0 ? -diff_y : diff_y;
    Mv2 diff;
    diff.x = static_cast<std::int16_t>(diff_x);
    diff.y = static_cast<std::int16_t>(diff_y);

    switch (mv_cost_type) {
    case MV_COST_ENTROPY:
        if (mvcost) {
            return static_cast<std::int32_t>(roundPowerOfTwo64(
                static_cast<std::int64_t>(mvCost(diff, mvjcost, mvcost)) * error_per_bit,
                kRddivBits + kAv1ProbCostShift - kRdEpbShift + kPixelTransformErrorScale));
        }
        return 0;
    case MV_COST_L1_LOWRES:
        return (kSseLambdaLowres * (abs_diff_y + abs_diff_x)) >> 3;
    case MV_COST_L1_MIDRES:
        return (kSseLambdaMidres * (abs_diff_y + abs_diff_x)) >> 3;
    case MV_COST_L1_HDRES:
        return (kSseLambdaHdres * (abs_diff_y + abs_diff_x)) >> 3;
    case MV_COST_OPT:
        return static_cast<std::int32_t>(roundPowerOfTwo64(
            static_cast<std::int64_t>((abs_diff_y + abs_diff_x) << 8) * error_per_bit,
            kRddivBits + kAv1ProbCostShift - kRdEpbShift + kPixelTransformErrorScale));
    case MV_COST_NONE:
        return 0;
    default:
        assert(0 && "Invalid rd_cost_type");
        return 0;
    }
}

// mcomp.h:74-81 svt_mv_err_cost_.
std::int32_t fpMvErrCost(const Mv2& mv, const SvtMvCostParam& params) {
    return mvErrCost(mv, *params.ref_mv, params.mvjcost, params.mvcost, params.error_per_bit, params.mv_cost_type);
}

// mv.h:56-63.
Mv2 getFullmvFromMv(const Mv2& subpel_mv) {
    Mv2 full_mv;
    full_mv.x = static_cast<std::int16_t>(((subpel_mv.x) + 3 + ((subpel_mv.x) >= 0)) >> 3);
    full_mv.y = static_cast<std::int16_t>(((subpel_mv.y) + 3 + ((subpel_mv.y) >= 0)) >> 3);
    return full_mv;
}

// svt_init_mv_cost_params (product_coding_loop.c:1842-:1853).
void svtInitMvCostParams(const InitMvCostParamsCtx& ctx, SvtMvCostParam* mv_cost_params, const Mv2* ref_mv,
                         std::uint8_t base_q_idx, std::uint32_t rdmult, std::uint8_t hbd_md) {
    mv_cost_params->ref_mv = ref_mv;
    mv_cost_params->full_ref_mv = getFullmvFromMv(*ref_mv);
    mv_cost_params->early_exit_th = 1020 - (ctx.sq_size >> 2);
    mv_cost_params->mv_cost_type = ctx.skip_diag_refinement >= 3 ? MV_COST_OPT : MV_COST_ENTROPY;
    const std::uint32_t epb = rdmult >> kRdEpbShift;
    mv_cost_params->error_per_bit = epb > 1 ? static_cast<std::int32_t>(epb) : 1;  // AOMMAX(.., 1)
    mv_cost_params->sad_per_bit = getSadPerBit(base_q_idx, hbd_md);
    mv_cost_params->mvjcost = ctx.nmv_vec_cost;
    mv_cost_params->mvcost[0] = ctx.nmvcoststack0;
    mv_cost_params->mvcost[1] = ctx.nmvcoststack1;
}

// mode_decision.c:2086-:2103 get_sad_per_bit + init_me_luts_bd (:2090-:2099)
// + svt_av1_convert_qindex_to_q (rc_process.c:185-:198). The runtime-init
// lut becomes the formula call - the same (int)(0.0418 * q + 2.4107) value.
std::int32_t getSadPerBit(int qidx, int is_hbd) {
    const double q = is_hbd ? l15_rc::acQuantQtx(qidx, 0, 1) / 16.0 : l15_rc::acQuantQtx(qidx, 0, 0) / 4.0;
    return static_cast<int>(0.0418 * q + 2.4107);
}

// definitions.h EB_ABS_DIFF (the uint8 pair).
static std::uint32_t ebAbsDiff(std::uint8_t a, std::uint8_t b) {
    return a > b ? static_cast<std::uint32_t>(a - b) : static_cast<std::uint32_t>(b - a);
}

// svt_pme_sad_loop_kernel_c (product_coding_loop.c:1716-:1769).
void svtPmeSadLoopKernel(const SvtMvCostParam* mv_cost_params, const std::uint8_t* src, std::uint32_t src_stride,
                         const std::uint8_t* ref, std::uint32_t ref_stride, std::uint32_t block_height,
                         std::uint32_t block_width, std::uint32_t* best_cost, std::int16_t* best_mvx,
                         std::int16_t* best_mvy, std::int16_t search_position_start_x,
                         std::int16_t search_position_start_y, std::int16_t search_area_width,
                         std::int16_t search_area_height, std::int16_t search_step, std::int16_t mvx,
                         std::int16_t mvy) {
    std::int16_t x_search_index = 0;
    std::int16_t y_search_index = 0;
    std::int16_t col_num = 0;
    std::int16_t search_step_x = 1;
    for (y_search_index = 0; y_search_index < search_area_height; y_search_index += search_step) {
        for (x_search_index = 0; x_search_index < search_area_width; x_search_index += search_step_x) {
            if (((search_area_width - x_search_index) < 8) && (col_num == 0)) {
                continue;
            }
            if (col_num == 7) {
                col_num = 0;
                search_step_x = search_step;
            } else {
                ++col_num;
                search_step_x = 1;
            }
            std::uint32_t x = 0;
            std::uint32_t y = 0;
            std::uint32_t cost = 0;

            for (y = 0; y < block_height; ++y) {
                for (x = 0; x < block_width; ++x) {
                    cost += ebAbsDiff(src[y * src_stride + x],
                                      ref[x_search_index + y * ref_stride + x]);
                }
            }

            std::uint32_t refinement_pos_x =
                static_cast<std::uint32_t>(search_position_start_x + x_search_index);
            std::uint32_t refinement_pos_y =
                static_cast<std::uint32_t>(search_position_start_y + y_search_index);
            Mv2 best_mv;
            best_mv.x = static_cast<std::int16_t>(mvx + (refinement_pos_x * 8));
            best_mv.y = static_cast<std::int16_t>(mvy + (refinement_pos_y * 8));
            cost += static_cast<std::uint32_t>(fpMvErrCost(best_mv, *mv_cost_params));
            if (cost < *best_cost) {
                *best_mvx = static_cast<std::int16_t>(mvx + (refinement_pos_x * 8));
                *best_mvy = static_cast<std::int16_t>(mvy + (refinement_pos_y * 8));
                *best_cost = cost;
            }
        }

        ref += static_cast<ptrdiff_t>(search_step) * ref_stride;
    }
}

// ---------------------------------------------------------------------------
// The lambda chain (rc_process.c:346-:464)
// ---------------------------------------------------------------------------

// rc_process.c:346-:362.
double defInterRdMultiplier(int qindex) {
    return 3.2 + 0.0015 * qindex;
}
double defArfRdMultiplier(int qindex) {
    return 3.25 + 0.0015 * qindex;
}
double defKfRdMultiplier(int qindex) {
    return 3.3 + 0.0015 * qindex;
}

// rc_process.c:364-:392.
std::int32_t computeRdMultBasedOnQindex(int bit_depth, int update_type, int qindex) {
    const int q = l15_rc::dcQuantQtx(qindex, 0, bit_depth);
    std::int64_t rdmult = 0;

    if (update_type == SVT_AV1_KF_UPDATE) {
        rdmult = static_cast<std::int64_t>(defKfRdMultiplier(q) * q * q);
    } else if (update_type == SVT_AV1_GF_UPDATE || update_type == SVT_AV1_ARF_UPDATE) {
        rdmult = static_cast<std::int64_t>(defArfRdMultiplier(q) * q * q);
    } else {
        rdmult = static_cast<std::int64_t>(defInterRdMultiplier(q) * q * q);
    }

    switch (bit_depth) {
    case 0:  // EB_EIGHT_BIT
        break;
    case 1:  // EB_TEN_BIT
        rdmult = roundPowerOfTwo64(rdmult, 4);
        break;
    case 2:  // EB_TWELVE_BIT
        rdmult = roundPowerOfTwo64(rdmult, 8);
        break;
    default:
        assert(0 && "bit_depth should be EB_EIGHT_BIT, EB_TEN_BIT or EB_TWELVE_BIT");
        return -1;
    }

    return rdmult > 0
        ? static_cast<std::int32_t>(rdmult < 2147483647LL ? rdmult : 2147483647LL)
        : 1;
}

// rc_process.c:394-:395 (verbatim) + :398-:441 update_lambda.
const int rdFrameTypeFactor[2][7] = {{150, 180, 150, 150, 180, 180, 150},
                                     {128, 144, 128, 128, 144, 144, 128}};
std::uint32_t updateLambda(const LambdaPcs& pcs, std::uint8_t q_index, std::uint8_t me_q_index, int bit_depth,
                           std::int64_t rdmult) {
    // gf_update_type per :407-:410.
    const std::uint8_t gf_update_type = pcs.frame_type == 0
        ? static_cast<std::uint8_t>(SVT_AV1_KF_UPDATE)
        : pcs.temporal_layer_index == 0
            ? static_cast<std::uint8_t>(SVT_AV1_ARF_UPDATE)
            : pcs.temporal_layer_index < pcs.hierarchical_levels
                ? static_cast<std::uint8_t>(SVT_AV1_INTNL_ARF_UPDATE)
                : static_cast<std::uint8_t>(SVT_AV1_LF_UPDATE);
    rdmult = (rdmult * rdFrameTypeFactor[bit_depth != 0][gf_update_type]) >> 7;
    if (pcs.rtc && pcs.frame_type == 0) {
        rdmult = (rdmult * 100) >> 7;  // RTC_KF_LAMBDA_BOOST
    }
    if (pcs.stats_based_sb_lambda_modulation) {
        int factor = 128;
        if (pcs.rtc) {
            const int qdiff = me_q_index - pcs.base_q_idx;
            if (qdiff < 0) {
                factor = (qdiff <= -4) ? 100 : 115;
            }
        } else if (pcs.delta_q_present || pcs.r0_delta_qp_md) {
            const int qdiff = q_index - pcs.base_q_idx;
            if (qdiff < 0) {
                factor = (qdiff <= -8) ? 90 : 115;
            } else if (qdiff > 0) {
                factor = (qdiff <= 8) ? 135 : 150;
            }
        } else {
            const int qdiff = me_q_index - pcs.base_q_idx;
            if (qdiff < 0) {
                factor = (qdiff <= -4) ? 100 : 115;
            } else if (qdiff > 0) {
                factor = (qdiff <= 4) ? 135 : 150;
            }
        }

        rdmult = (rdmult * factor) >> 7;
    }
    return static_cast<std::uint32_t>(rdmult);
}

// lambda_rate_tables.h:23-:39 (verbatim; the 10/12-bit twins are dead
// under the pinned build's CONFIG_ENABLE_HIGH_BIT_DEPTH=0,
// lambda_rate_tables.h:41).
const std::uint32_t lambdaModeDecision8BitSad[256] = {
    86, 173, 173, 194, 216, 238, 259, 259, 281, 303, 324, 346, 368, 389, 411, 411,
    433, 454, 476, 498, 519, 541, 563, 563, 584, 606, 628, 649, 671, 693, 693, 714,
    736, 758, 779, 801, 823, 823, 844, 866, 888, 909, 931, 931, 953, 974, 996, 1018,
    1039, 1039, 1061, 1083, 1104, 1126, 1148, 1148, 1169, 1191, 1213, 1234, 1234, 1256, 1278, 1299,
    1321, 1343, 1343, 1364, 1386, 1408, 1429, 1429, 1451, 1473, 1494, 1516, 1516, 1538, 1559, 1581,
    1603, 1603, 1624, 1646, 1668, 1689, 1689, 1711, 1733, 1754, 1754, 1776, 1798, 1819, 1841, 1841,
    1884, 1906, 1949, 1993, 2014, 2058, 2079, 2123, 2144, 2188, 2209, 2253, 2274, 2318, 2339, 2383,
    2404, 2448, 2469, 2513, 2534, 2556, 2599, 2621, 2664, 2707, 2751, 2794, 2837, 2902, 2946, 2989,
    3032, 3076, 3119, 3162, 3206, 3249, 3292, 3336, 3379, 3422, 3487, 3552, 3596, 3661, 3726, 3769,
    3834, 3899, 3942, 4007, 4051, 4116, 4159, 4224, 4311, 4376, 4441, 4506, 4571, 4636, 4701, 4766,
    4831, 4896, 4982, 5047, 5134, 5199, 5264, 5351, 5416, 5481, 5567, 5654, 5740, 5827, 5892, 5979,
    6065, 6152, 6239, 6325, 6412, 6499, 6585, 6694, 6780, 6867, 6975, 7062, 7149, 7257, 7365, 7452,
    7560, 7669, 7777, 7885, 7994, 8102, 8210, 8319, 8427, 8557, 8665, 8795, 8903, 9033, 9163, 9293,
    9423, 9553, 9683, 9835, 9987, 10117, 10290, 10442, 10593, 10767, 10940, 11113, 11308, 11481, 11676, 11893,
    12110, 12326, 12543, 12781, 13041, 13301, 13561, 13865, 14168, 14471, 14818, 15164, 15533, 15944, 16356, 16789,
    17244, 17742, 18262, 18826, 19411, 20039, 20689, 21404, 22140, 22920, 23787, 24675, 25650, 26690, 27773, 28943};

// rc_process.c:446-:451.
std::uint32_t computeRdMult(const LambdaPcs& pcs, std::uint8_t q_index, std::uint8_t me_q_index, int bit_depth) {
    const std::int64_t rdmult = computeRdMultBasedOnQindex(bit_depth, pcs.update_type, q_index);
    return updateLambda(pcs, q_index, me_q_index, bit_depth, rdmult);
}

// rc_process.c:453-:464 (the CONFIG=0 fold: 8-bit only).
std::uint32_t computeFastLambda(const LambdaPcs& pcs, std::uint8_t q_index, std::uint8_t me_q_index, int bit_depth) {
    const std::int64_t rdmult = lambdaModeDecision8BitSad[q_index];
    return updateLambda(pcs, q_index, me_q_index, bit_depth, rdmult);
}

// av1_lambda_assign_md (md_process.c:730-:765).
void av1LambdaAssignMd(const LambdaAssignPcs& pcs, LambdaAssignCtx* ctx) {
    // compute_rd_mult/fast_lambda(pcs, qp_index, me_q_index, bit_depth)
    // (:731-:734): the update_lambda surface (rc_process.c:398-:441) reads
    // ppcs->frm_hdr.frame_type / ppcs->temporal_layer_index /
    // ppcs->hierarchical_levels + the scs/frm_hdr fields the carrier
    // threads.
    LambdaPcs lambda_pcs;
    lambda_pcs.update_type = pcs.update_type;
    lambda_pcs.frame_type = pcs.ppcs_frame_type;
    lambda_pcs.temporal_layer_index = pcs.ppcs_temporal_layer_index;
    lambda_pcs.hierarchical_levels = pcs.ppcs_hierarchical_levels;
    lambda_pcs.rtc = pcs.rtc;
    lambda_pcs.stats_based_sb_lambda_modulation = pcs.stats_based_sb_lambda_modulation;
    lambda_pcs.delta_q_present = pcs.delta_q_present;
    lambda_pcs.r0_delta_qp_md = pcs.r0_delta_qp_md;
    lambda_pcs.base_q_idx = pcs.base_q_idx;

    ctx->full_lambda_md[0] = computeRdMult(lambda_pcs, ctx->qp_index, ctx->me_q_index, 0);
    ctx->fast_lambda_md[0] = computeFastLambda(lambda_pcs, ctx->qp_index, ctx->me_q_index, 0);
    ctx->full_lambda_md[1] = computeRdMult(lambda_pcs, ctx->qp_index, ctx->me_q_index, 1);
    ctx->fast_lambda_md[1] = computeFastLambda(lambda_pcs, ctx->qp_index, ctx->me_q_index, 1);

    if (!pcs.rtc && pcs.stats_based_sb_lambda_modulation) {
        if (pcs.pcs_temporal_layer_index > 0) {
            if (pcs.ref_intra_percentage < kLambdaModIntraTh) {
                ctx->full_lambda_md[0] = (ctx->full_lambda_md[0] * kLambdaModIntraScalingFactor) >> 7;
                ctx->fast_lambda_md[0] = (ctx->fast_lambda_md[0] * kLambdaModIntraScalingFactor) >> 7;
                ctx->full_lambda_md[1] = (ctx->full_lambda_md[1] * kLambdaModIntraScalingFactor) >> 7;
                ctx->fast_lambda_md[1] = (ctx->fast_lambda_md[1] * kLambdaModIntraScalingFactor) >> 7;
            }
        }
    }

    if (pcs.lambda_weight) {
        ctx->full_lambda_md[0] = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(ctx->full_lambda_md[0]) * pcs.lambda_weight) >> 7);
        ctx->fast_lambda_md[0] = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(ctx->fast_lambda_md[0]) * pcs.lambda_weight) >> 7);
        ctx->full_lambda_md[1] = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(ctx->full_lambda_md[1]) * pcs.lambda_weight) >> 7);
        ctx->fast_lambda_md[1] = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(ctx->fast_lambda_md[1]) * pcs.lambda_weight) >> 7);
    }
    ctx->full_lambda_md[1] *= 16;
    ctx->fast_lambda_md[1] *= 4;

    ctx->full_lambda_md[0] = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(ctx->full_lambda_md[0]) * pcs.lambda_scale_factor) >> 7);
    ctx->full_lambda_md[1] = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(ctx->full_lambda_md[1]) * pcs.lambda_scale_factor) >> 7);
    ctx->fast_lambda_md[0] = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(ctx->fast_lambda_md[0]) * pcs.lambda_scale_factor) >> 7);
    ctx->fast_lambda_md[1] = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(ctx->fast_lambda_md[1]) * pcs.lambda_scale_factor) >> 7);

    ctx->full_sb_lambda_md[0] = ctx->full_lambda_md[0];
    ctx->full_sb_lambda_md[1] = ctx->full_lambda_md[1];
}

// ---------------------------------------------------------------------------
// setup_mds / setup_pc_tree (md_process.c:158-:211)
// ---------------------------------------------------------------------------

// setup_mds (md_process.c:158-:185): the flat-tree recursion; split[i] =
// cur + i*blocks_to_skip + 1 (the reference's mds + i*blocks_to_skip + 1);
// the port stores the absolute index (the base pointer travels along).
static void setupMdsRec(const l14_utility::BlockGeom* geom_table, const MdScan* base, MdScan* mds,
                        std::uint32_t* mds_idx, int index, entropy::BlockSize bsize, const int min_sq_size) {
    mds->mds_idx = *mds_idx;
    mds->bsize = bsize;
    mds->index = static_cast<std::uint32_t>(index);

    const l14_utility::BlockGeom* blk_geom = &geom_table[*mds_idx];
    const int sq_size = entropy::blockSizeWide[bsize];
    if (sq_size > min_sq_size) {
        const entropy::BlockSize subsize = getPartitionSubsize(bsize, entropy::PARTITION_SPLIT);
        const int sq_subsize = entropy::blockSizeWide[subsize];
        int blocks_per_subdepth = (sq_subsize / min_sq_size) * (sq_subsize / min_sq_size);
        int blocks_to_skip = 0;

        for (int i = min_sq_size; i <= sq_subsize; i <<= 1, blocks_per_subdepth >>= 2) {
            blocks_to_skip += blocks_per_subdepth;
        }

        *mds_idx += blk_geom->d1_depth_offset;
        for (int i = 0; i < 4; ++i) {
            mds->split[i] = static_cast<std::uint32_t>((mds + i * blocks_to_skip + 1) - base);
            setupMdsRec(geom_table, base, mds + i * blocks_to_skip + 1, mds_idx, i, subsize, min_sq_size);
        }
        mds->has_split = 1;
    } else {
        *mds_idx += blk_geom->ns_depth_offset;
        mds->has_split = 0;
        for (int i = 0; i < 4; ++i) {
            mds->split[i] = 0;
        }
    }
}

void setupMds(entropy::BlockSize bsize, const int min_sq_size, const l14_utility::BlockGeom* geom_table,
              MdScan* mds, std::uint32_t* mds_idx, int index) {
    setupMdsRec(geom_table, mds, mds, mds_idx, index, bsize, min_sq_size);
}

// setup_pc_tree (md_process.c:187-:211): the flat-tree recursion over the
// caller's PC_TREE storage + the tested_blk row bases; split[i] = cur +
// i*blocks_to_skip + 1 with index = i (the reference's child call).
static void setupPcTreeRec(PcTree* base, PcTree* pc_tree, std::uint32_t tested_blk_base, int index,
                           entropy::BlockSize bsize, const int min_sq_size) {
    pc_tree->bsize = bsize;
    pc_tree->index = static_cast<std::uint32_t>(index);
    pc_tree->tested_blk_base = tested_blk_base;

    const int sq_size = entropy::blockSizeWide[bsize];
    if (sq_size > min_sq_size) {
        const entropy::BlockSize subsize = getPartitionSubsize(bsize, entropy::PARTITION_SPLIT);
        const int sq_subsize = entropy::blockSizeWide[subsize];
        int blocks_per_subdepth = (sq_subsize / min_sq_size) * (sq_subsize / min_sq_size);
        int blocks_to_skip = 0;

        for (int i = min_sq_size; i <= sq_subsize; i <<= 1, blocks_per_subdepth >>= 2) {
            blocks_to_skip += blocks_per_subdepth;
        }

        for (int i = 0; i < 4; ++i) {
            pc_tree->split[i] = static_cast<std::uint32_t>((pc_tree + i * blocks_to_skip + 1) - base);
            setupPcTreeRec(base, pc_tree + i * blocks_to_skip + 1,
                           tested_blk_base + static_cast<std::uint32_t>(i * blocks_to_skip + 1), i, subsize,
                           min_sq_size);
        }
        pc_tree->has_split = 1;
    } else {
        pc_tree->has_split = 0;
        for (int i = 0; i < 4; ++i) {
            pc_tree->split[i] = 0;
        }
    }
}

void setupPcTree(PcTree* pc_tree, std::uint32_t tested_blk_base, int index, entropy::BlockSize bsize,
                 const int min_sq_size) {
    setupPcTreeRec(pc_tree, pc_tree, tested_blk_base, index, bsize, min_sq_size);
}

// ---------------------------------------------------------------------------
// coding_loop.c misc
// ---------------------------------------------------------------------------

// svt_aom_residual_kernel (coding_loop.c:69-:91): the hbd fold over the
// PO1 kernels. The offsets advance in uint16 ELEMENTS on the hbd path
// (the reference's cast + offset arithmetic).
void residualKernel(const std::uint8_t* input, std::uint32_t input_offset, std::uint32_t input_stride,
                    const std::uint8_t* pred, std::uint32_t pred_offset, std::uint32_t pred_stride,
                    std::int16_t* residual, std::uint32_t residual_offset, std::uint32_t residual_stride,
                    bool hbd, std::uint32_t area_width, std::uint32_t area_height) {
    if (hbd) {
        picops::residualKernel16bit(reinterpret_cast<const std::uint16_t*>(input) + input_offset, input_stride,
                                    reinterpret_cast<const std::uint16_t*>(pred) + pred_offset, pred_stride,
                                    residual + residual_offset, residual_stride, area_width, area_height);
    } else {
        picops::residualKernel8bit(&input[input_offset], input_stride, &pred[pred_offset], pred_stride,
                                   residual + residual_offset, residual_stride, area_width, area_height);
    }
}

// av1_copy_frame_mvs (coding_loop.c:1038-:1070). The MV_REF two-word
// {ref_frame; mv.as_int}: the out pair is the ref_frame ordinal array +
// the packed as_int array; ref_frame_side is pcs->ref_frame_side (the
// definitions.h:2032 [MAX_NUM_OF_REF_PIC_LIST][REF_LIST_MAX_DEPTH]
// flattened per ref_frame index, block_structures.h).
void av1CopyFrameMvs(const CopyFrameMvsMi& mi, std::int32_t mi_row, std::int32_t mi_col, int x_mis, int y_mis,
                     std::int32_t* frame_mvs_ref_frame, std::uint32_t* frame_mvs_mv, std::int32_t frame_mvs_stride,
                     const std::int8_t* ref_frame_side) {
    std::int32_t* frame_mvs = frame_mvs_ref_frame +
        static_cast<std::uint32_t>((mi_row >> 1) * frame_mvs_stride + (mi_col >> 1));
    std::uint32_t* frame_mvs_int =
        frame_mvs_mv + static_cast<std::uint32_t>((mi_row >> 1) * frame_mvs_stride + (mi_col >> 1));
    const int x_mis_half = roundPowerOfTwo32(x_mis, 1);
    const int y_mis_half = roundPowerOfTwo32(y_mis, 1);
    int w = 0;
    int h = 0;

    for (h = 0; h < y_mis_half; ++h) {
        std::int32_t* mv = frame_mvs;
        std::uint32_t* mv_int = frame_mvs_int;
        for (w = 0; w < x_mis_half; ++w) {
            mv[0] = kNoneFrame;
            mv_int[0] = 0;

            for (int idx = 0; idx < 2; ++idx) {
                const std::int32_t ref_frame = mi.ref_frame[idx];
                if (ref_frame > 0) {  // > INTRA_FRAME
                    const std::int8_t ref_idx = ref_frame_side[ref_frame];
                    if (ref_idx) {
                        continue;
                    }
                    const std::int32_t abs_y = mi.mv[idx][1] < 0 ? -mi.mv[idx][1] : mi.mv[idx][1];
                    const std::int32_t abs_x = mi.mv[idx][0] < 0 ? -mi.mv[idx][0] : mi.mv[idx][0];
                    if ((abs_y > kRefmvsLimit) || (abs_x > kRefmvsLimit)) {
                        continue;
                    }
                    mv[0] = ref_frame;
                    mv_int[0] = static_cast<std::uint32_t>(static_cast<std::uint16_t>(mi.mv[idx][0])) |
                        (static_cast<std::uint32_t>(static_cast<std::uint16_t>(mi.mv[idx][1])) << 16);
                }
            }
            ++mv;
            ++mv_int;
        }
        frame_mvs += frame_mvs_stride;
        frame_mvs_int += frame_mvs_stride;
    }
}

// copy_qcoeffs (coding_loop.c:1429-:1465).
void copyQcoeffs(const CopyQcoeffsCtx& ctx, entropy::BlockSize bsize, std::uint8_t tx_depth,
                 const std::int32_t* ep_coeff_y, const std::int32_t* md_coeff_y, const std::int32_t* ep_coeff_u,
                 const std::int32_t* md_coeff_u, const std::int32_t* ep_coeff_v, const std::int32_t* md_coeff_v,
                 std::uint16_t y_has_coeff, std::uint8_t u_has_coeff, std::uint8_t v_has_coeff) {
    const std::uint8_t txb_itr = ctx.txb_itr;
    const std::uint8_t uv_pass = tx_depth && txb_itr ? 0 : 1;

    if ((y_has_coeff & (1 << txb_itr))) {
        const entropy::TxSize tx_size = entropy::txDepthToTxSize[tx_depth][bsize];
        const int tx_width = entropy::txSizeWide[tx_size];
        const int tx_height = entropy::txSizeHigh[tx_size];
        std::int32_t* ep_dst = const_cast<std::int32_t*>(ep_coeff_y);
        std::memcpy(ep_dst, md_coeff_y, sizeof(std::int32_t) * static_cast<std::size_t>(tx_height * tx_width));
    }

    if (ctx.has_uv && uv_pass) {
        const entropy::TxSize tx_size_uv = av1GetMaxUvTxsize(bsize, 1, 1);
        const int tx_width_uv = entropy::txSizeWide[tx_size_uv];
        const int tx_height_uv = entropy::txSizeHigh[tx_size_uv];

        if ((u_has_coeff & (1 << txb_itr))) {
            std::int32_t* ep_dst = const_cast<std::int32_t*>(ep_coeff_u);
            std::memcpy(ep_dst, md_coeff_u,
                        sizeof(std::int32_t) * static_cast<std::size_t>(tx_height_uv * tx_width_uv));
        }

        if ((v_has_coeff & (1 << txb_itr))) {
            std::int32_t* ep_dst = const_cast<std::int32_t*>(ep_coeff_v);
            std::memcpy(ep_dst, md_coeff_v,
                        sizeof(std::int32_t) * static_cast<std::size_t>(tx_height_uv * tx_width_uv));
        }
    }
}

// svt_aom_txb_estimate_coeff_bits (rd_cost.c:1233-:1328), flattened onto
// the landed RE3 carriers (l16_rdo::costCoeffsTxb / costSkipTxb). The
// cand_bf->cand->block_mi.mode / filter_intra_mode travel as the two
// leading ints; the luma/chroma bits are the out values.
static void estimateCoeffBits(std::uint8_t allow_update_cdf, const int lumaMode, const int filterIntraMode,
                              const l16_rdo::MdRateEstimationContext* me, l16_rdo::FcTokenCdfs* ec_ctx,
                              l16_rdo::FcSyntaxCdfs* ec_syntax_ctx, const std::int32_t* y_coeff,
                              const std::int32_t* cb_coeff, const std::int32_t* cr_coeff, std::uint32_t y_eob,
                              std::uint32_t cb_eob, std::uint32_t cr_eob, entropy::TxSize txsize,
                              entropy::TxSize txsize_uv, entropy::TxType tx_type, entropy::TxType tx_type_uv,
                              int component_type, std::int32_t luma_txb_skip_context,
                              std::int32_t luma_dc_sign_context, std::int32_t cb_txb_skip_context,
                              std::int32_t cb_dc_sign_context, std::int32_t cr_txb_skip_context,
                              std::int32_t cr_dc_sign_context, bool reduced_tx_set, std::uint8_t* levels_buf,
                              std::int8_t* coeff_contexts, std::int32_t fast_coeff_est_level,
                              std::int32_t subres_step, std::uint64_t* y_txb_coeff_bits,
                              std::uint64_t* cb_txb_coeff_bits, std::uint64_t* cr_txb_coeff_bits) {
    l16_rdo::CostCoeffsTxbArgs a;
    a.me = me;
    a.allowUpdateCdf = allow_update_cdf;
    a.fc = ec_ctx;
    a.fcSyntax = ec_syntax_ctx;
    a.lumaMode = lumaMode;
    a.filterIntraMode = filterIntraMode;
    a.qcoeff = nullptr;
    a.eob = 0;
    a.planeType = 0;
    a.transformSize = 0;
    a.transformType = 0;
    a.txbSkipCtx = 0;
    a.dcSignCtx = 0;
    a.reducedTxSet = reduced_tx_set;
    a.levelsBuf = levels_buf;
    a.coeffContexts = coeff_contexts;
    a.fastCoeffEstLevel = fast_coeff_est_level;
    a.subresStep = subres_step;

    if (component_type == 0 /*COMPONENT_LUMA*/ || component_type == 4 /*COMPONENT_ALL*/) {
        if (y_eob) {
            a.qcoeff = y_coeff;
            a.eob = static_cast<std::uint16_t>(y_eob);
            a.planeType = 0;  // PLANE_TYPE_Y
            a.transformSize = txsize;
            a.transformType = tx_type;
            a.txbSkipCtx = static_cast<std::int16_t>(luma_txb_skip_context);
            a.dcSignCtx = static_cast<std::int16_t>(luma_dc_sign_context);
            a.isInter = l16_rdo::isInterMode(lumaMode) != 0;
            *y_txb_coeff_bits = static_cast<std::uint64_t>(l16_rdo::costCoeffsTxb(a));
            *y_txb_coeff_bits = (*y_txb_coeff_bits) << subres_step;
        } else {
            *y_txb_coeff_bits = static_cast<std::uint64_t>(l16_rdo::costSkipTxb(
                me, allow_update_cdf, ec_ctx, txsize, 0 /*PLANE_TYPE_Y*/, luma_txb_skip_context));
        }
    }

    if (component_type == 2 /*COMPONENT_CHROMA_CB*/ || component_type == 1 /*COMPONENT_CHROMA*/ ||
        component_type == 4 /*COMPONENT_ALL*/) {
        if (cb_eob) {
            a.qcoeff = cb_coeff;
            a.eob = static_cast<std::uint16_t>(cb_eob);
            a.planeType = 1;  // PLANE_TYPE_UV
            a.transformSize = txsize_uv;
            a.transformType = tx_type_uv;
            a.txbSkipCtx = static_cast<std::int16_t>(cb_txb_skip_context);
            a.dcSignCtx = static_cast<std::int16_t>(cb_dc_sign_context);
            a.isInter = l16_rdo::isInterMode(lumaMode) != 0;
            *cb_txb_coeff_bits = static_cast<std::uint64_t>(l16_rdo::costCoeffsTxb(a));
        } else {
            *cb_txb_coeff_bits = static_cast<std::uint64_t>(l16_rdo::costSkipTxb(
                me, allow_update_cdf, ec_ctx, txsize_uv, 1 /*PLANE_TYPE_UV*/, cb_txb_skip_context));
        }
    }

    if (component_type == 3 /*COMPONENT_CHROMA_CR*/ || component_type == 1 /*COMPONENT_CHROMA*/ ||
        component_type == 4 /*COMPONENT_ALL*/) {
        if (cr_eob) {
            a.qcoeff = cr_coeff;
            a.eob = static_cast<std::uint16_t>(cr_eob);
            a.planeType = 1;  // PLANE_TYPE_UV
            a.transformSize = txsize_uv;
            a.transformType = tx_type_uv;
            a.txbSkipCtx = static_cast<std::int16_t>(cr_txb_skip_context);
            a.dcSignCtx = static_cast<std::int16_t>(cr_dc_sign_context);
            a.isInter = l16_rdo::isInterMode(lumaMode) != 0;
            *cr_txb_coeff_bits = static_cast<std::uint64_t>(l16_rdo::costCoeffsTxb(a));
        } else {
            *cr_txb_coeff_bits = static_cast<std::uint64_t>(l16_rdo::costSkipTxb(
                me, allow_update_cdf, ec_ctx, txsize_uv, 1 /*PLANE_TYPE_UV*/, cr_txb_skip_context));
        }
    }
}

// update_coeff_cdf (coding_loop.c:1468-:1593): the txb-ctx read (the l7
// getTxbCtx), the coefficient-bit estimate (the RE3 carriers), and the
// luma/cb/cr dc-sign NA writes.
void updateCoeffCdf(const UpdateCoeffCdfCarrier& c, std::uint64_t* y_txb_coeff_bits,
                    std::uint64_t* cb_txb_coeff_bits, std::uint64_t* cr_txb_coeff_bits) {
    const std::uint8_t tx_depth = c.tx_depth;
    const std::uint8_t txb_itr = c.txb_itr;
    const std::uint8_t uv_pass = tx_depth && c.txb_itr ? 0 : 1;
    const int is_inter = c.is_inter;
    const entropy::TxSize tx_size = c.tx_size;
    const int tx_width = entropy::txSizeWide[tx_size];
    const int tx_height = entropy::txSizeHigh[tx_size];
    const entropy::TxSize tx_size_uv = c.tx_size_uv;
    const int tx_width_uv = entropy::txSizeWide[tx_size_uv];
    const int tx_height_uv = entropy::txSizeHigh[tx_size_uv];
    const std::uint32_t txb_origin_x = c.blk_org_x + txOrg[c.bsize][is_inter][tx_depth][txb_itr].x;
    const std::uint32_t txb_origin_y = c.blk_org_y + txOrg[c.bsize][is_inter][tx_depth][txb_itr].y;

    int luma_txb_skip_context = 0;
    int luma_dc_sign_context = 0;
    entropy::getTxbCtx(c.ep_luma_na_above, c.ep_luma_na_left, c.luma_txb_w_unit, c.luma_txb_h_unit, 0, c.bsize,
                       tx_size, &luma_txb_skip_context, &luma_dc_sign_context);

    int cb_txb_skip_context = 0;
    int cb_dc_sign_context = 0;
    int cr_txb_skip_context = 0;
    int cr_dc_sign_context = 0;
    if (c.has_uv && uv_pass) {
        entropy::getTxbCtx(c.ep_cb_na_above, c.ep_cb_na_left, c.luma_txb_w_unit >> 1, c.luma_txb_h_unit >> 1, 1,
                           getPlaneBlockSize(c.bsize, 1, 1), tx_size_uv, &cb_txb_skip_context,
                           &cb_dc_sign_context);

        entropy::getTxbCtx(c.ep_cr_na_above, c.ep_cr_na_left, c.luma_txb_w_unit >> 1, c.luma_txb_h_unit >> 1, 1,
                           getPlaneBlockSize(c.bsize, 1, 1), tx_size_uv, &cr_txb_skip_context,
                           &cr_dc_sign_context);
    }

    if (c.y_has_coeff || c.u_has_coeff || c.v_has_coeff) {
        estimateCoeffBits(1 /*allow_update_cdf*/, c.lumaMode, c.filterIntraMode, c.md_rate_est_ctx, c.ec_ctx,
                          c.ec_syntax_ctx, c.coeff_y, c.coeff_u, c.coeff_v, c.eob_y, c.eob_u, c.eob_v, tx_size,
                          tx_size_uv, c.tx_type, c.tx_type_uv,
                          (c.has_uv && uv_pass) ? 4 /*COMPONENT_ALL*/ : 0 /*COMPONENT_LUMA*/,
                          luma_txb_skip_context, luma_dc_sign_context, cb_txb_skip_context, cb_dc_sign_context,
                          cr_txb_skip_context, cr_dc_sign_context, c.reduced_tx_set != 0, c.levels_buf,
                          c.coeff_contexts, c.fast_coeff_est_level, c.subres_step, y_txb_coeff_bits,
                          cb_txb_coeff_bits, cr_txb_coeff_bits);
    }

    std::uint8_t dc_sign_level_coeff = c.quant_dc_y;
    neighborArrayUnitModeWritePu(c.ep_luma_dc_sign_na_write, &dc_sign_level_coeff, txb_origin_x, txb_origin_y,
                                 static_cast<std::uint32_t>(tx_width), static_cast<std::uint32_t>(tx_height),
                                 kNaTopAndLeftOnlyMask);

    if (c.has_uv && uv_pass) {
        dc_sign_level_coeff = c.quant_dc_u;
        neighborArrayUnitModeWritePu(c.ep_cb_dc_sign_na_write, &dc_sign_level_coeff,
                                     (txb_origin_x & ~(kRoundUvX - 1)) >> 1,
                                     (txb_origin_y & ~(kRoundUvX - 1)) >> 1,
                                     static_cast<std::uint32_t>(tx_width_uv),
                                     static_cast<std::uint32_t>(tx_height_uv), kNaTopAndLeftOnlyMask);

        dc_sign_level_coeff = c.quant_dc_v;
        neighborArrayUnitModeWritePu(c.ep_cr_dc_sign_na_write, &dc_sign_level_coeff,
                                     (txb_origin_x & ~(kRoundUvX - 1)) >> 1,
                                     (txb_origin_y & ~(kRoundUvX - 1)) >> 1,
                                     static_cast<std::uint32_t>(tx_width_uv),
                                     static_cast<std::uint32_t>(tx_height_uv), kNaTopAndLeftOnlyMask);
    }
}

// svt_aom_satd_c (common_dsp_rtcd.c:48-55).
std::int32_t satd(const std::int64_t* coeff, int length) {
    int satd = 0;
    for (int i = 0; i < length; ++i) {
        satd += coeff[i] < 0 ? -static_cast<int>(coeff[i]) : static_cast<int>(coeff[i]);
    }
    return satd;
}

// hadamard_path (product_coding_loop.c:1138-:1207): the tile walk + the
// PO1 residual/hadamard chain + the satd accumulation.
void hadamardPath(const std::uint8_t* input, std::uint32_t input_stride, std::uint32_t input_origin_index,
                  const std::uint8_t* pred, std::uint32_t pred_stride, std::int16_t* residual,
                  std::uint32_t residual_stride, std::int32_t* coeff, entropy::BlockSize bsize, bool hbd,
                  std::uint32_t* satd_cost_out) {
    const entropy::TxSize tx_size_raw = ebMaxTxsizeLookup[bsize];
    const entropy::TxSize tx_size = tx_size_raw < entropy::TX_32X32 ? tx_size_raw : entropy::TX_32X32;

    const int stepr = entropy::ebTxSizeHighUnit[tx_size];
    const int stepc = entropy::ebTxSizeWideUnit[tx_size];
    const int txbw = entropy::txSizeWide[tx_size];
    const int txbh = entropy::txSizeHigh[tx_size];

    const int max_blocks_wide = entropy::blockSizeWide[bsize] >> kMiSizeLog2;
    const int max_blocks_high = entropy::blockSizeHigh[bsize] >> kMiSizeLog2;

    std::uint32_t satd_cost = 0;

    for (int row = 0; row < max_blocks_high; row += stepr) {
        for (int col = 0; col < max_blocks_wide; col += stepc) {
            const std::uint32_t input_idx =
                (static_cast<std::uint32_t>(row) * input_stride + static_cast<std::uint32_t>(col)) << 2;
            const std::uint32_t pred_idx =
                (static_cast<std::uint32_t>(row) * pred_stride + static_cast<std::uint32_t>(col)) << 2;

            residualKernel(input, input_idx + input_origin_index, input_stride, pred, pred_idx, pred_stride,
                           residual, 0, residual_stride, hbd, static_cast<std::uint32_t>(txbw),
                           static_cast<std::uint32_t>(txbh));

            switch (tx_size) {
            case entropy::TX_4X4:
                picops::hadamard4x4(residual, static_cast<std::ptrdiff_t>(residual_stride), &coeff[0]);
                break;

            case entropy::TX_8X8:
                picops::hadamard8x8(residual, static_cast<std::ptrdiff_t>(residual_stride), &coeff[0]);
                break;

            case entropy::TX_16X16:
                picops::hadamard16x16(residual, static_cast<std::ptrdiff_t>(residual_stride), &coeff[0]);
                break;

            case entropy::TX_32X32:
                picops::hadamard32x32(residual, static_cast<std::ptrdiff_t>(residual_stride), &coeff[0]);
                break;

            default:
                assert(0);
                break;
            }
            std::int64_t satd_coeff[4096];
            for (int i = 0; i < txSize2d[tx_size]; ++i) {
                satd_coeff[i] = coeff[i];
            }
            satd_cost += static_cast<std::uint32_t>(satd(satd_coeff, txSize2d[tx_size]));
        }
    }
    *satd_cost_out = satd_cost;
}

}  // namespace pipeline
