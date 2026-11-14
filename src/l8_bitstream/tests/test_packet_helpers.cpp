#include <doctest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "bitstream.h"
#include "packet_helpers.h"

// PK1 - the A7P packetization pure-surface tests. Independent oracles +
// exhaustive enumerations + poison-filled carriers. The parks list (the
// named walls this file does NOT test, per packet_helpers.h):
//   - packetization_reorder_queue.c:15-26 (EB_NEW/EB_DELETE entry ctor/dctor
//     shells)
//   - the packetization kernel driver packetization_process.c:459-959
//     (fifos/mutexes/EbObjectWrapper lifetimes; the superres recode policy
//     :483-587; the RC/first-pass accounting :839-859; speed control
//     :880-885; release_references_eos :393-438)
//   - collect_frames_info :272-298 (the svt_av1_get_time timing wall)
//   - print_detailed_frame_info :159-270 (DETAILED_FRAME_OUTPUT debug log)
//   - the push/pop overflow SVT_ERROR + EB_FREE + svt_release_object drop
//     shells :127-132/:142-144
//   - bitstream_unit.c's od_ec_enc family (landed in l7_entropy - NOT
//     re-ported); output_bitstream_unit_dctor :24-27 (vector owns);
//     svt_aom_bitstream_ctor EB_NEW shell (entropy_coding.c:1258-1262)

namespace bitstream {}

TEST_CASE("PK1 S2: output bitstream unit ctor sizes and reset rewinds") {
    // svt_aom_output_bitstream_unit_ctor (bitstream_unit.c:32-45): size 0 ->
    // null buffer + size 0; else the allocation. svt_aom_output_bitstream_
    // reset (:50-57): the write pointer back to the beginning. Poison: the
    // reset must not move the BEGIN pointer or the size.
    bitstream::OutputBitstreamUnit unit{};
    CHECK(bitstream::outputBitstreamUnitCtor(&unit, 0) == 0);  // EB_ErrorNone
    CHECK(unit.size == 0);
    CHECK(unit.buffer_begin_av1 == nullptr);
    CHECK(unit.buffer_av1 == nullptr);
    CHECK(bitstream::outputBitstreamUnitCtor(&unit, 16) == 0);
    CHECK(unit.size == 16);
    REQUIRE(unit.buffer_begin_av1 != nullptr);
    CHECK(unit.buffer_av1 == unit.buffer_begin_av1);
    unit.buffer_av1 += 5;  // pretend 5 bytes were written
    CHECK(bitstream::outputBitstreamReset(&unit) == 0);
    CHECK(unit.buffer_av1 == unit.buffer_begin_av1);
    CHECK(unit.size == 16);
    // the zero ctor's branch is not re-entered by reset (write ptr == begin)
    CHECK(bitstream::outputBitstreamReset(&unit) == 0);
}

TEST_CASE("PK1 S2: realloc preserves the write offset; bytes-count/copy/readback") {
    // svt_realloc_output_bitstream_unit (bitstream_unit.c:64-74): offset =
    // buffer_av1 - buffer_begin_av1 survives the grow (sz > 0 gate :65);
    // sz == 0 is a no-op. The Bitstream helpers (entropy_coding.c:
    // 1264-1276): reset -> outputBitstreamReset, get_bytes_count =
    // (int)(buffer_av1 - buffer_begin_av1), copy = memcpy from the
    // beginning.
    bitstream::OutputBitstreamUnit unit{};
    REQUIRE(bitstream::outputBitstreamUnitCtor(&unit, 8) == 0);
    unit.buffer_av1 = unit.buffer_begin_av1 + 6;
    for (int i = 0; i < 6; ++i) unit.buffer_begin_av1[i] = (std::uint8_t)(0x10 * (i + 1));
    CHECK(bitstream::reallocOutputBitstreamUnit(&unit, 32) == 0);
    CHECK(unit.size == 32);
    REQUIRE(unit.buffer_begin_av1 != nullptr);
    CHECK(unit.buffer_av1 == unit.buffer_begin_av1 + 6);  // offset preserved
    for (int i = 0; i < 6; ++i) CHECK(unit.buffer_begin_av1[i] == (std::uint8_t)(0x10 * (i + 1)));
    // no-op: sz == 0
    CHECK(bitstream::reallocOutputBitstreamUnit(&unit, 0) == 0);
    CHECK(unit.size == 32);
    // null unit: no-op, no crash
    CHECK(bitstream::reallocOutputBitstreamUnit(nullptr, 16) == 0);
    // the Bitstream wrapper helpers
    bitstream::Bitstream bs{};
    bs.output_bitstream_ptr = &unit;
    CHECK(bitstream::bitstreamGetBytesCount(&bs) == 6);
    unsigned char out[16];
    memset(out, 0x55, sizeof(out));
    bitstream::bitstreamCopy(&bs, out, 6);
    for (int i = 0; i < 6; ++i) CHECK(out[i] == (std::uint8_t)(0x10 * (i + 1)));
    CHECK(out[6] == 0x55);  // the copy extent ends at size
    bitstream::bitstreamReset(&bs);
    CHECK(unit.buffer_av1 == unit.buffer_begin_av1);
    CHECK(bitstream::bitstreamGetBytesCount(&bs) == 0);
}

