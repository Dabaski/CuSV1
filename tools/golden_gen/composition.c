// tools/golden_gen/composition.c
// Composition layer for the golden generator. No arithmetic of its own
// except the two 4x4 2D cores, which are documented specializations of the
// SVT general cores (provenance inline). Everything else forwards to the
// verbatim extracts in svt_gen.c.
#include "svt_gen.c"
// TD4d: forward declarations for the drivers defined below the first use
// (C4013 prototype discipline - MSVC assumes int returns otherwise).
static int svtd_decide(const uint8_t* src, const uint8_t* above, int n_top, int n_topright,
                       const uint8_t* left, int n_left, int txb_skip_ctx,
                       const uint8_t* al, uint32_t* best_sad);
static int svtd_eob_from_coeffs(const TranLow* coeff, const int16_t* scan, TxSize tx_size);
// TD2 gate: the l7 twin wrappers (l7_ctx_shim.cpp, extern "C").
int l7_getPaddedIdx(int idx, int bwl);
int l7_getNzMag(const uint8_t* levels, int bwl, int tx_class);
int l7_getNzMapCtxFromStats(int stats, int coeff_idx, int bwl, int tx_size, int tx_class);
int l7_getLowerLevelsCtxEob(int bwl, int height, int scan_idx);
int l7_getLowerLevelsCtx(const uint8_t* levels, int coeff_idx, int bwl, int tx_size,
                         int tx_class);
int l7_getBrCtxEob(int c, int bwl, int tx_class);
int l7_getBrCtx(const uint8_t* levels, int c, int bwl, int tx_class);
void l7_txbInitLevels(const int32_t* coeff, int width, int height, uint8_t* levels);
// TD2c golomb + raw-sign surface (bytes/values exchange only - the l7 reader
// struct layouts differ from the vendored bitreader.h shape; named in TD2).
int l7_writeGolombToBuf(int level, unsigned char* out, unsigned cap, unsigned* outSize);
int l7_readGolombFromBytes(const unsigned char* buffer, unsigned size, int* out);
int l7_signtrace(const unsigned char* buffer, unsigned size, int dcsign0, int* out);

// ---- dispatch adapters -----------------------------------------------------
// SVT builds svt_aom_eb_pred / svt_aom_dc_pred with the intra_pred_sized
// macro (intra_prediction.c:1402) over every TxSize; the generator
// instantiates the TX_4X4, TX_8X8 and TX_16X16 columns. These adapters
// forward with the fixed bw=bh per column ??? pure plumbing, no arithmetic.
SvtdPredFn svtd_eb_pred[13][5];
SvtdPredFn svtd_dc_pred[2][2][5];
#define svt_aom_eb_pred svtd_eb_pred
#define svt_aom_dc_pred svtd_dc_pred
int32_t svtd_filt_type = 0;  // get_filt_type shim state (see svt_gen.c)

#define SVTD_ADAPTER(name, fn) \
    static void name(uint8_t* dst, ptrdiff_t stride, const uint8_t* above, const uint8_t* left) { \
        fn(dst, stride, 4, 4, above, left); \
    }

#define SVTD_ADAPTER8(name, fn) \
    static void name(uint8_t* dst, ptrdiff_t stride, const uint8_t* above, const uint8_t* left) { \
        fn(dst, stride, 8, 8, above, left); \
    }

#define SVTD_ADAPTER16(name, fn) \
    static void name(uint8_t* dst, ptrdiff_t stride, const uint8_t* above, const uint8_t* left) { \
        fn(dst, stride, 16, 16, above, left); \
    }

#define SVTD_ADAPTER32(name, fn) \
    static void name(uint8_t* dst, ptrdiff_t stride, const uint8_t* above, const uint8_t* left) { \
        fn(dst, stride, 32, 32, above, left); \
    }

// L7 (A4): plain static functions instead of a backslash-continued macro -
// the backslash-newline lines trip git diff --check ("trailing whitespace").
static void eb_dc_64x64(uint8_t* dst, ptrdiff_t stride, const uint8_t* above, const uint8_t* left) {
    dc_predictor(dst, stride, 64, 64, above, left);
}

static void eb_dc_left_64x64(uint8_t* dst, ptrdiff_t stride, const uint8_t* above, const uint8_t* left) {
    dc_left_predictor(dst, stride, 64, 64, above, left);
}

static void eb_dc_top_64x64(uint8_t* dst, ptrdiff_t stride, const uint8_t* above, const uint8_t* left) {
    dc_top_predictor(dst, stride, 64, 64, above, left);
}

static void eb_dc_128_64x64(uint8_t* dst, ptrdiff_t stride, const uint8_t* above, const uint8_t* left) {
    dc_128_predictor(dst, stride, 64, 64, above, left);
}

static void eb_v_64x64(uint8_t* dst, ptrdiff_t stride, const uint8_t* above, const uint8_t* left) {
    v_predictor(dst, stride, 64, 64, above, left);
}

static void eb_h_64x64(uint8_t* dst, ptrdiff_t stride, const uint8_t* above, const uint8_t* left) {
    h_predictor(dst, stride, 64, 64, above, left);
}

static void eb_smooth_64x64(uint8_t* dst, ptrdiff_t stride, const uint8_t* above, const uint8_t* left) {
    smooth_predictor(dst, stride, 64, 64, above, left);
}

static void eb_smooth_v_64x64(uint8_t* dst, ptrdiff_t stride, const uint8_t* above, const uint8_t* left) {
    smooth_v_predictor(dst, stride, 64, 64, above, left);
}

static void eb_smooth_h_64x64(uint8_t* dst, ptrdiff_t stride, const uint8_t* above, const uint8_t* left) {
    smooth_h_predictor(dst, stride, 64, 64, above, left);
}

static void eb_paeth_64x64(uint8_t* dst, ptrdiff_t stride, const uint8_t* above, const uint8_t* left) {
    paeth_predictor(dst, stride, 64, 64, above, left);
}

SVTD_ADAPTER(eb_dc_4x4, dc_predictor)
SVTD_ADAPTER(eb_dc_left_4x4, dc_left_predictor)
SVTD_ADAPTER(eb_dc_top_4x4, dc_top_predictor)
SVTD_ADAPTER(eb_dc_128_4x4, dc_128_predictor)
SVTD_ADAPTER(eb_v_4x4, v_predictor)
SVTD_ADAPTER(eb_h_4x4, h_predictor)
SVTD_ADAPTER(eb_smooth_4x4, smooth_predictor)
SVTD_ADAPTER(eb_smooth_v_4x4, smooth_v_predictor)
SVTD_ADAPTER(eb_smooth_h_4x4, smooth_h_predictor)
SVTD_ADAPTER(eb_paeth_4x4, paeth_predictor)

SVTD_ADAPTER8(eb_dc_8x8, dc_predictor)
SVTD_ADAPTER8(eb_dc_left_8x8, dc_left_predictor)
SVTD_ADAPTER8(eb_dc_top_8x8, dc_top_predictor)
SVTD_ADAPTER8(eb_dc_128_8x8, dc_128_predictor)
SVTD_ADAPTER8(eb_v_8x8, v_predictor)
SVTD_ADAPTER8(eb_h_8x8, h_predictor)
SVTD_ADAPTER8(eb_smooth_8x8, smooth_predictor)
SVTD_ADAPTER8(eb_smooth_v_8x8, smooth_v_predictor)
SVTD_ADAPTER8(eb_smooth_h_8x8, smooth_h_predictor)
SVTD_ADAPTER8(eb_paeth_8x8, paeth_predictor)

SVTD_ADAPTER16(eb_dc_16x16, dc_predictor)
SVTD_ADAPTER16(eb_dc_left_16x16, dc_left_predictor)
SVTD_ADAPTER16(eb_dc_top_16x16, dc_top_predictor)
SVTD_ADAPTER16(eb_dc_128_16x16, dc_128_predictor)
SVTD_ADAPTER16(eb_v_16x16, v_predictor)
SVTD_ADAPTER16(eb_h_16x16, h_predictor)
SVTD_ADAPTER16(eb_smooth_16x16, smooth_predictor)
SVTD_ADAPTER16(eb_smooth_v_16x16, smooth_v_predictor)
SVTD_ADAPTER16(eb_smooth_h_16x16, smooth_h_predictor)
SVTD_ADAPTER16(eb_paeth_16x16, paeth_predictor)

SVTD_ADAPTER32(eb_dc_32x32, dc_predictor)
SVTD_ADAPTER32(eb_dc_left_32x32, dc_left_predictor)
SVTD_ADAPTER32(eb_dc_top_32x32, dc_top_predictor)
SVTD_ADAPTER32(eb_dc_128_32x32, dc_128_predictor)
SVTD_ADAPTER32(eb_v_32x32, v_predictor)
SVTD_ADAPTER32(eb_h_32x32, h_predictor)
SVTD_ADAPTER32(eb_smooth_32x32, smooth_predictor)
SVTD_ADAPTER32(eb_smooth_v_32x32, smooth_v_predictor)
SVTD_ADAPTER32(eb_smooth_h_32x32, smooth_h_predictor)
SVTD_ADAPTER32(eb_paeth_32x32, paeth_predictor)

static void svtd_populate_dispatch(void) {
    // TX_4X4 column (index 0)
    svtd_eb_pred[DC_PRED][0]      = eb_dc_4x4;
    svtd_eb_pred[V_PRED][0]       = eb_v_4x4;
    svtd_eb_pred[H_PRED][0]       = eb_h_4x4;
    svtd_eb_pred[D45_PRED][0]     = eb_v_4x4;
    svtd_eb_pred[D135_PRED][0]    = eb_v_4x4;
    svtd_eb_pred[D113_PRED][0]    = eb_v_4x4;
    svtd_eb_pred[D157_PRED][0]    = eb_v_4x4;
    svtd_eb_pred[D203_PRED][0]    = eb_v_4x4;
    svtd_eb_pred[D67_PRED][0]     = eb_v_4x4;
    svtd_eb_pred[SMOOTH_PRED][0]  = eb_smooth_4x4;
    svtd_eb_pred[SMOOTH_V_PRED][0] = eb_smooth_v_4x4;
    svtd_eb_pred[SMOOTH_H_PRED][0] = eb_smooth_h_4x4;
    svtd_eb_pred[PAETH_PRED][0]   = eb_paeth_4x4;

    svtd_dc_pred[1][1][0] = eb_dc_4x4;
    svtd_dc_pred[1][0][0] = eb_dc_left_4x4;
    svtd_dc_pred[0][1][0] = eb_dc_top_4x4;
    svtd_dc_pred[0][0][0] = eb_dc_128_4x4;

    // TX_8X8 column (index 1)
    svtd_eb_pred[DC_PRED][1]      = eb_dc_8x8;
    svtd_eb_pred[V_PRED][1]       = eb_v_8x8;
    svtd_eb_pred[H_PRED][1]       = eb_h_8x8;
    svtd_eb_pred[D45_PRED][1]     = eb_v_8x8;
    svtd_eb_pred[D135_PRED][1]    = eb_v_8x8;
    svtd_eb_pred[D113_PRED][1]    = eb_v_8x8;
    svtd_eb_pred[D157_PRED][1]    = eb_v_8x8;
    svtd_eb_pred[D203_PRED][1]    = eb_v_8x8;
    svtd_eb_pred[D67_PRED][1]     = eb_v_8x8;
    svtd_eb_pred[SMOOTH_PRED][1]  = eb_smooth_8x8;
    svtd_eb_pred[SMOOTH_V_PRED][1] = eb_smooth_v_8x8;
    svtd_eb_pred[SMOOTH_H_PRED][1] = eb_smooth_h_8x8;
    svtd_eb_pred[PAETH_PRED][1]   = eb_paeth_8x8;

    svtd_dc_pred[1][1][1] = eb_dc_8x8;
    svtd_dc_pred[1][0][1] = eb_dc_left_8x8;
    svtd_dc_pred[0][1][1] = eb_dc_top_8x8;
    svtd_dc_pred[0][0][1] = eb_dc_128_8x8;

    // TX_16X16 column (index 2)
    svtd_eb_pred[DC_PRED][2]      = eb_dc_16x16;
    svtd_eb_pred[V_PRED][2]       = eb_v_16x16;
    svtd_eb_pred[H_PRED][2]       = eb_h_16x16;
    svtd_eb_pred[D45_PRED][2]     = eb_v_16x16;
    svtd_eb_pred[D135_PRED][2]    = eb_v_16x16;
    svtd_eb_pred[D113_PRED][2]    = eb_v_16x16;
    svtd_eb_pred[D157_PRED][2]    = eb_v_16x16;
    svtd_eb_pred[D203_PRED][2]    = eb_v_16x16;
    svtd_eb_pred[D67_PRED][2]     = eb_v_16x16;
    svtd_eb_pred[SMOOTH_PRED][2]  = eb_smooth_16x16;
    svtd_eb_pred[SMOOTH_V_PRED][2] = eb_smooth_v_16x16;
    svtd_eb_pred[SMOOTH_H_PRED][2] = eb_smooth_h_16x16;
    svtd_eb_pred[PAETH_PRED][2]   = eb_paeth_16x16;

    svtd_dc_pred[1][1][2] = eb_dc_16x16;
    svtd_dc_pred[1][0][2] = eb_dc_left_16x16;
    svtd_dc_pred[0][1][2] = eb_dc_top_16x16;
    svtd_dc_pred[0][0][2] = eb_dc_128_16x16;

    // TX_32X32 column (index 3, L6)
    svtd_eb_pred[DC_PRED][3]      = eb_dc_32x32;
    svtd_eb_pred[V_PRED][3]       = eb_v_32x32;
    svtd_eb_pred[H_PRED][3]       = eb_h_32x32;
    svtd_eb_pred[D45_PRED][3]     = eb_v_32x32;
    svtd_eb_pred[D135_PRED][3]    = eb_v_32x32;
    svtd_eb_pred[D113_PRED][3]    = eb_v_32x32;
    svtd_eb_pred[D157_PRED][3]    = eb_v_32x32;
    svtd_eb_pred[D203_PRED][3]    = eb_v_32x32;
    svtd_eb_pred[D67_PRED][3]     = eb_v_32x32;
    svtd_eb_pred[SMOOTH_PRED][3]  = eb_smooth_32x32;
    svtd_eb_pred[SMOOTH_V_PRED][3] = eb_smooth_v_32x32;
    svtd_eb_pred[SMOOTH_H_PRED][3] = eb_smooth_h_32x32;
    svtd_eb_pred[PAETH_PRED][3]   = eb_paeth_32x32;

    svtd_dc_pred[1][1][3] = eb_dc_32x32;
    svtd_dc_pred[1][0][3] = eb_dc_left_32x32;
    svtd_dc_pred[0][1][3] = eb_dc_top_32x32;
    svtd_dc_pred[0][0][3] = eb_dc_128_32x32;

    // TX_64X64 column (index 4, L7)
    svtd_eb_pred[DC_PRED][4]      = eb_dc_64x64;
    svtd_eb_pred[V_PRED][4]       = eb_v_64x64;
    svtd_eb_pred[H_PRED][4]       = eb_h_64x64;
    svtd_eb_pred[D45_PRED][4]     = eb_v_64x64;
    svtd_eb_pred[D135_PRED][4]    = eb_v_64x64;
    svtd_eb_pred[D113_PRED][4]    = eb_v_64x64;
    svtd_eb_pred[D157_PRED][4]    = eb_v_64x64;
    svtd_eb_pred[D203_PRED][4]    = eb_v_64x64;
    svtd_eb_pred[D67_PRED][4]     = eb_v_64x64;
    svtd_eb_pred[SMOOTH_PRED][4]  = eb_smooth_64x64;
    svtd_eb_pred[SMOOTH_V_PRED][4] = eb_smooth_v_64x64;
    svtd_eb_pred[SMOOTH_H_PRED][4] = eb_smooth_h_64x64;
    svtd_eb_pred[PAETH_PRED][4]   = eb_paeth_64x64;

    svtd_dc_pred[1][1][4] = eb_dc_64x64;
    svtd_dc_pred[1][0][4] = eb_dc_left_64x64;
    svtd_dc_pred[0][1][4] = eb_dc_top_64x64;
    svtd_dc_pred[0][0][4] = eb_dc_128_64x64;
}

// ---- forward 2D core -------------------------------------------------------
// Specialization of av1_tranform_two_d_core_c (transforms.c:2398) to TX_4X4,
// 8-bit, no flips: fwd_shift_4x4 = {2, 0, 0} (transforms.c:122), cos_bit 13/13
// (fwd_cos_bit_col/row[0][0]). Column pass left-shifts by shift[0]=2
// (round_shift_array with -shift), row pass has no final shift.
static void svtd_fwd2d4x4(const int16_t* input, uint32_t input_stride, int32_t* output, TxfmFunc txfm) {
    const int8_t* shift       = fwd_shift_4x4;
    const int8_t  cos_bit_col = fwd_cos_bit_col[0][0];
    const int8_t  cos_bit_row = fwd_cos_bit_row[0][0];
    int32_t       buf[4 * 4];
    int32_t       temp_in[4];
    int32_t       temp_out[4];
    int32_t       r, c;

    for (c = 0; c < 4; ++c) {
        for (r = 0; r < 4; ++r) {
            temp_in[r] = input[r * input_stride + c];
        }
        svt_av1_round_shift_array_c(temp_in, 4, -shift[0]);
        txfm(temp_in, temp_out, cos_bit_col, NULL);
        svt_av1_round_shift_array_c(temp_out, 4, -shift[1]);
        for (r = 0; r < 4; ++r) {
            buf[r * 4 + c] = temp_out[r];
        }
    }
    for (r = 0; r < 4; ++r) {
        txfm(buf + r * 4, output + r * 4, cos_bit_row, NULL);
        svt_av1_round_shift_array_c(output + r * 4, 4, -shift[2]);
    }
}

// ---- inverse 2D add core ---------------------------------------------------
// Specialization of inv_txfm2d_add_c (inv_transforms.c:2496) to TX_4X4,
// 8-bit, no flips: inv_shift_4x4 = {0, -4} (inv_transforms.c:18), cos_bit 12
// (INV_COS_BIT, inv_transforms.h:24), clamp bits bd+8=16 and max(bd+6,16)=16
// (svt_av1_gen_inv_stage_range opt_range, bd=8), add via
// clip_pixel_highbd(output_r + round_shift(temp_out, -shift[1]), 8).
static void svtd_inv2dadd4x4(const int32_t* input, uint8_t* pred, int32_t stride, TxfmFunc txfm) {
    const int8_t* shift       = inv_shift_4x4;
    const int8_t  cos_bit_col = inv_cos_bit_col[0][0];
    const int8_t  cos_bit_row = inv_cos_bit_row[0][0];
    int32_t       buf[4 * 4];
    int32_t       temp_in[4];
    int32_t       temp_out[4];
    const int8_t  stage_range[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    int32_t       r, c;

    for (r = 0; r < 4; ++r) {
        for (c = 0; c < 4; ++c) {
            temp_in[c] = input[r * 4 + c];
        }
        clamp_buf(temp_in, 4, (int8_t)(8 + 8));
        txfm(temp_in, buf + r * 4, cos_bit_row, stage_range);
        svt_av1_round_shift_array_c(buf + r * 4, 4, -shift[0]);
    }
    for (c = 0; c < 4; ++c) {
        for (r = 0; r < 4; ++r) {
            temp_in[r] = buf[r * 4 + c];
        }
        clamp_buf(temp_in, 4, (int8_t)(8 + 6 > 16 ? 8 + 6 : 16));
        txfm(temp_in, temp_out, cos_bit_col, stage_range);
        svt_av1_round_shift_array_c(temp_out, 4, -shift[1]);
        for (r = 0; r < 4; ++r) {
            pred[r * stride + c] =
                (uint8_t)clip_pixel_highbd(pred[r * stride + c] + temp_out[r], 8);
        }
    }
}

// ---- builder call helpers --------------------------------------------------
// Each wraps the VERBATIM build_intra_predictors with the neighbor arrays it
// expects: above_ref/left_ref with the corner at index -1 (the builder reads
// above_row[-1] from above_ref[-1]).
static void svtd_call_builder_tx(uint8_t* dst, int mode, int angle_delta, int filter_intra_mode,
                                 int disable_edge_filter, const uint8_t* above, int n_top, int n_topright,
                                 const uint8_t* left, int n_left, int n_bottomleft, uint8_t above_left,
                                 TxSize tx_size) {
    uint8_t above_data[2 * 64 + 48];
    uint8_t left_data[2 * 64 + 48];
    memset(above_data, 0x80, sizeof(above_data));
    memset(left_data, 0x80, sizeof(left_data));
    uint8_t* above_row = above_data + 32;
    uint8_t* left_col  = left_data + 32;
    int      i;
    for (i = 0; i < n_top + n_topright; ++i) above_row[i] = above[i];
    for (i = 0; i < n_left + n_bottomleft; ++i) left_col[i] = left[i];
    above_row[-1] = above_left;
    left_col[-1]  = above_left;
    build_intra_predictors(NULL, above_row, left_col, dst, tx_size_wide[tx_size], (PredictionMode)mode,
                           angle_delta, (FilterIntraMode)filter_intra_mode, tx_size,
                           disable_edge_filter, n_top, n_topright, n_left, n_bottomleft, 0);
}

static void svtd_call_builder(uint8_t* dst, int mode, int angle_delta, int filter_intra_mode,
                              int disable_edge_filter, const uint8_t* above, int n_top, int n_topright,
                              const uint8_t* left, int n_left, int n_bottomleft, uint8_t above_left) {
    svtd_call_builder_tx(dst, mode, angle_delta, filter_intra_mode, disable_edge_filter, above, n_top,
                         n_topright, left, n_left, n_bottomleft, above_left, TX_4X4);
}

// V-pred E1 helper: above only (n_top=4), corner 0
static void svtd_builder_v(uint8_t* dst, const uint8_t* above, int n_top) {
    uint8_t left[1] = {0};
    svtd_call_builder(dst, V_PRED, 0, FILTER_INTRA_MODES, 0, above, n_top, 0, left, 0, 0, 0);
}

// DC-pred E1 helper: both edges
static void svtd_builder_dc(uint8_t* dst, const uint8_t* above, const uint8_t* left, int n_top, int n_left) {
    svtd_call_builder(dst, DC_PRED, 0, FILTER_INTRA_MODES, 0, above, n_top, 0, left, n_left, 0, 0);
}

// D67 E5 helper: above + topright, left edge present (n=4, matching the
// committed E5 fixture), corner, edge filter/upsample controlled
static void svtd_builder_d67(uint8_t* dst, const uint8_t* above, int n_top, int n_topright,
                             uint8_t above_left, int disable_edge_filter) {
    const uint8_t left[4] = {9, 9, 9, 9};
    svtd_call_builder(dst, D67_PRED, 0, FILTER_INTRA_MODES, disable_edge_filter, above, n_top,
                      n_topright, left, 4, 0, above_left);
}

// ---- forward 2D core (8x8) -------------------------------------------------
// Specialization of av1_tranform_two_d_core_c (transforms.c:2398) to TX_8X8,
// 8-bit, no flips: fwd_shift_8x8 = {2, -1, 0} (transforms.c:123), cos_bit
// 13/13 (fwd_cos_bit_col/row[1][1]). Col pass left-shifts by shift[0]=2,
// after col transform right-shift by -shift[1]=1 (rounding), row pass no
// final shift.
static void svtd_fwd2d8x8(const int16_t* input, uint32_t input_stride, int32_t* output, TxfmFunc txfm) {
    const int8_t* shift       = fwd_shift_8x8;
    const int8_t  cos_bit_col = fwd_cos_bit_col[1][1];
    const int8_t  cos_bit_row = fwd_cos_bit_row[1][1];
    int32_t       buf[8 * 8];
    int32_t       temp_in[8];
    int32_t       temp_out[8];
    int32_t       r, c;

    for (c = 0; c < 8; ++c) {
        for (r = 0; r < 8; ++r) {
            temp_in[r] = input[r * input_stride + c];
        }
        svt_av1_round_shift_array_c(temp_in, 8, -shift[0]);
        txfm(temp_in, temp_out, cos_bit_col, NULL);
        svt_av1_round_shift_array_c(temp_out, 8, -shift[1]);
        for (r = 0; r < 8; ++r) {
            buf[r * 8 + c] = temp_out[r];
        }
    }
    for (r = 0; r < 8; ++r) {
        txfm(buf + r * 8, output + r * 8, cos_bit_row, NULL);
        svt_av1_round_shift_array_c(output + r * 8, 8, -shift[2]);
    }
}

// ---- inverse 2D add core (8x8) ---------------------------------------------
// Specialization of inv_txfm2d_add_c (inv_transforms.c:2496) to TX_8X8,
// 8-bit, no flips: inv_shift_8x8 = {-1, -4} (inv_transforms.c:19), cos_bit
// 12 (INV_COS_BIT), clamp bits bd+8=16 and max(bd+6,16)=16.
static void svtd_inv2dadd8x8(const int32_t* input, uint8_t* pred, int32_t stride, TxfmFunc txfm) {
    const int8_t* shift       = inv_shift_8x8;
    const int8_t  cos_bit_col = inv_cos_bit_col[1][1];
    const int8_t  cos_bit_row = inv_cos_bit_row[1][1];
    int32_t       buf[8 * 8];
    int32_t       temp_in[8];
    int32_t       temp_out[8];
    const int8_t  stage_range[8] = {16, 16, 16, 16, 16, 16, 16, 16};
    int32_t       r, c;

    for (r = 0; r < 8; ++r) {
        for (c = 0; c < 8; ++c) {
            temp_in[c] = input[r * 8 + c];
        }
        clamp_buf(temp_in, 8, (int8_t)(8 + 8));
        txfm(temp_in, buf + r * 8, cos_bit_row, stage_range);
        svt_av1_round_shift_array_c(buf + r * 8, 8, -shift[0]);
    }
    for (c = 0; c < 8; ++c) {
        for (r = 0; r < 8; ++r) {
            temp_in[r] = buf[r * 8 + c];
        }
        clamp_buf(temp_in, 8, (int8_t)(8 + 6 > 16 ? 8 + 6 : 16));
        txfm(temp_in, temp_out, cos_bit_col, stage_range);
        svt_av1_round_shift_array_c(temp_out, 8, -shift[1]);
        for (r = 0; r < 8; ++r) {
            pred[r * stride + c] =
                (uint8_t)clip_pixel_highbd(pred[r * stride + c] + temp_out[r], 8);
        }
    }
}

// ---- forward 2D core (16x16) -----------------------------------------------
// Specialization of av1_tranform_two_d_core_c (transforms.c:2398) to TX_16X16,
// 8-bit, no flips: fwd_shift_16x16 = {2, -2, 0} (transforms.c:124), cos_bit
// col 13 = fwd_cos_bit_col[2][2], row 12 = fwd_cos_bit_row[2][2]
// (transforms.c:19-22). Col pass left-shifts by shift[0]=2, after col
// transform right-shifts by -shift[1]=2 (rounding), row pass no final shift.
static void svtd_fwd2d16x16(const int16_t* input, uint32_t input_stride, int32_t* output, TxfmFunc txfm) {
    const int8_t* shift       = fwd_shift_16x16;
    const int8_t  cos_bit_col = 13;  // fwd_cos_bit_col[2][2]
    const int8_t  cos_bit_row = 12;  // fwd_cos_bit_row[2][2]
    int32_t       buf[16 * 16];
    int32_t       temp_in[16];
    int32_t       temp_out[16];
    int32_t       r, c;

    for (c = 0; c < 16; ++c) {
        for (r = 0; r < 16; ++r) {
            temp_in[r] = input[r * input_stride + c];
        }
        svt_av1_round_shift_array_c(temp_in, 16, -shift[0]);
        txfm(temp_in, temp_out, cos_bit_col, NULL);
        svt_av1_round_shift_array_c(temp_out, 16, -shift[1]);
        for (r = 0; r < 16; ++r) {
            buf[r * 16 + c] = temp_out[r];
        }
    }
    for (r = 0; r < 16; ++r) {
        txfm(buf + r * 16, output + r * 16, cos_bit_row, NULL);
        svt_av1_round_shift_array_c(output + r * 16, 16, -shift[2]);
    }
}

// ---- inverse 2D add core (16x16) -------------------------------------------
// Specialization of inv_txfm2d_add_c (inv_transforms.c:2496) to TX_16X16,
// 8-bit, no flips: inv_shift_16x16 = {-2, -4} (inv_transforms.c:20), cos_bit
// 12/12 = inv_cos_bit_col/row[2][2] (INV_COS_BIT, inv_transforms.h:24,
// :32-41), clamp bits bd+8=16 and max(bd+6,16)=16 (svt_av1_gen_inv_stage_range
// opt_range, bd=8). Row pass right-shifts -shift[0]=2 after the row 1D; col
// pass right-shifts -shift[1]=4 before the clip add. stage_range = opt_range
// 16 for every stage (DCT16 8 stages, ADST16 10; both read only indices 3-7).
static void svtd_inv2dadd16x16(const int32_t* input, uint8_t* pred, int32_t stride, TxfmFunc txfm) {
    const int8_t* shift       = inv_shift_16x16;
    const int8_t  cos_bit_col = inv_cos_bit_col[2][2];
    const int8_t  cos_bit_row = inv_cos_bit_row[2][2];
    int32_t       buf[16 * 16];
    int32_t       temp_in[16];
    int32_t       temp_out[16];
    const int8_t  stage_range[MAX_TXFM_STAGE_NUM] = {16, 16, 16, 16, 16, 16, 16, 16,
                                                     16, 16, 16, 16, 16, 16, 16, 16,
                                                     16, 16, 16, 16, 16, 16, 16, 16,
                                                     16, 16, 16, 16, 16, 16, 16, 16,
                                                     16};
    int32_t       r, c;

    for (r = 0; r < 16; ++r) {
        for (c = 0; c < 16; ++c) {
            temp_in[c] = input[r * 16 + c];
        }
        clamp_buf(temp_in, 16, (int8_t)(8 + 8));
        txfm(temp_in, buf + r * 16, cos_bit_row, stage_range);
        svt_av1_round_shift_array_c(buf + r * 16, 16, -shift[0]);
    }
    for (c = 0; c < 16; ++c) {
        for (r = 0; r < 16; ++r) {
            temp_in[r] = buf[r * 16 + c];
        }
        clamp_buf(temp_in, 16, (int8_t)(8 + 6 > 16 ? 8 + 6 : 16));
        txfm(temp_in, temp_out, cos_bit_col, stage_range);
        svt_av1_round_shift_array_c(temp_out, 16, -shift[1]);
        for (r = 0; r < 16; ++r) {
            pred[r * stride + c] =
                (uint8_t)clip_pixel_highbd(pred[r * stride + c] + temp_out[r], 8);
        }
    }
}

// ---- forward 2D core (32x32, L0) -------------------------------------------
// Specialization of av1_tranform_two_d_core_c (transforms.c:2398) to
// TX_32X32, 8-bit, no flips: fwd_shift_32x32 = {2, -4, 0} (transforms.c:125),
// cos_bit col 12 = fwd_cos_bit_col[3][3], row 12 = fwd_cos_bit_row[3][3]
// (transforms.c:19-22). Col pass left-shifts by shift[0]=2, after col
// transform right-shifts by -shift[1]=4 (rounding), row pass no final shift.
static void svtd_fwd2d32x32(const int16_t* input, uint32_t input_stride, int32_t* output, TxfmFunc txfm) {
    const int8_t* shift       = fwd_shift_32x32;
    const int8_t  cos_bit_col = 12;  // fwd_cos_bit_col[3][3]
    const int8_t  cos_bit_row = 12;  // fwd_cos_bit_row[3][3]
    int32_t       buf[32 * 32];
    int32_t       temp_in[32];
    int32_t       temp_out[32];
    int32_t       r, c;

    for (c = 0; c < 32; ++c) {
        for (r = 0; r < 32; ++r) {
            temp_in[r] = input[r * input_stride + c];
        }
        svt_av1_round_shift_array_c(temp_in, 32, -shift[0]);
        txfm(temp_in, temp_out, cos_bit_col, NULL);
        svt_av1_round_shift_array_c(temp_out, 32, -shift[1]);
        for (r = 0; r < 32; ++r) {
            buf[r * 32 + c] = temp_out[r];
        }
    }
    for (r = 0; r < 32; ++r) {
        txfm(buf + r * 32, output + r * 32, cos_bit_row, NULL);
        svt_av1_round_shift_array_c(output + r * 32, 32, -shift[2]);
    }
}

// ---- inverse 2D add core (32x32, L0) ---------------------------------------
// Specialization of inv_txfm2d_add_c (inv_transforms.c:2496) to TX_32X32,
// 8-bit, no flips: inv_shift_32x32 = {-2, -4} (inv_transforms.c:21), cos_bit
// 12/12 = inv_cos_bit_col/row[3][3] (INV_COS_BIT), clamps bd+8=16 /
// max(bd+6,16)=16. Row pass >>2, col pass >>4.
static void svtd_inv2dadd32x32(const int32_t* input, uint8_t* pred, int32_t stride, TxfmFunc txfm) {
    const int8_t* shift       = inv_shift_32x32;
    const int8_t  cos_bit_col = inv_cos_bit_col[3][3];
    const int8_t  cos_bit_row = inv_cos_bit_row[3][3];
    int32_t       buf[32 * 32];
    int32_t       temp_in[32];
    int32_t       temp_out[32];
    const int8_t  stage_range[MAX_TXFM_STAGE_NUM] = {16, 16, 16, 16, 16, 16, 16, 16,
                                                     16, 16, 16, 16, 16, 16, 16, 16,
                                                     16, 16, 16, 16, 16, 16, 16, 16,
                                                     16, 16, 16, 16, 16, 16, 16, 16,
                                                     16};
    int32_t       r, c;

    for (r = 0; r < 32; ++r) {
        for (c = 0; c < 32; ++c) {
            temp_in[c] = input[r * 32 + c];
        }
        clamp_buf(temp_in, 32, (int8_t)(8 + 8));
        txfm(temp_in, buf + r * 32, cos_bit_row, stage_range);
        svt_av1_round_shift_array_c(buf + r * 32, 32, -shift[0]);
    }
    for (c = 0; c < 32; ++c) {
        for (r = 0; r < 32; ++r) {
            temp_in[r] = buf[r * 32 + c];
        }
        clamp_buf(temp_in, 32, (int8_t)(8 + 6 > 16 ? 8 + 6 : 16));
        txfm(temp_in, temp_out, cos_bit_col, stage_range);
        svt_av1_round_shift_array_c(temp_out, 32, -shift[1]);
        for (r = 0; r < 32; ++r) {
            pred[r * stride + c] =
                (uint8_t)clip_pixel_highbd(pred[r * stride + c] + temp_out[r], 8);
        }
    }
}

// ---- L7: 64x64 2D cores (DCT-ONLY: av1_txfm_type_ls[4] = {DCT64, INVALID,
// INVALID, IDENTITY64} (inv_transforms.h:196) - no ADST exists at 64x64 in
// this tree) ---------------------------------------------------------------
// Specialization of av1_tranform_two_d_core_c (transforms.c:2398) to
// TX_64X64, 8-bit, no flips: fwd_shift_64x64 = {0, -2, -2}
// (transforms.c:126), cos_bit col 13 = fwd_cos_bit_col[4][4], row 10 =
// fwd_cos_bit_row[4][4] (transforms.c:19-22; the ONLY size where a pass
// drops below 12). Col pass NO pre-shift (shift[0] = 0), after col
// transform right-shifts by -shift[1] = 2 (rounding), row pass right-shifts
// by -shift[2] = 2 (rounding).
static void svtd_fwd2d64x64(const int16_t* input, uint32_t input_stride, int32_t* output, TxfmFunc txfm) {
    const int8_t* shift       = fwd_shift_64x64;
    const int8_t  cos_bit_col = 13;  // fwd_cos_bit_col[4][4]
    const int8_t  cos_bit_row = 10;  // fwd_cos_bit_row[4][4]
    int32_t       buf[64 * 64];
    int32_t       temp_in[64];
    int32_t       temp_out[64];
    int32_t       r, c;

    for (c = 0; c < 64; ++c) {
        for (r = 0; r < 64; ++r) {
            temp_in[r] = input[r * input_stride + c];
        }
        svt_av1_round_shift_array_c(temp_in, 64, -shift[0]);
        txfm(temp_in, temp_out, cos_bit_col, NULL);
        svt_av1_round_shift_array_c(temp_out, 64, -shift[1]);
        for (r = 0; r < 64; ++r) {
            buf[r * 64 + c] = temp_out[r];
        }
    }
    for (r = 0; r < 64; ++r) {
        txfm(buf + r * 64, output + r * 64, cos_bit_row, NULL);
        svt_av1_round_shift_array_c(output + r * 64, 64, -shift[2]);
    }
}

// Inverse 2D add at TX_64X64: inv_shift_64x64 = {-2, -4}
// (inv_transforms.c:22), cos_bit 12/12 = inv_cos_bit_col/row[4][4]
// (INV_COS_BIT), clamps bd+8=16 / max(bd+6,16)=16. Row >>2, col >>4.
static void svtd_inv2dadd64x64(const int32_t* input, uint8_t* pred, int32_t stride, TxfmFunc txfm) {
    const int8_t* shift       = inv_shift_64x64;
    const int8_t  cos_bit_col = inv_cos_bit_col[4][4];
    const int8_t  cos_bit_row = inv_cos_bit_row[4][4];
    int32_t       buf[64 * 64];
    int32_t       temp_in[64];
    int32_t       temp_out[64];
    const int8_t  stage_range[MAX_TXFM_STAGE_NUM] = {16, 16, 16, 16, 16, 16, 16, 16,
                                                     16, 16, 16, 16, 16, 16, 16, 16,
                                                     16, 16, 16, 16, 16, 16, 16, 16,
                                                     16, 16, 16, 16, 16, 16, 16, 16,
                                                     16};
    int32_t       r, c;

    for (r = 0; r < 64; ++r) {
        for (c = 0; c < 64; ++c) {
            temp_in[c] = input[r * 64 + c];
        }
        clamp_buf(temp_in, 64, (int8_t)(8 + 8));
        txfm(temp_in, buf + r * 64, cos_bit_row, stage_range);
        svt_av1_round_shift_array_c(buf + r * 64, 64, -shift[0]);
    }
    for (c = 0; c < 64; ++c) {
        for (r = 0; r < 64; ++r) {
            temp_in[r] = buf[r * 64 + c];
        }
        clamp_buf(temp_in, 64, (int8_t)(8 + 6 > 16 ? 8 + 6 : 16));
        txfm(temp_in, temp_out, cos_bit_col, stage_range);
        svt_av1_round_shift_array_c(temp_out, 64, -shift[1]);
        for (r = 0; r < 64; ++r) {
            pred[r * stride + c] =
                (uint8_t)clip_pixel_highbd(pred[r * stride + c] + temp_out[r], 8);
        }
    }
}

// ---- quantizer composition (Q0) --------------------------------------------
// Default (up-right diagonal) scan for 4x4, generated by the svt_aom_init_iscan
// formula (coefficients.c:345-363) specialized to W=H=4: square, odd diagonal
// r-increasing, even r-decreasing.
static void svtd_default_scan_4x4(int16_t* scan) {
    const int W = 4, H = 4;  // tx_size_wide/high[TX_4X4]
    int idx = 0;
    for (int d = 0; d < W + H - 1; ++d) {
        const int rlo  = (d - (W - 1)) > 0 ? (d - (W - 1)) : 0;
        const int rhi  = d < (H - 1) ? d : (H - 1);
        int       incr = (H > W) ? 1 : (W > H) ? 0 : (d & 1);
        if (incr) {
            for (int r = rlo; r <= rhi; ++r) {
                scan[idx++] = (int16_t)(r * W + (d - r));
            }
        } else {
            for (int r = rhi; r >= rlo; --r) {
                scan[idx++] = (int16_t)(r * W + (d - r));
            }
        }
    }
}

// Luma quantizer tables at sharpness == 0 (the default config makes the
// sharpness branch at md_config_process.c:117-124 inert), mirroring the luma
// rows of svt_av1_build_quantizer (md_config_process.c:106-135) verbatim.
typedef struct SvtdQuantTables {
    int16_t quant[2];        // y_quant[q][0..1]      (:130 via svt_aom_invert_quant)
    int16_t quant_shift[2];  // y_quant_shift[q][0..1] (:130 via svt_aom_invert_quant)
    int16_t quant_fp[2];     // y_quant_fp[q][0..1]    (:131)
    int16_t round_fp[2];     // y_round_fp[q][0..1]    (:127, :132)
    int16_t zbin[2];         // y_zbin[q][0..1]        (:133)
    int16_t round[2];        // y_round[q][0..1]       (:108, :134)
    int16_t dequant[2];      // y_dequant_qtx[q][0..1] (:135)
} SvtdQuantTables;

static void svtd_build_quantizer_luma(int q, SvtdQuantTables* t) {
    const int32_t qzbin_factor     = svt_aom_get_qzbin_factor(q, EB_EIGHT_BIT);  // :107
    const int32_t qrounding_factor = q == 0 ? 64 : 48;                           // :108
    for (int i = 0; i < 2; ++i) {
        const int32_t quant_qtx = i == 0 ? svt_aom_dc_quant_qtx(q, 0, EB_EIGHT_BIT)  // :128
                                         : svt_aom_ac_quant_qtx(q, 0, EB_EIGHT_BIT); // :129
        svt_aom_invert_quant(&t->quant[i], &t->quant_shift[i], quant_qtx);           // :130
        t->quant_fp[i] = (int16_t)((1 << 16) / quant_qtx);                           // :131
        t->round_fp[i] = (int16_t)((64 * quant_qtx) >> 7);                           // :127 + :132
        t->zbin[i]     = (int16_t)ROUND_POWER_OF_TWO(qzbin_factor * quant_qtx, 7);   // :133
        t->round[i]    = (int16_t)((qrounding_factor * quant_qtx) >> 7);             // :134
        t->dequant[i]  = (int16_t)quant_qtx;                                         // :135
    }
}

// TX_4X4 FP quantize entry = quantize_fp_helper_c at log_scale 0
// (full_loop.c:286-305, svt_av1_quantize_fp_c forwards verbatim). scan = the
// default scan; iscan/zbin/quant_shift are (void)'d inside the helper (qm/iqm
// NULL -> the non-QM branch).
static void svtd_quantize_fp_4x4(const TranLow* coeff, const SvtdQuantTables* t, const int16_t* scan,
                                 TranLow* qcoeff, TranLow* dqcoeff, uint16_t* eob) {
    quantize_fp_helper_c(coeff, 16, t->zbin, t->round_fp, t->quant_fp, t->quant_shift, qcoeff,
                         dqcoeff, t->dequant, eob, scan, NULL, NULL, NULL, 0);
}

// TX_4X4 B quantize entry = svt_aom_quantize_b_c verbatim at log_scale 0
// (the non-QM dispatch inside av1_quantize_b_facade_ii, full_loop.c:202-218).
static void svtd_quantize_b_4x4(const TranLow* coeff, const SvtdQuantTables* t, const int16_t* scan,
                                TranLow* qcoeff, TranLow* dqcoeff, uint16_t* eob) {
    svt_aom_quantize_b_c(coeff, 16, t->zbin, t->round, t->quant, t->quant_shift, qcoeff, dqcoeff,
                         t->dequant, eob, scan, NULL, NULL, NULL, 0);
}

// ---- FR-series: frame loops gather REAL reconstructed top-right ------------
// when nTopRightPx > 0 (above[B..2B-1] = recon[(py-1)*fstride + px + B + i];
// M1 availability: the row above is fully reconstructed). Helper applied to
// the shared gather idiom.
static void svtd_gather_above(uint8_t* above, const uint8_t* recon, int fstride, int px, int py,
                              int bsz, int nTr) {
    for (int i = 0; i < bsz; ++i) above[i] = recon[(py - 1) * fstride + px + i];
    for (int i = 0; i < nTr; ++i) above[bsz + i] = recon[(py - 1) * fstride + px + bsz + i];
}

// ---- frame-policy composition (8x8 blocks, 2x2 grid = 16x16 frame) ---------
// Same D2 policy as the 4x4 version, SAD scored with sad8x8 semantics.
static void svtd_frame_auto_8x8_blocks(const uint8_t* src, uint8_t* recon, int32_t* coeffs, int* modes) {
    const int fstride = 16;
    const int gridW = 2, gridH = 2;
    const int bsz = 8;
    memset(recon, 0, 256);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * bsz, py = by * bsz;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? bsz : 0;
            const int nLeft = hasLeft ? bsz : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? bsz : 0;
            uint8_t above[17] = {0};
            uint8_t left[17] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, fstride, px, py, bsz, nTr);
            if (hasLeft) for (int i = 0; i < bsz; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];

            const int aboveMode = hasTop ? modes[(by - 1) * gridW + bx] : DC_PRED;
            const int leftMode = hasLeft ? modes[by * gridW + bx - 1] : DC_PRED;
            svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED ||
                               aboveMode == SMOOTH_H_PRED) ||
                              (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED ||
                               leftMode == SMOOTH_H_PRED))
                                 ? 1
                                 : 0;

            uint8_t srcblk[64];
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    srcblk[i * bsz + j] = src[(py + i) * fstride + px + j];

            // D2 policy: 13 candidates, SAD scored, lowest wins, tie = lowest idx
            uint32_t best_sad = 0;
            int mode = -1;
            for (int m = 0; m <= PAETH_PRED; ++m) {
                uint8_t pred[64];
                svtd_call_builder_tx(pred, m, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                     0, al, TX_8X8);
                const uint32_t sad = svt_nxm_sad_kernel_helper_c(srcblk, bsz, pred, bsz, bsz, bsz);
                if (mode < 0 || sad < best_sad) { best_sad = sad; mode = m; }
            }
            modes[by * gridW + bx] = mode;

            uint8_t pred[64];
            svtd_call_builder_tx(pred, mode, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                 0, al, TX_8X8);
            int16_t res[64];
            for (int i = 0; i < 64; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
            int32_t cb[64];
            svtd_fwd2d8x8(res, bsz, cb, svt_av1_fdct8_new);
            for (int i = 0; i < 64; ++i) coeffs[(by * gridW + bx) * 64 + i] = cb[i];
            svtd_inv2dadd8x8(cb, pred, bsz, svt_av1_idct8_new);
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    recon[(py + i) * fstride + px + j] = pred[i * bsz + j];
        }
    }
}

