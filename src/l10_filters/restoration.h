#pragma once

#include <cstdint>

// LR1 (A8 campaign): 1:1 port of the restoration math core of
// third_party/SVT-AV1/Source/Lib/Codec/restoration.c (1534 nb total):
//   sgr_params :31-:49 (the SgrParamsType[16] {r[2], s[2]} table),
//   extend_frame_lowbd :110-:126 + the svt_extend_frame entry :150-:157,
//   copy_tile_lowbd :159-:164 + the copy_tile dispatcher :173-:180
//   (8-bit path only; highbd named-parked),
//   boxsum1 :430-:495, boxsum2 :497-:584, boxsum :586-:594 (the
//   horizontal+vertical prefix-boxsum family over int32 buffers with the
//   sqr flag; the edge rows/cols of the given array get TRUNCATED sums:
//   2-sums at the outermost rows/cols for r=1, 3-sums for r=2 - the walk
//   never reads outside the given width/height),
//   svt_decode_xq :597-:608 (params-aware: r[0]==0 -> xq[0]=0,
//   xq[1]=(1<<SGRPROJ_PRJ_BITS)-xqd[1]; r[1]==0 -> xq[0]=xqd[0], xq[1]=0;
//   else xq[0]=xqd[0], xq[1]=(1<<7)-xq[0]-xqd[1]; SGRPROJ_PRJ_BITS = 7,
//   restoration.h:94),
//   x_by_xplus1 :610-:625 (256 entries, entry 0 special-cased to 1),
//   one_by_x :627-:630 (25 entries = MAX_NELEM, MAX_RADIUS = 2,
//   restoration.h:110-111).
// LR2 (A8 campaign): the selfguided family :632-:962 -
//   selfguided_restoration_fast_internal :632-:764 (estimation rows
//   stepped by 2, row-parity *6/*5 kernels, assert(r == 2) at the filter),
//   selfguided_restoration_internal :766-:884 (row stride 1, 3x3 *4/*3
//   kernel, nb = 5),
//   svt_av1_selfguided_restoration_c :886-:922 (the dgd32 staging copy +
//   the r[0]/r[1] skip pair),
//   svt_apply_selfguided_restoration_c :924-:962 (tmpbuf flt0/flt1, the
//   decodeXq composition, the u/v accumulation, the >> (PRJ+RST) and the
//   clip_pixel_highbd write).
// PARKED (named, port nothing): the stripe machinery
// setup/restore_processing_stripe_boundary :249-:397 and
// get_stripe_boundary_info :216 (RestorationTileLimits /
// RestorationLineBuffers struct-coupled), wiener_filter_stripe :399
// (RestorationUnitInfo + the convolve.c A13 dependency),
// sgrproj/wiener stripe + highbd :987-:1035, the
// :1040+ filter_unit/frame walkers, :1124+ the tile walkers and
// boundary-line savers.
// Coverage-only slice: moves no conformance count, touches no bitstream;
// the restoration header surface stays disabled (enable_restoration 0).
// BRIEF CORRECTION (worklog): the brief's decode_xq description
// ("0 -> 96 else 128 - 3*xqd[0], xqd[1] -> 5*xqd[1]") is the OLD spec
// decode_xq and does NOT exist in this tree; the vendored :597-:608 is the
// params-aware version ported above (grep-verified).

