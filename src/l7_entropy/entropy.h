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

// PlaneType (definitions.h:685)
enum PlaneType { PLANE_TYPE_Y, PLANE_TYPE_UV, PLANE_TYPES };
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
// scan: forward scan (svt_aom_init_iscan family).
void writeTxbCoeffs(AomWriter* w, EcFrameContext* fc, const std::int32_t* coeff,
                    const std::int16_t* scan, TxSize tx_size, int eob, int txb_skip_ctx,
                    int dc_sign_ctx);
// read twin (aom decodetxb.c read_coeffs_txb, symbol-for-symbol). Fills
// coeff (raster, signed); returns eob.
int readTxbCoeffs(AomReader* r, EcFrameContext* fc, std::int32_t* coeff,
                  const std::int16_t* scan, TxSize tx_size, int txb_skip_ctx, int dc_sign_ctx);

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
//   cul_level (clamped to COEFF_CONTEXT_MASK) + set_dc_sign into a uint8_t
//   and writes it to the above/left arrays over the TU's mi extent.
void writeBlockCoeffs(AomWriter* w, EcFrameContext* fc, DcSignLevelCoeffNa* na,
                      const std::int32_t* coeff, const std::int16_t* scan, TxSize tx_size,
                      BlockSize bsize, int eob, int mi_row, int mi_col);
int readBlockCoeffs(AomReader* r, EcFrameContext* fc, DcSignLevelCoeffNa* na,
                    std::int32_t* coeff, const std::int16_t* scan, TxSize tx_size,
                    BlockSize bsize, int mi_row, int mi_col);

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