// ---- FR fixture loop: D2 policy, 4x4 blocks, 4x4 grid = 16x16 frame --------
// svtd_frame_auto_8x8 generalized to gridW=gridH=4, fstride=16 (real
// top-right gather via svtd_gather_above).
static void svtd_frame_auto_4x4_16x16(const uint8_t* src, uint8_t* recon, int32_t* coeffs,
                                      int* modes) {
    const int fstride = 16;
    const int gridW = 4, gridH = 4;
    memset(recon, 0, 256);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 4, py = by * 4;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? 4 : 0;
            const int nLeft = hasLeft ? 4 : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? 4 : 0;
            uint8_t above[9] = {0};
            uint8_t left[9] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, fstride, px, py, 4, nTr);
            if (hasLeft) for (int i = 0; i < 4; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];

            const int aboveMode = hasTop ? modes[(by - 1) * gridW + bx] : DC_PRED;
            const int leftMode = hasLeft ? modes[by * gridW + bx - 1] : DC_PRED;
            svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED ||
                               aboveMode == SMOOTH_H_PRED) ||
                              (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED ||
                               leftMode == SMOOTH_H_PRED))
                                 ? 1
                                 : 0;

            const uint8_t srcblk[16] = {
                src[(py + 0) * fstride + px + 0], src[(py + 0) * fstride + px + 1],
                src[(py + 0) * fstride + px + 2], src[(py + 0) * fstride + px + 3],
                src[(py + 1) * fstride + px + 0], src[(py + 1) * fstride + px + 1],
                src[(py + 1) * fstride + px + 2], src[(py + 1) * fstride + px + 3],
                src[(py + 2) * fstride + px + 0], src[(py + 2) * fstride + px + 1],
                src[(py + 2) * fstride + px + 2], src[(py + 2) * fstride + px + 3],
                src[(py + 3) * fstride + px + 0], src[(py + 3) * fstride + px + 1],
                src[(py + 3) * fstride + px + 2], src[(py + 3) * fstride + px + 3]};

            uint32_t best_sad = 0;
            const int mode = svtd_decide(srcblk, above, nTop, nTr, left, nLeft, 0, al, &best_sad);
            modes[by * gridW + bx] = mode;

            uint8_t pred[16];
            svtd_call_builder(pred, mode, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft, 0, al);
            int16_t res[16];
            for (int i = 0; i < 16; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
            int32_t cb[16];
            svtd_fwd2d4x4(res, 4, cb, svt_av1_fdct4_new);
            for (int i = 0; i < 16; ++i) coeffs[(by * gridW + bx) * 16 + i] = cb[i];
            svtd_inv2dadd4x4(cb, pred, 4, svt_av1_idct4_new);
            for (int i = 0; i < 16; ++i) recon[(py + (i >> 2)) * fstride + px + (i & 3)] = pred[i];
        }
    }
}

// ---- Q2 frame composition: D2 policy + fixed-qindex quantization -----------
// Same loop as svtd_frame_auto_8x8 (4x4 blocks), with the FP quantizer wired
// in: qcoeff = coded coeffs, dqcoeff feeds the inverse. Fixed qindex per run
// (no rate control).
static void svtd_frame_auto_4x4_q(const uint8_t* src, uint8_t* recon, int32_t* coeffs, int* modes,
                                  int qindex) {
    const int fstride = 8;
    const int gridW = 2, gridH = 2;
    SvtdQuantTables t;
    svtd_build_quantizer_luma(qindex, &t);
    int16_t scan[16];
    svtd_default_scan_4x4(scan);
    memset(recon, 0, 64);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 4, py = by * 4;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? 4 : 0;
            const int nLeft = hasLeft ? 4 : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? 4 : 0;
            uint8_t above[9] = {0};
            uint8_t left[9] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, fstride, px, py, 4, nTr);
            if (hasLeft) for (int i = 0; i < 4; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];

            const int aboveMode = hasTop ? modes[(by - 1) * gridW + bx] : DC_PRED;
            const int leftMode = hasLeft ? modes[by * gridW + bx - 1] : DC_PRED;
            svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED ||
                               aboveMode == SMOOTH_H_PRED) ||
                              (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED ||
                               leftMode == SMOOTH_H_PRED))
                                 ? 1
                                 : 0;

            const uint8_t srcblk[16] = {
                src[(py + 0) * fstride + px + 0], src[(py + 0) * fstride + px + 1],
                src[(py + 0) * fstride + px + 2], src[(py + 0) * fstride + px + 3],
                src[(py + 1) * fstride + px + 0], src[(py + 1) * fstride + px + 1],
                src[(py + 1) * fstride + px + 2], src[(py + 1) * fstride + px + 3],
                src[(py + 2) * fstride + px + 0], src[(py + 2) * fstride + px + 1],
                src[(py + 2) * fstride + px + 2], src[(py + 2) * fstride + px + 3],
                src[(py + 3) * fstride + px + 0], src[(py + 3) * fstride + px + 1],
                src[(py + 3) * fstride + px + 2], src[(py + 3) * fstride + px + 3]};

            uint32_t best_sad = 0;
            const int mode = svtd_decide(srcblk, above, nTop, nTr, left, nLeft, 0, al, &best_sad);
            modes[by * gridW + bx] = mode;

            uint8_t pred[16];
            svtd_call_builder(pred, mode, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft, 0, al);
            int16_t res[16];
            for (int i = 0; i < 16; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
            int32_t cb[16];
            svtd_fwd2d4x4(res, 4, cb, svt_av1_fdct4_new);
            TranLow qc[16], dq[16];
            uint16_t eob = 0;
            svtd_quantize_fp_4x4(cb, &t, scan, qc, dq, &eob);
            for (int i = 0; i < 16; ++i) coeffs[(by * gridW + bx) * 16 + i] = qc[i];
            svtd_inv2dadd4x4(dq, pred, 4, svt_av1_idct4_new);
            for (int i = 0; i < 16; ++i) recon[(py + (i >> 2)) * fstride + px + (i & 3)] = pred[i];
        }
    }
}

// Default (up-right diagonal) scan for 8x8, same svt_aom_init_iscan formula
// (coefficients.c:345-363) at W=H=8.
static void svtd_default_scan_8x8(int16_t* scan) {
    const int W = 8, H = 8;  // tx_size_wide/high[TX_8X8]
    int idx = 0;
    for (int d = 0; d < W + H - 1; ++d) {
        const int rlo  = (d - (W - 1)) > 0 ? (d - (W - 1)) : 0;
        const int rhi  = d < (H - 1) ? d : (H - 1);
        int       incr = (H > W) ? 1 : (W > H) ? 0 : (d & 1);
        if (incr) {
            for (int r = rlo; r <= rhi; ++r) {
                scan[idx++] = (int16_t)(r * W + (d - r));
            }
        } else {
            for (int r = rhi; r >= rlo; --r) {
                scan[idx++] = (int16_t)(r * W + (d - r));
            }
        }
    }
}

// TX_8X8 FP quantize entry: quantize_fp_helper_c at n_coeffs=64, log_scale 0
// (log_scale = av1_get_tx_scale_tab[TX_8X8] = 0, full_loop.c:22 + :1617).
static void svtd_quantize_fp_8x8(const TranLow* coeff, const SvtdQuantTables* t, const int16_t* scan,
                                 TranLow* qcoeff, TranLow* dqcoeff, uint16_t* eob) {
    quantize_fp_helper_c(coeff, 64, t->zbin, t->round_fp, t->quant_fp, t->quant_shift, qcoeff,
                         dqcoeff, t->dequant, eob, scan, NULL, NULL, NULL, 0);
}

// TX_8X8 B quantize entry: svt_aom_quantize_b_c verbatim at log_scale 0.
static void svtd_quantize_b_8x8(const TranLow* coeff, const SvtdQuantTables* t, const int16_t* scan,
                                TranLow* qcoeff, TranLow* dqcoeff, uint16_t* eob) {
    svt_aom_quantize_b_c(coeff, 64, t->zbin, t->round, t->quant, t->quant_shift, qcoeff, dqcoeff,
                         t->dequant, eob, scan, NULL, NULL, NULL, 0);
}

// TX_16X16 FP quantize entry: quantize_fp_helper_c at n_coeffs=256, log_scale
// 0 (av1_get_tx_scale_tab[TX_16X16] = 0, full_loop.c:22 + :1617).
static void svtd_quantize_fp_16x16(const TranLow* coeff, const SvtdQuantTables* t, const int16_t* scan,
                                   TranLow* qcoeff, TranLow* dqcoeff, uint16_t* eob) {
    quantize_fp_helper_c(coeff, 256, t->zbin, t->round_fp, t->quant_fp, t->quant_shift, qcoeff,
                         dqcoeff, t->dequant, eob, scan, NULL, NULL, NULL, 0);
}

// TX_16X16 B quantize entry: svt_aom_quantize_b_c verbatim at log_scale 0.
static void svtd_quantize_b_16x16(const TranLow* coeff, const SvtdQuantTables* t, const int16_t* scan,
                                  TranLow* qcoeff, TranLow* dqcoeff, uint16_t* eob) {
    svt_aom_quantize_b_c(coeff, 256, t->zbin, t->round, t->quant, t->quant_shift, qcoeff, dqcoeff,
                         t->dequant, eob, scan, NULL, NULL, NULL, 0);
}

// TX_32X32 FP quantize entry (L0): quantize_fp_helper_c at n_coeffs=1024,
// log_scale 1 (av1_get_tx_scale_tab[TX_32X32] = 1, full_loop.c:22). The
// helper consumes log_scale in its own arithmetic (full_loop.c:228 rounding
// ROUND_POWER_OF_TWO, :244 threshold << (1+log_scale), :246 >> (16-log_scale),
// :249 dq >> log_scale) - the escalated values are proven by these gate lines
// BEFORE any host port.
static void svtd_quantize_fp_32x32(const TranLow* coeff, const SvtdQuantTables* t, const int16_t* scan,
                                   TranLow* qcoeff, TranLow* dqcoeff, uint16_t* eob) {
    quantize_fp_helper_c(coeff, 1024, t->zbin, t->round_fp, t->quant_fp, t->quant_shift, qcoeff,
                         dqcoeff, t->dequant, eob, scan, NULL, NULL, NULL, 1);
}

// TX_32X32 B quantize entry: svt_aom_quantize_b_c at log_scale 1
// (full_loop.c:36 zbins ROUND_POWER_OF_TWO, :67 round add, :69-70
// >> (16-log_scale+AOM_QM_BITS), :74 dq >> log_scale).
static void svtd_quantize_b_32x32(const TranLow* coeff, const SvtdQuantTables* t, const int16_t* scan,
                                  TranLow* qcoeff, TranLow* dqcoeff, uint16_t* eob) {
    svt_aom_quantize_b_c(coeff, 1024, t->zbin, t->round, t->quant, t->quant_shift, qcoeff, dqcoeff,
                         t->dequant, eob, scan, NULL, NULL, NULL, 1);
}

// ---- QW1 frame composition: 8x8 D-policy + fixed-qindex quantization ------
// Same loop as svtd_frame_auto_8x8_blocks (8x8 blocks, D2 policy), with the
// FP quantizer wired in at n_coeffs=64/log_scale 0: qcoeff = coded coeffs,
// dqcoeff feeds the inverse. Fixed qindex per run (no rate control).
static void svtd_frame_auto_8x8_q(const uint8_t* src, uint8_t* recon, int32_t* coeffs, int* modes,
                                  int qindex) {
    const int fstride = 16;
    const int gridW = 2, gridH = 2;
    const int bsz = 8;
    SvtdQuantTables t;
    svtd_build_quantizer_luma(qindex, &t);
    int16_t scan8[64];
    svtd_default_scan_8x8(scan8);
    memset(recon, 0, 256);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * bsz, py = by * bsz;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? bsz : 0;
            const int nLeft = hasLeft ? bsz : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? bsz : 0;
            uint8_t above[17] = {0};
            uint8_t left[17] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, fstride, px, py, bsz, nTr);
            if (hasLeft) for (int i = 0; i < bsz; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];

            const int aboveMode = hasTop ? modes[(by - 1) * gridW + bx] : DC_PRED;
            const int leftMode = hasLeft ? modes[by * gridW + bx - 1] : DC_PRED;
            svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED ||
                               aboveMode == SMOOTH_H_PRED) ||
                              (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED ||
                               leftMode == SMOOTH_H_PRED))
                                 ? 1
                                 : 0;

            uint8_t srcblk[64];
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    srcblk[i * bsz + j] = src[(py + i) * fstride + px + j];

            // D2 policy: 13 candidates, SAD scored, lowest wins, tie = lowest idx
            uint32_t best_sad = 0;
            int mode = -1;
            for (int m = 0; m <= PAETH_PRED; ++m) {
                uint8_t pred[64];
                svtd_call_builder_tx(pred, m, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                     0, al, TX_8X8);
                const uint32_t sad = svt_nxm_sad_kernel_helper_c(srcblk, bsz, pred, bsz, bsz, bsz);
                if (mode < 0 || sad < best_sad) { best_sad = sad; mode = m; }
            }
            modes[by * gridW + bx] = mode;

            uint8_t pred[64];
            svtd_call_builder_tx(pred, mode, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                 0, al, TX_8X8);
            int16_t res[64];
            for (int i = 0; i < 64; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
            int32_t cb[64];
            svtd_fwd2d8x8(res, bsz, cb, svt_av1_fdct8_new);
            TranLow qc[64], dq[64];
            uint16_t eob = 0;
            svtd_quantize_fp_8x8(cb, &t, scan8, qc, dq, &eob);
            for (int i = 0; i < 64; ++i) coeffs[(by * gridW + bx) * 64 + i] = qc[i];
            svtd_inv2dadd8x8(dq, pred, bsz, svt_av1_idct8_new);
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    recon[(py + i) * fstride + px + j] = pred[i * bsz + j];
        }
    }
}

// ---- C7: 16x16 frame-policy composition (2x2 grid of 16x16 = 32x32) -------
// Same D2 policy as the smaller variants, TX_16X16 column; REAL recon
// top-right gather via svtd_gather_above (fstride 32). Row-1 blocks predict
// from the reconstructed ramp row 15; block (bx=0, by=1) is the only nTr>0
// block and its SAD consumes above[16..31].
static void svtd_default_scan_16x16(int16_t* scan);  // defined with the Q0 scan block below

static void svtd_frame_auto_16x16_blocks(const uint8_t* src, uint8_t* recon, int32_t* coeffs, int* modes) {
    const int fstride = 32;
    const int gridW = 2, gridH = 2;
    const int bsz = 16;
    memset(recon, 0, 1024);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * bsz, py = by * bsz;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? bsz : 0;
            const int nLeft = hasLeft ? bsz : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? bsz : 0;
            uint8_t above[33] = {0};
            uint8_t left[33] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, fstride, px, py, bsz, nTr);
            if (hasLeft) for (int i = 0; i < bsz; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];

            const int aboveMode = hasTop ? modes[(by - 1) * gridW + bx] : DC_PRED;
            const int leftMode = hasLeft ? modes[by * gridW + bx - 1] : DC_PRED;
            svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED ||
                               aboveMode == SMOOTH_H_PRED) ||
                              (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED ||
                               leftMode == SMOOTH_H_PRED))
                                 ? 1
                                 : 0;

            uint8_t srcblk[256];
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    srcblk[i * bsz + j] = src[(py + i) * fstride + px + j];

            // D2 policy: 13 candidates, SAD scored with the 16x16 sad helper
            uint32_t best_sad = 0;
            int mode = -1;
            for (int m = 0; m <= PAETH_PRED; ++m) {
                uint8_t pred[256];
                svtd_call_builder_tx(pred, m, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                     0, al, TX_16X16);
                const uint32_t sad = svt_nxm_sad_kernel_helper_c(srcblk, bsz, pred, bsz, bsz, bsz);
                if (mode < 0 || sad < best_sad) { best_sad = sad; mode = m; }
            }
            modes[by * gridW + bx] = mode;

            uint8_t pred[256];
            svtd_call_builder_tx(pred, mode, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                 0, al, TX_16X16);
            int16_t res[256];
            for (int i = 0; i < 256; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
            int32_t cb[256];
            svtd_fwd2d16x16(res, bsz, cb, svt_av1_fdct16_new);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = cb[i];
            svtd_inv2dadd16x16(cb, pred, bsz, svt_av1_idct16_new);
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    recon[(py + i) * fstride + px + j] = pred[i * bsz + j];
        }
    }
}

// Q variant: same loop with the FP quantizer wired at a fixed qindex
// (n_coeffs=256, log_scale 0).
static void svtd_frame_auto_16x16_q(const uint8_t* src, uint8_t* recon, int32_t* coeffs, int* modes,
                                    int qindex) {
    const int gridW = 2, gridH = 2;
    const int bsz = 16;
    SvtdQuantTables t;
    svtd_build_quantizer_luma(qindex, &t);
    int16_t scan16[256];
    svtd_default_scan_16x16(scan16);
    memset(recon, 0, 1024);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * bsz, py = by * bsz;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? bsz : 0;
            const int nLeft = hasLeft ? bsz : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? bsz : 0;
            uint8_t above[33] = {0};
            uint8_t left[33] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, 32, px, py, bsz, nTr);
            if (hasLeft) for (int i = 0; i < bsz; ++i) left[i] = recon[(py + i) * 32 + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * 32 + px - 1];

            const int aboveMode = hasTop ? modes[(by - 1) * gridW + bx] : DC_PRED;
            const int leftMode = hasLeft ? modes[by * gridW + bx - 1] : DC_PRED;
            svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED ||
                               aboveMode == SMOOTH_H_PRED) ||
                              (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED ||
                               leftMode == SMOOTH_H_PRED))
                                 ? 1
                                 : 0;

            uint8_t srcblk[256];
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    srcblk[i * bsz + j] = src[(py + i) * 32 + px + j];

            uint32_t best_sad = 0;
            int mode = -1;
            for (int m = 0; m <= PAETH_PRED; ++m) {
                uint8_t pred[256];
                svtd_call_builder_tx(pred, m, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                     0, al, TX_16X16);
                const uint32_t sad = svt_nxm_sad_kernel_helper_c(srcblk, bsz, pred, bsz, bsz, bsz);
                if (mode < 0 || sad < best_sad) { best_sad = sad; mode = m; }
            }
            modes[by * gridW + bx] = mode;

            uint8_t pred[256];
            svtd_call_builder_tx(pred, mode, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                 0, al, TX_16X16);
            int16_t res[256];
            for (int i = 0; i < 256; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
            int32_t cb[256];
            svtd_fwd2d16x16(res, bsz, cb, svt_av1_fdct16_new);
            TranLow qc[256], dq[256];
            uint16_t eob = 0;
            svtd_quantize_fp_16x16(cb, &t, scan16, qc, dq, &eob);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = qc[i];
            svtd_inv2dadd16x16(dq, pred, bsz, svt_av1_idct16_new);
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    recon[(py + i) * 32 + px + j] = pred[i * bsz + j];
        }
    }
}

// ---- HK3: forced-mode 16x16 frame composition (closes the C7b gap) --------
// Mirrors svtd_frame_v_dct_8x8 (F1 pattern) at TX_16X16: V_PRED forced + DCT,
// 2x2 grid of 16x16 over the f16 fixture, M1 availability with the REAL recon
// top-right gather. recon == source (the 16x16 fwd/inv roundtrip is exact);
// the coeffs carry the pin (fwd of src minus the forced-mode prediction).
static void svtd_frame_v_dct_16x16(const uint8_t* src, uint8_t* recon, int32_t* coeffs) {
    const int fstride = 32;
    const int gridW = 2, gridH = 2;
    memset(recon, 0, 1024);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16, py = by * 16;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? 16 : 0;
            const int nLeft = hasLeft ? 16 : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? 16 : 0;
            uint8_t above[33] = {0};
            uint8_t left[33] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, fstride, px, py, 16, nTr);
            if (hasLeft) for (int i = 0; i < 16; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];
            uint8_t pred[256];
            svtd_call_builder_tx(pred, V_PRED, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left,
                                 nLeft, 0, al, TX_16X16);
            int16_t res[256];
            for (int i = 0; i < 256; ++i)
                res[i] = (int16_t)(src[(py + (i >> 4)) * fstride + px + (i & 15)] - pred[i]);
            int32_t cb[256];
            svtd_fwd2d16x16(res, 16, cb, svt_av1_fdct16_new);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = cb[i];
            svtd_inv2dadd16x16(cb, pred, 16, svt_av1_idct16_new);
            for (int i = 0; i < 256; ++i)
                recon[(py + (i >> 4)) * fstride + px + (i & 15)] = pred[i];
        }
    }
}

// Q variant: forced V_PRED + DCT + FP quantizer at a fixed qindex
// (n_coeffs=256, log_scale 0); qcoeff = coded coeffs, dqcoeff feeds the
// inverse, so recon carries real quantization loss.
static void svtd_frame_v_dct_16x16_q(const uint8_t* src, uint8_t* recon, int32_t* coeffs, int qindex) {
    const int gridW = 2, gridH = 2;
    SvtdQuantTables t;
    svtd_build_quantizer_luma(qindex, &t);
    int16_t scan16[256];
    svtd_default_scan_16x16(scan16);
    memset(recon, 0, 1024);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16, py = by * 16;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? 16 : 0;
            const int nLeft = hasLeft ? 16 : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? 16 : 0;
            uint8_t above[33] = {0};
            uint8_t left[33] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, 32, px, py, 16, nTr);
            if (hasLeft) for (int i = 0; i < 16; ++i) left[i] = recon[(py + i) * 32 + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * 32 + px - 1];
            uint8_t pred[256];
            svtd_call_builder_tx(pred, V_PRED, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left,
                                 nLeft, 0, al, TX_16X16);
            int16_t res[256];
            for (int i = 0; i < 256; ++i)
                res[i] = (int16_t)(src[(py + (i >> 4)) * 32 + px + (i & 15)] - pred[i]);
            int32_t cb[256];
            svtd_fwd2d16x16(res, 16, cb, svt_av1_fdct16_new);
            TranLow qc[256], dq[256];
            uint16_t eob = 0;
            svtd_quantize_fp_16x16(cb, &t, scan16, qc, dq, &eob);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = qc[i];
            svtd_inv2dadd16x16(dq, pred, 16, svt_av1_idct16_new);
            for (int i = 0; i < 256; ++i)
                recon[(py + (i >> 4)) * 32 + px + (i & 15)] = pred[i];
        }
    }
}

