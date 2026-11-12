#include <cstdlib>

#include "deblock_hbd.h"

namespace l10_filters {

// A8W1d G1 (the hbd 4-tap rung): signed_char_clamp_high
// (deblocking_common.c:34-44), highbd_filter_mask2 (:388-:398),
// highbd_hev_mask (:429-:437), highbd_filter4 (:439-:471) and the
// svt_aom_highbd_lpf_horizontal_4_c (:473-:489) / _vertical_4_c
// (:491-:505) entries. 1:1 verbatim semantics; scalar limits per the
// landed DF1 element-[0] convention.

// 1:1 port of signed_char_clamp_high (deblocking_common.c:34-44).
std::int16_t signedCharClampHigh(std::int32_t t, std::int32_t bd) {
    switch (bd) {
    case 10:
        return static_cast<std::int16_t>(t < -128 * 4 ? -128 * 4 : (t > 128 * 4 - 1 ? 128 * 4 - 1 : t));
    case 12:
        return static_cast<std::int16_t>(t < -128 * 16 ? -128 * 16 : (t > 128 * 16 - 1 ? 128 * 16 - 1 : t));
    case 8:
    default:
        return static_cast<std::int16_t>(t < -128 ? -128 : (t > 128 - 1 ? 128 - 1 : t));
    }
}

// 1:1 port of highbd_filter_mask2 (deblocking_common.c:388-398).
std::int8_t highbdFilterMask2(std::uint8_t limit, std::uint8_t blimit, std::uint16_t p1, std::uint16_t p0,
                              std::uint16_t q0, std::uint16_t q1, std::int32_t bd) {
    const std::int16_t limit16 = static_cast<std::int16_t>(static_cast<std::uint16_t>(limit) << (bd - 8));
    const std::int16_t blimit16 = static_cast<std::int16_t>(static_cast<std::uint16_t>(blimit) << (bd - 8));
    std::int8_t mask = 0;
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p1) - static_cast<int>(p0)) > limit16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q1) - static_cast<int>(q0)) > limit16) * -1));
    mask = static_cast<std::int8_t>(
        mask | ((std::abs(static_cast<int>(p0) - static_cast<int>(q0)) * 2 +
                 std::abs(static_cast<int>(p1) - static_cast<int>(q1)) / 2 > blimit16) *
            -1));
    return static_cast<std::int8_t>(~mask);
}

// 1:1 port of highbd_hev_mask (deblocking_common.c:429-437): NOT
// inverted; -1 = high edge variance.
std::int16_t highbdHevMask(std::uint8_t thresh, std::uint16_t p1, std::uint16_t p0, std::uint16_t q0, std::uint16_t q1,
                           std::int32_t bd) {
    const std::int16_t thresh16 = static_cast<std::int16_t>(static_cast<std::uint16_t>(thresh) << (bd - 8));
    std::int16_t hev = 0;
    hev = static_cast<std::int16_t>(hev | ((std::abs(static_cast<int>(p1) - static_cast<int>(p0)) > thresh16) * -1));
    hev = static_cast<std::int16_t>(hev | ((std::abs(static_cast<int>(q1) - static_cast<int>(q0)) > thresh16) * -1));
    return hev;
}

