// l14_utility - the svt_psnr.c SSE surface (A13, PS1).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/svt_psnr.c.
// BRIEF CORRECTION (measured): svt_psnr.c contains NO svt_aom_psnr
// function (grep-verified) - the file's surface is the per-plane SSE
// arithmetic; the PSNR dB conversion is not in this file.
// LANDED: variance (file-local static in the reference; exported - the
// mdScanAllBlks statics-exported precedent), svt_aom_mse16x16_c,
// svt_aom_get_sse.
// PARKED (named): the get_y/u/v_sse_part trio :189-:249 - the
// Yv12BufferConfig-threaded shells over get_sse; the
// CONFIG_ENABLE_HIGH_BIT_DEPTH block :77-:187 + the hbd _part trio
// :219-:249 - CONFIG_ENABLE_HIGH_BIT_DEPTH is 0 in the pinned build
// (Source/API/EbConfigMacros.h:56), config-dead.
#pragma once

#include <cstdint>

namespace l14_utility {

// variance - svt_psnr.c:19-:33 verbatim: the strided SSE over w x h.
std::uint32_t variance(const std::uint8_t* a, std::int32_t a_stride, const std::uint8_t* b, std::int32_t b_stride,
                       std::int32_t w, std::int32_t h);

// svt_aom_mse16x16_c - svt_psnr.c:35-:38 verbatim.
std::uint32_t mse16x16(const std::uint8_t* src_ptr, std::int32_t source_stride, const std::uint8_t* ref_ptr,
                       std::int32_t recon_stride);

// svt_aom_get_sse - svt_psnr.c:40-:75 verbatim: the dw column strip, the
// dh row strip and the 16x16 grid tile the region exactly.
std::int64_t getSse(const std::uint8_t* a, std::int32_t a_stride, const std::uint8_t* b, std::int32_t b_stride,
                    std::int32_t width, std::int32_t height);

}  // namespace l14_utility
