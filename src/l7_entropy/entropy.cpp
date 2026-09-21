// l7_entropy - host port of the SVT od_ec range coder (EC1).
// Encoder: Source/Lib/Codec/bitstream_unit.c (pinned vendored tree).
// Bodies are verbatim ports with camelCase names; member/field names keep
// their SVT spellings. Integer-only, bit-exact. MSVC toolchain: the
// OD_MEASURE_EC_OVERHEAD blocks are #if'd out upstream (0) and omitted;
// EB_UNLIKELY(x) resolves to (x) (definitions.h:508 MSVC branch);
// OD_WARN_UNUSED_RESULT/OD_ARG_NONNULL are empty attributes
// (bitstream_unit.h:57-75 MSVC branch); NOINLINE is an icache hint, dropped.

#include "entropy.h"

#include <cstring>
#include <intrin.h>
#include <cassert>
#include <climits>
#include <cstdio>

namespace entropy {

// get_msb, portable #else body (definitions.h:628-644): returns
// (int32_t)floor(log2(n)). n must be > 0. svt_log2f aliases this
// (definitions.h:592).
static inline int getMsb(std::uint32_t n) {
    int  log   = 0;
    std::uint32_t value = n;
    int  i;

    for (i = 4; i >= 0; --i) {
        const int      shift = (1 << i);
        const std::uint32_t x     = value >> shift;
        if (x != 0) {
            value = x;
            log += shift;
        }
    }
    return log;
}

// HToBE64(X) = BSwap64(X) on little-endian hosts (bitstream_unit.h:162,
// WORDS_BIGENDIAN branch dropped; MSVC _byteswap_uint64 path of
// bitstream_unit.h:211).
static inline std::uint64_t bswap64(std::uint64_t x) {
    return _byteswap_uint64(x);
}

// propagate_carry_bwd (bitstream_unit.c:77-79)
// ptr points one past the last written byte; propagate carry backward
static inline void propagateCarryBwd(unsigned char* ptr) {
    while (!++*--ptr) {}
}

// od_ec_enc_flush (bitstream_unit.c:110-144)
// Flush accumulated bytes from the arithmetic coder to the output buffer.
// This is the cold path of normalize, kept out-of-line to reduce icache
// pressure on the hot (no-flush) path.
// Returns the residual low value after flushing.
static void odEcEncFlush(OdEcEnc* enc, OdEcWindow low, unsigned rng, int c, int d) {
    // Need to add 1 byte here since enc->cnt always counts 1 byte less
    // (enc->cnt = -9) to ensure correct operation
    int s              = c + d;
    int num_bits_ready = (s & ~7) + 8;

    // Update "c" to contain the number of non-ready bits in "low". Since "low"
    // has 64-bit capacity, we need to add the (64 - 40) cushion bits and take
    // off the number of ready bits.
    c += 24 - num_bits_ready;

    // Extract ready bits from low
    std::uint64_t output = low >> c;

    // Separate carry bit from data
    std::uint64_t mask = (std::uint64_t)1 << num_bits_ready;

    if (output & mask) {
        propagateCarryBwd(enc->ptr);
    }

    // Write to buffer. Carry bit will be shifted away, no need to mask
    // output &= mask - 1;
    const std::uint64_t reg = bswap64(output << (64 - num_bits_ready));
    memcpy(enc->ptr, &reg, 8);

    enc->ptr += num_bits_ready >> 3;

    low &= (((std::uint64_t)1 << c) - 1);

    enc->low = low << d;
    enc->rng = rng << d;
    enc->cnt = static_cast<std::int16_t>((s & 7) - 8);
}

// svt_od_ec_enc_normalize (bitstream_unit.c:151-174)
// Takes updated low and range values, renormalizes them so that
// 32768 <= rng < 65536 (flushing bytes from low to the output buffer if
// necessary), and stores them back in the encoder context.
static inline void odEcEncNormalize(OdEcEnc* enc, OdEcWindow low, unsigned rng) {
    int c = enc->cnt;
    // assert(rng <= 65535U);
    /*The number of leading zeros in the 16-bit binary representation of rng.*/
    // svt_log2f(rng) = get_msb (definitions.h:592, portable body :628-644)
    int d = 15 - getMsb(rng);

    /* We flush every time "low" cannot safely and efficiently accommodate any
       more data. Overall, c must not exceed 63 at the time of byte flush out. To
       facilitate this, "c+d" cannot exceed 56-bits because we have to keep 1 byte
       for carry. Also, we need to subtract 16 because we want to keep room for
       the next symbol worth "d"-bits (max 15). An alternate condition would be if
       (e < d), where e = number of leading zeros in "low", indicating there is
       not enough rooom to accommodate "rng" worth of "d"-bits in "low". However,
       this approach needs additional computations: (i) compute "e", (ii) push
       the leading 0x00's as a special case.
    */
    if (c + d >= 40) {  // 56 - 16 (EB_UNLIKELY dropped, definitions.h:508)
        odEcEncFlush(enc, low, rng, c, d);
    } else {
        enc->low = low << d;
        enc->rng = rng << d;
        enc->cnt = static_cast<std::int16_t>(c + d);
    }
}

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

// svt_od_ec_encode_bool_eq_q15 (bitstream_unit.c:232-247)
// Encode a single binary value with 1/2 probability.
// val: The value to encode (0 or 1).
void odEcEncodeBoolEqQ15(OdEcEnc* enc, int val) {
    OdEcWindow l = enc->low;
    std::uint32_t r = enc->rng;
    std::uint32_t v = ((r >> 8) << (CDF_PROB_BITS - 1 - 7)) + EC_MIN_PROB;
    r -= v;
    if (val) {
        l += r;
        r = v;
    }
    odEcEncNormalize(enc, l, r);
}

// svt_od_ec_encode_bool_q15 (bitstream_unit.c:252-269)
// Encode a single binary value.
// val: The value to encode (0 or 1).
// f: The probability that the val is one, scaled by 32768.
void odEcEncodeBoolQ15(OdEcEnc* enc, int val, std::uint32_t f) {
    OdEcWindow l = enc->low;
    std::uint32_t r = enc->rng;
    // EB_ASSUME(f <= 32768) dropped (definitions.h:516 MSVC branch, no-op)
    std::uint32_t v = ((r >> 8) * (f >> EC_PROB_SHIFT) >> (7 - EC_PROB_SHIFT)) + EC_MIN_PROB;
    r -= v;
    if (val) {
        l += r;
        r = v;
    }
    odEcEncNormalize(enc, l, r);
}

// svt_od_ec_encode_cdf_q15 (bitstream_unit.c:279-301)
void odEcEncodeCdfQ15(OdEcEnc* enc, int s, const std::uint16_t* icdf, int nsyms) {
    OdEcWindow l = enc->low;
    std::uint32_t r = enc->rng;
    const std::uint32_t r_hi = r >> 8;
    const std::uint32_t temp = EC_MIN_PROB * (nsyms - 1 - s);
    if (0 < s) {
        std::uint32_t u = (r_hi * (icdf[s - 1] >> EC_PROB_SHIFT) >> (7 - EC_PROB_SHIFT)) + temp + EC_MIN_PROB;
        l += r - u;
        r = u;
    }
    r -= (r_hi * (icdf[s] >> EC_PROB_SHIFT) >> (7 - EC_PROB_SHIFT)) + temp;
    odEcEncNormalize(enc, l, r);
}

// svt_od_ec_enc_done (bitstream_unit.c:309-343)
// Indicates that there are no more symbols to encode.
// All remaining output bytes are flushed to the output buffer.
// odEcEncReset should be called before using the encoder again.
unsigned char* odEcEncDone(OdEcEnc* enc, std::uint32_t* nbytes) {
    int c = enc->cnt;

    /*We output the minimum number of bits that ensures that the symbols encoded
       thus far will be decoded correctly regardless of the bits that follow.*/
    OdEcWindow m = 0x3FFF;
    OdEcWindow e = ((enc->low + m) & ~m) | (m + 1);
    OdEcWindow v = e >> (c + 16);
    if (v & 0x0100) {
        propagateCarryBwd(enc->ptr);
    }
    do {
        *enc->ptr++ = (unsigned char)((e >> (c + 16)) & 0xFF);

        c -= 8;
    } while (10 + c > 0);

    *nbytes = (std::uint32_t)(enc->ptr - enc->buf);

    return enc->buf;
}

// svt_od_ec_enc_tell (bitstream_unit.c:354-358)
// Returns the number of bits "used" by the encoded symbols so far.
int odEcEncTell(const OdEcEnc* enc) {
    /*The 10 here counteracts the offset of -9 baked into cnt, and adds 1 extra
       bit, which we reserve for terminating the stream.*/
    return (enc->cnt + 10) + (int)(enc->ptr - enc->buf) * 8;
}

// svt_od_ec_tell_frac (bitstream_unit.c:369-395)
// Given the current total integer number of bits used and the current value
// of rng, computes the fraction number of bits used to OD_BITRES precision.
std::uint32_t odEcTellFrac(std::uint32_t nbitsTotal, std::uint32_t rng) {
    std::uint32_t nbits;
    int      l;
    int      i;
    /*To handle the non-integral number of bits still left in the encoder/decoder
       state, we compute the worst-case number of bits of val that must be
       encoded to ensure that the value is inside the range for any possible
       subsequent bits.
      The computation here is independent of val itself (the decoder does not
       even track that value), even though the real number of bits used after
       od_ec_enc_done() may be 1 smaller if rng is a power of two and the
       corresponding trailing bits of val are all zeros.
      If we did try to track that special case, then coding a value with a
       probability of 1/(1 << n) might sometimes appear to use more than n bits.
      This may help explain the surprising result that a newly initialized
       encoder or decoder claims to have used 1 bit.*/
    nbits = nbitsTotal << OD_BITRES;
    l     = 0;
    for (i = OD_BITRES; i-- > 0;) {
        int b;
        rng = rng * rng >> 15;
        b   = (int)(rng >> 16);
        l   = l << 1 | b;
        rng >>= b;
    }
    return nbits - l;
}

// svt_od_ec_enc_tell_frac (bitstream_unit.c:406-408)
std::uint32_t odEcEncTellFrac(const OdEcEnc* enc) {
    return odEcTellFrac((std::uint32_t)odEcEncTell(enc), enc->rng);
}

// OD_EC_LOTS_OF_BITS (entdec.c:74)
// This is meant to be a large, positive constant that can still be efficiently
//    loaded as an immediate (on platforms like ARM, for example).
// Even relatively modest values like 100 would work fine.
static const std::int32_t kOdEcLotsOfBits = 0x4000;

// od_ec_dec_refill (entdec.c:78-115)
// The return value of od_ec_dec_tell does not change across an od_ec_dec_refill
//    call.
static void odEcDecRefill(OdEcDec* dec) {
    std::int32_t s;
    OdEcWindow dif;
    std::int16_t cnt;
    const unsigned char* bptr;
    const unsigned char* end;
    dif = dec->dif;
    cnt = dec->cnt;
    bptr = dec->bptr;
    end = dec->end;
    s = OD_EC_WINDOW_SIZE - 9 - (cnt + 15);
    for (; s >= 0 && bptr < end; s -= 8, bptr++) {
        /*Each time a byte is inserted into the window (dif), bptr advances and cnt
           is incremented by 8, so the total number of consumed bits (the return
           value of od_ec_dec_tell) does not change.*/
        dif ^= (OdEcWindow)bptr[0] << s;
        cnt = static_cast<std::int16_t>(cnt + 8);
    }
    if (bptr >= end) {
        /*We've reached the end of the buffer. It is perfectly valid for us to need
           to fill the window with additional bits past the end of the buffer (and
           this happens in normal operation). These bits should all just be taken
           as zero. But we cannot increment bptr past 'end' (this is undefined
           behavior), so we start to increment dec->tell_offs. We also don't want
           to keep testing bptr against 'end', so we set cnt to OD_EC_LOTS_OF_BITS
           and adjust dec->tell_offs so that the total number of unconsumed bits in
           the window (dec->cnt - dec->tell_offs) does not change. This effectively
           puts lots of zero bits into the window, and means we won't try to refill
           it from the buffer for a very long time (at which point we'll put lots
           of zero bits into the window again).*/
        dec->tell_offs += kOdEcLotsOfBits - cnt;
        cnt = kOdEcLotsOfBits;
    }
    dec->dif = dif;
    dec->cnt = cnt;
    dec->bptr = bptr;
}

// od_ec_dec_normalize (entdec.c:125-138)
// Takes updated dif and range values, renormalizes them so that
// 32768 <= rng < 65536 (reading more bytes from the stream into dif if
// necessary), and stores them back in the decoder context.
static int odEcDecNormalize(OdEcDec* dec, OdEcWindow dif, unsigned rng, int ret) {
    // assert(rng <= 65535U);
    /*The number of leading zeros in the 16-bit binary representation of rng.*/
    // OD_ILOG_NZ(rng) = svt_log2f(rng) + 1 (bitstream_unit.h:55)
    int d = 16 - (getMsb(rng) + 1);
    /*d bits in dec->dif are consumed.*/
    dec->cnt = static_cast<std::int16_t>(dec->cnt - d);
    /*This is equivalent to shifting in 1's instead of 0's.*/
    dec->dif = ((dif + 1) << d) - 1;
    dec->rng = static_cast<std::uint16_t>(rng << d);
    if (dec->cnt < 0) odEcDecRefill(dec);
    return ret;
}

// od_ec_dec_init (entdec.c:143-153)
// Initializes the decoder.
// buf: The input buffer to use.
// storage: The size in bytes of the input buffer.
void odEcDecInit(OdEcDec* dec, const unsigned char* buf, std::uint32_t storage) {
    dec->buf = buf;
    dec->tell_offs = 10 - (OD_EC_WINDOW_SIZE - 8);
    dec->end = buf + storage;
    dec->bptr = buf;
    dec->dif = ((OdEcWindow)1 << (OD_EC_WINDOW_SIZE - 1)) - 1;
    dec->rng = 0x8000;
    dec->cnt = -15;
    odEcDecRefill(dec);
}

// od_ec_decode_bool_q15 (entdec.c:158-182)
// Decode a single binary value.
// f: The probability that the bit is one, scaled by 32768.
// Return: The value decoded (0 or 1).
int odEcDecodeBoolQ15(OdEcDec* dec, unsigned f) {
    OdEcWindow dif;
    OdEcWindow vw;
    unsigned r;
    unsigned r_new;
    unsigned v;
    int ret;
    dif = dec->dif;
    r = dec->rng;
    v = ((r >> 8) * (std::uint32_t)(f >> EC_PROB_SHIFT) >> (7 - EC_PROB_SHIFT));
    v += EC_MIN_PROB;
    vw = (OdEcWindow)v << (OD_EC_WINDOW_SIZE - 16);
    ret = 1;
    r_new = v;
    if (dif >= vw) {
        r_new = r - v;
        dif -= vw;
        ret = 0;
    }
    return odEcDecNormalize(dec, dif, r_new, ret);
}

// od_ec_decode_cdf_q15 (entdec.c:193-223)
// Decodes a symbol given an inverse cumulative distribution function (CDF)
// table in Q15.
// icdf: CDF_PROB_TOP minus the CDF, such that symbol s falls in the range
//        [s > 0 ? (CDF_PROB_TOP - icdf[s - 1]) : 0, CDF_PROB_TOP - icdf[s]).
//        The values must be monotonically non-increasing, and icdf[nsyms - 1]
//         must be 0.
// nsyms: The number of symbols in the alphabet.
//        This should be at most 16.
// Return: The decoded symbol s.
int odEcDecodeCdfQ15(OdEcDec* dec, const std::uint16_t* icdf, int nsyms) {
    OdEcWindow dif;
    unsigned r;
    unsigned c;
    unsigned u;
    unsigned v;
    int ret;
    dif = dec->dif;
    r = dec->rng;
    const int N = nsyms - 1;

    c = (unsigned)(dif >> (OD_EC_WINDOW_SIZE - 16));
    v = r;
    ret = -1;
    do {
        u = v;
        v = ((r >> 8) * (std::uint32_t)(icdf[++ret] >> EC_PROB_SHIFT) >>
             (7 - EC_PROB_SHIFT));
        v += EC_MIN_PROB * (N - ret);
    } while (c < v);
    r = u - v;
    dif -= (OdEcWindow)v << (OD_EC_WINDOW_SIZE - 16);
    return odEcDecNormalize(dec, dif, r, ret);
}

// update_cdf (cabac_context_model.h:76-105)
// CDF adaptation: rate is computed in the spec as
//  3 + ( cdf[N] > 15 ) + ( cdf[N] > 31 ) + Min(FloorLog2(N), 2)
// which this SVT tree reduces to 4 + (count >> 4) + (nsymbs > 3) (the
// in-file derivation, cabac_context_model.h:81-93).
void updateCdf(AomCdfProb* cdf, int val, int nsymbs) {
    const int count = cdf[nsymbs];
    cdf[nsymbs] = static_cast<std::uint16_t>(cdf[nsymbs] + (count < 32));

    const int rate = 4 + (count >> 4) + (nsymbs > 3);

    int i = 0;
    for (; i < val; i++) {
        cdf[i] = static_cast<std::uint16_t>(cdf[i] + ((CDF_PROB_TOP - cdf[i]) >> rate));
    }
    for (; i < nsymbs - 1; i++) {
        cdf[i] = static_cast<std::uint16_t>(cdf[i] - (cdf[i] >> rate));
    }
}

// od_ec_dec_tell (entdec.c:231-237)
// Returns the number of bits "used" by the decoded symbols so far.
int odEcDecTell(const OdEcDec* dec) {
    /*There is a window of bits stored in dec->dif. The difference
      (dec->bptr - dec->buf) tells us how many bytes have been read into this
      window. The difference (dec->cnt - dec->tell_offs) tells us how many of
      the bits in that window remain unconsumed.*/
    return (int)((dec->bptr - dec->buf) * 8 - dec->cnt + dec->tell_offs);
}

// aom_write_symbol (bitstream_unit.h:265-279)
void odEcWriteSymbol(AomWriter* w, int symb, AomCdfProb* cdf, int nsymbs) {
    if (nsymbs == 2) {
        // Binary CDF specialization: route directly to the optimal bool encoder.
        // For nsyms==2, the CDF encode path is provably equivalent to
        // svt_od_ec_encode_bool_q15(enc, symb, cdf[0]).
        // When nsymbs is a compile-time constant 2, this branch folds away.
        odEcEncodeBoolQ15(&w->ec, symb, cdf[0]);
    } else {
        odEcEncodeCdfQ15(&w->ec, symb, cdf, nsymbs);
    }

    if (w->allow_update_cdf) {
        updateCdf(cdf, symb, nsymbs);
    }
}

// aom_stop_encode (bitstream_unit.h:245-253)
void odEcStopEncode(AomWriter* w) {
    std::uint32_t bytes = 0;
    unsigned char* data = odEcEncDone(&w->ec, &bytes);
    if (!data) {
        return;
    }
    w->pos = bytes;
}

// aom_write_bit (bitstream_unit.h:255-257)
void odEcWriteLiteralBit(AomWriter* w, int bit, int unused_bits) {
    (void)unused_bits;
    odEcEncodeBoolEqQ15(&w->ec, bit);
}

// aom_write_literal (bitstream_unit.h:259-263)
void odEcWriteLiteralBits(AomWriter* w, unsigned data, int bits) {
    for (int bit = bits - 1; bit >= 0; bit--) {
        odEcWriteLiteralBit(w, 1 & (data >> bit), 0);
    }
}

// aom_read_bit (bitreader.h:71-75): aom_read(r, 128) -> od_ec_decode_bool_q15 at the half probability
int odEcReadBit(AomReader* r) {
    // aom_read_(r, 128): p = (0x7FFFFF - (128 << 15) + 128) >> 8 = (0x7FFFFF - 0x400000 + 0x80) >> 8
    const int p = (0x7FFFFF - (128 << 15) + 128) >> 8;
    return odEcDecodeBoolQ15(&r->ec, static_cast<unsigned>(p));
}

// aom_reader_init (bitreader.c:14-22)
int odEcReaderInit(AomReader* r, const unsigned char* buffer, std::uint32_t size) {
    if (size && !buffer) {
        return 1;
    }
    odEcDecInit(&r->ec, buffer, size);
    return 0;
}

// aom_read_cdf_ (bitreader.h:84-90)
int odEcReadCdf(AomReader* r, const AomCdfProb* cdf, int nsymbs) {
    // assert(cdf != NULL);
    return odEcDecodeCdfQ15(&r->ec, cdf, nsymbs);
}

// aom_read_symbol_ (bitreader.h:92-98)
int odEcReadSymbol(AomReader* r, AomCdfProb* cdf, int nsymbs) {
    int ret;
    ret = odEcReadCdf(r, cdf, nsymbs);
    if (r->allow_update_cdf) updateCdf(cdf, ret, nsymbs);
    return ret;
}

// ---------------------------------------------------------------------------
// EC3: intra-frame (key-frame) symbol surface.
// ---------------------------------------------------------------------------
// CDF initializer macros, verbatim (cabac_context_model.h:50-65). The
// AOM_CDF* tables below are then byte-identical text with the SVT source.
#define AOM_EXPAND_LIST(x) x
#define AOM_CDF2(a0) AOM_ICDF(a0), 0
#define AOM_CDF3(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF2(__VA_ARGS__))
#define AOM_CDF4(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF3(__VA_ARGS__))
#define AOM_CDF5(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF4(__VA_ARGS__))
#define AOM_CDF6(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF5(__VA_ARGS__))
#define AOM_CDF7(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF6(__VA_ARGS__))
#define AOM_CDF8(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF7(__VA_ARGS__))
#define AOM_CDF9(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF8(__VA_ARGS__))
#define AOM_CDF10(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF9(__VA_ARGS__))
#define AOM_CDF11(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF10(__VA_ARGS__))
#define AOM_CDF12(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF11(__VA_ARGS__))
#define AOM_CDF13(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF12(__VA_ARGS__))
#define AOM_CDF14(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF13(__VA_ARGS__))
#define AOM_CDF15(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF14(__VA_ARGS__))
#define AOM_CDF16(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF15(__VA_ARGS__))

// intra_mode_context (common_utils.c:134-148, verbatim)
const std::uint8_t intraModeContext[INTRA_MODES] = {
    0,
    1,
    2,
    3,
    4,
    4,
    4,
    4,
    3,
    0,
    1,
    2,
    0,
};

// block_size_wide / block_size_high (common_utils.c:286-291, verbatim)
const std::uint8_t blockSizeWide[BLOCK_SIZES_ALL] = {4, 4, 8, 8, 8, 16, 16, 16, 32, 32, 32,
                                                     64, 64, 64, 128, 128, 4, 16, 8, 32, 16, 64};

const std::uint8_t blockSizeHigh[BLOCK_SIZES_ALL] = {4, 8, 4, 8, 16, 8, 16, 32, 16, 32, 64,
                                                     32, 64, 128, 64, 128, 16, 4, 32, 8, 64, 16};

// svt_aom_default_kf_y_mode_cdf (cabac_context_model.c:59-85, verbatim)
static const AomCdfProb kf_y_mode_cdf_default[KF_MODE_CONTEXTS][KF_MODE_CONTEXTS][CDF_SIZE(INTRA_MODES)] = {
    {{AOM_CDF13(15588, 17027, 19338, 20218, 20682, 21110, 21825, 23244, 24189, 28165, 29093, 30466)},
     {AOM_CDF13(12016, 18066, 19516, 20303, 20719, 21444, 21888, 23032, 24434, 28658, 30172, 31409)},
     {AOM_CDF13(10052, 10771, 22296, 22788, 23055, 23239, 24133, 25620, 26160, 29336, 29929, 31567)},
     {AOM_CDF13(14091, 15406, 16442, 18808, 19136, 19546, 19998, 22096, 24746, 29585, 30958, 32462)},
     {AOM_CDF13(12122, 13265, 15603, 16501, 18609, 20033, 22391, 25583, 26437, 30261, 31073, 32475)}},
    {{AOM_CDF13(10023, 19585, 20848, 21440, 21832, 22760, 23089, 24023, 25381, 29014, 30482, 31436)},
     {AOM_CDF13( 5983, 24099, 24560, 24886, 25066, 25795, 25913, 26423, 27610, 29905, 31276, 31794)},
     {AOM_CDF13( 7444, 12781, 20177, 20728, 21077, 21607, 22170, 23405, 24469, 27915, 29090, 30492)},
     {AOM_CDF13( 8537, 14689, 15432, 17087, 17408, 18172, 18408, 19825, 24649, 29153, 31096, 32210)},
     {AOM_CDF13( 7543, 14231, 15496, 16195, 17905, 20717, 21984, 24516, 26001, 29675, 30981, 31994)}},
    {{AOM_CDF13(12613, 13591, 21383, 22004, 22312, 22577, 23401, 25055, 25729, 29538, 30305, 32077)},
     {AOM_CDF13( 9687, 13470, 18506, 19230, 19604, 20147, 20695, 22062, 23219, 27743, 29211, 30907)},
     {AOM_CDF13( 6183,  6505, 26024, 26252, 26366, 26434, 27082, 28354, 28555, 30467, 30794, 32086)},
     {AOM_CDF13(10718, 11734, 14954, 17224, 17565, 17924, 18561, 21523, 23878, 28975, 30287, 32252)},
     {AOM_CDF13( 9194,  9858, 16501, 17263, 18424, 19171, 21563, 25961, 26561, 30072, 30737, 32463)}},
    {{AOM_CDF13(12602, 14399, 15488, 18381, 18778, 19315, 19724, 21419, 25060, 29696, 30917, 32409)},
     {AOM_CDF13( 8203, 13821, 14524, 17105, 17439, 18131, 18404, 19468, 25225, 29485, 31158, 32342)},
     {AOM_CDF13( 8451,  9731, 15004, 17643, 18012, 18425, 19070, 21538, 24605, 29118, 30078, 32018)},
     {AOM_CDF13( 7714,  9048,  9516, 16667, 16817, 16994, 17153, 18767, 26743, 30389, 31536, 32528)},
     {AOM_CDF13( 8843, 10280, 11496, 15317, 16652, 17943, 19108, 22718, 25769, 29953, 30983, 32485)}},
    {{AOM_CDF13(12578, 13671, 15979, 16834, 19075, 20913, 22989, 25449, 26219, 30214, 31150, 32477)},
     {AOM_CDF13( 9563, 13626, 15080, 15892, 17756, 20863, 22207, 24236, 25380, 29653, 31143, 32277)},
     {AOM_CDF13( 8356,  8901, 17616, 18256, 19350, 20106, 22598, 25947, 26466, 29900, 30523, 32261)},
     {AOM_CDF13(10835, 11815, 13124, 16042, 17018, 18039, 18947, 22753, 24615, 29489, 30883, 32482)},
     {AOM_CDF13( 7618,  8288,  9859, 10509, 15386, 18657, 22903, 28776, 29180, 31355, 31802, 32593)}}
};

// default_angle_delta_cdf (cabac_context_model.c:87-96, verbatim)
static const AomCdfProb angle_delta_cdf_default[DIRECTIONAL_MODES][CDF_SIZE(2 * MAX_ANGLE_DELTA + 1)] = {
    {AOM_CDF7( 2180,  5032,  7567, 22776, 26989, 30217)},
    {AOM_CDF7( 2301,  5608,  8801, 23487, 26974, 30330)},
    {AOM_CDF7( 3780, 11018, 13699, 19354, 23083, 31286)},
    {AOM_CDF7( 4581, 11226, 15147, 17138, 21834, 28397)},
    {AOM_CDF7( 1737, 10927, 14509, 19588, 22745, 28823)},
    {AOM_CDF7( 2664, 10176, 12485, 17650, 21600, 30495)},
    {AOM_CDF7( 2240, 11096, 15453, 20341, 22561, 28917)},
    {AOM_CDF7( 3605, 10428, 12459, 17676, 21244, 30655)}
};

// default_filter_intra_mode_cdf (cabac_context_model.c:614-616, verbatim)
static const AomCdfProb filter_intra_mode_cdf_default[CDF_SIZE(FILTER_INTRA_MODES)] = {
    AOM_CDF5( 8949, 12776, 17211, 29558)
};

// default_filter_intra_cdfs (cabac_context_model.c:618-623, verbatim)
static const AomCdfProb filter_intra_cdfs_default[BLOCK_SIZES_ALL][CDF_SIZE(2)] = {
    {AOM_CDF2( 4621)}, {AOM_CDF2( 6743)}, {AOM_CDF2( 5893)}, {AOM_CDF2( 7866)}, {AOM_CDF2(12551)}, {AOM_CDF2( 9394)},
    {AOM_CDF2(12408)}, {AOM_CDF2(14301)}, {AOM_CDF2(12756)}, {AOM_CDF2(22343)}, {AOM_CDF2(16384)}, {AOM_CDF2(16384)},
    {AOM_CDF2(16384)}, {AOM_CDF2(16384)}, {AOM_CDF2(16384)}, {AOM_CDF2(16384)}, {AOM_CDF2(12770)}, {AOM_CDF2(10368)},
    {AOM_CDF2(20229)}, {AOM_CDF2(18101)}, {AOM_CDF2(16384)}, {AOM_CDF2(16384)}
};

// default_partition_cdf (cabac_context_model.c:134-155, verbatim)
static const AomCdfProb partition_cdf_default[PARTITION_CONTEXTS][CDF_SIZE(EXT_PARTITION_TYPES)] = {
    {AOM_CDF4(19132, 25510, 30392)},
    {AOM_CDF4(13928, 19855, 28540)},
    {AOM_CDF4(12522, 23679, 28629)},
    {AOM_CDF4( 9896, 18783, 25853)},
    {AOM_CDF10(15597, 20929, 24571, 26706, 27664, 28821, 29601, 30571, 31902)},
    {AOM_CDF10( 7925, 11043, 16785, 22470, 23971, 25043, 26651, 28701, 29834)},
    {AOM_CDF10( 5414, 13269, 15111, 20488, 22360, 24500, 25537, 26336, 32117)},
    {AOM_CDF10( 2662,  6362,  8614, 20860, 23053, 24778, 26436, 27829, 31171)},
    {AOM_CDF10(18462, 20920, 23124, 27647, 28227, 29049, 29519, 30178, 31544)},
    {AOM_CDF10( 7689,  9060, 12056, 24992, 25660, 26182, 26951, 28041, 29052)},
    {AOM_CDF10( 6015,  9009, 10062, 24544, 25409, 26545, 27071, 27526, 32047)},
    {AOM_CDF10( 1394,  2208,  2796, 28614, 29061, 29466, 29840, 30185, 31899)},
    {AOM_CDF10(20137, 21547, 23078, 29566, 29837, 30261, 30524, 30892, 31724)},
    {AOM_CDF10( 6732,  7490,  9497, 27944, 28250, 28515, 28969, 29630, 30104)},
    {AOM_CDF10( 5945,  7663,  8348, 28683, 29117, 29749, 30064, 30298, 32238)},
    {AOM_CDF10(  870,  1212,  1487, 31198, 31394, 31574, 31743, 31881, 32332)},
    {AOM_CDF8(27899, 28219, 28529, 32484, 32539, 32619, 32639)},
    {AOM_CDF8( 6607,  6990,  8268, 32060, 32219, 32338, 32371)},
    {AOM_CDF8( 5429,  6676,  7122, 32027, 32227, 32531, 32582)},
    {AOM_CDF8(  711,   966,  1172, 32448, 32538, 32617, 32664)}
};

// partition_context_lookup (definitions.h:1551-1573, verbatim values in
// BlockSize enum order: above/left bit masks)
const std::uint8_t partitionContextLookupAbove[BLOCK_SIZES_ALL] = {31, 31, 30, 30, 30, 28, 28, 28, 24, 24, 24, 16, 16, 16, 0, 0, 31, 28, 30, 24, 28, 16};

const std::uint8_t partitionContextLookupLeft[BLOCK_SIZES_ALL] = {31, 30, 31, 30, 28, 30, 28, 24, 28, 24, 16, 24, 16, 0, 16, 0, 28, 31, 24, 30, 16, 28};

// default_skip_cdfs (cabac_context_model.c:594-596, verbatim)
static const AomCdfProb skip_cdfs_default[SKIP_CONTEXTS][CDF_SIZE(2)] = {
    {AOM_CDF2(31671)}, {AOM_CDF2(16515)}, {AOM_CDF2(4576)}
};

// default token CDF tables (cabac_context_model.c, verbatim)
static const AomCdfProb txb_skip_cdfs_default[TOKEN_CDF_Q_CTXS][TX_SIZES][TXB_SKIP_CONTEXTS][CDF_SIZE(2)] = {
#include "txb_skip_cdfs_buckets.inc"
};
static const AomCdfProb dc_sign_cdfs_default[TOKEN_CDF_Q_CTXS][PLANE_TYPES][DC_SIGN_CONTEXTS][CDF_SIZE(2)] = {
#include "dc_sign_cdfs_buckets.inc"
};
static const AomCdfProb coeff_base_eob_multi_cdfs_default[TOKEN_CDF_Q_CTXS][TX_SIZES][PLANE_TYPES][SIG_COEF_CONTEXTS_EOB][CDF_SIZE(3)] = {
#include "coeff_base_eob_multi_cdfs_buckets.inc"
};
static const AomCdfProb coeff_base_multi_cdfs_default[TOKEN_CDF_Q_CTXS][TX_SIZES][PLANE_TYPES][SIG_COEF_CONTEXTS][CDF_SIZE(4)] = {
#include "coeff_base_multi_cdfs_buckets.inc"
};
static const AomCdfProb coeff_lps_multi_cdfs_default[TOKEN_CDF_Q_CTXS][TX_32X32 + 1][PLANE_TYPES][LEVEL_CONTEXTS][CDF_SIZE(BR_CDF_SIZE)] = {
#include "coeff_lps_multi_cdfs_buckets.inc"
};
static const AomCdfProb eob_extra_cdfs_default[TOKEN_CDF_Q_CTXS][TX_SIZES][PLANE_TYPES][EOB_COEF_CONTEXTS][CDF_SIZE(2)] = {
#include "eob_extra_cdfs_buckets.inc"
};
static const AomCdfProb eob_multi16_cdfs_default[TOKEN_CDF_Q_CTXS][PLANE_TYPES][2][CDF_SIZE(5)] = {
#include "eob_multi16_cdfs_buckets.inc"
};
static const AomCdfProb eob_multi32_cdfs_default[TOKEN_CDF_Q_CTXS][PLANE_TYPES][2][CDF_SIZE(6)] = {
#include "eob_multi32_cdfs_buckets.inc"
};
static const AomCdfProb eob_multi64_cdfs_default[TOKEN_CDF_Q_CTXS][PLANE_TYPES][2][CDF_SIZE(7)] = {
#include "eob_multi64_cdfs_buckets.inc"
};
static const AomCdfProb eob_multi128_cdfs_default[TOKEN_CDF_Q_CTXS][PLANE_TYPES][2][CDF_SIZE(8)] = {
#include "eob_multi128_cdfs_buckets.inc"
};
static const AomCdfProb eob_multi256_cdfs_default[TOKEN_CDF_Q_CTXS][PLANE_TYPES][2][CDF_SIZE(9)] = {
#include "eob_multi256_cdfs_buckets.inc"
};
static const AomCdfProb eob_multi512_cdfs_default[TOKEN_CDF_Q_CTXS][PLANE_TYPES][2][CDF_SIZE(10)] = {
#include "eob_multi512_cdfs_buckets.inc"
};
static const AomCdfProb eob_multi1024_cdfs_default[TOKEN_CDF_Q_CTXS][PLANE_TYPES][2][CDF_SIZE(11)] = {
#include "eob_multi1024_cdfs_buckets.inc"
};

// default_intra_ext_tx_cdf (cabac_context_model.c:157, verbatim)
static const AomCdfProb intra_ext_tx_cdfs_default[EXT_TX_SETS_INTRA][EXT_TX_SIZES][INTRA_MODES][CDF_SIZE(16)] = {
#include "intra_ext_tx_cdfs_default.inc"
};

// av1_num_ext_tx_set (common_utils.c:195, verbatim)
const int32_t av1NumExtTxSet[EXT_TX_SET_TYPES] = {1, 2, 5, 7, 12, 16};

// av1_ext_tx_used (common_utils.c:197-205, verbatim)
const int32_t av1ExtTxUsed[EXT_TX_SET_TYPES][16] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
};

