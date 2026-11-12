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
