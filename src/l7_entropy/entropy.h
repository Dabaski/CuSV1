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
#define CDF_SIZE(x) ((x) + 1)  // cabac_context_model.h:38
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

// ---------------------------------------------------------------------------
// Symbol wrappers with adaptation (EC2).
// ---------------------------------------------------------------------------
// AomWriter - bitstream_unit.h:222-228. Deviation: buffer_parent dropped
// (host port owns its buffer; aom_start_encode/ensure_capacity glue not
// ported - the caller assigns ec.buf), pos kept for odEcStopEncode
// semantics.
struct AomWriter {
    OdEcEnc ec;
    std::uint32_t allow_update_cdf;
    std::uint32_t pos;
};

// aom_write_symbol (bitstream_unit.h:265-279): nsymbs == 2 routes to
// odEcEncodeBoolQ15 at the CURRENT adapted cdf[0]; otherwise the cdf path;
// then updateCdf when allow_update_cdf.
void odEcWriteSymbol(AomWriter* w, int symb, AomCdfProb* cdf, int nsymbs);

// aom_stop_encode (bitstream_unit.h:245-253): flushes and stores the byte
// count in w->pos.
void odEcStopEncode(AomWriter* w);

// aom_reader - bitreader.h:40-47. Deviation: buffer/buffer_end pointers
// dropped (only used by find_begin/find_end/has_overflowed, not ported).
struct AomReader {
    OdEcDec ec;
    std::uint8_t allow_update_cdf;
};

// aom_reader_init (bitreader.c:14-22)
int odEcReaderInit(AomReader* r, const unsigned char* buffer, std::uint32_t size);

// aom_read_cdf_ (bitreader.h:84-90)
int odEcReadCdf(AomReader* r, const AomCdfProb* cdf, int nsymbs);

// aom_read_symbol_ (bitreader.h:92-98)
int odEcReadSymbol(AomReader* r, AomCdfProb* cdf, int nsymbs);

// ---------------------------------------------------------------------------
// Intra-frame (key-frame) symbol surface (EC3).
// ---------------------------------------------------------------------------
// BlockSize - definitions.h:883-905 (verbatim order; BLOCK_SIZES_ALL
// sentinel). Deviation: l0_core carries a project-minimal 2-value
// BlockSize; this layer mirrors the full SVT enum because the CDF tables
// (filter_intra_cdfs) index by it.
enum BlockSize {
    BLOCK_4X4,
    BLOCK_4X8,
    BLOCK_8X4,
    BLOCK_8X8,
    BLOCK_8X16,
    BLOCK_16X8,
    BLOCK_16X16,
    BLOCK_16X32,
    BLOCK_32X16,
    BLOCK_32X32,
    BLOCK_32X64,
    BLOCK_64X32,
    BLOCK_64X64,
    BLOCK_64X128,
    BLOCK_128X64,
    BLOCK_128X128,
    BLOCK_4X16,
    BLOCK_16X4,
    BLOCK_8X32,
    BLOCK_32X8,
    BLOCK_16X64,
    BLOCK_64X16,
    BLOCK_SIZES_ALL,
};

// PredictionMode, intra subset (definitions.h:1169-1206: the intra modes and
// the INTRA_MODES sentinel; the inter modes NEARESTMV..MB_MODE_COUNT are not
// part of this layer's surface).
enum PredictionMode {
    DC_PRED,     // Average of above and left pixels
    V_PRED,      // Vertical
    H_PRED,      // Horizontal
    D45_PRED,    // Directional 45 degree
    D135_PRED,   // Directional 135 degree
    D113_PRED,   // Directional 113 degree
    D157_PRED,   // Directional 157 degree
    D203_PRED,   // Directional 203 degree
    D67_PRED,    // Directional 67  degree
    SMOOTH_PRED, // Combination of horizontal and vertical interpolation
    SMOOTH_V_PRED, // Vertical interpolation
    SMOOTH_H_PRED, // Horizontal interpolation
    PAETH_PRED,
    INTRA_MODES = PAETH_PRED + 1, // PAETH_PRED has to be the last intra mode.
};

// FilterIntraMode (definitions.h:1295-1302)
enum FilterIntraMode {
    FILTER_DC_PRED,
    FILTER_V_PRED,
    FILTER_H_PRED,
    FILTER_D157_PRED,
    FILTER_PAETH_PRED,
    FILTER_INTRA_MODES,
};

