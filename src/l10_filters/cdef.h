#pragma once

#include <cstdint>

// CDEF1 (A8 campaign): 1:1 port of the CDEF find_dir + filter kernel family
// of third_party/SVT-AV1/Source/Lib/Codec/cdef.c:
//   sign :16-:18, constrain :20-:27 (the damping/get_msb guard),
//   eb_cdef_directions_padded :35-:56 (the [12][2] padded offsets; the
//   kernel-facing table is padded + 2, :58),
//   adjust_strength :66-:70,
//   svt_aom_cdef_find_dir_c :88-:150 (THE SPEC KERNEL: 16-bit input, 8
//   direction partial sums, div_table {0,840,420,280,210,168,140,120,105}
//   :97, cost MAXimized - cost[d] is the weighted variance of the input
//   around its per-line means along direction d, so a constant-along-d
//   block (an edge along d) has LARGE cost[d]),
//   find_dir_dual_c :152-:156, cdef_find_dir :158-:187 (list walker),
//   pri/sec taps :189-:190 ({{4,2},{3,3}} / {{2,1},{2,1}}),
//   svt_cdef_filter_block_c :193-:254 (16-bit SPEC filter: CDEF_VERY_LARGE
//   sentinel 0x7F7F, max guarded vs sentinel, min unguarded-but-sentinel
//   never wins, int16 sum, y = clamp(x + ((8+sum-(sum<0))>>4), min, max)),
//   filter_block_8bit_c :257-:300 (interior 8-bit twin, no sentinel),
//   find_dir_8bit_c :303-:311 (widen, delegate), find_dir_dual_8bit_c
//   :313-:317, eb_cdef_directions_rc :408-:422, cdef_tap_off_frame :427-:430,
//   filter_block_8bit_bounded_c :437-:504 (frame-edge kernel: off-frame
//   taps excluded from sum/max/min - exactly what the sentinel does in the
//   16-bit kernel), cdef_find_dir_8bit :509-:535 (list walker).
//
// CDEF2 (PERRY-A8): the fb/lbd driver chain + the copy helpers:
//   svt_aom_copy_rect8_8bit_to_16bit_c :72-:79,
//   svt_cdef_filter_fb :322-:402 (the 16-bit fb driver; OUTSIDE the
//   CDEF_8BITS_PATH guard that opens at :506),
//   cdef_filter_fb_lbd_impl :542-:638, the lbd wrappers :640-:704 and the
//   dispatch svt_cdef_filter_fb_lbd :706-:778 (all INSIDE the
//   CDEF_8BITS_PATH guard; the guard's macro has no definition in Codec -
//   it is a build-system define),
//   cdef_copy.h: svt_cdef_copy_rect8 :28-:74 (the constant-width dispatch
//   {2,32,34,36,64,66,68} + variable fallback), svt_aom_copy_rect :79-:85
//   (landed as copy_sb8_16's 16-bit-branch dependency), svt_aom_copy_sb8_16
//   :96-:105.
// PARKED (named): svt_aom_fill_rect :88-:94 (its only callers are the
// parked enc_cdef.c halo fills), the enc_cdef mse/search family (CDEF3),
// corner_match.c/corner_detect.c (global-motion machinery, ORPHANED per the
// Scout CDEF classification).
// The strength derivation (:327-:329 / :551-:553) is factored into
// cdefPriStrength/cdefSecStrength (named deviation, cdefSign precedent);
// the impl is exported as a plain function where the reference
// force-inlines it into the wrappers (perf property, not behavior).
// The 16-bit filter kernel is the SPEC; the 8-bit twins are their own 1:1
// bodies (as in the reference) and the derivation relationship is pinned by
// tests. Integer only: no float anywhere in this family.

