#include <doctest.h>
#include <cmath>
#include <cstdint>

#include "rc_arith.h"

// A9R C4: the tpl level pair of initial_rc_process.c.
//   svt_aom_get_tpl_group_level (initial_rc_process.c:190-202) -
//   non-static in the reference.
//   get_tpl_params_level (initial_rc_process.c:307-317) - static in the
//   reference; exported for testability (the DF1 signedCharClamp precedent).
//
// Both are pure mode-band selectors over the EncMode domain
// (EbSvtAv1Enc.h:44-61: ENC_MR = -1 through ENC_M13 = 13). The oracle is a
// test-side transcription of the two reference bodies, enumerated over the
// FULL enc-mode domain including the negative ENC_MR research mode (whose
// -1 lands in the first "<= ENC_M5" / "<= ENC_M2" branch).

namespace {

// svt_aom_get_tpl_group_level (initial_rc_process.c:190-202).
std::uint8_t oracleTplGroupLevel(int tpl, int encMode) {
    std::uint8_t tplGroupLevel;
    if (!tpl) {
        tplGroupLevel = 0;
    } else if (encMode <= l15_rc::kEncM5) {
        tplGroupLevel = 1;
    } else if (encMode <= l15_rc::kEncM8) {
        tplGroupLevel = 3;
    } else {
        tplGroupLevel = 4;
    }
    return tplGroupLevel;
}

// get_tpl_params_level (initial_rc_process.c:307-317).
std::uint8_t oracleTplParamsLevel(int encMode) {
    std::uint8_t tplParamsLevel;
    if (encMode <= l15_rc::kEncM2) {
        tplParamsLevel = 1;
    } else if (encMode <= l15_rc::kEncM7) {
        tplParamsLevel = 4;
    } else {
        tplParamsLevel = 5;
    }
    return tplParamsLevel;
}

}  // namespace

TEST_CASE("get_tpl_group_level matches the oracle over the full enc-mode domain") {
    int bad = 0;
    for (int tpl = 0; tpl <= 1; ++tpl) {
        for (int mode = -1; mode <= 13; ++mode) {
            if (l15_rc::getTplGroupLevel((std::uint8_t)tpl, (std::int8_t)mode) !=
                oracleTplGroupLevel(tpl, mode))
                ++bad;
        }
    }
    CHECK(bad == 0);
    // boundary anchors: tpl off is level 0; the M5/M8 bands split 1/3/4.
    CHECK(l15_rc::getTplGroupLevel(0, 0) == 0);
    CHECK(l15_rc::getTplGroupLevel(1, l15_rc::kEncM5) == 1);
    CHECK(l15_rc::getTplGroupLevel(1, l15_rc::kEncM5 + 1) == 3);
    CHECK(l15_rc::getTplGroupLevel(1, l15_rc::kEncM8) == 3);
    CHECK(l15_rc::getTplGroupLevel(1, l15_rc::kEncM8 + 1) == 4);
    // ENC_MR (-1) lands in the first band.
    CHECK(l15_rc::getTplGroupLevel(1, l15_rc::kEncMr) == 1);
    CHECK(l15_rc::getTplGroupLevel(1, l15_rc::kEncM13) == 4);
    // any non-zero tpl selects a band.
    CHECK(l15_rc::getTplGroupLevel(2, 0) == 1);
}

TEST_CASE("get_tpl_params_level matches the oracle over the full enc-mode domain") {
    int bad = 0;
    for (int mode = -1; mode <= 13; ++mode) {
        if (l15_rc::getTplParamsLevel((std::int8_t)mode) != oracleTplParamsLevel(mode)) ++bad;
    }
    CHECK(bad == 0);
    // boundary anchors: the M2/M7 bands split 1/4/5.
    CHECK(l15_rc::getTplParamsLevel(l15_rc::kEncM2) == 1);
    CHECK(l15_rc::getTplParamsLevel(l15_rc::kEncM2 + 1) == 4);
    CHECK(l15_rc::getTplParamsLevel(l15_rc::kEncM7) == 4);
    CHECK(l15_rc::getTplParamsLevel(l15_rc::kEncM7 + 1) == 5);
    CHECK(l15_rc::getTplParamsLevel(l15_rc::kEncMr) == 1);
    CHECK(l15_rc::getTplParamsLevel(l15_rc::kEncM13) == 5);
}

// ---- A9R C5: set_1pvbr_param (initial_rc_process.c:631-696) ----
//
// The mutex block/release (initial_rc_process.c:639, :667) is the named
// park (the rc->rc_mutex precedent). The stats_buf_ctx->stats_in_start
// wall threads as the single entry pointer (index picture_number).

