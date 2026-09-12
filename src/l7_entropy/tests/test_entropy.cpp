#include <doctest.h>

#include <cstring>

#include "entropy.h"

// EC1 tolerance note: integer-only port, bit-exact vs the SVT C - every
// expected value below is transcribed from the EC0 gate lines in
// tools/golden_gen/expected_primitives.txt (committed generator), never
// hand-traced.

TEST_CASE("odEcEncReset initial state") {
    // svt_od_ec_enc_reset (bitstream_unit.c:185-197): rng = 0x8000,
    // cnt = -9, low = 0, error = 0.
    entropy::OdEcEnc enc{};
    entropy::odEcEncReset(&enc);
    CHECK(enc.rng == 0x8000);
    CHECK(enc.cnt == -9);
    CHECK(enc.low == 0);
    CHECK(enc.error == 0);
}

TEST_CASE("odEcEncodeBoolEqQ15 12-bit sequence matches gate bytes") {
    // Gate: ec_enc_bool_eq 2 b2 e8 (fixed bits 1,0,1,1,0,0,1,0,1,1,1,0,
    // svt_od_ec_encode_bool_eq_q15 bitstream_unit.c:232-247 through
    // svt_od_ec_enc_done bitstream_unit.c:309-343).
    entropy::OdEcEnc enc{};
    unsigned char buf[64] = {0};
    enc.buf = buf;
    entropy::odEcEncReset(&enc);
    const int bits[12] = {1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0};
    for (int i = 0; i < 12; ++i) entropy::odEcEncodeBoolEqQ15(&enc, bits[i]);
    std::uint32_t n = 0;
    CHECK(entropy::odEcEncDone(&enc, &n) != nullptr);
    REQUIRE(n == 2);
    CHECK((unsigned)buf[0] == 0xb2);
    CHECK((unsigned)buf[1] == 0xe8);
}

TEST_CASE("odEcEncodeBoolQ15 f=16384 and f=8192 match gate bytes") {
    // Gate: ec_enc_bool_f16384 2 69 40 (8 bits {0,1,1,0,1,0,0,1},
    // svt_od_ec_encode_bool_q15 bitstream_unit.c:252-269) and
    // ec_enc_bool_f8192 2 bb e0 (same bits, f = 8192).
    entropy::OdEcEnc enc{};
    unsigned char buf[64] = {0};
    enc.buf = buf;
    entropy::odEcEncReset(&enc);
    const int bits[8] = {0, 1, 1, 0, 1, 0, 0, 1};
    for (int i = 0; i < 8; ++i) entropy::odEcEncodeBoolQ15(&enc, bits[i], 16384);
    std::uint32_t n = 0;
    entropy::odEcEncDone(&enc, &n);
    REQUIRE(n == 2);
    CHECK((unsigned)buf[0] == 0x69);
    CHECK((unsigned)buf[1] == 0x40);
    enc.buf = buf;
    entropy::odEcEncReset(&enc);
    for (int i = 0; i < 8; ++i) entropy::odEcEncodeBoolQ15(&enc, bits[i], 8192);
    entropy::odEcEncDone(&enc, &n);
    REQUIRE(n == 2);
    CHECK((unsigned)buf[0] == 0xbb);
    CHECK((unsigned)buf[1] == 0xe0);
}

TEST_CASE("odEcEncodeCdfQ15 13-symbol sequence matches gate bytes") {
    // Gate: ec_enc_cdf13 5 23 97 49 00 d4 (10 symbols {0,5,12,3,5,5,1,0,7,9}
    // through the fixture icdf13, svt_od_ec_encode_cdf_q15
    // bitstream_unit.c:279-301). Same fixture icdf as the EC0 driver
    // (main_primitives.c): monotonically non-increasing, icdf[12] = 0.
    static const std::uint16_t icdf13[13] = {26700, 22000, 18000, 14000, 10500, 8000, 6000, 4500, 3200, 2200, 1400, 700, 0};
    entropy::OdEcEnc enc{};
    unsigned char buf[64] = {0};
    enc.buf = buf;
    entropy::odEcEncReset(&enc);
    const int syms[10] = {0, 5, 12, 3, 5, 5, 1, 0, 7, 9};
    for (int i = 0; i < 10; ++i) entropy::odEcEncodeCdfQ15(&enc, syms[i], icdf13, 13);
    std::uint32_t n = 0;
    entropy::odEcEncDone(&enc, &n);
    REQUIRE(n == 5);
    const unsigned char expected[5] = {0x23, 0x97, 0x49, 0x00, 0xd4};
    for (int i = 0; i < 5; ++i) CHECK(buf[i] == expected[i]);
}