// Default (up-right diagonal) scan for 32x32 (L0), svt_aom_init_iscan formula
// (coefficients.c:345-363) at W=H=32.
static void svtd_default_scan_32x32(int16_t* scan) {
    const int W = 32, H = 32;  // tx_size_wide/high[TX_32X32]
    int idx = 0;
    for (int d = 0; d < W + H - 1; ++d) {
        const int rlo  = (d - (W - 1)) > 0 ? (d - (W - 1)) : 0;
        const int rhi  = d < (H - 1) ? d : (H - 1);
        int       incr = (H > W) ? 1 : (W > H) ? 0 : (d & 1);
        if (incr) {
            for (int r = rlo; r <= rhi; ++r) {
                scan[idx++] = (int16_t)(r * W + (d - r));
            }
        } else {
            for (int r = rhi; r >= rlo; --r) {
                scan[idx++] = (int16_t)(r * W + (d - r));
            }
        }
    }
}

// ---- gen_inv_stage_range 32x32/64x64 gate lines (L0) -----------------------
// svt_av1_gen_inv_stage_range (inv_transforms.c:44) at bd=8:
// opt_range_row/col = 16 (:49-51); inv_start_range[TX_32X32] = 7,
// [TX_64X64] = 7 (inv_transforms.h:221-222); shift = inv_shift_32x32/64x64 =
// {-2, -4} (inv_transforms.c:21-22). Stage counts (av1_txfm_stage_num_list,
// inv_transforms.h:197-206): DCT32 = 10, ADST32 = 10, DCT64 = 12; ADST64 is
// TXFM_TYPE_INVALID (av1_txfm_type_ls[4], inv_transforms.h:196) - only
// DCT_DCT is legal at TX_64X64 in this tree.
static void svtd_gen_inv_range_32x32(void) {
    const int opt_range       = 16;  // bd=8
    const int stage_num_dct   = 10;
    const int stage_num_adst  = 10;
    printf("gen_inv_range_32x32_dct:");
    for (int i = 0; i < stage_num_dct; ++i) printf(" %d", opt_range);
    printf("\n");
    printf("gen_inv_range_32x32_adst:");
    for (int i = 0; i < stage_num_adst; ++i) printf(" %d", opt_range);
    printf("\n");
}

static void svtd_gen_inv_range_64x64(void) {
    const int opt_range     = 16;  // bd=8
    const int stage_num_dct = 12;
    printf("gen_inv_range_64x64_dct:");
    for (int i = 0; i < stage_num_dct; ++i) printf(" %d", opt_range);
    printf("\n");
}

// ---- L6: 32x32 frame-policy composition (2x2 grid of 32x32 = 64x64 frame) -
// Same D2 policy, TX_32X32 column; REAL recon top-right gather via
// svtd_gather_above (fstride 64). Rows 32-63 of the frame are zero.
static void svtd_frame_auto_32x32_blocks(const uint8_t* src, uint8_t* recon, int32_t* coeffs, int* modes) {
    const int fstride = 64;
    const int gridW = 2, gridH = 2;
    const int bsz = 32;
    memset(recon, 0, 4096);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * bsz, py = by * bsz;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? bsz : 0;
            const int nLeft = hasLeft ? bsz : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? bsz : 0;
            uint8_t above[65] = {0};
            uint8_t left[65] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, fstride, px, py, bsz, nTr);
            if (hasLeft) for (int i = 0; i < bsz; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];

            const int aboveMode = hasTop ? modes[(by - 1) * gridW + bx] : DC_PRED;
            const int leftMode = hasLeft ? modes[by * gridW + bx - 1] : DC_PRED;
            svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED ||
                               aboveMode == SMOOTH_H_PRED) ||
                              (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED ||
                               leftMode == SMOOTH_H_PRED))
                                 ? 1
                                 : 0;

            uint8_t srcblk[1024];
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    srcblk[i * bsz + j] = src[(py + i) * fstride + px + j];

            uint32_t best_sad = 0;
            int mode = -1;
            for (int m = 0; m <= PAETH_PRED; ++m) {
                uint8_t pred[1024];
                svtd_call_builder_tx(pred, m, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                     0, al, TX_32X32);
                const uint32_t sad = svt_nxm_sad_kernel_helper_c(srcblk, bsz, pred, bsz, bsz, bsz);
                if (mode < 0 || sad < best_sad) { best_sad = sad; mode = m; }
            }
            modes[by * gridW + bx] = mode;

            uint8_t pred[1024];
            svtd_call_builder_tx(pred, mode, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                 0, al, TX_32X32);
            int16_t res[1024];
            for (int i = 0; i < 1024; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
            int32_t cb[1024];
            svtd_fwd2d32x32(res, bsz, cb, svt_av1_fdct32_new);
            for (int i = 0; i < 1024; ++i) coeffs[(by * gridW + bx) * 1024 + i] = cb[i];
            svtd_inv2dadd32x32(cb, pred, bsz, svt_av1_idct32_new);
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    recon[(py + i) * fstride + px + j] = pred[i * bsz + j];
        }
    }
}

// Q variant (log_scale 1).
static void svtd_frame_auto_32x32_q(const uint8_t* src, uint8_t* recon, int32_t* coeffs, int* modes,
                                    int qindex) {
    const int gridW = 2, gridH = 2;
    const int bsz = 32;
    SvtdQuantTables t;
    svtd_build_quantizer_luma(qindex, &t);
    int16_t scan32[1024];
    svtd_default_scan_32x32(scan32);
    memset(recon, 0, 4096);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * bsz, py = by * bsz;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? bsz : 0;
            const int nLeft = hasLeft ? bsz : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? bsz : 0;
            uint8_t above[65] = {0};
            uint8_t left[65] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, 64, px, py, bsz, nTr);
            if (hasLeft) for (int i = 0; i < bsz; ++i) left[i] = recon[(py + i) * 64 + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * 64 + px - 1];

            const int aboveMode = hasTop ? modes[(by - 1) * gridW + bx] : DC_PRED;
            const int leftMode = hasLeft ? modes[by * gridW + bx - 1] : DC_PRED;
            svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED ||
                               aboveMode == SMOOTH_H_PRED) ||
                              (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED ||
                               leftMode == SMOOTH_H_PRED))
                                 ? 1
                                 : 0;

            uint8_t srcblk[1024];
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    srcblk[i * bsz + j] = src[(py + i) * 64 + px + j];

            uint32_t best_sad = 0;
            int mode = -1;
            for (int m = 0; m <= PAETH_PRED; ++m) {
                uint8_t pred[1024];
                svtd_call_builder_tx(pred, m, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                     0, al, TX_32X32);
                const uint32_t sad = svt_nxm_sad_kernel_helper_c(srcblk, bsz, pred, bsz, bsz, bsz);
                if (mode < 0 || sad < best_sad) { best_sad = sad; mode = m; }
            }
            modes[by * gridW + bx] = mode;

            uint8_t pred[1024];
            svtd_call_builder_tx(pred, mode, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                 0, al, TX_32X32);
            int16_t res[1024];
            for (int i = 0; i < 1024; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
            int32_t cb[1024];
            svtd_fwd2d32x32(res, bsz, cb, svt_av1_fdct32_new);
            TranLow qc[1024], dq[1024];
            uint16_t eob = 0;
            svtd_quantize_fp_32x32(cb, &t, scan32, qc, dq, &eob);
            for (int i = 0; i < 1024; ++i) coeffs[(by * gridW + bx) * 1024 + i] = qc[i];
            svtd_inv2dadd32x32(dq, pred, bsz, svt_av1_idct32_new);
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    recon[(py + i) * 64 + px + j] = pred[i * bsz + j];
        }
    }
}

// Forced-mode (V_PRED + DCT) 32x32 frame composition (HK3 pattern, avoids the
// C7b gap for Recon32x32/Recon32x32Q).
static void svtd_frame_v_dct_32x32(const uint8_t* src, uint8_t* recon, int32_t* coeffs) {
    const int fstride = 64;
    const int gridW = 2, gridH = 2;
    memset(recon, 0, 4096);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16 * 2, py = by * 32;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? 32 : 0;
            const int nLeft = hasLeft ? 32 : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? 32 : 0;
            uint8_t above[65] = {0};
            uint8_t left[65] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, fstride, px, py, 32, nTr);
            if (hasLeft) for (int i = 0; i < 32; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];
            uint8_t pred[1024];
            svtd_call_builder_tx(pred, V_PRED, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left,
                                 nLeft, 0, al, TX_32X32);
            int16_t res[1024];
            for (int i = 0; i < 1024; ++i)
                res[i] = (int16_t)(src[(py + (i >> 5)) * fstride + px + (i & 31)] - pred[i]);
            int32_t cb[1024];
            svtd_fwd2d32x32(res, 32, cb, svt_av1_fdct32_new);
            for (int i = 0; i < 1024; ++i) coeffs[(by * gridW + bx) * 1024 + i] = cb[i];
            svtd_inv2dadd32x32(cb, pred, 32, svt_av1_idct32_new);
            for (int i = 0; i < 1024; ++i)
                recon[(py + (i >> 5)) * fstride + px + (i & 31)] = pred[i];
        }
    }
}

// Q variant of forced-mode.
static void svtd_frame_v_dct_32x32_q(const uint8_t* src, uint8_t* recon, int32_t* coeffs, int qindex) {
    const int gridW = 2, gridH = 2;
    SvtdQuantTables t;
    svtd_build_quantizer_luma(qindex, &t);
    int16_t scan32[1024];
    svtd_default_scan_32x32(scan32);
    memset(recon, 0, 4096);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 32, py = by * 32;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? 32 : 0;
            const int nLeft = hasLeft ? 32 : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? 32 : 0;
            uint8_t above[65] = {0};
            uint8_t left[65] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, 64, px, py, 32, nTr);
            if (hasLeft) for (int i = 0; i < 32; ++i) left[i] = recon[(py + i) * 64 + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * 64 + px - 1];
            uint8_t pred[1024];
            svtd_call_builder_tx(pred, V_PRED, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left,
                                 nLeft, 0, al, TX_32X32);
            int16_t res[1024];
            for (int i = 0; i < 1024; ++i)
                res[i] = (int16_t)(src[(py + (i >> 5)) * 64 + px + (i & 31)] - pred[i]);
            int32_t cb[1024];
            svtd_fwd2d32x32(res, 32, cb, svt_av1_fdct32_new);
            TranLow qc[1024], dq[1024];
            uint16_t eob = 0;
            svtd_quantize_fp_32x32(cb, &t, scan32, qc, dq, &eob);
            for (int i = 0; i < 1024; ++i) coeffs[(by * gridW + bx) * 1024 + i] = qc[i];
            svtd_inv2dadd32x32(dq, pred, 32, svt_av1_idct32_new);
            for (int i = 0; i < 1024; ++i)
                recon[(py + (i >> 5)) * 64 + px + (i & 31)] = pred[i];
        }
    }
}

// Default (up-right diagonal) scan for 64x64 (L7), svt_aom_init_iscan formula
// (coefficients.c:345-363) at W=H=64.
static void svtd_default_scan_64x64(int16_t* scan) {
    const int W = 64, H = 64;  // tx_size_wide/high[TX_64X64]
    int idx = 0;
    for (int d = 0; d < W + H - 1; ++d) {
        const int rlo  = (d - (W - 1)) > 0 ? (d - (W - 1)) : 0;
        const int rhi  = d < (H - 1) ? d : (H - 1);
        int       incr = (H > W) ? 1 : (W > H) ? 0 : (d & 1);
        if (incr) {
            for (int r = rlo; r <= rhi; ++r) {
                scan[idx++] = (int16_t)(r * W + (d - r));
            }
        } else {
            for (int r = rhi; r >= rlo; --r) {
                scan[idx++] = (int16_t)(r * W + (d - r));
            }
        }
    }
}

// ---- FS2: THE TX_64X64 SCAN-CONTRACT SETTLE (pinned against SVT's own C) ---
// The TX_64X64 coefficient-coding domain is the ADJUSTED 32x32 (the top-left
// quadrant of the 64x64 matrix), end to end in the pinned tree:
//   1. av1_get_max_eob(TX_64X64) = 1024 (inv_transforms.h:129-137); the
//      quantize runs n_coeffs = 1024 (full_loop.c:1262).
//   2. The scan pool at TX_64X64 is generated with W = H = 32, n = 1024
//      (svt_aom_init_iscan, coefficients.c:331-337) -> the default scan is
//      the 32x32-grid up-right diagonal (values r*32 + c, 0..1023),
//      indexing a 32-WIDE buffer (coefficients.c:364).
//   3. The fwd wrapper COMPACTS in place: rows 1..31 memcpy'd from row*64
//      to row*32 (svt_handle_transform64x64_N2_N4_c, transforms.c:2700-2707,
//      called at transforms.c:2916) - the quantize/token coeff buffer at
//      TX_64X64 IS the 32-wide compacted top-left quadrant.
//   4. The inverse takes the 32-WIDE 1024-entry input and remaps it into the
//      zero-padded 64x64 internally (svt_av1_inv_txfm2d_add_64x64_c,
//      inv_transforms.c:2615-2628, the verbatim comment).
//   5. The token chain folds 64 -> 32 via get_txb_bwl/wide/high
//      (common_utils.h:115-128); the nz-map LUT row aliases the 32x32 table
//      (coefficients.c:274).
// THE QC-BUFFER CONTRACT (documented here; the seam the court named):
//   the token chain at TX_64X64 consumes a 32-WIDE 1024-entry COMPACTED
//   buffer (qc32[r*32 + c] = qc64[r*64 + c], r,c < 32) with the normative
//   1024-position scan below; the quantize is n_coeffs = 1024 over the same
//   compacted buffer (NOT the 4096-wide buffer the l3 f64/b64 gate lines
//   pin - those stay as the l3 64-wide facade artifacts). Coefficients
//   outside the top-left 32x32 are never quantized nor coded (the decoder
//   zero-fills them: the recon contract below); the recon path expands the
//   compacted dqcoeff into a 64-wide zeroed array before the l3 inv64 -
//   reproducing svt_av1_inv_txfm2d_add_64x64_c's internal remap exactly.
// This drive IS the settle: it runs the vendored quantize_fp_helper_c +
// svt_av1_idct64_new on the compacted domain and the fs264_recon gate line
// pins the result; FS5's real-decoder decode gives the final content-1:1
// proof.
static void svtd_default_scan_64x64_token(int16_t* scan) {
    // svt_aom_init_iscan at TX_64X64 (coefficients.c:331-337, 349-363):
    // n = 1024, W = H = tx_size_wide/high capped at 32 -> the same
    // up-right diagonal formula as the 32x32 grid.
    const int W = 32, H = 32;
    int idx = 0;
    for (int d = 0; d < W + H - 1; ++d) {
        const int rlo  = (d - (W - 1)) > 0 ? (d - (W - 1)) : 0;
        const int rhi  = d < (H - 1) ? d : (H - 1);
        int       incr = (H > W) ? 1 : (W > H) ? 0 : (d & 1);
        if (incr) {
            for (int r = rlo; r <= rhi; ++r) {
                scan[idx++] = (int16_t)(r * W + (d - r));
            }
        } else {
            for (int r = rhi; r >= rlo; --r) {
                scan[idx++] = (int16_t)(r * W + (d - r));
            }
        }
    }
}

// TX_64X64 FP quantize entry (L7): quantize_fp_helper_c at n_coeffs=4096,
// log_scale 2 (av1_get_tx_scale_tab[TX_64X64] = 2, full_loop.c:22).
static void svtd_quantize_fp_64x64(const TranLow* coeff, const SvtdQuantTables* t, const int16_t* scan,
                                   TranLow* qcoeff, TranLow* dqcoeff, uint16_t* eob) {
    quantize_fp_helper_c(coeff, 4096, t->zbin, t->round_fp, t->quant_fp, t->quant_shift, qcoeff,
                         dqcoeff, t->dequant, eob, scan, NULL, NULL, NULL, 2);
}

// FS2: the EMISSION quantize at TX_64X64 - the vendored call shape:
// n_coeffs = av1_get_max_eob(TX_64X64) = 1024 (full_loop.c:1262 +
// inv_transforms.h:129-137) over the COMPACTED 32-wide buffer with the
// normative 32x32-grid scan (coefficients.c:331-337, 364). The l3
// svtd_quantize_fp_64x64 above stays the 4096-wide facade artifact.
static void svtd_quantize_fp_64x64_token(const TranLow* coeff, const SvtdQuantTables* t,
                                         const int16_t* scan, TranLow* qcoeff, TranLow* dqcoeff,
                                         uint16_t* eob) {
    quantize_fp_helper_c(coeff, 1024, t->zbin, t->round_fp, t->quant_fp, t->quant_shift, qcoeff,
                         dqcoeff, t->dequant, eob, scan, NULL, NULL, NULL, 2);
}

// TX_64X64 B quantize entry: svt_aom_quantize_b_c at log_scale 2.
static void svtd_quantize_b_64x64(const TranLow* coeff, const SvtdQuantTables* t, const int16_t* scan,
                                  TranLow* qcoeff, TranLow* dqcoeff, uint16_t* eob) {
    svt_aom_quantize_b_c(coeff, 4096, t->zbin, t->round, t->quant, t->quant_shift, qcoeff, dqcoeff,
                         t->dequant, eob, scan, NULL, NULL, NULL, 2);
}

// ---- CH3: chroma frame-policy composition (4:2:0) --------------------------
// UV plane 32x32 (the 4:2:0 box average ((sum+2)>>2) of a 64x64 luma fixture
// with rows 0-31 = ramp x+y+1 and rows 32-63 = 0 -> UV rows 0-15 =
// 2*(i+j+2), rows 16-31 = 0), 2x2 grid of 16x16 UV blocks. SVT chroma
// dispatch: candidate uv_mode folds to the luma primitive set via g_uv2y
// (get_uv_mode, common_utils.h:130-133) and the builder never sees FI
// (enc_intra_prediction.c:641). The D2 policy scores the 13 folded
// candidates (uv modes 0..12; UV_CFL_PRED is NOT a candidate - the
// mode-decision CFL combine is out of scope, its prediction-surface fold is
// DC and DC_PRED is a candidate; policy named). Neighbor modes stored as UV
// modes (numerically the folded values, so the smooth check in the shim
// matches svt_aom_is_smooth's uv_mode set). REAL recon top-right gather.
static void svtd_frame_chroma_auto_16x16_blocks(const uint8_t* src, uint8_t* recon, int32_t* coeffs,
                                                int* modes) {
    const int fstride = 32;
    const int gridW = 2, gridH = 2;
    const int bsz = 16;
    memset(recon, 0, 1024);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * bsz, py = by * bsz;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? bsz : 0;
            const int nLeft = hasLeft ? bsz : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? bsz : 0;
            uint8_t above[33] = {0};
            uint8_t left[33] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, fstride, px, py, bsz, nTr);
            if (hasLeft) for (int i = 0; i < bsz; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];

            // chroma filt_type source: chroma_above_mbmi / chroma_left_mbmi
            // (enc_intra_prediction.c:28-33); the smooth test on uv_mode
            // (intra_prediction.c:139-140) is numerically the luma smooth
            // set because UV_SMOOTH_* folds to SMOOTH_* (g_uv2y).
            const int aboveMode = hasTop ? modes[(by - 1) * gridW + bx] : DC_PRED;
            const int leftMode = hasLeft ? modes[by * gridW + bx - 1] : DC_PRED;
            svtd_filt_type = ((aboveMode == UV_SMOOTH_PRED || aboveMode == UV_SMOOTH_V_PRED ||
                               aboveMode == UV_SMOOTH_H_PRED) ||
                              (leftMode == UV_SMOOTH_PRED || leftMode == UV_SMOOTH_V_PRED ||
                               leftMode == UV_SMOOTH_H_PRED))
                                 ? 1
                                 : 0;

            uint8_t srcblk[256];
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    srcblk[i * bsz + j] = src[(py + i) * fstride + px + j];

            // D2 policy over the UV candidate set: uv modes 0..12 folded via
            // g_uv2y (identity on 0..12); CFL excluded as a candidate.
            uint32_t best_sad = 0;
            int mode = -1;
            for (int m = 0; m < UV_CFL_PRED; ++m) {
                uint8_t pred[256];
                svtd_call_builder_tx(pred, g_uv2y[m], 0, FILTER_INTRA_MODES, 0, above, nTop, nTr,
                                     left, nLeft, 0, al, TX_16X16);
                const uint32_t sad = svt_nxm_sad_kernel_helper_c(srcblk, bsz, pred, bsz, bsz, bsz);
                if (mode < 0 || sad < best_sad) { best_sad = sad; mode = m; }
            }
            modes[by * gridW + bx] = mode;

            uint8_t pred[256];
            svtd_call_builder_tx(pred, g_uv2y[mode], 0, FILTER_INTRA_MODES, 0, above, nTop, nTr,
                                 left, nLeft, 0, al, TX_16X16);
            int16_t res[256];
            for (int i = 0; i < 256; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
            int32_t cb[256];
            svtd_fwd2d16x16(res, bsz, cb, svt_av1_fdct16_new);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = cb[i];
            svtd_inv2dadd16x16(cb, pred, bsz, svt_av1_idct16_new);
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    recon[(py + i) * fstride + px + j] = pred[i * bsz + j];
        }
    }
}

static void svtd_frame_chroma_auto_16x16_q(const uint8_t* src, uint8_t* recon, int32_t* coeffs,
                                           int* modes, int qindex) {
    const int fstride = 32;
    const int gridW = 2, gridH = 2;
    const int bsz = 16;
    SvtdQuantTables t;
    svtd_build_quantizer_luma(qindex, &t);
    int16_t scan16[256];
    svtd_default_scan_16x16(scan16);
    memset(recon, 0, 1024);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * bsz, py = by * bsz;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? bsz : 0;
            const int nLeft = hasLeft ? bsz : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? bsz : 0;
            uint8_t above[33] = {0};
            uint8_t left[33] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, fstride, px, py, bsz, nTr);
            if (hasLeft) for (int i = 0; i < bsz; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];

            const int aboveMode = hasTop ? modes[(by - 1) * gridW + bx] : UV_DC_PRED;
            const int leftMode = hasLeft ? modes[by * gridW + bx - 1] : UV_DC_PRED;
            svtd_filt_type = ((aboveMode == UV_SMOOTH_PRED || aboveMode == UV_SMOOTH_V_PRED ||
                               aboveMode == UV_SMOOTH_H_PRED) ||
                              (leftMode == UV_SMOOTH_PRED || leftMode == UV_SMOOTH_V_PRED ||
                               leftMode == UV_SMOOTH_H_PRED))
                                 ? 1
                                 : 0;

            uint8_t srcblk[256];
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    srcblk[i * bsz + j] = src[(py + i) * fstride + px + j];

            uint32_t best_sad = 0;
            int mode = -1;
            for (int m = 0; m < UV_CFL_PRED; ++m) {
                uint8_t pred[256];
                svtd_call_builder_tx(pred, g_uv2y[m], 0, FILTER_INTRA_MODES, 0, above, nTop, nTr,
                                     left, nLeft, 0, al, TX_16X16);
                const uint32_t sad = svt_nxm_sad_kernel_helper_c(srcblk, bsz, pred, bsz, bsz, bsz);
                if (mode < 0 || sad < best_sad) { best_sad = sad; mode = m; }
            }
            modes[by * gridW + bx] = mode;

            uint8_t pred[256];
            svtd_call_builder_tx(pred, g_uv2y[mode], 0, FILTER_INTRA_MODES, 0, above, nTop, nTr,
                                 left, nLeft, 0, al, TX_16X16);
            int16_t res[256];
            for (int i = 0; i < 256; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
            int32_t cb[256];
            svtd_fwd2d16x16(res, bsz, cb, svt_av1_fdct16_new);
            TranLow qc[256], dq[256];
            uint16_t eob = 0;
            svtd_quantize_fp_16x16(cb, &t, scan16, qc, dq, &eob);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = qc[i];
            svtd_inv2dadd16x16(dq, pred, bsz, svt_av1_idct16_new);
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    recon[(py + i) * fstride + px + j] = pred[i * bsz + j];
        }
    }
}

// Forced-mode (UV_V_PRED + DCT) chroma frame composition.
static void svtd_frame_chroma_v_dct_16x16(const uint8_t* src, uint8_t* recon, int32_t* coeffs) {
    const int fstride = 32;
    const int gridW = 2, gridH = 2;
    memset(recon, 0, 1024);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16, py = by * 16;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? 16 : 0;
            const int nLeft = hasLeft ? 16 : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? 16 : 0;
            uint8_t above[33] = {0};
            uint8_t left[33] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, fstride, px, py, 16, nTr);
            if (hasLeft) for (int i = 0; i < 16; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];
            uint8_t pred[256];
            // forced UV_V_PRED folds to V_PRED (g_uv2y[UV_V_PRED] = V_PRED)
            svtd_call_builder_tx(pred, g_uv2y[UV_V_PRED], 0, FILTER_INTRA_MODES, 0, above, nTop,
                                 nTr, left, nLeft, 0, al, TX_16X16);
            int16_t res[256];
            for (int i = 0; i < 256; ++i)
                res[i] = (int16_t)(src[(py + (i >> 4)) * fstride + px + (i & 15)] - pred[i]);
            int32_t cb[256];
            svtd_fwd2d16x16(res, 16, cb, svt_av1_fdct16_new);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = cb[i];
            svtd_inv2dadd16x16(cb, pred, 16, svt_av1_idct16_new);
            for (int i = 0; i < 256; ++i)
                recon[(py + (i >> 4)) * fstride + px + (i & 15)] = pred[i];
        }
    }
}

// Q variant of the forced-mode chroma composition.
static void svtd_frame_chroma_v_dct_16x16_q(const uint8_t* src, uint8_t* recon, int32_t* coeffs,
                                            int qindex) {
    const int fstride = 32;
    const int gridW = 2, gridH = 2;
    SvtdQuantTables t;
    svtd_build_quantizer_luma(qindex, &t);
    int16_t scan16[256];
    svtd_default_scan_16x16(scan16);
    memset(recon, 0, 1024);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 16, py = by * 16;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? 16 : 0;
            const int nLeft = hasLeft ? 16 : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? 16 : 0;
            uint8_t above[33] = {0};
            uint8_t left[33] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, fstride, px, py, 16, nTr);
            if (hasLeft) for (int i = 0; i < 16; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];
            uint8_t pred[256];
            svtd_call_builder_tx(pred, g_uv2y[UV_V_PRED], 0, FILTER_INTRA_MODES, 0, above, nTop,
                                 nTr, left, nLeft, 0, al, TX_16X16);
            int16_t res[256];
            for (int i = 0; i < 256; ++i)
                res[i] = (int16_t)(src[(py + (i >> 4)) * fstride + px + (i & 15)] - pred[i]);
            int32_t cb[256];
            svtd_fwd2d16x16(res, 16, cb, svt_av1_fdct16_new);
            TranLow qc[256], dq[256];
            uint16_t eob = 0;
            svtd_quantize_fp_16x16(cb, &t, scan16, qc, dq, &eob);
            for (int i = 0; i < 256; ++i) coeffs[(by * gridW + bx) * 256 + i] = qc[i];
            svtd_inv2dadd16x16(dq, pred, 16, svt_av1_idct16_new);
            for (int i = 0; i < 256; ++i)
                recon[(py + (i >> 4)) * fstride + px + (i & 15)] = pred[i];
        }
    }
}

// ---- L7: 64x64 frame-policy composition (2x2 grid of 64x64 = 128x128) -----
// Same D2 policy, TX_64X64 column; REAL recon top-right gather via
// svtd_gather_above (fstride 128). Rows 64-127 of the frame are zero.
static void svtd_frame_auto_64x64_blocks(const uint8_t* src, uint8_t* recon, int32_t* coeffs, int* modes) {
    const int fstride = 128;
    const int gridW = 2, gridH = 2;
    const int bsz = 64;
    memset(recon, 0, 16384);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * bsz, py = by * bsz;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? bsz : 0;
            const int nLeft = hasLeft ? bsz : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? bsz : 0;
            uint8_t above[129] = {0};
            uint8_t left[129] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, fstride, px, py, bsz, nTr);
            if (hasLeft) for (int i = 0; i < bsz; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];

            const int aboveMode = hasTop ? modes[(by - 1) * gridW + bx] : DC_PRED;
            const int leftMode = hasLeft ? modes[by * gridW + bx - 1] : DC_PRED;
            svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED ||
                               aboveMode == SMOOTH_H_PRED) ||
                              (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED ||
                               leftMode == SMOOTH_H_PRED))
                                 ? 1
                                 : 0;

            uint8_t srcblk[4096];
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    srcblk[i * bsz + j] = src[(py + i) * fstride + px + j];

            uint32_t best_sad = 0;
            int mode = -1;
            for (int m = 0; m <= PAETH_PRED; ++m) {
                uint8_t pred[4096];
                svtd_call_builder_tx(pred, m, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                     0, al, TX_64X64);
                const uint32_t sad = svt_nxm_sad_kernel_helper_c(srcblk, bsz, pred, bsz, bsz, bsz);
                if (mode < 0 || sad < best_sad) { best_sad = sad; mode = m; }
            }
            modes[by * gridW + bx] = mode;

            uint8_t pred[4096];
            svtd_call_builder_tx(pred, mode, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                 0, al, TX_64X64);
            int16_t res[4096];
            for (int i = 0; i < 4096; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
            int32_t cb[4096];
            svtd_fwd2d64x64(res, bsz, cb, svt_av1_fdct64_new);
            for (int i = 0; i < 4096; ++i) coeffs[(by * gridW + bx) * 4096 + i] = cb[i];
            svtd_inv2dadd64x64(cb, pred, bsz, svt_av1_idct64_new);
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    recon[(py + i) * fstride + px + j] = pred[i * bsz + j];
        }
    }
}

// Q variant (log_scale 2).
static void svtd_frame_auto_64x64_q(const uint8_t* src, uint8_t* recon, int32_t* coeffs, int* modes,
                                    int qindex) {
    const int gridW = 2, gridH = 2;
    const int bsz = 64;
    SvtdQuantTables t;
    svtd_build_quantizer_luma(qindex, &t);
    int16_t scan64[4096];
    svtd_default_scan_64x64(scan64);
    memset(recon, 0, 16384);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * bsz, py = by * bsz;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? bsz : 0;
            const int nLeft = hasLeft ? bsz : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? bsz : 0;
            uint8_t above[129] = {0};
            uint8_t left[129] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, 128, px, py, bsz, nTr);
            if (hasLeft) for (int i = 0; i < bsz; ++i) left[i] = recon[(py + i) * 128 + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * 128 + px - 1];

            const int aboveMode = hasTop ? modes[(by - 1) * gridW + bx] : DC_PRED;
            const int leftMode = hasLeft ? modes[by * gridW + bx - 1] : DC_PRED;
            svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED ||
                               aboveMode == SMOOTH_H_PRED) ||
                              (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED ||
                               leftMode == SMOOTH_H_PRED))
                                 ? 1
                                 : 0;

            uint8_t srcblk[4096];
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    srcblk[i * bsz + j] = src[(py + i) * 128 + px + j];

            uint32_t best_sad = 0;
            int mode = -1;
            for (int m = 0; m <= PAETH_PRED; ++m) {
                uint8_t pred[4096];
                svtd_call_builder_tx(pred, m, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                     0, al, TX_64X64);
                const uint32_t sad = svt_nxm_sad_kernel_helper_c(srcblk, bsz, pred, bsz, bsz, bsz);
                if (mode < 0 || sad < best_sad) { best_sad = sad; mode = m; }
            }
            modes[by * gridW + bx] = mode;

            uint8_t pred[4096];
            svtd_call_builder_tx(pred, mode, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft,
                                 0, al, TX_64X64);
            int16_t res[4096];
            for (int i = 0; i < 4096; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
            int32_t cb[4096];
            svtd_fwd2d64x64(res, bsz, cb, svt_av1_fdct64_new);
            TranLow qc[4096], dq[4096];
            uint16_t eob = 0;
            svtd_quantize_fp_64x64(cb, &t, scan64, qc, dq, &eob);
            for (int i = 0; i < 4096; ++i) coeffs[(by * gridW + bx) * 4096 + i] = qc[i];
            svtd_inv2dadd64x64(dq, pred, bsz, svt_av1_idct64_new);
            for (int i = 0; i < bsz; ++i)
                for (int j = 0; j < bsz; ++j)
                    recon[(py + i) * 128 + px + j] = pred[i * bsz + j];
        }
    }
}

// Forced-mode (V_PRED + DCT) 64x64 frame composition (HK3 pattern).
static void svtd_frame_v_dct_64x64(const uint8_t* src, uint8_t* recon, int32_t* coeffs) {
    const int gridW = 2, gridH = 2;
    memset(recon, 0, 16384);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 64, py = by * 64;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? 64 : 0;
            const int nLeft = hasLeft ? 64 : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? 64 : 0;
            uint8_t above[129] = {0};
            uint8_t left[129] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, 128, px, py, 64, nTr);
            if (hasLeft) for (int i = 0; i < 64; ++i) left[i] = recon[(py + i) * 128 + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * 128 + px - 1];
            uint8_t pred[4096];
            svtd_call_builder_tx(pred, V_PRED, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left,
                                 nLeft, 0, al, TX_64X64);
            int16_t res[4096];
            for (int i = 0; i < 4096; ++i)
                res[i] = (int16_t)(src[(py + (i >> 6)) * 128 + px + (i & 63)] - pred[i]);
            int32_t cb[4096];
            svtd_fwd2d64x64(res, 64, cb, svt_av1_fdct64_new);
            for (int i = 0; i < 4096; ++i) coeffs[(by * gridW + bx) * 4096 + i] = cb[i];
            svtd_inv2dadd64x64(cb, pred, 64, svt_av1_idct64_new);
            for (int i = 0; i < 4096; ++i)
                recon[(py + (i >> 6)) * 128 + px + (i & 63)] = pred[i];
        }
    }
}

