// l13_palette::segmentation - the segmentation.c surface (A12, SEG1).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/segmentation.c
// (326 physical, 302 non-blank) + segmentation_params.c (21 physical,
// 17 non-blank) + segmentation_params.h (81 physical, 65 non-blank).
// LANDED (the struct-free surface):
//   the three svt_aom_segmentation_feature_{signed,bits,max} tables
//   (segmentation_params.c:16/:18/:20-21), the SEG_LVL_FEATURES enum
//   (segmentation_params.h:17-27), the SegmentationParams struct
//   (:35-75), calculate_segmentation_data (segmentation.c:260-271),
//   get_variance_for_cu (:23-84), find_segment_qps (:273-326, the
//   pcs walls threaded as parameters), and aqSegmentId (the
//   :144-157 segment-id walk, extracted).
// PARKED (named, port nothing):
//   roi_map_apply_segmentation_based_quantization segmentation.c:87-134
//   (SvtAv1RoiMapEvt / SuperBlock / BlkStruct walls),
//   svt_aom_apply_segmentation_based_quantization :136-158 (PCS wall; its
//   pure tail is the extracted aqSegmentId above),
//   roi_map_setup_segmentation :160-237 (roi_map +
//   svt_av1_pick_filter_level_by_q walls), svt_aom_setup_segmentation
//   :239-258 (pcs/scs walls: the aq_mode gate), and temporally_update_qps
//   (declared segmentation.h:29, defined nowhere in Source/Lib -
//   grep-verified; a dead declaration).
// COVERAGE-ONLY: our emission keeps segmentation_enabled at literal 0
// (no segment symbol is ever written); nothing here moves a conformance
// count. Quirks preserved 1:1 and named inline: ROUND's integer identity
// (utility.h:110), the POW2 uint16 wrap, the descending bin-edge /
// bin-center pairing (edge[7] is the FIRST, smallest edge), the 8X16/16X8
// and 16X32/32X16 index1 pairings (the reference's own transposed-looking
// forms), and the step_size that can only ever be 1.
// Deviations (all named inline): statics exported as plain functions /
// externs (LR1/LR2 precedent); the local BlockSize enum (CDEF1 precedent);
// getMsb/log2Safe are per-module locals (l7/l10/l13 precedent).
#pragma once

#include <cstdint>