TEST_CASE("PK1 S2: start_encode binds the EC; ensure_capacity grows mid-stream") {
    // aom_start_encode (bitstream_unit.h:230-236): pos = 0, the EC borrows
    // the unit's buffer, state reset (rng 0x8000, cnt -9, low 0).
    // svt_aom_ec_ensure_capacity (bitstream_unit.c:210-228): needed =
    // offs + min_free; grow + re-bind the borrowed pointers past the gate.
    // Oracle: a 200-symbol bool roundtrip through TWO forced growths,
    // decoded with the l7 reader at the same f sequence.
    bitstream::OutputBitstreamUnit unit{};
    REQUIRE(bitstream::outputBitstreamUnitCtor(&unit, 8) == 0);
    entropy::AomWriter w{};
    bitstream::aomStartEncode(&w, &unit);
    CHECK(w.pos == 0);
    CHECK(w.ec.buf == unit.buffer_begin_av1);
    CHECK(w.ec.rng == 0x8000);
    CHECK(w.ec.cnt == -9);
    CHECK(w.ec.low == 0);
    CHECK(w.ec.error == 0);
    static const unsigned fs[3] = {16384, 8192, 24576};  // 1/2, 1/4, 3/4
    static const int vals[3] = {1, 0, 1};
    const int n = 200;
    int grew = 0;
    for (int i = 0; i < n; ++i) {
        entropy::odEcEncodeBoolQ15(&w.ec, vals[i % 3], fs[i % 3]);
        if (i % 8 == 7) {
            const std::uint32_t before = unit.size;
            CHECK(bitstream::ecEnsureCapacity(&w, &unit, 16) == 0);
            CHECK(w.ec.error == 0);
            if (unit.size != before) ++grew;
            CHECK(w.ec.buf == unit.buffer_begin_av1);
        }
    }
    CHECK(grew >= 2);  // the 8-byte start capacity cannot hold 25 bytes
    entropy::odEcStopEncode(&w);
    CHECK(w.pos > 0);
    // decode the same f/val sequence
    entropy::OdEcDec dec{};
    entropy::odEcDecInit(&dec, unit.buffer_begin_av1, w.pos);
    for (int i = 0; i < n; ++i)
        CHECK(entropy::odEcDecodeBoolQ15(&dec, fs[i % 3]) == vals[i % 3]);
    // capacity satisfied -> no grow, error stays clean
    const std::uint32_t szBefore = unit.size;
    CHECK(bitstream::ecEnsureCapacity(&w, &unit, 1) == 0);
    CHECK(unit.size == szBefore);
    CHECK(w.ec.error == 0);
}

TEST_CASE("PK1 S3: little-endian puts and mem_put_varsize") {
    // mem_put_le16/24/32 (definitions.h:562-584) + mem_put_varsize
    // (entropy_coding.c:32-50). Independent oracle: manual byte-shift
    // assembly, enumerated literally.
    std::uint8_t b[8];
    memset(b, 0xEE, sizeof(b));
    bitstream::memPutLe16(b, 0x1234);
    CHECK(b[0] == 0x34); CHECK(b[1] == 0x12); CHECK(b[2] == 0xEE);
    bitstream::memPutLe24(b, 0xABCDEF);
    CHECK(b[0] == 0xEF); CHECK(b[1] == 0xCD); CHECK(b[2] == 0xAB); CHECK(b[3] == 0xEE);
    bitstream::memPutLe32(b, 0x01020304);
    CHECK(b[0] == 0x04); CHECK(b[1] == 0x03); CHECK(b[2] == 0x02); CHECK(b[3] == 0x01);
    // varsize: exhaustive sz 1..4 with a value that fills each width
    memset(b, 0xEE, sizeof(b));
    bitstream::memPutVarsize(b, 1, 0x12);
    CHECK(b[0] == 0x12); CHECK(b[1] == 0xEE);
    memset(b, 0xEE, sizeof(b));
    bitstream::memPutVarsize(b, 2, 0x1234);
    CHECK(b[0] == 0x34); CHECK(b[1] == 0x12); CHECK(b[2] == 0xEE);
    memset(b, 0xEE, sizeof(b));
    bitstream::memPutVarsize(b, 3, 0x1ABCDE);
    CHECK(b[0] == 0xDE); CHECK(b[1] == 0xBC); CHECK(b[2] == 0x1A); CHECK(b[3] == 0xEE);
    memset(b, 0xEE, sizeof(b));
    bitstream::memPutVarsize(b, 4, (int)0x12345678);
    CHECK(b[0] == 0x78); CHECK(b[1] == 0x56); CHECK(b[2] == 0x34); CHECK(b[3] == 0x12);
    CHECK(b[4] == 0xEE);
    // the truncation semantics: sz 1 keeps the low byte only
    memset(b, 0xEE, sizeof(b));
    bitstream::memPutVarsize(b, 1, 0xABCD);
    CHECK(b[0] == 0xCD); CHECK(b[1] == 0xEE);
}

