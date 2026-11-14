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

// ---- A9D S11: initial_rc_process.c is_frame_already_exists (:161-168) +
// validate_pic_for_tpl (:171-188) + set_tpl_params (:319-401) +
// svt_aom_set_tpl_group (:204-305) ----

TEST_CASE("is_frame_already_exists walks the tpl group for the POC") {
    l15_rc::TplGroupPicView group[3] = {};
    group[0].pictureNumber = 10;
    group[1].pictureNumber = 20;
    group[2].pictureNumber = 30;
    CHECK(l15_rc::isFrameAlreadyExists(group, 0, 10) == false);
    CHECK(l15_rc::isFrameAlreadyExists(group, 2, 30) == false);  // end_index excludes [2]
    CHECK(l15_rc::isFrameAlreadyExists(group, 2, 20) == true);
    CHECK(l15_rc::isFrameAlreadyExists(group, 3, 30) == true);
}

TEST_CASE("validate_pic_for_tpl pins the reduced-group gate and the counters") {
    {
        // reduced_tpl_group = -1: EVERY unskipped, non-duplicate pic is
        // validated; used_tpl_frame_num counts them.
        l15_rc::TplControlsCarrier ctrls{};
        ctrls.reducedTplGroup = -1;
        l15_rc::TplGroupPicView group[3] = {};
        group[0].pictureNumber = 10;
        group[1].pictureNumber = 20;
        group[2].pictureNumber = 30;
        group[2].picSkipped = true;  // skipped: NOT validated
        std::uint8_t valid[4] = {0, 0, 0, 9};
        std::uint32_t used = 0;
        l15_rc::validatePicForTpl(ctrls, group, valid, used, 0);
        l15_rc::validatePicForTpl(ctrls, group, valid, used, 1);
        l15_rc::validatePicForTpl(ctrls, group, valid, used, 2);
        CHECK(valid[0] == 1);
        CHECK(valid[1] == 1);
        CHECK(valid[2] == 0);
        CHECK(valid[3] == 9);  // untouched
        CHECK(used == 2);
    }
    {
        // reduced_tpl_group = 1: only members at layer <= 1 validate.
        l15_rc::TplControlsCarrier ctrls{};
        ctrls.reducedTplGroup = 1;
        l15_rc::TplGroupPicView group[2] = {};
        group[0].pictureNumber = 10;
        group[0].temporalLayerIndex = 1;
        group[1].pictureNumber = 20;
        group[1].temporalLayerIndex = 2;
        std::uint8_t valid[2] = {0, 0};
        std::uint32_t used = 0;
        l15_rc::validatePicForTpl(ctrls, group, valid, used, 0);
        l15_rc::validatePicForTpl(ctrls, group, valid, used, 1);
        CHECK(valid[0] == 1);
        CHECK(valid[1] == 0);
        CHECK(used == 1);
    }
    {
        // the duplicate-POC gate: a member whose POC already appears earlier
        // in the group is NOT validated even with reduced = -1.
        l15_rc::TplControlsCarrier ctrls{};
        ctrls.reducedTplGroup = -1;
        l15_rc::TplGroupPicView group[2] = {};
        group[0].pictureNumber = 10;
        group[1].pictureNumber = 10;  // duplicate
        std::uint8_t valid[2] = {0, 0};
        std::uint32_t used = 0;
        l15_rc::validatePicForTpl(ctrls, group, valid, used, 0);
        l15_rc::validatePicForTpl(ctrls, group, valid, used, 1);
        CHECK(valid[0] == 1);
        CHECK(valid[1] == 0);
        CHECK(used == 1);
    }
}