#define KF_MODE_CONTEXTS 5      // cabac_context_model.h:262
#define DIRECTIONAL_MODES 8     // definitions.h:1305
#define MAX_ANGLE_DELTA 3       // definitions.h:1306

// av1_is_directional_mode (intra_prediction.h:206-208)
inline int isDirectionalMode(PredictionMode mode) {
    return mode >= V_PRED && mode <= D67_PRED;
}

// block_size_wide/high (common_utils.c:286-291)
extern const std::uint8_t blockSizeWide[BLOCK_SIZES_ALL];
extern const std::uint8_t blockSizeHigh[BLOCK_SIZES_ALL];
// intra_mode_context (common_utils.c:134-148)
extern const std::uint8_t intraModeContext[INTRA_MODES];

// FRAME_CONTEXT slice (cabac_context_model.h:299-345, the four tables this
// layer writes/reads).
struct EcFrameContext {
    AomCdfProb kf_y_cdf[KF_MODE_CONTEXTS][KF_MODE_CONTEXTS][CDF_SIZE(INTRA_MODES)];
    AomCdfProb angle_delta_cdf[DIRECTIONAL_MODES][CDF_SIZE(2 * MAX_ANGLE_DELTA + 1)];
    AomCdfProb filter_intra_cdfs[BLOCK_SIZES_ALL][CDF_SIZE(2)];
    AomCdfProb filter_intra_mode_cdf[CDF_SIZE(FILTER_INTRA_MODES)];
};

// Initialize from the SVT default tables (cabac_context_model.c:59-97,
// :614-623; RESET-INIT equivalent of svt_aom_av1_setup_frame_context
// COPY_CDF, cabac_context_model.c:740-767 for these four tables).
void initDefaultEcFrameContext(EcFrameContext* fc);

// Bitwise equality of the four tables (test-side adapted-cdf comparison).
int ecFrameCdfsEqual(const EcFrameContext* a, const EcFrameContext* b);

// svt_aom_get_kf_y_mode_ctx (entropy_coding.c:1004-1021), flattened:
// SVT reads neighbor modes from xd (left_available/up_available derefs);
// the host port takes them explicitly. Unavailable -> DC_PRED context.
void getKfYModeCtx(int left_available, int left_mode, int up_available, int up_mode,
                   int* above_ctx, int* left_ctx);

// svt_aom_filter_intra_allowed_bsize (mode_decision.c:108-112)
int filterIntraAllowedBsize(BlockSize bs);

// svt_aom_filter_intra_allowed (mode_decision.c:115-119)
int filterIntraAllowed(std::uint8_t enable_filter_intra, BlockSize bsize,
                       std::uint8_t palette_size, std::uint32_t mode);

// encode_intra_luma_mode_kf_av1 (entropy_coding.c:1026-1040): mode symbol
// from kf_y_cdf[above_ctx][left_ctx], then the angle-delta symbol when
// bsize >= BLOCK_8X8 and the mode is directional. Deviation: the context
// pair is passed in (SVT derives it inside from blk_ptr->av1xd).
void writeKfLumaMode(AomWriter* w, EcFrameContext* fc, BlockSize bsize,
                     PredictionMode mode, int above_ctx, int left_ctx, int angle_delta);

// Decode side of the same surface: mode symbol, then the angle-delta symbol
// indexed by the DECODED mode (delta out as delta + MAX_ANGLE_DELTA).
PredictionMode readKfLumaMode(AomReader* r, EcFrameContext* fc, BlockSize bsize,
                              int above_ctx, int left_ctx, int* angle_delta);

// Filter-intra pair (entropy_coding.c:5050-5058): flag symbol
// (fi_mode != FILTER_INTRA_MODES) then, when set, the FI mode symbol.
void writeFilterIntra(AomWriter* w, EcFrameContext* fc, BlockSize bsize,
                      FilterIntraMode fi_mode);
// Return: the flag value (fi_mode != FILTER_INTRA_MODES).
int readFilterIntra(AomReader* r, EcFrameContext* fc, BlockSize bsize,
                    FilterIntraMode* fi_mode);

// svt_od_ec_enc_done (bitstream_unit.c:309-343)
unsigned char* odEcEncDone(OdEcEnc* enc, std::uint32_t* nbytes);

}  // namespace entropy