#pragma once

#include <climits>
#include <cstdlib>
#include <cmath>
#include <cstdint>

#define AOMMIN(x, y) (((x) < (y)) ? (x) : (y))  // definitions.h:1001

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

// aom_write_bit (bitstream_unit.h:255-257): od_ec_encode_bool_eq_q15
void odEcWriteLiteralBit(AomWriter* w, int bit, int unused_bits);
// aom_write_literal (bitstream_unit.h:259-263)
void odEcWriteLiteralBits(AomWriter* w, unsigned data, int bits);

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

// aom_read_bit (bitreader.h:71-75): od_ec_decode_bool_q15 at f=128 (half)
int odEcReadBit(AomReader* r);

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

// CflAllowedType (definitions.h:1121, verbatim)
enum CflAllowedType { CFL_DISALLOWED, CFL_ALLOWED, CFL_ALLOWED_TYPES };

// UvPredictionMode (definitions.h:1210-1227, verbatim order). UV_CFL_PRED is
// enumerated but the CFL-specific combine (cfl_alpha AC-from-luma) is OUT of
// scope (named deviation, CH-series/CS1): at the prediction surface SVT folds
// UV_CFL_PRED to DC_PRED (g_uv2y, common_utils.c:14-31).
enum UvPredictionMode {
    UV_DC_PRED = 0,
    UV_V_PRED = 1,
    UV_H_PRED = 2,
    UV_D45_PRED = 3,
    UV_D135_PRED = 4,
    UV_D113_PRED = 5,
    UV_D157_PRED = 6,
    UV_D203_PRED = 7,
    UV_D67_PRED = 8,
    UV_SMOOTH_PRED = 9,
    UV_SMOOTH_V_PRED = 10,
    UV_SMOOTH_H_PRED = 11,
    UV_PAETH_PRED = 12,
    UV_CFL_PRED = 13,
    UV_INTRA_MODES = 14,
    UV_MODE_INVALID = 15,
};

// get_uv_mode (common_utils.h:130-133) = g_uv2y[mode]
// (common_utils.c:14-31, verbatim table): UV modes fold to the luma
// primitive set, UV_CFL_PRED -> DC_PRED, sentinels -> INTRA_INVALID.
PredictionMode uv2y(UvPredictionMode mode);

// ---------------------------------------------------------------------------
// SYM1: palette + segmentation symbol constants (verbatim values).
// ---------------------------------------------------------------------------
#define PALETTE_MAX_SIZE 8   // definitions.h:380
#define PALETTE_MIN_SIZE 2   // definitions.h:382
// definitions.h:1143-1151 (TWO_COLORS..EIGHT_COLORS; PALETTE_SIZES = 7).
#define PALETTE_SIZES 7
// definitions.h:1155-1164 (PALETTE_COLOR_ONE..EIGHT; PALETTE_COLORS = 8).
#define PALETTE_COLORS 8
#define PALETTE_COLOR_INDEX_CONTEXTS 5  // cabac_context_model.h:238
#define PALETTE_Y_MODE_CONTEXTS 3       // cabac_context_model.h:246
#define PALETTE_UV_MODE_CONTEXTS 2      // cabac_context_model.h:252
// cabac_context_model.h:260 - SVT's own double-T spelling, kept verbatim.
#define PALATTE_BSIZE_CTXS 7
#define MAX_SEGMENTS 8              // definitions.h:1674
#define SEG_TEMPORAL_PRED_CTXS 3    // cabac_context_model.h:264
#define SPATIAL_PREDICTION_PROBS 3  // cabac_context_model.h:265
// palette.c:639 (the hash bound; the lookup table has MAX + 1 entries).
#define MAX_COLOR_CONTEXT_HASH 8
// SEG_LVL_FEATURES count (segmentation_params.h:17-27; SEG_LVL_MAX = 8).
#define SEG_LVL_MAX 8
// definitions.h:1451
#define PRIMARY_REF_NONE 7

// SYM1: AomWriteBitBuffer (entropy_coding.h:116-119, verbatim layout). The
// l7 twin of the landed l8 copy - both port entropy_coding.c:1343-1382; l7
// cannot link l8 (no CMake edits; the duplication is named).
struct AomWriteBitBuffer {
    std::uint8_t* bit_buffer;
    std::uint32_t bit_offset;
};

// SYM1: the aom read_bit_buffer (aom bitreader_buffer.h:21-27). The error
// handler is dropped: out-of-buffer reads return 0 (named deviation; the
// seg fixture buffers are sized to never hit it).
struct AomReadBitBuffer {
    const std::uint8_t* bit_buffer;
    const std::uint8_t* bit_buffer_end;
    std::uint32_t bit_offset;
};

// svt_aom_wb_write_bit / _literal / _inv_signed_literal
// (entropy_coding.c:1351-1382).
void wbWriteBit(AomWriteBitBuffer* wb, std::int32_t bit);
void wbWriteLiteral(AomWriteBitBuffer* wb, std::int32_t data, std::int32_t bits);
void wbWriteInvSignedLiteral(AomWriteBitBuffer* wb, std::int32_t data, std::int32_t bits);

// aom aom_rb_read_bit / _literal / _inv_signed_literal
// (bitreader_buffer.c:25-60).
int rbReadBit(AomReadBitBuffer* rb);
int rbReadLiteral(AomReadBitBuffer* rb, int bits);
int rbReadInvSignedLiteral(AomReadBitBuffer* rb, int bits);

// svt_aom_segmentation_feature_bits / _signed (segmentation_params.c:18/:16).
extern const std::int32_t segFeatureBits[SEG_LVL_MAX];
extern const std::int32_t segFeatureSigned[SEG_LVL_MAX];

// SYM1: encode_segmentation (entropy_coding.c:2253-2280), the
// PictureParentControlSet wall threaded: the enabled/update flags, the
// primary_ref_frame (the update-flag block is written only when
// primary_ref_frame != PRIMARY_REF_NONE, :2257), and the feature grid.
void writeSegFeatures(AomWriteBitBuffer* wb, int enabled, int update_map, int temporal_update,
                      int update_data, int primary_ref_frame,
                      const std::int16_t feature_enabled[MAX_SEGMENTS][SEG_LVL_MAX],
                      const std::int16_t feature_data[MAX_SEGMENTS][SEG_LVL_MAX]);