TEST_CASE("odEcEncTell and odEcEncTellFrac match gate values") {
    // Gate: ec_tell 78 624 - encoder tell/tell_frac at the end of the cdf13
    // run (svt_od_ec_enc_tell bitstream_unit.c:354-358,
    // svt_od_ec_enc_tell_frac :406-408 -> svt_od_ec_tell_frac :369-395;
    // frac = tell<<3 with l=0 because rng stays < 65536).
    static const std::uint16_t icdf13[13] = {26700, 22000, 18000, 14000, 10500, 8000, 6000, 4500, 3200, 2200, 1400, 700, 0};
    entropy::OdEcEnc enc{};
    unsigned char buf[64] = {0};
    enc.buf = buf;
    entropy::odEcEncReset(&enc);
    const int syms[10] = {0, 5, 12, 3, 5, 5, 1, 0, 7, 9};
    for (int i = 0; i < 10; ++i) entropy::odEcEncodeCdfQ15(&enc, syms[i], icdf13, 13);
    std::uint32_t n = 0;
    entropy::odEcEncDone(&enc, &n);
    CHECK(entropy::odEcEncTell(&enc) == 78);
    CHECK(entropy::odEcEncTellFrac(&enc) == 624);
}

TEST_CASE("odEcDecodeBoolQ15 round-trips the bool_eq gate bytes") {
    // Gate bytes ec_enc_bool_eq 2 b2 e8 decoded back through
    // od_ec_decode_bool_q15 (entdec.c:158-182) at f = 16384 = the
    // aom_read_bit probability (bitreader.h:71-75).
    unsigned char data[2] = {0xb2, 0xe8};
    entropy::OdEcDec dec;
    entropy::odEcDecInit(&dec, data, 2);
    const int bits[12] = {1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0};
    for (int i = 0; i < 12; ++i) {
        CHECK(entropy::odEcDecodeBoolQ15(&dec, 16384) == bits[i]);
    }
}

TEST_CASE("odEcDecodeCdfQ15 round-trips the cdf13 gate bytes") {
    // Gate bytes ec_enc_cdf13 5 23 97 49 00 d4 decoded back through
    // od_ec_decode_cdf_q15 (entdec.c:193-223) with the same fixture icdf13.
    static const std::uint16_t icdf13[13] = {26700, 22000, 18000, 14000, 10500, 8000, 6000, 4500, 3200, 2200, 1400, 700, 0};
    unsigned char data[5] = {0x23, 0x97, 0x49, 0x00, 0xd4};
    entropy::OdEcDec dec;
    entropy::odEcDecInit(&dec, data, 5);
    const int syms[10] = {0, 5, 12, 3, 5, 5, 1, 0, 7, 9};
    for (int i = 0; i < 10; ++i) {
        CHECK(entropy::odEcDecodeCdfQ15(&dec, icdf13, 13) == syms[i]);
    }
}

