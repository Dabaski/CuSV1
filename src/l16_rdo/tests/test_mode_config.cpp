// EMC1 - the A4 mode-config surface (slice 1: the standalone arithmetic).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/enc_mode_config.c
// Spans in this slice (the brief's order):
//   svt_aom_get_qp_based_th_scaling_factors  :26-:54
//   the pf_gi / angular_pred_level tables    :20-:24
//   svt_aom_get_wn_filter_level_{default,rtc,allintra}  :1365-:1407
//   svt_aom_get_sg_filter_level_{default,rtc,allintra}  :1410-:1448
//   dlf_level_modulation + get_dlf_level_{default,rtc,allintra} :1450-:1567
//   svt_aom_set_gm_controls                  :2523-:2625
// BRIEF CORRECTION (grep-verified against the reference): the brief's
// "tb/trl level getters" name nothing in enc_mode_config.c - the ":21-:24
// area" is pf_gi (:21) + angular_pred_level (:23) + the three level
// macros; no identifier matching tb/trl exists in the file.
#include <doctest.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <initializer_list>
#include <vector>
#include "mode_config.h"

using namespace l16_rdo::mode_config;

// ---------------------------------------------------------------------------
// Independent oracles (test-side transcriptions of the reference
// arithmetic, never shared with the port).
// ---------------------------------------------------------------------------

// MAX (aom_dsp/*.h: MAX(a, b) ((a) > (b) ? (a) : (b))).
static uint32_t oracleMaxU32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

// svt_aom_get_qp_based_th_scaling_factors, independent transcription
// (enc_mode_config.c:26-54). MAX_QP_VALUE = 63 (definitions.h:1646).
static void oracleQpThScaling(bool enable, uint32_t* w, uint32_t* d, uint32_t qp) {
    if (enable) {
        uint32_t q_weight = oracleMaxU32(10u, qp);
        uint32_t q_weight_denom = 63u;
        if (qp >= 46) {
            // The reference's MAX is over (double)qp (enc_mode_config.c:39):
            //   double ex = -(MAX(40, (double)qp) - 35) / 10;
            double ex = -(std::max(40.0, (double)qp) - 35) / 10;
            double q_weight_int = std::exp(ex);
            q_weight_int = 1.05 - q_weight_int;
            q_weight_int *= 10000;
            q_weight = (uint32_t)q_weight_int;
            q_weight_denom = 10000u;
        }
        *w = q_weight;
        *d = q_weight_denom;
    } else {
        *w = 1;
        *d = 1;
    }
}

// The wn/sg/dlf ladders, independent transcriptions
// (enc_mode_config.c:1365-1567). Res constants: INPUT_SIZE_8K_RANGE = 6,
// INPUT_SIZE_360p_RANGE = 1 (definitions.h:1808-1817).
static uint8_t oracleWnDefault(int mode, int res, bool notLast) {
    uint8_t lvl;
    if (mode <= 3)       lvl = notLast ? 4 : 0;  // :1368-1369 (<= ENC_M3)
    else if (mode <= 8)  lvl = notLast ? 5 : 0;  // :1370-1371 (<= ENC_M8)
    else                 lvl = 0;               // :1372-1373
    if (res >= 6) lvl = 0;                       // :1376-1378 (>= 8K)
    return lvl;
}

static uint8_t oracleSgDefault(int mode, int res, int fastDecode) {
    uint8_t lvl;
    if (mode <= -1)      lvl = 1;  // :1412-1413 (<= ENC_MR)
    else if (mode <= 3)  lvl = 3;  // :1414-1415 (<= ENC_M3)
    else                 lvl = 0;  // :1416-1417
    // :1421 (8K OR fast_decode && !(<= 360p))
    if (res >= 6 || (fastDecode && !(res <= 1))) lvl = 0;
    return lvl;
}

// dlf_level_modulation, independent transcription (:1450-1472).
static void oracleDlfMod(uint8_t refSkip, uint8_t* pLevel, uint8_t modMode) {
    uint8_t lvl = *pLevel;
    if (modMode == 1 || modMode == 2) {                  // :1453
        if (refSkip < 25) {                              // :1454
            lvl = lvl == 0 ? 6 : lvl > 5 ? (5 > (uint8_t)(lvl - 2) ? 5 : (uint8_t)(lvl - 2)) : lvl;
        } else if (refSkip < 50) {                       // :1456
            lvl = lvl == 0 ? 7 : lvl > 5 ? (uint8_t)(lvl - 1) : lvl;
        }
    }
    if (modMode == 2 || modMode == 3) {                  // :1461
        if (lvl > 4) {                                   // :1462
            if (refSkip > 95)                            // :1463
                lvl = lvl >= 6 ? (uint8_t)0 : (uint8_t)(lvl + 2);
            else if (refSkip > 75)                       // :1465
                lvl = lvl == 7 ? (uint8_t)0 : (uint8_t)(lvl + 1);
        }
    }
    *pLevel = lvl;
}

// get_dlf_level_default, independent transcription (:1474-1523).
static uint8_t oracleDlfDefault(uint8_t refSkip, uint8_t coeffLvl, int mode, int notLast,
                                int fastDecode, int res, int isBase) {
    uint8_t lvl = 0;
    uint8_t mod = 0;
    if (fastDecode <= 1 || res <= 1) {                   // :1479
        if (mode <= 0)        lvl = 1;                   // :1480
        else if (mode <= 3)   lvl = 2;                   // :1482
        else if (mode <= 6)   lvl = notLast ? 3 : 6;     // :1484-1485
        else if (mode <= 7) { lvl = notLast ? 3 : 6; mod = 3; }  // :1486-1488
        else if (mode <= 9) { lvl = notLast ? 6 : 0; mod = 3; }  // :1489-1491
        else if (mode <= 11) {                           // :1492-1498
            if (coeffLvl == 3 /*HIGH_LVL*/) lvl = isBase ? 6 : 0;
            else                            lvl = isBase ? 6 : (notLast ? 7 : 0);
            mod = 3;
        } else {              lvl = 0; mod = 3; }        // :1499-1501
    } else {                                          // :1503 fast-decode 2
        if (mode <= 6)        lvl = 4;                   // :1504-1505
        else if (mode <= 7) { lvl = 6; mod = 3; }         // :1506-1508
        else if (mode <= 10) { lvl = notLast ? 6 : 0; mod = 3; }  // :1509-1511
        else                 { lvl = notLast ? 7 : 0; mod = 3; }  // :1512-1514
    }
    if (!isBase) oracleDlfMod(refSkip, &lvl, mod);        // :1518-1519
    return lvl;
}

// get_dlf_level_rtc, independent transcription (:1525-1546).
static uint8_t oracleDlfRtc(uint8_t refSkip, int mode, int isBase) {
    uint8_t lvl = 0;
    uint8_t mod = 0;
    if (mode <= 7)       { lvl = 3; mod = 1; }   // :1528-1530
    else if (mode <= 9)  { lvl = 6; mod = 3; }   // :1531-1533
    else if (mode <= 10) { lvl = 7; mod = 3; }   // :1534-1536
    else                 { lvl = 0; mod = 3; }   // :1537-1539
    if (!isBase) oracleDlfMod(refSkip, &lvl, mod);  // :1541-1542
    return lvl;
}

// get_dlf_level_allintra, independent transcription (:1548-1567).
static uint8_t oracleDlfAllIntra(int mode, int fastDecode, int res) {
    uint8_t lvl = 0;
    if (fastDecode <= 1 || res <= 1) {   // :1550
        if (mode <= 3)      lvl = 1;     // :1551-1552
        else if (mode <= 5) lvl = 2;     // :1553-1554
        else                lvl = 5;     // :1555-1556
    } else {                             // :1558
        if (mode <= 7)      lvl = 0;     // :1559-1560
        else                lvl = 5;     // :1561-1562
    }
    return lvl;
}

// The GmControls mirror, independent transcription (pcs.h:512-552) - the
// full 16-field struct the reference case bodies write.
struct GmOracle {
    int enabled;
    int identiyExit;
    int searchStartModel;
    int searchEndModel;
    int skipIdentity;
    int bypassBasedOnMe;
    int paramsRefinementSteps;
    int downsampleLevel;
    int corners;
    int chessRfn;
    int matchSz;
    bool injPsqGlb;
    bool ppEnabled;
    bool refIdx0Only;
    int rfnEarlyExit;
    int correspondenceMethod;
};

// svt_aom_set_gm_controls, independent transcription (:2523-2625). The
// enum values: TRANSLATION = 1, ROTZOOM = 2, AFFINE = 3
// (definitions.h:1740-1745); GM_FULL = 0 (definitions.h:235-244);
// CORNERS = 4, MV_8x8 = 3, MV_16x16 = 2, MV_32x32 = 1 (pcs.h:504-510).
static void oracleGm(GmOracle& c, int level, int res, bool config) {
    if (!config) {                 // :2525-2528 (CONFIG_ENABLE_GLOBAL_MOTION off)
        c.enabled = 0;
        c.ppEnabled = 0;
        return;
    }
    switch (level) {
    case 0:                        // :2531-2534
        c.enabled = 0;
        c.ppEnabled = 0;
        break;
    case 1:                        // :2536-2554
        c.enabled = 1; c.identiyExit = 0;
        c.searchStartModel = 1 /*TRANSLATION*/; c.searchEndModel = 3 /*AFFINE*/;
        c.skipIdentity = 0; c.bypassBasedOnMe = 0;
        c.paramsRefinementSteps = 5; c.downsampleLevel = 0 /*GM_FULL*/;
        c.corners = 4; c.chessRfn = 0; c.matchSz = 13;
        c.injPsqGlb = false; c.ppEnabled = 0;
        c.refIdx0Only = 0; c.rfnEarlyExit = 0;
        c.correspondenceMethod = 4 /*CORNERS*/;
        break;
    case 2:                        // :2555-2572
        c.enabled = 1; c.identiyExit = 1;
        c.searchStartModel = 1; c.searchEndModel = 2 /*ROTZOOM*/;
        c.skipIdentity = 0; c.bypassBasedOnMe = 0;
        c.paramsRefinementSteps = 5; c.downsampleLevel = 0;
        c.corners = 2; c.chessRfn = 0; c.matchSz = 7;
        c.injPsqGlb = false; c.ppEnabled = 1;
        c.refIdx0Only = 0; c.rfnEarlyExit = 0;
        c.correspondenceMethod = 4;
        break;
    case 3:                        // :2573-2592
        c.enabled = 1; c.identiyExit = 1;
        c.searchStartModel = 1; c.searchEndModel = 2;
        c.skipIdentity = 0; c.bypassBasedOnMe = 1;
        c.paramsRefinementSteps = 5; c.downsampleLevel = 0;
        c.corners = 2; c.chessRfn = 1; c.matchSz = 7;
        c.injPsqGlb = true; c.ppEnabled = 0;
        c.refIdx0Only = 1; c.rfnEarlyExit = 1;
        c.correspondenceMethod = res <= 2 /*480p*/ ? 3 /*MV_8x8*/ : res <= 4 /*1080p*/ ? 2 /*MV_16x16*/ : 1 /*MV_32x32*/;
        break;
    case 4:                        // :2593-2612
        c.enabled = 1; c.identiyExit = 1;
        c.searchStartModel = 1; c.searchEndModel = 2;
        c.skipIdentity = 1; c.bypassBasedOnMe = 1;
        c.paramsRefinementSteps = 5; c.downsampleLevel = 0;
        c.corners = 2; c.chessRfn = 1; c.matchSz = 7;
        c.injPsqGlb = true; c.ppEnabled = 0;
        c.refIdx0Only = 1; c.rfnEarlyExit = 1;
        c.correspondenceMethod = res <= 2 ? 3 : res <= 4 ? 2 : 1;
        break;
    default:                       // :2613-2615 assert(0); nothing written
        break;
    }
    if (c.correspondenceMethod < 4 /*CORNERS*/) c.ppEnabled = 0;  // :2617-2620
}

// ---------------------------------------------------------------------------
// Slice 1: the standalone arithmetic.
// ---------------------------------------------------------------------------

TEST_CASE("get_qp_based_th_scaling_factors - disabled returns 1/1 for every qp") {
    // :49-52 - the else branch is unconditional 1/1.
    for (uint32_t qp = 0; qp <= 63; ++qp) {
        uint32_t w = 0, d = 0;
        getQpBasedThScalingFactors(false, &w, &d, qp);
        CHECK(w == 1);
        CHECK(d == 1);
    }
}

TEST_CASE("get_qp_based_th_scaling_factors - enabled linear band qp 0..45") {
    // :30-31 - q_weight = MAX(10, qp), denom = MAX_QP_VALUE (63); the
    // >= 46 exp branch does not fire. qp > 63 clamps at MAX(10, qp) the
    // same way, so the linear band covers the caller domain 0..MAX_QP_VALUE.
    for (uint32_t qp = 0; qp <= 45; ++qp) {
        uint32_t w = 0, d = 0, ow = 0, od = 0;
        getQpBasedThScalingFactors(true, &w, &d, qp);
        oracleQpThScaling(true, &ow, &od, qp);
        CHECK(w == ow);
        CHECK(d == od);
        CHECK(w == (qp < 10 ? 10u : qp));  // :30 hand-derived
        CHECK(d == 63u);                   // :31
    }
}

TEST_CASE("get_qp_based_th_scaling_factors - enabled exponential band qp 46..63") {
    // :38-46 - the exp branch. Literal pins hand-derived from the reference
    // formula (double exp, truncating cast):
    //   qp 46: ex = -1.1, 1.05 - exp(-1.1) = 0.71712891628  -> 7171
    //   qp 47: ex = -1.2, 1.05 - exp(-1.2) = 0.74880578811  -> 7488
    //   qp 50: ex = -1.5, 1.05 - exp(-1.5) = 0.82686983994  -> 8268
    //   qp 63: ex = -2.8, 1.05 - exp(-2.8) = 0.98918993738  -> 9891
    struct Pin { uint32_t qp; uint32_t w; };
    static const Pin pins[] = {{46, 7171}, {47, 7488}, {50, 8268}, {63, 9891}};
    for (const Pin& p : pins) {
        uint32_t w = 0, d = 0;
        getQpBasedThScalingFactors(true, &w, &d, p.qp);
        CHECK(w == p.w);
        CHECK(d == 10000u);  // :45
    }
    for (uint32_t qp = 46; qp <= 63; ++qp) {
        uint32_t w = 0, d = 0, ow = 0, od = 0;
        getQpBasedThScalingFactors(true, &w, &d, qp);
        oracleQpThScaling(true, &ow, &od, qp);
        CHECK(w == ow);
        CHECK(d == od);
    }
}

TEST_CASE("pf_gi table - the :21 literals") {
    for (int i = 0; i < 16; ++i) CHECK(pfGi[i] == (uint8_t)(4 * i));
    // The :21 full literal, entry by entry.
    static const uint8_t kRef[16] = {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60};
    for (int i = 0; i < 16; ++i) CHECK(pfGi[i] == kRef[i]);
}

TEST_CASE("angular_pred_level table - the :23 literals") {
    static const uint8_t kRef[10] = {0, 1, 2, 2, 3, 4, 4, 4, 4, 0};
    for (int i = 0; i < 10; ++i) CHECK(angularPredLevel[i] == kRef[i]);
    CHECK(kMaxIntraLevel == 10);  // :22
}

TEST_CASE("the level macros - :20/:22/:24 and MAX_QP_VALUE") {
    CHECK(kMaxPd0Lvl == 8);      // :20
    CHECK(kMaxTxsLevel == 6);    // :24
    CHECK(kMaxQpValue == 63);    // definitions.h:1646
}

TEST_CASE("get_wn_filter_level_default - every (mode, resolution, layer) cell") {
    // :1365-1381. Boundary pins hand-derived from the ladder:
    //   mode -1/3 not-last -> 4; mode 3 last -> 0 (first branch);
    //   mode 4/8 not-last -> 5; mode 9 -> 0 (last branch);
    //   any res >= 6 forces 0 (:1376-1378).
    CHECK(getWnFilterLevelDefault(ENC_MR, 0, true) == 4);
    CHECK(getWnFilterLevelDefault(ENC_M3, 0, true) == 4);
    CHECK(getWnFilterLevelDefault(ENC_M3, 0, false) == 0);
    CHECK(getWnFilterLevelDefault(ENC_M4, 0, true) == 5);
    CHECK(getWnFilterLevelDefault(ENC_M8, 0, true) == 5);
    CHECK(getWnFilterLevelDefault(ENC_M9, 0, true) == 0);
    CHECK(getWnFilterLevelDefault(ENC_M0, 6, true) == 0);  // 8K clamp
    CHECK(getWnFilterLevelDefault(ENC_M13, 6, false) == 0);
    for (int mode = (int)ENC_MR; mode <= (int)ENC_M13; ++mode) {
        for (int res = 0; res <= 6; ++res) {
            for (int layer = 0; layer <= 1; ++layer) {
                CHECK(getWnFilterLevelDefault((EncMode)mode, (uint8_t)res, layer != 0)
                      == oracleWnDefault(mode, res, layer != 0));
            }
        }
    }
}

TEST_CASE("get_wn_filter_level_rtc - always 0 (the :1383-1392 body)") {
    for (int res = 0; res <= 6; ++res) CHECK(getWnFilterLevelRtc((uint8_t)res) == 0);
}

TEST_CASE("get_wn_filter_level_allintra - every mode cell") {
    // :1394-1407. M<=M0 -> 3, M<=M3 -> 3, M<=M6 -> 4, else 0.
    // ENC_MR = -1 lands in the first branch (<= ENC_M0).
    CHECK(getWnFilterLevelAllIntra(ENC_MR) == 3);
    CHECK(getWnFilterLevelAllIntra(ENC_M0) == 3);
    CHECK(getWnFilterLevelAllIntra(ENC_M3) == 3);
    CHECK(getWnFilterLevelAllIntra(ENC_M4) == 4);
    CHECK(getWnFilterLevelAllIntra(ENC_M6) == 4);
    CHECK(getWnFilterLevelAllIntra(ENC_M7) == 0);
    CHECK(getWnFilterLevelAllIntra(ENC_M13) == 0);
    for (int mode = (int)ENC_MR; mode <= (int)ENC_M13; ++mode) {
        uint8_t exp;
        if (mode <= 0)      exp = 3;
        else if (mode <= 3) exp = 3;
        else if (mode <= 6) exp = 4;
        else                exp = 0;
        CHECK(getWnFilterLevelAllIntra((EncMode)mode) == exp);
    }
}

TEST_CASE("get_sg_filter_level_default - every (mode, resolution, fast_decode) cell") {
    // :1410-1426. Boundary pins:
    //   MR(-1) -> 1; M0-M3 -> 3; M4+ -> 0 (:1412-1418);
    //   res >= 6 OR fast_decode && res > 1 forces 0 (:1421-1423).
    CHECK(getSgFilterLevelDefault(ENC_MR, 0, 0) == 1);
    CHECK(getSgFilterLevelDefault(ENC_M0, 0, 0) == 3);
    CHECK(getSgFilterLevelDefault(ENC_M3, 0, 0) == 3);
    CHECK(getSgFilterLevelDefault(ENC_M4, 0, 0) == 0);
    CHECK(getSgFilterLevelDefault(ENC_M3, 6, 0) == 0);   // 8K clamp
    CHECK(getSgFilterLevelDefault(ENC_M0, 2, 1) == 0);   // fast-decode + >360p
    CHECK(getSgFilterLevelDefault(ENC_M0, 1, 1) == 3);   // <= 360p survives
    for (int mode = (int)ENC_MR; mode <= (int)ENC_M13; ++mode) {
        for (int res = 0; res <= 6; ++res) {
            for (int fd = 0; fd <= 1; ++fd) {
                CHECK(getSgFilterLevelDefault((EncMode)mode, (uint8_t)res, (uint8_t)fd)
                      == oracleSgDefault(mode, res, fd));
            }
        }
    }
}

TEST_CASE("get_sg_filter_level_rtc - always 0 (the :1428-1437 body)") {
    for (int res = 0; res <= 6; ++res)
        for (int fd = 0; fd <= 1; ++fd) CHECK(getSgFilterLevelRtc((uint8_t)res, (uint8_t)fd) == 0);
}

TEST_CASE("get_sg_filter_level_allintra - <= MR -> 1, else 0") {
    // :1439-1448.
    CHECK(getSgFilterLevelAllIntra(ENC_MR) == 1);
    CHECK(getSgFilterLevelAllIntra(ENC_M0) == 0);
    CHECK(getSgFilterLevelAllIntra(ENC_M13) == 0);
    for (int mode = (int)ENC_MR; mode <= (int)ENC_M13; ++mode) {
        CHECK(getSgFilterLevelAllIntra((EncMode)mode) == (mode <= -1 ? 1 : 0));
    }
}

TEST_CASE("dlf_level_modulation - every (mode, skip, level) cell") {
    // :1450-1472. Boundary pins hand-derived:
    //   skip 10, mod 1, lvl 6 -> MAX(5, 4) = 4  (:1455, lvl > 5 branch);
    //   skip 10, mod 1, lvl 0 -> 6               (:1455);
    //   skip 30, mod 1, lvl 6 -> 5               (:1457, lvl - 1);
    //   skip 30, mod 1, lvl 5 -> 5               (:1457, lvl not > 5);
    //   skip 97, mod 3, lvl 7 -> 0               (:1464, lvl >= 6);
    //   skip 97, mod 3, lvl 5 -> 7               (:1464, lvl < 6 -> +2);
    //   skip 80, mod 3, lvl 5 -> 6               (:1466, lvl != 7 -> +1);
    //   skip 80, mod 3, lvl 7 -> 0               (:1466, lvl == 7);
    //   mod 0 -> unchanged for every cell        (:1453/:1461 gates).
    uint8_t lvl;
    lvl = 6; dlfLevelModulation({10, 0}, &lvl, 1); CHECK(lvl == 5);   // :1455 MAX(5, 6-2) = 5
    lvl = 0; dlfLevelModulation({10, 0}, &lvl, 1); CHECK(lvl == 6);
    lvl = 6; dlfLevelModulation({30, 0}, &lvl, 1); CHECK(lvl == 5);
    lvl = 5; dlfLevelModulation({30, 0}, &lvl, 1); CHECK(lvl == 5);
    lvl = 7; dlfLevelModulation({97, 0}, &lvl, 3); CHECK(lvl == 0);
    lvl = 5; dlfLevelModulation({97, 0}, &lvl, 3); CHECK(lvl == 7);
    lvl = 5; dlfLevelModulation({80, 0}, &lvl, 3); CHECK(lvl == 6);
    lvl = 7; dlfLevelModulation({80, 0}, &lvl, 3); CHECK(lvl == 0);
    lvl = 6; dlfLevelModulation({10, 0}, &lvl, 0); CHECK(lvl == 6);
    // Full domain: modulation mode 0..3 x ref-skip 0..100 x input level 0..7.
    for (int modMode = 0; modMode <= 3; ++modMode) {
        for (int skip = 0; skip <= 100; ++skip) {
            for (int inLvl = 0; inLvl <= 7; ++inLvl) {
                uint8_t out = (uint8_t)inLvl;
                dlfLevelModulation({(uint8_t)skip, 0}, &out, (uint8_t)modMode);
                uint8_t oracle = (uint8_t)inLvl;
                oracleDlfMod((uint8_t)skip, &oracle, (uint8_t)modMode);
                CHECK(out == oracle);
            }
        }
    }
}

TEST_CASE("get_dlf_level_default - every (mode, fd, res, layer, base) cell, no modulation") {
    // :1474-1523 with ref_skip 0 (modulation cannot fire on 0 for the
    // promotion branches: skip < 25 promotes). Boundary pins:
    //   fd0: M0 -> 1; M3 -> 2; M6 not-last -> 3, last -> 6;
    //   M7 not-last -> 3, last -> 6; M9 not-last -> 6, last -> 0;
    //   M11 base -> 6, non-base not-last -> 7, non-base last -> 0; M13 -> 0.
    //   fd2: M6 -> 4; M7 -> 6; M10 not-last -> 6; M11+ not-last -> 7.
    //   res <= 1 forces the fd<=1 branch even at fd2 (:1479).
    CHECK(getDlfLevelDefault({0, 0}, ENC_M0, 0, 0, (ResolutionRange)0, 1) == 1);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M3, 0, 0, (ResolutionRange)0, 1) == 2);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M6, 1, 0, (ResolutionRange)0, 1) == 3);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M6, 0, 0, (ResolutionRange)0, 1) == 6);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M7, 1, 0, (ResolutionRange)0, 1) == 3);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M9, 1, 0, (ResolutionRange)0, 1) == 6);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M9, 0, 0, (ResolutionRange)0, 1) == 0);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M11, 1, 0, (ResolutionRange)0, 1) == 6);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M11, 1, 0, (ResolutionRange)0, 0) == 7);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M13, 0, 0, (ResolutionRange)0, 1) == 0);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M6, 0, 2, (ResolutionRange)2, 1) == 4);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M7, 0, 2, (ResolutionRange)2, 1) == 6);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M10, 1, 2, (ResolutionRange)2, 1) == 6);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M10, 0, 2, (ResolutionRange)2, 1) == 0);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M11, 1, 2, (ResolutionRange)2, 1) == 7);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M13, 0, 2, (ResolutionRange)2, 1) == 0);
    CHECK(getDlfLevelDefault({0, 0}, ENC_M3, 0, 2, (ResolutionRange)0, 1) == 2);  // res 0 <= 360p
    for (int mode = (int)ENC_MR; mode <= (int)ENC_M13; ++mode) {
        for (int fd = 0; fd <= 2; ++fd) {
            for (int res = 0; res <= 6; ++res) {
                for (int notLast = 0; notLast <= 1; ++notLast) {
                    for (int base = 0; base <= 1; ++base) {
                        CHECK(getDlfLevelDefault({0, 0}, (EncMode)mode, (uint8_t)notLast, (uint8_t)fd,
                                                 (ResolutionRange)res, base)
                              == oracleDlfDefault(0, 0 /*VLOW*/, mode, notLast, fd, res, base));
                    }
                }
            }
        }
    }
}

TEST_CASE("get_dlf_level_default - the HIGH_LVL branch and the modulation walk") {
    // :1492-1498 reads pcs->coeff_lvl; :1518-1519 modulates when !is_base.
    // Boundary pins (reference-derived):
    //   M11, HIGH_LVL(3), base -> 6 (:1494);
    //   the HIGH arm IGNORES is_not_last_layer - non-base HIGH is 0 for
    //   every layer value (:1494);
    //   mode 10 IS inside the M11 branch (:1492, 10 <= 11) - the same
    //   HIGH/non-base 0;
    //   M8 default is the :1489-1491 arm: not-last 6 / last 0, mod 3 -
    //   with skip 10 (no >75/>95 window) the level is unchanged.
    CHECK(getDlfLevelDefault({0, 3}, ENC_M11, 0, 0, (ResolutionRange)0, 1) == 6);
    CHECK(getDlfLevelDefault({0, 3}, ENC_M11, 0, 0, (ResolutionRange)0, 0) == 0);
    CHECK(getDlfLevelDefault({0, 3}, ENC_M11, 1, 0, (ResolutionRange)0, 0) == 0);
    CHECK(getDlfLevelDefault({0, 3}, ENC_M10, 0, 0, (ResolutionRange)0, 0) == 0);
    CHECK(getDlfLevelDefault({10, 0}, ENC_M8, 1, 0, (ResolutionRange)0, 0) == 6);
    CHECK(getDlfLevelDefault({10, 0}, ENC_M8, 0, 0, (ResolutionRange)0, 0) == 0);
    // The mode x coeff_lvl x base x layer x fd x res x skip grid, modes
    // 10/11 (the only cells the coeff branch can reach) plus every mode at
    // fd 0/2 with the skip boundaries of the modulation windows.
    static const int skips[] = {0, 24, 25, 30, 49, 50, 75, 76, 95, 96, 100};
    for (int mode = (int)ENC_MR; mode <= (int)ENC_M13; ++mode) {
        for (int fdI = 0; fdI < 2; ++fdI) {
            int fds[2] = {0, 2};
            int fd = fds[fdI];
            for (int resI = 0; resI < 2; ++resI) {
                int ress[2] = {0, 2};
                int res = ress[resI];
                for (int notLast = 0; notLast <= 1; ++notLast) {
                    for (int base = 0; base <= 1; ++base) {
                        for (int coeff = 0; coeff <= 3; ++coeff) {
                            for (int skip : skips) {
                                CHECK(getDlfLevelDefault({(uint8_t)skip, (uint8_t)coeff}, (EncMode)mode,
                                                         (uint8_t)notLast, (uint8_t)fd, (ResolutionRange)res, base)
                                      == oracleDlfDefault((uint8_t)skip, (uint8_t)coeff, mode, notLast, fd, res, base));
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("get_dlf_level_rtc - the :1525-1546 ladder with modulation") {
    // Boundary pins (reference-derived):
    //   M7 -> 3 with mod 1 (:1528-1530); M8 -> 6 with mod 3 (:1531-1533);
    //   M10 -> 7 with mod 3 (:1534-1536); M11+ -> 0 with mod 3 (:1537-1539).
    //   Modulation at skip 10 never moves these levels: mod 1 lifts only
    //   level > 5 (:1455, M7's 3 is not) and mod 3's windows are
    //   > 95 / > 75 (:1463-1466) - 10 is in neither.
    //   The mod-3 windows do move them at skip 80/97: M8 (6) -> 7
    //   (:1466, != 7 -> +1); M10 (7) -> 0 (:1464, >= 6).
    CHECK(getDlfLevelRtc({0, 0}, ENC_M7, 1) == 3);
    CHECK(getDlfLevelRtc({0, 0}, ENC_M8, 1) == 6);
    CHECK(getDlfLevelRtc({0, 0}, ENC_M10, 1) == 7);
    CHECK(getDlfLevelRtc({0, 0}, ENC_M11, 1) == 0);
    CHECK(getDlfLevelRtc({10, 0}, ENC_M7, 0) == 3);
    CHECK(getDlfLevelRtc({10, 0}, ENC_M8, 0) == 6);
    CHECK(getDlfLevelRtc({10, 0}, ENC_M10, 0) == 7);
    CHECK(getDlfLevelRtc({80, 0}, ENC_M8, 0) == 7);
    CHECK(getDlfLevelRtc({97, 0}, ENC_M10, 0) == 0);
    for (int mode = (int)ENC_MR; mode <= (int)ENC_M13; ++mode) {
        for (int base = 0; base <= 1; ++base) {
            for (int skip : {0, 24, 25, 49, 50, 75, 76, 95, 96, 100}) {
                CHECK(getDlfLevelRtc({(uint8_t)skip, 0}, (EncMode)mode, base)
                      == oracleDlfRtc((uint8_t)skip, mode, base));
            }
        }
    }
}

TEST_CASE("get_dlf_level_allintra - every (mode, fd, res) cell") {
    // :1548-1567. Boundary pins:
    //   fd0: M3 -> 1, M5 -> 2, M6 -> 5;
    //   fd2 res 0 (<= 360p): M7 -> 2; fd2 res 2: M7 -> 0, M8+ -> 5.
    //   fd2 res 0 (<= 360p) stays in the FIRST branch (:1550) - its else
    //   arm (:1555-1556) has no M7 special case, so M7 -> 5; the "2"
    //   belongs to <= M5 only.
    CHECK(getDlfLevelAllIntra(ENC_M3, 0, (ResolutionRange)0) == 1);
    CHECK(getDlfLevelAllIntra(ENC_M5, 0, (ResolutionRange)0) == 2);
    CHECK(getDlfLevelAllIntra(ENC_M6, 0, (ResolutionRange)0) == 5);
    CHECK(getDlfLevelAllIntra(ENC_M7, 2, (ResolutionRange)0) == 5);
    CHECK(getDlfLevelAllIntra(ENC_M7, 2, (ResolutionRange)2) == 0);
    CHECK(getDlfLevelAllIntra(ENC_M8, 2, (ResolutionRange)2) == 5);
    for (int mode = (int)ENC_MR; mode <= (int)ENC_M13; ++mode) {
        for (int fd = 0; fd <= 2; ++fd) {
            for (int res = 0; res <= 6; ++res) {
                CHECK(getDlfLevelAllIntra((EncMode)mode, (uint8_t)fd, (ResolutionRange)res)
                      == oracleDlfAllIntra(mode, fd, res));
            }
        }
    }
}

// The poisoned uint8 sentinel: fields the reference case body does not
// write stay 0xAA in the port's mirror (the whole-struct no-stray-write
// check).
static const uint8_t kPoison = 0xAA;

TEST_CASE("set_gm_controls - the CONFIG=0 arm zeroes enabled/pp for every level") {
    // :2525-2528 - the !CONFIG_ENABLE_GLOBAL_MOTION arm. MEASURED
    // correction of the earlier "pinned build" claim: the pinned/generator
    // build configures NEITHER RTC_BUILD nor MINIMAL_BUILD
    // (tools/golden_gen/extract.ps1:378-380), so CONFIG_ENABLE_GLOBAL_
    // MOTION resolves to 1 there (EbConfigMacros.h:182-183) and THIS arm
    // is the RTC+MINIMAL flavor's (:61-63). Both arms are carried; the
    // two written fields are compared (plus the uint8 no-stray-write
    // proof); the poisoned bools stay unread.
    for (int level = 0; level <= 7; ++level) {
        GmPpcs pcs;
        std::memset(&pcs, kPoison, sizeof(pcs));
        setGmControls(pcs, (uint8_t)level, 0);
        CHECK(pcs.gmCtrls.enabled == 0);
        CHECK(pcs.gmCtrls.ppEnabled == false);
        CHECK(pcs.gmCtrls.identiyExit == kPoison);            // untouched
        CHECK(pcs.gmCtrls.correspondenceMethod == kPoison);   // untouched
    }
}

TEST_CASE("set_gm_controls - CONFIG=1, every level, whole-struct field checks") {
    // :2530-2620 with the CONFIG fork threaded. Case 0 writes ONLY
    // enabled/pp_enabled (:2531-2534); cases 1-4 write ALL 16 fields. The
    // mirror's untouched fields stay poisoned - the uint8 ones are checked
    // against the poison (a no-stray-write proof); the bool ones are
    // EXCLUDED (reading a bool holding 0xAA is UB, so they are compared
    // only where the reference writes them).
    // Field slots: 0 enabled, 1 identiyExit, 2 searchStartModel,
    // 3 searchEndModel, 4 skipIdentity, 5 bypassBasedOnMe,
    // 6 paramsRefinementSteps, 7 downsampleLevel, 8 corners, 9 chessRfn,
    // 10 matchSz, 11 injPsqGlb(bool), 12 ppEnabled(bool),
    // 13 refIdx0Only(bool), 14 rfnEarlyExit, 15 correspondenceMethod.
    for (int res = 0; res <= 6; ++res) {
        for (int level = 0; level <= 7; ++level) {
            // The reference's default case (assert(0)) writes nothing.
            const bool fullWrite = level >= 1 && level <= 4;
            GmPpcs pcs;
            std::memset(&pcs, kPoison, sizeof(pcs));
            pcs.inputResolution = (uint8_t)res;
            setGmControls(pcs, (uint8_t)level, 1);
            GmOracle o;
            std::memset(&o, 0, sizeof(o));
            oracleGm(o, level, res, true);
            if (level <= 4) {
                // The case bodies write enabled/pp_enabled (case 0:
                // :2531-2534; cases 1-4 the full 16).
                CHECK(pcs.gmCtrls.enabled == (uint8_t)o.enabled);
                CHECK(pcs.gmCtrls.ppEnabled == o.ppEnabled);
            } else {
                // The reference's default arm assert(0)s and writes
                // NOTHING (:2613-2615) - the mirror stays poison.
                CHECK(pcs.gmCtrls.enabled == kPoison);
            }
            if (fullWrite) {
                CHECK(pcs.gmCtrls.identiyExit == (uint8_t)o.identiyExit);
                CHECK(pcs.gmCtrls.searchStartModel == (uint8_t)o.searchStartModel);
                CHECK(pcs.gmCtrls.searchEndModel == (uint8_t)o.searchEndModel);
                CHECK(pcs.gmCtrls.skipIdentity == (uint8_t)o.skipIdentity);
                CHECK(pcs.gmCtrls.bypassBasedOnMe == (uint8_t)o.bypassBasedOnMe);
                CHECK(pcs.gmCtrls.paramsRefinementSteps == (uint8_t)o.paramsRefinementSteps);
                CHECK(pcs.gmCtrls.downsampleLevel == (uint8_t)o.downsampleLevel);
                CHECK(pcs.gmCtrls.corners == (uint8_t)o.corners);
                CHECK(pcs.gmCtrls.chessRfn == (uint8_t)o.chessRfn);
                CHECK(pcs.gmCtrls.matchSz == (uint8_t)o.matchSz);
                CHECK(pcs.gmCtrls.injPsqGlb == o.injPsqGlb);
                CHECK(pcs.gmCtrls.refIdx0Only == o.refIdx0Only);
                CHECK(pcs.gmCtrls.rfnEarlyExit == (uint8_t)o.rfnEarlyExit);
                CHECK(pcs.gmCtrls.correspondenceMethod == (uint8_t)o.correspondenceMethod);
            } else {
                // No-stray-write proof on the uint8 fields; the bool
                // fields stay unread (see above).
                CHECK(pcs.gmCtrls.identiyExit == kPoison);
                CHECK(pcs.gmCtrls.matchSz == kPoison);
            }
        }
    }
    // The correspondence fold literals (:2589-2591): res 0/2 -> MV_8x8(3),
    // res 3/4 -> MV_16x16(2), res 5/6 -> MV_32x32(1).
    CHECK(getGmCorrespondence(0) == 3);
    CHECK(getGmCorrespondence(2) == 3);
    CHECK(getGmCorrespondence(3) == 2);
    CHECK(getGmCorrespondence(4) == 2);
    CHECK(getGmCorrespondence(5) == 1);
    CHECK(getGmCorrespondence(6) == 1);
}

TEST_CASE("set_gm_controls - CONFIG=1 levels 1..4 keep match_sz odd") {
    // :2621-2623 - the assert((match_sz & 1) == 1) precondition; the case
    // bodies set 13 (level 1) and 7 (levels 2-4).
    for (int level = 1; level <= 4; ++level) {
        GmPpcs pcs;
        std::memset(&pcs, kPoison, sizeof(pcs));
        setGmControls(pcs, (uint8_t)level, 1);
        CHECK((pcs.gmCtrls.matchSz & 1) == 1);
    }
}

// ---------------------------------------------------------------------------
// EMC1 slice 2, batch A: the ME/HME setter families.
//   set_hme_search_params          :223-:287
//   set_me_search_params           :288-:352
//   svt_aom_set_me_hme_ref_prune_ctrls :353-:434
//   svt_aom_set_mv_based_sa_ctrls  :436-:459
//   svt_aom_set_me_sr_adjustment_ctrls :461-:532
//   svt_aom_set_me_8x8_var_ctrls   :534-:556
//   svt_aom_set_prehme_ctrls       :559-:614
//   tf_set_me_hme_params_oq        :619-:701
//   svt_aom_sig_deriv_me           :708-:849
//   svt_aom_sig_deriv_me_tf        :856-:897
// The MeO mirror below carries the PORT's exact field types so every
// comparison is same-type (no signed/unsigned mixes under /WX).
// ---------------------------------------------------------------------------

// DIVIDE_AND_ROUND (utility.h), independent transcription.
static uint32_t oracleDivRound(uint32_t x, uint32_t y) {
    return (x + (y >> 1)) / y;
}

// The test-side carrier mirror (the port's MeModeCfg surface).
struct MeCfgO {
    int encMode;
    bool rtcTune;
    uint32_t qp;
    bool hmeQpScaling;
    bool meQpScaling;
    bool tfMeQpScaling;
    int hierarchicalLevels;
    int scClass5;
    bool enableHmeFlag;
    int enableHmeLevel0Flag;
    int enableHmeLevel1Flag;
    int enableHmeLevel2Flag;
    int useBestUnipred;
    bool boosted;
    int inputResolution;
    int mrpSafeLimitNref;
    uint32_t mrpSafeLimitZzTh;
    int tfHmeMeLevel;
    int tfQpOpt;
    bool tfEnableHmeFlag;
    int tfEnableHmeLevel0Flag;
    int tfEnableHmeLevel1Flag;
    int tfEnableHmeLevel2Flag;
};

// The test-side mirror of the port's MeSearchParams surface (only the
// fields the batch-A spans write; bools as int, the rest at port width).
struct MeO {
    uint16_t numHmeSaW, numHmeSaH;
    uint16_t hmeL0MinW, hmeL0MinH, hmeL0MaxW, hmeL0MaxH;
    uint16_t hmeL1W, hmeL1H;
    uint16_t hmeL2W, hmeL2H;
    uint16_t hmeL0TfMinW, hmeL0TfMinH, hmeL0TfMaxW, hmeL0TfMaxH;
    uint16_t meMinW, meMinH, meMaxW, meMaxH;
    int pruneEnable;
    uint16_t pruneHmeTh, pruneMeTh;
    uint32_t zzSadTh;
    uint16_t zzSadPct;
    uint32_t phmeSadTh;
    uint16_t phmeSadPct;
    int mvSaEnable, mvSaNearestOnly;
    uint16_t mvSaSizeTh, mvSaMultiplier;
    uint8_t srEnable, srDistResize;
    uint16_t srMvLenTh, srStationaryTh, srStationaryDiv, srReduceTh, srLowSadDiv;
    uint8_t varEnable;
    uint32_t varDiv4Th, varDiv2Th, varMult2Th;
    uint8_t preEnable, preSkipLine, preL1EarlyExit;
    uint16_t preMin0W, preMin0H, preMax0W, preMax0H, preMin1W, preMin1H, preMax1W, preMax1H;
    uint8_t hmeMethod, meMethod;
    uint8_t srThMin, srThMax;
    int32_t pruneCandTh;
    uint8_t scBoost, bestUnipred;
    uint32_t earlyExitTh, staticB64Th, safeLimitZzTh, prevStageExitTh;
    int hmeFlag, hmeL0Flag, hmeL1Flag, hmeL2Flag;
};

static void oracleZero(MeO& o) {
    std::memset(&o, 0, sizeof(o));
}

// set_hme_search_params ladder + modulation, independent transcription
// (enc_mode_config.c:223-283); the reference's implicit uint16 narrowing
// is explicit here.
static void oracleHme(const MeCfgO& pcs, MeO& m, int res) {
    m.numHmeSaW = 2;                                           // :227
    m.numHmeSaH = 2;                                           // :228
    if (pcs.encMode <= 1) {                                    // :230 <= ENC_M1
        if (res < 5 /*4K*/) {
            m.hmeL0MinW = 32; m.hmeL0MinH = 32;                // :232
            m.hmeL0MaxW = 192; m.hmeL0MaxH = 192;              // :233
        } else {
            m.hmeL0MinW = 240; m.hmeL0MinH = 240;              // :235
            m.hmeL0MaxW = 480; m.hmeL0MaxH = 480;              // :236
        }
    } else if (pcs.encMode <= 5) {                             // :238 <= ENC_M5
        m.hmeL0MinW = 32; m.hmeL0MinH = 32;
        m.hmeL0MaxW = 192; m.hmeL0MaxH = 192;
    } else if (!pcs.rtcTune && pcs.encMode <= 7) {             // :241
        if (res >= 5) {
            m.hmeL0MinW = 32; m.hmeL0MinH = 32;
            m.hmeL0MaxW = 192; m.hmeL0MaxH = 192;
        } else {
            m.hmeL0MinW = 16; m.hmeL0MinH = 16;
            m.hmeL0MaxW = 192; m.hmeL0MaxH = 192;
        }
    } else if (pcs.encMode <= 8) {                             // :249
        m.hmeL0MinW = 16; m.hmeL0MinH = 16;
        m.hmeL0MaxW = 192; m.hmeL0MaxH = 192;
    } else {                                                   // :252
        if (res < 5) {
            m.hmeL0MinW = 8; m.hmeL0MinH = 8;
            m.hmeL0MaxW = 96; m.hmeL0MaxH = 96;
        } else {
            m.hmeL0MinW = 16; m.hmeL0MinH = 16;
            m.hmeL0MaxW = 96; m.hmeL0MaxH = 96;
        }
    }
    // :261-274 the qp modulation
    uint32_t w, d;
    oracleQpThScaling(pcs.hmeQpScaling, &w, &d, pcs.qp);
    m.hmeL0MinW = (uint16_t)std::max<uint32_t>(8u, oracleDivRound(m.hmeL0MinW * w, d));
    m.hmeL0MinH = (uint16_t)std::max<uint32_t>(8u, oracleDivRound(m.hmeL0MinH * w, d));
    m.hmeL0MaxW = (uint16_t)std::max<uint32_t>(96u, oracleDivRound(m.hmeL0MaxW * w, d));
    m.hmeL0MaxH = (uint16_t)std::max<uint32_t>(96u, oracleDivRound(m.hmeL0MaxH * w, d));
    if (pcs.encMode <= -1) {                                   // :276 <= ENC_MR
        m.hmeL1W = 16; m.hmeL1H = 16;
        m.hmeL2W = 16; m.hmeL2H = 16;
    } else {
        m.hmeL1W = 8; m.hmeL1H = 3;
        m.hmeL2W = 8; m.hmeL2H = 3;
    }
}

// set_me_search_params ladder + modulation, independent transcription
// (enc_mode_config.c:288-351).
static void oracleMe(const MeCfgO& pcs, MeO& m, int res) {
    if (pcs.rtcTune) {                                         // :294
        const bool useFlatIpp = pcs.hierarchicalLevels == 0;   // :295
        if (pcs.encMode <= 10 || (pcs.encMode == 11 && !useFlatIpp)) {  // :296
            if (res < 4 /*1080p*/) {
                m.meMinW = 24; m.meMinH = 16;
                m.meMaxW = 32; m.meMaxH = 16;
            } else {
                m.meMinW = 16; m.meMinH = 9;
                m.meMaxW = 16; m.meMaxH = 9;
            }
        } else {
            m.meMinW = 8; m.meMinH = 1;
            m.meMaxW = 8; m.meMaxH = 1;
        }
    } else if (pcs.encMode <= -1) {                            // :308 <= ENC_MR
        m.meMinW = 96; m.meMinH = 96;
        m.meMaxW = 256; m.meMaxH = 256;
    } else if (pcs.encMode <= 1) {                             // :311
        m.meMinW = 84; m.meMinH = 84;
        m.meMaxW = 224; m.meMaxH = 224;
    } else if (pcs.encMode <= 3) {                             // :314
        m.meMinW = 24; m.meMinH = 24;
        m.meMaxW = 88; m.meMaxH = 88;
    } else if (pcs.encMode <= 4) {                             // :317
        m.meMinW = 24; m.meMinH = 12;
        m.meMaxW = 48; m.meMaxH = 32;
    } else if (pcs.encMode <= 7) {                             // :320
        m.meMinW = 24; m.meMinH = 9;
        m.meMaxW = 24; m.meMaxH = 12;
    } else if (pcs.encMode <= 8) {                             // :323
        m.meMinW = 16; m.meMinH = 6;
        m.meMaxW = 24; m.meMaxH = 12;
    } else if (pcs.encMode <= 11) {                            // :326
        if (res < 3 /*720p*/) {
            m.meMinW = 12; m.meMinH = 4;
            m.meMaxW = 16; m.meMaxH = 9;
        } else if (res < 4 /*1080p*/) {
            m.meMinW = 12; m.meMinH = 1;
            m.meMaxW = 16; m.meMaxH = 7;
        } else if (res < 5 /*4K*/) {
            m.meMinW = 8; m.meMinH = 1;
            m.meMaxW = 8; m.meMaxH = 7;
        } else {
            m.meMinW = 8; m.meMinH = 1;
            m.meMaxW = 8; m.meMaxH = 1;
        }
    } else {                                                   // :340
        m.meMinW = 8; m.meMinH = 1;
        m.meMaxW = 8; m.meMaxH = 1;
    }
    uint32_t w, d;
    oracleQpThScaling(pcs.meQpScaling, &w, &d, pcs.qp);
    m.meMinW = (uint16_t)std::max<uint32_t>(8u, oracleDivRound(m.meMinW * w, d));
    m.meMinH = (uint16_t)std::max<uint32_t>(3u, oracleDivRound(m.meMinH * w, d));
    m.meMaxW = (uint16_t)std::max<uint32_t>(8u, oracleDivRound(m.meMaxW * w, d));
    m.meMaxH = (uint16_t)std::max<uint32_t>(3u, oracleDivRound(m.meMaxH * w, d));
}

// svt_aom_set_me_hme_ref_prune_ctrls, independent transcription
// (enc_mode_config.c:353-434). Level 7+ = the assert(0) default (writes
// nothing).
static void oraclePrune(MeO& m, int level) {
    switch (level) {
    case 0:
        m.pruneEnable = 0;
        m.pruneHmeTh = 0xFFFF; m.pruneMeTh = 0xFFFF;
        m.zzSadTh = 0; m.zzSadPct = 0; m.phmeSadTh = 0; m.phmeSadPct = 0;
        break;
    case 1:
        m.pruneEnable = 1; m.pruneHmeTh = 80; m.pruneMeTh = 0xFFFF;
        m.zzSadTh = 0; m.zzSadPct = 0; m.phmeSadTh = 0; m.phmeSadPct = 0;
        break;
    case 2:
        m.pruneEnable = 1; m.pruneHmeTh = 50; m.pruneMeTh = 0xFFFF;
        m.zzSadTh = 0; m.zzSadPct = 0; m.phmeSadTh = 0; m.phmeSadPct = 0;
        break;
    case 3:
        m.pruneEnable = 1; m.pruneHmeTh = 30; m.pruneMeTh = 0xFFFF;
        m.zzSadTh = 0; m.zzSadPct = 0; m.phmeSadTh = 0; m.phmeSadPct = 0;
        break;
    case 4:
        m.pruneEnable = 1; m.pruneHmeTh = 15; m.pruneMeTh = 60;
        m.zzSadTh = 0; m.zzSadPct = 0; m.phmeSadTh = 0; m.phmeSadPct = 0;
        break;
    case 5:
        m.pruneEnable = 1; m.pruneHmeTh = 5; m.pruneMeTh = 60;
        m.zzSadTh = 0; m.zzSadPct = 0; m.phmeSadTh = 0; m.phmeSadPct = 0;
        break;
    case 6:
        m.pruneEnable = 1; m.pruneHmeTh = 5; m.pruneMeTh = 60;
        m.zzSadTh = 20u * 64u * 64u; m.zzSadPct = 5;
        m.phmeSadTh = 10u * 64u * 64u; m.phmeSadPct = 5;
        break;
    default:
        break;
    }
}

// svt_aom_set_mv_based_sa_ctrls (:436-459).
static void oracleMvSa(MeO& m, int level) {
    if (level == 0) {
        m.mvSaEnable = 0;
    } else if (level == 1) {
        m.mvSaEnable = 1; m.mvSaNearestOnly = 0;
        m.mvSaSizeTh = 25; m.mvSaMultiplier = 2;
    } else if (level == 2) {
        m.mvSaEnable = 1; m.mvSaNearestOnly = 1;
        m.mvSaSizeTh = 25; m.mvSaMultiplier = 2;
    }
}

// svt_aom_set_me_sr_adjustment_ctrls (:461-532) with the enable_hme
// post-processing (:521-531). The oracle starts from the poisoned
// 0xAAAA sentinel for the two th fields the level-0 arm leaves unwritten
// but the post-processing divides (0xAAAA/4 = 10922, /16 = 2730).
static void oracleSr(MeO& m, int level, int hmeL2Flag, int hmeL1Flag) {
    if (level == 0) {
        m.srEnable = 0;
    } else if (level == 1) {
        m.srEnable = 1; m.srMvLenTh = 4; m.srStationaryTh = 12000; m.srStationaryDiv = 8;
        m.srReduceTh = 6000; m.srLowSadDiv = 8; m.srDistResize = 0;
    } else if (level == 2) {
        m.srEnable = 1; m.srMvLenTh = 4; m.srStationaryTh = 12000; m.srStationaryDiv = 8;
        m.srReduceTh = 6000; m.srLowSadDiv = 8; m.srDistResize = 1;
    } else if (level == 3) {
        m.srEnable = 1; m.srMvLenTh = 4; m.srStationaryTh = 12000; m.srStationaryDiv = 8;
        m.srReduceTh = 12000; m.srLowSadDiv = 8; m.srDistResize = 1;
    } else if (level == 4) {
        m.srEnable = 2; m.srMvLenTh = 16; m.srStationaryTh = 20000; m.srStationaryDiv = 8;
        m.srReduceTh = 20000; m.srLowSadDiv = 8; m.srDistResize = 1;
    } else if (level == 5) {
        m.srEnable = 2; m.srMvLenTh = 20; m.srStationaryTh = 24000; m.srStationaryDiv = 8;
        m.srReduceTh = 24000; m.srLowSadDiv = 8; m.srDistResize = 1;
    }
    if (hmeL2Flag == 0) {
        if (hmeL1Flag == 1) {
            m.srStationaryTh = (uint16_t)(m.srStationaryTh / 4);
            m.srReduceTh = (uint16_t)(m.srReduceTh / 4);
        } else {
            m.srStationaryTh = (uint16_t)(m.srStationaryTh / 16);
            m.srReduceTh = (uint16_t)(m.srReduceTh / 16);
        }
    }
}

// svt_aom_set_me_8x8_var_ctrls (:534-556).
static void oracleVar8x8(MeO& m, int level) {
    if (level == 0) {
        m.varEnable = 0;
    } else if (level == 1) {
        m.varEnable = 1; m.varDiv4Th = 0; m.varDiv2Th = 0; m.varMult2Th = 900000u;
    } else if (level == 2) {
        m.varEnable = 1; m.varDiv4Th = 80000u; m.varDiv2Th = 150000u; m.varMult2Th = 0xFFFFFFFFu;
    }
}

// svt_aom_set_prehme_ctrls (:559-614).
static void oraclePrehme(MeO& m, int level) {
    if (level == 0) {
        m.preEnable = 0;
    } else if (level == 1) {
        m.preEnable = 1;
        m.preMin0W = 8; m.preMin0H = 144; m.preMax0W = 8; m.preMax0H = 496;
        m.preMin1W = 144; m.preMin1H = 3; m.preMax1W = 496; m.preMax1H = 3;
        m.preSkipLine = 0; m.preL1EarlyExit = 0;
    } else if (level == 2) {
        m.preEnable = 1;
        m.preMin0W = 8; m.preMin0H = 100; m.preMax0W = 8; m.preMax0H = 400;
        m.preMin1W = 96; m.preMin1H = 3; m.preMax1W = 384; m.preMax1H = 3;
        m.preSkipLine = 0; m.preL1EarlyExit = 0;
    } else if (level == 3) {
        m.preEnable = 1;
        m.preMin0W = 8; m.preMin0H = 100; m.preMax0W = 8; m.preMax0H = 350;
        m.preMin1W = 32; m.preMin1H = 7; m.preMax1W = 200; m.preMax1H = 7;
        m.preSkipLine = 1; m.preL1EarlyExit = 0;
    } else if (level == 4) {
        m.preEnable = 1;
        m.preMin0W = 8; m.preMin0H = 100; m.preMax0W = 8; m.preMax0H = 350;
        m.preMin1W = 32; m.preMin1H = 7; m.preMax1W = 128; m.preMax1H = 7;
        m.preSkipLine = 1; m.preL1EarlyExit = 1;
    }
}

// tf_set_me_hme_params_oq (:619-701).
static void oracleTf(MeO& m, int level, int res, int qpOpt, bool tfMeQpScaling, uint32_t qp) {
    if (level == 0) {
        m.numHmeSaW = 2; m.numHmeSaH = 2;
        m.hmeL0TfMinW = 30; m.hmeL0TfMinH = 30; m.hmeL0TfMaxW = 60; m.hmeL0TfMaxH = 60;
        m.hmeL1W = 16; m.hmeL1H = 16; m.hmeL2W = 16; m.hmeL2H = 16;
        m.meMinW = 60; m.meMinH = 60; m.meMaxW = 120; m.meMaxH = 120;
    } else if (level == 1) {
        m.numHmeSaW = 2; m.numHmeSaH = 2;
        m.hmeL0TfMinW = 16; m.hmeL0TfMinH = 16; m.hmeL0TfMaxW = 32; m.hmeL0TfMaxH = 32;
        m.hmeL1W = 16; m.hmeL1H = 16; m.hmeL2W = 16; m.hmeL2H = 16;
        m.meMinW = 16; m.meMinH = 16; m.meMaxW = 32; m.meMaxH = 32;
    } else if (level == 2) {
        m.numHmeSaW = 2; m.numHmeSaH = 2;
        if (res <= 1 /*360p*/) {
            m.hmeL0TfMinW = 8; m.hmeL0TfMinH = 8; m.hmeL0TfMaxW = 8; m.hmeL0TfMaxH = 8;
            m.hmeL1W = 8; m.hmeL1H = 8;
        } else if (res <= 2 /*480p*/) {
            m.hmeL0TfMinW = 8; m.hmeL0TfMinH = 8; m.hmeL0TfMaxW = 16; m.hmeL0TfMaxH = 16;
            m.hmeL1W = 8; m.hmeL1H = 8;
        } else {
            m.hmeL0TfMinW = 16; m.hmeL0TfMinH = 16; m.hmeL0TfMaxW = 32; m.hmeL0TfMaxH = 32;
            m.hmeL1W = 16; m.hmeL1H = 16;
        }
        m.hmeL2W = 16; m.hmeL2H = 16;
        m.meMinW = 8; m.meMinH = 8; m.meMaxW = 8; m.meMaxH = 8;
    } else if (level == 3) {
        m.numHmeSaW = 2; m.numHmeSaH = 2;
        m.hmeL0TfMinW = 8; m.hmeL0TfMinH = 8; m.hmeL0TfMaxW = 8; m.hmeL0TfMaxH = 8;
        m.hmeL1W = 8; m.hmeL1H = 8; m.hmeL2W = 8; m.hmeL2H = 8;
        m.meMinW = 8; m.meMinH = 8; m.meMaxW = 8; m.meMaxH = 8;
    } else if (level == 4) {
        m.numHmeSaW = 2; m.numHmeSaH = 2;
        m.hmeL0TfMinW = 4; m.hmeL0TfMinH = 4; m.hmeL0TfMaxW = 4; m.hmeL0TfMaxH = 4;
        m.hmeL1W = 8; m.hmeL1H = 8; m.hmeL2W = 8; m.hmeL2H = 8;
        m.meMinW = 8; m.meMinH = 8; m.meMaxW = 8; m.meMaxH = 8;
    }
    if (qpOpt) {
        uint32_t w, d;
        oracleQpThScaling(tfMeQpScaling, &w, &d, qp);
        m.meMinW = (uint16_t)std::max<uint32_t>(8u, oracleDivRound(m.meMinW * w, d));
        m.meMinH = (uint16_t)std::max<uint32_t>(8u, oracleDivRound(m.meMinH * w, d));
        m.meMaxW = (uint16_t)std::max<uint32_t>(8u, oracleDivRound(m.meMaxW * w, d));
        m.meMaxH = (uint16_t)std::max<uint32_t>(8u, oracleDivRound(m.meMaxH * w, d));
    }
}

TEST_CASE("set_hme_search_params - the ladder over every (mode, resolution, rtc) cell, scaling off") {
    // :223-283 with the th-scaling flag off (the :261-274 modulation then
    // divides by 1/1 - the MAX clamps still apply but every ladder value
    // is >= its clamp, so the values pass through unchanged).
    for (int mode = (int)ENC_MR; mode <= (int)ENC_M13; ++mode) {
        for (int res = 0; res <= 6; ++res) {
            for (int rtc = 0; rtc <= 1; ++rtc) {
                MeModeCfg pcs{};
                pcs.encMode = (EncMode)mode;
                pcs.rtcTune = rtc != 0;
                pcs.hmeQpBasedThScaling = false;
                pcs.qp = 37;
                MeSearchParams m;
                std::memset(&m, kPoison, sizeof(m));
                setHmeSearchParams(pcs, pcs, m, (ResolutionRange)res);
                MeO o;
                oracleZero(o);
                MeCfgO oc{};
                oc.encMode = mode;
                oc.rtcTune = rtc != 0;
                oc.hmeQpScaling = false;
                oc.qp = 37;
                oracleHme(oc, o, res);
                CHECK(m.numHmeSaW == o.numHmeSaW);          // :227
                CHECK(m.numHmeSaH == o.numHmeSaH);          // :228
                CHECK(m.hmeL0Sa.saMin.width == o.hmeL0MinW);
                CHECK(m.hmeL0Sa.saMin.height == o.hmeL0MinH);
                CHECK(m.hmeL0Sa.saMax.width == o.hmeL0MaxW);
                CHECK(m.hmeL0Sa.saMax.height == o.hmeL0MaxH);
                CHECK(m.hmeL1Sa.width == o.hmeL1W);         // :276-281
                CHECK(m.hmeL1Sa.height == o.hmeL1H);
                CHECK(m.hmeL2Sa.width == o.hmeL2W);
                CHECK(m.hmeL2Sa.height == o.hmeL2H);
                // No-stray-write proof: the setter touches only the
                // hme_l0/l1/l2 + num_hme_sa fields.
                CHECK(m.meSa.saMin.width == 0xAAAA);
                CHECK(m.hmeL0SaDefaultTf.saMin.width == 0xAAAA);
            }
        }
    }
    // The ladder boundary pins (hand-derived, scaling off):
    //   M1, res 4K(5): 240/480 - the only 240/480 cell (:235-236);
    //   M7 rtc falls to the <= M8 arm (:249-251); M9 res 0: 8/96;
    //   MR(-1): l1/l2 = 16/16 (:276-278), M0: 8/3.
    MeModeCfg pcs{};
    pcs.hmeQpBasedThScaling = false;
    pcs.qp = 37;
    MeSearchParams m;
    std::memset(&m, kPoison, sizeof(m));
    pcs.encMode = ENC_M1;
    setHmeSearchParams(pcs, pcs, m, (ResolutionRange)5);
    CHECK(m.hmeL0Sa.saMin.width == 240);
    CHECK(m.hmeL0Sa.saMax.width == 480);
    std::memset(&m, kPoison, sizeof(m));
    pcs.encMode = ENC_M7;
    pcs.rtcTune = true;
    setHmeSearchParams(pcs, pcs, m, (ResolutionRange)0);
    CHECK(m.hmeL0Sa.saMin.width == 16);
    CHECK(m.hmeL0Sa.saMax.width == 192);
    std::memset(&m, kPoison, sizeof(m));
    pcs.rtcTune = false;
    pcs.encMode = ENC_M9;
    setHmeSearchParams(pcs, pcs, m, (ResolutionRange)0);
    CHECK(m.hmeL0Sa.saMin.width == 8);
    CHECK(m.hmeL0Sa.saMax.width == 96);
    std::memset(&m, kPoison, sizeof(m));
    pcs.encMode = ENC_MR;
    setHmeSearchParams(pcs, pcs, m, (ResolutionRange)0);
    CHECK(m.hmeL1Sa.width == 16);
    CHECK(m.hmeL1Sa.height == 16);
    std::memset(&m, kPoison, sizeof(m));
    pcs.encMode = ENC_M0;
    setHmeSearchParams(pcs, pcs, m, (ResolutionRange)0);
    CHECK(m.hmeL1Sa.width == 8);
    CHECK(m.hmeL1Sa.height == 3);
}

TEST_CASE("set_hme_search_params - the qp modulation over every qp and both scaling flags") {
    // :261-274. Representative ladder cells x every qp x both flags.
    static const int hmeModes[] = {-1, 0, 1, 5, 6, 7, 8, 9, 13};
    static const int hmeRess[] = {0, 4, 5, 6};
    for (int mode : hmeModes) {
        for (int res : hmeRess) {
            for (int flag = 0; flag <= 1; ++flag) {
                for (uint32_t qp = 0; qp <= 63; ++qp) {
                    MeModeCfg pcs{};
                    pcs.encMode = (EncMode)mode;
                    pcs.rtcTune = false;
                    pcs.hmeQpBasedThScaling = flag != 0;
                    pcs.qp = qp;
                    MeSearchParams m;
                    std::memset(&m, kPoison, sizeof(m));
                    setHmeSearchParams(pcs, pcs, m, (ResolutionRange)res);
                    MeO o;
                    oracleZero(o);
                    MeCfgO oc{};
                    oc.encMode = mode;
                    oc.hmeQpScaling = flag != 0;
                    oc.qp = qp;
                    oracleHme(oc, o, res);
                    CHECK(m.hmeL0Sa.saMin.width == o.hmeL0MinW);
                    CHECK(m.hmeL0Sa.saMin.height == o.hmeL0MinH);
                    CHECK(m.hmeL0Sa.saMax.width == o.hmeL0MaxW);
                    CHECK(m.hmeL0Sa.saMax.height == o.hmeL0MaxH);
                }
            }
        }
    }
}

TEST_CASE("set_me_search_params - the ladder over every (mode, resolution, rtc, flat-ipp) cell, scaling off") {
    // :288-351 with the th-scaling flag off.
    for (int mode = (int)ENC_MR; mode <= (int)ENC_M13; ++mode) {
        for (int res = 0; res <= 6; ++res) {
            for (int rtc = 0; rtc <= 1; ++rtc) {
                for (int hl = 0; hl <= 1; ++hl) {
                    MeModeCfg pcs{};
                    pcs.encMode = (EncMode)mode;
                    pcs.rtcTune = rtc != 0;
                    pcs.hierarchicalLevels = (uint8_t)hl;
                    pcs.meQpBasedThScaling = false;
                    pcs.qp = 37;
                    MeSearchParams m;
                    std::memset(&m, kPoison, sizeof(m));
                    setMeSearchParams(pcs, pcs, m, (ResolutionRange)res);
                    MeO o;
                    oracleZero(o);
                    MeCfgO oc{};
                    oc.encMode = mode;
                    oc.rtcTune = rtc != 0;
                    oc.hierarchicalLevels = hl;
                    oc.meQpScaling = false;
                    oc.qp = 37;
                    oracleMe(oc, o, res);
                    CHECK(m.meSa.saMin.width == o.meMinW);
                    CHECK(m.meSa.saMin.height == o.meMinH);
                    CHECK(m.meSa.saMax.width == o.meMaxW);
                    CHECK(m.meSa.saMax.height == o.meMaxH);
                    // No-stray-write proof: only me_sa is touched.
                    CHECK(m.hmeL0Sa.saMin.width == 0xAAAA);
                    CHECK(m.numHmeSaW == 0xAAAA);
                }
            }
        }
    }
    // The boundary pins (hand-derived, scaling off):
    //   rtc M11 flat (hl 0): the :304-307 else sets {8,1}, and the
    //   :347-350 modulation floor MAX(3, DAR(1*1, 1)) raises the height
    //   to 3 EVEN with scaling off; MR: 96/256; M11 non-rtc res 2:
    //   12x4/16x9; M13 res 5: 8x3/8x1 (same floor).
    MeModeCfg pcs{};
    pcs.meQpBasedThScaling = false;
    pcs.qp = 37;
    MeSearchParams m;
    std::memset(&m, kPoison, sizeof(m));
    pcs.encMode = ENC_M11;
    pcs.rtcTune = true;
    pcs.hierarchicalLevels = 0;
    setMeSearchParams(pcs, pcs, m, (ResolutionRange)0);
    CHECK(m.meSa.saMin.width == 8);
    CHECK(m.meSa.saMin.height == 3);
    std::memset(&m, kPoison, sizeof(m));
    pcs.encMode = ENC_MR;
    pcs.rtcTune = false;
    setMeSearchParams(pcs, pcs, m, (ResolutionRange)0);
    CHECK(m.meSa.saMin.width == 96);
    CHECK(m.meSa.saMax.width == 256);
    std::memset(&m, kPoison, sizeof(m));
    pcs.encMode = ENC_M11;
    setMeSearchParams(pcs, pcs, m, (ResolutionRange)2);
    CHECK(m.meSa.saMin.width == 12);
    CHECK(m.meSa.saMin.height == 4);
    CHECK(m.meSa.saMax.width == 16);
    CHECK(m.meSa.saMax.height == 9);
    std::memset(&m, kPoison, sizeof(m));
    pcs.encMode = ENC_M13;
    setMeSearchParams(pcs, pcs, m, (ResolutionRange)5);
    CHECK(m.meSa.saMin.width == 8);
    CHECK(m.meSa.saMin.height == 3);
    CHECK(m.meSa.saMax.height == 3);
}

TEST_CASE("set_me_search_params - the qp modulation over every qp and both scaling flags") {
    // :344-350.
    static const int meModes[] = {-1, 0, 4, 8, 10, 11, 12, 13};
    static const int meRess[] = {0, 3, 4, 5};
    for (int mode : meModes) {
        for (int res : meRess) {
            for (int rtc = 0; rtc <= 1; ++rtc) {
                for (int flag = 0; flag <= 1; ++flag) {
                    for (uint32_t qp = 0; qp <= 63; ++qp) {
                        MeModeCfg pcs{};
                        pcs.encMode = (EncMode)mode;
                        pcs.rtcTune = rtc != 0;
                        pcs.hierarchicalLevels = 0;
                        pcs.meQpBasedThScaling = flag != 0;
                        pcs.qp = qp;
                        MeSearchParams m;
                        std::memset(&m, kPoison, sizeof(m));
                        setMeSearchParams(pcs, pcs, m, (ResolutionRange)res);
                        MeO o;
                        oracleZero(o);
                        MeCfgO oc{};
                        oc.encMode = mode;
                        oc.rtcTune = rtc != 0;
                        oc.hierarchicalLevels = 0;
                        oc.meQpScaling = flag != 0;
                        oc.qp = qp;
                        oracleMe(oc, o, res);
                        CHECK(m.meSa.saMin.width == o.meMinW);
                        CHECK(m.meSa.saMin.height == o.meMinH);
                        CHECK(m.meSa.saMax.width == o.meMaxW);
                        CHECK(m.meSa.saMax.height == o.meMaxH);
                    }
                }
            }
        }
    }
}

TEST_CASE("set_me_hme_ref_prune_ctrls - every level, whole-struct") {
    // :353-434. Levels 0..6 write all seven fields; 7+ is the assert(0)
    // default (nothing written).
    for (int level = 0; level <= 7; ++level) {
        MeSearchParams m;
        std::memset(&m, kPoison, sizeof(m));
        setMeHmeRefPruneCtrls(m, (uint8_t)level);
        MeO o;
        oracleZero(o);
        oraclePrune(o, level);
        if (level <= 6) {
            CHECK(m.meHmePruneCtrls.enableMeHmeRefPruning == (o.pruneEnable != 0));
            CHECK(m.meHmePruneCtrls.pruneRefIfHmeSadDevBiggerThanTh == o.pruneHmeTh);
            CHECK(m.meHmePruneCtrls.pruneRefIfMeSadDevBiggerThanTh == o.pruneMeTh);
            CHECK(m.meHmePruneCtrls.zzSadTh == o.zzSadTh);
            CHECK(m.meHmePruneCtrls.zzSadPct == o.zzSadPct);
            CHECK(m.meHmePruneCtrls.phmeSadTh == o.phmeSadTh);
            CHECK(m.meHmePruneCtrls.phmeSadPct == o.phmeSadPct);
        } else {
            // The default arm writes nothing (:430-432).
            CHECK(m.meHmePruneCtrls.pruneRefIfHmeSadDevBiggerThanTh == 0xAAAA);
            CHECK(m.meHmePruneCtrls.zzSadTh == 0xAAAAAAAA);
        }
    }
    // Literal pins: level 0 sets both th to (uint16_t)~0 = 65535
    // (:359-360); level 4 is the first me_th 60 cell (:402); level 6
    // enables the zz/phme sad gates (20/10 * 64 * 64, :425-427).
    MeSearchParams m;
    std::memset(&m, kPoison, sizeof(m));
    setMeHmeRefPruneCtrls(m, 0);
    CHECK(m.meHmePruneCtrls.pruneRefIfHmeSadDevBiggerThanTh == 65535);
    std::memset(&m, kPoison, sizeof(m));
    setMeHmeRefPruneCtrls(m, 4);
    CHECK(m.meHmePruneCtrls.pruneRefIfHmeSadDevBiggerThanTh == 15);
    CHECK(m.meHmePruneCtrls.pruneRefIfMeSadDevBiggerThanTh == 60);
    std::memset(&m, kPoison, sizeof(m));
    setMeHmeRefPruneCtrls(m, 6);
    CHECK(m.meHmePruneCtrls.zzSadTh == 81920);
    CHECK(m.meHmePruneCtrls.phmeSadTh == 40960);
}

TEST_CASE("set_mv_based_sa_ctrls - every level") {
    // :436-459. Case 0 writes only enabled; 1/2 write all four; 3+ the
    // assert(0) default.
    for (int level = 0; level <= 3; ++level) {
        MeSearchParams m;
        std::memset(&m, kPoison, sizeof(m));
        setMvBasedSaCtrls(m, (uint8_t)level);
        MeO o;
        oracleZero(o);
        oracleMvSa(o, level);
        if (level <= 2) {
            CHECK(m.mvBasedSaAdj.enabled == (o.mvSaEnable != 0));
            if (level >= 1) {
                CHECK(m.mvBasedSaAdj.nearestRefOnly == (o.mvSaNearestOnly != 0));
                CHECK(m.mvBasedSaAdj.mvSizeTh == o.mvSaSizeTh);
                CHECK(m.mvBasedSaAdj.saMultiplier == o.mvSaMultiplier);
            }
        } else {
            // The default arm writes nothing (:455-457).
            CHECK(m.mvBasedSaAdj.mvSizeTh == 0xAAAA);
            CHECK(m.mvBasedSaAdj.saMultiplier == 0xAAAA);
        }
    }
}

TEST_CASE("set_me_sr_adjustment_ctrls - every level x hme-flag pair") {
    // :461-532. The enable_hme post-processing (:521-531) reads the
    // carrier's hme-level flags: level2 off + level1 on -> /4; level2 off
    // + level1 off -> /16. The poisoned 0xAAAA th fields make the
    // level-0 division deterministic.
    for (int level = 0; level <= 6; ++level) {
        for (int l2 = 0; l2 <= 1; ++l2) {
            for (int l1 = 0; l1 <= 1; ++l1) {
                MeSearchParams m;
                std::memset(&m, kPoison, sizeof(m));
                m.enableHmeLevel2Flag = l2 != 0;
                m.enableHmeLevel1Flag = l1 != 0;
                setMeSrAdjustmentCtrls(m, (uint8_t)level);
                MeO o;
                oracleZero(o);
                o.srStationaryTh = 0xAAAA;
                o.srReduceTh = 0xAAAA;
                oracleSr(o, level, l2, l1);
                if (level <= 5) {
                    CHECK(m.meSrAdjustmentCtrls.enableMeSrAdjustment == o.srEnable);
                    if (level >= 1) {
                        CHECK(m.meSrAdjustmentCtrls.reduceMeSrBasedOnMvLengthTh == o.srMvLenTh);
                        CHECK(m.meSrAdjustmentCtrls.stationaryMeSrDivisor == o.srStationaryDiv);
                        CHECK(m.meSrAdjustmentCtrls.meSrDivisorForLowHmeSad == o.srLowSadDiv);
                        CHECK(m.meSrAdjustmentCtrls.distanceBasedHmeResizing == o.srDistResize);
                    }
                }
                // The post-processing always runs (:521-531).
                CHECK(m.meSrAdjustmentCtrls.stationaryHmeSadAbsTh == o.srStationaryTh);
                CHECK(m.meSrAdjustmentCtrls.reduceMeSrBasedOnHmeSadAbsTh == o.srReduceTh);
            }
        }
    }
    // Literal pins: level 3 keeps th 12000 (:491) and both flags off
    // divide by 16 (:527-529); level 1 with level2 off + level1 on
    // divides by 4 (:523-525); level 2 flag on keeps the written values.
    // The :521-531 gate reads the carrier's flag fields - set them
    // explicitly (a poisoned bool reads true and would skip the gate).
    MeSearchParams m;
    std::memset(&m, kPoison, sizeof(m));
    m.enableHmeLevel2Flag = false;
    m.enableHmeLevel1Flag = false;
    setMeSrAdjustmentCtrls(m, 3);
    CHECK(m.meSrAdjustmentCtrls.stationaryHmeSadAbsTh == 12000 / 16);
    CHECK(m.meSrAdjustmentCtrls.reduceMeSrBasedOnHmeSadAbsTh == 12000 / 16);
    std::memset(&m, kPoison, sizeof(m));
    m.enableHmeLevel2Flag = false;
    m.enableHmeLevel1Flag = false;
    setMeSrAdjustmentCtrls(m, 1);
    CHECK(m.meSrAdjustmentCtrls.stationaryHmeSadAbsTh == 12000 / 16);
    CHECK(m.meSrAdjustmentCtrls.reduceMeSrBasedOnHmeSadAbsTh == 6000 / 16);
    std::memset(&m, kPoison, sizeof(m));
    m.enableHmeLevel2Flag = false;
    m.enableHmeLevel1Flag = true;
    setMeSrAdjustmentCtrls(m, 1);
    CHECK(m.meSrAdjustmentCtrls.stationaryHmeSadAbsTh == 12000 / 4);
    CHECK(m.meSrAdjustmentCtrls.reduceMeSrBasedOnHmeSadAbsTh == 6000 / 4);
    std::memset(&m, kPoison, sizeof(m));
    m.enableHmeLevel2Flag = true;
    setMeSrAdjustmentCtrls(m, 1);
    CHECK(m.meSrAdjustmentCtrls.stationaryHmeSadAbsTh == 12000);
    CHECK(m.meSrAdjustmentCtrls.reduceMeSrBasedOnHmeSadAbsTh == 6000);
}

TEST_CASE("set_me_8x8_var_ctrls - every level") {
    // :534-556. Case 0 writes only enabled; 1/2 write all four; 3+ the
    // assert(0) default (falls through, :553-555).
    for (int level = 0; level <= 3; ++level) {
        MeSearchParams m;
        std::memset(&m, kPoison, sizeof(m));
        setMe8x8VarCtrls(m, (uint8_t)level);
        MeO o;
        oracleZero(o);
        oracleVar8x8(o, level);
        if (level <= 2) {
            CHECK(m.me8x8VarCtrls.enabled == o.varEnable);
            if (level >= 1) {
                CHECK(m.me8x8VarCtrls.meSrDiv4Th == o.varDiv4Th);
                CHECK(m.me8x8VarCtrls.meSrDiv2Th == o.varDiv2Th);
                CHECK(m.me8x8VarCtrls.meSrMult2Th == o.varMult2Th);
            }
        } else {
            // The default arm writes nothing (:553-555).
            CHECK(m.me8x8VarCtrls.meSrDiv4Th == 0xAAAAAAAA);
        }
    }
}

TEST_CASE("set_prehme_ctrls - every level, whole-struct") {
    // :559-614. Case 0 writes only enable; 1-4 write all; 5+ default.
    for (int level = 0; level <= 5; ++level) {
        MeSearchParams m;
        std::memset(&m, kPoison, sizeof(m));
        setPrehmeCtrls(m, (uint8_t)level);
        MeO o;
        oracleZero(o);
        oraclePrehme(o, level);
        if (level <= 4) {
            CHECK(m.prehmeCtrl.enable == o.preEnable);
            if (level >= 1) {
                CHECK(m.prehmeCtrl.prehmeSaCfg[0].saMin.width == o.preMin0W);
                CHECK(m.prehmeCtrl.prehmeSaCfg[0].saMin.height == o.preMin0H);
                CHECK(m.prehmeCtrl.prehmeSaCfg[0].saMax.width == o.preMax0W);
                CHECK(m.prehmeCtrl.prehmeSaCfg[0].saMax.height == o.preMax0H);
                CHECK(m.prehmeCtrl.prehmeSaCfg[1].saMin.width == o.preMin1W);
                CHECK(m.prehmeCtrl.prehmeSaCfg[1].saMin.height == o.preMin1H);
                CHECK(m.prehmeCtrl.prehmeSaCfg[1].saMax.width == o.preMax1W);
                CHECK(m.prehmeCtrl.prehmeSaCfg[1].saMax.height == o.preMax1H);
                CHECK(m.prehmeCtrl.skipSearchLine == o.preSkipLine);
                CHECK(m.prehmeCtrl.l1EarlyExit == o.preL1EarlyExit);
            }
        } else {
            // The default arm writes nothing (:610-612).
            CHECK(m.prehmeCtrl.prehmeSaCfg[0].saMin.width == 0xAAAA);
            CHECK(m.prehmeCtrl.skipSearchLine == 0xAA);
        }
    }
    // Literal pins: level 1 vertical {8,144}/{8,496}, horizontal
    // {144,3}/{496,3} (:569-573); level 4 adds l1_early_exit (:608).
    MeSearchParams m;
    std::memset(&m, kPoison, sizeof(m));
    setPrehmeCtrls(m, 1);
    CHECK(m.prehmeCtrl.prehmeSaCfg[0].saMin.height == 144);
    CHECK(m.prehmeCtrl.prehmeSaCfg[0].saMax.height == 496);
    CHECK(m.prehmeCtrl.prehmeSaCfg[1].saMin.width == 144);
    CHECK(m.prehmeCtrl.prehmeSaCfg[1].saMax.width == 496);
    CHECK(m.prehmeCtrl.l1EarlyExit == 0);
    std::memset(&m, kPoison, sizeof(m));
    setPrehmeCtrls(m, 4);
    CHECK(m.prehmeCtrl.prehmeSaCfg[1].saMax.width == 128);
    CHECK(m.prehmeCtrl.skipSearchLine == 1);
    CHECK(m.prehmeCtrl.l1EarlyExit == 1);
}

TEST_CASE("tf_set_me_hme_params_oq - every level x resolution x qp_opt") {
    // :619-701. Levels 0..4 write the full surface; 5+ default. Level 2
    // branches on scs->input_resolution (:646-658); qp_opt gates the
    // modulation (:690-700).
    for (int level = 0; level <= 5; ++level) {
        for (int res = 0; res <= 6; ++res) {
            for (int qpOpt = 0; qpOpt <= 1; ++qpOpt) {
                for (int flag = 0; flag <= 1; ++flag) {
                    MeModeCfg pcs{};
                    pcs.inputResolution = (ResolutionRange)res;
                    pcs.tfHmeMeLevel = (uint8_t)level;
                    pcs.tfQpOpt = (uint8_t)qpOpt;
                    pcs.tfMeQpBasedThScaling = flag != 0;
                    pcs.qp = 37;
                    MeSearchParams m;
                    std::memset(&m, kPoison, sizeof(m));
                    tfSetMeHmeParamsOq(m, pcs);
                    MeO o;
                    oracleZero(o);
                    oracleTf(o, level, res, qpOpt, flag != 0, 37);
                    if (level <= 4) {
                        CHECK(m.numHmeSaW == o.numHmeSaW);
                        CHECK(m.numHmeSaH == o.numHmeSaH);
                        CHECK(m.hmeL0SaDefaultTf.saMin.width == o.hmeL0TfMinW);
                        CHECK(m.hmeL0SaDefaultTf.saMin.height == o.hmeL0TfMinH);
                        CHECK(m.hmeL0SaDefaultTf.saMax.width == o.hmeL0TfMaxW);
                        CHECK(m.hmeL0SaDefaultTf.saMax.height == o.hmeL0TfMaxH);
                        CHECK(m.hmeL1Sa.width == o.hmeL1W);
                        CHECK(m.hmeL1Sa.height == o.hmeL1H);
                        CHECK(m.hmeL2Sa.width == o.hmeL2W);
                        CHECK(m.hmeL2Sa.height == o.hmeL2H);
                        CHECK(m.meSa.saMin.width == o.meMinW);
                        CHECK(m.meSa.saMin.height == o.meMinH);
                        CHECK(m.meSa.saMax.width == o.meMaxW);
                        CHECK(m.meSa.saMax.height == o.meMaxH);
                        // No-stray-write proof: hme_l0_sa is untouched.
                        CHECK(m.hmeL0Sa.saMin.width == 0xAAAA);
                    } else {
                        CHECK(m.numHmeSaW == 0xAAAA);
                    }
                }
            }
        }
    }
    // The modulation sweep at level 0: every qp, both flags.
    for (int flag = 0; flag <= 1; ++flag) {
        for (uint32_t qp = 0; qp <= 63; ++qp) {
            MeModeCfg pcs{};
            pcs.inputResolution = (ResolutionRange)0;
            pcs.tfHmeMeLevel = 0;
            pcs.tfQpOpt = 1;
            pcs.tfMeQpBasedThScaling = flag != 0;
            pcs.qp = qp;
            MeSearchParams m;
            std::memset(&m, kPoison, sizeof(m));
            tfSetMeHmeParamsOq(m, pcs);
            MeO o;
            oracleZero(o);
            oracleTf(o, 0, 0, 1, flag != 0, qp);
            CHECK(m.meSa.saMin.width == o.meMinW);
            CHECK(m.meSa.saMin.height == o.meMinH);
            CHECK(m.meSa.saMax.width == o.meMaxW);
            CHECK(m.meSa.saMax.height == o.meMaxH);
        }
    }
}

// svt_aom_sig_deriv_me, independent transcription (:708-849): the
// composition over the sub-oracles.
static void oracleSigDerivMe(const MeCfgO& pcs, MeO& m, int res) {
    oracleMe(pcs, m, res);    // :716 set_me_search_params (scaling off)
    oracleHme(pcs, m, res);   // :719 set_hme_search_params
    m.hmeFlag = pcs.enableHmeFlag;            // :722
    m.hmeL0Flag = pcs.enableHmeLevel0Flag;    // :723
    m.hmeL1Flag = pcs.enableHmeLevel1Flag;    // :724
    m.hmeL2Flag = pcs.enableHmeLevel2Flag;    // :725
    m.hmeMethod = 0;          // :727 SUB_SAD_SEARCH
    m.meMethod = 0;           // :728
    m.srThMin = 0;            // :730
    m.srThMax = 0;            // :731
    // :733-753 the pre-hme ladder
    int prehmeLevel;
    if (pcs.rtcTune) {
        if (pcs.encMode <= 8) {
            prehmeLevel = 2;
        } else if (pcs.encMode <= 10 || (pcs.encMode == 11 && !(pcs.rtcTune && pcs.hierarchicalLevels == 0))) {
            prehmeLevel = 4;
        } else {
            prehmeLevel = 0;
        }
    } else {
        if (pcs.encMode <= 7) {
            prehmeLevel = 2;
        } else if (pcs.encMode <= 11) {
            prehmeLevel = 4;
        } else {
            prehmeLevel = 0;
        }
    }
    if (pcs.enableHmeLevel1Flag == 0) prehmeLevel = 0;   // :751-753
    oraclePrehme(m, prehmeLevel);
    // :759-783 the ref-prune ladder
    int pruneLevel;
    const bool useFlatIpp = pcs.rtcTune && pcs.hierarchicalLevels == 0;  // :714
    if (pcs.rtcTune) {
        if (useFlatIpp) {
            pruneLevel = 6;
        } else if (pcs.encMode <= 7) {
            pruneLevel = pcs.boosted ? 1 : 6;
        } else {
            pruneLevel = 6;
        }
    } else {
        if (pcs.encMode <= -1) {
            pruneLevel = 0;
        } else if (pcs.encMode <= 0) {
            pruneLevel = pcs.boosted ? 1 : 4;
        } else if (pcs.encMode <= 4) {
            pruneLevel = pcs.boosted ? 1 : 5;
        } else if (pcs.encMode <= 8) {
            pruneLevel = pcs.boosted ? 1 : 6;
        } else {
            pruneLevel = 6;
        }
    }
    oraclePrune(m, pruneLevel);
    // :788-794 the me-sr level + post-processing. The level-0 arm (modes
    // <= M0) writes only enable, so the :521-531 division applies to the
    // carrier's prior values - the poisoned 0xAAAA sentinel stands in
    // (0xAAAA/16 = 2730), the same convention the setter test pins.
    m.srStationaryTh = 0xAAAA;
    m.srReduceTh = 0xAAAA;
    oracleSr(m, pcs.encMode <= 0 ? 0 : 3, pcs.enableHmeLevel2Flag, pcs.enableHmeLevel1Flag);
    // :796-802 the mv-sa level
    oracleMvSa(m, pcs.encMode <= 0 ? 2 : 0);
    // :804-805
    oracleVar8x8(m, 2);
    // :806-810
    m.pruneCandTh = pcs.encMode <= 1 ? 0 : 65;
    // :813-823
    if (pcs.scClass5) {
        m.scBoost = (uint8_t)(pcs.encMode <= 2 ? 1 : pcs.encMode <= 5 ? 2 : 0);
    } else {
        m.scBoost = 0;
    }
    // :825
    m.bestUnipred = (uint8_t)pcs.useBestUnipred;
    // :826-836
    if (pcs.rtcTune) {
        m.earlyExitTh = 64u * 64u * 8u;
    } else {
        m.earlyExitTh = pcs.encMode <= 2 ? 0u : pcs.encMode <= 9 ? 64u * 64u * 8u : 64u * 64u * 12u;
    }
    // :840-844
    m.staticB64Th = pcs.rtcTune ? 64u * 64u : 0u;
    // :846
    m.safeLimitZzTh = pcs.mrpSafeLimitNref == 1 ? pcs.mrpSafeLimitZzTh : 0u;
    // :848
    m.prevStageExitTh = 0;
}

TEST_CASE("sig_deriv_me - the composition over every (mode, res, rtc, boosted, flat-ipp, sc5, hme-flag) cell") {
    // :708-849. Scaling flags off (the pure ladder); the flag-driven
    // modulation paths are pinned by the setter cases above.
    static const int sdRess[] = {0, 3, 4, 5};
    for (int mode = (int)ENC_MR; mode <= (int)ENC_M13; ++mode) {
        for (int res : sdRess) {
            for (int rtc = 0; rtc <= 1; ++rtc) {
                for (int boosted = 0; boosted <= 1; ++boosted) {
                    for (int hl = 0; hl <= 1; ++hl) {
                        for (int sc5 = 0; sc5 <= 1; ++sc5) {
                            for (int l1 = 0; l1 <= 1; ++l1) {
                                for (int l2 = 0; l2 <= 1; ++l2) {
                                    MeModeCfg scs{};
                                    scs.rtcTune = rtc != 0;
                                    scs.qp = 37;
                                    scs.inputResolution = (ResolutionRange)res;
                                    scs.mrpSafeLimitNref = (uint8_t)sc5;  // representative nref value
                                    scs.mrpSafeLimitZzTh = 12345;
                                    MeModeCfg pcs{};
                                    pcs.encMode = (EncMode)mode;
                                    pcs.rtcTune = rtc != 0;
                                    pcs.hierarchicalLevels = (uint8_t)hl;
                                    pcs.scClass5 = sc5 != 0;
                                    pcs.enableHmeFlag = true;
                                    pcs.enableHmeLevel0Flag = true;
                                    pcs.enableHmeLevel1Flag = l1 != 0;
                                    pcs.enableHmeLevel2Flag = l2 != 0;
                                    pcs.useBestMeUnipredCandOnly = (uint8_t)boosted;
                                    pcs.frameIsBoosted = boosted != 0;
                                    MeSearchParams m;
                                    std::memset(&m, kPoison, sizeof(m));
                                    sigDerivMe(scs, pcs, m);
                                    MeO o;
                                    oracleZero(o);
                                    MeCfgO oc{};
                                    oc.encMode = mode;
                                    oc.rtcTune = rtc != 0;
                                    oc.hierarchicalLevels = hl;
                                    oc.scClass5 = sc5;
                                    oc.enableHmeFlag = true;
                                    oc.enableHmeLevel0Flag = 1;
                                    oc.enableHmeLevel1Flag = l1;
                                    oc.enableHmeLevel2Flag = l2;
                                    oc.useBestUnipred = boosted;
                                    oc.boosted = boosted != 0;
                                    oc.mrpSafeLimitNref = sc5;
                                    oc.mrpSafeLimitZzTh = 12345;
                                    oracleSigDerivMe(oc, o, res);
                                    CHECK(m.meSa.saMin.width == o.meMinW);
                                    CHECK(m.meSa.saMin.height == o.meMinH);
                                    CHECK(m.meSa.saMax.width == o.meMaxW);
                                    CHECK(m.meSa.saMax.height == o.meMaxH);
                                    CHECK(m.hmeL0Sa.saMin.width == o.hmeL0MinW);
                                    CHECK(m.hmeL0Sa.saMin.height == o.hmeL0MinH);
                                    CHECK(m.hmeL0Sa.saMax.width == o.hmeL0MaxW);
                                    CHECK(m.hmeL0Sa.saMax.height == o.hmeL0MaxH);
                                    CHECK(m.hmeL1Sa.width == o.hmeL1W);
                                    CHECK(m.hmeL1Sa.height == o.hmeL1H);
                                    CHECK(m.hmeL2Sa.width == o.hmeL2W);
                                    CHECK(m.hmeL2Sa.height == o.hmeL2H);
                                    CHECK(m.numHmeSaW == o.numHmeSaW);
                                    CHECK(m.numHmeSaH == o.numHmeSaH);
                                    CHECK(m.enableHmeFlag == (o.hmeFlag != 0));
                                    CHECK(m.enableHmeLevel0Flag == (o.hmeL0Flag != 0));
                                    CHECK(m.enableHmeLevel1Flag == (o.hmeL1Flag != 0));
                                    CHECK(m.enableHmeLevel2Flag == (o.hmeL2Flag != 0));
                                    CHECK(m.hmeSearchMethod == o.hmeMethod);
                                    CHECK(m.meSearchMethod == o.meMethod);
                                    CHECK(m.reduceHmeL0SrThMin == o.srThMin);
                                    CHECK(m.reduceHmeL0SrThMax == o.srThMax);
                                    CHECK(m.prehmeCtrl.enable == o.preEnable);
                                    if (o.preEnable != 0) {
                                        // Levels 1-4 write the full surface
                                        // (:566-609); level 0 writes only
                                        // enable (:563-565).
                                        CHECK(m.prehmeCtrl.prehmeSaCfg[0].saMin.width == o.preMin0W);
                                        CHECK(m.prehmeCtrl.prehmeSaCfg[0].saMax.height == o.preMax0H);
                                        CHECK(m.prehmeCtrl.prehmeSaCfg[1].saMin.width == o.preMin1W);
                                        CHECK(m.prehmeCtrl.prehmeSaCfg[1].saMax.height == o.preMax1H);
                                        CHECK(m.prehmeCtrl.skipSearchLine == o.preSkipLine);
                                        CHECK(m.prehmeCtrl.l1EarlyExit == o.preL1EarlyExit);
                                    } else {
                                        // The level-0 arm leaves the rest
                                        // untouched (:563-565).
                                        CHECK(m.prehmeCtrl.prehmeSaCfg[0].saMin.width == 0xAAAA);
                                        CHECK(m.prehmeCtrl.skipSearchLine == 0xAA);
                                    }
                                    CHECK(m.meHmePruneCtrls.enableMeHmeRefPruning == (o.pruneEnable != 0));
                                    CHECK(m.meHmePruneCtrls.pruneRefIfHmeSadDevBiggerThanTh == o.pruneHmeTh);
                                    CHECK(m.meHmePruneCtrls.pruneRefIfMeSadDevBiggerThanTh == o.pruneMeTh);
                                    CHECK(m.meHmePruneCtrls.zzSadTh == o.zzSadTh);
                                    CHECK(m.meHmePruneCtrls.zzSadPct == o.zzSadPct);
                                    CHECK(m.meHmePruneCtrls.phmeSadTh == o.phmeSadTh);
                                    CHECK(m.meHmePruneCtrls.phmeSadPct == o.phmeSadPct);
                                    CHECK(m.meSrAdjustmentCtrls.enableMeSrAdjustment == o.srEnable);
                                    CHECK(m.meSrAdjustmentCtrls.stationaryHmeSadAbsTh == o.srStationaryTh);
                                    CHECK(m.meSrAdjustmentCtrls.reduceMeSrBasedOnHmeSadAbsTh == o.srReduceTh);
                                    CHECK(m.mvBasedSaAdj.enabled == (o.mvSaEnable != 0));
                                    CHECK(m.me8x8VarCtrls.enabled == o.varEnable);
                                    CHECK(m.me8x8VarCtrls.meSrMult2Th == o.varMult2Th);
                                    CHECK(m.pruneMeCandidatesTh == o.pruneCandTh);
                                    CHECK(m.scClassMeBoost == o.scBoost);
                                    CHECK(m.useBestUnipredCandOnly == o.bestUnipred);
                                    CHECK(m.meEarlyExitTh == o.earlyExitTh);
                                    CHECK(m.meStaticB64Th == o.staticB64Th);
                                    CHECK(m.meSafeLimitZzTh == o.safeLimitZzTh);
                                    CHECK(m.prevMeStageBasedExitTh == o.prevStageExitTh);
                                    // No-stray-write: hme_l0_sa_default_tf
                                    // is untouched by sig_deriv_me.
                                    CHECK(m.hmeL0SaDefaultTf.saMin.width == 0xAAAA);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// svt_aom_sig_deriv_me_tf, independent transcription (:856-897).
static void oracleSigDerivMeTf(const MeCfgO& pcs, MeO& m, int res) {
    oracleTf(m, pcs.tfHmeMeLevel, res, pcs.tfQpOpt, pcs.tfMeQpScaling, pcs.qp);  // :858
    m.hmeFlag = pcs.tfEnableHmeFlag;             // :860
    m.hmeL0Flag = pcs.tfEnableHmeLevel0Flag;     // :861
    m.hmeL1Flag = pcs.tfEnableHmeLevel1Flag;     // :862
    m.hmeL2Flag = pcs.tfEnableHmeLevel2Flag;     // :863
    if (pcs.tfHmeMeLevel <= 2) {                 // :864-874
        m.hmeMethod = 1;                         // FULL_SAD_SEARCH
        m.meMethod = 1;
    } else {
        m.hmeMethod = 0;                         // SUB_SAD_SEARCH
        m.meMethod = 0;
    }
    oraclePrehme(m, 0);                          // :876-877
    oraclePrune(m, 0);                           // :881
    // :885 - the level-0 sr arm writes only enable (:465-467); the :521-531
    // post-processing then divides the UNWRITTEN th fields by 4/16, so the
    // poisoned 0xAAAA sentinel stands in (0xAAAA/4 = 10922, /16 = 2730).
    m.srStationaryTh = 0xAAAA;
    m.srReduceTh = 0xAAAA;
    oracleSr(m, 0, pcs.tfEnableHmeLevel2Flag, pcs.tfEnableHmeLevel1Flag);
    oracleMvSa(m, 0);                            // :887
    oracleVar8x8(m, 0);                          // :889
    m.scBoost = 0;                               // :891
    m.earlyExitTh = pcs.tfHmeMeLevel <= 1 ? 0u : 64u * 64u * 4u;   // :892
    // :893 is me_safe_limit_zz_th = 0 (set above). me_static_b64_th is
    // NOT written by the tf arm - its only writers are :841/:843 inside
    // sig_deriv_me - so staticB64Th keeps the oracleZero zero here and the
    // test pins the impl's no-write (poison stands).
    m.srThMin = 0;                               // :894
    m.srThMax = 0;                               // :895
    m.prevStageExitTh = pcs.tfHmeMeLevel <= 1 ? 0u : 64u * 64u * 4u;  // :896
}

TEST_CASE("sig_deriv_me_tf - the composition over every (level, resolution, qp_opt, tf-flag) cell") {
    // :856-897. The sr post-processing sees the tf flags (:863 sets them
    // before the :885 call reads them).
    for (int level = 0; level <= 5; ++level) {
        for (int res = 0; res <= 6; ++res) {
            for (int qpOpt = 0; qpOpt <= 1; ++qpOpt) {
                for (int flag = 0; flag <= 1; ++flag) {
                    MeModeCfg pcs{};
                    pcs.inputResolution = (ResolutionRange)res;
                    pcs.tfHmeMeLevel = (uint8_t)level;
                    pcs.tfQpOpt = (uint8_t)qpOpt;
                    pcs.tfMeQpBasedThScaling = flag != 0;
                    pcs.qp = 37;
                    pcs.tfEnableHmeFlag = flag != 0;
                    pcs.tfEnableHmeLevel0Flag = flag != 0;
                    pcs.tfEnableHmeLevel1Flag = flag != 0;
                    pcs.tfEnableHmeLevel2Flag = flag != 0;
                    MeSearchParams m;
                    std::memset(&m, kPoison, sizeof(m));
                    sigDerivMeTf(pcs, m);
                    MeO o;
                    oracleZero(o);
                    MeCfgO oc{};
                    oc.tfHmeMeLevel = level;
                    oc.tfQpOpt = qpOpt;
                    oc.tfMeQpScaling = flag != 0;
                    oc.qp = 37;
                    oc.tfEnableHmeFlag = flag != 0;
                    oc.tfEnableHmeLevel0Flag = flag;
                    oc.tfEnableHmeLevel1Flag = flag;
                    oc.tfEnableHmeLevel2Flag = flag;
                    oracleSigDerivMeTf(oc, o, res);
                    if (level <= 4) {
                        CHECK(m.numHmeSaW == o.numHmeSaW);
                        CHECK(m.numHmeSaH == o.numHmeSaH);
                        CHECK(m.hmeL0SaDefaultTf.saMin.width == o.hmeL0TfMinW);
                        CHECK(m.hmeL0SaDefaultTf.saMax.height == o.hmeL0TfMaxH);
                        CHECK(m.hmeL1Sa.width == o.hmeL1W);
                        CHECK(m.hmeL1Sa.height == o.hmeL1H);
                        CHECK(m.hmeL2Sa.width == o.hmeL2W);
                        CHECK(m.hmeL2Sa.height == o.hmeL2H);
                        CHECK(m.meSa.saMin.width == o.meMinW);
                        CHECK(m.meSa.saMin.height == o.meMinH);
                        CHECK(m.meSa.saMax.width == o.meMaxW);
                        CHECK(m.meSa.saMax.height == o.meMaxH);
                    } else {
                        // The default arm (:684-686) writes nothing.
                        CHECK(m.numHmeSaW == 0xAAAA);
                        CHECK(m.hmeL0SaDefaultTf.saMin.width == 0xAAAA);
                    }
                    CHECK(m.enableHmeFlag == (o.hmeFlag != 0));
                    CHECK(m.enableHmeLevel0Flag == (o.hmeL0Flag != 0));
                    CHECK(m.enableHmeLevel1Flag == (o.hmeL1Flag != 0));
                    CHECK(m.enableHmeLevel2Flag == (o.hmeL2Flag != 0));
                    CHECK(m.hmeSearchMethod == o.hmeMethod);
                    CHECK(m.meSearchMethod == o.meMethod);
                    CHECK(m.prehmeCtrl.enable == o.preEnable);
                    CHECK(m.meHmePruneCtrls.enableMeHmeRefPruning == (o.pruneEnable != 0));
                    CHECK(m.meHmePruneCtrls.pruneRefIfHmeSadDevBiggerThanTh == o.pruneHmeTh);
                    CHECK(m.meHmePruneCtrls.pruneRefIfMeSadDevBiggerThanTh == o.pruneMeTh);
                    CHECK(m.meSrAdjustmentCtrls.enableMeSrAdjustment == o.srEnable);
                    CHECK(m.meSrAdjustmentCtrls.stationaryHmeSadAbsTh == o.srStationaryTh);
                    CHECK(m.meSrAdjustmentCtrls.reduceMeSrBasedOnHmeSadAbsTh == o.srReduceTh);
                    CHECK(m.mvBasedSaAdj.enabled == (o.mvSaEnable != 0));
                    CHECK(m.me8x8VarCtrls.enabled == o.varEnable);
                    CHECK(m.scClassMeBoost == o.scBoost);
                    CHECK(m.meEarlyExitTh == o.earlyExitTh);
                    // :893 is me_safe_limit_zz_th; me_static_b64_th is
                    // never written by the tf arm (:856-897), so the
                    // poisoned sentinel must stand (no-stray-write).
                    CHECK(m.meSafeLimitZzTh == o.safeLimitZzTh);
                    CHECK(m.meStaticB64Th == 0xAAAAAAAAu);
                    CHECK(m.reduceHmeL0SrThMin == o.srThMin);
                    CHECK(m.reduceHmeL0SrThMax == o.srThMax);
                    CHECK(m.prevMeStageBasedExitTh == o.prevStageExitTh);
                }
            }
        }
    }
}


// ---------------------------------------------------------------------------
// Batch B: the cdef search/recon setter family (enc_mode_config.c:899-1240).
// ---------------------------------------------------------------------------

// Test-side pf_gi copy (enc_mode_config.c:21) - the oracle never reads the
// port's table.
static const uint8_t kPfGi[16] = {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60};

// The poisoned int8 sentinel: 0xAA reinterpreted as int8_t (= -86).
static const int8_t kPoisonI8 = -86;

struct CdefSearchOracle {
    uint8_t enabled;
    uint8_t firstPassFsNum;
    uint8_t firstPass[64];
    uint8_t secondPassFsNum;
    uint8_t secondPass[64];
    int8_t firstPassUv[64];
    int8_t secondPassUv[64];
    int8_t useReferenceCdefFs;
    uint8_t subsamplingFactor;
    uint8_t searchBestRefFs;
    uint8_t skipTh;
    int uvFromY;
    int useQpStrength;
};

// Independent transcription of set_cdef_search_controls (:899-1206). The
// uv arrays hold the poison sentinel wherever the reference leaves them
// untouched; the tri-state uvFromY/useQpStrength make the :1197 post-loop
// guard fire only when the reference's own would.
static void oracleCdefSearch(CdefSearchOracle& o, int level, bool isBase, bool isNotHighestLayer) {
    std::memset(&o, 0xAA, sizeof(o));
    switch (level) {
    case 0:  // :906-911
        o.enabled = 0;
        o.useReferenceCdefFs = 0;
        o.skipTh = 0;
        o.uvFromY = false;
        break;
    case 1: {  // :912-955 pf_set {0..15}, sf_set {0,1,2,3}
        o.enabled = 1;
        o.firstPassFsNum = 16;
        o.secondPassFsNum = 16 * 3;
        for (int i = 0; i < 16; ++i) o.firstPass[i] = kPfGi[i];
        int sf = 0;
        for (int i = 0; i < 16; ++i)
            for (int j = 1; j < 4; ++j) o.secondPass[sf++] = (uint8_t)(o.firstPass[i] + j);
        for (int i = 0; i < 16; ++i) o.firstPassUv[i] = (int8_t)o.firstPass[i];
        for (int i = 0; i < o.secondPassFsNum; ++i) o.secondPassUv[i] = (int8_t)o.secondPass[i];
        o.useReferenceCdefFs = 0;
        o.searchBestRefFs = 0;
        o.subsamplingFactor = 1;
        o.skipTh = 0;
        o.uvFromY = false;
        o.useQpStrength = false;
        break;
    }
    case 2: {  // :956-995 pf_set {0,1,2,4,5,6,8,9,10,12,13,14}
        o.enabled = 1;
        o.firstPassFsNum = 12;
        o.secondPassFsNum = 12 * 3;
        const int sel[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
        for (int i = 0; i < 12; ++i) o.firstPass[i] = kPfGi[sel[i]];
        int sf = 0;
        for (int i = 0; i < 12; ++i)
            for (int j = 1; j < 4; ++j) o.secondPass[sf++] = (uint8_t)(o.firstPass[i] + j);
        for (int i = 0; i < 12; ++i) o.firstPassUv[i] = (int8_t)o.firstPass[i];
        for (int i = 0; i < o.secondPassFsNum; ++i) o.secondPassUv[i] = -1;
        o.useReferenceCdefFs = 0;
        o.searchBestRefFs = 0;
        o.subsamplingFactor = 1;
        o.skipTh = 0;
        o.uvFromY = false;
        o.useQpStrength = false;
        break;
    }
    case 3: {  // :996-1028 pf_set {0,4,8,12,15}
        o.enabled = 1;
        o.firstPassFsNum = 5;
        o.secondPassFsNum = 5 * 3;
        const int sel[5] = {0, 4, 8, 12, 15};
        for (int i = 0; i < 5; ++i) o.firstPass[i] = kPfGi[sel[i]];
        int sf = 0;
        for (int i = 0; i < 5; ++i)
            for (int j = 1; j < 4; ++j) o.secondPass[sf++] = (uint8_t)(o.firstPass[i] + j);
        for (int i = 0; i < 5; ++i) o.firstPassUv[i] = (int8_t)o.firstPass[i];
        for (int i = 0; i < o.secondPassFsNum; ++i) o.secondPassUv[i] = -1;
        o.useReferenceCdefFs = 0;
        o.searchBestRefFs = 0;
        o.subsamplingFactor = 1;
        o.skipTh = 0;
        o.uvFromY = false;
        o.useQpStrength = false;
        break;
    }
    case 4: {  // :1029-1059 pf_set {0,7,15}
        o.enabled = 1;
        o.firstPassFsNum = 3;
        o.secondPassFsNum = 3 * 3;
        const int sel[3] = {0, 7, 15};
        for (int i = 0; i < 3; ++i) o.firstPass[i] = kPfGi[sel[i]];
        int sf = 0;
        for (int i = 0; i < 3; ++i)
            for (int j = 1; j < 4; ++j) o.secondPass[sf++] = (uint8_t)(o.firstPass[i] + j);
        for (int i = 0; i < 3; ++i) o.firstPassUv[i] = (int8_t)o.firstPass[i];
        for (int i = 0; i < o.secondPassFsNum; ++i) o.secondPassUv[i] = -1;
        o.useReferenceCdefFs = 0;
        o.searchBestRefFs = 0;
        o.subsamplingFactor = 1;
        o.skipTh = 0;
        o.uvFromY = false;
        o.useQpStrength = false;
        break;
    }
    case 5: {  // :1060-1086 pf_set {0,7,15}, sf_set {0,2}
        o.enabled = 1;
        o.firstPassFsNum = 3;
        o.secondPassFsNum = 3 * 1;
        o.firstPass[0] = kPfGi[0];
        o.firstPass[1] = kPfGi[7];
        o.firstPass[2] = kPfGi[15];
        o.secondPass[0] = (uint8_t)(kPfGi[0] + 2);
        o.secondPass[1] = (uint8_t)(kPfGi[7] + 2);
        o.secondPass[2] = (uint8_t)(kPfGi[15] + 2);
        for (int i = 0; i < 3; ++i) o.firstPassUv[i] = (int8_t)o.firstPass[i];
        for (int i = 0; i < o.secondPassFsNum; ++i) o.secondPassUv[i] = -1;
        o.useReferenceCdefFs = 0;
        o.searchBestRefFs = isNotHighestLayer ? 0 : 1;
        o.subsamplingFactor = 1;
        o.skipTh = 0;
        o.uvFromY = false;
        o.useQpStrength = false;
        break;
    }
    case 6: {  // :1087-1112 pf_set {0,15}, sf_set {0,2}
        o.enabled = 1;
        o.firstPassFsNum = 2;
        o.secondPassFsNum = 2 * 1;
        o.firstPass[0] = kPfGi[0];
        o.firstPass[1] = kPfGi[15];
        o.secondPass[0] = (uint8_t)(kPfGi[0] + 2);
        o.secondPass[1] = (uint8_t)(kPfGi[15] + 2);
        o.firstPassUv[0] = (int8_t)o.firstPass[0];
        o.firstPassUv[1] = (int8_t)o.firstPass[1];
        o.firstPassUv[2] = -1;
        o.secondPassUv[0] = -1;
        o.secondPassUv[1] = -1;
        o.useReferenceCdefFs = 0;
        o.searchBestRefFs = isNotHighestLayer ? 0 : 1;
        o.subsamplingFactor = 4;
        o.skipTh = 0;
        o.uvFromY = false;
        o.useQpStrength = false;
        break;
    }
    case 7: {  // :1113-1137
        o.enabled = 1;
        o.firstPassFsNum = 2;
        o.secondPassFsNum = 2 * 1;
        o.firstPass[0] = kPfGi[0];
        o.firstPass[1] = kPfGi[15];
        o.secondPass[0] = (uint8_t)(kPfGi[0] + 2);
        o.secondPass[1] = (uint8_t)(kPfGi[15] + 2);
        o.firstPassUv[0] = (int8_t)o.firstPass[0];
        o.firstPassUv[1] = (int8_t)o.firstPass[1];
        o.firstPassUv[2] = -1;
        o.secondPassUv[0] = -1;
        o.secondPassUv[1] = -1;
        o.useReferenceCdefFs = isNotHighestLayer ? 0 : 1;
        o.searchBestRefFs = isBase ? 0 : 1;
        o.subsamplingFactor = 4;
        o.skipTh = isBase ? 0 : 80;
        o.uvFromY = false;
        o.useQpStrength = false;
        break;
    }
    case 8: {  // :1138-1162
        o.enabled = 1;
        o.firstPassFsNum = 2;
        o.secondPassFsNum = 2 * 1;
        o.firstPass[0] = kPfGi[0];
        o.firstPass[1] = kPfGi[15];
        o.secondPass[0] = (uint8_t)(kPfGi[0] + 2);
        o.secondPass[1] = (uint8_t)(kPfGi[15] + 2);
        o.firstPassUv[0] = -1;
        o.firstPassUv[1] = -1;
        o.firstPassUv[2] = -1;
        o.secondPassUv[0] = -1;
        o.secondPassUv[1] = -1;
        o.useReferenceCdefFs = isBase ? 0 : 1;
        o.searchBestRefFs = isBase ? 0 : 1;
        o.subsamplingFactor = 4;
        o.skipTh = isBase ? 0 : 80;
        o.uvFromY = true;
        o.useQpStrength = false;
        break;
    }
    case 9: {  // :1164-1184 primary-only
        o.enabled = 1;
        o.firstPassFsNum = 2;
        o.secondPassFsNum = 0;
        o.firstPass[0] = kPfGi[0];
        o.firstPass[1] = kPfGi[15];
        o.firstPassUv[0] = -1;
        o.firstPassUv[1] = -1;
        o.firstPassUv[2] = -1;
        o.secondPassUv[0] = -1;
        o.secondPassUv[1] = -1;
        o.useReferenceCdefFs = isBase ? 0 : 1;
        o.searchBestRefFs = isBase ? 0 : 1;
        o.subsamplingFactor = 4;
        o.skipTh = isBase ? 0 : 80;
        o.uvFromY = true;
        o.useQpStrength = false;
        break;
    }
    case 10:  // :1186-1191
        o.enabled = 1;
        o.useReferenceCdefFs = 0;
        o.useQpStrength = true;
        o.skipTh = isBase ? 0 : 80;
        break;
    default:  // :1192-1194 assert(0) writes nothing
        break;
    }
    // :1196-1205 - the chroma-copy post-loop.
    if (o.uvFromY && !o.useQpStrength) {
        for (int i = 0; i < o.firstPassFsNum; ++i) o.firstPassUv[i] = -1;
        for (int i = 0; i < o.secondPassFsNum; ++i) o.secondPassUv[i] = -1;
    }
}

TEST_CASE("set_cdef_search_controls - every level x isBase x isNotHighestLayer, whole-struct") {
    // :899-1206. Levels 1-5 write the full surface; 6-9 the short pf sets
    // with the tail uv slots untouched; 0 and 10 write their scalars only;
    // the default arm writes nothing. The two poisoned bools stay unread
    // where the reference does not write them (the gm precedent).
    for (int level = 0; level <= 11; ++level) {
        for (int base = 0; base <= 1; ++base) {
            for (int nl = 0; nl <= 1; ++nl) {
                CdefSearchCtrls c;
                std::memset(&c, kPoison, sizeof(c));
                setCdefSearchCtrls(c, (uint8_t)level, base != 0, nl != 0);
                CdefSearchOracle o;
                oracleCdefSearch(o, level, base != 0, nl != 0);
                CHECK(c.enabled == o.enabled);
                CHECK(c.firstPassFsNum == o.firstPassFsNum);
                CHECK(c.defaultSecondPassFsNum == o.secondPassFsNum);
                CHECK(c.useReferenceCdefFs == o.useReferenceCdefFs);
                CHECK(c.subsamplingFactor == o.subsamplingFactor);
                CHECK(c.searchBestRefFs == o.searchBestRefFs);
                CHECK(c.skipTh == o.skipTh);
                for (int i = 0; i < 64; ++i) {
                    CHECK(c.defaultFirstPassFs[i] == o.firstPass[i]);
                    CHECK(c.defaultSecondPassFs[i] == o.secondPass[i]);
                    CHECK(c.defaultFirstPassFsUv[i] == o.firstPassUv[i]);
                    CHECK(c.defaultSecondPassFsUv[i] == o.secondPassUv[i]);
                }
                // The bools are compared only where the reference writes
                // them: uv_from_y by levels 0-9, use_qp_strength by 1-10
                // (reading a bool holding 0xAA is UB - the gm precedent).
                if (level <= 9) CHECK(c.uvFromY == (o.uvFromY != 0));
                if (level >= 1 && level <= 10) CHECK(c.useQpStrength == (o.useQpStrength != 0));
            }
        }
    }
}

TEST_CASE("set_cdef_recon_controls - the five levels, whole-struct") {
    // :1208-1240. Each case writes exactly the three fields; the default
    // arm writes nothing (the poison stands).
    for (int level = 0; level <= 5; ++level) {
        CdefReconCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setCdefReconCtrls(c, (uint8_t)level);
        switch (level) {
        case 0:
            CHECK(c.zeroFsCostBias == 0);
            CHECK(c.zeroFilterStrengthLvl == 0);
            CHECK(c.prevCdefDistTh == 0);
            break;
        case 1:
            CHECK(c.zeroFsCostBias == 61);
            CHECK(c.zeroFilterStrengthLvl == 2);
            CHECK(c.prevCdefDistTh == 10);
            break;
        case 2:
            CHECK(c.zeroFsCostBias == 61);
            CHECK(c.zeroFilterStrengthLvl == 3);
            CHECK(c.prevCdefDistTh == 10);
            break;
        case 3:
            CHECK(c.zeroFsCostBias == 60);
            CHECK(c.zeroFilterStrengthLvl == 3);
            CHECK(c.prevCdefDistTh == 10);
            break;
        case 4:
            CHECK(c.zeroFsCostBias == 58);
            CHECK(c.zeroFilterStrengthLvl == 3);
            CHECK(c.prevCdefDistTh == 10);
            break;
        default:  // :1236-1238 assert(0) writes nothing
            CHECK(c.zeroFsCostBias == 0xAAAA);
            CHECK(c.zeroFilterStrengthLvl == kPoison);
            CHECK(c.prevCdefDistTh == 0xAAAA);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Batch B: set_dlf_controls (enc_mode_config.c:1569-1665).
// ---------------------------------------------------------------------------

struct DlfOracle {
    int enabled, sbBasedDlf, pickMethod, dlfAvg, useRefAvgY, useRefAvgUv;
    int earlyExitConvergence, zeroFilterStrengthLvl;
    uint16_t prevDlfDistTh;
};

// Independent transcription of svt_aom_set_dlf_controls (:1569-1665).
// Tri-state ints: -1 = untouched (the default arm), else the written value.
static void oracleDlf(DlfOracle& o, int level) {
    o.enabled = o.sbBasedDlf = o.pickMethod = o.dlfAvg = o.useRefAvgY = o.useRefAvgUv = -1;
    o.earlyExitConvergence = o.zeroFilterStrengthLvl = -1;
    o.prevDlfDistTh = 0;
    switch (level) {
    case 0:
        o.enabled = 0; o.sbBasedDlf = 0; o.pickMethod = 0 /*LPF_PICK_FROM_FULL_IMAGE*/;
        o.dlfAvg = 0; o.useRefAvgY = 0; o.useRefAvgUv = 0;
        o.earlyExitConvergence = 0; o.zeroFilterStrengthLvl = 0; o.prevDlfDistTh = 0;
        break;
    case 1:
        o.enabled = 1; o.sbBasedDlf = 0; o.pickMethod = 0;
        o.dlfAvg = 0; o.useRefAvgY = 0; o.useRefAvgUv = 0;
        o.earlyExitConvergence = 0; o.zeroFilterStrengthLvl = 0; o.prevDlfDistTh = 0;
        break;
    case 2:
        o.enabled = 1; o.sbBasedDlf = 0; o.pickMethod = 0;
        o.dlfAvg = 1; o.useRefAvgY = 0; o.useRefAvgUv = 1;
        o.earlyExitConvergence = 1; o.zeroFilterStrengthLvl = 0; o.prevDlfDistTh = 0;
        break;
    case 3:
        o.enabled = 1; o.sbBasedDlf = 0; o.pickMethod = 0;
        o.dlfAvg = 1; o.useRefAvgY = 1; o.useRefAvgUv = 1;
        o.earlyExitConvergence = 1; o.zeroFilterStrengthLvl = 0; o.prevDlfDistTh = 0;
        break;
    case 4:
        o.enabled = 1; o.sbBasedDlf = 0; o.pickMethod = 0;
        o.dlfAvg = 1; o.useRefAvgY = 1; o.useRefAvgUv = 1;
        o.earlyExitConvergence = 1; o.zeroFilterStrengthLvl = 2; o.prevDlfDistTh = 10;
        break;
    case 5:
        o.enabled = 1; o.sbBasedDlf = 1; o.pickMethod = 2 /*LPF_PICK_FROM_Q*/;
        o.dlfAvg = 0; o.useRefAvgY = 0; o.useRefAvgUv = 0;
        o.earlyExitConvergence = 0; o.zeroFilterStrengthLvl = 1; o.prevDlfDistTh = 0;
        break;
    case 6:
        o.enabled = 1; o.sbBasedDlf = 1; o.pickMethod = 2;
        o.dlfAvg = 0; o.useRefAvgY = 0; o.useRefAvgUv = 0;
        o.earlyExitConvergence = 0; o.zeroFilterStrengthLvl = 2; o.prevDlfDistTh = 0;
        break;
    case 7:
        o.enabled = 1; o.sbBasedDlf = 1; o.pickMethod = 2;
        o.dlfAvg = 0; o.useRefAvgY = 0; o.useRefAvgUv = 0;
        o.earlyExitConvergence = 0; o.zeroFilterStrengthLvl = 3; o.prevDlfDistTh = 0;
        break;
    default:  // :1661-1663 assert(0) writes nothing
        break;
    }
}

TEST_CASE("set_dlf_controls - levels 0-7 write all nine fields, default writes nothing") {
    // :1569-1665. Every case body writes the full DlfCtrls surface, so the
    // bools are comparable at levels 0-7; at the default arm the poisoned
    // bools stay unread (the gm-case precedent).
    for (int level = 0; level <= 8; ++level) {
        DlfCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setDlfCtrls(c, (uint8_t)level);
        DlfOracle o;
        oracleDlf(o, level);
        if (level <= 7) {
            CHECK(c.enabled == o.enabled);
            CHECK(c.sbBasedDlf == o.sbBasedDlf);
            CHECK(c.pickMethod == o.pickMethod);
            CHECK(c.dlfAvg == (o.dlfAvg != 0));
            CHECK(c.useRefAvgY == (o.useRefAvgY != 0));
            CHECK(c.useRefAvgUv == (o.useRefAvgUv != 0));
            CHECK(c.earlyExitConvergence == o.earlyExitConvergence);
            CHECK(c.zeroFilterStrengthLvl == o.zeroFilterStrengthLvl);
            CHECK(c.prevDlfDistTh == o.prevDlfDistTh);
        } else {
            // :1661-1663 - assert(0), writes nothing; the uint8/uint16
            // poison stands (the bools stay unread).
            CHECK(c.enabled == kPoison);
            CHECK(c.sbBasedDlf == kPoison);
            CHECK(c.pickMethod == kPoison);
            CHECK(c.earlyExitConvergence == kPoison);
            CHECK(c.zeroFilterStrengthLvl == kPoison);
            CHECK(c.prevDlfDistTh == 0xAAAA);
        }
    }
}

// ---------------------------------------------------------------------------
// Batch B: set_intrabc_level (enc_mode_config.c:1672-1851).
// ---------------------------------------------------------------------------

TEST_CASE("set_intrabc_level - the level table, whole-struct field checks") {
    // :1672-1851 with MAX_INTRABC_LEVEL = 7 (:1670). Level 0 writes ONLY
    // enabled; levels 1-5 write the full surface; levels 6-7 write
    // everything EXCEPT mesh_search_mv_diff_threshold / mesh_patterns /
    // mesh_qp_scaling (poison stands); the default arm writes nothing.
    // Poisoned bools stay unread (the gm-case precedent).
    for (int level = 0; level <= 8; ++level) {
        IntrabcCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setIntrabcLevel(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.enabled == false);
            CHECK(c.maxBlockSizeHash == kPoison);
            CHECK(c.maxCandPerBucket == 0xAAAA);
            CHECK(c.exhaustiveMeshThresh == 0xAAAAAAAAAAAAAAAAull);
            CHECK((uint32_t)c.meshSearchMvDiffThreshold == 0xAAAAAAAAu);
            CHECK(c.searchDir == kPoison);
        } else if (level >= 1 && level <= 5) {
            CHECK(c.enabled == true);
            CHECK(c.paletteHint == (level >= 2));
            CHECK(c.nsqParentGating == (level >= 3));
            CHECK(c.b4ParentGating == false);
            CHECK(c.maxBlockSizeHash == (level <= 4 ? 64 : 8));
            CHECK(c.maxCandPerBucket == (level <= 3 ? 256 : (level == 4 ? 64 : 64)));
            CHECK(c.exhaustiveMeshThresh == (level <= 3 ? (1ull << 20) : (1ull << 24)));
            CHECK(c.meshSearchMvDiffThreshold == (level <= 2 ? -1 : 0));
            if (level == 1) {
                CHECK(c.meshPatterns[0].range == 256);
                CHECK(c.meshPatterns[0].interval == 1);
                CHECK(c.meshPatterns[1].range == 256);
                CHECK(c.meshPatterns[1].interval == 1);
            } else if (level == 2 || level == 3) {
                CHECK(c.meshPatterns[0].range == 256);
                CHECK(c.meshPatterns[0].interval == 8);
                CHECK(c.meshPatterns[1].range == 64);
                CHECK(c.meshPatterns[1].interval == 1);
            } else {
                CHECK(c.meshPatterns[0].range == 256);
                CHECK(c.meshPatterns[0].interval == 8);
                CHECK(c.meshPatterns[1].range == 32);
                CHECK(c.meshPatterns[1].interval == 1);
            }
            CHECK(c.meshPatterns[2].range == 0);
            CHECK(c.meshPatterns[2].interval == 0);
            CHECK(c.meshPatterns[3].range == 0);
            CHECK(c.meshPatterns[3].interval == 0);
            CHECK(c.meshQpScaling == (level >= 3));
            CHECK(c.searchDir == 0);
        } else if (level == 6 || level == 7) {
            CHECK(c.enabled == true);
            CHECK(c.paletteHint == true);
            CHECK(c.nsqParentGating == true);
            CHECK(c.b4ParentGating == false);
            CHECK(c.maxBlockSizeHash == 8);
            CHECK(c.maxCandPerBucket == 32);
            CHECK(c.exhaustiveMeshThresh == ~(uint64_t)0);
            // :1819-1820/:1839-1840 - the mesh surface is NOT written by
            // levels 6-7; the poison stands (no-stray-write proof).
            CHECK((uint32_t)c.meshSearchMvDiffThreshold == 0xAAAAAAAAu);
            CHECK((uint32_t)c.meshPatterns[0].range == 0xAAAAAAAAu);
            CHECK((uint32_t)c.meshPatterns[1].interval == 0xAAAAAAAAu);
            CHECK(c.searchDir == (level == 6 ? 0 : 1));
        } else {
            // :1847-1849 - assert(0), writes nothing.
            CHECK(c.maxBlockSizeHash == kPoison);
            CHECK(c.maxCandPerBucket == 0xAAAA);
            CHECK(c.exhaustiveMeshThresh == 0xAAAAAAAAAAAAAAAAull);
            CHECK((uint32_t)c.meshSearchMvDiffThreshold == 0xAAAAAAAAu);
            CHECK((uint32_t)c.meshPatterns[2].range == 0xAAAAAAAAu);
            CHECK(c.searchDir == kPoison);
        }
    }
}

// ---------------------------------------------------------------------------
// Batch B: set_palette_level (enc_mode_config.c:1856-1930).
// ---------------------------------------------------------------------------

TEST_CASE("set_palette_level - the ten-level table, whole-struct field checks") {
    // :1856-1930. Level 0 writes ONLY enabled; levels 1-9 write all five
    // PaletteCtrls fields; the default arm writes nothing. The poisoned
    // centroid_refinement bool stays unread at level 0 and default (the
    // gm-case precedent); dominant_color_step = (uint8_t)~0 = 255 at
    // levels 3-9 (:1879+).
    for (int level = 0; level <= 10; ++level) {
        PaletteCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setPaletteLevel(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.enabled == 0);
            CHECK(c.dominantColorStep == kPoison);
            CHECK(c.kmeanColorStep == kPoison);
            CHECK(c.kMeansMaxItr == kPoison);
        } else if (level <= 9) {
            CHECK(c.enabled == 1);
            CHECK(c.dominantColorStep == (level == 1 ? 1 : (level == 2 ? 2 : 255)));
            CHECK(c.kmeanColorStep == (level == 1 ? 1 : (level == 2 ? 1 : (level == 3 ? 1 :
                                                   (level == 4 ? 2 : (level == 5 ? 3 :
                                                   (level == 6 ? 5 : (level == 7 ? 5 :
                                                   (level == 8 ? 6 : 50)))))))));
            CHECK(c.centroidRefinement == (level == 1));
            // k_means_max_itr: 50 at L1 (:1868), 2 at L2-L6
            // (:1875/:1882/:1889/:1896/:1903), 1 at L7-L9
            // (:1910/:1917/:1924).
            CHECK(c.kMeansMaxItr == (level == 1 ? 50 : (level <= 6 ? 2 : 1)));
        } else {
            // :1926-1928 - assert(0), writes nothing.
            CHECK(c.enabled == kPoison);
            CHECK(c.dominantColorStep == kPoison);
            CHECK(c.kmeanColorStep == kPoison);
            CHECK(c.kMeansMaxItr == kPoison);
        }
    }
}

// ---------------------------------------------------------------------------
// Batch B: set_wn_filter_ctrls (:1242-1301) + set_sg_filter_ctrls
// (:1303-1362). The Av1Common wall folds into the two ctrl carriers (the
// wn/sg LEVEL getters landed in slice 1; these are the ctrls consumers).
// ---------------------------------------------------------------------------

TEST_CASE("set_wn_filter_ctrls - levels 0-6, whole-struct field checks") {
    // :1242-1301. Level 0 writes ONLY enabled; levels 1-6 write all six
    // WnFilterCtrls fields; the default arm writes nothing. The poisoned
    // bools stay unread at level 0 and default (the gm-case precedent).
    for (int level = 0; level <= 7; ++level) {
        WnFilterCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setWnFilterCtrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.enabled == false);
            CHECK(c.filterTapLvl == kPoison);
        } else if (level <= 6) {
            CHECK(c.enabled == true);
            CHECK(c.useChroma == (level <= 4));
            CHECK(c.filterTapLvl == (level <= 2 ? 1 : 2));
            CHECK(c.useRefinement == (level <= 3));
            CHECK(c.maxOneRefinementStep == (level == 1 ? false : true));
            CHECK(c.usePrevFrameCoeffs == (level == 6));
        } else {
            // :1297-1299 - assert(0), writes nothing.
            CHECK(c.filterTapLvl == kPoison);
        }
    }
}

TEST_CASE("set_sg_filter_ctrls - levels 0-4, whole-struct field checks") {
    // :1303-1362. Level 0 writes ONLY enabled; levels 1-4 write the full
    // SgFilterCtrls surface; the default arm writes nothing. The poisoned
    // bool stays unread at level 0 and default; the int8 arrays keep the
    // 0xAA-as-int8 sentinel where unwritten.
    for (int level = 0; level <= 5; ++level) {
        SgFilterCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setSgFilterCtrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.enabled == false);
            CHECK(c.startEp[0] == kPoisonI8);
            CHECK(c.endEp[1] == kPoisonI8);
        } else if (level <= 4) {
            CHECK(c.enabled == true);
            CHECK(c.useChroma == (level <= 3));
            CHECK(c.startEp[0] == 0);
            CHECK(c.endEp[0] == 16);
            CHECK(c.epInc[0] == (level <= 2 ? 1 : 8));
            CHECK(c.startEp[1] == (level == 1 ? 0 : 4));
            CHECK(c.endEp[1] == (level == 1 ? 16 : 5));
            CHECK(c.epInc[1] == 1);
            CHECK(c.refine[0] == 1);
            CHECK(c.refine[1] == (level == 1 ? 1 : 0));
        } else {
            // :1358-1360 - assert(0), writes nothing. The poisoned bools
            // stay unread; the int8 sentinels stand.
            CHECK(c.startEp[0] == kPoisonI8);
            CHECK(c.refine[1] == kPoisonI8);
        }
    }
}

// ---------------------------------------------------------------------------
// Batch B: the small helper family (get_gm_core_level :181-199,
// derive_gm_level :201-215, derive_input_resolution scs.c:120-139,
// get_max_can_count :1936-1981).
// ---------------------------------------------------------------------------

TEST_CASE("get_gm_core_level - the CONFIG fork and the super-res-off ladder") {
    // :181-199. CONFIG=0: always 0. CONFIG=1: super_res_off gates the
    // ladder (MR->2, M0-M4->4, else 0); super_res_off=false -> 0. The
    // fork is threaded as the runtime parameter (the setGmControls
    // precedent).
    for (int mode = -1; mode <= 13; ++mode) {
        CHECK(getGmCoreLevel((EncMode)mode, true, 0) == 0);
        CHECK(getGmCoreLevel((EncMode)mode, false, 1) == 0);
        int expect = 0;
        if (mode <= -1) expect = 2;
        else if (mode <= 4) expect = 4;
        else expect = 0;
        CHECK(getGmCoreLevel((EncMode)mode, true, 1) == expect);
    }
}

TEST_CASE("derive_gm_level - the inter-only gate") {
    // :201-215. is_islice -> 0 regardless; !is_islice ->
    // get_gm_core_level(enc_mode, super_res_off). The pcs wall folds into
    // the two consumed scalars.
    for (int mode = -1; mode <= 13; ++mode) {
        CHECK(deriveGmLevel((EncMode)mode, true, true, 1) == 0);
        int expect = 0;
        if (mode <= -1) expect = 2;
        else if (mode <= 4) expect = 4;
        CHECK(deriveGmLevel((EncMode)mode, false, true, 1) == expect);
        CHECK(deriveGmLevel((EncMode)mode, false, false, 1) == 0);
    }
}

TEST_CASE("derive_input_resolution - the seven-band boundary table") {
    // sequence_control_set.c:120-139 with definitions.h:1819-1824
    // thresholds. Every band boundary is tested from BOTH sides (the
    // range-enumeration rule); the EbErrorType return (always
    // EB_ErrorNone) is dropped - named deviation.
    const struct { uint32_t size; ResolutionRange res; } t[] = {
        {0x284FFu, INPUT_SIZE_240p_RANGE},
        {0x28500u, INPUT_SIZE_360p_RANGE},
        {0x4CDFFu, INPUT_SIZE_360p_RANGE},
        {0x4CE00u, INPUT_SIZE_480p_RANGE},
        {0xA13FFu, INPUT_SIZE_480p_RANGE},
        {0xA1400u, INPUT_SIZE_720p_RANGE},
        {0x16D9FFu, INPUT_SIZE_720p_RANGE},
        {0x16DA00u, INPUT_SIZE_1080p_RANGE},
        {0x5351FFu, INPUT_SIZE_1080p_RANGE},
        {0x535200u, INPUT_SIZE_4K_RANGE},
        {0x1409FFFu, INPUT_SIZE_4K_RANGE},
        {0x140A000u, INPUT_SIZE_8K_RANGE},
        {0xFFFFFFFFu, INPUT_SIZE_8K_RANGE},
    };
    for (const auto& e : t) CHECK(deriveInputResolution(e.size) == e.res);
}

TEST_CASE("get_max_can_count - the rtc and non-rtc preset ladders") {
    // :1936-1981. Every preset boundary enumerated from -1 (ENC_MR)
    // through 13; the ladder is <=-chained so only the boundary presets
    // plus one interior preset per band need the value checked - but the
    // loop checks ALL presets (the range-enumeration rule).
    for (int mode = -1; mode <= 13; ++mode) {
        int rtc;
        if (mode <= 7) rtc = 150;
        else if (mode == 8) rtc = 75;
        else if (mode <= 10) rtc = 65;
        else if (mode == 11) rtc = 15;
        else rtc = 10;
        CHECK(getMaxCanCount((EncMode)mode, true) == rtc);
        int nrtc;
        if (mode <= 1) nrtc = 1225;
        else if (mode == 2) nrtc = 1000;
        else if (mode == 3) nrtc = 720;
        else if (mode == 4) nrtc = 576;
        else if (mode == 5) nrtc = 369;
        else if (mode == 6) nrtc = 236;
        else if (mode <= 9) nrtc = 190;
        else nrtc = 80;
        CHECK(getMaxCanCount((EncMode)mode, false) == nrtc);
    }
}

// ---------------------------------------------------------------------------
// Batch B: the sig_deriv_multi_processes family (:1988-2518). The oracle
// transcribes the three composition flows independently; the per-level
// ctrl-struct expectations are independent re-transcriptions of the setter
// tables (the full structs are setter-test-proven; the composition test
// proves the level derivation and the wiring).
// ---------------------------------------------------------------------------

struct MultiProcO {
    int gmLevel;
    int hmeFlag, hmeL0, hmeL1, hmeL2;
    int tfHmeFlag, tfHmeL0, tfHmeL1, tfHmeL2;
    int multiPassPd;
    int intrabcLevel;
    int paletteLevel;
    int cdefSearchLevel, cdefReconLevel;
    int wnLevel, sgLevel;
    int initRes;
    int enableRestoration, cdfMode, hbdMd, maxCanCount, useBest;
    int allowIntrabc, scTools;
};

// oracle reps for the setter tables (independent re-transcriptions).
static int oracleIntrabcEnabled(int level) { return level != 0; }
static void oracleIntrabcRep(int level, int& hashBs, int& cand, std::uint64_t& thresh, int& dir) {
    hashBs = 0; cand = 0; thresh = 0; dir = 0;
    switch (level) {
    case 1: case 2: case 3: hashBs = 64; cand = 256; thresh = 1ull << 20; dir = 0; break;
    case 4: hashBs = 64; cand = 64; thresh = 1ull << 24; dir = 0; break;
    case 5: hashBs = 8; cand = 64; thresh = 1ull << 24; dir = 0; break;
    case 6: hashBs = 8; cand = 32; thresh = ~(std::uint64_t)0; dir = 0; break;
    case 7: hashBs = 8; cand = 32; thresh = ~(std::uint64_t)0; dir = 1; break;
    default: break;
    }
}
static void oraclePaletteRep(int level, int& en, int& dcs, int& kcs, int& cr, int& kmitr) {
    en = 0; dcs = 0; kcs = 0; cr = 0; kmitr = 0;
    switch (level) {
    case 1: en = 1; dcs = 1; kcs = 1; cr = 1; kmitr = 50; break;
    case 2: en = 1; dcs = 2; kcs = 1; cr = 0; kmitr = 2; break;
    case 3: en = 1; dcs = 255; kcs = 1; cr = 0; kmitr = 2; break;
    case 4: en = 1; dcs = 255; kcs = 2; cr = 0; kmitr = 2; break;
    case 5: en = 1; dcs = 255; kcs = 3; cr = 0; kmitr = 2; break;
    case 6: en = 1; dcs = 255; kcs = 5; cr = 0; kmitr = 2; break;
    case 7: en = 1; dcs = 255; kcs = 5; cr = 0; kmitr = 1; break;
    case 8: en = 1; dcs = 255; kcs = 6; cr = 0; kmitr = 1; break;
    case 9: en = 1; dcs = 255; kcs = 50; cr = 0; kmitr = 1; break;
    default: break;
    }
}
static void oracleCdefSearchRep(int level, bool base, bool nl, int& en, int& sub, int& skipTh,
                                int& searchBest, int& useRef) {
    en = 0; sub = 0; skipTh = 0; searchBest = 0; useRef = 0;
    switch (level) {
    case 1: case 2: case 3: case 4: en = 1; sub = 1; skipTh = 0; searchBest = 0; useRef = 0; break;
    case 5: en = 1; sub = 1; skipTh = 0; searchBest = nl ? 0 : 1; useRef = 0; break;
    case 6: en = 1; sub = 4; skipTh = 0; searchBest = nl ? 0 : 1; useRef = 0; break;
    case 7: en = 1; sub = 4; skipTh = base ? 0 : 80; searchBest = base ? 0 : 1; useRef = nl ? 0 : 1; break;
    case 8: en = 1; sub = 4; skipTh = base ? 0 : 80; searchBest = base ? 0 : 1; useRef = base ? 0 : 1; break;
    case 9: en = 1; sub = 4; skipTh = base ? 0 : 80; searchBest = base ? 0 : 1; useRef = base ? 0 : 1; break;
    case 10: en = 1; sub = -1; skipTh = base ? 0 : 80; searchBest = 0; useRef = 0; break;
    default: break;
    }
}
static void oracleCdefReconRep(int level, int& zb, int& zl, int& pd) {
    zb = 0; zl = 0; pd = 0;
    switch (level) {
    case 1: zb = 61; zl = 2; pd = 10; break;
    case 2: zb = 61; zl = 3; pd = 10; break;
    case 3: zb = 60; zl = 3; pd = 10; break;
    case 4: zb = 58; zl = 3; pd = 10; break;
    default: break;
    }
}
static void oracleWnRep(int level, int& en, int& chroma, int& tap, int& refine, int& maxOne, int& prev) {
    en = 0; chroma = 0; tap = 0; refine = 0; maxOne = 0; prev = 0;
    switch (level) {
    case 1: en = 1; chroma = 1; tap = 1; refine = 1; maxOne = 0; prev = 0; break;
    case 2: en = 1; chroma = 1; tap = 1; refine = 1; maxOne = 1; prev = 0; break;
    case 3: en = 1; chroma = 1; tap = 2; refine = 1; maxOne = 1; prev = 0; break;
    case 4: en = 1; chroma = 1; tap = 2; refine = 0; maxOne = 1; prev = 0; break;
    case 5: en = 1; chroma = 0; tap = 2; refine = 0; maxOne = 1; prev = 0; break;
    case 6: en = 1; chroma = 0; tap = 2; refine = 0; maxOne = 1; prev = 1; break;
    default: break;
    }
}
static void oracleSgRep(int level, int& en, int& chroma, int& s0, int& e0, int& i0, int& s1,
                        int& e1, int& i1, int& r0, int& r1) {
    en = 0; chroma = 0; s0 = e0 = i0 = s1 = e1 = i1 = r0 = r1 = 0;
    switch (level) {
    case 1: en = 1; chroma = 1; s0 = 0; e0 = 16; i0 = 1; s1 = 0; e1 = 16; i1 = 1; r0 = 1; r1 = 1; break;
    case 2: en = 1; chroma = 1; s0 = 0; e0 = 16; i0 = 1; s1 = 4; e1 = 5; i1 = 1; r0 = 1; r1 = 0; break;
    case 3: en = 1; chroma = 1; s0 = 0; e0 = 16; i0 = 8; s1 = 4; e1 = 5; i1 = 1; r0 = 1; r1 = 0; break;
    case 4: en = 1; chroma = 0; s0 = 0; e0 = 16; i0 = 8; s1 = 4; e1 = 5; i1 = 1; r0 = 1; r1 = 0; break;
    default: break;
    }
}

// The wn/sg level getters oracleWnDefault/oracleSgDefault already exist at the top of the file (independent transcriptions of :1365-1448) and are reused here.


// The init-resolution derivation the compositions perform (scs.c:120-139 thresholds, independent transcription - NOT the port's helper).
static int oracleInitRes(std::uint32_t w, std::uint32_t h) {
    const std::uint32_t sz = w * h;
    if (sz < 0x28500u) return 0;
    if (sz < 0x4CE00u) return 1;
    if (sz < 0xA1400u) return 2;
    if (sz < 0x16DA00u) return 3;
    if (sz < 0x535200u) return 4;
    if (sz < 0x140A000u) return 5;
    return 6;
}

// svt_aom_sig_deriv_multi_processes_default (:1988-2188), independent
// transcription.
static void oracleMultiProcDefault(const MultiProcScs& scs, MultiProcPpcs& pcs, MultiProcO& o) {
    const int mode = (int)pcs.encMode;
    o.initRes = oracleInitRes(scs.maxInitialInputLumaWidth, scs.maxInitialInputLumaHeight);
    o.gmLevel = pcs.isISlice ? 0 : (mode <= -1 ? 2 : (mode <= 4 ? 4 : 0));
    o.hmeFlag = 1; o.hmeL0 = 1;
    if (pcs.scClass5) {
        if (mode <= 2) { o.hmeL1 = 1; o.hmeL2 = 1; } else { o.hmeL1 = 1; o.hmeL2 = 0; }
    } else {
        o.hmeL1 = 1; o.hmeL2 = 0;
    }
    switch (pcs.tfHmeMeLevel) {
    case 0: o.tfHmeFlag = 1; o.tfHmeL0 = 1; o.tfHmeL1 = 1; o.tfHmeL2 = 1; break;
    case 1: case 2: o.tfHmeFlag = 1; o.tfHmeL0 = 1; o.tfHmeL1 = 1; o.tfHmeL2 = 0; break;
    case 3: case 4: o.tfHmeFlag = 1; o.tfHmeL0 = 1; o.tfHmeL1 = 0; o.tfHmeL2 = 0; break;
    default: break;
    }
    o.multiPassPd = 1;
    int ibc = 0;
    if (!scs.enableIntrabc) ibc = 0;
    else if (pcs.scClass5) {
        if (pcs.isISlice) {
            if (mode <= 3) ibc = 2;
            else if (mode <= 5) ibc = 3;
            else if (mode <= 8) ibc = 5;
            else if (mode <= 9) ibc = 6;
            else ibc = 0;
        } else ibc = 0;
    }
    o.intrabcLevel = ibc;
    o.allowIntrabc = oracleIntrabcEnabled(ibc);
    int pal = 0;
    if (pcs.scClass5) {
        if (mode <= 0) pal = pcs.isISlice ? 1 : 0;
        else if (mode == 1) pal = pcs.isISlice ? 2 : 0;
        else if (mode == 2) pal = pcs.isISlice ? 4 : 0;
        else if (mode <= 5) pal = pcs.isISlice ? 5 : 0;
        else if (mode <= 9) pal = pcs.isISlice ? 6 : 0;
        else if (mode == 10) pal = pcs.isISlice ? 8 : 0;
        else pal = 0;
    }
    o.paletteLevel = pal;
    o.scTools = (pcs.scClass5 && (pal || o.allowIntrabc)) ? 1 : 0;
    int cdef = 0;
    if (!scs.seqCdefLevel || o.allowIntrabc) cdef = 0;
    else if (scs.cdefLevel != -1) cdef = scs.cdefLevel;
    else if (mode <= -1) cdef = 1;
    else if (mode <= 2) cdef = 2;
    else if (mode <= 5) cdef = 5;
    else if (mode <= 7) cdef = pcs.isBase ? 5 : 6;
    else cdef = 7;
    o.cdefSearchLevel = cdef;
    int cr = 0;
    if (scs.fastDecode == 0 || pcs.gm.inputResolution <= 1) {
        if (mode <= 8) cr = 0;
        else if (mode <= 10) cr = 1;
        else cr = 2;
    } else if (scs.fastDecode == 1) cr = 1;
    else {
        if (mode <= 8) cr = 2; else cr = 1;
    }
    o.cdefReconLevel = cr;
    if (scs.seqEnableRestoration) {
        o.wnLevel = oracleWnDefault(mode, o.initRes, pcs.isNotLastLayer);
        o.sgLevel = oracleSgDefault(mode, o.initRes, scs.fastDecode);
    } else { o.wnLevel = 0; o.sgLevel = 0; }
    o.enableRestoration = (o.wnLevel > 0 || o.sgLevel > 0) ? 1 : 0;
    o.cdfMode = 1;
    if (scs.encoderBitDepth == 8) o.hbdMd = 0;
    else if (scs.hbdMds != -1) o.hbdMd = scs.hbdMds;
    else if (mode <= -1) o.hbdMd = 1;
    else if (mode <= 5) o.hbdMd = pcs.isBase ? 2 : 0;
    else o.hbdMd = pcs.isISlice ? 2 : 0;
    o.maxCanCount = (mode <= 1) ? 1225 : (mode == 2) ? 1000 : (mode == 3) ? 720 :
                    (mode == 4) ? 576 : (mode == 5) ? 369 : (mode == 6) ? 236 :
                    (mode <= 9) ? 190 : 80;
    o.useBest = mode <= 1 ? 0 : 1;
}

// svt_aom_sig_deriv_multi_processes_rtc (:2190-2367).
static void oracleMultiProcRtc(const MultiProcScs& scs, MultiProcPpcs& pcs, MultiProcO& o, int ftr) {
    const int mode = (int)pcs.encMode;
    o.initRes = oracleInitRes(scs.maxInitialInputLumaWidth, scs.maxInitialInputLumaHeight);
    o.gmLevel = pcs.isISlice ? 0 : (mode <= -1 ? 2 : (mode <= 4 ? 4 : 0));
    o.hmeFlag = 1; o.hmeL0 = 1;
    if (pcs.useFlatIpp) {
        if (mode <= 11 || (mode == 12 && pcs.picAvgVariance < 1000)) { o.hmeL1 = 1; o.hmeL2 = 0; }
        else { o.hmeL1 = 0; o.hmeL2 = 0; }
    } else { o.hmeL1 = 1; o.hmeL2 = 0; }
    switch (pcs.tfHmeMeLevel) {
    case 0: o.tfHmeFlag = 1; o.tfHmeL0 = 1; o.tfHmeL1 = 1; o.tfHmeL2 = 1; break;
    case 1: case 2: o.tfHmeFlag = 1; o.tfHmeL0 = 1; o.tfHmeL1 = 1; o.tfHmeL2 = 0; break;
    case 3: case 4: o.tfHmeFlag = 1; o.tfHmeL0 = 1; o.tfHmeL1 = 0; o.tfHmeL2 = 0; break;
    default: break;
    }
    o.multiPassPd = 1;
    o.intrabcLevel = 0;
    o.allowIntrabc = 0;
    int pal = 0;
    if (pcs.scClass5) {
        if (ftr) {
            if (mode <= 7) pal = pcs.isISlice ? 5 : 7;
            else if (mode == 8) pal = 7;
            else pal = 0;
        } else {
            if (mode <= 7) pal = pcs.isISlice ? 5 : 0;
            else if (mode == 8) pal = pcs.isISlice ? 7 : 0;
            else pal = 0;
        }
    }
    o.paletteLevel = pal;
    o.scTools = (pcs.scClass5 && (pal || o.allowIntrabc)) ? 1 : 0;
    int cdef = 0;
    if (!scs.seqCdefLevel || o.allowIntrabc) cdef = 0;
    else if (scs.cdefLevel != -1) cdef = scs.cdefLevel;
    else if (pcs.useFlatIpp) {
        if (mode <= 7) cdef = 5;
        else if (mode <= 11) cdef = (pcs.gm.inputResolution <= 1) ? 6 : (pcs.isISlice ? 5 : 8);
        else if (mode == 12 && pcs.picAvgVariance < 350)
            cdef = (pcs.gm.inputResolution <= 1) ? 6 : (pcs.isISlice ? 5 : 8);
        else cdef = pcs.isISlice ? 5 : 9;
    } else {
        if (mode <= 7) cdef = 5;
        else if (mode == 8) cdef = (pcs.gm.inputResolution <= 1) ? 6 : (pcs.isISlice ? 5 : 8);
        else cdef = pcs.isISlice ? 5 : 8;
    }
    o.cdefSearchLevel = cdef;
    o.cdefReconLevel = 0;
    if (scs.seqEnableRestoration) {
        o.wnLevel = 0;  // get_wn_filter_level_rtc = 0 for every resolution
        o.sgLevel = 0;  // get_sg_filter_level_rtc = 0 for every resolution/fd
    } else { o.wnLevel = 0; o.sgLevel = 0; }
    o.enableRestoration = 0;
    o.cdfMode = 1;
    if (scs.enableHbdModeDecision == -1) o.hbdMd = pcs.isISlice ? 2 : 0;
    else o.hbdMd = scs.enableHbdModeDecision;
    o.maxCanCount = (mode <= 7) ? 150 : (mode == 8) ? 75 : (mode <= 10) ? 65 :
                    (mode == 11) ? 15 : 10;
    o.useBest = 1;
}

// svt_aom_sig_deriv_multi_processes_allintra (:2369-2518).
static void oracleMultiProcAllIntra(const MultiProcScs& scs, MultiProcPpcs& pcs, MultiProcO& o) {
    const int mode = (int)pcs.encMode;
    o.initRes = oracleInitRes(scs.maxInitialInputLumaWidth, scs.maxInitialInputLumaHeight);
    o.multiPassPd = 1;  // :2376
    int ibc = 0;
    if (!scs.enableIntrabc) ibc = 0;
    else if (pcs.scClass5) {
        if (mode <= -1) ibc = 1;
        else if (mode == 0) ibc = 3;
        else if (mode == 1) ibc = 4;
        else if (mode == 2) ibc = 5;
        else if (mode == 3) ibc = 6;
        else if (mode == 4) ibc = 7;
        else ibc = 0;
    }
    o.intrabcLevel = ibc;
    o.allowIntrabc = oracleIntrabcEnabled(ibc);
    int pal = 0;
    if (pcs.scClass5) {
        if (mode <= 2) pal = 2;
        else if (mode == 3) pal = 3;
        else if (mode <= 5) pal = 4;
        else if (mode == 6) pal = 5;
        else if (mode == 7) pal = 7;
        else pal = 0;
    }
    o.paletteLevel = pal;
    o.scTools = (pal || o.allowIntrabc) ? 1 : 0;
    int cdef = 0;
    if (!scs.seqCdefLevel || o.allowIntrabc) cdef = 0;
    else if (scs.cdefLevel != -1) cdef = scs.cdefLevel;
    else if (scs.fastDecode == 0 || pcs.gm.inputResolution <= 1) {
        if (mode <= -1) cdef = 1;
        else if (mode == 0) cdef = 2;
        else if (mode <= 3) cdef = 3;
        else if (mode <= 5) cdef = 5;
        else if (mode == 6) cdef = 7;
        else cdef = 10;
    } else {
        if (mode <= 3) cdef = 3;
        else if (mode <= 5) cdef = 5;
        else if (mode <= 7) cdef = 7;
        else cdef = 0;
    }
    o.cdefSearchLevel = cdef;
    int cr = 0;
    if (scs.fastDecode == 0 || pcs.gm.inputResolution <= 1) {
        cr = mode <= 7 ? 0 : 1;
    } else if (scs.fastDecode == 1) {
        cr = mode <= 7 ? 1 : 3;
    } else cr = 3;
    o.cdefReconLevel = cr;
    if (scs.seqEnableRestoration) {
        // get_wn_filter_level_allintra / get_sg_filter_level_allintra
        o.wnLevel = mode <= 0 ? 3 : (mode <= 3 ? 3 : (mode <= 6 ? 4 : 0));
        o.sgLevel = mode <= -1 ? 1 : 0;
    } else { o.wnLevel = 0; o.sgLevel = 0; }
    o.enableRestoration = (o.wnLevel > 0 || o.sgLevel > 0) ? 1 : 0;
    o.cdfMode = 1;
    if (scs.enableHbdModeDecision == -1) o.hbdMd = mode <= -1 ? 1 : 2;
    else o.hbdMd = scs.enableHbdModeDecision;
    o.maxCanCount = (mode <= 1) ? 1225 : (mode == 2) ? 1000 : (mode == 3) ? 720 :
                    (mode == 4) ? 576 : (mode == 5) ? 369 : (mode == 6) ? 236 :
                    (mode <= 9) ? 190 : 80;
    o.useBest = -1;  // NOT written by the allintra variant (poison stands)
}

// The shared field comparison (the flow outputs + the ctrl-struct
// representatives). useBestPoison: the allintra variant leaves
// use_best_me_unipred_cand_only untouched - the uint8 poison stands.
// writesHmeAndGm: only the default/rtc variants touch the GM ctrls and
// the HME flags; the allintra variant writes NONE of them (:2369-2518),
// so the GM enabled uint8 keeps the poison (a safe uint8 read) and the
// eight HME bools stay unread (poisoned bools stay unread).
static void checkMultiProc(MultiProcPpcs& c, const MultiProcO& o,
                           bool base, bool nl, bool useBestPoison, bool writesHmeAndGm) {
    if (writesHmeAndGm) {
        CHECK(c.gm.gmCtrls.enabled == (uint8_t)(o.gmLevel != 0));
        CHECK(c.enableHmeFlag == (o.hmeFlag != 0));
        CHECK(c.enableHmeLevel0Flag == (o.hmeL0 != 0));
        CHECK(c.enableHmeLevel1Flag == (o.hmeL1 != 0));
        CHECK(c.enableHmeLevel2Flag == (o.hmeL2 != 0));
        CHECK(c.tfEnableHmeFlag == (o.tfHmeFlag != 0));
        CHECK(c.tfEnableHmeLevel0Flag == (o.tfHmeL0 != 0));
        CHECK(c.tfEnableHmeLevel1Flag == (o.tfHmeL1 != 0));
        CHECK(c.tfEnableHmeLevel2Flag == (o.tfHmeL2 != 0));
    } else {
        CHECK(c.gm.gmCtrls.enabled == 0xAA);
    }
    CHECK(c.multiPassPdLevel == o.multiPassPd);
    CHECK(c.allowIntrabc == (o.allowIntrabc != 0));
    {
        int hashBs, cand, dir; std::uint64_t thresh;
        oracleIntrabcRep(o.intrabcLevel, hashBs, cand, thresh, dir);
        CHECK(c.intrabcCtrls.enabled == (oracleIntrabcEnabled(o.intrabcLevel) != 0));
        if (o.intrabcLevel != 0) {
            CHECK(c.intrabcCtrls.maxBlockSizeHash == hashBs);
            CHECK(c.intrabcCtrls.maxCandPerBucket == cand);
            CHECK(c.intrabcCtrls.exhaustiveMeshThresh == thresh);
            CHECK(c.intrabcCtrls.searchDir == dir);
        }
    }
    CHECK(c.paletteLevel == o.paletteLevel);
    {
        int en, dcs, kcs, cr, kmitr;
        oraclePaletteRep(o.paletteLevel, en, dcs, kcs, cr, kmitr);
        CHECK(c.paletteCtrls.enabled == (uint8_t)en);
        if (o.paletteLevel != 0) {
            CHECK(c.paletteCtrls.dominantColorStep == dcs);
            CHECK(c.paletteCtrls.kmeanColorStep == kcs);
            CHECK(c.paletteCtrls.centroidRefinement == (cr != 0));
            CHECK(c.paletteCtrls.kMeansMaxItr == kmitr);
        }
    }
    CHECK(c.cdefLevel == o.cdefSearchLevel);
    {
        int en, sub, skipTh, searchBest, useRef;
        oracleCdefSearchRep(o.cdefSearchLevel, base, nl, en, sub, skipTh, searchBest, useRef);
        CHECK(c.cdefSearchCtrls.enabled == en);
        if (o.cdefSearchLevel != 0 && o.cdefSearchLevel != 10) {
            CHECK(c.cdefSearchCtrls.subsamplingFactor == sub);
            CHECK(c.cdefSearchCtrls.searchBestRefFs == searchBest);
        }
        if (o.cdefSearchLevel != 0 && o.cdefSearchLevel <= 10) {
            CHECK(c.cdefSearchCtrls.skipTh == skipTh);
            CHECK(c.cdefSearchCtrls.useReferenceCdefFs == useRef);
        }
    }
    {
        int zb, zl, pd;
        oracleCdefReconRep(o.cdefReconLevel, zb, zl, pd);
        CHECK(c.cdefReconCtrls.zeroFsCostBias == zb);
        CHECK(c.cdefReconCtrls.zeroFilterStrengthLvl == zl);
        CHECK(c.cdefReconCtrls.prevCdefDistTh == pd);
    }
    {
        int en, chroma, tap, refine, maxOne, prev;
        oracleWnRep(o.wnLevel, en, chroma, tap, refine, maxOne, prev);
        CHECK(c.wnFilterCtrls.enabled == (en != 0));
        if (o.wnLevel != 0) {
            CHECK(c.wnFilterCtrls.useChroma == (chroma != 0));
            CHECK(c.wnFilterCtrls.filterTapLvl == tap);
            CHECK(c.wnFilterCtrls.useRefinement == (refine != 0));
            CHECK(c.wnFilterCtrls.maxOneRefinementStep == (maxOne != 0));
            CHECK(c.wnFilterCtrls.usePrevFrameCoeffs == (prev != 0));
        }
    }
    {
        int en, chroma, s0, e0, i0, s1, e1, i1, r0, r1;
        oracleSgRep(o.sgLevel, en, chroma, s0, e0, i0, s1, e1, i1, r0, r1);
        CHECK(c.sgFilterCtrls.enabled == (en != 0));
        if (o.sgLevel != 0) {
            CHECK(c.sgFilterCtrls.useChroma == (chroma != 0));
            CHECK(c.sgFilterCtrls.startEp[0] == s0);
            CHECK(c.sgFilterCtrls.endEp[0] == e0);
            CHECK(c.sgFilterCtrls.epInc[0] == i0);
            CHECK(c.sgFilterCtrls.startEp[1] == s1);
            CHECK(c.sgFilterCtrls.endEp[1] == e1);
            CHECK(c.sgFilterCtrls.epInc[1] == i1);
            CHECK(c.sgFilterCtrls.refine[0] == r0);
            CHECK(c.sgFilterCtrls.refine[1] == r1);
        }
    }
    CHECK(c.enableRestoration == (o.enableRestoration != 0));
    CHECK(c.frameEndCdfUpdateMode == o.cdfMode);
    CHECK(c.hbdMd == o.hbdMd);
    CHECK(c.maxCanCount == o.maxCanCount);
    if (useBestPoison) CHECK(c.useBestMeUnipredCandOnly == 0xAA);
    else CHECK(c.useBestMeUnipredCandOnly == o.useBest);
    CHECK(c.allowScreenContentTools == (o.scTools != 0));
}

TEST_CASE("sig_deriv_multi_processes_default - the composition over every wall cell") {
    // :1988-2188. Axes: mode, isISlice, isBase, scClass5, enableIntrabc,
    // seqCdefLevel, fastDecode, inputResolution {360p, 720p},
    // seqEnableRestoration, encoderBitDepth {8, 10}, hbdMds {-1, 1},
    // isNotLastLayer, isNotHighestLayer. maxInitial dims fixed at
    // 640x360 -> initRes 360p (the derivation itself is helper-tested).
    for (int mode = -1; mode <= 13; ++mode)
    for (int isI = 0; isI <= 1; ++isI)
    for (int base = 0; base <= 1; ++base)
    for (int sc5 = 0; sc5 <= 1; ++sc5)
    for (int ibc = 0; ibc <= 1; ++ibc)
    for (int seqCdef = 0; seqCdef <= 1; ++seqCdef)
    for (int fd = 0; fd <= 2; ++fd)
    for (int res = 1; res <= 3; res += 2)
    for (int seqRest = 0; seqRest <= 1; ++seqRest)
    for (int bd = 8; bd <= 10; bd += 2)
    for (int hbdMds = -1; hbdMds <= 1; hbdMds += 2)
    for (int nll = 0; nll <= 1; ++nll)
    for (int nlh = 0; nlh <= 1; ++nlh) {
        MultiProcPpcs c;
        std::memset(&c, kPoison, sizeof(c));
        c.encMode = (EncMode)mode;
        c.isISlice = isI != 0;
        c.isBase = base != 0;
        c.scClass5 = sc5 != 0;
        c.isNotLastLayer = nll != 0;
        c.isNotHighestLayer = nlh != 0;
        c.useFlatIpp = false;
        c.picAvgVariance = 12345;
        c.tfHmeMeLevel = 2;
        c.gm.inputResolution = (uint8_t)res;
        MultiProcScs s{};
        s.fastDecode = (uint8_t)fd;
        s.enableIntrabc = ibc != 0;
        s.cdefLevel = -1;
        s.seqCdefLevel = (uint8_t)seqCdef;
        s.seqEnableRestoration = seqRest != 0;
        s.maxInitialInputLumaWidth = 640;
        s.maxInitialInputLumaHeight = 360;
        s.hbdMds = hbdMds;
        s.encoderBitDepth = (uint8_t)bd;
        s.enableHbdModeDecision = -1;
        sigDerivMultiProcessesDefault(c, s, 1);
        MultiProcO o;
        std::memset(&o, 0, sizeof(o));
        oracleMultiProcDefault(s, c, o);
        checkMultiProc(c, o, base != 0, nlh != 0, false, true);
    }
}

TEST_CASE("sig_deriv_multi_processes_rtc - the composition over every wall cell") {
    // :2190-2367. Axes: mode, isISlice, useFlatIpp, scClass5, seqCdefLevel,
    // inputResolution, picAvgVariance {500, 2000} (crosses both variance
    // thresholds), ftrRtcInterPalette {0, 1}, seqEnableRestoration,
    // fastDecode, enableHbdModeDecision {-1, 1}. intrabc_level = 0
    // unconditionally (:2248-2249).
    for (int mode = -1; mode <= 13; ++mode)
    for (int isI = 0; isI <= 1; ++isI)
    for (int flat = 0; flat <= 1; ++flat)
    for (int sc5 = 0; sc5 <= 1; ++sc5)
    for (int seqCdef = 0; seqCdef <= 1; ++seqCdef)
    for (int res = 1; res <= 3; res += 2)
    for (int varIdx = 0; varIdx <= 1; ++varIdx)
    for (int ftr = 0; ftr <= 1; ++ftr)
    for (int seqRest = 0; seqRest <= 1; ++seqRest)
    for (int fd = 0; fd <= 2; ++fd)
    for (int hbd = -1; hbd <= 1; hbd += 2) {
        MultiProcPpcs c;
        std::memset(&c, kPoison, sizeof(c));
        c.encMode = (EncMode)mode;
        c.isISlice = isI != 0;
        c.isBase = false;
        c.scClass5 = sc5 != 0;
        c.useFlatIpp = flat != 0;
        c.isNotLastLayer = true;
        c.isNotHighestLayer = true;
        c.picAvgVariance = varIdx == 0 ? 500u : 2000u;
        c.tfHmeMeLevel = 3;
        c.gm.inputResolution = (uint8_t)res;
        MultiProcScs s{};
        s.fastDecode = (uint8_t)fd;
        s.enableIntrabc = true;
        s.cdefLevel = -1;
        s.seqCdefLevel = (uint8_t)seqCdef;
        s.seqEnableRestoration = seqRest != 0;
        s.maxInitialInputLumaWidth = 640;
        s.maxInitialInputLumaHeight = 360;
        s.hbdMds = -1;
        s.encoderBitDepth = 10;
        s.enableHbdModeDecision = hbd;
        sigDerivMultiProcessesRtc(c, s, 1, ftr);
        MultiProcO o;
        std::memset(&o, 0, sizeof(o));
        oracleMultiProcRtc(s, c, o, ftr);
        checkMultiProc(c, o, false, true, false, true);
    }
}

TEST_CASE("sig_deriv_multi_processes_allintra - the composition over every wall cell") {
    // :2369-2518. Axes: mode, scClass5, enableIntrabc, seqCdefLevel,
    // fastDecode, inputResolution, seqEnableRestoration,
    // enableHbdModeDecision {-1, 1}. use_best_me_unipred_cand_only is NOT
    // written by this variant - the poison stands (checked in
    // checkMultiProc via useBestPoison).
    for (int mode = -1; mode <= 13; ++mode)
    for (int sc5 = 0; sc5 <= 1; ++sc5)
    for (int ibc = 0; ibc <= 1; ++ibc)
    for (int seqCdef = 0; seqCdef <= 1; ++seqCdef)
    for (int fd = 0; fd <= 2; ++fd)
    for (int res = 1; res <= 3; res += 2)
    for (int seqRest = 0; seqRest <= 1; ++seqRest)
    for (int hbd = -1; hbd <= 1; hbd += 2) {
        MultiProcPpcs c;
        std::memset(&c, kPoison, sizeof(c));
        c.encMode = (EncMode)mode;
        c.isISlice = true;
        c.isBase = false;
        c.scClass5 = sc5 != 0;
        c.useFlatIpp = false;
        c.isNotLastLayer = true;
        c.isNotHighestLayer = true;
        c.picAvgVariance = 12345;
        c.tfHmeMeLevel = 1;
        c.gm.inputResolution = (uint8_t)res;
        MultiProcScs s{};
        s.fastDecode = (uint8_t)fd;
        s.enableIntrabc = ibc != 0;
        s.cdefLevel = -1;
        s.seqCdefLevel = (uint8_t)seqCdef;
        s.seqEnableRestoration = seqRest != 0;
        s.maxInitialInputLumaWidth = 1280;
        s.maxInitialInputLumaHeight = 720;
        s.hbdMds = -1;
        s.encoderBitDepth = 8;
        s.enableHbdModeDecision = hbd;
        sigDerivMultiProcessesAllIntra(c, s);
        MultiProcO o;
        std::memset(&o, 0, sizeof(o));
        oracleMultiProcAllIntra(s, c, o);
        checkMultiProc(c, o, false, true, true, false);
    }
}


// ---------------------------------------------------------------------------
// Batch B rung 4: set_inter_comp_controls (:2627-2709), the level getters,
// the sg/restoration enable wrappers (:2711-2781), the pre-analysis
// derivations (:2788-2942).
// ---------------------------------------------------------------------------

TEST_CASE("set_inter_comp_controls - the five levels, whole-struct field checks") {
    // :2627-2709. Level 0 writes tot_comp_types + the seven do_* fields;
    // levels 1-4 write the full surface; the default arm writes nothing.
    // The poisoned bools stay unread at level 0 and default.
    for (int level = 0; level <= 5; ++level) {
        InterCompCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setInterCompCtrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.totCompTypes == kMdCompDist);
            CHECK(c.doMe == false);
            CHECK(c.doPme == false);
            CHECK(c.doNearestNearest == false);
            CHECK(c.doNearNear == false);
            CHECK(c.doNearestNearNew == false);
            CHECK(c.do3x3Bi == false);
            CHECK(c.doGlobal == false);
            CHECK(c.pred0ToPred1Mult == kPoison);
            CHECK(c.maxMvLength == 0xAAAA);
        } else if (level <= 4) {
            CHECK(c.totCompTypes == kMdCompTypes);
            CHECK(c.doMe == true);
            CHECK(c.doPme == (level <= 2));
            CHECK(c.doNearestNearest == true);
            CHECK(c.doNearNear == true);
            CHECK(c.doNearestNearNew == (level == 1));
            CHECK(c.do3x3Bi == (level == 1));
            CHECK(c.doGlobal == true);
            CHECK(c.skipOnRefInfo == (level >= 3));
            CHECK(c.useRate == (level == 1));
            CHECK(c.pred0ToPred1Mult == (level == 1 ? 0 : (level == 2 ? 1 : (level == 3 ? 1 : 4))));
            CHECK(c.maxMvLength == (level == 4 ? 32 : 0));
            CHECK(c.noSymDist == (level >= 3));
        } else {
            // :2705-2707 - assert(0), writes nothing.
            CHECK(c.totCompTypes == kPoison);
            CHECK(c.pred0ToPred1Mult == kPoison);
            CHECK(c.maxMvLength == 0xAAAA);
        }
    }
}

TEST_CASE("the level getter ladders - every preset enumerated") {
    // get_inter_compound_level :8839-8850; get_filter_intra_level_default
    // :8852-8863 / _rtc :8865-8869 / _allintra :8871-8882;
    // get_inter_intra_level :8884-8894; get_intra_mode_levels_allintra
    // :5429-5453; get_enable_me_8x8 :146-173 / _16x16 :175-179.
    for (int mode = -1; mode <= 13; ++mode) {
        int icl = mode <= 0 ? 3 : (mode <= 2 ? 4 : 0);
        CHECK(getInterCompoundLevel((EncMode)mode) == icl);
        int fil = mode <= 1 ? 1 : (mode <= 5 ? 2 : 0);
        CHECK(getFilterIntraLevelDefault((EncMode)mode) == fil);
        CHECK(getFilterIntraLevelRtc() == 0);
        int fia = mode <= 0 ? 1 : (mode <= 6 ? 2 : 0);
        CHECK(getFilterIntraLevelAllIntra((EncMode)mode) == fia);
        uint32_t il, dil;
        getIntraModeLevelsAllIntra((EncMode)mode, &il, &dil);
        int expIl = mode <= 4 ? 1 : (mode <= 5 ? 2 : (mode <= 6 ? 6 : (mode <= 8 ? 7 : 8)));
        CHECK(il == expIl);
        CHECK(dil == 0);
        CHECK(getEnableMe16x16((EncMode)mode) == 1);
        for (int trans = 0; trans <= 1; ++trans) {
            int iil = mode <= 1 ? 2 : (mode <= 8 ? (trans ? 2 : 0) : 0);
            CHECK(getInterIntraLevel((EncMode)mode, (uint8_t)trans) == iil);
        }
        for (int res = 0; res <= 6; ++res) {
            for (int rtc = 0; rtc <= 1; ++rtc) {
                int m8;
                if (rtc) m8 = mode <= 8 ? (res <= 3 ? 1 : 0) : 0;
                else m8 = mode <= 5 ? 1 : (mode <= 8 ? (res <= 3 ? 1 : 0) : 0);
                CHECK(getEnableMe8x8((EncMode)mode, (uint8_t)res, rtc != 0) == m8);
            }
        }
    }
}

TEST_CASE("the enable_sg / enable_restoration wrappers") {
    // :2711-2727 (sg) and :2733-2781 (restoration). The restoration
    // wrappers thread the DEFAULT config value and the two-iteration wn
    // loop over the ported getters.
    for (int mode = -1; mode <= 13; ++mode) {
        CHECK(getEnableSgAllIntra((EncMode)mode) == (mode <= -1 ? 1 : 0));
        CHECK(getEnableRestorationAllIntra((EncMode)mode, -1) == (mode <= 6 ? 1 : 0));
        CHECK(getEnableRestorationAllIntra((EncMode)mode, 0) == 0);
        CHECK(getEnableRestorationAllIntra((EncMode)mode, 1) == 1);
        for (int res = 1; res <= 3; res += 2) {
            for (int fd = 0; fd <= 2; ++fd) {
                // sg default: MR->1, <=M3->3, else 0; the fd gate zeroes
                // at fd > 0 && res > 360p.
                int sgd = mode <= -1 ? 1 : (mode <= 3 ? 3 : 0);
                if (fd && res > 1) sgd = 0;
                CHECK(getEnableSgDefault((EncMode)mode, (uint8_t)res, (uint8_t)fd) ==
                      (sgd > 0 ? 1 : 0));
                CHECK(getEnableSgRtc((uint8_t)res, (uint8_t)fd) == 0);
                // restoration default: wn loop picks notLast=true ->
                // <=M3:4, <=M8:5, else 0 (res < 8K here).
                int wn = mode <= 3 ? 4 : (mode <= 8 ? 5 : 0);
                CHECK(getEnableRestorationDefault((EncMode)mode, -1, (uint8_t)res, (uint8_t)fd) ==
                      ((wn > 0 || sgd > 0) ? 1 : 0));
                CHECK(getEnableRestorationDefault((EncMode)mode, 0, (uint8_t)res, (uint8_t)fd) == 0);
                CHECK(getEnableRestorationDefault((EncMode)mode, 1, (uint8_t)res, (uint8_t)fd) == 1);
                CHECK(getEnableRestorationRtc(-1, (uint8_t)res, (uint8_t)fd) == 0);
                CHECK(getEnableRestorationRtc(0, (uint8_t)res, (uint8_t)fd) == 0);
                CHECK(getEnableRestorationRtc(1, (uint8_t)res, (uint8_t)fd) == 1);
            }
        }
    }
}

TEST_CASE("sig_deriv_pre_analysis_pcs - the ME flags over every preset and resolution") {
    // :2788-2811. resolution derives from max_INPUT dims (the fixtures
    // 640x360 -> 360p, 1920x1080 -> 1080p); enable_me_8x8 is gated on
    // enable_me_16x16 (always 1); the eight HME flags all 1.
    const uint32_t dims[2][2] = {{640, 360}, {1920, 1080}};
    for (int mode = -1; mode <= 13; ++mode) {
        for (int d = 0; d <= 1; ++d) {
            for (int rtc = 0; rtc <= 1; ++rtc) {
                PreAnalysisPcs c;
                std::memset(&c, kPoison, sizeof(c));
                c.encMode = (EncMode)mode;
                c.maxInputLumaWidth = dims[d][0];
                c.maxInputLumaHeight = dims[d][1];
                c.rtcTune = rtc != 0;
                sigDerivPreAnalysisPcs(c);
                int res = d == 0 ? 1 : 4;  // oracleInitRes band
                int m8;
                if (rtc) m8 = mode <= 8 ? (res <= 3 ? 1 : 0) : 0;
                else m8 = mode <= 5 ? 1 : (mode <= 8 ? (res <= 3 ? 1 : 0) : 0);
                CHECK(c.enableMe16x16 == true);
                CHECK(c.enableMe8x8 == (m8 != 0));
                CHECK(c.enableHmeFlag == true);
                CHECK(c.enableHmeLevel0Flag == true);
                CHECK(c.enableHmeLevel1Flag == true);
                CHECK(c.enableHmeLevel2Flag == true);
                CHECK(c.tfEnableHmeFlag == true);
                CHECK(c.tfEnableHmeLevel0Flag == true);
                CHECK(c.tfEnableHmeLevel1Flag == true);
                CHECK(c.tfEnableHmeLevel2Flag == true);
            }
        }
    }
}

// svt_aom_sig_deriv_pre_analysis_scs (:2818-2918), independent transcription.
static void oraclePreAnalysisScs(const PreAnalysisScs& in, std::int8_t encMode,
                                 const PreAnalysisCfg& cfg, PreAnalysisScs& o) {
    const int mode = encMode;
    const bool allintra = in.allintra;
    const bool rtcTune = in.rtcTune;
    // :2821-2825
    if (cfg.superres) o.seqEnableSuperres = in.superresMode > 0;
    else o.seqEnableSuperres = false;
    // :2826-2837
    if (cfg.interIntra) {
        int ii = 0;
        for (int t = 0; t < 2; ++t) {
            if (ii) break;
            ii |= mode <= 1 ? 2 : (mode <= 8 ? (t ? 2 : 0) : 0);
        }
        o.seqEnableInterintraCompound = ii != 0;
    } else o.seqEnableInterintraCompound = false;
    // :2839-2850
    if (cfg.filterIntra) {
        int fi = allintra ? (mode <= 0 ? 1 : (mode <= 6 ? 2 : 0))
                 : rtcTune ? 0
                           : (mode <= 1 ? 1 : (mode <= 5 ? 2 : 0));
        o.seqFilterIntraLevel = fi != 0 ? 1 : 0;
    } else o.seqFilterIntraLevel = 0;
    // :2852-2863
    if (cfg.interCompound) {
        int cl = mode <= 0 ? 3 : (mode <= 2 ? 4 : 0);
        o.seqEnableJntComp = cl != 0 ? 1 : 0;
        o.seqEnableMaskedCompound = cl != 0 ? 1 : 0;
    } else {
        o.seqEnableJntComp = 0;
        o.seqEnableMaskedCompound = 0;
    }
    // :2864-2878
    if (allintra) {
        uint32_t il = mode <= 4 ? 1 : (mode <= 5 ? 2 : (mode <= 6 ? 6 : (mode <= 8 ? 7 : 8)));
        int a[10] = {0, 1, 2, 2, 3, 4, 4, 4, 4, 0};  // angular_pred_level :23
        o.seqEnableIntraEdgeFilter = (0 >= 1 || a[il] == 2 || a[il] == 3) ? 1 : 0;
    } else o.seqEnableIntraEdgeFilter = 1;
    // :2879-2909
    if (cfg.restoration) {
        // the fixtures derive init_res from the max_initial dims (640x360
        // -> 360p = 1 for every cell here)
        if (in.enableRestorationFiltering == -1) {
            if (allintra) o.seqEnableRestoration = mode <= 6 ? 1 : 0;
            else if (rtcTune) o.seqEnableRestoration = 0;
            else {
                int sgd = mode <= -1 ? 1 : (mode <= 3 ? 3 : 0);
                if (in.fastDecode && !(1 <= 1)) sgd = 0;
                int wn = mode <= 3 ? 4 : (mode <= 8 ? 5 : 0);
                o.seqEnableRestoration = (sgd > 0 || wn > 0) ? 1 : 0;
            }
        } else {
            int autoEn;
            if (allintra) autoEn = mode <= 6 ? 1 : 0;
            else if (rtcTune) autoEn = 0;
            else {
                int sgd = mode <= -1 ? 1 : (mode <= 3 ? 3 : 0);
                if (in.fastDecode && !(1 <= 1)) sgd = 0;
                int wn = mode <= 3 ? 4 : (mode <= 8 ? 5 : 0);
                autoEn = (sgd > 0 || wn > 0) ? 1 : 0;
            }
            o.seqEnableRestoration = (in.enableRestorationFiltering && autoEn) ? 1 : 0;
        }
    } else o.seqEnableRestoration = 0;
    // :2911-2915
    o.seqCdefLevel = in.cdefLevel == -1 ? 1 : (in.cdefLevel > 0 ? 1 : 0);
    // :2917
    o.seqEnableWarpedMotion = true;
}

TEST_CASE("sig_deriv_pre_analysis_scs - the composition over every wall cell") {
    // :2818-2918. The five CONFIG forks are threaded (pinned build: all
    // 1); the test enumerates the all-1 arm AND the all-0 arm (both
    // #if/#else shapes). superresMode {0 = SUPERRES_NONE, 1};
    // enable_restoration_filtering {DEFAULT(-1), 0, 1}; cdefLevel
    // {DEFAULT(-1), 0, 3}; fastDecode {0, 1}; the max_initial dims fixed
    // at 640x360 (init_res 360p).
    const PreAnalysisCfg cfgs[2] = {{1, 1, 1, 1, 1}, {0, 0, 0, 0, 0}};
    for (int mode = -1; mode <= 13; ++mode)
    for (int ai = 0; ai <= 1; ++ai)
    for (int rtc = 0; rtc <= 1; ++rtc)
    for (int fd = 0; fd <= 1; ++fd)
    for (int cfgIdx = 0; cfgIdx <= 1; ++cfgIdx)
    for (int srMode = 0; srMode <= 1; ++srMode)
    for (int enr = -1; enr <= 1; ++enr)
    for (int cdefIdx = 0; cdefIdx <= 2; ++cdefIdx) {
    int cdef = cdefIdx == 0 ? -1 : (cdefIdx == 1 ? 0 : 3);
        PreAnalysisScs c;
        std::memset(&c, kPoison, sizeof(c));
        c.allintra = ai != 0;
        c.rtcTune = rtc != 0;
        c.fastDecode = (uint8_t)fd;
        c.superresMode = srMode;
        c.enableRestorationFiltering = enr;
        c.cdefLevel = cdef;
        c.maxInitialInputLumaWidth = 640;
        c.maxInitialInputLumaHeight = 360;
        sigDerivPreAnalysisScs(c, (int8_t)mode, cfgs[cfgIdx]);
        PreAnalysisScs o;
        std::memset(&o, kPoison, sizeof(o));
        oraclePreAnalysisScs(c, (int8_t)mode, cfgs[cfgIdx], o);
        CHECK(c.seqEnableSuperres == o.seqEnableSuperres);
        CHECK(c.seqEnableInterintraCompound == o.seqEnableInterintraCompound);
        CHECK(c.seqFilterIntraLevel == o.seqFilterIntraLevel);
        CHECK(c.seqEnableJntComp == o.seqEnableJntComp);
        CHECK(c.seqEnableMaskedCompound == o.seqEnableMaskedCompound);
        CHECK(c.seqEnableIntraEdgeFilter == o.seqEnableIntraEdgeFilter);
        CHECK(c.seqEnableRestoration == o.seqEnableRestoration);
        CHECK(c.seqCdefLevel == o.seqCdefLevel);
        CHECK(c.seqEnableWarpedMotion == o.seqEnableWarpedMotion);
    }
}

TEST_CASE("is_ref_same_size - the null/size gating table") {
    // :2925-2941. false unless: B slice, ref_count > 0, the slot present,
    // and the ref picture dims equal the frame dims.
    for (int b = 0; b <= 1; ++b)
    for (int list = 0; list <= 1; ++list)
    for (int count = 0; count <= 1; ++count)
    for (int present = 0; present <= 1; ++present)
    for (int match = 0; match <= 1; ++match) {
        RefSameSizePcs c;
        std::memset(&c, kPoison, sizeof(c));
        c.isBSlice = b != 0;
        c.refListCountTry[0] = c.refListCountTry[1] = (uint8_t)count;
        c.refPresent[0][0] = c.refPresent[1][0] = present != 0;
        c.refPicWidth[0][0] = c.refPicWidth[1][0] = match ? 1920u : 640u;
        c.refPicHeight[0][0] = c.refPicHeight[1][0] = match ? 1080u : 360u;
        c.frameWidth = 1920;
        c.frameHeight = 1080;
        bool expect = b != 0 && count != 0 && present != 0 && match != 0;
        CHECK(isRefSameSize(c, (uint8_t)list, 0) == expect);
    }
}


// ---------------------------------------------------------------------------
// Batch B rung 5: set_obmc_controls (:2943-3007) + dimensions_require_8x8
// (:3012-3028).
// ---------------------------------------------------------------------------

TEST_CASE("set_obmc_controls - the seven levels, whole-struct field checks") {
    // :2943-3007. Level 0 writes ONLY enabled; levels 1-6 write all seven
    // ObmcControls fields; the DEFAULT arm is NOT assert(0) - it writes
    // enabled = 0 (:3003-3005) and leaves the rest poisoned.
    for (int level = 0; level <= 8; ++level) {
        ObmcCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setObmcCtrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.enabled == 0);
            CHECK(c.maxBlkSizeToRefine == kPoison);
            CHECK(c.maxBlkSize == kPoison);
        } else if (level <= 6) {
            CHECK(c.enabled == 1);
            CHECK(c.maxBlkSizeToRefine == (level == 1 ? 128 : (level == 2 ? 64 :
                                            (level <= 5 ? 32 : 16))));
            CHECK(c.maxBlkSize == (level <= 4 ? 128 : (level == 5 ? 32 : 16)));
            CHECK(c.refineLevel == (level <= 2 ? (level == 1 ? 0 : 1) :
                                    (level <= 4 ? 1 : 4)));
            CHECK(c.transFaceOff == (uint8_t)(level >= 4));
            CHECK(c.fpelSearchRange == (level <= 4 ? (level == 3 ? 8 : 16) : 8));
            CHECK(c.fpelSearchDiag == (level <= 2 ? 1 : (level == 3 ? 0 :
                                        (level == 4 ? 1 : 0))));
        } else {
            // :3003-3005 - the default arm writes enabled = 0 ONLY.
            CHECK(c.enabled == 0);
            CHECK(c.maxBlkSizeToRefine == kPoison);
            CHECK(c.fpelSearchDiag == kPoison);
        }
    }
}

// dimensions_require_8x8 (:3012-3028), independent transcription.
static bool oracleDimRequire8x8(uint16_t w, uint16_t h) {
    uint16_t lw = (uint16_t)(w % 64);
    uint16_t lh = (uint16_t)(h % 64);
    for (uint16_t half = 32; half >= 8; half = (uint16_t)(half >> 1)) {
        if ((lw == 0 || lw > half) && (lh == 0 || lh > half)) return false;
        lw = (uint16_t)(lw % half);
        lh = (uint16_t)(lh % half);
    }
    return true;
}

TEST_CASE("dimensions_require_8x8 - the dense aligned grid + the hand pins") {
    // :3012-3028. Assumes max block 64x64 (ISLICEs never use 128x128); NOT
    // NSQ aware - the (leftover > half) arms mean non-tileable dims like
    // 40x40 do NOT require 8x8 (pinned below).
    for (uint16_t w = 8; w <= 280; w += 8)
        for (uint16_t h = 8; h <= 280; h += 8)
            CHECK(dimensionsRequire8x8(w, h) == oracleDimRequire8x8(w, h));
    // Hand pins (independently derived from the loop arithmetic):
    CHECK(dimensionsRequire8x8(64, 64) == false);    // full 64 tiling
    CHECK(dimensionsRequire8x8(96, 96) == false);    // 32-tileable
    CHECK(dimensionsRequire8x8(80, 80) == false);    // 16-tileable
    CHECK(dimensionsRequire8x8(72, 72) == true);     // only 8 fits
    CHECK(dimensionsRequire8x8(40, 40) == false);    // leftover 40 > 32 arm
    CHECK(dimensionsRequire8x8(8, 8) == true);
    CHECK(dimensionsRequire8x8(136, 136) == true);   // 136%64=8 -> only 8 fits
    CHECK(dimensionsRequire8x8(64, 72) == true);     // h forces 8
    CHECK(dimensionsRequire8x8(280, 280) == false);  // 280%64=24: at half=16, 24>16 -> false
    CHECK(dimensionsRequire8x8(264, 264) == true);   // 264%64=8 -> only 8 fits
}


// ---------------------------------------------------------------------------
// set_depth_removal_level_controls (:3030-3343) + get_sb128_me_data
// (:63-115) - independent transcription. The oracle mirrors the
// reference's own prior-state reads: the disallow_below_* fields and
// (for level > 15) enabled are READ before write, so the caller supplies
// live prior state (the poison discipline does not apply to those four
// fields; the remaining wall inputs are still fully enumerated).
// ---------------------------------------------------------------------------
static int64_t oracleDrRdCost(int64_t rdmult, int64_t rate, int64_t dist) {
    // RDCOST (rd_cost.h:36-38): ROUND_POWER_OF_TWO(rate*rmult, 9) + dist*(1<<7).
    return (((rate * rdmult) + (1 << 8)) >> 9) + (dist * (1 << 7));
}

static const uint8_t oracleQuantizerToQindex[64] = {
    0,   4,   8,   12,  16,  20,  24,  28,  32,  36,  40,  44,  48,  52,  56,  60,  64,  68,  72,  76,  80,  84,
    88,  92,  96,  100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 148, 152, 156, 160, 164, 168, 172,
    176, 180, 184, 188, 192, 196, 200, 204, 208, 212, 216, 220, 224, 228, 232, 236, 240, 244, 249, 255};

static void oracleGetSb128MeData(const DepthRemovalPcs& pcs, const DepthRemovalCtx& ctx, uint32_t* d64,
                                 uint32_t* d32, uint32_t* d16, uint32_t* d8, uint32_t* var) {
    const uint32_t meSbSize = pcs.b64Size;
    const uint32_t mePW = (pcs.alignedWidth + meSbSize - 1) / meSbSize;
    const uint32_t mePH = (pcs.alignedHeight + meSbSize - 1) / meSbSize;
    const uint32_t meSbX = ctx.sbOriginX / meSbSize;
    const uint32_t meSbY = ctx.sbOriginY / meSbSize;
    const uint32_t addr = meSbX + meSbY * mePW;
    uint32_t dist64 = pcs.me64x64Distortion[addr];
    uint32_t dist32 = pcs.me32x32Distortion[addr];
    uint32_t dist16 = pcs.me16x16Distortion[addr];
    uint32_t dist8 = pcs.me8x8Distortion[addr];
    uint32_t var8 = pcs.me8x8CostVariance[addr];
    int count = 1;
    if (meSbX + 1 < mePW) {
        dist64 += pcs.me64x64Distortion[addr + 1];
        dist32 += pcs.me32x32Distortion[addr + 1];
        dist16 += pcs.me16x16Distortion[addr + 1];
        dist8 += pcs.me8x8Distortion[addr + 1];
        var8 = var8 > pcs.me8x8CostVariance[addr + 1] ? var8 : pcs.me8x8CostVariance[addr + 1];
        ++count;
    }
    if (meSbY + 1 < mePH) {
        dist64 += pcs.me64x64Distortion[addr + mePW];
        dist32 += pcs.me32x32Distortion[addr + mePW];
        dist16 += pcs.me16x16Distortion[addr + mePW];
        dist8 += pcs.me8x8Distortion[addr + mePW];
        var8 = var8 > pcs.me8x8CostVariance[addr + mePW] ? var8 : pcs.me8x8CostVariance[addr + mePW];
        ++count;
    }
    if (meSbX + 1 < mePW && meSbY + 1 < mePH) {
        dist64 += pcs.me64x64Distortion[addr + mePW + 1];
        dist32 += pcs.me32x32Distortion[addr + mePW + 1];
        dist16 += pcs.me16x16Distortion[addr + mePW + 1];
        dist8 += pcs.me8x8Distortion[addr + mePW + 1];
        var8 = var8 > pcs.me8x8CostVariance[addr + mePW + 1] ? var8 : pcs.me8x8CostVariance[addr + mePW + 1];
        ++count;
    }
    *d64 = dist64 / count;
    *d32 = dist32 / count;
    *d16 = dist16 / count;
    *d8 = dist8 / count;
    *var = var8;
}

static void oracleSetDepthRemoval(DepthRemovalCtrls& ctrls, const DepthRemovalPcs& pcs, DepthRemovalCtx& ctx,
                                  uint8_t level) {
    if (pcs.isISlice) {
        ctrls.enabled = 0;
        return;
    }
    const uint32_t fastLambda = ctx.fastLambdaMd[0];
    const uint32_t sbSize = 64 * 64;
    const uint64_t costThRate = (uint64_t)1 << 13;
    uint64_t mult4x4 = 0, mult16 = 0, mult32 = 0, mult64 = 0;
    int64_t dev16Th = kMaxSignedValue, dev32Th = kMaxSignedValue, dev32To8Th = kMaxSignedValue;
    int8_t qpScale = 0;
    if (pcs.modulateByQp) {
        const int diff = (int)ctx.sbQindex - (int)oracleQuantizerToQindex[pcs.pictureQp];
        if (diff <= -12)      level = (uint8_t)(level >= 4 ? level - 4 : 0);
        else if (diff <= -6)  level = (uint8_t)(level >= 3 ? level - 3 : 0);
        else if (diff <= -3)  level = (uint8_t)(level >= 2 ? level - 2 : 0);
        else if (diff < 0)    level = (uint8_t)(level >= 1 ? level - 1 : 0);
    }
    bool written = level <= 15;
    if (written) {
        ctrls.enabled = level == 0 ? 0 : 1;
        if (level >= 1) {
            mult4x4 = 64;
            switch (level) {
            case 1:  dev16Th = 0;  dev32Th = 0; qpScale = 1; break;
            case 2:  dev16Th = 10; dev32Th = 0; qpScale = 1; break;
            case 3:  dev16Th = 20; dev32Th = 0; qpScale = 1; break;
            case 4:  dev16Th = 30; dev32Th = 0; qpScale = 1; break;
            case 5:  mult16 = 6; mult32 = 6; dev16Th = 40;  dev32Th = 0; qpScale = 1; break;
            case 6:  mult16 = 6; mult32 = 6; dev16Th = 50;  dev32Th = 25; qpScale = 1; break;
            case 7:  mult16 = 6; mult32 = 6; dev16Th = 50;  dev32Th = 25; qpScale = 2; break;
            case 8:  mult16 = 16; mult32 = 8; mult64 = 8; dev16Th = 100; dev32Th = 50; qpScale = 3; break;
            case 9:  mult16 = 32; mult32 = 8; mult64 = 8; dev16Th = 100; dev32Th = 50; qpScale = 3; break;
            case 10: mult16 = 128; mult32 = 8; mult64 = 8; dev16Th = 200; dev32Th = 75; qpScale = 3; break;
            case 11: mult16 = 128; mult32 = 8; mult64 = 8; dev16Th = 250; dev32Th = 125; qpScale = 3; break;
            case 12: mult16 = 128; mult32 = 16; mult64 = 8; dev16Th = 250; dev32Th = 150; qpScale = 4; break;
            case 13: mult16 = 256; mult32 = 16; mult64 = 8; dev16Th = 250; dev32Th = 150; qpScale = 4; break;
            case 14: mult16 = 256; mult32 = 16; mult64 = 16; dev16Th = 250; dev32Th = 150; qpScale = 4; break;
            case 15: mult16 = 384; mult32 = 24; mult64 = 24; dev16Th = 300; dev32Th = 200; qpScale = 4; break;
            default: break;
            }
        }
    }
    if (ctrls.enabled) {
        const bool useRefInfo = pcs.rtc ? false : true;
        if (useRefInfo) {
            // is_ref_same_size, re-derived from the carrier surface
            // (:2925-2941: the B-slice gate, the count/null gate, then the
            // per-slot size match).
            const bool l0Avail = pcs.ref.isBSlice && pcs.ref.refPresent[0][0] &&
                pcs.ref.refPicWidth[0][0] == pcs.ref.frameWidth &&
                pcs.ref.refPicHeight[0][0] == pcs.ref.frameHeight;
            const bool l1Avail = pcs.ref.isBSlice && pcs.ref.refPresent[1][0] &&
                pcs.ref.refPicWidth[1][0] == pcs.ref.frameWidth &&
                pcs.ref.refPicHeight[1][0] == pcs.ref.frameHeight;
            if (l0Avail) {
                uint8_t minSq = (uint8_t)~0;
                const int d0 = (int)(pcs.pictureNumber - pcs.refPocL0);
                const int ad0 = d0 < 0 ? -d0 : d0;
                if (ad0 <= 1) minSq = pcs.refSbMinSqSizeL0;
                if (l1Avail) {
                    const int d1 = (int)(pcs.pictureNumber - pcs.refPocL1);
                    const int ad1 = d1 < 0 ? -d1 : d1;
                    if (ad1 <= 1) minSq = minSq < pcs.refSbMinSqSizeL1 ? minSq : pcs.refSbMinSqSizeL1;
                }
                if (minSq != (uint8_t)~0) {
                    if (minSq >= 64) {
                        dev32Th += 5;
                        dev16Th += 20;
                    } else if (minSq >= 32) {
                        dev16Th += 15;
                    }
                }
            }
        }
        uint32_t dist64, dist32, dist16, dist8, var8;
        if (pcs.superBlockSize == 64) {
            dist64 = pcs.me64x64Distortion[ctx.sbIndex];
            dist32 = pcs.me32x32Distortion[ctx.sbIndex];
            dist16 = pcs.me16x16Distortion[ctx.sbIndex];
            dist8 = pcs.me8x8Distortion[ctx.sbIndex];
            var8 = pcs.me8x8CostVariance[ctx.sbIndex];
        } else {
            oracleGetSb128MeData(pcs, ctx, &dist64, &dist32, &dist16, &dist8, &var8);
        }
        // The unsigned wrap for picture_qp > 53 is the reference's own
        // arithmetic (63 - (qp + 10) in uint32).
        var8 /= (63u - (pcs.pictureQp + 10u)) > 1u ? (63u - (pcs.pictureQp + 10u)) : 1u;
        if (var8 < kLow8x8DistVarTh) {
            dev16Th = dev16Th << 2;
        } else if (var8 < kHigh8x8DistVarTh) {
            dev16Th = dev16Th << 1;
            dev32Th = dev32Th >> 1;
        } else {
            dev16Th = 0;
            dev32Th = 0;
        }
        const uint32_t qpWeight = ((63u - (pcs.pictureQp + 10u)) > 1u ? (63u - (pcs.pictureQp + 10u)) : 1u) >> 4;
        const uint32_t qpW = qpWeight > 1u ? qpWeight : 1u;
        dev16Th *= (int64_t)(qpW * (uint32_t)qpScale);
        dev32Th *= (int64_t)(qpW * (uint32_t)qpScale);
        dev32To8Th = (dev32Th * ((1 << 2) + 1)) >> 2;
        const uint64_t th16 = mult16 ? (uint64_t)oracleDrRdCost(fastLambda, (int64_t)costThRate,
                                                                (int64_t)((sbSize >> 3) * mult16)) : 0;
        const uint64_t th32 = mult32 ? (uint64_t)oracleDrRdCost(fastLambda, (int64_t)costThRate,
                                                                (int64_t)((sbSize >> 3) * mult32)) : 0;
        const uint64_t th64 = mult64 ? (uint64_t)oracleDrRdCost(fastLambda, (int64_t)costThRate,
                                                                (int64_t)((sbSize >> 3) * mult64)) : 0;
        const uint64_t cost64 = (uint64_t)oracleDrRdCost(fastLambda, 0, dist64);
        const uint64_t cost32 = (uint64_t)oracleDrRdCost(fastLambda, 0, dist32);
        const uint64_t cost16 = (uint64_t)oracleDrRdCost(fastLambda, 0, dist16);
        const uint64_t cost8 = (uint64_t)oracleDrRdCost(fastLambda, 0, dist8);
        const int64_t dev32To16 = (int64_t)(((int64_t)(cost32 > 1u ? cost32 : 1u) -
                                             (int64_t)(cost16 > 1u ? cost16 : 1u)) * 1000) /
            (int64_t)(cost16 > 1u ? cost16 : 1u);
        const int64_t dev32To8 = (int64_t)(((int64_t)(cost32 > 1u ? cost32 : 1u) -
                                            (int64_t)(cost8 > 1u ? cost8 : 1u)) * 1000) /
            (int64_t)(cost8 > 1u ? cost8 : 1u);
        const int64_t dev16To8 = (int64_t)(((int64_t)(cost16 > 1u ? cost16 : 1u) -
                                            (int64_t)(cost8 > 1u ? cost8 : 1u)) * 1000) /
            (int64_t)(cost8 > 1u ? cost8 : 1u);
        const uint8_t w = pcs.sbGeomWidth, h = pcs.sbGeomHeight;
        ctrls.disallowBelow64x64 =
            (uint8_t)(((w % 64) == 0 || (w % 64) > 32) && ((h % 64) == 0 || (h % 64) > 32)
                          ? (ctrls.disallowBelow64x64 || cost64 < th64) : 0);
        ctrls.disallowBelow32x32 =
            (uint8_t)(((w % 32) == 0 || (w % 32) > 16) && ((h % 32) == 0 || (h % 32) > 16)
                          ? (ctrls.disallowBelow32x32 || cost32 < th32 ||
                             (dev32To16 < dev32Th && dev32To8 < dev32To8Th)) : 0);
        ctrls.disallowBelow16x16 =
            (uint8_t)((!dimensionsRequire8x8(w, h))
                          ? (ctrls.disallowBelow16x16 || cost16 < th16 || dev16To8 < dev16Th) : 0);
        if (!ctx.disallow4x4 && mult4x4) {
            const uint64_t th4x4 = (uint64_t)oracleDrRdCost(fastLambda, (int64_t)costThRate,
                                                            (int64_t)((sbSize >> 1) * mult4x4));
            if (cost8 < th4x4 && var8 < kLow8x8DistVarTh) ctx.disallow4x4 = 1;
        }
    }
}

TEST_CASE("get_depth_removal_level_params - every level 0-15 plus the no-default arm") {
    // :3068-3224. The switch has NO default: level > 15 leaves enabled
    // unwritten (enabledWritten = false) and the locals at their pre-switch
    // init values (multipliers 0, dev ths MAX_SIGNED_VALUE, qp_scale 0).
    DepthRemovalLevelParams p;
    getDepthRemovalLevelParams(0, p);
    CHECK(p.enabledWritten);
    CHECK(p.enabled == 0);
    CHECK(p.disallow4x4CostThMultiplier == 0);
    getDepthRemovalLevelParams(1, p);
    CHECK(p.enabledWritten);
    CHECK(p.enabled == 1);
    CHECK(p.disallow4x4CostThMultiplier == 64);
    CHECK(p.disallowBelow16x16CostThMultiplier == 0);
    CHECK(p.disallowBelow32x32CostThMultiplier == 0);
    CHECK(p.disallowBelow64x64CostThMultiplier == 0);
    CHECK(p.dev16x16To8x8Th == 0);
    CHECK(p.dev32x32To16x16Th == 0);
    CHECK(p.qpScaleFactor == 1);
    struct Row { int lvl; uint64_t m16, m32, m64; int64_t d16, d32; int qp; };
    static const Row rows[] = {
        {2, 0, 0, 0, 10, 0, 1},    {3, 0, 0, 0, 20, 0, 1},    {4, 0, 0, 0, 30, 0, 1},
        {5, 6, 6, 0, 40, 0, 1},    {6, 6, 6, 0, 50, 25, 1},   {7, 6, 6, 0, 50, 25, 2},
        {8, 16, 8, 8, 100, 50, 3}, {9, 32, 8, 8, 100, 50, 3}, {10, 128, 8, 8, 200, 75, 3},
        {11, 128, 8, 8, 250, 125, 3}, {12, 128, 16, 8, 250, 150, 4},
        {13, 256, 16, 8, 250, 150, 4}, {14, 256, 16, 16, 250, 150, 4},
        {15, 384, 24, 24, 300, 200, 4},
    };
    for (const Row& r : rows) {
        getDepthRemovalLevelParams((uint8_t)r.lvl, p);
        CHECK(p.enabledWritten);
        CHECK(p.enabled == 1);
        CHECK(p.disallow4x4CostThMultiplier == 64);
        CHECK(p.disallowBelow16x16CostThMultiplier == r.m16);
        CHECK(p.disallowBelow32x32CostThMultiplier == r.m32);
        CHECK(p.disallowBelow64x64CostThMultiplier == r.m64);
        CHECK(p.dev16x16To8x8Th == r.d16);
        CHECK(p.dev32x32To16x16Th == r.d32);
        CHECK(p.qpScaleFactor == r.qp);
    }
    // Level 16 (and anything > 15): the reference switch has no default -
    // the port reports enabledWritten = false and hands back the INIT
    // locals (multipliers 0, dev ths MAX_SIGNED_VALUE, qp_scale 0).
    getDepthRemovalLevelParams(16, p);
    CHECK(!p.enabledWritten);
    CHECK(p.disallow4x4CostThMultiplier == 0);
    CHECK(p.disallowBelow16x16CostThMultiplier == 0);
    CHECK(p.disallowBelow32x32CostThMultiplier == 0);
    CHECK(p.disallowBelow64x64CostThMultiplier == 0);
    CHECK(p.dev16x16To8x8Th == kMaxSignedValue);
    CHECK(p.dev32x32To16x16Th == kMaxSignedValue);
    CHECK(p.qpScaleFactor == 0);
}

// The wall grid for the composition: every cell x level 0-15 (+ the >15
// arm under both prior-enabled states) against the oracle.
struct DrCell {
    bool modulateByQp;
    uint32_t pictureQp;
    uint8_t sbQindex;      // the diff = sbQindex - q2i[qp] bin
    bool rtc;
    uint8_t refAvail;      // bit0 = l0, bit1 = l1
    uint64_t refPocDelta;  // picture_number - ref_poc (abs'd by the port)
    uint8_t sbMinSqL0;
    uint8_t sbMinSqL1;
    uint32_t superBlockSize;  // 64 or 128
    uint32_t sbOriginX, sbOriginY;
    uint8_t sbGeomW, sbGeomH;
    uint32_t d64[4], d32[4], d16[4], d8[4], var[4];  // the me arrays
    uint8_t priorBelow64, priorBelow32, priorBelow16;
};

static DepthRemovalPcs drCellPcs(const DrCell& cell, bool isISlice, uint64_t pictureNumber) {
    DepthRemovalPcs pcs;
    std::memset(&pcs, 0, sizeof(pcs));
    pcs.isISlice = isISlice;
    pcs.pictureNumber = pictureNumber;
    pcs.rtc = cell.rtc;
    pcs.superBlockSize = cell.superBlockSize;
    pcs.b64Size = 64;
    pcs.alignedWidth = 128;   // me_pic 2x2 for the SB128 arm
    pcs.alignedHeight = 128;
    pcs.sbGeomWidth = cell.sbGeomW;
    pcs.sbGeomHeight = cell.sbGeomH;
    pcs.modulateByQp = cell.modulateByQp;
    pcs.pictureQp = cell.pictureQp;
    pcs.ref.isBSlice = true;
    pcs.ref.refListCountTry[0] = 1;
    pcs.ref.refListCountTry[1] = 1;
    pcs.ref.refPresent[0][0] = (cell.refAvail & 1) != 0;
    pcs.ref.refPresent[1][0] = (cell.refAvail & 2) != 0;
    pcs.ref.refPicWidth[0][0] = 64;
    pcs.ref.refPicHeight[0][0] = 64;
    pcs.ref.refPicWidth[1][0] = 64;
    pcs.ref.refPicHeight[1][0] = 64;
    pcs.ref.frameWidth = 64;
    pcs.ref.frameHeight = 64;
    pcs.refPocL0 = cell.refAvail & 1 ? pictureNumber - cell.refPocDelta : 0;
    pcs.refPocL1 = cell.refAvail & 2 ? pictureNumber - cell.refPocDelta : 0;
    pcs.refSbMinSqSizeL0 = cell.sbMinSqL0;
    pcs.refSbMinSqSizeL1 = cell.sbMinSqL1;
    pcs.me64x64Distortion = cell.d64;
    pcs.me32x32Distortion = cell.d32;
    pcs.me16x16Distortion = cell.d16;
    pcs.me8x8Distortion = cell.d8;
    pcs.me8x8CostVariance = cell.var;
    return pcs;
}

TEST_CASE("set_depth_removal_level_controls - the wall grid x every level, vs the oracle") {
    static const DrCell cells[] = {
        // 0: plain non-rtc, no refs, SB64, flat geometry, tiny dists
        {false, 30, 120, false, 0, 0, 255, 255, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
         0, 0, 0},
        // 1: refs both avail, poc delta 0, min_sq 64 (the +20/+5 bump arm)
        {false, 30, 120, false, 3, 0, 64, 64, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
         0, 0, 0},
        // 2: min_sq 32 (the +15-only arm)
        {false, 30, 120, false, 3, 0, 32, 32, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
         0, 0, 0},
        // 3: min_sq 16 (no bump), poc delta 5 (no bump either)
        {false, 30, 120, false, 3, 5, 16, 16, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
         0, 0, 0},
        // 4: l0 only
        {false, 30, 120, false, 1, 0, 64, 32, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
         0, 0, 0},
        // 5: l1 only (l0 unavailable -> the whole ref arm is skipped)
        {false, 30, 120, false, 2, 0, 64, 64, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
         0, 0, 0},
        // 6: rtc (use_ref_info = false)
        {false, 30, 120, true, 3, 0, 64, 64, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
         0, 0, 0},
        // 7: SB128 at the (0,0) corner (both +1 arms live)
        {false, 30, 120, false, 0, 0, 255, 255, 128, 0, 0, 128, 128,
         {100, 200, 300, 400}, {80, 160, 240, 320}, {60, 120, 180, 240},
         {40, 80, 120, 160}, {10, 20, 30, 40}, 0, 0, 0},
        // 8: SB128 at the (64,0) edge (x+1 arm dead)
        {false, 30, 120, false, 0, 0, 255, 255, 128, 64, 0, 128, 128,
         {100, 200, 300, 400}, {80, 160, 240, 320}, {60, 120, 180, 240},
         {40, 80, 120, 160}, {10, 20, 30, 40}, 0, 0, 0},
        // 9: SB128 at the (0,64) edge (y+1 arm dead)
        {false, 30, 120, false, 0, 0, 255, 255, 128, 0, 64, 128, 128,
         {100, 200, 300, 400}, {80, 160, 240, 320}, {60, 120, 180, 240},
         {40, 80, 120, 160}, {10, 20, 30, 40}, 0, 0, 0},
        // 10: SB128 at the (64,64) far corner (all +1 arms dead)
        {false, 30, 120, false, 0, 0, 255, 255, 128, 64, 64, 128, 128,
         {100, 200, 300, 400}, {80, 160, 240, 320}, {60, 120, 180, 240},
         {40, 80, 120, 160}, {10, 20, 30, 40}, 0, 0, 0},
        // 11: variance above HIGH after the qp division (the zero arm)
        {false, 30, 120, false, 0, 0, 255, 255, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {60000, 0, 0, 0},
         0, 0, 0},
        // 12: variance in the mid band (<<1 / >>1 arm)
        {false, 30, 120, false, 0, 0, 255, 255, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {30000, 0, 0, 0},
         0, 0, 0},
        // 13: the qp > 53 unsigned-wrap divisor arm
        {false, 63, 255, false, 0, 0, 255, 255, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
         0, 0, 0},
        // 14: non-tileable geometry (the :0 arms) + prior flags set
        {false, 30, 120, false, 0, 0, 255, 255, 64, 0, 0, 100, 100,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
         1, 1, 1},
        // 15: the delta-qp modulation, diff <= -12 bin (sbQindex 0, qp 30
        // -> q2i[30] = 120 -> diff -120): level drops 4
        {true, 30, 0, false, 0, 0, 255, 255, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
         0, 0, 0},
        // 16: the -2 bin (115 - 120 = -5: <= -3, not <= -6)
        {true, 30, 115, false, 0, 0, 255, 255, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
         0, 0, 0},
        // 17: the -3 bin (113 - 120 = -7: <= -6, not <= -12)
        {true, 30, 113, false, 0, 0, 255, 255, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
         0, 0, 0},
        // 18: diff < 0 bin (sbQindex 119 -> -1)
        {true, 30, 119, false, 0, 0, 255, 255, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
         0, 0, 0},
        // 19: diff == 0 (no modulation)
        {true, 30, 120, false, 0, 0, 255, 255, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
         0, 0, 0},
        // 20: diff > 0 (no modulation)
        {true, 30, 124, false, 0, 0, 255, 255, 64, 0, 0, 64, 64,
         {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
         0, 0, 0},
        // 21: costly dists (the thresholds bite), prior flags set
        {false, 30, 120, false, 3, 0, 64, 64, 64, 0, 0, 64, 64,
         {900000, 0, 0, 0}, {800000, 0, 0, 0}, {700000, 0, 0, 0}, {600000, 0, 0, 0},
         {10, 0, 0, 0}, 1, 1, 0},
    };
    int checked = 0;
    for (const DrCell& cell : cells) {
        for (int level = 0; level <= 15; ++level) {
            DepthRemovalPcs pcs = drCellPcs(cell, false, 1000);
            DepthRemovalCtx ctx;
            std::memset(&ctx, 0, sizeof(ctx));
            ctx.fastLambdaMd[0] = 500;
            ctx.fastLambdaMd[1] = 777;
            ctx.sbIndex = 0;
            ctx.sbQindex = cell.sbQindex;
            DepthRemovalCtrls c;
            std::memset(&c, 0xA5, sizeof(c));
            c.disallowBelow64x64 = cell.priorBelow64;
            c.disallowBelow32x32 = cell.priorBelow32;
            c.disallowBelow16x16 = cell.priorBelow16;
            DepthRemovalCtrls o = c;
            DepthRemovalCtx ctxO = ctx;
            setDepthRemovalLevelCtrls(c, pcs, ctx, (uint8_t)level);
            oracleSetDepthRemoval(o, pcs, ctxO, (uint8_t)level);
            CHECK(c.enabled == o.enabled);
            CHECK(c.disallowBelow64x64 == o.disallowBelow64x64);
            CHECK(c.disallowBelow32x32 == o.disallowBelow32x32);
            CHECK(c.disallowBelow16x16 == o.disallowBelow16x16);
            CHECK(ctx.disallow4x4 == ctxO.disallow4x4);
            ++checked;
        }
        // The >15 arm under BOTH prior-enabled states (the switch's
        // missing default: enabled keeps prior state).
        for (uint8_t priorEnabled = 0; priorEnabled <= 1; ++priorEnabled) {
            DepthRemovalPcs pcs = drCellPcs(cell, false, 1000);
            DepthRemovalCtx ctx;
            std::memset(&ctx, 0, sizeof(ctx));
            ctx.fastLambdaMd[0] = 500;
            DepthRemovalCtrls c;
            std::memset(&c, 0xA5, sizeof(c));
            c.enabled = priorEnabled;
            c.disallowBelow64x64 = cell.priorBelow64;
            c.disallowBelow32x32 = cell.priorBelow32;
            c.disallowBelow16x16 = cell.priorBelow16;
            DepthRemovalCtrls o = c;
            DepthRemovalCtx ctxO = ctx;
            setDepthRemovalLevelCtrls(c, pcs, ctx, 16);
            oracleSetDepthRemoval(o, pcs, ctxO, 16);
            CHECK(c.enabled == o.enabled);
            CHECK(c.disallowBelow64x64 == o.disallowBelow64x64);
            CHECK(c.disallowBelow32x32 == o.disallowBelow32x32);
            CHECK(c.disallowBelow16x16 == o.disallowBelow16x16);
            CHECK(ctx.disallow4x4 == ctxO.disallow4x4);
            ++checked;
        }
    }
    CHECK(checked == 22 * 18);
}

TEST_CASE("set_depth_removal_level_controls - the I-slice and level-0 gates, poison pins") {
    // I-slice: enabled = 0 and NOTHING else is touched (:3033-3034).
    DrCell cell = {false, 30, 120, false, 3, 0, 64, 64, 64, 0, 0, 64, 64,
                   {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
                   0, 0, 0};
    DepthRemovalPcs pcs = drCellPcs(cell, true, 1000);
    DepthRemovalCtx ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.fastLambdaMd[0] = 500;
    DepthRemovalCtrls c;
    std::memset(&c, 0xA5, sizeof(c));
    setDepthRemovalLevelCtrls(c, pcs, ctx, 9);
    CHECK(c.enabled == 0);
    CHECK(c.disallowBelow64x64 == 0xA5);
    CHECK(c.disallowBelow32x32 == 0xA5);
    CHECK(c.disallowBelow16x16 == 0xA5);
    CHECK(ctx.disallow4x4 == 0);
    // Level 0 (non-I-slice): enabled = 0, the rest untouched (:3069-3071).
    DepthRemovalPcs pcs2 = drCellPcs(cell, false, 1000);
    DepthRemovalCtrls c2;
    std::memset(&c2, 0xA5, sizeof(c2));
    setDepthRemovalLevelCtrls(c2, pcs2, ctx, 0);
    CHECK(c2.enabled == 0);
    CHECK(c2.disallowBelow64x64 == 0xA5);
    CHECK(c2.disallowBelow32x32 == 0xA5);
    CHECK(c2.disallowBelow16x16 == 0xA5);
    // Level > 15 with prior enabled = 0: nothing written at all (the
    // enabled byte must be set to the KNOWN 0 - a poison 0xA5 would read
    // truthy and RUN the enabled block, writing the three flags).
    DepthRemovalCtrls c3;
    std::memset(&c3, 0xA5, sizeof(c3));
    c3.enabled = 0;
    setDepthRemovalLevelCtrls(c3, pcs2, ctx, 16);
    CHECK(c3.enabled == 0);
    CHECK(c3.disallowBelow64x64 == 0xA5);
    CHECK(c3.disallowBelow32x32 == 0xA5);
    CHECK(c3.disallowBelow16x16 == 0xA5);
}

TEST_CASE("set_depth_removal_level_controls - hand pins over the modulation and 4x4 arm") {
    // Hand-derived cell: level 15, no refs, SB64, qp 30, flat geometry,
    // dist8 tiny (cost8 < the 4x4 th) + variance below LOW after the qp
    // division -> ctx.disallow4x4 flips to 1; prior disallow4x4 = 1 keeps
    // it (the !ctx->disallow_4x4 gate :3333).
    DrCell cell = {false, 30, 120, false, 0, 0, 255, 255, 64, 0, 0, 64, 64,
                   {1000, 0, 0, 0}, {800, 0, 0, 0}, {600, 0, 0, 0}, {4, 0, 0, 0}, {10, 0, 0, 0},
                   0, 0, 0};
    DepthRemovalPcs pcs = drCellPcs(cell, false, 1000);
    DepthRemovalCtx ctx;
    std::memset(&ctx, 0, sizeof(ctx));
    ctx.fastLambdaMd[0] = 500;
    DepthRemovalCtrls c;
    std::memset(&c, 0xA5, sizeof(c));
    setDepthRemovalLevelCtrls(c, pcs, ctx, 15);
    CHECK(c.enabled == 1);
    CHECK(ctx.disallow4x4 == 1);
    // The gate: prior disallow4x4 = 1 -> no re-evaluation, stays 1.
    DepthRemovalCtx ctx2;
    std::memset(&ctx2, 0, sizeof(ctx2));
    ctx2.fastLambdaMd[0] = 500;
    ctx2.disallow4x4 = 1;
    DepthRemovalCtrls c2;
    std::memset(&c2, 0xA5, sizeof(c2));
    setDepthRemovalLevelCtrls(c2, pcs, ctx2, 15);
    CHECK(ctx2.disallow4x4 == 1);
    // The modulation: level 15 with diff <= -12 drops to 11 (same
    // multipliers, dev16 250): the dists here make the two levels
    // indistinguishable in the flags, so the pin is the ref-less
    // identical-threshold pair - instead pin the LEVEL BOUND directly:
    // level 2 with diff <= -12 -> level 0 (enabled = 0).
    DrCell cell2 = {true, 30, 0, false, 0, 0, 255, 255, 64, 0, 0, 64, 64,
                    {100, 0, 0, 0}, {80, 0, 0, 0}, {60, 0, 0, 0}, {40, 0, 0, 0}, {10, 0, 0, 0},
                    0, 0, 0};
    DepthRemovalPcs pcs2 = drCellPcs(cell2, false, 1000);
    DepthRemovalCtrls c3;
    std::memset(&c3, 0xA5, sizeof(c3));
    setDepthRemovalLevelCtrls(c3, pcs2, ctx, 2);
    CHECK(c3.enabled == 0);
    // ...and level 5 with the same diff drops to level 1 (enabled stays 1;
    // MAX(0, 5 - 4) = 1). Level 3 would clamp to 0 (MAX(0, 3 - 4)).
    DepthRemovalCtrls c4;
    std::memset(&c4, 0xA5, sizeof(c4));
    setDepthRemovalLevelCtrls(c4, pcs2, ctx, 5);
    CHECK(c4.enabled == 1);
}

// ---------------------------------------------------------------------------
// Batch C rung 2: the MD search setters (:3348-3631).
// ---------------------------------------------------------------------------

TEST_CASE("md_nsq_motion_search_controls - the three levels, whole-struct") {
    // :3348-3373. Level 0 writes ONLY enabled; 1-2 write all five fields;
    // the default arm assert(0)s and writes nothing.
    for (int level = 0; level <= 3; ++level) {
        MdNsqMotionSearchCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setMdNsqMotionSearchCtrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.enabled == 0);
            CHECK(c.distType == kPoison);
            CHECK(c.fullPelSearchWidth == kPoison);
        } else if (level <= 2) {
            CHECK(c.enabled == 1);
            CHECK(c.distType == VAR);
            CHECK(c.fullPelSearchWidth == (level == 1 ? 32 : 16));
            CHECK(c.fullPelSearchHeight == (level == 1 ? 16 : 8));
            CHECK(c.enablePsad == 1);
        } else {
            CHECK(c.enabled == kPoison);
            CHECK(c.distType == kPoison);
            CHECK(c.enablePsad == kPoison);
        }
    }
}

TEST_CASE("md_pme_search_controls - the six levels, whole-struct") {
    // :3375-3451. Level 0 writes ONLY enabled; 1-5 write all eleven
    // fields; the default arm assert(0)s and writes nothing. The
    // MAX/MIN_SIGNED_VALUE sentinels are pinned literally (utility.h:164-
    // 165: MIN = ~0 - ((signed)(~0u >> 1)) = INT32_MIN).
    for (int level = 0; level <= 6; ++level) {
        MdPmeCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setMdPmeCtrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.enabled == 0);
            CHECK(c.distType == kPoison);
            CHECK(c.saQWeight == kPoison);
        } else if (level <= 5) {
            CHECK(c.enabled == 1);
            CHECK(c.distType == (level >= 4 ? SAD : VAR));
            CHECK(c.fullPelSearchWidth == (level >= 4 ? 7 : 9));
            CHECK(c.fullPelSearchHeight == (level == 3 ? 7 : (level >= 4 ? 5 : 9)));
            CHECK(c.earlyCheckMvThMultiplier == (level == 5 ? 64 : kMinSignedValue));
            CHECK(c.preFpPmeToMeCostTh == (level >= 4 ? 25 : kMaxSignedValue));
            CHECK(c.preFpPmeToMeMvTh == (level >= 3 ? 16 : kMinSignedValue));
            CHECK(c.postFpPmeToMeCostTh == (level >= 4 ? 50 : (level == 3 ? 50 : kMaxSignedValue)));
            CHECK(c.postFpPmeToMeMvTh == (level == 4 || level == 5 ? 32 : kMinSignedValue));
            CHECK(c.enablePsad == (level >= 4 ? 1 : 0));
            CHECK(c.saQWeight == (level == 1 ? 0 : 1));
        } else {
            CHECK(c.enabled == kPoison);
            CHECK(c.distType == kPoison);
            CHECK(c.preFpPmeToMeCostTh == (int32_t)((uint32_t)kPoison * 0x01010101u));
        }
    }
}

TEST_CASE("set_subres_controls - the three levels and the prior-state default arm") {
    // :3453-3476. step is written by cases 0-2; the post-switch
    // odd_to_even_deviation_th is written from the (possibly prior) step
    // on EVERY call - so the default arm (assert(0), step untouched)
    // still writes the TH from the PRIOR step. The poison step 0xA5 != 0
    // pins the else arm.
    for (int level = 0; level <= 2; ++level) {
        SubresCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setSubresCtrls(c, (uint8_t)level);
        CHECK(c.step == level);
        CHECK(c.oddToEvenDeviationTh == (level == 0 ? 0 : 5));
    }
    SubresCtrls c;
    std::memset(&c, kPoison, sizeof(c));
    setSubresCtrls(c, 3);  // default arm: step untouched (0xA5), TH = 5
    CHECK(c.step == kPoison);
    CHECK(c.oddToEvenDeviationTh == 5);
    // A prior step of 0 (e.g. level 0 first, then the default arm) keeps 0.
    SubresCtrls c2;
    std::memset(&c2, kPoison, sizeof(c2));
    setSubresCtrls(c2, 0);
    setSubresCtrls(c2, 7);
    CHECK(c2.step == 0);
    CHECK(c2.oddToEvenDeviationTh == 0);
}

TEST_CASE("set_pf_controls - the four shapes, whole-struct") {
    // :3478-3503. The #if !CONFIG_ENABLE_TX_PF_N2 assert is compiled out
    // in the pinned build (TX_PF_N2 = 1), so pf_level 2 (N2_SHAPE) is a
    // plain write here; the default arm assert(0)s and writes nothing.
    for (int level = 0; level <= 4; ++level) {
        PfCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setPfCtrls(c, (uint8_t)level);
        if (level <= 3) {
            CHECK(c.pfShape == (level == 0 ? ONLY_DC_SHAPE
                                             : (level == 1 ? DEFAULT_SHAPE
                                                           : (level == 2 ? N2_SHAPE : N4_SHAPE))));
        } else {
            CHECK(c.pfShape == kPoison);
        }
    }
}

TEST_CASE("md_sq_motion_search_controls - the five levels, whole-struct") {
    // :3508-3628. Level 0 writes ONLY enabled; 1-4 write all 22 fields and
    // differ ONLY in the two sparse multipliers (500/400/300/100); the
    // default arm assert(0)s and writes nothing.
    for (int level = 0; level <= 5; ++level) {
        MdSqMotionSearchCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setMdSqMotionSearchCtrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.enabled == 0);
            CHECK(c.distType == kPoison);
            CHECK(c.enablePsad == kPoison);
        } else if (level <= 4) {
            CHECK(c.enabled == 1);
            CHECK(c.distType == SAD);
            CHECK(c.pameDistortionTh == 10);
            CHECK(c.sprsLev0Enabled == 1);
            CHECK(c.sprsLev0Step == 4);
            CHECK(c.sprsLev0W == 15);
            CHECK(c.sprsLev0H == 15);
            CHECK(c.maxSprsLev0W == 150);
            CHECK(c.maxSprsLev0H == 150);
            CHECK(c.sprsLev0Multiplier == (level == 1 ? 500 : (level == 2 ? 400 :
                                            (level == 3 ? 300 : 100))));
            CHECK(c.sprsLev1Enabled == 1);
            CHECK(c.sprsLev1Step == 2);
            CHECK(c.sprsLev1W == 4);
            CHECK(c.sprsLev1H == 4);
            CHECK(c.maxSprsLev1W == 50);
            CHECK(c.maxSprsLev1H == 50);
            CHECK(c.sprsLev1Multiplier == c.sprsLev0Multiplier);
            CHECK(c.sprsLev2Enabled == 1);
            CHECK(c.sprsLev2Step == 1);
            CHECK(c.sprsLev2W == 3);
            CHECK(c.sprsLev2H == 3);
            CHECK(c.enablePsad == 1);
        } else {
            CHECK(c.enabled == kPoison);
            // the fixture poison byte pair 0xAAAA reads as -21846 in the
            // int16 field
            CHECK(c.sprsLev0Multiplier == (int16_t)-21846);
            CHECK(c.enablePsad == kPoison);
        }
    }
}

// ---------------------------------------------------------------------------
// Batch C rung 3: the subpel/rdoq/shaving setters (:3633-3962).
// ---------------------------------------------------------------------------

TEST_CASE("md_subpel_me_controls - the eleven levels, whole-struct") {
    // :3633-3774. Level 0 writes ONLY enabled; 1-6 write all thirteen
    // fields; 7-10 write the SUBPEL_FIXED_STAGE_SEARCH surface ONLY (the
    // six unused fields stay untouched per the reference's own comment
    // :3730-3732); the default arm assert(0)s and writes nothing.
    for (int level = 0; level <= 11; ++level) {
        MdSubPelSearchCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setMdSubpelMeCtrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.enabled == 0);
            CHECK(c.subpelSearchType == kPoison);
            CHECK(c.biasFp == (int32_t)((uint32_t)kPoison * 0x01010101u));
        } else if (level <= 6) {
            CHECK(c.enabled == 1);
            CHECK(c.subpelSearchType == (level == 1 ? USE_8_TAPS : USE_4_TAPS));
            CHECK(c.subpelItersPerStep == (level == 6 ? 1 : 2));
            CHECK(c.maxPrecision == (level == 6 ? QUARTER_PEL : EIGHTH_PEL));
            CHECK(c.subpelSearchMethod == (level <= 3 ? SUBPEL_TREE : SUBPEL_TREE_PRUNED));
            CHECK(c.predVarianceTh == 0);
            CHECK(c.absThMult == 0);
            CHECK(c.roundDevTh == kMaxSignedValue);
            CHECK(c.skipDiagRefinement == (level == 6 ? 3 : 0));
            CHECK(c.minBlkSz == (level == 1 ? 0 : 4));
            CHECK(c.mvpTh == (level == 1 ? 0 : (level == 6 ? 12 : 18)));
            CHECK(c.hpMvTh == (level == 1 ? kMaxSignedValue : 32));
            CHECK(c.biasFp == (level == 1 || level == 2 ? 0 : (level <= 4 ? 104 : 110)));
        } else if (level <= 10) {
            CHECK(c.enabled == 1);
            CHECK(c.maxPrecision == QUARTER_PEL);
            CHECK(c.subpelSearchMethod == SUBPEL_FIXED_STAGE_SEARCH);
            CHECK(c.predVarianceTh == (level <= 8 ? 0 : 100));
            CHECK(c.absThMult == (level == 7 ? 0 : (level == 8 ? 10 : (level == 9 ? 20 : 25))));
            CHECK(c.minBlkSz == 4);
            CHECK(c.biasFp == 110);
            // the six unused fields stay untouched
            CHECK(c.subpelSearchType == kPoison);
            CHECK(c.subpelItersPerStep == (int32_t)((uint32_t)kPoison * 0x01010101u));
            CHECK(c.roundDevTh == (int32_t)((uint32_t)kPoison * 0x01010101u));
            CHECK(c.skipDiagRefinement == kPoison);
            CHECK(c.mvpTh == kPoison);
            CHECK(c.hpMvTh == (int32_t)((uint32_t)kPoison * 0x01010101u));
        } else {
            CHECK(c.enabled == kPoison);
            CHECK(c.maxPrecision == kPoison);
            CHECK(c.subpelSearchMethod == kPoison);
        }
    }
}

TEST_CASE("md_subpel_pme_controls - the five levels, whole-struct") {
    // :3779-3846. Level 0 writes ONLY enabled; 1-4 write all thirteen
    // fields; the default arm assert(0)s and writes nothing.
    for (int level = 0; level <= 5; ++level) {
        MdSubPelSearchCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setMdSubpelPmeCtrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.enabled == 0);
            CHECK(c.subpelSearchType == kPoison);
        } else if (level <= 4) {
            CHECK(c.enabled == 1);
            CHECK(c.subpelSearchType == USE_8_TAPS);
            CHECK(c.subpelItersPerStep == 2);
            CHECK(c.maxPrecision == (level == 4 ? HALF_PEL : EIGHTH_PEL));
            CHECK(c.subpelSearchMethod == (level == 1 ? SUBPEL_TREE : SUBPEL_TREE_PRUNED));
            CHECK(c.predVarianceTh == 0);
            CHECK(c.absThMult == 0);
            CHECK(c.roundDevTh == kMaxSignedValue);
            CHECK(c.minBlkSz == 0);
            CHECK(c.mvpTh == 0);
            CHECK(c.hpMvTh == 0);
            CHECK(c.biasFp == (level == 1 ? 0 : (level == 2 ? 104 : 110)));
        } else {
            CHECK(c.enabled == kPoison);
            CHECK(c.biasFp == (int32_t)((uint32_t)kPoison * 0x01010101u));
        }
    }
}

TEST_CASE("set_rdoq_controls - the six levels, whole-struct") {
    // :3851-3906. Level 0 writes ONLY enabled; case 1 does NOT write
    // cut_off_denum (poison survives - pinned); 2-5 write all seven
    // fields; the default arm assert(0)s and writes nothing.
    for (int level = 0; level <= 6; ++level) {
        RdoqCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setRdoqCtrls(c, (uint8_t)level);
        // the uint16 poison pair the fixture's 0xAA memset leaves:
        // 0xAA * 0x0101 = 0xAAAA = 43690
        const uint16_t kPoison16 = (uint16_t)(kPoison * 0x0101);
        if (level == 0) {
            CHECK(c.enabled == 0);
            CHECK(c.cutOffNum == kPoison16);
            CHECK(c.cutOffDenum == kPoison16);
        } else if (level == 1) {
            CHECK(c.enabled == 1);
            CHECK(c.cutOffNum == 0);
            CHECK(c.cutOffDenum == kPoison16);  // the reference leaves it
            CHECK(c.skipUv == 0);
            CHECK(c.dctDctOnly == 0);
            CHECK(c.eobTh == 0xFF);
            CHECK(c.eobFastTh == 0xFF);
        } else if (level <= 5) {
            CHECK(c.enabled == 1);
            CHECK(c.cutOffNum == (level == 2 ? 80 : 60));
            CHECK(c.cutOffDenum == 100);
            CHECK(c.skipUv == (level >= 3 ? 1 : 0));
            CHECK(c.dctDctOnly == (level >= 3 ? 1 : 0));
            CHECK(c.eobTh == (level == 5 ? 85 : 0xFF));
            CHECK(c.eobFastTh == (level == 2 || level == 3 ? 0xFF : (level == 4 ? 30 : 0)));
        } else {
            CHECK(c.enabled == kPoison);
            CHECK(c.eobTh == kPoison);
            CHECK(c.cutOffDenum == kPoison16);
        }
    }
}

TEST_CASE("set_coeff_shaving_controls - the three levels, whole-struct") {
    // :3911-3935. Level 0 writes ONLY enabled; 1-2 write all four fields
    // and differ ONLY in rd_zero_strength (32 vs 0); the default arm
    // assert(0)s and writes nothing.
    for (int level = 0; level <= 3; ++level) {
        CoeffShavingCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setCoeffShavingCtrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.enabled == 0);
            CHECK(c.levelThreshold == (int32_t)((uint32_t)kPoison * 0x01010101u));
        } else if (level <= 2) {
            CHECK(c.enabled == 1);
            CHECK(c.levelThreshold == 1);
            CHECK(c.zeroGapThreshold == 8);
            CHECK(c.rdZeroStrength == (level == 1 ? 32 : 0));
        } else {
            CHECK(c.enabled == kPoison);
            CHECK(c.zeroGapThreshold == (int32_t)((uint32_t)kPoison * 0x01010101u));
        }
    }
}

// ---------------------------------------------------------------------------
// Batch C rung 4: the txt + interpolation-search setters (:3964-4162).
// ---------------------------------------------------------------------------

TEST_CASE("set_txt_controls - the eleven levels, whole-struct") {
    // :3964-4137. All eleven cases write ALL eleven fields (level 0 writes
    // enabled = 0 plus the ten controls); the default arm assert(0)s and
    // writes nothing. The group ladders 8-10 walk inter 4/3/2/1 + intra
    // 5/4/3/2 down to the (2,1)/(3,2) floor.
    for (int level = 0; level <= 11; ++level) {
        TxtCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setTxtCtrls(c, (uint8_t)level);
        if (level <= 10) {
            CHECK(c.enabled == (level == 0 ? 0 : 1));
            // level 0 writes the four groups as 1 (:3971-3975); 1-5 all
            // MAX (:3986-4047); the 6-10 ladders walk down (:4059-4125).
            CHECK(c.txtGroupInterLt16x16 ==
                  (level == 0 ? 1 : (level <= 6 ? kMaxTxTypeGroup :
                                     (level == 7 ? 5 : (level == 8 ? 4 :
                                                        (level == 9 ? 3 : 2))))));
            CHECK(c.txtGroupInterGtEq16x16 ==
                  (level == 0 ? 1 : (level <= 5 ? kMaxTxTypeGroup :
                                     (level == 6 ? 5 : (level == 7 ? 5 :
                                                        (level == 8 ? 3 : (level == 9 ? 2 : 1)))))));
            CHECK(c.txtGroupIntraLt16x16 ==
                  (level == 0 ? 1 : (level <= 7 ? kMaxTxTypeGroup :
                                     (level == 8 ? 5 : (level == 9 ? 4 : 3)))));
            CHECK(c.txtGroupIntraGtEq16x16 ==
                  (level == 0 ? 1 : (level <= 7 ? kMaxTxTypeGroup :
                                     (level == 8 ? 4 : (level == 9 ? 3 : 2)))));
            CHECK(c.earlyExitDistTh == 0);
            CHECK(c.earlyExitCoeffTh == 0);
            // :3978/:3992 (0) :4006 (20) :4021 (15) :4036+ (10)
            CHECK(c.satdEarlyExitThIntra ==
                  (level <= 1 ? 0 : (level == 2 ? 20 : (level == 3 ? 15 : 10))));
            CHECK(c.satdEarlyExitThInter ==
                  (level == 0 ? 0 : (level == 1 ? 0 : (level == 2 ? 20 :
                                                       (level == 3 ? 15 : (level == 4 ? 10 : 5))))));
            CHECK(c.satdThQWeight == (level <= 1 ? 0 : 1));
            CHECK(c.txtRateCostTh ==
                  (level <= 1 ? 0 : (level <= 4 ? 250 : (level <= 8 ? 100 : (level == 9 ? 65 : 50)))));
        } else {
            CHECK(c.enabled == kPoison);
            CHECK(c.txtRateCostTh == (uint16_t)0xAAAA);
        }
    }
}

TEST_CASE("set_interpolation_search_level_ctrls - the five levels") {
    // :4139-4162. The ifs level IS the whole struct; the default arm
    // assert(0)s and writes nothing.
    for (int level = 0; level <= 5; ++level) {
        InterpolationSearchCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setInterpolationSearchLevelCtrls(c, (uint8_t)level);
        if (level <= 4) {
            CHECK(c.level == level);  // IfsLevel is the identity map
        } else {
            CHECK(c.level == kPoison);
        }
    }
}

// ---------------------------------------------------------------------------
// Batch C rung 5: set_cand_reduction_ctrls (:4164-4394).
// ---------------------------------------------------------------------------

// The four-arm dc_only_th fold, independent transcription (:4241-4244 etc).
static uint16_t oracleDcOnlyTh(bool isLpd1, bool useFlatIpp, bool notLast, int level) {
    if (!isLpd1) return (uint16_t)(notLast ? 10 : 200);
    if (useFlatIpp) return (uint16_t)(level >= 5 ? 800 : (level == 4 ? 600 : 200));
    return (uint16_t)(notLast ? 30 : (level >= 4 ? 600 : 200));
}

// The reduce_unipred_candidates arm (:4340-4345), independent transcription.
static uint8_t oracleReduceUnipred(bool notLast, bool l0, bool l1, uint8_t refSkipPerc,
                                   uint32_t var8, uint32_t dist64, uint32_t qp) {
    return (uint8_t)(!notLast || ((l0 && l1 && refSkipPerc > 35) &&
                                  var8 < (500 * qp) && dist64 < (500 * qp)) ? 2 : 1);
}

TEST_CASE("set_cand_reduction_ctrls - the seven levels x the wall grid, vs the oracle") {
    // :4164-4394. is_lpd1 (pd1_level > -1), is_not_last_layer,
    // use_flat_ipp and the tail gate all thread through the carriers; the
    // level switch + the :4389-4393 tail gate are the full behavior.
    struct Wall { bool lpd1; bool flatIpp; bool notLast; uint8_t l0c, l1c; bool bestOnly; };
    static const Wall walls[] = {
        {false, false, false, 1, 1, true},   // REGULAR_PD1, single-ref best-only
        {false, false, false, 2, 1, true},   // the tail gate zeroes lpd1_mvp_best_me_list
        {true,  false, false, 1, 1, true},   // LPD1, tail gate satisfied
        {true,  false, true,  1, 1, false},  // LPD1, not-last-layer, gate unsatisfied
        {true,  true,  false, 1, 1, true},   // flat-ipp arm of the dc_only_th fold
        {false, true,  true,  1, 1, false},
    };
    struct Skip { uint8_t l0, l1, perc; uint32_t var8, dist64, qp; };
    static const Skip skips[] = {
        {0, 0, 0, 1000, 1000, 30},      // l0/l1 not skip -> arm 1
        {1, 1, 40, 10000, 10000, 30},   // skip + cheap -> arm 2
        {1, 1, 40, 20000, 20000, 30},   // skip + costly -> arm 1
        {1, 0, 40, 10000, 10000, 30},   // l0 only -> arm 1
        {0, 0, 0, 0, 0, 0},             // qp 0: 500*0 = 0, var < 0 false
    };
    for (const Wall& w : walls) {
        for (const Skip& s : skips) {
            for (int level = 0; level <= 7; ++level) {
                CandReductionCtrls c;
                std::memset(&c, kPoison, sizeof(c));
                CandReductionPcs pcs;
                pcs.refList0CountTry = w.l0c;
                pcs.refList1CountTry = w.l1c;
                pcs.useBestMeUnipredCandOnly = w.bestOnly;
                CandReductionCtx ctx;
                ctx.pd1Level = (int8_t)(w.lpd1 ? LPD1_LVL_1 : REGULAR_PD1);
                ctx.isNotLastLayer = w.notLast;
                ctx.rtc = w.flatIpp;
                ctx.hierarchicalLevels = w.flatIpp ? 0 : 1;
                setCandReductionCtrls(c, (uint8_t)level, pcs, ctx, s.qp, s.var8, s.dist64,
                                      s.l0, s.l1, s.perc);
                const uint8_t expectMvp =
                    (uint8_t)(((w.l0c == 1 && w.l1c == 1 && w.bestOnly) && level >= 5) ? 1 : 0);
                if (level <= 6) {
                    CHECK(c.reduceFilterIntra == (level == 0 ? 0 : 1));
                    CHECK(c.redundantCandCtrls.scoreTh == (level >= 4 ? 8 : 0));
                    CHECK(c.redundantCandCtrls.magTh ==
                          (level >= 4 ? 64 : (int32_t)((uint32_t)kPoison * 0x01010101u)));
                    CHECK(c.useNeighbouringModeCtrls.enabled == (level >= 2 ? 1 : 0));
                    CHECK(c.nearCountCtrls.enabled == 1);
                    CHECK(c.nearCountCtrls.nearCount ==
                          (level <= 2 ? 3 : (level == 3 ? 1 : (level <= 5 ? 1 : 0))));
                    CHECK(c.nearCountCtrls.nearNearCount == (level <= 3 ? 3 : 1));
                    CHECK(c.lpd1MvpBestMeList == expectMvp);
                    CHECK(c.candEliminationCtrls.enabled == (level >= 2 ? 1u : 0u));
                    if (level >= 2) {
                        CHECK(c.candEliminationCtrls.dcOnlyTh ==
                              oracleDcOnlyTh(w.lpd1, w.flatIpp, w.notLast, level));
                        CHECK(c.candEliminationCtrls.skipDcTh ==
                              (level >= 5 ? (uint16_t)(w.notLast ? 5 : 15) : 0));
                    }
                    CHECK(c.reduceUnipredCandidates ==
                          (level <= 2 ? 0 : (level <= 4 ? 1 :
                               oracleReduceUnipred(w.notLast, s.l0 == 1, s.l1 == 1,
                                                   s.perc, s.var8, s.dist64, s.qp))));
                } else {
                    // default arm: the switch writes nothing, but the tail
                    // gate STILL runs (and can write lpd1_mvp_best_me_list).
                    CHECK(c.reduceFilterIntra == kPoison);
                    CHECK(c.candEliminationCtrls.enabled == (uint32_t)((uint32_t)kPoison * 0x01010101u));
                    CHECK(c.lpd1MvpBestMeList ==
                          (uint8_t)((w.l0c == 1 && w.l1c == 1 && w.bestOnly) ? kPoison : 0));
                }
            }
        }
    }
}

TEST_CASE("set_cand_reduction_ctrls - the tail gate over every count pair") {
    // :4389-4393: lpd1_mvp_best_me_list survives ONLY when
    // ref_list0_count_try == 1 && ref_list1_count_try == 1 &&
    // use_best_me_unipred_cand_only - enumerated over both counts x the
    // flag, on a level that sets the flag (5) AND on the default arm (7).
    for (int l0 = 0; l0 <= 2; ++l0)
        for (int l1 = 0; l1 <= 2; ++l1)
            for (int best = 0; best <= 1; ++best)
                for (int level : {5, 7}) {
                    CandReductionCtrls c;
                    std::memset(&c, kPoison, sizeof(c));
                    CandReductionPcs pcs;
                    pcs.refList0CountTry = (uint8_t)l0;
                    pcs.refList1CountTry = (uint8_t)l1;
                    pcs.useBestMeUnipredCandOnly = best == 1;
                    CandReductionCtx ctx;
                    ctx.pd1Level = LPD1_LVL_0;
                    ctx.isNotLastLayer = false;
                    ctx.rtc = false;
                    ctx.hierarchicalLevels = 1;
                    setCandReductionCtrls(c, (uint8_t)level, pcs, ctx, 30, 0, 0, 0, 0, 0);
                    const bool gate = l0 == 1 && l1 == 1 && best == 1;
                    CHECK(c.lpd1MvpBestMeList ==
                          (uint8_t)(gate ? (level == 5 ? 1 : kPoison) : 0));
                }
}

// ---------------------------------------------------------------------------
// EMC2 rung 1: the SB128 walls (:63-:145), the :3937-:3963 gap
// (set_sq_txs_ctrls), set_chroma_controls (:4396-:4463), set_wm_controls
// (:4467-:4521), the nic level getters (:4521-:4586) and
// svt_aom_set_nic_controls (:4588-:4966).
// ---------------------------------------------------------------------------

// get_sb128_me_data, independent transcription (enc_mode_config.c:63-115).
struct OracleSb128 {
    uint32_t meSbSize;
    uint32_t picW;
    uint32_t picH;
    const uint32_t* d64;
    const uint32_t* d32;
    const uint32_t* d16;
    const uint32_t* d8;
    const uint32_t* cv;
};
static void oracleSb128MeData(const OracleSb128& o, uint32_t ox, uint32_t oy, uint32_t* r64,
                              uint32_t* r32, uint32_t* r16, uint32_t* r8, uint32_t* rcv) {
    const uint32_t w = (o.picW + o.meSbSize - 1) / o.meSbSize;
    const uint32_t h = (o.picH + o.meSbSize - 1) / o.meSbSize;
    const uint32_t x = ox / o.meSbSize;
    const uint32_t y = oy / o.meSbSize;
    const uint32_t a = x + y * w;
    uint32_t v64 = o.d64[a], v32 = o.d32[a], v16 = o.d16[a], v8 = o.d8[a], vc = o.cv[a];
    int count = 1;
    if (x + 1 < w) {
        v64 += o.d64[a + 1];
        v32 += o.d32[a + 1];
        v16 += o.d16[a + 1];
        v8 += o.d8[a + 1];
        vc = oracleMaxU32(vc, o.cv[a + 1]);
        count++;
    }
    if (y + 1 < h) {
        v64 += o.d64[a + w];
        v32 += o.d32[a + w];
        v16 += o.d16[a + w];
        v8 += o.d8[a + w];
        vc = oracleMaxU32(vc, o.cv[a + w]);
        count++;
    }
    if (x + 1 < w && y + 1 < h) {
        v64 += o.d64[a + w + 1];
        v32 += o.d32[a + w + 1];
        v16 += o.d16[a + w + 1];
        v8 += o.d8[a + w + 1];
        vc = oracleMaxU32(vc, o.cv[a + w + 1]);
        count++;
    }
    *r64 = v64 / count;
    *r32 = v32 / count;
    *r16 = v16 / count;
    *r8 = v8 / count;
    *rcv = vc;
}

TEST_CASE("get_sb128_me_data - the 2x2 gather over every grid position, vs the oracle") {
    // A 192x128 picture (me grid 3x2 at b64_size 64): every SB position
    // exercises a different neighbor-mask arm (interior, right edge,
    // bottom edge, bottom-right corner).
    const uint32_t n = 6;
    std::vector<uint32_t> d64(n), d32(n), d16(n), d8(n), cv(n);
    for (uint32_t i = 0; i < n; ++i) {
        d64[i] = 1000u * (i + 1) + 7;
        d32[i] = 500u * (i + 1) + 3;
        d16[i] = 250u * (i + 1) + 11;
        d8[i] = 125u * (i + 1) + 5;
        cv[i] = 97u * (i + 1) + 1;
    }
    Sb128Pcs pcs;
    pcs.b64Size = 64;
    pcs.alignedWidth = 192;
    pcs.alignedHeight = 128;
    pcs.me64x64Distortion = d64.data();
    pcs.me32x32Distortion = d32.data();
    pcs.me16x16Distortion = d16.data();
    pcs.me8x8Distortion = d8.data();
    pcs.me8x8CostVariance = cv.data();
    OracleSb128 o;
    o.meSbSize = 64;
    o.picW = 192;
    o.picH = 128;
    o.d64 = d64.data();
    o.d32 = d32.data();
    o.d16 = d16.data();
    o.d8 = d8.data();
    o.cv = cv.data();
    for (uint32_t oy = 0; oy < 128; oy += 64)
        for (uint32_t ox = 0; ox < 192; ox += 64) {
            Sb128Ctx ctx;
            ctx.sbOriginX = ox;
            ctx.sbOriginY = oy;
            uint32_t r64, r32, r16, r8, rcv;
            getSb128MeData(pcs, ctx, &r64, &r32, &r16, &r8, &rcv);
            uint32_t e64, e32, e16, e8, ecv;
            oracleSb128MeData(o, ox, oy, &e64, &e32, &e16, &e8, &ecv);
            CHECK(r64 == e64);
            CHECK(r32 == e32);
            CHECK(r16 == e16);
            CHECK(r8 == e8);
            CHECK(rcv == ecv);
        }
}

TEST_CASE("get_sb128_me_data - the interior 4-cell average hand pin") {
    // SB (64,0): me_sb (1,0) of the 3x2 grid -> cells {1,2,4,5} ->
    // dist_64 = (2007+3007+5007+6007)/4.
    const uint32_t n = 6;
    std::vector<uint32_t> d64(n), d32(n), d16(n), d8(n), cv(n);
    for (uint32_t i = 0; i < n; ++i) {
        d64[i] = 1000u * (i + 1) + 7;
        d32[i] = 500u * (i + 1) + 3;
        d16[i] = 250u * (i + 1) + 11;
        d8[i] = 125u * (i + 1) + 5;
        cv[i] = 97u * (i + 1) + 1;
    }
    Sb128Pcs pcs;
    pcs.b64Size = 64;
    pcs.alignedWidth = 192;
    pcs.alignedHeight = 128;
    pcs.me64x64Distortion = d64.data();
    pcs.me32x32Distortion = d32.data();
    pcs.me16x16Distortion = d16.data();
    pcs.me8x8Distortion = d8.data();
    pcs.me8x8CostVariance = cv.data();
    Sb128Ctx ctx;
    ctx.sbOriginX = 64;
    ctx.sbOriginY = 0;
    uint32_t r64, r32, r16, r8, rcv;
    getSb128MeData(pcs, ctx, &r64, &r32, &r16, &r8, &rcv);
    CHECK(r64 == (2007u + 3007u + 5007u + 6007u) / 4);
    CHECK(r32 == (1003u + 1503u + 2503u + 3003u) / 4);
    CHECK(r16 == (511u + 761u + 1261u + 1511u) / 4);
    CHECK(r8 == (255u + 380u + 630u + 755u) / 4);
    CHECK(rcv == 583);  // MAX(cv[1], cv[2], cv[4], cv[5]) = 97*5+1
}

TEST_CASE("get_sb128_variance - the uint16 average over every grid position, vs the oracle") {
    // Stride 3 (the tier count); the [ME_TIER_ZERO_PU_64x64] = 0 column is
    // the one consumed. 128x128 picture -> 2x2 b64 grid.
    const uint32_t n = 4, stride = 3;
    std::vector<uint16_t> var(n * stride);
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t t = 0; t < stride; ++t)
            var[i * stride + t] = (uint16_t)(400u * (i + 1) + 100u * t + 9u);
    Sb128VarPcs pcs;
    pcs.b64Size = 64;
    pcs.alignedWidth = 128;
    pcs.alignedHeight = 128;
    pcs.variance64 = var.data();  // &variance[0][0], stride 3
    pcs.varianceStride = stride;
    for (uint32_t oy = 0; oy < 128; oy += 64)
        for (uint32_t ox = 0; ox < 128; ox += 64) {
            Sb128Ctx ctx;
            ctx.sbOriginX = ox;
            ctx.sbOriginY = oy;
            uint16_t v;
            getSb128Variance(pcs, ctx, &v);
            const uint32_t w = (128 + 63) / 64, h = w;
            const uint32_t x = ox / 64, y = oy / 64, a = x + y * w;
            uint32_t acc = var[a * stride + 0], cnt = 1;
            if (x + 1 < w) { acc += var[(a + 1) * stride + 0]; cnt++; }
            if (y + 1 < h) { acc += var[(a + w) * stride + 0]; cnt++; }
            if (x + 1 < w && y + 1 < h) { acc += var[(a + w + 1) * stride + 0]; cnt++; }
            CHECK(v == (uint16_t)(acc / cnt));
        }
}

TEST_CASE("set_sq_txs_ctrls - the four levels, whole-struct") {
    // :3937-3963. Level 0 writes ONLY enabled; levels 1-3 write the full
    // surface; the default arm (4) writes nothing.
    for (int level = 0; level <= 4; ++level) {
        NsqPsqTxsCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setSqTxsCtrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.enabled == 0);
            CHECK(c.hvToSqTh == 0xAAAAAAAAu);
            CHECK(c.hToVTh == 0xAAAAAAAAu);
        } else if (level == 1) {
            CHECK(c.enabled == 1);
            CHECK(c.hvToSqTh == 1000);
            CHECK(c.hToVTh == 100);
        } else if (level == 2) {
            CHECK(c.enabled == 1);
            CHECK(c.hvToSqTh == 250);
            CHECK(c.hToVTh == 25);
        } else if (level == 3) {
            CHECK(c.enabled == 1);
            CHECK(c.hvToSqTh == 150);
            CHECK(c.hToVTh == 15);
        } else {
            CHECK(c.enabled == kPoison);
            CHECK(c.hvToSqTh == 0xAAAAAAAAu);
            CHECK(c.hToVTh == 0xAAAAAAAAu);
        }
    }
}

TEST_CASE("set_chroma_controls - the six levels, whole-struct + returned uv_mode") {
    // :4396-4463. CHROMA_MODE_2 = 2, CHROMA_MODE_0 = 0, CHROMA_MODE_1 = 1
    // (definitions.h:2053-2055). Level 0: mode 2 + enabled 0; levels 1-4:
    // mode 0 with the descending (ind_uv_last_mds, nic num, cost th)
    // ladders; level 5: mode 1 + enabled 1 ONLY; default: uv_mode 0 (the
    // :4455-4457 trailing write on the unswitched local).
    for (int level = 0; level <= 6; ++level) {
        UvCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        const uint8_t uvMode = setChromaCtrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(uvMode == 2);
            CHECK(c.uvMode == 2);
            CHECK(c.enabled == 0);
            CHECK(c.indUvLastMds == kPoison);
            CHECK(c.skipIndUvIfOnlyDc == kPoison);
            CHECK(c.uvNicScalingNum == kPoison);
            CHECK(c.interVsIntraCostTh == 0xAAAAAAAAu);
        } else if (level == 1) {
            CHECK(uvMode == 0);
            CHECK(c.uvMode == 0);
            CHECK(c.enabled == 1);
            CHECK(c.indUvLastMds == 0);
            CHECK(c.skipIndUvIfOnlyDc == 0);
            CHECK(c.uvNicScalingNum == 16);
            CHECK(c.interVsIntraCostTh == 0);
        } else if (level == 2) {
            CHECK(uvMode == 0);
            CHECK(c.enabled == 1);
            CHECK(c.indUvLastMds == 1);
            CHECK(c.skipIndUvIfOnlyDc == 0);
            CHECK(c.uvNicScalingNum == 8);
            CHECK(c.interVsIntraCostTh == 0);
        } else if (level == 3) {
            CHECK(uvMode == 0);
            CHECK(c.enabled == 1);
            CHECK(c.indUvLastMds == 1);
            CHECK(c.skipIndUvIfOnlyDc == 0);
            CHECK(c.uvNicScalingNum == 1);
            CHECK(c.interVsIntraCostTh == 100);
        } else if (level == 4) {
            CHECK(uvMode == 0);
            CHECK(c.enabled == 1);
            CHECK(c.indUvLastMds == 2);
            CHECK(c.skipIndUvIfOnlyDc == 1);
            CHECK(c.uvNicScalingNum == 1);
            CHECK(c.interVsIntraCostTh == 100);
        } else if (level == 5) {
            CHECK(uvMode == 1);
            CHECK(c.uvMode == 1);
            CHECK(c.enabled == 1);
            CHECK(c.indUvLastMds == kPoison);
            CHECK(c.skipIndUvIfOnlyDc == kPoison);
            CHECK(c.uvNicScalingNum == kPoison);
            CHECK(c.interVsIntraCostTh == 0xAAAAAAAAu);
        } else {
            // default arm: switch writes nothing, uv_mode keeps its 0 init
            // and the trailing write stores 0.
            CHECK(uvMode == 0);
            CHECK(c.uvMode == 0);
            CHECK(c.enabled == kPoison);
            CHECK(c.indUvLastMds == kPoison);
            CHECK(c.uvNicScalingNum == kPoison);
        }
    }
}

TEST_CASE("set_wm_controls - the five levels, whole-struct") {
    // :4467-4521 with MAX_WARP_LVL = 4 (:4465). Level 0 writes ONLY
    // enabled; levels 1-4 write the full surface (case 4 = MAX_WARP_LVL:
    // use_wm_for_mvp 0, iterations 0); default writes nothing.
    for (int level = 0; level <= 5; ++level) {
        WmCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setWmCtrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.enabled == 0);
            CHECK(c.useWmForMvp == kPoison);
            CHECK(c.refinementIterations == kPoison);
            CHECK(c.refineDiag == kPoison);
            CHECK(c.refineLevel == kPoison);
            CHECK(c.lowerBandTh == 0xAAAA);
            CHECK(c.upperBandTh == 0xAAAA);
            // :4476-4477 - only enabled is written; shut_approx_if_not_mds0
            // keeps its poison byte and stays unread (the intrabc-test
            // precedent: a 0xAA bool compares unequal to both true and
            // false, so it cannot be CHECKed).
        } else if (level == 1) {
            CHECK(c.enabled == 1);
            CHECK(c.useWmForMvp == 1);
            CHECK(c.refinementIterations == 16);
            CHECK(c.refineDiag == 1);
            CHECK(c.refineLevel == 0);
            CHECK(c.lowerBandTh == 0);
            CHECK(c.upperBandTh == (uint16_t)~0);
            CHECK(c.shutApproxIfNotMds0 == false);
        } else if (level == 2) {
            CHECK(c.enabled == 1);
            CHECK(c.useWmForMvp == 1);
            CHECK(c.refinementIterations == 8);
            CHECK(c.refineDiag == 0);
            CHECK(c.refineLevel == 1);
            CHECK(c.lowerBandTh == 0);
            CHECK(c.upperBandTh == (uint16_t)~0);
            CHECK(c.shutApproxIfNotMds0 == true);
        } else if (level == 3) {
            CHECK(c.enabled == 1);
            CHECK(c.useWmForMvp == 1);
            CHECK(c.refinementIterations == 8);
            CHECK(c.refineDiag == 0);
            CHECK(c.refineLevel == 1);
            CHECK(c.lowerBandTh == (1 << 10));
            CHECK(c.upperBandTh == (uint16_t)~0);
            CHECK(c.shutApproxIfNotMds0 == true);
        } else if (level == 4) {
            CHECK(c.enabled == 1);
            CHECK(c.useWmForMvp == 0);
            CHECK(c.refinementIterations == 0);
            CHECK(c.refineDiag == 0);
            CHECK(c.refineLevel == 1);
            CHECK(c.lowerBandTh == 0);
            CHECK(c.upperBandTh == (uint16_t)~0);
            CHECK(c.shutApproxIfNotMds0 == false);
        } else {
            CHECK(c.enabled == kPoison);
            CHECK(c.useWmForMvp == kPoison);
            CHECK(c.refinementIterations == kPoison);
            CHECK(c.refineDiag == kPoison);
            CHECK(c.refineLevel == kPoison);
            CHECK(c.lowerBandTh == 0xAAAA);
            CHECK(c.upperBandTh == 0xAAAA);
        }
    }
}

// nic level getters, independent transcriptions (:4521-:4586).
static uint8_t oracleNicLevelDefault(int em, int isBase) {
    uint8_t lvl;
    if (em <= (int)ENC_MR) lvl = isBase ? 1 : 2;
    else if (em <= (int)ENC_M0) lvl = isBase ? 2 : 4;
    else if (em <= (int)ENC_M1) lvl = isBase ? 4 : 5;
    else if (em <= (int)ENC_M3) lvl = isBase ? 5 : 6;
    else if (em <= (int)ENC_M5) lvl = 7;
    else if (em <= (int)ENC_M6) lvl = 8;
    else if (em <= (int)ENC_M7) lvl = isBase ? 8 : 9;
    else if (em <= (int)ENC_M8) lvl = 9;
    else if (em <= (int)ENC_M9) lvl = isBase ? 9 : 11;
    else lvl = 11;
    return lvl;
}
static uint8_t oracleNicLevelRtc(int em) {
    if (em <= (int)ENC_M8) return 9;
    if (em <= (int)ENC_M9) return 10;
    return 11;
}
static uint8_t oracleNicLevelAllIntra(int em) {
    uint8_t lvl;
    if (em <= (int)ENC_M0) lvl = 1;
    else if (em <= (int)ENC_M2) lvl = 3;
    else if (em <= (int)ENC_M4) lvl = 5;
    else if (em <= (int)ENC_M6) lvl = 6;
    else if (em <= (int)ENC_M7) lvl = 7;
    else lvl = 11;
    return lvl;
}

TEST_CASE("get_nic_level_{default,rtc,allintra} - every preset x is_base, vs the oracle") {
    for (int em = (int)ENC_MR; em <= (int)ENC_M13; ++em) {
        for (int base = 0; base <= 1; ++base) {
            CHECK(getNicLevelDefault((EncMode)em, (uint8_t)base) == oracleNicLevelDefault(em, base));
        }
        CHECK(getNicLevelRtc((EncMode)em) == oracleNicLevelRtc(em));
        CHECK(getNicLevelAllIntra((EncMode)em) == oracleNicLevelAllIntra(em));
    }
    // Hand pins on the is_base boundaries (the :4531/:4534/:4543 8/9/11
    // splits).
    CHECK(getNicLevelDefault(ENC_M7, 1) == 8);
    CHECK(getNicLevelDefault(ENC_M7, 0) == 9);
    CHECK(getNicLevelDefault(ENC_M9, 1) == 9);
    CHECK(getNicLevelDefault(ENC_M9, 0) == 11);
    CHECK(getNicLevelRtc(ENC_M8) == 9);
    CHECK(getNicLevelRtc(ENC_M9) == 10);
    CHECK(getNicLevelRtc(ENC_M10) == 11);
}

// MD_STAGE_NICS_SCAL_NUM, independent transcription
// (definitions.h:798-817, S0..S3 columns).
static const uint8_t oracleNicsScalNum[16][4] = {
    {0, 20, 20, 20}, {0, 18, 18, 18}, {0, 16, 16, 16}, {0, 12, 12, 12},
    {0, 10, 10, 10}, {0, 8, 8, 8},   {0, 6, 6, 6},    {0, 4, 5, 5},
    {0, 4, 4, 4},    {0, 3, 4, 4},   {0, 3, 3, 3},    {0, 3, 2, 2},
    {0, 3, 1, 1},    {0, 2, 1, 1},   {0, 2, 0, 0},    {0, 0, 0, 0},
};

TEST_CASE("kMdStageNicsScalNum - all 64 cells, vs the oracle") {
    for (int lvl = 0; lvl < 16; ++lvl)
        for (int s = 0; s < 4; ++s)
            CHECK(kMdStageNicsScalNum[lvl][s] == oracleNicsScalNum[lvl][s]);
}

// svt_aom_set_nic_controls, independent transcription of the 12-level
// switch (:4593-:4956). Rows: {scaling, mds1_class_th, mds1_band_cnt,
// mds2_class_th, mds2_band_cnt, mds3_class_th, mds3_band_cnt,
// i_mds3_mult, skip_mds1, th_intra, th_inter, mds1_rank, mds2_base,
// mds2_rank, mds2_dev, mds3_base, merge_mult}.
struct OracleNicRow {
    uint8_t scaling;
    uint64_t mds1ClassTh;
    int mds1BandCnt;   // -1 = not written
    uint64_t mds2ClassTh;
    int mds2BandCnt;   // -1 = not written
    uint64_t mds3ClassTh;
    int mds3BandCnt;   // -1 = not written
    uint8_t iMds3Mult;
    int skipMds1;
    uint64_t thIntra;
    uint64_t thInter;
    int mds1Rank;      // -1 = not written
    uint64_t mds2Base;
    int mds2Rank;
    int mds2Dev;
    uint64_t mds3Base;
    int mergeMult;     // -1 = not written
    int staging;
};
static const OracleNicRow oracleNicRows[12] = {
    // level 0: everything wide-open
    {0, ~(uint64_t)0, -1, ~(uint64_t)0, -1, ~(uint64_t)0, -1, 0, 0,
     ~(uint64_t)0, ~(uint64_t)0, 0, ~(uint64_t)0, 0, 0, ~(uint64_t)0, (uint8_t)~0, 1},
    // level 1
    {0, ~(uint64_t)0, -1, 25, 4, 25, 4, 50, 0,
     ~(uint64_t)0, ~(uint64_t)0, 0, 50, 0, 0, 50, (uint8_t)~0, 1},
    // level 2
    {1, ~(uint64_t)0, -1, 25, 4, 25, 8, 50, 0,
     1200, 500, 0, 30, 0, 0, 30, (uint8_t)~0, 1},
    // level 3
    {3, ~(uint64_t)0, -1, 25, 4, 25, 8, 50, 0,
     1200, 500, 0, 30, 0, 0, 25, (uint8_t)~0, 1},
    // level 4
    {3, 500, 3, 25, 8, 20, 12, 50, 0,
     1200, 300, 0, 20, 0, 0, 15, (uint8_t)~0, 1},
    // level 5
    {6, 300, 4, 25, 10, 15, 16, 50, 0,
     1200, 300, 0, 20, 0, 0, 15, (uint8_t)~0, 1},
    // level 6
    {6, 200, 16, 10, 10, 5, 16, 50, 0,
     1200, 300, 3, 15, 1, 5, 15, 4, 1},
    // level 7
    {8, 200, 16, 10, 10, 5, 16, 50, 0,
     1200, 300, 3, 15, 1, 5, 15, 4, 1},
    // level 8
    {13, 200, 16, 10, 10, 5, 16, 50, 1,
     300, 300, 3, 3, 1, 5, 3, 4, 1},
    // level 9
    {14, 200, 16, 10, 10, 5, 16, 50, 1,
     100, 100, 3, 1, 1, 5, 1, 4, 1},
    // level 10
    {15, 150, 16, 5, 10, 5, 16, 50, 1,
     1, 1, 3, 1, 1, 5, 1, 4, 1},
    // level 11
    {15, 75, 16, 0, 10, 0, 16, 50, 1,
     1, 1, 3, 1, 1, 5, 1, 4, 1},
};

TEST_CASE("set_nic_controls - every level x null/nonnull ctx, vs the oracle") {
    for (int level = 0; level <= 12; ++level) {
        NicCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        const uint8_t scaling = setNicCtrls(&c, (uint8_t)level);
        if (level <= 11) {
            const OracleNicRow& r = oracleNicRows[level];
            CHECK(scaling == r.scaling);
            CHECK(c.mdStagingMode == (uint8_t)r.staging);
            CHECK(c.pruningCtrls.mds1ClassTh == r.mds1ClassTh);
            if (r.mds1BandCnt >= 0) CHECK(c.pruningCtrls.mds1BandCnt == (uint8_t)r.mds1BandCnt);
            else CHECK(c.pruningCtrls.mds1BandCnt == kPoison);
            CHECK(c.pruningCtrls.mds2ClassTh == r.mds2ClassTh);
            if (r.mds2BandCnt >= 0) CHECK(c.pruningCtrls.mds2BandCnt == (uint8_t)r.mds2BandCnt);
            else CHECK(c.pruningCtrls.mds2BandCnt == kPoison);
            CHECK(c.pruningCtrls.mds3ClassTh == r.mds3ClassTh);
            if (r.mds3BandCnt >= 0) CHECK(c.pruningCtrls.mds3BandCnt == (uint8_t)r.mds3BandCnt);
            else CHECK(c.pruningCtrls.mds3BandCnt == kPoison);
            CHECK(c.pruningCtrls.iMds3ClassThMult == r.iMds3Mult);
            CHECK(c.pruningCtrls.enableSkippingMds1 == (r.skipMds1 != 0));
            CHECK(c.pruningCtrls.mds1CandBaseThIntra == r.thIntra);
            CHECK(c.pruningCtrls.mds1CandBaseThInter == r.thInter);
            CHECK(c.pruningCtrls.mds1CandThRankFactor == (uint16_t)r.mds1Rank);
            CHECK(c.pruningCtrls.mds2CandBaseTh == r.mds2Base);
            CHECK(c.pruningCtrls.mds2CandThRankFactor == (uint16_t)r.mds2Rank);
            CHECK(c.pruningCtrls.mds2RelativeDevTh == (uint16_t)r.mds2Dev);
            CHECK(c.pruningCtrls.mds3CandBaseTh == r.mds3Base);
            if (r.mergeMult >= 0) CHECK(c.pruningCtrls.mergeInterCandsMult == (uint8_t)r.mergeMult);
            else CHECK(c.pruningCtrls.mergeInterCandsMult == kPoison);
            // The scaling triple follows the MD_STAGE_NICS_SCAL_NUM row.
            CHECK(c.scalingCtrls.stage1ScalingNum ==
                  oracleNicsScalNum[r.scaling][(int)kMdStage1]);
            CHECK(c.scalingCtrls.stage2ScalingNum ==
                  oracleNicsScalNum[r.scaling][(int)kMdStage2]);
            CHECK(c.scalingCtrls.stage3ScalingNum ==
                  oracleNicsScalNum[r.scaling][(int)kMdStage3]);
        } else {
            // :4957-4964 - the trailing ctx write is OUTSIDE the switch:
            // it runs on the default arm too, from the locals' inits
            // (scaling 0, staging MD_STAGING_MODE_0). The pruning surface
            // keeps its poison.
            CHECK(scaling == 0);
            CHECK(c.mdStagingMode == 0);
            CHECK(c.pruningCtrls.mds1ClassTh == 0xAAAAAAAAAAAAAAAAull);
            CHECK(c.pruningCtrls.mds1CandBaseThIntra == 0xAAAAAAAAAAAAAAAAull);
            CHECK(c.scalingCtrls.stage1ScalingNum == oracleNicsScalNum[0][1]);
            CHECK(c.scalingCtrls.stage2ScalingNum == oracleNicsScalNum[0][2]);
            CHECK(c.scalingCtrls.stage3ScalingNum == oracleNicsScalNum[0][3]);
        }
    }
}

TEST_CASE("set_nic_controls - the null-ctx arm returns the scaling level and writes nothing") {
    for (int level = 0; level <= 11; ++level) {
        CHECK(setNicCtrls(nullptr, (uint8_t)level) == oracleNicRows[level].scaling);
    }
    CHECK(setNicCtrls(nullptr, 12) == 0);
}

// ---------------------------------------------------------------------------
// EMC2 rung 2: set_nsq_geom_ctrls (:4968-:5022) + set_nsq_search_ctrls
// (:5024-:5375).
// ---------------------------------------------------------------------------

TEST_CASE("set_nsq_geom_ctrls - every level, whole-struct + the nullable out-params") {
    // :4968-5022. The local is zero-init THEN switched, so every level
    // (0-4) leaves all four fields written; the default arm (5) writes
    // the zero-init local through (all zeros, enabled = 0/false). The
    // out-params mirror the final local; a NULL out-param is untouched.
    for (int level = 0; level <= 5; ++level) {
        NsqGeomCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        uint8_t hva = kPoison, hv4 = kPoison, minB = kPoison;
        setNsqGeomCtrls(c, (uint8_t)level, &hva, &hv4, &minB);
        uint8_t eHva = 0, eHv4 = 0, eMin = 0;
        bool eEn = false;
        if (level == 0) {
            eEn = false; eMin = 0; eHv4 = 0; eHva = 0;
        } else if (level == 1) {
            eEn = true; eMin = 0; eHv4 = 1; eHva = 1;
        } else if (level == 2) {
            eEn = true; eMin = 0; eHv4 = 1; eHva = 0;
        } else if (level == 3) {
            eEn = true; eMin = 8; eHv4 = 0; eHva = 0;
        } else if (level == 4) {
            eEn = true; eMin = 16; eHv4 = 0; eHva = 0;
        }  // level 5: the zero-init local flows through (all zeros)
        CHECK(c.enabled == eEn);
        CHECK(c.minNsqBlockSize == eMin);
        CHECK(c.allowHv4 == eHv4);
        CHECK(c.allowHvaHvb == eHva);
        CHECK(hva == eHva);
        CHECK(hv4 == eHv4);
        CHECK(minB == eMin);
    }
    // NULL out-params: the call neither reads nor writes them.
    NsqGeomCtrls c;
    std::memset(&c, kPoison, sizeof(c));
    setNsqGeomCtrls(c, 2, nullptr, nullptr, nullptr);
    CHECK(c.enabled == true);
    CHECK(c.minNsqBlockSize == 0);
    CHECK(c.allowHv4 == 1);
    CHECK(c.allowHvaHvb == 0);
}

// The 20-row switch, independent transcription (:5055-:5336). Columns:
// {en, sq, part0, split, lower, hvv, nhv, off16, psq, sub, comp, hv};
// (uint32)~0 encoded as 0xFFFFFFFFu.
struct OracleNsqSearchRow {
    int en;
    uint32_t sq, part0, split, lower, hvv, nhv, off16, psq, sub, comp, hv;
};
static const OracleNsqSearchRow oracleNsqSearchRows[20] = {
    {0, 0xFFFFFFFFu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFFFFFFFFu},
    {1, 105, 0, 0, 0, 0, 0, 0, 0, 0, 0, 115},
    {1, 105, 0, 150, 3, 0, 0, 10, 0, 0, 0, 115},
    {1, 105, 0, 100, 3, 0, 0, 10, 0, 1, 0, 115},
    {1, 100, 0, 100, 3, 0, 0, 10, 0, 1, 80, 115},
    {1, 100, 0, 100, 5, 0, 0, 10, 0, 1, 80, 110},
    {1, 100, 0, 100, 5, 0, 0, 10, 0, 1, 80, 100},
    {1, 95, 0, 80, 5, 0, 0, 10, 0, 1, 80, 100},
    {1, 95, 0, 80, 5, 30, 20, 10, 0, 1, 80, 100},
    {1, 95, 0, 80, 5, 40, 30, 10, 0, 1, 60, 100},
    {1, 95, 0, 60, 10, 40, 30, 10, 0, 1, 60, 100},
    {1, 95, 0, 60, 10, 50, 30, 10, 0, 1, 40, 100},
    {1, 95, 0, 60, 10, 50, 30, 10, 0, 1, 20, 100},
    {1, 95, 0, 60, 10, 60, 40, 10, 0, 1, 20, 100},
    {1, 95, 5, 50, 10, 60, 40, 10, 0, 1, 20, 100},
    {1, 90, 20, 40, 20, 60, 50, 10, 0, 1, 15, 75},
    {1, 90, 50, 40, 20, 70, 60, 10, 0, 1, 15, 75},
    {1, 90, 50, 40, 20, 70, 60, 15, 1, 1, 10, 75},
    {1, 90, 75, 40, 20, 80, 70, 15, 1, 1, 5, 75},
    {1, 90, 80, 35, 20, 85, 70, 15, 1, 1, 5, 75},
};

// DIVIDE_AND_ROUND (utility.h:96).
static uint32_t oracleDivRound(uint64_t x, uint32_t y) {
    return (uint32_t)((x + (y >> 1)) / y);
}

// set_nsq_search_ctrls, independent transcription of the level choice +
// QP tail + PD_PASS_0 overwrite (:5024-:5364).
static void oracleNsqSearch(NsqSearchCtrls& c, NsqPsqTxsCtrls& t, bool isISlice, int em,
                            bool mimic, uint32_t sbSize, uint32_t dist8, uint32_t var8, uint32_t qp,
                            bool qpScale, int pdPass, int levelIn) {
    bool meDistMod = false;
    if (!isISlice && em > (int)ENC_MR) meDistMod = true;
    int level = (int)levelIn;
    if (mimic) {
        level = 0;
    } else if (meDistMod && level) {
        if (dist8 <= sbSize * sbSize * 3 && var8 <= 10000) {
            level = level + 1 < 19 ? level + 1 : 19;
        }
    }
    if (level <= 19) {
        const OracleNsqSearchRow& r = oracleNsqSearchRows[level];
        c.enabled = r.en != 0;
        c.sqWeight = r.sq;
        c.hvWeight = r.hv;
        c.maxPart0ToPart1Dev = r.part0;
        c.nsqSplitCostTh = r.split;
        c.lowerDepthSplitCostTh = r.lower;
        c.hVsVSplitRateTh = r.hvv;
        c.nonHvSplitRateTh = r.nhv;
        c.rateThOffsetLte16 = r.off16;
        c.psqTxsLvl = (uint8_t)r.psq;
        c.subDepthBlockLvl = (uint8_t)r.sub;
        c.componentMultipleTh = r.comp;
    }
    uint32_t w = 1, d = 1;
    if (qpScale) {
        w = oracleMaxU32(10u, qp);
        d = 63u;
        if (qp >= 46) {
            double ex = -(std::max(40.0, (double)qp) - 35) / 10;
            double qi = std::exp(ex);
            qi = 1.05 - qi;
            qi *= 10000;
            w = (uint32_t)qi;
            d = 10000u;
        }
    }
    c.componentMultipleTh = oracleDivRound((uint64_t)c.componentMultipleTh * w, d);
    c.nsqSplitCostTh = oracleDivRound((uint64_t)c.nsqSplitCostTh * w, d);
    int off = 5;
    off = (int)oracleDivRound((uint64_t)off * w, d);
    c.maxPart0ToPart1Dev = (uint32_t)std::max((int)((int)c.maxPart0ToPart1Dev - off), 0);
    if (pdPass == (int)kPdPass0) {
        c.sqWeight = 90;
        c.maxPart0ToPart1Dev = 0;
        c.nsqSplitCostTh = 60;
        c.lowerDepthSplitCostTh = 10;
        c.hVsVSplitRateTh = 60;
        c.nonHvSplitRateTh = 60;
        c.rateThOffsetLte16 = 10;
        c.psqTxsLvl = 0;
        c.subDepthBlockLvl = 0;
        c.componentMultipleTh = 10;
        c.hvWeight = 90;
    }
    // set_sq_txs_ctrls chaining.
    if (c.psqTxsLvl == 0) t.enabled = 0;
    else if (c.psqTxsLvl == 1) { t.enabled = 1; t.hvToSqTh = 1000; t.hToVTh = 100; }
    else if (c.psqTxsLvl == 2) { t.enabled = 1; t.hvToSqTh = 250; t.hToVTh = 25; }
    else if (c.psqTxsLvl == 3) { t.enabled = 1; t.hvToSqTh = 150; t.hToVTh = 15; }
}

TEST_CASE("set_nsq_search_ctrls - the 20-row switch + QP tail + pd0 overwrite, vs the oracle") {
    // Neutral walls (I-slice: me_dist_mod off; no mimic; SB64 arrays
    // hostile so the bump cannot fire) enumerate the switch rows 0-19
    // plus the default arm (20) under both qp-scaling flags and
    // PD_PASS_0/1.
    const uint32_t n = 4;
    std::vector<uint32_t> d8(n), cv(n);
    for (uint32_t i = 0; i < n; ++i) {
        d8[i] = 0xFFFFFFFFu;  // never <= 12288, so no bump
        cv[i] = 0xFFFFFFFFu;  // never <= 10000
    }
    for (int level = 0; level <= 20; ++level)
        for (int qpScale = 0; qpScale <= 1; ++qpScale)
            for (int pdPass = 0; pdPass <= 1; ++pdPass) {
                NsqSearchCtrls c;
                std::memset(&c, kPoison, sizeof(c));
                NsqPsqTxsCtrls t;
                std::memset(&t, kPoison, sizeof(t));
                NsqSearchPcs pcs;
                std::memset(&pcs, 0, sizeof(pcs));
                pcs.isISlice = true;  // me_dist_mod off
                pcs.encMode = ENC_M0;
                pcs.mimicOnlyTx4x4 = false;
                pcs.sbSize = 64;
                pcs.sbIndex = 1;
                pcs.me8x8Distortion = d8.data();
                pcs.me8x8CostVariance = cv.data();
                pcs.sb128.b64Size = 64;
                pcs.sb128.alignedWidth = 64;
                pcs.sb128.alignedHeight = 64;
                pcs.sb128.me64x64Distortion = d8.data();
                pcs.sb128.me32x32Distortion = d8.data();
                pcs.sb128.me16x16Distortion = d8.data();
                pcs.sb128.me8x8Distortion = d8.data();
                pcs.sb128.me8x8CostVariance = cv.data();
                pcs.qp = 50;
                pcs.nsqQpBasedThScaling = qpScale == 1;
                pcs.pdPass = (uint8_t)pdPass;
                setNsqSearchCtrls(c, t, pcs, (uint8_t)level);
                NsqSearchCtrls e;
                std::memset(&e, kPoison, sizeof(e));
                NsqPsqTxsCtrls et;
                std::memset(&et, kPoison, sizeof(et));
                oracleNsqSearch(e, et, true, (int)ENC_M0, false, 64, 0, 0, 50, qpScale == 1,
                                pdPass, level);
                CHECK(c.enabled == e.enabled);
                CHECK(c.sqWeight == e.sqWeight);
                CHECK(c.hvWeight == e.hvWeight);
                CHECK(c.maxPart0ToPart1Dev == e.maxPart0ToPart1Dev);
                CHECK(c.nsqSplitCostTh == e.nsqSplitCostTh);
                CHECK(c.lowerDepthSplitCostTh == e.lowerDepthSplitCostTh);
                CHECK(c.hVsVSplitRateTh == e.hVsVSplitRateTh);
                CHECK(c.nonHvSplitRateTh == e.nonHvSplitRateTh);
                CHECK(c.rateThOffsetLte16 == e.rateThOffsetLte16);
                CHECK(c.componentMultipleTh == e.componentMultipleTh);
                CHECK(c.psqTxsLvl == e.psqTxsLvl);
                CHECK(c.subDepthBlockLvl == e.subDepthBlockLvl);
                CHECK(t.enabled == et.enabled);
                CHECK(t.hvToSqTh == et.hvToSqTh);
                CHECK(t.hToVTh == et.hToVTh);
                // Hand pin on the one row where the QP tail actually
                // bites: level 4 (comp 80, split 100, part0 0) at qp 50,
                // scaling on -> comp (80*8268+5000)/10000, split
                // (100*8268+5000)/10000, part0 max(0-4,0) = 0.
                if (level == 4 && qpScale == 1 && pdPass == 1) {
                    CHECK(c.componentMultipleTh == oracleDivRound(80ull * 8268, 10000));
                    CHECK(c.nsqSplitCostTh == oracleDivRound(100ull * 8268, 10000));
                    CHECK(c.maxPart0ToPart1Dev == 0);
                }
            }
}

TEST_CASE("set_nsq_search_ctrls - the mimic gate, the me-dist bump and its cap") {
    const uint32_t n = 4;
    std::vector<uint32_t> d8(n), cv(n);
    // SB (1,0) of the 2x2 grid: cells {1,2,3} -> avg dist 1200 / var 60
    // (the SB128 gather averages three cells here).
    d8[0] = 1000; d8[1] = 1100; d8[2] = 1200; d8[3] = 1300;
    cv[0] = 10; cv[1] = 20; cv[2] = 90; cv[3] = 200;
    for (int sbSize = 64; sbSize <= 128; sbSize += 64) {
        // mimic_only_tx_4x4 forces row 0 regardless of the level.
        {
            NsqSearchCtrls c;
            std::memset(&c, kPoison, sizeof(c));
            NsqPsqTxsCtrls t;
            std::memset(&t, kPoison, sizeof(t));
            NsqSearchPcs pcs;
            std::memset(&pcs, 0, sizeof(pcs));
            pcs.isISlice = false;
            pcs.encMode = ENC_M10;  // me_dist_mod on
            pcs.mimicOnlyTx4x4 = true;
            pcs.sbSize = (uint32_t)sbSize;
            pcs.sbIndex = 1;
            pcs.me8x8Distortion = d8.data();
            pcs.me8x8CostVariance = cv.data();
            pcs.sb128.b64Size = 64;
            pcs.sb128.alignedWidth = 128;
            pcs.sb128.alignedHeight = 128;
            pcs.sb128.me64x64Distortion = d8.data();
            pcs.sb128.me32x32Distortion = d8.data();
            pcs.sb128.me16x16Distortion = d8.data();
            pcs.sb128.me8x8Distortion = d8.data();
            pcs.sb128.me8x8CostVariance = cv.data();
            pcs.pdPass = kPdPass1;
            setNsqSearchCtrls(c, t, pcs, 9);
            CHECK(c.enabled == false);
            CHECK(c.sqWeight == 0xFFFFFFFFu);
            CHECK(c.hvWeight == 0xFFFFFFFFu);
        }
        // The bump: easy SB -> level + 1 (row 6 from 5).
        {
            NsqSearchCtrls c;
            std::memset(&c, kPoison, sizeof(c));
            NsqPsqTxsCtrls t;
            std::memset(&t, kPoison, sizeof(t));
            NsqSearchPcs pcs;
            std::memset(&pcs, 0, sizeof(pcs));
            pcs.isISlice = false;
            pcs.encMode = ENC_M10;
            pcs.mimicOnlyTx4x4 = false;
            pcs.sbSize = (uint32_t)sbSize;
            pcs.sbIndex = 1;
            pcs.me8x8Distortion = d8.data();
            pcs.me8x8CostVariance = cv.data();
            pcs.sb128.b64Size = 64;
            pcs.sb128.alignedWidth = 128;
            pcs.sb128.alignedHeight = 128;
            pcs.sb128.me64x64Distortion = d8.data();
            pcs.sb128.me32x32Distortion = d8.data();
            pcs.sb128.me16x16Distortion = d8.data();
            pcs.sb128.me8x8Distortion = d8.data();
            pcs.sb128.me8x8CostVariance = cv.data();
            pcs.pdPass = kPdPass1;
            setNsqSearchCtrls(c, t, pcs, 5);
            const OracleNsqSearchRow& r6 = oracleNsqSearchRows[6];
            CHECK(c.enabled == true);
            CHECK(c.sqWeight == r6.sq);
            CHECK(c.nsqSplitCostTh == r6.split);
            CHECK(c.componentMultipleTh == r6.comp);
            CHECK(c.hvWeight == r6.hv);
            // The cap: level 19 + bump stays 19. The QP tail is always
            // on for part0: qp 0 -> weight 1 -> offset 5 -> 80 - 5 = 75.
            setNsqSearchCtrls(c, t, pcs, 19);
            const OracleNsqSearchRow& r19 = oracleNsqSearchRows[19];
            CHECK(c.nsqSplitCostTh == r19.split);
            CHECK(c.maxPart0ToPart1Dev == r19.part0 - 5);
        }
        // No bump: hard SB -> the row as-is.
        {
            std::vector<uint32_t> hd8(n), hcv(n);
            for (uint32_t i = 0; i < n; ++i) {
                hd8[i] = 999999u;
                hcv[i] = 999999u;
            }
            NsqSearchCtrls c;
            std::memset(&c, kPoison, sizeof(c));
            NsqPsqTxsCtrls t;
            std::memset(&t, kPoison, sizeof(t));
            NsqSearchPcs pcs;
            std::memset(&pcs, 0, sizeof(pcs));
            pcs.isISlice = false;
            pcs.encMode = ENC_M10;
            pcs.mimicOnlyTx4x4 = false;
            pcs.sbSize = (uint32_t)sbSize;
            pcs.sbIndex = 1;
            pcs.me8x8Distortion = hd8.data();
            pcs.me8x8CostVariance = hcv.data();
            pcs.sb128.b64Size = 64;
            pcs.sb128.alignedWidth = 128;
            pcs.sb128.alignedHeight = 128;
            pcs.sb128.me64x64Distortion = hd8.data();
            pcs.sb128.me32x32Distortion = hd8.data();
            pcs.sb128.me16x16Distortion = hd8.data();
            pcs.sb128.me8x8Distortion = hd8.data();
            pcs.sb128.me8x8CostVariance = hcv.data();
            pcs.pdPass = kPdPass1;
            setNsqSearchCtrls(c, t, pcs, 5);
            const OracleNsqSearchRow& r5 = oracleNsqSearchRows[5];
            CHECK(c.sqWeight == r5.sq);
            CHECK(c.hvWeight == r5.hv);
        }
    }
}

// ---------------------------------------------------------------------------
// EMC2 rung 3: the intra-mode level getters (:5377-:5427),
// set_inter_intra_ctrls (:5455-:5481), set_pd0_ctrls (:5483-:5601),
// set_lpd1_ctrls (:5603-:5937).
// ---------------------------------------------------------------------------

// Independent transcriptions (:5377-:5427). MAX_INTRA_LEVEL - 1 = 9.
static void oracleIntraLevelsDefault(int em, bool isISlice, bool isBase, int trans, uint32_t* intra,
                                     uint32_t* distAng) {
    uint32_t i, d;
    if (em <= (int)ENC_MR) {
        i = isBase ? 1 : 2;
        d = 0;
    } else if (em <= (int)ENC_M2) {
        i = isBase ? 1 : 2;
        d = isBase ? 0 : 1;
    } else if (em <= (int)ENC_M5) {
        i = isBase ? 1 : 6;
        d = isISlice ? 0 : 2;
    } else if (em <= (int)ENC_M7) {
        i = isBase ? 2 : 6;
        d = isISlice ? 0 : 2;
    } else if (em <= (int)ENC_M11) {
        i = (isISlice || trans == 1) ? 4 : 6;
        d = isISlice ? 0 : 2;
    } else {
        i = 9;
        d = 0;
    }
    *intra = i;
    *distAng = d;
}
static void oracleIntraLevelsRtc(int em, bool isISlice, int trans, bool flat, uint32_t* intra,
                                 uint32_t* distAng) {
    uint32_t i, d;
    if ((!flat && em <= (int)ENC_M7) || (flat && em <= (int)ENC_M9)) {
        i = (isISlice || trans == 1) ? 1 : 6;
        d = 1;
    } else if (em <= (int)ENC_M8) {
        i = (isISlice || trans == 1) ? 4 : 6;
        d = 1;
    } else if (em <= (int)ENC_M10) {
        i = (isISlice || trans == 1) ? 4 : 6;
        d = 2;
    } else {
        i = 9;
        d = 0;
    }
    *intra = i;
    *distAng = d;
}

TEST_CASE("get_intra_mode_levels_{default,rtc} - the full wall grid, vs the oracle") {
    for (int em = (int)ENC_MR; em <= (int)ENC_M13; ++em)
        for (int islice = 0; islice <= 1; ++islice)
            for (int base = 0; base <= 1; ++base)
                for (int trans = 0; trans <= 1; ++trans) {
                    uint32_t intra, distAng, eIntra, eDistAng;
                    getIntraModeLevelsDefault((EncMode)em, islice == 1, base == 1, trans,
                                              &intra, &distAng);
                    oracleIntraLevelsDefault(em, islice == 1, base == 1, trans, &eIntra, &eDistAng);
                    CHECK(intra == eIntra);
                    CHECK(distAng == eDistAng);
                    for (int flat = 0; flat <= 1; ++flat) {
                        getIntraModeLevelsRtc((EncMode)em, islice == 1, trans, flat == 1, &intra,
                                              &distAng);
                        oracleIntraLevelsRtc(em, islice == 1, trans, flat == 1, &eIntra, &eDistAng);
                        CHECK(intra == eIntra);
                        CHECK(distAng == eDistAng);
                    }
                }
    // Hand pins on the band edges (the :5391/:5412 flat-ipp splits).
    uint32_t intra, distAng, eIntra, eDistAng;
    getIntraModeLevelsRtc(ENC_M8, false, 0, true, &intra, &distAng);
    oracleIntraLevelsRtc((int)ENC_M8, false, 0, true, &eIntra, &eDistAng);
    CHECK(intra == eIntra);  // flat M8 -> first band
    getIntraModeLevelsRtc(ENC_M8, false, 0, false, &intra, &distAng);
    oracleIntraLevelsRtc((int)ENC_M8, false, 0, false, &eIntra, &eDistAng);
    CHECK(intra == eIntra);  // non-flat M8 -> the <= M8 band
    getIntraModeLevelsDefault(ENC_M13, false, 1, 0, &intra, &distAng);
    CHECK(intra == 9);
    CHECK(distAng == 0);
}

TEST_CASE("set_inter_intra_ctrls - the three levels, whole-struct") {
    // :5455-5481. Level 0 all zero; level 1 (1,1,1,1); level 2
    // (1,0,0,2); default writes nothing.
    for (int level = 0; level <= 3; ++level) {
        InterIntraCompCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setInterIntraCtrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.enabled == 0);
            CHECK(c.useRdModel == 0);
            CHECK(c.wedgeModeSq == 0);
            CHECK(c.wedgeModeNsq == 0);
        } else if (level == 1) {
            CHECK(c.enabled == 1);
            CHECK(c.useRdModel == 1);
            CHECK(c.wedgeModeSq == 1);
            CHECK(c.wedgeModeNsq == 1);
        } else if (level == 2) {
            CHECK(c.enabled == 1);
            CHECK(c.useRdModel == 0);
            CHECK(c.wedgeModeSq == 0);
            CHECK(c.wedgeModeNsq == 2);
        } else {
            CHECK(c.enabled == kPoison);
            CHECK(c.useRdModel == kPoison);
            CHECK(c.wedgeModeSq == kPoison);
            CHECK(c.wedgeModeNsq == kPoison);
        }
    }
}

TEST_CASE("set_pd0_ctrls - every level x the hbd early-out, whole-struct") {
    // :5483-5601. PD0_LVL_* = 0..6. The detector/ref/var/edge/shift
    // arrays: unwritten slots keep poison; written slots per the switch.
    const uint16_t poison16 = 0xAAAA;
    for (int hbd = 0; hbd <= 1; ++hbd)
        for (int level = 0; level <= 9; ++level) {
            Pd0Ctrls c;
            std::memset(&c, kPoison, sizeof(c));
            setPd0Ctrls(c, (uint8_t)level, (uint8_t)hbd);
            if (hbd) {
                // :5487-5490 - the early-out: pd0_level forced to
                // PD0_LVL_0, everything else untouched.
                CHECK(c.pd0Level == 0);
                CHECK(c.useRefInfo[0] == kPoison);
                CHECK(c.me8x8CostVarianceTh[6] == 0xAAAAAAAAu);
                continue;
            }
            int eLevel = -1;
            int eDet[7], eRef[7];
            uint32_t eVar[7], eEdge[7];
            uint16_t eShift[7];
            for (int i = 0; i < 7; ++i) {
                eDet[i] = -1; eRef[i] = -1; eVar[i] = 0xAAAAAAAAu;
                eEdge[i] = 0xAAAAAAAAu; eShift[i] = poison16;
            }
            if (level == 0) {
                eLevel = 0; eDet[0] = 0;
            } else if (level == 1) {
                eLevel = 1; eDet[0] = 0; eDet[1] = 0;
            } else if (level == 2) {
                eLevel = 2; eDet[0] = 0; eDet[1] = 0; eDet[2] = 0;
            } else if (level == 3) {
                eLevel = 3; eDet[0] = 0; eDet[1] = 0; eDet[2] = 0; eDet[3] = 0;
            } else if (level == 4) {
                eLevel = 4;
                eDet[0] = 0; eDet[1] = 0; eDet[2] = 0; eDet[3] = 0; eDet[4] = 1;
                eRef[4] = 2; eVar[4] = 250000; eEdge[4] = 16384; eShift[4] = 3;
            } else if (level == 5) {
                eLevel = 5;
                eDet[0] = 0; eDet[1] = 0; eDet[2] = 0; eDet[3] = 0; eDet[4] = 1;
                eRef[4] = 2; eVar[4] = 250000; eEdge[4] = 16384; eShift[4] = 3;
                eDet[5] = 1; eRef[5] = 1; eVar[5] = 250000 >> 1; eEdge[5] = 16384; eShift[5] = 2;
            } else if (level == 6) {
                eLevel = 5;
                eDet[0] = 0; eDet[1] = 0; eDet[2] = 0; eDet[3] = 0; eDet[4] = 0; eDet[5] = 1;
                eRef[5] = 0; eVar[5] = 500000; eEdge[5] = 16384; eShift[5] = 2;
            } else if (level == 7) {
                eLevel = 6;
                eDet[0] = 0; eDet[1] = 0; eDet[2] = 0; eDet[3] = 0; eDet[4] = 0; eDet[5] = 1;
                eRef[5] = 0; eVar[5] = 500000 << 1; eEdge[5] = (uint32_t)~0; eShift[5] = (uint16_t)~0;
                eDet[6] = 1; eRef[6] = 1; eVar[6] = 250000; eEdge[6] = 16384; eShift[6] = 2;
            } else if (level == 8) {
                eLevel = 6;
                eDet[0] = 0; eDet[1] = 0; eDet[2] = 0; eDet[3] = 0; eDet[4] = 0; eDet[5] = 1;
                eRef[5] = 0; eVar[5] = 500000 << 1; eEdge[5] = (uint32_t)~0; eShift[5] = (uint16_t)~0;
                eDet[6] = 1; eRef[6] = 2; eVar[6] = 250000; eEdge[6] = 16384; eShift[6] = 2;
            }  // level 9: the default arm writes nothing
            if (level <= 8) {
                CHECK(c.pd0Level == (uint8_t)eLevel);
                for (int i = 0; i < 7; ++i) {
                    if (eDet[i] >= 0) CHECK(c.usePd0Detector[i] == (eDet[i] != 0));
                    if (eRef[i] >= 0) CHECK(c.useRefInfo[i] == (uint8_t)eRef[i]);
                    CHECK(c.me8x8CostVarianceTh[i] == eVar[i]);
                    CHECK(c.edgeDistTh[i] == eEdge[i]);
                    CHECK(c.neighMeDistShift[i] == eShift[i]);
                }
            } else {
                CHECK(c.pd0Level == kPoison);
                CHECK(c.me8x8CostVarianceTh[4] == 0xAAAAAAAAu);
                CHECK(c.edgeDistTh[5] == 0xAAAAAAAAu);
            }
        }
}

// set_lpd1_ctrls, independent transcription (:5603-5937). Per case, the
// written sub-rows; written[k] = false means poison stands.
struct OracleLpd1Row {
    bool written;
    int det, ref;
    uint32_t dist, rate, nz;
    uint16_t mv;
    uint32_t var;
    uint32_t skip;
    uint16_t shift;
};
static const uint64_t kRate26000 = 6000 + 40 * 500;
static const uint64_t kRate56000 = 6000 + 100 * 500;
static const uint64_t kRate262000 = 6000 + 512 * 500;
static const uint64_t kRate68500 = 6000 + 125 * 500;
static const uint64_t kRate4102000 = 6000 + 8192 * 500;
static const uint64_t kRate2054000 = 6000 + 4096 * 500;
static const uint64_t kRate518000 = 6000 + 1024 * 500;

static OracleLpd1Row oracleLpd1Rows(int level, int row) {
    OracleLpd1Row r = {};
    r.written = false;
    r.det = -1; r.ref = -1;
    r.dist = 0xAAAAAAAAu; r.rate = 0xAAAAAAAAu; r.nz = 0xAAAAAAAAu;
    r.mv = 0xAAAA; r.var = 0xAAAAAAAAu; r.skip = 0xAAAAAAAAu; r.shift = 0xAAAA;
    // Case N writes detector rows [0..pd1_level]; the pd1_level mapping
    // is the ePd1 table below (cases 1-3 sit on LPD1_LVL_0, case 5 jumps
    // to LPD1_LVL_3), NOT level-1.
    static const int ePd1[9] = {-1, 0, 0, 0, 1, 3, 4, 5, 6};
    if (level < 1 || level > 8 || row > ePd1[level]) return r;
    r.written = true;
    r.det = 1;
    switch (level) {
    case 1:
        r.ref = 1; r.dist = 25; r.rate = (uint32_t)kRate26000; r.nz = 0; r.mv = 300;
        r.var = 250000 >> 3; r.skip = 1024; r.shift = 1;
        break;
    case 2:
        r.ref = 1; r.dist = 25; r.rate = (uint32_t)kRate26000; r.nz = 16; r.mv = 300;
        r.var = 250000 >> 3; r.skip = 1024; r.shift = 1;
        break;
    case 3:
        r.ref = 0; r.dist = 35; r.rate = (uint32_t)kRate56000; r.nz = 16; r.mv = 900;
        r.var = 750000 >> 3; r.skip = 16384; r.shift = 3;
        break;
    case 4:
        if (row == 0) {
            r.ref = 0; r.dist = 256 << 4; r.rate = (uint32_t)kRate262000; r.nz = 32; r.mv = 2048;
            r.var = 500000; r.skip = 16384; r.shift = 3;
        } else {
            r.ref = 1; r.dist = 256 << 1; r.rate = (uint32_t)kRate68500; r.nz = 32; r.mv = 1600;
            r.var = 500000 >> 3; r.skip = 16384; r.shift = 3;
        }
        break;
    case 5:
        if (row == 0) {
            r.ref = 0; r.dist = 256 << 10; r.rate = (uint32_t)kRate4102000; r.nz = 512;
            r.mv = 2048 * 16; r.var = (uint32_t)~0; r.skip = 16384 * 7; r.shift = 5;
        } else if (row == 1) {
            r.ref = 0; r.dist = 256 << 8; r.rate = (uint32_t)kRate2054000; r.nz = 256;
            r.mv = 2048 * 8; r.var = (uint32_t)~0; r.skip = 16384 * 6; r.shift = 5;
        } else if (row == 2) {
            r.ref = 0; r.dist = 256 << 8; r.rate = (uint32_t)kRate2054000; r.nz = 164;
            r.mv = 2048 * 8; r.var = (uint32_t)~0; r.skip = 16384 * 6; r.shift = 5;
        } else {
            r.ref = 1; r.dist = 256 << 8; r.rate = (uint32_t)kRate2054000; r.nz = 128;
            r.mv = 2048 * 8; r.var = (uint32_t)~0; r.skip = 16384 * 6; r.shift = 5;
        }
        break;
    case 6:
        if (row <= 1) {
            r.ref = 0; r.dist = 256 << 10; r.rate = (uint32_t)kRate4102000; r.nz = 512;
            r.mv = 2048 * 16; r.var = (uint32_t)~0; r.skip = 16384 * 7; r.shift = 5;
        } else if (row == 2) {
            r.ref = 0; r.dist = 256 << 10; r.rate = (uint32_t)kRate4102000; r.nz = 256;
            r.mv = 2048 * 16; r.var = (uint32_t)~0; r.skip = 16384 * 7; r.shift = 5;
        } else if (row == 3) {
            r.ref = 0; r.dist = 256 << 10; r.rate = (uint32_t)kRate4102000; r.nz = 164;
            r.mv = 2048 * 16; r.var = (uint32_t)~0; r.skip = 16384 * 7; r.shift = 5;
        } else {
            r.ref = 1; r.dist = 256 << 4; r.rate = (uint32_t)kRate518000; r.nz = 128; r.mv = 2048;
            r.var = (uint32_t)~0; r.skip = 16384 * 2; r.shift = 3;
        }
        break;
    case 7:
        if (row == 0) {
            r.ref = 0; r.dist = 256 << 10; r.rate = (uint32_t)kRate4102000; r.nz = 256;
            r.mv = 2048 * 16; r.var = (uint32_t)~0; r.skip = (uint32_t)~0; r.shift = (uint16_t)~0;
        } else if (row == 1) {
            r.ref = 0; r.dist = 256 << 10; r.rate = (uint32_t)kRate4102000; r.nz = 128;
            r.mv = 2048 * 16; r.var = (uint32_t)~0; r.skip = (uint32_t)~0; r.shift = (uint16_t)~0;
        } else if (row == 2 || row == 3) {
            r.ref = 0; r.dist = 256 << 10; r.rate = (uint32_t)kRate4102000; r.nz = 96;
            r.mv = 2048 * 16; r.var = (uint32_t)~0; r.skip = (uint32_t)~0; r.shift = (uint16_t)~0;
        } else {
            r.ref = 1; r.dist = 256 << 8; r.rate = (uint32_t)kRate2054000; r.nz = 64;
            r.mv = 2048 * 8; r.var = (uint32_t)~0; r.skip = 16384 * 6; r.shift = 5;
        }
        break;
    case 8:
        if (row == 0) {
            r.ref = 0; r.dist = 256 << 10; r.rate = (uint32_t)kRate4102000; r.nz = 256;
            r.mv = 2048 * 16; r.var = (uint32_t)~0; r.skip = (uint32_t)~0; r.shift = (uint16_t)~0;
        } else if (row == 1) {
            r.ref = 0; r.dist = 256 << 10; r.rate = (uint32_t)kRate4102000; r.nz = 128;
            r.mv = 2048 * 16; r.var = (uint32_t)~0; r.skip = (uint32_t)~0; r.shift = (uint16_t)~0;
        } else if (row == 2 || row == 3) {
            r.ref = 0; r.dist = 256 << 10; r.rate = (uint32_t)kRate4102000; r.nz = 96;
            r.mv = 2048 * 16; r.var = (uint32_t)~0; r.skip = (uint32_t)~0; r.shift = (uint16_t)~0;
        } else {  // rows 4-6 identical
            r.ref = 1; r.dist = 256 << 8; r.rate = (uint32_t)kRate2054000; r.nz = 64;
            r.mv = 2048 * 8; r.var = (uint32_t)~0; r.skip = 16384 * 6; r.shift = 5;
        }
        break;
    }
    return r;
}

TEST_CASE("set_lpd1_ctrls - every level x every sub-row, vs the oracle") {
    // :5603-5937. Case N writes pd1_level = LPD1_LVL_{N-1} and rows
    // [0..N-1]; case 0 writes ONLY pd1_level = REGULAR_PD1; the default
    // arm (9) writes nothing.
    for (int level = 0; level <= 9; ++level) {
        Lpd1Ctrls c;
        std::memset(&c, kPoison, sizeof(c));
        setLpd1Ctrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.pd1Level == (uint8_t)REGULAR_PD1);
            CHECK(c.useRefInfo[0] == kPoison);
            CHECK(c.costThDist[6] == 0xAAAAAAAAu);
            continue;
        }
        if (level <= 8) {
            // The pd1_level mapping is NOT level-1: cases 1-3 all sit on
            // LPD1_LVL_0 (they refine row 0 only), case 5 jumps to
            // LPD1_LVL_3 (:5690), cases 6-8 continue 4/5/6.
            static const int ePd1[9] = {-1, 0, 0, 0, 1, 3, 4, 5, 6};
            CHECK(c.pd1Level == (uint8_t)ePd1[level]);
            for (int row = 0; row < 7; ++row) {
                const OracleLpd1Row& r = oracleLpd1Rows(level, row);
                if (r.written) {
                    CHECK(c.useLpd1Detector[row] == (r.det != 0));
                    CHECK(c.useRefInfo[row] == (uint8_t)r.ref);
                    CHECK(c.costThDist[row] == r.dist);
                    CHECK(c.costThRate[row] == r.rate);
                    CHECK(c.nzCoeffTh[row] == r.nz);
                    CHECK(c.maxMvLength[row] == r.mv);
                    CHECK(c.me8x8CostVarianceTh[row] == r.var);
                    CHECK(c.skipPd0EdgeDistTh[row] == r.skip);
                    CHECK(c.skipPd0MeShift[row] == r.shift);
                } else {
                    CHECK(c.costThDist[row] == 0xAAAAAAAAu);
                    CHECK(c.skipPd0EdgeDistTh[row] == 0xAAAAAAAAu);
                    CHECK(c.skipPd0MeShift[row] == 0xAAAA);
                }
            }
        } else {
            CHECK(c.pd1Level == kPoison);
            CHECK(c.costThDist[0] == 0xAAAAAAAAu);
            CHECK(c.skipPd0MeShift[6] == 0xAAAA);
        }
    }
}

// ---------------------------------------------------------------------------
// EMC2 rung 4: set_bipred3x3_controls (:5939-:5974) +
// set_dist_based_ref_pruning_controls (:5976-:6231).
// ---------------------------------------------------------------------------

TEST_CASE("set_bipred3x3_controls - the five levels, whole-struct") {
    // :5939-5974. Level 0 writes ONLY enabled; 1-4 the full surface;
    // default writes nothing.
    for (int level = 0; level <= 5; ++level) {
        Bipred3x3Ctrls c;
        std::memset(&c, kPoison, sizeof(c));
        setBipred3x3Ctrls(c, (uint8_t)level);
        if (level == 0) {
            CHECK(c.enabled == 0);
            CHECK(c.searchDiag == kPoison);
            CHECK(c.useBestList == kPoison);
            CHECK(c.useL0L1Dev == kPoison);
        } else if (level == 1) {
            CHECK(c.enabled == 1);
            CHECK(c.searchDiag == 1);
            CHECK(c.useBestList == 0);
            CHECK(c.useL0L1Dev == (uint8_t)~0);
        } else if (level == 2) {
            CHECK(c.enabled == 1);
            CHECK(c.searchDiag == 0);
            CHECK(c.useBestList == 0);
            CHECK(c.useL0L1Dev == (uint8_t)~0);
        } else if (level == 3) {
            CHECK(c.enabled == 1);
            CHECK(c.searchDiag == 0);
            CHECK(c.useBestList == 1);
            CHECK(c.useL0L1Dev == (uint8_t)~0);
        } else if (level == 4) {
            CHECK(c.enabled == 1);
            CHECK(c.searchDiag == 0);
            CHECK(c.useBestList == 1);
            CHECK(c.useL0L1Dev == 20);
        } else {
            CHECK(c.enabled == kPoison);
            CHECK(c.useL0L1Dev == kPoison);
        }
    }
}

// set_dist_based_ref_pruning_controls, independent transcription
// (:5976-6231). Per level: the 11 max_dev_to_best entries (0xFFFFFFFF =
// (uint32)~0), tpl offset, check multiplier, closest flag.
struct OracleRefPruneRow {
    int en;
    uint32_t dev[11];
    int tpl;      // -1 = not written
    int check;    // -1 = not written
    int closest;  // -1 = not written
};
static const OracleRefPruneRow oracleRefPruneRows[9] = {
    // level 0: enabled 0 only (the dev/tpl/check/closest surface keeps
    // the struct's prior state - poison in the test)
    {0, {0xAAAAAAAAu, 0xAAAAAAAAu, 0xAAAAAAAAu, 0xAAAAAAAAu, 0xAAAAAAAAu, 0xAAAAAAAAu,
         0xAAAAAAAAu, 0xAAAAAAAAu, 0xAAAAAAAAu, 0xAAAAAAAAu, 0xAAAAAAAAu},
     0xAA, 0xAA, 0xAA},
    // level 1: everything wide open
    {1, {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
         0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu},
     0, 0, 1},
    // level 2: PRED_ME capped at 150
    {1, {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 150,
         0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu},
     0, 0, 1},
    // level 3
    {1, {30, 30, 30, 0xFFFFFFFFu, 0xFFFFFFFFu, 60, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 30, 30},
     0, 0, 1},
    // level 4
    {1, {30, 30, 30, 30, 30, 30, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 30, 30},
     20, 1, 1},
    // level 5
    {1, {30, 0, 30, 30, 30, 30, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 30, 0},
     20, 1, 1},
    // level 6
    {1, {30, 0, 30, 30, 30, 30, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 30, 0},
     20, 1, 1},
    // level 7
    {1, {10, 0, 10, 10, 10, 10, 0xFFFFFFFFu, 0xFFFFFFFFu, 10, 10, 0},
     20, 1, 1},
    // level 8: only GLOBAL stays open
    {1, {0, 0, 0, 0, 0, 0, 0xFFFFFFFFu, 0, 0, 0, 0},
     20, 1, 1},
};

TEST_CASE("set_dist_based_ref_pruning_controls - every level x all 11 groups, vs the oracle") {
    // :5976-6231. Level 0 writes ONLY enabled; 1-8 write the full
    // surface; the default arm (9) writes nothing.
    for (int level = 0; level <= 9; ++level) {
        RefPruningCtrls c;
        std::memset(&c, kPoison, sizeof(c));
        setDistBasedRefPruningCtrls(c, (uint8_t)level);
        if (level <= 8) {
            const OracleRefPruneRow& r = oracleRefPruneRows[level];
            CHECK(c.enabled == r.en);
            for (int g = 0; g < 11; ++g) CHECK(c.maxDevToBest[g] == r.dev[g]);
            CHECK(c.useTplInfoOffset == (uint8_t)r.tpl);
            CHECK(c.checkClosestMultiplier == (uint8_t)r.check);
            for (int g = 0; g < 11; ++g) CHECK(c.closestRefs[g] == (uint8_t)r.closest);
        } else {
            CHECK(c.enabled == kPoison);
            CHECK(c.maxDevToBest[kPaMeGroup] == 0xAAAAAAAAu);
            CHECK(c.useTplInfoOffset == kPoison);
            CHECK(c.closestRefs[kInterCompGroup] == kPoison);
        }
    }
}
