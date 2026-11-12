#pragma once

#include <cstdint>

#include "filters.h"

// A8W1d (the hbd rung): the HIGHBD deblocking family of
// third_party/SVT-AV1/Source/Lib/Codec/deblocking_common.c - the bd-
// parameterized twins of the landed DF1/DF2 8-bit kernels in filters.h.
// Each kernel shifts the 8-bit limits by (bd - 8) and runs the same
// integer core; at bd=8 the outputs are byte-identical to the 8-bit
// path (the derivation fixtures in test_deblock_hbd.cpp pin that).
// Groups (one kernel group per turn, RED already staged in
// test_deblock_hbd.cpp):
//   G1: signedCharClampHigh, highbdFilterMask2, highbdHevMask,
//       highbdFilter4, highbdLpfHorizontal4, highbdLpfVertical4
//   G2: highbdFlatMask3Chroma, highbdFilterMask3Chroma, highbdFilter6,
//       highbdLpfHorizontal6, highbdLpfVertical6
//   G3: highbdFlatMask4, highbdFilterMask, highbdFilter8,
//       highbdLpfHorizontal8, highbdLpfVertical8
//   G4: highbdFilter14, highbdMbLpfHorizontalEdgeW,
//       highbdMbLpfVerticalEdgeW, highbdLpfHorizontal14,
//       highbdLpfVertical14
// Reference spans (measured non-blank): :34-:44, :194-:203, :388-:398,
// :400-:414, :416-:427, :429-:437, :439-:471, :473-:489, :491-:505,
// :507-:524, :526-:543, :545-:558, :591-:627, :629-:672, :674-:677,
// :679-:690, :692-:706, :708-:721, :723-:739, :741-:779, :781-:784
// = 347 nb. The reference takes const uint8_t* limit lanes; the port
// takes scalars (the landed DF1 element-[0] convention).