// Q variant of forced-mode (log_scale 2).
static void svtd_frame_v_dct_64x64_q(const uint8_t* src, uint8_t* recon, int32_t* coeffs, int qindex) {
    const int gridW = 2, gridH = 2;
    SvtdQuantTables t;
    svtd_build_quantizer_luma(qindex, &t);
    int16_t scan64[4096];
    svtd_default_scan_64x64(scan64);
    memset(recon, 0, 16384);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 64, py = by * 64;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? 64 : 0;
            const int nLeft = hasLeft ? 64 : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? 64 : 0;
            uint8_t above[129] = {0};
            uint8_t left[129] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, 128, px, py, 64, nTr);
            if (hasLeft) for (int i = 0; i < 64; ++i) left[i] = recon[(py + i) * 128 + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * 128 + px - 1];
            uint8_t pred[4096];
            svtd_call_builder_tx(pred, V_PRED, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left,
                                 nLeft, 0, al, TX_64X64);
            int16_t res[4096];
            for (int i = 0; i < 4096; ++i)
                res[i] = (int16_t)(src[(py + (i >> 6)) * 128 + px + (i & 63)] - pred[i]);
            int32_t cb[4096];
            svtd_fwd2d64x64(res, 64, cb, svt_av1_fdct64_new);
            TranLow qc[4096], dq[4096];
            uint16_t eob = 0;
            svtd_quantize_fp_64x64(cb, &t, scan64, qc, dq, &eob);
            for (int i = 0; i < 4096; ++i) coeffs[(by * gridW + bx) * 4096 + i] = qc[i];
            svtd_inv2dadd64x64(dq, pred, 64, svt_av1_idct64_new);
            for (int i = 0; i < 4096; ++i)
                recon[(py + (i >> 6)) * 128 + px + (i & 63)] = pred[i];
        }
    }
}

// ---- gen_inv_stage_range 8x8 gate line -------------------------------------
// Mirrors svt_av1_gen_inv_stage_range (inv_transforms.c:44) for TX_8X8 at
// bd=8, DCT_DCT and ADST_ADST, to settle the stage_range shim. Prints the
// per-stage clamp bits for row and col passes.
static void svtd_gen_inv_range_8x8(void) {
    // av1_txfm_stage_num_list (inv_transforms.h:197): DCT8=6, ADST8=8
    // av1_txfm_type_ls[1] (inv_transforms.h:193): DCT8 / ADST8
    // inv_start_range[1] (inv_transforms.h:219): 6
    // shift = inv_shift_8x8 = {-1, -4}
    const int opt_range = 16;  // bd=8
    const int stage_num_dct = 6;
    const int stage_num_adst = 8;
    printf("gen_inv_range_8x8_dct:");
    for (int i = 0; i < stage_num_dct; ++i) printf(" %d", opt_range);
    printf("\n");
    printf("gen_inv_range_8x8_adst:");
    for (int i = 0; i < stage_num_adst; ++i) printf(" %d", opt_range);
    printf("\n");
}

// Default (up-right diagonal) scan for 16x16, same svt_aom_init_iscan formula
// (coefficients.c:345-363) at W=H=16.
static void svtd_default_scan_16x16(int16_t* scan) {
    const int W = 16, H = 16;  // tx_size_wide/high[TX_16X16]
    int idx = 0;
    for (int d = 0; d < W + H - 1; ++d) {
        const int rlo  = (d - (W - 1)) > 0 ? (d - (W - 1)) : 0;
        const int rhi  = d < (H - 1) ? d : (H - 1);
        int       incr = (H > W) ? 1 : (W > H) ? 0 : (d & 1);
        if (incr) {
            for (int r = rlo; r <= rhi; ++r) {
                scan[idx++] = (int16_t)(r * W + (d - r));
            }
        } else {
            for (int r = rhi; r >= rlo; --r) {
                scan[idx++] = (int16_t)(r * W + (d - r));
            }
        }
    }
}

// ---- gen_inv_stage_range 16x16 gate line -----------------------------------
// Mirrors svt_av1_gen_inv_stage_range (inv_transforms.c:44) for TX_16X16 at
// bd=8, DCT_DCT and ADST_ADST: opt_range_row/col = 16 (:49-51),
// inv_start_range[TX_16X16] = 7 (inv_transforms.h:220), shift =
// inv_shift_16x16 = {-2, -4} (inv_transforms.c:20). Prints the per-stage
// clamp bits for row and col passes; DCT16 = 8 stages, ADST16 = 10 stages
// (av1_txfm_stage_num_list, inv_transforms.h:200, :205).
static void svtd_gen_inv_range_16x16(void) {
    const int opt_range    = 16;  // bd=8
    const int stage_num_dct  = 8;
    const int stage_num_adst = 10;
    printf("gen_inv_range_16x16_dct:");
    for (int i = 0; i < stage_num_dct; ++i) printf(" %d", opt_range);
    printf("\n");
    printf("gen_inv_range_16x16_adst:");
    for (int i = 0; i < stage_num_adst; ++i) printf(" %d", opt_range);
    printf("\n");
}

// ---- F1 frame composition (V + DCT, 8x8) -----------------------------------
// M1 raster semantics: above iff by>0, left iff bx>0, above-left iff both,
// top-right iff by>0 && bx+1<gridW, bottom-left never. Blocks predict from
// RECONSTRUCTED neighbors only (early-out included via the verbatim builder).
static void svtd_frame_v_dct_8x8(const uint8_t* src, uint8_t* recon, int32_t* coeffs) {
    const int fstride = 8;
    const int gridW = 2, gridH = 2;
    memset(recon, 0, 64);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 4, py = by * 4;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? 4 : 0;
            const int nLeft = hasLeft ? 4 : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? 4 : 0;
            uint8_t above[9] = {0};
            uint8_t left[9] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, fstride, px, py, 4, nTr);
            if (hasLeft) for (int i = 0; i < 4; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];
            uint8_t pred[16];
            svtd_call_builder(pred, V_PRED, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft, 0, al);
            int16_t res[16];
            for (int i = 0; i < 16; ++i)
                res[i] = (int16_t)(src[(py + (i >> 2)) * fstride + px + (i & 3)] - pred[i]);
            int32_t cb[16];
            svtd_fwd2d4x4(res, 4, cb, svt_av1_fdct4_new);
            for (int i = 0; i < 16; ++i) coeffs[(by * gridW + bx) * 16 + i] = cb[i];
            svtd_inv2dadd4x4(cb, pred, 4, svt_av1_idct4_new);
            for (int i = 0; i < 16; ++i) recon[(py + (i >> 2)) * fstride + px + (i & 3)] = pred[i];
        }
    }
}
// ---- frame-policy composition (moved from main_frame.c so both generators emit it) ----
// SAD 4x4 between the source block and a candidate prediction, both flat
// 4-strided (the caller materializes the candidate prediction).
static uint32_t svtd_sad4x4(const uint8_t* src, const uint8_t* pred) {
    return svt_nxm_sad_kernel_helper_c(src, 4, pred, 4, 4, 4);
}

// D2 policy: pick the mode minimizing SAD; ties go to the lowest mode index.
static int svtd_decide(const uint8_t* src, const uint8_t* above, int n_top, int n_topright,
                       const uint8_t* left, int n_left, int n_bottomleft, uint8_t above_left,
                       uint32_t* best_sad_out) {
    uint32_t best_sad = 0;
    int best_mode = -1;
    for (int m = 0; m <= PAETH_PRED; ++m) {
        uint8_t pred[16];
        svtd_call_builder(pred, m, 0, FILTER_INTRA_MODES, 0, above, n_top, n_topright, left, n_left,
                          n_bottomleft, above_left);
        const uint32_t sad = svtd_sad4x4(src, pred);
        if (best_mode < 0 || sad < best_sad) {
            best_sad = sad;
            best_mode = m;
        }
    }
    *best_sad_out = best_sad;
    return best_mode;
}

static void svtd_frame_auto_8x8(const uint8_t* src, uint8_t* recon, int32_t* coeffs, int* modes) {
    const int fstride = 8;
    const int gridW = 2, gridH = 2;
    memset(recon, 0, 64);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 4, py = by * 4;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? 4 : 0;
            const int nLeft = hasLeft ? 4 : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? 4 : 0;
            uint8_t above[9] = {0};
            uint8_t left[9] = {0};
            uint8_t al = 0;
            if (hasTop) svtd_gather_above(above, recon, fstride, px, py, 4, nTr);
            if (hasLeft) for (int i = 0; i < 4; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];

            // NeighborContext for filt_type: chosen modes of the above/left
            // blocks (SMOOTH* -> 1). Unknown (first row/col) -> DC -> 0.
            const int aboveMode = hasTop ? modes[(by - 1) * gridW + bx] : DC_PRED;
            const int leftMode = hasLeft ? modes[by * gridW + bx - 1] : DC_PRED;
            svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED ||
                               aboveMode == SMOOTH_H_PRED) ||
                              (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED ||
                               leftMode == SMOOTH_H_PRED))
                                 ? 1
                                 : 0;

            const uint8_t srcblk[16] = {
                src[(py + 0) * fstride + px + 0], src[(py + 0) * fstride + px + 1],
                src[(py + 0) * fstride + px + 2], src[(py + 0) * fstride + px + 3],
                src[(py + 1) * fstride + px + 0], src[(py + 1) * fstride + px + 1],
                src[(py + 1) * fstride + px + 2], src[(py + 1) * fstride + px + 3],
                src[(py + 2) * fstride + px + 0], src[(py + 2) * fstride + px + 1],
                src[(py + 2) * fstride + px + 2], src[(py + 2) * fstride + px + 3],
                src[(py + 3) * fstride + px + 0], src[(py + 3) * fstride + px + 1],
                src[(py + 3) * fstride + px + 2], src[(py + 3) * fstride + px + 3]};

            uint32_t best_sad = 0;
            const int mode = svtd_decide(srcblk, above, nTop, nTr, left, nLeft, 0, al, &best_sad);
            modes[by * gridW + bx] = mode;

            uint8_t pred[16];
            svtd_call_builder(pred, mode, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft, 0, al);
            int16_t res[16];
            for (int i = 0; i < 16; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
            int32_t cb[16];
            svtd_fwd2d4x4(res, 4, cb, svt_av1_fdct4_new);
            for (int i = 0; i < 16; ++i) coeffs[(by * gridW + bx) * 16 + i] = cb[i];
            svtd_inv2dadd4x4(cb, pred, 4, svt_av1_idct4_new);
            for (int i = 0; i < 16; ++i) recon[(py + (i >> 2)) * fstride + px + (i & 3)] = pred[i];
        }
    }
}




// ---- ECP1 partition walk helpers (composition over the extracts) ---------
// Context derivation (entropy_coding.c:945-960 flattened): bsl from
// block_size_wide (mi_size_wide_log2 == log2(px>>2) for squares); fresh
// INVALID_NEIGHBOR_DATA (0xFF, definitions.h:334) cells map to 0.
typedef struct EcPartState {
    AomWriter* w;
    AomCdfProb (*cdf)[CDF_SIZE(EXT_PARTITION_TYPES)];
    uint8_t* above;  // per mi column
    uint8_t* left;   // per mi row, (miRow & 15)
    int frame_mi;    // frame extent in mi units
    int aligned_px;  // aligned frame extent in px
    int nctx;        // coded partition symbol count
} EcPartState;

static int ecpart_bsl(BlockSize bsize) {
    return svt_log2f(block_size_wide[bsize] >> 2) - 1;
}

static int ecpart_derive_ctx(const uint8_t* above, const uint8_t* left, int miRow, int miCol, BlockSize bsize) {
    const uint8_t ab = above[miCol];
    const uint8_t lf = left[miRow & 15];
    const int a = ((ab == (uint8_t)INVALID_NEIGHBOR_DATA ? 0 : ab) >> ecpart_bsl(bsize)) & 1;
    const int l = ((lf == (uint8_t)INVALID_NEIGHBOR_DATA ? 0 : lf) >> ecpart_bsl(bsize)) & 1;
    return (l * 2 + a) + ecpart_bsl(bsize) * PARTITION_PLOFFSET;
}

// coding_loop.c:1700-1713: each CODED block writes
// partition_context_lookup[bsize] over its mi extent (above bytes at
// mi_col.., left bytes at (mi_row & 15)..).
static void ecpart_update_ctx(uint8_t* above, uint8_t* left, int miRow, int miCol, BlockSize bsize) {
    const int mi_w = block_size_wide[bsize] >> 2;
    const int mi_h = block_size_high[bsize] >> 2;
    for (int j = 0; j < mi_w; ++j) above[miCol + j] = (uint8_t)partition_context_lookup[bsize].above;
    for (int i = 0; i < mi_h; ++i) left[(miRow + i) & 15] = (uint8_t)partition_context_lookup[bsize].left;
}

// Ratified structural tree: 32x32 frame inside sb_size 64 - SPLIT at 64x64
// (forced, no symbol) and 32x32 (coded), NONE at the four 16x16 leaves.
static int ecpart_decide(int miRow, int miCol, BlockSize bsize) {
    (void)miRow;
    (void)miCol;
    return (bsize == BLOCK_64X64 || bsize == BLOCK_32X32) ? PARTITION_SPLIT : PARTITION_NONE;
}

static BlockSize ecpart_child(BlockSize bsize) {
    switch (bsize) {
        case BLOCK_128X128: return BLOCK_64X64;
        case BLOCK_64X64: return BLOCK_32X32;
        case BLOCK_32X32: return BLOCK_16X16;
        case BLOCK_16X16: return BLOCK_8X8;
        default: return BLOCK_4X4;
    }
}

// Writer walk (encode_partition_av1, entropy_coding.c:932-981): has_rows/
// has_cols from the px rule (:941-943), forced split with NO symbol
// (:962-965), full symbol when both edges (:967-969, alphabet per
// svt_aom_partition_cdf_length :922-930), gathered 2-symbol on XOR edges
// (:970-977, unreachable in the structural tree, exercised by ecpart_gather).
static void ecpart_write(EcPartState* s, int miRow, int miCol, BlockSize bsize) {
    if (miRow >= s->frame_mi || miCol >= s->frame_mi) return;
    const int hbs = block_size_wide[bsize] >> 1;
    const int has_rows = (miRow * 4 + hbs) < s->aligned_px;
    const int has_cols = (miCol * 4 + hbs) < s->aligned_px;
    const int decided = ecpart_decide(miRow, miCol, bsize);
    if (!has_rows && !has_cols) {
        if (decided != PARTITION_SPLIT) { fprintf(stderr, "ECP1 forced-split mismatch\n"); exit(1); }
    } else {
        const int ctx = ecpart_derive_ctx(s->above, s->left, miRow, miCol, bsize);
        printf(" %d", ctx);
        if (has_rows && has_cols) {
            aom_write_symbol(s->w, decided, s->cdf[ctx], svt_aom_partition_cdf_length(bsize));
        } else if (!has_rows && has_cols) {
            AomCdfProb g[CDF_SIZE(2)];
            partition_gather_vert_alike(g, s->cdf[ctx], bsize);
            aom_write_symbol(s->w, decided == PARTITION_SPLIT, g, 2);
        } else {
            AomCdfProb g[CDF_SIZE(2)];
            partition_gather_horz_alike(g, s->cdf[ctx], bsize);
            aom_write_symbol(s->w, decided == PARTITION_SPLIT, g, 2);
        }
        s->nctx++;
        if (decided != PARTITION_SPLIT) {
            ecpart_update_ctx(s->above, s->left, miRow, miCol, bsize);
            return;
        }
    }
    const BlockSize sub = ecpart_child(bsize);
    const int stepMi = block_size_wide[bsize] >> 3;
    ecpart_write(s, miRow, miCol, sub);
    ecpart_write(s, miRow, miCol + stepMi, sub);
    ecpart_write(s, miRow + stepMi, miCol, sub);
    ecpart_write(s, miRow + stepMi, miCol + stepMi, sub);
}

// Reader twin (aom read_partition decodeframe.c:1266-1293 semantics, the
// out-of-tree BSF4 arbiter; persistent-cdf branch adapts via
// aom_read_symbol_, gathered branch reads the temporary via aom_read_cdf_
// exactly like the aom decoder - the writer's gathered adaptation is
// discarded with the temporary, entropy_coding.c:971-977).
typedef struct EcPartRState {
    aom_reader* r;
    AomCdfProb (*cdf)[CDF_SIZE(EXT_PARTITION_TYPES)];
    uint8_t* above;
    uint8_t* left;
    int frame_mi;
    int aligned_px;
    int* rt;   // decoded partition values in coded order
    int nrt;
    int bad;
} EcPartRState;

static void ecpart_read(EcPartRState* s, int miRow, int miCol, BlockSize bsize) {
    if (miRow >= s->frame_mi || miCol >= s->frame_mi) return;
    const int hbs = block_size_wide[bsize] >> 1;
    const int has_rows = (miRow * 4 + hbs) < s->aligned_px;
    const int has_cols = (miCol * 4 + hbs) < s->aligned_px;
    if (!has_rows && !has_cols) {
        // forced SPLIT, no symbol
    } else {
        const int ctx = ecpart_derive_ctx(s->above, s->left, miRow, miCol, bsize);
        int p;
        if (has_rows && has_cols) {
            p = aom_read_symbol_(s->r, s->cdf[ctx], svt_aom_partition_cdf_length(bsize));
        } else if (!has_rows && has_cols) {
            AomCdfProb g[CDF_SIZE(2)];
            partition_gather_vert_alike(g, s->cdf[ctx], bsize);
            p = aom_read_cdf_(s->r, g, 2) ? PARTITION_SPLIT : PARTITION_HORZ;
        } else {
            AomCdfProb g[CDF_SIZE(2)];
            partition_gather_horz_alike(g, s->cdf[ctx], bsize);
            p = aom_read_cdf_(s->r, g, 2) ? PARTITION_SPLIT : PARTITION_VERT;
        }
        printf(" %d", p);
        if (p != ecpart_decide(miRow, miCol, bsize)) s->bad = 1;
        s->rt[s->nrt++] = p;
        if (p != PARTITION_SPLIT) {
            ecpart_update_ctx(s->above, s->left, miRow, miCol, bsize);
            return;
        }
    }
    const BlockSize sub = ecpart_child(bsize);
    const int stepMi = block_size_wide[bsize] >> 3;
    ecpart_read(s, miRow, miCol, sub);
    ecpart_read(s, miRow, miCol + stepMi, sub);
    ecpart_read(s, miRow + stepMi, miCol, sub);
    ecpart_read(s, miRow + stepMi, miCol + stepMi, sub);
}

// ---- BSF3: structural keyframe assembly -----------------------------------
// SVT packer structure + court-ratified D1 mono patch. Every field value is
// the ratified BSF0(g) config: profile 0, still_picture=1,
// reduced_still_picture_header=0, monochrome=1 (D1), use_128x128=0 (sb 64),
// enable_filter_intra=1, enable_intra_edge_filter=1, enable_order_hint=0
// (D3), seq_force_screen_content_tools=2 / seq_force_integer_mv=2 (choose
// bits written), seq_level_idx=0 (level 2.0), 32x32 frame == max dims,
// single tile, base_q_idx 0 -> all_lossless = coded_lossless = 1.
//
// THE D1 MONO PATCH (court-ratified; exact spans in the pinned
// write_color_config / encode_quantization):
//   span 1 - entropy_coding.c:2689  const int is_monochrome = 0 -> 1;
//   span 2 - entropy_coding.c:2706-2710  the commented-out spec mono branch
//            becomes live (color_range bit, then return - skipping the
//            4:2:0 subsampling writes :2720-2745 and separate_uv_delta_q
//            :2747-2751, which the spec derives to 0 for mono);
//   span 3 - entropy_coding.c:2385-2386  encode_quantization writes the U
//            delta_q pair UNCONDITIONALLY; the spec's num_planes guard
//            (aom setup_quantization is num_planes-aware,
//            decodeframe.c:5121-5122) skips them for mono - the pinned
//            writer lacks the guard because mono is unreachable there.
//            Required by the ratified 21-bit frame-header walk. NOT a
//            commented branch - named explicitly for the court audit.
// No other writer behavior changes.

// write_sequence_header (entropy_coding.c:2754-2839), ratified values.
static void svtd_bsf3_sequence_header(AomWriteBitBuffer* wb, int max_dim) {
    // max_dim is a power of two: frame_width_bits = svt_log2f(max_dim), no
    // bump (:2757-2764); the >= 1 guard (:2766-2771) no-op. 32 -> bits 5
    // (verbatim 32x32 walk); 16 -> bits 4 (TD0 16x16-frame rungs).
    const int bits = svt_log2f(max_dim);
    svt_aom_wb_write_literal(wb, bits - 1, 4);        // frame_width_bits - 1 (:2775)
    svt_aom_wb_write_literal(wb, bits - 1, 4);        // frame_height_bits - 1 (:2776)
    svt_aom_wb_write_literal(wb, max_dim - 1, bits);  // max_frame_width - 1 (:2777)
    svt_aom_wb_write_literal(wb, max_dim - 1, bits);  // max_frame_height - 1 (:2778)
    if (1) {  // !reduced_still_picture_header (:2780)
        svt_aom_wb_write_bit(wb, 0);  // frame_id_numbers_present_flag (:2784; sequence_control_set.c:85)
    }
    svt_aom_wb_write_bit(wb, 0);  // sb_size == BLOCK_128X128 ? 1 : 0 (:2795; enc_handle.c:4072-4090)
    svt_aom_wb_write_bit(wb, 1);  // filter_intra_level (:2797; ratified BSF0(g))
    svt_aom_wb_write_bit(wb, 1);  // enable_intra_edge_filter (:2798; enc_mode_config.c:2877)
    if (1) {  // !reduced (:2800)
        svt_aom_wb_write_bit(wb, 0);  // enable_interintra_compound (:2801)
        svt_aom_wb_write_bit(wb, 0);  // enable_masked_compound (:2802)
        svt_aom_wb_write_bit(wb, 0);  // enable_warped_motion (:2804)
        svt_aom_wb_write_bit(wb, 0);  // enable_dual_filter (:2805; sequence_control_set.c:91)
        svt_aom_wb_write_bit(wb, 0);  // enable_order_hint = 0 (D3; SVT default 1, sequence_control_set.c:104; :2807)
        // :2809-2812 skipped (order hint off); :2831-2833 skipped (order_hint_bits)
        svt_aom_wb_write_bit(wb, 1);  // seq_force_screen_content_tools == 2 -> choose bit 1 (:2814-2815)
        svt_aom_wb_write_bit(wb, 1);  // seq_force_integer_mv == 2 -> choose bit 1 (:2821-2823)
    }
    svt_aom_wb_write_bit(wb, 0);  // enable_superres (:2836; enc_mode_config.c:2824)
    svt_aom_wb_write_bit(wb, 0);  // cdef_level (:2837)
    svt_aom_wb_write_bit(wb, 0);  // enable_restoration (:2838)
}

// write_color_config (entropy_coding.c:2687-2752) + D1 spans 1 and 2.
static void svtd_bsf3_color_config(AomWriteBitBuffer* wb) {
    svt_aom_wb_write_bit(wb, 0);  // high_bitdepth: 8-bit (:2676-2679)
    // monochrome bit: profile != HIGH_PROFILE -> bit written (:2691-2692).
    // D1 span 1: :2689 const is_monochrome = 0 -> ratified 1.
    svt_aom_wb_write_bit(wb, 1);  // is_monochrome = 1 (D1)
    svt_aom_wb_write_bit(wb, 0);  // color_description_present (:2696-2699; CP/TC/MC unspecified)
    // D1 span 2: the commented-out mono branch (:2706-2710) live:
    svt_aom_wb_write_bit(wb, 1);  // color_range = 1 (court-ratified full range; :2708)
    return;                       // mono return (:2709): skips subsampling
                                  // (:2720-2745) and separate_uv_delta_q
                                  // (:2747-2751; spec derives 0 for mono)
}

// write_sequence_header_obu (:3699-3763), ratified reduced=0 path; max_dim
// parameterizes the frame-size literals (32 verbatim; 16 for TD0 rungs).
static uint32_t svtd_bsf3_sps_payload(AomWriteBitBuffer* wb, int max_dim) {
    svt_aom_wb_write_literal(wb, 0, 3);  // profile = MAIN_PROFILE (:3705; enc_settings.c:989)
    svt_aom_wb_write_bit(wb, 1);         // still_picture (:3708)
    svt_aom_wb_write_bit(wb, 0);         // reduced_still_picture_header (:3712; ratified D2)
    if (1) {  // !reduced (:3716)
        svt_aom_wb_write_bit(wb, 0);          // timing_info_present (:3717; never set in the pinned tree)
        svt_aom_wb_write_bit(wb, 0);          // initial_display_delay_present_flag (:3726; ratified BSF0(g))
        svt_aom_wb_write_literal(wb, 0, 5);   // operating_points_cnt_minus_1 (:3728-3729)
        // op loop i = 0 (:3731-3751):
        svt_aom_wb_write_literal(wb, 0, 12);  // operating_point[0].op_idc (:3732)
        svt_aom_wb_write_literal(wb, 0, 5);   // seq_level_idx = 0 (level 2.0: 32x32@30 matches the 512x288@30 slot :121-129; major_minor_to_seq_level_idx entropy_coding.h:81-84; :3733)
        // tier skipped: level major 2 <= 3 (:3734-3736); decoder model skipped
        // (:3737-3743); initial_display_delay skipped (:3744-3750)
    }
    svtd_bsf3_sequence_header(wb, max_dim);
    svtd_bsf3_color_config(wb);
    svt_aom_wb_write_bit(wb, 0);  // film_grain_params_present (:3757; enc_handle.c:4449)
    add_trailing_bits(wb);        // (:3759)
    return svt_aom_wb_bytes_written(wb);
}

// write_uncompressed_header_obu (:3294-3637), ratified structural KF walk
// (BSF0(b): 21 bits).
static void svtd_bsf3_frame_header(AomWriteBitBuffer* wb) {
    svt_aom_wb_write_bit(wb, 0);         // show_existing_frame (:3333)
    svt_aom_wb_write_literal(wb, 0, 2);  // frame_type = KEY_FRAME (:3336)
    svt_aom_wb_write_bit(wb, 1);         // show_frame (:3338)
    // showable_frame skipped (show_frame = 1, :3340-3342); error_resilient
    // skipped (KEY_FRAME && show_frame, :3343-3347)
    svt_aom_wb_write_bit(wb, 0);  // disable_cdf_update (:3350; BSF4-fix: SVT keyframe default 0, resource_coordination_process.c:360 - the ec coupling ec_process.c:101 allow_update_cdf = !disable_cdf_update requires 0 for the adapted emission)
    svt_aom_wb_write_bit(wb, 0);  // allow_screen_content_tools - bit IS written (force == 2, :3352-3353)
    // force_integer_mv skipped (asc = 0, :3358-3366)
    svt_aom_wb_write_bit(wb, 0);  // frame_size_override_flag (:3386; frame == max dims, :3368-3371)
    // order hint skipped (enable_order_hint = 0, :3389-3391); primary_ref
    // skipped (intra-only, :3393-3395); refresh mask skipped (KF && show, :3399-3402)
    // write_frame_size (:3470 -> :2652-2669): w/h literals skipped (override
    // = 0); superres skipped ENTIRELY (enable_superres = 0 -> no bit,
    // :2636-2639); render size:
    svt_aom_wb_write_bit(wb, 0);  // render_and_frame_size_different (:2616-2624; frame_resize_enabled = 0)
    // allow_intrabc skipped (asc = 0, :3472-3474)
    // might_bwd_adapt = !reduced && !disable_cdf_update = 1 (:3548) -> ONE
    // bit (BSF4-fix: the region is now LIVE): refresh_frame_context ==
    // REFRESH_FRAME_CONTEXT_DISABLED = 1 (default resource_coordination_
    // process.c:381; no other writer assignment - :3550 is the
    // large_scale_tile path, grep-verified)
    svt_aom_wb_write_bit(wb, 1);  // refresh_frame_context == DISABLED (:3553)
    // write_tile_info (:3556 -> :2581-2614): single tile, sb 64 vs 32x32
    // frame -> sbCols = sbRows = 1 -> log2_tile_cols = log2_tile_rows = 0 =
    // min = max -> increment/terminator bits = 0 (:2410-2425);
    // context_update_tile_id + tile_size_bytes skipped (tiles == 1, :2588-2613)
    svt_aom_wb_write_bit(wb, 1);           // uniform_tile_spacing_flag (:2405; set :2556)
    svt_aom_wb_write_literal(wb, 0, 8);    // base_q_idx (encode_quantization :2376)
    svt_aom_wb_write_bit(wb, 0);           // delta_q Y dc (write_delta_q :2365-2372)
    // D1 span 3: U/V delta_q writes (:2385-2386) SKIPPED for mono (see the
    // patch note above).
    svt_aom_wb_write_bit(wb, 0);  // using_qmatrix (:2391)
    // diff_uv_delta skipped (deltas all 0)
    svt_aom_wb_write_bit(wb, 0);  // segmentation_enabled (encode_segmentation :2255)
    // delta_q block skipped (base_q_idx == 0, :3565-3587)
    // loopfilter/cdef/restoration skipped (all_lossless, :3589-3602)
    // tx_mode skipped (coded_lossless -> ONLY_4X4, :3603-3604)
    // comp_inter_inter / skip_mode / warped skipped (intra-only, :3610-3624)
    svt_aom_wb_write_bit(wb, 1);  // reduced_tx_set (:3626; ratified)
    // global motion skipped (intra-only, :3628-3631); film grain skipped
    // (params_present = 0, :3632-3636)
}

// write_frame_header_obu (:3784-3802) with appendTrailingBits = show_existing
// = 0 (:3858): NO trailing-bits marker inside OBU_FRAME.
static uint32_t svtd_bsf3_frame_header_obu(uint8_t* dst) {
    AomWriteBitBuffer wb = {dst, 0};
    svtd_bsf3_frame_header(&wb);
    return svt_aom_wb_bytes_written(&wb);
}

// Tile data: decode-order symbols through the od_ec encoder - partition
// plane (encode_partition_av1 :932-981) interleaved with per-leaf block
// symbols in write_modes_b I_SLICE order (:4977-5113): skip
// (encode_skip_coeff_av1 :995-1000, context :983-989), kf y mode
// (encode_intra_luma_mode_kf_av1 :1026-1040), angle delta when directional,
// filter-intra where allowed (none here: modes {1,7,2,2} are not DC_PRED).
static uint32_t svtd_bsf3_tile_data(uint8_t* dst) {
    static AomCdfProb part_cdf[PARTITION_CONTEXTS][CDF_SIZE(EXT_PARTITION_TYPES)];
    memcpy(part_cdf, default_partition_cdf, sizeof(part_cdf));
    static AomCdfProb skip_cdf[SKIP_CONTEXTS][CDF_SIZE(2)];
    memcpy(skip_cdf, default_skip_cdfs, sizeof(skip_cdf));
    static AomCdfProb kf_y_cdf[KF_MODE_CONTEXTS][KF_MODE_CONTEXTS][CDF_SIZE(INTRA_MODES)];
    memcpy(kf_y_cdf, svt_aom_default_kf_y_mode_cdf, sizeof(kf_y_cdf));
    static AomCdfProb angle_cdf[DIRECTIONAL_MODES][CDF_SIZE(2 * MAX_ANGLE_DELTA + 1)];
    memcpy(angle_cdf, default_angle_delta_cdf, sizeof(angle_cdf));

    AomWriter w;
    w.ec.buf = dst;
    svt_od_ec_enc_reset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos              = 0;

    uint8_t above_pctx[8];
    uint8_t left_pctx[16];
    memset(above_pctx, (int)INVALID_NEIGHBOR_DATA, sizeof(above_pctx));
    memset(left_pctx, (int)INVALID_NEIGHBOR_DATA, sizeof(left_pctx));

    // structural keyframe: modes {1,7,2,2} (f16 fixture), all skip = 1
    static const int modes[4] = {1, 7, 2, 2};
    // leaf positions in mi units: (0,0),(0,4),(4,0),(4,4); decided
    // neighbor modes for the kf contexts (unavailable -> DC_PRED).
    static const int above_mode[4] = {-1, -1, 1, 7};
    static const int left_mode[4]  = {-1, 1, -1, 2};
    // skip contexts: above/left neighbor skip flags (all coded blocks skip)
    static const int skip_ctx[4] = {0, 1, 1, 2};

    EcPartState st = {&w, part_cdf, above_pctx, left_pctx, 8, 32, 0};
    // 64x64 @(0,0): forced SPLIT, no symbol; 32x32 @(0,0): coded SPLIT.
    // (walk structure identical to the ECP1 gate; the leaves here also emit
    // their block symbols, so the recursion is unrolled.)
    const int ctx32 = ecpart_derive_ctx(st.above, st.left, 0, 0, BLOCK_32X32);
    aom_write_symbol(&w, PARTITION_SPLIT, part_cdf[ctx32], svt_aom_partition_cdf_length(BLOCK_32X32));
    for (int b = 0; b < 4; ++b) {
        static const int lr[4] = {0, 0, 4, 4};
        static const int lc[4] = {0, 4, 0, 4};
        // partition NONE at the 16x16 leaf (ECP1-measured ctx 4)
        const int pctx = ecpart_derive_ctx(st.above, st.left, lr[b], lc[b], BLOCK_16X16);
        aom_write_symbol(&w, PARTITION_NONE, part_cdf[pctx], svt_aom_partition_cdf_length(BLOCK_16X16));
        ecpart_update_ctx(st.above, st.left, lr[b], lc[b], BLOCK_16X16);
        // skip symbol (first arithmetic-coded symbol of the block)
        aom_write_symbol(&w, 1, skip_cdf[skip_ctx[b]], 2);
        // kf y mode + angle delta
        const int top_ctx = intra_mode_context[above_mode[b] < 0 ? DC_PRED : (PredictionMode)above_mode[b]];
        const int left_ctx = intra_mode_context[left_mode[b] < 0 ? DC_PRED : (PredictionMode)left_mode[b]];
        aom_write_symbol(&w, modes[b], kf_y_cdf[top_ctx][left_ctx], INTRA_MODES);
        if (BLOCK_16X16 >= BLOCK_8X8 && av1_is_directional_mode((PredictionMode)modes[b])) {
            aom_write_symbol(&w, MAX_ANGLE_DELTA, angle_cdf[modes[b] - V_PRED],
                             2 * MAX_ANGLE_DELTA + 1);
        }
        // filter-intra: svt_aom_filter_intra_allowed(1, 16x16, 0, mode) is 0
        // for every decided mode here (none is DC_PRED) -> no symbols.
    }
    aom_stop_encode(&w);
    return w.pos;
}

