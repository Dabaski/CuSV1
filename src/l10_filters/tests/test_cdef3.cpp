#include <doctest.h>
#include <cdef.h>

// CDEF3 (A8 campaign): the enc_cdef mse/search block + fill_rect.
// 1:1 port of third_party/SVT-AV1/Source/Lib/Codec/enc_cdef.c:
//   mse_8xn_16bit_c :24-35, mse_4xn_16bit_c :37-48, mse_8xn_8bit_c :50-61,
//   mse_4xn_8bit_c :63-74 (four static INLINE kernels),
//   svt_aom_compute_cdef_dist_16bit_c :77-113,
//   svt_aom_compute_cdef_dist_8bit_c :115-151 (the block-level dist walkers),
//   cdef_apply_fb :200-297 (the 8-bit src8 builder + filter call, inside the
//   CDEF_8BITS_PATH guard), svt_search_one_dual_c :714-772,
//   joint_strength_search_dual :787-818,
//   and cdef_copy.h svt_aom_fill_rect :88-94.
// Named deviations: the four mse kernels, the two dist walkers, search_one_dual
// and cdef_apply_fb are file-local (static INLINE / static / static inline) in
// the reference and are exported for direct testability (cdefSign /
// cdefFilterFbLbdImpl precedent). The kernels keep the exact reference loop
// shapes: src packed (row stride = the block width), dst strided by dstride.
// PARKED (named): svt_sb_compute_cdef_list :153-184 (mi_grid coupled),
// svt_av1_cdef_frame :300-... (pcs/frm_hdr coupled), svt_pick_cdef_from_qp
// :823-... (float roundf), propagate_cdef_strength :875-...,
// finish_cdef_search :897-.... Coverage-only slice: no bitstream surface.

namespace {

// mse_8xn_16bit_c (enc_cdef.c:24-35): the 16-bit 8-wide kernel. src is
// packed (row stride 8), dst is strided by dstride; rows step by the
// subsampling factor.
TEST_CASE("cdefMse8xn16bit full 8x8 ramp dst-src") {
    std::uint16_t src[64];
    for (int i = 0; i < 64; ++i) src[i] = static_cast<std::uint16_t>(i);
    std::uint16_t dst[8 * 12];
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) dst[r * 12 + c] = static_cast<std::uint16_t>(r * 8 + c);
    // dst[i*dstride+j] - src[8i+j] == 0 for all 64: sum 0.
    const std::uint64_t got = l10_filters::cdefMse8xn16bit(src, dst + 0, 12, 8, 1);
    CHECK(got == 0);
}

} // namespace

// mse_4xn_16bit_c (enc_cdef.c:37-48): the 16-bit 4-wide kernel. src packed
// (row stride 4), dst strided.
TEST_CASE("cdefMse4xn16bit full 4x8 ramp dst-src") {
    std::uint16_t src[32];
    for (int i = 0; i < 32; ++i) src[i] = static_cast<std::uint16_t>(i);
    std::uint16_t dst[8 * 12];
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 4; ++c) dst[r * 12 + c] = static_cast<std::uint16_t>(r * 4 + c);
    const std::uint64_t got = l10_filters::cdefMse4xn16bit(src, dst + 0, 12, 8, 1);
    CHECK(got == 0);
}

// mse_8xn_8bit_c (enc_cdef.c:50-61): the 8-bit 8-wide twin.
TEST_CASE("cdefMse8xn8bit full 8x8 ramp dst-src") {
    std::uint8_t src[64];
    for (int i = 0; i < 64; ++i) src[i] = static_cast<std::uint8_t>(i);
    std::uint8_t dst[8 * 12];
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) dst[r * 12 + c] = static_cast<std::uint8_t>(r * 8 + c);
    const std::uint64_t got = l10_filters::cdefMse8xn8bit(src, dst + 0, 12, 8, 1);
    CHECK(got == 0);
}

// mse_4xn_8bit_c (enc_cdef.c:63-74): the 8-bit 4-wide twin.
TEST_CASE("cdefMse4xn8bit full 4x8 ramp dst-src") {
    std::uint8_t src[32];
    for (int i = 0; i < 32; ++i) src[i] = static_cast<std::uint8_t>(i);
    std::uint8_t dst[8 * 12];
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 4; ++c) dst[r * 12 + c] = static_cast<std::uint8_t>(r * 4 + c);
    const std::uint64_t got = l10_filters::cdefMse4xn8bit(src, dst + 0, 12, 8, 1);
    CHECK(got == 0);
}

