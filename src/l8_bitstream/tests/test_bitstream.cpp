#include <doctest.h>

#include <cstdio>
#include <cstring>

#include "bitstream.h"

// BSF2 tolerance note: integer-only port, bit-exact vs the SVT C - every
// expected value below is transcribed from the BSF2 gate lines in
// tools/golden_gen/expected_primitives.txt (committed generator), never
// hand-traced.

TEST_CASE("OBU header bytes per type match gate") {
    // Gate: obu_hdr 1 0a 1 12 1 32 - write_obu_header (entropy_coding.c:
    // 3639-3654): forbidden 0 / type 4b / ext 0 / has_size 1 hardcoded
    // :3646 / reserved 0. SPS(1) -> 0x0a, TD(2) -> 0x12, FRAME(6) -> 0x32.
    unsigned char buf[8] = {0};
    CHECK(bitstream::writeObuHeader(bitstream::OBU_SEQUENCE_HEADER, 0, buf) == 1);
    CHECK((unsigned)buf[0] == 0x0a);
    CHECK(bitstream::writeObuHeader(bitstream::OBU_TEMPORAL_DELIMITER, 0, buf) == 1);
    CHECK((unsigned)buf[0] == 0x12);
    CHECK(bitstream::writeObuHeader(bitstream::OBU_FRAME, 0, buf) == 1);
    CHECK((unsigned)buf[0] == 0x32);
}

TEST_CASE("temporal delimiter is exactly 2 bytes 12 00") {
    // Gate: td_bytes 2 12 00 a7 - svt_aom_encode_td_av1 (entropy_coding.c:
    // 3953-3960) = write_obu_header(OBU_TEMPORAL_DELIMITER) + write_uleb_
    // obu_size(size 1, payload 0); the 0xa7 poison tail proves the 2-byte
    // write extent.
    unsigned char buf[8];
    memset(buf, 0xa7, sizeof(buf));
    CHECK(bitstream::encodeTdAv1(buf) == 0);  // EB_ErrorNone (API/EbSvtAv1.h:123)
    REQUIRE(buf[0] == 0x12);
    CHECK(buf[1] == 0x00);
    CHECK((unsigned)buf[2] == 0xa7);
    CHECK((unsigned)buf[3] == 0xa7);
}

TEST_CASE("uleb boundary classes match gate") {
    // Gate: uleb 0:1:00 127:1:7f 128:2:8001 255:2:ff01 16383:2:ff7f
    // 16384:3:808001 - svt_aom_uleb_size_in_bytes (entropy_coding.c:
    // 1313-1319) + svt_aom_uleb_encode (:1321-1341).
    static const std::uint64_t vals[6] = {0, 127, 128, 255, 16383, 16384};
    static const unsigned wantSize[6] = {1, 1, 2, 2, 2, 3};
    static const std::uint8_t wantBytes[6][3] = {{0x00, 0, 0}, {0x7f, 0, 0}, {0x80, 0x01, 0}, {0xff, 0x01, 0}, {0xff, 0x7f, 0}, {0x80, 0x80, 0x01}};
    for (int i = 0; i < 6; ++i) {
        CHECK(bitstream::ulebSizeInBytes(vals[i]) == wantSize[i]);
        std::uint8_t out[8] = {0};
        std::size_t sz = 0;
        CHECK(bitstream::ulebEncode(vals[i], sizeof(out), out, &sz) == 0);  // SVT_AOM_CODEC_OK
        CHECK(sz == wantSize[i]);
        for (std::size_t j = 0; j < sz; ++j) CHECK(out[j] == wantBytes[i][j]);
    }
}

TEST_CASE("literal/bit packing matches gate") {
    // Gate: wblit 3 0 aa bf 60 00 - 1 bit(1) + 3-bit literal(2) + 8-bit
    // literal(0xAB) + write_inv_signed_literal(-5, 6 -> 7 bits written,
    // :1380-1382) + 1 bit(0) = 20 bits -> 3 bytes (wb_bytes_written
    // :1347-1349 ceil), not byte-aligned (wb_is_byte_aligned :1343-1345),
    // byte 3 untouched.
    unsigned char buf[16];
    memset(buf, 0, sizeof(buf));
    bitstream::AomWriteBitBuffer wb = {buf, 0};
    bitstream::wbWriteBit(&wb, 1);
    bitstream::wbWriteLiteral(&wb, 0x2, 3);
    bitstream::wbWriteLiteral(&wb, 0xAB, 8);
    bitstream::wbWriteInvSignedLiteral(&wb, -5, 6);
    bitstream::wbWriteBit(&wb, 0);
    CHECK(bitstream::wbBytesWritten(&wb) == 3);
    CHECK(bitstream::wbIsByteAligned(&wb) == 0);
    CHECK((unsigned)buf[0] == 0xaa);
    CHECK((unsigned)buf[1] == 0xbf);
    CHECK((unsigned)buf[2] == 0x60);
    CHECK((unsigned)buf[3] == 0x00);

    // Gate: wbalign 3 1 - pad to byte alignment with zero bits (the
    // byte_alignment() pattern): 24 bits -> 3 whole bytes, aligned.
    while (!bitstream::wbIsByteAligned(&wb)) bitstream::wbWriteBit(&wb, 0);
    CHECK(bitstream::wbBytesWritten(&wb) == 3);
    CHECK(bitstream::wbIsByteAligned(&wb) == 1);
}