// 1:1 port of highbd_filter4 (deblocking_common.c:439-471). The ^0x80
// bias becomes -(0x80 << (bd - 8)); ROUND_POWER_OF_TWO(filter1, 1) is
// the landed ((filter1 + 1) >> 1) inline form.
void highbdFilter4(std::int8_t mask, std::uint8_t thresh, std::uint16_t* op1, std::uint16_t* op0, std::uint16_t* oq0,
                   std::uint16_t* oq1, std::int32_t bd) {
    std::int16_t filter1, filter2;
    const std::int32_t shift = bd - 8;
    const std::int16_t ps1 = static_cast<std::int16_t>(*op1 - (0x80 << shift));
    const std::int16_t ps0 = static_cast<std::int16_t>(*op0 - (0x80 << shift));
    const std::int16_t qs0 = static_cast<std::int16_t>(*oq0 - (0x80 << shift));
    const std::int16_t qs1 = static_cast<std::int16_t>(*oq1 - (0x80 << shift));
    const std::int16_t hev = highbdHevMask(thresh, *op1, *op0, *oq0, *oq1, bd);

    // Add outer taps if we have high edge variance.
    std::int16_t filter = static_cast<std::int16_t>(signedCharClampHigh(ps1 - qs1, bd) & hev);

    // Inner taps.
    filter = static_cast<std::int16_t>(signedCharClampHigh(filter + 3 * (qs0 - ps0), bd) & mask);

    // Save bottom 3 bits so that we round one side +4 and the other +3
    // if it equals 4 we'll set to adjust by -1 to account for the fact
    // we'd round 3 the other way.
    filter1 = static_cast<std::int16_t>(signedCharClampHigh(filter + 4, bd) >> 3);
    filter2 = static_cast<std::int16_t>(signedCharClampHigh(filter + 3, bd) >> 3);

    *oq0 = static_cast<std::uint16_t>(signedCharClampHigh(qs0 - filter1, bd) + (0x80 << shift));
    *op0 = static_cast<std::uint16_t>(signedCharClampHigh(ps0 + filter2, bd) + (0x80 << shift));

    // Outer tap adjustments.
    filter = static_cast<std::int16_t>(((filter1 + 1) >> 1) & ~hev);

    *oq1 = static_cast<std::uint16_t>(signedCharClampHigh(qs1 - filter, bd) + (0x80 << shift));
    *op1 = static_cast<std::uint16_t>(signedCharClampHigh(ps1 + filter, bd) + (0x80 << shift));
}

// 1:1 port of svt_aom_highbd_lpf_horizontal_4_c (deblocking_common.c
// :473-489): four columns, strided taps.
void highbdLpfHorizontal4(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                          std::int32_t bd) {
    const int count = 4;
    for (int i = 0; i < count; ++i) {
        const std::uint16_t p1 = s[-2 * pitch];
        const std::uint16_t p0 = s[-pitch];
        const std::uint16_t q0 = s[0 * pitch];
        const std::uint16_t q1 = s[1 * pitch];
        const std::int8_t mask = highbdFilterMask2(limit, blimit, p1, p0, q0, q1, bd);
        highbdFilter4(mask, thresh, s - 2 * pitch, s - 1 * pitch, s, s + 1 * pitch, bd);
        ++s;
    }
}

// 1:1 port of svt_aom_highbd_lpf_vertical_4_c (deblocking_common.c
// :491-505): contiguous taps, four rows.
void highbdLpfVertical4(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                        std::int32_t bd) {
    const int count = 4;
    for (int i = 0; i < count; ++i) {
        const std::uint16_t p1 = s[-2], p0 = s[-1];
        const std::uint16_t q0 = s[0], q1 = s[1];
        const std::int8_t mask = highbdFilterMask2(limit, blimit, p1, p0, q0, q1, bd);
        highbdFilter4(mask, thresh, s - 2, s - 1, s, s + 1, bd);
        s += pitch;
    }
}

// G2 (A8W1d): the hbd 6-tap rung - highbd_flat_mask3_chroma
// (deblocking_common.c:194-:203), highbd_filter_mask3_chroma
// (:679-:690), highbd_filter6 (:692-:706) and the
// svt_aom_highbd_lpf_vertical_6_c (:708-:721) / _horizontal_6_c
// (:723-:739) entries.

