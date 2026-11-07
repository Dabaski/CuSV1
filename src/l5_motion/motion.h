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

// SW1: the sadMxN macro instances (C_DEFAULT/compute_sad_c.c:103-106,
// :118-193). The macro body is the nxm helper called (height=n, width=m)
// - m = width, n = height. The landed sad4x4/8x8/16x16/32x32/64x64 ARE
// the instances at those five sizes (their bodies are the helper loop);
// the remaining 17 sizes are thin wrappers below.
//
// svt_nxm_sad_kernel_helper_c (C_DEFAULT/compute_sad_c.c:21-40, verbatim
// semantics): the generic NxM SAD every macro instance forwards to.
std::uint32_t nxmSadKernelHelper(const std::uint8_t* src, std::uint32_t srcStride,
                                 const std::uint8_t* ref, std::uint32_t refStride,
                                 std::uint32_t height, std::uint32_t width);

std::uint32_t sad4x8(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                     std::uint32_t refStride);

std::uint32_t sad8x4(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                     std::uint32_t refStride);

std::uint32_t sad128x128(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                         std::uint32_t refStride);

std::uint32_t sad128x64(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                        std::uint32_t refStride);

std::uint32_t sad64x128(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                        std::uint32_t refStride);

std::uint32_t sad64x32(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride);

std::uint32_t sad32x64(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride);

std::uint32_t sad32x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride);

std::uint32_t sad16x32(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride);

std::uint32_t sad16x8(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                      std::uint32_t refStride);

std::uint32_t sad8x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                      std::uint32_t refStride);

std::uint32_t sad4x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                      std::uint32_t refStride);

std::uint32_t sad16x4(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                      std::uint32_t refStride);

std::uint32_t sad8x32(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                      std::uint32_t refStride);

std::uint32_t sad32x8(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                      std::uint32_t refStride);

std::uint32_t sad16x64(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride);

std::uint32_t sad64x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                       std::uint32_t refStride);

// SW1: the sadMxNx4D macro instances (C_DEFAULT/compute_sad_c.c:109-116
// + the instance lines :118-193). The macro body calls the sadMxN
// instance per ref_array entry: sad_array[i] = sadMxN(ref_array[i]),
// i = 0..3 - the reference's index order.
void sad128x128x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                   std::uint32_t refStride, std::uint32_t* sadArray);
void sad128x64x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                  std::uint32_t refStride, std::uint32_t* sadArray);
void sad64x128x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                  std::uint32_t refStride, std::uint32_t* sadArray);
void sad64x64x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray);
void sad64x32x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray);
void sad32x64x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray);
void sad32x32x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray);
void sad32x16x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray);
void sad16x32x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray);
void sad16x16x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray);
void sad16x8x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                std::uint32_t refStride, std::uint32_t* sadArray);
void sad8x16x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                std::uint32_t refStride, std::uint32_t* sadArray);
void sad8x8x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
               std::uint32_t refStride, std::uint32_t* sadArray);
void sad8x4x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
               std::uint32_t refStride, std::uint32_t* sadArray);
void sad4x8x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
               std::uint32_t refStride, std::uint32_t* sadArray);
void sad4x4x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
               std::uint32_t refStride, std::uint32_t* sadArray);
void sad4x16x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                std::uint32_t refStride, std::uint32_t* sadArray);
void sad16x4x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                std::uint32_t refStride, std::uint32_t* sadArray);
void sad8x32x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                std::uint32_t refStride, std::uint32_t* sadArray);
void sad32x8x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                std::uint32_t refStride, std::uint32_t* sadArray);
void sad16x64x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray);
void sad64x16x4d(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* const refArray[4],
                 std::uint32_t refStride, std::uint32_t* sadArray);

// SW1: the OBMCSADMxN macro instances (C_DEFAULT/sad_av1.c:36-40 + the
// instance lines :43-64). The macro body is obmc_sad(ref, ref_stride,
// wsrc, mask, m, n) - m = width, n = height - forwarded to the landed
// obmcSad core.
std::uint32_t obmcSad128x128(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                             const std::int32_t* mask);
std::uint32_t obmcSad128x64(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                            const std::int32_t* mask);
std::uint32_t obmcSad64x128(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                            const std::int32_t* mask);
std::uint32_t obmcSad64x64(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask);
std::uint32_t obmcSad64x32(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask);
std::uint32_t obmcSad32x64(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask);
std::uint32_t obmcSad32x32(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask);
std::uint32_t obmcSad32x16(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask);
std::uint32_t obmcSad16x32(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask);
std::uint32_t obmcSad16x16(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask);
std::uint32_t obmcSad16x8(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                          const std::int32_t* mask);
std::uint32_t obmcSad8x16(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                          const std::int32_t* mask);
std::uint32_t obmcSad8x8(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                         const std::int32_t* mask);
std::uint32_t obmcSad8x4(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                         const std::int32_t* mask);
std::uint32_t obmcSad4x8(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                         const std::int32_t* mask);
std::uint32_t obmcSad4x4(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                         const std::int32_t* mask);
std::uint32_t obmcSad4x16(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                          const std::int32_t* mask);
std::uint32_t obmcSad16x4(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                          const std::int32_t* mask);
std::uint32_t obmcSad8x32(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                          const std::int32_t* mask);
std::uint32_t obmcSad32x8(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                          const std::int32_t* mask);
std::uint32_t obmcSad16x64(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask);
std::uint32_t obmcSad64x16(const std::uint8_t* ref, int refStride, const std::int32_t* wsrc,
                           const std::int32_t* mask);

}  // namespace motion