// ext_tx_set_index (common_utils.c:206-209, verbatim)
const int32_t extTxSetIndex[2][EXT_TX_SET_TYPES] = {
    {0, -1, 2, 1, -1, -1}, // Intra
    {0, 3, -1, -1, 2, 1} // Inter
};

// av1_ext_tx_ind (cabac_context_model.c:34-41, verbatim)
const int32_t av1ExtTxInd[EXT_TX_SET_TYPES][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 3, 4, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, 5, 6, 4, 0, 0, 0, 0, 0, 0, 2, 3, 0, 0, 0, 0},
    {3, 4, 5, 8, 6, 7, 9, 10, 11, 0, 1, 2, 0, 0, 0, 0},
    {7, 8, 9, 12, 10, 11, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6},
};

// tx_type_to_class (cabac_context_model.c:15-35, verbatim; TS1 uses [DCT_DCT])
static const TxClass tx_type_to_class[TX_TYPES] = {
    TX_CLASS_2D, TX_CLASS_2D, TX_CLASS_2D, TX_CLASS_2D, TX_CLASS_2D, TX_CLASS_2D, TX_CLASS_2D,
    TX_CLASS_2D, TX_CLASS_2D, TX_CLASS_2D, TX_CLASS_VERT, TX_CLASS_HORIZ, TX_CLASS_VERT, TX_CLASS_HORIZ,
    TX_CLASS_VERT, TX_CLASS_HORIZ,
};