// svt_aom_encode_sps_av1 (:3925-3948) structure: phase 1 measure, phase 2
// rewrite (the payload size depends on its own content; the content is
// stable across the two writes).
static uint32_t svtd_bsf3_encode_sps_dims(uint8_t* dst, int max_dim) {
    const uint32_t obu_header_size  = write_obu_header(OBU_SEQUENCE_HEADER, 0, dst);
    AomWriteBitBuffer wb            = {dst + obu_header_size, 0};
    const uint32_t obu_payload_size = svtd_bsf3_sps_payload(&wb, max_dim);
    const size_t length_field_size  = svt_aom_uleb_size_in_bytes(obu_payload_size);
    size_t coded_size;
    svt_aom_uleb_encode(obu_payload_size, sizeof(obu_payload_size), dst + obu_header_size, &coded_size);
    AomWriteBitBuffer wb2 = {dst + obu_header_size + length_field_size, 0};
    svtd_bsf3_sps_payload(&wb2, max_dim);  // phase 2 rewrite at the correct offset
    return obu_header_size + (uint32_t)length_field_size + obu_payload_size;
}

static uint32_t svtd_bsf3_encode_sps(uint8_t* dst) {
    return svtd_bsf3_encode_sps_dims(dst, 32);
}

// svt_aom_write_frame_header_av1 (:3843-3920) structure for the single-tile
// OBU_FRAME: obu_type = OBU_FRAME (:3852), uncompressed header with NO
// trailing bits (:3858), tile-group header = 0 bytes (:3770-3772), uleb
// payload size (:3876/:3892), tile data copy with no per-tile prefix at
// tile_cnt == 1 (:3902-3915).
static uint32_t svtd_bsf3_frame_obu(uint8_t* dst, const uint8_t* tile_data, uint32_t tile_size) {
    const uint32_t obu_header_size = write_obu_header(OBU_FRAME, 0, dst);
    const uint32_t frame_hdr_size  = svtd_bsf3_frame_header_obu(dst + obu_header_size);
    const uint32_t tg_hdr_size     = 0;  // single tile: write_tile_group_header writes 0 bytes
    const uint32_t obu_payload_size = frame_hdr_size + tg_hdr_size + tile_size;
    const size_t length_field_size  = svt_aom_uleb_size_in_bytes(obu_payload_size);
    size_t coded_size;
    svt_aom_uleb_encode(obu_payload_size, sizeof(obu_payload_size), dst + obu_header_size, &coded_size);
    const uint32_t write_offset = obu_header_size + (uint32_t)length_field_size;
    svtd_bsf3_frame_header_obu(dst + write_offset);  // phase 2 rewrite
    // tile data copy (:3902-3915); the caller zeroes dst so the 3 pad bits
    // after the 21-bit uncompressed header are the spec's byte_alignment
    // zero bits.
    memcpy(dst + write_offset + frame_hdr_size + tg_hdr_size, tile_data, tile_size);
    return write_offset + obu_payload_size;
}

// ---- TS4: lossy header v2 (the ratified additions) -------------------------
// write_uncompressed_header_obu (:3294-3637) lossy walk: the 22 structural
// bits with base_q_idx = 100 (VALUE change, still 8 bits), then the
// delta_q block LIVE (base_q_idx > 0, :3565-3587): delta_q_present 1 bit = 0
// (delta_lf is nested INSIDE delta_q_present - not written at 0, court-
// verified nesting), then all_lossless = 0 -> encode_loopfilter (:2290-2299):
// loop_filter_level[0] 6 bits = 0, loop_filter_level[1] 6 bits = 0 (the
// level[2]/[3] U/V pair skipped at zero AND for mono, :2296-2299),
// sharpness 3 bits = 0, mode_ref_delta_enabled 1 bit = 0 (deltas skipped),
// CDEF/restoration still skipped (seq cdef_level = 0 / enable_restoration
// = 0), tx_mode_select 1 bit = 0 = TX_MODE_LARGEST (:3603-3607),
// reduced_tx_set 1 bit = 1. Total 22 + 18 = 40 bits = exactly 5 bytes, NO
// byte_alignment padding.
static void svtd_bsf3_frame_header_v2(AomWriteBitBuffer* wb) {
    svt_aom_wb_write_bit(wb, 0);           // show_existing_frame (:3333)
    svt_aom_wb_write_literal(wb, 0, 2);    // frame_type = KEY_FRAME (:3336)
    svt_aom_wb_write_bit(wb, 1);           // show_frame (:3338)
    svt_aom_wb_write_bit(wb, 0);           // disable_cdf_update (:3350; BSF4-fix)
    svt_aom_wb_write_bit(wb, 0);           // allow_screen_content_tools (:3352-3353)
    svt_aom_wb_write_bit(wb, 0);           // frame_size_override_flag (:3386)
    svt_aom_wb_write_bit(wb, 0);           // render_and_frame_size_different (:2616-2624)
    svt_aom_wb_write_bit(wb, 1);           // refresh_frame_context == DISABLED (:3553)
    svt_aom_wb_write_bit(wb, 1);           // uniform_tile_spacing_flag (:2405)
    svt_aom_wb_write_literal(wb, 100, 8);  // base_q_idx = 100 (encode_quantization :2376)
    svt_aom_wb_write_bit(wb, 0);           // delta_q Y dc (write_delta_q :2365-2372)
    // D1 span 3: U/V delta_q writes (:2385-2386) SKIPPED for mono.
    svt_aom_wb_write_bit(wb, 0);           // using_qmatrix (:2391)
    svt_aom_wb_write_bit(wb, 0);           // segmentation_enabled (:2255)
    svt_aom_wb_write_bit(wb, 0);           // delta_q_present (:3565-3587; delta_lf nested)
    svt_aom_wb_write_literal(wb, 0, 6);    // loop_filter_level[0] (:2290-2299)
    svt_aom_wb_write_literal(wb, 0, 6);    // loop_filter_level[1] (U/V pair [2]/[3] skipped)
    svt_aom_wb_write_literal(wb, 0, 3);    // loop_filter_sharpness
    svt_aom_wb_write_bit(wb, 0);           // loop_filter_delta_enabled (deltas skipped)
    // CDEF/restoration skipped (seq cdef_level = 0 / enable_restoration = 0)
    svt_aom_wb_write_bit(wb, 0);           // tx_mode_select = 0 -> TX_MODE_LARGEST (:3603-3607)
    svt_aom_wb_write_bit(wb, 1);           // reduced_tx_set (:3626; ratified)
}

static uint32_t svtd_bsf3_frame_header_obu_v2(uint8_t* dst) {
    AomWriteBitBuffer wb = {dst, 0};
    svtd_bsf3_frame_header_v2(&wb);
    return svt_aom_wb_bytes_written(&wb);
}

static uint32_t svtd_bsf3_frame_obu_v2(uint8_t* dst, const uint8_t* tile_data, uint32_t tile_size) {
    const uint32_t obu_header_size = write_obu_header(OBU_FRAME, 0, dst);
    const uint32_t frame_hdr_size  = svtd_bsf3_frame_header_obu_v2(dst + obu_header_size);
    const uint32_t tg_hdr_size     = 0;
    const uint32_t obu_payload_size = frame_hdr_size + tg_hdr_size + tile_size;
    const size_t length_field_size  = svt_aom_uleb_size_in_bytes(obu_payload_size);
    size_t coded_size;
    svt_aom_uleb_encode(obu_payload_size, sizeof(obu_payload_size), dst + obu_header_size, &coded_size);
    const uint32_t write_offset = obu_header_size + (uint32_t)length_field_size;
    svtd_bsf3_frame_header_obu_v2(dst + write_offset);  // phase 2 rewrite
    memcpy(dst + write_offset + frame_hdr_size + tg_hdr_size, tile_data, tile_size);
    return write_offset + obu_payload_size;
}



// ---- TS1: per-TU coefficient chain (entropy_coding.c:355-544 LUMA DCT_DCT
// path) + the decoder-order read twin (aom decodetxb.c read_coeffs_txb) ----
// DCT_DCT only: tx_class = TX_CLASS_2D (tx_type_to_class[0]), no tx-type
// symbol (the :321-322 gate fails for DCT_DCT-only ports that never emit
// non-zero tx types - and at q0 it is never emitted at all).

// The TS1 slice of FRAME_CONTEXT (cabac_context_model.h:279-291): only the
// token tables the chain consumes. LUMA component (index 0) exercised; the
// arrays are ported whole (PLANE_TYPES wide) per the range rule.
typedef struct Ts1FrameContext {
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
    AomCdfProb intra_ext_tx_cdf[EXT_TX_SETS_INTRA][EXT_TX_SIZES][INTRA_MODES][CDF_SIZE(16)];
    AomCdfProb kf_y_cdf[KF_MODE_CONTEXTS][KF_MODE_CONTEXTS][CDF_SIZE(INTRA_MODES)];
    AomCdfProb angle_delta_cdf[DIRECTIONAL_MODES][CDF_SIZE(2 * MAX_ANGLE_DELTA + 1)];
} Ts1FrameContext;

// TD5a: the token-chain CDF tables are QINDEX-BUCKET-SELECTED per the spec's
// init_coeff_cdfs (07.bitstream.semantics.md:1800-1820) and the vendored
// svt_av1_default_coef_probs (cabac_context_model.c:1919-1938, via the
// verbatim get_q_ctx :1907-1918 extract in svt_gen.c). Previously this init
// copied the idx-0 bucket for every frame - the TS1 deviation named in the
// register; resolution: it WAS the q100 tile-divergence bug.
static void ts1_init(Ts1FrameContext* fc, int base_qindex) {
    const int idx = get_q_ctx(base_qindex);
    memcpy(fc->txb_skip_cdf, av1_default_txb_skip_cdfs[idx], sizeof(fc->txb_skip_cdf));
    memcpy(fc->dc_sign_cdf, av1_default_dc_sign_cdfs[idx], sizeof(fc->dc_sign_cdf));
    memcpy(fc->coeff_base_eob_cdf, av1_default_coeff_base_eob_multi_cdfs[idx], sizeof(fc->coeff_base_eob_cdf));
    memcpy(fc->coeff_base_cdf, av1_default_coeff_base_multi_cdfs[idx], sizeof(fc->coeff_base_cdf));
    memcpy(fc->coeff_br_cdf, av1_default_coeff_lps_multi_cdfs[idx], sizeof(fc->coeff_br_cdf));
    memcpy(fc->eob_extra_cdf, av1_default_eob_extra_cdfs[idx], sizeof(fc->eob_extra_cdf));
    memcpy(fc->eob_flag_cdf16, av1_default_eob_multi16_cdfs[idx], sizeof(fc->eob_flag_cdf16));
    memcpy(fc->eob_flag_cdf32, av1_default_eob_multi32_cdfs[idx], sizeof(fc->eob_flag_cdf32));
    memcpy(fc->eob_flag_cdf64, av1_default_eob_multi64_cdfs[idx], sizeof(fc->eob_flag_cdf64));
    memcpy(fc->eob_flag_cdf128, av1_default_eob_multi128_cdfs[idx], sizeof(fc->eob_flag_cdf128));
    memcpy(fc->eob_flag_cdf256, av1_default_eob_multi256_cdfs[idx], sizeof(fc->eob_flag_cdf256));
    memcpy(fc->eob_flag_cdf512, av1_default_eob_multi512_cdfs[idx], sizeof(fc->eob_flag_cdf512));
    memcpy(fc->eob_flag_cdf1024, av1_default_eob_multi1024_cdfs[idx], sizeof(fc->eob_flag_cdf1024));
    memcpy(fc->intra_ext_tx_cdf, default_intra_ext_tx_cdf, sizeof(fc->intra_ext_tx_cdf));
memcpy(fc->kf_y_cdf, svt_aom_default_kf_y_mode_cdf, sizeof(fc->kf_y_cdf));
memcpy(fc->angle_delta_cdf, default_angle_delta_cdf, sizeof(fc->angle_delta_cdf));
}

static int svtd_trace = 0;
static const char* svtd_trace_tag = "";
// TD3a adaptation-invariance probe: 1 = the production writer state
// (allow_update_cdf, adaptation live); overridable to 0 to write the rung
// tiles with static default rows and diff the bytes.
static int svtd_td0_adapt_probe = 1;
// TD3 cross-decoder script: 1 = emit S lines (tag nsymbs row-as-written
// intended-val) + T lines (tile bytes) for the out-of-tree replay harness
// (aom entdec vs dav1d msac, state-for-state).
static int svtd_script = 0;
static void svtd_script_row(const char* tag, int val, const AomCdfProb* cdf, int ns) {
    if (!svtd_script) return;
    fprintf(stderr, "S %s%s %d %d", svtd_trace_tag, tag, ns, val);
    for (int i = 0; i <= ns; ++i) fprintf(stderr, " %u", cdf[i]);
    fprintf(stderr, "\n");
}
static void svtd_script_bool(const char* tag, int val) {
    if (!svtd_script) return;
    static const AomCdfProb eqrow[3] = { AOM_CDF2(16384) };
    fprintf(stderr, "S %s%s 2 %d 16384 0 0\n", svtd_trace_tag, tag, val);
}

// FS2 (the FS3-prep gate, generator side): the verbatim av1_write_tx_type
// gate (entropy_coding.c:321-322) folded for the DCT_DCT-only port's call
// sites - intra (is_inter=0), use_reduced_set=1 (the ratified config:
// reduced_tx_set = 1 in the v2 header), q > 0 implied by the callers (the
// lossless paths never enter the chain). get_ext_tx_set_type folds from
// common_utils.h:59-77: at TX_32X32/64X64 intra the eset is DCTONLY
// (1 type) -> NO tx-type symbol; at 4x4/8x8/16x16 reduced intra -> DTT4_IDTX
// (5 types) -> the symbol (byte-identical to the previous ungated emission).
static int svtd_get_ext_tx_types(TxSize tx_size) {
    const TxSize sqr_up = txsize_sqr_up_map[tx_size];
    if (sqr_up >= TX_32X32) return av1_num_ext_tx_set[EXT_TX_SET_DCTONLY];
    return av1_num_ext_tx_set[EXT_TX_SET_DTT4_IDTX];
}
static void svtd_tr(const char* tag, int val, const OdEcEnc* enc) {
    if (!svtd_trace) return;
    if (enc) {
        fprintf(stderr, "W %s%s val=%d rng=%u low=%llu cnt=%d\n", svtd_trace_tag, tag, val,
                enc->rng, (unsigned long long)enc->low, (int)enc->cnt);
    } else {
        fprintf(stderr, "W %s%s val=%d\n", svtd_trace_tag, tag, val);
    }
}

