// tools/golden_gen/composition.c
// Composition layer for the golden generator. No arithmetic of its own
// except the two 4x4 2D cores, which are documented specializations of the
// SVT general cores (provenance inline). Everything else forwards to the
// verbatim extracts in svt_gen.c.
#include "svt_gen.c"

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

// TX_64X64 FP quantize entry (L7): quantize_fp_helper_c at n_coeffs=4096,
// log_scale 2 (av1_get_tx_scale_tab[TX_64X64] = 2, full_loop.c:22).
static void svtd_quantize_fp_64x64(const TranLow* coeff, const SvtdQuantTables* t, const int16_t* scan,
                                   TranLow* qcoeff, TranLow* dqcoeff, uint16_t* eob) {
    quantize_fp_helper_c(coeff, 4096, t->zbin, t->round_fp, t->quant_fp, t->quant_shift, qcoeff,
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
static void svtd_bsf3_sequence_header(AomWriteBitBuffer* wb) {
    // max dims 32x32: frame_width_bits = svt_log2f(32) = 5, no bump
    // (:2757-2764); the >= 1 guard (:2766-2771) no-op.
    svt_aom_wb_write_literal(wb, 5 - 1, 4);   // frame_width_bits - 1 (:2775)
    svt_aom_wb_write_literal(wb, 5 - 1, 4);   // frame_height_bits - 1 (:2776)
    svt_aom_wb_write_literal(wb, 32 - 1, 5);  // max_frame_width - 1 (:2777)
    svt_aom_wb_write_literal(wb, 32 - 1, 5);  // max_frame_height - 1 (:2778)
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

// write_sequence_header_obu (:3699-3763), ratified reduced=0 path.
static uint32_t svtd_bsf3_sps_payload(AomWriteBitBuffer* wb) {
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
    svtd_bsf3_sequence_header(wb);
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
    svt_aom_wb_write_bit(wb, 1);  // disable_cdf_update (:3350; ratified)
    svt_aom_wb_write_bit(wb, 0);  // allow_screen_content_tools - bit IS written (force == 2, :3352-3353)
    // force_integer_mv skipped (asc = 0, :3358-3366)
    svt_aom_wb_write_bit(wb, 0);  // frame_size_override_flag (:3386; frame == max dims, :3368-3371)
    // order hint skipped (enable_order_hint = 0, :3389-3391); primary_ref
    // skipped (intra-only, :3393-3395); refresh mask skipped (KF && show, :3399-3402)
    // write_frame_size (:3470 -> :2652-2669): w/h literals skipped (override
    // = 0); superres skipped ENTIRELY (enable_superres = 0 -> no bit,
    // :2636-2639); render size:
    svt_aom_wb_write_bit(wb, 0);  // render_and_frame_size_different (:2616-2624; frame_resize_enabled = 0)
    // allow_intrabc skipped (asc = 0, :3472-3474); refresh_frame_context
    // skipped (might_bwd_adapt = !reduced && !disable_cdf_update = 0, :3548-3554)
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
static uint32_t svtd_bsf3_encode_sps(uint8_t* dst) {
    const uint32_t obu_header_size  = write_obu_header(OBU_SEQUENCE_HEADER, 0, dst);
    AomWriteBitBuffer wb            = {dst + obu_header_size, 0};
    const uint32_t obu_payload_size = svtd_bsf3_sps_payload(&wb);
    const size_t length_field_size  = svt_aom_uleb_size_in_bytes(obu_payload_size);
    size_t coded_size;
    svt_aom_uleb_encode(obu_payload_size, sizeof(obu_payload_size), dst + obu_header_size, &coded_size);
    AomWriteBitBuffer wb2 = {dst + obu_header_size + length_field_size, 0};
    svtd_bsf3_sps_payload(&wb2);  // phase 2 rewrite at the correct offset
    return obu_header_size + (uint32_t)length_field_size + obu_payload_size;
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