// txsize_log2_minus4 (inv_transforms.h:341-359, verbatim)
extern const std::int8_t txsizeLog2Minus4[TX_SIZES_ALL] = {
    0, 2, 4, 6, 6, 1, 1, 3, 3, 5, 5, 6, 6, 2, 2, 4, 4, 5, 5,
};
// tx_size_wide/high/log2 (common_utils.c:116-128, verbatim)
const std::int32_t txSizeWide[TX_SIZES_ALL] = {4, 8, 16, 32, 64, 4, 8, 8, 16, 16, 32, 32, 64, 4, 16, 8, 32, 16, 64};
const std::int32_t txSizeHigh[TX_SIZES_ALL] = {4, 8, 16, 32, 64, 8, 4, 16, 8, 32, 16, 64, 32, 16, 4, 32, 8, 64, 16};
const std::int32_t txSizeWideLog2[TX_SIZES_ALL] = {2, 3, 4, 5, 6, 2, 3, 3, 4, 4, 5, 5, 6, 2, 4, 3, 5, 4, 6};
// txsize_sqr_map / txsize_sqr_up_map (common_utils.c:150/:172, verbatim)
const TxSize txsizeSqrMap[TX_SIZES_ALL] = {
    TX_4X4, // TX_4X4
    TX_8X8, // TX_8X8
    TX_16X16, // TX_16X16
    TX_32X32, // TX_32X32
    TX_64X64, // TX_64X64
    TX_4X4, // TX_4X8
    TX_4X4, // TX_8X4
    TX_8X8, // TX_8X16
    TX_8X8, // TX_16X8
    TX_16X16, // TX_16X32
    TX_16X16, // TX_32X16
    TX_32X32, // TX_32X64
    TX_32X32, // TX_64X32
    TX_4X4, // TX_4X16
    TX_4X4, // TX_16X4
    TX_8X8, // TX_8X32
    TX_8X8, // TX_32X8
    TX_16X16, // TX_16X64
    TX_16X16, // TX_64X16
};
const TxSize txsizeSqrUpMap[TX_SIZES_ALL] = {
    TX_4X4, // TX_4X4
    TX_8X8, // TX_8X8
    TX_16X16, // TX_16X16
    TX_32X32, // TX_32X32
    TX_64X64, // TX_64X64
    TX_8X8, // TX_4X8
    TX_8X8, // TX_8X4
    TX_16X16, // TX_8X16
    TX_16X16, // TX_16X8
    TX_32X32, // TX_16X32
    TX_32X32, // TX_32X16
    TX_64X64, // TX_32X64
    TX_64X64, // TX_64X32
    TX_16X16, // TX_4X16
    TX_16X16, // TX_16X4
    TX_32X32, // TX_8X32
    TX_32X32, // TX_32X8
    TX_64X64, // TX_16X64
    TX_64X64, // TX_64X16
};

