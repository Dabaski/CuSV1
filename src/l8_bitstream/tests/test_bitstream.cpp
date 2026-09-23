#include <doctest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include <entropy.h>
#include <pipeline.h>
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

TEST_CASE("structural keyframe TU assembly matches gate") {
    // Gates: sps_obu 11 0a 09 10 00 00 02 27 fe 60 c2 a0,
    // frame_obu 10 32 08 18 80 08 c5 2d b8 76 0c,
    // tu_bytes 23 12 00 0a 09 10 00 00 02 27 fe 60 c2 a0 32 08 18 80 08 c5
    // 2d b8 76 0c (tools/golden_gen composition.c BSF3 block: SVT packer +
    // court-ratified D1 mono patch). The tile data is composed HERE through
    // the l7 surfaces in decode order - partition plane
    // (encode_partition_av1 :932-981) interleaved with per-leaf
    // skip (encode_skip_coeff_av1 :995-1000) + kf y mode + angle delta
    // (encode_intra_luma_mode_kf_av1 :1026-1040) - exactly the generator's
    // svtd_bsf3_tile_data walk: 64x64 forced SPLIT (no symbol), coded
    // SPLIT at 32x32 ctx 8, four 16x16 NONE leaves (ctx 4), modes
    // {1,7,2,2}, all skip = 1, no FI symbols (no decided mode is DC_PRED).
    entropy::EcFrameContext fc;
    entropy::initDefaultEcFrameContext(&fc, 100);
    entropy::AomWriter w{};
    unsigned char tileBuf[64] = {0};
    w.ec.buf = tileBuf;
    entropy::odEcEncReset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;

    std::uint8_t aboveCtx[8];
    std::uint8_t leftCtx[16];
    memset(aboveCtx, INVALID_NEIGHBOR_DATA, sizeof(aboveCtx));
    memset(leftCtx, INVALID_NEIGHBOR_DATA, sizeof(leftCtx));
    static const int modes[4] = {1, 7, 2, 2};
    static const int lr[4] = {0, 0, 4, 4};
    static const int lc[4] = {0, 4, 0, 4};
    static const int aboveMode[4] = {-1, -1, 1, 7};
    static const int leftMode[4] = {-1, 1, -1, 2};
    static const int skipCtx[4] = {0, 1, 1, 2};

    // 32x32 coded SPLIT (the 64x64 above it is a forced split: no symbol)
    entropy::writePartition(&w, &fc, entropy::BLOCK_32X32, 1, 1, aboveCtx[0], leftCtx[0],
                            entropy::PARTITION_SPLIT);
    for (int b = 0; b < 4; ++b) {
        entropy::writePartition(&w, &fc, entropy::BLOCK_16X16, 1, 1, aboveCtx[lc[b]],
                                leftCtx[lr[b] & 15], entropy::PARTITION_NONE);
        entropy::updatePartitionContext(aboveCtx, leftCtx, lr[b], lc[b], entropy::BLOCK_16X16);
        entropy::writeSkip(&w, &fc, skipCtx[b], 1);
        int topCtx, leftCtxKf;
        entropy::getKfYModeCtx(leftMode[b] >= 0 ? 1 : 0, leftMode[b] < 0 ? 0 : leftMode[b],
                               aboveMode[b] >= 0 ? 1 : 0, aboveMode[b] < 0 ? 0 : aboveMode[b],
                               &topCtx, &leftCtxKf);
        entropy::writeKfLumaMode(&w, &fc, entropy::BLOCK_16X16,
                                 static_cast<entropy::PredictionMode>(modes[b]), topCtx, leftCtxKf, 0);
    }
    entropy::odEcStopEncode(&w);
    REQUIRE(w.pos == 5);
    static const unsigned char wantTile[5] = {0xc5, 0x2d, 0xb8, 0x76, 0x0c};
    for (int i = 0; i < 5; ++i) CHECK((unsigned)tileBuf[i] == wantTile[i]);

    // sequence header payload (write_sequence_header_obu :3699-3763 with
    // the D1 patch): 9 bytes, no OBU header/uleb
    unsigned char spsBuf[64] = {0};
    const std::uint32_t spsPayload = bitstream::writeSequenceHeaderObu(spsBuf);
    REQUIRE(spsPayload == 9);
    static const unsigned char wantSpsPayload[9] = {0x10, 0x00, 0x00, 0x02, 0x27, 0xfe, 0x60, 0xc2, 0xa0};
    for (int i = 0; i < 9; ++i) CHECK((unsigned)spsBuf[i] == wantSpsPayload[i]);

    // frame header (21-bit ratified walk, NO trailing-bits marker): 3 bytes
    unsigned char fhBuf[64] = {0};
    const std::uint32_t fhSize = bitstream::writeFrameHeader(fhBuf);
    REQUIRE(fhSize == 3);
    CHECK((unsigned)fhBuf[0] == 0x10);
    CHECK((unsigned)fhBuf[1] == 0xc0);
    CHECK((unsigned)fhBuf[2] == 0x04);

    // full TU: TD + SPS + OBU_FRAME (svt_aom_encode_td_av1 :3953-3960 +
    // svt_aom_encode_sps_av1 :3925-3948 + svt_aom_write_frame_header_av1
    // :3843-3920 structures)
    unsigned char tuBuf[128];
    memset(tuBuf, 0, sizeof(tuBuf));
    const std::uint32_t tuSize =
        bitstream::assembleStructuralKeyframeTU(tuBuf, tileBuf, w.pos);
    REQUIRE(tuSize == 23);
    static const unsigned char wantTu[23] = {0x12, 0x00, 0x0a, 0x09, 0x10, 0x00, 0x00, 0x02, 0x27,
                                             0xfe, 0x60, 0xc2, 0xa0, 0x32, 0x08, 0x10, 0xc0, 0x04,
                                             0xc5, 0x2d, 0xb8, 0x76, 0x0c};
    for (int i = 0; i < 23; ++i) CHECK((unsigned)tuBuf[i] == wantTu[i]);
}

