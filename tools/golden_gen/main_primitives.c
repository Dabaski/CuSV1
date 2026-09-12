// tools/golden_gen/main_primitives.c
// Dumps every per-primitive golden. Run and diff against
// tools/golden_gen/expected_primitives.txt (validation gate).
#include "composition.c"

// ---- entropy coder ground floor (EC0) ----
// Drivers mirror aom_start_encode (bitstream_unit.h:230-236): point
// ec.buf at a fixed buffer, then svt_od_ec_enc_reset.
static uint8_t ec_buf[1024];
static OdEcEnc ec_enc;
static od_ec_dec ec_dec;
static void ec_reset(void) {
    ec_enc.buf = ec_buf;
    svt_od_ec_enc_reset(&ec_enc);
}
static void ec_print_bytes(const char* name, uint32_t n) {
    printf("%s %u", name, n);
    for (uint32_t i = 0; i < n; ++i) printf(" %02x", ec_buf[i]);
    printf("\n");
}
// Fixed fixture iCDF, 13 symbols (EC0 fixture data, not a golden):
// monotonically non-increasing, icdf[12] = 0 (svt_od_ec_encode_cdf_q15
// precondition, bitstream_unit.c:282). Skewed toward low indices.
static const uint16_t ec_icdf13[13] = {26700, 22000, 18000, 14000, 10500, 8000, 6000, 4500, 3200, 2200, 1400, 700, 0};

