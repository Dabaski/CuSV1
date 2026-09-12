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

}  // namespace entropy