TEST_CASE("lossy header v2 + TU v2 match gate (TS4)") {
    // TS4: the ratified lossy additions. Header v2 = the 22 structural bits
    // with base_q_idx = 100 + delta_q_present (1 bit, 0; delta_lf is nested
    // inside delta_q_present, entropy_coding.c:3564-3587) +
    // encode_loopfilter zeros 6+6+3+1 (the level[2]/[3] U/V pair skipped at
    // zero and for mono, :2296-2299) + tx_mode_select (1 bit, 0 =
    // TX_MODE_LARGEST, :3603-3607) = 40 bits = exactly 5 bytes, NO
    // byte_alignment padding. CDEF/restoration still skipped (seq
    // cdef_level = 0 / enable_restoration = 0). The SPS carries no lossy
    // state (the gate HALTs if sps_obu_v2 moves). Tile v2 = the v1
    // partition/kf-mode walk with skip = 0 (context 0 everywhere) + the
    // TS3-proven token chains; TU v2 = TD + SPS + OBU_FRAME(header v2 +
    // tile v2). Gate: tu_bytes_v2 45 (tools/golden_gen TS4 block).

    // frame header v2: 5 bytes (gate frame_obu_v2 header span)
    unsigned char fh2Buf[64] = {0};
    const std::uint32_t fh2Size = bitstream::writeFrameHeaderV2(fh2Buf);
    REQUIRE(fh2Size == 5);
    static const unsigned char wantFh2[5] = {0x10, 0xd9, 0x00, 0x00, 0x01};
    for (int i = 0; i < 5; ++i) CHECK((unsigned)fh2Buf[i] == wantFh2[i]);

    // tile v2: partition plane + skip(0) + kf modes + the TS3-proven token
    // chains; the coefficients come from the pipeline encode
    // (encodeFrameAuto16x16Q - the TS3c-proven bit-exact path)
    pixels::Plane plane(32, 32, 4);
    pixels::Plane recon(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            plane.at(x, y) = (y < 16) ? static_cast<std::uint8_t>(4 * (x + y + 1)) : 0;
    std::int32_t coeffs[1024] = {0};
    std::uint8_t modes[4] = {0};
    pipeline::encodeFrameAuto16x16Q(plane, recon, coeffs, modes, 100, transforms::TxType::DCT_DCT);
    static const std::uint8_t wantModes[4] = {1, 7, 2, 2};  // gate ecfrm_modes
    bool modesOk = true;
    for (int i = 0; i < 4; ++i)
        if (modes[i] != wantModes[i]) modesOk = false;
    CHECK(modesOk);

    std::int16_t scan16[256];
    transforms::defaultScan16x16(scan16);
    entropy::EcFrameContext fc;
    entropy::initDefaultEcFrameContext(&fc, 100);
    entropy::AomWriter w{};
    unsigned char tileBuf[128] = {0};
    w.ec.buf = tileBuf;
    entropy::odEcEncReset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;
    std::uint8_t aboveCtx[8];
    std::uint8_t leftCtx[16];
    memset(aboveCtx, INVALID_NEIGHBOR_DATA, sizeof(aboveCtx));
    memset(leftCtx, INVALID_NEIGHBOR_DATA, sizeof(leftCtx));
    entropy::DcSignLevelCoeffNa na;
    memset(&na, 0xFF, sizeof(na));
    static const int lr[4] = {0, 0, 4, 4};
    static const int lc[4] = {0, 4, 0, 4};
    entropy::writePartition(&w, &fc, entropy::BLOCK_32X32, 1, 1, aboveCtx[0], leftCtx[0],
                            entropy::PARTITION_SPLIT);
    for (int b = 0; b < 4; ++b) {
        entropy::writePartition(&w, &fc, entropy::BLOCK_16X16, 1, 1, aboveCtx[lc[b]],
                                leftCtx[lr[b] & 15], entropy::PARTITION_NONE);
        entropy::updatePartitionContext(aboveCtx, leftCtx, lr[b], lc[b], entropy::BLOCK_16X16);
        // skip = 0: every block codes its residual; context =
        // above_skip + left_skip = 0 for all four (all neighbors coded
        // skip = 0, unavailable -> 0)
        entropy::writeSkip(&w, &fc, 0, 0);
        const int aboveMode = b / 2 > 0 ? modes[(b / 2 - 1) * 2 + b % 2] : 0;
        const int leftMode = b % 2 > 0 ? modes[(b / 2) * 2 + b % 2 - 1] : 0;
        int topCtx, leftCtxKf;
        entropy::getKfYModeCtx(b % 2 > 0 ? 1 : 0, leftMode, b / 2 > 0 ? 1 : 0, aboveMode,
                               &topCtx, &leftCtxKf);
        entropy::writeKfLumaMode(&w, &fc, entropy::BLOCK_16X16,
                                 static_cast<entropy::PredictionMode>(modes[b]), topCtx, leftCtxKf, 0);
        std::int32_t ec = 0;
        for (int c = 255; c >= 0; --c) {
            if (coeffs[b * 256 + scan16[c]] != 0) { ec = c + 1; break; }
        }
        entropy::writeBlockCoeffs(&w, &fc, &na, coeffs + b * 256, scan16, entropy::TX_16X16,
                                  entropy::BLOCK_16X16, ec, lr[b], lc[b], 1,
                                  static_cast<entropy::PredictionMode>(modes[b]));
    }
    entropy::odEcStopEncode(&w);

    // TU v2 == gate tu_bytes_v2 47 (TD5b: the q100 bucket, idx 2)
    unsigned char tuBuf[128];
    memset(tuBuf, 0, sizeof(tuBuf));
    const std::uint32_t tuSize =
        bitstream::assembleStructuralKeyframeTUv2(tuBuf, tileBuf, w.pos);
    REQUIRE(tuSize == 47);
    static const unsigned char wantTu2[47] = {
        0x12, 0x00, 0x0a, 0x09, 0x10, 0x00, 0x00, 0x02, 0x27, 0xfe, 0x60, 0xc2, 0xa0, 0x32, 0x20,
        0x10, 0xd9, 0x00, 0x00, 0x01, 0xbc, 0xb5, 0x86, 0x91, 0x2e, 0xb4, 0xff, 0xd9, 0xb5, 0xbd,
        0xc5, 0x16, 0x13, 0xec, 0x17, 0x00, 0xe5, 0x21, 0x87, 0x39, 0x1a, 0x0c, 0xa8, 0x0e, 0xdc,
        0xb0, 0x38};
    for (int i = 0; i < 47; ++i) CHECK((unsigned)tuBuf[i] == wantTu2[i]);
}

TEST_CASE("structural keyframe .obu file equals the composed v2 TU and the gate bytes") {
    // TS4 milestone artifact: goldens/structural_keyframe.obu is the
    // gate-pinned tu_bytes_v2 stream (tools/golden_gen expected line:
    // tu_bytes_v2 47 12 00 0a ... b0 38 - TD5b: the q100 bucket, idx 2),
    // produced from the generator
    // output (not hand-typed). The test proves the three-way identity:
    // composed TU (l6 coefficients + l7 tile symbols + l8 assembly) ==
    // committed file == gate bytes. The USER runs the external decode
    // check (aomdec/dav1d/ffmpeg - decode handoff attempt #3).
    static const unsigned char wantTu2[47] = {
        0x12, 0x00, 0x0a, 0x09, 0x10, 0x00, 0x00, 0x02, 0x27, 0xfe, 0x60, 0xc2, 0xa0, 0x32, 0x20,
        0x10, 0xd9, 0x00, 0x00, 0x01, 0xbc, 0xb5, 0x86, 0x91, 0x2e, 0xb4, 0xff, 0xd9, 0xb5, 0xbd,
        0xc5, 0x16, 0x13, 0xec, 0x17, 0x00, 0xe5, 0x21, 0x87, 0x39, 0x1a, 0x0c, 0xa8, 0x0e, 0xdc,
        0xb0, 0x38};

    // composed v2 TU (identical pipeline to the TS4 test)
    pixels::Plane plane(32, 32, 4);
    pixels::Plane recon(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            plane.at(x, y) = (y < 16) ? static_cast<std::uint8_t>(4 * (x + y + 1)) : 0;
    std::int32_t coeffs[1024] = {0};
    std::uint8_t modes[4] = {0};
    pipeline::encodeFrameAuto16x16Q(plane, recon, coeffs, modes, 100, transforms::TxType::DCT_DCT);
    std::int16_t scan16[256];
    transforms::defaultScan16x16(scan16);
    entropy::EcFrameContext fc;
    entropy::initDefaultEcFrameContext(&fc, 100);
    entropy::AomWriter w{};
    unsigned char tileBuf[128] = {0};
    w.ec.buf = tileBuf;
    entropy::odEcEncReset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;
    std::uint8_t aboveCtx[8];
    std::uint8_t leftCtx[16];
    memset(aboveCtx, INVALID_NEIGHBOR_DATA, sizeof(aboveCtx));
    memset(leftCtx, INVALID_NEIGHBOR_DATA, sizeof(leftCtx));
    entropy::DcSignLevelCoeffNa na;
    memset(&na, 0xFF, sizeof(na));
    static const int lr[4] = {0, 0, 4, 4};
    static const int lc[4] = {0, 4, 0, 4};
    entropy::writePartition(&w, &fc, entropy::BLOCK_32X32, 1, 1, aboveCtx[0], leftCtx[0],
                            entropy::PARTITION_SPLIT);
    for (int b = 0; b < 4; ++b) {
        entropy::writePartition(&w, &fc, entropy::BLOCK_16X16, 1, 1, aboveCtx[lc[b]],
                                leftCtx[lr[b] & 15], entropy::PARTITION_NONE);
        entropy::updatePartitionContext(aboveCtx, leftCtx, lr[b], lc[b], entropy::BLOCK_16X16);
        entropy::writeSkip(&w, &fc, 0, 0);
        const int aboveMode = b / 2 > 0 ? modes[(b / 2 - 1) * 2 + b % 2] : 0;
        const int leftMode = b % 2 > 0 ? modes[(b / 2) * 2 + b % 2 - 1] : 0;
        int topCtx, leftCtxKf;
        entropy::getKfYModeCtx(b % 2 > 0 ? 1 : 0, leftMode, b / 2 > 0 ? 1 : 0, aboveMode,
                               &topCtx, &leftCtxKf);
        entropy::writeKfLumaMode(&w, &fc, entropy::BLOCK_16X16,
                                 static_cast<entropy::PredictionMode>(modes[b]), topCtx, leftCtxKf, 0);
        std::int32_t ec = 0;
        for (int c = 255; c >= 0; --c) {
            if (coeffs[b * 256 + scan16[c]] != 0) { ec = c + 1; break; }
        }
        entropy::writeBlockCoeffs(&w, &fc, &na, coeffs + b * 256, scan16, entropy::TX_16X16,
                                  entropy::BLOCK_16X16, ec, lr[b], lc[b], 1,
                                  static_cast<entropy::PredictionMode>(modes[b]));
    }
    entropy::odEcStopEncode(&w);
    unsigned char tuBuf[128];
    memset(tuBuf, 0, sizeof(tuBuf));
    const std::uint32_t tuSize = bitstream::assembleStructuralKeyframeTUv2(tuBuf, tileBuf, w.pos);
    REQUIRE(tuSize == 47);
    for (int i = 0; i < 47; ++i) CHECK((unsigned)tuBuf[i] == wantTu2[i]);

    // committed artifact identity. The goldens path derives from __FILE__
    // (robust against compile-definition quoting across CMake generators -
    // named; a BSF4_OBU_PATH definition was tried and broke on the VS
    // generator with the space in the repo path).
    const std::string tuFilePath = []() {
        const std::string f = __FILE__;
        return f.substr(0, f.find_last_of("/\\") + 1) + "goldens/structural_keyframe.obu";
    }();
    FILE* f = nullptr;
    fopen_s(&f, tuFilePath.c_str(), "rb");
    REQUIRE(f != nullptr);
    unsigned char fileBytes[64] = {0};
    const size_t n = fread(fileBytes, 1, sizeof(fileBytes), f);
    fclose(f);
    REQUIRE(n == 47);
    for (int i = 0; i < 47; ++i) CHECK(fileBytes[i] == wantTu2[i]);
}

// ---- FS5b: the per-geometry single-TU v3 artifacts (4/8/16/64) -------------
// Each artifact = TD + SPS(maxDim) + OBU_FRAME(v2 header + the single-TU
// tile), the decoder-consistent structure per frame size:
//   4x4:  the 8x8 frame coded as [part@8 SPLIT (4-symbol row, ctx 0)]
//         [4x [skip][kf, no delta][FI?][tokens]] - the 4x4 TU exists only
//         as a partition leaf (the decoder aligns frame dims to 8 px, so a
//         4x4 frame reads the partition symbol at the 8x8 node; measured
//         against the dav1d 1.5.4 TD4 trace)                = the fs5g4 tile
//   8x8:  [part@8, 4-symbol row][skip][kf][delta][FI?][tokens] = the fs28 tile
//   16x16:[part@16, 10-symbol row][skip][kf][delta][FI?][tokens] = fs216
//         (hand-rolled: the l6 16x16Q walk keeps the ratified TD5b-era shape
//          without the partition/skip symbols)
//   64x64:[part@64, 10-symbol row][skip][kf][delta][tokens] = the fs264 tile
// Three-way identity: the composed TU (l6 coeffs + l7 symbols + l8 assembly)
// == the committed .obu file == the generator's tu_bytes_v2_dS gate line.
namespace fs5b {

// the gate-line loader: one line's ints after the tag (the FS3 test's
// __FILE__-derived repo root)
struct Line {
    int n = 0;
    long long v[4200];
};

Line loadGate(const char* tag, int base = 10) {
    Line out;
    const std::string f = __FILE__;
    std::string dir = f.substr(0, f.find_last_of("/\\") + 1);
    for (int k = 0; k < 4; ++k) dir = dir.substr(0, dir.find_last_of("/\\"));
    const std::string gatePath = dir + "/tools/golden_gen/expected_primitives.txt";
    FILE* fp = nullptr;
    fopen_s(&fp, gatePath.c_str(), "r");
    if (!fp) {
        fprintf(stderr, "FS5b loader: gate NOT FOUND: %s\n", gatePath.c_str());
        return out;
    }
    char line[65536];
    const size_t tlen = strlen(tag);
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, tag, tlen) == 0) {
            char* p = line + tlen;
            char* ctx = nullptr;
            char* tok = strtok_s(p, " \t\r\n", &ctx);
            while (tok) {
                if (out.n < 4200) out.v[out.n++] = strtoll(tok, nullptr, base);
                tok = strtok_s(nullptr, " \t\r\n", &ctx);
            }
            break;
        }
    }
    fclose(fp);
    if (out.n == 0) fprintf(stderr, "FS5b loader: tag [%s] not matched\n", tag);
    return out;
}

}  // namespace fs5b

