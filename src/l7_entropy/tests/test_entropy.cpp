#include <doctest.h>

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