// Hand-derived non-zero sums: dst differs from src by a constant. dst[r][c] =
// src + k for every element, so e = k and sum = element_count * k^2. The
// stride-12 dst layout (poisoned padding) makes a stride-ignoring mutant read
// the padding and blow up, so the hand value pins the dstride advance.
TEST_CASE("cdefMse8xn16bit hand sum const-1 dst offset") {
    std::uint16_t src[64];
    for (int i = 0; i < 64; ++i) src[i] = static_cast<std::uint16_t>(100 + i);
    std::uint16_t dst[8 * 12];
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) dst[r * 12 + c] = static_cast<std::uint16_t>(101 + r * 8 + c);
    const std::uint64_t got = l10_filters::cdefMse8xn16bit(src, dst, 12, 8, 1);
    CHECK(got == 64 * 1 * 1);
}

TEST_CASE("cdefMse4xn16bit hand sum const-2 dst offset") {
    std::uint16_t src[32];
    for (int i = 0; i < 32; ++i) src[i] = static_cast<std::uint16_t>(50 + i);
    std::uint16_t dst[8 * 12];
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 4; ++c) dst[r * 12 + c] = static_cast<std::uint16_t>(52 + r * 4 + c);
    const std::uint64_t got = l10_filters::cdefMse4xn16bit(src, dst, 12, 8, 1);
    CHECK(got == 32 * 2 * 2);
}

TEST_CASE("cdefMse8xn8bit hand sum const-3 dst offset") {
    std::uint8_t src[64];
    for (int i = 0; i < 64; ++i) src[i] = static_cast<std::uint8_t>(10 + i);
    std::uint8_t dst[8 * 12];
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) dst[r * 12 + c] = static_cast<std::uint8_t>(13 + r * 8 + c);
    const std::uint64_t got = l10_filters::cdefMse8xn8bit(src, dst, 12, 8, 1);
    CHECK(got == 64 * 3 * 3);
}

TEST_CASE("cdefMse4xn8bit hand sum const-4 dst offset") {
    std::uint8_t src[32];
    for (int i = 0; i < 32; ++i) src[i] = static_cast<std::uint8_t>(20 + i);
    std::uint8_t dst[8 * 12];
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 4; ++c) dst[r * 12 + c] = static_cast<std::uint8_t>(24 + r * 4 + c);
    const std::uint64_t got = l10_filters::cdefMse4xn8bit(src, dst, 12, 8, 1);
    CHECK(got == 32 * 4 * 4);
}

// ---- dist walker fixtures -------------------------------------------------
// Hand-derived known answers: src is the packed filtered-block scratch, dst
// the recon frame; every element diff is a constant per block so the sum is
// count * diff^2 and the (by,bx) placement is pinned by construction.

// BLOCK_8X8: two blocks side by side on a stride-16 dst. bi=0: src 1s, dst
// 2s (diff 1) -> 64. bi=1: src 2s, dst 4s (diff 2) -> 64*4. Total 320.
TEST_CASE("cdefComputeDist16bit 8x8 two blocks") {
    std::uint16_t src[128];
    for (int i = 0; i < 64; ++i) src[i] = 1;
    for (int i = 64; i < 128; ++i) src[i] = 2;
    std::uint16_t dst[8 * 16];
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 16; ++c) dst[r * 16 + c] = (c < 8) ? 2 : 4;
    }
    l10_filters::CdefList dlist[2] = {{0, 0}, {0, 1}};
    const std::uint64_t got = l10_filters::cdefComputeDist16bit(dst, 16, src, dlist, 2,
                                                               l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    CHECK(got == 320);
}

// BLOCK_4X8: height 8, width 4. bi=0: src 1s, dst 2s (diff 1) -> 32.
// bi=1: src 1s, dst 4s (diff 3) -> 32*9. Total 320.
TEST_CASE("cdefComputeDist16bit 4x8 two blocks") {
    std::uint16_t src[64];
    for (int i = 0; i < 32; ++i) src[i] = 1;
    for (int i = 32; i < 64; ++i) src[i] = 1;
    std::uint16_t dst[8 * 12];
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) dst[r * 12 + c] = (c < 4) ? 2 : 4;
    }
    l10_filters::CdefList dlist[2] = {{0, 0}, {0, 1}};
    const std::uint64_t got = l10_filters::cdefComputeDist16bit(dst, 12, src, dlist, 2,
                                                               l10_filters::CdefBlockSize::BLOCK_4X8, 0, 1);
    CHECK(got == 320);
}

