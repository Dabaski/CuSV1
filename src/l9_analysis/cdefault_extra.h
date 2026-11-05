// l9_analysis - cdefault_extra: struct-free C_DEFAULT kernels whose family
// has no other landed home (CS1 sweep). Reference:
// third_party/SVT-AV1/Source/Lib/C_DEFAULT/inter_prediction_c.c.
#pragma once

#include <cstdint>

namespace analysis {

// diffwtd_mask_d16 (inter_prediction_c.c:15-28) +
// svt_av1_build_compound_diffwtd_mask_d16_c (:30-43). maskType: 0 =
// DIFFWTD_38, 1 = DIFFWTD_38_INV. round_0/round_1 are threaded as
// parameters (named deviation: the ConvolveParams struct-walled caller is
// not ported); round = 2*FILTER_BITS(7) - round_0 - round_1 + (bd - 8).
void buildCompoundDiffwtdMaskD16(std::uint8_t* mask, int maskType, const std::uint16_t* src0,
                                 int src0Stride, const std::uint16_t* src1, int src1Stride, int h, int w,
                                 int round0, int round1, int bd);

}  // namespace analysis