// get_q_ctx (cabac_context_model.c:1907-1918, verbatim; the spec's
// init_coeff_cdfs, 07.bitstream.semantics.md:1800-1820): the
// TOKEN_CDF_Q_CTXS bucket for the coefficient CDF tables.
static int getQCtx(std::int32_t q) {
    if (q <= 20) {
        return 0;
    }
    if (q <= 60) {
        return 1;
    }
    if (q <= 120) {
        return 2;
    }
    return 3;
}

// COPY_CDF equivalent for the four tables (cabac_context_model.c:740-741,
// :767 - the kf_y_cdf/angle_delta_cdf/filter_intra rows of
// svt_aom_av1_setup_frame_context) plus the coefficient tables, which are
// QINDEX-BUCKET-SELECTED per the spec's init_coeff_cdfs (TD5b; previously
// this init copied the idx-0 bucket for every frame - the TS1 deviation
// named in the register; resolution: it WAS the q100 tile-divergence bug).
void initDefaultEcFrameContext(EcFrameContext* fc, std::int32_t base_q_idx) {
    const int idx = getQCtx(base_q_idx);
    memcpy(fc->kf_y_cdf, kf_y_mode_cdf_default, sizeof(fc->kf_y_cdf));
    memcpy(fc->angle_delta_cdf, angle_delta_cdf_default, sizeof(fc->angle_delta_cdf));
    memcpy(fc->filter_intra_cdfs, filter_intra_cdfs_default, sizeof(fc->filter_intra_cdfs));
    memcpy(fc->filter_intra_mode_cdf, filter_intra_mode_cdf_default, sizeof(fc->filter_intra_mode_cdf));
    memcpy(fc->partition_cdf, partition_cdf_default, sizeof(fc->partition_cdf));
    memcpy(fc->skip_cdfs, skip_cdfs_default, sizeof(fc->skip_cdfs));
    memcpy(fc->txb_skip_cdf, txb_skip_cdfs_default[idx], sizeof(fc->txb_skip_cdf));
    memcpy(fc->dc_sign_cdf, dc_sign_cdfs_default[idx], sizeof(fc->dc_sign_cdf));
    memcpy(fc->coeff_base_eob_cdf, coeff_base_eob_multi_cdfs_default[idx], sizeof(fc->coeff_base_eob_cdf));
    memcpy(fc->coeff_base_cdf, coeff_base_multi_cdfs_default[idx], sizeof(fc->coeff_base_cdf));
    memcpy(fc->coeff_br_cdf, coeff_lps_multi_cdfs_default[idx], sizeof(fc->coeff_br_cdf));
    memcpy(fc->eob_extra_cdf, eob_extra_cdfs_default[idx], sizeof(fc->eob_extra_cdf));
    memcpy(fc->eob_flag_cdf16, eob_multi16_cdfs_default[idx], sizeof(fc->eob_flag_cdf16));
    memcpy(fc->eob_flag_cdf32, eob_multi32_cdfs_default[idx], sizeof(fc->eob_flag_cdf32));
    memcpy(fc->eob_flag_cdf64, eob_multi64_cdfs_default[idx], sizeof(fc->eob_flag_cdf64));
    memcpy(fc->eob_flag_cdf128, eob_multi128_cdfs_default[idx], sizeof(fc->eob_flag_cdf128));
    memcpy(fc->eob_flag_cdf256, eob_multi256_cdfs_default[idx], sizeof(fc->eob_flag_cdf256));
    memcpy(fc->eob_flag_cdf512, eob_multi512_cdfs_default[idx], sizeof(fc->eob_flag_cdf512));
    memcpy(fc->eob_flag_cdf1024, eob_multi1024_cdfs_default[idx], sizeof(fc->eob_flag_cdf1024));
    memcpy(fc->intra_ext_tx_cdf, intra_ext_tx_cdfs_default, sizeof(fc->intra_ext_tx_cdf));
}

int ecFrameCdfsEqual(const EcFrameContext* a, const EcFrameContext* b) {
    return memcmp(a->kf_y_cdf, b->kf_y_cdf, sizeof(a->kf_y_cdf)) == 0 &&
           memcmp(a->angle_delta_cdf, b->angle_delta_cdf, sizeof(a->angle_delta_cdf)) == 0 &&
           memcmp(a->filter_intra_cdfs, b->filter_intra_cdfs, sizeof(a->filter_intra_cdfs)) == 0 &&
           memcmp(a->filter_intra_mode_cdf, b->filter_intra_mode_cdf, sizeof(a->filter_intra_mode_cdf)) == 0 &&
           memcmp(a->partition_cdf, b->partition_cdf, sizeof(a->partition_cdf)) == 0 &&
           memcmp(a->skip_cdfs, b->skip_cdfs, sizeof(a->skip_cdfs)) == 0 &&
           memcmp(a->txb_skip_cdf, b->txb_skip_cdf, sizeof(a->txb_skip_cdf)) == 0 &&
           memcmp(a->dc_sign_cdf, b->dc_sign_cdf, sizeof(a->dc_sign_cdf)) == 0 &&
           memcmp(a->coeff_base_eob_cdf, b->coeff_base_eob_cdf, sizeof(a->coeff_base_eob_cdf)) == 0 &&
           memcmp(a->coeff_base_cdf, b->coeff_base_cdf, sizeof(a->coeff_base_cdf)) == 0 &&
           memcmp(a->coeff_br_cdf, b->coeff_br_cdf, sizeof(a->coeff_br_cdf)) == 0 &&
           memcmp(a->eob_extra_cdf, b->eob_extra_cdf, sizeof(a->eob_extra_cdf)) == 0 &&
           memcmp(a->eob_flag_cdf16, b->eob_flag_cdf16, sizeof(a->eob_flag_cdf16)) == 0 &&
           memcmp(a->eob_flag_cdf32, b->eob_flag_cdf32, sizeof(a->eob_flag_cdf32)) == 0 &&
           memcmp(a->eob_flag_cdf64, b->eob_flag_cdf64, sizeof(a->eob_flag_cdf64)) == 0 &&
           memcmp(a->eob_flag_cdf128, b->eob_flag_cdf128, sizeof(a->eob_flag_cdf128)) == 0 &&
           memcmp(a->eob_flag_cdf256, b->eob_flag_cdf256, sizeof(a->eob_flag_cdf256)) == 0 &&
           memcmp(a->eob_flag_cdf512, b->eob_flag_cdf512, sizeof(a->eob_flag_cdf512)) == 0 &&
           memcmp(a->eob_flag_cdf1024, b->eob_flag_cdf1024, sizeof(a->eob_flag_cdf1024)) == 0 &&
           memcmp(a->intra_ext_tx_cdf, b->intra_ext_tx_cdf, sizeof(a->intra_ext_tx_cdf)) == 0;
}

// svt_aom_get_kf_y_mode_ctx (entropy_coding.c:1004-1021), flattened
// (neighbor modes as explicit args; unavailable -> DC_PRED).
void getKfYModeCtx(int left_available, int left_mode, int up_available, int up_mode,
                   int* above_ctx, int* left_ctx) {
    int intra_luma_left_mode = DC_PRED;
    int intra_luma_top_mode  = DC_PRED;
    if (left_available) {
        intra_luma_left_mode = left_mode;
    }
    if (up_available) {
        intra_luma_top_mode = up_mode;
    }

    *above_ctx = intraModeContext[intra_luma_top_mode];
    *left_ctx  = intraModeContext[intra_luma_left_mode];
}

// svt_aom_filter_intra_allowed_bsize (mode_decision.c:108-112)
int filterIntraAllowedBsize(BlockSize bs) {
    // CONFIG_ENABLE_FILTER_INTRA == 1 on this build config
    return blockSizeWide[bs] <= 32 && blockSizeHigh[bs] <= 32;
}

// svt_aom_filter_intra_allowed (mode_decision.c:115-119)
int filterIntraAllowed(std::uint8_t enable_filter_intra, BlockSize bsize,
                       std::uint8_t palette_size, std::uint32_t mode) {
    return enable_filter_intra && mode == DC_PRED && palette_size == 0 && filterIntraAllowedBsize(bsize);
}