// 1:1 port of highbd_flat_mask3_chroma (deblocking_common.c:194-203).
std::int8_t highbdFlatMask3Chroma(std::uint8_t thresh, std::uint16_t p2, std::uint16_t p1, std::uint16_t p0,
                                  std::uint16_t q0, std::uint16_t q1, std::uint16_t q2, std::int32_t bd) {
    const std::int16_t thresh16 = static_cast<std::int16_t>(static_cast<std::uint16_t>(thresh) << (bd - 8));
    std::int8_t mask = 0;
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p1) - static_cast<int>(p0)) > thresh16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q1) - static_cast<int>(q0)) > thresh16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p2) - static_cast<int>(p0)) > thresh16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q2) - static_cast<int>(q0)) > thresh16) * -1));
    return static_cast<std::int8_t>(~mask);
}

// 1:1 port of highbd_filter_mask3_chroma (deblocking_common.c:679-690).
std::int8_t highbdFilterMask3Chroma(std::uint8_t limit, std::uint8_t blimit, std::uint16_t p2, std::uint16_t p1,
                                    std::uint16_t p0, std::uint16_t q0, std::uint16_t q1, std::uint16_t q2,
                                    std::int32_t bd) {
    const std::int16_t limit16 = static_cast<std::int16_t>(static_cast<std::uint16_t>(limit) << (bd - 8));
    const std::int16_t blimit16 = static_cast<std::int16_t>(static_cast<std::uint16_t>(blimit) << (bd - 8));
    std::int8_t mask = 0;
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p2) - static_cast<int>(p1)) > limit16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p1) - static_cast<int>(p0)) > limit16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q1) - static_cast<int>(q0)) > limit16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q2) - static_cast<int>(q1)) > limit16) * -1));
    mask = static_cast<std::int8_t>(
        mask | ((std::abs(static_cast<int>(p0) - static_cast<int>(q0)) * 2 +
                 std::abs(static_cast<int>(p1) - static_cast<int>(q1)) / 2 > blimit16) *
            -1));
    return static_cast<std::int8_t>(~mask);
}

// 1:1 port of highbd_filter6 (deblocking_common.c:692-706): the 5-tap
// flat filter with the highbd_filter4 fallback.
void highbdFilter6(std::int8_t mask, std::uint8_t thresh, std::int8_t flat, std::uint16_t* op2, std::uint16_t* op1,
                   std::uint16_t* op0, std::uint16_t* oq0, std::uint16_t* oq1, std::uint16_t* oq2, std::int32_t bd) {
    if (flat && mask) {
        const std::uint16_t p2 = *op2, p1 = *op1, p0 = *op0;
        const std::uint16_t q0 = *oq0, q1 = *oq1, q2 = *oq2;

        // 5-tap filter [1, 2, 2, 2, 1]
        *op1 = static_cast<std::uint16_t>((p2 * 3 + p1 * 2 + p0 * 2 + q0 + 4) >> 3);
        *op0 = static_cast<std::uint16_t>((p2 + p1 * 2 + p0 * 2 + q0 * 2 + q1 + 4) >> 3);
        *oq0 = static_cast<std::uint16_t>((p1 + p0 * 2 + q0 * 2 + q1 * 2 + q2 + 4) >> 3);
        *oq1 = static_cast<std::uint16_t>((p0 + q0 * 2 + q1 * 2 + q2 * 3 + 4) >> 3);
    } else {
        highbdFilter4(mask, thresh, op1, op0, oq0, oq1, bd);
    }
}

// 1:1 port of svt_aom_highbd_lpf_vertical_6_c (deblocking_common.c
// :708-721): contiguous taps, four rows.
void highbdLpfVertical6(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                        std::int32_t bd) {
    const int count = 4;
    for (int i = 0; i < count; ++i) {
        const std::uint16_t p2 = s[-3], p1 = s[-2], p0 = s[-1];
        const std::uint16_t q0 = s[0], q1 = s[1], q2 = s[2];
        const std::int8_t mask = highbdFilterMask3Chroma(limit, blimit, p2, p1, p0, q0, q1, q2, bd);
        const std::int8_t flat = highbdFlatMask3Chroma(1, p2, p1, p0, q0, q1, q2, bd);
        highbdFilter6(mask, thresh, flat, s - 3, s - 2, s - 1, s, s + 1, s + 2, bd);
        s += pitch;
    }
}