TEST_CASE("set_tpl_params enumerates every level and the resolution fork") {
    const int res[3] = {l15_rc::kInputSize480pRange, l15_rc::kInputSize480pRange + 1, l15_rc::kInputSize4KRange};
    for (int r = 0; r < 3; ++r) {
        for (int lvl = 0; lvl <= 5; ++lvl) {
            l15_rc::TplControlsCarrier c{};
            c.intraModeEnd = -1;
            c.pfShape = -1;
            c.subpelDepth = -1;
            c.subpelDiagRefinement = -1;
            c.dispenserSearchLevel = -1;
            c.subsampleTx = -1;
            c.useSadInSrcSearch = -1;
            l15_rc::setTplParams(c, lvl, res[r]);
            switch (lvl) {
            case 0:
                CHECK(c.computeRate == 0);
                CHECK(c.enableTplQps == 0);
                CHECK(c.disableIntraPredNref == 0);
                CHECK(c.intraModeEnd == 0);  // DC_PRED
                CHECK(c.pfShape == 0);       // DEFAULT_SHAPE
                CHECK(c.useSadInSrcSearch == 0);
                CHECK(c.dispenserSearchLevel == 0);
                CHECK(c.subsampleTx == 0);
                CHECK(c.subpelDepth == 3);  // FULL_PEL
                CHECK(c.subpelDiagRefinement == 0);
                break;
            case 1:
                CHECK(c.computeRate == 1);
                CHECK(c.enableTplQps == 1);
                CHECK(c.disableIntraPredNref == 0);
                CHECK(c.intraModeEnd == 12);  // PAETH_PRED
                CHECK(c.pfShape == 0);
                CHECK(c.useSadInSrcSearch == 0);
                CHECK(c.subpelDepth == 1);  // QUARTER_PEL
                CHECK(c.subpelDiagRefinement == 0);
                break;
            case 2:
                CHECK(c.computeRate == 0);
                CHECK(c.enableTplQps == 0);
                CHECK(c.disableIntraPredNref == 1);
                CHECK(c.intraModeEnd == 12);
                CHECK(c.pfShape == (res[r] <= l15_rc::kInputSize480pRange ? 1 : 2));  // N2 / N4
                CHECK(c.useSadInSrcSearch == 1);
                CHECK(c.subpelDepth == 1);
                CHECK(c.subpelDiagRefinement == 0);
                break;
            case 3:
                CHECK(c.disableIntraPredNref == 1);
                CHECK(c.intraModeEnd == 0);  // DC_PRED
                CHECK(c.subpelDepth == 1);
                CHECK(c.subpelDiagRefinement == 4);
                break;
            case 4:
                CHECK(c.intraModeEnd == 0);
                CHECK(c.subpelDepth == 3);
                CHECK(c.subpelDiagRefinement == 4);
                CHECK(c.dispenserSearchLevel == 0);
                CHECK(c.subsampleTx == 0);
                break;
            case 5:
                CHECK(c.intraModeEnd == 0);
                CHECK(c.subpelDepth == 3);
                CHECK(c.subpelDiagRefinement == 4);
                CHECK(c.dispenserSearchLevel == 1);
                CHECK(c.subsampleTx == 2);
                break;
            }
        }
    }
}

