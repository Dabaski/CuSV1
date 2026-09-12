// l7_entropy - host port of the SVT od_ec range coder (EC1).
// Encoder: Source/Lib/Codec/bitstream_unit.c (pinned vendored tree).
// Bodies are verbatim ports with camelCase names; member/field names keep
// their SVT spellings. Integer-only, bit-exact.

#include "entropy.h"

namespace entropy {

// svt_od_ec_enc_reset (bitstream_unit.c:185-197)
void odEcEncReset(OdEcEnc* enc) {
    enc->ptr = enc->buf;
    enc->low = 0;
    enc->rng = 0x8000;
    /*This is initialized to -9 so that it crosses zero after we've accumulated
       one byte + one carry bit.*/
    enc->cnt   = -9;
    enc->error = 0;
}

}  // namespace entropy