namespace {

l15_rc::P1vbrCarrier pvbrBase(const std::uint32_t* meDist, const std::uint32_t* meVar,
                              std::uint16_t b64Count) {
    l15_rc::P1vbrCarrier pcs{};
    pcs.sliceType = 0;  // B_SLICE (not I_SLICE)
    pcs.pictureNumber = 9;
    pcs.b64TotalCount = b64Count;
    pcs.rcMeDistortion = meDist;
    pcs.me8x8CostVariance = meVar;
    pcs.inputResolution = l15_rc::kInputSize4KRange;  // above 480p: no res weight
    // the pre-existing entry content the mirror must copy BEFORE any write
    pcs.entry.statStruct.poc = 7;
    pcs.entry.statStruct.totalNumBits = 555;
    pcs.entry.statStruct.qindex = 40;
    pcs.entry.statStruct.worstQindex = 45;
    pcs.entry.statStruct.temporalLayerIndex = 1;
    pcs.entry.codedError = 3.5;
    pcs.entry.frame = 8.0;
    pcs.entry.duration = 1.0;
    pcs.entry.count = 1.0;
    return pcs;
}

}  // namespace

TEST_CASE("set_1pvbr_param on an I_SLICE only copies the entry stat struct") {
    const std::uint32_t meDist[2] = {100, 200};
    const std::uint32_t meVar[2] = {10, 20};
    l15_rc::P1vbrCarrier pcs = pvbrBase(meDist, meVar, 2);
    pcs.sliceType = l15_rc::kIslice;
    l15_rc::set1pVbrParam(pcs);
    // the mirror copied the entry...
    CHECK(pcs.statStruct.poc == 7);
    CHECK(pcs.statStruct.totalNumBits == 555);
    CHECK(pcs.statStruct.qindex == 40);
    // ...and the entry was NOT rewritten (no averaging on an I_SLICE).
    CHECK(pcs.entry.statStruct.totalNumBits == 555);
    CHECK(pcs.entry.statStruct.poc == 7);
    CHECK(pcs.entry.codedError == 3.5);
}

TEST_CASE("set_1pvbr_param no-weight path pins the averages, the floor and the error") {
    const std::uint32_t meDist[4] = {100, 200, 300, 400};  // avg 250
    const std::uint32_t meVar[4] = {10, 20, 30, 40};       // avg 25 <= 50000
    l15_rc::P1vbrCarrier pcs = pvbrBase(meDist, meVar, 4);
    l15_rc::set1pVbrParam(pcs);
    // total_num_bits = MAX(MIN_AVG_ME_DIST = 1000, avg 250) -> the floor.
    CHECK(pcs.entry.statStruct.totalNumBits == 1000);
    // coded_error = (double)avg_me_dist * b64_total_count * weight / 30:
    // 250 * 4 * 1.0 / 30 (test-side transcription, 1e-9 absolute).
    CHECK(std::fabs(pcs.entry.codedError - (250.0 * 4 * 1.0 / 30.0)) <= 1e-9);
    // the entry poc and the mirror writes.
    CHECK(pcs.entry.statStruct.poc == 9);
    CHECK(pcs.statStruct.poc == 9);
    // the mirror was copied BEFORE the entry rewrite: it keeps the old
    // total_num_bits 555 while the entry now holds 1000 (the ordering pin).
    CHECK(pcs.statStruct.totalNumBits == 555);
    CHECK(pcs.statStruct.qindex == 40);
}

TEST_CASE("set_1pvbr_param weight paths and the avg floor crossing") {
    // variance above the 50000 threshold -> weight 1.5. avg_me_dist 500 is
    // BELOW the MIN_AVG_ME_DIST floor -> total_num_bits stays 1000.
    const std::uint32_t meDistA[4] = {500, 500, 500, 500};
    const std::uint32_t meVarA[4] = {60000, 60000, 60000, 60000};
    l15_rc::P1vbrCarrier a = pvbrBase(meDistA, meVarA, 4);
    l15_rc::set1pVbrParam(a);
    CHECK(a.entry.statStruct.totalNumBits == 1000);
    CHECK(std::fabs(a.entry.codedError - (500.0 * 4 * 1.5 / 30.0)) <= 1e-9);
    // resolution <= 480p on top of the variance weight -> weight 1.5*1.5.
    l15_rc::P1vbrCarrier b = pvbrBase(meDistA, meVarA, 4);
    b.inputResolution = l15_rc::kInputSize480pRange;
    l15_rc::set1pVbrParam(b);
    CHECK(std::fabs(b.entry.codedError - (500.0 * 4 * 2.25 / 30.0)) <= 1e-9);
    // resolution <= 480p alone (variance below threshold) -> weight 1.5.
    const std::uint32_t meVarC[4] = {1, 1, 1, 1};
    l15_rc::P1vbrCarrier c = pvbrBase(meDistA, meVarC, 4);
    c.inputResolution = l15_rc::kInputSize480pRange;
    l15_rc::set1pVbrParam(c);
    CHECK(std::fabs(c.entry.codedError - (500.0 * 4 * 1.5 / 30.0)) <= 1e-9);
    // avg_me_dist ABOVE the floor passes through unclamped.
    const std::uint32_t meDistD[4] = {2000, 2000, 2000, 2000};
    l15_rc::P1vbrCarrier d = pvbrBase(meDistD, meVarC, 4);
    l15_rc::set1pVbrParam(d);
    CHECK(d.entry.statStruct.totalNumBits == 2000);
}
