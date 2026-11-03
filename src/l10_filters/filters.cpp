#include <cstdlib>
#include <cstring>

#include "filters.h"

namespace l10_filters {

// 1:1 port of signed_char_clamp (deblocking_common.c:30-32).
std::int8_t signedCharClamp(std::int32_t t) {
    return static_cast<std::int8_t>(t < -128 ? -128 : (t > 127 ? 127 : t));
}

// 1:1 port of filter_mask2 (deblocking_common.c:152-158).
std::int8_t filterMask2(std::uint8_t limit, std::uint8_t blimit, std::uint8_t p1, std::uint8_t p0,
                        std::uint8_t q0, std::uint8_t q1) {
    std::int8_t mask = 0;
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p1) - static_cast<int>(p0)) > limit) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q1) - static_cast<int>(q0)) > limit) * -1));
    mask = static_cast<std::int8_t>(
        mask | ((std::abs(static_cast<int>(p0) - static_cast<int>(q0)) * 2 +
                 std::abs(static_cast<int>(p1) - static_cast<int>(q1)) / 2 > blimit) *
            -1));
    return static_cast<std::int8_t>(~mask);
}

// 1:1 port of filter_mask (deblocking_common.c:160-171).
std::int8_t filterMask(std::uint8_t limit, std::uint8_t blimit, std::uint8_t p3, std::uint8_t p2, std::uint8_t p1,
                       std::uint8_t p0, std::uint8_t q0, std::uint8_t q1, std::uint8_t q2, std::uint8_t q3) {
    (void)blimit;
    std::int8_t mask = 0;
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p3) - static_cast<int>(p2)) > limit) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p2) - static_cast<int>(p1)) > limit) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p1) - static_cast<int>(p0)) > limit) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q1) - static_cast<int>(q0)) > limit) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q2) - static_cast<int>(q1)) > limit) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q3) - static_cast<int>(q2)) > limit) * -1));
    mask = static_cast<std::int8_t>(
        mask | ((std::abs(static_cast<int>(p0) - static_cast<int>(q0)) * 2 +
                 std::abs(static_cast<int>(p1) - static_cast<int>(q1)) / 2 > blimit) *
            -1));
    return static_cast<std::int8_t>(~mask);
}

// 1:1 port of filter_mask3_chroma (deblocking_common.c:173-182).
std::int8_t filterMask3Chroma(std::uint8_t limit, std::uint8_t blimit, std::uint8_t p2, std::uint8_t p1,
                              std::uint8_t p0, std::uint8_t q0, std::uint8_t q1, std::uint8_t q2) {
    std::int8_t mask = 0;
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p2) - static_cast<int>(p1)) > limit) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p1) - static_cast<int>(p0)) > limit) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q1) - static_cast<int>(q0)) > limit) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q2) - static_cast<int>(q1)) > limit) * -1));
    mask = static_cast<std::int8_t>(
        mask | ((std::abs(static_cast<int>(p0) - static_cast<int>(q0)) * 2 +
                 std::abs(static_cast<int>(p1) - static_cast<int>(q1)) / 2 > blimit) *
            -1));
    return static_cast<std::int8_t>(~mask);
}

// 1:1 port of flat_mask3_chroma (deblocking_common.c:184-192).
std::int8_t flatMask3Chroma(std::uint8_t thresh, std::uint8_t p2, std::uint8_t p1, std::uint8_t p0, std::uint8_t q0,
                            std::uint8_t q1, std::uint8_t q2) {
    std::int8_t mask = 0;
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p1) - static_cast<int>(p0)) > thresh) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q1) - static_cast<int>(q0)) > thresh) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p2) - static_cast<int>(p0)) > thresh) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q2) - static_cast<int>(q0)) > thresh) * -1));
    return static_cast<std::int8_t>(~mask);
}