TEST_CASE("PK1 S3: write_obu_metadata - type + payload + trailing bits") {
    // write_obu_metadata (entropy_coding.c:3678-3690): 8-bit type literal,
    // the payload bytes as 8-bit literals, then add_trailing_bits
    // (:3668-3675: byte-aligned -> 0x80). Independent oracle: the literal
    // byte sequence enumerated per case.
    std::uint8_t buf[16];
    bitstream::SvtMetadataT md{};
    md.type = 4;  // EB_AV1_METADATA_TYPE_ITUT_T35
    std::uint8_t payload1[1] = {0xAB};
    md.payload = payload1;
    md.sz = 1;
    memset(buf, 0xC5, sizeof(buf));
    CHECK(bitstream::writeObuMetadata(&md, buf) == 3);
    CHECK(buf[0] == 0x04); CHECK(buf[1] == 0xAB); CHECK(buf[2] == 0x80);
    CHECK(buf[3] == 0xC5);  // the write extent ends at the trailing byte
    std::uint8_t payload3[3] = {0x01, 0x02, 0x03};
    md.payload = payload3;
    md.sz = 3;
    memset(buf, 0xC5, sizeof(buf));
    CHECK(bitstream::writeObuMetadata(&md, buf) == 5);
    CHECK(buf[0] == 0x04); CHECK(buf[1] == 0x01); CHECK(buf[2] == 0x02);
    CHECK(buf[3] == 0x03); CHECK(buf[4] == 0x80); CHECK(buf[5] == 0xC5);
    // empty payload: type + trailing only
    md.payload = payload1;
    md.sz = 0;
    memset(buf, 0xC5, sizeof(buf));
    CHECK(bitstream::writeObuMetadata(&md, buf) == 2);
    CHECK(buf[0] == 0x04); CHECK(buf[1] == 0x80); CHECK(buf[2] == 0xC5);
    // null metadata / null payload -> 0, no writes
    memset(buf, 0xC5, sizeof(buf));
    CHECK(bitstream::writeObuMetadata(nullptr, buf) == 0);
    CHECK(buf[0] == 0xC5);
    md.payload = nullptr;
    CHECK(bitstream::writeObuMetadata(&md, buf) == 0);
    CHECK(buf[0] == 0xC5);
}

TEST_CASE("PK1 S3: write_tile_group_header - the log2 gates and literals") {
    // write_tile_group_header (entropy_coding.c:3765-3782): tiles_log2 == 0
    // -> 0 bytes, NOTHING written (the packetizer's single-tile case
    // :3859-3860); else the present-flag bit and, when set, the two
    // tiles_log2 literals. Oracle: the MSB-first bit string assembled by
    // hand.
    std::uint8_t buf[4];
    memset(buf, 0xC5, sizeof(buf));
    CHECK(bitstream::writeTileGroupHeader(buf, 0, 0, 0, 0) == 0);
    CHECK(buf[0] == 0xC5);  // untouched at log2 0
    memset(buf, 0xC5, sizeof(buf));
    CHECK(bitstream::writeTileGroupHeader(buf, 0, 0, 1, 0) == 1);
    CHECK(buf[0] == 0x00); CHECK(buf[1] == 0xC5);  // one 0 bit
    memset(buf, 0xC5, sizeof(buf));
    CHECK(bitstream::writeTileGroupHeader(buf, 1, 0, 1, 1) == 1);
    CHECK(buf[0] == 0xC0);  // bits 1 1 0 -> 11000000
    memset(buf, 0xC5, sizeof(buf));
    CHECK(bitstream::writeTileGroupHeader(buf, 3, 1, 2, 1) == 1);
    CHECK(buf[0] == 0xE8);  // 1 11 01 -> 11101000
    memset(buf, 0xC5, sizeof(buf));
    CHECK(bitstream::writeTileGroupHeader(buf, 5, 2, 4, 1) == 2);
    CHECK(buf[0] == 0xA9);  // 1 0101 0010 -> 9 bits: 10101001 0
    CHECK(buf[1] == 0x00);  // the 9th bit is 0
}

