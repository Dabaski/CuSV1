#pragma once

#include <cstdint>

namespace entropy {

// ---------------------------------------------------------------------------
// od_ec probability/window plumbing (bitstream_unit.h:85-98, verbatim
// constants; CDF_PROB_BITS/CDF_PROB_TOP/AOM_ICDF from
// cabac_context_model.h:38-47).
// ---------------------------------------------------------------------------
#define CDF_PROB_BITS 15
#define CDF_PROB_TOP (1 << CDF_PROB_BITS)
#define AOM_ICDF(x) (CDF_PROB_TOP - (x))

// entcode.h (bitstream_unit.h:84-92)
#define EC_PROB_SHIFT 6
#define EC_MIN_PROB 4  // must be <= (1<<EC_PROB_SHIFT)/16
#define OD_BITRES (3)
#define OD_ICDF AOM_ICDF

// OdEcEnc - bitstream_unit.h:101-120 (verbatim member layout: low/rng/cnt/
// error/buf/ptr). OdEcWindow - bitstream_unit.h:96 (uint64_t window).
typedef std::uint64_t OdEcWindow;

struct OdEcEnc {
    OdEcWindow low;
    std::uint32_t rng;
    std::int16_t cnt;
    std::int16_t error;
    unsigned char* buf;
    unsigned char* ptr;
};

// svt_od_ec_enc_reset (bitstream_unit.c:185-197)
void odEcEncReset(OdEcEnc* enc);

// svt_od_ec_encode_bool_eq_q15 (bitstream_unit.c:232-247)
void odEcEncodeBoolEqQ15(OdEcEnc* enc, int val);

// svt_od_ec_enc_done (bitstream_unit.c:309-343)
unsigned char* odEcEncDone(OdEcEnc* enc, std::uint32_t* nbytes);

}  // namespace entropy