// BLOCK_8X4: height 4, width 8. bi=0: diff 1 -> 32. bi=1: src 1s, dst 4s
// (diff 3) -> 32*9. Total 320.
TEST_CASE("cdefComputeDist16bit 8x4 two blocks") {
    std::uint16_t src[64];
    for (int i = 0; i < 32; ++i) src[i] = 1;
    for (int i = 32; i < 64; ++i) src[i] = 1;
    std::uint16_t dst[4 * 16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 16; ++c) dst[r * 16 + c] = (c < 8) ? 2 : 4;
    }
    l10_filters::CdefList dlist[2] = {{0, 0}, {0, 1}};
    const std::uint64_t got = l10_filters::cdefComputeDist16bit(dst, 16, src, dlist, 2,
                                                               l10_filters::CdefBlockSize::BLOCK_8X4, 0, 1);
    CHECK(got == 320);
}

// BLOCK_4X4: bi=0: diff 1 -> 16. bi=1: src 2s, dst 4s (diff 2) -> 16*4.
// Total 80.
TEST_CASE("cdefComputeDist16bit 4x4 two blocks") {
    std::uint16_t src[32];
    for (int i = 0; i < 16; ++i) src[i] = 1;
    for (int i = 16; i < 32; ++i) src[i] = 2;
    std::uint16_t dst[4 * 8];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 8; ++c) dst[r * 8 + c] = (c < 4) ? 2 : 4;
    }
    l10_filters::CdefList dlist[2] = {{0, 0}, {0, 1}};
    const std::uint64_t got = l10_filters::cdefComputeDist16bit(dst, 8, src, dlist, 2,
                                                  l10_filters::CdefBlockSize::BLOCK_4X4, 0, 1);
    CHECK(got == 80);
}

// BLOCK_8X8 8-bit: bi=0: src 1s, dst 2s (diff 1) -> 64. bi=1: src 1s, dst
// 3s (diff 2) -> 64*4. Total 320.
TEST_CASE("cdefComputeDist8bit 8x8 two blocks") {
    std::uint8_t src[128];
    for (int i = 0; i < 64; ++i) src[i] = 1;
    for (int i = 64; i < 128; ++i) src[i] = 1;
    std::uint8_t dst[8 * 16];
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 16; ++c) dst[r * 16 + c] = (c < 8) ? 2 : 3;
    }
    l10_filters::CdefList dlist[2] = {{0, 0}, {0, 1}};
    const std::uint64_t got = l10_filters::cdefComputeDist8bit(dst, 16, src, dlist, 2,
                                                              l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    CHECK(got == 320);
}

// BLOCK_4X4 8-bit: bi=0: diff 1 -> 16. bi=1: src 2s, dst 4s (diff 2) -> 64.
// Total 80.
TEST_CASE("cdefComputeDist8bit 4x4 two blocks") {
    std::uint8_t src[32];
    for (int i = 0; i < 16; ++i) src[i] = 1;
    for (int i = 16; i < 32; ++i) src[i] = 2;
    std::uint8_t dst[4 * 8];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 8; ++c) dst[r * 8 + c] = (c < 4) ? 2 : 4;
    }
    l10_filters::CdefList dlist[2] = {{0, 0}, {0, 1}};
    const std::uint64_t got = l10_filters::cdefComputeDist8bit(dst, 8, src, dlist, 2,
                                                              l10_filters::CdefBlockSize::BLOCK_4X4, 0, 1);
    CHECK(got == 80);
}

// joint_strength_search_dual (enc_cdef.c:787): two strengths over a small
// synthetic mse table, the refine loop for the search. The search returns
// the best mse.
TEST_CASE("cdefJointStrengthSearchDual greedy and refine") {
    std::uint64_t luma[2][4] = {{100, 90, 50, 10}, {60, 40, 30, 5}};
    std::uint64_t chroma[2][4] = {{10, 20, 30, 40}, {40, 30, 20, 10}};
    std::uint64_t* mseL[2] = {luma[0], luma[1]};
    std::uint64_t* mseC[2] = {chroma[0], chroma[1]};
    std::uint64_t** mse[2] = {mseL, mseC};
    std::int32_t lev0[4] = {-1, -1, -1, -1};
    std::int32_t lev1[4] = {-1, -1, -1, -1};
    const std::uint64_t res = l10_filters::cdefJointStrengthSearchDual(lev0, lev1, 2, mse, 2, 0, 4);
    CHECK(res > 0);
    CHECK(lev0[0] >= 0);
    CHECK(lev1[0] >= 0);
}

// fill_rect trivial known answer: 2x3 fill of 0x1234 in a 10-stride dst.
TEST_CASE("cdefFillRect fills v rows of h elements") {
    std::uint16_t dst[10 * 10] = {0};
    l10_filters::cdefFillRect(dst, 10, 2, 3, 0x1234);
    CHECK(dst[0] == 0x1234);
    CHECK(dst[1] == 0x1234);
    CHECK(dst[2] == 0x1234);
    CHECK(dst[3] == 0);
    CHECK(dst[10] == 0x1234);
    CHECK(dst[20] == 0);
}