// encode_intra_luma_mode_kf_av1 (entropy_coding.c:1026-1040)
void writeKfLumaMode(AomWriter* w, EcFrameContext* fc, BlockSize bsize,
                     PredictionMode mode, int above_ctx, int left_ctx, int angle_delta) {
    odEcWriteSymbol(w, mode, fc->kf_y_cdf[above_ctx][left_ctx], INTRA_MODES);

    if (bsize >= BLOCK_8X8 && isDirectionalMode(mode)) {
        odEcWriteSymbol(w,
                        angle_delta + MAX_ANGLE_DELTA,
                        fc->angle_delta_cdf[mode - V_PRED],
                        2 * MAX_ANGLE_DELTA + 1);
    }
}

// Decode side of encode_intra_luma_mode_kf_av1: angle CDF indexed by the
// DECODED mode (entropy_coding.c:1030-1037 reader semantics). *angle_delta
// receives the RAW decoded symbol (delta + MAX_ANGLE_DELTA), matching the
// gate's eckf_rt accounting.
PredictionMode readKfLumaMode(AomReader* r, EcFrameContext* fc, BlockSize bsize,
                              int above_ctx, int left_ctx, int* angle_delta) {
    const PredictionMode m = (PredictionMode)odEcReadSymbol(r, fc->kf_y_cdf[above_ctx][left_ctx], INTRA_MODES);
    if (bsize >= BLOCK_8X8 && isDirectionalMode(m)) {
        *angle_delta = odEcReadSymbol(r, fc->angle_delta_cdf[m - V_PRED], 2 * MAX_ANGLE_DELTA + 1);
    } else {
        *angle_delta = 0;
    }
    return m;
}

// Filter-intra pair (entropy_coding.c:5050-5058)
void writeFilterIntra(AomWriter* w, EcFrameContext* fc, BlockSize bsize,
                      FilterIntraMode fi_mode) {
    odEcWriteSymbol(w, fi_mode != FILTER_INTRA_MODES, fc->filter_intra_cdfs[bsize], 2);
    if (fi_mode != FILTER_INTRA_MODES) {
        odEcWriteSymbol(w, fi_mode, fc->filter_intra_mode_cdf, FILTER_INTRA_MODES);
    }
}

int readFilterIntra(AomReader* r, EcFrameContext* fc, BlockSize bsize,
                    FilterIntraMode* fi_mode) {
    const int f = odEcReadSymbol(r, fc->filter_intra_cdfs[bsize], 2);
    if (f) {
        *fi_mode = (FilterIntraMode)odEcReadSymbol(r, fc->filter_intra_mode_cdf, FILTER_INTRA_MODES);
    } else {
        *fi_mode = FILTER_INTRA_MODES;
    }
    return f;
}

// ---------------------------------------------------------------------------
// ECP1: partition symbol surface (court-ordered l7 exception).
// ---------------------------------------------------------------------------
// svt_aom_partition_cdf_length (entropy_coding.c:922-930)
int partitionCdfLength(BlockSize bsize) {
    if (bsize <= BLOCK_8X8) {
        return PARTITION_TYPES;
    } else if (bsize == BLOCK_128X128) {
        return EXT_PARTITION_TYPES - 2;
    } else {
        return EXT_PARTITION_TYPES;
    }
}

// cdf_element_prob (cabac_context_model.h:373-376)
static int cdfElementProb(const AomCdfProb* cdf, std::size_t element) {
    return (element > 0 ? cdf[element - 1] : CDF_PROB_TOP) - cdf[element];
}

// partition_gather_horz_alike (cabac_context_model.h:378-391). The SVT text
// accumulates into the uint16 out[0] and finishes with AOM_ICDF(out[0]); the
// intermediate is CDF_PROB_TOP - sum(probs) and stays within uint16 range,
// spelled here with an int accumulator and explicit casts (same values).
void partitionGatherHorzAlike(AomCdfProb* out, const AomCdfProb* in, BlockSize bsize) {
    int acc = CDF_PROB_TOP;
    acc -= cdfElementProb(in, PARTITION_HORZ);
    acc -= cdfElementProb(in, PARTITION_SPLIT);
    acc -= cdfElementProb(in, PARTITION_HORZ_A);
    acc -= cdfElementProb(in, PARTITION_HORZ_B);
    acc -= cdfElementProb(in, PARTITION_VERT_A);
    if (bsize != BLOCK_128X128) {
        acc -= cdfElementProb(in, PARTITION_HORZ_4);
    }
    out[0] = static_cast<AomCdfProb>(CDF_PROB_TOP - acc);
    out[1] = AOM_ICDF(CDF_PROB_TOP);
    out[2] = 0;
}

// partition_gather_vert_alike (cabac_context_model.h:393-405)
void partitionGatherVertAlike(AomCdfProb* out, const AomCdfProb* in, BlockSize bsize) {
    int acc = CDF_PROB_TOP;
    acc -= cdfElementProb(in, PARTITION_VERT);
    acc -= cdfElementProb(in, PARTITION_SPLIT);
    acc -= cdfElementProb(in, PARTITION_HORZ_A);
    acc -= cdfElementProb(in, PARTITION_VERT_A);
    acc -= cdfElementProb(in, PARTITION_VERT_B);
    if (bsize != BLOCK_128X128) {
        acc -= cdfElementProb(in, PARTITION_VERT_4);
    }
    out[0] = static_cast<AomCdfProb>(CDF_PROB_TOP - acc);
    out[1] = AOM_ICDF(CDF_PROB_TOP);
    out[2] = 0;
}

// entropy_coding.c:945-960 flattened. bsl = mi_size_wide_log2[bsize] -
// mi_size_wide_log2[BLOCK_8X8]; mi_size_wide_log2 == svt_log2f(px >> 2) for
// the square partition points the caller guards (:935). Fresh cells
// (INVALID_NEIGHBOR_DATA, definitions.h:334) map to 0 (:950-951).
int partitionPlaneContext(std::uint8_t above_byte, std::uint8_t left_byte, BlockSize bsize) {
    const int bsl = getMsb(blockSizeWide[bsize] >> 2) - 1;
    const int above = ((above_byte == (std::uint8_t)INVALID_NEIGHBOR_DATA ? 0 : above_byte) >> bsl) & 1;
    const int left = ((left_byte == (std::uint8_t)INVALID_NEIGHBOR_DATA ? 0 : left_byte) >> bsl) & 1;
    return (left * 2 + above) + bsl * PARTITION_PLOFFSET;
}

// encode_partition_av1 (entropy_coding.c:932-981) flattened (the caller
// guards is_partition_point :935 and derives has_rows/has_cols :941-943).
void writePartition(AomWriter* w, EcFrameContext* fc, BlockSize bsize, int has_rows, int has_cols,
                    std::uint8_t above_byte, std::uint8_t left_byte, PartitionType p) {
    const int ctx = partitionPlaneContext(above_byte, left_byte, bsize);
    if (!has_rows && !has_cols) {
        return;  // forced SPLIT, NO symbol (:962-965)
    }
    if (has_rows && has_cols) {
        odEcWriteSymbol(w, p, fc->partition_cdf[ctx], partitionCdfLength(bsize));
    } else if (!has_rows && has_cols) {
        AomCdfProb g[CDF_SIZE(2)];
        partitionGatherVertAlike(g, fc->partition_cdf[ctx], bsize);
        odEcWriteSymbol(w, p == PARTITION_SPLIT, g, 2);
    } else {
        AomCdfProb g[CDF_SIZE(2)];
        partitionGatherHorzAlike(g, fc->partition_cdf[ctx], bsize);
        odEcWriteSymbol(w, p == PARTITION_SPLIT, g, 2);
    }
}

// aom read_partition (decodeframe.c:1266-1293 semantics; out-of-tree BSF4
// arbiter, no aom code extracted). Gathered branches read the temporary via
// the NON-adapting odEcReadCdf (aom:1284/:1291) - consistent with the writer
// discarding the temporary's adaptation.
PartitionType readPartition(AomReader* r, EcFrameContext* fc, BlockSize bsize, int has_rows,
                            int has_cols, std::uint8_t above_byte, std::uint8_t left_byte) {
    const int ctx = partitionPlaneContext(above_byte, left_byte, bsize);
    if (!has_rows && !has_cols) return PARTITION_SPLIT;
    if (has_rows && has_cols) {
        return (PartitionType)odEcReadSymbol(r, fc->partition_cdf[ctx], partitionCdfLength(bsize));
    }
    if (!has_rows && has_cols) {
        AomCdfProb g[CDF_SIZE(2)];
        partitionGatherVertAlike(g, fc->partition_cdf[ctx], bsize);
        return odEcReadCdf(r, g, 2) ? PARTITION_SPLIT : PARTITION_HORZ;
    }
    AomCdfProb g[CDF_SIZE(2)];
    partitionGatherHorzAlike(g, fc->partition_cdf[ctx], bsize);
    return odEcReadCdf(r, g, 2) ? PARTITION_SPLIT : PARTITION_VERT;
}

// coding_loop.c:1700-1713 (the NA writes over the block extent, == the aom
// memset semantics): lookup bytes from partition_context_lookup
// (definitions.h:1551-1573). left indexing (mi_row & 15) = MAX_MIB_MASK for
// the ratified sb_size 64.
void updatePartitionContext(std::uint8_t* above, std::uint8_t* left, int mi_row, int mi_col,
                            BlockSize bsize) {
    const int mi_w = blockSizeWide[bsize] >> 2;
    const int mi_h = blockSizeHigh[bsize] >> 2;
    for (int j = 0; j < mi_w; ++j) above[mi_col + j] = partitionContextLookupAbove[bsize];
    for (int i = 0; i < mi_h; ++i) left[(mi_row + i) & 15] = partitionContextLookupLeft[bsize];
}

// av1_get_skip_context (entropy_coding.c:983-989) flattened
int getSkipContext(int above_available, int above_skip, int left_available, int left_skip) {
    const int above_skip_flag = above_available ? above_skip : 0;
    const int left_skip_flag  = left_available ? left_skip : 0;
    return (std::uint8_t)(above_skip_flag + left_skip_flag);
}

// encode_skip_coeff_av1 (entropy_coding.c:995-1000)
void writeSkip(AomWriter* w, EcFrameContext* fc, int ctx, int skip) {
    odEcWriteSymbol(w, skip ? 1 : 0, fc->skip_cdfs[ctx], 2);
}

// aom read_skip_txfm semantics (decodemv.c, out-of-tree BSF4 arbiter) minus
// the SEG_LVL_SKIP implicit-1 branch (segmentation deferred)
int readSkip(AomReader* r, EcFrameContext* fc, int ctx) {
    return odEcReadSymbol(r, fc->skip_cdfs[ctx], 2);
}

// ---------------------------------------------------------------------------
// TS1: per-TU coefficient chain (entropy_coding.c:355-544, LUMA DCT_DCT).
// ---------------------------------------------------------------------------
// eb_av1_nz_map_ctx_offset LUT (coefficients.c:24-303, verbatim): the full
// [19] pointer array + all sub-tables. The 2D DCT_DCT path indexes by the
// square TxSize values (0-4).
namespace {
#include "nz_map_ctx_offset.inc"
}  // anonymous namespace

// coefficients.h:56-67
int getLowerLevelsCtxEob(int bwl, int height, int scan_idx) {
    if (scan_idx == 0) return 0;
    if (scan_idx <= (height << bwl) / 8) return 1;
    if (scan_idx <= (height << bwl) / 4) return 2;
    return 3;
}

// coefficients.h:69-81
int getBrCtxEob(int c, int bwl, TxClass tx_class) {
    const int row = c >> bwl;
    const int col = c - (row << bwl);
    if (c == 0) return 0;
    if ((tx_class == TX_CLASS_2D && row < 2 && col < 2) || (tx_class == TX_CLASS_HORIZ && col == 0) ||
        (tx_class == TX_CLASS_VERT && row == 0)) return 7;
    return 14;
}