static void svtd_write_coeffs_txb(AomWriter* w, Ts1FrameContext* fc, const TranLow* coeff,
                                  const int16_t* scan, TxSize tx_size, int eob, int txb_skip_ctx,
                                  int dc_sign_ctx, PredictionMode intra_dir) {
    const TxSize txs_ctx        = get_txsize_entropy_ctx(tx_size);
    const int    eob_multi_size = txsize_log2_minus4[tx_size];
    const int    eob_multi_ctx  = 0;  // TX_CLASS_2D

    svtd_script_row("skip", eob == 0, fc->txb_skip_cdf[txs_ctx][txb_skip_ctx], 2);
    aom_write_symbol(w, eob == 0, fc->txb_skip_cdf[txs_ctx][txb_skip_ctx], 2);
    svtd_tr("skip", eob == 0, &w->ec);
    if (svtd_trace) {
        fprintf(stderr, "W skipcdf f=%u (txs=%d ctx=%d)\n",
                fc->txb_skip_cdf[txs_ctx][txb_skip_ctx][0], (int)txs_ctx, txb_skip_ctx);
    }
    if (eob == 0) return;

    // TS3: tx-type emission (entropy_coding.c:374-376), gated by the
    // verbatim av1_write_tx_type gate (entropy_coding.c:321-322) via
    // svtd_get_ext_tx_types (common_utils.h:59-77 fold): at TX_32X32/64X64
    // intra the eset is DCTONLY (1 type) -> the symbol is NOT written.
    // The intra_dir is the caller's luma mode (av1_write_tx_type :339-342
    // folds filter-intra via fimode_to_intradir; DCT_DCT-only port has
    // filter_intra_mode == FILTER_INTRA_MODES).
    if (svtd_get_ext_tx_types(tx_size) > 1) {
        const TxSize sq = txsize_sqr_map[tx_size];
        svtd_script_row("tx", av1_ext_tx_ind[EXT_TX_SET_DTT4_IDTX][DCT_DCT],
                        fc->intra_ext_tx_cdf[2][sq][intra_dir], 5);
        aom_write_symbol(w, av1_ext_tx_ind[EXT_TX_SET_DTT4_IDTX][DCT_DCT],
                         fc->intra_ext_tx_cdf[2][sq][intra_dir], 5);
        svtd_tr("tx", av1_ext_tx_ind[EXT_TX_SET_DTT4_IDTX][DCT_DCT], &w->ec);
    }

    int eob_extra;
    const int eob_pt = get_eob_pos_token(eob, &eob_extra);
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
    svtd_script_row("eobpt", eob_pt - 1, eob_cdf, nsyms);
    aom_write_symbol(w, eob_pt - 1, eob_cdf, nsyms);
    svtd_tr("eobpt", eob_pt - 1, &w->ec);
    if (svtd_trace) {
        fprintf(stderr, "W eobcdf:");
        for (int i2 = 0; i2 < nsyms + 1; ++i2) fprintf(stderr, " %u", eob_cdf[i2]);
        fprintf(stderr, " (s=%d)\n", eob_pt - 1);
    }
    if (eob_pt > 2) {
        const int cnt = eob_pt - 3;
        const int bit = (eob_extra >> cnt) & 1;
        svtd_script_row("eobx", bit, fc->eob_extra_cdf[txs_ctx][0][cnt], 2);
        aom_write_symbol(w, bit, fc->eob_extra_cdf[txs_ctx][0][cnt], 2);
        svtd_tr("eobx", bit, &w->ec);
        aom_write_literal(w, eob_extra, cnt);
        for (int li = 0; li < cnt; ++li) {
            const int lbit = (eob_extra >> (cnt - 1 - li)) & 1;
            svtd_script_bool("eobxlit", lbit);
            svtd_tr("eobxlit", lbit, &w->ec);
        }
    }

    const int bwl    = get_txb_bwl(tx_size);
    const int width  = get_txb_wide(tx_size);
    const int height = get_txb_high(tx_size);

    uint8_t levels[TX_PAD_2D];
    memset(levels, 0, sizeof(levels));
    svt_av1_txb_init_levels_c(coeff, width, height, levels);
    int8_t coeff_contexts[TX_PAD_2D];
    svt_av1_get_nz_map_contexts_c(levels, scan, eob, tx_size, TX_CLASS_2D, coeff_contexts);

    const int32_t br_txs_ctx = AOMMIN(txs_ctx, TX_32X32);

    // backward pass
    {
        const int c   = eob - 1;
        const int pos = scan[c];
        const int coeff_ctx =
            (eob == 1) ? 0 : coeff_contexts[pos];  // get_lower_levels_ctx_eob -> 0 at scan_idx 0
        const TranLow v     = coeff[pos];
        const int32_t level = ABS(v);
        const int32_t lctx  = (eob == 1) ? get_lower_levels_ctx_eob(bwl, height, c) : coeff_ctx;
        svtd_script_row("beob", AOMMIN(level, 3) - 1, fc->coeff_base_eob_cdf[txs_ctx][0][lctx], 3);
        aom_write_symbol(w, AOMMIN(level, 3) - 1, fc->coeff_base_eob_cdf[txs_ctx][0][lctx], 3);
        svtd_tr("beob", AOMMIN(level, 3) - 1, &w->ec);
        if (level > NUM_BASE_LEVELS) {
            const int32_t base_range = level - 1 - NUM_BASE_LEVELS;
            const int16_t br_ctx     = get_br_ctx_eob(pos, bwl, TX_CLASS_2D);
            for (int32_t idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
                const int32_t k = AOMMIN(base_range - idx, BR_CDF_SIZE - 1);
                svtd_script_row("br", k, fc->coeff_br_cdf[br_txs_ctx][0][br_ctx], BR_CDF_SIZE);
                aom_write_symbol(w, k, fc->coeff_br_cdf[br_txs_ctx][0][br_ctx], BR_CDF_SIZE);
                svtd_tr("br", k, &w->ec);
                if (k < BR_CDF_SIZE - 1) break;
            }
        }
    }
    for (int c = eob - 2; c >= 0; --c) {
        const int pos       = scan[c];
        const int coeff_ctx = coeff_contexts[pos];
        const TranLow v     = coeff[pos];
        const int32_t level = ABS(v);
        if (svtd_script) fprintf(stderr, "C %sbase c=%d pos=%d ctx=%d\n", svtd_trace_tag, c, pos,
                                 coeff_ctx);
        svtd_script_row("base", AOMMIN(level, 3), fc->coeff_base_cdf[txs_ctx][0][coeff_ctx], 4);
        aom_write_symbol(w, AOMMIN(level, 3), fc->coeff_base_cdf[txs_ctx][0][coeff_ctx], 4);
        svtd_tr("base", AOMMIN(level, 3), &w->ec);
        if (level > NUM_BASE_LEVELS) {
            const int32_t base_range = level - 1 - NUM_BASE_LEVELS;
            const int16_t br_ctx     = get_br_ctx(levels, pos, bwl, TX_CLASS_2D);
            if (svtd_script)
                fprintf(stderr, "C %sbrs c=%d pos=%d brctx=%d lv=%d\n", svtd_trace_tag, c, pos,
                        br_ctx, (int)levels[get_padded_idx(pos, bwl)]);
            for (int32_t idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
                const int32_t k = AOMMIN(base_range - idx, BR_CDF_SIZE - 1);
                svtd_script_row("brs", k, fc->coeff_br_cdf[br_txs_ctx][0][br_ctx], BR_CDF_SIZE);
                aom_write_symbol(w, k, fc->coeff_br_cdf[br_txs_ctx][0][br_ctx], BR_CDF_SIZE);
                svtd_tr("brs", k, &w->ec);
                if (k < BR_CDF_SIZE - 1) break;
            }
        }
    }

    // forward pass: signs + golomb
    for (int c = 0; c < eob; ++c) {
        const int pos       = scan[c];
        const TranLow v     = coeff[pos];
        const int32_t level = ABS(v);
        if (!level) continue;
        if (c == 0) {
            svtd_script_row("sign0", (v < 0) ? 1 : 0, fc->dc_sign_cdf[0][dc_sign_ctx], 2);
            aom_write_symbol(w, (v < 0) ? 1 : 0, fc->dc_sign_cdf[0][dc_sign_ctx], 2);
            svtd_tr("sign0", (v < 0) ? 1 : 0, &w->ec);
        } else {
            aom_write_bit(w, (v < 0) ? 1 : 0);
            svtd_script_bool("sign", (v < 0) ? 1 : 0);
            svtd_tr("sign", (v < 0) ? 1 : 0, &w->ec);
        }
        if (level > COEFF_BASE_RANGE + NUM_BASE_LEVELS) {
            const int gval = level - COEFF_BASE_RANGE - 1 - NUM_BASE_LEVELS;
            write_golomb(w, gval);
            {
                const int32_t gx = gval + 1;
                const uint32_t glen = svt_log2f(gx) + 1;
                for (uint32_t zi = 0; zi < glen - 1; ++zi) {
                    svtd_script_bool("golz", 0);
                    svtd_tr("golz", 0, &w->ec);
                }
                for (int bi = glen - 1; bi >= 0; --bi) {
                    const int gbit = (gx >> bi) & 1;
                    svtd_script_bool("gol", gbit);
                    svtd_tr("gol", gbit, &w->ec);
                }
            }
            svtd_tr("golomb", gval, &w->ec);
        }
    }
}
// read twin: mirrors aom read_coeffs_txb (decodetxb.c) symbol-for-symbol.
// Returns the decoded coefficient count; coeff[] receives levels in raster
// order (signed).
static int svtd_read_coeffs_txb(aom_reader* r, Ts1FrameContext* fc, TranLow* coeff,
                                const int16_t* scan, TxSize tx_size, int txb_skip_ctx,
                                int dc_sign_ctx, PredictionMode intra_dir) {
    const TxSize txs_ctx        = get_txsize_entropy_ctx(tx_size);
    const int    eob_multi_size = txsize_log2_minus4[tx_size];
    const int    eob_multi_ctx  = 0;
    memset(coeff, 0, sizeof(TranLow) * (get_txb_wide(tx_size) * get_txb_high(tx_size)));

    const int all_zero = aom_read_symbol_(r, fc->txb_skip_cdf[txs_ctx][txb_skip_ctx], 2);
    if (svtd_script) fprintf(stderr, "R %sskip %d\n", svtd_trace_tag, all_zero);
    if (all_zero) return 0;

    // TS3: tx-type read (between txb_skip and eob_pt, entropy_coding.c:374-376),
    // the same verbatim gate as the writer side (av1_read_tx_type folds it:
    // <= 1 types -> DCT_DCT implicit, no symbol).
    if (svtd_get_ext_tx_types(tx_size) > 1) {
        const TxSize sq = txsize_sqr_map[tx_size];
        const int ttx = aom_read_symbol_(r, fc->intra_ext_tx_cdf[2][sq][intra_dir], 5);
        if (svtd_script) fprintf(stderr, "R %stx %d\n", svtd_trace_tag, ttx);
        (void)ttx;  // DCT_DCT-only port: the read symbol is discarded
    }

    int eob_pt;
    int nsyms;
    switch (eob_multi_size) {
    case 0: eob_pt = aom_read_symbol_(r, fc->eob_flag_cdf16[0][eob_multi_ctx], 5) + 1; break;
    case 1: eob_pt = aom_read_symbol_(r, fc->eob_flag_cdf32[0][eob_multi_ctx], 6) + 1; break;
    case 2: eob_pt = aom_read_symbol_(r, fc->eob_flag_cdf64[0][eob_multi_ctx], 7) + 1; break;
    case 3: eob_pt = aom_read_symbol_(r, fc->eob_flag_cdf128[0][eob_multi_ctx], 8) + 1; break;
    case 4: eob_pt = aom_read_symbol_(r, fc->eob_flag_cdf256[0][eob_multi_ctx], 9) + 1; break;
    case 5: eob_pt = aom_read_symbol_(r, fc->eob_flag_cdf512[0][eob_multi_ctx], 10) + 1; break;
    default: eob_pt = aom_read_symbol_(r, fc->eob_flag_cdf1024[0][eob_multi_ctx], 11) + 1; break;
    }
    if (svtd_script) fprintf(stderr, "R %seobpt %d\n", svtd_trace_tag, eob_pt - 1);
    int eob_extra = 0;
    {
        const int eob_offset_bits = (eob_pt > 2) ? (eob_pt - 2) : 0;
        if (eob_offset_bits > 0) {
            const int eob_ctx = eob_pt - 3;
            const int bit = aom_read_symbol_(r, fc->eob_extra_cdf[txs_ctx][0][eob_ctx], 2);
            if (bit) eob_extra += (1 << (eob_offset_bits - 1));
            for (int i = 1; i < eob_offset_bits; i++) {
                if (aom_read_bit(r, NULL)) eob_extra += (1 << (eob_offset_bits - 1 - i));
            }
        }
        if (svtd_script) fprintf(stderr, "R %seobx %d\n", svtd_trace_tag, eob_extra);
    }
    // rec_eob_pos (aom decodetxb.c): group_start[token] + extra, where
    // group_start[1]=1, group_start[2]=2, group_start[t>2]=(1<<(t-2))+1
    int eob;
    if (eob_pt <= 2) {
        eob = eob_pt;
    } else {
        eob = (1 << (eob_pt - 2)) + 1 + eob_extra;
    }

    const int bwl    = get_txb_bwl(tx_size);
    const int width  = get_txb_wide(tx_size);
    const int height = get_txb_high(tx_size);
    uint8_t levels[TX_PAD_2D];
    memset(levels, 0, sizeof(levels));
    const int32_t br_txs_ctx = AOMMIN(txs_ctx, TX_32X32);

    // last coefficient (scan[eob-1]): base_eob (value = sym + 1) + br
    {
        const int c   = eob - 1;
        const int pos = scan[c];
        const int lctx = get_lower_levels_ctx_eob(bwl, height, c);
        int level = aom_read_symbol_(r, fc->coeff_base_eob_cdf[txs_ctx][0][lctx], 3) + 1;
        if (svtd_script) fprintf(stderr, "R %sbeob c=%d pos=%d lctx=%d %d\n", svtd_trace_tag, c, pos,
                                 lctx, level - 1);
        if (level > NUM_BASE_LEVELS) {
            const int br_ctx = get_br_ctx_eob(pos, bwl, TX_CLASS_2D);
            for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
                const int k = aom_read_symbol_(r, fc->coeff_br_cdf[br_txs_ctx][0][br_ctx], BR_CDF_SIZE);
                level += k;
                if (svtd_script) fprintf(stderr, "R %sbr c=%d pos=%d brctx=%d k=%d\n", svtd_trace_tag, c,
                                         pos, br_ctx, k);
                if (k < BR_CDF_SIZE - 1) break;
            }
        }
        levels[get_padded_idx(pos, bwl)] = level;
    }
    // reverse pass c = eob-2 .. 0
    for (int c = eob - 2; c >= 0; --c) {
        const int pos = scan[c];
        const int coeff_ctx =
            (eob == 1) ? 0 : get_lower_levels_ctx(levels, pos, bwl, tx_size, TX_CLASS_2D);
        int level = aom_read_symbol_(r, fc->coeff_base_cdf[txs_ctx][0][coeff_ctx], 4);
        if (svtd_script) {
        AomCdfProb* dbgRow = fc->coeff_base_cdf[txs_ctx][0][coeff_ctx];
        fprintf(stderr,
                "R %sbase c=%d pos=%d ctx=%d %d row=%u,%u,%u cnt=%u rng=%u dif=%llu cnt2=%d\n",
                svtd_trace_tag, c, pos, coeff_ctx, level, dbgRow[0], dbgRow[1], dbgRow[2],
                dbgRow[4], r->ec.rng, (unsigned long long)r->ec.dif, (int)r->ec.cnt);
    }
        if (level > NUM_BASE_LEVELS) {
            const int br_ctx = get_br_ctx(levels, pos, bwl, TX_CLASS_2D);
            for (int idx = 0; idx < COEFF_BASE_RANGE; idx += BR_CDF_SIZE - 1) {
                const int k = aom_read_symbol_(r, fc->coeff_br_cdf[br_txs_ctx][0][br_ctx], BR_CDF_SIZE);
                level += k;
                if (svtd_script) fprintf(stderr, "R %sbrs c=%d pos=%d brctx=%d k=%d\n", svtd_trace_tag, c,
                                         pos, br_ctx, k);
                if (k < BR_CDF_SIZE - 1) break;
            }
        }
        levels[get_padded_idx(pos, bwl)] = level;
    }
    // forward pass: signs + golomb
    for (int c = 0; c < eob; ++c) {
        const int pos = scan[c];
        const int level = levels[get_padded_idx(pos, bwl)];
        if (!level) continue;
        int sign;
        if (c == 0) {
            sign = aom_read_symbol_(r, fc->dc_sign_cdf[0][dc_sign_ctx], 2);
            if (svtd_script) fprintf(stderr, "R %ssign0 %d\n", svtd_trace_tag, sign);
        } else {
            sign = aom_read_bit(r, NULL);
            if (svtd_script)
                fprintf(stderr, "R %ssign %d rng=%u dif=%llu cnt=%d bptr=%d\n", svtd_trace_tag, sign,
                         r->ec.rng, (unsigned long long)r->ec.dif, (int)r->ec.cnt,
                         (int)(r->ec.bptr - r->ec.buf));
        }
        int lv = level;
        if (lv >= MAX_BASE_BR_RANGE) {
            // read_golomb (aom decodetxb.c): count-1 ones-prefixed bits
            int x = 1, length = 0, i = 0;
            while (!i) {
                i = aom_read_bit(r, NULL);
                ++length;
                if (svtd_script) fprintf(stderr, "R %sgolz %d\n", svtd_trace_tag, i ? 0 : 1);
            }
            for (i = 0; i < length - 1; ++i) {
                x <<= 1;
                x += aom_read_bit(r, NULL);
            }
            {
                const int gx = x;
                for (int bi = length - 1; bi >= 0; --bi) {
                    if (svtd_script) fprintf(stderr, "R %sgol %d\n", svtd_trace_tag, (gx >> bi) & 1);
                }
            }
            lv += x - 1;
        }
        if (svtd_script) fprintf(stderr, "R %ssigned c=%d pos=%d lv=%d sign=%d\n", svtd_trace_tag, c,
                                 pos, lv, sign);
        coeff[pos] = sign ? -lv : lv;
    }
    return eob;
}
// ---- FS2: per-size Q emission drives ---------------------------------------
// One single-TU SxS frame per geometry (4/8/32/64; 16x16 is the committed
// ecfrm/td0 set). Per drive: the D2 SAD policy decides the mode over the
// fresh-edge fixture (the 13-mode sweep; policy ours, primitives 1:1), then
// the full per-block symbol walk in the wire order:
//   [partition iff the walk reads one] [skip=0] [kf mode] [FI iff
//   mode==DC_PRED && block_size <= 32x32 (mode_decision.c:108-120)]
//   [tx-type iff svtd_get_ext_tx_types > 1] [token chain].
// Partition reads per frame size: a 4x4 frame reads NONE (at the 8x8 level
// both splits are unavailable in a 1x1-mi frame -> forced split, the 4x4
// quadrant coded directly); 8x8 reads a 4-symbol row at BLOCK_8X8
// (svt_aom_partition_cdf_length, entropy_coding.c:922-930); 16/32/64 read
// 10-symbol rows at their level. The fresh partition context comes from
// ecpart_derive_ctx on INVALID-filled arrays (the TD0 walk shape); the
// fresh kf ctx pair = intra_mode_context[DC_PRED] (the TD0 walk shape).
// All four drives run the read twin with the SAME bucket + scan and assert
// the roundtrip; the fs2S_* gate lines pin every surface for the l6 mirror
// (FS3/FS4). The TX_64X64 drive runs the scan-contract settle documented at
// svtd_default_scan_64x64_token above.
static void svtd_fs2_drive(int S) {
    static char tagbuf[8];
    tagbuf[0] = (S == 4) ? 'p' : (S == 8) ? 'q' : (S == 32) ? 'r' : 's';
    tagbuf[1] = ':';
    tagbuf[2] = 0;
    svtd_trace_tag = tagbuf;
    const TxSize ts    = (S == 4) ? TX_4X4 : (S == 8) ? TX_8X8 : (S == 32) ? TX_32X32 : TX_64X64;
    const BlockSize bs = (S == 4)    ? BLOCK_4X4
                         : (S == 8)  ? BLOCK_8X8
                         : (S == 32) ? BLOCK_32X32
                                     : BLOCK_64X64;
    static uint8_t src[4096];
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
            src[y * S + x] = (uint8_t)((y < S / 2) ? (4 * (x + y + 1)) : 0);

    SvtdQuantTables t;
    svtd_build_quantizer_luma(100, &t);
    static int16_t scan[4096];
    if (S == 4) {
        svtd_default_scan_4x4(scan);
    } else if (S == 8) {
        svtd_default_scan_8x8(scan);
    } else if (S == 32) {
        svtd_default_scan_32x32(scan);
    } else {
        svtd_default_scan_64x64_token(scan);  // the normative 1024-position scan
    }
    const int n = S * S;

    // D2 policy: 13 candidates, SAD scored, lowest wins, tie = lowest idx
    static uint8_t srcblk[4096];
    memcpy(srcblk, src, (size_t)n);
    static uint8_t pred[4096];
    uint32_t best_sad = 0;
    int mode = -1;
    for (int m = 0; m <= PAETH_PRED; ++m) {
        svtd_call_builder_tx(pred, m, 0, FILTER_INTRA_MODES, 0, NULL, 0, 0, NULL, 0, 0, 0, ts);
        const uint32_t sad = svt_nxm_sad_kernel_helper_c(srcblk, S, pred, S, S, S);
        if (mode < 0 || sad < best_sad) { best_sad = sad; mode = m; }
    }
    svtd_call_builder_tx(pred, mode, 0, FILTER_INTRA_MODES, 0, NULL, 0, 0, NULL, 0, 0, 0, ts);
    static int16_t res[4096];
    for (int i = 0; i < n; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);

    // forward + quantize (the emission domain)
    static int32_t cb[4096];
    static TranLow qc[4096], dq[4096];
    memset(qc, 0, sizeof(qc));
    memset(dq, 0, sizeof(dq));
    uint16_t eob = 0;
    if (S == 64) {
        // THE TX_64X64 SCAN-CONTRACT (the settle documented at
        // svtd_default_scan_64x64_token): fwd64 (the l3 64-wide facade) ->
        // compact the top-left 32x32 of the INT32 coeff output into a
        // 32-WIDE buffer (the vendored fwd wrapper does exactly this,
        // svt_handle_transform64x64_N2_N4_c, transforms.c:2700-2707) ->
        // quantize n_coeffs = 1024 over the compacted buffer with the
        // normative 32x32-grid scan (full_loop.c:1262 + the scan).
        svtd_fwd2d64x64(res, S, cb, svt_av1_fdct64_new);
        for (int r = 1; r < 32; ++r)
            memcpy(cb + r * 32, cb + r * 64, 32 * sizeof(*cb));
        svtd_quantize_fp_64x64_token(cb, &t, scan, qc, dq, &eob);
    } else if (S == 32) {
        svtd_fwd2d32x32(res, S, cb, svt_av1_fdct32_new);
        svtd_quantize_fp_32x32(cb, &t, scan, qc, dq, &eob);
    } else if (S == 8) {
        svtd_fwd2d8x8(res, S, cb, svt_av1_fdct8_new);
        svtd_quantize_fp_8x8(cb, &t, scan, qc, dq, &eob);
    } else {
        svtd_fwd2d4x4(res, S, cb, svt_av1_fdct4_new);
        svtd_quantize_fp_4x4(cb, &t, scan, qc, dq, &eob);
    }

    // emission context (q100 -> the idx-2 bucket, TD5a)
    static AomCdfProb part_cdf[PARTITION_CONTEXTS][CDF_SIZE(EXT_PARTITION_TYPES)];
    memcpy(part_cdf, default_partition_cdf, sizeof(part_cdf));
    static AomCdfProb skip_cdf[SKIP_CONTEXTS][CDF_SIZE(2)];
    memcpy(skip_cdf, default_skip_cdfs, sizeof(skip_cdf));
    static AomCdfProb kf_y_cdf[KF_MODE_CONTEXTS][KF_MODE_CONTEXTS][CDF_SIZE(INTRA_MODES)];
    memcpy(kf_y_cdf, svt_aom_default_kf_y_mode_cdf, sizeof(kf_y_cdf));
    static AomCdfProb fi_cdf[CDF_SIZE(2)];
    memcpy(fi_cdf, default_filter_intra_cdfs[bs], sizeof(fi_cdf));
    Ts1FrameContext fc;
    ts1_init(&fc, 100);
    Ts1FrameContext fc2;
    ts1_init(&fc2, 100);

    static uint8_t tile_buf[1024];  // the fs264 tile is 420 bytes: [256] overflowed (the rt root cause)
    memset(tile_buf, 0, sizeof(tile_buf));
    AomWriter w;
    w.ec.buf = tile_buf;
    svt_od_ec_enc_reset(&w.ec);
    w.allow_update_cdf = svtd_td0_adapt_probe;
    w.pos = 0;

    // [partition iff the walk reads one]
    if (S >= 8) {
        static uint8_t above_pctx[8];
        static uint8_t left_pctx[16];
        memset(above_pctx, (int)INVALID_NEIGHBOR_DATA, sizeof(above_pctx));
        memset(left_pctx, (int)INVALID_NEIGHBOR_DATA, sizeof(left_pctx));
        const int pctx = ecpart_derive_ctx(above_pctx, left_pctx, 0, 0, bs);
svtd_script_row("part", PARTITION_NONE, part_cdf[pctx],
                        svt_aom_partition_cdf_length(bs));
        aom_write_symbol(&w, PARTITION_NONE, part_cdf[pctx], svt_aom_partition_cdf_length(bs));
        printf("fs2%d_part %d\n", S, pctx);
    } else {
        // 4x4 frame: no partition symbol (the forced-split quadrant is
        // coded directly).
        printf("fs24_part -1\n");
    }
// skip = 0, ctx 0 (fresh)
    svtd_script_row("skip", 0, skip_cdf[0], 2);
    aom_write_symbol(&w, 0, skip_cdf[0], 2);
    // kf mode, fresh ctx pair
    const int kctx = intra_mode_context[DC_PRED];
    svtd_script_row("mode", mode, kf_y_cdf[kctx][kctx], INTRA_MODES);
    aom_write_symbol(&w, mode, kf_y_cdf[kctx][kctx], INTRA_MODES);
    // FS3 fix (the drive deviation, named): the angle-delta symbol after a
    // directional kf mode at bsize >= 8x8 (encode_intra_luma_mode_kf_av1,
    // entropy_coding.c:1030-1037; the svtd_td0_tile walk precedent). The
    // previous drive omitted it - the fs2 8/32/64 bytes changed and the
    // gate lines regenerated; 4x4 unchanged (bsize < 8X8, no delta).
    static AomCdfProb angle_cdf[DIRECTIONAL_MODES][CDF_SIZE(2 * MAX_ANGLE_DELTA + 1)];
    memcpy(angle_cdf, default_angle_delta_cdf, sizeof(angle_cdf));
    if (S >= 8 && av1_is_directional_mode((PredictionMode)mode)) {
        svtd_script_row("delta", MAX_ANGLE_DELTA, angle_cdf[mode - V_PRED],
                        2 * MAX_ANGLE_DELTA + 1);
        aom_write_symbol(&w, MAX_ANGLE_DELTA, angle_cdf[mode - V_PRED],
                         2 * MAX_ANGLE_DELTA + 1);
    }
    // FI iff allowed (mode_decision.c:108-120; 64x64 dead by bsize)
    if (mode == DC_PRED && block_size_wide[bs] <= 32 && block_size_high[bs] <= 32) {
        svtd_script_row("fi", 0, fi_cdf, 2);
        aom_write_symbol(&w, 0, fi_cdf, 2);
    }
    // the token chain (the tx-type gate inside is the verbatim FS3-prep fix)
    svtd_write_coeffs_txb(&w, &fc, qc, scan, ts, eob, 0, 0, (PredictionMode)mode);
    aom_stop_encode(&w);

    printf("fs2%d_modes %d\n", S, mode);
    printf("fs2%d_eobs %d\n", S, (int)eob);
    printf("fs2%d_bytes %u", S, w.pos);
    for (uint32_t i = 0; i < w.pos; ++i) printf(" %02x", tile_buf[i]);
    printf("\n");
    printf("fs2%d_coeffs", S);
    for (int i = 0; i < n; ++i) printf(" %d", (int)qc[i]);
    printf("\n");

    // recon (the decoder contract): the emission-domain dqcoeff placed back
    static uint8_t rec[4096];
    if (S == 64) {
        // expand the compacted dqcoeff into the 64-wide zeroed array (the
        // svt_av1_inv_txfm2d_add_64x64_c internal remap,
        // inv_transforms.c:2615-2628) then the l3 inv64 facade
        static TranLow dq64[4096];
        memset(dq64, 0, sizeof(dq64));
        for (int r = 0; r < 32; ++r)
            for (int c = 0; c < 32; ++c) dq64[r * 64 + c] = dq[r * 32 + c];
        svtd_inv2dadd64x64(dq64, pred, S, svt_av1_idct64_new);
    } else if (S == 32) {
        svtd_inv2dadd32x32(dq, pred, S, svt_av1_idct32_new);
    } else if (S == 8) {
        svtd_inv2dadd8x8(dq, pred, S, svt_av1_idct8_new);
    } else {
        svtd_inv2dadd4x4(dq, pred, S, svt_av1_idct4_new);
    }
    memcpy(rec, pred, (size_t)n);
    printf("fs2%d_recon", S);
    for (int i = 0; i < n; ++i) printf(" %d", (int)rec[i]);
    printf("\n");

    // read twin with the SAME bucket + scan
    static uint8_t above_na[16];
    static uint8_t left_na[8];
    memset(above_na, (int)INVALID_NEIGHBOR_DATA, sizeof(above_na));
    memset(left_na, (int)INVALID_NEIGHBOR_DATA, sizeof(left_na));
    aom_reader r;
    if (svtd_script) { fprintf(stderr, "FS2 tail S=%d:", S); for (int ti = 410; ti < 424; ++ti) fprintf(stderr, " %02x", tile_buf[ti]); fprintf(stderr, " (pos=%u)\n", w.pos); }
    if (aom_reader_init(&r, tile_buf, w.pos)) { fprintf(stderr, "FS2 reader init\n"); return; }
    r.allow_update_cdf = 1;
    static AomCdfProb rs[CDF_SIZE(2)];
    memcpy(rs, default_skip_cdfs[0], sizeof(rs));
    if (S >= 8) {
        static AomCdfProb rp[PARTITION_CONTEXTS][CDF_SIZE(EXT_PARTITION_TYPES)];
        memcpy(rp, default_partition_cdf, sizeof(rp));
        const int pctx2 = ecpart_derive_ctx(above_na, left_na, 0, 0, bs);
        const int pv = aom_read_symbol_(&r, rp[pctx2], svt_aom_partition_cdf_length(bs));
        if (pv != PARTITION_NONE) { fprintf(stderr, "FS2 rt part %d\n", pv); return; }
    }
    aom_read_symbol_(&r, rs, 2);
    {
        static AomCdfProb rk[KF_MODE_CONTEXTS][KF_MODE_CONTEXTS][CDF_SIZE(INTRA_MODES)];
        memcpy(rk, svt_aom_default_kf_y_mode_cdf, sizeof(rk));
        const int kctx2 = intra_mode_context[DC_PRED];
        const int m2 = aom_read_symbol_(&r, rk[kctx2][kctx2], INTRA_MODES);
        if (m2 != mode) { fprintf(stderr, "FS2 rt mode %d\n", m2); return; }
        // FS3 fix: the angle-delta read after a directional mode at 8x8+
        if (S >= 8 && av1_is_directional_mode((PredictionMode)m2)) {
            static AomCdfProb ra[CDF_SIZE(2 * MAX_ANGLE_DELTA + 1)];
            memcpy(ra, default_angle_delta_cdf[mode - V_PRED], sizeof(ra));
            aom_read_symbol_(&r, ra, 2 * MAX_ANGLE_DELTA + 1);
        }
        if (mode == DC_PRED && block_size_wide[bs] <= 32 && block_size_high[bs] <= 32) {
            static AomCdfProb rfi[CDF_SIZE(2)];
            memcpy(rfi, default_filter_intra_cdfs[bs], sizeof(rfi));
            aom_read_symbol_(&r, rfi, 2);
        }
    }
static TranLow rc[4096];
    memset(rc, 0, sizeof(rc));
    const int reob = svtd_read_coeffs_txb(&r, &fc2, rc, scan, ts, 0, 0, (PredictionMode)mode);
    int coeff_eq = (reob == (int)eob) ? 1 : 0;
    int first_mm = -1;
    int mm_count = 0;
    for (int i = 0; i < n; ++i) {
        if (rc[i] != qc[i]) {
            coeff_eq = 0;
            ++mm_count;
            if (first_mm < 0) first_mm = i;
            if (svtd_script) fprintf(stderr, "FS2 rt mm S=%d i=%d qc=%d rc=%d\n", S, i, (int)qc[i],
                                     (int)rc[i]);
        }
    }
    if (first_mm >= 0) {
        fprintf(stderr, "FS2 rt mismatch S=%d first=%d qc=%d rc=%d total=%d\n", S, first_mm,
                (int)qc[first_mm], (int)rc[first_mm], mm_count);
    }
    printf("fs2%d_rt %d %d\n", S, reob, coeff_eq);
}
// ---- FS5: the grid drive (the running partition-context proof) -------------
// A 64x64 frame of the fs2 fixture coded as the 2x2 grid of 32x32 blocks at
// q100. Per block the walk is the l6 32x32Q shape with the RUNNING states:
//   [partition (10-symbol BLOCK_32X32 row, ctx from the running pctx arrays
//    via ecpart_derive_ctx; ecpart_update_ctx after - coding_loop.c:1700-1713)]
//   [skip=0, ctx 0 (all skips 0)][kf mode, ctx from the neighbor modes]
//   [delta iff directional][FI iff DC_PRED && bsize <= 32x32]
//   [token chain, the dc_sign ctx from the RUNNING coefficient NA].
// The FS3/FS4 single-TU drives see only the fresh INVALID pair; this drive
// pins the grid wiring (updatePartitionContext) the FS5 court slice mandates.
// The read twin re-walks with the same running states and asserts per-block
// roundtrips.
static void svtd_fs5_grid_drive(void) {
    svtd_trace_tag = "t:";
    static uint8_t src[4096];
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) src[y * 64 + x] = (y < 32) ? (uint8_t)(4 * (x + y + 1)) : 0;
    SvtdQuantTables t;
    svtd_build_quantizer_luma(100, &t);
    int16_t scan32[1024];
    svtd_default_scan_32x32(scan32);
    Ts1FrameContext fc, fc_r;
    ts1_init(&fc, 100);
    ts1_init(&fc_r, 100);
    static AomCdfProb part_cdf[PARTITION_CONTEXTS][CDF_SIZE(EXT_PARTITION_TYPES)];
    memcpy(part_cdf, default_partition_cdf, sizeof(part_cdf));
    static AomCdfProb skip_cdf[SKIP_CONTEXTS][CDF_SIZE(2)];
    memcpy(skip_cdf, default_skip_cdfs, sizeof(skip_cdf));
    static AomCdfProb angle_cdf[DIRECTIONAL_MODES][CDF_SIZE(2 * MAX_ANGLE_DELTA + 1)];
    memcpy(angle_cdf, default_angle_delta_cdf, sizeof(angle_cdf));

    static uint8_t recon[4096];
    memset(recon, 0, sizeof(recon));
    int modes[4] = {0};
    static uint8_t tile[4096];
    memset(tile, 0, sizeof(tile));
    AomWriter w;
    w.ec.buf = tile;
    svt_od_ec_enc_reset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;

    uint8_t above_pctx[16], left_pctx[16];
    memset(above_pctx, (int)INVALID_NEIGHBOR_DATA, sizeof(above_pctx));
    memset(left_pctx, (int)INVALID_NEIGHBOR_DATA, sizeof(left_pctx));
    uint8_t above_na[16], left_na[16];
    memset(above_na, (int)INVALID_NEIGHBOR_DATA, sizeof(above_na));
    memset(left_na, (int)INVALID_NEIGHBOR_DATA, sizeof(left_na));

    static const int px[4][2] = {{0, 0}, {32, 0}, {0, 32}, {32, 32}};
    uint16_t eobs[4] = {0};
    static TranLow all_qc[4][1024];
    for (int b = 0; b < 4; ++b) {
        const int bx = b % 2, by = b / 2;
        const int r = px[b][1], c = px[b][0];
        const int miRow = by * 8, miCol = bx * 8;
        const int hasTop = by > 0, hasLeft = bx > 0;
        const int nTop = hasTop ? 32 : 0, nLeft = hasLeft ? 32 : 0;
        const int nTr = (hasTop && bx + 1 < 2) ? 32 : 0;
        uint8_t above[65] = {0}, left_edge[65] = {0};
        uint8_t al = 0;
        if (hasTop) {
            for (int i = 0; i < 32 + nTr; ++i) above[i] = recon[(r - 1) * 64 + c + i];
        }
        if (hasLeft) {
            for (int i = 0; i < 32; ++i) left_edge[i] = recon[(r + i) * 64 + c - 1];
        }
        if (hasTop && hasLeft) al = recon[(r - 1) * 64 + c - 1];
        const int aboveMode = hasTop ? modes[(by - 1) * 2 + bx] : DC_PRED;
        const int leftMode = hasLeft ? modes[by * 2 + bx - 1] : DC_PRED;
        svtd_filt_type =
            ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED || aboveMode == SMOOTH_H_PRED) ||
             (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED || leftMode == SMOOTH_H_PRED))
                ? 1
                : 0;
        uint8_t srcblk[1024];
        for (int i = 0; i < 32; ++i)
            for (int j = 0; j < 32; ++j) srcblk[i * 32 + j] = src[(r + i) * 64 + c + j];
        // D2 policy: 13 candidates, SAD scored, lowest wins, tie = lowest idx
        uint32_t best_sad = 0;
        int mode = -1;
        for (int m = 0; m <= PAETH_PRED; ++m) {
            uint8_t pred[1024];
            svtd_call_builder_tx(pred, m, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left_edge,
                                 nLeft, 0, al, TX_32X32);
            const uint32_t sad = svt_nxm_sad_kernel_helper_c(srcblk, 32, pred, 32, 32, 32);
            if (mode < 0 || sad < best_sad) { best_sad = sad; mode = m; }
        }
        modes[b] = mode;
        uint8_t pred[1024];
        svtd_call_builder_tx(pred, mode, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left_edge,
                             nLeft, 0, al, TX_32X32);
        int16_t res[1024];
        for (int i = 0; i < 1024; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
        int32_t cb[1024];
        svtd_fwd2d32x32(res, 32, cb, svt_av1_fdct32_new);
        TranLow qc[1024], dq[1024];
        uint16_t eob = 0;
        svtd_quantize_fp_32x32(cb, &t, scan32, qc, dq, &eob);
        eobs[b] = eob;
        memcpy(all_qc[b], qc, sizeof(qc));

        // ---- the walk (running partition ctxs, skip ctx 0, kf ctx from the
        // neighbor modes, delta, FI iff DC, tokens with the running NA) ----
        const int pctx = ecpart_derive_ctx(above_pctx, left_pctx, miRow, miCol, BLOCK_32X32);
        svtd_script_row("part", PARTITION_NONE, part_cdf[pctx],
                        svt_aom_partition_cdf_length(BLOCK_32X32));
        aom_write_symbol(&w, PARTITION_NONE, part_cdf[pctx],
                         svt_aom_partition_cdf_length(BLOCK_32X32));
        ecpart_update_ctx(above_pctx, left_pctx, miRow, miCol, BLOCK_32X32);
        svtd_script_row("skip", 0, skip_cdf[0], 2);
        aom_write_symbol(&w, 0, skip_cdf[0], 2);
        const int top_ctx = intra_mode_context[aboveMode < 0 ? DC_PRED : (PredictionMode)aboveMode];
        const int left_ctx = intra_mode_context[leftMode < 0 ? DC_PRED : (PredictionMode)leftMode];
        svtd_script_row("mode", mode, fc.kf_y_cdf[top_ctx][left_ctx], INTRA_MODES);
        aom_write_symbol(&w, mode, fc.kf_y_cdf[top_ctx][left_ctx], INTRA_MODES);
        if (av1_is_directional_mode((PredictionMode)mode)) {
            svtd_script_row("delta", MAX_ANGLE_DELTA, angle_cdf[mode - V_PRED],
                            2 * MAX_ANGLE_DELTA + 1);
            aom_write_symbol(&w, MAX_ANGLE_DELTA, angle_cdf[mode - V_PRED],
                             2 * MAX_ANGLE_DELTA + 1);
        }
        if (mode == DC_PRED) {
            static AomCdfProb fi_cdf[CDF_SIZE(2)];
            memcpy(fi_cdf, default_filter_intra_cdfs[BLOCK_32X32], sizeof(fi_cdf));
            svtd_script_row("fi", 0, fi_cdf, 2);
            aom_write_symbol(&w, 0, fi_cdf, 2);
        }
        // the token chain: the dc_sign ctx from the RUNNING NA (the ecfrm
        // pattern; tx units 8 for the 32-wide TU)
        uint8_t* above_ptr = &above_na[miCol];
        uint8_t* left_ptr = &left_na[miRow];
        int16_t dc_sign = 0;
        if (above_ptr[0] != (uint8_t)INVALID_NEIGHBOR_DATA) {
            for (int k = 0; k < 8; ++k)
                dc_sign += ((above_ptr[k] >> COEFF_CONTEXT_BITS) == 1)
                               ? -1
                               : ((above_ptr[k] >> COEFF_CONTEXT_BITS) == 2) ? 1 : 0;
        }
        if (left_ptr[0] != (uint8_t)INVALID_NEIGHBOR_DATA) {
            for (int k = 0; k < 8; ++k)
                dc_sign += ((left_ptr[k] >> COEFF_CONTEXT_BITS) == 1)
                               ? -1
                               : ((left_ptr[k] >> COEFF_CONTEXT_BITS) == 2) ? 1 : 0;
        }
        const int dc_sign_ctx = dc_sign > 0 ? 2 : dc_sign < 0 ? 1 : 0;
        svtd_write_coeffs_txb(&w, &fc, qc, scan32, TX_32X32, eob, 0, dc_sign_ctx,
                              (PredictionMode)mode);
        // NA update (the ecfrm pattern: cul_level + set_dc_sign, the TU extent)
        int32_t cul = 0;
        for (int q = 0; q < eob; ++q) cul += abs((int)qc[scan32[q]]);
        cul = AOMMIN(cul, COEFF_CONTEXT_MASK);
        if (eob > 0) {
            if (qc[0] < 0) cul |= 1 << COEFF_CONTEXT_BITS;
            else if (qc[0] > 0) cul += 2 << COEFF_CONTEXT_BITS;
        }
        for (int k = 0; k < 8; ++k) above_ptr[k] = (uint8_t)cul;
        for (int k = 0; k < 8; ++k) left_ptr[k] = (uint8_t)cul;
        // recon update (lossy: dequantized inverse over the predicted block)
        svtd_inv2dadd32x32(dq, pred, 32, svt_av1_idct32_new);
        for (int i = 0; i < 32; ++i)
            for (int j = 0; j < 32; ++j) recon[(r + i) * 64 + c + j] = pred[i * 32 + j];
    }
    aom_stop_encode(&w);

    printf("fs5g32_modes");
    for (int b = 0; b < 4; ++b) printf(" %d", modes[b]);
    printf("\n");
    printf("fs5g32_eobs");
    for (int b = 0; b < 4; ++b) printf(" %d", (int)eobs[b]);
    printf("\n");
    printf("fs5g32_bytes %u", w.pos);
    for (uint32_t i = 0; i < w.pos; ++i) printf(" %02x", tile[i]);
    printf("\n");
    printf("fs5g32_coeffs");
    for (int b = 0; b < 4; ++b)
        for (int i = 0; i < 1024; ++i) printf(" %d", (int)all_qc[b][i]);
    printf("\n");
    printf("fs5g32_recon");
    for (int i = 0; i < 4096; ++i) printf(" %d", (int)recon[i]);
    printf("\n");

    // read twin with the SAME running states
    uint8_t above_pctx_r[16], left_pctx_r[16];
    memset(above_pctx_r, (int)INVALID_NEIGHBOR_DATA, sizeof(above_pctx_r));
    memset(left_pctx_r, (int)INVALID_NEIGHBOR_DATA, sizeof(left_pctx_r));
    uint8_t above_na_r[16], left_na_r[16];
    memset(above_na_r, (int)INVALID_NEIGHBOR_DATA, sizeof(above_na_r));
    memset(left_na_r, (int)INVALID_NEIGHBOR_DATA, sizeof(left_na_r));
    static AomCdfProb rpart[PARTITION_CONTEXTS][CDF_SIZE(EXT_PARTITION_TYPES)];
    memcpy(rpart, default_partition_cdf, sizeof(rpart));
    static AomCdfProb rskip[SKIP_CONTEXTS][CDF_SIZE(2)];
    memcpy(rskip, default_skip_cdfs, sizeof(rskip));
    static AomCdfProb rangle[DIRECTIONAL_MODES][CDF_SIZE(2 * MAX_ANGLE_DELTA + 1)];
    memcpy(rangle, default_angle_delta_cdf, sizeof(rangle));
    aom_reader rr;
    if (aom_reader_init(&rr, tile, w.pos)) { fprintf(stderr, "FS5 reader init\n"); return; }
    rr.allow_update_cdf = 1;
    printf("fs5g32_rt");
    for (int b = 0; b < 4; ++b) {
        const int bx = b % 2, by = b / 2;
        const int miRow = by * 8, miCol = bx * 8;
        const int pctx2 = ecpart_derive_ctx(above_pctx_r, left_pctx_r, miRow, miCol, BLOCK_32X32);
        const int pv = aom_read_symbol_(&rr, rpart[pctx2],
                                        svt_aom_partition_cdf_length(BLOCK_32X32));
        if (pv != PARTITION_NONE) { fprintf(stderr, "FS5 rt part %d\n", pv); break; }
        ecpart_update_ctx(above_pctx_r, left_pctx_r, miRow, miCol, BLOCK_32X32);
        aom_read_symbol_(&rr, rskip[0], 2);
        const int aboveMode2 = by > 0 ? modes[(by - 1) * 2 + bx] : DC_PRED;
        const int leftMode2 = bx > 0 ? modes[by * 2 + bx - 1] : DC_PRED;
        const int top_ctx2 =
            intra_mode_context[aboveMode2 < 0 ? DC_PRED : (PredictionMode)aboveMode2];
        const int left_ctx2 =
            intra_mode_context[leftMode2 < 0 ? DC_PRED : (PredictionMode)leftMode2];
        const int rmode = aom_read_symbol_(&rr, fc_r.kf_y_cdf[top_ctx2][left_ctx2], INTRA_MODES);
        if (rmode != modes[b]) { fprintf(stderr, "FS5 rt mode %d\n", rmode); break; }
        if (av1_is_directional_mode((PredictionMode)rmode)) {
            static AomCdfProb ra[CDF_SIZE(2 * MAX_ANGLE_DELTA + 1)];
            memcpy(ra, default_angle_delta_cdf[rmode - V_PRED], sizeof(ra));
            aom_read_symbol_(&rr, ra, 2 * MAX_ANGLE_DELTA + 1);
        }
        if (rmode == DC_PRED) {
            static AomCdfProb rfi[CDF_SIZE(2)];
            memcpy(rfi, default_filter_intra_cdfs[BLOCK_32X32], sizeof(rfi));
            aom_read_symbol_(&rr, rfi, 2);
        }
        // dc_sign ctx from the running NA (mirror of the writer)
        uint8_t* above_ptr = &above_na_r[miCol];
        uint8_t* left_ptr = &left_na_r[miRow];
        int16_t dc_sign = 0;
        if (above_ptr[0] != (uint8_t)INVALID_NEIGHBOR_DATA) {
            for (int k = 0; k < 8; ++k)
                dc_sign += ((above_ptr[k] >> COEFF_CONTEXT_BITS) == 1)
                               ? -1
                               : ((above_ptr[k] >> COEFF_CONTEXT_BITS) == 2) ? 1 : 0;
        }
        if (left_ptr[0] != (uint8_t)INVALID_NEIGHBOR_DATA) {
            for (int k = 0; k < 8; ++k)
                dc_sign += ((left_ptr[k] >> COEFF_CONTEXT_BITS) == 1)
                               ? -1
                               : ((left_ptr[k] >> COEFF_CONTEXT_BITS) == 2) ? 1 : 0;
        }
        const int dc_sign_ctx2 = dc_sign > 0 ? 2 : dc_sign < 0 ? 1 : 0;
        static TranLow rc[1024];
        memset(rc, 0, sizeof(rc));
        const int reob = svtd_read_coeffs_txb(&rr, &fc_r, rc, scan32, TX_32X32, 0, dc_sign_ctx2,
                                              (PredictionMode)modes[b]);
        int ok = (reob == (int)eobs[b]);
        for (int q = 0; q < (int)eobs[b]; ++q) {
            if (rc[scan32[q]] != all_qc[b][scan32[q]]) { ok = 0; break; }
        }
        printf(" %d", ok);
        // NA update (the reader side)
        int32_t cul2 = 0;
        for (int q = 0; q < reob; ++q) cul2 += abs((int)rc[scan32[q]]);
        cul2 = AOMMIN(cul2, COEFF_CONTEXT_MASK);
        if (reob > 0) {
            if (rc[0] < 0) cul2 |= 1 << COEFF_CONTEXT_BITS;
            else if (rc[0] > 0) cul2 += 2 << COEFF_CONTEXT_BITS;
        }
        for (int k = 0; k < 8; ++k) above_na_r[miCol + k] = (uint8_t)cul2;
        for (int k = 0; k < 8; ++k) left_na_r[miRow + k] = (uint8_t)cul2;
    }
    printf("\n");
}
// TS1 driver: one 16x16 TU (f16 block 0, q100) + 4x4/8x8 TUs (multi-size
// eob selection coverage). Writes, reads back, compares.
static int svtd_ts1_drive(uint8_t* buf, int verbose) {
    // f16 fixture (same as f16_modes): top half ramp, bottom zero
    uint8_t src[1024];
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) src[y * 32 + x] = (y < 16) ? (uint8_t)(4 * (x + y + 1)) : 0;

    SvtdQuantTables t;
    svtd_build_quantizer_luma(100, &t);
    int16_t scan16[256], scan8[64], scan4[16];
    svtd_default_scan_16x16(scan16);
    svtd_default_scan_8x8(scan8);
    svtd_default_scan_4x4(scan4);

    // 16x16 TU: block (bx=0, by=0)
    uint8_t srcblk[256];
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) srcblk[i * 16 + j] = src[i * 32 + j];
    int16_t res16[256];
    for (int i = 0; i < 256; ++i) res16[i] = (int16_t)srcblk[i];  // DC_PRED with no neighbors -> predictor 0
    int32_t cb16[256];
    svtd_fwd2d16x16(res16, 16, cb16, svt_av1_fdct16_new);
    TranLow qc16[256], dq16[256];
    uint16_t eob16 = 0;
    svtd_quantize_fp_16x16(cb16, &t, scan16, qc16, dq16, &eob16);

    // 8x8 TU: block (bx=1, by=0)
    int16_t res8[64];
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) res8[i * 8 + j] = (int16_t)src[i * 32 + 16 + j];
    int32_t cb8[64];
    svtd_fwd2d8x8(res8, 8, cb8, svt_av1_fdct8_new);
    TranLow qc8[64], dq8[64];
    uint16_t eob8 = 0;
    svtd_quantize_fp_8x8(cb8, &t, scan8, qc8, dq8, &eob8);

    // 4x4 TU: block (bx=0, by=1) top-left
    int16_t res4[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) res4[i * 4 + j] = (int16_t)src[(16 + i) * 32 + j];
    int32_t cb4[16];
    svtd_fwd2d4x4(res4, 4, cb4, svt_av1_fdct4_new);
    TranLow qc4[16], dq4[16];
    uint16_t eob4 = 0;
    svtd_quantize_fp_4x4(cb4, &t, scan4, qc4, dq4, &eob4);

    Ts1FrameContext fc;
    ts1_init(&fc, 100);
    Ts1FrameContext fc_r;
    ts1_init(&fc_r, 100);

    AomWriter w;
    w.ec.buf = buf;
    svt_od_ec_enc_reset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;

    // contexts: our whole-block TUs always have plane_bsize == tx_bsize
    // (svt_aom_get_txb_ctx :298-299) -> txb_skip_ctx = 0; dc_sign_ctx = 0
    // (no coded neighbors yet). TS2 ports the general ctx derivation.
    svtd_write_coeffs_txb(&w, &fc, qc16, scan16, TX_16X16, eob16, 0, 0, DC_PRED);
    svtd_write_coeffs_txb(&w, &fc, qc8, scan8, TX_8X8, eob8, 0, 0, DC_PRED);
    svtd_write_coeffs_txb(&w, &fc, qc4, scan4, TX_4X4, eob4, 0, 0, DC_PRED);
    aom_stop_encode(&w);

    if (verbose) {
        printf("ectok_eob %u %u %u\n", eob16, eob8, eob4);

        printf("ectok_bytes %u", w.pos);
        for (uint32_t i = 0; i < w.pos; ++i) printf(" %02x", buf[i]);
        printf("\n");
    }

    aom_reader r;
    if (aom_reader_init(&r, buf, w.pos)) return 2;
    r.allow_update_cdf = 1;
    TranLow rc16[256], rc8[64], rc4[16];
    int reob16 = svtd_read_coeffs_txb(&r, &fc_r, rc16, scan16, TX_16X16, 0, 0, DC_PRED);
    int reob8 = svtd_read_coeffs_txb(&r, &fc_r, rc8, scan8, TX_8X8, 0, 0, DC_PRED);
    int reob4 = svtd_read_coeffs_txb(&r, &fc_r, rc4, scan4, TX_4X4, 0, 0, DC_PRED);
    if (verbose) printf("ectok_rt %d %d %d\n", reob16, reob8, reob4);

    if (reob16 != (int)eob16 || reob8 != (int)eob8 || reob4 != (int)eob4) {
        return 3;
    }
    for (int i = 0; i < 256; ++i)
        if (rc16[i] != qc16[i]) return 4;
    for (int i = 0; i < 64; ++i)
        if (rc8[i] != qc8[i]) return 5;
    for (int i = 0; i < 16; ++i)
        if (rc4[i] != qc4[i]) return 6;

    if (memcmp(fc.txb_skip_cdf, fc_r.txb_skip_cdf, sizeof(fc.txb_skip_cdf))) return 7;
    if (memcmp(fc.dc_sign_cdf, fc_r.dc_sign_cdf, sizeof(fc.dc_sign_cdf))) return 7;
    if (memcmp(fc.coeff_base_eob_cdf, fc_r.coeff_base_eob_cdf, sizeof(fc.coeff_base_eob_cdf))) return 7;
    if (memcmp(fc.coeff_base_cdf, fc_r.coeff_base_cdf, sizeof(fc.coeff_base_cdf))) return 7;
    if (memcmp(fc.coeff_br_cdf, fc_r.coeff_br_cdf, sizeof(fc.coeff_br_cdf))) return 7;
    if (memcmp(fc.eob_extra_cdf, fc_r.eob_extra_cdf, sizeof(fc.eob_extra_cdf))) return 7;
    if (memcmp(fc.eob_flag_cdf16, fc_r.eob_flag_cdf16, sizeof(fc.eob_flag_cdf16))) return 7;
    if (memcmp(fc.eob_flag_cdf32, fc_r.eob_flag_cdf32, sizeof(fc.eob_flag_cdf32))) return 7;
    if (memcmp(fc.eob_flag_cdf64, fc_r.eob_flag_cdf64, sizeof(fc.eob_flag_cdf64))) return 7;
    if (memcmp(fc.eob_flag_cdf128, fc_r.eob_flag_cdf128, sizeof(fc.eob_flag_cdf128))) return 7;
    if (memcmp(fc.eob_flag_cdf256, fc_r.eob_flag_cdf256, sizeof(fc.eob_flag_cdf256))) return 7;
    if (memcmp(fc.eob_flag_cdf512, fc_r.eob_flag_cdf512, sizeof(fc.eob_flag_cdf512))) return 7;
    if (memcmp(fc.eob_flag_cdf1024, fc_r.eob_flag_cdf1024, sizeof(fc.eob_flag_cdf1024))) return 7;
    return 0;
}
// ---- TS3: full-frame 4x 16x16 token stream (skip=0, q100) ------------------
// 4 f16 blocks, decided modes {1,7,2,2} via the D2 policy loop (same as
// svtd_frame_auto_16x16_blocks), skip=0 for all four (no skip decision
// logic). Per block: kf_y_mode + angle_delta + filter_intra skip + tx_type
// + txb_skip + eob_pt + eob_extra + base_eob/br + base/br + signs/golomb.
// The predictor is the DECIDED mode's build_intra_predictors output (M1
// availability), NOT DC=0 Ã¢â‚¬â€ matching the l6 pipeline's residual computation.
static int svtd_ts3_drive(uint8_t* buf) {
    uint8_t src[1024];
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) src[y * 32 + x] = (y < 16) ? (uint8_t)(4 * (x + y + 1)) : 0;

    SvtdQuantTables t;
    svtd_build_quantizer_luma(100, &t);
    int16_t scan16[256];
    svtd_default_scan_16x16(scan16);

    Ts1FrameContext fc;
    ts1_init(&fc, 100);
    Ts1FrameContext fc_r;
    ts1_init(&fc_r, 100);

    uint8_t recon[1024];
    memset(recon, 0, sizeof(recon));
    int modes[4] = {0};

    AomWriter w;
    w.ec.buf = buf;
    svt_od_ec_enc_reset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;

    uint8_t above_na[16], left_na[8];
    memset(above_na, (int)INVALID_NEIGHBOR_DATA, sizeof(above_na));
    memset(left_na, (int)INVALID_NEIGHBOR_DATA, sizeof(left_na));

    static const int px[4][2] = {{0, 0}, {16, 0}, {0, 16}, {16, 16}};
    static const int mi[4][2] = {{0, 0}, {0, 4}, {4, 0}, {4, 4}};
    uint16_t eobs[4] = {0};
    static TranLow all_qc[4][256];
    for (int b = 0; b < 4; ++b) {
        const int bx = b % 2, by = b / 2;
        const int r = px[b][1], c = px[b][0];
        const int hasTop = by > 0, hasLeft = bx > 0;
        const int hasAL = hasTop && hasLeft;
        const int nTop = hasTop ? 16 : 0, nLeft = hasLeft ? 16 : 0;
        const int nTr = (hasTop && bx + 1 < 2) ? 16 : 0;
        uint8_t above[33] = {0}, left_edge[33] = {0};
        uint8_t al = 0;
        if (hasTop) svtd_gather_above(above, recon, 32, c, r, 16, nTr);
        if (hasLeft) for (int i = 0; i < 16; ++i) left_edge[i] = recon[(r + i) * 32 + c - 1];
        if (hasTop && hasLeft) al = recon[(r - 1) * 32 + c - 1];
        const int aboveMode = hasTop ? modes[(by - 1) * 2 + bx] : DC_PRED;
        const int leftMode = hasLeft ? modes[by * 2 + bx - 1] : DC_PRED;
        svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED || aboveMode == SMOOTH_H_PRED) ||
                          (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED || leftMode == SMOOTH_H_PRED)) ? 1 : 0;
        uint8_t srcblk[256];
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 16; ++j) srcblk[i * 16 + j] = src[(r + i) * 32 + c + j];
        // D2 policy
        uint32_t best_sad = 0; int mode = -1;
        for (int m = 0; m <= PAETH_PRED; ++m) {
            uint8_t pred[256];
            svtd_call_builder_tx(pred, m, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left_edge, nLeft, 0, al, TX_16X16);
            const uint32_t sad = svt_nxm_sad_kernel_helper_c(srcblk, 16, pred, 16, 16, 16);
            if (mode < 0 || sad < best_sad) { best_sad = sad; mode = m; }
        }
        modes[b] = mode;
        // decided-mode predictor (NOT DC=0)
        uint8_t pred[256];
        svtd_call_builder_tx(pred, mode, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left_edge, nLeft, 0, al, TX_16X16);
        int16_t res[256];
        for (int i = 0; i < 256; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
        int32_t cb[256];
        svtd_fwd2d16x16(res, 16, cb, svt_av1_fdct16_new);
        TranLow qc[256], dq[256];
        uint16_t eob = 0;
        svtd_quantize_fp_16x16(cb, &t, scan16, qc, dq, &eob);
        eobs[b] = eob;
        memcpy(all_qc[b], qc, sizeof(all_qc[b]));
        // recon (LOSSY at q100: quantize -> dequantize -> inverse)
        svtd_inv2dadd16x16(dq, pred, 16, svt_av1_idct16_new);
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 16; ++j) recon[(r + i) * 32 + c + j] = pred[i * 16 + j];
    }
    printf("ecfrm_modes");
    for (int b = 0; b < 4; ++b) printf(" %d", modes[b]);
    printf("\n");
    printf("ecfrm_eobs");
    for (int b = 0; b < 4; ++b) printf(" %d", (int)eobs[b]);
    printf("\n");

    // token emission loop (raster order, using the recon from the decision pass)
    for (int b = 0; b < 4; ++b) {
        const int bx = b % 2, by = b / 2;
        const int r = px[b][1], c = px[b][0];
        const int hasTop = by > 0, hasLeft = bx > 0;
        const int hasAL = hasTop && hasLeft;
        const int nTop = hasTop ? 16 : 0, nLeft = hasLeft ? 16 : 0;
        const int nTr = (hasTop && bx + 1 < 2) ? 16 : 0;
        uint8_t above[33] = {0}, left_edge[33] = {0};
        uint8_t al = 0;
        if (hasTop) svtd_gather_above(above, recon, 32, c, r, 16, nTr);
        if (hasLeft) for (int i = 0; i < 16; ++i) left_edge[i] = recon[(r + i) * 32 + c - 1];
        if (hasTop && hasLeft) al = recon[(r - 1) * 32 + c - 1];
        const int aboveMode = hasTop ? modes[(by - 1) * 2 + bx] : DC_PRED;
        const int leftMode = hasLeft ? modes[by * 2 + bx - 1] : DC_PRED;
        svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED || aboveMode == SMOOTH_H_PRED) ||
                          (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED || leftMode == SMOOTH_H_PRED)) ? 1 : 0;

        // recompute the residual using the same recon edges
        uint8_t srcblk[256];
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 16; ++j) srcblk[i * 16 + j] = src[(r + i) * 32 + c + j];
        uint8_t pred[256];
        svtd_call_builder_tx(pred, modes[b], 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left_edge, nLeft, 0, al, TX_16X16);
        int16_t res[256];
        for (int i = 0; i < 256; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
        int32_t cb[256];
        svtd_fwd2d16x16(res, 16, cb, svt_av1_fdct16_new);
        TranLow qc[256], dq[256];
        uint16_t eob = 0;
        svtd_quantize_fp_16x16(cb, &t, scan16, qc, dq, &eob);

        // 1. kf_y_mode (BSF1)
        const int top_ctx = intra_mode_context[aboveMode < 0 ? DC_PRED : (PredictionMode)aboveMode];
        const int left_ctx = intra_mode_context[leftMode < 0 ? DC_PRED : (PredictionMode)leftMode];
        aom_write_symbol(&w, modes[b], fc.kf_y_cdf[top_ctx][left_ctx], INTRA_MODES);

        // 2. angle delta for directional modes
        if (av1_is_directional_mode((PredictionMode)modes[b])) {
            aom_write_symbol(&w, MAX_ANGLE_DELTA, fc.angle_delta_cdf[modes[b] - V_PRED], 2 * MAX_ANGLE_DELTA + 1);
        }

        // 3. filter-intra: never for {1,7,3,2} (svt_aom_filter_intra_allowed is
        // DC_PRED-only, mode_decision.c:108-119; no decided mode is DC_PRED)
        // 4. tx-type: emitted INSIDE svtd_write_coeffs_txb (one symbol, after
        // txb_skip, before eob_pt) — mirroring the l7 writeTxbCoeffs structure

        // 5. token chain (TS1+TS2)
        const int eob_tok = svtd_eob_from_coeffs(qc, scan16, TX_16X16);
        const int tx_w = eb_tx_size_wide_unit[TX_16X16];
        const int tx_h = eb_tx_size_high_unit[TX_16X16];
        uint8_t* above_ptr = &above_na[px[b][0] / 4];
        uint8_t* left_ptr = &left_na[px[b][1] / 4];
        int16_t dc_sign = 0;
        if (above_ptr[0] != (uint8_t)INVALID_NEIGHBOR_DATA) {
            for (int k = 0; k < tx_w; ++k) dc_sign += ((above_ptr[k] >> COEFF_CONTEXT_BITS) == 1) ? -1 : ((above_ptr[k] >> COEFF_CONTEXT_BITS) == 2) ? 1 : 0;
        }
        if (left_ptr[0] != (uint8_t)INVALID_NEIGHBOR_DATA) {
            for (int k = 0; k < tx_h; ++k) dc_sign += ((left_ptr[k] >> COEFF_CONTEXT_BITS) == 1) ? -1 : ((left_ptr[k] >> COEFF_CONTEXT_BITS) == 2) ? 1 : 0;
        }
        const int dc_sign_ctx = dc_sign > 0 ? 2 : dc_sign < 0 ? 1 : 0;
        svtd_write_coeffs_txb(&w, &fc, qc, scan16, TX_16X16, eob_tok, 0, dc_sign_ctx,
                              (PredictionMode)modes[b]);

        // NA update
        int32_t cul = 0;
        for (int q = 0; q < eob_tok; ++q) cul += abs((int)qc[scan16[q]]);
        cul = AOMMIN(cul, COEFF_CONTEXT_MASK);
        if (eob_tok > 0) {
            if (qc[0] < 0) cul |= 1 << COEFF_CONTEXT_BITS;
            else if (qc[0] > 0) cul += 2 << COEFF_CONTEXT_BITS;
        }
        for (int k = 0; k < tx_w; ++k) above_ptr[k] = (uint8_t)cul;
        for (int k = 0; k < tx_h; ++k) left_ptr[k] = (uint8_t)cul;
    }
    aom_stop_encode(&w);
    printf("ecfrm_bytes %u", w.pos);
    for (uint32_t i = 0; i < w.pos; ++i) printf(" %02x", buf[i]);
    printf("\n");

    // read twin
    uint8_t above_r[16], left_r[8];
    memset(above_r, (int)INVALID_NEIGHBOR_DATA, sizeof(above_r));
    memset(left_r, (int)INVALID_NEIGHBOR_DATA, sizeof(left_r));
    aom_reader r;
    if (aom_reader_init(&r, buf, w.pos)) return 2;
    r.allow_update_cdf = 1;
    int rt_bad = 0;
    printf("ecfrm_rt");
    for (int b = 0; b < 4; ++b) {
        const int bx = b % 2, by = b / 2;
        // NOTE: `row`/`col`, NOT `r` — `r` here would shadow the aom_reader
        // above, and every &r read would decode from the row int as garbage.
        const int row = px[b][1], col = px[b][0];
        const int hasTop = by > 0, hasLeft = bx > 0;
        const int hasAL = hasTop && hasLeft;
        const int nTop = hasTop ? 16 : 0, nLeft = hasLeft ? 16 : 0;
        const int nTr = (hasTop && bx + 1 < 2) ? 16 : 0;
        uint8_t above[33] = {0}, left_edge[33] = {0};
        uint8_t al = 0;
        if (hasTop) svtd_gather_above(above, recon, 32, col, row, 16, nTr);
        if (hasLeft) for (int i = 0; i < 16; ++i) left_edge[i] = recon[(row + i) * 32 + col - 1];
        if (hasTop && hasLeft) al = recon[(row - 1) * 32 + col - 1];
        const int aboveMode = hasTop ? modes[(by - 1) * 2 + bx] : DC_PRED;
        const int leftMode = hasLeft ? modes[by * 2 + bx - 1] : DC_PRED;
        svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED || aboveMode == SMOOTH_H_PRED) ||
                          (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED || leftMode == SMOOTH_H_PRED)) ? 1 : 0;

        // 1. kf_y_mode read
        const int top_ctx = intra_mode_context[aboveMode < 0 ? DC_PRED : (PredictionMode)aboveMode];
        const int left_ctx = intra_mode_context[leftMode < 0 ? DC_PRED : (PredictionMode)leftMode];
        const int m = aom_read_symbol_(&r, fc_r.kf_y_cdf[top_ctx][left_ctx], INTRA_MODES);
        printf(" %d", m);
        if (m != modes[b]) rt_bad = 1;

        // 2. angle delta read
        if (av1_is_directional_mode((PredictionMode)modes[b])) {
            aom_read_symbol_(&r, fc_r.angle_delta_cdf[modes[b] - V_PRED], 2 * MAX_ANGLE_DELTA + 1);
        }

        // 4. tx-type: read INSIDE svtd_read_coeffs_txb (mirrors the write side)

        // 5. token chain read
        const int tx_w = eb_tx_size_wide_unit[TX_16X16];
        const int tx_h = eb_tx_size_high_unit[TX_16X16];
        uint8_t* above_ptr = &above_r[px[b][0] / 4];
        uint8_t* left_ptr = &left_r[px[b][1] / 4];
        int16_t dc_sign = 0;
        if (above_ptr[0] != (uint8_t)INVALID_NEIGHBOR_DATA) {
            for (int k = 0; k < tx_w; ++k) dc_sign += ((above_ptr[k] >> COEFF_CONTEXT_BITS) == 1) ? -1 : ((above_ptr[k] >> COEFF_CONTEXT_BITS) == 2) ? 1 : 0;
        }
        if (left_ptr[0] != (uint8_t)INVALID_NEIGHBOR_DATA) {
            for (int k = 0; k < tx_h; ++k) dc_sign += ((left_ptr[k] >> COEFF_CONTEXT_BITS) == 1) ? -1 : ((left_ptr[k] >> COEFF_CONTEXT_BITS) == 2) ? 1 : 0;
        }
        const int dc_sign_ctx = dc_sign > 0 ? 2 : dc_sign < 0 ? 1 : 0;
        // recompute the quantized coefficients from the same predictor
        uint8_t srcblk[256];
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 16; ++j) srcblk[i * 16 + j] = src[(row + i) * 32 + col + j];
        uint8_t pred[256];
        svtd_call_builder_tx(pred, modes[b], 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left_edge, nLeft, 0, al, TX_16X16);
        int16_t res[256];
        for (int i = 0; i < 256; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
        int32_t cb[256];
        svtd_fwd2d16x16(res, 16, cb, svt_av1_fdct16_new);
        TranLow qc[256], dq[256];
        uint16_t eob_q = 0;
        svtd_quantize_fp_16x16(cb, &t, scan16, qc, dq, &eob_q);

        TranLow rc[256];
        memset(rc, 0, sizeof(rc));
        const int reob = svtd_read_coeffs_txb(&r, &fc_r, rc, scan16, TX_16X16, 0, dc_sign_ctx,
                                              (PredictionMode)m);
        printf(" %d", reob);
        if (reob != (int)eob_q) rt_bad = 1;
        for (int i = 0; i < 256; ++i) if (rc[i] != qc[i]) rt_bad = 1;

        // NA update
        int32_t cul = 0;
        for (int q = 0; q < reob; ++q) cul += abs((int)rc[scan16[q]]);
        cul = AOMMIN(cul, COEFF_CONTEXT_MASK);
        if (reob > 0) {
            if (rc[0] < 0) cul |= 1 << COEFF_CONTEXT_BITS;
            else if (rc[0] > 0) cul += 2 << COEFF_CONTEXT_BITS;
        }
        for (int k = 0; k < tx_w; ++k) above_ptr[k] = (uint8_t)cul;
        for (int k = 0; k < tx_h; ++k) left_ptr[k] = (uint8_t)cul;
    }
    printf("\n");
    if (rt_bad) { fprintf(stderr, "TS3 roundtrip FAILED\n"); return 3; }

    int cdf_eq = 1;
    if (memcmp(fc.txb_skip_cdf, fc_r.txb_skip_cdf, sizeof(fc.txb_skip_cdf))) cdf_eq = 0;
    if (memcmp(fc.dc_sign_cdf, fc_r.dc_sign_cdf, sizeof(fc.dc_sign_cdf))) cdf_eq = 0;
    if (memcmp(fc.coeff_base_eob_cdf, fc_r.coeff_base_eob_cdf, sizeof(fc.coeff_base_eob_cdf))) cdf_eq = 0;
    if (memcmp(fc.coeff_base_cdf, fc_r.coeff_base_cdf, sizeof(fc.coeff_base_cdf))) cdf_eq = 0;
    if (memcmp(fc.coeff_br_cdf, fc_r.coeff_br_cdf, sizeof(fc.coeff_br_cdf))) cdf_eq = 0;
    if (memcmp(fc.eob_extra_cdf, fc_r.eob_extra_cdf, sizeof(fc.eob_extra_cdf))) cdf_eq = 0;
    if (memcmp(fc.eob_flag_cdf64, fc_r.eob_flag_cdf64, sizeof(fc.eob_flag_cdf64))) cdf_eq = 0;
    if (memcmp(fc.intra_ext_tx_cdf, fc_r.intra_ext_tx_cdf, sizeof(fc.intra_ext_tx_cdf))) cdf_eq = 0;
    if (memcmp(fc.kf_y_cdf, fc_r.kf_y_cdf, sizeof(fc.kf_y_cdf))) cdf_eq = 0;
    if (memcmp(fc.angle_delta_cdf, fc_r.angle_delta_cdf, sizeof(fc.angle_delta_cdf))) cdf_eq = 0;
    printf("ecfrm_cdf_eq %d\n", cdf_eq);
    // TS3c: the l6 acceptance compares its modes, eobs, coeffs and recon
    // against the generator's, bit-exact — publish coeffs and recon too
    // (raster order, 4x256 + 32x32).
    printf("ecfrm_coeffs");
    for (int b2 = 0; b2 < 4; ++b2)
        for (int i = 0; i < 256; ++i) printf(" %d", (int)all_qc[b2][i]);
    printf("\n");
    printf("ecfrm_recon");
    for (int i = 0; i < 1024; ++i) printf(" %d", (int)recon[i]);
    printf("\n");
    return 0;
}// ---- TS2: per-block txb-ctx + NA gate lines --------------------------------
// The 4-block 64x32 fixture with q100 quantized residuals, NA accumulation
// across blocks. Reuses the TS1 writer/reader + frame context.