namespace l10_filters {

// sign (cdef.c:16-18). File-local static INLINE in the reference; exported
// for direct testability (DF1 signedCharClamp precedent, named deviation).
std::int32_t cdefSign(std::int32_t i);

// constrain (cdef.c:20-27): sign(diff) * min(|diff|, max(0,
// threshold - (|diff| >> max(0, damping - get_msb(threshold))))).
// File-local in the reference; exported for the damping-boundary tests
// (named deviation, same precedent).
std::int32_t cdefConstrain(std::int32_t diff, std::int32_t threshold, std::int32_t damping);

// adjust_strength (cdef.c:66-70): the primary-strength modulation by the
// 8x8 directional variance (0 -> 0; else (strength*(4+min(get_msb(var>>6),
// 12))+8)>>4). File-local in the reference; exported for testability
// (named deviation, same precedent). Its only reference caller is the
// parked cdef_filter_fb, but the brief lists it as CDEF1 scope.
std::int32_t cdefAdjustStrength(std::int32_t strength, std::int32_t var);

// CDEF constants (cdef.h): CDEF_VERY_LARGE :40, CDEF_BSTRIDE :37
// (align(128 + 2*8, 3) = 144, MAX_SB_SIZE_LOG2 = 7), CDEF_NBLOCKS :27
// ((1<<7)/8 = 16).
constexpr std::uint16_t kCdefVeryLarge = 0x7F7F;
constexpr int kCdefBstride = 144;
constexpr int kCdefNblocks = 16;

// eb_cdef_directions_padded (cdef.c:35-56): the [12][2] padded offsets.
// The kernel-facing table is padded + 2 (cdef.c:58); index by dir in [0,7]
// through cdefDirections(dir, k) below (the +2 offset folded in).
constexpr std::int32_t kCdefDirectionsPadded[12][2] = {
    {1 * kCdefBstride + 0, 2 * kCdefBstride + 0},   {1 * kCdefBstride + 0, 2 * kCdefBstride - 1},
    {-1 * kCdefBstride + 1, -2 * kCdefBstride + 2}, {0 * kCdefBstride + 1, -1 * kCdefBstride + 2},
    {0 * kCdefBstride + 1, 0 * kCdefBstride + 2},   {0 * kCdefBstride + 1, 1 * kCdefBstride + 2},
    {1 * kCdefBstride + 1, 2 * kCdefBstride + 2},   {1 * kCdefBstride + 0, 2 * kCdefBstride + 1},
    {1 * kCdefBstride + 0, 2 * kCdefBstride + 0},   {1 * kCdefBstride + 0, 2 * kCdefBstride - 1},
    {-1 * kCdefBstride + 1, -2 * kCdefBstride + 2}, {0 * kCdefBstride + 1, -1 * kCdefBstride + 2},
};

constexpr std::int32_t cdefDirections(int dir, int k) { return kCdefDirectionsPadded[dir + 2][k]; }

// eb_cdef_directions_rc (cdef.c:408-421): the per-tap (row, col) decode of
// the padded table, same +2-offset indexing. Used by the bounded kernel to
// test, per tap, whether the tap lands off-frame.
constexpr std::int8_t kCdefDirectionsRc[12][2][2] = {
    {{1, 0}, {2, 0}},   {{1, 0}, {2, -1}},  {{-1, 1}, {-2, 2}}, {{0, 1}, {-1, 2}}, {{0, 1}, {0, 2}},
    {{0, 1}, {1, 2}},   {{1, 1}, {2, 2}},   {{1, 0}, {2, 1}},   {{1, 0}, {2, 0}},  {{1, 0}, {2, -1}},
    {{-1, 1}, {-2, 2}}, {{0, 1}, {-1, 2}},
};

constexpr std::int8_t cdefDirectionsRc(int dir, int k, int rc) { return kCdefDirectionsRc[dir + 2][k][rc]; }

// svt_aom_cdef_find_dir_c (cdef.c:88-150): the SPEC direction kernel, 16-bit
// input. Writes the directional-variance margin through var.
std::uint8_t cdefFindDir(const std::uint16_t* img, int stride, std::int32_t* var, std::int32_t coeffShift);

// CdefBlockSize: only BLOCK_4X4/BLOCK_4X8/BLOCK_8X4/BLOCK_8X8 equality is
// used by the kernels (cdef.c:201-202, :263-265, :444-446). Local enum:
// l0_core::BlockSize lacks 4X8/8X4 and is not extended (named deviation,
// shared-header freeze).
enum class CdefBlockSize { BLOCK_4X4, BLOCK_4X8, BLOCK_8X4, BLOCK_8X8 };

// CdefList (definitions.h:286-288): the non-skip 8x8 block list entry.
struct CdefList {
    std::uint8_t by;
    std::uint8_t bx;
};

// pri/sec taps (cdef.c:189-190). The set is selected by
// (pri_strength >> coeff_shift) & 1; both sec rows are {2, 1}.
constexpr std::int32_t kCdefPriTaps[2][2] = {{4, 2}, {3, 3}};
constexpr std::int32_t kCdefSecTaps[2][2] = {{2, 1}, {2, 1}};

// svt_cdef_filter_block_c (cdef.c:193-254): the 16-bit SPEC filter kernel.
// Writes dst8 (8-bit) or dst16 (16-bit) - exactly one must be null, as in
// the reference.
void cdefFilterBlock(std::uint8_t* dst8, std::uint16_t* dst16, std::int32_t dstride, const std::uint16_t* in,
                     std::int32_t priStrength, std::int32_t secStrength, std::int32_t dir, std::int32_t priDamping,
                     std::int32_t secDamping, CdefBlockSize bsize, std::int32_t coeffShift,
                     std::uint8_t subsamplingFactor);

// svt_cdef_filter_block_8bit_c (cdef.c:257-300): the interior 8-bit twin of
// the SPEC kernel - same math with NO sentinel (max/min unconditional) and
// sub = (bsize == BLOCK_4X4) ? 1 : subsampling_factor.
void cdefFilterBlock8bit(std::uint8_t* dst, std::int32_t dstride, const std::uint8_t* in, std::int32_t priStrength,
                         std::int32_t secStrength, std::int32_t dir, std::int32_t damping, CdefBlockSize bsize,
                         std::int32_t coeffShift, std::uint8_t subsamplingFactor);

// svt_aom_cdef_find_dir_8bit_c (cdef.c:303-311): widen the 8-bit block to a
// packed 8x8 uint16 image and delegate to the SPEC kernel with stride 8.
std::uint8_t cdefFindDir8bit(const std::uint8_t* img, int stride, std::int32_t* var, std::int32_t coeffShift);

// svt_aom_cdef_find_dir_dual_8bit_c (cdef.c:313-317).
void cdefFindDirDual8bit(const std::uint8_t* img1, const std::uint8_t* img2, int stride, std::int32_t* var1,
                         std::int32_t* var2, std::int32_t coeffShift, std::uint8_t* out1, std::uint8_t* out2);

// cdef_tap_off_frame (cdef.c:427-430): true when tap (r, c) in block-local
// coords crosses a block edge that is also a frame edge. Internal tile/SB
// boundaries are NOT off-frame (the caller only sets edge_* on true frame
// borders).
bool cdefTapOffFrame(int r, int c, int rows, int cols, int edgeTop, int edgeBottom, int edgeLeft, int edgeRight);

// svt_cdef_filter_block_8bit_bounded_c (cdef.c:437-504): the boundary-aware
// 8-bit kernel - identical math to the interior twin, but an off-frame tap
// (per cdefTapOffFrame, from the 4 edge flags) is excluded from sum, max
// AND min, which exactly reproduces the 16-bit sentinel kernel. Edge flag
// order matches the reference: top, left, bottom, right.
void cdefFilterBlock8bitBounded(std::uint8_t* dst, std::int32_t dstride, const std::uint8_t* in,
                                std::int32_t priStrength, std::int32_t secStrength, std::int32_t dir,
                                std::int32_t damping, CdefBlockSize bsize, std::int32_t coeffShift,
                                std::uint8_t subsamplingFactor, int edgeTop, int edgeLeft, int edgeBottom,
                                int edgeRight);

// svt_aom_cdef_find_dir_dual_c (cdef.c:152-156): two blocks through the SPEC
// kernel.
void cdefFindDirDual(const std::uint16_t* img1, const std::uint16_t* img2, int stride, std::int32_t* var1,
                     std::int32_t* var2, std::int32_t coeffShift, std::uint8_t* out1, std::uint8_t* out2);

// cdef_find_dir (cdef.c:158-187): the list walker. Pairs (bi, bi+1) through
// the dual kernel; an odd cdefCount leaves a single tail for the SPEC
// kernel. var/dir are CDEF_NBLOCKS-square.
void cdefFindDirList(const std::uint16_t* in, const CdefList* dlist, std::int32_t var[kCdefNblocks][kCdefNblocks],
                     std::int32_t cdefCount, std::int32_t coeffShift,
                     std::uint8_t dir[kCdefNblocks][kCdefNblocks]);

// cdef_find_dir_8bit (cdef.c:509-535): the 8-bit list walker, same pairing
// through the 8-bit dual kernel (CDEF_8BITS_PATH body, in CDEF1 scope).
void cdefFindDir8bitList(const std::uint8_t* in, const CdefList* dlist,
                         std::int32_t var[kCdefNblocks][kCdefNblocks], std::int32_t cdefCount,
                         std::int32_t coeffShift, std::uint8_t dir[kCdefNblocks][kCdefNblocks]);

// ---- CDEF2 (PERRY-A8): the fb/lbd driver chain + cdef_copy ---------------

// CDEF_SEC_STRENGTHS (cdef.h:23): the cdef_strength encoding base.
constexpr int kCdefSecStrengths = 4;

// The strength derivation of cdef.c:327-329 / :551-553, factored out of the
// fb bodies for direct testability (named deviation, cdefSign precedent):
//   pri = (cdef_strength / CDEF_SEC_STRENGTHS) << coeff_shift
//   sec = (cdef_strength % CDEF_SEC_STRENGTHS + (sec == 3)) << coeff_shift
// i.e. sec codes {0,1,2,3} carry effective secondary strengths {0,1,2,4}.
std::int32_t cdefPriStrength(std::int32_t cdefStrength, std::int32_t coeffShift);
std::int32_t cdefSecStrength(std::int32_t cdefStrength, std::int32_t coeffShift);

// cdef_filter_fb_lbd_impl (cdef.c:542-638): the 8-bit production fb BODY.
// The reference force-inlines it into the per-plane call sites below so
// bsize/bsizex/bsizey/is_luma are compile-time constants (a perf property,
// not behavior - named deviation: exported as a plain function here, with
// the wrappers keeping the pinned constants). frame_* are the fb's
// frame-edge flags; by_last/bx_last derive from vsize/hsize (:557-558).
void cdefFilterFbLbdImpl(std::uint8_t* dst8, std::int32_t dstride, const std::uint8_t* in8, int frameTop,
                         int frameLeft, int frameBottom, int frameRight, int vsize, int hsize, std::int32_t xdec,
                         std::int32_t ydec, std::uint8_t dir[kCdefNblocks][kCdefNblocks], std::int32_t* dirinit,
                         std::int32_t var[kCdefNblocks][kCdefNblocks], std::int32_t pli, const CdefList* dlist,
                         std::int32_t cdefCount, std::int32_t cdefStrength, std::int32_t damping,
                         std::int32_t coeffShift, std::uint8_t subsamplingFactor, CdefBlockSize bsize,
                         std::int32_t bsizex, std::int32_t bsizey, int isLuma);

// cdef_filter_fb_lbd_luma/chroma (cdef.c:640-704) and
// svt_cdef_filter_fb_lbd (cdef.c:706-778): the thin per-plane wrappers
// pinning bsize/bsizex/bsizey/is_luma at compile time (luma 8X8/3/3/1,
// chroma 4X4/2/2/0) and the xdec/ydec dispatch - (0,0) luma, (1,1)
// chroma 4:2:0, any other pairing the runtime-variant impl body.
void cdefFilterFbLbd(std::uint8_t* dst8, std::int32_t dstride, const std::uint8_t* in8, int frameTop, int frameLeft,
                     int frameBottom, int frameRight, int vsize, int hsize, std::int32_t xdec, std::int32_t ydec,
                     std::uint8_t dir[kCdefNblocks][kCdefNblocks], std::int32_t* dirinit,
                     std::int32_t var[kCdefNblocks][kCdefNblocks], std::int32_t pli, const CdefList* dlist,
                     std::int32_t cdefCount, std::int32_t cdefStrength, std::int32_t damping,
                     std::int32_t coeffShift, std::uint8_t subsamplingFactor);

// svt_aom_copy_rect8_8bit_to_16bit_c (cdef.c:72-79): the u8->u16 widening
// copy the CDEF gather and copy_sb8_16 compose.
void cdefCopyRect8To16(std::uint16_t* dst, std::int32_t dstride, const std::uint8_t* src, std::int32_t sstride,
                       std::int32_t v, std::int32_t h);

// svt_cdef_copy_rect8 (cdef_copy.h:28-74): the 8-bit recon -> padded-src8
// rect copy. Dispatches on the fixed width set {2,32,34,36,64,66,68} to
// constant-size row copies (a perf property of the reference - behavior is
// identical for every width; the fallback covers any other h). The listed
// widths are all the CDEF gather ever uses (body hsz+2*halo, halo strips).
void cdefCopyRect8(std::uint8_t* dst, std::int32_t dstride, const std::uint8_t* src, std::int32_t srcVoffset,
                   std::int32_t srcHoffset, std::int32_t sstride, std::int32_t v, std::int32_t h);

// svt_aom_copy_rect (cdef_copy.h:79-85): the uint16->uint16 rect copy.
// The brief marks it HBD-only/PARKED, but copy_sb8_16's is_16bit branch
// calls it, so it lands as that composition's dependency (named deviation);
// its only other callers are the parked enc_cdef.c edge path.
void cdefCopyRect16(std::uint16_t* dst, std::int32_t dstride, const std::uint16_t* src, std::int32_t sstride,
                    std::int32_t v, std::int32_t h);

// svt_aom_fill_rect (cdef_copy.h:88-94): PARKED - its only callers are the
// parked enc_cdef.c halo fills (CDEF3); nothing in CDEF2 scope composes it.

// svt_aom_copy_sb8_16 (cdef_copy.h:96-105): the CDEF superblock copy into
// the padded src buffer - widen 8-bit recon (u8->u16), or copy 16-bit recon
// (u16->u16), from a (voffset, hoffset) origin; is16bit selects the branch.
void cdefCopySb8To16(std::uint16_t* dst, std::int32_t dstride, const std::uint8_t* src, std::int32_t srcVoffset,
                     std::int32_t srcHoffset, std::int32_t sstride, std::int32_t vsize, std::int32_t hsize,
                     int is16bit);

// svt_cdef_filter_fb (cdef.c:322-402): the 16-bit fb driver over the padded
// in buffer (stride CDEF_BSTRIDE). dst8/dst16: exactly one non-null - dst8
// writes the 8-bit search scratch, dst16 the 16-bit scratch; dstride == 0
// means the packed per-block search-scratch layout (bi << (bsizex+bsizey)).
// dirinit (the search's freshness flag, may be null): pli == 0 recomputes
// dir through the list walker exactly when !dirinit || !*dirinit, then sets
// *dirinit = 1. PLANE_Y is 0 (definitions.h:683), so pli != 0 is chroma.
void cdefFilterFb(std::uint8_t* dst8, std::uint16_t* dst16, std::int32_t dstride, const std::uint16_t* in,
                  std::int32_t xdec, std::int32_t ydec, std::uint8_t dir[kCdefNblocks][kCdefNblocks],
                  std::int32_t* dirinit, std::int32_t var[kCdefNblocks][kCdefNblocks], std::int32_t pli,
                  const CdefList* dlist, std::int32_t cdefCount, std::int32_t cdefStrength, std::int32_t damping,
                  std::int32_t coeffShift, std::uint8_t subsamplingFactor);

} // namespace l10_filters
