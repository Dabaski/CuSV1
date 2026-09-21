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
    // for every q>0 TU — the DCT_DCT index through intra_ext_tx_cdf with
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

    entropy::writeTxbCoeffs(&w, &fc, qc16, scan16, entropy::TX_16X16, eob16, 0, 0, 1, entropy::DC_PRED);
    entropy::writeTxbCoeffs(&w, &fc, qc8, scan8, entropy::TX_8X8, eob8, 0, 0, 1, entropy::DC_PRED);
    entropy::writeTxbCoeffs(&w, &fc, qc4, scan4, entropy::TX_4X4, eob4, 0, 0, 1, entropy::DC_PRED);
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
    const int reob16 = entropy::readTxbCoeffs(&r, &fcR, rc16, scan16, entropy::TX_16X16, 0, 0, 1, entropy::DC_PRED);
    const int reob8 = entropy::readTxbCoeffs(&r, &fcR, rc8, scan8, entropy::TX_8X8, 0, 0, 1, entropy::DC_PRED);
    const int reob4 = entropy::readTxbCoeffs(&r, &fcR, rc4, scan4, entropy::TX_4X4, 0, 0, 1, entropy::DC_PRED);
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
    // always) — ported in l7 for the range rule but never exercised here.
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
                                  entropy::BLOCK_16X16, eob[b], mi[b][0], mi[b][1], 1, entropy::DC_PRED);
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
                                                  entropy::BLOCK_16X16, mi[b][0], mi[b][1], 1, entropy::DC_PRED);
        CHECK(reob == (int)eob[b]);
        for (int i = 0; i < 256; ++i) CHECK(rc[i] == qc[b][i]);
    }
    CHECK(entropy::ecFrameCdfsEqual(&fc, &fcR) == 1);
}