// coefficients.h:83-127 (verbatim semantics)
int getBrCtx(const std::uint8_t* levels, int c, int bwl, TxClass tx_class) {
    const int row    = c >> bwl;
    const int col    = c - (row << bwl);
    const int stride = (1 << bwl) + TX_PAD_HOR;
    const int pos    = row * stride + col;
    int mag = levels[pos + 1];
    mag += levels[pos + stride];
    switch (tx_class) {
    case TX_CLASS_2D:
        mag += levels[pos + stride + 1];
        mag = AOMMIN((mag + 1) >> 1, 6);
        if (c == 0) return mag;
        if ((row < 2) && (col < 2)) return mag + 7;
        break;
    case TX_CLASS_HORIZ:
        mag += levels[pos + 2];
        mag = AOMMIN((mag + 1) >> 1, 6);
        if (c == 0) return mag;
        if (col == 0) return mag + 7;
        break;
    case TX_CLASS_VERT:
        mag += levels[pos + (stride << 1)];
        mag = AOMMIN((mag + 1) >> 1, 6);
        if (c == 0) return mag;
        if (row == 0) return mag + 7;
        break;
    default: break;
    }
    return mag + 14;
}

// coefficients.h:129-131
int getPaddedIdx(int idx, int bwl) {
    return idx + ((idx >> bwl) << TX_PAD_HOR_LOG2);
}

// coefficients.h:133-155 (verbatim, all three classes)
int getNzMag(const std::uint8_t* levels, int bwl, TxClass tx_class) {
    int mag;
#define CLIP_MAX3(x) ((x > 3) ? 3 : x)
    mag = CLIP_MAX3(levels[1]);
    mag += CLIP_MAX3(levels[(1 << bwl) + TX_PAD_HOR]);
    if (tx_class == TX_CLASS_2D) {
        mag += CLIP_MAX3(levels[(1 << bwl) + TX_PAD_HOR + 1]);
        mag += CLIP_MAX3(levels[2]);
        mag += CLIP_MAX3(levels[(2 << bwl) + (2 << TX_PAD_HOR_LOG2)]);
    } else if (tx_class == TX_CLASS_VERT) {
        mag += CLIP_MAX3(levels[(2 << bwl) + (2 << TX_PAD_HOR_LOG2)]);
        mag += CLIP_MAX3(levels[(3 << bwl) + (3 << TX_PAD_HOR_LOG2)]);
        mag += CLIP_MAX3(levels[(4 << bwl) + (4 << TX_PAD_HOR_LOG2)]);
    } else {
        mag += CLIP_MAX3(levels[2]);
        mag += CLIP_MAX3(levels[3]);
        mag += CLIP_MAX3(levels[4]);
    }
#undef CLIP_MAX3
    return mag;
}

// coefficients.h:157-194 (2D class = the DCT_DCT port's only path)
int getNzMapCtxFromStats(int stats, int coeff_idx, int bwl, TxSize tx_size, TxClass tx_class) {
    (void)bwl;
    if ((tx_class | coeff_idx) == 0) return 0;
    int ctx = (stats + 1) >> 1;
    ctx = AOMMIN(ctx, 4);
    switch (tx_class) {
    case TX_CLASS_2D:
        return ctx + eb_av1_nz_map_ctx_offset[tx_size][coeff_idx];
    default:
        return 0;  // HORIZ/VERT classes are the 1D path - not in the DCT_DCT port
    }
}

// coefficients.h:196-200
int getLowerLevelsCtx(const std::uint8_t* levels, int coeff_idx, int bwl, TxSize tx_size, TxClass tx_class) {
    const int stats = getNzMag(levels + getPaddedIdx(coeff_idx, bwl), bwl, tx_class);
    return getNzMapCtxFromStats(stats, coeff_idx, bwl, tx_size, tx_class);
}

// rd_cost.c:93-105 (verbatim)
void txbInitLevels(const std::int32_t* coeff, int width, int height, std::uint8_t* levels) {
    std::uint8_t* ls = levels;
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            *ls++ = static_cast<std::uint8_t>(AOMMIN(std::abs(coeff[i * width + j]), INT8_MAX));
        }
        for (int j = 0; j < TX_PAD_HOR; j++) *ls++ = 0;
    }
}

// C_DEFAULT/encode_txb_ref_c.c:17-44 (verbatim)
static int nzMapCtx(const std::uint8_t* levels, int coeff_idx, int bwl, int height, int scan_idx, int is_eob,
                    TxSize tx_size, TxClass tx_class) {
    if (is_eob) {
        if (scan_idx == 0) return 0;
        if (scan_idx <= (height << bwl) / 8) return 1;
        if (scan_idx <= (height << bwl) / 4) return 2;
        return 3;
    }
    const int stats = getNzMag(levels + getPaddedIdx(coeff_idx, bwl), bwl, tx_class);
    return getNzMapCtxFromStats(stats, coeff_idx, bwl, tx_size, tx_class);
}

// C_DEFAULT/encode_txb_ref_c.c:35-44 (verbatim)
void getNzMapContexts(const std::uint8_t* levels, const std::int16_t* scan, std::uint16_t eob,
                      TxSize tx_size, TxClass tx_class, std::int8_t* coeff_contexts) {
    const int bwl    = getTxbBwl(tx_size);
    const int height = getTxbHigh(tx_size);
    for (int i = 0; i < eob; ++i) {
        const int pos = scan[i];
        coeff_contexts[pos] =
            static_cast<std::int8_t>(nzMapCtx(levels, pos, bwl, height, i, i == eob - 1, tx_size, tx_class));
    }
}

// entropy_coding.h:110-112
TxSize getTxsizeEntropyCtx(TxSize txsize) {
    return static_cast<TxSize>((txsizeSqrMap[txsize] + txsizeSqrUpMap[txsize] + 1) >> 1);
}

// common_utils.h:100-128 (verbatim; av1_get_adjusted_tx_size folded per function)
static TxSize av1GetAdjustedTxSize(TxSize tx_size) {
    switch (tx_size) {
    case TX_64X64: case TX_64X32: case TX_32X64: return TX_32X32;
    case TX_64X16: return TX_32X16;
    case TX_16X64: return TX_16X32;
    default: return tx_size;
    }
}
int getTxbBwl(TxSize tx_size) {
    tx_size = av1GetAdjustedTxSize(tx_size);
    return txSizeWideLog2[tx_size];
}
int getTxbWide(TxSize tx_size) {
    tx_size = av1GetAdjustedTxSize(tx_size);
    return txSizeWide[tx_size];
}
int getTxbHigh(TxSize tx_size) {
    tx_size = av1GetAdjustedTxSize(tx_size);
    return txSizeHigh[tx_size];
}

// entropy_coding.h:94-102
int getEobPosToken(int eob, int* extra) {
    if (eob < 3) {
        *extra = 0;
        return eob;
    }
    const int t = getMsb(static_cast<std::uint32_t>(eob - 1));
    *extra = eob - 1 - (1 << t);
    return t + 2;
}

// entropy_coding.c:236-243
void writeGolomb(AomWriter* w, int level) {
    const std::int32_t x      = level + 1;
    const std::uint32_t length = static_cast<std::uint32_t>(getMsb(x) + 1);
    odEcWriteLiteralBits(w, 0, static_cast<int>(length - 1));  // aom_write_literal(w, 0, length-1)
    odEcWriteLiteralBits(w, x, static_cast<int>(length));
}

// aom decodetxb.c read_golomb (verbatim semantics)
int readGolomb(AomReader* r) {
    int x = 1, length = 0, i = 0;
    while (!i) {
        i = odEcReadBit(r);
        ++length;
    }
    for (i = 0; i < length - 1; ++i) {
        x <<= 1;
        x += odEcReadBit(r);
    }
    return x - 1;
}

// ---------------------------------------------------------------------------
// TS3: tx-type surface (entropy_coding.c:317-353, intra path).
// ---------------------------------------------------------------------------
// get_ext_tx_set_type (common_utils.h:59-77, intra path: is_inter=0)
TxSetType getExtTxSetType(TxSize tx_size, int is_inter, int use_reduced_set) {
    const TxSize tx_size_sqr_up = txsizeSqrUpMap[tx_size];
    if (tx_size_sqr_up > TX_32X32) return EXT_TX_SET_DCTONLY;
    if (tx_size_sqr_up == TX_32X32) return is_inter ? EXT_TX_SET_DCT_IDTX : EXT_TX_SET_DCTONLY;
    if (use_reduced_set) return is_inter ? EXT_TX_SET_DCT_IDTX : EXT_TX_SET_DTT4_IDTX;
    const TxSize tx_size_sqr = txsizeSqrMap[tx_size];
    if (is_inter) return (tx_size_sqr == TX_16X16 ? EXT_TX_SET_DTT9_IDTX_1DDCT : EXT_TX_SET_ALL16);
    return (tx_size_sqr == TX_16X16 ? EXT_TX_SET_DTT4_IDTX : EXT_TX_SET_DTT4_IDTX_1DDCT);
}

// get_ext_tx_types (common_utils.h:79-82)
int getExtTxTypes(TxSize tx_size, int is_inter, int use_reduced_set) {
    return av1NumExtTxSet[getExtTxSetType(tx_size, is_inter, use_reduced_set)];
}

// get_ext_tx_set (common_utils.h:87-90)
int getExtTxSet(TxSize tx_size, int is_inter, int use_reduced_set) {
    return extTxSetIndex[is_inter][getExtTxSetType(tx_size, is_inter, use_reduced_set)];
}

// av1_write_tx_type (entropy_coding.c:317-353, intra path only)
void writeTxType(AomWriter* w, EcFrameContext* fc, int base_q_idx, int reduced_tx_set,
                 PredictionMode intra_dir, TxType tx_type, TxSize tx_size) {
    const int is_inter = 0;  // intra path only
    if (getExtTxTypes(tx_size, is_inter, reduced_tx_set) > 1 && base_q_idx > 0) {
        const TxSize square_tx_size = txsizeSqrMap[tx_size];
        const TxSetType tx_set_type = getExtTxSetType(tx_size, is_inter, reduced_tx_set);
        const int32_t eset = getExtTxSet(tx_size, is_inter, reduced_tx_set);
        odEcWriteSymbol(w, av1ExtTxInd[tx_set_type][tx_type],
                        fc->intra_ext_tx_cdf[eset][square_tx_size][intra_dir],
                        av1NumExtTxSet[tx_set_type]);
    }
}