TEST_CASE("PK1 S3: write_metadata_av1 composes matching-type OBUs and advances") {
    // svt_aom_write_metadata_av1 (entropy_coding.c:3804-3838): per
    // matching-type entry (current && payload && type == type, :3816) -
    // OBU header + uleb + payload composed at the write pointer, data
    // advanced by header + length_field + payload (:3833); non-matching or
    // payload-less entries skipped; the null-array gate (:3807-3809)
    // returns EB_ErrorBadParameter. Oracle: the OBU header byte from the
    // landed gate-tested writeObuHeader, the uleb size from the landed
    // ulebSizeInBytes, the payload bytes hand-enumerated.
    bitstream::OutputBitstreamUnit unit{};
    REQUIRE(bitstream::outputBitstreamUnitCtor(&unit, 64) == 0);
    bitstream::Bitstream bs{&unit};
    std::uint8_t p0[2] = {0x11, 0x22};
    std::uint8_t p2[1] = {0x33};
    bitstream::SvtMetadataT e0{}, e1{}, e2{};
    e0.type = bitstream::EB_AV1_METADATA_TYPE_HDR_CLL;
    e0.payload = p0;
    e0.sz = 2;
    e1.type = bitstream::EB_AV1_METADATA_TYPE_ITUT_T35;  // type mismatch -> skipped
    e1.payload = p2;
    e1.sz = 1;
    e2.type = bitstream::EB_AV1_METADATA_TYPE_HDR_CLL;
    e2.payload = p2;
    e2.sz = 1;
    bitstream::SvtMetadataT* backing[3] = {&e0, &e1, &e2};
    bitstream::SvtMetadataArrayT arr{};
    arr.metadata_array = backing;
    arr.sz = 3;
    CHECK(bitstream::writeMetadataAv1(&bs, &arr, bitstream::EB_AV1_METADATA_TYPE_HDR_CLL) == 0);
    std::uint8_t hdr = 0xC5;
    CHECK(bitstream::writeObuHeader(bitstream::OBU_METADATA, 0, &hdr) == 1);
    // entry0: hdr + uleb(1 type + 2 + 1 trailing = 4) + [01 11 22 80];
    // entry2: hdr + uleb(1 + 1 + 1 = 3) + [01 33 80]. The payload's first
    // byte is the ENTRY's own type (HDR_CLL = 1), not the query type.
    const std::uint8_t want[11] = {hdr, 0x04, 0x01, 0x11, 0x22, 0x80, hdr, 0x03, 0x01, 0x33, 0x80};
    CHECK(unit.buffer_av1 - unit.buffer_begin_av1 == 11);
    for (int i = 0; i < 11; ++i) CHECK(unit.buffer_begin_av1[i] == want[i]);
    CHECK(bitstream::ulebSizeInBytes(4) == 1);
    // null array struct -> EB_ErrorBadParameter, buffer untouched
    const std::int64_t before = unit.buffer_av1 - unit.buffer_begin_av1;
    CHECK(bitstream::writeMetadataAv1(&bs, nullptr, bitstream::EB_AV1_METADATA_TYPE_HDR_CLL) ==
          (std::int32_t)0x80001005);
    CHECK(unit.buffer_av1 - unit.buffer_begin_av1 == before);
    // null metadata_array pointer -> EB_ErrorBadParameter
    bitstream::SvtMetadataArrayT nullArr{};
    nullArr.metadata_array = nullptr;
    nullArr.sz = 3;
    CHECK(bitstream::writeMetadataAv1(&bs, &nullArr, bitstream::EB_AV1_METADATA_TYPE_HDR_CLL) ==
          (std::int32_t)0x80001005);
    // matching type but NO matching entries -> nothing composed, no advance
    CHECK(bitstream::writeMetadataAv1(&bs, &arr, bitstream::EB_AV1_METADATA_TYPE_TIMECODE) == 0);
    CHECK(unit.buffer_av1 - unit.buffer_begin_av1 == before);
}