// helper: compute eob from quantized coefficients (last nonzero scan idx + 1)
static int svtd_eob_from_coeffs(const TranLow* coeff, const int16_t* scan, TxSize tx_size) {
    const int width = get_txb_wide(tx_size);
    const int height = get_txb_high(tx_size);
    int last = 0;
    for (int i = 0; i < width * height; ++i) {
        if (coeff[i] != 0) last = i + 1;
    }
    // find the scan position of the last nonzero raster coefficient
    int eob = 0;
    for (int c = 0; c < width * height; ++c) {
        if (coeff[scan[c]] != 0) eob = c + 1;
    }
    return eob;
}

// Lossy tile v2: the v1 partition/kf-mode walk with skip = 0 (context 0 for
// every block: all neighbors coded skip = 0, unavailable -> 0 per
// av1_get_skip_context) and the TS3-proven token chain appended per leaf
// (txb_skip = 0 inside the chain + tx-type + eob + coefficients). The
// coefficients are the REAL encode: the same deterministic loop as
// svtd_ts3_drive (f16 fixture, D2 decision against the recon feedback,
// quantize_fp at q100, NA-driven dc_sign_ctx) - TS3c proved this path
// bit-exact vs the l6 pipeline.
static uint32_t svtd_bsf3_tile_data_v2(uint8_t* dst) {
    uint8_t src[1024];
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) src[y * 32 + x] = (y < 16) ? (uint8_t)(4 * (x + y + 1)) : 0;

    SvtdQuantTables t;
    svtd_build_quantizer_luma(100, &t);
    int16_t scan16[256];
    svtd_default_scan_16x16(scan16);

    Ts1FrameContext fc;
    ts1_init(&fc, 100);

    uint8_t recon[1024];
    memset(recon, 0, sizeof(recon));
    int modes[4] = {0};
    uint16_t eobs[4] = {0};
    static TranLow all_qc[4][256];

    AomWriter w;
    w.ec.buf = dst;
    svt_od_ec_enc_reset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos              = 0;

    // partition + skip cdfs (fresh defaults, same as the v1 walk)
    static AomCdfProb part_cdf[PARTITION_CONTEXTS][CDF_SIZE(EXT_PARTITION_TYPES)];
    memcpy(part_cdf, default_partition_cdf, sizeof(part_cdf));
    static AomCdfProb skip_cdf[SKIP_CONTEXTS][CDF_SIZE(2)];
    memcpy(skip_cdf, default_skip_cdfs, sizeof(skip_cdf));

    uint8_t above_pctx[8];
    uint8_t left_pctx[16];
    memset(above_pctx, (int)INVALID_NEIGHBOR_DATA, sizeof(above_pctx));
    memset(left_pctx, (int)INVALID_NEIGHBOR_DATA, sizeof(left_pctx));

    uint8_t above_na[16], left_na[8];
    memset(above_na, (int)INVALID_NEIGHBOR_DATA, sizeof(above_na));
    memset(left_na, (int)INVALID_NEIGHBOR_DATA, sizeof(left_na));

    static const int px[4][2] = {{0, 0}, {16, 0}, {0, 16}, {16, 16}};
    static const int lr[4] = {0, 0, 4, 4};
    static const int lc[4] = {0, 4, 0, 4};

    // encode loop (identical to svtd_ts3_drive): decision pass fills recon,
    // modes, eobs, coefficients
    for (int b = 0; b < 4; ++b) {
        const int bx = b % 2, by = b / 2;
        const int r = px[b][1], c = px[b][0];
        const int hasTop = by > 0, hasLeft = bx > 0;
        const int nTop = hasTop ? 16 : 0, nLeft = hasLeft ? 16 : 0;
        const int nTr = (hasTop && bx + 1 < 2) ? 16 : 0;
        uint8_t above[33] = {0}, left_edge[33] = {0};
        uint8_t al = 0;
        if (hasTop) svtd_gather_above(above, recon, 32, c, r, 16, nTr);
        if (hasLeft) for (int i = 0; i < 16; ++i) left_edge[i] = recon[(r + i) * 32 + c - 1];
        if (hasTop && hasLeft) al = recon[(r - 1) * 32 + c - 1];
        const int aboveMode = hasTop ? modes[(by - 1) * 2 + bx] : DC_PRED;
        const int leftMode = hasLeft ? modes[by * 2 + bx - 1] : DC_PRED;
        svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED || aboveMode == SMOOTH_H_PRED) ||
                          (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED || leftMode == SMOOTH_H_PRED)) ? 1 : 0;
        uint8_t srcblk[256];
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 16; ++j) srcblk[i * 16 + j] = src[(r + i) * 32 + c + j];
        uint32_t best_sad = 0; int mode = -1;
        for (int m = 0; m <= PAETH_PRED; ++m) {
            uint8_t pred[256];
            svtd_call_builder_tx(pred, m, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left_edge, nLeft, 0, al, TX_16X16);
            const uint32_t sad = svt_nxm_sad_kernel_helper_c(srcblk, 16, pred, 16, 16, 16);
            if (mode < 0 || sad < best_sad) { best_sad = sad; mode = m; }
        }
        modes[b] = mode;
        uint8_t pred[256];
        svtd_call_builder_tx(pred, mode, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left_edge, nLeft, 0, al, TX_16X16);
        int16_t res[256];
        for (int i = 0; i < 256; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
        int32_t cb[256];
        svtd_fwd2d16x16(res, 16, cb, svt_av1_fdct16_new);
        TranLow qc[256], dq[256];
        uint16_t eob = 0;
        svtd_quantize_fp_16x16(cb, &t, scan16, qc, dq, &eob);
        eobs[b] = eob;
        memcpy(all_qc[b], qc, sizeof(all_qc[b]));
        svtd_inv2dadd16x16(dq, pred, 16, svt_av1_idct16_new);
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 16; ++j) recon[(r + i) * 32 + c + j] = pred[i * 16 + j];
    }

    // tile symbol emission: partition plane + per-leaf skip(0) + kf mode +
    // angle delta + token chain (the TS3-proven per-block symbol sequence)
    EcPartState st = {&w, part_cdf, above_pctx, left_pctx, 8, 32, 0};
    const int ctx32 = ecpart_derive_ctx(st.above, st.left, 0, 0, BLOCK_32X32);
    aom_write_symbol(&w, PARTITION_SPLIT, part_cdf[ctx32], svt_aom_partition_cdf_length(BLOCK_32X32));
    for (int b = 0; b < 4; ++b) {
        const int bx = b % 2, by = b / 2;
        const int r = px[b][1], c = px[b][0];
        const int pctx = ecpart_derive_ctx(st.above, st.left, lr[b], lc[b], BLOCK_16X16);
        aom_write_symbol(&w, PARTITION_NONE, part_cdf[pctx], svt_aom_partition_cdf_length(BLOCK_16X16));
        ecpart_update_ctx(st.above, st.left, lr[b], lc[b], BLOCK_16X16);
        // skip = 0: every block codes its residual; the context is
        // above_skip + left_skip = 0 for all four (all neighbors coded
        // skip = 0, unavailable -> 0)
        aom_write_symbol(&w, 0, skip_cdf[0], 2);
        const int aboveMode = by > 0 ? modes[(by - 1) * 2 + bx] : DC_PRED;
        const int leftMode = bx > 0 ? modes[by * 2 + bx - 1] : DC_PRED;
        const int top_ctx = intra_mode_context[aboveMode < 0 ? DC_PRED : (PredictionMode)aboveMode];
        const int left_ctx = intra_mode_context[leftMode < 0 ? DC_PRED : (PredictionMode)leftMode];
        aom_write_symbol(&w, modes[b], fc.kf_y_cdf[top_ctx][left_ctx], INTRA_MODES);
        if (av1_is_directional_mode((PredictionMode)modes[b])) {
            aom_write_symbol(&w, MAX_ANGLE_DELTA, fc.angle_delta_cdf[modes[b] - V_PRED],
                             2 * MAX_ANGLE_DELTA + 1);
        }
        // token chain (TS1+TS2+TS3): txb_skip_ctx = 0 (whole-block TU),
        // dc_sign_ctx from the NA sweep, intra_dir = the decided mode
        const int tx_w = eb_tx_size_wide_unit[TX_16X16];
        const int tx_h = eb_tx_size_high_unit[TX_16X16];
        uint8_t* above_ptr = &above_na[px[b][0] / 4];
        uint8_t* left_ptr = &left_na[px[b][1] / 4];
        int16_t dc_sign = 0;
        if (above_ptr[0] != (uint8_t)INVALID_NEIGHBOR_DATA) {
            for (int k = 0; k < tx_w; ++k) dc_sign += ((above_ptr[k] >> COEFF_CONTEXT_BITS) == 1) ? -1 : ((above_ptr[k] >> COEFF_CONTEXT_BITS) == 2) ? 1 : 0;
        }
        if (left_ptr[0] != (uint8_t)INVALID_NEIGHBOR_DATA) {
            for (int k = 0; k < tx_h; ++k) dc_sign += ((left_ptr[k] >> COEFF_CONTEXT_BITS) == 1) ? -1 : ((left_ptr[k] >> COEFF_CONTEXT_BITS) == 2) ? 1 : 0;
        }
        const int dc_sign_ctx = dc_sign > 0 ? 2 : dc_sign < 0 ? 1 : 0;
        const int eob_tok = svtd_eob_from_coeffs(all_qc[b], scan16, TX_16X16);
        svtd_write_coeffs_txb(&w, &fc, all_qc[b], scan16, TX_16X16, eob_tok, 0, dc_sign_ctx,
                              (PredictionMode)modes[b]);
        if (eob_tok != (int)eobs[b]) { fprintf(stderr, "TS4 tile v2 eob mismatch\n"); return 0; }
        // NA update (packed cul_level + dc sign, over the TU MI extent)
        int32_t cul = 0;
        for (int q = 0; q < eob_tok; ++q) cul += abs((int)all_qc[b][scan16[q]]);
        cul = AOMMIN(cul, COEFF_CONTEXT_MASK);
        if (eob_tok > 0) {
            if (all_qc[b][0] < 0) cul |= 1 << COEFF_CONTEXT_BITS;
            else if (all_qc[b][0] > 0) cul += 2 << COEFF_CONTEXT_BITS;
        }
        for (int k = 0; k < tx_w; ++k) above_ptr[k] = (uint8_t)cul;
        for (int k = 0; k < tx_h; ++k) left_ptr[k] = (uint8_t)cul;
        (void)r; (void)c;
    }
    aom_stop_encode(&w);
    return w.pos;
}

// ---- TD0: decoder-conformance bisect ladder --------------------------------
// One 16x16 frame, ONE 16x16 block at (0,0) (fresh contexts everywhere):
// 64x64 SB forced SPLIT (no symbol), 32x32(0,0) forced SPLIT (no symbol:
// has_rows/has_cols false at mi 4x4), 16x16(0,0) coded partition NONE
// (fresh ctx = the ECP1-measured 4), then per rung the block symbols.
// Each rung isolates one wire surface; the decode oracle (ffmpeg's
// libdav1d/libaom - the only conformant decoders available locally) is the
// arbiter. Rungs: (a) q0 header, skip=1, DC_PRED, NO chain - partition/
// skip/kf-mode/mode-ctx plus the FI flag (aom's av1_filter_intra_allowed =
// DC_PRED && bsize <= 32x32, reconintra.h:68-80 - the flag IS read for
// DC_PRED blocks; our writer writes it, value 0); (b) q100 header, skip=0,
// eob=0 TU (the txb_skip symbol only, no tx-type via the eob==0 early
// return); (c) + real tokens eob=5, levels <= 8 (the base/br/sign/
// eob_extra/tx-type surface, no golomb); (d) + a golomb-class level (DC 20
// >= MAX_BASE_BR_RANGE 15; 20*dc_q(100) = 18840 stays under the +/-32767
// dqcoeff clamp) + the angle-delta symbol (mode D203, delta 0).
#define TD0_RUNG_A 0
#define TD0_RUNG_B 1
#define TD0_RUNG_C 2
#define TD0_RUNG_D 3
#define TD0_RUNG_E 4
#define TD0_RUNG_F 5
#define TD0_RUNG_G 6

// TD1: writer-arm state trace. Set before a rung tile write; prints one line
// per symbol with the od_ec encoder state AFTER the encode. The decoder-arm
// harness (out-of-tree, aom entdec verbatim) prints the same tags so the two
// arms align state-for-state.
// rung block parameters: mode, skip flag, chain coefficients (raster)
static void svtd_td0_block_params(int rung, int* mode, int* skip, TranLow qc[256]) {
    memset(qc, 0, sizeof(TranLow) * 256);
    switch (rung) {
    case TD0_RUNG_E: *mode = V_PRED; break;
    case TD0_RUNG_D: *mode = D203_PRED; break;
    default: *mode = DC_PRED; break;
    }
    *skip = (rung == TD0_RUNG_A || rung == TD0_RUNG_E) ? 1 : 0;
    if (rung == TD0_RUNG_F || rung == TD0_RUNG_G) {
        int16_t scan[256];
        svtd_default_scan_16x16(scan);
        // DC level 3: dqcoeff 3*dc_q(100)=279 -> +1 pixel shift (VISIBLE);
        // the tile keeps every dqcoeff under the +/-32767 clamp.
        qc[scan[0]] = 3;
        if (rung == TD0_RUNG_G) qc[scan[1]] = 3;  // +1 br symbol + the raw sign bit
    }
    if (rung == TD0_RUNG_C || rung == TD0_RUNG_D) {
        int16_t scan[256];
        svtd_default_scan_16x16(scan);
        const int lev[5] = { 3, 8, 5, 3, 2 };
        const int sgn[5] = { 1, 0, 0, 1, 0 };
        for (int c = 0; c < 5; ++c) qc[scan[c]] = sgn[c] ? -lev[c] : lev[c];
        if (rung == TD0_RUNG_D) qc[0] = -20;  // golomb-class level (20 >= 15)
    }
}