// aom decodetxb.c av1_read_tx_type (intra path, symbol-for-symbol twin)
TxType readTxType(AomReader* r, EcFrameContext* fc, int base_q_idx, int reduced_tx_set,
                  PredictionMode intra_dir, TxSize tx_size) {
    const int is_inter = 0;
    if (getExtTxTypes(tx_size, is_inter, reduced_tx_set) <= 1 || base_q_idx == 0) return DCT_DCT;
    const TxSetType tx_set_type = getExtTxSetType(tx_size, is_inter, reduced_tx_set);
    const int32_t eset = getExtTxSet(tx_size, is_inter, reduced_tx_set);
    const int32_t num = av1NumExtTxSet[tx_set_type];
    const int32_t sym = odEcReadSymbol(r, fc->intra_ext_tx_cdf[eset][txsizeSqrMap[tx_size]][intra_dir], num);
    // av1_ext_tx_inv[tx_set_type][sym] — the inverse mapping for DTT4_IDTX (eset 2)
    // For the reduced intra set (DTT4_IDTX): {9,0,3,1,2,0,...} — index 0→9(IDTX? no,
    // av1_ext_tx_inv[2][0]=9? hmm — looking at the table: {9, 0, 3, 1, 2, ...}
    // sym 0 → 9 (IDTX? no — DCT_DCT is 0, so av1_ext_tx_inv maps the read
    // symbol back to the TxType enum). For DCT_DCT-only port: sym 0 → DCT_DCT.
    // The av1_ext_tx_inv[2] row: {9, 0, 3, 1, 2, 0, ...} — sym 0→9 (IDTX? no.
    // Actually the aom table maps the CDF index to the TxType. For DTT4_IDTX
    // (eset 2): the set is {DCT_DCT, DCT_ADST, ADST_DCT, ADST_ADST, IDTX} in
    // the order {0,3,4,2,9} per av1_ext_tx_ind[2] = {1,3,4,2,0,0,...}. Wait —
    // av1_ext_tx_ind[2] maps TxType → symbol index: DCT_DCT→1, H_DCT→3, V_DCT→4,
    // ADST_DCT→2, IDTX→0. So the inverse (av1_ext_tx_inv[2]) maps symbol → TxType:
    // sym 0→IDTX(9), sym 1→DCT_DCT(0), sym 2→ADST_DCT(1), sym 3→H_DCT(11),
    // sym 4→V_DCT(10). For our DCT_DCT-only port, the DCT_DCT symbol index is
    // av1_ext_tx_ind[2][DCT_DCT] = 1, and the reader inverts: av1_ext_tx_inv[2][1] = 0.
    static const int32_t av1ExtTxInv[EXT_TX_SET_TYPES][16] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {9, 0, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {9, 0, 10, 11, 3, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {9, 10, 11, 0, 1, 2, 4, 5, 3, 6, 7, 8, 0, 0, 0, 0},
        {9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 4, 5, 3, 6, 7, 8},
    };
    return static_cast<TxType>(av1ExtTxInv[tx_set_type][sym]);
}

// ---------------------------------------------------------------------------
// TS2: per-block tables + getTxbCtx + per-block loop.
// ---------------------------------------------------------------------------
// tx_blocks_per_depth (transforms.c:24-46, verbatim)
const std::uint8_t txBlocksPerDepth[22][3] = {
    {1, 1, 1}, // BLOCK_4X4
    {1, 1, 1}, // BLOCK_4X8
    {1, 1, 1}, // BLOCK_8X4
    {1, 4, 4}, // BLOCK_8X8
    {1, 2, 8}, // BLOCK_8X16
    {1, 2, 8}, // BLOCK_16X8
    {1, 4, 16}, // BLOCK_16X16
    {1, 2, 8}, // BLOCK_16X32
    {1, 2, 8}, // BLOCK_32X16
    {1, 4, 16}, // BLOCK_32X32
    {1, 2, 8}, // BLOCK_32X64
    {1, 2, 8}, // BLOCK_64X32
    {1, 4, 16}, // BLOCK_64X64
    {2, 2, 2}, // BLOCK_64X128
    {2, 2, 2}, // BLOCK_128X64
    {4, 4, 4}, // BLOCK_128X128
    {1, 2, 4}, // BLOCK_4X16
    {1, 2, 4}, // BLOCK_16X4
    {1, 2, 4}, // BLOCK_8X32
    {1, 2, 4}, // BLOCK_32X8
    {1, 2, 4}, // BLOCK_16X64
    {1, 2, 4} // BLOCK_64X16
};

// tx_depth_to_tx_size (common_utils.c:95-115, verbatim)
const TxSize txDepthToTxSize[3][22] = {
    {TX_4X4, TX_4X8, TX_8X4, TX_8X8, TX_8X16, TX_16X8, TX_16X16,
     TX_16X32, TX_32X16, TX_32X32, TX_32X64, TX_64X32, TX_64X64,
     TX_64X64, TX_64X64, TX_64X64, TX_4X16, TX_16X4, TX_8X32, TX_32X8, TX_16X64, TX_64X16},
    {TX_4X4, TX_4X8, TX_8X4, TX_4X4, TX_8X8, TX_8X8, TX_8X8,
     TX_16X16, TX_16X16, TX_16X16, TX_32X32, TX_32X32, TX_32X32,
     TX_64X64, TX_64X64, TX_64X64, TX_4X8, TX_8X4, TX_8X16, TX_16X8, TX_16X32, TX_32X16},
    {TX_4X4, TX_4X8, TX_8X4, TX_8X8, TX_4X4, TX_4X4, TX_4X4, TX_8X8, TX_8X8, TX_8X8, TX_16X16, TX_16X16, TX_16X16,
     TX_64X64, TX_64X64, TX_64X64, TX_4X4, TX_4X4, TX_8X8, TX_8X8, TX_16X16, TX_16X16}
};

// txsize_to_bsize (inv_transforms.h:319-339, verbatim)
const BlockSize txsizeToBsize[TX_SIZES_ALL] = {
    BLOCK_4X4, BLOCK_8X8, BLOCK_16X16, BLOCK_32X32, BLOCK_64X64,
    BLOCK_4X8, BLOCK_8X4, BLOCK_8X16, BLOCK_16X8, BLOCK_16X32, BLOCK_32X16,
    BLOCK_32X64, BLOCK_64X32, BLOCK_4X16, BLOCK_16X4, BLOCK_8X32, BLOCK_32X8,
    BLOCK_16X64, BLOCK_64X16
};

// eb_tx_size_wide_unit / eb_tx_size_high_unit (common_utils.c:65-72, verbatim)
const std::int32_t ebTxSizeWideUnit[TX_SIZES_ALL] = {1, 2, 4, 8, 16, 1, 2, 2, 4, 4, 8, 8, 16, 1, 4, 2, 8, 4, 16};
const std::int32_t ebTxSizeHighUnit[TX_SIZES_ALL] = {1, 2, 4, 8, 16, 2, 1, 4, 2, 8, 4, 16, 8, 4, 1, 8, 2, 16, 4};

// entropy_coding.c:248-315 port. plane 0 (LUMA); the NA's above/left arrays
// passed explicitly. The dc_sign signs table {0, -1, 1} (:256) and the OR
// accumulation (:264-285), dc_sign_ctx derivation (:288-294), then:
//   plane 0 && plane_bsize == tx_bsize -> txb_skip_ctx = 0 (:298-299)
//   plane 0 && plane_bsize != tx_bsize -> skip_contexts table (:301-308)
//   plane != 0 -> chroma path (:310-314) â€” dead for our luma TUs, ported
//   for the range rule.
void getTxbCtx(const std::uint8_t* above_ptr, const std::uint8_t* left_ptr, int txb_w_unit,
               int txb_h_unit, int plane, BlockSize plane_bsize, TxSize tx_size,
               int* txb_skip_ctx, int* dc_sign_ctx) {
    static const int8_t signs[3] = {0, -1, 1};
    int16_t dc_sign = 0;
    int32_t top = 0;
    int32_t left = 0;

    if (above_ptr[0] != INVALID_NEIGHBOR_DATA) {
        for (int32_t k = 0; k < txb_w_unit; ++k) {
            std::uint8_t v = above_ptr[k];
            std::uint8_t sign = v >> COEFF_CONTEXT_BITS;
            dc_sign += signs[sign];
            top |= v;
        }
    }
    if (left_ptr[0] != INVALID_NEIGHBOR_DATA) {
        for (int32_t k = 0; k < txb_h_unit; ++k) {
            std::uint8_t v = left_ptr[k];
            std::uint8_t sign = v >> COEFF_CONTEXT_BITS;
            dc_sign += signs[sign];
            left |= v;
        }
    }

    if (dc_sign > 0) {
        *dc_sign_ctx = 2;
    } else if (dc_sign < 0) {
        *dc_sign_ctx = 1;
    } else {
        *dc_sign_ctx = 0;
    }

    const BlockSize tx_bsize = txsizeToBsize[tx_size];
    if (plane == 0) {
        if (plane_bsize == tx_bsize) {
            *txb_skip_ctx = 0;
        } else {
            static const std::uint8_t skip_contexts[5][5] = {
                {1, 2, 2, 2, 3}, {1, 4, 4, 4, 5}, {1, 4, 4, 4, 5}, {1, 4, 4, 4, 5}, {1, 4, 4, 4, 6}};
            top &= COEFF_CONTEXT_MASK;
            left &= COEFF_CONTEXT_MASK;
            const int32_t max = AOMMIN(top | left, 4);
            const int32_t min = AOMMIN(AOMMIN(top, left), 4);
            *txb_skip_ctx = skip_contexts[min][max];
        }
    } else {
        const int32_t ctx_base = ((left != 0) + (top != 0));
        // eb_num_pels_log2_lookup simplified: our TUs are square, the
        // plane/tx bsize comparison is always >= for our whole-block case
        // -> ctx_offset = 10 for larger plane, 7 otherwise. Dead for luma
        // whole-block TUs.
        const int32_t ctx_offset = (blockSizeWide[plane_bsize] * blockSizeHigh[plane_bsize] >
                                    blockSizeWide[tx_bsize] * blockSizeHigh[tx_bsize]) ? 10 : 7;
        *txb_skip_ctx = static_cast<int16_t>(ctx_base + ctx_offset);
    }
}

// entropy_coding.c:757-820 tx_depth=0 path (whole-block TU, txb_count=1).
// eob is the quantizer output (entropy_coding.c:592 blk_ptr->eob.y[txb_itr]).
// The NA update (:596-603) packs cul_level + dc_sign and writes to the
// above/left arrays over the TU's mi extent.
static void setDcSign(int* cul_level, int dc_val) {
    if (dc_val < 0) {
        *cul_level |= 1 << COEFF_CONTEXT_BITS;
    } else if (dc_val > 0) {
        *cul_level += 2 << COEFF_CONTEXT_BITS;
    }
}

void writeBlockCoeffs(AomWriter* w, EcFrameContext* fc, DcSignLevelCoeffNa* na,
                      const std::int32_t* coeff, const std::int16_t* scan, TxSize tx_size,
                      BlockSize bsize, int eob, int mi_row, int mi_col,
                      int reduced_tx_set, PredictionMode intra_dir) {
    const int tx_w_unit = static_cast<int>(ebTxSizeWideUnit[tx_size]);
    const int tx_h_unit = static_cast<int>(ebTxSizeHighUnit[tx_size]);
    int txb_skip_ctx = 0, dc_sign_ctx = 0;
    getTxbCtx(&na->above[mi_col], &na->left[mi_row], tx_w_unit, tx_h_unit, 0, bsize, tx_size,
              &txb_skip_ctx, &dc_sign_ctx);

    writeTxbCoeffs(w, fc, coeff, scan, tx_size, eob, txb_skip_ctx, dc_sign_ctx, reduced_tx_set, intra_dir);

    // cul_level: sum of abs levels (entropy_coding.c:487/:510 accumulation,
    // clamped at :541), then set_dc_sign (:542). NA update: write the packed
    // byte to above[mi_col..mi_col+tx_w_unit-1] and left[mi_row..mi_row+tx_h_unit-1]
    // (entropy_coding.c:596-603 NA write over the TU extent).
    int32_t cul_level = 0;
    for (int c = 0; c < eob; ++c) {
        cul_level += std::abs(coeff[scan[c]]);
    }
    cul_level = AOMMIN(cul_level, COEFF_CONTEXT_MASK);
    setDcSign(&cul_level, coeff[0]);
    for (int k = 0; k < tx_w_unit; ++k) na->above[mi_col + k] = static_cast<std::uint8_t>(cul_level);
    for (int k = 0; k < tx_h_unit; ++k) na->left[mi_row + k] = static_cast<std::uint8_t>(cul_level);
}

int readBlockCoeffs(AomReader* r, EcFrameContext* fc, DcSignLevelCoeffNa* na,
                    std::int32_t* coeff, const std::int16_t* scan, TxSize tx_size,
                    BlockSize bsize, int mi_row, int mi_col,
                    int reduced_tx_set, PredictionMode intra_dir) {
    const int tx_w_unit = static_cast<int>(ebTxSizeWideUnit[tx_size]);
    const int tx_h_unit = static_cast<int>(ebTxSizeHighUnit[tx_size]);
    int txb_skip_ctx = 0, dc_sign_ctx = 0;
    getTxbCtx(&na->above[mi_col], &na->left[mi_row], tx_w_unit, tx_h_unit, 0, bsize, tx_size,
              &txb_skip_ctx, &dc_sign_ctx);

    const int eob = readTxbCoeffs(r, fc, coeff, scan, tx_size, txb_skip_ctx, dc_sign_ctx, reduced_tx_set, intra_dir);
    // NA update: same as the writer (aom read side: av1_set_entropy_contexts)
    int32_t cul_level = 0;
    for (int c = 0; c < eob; ++c) cul_level += std::abs(coeff[scan[c]]);
    cul_level = AOMMIN(cul_level, COEFF_CONTEXT_MASK);
    if (eob > 0) setDcSign(&cul_level, coeff[scan[0]]);
    for (int k = 0; k < tx_w_unit; ++k) na->above[mi_col + k] = static_cast<std::uint8_t>(cul_level);
    for (int k = 0; k < tx_h_unit; ++k) na->left[mi_row + k] = static_cast<std::uint8_t>(cul_level);
    return eob;
}

// entropy_coding.c:355-544 (LUMA DCT_DCT port)
void writeTxbCoeffs(AomWriter* w, EcFrameContext* fc, const std::int32_t* coeff,
                    const std::int16_t* scan, TxSize tx_size, int eob, int txb_skip_ctx,
                    int dc_sign_ctx, int reduced_tx_set, PredictionMode intra_dir) {
    const TxSize txs_ctx        = getTxsizeEntropyCtx(tx_size);
    const int    eob_multi_size = txsizeLog2Minus4[tx_size];
    const int    eob_multi_ctx  = 0;  // TX_CLASS_2D (tx_type_to_class[DCT_DCT])

    odEcWriteSymbol(w, eob == 0, fc->txb_skip_cdf[txs_ctx][txb_skip_ctx], 2);
    if (eob == 0) return;

    // TS3: tx-type emission (entropy_coding.c:374-376). q100 gate: the
    // caller passes base_q_idx via the reduced_tx_set parameter (repurposed
    // for the DCT_DCT-only port — reduced_tx_set > 0 implies q > 0 for the
    // DCT_DCT-only call sites). The DCT_DCT-only port always emits the
    // DCT_DCT index through the DTT4_IDTX set (eset 2, 5 symbols) for
    // reduced_tx_set=1 intra. The intra_dir is the caller's luma mode.
    if (reduced_tx_set > 0) {
        const TxSize sq = txsizeSqrMap[tx_size];
        odEcWriteSymbol(w, av1ExtTxInd[EXT_TX_SET_DTT4_IDTX][DCT_DCT],
                        fc->intra_ext_tx_cdf[2][sq][intra_dir], 5);
    }

    int eob_extra;
    const int eob_pt = getEobPosToken(eob, &eob_extra);
    AomCdfProb* eob_cdf;
    int nsyms;
    switch (eob_multi_size) {
    case 0: eob_cdf = fc->eob_flag_cdf16[0][eob_multi_ctx]; nsyms = 5; break;
    case 1: eob_cdf = fc->eob_flag_cdf32[0][eob_multi_ctx]; nsyms = 6; break;
    case 2: eob_cdf = fc->eob_flag_cdf64[0][eob_multi_ctx]; nsyms = 7; break;
    case 3: eob_cdf = fc->eob_flag_cdf128[0][eob_multi_ctx]; nsyms = 8; break;
    case 4: eob_cdf = fc->eob_flag_cdf256[0][eob_multi_ctx]; nsyms = 9; break;
    case 5: eob_cdf = fc->eob_flag_cdf512[0][eob_multi_ctx]; nsyms = 10; break;
    default: eob_cdf = fc->eob_flag_cdf1024[0][eob_multi_ctx]; nsyms = 11; break;
    }
    odEcWriteSymbol(w, eob_pt - 1, eob_cdf, nsyms);
    if (eob_pt > 2) {
        const int cnt = eob_pt - 3;
        const int bit = (eob_extra >> cnt) & 1;
        odEcWriteSymbol(w, bit, fc->eob_extra_cdf[txs_ctx][0][cnt], 2);
        odEcWriteLiteralBits(w, eob_extra, cnt);
    }

    const int bwl    = getTxbBwl(tx_size);
    const int width  = getTxbWide(tx_size);
    const int height = getTxbHigh(tx_size);

    std::uint8_t levels[TX_PAD_2D];
    memset(levels, 0, sizeof(levels));
    txbInitLevels(coeff, width, height, levels);
    std::int8_t coeff_contexts[TX_PAD_2D];
    getNzMapContexts(levels, scan, static_cast<std::uint16_t>(eob), tx_size, TX_CLASS_2D, coeff_contexts);

    const int br_txs_ctx = AOMMIN(txs_ctx, TX_32X32);

    // last coefficient (scan[eob-1]): base_eob (AOMMIN(level,3)-1) + br
    {
        const int c   = eob - 1;
        const int pos = scan[c];
        const int lctx = (eob == 1) ? 0 : coeff_contexts[pos];
        const std::int32_t level = std::abs(coeff[pos]);
        odEcWriteSymbol(w, AOMMIN(level, 3) - 1, fc->coeff_base_eob_cdf[txs_ctx][0][lctx], 3);
        if (level > NUM_BASE_LEVELS) {
            const int32_t base_range = level - 1 - NUM_BASE_LEVELS;
            const int br_ctx = (eob == 1) ? 0 : getBrCtxEob(pos, bwl, TX_CLASS_2D);
            if (eob == 1) {
                // base_eob br_ctx: aom get_br_ctx_eob(pos, bhl, tx_class); for eob==1, pos=0 -> 0
            }
            for (int32_t idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
                const int32_t k = AOMMIN(base_range - idx, BR_CDF_SIZE - 1);
                odEcWriteSymbol(w, k, fc->coeff_br_cdf[br_txs_ctx][0][br_ctx], BR_CDF_SIZE);
                if (k < BR_CDF_SIZE - 1) break;
            }
        }
    }
    // reverse pass c = eob-2 .. 0: base (AOMMIN(level,3)) + br
    for (int c = eob - 2; c >= 0; --c) {
        const int pos = scan[c];
        const int coeff_ctx = coeff_contexts[pos];
        const std::int32_t level = std::abs(coeff[pos]);
        odEcWriteSymbol(w, AOMMIN(level, 3), fc->coeff_base_cdf[txs_ctx][0][coeff_ctx], 4);
        if (level > NUM_BASE_LEVELS) {
            const int32_t base_range = level - 1 - NUM_BASE_LEVELS;
            const int br_ctx = getBrCtx(levels, pos, bwl, TX_CLASS_2D);
            for (int32_t idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
                const int32_t k = AOMMIN(base_range - idx, BR_CDF_SIZE - 1);
                odEcWriteSymbol(w, k, fc->coeff_br_cdf[br_txs_ctx][0][br_ctx], BR_CDF_SIZE);
                if (k < BR_CDF_SIZE - 1) break;
            }
        }
    }
    // forward pass: signs + golomb
    for (int c = 0; c < eob; ++c) {
        const int pos = scan[c];
        const std::int32_t v = coeff[pos];
        const std::int32_t level = std::abs(v);
        if (!level) continue;
        if (c == 0) {
            odEcWriteSymbol(w, (v < 0) ? 1 : 0, fc->dc_sign_cdf[0][dc_sign_ctx], 2);
        } else {
            odEcWriteLiteralBit(w, (v < 0) ? 1 : 0, 1);
        }
        if (level > COEFF_BASE_RANGE + NUM_BASE_LEVELS) {
            writeGolomb(w, level - COEFF_BASE_RANGE - 1 - NUM_BASE_LEVELS);
        }
    }
}

// aom decodetxb.c read_coeffs_txb (symbol-for-symbol twin)
int readTxbCoeffs(AomReader* r, EcFrameContext* fc, std::int32_t* coeff,
                  const std::int16_t* scan, TxSize tx_size, int txb_skip_ctx, int dc_sign_ctx,
                  int reduced_tx_set, PredictionMode intra_dir) {
    const TxSize txs_ctx        = getTxsizeEntropyCtx(tx_size);
    const int    eob_multi_size = txsizeLog2Minus4[tx_size];
    const int    eob_multi_ctx  = 0;
    memset(coeff, 0, sizeof(std::int32_t) * (getTxbWide(tx_size) * getTxbHigh(tx_size)));

    const int all_zero = odEcReadSymbol(r, fc->txb_skip_cdf[txs_ctx][txb_skip_ctx], 2);
    if (all_zero) return 0;

    // TS3: tx-type read (entropy_coding.c:374-376 mirror)
    if (reduced_tx_set > 0) {
        const TxSize sq = txsizeSqrMap[tx_size];
        const int ttx = odEcReadSymbol(r, fc->intra_ext_tx_cdf[2][sq][intra_dir], 5);
        (void)ttx;  // DCT_DCT-only port
    }

    int eob_pt;
    switch (eob_multi_size) {
    case 0: eob_pt = odEcReadSymbol(r, fc->eob_flag_cdf16[0][eob_multi_ctx], 5) + 1; break;
    case 1: eob_pt = odEcReadSymbol(r, fc->eob_flag_cdf32[0][eob_multi_ctx], 6) + 1; break;
    case 2: eob_pt = odEcReadSymbol(r, fc->eob_flag_cdf64[0][eob_multi_ctx], 7) + 1; break;
    case 3: eob_pt = odEcReadSymbol(r, fc->eob_flag_cdf128[0][eob_multi_ctx], 8) + 1; break;
    case 4: eob_pt = odEcReadSymbol(r, fc->eob_flag_cdf256[0][eob_multi_ctx], 9) + 1; break;
    case 5: eob_pt = odEcReadSymbol(r, fc->eob_flag_cdf512[0][eob_multi_ctx], 10) + 1; break;
    default: eob_pt = odEcReadSymbol(r, fc->eob_flag_cdf1024[0][eob_multi_ctx], 11) + 1; break;
    }
    int eob_extra = 0;
    {
        const int eob_offset_bits = (eob_pt > 2) ? (eob_pt - 2) : 0;
        if (eob_offset_bits > 0) {
            const int eob_ctx = eob_pt - 3;
            if (odEcReadSymbol(r, fc->eob_extra_cdf[txs_ctx][0][eob_ctx], 2)) {
                eob_extra += (1 << (eob_offset_bits - 1));
            }
            for (int i = 1; i < eob_offset_bits; i++) {
                if (odEcReadBit(r)) eob_extra += (1 << (eob_offset_bits - 1 - i));
            }
        }
    }
    // rec_eob_pos: group_start[1]=1, [2]=2, [t>2]=(1<<(t-2))+1
    const int eob = (eob_pt <= 2) ? eob_pt : ((1 << (eob_pt - 2)) + 1 + eob_extra);

    const int bwl    = getTxbBwl(tx_size);
    const int width  = getTxbWide(tx_size);
    (void)width;
    const int height = getTxbHigh(tx_size);
    std::uint8_t levels[TX_PAD_2D];
    memset(levels, 0, sizeof(levels));
    const int br_txs_ctx = AOMMIN(txs_ctx, TX_32X32);

    // last coefficient
    {
        const int c   = eob - 1;
        const int pos = scan[c];
        const int lctx = getLowerLevelsCtxEob(bwl, height, c);
        int level = odEcReadSymbol(r, fc->coeff_base_eob_cdf[txs_ctx][0][lctx], 3) + 1;
        if (level > NUM_BASE_LEVELS) {
            const int br_ctx = getBrCtxEob(pos, bwl, TX_CLASS_2D);
            for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
                const int k = odEcReadSymbol(r, fc->coeff_br_cdf[br_txs_ctx][0][br_ctx], BR_CDF_SIZE);
                level += k;
                if (k < BR_CDF_SIZE - 1) break;
            }
        }
        levels[getPaddedIdx(pos, bwl)] = static_cast<std::uint8_t>(level);
    }
    // reverse pass
    for (int c = eob - 2; c >= 0; --c) {
        const int pos = scan[c];
        const int coeff_ctx = getLowerLevelsCtx(levels, pos, bwl, tx_size, TX_CLASS_2D);
        int level = odEcReadSymbol(r, fc->coeff_base_cdf[txs_ctx][0][coeff_ctx], 4);
        if (level > NUM_BASE_LEVELS) {
            const int br_ctx = getBrCtx(levels, pos, bwl, TX_CLASS_2D);
            for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
                const int k = odEcReadSymbol(r, fc->coeff_br_cdf[br_txs_ctx][0][br_ctx], BR_CDF_SIZE);
                level += k;
                if (k < BR_CDF_SIZE - 1) break;
            }
        }
        levels[getPaddedIdx(pos, bwl)] = static_cast<std::uint8_t>(level);
    }
    // forward pass: signs + golomb
    for (int c = 0; c < eob; ++c) {
        const int pos = scan[c];
        const int level = levels[getPaddedIdx(pos, bwl)];
        if (!level) continue;
        int sign;
        if (c == 0) {
            sign = odEcReadSymbol(r, fc->dc_sign_cdf[0][dc_sign_ctx], 2);
        } else {
            sign = odEcReadBit(r);
        }
        int lv = level;
        if (lv >= MAX_BASE_BR_RANGE) {
            lv += readGolomb(r);
        }
        coeff[pos] = sign ? -lv : lv;
    }
    return eob;
}

}  // namespace entropy