TEST_CASE("PK1 S3: encode_sps_av1 == the committed artifact's SPS OBU region") {
    // svt_aom_encode_sps_av1 (entropy_coding.c:3925-3948) with the landed
    // mono walk (writeSequenceHeaderObu, maxDim 32) as the threaded
    // callback. INDEPENDENT ORACLE: the committed artifact
    // goldens/structural_keyframe.obu = TD (12 00) + the SPS OBU + the
    // OBU_FRAME; the composed SPS OBU must equal the artifact's SPS region
    // byte-for-byte, and the advance must equal header + length field +
    // payload. The callback is called twice (phase-1 measure + phase-2
    // rewrite) - counted. The uleb-failure path: a payload of 2^28 needs a
    // 5-byte uleb against available sizeof(uint32_t) = 4 ->
    // EB_ErrorBadParameter (the :3940-3942 gate).
    const std::string f = __FILE__;
    std::string dir = f.substr(0, f.find_last_of("/\\") + 1);
    char tuFilePath[256];
    snprintf(tuFilePath, sizeof(tuFilePath), "%sgoldens/structural_keyframe.obu", dir.c_str());
    FILE* fp = nullptr;
    fopen_s(&fp, tuFilePath, "rb");
    REQUIRE(fp != nullptr);
    unsigned char golden[256] = {0};
    const size_t goldenLen = fread(golden, 1, sizeof(golden), fp);
    fclose(fp);
    REQUIRE(goldenLen == 66);
    CHECK(golden[0] == 0x12);  // the TD
    CHECK(golden[1] == 0x00);
    CHECK(golden[2] == 0x0a);  // the SPS OBU header byte
    // parse the artifact's uleb payload size at [3..]
    std::uint64_t payloadSize = 0;
    int shift = 0;
    int spsLenBytes = 0;
    for (int i = 3; i < 10; ++i) {
        payloadSize |= (std::uint64_t)(golden[i] & 0x7f) << shift;
        shift += 7;
        ++spsLenBytes;
        if (!(golden[i] & 0x80)) break;
    }
    CHECK(payloadSize == 9);  // the artifact's SPS payload (measured below)
    const int spsTotal = 1 + spsLenBytes + (int)payloadSize;
    CHECK(spsTotal == 11);  // [2..12) is the SPS OBU

    static int spsCalls = 0;
    spsCalls = 0;
    bitstream::OutputBitstreamUnit unit{};
    REQUIRE(bitstream::outputBitstreamUnitCtor(&unit, 128) == 0);
    bitstream::Bitstream bs{&unit};
    CHECK(bitstream::encodeSpsAv1(&bs, [](std::uint8_t* dst) {
        ++spsCalls;
        return bitstream::writeSequenceHeaderObu(dst);
    }) == 0);
    CHECK(spsCalls == 2);  // phase-1 measure + phase-2 rewrite
    CHECK(unit.buffer_av1 - unit.buffer_begin_av1 == spsTotal);
    for (int i = 0; i < spsTotal; ++i) CHECK(unit.buffer_begin_av1[i] == golden[2 + i]);
    CHECK(unit.buffer_begin_av1[0] == 0x0a);
    CHECK(unit.buffer_begin_av1[1] == 0x09);
    // the uleb-failure gate: payload 2^28 -> 5-byte uleb vs available 4
    bitstream::OutputBitstreamUnit unit2{};
    REQUIRE(bitstream::outputBitstreamUnitCtor(&unit2, 16) == 0);
    bitstream::Bitstream bs2{&unit2};
    CHECK(bitstream::encodeSpsAv1(&bs2, [](std::uint8_t*) { return 0x10000000u; }) ==
          (std::int32_t)0x80001005);
}