int main(void) {
    svtd_populate_dispatch();
    fprintf(stderr, "CK: dispatch\n"); fflush(stderr);

    // ---- table spot values (test_transform.cpp) ----
    printf("cospi13_16 %d\n", cospi_arr(13)[16]);
    printf("cospi13_32 %d\n", cospi_arr(13)[32]);
    printf("cospi13_48 %d\n", cospi_arr(13)[48]);
    printf("sinpi13_4 %d\n", sinpi_arr(13)[4]);
    printf("halfbtf_5793_8 %d\n", half_btf(5793, 8, 5793, 8, 13));
    fprintf(stderr, "CK: tables\n"); fflush(stderr);
    printf("roundshift_96784_13 %d\n", round_shift(96784, 13));

    // ---- forward 1D (fdct4/fadst4 {5,3,7,1} @13) ----
    {
        const int32_t in[4] = {5, 3, 7, 1};
        int32_t o[4];
        svt_av1_fdct4_new(in, o, 13, NULL);
        printf("fdct4 %d %d %d %d\n", o[0], o[1], o[2], o[3]);
        svt_av1_fadst4_new(in, o, 13, NULL);
        printf("fadst4 %d %d %d %d\n", o[0], o[1], o[2], o[3]);
    }

    // ---- forward 2D cores (mirrors av1_tranform_two_d_core_c TX_4X4) ----
    {
        const int16_t in[16] = {9, 2, 3, 1, 5, 6, 7, 8, 8, 7, 6, 5, 4, 3, 9, 1};
        int32_t out[16];
        svtd_fwd2d4x4(in, 4, out, svt_av1_fdct4_new);
        printf("fwd2d_dct:"); for (int i = 0; i < 16; ++i) printf(" %d", out[i]); printf("\n");
        svtd_fwd2d4x4(in, 4, out, svt_av1_fadst4_new);
        printf("fwd2d_adst:"); for (int i = 0; i < 16; ++i) printf(" %d", out[i]); printf("\n");
        int16_t ones[16]; for (int i = 0; i < 16; ++i) ones[i] = 1;
        svtd_fwd2d4x4(ones, 4, out, svt_av1_fdct4_new);
        printf("fwd2d_ones:"); for (int i = 0; i < 16; ++i) printf(" %d", out[i]); printf("\n");
    }

    // ---- inverse 1D ({100,50,-20,8} @12) ----
    {
        const int32_t in[4] = {100, 50, -20, 8};
        int32_t o[4];
        const int8_t sr[8] = {16, 16, 16, 16, 16, 16, 16, 16};
        svt_av1_idct4_new(in, o, 12, sr);
        printf("idct4 %d %d %d %d\n", o[0], o[1], o[2], o[3]);
        svt_av1_iadst4_new(in, o, 12, sr);
        printf("iadst4 %d %d %d %d\n", o[0], o[1], o[2], o[3]);
    }

    fprintf(stderr, "CK: 4x4 done\n"); fflush(stderr);

    // ---- 8x8 forward 1D @ cos_bit 13 ----
    {
        const int32_t in8[8] = {200, 80, -50, 30, 100, -20, 60, 10};
        int32_t o8[8];
        svt_av1_fdct8_new(in8, o8, 13, NULL);
        printf("fdct8:"); for (int i = 0; i < 8; ++i) printf(" %d", o8[i]); printf("\n");
        svt_av1_fadst8_new(in8, o8, 13, NULL);
        printf("fadst8:"); for (int i = 0; i < 8; ++i) printf(" %d", o8[i]); printf("\n");
    }

    fprintf(stderr, "CK: fwd8 done\n"); fflush(stderr);

    // ---- 8x8 inverse 1D @ cos_bit 12 ----
    {
        const int32_t in8[8] = {300, -120, 75, 200, -60, 40, 90, -15};
        int32_t o8[8];
        const int8_t sr8[8] = {16, 16, 16, 16, 16, 16, 16, 16};
        svt_av1_idct8_new(in8, o8, 12, sr8);
        printf("idct8:"); for (int i = 0; i < 8; ++i) printf(" %d", o8[i]); printf("\n");
        svt_av1_iadst8_new(in8, o8, 12, sr8);
        printf("iadst8:"); for (int i = 0; i < 8; ++i) printf(" %d", o8[i]); printf("\n");
    }

    fprintf(stderr, "CK: inv8 done\n"); fflush(stderr);

    // ---- 8x8 forward 2D (DCT + ADST) ----
    {
        const int16_t in[64] = {12, 45, 3, 78, 22, 91, 6, 30,
                                67, 8, 54, 11, 39, 71, 17, 48,
                                2, 90, 25, 63, 7, 44, 85, 19,
                                51, 36, 9, 77, 28, 5, 60, 83,
                                15, 72, 41, 4, 88, 33, 26, 58,
                                80, 13, 66, 47, 1, 95, 38, 70,
                                24, 56, 10, 82, 31, 68, 14, 42,
                                75, 29, 87, 20, 53, 16, 79, 34};
        int32_t out[64];
        svtd_fwd2d8x8(in, 8, out, svt_av1_fdct8_new);
        printf("fwd2d8_dct:"); for (int i = 0; i < 64; ++i) printf(" %d", out[i]); printf("\n");
        svtd_fwd2d8x8(in, 8, out, svt_av1_fadst8_new);
        printf("fwd2d8_adst:"); for (int i = 0; i < 64; ++i) printf(" %d", out[i]); printf("\n");
    }

    fprintf(stderr, "CK: fwd2d8 done\n"); fflush(stderr);

    // ---- 8x8 inverse 2D add (DCT + ADST) onto a V-pred 8x8 ----
    {
        const int32_t cdct8[64] = {520, -34, 78, -11, 92, 5, -63, 28,
                                   -17, 45, -8, 60, -29, 71, 14, -52,
                                   33, -76, 19, 41, -55, 23, 87, -9,
                                   62, 12, -48, 70, -16, 38, -83, 25,
                                   -44, 58, 8, -92, 31, 67, -21, 49,
                                   15, -39, 74, -6, 84, -27, 51, -13,
                                   66, 22, -57, 35, -78, 10, 43, -31,
                                   -25, 80, -18, 56, 7, -61, 29, -71};
        const int32_t cadst8[64] = {-45, 67, -12, 89, 23, -58, 41, -30,
                                    71, -24, 56, -83, 15, 49, -37, 62,
                                    -9, 38, -71, 27, 64, -45, 18, -77,
                                    55, -61, 30, -14, 76, -22, 47, -88,
                                    20, 41, -66, 12, -53, 78, -35, 59,
                                    -72, 16, 44, -27, 61, -9, 33, -50,
                                    37, -55, 69, -18, 42, -64, 25, -46,
                                    -14, 58, -32, 74, -20, 51, -79, 11};
        uint8_t pred[64];
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) pred[r*8+c] = (uint8_t)(10 + 5*c);
        svtd_inv2dadd8x8(cdct8, pred, 8, svt_av1_idct8_new);
        printf("inv2d8_dct_onto_vpred:"); for (int i = 0; i < 64; ++i) printf(" %d", pred[i]); printf("\n");
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c) pred[r*8+c] = (uint8_t)(10 + 5*c);
        svtd_inv2dadd8x8(cadst8, pred, 8, svt_av1_iadst8_new);
        printf("inv2d8_adst_onto_vpred:"); for (int i = 0; i < 64; ++i) printf(" %d", pred[i]); printf("\n");
    }

    fprintf(stderr, "CK: inv2d8 done\n"); fflush(stderr);

    // ---- 16x16 forward 1D @ cos_bit 13 (C0) ----
    {
        const int32_t in16[16] = {200, 80, -50, 30, 100, -20, 60, 10,
                                  -35, 95, 5, -70, 45, 25, -15, 55};
        int32_t o16[16];
        svt_av1_fdct16_new(in16, o16, 13, NULL);
        printf("fdct16:"); for (int i = 0; i < 16; ++i) printf(" %d", o16[i]); printf("\n");
        svt_av1_fadst16_new(in16, o16, 13, NULL);
        printf("fadst16:"); for (int i = 0; i < 16; ++i) printf(" %d", o16[i]); printf("\n");
    }

    fprintf(stderr, "CK: fwd16 done\n"); fflush(stderr);

    // ---- 16x16 inverse 1D @ cos_bit 12 (C0; stage_range = opt_range 16 per
    // gen_inv_range_16x16 gate lines below; idct16 consumes indices 3-7,
    // iadst16 consumes 3/5/7) ----
    {
        const int32_t in16[16] = {300, -120, 75, 200, -60, 40, 90, -15,
                                  55, -95, 20, 65, -40, 85, -25, 10};
        int32_t o16[16];
        const int8_t sr16[MAX_TXFM_STAGE_NUM] = {16, 16, 16, 16, 16, 16, 16, 16,
                                                 16, 16, 16, 16, 16, 16, 16, 16};
        svt_av1_idct16_new(in16, o16, 12, sr16);
        printf("idct16:"); for (int i = 0; i < 16; ++i) printf(" %d", o16[i]); printf("\n");
        svt_av1_iadst16_new(in16, o16, 12, sr16);
        printf("iadst16:"); for (int i = 0; i < 16; ++i) printf(" %d", o16[i]); printf("\n");
    }

    fprintf(stderr, "CK: inv16 done\n"); fflush(stderr);

    // ---- 16x16 forward 2D (DCT + ADST), C0 ----
    // fixture: deterministic 16x16, values in [-105, 105] (int16-safe)
    {
        int16_t in[256];
        for (int r = 0; r < 16; ++r)
            for (int c = 0; c < 16; ++c)
                in[r * 16 + c] = (int16_t)((c * 13 + r * 7 + ((c * r) & 31)) % 211) - 105;
        int32_t out[256];
        svtd_fwd2d16x16(in, 16, out, svt_av1_fdct16_new);
        printf("fwd2d16_dct:"); for (int i = 0; i < 256; ++i) printf(" %d", out[i]); printf("\n");
        svtd_fwd2d16x16(in, 16, out, svt_av1_fadst16_new);
        printf("fwd2d16_adst:"); for (int i = 0; i < 256; ++i) printf(" %d", out[i]); printf("\n");
    }

    fprintf(stderr, "CK: fwd2d16 done\n"); fflush(stderr);

    // ---- 16x16 inverse 2D add (DCT + ADST) onto a V-pred 16x16, C0 ----
    {
        int32_t cdct16[256];
        int32_t cadst16[256];
        for (int i = 0; i < 256; ++i) {
            const int r = i / 16, c = i % 16;
            cdct16[i] = ((r * 31 + c * 17 + 45) % 97) - 48;
            cadst16[i] = ((r * 23 + c * 41 + 13) % 89) - 44;
        }
        uint8_t pred[256];
        for (int r = 0; r < 16; ++r)
            for (int c = 0; c < 16; ++c) pred[r*16+c] = (uint8_t)(10 + 5*c);
        svtd_inv2dadd16x16(cdct16, pred, 16, svt_av1_idct16_new);
        printf("inv2d16_dct_onto_vpred:"); for (int i = 0; i < 256; ++i) printf(" %d", pred[i]); printf("\n");
        for (int r = 0; r < 16; ++r)
            for (int c = 0; c < 16; ++c) pred[r*16+c] = (uint8_t)(10 + 5*c);
        svtd_inv2dadd16x16(cadst16, pred, 16, svt_av1_iadst16_new);
        printf("inv2d16_adst_onto_vpred:"); for (int i = 0; i < 256; ++i) printf(" %d", pred[i]); printf("\n");
    }

    fprintf(stderr, "CK: inv2d16 done\n"); fflush(stderr);

    // default scan 16x16 (svt_aom_init_iscan formula at W=H=16)
    {
        int16_t scan16[256];
        svtd_default_scan_16x16(scan16);
        printf("qscan16:"); for (int i = 0; i < 256; ++i) printf(" %d", scan16[i]); printf("\n");
    }

    svtd_gen_inv_range_16x16();

    svtd_gen_inv_range_32x32();
    svtd_gen_inv_range_64x64();

    // ---- L0: 32x32 transforms gate lines ----
    {
        // 1D fwd @ cos_bit 12 (the fwd_cos_bit_col/row[3][3] value the 2D uses)
        const int32_t in32[32] = {200, 80, -50, 30, 100, -20, 60, 10, -35, 95, 5, -70, 45, 25, -15, 55,
                                  65, -85, 15, -5, 75, -60, 40, 90, -25, 35, 50, -45, 20, -10, 70, -30};
        int32_t o32[32];
        svt_av1_fdct32_new(in32, o32, 12, NULL);
        printf("fdct32:"); for (int i = 0; i < 32; ++i) printf(" %d", o32[i]); printf("\n");
        av1_fadst32_new(in32, o32, 12, NULL);
        printf("fadst32:"); for (int i = 0; i < 32; ++i) printf(" %d", o32[i]); printf("\n");
        // 1D inv @ 12, stage_range 16 (gen_inv_range_32x32 lines)
        const int32_t i32[32] = {300, -120, 75, 200, -60, 40, 90, -15, 55, -95, 20, 65, -40, 85, -25, 10,
                                 30, -70, 95, -35, 60, -15, 80, 25, -50, 45, -20, 70, -90, 15, 50, -55};
        const int8_t sr32[MAX_TXFM_STAGE_NUM] = {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                                                 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                                                 16};
        svt_av1_idct32_new(i32, o32, 12, sr32);
        printf("idct32:"); for (int i = 0; i < 32; ++i) printf(" %d", o32[i]); printf("\n");
        av1_iadst32_new(i32, o32, 12, sr32);
        printf("iadst32:"); for (int i = 0; i < 32; ++i) printf(" %d", o32[i]); printf("\n");

        // 2D fwd (DCT + ADST); fixture (c*7+r*5+((c*r)&15))%173-86
        int16_t in[1024];
        for (int r = 0; r < 32; ++r)
            for (int c = 0; c < 32; ++c)
                in[r * 32 + c] = (int16_t)((c * 7 + r * 5 + ((c * r) & 15)) % 173) - 86;
        int32_t out[1024];
        svtd_fwd2d32x32(in, 32, out, svt_av1_fdct32_new);
        printf("fwd2d32_dct:"); for (int i = 0; i < 1024; ++i) printf(" %d", out[i]); printf("\n");
        svtd_fwd2d32x32(in, 32, out, av1_fadst32_new);
        printf("fwd2d32_adst:"); for (int i = 0; i < 1024; ++i) printf(" %d", out[i]); printf("\n");

        // 2D inverse add onto a V-pred 32x32
        int32_t cdct[1024];
        int32_t cadst[1024];
        for (int i = 0; i < 1024; ++i) {
            const int r = i / 32, c = i % 32;
            cdct[i] = ((r * 13 + c * 29 + 31) % 89) - 44;
            cadst[i] = ((r * 17 + c * 11 + 7) % 79) - 39;
        }
        uint8_t pred[1024];
        for (int r = 0; r < 32; ++r)
            for (int c = 0; c < 32; ++c) pred[r * 32 + c] = (uint8_t)(10 + 3 * c);
        svtd_inv2dadd32x32(cdct, pred, 32, svt_av1_idct32_new);
        printf("inv2d32_dct_onto_vpred:"); for (int i = 0; i < 1024; ++i) printf(" %d", pred[i]); printf("\n");
        for (int r = 0; r < 32; ++r)
            for (int c = 0; c < 32; ++c) pred[r * 32 + c] = (uint8_t)(10 + 3 * c);
        svtd_inv2dadd32x32(cadst, pred, 32, av1_iadst32_new);
        printf("inv2d32_adst_onto_vpred:"); for (int i = 0; i < 1024; ++i) printf(" %d", pred[i]); printf("\n");

        // qscan32
        int16_t scan32[1024];
        svtd_default_scan_32x32(scan32);
        printf("qscan32:"); for (int i = 0; i < 1024; ++i) printf(" %d", scan32[i]); printf("\n");

        // log_scale-1 quant gate lines; fixture = fwd2d32_dct output (recomputed
        // into a dedicated buffer - the shared `out` now holds the ADST pass)
        SvtdQuantTables t100, t0;
        svtd_build_quantizer_luma(100, &t100);
        svtd_build_quantizer_luma(0, &t0);
        {
            int32_t outDct[1024];
            svtd_fwd2d32x32(in, 32, outDct, svt_av1_fdct32_new);
            TranLow qc[1024], dq[1024];
            uint16_t eob = 0;
            svtd_quantize_fp_32x32(outDct, &t100, scan32, qc, dq, &eob);
        printf("q32fp_q100:");
        for (int i = 0; i < 1024; ++i) printf(" %d", qc[i]);
        printf(" |");
        for (int i = 0; i < 1024; ++i) printf(" %d", dq[i]);
        printf(" | %u\n", eob);
        svtd_quantize_b_32x32(outDct, &t100, scan32, qc, dq, &eob);
        printf("q32b_q100:");
        for (int i = 0; i < 1024; ++i) printf(" %d", qc[i]);
        printf(" |");
        for (int i = 0; i < 1024; ++i) printf(" %d", dq[i]);
        printf(" | %u\n", eob);
        svtd_quantize_fp_32x32(outDct, &t0, scan32, qc, dq, &eob);
        printf("q32fp_q0:");
        for (int i = 0; i < 1024; ++i) printf(" %d", qc[i]);
        printf(" |");
        for (int i = 0; i < 1024; ++i) printf(" %d", dq[i]);
        printf(" | %u\n", eob);
        }
    }

    fprintf(stderr, "CK: L0 done\n"); fflush(stderr);

    svtd_gen_inv_range_8x8();

    // ---- B16: 16x16 builder goldens (TX_16X16 column) ----
    // upsample never fires at 16x16 (blk_wh = 32 > 16, svt_aom_use_intra_edge_upsample)
    {
        const uint8_t above16[32] = {11, 22, 33, 44, 55, 66, 77, 88,
                                     99, 110, 120, 130, 140, 150, 160, 170,
                                     180, 190, 200, 210, 220, 230, 240, 250,
                                     245, 235, 225, 215, 205, 195, 185, 175};
        const uint8_t left16[32] = {5, 15, 25, 35, 45, 55, 65, 75,
                                    85, 95, 105, 115, 125, 135, 145, 155,
                                    165, 175, 185, 195, 205, 215, 225, 235,
                                    245, 250, 240, 230, 220, 210, 200, 190};
        uint8_t dst[256];
        // (a) V_PRED full above at TX_16X16
        svtd_call_builder_tx(dst, V_PRED, 0, FILTER_INTRA_MODES, 0, above16, 16, 0, above16, 0, 0, 0, TX_16X16);
        printf("b16_v:");
        for (int i = 0; i < 256; ++i) printf(" %d", dst[i]);
        printf("\n");
        // (b) DC both edges
        svtd_call_builder_tx(dst, DC_PRED, 0, FILTER_INTRA_MODES, 0, above16, 16, 0, left16, 16, 0, 7, TX_16X16);
        printf("b16_dc:");
        for (int i = 0; i < 256; ++i) printf(" %d", dst[i]);
        printf("\n");
        // (c) DC-128 no edges
        svtd_call_builder_tx(dst, DC_PRED, 0, FILTER_INTRA_MODES, 0, above16, 0, 0, left16, 0, 0, 0, TX_16X16);
        printf("b16_dc128:");
        for (int i = 0; i < 256; ++i) printf(" %d", dst[i]);
        printf("\n");
        // (d) D45 zone 1 with REAL top-right 16 (need_right -> numTop 32;
        // upsample off; strength = filt_str(16,16,-23,0): d 23 -> 2)
        svtd_call_builder_tx(dst, D45_PRED, 0, FILTER_INTRA_MODES, 0, above16, 16, 16, left16, 16, 0, 7, TX_16X16);
        printf("b16_d45:");
        for (int i = 0; i < 256; ++i) printf(" %d", dst[i]);
        printf("\n");
        // (e) D135 zone 2 (above+left, corner 7)
        svtd_call_builder_tx(dst, D135_PRED, 0, FILTER_INTRA_MODES, 0, above16, 16, 0, left16, 16, 0, 7, TX_16X16);
        printf("b16_d135:");
        for (int i = 0; i < 256; ++i) printf(" %d", dst[i]);
        printf("\n");
        // (f) D203 zone 3 (need_bottom -> numLeft 32 extension)
        svtd_call_builder_tx(dst, D203_PRED, 0, FILTER_INTRA_MODES, 0, above16, 16, 0, left16, 16, 0, 7, TX_16X16);
        printf("b16_d203:");
        for (int i = 0; i < 256; ++i) printf(" %d", dst[i]);
        printf("\n");
        // (g) SMOOTH (sm_weight_arrays bs=16 row)
        svtd_call_builder_tx(dst, SMOOTH_PRED, 0, FILTER_INTRA_MODES, 0, above16, 16, 0, left16, 16, 0, 7, TX_16X16);
        printf("b16_sm:");
        for (int i = 0; i < 256; ++i) printf(" %d", dst[i]);
        printf("\n");
        // (h) PAETH
        svtd_call_builder_tx(dst, PAETH_PRED, 0, FILTER_INTRA_MODES, 0, above16, 16, 0, left16, 16, 0, 7, TX_16X16);
        printf("b16_paeth:");
        for (int i = 0; i < 256; ++i) printf(" %d", dst[i]);
        printf("\n");
        // (i) FILTER_V_PRED at TX_16X16 (strips c=1,5,9,13)
        svtd_call_builder_tx(dst, V_PRED, 0, FILTER_V_PRED /* 1 */, 0, above16 + 1, 16, 0, left16, 16, 0, 10, TX_16X16);
        printf("b16_fiv:");
        for (int i = 0; i < 256; ++i) printf(" %d", dst[i]);
        printf("\n");
    }

    fprintf(stderr, "CK: b16 done\n"); fflush(stderr);

    // ---- B32: 32x32 builder goldens (TX_32X32 column, L6) ----
    // upsample dead at 32x32 (blk_wh = 64 > 16); corner blend LIVE (64 >= 24);
    // zone-1 filter nPx = 65; sm_w32 row
    {
        uint8_t above64[64];
        uint8_t left64[64];
        for (int i = 0; i < 64; ++i) {
            above64[i] = (uint8_t)((11 + 7 * i) % 251);
            left64[i] = (uint8_t)((5 + 11 * i) % 251);
        }
        uint8_t dst[1024];
        svtd_call_builder_tx(dst, V_PRED, 0, FILTER_INTRA_MODES, 0, above64, 32, 0, above64, 0, 0, 0, TX_32X32);
        printf("b32_v:");
        for (int i = 0; i < 1024; ++i) printf(" %d", dst[i]);
        printf("\n");
        svtd_call_builder_tx(dst, DC_PRED, 0, FILTER_INTRA_MODES, 0, above64, 32, 0, left64, 32, 0, 7, TX_32X32);
        printf("b32_dc:");
        for (int i = 0; i < 1024; ++i) printf(" %d", dst[i]);
        printf("\n");
        svtd_call_builder_tx(dst, DC_PRED, 0, FILTER_INTRA_MODES, 0, above64, 0, 0, left64, 0, 0, 0, TX_32X32);
        printf("b32_dc128:");
        for (int i = 0; i < 1024; ++i) printf(" %d", dst[i]);
        printf("\n");
        svtd_call_builder_tx(dst, D45_PRED, 0, FILTER_INTRA_MODES, 0, above64, 32, 32, left64, 32, 0, 7, TX_32X32);
        printf("b32_d45:");
        for (int i = 0; i < 1024; ++i) printf(" %d", dst[i]);
        printf("\n");
        svtd_call_builder_tx(dst, D135_PRED, 0, FILTER_INTRA_MODES, 0, above64, 32, 0, left64, 32, 0, 7, TX_32X32);
        printf("b32_d135:");
        for (int i = 0; i < 1024; ++i) printf(" %d", dst[i]);
        printf("\n");
        svtd_call_builder_tx(dst, D203_PRED, 0, FILTER_INTRA_MODES, 0, above64, 32, 0, left64, 32, 0, 7, TX_32X32);
        printf("b32_d203:");
        for (int i = 0; i < 1024; ++i) printf(" %d", dst[i]);
        printf("\n");
        svtd_call_builder_tx(dst, SMOOTH_PRED, 0, FILTER_INTRA_MODES, 0, above64, 32, 0, left64, 32, 0, 7, TX_32X32);
        printf("b32_sm:");
        for (int i = 0; i < 1024; ++i) printf(" %d", dst[i]);
        printf("\n");
        svtd_call_builder_tx(dst, PAETH_PRED, 0, FILTER_INTRA_MODES, 0, above64, 32, 0, left64, 32, 0, 7, TX_32X32);
        printf("b32_paeth:");
        for (int i = 0; i < 1024; ++i) printf(" %d", dst[i]);
        printf("\n");
        svtd_call_builder_tx(dst, V_PRED, 0, FILTER_V_PRED /* 1 */, 0, above64 + 1, 32, 0, left64, 32, 0, 10, TX_32X32);
        printf("b32_fiv:");
        for (int i = 0; i < 1024; ++i) printf(" %d", dst[i]);
        printf("\n");
    }

    fprintf(stderr, "CK: b32 done\n"); fflush(stderr);

    // ---- L6: 32x32 frame-policy gate lines (64x64, 2x2 of 32x32) ----
    // rows 0-31 = ramp 2*(x+y+1) (max 2*63 = 126 < 255); rows 32-63 zero.
    // block (bx=0, by=1) is the only nTr>0 block: above[j] = 2j+64 and D45
    // zone-1 pred above[1+r+c] = 2*(r+c+33) = src[32+r][c] -> SAD 0 only with
    // REAL TR.
    {
        uint8_t src[4096];
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                src[y * 64 + x] = (y < 32) ? (uint8_t)(2 * (x + y + 1)) : 0;
            }
        }
        uint8_t recon[4096];
        int32_t coeffs[4096];
        int modes[4] = {0};
        svtd_frame_auto_32x32_blocks(src, recon, coeffs, modes);
        printf("f32_modes:");
        for (int i = 0; i < 4; ++i) printf(" %d", modes[i]);
        printf("\n");
        printf("f32_recon:");
        for (int i = 0; i < 4096; ++i) printf(" %d", recon[i]);
        printf("\n");
        printf("f32_coeffs:");
        for (int i = 0; i < 4096; ++i) printf(" %d", coeffs[i]);
        printf("\n");

        uint8_t reconQ[4096];
        int32_t coeffsQ[4096];
        int modesQ[4] = {0};
        svtd_frame_auto_32x32_q(src, reconQ, coeffsQ, modesQ, 100);
        printf("f32q_modes:");
        for (int i = 0; i < 4; ++i) printf(" %d", modesQ[i]);
        printf("\n");
        printf("f32q_coeffs:");
        for (int i = 0; i < 4096; ++i) printf(" %d", coeffsQ[i]);
        printf("\n");

        uint8_t reconV[4096];
        int32_t coeffsV[4096];
        svtd_frame_v_dct_32x32(src, reconV, coeffsV);
        printf("f32v_recon:");
        for (int i = 0; i < 4096; ++i) printf(" %d", reconV[i]);
        printf("\n");
        printf("f32v_coeffs:");
        for (int i = 0; i < 4096; ++i) printf(" %d", coeffsV[i]);
        printf("\n");

        uint8_t reconVQ[4096];
        int32_t coeffsVQ[4096];
        svtd_frame_v_dct_32x32_q(src, reconVQ, coeffsVQ, 100);
        printf("f32vq_recon:");
        for (int i = 0; i < 4096; ++i) printf(" %d", reconVQ[i]);
        printf("\n");
        printf("f32vq_coeffs:");
        for (int i = 0; i < 4096; ++i) printf(" %d", coeffsVQ[i]);
        printf("\n");
    }

    fprintf(stderr, "CK: f32 done\n"); fflush(stderr);

    // ---- L7: 64x64 transforms gate lines (DCT-only) ----
    {
        // 1D fwd @ cos_bit 13 (fwd_cos_bit_col[4][4])
        const int32_t in64[64] = {
            200, 80, -50, 30, 100, -20, 60, 10, -35, 95, 5, -70, 45, 25, -15, 55,
            65, -85, 15, -5, 75, -60, 40, 90, -25, 35, 50, -45, 20, -10, 70, -30,
            -40, 55, 10, -60, 85, -20, 35, -5, 45, -75, 25, 65, -35, 15, -25, 80,
            5, -15, 40, -55, 25, -65, 15, 35, -45, 20, -10, 60, -85, 30, 50, -20};
        int32_t o64[64];
        svt_av1_fdct64_new(in64, o64, 13, NULL);
        printf("fdct64:");
        for (int i = 0; i < 64; ++i) printf(" %d", o64[i]);
        printf("\n");
        // 1D inv @ 12, stage_range 16 (gen_inv_range_64x64_dct line, 12 stages)
        const int32_t i64[64] = {
            300, -120, 75, 200, -60, 40, 90, -15, 55, -95, 20, 65, -40, 85, -25, 10,
            30, -70, 95, -35, 60, -15, 80, 25, -50, 45, -20, 70, -90, 15, 50, -55,
            35, -65, 90, -30, 55, -10, 75, 20, -45, 40, -15, 65, -85, 10, 45, -50,
            25, -35, 35, -40, 50, -20, 70, 15, -55, 30, -25, 60, -95, 5, 40, -45};
        const int8_t sr64[MAX_TXFM_STAGE_NUM] = {16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                                                 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
                                                 16};
        svt_av1_idct64_new(i64, o64, 12, sr64);
        printf("idct64:");
        for (int i = 0; i < 64; ++i) printf(" %d", o64[i]);
        printf("\n");

        // 2D fwd DCT; fixture (c*3+r*2+((c*r)&7))%149-74
        int16_t in[4096];
        for (int r = 0; r < 64; ++r)
            for (int c = 0; c < 64; ++c)
                in[r * 64 + c] = (int16_t)((c * 3 + r * 2 + ((c * r) & 7)) % 149) - 74;
        int32_t out[4096];
        svtd_fwd2d64x64(in, 64, out, svt_av1_fdct64_new);
        printf("fwd2d64_dct:");
        for (int i = 0; i < 4096; ++i) printf(" %d", out[i]);
        printf("\n");

        // 2D inverse add DCT onto a V-pred 64x64
        int32_t cdct[4096];
        for (int i = 0; i < 4096; ++i) {
            const int r = i / 64, c = i % 64;
            cdct[i] = ((r * 7 + c * 19 + 23) % 83) - 41;
        }
        uint8_t pred[4096];
        for (int r = 0; r < 64; ++r)
            for (int c = 0; c < 64; ++c) pred[r * 64 + c] = (uint8_t)(9 + 2 * c);
        svtd_inv2dadd64x64(cdct, pred, 64, svt_av1_idct64_new);
        printf("inv2d64_dct_onto_vpred:");
        for (int i = 0; i < 4096; ++i) printf(" %d", pred[i]);
        printf("\n");

        // qscan64
        int16_t scan64[4096];
        svtd_default_scan_64x64(scan64);
        printf("qscan64:");
        for (int i = 0; i < 4096; ++i) printf(" %d", scan64[i]);
        printf("\n");

        // log_scale-2 quant gate lines; fixture = fwd2d64_dct output
        SvtdQuantTables t100, t0;
        svtd_build_quantizer_luma(100, &t100);
        svtd_build_quantizer_luma(0, &t0);
        TranLow qc[4096], dq[4096];
        uint16_t eob = 0;
        svtd_quantize_fp_64x64(out, &t100, scan64, qc, dq, &eob);
        printf("q64fp_q100:");
        for (int i = 0; i < 4096; ++i) printf(" %d", qc[i]);
        printf(" |");
        for (int i = 0; i < 4096; ++i) printf(" %d", dq[i]);
        printf(" | %u\n", eob);
        svtd_quantize_b_64x64(out, &t100, scan64, qc, dq, &eob);
        printf("q64b_q100:");
        for (int i = 0; i < 4096; ++i) printf(" %d", qc[i]);
        printf(" |");
        for (int i = 0; i < 4096; ++i) printf(" %d", dq[i]);
        printf(" | %u\n", eob);
        svtd_quantize_fp_64x64(out, &t0, scan64, qc, dq, &eob);
        printf("q64fp_q0:");
        for (int i = 0; i < 4096; ++i) printf(" %d", qc[i]);
        printf(" |");
        for (int i = 0; i < 4096; ++i) printf(" %d", dq[i]);
        printf(" | %u\n", eob);
    }

    fprintf(stderr, "CK: L7 done\n"); fflush(stderr);

    // ---- L9: 64x64 builder + frame gate lines ----
    // upsample dead at 64x64 (blk_wh = 128 > 16); corner blend LIVE (128>=24)
    {
        uint8_t above128[128];
        uint8_t left128[128];
        for (int i = 0; i < 128; ++i) {
            above128[i] = (uint8_t)((13 + 5 * i) % 251);
            left128[i] = (uint8_t)((7 + 9 * i) % 251);
        }
        uint8_t dst[4096];
        svtd_call_builder_tx(dst, V_PRED, 0, FILTER_INTRA_MODES, 0, above128, 64, 0, above128, 0, 0, 0, TX_64X64);
        printf("b64_v:");
        for (int i = 0; i < 4096; ++i) printf(" %d", dst[i]);
        printf("\n");
        svtd_call_builder_tx(dst, DC_PRED, 0, FILTER_INTRA_MODES, 0, above128, 64, 0, left128, 64, 0, 7, TX_64X64);
        printf("b64_dc:");
        for (int i = 0; i < 4096; ++i) printf(" %d", dst[i]);
        printf("\n");
        svtd_call_builder_tx(dst, D45_PRED, 0, FILTER_INTRA_MODES, 0, above128, 64, 64, left128, 64, 0, 7, TX_64X64);
        printf("b64_d45:");
        for (int i = 0; i < 4096; ++i) printf(" %d", dst[i]);
        printf("\n");
        svtd_call_builder_tx(dst, SMOOTH_PRED, 0, FILTER_INTRA_MODES, 0, above128, 64, 0, left128, 64, 0, 7, TX_64X64);
        printf("b64_sm:");
        for (int i = 0; i < 4096; ++i) printf(" %d", dst[i]);
        printf("\n");
    }

    fprintf(stderr, "CK: b64 done\n"); fflush(stderr);

    // ---- L9: 64x64 frame-policy gate lines (128x128, 2x2 of 64x64) ----
    // rows 0-63 = ramp (x+y+1) (max 127 < 255); rows 64-127 zero. block
    // (bx=0, by=1) is the only nTr>0 block: above[j] = j+64 and D45 zone-1
    // pred above[1+r+c] = r+c+65 = src[64+r][c] -> SAD 0 only with REAL TR.
    {
        uint8_t src[16384];
        for (int y = 0; y < 128; ++y) {
            for (int x = 0; x < 128; ++x) {
                src[y * 128 + x] = (y < 64) ? (uint8_t)(x + y + 1) : 0;
            }
        }
        uint8_t recon[16384];
        int32_t coeffs[16384];
        int modes[4] = {0};
        svtd_frame_auto_64x64_blocks(src, recon, coeffs, modes);
        printf("f64_modes:");
        for (int i = 0; i < 4; ++i) printf(" %d", modes[i]);
        printf("\n");
        printf("f64_recon:");
        for (int i = 0; i < 16384; ++i) printf(" %d", recon[i]);
        printf("\n");
        printf("f64_coeffs:");
        for (int i = 0; i < 16384; ++i) printf(" %d", coeffs[i]);
        printf("\n");

        uint8_t reconQ[16384];
        int32_t coeffsQ[16384];
        int modesQ[4] = {0};
        svtd_frame_auto_64x64_q(src, reconQ, coeffsQ, modesQ, 100);
        printf("f64q_modes:");
        for (int i = 0; i < 4; ++i) printf(" %d", modesQ[i]);
        printf("\n");
        printf("f64q_coeffs:");
        for (int i = 0; i < 16384; ++i) printf(" %d", coeffsQ[i]);
        printf("\n");

        uint8_t reconV[16384];
        int32_t coeffsV[16384];
        svtd_frame_v_dct_64x64(src, reconV, coeffsV);
        printf("f64v_recon:");
        for (int i = 0; i < 16384; ++i) printf(" %d", reconV[i]);
        printf("\n");
        printf("f64v_coeffs:");
        for (int i = 0; i < 16384; ++i) printf(" %d", coeffsV[i]);
        printf("\n");

        uint8_t reconVQ[16384];
        int32_t coeffsVQ[16384];
        svtd_frame_v_dct_64x64_q(src, reconVQ, coeffsVQ, 100);
        printf("f64vq_recon:");
        for (int i = 0; i < 16384; ++i) printf(" %d", reconVQ[i]);
        printf("\n");
        printf("f64vq_coeffs:");
        for (int i = 0; i < 16384; ++i) printf(" %d", coeffsVQ[i]);
        printf("\n");
    }

    fprintf(stderr, "CK: f64 done\n"); fflush(stderr);

    // ---- B5: 8x8 builder goldens ----
    {
        // (a) V_PRED full-neighbor at TX_8X8
        {
            const uint8_t above[8] = {31, 12, 77, 4, 50, 23, 68, 15};
            uint8_t dst[64] = {0};
            svtd_call_builder_tx(dst, V_PRED, 0, FILTER_INTRA_MODES, 0, above, 8, 0, above, 0, 0, 0, TX_8X8);
            printf("b5_v8:"); for (int i = 0; i < 64; ++i) printf(" %d", dst[i]); printf("\n");
        }
        // (b) DC_PRED no-neighbor at TX_8X8
        {
            uint8_t dst[64] = {0};
            uint8_t dummy[1] = {0};
            svtd_call_builder_tx(dst, DC_PRED, 0, FILTER_INTRA_MODES, 0, dummy, 0, 0, dummy, 0, 0, 0, TX_8X8);
            printf("b5_dc128_8:"); for (int i = 0; i < 64; ++i) printf(" %d", dst[i]); printf("\n");
        }
        // (b5-c) D67 at TX_8X8 — upsample path (blk_wh=16, delta=67-90=-23, 0<d<40)
        {
            const uint8_t above[16] = {10, 20, 30, 100, 50, 60, 70, 80, 90, 40, 25, 66, 11, 72, 33, 58};
            const uint8_t left[8] = {9, 9, 9, 9, 9, 9, 9, 9};
            uint8_t dst[64] = {0};
            svtd_call_builder_tx(dst, D67_PRED, 0, FILTER_INTRA_MODES, 0, above, 8, 8, left, 8, 0, 7, TX_8X8);
            printf("b5_d67_8:"); for (int i = 0; i < 64; ++i) printf(" %d", dst[i]); printf("\n");
        }
        // (b5-d) FILTER_V_PRED at TX_8X8 — two-column strip case (bw=8, strips at c=1,5)
        {
            const uint8_t above[9] = {10, 20, 30, 40, 50, 60, 70, 80, 90};
            const uint8_t left[8] = {21, 31, 41, 51, 61, 71, 81, 91};
            uint8_t dst[64] = {0};
            svtd_call_builder_tx(dst, V_PRED, 0, FILTER_V_PRED /* 1 */, 0, above + 1, 8, 0, left, 8, 0, 10, TX_8X8);
            printf("b5_fiv8:"); for (int i = 0; i < 64; ++i) printf(" %d", dst[i]); printf("\n");
        }
        // (b5-e) D45 at TX_8X8 — edge-filtered (delta=-45, |d|>=40 → strength 1)
        {
            const uint8_t above[16] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 15, 25, 35, 45, 55, 65, 75};
            const uint8_t left[8] = {12, 22, 32, 42, 52, 62, 72, 82};
            uint8_t dst[64] = {0};
            svtd_call_builder_tx(dst, D45_PRED, 0, FILTER_INTRA_MODES, 0, above, 8, 8, left, 8, 0, 5, TX_8X8);
            printf("b5_d45ef_8:"); for (int i = 0; i < 64; ++i) printf(" %d", dst[i]); printf("\n");
        }
    }

    // ---- B7: 8x8 frame-policy composition (2x2 blocks of 8x8, 16x16 frame) ----
    {
        const uint8_t src[256] = {
            21,  3,  5,  9, 19,  2,  8, 14,  7, 13,  5,  1, 25,  4,  6, 18,
            15,  4, 25,  2, 12,  9, 30,  3, 10, 16,  6, 12,  3, 25, 11,  9,
            18,  5,  7, 13, 14,  2, 20,  8,  6, 24,  3,  9, 11, 17,  5, 19,
            22,  1,  8, 15,  4, 29,  7, 13, 16, 28, 12, 20,  2, 31,  9, 26,
             9, 11,  3,  7,  5, 23,  1, 17, 15,  4, 25,  2, 12,  9, 30,  3,
            10, 16,  6, 12,  3, 25, 11,  9, 18,  5,  7, 13, 14,  2, 20,  8,
             6, 24,  3,  9, 11, 17,  5, 19, 22,  1,  8, 15,  4, 29,  7, 13,
            16, 28, 12, 20,  2, 31,  9, 26, 21,  3,  5,  9, 19,  2,  8, 14,
             7, 13,  5,  1, 25,  4,  6, 18, 15,  4, 25,  2, 12,  9, 30,  3,
            10, 16,  6, 12,  3, 25, 11,  9, 18,  5,  7, 13, 14,  2, 20,  8,
             6, 24,  3,  9, 11, 17,  5, 19, 22,  1,  8, 15,  4, 29,  7, 13,
            16, 28, 12, 20,  2, 31,  9, 26,  9, 11,  3,  7,  5, 23,  1, 17,
            15,  4, 25,  2, 12,  9, 30,  3, 10, 16,  6, 12,  3, 25, 11,  9,
            18,  5,  7, 13, 14,  2, 20,  8,  6, 24,  3,  9, 11, 17,  5, 19,
            22,  1,  8, 15,  4, 29,  7, 13, 16, 28, 12, 20,  2, 31,  9, 26,
            21,  3,  5,  9, 19,  2,  8, 14,  7, 13,  5,  1, 25,  4,  6, 18,
        };
        uint8_t recon[256];
        int32_t coeffs[4 * 64];
        int modes[4] = {0, 0, 0, 0};
        svtd_frame_auto_8x8_blocks(src, recon, coeffs, modes);
        printf("b7_modes:");
        for (int i = 0; i < 4; ++i) printf(" %d", modes[i]);
        printf("\n");
        printf("b7_recon:");
        for (int i = 0; i < 256; ++i) printf(" %d", recon[i]);
        printf("\n");
        printf("b7_coeffs:");
        for (int i = 0; i < 4 * 64; ++i) printf(" %d", coeffs[i]);
        printf("\n");
    }

    // ---- inverse 2D add cores ----
    {
        const int32_t cdct[16] = {-520, 140, 324, 202, -17, 18, 102, -68, 56, 3, 36, 120, -18, 23, 6, -22};
        const int32_t cadst[16] = {-631, 53, 4, 55, -16, 2, 45, -26, -12, -27, -47, -2, 2, -2, 16, 53};
        uint8_t pred[16];
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) pred[r*4+c] = (uint8_t)("\x0a\x28\x1e\x14"[c]);
        svtd_inv2dadd4x4(cdct, pred, 4, svt_av1_idct4_new);
        printf("inv2d_dct_onto_vpred:"); for (int i = 0; i < 16; ++i) printf(" %d", pred[i]); printf("\n");
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) pred[r*4+c] = (uint8_t)("\x0a\x28\x1e\x14"[c]);
        svtd_inv2dadd4x4(cadst, pred, 4, svt_av1_iadst4_new);
        printf("inv2d_adst_onto_vpred:"); for (int i = 0; i < 16; ++i) printf(" %d", pred[i]); printf("\n");
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) pred[r*4+c] = 27;
        svtd_inv2dadd4x4(cadst, pred, 4, svt_av1_iadst4_new);
        printf("inv2d_adst_onto_dcpred:"); for (int i = 0; i < 16; ++i) printf(" %d", pred[i]); printf("\n");
    }

    // ---- E1 composition (V + DC blocks) ----
    {
        const uint8_t above_v[4] = {10, 40, 30, 20};
        const uint8_t src_v[16] = {21, 3, 5, 9, 9, 11, 3, 7, 7, 13, 5, 1, 15, 4, 25, 2};
        uint8_t pred[16];
        int16_t res[16];
        int32_t cb[16];
        svtd_builder_v(pred, above_v, 4);
        for (int i = 0; i < 16; ++i) res[i] = (int16_t)(src_v[i] - pred[i]);
        svtd_fwd2d4x4(res, 4, cb, svt_av1_fdct4_new);
        printf("e1_v_dct:"); for (int i = 0; i < 16; ++i) printf(" %d", cb[i]); printf("\n");

        const uint8_t above_dc[4] = {12, 24, 36, 48};
        const uint8_t left_dc[4] = {6, 18, 30, 42};
        const uint8_t src_dc[16] = {9, 4, 7, 5, 12, 8, 3, 6, 15, 2, 11, 4, 6, 9, 13, 2};
        svtd_builder_dc(pred, above_dc, left_dc, 4, 4);
        for (int i = 0; i < 16; ++i) res[i] = (int16_t)(src_dc[i] - pred[i]);
        svtd_fwd2d4x4(res, 4, cb, svt_av1_fdct4_new);
        printf("e1_dc_dct:"); for (int i = 0; i < 16; ++i) printf(" %d", cb[i]); printf("\n");
    }

    // ---- filter-intra modes 0-4 (corner 10, above {20,30,40,50}, left {21,31,41,51}) ----
    {
        uint8_t ab[6] = {10, 20, 30, 40, 50};
        const uint8_t left[4] = {21, 31, 41, 51};
        uint8_t dst[16];
        for (int m = 0; m < 5; ++m) {
            svt_av1_filter_intra_predictor_c(dst, 4, TX_4X4, ab + 1, left, m);
            printf("fi_mode%d:", m); for (int i = 0; i < 16; ++i) printf(" %d", dst[i]); printf("\n");
        }
    }

    // ---- E5: D67 on/off (non-linear above) ----
    {
        const uint8_t above[8] = {10, 20, 30, 100, 50, 60, 70, 80};
        uint8_t dst[16];
        svtd_builder_d67(dst, above, 4, 4, 7, 0);
        printf("e5_d67_on:"); for (int i = 0; i < 16; ++i) printf(" %d", dst[i]); printf("\n");
        svtd_builder_d67(dst, above, 4, 4, 7, 1);
        printf("e5_d67_off:"); for (int i = 0; i < 16; ++i) printf(" %d", dst[i]); printf("\n");
    }

    // ---- SAD 4x4 (helper at width=height=4) ----
    {
        uint8_t s[16], r[16];
        for (int i = 0; i < 16; ++i) { s[i] = (uint8_t)(i + 1); r[i] = 0; }
        printf("sad4x4_ramp_vs_zero %u\n", svt_nxm_sad_kernel_helper_c(s, 4, r, 4, 4, 4));
        uint8_t s2[64];
        uint8_t r2[64];
        memset(s2, 0, 64);
        memset(r2, 0, 64);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                s2[y * 8 + x] = (uint8_t)(y * 8 + x);
                r2[y * 8 + x] = (uint8_t)(63 - (y * 8 + x));
            }
        }
        printf("sad4x4_revramp_stride8 %u\n", svt_nxm_sad_kernel_helper_c(s2, 8, r2, 8, 4, 4));
        printf("sad8x8_revramp_stride8 %u\n", svt_nxm_sad_kernel_helper_c(s2, 8, r2, 8, 8, 8));
    }

    // ---- F1 frame (V + DCT, 8x8) ----
    {
        const uint8_t src[64] = {21, 3,  5,  9,  19, 2, 8,  14, 9,  11, 3, 7,  5,  23, 1, 17,
                                 7,  13, 5,  1,  25, 4, 6,  18, 15, 4,  25, 2,  12, 9,  30, 3,
                                 18, 5,  7,  13, 14, 2, 20, 8,  6,  24, 3,  9,  11, 17, 5, 19,
                                 22, 1,  8,  15, 4,  29, 7, 13, 10, 16, 6, 12, 3,  25, 11, 9};
        uint8_t recon[64];
        int32_t coeffs[64];
        svtd_frame_v_dct_8x8(src, recon, coeffs);
        printf("f1_recon:"); for (int i = 0; i < 64; ++i) printf(" %d", recon[i]); printf("\n");
        printf("f1_coeffs:"); for (int i = 0; i < 64; ++i) printf(" %d", coeffs[i]); printf("\n");
    }

    // ---- D2 per-candidate SADs (4 fixtures) ----
    {
        // fixture V: rows identical to the above row
        {
            const uint8_t above[4] = {10, 20, 30, 40};
            const uint8_t src[16] = {10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40};
            printf("d2_v:");
            for (int m = 0; m <= PAETH_PRED; ++m) {
                uint8_t pred[16];
                svtd_call_builder(pred, m, 0, FILTER_INTRA_MODES, 0, above, 4, 0, above /*unused*/, 0, 0, 0);
                printf(" %u", svt_nxm_sad_kernel_helper_c(src, 4, pred, 4, 4, 4));
            }
            printf("\n");
        }
        // fixture H: rows constant, left column = row value
        {
            const uint8_t left[4] = {5, 10, 15, 20};
            const uint8_t src[16] = {5, 5, 5, 5, 10, 10, 10, 10, 15, 15, 15, 15, 20, 20, 20, 20};
            printf("d2_h:");
            for (int m = 0; m <= PAETH_PRED; ++m) {
                uint8_t pred[16];
                svtd_call_builder(pred, m, 0, FILTER_INTRA_MODES, 0, left /*above unused*/, 0, 0, left, 4, 0, 0);
                printf(" %u", svt_nxm_sad_kernel_helper_c(src, 4, pred, 4, 4, 4));
            }
            printf("\n");
        }
        // fixture DC: flat block, all edges 50 (tie-break check)
        {
            const uint8_t above[4] = {50, 50, 50, 50};
            const uint8_t left[4] = {50, 50, 50, 50};
            const uint8_t src[16] = {50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50};
            printf("d2_dc:");
            for (int m = 0; m <= PAETH_PRED; ++m) {
                uint8_t pred[16];
                svtd_call_builder(pred, m, 0, FILTER_INTRA_MODES, 0, above, 4, 0, left, 4, 0, 50);
                printf(" %u", svt_nxm_sad_kernel_helper_c(src, 4, pred, 4, 4, 4));
            }
            printf("\n");
        }
        // fixture D45: diagonal ramp matching the 45-degree above direction
        {
            const uint8_t above[8] = {0, 10, 20, 30, 40, 50, 60, 70};
            const uint8_t left[4] = {0, 10, 20, 30};
            uint8_t src[16];
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) src[y * 4 + x] = (uint8_t)(10 * (x + y + 1));
            printf("d2_d45:");
            for (int m = 0; m <= PAETH_PRED; ++m) {
                uint8_t pred[16];
                svtd_call_builder(pred, m, 0, FILTER_INTRA_MODES, 0, above, 4, 4, left, 4, 0, 0);
                printf(" %u", svt_nxm_sad_kernel_helper_c(src, 4, pred, 4, 4, 4));
            }
            printf("\n");
        }
    }
    // ---- D3 frame-policy composition (identical policy to main_frame) ----
    {
        const uint8_t src[64] = {21, 3,  5,  9,  19, 2, 8,  14, 9,  11, 3, 7,  5,  23, 1, 17,
                                 7,  13, 5,  1,  25, 4, 6,  18, 15, 4,  25, 2,  12, 9,  30, 3,
                                 18, 5,  7,  13, 14, 2, 20, 8,  6,  24, 3,  9,  11, 17, 5, 19,
                                 22, 1,  8,  15, 4,  29, 7, 13, 10, 16, 6, 12, 3,  25, 11, 9};
        uint8_t recon[64];
        int32_t coeffs[64];
        int modes[4] = {0, 0, 0, 0};
        svtd_frame_auto_8x8(src, recon, coeffs, modes);
        printf("d3_modes:");
        for (int i = 0; i < 4; ++i) printf(" %d", modes[i]);
        printf("\n");
        printf("d3_recon:");
        for (int i = 0; i < 64; ++i) printf(" %d", recon[i]);
        printf("\n");
        printf("d3_coeffs:");
        for (int i = 0; i < 64; ++i) printf(" %d", coeffs[i]);
        printf("\n");
    }
    // ---- Q0: quantizer gate lines ----
    {
        // default scan 4x4 (coefficients.c:345-363 formula)
        int16_t scan[16];
        svtd_default_scan_4x4(scan);
        printf("qscan4:"); for (int i = 0; i < 16; ++i) printf(" %d", scan[i]); printf("\n");

        // luma quantizer tables at qindex 0, 1, 100, 200, 255
        const int qs[5] = {0, 1, 100, 200, 255};
        for (int qi = 0; qi < 5; ++qi) {
            SvtdQuantTables t;
            svtd_build_quantizer_luma(qs[qi], &t);
            printf("qtab_q%d:", qs[qi]);
            for (int i = 0; i < 2; ++i) printf(" %d", t.quant[i]);
            for (int i = 0; i < 2; ++i) printf(" %d", t.quant_shift[i]);
            for (int i = 0; i < 2; ++i) printf(" %d", t.quant_fp[i]);
            for (int i = 0; i < 2; ++i) printf(" %d", t.round_fp[i]);
            for (int i = 0; i < 2; ++i) printf(" %d", t.zbin[i]);
            for (int i = 0; i < 2; ++i) printf(" %d", t.round[i]);
            for (int i = 0; i < 2; ++i) printf(" %d", t.dequant[i]);
            printf("\n");
        }

        // quantize fixture = d3_coeffs block 0 (the first 16 values of the
        // D3 frame golden above; real fwd-txfm output incl. -3784 DC and
        // sub-threshold ACs)
        const TranLow fix[16] = {-3784, 78, 4, 54, -17, 18, 102, -68, 56, 3, 36, 120, -18, 23, 6, -22};
        for (int qi = 0; qi < 5; ++qi) {
            SvtdQuantTables t;
            svtd_build_quantizer_luma(qs[qi], &t);
            TranLow qc[16], dq[16];
            uint16_t eob = 0;
            svtd_quantize_fp_4x4(fix, &t, scan, qc, dq, &eob);
            printf("qfp_q%d:", qs[qi]);
            for (int i = 0; i < 16; ++i) printf(" %d", qc[i]);
            printf(" |");
            for (int i = 0; i < 16; ++i) printf(" %d", dq[i]);
            printf(" | %u\n", eob);
            svtd_quantize_b_4x4(fix, &t, scan, qc, dq, &eob);
            printf("qb_q%d:", qs[qi]);
            for (int i = 0; i < 16; ++i) printf(" %d", qc[i]);
            printf(" |");
            for (int i = 0; i < 16; ++i) printf(" %d", dq[i]);
            printf(" | %u\n", eob);
        }
    }
    // ---- Q2: quantized 4x4 frame-policy composition (fixed qindex) ----
    {
        const uint8_t src[64] = {21, 3,  5,  9,  19, 2, 8,  14, 9,  11, 3, 7,  5,  23, 1, 17,
                                 7,  13, 5,  1,  25, 4, 6,  18, 15, 4,  25, 2,  12, 9,  30, 3,
                                 18, 5,  7,  13, 14, 2, 20, 8,  6,  24, 3,  9,  11, 17, 5, 19,
                                 22, 1,  8,  15, 4,  29, 7, 13, 10, 16, 6, 12, 3,  25, 11, 9};
        uint8_t recon[64];
        int32_t coeffs[64];
        int modes[4] = {0, 0, 0, 0};
        svtd_frame_auto_4x4_q(src, recon, coeffs, modes, 100);
        printf("q2f_modes:");
        for (int i = 0; i < 4; ++i) printf(" %d", modes[i]);
        printf("\n");
        printf("q2f_recon:");
        for (int i = 0; i < 64; ++i) printf(" %d", recon[i]);
        printf("\n");
        printf("q2f_coeffs:");
        for (int i = 0; i < 64; ++i) printf(" %d", coeffs[i]);
        printf("\n");
    }
    // ---- QC1: 8x8 quantization + ADST-proof gate lines ----
    {
        // default scan 8x8 (coefficients.c:345-363 formula at W=H=8)
        int16_t scan8[64];
        svtd_default_scan_8x8(scan8);
        printf("qscan8:"); for (int i = 0; i < 64; ++i) printf(" %d", scan8[i]); printf("\n");

        SvtdQuantTables t100, t0;
        svtd_build_quantizer_luma(100, &t100);
        svtd_build_quantizer_luma(0, &t0);

        // 8x8 DCT fixture = the fwd2d8_dct output (recomputed for provenance)
        const int16_t in8[64] = {12, 45, 3, 78, 22, 91, 6, 30,
                                 67, 8, 54, 11, 39, 71, 17, 48,
                                 2, 90, 25, 63, 7, 44, 85, 19,
                                 51, 36, 9, 77, 28, 5, 60, 83,
                                 15, 72, 41, 4, 88, 33, 26, 58,
                                 80, 13, 66, 47, 1, 95, 38, 70,
                                 24, 56, 10, 82, 31, 68, 14, 42,
                                 75, 29, 87, 20, 53, 16, 79, 34};
        int32_t cdct8[64];
        svtd_fwd2d8x8(in8, 8, cdct8, svt_av1_fdct8_new);
        TranLow qc[64], dq[64];
        uint16_t eob = 0;
        svtd_quantize_fp_8x8(cdct8, &t100, scan8, qc, dq, &eob);
        printf("q8fp_q100:");
        for (int i = 0; i < 64; ++i) printf(" %d", qc[i]);
        printf(" |");
        for (int i = 0; i < 64; ++i) printf(" %d", dq[i]);
        printf(" | %u\n", eob);
        svtd_quantize_b_8x8(cdct8, &t100, scan8, qc, dq, &eob);
        printf("q8b_q100:");
        for (int i = 0; i < 64; ++i) printf(" %d", qc[i]);
        printf(" |");
        for (int i = 0; i < 64; ++i) printf(" %d", dq[i]);
        printf(" | %u\n", eob);
        svtd_quantize_fp_8x8(cdct8, &t0, scan8, qc, dq, &eob);
        printf("q8fp_q0:");
        for (int i = 0; i < 64; ++i) printf(" %d", qc[i]);
        printf(" |");
        for (int i = 0; i < 64; ++i) printf(" %d", dq[i]);
        printf(" | %u\n", eob);

        // ADST proof: the fp/b helpers are TxType-agnostic; bind ADST-produced
        // coeff vectors at both geometries through the same verbatim helper
        {
            const int16_t in[16] = {9, 2, 3, 1, 5, 6, 7, 8, 8, 7, 6, 5, 4, 3, 9, 1};
            int32_t cadst[16];
            svtd_fwd2d4x4(in, 4, cadst, svt_av1_fadst4_new);
            int16_t scan4[16];
            svtd_default_scan_4x4(scan4);
            TranLow qc4[16], dq4[16];
            uint16_t eob4 = 0;
            svtd_quantize_fp_4x4(cadst, &t100, scan4, qc4, dq4, &eob4);
            printf("qadst4fp_q100:");
            for (int i = 0; i < 16; ++i) printf(" %d", qc4[i]);
            printf(" |");
            for (int i = 0; i < 16; ++i) printf(" %d", dq4[i]);
            printf(" | %u\n", eob4);
            svtd_quantize_b_4x4(cadst, &t100, scan4, qc4, dq4, &eob4);
            printf("qadst4b_q100:");
            for (int i = 0; i < 16; ++i) printf(" %d", qc4[i]);
            printf(" |");
            for (int i = 0; i < 16; ++i) printf(" %d", dq4[i]);
            printf(" | %u\n", eob4);
        }
        {
            int32_t cadst8[64];
            svtd_fwd2d8x8(in8, 8, cadst8, svt_av1_fadst8_new);
            TranLow qc8[64], dq8[64];
            uint16_t eob8 = 0;
            svtd_quantize_fp_8x8(cadst8, &t100, scan8, qc8, dq8, &eob8);
            printf("qadst8fp_q100:");
            for (int i = 0; i < 64; ++i) printf(" %d", qc8[i]);
            printf(" |");
            for (int i = 0; i < 64; ++i) printf(" %d", dq8[i]);
            printf(" | %u\n", eob8);
        }

        // ---- C6: 16x16 quantization gate lines ----
        // fixture = the fwd2d16_dct output (recomputed for provenance)
        {
            int16_t in16[256];
            for (int r = 0; r < 16; ++r)
                for (int c = 0; c < 16; ++c)
                    in16[r * 16 + c] = (int16_t)((c * 13 + r * 7 + ((c * r) & 31)) % 211) - 105;
            int32_t cdct16[256];
            svtd_fwd2d16x16(in16, 16, cdct16, svt_av1_fdct16_new);
            int16_t scan16[256];
            svtd_default_scan_16x16(scan16);
            TranLow qc16[256], dq16[256];
            uint16_t eob16 = 0;
            svtd_quantize_fp_16x16(cdct16, &t100, scan16, qc16, dq16, &eob16);
            printf("q16fp_q100:");
            for (int i = 0; i < 256; ++i) printf(" %d", qc16[i]);
            printf(" |");
            for (int i = 0; i < 256; ++i) printf(" %d", dq16[i]);
            printf(" | %u\n", eob16);
            svtd_quantize_b_16x16(cdct16, &t100, scan16, qc16, dq16, &eob16);
            printf("q16b_q100:");
            for (int i = 0; i < 256; ++i) printf(" %d", qc16[i]);
            printf(" |");
            for (int i = 0; i < 256; ++i) printf(" %d", dq16[i]);
            printf(" | %u\n", eob16);
            svtd_quantize_fp_16x16(cdct16, &t0, scan16, qc16, dq16, &eob16);
            printf("q16fp_q0:");
            for (int i = 0; i < 256; ++i) printf(" %d", qc16[i]);
            printf(" |");
            for (int i = 0; i < 256; ++i) printf(" %d", dq16[i]);
            printf(" | %u\n", eob16);
        }
    }
    // ---- QW1: 8x8 frame-policy-with-quant composition (fixed qindex) ----
    {
        // same 16x16 fixture as the B7 composition (b7_modes 1 5 6 0 lossless)
        const uint8_t src[256] = {
            21,  3,  5,  9, 19,  2,  8, 14,  7, 13,  5,  1, 25,  4,  6, 18,
            15,  4, 25,  2, 12,  9, 30,  3, 10, 16,  6, 12,  3, 25, 11,  9,
            18,  5,  7, 13, 14,  2, 20,  8,  6, 24,  3,  9, 11, 17,  5, 19,
            22,  1,  8, 15,  4, 29,  7, 13, 16, 28, 12, 20,  2, 31,  9, 26,
             9, 11,  3,  7,  5, 23,  1, 17, 15,  4, 25,  2, 12,  9, 30,  3,
            10, 16,  6, 12,  3, 25, 11,  9, 18,  5,  7, 13, 14,  2, 20,  8,
             6, 24,  3,  9, 11, 17,  5, 19, 22,  1,  8, 15,  4, 29,  7, 13,
            16, 28, 12, 20,  2, 31,  9, 26, 21,  3,  5,  9, 19,  2,  8, 14,
             7, 13,  5,  1, 25,  4,  6, 18, 15,  4, 25,  2, 12,  9, 30,  3,
            10, 16,  6, 12,  3, 25, 11,  9, 18,  5,  7, 13, 14,  2, 20,  8,
             6, 24,  3,  9, 11, 17,  5, 19, 22,  1,  8, 15,  4, 29,  7, 13,
            16, 28, 12, 20,  2, 31,  9, 26,  9, 11,  3,  7,  5, 23,  1, 17,
            15,  4, 25,  2, 12,  9, 30,  3, 10, 16,  6, 12,  3, 25, 11,  9,
            18,  5,  7, 13, 14,  2, 20,  8,  6, 24,  3,  9, 11, 17,  5, 19,
            22,  1,  8, 15,  4, 29,  7, 13, 16, 28, 12, 20,  2, 31,  9, 26,
            21,  3,  5,  9, 19,  2,  8, 14,  7, 13,  5,  1, 25,  4,  6, 18};
        uint8_t recon[256];
        int32_t coeffs[4 * 64];
        int modes[4] = {0, 0, 0, 0};
        svtd_frame_auto_8x8_q(src, recon, coeffs, modes, 100);
        printf("qw8_modes:");
        for (int i = 0; i < 4; ++i) printf(" %d", modes[i]);
        printf("\n");
        printf("qw8_recon:");
        for (int i = 0; i < 256; ++i) printf(" %d", recon[i]);
        printf("\n");
        printf("qw8_coeffs:");
        for (int i = 0; i < 4 * 64; ++i) printf(" %d", coeffs[i]);
        printf("\n");
    }
    // ---- R2: builder angle-delta goldens for V/H at both geometries ----
    // The host builder handles delta already (verbatim
    // build_intra_predictors: pAngle = mode_to_angle_map[mode] + delta*3);
    // these gate lines pin it so the kernel isDr parity tests have goldens.
    // V delta=+1 -> pAngle 93 (zone 2); H delta=-1 -> pAngle 177 (zone 2).
    {
        const uint8_t above[16] = {31, 12, 77, 4, 50, 23, 68, 15, 9, 41, 27, 63, 11, 55, 38, 72};
        const uint8_t left[8] = {14, 3, 8, 13, 17, 9, 19, 26};
        {
            uint8_t dst[64] = {0};
            svtd_call_builder_tx(dst, V_PRED, 1, FILTER_INTRA_MODES, 0, above, 8, 8, left, 8, 0, 7, TX_8X8);
            printf("b9_vd1_8:"); for (int i = 0; i < 64; ++i) printf(" %d", dst[i]); printf("\n");
            svtd_call_builder_tx(dst, H_PRED, -1, FILTER_INTRA_MODES, 0, above, 8, 8, left, 8, 0, 7, TX_8X8);
            printf("b9_hm1_8:"); for (int i = 0; i < 64; ++i) printf(" %d", dst[i]); printf("\n");
        }
        {
            uint8_t dst[16] = {0};
            svtd_call_builder_tx(dst, V_PRED, 1, FILTER_INTRA_MODES, 0, above, 4, 4, left, 4, 0, 7, TX_4X4);
            printf("b9_vd1_4:"); for (int i = 0; i < 16; ++i) printf(" %d", dst[i]); printf("\n");
            svtd_call_builder_tx(dst, H_PRED, -1, FILTER_INTRA_MODES, 0, above, 4, 4, left, 4, 0, 7, TX_4X4);
            printf("b9_hm1_4:"); for (int i = 0; i < 16; ++i) printf(" %d", dst[i]); printf("\n");
        }
    }
    // ---- FR-series: real top-right fixture ----
    // rows 0-7 = diagonal ramp 10*(x+y+1) (max 230, no overflow), rows 8-15
    // zero. The four row-1 4x4 blocks are the d2_d45 structure shifted by 3
    // rows (above row + REAL top-right = the ramp continuation), so D45 wins
    // with SAD 0 and its zone-1 prediction reads above[4..7] — the extension
    // must be REAL reconstructed samples, not zeros.
    {
        uint8_t src[256];
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                src[y * 16 + x] = (y < 8) ? (uint8_t)(10 * (x + y + 1)) : 0;
            }
        }
        uint8_t recon[256];
        int32_t coeffs[256];
        int modes[16] = {0};
        svtd_frame_auto_4x4_16x16(src, recon, coeffs, modes);
        printf("fr_modes:");
        for (int i = 0; i < 16; ++i) printf(" %d", modes[i]);
        printf("\n");
        printf("fr_recon:");
        for (int i = 0; i < 256; ++i) printf(" %d", recon[i]);
        printf("\n");
        printf("fr_coeffs:");
        for (int i = 0; i < 256; ++i) printf(" %d", coeffs[i]);
        printf("\n");
    }
    // ---- C7: 16x16 frame-policy gate lines (32x32, 2x2 of 16x16) ----
    // rows 0-15 = ramp 4*(x+y+1) (max 4*31 = 124 < 255; row 15 recon =
    // above for row-1 blocks; above[j] = 4j+64 and D45 zone-1 pred
    // above[1+r+c] = 4*(r+c+17) = src[16+r][c] -> SAD 0 only with REAL TR),
    // rows 16-31 zero.
    {
        uint8_t src[1024];
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                src[y * 32 + x] = (y < 16) ? (uint8_t)(4 * (x + y + 1)) : 0;
            }
        }
        uint8_t recon[1024];
        int32_t coeffs[1024];
        int modes[4] = {0};
        svtd_frame_auto_16x16_blocks(src, recon, coeffs, modes);
        printf("f16_modes:");
        for (int i = 0; i < 4; ++i) printf(" %d", modes[i]);
        printf("\n");
        printf("f16_recon:");
        for (int i = 0; i < 1024; ++i) printf(" %d", recon[i]);
        printf("\n");
        printf("f16_coeffs:");
        for (int i = 0; i < 1024; ++i) printf(" %d", coeffs[i]);
        printf("\n");

        uint8_t reconQ[1024];
        int32_t coeffsQ[1024];
        int modesQ[4] = {0};
        svtd_frame_auto_16x16_q(src, reconQ, coeffsQ, modesQ, 100);
        printf("f16q_modes:");
        for (int i = 0; i < 4; ++i) printf(" %d", modesQ[i]);
        printf("\n");
        printf("f16q_recon:");
        for (int i = 0; i < 1024; ++i) printf(" %d", reconQ[i]);
        printf("\n");
        printf("f16q_coeffs:");
        for (int i = 0; i < 1024; ++i) printf(" %d", coeffsQ[i]);
        printf("\n");
    }
    // ---- HK3: forced-mode 16x16 frame gate lines (V_PRED + DCT, q100 Q) ----
    // same f16 fixture (rows 0-15 ramp 4*(x+y+1), rows 16-31 zero); closes the
    // C7b gap: encodeFrameRecon16x16 / encodeFrameRecon16x16Q get gate pins.
    {
        uint8_t src[1024];
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                src[y * 32 + x] = (y < 16) ? (uint8_t)(4 * (x + y + 1)) : 0;
            }
        }
        uint8_t recon[1024];
        int32_t coeffs[1024];
        svtd_frame_v_dct_16x16(src, recon, coeffs);
        printf("f16v_recon:");
        for (int i = 0; i < 1024; ++i) printf(" %d", recon[i]);
        printf("\n");
        printf("f16v_coeffs:");
        for (int i = 0; i < 1024; ++i) printf(" %d", coeffs[i]);
        printf("\n");

        uint8_t reconQ[1024];
        int32_t coeffsQ[1024];
        svtd_frame_v_dct_16x16_q(src, reconQ, coeffsQ, 100);
        printf("f16vq_recon:");
        for (int i = 0; i < 1024; ++i) printf(" %d", reconQ[i]);
        printf("\n");
        printf("f16vq_coeffs:");
        for (int i = 0; i < 1024; ++i) printf(" %d", coeffsQ[i]);
        printf("\n");
    }

    // ---- CH0: chroma (UV) builder gate lines at 4x4 ----
    // SVT chroma flow: uv_mode folds to the LUMA primitive set via g_uv2y
    // (get_uv_mode, common_utils.h:130-133; UV_CFL_PRED -> DC_PRED,
    // common_utils.c:28) and chroma NEVER uses filter-intra
    // (enc_intra_prediction.c:641 passes FILTER_INTRA_MODES for plane != 0).
    // The predictors themselves are plane-agnostic. Fixture policy (OURS —
    // SVT has no fixtures): UV-subsampled edges aboveUV[8] = (5+3i)%237,
    // leftUV[8] = (11+7i)%237, corner 13 (distinct from every luma fixture
    // so these lines cannot alias a luma line).
    {
        const uint8_t aboveUV[8] = {5, 8, 11, 14, 17, 20, 23, 26};
        const uint8_t leftUV[8] = {11, 18, 25, 32, 39, 46, 53, 60};
        uint8_t dst[16];
        // (0) the fold table itself
        printf("uv2y:");
        for (int i = 0; i < 16; ++i) printf(" %d", g_uv2y[i]);
        printf("\n");
        // (a) UV_V_PRED delta 0 (folds to V_PRED; above-only)
        svtd_call_builder_tx(dst, g_uv2y[UV_V_PRED], 0, FILTER_INTRA_MODES, 0, aboveUV, 4, 0,
                             aboveUV, 0, 0, 0, TX_4X4);
        printf("bc4_uvv:");
        for (int i = 0; i < 16; ++i) printf(" %d", dst[i]);
        printf("\n");
        // (b) UV_DC_PRED both edges
        svtd_call_builder_tx(dst, g_uv2y[UV_DC_PRED], 0, FILTER_INTRA_MODES, 0, aboveUV, 4, 0,
                             leftUV, 4, 0, 13, TX_4X4);
        printf("bc4_uvdc:");
        for (int i = 0; i < 16; ++i) printf(" %d", dst[i]);
        printf("\n");
        // (c) UV_D45_PRED delta 0, REAL top-right (nTop 4 + nTr 4 = 8)
        svtd_call_builder_tx(dst, g_uv2y[UV_D45_PRED], 0, FILTER_INTRA_MODES, 0, aboveUV, 4, 4,
                             leftUV, 4, 0, 13, TX_4X4);
        printf("bc4_uvd45:");
        for (int i = 0; i < 16; ++i) printf(" %d", dst[i]);
        printf("\n");
        // (d) UV_D45_PRED delta -1 (p_angle 42, zone 1, dx = der[42])
        svtd_call_builder_tx(dst, g_uv2y[UV_D45_PRED], -1, FILTER_INTRA_MODES, 0, aboveUV, 4, 4,
                             leftUV, 4, 0, 13, TX_4X4);
        printf("bc4_uvd45m1:");
        for (int i = 0; i < 16; ++i) printf(" %d", dst[i]);
        printf("\n");
        // (e) UV_CFL_PRED above-only: folds to DC_PRED with no left edge ->
        // dc_top path; discriminates the fold target (DC) from any other
        // interpretation
        svtd_call_builder_tx(dst, g_uv2y[UV_CFL_PRED], 0, FILTER_INTRA_MODES, 0, aboveUV, 4, 0,
                             aboveUV, 0, 0, 0, TX_4X4);
        printf("bc4_uvcfl:");
        for (int i = 0; i < 16; ++i) printf(" %d", dst[i]);
        printf("\n");
        // (f) UV_SMOOTH_PRED (folds to SMOOTH, sm_weight_arrays bs=4 row)
        svtd_call_builder_tx(dst, g_uv2y[UV_SMOOTH_PRED], 0, FILTER_INTRA_MODES, 0, aboveUV, 4, 0,
                             leftUV, 4, 0, 13, TX_4X4);
        printf("bc4_uvsmooth:");
        for (int i = 0; i < 16; ++i) printf(" %d", dst[i]);
        printf("\n");
        // (g) UV_PAETH_PRED (folds to PAETH; base vs left/top/topLeft)
        svtd_call_builder_tx(dst, g_uv2y[UV_PAETH_PRED], 0, FILTER_INTRA_MODES, 0, aboveUV, 4, 0,
                             leftUV, 4, 0, 13, TX_4X4);
        printf("bc4_uvpaeth:");
        for (int i = 0; i < 16; ++i) printf(" %d", dst[i]);
        printf("\n");
    }

    // ---- CH3: chroma frame-policy gate lines (4:2:0) ----
    // UV plane 32x32 = 4:2:0 box average ((sum+2)>>2) of a 64x64 luma fixture
    // with rows 0-31 = ramp x+y+1, rows 32-63 = 0: UV rows 0-15 = 2*(i+j+2),
    // rows 16-31 = 0. 2x2 grid of 16x16 UV blocks; the chroma fold (g_uv2y)
    // and FI-free builder are inside svtd_frame_chroma_*.
    {
        uint8_t srcUV[1024];
        for (int i = 0; i < 32; ++i) {
            for (int j = 0; j < 32; ++j) {
                srcUV[i * 32 + j] = (i < 16) ? (uint8_t)(2 * (i + j + 2)) : 0;
            }
        }
        uint8_t recon[1024];
        int32_t coeffs[1024];
        int modes[4] = {0};
        svtd_frame_chroma_auto_16x16_blocks(srcUV, recon, coeffs, modes);
        printf("bcf16_modes:");
        for (int i = 0; i < 4; ++i) printf(" %d", modes[i]);
        printf("\n");
        printf("bcf16_recon:");
        for (int i = 0; i < 1024; ++i) printf(" %d", recon[i]);
        printf("\n");
        printf("bcf16_coeffs:");
        for (int i = 0; i < 1024; ++i) printf(" %d", coeffs[i]);
        printf("\n");

        uint8_t reconQ[1024];
        int32_t coeffsQ[1024];
        int modesQ[4] = {0};
        svtd_frame_chroma_auto_16x16_q(srcUV, reconQ, coeffsQ, modesQ, 100);
        printf("bcf16q_modes:");
        for (int i = 0; i < 4; ++i) printf(" %d", modesQ[i]);
        printf("\n");
        printf("bcf16q_coeffs:");
        for (int i = 0; i < 1024; ++i) printf(" %d", coeffsQ[i]);
        printf("\n");

        uint8_t reconV[1024];
        int32_t coeffsV[1024];
        svtd_frame_chroma_v_dct_16x16(srcUV, reconV, coeffsV);
        printf("bcf16v_recon:");
        for (int i = 0; i < 1024; ++i) printf(" %d", reconV[i]);
        printf("\n");
        printf("bcf16v_coeffs:");
        for (int i = 0; i < 1024; ++i) printf(" %d", coeffsV[i]);
        printf("\n");

        uint8_t reconVQ[1024];
        int32_t coeffsVQ[1024];
        svtd_frame_chroma_v_dct_16x16_q(srcUV, reconVQ, coeffsVQ, 100);
        printf("bcf16vq_recon:");
        for (int i = 0; i < 1024; ++i) printf(" %d", reconVQ[i]);
        printf("\n");
        printf("bcf16vq_coeffs:");
        for (int i = 0; i < 1024; ++i) printf(" %d", coeffsVQ[i]);
        printf("\n");
    }

    // ---- entropy coder ground floor (EC0) ----
    {
        // 1) bool_eq sequence (aom_write_bit path, bitstream_unit.h:255-257)
        ec_reset();
        const int bits[12] = {1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0};
        for (int i = 0; i < 12; ++i) svt_od_ec_encode_bool_eq_q15(&ec_enc, bits[i]);
        uint32_t n = 0;
        svt_od_ec_enc_done(&ec_enc, &n);
        ec_print_bytes("ec_enc_bool_eq", n);
        // decode side of the same bits: aom_read_bit resolves to
        // od_ec_decode_bool_q15(f=16384) (bitreader.h:71-75, aom_read's
        // (0x7FFFFF-(128<<15)+128)>>8 = 16384)
        od_ec_dec_init(&ec_dec, ec_buf, n);
        int rd[12], bad = 0;
        for (int i = 0; i < 12; ++i) { rd[i] = od_ec_decode_bool_q15(&ec_dec, 16384); if (rd[i] != bits[i]) bad = 1; }
        printf("ec_roundtrip_bool_eq %d", rd[0]);
        for (int i = 1; i < 12; ++i) printf(" %d", rd[i]);
        printf("\n");
        if (bad) { fprintf(stderr, "EC0 roundtrip bool_eq FAILED\n"); return 1; }
    }
    {
        // 2) bool with explicit f (svt_od_ec_encode_bool_q15)
        ec_reset();
        const int bits[8] = {0, 1, 1, 0, 1, 0, 0, 1};
        for (int i = 0; i < 8; ++i) svt_od_ec_encode_bool_q15(&ec_enc, bits[i], 16384);
        uint32_t n = 0;
        svt_od_ec_enc_done(&ec_enc, &n);
        ec_print_bytes("ec_enc_bool_f16384", n);
        uint8_t snap[64];
        memcpy(snap, ec_buf, n);
        od_ec_dec_init(&ec_dec, ec_buf, n);
        int bad = 0;
        for (int i = 0; i < 8; ++i) if (od_ec_decode_bool_q15(&ec_dec, 16384) != bits[i]) bad = 1;
        if (bad) { fprintf(stderr, "EC0 roundtrip bool f16384 FAILED\n"); return 1; }
        // bool_eq over the same bits: provable equivalence
        // (bitstream_unit.h:267-270) - byte-identical output
        ec_reset();
        for (int i = 0; i < 8; ++i) svt_od_ec_encode_bool_eq_q15(&ec_enc, bits[i]);
        uint32_t n2 = 0;
        svt_od_ec_enc_done(&ec_enc, &n2);
        printf("ec_booleq_vs_bool16384 %d\n", (n2 == n) ? 1 : 0);
        if (n2 != n || memcmp(snap, ec_buf, n)) {
            fprintf(stderr, "EC0 bool_eq/bool16384 equivalence FAILED\n");
            return 1;
        }
        // different f, same decoder
        ec_reset();
        for (int i = 0; i < 8; ++i) svt_od_ec_encode_bool_q15(&ec_enc, bits[i], 8192);
        uint32_t nf = 0;
        svt_od_ec_enc_done(&ec_enc, &nf);
        ec_print_bytes("ec_enc_bool_f8192", nf);
        od_ec_dec_init(&ec_dec, ec_buf, nf);
        printf("ec_roundtrip_bool_f8192");
        for (int i = 0; i < 8; ++i) printf(" %d", od_ec_decode_bool_q15(&ec_dec, 8192));
        printf("\n");
    }
    {
        // 3) cdf sequence, 13-symbol fixture icdf
        ec_reset();
        const int syms[10] = {0, 5, 12, 3, 5, 5, 1, 0, 7, 9};
        for (int i = 0; i < 10; ++i) svt_od_ec_encode_cdf_q15(&ec_enc, syms[i], ec_icdf13, 13);
        uint32_t n = 0;
        svt_od_ec_enc_done(&ec_enc, &n);
        ec_print_bytes("ec_enc_cdf13", n);
        od_ec_dec_init(&ec_dec, ec_buf, n);
        int rd[10], bad = 0;
        for (int i = 0; i < 10; ++i) { rd[i] = od_ec_decode_cdf_q15(&ec_dec, ec_icdf13, 13); if (rd[i] != syms[i]) bad = 1; }
        printf("ec_roundtrip_cdf13 %d", rd[0]);
        for (int i = 1; i < 10; ++i) printf(" %d", rd[i]);
        printf("\n");
        if (bad) { fprintf(stderr, "EC0 roundtrip cdf13 FAILED\n"); return 1; }
        // tell/tell_frac at the end of the cdf run (encoder side)
        printf("ec_tell %d %u\n", svt_od_ec_enc_tell(&ec_enc), svt_od_ec_enc_tell_frac(&ec_enc));
    }
    {
        // 4) decoder tell at the end of the cdf13 decode (EC2; entdec.c:231-237)
        od_ec_dec_init(&ec_dec, ec_buf, 5);
        for (int i = 0; i < 10; ++i) od_ec_decode_cdf_q15(&ec_dec, ec_icdf13, 13);
        printf("ec_dec_tell %d\n", od_ec_dec_tell(&ec_dec));
    }
    {
        // 5) CDF adaptation (EC2; update_cdf, cabac_context_model.h:76-105).
        // Binary CDF {16384, 0} + counter: 40 val=0 updates, per-step icdf[0]
        // snapshot - walks the counter through 0..39 so the rate transitions
        // (4 + (count>>4) + (nsymbs>3)) at count 16 and 32 are all covered.
        static uint16_t cdf2[3] = {16384, 0, 0};
        printf("ecupd_cdf2_seq");
        for (int i = 0; i < 40; ++i) {
            update_cdf(cdf2, 0, 2);
            printf(" %u", cdf2[0]);
        }
        printf("\n");
        // 13-symbol CDF (12 icdf values + terminator 0 + counter 0): the EC0
        // symbol sequence, full 14-word dump afterwards (counter = 10).
        static uint16_t cdf13[14] = {26700, 22000, 18000, 14000, 10500, 8000, 6000, 4500, 3200, 2200, 1400, 700, 0, 0};
        const int syms[10] = {0, 5, 12, 3, 5, 5, 1, 0, 7, 9};
        for (int i = 0; i < 10; ++i) update_cdf(cdf13, syms[i], 13);
        printf("ecupd_cdf13_full");
        for (int i = 0; i < 14; ++i) printf(" %u", cdf13[i]);
        printf("\n");
    }
    {
        // 6) writer wrapper with adaptation (aom_write_symbol,
        // bitstream_unit.h:265-279): binary CDF = AOM_CDF2(28672) expansion
        // {4096, 0} + counter, 8 symbols, allow_update_cdf = 1.
        AomWriter w;
        w.ec.buf = ec_buf;
        svt_od_ec_enc_reset(&w.ec);
        w.allow_update_cdf = 1;
        w.pos              = 0;
        static uint16_t cdf2w[3] = {4096, 0, 0};
        const int syms2[8] = {1, 0, 1, 1, 0, 1, 0, 0};
        for (int i = 0; i < 8; ++i) aom_write_symbol(&w, syms2[i], cdf2w, 2);
        aom_stop_encode(&w);
        printf("ecsym_cdf2 %u", w.pos);
        for (uint32_t i = 0; i < w.pos; ++i) printf(" %02x", ec_buf[i]);
        printf("\n");
        printf("ecsym_cdf2_cdf %u %u %u\n", cdf2w[0], cdf2w[1], cdf2w[2]);
        // reader side (aom_reader_init bitreader.c:14-22 + aom_read_symbol_
        // bitreader.h:92-98 with adaptation): same start CDF, symbols must
        // round-trip and the adapted CDFs must end identical.
        aom_reader r;
        if (aom_reader_init(&r, ec_buf, w.pos)) { fprintf(stderr, "EC2 reader init FAILED\n"); return 1; }
        r.allow_update_cdf = 1;
        static uint16_t cdf2r[3] = {4096, 0, 0};
        printf("ecsym_cdf2_rt");
        for (int i = 0; i < 8; ++i) printf(" %d", aom_read_symbol_(&r, cdf2r, 2));
        printf("\n");
        printf("ecsym_cdf2_cdf_eq %d\n", memcmp(cdf2w, cdf2r, sizeof(cdf2w)) == 0);
        // 7) 13-symbol variant through both wrappers with adaptation
        w.ec.buf = ec_buf;
        svt_od_ec_enc_reset(&w.ec);
        w.allow_update_cdf = 1;
        w.pos              = 0;
        static uint16_t cdf13w[14] = {26700, 22000, 18000, 14000, 10500, 8000, 6000, 4500, 3200, 2200, 1400, 700, 0, 0};
        const int syms13[10] = {0, 5, 12, 3, 5, 5, 1, 0, 7, 9};
        for (int i = 0; i < 10; ++i) aom_write_symbol(&w, syms13[i], cdf13w, 13);
        aom_stop_encode(&w);
        printf("ecsym_cdf13 %u", w.pos);
        for (uint32_t i = 0; i < w.pos; ++i) printf(" %02x", ec_buf[i]);
        printf("\n");
        printf("ecsym_cdf13_cdf");
        for (int i = 0; i < 14; ++i) printf(" %u", cdf13w[i]);
        printf("\n");
        if (aom_reader_init(&r, ec_buf, w.pos)) { fprintf(stderr, "EC2 reader init FAILED\n"); return 1; }
        r.allow_update_cdf = 1;
        static uint16_t cdf13r[14] = {26700, 22000, 18000, 14000, 10500, 8000, 6000, 4500, 3200, 2200, 1400, 700, 0, 0};
        printf("ecsym_cdf13_rt");
        for (int i = 0; i < 10; ++i) printf(" %d", aom_read_symbol_(&r, cdf13r, 13));
        printf("\n");
        printf("ecsym_cdf13_cdf_eq %d\n", memcmp(cdf13w, cdf13r, sizeof(cdf13w)) == 0);
    }
    return 0;
}
