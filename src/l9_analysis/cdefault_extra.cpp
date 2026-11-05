#include "cdefault_extra.h"

#include <cstdlib>

namespace analysis {

namespace {

// diffwtd_mask_d16 (C_DEFAULT/inter_prediction_c.c:15-28, verbatim).
// FILTER_BITS = 7 (Codec/definitions.h:435), DIFF_FACTOR = 16
// (:1253), AOM_BLEND_A64_MAX_ALPHA = 64 (:1248).
void diffwtdMaskD16(std::uint8_t* mask, int whichInverse, int maskBase, const std::uint16_t* src0,
                    int src0Stride, const std::uint16_t* src1, int src1Stride, int h, int w,
                    int round0, int round1, int bd) {
    const int round = 2 * 7 - round0 - round1 + (bd - 8);
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            int diff = std::abs(src0[i * src0Stride + j] - src1[i * src1Stride + j]);
            diff     = (diff + (1 << (round - 1))) >> round;
            int m    = maskBase + diff / 16;
            if (m < 0) m = 0;
            if (m > 64) m = 64;
            mask[i * w + j] = static_cast<std::uint8_t>(whichInverse ? 64 - m : m);
        }
    }
}

}  // namespace

// svt_av1_build_compound_diffwtd_mask_d16_c (C_DEFAULT/inter_prediction_c.c:30-43, verbatim)
void buildCompoundDiffwtdMaskD16(std::uint8_t* mask, int maskType, const std::uint16_t* src0,
                                 int src0Stride, const std::uint16_t* src1, int src1Stride, int h, int w,
                                 int round0, int round1, int bd) {
    switch (maskType) {
    case 0:  // DIFFWTD_38
        diffwtdMaskD16(mask, 0, 38, src0, src0Stride, src1, src1Stride, h, w, round0, round1, bd);
        break;
    case 1:  // DIFFWTD_38_INV
        diffwtdMaskD16(mask, 1, 38, src0, src0Stride, src1, src1Stride, h, w, round0, round1, bd);
        break;
    default:
        break;
    }
}

}  // namespace analysis