namespace l13_palette {
namespace segmentation {

// segmentation_params.h:17-27 SEG_LVL_FEATURES. Unscoped so the values index
// the feature tables directly (the reference uses them as int indices).
enum SegLvl {
    segLvlAltQ = 0,     // SEG_LVL_ALT_Q
    segLvlAltLfYV = 1,  // SEG_LVL_ALT_LF_Y_V
    segLvlAltLfYH = 2,  // SEG_LVL_ALT_LF_Y_H
    segLvlAltLfU = 3,   // SEG_LVL_ALT_LF_U
    segLvlAltLfV = 4,   // SEG_LVL_ALT_LF_V
    segLvlRefFrame = 5, // SEG_LVL_REF_FRAME
    segLvlSkip = 6,     // SEG_LVL_SKIP
    segLvlGlobalMv = 7, // SEG_LVL_GLOBALMV
    segLvlMax = 8,      // SEG_LVL_MAX
};

// MAX_SEGMENTS (definitions.h:1674).
constexpr std::int32_t maxSegments = 8;

// SegmentationParams (segmentation_params.h:35-75). DEVIATION (named): the
// EbDctor dctor (object.h machinery) and the SegmentationNeighborMap side
// struct are not ported; all data fields carry their reference types.
struct SegmentationParams {
    std::uint8_t segmentation_enabled;
    std::uint8_t segmentation_update_map;
    std::uint8_t segmentation_temporal_update;
    std::uint8_t segmentation_update_data;
    std::int16_t feature_data[maxSegments][segLvlMax];
    std::int16_t feature_enabled[maxSegments][segLvlMax];
    std::int16_t seg_qm_level[maxSegments][segLvlMax];
    std::uint8_t last_active_seg_id;
    std::uint8_t seg_id_pre_skip;
    std::int16_t variance_bin_edge[maxSegments];
};

// definitions.h:882-904 BlockSize (the full 22-value reference enum) and
// :907 BLOCK_INVALID = 255. DEVIATION (named): l0_core::BlockSize carries
// only BLOCK_4X4/BLOCK_8X8 (CDEF1 local-enum precedent), so the reference
// enum is mirrored here with its reference numeric values.
enum class BlockSize {
    BLOCK_4X4 = 0,
    BLOCK_4X8 = 1,
    BLOCK_8X4 = 2,
    BLOCK_8X8 = 3,
    BLOCK_8X16 = 4,
    BLOCK_16X8 = 5,
    BLOCK_16X16 = 6,
    BLOCK_16X32 = 7,
    BLOCK_32X16 = 8,
    BLOCK_32X32 = 9,
    BLOCK_32X64 = 10,
    BLOCK_64X32 = 11,
    BLOCK_64X64 = 12,
    BLOCK_64X128 = 13,
    BLOCK_128X64 = 14,
    BLOCK_128X128 = 15,
    BLOCK_4X16 = 16,
    BLOCK_16X4 = 17,
    BLOCK_8X32 = 18,
    BLOCK_32X8 = 19,
    BLOCK_16X64 = 20,
    BLOCK_64X16 = 21,
    BLOCK_INVALID = 255,
};

// get_variance_for_cu (segmentation.c:23-84, static): the zero-ME variance
// lookup for a block at pixel offset (org_x, org_y) within its SB origin
// (the caller passes pixel offsets within the SB, product_coding_loop.c:
// 8962-8963), over an 85-entry EbMeTierZeroPu window (me_context.h:45-131:
// 0 = 64x64, 1..4 = 32x32, 5..20 = 16x16, 21..84 = 8x8, filled row-major
// by pic_analysis_process.c:340-418). Returns
// (variance_ptr[index0] + variance_ptr[index1]) >> 1.
std::uint16_t getVarianceForCu(BlockSize bsize, int orgX, int orgY, const std::uint16_t* variancePtr);

// aqSegmentId - the segment-id walk of
// svt_aom_apply_segmentation_based_quantization (segmentation.c:144-157).
// DEVIATION (named): extracted - the reference writes blk_ptr->segment_id
// inside a PCS-walled function; here the walk is a pure function returning
// the id. The walk is verbatim: default 0, scan i = MAX_SEGMENTS-1..0,
// break on the FIRST segment whose bin edge holds the variance AND whose
// base_q_idx + feature_data[i][ALT_Q] is positive.
std::uint8_t aqSegmentId(std::uint16_t variance, const SegmentationParams* segmentation_params,
                         std::int32_t baseQIdx);

// find_segment_qps (segmentation.c:273-326). DEVIATION (named): the
// reference signature takes PictureControlSet* and reads
// pcs->b64_total_count + pcs->ppcs->variance[sb_idx]; both are threaded as
// parameters here (one 85-entry window pointer per 64x64 SB, the caller
// passes b64_total_count of them). Everything else is verbatim, including
// the utility.h macro expansions: ROUND is the identity on integers
// (utility.h:110, `1/2` is integer 0), POW2(x) = 1 << x narrowed to
// int16_t, MIN_UNSIGNED_VALUE = 0 (utility.h:162), and
// svt_log2f_safe = get_msb(x|1) (definitions.h:591).
void findSegmentQps(SegmentationParams* segmentation_params, std::uint32_t b64TotalCount,
                    const std::uint16_t* const* variance);

// calculate_segmentation_data (segmentation.c:260-271): last_active_seg_id
// ends as the highest segment id with ANY enabled feature; seg_id_pre_skip
// is set when a feature at or above SEG_LVL_REF_FRAME is enabled.
void calculateSegmentationData(SegmentationParams* segmentation_params);

// svt_aom_segmentation_feature_signed (segmentation_params.c:16):
// {1,1,1,1,1,0,0,0} - the first five features are signed deltas.
extern const std::int32_t kSegFeatureSigned[segLvlMax];

// svt_aom_segmentation_feature_bits (segmentation_params.c:18).
extern const std::int32_t kSegFeatureBits[segLvlMax];

// svt_aom_segmentation_feature_max (segmentation_params.c:20-21):
// {MAXQ, 4x MAX_LOOP_FILTER, 7, 0, 0} - MAXQ (definitions.h:1642) is 255,
// MAX_LOOP_FILTER (definitions.h:1652) is 63.
extern const std::int32_t kSegFeatureMax[segLvlMax];

} // namespace segmentation
} // namespace l13_palette