// SYM1: the aom setup_segmentation reader twin (aom decodeframe.c, the
// out-of-tree arbiter; the SVT tree carries no decoder side for the frame
// header). The reader reads the enabled bit ITSELF (no enabled parameter -
// the writer takes it, the reader reads it); on primary_ref_frame ==
// PRIMARY_REF_NONE the update flags are FORCED (update_map=1, temporal=0,
// update_data=1). The reader CLAMPS each datum into [-max, max]
// (av1_seg_feature_data_max = (1 << bits) - 1); the pinned writer does not
// clamp (its in-file TODO) - in-span values pass through unchanged.
void readSegFeatures(AomReadBitBuffer* rb, int update_map, int temporal_update,
                     int update_data, int primary_ref_frame,
                     std::int16_t feature_enabled[MAX_SEGMENTS][SEG_LVL_MAX],
                     std::int16_t feature_data[MAX_SEGMENTS][SEG_LVL_MAX]);

// SYM1: the segmentation map surface. The Av1Common wall is threaded as
// (segment_ids, mi_cols, mi_rows); the map is a per-mi-cell byte array.
// svt_aom_get_segment_id (entropy_coding.c:4723-4740): the MIN over the
// block's mi extent (edge-clamped).
int getSegmentId(const std::uint8_t* segment_ids, int mi_cols, int mi_rows, BlockSize bsize,
                 int mi_row, int mi_col);
// svt_av1_update_segmentation_map (:4843-4861): writes segment_id over the
// block's mi extent (edge-clamped).
void updateSegmentationMap(std::uint8_t* segment_ids, int mi_cols, int mi_rows, BlockSize bsize,
                           int mi_row, int mi_col, std::uint8_t segment_id);
// svt_av1_get_spatial_seg_prediction (:4744-4800): the cdf index (0..2) out
// and the predictor (the majority-or-left rule) returned.
int getSpatialSegPrediction(const std::uint8_t* segment_ids, int mi_cols, int mi_rows,
                            int up_available, int left_available, int mi_row, int mi_col,
                            int* cdf_index);
// svt_av1_neg_interleave (:4802-4818)
int negInterleave(int x, int ref, int max);
// aom av1_neg_deinterleave (decodemv.c:261-277) - the reader twin.
int negDeinterleave(int diff, int ref, int max);
#define AOMMAX(x, y) (((x) > (y)) ? (x) : (y))  // definitions.h:1002

// SYM1: the l7 twin of the fast palette color-index context (palette.c:
// 639-776; the landed l13_palette twin cannot be linked - no CMake edits -
// so l7 carries its own copy of the same spans).
extern const std::int32_t paletteColorIndexContextLookup[MAX_COLOR_CONTEXT_HASH + 1];
int fastPaletteColorIndexContextOnEdge(const std::uint8_t* colorMap, int stride, int r, int c,
                                       int* colorIdx);
int fastPaletteColorIndexContext(const std::uint8_t* colorMap, int stride, int r, int c,
                                 int* colorIdx);

// eb_num_pels_log2_lookup (common_utils.c:39, verbatim) - the
// get_palette_bsize_ctx lookup (local copy; l13_palette carries its own).
extern const std::uint8_t ebNumPelsLog2Lookup[BLOCK_SIZES_ALL];

// svt_aom_allow_palette (entropy_coding.c:4218-4223). CONFIG_ENABLE_PALETTE
// is 1 for the pinned default config (EbConfigMacros.h:211; the RTC-build
// 0 at :83 DCEs the palette entropy write in RTC only).
int allowPalette(int allow_screen_content_tools, BlockSize bsize);

// svt_aom_get_palette_bsize_ctx (entropy_coding.c:4224-4231).
int getPaletteBsizeCtx(BlockSize bsize);

// svt_aom_get_palette_mode_ctx (entropy_coding.c:4236-4249). DEVIATION
// (named): the MacroBlockD/MbModeInfo wall is threaded - the two consumed
// neighbor facts are the (available, palette_size) pairs.
int getPaletteModeCtx(int above_available, int above_palette_size, int left_available,
                      int left_palette_size);

// delta_encode_palette_colors (entropy_coding.c:4252-4282): the WRITER twin
// of the landed l13_palette::deltaEncodeCost (palette.c:80-109). colors[0]
// as a bit_depth literal, then the 2-bit (bits - min_bits) header and the
// shrinking bits-per-delta chain. min_val is the smallest legal delta (the
// palette caller passes 1).
void deltaEncodePaletteColors(const int* colors, int num, int bit_depth, int min_val, AomWriter* w);

// get_unsigned_bits (entropy_coding.c:4284-4286)
int getUnsignedBits(unsigned int num_values);
// write_uniform (entropy_coding.c:4288-4302)
void writeUniform(AomWriter* w, int n, int v);
// svt_aom_write_uniform_cost (entropy_coding.c:4304-4314); av1_cost_literal
// (md_rate_estimation.h:31, AV1_PROB_COST_SHIFT = 9).
int writeUniformCost(int n, int v);
// aom av1_read_uniform (aom decoder.h:488-496) - the reader twin of
// write_uniform (the out-of-tree arbiter precedent).
int readUniform(AomReader* r, int n);

// aom_read_literal_ (bitreader.h:158-160): bits literal bits, MSB first -
// the reader twin of odEcWriteLiteralBits.
int readLiteralBits(AomReader* r, int bits);

// write_palette_colors_y (entropy_coding.c:4320-4337). DEVIATIONS (named):
// the svt_get_palette_cache_y MacroBlockD wall is threaded (the caller
// passes the cache + n_cache); palette.c's svt_av1_index_color_cache is a
// local twin (the landed l13_palette::indexColorCache cannot be linked -
// l7/l13 duplication, both port palette.c:111-141).
void writePaletteColorsY(AomWriter* w, int bit_depth, const std::uint16_t* color_cache, int n_cache,
                         const std::uint16_t* colors, int n);