TEST_CASE("PK1 S3: write_frame_header_av1 - two-tile composition, capacity gate, show_existing") {
    // svt_aom_write_frame_header_av1 (entropy_coding.c:3843-3920) with a
    // TEST-SIDE stand-in for the threaded scs/pcs frame-header wall (a
    // fixed 3-byte pattern, size 3, called twice per invocation: phase-1
    // measure :3858 + phase-2 rewrite :3896). Oracle: hand-composed layout
    // [hdr][uleb][frame hdr][tg hdr][tile0 prefix][tile0 data][tile1 data].
    static int fhCalls = 0;
    auto standIn = [](std::uint8_t* dst, int show_existing, int appendTrailingBits) {
        (void)show_existing;
        (void)appendTrailingBits;
        dst[0] = 0xCA;
        dst[1] = 0xFE;
        dst[2] = 0xBA;
        ++fhCalls;
        return 3u;
    };
    static std::uint8_t t0[5] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4};
    static std::uint8_t t1[7] = {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6};
    bitstream::TileEcInfo tiles[2] = {{5, t0}, {7, t1}};
    bitstream::OutputBitstreamUnit unit{};
    REQUIRE(bitstream::outputBitstreamUnitCtor(&unit, 16) == 0);
    bitstream::Bitstream bs{&unit};
    fhCalls = 0;
    // payload = 3 (hdr) + 1 (tg: log2 1, flag 0 -> one 0 bit) + 5 + 1 + 7 = 17
    CHECK(bitstream::writeFrameHeaderAv1(&bs, 0, standIn, 1, 0, tiles, 2) == 0);
    CHECK(fhCalls == 2);
    CHECK(unit.size == 20);  // the :3882-3884 gate: 19 needed > 16 -> grow to 20
    CHECK(unit.buffer_av1 - unit.buffer_begin_av1 == 19);
    std::uint8_t hdrByte = 0xC5;
    CHECK(bitstream::writeObuHeader(bitstream::OBU_FRAME, 0, &hdrByte) == 1);
    CHECK(unit.buffer_begin_av1[0] == hdrByte);
    CHECK(unit.buffer_begin_av1[1] == 0x11);  // uleb(17)
    CHECK(unit.buffer_begin_av1[2] == 0xCA);  // the frame-hdr pattern (rewritten)
    CHECK(unit.buffer_begin_av1[3] == 0xFE);
    CHECK(unit.buffer_begin_av1[4] == 0xBA);
    CHECK(unit.buffer_begin_av1[5] == 0x00);  // tg hdr: one 0 bit
    CHECK(unit.buffer_begin_av1[6] == 0x04);  // tile0 size prefix (5 - 1)
    for (int i = 0; i < 5; ++i) CHECK(unit.buffer_begin_av1[7 + i] == t0[i]);
    for (int i = 0; i < 7; ++i) CHECK(unit.buffer_begin_av1[12 + i] == t1[i]);
    // single tile (the packetizer's shape): no tg header, no prefix
    bitstream::TileEcInfo one[1] = {{9, t0}};
    bitstream::OutputBitstreamUnit unit1{};
    REQUIRE(bitstream::outputBitstreamUnitCtor(&unit1, 64) == 0);
    bitstream::Bitstream bs1{&unit1};
    fhCalls = 0;
    // payload = 3 + 0 (tg at log2 0) + 9 = 12
    CHECK(bitstream::writeFrameHeaderAv1(&bs1, 0, standIn, 0, 0, one, 1) == 0);
    CHECK(fhCalls == 2);
    CHECK(unit1.size == 64);  // no growth needed
    CHECK(unit1.buffer_av1 - unit1.buffer_begin_av1 == 14);
    CHECK(unit1.buffer_begin_av1[0] == hdrByte);
    CHECK(unit1.buffer_begin_av1[1] == 0x0C);  // uleb(12)
    CHECK(unit1.buffer_begin_av1[2] == 0xCA);
    CHECK(unit1.buffer_begin_av1[5] == t0[0]);  // tile data right after the hdr
    // show_existing: OBU_FRAME_HEADER, tg header still measured+written
    // (:3859-3860 is unconditional), tile data + prefixes skipped (:3865)
    bitstream::OutputBitstreamUnit unit3{};
    REQUIRE(bitstream::outputBitstreamUnitCtor(&unit3, 64) == 0);
    bitstream::Bitstream bs3{&unit3};
    fhCalls = 0;
    // payload = 3 + 1 (tg) = 4
    CHECK(bitstream::writeFrameHeaderAv1(&bs3, 1, standIn, 1, 0, tiles, 2) == 0);
    CHECK(fhCalls == 2);
    CHECK(unit3.buffer_av1 - unit3.buffer_begin_av1 == 6);
    std::uint8_t fhByte = 0xC5;
    CHECK(bitstream::writeObuHeader(bitstream::OBU_FRAME_HEADER, 0, &fhByte) == 1);
    CHECK(unit3.buffer_begin_av1[0] == fhByte);
    CHECK(unit3.buffer_begin_av1[1] == 0x04);  // uleb(4)
    CHECK(unit3.buffer_begin_av1[2] == 0xCA);
    CHECK(unit3.buffer_begin_av1[5] == 0x00);  // tg hdr still there
    // no tile bytes leaked past the advance
    CHECK(unit3.buffer_begin_av1[6] == 0x00);  // zero-init vector
}

TEST_CASE("PK1 S1: reorder queue pos wraps head+delta modulo size") {
    // get_reorder_queue_pos (packetization_process.c:81-83):
    // (head_index + delta) % queue_size.
    bitstream::EncodeContext ctx{};
    ctx.packetization_reorder_queue_size = 8;
    ctx.packetization_reorder_queue_head_index = 6;
    CHECK(bitstream::getReorderQueuePos(&ctx, 0) == 6);
    CHECK(bitstream::getReorderQueuePos(&ctx, 3) == 1);   // 9 % 8
    CHECK(bitstream::getReorderQueuePos(&ctx, -2) == 4);  // 4
    ctx.packetization_reorder_queue_head_index = 0;
    CHECK(bitstream::getReorderQueuePos(&ctx, 8) == 0);   // full wrap
    CHECK(bitstream::getReorderQueuePos(&ctx, 9) == 1);
}