static uint32_t svtd_td0_tile(int rung, uint8_t* dst) {
    static char tagbuf[8];
    tagbuf[0] = "abcdefg"[rung];
    tagbuf[1] = ':';
    tagbuf[2] = '\0';
    svtd_trace_tag = tagbuf;
    svtd_trace = 1;
    static AomCdfProb part_cdf[PARTITION_CONTEXTS][CDF_SIZE(EXT_PARTITION_TYPES)];
    memcpy(part_cdf, default_partition_cdf, sizeof(part_cdf));
    static AomCdfProb skip_cdf[SKIP_CONTEXTS][CDF_SIZE(2)];
    memcpy(skip_cdf, default_skip_cdfs, sizeof(skip_cdf));
    static AomCdfProb kf_y_cdf[KF_MODE_CONTEXTS][KF_MODE_CONTEXTS][CDF_SIZE(INTRA_MODES)];
    memcpy(kf_y_cdf, svt_aom_default_kf_y_mode_cdf, sizeof(kf_y_cdf));
    static AomCdfProb angle_cdf[DIRECTIONAL_MODES][CDF_SIZE(2 * MAX_ANGLE_DELTA + 1)];
    memcpy(angle_cdf, default_angle_delta_cdf, sizeof(angle_cdf));
    static AomCdfProb fi_cdf[CDF_SIZE(2)];
    memcpy(fi_cdf, default_filter_intra_cdfs[BLOCK_16X16], sizeof(fi_cdf));
    Ts1FrameContext fc;
    // TD5a: the rung's own qindex - a/e carry the lossless q0 header, b..g
    // the lossy q100 header; the coefficient CDF bucket follows (spec
    // init_coeff_cdfs).
    const int td0_qidx = (rung == TD0_RUNG_A || rung == TD0_RUNG_E) ? 0 : 100;
    ts1_init(&fc, td0_qidx);

    AomWriter w;
    w.ec.buf = dst;
    svt_od_ec_enc_reset(&w.ec);
    w.allow_update_cdf = svtd_td0_adapt_probe;  // TD3a probe: 1 normally
    w.pos              = 0;

    uint8_t above_pctx[8];
    uint8_t left_pctx[16];
    memset(above_pctx, (int)INVALID_NEIGHBOR_DATA, sizeof(above_pctx));
    memset(left_pctx, (int)INVALID_NEIGHBOR_DATA, sizeof(left_pctx));

    // partition walk (fresh contexts): 64x64 forced SPLIT, 32x32(0,0) forced
    // SPLIT, 16x16(0,0) coded NONE
    EcPartState st = {&w, part_cdf, above_pctx, left_pctx, 4, 16, 0};
    const int pctx = ecpart_derive_ctx(st.above, st.left, 0, 0, BLOCK_16X16);
    svtd_script_row("part", PARTITION_NONE, part_cdf[pctx],
                    svt_aom_partition_cdf_length(BLOCK_16X16));
    aom_write_symbol(&w, PARTITION_NONE, part_cdf[pctx], svt_aom_partition_cdf_length(BLOCK_16X16));
    svtd_tr("part", PARTITION_NONE, &w.ec);

    int mode, skip;
    TranLow qc[256];
    svtd_td0_block_params(rung, &mode, &skip, qc);
    // skip flag, ctx 0 (fresh: unavailable neighbors -> 0)
    svtd_script_row("skip", skip, skip_cdf[0], 2);
    aom_write_symbol(&w, skip, skip_cdf[0], 2);
    svtd_tr("skip", skip, &w.ec);

    // kf mode (both contexts = intra_mode_context[DC_PRED], fresh)
    const int top_ctx = intra_mode_context[DC_PRED];
    const int left_ctx = intra_mode_context[DC_PRED];
    svtd_script_row("mode", mode, kf_y_cdf[top_ctx][left_ctx], INTRA_MODES);
    aom_write_symbol(&w, mode, kf_y_cdf[top_ctx][left_ctx], INTRA_MODES);
    svtd_tr("mode", mode, &w.ec);
    if (av1_is_directional_mode((PredictionMode)mode)) {
        svtd_script_row("delta", MAX_ANGLE_DELTA, angle_cdf[mode - V_PRED],
                        2 * MAX_ANGLE_DELTA + 1);
        aom_write_symbol(&w, MAX_ANGLE_DELTA, angle_cdf[mode - V_PRED],
                         2 * MAX_ANGLE_DELTA + 1);
        svtd_tr("delta", MAX_ANGLE_DELTA, &w.ec);
    }
    // filter-intra flag: read for DC_PRED blocks (aom reconintra.h:68-80);
    // the symbol is written exactly when the decoder reads it (mode DC).
    if (mode == DC_PRED) {
        svtd_script_row("fi", 0, fi_cdf, 2);
        aom_write_symbol(&w, 0, fi_cdf, 2);
        svtd_tr("fi", 0, &w.ec);
    }
    // token chain: the svtd twin - [txb_skip][tx-type for q>0 eob>0][eob_pt]
    // [eob_extra][base_eob+br][reverse][signs+golomb]
    if (skip == 0) {
        int16_t scan[256];
        svtd_default_scan_16x16(scan);
        const int eob_tok = svtd_eob_from_coeffs(qc, scan, TX_16X16);
        const int wantEob[7] = { 0, 0, 5, 5, 0, 1, 2 };
        if (eob_tok != wantEob[rung]) { fprintf(stderr, "TD0 rung %d eob %d\n", rung, eob_tok); return 0; }
        svtd_write_coeffs_txb(&w, &fc, qc, scan, TX_16X16, eob_tok, 0, 0, (PredictionMode)mode);
    }
    svtd_trace = 0;
    aom_stop_encode(&w);

    // TD0 probe: read the tile back with the aom-entdec reader (the decoder
    // oracle's own primitives) and print the decoded symbol sequence —
    // whatever THIS reads is what a conformant decoder reads.
    {
        aom_reader r;
        if (aom_reader_init(&r, dst, w.pos)) { fprintf(stderr, "TD0 reader init\n"); return 0; }
        r.allow_update_cdf = 1;  // the writer adapted; the decoder must too
        static AomCdfProb rp[PARTITION_CONTEXTS][CDF_SIZE(EXT_PARTITION_TYPES)];
        memcpy(rp, default_partition_cdf, sizeof(rp));
        static AomCdfProb rs[SKIP_CONTEXTS][CDF_SIZE(2)];
        memcpy(rs, default_skip_cdfs, sizeof(rs));
        static AomCdfProb rk[KF_MODE_CONTEXTS][KF_MODE_CONTEXTS][CDF_SIZE(INTRA_MODES)];
        memcpy(rk, svt_aom_default_kf_y_mode_cdf, sizeof(rk));
        static AomCdfProb ra[DIRECTIONAL_MODES][CDF_SIZE(2 * MAX_ANGLE_DELTA + 1)];
        memcpy(ra, default_angle_delta_cdf, sizeof(ra));
        static AomCdfProb rfi[CDF_SIZE(2)];
        memcpy(rfi, default_filter_intra_cdfs[BLOCK_16X16], sizeof(rfi));
        Ts1FrameContext fc2;
        ts1_init(&fc2, td0_qidx);
        fprintf(stderr, "TD0 rung %d rt:", rung);
        const int pctx2 = ecpart_derive_ctx(above_pctx, left_pctx, 0, 0, BLOCK_16X16);
        fprintf(stderr, " part=%d", aom_read_symbol_(&r, rp[pctx2], svt_aom_partition_cdf_length(BLOCK_16X16)));
        fprintf(stderr, " skip=%d", aom_read_symbol_(&r, rs[0], 2));
        const int rmode = aom_read_symbol_(&r, rk[top_ctx][left_ctx], INTRA_MODES);
        fprintf(stderr, " mode=%d", rmode);
        if (av1_is_directional_mode((PredictionMode)rmode)) {
            fprintf(stderr, " delta=%d", aom_read_symbol_(&r, ra[rmode - V_PRED], 2 * MAX_ANGLE_DELTA + 1));
        }
        if (rmode == DC_PRED) fprintf(stderr, " fi=%d", aom_read_symbol_(&r, rfi, 2));
        if (skip == 0) {
            int16_t scan2[256];
            svtd_default_scan_16x16(scan2);
            uint8_t above_na2[16], left_na2[8];
            memset(above_na2, (int)INVALID_NEIGHBOR_DATA, sizeof(above_na2));
            memset(left_na2, (int)INVALID_NEIGHBOR_DATA, sizeof(left_na2));
            TranLow rc[256];
            memset(rc, 0, sizeof(rc));
            const int reob = svtd_read_coeffs_txb(&r, &fc2, rc, scan2, TX_16X16, 0, 0,
                                                  (PredictionMode)rmode);
            fprintf(stderr, " reob=%d", reob);
            for (int c2 = 0; c2 < 5; ++c2) fprintf(stderr, " c%d=%d", c2, (int)rc[scan2[c2]]);
        }
        fprintf(stderr, " (pos %u/%u)\n", (uint32_t)od_ec_dec_tell(&r.ec), w.pos);
    }
    return w.pos;
}

// expected decoded picture: the decoder reconstructs pred + inv(dqcoeff)
// with its normative integer inverse transform and clamps to [0,255]; all
// rung dqcoeffs stay under the +/-32767 clamp so the IDCT inputs match.
static void svtd_td0_expected(int rung, uint8_t* pic) {
    int mode, skip;
    TranLow qc[256];
    svtd_td0_block_params(rung, &mode, &skip, qc);
    uint8_t pred[256];
    memset(pred, 0, sizeof(pred));
    svtd_call_builder_tx(pred, mode, 0, FILTER_INTRA_MODES, 0, NULL, 0, 0, NULL, 0, 0, 0, TX_16X16);
    SvtdQuantTables t;
    svtd_build_quantizer_luma(100, &t);
    TranLow dq[256];
    for (int i = 0; i < 256; ++i) dq[i] = qc[i] * t.dequant[i != 0];
    svtd_inv2dadd16x16(dq, pred, 16, svt_av1_idct16_new);
    memcpy(pic, pred, 256);
}

static uint32_t svtd_td0_tu(int rung, uint8_t* tu, uint8_t* pic) {
    static uint8_t tile_buf[512];
    static uint8_t sps_buf[64];
    static uint8_t obu_buf[256];
    memset(tile_buf, 0, sizeof(tile_buf));
    memset(sps_buf, 0, sizeof(sps_buf));
    memset(obu_buf, 0, sizeof(obu_buf));
    const uint32_t tile_size = svtd_td0_tile(rung, tile_buf);
    if (tile_size == 0) return 0;
    const uint32_t sps_size = svtd_bsf3_encode_sps_dims(sps_buf, 16);
    svtd_td0_expected(rung, pic);
    memset(tu, 0, 128);
    svt_aom_encode_td_av1(tu);
    memcpy(tu + 2, sps_buf, sps_size);
    uint32_t offset = 2 + sps_size;
    // rung a/e keep the lossless q0 header (skip=1, no TUs read); rungs
    // b/c/d/f/g carry the lossy q100 header (40 bits, LARGEST, tx-type gate
    // live, q100 dequant).
    uint32_t obu_size;
    if (rung == TD0_RUNG_A || rung == TD0_RUNG_E) {
        obu_size = svtd_bsf3_frame_obu(tu + offset, tile_buf, tile_size);
    } else {
        obu_size = svtd_bsf3_frame_obu_v2(tu + offset, tile_buf, tile_size);
    }
    if (svtd_script) {
        fprintf(stderr, "T %c %u", "abcdefg"[rung], tile_size);
        for (uint32_t i = 0; i < tile_size; ++i) fprintf(stderr, " %02x", tile_buf[i]);
        fprintf(stderr, "\n");
    }
    return offset + obu_size;
}

// ---- TD2: context-equality gate (exhaustive) -------------------------------
// For TX_CLASS_2D at TX_4X4 / TX_8X8 / TX_16X16 (range rule: every size in
// our TxType scope), enumerate the REACHABLE (pos, stats) state space and
// assert bit-exact equality between the extracted SVT C context functions
// and the l7 ported twins (linked in-process via l7_ctx_shim.cpp).
// Reachability arguments: get_nz_mag CLIP_MAX3-clips every read cell to 3,
// so the 2D mag domain is exactly [0, 15] (5 cells x {0..3}); every value in
// [0, 15] is reachable. get_br_ctx reads 3 cells RAW (no pre-clip) with the
// post-sum (mag+1)>>1 AOMMIN 6 - the sum saturates at 12, so the {0..4}^3
// sweep covers sums 0..12 fully and the belt patterns pin the saturation.
// The level buffers are poisoned before each call so unwritten regions
// (stride/padding/END) participate in the comparison.

// l7 twin wrappers (l7_ctx_shim.cpp, extern "C"): the ported twins the gate
// asserts against.
int l7_getBrCtxEob(int c, int bwl, int tx_class);
int l7_getBrCtx(const uint8_t* levels, int c, int bwl, int tx_class);
void l7_txbInitLevels(const int32_t* coeff, int width, int height, uint8_t* levels);
// TD2c: the golomb + raw-sign surface (void* = the l7 struct pointers; the
// l7 layouts mirror the vendored bitstream_unit.h structs)
void l7_writeGolomb(void* w, int level);
int l7_readGolombFromBytes(const unsigned char* buffer, unsigned size, int* out);



int l7_signtrace(const unsigned char* buffer, unsigned size, int dcsign0, int* out);

static uint64_t svtd_ctx_fnv;
static int svtd_ctx_count;
static int svtd_ctx_bad;

static void svtd_ctx_ck(const char* tag, int svt, int l7v, int tx, int pos, int stats) {
    svtd_ctx_count++;
    svtd_ctx_fnv ^= (uint64_t)(uint32_t)svt;
    svtd_ctx_fnv *= 1099511628211ULL;
    if (svt != l7v) {
        svtd_ctx_bad = 1;
        fprintf(stderr, "CTXGATE MISMATCH tx=%d tag=%s pos=%d stats=%d svt=%d l7=%d\n", tx, tag,
                pos, stats, svt, l7v);
    }
}

static void svtd_ctxgate_init(void) {
    svtd_ctx_fnv = 1469598103934665603ULL;
    svtd_ctx_count = 0;
    svtd_ctx_bad = 0;
}

static int svtd_ctxgate_size(int tx) {
    const int bwl    = get_txb_bwl(tx);
    const int w      = get_txb_wide(tx);
    const int h      = get_txb_high(tx);
    const int n      = w * h;
    const int stride = (1 << bwl) + TX_PAD_HOR;
    static uint8_t buf[TX_PAD_2D];

    // A. get_lower_levels_ctx_eob over scan_idx [0, n)
    for (int s = 0; s < n; ++s) {
        const int a = get_lower_levels_ctx_eob(bwl, h, s);
        const int b = l7_getLowerLevelsCtxEob(bwl, h, s);
        svtd_ctx_ck("eob_ll", a, b, tx, s, 0);
    }
    // B. get_br_ctx_eob over pos [0, n)
    for (int p = 0; p < n; ++p) {
        const int a = get_br_ctx_eob(p, bwl, TX_CLASS_2D);
        const int b = l7_getBrCtxEob(p, bwl, TX_CLASS_2D);
        svtd_ctx_ck("eob_br", a, b, tx, p, 0);
    }
    // C. get_nz_map_ctx_from_stats over (pos, stats), stats [0, 31]
    // (reachable clipped domain [0, 15]; [16, 31] = defined-but-unreachable belt)
    for (int p = 0; p < n; ++p) {
        for (int st = 0; st <= 31; ++st) {
            const int a = get_nz_map_ctx_from_stats(st, p, bwl, tx, TX_CLASS_2D);
            const int b = l7_getNzMapCtxFromStats(st, p, bwl, tx, TX_CLASS_2D);
            svtd_ctx_ck("fromstats", a, b, tx, p, st);
        }
    }
    // D. get_nz_mag + get_lower_levels_ctx over the clipped-reachable cell
    // space. The 2D reader reads 5 cells relative to the padded index:
    //   o0 = 1; o1 = stride; o2 = stride + 1; o3 = 2;
    //   o4 = (2 << bwl) + (2 << TX_PAD_HOR_LOG2)
    // Each cell clips to 3, so {0,1,2,3}^5 sweeps every reachable (pos,
    // stats) state; the belt combos pin the clip3 at 4/127/255.
    for (int p = 0; p < n; ++p) {
        const int base = get_padded_idx(p, bwl);
        for (int combo = 0; combo < 1024 + 3; ++combo) {
            memset(buf, 0, sizeof(buf));
            if (combo < 1024) {
                int d[5];
                int cc = combo;
                for (int di = 0; di < 5; ++di) { d[di] = cc & 3; cc >>= 2; }
                buf[base + 1] = (uint8_t)d[0];
                buf[base + stride] = (uint8_t)d[1];
                buf[base + stride + 1] = (uint8_t)d[2];
                buf[base + 2] = (uint8_t)d[3];
                buf[base + (2 << bwl) + (2 << TX_PAD_HOR_LOG2)] = (uint8_t)d[4];
            } else {
                const int v = (combo == 1024) ? 4 : (combo == 1025) ? 127 : 255;
                buf[base + 1] = (uint8_t)v;
                buf[base + stride] = (uint8_t)v;
                buf[base + stride + 1] = (uint8_t)v;
                buf[base + 2] = (uint8_t)v;
                buf[base + (2 << bwl) + (2 << TX_PAD_HOR_LOG2)] = (uint8_t)v;
            }
            const int mag_s = get_nz_mag(buf + base, bwl, TX_CLASS_2D);
            const int mag_l = l7_getNzMag(buf + base, bwl, TX_CLASS_2D);
            svtd_ctx_ck("mag", mag_s, mag_l, tx, p, combo);
            const int ctx_s = get_lower_levels_ctx(buf, p, bwl, tx, TX_CLASS_2D);
            const int ctx_l = l7_getLowerLevelsCtx(buf, p, bwl, tx, TX_CLASS_2D);
            svtd_ctx_ck("ll", ctx_s, ctx_l, tx, p, combo);
        }
    }
    // F. get_br_ctx over the raw-sum sweep: 3 cells read RAW at
    //   base + 1, base + stride, base + stride + 1
    // {0..4}^3 covers sums 0..12 (the (mag+1)>>1 min-6 saturation point);
    // belt patterns pin the saturation and the 127/255 raw-byte behavior.
    for (int p = 0; p < n; ++p) {
        const int base = get_padded_idx(p, bwl);
        for (int combo = 0; combo < 125 + 5; ++combo) {
            memset(buf, 0, sizeof(buf));
            if (combo < 125) {
                int d[3];
                int cc = combo;
                for (int di = 0; di < 3; ++di) { d[di] = cc % 5; cc /= 5; }
                buf[base + 1] = (uint8_t)d[0];
                buf[base + stride] = (uint8_t)d[1];
                buf[base + stride + 1] = (uint8_t)d[2];
            } else {
                static const int belts[5][3] = { {0, 0, 5}, {0, 0, 15}, {0, 0, 127},
                                                 {0, 0, 255}, {127, 127, 127} };
                const int bi = combo - 125;
                buf[base + 1] = (uint8_t)belts[bi][0];
                buf[base + stride] = (uint8_t)belts[bi][1];
                buf[base + stride + 1] = (uint8_t)belts[bi][2];
            }
            const int a = get_br_ctx(buf, p, bwl, TX_CLASS_2D);
            const int b = l7_getBrCtx(buf, p, bwl, TX_CLASS_2D);
            svtd_ctx_ck("br", a, b, tx, p, combo);
        }
    }
    // G. txb_init_levels: full-buffer state equality over the value sweep
    // (per-cell clamp map: identical buffers prove stride/padding/offsets for
    // the geometry; the sweep covers the reachable coefficient domain edges).
    {
        static TranLow coeff[1024];
        static uint8_t lvs[2][TX_PAD_2D];
        static const int sweep[] = { 0, 1, 2, 3, 4, 15, 16, 127, 128, 255, 1000,
                                     -1, -3, -127, -128, -1000 };
        const int nvals = (int)(sizeof(sweep) / sizeof(sweep[0]));
        for (int vi = 0; vi < nvals; ++vi) {
            for (int i = 0; i < n; ++i) coeff[i] = sweep[vi];
            memset(lvs[0], 0xA5, TX_PAD_2D);
            memset(lvs[1], 0xA5, TX_PAD_2D);
            svt_av1_txb_init_levels_c(coeff, w, h, lvs[0]);
            l7_txbInitLevels(coeff, w, h, lvs[1]);
            svtd_ctx_count++;
            svtd_ctx_fnv ^= 0x1F2E3D4C5B6A7988ULL;
            svtd_ctx_fnv *= 1099511628211ULL;
            if (memcmp(lvs[0], lvs[1], TX_PAD_2D) != 0) {
                svtd_ctx_bad = 1;
                int first = -1;
                for (int k = 0; k < TX_PAD_2D; ++k) {
                    if (lvs[0][k] != lvs[1][k]) { first = k; break; }
                }
                fprintf(stderr, "CTXGATE MISMATCH tx=%d tag=init val=%d first-byte=%d svt=%d l7=%d\n",
                        tx, sweep[vi], first,
                        first >= 0 ? (int)lvs[0][first] : -1,
                        first >= 0 ? (int)lvs[1][first] : -1);
            }
        }
    }
    return svtd_ctx_bad;
}

// TD2c: the golomb + raw-sign exhaustive gate. Golomb: enumerate the reachable
// level range g in [0, 65535] (the wire value = abs(level) - 14, reachable up
// to the u16 wire clamp and beyond the 8-bit transform domain; read_golomb's
// 20-bit length cap admits x up to ~2M so 65535 is well inside). Asserts:
// (a) our writeGolomb emits BYTES identical to the SVT write_golomb extract,
// (b) our readGolomb decodes the SVT-written stream back to g. The l7 side
// runs in its own structs (the layouts differ structurally from the vendored
// reader - named in the report); the exchange is bytes/values only.
static int svtd_golombgate(void) {
    static uint8_t b1[64], b2[64];
    int rc = 0;
    for (int g = 0; g <= 65535; ++g) {
        AomWriter w1;
        w1.ec.buf = b1;
        svt_od_ec_enc_reset(&w1.ec);
        w1.allow_update_cdf = 1;
        w1.pos = 0;
        write_golomb(&w1, g);
        aom_stop_encode(&w1);

        unsigned w2pos = 0;
        const int wrc = l7_writeGolombToBuf(g, b2, sizeof(b2), &w2pos);
        if (wrc) {
            svtd_ctx_bad = 1;
            fprintf(stderr, "GOLGATE L7-WRITE rc=%d g=%d\n", wrc, g);
            rc = 2;
            break;
        }

        svtd_ctx_count++;
        if (w1.pos != w2pos || memcmp(b1, b2, w1.pos) != 0) {
            svtd_ctx_bad = 1;
            if (!rc) {
                fprintf(stderr, "GOLGATE WRITE MISMATCH g=%d svt=%u l7=%u b0=%02x/%02x\n", g,
                        (unsigned)w1.pos, (unsigned)w2pos,
                        w1.pos ? (unsigned)b1[0] : 0, w2pos ? (unsigned)b2[0] : 0);
                rc = 1;
            }
        }
        svtd_ctx_fnv ^= (uint64_t)(uint32_t)w1.pos;
        svtd_ctx_fnv *= 1099511628211ULL;
        svtd_ctx_fnv ^= (uint64_t)(uint32_t)g;
        svtd_ctx_fnv *= 1099511628211ULL;

        // read side: our readGolomb over the SVT-written bytes
        int v = -1;
        if (l7_readGolombFromBytes(b1, w1.pos, &v)) {
            svtd_ctx_bad = 1;
            fprintf(stderr, "GOLGATE READ-INIT FAIL g=%d\n", g);
            rc = 4;
            break;
        }
        svtd_ctx_count++;
        svtd_ctx_fnv ^= (uint64_t)(uint32_t)v;
        svtd_ctx_fnv *= 1099511628211ULL;
        if (v != g) {
            svtd_ctx_bad = 1;
            if (rc == 0 || rc == 1) {
                fprintf(stderr, "GOLGATE READ MISMATCH g=%d read=%d\n", g, v);
                rc = 3;
            }
        }
    }
    return rc;
}

// raw-sign positions: (dc-sign cdf symbol, raw bool-eq bit) pairs written by
// the SVT extracts, read back through OUR l7 reader (bytes-exchanged only -
// the shim runs the l7 codec locally). The two sign surfaces in the token
// chain: the c==0 dc-sign symbol and the c>0 raw aom_read_bit.
static int svtd_signgate(void) {
    static uint8_t b1[64];
    int rc = 0;
    static const AomCdfProb dcsign_row[CDF_SIZE(2)] = { AOM_CDF2(16384) };
    for (int pat = 0; pat < 4; ++pat) {
        AomWriter w;
        w.ec.buf = b1;
        svt_od_ec_enc_reset(&w.ec);
        w.allow_update_cdf = 0;
        w.pos = 0;
        aom_write_bit(&w, pat & 1);
        aom_write_symbol(&w, (pat >> 1) & 1, (AomCdfProb*)dcsign_row, 2);
        aom_write_bit(&w, (pat >> 1) & 1);
        aom_stop_encode(&w);

        int out[4] = { -1, -1, -1, -1 };
        if (l7_signtrace(b1, w.pos, dcsign_row[0], out)) return 2;
        svtd_ctx_count++;
        svtd_ctx_fnv ^= (uint64_t)(uint32_t)((out[0] << 8) | (out[1] << 4) | (out[2] << 2) | out[3]);
        svtd_ctx_fnv *= 1099511628211ULL;
        if (out[0] != (pat & 1) || out[1] != ((pat >> 1) & 1) || out[2] != ((pat >> 1) & 1) ||
            out[3] != (pat & 1)) {
            svtd_ctx_bad = 1;
            fprintf(stderr, "SIGN GATE MISMATCH pat=%d got b0=%d sym=%d b1=%d b2=%d\n", pat, out[0],
                    out[1], out[2], out[3]);
            rc = 1;
        }
    }
    return rc;
}

static int svtd_ts2_drive(uint8_t* buf) {
    // 64x32 frame from the f16 fixture pattern
    uint8_t src[2048];
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x) src[y * 64 + x] = (y < 16) ? (uint8_t)(4 * (x % 32 + y + 1)) : 0;

    SvtdQuantTables t;
    svtd_build_quantizer_luma(100, &t);
    int16_t scan16[256];
    svtd_default_scan_16x16(scan16);

    Ts1FrameContext fc;
    ts1_init(&fc, 100);
    Ts1FrameContext fc_r;
    ts1_init(&fc_r, 100);

    // dc-sign-level NA: above[16] (64px / 4), left[8] (32px / 4)
    uint8_t above_na[16];
    uint8_t left_na[8];
    memset(above_na, (int)INVALID_NEIGHBOR_DATA, sizeof(above_na));
    memset(left_na, (int)INVALID_NEIGHBOR_DATA, sizeof(left_na));

    AomWriter w;
    w.ec.buf = buf;
    svt_od_ec_enc_reset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;

    // 2x2 grid of 16x16 blocks, raster order
    static const int mi_pos[4][2] = {{0, 0}, {0, 4}, {4, 0}, {4, 4}};
    static const int px_pos[4][2] = {{0, 0}, {16, 0}, {0, 16}, {16, 16}};
    printf("ecblk_ctx");
    for (int b = 0; b < 4; ++b) {
        const int r = px_pos[b][1], c = px_pos[b][0];
        uint8_t srcblk[256];
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 16; ++j) srcblk[i * 16 + j] = src[(r + i) * 64 + c + j];
        int16_t res[256];
        for (int i = 0; i < 256; ++i) res[i] = (int16_t)srcblk[i];  // DC pred = 0
        int32_t cb[256];
        svtd_fwd2d16x16(res, 16, cb, svt_av1_fdct16_new);
        TranLow qc[256], dq[256];
        uint16_t eob = 0;
        svtd_quantize_fp_16x16(cb, &t, scan16, qc, dq, &eob);

        // derive txb ctx (simplified get_txb_ctx, whole-block 16x16 in 64x32)
        const int tx_w = eb_tx_size_wide_unit[TX_16X16];
        const int tx_h = eb_tx_size_high_unit[TX_16X16];
        uint8_t* above_ptr = &above_na[px_pos[b][0] / 4];
        uint8_t* left_ptr = &left_na[px_pos[b][1] / 4];
        int16_t dc_sign = 0;
        if (above_ptr[0] != (uint8_t)INVALID_NEIGHBOR_DATA) {
            for (int k = 0; k < tx_w; ++k) dc_sign += ((above_ptr[k] >> COEFF_CONTEXT_BITS) == 1) ? -1 : ((above_ptr[k] >> COEFF_CONTEXT_BITS) == 2) ? 1 : 0;
        }
        if (left_ptr[0] != (uint8_t)INVALID_NEIGHBOR_DATA) {
            for (int k = 0; k < tx_h; ++k) dc_sign += ((left_ptr[k] >> COEFF_CONTEXT_BITS) == 1) ? -1 : ((left_ptr[k] >> COEFF_CONTEXT_BITS) == 2) ? 1 : 0;
        }
        const int dc_sign_ctx = dc_sign > 0 ? 2 : dc_sign < 0 ? 1 : 0;
        printf(" %d", dc_sign_ctx);

        const int eob_tok = svtd_eob_from_coeffs(qc, scan16, TX_16X16);
        svtd_write_coeffs_txb(&w, &fc, qc, scan16, TX_16X16, eob_tok, 0, dc_sign_ctx, DC_PRED);

        // NA update
        int32_t cul = 0;
        for (int q = 0; q < eob_tok; ++q) cul += abs((int)qc[scan16[q]]);
        cul = AOMMIN(cul, COEFF_CONTEXT_MASK);
        if (eob_tok > 0) {
            if (qc[0] < 0) cul |= 1 << COEFF_CONTEXT_BITS;
            else if (qc[0] > 0) cul += 2 << COEFF_CONTEXT_BITS;
        }
        for (int k = 0; k < tx_w; ++k) above_ptr[k] = (uint8_t)cul;
        for (int k = 0; k < tx_h; ++k) left_ptr[k] = (uint8_t)cul;
    }
    printf("\n");
    aom_stop_encode(&w);
    printf("ecblk_bytes %u", w.pos);
    for (uint32_t i = 0; i < w.pos; ++i) printf(" %02x", buf[i]);
    printf("\n");

    // read twin
    uint8_t above_r[16];
    uint8_t left_r[8];
    memset(above_r, (int)INVALID_NEIGHBOR_DATA, sizeof(above_r));
    memset(left_r, (int)INVALID_NEIGHBOR_DATA, sizeof(left_r));
    aom_reader r;
    if (aom_reader_init(&r, buf, w.pos)) return 2;
    r.allow_update_cdf = 1;
    printf("ecblk_rt");
    int rt_bad = 0;
    for (int b = 0; b < 4; ++b) {
        uint8_t srcblk[256];
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 16; ++j) srcblk[i * 16 + j] = src[(px_pos[b][1] + i) * 64 + px_pos[b][0] + j];
        int16_t res[256];
        for (int i = 0; i < 256; ++i) res[i] = (int16_t)srcblk[i];
        int32_t cb[256];
        svtd_fwd2d16x16(res, 16, cb, svt_av1_fdct16_new);
        TranLow qc[256], dq[256];
        uint16_t eob_q = 0;
        svtd_quantize_fp_16x16(cb, &t, scan16, qc, dq, &eob_q);

        const int tx_w = eb_tx_size_wide_unit[TX_16X16];
        const int tx_h = eb_tx_size_high_unit[TX_16X16];
        uint8_t* above_ptr = &above_r[px_pos[b][0] / 4];
        uint8_t* left_ptr = &left_r[px_pos[b][1] / 4];
        int16_t dc_sign = 0;
        if (above_ptr[0] != (uint8_t)INVALID_NEIGHBOR_DATA) {
            for (int k = 0; k < tx_w; ++k) dc_sign += ((above_ptr[k] >> COEFF_CONTEXT_BITS) == 1) ? -1 : ((above_ptr[k] >> COEFF_CONTEXT_BITS) == 2) ? 1 : 0;
        }
        if (left_ptr[0] != (uint8_t)INVALID_NEIGHBOR_DATA) {
            for (int k = 0; k < tx_h; ++k) dc_sign += ((left_ptr[k] >> COEFF_CONTEXT_BITS) == 1) ? -1 : ((left_ptr[k] >> COEFF_CONTEXT_BITS) == 2) ? 1 : 0;
        }
        const int dc_sign_ctx = dc_sign > 0 ? 2 : dc_sign < 0 ? 1 : 0;

        TranLow rc[256];
        memset(rc, 0, sizeof(rc));
        const int reob = svtd_read_coeffs_txb(&r, &fc_r, rc, scan16, TX_16X16, 0, dc_sign_ctx, DC_PRED);
        printf(" %d", reob);
        if (reob != (int)eob_q) rt_bad = 1;
        for (int i = 0; i < 256; ++i) if (rc[i] != qc[i]) rt_bad = 1;

        // NA update (read side)
        int32_t cul = 0;
        for (int q = 0; q < reob; ++q) cul += abs((int)rc[scan16[q]]);
        cul = AOMMIN(cul, COEFF_CONTEXT_MASK);
        if (reob > 0) {
            if (rc[0] < 0) cul |= 1 << COEFF_CONTEXT_BITS;
            else if (rc[0] > 0) cul += 2 << COEFF_CONTEXT_BITS;
        }
        for (int k = 0; k < tx_w; ++k) above_ptr[k] = (uint8_t)cul;
        for (int k = 0; k < tx_h; ++k) left_ptr[k] = (uint8_t)cul;
    }
    printf("\n");
    if (rt_bad) { fprintf(stderr, "TS2 roundtrip FAILED\n"); return 3; }

    int cdf_eq = 1;
    if (memcmp(fc.txb_skip_cdf, fc_r.txb_skip_cdf, sizeof(fc.txb_skip_cdf))) cdf_eq = 0;
    if (memcmp(fc.dc_sign_cdf, fc_r.dc_sign_cdf, sizeof(fc.dc_sign_cdf))) cdf_eq = 0;
    if (memcmp(fc.coeff_base_eob_cdf, fc_r.coeff_base_eob_cdf, sizeof(fc.coeff_base_eob_cdf))) cdf_eq = 0;
    if (memcmp(fc.coeff_base_cdf, fc_r.coeff_base_cdf, sizeof(fc.coeff_base_cdf))) cdf_eq = 0;
    if (memcmp(fc.coeff_br_cdf, fc_r.coeff_br_cdf, sizeof(fc.coeff_br_cdf))) cdf_eq = 0;
    if (memcmp(fc.eob_extra_cdf, fc_r.eob_extra_cdf, sizeof(fc.eob_extra_cdf))) cdf_eq = 0;
    if (memcmp(fc.eob_flag_cdf64, fc_r.eob_flag_cdf64, sizeof(fc.eob_flag_cdf64))) cdf_eq = 0;
    printf("ecblk_cdf_eq %d\n", cdf_eq);
    return 0;
}