// 1:1 port of svt_aom_highbd_lpf_horizontal_6_c (deblocking_common.c
// :723-739): strided taps, four columns.
void highbdLpfHorizontal6(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                          std::int32_t bd) {
    const int count = 4;
    for (int i = 0; i < count; ++i) {
        const std::uint16_t p2 = s[-3 * pitch], p1 = s[-2 * pitch], p0 = s[-pitch];
        const std::uint16_t q0 = s[0 * pitch], q1 = s[1 * pitch], q2 = s[2 * pitch];
        const std::int8_t mask = highbdFilterMask3Chroma(limit, blimit, p2, p1, p0, q0, q1, q2, bd);
        const std::int8_t flat = highbdFlatMask3Chroma(1, p2, p1, p0, q0, q1, q2, bd);
        highbdFilter6(mask, thresh, flat, s - 3 * pitch, s - 2 * pitch, s - 1 * pitch, s, s + 1 * pitch,
            s + 2 * pitch, bd);
        ++s;
    }
}

// G3 (A8W1d): the hbd 8-tap rung - highbd_filter_mask
// (deblocking_common.c:400-:414), highbd_flat_mask4 (:416-:427),
// highbd_filter8 (:507-:524) and the svt_aom_highbd_lpf_horizontal_8_c
// (:526-:543) / _vertical_8_c (:545-:558) entries.

// 1:1 port of highbd_filter_mask (deblocking_common.c:400-414).
std::int8_t highbdFilterMask(std::uint8_t limit, std::uint8_t blimit, std::uint16_t p3, std::uint16_t p2,
                             std::uint16_t p1, std::uint16_t p0, std::uint16_t q0, std::uint16_t q1,
                             std::uint16_t q2, std::uint16_t q3, std::int32_t bd) {
    const std::int16_t limit16 = static_cast<std::int16_t>(static_cast<std::uint16_t>(limit) << (bd - 8));
    const std::int16_t blimit16 = static_cast<std::int16_t>(static_cast<std::uint16_t>(blimit) << (bd - 8));
    std::int8_t mask = 0;
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p3) - static_cast<int>(p2)) > limit16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p2) - static_cast<int>(p1)) > limit16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p1) - static_cast<int>(p0)) > limit16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q1) - static_cast<int>(q0)) > limit16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q2) - static_cast<int>(q1)) > limit16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q3) - static_cast<int>(q2)) > limit16) * -1));
    mask = static_cast<std::int8_t>(
        mask | ((std::abs(static_cast<int>(p0) - static_cast<int>(q0)) * 2 +
                 std::abs(static_cast<int>(p1) - static_cast<int>(q1)) / 2 > blimit16) *
            -1));
    return static_cast<std::int8_t>(~mask);
}

// 1:1 port of highbd_flat_mask4 (deblocking_common.c:416-427).
std::int8_t highbdFlatMask4(std::uint8_t thresh, std::uint16_t p3, std::uint16_t p2, std::uint16_t p1,
                            std::uint16_t p0, std::uint16_t q0, std::uint16_t q1, std::uint16_t q2,
                            std::uint16_t q3, std::int32_t bd) {
    const std::int16_t thresh16 = static_cast<std::int16_t>(static_cast<std::uint16_t>(thresh) << (bd - 8));
    std::int8_t mask = 0;
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p1) - static_cast<int>(p0)) > thresh16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q1) - static_cast<int>(q0)) > thresh16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p2) - static_cast<int>(p0)) > thresh16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q2) - static_cast<int>(q0)) > thresh16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p3) - static_cast<int>(p0)) > thresh16) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q3) - static_cast<int>(q0)) > thresh16) * -1));
    return static_cast<std::int8_t>(~mask);
}

