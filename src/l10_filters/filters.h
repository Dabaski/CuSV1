#pragma once

#include <cstdint>

namespace l10_filters {

// SIMD_WIDTH (definitions.h:1653) and MAX_LOOP_FILTER (definitions.h:1652).
constexpr int kSimdWidth = 16;
constexpr int kMaxLoopFilter = 63;

// LoopFilterThresh (definitions.h:1678-1682): the per-level limit triple,
// each a SIMD_WIDTH byte array (memset-filled by update_sharpness).
struct LoopFilterThresh {
    std::uint8_t mblim[kSimdWidth];
    std::uint8_t lim[kSimdWidth];
    std::uint8_t hev_thr[kSimdWidth];
};

// LoopFilterInfoN (definitions.h:1684-1687), the lfthr table only - the lvl
// member is the loop_filter_frame_init driver's, not in DF1 scope.
struct LoopFilterInfoN {
    LoopFilterThresh lfthr[kMaxLoopFilter + 1];
};

// DF1 (A8 campaign, deblocking kernel family). 1:1 port of the 8-bit
// deblocking mask/filter surface of
// third_party/SVT-AV1/Source/Lib/Codec/deblocking_common.c:
//   filter_mask2 :152-:158, filter_mask :160-:171,
//   filter_mask3_chroma :173-:182, flat_mask3_chroma :184-:192,
//   flat_mask4 :205-:215, hev_mask :218-:223, filter4 :225-:251,
//   lpf_horizontal_4_c :253-:267, lpf_vertical_4_c :269-:283,
//   filter6 :285-:299, filter8 :301-:317,
//   lpf_horizontal_6_c :319-:335, lpf_vertical_6_c :337-:353,
//   lpf_horizontal_8_c :355-:371, lpf_vertical_8_c :373-:386,
//   svt_aom_update_sharpness :568-:589, plus the signed_char_clamp helper
//   :30-:32 that filter4 requires. highbd_* :194-:203/:388-:567/:591-:780
//   and signed_char_clamp_high :34-:44 are PARKED (high bit depth).
// COVERAGE-ONLY slice: moves no conformance count, touches no bitstream.
// The lpf_* entry points take blimit/limit/thresh as scalar uint8_t (the
// reference reads element [0] of SIMD_WIDTH=16 byte arrays).

// filter_mask2 (deblocking_common.c:152-158): 11111111 yes, 00000000 no.
std::int8_t filterMask2(std::uint8_t limit, std::uint8_t blimit, std::uint8_t p1, std::uint8_t p0,
                        std::uint8_t q0, std::uint8_t q1);

// filter_mask (deblocking_common.c:160-171): the 9-tap mask.
std::int8_t filterMask(std::uint8_t limit, std::uint8_t blimit, std::uint8_t p3, std::uint8_t p2, std::uint8_t p1,
                       std::uint8_t p0, std::uint8_t q0, std::uint8_t q1, std::uint8_t q2, std::uint8_t q3);

// filter_mask3_chroma (deblocking_common.c:173-182): the 6-tap chroma mask.
std::int8_t filterMask3Chroma(std::uint8_t limit, std::uint8_t blimit, std::uint8_t p2, std::uint8_t p1,
                              std::uint8_t p0, std::uint8_t q0, std::uint8_t q1, std::uint8_t q2);

// flat_mask3_chroma (deblocking_common.c:184-192): the chroma flatness gate.
std::int8_t flatMask3Chroma(std::uint8_t thresh, std::uint8_t p2, std::uint8_t p1, std::uint8_t p0, std::uint8_t q0,
                            std::uint8_t q1, std::uint8_t q2);

// flat_mask4 (deblocking_common.c:205-215): the 8-tap flatness gate.
std::int8_t flatMask4(std::uint8_t thresh, std::uint8_t p3, std::uint8_t p2, std::uint8_t p1, std::uint8_t p0,
                      std::uint8_t q0, std::uint8_t q1, std::uint8_t q2, std::uint8_t q3);

// hev_mask (deblocking_common.c:218-223): NOT inverted, unlike the masks -
// -1 = high edge variance (filter the outer taps), 0 = none.
std::int8_t hevMask(std::uint8_t thresh, std::uint8_t p1, std::uint8_t p0, std::uint8_t q0, std::uint8_t q1);

// signed_char_clamp (deblocking_common.c:30-32): the int8 saturating clamp
// filter4 uses. File-local static INLINE in the reference; exported here so
// the clamp CONSTANTS are directly testable (named deviation).
std::int8_t signedCharClamp(std::int32_t t);

// filter4 (deblocking_common.c:225-251): the 4-tap core filter; writes the
// four edge samples in place through the pointers.
void filter4(std::int8_t mask, std::uint8_t thresh, std::uint8_t* op1, std::uint8_t* op0, std::uint8_t* oq0,
             std::uint8_t* oq1);

// filter6 (deblocking_common.c:285-299): the 5-tap flat filter with the
// filter4 fallback when either gate is off.
void filter6(std::int8_t mask, std::uint8_t thresh, std::int8_t flat, std::uint8_t* op2, std::uint8_t* op1,
             std::uint8_t* op0, std::uint8_t* oq0, std::uint8_t* oq1, std::uint8_t* oq2);

// filter8 (deblocking_common.c:301-317): the 7-tap flat filter with the
// filter4 fallback when either gate is off.
void filter8(std::int8_t mask, std::uint8_t thresh, std::int8_t flat, std::uint8_t* op3, std::uint8_t* op2,
             std::uint8_t* op1, std::uint8_t* op0, std::uint8_t* oq0, std::uint8_t* oq1, std::uint8_t* oq2,
             std::uint8_t* oq3);

// lpf_horizontal_4_c (deblocking_common.c:253-267): scalar blimit/limit/
// thresh (the reference reads element [0] of SIMD_WIDTH=16 arrays); the
// pointer steps horizontally, taps are strided by pitch.
void lpfHorizontal4(std::uint8_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh);

// lpf_vertical_4_c (deblocking_common.c:269-283): contiguous taps, the
// pointer advances by pitch per row.
void lpfVertical4(std::uint8_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh);

// lpf_horizontal_6_c (deblocking_common.c:319-335): mask = filter_mask3_
// chroma, flat = flat_mask3_chroma(1, ...), filter6 dispatch.
void lpfHorizontal6(std::uint8_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh);

// lpf_vertical_6_c (deblocking_common.c:337-353): contiguous taps, the
// pointer advances by pitch per row.
void lpfVertical6(std::uint8_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh);

// lpf_horizontal_8_c (deblocking_common.c:355-371): mask = filter_mask,
// flat = flat_mask4(1, ...), filter8 dispatch.
void lpfHorizontal8(std::uint8_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh);

// lpf_vertical_8_c (deblocking_common.c:373-386): contiguous taps, the
// pointer advances by pitch per row.
void lpfVertical8(std::uint8_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh);

// svt_aom_update_sharpness (deblocking_common.c:568-589): fills lfthr[lvl]
// lim/mblim for every level. hev_thr is NOT touched here (it is set by
// svt_av1_loop_filter_frame_init, a light-wall function outside DF1).
void updateSharpness(LoopFilterInfoN* lfi, int sharpnessLvl);

// DF2. filter14 (deblocking_common.c:786-809): the 14-tap family's core,
// the 13-tap flat filter [1,1,1,1,1,2,2,2,1,1,1,1,1] writing op5..oq5
// (op6/oq6 untouched), ROUND_POWER_OF_TWO(...,4); the else chain STACKS
// through filter8 to filter4 (DF1 composition, not re-implementation).
void filter14(std::int8_t mask, std::uint8_t thresh, std::int8_t flat, std::int8_t flat2, std::uint8_t* op6,
              std::uint8_t* op5, std::uint8_t* op4, std::uint8_t* op3, std::uint8_t* op2, std::uint8_t* op1,
              std::uint8_t* op0, std::uint8_t* oq0, std::uint8_t* oq1, std::uint8_t* oq2, std::uint8_t* oq3,
              std::uint8_t* oq4, std::uint8_t* oq5, std::uint8_t* oq6);

// lpf_horizontal_14_c (deblocking_common.c:849-852): the 14-tap horizontal
// entry. mb_lpf_horizontal_edge_w (:811-847, static in the reference) loops
// step*count columns with step=4; the entry passes count=1, so exactly
// FOUR columns are filtered per call (count semantics pinned from :849-852).
// Taps s[-7*pitch .. +6*pitch]; flat2 = flat_mask4(1, p6, p5, p4, p0, q0,
// q4, q5, q6) - the OUTER flatness measured against p0/q0.
void lpfHorizontal14(std::uint8_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh);

// lpf_vertical_14_c (deblocking_common.c:854-889): the 14-tap vertical
// entry via mb_lpf_vertical_edge_w (static), taps contiguous s[-7..+6],
// the pointer advances by pitch per row; the entry passes count=4
// (four rows per call).
void lpfVertical14(std::uint8_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh);

} // namespace l10_filters