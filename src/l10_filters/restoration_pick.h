#pragma once

#include <cstdint>

#include "restoration.h"

// A8R (A8 campaign): 1:1 port of the PURE-MATH spans of
// third_party/SVT-AV1/Source/Lib/Codec/restoration_pick.c:
//   svt_av1_lowbd_pixel_proj_error_c :161-:226,
//   svt_av1_highbd_pixel_proj_error_c :228-:299 (uint16 planes aliased
//   through the uint8 params; MC1 sseHighbd precedent, named deviation),
//   get_pixel_proj_error :301-:322 (the CONFIG_ENABLE_HIGH_BIT_DEPTH=0
//   shape: use_highbitdepth is UNUSED and the lowbd path always runs),
//   finer_search_pixel_proj_error :324-:420,
//   svt_get_proj_subspace_c :422-:510,
//   encode_xq :512-:524,
//   apply_sgr :526-:552,
//   search_selfguided_restoration :554-:630,
//   count_sgrproj_bits :634-:650,
//   svt_av1_compute_stats_c :652-:690,
//   svt_av1_compute_stats_highbd_c :692-:743 (EbBitDepth folded to its
//   integer values 8/10/12, API/EbSvtAv1Formats.h:102-:105; named
//   deviation),
//   wrap_index :745-:750,
//   linsolve_wiener :752-:796,
//   update_a_sep_sym :798-:848,
//   update_b_sep_sym :850-:899,
//   wiener_decompose_sep_sym :901-:932,
//   compute_score :934-:971,
//   finalize_sym_filter :973-:1003,
//   count_wiener_bits :1005-:1039.
// CARRIED DEPENDENCIES (not restoration_pick.c lines): find_average /
// find_average_highbd are static INLINE in the reference HEADER
// restoration_pick.h:21-:43; the count chain
// svt_aom_count_primitive_refsubexpfin (entropy_coding.c:2992-:2994) needs
// recenter_nonneg :2842-:2850, recenter_finite_nonneg :2854-:2860,
// svt_aom_count_primitive_quniform :2891-:2898,
// svt_aom_count_primitive_subexpfin :2947-:2970 - ported here file-local
// because the l7 entropy surface does not export them (grep-verified); the
// per-module duplication precedent is l13 palette.cpp av1CeilLog2.
// PARKED (named, port nothing): the RestSearchCtxt/rsc driver functions -
// sse_part_extractors/sse_restoration_unit :33-:52 (Yv12BufferConfig-walled),
// rsc_on_tile :85-:93, reset_rsc :95-:98, init_rsc_seg :100-:121,
// try_restoration_unit_seg :123-:159, finer_tile_search_wiener_seg
// :1041-:1152, search_switchable :1154-:1218, copy_unit_info :1220-:1229,
// rest_tiles_in_plane :1231-:1234, search_sgrproj_seg :1237-:1273,
// search_sgrproj_finish :1276-:1303, search_wiener_seg :1306-:1380,
// search_wiener_finish :1383-:1430, search_norestore_seg :1432-:1441,
// search_norestore_finish :1444-:1455, search_rest_type_finish :1458-:1467,
// restoration_seg_search :1474-:1556, rest_finish_search :1561-:1646.
// Coverage-only slice: moves no conformance count, touches no bitstream;
// the restoration header surface stays disabled (enable_restoration 0).

