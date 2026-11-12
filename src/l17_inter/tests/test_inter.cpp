#include <doctest.h>

#include "inter_prediction.h"

TEST_CASE("the inter scale-factor bit constants pin inter_prediction.c:21-24") {
    CHECK(inter::kSubpelBits == 4);
    CHECK(inter::kScaleSubpelBits == 10);
    CHECK(inter::kScaleSubpelShifts == 1024);
    CHECK(inter::kScaleSubpelMask == 1023);
    CHECK(inter::kScaleExtraBits == 6);
}
