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