TEST_CASE("FS5b: per-geometry single-TU v3 artifacts (4x4/8x8/16x16/64x64)") {
    struct Geo {
        int S;
    };
    const Geo geos[4] = {{4}, {8}, {16}, {64}};

    for (int g = 0; g < 4; ++g) {
        const int S = geos[g].S;
        const int n = S * S;

        pixels::Plane plane(S, S, 4);
        pixels::Plane recon(S, S, 4);
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x)
                plane.at(x, y) = static_cast<std::uint8_t>((y < S / 2) ? (4 * (x + y + 1)) : 0);

        std::int32_t coeffs[4096] = {0};
        std::uint8_t modes[1] = {0};
        unsigned char tileBuf[1024];
        memset(tileBuf, 0, sizeof(tileBuf));
        std::uint32_t tilePos = 0;

        if (S == 4) {
            // the 8x8 frame with the four 4x4 TUs: the l6 loop (coeffs only;
            // its per-block emission shape = [skip][kf][FI?][tokens]) + the
            // hand-rolled [part@8 SPLIT] prefix
            pixels::Plane plane8(8, 8, 4);
            pixels::Plane recon8(8, 8, 4);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    plane8.at(x, y) =
                        static_cast<std::uint8_t>((y < 4) ? (4 * (x + y + 1)) : 0);
            std::int32_t coeffs8[256] = {0};
            std::uint8_t modes8[4] = {0};
            pipeline::encodeFrameAuto4x4Q(plane8, recon8, coeffs8, modes8, 100,
                                          transforms::TxType::DCT_DCT);
            entropy::EcFrameContext fc;
            entropy::initDefaultEcFrameContext(&fc, 100);
            entropy::AomWriter w{};
            w.ec.buf = tileBuf;
            entropy::odEcEncReset(&w.ec);
            w.allow_update_cdf = 1;
            entropy::DcSignLevelCoeffNa na;
            memset(&na, 0xFF, sizeof(na));
            std::uint8_t pAbove[8];
            std::uint8_t pLeft[16];
            memset(pAbove, INVALID_NEIGHBOR_DATA, sizeof(pAbove));
            memset(pLeft, INVALID_NEIGHBOR_DATA, sizeof(pLeft));
            entropy::writePartition(&w, &fc, entropy::BLOCK_8X8, 1, 1, pAbove[0], pLeft[0],
                                    entropy::PARTITION_SPLIT);
            entropy::updatePartitionContext(pAbove, pLeft, 0, 0, entropy::BLOCK_8X8);
            std::int16_t scan4[16];
            transforms::defaultScan4x4(scan4);
            static const int lr4[4] = {0, 0, 1, 1};
            static const int lc4[4] = {0, 1, 0, 1};
            for (int b = 0; b < 4; ++b) {
                entropy::writeSkip(&w, &fc, 0, 0);
                const int aboveMode = b / 2 > 0 ? modes8[(b / 2 - 1) * 2 + b % 2] : 0;
                const int leftMode = b % 2 > 0 ? modes8[(b / 2) * 2 + b % 2 - 1] : 0;
                int topCtx = 0, leftCtx = 0;
                entropy::getKfYModeCtx(b % 2 > 0 ? 1 : 0, leftMode, b / 2 > 0 ? 1 : 0,
                                       aboveMode, &topCtx, &leftCtx);
                entropy::writeKfLumaMode(&w, &fc, entropy::BLOCK_4X4,
                                         static_cast<entropy::PredictionMode>(modes8[b]), topCtx,
                                         leftCtx, 0);
                if (entropy::filterIntraAllowed(1, entropy::BLOCK_4X4, 0,
                                                static_cast<std::uint32_t>(modes8[b]))) {
                    entropy::writeFilterIntra(&w, &fc, entropy::BLOCK_4X4,
                                              entropy::FILTER_INTRA_MODES);
                }
                std::int32_t eob = 0;
                for (int c = 15; c >= 0; --c) {
                    if (coeffs8[b * 16 + scan4[c]] != 0) { eob = c + 1; break; }
                }
                entropy::writeBlockCoeffs(&w, &fc, &na, coeffs8 + b * 16, scan4, entropy::TX_4X4,
                                          entropy::BLOCK_4X4, eob, lr4[b], lc4[b], 1,
                                          static_cast<entropy::PredictionMode>(modes8[b]));
            }
            entropy::odEcStopEncode(&w);
            tilePos = w.pos;
        } else if (S == 16) {
            pipeline::encodeFrameAuto16x16Q(plane, recon, coeffs, modes, 100,
                                            transforms::TxType::DCT_DCT);
            entropy::EcFrameContext fc;
            entropy::initDefaultEcFrameContext(&fc, 100);
            entropy::AomWriter w{};
            w.ec.buf = tileBuf;
            entropy::odEcEncReset(&w.ec);
            w.allow_update_cdf = 1;
            entropy::DcSignLevelCoeffNa na;
            memset(&na, 0xFF, sizeof(na));
            std::uint8_t pAbove[8];
            std::uint8_t pLeft[16];
            memset(pAbove, INVALID_NEIGHBOR_DATA, sizeof(pAbove));
            memset(pLeft, INVALID_NEIGHBOR_DATA, sizeof(pLeft));
            entropy::writePartition(&w, &fc, entropy::BLOCK_16X16, 1, 1, pAbove[0], pLeft[0],
                                    entropy::PARTITION_NONE);
            entropy::writeSkip(&w, &fc, 0, 0);
            int topCtx = 0, leftCtx = 0;
            entropy::getKfYModeCtx(0, entropy::DC_PRED, 0, entropy::DC_PRED, &topCtx, &leftCtx);
            entropy::writeKfLumaMode(&w, &fc, entropy::BLOCK_16X16,
                                     static_cast<entropy::PredictionMode>(modes[0]), topCtx,
                                     leftCtx, 0);
            if (entropy::filterIntraAllowed(1, entropy::BLOCK_16X16, 0,
                                            static_cast<std::uint32_t>(modes[0]))) {
                entropy::writeFilterIntra(&w, &fc, entropy::BLOCK_16X16,
                                          entropy::FILTER_INTRA_MODES);
            }
            std::int16_t scan16[256];
            transforms::defaultScan16x16(scan16);
            std::int32_t eob = 0;
            for (int c = 255; c >= 0; --c) {
                if (coeffs[scan16[c]] != 0) { eob = c + 1; break; }
            }
            entropy::writeBlockCoeffs(&w, &fc, &na, coeffs, scan16, entropy::TX_16X16,
                                      entropy::BLOCK_16X16, eob, 0, 0, 1,
                                      static_cast<entropy::PredictionMode>(modes[0]));
            entropy::odEcStopEncode(&w);
            tilePos = w.pos;
        } else if (S == 8) {
            entropy::EcFrameContext fc;
            entropy::AomWriter w{};
            w.ec.buf = tileBuf;
            entropy::DcSignLevelCoeffNa na;
            memset(&na, 0xFF, sizeof(na));
            pipeline::encodeFrameAuto8x8Q(plane, recon, coeffs, modes, 100,
                                          transforms::TxType::DCT_DCT, &w, &fc, &na);
            tilePos = w.pos;
        } else {
            entropy::EcFrameContext fc;
            entropy::AomWriter w{};
            w.ec.buf = tileBuf;
            entropy::DcSignLevelCoeffNa na;
            memset(&na, 0xFF, sizeof(na));
            pipeline::encodeFrameAuto64x64Q(plane, recon, coeffs, modes, 100,
                                            transforms::TxType::DCT_DCT, &w, &fc, &na);
            tilePos = w.pos;
        }

        unsigned char tuBuf[1200];
        memset(tuBuf, 0, sizeof(tuBuf));
        const std::uint32_t tuSize =
            bitstream::assembleStructuralKeyframeTUv2(tuBuf, tileBuf, tilePos, S == 4 ? 8 : S);

        // the gate line (the first value = the byte count)
        char tag[48];
        snprintf(tag, sizeof(tag), "tu_bytes_v2_d%d ", S);
        const fs5b::Line want = fs5b::loadGate(tag, 16);
        REQUIRE(want.n == 1 + (int)tuSize);
        for (std::uint32_t i = 0; i < tuSize; ++i)
            CHECK((int)tuBuf[i] == (int)want.v[1 + i]);

        // the committed artifact file
        const std::string f = __FILE__;
        std::string dir = f.substr(0, f.find_last_of("/\\") + 1);
        char tuFilePath[256];
        snprintf(tuFilePath, sizeof(tuFilePath), "%sgoldens/structural_keyframe_d%d.obu",
                 dir.c_str(), S);
        FILE* fp = nullptr;
        fopen_s(&fp, tuFilePath, "rb");
        REQUIRE(fp != nullptr);
        unsigned char fileBytes[1200] = {0};
        const size_t rd = fread(fileBytes, 1, sizeof(fileBytes), fp);
        fclose(fp);
        REQUIRE(rd == tuSize);
        for (std::uint32_t i = 0; i < tuSize; ++i) CHECK(fileBytes[i] == tuBuf[i]);
        (void)n;
    }
}