// 1:1 port of highbd_filter8 (deblocking_common.c:507-524): the 7-tap
// flat filter with the highbd_filter4 fallback.
void highbdFilter8(std::int8_t mask, std::uint8_t thresh, std::int8_t flat, std::uint16_t* op3, std::uint16_t* op2,
                   std::uint16_t* op1, std::uint16_t* op0, std::uint16_t* oq0, std::uint16_t* oq1,
                   std::uint16_t* oq2, std::uint16_t* oq3, std::int32_t bd) {
    if (flat && mask) {
        const std::uint16_t p3 = *op3, p2 = *op2, p1 = *op1, p0 = *op0;
        const std::uint16_t q0 = *oq0, q1 = *oq1, q2 = *oq2, q3 = *oq3;

        // 7-tap filter [1, 1, 1, 2, 1, 1, 1]
        *op2 = static_cast<std::uint16_t>((p3 + p3 + p3 + 2 * p2 + p1 + p0 + q0 + 4) >> 3);
        *op1 = static_cast<std::uint16_t>((p3 + p3 + p2 + 2 * p1 + p0 + q0 + q1 + 4) >> 3);
        *op0 = static_cast<std::uint16_t>((p3 + p2 + p1 + 2 * p0 + q0 + q1 + q2 + 4) >> 3);
        *oq0 = static_cast<std::uint16_t>((p2 + p1 + p0 + 2 * q0 + q1 + q2 + q3 + 4) >> 3);
        *oq1 = static_cast<std::uint16_t>((p1 + p0 + q0 + 2 * q1 + q2 + q3 + q3 + 4) >> 3);
        *oq2 = static_cast<std::uint16_t>((p0 + q0 + q1 + 2 * q2 + q3 + q3 + q3 + 4) >> 3);
    } else {
        highbdFilter4(mask, thresh, op1, op0, oq0, oq1, bd);
    }
}

// 1:1 port of svt_aom_highbd_lpf_horizontal_8_c (deblocking_common.c
// :526-543): strided taps, four columns.
void highbdLpfHorizontal8(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                          std::int32_t bd) {
    const int count = 4;
    for (int i = 0; i < count; ++i) {
        const std::uint16_t p3 = s[-4 * pitch], p2 = s[-3 * pitch], p1 = s[-2 * pitch], p0 = s[-pitch];
        const std::uint16_t q0 = s[0 * pitch], q1 = s[1 * pitch], q2 = s[2 * pitch], q3 = s[3 * pitch];
        const std::int8_t mask = highbdFilterMask(limit, blimit, p3, p2, p1, p0, q0, q1, q2, q3, bd);
        const std::int8_t flat = highbdFlatMask4(1, p3, p2, p1, p0, q0, q1, q2, q3, bd);
        highbdFilter8(mask, thresh, flat, s - 4 * pitch, s - 3 * pitch, s - 2 * pitch, s - 1 * pitch, s,
            s + 1 * pitch, s + 2 * pitch, s + 3 * pitch, bd);
        ++s;
    }
}

// 1:1 port of svt_aom_highbd_lpf_vertical_8_c (deblocking_common.c
// :545-558): contiguous taps, four rows.
void highbdLpfVertical8(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                        std::int32_t bd) {
    const int count = 4;
    for (int i = 0; i < count; ++i) {
        const std::uint16_t p3 = s[-4], p2 = s[-3], p1 = s[-2], p0 = s[-1];
        const std::uint16_t q0 = s[0], q1 = s[1], q2 = s[2], q3 = s[3];
        const std::int8_t mask = highbdFilterMask(limit, blimit, p3, p2, p1, p0, q0, q1, q2, q3, bd);
        const std::int8_t flat = highbdFlatMask4(1, p3, p2, p1, p0, q0, q1, q2, q3, bd);
        highbdFilter8(mask, thresh, flat, s - 4, s - 3, s - 2, s - 1, s, s + 1, s + 2, s + 3, bd);
        s += pitch;
    }
}

