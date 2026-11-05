#pragma once

#include <cstdint>
#include <string>

namespace motion {

std::uint32_t sad8x8(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                     std::uint32_t refStride);

// svt_nxm_sad_kernel_helper_c (C_DEFAULT/compute_sad_c.c:21) at 16x16 (C7)
std::uint32_t sad16x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride);

// svt_nxm_sad_kernel_helper_c (C_DEFAULT/compute_sad_c.c:21) at 32x32 (L5)
std::uint32_t sad32x32(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride);

// svt_nxm_sad_kernel_helper_c (C_DEFAULT/compute_sad_c.c:21) at 64x64 (L9)
std::uint32_t sad64x64(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride);

std::string sad8x8CuSource();

std::uint32_t sad4x4(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                     std::uint32_t refStride);

std::string sad4x4CuSource();

// svt_aom_sad_16b_kernel_c (C_DEFAULT/compute_sad_c.c:42-61): the nxm
// helper's EB_ABS_DIFF loop over 16-bit planes, generic w x h. The
// reference takes non-const uint16_t*; const here is the named signature
// deviation (read-only use).
std::uint32_t sad16b(const std::uint16_t* src, std::uint32_t srcStride, const std::uint16_t* ref,
                     std::uint32_t refStride, std::uint32_t height, std::uint32_t width);

// svt_sad_loop_kernel_c (C_DEFAULT/compute_sad_c.c:63-101): the full
// search-area SAD sweep. src_stride_raw is the per-search-row ref
// advance (NOT ref_stride); skip_search_line drops even search rows for
// 16-wide blocks of height <= 16.
void sadLoopKernel(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                   std::uint32_t refStride, std::uint32_t blockHeight, std::uint32_t blockWidth,
                   std::uint64_t* bestSad, std::int16_t* xSearchCenter, std::int16_t* ySearchCenter,
                   std::uint32_t srcStrideRaw, std::uint8_t skipSearchLine, std::int16_t searchAreaWidth,
                   std::int16_t searchAreaHeight);

// obmc_sad (C_DEFAULT/sad_av1.c:18-34): the static core exposed with
// explicit width/height (named deviation - the reference exports 22
// per-size OBMCSADMxN wrapper instances around this core).
std::uint32_t obmcSad(const std::uint8_t* pre, int preStride, const std::int32_t* wsrc, const std::int32_t* mask,
                      int width, int height);

}  // namespace motion