namespace l10_filters {

namespace restoration {

// SgrParamsType (restoration.h:86-89 shape): {r[2], s[2]} per
// sgr_params_idx. r = 0 skips the filter; the paired s is -1 (invalid).
struct SgrParamsType {
    std::int32_t r[2];
    std::int32_t s[2];
};

// LR2: the SGR constants of the reference header, verbatim values.
// SGRPROJ_BORDER_VERT / SGRPROJ_BORDER_HORZ (restoration.h:41-:42).
constexpr std::int32_t sgrprojBorderVert = 3;
constexpr std::int32_t sgrprojBorderHorz = 3;
// SGRPROJ_PRJ_BITS (:94), SGRPROJ_RST_BITS (:96), SGRPROJ_SGR_BITS (:98),
// SGRPROJ_SGR (:99).
constexpr std::int32_t sgrprojPrjBits = 7;
constexpr std::int32_t sgrprojRstBits = 4;
constexpr std::int32_t sgrprojSgrBits = 8;
constexpr std::int32_t sgrprojSgr = 1 << sgrprojSgrBits;
// MAX_RADIUS (:110); MAX_NELEM = 25 is the oneByX extent (:111).
constexpr std::int32_t maxRadius = 2;
// SGRPROJ_MTABLE_BITS (:112), SGRPROJ_RECIP_BITS (:113).
constexpr std::int32_t sgrprojMtableBits = 20;
constexpr std::int32_t sgrprojRecipBits = 12;
// RESTORATION_PROC_UNIT_PELS (:76-:78): (RESTORATION_PROC_UNIT_SIZE 64,
// :36 + RESTORATION_BORDER_HORZ/VERT = SGRPROJ_* via the :52/:61 #if, +
// RESTORATION_PADDING 20, :75) per side.
constexpr std::int32_t restorationProcUnitPels = (64 + sgrprojBorderHorz * 2 + 20) *
    (64 + sgrprojBorderVert * 2 + 20);
// RESTORATION_UNITPELS_MAX (:84): RESTORATION_UNITSIZE_MAX 256 (:80) * 3/2
// + 2*border + 16 (:81) x + 2*border + RESTORATION_UNIT_OFFSET 8 (:82-:83).
constexpr std::int32_t restorationUnitpelsMax =
    (256 * 3 / 2 + 2 * sgrprojBorderHorz + 16) * (256 * 3 / 2 + 2 * sgrprojBorderVert + 8);

// sgr_params (restoration.c:31-49): 16 rows indexed by sgr_params_idx.
extern const SgrParamsType sgrParams[16];

// boxsum1 (restoration.c:430-:495): radius-1 box sum, vertical-then-
// horizontal prefix walk. File-local static in the reference; exported for
// direct testability (named deviation, cdefSign precedent). The edge
// rows/cols of the given array get truncated sums (2-sums); no reads
// outside width/height.
void boxsum1(std::int32_t* src, std::int32_t width, std::int32_t height, std::int32_t src_stride, std::int32_t sqr,
             std::int32_t* dst, std::int32_t dst_stride);

// boxsum2 (restoration.c:497-:584): radius-2 twin of boxsum1; the outermost
// rows/cols get truncated 3-sums.
void boxsum2(std::int32_t* src, std::int32_t width, std::int32_t height, std::int32_t src_stride, std::int32_t sqr,
             std::int32_t* dst, std::int32_t dst_stride);

// boxsum (restoration.c:586-:594): the dispatch wrapper, r == 1 -> boxsum1,
// r == 2 -> boxsum2, anything else is invalid (assert(0) in the reference).
void boxsum(std::int32_t* src, std::int32_t width, std::int32_t height, std::int32_t src_stride, std::int32_t r,
            std::int32_t sqr, std::int32_t* dst, std::int32_t dst_stride);

// svt_decode_xq (restoration.c:597-:608): the params-aware xqd -> xq map
// (SGRPROJ_PRJ_BITS = 7 base, the LR2 sgrprojPrjBits constant). Named
// deviation: exported under decodeXq.
void decodeXq(const std::int32_t* xqd, std::int32_t* xq, const SgrParamsType* params);

// x_by_xplus1 (restoration.c:610-:625): 256 entries, entry 0 special-cased
// to 1, entry 255 = 256.
extern const std::int32_t xByXplus1[256];

// one_by_x (restoration.c:627-:630): 25 entries = MAX_NELEM.
extern const std::int32_t oneByX[25];

// extend_frame_lowbd (restoration.c:110-:126): fill the side borders by
// row-end pixel replication, then copy the extended first/last rows
// (width + 2*border_horz bytes) into the top/bottom borders. File-local in
// the reference; exported for direct testability (named deviation).
void extendFrameLowbd(std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t stride,
                      std::int32_t border_horz, std::int32_t border_vert);

// svt_extend_frame (restoration.c:150-:157): the highbd dispatcher. The
// highbd branch is PARKED (named): assert(0), unreachable in our emission
// (enable_restoration 0, 8-bit mono); extend_frame_highbd :128-:148 is not
// ported.
void extendFrame(std::uint8_t* data, std::int32_t width, std::int32_t height, std::int32_t stride,
                 std::int32_t border_horz, std::int32_t border_vert, std::int32_t highbd);

// copy_tile_lowbd (restoration.c:159-:164): row-by-row width-byte copy.
// File-local in the reference; exported for direct testability (named
// deviation).
void copyTileLowbd(std::int32_t width, std::int32_t height, const std::uint8_t* src, std::int32_t src_stride,
                   std::uint8_t* dst, std::int32_t dst_stride);

// copy_tile (restoration.c:173-:180): the highbd dispatcher; the highbd
// branch is PARKED (named): assert(0), copy_tile_highbd :166-:171 not
// ported.
void copyTile(std::int32_t width, std::int32_t height, const std::uint8_t* src, std::int32_t src_stride,
              std::uint8_t* dst, std::int32_t dst_stride, std::int32_t highbd);

// selfguided_restoration_fast_internal (restoration.c:632-:764): the
// radius-r boxsums over the 3-bordered dgd, the z/A/B estimation pass
// (rows stepped by 2), then the row-parity *6/*5 filter kernels. File-local
// static in the reference; exported for direct testability (named
// deviation, LR1 boxsum1 precedent). Requires r == 2 (assert) and the dgd
// buffer to carry 3 border rows/cols around the width x height content.
void selfguidedRestorationFastInternal(std::int32_t* dgd, std::int32_t width, std::int32_t height,
    std::int32_t dgd_stride, std::int32_t* dst, std::int32_t dst_stride, std::int32_t bit_depth,
    std::int32_t sgr_params_idx, std::int32_t radius_idx);

// selfguided_restoration_internal (restoration.c:766-:884): the slow twin -
// the same estimation pass with the row stride 1, then the 3x3 *4/*3
// kernel (nb = 5, no r == 2 assert). File-local static in the reference;
// exported for direct testability (named deviation).
void selfguidedRestorationInternal(std::int32_t* dgd, std::int32_t width, std::int32_t height,
    std::int32_t dgd_stride, std::int32_t* dst, std::int32_t dst_stride, std::int32_t bit_depth,
    std::int32_t sgr_params_idx, std::int32_t radius_idx);

// svt_av1_selfguided_restoration_c (restoration.c:886-:922): stage the
// 3-bordered dat8 window into a stack int32 buffer, then the r[0]/r[1]
// skip pair - fast into flt0, slow into flt1, a zero radius leaves its
// flt untouched. The highbd branch is PARKED (named): CONVERT_TO_SHORTPTR
// reads assert(0), unreachable in our 8-bit-only emission
// (enable_restoration 0).
void selfguidedRestoration(const std::uint8_t* dgd8, std::int32_t width, std::int32_t height,
    std::int32_t dgd_stride, std::int32_t* flt0, std::int32_t* flt1, std::int32_t flt_stride,
    std::int32_t sgr_params_idx, std::int32_t bit_depth, std::int32_t highbd);

// svt_apply_selfguided_restoration_c (restoration.c:924-:962): both
// filters into tmpbuf (flt1 = flt0 + RESTORATION_UNITPELS_MAX), decodeXq,
// then the per-pixel u/v accumulation, the >> (SGRPROJ_PRJ_BITS +
// SGRPROJ_RST_BITS) and the clip_pixel_highbd write. tmpbuf needs
// 2 * RESTORATION_UNITPELS_MAX int32; dat8 must carry 3 border rows/cols.
// The highbd reads/writes are PARKED (named, unreachable in our 8-bit
// emission).
void applySelfguidedRestoration(const std::uint8_t* dat8, std::int32_t width, std::int32_t height,
    std::int32_t stride, std::int32_t eps, const std::int32_t* xqd, std::uint8_t* dst8,
    std::int32_t dst_stride, std::int32_t* tmpbuf, std::int32_t bit_depth, std::int32_t highbd);

} // namespace restoration

} // namespace l10_filters
