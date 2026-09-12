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

}  // namespace entropy