TEST_CASE("PK1 S1: queue entry resolves the wrapped slot pointer") {
    // get_reorder_queue_entry (packetization_process.c:85-88): the entry
    // pointer at get_reorder_queue_pos(delta). Poison-filled carriers: the
    // slots the call must not touch stay poisoned.
    static bitstream::PacketizationReorderEntry slots[4];
    bitstream::PacketizationReorderEntry* queue[4] = {&slots[0], &slots[1], &slots[2], &slots[3]};
    for (int i = 0; i < 4; ++i) {
        memset(&slots[i], 0, sizeof(slots[i]));
        slots[i].picture_number = 100 + i;
    }
    bitstream::EncodeContext ctx{};
    ctx.packetization_reorder_queue = queue;
    ctx.packetization_reorder_queue_size = 4;
    ctx.packetization_reorder_queue_head_index = 2;
    CHECK(bitstream::getReorderQueueEntry(&ctx, 0) == &slots[2]);
    CHECK(bitstream::getReorderQueueEntry(&ctx, 2) == &slots[0]);  // 4 % 4
    CHECK(bitstream::getReorderQueueEntry(&ctx, 3) == &slots[1]);  // 5 % 4
    CHECK(bitstream::getReorderQueueEntry(&ctx, 3)->picture_number == 101);
}

TEST_CASE("PK1 S1: pts_descend orders descending and truncates int-diffs") {
    // pts_descend (packetization_process.c:117-123): (int)(bb->pts -
    // ba->pts). Independent oracle: the comparator's own sign convention -
    // negative when a's pts is LARGER (a sorts before b), positive when
    // smaller, 0 on ties. The (int) cast truncates 64-bit diffs: a 2^32+1
    // gap reads as +1 (the reference quirk, pinned verbatim).
    // Contract: qsort hands the comparator POINTERS INTO an array of
    // wrapper pointers, so the args are EbBufferHeaderType** slots.
    bitstream::EbBufferHeaderType a{}, b{};
    bitstream::EbBufferHeaderType* sa[2] = {&a, &b};
    a.pts = 30;
    b.pts = 10;
    CHECK(bitstream::ptsDescend(&sa[0], &sa[1]) < 0);   // 30 before 10
    CHECK(bitstream::ptsDescend(&sa[1], &sa[0]) > 0);
    a.pts = b.pts = 77;
    CHECK(bitstream::ptsDescend(&sa[0], &sa[1]) == 0);
    a.pts = 1;
    b.pts = ((std::int64_t)1 << 32) + 2;  // diff 2^32+1 -> (int) 1
    CHECK(bitstream::ptsDescend(&sa[0], &sa[1]) == 1);
}

TEST_CASE("PK1 S1: push_undisplayed appends until UNDISP_QUEUE_SIZE") {
    // push_undisplayed_frame (packetization_process.c:125-137): append at
    // the live count, count++. Overflow at UNDISP_QUEUE_SIZE = 8: the drop
    // shells (:127-132) are parked - the observable arithmetic is "no
    // append", threaded as the false return. Poison check: the overflow
    // call must not write past the live region.
    static bitstream::EbBufferHeaderType frames[10];
    memset(frames, 0, sizeof(frames));
    for (int i = 0; i < 10; ++i) frames[i].pts = i;
    bitstream::EncodeContext ctx{};
    ctx.picture_decision_undisplayed_queue_count = 0;
    for (int i = 0; i < 8; ++i) {
        CHECK(bitstream::pushUndisplayedFrame(&ctx, &frames[i]));
        CHECK(ctx.picture_decision_undisplayed_queue_count == (std::uint32_t)(i + 1));
    }
    // the queue is exactly the pushed sequence
    for (int i = 0; i < 8; ++i)
        CHECK(ctx.picture_decision_undisplayed_queue[i] == &frames[i]);
    // overflow: no append - the live top slot and the count stay untouched
    // (the array is exactly UNDISP_QUEUE_SIZE, writes past [7] would hit
    // the adjacent count field, as in the reference's guard)
    CHECK_FALSE(bitstream::pushUndisplayedFrame(&ctx, &frames[8]));
    CHECK(ctx.picture_decision_undisplayed_queue_count == 8);
    CHECK(ctx.picture_decision_undisplayed_queue[7] == &frames[7]);
    CHECK_FALSE(bitstream::pushUndisplayedFrame(&ctx, &frames[9]));
    CHECK(ctx.picture_decision_undisplayed_queue_count == 8);
    CHECK(ctx.picture_decision_undisplayed_queue[7] == &frames[7]);
}