// G4 (A8W1d): the hbd 14-tap rung - highbd_filter14
// (deblocking_common.c:591-:627), highbd_mb_lpf_horizontal_edge_w
// (:629-:672), svt_aom_highbd_lpf_horizontal_14_c (:674-:677),
// highbd_mb_lpf_vertical_edge_w (:741-:779) and
// svt_aom_highbd_lpf_vertical_14_c (:781-:784).

// 1:1 port of highbd_filter14 (deblocking_common.c:591-627): the 13-tap
// flat filter with the highbd_filter8 fallback.
void highbdFilter14(std::int8_t mask, std::uint8_t thresh, std::int8_t flat, std::int8_t flat2, std::uint16_t* op6,
                    std::uint16_t* op5, std::uint16_t* op4, std::uint16_t* op3, std::uint16_t* op2,
                    std::uint16_t* op1, std::uint16_t* op0, std::uint16_t* oq0, std::uint16_t* oq1,
                    std::uint16_t* oq2, std::uint16_t* oq3, std::uint16_t* oq4, std::uint16_t* oq5,
                    std::uint16_t* oq6, std::int32_t bd) {
    if (flat2 && flat && mask) {
        const std::uint16_t p6 = *op6, p5 = *op5, p4 = *op4, p3 = *op3;
        const std::uint16_t p2 = *op2, p1 = *op1, p0 = *op0;
        const std::uint16_t q0 = *oq0, q1 = *oq1, q2 = *oq2, q3 = *oq3;
        const std::uint16_t q4 = *oq4, q5 = *oq5, q6 = *oq6;

        // 13-tap filter [1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1]
        *op5 = static_cast<std::uint16_t>((p6 * 7 + p5 * 2 + p4 * 2 + p3 + p2 + p1 + p0 + q0 + 8) >> 4);
        *op4 = static_cast<std::uint16_t>((p6 * 5 + p5 * 2 + p4 * 2 + p3 * 2 + p2 + p1 + p0 + q0 + q1 + 8) >> 4);
        *op3 =
            static_cast<std::uint16_t>((p6 * 4 + p5 + p4 * 2 + p3 * 2 + p2 * 2 + p1 + p0 + q0 + q1 + q2 + 8) >> 4);
        *op2 = static_cast<std::uint16_t>(
            (p6 * 3 + p5 + p4 + p3 * 2 + p2 * 2 + p1 * 2 + p0 + q0 + q1 + q2 + q3 + 8) >> 4);
        *op1 = static_cast<std::uint16_t>(
            (p6 * 2 + p5 + p4 + p3 + p2 * 2 + p1 * 2 + p0 * 2 + q0 + q1 + q2 + q3 + q4 + 8) >> 4);
        *op0 = static_cast<std::uint16_t>(
            (p6 + p5 + p4 + p3 + p2 + p1 * 2 + p0 * 2 + q0 * 2 + q1 + q2 + q3 + q4 + q5 + 8) >> 4);
        *oq0 = static_cast<std::uint16_t>(
            (p5 + p4 + p3 + p2 + p1 + p0 * 2 + q0 * 2 + q1 * 2 + q2 + q3 + q4 + q5 + q6 + 8) >> 4);
        *oq1 = static_cast<std::uint16_t>(
            (p4 + p3 + p2 + p1 + p0 + q0 * 2 + q1 * 2 + q2 * 2 + q3 + q4 + q5 + q6 * 2 + 8) >> 4);
        *oq2 = static_cast<std::uint16_t>(
            (p3 + p2 + p1 + p0 + q0 + q1 * 2 + q2 * 2 + q3 * 2 + q4 + q5 + q6 * 3 + 8) >> 4);
        *oq3 = static_cast<std::uint16_t>((p2 + p1 + p0 + q0 + q1 + q2 * 2 + q3 * 2 + q4 * 2 + q5 + q6 * 4 + 8) >> 4);
        *oq4 = static_cast<std::uint16_t>((p1 + p0 + q0 + q1 + q2 + q3 * 2 + q4 * 2 + q5 * 2 + q6 * 5 + 8) >> 4);
        *oq5 = static_cast<std::uint16_t>((p0 + q0 + q1 + q2 + q3 + q4 * 2 + q5 * 2 + q6 * 7 + 8) >> 4);
    } else {
        highbdFilter8(mask, thresh, flat, op3, op2, op1, op0, oq0, oq1, oq2, oq3, bd);
    }
}

