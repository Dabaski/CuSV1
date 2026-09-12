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

// svt_od_ec_encode_bool_q15 (bitstream_unit.c:252-269)
// Encode a single binary value.
// val: The value to encode (0 or 1).
// f: The probability that the val is one, scaled by 32768.
void odEcEncodeBoolQ15(OdEcEnc* enc, int val, std::uint32_t f);

// svt_od_ec_encode_cdf_q15 (bitstream_unit.c:279-301)
// Encodes a symbol given a cumulative distribution function (CDF) table in
// Q15. s: The index of the symbol to encode. icdf: 32768 minus the CDF.
// nsyms: The number of symbols in the alphabet (at most 16).
void odEcEncodeCdfQ15(OdEcEnc* enc, int s, const std::uint16_t* icdf, int nsyms);

// svt_od_ec_enc_tell (bitstream_unit.c:354-358)
// Returns the number of bits "used" by the encoded symbols so far.
int odEcEncTell(const OdEcEnc* enc);

// svt_od_ec_enc_tell_frac (bitstream_unit.c:406-408) -> svt_od_ec_tell_frac
// (bitstream_unit.c:369-395). Number of bits scaled by 2**OD_BITRES.
std::uint32_t odEcEncTellFrac(const OdEcEnc* enc);

// svt_od_ec_enc_done (bitstream_unit.c:309-343)
unsigned char* odEcEncDone(OdEcEnc* enc, std::uint32_t* nbytes);

}  // namespace entropy