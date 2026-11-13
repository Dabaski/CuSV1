#include <doctest.h>

#include <cstdio>
#include <cstring>

#include "entropy.h"
#include "inter_symbols.h"

// A5E tolerance note: integer-only port, bit-exact vs the SVT C - the
// context getters are enumerated exhaustively against an independent
// oracle transcription in this file; the symbol writers roundtrip through
// the existing l7 reader primitives (odEcReadSymbol/odEcReadBit) with the
// cdf rows the context getters select, and the adapted-cdf state is
// asserted against an independent update_cdf transcription.

namespace {

// ---------------------------------------------------------------------------
// Independent oracle transcriptions (the test-side arithmetic; NOT calls
// into the port under test).
// ---------------------------------------------------------------------------

// The reference-frame enum values under test (definitions.h:1358-:1368).
constexpr int OH_NONE = -1, OH_INTRA = 0, OH_LAST = 1, OH_LAST2 = 2, OH_LAST3 = 3,
              OH_GOLDEN = 4, OH_BWDREF = 5, OH_ALTREF2 = 6, OH_ALTREF = 7;

// inter_prediction.c:276-:285 svt_aom_get_relative_dist_enc (oracle).
int oracleRelDist(int enable, int bits, int ref_hint, int order_hint) {
    if (!enable) return 0;
    int diff = ref_hint - order_hint;
    const int m = 1 << (bits - 1);
    return (diff & (m - 1)) - (diff & m);
}

// entropy_coding.c:52-:78 (oracle transcription).
int oracleCompIndexCtx(int above_avail, int above_second, int above_compound_idx, int above_rf0,
                       int left_avail, int left_second, int left_compound_idx, int left_rf0,
                       int cur, int bck, int fwd, int enable, int bits) {
    const int f = enable ? oracleRelDist(enable, bits, fwd, cur) : 0;
    const int b = enable ? oracleRelDist(enable, bits, cur, bck) : 0;
    const int fwd_abs = f < 0 ? -f : f;
    const int bck_abs = b < 0 ? -b : b;
    int above_ctx = 0, left_ctx = 0;
    if (above_avail) {
        if (above_second) above_ctx = above_compound_idx;
        else if (above_rf0 == OH_ALTREF) above_ctx = 1;
    }
    if (left_avail) {
        if (left_second) left_ctx = left_compound_idx;
        else if (left_rf0 == OH_ALTREF) left_ctx = 1;
    }
    const int offset = fwd_abs == bck_abs ? 1 : 0;
    return above_ctx + left_ctx + 3 * offset;
}

// entropy_coding.c:80-:99 (oracle transcription).
int oracleCompGroupIdxCtx(int above_avail, int above_second, int above_group, int above_rf0,
                          int left_avail, int left_second, int left_group, int left_rf0) {
    int above_ctx = 0, left_ctx = 0;
    if (above_avail) {
        if (above_second) above_ctx = above_group;
        else if (above_rf0 == OH_ALTREF) above_ctx = 3;
    }
    if (left_avail) {
        if (left_second) left_ctx = left_group;
        else if (left_rf0 == OH_ALTREF) left_ctx = 3;
    }
    int sum = above_ctx + left_ctx;
    return sum < 5 ? sum : 5;
}

// The xd carrier builder: above/left availability via NULL pointers, the
// availability flags mirrored (the reference's xd->up_available /
// xd->left_available gate the same spans independently of the pointers).
entropy::InterMbD makeXd(const entropy::InterMbMi* above, const entropy::InterMbMi* left,
                         int up_avail, int left_avail) {
    entropy::InterMbD xd{};
    xd.above_mi = above;
    xd.left_mi = left;
    xd.up_available = up_avail;
    xd.left_available = left_avail;
    xd.mi_stride = 4;
    return xd;
}

}  // namespace

// ---------------------------------------------------------------------------
// S2: av1_get_skip_mode_context :1097-:1103 - exhaustive enumeration of
// the above/left skip_mode bits against the oracle.
// ---------------------------------------------------------------------------
TEST_CASE("getSkipModeContext exhaustive vs oracle") {
    for (int a = 0; a < 3; ++a) {  // above: absent, 0, 1
        for (int l = 0; l < 3; ++l) {
            entropy::InterMbMi above{}, left{};
            entropy::InterMbD xd = makeXd(a ? &above : NULL, l ? &left : NULL, 1, 1);
            above.skip_mode = a == 2;
            left.skip_mode = l == 2;
            const int got = entropy::getSkipModeContext(&xd);
            const int want = (a ? above.skip_mode : 0) + (l ? left.skip_mode : 0);
            CHECK_EQ(got, want);
        }
    }
}

