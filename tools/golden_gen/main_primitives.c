// tools/golden_gen/main_primitives.c
// Dumps every per-primitive golden. Run and diff against
// tools/golden_gen/expected_primitives.txt (validation gate).
#include "composition.c"

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
    return 0;
}
