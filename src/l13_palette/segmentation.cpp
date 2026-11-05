#include <algorithm>
#include <cassert>

#include "segmentation.h"

namespace l13_palette {
namespace segmentation {

// svt_aom_segmentation_feature_signed (segmentation_params.c:16).
const std::int32_t kSegFeatureSigned[segLvlMax] = {1, 1, 1, 1, 1, 0, 0, 0};

// svt_aom_segmentation_feature_bits (segmentation_params.c:18).
const std::int32_t kSegFeatureBits[segLvlMax] = {8, 6, 6, 6, 6, 3, 0, 0};

// svt_aom_segmentation_feature_max (segmentation_params.c:20-21).
const std::int32_t kSegFeatureMax[segLvlMax] = {255, 63, 63, 63, 63, 7, 0, 0};

// calculate_segmentation_data (segmentation.c:260-271).
void calculateSegmentationData(SegmentationParams* segmentation_params) {
    for (int i = 0; i < maxSegments; i++) {
        for (int j = 0; j < segLvlMax; j++) {
            if (segmentation_params->feature_enabled[i][j]) {
                segmentation_params->last_active_seg_id = static_cast<std::uint8_t>(i);
                if (j >= segLvlRefFrame) {
                    segmentation_params->seg_id_pre_skip = 1;
                }
            }
        }
    }
}

namespace {

// The EbMeTierZeroPu bases consumed by the switch (me_context.h:48/:52/:68).
constexpr int kMeTierZeroPu32x32_0 = 1;
constexpr int kMeTierZeroPu16x16_0 = 5;
constexpr int kMeTierZeroPu8x8_0 = 21;

// get_msb, portable #else body (definitions.h:628-643): floor(log2(n)),
// n != 0. l7/l10/l13-palette precedent (the per-module local copy).
std::int32_t getMsb(std::uint32_t n) {
    std::int32_t log = 0;
    std::uint32_t value = n;
    assert(n != 0);
    for (std::int32_t i = 4; i >= 0; --i) {
        const std::int32_t shift = (1 << i);
        const std::uint32_t x = value >> shift;
        if (x != 0) {
            value = x;
            log += shift;
        }
    }
    return log;
}

// svt_log2f_safe (definitions.h:591): get_msb((x) | 1).
std::uint16_t log2Safe(std::uint32_t x) {
    return static_cast<std::uint16_t>(getMsb(x | 1));
}

} // namespace

// aqSegmentId (segmentation.c:144-157, the extracted walk).
std::uint8_t aqSegmentId(std::uint16_t variance, const SegmentationParams* segmentation_params,
                         std::int32_t baseQIdx) {
    std::uint8_t segmentId = 0;
    for (int i = maxSegments - 1; i >= 0; i--) {
        // uint16 <= int16: both promote to int in the reference (signed).
        if (variance <= segmentation_params->variance_bin_edge[i]) {
            const std::int32_t qIndex = baseQIdx + segmentation_params->feature_data[i][segLvlAltQ];
            // Avoid lossless since SVT-AV1 doesn't support it.
            if (qIndex > 0) {
                segmentId = static_cast<std::uint8_t>(i);
                break;
            }
        }
    }
    return segmentId;
}

// find_segment_qps (segmentation.c:273-326), the pcs walls threaded as
// parameters (see the header note).
void findSegmentQps(SegmentationParams* segmentation_params, std::uint32_t b64TotalCount,
                    const std::uint16_t* const* variance) {
    std::uint16_t minVar = 0xFFFFu /*UINT16_MAX*/, maxVar = 0u /*MIN_UNSIGNED_VALUE*/, avgVar = 0;
    const float strength = 2; // to tune

    // get range of variance
    for (std::uint32_t sbIdx = 0; sbIdx < b64TotalCount; ++sbIdx) {
        const std::uint16_t* variancePtr = variance[sbIdx];
        std::uint32_t varIndex, localAvg = 0;
        // Loop over all 8x8s in a 64x64
        for (varIndex = kMeTierZeroPu8x8_0; varIndex <= 84u /*ME_TIER_ZERO_PU_8x8_63*/; varIndex++) {
            maxVar = static_cast<std::uint16_t>(std::max<std::uint16_t>(maxVar, variancePtr[varIndex]));
            minVar = static_cast<std::uint16_t>(std::min<std::uint16_t>(minVar, variancePtr[varIndex]));
            localAvg += variancePtr[varIndex];
        }
        avgVar = static_cast<std::uint16_t>(avgVar + (localAvg >> 6));
    }
    avgVar = static_cast<std::uint16_t>(avgVar / b64TotalCount);
    avgVar = log2Safe(avgVar);

    // get variance bin edges & QPs
    const std::uint16_t minVarLog = log2Safe(minVar);
    const std::uint16_t maxVarLog = log2Safe(maxVar);
    // ROUND is the identity on integers (utility.h:110 expands with 1/2 = 0),
    // so the false branch is the plain integer division.
    const std::uint16_t diff = static_cast<std::uint16_t>(maxVarLog - minVarLog);
    const std::uint16_t stepSize = diff <= maxSegments ? 1 : static_cast<std::uint16_t>(diff / maxSegments);
    std::uint16_t binEdge = static_cast<std::uint16_t>(minVarLog + stepSize);
    std::uint16_t binCenter = static_cast<std::uint16_t>(binEdge >> 1);

    for (int i = maxSegments - 1; i >= 0; i--) {
        // POW2(bin_edge) = 1 << bin_edge, narrowed to int16_t (wraps at
        // bin_edge >= 16 - the reference's own narrowing).
        segmentation_params->variance_bin_edge[i] = static_cast<std::int16_t>(1 << binEdge);
        segmentation_params->feature_data[i][segLvlAltQ] = static_cast<std::int16_t>(
            static_cast<int>(static_cast<std::uint16_t>(strength)) *
            (std::max(1, static_cast<int>(binCenter)) - static_cast<int>(avgVar)));
        binEdge = static_cast<std::uint16_t>(binEdge + stepSize);
        binCenter = static_cast<std::uint16_t>(binCenter + stepSize);
    }
    if (segmentation_params->feature_data[0][segLvlAltQ] < 0) {
        // avoid lossless block
        segmentation_params->feature_data[0][segLvlAltQ] = 0;
    }
}

// get_variance_for_cu (segmentation.c:23-84).
std::uint16_t getVarianceForCu(BlockSize bsize, int orgX, int orgY, const std::uint16_t* variancePtr) {
    int index0, index1;
    switch (bsize) {
    case BlockSize::BLOCK_4X4:
    case BlockSize::BLOCK_4X8:
    case BlockSize::BLOCK_8X4:
    case BlockSize::BLOCK_8X8:
        index0 = index1 = kMeTierZeroPu8x8_0 + ((orgX >> 3) + orgY);
        break;

    case BlockSize::BLOCK_8X16:
        index0 = kMeTierZeroPu8x8_0 + ((orgX >> 3) + orgY);
        index1 = index0 + 1;
        break;

    case BlockSize::BLOCK_16X8:
        index0 = kMeTierZeroPu8x8_0 + ((orgX >> 3) + orgY);
        index1 = index0 + orgY;
        break;

    case BlockSize::BLOCK_4X16:
    case BlockSize::BLOCK_16X4:
    case BlockSize::BLOCK_16X16:
        index0 = index1 = kMeTierZeroPu16x16_0 + ((orgX >> 4) + (orgY >> 2));
        break;

    case BlockSize::BLOCK_16X32:
        index0 = kMeTierZeroPu16x16_0 + ((orgX >> 4) + (orgY >> 2));
        index1 = index0 + 1;
        break;

    case BlockSize::BLOCK_32X16:
        index0 = kMeTierZeroPu16x16_0 + ((orgX >> 4) + (orgY >> 2));
        index1 = index0 + (orgY >> 2);
        break;

    case BlockSize::BLOCK_8X32:
    case BlockSize::BLOCK_32X8:
    case BlockSize::BLOCK_32X32:
        index0 = index1 = kMeTierZeroPu32x32_0 + ((orgX >> 5) + (orgY >> 4));
        break;

    case BlockSize::BLOCK_32X64:
        index0 = kMeTierZeroPu32x32_0 + ((orgX >> 5) + (orgY >> 4));
        index1 = index0 + 1;
        break;

    case BlockSize::BLOCK_64X32:
        index0 = kMeTierZeroPu32x32_0 + ((orgX >> 5) + (orgY >> 4));
        index1 = index0 + (orgY >> 4);
        break;
    default:
        index0 = index1 = 0;
        break;
    }
    return static_cast<std::uint16_t>((variancePtr[index0] + variancePtr[index1]) >> 1);
}

} // namespace segmentation
} // namespace l13_palette
