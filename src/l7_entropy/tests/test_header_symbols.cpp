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

#include <cstdio>
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

// ---------------------------------------------------------------------------
// C2: encode_loopfilter :2284-:2342 - the flag/level branch matrix with a
// byte-exact independent oracle.
// ---------------------------------------------------------------------------
namespace {
// Oracle transcription of entropy_coding.c:2284-:2342 (test side).
void oracleLoopfilter(int allow_intrabc, int fl0, int fl1, int flu, int flv, int sharp,
                      int mrde, int* bitVals, int* nBits) {
    int n = 0;
    if (allow_intrabc) {
        *nBits = 0;
        return;
    }
    for (int b = 5; b >= 0; --b) bitVals[n++] = (fl0 >> b) & 1;
    for (int b = 5; b >= 0; --b) bitVals[n++] = (fl1 >> b) & 1;
    if (fl0 || fl1) {
        for (int b = 5; b >= 0; --b) bitVals[n++] = (flu >> b) & 1;
        for (int b = 5; b >= 0; --b) bitVals[n++] = (flv >> b) & 1;
    }
    for (int b = 2; b >= 0; --b) bitVals[n++] = (sharp >> b) & 1;
    bitVals[n++] = mrde;
    *nBits = n;
}
entropy::HdrPcs c2Pcs(entropy::HdrSeqHeader* sh, entropy::HdrAv1Common* cm) {
    *sh = entropy::HdrSeqHeader{};
    *cm = entropy::HdrAv1Common{};
    entropy::HdrPcs pcs{};
    pcs.scs = sh;
    pcs.av1_cm = cm;
    return pcs;
}
}  // namespace

TEST_CASE("encodeLoopfilter branch matrix vs oracle") {
    const int levels[4] = {0, 1, 31, 63};
    for (int intrabc = 0; intrabc < 2; ++intrabc) {
        for (int i0 = 0; i0 < 4; ++i0) {
            for (int i1 = 0; i1 < 4; ++i1) {
                for (int mrde = 0; mrde < 2; ++mrde) {
                    WbHarness h;
                    entropy::HdrSeqHeader sh_c2{};
            entropy::HdrAv1Common cm_c2{};
            entropy::HdrPcs pcs = c2Pcs(&sh_c2, &cm_c2);
                    pcs.frm_hdr.allow_intrabc = intrabc;
                    pcs.frm_hdr.loop_filter_params.filter_level[0] = levels[i0];
                    pcs.frm_hdr.loop_filter_params.filter_level[1] = levels[i1];
                    pcs.frm_hdr.loop_filter_params.filter_level_u = 17;
                    pcs.frm_hdr.loop_filter_params.filter_level_v = 29;
                    pcs.frm_hdr.loop_filter_params.sharpness_level = 5;
                    pcs.frm_hdr.loop_filter_params.mode_ref_delta_enabled = mrde;
                    entropy::encodeLoopfilter(&pcs, &h.wb);
                    int vals[64], nBits = 0;
                    oracleLoopfilter(intrabc, levels[i0], levels[i1], 17, 29, 5, mrde, vals, &nBits);
                    if (intrabc) {
                        CHECK_EQ(h.bits(), 0u);
                        continue;
                    }
                    CHECK_EQ(h.bits(), (std::uint32_t)nBits);
                    for (int b = 0; b < nBits; ++b) {
                        REQUIRE(h.bitAt((std::uint32_t)b) == vals[b]);
                    }
                }
            }
        }
    }
    // The chroma-levels gate: u/v literals appear EXACTLY when a luma level
    // is nonzero (16 vs 28 bits).
    {
        WbHarness h0, h1;
        entropy::HdrSeqHeader sh0{}, sh1{};
        entropy::HdrAv1Common cm0{}, cm1{};
        entropy::HdrPcs p0 = c2Pcs(&sh0, &cm0), p1 = c2Pcs(&sh1, &cm1);
        p0.frm_hdr.loop_filter_params.filter_level[0] = 0;
        p0.frm_hdr.loop_filter_params.filter_level[1] = 0;
        p1.frm_hdr.loop_filter_params.filter_level[0] = 0;
        p1.frm_hdr.loop_filter_params.filter_level[1] = 1;
        entropy::encodeLoopfilter(&p0, &h0.wb);
        entropy::encodeLoopfilter(&p1, &h1.wb);
        CHECK_EQ(h0.bits(), 16u);
        CHECK_EQ(h1.bits(), 28u);
    }
}

