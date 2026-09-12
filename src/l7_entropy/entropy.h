#pragma once

#include <climits>
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
// OD_EC_WINDOW_SIZE (bitstream_unit.h:97); CHAR_BIT from <climits> above.
#define OD_EC_WINDOW_SIZE ((std::int32_t)sizeof(OdEcWindow) * CHAR_BIT)

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

// ---------------------------------------------------------------------------
// Decoder side: entdec.h:21-51 / entdec.c (pinned vendored aom_dsp subtree).
// ---------------------------------------------------------------------------
// The entropy decoder context (entdec.h:27-51).
struct OdEcDec {
    /*The start of the current input buffer.*/
    const unsigned char* buf;
    /*An offset used to keep track of tell after reaching the end of the stream.
      This is constant throughout most of the decoding process, but becomes
       important once we hit the end of the buffer and stop incrementing bptr
       (and instead pretend cnt has lots of bits).*/
    std::int32_t tell_offs;
    /*The end of the current input buffer.*/
    const unsigned char* end;
    /*The read pointer for the entropy-coded bits.*/
    const unsigned char* bptr;
    /*The difference between the high end of the current range, (low + rng), and
       the coded value, minus 1.
      This stores up to OD_EC_WINDOW_SIZE bits of that difference, but the
       decoder only uses the top 16 bits of the window to decode the next symbol.
      As we shift up during renormalization, if we don't have enough bits left in
       the window to fill the top 16, we'll read in more bits of the coded
       value.*/
    OdEcWindow dif;
    /*The number of values in the current range.*/
    std::uint16_t rng;
    /*The number of bits of data in the current value.*/
    std::int16_t cnt;
};

// od_ec_dec_init (entdec.c:143-153)
void odEcDecInit(OdEcDec* dec, const unsigned char* buf, std::uint32_t storage);

int odEcDecodeBoolQ15(OdEcDec* dec, unsigned f);

// od_ec_decode_bool_q15 (entdec.c:158-182)
// Decode a single binary value.
// f: The probability that the bit is one, scaled by 32768.
// Return: The value decoded (0 or 1).
int odEcDecodeBoolQ15(OdEcDec* dec, unsigned f);

// od_ec_decode_cdf_q15 (entdec.c:193-223)
// Decodes a symbol given an inverse cumulative distribution function (CDF)
// table in Q15. Return: The decoded symbol s.
int odEcDecodeCdfQ15(OdEcDec* dec, const std::uint16_t* icdf, int nsyms);

// AomCdfProb (cabac_context_model.h:31)
typedef std::uint16_t AomCdfProb;

// update_cdf (cabac_context_model.h:76-105)
// CDF adaptation: cdf[nsymbs] is the adaptation counter; the update rate is
// 4 + (count >> 4) + (nsymbs > 3) per the in-file spec derivation.
void updateCdf(AomCdfProb* cdf, int val, int nsymbs);

// od_ec_dec_tell (entdec.c:231-237)
// Returns the number of bits "used" by the decoded symbols so far.
int odEcDecTell(const OdEcDec* dec);

// svt_od_ec_enc_done (bitstream_unit.c:309-343)
unsigned char* odEcEncDone(OdEcEnc* enc, std::uint32_t* nbytes);

}  // namespace entropy