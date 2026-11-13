#include <doctest.h>
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