// 1:1 port of flat_mask4 (deblocking_common.c:205-215).
std::int8_t flatMask4(std::uint8_t thresh, std::uint8_t p3, std::uint8_t p2, std::uint8_t p1, std::uint8_t p0,
                      std::uint8_t q0, std::uint8_t q1, std::uint8_t q2, std::uint8_t q3) {
    std::int8_t mask = 0;
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p1) - static_cast<int>(p0)) > thresh) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q1) - static_cast<int>(q0)) > thresh) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p2) - static_cast<int>(p0)) > thresh) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q2) - static_cast<int>(q0)) > thresh) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(p3) - static_cast<int>(p0)) > thresh) * -1));
    mask = static_cast<std::int8_t>(mask | ((std::abs(static_cast<int>(q3) - static_cast<int>(q0)) > thresh) * -1));
    return static_cast<std::int8_t>(~mask);
}

// 1:1 port of hev_mask (deblocking_common.c:218-223). NOT inverted: hev is
// -1 when high variance is present, 0 when not.
std::int8_t hevMask(std::uint8_t thresh, std::uint8_t p1, std::uint8_t p0, std::uint8_t q0, std::uint8_t q1) {
    std::int8_t hev = 0;
    hev = static_cast<std::int8_t>(hev | ((std::abs(static_cast<int>(p1) - static_cast<int>(p0)) > thresh) * -1));
    hev = static_cast<std::int8_t>(hev | ((std::abs(static_cast<int>(q1) - static_cast<int>(q0)) > thresh) * -1));
    return hev;
}

// 1:1 port of filter4 (deblocking_common.c:225-251).
void filter4(std::int8_t mask, std::uint8_t thresh, std::uint8_t* op1, std::uint8_t* op0, std::uint8_t* oq0,
             std::uint8_t* oq1) {
    const std::int8_t ps1 = static_cast<std::int8_t>(*op1 ^ 0x80);
    const std::int8_t ps0 = static_cast<std::int8_t>(*op0 ^ 0x80);
    const std::int8_t qs0 = static_cast<std::int8_t>(*oq0 ^ 0x80);
    const std::int8_t qs1 = static_cast<std::int8_t>(*oq1 ^ 0x80);
    const std::int8_t hev = hevMask(thresh, *op1, *op0, *oq0, *oq1);
    std::int8_t filter = static_cast<std::int8_t>(signedCharClamp(ps1 - qs1) & hev);
    filter = static_cast<std::int8_t>(signedCharClamp(filter + 3 * (qs0 - ps0)) & mask);
    const std::int8_t filter1 = static_cast<std::int8_t>(signedCharClamp(filter + 4) >> 3);
    const std::int8_t filter2 = static_cast<std::int8_t>(signedCharClamp(filter + 3) >> 3);
    *oq0 = static_cast<std::uint8_t>(signedCharClamp(qs0 - filter1) ^ 0x80);
    *op0 = static_cast<std::uint8_t>(signedCharClamp(ps0 + filter2) ^ 0x80);
    filter = static_cast<std::int8_t>(((filter1 + 1) >> 1) & ~hev);
    *oq1 = static_cast<std::uint8_t>(signedCharClamp(qs1 - filter) ^ 0x80);
    *op1 = static_cast<std::uint8_t>(signedCharClamp(ps1 + filter) ^ 0x80);
}

// 1:1 port of filter6 (deblocking_common.c:285-299).
void filter6(std::int8_t mask, std::uint8_t thresh, std::int8_t flat, std::uint8_t* op2, std::uint8_t* op1,
             std::uint8_t* op0, std::uint8_t* oq0, std::uint8_t* oq1, std::uint8_t* oq2) {
    if (flat && mask) {
        const std::uint8_t p2 = *op2, p1 = *op1, p0 = *op0;
        const std::uint8_t q0 = *oq0, q1 = *oq1, q2 = *oq2;
        // 5-tap filter [1, 2, 2, 2, 1]
        *op1 = static_cast<std::uint8_t>((p2 * 3 + p1 * 2 + p0 * 2 + q0 + 4) >> 3);
        *op0 = static_cast<std::uint8_t>((p2 + p1 * 2 + p0 * 2 + q0 * 2 + q1 + 4) >> 3);
        *oq0 = static_cast<std::uint8_t>((p1 + p0 * 2 + q0 * 2 + q1 * 2 + q2 + 4) >> 3);
        *oq1 = static_cast<std::uint8_t>((p0 + q0 * 2 + q1 * 2 + q2 * 3 + 4) >> 3);
    } else {
        filter4(mask, thresh, op1, op0, oq0, oq1);
    }
}