// 1:1 port of highbd_mb_lpf_horizontal_edge_w (deblocking_common.c
// :629-672): step=4, step*count columns; exported (named deviation:
// static).
void highbdMbLpfHorizontalEdgeW(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit,
                                std::uint8_t thresh, int count, std::int32_t bd) {
    const int step = 4;
    for (int i = 0; i < step * count; ++i) {
        const std::uint16_t p3 = s[-4 * pitch], p2 = s[-3 * pitch], p1 = s[-2 * pitch], p0 = s[-pitch];
        const std::uint16_t q0 = s[0 * pitch], q1 = s[1 * pitch], q2 = s[2 * pitch], q3 = s[3 * pitch];
        const std::int8_t mask = highbdFilterMask(limit, blimit, p3, p2, p1, p0, q0, q1, q2, q3, bd);
        const std::int8_t flat = highbdFlatMask4(1, p3, p2, p1, p0, q0, q1, q2, q3, bd);
        const std::int8_t flat2 = highbdFlatMask4(1, s[-7 * pitch], s[-6 * pitch], s[-5 * pitch], p0, q0,
            s[4 * pitch], s[5 * pitch], s[6 * pitch], bd);
        highbdFilter14(mask, thresh, flat, flat2, s - 7 * pitch, s - 6 * pitch, s - 5 * pitch, s - 4 * pitch,
            s - 3 * pitch, s - 2 * pitch, s - 1 * pitch, s, s + 1 * pitch, s + 2 * pitch, s + 3 * pitch,
            s + 4 * pitch, s + 5 * pitch, s + 6 * pitch, bd);
        ++s;
    }
}

// 1:1 port of svt_aom_highbd_lpf_horizontal_14_c (deblocking_common.c
// :674-677): count=1.
void highbdLpfHorizontal14(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                           std::int32_t bd) {
    highbdMbLpfHorizontalEdgeW(s, pitch, blimit, limit, thresh, 1, bd);
}

// 1:1 port of highbd_mb_lpf_vertical_edge_w (deblocking_common.c
// :741-779): count rows; exported (named deviation: static).
void highbdMbLpfVerticalEdgeW(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit,
                              std::uint8_t thresh, int count, std::int32_t bd) {
    for (int i = 0; i < count; ++i) {
        const std::uint16_t p3 = s[-4], p2 = s[-3], p1 = s[-2], p0 = s[-1];
        const std::uint16_t q0 = s[0], q1 = s[1], q2 = s[2], q3 = s[3];
        const std::int8_t mask = highbdFilterMask(limit, blimit, p3, p2, p1, p0, q0, q1, q2, q3, bd);
        const std::int8_t flat = highbdFlatMask4(1, p3, p2, p1, p0, q0, q1, q2, q3, bd);
        const std::int8_t flat2 = highbdFlatMask4(1, s[-7], s[-6], s[-5], p0, q0, s[4], s[5], s[6], bd);
        highbdFilter14(mask, thresh, flat, flat2, s - 7, s - 6, s - 5, s - 4, s - 3, s - 2, s - 1, s, s + 1, s + 2,
            s + 3, s + 4, s + 5, s + 6, bd);
        s += pitch;
    }
}

// 1:1 port of svt_aom_highbd_lpf_vertical_14_c (deblocking_common.c
// :781-784): count=4.
void highbdLpfVertical14(std::uint16_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh,
                         std::int32_t bd) {
    highbdMbLpfVerticalEdgeW(s, pitch, blimit, limit, thresh, 4, bd);
}

} // namespace l10_filters