// ---------------------------------------------------------------------------
// S2: encode_skip_mode_av1 :1109-:1115 - the skip-mode flag symbol through
// skip_mode_cdfs[ctx] (2 symbols). Writer roundtrip through the existing
// l7 reader primitive (odEcReadSymbol is the reader twin of aom_write_
// symbol) + the adapted-cdf assertion against the independent update_cdf
// transcription.
// ---------------------------------------------------------------------------
TEST_CASE("writeSkipMode roundtrip + adapted cdf state") {
    entropy::InterFc fcW, fcR;
    entropy::initDefaultInterFrameContext(&fcW);
    fcR = fcW;
    for (int flag = 0; flag < 2; ++flag) {
        for (int ctxIn = 0; ctxIn < 3; ++ctxIn) {
            // The ctx comes from the two neighbor skip_mode bits; build an
            // xd for each ctx value: (0,0)->0, (1,0)->1, (1,1)->2.
            entropy::InterMbMi above{}, left{};
            above.skip_mode = ctxIn >= 1;
            left.skip_mode = ctxIn == 2;
            entropy::InterMbD xd = makeXd(&above, &left, 1, 1);
            CHECK_EQ(entropy::getSkipModeContext(&xd), ctxIn);
            entropy::AomWriter w{};
            unsigned char buf[64] = {0};
            w.ec.buf = buf;
            entropy::odEcEncReset(&w.ec);
            w.allow_update_cdf = 1;
            w.pos = 0;
            entropy::writeSkipMode(&w, &fcW, &xd, flag);
            entropy::odEcStopEncode(&w);
            entropy::AomReader r;
            REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
            r.allow_update_cdf = 1;
            const int decoded = entropy::odEcReadSymbol(&r, fcR.skip_mode_cdfs[ctxIn], 2);
            CHECK_EQ(decoded, flag);
            // Cdf adaptation lockstep: both sides adapted the SAME row.
            REQUIRE(std::memcmp(fcW.skip_mode_cdfs[ctxIn], fcR.skip_mode_cdfs[ctxIn],
                                sizeof(fcR.skip_mode_cdfs[ctxIn])) == 0);
            // Untouched rows stay default.
            for (int other = 0; other < 3; ++other) {
                if (other != ctxIn) {
                    REQUIRE(std::memcmp(fcW.skip_mode_cdfs[other], fcR.skip_mode_cdfs[other],
                                        sizeof(fcR.skip_mode_cdfs[other])) == 0);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// S3: svt_av1_get_intra_inter_context :1126-:1141 - exhaustive enumeration
// of (above availability x left availability x the two neighbours'
// intra/inter state) against the oracle. The availability flags are
// INDEPENDENT of the pointer presence (the reference reads xd->up_available
// / xd->left_available and xd->above_mbmi/left_mbmi separately).
// ---------------------------------------------------------------------------
TEST_CASE("getIntraInterContext exhaustive vs oracle") {
    // state: 0 = unavailable edge, 1 = available inter, 2 = available intra
    for (int a = 0; a < 3; ++a) {
        for (int l = 0; l < 3; ++l) {
            entropy::InterMbMi above{}, left{};
            above.ref_frame[0] = a == 1 ? OH_LAST : OH_INTRA;
            left.ref_frame[0] = l == 1 ? OH_LAST : OH_INTRA;
            const entropy::InterMbD xd = makeXd(a ? &above : NULL, l ? &left : NULL, a ? 1 : 0,
                                                l ? 1 : 0);
            const int got = entropy::getIntraInterContext(&xd);
            int want;
            if (a && l) {  // both edges available
                const int above_intra = a != 1;
                const int left_intra = l != 1;
                want = left_intra && above_intra ? 3 : (left_intra || above_intra ? 1 : 0);
            } else if (a || l) {  // one edge available
                want = 2 * (a ? (a != 1) : (l != 1));
            } else {
                want = 0;
            }
            CHECK_EQ(got, want);
        }
    }
}

// ---------------------------------------------------------------------------
// S3: write_is_inter :1147-:1151 - roundtrip through intra_inter_cdf[ctx]
// + adaptation lockstep (same reader-row contract as the S2 test).
// ---------------------------------------------------------------------------
TEST_CASE("writeIsInter roundtrip + adapted cdf state") {
    entropy::InterFc fcW, fcR;
    entropy::initDefaultInterFrameContext(&fcW);
    fcR = fcW;
    for (int ctxIn = 0; ctxIn < 4; ++ctxIn) {
        // The reference's table (definitions.h:1117-:1124): 0 = inter/inter
        // (and inter/--), 1 = intra/inter, 2 = one intra edge, 3 = intra/
        // intra. Build an xd for each ctx value.
        entropy::InterMbMi above{}, left{};
        entropy::InterMbD xd = makeXd(NULL, NULL, 0, 0);
        if (ctxIn == 0) {
            above.ref_frame[0] = OH_LAST;
            left.ref_frame[0] = OH_LAST;
            xd = makeXd(&above, &left, 1, 1);
        } else if (ctxIn == 1) {
            above.ref_frame[0] = OH_LAST;
            left.ref_frame[0] = OH_INTRA;
            xd = makeXd(&above, &left, 1, 1);
        } else if (ctxIn == 2) {
            above.ref_frame[0] = OH_INTRA;  // the ONE edge available is intra
            xd = makeXd(&above, NULL, 1, 0);
        } else if (ctxIn == 3) {
            above.ref_frame[0] = OH_INTRA;
            left.ref_frame[0] = OH_INTRA;
            xd = makeXd(&above, &left, 1, 1);
        }
        CHECK_EQ(entropy::getIntraInterContext(&xd), ctxIn);
        for (int is_inter = 0; is_inter < 2; ++is_inter) {
            entropy::AomWriter w{};
            unsigned char buf[64] = {0};
            w.ec.buf = buf;
            entropy::odEcEncReset(&w.ec);
            w.allow_update_cdf = 1;
            w.pos = 0;
            entropy::writeIsInter(&w, &fcW, &xd, is_inter);
            entropy::odEcStopEncode(&w);
            entropy::AomReader r;
            REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
            r.allow_update_cdf = 1;
            const int decoded = entropy::odEcReadSymbol(&r, fcR.intra_inter_cdf[ctxIn], 2);
            CHECK_EQ(decoded, is_inter);
            REQUIRE(std::memcmp(fcW.intra_inter_cdf[ctxIn], fcR.intra_inter_cdf[ctxIn],
                                sizeof(fcR.intra_inter_cdf[ctxIn])) == 0);
            for (int other = 0; other < 4; ++other) {
                if (other != ctxIn) {
                    REQUIRE(std::memcmp(fcW.intra_inter_cdf[other], fcR.intra_inter_cdf[other],
                                        sizeof(fcR.intra_inter_cdf[other])) == 0);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// S4 helpers: the oracle transcriptions of the motion-mode gate helpers.
// inter_prediction.h:378-:381 is_motion_variation_allowed_bsize.
// ---------------------------------------------------------------------------
int oracleBsizeWide(entropy::BlockSize b) {
    // blockSizeWide (entropy.cpp:537-:539, verbatim; the enum order is the
    // entropy.h:180-:203 BlockSize order).
    static const int w[22] = {4, 4, 8, 8, 8, 16, 16, 16, 32, 32, 32, 64, 64, 64, 128, 128,
                              4, 16, 8, 32, 16, 64};
    return w[b];
}
int oracleBsizeHigh(entropy::BlockSize b) {
    static const int h[22] = {4, 8, 4, 8, 16, 8, 16, 32, 16, 32, 64, 32, 64, 128, 64, 128,
                              16, 4, 32, 8, 64, 16};
    return h[b];
}
bool oracleMotionVariationAllowedBsize(entropy::BlockSize b) {
    return oracleBsizeWide(b) >= 8 && oracleBsizeHigh(b) >= 8;
}
// inter_prediction.h:382-:388 is_global_mv_block.
int oracleGlobalMvBlock(int mode, entropy::BlockSize b, int type) {
    return (mode == 15 || mode == 23) && type > 1 && oracleMotionVariationAllowedBsize(b);
}
// definitions.h:1610-:1612 is_inter_singleref_mode.
bool oracleSinglerefMode(int mode) { return mode >= 13 && mode < 17; }
// entropy_coding.c:1159-:1195 svt_aom_motion_mode_allowed (oracle). The
// rf0 gate is absent in the reference (only rf1 is consumed).
int oracleMotionModeAllowed(int switchable, int force_int, int allow_warped, int gm_type,
                            std::uint32_t overlappable, entropy::BlockSize bsize, int rf1,
                            int mode) {
    (void)0;  // rf0 is not consumed by the reference span
    if (!switchable) return 0;  // SIMPLE_TRANSLATION
    if (force_int == 0 && oracleGlobalMvBlock(mode, bsize, gm_type)) return 0;
    if (oracleMotionVariationAllowedBsize(bsize) && oracleSinglerefMode(mode) &&
        rf1 != 0 && !(rf1 > 0)) {
        if (overlappable == 0) return 0;
        if (allow_warped && 1) {  // num_proj_ref >= 1 (the fixture passes 1)
            return force_int ? 1 : 2;  // OBMC_CAUSAL : WARPED_CAUSAL
        }
        return 1;  // OBMC_CAUSAL
    }
    return 0;
}

// ---------------------------------------------------------------------------
// S4: svt_aom_motion_mode_allowed :1159-:1195 - the gate matrix: the
// switchable gate, the global-mv gate (mode x bsize x wmtype), the
// bsize/singleref/rf1 gate, the overlappable-neighbors gate, the warped
// branch (force_integer_mv fork + the num_proj_ref gate).
// ---------------------------------------------------------------------------
TEST_CASE("motionModeAllowed gate matrix vs oracle") {
    const int modes[6] = {0 /*DC*/, 13 /*NEARESTMV*/, 14 /*NEARMV*/, 15 /*GLOBALMV*/,
                          17 /*NEAREST_NEARESTMV*/, 23 /*GLOBAL_GLOBALMV*/};
    const int bsizes[6] = {0 /*4x4*/, 3 /*8x8*/, 4 /*8x16*/, 6 /*16x16*/, 9 /*32x32*/, 12 /*64x64*/};
    const int gmTypes[4] = {0 /*IDENTITY*/, 1 /*TRANSLATION*/, 2 /*ROTZOOM*/, 3 /*AFFINE*/};
    const int rf1s[3] = {OH_INTRA, OH_NONE, OH_LAST};
    for (int sw = 0; sw < 2; ++sw) {
        for (int fi = 0; fi < 2; ++fi) {
            for (int aw = 0; aw < 2; ++aw) {
                for (int gt = 0; gt < 4; ++gt) {
                    for (int ov = 0; ov < 2; ++ov) {
                        for (int bi = 0; bi < 6; ++bi) {
                            for (int mi = 0; mi < 6; ++mi) {
                                for (int r1 = 0; r1 < 3; ++r1) {
                                    const int got = entropy::motionModeAllowed(
                                        sw, fi, aw, gmTypes[gt], 1, (std::uint32_t)ov,
                                        (entropy::BlockSize)bsizes[bi], OH_LAST, rf1s[r1],
                                        (entropy::PredictionMode)modes[mi]);
                                    const int want = oracleMotionModeAllowed(
                                        sw, fi, aw, gmTypes[gt], (std::uint32_t)ov,
                                        (entropy::BlockSize)bsizes[bi], rf1s[r1],
                                        modes[mi]);
                                    CHECK_EQ(got, want);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // The num_proj_ref gate: allow_warped with num_proj_ref 0 vs 1
    // (allow_warped + npr 0 falls through to OBMC_CAUSAL).
    entropy::BlockSize b8 = entropy::BLOCK_8X8;
    CHECK_EQ((int)entropy::motionModeAllowed(1, 0, 1, 1, 0, 2, b8, OH_LAST, OH_NONE,
                                             entropy::NEARMV),
             1);  // npr < 1 -> OBMC_CAUSAL
    CHECK_EQ((int)entropy::motionModeAllowed(1, 0, 1, 1, 1, 2, b8, OH_LAST, OH_NONE,
                                             entropy::NEARMV),
             2);  // npr >= 1 -> WARPED_CAUSAL
}

// ---------------------------------------------------------------------------
// S4: write_motion_mode :1201-:1217 - the three branches: the allowed
// SIMPLE_TRANSLATION writes NOTHING; OBMC_CAUSAL writes the obmc bit;
// WARPED_CAUSAL-allowed writes the 3-symbol motion-mode symbol. Adapted
// cdf lockstep per row.
// ---------------------------------------------------------------------------
TEST_CASE("writeMotionMode branch symbol surface") {
    entropy::InterFc fc;
    entropy::initDefaultInterFrameContext(&fc);
    entropy::BlockSize b8 = entropy::BLOCK_8X8;
    // Branch 1: SIMPLE_TRANSLATION allowed -> no symbol (the byte count
    // equals the od_ec flush baseline of an empty encoder).
    {
        entropy::AomWriter w{};
        unsigned char buf[64] = {0};
        w.ec.buf = buf;
        entropy::odEcEncReset(&w.ec);
        w.allow_update_cdf = 1;
        w.pos = 0;
        entropy::writeMotionMode(&w, &fc, b8, entropy::GLOBALMV, 1, 0, OH_LAST, OH_NONE,
                                 entropy::SIMPLE_TRANSLATION, 1, 0, 1, 1);
        entropy::odEcStopEncode(&w);
        entropy::AomWriter wBase{};
        unsigned char bufBase[64] = {0};
        wBase.ec.buf = bufBase;
        entropy::odEcEncReset(&wBase.ec);
        wBase.allow_update_cdf = 1;
        wBase.pos = 0;
        entropy::odEcStopEncode(&wBase);
        CHECK_EQ((int)w.pos, (int)wBase.pos);  // nothing written
        REQUIRE(std::memcmp(buf, bufBase, sizeof(buf)) == 0);
    }
    // Branch 2: OBMC_CAUSAL allowed -> obmc_cdf[bsize] bit.
    {
        entropy::InterFc fcW = fc, fcR = fc;
        entropy::AomWriter w{};
        unsigned char buf[64] = {0};
        w.ec.buf = buf;
        entropy::odEcEncReset(&w.ec);
        w.allow_update_cdf = 1;
        w.pos = 0;
        entropy::writeMotionMode(&w, &fcW, b8, entropy::NEARMV, 1, 2, OH_LAST, OH_NONE,
                                 entropy::OBMC_CAUSAL, 1, 0, 0, 1);
        entropy::odEcStopEncode(&w);
        CHECK_EQ((int)w.pos, 1);
        entropy::AomReader r;
        REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
        r.allow_update_cdf = 1;
        CHECK_EQ(entropy::odEcReadSymbol(&r, fcR.obmc_cdf[b8], 2), 1);
        REQUIRE(std::memcmp(fcW.obmc_cdf[b8], fcR.obmc_cdf[b8], sizeof(fcR.obmc_cdf[b8])) == 0);
        // The motion_mode_cdf row untouched.
        REQUIRE(std::memcmp(fcW.motion_mode_cdf[b8], fc.motion_mode_cdf[b8],
                            sizeof(fc.motion_mode_cdf[b8])) == 0);
    }
    // Branch 3: WARPED_CAUSAL allowed -> 3-symbol motion_mode_cdf[bsize].
    {
        entropy::InterFc fcW = fc, fcR = fc;
        entropy::AomWriter w{};
        unsigned char buf[64] = {0};
        w.ec.buf = buf;
        entropy::odEcEncReset(&w.ec);
        w.allow_update_cdf = 1;
        w.pos = 0;
        entropy::writeMotionMode(&w, &fcW, b8, entropy::NEARMV, 1, 2, OH_LAST, OH_NONE,
                                 entropy::WARPED_CAUSAL, 1, 0, 1, 1);
        entropy::odEcStopEncode(&w);
        entropy::AomReader r;
        REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
        r.allow_update_cdf = 1;
        CHECK_EQ(entropy::odEcReadSymbol(&r, fcR.motion_mode_cdf[b8], 3), 2);
        REQUIRE(std::memcmp(fcW.motion_mode_cdf[b8], fcR.motion_mode_cdf[b8],
                            sizeof(fcR.motion_mode_cdf[b8])) == 0);
        REQUIRE(std::memcmp(fcW.obmc_cdf[b8], fc.obmc_cdf[b8], sizeof(fc.obmc_cdf[b8])) == 0);
    }
}

// ---------------------------------------------------------------------------
// S5: write_inter_mode :1386-:1404 - the mode_ctx mask decode (newmv/7,
// zeromv bit 3, refmv bits 4-7) and the symbol sequence per mode class:
// NEWMV writes 1 symbol, GLOBALMV 2, the nearmv/nearest pair 3.
// ---------------------------------------------------------------------------
TEST_CASE("writeInterMode mask decode + symbol sequence + adaptation") {
    entropy::InterFc fcW, fcR;
    entropy::initDefaultInterFrameContext(&fcW);
    fcR = fcW;
    // mode_ctx values covering all four ctx fields with the reference's
    // asserted bounds (newmv_ctx < 6, refmv_ctx < 6, zeromv_ctx < 2).
    const int ctxs[8] = {0, 5, 8, 9, 24, 48, 83, 88};
    const int modes[4] = {16 /*NEWMV*/, 15 /*GLOBALMV*/, 13 /*NEARESTMV*/, 14 /*NEARMV*/};
    for (int ci = 0; ci < 8; ++ci) {
        for (int mi = 0; mi < 4; ++mi) {
            const int mode = modes[mi];
            const int mode_ctx = ctxs[ci];
            const int newmv_ctx = mode_ctx & 7;
            const int zeromv_ctx = (mode_ctx >> 3) & 1;
            const int refmv_ctx = (mode_ctx >> 4) & 15;
            entropy::AomWriter w{};
            unsigned char buf[64] = {0};
            w.ec.buf = buf;
            entropy::odEcEncReset(&w.ec);
            w.allow_update_cdf = 1;
            w.pos = 0;
            entropy::writeInterMode(&w, &fcW, (entropy::PredictionMode)mode,
                                    (std::int16_t)mode_ctx);
            entropy::odEcStopEncode(&w);
            entropy::AomReader r;
            REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
            r.allow_update_cdf = 1;
            // Symbol 1: newmv bit = (mode != NEWMV).
            int want1 = mode != 16;
            CHECK_EQ(entropy::odEcReadSymbol(&r, fcR.newmv_cdf[newmv_ctx], 2), want1);
            REQUIRE(std::memcmp(fcW.newmv_cdf[newmv_ctx], fcR.newmv_cdf[newmv_ctx],
                                sizeof(fcR.newmv_cdf[newmv_ctx])) == 0);
            if (mode != 16) {
                // Symbol 2: zeromv bit = (mode != GLOBALMV).
                const int want2 = mode != 15;
                CHECK_EQ(entropy::odEcReadSymbol(&r, fcR.zeromv_cdf[zeromv_ctx], 2), want2);
                REQUIRE(std::memcmp(fcW.zeromv_cdf[zeromv_ctx], fcR.zeromv_cdf[zeromv_ctx],
                                    sizeof(fcR.zeromv_cdf[zeromv_ctx])) == 0);
                if (mode != 15) {
                    // Symbol 3: refmv bit = (mode != NEARESTMV).
                    const int want3 = mode != 13;
                    CHECK_EQ(entropy::odEcReadSymbol(&r, fcR.refmv_cdf[refmv_ctx], 2), want3);
                    REQUIRE(std::memcmp(fcW.refmv_cdf[refmv_ctx], fcR.refmv_cdf[refmv_ctx],
                                        sizeof(fcR.refmv_cdf[refmv_ctx])) == 0);
                } else {
                    // No third symbol: the refmv row stays default.
                    REQUIRE(std::memcmp(fcW.refmv_cdf[refmv_ctx], fcR.refmv_cdf[refmv_ctx],
                                        sizeof(fcR.refmv_cdf[refmv_ctx])) == 0);
                }
            } else {
                // One symbol only: the zeromv/refmv rows stay default.
                REQUIRE(std::memcmp(fcW.zeromv_cdf[zeromv_ctx], fcR.zeromv_cdf[zeromv_ctx],
                                    sizeof(fcR.zeromv_cdf[zeromv_ctx])) == 0);
                REQUIRE(std::memcmp(fcW.refmv_cdf[refmv_ctx], fcR.refmv_cdf[refmv_ctx],
                                    sizeof(fcR.refmv_cdf[refmv_ctx])) == 0);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// S6: write_drl_idx :1407-:1443 - the new_mv loop (idx 0..1 over drl_ctx)
// vs the nearmv loop (idx 1..2 over drl_ctx_near, the NEARESTMV offset),
// the -1 sentinel skips, and the early return at drl_index == idx.
// ---------------------------------------------------------------------------
TEST_CASE("writeDrlIdx symbol sequence vs oracle") {
    entropy::InterFc fcW, fcR;
    entropy::initDefaultInterFrameContext(&fcW);
    fcR = fcW;
    struct Case {
        int mode;
        std::int8_t drl_ctx[2];
        std::int8_t drl_ctx_near[2];
        int drl_index;
        int want_rows[3];  // the drl_cdf rows consumed, -1 = no more symbols
        int want_bits[3];  // the (drl_index != idx) bits
    };
    // Oracle transcription of entropy_coding.c:1407-:1443 (the drl_ctx
    // values are 0..2 - DRL_MODE_CONTEXTS).
    static const Case cases[9] = {
        // new_mv modes
        {16, {2, 1}, {0, 0}, 0, {2, -1, -1}, {0, 0, 0}},
        {16, {2, 1}, {0, 0}, 1, {2, 1, -1}, {1, 0, 0}},
        {24, {-1, 1}, {0, 0}, 1, {1, -1, -1}, {0, 0, 0}},
        {24, {-1, -1}, {0, 0}, 1, {-1, -1, -1}, {0, 0, 0}},
        {16, {2, 1}, {0, 0}, 2, {2, 1, -1}, {1, 1, 0}},  // no early return
        // nearmv modes (the idx-1 offset)
        {14, {0, 0}, {1, 2}, 0, {1, -1, -1}, {0, 0, 0}},
        {18, {0, 0}, {1, 2}, 1, {1, 2, -1}, {1, 0, 0}},
        {21, {0, 0}, {1, 2}, 2, {1, 2, -1}, {1, 1, 0}},
        {21, {0, 0}, {-1, 2}, 2, {2, -1, -1}, {1, 0, 0}},
    };
    for (int tc = 0; tc < 9; ++tc) {
        const Case& c = cases[tc];
        entropy::AomWriter w{};
        unsigned char buf[64] = {0};
        w.ec.buf = buf;
        entropy::odEcEncReset(&w.ec);
        w.allow_update_cdf = 1;
        w.pos = 0;
        entropy::writeDrlIdx(&w, &fcW, (entropy::PredictionMode)c.mode, c.drl_ctx,
                             c.drl_ctx_near, (std::uint8_t)c.drl_index);
        entropy::odEcStopEncode(&w);
        entropy::AomReader r;
        REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
        r.allow_update_cdf = 1;
        for (int k = 0; k < 3; ++k) {
            if (c.want_rows[k] < 0) break;
            const int decoded = entropy::odEcReadSymbol(&r, fcR.drl_cdf[c.want_rows[k]], 2);
            CHECK_EQ(decoded, c.want_bits[k]);
            REQUIRE(std::memcmp(fcW.drl_cdf[c.want_rows[k]], fcR.drl_cdf[c.want_rows[k]],
                                sizeof(fcR.drl_cdf[c.want_rows[k]])) == 0);
        }
        // Rows NOT consumed stay default.
        for (int row = 0; row < 3; ++row) {
            bool consumed = false;
            for (int k = 0; k < 3; ++k) consumed |= (c.want_rows[k] == row);
            if (!consumed) {
                REQUIRE(std::memcmp(fcW.drl_cdf[row], fcR.drl_cdf[row],
                                    sizeof(fcR.drl_cdf[row])) == 0);
            }
        }
    }
    // NEARESTMV / intra modes write nothing.
    {
        entropy::AomWriter w{};
        unsigned char buf[64] = {0};
        w.ec.buf = buf;
        entropy::odEcEncReset(&w.ec);
        w.allow_update_cdf = 1;
        w.pos = 0;
        const std::int8_t ctx2[2] = {2, 1};
        entropy::writeDrlIdx(&w, &fcW, entropy::NEARESTMV, ctx2, ctx2, 1);
        entropy::writeDrlIdx(&w, &fcW, entropy::DC_PRED, ctx2, ctx2, 1);
        entropy::odEcStopEncode(&w);
        entropy::AomWriter wBase{};
        unsigned char bufBase[64] = {0};
        wBase.ec.buf = bufBase;
        entropy::odEcEncReset(&wBase.ec);
        wBase.allow_update_cdf = 1;
        entropy::odEcStopEncode(&wBase);
        CHECK_EQ((int)w.pos, (int)wBase.pos);
        REQUIRE(std::memcmp(buf, bufBase, sizeof(buf)) == 0);
    }
}

// ---------------------------------------------------------------------------
// S7 helpers: the oracle transcriptions for the mv-component chain.
// md_rate_estimation.c:366-:368 mv_class_base + :370-:376 get_mv_class.
// ---------------------------------------------------------------------------
int oracleMvClassBase(int c) { return c ? (2 << (c + 2)) : 0; }
void oracleMvClass(int z, int* cls, int* offset) {
    *cls = (z >= 2 * 4096) ? 10 : (z >> 3) == 0 ? 0 : -1;
    if (*cls == -1) {  // floor(log2(z>>3)) portable form
        int v = z >> 3, log = 0;
        while ((1 << (log + 1)) <= v) ++log;
        *cls = log;
    }
    *offset = z - oracleMvClassBase(*cls);
}

// ---------------------------------------------------------------------------
// S7: encode_mv_component :1445-:1481 - the joint/class/offset band
// enumeration: sign, class, class0/integer bits, fractional bits, the
// high-precision bit, at both precisions. The emitted symbol sequence is
// asserted symbol-by-symbol through the reader on the cdf rows the port
// selected, plus the adapted-cdf lockstep of every touched row.
// ---------------------------------------------------------------------------
TEST_CASE("encodeMvComponent band enumeration vs symbol trace") {
    entropy::InterFc fc;
    entropy::initDefaultInterFrameContext(&fc);
    // comp values: the class-0 band (1..2 with d 0..1), each class boundary,
    // negative values, and the large-magnitude classes.
    static const int comps[16] = {1, 2, 3, 4, 5, 8, 9, 16, 17, 64, 65, 256, 257, 1024, 2048, -37};
    const entropy::MvSubpelPrecision precisions[2] = {entropy::MV_SUBPEL_LOW_PRECISION,
                                                      entropy::MV_SUBPEL_HIGH_PRECISION};
    for (int pi = 0; pi < 2; ++pi) {
        for (int ci = 0; ci < 16; ++ci) {
            const int comp = comps[ci];
            const int sign = comp < 0;
            const int mag = sign ? -comp : comp;
            int cls, offset;
            oracleMvClass(mag - 1, &cls, &offset);
            const int d = offset >> 3;
            const int fr = (offset >> 1) & 3;
            const int hp = offset & 1;
            entropy::NmvComponent mvW = fc.nmvc.comps[0];
            entropy::NmvComponent mvR = fc.nmvc.comps[0];
            entropy::AomWriter w{};
            unsigned char buf[256] = {0};
            w.ec.buf = buf;
            entropy::odEcEncReset(&w.ec);
            w.allow_update_cdf = 1;
            w.pos = 0;
            entropy::encodeMvComponent(&w, comp, &mvW, precisions[pi]);
            entropy::odEcStopEncode(&w);
            entropy::AomReader r;
            REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
            r.allow_update_cdf = 1;
            // Symbol 1: sign.
            CHECK_EQ(entropy::odEcReadSymbol(&r, mvR.sign_cdf, 2), sign);
            REQUIRE(std::memcmp(mvW.sign_cdf, mvR.sign_cdf, sizeof(mvR.sign_cdf)) == 0);
            // Symbol 2: class.
            CHECK_EQ(entropy::odEcReadSymbol(&r, mvR.classes_cdf, MV_CLASSES), cls);
            REQUIRE(std::memcmp(mvW.classes_cdf, mvR.classes_cdf, sizeof(mvR.classes_cdf)) == 0);
            // Integer bits.
            if (cls == 0) {
                CHECK_EQ(entropy::odEcReadSymbol(&r, mvR.class0_cdf, CLASS0_SIZE), d);
                REQUIRE(std::memcmp(mvW.class0_cdf, mvR.class0_cdf, sizeof(mvR.class0_cdf)) == 0);
            } else {
                const int n = cls + CLASS0_BITS - 1;
                for (int i = 0; i < n; ++i) {
                    CHECK_EQ(entropy::odEcReadSymbol(&r, mvR.bits_cdf[i], 2), (d >> i) & 1);
                    REQUIRE(std::memcmp(mvW.bits_cdf[i], mvR.bits_cdf[i],
                                        sizeof(mvR.bits_cdf[i])) == 0);
                }
            }
            // Fractional bits: precision > MV_SUBPEL_NONE for both fixtures.
            entropy::AomCdfProb* fpRowR = cls == 0 ? mvR.class0_fp_cdf[d] : mvR.fp_cdf;
            entropy::AomCdfProb* fpRowW = cls == 0 ? mvW.class0_fp_cdf[d] : mvW.fp_cdf;
            CHECK_EQ(entropy::odEcReadSymbol(&r, fpRowR, MV_FP_SIZE), fr);
            REQUIRE(std::memcmp(fpRowW, fpRowR, sizeof(mvR.fp_cdf)) == 0);
            // High-precision bit: only at MV_SUBPEL_HIGH_PRECISION.
            if (pi == 1) {
                entropy::AomCdfProb* hpRowW = cls == 0 ? mvW.class0_hp_cdf : mvW.hp_cdf;
                entropy::AomCdfProb* hpRowR = cls == 0 ? mvR.class0_hp_cdf : mvR.hp_cdf;
                CHECK_EQ(entropy::odEcReadSymbol(&r, hpRowR, 2), hp);
                REQUIRE(std::memcmp(hpRowW, hpRowR, sizeof(mvR.hp_cdf)) == 0);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// S8: av1_get_mv_joint_diff :1485-:1493 - the full 2x2 (diff[0]==0 x
// diff[1]==0) joint matrix.
// ---------------------------------------------------------------------------
TEST_CASE("getMvJointDiff joint matrix") {
    const std::int32_t z[2] = {0, 0};
    // (0,0)->MV_JOINT_ZERO(0); (0,x)->HNZVZ(1); (x,0)->HZVNZ(2);
    // (x,y)->HNZVNZ(3).
    // (0,0)->MV_JOINT_ZERO(0); (y!=0,x==0)->HZVNZ(2); (y==0,x!=0)->HNZVZ(1);
    // (x,y)->HNZVNZ(3). diff[0] is the y (vertical) component, diff[1] the
    // x (horizontal) component.
    CHECK_EQ((int)entropy::getMvJointDiff(z), 0);
    const std::int32_t nz[2] = {5, 0}, nz2[2] = {0, 7}, both[2] = {-3, 9};
    CHECK_EQ((int)entropy::getMvJointDiff(nz), 2);
    CHECK_EQ((int)entropy::getMvJointDiff(nz2), 1);
    CHECK_EQ((int)entropy::getMvJointDiff(both), 3);
}

// ---------------------------------------------------------------------------
// S8: svt_av1_encode_mv :1495-:1519 - the joint symbol then the per-
// component chains (vertical y first, horizontal x second), with the
// force_integer_mv precision override.
// ---------------------------------------------------------------------------
TEST_CASE("encodeMv joint + component chain vs symbol trace") {
    entropy::InterFc fc;
    entropy::initDefaultInterFrameContext(&fc);
    struct Case {
        std::int16_t my, mx, ry, rx;
        int force_integer_mv;
        int usehp;  // 0 = LOW_PRECISION, 1 = HIGH_PRECISION
    };
    // Joints x class bands x the force_integer_mv override.
    static const Case cases[12] = {
        {0, 0, 0, 0, 0, 1},     // ZERO joint
        {0, 8, 0, 0, 0, 1},     // HNZVZ, class 0
        {8, 0, 0, 0, 0, 1},     // HZVNZ, class 0
        {8, 8, 0, 0, 0, 1},     // HNZVNZ, class 0
        {0, 24, 0, 0, 0, 1},    // HNZVZ, class 1
        {24, 0, 0, 0, 0, 1},    // HZVNZ, class 1
        {-40, 37, 0, 0, 0, 1},  // both nonzero, negatives
        {300, -500, 0, 0, 0, 1},// large classes
        {8, 8, 0, 0, 1, 1},     // force_integer_mv overrides usehp to NONE
        {5, 5, 1, 1, 0, 1},     // nonzero diff with subpel magnitudes
        {0, -3, 0, 0, 0, 1},    // negative x only
        {-1, 0, 0, 0, 0, 0},    // LOW_PRECISION: no hp bit
    };
    for (int tc = 0; tc < 12; ++tc) {
        const Case& c = cases[tc];
        entropy::NmvContext ctxW = fc.nmvc;
        entropy::NmvContext ctxR = fc.nmvc;
        entropy::Mv mv{c.mx, c.my}, ref{c.rx, c.ry};
        entropy::AomWriter w{};
        unsigned char buf[256] = {0};
        w.ec.buf = buf;
        entropy::odEcEncReset(&w.ec);
        w.allow_update_cdf = 1;
        w.pos = 0;
        entropy::encodeMv(c.force_integer_mv, &w, mv, ref, &ctxW, c.usehp);
        entropy::odEcStopEncode(&w);
        entropy::AomReader r;
        REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
        r.allow_update_cdf = 1;
        const std::int32_t diff[2] = {c.my - c.ry, c.mx - c.rx};
        const int joint = (int)entropy::getMvJointDiff(diff);
        CHECK_EQ(entropy::odEcReadSymbol(&r, ctxR.joints_cdf, MV_JOINTS), joint);
        REQUIRE(std::memcmp(ctxW.joints_cdf, ctxR.joints_cdf, sizeof(ctxR.joints_cdf)) == 0);
        const int effPrec = c.force_integer_mv ? entropy::MV_SUBPEL_NONE : c.usehp;
        for (int compIdx = 0; compIdx < 2; ++compIdx) {
            const bool active = (compIdx == 0) ? (joint == 2 || joint == 3)
                                                : (joint == 1 || joint == 3);
            if (!active) continue;
            const int comp = diff[compIdx];
            const int sign = comp < 0;
            const int mag = sign ? -comp : comp;
            int cls, offset;
            oracleMvClass(mag - 1, &cls, &offset);
            const int d = offset >> 3;
            const int fr = (offset >> 1) & 3;
            const int hp = offset & 1;
            entropy::NmvComponent* cW = &ctxW.comps[compIdx];
            entropy::NmvComponent* cR = &ctxR.comps[compIdx];
            CHECK_EQ(entropy::odEcReadSymbol(&r, cR->sign_cdf, 2), sign);
            REQUIRE(std::memcmp(cW->sign_cdf, cR->sign_cdf, sizeof(cR->sign_cdf)) == 0);
            CHECK_EQ(entropy::odEcReadSymbol(&r, cR->classes_cdf, MV_CLASSES), cls);
            REQUIRE(std::memcmp(cW->classes_cdf, cR->classes_cdf, sizeof(cR->classes_cdf)) == 0);
            if (cls == 0) {
                CHECK_EQ(entropy::odEcReadSymbol(&r, cR->class0_cdf, CLASS0_SIZE), d);
                REQUIRE(std::memcmp(cW->class0_cdf, cR->class0_cdf, sizeof(cR->class0_cdf)) == 0);
            } else {
                const int n = cls + CLASS0_BITS - 1;
                for (int i = 0; i < n; ++i) {
                    CHECK_EQ(entropy::odEcReadSymbol(&r, cR->bits_cdf[i], 2), (d >> i) & 1);
                    REQUIRE(std::memcmp(cW->bits_cdf[i], cR->bits_cdf[i],
                                        sizeof(cR->bits_cdf[i])) == 0);
                }
            }
            if (effPrec > entropy::MV_SUBPEL_NONE) {
                entropy::AomCdfProb* fpW = cls == 0 ? cW->class0_fp_cdf[d] : cW->fp_cdf;
                entropy::AomCdfProb* fpR = cls == 0 ? cR->class0_fp_cdf[d] : cR->fp_cdf;
                CHECK_EQ(entropy::odEcReadSymbol(&r, fpR, MV_FP_SIZE), fr);
                REQUIRE(std::memcmp(fpW, fpR, sizeof(cR->fp_cdf)) == 0);
                if (effPrec > entropy::MV_SUBPEL_LOW_PRECISION) {
                    entropy::AomCdfProb* hpW = cls == 0 ? cW->class0_hp_cdf : cW->hp_cdf;
                    entropy::AomCdfProb* hpR = cls == 0 ? cR->class0_hp_cdf : cR->hp_cdf;
                    CHECK_EQ(entropy::odEcReadSymbol(&r, hpR, 2), hp);
                    REQUIRE(std::memcmp(hpW, hpR, sizeof(cR->hp_cdf)) == 0);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// S9: svt_aom_get_ref_filter_type :1522-:1527 - the ref-match gate and the
// packed-filter extraction (dir 0 = low 16 bits, dir 1 = high 16 bits).
// ---------------------------------------------------------------------------
TEST_CASE("getRefFilterType ref-match gate + packed extraction") {
    entropy::InterMbMi mi{};
    mi.ref_frame[0] = OH_LAST;
    mi.ref_frame[1] = OH_ALTREF;
    mi.interp_filters = (2u << 16) | 1u;  // y filter 1, x filter 2
    // Matching ref -> the packed filter for that dir.
    CHECK_EQ((int)entropy::getRefFilterType(&mi, 0, OH_LAST), 1);
    CHECK_EQ((int)entropy::getRefFilterType(&mi, 1, OH_LAST), 2);
    CHECK_EQ((int)entropy::getRefFilterType(&mi, 0, OH_ALTREF), 1);
    CHECK_EQ((int)entropy::getRefFilterType(&mi, 1, OH_ALTREF), 2);
    // Non-matching ref -> SWITCHABLE_FILTERS (3).
    CHECK_EQ((int)entropy::getRefFilterType(&mi, 0, OH_GOLDEN), 3);
    CHECK_EQ((int)entropy::getRefFilterType(&mi, 1, OH_INTRA), 3);
}

// ---------------------------------------------------------------------------
// S9: svt_aom_get_pred_context_switchable_interp :1530-:1573 - exhaustive
// enumeration: rf1 compound offset x dir offset x above/left availability
// x the filter-type relations (equal / one-unavailable / both-set-
// different), against the oracle. The neighbors' packed filters carry the
// same filter in both 16-bit halves so both dirs extract the same value.
// ---------------------------------------------------------------------------
TEST_CASE("getPredContextSwitchableInterp exhaustive vs oracle") {
    // Neighbor states: 0 = unavailable (type stays SWITCHABLE_FILTERS),
    // 1..3 = available with filter 0..2, 4 = available with a NON-MATCHING
    // ref (type = SWITCHABLE_FILTERS).
    // Oracle: the reference's left_type/above_type value.
    auto typeOf = [](int state, int dir) -> int {
        (void)dir;  // the fixture's packed filters carry the same value in
                    // both 16-bit halves, so both dirs extract the same one
        if (state == 0) return entropy::SWITCHABLE_FILTERS;  // unavailable -> stays 3
        if (state == 4) return entropy::SWITCHABLE_FILTERS;  // non-matching ref -> 3
        return state - 1;  // filter 0..2, ref matches
    };
    for (int rf1 = 0; rf1 < 2; ++rf1) {
        for (int dir = 0; dir < 2; ++dir) {
            for (int a = 0; a < 5; ++a) {
                for (int l = 0; l < 5; ++l) {
                    entropy::InterMbMi above{}, left{};
                    above.ref_frame[0] = a == 4 ? OH_GOLDEN : OH_LAST;
                    left.ref_frame[0] = l == 4 ? OH_GOLDEN : OH_LAST;
                    const int af = a >= 1 && a <= 3 ? a - 1 : 0;
                    const int lf = l >= 1 && l <= 3 ? l - 1 : 0;
                    above.interp_filters = ((std::uint32_t)af << 16) | (std::uint32_t)af;
                    left.interp_filters = ((std::uint32_t)lf << 16) | (std::uint32_t)lf;
                    const entropy::InterMbD xd = makeXd(a ? &above : NULL, l ? &left : NULL,
                                                        a ? 1 : 0, l ? 1 : 0);
                    const int got = entropy::getPredContextSwitchableInterp(
                        OH_LAST, rf1 ? OH_ALTREF : OH_NONE, &xd, dir);
                    const int ctx_offset = (rf1 ? 1 : 0) * INTER_FILTER_COMP_OFFSET;
                    int want = ctx_offset + (dir & 1) * INTER_FILTER_DIR_OFFSET;
                    const int left_type = typeOf(l, dir);
                    const int above_type = typeOf(a, dir);
                    if (left_type == above_type) want += left_type;
                    else if (left_type == entropy::SWITCHABLE_FILTERS) want += above_type;
                    else if (above_type == entropy::SWITCHABLE_FILTERS) want += left_type;
                    else want += entropy::SWITCHABLE_FILTERS;
                    CHECK_EQ(got, want);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// S10: svt_aom_is_nontrans_global_motion :1575-:1596 + av1_is_interp_needed
// :1598-:1612 - the mode gate, the mi-size gate (4x8/8x4/16x4/4x16 and 4x4
// fail the MIN(mi_w, mi_h) >= 2 test), the compound ref sweep, and the
// interp-needed gates.
// ---------------------------------------------------------------------------
TEST_CASE("isNontransGlobalMotion + isInterpNeeded gates") {
    int gmWmtype[OH_ALTREF + 1];
    for (int i = 0; i <= OH_ALTREF; ++i) gmWmtype[i] = 1;  // TRANSLATION
    struct Case {
        int mode;
        entropy::BlockSize bsize;
        int rf0, rf1;
        int wm0, wm1;  // -1 = leave TRANSLATION
        int wantNontrans;
        int skip_mode;
        int motion_mode;
        int wantInterpNeeded;
    };
    static const Case cases[12] = {
        {15, entropy::BLOCK_8X8, OH_LAST, OH_NONE, 2, -1, 1, 0, 0, 0},    // GLOBALMV nontrans
        {15, entropy::BLOCK_8X8, OH_LAST, OH_NONE, -1, -1, 0, 0, 0, 1},   // GLOBALMV translation
        {16, entropy::BLOCK_8X8, OH_LAST, OH_NONE, 2, -1, 0, 0, 0, 1},    // NEWMV -> not nontrans
        {23, entropy::BLOCK_16X16, OH_LAST, OH_ALTREF, 2, 2, 1, 0, 0, 0}, // GLOBAL_GLOBALMV compound
        {23, entropy::BLOCK_16X16, OH_LAST, OH_ALTREF, 2, -1, 0, 0, 0, 1},// one ref translation
        {15, entropy::BLOCK_4X4, OH_LAST, OH_NONE, 2, -1, 0, 0, 0, 1},    // mi too small
        {15, entropy::BLOCK_4X8, OH_LAST, OH_NONE, 2, -1, 0, 0, 0, 1},    // mi too small
        {15, entropy::BLOCK_8X4, OH_LAST, OH_NONE, 2, -1, 0, 0, 0, 1},    // mi too small
        {15, entropy::BLOCK_16X4, OH_LAST, OH_NONE, 2, -1, 0, 0, 0, 1},   // mi too small
        {15, entropy::BLOCK_4X16, OH_LAST, OH_NONE, 2, -1, 0, 0, 0, 1},   // mi too small
        {15, entropy::BLOCK_8X8, OH_LAST, OH_NONE, 2, -1, 1, 1, 0, 0},    // skip_mode -> no interp
        {15, entropy::BLOCK_8X8, OH_LAST, OH_NONE, 2, -1, 1, 0, 2, 0},    // WARPED -> no interp
    };
    for (int tc = 0; tc < 12; ++tc) {
        const Case& c = cases[tc];
        entropy::InterMbMi mi{};
        mi.mode = (entropy::PredictionMode)c.mode;
        mi.ref_frame[0] = c.rf0;
        mi.ref_frame[1] = c.rf1;
        mi.skip_mode = c.skip_mode;
        mi.motion_mode = (entropy::MotionMode)c.motion_mode;
        int wm[OH_ALTREF + 1];
        for (int i = 0; i <= OH_ALTREF; ++i) wm[i] = 1;
        if (c.wm0 >= 0) wm[c.rf0] = c.wm0;
        if (c.wm1 >= 0) wm[c.rf1] = c.wm1;
        CHECK_EQ(entropy::isNontransGlobalMotion(&mi, c.bsize, wm), c.wantNontrans);
        CHECK_EQ(entropy::isInterpNeeded(&mi, c.bsize, wm), c.wantInterpNeeded);
    }
}

// ---------------------------------------------------------------------------
// S10: write_mb_interp_filter :1614-:1631 - the SWITCHABLE gate, the
// interp-needed gate, the dual-filter dir count, the ctx derivation and
// the per-dir symbol roundtrip + adaptation lockstep.
// ---------------------------------------------------------------------------
TEST_CASE("writeMbInterpFilter gates + per-dir symbols") {
    entropy::InterFc fcW, fcR;
    entropy::initDefaultInterFrameContext(&fcW);
    fcR = fcW;
    int wm[OH_ALTREF + 1];
    for (int i = 0; i <= OH_ALTREF; ++i) wm[i] = 1;  // all TRANSLATION
    entropy::InterMbMi mi{};
    mi.mode = entropy::NEWMV;
    mi.ref_frame[0] = OH_LAST;
    mi.ref_frame[1] = OH_NONE;
    mi.interp_filters = (2u << 16) | 1u;  // y=1, x=2
    entropy::InterMbMi above{}, left{};
    above.ref_frame[0] = OH_LAST;
    left.ref_frame[0] = OH_LAST;
    above.interp_filters = 1;
    left.interp_filters = 2;
    entropy::InterMbD xd = makeXd(&above, &left, 1, 1);
    // Gate 1: non-SWITCHABLE frame filter -> nothing.
    {
        entropy::AomWriter w{}, wBase{};
        unsigned char buf[64] = {0}, bufBase[64] = {0};
        w.ec.buf = buf;
        wBase.ec.buf = bufBase;
        entropy::odEcEncReset(&w.ec);
        entropy::odEcEncReset(&wBase.ec);
        w.allow_update_cdf = 1;
        wBase.allow_update_cdf = 1;
        entropy::writeMbInterpFilter(&w, &fcW, entropy::BLOCK_8X8, OH_LAST, OH_NONE, 0, 1, &mi,
                                     &xd, wm);
        entropy::odEcStopEncode(&w);
        entropy::odEcStopEncode(&wBase);
        CHECK_EQ((int)w.pos, (int)wBase.pos);
        REQUIRE(std::memcmp(buf, bufBase, sizeof(buf)) == 0);
    }
    // Gate 2: interp not needed (skip_mode) -> nothing.
    {
        entropy::InterMbMi skipMi = mi;
        skipMi.skip_mode = 1;
        entropy::AomWriter w{}, wBase{};
        unsigned char buf[64] = {0}, bufBase[64] = {0};
        w.ec.buf = buf;
        wBase.ec.buf = bufBase;
        entropy::odEcEncReset(&w.ec);
        entropy::odEcEncReset(&wBase.ec);
        w.allow_update_cdf = 1;
        wBase.allow_update_cdf = 1;
        entropy::writeMbInterpFilter(&w, &fcW, entropy::BLOCK_8X8, OH_LAST, OH_NONE,
                                     entropy::SWITCHABLE, 1, &skipMi, &xd, wm);
        entropy::odEcStopEncode(&w);
        entropy::odEcStopEncode(&wBase);
        CHECK_EQ((int)w.pos, (int)wBase.pos);
        REQUIRE(std::memcmp(buf, bufBase, sizeof(buf)) == 0);
    }
    // Single-dir (enable_dual_filter = 0): one symbol, dir 0.
    {
        entropy::AomWriter w{};
        unsigned char buf[64] = {0};
        w.ec.buf = buf;
        entropy::odEcEncReset(&w.ec);
        w.allow_update_cdf = 1;
        w.pos = 0;
        entropy::writeMbInterpFilter(&w, &fcW, entropy::BLOCK_8X8, OH_LAST, OH_NONE,
                                     entropy::SWITCHABLE, 0, &mi, &xd, wm);
        entropy::odEcStopEncode(&w);
        entropy::AomReader r;
        REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
        r.allow_update_cdf = 1;
        // dir 0 ctx: rf1 <= INTRA -> offset 0; left type 2, above type 1 ->
        // different -> +SWITCHABLE_FILTERS = 3.
        const int ctx0 = entropy::getPredContextSwitchableInterp(OH_LAST, OH_NONE, &xd, 0);
        CHECK_EQ(ctx0, 3);
        CHECK_EQ(entropy::odEcReadSymbol(&r, fcR.switchable_interp_cdf[ctx0],
                                         entropy::SWITCHABLE_FILTERS), 1);
        REQUIRE(std::memcmp(fcW.switchable_interp_cdf[ctx0], fcR.switchable_interp_cdf[ctx0],
                            sizeof(fcR.switchable_interp_cdf[ctx0])) == 0);
        // The dir-1 rows untouched.
        for (int ctx = 0; ctx < SWITCHABLE_FILTER_CONTEXTS; ++ctx) {
            if (ctx != ctx0) {
                REQUIRE(std::memcmp(fcW.switchable_interp_cdf[ctx],
                                    fcR.switchable_interp_cdf[ctx],
                                    sizeof(fcR.switchable_interp_cdf[ctx])) == 0);
            }
        }
    }
    // Dual-dir (enable_dual_filter = 1): two symbols, dirs 0 and 1.
    {
        entropy::AomWriter w{};
        unsigned char buf[64] = {0};
        w.ec.buf = buf;
        entropy::odEcEncReset(&w.ec);
        w.allow_update_cdf = 1;
        w.pos = 0;
        entropy::writeMbInterpFilter(&w, &fcW, entropy::BLOCK_8X8, OH_LAST, OH_NONE,
                                     entropy::SWITCHABLE, 1, &mi, &xd, wm);
        entropy::odEcStopEncode(&w);
        entropy::AomReader r;
        REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
        r.allow_update_cdf = 1;
        const int ctx0 = entropy::getPredContextSwitchableInterp(OH_LAST, OH_NONE, &xd, 0);
        const int ctx1 = entropy::getPredContextSwitchableInterp(OH_LAST, OH_NONE, &xd, 1);
        // dir 1 extracts the HIGH 16-bit halves: the fixture's neighbors
        // carry 0 there, so both types are 0 -> equal -> +0.
        CHECK_EQ(ctx1, 0 + INTER_FILTER_DIR_OFFSET);
        CHECK_EQ(entropy::odEcReadSymbol(&r, fcR.switchable_interp_cdf[ctx0],
                                         entropy::SWITCHABLE_FILTERS), 1);
        CHECK_EQ(entropy::odEcReadSymbol(&r, fcR.switchable_interp_cdf[ctx1],
                                         entropy::SWITCHABLE_FILTERS), 2);
        REQUIRE(std::memcmp(fcW.switchable_interp_cdf[ctx0], fcR.switchable_interp_cdf[ctx0],
                            sizeof(fcR.switchable_interp_cdf[ctx0])) == 0);
        REQUIRE(std::memcmp(fcW.switchable_interp_cdf[ctx1], fcR.switchable_interp_cdf[ctx1],
                            sizeof(fcR.switchable_interp_cdf[ctx1])) == 0);
    }
}

// ---------------------------------------------------------------------------
// S11: write_inter_compound_mode :1633-:1638 - the INTER_COMPOUND_OFFSET
// symbol (mode - NEAREST_NEARESTMV) through inter_compound_mode_cdf[ctx],
// 8 symbols, over the full compound-mode x context grid.
// ---------------------------------------------------------------------------
TEST_CASE("writeInterCompoundMode offset grid + adaptation") {
    entropy::InterFc fcW, fcR;
    entropy::initDefaultInterFrameContext(&fcW);
    fcR = fcW;
    for (int ctx = 0; ctx < 8; ++ctx) {
        for (int mode = 17; mode <= 24; ++mode) {
            entropy::AomWriter w{};
            unsigned char buf[64] = {0};
            w.ec.buf = buf;
            entropy::odEcEncReset(&w.ec);
            w.allow_update_cdf = 1;
            w.pos = 0;
            entropy::writeInterCompoundMode(&w, &fcW, (entropy::PredictionMode)mode,
                                            (std::int16_t)ctx);
            entropy::odEcStopEncode(&w);
            entropy::AomReader r;
            REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
            r.allow_update_cdf = 1;
            CHECK_EQ(entropy::odEcReadSymbol(&r, fcR.inter_compound_mode_cdf[ctx], 8),
                     mode - 17);
            REQUIRE(std::memcmp(fcW.inter_compound_mode_cdf[ctx],
                                fcR.inter_compound_mode_cdf[ctx],
                                sizeof(fcR.inter_compound_mode_cdf[ctx])) == 0);
        }
    }
}

// ---------------------------------------------------------------------------
// S12 helpers: the oracle transcriptions of the reference-mode /
// comp-reference context family (entropy_coding.c:1640-:2102).
// ---------------------------------------------------------------------------

// block_structures.h:113-:117 has_uni_comp_refs (oracle).
int oracleUniComp(int rf0, int rf1) {
    const int second = rf1 > OH_INTRA;
    return second && !((rf0 >= OH_BWDREF) ^ (rf1 >= OH_BWDREF));
}
// inter_prediction.h:251 IS_BACKWARD_REF_FRAME (oracle).
int oracleBackward(int rf) { return (rf >= OH_BWDREF && rf <= OH_ALTREF) ? 1 : 0; }
// entropy_coding.c:1839-:1881 get_reference_mode_context_new (oracle).
int oracleRefModeCtx(int a_avail, int l_avail, int a_rf0, int a_rf1, int l_rf0, int l_rf1) {
    int ctx;
    const int a_second = a_rf1 > OH_INTRA, l_second = l_rf1 > OH_INTRA;
    const int a_inter = a_rf0 > OH_INTRA, l_inter = l_rf0 > OH_INTRA;
    if (a_avail && l_avail) {
        if (!a_second && !l_second) {
            ctx = oracleBackward(a_rf0) ^ oracleBackward(l_rf0);
        } else if (!a_second) {
            ctx = 2 + (oracleBackward(a_rf0) || !a_inter);
        } else if (!l_second) {
            ctx = 2 + (oracleBackward(l_rf0) || !l_inter);
        } else {
            ctx = 4;
        }
    } else if (a_avail || l_avail) {
        const int e_rf0 = a_avail ? a_rf0 : l_rf0;
        const int e_second = a_avail ? a_second : l_second;
        if (!e_second) {  // edge does not use comp pred (0/1)
            ctx = oracleBackward(e_rf0);
        } else {
            ctx = 3;
        }
    } else {
        ctx = 1;
    }
    return ctx;
}
// entropy_coding.c:1701-:1770 get_comp_reference_type_context_new (oracle).
int oracleCompRefTypeCtx(int a_avail, int l_avail, int a_rf0, int a_rf1, int l_rf0, int l_rf1) {
    int pred_context;
    const int a_second = a_rf1 > OH_INTRA, l_second = l_rf1 > OH_INTRA;
    const int a_inter = a_rf0 > OH_INTRA, l_inter = l_rf0 > OH_INTRA;
    if (a_avail && l_avail) {
        const int a_intra = !a_inter, l_intra = !l_inter;
        if (a_intra && l_intra) {
            pred_context = 2;
        } else if (a_intra || l_intra) {
            const int i_second = a_intra ? l_second : a_second;
            const int i_rf0 = a_intra ? l_rf0 : a_rf0;
            const int i_rf1 = a_intra ? l_rf1 : a_rf1;
            if (!i_second) {
                pred_context = 2;
            } else {
                pred_context = 1 + 2 * oracleUniComp(i_rf0, i_rf1);
            }
        } else {
            const int a_sg = !a_second, l_sg = !l_second;
            if (a_sg && l_sg) {
                pred_context = 1 + 2 * (!(oracleBackward(a_rf0) ^ oracleBackward(l_rf0)));
            } else if (l_sg || a_sg) {
                const int uni_rfc = a_sg ? oracleUniComp(l_rf0, l_rf1)
                                         : oracleUniComp(a_rf0, a_rf1);
                if (!uni_rfc) {
                    pred_context = 1;
                } else {
                    pred_context = 3 + (!(oracleBackward(a_rf0) ^ oracleBackward(l_rf0)));
                }
            } else {
                const int a_uni = oracleUniComp(a_rf0, a_rf1);
                const int l_uni = oracleUniComp(l_rf0, l_rf1);
                if (!a_uni && !l_uni) {
                    pred_context = 0;
                } else if (!a_uni || !l_uni) {
                    pred_context = 2;
                } else {
                    pred_context = 3 + (!((a_rf0 == OH_BWDREF) ^ (l_rf0 == OH_BWDREF)));
                }
            }
        }
    } else if (a_avail || l_avail) {
        const int e_rf0 = a_avail ? a_rf0 : l_rf0;
        const int e_rf1 = a_avail ? a_rf1 : l_rf1;
        if (!(e_rf0 > OH_INTRA)) {
            pred_context = 2;
        } else if (!(e_rf1 > OH_INTRA)) {
            pred_context = 2;
        } else {
            pred_context = 4 * oracleUniComp(e_rf0, e_rf1);
        }
    } else {
        pred_context = 2;
    }
    return pred_context;
}
// The voting pattern (entropy_coding.c:1918-:1991 family, oracle).
int oracleVote(int cntA, int cntB) {
    return cntA == cntB ? 1 : (cntA < cntB ? 0 : 2);
}

// ---------------------------------------------------------------------------
// S12: getReferenceModeContext :1839-:1881 - exhaustive enumeration over
// the above/left availability x second-ref x backward-ref grid against
// the oracle.
// ---------------------------------------------------------------------------
TEST_CASE("getReferenceModeContext exhaustive vs oracle") {
    // Neighbor ref configurations: intra(0) single-fwd, single-bwd(ALTREF),
    // compound fwd/fwd, compound fwd/bwd, compound bwd/bwd, plus the
    // out-of-BACKWARD-range rf (REF_FRAMES = 8) the upper bound excludes.
    static const int refs[7][2] = {
        {OH_INTRA, OH_NONE}, {OH_LAST, OH_NONE}, {OH_ALTREF, OH_NONE},
        {OH_LAST, OH_ALTREF}, {OH_BWDREF, OH_ALTREF},
        {OH_ALTREF2, OH_ALTREF}, {REF_FRAMES, OH_NONE},
    };
    for (int a_avail = 0; a_avail < 2; ++a_avail) {
        for (int l_avail = 0; l_avail < 2; ++l_avail) {
            for (int ai = 0; ai < 7; ++ai) {
                for (int li = 0; li < 7; ++li) {
                    entropy::InterMbMi above{}, left{};
                    above.ref_frame[0] = refs[ai][0];
                    above.ref_frame[1] = refs[ai][1];
                    left.ref_frame[0] = refs[li][0];
                    left.ref_frame[1] = refs[li][1];
                    const entropy::InterMbD xd = makeXd(a_avail ? &above : NULL,
                                                        l_avail ? &left : NULL, a_avail,
                                                        l_avail);
                    const int got = entropy::getReferenceModeContext(&xd);
                    const int want = oracleRefModeCtx(a_avail, l_avail, refs[ai][0],
                                                      refs[ai][1], refs[li][0], refs[li][1]);
                    CHECK_EQ(got, want);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// S12: getCompReferenceTypeContext :1701-:1770 - the same grid (including
// the out-of-BACKWARD-range rf the upper bound excludes).
// ---------------------------------------------------------------------------
TEST_CASE("getCompReferenceTypeContext exhaustive vs oracle") {
    static const int refs[7][2] = {
        {OH_INTRA, OH_NONE}, {OH_LAST, OH_NONE}, {OH_ALTREF, OH_NONE},
        {OH_LAST, OH_ALTREF}, {OH_BWDREF, OH_ALTREF},
        {OH_ALTREF2, OH_ALTREF}, {REF_FRAMES, OH_NONE},
    };
    for (int a_avail = 0; a_avail < 2; ++a_avail) {
        for (int l_avail = 0; l_avail < 2; ++l_avail) {
            for (int ai = 0; ai < 7; ++ai) {
                for (int li = 0; li < 7; ++li) {
                    entropy::InterMbMi above{}, left{};
                    above.ref_frame[0] = refs[ai][0];
                    above.ref_frame[1] = refs[ai][1];
                    left.ref_frame[0] = refs[li][0];
                    left.ref_frame[1] = refs[li][1];
                    const entropy::InterMbD xd = makeXd(a_avail ? &above : NULL,
                                                        l_avail ? &left : NULL, a_avail,
                                                        l_avail);
                    const int got = entropy::getCompReferenceTypeContext(&xd);
                    const int want = oracleCompRefTypeCtx(a_avail, l_avail, refs[ai][0],
                                                          refs[ai][1], refs[li][0], refs[li][1]);
                    CHECK_EQ(got, want);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// S12: the count-based contexts (uni_comp_ref p/p1/p2, the shared voting
// family, single_ref p1..p6) - exhaustive over ref-count vectors.
// ---------------------------------------------------------------------------
TEST_CASE("count-based ref contexts exhaustive vs oracle") {
    // Sweep the counts of the seven slots the voters read (the InterMbD
    // carrier owns the neighbors_ref_counts array, coding_unit.h:102).
    entropy::InterMbD xd{};
    // uni_comp_ref_p: frf(L,L2,L3,G) vs brf(b,A2,A).
    for (int frf = 0; frf <= 6; ++frf) {
        for (int brf = 0; brf <= 6; ++brf) {
            std::memset(xd.neighbors_ref_counts, 0, sizeof(xd.neighbors_ref_counts));
            xd.neighbors_ref_counts[OH_LAST] = (std::uint8_t)(frf > 2 ? 2 : frf);
            xd.neighbors_ref_counts[OH_LAST2] = (std::uint8_t)(frf > 4 ? 2 : (frf > 2 ? frf - 2 : 0));
            xd.neighbors_ref_counts[OH_LAST3] = 1;
            xd.neighbors_ref_counts[OH_GOLDEN] = (std::uint8_t)((frf - 1 > 2 ? 0 : frf - 1) & 0xff);
            xd.neighbors_ref_counts[OH_BWDREF] = (std::uint8_t)brf;
            const int got = entropy::getPredContextUniCompRefP(&xd);
            int f = 0, b = 0;
            for (int i = OH_LAST; i <= OH_GOLDEN; ++i) f += xd.neighbors_ref_counts[i];
            for (int i = OH_BWDREF; i <= OH_ALTREF; ++i) b += xd.neighbors_ref_counts[i];
            CHECK_EQ(got, oracleVote(f, b));
        }
    }
    // The shared voting family via the comp/single wrappers: LAST+L2 vs
    // L3+G (comp_ref_p / single_ref_p3), LAST vs L2 (p1 / p4), L3 vs G
    // (p2 / p5), B+A2 vs A (bwdref_p / single_ref_p2), B vs A2 (p1 / p6).
    entropy::InterMbD xd2{};
    const int sweep[5] = {0, 1, 2, 3, 4};
    for (int cL = 0; cL < 5; ++cL) {
        for (int cL2 = 0; cL2 < 5; ++cL2) {
            for (int cL3 = 0; cL3 < 5; ++cL3) {
                for (int cG = 0; cG < 5; ++cG) {
                    for (int cB = 0; cB < 5; ++cB) {
                        for (int cA2 = 0; cA2 < 5; ++cA2) {
                            for (int cA = 0; cA < 5; ++cA) {
                                if (cL + cL2 + cL3 + cG + cB + cA2 + cA > 8) continue;
                                std::memset(xd2.neighbors_ref_counts, 0,
                                            sizeof(xd2.neighbors_ref_counts));
                                xd2.neighbors_ref_counts[OH_LAST] = (std::uint8_t)cL;
                                xd2.neighbors_ref_counts[OH_LAST2] = (std::uint8_t)cL2;
                                xd2.neighbors_ref_counts[OH_LAST3] = (std::uint8_t)cL3;
                                xd2.neighbors_ref_counts[OH_GOLDEN] = (std::uint8_t)cG;
                                xd2.neighbors_ref_counts[OH_BWDREF] = (std::uint8_t)cB;
                                xd2.neighbors_ref_counts[OH_ALTREF2] = (std::uint8_t)cA2;
                                xd2.neighbors_ref_counts[OH_ALTREF] = (std::uint8_t)cA;
                                CHECK_EQ(entropy::getPredContextCompRefP(&xd2),
                                         oracleVote(cL + cL2, cL3 + cG));
                                CHECK_EQ(entropy::getPredContextCompRefP1(&xd2),
                                         oracleVote(cL, cL2));
                                CHECK_EQ(entropy::getPredContextCompRefP2(&xd2),
                                         oracleVote(cL3, cG));
                                CHECK_EQ(entropy::getPredContextCompBwdrefP(&xd2),
                                         oracleVote(cB + cA2, cA));
                                CHECK_EQ(entropy::getPredContextCompBwdrefP1(&xd2),
                                         oracleVote(cB, cA2));
                                CHECK_EQ(entropy::getPredContextSingleRefP1(&xd2),
                                         oracleVote(cL + cL2 + cL3 + cG, cB + cA2 + cA));
                                CHECK_EQ(entropy::getPredContextSingleRefP2(&xd2),
                                         oracleVote(cB + cA2, cA));
                                CHECK_EQ(entropy::getPredContextSingleRefP3(&xd2),
                                         oracleVote(cL + cL2, cL3 + cG));
                                CHECK_EQ(entropy::getPredContextSingleRefP4(&xd2),
                                         oracleVote(cL, cL2));
                                CHECK_EQ(entropy::getPredContextSingleRefP5(&xd2),
                                         oracleVote(cL3, cG));
                                CHECK_EQ(entropy::getPredContextSingleRefP6(&xd2),
                                         oracleVote(cB, cA2));
                            }
                        }
                    }
                }
            }
        }
    }
    // The uni p1/p2 voters over their own count pairs.
    std::memset(xd2.neighbors_ref_counts, 0, sizeof(xd2.neighbors_ref_counts));
    for (int cL2 = 0; cL2 < 4; ++cL2) {
        for (int cL3 = 0; cL3 < 4; ++cL3) {
            for (int cG = 0; cG < 4; ++cG) {
                xd2.neighbors_ref_counts[OH_LAST2] = (std::uint8_t)cL2;
                xd2.neighbors_ref_counts[OH_LAST3] = (std::uint8_t)cL3;
                xd2.neighbors_ref_counts[OH_GOLDEN] = (std::uint8_t)cG;
                CHECK_EQ(entropy::getPredContextUniCompRefP1(&xd2), oracleVote(cL2, cL3 + cG));
                CHECK_EQ(entropy::getPredContextUniCompRefP2(&xd2), oracleVote(cL3, cG));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// S12: svt_aom_collect_neighbors_ref_counts_new :1883-:1908 - the count
// accumulation over the above/left availability x inter x compound grid.
// ---------------------------------------------------------------------------
TEST_CASE("collectNeighborsRefCounts accumulation vs oracle") {
    static const int refs[5][2] = {
        {OH_INTRA, OH_NONE}, {OH_LAST, OH_NONE}, {OH_ALTREF, OH_NONE},
        {OH_LAST, OH_ALTREF}, {OH_BWDREF, OH_ALTREF},
    };
    for (int a_avail = 0; a_avail < 2; ++a_avail) {
        for (int l_avail = 0; l_avail < 2; ++l_avail) {
            for (int ai = 0; ai < 5; ++ai) {
                for (int li = 0; li < 5; ++li) {
                    entropy::InterMbMi above{}, left{};
                    above.ref_frame[0] = refs[ai][0];
                    above.ref_frame[1] = refs[ai][1];
                    left.ref_frame[0] = refs[li][0];
                    left.ref_frame[1] = refs[li][1];
                    entropy::InterMbD xd = makeXd(a_avail ? &above : NULL,
                                                  l_avail ? &left : NULL, a_avail, l_avail);
                    entropy::collectNeighborsRefCounts(&xd);
                    std::uint8_t want[TOTAL_REFS_PER_FRAME] = {0};
                    if (a_avail && refs[ai][0] > OH_INTRA) {
                        want[refs[ai][0]]++;
                        if (refs[ai][1] > OH_INTRA) want[refs[ai][1]]++;
                    }
                    if (l_avail && refs[li][0] > OH_INTRA) {
                        want[refs[li][0]]++;
                        if (refs[li][1] > OH_INTRA) want[refs[li][1]]++;
                    }
                    for (int i = 0; i < TOTAL_REFS_PER_FRAME; ++i) {
                        CHECK_EQ((int)xd.neighbors_ref_counts[i], (int)want[i]);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// S12: the cdf getters - pointer identity against the expected family row
// (the getters must return &fc->family[ctx][slot], not a copy).
// ---------------------------------------------------------------------------
TEST_CASE("ref cdf getters return the expected rows") {
    entropy::InterFc fc;
    entropy::initDefaultInterFrameContext(&fc);
    entropy::InterMbD xd = makeXd(NULL, NULL, 0, 0);  // no edges -> ctx 1
    CHECK_EQ(entropy::getReferenceModeCdf(&fc, &xd), fc.comp_inter_cdf[1]);
    CHECK_EQ(entropy::getCompReferenceTypeCdf(&fc, &xd), fc.comp_ref_type_cdf[2]);
    CHECK_EQ(entropy::getPredCdfUniCompRefP(&fc, &xd), fc.uni_comp_ref_cdf[1][0]);
    CHECK_EQ(entropy::getPredCdfUniCompRefP1(&fc, &xd), fc.uni_comp_ref_cdf[1][1]);
    CHECK_EQ(entropy::getPredCdfUniCompRefP2(&fc, &xd), fc.uni_comp_ref_cdf[1][2]);
    CHECK_EQ(entropy::getPredCdfSingleRefP1(&fc, &xd), fc.single_ref_cdf[1][0]);
    CHECK_EQ(entropy::getPredCdfSingleRefP2(&fc, &xd), fc.single_ref_cdf[1][1]);
    CHECK_EQ(entropy::getPredCdfSingleRefP3(&fc, &xd), fc.single_ref_cdf[1][2]);
    CHECK_EQ(entropy::getPredCdfSingleRefP4(&fc, &xd), fc.single_ref_cdf[1][3]);
    CHECK_EQ(entropy::getPredCdfSingleRefP5(&fc, &xd), fc.single_ref_cdf[1][4]);
    CHECK_EQ(entropy::getPredCdfSingleRefP6(&fc, &xd), fc.single_ref_cdf[1][5]);
    CHECK_EQ(entropy::getPredCdfCompRefP(&fc, &xd), fc.comp_ref_cdf[1][0]);
    CHECK_EQ(entropy::getPredCdfCompRefP1(&fc, &xd), fc.comp_ref_cdf[1][1]);
    CHECK_EQ(entropy::getPredCdfCompRefP2(&fc, &xd), fc.comp_ref_cdf[1][2]);
    CHECK_EQ(entropy::getPredCdfCompBwdrefP(&fc, &xd), fc.comp_bwdref_cdf[1][0]);
    CHECK_EQ(entropy::getPredCdfCompBwdrefP1(&fc, &xd), fc.comp_bwdref_cdf[1][1]);
}

// ---------------------------------------------------------------------------
// S13: write_ref_frames :2104-:2187 - the full ref-frame symbol walk over
// the single / uni-compound / bidir-compound paths, with the reference-
// mode gate, asserted against an oracle transcription of the span (the
// symbol sequence + the cdf rows + the adaptation lockstep).
// ---------------------------------------------------------------------------
namespace {
// The oracle: emits (cdfRowPointer, symbol) steps via out-params. The rows
// are identified by (family, ctx, slot) triples resolved against fc.
struct RefStep {
    int family;  // 0 comp_inter, 1 comp_ref_type, 2 uni_p, 3 uni_p1, 4 uni_p2,
                 // 5 comp_ref_p, 6 comp_ref_p1, 7 comp_ref_p2, 8 comp_bwdref_p,
                 // 9 comp_bwdref_p1, 10 single_p1..15 single_p6
    int ctx;
    int slot;
    int symbol;
};
int oracleWriteRefFrames(const entropy::InterMbD* xd, const entropy::InterMbMi* mbmi,
                         entropy::BlockSize bsize, int reference_mode, RefStep* steps) {
    int n = 0;
    const int is_compound = mbmi->ref_frame[1] > OH_INTRA;
    if (reference_mode == 2 /*REFERENCE_MODE_SELECT*/) {
        if (entropy::blockSizeWide[bsize] >= 8 && entropy::blockSizeHigh[bsize] >= 8) {
            steps[n++] = {0, entropy::getReferenceModeContext(xd), 0, is_compound};
        }
    }
    if (is_compound) {
        const int comp_ref_type = oracleUniComp(mbmi->ref_frame[0], mbmi->ref_frame[1]) ? 0 : 1;
        steps[n++] = {1, entropy::getCompReferenceTypeContext(xd), 0, comp_ref_type};
        if (comp_ref_type == 0) {  // UNIDIR
            const int bit = mbmi->ref_frame[0] == OH_BWDREF;
            steps[n++] = {2, entropy::getPredContextUniCompRefP(xd), 0, bit};
            if (!bit) {
                const int bit1 = mbmi->ref_frame[1] == OH_LAST3 || mbmi->ref_frame[1] == OH_GOLDEN;
                steps[n++] = {3, entropy::getPredContextUniCompRefP1(xd), 1, bit1};
                if (bit1) {
                    const int bit2 = mbmi->ref_frame[1] == OH_GOLDEN;
                    steps[n++] = {4, entropy::getPredContextUniCompRefP2(xd), 2, bit2};
                }
            }
            return n;
        }
        const int bit = mbmi->ref_frame[0] == OH_GOLDEN || mbmi->ref_frame[0] == OH_LAST3;
        steps[n++] = {5, entropy::getPredContextCompRefP(xd), 0, bit};
        if (!bit) {
            const int bit1 = mbmi->ref_frame[0] == OH_LAST2;
            steps[n++] = {6, entropy::getPredContextCompRefP1(xd), 1, bit1};
        } else {
            const int bit2 = mbmi->ref_frame[0] == OH_GOLDEN;
            steps[n++] = {7, entropy::getPredContextCompRefP2(xd), 2, bit2};
        }
        const int bit_bwd = mbmi->ref_frame[1] == OH_ALTREF;
        steps[n++] = {8, entropy::getPredContextCompBwdrefP(xd), 0, bit_bwd};
        if (!bit_bwd) {
            const int bit3 = mbmi->ref_frame[1] == OH_ALTREF2;
            steps[n++] = {9, entropy::getPredContextCompBwdrefP1(xd), 1, bit3};
        }
    } else {
        const int bit0 = (mbmi->ref_frame[0] <= OH_ALTREF && mbmi->ref_frame[0] >= OH_BWDREF);
        steps[n++] = {10, entropy::getPredContextSingleRefP1(xd), 0, bit0};
        if (bit0) {
            const int bit1 = mbmi->ref_frame[0] == OH_ALTREF;
            steps[n++] = {11, entropy::getPredContextSingleRefP2(xd), 1, bit1};
            if (!bit1) {
                const int bit6 = mbmi->ref_frame[0] == OH_ALTREF2;
                steps[n++] = {15, entropy::getPredContextSingleRefP6(xd), 5, bit6};
            }
        } else {
            const int bit2 = mbmi->ref_frame[0] == OH_LAST3 || mbmi->ref_frame[0] == OH_GOLDEN;
            steps[n++] = {12, entropy::getPredContextSingleRefP3(xd), 2, bit2};
            if (!bit2) {
                const int bit3 = mbmi->ref_frame[0] != OH_LAST;
                steps[n++] = {13, entropy::getPredContextSingleRefP4(xd), 3, bit3};
            } else {
                const int bit4 = mbmi->ref_frame[0] != OH_LAST3;
                steps[n++] = {14, entropy::getPredContextSingleRefP5(xd), 4, bit4};
            }
        }
    }
    return n;
}
}  // namespace

TEST_CASE("writeRefFrames symbol walk vs oracle") {
    entropy::InterFc fcW, fcR;
    entropy::initDefaultInterFrameContext(&fcW);
    fcR = fcW;
    struct Case {
        int reference_mode;  // 0 SINGLE, 2 SELECT
        entropy::BlockSize bsize;
        int rf0, rf1;
    };
    static const Case cases[18] = {
        // Single refs (REFERENCE_MODE_SELECT, comp allowed at 8x8).
        {2, entropy::BLOCK_8X8, OH_LAST, OH_NONE},
        {2, entropy::BLOCK_8X8, OH_LAST2, OH_NONE},
        {2, entropy::BLOCK_8X8, OH_LAST3, OH_NONE},
        {2, entropy::BLOCK_8X8, OH_GOLDEN, OH_NONE},
        {2, entropy::BLOCK_8X8, OH_BWDREF, OH_NONE},
        {2, entropy::BLOCK_8X8, OH_ALTREF2, OH_NONE},
        {2, entropy::BLOCK_8X8, OH_ALTREF, OH_NONE},
        // SINGLE_REFERENCE frame: no comp-inter symbol.
        {0, entropy::BLOCK_8X8, OH_LAST, OH_NONE},
        {0, entropy::BLOCK_8X8, OH_ALTREF, OH_NONE},
        // SELECT but comp NOT allowed (4x4): no comp-inter symbol, single path.
        {2, entropy::BLOCK_4X4, OH_ALTREF, OH_NONE},
        // Uni-compound pairs.
        {2, entropy::BLOCK_8X8, OH_LAST, OH_LAST2},
        {2, entropy::BLOCK_8X8, OH_LAST, OH_LAST3},
        {2, entropy::BLOCK_8X8, OH_LAST, OH_GOLDEN},
        {2, entropy::BLOCK_8X8, OH_BWDREF, OH_ALTREF},
        {2, entropy::BLOCK_8X8, OH_LAST2, OH_GOLDEN},
        // Bidir-compound pairs.
        {2, entropy::BLOCK_16X16, OH_LAST, OH_ALTREF},
        {2, entropy::BLOCK_16X16, OH_GOLDEN, OH_ALTREF2},
        {2, entropy::BLOCK_16X16, OH_LAST2, OH_BWDREF},
    };
    for (int tc = 0; tc < 16; ++tc) {
        const Case& c = cases[tc];
        entropy::InterMbMi mbmi{};
        mbmi.ref_frame[0] = c.rf0;
        mbmi.ref_frame[1] = c.rf1;
        entropy::InterMbD xd = makeXd(NULL, NULL, 0, 0);  // no edges: ctx fixed
        entropy::collectNeighborsRefCounts(&xd);
        entropy::AomWriter w{};
        unsigned char buf[64] = {0};
        w.ec.buf = buf;
        entropy::odEcEncReset(&w.ec);
        w.allow_update_cdf = 1;
        w.pos = 0;
        entropy::writeRefFrames(&w, &fcW, &xd, &mbmi, c.bsize, c.reference_mode);
        entropy::odEcStopEncode(&w);
        // The oracle steps.
        RefStep steps[8];
        const int n = oracleWriteRefFrames(&xd, &mbmi, c.bsize, c.reference_mode, steps);
        entropy::AomReader r;
        REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
        r.allow_update_cdf = 1;
        for (int k = 0; k < n; ++k) {
            const RefStep& s = steps[k];
            entropy::AomCdfProb* rowR = nullptr;
            entropy::AomCdfProb* rowW = nullptr;
            switch (s.family) {
            case 0: rowR = fcR.comp_inter_cdf[s.ctx]; rowW = fcW.comp_inter_cdf[s.ctx]; break;
            case 1: rowR = fcR.comp_ref_type_cdf[s.ctx]; rowW = fcW.comp_ref_type_cdf[s.ctx]; break;
            case 2: rowR = fcR.uni_comp_ref_cdf[s.ctx][0]; rowW = fcW.uni_comp_ref_cdf[s.ctx][0]; break;
            case 3: rowR = fcR.uni_comp_ref_cdf[s.ctx][1]; rowW = fcW.uni_comp_ref_cdf[s.ctx][1]; break;
            case 4: rowR = fcR.uni_comp_ref_cdf[s.ctx][2]; rowW = fcW.uni_comp_ref_cdf[s.ctx][2]; break;
            case 5: rowR = fcR.comp_ref_cdf[s.ctx][0]; rowW = fcW.comp_ref_cdf[s.ctx][0]; break;
            case 6: rowR = fcR.comp_ref_cdf[s.ctx][1]; rowW = fcW.comp_ref_cdf[s.ctx][1]; break;
            case 7: rowR = fcR.comp_ref_cdf[s.ctx][2]; rowW = fcW.comp_ref_cdf[s.ctx][2]; break;
            case 8: rowR = fcR.comp_bwdref_cdf[s.ctx][0]; rowW = fcW.comp_bwdref_cdf[s.ctx][0]; break;
            case 9: rowR = fcR.comp_bwdref_cdf[s.ctx][1]; rowW = fcW.comp_bwdref_cdf[s.ctx][1]; break;
            case 10: rowR = fcR.single_ref_cdf[s.ctx][0]; rowW = fcW.single_ref_cdf[s.ctx][0]; break;
            case 11: rowR = fcR.single_ref_cdf[s.ctx][1]; rowW = fcW.single_ref_cdf[s.ctx][1]; break;
            case 12: rowR = fcR.single_ref_cdf[s.ctx][2]; rowW = fcW.single_ref_cdf[s.ctx][2]; break;
            case 13: rowR = fcR.single_ref_cdf[s.ctx][3]; rowW = fcW.single_ref_cdf[s.ctx][3]; break;
            case 14: rowR = fcR.single_ref_cdf[s.ctx][4]; rowW = fcW.single_ref_cdf[s.ctx][4]; break;
            case 15: rowR = fcR.single_ref_cdf[s.ctx][5]; rowW = fcW.single_ref_cdf[s.ctx][5]; break;
            }
            CHECK_EQ(entropy::odEcReadSymbol(&r, rowR, 2), s.symbol);
            REQUIRE(std::memcmp(rowW, rowR, sizeof(fcR.comp_inter_cdf[0])) == 0);
        }
        // Exactly n symbols were consumed: the remaining stream is the
        // od_ec flush tail only (compare against a no-symbol baseline).
        entropy::AomWriter wBase{};
        unsigned char bufBase[64] = {0};
        wBase.ec.buf = bufBase;
        entropy::odEcEncReset(&wBase.ec);
        wBase.allow_update_cdf = 1;
        entropy::odEcStopEncode(&wBase);
        entropy::AomReader rBase;
        REQUIRE(entropy::odEcReaderInit(&rBase, bufBase, wBase.pos) == 0);
        rBase.allow_update_cdf = 1;
        entropy::AomCdfProb tail[3] = {AOM_ICDF(16384), 0, 0};
        entropy::AomCdfProb tailBase[3] = {AOM_ICDF(16384), 0, 0};
        entropy::odEcReadSymbol(&r, tail, 2);
        entropy::odEcReadSymbol(&rBase, tailBase, 2);
    }
}

// ---------------------------------------------------------------------------
// S1: svt_aom_get_comp_index_context_enc :52-:78 - exhaustive enumeration
// of the neighbour configurations (above/left availability x ref counts x
// mode classes x the fwd/bck distance relation) against the oracle.
// ---------------------------------------------------------------------------
TEST_CASE("getCompIndexContextEnc exhaustive vs oracle") {
    struct MiCase {
        int avail;      // above/left available
        int second;     // has_second_ref
        int compound_idx;
        int rf0;
    };
    // The reference's consumed field space: not-available, intra (rf0
    // INTRA), the four forward refs, the three backward refs, single vs
    // second-ref with compound_idx 0/1.
    static const MiCase miCases[14] = {
        {0, 0, 0, OH_INTRA},  // unavailable -> both ctx terms 0
        {1, 0, 0, OH_INTRA},  // intra rf0, no second
        {1, 0, 0, OH_LAST},   // single forward
        {1, 0, 0, OH_LAST2},  // single forward
        {1, 0, 0, OH_LAST3},  // single forward
        {1, 0, 0, OH_GOLDEN}, // single forward
        {1, 0, 0, OH_ALTREF}, // the ALTREF single-ref ctx = 1 branch
        {1, 0, 0, OH_BWDREF}, // single backward, non-ALTREF -> 0
        {1, 0, 0, OH_ALTREF2},// single backward, non-ALTREF -> 0
        {1, 1, 0, OH_LAST},   // compound idx 0 -> ctx 0
        {1, 1, 1, OH_LAST},   // compound idx 1 -> ctx 1
        {1, 1, 0, OH_ALTREF}, // second overrides the ALTREF branch
        {1, 1, 1, OH_ALTREF},
        {1, 1, 1, OH_BWDREF},
    };
    // The order-hint distance space: enabled with 4/7 bits, disabled, and
    // the fwd==bck / fwd!=bck relations over representative indices.
    struct DistCase {
        int enable;
        int bits;
        int cur, bck, fwd;
    };
    static const DistCase distCases[8] = {
        {1, 7, 5, 5, 5}, {1, 7, 5, 8, 8}, {1, 7, 5, 8, 2}, {1, 7, 130, 2, 258},
        {1, 4, 3, 9, 9}, {1, 4, 3, 9, 2}, {0, 7, 5, 9, 2}, {0, 4, 5, 9, 2},
    };
    for (int ac = 0; ac < 14; ++ac) {
        for (int lc = 0; lc < 14; ++lc) {
            for (int dc = 0; dc < 8; ++dc) {
                const MiCase& a = miCases[ac];
                const MiCase& l = miCases[lc];
                const DistCase& d = distCases[dc];
                entropy::InterMbMi above{}, left{};
                above.ref_frame[0] = a.rf0;
                above.ref_frame[1] = a.second ? OH_LAST : OH_NONE;
                above.compound_idx = a.compound_idx;
                left.ref_frame[0] = l.rf0;
                left.ref_frame[1] = l.second ? OH_LAST : OH_NONE;
                left.compound_idx = l.compound_idx;
                const entropy::InterMbD xd = makeXd(a.avail ? &above : NULL,
                                                    l.avail ? &left : NULL, a.avail, l.avail);
                const int got = entropy::getCompIndexContextEnc(
                    &xd, d.cur, d.bck, d.fwd, d.enable, d.bits);
                const int want = oracleCompIndexCtx(
                    a.avail, a.second, a.compound_idx, a.rf0, l.avail, l.second, l.compound_idx,
                    l.rf0, d.cur, d.bck, d.fwd, d.enable, d.bits);
                CHECK_EQ(got, want);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// S1: svt_aom_get_comp_group_idx_context_enc :80-:99 - exhaustive
// enumeration against the oracle, including the AOMMIN(5, ...) clamp
// (both ctx terms 3 -> sum 6 -> 5).
// ---------------------------------------------------------------------------
TEST_CASE("getCompGroupIdxContextEnc exhaustive vs oracle + clamp") {
    struct MiCase {
        int avail;
        int second;
        int group;
        int rf0;
    };
    static const MiCase miCases[10] = {
        {0, 0, 0, OH_INTRA},
        {1, 0, 0, OH_INTRA},
        {1, 0, 0, OH_ALTREF},   // the ALTREF single-ref ctx = 3 branch
        {1, 0, 0, OH_LAST},     // single forward -> 0
        {1, 0, 0, OH_BWDREF},   // single backward, non-ALTREF -> 0
        {1, 1, 0, OH_LAST},     // compound group 0 -> ctx 0
        {1, 1, 1, OH_LAST},     // compound group 1 -> ctx 1
        {1, 1, 1, OH_ALTREF},   // second overrides the ALTREF branch
        {1, 1, 0, OH_ALTREF},
        {1, 1, 1, OH_GOLDEN},
    };
    for (int ac = 0; ac < 10; ++ac) {
        for (int lc = 0; lc < 10; ++lc) {
            const MiCase& a = miCases[ac];
            const MiCase& l = miCases[lc];
            entropy::InterMbMi above{}, left{};
            above.ref_frame[0] = a.rf0;
            above.ref_frame[1] = a.second ? OH_LAST : OH_NONE;
            above.comp_group_idx = a.group;
            left.ref_frame[0] = l.rf0;
            left.ref_frame[1] = l.second ? OH_LAST : OH_NONE;
            left.comp_group_idx = l.group;
            const entropy::InterMbD xd = makeXd(a.avail ? &above : NULL,
                                                l.avail ? &left : NULL, a.avail, l.avail);
            const int got = entropy::getCompGroupIdxContextEnc(&xd);
            const int want = oracleCompGroupIdxCtx(
                a.avail, a.second, a.group, a.rf0, l.avail, l.second, l.group, l.rf0);
            CHECK_EQ(got, want);
            // The clamp: both ALTREF single-ref -> 3+3 = 6 clamps to 5.
            if (a.avail && l.avail && !a.second && !l.second && a.rf0 == OH_ALTREF &&
                l.rf0 == OH_ALTREF) {
                CHECK_EQ(got, 5);
            }
        }
    }
}