TEST_CASE("encoder and decoder are mutually consistent on a mixed sequence") {
    // Integration round-trip (no new primitive): every expected byte/symbol
    // side is gate-verified bit-exact (EC0 lines); this composes both sides
    // and asserts f(g(x)) == x, including the f extremes 128 and 32767
    // (f < 32768 precondition, bitstream_unit.c:253).
    entropy::OdEcEnc enc{};
    unsigned char buf[128] = {0};
    enc.buf = buf;
    entropy::odEcEncReset(&enc);
    static const std::uint16_t icdf13[13] = {26700, 22000, 18000, 14000, 10500, 8000, 6000, 4500, 3200, 2200, 1400, 700, 0};
    const int plan[24][3] = {
        {0, 0, 0}, {1, 16384, 0}, {2, 0, 1}, {3, 8192, 0}, {4, 0, 0},
        {5, 32767, 1}, {6, 128, 0}, {7, 16384, 1}, {8, 0, 0}, {9, 4096, 1},
        {10, 0, 0}, {11, 16384, 1}, {12, 0, 0}, {5, 8192, 0}, {2, 16384, 1},
        {0, 0, 0}, {12, 16384, 0}, {1, 2048, 1}, {3, 16384, 0}, {0, 0, 0},
        {7, 16384, 1}, {11, 16384, 0}, {4, 16384, 1}, {9, 0, 0},
    };
    for (int i = 0; i < 24; ++i) {
        if (plan[i][2] == 0) {
            // plain bit at f = 16384 default or the plan's f
            entropy::odEcEncodeBoolQ15(&enc, plan[i][0] & 1, (std::uint32_t)plan[i][1]);
        } else {
            entropy::odEcEncodeCdfQ15(&enc, plan[i][0] % 13, icdf13, 13);
        }
    }
    std::uint32_t n = 0;
    entropy::odEcEncDone(&enc, &n);
    REQUIRE(n > 0);
    REQUIRE(n < 128);
    entropy::OdEcDec dec;
    entropy::odEcDecInit(&dec, buf, n);
    for (int i = 0; i < 24; ++i) {
        if (plan[i][2] == 0) {
            CHECK(entropy::odEcDecodeBoolQ15(&dec, (unsigned)plan[i][1]) == (plan[i][0] & 1));
        } else {
            CHECK(entropy::odEcDecodeCdfQ15(&dec, icdf13, 13) == plan[i][0] % 13);
        }
    }
}

TEST_CASE("updateCdf binary sequence matches gate") {
    // Gate: ecupd_cdf2_seq - 40 val=0 updates on {16384, 0} + counter
    // (update_cdf, cabac_context_model.h:76-105). The snapshot list walks
    // the counter through the rate transitions at count 16 (rate 5) and
    // count 32 (rate 6).
    static const std::uint16_t expected[40] = {15360, 14400, 13500, 12657, 11866, 11125, 10430, 9779, 9168, 8595, 8058, 7555, 7083, 6641, 6226, 5837, 5655, 5479, 5308, 5143, 4983, 4828, 4678, 4532, 4391, 4254, 4122, 3994, 3870, 3750, 3633, 3520, 3465, 3411, 3358, 3306, 3255, 3205, 3155, 3106};
    std::uint16_t cdf[3] = {16384, 0, 0};
    for (int i = 0; i < 40; ++i) {
        entropy::updateCdf(cdf, 0, 2);
        CHECK(cdf[0] == expected[i]);
    }
    CHECK(cdf[2] == 32);  // counter stops at 32 (count < 32 precondition)
}

TEST_CASE("updateCdf 13-symbol full dump matches gate") {
    // Gate: ecupd_cdf13_full - the EC0 10-symbol sequence, full 14-word
    // dump (12 icdf values + terminator + counter=10).
    static const std::uint16_t expected[14] = {26618, 22265, 19354, 15597, 13048, 8660, 7205, 5123, 4177, 2425, 1842, 1333, 0, 10};
    std::uint16_t cdf[14] = {26700, 22000, 18000, 14000, 10500, 8000, 6000, 4500, 3200, 2200, 1400, 700, 0, 0};
    const int syms[10] = {0, 5, 12, 3, 5, 5, 1, 0, 7, 9};
    for (int i = 0; i < 10; ++i) entropy::updateCdf(cdf, syms[i], 13);
    for (int i = 0; i < 14; ++i) CHECK(cdf[i] == expected[i]);
}

TEST_CASE("odEcDecTell matches gate after cdf13 decode") {
    // Gate: ec_dec_tell 6 - od_ec_dec_tell (entdec.c:231-237) after
    // decoding the cdf13 gate bytes.
    unsigned char data[5] = {0x23, 0x97, 0x49, 0x00, 0xd4};
    entropy::OdEcDec dec;
    entropy::odEcDecInit(&dec, data, 5);
    static const std::uint16_t icdf13[13] = {26700, 22000, 18000, 14000, 10500, 8000, 6000, 4500, 3200, 2200, 1400, 700, 0};
    for (int i = 0; i < 10; ++i) entropy::odEcDecodeCdfQ15(&dec, icdf13, 13);
    CHECK(entropy::odEcDecTell(&dec) == 6);
}