namespace l10_filters {

// signed_char_clamp_high (deblocking_common.c:34-44). The switch has
// cases 10 / 12 / default ONLY; bd=14 clamps to the 8-bit arm (pinned
// as-is in the test; unreachable in practice).
std::int16_t signedCharClampHigh(std::int32_t t, std::int32_t bd);

// highbd_filter_mask2 (deblocking_common.c:388-398): the 4-tap mask.
std::int8_t highbdFilterMask2(std::uint8_t limit, std::uint8_t blimit, std::uint16_t p1, std::uint16_t p0,
                              std::uint16_t q0, std::uint16_t q1, std::int32_t bd);

// highbd_filter_mask (deblocking_common.c:400-414): the 9-tap mask.
std::int8_t highbdFilterMask(std::uint8_t limit, std::uint8_t blimit, std::uint16_t p3, std::uint16_t p2,
                             std::uint16_t p1, std::uint16_t p0, std::uint16_t q0, std::uint16_t q1,
                             std::uint16_t q2, std::uint16_t q3, std::int32_t bd);

// highbd_flat_mask3_chroma (deblocking_common.c:194-203).
std::int8_t highbdFlatMask3Chroma(std::uint8_t thresh, std::uint16_t p2, std::uint16_t p1, std::uint16_t p0,
                                  std::uint16_t q0, std::uint16_t q1, std::uint16_t q2, std::int32_t bd);

// highbd_filter_mask3_chroma (deblocking_common.c:679-690).
std::int8_t highbdFilterMask3Chroma(std::uint8_t limit, std::uint8_t blimit, std::uint16_t p2, std::uint16_t p1,
                                    std::uint16_t p0, std::uint16_t q0, std::uint16_t q1, std::uint16_t q2,
                                    std::int32_t bd);

// highbd_flat_mask4 (deblocking_common.c:416-427).
std::int8_t highbdFlatMask4(std::uint8_t thresh, std::uint16_t p3, std::uint16_t p2, std::uint16_t p1,
                            std::uint16_t p0, std::uint16_t q0, std::uint16_t q1, std::uint16_t q2,
                            std::uint16_t q3, std::int32_t bd);

// highbd_hev_mask (deblocking_common.c:429-437): NOT inverted; int16.
std::int16_t highbdHevMask(std::uint8_t thresh, std::uint16_t p1, std::uint16_t p0, std::uint16_t q0,
                           std::uint16_t q1, std::int32_t bd);

// highbd_filter4 (deblocking_common.c:439-471): the 4-tap core; writes
// the four edge samples in place through the pointers.
void highbdFilter4(std::int8_t mask, std::uint8_t thresh, std::uint16_t* op1, std::uint16_t* op0, std::uint16_t* oq0,
                   std::uint16_t* oq1, std::int32_t bd);

// highbd_filter6 (deblocking_common.c:692-706): the 5-tap flat filter
// with the highbd_filter4 fallback.
void highbdFilter6(std::int8_t mask, std::uint8_t thresh, std::int8_t flat, std::uint16_t* op2, std::uint16_t* op1,
                   std::uint16_t* op0, std::uint16_t* oq0, std::uint16_t* oq1, std::uint16_t* oq2, std::int32_t bd);

// highbd_filter8 (deblocking_common.c:507-524): the 7-tap flat filter
// with the highbd_filter4 fallback.
void highbdFilter8(std::int8_t mask, std::uint8_t thresh, std::int8_t flat, std::uint16_t* op3, std::uint16_t* op2,
                   std::uint16_t* op1, std::uint16_t* op0, std::uint16_t* oq0, std::uint16_t* oq1,
                   std::uint16_t* oq2, std::uint16_t* oq3, std::int32_t bd);

// highbd_filter14 (deblocking_common.c:591-627): the 13-tap flat filter
// with the highbd_filter8 fallback chain.
void highbdFilter14(std::int8_t mask, std::uint8_t thresh, std::int8_t flat, std::int8_t flat2, std::uint16_t* op6,
                    std::uint16_t* op5, std::uint16_t* op4, std::uint16_t* op3, std::uint16_t* op2,
                    std::uint16_t* op1, std::uint16_t* op0, std::uint16_t* oq0, std::uint16_t* oq1,
                    std::uint16_t* oq2, std::uint16_t* oq3, std::uint16_t* oq4, std::uint16_t* oq5,
                    std::uint16_t* oq6, std::int32_t bd);

// highbd_mb_lpf_horizontal_edge_w (deblocking_common.c:629-672): step=4,
// loops step*count columns; exported (named deviation: static).
void highbdMbLpfHorizontalEdgeW(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit,
                                std::uint8_t thresh, int count, std::int32_t bd);

// highbd_mb_lpf_vertical_edge_w (deblocking_common.c:741-779): loops
// count rows; exported (named deviation: static).
void highbdMbLpfVerticalEdgeW(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit,
                              std::uint8_t thresh, int count, std::int32_t bd);

// highbd_lpf_horizontal_4_c (deblocking_common.c:473-489): four columns.
void highbdLpfHorizontal4(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                          std::int32_t bd);

// highbd_lpf_vertical_4_c (deblocking_common.c:491-505): four rows.
void highbdLpfVertical4(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                        std::int32_t bd);

// highbd_lpf_vertical_6_c (deblocking_common.c:708-721) /
// highbd_lpf_horizontal_6_c (:723-739): mask = highbd_filter_mask3_
// chroma, flat = highbd_flat_mask3_chroma(1, ...), highbd_filter6
// dispatch; four rows / four columns.
void highbdLpfVertical6(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                        std::int32_t bd);
void highbdLpfHorizontal6(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                          std::int32_t bd);

// highbd_lpf_horizontal_8_c (deblocking_common.c:526-543) /
// highbd_lpf_vertical_8_c (:545-558): mask = highbd_filter_mask, flat =
// highbd_flat_mask4(1, ...), highbd_filter8 dispatch.
void highbdLpfHorizontal8(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                          std::int32_t bd);
void highbdLpfVertical8(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                        std::int32_t bd);

// highbd_lpf_horizontal_14_c (deblocking_common.c:674-677): count=1.
void highbdLpfHorizontal14(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                           std::int32_t bd);

// highbd_lpf_vertical_14_c (deblocking_common.c:781-784): count=4.
void highbdLpfVertical14(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                         std::int32_t bd);

} // namespace l10_filters
