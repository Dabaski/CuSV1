#include <doctest.h>

#include <cstdio>
#include <cstring>

#include <pixels.h>
#include <transform.h>
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

TEST_CASE("odEcWriteSymbol/odEcStopEncode adapted binary path matches gate") {
    // Gate: ecsym_cdf2 2 f9 d8, ecsym_cdf2_cdf 8613 0 8 - 8 symbols
    // {1,0,1,1,0,1,0,0} through aom_write_symbol (bitstream_unit.h:265-279,
    // nsymbs==2 routes to odEcEncodeBoolQ15 at the CURRENT adapted cdf[0])
    // with allow_update_cdf = 1, start CDF = AOM_CDF2(28672) = {4096, 0}
    // + counter.
    entropy::AomWriter w{};
    unsigned char buf[64] = {0};
    w.ec.buf = buf;
    entropy::odEcEncReset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;
    std::uint16_t cdf[3] = {4096, 0, 0};
    const int syms[8] = {1, 0, 1, 1, 0, 1, 0, 0};
    for (int i = 0; i < 8; ++i) entropy::odEcWriteSymbol(&w, syms[i], cdf, 2);
    entropy::odEcStopEncode(&w);
    REQUIRE(w.pos == 2);
    CHECK((unsigned)buf[0] == 0xf9);
    CHECK((unsigned)buf[1] == 0xd8);
    CHECK(cdf[0] == 8613);
    CHECK(cdf[1] == 0);
    CHECK(cdf[2] == 8);
}

TEST_CASE("odEcReaderInit/odEcReadSymbol adapted round-trip matches gate") {
    // Gate: ecsym_cdf2_rt 1 0 1 1 0 1 0 0 + ecsym_cdf2_cdf_eq 1
    // (aom_reader_init bitreader.c:14-22 + aom_read_symbol_ bitreader.h:92-98).
    unsigned char data[2] = {0xf9, 0xd8};
    entropy::AomReader r;
    REQUIRE(entropy::odEcReaderInit(&r, data, 2) == 0);
    r.allow_update_cdf = 1;
    std::uint16_t cdf[3] = {4096, 0, 0};
    const int syms[8] = {1, 0, 1, 1, 0, 1, 0, 0};
    const std::uint16_t expectedCdf[3] = {8613, 0, 8};
    for (int i = 0; i < 8; ++i) {
        CHECK(entropy::odEcReadSymbol(&r, cdf, 2) == syms[i]);
    }
    for (int i = 0; i < 3; ++i) CHECK(cdf[i] == expectedCdf[i]);
}

TEST_CASE("KF luma-mode symbol sequence matches gate bytes and round-trips") {
    // Gate: eckf_ctx 0 0 1 0 2 1 3 2 3 3, eckf_bytes 3 47 bd 40,
    // eckf_rt 0 1 1 1 4 2 0 0 0, eckf_cdf_eq 1. The 5-block fixture mirrors
    // write_intra_frame_mode_info order (entropy_coding.c:1026-1040 + the
    // filter-intra pair :5047-5060) with fixture neighbors both sides and
    // adaptation on.
    static const int topModes[5]  = {0, 1, 2, 3, 8};  // DC, V, H, D45, D67
    static const int leftModes[5] = {0, 0, 1, 2, 3};
    static const entropy::BlockSize bsize[5] = {entropy::BLOCK_16X16, entropy::BLOCK_8X8, entropy::BLOCK_4X4, entropy::BLOCK_32X32, entropy::BLOCK_64X64};
    static const entropy::PredictionMode mode[5] = {entropy::DC_PRED, entropy::V_PRED, entropy::H_PRED, entropy::DC_PRED, entropy::DC_PRED};
    static const int delta[5] = {0, 1, 0, 0, 0};
    static const entropy::FilterIntraMode fiMode[5] = {entropy::FILTER_V_PRED, entropy::FILTER_INTRA_MODES, entropy::FILTER_INTRA_MODES, entropy::FILTER_INTRA_MODES, entropy::FILTER_INTRA_MODES};
    static const int expectedCtx[10] = {0, 0, 1, 0, 2, 1, 3, 2, 3, 3};
    static const unsigned char expectedBytes[3] = {0x47, 0xbd, 0x40};

    entropy::EcFrameContext fc;
    entropy::initDefaultEcFrameContext(&fc, 100);
    entropy::EcFrameContext fcR;
    entropy::initDefaultEcFrameContext(&fcR, 100);

    entropy::AomWriter w{};
    unsigned char buf[64] = {0};
    w.ec.buf = buf;
    entropy::odEcEncReset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;

    int ctxPairs[10];
    for (int b = 0; b < 5; ++b) {
        int topCtx, leftCtx;
        entropy::getKfYModeCtx(1, leftModes[b], 1, topModes[b], &topCtx, &leftCtx);
        ctxPairs[2 * b] = topCtx;
        ctxPairs[2 * b + 1] = leftCtx;
        entropy::writeKfLumaMode(&w, &fc, bsize[b], mode[b], topCtx, leftCtx, delta[b]);
        if (entropy::filterIntraAllowed(1, bsize[b], 0, (std::uint32_t)mode[b])) {
            entropy::writeFilterIntra(&w, &fc, bsize[b], fiMode[b]);
        }
    }
    for (int i = 0; i < 10; ++i) CHECK(ctxPairs[i] == expectedCtx[i]);
    entropy::odEcStopEncode(&w);
    REQUIRE(w.pos == 3);
    for (int i = 0; i < 3; ++i) CHECK(buf[i] == expectedBytes[i]);

    // reader round-trip with adaptation (fresh default CDFs)
    entropy::AomReader r;
    REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
    r.allow_update_cdf = 1;
    static const int wantRt[9] = {0, 1, 1, 1, 4, 2, 0, 0, 0};
    int ri = 0;
    for (int b = 0; b < 5; ++b) {
        int topCtx, leftCtx;
        entropy::getKfYModeCtx(1, leftModes[b], 1, topModes[b], &topCtx, &leftCtx);
        int d = 0;
        int m = (int)entropy::readKfLumaMode(&r, &fcR, bsize[b], topCtx, leftCtx, &d);
        CHECK(m == wantRt[ri++]);
        if (bsize[b] >= entropy::BLOCK_8X8 && entropy::isDirectionalMode(mode[b])) {
            CHECK(d == wantRt[ri++]);  // gate rt carries the raw delta symbol
				                    // (delta + MAX_ANGLE_DELTA)
        }
        if (entropy::filterIntraAllowed(1, bsize[b], 0, (std::uint32_t)mode[b])) {
            entropy::FilterIntraMode f;
            const int flag = entropy::readFilterIntra(&r, &fcR, bsize[b], &f);
            CHECK(flag == wantRt[ri++]);
            if (flag) CHECK((int)f == wantRt[ri++]);
        }
    }
    CHECK(entropy::ecFrameCdfsEqual(&fc, &fcR) == 1);
}
TEST_CASE("partition symbol surface matches gate (ratified 32x32 frame walk)") {
    // Gate: ecpart_ctx 8 4 4 4 4, ecpart_bytes 1 b5, ecpart_rt 3 0 0 0 0,
    // ecpart_cdf_eq 1, ecpart_gather 10923 0 10380 0 1
    // (tools/golden_gen/main_primitives.c ECP1 block). Ratified structural
    // keyframe tree: 32x32 frame inside sb_size 64 - 64x64 forced SPLIT (no
    // symbol), coded 10-symbol SPLIT at 32x32 (ctx 8), four coded 10-symbol
    // NONEs at 16x16 (ctx 4). Alphabet per svt_aom_partition_cdf_length
    // (entropy_coding.c:922-930), enumerated: 10 (EXT_PARTITION_TYPES) for
    // 16x16 / 32x32 / 64x64, 4 (PARTITION_TYPES) for 8x8, 8 for 128x128.
    // Contexts: partition_context_lookup (definitions.h:1551-1574) written
    // per coded block over the mi extent (coding_loop.c:1700-1713);
    // ctx = (left*2+above) + bsl*PARTITION_PLOFFSET with (byte >> bsl) & 1
    // and INVALID_NEIGHBOR_DATA (0xFF, definitions.h:334) -> 0
    // (entropy_coding.c:945-960). Gather helpers (cabac_context_model.h:
    // 378-405) pin the XOR-edge 2-symbol branches (:970-977).
    CHECK(entropy::partitionCdfLength(entropy::BLOCK_16X16) == 10);
    CHECK(entropy::partitionCdfLength(entropy::BLOCK_32X32) == 10);
    CHECK(entropy::partitionCdfLength(entropy::BLOCK_64X64) == 10);
    CHECK(entropy::partitionCdfLength(entropy::BLOCK_8X8) == 4);
    CHECK(entropy::partitionCdfLength(entropy::BLOCK_128X128) == 8);

    entropy::EcFrameContext fc;
    entropy::initDefaultEcFrameContext(&fc, 100);
    entropy::EcFrameContext fcR;
    entropy::initDefaultEcFrameContext(&fcR, 100);

    entropy::AomWriter w{};
    unsigned char buf[64] = {0};
    w.ec.buf = buf;
    entropy::odEcEncReset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;

    // fresh partition-context state (INVALID_NEIGHBOR_DATA everywhere)
    std::uint8_t aboveCtx[8];
    std::uint8_t leftCtx[16];
    memset(aboveCtx, INVALID_NEIGHBOR_DATA, sizeof(aboveCtx));
    memset(leftCtx, INVALID_NEIGHBOR_DATA, sizeof(leftCtx));

    int ctxSeq[5];
    int nctx = 0;
    // recursive walk is unrolled for the fixed ratified tree (mi grid 8x8):
    // 64x64 forced SPLIT; 32x32 coded SPLIT; four 16x16 coded NONEs.
    // (hbs px rule: (mi*4 + blockSizeWide/2) < 32, entropy_coding.c:941-943.)
    struct Node {
        int miRow, miCol;
        entropy::BlockSize bsize;
    };
    const entropy::BlockSize sub32 = entropy::BLOCK_32X32;
    const entropy::BlockSize sub16 = entropy::BLOCK_16X16;
    // 64x64 @(0,0): hbs 32px, 0+32 < 32 false -> forced SPLIT, no symbol.
    // children at mi step 8: only (0,0) in frame.
    {
        // 32x32 @(0,0): hbs 16px -> both edges -> coded SPLIT (ctx 8)
        ctxSeq[nctx++] = entropy::partitionPlaneContext(aboveCtx[0], leftCtx[0], sub32);
        entropy::writePartition(&w, &fc, sub32, 1, 1, aboveCtx[0], leftCtx[0], entropy::PARTITION_SPLIT);
        // 16x16 leaves at mi (0,0),(0,2),(2,0),(2,2): coded NONEs
        const Node leaves[4] = {{0, 0, sub16}, {0, 2, sub16}, {2, 0, sub16}, {2, 2, sub16}};
        for (int i = 0; i < 4; ++i) {
            const int r = leaves[i].miRow, c = leaves[i].miCol;
            ctxSeq[nctx++] = entropy::partitionPlaneContext(aboveCtx[c], leftCtx[r & 15], sub16);
            entropy::writePartition(&w, &fc, sub16, 1, 1, aboveCtx[c], leftCtx[r & 15],
                                    entropy::PARTITION_NONE);
            entropy::updatePartitionContext(aboveCtx, leftCtx, r, c, sub16);
        }
    }
    {
        const int wantCtx[5] = {8, 4, 4, 4, 4};
        for (int i = 0; i < 5; ++i) CHECK(ctxSeq[i] == wantCtx[i]);
    }
    entropy::odEcStopEncode(&w);
    REQUIRE(w.pos == 1);
    CHECK((unsigned)buf[0] == 0xb5);

    // reader twin: fresh state + fresh default cdfs, same fixed walk
    std::uint8_t aboveR[8];
    std::uint8_t leftR[16];
    memset(aboveR, INVALID_NEIGHBOR_DATA, sizeof(aboveR));
    memset(leftR, INVALID_NEIGHBOR_DATA, sizeof(leftR));
    entropy::AomReader r;
    REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
    r.allow_update_cdf = 1;
    const entropy::PartitionType wantRt[5] = {entropy::PARTITION_SPLIT, entropy::PARTITION_NONE,
                                              entropy::PARTITION_NONE, entropy::PARTITION_NONE,
                                              entropy::PARTITION_NONE};
    int ri = 0;
    entropy::PartitionType p;
    p = entropy::readPartition(&r, &fcR, sub32, 1, 1, aboveR[0], leftR[0]);
    CHECK(p == wantRt[ri++]);
    const Node leaves[4] = {{0, 0, sub16}, {0, 2, sub16}, {2, 0, sub16}, {2, 2, sub16}};
    for (int i = 0; i < 4; ++i) {
        p = entropy::readPartition(&r, &fcR, sub16, 1, 1, aboveR[leaves[i].miCol],
                                   leftR[leaves[i].miRow & 15]);
        CHECK(p == wantRt[ri++]);
        entropy::updatePartitionContext(aboveR, leftR, leaves[i].miRow, leaves[i].miCol, sub16);
    }
    CHECK(entropy::ecFrameCdfsEqual(&fc, &fcR) == 1);

    // gathered 2-symbol branches (entropy_coding.c:970-977): fixture gathers
    // from the FRESH row-8 cdf (32x32, ctx 8), round-trip with adaptation
    entropy::AomCdfProb gh[CDF_SIZE(2)];
    entropy::AomCdfProb gv[CDF_SIZE(2)];
    entropy::partitionGatherHorzAlike(gh, fc.partition_cdf[8], entropy::BLOCK_32X32);
    entropy::partitionGatherVertAlike(gv, fc.partition_cdf[8], entropy::BLOCK_32X32);
    CHECK((int)gh[0] == 10923);
    CHECK((int)gv[0] == 10380);
    entropy::AomCdfProb gw[CDF_SIZE(2)];
    memcpy(gw, gh, sizeof(gw));
    entropy::AomWriter w2{};
    unsigned char buf2[64] = {0};
    w2.ec.buf = buf2;
    entropy::odEcEncReset(&w2.ec);
    w2.allow_update_cdf = 1;
    w2.pos = 0;
    entropy::odEcWriteSymbol(&w2, 1, gw, 2);
    entropy::odEcWriteSymbol(&w2, 0, gw, 2);
    entropy::odEcStopEncode(&w2);
    entropy::AomCdfProb gr[CDF_SIZE(2)];
    memcpy(gr, gh, sizeof(gr));
    entropy::AomReader r2;
    REQUIRE(entropy::odEcReaderInit(&r2, buf2, w2.pos) == 0);
    r2.allow_update_cdf = 1;
    CHECK(entropy::odEcReadSymbol(&r2, gr, 2) == 1);
    CHECK(entropy::odEcReadSymbol(&r2, gr, 2) == 0);
}