// aom read_palette_colors_y (decodemv.c:478-507) - the reader twin. The
// av1_get_palette_cache wall is threaded; colors receives the sorted
// palette (the merge_colors / memcpy tails included).
void readPaletteColorsY(AomReader* r, int bit_depth, const std::uint16_t* color_cache, int n_cache,
                        std::uint16_t* colors, int n);

// ---------------------------------------------------------------------------
// Partition symbol surface (ECP1, court-ordered l7 exception).
// ---------------------------------------------------------------------------
// definitions.h:334 (verbatim value): fresh neighbor-array cells.
#define INVALID_NEIGHBOR_DATA 0xFFu
// definitions.h:943-945 (verbatim).
#define PARTITION_PLOFFSET 4  // number of probability models per block size
#define PARTITION_BLOCK_SIZES 5
#define PARTITION_CONTEXTS (PARTITION_BLOCK_SIZES * PARTITION_PLOFFSET)

// definitions.h:1313 (verbatim value)
#define SKIP_CONTEXTS 3

// ---------------------------------------------------------------------------
// Token/coefficiency surface (TS1). Constants verbatim:
// cabac_context_model.h:109-129, definitions.h:410-426, :1313.
// ---------------------------------------------------------------------------
#define TOKEN_CDF_Q_CTXS 4
#define TXB_SKIP_CONTEXTS 13
#define EOB_COEF_CONTEXTS 9
#define SIG_COEF_CONTEXTS_2D 26
#define SIG_COEF_CONTEXTS_1D 16
#define SIG_COEF_CONTEXTS_EOB 4
#define SIG_COEF_CONTEXTS (SIG_COEF_CONTEXTS_2D + SIG_COEF_CONTEXTS_1D)
#define LEVEL_CONTEXTS 21
#define NUM_BASE_LEVELS 2
#define BR_CDF_SIZE (4)
#define COEFF_BASE_RANGE (4 * (BR_CDF_SIZE - 1))
#define COEFF_CONTEXT_BITS 6
#define COEFF_CONTEXT_MASK ((1 << COEFF_CONTEXT_BITS) - 1)
#define MAX_BASE_BR_RANGE (COEFF_BASE_RANGE + NUM_BASE_LEVELS + 1)
#define DC_SIGN_CONTEXTS 3
#define TX_PAD_HOR_LOG2 2
#define TX_PAD_HOR 4
#define TX_PAD_TOP 0
#define TX_PAD_BOTTOM 4
#define TX_PAD_VER (TX_PAD_TOP + TX_PAD_BOTTOM)
#define TX_PAD_END 16
#define TX_PAD_2D ((64 + TX_PAD_HOR) * (64 + TX_PAD_VER) + TX_PAD_END)
#define EXT_TX_SETS_INTRA 3   // definitions.h:1104
#define EXT_TX_SIZES 4        // definitions.h:1102

// TxType (definitions.h:1017-1037, verbatim order)
enum TxType {
    DCT_DCT, ADST_DCT, DCT_ADST, ADST_ADST, FLIPADST_DCT, DCT_FLIPADST,
    FLIPADST_FLIPADST, ADST_FLIPADST, FLIPADST_ADST, IDTX, V_DCT, H_DCT,
    V_ADST, H_ADST, V_FLIPADST, H_FLIPADST,
    TX_TYPES, INVALID_TX_TYPE,
};

// TxSetType (definitions.h:1071-1082 enum, verbatim order)
enum TxSetType {
    EXT_TX_SET_DCTONLY = 0,
    EXT_TX_SET_DCT_IDTX = 1,
    EXT_TX_SET_DTT4_IDTX = 2,
    EXT_TX_SET_DTT4_IDTX_1DDCT = 3,
    EXT_TX_SET_DTT9_IDTX_1DDCT = 4,
    EXT_TX_SET_ALL16 = 5,
    EXT_TX_SET_TYPES = 6,
};

// PlaneType (definitions.h:685)
enum PlaneType { PLANE_TYPE_Y, PLANE_TYPE_UV, PLANE_TYPES };

// COMPONENT_TYPE (definitions.h:674-681, verbatim values). The token-chain
// component index (entropy_coding.c:374-376 gates the tx-type symbol
// LUMA-only; the six component-dim CDF families index [PLANE_TYPES] =
// {0, 1} - COMPONENT_LUMA/COMPONENT_CHROMA are numerically the PLANE_TYPE
// rows; CB/CR distinctions select buffers, not CDF rows, in our surface).
enum ComponentType {
    COMPONENT_LUMA = 0,       // luma
    COMPONENT_CHROMA = 1,     // chroma (Cb+Cr)
    COMPONENT_CHROMA_CB = 2,  // chroma Cb
    COMPONENT_CHROMA_CR = 3,  // chroma Cr
    COMPONENT_ALL = 4,        // Y+Cb+Cr
    COMPONENT_NONE = 15
};
// TxClass (definitions.h:989-994)
enum TxClass { TX_CLASS_2D = 0, TX_CLASS_HORIZ = 1, TX_CLASS_VERT = 2, TX_CLASSES = 3 };
// TxSize (definitions.h:951-983, full verbatim order - the txb helpers index
// rectangular sizes)
enum TxSize {
    TX_4X4, TX_8X8, TX_16X16, TX_32X32, TX_64X64, TX_4X8, TX_8X4, TX_8X16, TX_16X8,
    TX_16X32, TX_32X16, TX_32X64, TX_64X32, TX_4X16, TX_16X4, TX_8X32, TX_32X8,
    TX_16X64, TX_64X16,
    TX_SIZES_ALL,
    TX_SIZES = TX_4X8,
    TX_SIZES_LARGEST = TX_64X64,
    TX_INVALID = 255,
};

// PartitionType (definitions.h:911-925, verbatim order).
enum PartitionType {
    PARTITION_NONE,
    PARTITION_HORZ,
    PARTITION_VERT,
    PARTITION_SPLIT,
    PARTITION_HORZ_A, // HORZ split and the top partition is split again
    PARTITION_HORZ_B, // HORZ split and the bottom partition is split again
    PARTITION_VERT_A, // VERT split and the left partition is split again
    PARTITION_VERT_B, // VERT split and the right partition is split again
    PARTITION_HORZ_4, // 4:1 horizontal partition
    PARTITION_VERT_4, // 4:1 vertical partition
    EXT_PARTITION_TYPES,
    PARTITION_TYPES = PARTITION_SPLIT + 1,
    PARTITION_INVALID = 255,
};