TEST_CASE("PK1 S1: pop_undisplayed drains LIFO from the live count") {
    // pop_undisplayed_frame (packetization_process.c:139-149): the top of
    // the live region, count--; empty -> nullptr (the SVT_ERROR shell
    // parked). Independent oracle: LIFO against a manually driven index.
    static bitstream::EbBufferHeaderType frames[4];
    memset(frames, 0, sizeof(frames));
    bitstream::EncodeContext ctx{};
    CHECK(bitstream::popUndisplayedFrame(&ctx) == nullptr);  // empty
    CHECK(ctx.picture_decision_undisplayed_queue_count == 0);
    for (int i = 0; i < 4; ++i) {
        frames[i].pts = 50 - i;
        CHECK(bitstream::pushUndisplayedFrame(&ctx, &frames[i]));
    }
    for (int i = 3; i >= 0; --i) {
        CHECK(bitstream::popUndisplayedFrame(&ctx) == &frames[i]);
        CHECK(ctx.picture_decision_undisplayed_queue_count == (std::uint32_t)i);
    }
    CHECK(bitstream::popUndisplayedFrame(&ctx) == nullptr);
    CHECK(ctx.picture_decision_undisplayed_queue_count == 0);
}

TEST_CASE("PK1 S1: sort_undisplayed orders the live region by descending pts") {
    // sort_undisplayed_frame (packetization_process.c:151-156): qsort with
    // pts_descend over picture_decision_undisplayed_queue[0..count).
    // Independent oracle: a sorted-ness check (every adjacent pair obeys
    // the comparator), not a permutation snapshot - qsort is not stable.
    static bitstream::EbBufferHeaderType frames[6];
    memset(frames, 0, sizeof(frames));
    static const std::int64_t pts[6] = {3, 90, 7, 41, 12, 66};
    bitstream::EncodeContext ctx{};
    for (int i = 0; i < 6; ++i) {
        frames[i].pts = pts[i];
        CHECK(bitstream::pushUndisplayedFrame(&ctx, &frames[i]));
    }
    bitstream::sortUndisplayedFrame(&ctx);
    CHECK(ctx.picture_decision_undisplayed_queue_count == 6);
    std::int64_t last = (std::int64_t)1 << 62;
    for (std::uint32_t i = 0; i < 6; ++i) {
        CHECK(ctx.picture_decision_undisplayed_queue[i]->pts <= last);
        last = ctx.picture_decision_undisplayed_queue[i]->pts;
    }
    CHECK(last == 3);  // the minimum landed last (max moved first)
    // count == 0 sort is a no-op
    bitstream::EncodeContext empty{};
    bitstream::sortUndisplayedFrame(&empty);
    CHECK(empty.picture_decision_undisplayed_queue_count == 0);
}

TEST_CASE("PK1 S1: release_frames bumps picture numbers and advances the head") {
    // release_frames (packetization_process.c:377-390): each drained slot
    // gets picture_number += queue_size, output wrapper + ppcs hold
    // cleared, then head_index = get_reorder_queue_pos(frames). The
    // svt_release_object(ppcs) shell (:385) is parked - the nulling is
    // pinned. Exhaustive over a size-4 queue: every head x drained-slot
    // pair. The queue invariant (packetization_process.c:677-683): slot s
    // holds decode order s mod size.
    static bitstream::PacketizationReorderEntry slots[4];
    static bitstream::EbBufferHeaderType frames[4];
    memset(frames, 0, sizeof(frames));
    void* ppcsSentinel = (void*)0x77;
    for (int head = 0; head < 4; ++head) {
        bitstream::PacketizationReorderEntry* queue[4] = {&slots[0], &slots[1], &slots[2], &slots[3]};
        std::uint64_t before[4];
        for (int s = 0; s < 4; ++s) {
            memset(&slots[s], 0, sizeof(slots[s]));
            slots[s].picture_number = (std::uint64_t)s;  // slot s holds order s
            slots[s].output_stream = &frames[s];
            slots[s].ppcs_wrapper = ppcsSentinel;
            before[s] = slots[s].picture_number;
        }
        bitstream::EncodeContext ctx{};
        ctx.packetization_reorder_queue = queue;
        ctx.packetization_reorder_queue_size = 4;
        ctx.packetization_reorder_queue_head_index = (std::uint32_t)head;
        bitstream::releaseFrames(&ctx, 3);
        CHECK(ctx.packetization_reorder_queue_head_index == (std::uint32_t)((head + 3) % 4));
        for (int s = 0; s < 4; ++s) {
            const bool drained = (s == head || s == (head + 1) % 4 || s == (head + 2) % 4);
            if (drained) {
                CHECK(slots[s].picture_number == before[s] + 4);
                CHECK(slots[s].output_stream == nullptr);
                CHECK(slots[s].ppcs_wrapper == nullptr);
            } else {
                CHECK(slots[s].picture_number == before[s]);
                CHECK(slots[s].output_stream == &frames[s]);
                CHECK(slots[s].ppcs_wrapper == ppcsSentinel);
            }
        }
    }
}