TEST_CASE("skip symbol surface matches gate (context combos)") {
    // Gate: ecskip_ctx 0 1 2 0, ecskip_bytes 2 f8 0c, ecskip_rt 1 0 0 1,
    // ecskip_cdf_eq 1 (tools/golden_gen/main_primitives.c ECP2 block).
    // The skip symbol is the FIRST arithmetic-coded symbol of each I_SLICE
    // block for our config (write_modes_b entropy_coding.c:4980-4985 via
    // encode_skip_coeff_av1 :995-1000; segmentation disabled). Context =
    // av1_get_skip_context (:983-989): above_skip + left_skip of the
    // neighbor mbmis, unavailable -> 0; all three SKIP_CONTEXTS
    // (definitions.h:1313) and both symbols exercised. Read twin = the aom
    // read_skip_txfm semantics (decodemv.c, out-of-tree BSF4 arbiter) minus
    // the SEG_LVL_SKIP implicit-1 branch (segmentation surface, deferred).
    CHECK(entropy::getSkipContext(0, 0, 0, 0) == 0);
    CHECK(entropy::getSkipContext(1, 1, 0, 0) == 1);
    CHECK(entropy::getSkipContext(1, 1, 1, 1) == 2);
    CHECK(entropy::getSkipContext(1, 0, 1, 0) == 0);

    entropy::EcFrameContext fc;
    entropy::initDefaultEcFrameContext(&fc, 100);
    entropy::EcFrameContext fcR;
    entropy::initDefaultEcFrameContext(&fcR, 100);

    entropy::AomWriter w{};
    unsigned char buf[64] = {0};
    w.ec.buf = buf;
    entropy::odEcEncReset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;

    static const int ctx[4] = {0, 1, 2, 0};
    static const int skip[4] = {1, 0, 0, 1};
    for (int b = 0; b < 4; ++b) entropy::writeSkip(&w, &fc, ctx[b], skip[b]);
    entropy::odEcStopEncode(&w);
    REQUIRE(w.pos == 2);
    CHECK((unsigned)buf[0] == 0xf8);
    CHECK((unsigned)buf[1] == 0x0c);

    entropy::AomReader r;
    REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
    r.allow_update_cdf = 1;
    for (int b = 0; b < 4; ++b) CHECK(entropy::readSkip(&r, &fcR, ctx[b]) == skip[b]);
    CHECK(entropy::ecFrameCdfsEqual(&fc, &fcR) == 1);
}