TEST_CASE("set_tpl_group matches the oracle over the full configuration grid") {
    // The oracle: a test-side transcription of initial_rc_process.c
    // :204-305 (the ternary ladder + the r0 switch), enumerated over the
    // full grid: level 0..4 x hasPcs x slice x hier 0..5 x res {2,3} x
    // lad {0,1} x rc mode {0,1,2}.
    int bad = 0;
    for (int lvl = 0; lvl <= 4; ++lvl) {
        for (int hasPcs = 0; hasPcs <= 1; ++hasPcs) {
            for (int slice = 0; slice <= 1; ++slice) {
                for (int hier = 0; hier <= 5; ++hier) {
                    for (int ri = 0; ri < 2; ++ri) {
                        const int res = ri == 0 ? l15_rc::kInputSize480pRange : l15_rc::kInputSize4KRange;
                        for (int lad = 0; lad <= 1; ++lad) {
                            for (int rcm = 0; rcm <= 2; ++rcm) {
                                l15_rc::TplControlsCarrier got{};
                                const int gotBlk =
                                    l15_rc::setTplGroup(got, hasPcs != 0, slice, hier, res, lvl, 1920, 1080,
                                                        lad != 0, rcm);
                                // the reference's local TplControls = {0}:
                                // reduced starts at ZERO (not -1) - only the
                                // level cases that assign change it (the
                                // level-0 case leaves 0, so the r0 ladder
                                // RUNS for TPL-off groups - the quirk).
                                int wantRed = 0;
                                int wantBlk = 0;
                                double wantR0 = 0.0;
                                int wantEnable = 0;
                                switch (lvl) {
                                case 0:
                                    wantEnable = 0;
                                    break;
                                case 1:
                                    wantEnable = 1;
                                    wantRed = -1;
                                    wantBlk = 16;
                                    break;
                                case 2:
                                    wantEnable = 1;
                                    wantRed = hasPcs ? (slice ? -1 : (hier == 5 ? 4 : 3)) : -1;
                                    wantBlk = 16;
                                    break;
                                case 3:
                                    wantEnable = 1;
                                    wantRed = hasPcs ? (hier == 5 ? 4 : 3) : -1;
                                    wantBlk = 16;
                                    break;
                                case 4:
                                    wantEnable = 1;
                                    wantRed = hasPcs ? (hier == 5 ? (slice ? 2 : (res <= l15_rc::kInputSize480pRange ? 3 : 1))
                                                      : hier == 4 ? (slice ? 2 : (res <= l15_rc::kInputSize480pRange ? 2 : 1))
                                                                  : (slice ? 3 : (res <= l15_rc::kInputSize480pRange ? 2 : 0)))
                                                    : -1;
                                    wantBlk = (1920 < 1080 ? 1920 : 1080) >= 720 ? 32 : 16;
                                    break;
                                }
                                if (hasPcs) {
                                    if (hier <= wantRed) wantRed = -1;
                                    if (wantRed >= 0) {
                                        switch (hier - wantRed) {
                                        case 0:
                                        default: wantR0 = 0; break;
                                        case 1: wantR0 = hier <= 2 ? 0.4 : hier <= 3 ? 0.8 : 1.6; break;
                                        case 2: wantR0 = hier <= 2 ? 0.6 : hier <= 3 ? 1.2 : 2.4; break;
                                        case 3: wantR0 = hier <= 3 ? 1.4 : 2.8; break;
                                        case 4: wantR0 = 4.0; break;
                                        case 5: wantR0 = 6.0; break;
                                        }
                                        if (!lad) wantR0 *= 1.25;
                                    } else {
                                        wantR0 = 0;
                                        if (!lad) wantR0 = slice ? 0 : hier <= 2 ? 0.4 : hier <= 3 ? 0.8 : 1.6;
                                    }
                                    if (rcm == 1) {  // SVT_AV1_RC_MODE_VBR
                                        wantR0 *= 1.25;
                                        wantR0 = wantR0 < 3 ? wantR0 : 3;
                                    }
                                }
                                if (got.enable != wantEnable || got.reducedTplGroup != wantRed ||
                                    got.synthBlkSize != wantBlk || std::fabs(got.r0AdjustFactor - wantR0) > 1e-9 ||
                                    gotBlk != wantBlk) {
                                    ++bad;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    CHECK(bad == 0);
    // a few pinned absolutes out of the grid.
    {
        l15_rc::TplControlsCarrier c{};
        const int blk = l15_rc::setTplGroup(c, false, 0, 3, l15_rc::kInputSize4KRange, 4, 1920, 1080, true, 0);
        CHECK(blk == 32);  // the NULL-pcs early return with synth 32
        CHECK(c.r0AdjustFactor == 0.0);  // the NULL path touches no r0
        CHECK(c.reducedTplGroup == -1);
    }
}