// 1:1 port of filter8 (deblocking_common.c:301-317).
void filter8(std::int8_t mask, std::uint8_t thresh, std::int8_t flat, std::uint8_t* op3, std::uint8_t* op2,
             std::uint8_t* op1, std::uint8_t* op0, std::uint8_t* oq0, std::uint8_t* oq1, std::uint8_t* oq2,
             std::uint8_t* oq3) {
    if (flat && mask) {
        const std::uint8_t p3 = *op3, p2 = *op2, p1 = *op1, p0 = *op0;
        const std::uint8_t q0 = *oq0, q1 = *oq1, q2 = *oq2, q3 = *oq3;
        // 7-tap filter [1, 1, 1, 2, 1, 1, 1]
        *op2 = static_cast<std::uint8_t>((p3 + p3 + p3 + 2 * p2 + p1 + p0 + q0 + 4) >> 3);
        *op1 = static_cast<std::uint8_t>((p3 + p3 + p2 + 2 * p1 + p0 + q0 + q1 + 4) >> 3);
        *op0 = static_cast<std::uint8_t>((p3 + p2 + p1 + 2 * p0 + q0 + q1 + q2 + 4) >> 3);
        *oq0 = static_cast<std::uint8_t>((p2 + p1 + p0 + 2 * q0 + q1 + q2 + q3 + 4) >> 3);
        *oq1 = static_cast<std::uint8_t>((p1 + p0 + q0 + 2 * q1 + q2 + q3 + q3 + 4) >> 3);
        *oq2 = static_cast<std::uint8_t>((p0 + q0 + q1 + 2 * q2 + q3 + q3 + q3 + 4) >> 3);
    } else {
        filter4(mask, thresh, op1, op0, oq0, oq1);
    }
}

// 1:1 port of lpf_horizontal_4_c (deblocking_common.c:253-267) with the
// SIMD_WIDTH element-[0] reads flattened to scalars.
void lpfHorizontal4(std::uint8_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh) {
    const int count = 4;
    for (int i = 0; i < count; ++i) {
        const std::uint8_t p1 = s[-2 * pitch], p0 = s[-pitch];
        const std::uint8_t q0 = s[0 * pitch], q1 = s[1 * pitch];
        const std::int8_t mask = filterMask2(limit, blimit, p1, p0, q0, q1);
        filter4(mask, thresh, s - 2 * pitch, s - 1 * pitch, s, s + 1 * pitch);
        ++s;
    }
}

// 1:1 port of lpf_vertical_4_c (deblocking_common.c:269-283).
void lpfVertical4(std::uint8_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh) {
    const int count = 4;
    for (int i = 0; i < count; ++i) {
        const std::uint8_t p1 = s[-2], p0 = s[-1];
        const std::uint8_t q0 = s[0], q1 = s[1];
        const std::int8_t mask = filterMask2(limit, blimit, p1, p0, q0, q1);
        filter4(mask, thresh, s - 2, s - 1, s, s + 1);
        s += pitch;
    }
}

// 1:1 port of lpf_horizontal_6_c (deblocking_common.c:319-335).
void lpfHorizontal6(std::uint8_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh) {
    const int count = 4;
    for (int i = 0; i < count; ++i) {
        const std::uint8_t p2 = s[-3 * pitch], p1 = s[-2 * pitch], p0 = s[-pitch];
        const std::uint8_t q0 = s[0 * pitch], q1 = s[1 * pitch], q2 = s[2 * pitch];
        const std::int8_t mask = filterMask3Chroma(limit, blimit, p2, p1, p0, q0, q1, q2);
        const std::int8_t flat = flatMask3Chroma(1, p2, p1, p0, q0, q1, q2);
        filter6(mask, thresh, flat, s - 3 * pitch, s - 2 * pitch, s - 1 * pitch, s, s + 1 * pitch, s + 2 * pitch);
        ++s;
    }
}