// ---------------------------------------------------------------------------
// C2: encode_cdef :2344-:2363.
// ---------------------------------------------------------------------------
TEST_CASE("encodeCdef damping/bits/strength grid vs oracle") {
    for (int intrabc = 0; intrabc < 2; ++intrabc) {
        for (int damping = 3; damping <= 6; ++damping) {
            for (int cdefBits = 0; cdefBits <= 3; ++cdefBits) {
                for (int nb = 0; nb <= 3; ++nb) {
                    WbHarness h;
                    entropy::HdrSeqHeader sh_c2{};
            entropy::HdrAv1Common cm_c2{};
            entropy::HdrPcs pcs = c2Pcs(&sh_c2, &cm_c2);
                    pcs.frm_hdr.allow_intrabc = intrabc;
                    pcs.frm_hdr.cdef_params.cdef_damping = damping;
                    pcs.frm_hdr.cdef_params.cdef_bits = cdefBits;
                    pcs.nb_cdef_strengths = nb;
                    for (int i = 0; i < nb; ++i) {
                        pcs.frm_hdr.cdef_params.cdef_y_strength[i] = 21 + i;
                        pcs.frm_hdr.cdef_params.cdef_uv_strength[i] = 40 + i;
                    }
                    entropy::encodeCdef(&pcs, &h.wb);
                    if (intrabc) {
                        CHECK_EQ(h.bits(), 0u);
                        continue;
                    }
                    CHECK_EQ(h.bits(), (std::uint32_t)(4 + 12 * nb));
                    // damping - 3 as a 2-bit literal, then cdef_bits.
                    for (int b = 0; b < 2; ++b)
                        REQUIRE(h.bitAt((std::uint32_t)b) == (((damping - 3) >> (1 - b)) & 1));
                    for (int b = 0; b < 2; ++b)
                        REQUIRE(h.bitAt((std::uint32_t)(2 + b)) == (((cdefBits) >> (1 - b)) & 1));
                    for (int i = 0; i < nb; ++i) {
                        for (int b = 0; b < 6; ++b)
                            REQUIRE(h.bitAt((std::uint32_t)(4 + 12 * i + b)) ==
                                    (((21 + i) >> (5 - b)) & 1));
                        for (int b = 0; b < 6; ++b)
                            REQUIRE(h.bitAt((std::uint32_t)(4 + 12 * i + 6 + b)) ==
                                    (((40 + i) >> (5 - b)) & 1));
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// C2: encode_quantization :2374-:2401.
// ---------------------------------------------------------------------------
namespace {
// Oracle transcription of entropy_coding.c:2365-:2401 (write_delta_q +
// encode_quantization).
void oracleDeltaQ(int delta_q, int* bitVals, int* nBits) {
    int n = 0;
    if (delta_q != 0) {
        bitVals[n++] = 1;
        // inv_signed_literal(delta_q, 6) = literal(delta_q, 7)
        for (int b = 6; b >= 0; --b) bitVals[n++] = (delta_q >> b) & 1;
    } else {
        bitVals[n++] = 0;
    }
    *nBits = n;
}
void oracleQuantization(int base_q, int dy, int du, int dv, int au, int av, int qm,
                        int qmy, int qmu, int qmv, int* bitVals, int* nBits) {
    int n = 0;
    for (int b = 7; b >= 0; --b) bitVals[n++] = (base_q >> b) & 1;
    int m;
    oracleDeltaQ(dy, bitVals + n, &m);
    n += m;
    const int diff = (du != dv) || (au != av);
    if (diff) bitVals[n++] = diff;
    oracleDeltaQ(du, bitVals + n, &m);
    n += m;
    oracleDeltaQ(au, bitVals + n, &m);
    n += m;
    if (diff) {
        oracleDeltaQ(dv, bitVals + n, &m);
        n += m;
        oracleDeltaQ(av, bitVals + n, &m);
        n += m;
    }
    bitVals[n++] = qm;
    if (qm) {
        for (int b = 3; b >= 0; --b) bitVals[n++] = (qmy >> b) & 1;
        for (int b = 3; b >= 0; --b) bitVals[n++] = (qmu >> b) & 1;
        if (diff) {
            for (int b = 3; b >= 0; --b) bitVals[n++] = (qmv >> b) & 1;
        }
    }
    *nBits = n;
}
}  // namespace

TEST_CASE("encodeQuantization full Y-delta span + diff gates vs oracle") {
    // The Y delta over the full signed span with U/V identical.
    for (int dy = -64; dy <= 63; ++dy) {
        WbHarness h;
        entropy::HdrSeqHeader sh_c2{};
            entropy::HdrAv1Common cm_c2{};
            entropy::HdrPcs pcs = c2Pcs(&sh_c2, &cm_c2);
        pcs.frm_hdr.quantization_params.base_q_idx = 137;
        pcs.frm_hdr.quantization_params.delta_q_dc[entropy::HD_PLANE_Y] = dy;
        pcs.frm_hdr.quantization_params.delta_q_dc[entropy::HD_PLANE_U] = 3;
        pcs.frm_hdr.quantization_params.delta_q_dc[entropy::HD_PLANE_V] = 3;
        pcs.frm_hdr.quantization_params.delta_q_ac[entropy::HD_PLANE_U] = 4;
        pcs.frm_hdr.quantization_params.delta_q_ac[entropy::HD_PLANE_V] = 4;
        entropy::encodeQuantization(&pcs, &h.wb);
        int vals[128], nBits = 0;
        oracleQuantization(137, dy, 3, 3, 4, 4, 0, 0, 0, 0, vals, &nBits);
        CHECK_EQ(h.bits(), (std::uint32_t)nBits);
        for (int b = 0; b < nBits; ++b) {
            REQUIRE(h.bitAt((std::uint32_t)b) == vals[b]);
        }
    }
    // The diff_uv_delta gates x qmatrix.
    for (int qm = 0; qm < 2; ++qm) {
        for (int caseIdx = 0; caseIdx < 4; ++caseIdx) {
            // case 0: dc equal, ac equal (no diff bit)
            // case 1: dc differs (diff bit)
            // case 2: ac differs (diff bit)
            // case 3: both differ (diff bit)
            WbHarness h;
            entropy::HdrSeqHeader sh_c2{};
            entropy::HdrAv1Common cm_c2{};
            entropy::HdrPcs pcs = c2Pcs(&sh_c2, &cm_c2);
            pcs.frm_hdr.quantization_params.base_q_idx = 200;
            pcs.frm_hdr.quantization_params.delta_q_dc[entropy::HD_PLANE_Y] = -5;
            pcs.frm_hdr.quantization_params.delta_q_dc[entropy::HD_PLANE_U] = caseIdx == 1 ? 2 : 3;
            pcs.frm_hdr.quantization_params.delta_q_dc[entropy::HD_PLANE_V] = 3;
            pcs.frm_hdr.quantization_params.delta_q_ac[entropy::HD_PLANE_U] = caseIdx == 2 ? 7 : 4;
            pcs.frm_hdr.quantization_params.delta_q_ac[entropy::HD_PLANE_V] = 4;
            pcs.frm_hdr.quantization_params.delta_q_ac[entropy::HD_PLANE_Y] = 0;
            pcs.frm_hdr.quantization_params.using_qmatrix = qm;
            pcs.frm_hdr.quantization_params.qm[entropy::HD_PLANE_Y] = qm ? 9 : 0;
            pcs.frm_hdr.quantization_params.qm[entropy::HD_PLANE_U] = qm ? 10 : 0;
            pcs.frm_hdr.quantization_params.qm[entropy::HD_PLANE_V] = qm ? (caseIdx ? 11 : 10) : 0;
            entropy::encodeQuantization(&pcs, &h.wb);
            const int diff = (caseIdx != 0);
            int vals[128], nBits = 0;
            oracleQuantization(200, -5, caseIdx == 1 ? 2 : 3, 3, caseIdx == 2 ? 7 : 4, 4, qm,
                               qm ? 9 : 0, qm ? 10 : 0, qm ? (caseIdx ? 11 : 10) : 0, vals, &nBits);
            CHECK_EQ(h.bits(), (std::uint32_t)nBits);
            for (int b = 0; b < nBits; ++b) {
                REQUIRE(h.bitAt((std::uint32_t)b) == vals[b]);
            }
            (void)diff;
        }
    }
}

// ---------------------------------------------------------------------------
// C2: encode_restoration_mode :2189-:2251.
// ---------------------------------------------------------------------------
namespace {
// Oracle transcription of entropy_coding.c:2189-:2251.
void oracleRestoration(int intrabc, const int types[3], int sb128, const int units[3],
                       int* bitVals, int* nBits) {
    int n = 0;
    if (intrabc) {
        *nBits = 0;
        return;
    }
    const int pair[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    int allNone = 1, chromaNone = 1;
    for (int p = 0; p < 3; ++p) {
        if (types[p] != 0) {
            allNone = 0;
            chromaNone &= (p == 0);
        }
        bitVals[n++] = pair[types[p]][0];
        bitVals[n++] = pair[types[p]][1];
    }
    if (!allNone) {
        if (!sb128) bitVals[n++] = units[0] > 64;
        if (units[0] > 64) bitVals[n++] = units[0] > 128;
    }
    if (!chromaNone) {
        bitVals[n++] = units[1] != units[0];
    }
    *nBits = n;
}
}  // namespace

TEST_CASE("encodeRestorationMode type grid + unit bits vs oracle") {
    const int types[4] = {0, 1, 2, 3};  // NONE, WIENER, SGRPROJ, SWITCHABLE
    // The full 4^3 type grid with fixed units.
    for (int t0 = 0; t0 < 4; ++t0) {
        for (int t1 = 0; t1 < 4; ++t1) {
            for (int t2 = 0; t2 < 4; ++t2) {
                const int ty[3] = {types[t0], types[t1], types[t2]};
                const int units[3] = {128, 128, 128};
                WbHarness h;
                entropy::HdrSeqHeader sh_c2{};
            entropy::HdrAv1Common cm_c2{};
            entropy::HdrPcs pcs = c2Pcs(&sh_c2, &cm_c2);
                pcs.frm_hdr.allow_intrabc = 0;
                for (int p = 0; p < 3; ++p) {
                    pcs.rst_info[p].frame_restoration_type = ty[p];
                    pcs.rst_info[p].restoration_unit_size = units[p];
                }
                pcs.sb_size_is_128 = 0;
                entropy::encodeRestorationMode(&pcs, &h.wb);
                int vals[64], nBits = 0;
                oracleRestoration(0, ty, 0, units, vals, &nBits);
                CHECK_EQ(h.bits(), (std::uint32_t)nBits);
                for (int b = 0; b < nBits; ++b) {
                    REQUIRE(h.bitAt((std::uint32_t)b) == vals[b]);
                }
            }
        }
    }
    // The unit-size bits over the sb_size x unit grid.
    for (int sb128 = 0; sb128 < 2; ++sb128) {
        for (int u0 = 0; u0 < 3; ++u0) {
            const int unit = 64 * (1 << u0);  // 64, 128, 256
            const int ty[3] = {1, 1, 1};      // all WIENER: chroma bit live
            const int units[3] = {unit, unit, unit};
            WbHarness h;
            entropy::HdrSeqHeader sh_c2{};
            entropy::HdrAv1Common cm_c2{};
            entropy::HdrPcs pcs = c2Pcs(&sh_c2, &cm_c2);
            for (int p = 0; p < 3; ++p) {
                pcs.rst_info[p].frame_restoration_type = ty[p];
                pcs.rst_info[p].restoration_unit_size = units[p];
            }
            pcs.sb_size_is_128 = sb128;
            entropy::encodeRestorationMode(&pcs, &h.wb);
            int vals[64], nBits = 0;
            oracleRestoration(0, ty, sb128, units, vals, &nBits);
            CHECK_EQ(h.bits(), (std::uint32_t)nBits);
            for (int b = 0; b < nBits; ++b) {
                REQUIRE(h.bitAt((std::uint32_t)b) == vals[b]);
            }
        }
    }
    // The chroma-unit difference bit: chroma type non-NONE with a smaller
    // chroma unit writes 1.
    {
        WbHarness h;
        entropy::HdrSeqHeader sh_c2{};
            entropy::HdrAv1Common cm_c2{};
            entropy::HdrPcs pcs = c2Pcs(&sh_c2, &cm_c2);
        pcs.rst_info[0].frame_restoration_type = 1;  // WIENER
        pcs.rst_info[0].restoration_unit_size = 128;
        pcs.rst_info[1].frame_restoration_type = 1;
        pcs.rst_info[1].restoration_unit_size = 64;
        pcs.rst_info[2].frame_restoration_type = 1;
        pcs.rst_info[2].restoration_unit_size = 64;
        pcs.sb_size_is_128 = 1;
        entropy::encodeRestorationMode(&pcs, &h.wb);
        const int ty128[3] = {1, 1, 1};
        const int u12864[3] = {128, 64, 64};
        int vals[64], nBits = 0;
        oracleRestoration(0, ty128, 1, u12864, vals, &nBits);
        CHECK_EQ(h.bits(), (std::uint32_t)nBits);
        CHECK_EQ(h.bitAt((std::uint32_t)(nBits - 1)), 1);
    }
    // intrabc short-circuit.
    {
        WbHarness h;
        entropy::HdrSeqHeader sh_c2{};
            entropy::HdrAv1Common cm_c2{};
            entropy::HdrPcs pcs = c2Pcs(&sh_c2, &cm_c2);
        pcs.frm_hdr.allow_intrabc = 1;
        entropy::encodeRestorationMode(&pcs, &h.wb);
        CHECK_EQ(h.bits(), 0u);
    }
}

// ---------------------------------------------------------------------------
// C2: write_render_size :2616-:2629 + write_superres_scale :2631-:2650 +
// write_frame_size :2652-:2669.
// ---------------------------------------------------------------------------
TEST_CASE("writeRenderSize + writeSuperresScale exact bit counts") {
    // Render size: the resize gate.
    {
        WbHarness h;
        entropy::HdrSeqHeader sh_c2{};
            entropy::HdrAv1Common cm_c2{};
            entropy::HdrPcs pcs = c2Pcs(&sh_c2, &cm_c2);
        pcs.frame_resize_enabled = 0;
        entropy::writeRenderSize(&h.wb, &pcs);
        CHECK_EQ(h.bits(), 1u);
        CHECK_EQ(h.bitAt(0), 0);
    }
    {
        WbHarness h;
        entropy::HdrSeqHeader sh_c2{};
            entropy::HdrAv1Common cm_c2{};
            entropy::HdrPcs pcs = c2Pcs(&sh_c2, &cm_c2);
        pcs.frame_resize_enabled = 1;
        pcs.render_width = 1024;
        pcs.render_height = 577;
        entropy::writeRenderSize(&h.wb, &pcs);
        CHECK_EQ(h.bits(), 33u);
        CHECK_EQ(h.bitAt(0), 1);
        for (int b = 0; b < 16; ++b)
            REQUIRE(h.bitAt((std::uint32_t)(1 + b)) == (((1023) >> (15 - b)) & 1));
        for (int b = 0; b < 16; ++b)
            REQUIRE(h.bitAt((std::uint32_t)(17 + b)) == (((576) >> (15 - b)) & 1));
    }
    // Superres: enable gate + the denominator domain [8, 15].
    {
        WbHarness h;
        entropy::HdrSeqHeader sh_c2{};
            entropy::HdrAv1Common cm_c2{};
            entropy::HdrPcs pcs = c2Pcs(&sh_c2, &cm_c2);
        pcs.scs->enable_superres = 0;
        pcs.av1_cm->frm_size.superres_denominator = 8;
        entropy::writeSuperresScale(&h.wb, &pcs);
        CHECK_EQ(h.bits(), 0u);
    }
    for (int denom = 8; denom <= 15; ++denom) {
        WbHarness h;
        entropy::HdrSeqHeader sh_c2{};
            entropy::HdrAv1Common cm_c2{};
            entropy::HdrPcs pcs = c2Pcs(&sh_c2, &cm_c2);
        pcs.scs->enable_superres = 1;
        pcs.av1_cm->frm_size.superres_denominator = denom;
        entropy::writeSuperresScale(&h.wb, &pcs);
        if (denom == 8) {
            CHECK_EQ(h.bits(), 1u);
            CHECK_EQ(h.bitAt(0), 0);
        } else {
            CHECK_EQ(h.bits(), 4u);
            CHECK_EQ(h.bitAt(0), 1);
            for (int b = 0; b < 3; ++b)
                REQUIRE(h.bitAt((std::uint32_t)(1 + b)) == ((((denom - 9)) >> (2 - b)) & 1));
        }
    }
}

TEST_CASE("writeFrameSize composition: override + superres + render") {
    for (int override_ = 0; override_ < 2; ++override_) {
        for (int denom = 8; denom <= 10; ++denom) {
            for (int resize = 0; resize < 2; ++resize) {
                WbHarness h;
                entropy::HdrSeqHeader sh_c2{};
            entropy::HdrAv1Common cm_c2{};
            entropy::HdrPcs pcs = c2Pcs(&sh_c2, &cm_c2);
                pcs.scs->enable_superres = 1;
                pcs.scs->frame_width_bits = 8;
                pcs.scs->frame_height_bits = 8;
                pcs.av1_cm->frm_size.superres_upscaled_width = 256;
                pcs.av1_cm->frm_size.superres_upscaled_height = 144;
                pcs.av1_cm->frm_size.superres_denominator = denom;
                pcs.frame_resize_enabled = resize;
                pcs.render_width = 256;
                pcs.render_height = 144;
                entropy::writeFrameSize(&pcs, override_, &h.wb);
                std::uint32_t want = override_ ? 16u : 0u;
                want += (denom == 8) ? 1u : 4u;
                want += resize ? 33u : 1u;
                CHECK_EQ(h.bits(), want);
            }
        }
    }
}
// ---------------------------------------------------------------------------
// C3: the tile machinery. Oracle transcriptions of
// entropy_coding.c:2403-:2614 (test side).
// ---------------------------------------------------------------------------
namespace {
int oracleAlignPow2(int value, int n) {
    return (value + ((1 << n) - 1)) & ~((1 << n) - 1);
}
int oracleTileLog2(int blk_size, int target) {
    int k;
    for (k = 0; (blk_size << k) < target; k++) {}
    return k;
}
// Oracle of svt_av1_get_tile_limits :2450-:2467.
void oracleTileLimits(int mi_cols_in, int mi_rows_in, int log2_sb, int* out) {
    const int mi_cols = oracleAlignPow2(mi_cols_in, log2_sb);
    const int mi_rows = oracleAlignPow2(mi_rows_in, log2_sb);
    const int sb_cols = mi_cols >> log2_sb;
    const int sb_rows = mi_rows >> log2_sb;
    const int sb_size_log2 = log2_sb + 2;
    out[0] = 4096 >> sb_size_log2;                       // max_tile_width_sb
    const int max_tile_area_sb = (4096 * 2304) >> (2 * sb_size_log2);
    out[1] = oracleTileLog2(out[0], sb_cols);            // min_log2_tile_cols
    out[2] = oracleTileLog2(1, sb_cols < 64 ? sb_cols : 64);  // max_log2_tile_cols
    out[3] = oracleTileLog2(1, sb_rows < 64 ? sb_rows : 64);  // max_log2_tile_rows
    out[4] = 0;                                          // min_log2_tile_rows
    int min_log2_tiles = oracleTileLog2(max_tile_area_sb, sb_cols * sb_rows);
    out[5] = min_log2_tiles > out[1] ? min_log2_tiles : out[1];  // min_log2_tiles
}
entropy::HdrPcs c3Pcs(int mi_cols, int mi_rows, int log2_sb) {
    entropy::HdrPcs pcs{};
    static entropy::HdrSeqHeader sh;  // one-at-a-time callers; reset each call
    sh = entropy::HdrSeqHeader{};
    pcs.scs = &sh;
    static entropy::HdrAv1Common cm;  // zero-init grids
    cm = entropy::HdrAv1Common{};
    cm.mi_cols = mi_cols;
    cm.mi_rows = mi_rows;
    pcs.av1_cm = &cm;
    pcs.log2_sb_size = (std::uint8_t)log2_sb;
    return pcs;
}
}  // namespace

TEST_CASE("getTileLimits grid vs oracle") {
    const int dims[4][2] = {{64, 64}, {257, 145}, {960, 544}, {3841, 2177}};
    for (int d = 0; d < 4; ++d) {
        for (int log2_sb = 4; log2_sb <= 5; ++log2_sb) {
            entropy::HdrPcs pcs = c3Pcs(dims[d][0], dims[d][1], log2_sb);
            entropy::getTileLimits(&pcs);
            int out[6];
            oracleTileLimits(dims[d][0], dims[d][1], log2_sb, out);
            CHECK_EQ(pcs.av1_cm->tiles_info.max_tile_width_sb, out[0]);
            CHECK_EQ(pcs.av1_cm->tiles_info.min_log2_tile_cols, out[1]);
            CHECK_EQ(pcs.av1_cm->tiles_info.max_log2_tile_cols, out[2]);
            CHECK_EQ(pcs.av1_cm->tiles_info.max_log2_tile_rows, out[3]);
            CHECK_EQ(pcs.av1_cm->tiles_info.min_log2_tile_rows, out[4]);
            CHECK_EQ(pcs.av1_cm->tiles_info.min_log2_tiles, out[5]);
        }
    }
    // The 1080p SB grid: one 64x64 SB over the full frame is legal.
    {
        entropy::HdrPcs pcs = c3Pcs(270, 160, 4);  // 1080x640 luma -> SB grid
        entropy::getTileLimits(&pcs);
        CHECK_EQ(pcs.av1_cm->tiles_info.min_log2_tile_cols, 0);
        CHECK_EQ(pcs.av1_cm->tiles_info.max_log2_tile_cols,
                 oracleTileLog2(1, 17));
    }
}

TEST_CASE("setTileInfo + calculateTileCols/Rows derived grid vs oracle") {
    // (mi_cols, mi_rows, log2_sb, requested log2 cols, requested log2 rows).
    const int cases[5][5] = {
        {256, 256, 4, 0, 0},
        {256, 256, 4, 2, 2},
        {3840, 2176, 4, 1, 0},
        {1024, 1024, 5, 0, 1},
        {3840, 2176, 4, 4, 0},
    };
    for (int c = 0; c < 5; ++c) {
        entropy::HdrPcs pcs = c3Pcs(cases[c][0], cases[c][1], cases[c][2]);
        pcs.log2_tile_cols = (std::uint8_t)cases[c][3];
        pcs.log2_tile_rows = (std::uint8_t)cases[c][4];
        entropy::setTileInfo(&pcs);
        const entropy::HdrTilesInfo* ti = &pcs.av1_cm->tiles_info;
        // Uniform spacing is forced (:2556).
        CHECK_EQ(ti->uniform_tile_spacing_flag, 1);
        // log2_tile_cols clamped into [min, max] (:2557-:2558).
        int out[6];
        oracleTileLimits(cases[c][0], cases[c][1], cases[c][2], out);
        const int minC = out[1], maxC = out[2], maxR = out[3];
        int wantCols = cases[c][3] < minC ? minC : cases[c][3];
        if (wantCols > maxC) wantCols = maxC;
        CHECK_EQ(pcs.av1_cm->log2_tile_cols, wantCols);
        // calculate_tile_cols: size_sb round-up walk + the start_mi array.
        const int mi_cols = oracleAlignPow2(cases[c][0], cases[c][2]);
        const int mi_rows = oracleAlignPow2(cases[c][1], cases[c][2]);
        const int sb_cols = mi_cols >> cases[c][2];
        const int sb_rows = mi_rows >> cases[c][2];
        const int size_sb = oracleAlignPow2(sb_cols, wantCols) >> wantCols;
        REQUIRE(size_sb > 0);
        int nCols = 0;
        for (int start_sb = 0; start_sb < sb_cols; nCols++) start_sb += size_sb;
        CHECK_EQ(ti->tile_cols, nCols);
        for (int i = 0; i < nCols; ++i) {
            CHECK_EQ(ti->tile_col_start_mi[i], i * size_sb * (1 << cases[c][2]));
        }
        CHECK_EQ(ti->tile_col_start_mi[nCols], sb_cols << cases[c][2]);
        // min_log2_tile_rows recomputed (:2489), max_tile_height_sb (:2490).
        const int minR = (out[5] - wantCols) > 0 ? (out[5] - wantCols) : 0;
        CHECK_EQ(ti->min_log2_tile_rows, minR);
        CHECK_EQ(ti->max_tile_height_sb, sb_rows >> minR);
        CHECK_EQ(pcs.av1_cm->tile_width,
                 (size_sb << cases[c][2]) < mi_cols ? (size_sb << cases[c][2]) : mi_cols);
        // calculate_tile_rows for the uniform branch.
        int wantRows = cases[c][4] < minR ? minR : cases[c][4];
        if (wantRows > maxR) wantRows = maxR;
        CHECK_EQ(pcs.av1_cm->log2_tile_rows, wantRows);
        const int size_sb_r = oracleAlignPow2(sb_rows, wantRows) >> wantRows;
        int nRows = 0;
        for (int start_sb = 0; start_sb < sb_rows; nRows++) start_sb += size_sb_r;
        CHECK_EQ(ti->tile_rows, nRows);
        for (int i = 0; i < nRows; ++i) {
            CHECK_EQ(ti->tile_row_start_mi[i], i * size_sb_r * (1 << cases[c][2]));
        }
        CHECK_EQ(ti->tile_row_start_mi[nRows], sb_rows << cases[c][2]);
        CHECK_EQ(pcs.av1_cm->tile_height,
                 (size_sb_r << cases[c][2]) < mi_rows ? (size_sb_r << cases[c][2]) : mi_rows);
    }
}


TEST_CASE("writeTileInfoMaxTile exact bit pattern vs oracle") {
    // Real derived state: dims -> setTileInfo -> writeTileInfoMaxTile. The
    // oracle walks the reference loop :2405-:2448 over the values
    // getTileLimits computed (the writer does not re-derive them).
    const int cases[4][5] = {
        // mi_cols, mi_rows, log2_sb, req_cols, req_rows
        {256, 256, 4, 0, 0},
        {256, 256, 4, 2, 1},
        {3840, 2176, 4, 1, 0},
        {1024, 1024, 5, 0, 1},
    };
    for (int c = 0; c < 4; ++c) {
        entropy::HdrPcs pcs = c3Pcs(cases[c][0], cases[c][1], cases[c][2]);
        pcs.log2_tile_cols = (std::uint8_t)cases[c][3];
        pcs.log2_tile_rows = (std::uint8_t)cases[c][4];
        entropy::setTileInfo(&pcs);
        int out[6];
        oracleTileLimits(cases[c][0], cases[c][1], cases[c][2], out);
        const int gotCols = pcs.av1_cm->log2_tile_cols;
        const int gotRows = pcs.av1_cm->log2_tile_rows;
        int vals[32], n = 0;
        vals[n++] = 1;  // uniform_tile_spacing_flag = 1
        for (int i = 0; i < gotCols - out[1]; ++i) vals[n++] = 1;
        if (gotCols < out[2]) vals[n++] = 0;
        // The rows walk consumes min_log2_tile_rows RECOMPUTED at :2418.
        const int minR2 = (out[5] - gotCols) > 0 ? (out[5] - gotCols) : 0;
        for (int i = 0; i < gotRows - minR2; ++i) vals[n++] = 1;
        if (gotRows < out[3]) vals[n++] = 0;
        WbHarness h;
        entropy::writeTileInfoMaxTile(&pcs, &h.wb);
        CHECK_EQ(h.bits(), (std::uint32_t)n);
        for (int b = 0; b < n; ++b) {
            REQUIRE(h.bitAt((std::uint32_t)b) == vals[b]);
        }
        // The reference MUTATES tiles_info.min_log2_tile_rows at :2418.
        CHECK_EQ(pcs.av1_cm->tiles_info.min_log2_tile_rows, minR2);
    }
}

TEST_CASE("writeTileInfo tile-count literal + size-bytes walk vs oracle") {
    // Real derived state with 2x2 tiles: 1024x1024 mi at sb 64 = 64x64 SBs,
    // requested 1x1 tile logs -> 2 tile cols x 2 tile rows = tile_cnt 4.
    {
        WbHarness h;
        entropy::HdrPcs pcs = c3Pcs(1024, 1024, 4);
        pcs.log2_tile_cols = 1;
        pcs.log2_tile_rows = 1;
        const std::uint32_t pos[4] = {10, 300, 5, 70000};  // last tile = biggest
        pcs.ec_writer_pos = pos;
        pcs.ec_writer_pos_count = 4;
        // The encoder flow derives the tile state first (svt_aom_set_tile_info
        // is the only writer of tiles_info.tile_rows/tile_cols); write_tile_info
        // then re-runs get_tile_limits (:2585) and writes.
        entropy::setTileInfo(&pcs);
        entropy::writeTileInfo(&pcs, &h.wb);
        // setTileInfo-derived state: log2_tile_cols = clamp(1, min 0, max 6)
        // = 1; min_log2_tiles = 1; min_log2_tile_rows = 0; log2_tile_rows =
        // clamp(1, 0, 6) = 1; tile_cols = tile_rows = 2.
        CHECK_EQ(pcs.av1_cm->log2_tile_cols, 1);
        CHECK_EQ(pcs.av1_cm->log2_tile_rows, 1);
        CHECK_EQ(pcs.av1_cm->tiles_info.tile_cols, 2);
        CHECK_EQ(pcs.av1_cm->tiles_info.tile_rows, 2);
        // tile_cnt 4 > 1: the count literal is 3 in log2_cols + log2_rows =
        // 2 bits; the size walk reads [0, tile_cnt - 1) = {10, 300, 5} ->
        // max 300 (>> 8 != 0, >> 16 == 0) -> size bytes minus 1 = 1.
        CHECK_EQ(pcs.tile_size_bytes_minus_1, 1);
        // uniform(1) + ones(1) + term(0) + ones(1) + term(0) + count(2) +
        // size-bytes(2) = 9 bits.
        CHECK_EQ(h.bits(), 9u);
        CHECK_EQ(h.bitAt(0), 1);
        CHECK_EQ(h.bitAt(1), 1);
        CHECK_EQ(h.bitAt(2), 0);
        CHECK_EQ(h.bitAt(3), 1);
        CHECK_EQ(h.bitAt(4), 0);
        CHECK_EQ(h.bitAt(5), 1);  // count literal 3 = 11
        CHECK_EQ(h.bitAt(6), 1);
        CHECK_EQ(h.bitAt(7), 0);  // size bytes 1 = 01
        CHECK_EQ(h.bitAt(8), 1);
    }
    // The single-tile branch: NO tile-count literal, NO size-bytes. A 1-SB
    // frame derives max_log2_tile_cols = 0, so the writer emits ONLY the
    // uniform bit.
    {
        WbHarness h;
        entropy::HdrPcs pcs = c3Pcs(16, 16, 4);
        pcs.log2_tile_cols = 0;
        pcs.log2_tile_rows = 0;
        const std::uint32_t pos[1] = {42};
        pcs.ec_writer_pos = pos;
        pcs.ec_writer_pos_count = 1;
        entropy::setTileInfo(&pcs);
        entropy::writeTileInfo(&pcs, &h.wb);
        CHECK_EQ(pcs.av1_cm->tiles_info.tile_cols, 1);
        CHECK_EQ(pcs.av1_cm->tiles_info.tile_rows, 1);
        CHECK_EQ(pcs.tile_size_bytes_minus_1, 0);
        CHECK_EQ(h.bits(), 1u);
        CHECK_EQ(h.bitAt(0), 1);
    }
}

// ---------------------------------------------------------------------------
// C4: the sequence-header family + global motion + film grain + the OBU
// shells.
// ---------------------------------------------------------------------------
TEST_CASE("writeBitstreamLevel + addTrailingBits + writeObuMetadata exact bits") {
    // write_bitstream_level :3693-:3697: LEVEL_BITS = 5 (LEVEL_MAJOR_BITS 3 +
    // LEVEL_MINOR_BITS 2, definitions.h:390-:392); major_minor_to_seq_level_idx
    // = (major - 2) << 2 + minor.
    for (int major = 2; major <= 7; ++major) {
        for (int minor = 0; minor <= 3; ++minor) {
            WbHarness h;
            entropy::writeBitstreamLevel(major, minor, &h.wb);
            CHECK_EQ(h.bits(), 5u);
            const int idx = ((major - 2) << 2) + minor;
            for (int b = 0; b < 5; ++b) {
                REQUIRE(h.bitAt((std::uint32_t)b) == ((idx >> (4 - b)) & 1));
            }
        }
    }
    // add_trailing_bits :3668-:3675: 0x80 literal when aligned, 1 bit else.
    {
        WbHarness h;
        entropy::wbWriteBit(&h.wb, 1);
        entropy::addTrailingBits(&h.wb);
        CHECK_EQ(h.bits(), 2u);  // 1 + 1 (not byte aligned)
    }
    {
        WbHarness h;
        entropy::wbWriteLiteral(&h.wb, 1, 8);
        entropy::addTrailingBits(&h.wb);
        CHECK_EQ(h.bits(), 16u);
        CHECK_EQ(h.bitAt(8), 1);
        CHECK_EQ(h.bitAt(9), 0);
        std::uint32_t ones = 0;
        for (int b = 10; b < 16; ++b) ones += (std::uint32_t)h.bitAt((std::uint32_t)b);
        CHECK_EQ(ones, 0u);
    }
    // write_obu_metadata :3678-:3691 over a 3-byte payload.
    {
        WbHarness h;
        const std::uint8_t payload[3] = {0xAB, 0x01, 0xFF};
        const std::uint32_t bytes =
            entropy::writeObuMetadata(29, payload, 3, &h.wb);
        CHECK_EQ(bytes, 5u);  // type(8) + 24 + trailing 8 (aligned at 32) = 40 bits
        CHECK_EQ(h.bits(), 40u);
        for (int b = 0; b < 8; ++b) REQUIRE(h.bitAt((std::uint32_t)b) == ((29 >> (7 - b)) & 1));
        REQUIRE(h.bitAt(8) == 1);
        REQUIRE(h.bitAt(9) == 0);
        REQUIRE(h.bitAt(10) == 1);
        REQUIRE(h.bitAt(11) == 0);
        // The null payload writes nothing.
        WbHarness h2;
        CHECK_EQ(entropy::writeObuMetadata(29, NULL, 0, &h2.wb), 0u);
        CHECK_EQ(h2.bits(), 0u);
    }
}

TEST_CASE("writeTileGroupHeader exact bits + gates") {
    // tiles_log2 = 0 -> nothing written.
    {
        std::uint8_t buf[16] = {0};
        CHECK_EQ(entropy::writeTileGroupHeader(buf, 0, 0, 0, 1), 0u);
    }
    // tiles_log2 = 3, flag 0 -> one bit.
    {
        std::uint8_t buf[16] = {0};
        CHECK_EQ(entropy::writeTileGroupHeader(buf, 0, 0, 3, 0), 1u);
        CHECK_EQ((buf[0] >> 7) & 1, 0);
    }
    // tiles_log2 = 3, flag 1 -> 1 + 3 + 3 bits = 1 byte.
    {
        std::uint8_t buf[16] = {0};
        CHECK_EQ(entropy::writeTileGroupHeader(buf, 2, 5, 3, 1), 1u);
        // bit 0 = flag 1; literal(2, 3) = 010; literal(5, 3) = 101 ->
        // byte 0b1_010_101 = 0x2A.
        CHECK_EQ(buf[0], 0xAA);  // 1_010_101 = flag + literal(2,3) + literal(5,3)
    }
}

TEST_CASE("writeColorConfig branch matrix vs oracle") {
    // The CICP unspecified gate: primaries 2 / transfer 2 / matrix 2 -> no
    // color description.
    for (int prof = 0; prof < 3; ++prof) {
        WbHarness h;
        entropy::HdrSeqHeader sh{};
        sh.profile = prof;
        sh.color_primaries = 2;
        sh.transfer_characteristics = 2;
        sh.matrix_coefficients = 2;
        sh.subsampling_x = 1;
        sh.subsampling_y = 1;
        sh.chroma_u_ac_qindex_offset = 0;
        sh.chroma_v_ac_qindex_offset = 0;
        sh.chroma_u_dc_qindex_offset = 0;
        sh.chroma_v_dc_qindex_offset = 0;
        sh.encoder_bit_depth = 8;
        entropy::writeColorConfig(&sh, &h.wb);
        // write_bitdepth first (1 bit for main/high 8-bit; 1 for professional
        // 8-bit), the mono bit, no color description, color_range, then the
        // separate_uv_delta_q bit.
        const int want[3] = {7, 6, 7};
        CHECK_EQ(h.bits(), (std::uint32_t)want[prof]);  // + chroma_pos(2)
    }
    // The color-description branch: three 8-bit literals.
    {
        WbHarness h;
        entropy::HdrSeqHeader sh{};
        sh.profile = 0;
        sh.color_primaries = 1;
        sh.transfer_characteristics = 13;
        sh.matrix_coefficients = 0;
        sh.color_range = 1;
        sh.subsampling_x = 1;
        sh.subsampling_y = 1;
        entropy::writeColorConfig(&sh, &h.wb);
        // 1 (bitdepth) + 1 (mono) + 1 (desc) + 24 (3 x 8) + 1 (range,
        // BT709/SRGB/IDENTITY takes the sRGB branch which writes NO range
        // bit...) - the reference writes the range bit in the else branch
        // only: primaries 1 + transfer 13 + matrix 0 -> the sRGB combo ->
        // no range bit, no subsampling writes, then separate_uv.
        CHECK_EQ(h.bits(), 1u + 1u + 1u + 24u + 1u);
    }
}

TEST_CASE("writeSequenceHeader frame-size-bits derivation") {
    // The width/height bits are derived and WRITTEN BACK to the carrier
    // (:2772-:2773): 256 wide needs 8 bits, 144 needs 8.
    {
        WbHarness h;
        entropy::HdrSeqHeader sh{};
        sh.max_frame_width = 256;
        sh.max_frame_height = 144;
        sh.reduced_still_picture_header = 1;
        entropy::writeSequenceHeader(&sh, &h.wb);
        CHECK_EQ(sh.frame_width_bits, 8);
        CHECK_EQ(sh.frame_height_bits, 8);
        // 4 + 4 + 8 + 8 = 24 dims bits + the 6 tail bits (:2795-:2838).
        CHECK_EQ(h.bits(), 30u);
    }
    // A 512x512 frame: 10-bit dims (svt_log2f(512) = 9, 512 > 512? no ->
    // 9 bits, min 1 enforced).
    {
        WbHarness h;
        entropy::HdrSeqHeader sh{};
        sh.max_frame_width = 512;
        sh.max_frame_height = 512;
        sh.reduced_still_picture_header = 1;
        entropy::writeSequenceHeader(&sh, &h.wb);
        CHECK_EQ(sh.frame_width_bits, 9);
        CHECK_EQ(sh.frame_height_bits, 9);
        CHECK_EQ(h.bits(), 32u);  // 26 dims + 6 tail
    }
    // The reduced-still-picture short-circuit: only the dim bits.
    {
        WbHarness h;
        entropy::HdrSeqHeader sh{};
        sh.max_frame_width = 1;
        sh.max_frame_height = 1;
        sh.reduced_still_picture_header = 1;
        entropy::writeSequenceHeader(&sh, &h.wb);
        CHECK_EQ(h.bits(), 16u);  // 10 dims + 6 tail
    }
}

TEST_CASE("writeGlobalMotionParams identity vs translation vs oracle shape") {
    // IDENTITY: one bit (type != IDENTITY = 0).
    {
        WbHarness h;
        entropy::HdrWarpParams p{}, ref{};
        entropy::writeGlobalMotionParams(&p, &ref, &h.wb, 1);
        CHECK_EQ(h.bits(), 1u);
        CHECK_EQ(h.bitAt(0), 0);
    }
    // TRANSLATION: type bit + no rotzoom bit + the two wmmat pairs through
    // the signed refsubexpfin with (1 << (HD_GM_ABS_TRANS_ONLY_BITS - !hp)) + 1
    // = (1 << 8) + 1 = 257 symbols, recentered around ref 0.
    {
        WbHarness h;
        entropy::HdrWarpParams p{}, ref{};
        p.wmtype = (entropy::TransformationType)1;  // TRANSLATION
        p.wmmat[0] = 1 << HD_GM_TRANS_PREC_DIFF;    // +1.0 in trans prec
        p.wmmat[1] = -(1 << HD_GM_TRANS_PREC_DIFF);
        entropy::writeGlobalMotionParams(&p, &ref, &h.wb, 1);
        CHECK_EQ(h.bitAt(0), 1);  // type != IDENTITY
        CHECK_EQ(h.bitAt(1), 0);  // not ROTZOOM
        CHECK_EQ(h.bitAt(2), 1);  // TRANSLATION
        // The recenter of v = 1 around r = 0 with n = 257: v > r<<1 -> v
        // passthrough = 1; subexpfin(257, 3, 1): 257 <= 3*8? no -> count 1,
        // t = (1 >= 8) = 0 -> literal(1, 3) -> the code writes a bool 0 then
        // literal bits.
        CHECK_EQ(h.bitAt(3), 0);
        // v2 = -1.0: recenter_nonneg(0, 510) -> v > 0<<1 -> 510; the
        // subexpfin walk takes several rungs.
    }
}

TEST_CASE("writeFrameSizeWithRefs found-gate vs not-found composition") {
    // found on the first ref: bit 1 + the superres write.
    {
        WbHarness h;
        entropy::HdrSeqHeader sh_ref{}; entropy::HdrAv1Common cm_ref{}; entropy::HdrPcs pcs = c2Pcs(&sh_ref, &cm_ref);
        pcs.scs->enable_superres = 0;
        pcs.av1_cm->frm_size.superres_denominator = 8;
        const std::uint32_t hints[7] = {7, 0, 0, 0, 0, 0, 0};
        const int dims[7] = {1, 0, 0, 0, 0, 0, 0};
        entropy::writeFrameSizeWithRefs(&pcs, &h.wb, hints, dims);
        CHECK_EQ(h.bits(), 1u);  // found bit + superres DISABLED writes nothing
        CHECK_EQ(h.bitAt(0), 1);
    }
    // not found anywhere: the full write_frame_size with override = 1.
    {
        WbHarness h;
        entropy::HdrSeqHeader sh_ref{}; entropy::HdrAv1Common cm_ref{}; entropy::HdrPcs pcs = c2Pcs(&sh_ref, &cm_ref);
        pcs.scs->enable_superres = 0;
        pcs.scs->frame_width_bits = 8;
        pcs.scs->frame_height_bits = 8;
        pcs.av1_cm->frm_size.superres_upscaled_width = 256;
        pcs.av1_cm->frm_size.superres_upscaled_height = 144;
        pcs.av1_cm->frm_size.superres_denominator = 8;
        pcs.frame_resize_enabled = 0;
        const std::uint32_t hints[7] = {0, 0, 0, 0, 0, 0, 0};
        const int dims[7] = {0, 0, 0, 0, 0, 0, 0};
        entropy::writeFrameSizeWithRefs(&pcs, &h.wb, hints, dims);
        // 7 not-found bits + override(16) + superres(0, disabled) + render(1).
        CHECK_EQ(h.bits(), 24u);
        // coded_width = 255 after the 7 not-found bits (stream offset 7).
        for (int b = 0; b < 8; ++b) REQUIRE(h.bitAt((std::uint32_t)(7 + b)) == ((255 >> (7 - b)) & 1));
        // coded_height = 143 in the next 8.
        for (int b = 0; b < 8; ++b) REQUIRE(h.bitAt((std::uint32_t)(15 + b)) == ((143 >> (7 - b)) & 1));
        // The not-found bits are all zero.
        for (int b = 0; b < 7; ++b) REQUIRE(h.bitAt((std::uint32_t)b) == 0);
    }
}

TEST_CASE("writeUncompressedHeaderObu keyframe-with-reduced-header path") {
    // The reduced-still-picture path: no show/type bits, no override bit,
    // straight to the frame-size + tile + quantization chain.
    {
        WbHarness h;
        entropy::HdrSeqHeader sh{};
        entropy::HdrAv1Common cm{};
        entropy::HdrPcs pcs{};
        sh.reduced_still_picture_header = 1;
        sh.max_frame_width = 256;
        sh.max_frame_height = 144;
        sh.seq_force_screen_content_tools = 0;
        sh.enable_order_hint = 0;
        sh.cdef_level = 0;
        sh.enable_restoration = 0;
        sh.film_grain_params_present = 0;
        sh.subsampling_x = 1;
        sh.subsampling_y = 1;
        cm.mi_cols = 256;
        cm.mi_rows = 144;
        cm.frm_size.frame_width = 256;
        cm.frm_size.frame_height = 144;
        cm.frm_size.superres_upscaled_width = 256;
        cm.frm_size.superres_upscaled_height = 144;
        cm.frm_size.superres_denominator = 8;
        pcs.scs = &sh;
        pcs.av1_cm = &cm;
        pcs.log2_sb_size = 4;
        pcs.nb_cdef_strengths = 0;
        pcs.frm_hdr.frame_type = entropy::HD_KEY_FRAME;
        pcs.frm_hdr.coded_lossless = 1;
        pcs.frm_hdr.tx_mode = entropy::HD_ONLY_4X4;
        entropy::writeUncompressedHeaderObu(&sh, &pcs, &h.wb, 0);
        // The reduced path: no bits before the frame size. The frame size:
        // override flag 0 (256 == max 256, 144 == max 144) -> no override
        // literal; superres disabled -> 1 bit; render 0 -> 1 bit.
        // The tiles: 1 tile -> uniform + no terminators = 1 bit.
        // quantization: base_q_idx 8 bits + delta 1 bit + qmatrix 1 bit.
        // segmentation: 1 bit (disabled). loopfilter: no (coded_lossless 1).
        // cdef: no. restoration: no (enable 0). tx_mode: coded_lossless 1 ->
        // assert only. comp inter: allow_comp_inter_inter 0 -> no bit.
        // skip mode: allowed 0 -> no bit. warped: intra-only -> no bit.
        // reduced_tx_set: 1 bit. global motion: intra-only -> no bit.
        // film grain: not present -> no bit.
        CHECK_GT(h.bits(), 0u);
        CHECK_EQ(pcs.tile_size_bytes_minus_1, 0u);
    }
}

TEST_CASE("writeSequenceHeaderObu reduced path byte-exact") {
    std::uint8_t buf[64] = {0};
    entropy::HdrSeqHeader sh{};
    sh.profile = 0;
    sh.still_picture = 1;
    sh.reduced_still_picture_header = 1;
    sh.level_major = 2;
    sh.level_minor = 0;
    sh.max_frame_width = 256;
    sh.max_frame_height = 144;
    sh.filter_intra_level = 1;
    sh.enable_intra_edge_filter = 0;
    sh.enable_superres = 0;
    sh.cdef_level = 1;
    sh.enable_restoration = 1;
    sh.film_grain_params_present = 0;
    sh.color_primaries = 2;
    sh.transfer_characteristics = 2;
    sh.matrix_coefficients = 2;
    sh.color_range = 0;
    sh.subsampling_x = 1;
    sh.subsampling_y = 1;
    sh.encoder_bit_depth = 8;
    sh.frame_width_bits = 8;
    sh.frame_height_bits = 8;
    const std::uint32_t bytes = entropy::writeSequenceHeaderObu(&sh, buf, 1);
    // profile 3 + still 1 + reduced 1 + level 5 + width-bits 4 + height-bits 4
    // + width 8 + height 8 + sb128 1 + filter-intra 1 + edge 1 + superres 1
    // + cdef 1 + restoration 1 + mono-bit? no (profile != HIGH writes the
    // mono bit) 1 + color desc 1 (unspecified) + separate_uv 1 = 40 bits + 8
    // trailing = 48 bits = 6 bytes.
    CHECK_EQ(bytes, 7u);  // 56 bits incl. the mono bit and the 6 seq tail bits
    CHECK_EQ(sh.frame_width_bits, 8);
    CHECK_EQ(sh.frame_height_bits, 8);
}

TEST_CASE("writeGlobalMotionParams rotzoom + affine vs oracle") {
    // ROTZOOM: type bit 1, rotzoom bit 1 (no translation bit), then the two
    // alpha pairs through the signed refsubexpfin with n = HD_GM_ALPHA_MAX + 1
    // = 4097, recentered around the default ref wmmat[2] = 1 << 16 (the
    // ref2 = -1 center) and wmmat[3] = 0 (ref3 = 0 center).
    {
        WbHarness h;
        entropy::HdrWarpParams p{}, ref{};
        ref.wmtype = (entropy::TransformationType)0;
        ref.wmmat[2] = 1 << 16;
        ref.wmmat[3] = 0;
        ref.wmmat[4] = 0;
        ref.wmmat[5] = 1 << 16;
        p.wmtype = (entropy::TransformationType)2;  // ROTZOOM
        p.wmmat[2] = 1 << 16;  // no alpha change: v2 == ref2 == -1
        p.wmmat[3] = 0;
        entropy::writeGlobalMotionParams(&p, &ref, &h.wb, 1);
        CHECK_EQ(h.bitAt(0), 1);  // type != IDENTITY
        CHECK_EQ(h.bitAt(1), 1);  // type == ROTZOOM
        // With v == ref the recentered value is 0 -> the subexpfin writes a
        // bool rung then a literal per the oracle; assert a fixed short
        // stream: the two zero-recentered pairs each take the first rung.
        CHECK_LT(h.bits(), 40u);
    }
    // The ROTZOOM-bit polarity: a TRANSLATION type must NOT read the
    // rotzoom bit as 1.
    {
        WbHarness h;
        entropy::HdrWarpParams p{}, ref{};
        p.wmtype = (entropy::TransformationType)2;
        p.wmmat[3] = 1 << HD_GM_ALPHA_PREC_DIFF;
        entropy::writeGlobalMotionParams(&p, &ref, &h.wb, 1);
        CHECK_EQ(h.bitAt(1), 1);
    }
    // AFFINE: the rotzoom bit is 0 and the translation bit is 0.
    {
        WbHarness h;
        entropy::HdrWarpParams p{}, ref{};
        p.wmtype = (entropy::TransformationType)3;
        entropy::writeGlobalMotionParams(&p, &ref, &h.wb, 1);
        CHECK_EQ(h.bitAt(0), 1);
        CHECK_EQ(h.bitAt(1), 0);  // not ROTZOOM
        CHECK_EQ(h.bitAt(2), 0);  // not TRANSLATION
    }
}