// av1_is_directional_mode (intra_prediction.h:206-208)
inline int isDirectionalMode(PredictionMode mode) {
    return mode >= V_PRED && mode <= D67_PRED;
}

// block_size_wide/high (common_utils.c:286-291)
extern const std::uint8_t blockSizeWide[BLOCK_SIZES_ALL];
extern const std::uint8_t blockSizeHigh[BLOCK_SIZES_ALL];
// intra_mode_context (common_utils.c:134-148)
extern const std::uint8_t intraModeContext[INTRA_MODES];

// FRAME_CONTEXT slice (cabac_context_model.h:299-345, the tables this
// layer writes/reads).
struct EcFrameContext {
    AomCdfProb kf_y_cdf[KF_MODE_CONTEXTS][KF_MODE_CONTEXTS][CDF_SIZE(INTRA_MODES)];
    AomCdfProb angle_delta_cdf[DIRECTIONAL_MODES][CDF_SIZE(2 * MAX_ANGLE_DELTA + 1)];
    AomCdfProb filter_intra_cdfs[BLOCK_SIZES_ALL][CDF_SIZE(2)];
    AomCdfProb filter_intra_mode_cdf[CDF_SIZE(FILTER_INTRA_MODES)];
    AomCdfProb partition_cdf[PARTITION_CONTEXTS][CDF_SIZE(EXT_PARTITION_TYPES)];
    AomCdfProb skip_cdfs[SKIP_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb txb_skip_cdf[TX_SIZES][TXB_SKIP_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb dc_sign_cdf[PLANE_TYPES][DC_SIGN_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb coeff_base_eob_cdf[TX_SIZES][PLANE_TYPES][SIG_COEF_CONTEXTS_EOB][CDF_SIZE(3)];
    AomCdfProb coeff_base_cdf[TX_SIZES][PLANE_TYPES][SIG_COEF_CONTEXTS][CDF_SIZE(4)];
    AomCdfProb coeff_br_cdf[TX_32X32 + 1][PLANE_TYPES][LEVEL_CONTEXTS][CDF_SIZE(BR_CDF_SIZE)];
    AomCdfProb eob_extra_cdf[TX_SIZES][PLANE_TYPES][EOB_COEF_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb eob_flag_cdf16[PLANE_TYPES][2][CDF_SIZE(5)];
    AomCdfProb eob_flag_cdf32[PLANE_TYPES][2][CDF_SIZE(6)];
    AomCdfProb eob_flag_cdf64[PLANE_TYPES][2][CDF_SIZE(7)];
    AomCdfProb eob_flag_cdf128[PLANE_TYPES][2][CDF_SIZE(8)];
    AomCdfProb eob_flag_cdf256[PLANE_TYPES][2][CDF_SIZE(9)];
    AomCdfProb eob_flag_cdf512[PLANE_TYPES][2][CDF_SIZE(10)];
    AomCdfProb eob_flag_cdf1024[PLANE_TYPES][2][CDF_SIZE(11)];
    AomCdfProb intra_ext_tx_cdf[EXT_TX_SETS_INTRA][EXT_TX_SIZES][INTRA_MODES][CDF_SIZE(TX_TYPES)];
    // CS1b: the uv_mode CDF (cabac_context_model.c:105 verbatim extract).
    // Disallowed rows: 13-symbol (counter at cdf[13]); allowed rows:
    // 14-symbol (counter at cdf[14]).
    AomCdfProb uv_mode_cdf[CFL_ALLOWED_TYPES][INTRA_MODES][CDF_SIZE(UV_INTRA_MODES)];
    // SYM1: the palette CDF families (cabac_context_model.h:306-311,
    // verbatim shapes). The color-index rows are CDF_SIZE(PALETTE_COLORS)
    // wide but code n symbols (n = palette size, 2..8): the adaptation
    // counter sits at cdf[n] (cabac_context_model.c:2000-2001
    // RESET_CDF_COUNTER_STRIDE semantics).
    AomCdfProb palette_y_size_cdf[PALATTE_BSIZE_CTXS][CDF_SIZE(PALETTE_SIZES)];
    AomCdfProb palette_uv_size_cdf[PALATTE_BSIZE_CTXS][CDF_SIZE(PALETTE_SIZES)];
    AomCdfProb palette_y_color_index_cdf[PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS]
                                        [CDF_SIZE(PALETTE_COLORS)];
    AomCdfProb palette_uv_color_index_cdf[PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS]
                                         [CDF_SIZE(PALETTE_COLORS)];
    AomCdfProb palette_y_mode_cdf[PALATTE_BSIZE_CTXS][PALETTE_Y_MODE_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb palette_uv_mode_cdf[PALETTE_UV_MODE_CONTEXTS][CDF_SIZE(2)];
    // SYM1: segmentation_probs (cabac_context_model.h:272-276, verbatim).
    struct EcSegProbs {
        AomCdfProb tree_cdf[CDF_SIZE(MAX_SEGMENTS)];
        AomCdfProb pred_cdf[SEG_TEMPORAL_PRED_CTXS][CDF_SIZE(2)];
        AomCdfProb spatial_pred_seg_cdf[SPATIAL_PREDICTION_PROBS][CDF_SIZE(MAX_SEGMENTS)];
    } seg;
};

// Initialize from the SVT default tables (cabac_context_model.c:59-97,
// :614-623; RESET-INIT equivalent of svt_aom_av1_setup_frame_context
// COPY_CDF, cabac_context_model.c:740-767 for these four tables).
// TD5b: the coefficient tables are QINDEX-BUCKET-SELECTED (the spec's
// init_coeff_cdfs; get_q_ctx cabac_context_model.c:1907-1918).
void initDefaultEcFrameContext(EcFrameContext* fc, std::int32_t base_q_idx);

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

// svt_aom_partition_cdf_length (entropy_coding.c:922-930): 10 symbols
// (EXT_PARTITION_TYPES) for 16x16/32x32/64x64, 4 (PARTITION_TYPES) for 8x8,
// 8 for 128x128.
int partitionCdfLength(BlockSize bsize);

// partition_gather_horz_alike / partition_gather_vert_alike
// (cabac_context_model.h:378-405, verbatim semantics): 2-symbol temporaries
// for the has_rows/has_cols XOR edges.
void partitionGatherHorzAlike(AomCdfProb* out, const AomCdfProb* in, BlockSize bsize);
void partitionGatherVertAlike(AomCdfProb* out, const AomCdfProb* in, BlockSize bsize);

// entropy_coding.c:945-960 flattened: raw context BYTES (fresh cells carry
// INVALID_NEIGHBOR_DATA and map to 0); ctx = (left*2 + above) + bsl*4 with
// above/left = (byte >> bsl) & 1, bsl from the square partition point.
int partitionPlaneContext(std::uint8_t above_byte, std::uint8_t left_byte, BlockSize bsize);

// encode_partition_av1 (entropy_coding.c:932-981) flattened. The caller
// owns the is_partition_point guard (bsize >= BLOCK_8X8, :935) and derives
// has_rows/has_cols with the px rule (:941-943). Forced split (both edges
// absent) writes NOTHING (:962-965); both edges -> full symbol (alphabet
// per partitionCdfLength); XOR edges -> gathered 2-symbol temporary
// (:970-977) whose adaptation is discarded, matching the SVT writer.
void writePartition(AomWriter* w, EcFrameContext* fc, BlockSize bsize, int has_rows, int has_cols,
                    std::uint8_t above_byte, std::uint8_t left_byte, PartitionType p);

// Read twin with the aom read_partition semantics (decodeframe.c:1266-1293,
// the out-of-tree BSF4 conformance arbiter; no aom code extracted): forced
// split -> PARTITION_SPLIT with no symbol; gathered branches read the
// temporary via the NON-adapting cdf read (aom:1284/:1291), consistent with
// the writer discarding the temporary's adaptation.
PartitionType readPartition(AomReader* r, EcFrameContext* fc, BlockSize bsize, int has_rows,
                            int has_cols, std::uint8_t above_byte, std::uint8_t left_byte);

// coding_loop.c:1700-1713: each CODED block writes
// partition_context_lookup[bsize] (definitions.h:1551-1573) over its mi
// extent. above is indexed by mi_col (caller owns mi_cols entries); left is
// indexed (mi_row & 15) - MAX_MIB_MASK for the ratified sb_size 64 (caller
// owns >= 16 entries).
void updatePartitionContext(std::uint8_t* above, std::uint8_t* left, int mi_row, int mi_col,
                            BlockSize bsize);

// av1_get_skip_context (entropy_coding.c:983-989) flattened: above_skip +
// left_skip of the neighbor mbmis, unavailable edges -> 0.
int getSkipContext(int above_available, int above_skip, int left_available, int left_skip);

// encode_skip_coeff_av1 (entropy_coding.c:995-1000): the skip symbol from
// skip_cdfs[ctx], 2 symbols - the FIRST arithmetic-coded symbol of each
// I_SLICE block for our config (write_modes_b :4980-4985, segmentation off;
// the commented write_skip lines :4978/:5123 are superseded aom-style calls).
void writeSkip(AomWriter* w, EcFrameContext* fc, int ctx, int skip);

// Read twin with the aom read_skip_txfm semantics (decodemv.c, out-of-tree
// BSF4 arbiter; no aom code extracted) minus the SEG_LVL_SKIP implicit-1
// branch - the segmentation surface is deferred with segmentation itself.
int readSkip(AomReader* r, EcFrameContext* fc, int ctx);

// ---------------------------------------------------------------------------
// TS1: per-TU coefficient chain (entropy_coding.c:355-544, LUMA DCT_DCT
// path). The token tables were added to EcFrameContext above. The luma-only
// port carries no tx-type symbol (the :321-322 gate's emission lands in TS3
// with the tx-type surface; the q0 structural keyframe never emits it).
// ---------------------------------------------------------------------------

// txsize_log2_minus4 (inv_transforms.h:341-359, verbatim: log2(num_coeffs)-4)
extern const std::int8_t txsizeLog2Minus4[TX_SIZES_ALL];
// tx_size_wide/high/log2 (common_utils.c:116-128, verbatim)
extern const std::int32_t txSizeWide[TX_SIZES_ALL];
extern const std::int32_t txSizeHigh[TX_SIZES_ALL];
extern const std::int32_t txSizeWideLog2[TX_SIZES_ALL];
// txsize_sqr_map / txsize_sqr_up_map (common_utils.c:150/:172)
extern const TxSize txsizeSqrMap[TX_SIZES_ALL];
extern const TxSize txsizeSqrUpMap[TX_SIZES_ALL];

// get_txsize_entropy_ctx (entropy_coding.h:110-112)
TxSize getTxsizeEntropyCtx(TxSize txsize);
// get_txb_bwl/wide/high (common_utils.h:115-128; via av1_get_adjusted_tx_size)
int getTxbBwl(TxSize tx_size);
int getTxbWide(TxSize tx_size);
int getTxbHigh(TxSize tx_size);
// get_padded_idx (coefficients.h:129-131)
int getPaddedIdx(int idx, int bwl);
// get_nz_mag (coefficients.h:133-155)
int getNzMag(const std::uint8_t* levels, int bwl, TxClass tx_class);
// get_nz_map_ctx_from_stats (coefficients.h:157-194; 2D via the offset LUT)
int getNzMapCtxFromStats(int stats, int coeff_idx, int bwl, TxSize tx_size, TxClass tx_class);
// get_lower_levels_ctx_eob (coefficients.h:56-67) / get_lower_levels_ctx (:196)
int getLowerLevelsCtxEob(int bwl, int height, int scan_idx);
int getLowerLevelsCtx(const std::uint8_t* levels, int coeff_idx, int bwl, TxSize tx_size, TxClass tx_class);
// get_br_ctx_eob (coefficients.h:69-81) / get_br_ctx (:83-127)
int getBrCtxEob(int c, int bwl, TxClass tx_class);
int getBrCtx(const std::uint8_t* levels, int c, int bwl, TxClass tx_class);
// svt_av1_txb_init_levels_c (rd_cost.c:93-105)
void txbInitLevels(const std::int32_t* coeff, int width, int height, std::uint8_t* levels);
// svt_av1_get_nz_map_contexts_c (C_DEFAULT/encode_txb_ref_c.c:35-44)
void getNzMapContexts(const std::uint8_t* levels, const std::int16_t* scan, std::uint16_t eob,
                      TxSize tx_size, TxClass tx_class, std::int8_t* coeff_contexts);
// get_eob_pos_token (entropy_coding.h:94-102)
int getEobPosToken(int eob, int* extra);
// write_golomb (entropy_coding.c:236-243)
void writeGolomb(AomWriter* w, int level);
// read_golomb (aom decodetxb.c read_golomb - the decoder-order twin)
int readGolomb(AomReader* r);

// entropy_coding.c:355-544 port (LUMA DCT_DCT): txb_skip :366 -> eob_pt/extra
// :378-411 -> last-coeff base_eob+br :479-500 -> reverse base/br :502-524 ->
// forward signs+golomb :527-539. coeff: raster-order quantized coefficients;
// scan: forward scan (svt_aom_init_iscan family). CS1b: component_type
// threads the six component-dim CDF families (dc_sign, eob_flag16..1024,
// eob_extra, coeff_base_eob, coeff_base, coeff_br - the [PLANE_TYPES] rows)
// and gates the tx-type symbol LUMA-only (entropy_coding.c:374-376: chroma
// TUs emit no tx-type symbol).
// RE7: tx_type = the block's DERIVED transform type
// (av1_write_coeffs_txb_1d reads blk_ptr->tx_type[txb_index],
// entropy_coding.c:362-363); base_q_idx threads the av1_write_tx_type gate
// (entropy_coding.c:321-322). The symbol value =
// av1_ext_tx_ind[tx_set_type][tx_type] through the landed writeTxType.
void writeTxbCoeffs(AomWriter* w, EcFrameContext* fc, const std::int32_t* coeff,
                    const std::int16_t* scan, TxSize tx_size, int eob, int txb_skip_ctx,
                    int dc_sign_ctx, int reduced_tx_set, PredictionMode intra_dir,
                    ComponentType component_type, TxType tx_type, int base_q_idx);
// read twin (aom decodetxb.c read_coeffs_txb, symbol-for-symbol). Fills
// coeff (raster, signed); returns eob. Same component_type threading.
// RE7: base_q_idx + tx_type_out mirror the writer's gate - the reader
// consumes the symbol exactly when the writer emitted it and outs the read
// type (DCT_DCT when the gate folds: q0 or the DCTONLY set; aom decodemv.c
// av1_read_tx_type:626-640).
int readTxbCoeffs(AomReader* r, EcFrameContext* fc, std::int32_t* coeff,
                  const std::int16_t* scan, TxSize tx_size, int txb_skip_ctx, int dc_sign_ctx,
                  int reduced_tx_set, PredictionMode intra_dir,
                  ComponentType component_type, int base_q_idx, TxType* tx_type_out);

// ---------------------------------------------------------------------------
// TS3: tx-type surface (entropy_coding.c:317-353, intra path only).
// ---------------------------------------------------------------------------
// get_ext_tx_set_type (common_utils.h:59-77; the txsize_sqr tables + TxSetType enum)
TxSetType getExtTxSetType(TxSize tx_size, int is_inter, int use_reduced_set);
// get_ext_tx_types (common_utils.h:79-82) -> av1_num_ext_tx_set[set_type]
int getExtTxTypes(TxSize tx_size, int is_inter, int use_reduced_set);
// get_ext_tx_set (common_utils.h:87-90) -> ext_tx_set_index[is_inter][set_type]
int getExtTxSet(TxSize tx_size, int is_inter, int use_reduced_set);

// av1_num_ext_tx_set (common_utils.c:195, verbatim)
extern const int32_t av1NumExtTxSet[EXT_TX_SET_TYPES];
// av1_ext_tx_used (common_utils.c:197-205, verbatim)
extern const int32_t av1ExtTxUsed[EXT_TX_SET_TYPES][16];
// ext_tx_set_index (common_utils.c:206-209, verbatim)
extern const int32_t extTxSetIndex[2][EXT_TX_SET_TYPES];
// av1_ext_tx_ind (cabac_context_model.c:34-41, verbatim)
extern const int32_t av1ExtTxInd[EXT_TX_SET_TYPES][16];

// av1_write_tx_type (entropy_coding.c:317-353, intra path only: is_inter=0).
// Gate: get_ext_tx_types(tx_size, 0, reduced_tx_set) > 1 AND base_q_idx > 0.
// Symbol value = av1_ext_tx_ind[tx_set_type][tx_type]; cdf =
// intra_ext_tx_cdf[eset][square_tx_size][intra_dir]; nsymbs =
// av1_num_ext_tx_set[tx_set_type].
void writeTxType(AomWriter* w, EcFrameContext* fc, int base_q_idx, int reduced_tx_set,
                 PredictionMode intra_dir, TxType tx_type, TxSize tx_size);
// Read twin (aom decodetxb.c: av1_read_tx_type - reads from intra_ext_tx_cdf
// at [eset][square][intra_dir] and inverts via av1_ext_tx_inv)
TxType readTxType(AomReader* r, EcFrameContext* fc, int base_q_idx, int reduced_tx_set,
                  PredictionMode intra_dir, TxSize tx_size);

// ---------------------------------------------------------------------------
// TS2: per-block coefficient loop. The dc-sign-level NA model: two flat
// arrays (above/left), one uint8_t per 4-pixel unit. Packed: bits 7-6 =
// dc_sign (0=none, 1=neg, 2=pos), bits 5-0 = cul_level (COEFF_CONTEXT_BITS).
// ---------------------------------------------------------------------------
struct DcSignLevelCoeffNa {
    std::uint8_t above[64];   // per mi column (max 64x64/4 = 16, padded)
    std::uint8_t left[64];    // per mi row
};

// svt_aom_get_txb_ctx (entropy_coding.c:248-315) flattened: the NA's
// above/left arrays passed explicitly. plane_bsize == tx_bsize for our
// whole-block TUs -> txb_skip_ctx = 0 (:298-299). The skip_contexts table
// branch (:301-308) and the chroma branch (:310-314) are ported for the
// range rule but dead for our whole-block luma TUs.
void getTxbCtx(const std::uint8_t* above_ptr, const std::uint8_t* left_ptr, int txb_w_unit,
               int txb_h_unit, int plane, BlockSize plane_bsize, TxSize tx_size,
               int* txb_skip_ctx, int* dc_sign_ctx);

// tx_blocks_per_depth (transforms.c:24-46, verbatim; [BLOCK_SIZES_ALL][MAX_VARTX_DEPTH+1])
extern const std::uint8_t txBlocksPerDepth[22][3];
// tx_depth_to_tx_size (common_utils.c:95-115, verbatim)
extern const TxSize txDepthToTxSize[3][22];
// txsize_to_bsize (inv_transforms.h:319-339, verbatim)
extern const BlockSize txsizeToBsize[TX_SIZES_ALL];
// eb_tx_size_wide_unit / eb_tx_size_high_unit (common_utils.c:65-72, verbatim)
extern const std::int32_t ebTxSizeWideUnit[TX_SIZES_ALL];
extern const std::int32_t ebTxSizeHighUnit[TX_SIZES_ALL];

// Per-block token loop (entropy_coding.c:757-820 tx_depth=0 path): for our
// whole-block TUs (tx_depth=0, txb_count=1), the loop is:
//   getTxbCtx -> writeTxbCoeffs -> NA update. The NA update packs
// cul_level (clamped to COEFF_CONTEXT_MASK) + set_dc_sign into a uint8_t
// and writes it to the above/left arrays over the TU's mi extent.
// CS1b: component_type routes getTxbCtx's plane arg (COMPONENT_CHROMA ->
// PLANE_TYPE_UV, the :310-314 branch: ctx_base + offset 7) and threads the
// six component-dim families through the chain. bsize is the PLANE block
// size (the luma bsize for COMPONENT_LUMA; the UV-plane bsize for
// COMPONENT_CHROMA).
// RE7: tx_type = the block's DERIVED transform type (the caller's
// lumaIntraTxType output; entropy_coding.c:374-376 reads
// blk_ptr->tx_type[txb_index]); base_q_idx threads the frame header's
// quantization_params.base_q_idx into the av1_write_tx_type gate
// (entropy_coding.c:321-322: get_ext_tx_types > 1 AND base_q_idx > 0).
void writeBlockCoeffs(AomWriter* w, EcFrameContext* fc, DcSignLevelCoeffNa* na,
                      const std::int32_t* coeff, const std::int16_t* scan, TxSize tx_size,
                      BlockSize bsize, int eob, int mi_row, int mi_col,
                      int reduced_tx_set, PredictionMode intra_dir,
                      ComponentType component_type, TxType tx_type, int base_q_idx);
int readBlockCoeffs(AomReader* r, EcFrameContext* fc, DcSignLevelCoeffNa* na,
                    std::int32_t* coeff, const std::int16_t* scan, TxSize tx_size,
                    BlockSize bsize, int mi_row, int mi_col,
                    int reduced_tx_set, PredictionMode intra_dir,
                    ComponentType component_type, int base_q_idx, TxType* tx_type_out);
int readBlockCoeffs(AomReader* r, EcFrameContext* fc, DcSignLevelCoeffNa* na,
                    std::int32_t* coeff, const std::int16_t* scan, TxSize tx_size,
                    BlockSize bsize, int mi_row, int mi_col,
                    int reduced_tx_set, PredictionMode intra_dir,
                    ComponentType component_type);

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

// ---------------------------------------------------------------------------
// CS1b: the chroma (uv_mode) symbol surface.
// ---------------------------------------------------------------------------
// encode_intra_chroma_mode_av1's mode symbol (entropy_coding.c:1080-1081):
// uv_mode_cdf[cflAllowed][luma_mode], alphabet UV_INTRA_MODES - !cflAllowed
// (13 disallowed / 14 allowed). The write_cfl_alphas branch (:1083-1085,
// chroma_mode == UV_CFL_PRED) is DEFERRED - named (UV_CFL_PRED is not a D2
// candidate, pipeline.cpp:1721; the alphas are dead in our emission).
// Read twin: aom decodemv.c:140-148 (read_intra_mode_uv).
void writeUvMode(AomWriter* w, EcFrameContext* fc, int cfl_allowed, PredictionMode luma_mode,
                 UvPredictionMode chroma_mode);
UvPredictionMode readUvMode(AomReader* r, EcFrameContext* fc, int cfl_allowed,
                            PredictionMode luma_mode);

// encode_intra_chroma_mode_av1's angle-delta symbol (entropy_coding.c:1087-
// 1092): symbol = angle_delta + MAX_ANGLE_DELTA through
// angle_delta_cdf[chroma_mode - V_PRED], gated by bsize >= BLOCK_8X8
// (av1_use_angle_delta, aom reconintra.h:59-61) AND
// isDirectionalMode(uv2y(chroma_mode)) - i.e. exactly the directional chroma
// modes UV_V_PRED..UV_D67_PRED (1..8; uv2y[uv] == uv there, so the writer's
// :1090 row equals the aom reader's folded row, decodemv.c:830-833 +
// read_angle_delta :603-606). The cdf row index is only evaluated inside the
// gate (range-safe: 0..7). Gated off: writeUvAngleDelta emits nothing,
// readUvAngleDelta consumes nothing and returns -1; else it returns the raw
// decoded symbol (delta + MAX_ANGLE_DELTA).
void writeUvAngleDelta(AomWriter* w, EcFrameContext* fc, BlockSize bsize,
                       UvPredictionMode chroma_mode, int angle_delta);
int readUvAngleDelta(AomReader* r, EcFrameContext* fc, BlockSize bsize,
                     UvPredictionMode chroma_mode);

// Filter-intra pair (entropy_coding.c:5050-5058): flag symbol
// (fi_mode != FILTER_INTRA_MODES) then, when set, the FI mode symbol.
void writeFilterIntra(AomWriter* w, EcFrameContext* fc, BlockSize bsize,
                      FilterIntraMode fi_mode);
// Return: the flag value (fi_mode != FILTER_INTRA_MODES).
int readFilterIntra(AomReader* r, EcFrameContext* fc, BlockSize bsize,
                    FilterIntraMode* fi_mode);

// SYM1: the color-map token chain.
// TOKENEXTRA (cabac_context_model.h:31-36, verbatim layout)
struct TokenExtra {
    AomCdfProb* color_map_cdf;
    std::uint8_t token;
};

// svt_av1_tokenize_color_map (palette.c:817-828) over
// cost_and_tokenize_map (:781-815), the Av1ColorMapParam wall threaded:
// the color map + dims + palette size + plane + the update flag in, the
// token arena out. Returns the token count (rows * cols; the reference
// advances the TOKENEXTRA* - named deviation). The first token carries
// colorMap[0] with a NULL cdf (written via write_uniform by the packer).
int tokenizeColorMap(EcFrameContext* fc, const std::uint8_t* colorMap, int stride, int rows, int cols,
                     int n, int plane, int allowUpdateCdf, TokenExtra* tokens);

// pack_map_tokens (entropy_coding.c:4339-4348): write_uniform for the
// first token, then the per-token cdf symbols. DEVIATION (named): the
// reference takes TOKENEXTRA** (it advances the shared arena pointer); this
// takes the base pointer - the arena ownership stays with the caller.
void packMapTokens(AomWriter* w, const TokenExtra* tokens, int n, int num);

// aom av1_get_palette_color_index_context (entropymode.c:893-951) -
// the DECODER-side full context (the aom arbiter precedent; the SVT tree
// carries no decoder). Fills colorOrder (the score-sorted color order) and,
// when colorIdx != NULL, the inverse-mapped current color. The aom lookup
// table (entropymode.c:887-889) is byte-identical to SVT's
// paletteColorIndexContextLookup, so the shared table is used.
int getColorIndexContext(const std::uint8_t* colorMap, int stride, int r, int c, int paletteSize,
                         std::uint8_t* colorOrder, int* colorIdx);

// aom decode_color_map_tokens (detokenize.c:25-58) - the reader twin
// of the tokenize/pack chain, with the Av1ColorMapParam wall threaded. The
// plane_width/height padding tail (last column / last row copy) included.
void decodeColorMapTokens(AomReader* r, std::uint8_t* colorMap, int planeWidth, int planeHeight,
                          int rows, int cols, int n, int plane, EcFrameContext* fc);

// SYM1: write_palette_mode_info (entropy_coding.c:4351-4377), the
// pcs/mbmi/blk_ptr walls threaded: bit_depth, the luma/uv modes, the
// is_chroma_reference fact (the l6 predicate), the y palette size/colors +
// the y cache, and the two neighbor (available, palette_size) facts for the
// mode ctx. The UV branch writes ONLY the uv-mode symbol (value 0): the
// reference asserts palette_size[1] == 0 there (:4371) - no uv palette is
// ever emitted.
void writePaletteModeInfo(AomWriter* w, EcFrameContext* fc, BlockSize bsize, int bit_depth,
                          PredictionMode intra_luma_mode, UvPredictionMode intra_chroma_mode,
                          int is_chroma_ref, int palette_size_y, const std::uint16_t* palette_colors_y,
                          const std::uint16_t* cache_y, int n_cache_y, int above_available,
                          int above_palette_size, int left_available, int left_palette_size);

// SYM1: aom read_palette_mode_info (decodemv.c:567-604) - the reader twin.
// Reads the y-mode bit (+ size + colors when set) and the uv-mode bit.
// DEVIATION (named): the uv modev=1 continuation (uv size symbol +
// read_palette_colors_uv, decodemv.c:511-560) is PARKED - the pinned SVT
// writer emits no uv palette, so no writer twin exists to roundtrip.
// Returns the decoded y palette size (0 = unused); colors receives the
// sorted palette when sizeY > 0.
int readPaletteModeInfo(AomReader* r, EcFrameContext* fc, BlockSize bsize, int bit_depth,
                        PredictionMode intra_luma_mode, UvPredictionMode intra_chroma_mode,
                        int is_chroma_ref, const std::uint16_t* cache_y, int n_cache_y,
                        int above_available, int above_palette_size, int left_available,
                        int left_palette_size, std::uint16_t* colors_y);

// SYM1: write_segment_id (entropy_coding.c:4863-4883), the
// pcs/mbmi walls threaded. skip_coeff mirrors the reference's bool: when
// set, NO symbol is written and the map/id take the prediction. Returns
// the resolved segment id (the reference stores it into mbmi - named
// deviation) and updates the map with it.
int writeSegmentId(AomWriter* w, EcFrameContext* fc, BlockSize bsize, std::uint8_t* segment_ids,
                   int mi_cols, int mi_rows, int mi_row, int mi_col, int up_available,
                   int left_available, int segment_id, int last_active_seg_id, int skip_coeff);

// SYM1: the aom read_segment_id reader twin (decodemv.c:280-303): the
// prediction is derived from the READER's own map copy (the caller keeps
// it in lockstep with the writer's updates), the coded id is read from
// spatial_pred_seg_cdf[cdf_num] (MAX_SEGMENTS symbols) and deinterleaved.
// No corrupt-frame check (the aom internal_error branch - named deviation;
// the port trusts its own roundtrips). Updates the reader's map with the
// decoded id (the aom set_segment_id tail).
int readSegmentId(AomReader* r, EcFrameContext* fc, BlockSize bsize, std::uint8_t* segment_ids,
                  int mi_cols, int mi_rows, int mi_row, int mi_col, int up_available,
                  int left_available, int last_active_seg_id);

// svt_od_ec_enc_done (bitstream_unit.c:309-343)
unsigned char* odEcEncDone(OdEcEnc* enc, std::uint32_t* nbytes);

}  // namespace entropy