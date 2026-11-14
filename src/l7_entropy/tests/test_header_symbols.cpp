// A5H - the header/tile writer surface of entropy_coding.c (A5).
// Reference spans: third_party/SVT-AV1/Source/Lib/Codec/entropy_coding.c
// :1343-:1349 (the wb byte-accounting helpers), :2365-:2372 (write_delta_q),
// :2671-:2685 (write_profile/write_bitdepth), :2841-:2994 (the recenter +
// primitive quniform/subexpfin/refsubexpfin writer+counter family).
//
// Test discipline: every arithmetic span gets an independent test-side
// transcription (the ORACLE), enumerated over its full small domain; every
// writer gets exact bit-position + byte-pattern assertions derived from the
// oracle, never from the port under test.

#include <doctest.h>

#include <cstdint>
#include <cstring>

#include "entropy.h"
#include "header_symbols.h"

namespace {

// ---------------------------------------------------------------------------
// Independent oracles (test-side transcriptions of the reference arithmetic;
// definitions.h:596 get_msb via the bit loop).
// ---------------------------------------------------------------------------

int oracleGetMsb(std::uint32_t n) {
    int k = 0;
    while ((n >> k) > 1) ++k;
    return k;
}

// entropy_coding.c:2842-:2850 recenter_nonneg (oracle).
std::uint16_t oracleRecenterNonneg(std::uint16_t r, std::uint16_t v) {
    if (v > (r << 1)) {
        return v;
    } else if (v >= r) {
        return (std::uint16_t)((v - r) << 1);
    } else {
        return (std::uint16_t)(((r - v) << 1) - 1);
    }
}

// entropy_coding.c:2854-:2860 recenter_finite_nonneg (oracle).
std::uint16_t oracleRecenterFiniteNonneg(std::uint16_t n, std::uint16_t r, std::uint16_t v) {
    if ((r << 1) <= n) {
        return oracleRecenterNonneg(r, v);
    } else {
        return oracleRecenterNonneg((std::uint16_t)(n - 1 - r), (std::uint16_t)(n - 1 - v));
    }
}

// entropy_coding.c:2891-:2898 svt_aom_count_primitive_quniform (oracle).
int oracleCountQuniform(std::uint16_t n, std::uint16_t v) {
    if (n <= 1) {
        return 0;
    }
    const int l = oracleGetMsb(n - 1) + 1;
    const int m = (1 << l) - n;
    return v < m ? l - 1 : l;
}

// entropy_coding.c:2877-:2889 aom_wb_write_primitive_quniform (oracle): the
// exact bit sequence, MSB-first.
int oracleBitsQuniform(std::uint16_t n, std::uint16_t v, int* bits, int* bitVals) {
    if (n <= 1) {
        return 0;
    }
    const int l = oracleGetMsb(n - 1) + 1;
    const int m = (1 << l) - n;
    if (v < m) {
        for (int b = 0; b < l - 1; ++b) bitVals[b] = (v >> (l - 2 - b)) & 1;
        *bits = l - 1;
    } else {
        const int w = m + ((v - m) >> 1);
        int k = 0;
        for (int b = 0; b < l - 1; ++b) bitVals[k++] = (w >> (l - 2 - b)) & 1;
        bitVals[k++] = (v - m) & 1;
        *bits = k;
    }
    return *bits;
}

// entropy_coding.c:2947-:2970 svt_aom_count_primitive_subexpfin (oracle).
int oracleCountSubexpfin(std::uint16_t n, std::uint16_t k, std::uint16_t v) {
    int count = 0;
    int i = 0, mk = 0;
    while (1) {
        const int b = (i ? k + i - 1 : k);
        const int a = 1 << b;
        if (n <= mk + 3 * a) {
            count += oracleCountQuniform((std::uint16_t)(n - mk), (std::uint16_t)(v - mk));
            break;
        } else {
            const int t = (v >= mk + a);
            ++count;
            if (t) {
                ++i;
                mk += a;
            } else {
                count += b;
                break;
            }
        }
    }
    return count;
}

// entropy_coding.c:2924-:2945 aom_wb_write_primitive_subexpfin (oracle): the
// exact bit sequence.
int oracleBitsSubexpfin(std::uint16_t n, std::uint16_t k, std::uint16_t v, int* bitVals) {
    int total = 0;
    int i = 0, mk = 0;
    while (1) {
        const int b = (i ? k + i - 1 : k);
        const int a = 1 << b;
        if (n <= mk + 3 * a) {
            int bits = 0;
            int vals[32];
            oracleBitsQuniform((std::uint16_t)(n - mk), (std::uint16_t)(v - mk), &bits, vals);
            for (int q = 0; q < bits; ++q) bitVals[total++] = vals[q];
            break;
        } else {
            const int t = (v >= mk + a);
            bitVals[total++] = t;
            if (t) {
                ++i;
                mk += a;
            } else {
                for (int q = b - 1; q >= 0; --q) bitVals[total++] = ((v - mk) >> q) & 1;
                break;
            }
        }
    }
    return total;
}

// entropy_coding.c:2984-:2990 svt_aom_wb_write_signed_primitive_refsubexpfin
// (oracle).
int oracleBitsSignedRefsubexpfin(std::uint16_t n, std::uint16_t k, std::int16_t ref,
                                 std::int16_t v, int* bitVals) {
    const std::uint16_t r = (std::uint16_t)(ref + n - 1);
    const std::uint16_t vv = (std::uint16_t)(v + n - 1);
    const std::uint16_t scaled_n = (std::uint16_t)((n << 1) - 1);
    const std::uint16_t rec = oracleRecenterFiniteNonneg(scaled_n, r, vv);
    return oracleBitsSubexpfin(scaled_n, k, rec, bitVals);
}

// The wb harness: zeroed buffer + offset 0.
struct WbHarness {
    std::uint8_t buf[64];
    entropy::AomWriteBitBuffer wb;
    WbHarness() {
        std::memset(buf, 0, sizeof(buf));
        wb.bit_buffer = buf;
        wb.bit_offset = 0;
    }
    std::uint32_t bits() const { return wb.bit_offset; }
    int bitAt(std::uint32_t idx) const {
        return (buf[idx / 8] >> (7 - (idx % 8))) & 1;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// wb byte accounting (entropy_coding.c:1343-:1349).
// ---------------------------------------------------------------------------
TEST_CASE("wbIsByteAligned + wbBytesWritten exhaustive over offsets") {
    for (std::uint32_t off = 0; off <= 25; ++off) {
        entropy::AomWriteBitBuffer wb;
        std::uint8_t buf[16] = {0};
        wb.bit_buffer = buf;
        wb.bit_offset = 0;
        // Advance off bits with literals (zeros).
        if (off) entropy::wbWriteLiteral(&wb, 0, (int)off);
        const int aligned = entropy::wbIsByteAligned(&wb);
        const std::uint32_t bytes = entropy::wbBytesWritten(&wb);
        CHECK(aligned == ((wb.bit_offset % 8) == 0 ? 1 : 0));
        CHECK(bytes == wb.bit_offset / 8 + (wb.bit_offset % 8 > 0 ? 1u : 0u));
        CHECK(bytes == off / 8 + (off % 8 > 0 ? 1u : 0u));
        (void)aligned;
    }
}

// ---------------------------------------------------------------------------
// recenter_nonneg :2842-:2850 - full small domain vs oracle.
// ---------------------------------------------------------------------------
TEST_CASE("recenterNonneg full domain vs oracle") {
    for (int r = 0; r <= 5; ++r) {
        for (int v = 0; v <= 13; ++v) {
            const std::uint16_t got = entropy::recenterNonneg((std::uint16_t)r, (std::uint16_t)v);
            const std::uint16_t want = oracleRecenterNonneg((std::uint16_t)r, (std::uint16_t)v);
            CHECK_EQ(got, want);
        }
    }
    // The three branches explicitly (r=2): v>4 passthrough, v==2 identity-ish
    // doubling, v<2 mirrored odd tail.
    CHECK_EQ(entropy::recenterNonneg(2, 5), 5);   // v > r<<1: passthrough
    CHECK_EQ(entropy::recenterNonneg(2, 3), 2);   // v >= r: (v-r)<<1
    CHECK_EQ(entropy::recenterNonneg(2, 1), 1);   // v < r: ((r-v)<<1)-1
    CHECK_EQ(entropy::recenterNonneg(0, 0), 0);   // the v==r==0 boundary
}

// ---------------------------------------------------------------------------
// recenter_finite_nonneg :2854-:2860 - full small domain vs oracle.
// ---------------------------------------------------------------------------
TEST_CASE("recenterFiniteNonneg full domain vs oracle") {
    for (int n = 1; n <= 8; ++n) {
        for (int r = 0; r < n; ++r) {
            for (int v = 0; v < n; ++v) {
                const std::uint16_t got =
                    entropy::recenterFiniteNonneg((std::uint16_t)n, (std::uint16_t)r, (std::uint16_t)v);
                const std::uint16_t want = oracleRecenterFiniteNonneg((std::uint16_t)n, (std::uint16_t)r,
                                                                      (std::uint16_t)v);
                CHECK_EQ(got, want);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// count + write primitive_quniform :2877-:2898.
// ---------------------------------------------------------------------------
TEST_CASE("countPrimitiveQuniform full domain vs oracle") {
    for (int n = 1; n <= 17; ++n) {
        for (int v = 0; v < n; ++v) {
            const int got = entropy::countPrimitiveQuniform((std::uint16_t)n, (std::uint16_t)v);
            const int want = oracleCountQuniform((std::uint16_t)n, (std::uint16_t)v);
            CHECK_EQ(got, want);
        }
    }
    CHECK_EQ(entropy::countPrimitiveQuniform(1, 0), 0);
}

TEST_CASE("wbWritePrimitiveQuniform bit pattern vs oracle") {
    for (int n = 2; n <= 17; ++n) {
        for (int v = 0; v < n; ++v) {
            WbHarness h;
            entropy::wbWritePrimitiveQuniform(&h.wb, (std::uint16_t)n, (std::uint16_t)v);
            int bits = 0, vals[32];
            oracleBitsQuniform((std::uint16_t)n, (std::uint16_t)v, &bits, vals);
            CHECK_EQ(h.bits(), (std::uint32_t)bits);
            for (int b = 0; b < bits; ++b) {
                REQUIRE(h.bitAt((std::uint32_t)b) == vals[b]);
            }
            // The count twin: the bit total is the quniform count.
            CHECK_EQ(entropy::countPrimitiveQuniform((std::uint16_t)n, (std::uint16_t)v), bits);
        }
    }
}

// ---------------------------------------------------------------------------
// count + write primitive_subexpfin :2924-:2970 (SUBEXPFIN_K = 3).
// ---------------------------------------------------------------------------
TEST_CASE("countPrimitiveSubexpfin full domain vs oracle") {
    for (int n = 1; n <= 17; ++n) {
        for (int v = 0; v < n; ++v) {
            const int got = entropy::countPrimitiveSubexpfin((std::uint16_t)n, 3, (std::uint16_t)v);
            const int want = oracleCountSubexpfin((std::uint16_t)n, 3, (std::uint16_t)v);
            CHECK_EQ(got, want);
        }
    }
}

TEST_CASE("wbWritePrimitiveSubexpfin bit pattern vs oracle") {
    for (int n = 2; n <= 17; ++n) {
        for (int v = 0; v < n; ++v) {
            WbHarness h;
            entropy::wbWritePrimitiveSubexpfin(&h.wb, (std::uint16_t)n, 3, (std::uint16_t)v);
            int vals[64];
            const int bits = oracleBitsSubexpfin((std::uint16_t)n, 3, (std::uint16_t)v, vals);
            CHECK_EQ(h.bits(), (std::uint32_t)bits);
            for (int b = 0; b < bits; ++b) {
                REQUIRE(h.bitAt((std::uint32_t)b) == vals[b]);
            }
            // The count twin: the subexpfin prefix bits + the quniform tail.
            CHECK_EQ(entropy::countPrimitiveSubexpfin((std::uint16_t)n, 3, (std::uint16_t)v), bits);
        }
    }
}

// ---------------------------------------------------------------------------
// write_primitive_refsubexpfin :2975-:2982 + the signed wrapper :2984-:2990
// + count_primitive_refsubexpfin :2992-:2994.
// ---------------------------------------------------------------------------
TEST_CASE("countPrimitiveRefsubexpfin equals subexpfin-of-recentered") {
    for (int n = 2; n <= 9; ++n) {
        for (int ref = 0; ref < n; ++ref) {
            for (int v = 0; v < n; ++v) {
                const std::uint16_t rec =
                    oracleRecenterFiniteNonneg((std::uint16_t)n, (std::uint16_t)ref, (std::uint16_t)v);
                CHECK_EQ(entropy::countPrimitiveRefsubexpfin((std::uint16_t)n, 3, (std::uint16_t)ref,
                                                             (std::uint16_t)v),
                         oracleCountSubexpfin((std::uint16_t)n, 3, rec));
            }
        }
    }
}

TEST_CASE("wbWritePrimitiveRefsubexpfin + signed wrapper bit pattern vs oracle") {
    // Unsigned pair over the full small domain.
    for (int n = 2; n <= 9; ++n) {
        for (int ref = 0; ref < n; ++ref) {
            for (int v = 0; v < n; ++v) {
                WbHarness h;
                entropy::wbWritePrimitiveRefsubexpfin(&h.wb, (std::uint16_t)n, 3, (std::uint16_t)ref,
                                                      (std::uint16_t)v);
                int vals[64];
                const std::uint16_t rec = oracleRecenterFiniteNonneg((std::uint16_t)n, (std::uint16_t)ref,
                                                                     (std::uint16_t)v);
                const int bits = oracleBitsSubexpfin((std::uint16_t)n, 3, rec, vals);
                CHECK_EQ(h.bits(), (std::uint32_t)bits);
                for (int b = 0; b < bits; ++b) {
                    REQUIRE(h.bitAt((std::uint32_t)b) == vals[b]);
                }
            }
        }
    }
    // The signed wrapper: scaled_n = (n << 1) - 1, ref/v recentered by n - 1.
    for (int n = 2; n <= 6; ++n) {
        for (int ref = -(n - 1); ref <= (n - 1); ++ref) {
            for (int v = -(n - 1); v <= (n - 1); ++v) {
                WbHarness h;
                entropy::wbWriteSignedPrimitiveRefsubexpfin(&h.wb, (std::uint16_t)n, 3,
                                                            (std::int16_t)ref, (std::int16_t)v);
                int vals[64];
                const int bits = oracleBitsSignedRefsubexpfin((std::uint16_t)n, 3, (std::int16_t)ref,
                                                              (std::int16_t)v, vals);
                CHECK_EQ(h.bits(), (std::uint32_t)bits);
                for (int b = 0; b < bits; ++b) {
                    REQUIRE(h.bitAt((std::uint32_t)b) == vals[b]);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// The AomWriter variants (:2863-:2875, :2901-:2922, :2975-:2977): the symbol
// stream through the EC engine roundtrips against the oracle bit sequence
// via the existing l7 literal reader (odEcReadBit / odEcReadLiteralBits).
// ---------------------------------------------------------------------------
TEST_CASE("writePrimitiveQuniform writer bits vs oracle via reader") {
    for (int n = 2; n <= 9; ++n) {
        for (int v = 0; v < n; ++v) {
            entropy::AomWriter w{};
            unsigned char buf[64] = {0};
            w.ec.buf = buf;
            entropy::odEcEncReset(&w.ec);
            w.allow_update_cdf = 0;
            w.pos = 0;
            entropy::writePrimitiveQuniform(&w, (std::uint16_t)n, (std::uint16_t)v);
            entropy::odEcStopEncode(&w);
            entropy::AomReader r;
            REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
            int vals[32];
            int bits = 0;
            oracleBitsQuniform((std::uint16_t)n, (std::uint16_t)v, &bits, vals);
            REQUIRE(bits > 0);
            // The first l-1 bits (when v < m) or l-1 bits + 1 bool (v >= m):
            // reconstruct via the oracle's shape.
            const int l = oracleGetMsb((std::uint32_t)(n - 1)) + 1;
            const int m = (1 << l) - n;
            int consumed = 0;
            if (v < m) {
                const int lit = entropy::readLiteralBits(&r, l - 1);
                consumed += l - 1;
                CHECK_EQ(lit, v);
                (void)consumed;
            } else {
                const int lit = entropy::readLiteralBits(&r, l - 1);
                const int tail = entropy::odEcReadBit(&r);
                CHECK_EQ(lit, m + ((v - m) >> 1));
                CHECK_EQ(tail, (v - m) & 1);
                (void)consumed;
            }
            (void)bits;
            (void)vals;
        }
    }
}

TEST_CASE("writePrimitiveSubexpfin writer bits vs oracle via reader") {
    for (int n = 2; n <= 20; ++n) {
        for (int v = 0; v < n; ++v) {
            entropy::AomWriter w{};
            unsigned char buf[64] = {0};
            w.ec.buf = buf;
            entropy::odEcEncReset(&w.ec);
            w.allow_update_cdf = 0;
            w.pos = 0;
            entropy::writePrimitiveSubexpfin(&w, (std::uint16_t)n, 3, (std::uint16_t)v);
            entropy::odEcStopEncode(&w);
            entropy::AomReader r;
            REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
            // Walk the oracle loop in lockstep: bool + literal per rung,
            // quniform tail at the last rung.
            int i = 0, mk = 0;
            while (1) {
                const int b = (i ? 3 + i - 1 : 3);
                const int a = 1 << b;
                if (n <= mk + 3 * a) {
                    const int l = oracleGetMsb((std::uint32_t)(n - mk - 1)) + 1;
                    const int m = (1 << l) - (n - mk);
                    const int vv = v - mk;
                    if (vv < m) {
                        if (l - 1 > 0) {
                            const int lit = entropy::readLiteralBits(&r, l - 1);
                            CHECK_EQ(lit, vv);
                        }
                    } else {
                        const int lit = entropy::readLiteralBits(&r, l - 1);
                        const int tail = entropy::odEcReadBit(&r);
                        CHECK_EQ(lit, m + ((vv - m) >> 1));
                        CHECK_EQ(tail, (vv - m) & 1);
                    }
                    break;
                } else {
                    const int t = (v >= mk + a);
                    const int gotT = entropy::odEcReadBit(&r);
                    CHECK_EQ(gotT, t);
                    if (t) {
                        ++i;
                        mk += a;
                    } else {
                        if (b > 0) {
                            const int lit = entropy::readLiteralBits(&r, b);
                            CHECK_EQ(lit, v - mk);
                        }
                        break;
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// write_delta_q :2365-:2372 over the full 7-bit signed span [-64, 63].
// ---------------------------------------------------------------------------
TEST_CASE("writeDeltaQ full span bit accounting + roundtrip") {
    for (int delta = -64; delta <= 63; ++delta) {
        WbHarness h;
        entropy::writeDeltaQ(&h.wb, delta);
        if (delta == 0) {
            CHECK_EQ(h.bits(), 1u);
            CHECK_EQ(h.bitAt(0), 0);
        } else {
            // flag bit + inv_signed_literal(delta, 6) = literal(delta, 7)
            // -> 8 bits total, the literal at stream indices 1..7.
            CHECK_EQ(h.bits(), 8u);
            CHECK_EQ(h.bitAt(0), 1);
            for (int b = 0; b < 7; ++b) {
                const int want = (delta >> (6 - b)) & 1;
                REQUIRE(h.bitAt((std::uint32_t)(1 + b)) == want);
            }
        }
    }
    // The nonzero boundary probe: -1 is all ones, 1 is 0000001 (the flag
    // bit takes stream index 0, the literal occupies stream indices 1..7).
    {
        WbHarness h;
        entropy::writeDeltaQ(&h.wb, -1);
        CHECK_EQ(h.bits(), 8u);
        CHECK_EQ(h.bitAt(0), 1);
        for (int b = 0; b < 7; ++b) CHECK_EQ(h.bitAt((std::uint32_t)(1 + b)), 1);
    }
    {
        WbHarness h;
        entropy::writeDeltaQ(&h.wb, 1);
        CHECK_EQ(h.bits(), 8u);
        CHECK_EQ(h.bitAt(0), 1);
        CHECK_EQ(h.bitAt(7), 1);
        std::uint32_t ones = 0;
        for (int b = 1; b < 7; ++b) ones += (std::uint32_t)h.bitAt((std::uint32_t)b);
        CHECK_EQ(ones, 0u);
    }
}

// ---------------------------------------------------------------------------
// write_profile :2671-:2674 (PROFILE_BITS = 3) over the profile domain.
// ---------------------------------------------------------------------------
TEST_CASE("writeProfile writes literal 3-bit profile") {
    for (int profile = 0; profile < 3; ++profile) {
        WbHarness h;
        entropy::writeProfile(profile, &h.wb);
        CHECK_EQ(h.bits(), 3u);
        for (int b = 0; b < 3; ++b) {
            const int want = (profile >> (2 - b)) & 1;
            REQUIRE(h.bitAt((std::uint32_t)b) == want);
        }
    }
}

// ---------------------------------------------------------------------------
// write_bitdepth :2676-:2685 - the full profile x depth branch matrix.
// ---------------------------------------------------------------------------
TEST_CASE("writeBitdepth branch matrix") {
    // (profile, encoder_bit_depth) -> the exact bit sequence.
    // SVT_EFFECTIVE_BIT_DEPTH is the identity for the pinned HBD=1 build
    // (EbConfigMacros.h:175/293).
    const int profiles[3] = {0, 1, 2};  // MAIN, HIGH, PROFESSIONAL
    for (int pi = 0; pi < 3; ++pi) {
        for (int depth = 8; depth <= 12; depth += 2) {
            WbHarness h;
            entropy::HdrSeqHeader sh{};
            sh.profile = profiles[pi];
            sh.encoder_bit_depth = depth;
            entropy::writeBitdepth(&sh, &h.wb);
            const int effIs8 = (depth == 8);
            if (profiles[pi] != 2) {
                // One bit: 0 for 8-bit, 1 otherwise.
                CHECK_EQ(h.bits(), 1u);
                CHECK_EQ(h.bitAt(0), effIs8 ? 0 : 1);
            } else {
                if (effIs8) {
                    CHECK_EQ(h.bits(), 1u);
                    CHECK_EQ(h.bitAt(0), 0);
                } else {
                    CHECK_EQ(h.bits(), 2u);
                    CHECK_EQ(h.bitAt(0), 1);
                    CHECK_EQ(h.bitAt(1), depth == 10 ? 0 : 1);
                }
            }
        }
    }
}
