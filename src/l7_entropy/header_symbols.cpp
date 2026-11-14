// A5H - the frame-header / tile-info writer surface of entropy_coding.c.
// Reference: third_party/SVT-AV1/Source/Lib/Codec/entropy_coding.c. Every
// function cites its span; the carrier walls are threaded per the RE8A
// pattern (see header_symbols.h).

#include "header_symbols.h"

#include <cassert>
#include <cstring>

namespace entropy {

namespace {

// definitions.h:596-:611 get_msb (MSVC _BitScanReverse form); the l7
// file-local twin (entropy.cpp:23 precedent).
inline int getMsb(std::uint32_t n) {
    assert(n != 0);
    unsigned long first_set_bit;
    _BitScanReverse(&first_set_bit, n);
    return (int)first_set_bit;
}

}  // namespace

// svt_aom_wb_is_byte_aligned (entropy_coding.c:1343-:1345)
int wbIsByteAligned(const AomWriteBitBuffer* wb) {
    return (wb->bit_offset % CHAR_BIT == 0);
}

// svt_aom_wb_bytes_written (entropy_coding.c:1347-:1349)
std::uint32_t wbBytesWritten(const AomWriteBitBuffer* wb) {
    return wb->bit_offset / CHAR_BIT + (wb->bit_offset % CHAR_BIT > 0);
}

// recenter_nonneg (entropy_coding.c:2842-:2850)
std::uint16_t recenterNonneg(std::uint16_t r, std::uint16_t v) {
    if (v > (r << 1)) {
        return v;
    } else if (v >= r) {
        return (std::uint16_t)((v - r) << 1);
    } else {
        return (std::uint16_t)(((r - v) << 1) - 1);
    }
}

// recenter_finite_nonneg (entropy_coding.c:2854-:2860)
std::uint16_t recenterFiniteNonneg(std::uint16_t n, std::uint16_t r, std::uint16_t v) {
    if ((r << 1) <= n) {
        return recenterNonneg(r, v);
    } else {
        return recenterNonneg((std::uint16_t)(n - 1 - r), (std::uint16_t)(n - 1 - v));
    }
}

// svt_aom_write_primitive_quniform (entropy_coding.c:2863-:2875)
void writePrimitiveQuniform(AomWriter* w, std::uint16_t n, std::uint16_t v) {
    if (n <= 1) {
        return;
    }
    const std::int32_t l = getMsb(n - 1) + 1;
    const std::int32_t m = (1 << l) - n;
    if (v < m) {
        odEcWriteLiteralBits(w, v, l - 1);
    } else {
        odEcWriteLiteralBits(w, m + ((v - m) >> 1), l - 1);
        odEcWriteLiteralBit(w, (v - m) & 1, 0);
    }
}

// aom_wb_write_primitive_quniform (entropy_coding.c:2877-:2889)
void wbWritePrimitiveQuniform(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t v) {
    if (n <= 1) {
        return;
    }
    const std::int32_t l = getMsb(n - 1) + 1;
    const std::int32_t m = (1 << l) - n;
    if (v < m) {
        wbWriteLiteral(wb, v, l - 1);
    } else {
        wbWriteLiteral(wb, m + ((v - m) >> 1), l - 1);
        wbWriteBit(wb, (v - m) & 1);
    }
}

// svt_aom_count_primitive_quniform (entropy_coding.c:2891-:2898)
int countPrimitiveQuniform(std::uint16_t n, std::uint16_t v) {
    if (n <= 1) {
        return 0;
    }
    const std::int32_t l = getMsb(n - 1) + 1;
    const std::int32_t m = (1 << l) - n;
    return v < m ? l - 1 : l;
}

// svt_aom_write_primitive_subexpfin (entropy_coding.c:2901-:2922)
void writePrimitiveSubexpfin(AomWriter* w, std::uint16_t n, std::uint16_t k, std::uint16_t v) {
    std::int32_t i = 0;
    std::int32_t mk = 0;
    while (1) {
        std::int32_t b = (i ? k + i - 1 : k);
        std::int32_t a = (1 << b);
        if (n <= mk + 3 * a) {
            writePrimitiveQuniform(w, (std::uint16_t)(n - mk), (std::uint16_t)(v - mk));
            break;
        } else {
            std::int32_t t = (v >= mk + a);
            odEcWriteLiteralBit(w, t, 0);
            if (t) {
                i = i + 1;
                mk += a;
            } else {
                odEcWriteLiteralBits(w, v - mk, b);
                break;
            }
        }
    }
}

// aom_wb_write_primitive_subexpfin (entropy_coding.c:2924-:2945)
void wbWritePrimitiveSubexpfin(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t k,
                               std::uint16_t v) {
    std::int32_t i = 0;
    std::int32_t mk = 0;
    while (1) {
        std::int32_t b = (i ? k + i - 1 : k);
        std::int32_t a = (1 << b);
        if (n <= mk + 3 * a) {
            wbWritePrimitiveQuniform(wb, (std::uint16_t)(n - mk), (std::uint16_t)(v - mk));
            break;
        } else {
            std::int32_t t = (v >= mk + a);
            wbWriteBit(wb, t);
            if (t) {
                i = i + 1;
                mk += a;
            } else {
                wbWriteLiteral(wb, v - mk, b);
                break;
            }
        }
    }
}

// svt_aom_count_primitive_subexpfin (entropy_coding.c:2947-:2970)
int countPrimitiveSubexpfin(std::uint16_t n, std::uint16_t k, std::uint16_t v) {
    std::int32_t count = 0;
    std::int32_t i = 0;
    std::int32_t mk = 0;
    while (1) {
        std::int32_t b = (i ? k + i - 1 : k);
        std::int32_t a = (1 << b);
        if (n <= mk + 3 * a) {
            count += countPrimitiveQuniform((std::uint16_t)(n - mk), (std::uint16_t)(v - mk));
            break;
        } else {
            std::int32_t t = (v >= mk + a);
            count++;
            if (t) {
                i = i + 1;
                mk += a;
            } else {
                count += b;
                break;
            }
        }
    }
    return count;
}

// svt_aom_write_primitive_refsubexpfin (entropy_coding.c:2975-:2977)
void writePrimitiveRefsubexpfin(AomWriter* w, std::uint16_t n, std::uint16_t k, std::uint16_t ref,
                                std::uint16_t v) {
    writePrimitiveSubexpfin(w, n, k, recenterFiniteNonneg(n, ref, v));
}

// aom_wb_write_primitive_refsubexpfin (entropy_coding.c:2979-:2982)
void wbWritePrimitiveRefsubexpfin(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t k,
                                  std::uint16_t ref, std::uint16_t v) {
    wbWritePrimitiveSubexpfin(wb, n, k, recenterFiniteNonneg(n, ref, v));
}

// svt_aom_wb_write_signed_primitive_refsubexpfin (entropy_coding.c:2984-:2990)
void wbWriteSignedPrimitiveRefsubexpfin(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t k,
                                        std::int16_t ref, std::int16_t v) {
    ref += n - 1;
    v += n - 1;
    const std::uint16_t scaled_n = (std::uint16_t)((n << 1) - 1);
    wbWritePrimitiveRefsubexpfin(wb, scaled_n, k, ref, v);
}

// svt_aom_count_primitive_refsubexpfin (entropy_coding.c:2992-:2994)
int countPrimitiveRefsubexpfin(std::uint16_t n, std::uint16_t k, std::uint16_t ref, std::uint16_t v) {
    return countPrimitiveSubexpfin(n, k, recenterFiniteNonneg(n, ref, v));
}

// write_delta_q (entropy_coding.c:2365-:2372)
void writeDeltaQ(AomWriteBitBuffer* wb, std::int32_t delta_q) {
    if (delta_q != 0) {
        wbWriteBit(wb, 1);
        wbWriteInvSignedLiteral(wb, delta_q, 6);
    } else {
        wbWriteBit(wb, 0);
    }
}

// write_profile (entropy_coding.c:2671-:2674)
void writeProfile(std::int32_t profile, AomWriteBitBuffer* wb) {
    assert(profile >= 0 && profile < 4);  // PROFILE_0..MAX_PROFILES bound
    wbWriteLiteral(wb, profile, HD_PROFILE_BITS);
}

// write_bitdepth (entropy_coding.c:2676-:2685). SVT_EFFECTIVE_BIT_DEPTH is
// the identity for the pinned HBD=1 build (EbConfigMacros.h:175/:293).
void writeBitdepth(const HdrSeqHeader* sh, AomWriteBitBuffer* wb) {
    // Profile 0/1: [0] for 8 bit, [1]  10-bit
    // Profile   2: [0] for 8 bit, [10] 10-bit, [11] - 12-bit
    wbWriteBit(wb, sh->encoder_bit_depth == HD_EB_EIGHT_BIT ? 0 : 1);
    if (sh->profile == HD_PROFESSIONAL_PROFILE &&
        sh->encoder_bit_depth != HD_EB_EIGHT_BIT) {
        wbWriteBit(wb, sh->encoder_bit_depth == HD_EB_TEN_BIT ? 0 : 1);
    }
}

}  // namespace entropy
