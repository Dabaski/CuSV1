// tools/golden_gen/composition.c
// Composition layer for the golden generator. No arithmetic of its own
// except the two 4x4 2D cores, which are documented specializations of the
// SVT general cores (provenance inline). Everything else forwards to the
// verbatim extracts in svt_gen.c.
#include "svt_gen.c"

// ---- dispatch adapters -----------------------------------------------------
// SVT builds svt_aom_eb_pred / svt_aom_dc_pred with the intra_pred_sized
// macro (intra_prediction.c:1402) over every TxSize; the generator
// instantiates the TX_4X4 column. These adapters forward with bw=bh=4 ???
// pure plumbing, no arithmetic.
SvtdPredFn svtd_eb_pred[13][2];
SvtdPredFn svtd_dc_pred[2][2][2];
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