// 1:1 port of lpf_vertical_6_c (deblocking_common.c:337-353).
void lpfVertical6(std::uint8_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh) {
    const int count = 4;
    for (int i = 0; i < count; ++i) {
        const std::uint8_t p2 = s[-3], p1 = s[-2], p0 = s[-1];
        const std::uint8_t q0 = s[0], q1 = s[1], q2 = s[2];
        const std::int8_t mask = filterMask3Chroma(limit, blimit, p2, p1, p0, q0, q1, q2);
        const std::int8_t flat = flatMask3Chroma(1, p2, p1, p0, q0, q1, q2);
        filter6(mask, thresh, flat, s - 3, s - 2, s - 1, s, s + 1, s + 2);
        s += pitch;
    }
}

// 1:1 port of lpf_horizontal_8_c (deblocking_common.c:355-371).
void lpfHorizontal8(std::uint8_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh) {
    const int count = 4;
    for (int i = 0; i < count; ++i) {
        const std::uint8_t p3 = s[-4 * pitch], p2 = s[-3 * pitch], p1 = s[-2 * pitch], p0 = s[-pitch];
        const std::uint8_t q0 = s[0 * pitch], q1 = s[1 * pitch], q2 = s[2 * pitch], q3 = s[3 * pitch];
        const std::int8_t mask = filterMask(limit, blimit, p3, p2, p1, p0, q0, q1, q2, q3);
        const std::int8_t flat = flatMask4(1, p3, p2, p1, p0, q0, q1, q2, q3);
        filter8(mask, thresh, flat, s - 4 * pitch, s - 3 * pitch, s - 2 * pitch, s - 1 * pitch, s, s + 1 * pitch,
                s + 2 * pitch, s + 3 * pitch);
        ++s;
    }
}

// 1:1 port of lpf_vertical_8_c (deblocking_common.c:373-386).
void lpfVertical8(std::uint8_t* s, int pitch, std::uint8_t blimit, std::uint8_t limit, std::uint8_t thresh) {
    const int count = 4;
    for (int i = 0; i < count; ++i) {
        const std::uint8_t p3 = s[-4], p2 = s[-3], p1 = s[-2], p0 = s[-1];
        const std::uint8_t q0 = s[0], q1 = s[1], q2 = s[2], q3 = s[3];
        const std::int8_t mask = filterMask(limit, blimit, p3, p2, p1, p0, q0, q1, q2, q3);
        const std::int8_t flat = flatMask4(1, p3, p2, p1, p0, q0, q1, q2, q3);
        filter8(mask, thresh, flat, s - 4, s - 3, s - 2, s - 1, s, s + 1, s + 2, s + 3);
        s += pitch;
    }
}

// 1:1 port of svt_aom_update_sharpness (deblocking_common.c:568-589).
void updateSharpness(LoopFilterInfoN* lfi, int sharpnessLvl) {
    for (int lvl = 0; lvl <= kMaxLoopFilter; lvl++) {
        int blockInsideLimit = lvl >> ((sharpnessLvl > 0) + (sharpnessLvl > 4));
        if (sharpnessLvl > 0) {
            if (blockInsideLimit > (9 - sharpnessLvl)) {
                blockInsideLimit = (9 - sharpnessLvl);
            }
        }
        if (blockInsideLimit < 1) {
            blockInsideLimit = 1;
        }
        std::memset(lfi->lfthr[lvl].lim, blockInsideLimit, kSimdWidth);
        std::memset(lfi->lfthr[lvl].mblim, (2 * (lvl + 2) + blockInsideLimit), kSimdWidth);
    }
}

} // namespace l10_filters