namespace l10_filters {

namespace restoration_pick {

// WIENER_HALFWIN / WIENER_HALFWIN1 / WIENER_WIN / WIENER_WIN2 /
// WIENER_WIN_CHROMA (restoration.h:115-:124).
constexpr std::int32_t wienerHalfwin   = 3;
constexpr std::int32_t wienerHalfwin1  = wienerHalfwin + 1;
constexpr std::int32_t wienerWin       = 2 * wienerHalfwin + 1;
constexpr std::int32_t wienerWin2      = wienerWin * wienerWin;
constexpr std::int32_t wienerWinChroma = wienerWin - 2;

// SGRPROJ_PRJ_MIN0/MAX0/MIN1/MAX1 (restoration.h:101-:104) and
// SGRPROJ_PARAMS_BITS (:90) / SGRPROJ_PRJ_SUBEXP_K (:106).
constexpr std::int32_t sgrprojPrjMin0    = -(1 << restoration::sgrprojPrjBits) * 3 / 4;
constexpr std::int32_t sgrprojPrjMax0    = sgrprojPrjMin0 + (1 << restoration::sgrprojPrjBits) - 1;
constexpr std::int32_t sgrprojPrjMin1    = -(1 << restoration::sgrprojPrjBits) / 4;
constexpr std::int32_t sgrprojPrjMax1    = sgrprojPrjMin1 + (1 << restoration::sgrprojPrjBits) - 1;
constexpr std::int32_t sgrprojParamsBits = 4;
constexpr std::int32_t sgrprojPrjSubexpK = 4;

// NUM_WIENER_ITERS (restoration_pick.c:29) and WIENER_TAP_SCALE_FACTOR
// (restoration_pick.c:31), the working precision of the wiener taps.
constexpr std::int32_t numWienerIters    = 5;
constexpr std::int64_t wienerTapScaleFactor = 1 << 16;

// The WIENER_FILT_* tap family (restoration.h:125-:151): precision, the
// three free-tap midvalues/bits, the derived MINV/MAXV ranges and the
// per-tap subexpfin k.
constexpr std::int32_t wienerFiltPrecBits = 7;
constexpr std::int32_t wienerFiltStep     = 1 << wienerFiltPrecBits;
constexpr std::int32_t wienerFiltTap0Midv = 3;
constexpr std::int32_t wienerFiltTap1Midv = -7;
constexpr std::int32_t wienerFiltTap2Midv = 15;
constexpr std::int32_t wienerFiltTap0Bits = 4;
constexpr std::int32_t wienerFiltTap1Bits = 5;
constexpr std::int32_t wienerFiltTap2Bits = 6;
constexpr std::int32_t wienerFiltTap0Minv = wienerFiltTap0Midv - (1 << wienerFiltTap0Bits) / 2;
constexpr std::int32_t wienerFiltTap1Minv = wienerFiltTap1Midv - (1 << wienerFiltTap1Bits) / 2;
constexpr std::int32_t wienerFiltTap2Minv = wienerFiltTap2Midv - (1 << wienerFiltTap2Bits) / 2;
constexpr std::int32_t wienerFiltTap0Maxv = wienerFiltTap0Midv - 1 + (1 << wienerFiltTap0Bits) / 2;
constexpr std::int32_t wienerFiltTap1Maxv = wienerFiltTap1Midv - 1 + (1 << wienerFiltTap1Bits) / 2;
constexpr std::int32_t wienerFiltTap2Maxv = wienerFiltTap2Midv - 1 + (1 << wienerFiltTap2Bits) / 2;
constexpr std::int32_t wienerFiltTap0SubexpK = 1;
constexpr std::int32_t wienerFiltTap1SubexpK = 2;
constexpr std::int32_t wienerFiltTap2SubexpK = 3;

// wrap_index (restoration_pick.c:745-:750): fold an index in
// [0, wiener_win-1] onto the first (wiener_win>>1)+1 taps (the symmetric
// half). File-local static INLINE in the reference; exported for direct
// testability (named deviation, LR1 boxsum1 precedent).
std::int32_t wrapIndex(std::int32_t i, std::int32_t wiener_win);

// find_average (restoration_pick.h:21-:31, the reference HEADER): the
// uint64 sum of the src window floored at (v_end-v_start)*(h_end-h_start),
// truncated to uint8. Static INLINE in the reference; exported (named
// deviation).
std::uint8_t findAverage(const std::uint8_t* src, std::int32_t h_start, std::int32_t h_end, std::int32_t v_start,
    std::int32_t v_end, std::int32_t stride);

// find_average_highbd (restoration_pick.h:33-:43): the uint16 twin.
// Static INLINE in the reference; exported (named deviation).
std::uint16_t findAverageHighbd(const std::uint16_t* src, std::int32_t h_start, std::int32_t h_end,
    std::int32_t v_start, std::int32_t v_end, std::int32_t stride);

// svt_av1_compute_stats_c (restoration_pick.c:652-:690): the wiener
// normal-equation statistics. M is wiener_win*wiener_win; H is
// wiener_win2*wiener_win2, filled on the upper triangle inside the walk and
// mirrored after. The average is taken over the DGD window.
// Non-static in the reference (rtcd-dispatched); ported under the reference
// name with the _c suffix dropped.
void computeStats(std::int32_t wiener_win, const std::uint8_t* dgd, const std::uint8_t* src, std::int32_t h_start,
    std::int32_t h_end, std::int32_t v_start, std::int32_t v_end, std::int32_t dgd_stride, std::int32_t src_stride,
    std::int64_t* M, std::int64_t* H);

// svt_av1_compute_stats_highbd_c (restoration_pick.c:692-:743): the uint16
// twin. The uint16 planes are ALIASED through the uint8 params (the
// reference's CONVERT_TO_SHORTPTR reads; MC1 sseHighbd precedent, named
// deviation). EbBitDepth is folded to its integer values (8/10/12,
// API/EbSvtAv1Formats.h:102-:105): the divider is 16 at 12, 4 at 10, else
// 1, applied to M and H after the walk.
void computeStatsHighbd(std::int32_t wiener_win, const std::uint8_t* dgd8, const std::uint8_t* src8,
    std::int32_t h_start, std::int32_t h_end, std::int32_t v_start, std::int32_t v_end, std::int32_t dgd_stride,
    std::int32_t src_stride, std::int64_t* M, std::int64_t* H, std::int32_t bit_depth);

// svt_av1_lowbd_pixel_proj_error_c (restoration_pick.c:161-:226): the
// four-branch SGR projection SSE - both filters, flt0 only, flt1 only, and
// the plain (dat - src) SSE fallback, selected by params->r[0]/r[1].
// Non-static in the reference (rtcd-dispatched); ported under the
// reference name with the _c suffix dropped.
std::int64_t lowbdPixelProjError(const std::uint8_t* src8, std::int32_t width, std::int32_t height,
    std::int32_t src_stride, const std::uint8_t* dat8, std::int32_t dat_stride, std::int32_t* flt0,
    std::int32_t flt0_stride, std::int32_t* flt1, std::int32_t flt1_stride, const std::int32_t* xq,
    const restoration::SgrParamsType* params);

// svt_av1_highbd_pixel_proj_error_c (restoration_pick.c:228-:299): the
// uint16 twin. TRUNCATING shift form - v starts at
// 1 << (RST + PRJ - 1) and e = (v >> (RST + PRJ)) + d - s - which differs
// from the lowbd ROUND_POWER_OF_TWO form at v = +-(1 << (RST+PRJ-1))
// (pinned by test). The uint16 planes are ALIASED through the uint8 params
// (CONVERT_TO_SHORTPTR read; MC1 sseHighbd precedent, named deviation).
std::int64_t highbdPixelProjError(const std::uint8_t* src8, std::int32_t width, std::int32_t height,
    std::int32_t src_stride, const std::uint8_t* dat8, std::int32_t dat_stride, std::int32_t* flt0,
    std::int32_t flt0_stride, std::int32_t* flt1, std::int32_t flt1_stride, const std::int32_t* xq,
    const restoration::SgrParamsType* params);

// get_pixel_proj_error (restoration_pick.c:301-:322): decode_xq then the
// projection. Ported in the CONFIG_ENABLE_HIGH_BIT_DEPTH=0 shape: the
// use_highbitdepth parameter is UNUSED (named deviation - the highbd
// dispatch else-branch does not exist in that build) and the lowbd path
// always runs. File-local static in the reference; exported (named
// deviation).
std::int64_t getPixelProjError(const std::uint8_t* src8, std::int32_t width, std::int32_t height,
    std::int32_t src_stride, const std::uint8_t* dat8, std::int32_t dat_stride, std::int32_t use_highbitdepth,
    std::int32_t* flt0, std::int32_t flt0_stride, std::int32_t* flt1, std::int32_t flt1_stride, std::int32_t* xqd,
    const restoration::SgrParamsType* params);

// encode_xq (restoration_pick.c:512-:524): the xq -> xqd inverse map with
// the per-branch clamps. Static INLINE in the reference; exported (named
// deviation).
void encodeXq(std::int32_t* xq, std::int32_t* xqd, const restoration::SgrParamsType* params);

// linsolve_wiener (restoration_pick.c:752-:796): Gaussian elimination with
// partial pivoting over int64, the LOSSY forward elimination
// (c / 256 * A / cd * 256 truncation chain) and the back-substitution at
// the WIENER_TAP_SCALE_FACTOR (1 << 16) tap scale. Returns 0 on a zero
// pivot (which the lossy elimination can produce for solvable systems -
// pinned by test). File-local static in the reference; exported (named
// deviation). A and b are CONSUMED (row-swapped/eliminated in place).
std::int32_t linsolveWiener(std::int32_t n, std::int64_t* A, std::int32_t stride, std::int64_t* b, std::int32_t* x);

// finer_search_pixel_proj_error (restoration_pick.c:324-:420): the xqd
// hill-climb - halving steps from start_step down to 1, per-tap negative
// then positive walks bounded by SGRPROJ_PRJ_MIN/MAX, the skip flag that
// abandons the remaining taps at a step once a negative move succeeded,
// and the continue-same-direction behavior at the highest step.
// xqd is CONSUMED (walked in place). use_highbitdepth is threaded UNUSED
// (the CONFIG_ENABLE_HIGH_BIT_DEPTH=0 shape, named deviation). File-local
// static in the reference; exported (named deviation).
std::int64_t finerSearchPixelProjError(const std::uint8_t* src8, std::int32_t width, std::int32_t height,
    std::int32_t src_stride, const std::uint8_t* dat8, std::int32_t dat_stride, std::int32_t use_highbitdepth,
    std::int32_t* flt0, std::int32_t flt0_stride, std::int32_t* flt1, std::int32_t flt1_stride,
    std::int32_t start_step, std::int32_t* xqd, std::int8_t do_refine, const restoration::SgrParamsType* params);

// svt_get_proj_subspace_c (restoration_pick.c:422-:510): the least-squares
// projection subsolve - the double H/C accumulation (u = dat << RST, s =
// (src << RST) - u, f = flt - u), the /size normalization, then the three
// r-branches (scalar H11, scalar H00, full 2x2) with the 1e-8 ill-posed
// guard returning the {0, 0} default and rint at SGRPROJ_PRJ_BITS.
// The use_highbitdepth else-branch reads the uint16 planes ALIASED through
// the uint8 params (CONVERT_TO_SHORTPTR fold, named deviation).
// Non-static in the reference; ported under the reference name with the
// _c suffix dropped.
void getProjSubspace(const std::uint8_t* src8, std::int32_t width, std::int32_t height, std::int32_t src_stride,
    const std::uint8_t* dat8, std::int32_t dat_stride, std::int32_t use_highbitdepth, std::int32_t* flt0,
    std::int32_t flt0_stride, std::int32_t* flt1, std::int32_t flt1_stride, std::int32_t* xq,
    const restoration::SgrParamsType* params);

// apply_sgr (restoration_pick.c:526-:552): tile the restoration unit into
// pu_width x pu_height blocks (AOMMIN-truncated edges) and run the landed
// restoration::selfguidedRestoration on each. The flt planes share
// flt_stride across the WHOLE unit. Static INLINE in the reference;
// exported (named deviation).
void applySgr(std::int32_t sgr_params_idx, const std::uint8_t* dat8, std::int32_t width, std::int32_t height,
    std::int32_t dat_stride, std::int32_t use_highbd, std::int32_t bit_depth, std::int32_t pu_width,
    std::int32_t pu_height, std::int32_t* flt0, std::int32_t* flt1, std::int32_t flt_stride);

// SgFilterCtrls (av1_common.h:47-:56), minimal carry: only the four
// per-plane search control arrays search_selfguided_restoration reads; the
// enabled/use_chroma bools are unread by the ported span (named deviation,
// the RE8A DeblockContext precedent). PLANE_TYPES = 2.
struct SgFilterCtrls {
    std::int8_t startEp[2];
    std::int8_t endEp[2];
    std::int8_t epInc[2];
    std::int8_t refine[2];
};

// search_selfguided_restoration (restoration_pick.c:554-:630): the SGR
// search capstone - per ep in [start_ep, end_ep): apply_sgr into rstbuf
// (flt1 = flt0 + RESTORATION_UNITPELS_MAX, flt_stride = ((width+7)&~7)+8),
// svt_get_proj_subspace, encode_xq, then finer_search_pixel_proj_error at
// start_step 2; the (ep, xqd) with the best err wins. plane is normalized
// !!plane. Static in the reference; exported (named deviation). rstbuf
// needs 2 * RESTORATION_UNITPELS_MAX int32.
restoration::SgrprojInfo searchSelfguidedRestoration(const std::uint8_t* dat8, std::int32_t width,
    std::int32_t height, std::int32_t dat_stride, const std::uint8_t* src8, std::int32_t src_stride,
    std::int32_t use_highbitdepth, std::int32_t bit_depth, std::int32_t pu_width, std::int32_t pu_height,
    std::int32_t* rstbuf, SgFilterCtrls* ctrls, std::int32_t plane);

// The subexpfin COUNT chain, carried from entropy_coding.c because the l7
// entropy surface does not export it (grep-verified; per-module
// duplication precedent): recenter_nonneg :2842-:2850,
// recenter_finite_nonneg :2854-:2860, svt_aom_count_primitive_quniform
// :2891-:2898, svt_aom_count_primitive_subexpfin :2947-:2970,
// svt_aom_count_primitive_refsubexpfin :2992-:2994. All file-local static
// in the reference; exported for direct testability (named deviation).
std::int32_t countPrimitiveQuniform(std::uint16_t n, std::uint16_t v);
std::int32_t countPrimitiveSubexpfin(std::uint16_t n, std::uint16_t k, std::uint16_t v);
std::int32_t countPrimitiveRefsubexpfin(std::uint16_t n, std::uint16_t k, std::uint16_t ref, std::uint16_t v);

// count_sgrproj_bits (restoration_pick.c:634-:650): SGRPROJ_PARAMS_BITS
// plus one refsubexpfin count per active radius, over the
// SGRPROJ_PRJ_MIN/MAX-normalized xqd deltas. Static in the reference;
// exported (named deviation).
std::int32_t countSgrprojBits(restoration::SgrprojInfo* sgrproj_info, restoration::SgrprojInfo* ref_sgrproj_info);

// count_wiener_bits (restoration_pick.c:1005-:1039): one refsubexpfin
// count per free tap of each filter; at wiener_win == WIENER_WIN the tap0
// terms are added for BOTH vfilter and hfilter (2 per side), taps 1/2
// always. Static in the reference; exported (named deviation).
std::int32_t countWienerBits(std::int32_t wiener_win, restoration::WienerInfo* wiener_info,
    restoration::WienerInfo* ref_wiener_info);

// update_a_sep_sym (restoration_pick.c:798-:848): fix vector b, update
// vector a - the A[jj]/B[ll*halfwin1+kk] accumulation over the mc/hc row
// pointers (hc index j*win + i), the normalization-enforcement algebra on
// A/B, then linsolve at halfwin1-1 unknowns; on success a is the mirrored
// S with S[center] = WIENER_TAP_SCALE_FACTOR - 2*(symmetric taps); on
// linsolve failure a is UNTOUCHED. mc is win row pointers into M (row
// stride win); hc is win2 row pointers into H (row stride win2). Static
// in the reference; exported (named deviation).
void updateASepSym(std::int32_t wiener_win, std::int64_t** mc, std::int64_t** hc, std::int32_t* a, std::int32_t* b);

// update_b_sep_sym (restoration_pick.c:850-:899): fix vector a, update
// vector b - the twin with a[j] in the A[ii] accumulation, the hc row
// index i*win + j (NOT update_a's j*win + i) and the transposed
// B[jj*halfwin1 + ii] slot. On linsolve failure b is UNTOUCHED. Static in
// the reference; exported (named deviation).
void updateBSepSym(std::int32_t wiener_win, std::int64_t** Mc, std::int64_t** hc, std::int32_t* a, std::int32_t* b);

// wiener_decompose_sep_sym (restoration_pick.c:901-:932): seed a/b from
// the init_filt taps (WIENER_FILT_TAP0/1/2_MIDV and the derived center
// 106) scaled by WIENER_TAP_SCALE_FACTOR / WIENER_FILT_STEP = 512, offset
// by plane_off = (WIENER_WIN - wiener_win) >> 1, then NUM_WIENER_ITERS-1
// = 4 alternating (update_a, update_b) iterations. Static in the
// reference; exported (named deviation).
void wienerDecomposeSepSym(std::int32_t wiener_win, std::int64_t* M, std::int64_t* H, std::int32_t* a,
    std::int32_t* b);

// compute_score (restoration_pick.c:934-:971): the x'Ax - x'b quadratic
// form of the finalized taps against the normal equations, compared to
// the identity-filter score at the M/H center - the caller reverts to
// RESTORE_NONE when this is positive. P accumulates ab[k]*M[k] with two
// sequential /WIENER_FILT_STEP truncations, Q ab[k]*H[k][l]*ab[l] with
// four. Static in the reference; exported (named deviation). vfilt/hfilt
// are the finalized InterpKernel taps (only [0..2] read).
std::int64_t computeScore(std::int32_t wiener_win, std::int64_t* M, std::int64_t* H,
    restoration::WienerKernel vfilt, restoration::WienerKernel hfilt);

// finalize_sym_filter (restoration_pick.c:973-:1003): round the first
// halfwin tap-scaler entries into filter units (round-half-away-from-zero
// at WIENER_FILT_STEP / WIENER_TAP_SCALE_FACTOR), clamp per tap (at
// wiener_win == WIENER_WIN taps 0/1/2 clamp to their own ranges; else
// fi[2] = CLIP(fi[1], TAP2 range), fi[1] = CLIP(fi[0], TAP1 range),
// fi[0] = 0 - the UNCLAMPED fi[1] feeds fi[2], order pinned by test),
// then mirror and set fi[3] = -2*(fi[0]+fi[1]+fi[2]). Static in the
// reference; exported (named deviation).
void finalizeSymFilter(std::int32_t wiener_win, std::int32_t* f, restoration::WienerKernel fi);

} // namespace restoration_pick

} // namespace l10_filters