TEST_CASE("token chain per-TU roundtrip matches gate (16x16/8x8/4x4, q100)") {
    // Gate: ectok_eob 21 10 0, ectok_bytes 16 1a 76 83 60 70 4d 64 92 25 9b
    // 7e 88 e5 d4 60 ec, ectok_rt 21 10 0, ectok_cdf_eq 1 (composition.c
    // TS1 block). Fixture: f16 source, block 0 (16x16, eob 21), block 1
    // (8x8, eob 10), block 4 (4x4, eob 0 = txb_skip-only path). Whole-block
    // TUs: txb_skip_ctx = 0 (get_txb_ctx :298-299 plane_bsize == tx_bsize),
    // dc_sign_ctx = 0 (no coded neighbors). TS3: the token chain emits the
    // tx-type symbol between txb_skip and eob_pt (entropy_coding.c:374-376)
    // for every q>0 TU - the DCT_DCT index through intra_ext_tx_cdf with
    // intra_dir = DC_PRED, exactly the generator's TS1 drive call sites
    // (composition.c:2901-2903); the eob=0 4x4 TU skips it via the early
    // return. Quantized through the f16q path (quantize_fp at q100).
    pixels::Plane src(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            src.at(x, y) = (y < 16) ? static_cast<std::uint8_t>(4 * (x + y + 1)) : 0;

    // 16x16 block (0,0): DC_PRED predictor = 0 (no neighbors)
    std::int16_t res16[256];
    for (int i = 0; i < 256; ++i) res16[i] = static_cast<std::int16_t>(src.at(i % 16, i / 16));
    std::int32_t cb16[256] = {0};
    transforms::fwdTxfm2d16x16(res16, cb16, 16, transforms::TxType::DCT_DCT);
    transforms::QuantTables qt;
    transforms::buildQuantTables(100, qt);
    std::int16_t scan16[256];
    transforms::defaultScan16x16(scan16);
    std::int32_t qc16[256] = {0}, dq16[256] = {0};
    std::uint16_t eob16 = 0;
    transforms::quantizeFp16x16(cb16, qt, scan16, qc16, dq16, &eob16);
    CHECK((int)eob16 == 21);

    // 8x8 block (bx=1, by=0): source pixels 16..23 x 0..7, predictor = 0
    std::int16_t res8[64];
    for (int i = 0; i < 64; ++i) res8[i] = static_cast<std::int16_t>(src.at(16 + i % 8, i / 8));
    std::int32_t cb8[64] = {0};
    transforms::fwdTxfm2d8x8(res8, cb8, 8, transforms::TxType::DCT_DCT);
    std::int16_t scan8[64];
    transforms::defaultScan8x8(scan8);
    std::int32_t qc8[64] = {0}, dq8[64] = {0};
    std::uint16_t eob8 = 0;
    transforms::quantizeFp8x8(cb8, qt, scan8, qc8, dq8, &eob8);
    CHECK((int)eob8 == 10);

    // 4x4 block (bx=0, by=1): source pixels 0..3 x 16..19, predictor = 0
    std::int16_t res4[16];
    for (int i = 0; i < 16; ++i) res4[i] = static_cast<std::int16_t>(src.at(i % 4, 16 + i / 4));
    std::int32_t cb4[16] = {0};
    transforms::fwdTxfm2d4x4(res4, cb4, 4, transforms::TxType::DCT_DCT);
    std::int16_t scan4[16];
    transforms::defaultScan4x4(scan4);
    std::int32_t qc4[16] = {0}, dq4[16] = {0};
    std::uint16_t eob4 = 0;
    transforms::quantizeFp4x4(cb4, qt, scan4, qc4, dq4, &eob4);
    CHECK((int)eob4 == 0);

    entropy::EcFrameContext fc;
    entropy::initDefaultEcFrameContext(&fc, 100);
    entropy::EcFrameContext fcR;
    entropy::initDefaultEcFrameContext(&fcR, 100);

    entropy::AomWriter w{};
    unsigned char buf[128] = {0};
    w.ec.buf = buf;
    entropy::odEcEncReset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;

    entropy::writeTxbCoeffs(&w, &fc, qc16, scan16, entropy::TX_16X16, eob16, 0, 0, 1, entropy::DC_PRED,
                            entropy::COMPONENT_LUMA);
    entropy::writeTxbCoeffs(&w, &fc, qc8, scan8, entropy::TX_8X8, eob8, 0, 0, 1, entropy::DC_PRED,
                            entropy::COMPONENT_LUMA);
    entropy::writeTxbCoeffs(&w, &fc, qc4, scan4, entropy::TX_4X4, eob4, 0, 0, 1, entropy::DC_PRED,
                            entropy::COMPONENT_LUMA);
    entropy::odEcStopEncode(&w);
    // TD5b: the token tables are the q100 bucket (idx 2) - the bytes follow
    // the regenerated ectok_bytes gate line.
    REQUIRE(w.pos == 17);
    static const unsigned char wantBytes[17] = {0x36, 0xa4, 0x3a, 0x79, 0xdf, 0xfe, 0x77, 0x33,
                                                0x1d, 0x73, 0x04, 0xff, 0xaf, 0xbd, 0xd9, 0x19,
                                                0xa0};
    for (int i = 0; i < 17; ++i) CHECK((unsigned)buf[i] == wantBytes[i]);

    entropy::AomReader r;
    REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
    r.allow_update_cdf = 1;
    std::int32_t rc16[256] = {0};
    std::int32_t rc8[64] = {0};
    std::int32_t rc4[16] = {0};
    const int reob16 = entropy::readTxbCoeffs(&r, &fcR, rc16, scan16, entropy::TX_16X16, 0, 0, 1,
                                              entropy::DC_PRED, entropy::COMPONENT_LUMA);
    const int reob8 = entropy::readTxbCoeffs(&r, &fcR, rc8, scan8, entropy::TX_8X8, 0, 0, 1,
                                             entropy::DC_PRED, entropy::COMPONENT_LUMA);
    const int reob4 = entropy::readTxbCoeffs(&r, &fcR, rc4, scan4, entropy::TX_4X4, 0, 0, 1,
                                             entropy::DC_PRED, entropy::COMPONENT_LUMA);
    CHECK(reob16 == 21);
    CHECK(reob8 == 10);
    CHECK(reob4 == 0);
    for (int i = 0; i < 256; ++i) CHECK(rc16[i] == qc16[i]);
    for (int i = 0; i < 64; ++i) CHECK(rc8[i] == qc8[i]);
    for (int i = 0; i < 16; ++i) CHECK(rc4[i] == qc4[i]);
    CHECK(entropy::ecFrameCdfsEqual(&fc, &fcR) == 1);
}

TEST_CASE("token chain per-block roundtrip matches gate (4x 16x16, q100, NA accumulation)") {
    // Gate: ecblk_ctx 0 2 2 2, ecblk_bytes 19 1a 76 83 60 70 4d 64 92 25 57
    // 03 f2 98 71 1c 85 43 f9 80, ecblk_rt 21 21 0 0, ecblk_cdf_eq 1
    // (composition.c TS2 block). 64x32 frame, 2x2 of 16x16 blocks, raster
    // order, NA accumulates after each block. txb_skip_ctx = 0 always
    // (plane_bsize == tx_bsize, :298-299); dc_sign_ctx covers 0/1/2.
    // TS3: one tx-type symbol per q>0 TU (bytes 18 -> 19); the intra_dir is
    // DC_PRED at every call site, matching the generator drive.
    // The skip_contexts table branch (:301-308) and the chroma branch
    // (:310-314) are dead for whole-block luma TUs (plane_bsize == tx_bsize
    // always) - ported in l7 for the range rule but never exercised here.
    static const int px[4][2] = {{0, 0}, {16, 0}, {0, 16}, {16, 16}};
    static const int mi[4][2] = {{0, 0}, {0, 4}, {4, 0}, {4, 4}};
    static const int wantCtx[4] = {0, 2, 2, 2};

    transforms::QuantTables qt;
    transforms::buildQuantTables(100, qt);
    std::int16_t scan16[256];
    transforms::defaultScan16x16(scan16);

    // quantize all 4 blocks (same as the generator fixture)
    std::int32_t qc[4][256];
    std::uint16_t eob[4];
    pixels::Plane src(64, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x)
            src.at(x, y) = (y < 16) ? static_cast<std::uint8_t>(4 * (x % 32 + y + 1)) : 0;
    for (int b = 0; b < 4; ++b) {
        std::int16_t res[256];
        for (int i = 0; i < 256; ++i) res[i] = static_cast<std::int16_t>(src.at(px[b][0] + i % 16, px[b][1] + i / 16));
        std::int32_t cb[256] = {0};
        transforms::fwdTxfm2d16x16(res, cb, 16, transforms::TxType::DCT_DCT);
        std::int32_t dq[256] = {0};
        transforms::quantizeFp16x16(cb, qt, scan16, qc[b], dq, &eob[b]);
    }

    entropy::EcFrameContext fc;
    entropy::initDefaultEcFrameContext(&fc, 100);
    entropy::EcFrameContext fcR;
    entropy::initDefaultEcFrameContext(&fcR, 100);

    entropy::AomWriter w{};
    unsigned char buf[128] = {0};
    w.ec.buf = buf;
    entropy::odEcEncReset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;

    entropy::DcSignLevelCoeffNa na;
    memset(&na, 0xFF, sizeof(na));  // INVALID_NEIGHBOR_DATA everywhere

    for (int b = 0; b < 4; ++b) {
        entropy::writeBlockCoeffs(&w, &fc, &na, qc[b], scan16, entropy::TX_16X16,
                                  entropy::BLOCK_16X16, eob[b], mi[b][0], mi[b][1], 1,
                                  entropy::DC_PRED, entropy::COMPONENT_LUMA);
    }
    entropy::odEcStopEncode(&w);
    // TD5b: the token tables are the q100 bucket (idx 2) - the bytes follow
    // the regenerated ecblk_bytes gate line.
    REQUIRE(w.pos == 20);
    static const unsigned char wantBytes[20] = {0x36, 0xa4, 0x3a, 0x79, 0xdf, 0xfe, 0x77, 0x33,
                                                0x1d, 0x72, 0xd8, 0x1a, 0x17, 0xd5, 0x70, 0xf6,
                                                0xf7, 0x1f, 0xfe, 0x7d};
    for (int i = 0; i < 20; ++i) CHECK((unsigned)buf[i] == wantBytes[i]);

    entropy::AomReader r;
    REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
    r.allow_update_cdf = 1;
    entropy::DcSignLevelCoeffNa naR;
    memset(&naR, 0xFF, sizeof(naR));

    for (int b = 0; b < 4; ++b) {
        std::int32_t rc[256] = {0};
        const int reob = entropy::readBlockCoeffs(&r, &fcR, &naR, rc, scan16, entropy::TX_16X16,
                                                  entropy::BLOCK_16X16, mi[b][0], mi[b][1], 1,
                                                  entropy::DC_PRED, entropy::COMPONENT_LUMA);
        CHECK(reob == (int)eob[b]);
        for (int i = 0; i < 256; ++i) CHECK(rc[i] == qc[b][i]);
    }
CHECK(entropy::ecFrameCdfsEqual(&fc, &fcR) == 1);
}

TEST_CASE("uv angle-delta pair matches gate bytes, gate enumerated (CS1b)") {
    // Gate: ecuv_delta_bytes 23 02 14 35 7d 9a 5b 45 51 70 80 b9 df 66 ce ca
    // bb ee 9a 7a 39 12 56 40, ecuv_delta_cdf_eq 1 (composition.c
    // svtd_cs1_uv_delta_drive: entropy_coding.c:1087-1092, the symbol
    // delta + MAX_ANGLE_DELTA through angle_delta_cdf[chroma_mode - V_PRED];
    // read twin aom decodemv.c:830-833 + read_angle_delta :603-606).
    // Gate enumeration (the full UvPredictionMode domain 0..13, both bsize
    // regimes): the symbol is emitted ONLY when bsize >= BLOCK_8X8
    // (av1_use_angle_delta, aom reconintra.h:59-61) AND
    // isDirectionalMode(uv2y(chroma_mode)) - directional chroma modes are
    // exactly UV_V_PRED..UV_D67_PRED (1..8, uv2y[uv] == uv so the SVT writer
    // row :1090 == the aom reader's folded row); DC/SMOOTH/PAETH/CFL fold to
    // non-directional and consume NOTHING. The cdf row index chroma_mode -
    // V_PRED is only evaluated inside that gate (range-safe: 0..7).
    entropy::EcFrameContext fc;
    entropy::initDefaultEcFrameContext(&fc, 100);
    entropy::EcFrameContext fcR;
    entropy::initDefaultEcFrameContext(&fcR, 100);

    entropy::AomWriter w{};
    unsigned char buf[64] = {0};
    w.ec.buf = buf;
    entropy::odEcEncReset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;

    int syms[DIRECTIONAL_MODES * (2 * MAX_ANGLE_DELTA + 1)];
    int n = 0;
    for (int uv = entropy::V_PRED; uv <= entropy::D67_PRED; ++uv) {
        for (int d = -MAX_ANGLE_DELTA; d <= MAX_ANGLE_DELTA; ++d) {
            entropy::writeUvAngleDelta(&w, &fc, entropy::BLOCK_16X16,
                                       (entropy::UvPredictionMode)uv, d);
            syms[n++] = d + MAX_ANGLE_DELTA;
        }
    }
    entropy::odEcStopEncode(&w);
    REQUIRE(w.pos == 23);
    static const unsigned char wantBytes[23] = {0x02, 0x14, 0x35, 0x7d, 0x9a, 0x5b, 0x45, 0x51,
                                                0x70, 0x80, 0xb9, 0xdf, 0x66, 0xce, 0xca, 0xbb,
                                                0xee, 0x9a, 0x7a, 0x39, 0x12, 0x56, 0x40};
    for (int i = 0; i < 23; ++i) CHECK((unsigned)buf[i] == wantBytes[i]);

    entropy::AomReader r;
    REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
    r.allow_update_cdf = 1;
    n = 0;
    for (int uv = entropy::V_PRED; uv <= entropy::D67_PRED; ++uv) {
        for (int d = -MAX_ANGLE_DELTA; d <= MAX_ANGLE_DELTA; ++d) {
            const int s = entropy::readUvAngleDelta(&r, &fcR, entropy::BLOCK_16X16,
                                                    (entropy::UvPredictionMode)uv);
            CHECK(s == syms[n++]);
        }
    }
    CHECK(entropy::ecFrameCdfsEqual(&fc, &fcR) == 1);

    // The gate: bsize < BLOCK_8X8 -> NOTHING for every UV mode 0..13
    // (writer emits no symbol - odEcEncTell unchanged; reader consumes none
    // and returns -1). The empty-stream odEcStopEncode flush is a machinery
    // constant, not gate evidence - not asserted.
    for (int uv = 0; uv < entropy::UV_INTRA_MODES; ++uv) {
        entropy::AomWriter wg{};
        unsigned char bufG[8] = {0};
        wg.ec.buf = bufG;
        entropy::odEcEncReset(&wg.ec);
        wg.allow_update_cdf = 1;
        wg.pos = 0;
        const int tellBefore = entropy::odEcEncTell(&wg.ec);
        entropy::writeUvAngleDelta(&wg, &fc, entropy::BLOCK_4X4, (entropy::UvPredictionMode)uv, 2);
        CHECK(entropy::odEcEncTell(&wg.ec) == tellBefore);
        entropy::AomReader rg;
        REQUIRE(entropy::odEcReaderInit(&rg, bufG, 0) == 0);
        rg.allow_update_cdf = 1;
        const int tellRBefore = entropy::odEcDecTell(&rg.ec);
        CHECK(entropy::readUvAngleDelta(&rg, &fcR, entropy::BLOCK_4X4, (entropy::UvPredictionMode)uv) == -1);
        CHECK(entropy::odEcDecTell(&rg.ec) == tellRBefore);
    }
    // bsize >= BLOCK_8X8: exactly the directional UV modes 1..8 emit one
    // symbol (odEcEncTell advances; the read-back re-adapts fcR so the final
    // cdf compare holds); 0 and 9..13 (non-directional, incl. UV_CFL_PRED)
    // emit nothing and read -1 with no consumption.
    for (int uv = 0; uv < entropy::UV_INTRA_MODES; ++uv) {
        entropy::AomWriter wg{};
        unsigned char bufG2[8] = {0};
        wg.ec.buf = bufG2;
        entropy::odEcEncReset(&wg.ec);
        wg.allow_update_cdf = 1;
        wg.pos = 0;
        const int tellBefore2 = entropy::odEcEncTell(&wg.ec);
        entropy::writeUvAngleDelta(&wg, &fc, entropy::BLOCK_8X8, (entropy::UvPredictionMode)uv, 2);
        const int tellAfter2 = entropy::odEcEncTell(&wg.ec);
        entropy::odEcStopEncode(&wg);
        if (uv >= entropy::V_PRED && uv <= entropy::D67_PRED) {
            CHECK(tellAfter2 > tellBefore2);
            entropy::AomReader rg;
            REQUIRE(entropy::odEcReaderInit(&rg, bufG2, wg.pos) == 0);
            rg.allow_update_cdf = 1;
            CHECK(entropy::readUvAngleDelta(&rg, &fcR, entropy::BLOCK_8X8, (entropy::UvPredictionMode)uv) ==
                  2 + MAX_ANGLE_DELTA);
        } else {
            CHECK(tellAfter2 == tellBefore2);
            entropy::AomReader rg;
            REQUIRE(entropy::odEcReaderInit(&rg, bufG2, 0) == 0);
            rg.allow_update_cdf = 1;
            const int tellRBefore2 = entropy::odEcDecTell(&rg.ec);
            CHECK(entropy::readUvAngleDelta(&rg, &fcR, entropy::BLOCK_8X8, (entropy::UvPredictionMode)uv) == -1);
            CHECK(entropy::odEcDecTell(&rg.ec) == tellRBefore2);
        }
    }
    CHECK(entropy::ecFrameCdfsEqual(&fc, &fcR) == 1);
}

TEST_CASE("uv_mode symbol pair matches gate bytes and round-trips (CS1b)") {
    // Gate: ecuv_mode_bytes 11 73 8f 88 d0 8f 88 7b b4 42 65 b2,
    // ecuv_mode_cdf_eq 1 (composition.c svtd_cs1_uv_mode_drive; the
    // default_uv_mode_cdf extract, cabac_context_model.c:105). Alphabet:
    // UV_INTRA_MODES - !cflAllowed (entropy_coding.c:1080-1081 write /
    // aom decodemv.c:140-148 read), ctx [cflAllowed][luma_mode]. The cfl=1
    // rows spread (luma+5) mod 14 so the 14-symbol alphabet incl.
    // UV_CFL_PRED round-trips as a symbol (the D2 decision never picks it;
    // write_cfl_alphas :1083-1085 stays deferred).
    entropy::EcFrameContext fc;
    entropy::initDefaultEcFrameContext(&fc, 100);
    entropy::EcFrameContext fcR;
    entropy::initDefaultEcFrameContext(&fcR, 100);

    entropy::AomWriter w{};
    unsigned char buf[64] = {0};
    w.ec.buf = buf;
    entropy::odEcEncReset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;

    int syms[2 * entropy::INTRA_MODES];
    int n = 0;
    for (int cfl = 0; cfl < entropy::CFL_ALLOWED_TYPES; ++cfl) {
        for (int luma = 0; luma < entropy::INTRA_MODES; ++luma) {
            const entropy::UvPredictionMode uv =
                cfl ? (entropy::UvPredictionMode)((luma + 5) % entropy::UV_INTRA_MODES)
                    : (entropy::UvPredictionMode)luma;
            entropy::writeUvMode(&w, &fc, cfl, (entropy::PredictionMode)luma, uv);
            syms[n++] = (int)uv;
        }
    }
    entropy::odEcStopEncode(&w);
    REQUIRE(w.pos == 11);
    static const unsigned char wantBytes[11] = {0x73, 0x8f, 0x88, 0xd0, 0x8f, 0x88,
                                                0x7b, 0xb4, 0x42, 0x65, 0xb2};
    for (int i = 0; i < 11; ++i) CHECK((unsigned)buf[i] == wantBytes[i]);

    entropy::AomReader r;
    REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
    r.allow_update_cdf = 1;
    n = 0;
    for (int cfl = 0; cfl < entropy::CFL_ALLOWED_TYPES; ++cfl) {
        for (int luma = 0; luma < entropy::INTRA_MODES; ++luma) {
            const entropy::UvPredictionMode s =
                entropy::readUvMode(&r, &fcR, cfl, (entropy::PredictionMode)luma);
            CHECK((int)s == syms[n++]);
        }
    }
    CHECK(entropy::ecFrameCdfsEqual(&fc, &fcR) == 1);
}

static const unsigned char ecuv4U_bytes[7] = {
    0x09, 0xa0, 0xca, 0x10, 0x67, 0x4b, 0x80
};

static const unsigned char ecuv8U_bytes[20] = {
    0x25, 0x11, 0xb3, 0x3d, 0x65, 0x60, 0x55, 0xbd, 0xd0, 0x58, 0xf6, 0x68,
    0x07, 0x67, 0x99, 0x99, 0x9c, 0xff, 0xff, 0xc0
};

static const unsigned char ecuv16U_bytes[59] = {
    0x4a, 0x50, 0xed, 0xdd, 0xfa, 0xcd, 0x3b, 0xeb, 0x3a, 0x1b, 0xb5, 0xa5,
    0x68, 0x74, 0x6b, 0x35, 0x22, 0xd0, 0x30, 0x22, 0xcf, 0x78, 0x26, 0x1e,
    0xa0, 0x16, 0xa1, 0x29, 0xe9, 0xa5, 0xf8, 0x35, 0x79, 0xc8, 0xe9, 0xb3,
    0x64, 0x1d, 0xaf, 0x0a, 0xa6, 0x94, 0xd4, 0xfd, 0xe0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08
};

static const unsigned char ecuv32U_bytes[194] = {
    0x26, 0x6b, 0xbd, 0xbf, 0x7e, 0x82, 0xee, 0xfb, 0x68, 0x15, 0x2e, 0xcd,
    0x76, 0xb1, 0x34, 0x4f, 0xc6, 0x05, 0x0d, 0x8a, 0xd5, 0xed, 0xf9, 0x86,
    0x47, 0xae, 0xdf, 0x96, 0x17, 0xbe, 0x7f, 0xa9, 0xc8, 0x03, 0x5c, 0xa2,
    0x12, 0x86, 0xc7, 0x57, 0x80, 0x30, 0x6f, 0x64, 0xfb, 0x49, 0x5c, 0x9c,
    0xca, 0xad, 0xfc, 0x88, 0x76, 0x90, 0xf2, 0x31, 0x6b, 0x74, 0xf6, 0x58,
    0x22, 0x8f, 0xb9, 0x86, 0x75, 0x25, 0x20, 0xad, 0xd3, 0x6c, 0xbb, 0x48,
    0x34, 0x8e, 0xc8, 0x0c, 0xe6, 0xc1, 0x8a, 0x14, 0x04, 0xea, 0xa6, 0x72,
    0xab, 0xe9, 0x45, 0x8c, 0x77, 0x27, 0xc7, 0x9e, 0xbf, 0xc4, 0xb9, 0xec,
    0xbf, 0x38, 0x7e, 0x9a, 0xc4, 0x23, 0x92, 0x16, 0x9d, 0x62, 0x4b, 0x22,
    0x9b, 0xa7, 0xd1, 0x7b, 0x69, 0x65, 0x11, 0xc6, 0xfc, 0x4d, 0x40, 0x6f,
    0x0b, 0xdc, 0x15, 0xe4, 0x16, 0x20, 0xc2, 0xb5, 0x1c, 0xb5, 0x83, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xc0
};

static const unsigned char ecuv0U_bytes[1] = {
    0xc0
};

static const unsigned char ecuv4V_bytes[7] = {
    0x17, 0x73, 0x07, 0x8a, 0xa3, 0xb0, 0xb8
};

static const unsigned char ecuv8V_bytes[20] = {
    0x6b, 0x8e, 0xc0, 0x4f, 0xa6, 0xe7, 0xaf, 0xf7, 0xe4, 0xb9, 0xfe, 0x17,
    0x21, 0x45, 0x60, 0xce, 0xb4, 0x4a, 0xaa, 0xab
};

static const unsigned char ecuv16V_bytes[59] = {
    0xc8, 0x10, 0xdb, 0xef, 0xeb, 0xd0, 0xef, 0xc1, 0x17, 0x83, 0x5f, 0xdd,
    0xef, 0xe7, 0x16, 0xb0, 0xf0, 0xcf, 0x52, 0x18, 0x38, 0x7b, 0x24, 0x59,
    0xee, 0x4b, 0xa2, 0x0d, 0x0a, 0x79, 0x40, 0x46, 0x7b, 0x71, 0xeb, 0x92,
    0x5a, 0x0e, 0x70, 0x8a, 0x57, 0x69, 0x77, 0x7c, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0x90
};

static const unsigned char ecuv32V_bytes[194] = {
    0x27, 0x99, 0xbe, 0x2e, 0xff, 0xc2, 0x23, 0xc1, 0x6e, 0x66, 0x7b, 0x01,
    0xb8, 0xdf, 0xed, 0xb9, 0x60, 0xc9, 0xcd, 0xc6, 0x76, 0x1a, 0x30, 0xb3,
    0xa6, 0xa1, 0x88, 0xa9, 0x8c, 0xc0, 0x3a, 0x48, 0xb4, 0xa3, 0xec, 0xe1,
    0xbf, 0x73, 0x85, 0x90, 0x14, 0xf9, 0x4b, 0x78, 0xe8, 0x7e, 0xef, 0xf1,
    0x3c, 0x8f, 0x5d, 0x88, 0x9a, 0x56, 0x04, 0x67, 0x4f, 0x09, 0xc9, 0x1f,
    0x95, 0xaf, 0x69, 0x96, 0xaa, 0xfb, 0xc0, 0x26, 0x15, 0x02, 0x77, 0x37,
    0xa8, 0x80, 0x94, 0x2b, 0x30, 0xd2, 0x7d, 0xa1, 0xf9, 0x8f, 0x8a, 0xdd,
    0xc3, 0x37, 0x2e, 0xab, 0x1c, 0xdb, 0xcb, 0x12, 0x8b, 0xfc, 0xfd, 0x89,
    0x86, 0x23, 0x2a, 0x9e, 0xf8, 0x3e, 0xe6, 0xaa, 0xb4, 0x7e, 0x08, 0x84,
    0xc1, 0x33, 0x6b, 0x7c, 0x1a, 0xa7, 0x1a, 0x5d, 0xf2, 0x75, 0xbf, 0xad,
    0x69, 0xaa, 0x11, 0xef, 0x25, 0x34, 0xdf, 0x6e, 0x06, 0x40, 0x52, 0x7f,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xfc
};

static const unsigned char ecuv0V_bytes[1] = {
    0xc0
};

// CS1b chroma fixture (mechanical mirror of composition.c
// svtd_cs1_uv_txb_drive): per (component, txs), eob = N/2, levels
// c==0 -> 3, c==1 -> 12, c==eob-1 -> 2, else (c%3)+1, all other positions 0;
// signs alternate starting +, V = negated U; txb_skip_ctx rotation {7,8,9,7};
// dc_sign_ctx 0; intra_dir DC_PRED; fresh default CDFs (q100 bucket) per
// stream. Byte arrays mechanically emitted from expected_primitives.txt.
static void cs1bFillQc(std::int32_t* qc, const std::int16_t* scan, int eob, int negate) {
    for (int cpos = 0; cpos < eob; ++cpos) {
        const int pos = scan[cpos];
        int level;
        if (cpos == 0) level = 3;
        else if (cpos == 1) level = 12;
        else if (cpos == eob - 1) level = 2;
        else level = (cpos % 3) + 1;
        int sign = (cpos % 2) == 0 ? 1 : -1;
        if (negate) sign = -sign;
        qc[pos] = level * sign;
    }
}

TEST_CASE("chroma token chain per-component per-txs matches gate bytes (CS1b)") {
    // Gate: ecuv4U/8U/16U/32U + ecuv4V/8V/16V/32V + ecuv0U/ecuv0V bytes,
    // ecuv_cdf_eq 1 (composition.c svtd_cs1_uv_txb_drive). The six
    // component-dim CDF families thread component_type through
    // writeTxbCoeffs/readTxbCoeffs (entropy_coding.c:355-544); the tx-type
    // symbol is LUMA-ONLY (entropy_coding.c:374-376 - chroma TUs emit no
    // tx-type symbol; without that gate the byte streams would diverge);
    // the chroma txb_skip_ctx rotation {7,8,9,7} is the :310-314 branch
    // (ctx_base + offset 7). Byte arrays mechanically emitted from
    // expected_primitives.txt (emit_ecuv_arrays.ps1); no hand-typed values.
    static const entropy::TxSize ts[4] = {entropy::TX_4X4, entropy::TX_8X8, entropy::TX_16X16,
                                          entropy::TX_32X32};
    static const int dims[4] = {16, 64, 256, 1024};
    static const int txbSkipCtxs[4] = {7, 8, 9, 7};
    static const unsigned char* const wantBytes[2][4] = {
        {ecuv4U_bytes, ecuv8U_bytes, ecuv16U_bytes, ecuv32U_bytes},
        {ecuv4V_bytes, ecuv8V_bytes, ecuv16V_bytes, ecuv32V_bytes}};
    static const int wantLen[2][4] = {{7, 20, 59, 194}, {7, 20, 59, 194}};
    static std::int16_t scans[4][1024];
    transforms::defaultScan4x4(scans[0]);
    transforms::defaultScan8x8(scans[1]);
    transforms::defaultScan16x16(scans[2]);
    transforms::defaultScan32x32(scans[3]);

    int cdfBad = 0;
    for (int comp = 0; comp < 2; ++comp) {
        const entropy::ComponentType component =
            comp ? entropy::COMPONENT_CHROMA : entropy::COMPONENT_LUMA;
        for (int t = 0; t < 4; ++t) {
            const int n = dims[t];
            const int eob = n / 2;
            std::int32_t qc[1024] = {0};
            cs1bFillQc(qc, scans[t], eob, comp);

            entropy::EcFrameContext fc;
            entropy::initDefaultEcFrameContext(&fc, 100);
            entropy::EcFrameContext fcR;
            entropy::initDefaultEcFrameContext(&fcR, 100);

            entropy::AomWriter w{};
            unsigned char buf[256] = {0};
            w.ec.buf = buf;
            entropy::odEcEncReset(&w.ec);
            w.allow_update_cdf = 1;
            w.pos = 0;
            entropy::writeTxbCoeffs(&w, &fc, qc, scans[t], ts[t], eob, txbSkipCtxs[t], 0, 1,
                                    entropy::DC_PRED, component);
            entropy::odEcStopEncode(&w);
            REQUIRE(w.pos == wantLen[comp][t]);
            for (int i = 0; i < wantLen[comp][t]; ++i) CHECK((unsigned)buf[i] == wantBytes[comp][t][i]);

            entropy::AomReader r;
            REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
            r.allow_update_cdf = 1;
            std::int32_t rc[1024];
            memset(rc, 0, sizeof(rc));
            const int reob = entropy::readTxbCoeffs(&r, &fcR, rc, scans[t], ts[t], txbSkipCtxs[t], 0, 1,
                                                     entropy::DC_PRED, component);
            CHECK(reob == eob);
            for (int i = 0; i < n; ++i) CHECK(rc[i] == qc[i]);
            if (entropy::ecFrameCdfsEqual(&fc, &fcR) != 1) cdfBad = 1;
        }
        // eob == 0 (txb_skip-only) case: TX_4X4, txb_skip_ctx 8.
        std::int32_t zeroQc[16] = {0};
        entropy::EcFrameContext fc0;
        entropy::initDefaultEcFrameContext(&fc0, 100);
        entropy::EcFrameContext fc0R;
        entropy::initDefaultEcFrameContext(&fc0R, 100);
        entropy::AomWriter w0{};
        unsigned char buf0[8] = {0};
        w0.ec.buf = buf0;
        entropy::odEcEncReset(&w0.ec);
        w0.allow_update_cdf = 1;
        w0.pos = 0;
        entropy::writeTxbCoeffs(&w0, &fc0, zeroQc, scans[0], entropy::TX_4X4, 0, 8, 0, 1,
                                entropy::DC_PRED, component);
        entropy::odEcStopEncode(&w0);
        REQUIRE(w0.pos == 1);
        CHECK((unsigned)buf0[0] == (comp ? ecuv0V_bytes[0] : ecuv0U_bytes[0]));
        entropy::AomReader r0;
        REQUIRE(entropy::odEcReaderInit(&r0, buf0, w0.pos) == 0);
        r0.allow_update_cdf = 1;
        std::int32_t rc0[16] = {0};
        const int reob0 = entropy::readTxbCoeffs(&r0, &fc0R, rc0, scans[0], entropy::TX_4X4, 8, 0, 1,
                                                 entropy::DC_PRED, component);
        CHECK(reob0 == 0);
        if (entropy::ecFrameCdfsEqual(&fc0, &fc0R) != 1) cdfBad = 1;
    }
    CHECK(cdfBad == 0);
}

TEST_CASE("chroma per-block wrapper derives txb_skip_ctx offset 7 (CS1b)") {
    // The :310-314 branch made live: writeBlockCoeffs/readBlockCoeffs route
    // component_type into getTxbCtx's plane arg (chroma -> PLANE_TYPE_UV).
    // Discrimination: with a poisoned NA (a coded chroma neighbor above:
    // nonzero packed byte 0x01; left INVALID) the chroma wrapper must derive
    // ctx = (left!=0) + (top!=0) + 7 = 8 - its byte stream must EQUAL a
    // direct writeTxbCoeffs call at txb_skip_ctx 8. The luma routing
    // (plane 0, plane_bsize == tx_bsize -> ctx 0) would diverge.
    // Plus the getTxbCtx chroma domain enumerated directly: ctx_base over
    // {top,left} in {INVALID, 0x00, nonzero} (ctx 7..9), the dc_sign_ctx
    // 3x3 sign grid, and the offset-10 characterization rows (plane_bsize >
    // tx_bsize; dead for whole-block TUs, ported per the range rule).
    std::int16_t scan4[16];
    transforms::defaultScan4x4(scan4);
    std::int32_t qc[16] = {0};
    qc[scan4[0]] = 3;
    qc[scan4[1]] = -2;
    qc[scan4[2]] = 5;
    qc[scan4[3]] = 2;
    const int eob = 4;

    entropy::EcFrameContext fcW;
    entropy::initDefaultEcFrameContext(&fcW, 100);
    entropy::DcSignLevelCoeffNa naW;
    memset(&naW, 0xFF, sizeof(naW));
    naW.above[0] = 0x01;  // coded chroma neighbor above (nonzero cul, no sign)
    entropy::AomWriter w{};
    unsigned char buf[16] = {0};
    w.ec.buf = buf;
    entropy::odEcEncReset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;
    entropy::writeBlockCoeffs(&w, &fcW, &naW, qc, scan4, entropy::TX_4X4, entropy::BLOCK_4X4, eob,
                              0, 0, 1, entropy::DC_PRED, entropy::COMPONENT_CHROMA);
    entropy::odEcStopEncode(&w);

    entropy::EcFrameContext fcD;
    entropy::initDefaultEcFrameContext(&fcD, 100);
    entropy::AomWriter wd{};
    unsigned char bufD[16] = {0};
    wd.ec.buf = bufD;
    entropy::odEcEncReset(&wd.ec);
    wd.allow_update_cdf = 1;
    wd.pos = 0;
    entropy::writeTxbCoeffs(&wd, &fcD, qc, scan4, entropy::TX_4X4, eob, 8, 0, 1, entropy::DC_PRED,
                            entropy::COMPONENT_CHROMA);
    entropy::odEcStopEncode(&wd);
    REQUIRE(w.pos == wd.pos);
    for (unsigned i = 0; i < w.pos; ++i) CHECK((unsigned)buf[i] == (unsigned)bufD[i]);

    // reader twin with the same poisoned NA (fresh): derives ctx 8 and
    // round-trips; the NA update writes the packed byte over the TU extent.
    entropy::EcFrameContext fcR;
    entropy::initDefaultEcFrameContext(&fcR, 100);
    entropy::DcSignLevelCoeffNa naR;
    memset(&naR, 0xFF, sizeof(naR));
    naR.above[0] = 0x01;
    entropy::AomReader r;
    REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
    r.allow_update_cdf = 1;
    std::int32_t rc[16] = {0};
    const int reob = entropy::readBlockCoeffs(&r, &fcR, &naR, rc, scan4, entropy::TX_4X4,
                                              entropy::BLOCK_4X4, 0, 0, 1, entropy::DC_PRED,
                                              entropy::COMPONENT_CHROMA);
    CHECK(reob == eob);
    for (int i = 0; i < 16; ++i) CHECK(rc[i] == qc[i]);
    CHECK(entropy::ecFrameCdfsEqual(&fcW, &fcR) == 1);

    // luma equivalence: all-INVALID NA -> ctx 0 (the whole-block luma rule)
    entropy::EcFrameContext fcL;
    entropy::initDefaultEcFrameContext(&fcL, 100);
    entropy::DcSignLevelCoeffNa naL;
    memset(&naL, 0xFF, sizeof(naL));
    entropy::AomWriter wl{};
    unsigned char bufL[16] = {0};
    wl.ec.buf = bufL;
    entropy::odEcEncReset(&wl.ec);
    wl.allow_update_cdf = 1;
    wl.pos = 0;
    entropy::writeBlockCoeffs(&wl, &fcL, &naL, qc, scan4, entropy::TX_4X4, entropy::BLOCK_4X4, eob,
                              0, 0, 1, entropy::DC_PRED, entropy::COMPONENT_LUMA);
    entropy::odEcStopEncode(&wl);
    entropy::EcFrameContext fcLD;
    entropy::initDefaultEcFrameContext(&fcLD, 100);
    entropy::AomWriter wld{};
    unsigned char bufLD[16] = {0};
    wld.ec.buf = bufLD;
    entropy::odEcEncReset(&wld.ec);
    wld.allow_update_cdf = 1;
    wld.pos = 0;
    entropy::writeTxbCoeffs(&wld, &fcLD, qc, scan4, entropy::TX_4X4, eob, 0, 0, 1,
                            entropy::DC_PRED, entropy::COMPONENT_LUMA);
    entropy::odEcStopEncode(&wld);
    REQUIRE(wl.pos == wld.pos);
    for (unsigned i = 0; i < wl.pos; ++i) CHECK((unsigned)bufL[i] == (unsigned)bufLD[i]);

    // getTxbCtx chroma domain, enumerated (plane_bsize == tx_bsize -> the
    // offset-7 branch): ctx_base = (left!=0) + (top!=0), INVALID -> absent.
    struct CtxCase {
        std::uint8_t above;
        std::uint8_t left;
        int wantCtx;
        int wantSign;
    };
    static const CtxCase cases[7] = {
        {0xFF, 0xFF, 7, 0}, {0x01, 0xFF, 8, 0}, {0xFF, 0x01, 8, 0}, {0x01, 0x01, 9, 0},
        {0x40, 0x80, 9, 0}, {0x80, 0x00, 8, 2}, {0x00, 0x40, 8, 1},
    };
    for (int ci = 0; ci < 7; ++ci) {
        int ctx = -1, sign = -1;
        entropy::getTxbCtx(&cases[ci].above, &cases[ci].left, 1, 1, 1, entropy::BLOCK_4X4,
                           entropy::TX_4X4, &ctx, &sign);
        CHECK(ctx == cases[ci].wantCtx);
        CHECK(sign == cases[ci].wantSign);
    }
    // offset-10 characterization rows (plane_bsize 8x8 > tx 4x4): ctx 10..12.
    int ctx10 = -1, sign10 = -1;
    entropy::getTxbCtx(&cases[3].above, &cases[3].left, 1, 1, 1, entropy::BLOCK_8X8,
                       entropy::TX_4X4, &ctx10, &sign10);
    CHECK(ctx10 == 12);
}

// ---------------------------------------------------------------------------
// SYM1 (SY1): the palette + segmentation CDF families land in the frame
// context. Defaults are the mechanical extracts (cabac_context_model.c
// :477-:509, :511-:586, :652-:664); the spot rows below quote those lines.
// ---------------------------------------------------------------------------
TEST_CASE("palette + seg CDF defaults land in initDefaultEcFrameContext") {
    entropy::EcFrameContext fc;
    entropy::initDefaultEcFrameContext(&fc, 0);
    // palette_y_size_cdf[0]: AOM_CDF7(7952, 13000, 18149, 21478, 25527, 29241)
    // - 6 explicit probs (indices 0..5), the 7-symbol zero tail at [6].
    CHECK(fc.palette_y_size_cdf[0][0] == AOM_ICDF(7952));
    CHECK(fc.palette_y_size_cdf[0][5] == AOM_ICDF(29241));
    CHECK(fc.palette_y_size_cdf[0][6] == 0);
    // palette_y_size_cdf[6] (last row): ..., 28999
    CHECK(fc.palette_y_size_cdf[6][5] == AOM_ICDF(28999));
    // palette_uv_size_cdf[6]: ..., 25865
    CHECK(fc.palette_uv_size_cdf[6][5] == AOM_ICDF(25865));
    // palette_y_mode_cdf[0][0]: AOM_CDF2(31676)
    CHECK(fc.palette_y_mode_cdf[0][0][0] == AOM_ICDF(31676));
    // palette_uv_mode_cdf[1]: AOM_CDF2(21488)
    CHECK(fc.palette_uv_mode_cdf[1][0] == AOM_ICDF(21488));
    // palette_y_color_index_cdf[0][0]: AOM_CDF2(28710) - one explicit prob,
    // the 2-symbol zero tail at [1], counter cell at [8].
    CHECK(fc.palette_y_color_index_cdf[0][0][0] == AOM_ICDF(28710));
    CHECK(fc.palette_y_color_index_cdf[0][0][1] == 0);
    // palette_uv_color_index_cdf[6][4]: AOM_CDF8(..., 32322) - 7th explicit
    CHECK(fc.palette_uv_color_index_cdf[6][4][6] == AOM_ICDF(32322));
    CHECK(fc.palette_uv_color_index_cdf[6][4][7] == 0);
    // seg.tree_cdf: AOM_CDF8(4096, 8192, 12288, 16384, 20480, 24576, 28672)
    CHECK(fc.seg.tree_cdf[0] == AOM_ICDF(4096));
    CHECK(fc.seg.tree_cdf[6] == AOM_ICDF(28672));
    CHECK(fc.seg.tree_cdf[7] == 0);
    // seg.pred_cdf: AOM_CDF2(128 * 128)
    CHECK(fc.seg.pred_cdf[0][0] == AOM_ICDF(128 * 128));
    // seg.spatial_pred_seg_cdf[2]: AOM_CDF8(..., 32679)
    CHECK(fc.seg.spatial_pred_seg_cdf[2][6] == AOM_ICDF(32679));
}

// SY1b: ecFrameCdfsEqual covers the palette + seg families (a one-cell
// mutation in each must read unequal - the bit-equality contract).
TEST_CASE("ecFrameCdfsEqual covers the palette + seg families") {
    entropy::EcFrameContext a, b;
    entropy::initDefaultEcFrameContext(&a, 0);
    b = a;
    CHECK(entropy::ecFrameCdfsEqual(&a, &b) == 1);
    b.palette_y_size_cdf[3][2] = 1234;
    CHECK(entropy::ecFrameCdfsEqual(&a, &b) == 0);
    b = a;
    b.palette_uv_color_index_cdf[1][1][1] = 4321;
    CHECK(entropy::ecFrameCdfsEqual(&a, &b) == 0);
    b = a;
    b.seg.spatial_pred_seg_cdf[1][0] = 777;
    CHECK(entropy::ecFrameCdfsEqual(&a, &b) == 0);
}

// SYM1 (SY2): svt_aom_allow_palette (entropy_coding.c:4218-4223) and
// svt_aom_get_palette_bsize_ctx (:4224-4231) over the ENUMERATED bsize set
// (all 22; the ctx values cite eb_num_pels_log2_lookup, common_utils.c:39).
TEST_CASE("allowPalette + getPaletteBsizeCtx over the bsize set") {
    // allow=0 -> 0 for every bsize (the && chain).
    for (int b = 0; b < entropy::BLOCK_SIZES_ALL; ++b) {
        CHECK(entropy::allowPalette(0, (entropy::BlockSize)b) == 0);
    }
    // allow=1: wide<=64 && high<=64 && bsize>=BLOCK_8X8 - the >= is ENUM
    // order, not geometry: BLOCK_4X16/BLOCK_16X4 are enum-appended (16, 17)
    // and PASS the gate despite being 4-px-tall blocks (the reference's own
    // quirk, enumerated here).
    static const int want[entropy::BLOCK_SIZES_ALL] = {
        0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1};
    for (int b = 0; b < entropy::BLOCK_SIZES_ALL; ++b) {
        CHECK(entropy::allowPalette(1, (entropy::BlockSize)b) == want[b]);
    }
    // get_palette_bsize_ctx = eb_num_pels_log2_lookup[bsize] - 6, EVERY value
    // the function admits (the 4x4-family negatives are reachable values of
    // the pure function; the allow_palette gate keeps callers above 8x8).
    static const int wantCtx[entropy::BLOCK_SIZES_ALL] = {
        -2, -1, -1, 0, 1, 1, 2, 3, 3, 4, 5, 5, 6, 7, 7, 8, 0, 0, 2, 2, 4, 4};
    for (int b = 0; b < entropy::BLOCK_SIZES_ALL; ++b) {
        CHECK(entropy::getPaletteBsizeCtx((entropy::BlockSize)b) == wantCtx[b]);
    }
}

// SYM1 (SY3): svt_aom_get_palette_mode_ctx (entropy_coding.c:236-4249,
// i.e. :4236-:4249) - the MacroBlockD wall threaded: the two consumed
// neighbor facts are (available, palette_size) pairs. Enumerated 2x2
// availability grid x the size {0, 3} contrast.
TEST_CASE("getPaletteModeCtx over the availability grid") {
    CHECK(entropy::getPaletteModeCtx(0, 0, 0, 0) == 0);
    CHECK(entropy::getPaletteModeCtx(1, 3, 0, 0) == 1);
    CHECK(entropy::getPaletteModeCtx(0, 0, 1, 3) == 1);
    CHECK(entropy::getPaletteModeCtx(1, 3, 1, 3) == 2);
    // The > 0 test, not the size itself: size 0 available contributes 0.
    CHECK(entropy::getPaletteModeCtx(1, 0, 1, 0) == 0);
}

// SYM1 (SY4): delta_encode_palette_colors (entropy_coding.c:4252-4282), the
// WRITER twin of the landed l13_palette::deltaEncodeCost (palette.c:80-109 -
// the same reference function counts what the writer emits). Fixture: the
// writer's literal-bit count == the cost formula over sampled chains.
namespace {
// av1_ceil_log2 (cabac_context_model.h:366-371, svt_log2f = get_msb).
int tCeilLog2(int n) {
    if (n < 2) return 0;
    int v = n - 1, log = 0;
    while (v) { ++log; v >>= 1; }
    return log;
}
// The deltaEncodeCost arithmetic (palette.c:80-109; transcribed from the
// landed src/l13_palette/palette.cpp deltaEncodeCost - the cross-layer
// agreement oracle).
int costFormula(const int* colors, int num, int bitDepth, int minVal) {
    if (num <= 0) return 0;
    int bits_cost = bitDepth;
    if (num == 1) return bits_cost;
    bits_cost += 2;
    int max_delta = 0;
    int deltas[PALETTE_MAX_SIZE];
    const int min_bits = bitDepth - 3;
    for (int i = 1; i < num; ++i) {
        const int delta = colors[i] - colors[i - 1];
        deltas[i - 1] = delta;
        if (delta > max_delta) max_delta = delta;
    }
    int bits_per_delta = tCeilLog2(max_delta + 1 - minVal);
    if (min_bits > bits_per_delta) bits_per_delta = min_bits;
    int range = (1 << bitDepth) - colors[0] - minVal;
    for (int i = 0; i < num - 1; ++i) {
        bits_cost += bits_per_delta;
        range -= deltas[i];
        const int rl = tCeilLog2(range);
        if (rl < bits_per_delta) bits_per_delta = rl;
    }
    return bits_cost;
}
} // namespace

TEST_CASE("deltaEncodePaletteColors bit count == the deltaEncodeCost formula") {
    static const int chains[4][8] = {
        {10, 50, 60, 200, 0, 0, 0, 0},       // num 4, growing + jump
        {3, 3, 3, 0, 0, 0, 0, 0},            // num 3, zero deltas (min_val 1!)
        {128, 129, 131, 136, 200, 255, 0, 0},// num 6, tight then wide
        {0, 1, 0, 0, 0, 0, 0, 0},            // num 2, min_val 1 boundary
    };
    static const int nums[4] = {4, 3, 6, 2};
    for (int tcase = 0; tcase < 4; ++tcase) {
        entropy::AomWriter w{};
        unsigned char buf[64] = {0};
        w.ec.buf = buf;
        entropy::odEcEncReset(&w.ec);
        w.allow_update_cdf = 0;
        w.pos = 0;
        const int before = entropy::odEcEncTell(&w.ec);
        entropy::deltaEncodePaletteColors(chains[tcase], nums[tcase], 8, 1, &w);
        const int written = entropy::odEcEncTell(&w.ec) - before;
        entropy::odEcStopEncode(&w);
        CHECK(written == costFormula(chains[tcase], nums[tcase], 8, 1));
        // num 1 / num 0 degenerates (the early returns).
        entropy::odEcEncReset(&w.ec);
        const int b1 = entropy::odEcEncTell(&w.ec);
        entropy::deltaEncodePaletteColors(chains[tcase], 1, 8, 1, &w);
        CHECK(entropy::odEcEncTell(&w.ec) - b1 == 8);
        entropy::odEcEncReset(&w.ec);
        const int b0 = entropy::odEcEncTell(&w.ec);
        entropy::deltaEncodePaletteColors(chains[tcase], 0, 8, 1, &w);
        CHECK(entropy::odEcEncTell(&w.ec) - b0 == 0);
    }
}

// SYM1 (SY5): get_unsigned_bits (entropy_coding.c:4284-4286),
// write_uniform (:4288-4302) + its cost twin svt_aom_write_uniform_cost
// (:4304-4314), and the aom reader twin av1_read_uniform (aom
// decoder.h:488-496). Enumerated (n, v) grid, write->read roundtrip.
TEST_CASE("writeUniform/readUniform grid roundtrip + cost twin") {
    for (int n = 1; n <= 16; ++n) {
        const int l = entropy::getUnsignedBits((unsigned)n);
        const int m = (1 << l) - n;
        for (int v = 0; v < n; ++v) {
            // The integer od_ec tell lags on short streams (the first
            // half-prob literal from a fresh encoder accounts 2), so the
            // bit-COUNT instrument here is the desync canary: a second,
            // fixed write whose decode breaks if the first wrote the wrong
            // number of bits. Content + count ride on one stream.
            entropy::AomWriter w{};
            unsigned char buf[16] = {0};
            w.ec.buf = buf;
            entropy::odEcEncReset(&w.ec);
            w.allow_update_cdf = 0;
            w.pos = 0;
            entropy::writeUniform(&w, n, v);
            entropy::writeUniform(&w, 16, 15);  // the canary
            entropy::odEcStopEncode(&w);
            // The cost twin (av1_cost_literal = n << 9, AV1_PROB_COST_SHIFT 9).
            CHECK(entropy::writeUniformCost(n, v) == (v < m ? (l - 1) << 9 : l << 9));
            // Reader twin roundtrip: value then canary.
            entropy::AomReader r;
            REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
            r.allow_update_cdf = 0;
            CHECK(entropy::readUniform(&r, n) == v);
            CHECK(entropy::readUniform(&r, 16) == 15);
        }
    }
    // get_unsigned_bits degenerates: 0 -> 0 (the reference's ternary).
    CHECK(entropy::getUnsignedBits(0) == 0);
    CHECK(entropy::getUnsignedBits(1) == 1);
    CHECK(entropy::getUnsignedBits(8) == 4);
}

// SYM1 (SY6): write_palette_colors_y (entropy_coding.c:4320-4337, the
// svt_get_palette_cache_y wall threaded as the cache args) + the aom
// read_palette_colors_y reader twin (decodemv.c:478-507). Sizes {2, 4, 8},
// the cache-hit paths incl. the all-cached early stop (merge_colors /
// memcpy) and the empty-cache chain.
TEST_CASE("writePaletteColorsY/readPaletteColorsY roundtrip + cache paths") {
    struct Case {
        int n;
        int nCache;
        std::uint16_t cache[2 * PALETTE_MAX_SIZE];
        std::uint16_t colors[PALETTE_MAX_SIZE];
    };
    static const Case cases[5] = {
        // size 2, empty cache
        {2, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {10, 50, 0, 0, 0, 0, 0, 0}},
        // size 4, cache of 3: two hits then a miss
        {4, 3, {5, 100, 200, 0, 0, 0}, {5, 100, 130, 250, 0, 0, 0, 0}},
        // size 8, all 8 in a 4-entry cache: early stop + the memcpy branch
        {8, 4, {2, 9, 20, 60, 0, 0, 0, 0},
         {2, 9, 20, 60, 100, 150, 200, 250}},
        // size 8, partial cache: hits at both cache slots (sorted in/out)
        {8, 2, {9, 150, 0, 0}, {2, 9, 20, 60, 100, 150, 200, 250}},
        // size 8, empty cache: the full 6-delta chain
        {8, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {1, 2, 5, 30, 31, 90, 200, 255}},
    };
    for (int tc = 0; tc < 5; ++tc) {
        entropy::AomWriter w{};
        unsigned char buf[64] = {0};
        w.ec.buf = buf;
        entropy::odEcEncReset(&w.ec);
        w.allow_update_cdf = 0;
        w.pos = 0;
        entropy::writePaletteColorsY(&w, 8, cases[tc].cache, cases[tc].nCache, cases[tc].colors,
                                     cases[tc].n);
        entropy::odEcStopEncode(&w);
        entropy::AomReader r;
        REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
        r.allow_update_cdf = 0;
        std::uint16_t got[PALETTE_MAX_SIZE];
        entropy::readPaletteColorsY(&r, 8, cases[tc].cache, cases[tc].nCache, got, cases[tc].n);
        for (int i = 0; i < cases[tc].n; ++i) {
            CHECK(got[i] == cases[tc].colors[i]);
        }
    }
}

// SYM1 (SY7): write_palette_mode_info (entropy_coding.c:4351-4377) threaded
// (the pcs/mbmi/blk_ptr walls as parameters; is_chroma_reference threaded -
// the l6 predicate) + the aom read_palette_mode_info reader twin
// (decodemv.c:567-604, the uv modev=1 continuation parked - the SVT writer
// emits no uv palette, entropy_coding.c:4371 asserts palette_size[1]==0).
// The roundtrip also pins CDF adaptation: writer-side and reader-side
// adaptations must leave identical frame contexts.
TEST_CASE("writePaletteModeInfo/readPaletteModeInfo roundtrip") {
    struct Case {
        entropy::PredictionMode lumaMode;
        entropy::UvPredictionMode chromaMode;
        int isChromaRef;
        int sizeY;                 // 0 = no y palette
        int nCache;
        std::uint16_t cache[2 * PALETTE_MAX_SIZE];
    };
    static const std::uint16_t colors8[PALETTE_MAX_SIZE] = {2, 9, 20, 60, 100, 150, 200, 250};
    static const Case cases[5] = {
        {entropy::DC_PRED, entropy::UV_DC_PRED, 1, 2, 0, {0}},
        {entropy::DC_PRED, entropy::UV_DC_PRED, 1, 5, 2, {9, 150, 0, 0}},
        {entropy::DC_PRED, entropy::UV_DC_PRED, 1, 8, 4, {2, 9, 20, 60, 0, 0, 0, 0}},
        {entropy::V_PRED, entropy::UV_DC_PRED, 1, 0, 0, {0}},   // no y symbols
        {entropy::DC_PRED, entropy::UV_V_PRED, 1, 3, 0, {0}},   // no uv symbol
    };
    for (int tc = 0; tc < 5; ++tc) {
        entropy::EcFrameContext fcW, fcR;
        entropy::initDefaultEcFrameContext(&fcW, 0);
        fcR = fcW;
        entropy::AomWriter w{};
        unsigned char buf[64] = {0};
        w.ec.buf = buf;
        entropy::odEcEncReset(&w.ec);
        w.allow_update_cdf = 1;
        w.pos = 0;
        std::uint16_t colors[PALETTE_MAX_SIZE];
        memcpy(colors, colors8, sizeof(colors));
        entropy::writePaletteModeInfo(&w, &fcW, entropy::BLOCK_16X16, 8, cases[tc].lumaMode,
                                      cases[tc].chromaMode, cases[tc].isChromaRef, cases[tc].sizeY,
                                      colors, cases[tc].cache, cases[tc].nCache, 1, 1, 0, 0);
        entropy::odEcStopEncode(&w);
        entropy::AomReader r;
        REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
        r.allow_update_cdf = 1;
        std::uint16_t got[PALETTE_MAX_SIZE];
        const int gotSize = entropy::readPaletteModeInfo(&r, &fcR, entropy::BLOCK_16X16, 8,
                                                         cases[tc].lumaMode, cases[tc].chromaMode,
                                                         cases[tc].isChromaRef, cases[tc].cache,
                                                         cases[tc].nCache, 1, 1, 0, 0, got);
        CHECK(gotSize == cases[tc].sizeY);
        for (int i = 0; i < cases[tc].sizeY; ++i) CHECK(got[i] == colors[i]);
        // CDF adaptation in lockstep (fixture (e)).
        CHECK(entropy::ecFrameCdfsEqual(&fcW, &fcR) == 1);
    }
}

// SYM1 (SY8a): the l7 twin of the fast palette color-index context
// (palette.c:639-776; the landed l13_palette twin cannot be linked - no
// CMake edits - so l7 carries its own copy; both port the same spans and
// the fixtures below mirror test_palette.cpp's).
TEST_CASE("l7 fastPaletteColorIndexContext twin: edge + interior + dup branches") {
    int idx = -1;
    const std::uint8_t mapE[4] = {5, 7, 9, 11};  // stride 2
    CHECK(entropy::fastPaletteColorIndexContextOnEdge(mapE, 2, 0, 1, &idx) == 0);
    CHECK(idx == 7);  // left 5 < current 7: unchanged
    idx = -1;
    CHECK(entropy::fastPaletteColorIndexContextOnEdge(mapE, 2, 1, 0, &idx) == 0);
    CHECK(idx == 9);  // above 5 < current 9: unchanged
    const std::uint8_t mapUp[4] = {7, 5, 0, 0};
    idx = -1;
    CHECK(entropy::fastPaletteColorIndexContextOnEdge(mapUp, 2, 0, 1, &idx) == 0);
    CHECK(idx == 6);  // left 7 > current 5: +1
    const std::uint8_t mapEq[4] = {3, 3, 0, 0};
    idx = -1;
    CHECK(entropy::fastPaletteColorIndexContextOnEdge(mapEq, 2, 0, 1, &idx) == 0);
    CHECK(idx == 0);  // left == current: 0
    // All-distinct interior: hash 8 -> ctx 1, color_idx unchanged (8).
    const std::uint8_t map[6] = {5, 2, 0, 6, 8, 0};
    idx = -1;
    CHECK(entropy::fastPaletteColorIndexContext(map, 3, 1, 1, &idx) == 1);
    CHECK(idx == 8);
    // Interior edge delegate (0,1): only a left neighbor.
    const std::uint8_t edgeMap[6] = {5, 7, 0, 0, 0, 0};
    idx = -1;
    CHECK(entropy::fastPaletteColorIndexContext(edgeMap, 3, 0, 1, &idx) == 0);
    CHECK(idx == 7);
    // Duplicate aggregation, branch (a) left == top (scores[0] = 4, the
    // INVALID shift pulls tl into slot 1): ctx 3, walk 7>4 -> 5, 5>4 -> 6.
    const std::uint8_t mapA[6] = {5, 7, 9, 7, 4, 0};
    idx = -1;
    CHECK(entropy::fastPaletteColorIndexContext(mapA, 3, 1, 1, &idx) == 3);
    CHECK(idx == 6);
    // Branch (b) left == topleft (scores[0] = 3, top stays): ctx 2,
    // walk 6>3 -> 4, 8>3 -> 5.
    const std::uint8_t mapB[6] = {6, 8, 9, 6, 3, 0};
    idx = -1;
    CHECK(entropy::fastPaletteColorIndexContext(mapB, 3, 1, 1, &idx) == 2);
    CHECK(idx == 5);
}

// SYM1 (SY8b): the color-map token chain. Writer: svt_av1_tokenize_color_map
// (palette.c:781-828, the Av1ColorMapParam wall threaded) + pack_map_tokens
// (entropy_coding.c:4339-4348). Reader: aom decode_color_map_tokens
// (detokenize.c:25-46) over av1_get_palette_color_index_context
// (entropymode.c:893-951) + av1_read_uniform. The roundtrip also proves the
// SVT fast context and the aom full context agree on (ctx, symbol/order).
TEST_CASE("palette color-map token chain roundtrip") {
    struct TCase {
        int n;
        int rows;
        int cols;
        int planeW;
        int planeH;
        std::uint8_t map[64];
    };
    static const TCase cases[4] = {
        {2, 4, 4, 4, 4,
         {0, 1, 0, 1,
          1, 1, 0, 0,
          0, 1, 1, 1,
          1, 0, 1, 0}},
        {4, 4, 5, 5, 4,
         {0, 1, 2, 3, 1,
          3, 2, 1, 0, 2,
          0, 0, 1, 1, 3,
          2, 3, 3, 2, 0}},
        {8, 8, 8, 8, 8,
         {0, 1, 2, 3, 4, 5, 6, 7,
          7, 6, 5, 4, 3, 2, 1, 0,
          0, 0, 1, 1, 2, 2, 3, 3,
          4, 4, 5, 5, 6, 6, 7, 7,
          1, 3, 5, 7, 0, 2, 4, 6,
          2, 2, 2, 2, 6, 6, 6, 6,
          7, 0, 7, 0, 7, 0, 7, 0,
          3, 1, 4, 1, 5, 2, 2, 6}},
        // the padding tail: 3x3 coded on a 5x4 plane (planeW 5, planeH 4)
        {4, 3, 3, 5, 4,
         {0, 1, 2, 9, 9,
          1, 2, 3, 9, 9,
          2, 3, 0, 9, 9,
          9, 9, 9, 9, 9}},
    };
    for (int tc = 0; tc < 4; ++tc) {
        entropy::EcFrameContext fcW, fcR;
        entropy::initDefaultEcFrameContext(&fcW, 0);
        fcR = fcW;
        entropy::TokenExtra tok[128];
        const int nTok = entropy::tokenizeColorMap(&fcW, cases[tc].map, cases[tc].planeW, cases[tc].rows,
                                                   cases[tc].cols, cases[tc].n, 0, 0, tok);
        CHECK(nTok == cases[tc].rows * cases[tc].cols);
        entropy::AomWriter w{};
        unsigned char buf[512] = {0};
        w.ec.buf = buf;
        entropy::odEcEncReset(&w.ec);
        w.allow_update_cdf = 1;
        w.pos = 0;
        entropy::packMapTokens(&w, tok, cases[tc].n, cases[tc].rows * cases[tc].cols);
        entropy::odEcStopEncode(&w);
        entropy::AomReader r;
        REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
        r.allow_update_cdf = 1;
        std::uint8_t got[64];
        memset(got, 0xAA, sizeof(got));
        entropy::decodeColorMapTokens(&r, got, cases[tc].planeW, cases[tc].planeH, cases[tc].rows,
                                      cases[tc].cols, cases[tc].n, 0, &fcR);
        // Coded cells: exact. Padded columns (c >= cols, r < rows): the
        // decoder's last-column copy (the map's own last coded value).
        // Padded rows (r >= rows): the last CODED row is copied WHOLE
        // (its own padded columns already filled), so the oracle reads the
        // reconstructed row itself.
        for (int rr = 0; rr < cases[tc].planeH; ++rr) {
            for (int cc = 0; cc < cases[tc].planeW; ++cc) {
                int want;
                if (rr < cases[tc].rows) {
                    want = (cc < cases[tc].cols)
                               ? cases[tc].map[rr * cases[tc].planeW + cc]
                               : cases[tc].map[rr * cases[tc].planeW + (cases[tc].cols - 1)];
                } else {
                    want = got[(cases[tc].rows - 1) * cases[tc].planeW + cc];
                }
                if (got[rr * cases[tc].planeW + cc] != want) {
                    fprintf(stderr, "MISMATCH tc=%d r=%d c=%d got=%d want=%d\n", tc, rr, cc,
                            (int)got[rr * cases[tc].planeW + cc], want);
                }
                CHECK(got[rr * cases[tc].planeW + cc] == want);
            }
        }
        // CDF adaptation in lockstep (fixture (e)).
        CHECK(entropy::ecFrameCdfsEqual(&fcW, &fcR) == 1);
    }
}

// SYM1 (SY9): the seg feature bits (encode_segmentation, entropy_coding.c:
// 2253-2280, threaded) + the aom setup_segmentation reader twin
// (decodeframe.c, the out-of-tree arbiter) over the enumerated feature/level
// grid. The wb trio is the l7 twin of the landed l8 copy (both port
// entropy_coding.c:1343-1382; l7 cannot link l8 - no CMake edits).
TEST_CASE("seg feature write/read grid roundtrip") {
    // segmentation_params.c:16/:18: signed {1,1,1,1,1,0,0,0},
    // bits {8,6,6,6,6,3,0,0}.
    for (int j = 0; j < SEG_LVL_MAX; ++j) {
        const int bits = entropy::segFeatureBits[j];
        const int hi = (1 << bits) - 1;   // the seg_feature_data_max span top
        // Data probes per the feature's sign: the extremes, the clamping
        // edge, and midpoints. Unsigned features take non-negative probes
        // only (a negative datum is not a legal input there).
        int probes[6];
        int nProbes;
        if (entropy::segFeatureSigned[j]) {
            probes[0] = -hi;
            probes[1] = -hi + 1;
            probes[2] = -1;
            probes[3] = 0;
            probes[4] = 1;
            probes[5] = hi;
            nProbes = 6;
        } else if (hi > 0) {
            probes[0] = 0;
            probes[1] = 1;
            probes[2] = hi / 2;
            probes[3] = hi;
            nProbes = 4;
        } else {  // the 0-bit features: only one probe
            probes[0] = 0;
            nProbes = 1;
        }
        for (int p = 0; p < nProbes; ++p) {
            entropy::AomWriteBitBuffer wb;
            std::uint8_t buf[64] = {0};
            wb.bit_buffer = buf;
            wb.bit_offset = 0;
            std::int16_t enabled[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
            std::int16_t data[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
            enabled[0][j] = 1;
            data[0][j] = (std::int16_t)probes[p];
            entropy::writeSegFeatures(&wb, 1, 1, 0, 1, 7 /*primary_ref_frame = NONE*/, enabled, data);
            entropy::AomReadBitBuffer rb;
            rb.bit_buffer = buf;
            rb.bit_buffer_end = buf + sizeof(buf);
            rb.bit_offset = 0;
            std::int16_t rEnabled[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
            std::int16_t rData[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
            entropy::readSegFeatures(&rb, 1, 0, 1, 7, rEnabled, rData);
            CHECK(rEnabled[0][j] == 1);
            // The reader clamps into [-max, max] (aom decodeframe.c); the
            // probes are within span so the values must agree exactly.
            CHECK((int)rData[0][j] == probes[p]);
            // The wb byte accounting: the reader consumed exactly what was
            // written (the feature grid + the four header bits per the
            // primary_ref_frame gate).
            CHECK(rb.bit_offset == wb.bit_offset);
        }
    }
    // The clamp semantics (the aom reader's own guard; the pinned writer
    // does NOT clamp - its in-file TODO). Through the writer's inv-signed
    // emission the ONLY reachable out-of-span readback is the bottom edge:
    // a datum of -(1 << bits) reads back as -256 and clamps to -255
    // (higher magnitudes wrap positive through the 9-bit pattern - the
    // inv-signed span is [-256, 255]; the clamp's reachable case is the
    // bottom edge only, which is what this pins).
    {
        entropy::AomWriteBitBuffer wb;
        std::uint8_t buf[64] = {0};
        wb.bit_buffer = buf;
        wb.bit_offset = 0;
        std::int16_t enabled[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
        std::int16_t data[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
        enabled[0][0] = 1; data[0][0] = -256;  // one past the legal span
        entropy::writeSegFeatures(&wb, 1, 0, 0, 1, 7, enabled, data);
        entropy::AomReadBitBuffer rb;
        rb.bit_buffer = buf;
        rb.bit_buffer_end = buf + sizeof(buf);
        rb.bit_offset = 0;
        std::int16_t rEnabled[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
        std::int16_t rData[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
        entropy::readSegFeatures(&rb, 0, 0, 1, 7, rEnabled, rData);
        CHECK(rData[0][0] == -255);
        CHECK(rb.bit_offset == wb.bit_offset);
    }
    // Segment row 7 + several features at once (the grid walk order).
    {
        entropy::AomWriteBitBuffer wb;
        std::uint8_t buf[64] = {0};
        wb.bit_buffer = buf;
        wb.bit_offset = 0;
        std::int16_t enabled[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
        std::int16_t data[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
        enabled[7][0] = 1; data[7][0] = -128;
        enabled[7][5] = 1; data[7][5] = 5;
        enabled[3][6] = 1; data[3][6] = 0;  // SEG_LVL_SKIP: 0 bits
        entropy::writeSegFeatures(&wb, 1, 0, 0, 1, 7, enabled, data);
        entropy::AomReadBitBuffer rb;
        rb.bit_buffer = buf;
        rb.bit_buffer_end = buf + sizeof(buf);
        rb.bit_offset = 0;
        std::int16_t rEnabled[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
        std::int16_t rData[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
        entropy::readSegFeatures(&rb, 0, 0, 1, 7, rEnabled, rData);
        CHECK(rEnabled[7][0] == 1); CHECK(rData[7][0] == -128);
        CHECK(rEnabled[7][5] == 1); CHECK(rData[7][5] == 5);
        CHECK(rEnabled[3][6] == 1); CHECK(rData[3][6] == 0);
        CHECK(rb.bit_offset == wb.bit_offset);
    }
    // The disabled frame: nothing written, the reader reads nothing.
    {
        entropy::AomWriteBitBuffer wb;
        std::uint8_t buf[8] = {0};
        wb.bit_buffer = buf;
        wb.bit_offset = 0;
        std::int16_t enabled[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
        std::int16_t data[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
        entropy::writeSegFeatures(&wb, 0, 0, 0, 0, 0, enabled, data);
        CHECK(wb.bit_offset == 1);  // only the enabled bit
        entropy::AomReadBitBuffer rb;
        rb.bit_buffer = buf;
        rb.bit_buffer_end = buf + sizeof(buf);
        rb.bit_offset = 0;
        std::int16_t rEnabled[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
        std::int16_t rData[MAX_SEGMENTS][SEG_LVL_MAX] = {{0}};
        entropy::readSegFeatures(&rb, 0, 0, 0, 0, rEnabled, rData);
        CHECK(rb.bit_offset == 1);
        CHECK(rEnabled[7][7] == 0);
    }
}

// SYM1 (SY10): the segmentation symbol surface. svt_aom_get_segment_id
// (entropy_coding.c:4723-4740), svt_av1_get_spatial_seg_prediction
// (:4744-4800), svt_av1_neg_interleave (:4802-4818) + the aom
// av1_neg_deinterleave twin (decodemv.c:261-277),
// svt_av1_update_segmentation_map (:4843-4861), write_segment_id
// (:4863-4883) + the aom read_segment_id reader twin (decodemv.c:280-303).
TEST_CASE("negInterleave/negDeinterleave inverse over the full grid") {
    for (int max = 1; max <= MAX_SEGMENTS; ++max) {
        for (int ref = 0; ref < max; ++ref) {
            for (int x = 0; x < max; ++x) {
                const int coded = entropy::negInterleave(x, ref, max);
                CHECK(entropy::negDeinterleave(coded, ref, max) == x);
            }
        }
    }
}

TEST_CASE("getSegmentId min-walk + updateSegmentationMap roundtrip") {
    // A 6x5 mi grid; BLOCK_8X8 (2x2 mi) blocks; the map holds per-mi ids.
    const int miCols = 6, miRows = 5;
    std::uint8_t ids[6 * 5];
    memset(ids, 0, sizeof(ids));
    // Write block (1,2) = id 4, block (3,2) = id 7 (partial at the edge).
    entropy::updateSegmentationMap(ids, miCols, miRows, entropy::BLOCK_8X8, 2, 2, 4);
    entropy::updateSegmentationMap(ids, miCols, miRows, entropy::BLOCK_8X8, 2, 4, 7);
    // Full-block read: the min over the 2x2 extent.
    CHECK(entropy::getSegmentId(ids, miCols, miRows, entropy::BLOCK_8X8, 2, 2) == 4);
    CHECK(entropy::getSegmentId(ids, miCols, miRows, entropy::BLOCK_8X8, 2, 4) == 7);
    // The edge-clamped read at (4,2): xmis = min(6-4, 2) = 2 covers cols 4-5;
    // the block extent reaches col 5 (id 7 from the (2,4) write).
    CHECK(entropy::getSegmentId(ids, miCols, miRows, entropy::BLOCK_8X8, 2, 4) == 7);
    // Untouched area -> 0.
    CHECK(entropy::getSegmentId(ids, miCols, miRows, entropy::BLOCK_16X16, 0, 0) == 0);
}

// SYM1 (SY10b): write_segment_id / read_segment_id roundtrip over a block
// walk (the raster emission order; the reader keeps its own map copy in
// lockstep), plus the skip_coeff path (no symbol; the prediction lands in
// both maps) and the CDF adaptation lockstep.
TEST_CASE("writeSegmentId/readSegmentId block-walk roundtrip") {
    const int miCols = 6, miRows = 5;
    std::uint8_t mapW[6 * 5] = {0};
    std::uint8_t mapR[6 * 5] = {0};
    entropy::EcFrameContext fcW, fcR;
    entropy::initDefaultEcFrameContext(&fcW, 0);
    fcR = fcW;
    entropy::AomWriter w{};
    unsigned char buf[64] = {0};
    w.ec.buf = buf;
    entropy::odEcEncReset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;
    struct B { int r, c, up, left, id, skip; };
    static const B blocks[6] = {
        {0, 0, 0, 0, 3, 0}, {0, 2, 0, 1, 0, 0}, {0, 4, 0, 1, 5, 1},
        {2, 0, 1, 0, 2, 0}, {2, 2, 1, 1, 7, 0}, {2, 4, 1, 1, 1, 0},
    };
    for (int b = 0; b < 6; ++b) {
        entropy::writeSegmentId(&w, &fcW, entropy::BLOCK_8X8, mapW, miCols, miRows, blocks[b].r,
                                blocks[b].c, blocks[b].up, blocks[b].left, blocks[b].id, 7,
                                blocks[b].skip);
    }
    entropy::odEcStopEncode(&w);
    entropy::AomReader r;
    REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
    r.allow_update_cdf = 1;
    for (int b = 0; b < 6; ++b) {
        if (blocks[b].skip) {
            // The skip path: no symbol; both sides take the prediction.
            int cdfNum;
            const int pred = entropy::getSpatialSegPrediction(mapR, miCols, miRows, blocks[b].up,
                                                              blocks[b].left, blocks[b].r,
                                                              blocks[b].c, &cdfNum);
            entropy::updateSegmentationMap(mapR, miCols, miRows, entropy::BLOCK_8X8, blocks[b].r,
                                           blocks[b].c, (std::uint8_t)pred);
            CHECK(pred == 0);  // prev_u unavailable -> the left neighbor's id
        } else {
            const int got = entropy::readSegmentId(&r, &fcR, entropy::BLOCK_8X8, mapR, miCols,
                                                   miRows, blocks[b].r, blocks[b].c, blocks[b].up,
                                                   blocks[b].left, 7);
            CHECK(got == blocks[b].id);
        }
    }
    CHECK(memcmp(mapW, mapR, sizeof(mapW)) == 0);
    CHECK(entropy::ecFrameCdfsEqual(&fcW, &fcR) == 1);
}
