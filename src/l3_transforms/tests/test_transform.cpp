#include <doctest.h>
#include <cmath>
#include <gpurt.h>
#include <transform.h>

TEST_CASE("cospi cos_bit=13 index 16 is 7568") {
    // golden: svt_aom_eb_av1_cospi_arr_data[13 - 10][16]
    CHECK(transforms::cospi13(16) == 7568);
}

TEST_CASE("cospi cos_bit=13 index 32 is 5793") {
    // golden: svt_aom_eb_av1_cospi_arr_data[13 - 10][32]
    CHECK(transforms::cospi13(32) == 5793);
}

TEST_CASE("cospi cos_bit=13 index 48 is 3135") {
    // golden: svt_aom_eb_av1_cospi_arr_data[13 - 10][48]
    CHECK(transforms::cospi13(48) == 3135);
}

TEST_CASE("sinpi cos_bit=13 index 4 is 7606") {
    // golden: svt_aom_eb_av1_sinpi_arr_data[13 - 10][4]
    CHECK(transforms::sinpi13(4) == 7606);
}

TEST_CASE("half_btf rounds 5793*8+5793*8 at bit 13 to 11") {
    CHECK(transforms::halfBtf(5793, 8, 5793, 8, 13) == 11);
}

TEST_CASE("round_shift rounds 96784 by 13 bits to 12") {
    CHECK(transforms::roundShift(96784, 13) == 12);
}

TEST_CASE("fdct4 matches svt_av1_fdct4_new golden") {
    // golden: svt_av1_fdct4_new @ cos_bit=13, input {5, 3, 7, 1}
    const std::int32_t in[4] = {5, 3, 7, 1};
    const std::int32_t out[4] = {11, 2, -3, 5};
    std::int32_t got[4] = {0};
    transforms::fdct4(in, got);
    CHECK(got[0] == out[0]);
}

TEST_CASE("fdct4 golden output 1 is 2") {
    const std::int32_t in[4] = {5, 3, 7, 1};
    std::int32_t got[4] = {0};
    transforms::fdct4(in, got);
    CHECK(got[1] == 2);
}

TEST_CASE("fdct4 golden output 2 is -3") {
    const std::int32_t in[4] = {5, 3, 7, 1};
    std::int32_t got[4] = {0};
    transforms::fdct4(in, got);
    CHECK(got[2] == -3);
}

TEST_CASE("fdct4 golden output 3 is 5") {
    const std::int32_t in[4] = {5, 3, 7, 1};
    std::int32_t got[4] = {0};
    transforms::fdct4(in, got);
    CHECK(got[3] == 5);
}

TEST_CASE("fadst4 golden output 0 is 10") {
    // golden: svt_av1_fadst4_new @ cos_bit=13, input {5, 3, 7, 1}
    const std::int32_t in[4] = {5, 3, 7, 1};
    std::int32_t got[4] = {0};
    transforms::fadst4(in, got);
    CHECK(got[0] == 10);
}

TEST_CASE("fadst4 golden output 1 is 6") {
    const std::int32_t in[4] = {5, 3, 7, 1};
    std::int32_t got[4] = {0};
    transforms::fadst4(in, got);
    CHECK(got[1] == 6);
}

TEST_CASE("fadst4 golden output 2 is -1") {
    const std::int32_t in[4] = {5, 3, 7, 1};
    std::int32_t got[4] = {0};
    transforms::fadst4(in, got);
    CHECK(got[2] == -1);
}

TEST_CASE("fadst4 golden output 3 is 6") {
    const std::int32_t in[4] = {5, 3, 7, 1};
    std::int32_t got[4] = {0};
    transforms::fadst4(in, got);
    CHECK(got[3] == 6);
}

TEST_CASE("idct4 matches svt_av1_idct4_new golden") {
    // golden: svt_av1_idct4_new @ cos_bit=12, input {100, 50, -20, 8}
    const std::int32_t in[4] = {100, 50, -20, 8};
    const std::int32_t golden[4] = {106, 97, 73, 8};
    std::int32_t got[4] = {0};
    transforms::idct4(in, got);
    CHECK(got[0] == golden[0]);
}

TEST_CASE("idct4 golden output 1 is 97") {
    const std::int32_t in[4] = {100, 50, -20, 8};
    std::int32_t got[4] = {0};
    transforms::idct4(in, got);
    CHECK(got[1] == 97);
}

TEST_CASE("idct4 golden output 2 is 73") {
    const std::int32_t in[4] = {100, 50, -20, 8};
    std::int32_t got[4] = {0};
    transforms::idct4(in, got);
    CHECK(got[2] == 73);
}

TEST_CASE("idct4 golden output 3 is 8") {
    const std::int32_t in[4] = {100, 50, -20, 8};
    std::int32_t got[4] = {0};
    transforms::idct4(in, got);
    CHECK(got[3] == 8);
}

TEST_CASE("fdct8 matches svt_av1_fdct8_new golden full vector") {
    // golden: svt_av1_fdct8_new @ cos_bit=13, input {200,80,-50,30,100,-20,60,10}
    // (gate line fdct8)
    const std::int32_t in[8] = {200, 80, -50, 30, 100, -20, 60, 10};
    const std::int32_t golden[8] = {290, 173, 154, 222, 191, 22, -163, 70};
    std::int32_t got[8] = {0};
    transforms::fdct8(in, got);
    bool ok = true;
    for (int i = 0; i < 8; ++i) {
        if (got[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("fadst8 matches svt_av1_fadst8_new golden full vector") {
    // golden: svt_av1_fadst8_new @ cos_bit=13, same input (gate line fadst8)
    const std::int32_t in[8] = {200, 80, -50, 30, 100, -20, 60, 10};
    const std::int32_t golden[8] = {165, 97, 67, 173, 296, 275, -2, 146};
    std::int32_t got[8] = {0};
    transforms::fadst8(in, got);
    bool ok = true;
    for (int i = 0; i < 8; ++i) {
        if (got[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("fdct16 matches svt_av1_fdct16_new golden full vector") {
    // golden: svt_av1_fdct16_new @ cos_bit=13, input {200,80,-50,30,100,-20,60,10,
    // -35,95,5,-70,45,25,-15,55} (gate line fdct16)
    const std::int32_t in[16] = {200, 80, -50, 30, 100, -20, 60, 10,
                                 -35, 95, 5, -70, 45, 25, -15, 55};
    const std::int32_t golden[16] = {364, 248, 203, 88, 215, 130, 235, 303,
                                     110, 41, 280, -304, -192, 51, 53, 36};
    std::int32_t got[16] = {0};
    transforms::fdct16(in, got);
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("fadst16 matches svt_av1_fadst16_new golden full vector") {
    // golden: svt_av1_fadst16_new @ cos_bit=13, same input (gate line fadst16)
    const std::int32_t in[16] = {200, 80, -50, 30, 100, -20, 60, 10,
                                 -35, 95, 5, -70, 45, 25, -15, 55};
    const std::int32_t golden[16] = {192, 142, 163, 22, 125, 39, 107, 307,
                                     254, 126, 576, 124, -57, 72, 119, 148};
    std::int32_t got[16] = {0};
    transforms::fadst16(in, got);
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("idct16 matches svt_av1_idct16_new golden full vector") {
    // golden: svt_av1_idct16_new @ cos_bit=12 (INV_COS_BIT), input
    // {300,-120,75,200,-60,40,90,-15,55,-95,20,65,-40,85,-25,10}
    // (gate line idct16); stage_range = opt_range 16 all stages
    // (gen_inv_range_16x16_dct); idct16 clamps stages 3-7 only
    const std::int32_t in[16] = {300, -120, 75, 200, -60, 40, 90, -15,
                                 55, -95, 20, 65, -40, 85, -25, 10};
    const std::int32_t golden[16] = {427, 201, 152, -56, 32, 128, -70, -76,
                                     126, 366, 316, 600, 712, 40, 251, 243};
    std::int32_t got[16] = {0};
    transforms::idct16(in, got);
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("iadst16 matches svt_av1_iadst16_new golden full vector") {
    // golden: svt_av1_iadst16_new @ cos_bit=12, same input (gate line iadst16);
    // iadst16 clamps stages 3/5/7 only; NO all-zero early-out at 16 (unlike
    // iadst4, inv_transforms.c:927-1130)
    const std::int32_t in[16] = {300, -120, 75, 200, -60, 40, 90, -15,
                                 55, -95, 20, 65, -40, 85, -25, 10};
    const std::int32_t golden[16] = {193, 225, 250, 126, -43, 130, 7, -180,
                                     -116, 178, 229, 480, 851, 188, 319, 335};
    std::int32_t got[16] = {0};
    transforms::iadst16(in, got);
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("invTxfm2dAdd16x16 matches svt golden 256 samples both TxTypes") {
    // golden: svtd_inv2dadd16x16 (inv_txfm2d_add_c @ TX_16X16): inv_shift_16x16
    // = {-2,-4} (inv_transforms.c:20), cos_bit 12/12 = inv_cos_bit_col/row[2][2]
    // (inv_transforms.h:32-41), clamps bd+8=16 / max(bd+6,16)=16, row >>2,
    // col >>4, clip add (gate lines inv2d16_dct/adst_onto_vpred)
    // coeff fixtures: cdct16[i] = ((r*31+c*17+45)%97)-48,
    // cadst16[i] = ((r*23+c*41+13)%89)-44; pred[r][c] = 10+5*c (V-pred)
    std::int32_t cdct16[256];
    std::int32_t cadst16[256];
    for (int i = 0; i < 256; ++i) {
        const int r = i / 16, c = i % 16;
        cdct16[i] = ((r * 31 + c * 17 + 45) % 97) - 48;
        cadst16[i] = ((r * 23 + c * 41 + 13) % 89) - 44;
    }
    const std::uint8_t goldenDct[256] = {
        11, 16, 20, 24, 29, 37, 39, 45, 50, 55, 59, 68, 70, 76, 78, 77,
        12, 18, 19, 24, 28, 36, 39, 45, 50, 55, 58, 66, 68, 76, 70, 89,
        10, 20, 22, 24, 32, 36, 41, 44, 50, 54, 63, 67, 72, 75, 81, 89,
        8, 14, 23, 25, 31, 35, 40, 47, 50, 54, 60, 65, 72, 74, 77, 84,
        11, 15, 20, 24, 29, 35, 40, 46, 48, 55, 61, 66, 68, 72, 81, 87,
        10, 14, 23, 28, 30, 38, 38, 46, 49, 55, 59, 67, 70, 76, 80, 84,
        11, 15, 19, 25, 29, 36, 43, 45, 54, 56, 62, 68, 67, 74, 81, 86,
        11, 15, 22, 26, 31, 39, 37, 39, 50, 52, 59, 65, 69, 75, 79, 84,
        10, 16, 20, 25, 29, 36, 41, 45, 57, 57, 59, 67, 69, 75, 79, 85,
        13, 18, 23, 27, 37, 58, 32, 45, 47, 63, 60, 64, 71, 75, 79, 86,
        11, 18, 26, 24, 45, 21, 19, 45, 45, 54, 63, 71, 66, 75, 77, 83,
        10, 15, 18, 25, 24, 22, 41, 43, 49, 56, 53, 77, 76, 77, 81, 88,
        10, 14, 21, 20, 27, 33, 34, 46, 48, 57, 52, 60, 72, 74, 81, 84,
        10, 12, 18, 22, 33, 31, 41, 44, 50, 57, 61, 66, 73, 73, 80, 84,
        12, 16, 20, 24, 31, 31, 39, 46, 50, 56, 59, 62, 72, 78, 82, 85,
        10, 14, 16, 25, 31, 31, 39, 46, 48, 56, 60, 64, 69, 77, 79, 84};
    const std::uint8_t goldenAdst[256] = {
        11, 15, 22, 23, 35, 39, 40, 44, 49, 57, 60, 64, 69, 74, 82, 87,
        10, 14, 19, 25, 27, 40, 41, 46, 50, 55, 61, 68, 71, 75, 81, 85,
        11, 15, 20, 25, 27, 34, 42, 43, 52, 53, 58, 65, 68, 75, 81, 85,
        11, 15, 20, 26, 28, 36, 40, 48, 51, 57, 58, 66, 72, 77, 82, 82,
        9, 18, 21, 22, 29, 34, 42, 44, 50, 53, 60, 64, 68, 73, 82, 86,
        10, 15, 20, 26, 29, 36, 38, 48, 52, 57, 60, 65, 70, 78, 80, 81,
        10, 14, 22, 24, 30, 35, 38, 43, 53, 53, 63, 61, 65, 76, 84, 89,
        8, 17, 18, 26, 29, 38, 38, 47, 49, 59, 58, 73, 61, 81, 79, 66,
        8, 17, 21, 26, 27, 36, 39, 47, 49, 53, 58, 70, 69, 79, 52, 84,
        12, 15, 20, 25, 30, 37, 40, 45, 50, 56, 65, 64, 72, 70, 80, 96,
        8, 15, 21, 26, 31, 32, 39, 44, 50, 58, 58, 66, 69, 72, 76, 86,
        11, 15, 18, 27, 29, 35, 40, 44, 51, 58, 61, 65, 71, 71, 81, 90,
        11, 17, 23, 25, 28, 34, 41, 45, 51, 56, 60, 66, 68, 73, 77, 87,
        11, 15, 18, 26, 28, 34, 39, 45, 53, 56, 60, 63, 70, 76, 79, 87,
        9, 19, 28, 22, 30, 33, 41, 49, 49, 56, 60, 64, 71, 73, 79, 87,
        9, 20, 5, 20, 30, 33, 40, 44, 48, 55, 60, 63, 69, 73, 77, 87};
    std::uint8_t pred[256];

    for (int i = 0; i < 256; ++i) pred[i] = static_cast<std::uint8_t>(10 + 5 * (i % 16));
    transforms::invTxfm2dAdd16x16(cdct16, pred, 16, transforms::TxType::DCT_DCT);
    bool okDct = true;
    for (int i = 0; i < 256; ++i) {
        if (pred[i] != goldenDct[i]) okDct = false;
    }
    CHECK(okDct);

    for (int i = 0; i < 256; ++i) pred[i] = static_cast<std::uint8_t>(10 + 5 * (i % 16));
    transforms::invTxfm2dAdd16x16(cadst16, pred, 16, transforms::TxType::ADST_ADST);
    bool okAdst = true;
    for (int i = 0; i < 256; ++i) {
        if (pred[i] != goldenAdst[i]) okAdst = false;
    }
    CHECK(okAdst);
}

TEST_CASE("idct32 matches svt_av1_idct32_new golden full vector") {
    // golden: svt_av1_idct32_new @ cos_bit=12 (INV_COS_BIT), stage_range 16x10
    // (gen_inv_range_32x32_dct), input {300,-120,75,200,...,-55} (gate line
    // idct32); clamp audit (L3): idct32 clamps ONLY stages 3-9 (clamp_value on
    // the butterfly adds; stages 1-2 range checks commented out in SVT)
    const std::int32_t in[32] = {300, -120, 75, 200, -60, 40, 90, -15, 55, -95, 20, 65, -40, 85, -25, 10,
                                 30, -70, 95, -35, 60, -15, 80, 25, -50, 45, -20, 70, -90, 15, 50, -55};
    const std::int32_t golden[32] = {559, 209, 287, 268, 58, 151, 26, -124, -26, 129, 115, 74, 16, -169, 15, -133,
                                     -1, 439, 83, 488, 266, 493, 273, 826, 688, 804, -35, -190, 628, 67, 169, 331};
    std::int32_t got[32] = {0};
    transforms::idct32(in, got);
    bool ok = true;
    for (int i = 0; i < 32; ++i) {
        if (got[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("iadst32 matches svt_av1_iadst32_new golden full vector") {
    // golden: static av1_iadst32_new (inv_transforms.c:1132) @ cos_bit=12, same
    // input (gate line iadst32); clamp audit (L3): iadst32 clamps EVERY stage
    // (clamp_buf on the full 32-vector after each of stages 0-11); NO all-zero
    // early-out at 32
    const std::int32_t in[32] = {300, -120, 75, 200, -60, 40, 90, -15, 55, -95, 20, 65, -40, 85, -25, 10,
                                 30, -70, 95, -35, 60, -15, 80, 25, -50, 45, -20, 70, -90, 15, 50, -55};
    const std::int32_t golden[32] = {219, 264, 101, 349, 196, 209, 240, 64, -99, 35, 90, 115, 98, -91, -129, -113,
                                     -398, 247, -85, 285, 174, 389, 195, 696, 777, 946, 261, -185, 704, 178, 246, 417};
    std::int32_t got[32] = {0};
    transforms::iadst32(in, got);
    bool ok = true;
    for (int i = 0; i < 32; ++i) {
        if (got[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("fdct32 matches svt_av1_fdct32_new golden full vector") {
    // golden: svt_av1_fdct32_new @ cos_bit=12 (fwd_cos_bit_col/row[3][3]),
    // input {200,80,-50,30,100,-20,60,10,-35,95,5,-70,45,25,-15,55,
    // 65,-85,15,-5,75,-60,40,90,-25,35,50,-45,20,-10,70,-55} (gate line fdct32)
    const std::int32_t in[32] = {200, 80, -50, 30, 100, -20, 60, 10, -35, 95, 5, -70, 45, 25, -15, 55,
                                 65, -85, 15, -5, 75, -60, 40, 90, -25, 35, 50, -45, 20, -10, 70, -30};
    const std::int32_t golden[32] = {506, 277, 261, 268, 104, 214, 115, 26, 291, 218, 103, 114, 222, 443, 171, 203,
                                     173, 159, -308, 442, 342, -132, -210, -417, -231, 222, -297, 233, 124, -16, -126, 253};
    std::int32_t got[32] = {0};
    transforms::fdct32(in, got);
    bool ok = true;
    for (int i = 0; i < 32; ++i) {
        if (got[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("fadst32 matches svt_av1_fadst32_new golden full vector") {
    // golden: static av1_fadst32_new (transforms.c:1908) @ cos_bit=12, same
    // input (gate line fadst32)
    const std::int32_t in[32] = {200, 80, -50, 30, 100, -20, 60, 10, -35, 95, 5, -70, 45, 25, -15, 55,
                                 65, -85, 15, -5, 75, -60, 40, 90, -25, 35, 50, -45, 20, -10, 70, -30};
    const std::int32_t golden[32] = {295, 149, 138, 223, 71, 169, 129, -84, 119, 173, 78, -6, -19, 322, 209, 255,
                                     287, 465, -238, 302, 646, 422, 352, -19, -213, 305, -244, 125, 218, 172, -94, 297};
    std::int32_t got[32] = {0};
    transforms::fadst32(in, got);
    bool ok = true;
    for (int i = 0; i < 32; ++i) {
        if (got[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("fwdTxfm2d16x16 dct matches svt golden full 256") {
    // golden: svtd_fwd2d16x16 (av1_tranform_two_d_core_c @ TX_16X16, DCT_DCT;
    // fwd_shift_16x16 = {2,-2,0} transforms.c:124, cos_bit col 13 / row 12 =
    // fwd_cos_bit_col/row[2][2] transforms.c:19-22)
    // gate line fwd2d16_dct; fixture (c*13+r*7+((c*r)&31))%211-105, stride 16
    std::int16_t in[256];
    for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 16; ++c)
            in[r * 16 + c] = static_cast<std::int16_t>((c * 13 + r * 7 + ((c * r) & 31)) % 211) - 105;
    const std::int32_t golden[256] = {
        7, 301, -3728, -590, -257, 180, -467, -473, 83, 115, -266, -134, 266, -192, 211, -112,
        -322, -3655, -642, 2503, -1360, 37, 188, 555, -267, -124, 311, -23, 7, -246, 97, -110,
        -185, -780, 1809, -1149, -331, 1234, -690, 66, -76, 97, 30, -298, -6, 349, -109, 101,
        -240, -400, 306, -535, 1002, -446, -440, 501, -83, -64, -261, 480, -43, -129, -231, 241,
        53, -302, 498, -168, 51, -463, 588, -277, -308, 555, -87, -4, -385, 339, 55, -256, 71,
        -327, 219, -248, 350, -235, 12, 111, 203, -219, -311, 296, 16, -44, -59, 207, 59,
        -174, 394, -330, -9, -65, 371, -245, -103, 139, -24, 71, -91, 68, -58, -14, -172,
        2, 215, -370, 367, -108, 44, -280, 206, 45, -75, 145, -113, -65, 113, -102, -128,
        118, -47, -163, 439, -411, 195, 19, 1, -181, 217, -179, -42, 258, -148, 26, 88,
        -140, 18, 162, -156, -128, 296, -334, 311, -163, 168, -221, 89, 88, -271, 298, 243,
        -218, 83, -25, -142, 277, -175, -170, 474, -390, 49, 73, 115, -191, 86, -46, -35,
        -85, 102, -78, 195, -199, 5, 377, -423, -117, 482, -382, 161, -190, 522, -557, -89,
        129, -39, -135, 249, -195, 43, 27, -191, 356, -100, -413, 811, -683, -37, 435, -91,
        29, 122, -26, -219, 103, 253, -408, 284, -51, -71, 94, -42, -234, 532, -442, 137,
        -41, -257, 214, 111, -125, -44, -95, 536, -686, 231, 379, -733, 555, 12, -247, -201,
        108, 184, -147, -95, 17, 330, -439, 164, -21, 266, -514, 355, 439, -1171, 913};
    std::int32_t got[256] = {0};
    transforms::fwdTxfm2d16x16(in, got, 16, transforms::TxType::DCT_DCT);
    bool ok = true;
    for (int i = 0; i < 256; ++i) {
        if (got[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("fwdTxfm2d8x8 dct matches svt golden full 64") {
    // golden: svtd_fwd2d8x8 (av1_tranform_two_d_core_c @ TX_8X8, DCT_DCT)
    // gate line fwd2d8_dct, input = the 8x8 discriminating fixture
    const std::int16_t in[64] = {12, 45, 3, 78, 22, 91, 6, 30,
                                 67, 8, 54, 11, 39, 71, 17, 48,
                                 2, 90, 25, 63, 7, 44, 85, 19,
                                 51, 36, 9, 77, 28, 5, 60, 83,
                                 15, 72, 41, 4, 88, 33, 26, 58,
                                 80, 13, 66, 47, 1, 95, 38, 70,
                                 24, 56, 10, 82, 31, 68, 14, 42,
                                 75, 29, 87, 20, 53, 16, 79, 34};
    const std::int32_t golden[64] = {
        2755, -122, 52, 15, -31, 19, 89, -343,
        -216, -108, -129, 86, -53, -96, -70, -368,
        -62, 134, -211, 207, -86, 148, 286, -61,
        -26, -77, -164, 189, 121, -270, 508, -375,
        -21, -11, -11, -109, 211, -162, -279, 318,
        -71, -100, -162, -140, 163, 183, -400, -764,
        132, 11, 68, -59, -361, 319, -60, -84,
        -123, 35, -341, -14, -332, -464, -499, -130};
    std::int32_t out[64] = {0};
    transforms::fwdTxfm2d8x8(in, out, 8, transforms::TxType::DCT_DCT);
    bool ok = true;
    for (int i = 0; i < 64; ++i) {
        if (out[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("fwdTxfm2d8x8 adst matches svt golden full 64") {
    // golden: svtd_fwd2d8x8 (av1_tranform_two_d_core_c @ TX_8X8, ADST_ADST)
    // gate line fwd2d8_adst, same fixture
    const std::int16_t in[64] = {12, 45, 3, 78, 22, 91, 6, 30,
                                 67, 8, 54, 11, 39, 71, 17, 48,
                                 2, 90, 25, 63, 7, 44, 85, 19,
                                 51, 36, 9, 77, 28, 5, 60, 83,
                                 15, 72, 41, 4, 88, 33, 26, 58,
                                 80, 13, 66, 47, 1, 95, 38, 70,
                                 24, 56, 10, 82, 31, 68, 14, 42,
                                 75, 29, 87, 20, 53, 16, 79, 34};
    const std::int32_t golden[64] = {
        2350, 666, 575, 342, 258, 270, 382, 158,
        718, 64, 175, 97, 137, 113, -5, -319,
        348, 230, 9, 194, -184, 188, 85, 75,
        306, 116, -236, 224, 23, -358, 737, -151,
        232, 82, -32, 64, 264, -325, -56, 483,
        159, 45, -188, -273, 338, 158, 217, -498,
        327, -8, 104, -20, -231, 307, 437, -73,
        161, 276, -145, 181, -40, -52, -351, -681};
    std::int32_t out[64] = {0};
    transforms::fwdTxfm2d8x8(in, out, 8, transforms::TxType::ADST_ADST);
    bool ok = true;
    for (int i = 0; i < 64; ++i) {
        if (out[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("idct8 matches svt_av1_idct8_new golden full vector") {
    // golden: svt_av1_idct8_new @ cos_bit=12, input {300,-120,75,200,-60,40,90,-15}
    // (gate line idct8)
    const std::int32_t in[8] = {300, -120, 75, 200, -60, 40, 90, -15};
    const std::int32_t golden[8] = {342, 31, 41, -21, 153, 577, 371, 206};
    std::int32_t got[8] = {0};
    transforms::idct8(in, got);
    bool ok = true;
    for (int i = 0; i < 8; ++i) {
        if (got[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("iadst8 matches svt_av1_iadst8_new golden full vector") {
    // golden: svt_av1_iadst8_new @ cos_bit=12, same input (gate line iadst8)
    const std::int32_t in[8] = {300, -120, 75, 200, -60, 40, 90, -15};
    const std::int32_t golden[8] = {216, 171, 33, -37, -77, 498, 484, 295};
    std::int32_t got[8] = {0};
    transforms::iadst8(in, got);
    bool ok = true;
    for (int i = 0; i < 8; ++i) {
        if (got[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("invTxfm2dAdd8x8 dct matches svt golden full 64") {
    // golden: svtd_inv2dadd8x8 (inv_txfm2d_add_c @ TX_8X8, DCT_DCT)
    // gate line inv2d8_dct_onto_vpred; pred = V-ramp (10+5c per row)
    const std::int32_t coeffs[64] = {
        520, -34, 78, -11, 92, 5, -63, 28,
        -17, 45, -8, 60, -29, 71, 14, -52,
        33, -76, 19, 41, -55, 23, 87, -9,
        62, 12, -48, 70, -16, 38, -83, 25,
        -44, 58, 8, -92, 31, 67, -21, 49,
        15, -39, 74, -6, 84, -27, 51, -13,
        66, 22, -57, 35, -78, 10, 43, -31,
        -25, 80, -18, 56, 7, -61, 29, -71};
    const std::uint8_t golden[64] = {
        27, 20, 27, 36, 36, 47, 54, 51,
        17, 20, 22, 31, 36, 42, 55, 52,
        22, 27, 24, 32, 35, 51, 39, 54,
        20, 18, 25, 34, 36, 22, 49, 65,
        20, 29, 24, 38, 42, 28, 45, 45,
        18, 27, 27, 36, 48, 45, 60, 53,
        21, 22, 18, 27, 44, 41, 45, 69,
        14, 19, 34, 30, 36, 39, 50, 62};
    std::uint8_t dst[64];
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) dst[r*8+c] = (std::uint8_t)(10 + 5*c);
    transforms::invTxfm2dAdd8x8(coeffs, dst, 8, transforms::TxType::DCT_DCT);
    bool ok = true;
    for (int i = 0; i < 64; ++i) {
        if (dst[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("invTxfm2dAdd8x8 adst matches svt golden full 64") {
    // golden: svtd_inv2dadd8x8 (inv_txfm2d_add_c @ TX_8X8, ADST_ADST)
    // gate line inv2d8_adst_onto_vpred; same pred
    const std::int32_t coeffs[64] = {
        -45, 67, -12, 89, 23, -58, 41, -30,
        71, -24, 56, -83, 15, 49, -37, 62,
        -9, 38, -71, 27, 64, -45, 18, -77,
        55, -61, 30, -14, 76, -22, 47, -88,
        20, 41, -66, 12, -53, 78, -35, 59,
        -72, 16, 44, -27, 61, -9, 33, -50,
        37, -55, 69, -18, 42, -64, 25, -46,
        -14, 58, -32, 74, -20, 51, -79, 11};
    const std::uint8_t golden[64] = {
        11, 23, 15, 28, 32, 35, 38, 47,
        11, 14, 16, 31, 34, 39, 36, 52,
        10, 20, 22, 23, 35, 36, 42, 45,
        13, 14, 25, 24, 26, 28, 34, 51,
        16, 11, 28, 26, 35, 30, 54, 29,
        10, 19, 19, 21, 24, 42, 36, 60,
        10, 25, 13, 22, 25, 41, 23, 39,
        9, 18, 21, 21, 33, 47, 33, 28};
    std::uint8_t dst[64];
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) dst[r*8+c] = (std::uint8_t)(10 + 5*c);
    transforms::invTxfm2dAdd8x8(coeffs, dst, 8, transforms::TxType::ADST_ADST);
    bool ok = true;
    for (int i = 0; i < 64; ++i) {
        if (dst[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("iadst4 matches svt_av1_iadst4_new golden") {
    // golden: svt_av1_iadst4_new @ cos_bit=12, input {100, 50, -20, 8}
    const std::int32_t in[4] = {100, 50, -20, 8};
    const std::int32_t golden[4] = {59, 100, 105, 37};
    std::int32_t got[4] = {0};
    transforms::iadst4(in, got);
    bool ok = true;
    for (int i = 0; i < 4; ++i) {
        if (got[i] != golden[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("inv 2d add dct reconstructs the source block") {
    // golden: inv_txfm2d_add_c (DCT_DCT) onto the V pred {10,40,30,20} x4 with
    // the E1 DCT_DCT coeffs of src {21,3,5,9,...} -> the source is recovered
    // exactly (fwd+inv round trip is bit-exact for this block)
    const std::int32_t coeffs[16] = {-520, 140, 324, 202, -17, 18, 102, -68,
                                     56,   3,   36,  120, -18, 23, 6,   -22};
    const std::int32_t golden[16] = {21, 3, 5, 9, 9, 11, 3, 7, 7, 13, 5, 1, 15, 4, 25, 2};
    std::uint8_t dst[16];
    const std::uint8_t row[4] = {10, 40, 30, 20};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            dst[r * 4 + c] = row[c];
        }
    }
    transforms::invTxfm2dAdd4x4(coeffs, dst, 4, transforms::TxType::DCT_DCT);
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (dst[i] != golden[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("inv 2d add adst matches svt golden") {
    // golden: inv_txfm2d_add_c (ADST_ADST inverse) onto the V pred
    // {10,40,30,20} x4 with an E1 coefficient set {DC-mode DCT_DCT coeffs of
    // src {9,4,7,5,...}} pushed through the ADST inverse (harness-computed
    // for exactly this input; NOT a forward-ADST coefficient set)
    const std::int32_t coeffs[16] = {-631, 53, 4,  55,  -16, 2,  45, -26,
                                     -12,  -27, -47, -2,  2,   -2, 16, 53};
    const std::int32_t golden[16] = {6, 29, 20, 7, 5, 29, 6, 0, 10, 18, 6, 0, 0, 18, 7, 0};
    std::uint8_t dst[16];
    const std::uint8_t row[4] = {10, 40, 30, 20};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            dst[r * 4 + c] = row[c];
        }
    }
    transforms::invTxfm2dAdd4x4(coeffs, dst, 4, transforms::TxType::ADST_ADST);
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (dst[i] != golden[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu inverse 2d add dct matches host bit-exactly") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const std::int32_t coeffs[16] = {-520, 140, 324, 202, -17, 18, 102, -68,
                                     56,   3,   36,  120, -18, 23, 6,   -22};
    std::uint8_t refDst[16];
    const std::uint8_t row[4] = {10, 40, 30, 20};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            refDst[r * 4 + c] = row[c];
        }
    }
    transforms::invTxfm2dAdd4x4(coeffs, refDst, 4, transforms::TxType::DCT_DCT);

    const std::string ptx = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> names = gpurt::ptxEntryNames(ptx);
    const auto it = std::find(names.begin(), names.end(), "inv_txfm_2d_add_4x4");
    REQUIRE(it != names.end());
    gpurt::Kernel k(ptx, *it);

    std::uint8_t dst[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            dst[r * 4 + c] = row[c];
        }
    }
    gpurt::DeviceBuffer dCoeffs(sizeof(coeffs));
    gpurt::DeviceBuffer dDst(sizeof(dst));
    dCoeffs.uploadFrom(coeffs, sizeof(coeffs));
    dDst.uploadFrom(dst, sizeof(dst));

    int typeArg = 0;
    int strideArg = 4;
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    gpurt::DeviceBuffer dStride(sizeof(strideArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dStride.uploadFrom(&strideArg, sizeof(strideArg));

    CUdeviceptr pCoeffs = dCoeffs.get();
    CUdeviceptr pType = dType.get();
    CUdeviceptr pDst = dDst.get();
    CUdeviceptr pStride = dStride.get();
    void* args[] = {&pCoeffs, &pType, &pDst, &pStride};
    k.launch(1, 1, 4, 1, args);

    std::uint8_t got[16] = {0};
    dDst.downloadTo(got, sizeof(got));

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != refDst[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu inverse 2d add adst matches host bit-exactly") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const std::int32_t coeffs[16] = {-631, 53, 4,  55,  -16, 2,  45, -26,
                                     -12,  -27, -47, -2,  2,   -2, 16, 53};
    std::uint8_t refDst[16];
    const std::uint8_t row[4] = {10, 40, 30, 20};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            refDst[r * 4 + c] = row[c];
        }
    }
    transforms::invTxfm2dAdd4x4(coeffs, refDst, 4, transforms::TxType::ADST_ADST);

    const std::string ptx = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> names = gpurt::ptxEntryNames(ptx);
    const auto it = std::find(names.begin(), names.end(), "inv_txfm_2d_add_4x4");
    REQUIRE(it != names.end());
    gpurt::Kernel k(ptx, *it);

    std::uint8_t dst[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            dst[r * 4 + c] = row[c];
        }
    }
    gpurt::DeviceBuffer dCoeffs(sizeof(coeffs));
    gpurt::DeviceBuffer dDst(sizeof(dst));
    dCoeffs.uploadFrom(coeffs, sizeof(coeffs));
    dDst.uploadFrom(dst, sizeof(dst));

    int typeArg = 1;
    int strideArg = 4;
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    gpurt::DeviceBuffer dStride(sizeof(strideArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dStride.uploadFrom(&strideArg, sizeof(strideArg));

    CUdeviceptr pCoeffs = dCoeffs.get();
    CUdeviceptr pType = dType.get();
    CUdeviceptr pDst = dDst.get();
    CUdeviceptr pStride = dStride.get();
    void* args[] = {&pCoeffs, &pType, &pDst, &pStride};
    k.launch(1, 1, 4, 1, args);

    std::uint8_t got[16] = {0};
    dDst.downloadTo(got, sizeof(got));

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != refDst[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("fadst4 all-zero input early-outs to zeros") {
    const std::int32_t in[4] = {0, 0, 0, 0};
    std::int32_t got[4] = {42, 42, 42, 42};
    transforms::fadst4(in, got);
    CHECK(got[2] == 0);
}

TEST_CASE("fwdTxfm2d4x4 dct golden dc is 168") {
    // golden: svt_av1_transform_two_d_4x4_c (DCT_DCT, TX_4X4), input
    // {9,2,3,1,5,6,7,8,8,7,6,5,4,3,9,1}
    const std::int16_t in[16] = {9, 2, 3, 1, 5, 6, 7, 8, 8, 7, 6, 5, 4, 3, 9, 1};
    std::int32_t out[16] = {0};
    transforms::fwdTxfm2d4x4(in, out, 4, transforms::TxType::DCT_DCT);
    CHECK(out[0] == 168);
}

TEST_CASE("fwdTxfm2d4x4 dct golden full block") {
    // golden: svt_av1_transform_two_d_4x4_c (DCT_DCT, TX_4X4), input
    // {9,2,3,1,5,6,7,8,8,7,6,5,4,3,9,1}
    const std::int16_t in[16] = {9, 2, 3, 1, 5, 6, 7, 8, 8, 7, 6, 5, 4, 3, 9, 1};
    const std::int32_t golden[16] = {168, 22,  -4, 31,  -5, 14, 32, -11,
                                     -40, 21,  -4, 30,  -2, 33, 13, -2};
    std::int32_t out[16] = {0};
    transforms::fwdTxfm2d4x4(in, out, 4, transforms::TxType::DCT_DCT);
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (out[i] != golden[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("fwdTxfm2d4x4 dct of a constant-one block has dc 31") {
    // golden: svt_av1_transform_two_d_4x4_c (DCT_DCT), input all 1s -> dc 31
    // (verified against SVT's own C; the AV1 Q3 x8 orthonormal figure (32) does
    // not match the reference pipeline's rounding, so the golden value wins)
    const std::int16_t in[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    std::int32_t out[16] = {0};
    transforms::fwdTxfm2d4x4(in, out, 4, transforms::TxType::DCT_DCT);
    CHECK(out[0] == 31);
}

TEST_CASE("fwdTxfm2d4x4 adst golden full block") {
    // golden: svt_av1_transform_two_d_4x4_c (ADST_ADST, TX_4X4), input
    // {9,2,3,1,5,6,7,8,8,7,6,5,4,3,9,1}
    const std::int16_t in[16] = {9, 2, 3, 1, 5, 6, 7, 8, 8, 7, 6, 5, 4, 3, 9, 1};
    const std::int32_t golden[16] = {150, 61, 8,  39,  47, 19, 39, -1,
                                     -22, 13, -1, 32,  -11, 31, 23, 8};
    std::int32_t out[16] = {0};
    transforms::fwdTxfm2d4x4(in, out, 4, transforms::TxType::ADST_ADST);
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (out[i] != golden[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu fwd txfm 2d dct matches host bit-exactly") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const std::int16_t in[16] = {9, 2, 3, 1, 5, 6, 7, 8, 8, 7, 6, 5, 4, 3, 9, 1};
    std::int32_t ref[16] = {0};
    transforms::fwdTxfm2d4x4(in, ref, 4, transforms::TxType::DCT_DCT);

    const std::string ptx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> names = gpurt::ptxEntryNames(ptx);
    const auto it = std::find(names.begin(), names.end(), "fwd_txfm_2d_4x4");
    REQUIRE(it != names.end());
    gpurt::Kernel k(ptx, *it);

    gpurt::DeviceBuffer dIn(sizeof(in));
    gpurt::DeviceBuffer dOut(sizeof(ref));
    dIn.uploadFrom(in, sizeof(in));

    int strideArg = 4;
    int typeArg = 0;
    gpurt::DeviceBuffer dStride(sizeof(strideArg));
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    dStride.uploadFrom(&strideArg, sizeof(strideArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));

    CUdeviceptr pIn = dIn.get();
    CUdeviceptr pStride = dStride.get();
    CUdeviceptr pType = dType.get();
    CUdeviceptr pOut = dOut.get();
    void* args[] = {&pIn, &pStride, &pType, &pOut};
    k.launch(1, 1, 4, 1, args);

    std::int32_t got[16] = {0};
    dOut.downloadTo(got, sizeof(got));

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != ref[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu fwd txfm 2d adst matches host bit-exactly") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const std::int16_t in[16] = {9, 2, 3, 1, 5, 6, 7, 8, 8, 7, 6, 5, 4, 3, 9, 1};
    std::int32_t ref[16] = {0};
    transforms::fwdTxfm2d4x4(in, ref, 4, transforms::TxType::ADST_ADST);

    const std::string ptx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> names = gpurt::ptxEntryNames(ptx);
    const auto it = std::find(names.begin(), names.end(), "fwd_txfm_2d_4x4");
    REQUIRE(it != names.end());
    gpurt::Kernel k(ptx, *it);

    gpurt::DeviceBuffer dIn(sizeof(in));
    gpurt::DeviceBuffer dOut(sizeof(ref));
    dIn.uploadFrom(in, sizeof(in));

    int strideArg = 4;
    int typeArg = 1;
    gpurt::DeviceBuffer dStride(sizeof(strideArg));
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    dStride.uploadFrom(&strideArg, sizeof(strideArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));

    CUdeviceptr pIn = dIn.get();
    CUdeviceptr pStride = dStride.get();
    CUdeviceptr pType = dType.get();
    CUdeviceptr pOut = dOut.get();
    void* args[] = {&pIn, &pStride, &pType, &pOut};
    k.launch(1, 1, 4, 1, args);

    std::int32_t got[16] = {0};
    dOut.downloadTo(got, sizeof(got));

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != ref[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("quantizer tables match the Q0 gate at qindex 100") {
    // golden: golden_gen qtab_q100 (md_config_process.c:106-135, sharpness=0)
    transforms::QuantTables t;
    transforms::buildQuantTables(100, t);
    const std::int16_t ref[14] = {-20435, -28086, 1024, 1024, 704, 585, 46, 56, 61, 74, 34, 42, 93, 112};
    bool ok = true;
    const std::int16_t* fields[7] = {t.quant, t.quantShift, t.quantFp, t.roundFp, t.zbin, t.round, t.dequant};
    for (int f = 0; f < 7; ++f) {
        for (int i = 0; i < 2; ++i) {
            if (fields[f][i] != ref[f * 2 + i]) ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("quantize fp 4x4 matches the Q0 gate vectors across qindices") {
    // fixture = d3_coeffs block 0 (golden_gen d3_coeffs, first 16 values)
    const std::int32_t fix[16] = {-3784, 78, 4, 54, -17, 18, 102, -68, 56, 3, 36, 120, -18, 23, 6, -22};
    // golden: golden_gen qfp_q{0,1,100,200,255} - qcoeff | dqcoeff | eob
    struct Ref {
        int q;
        std::int32_t qc[16];
        std::int32_t dq[16];
        std::uint16_t eob;
    } refs[] = {
        {0,
         {-946, 20, 1, 14, -4, 5, 26, -17, 14, 1, 9, 30, -5, 6, 2, -6},
         {-3784, 80, 4, 56, -16, 20, 104, -68, 56, 4, 36, 120, -20, 24, 8, -24},
         16},
        {1,
         {-473, 10, 1, 7, -2, 2, 13, -9, 7, 0, 5, 15, -2, 3, 1, -3},
         {-3784, 80, 8, 56, -16, 16, 104, -72, 56, 0, 40, 120, -16, 24, 8, -24},
         16},
        {100,
         {-41, 1, 0, 0, 0, 0, 1, -1, 0, 0, 0, 1, 0, 0, 0, 0},
         {-3813, 112, 0, 0, 0, 0, 112, -112, 0, 0, 0, 112, 0, 0, 0, 0},
         14},
        {200,
         {-10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {-3890, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         1},
        {255,
         {-3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {-4008, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         1},
    };
    std::int16_t scan[16];
    transforms::defaultScan4x4(scan);
    for (const auto& r : refs) {
        transforms::QuantTables t;
        transforms::buildQuantTables(r.q, t);
        std::int32_t qc[16] = {0};
        std::int32_t dq[16] = {0};
        std::uint16_t eob = 0;
        transforms::quantizeFp4x4(fix, t, scan, qc, dq, &eob);
        bool ok = true;
        for (int i = 0; i < 16; ++i) {
            if (qc[i] != r.qc[i] || dq[i] != r.dq[i]) ok = false;
        }
        if (eob != r.eob) ok = false;
        CHECK(ok);
    }
}

TEST_CASE("quantize fp/b 8x8 and ADST proof match the QC1 gate vectors") {
    // goldens: golden_gen qscan8 / q8fp_q100 / q8b_q100 / q8fp_q0 /
    // qadst4fp_q100 / qadst4b_q100 / qadst8fp_q100.
    // log_scale = av1_get_tx_scale_tab[TX_8X8] = 0 (full_loop.c:22 + :1617),
    // so 8x8 uses the same helper at n_coeffs=64. The ADST proof: the fp/b
    // helpers are TxType-agnostic — the fixtures are ADST fwd outputs of the
    // same fixtures the gate recomputes.
    std::int16_t scan8[64];
    transforms::defaultScan8x8(scan8);
    {
        // qscan8 spot check (diagonal d=0..6 tail: 48 41 34 27 20 13 6)
        const std::int16_t ref[16] = {0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5};
        bool scanOk = true;
        for (int i = 0; i < 16; ++i) {
            if (scan8[i] != ref[i]) scanOk = false;
        }
        CHECK(scanOk);
    }

    const std::int16_t in8[64] = {12, 45, 3, 78, 22, 91, 6, 30,
                                  67, 8, 54, 11, 39, 71, 17, 48,
                                  2, 90, 25, 63, 7, 44, 85, 19,
                                  51, 36, 9, 77, 28, 5, 60, 83,
                                  15, 72, 41, 4, 88, 33, 26, 58,
                                  80, 13, 66, 47, 1, 95, 38, 70,
                                  24, 56, 10, 82, 31, 68, 14, 42,
                                  75, 29, 87, 20, 53, 16, 79, 34};
    std::int32_t cdct8[64];
    transforms::fwdTxfm2d8x8(in8, cdct8, 8, transforms::TxType::DCT_DCT);

    struct Ref8 {
        int q;
        bool useB;
        std::int32_t qc[64];
        std::int32_t dq[64];
        std::uint16_t eob;
    };
    // clang-format off
    const Ref8 refs8[] = {
        {100, false,
         {30, -1, 0, 0, 0, 0, 1, -3, -2, -1, -1, 1, 0, -1, -1, -3, -1, 1, -2, 2, -1, 1, 3, -1, 0, -1, -1, 2, 1, -2, 5, -3,
          0, 0, 0, -1, 2, -1, -2, 3, -1, -1, -1, -1, 1, 2, -4, -7, 1, 0, 1, -1, -3, 3, -1, -1, -1, 0, -3, 0, -3, -4, -4, -1},
         {2790, -112, 0, 0, 0, 0, 112, -336, -224, -112, -112, 112, 0, -112, -112, -336, -112, 112, -224, 224, -112, 112, 336, -112, 0, -112, -112, 224, 112, -224, 560, -336,
          0, 0, 0, -112, 224, -112, -224, 336, -112, -112, -112, -112, 112, 224, -448, -784, 112, 0, 112, -112, -336, 336, -112, -112, -112, 0, -336, 0, -336, -448, -448, -112},
         64},
        {100, true,
         {29, -1, 0, 0, 0, 0, 1, -3, -2, -1, -1, 1, 0, -1, 0, -3, 0, 1, -2, 2, -1, 1, 2, 0, 0, -1, -1, 2, 1, -2, 4, -3,
          0, 0, 0, -1, 2, -1, -2, 3, 0, -1, -1, -1, 1, 2, -3, -7, 1, 0, 0, 0, -3, 3, 0, -1, -1, 0, -3, 0, -3, -4, -4, -1},
         {2697, -112, 0, 0, 0, 0, 112, -336, -224, -112, -112, 112, 0, -112, 0, -336, 0, 112, -224, 224, -112, 112, 224, 0, 0, -112, -112, 224, 112, -224, 448, -336,
          0, 0, 0, -112, 224, -112, -224, 336, 0, -112, -112, -112, 112, 224, -336, -784, 112, 0, 0, 0, -336, 336, 0, -112, -112, 0, -336, 0, -336, -448, -448, -112},
         64},
        {0, false,
         {689, -31, 13, 4, -8, 5, 22, -86, -54, -27, -32, 22, -13, -24, -18, -92, -16, 34, -53, 52, -22, 37, 72, -15, -7, -19, -41, 47, 30, -68, 127, -94,
          -5, -3, -3, -27, 53, -41, -70, 80, -18, -25, -41, -35, 41, 46, -100, -191, 33, 3, 17, -15, -90, 80, -15, -21, -31, 9, -85, -4, -83, -116, -125, -33},
         {2756, -124, 52, 16, -32, 20, 88, -344, -216, -108, -128, 88, -52, -96, -72, -368, -64, 136, -212, 208, -88, 148, 288, -60, -28, -76, -164, 188, 120, -272, 508, -376,
          -20, -12, -12, -108, 212, -164, -280, 320, -72, -100, -164, -140, 164, 184, -400, -764, 132, 12, 68, -60, -360, 320, -60, -84, -124, 36, -340, -16, -332, -464, -500, -132},
         64},
    };
    // clang-format on
    for (const auto& r : refs8) {
        transforms::QuantTables t;
        transforms::buildQuantTables(r.q, t);
        std::int32_t qc[64] = {0};
        std::int32_t dq[64] = {0};
        std::uint16_t eob = 0;
        if (r.useB) {
            transforms::quantizeB8x8(cdct8, t, scan8, qc, dq, &eob);
        } else {
            transforms::quantizeFp8x8(cdct8, t, scan8, qc, dq, &eob);
        }
        bool ok = true;
        for (int i = 0; i < 64; ++i) {
            if (qc[i] != r.qc[i] || dq[i] != r.dq[i]) ok = false;
        }
        if (eob != r.eob) ok = false;
        CHECK(ok);
    }

    // ADST proof at 4x4: same helper, ADST-produced coeffs
    {
        const std::int16_t in[16] = {9, 2, 3, 1, 5, 6, 7, 8, 8, 7, 6, 5, 4, 3, 9, 1};
        std::int32_t cadst[16];
        transforms::fwdTxfm2d4x4(in, cadst, 4, transforms::TxType::ADST_ADST);
        std::int16_t scan4[16];
        transforms::defaultScan4x4(scan4);

        struct Ref4 {
            bool useB;
            std::int32_t qc[16];
            std::int32_t dq[16];
            std::uint16_t eob;
        };
        const Ref4 refs4[] = {
            {false,
             {2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {186, 112, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             2},
            {true,
             {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             {93, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             1},
        };
        for (const auto& r : refs4) {
            transforms::QuantTables t;
            transforms::buildQuantTables(100, t);
            std::int32_t qc[16] = {0};
            std::int32_t dq[16] = {0};
            std::uint16_t eob = 0;
            if (r.useB) {
                transforms::quantizeB4x4(cadst, t, scan4, qc, dq, &eob);
            } else {
                transforms::quantizeFp4x4(cadst, t, scan4, qc, dq, &eob);
            }
            bool ok = true;
            for (int i = 0; i < 16; ++i) {
                if (qc[i] != r.qc[i] || dq[i] != r.dq[i]) ok = false;
            }
            if (eob != r.eob) ok = false;
            CHECK(ok);
        }
    }

    // ADST proof at 8x8
    {
        std::int32_t cadst8[64];
        transforms::fwdTxfm2d8x8(in8, cadst8, 8, transforms::TxType::ADST_ADST);
        transforms::QuantTables t;
        transforms::buildQuantTables(100, t);
        std::int32_t qc[64] = {0};
        std::int32_t dq[64] = {0};
        std::uint16_t eob = 0;
        transforms::quantizeFp8x8(cadst8, t, scan8, qc, dq, &eob);
        const std::int32_t refQc[64] = {
            25, 6, 5, 3, 2, 2, 3, 1, 6, 1, 2, 1, 1, 1, 0, -3, 3, 2, 0, 2, -2, 2, 1, 1, 3, 1, -2, 2, 0, -3, 7, -1,
            2, 1, 0, 1, 2, -3, 0, 4, 1, 0, -2, -2, 3, 1, 2, -4, 3, 0, 1, 0, -2, 3, 4, -1, 1, 2, -1, 2, 0, 0, -3, -6};
        const std::int32_t refDq[64] = {
            2325, 672, 560, 336, 224, 224, 336, 112, 672, 112, 224, 112, 112, 112, 0, -336, 336, 224, 0, 224, -224, 224, 112, 112, 336, 112, -224, 224, 0, -336, 784, -112,
            224, 112, 0, 112, 224, -336, 0, 448, 112, 0, -224, -224, 336, 112, 224, -448, 336, 0, 112, 0, -224, 336, 448, -112, 112, 224, -112, 224, 0, 0, -336, -672};
        bool ok = true;
        for (int i = 0; i < 64; ++i) {
            if (qc[i] != refQc[i] || dq[i] != refDq[i]) ok = false;
        }
        if (eob != 64) ok = false;
        CHECK(ok);
    }
}

TEST_CASE("quantize fp/b 16x16 match the C6 gate vectors") {
    // goldens: golden_gen qscan16 / q16fp_q100 / q16b_q100 / q16fp_q0.
    // log_scale = av1_get_tx_scale_tab[TX_16X16] = 0 (full_loop.c:22 +
    // :1617); fixture = fwd2d16_dct of (c*13+r*7+((c*r)&31))%211-105 (the
    // gate recomputes it). fp-vs-b discriminator live at 16x16: qc[5]
    // 2 (fp) vs 1 (b) at q100. Full 256-vector coverage comes from the GPU
    // kernel test (bit-exact vs host).
    std::int16_t scan16[256];
    transforms::defaultScan16x16(scan16);
    {
        // qscan16 spot: head 16 + diagonal-tail formula positions
        const std::int16_t ref[16] = {0, 1, 16, 32, 17, 2, 3, 18, 33, 48, 64, 49, 34, 19, 4, 5};
        bool scanOk = true;
        for (int i = 0; i < 16; ++i) {
            if (scan16[i] != ref[i]) scanOk = false;
        }
        CHECK(scanOk);
    }

    std::int16_t in16[256];
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; ++c) {
            in16[r * 16 + c] =
                static_cast<std::int16_t>((c * 13 + r * 7 + ((c * r) & 31)) % 211) - 105;
        }
    }
    std::int32_t cdct16[256];
    transforms::fwdTxfm2d16x16(in16, cdct16, 16, transforms::TxType::DCT_DCT);

    struct Ref16 {
        int q;
        bool useB;
        std::int32_t qcHead[8];
        std::int32_t dqHead[8];
        std::uint16_t eob;
    };
    const Ref16 refs16[] = {
        {100, false, {0, 3, -33, -5, -2, 2, -4, -4}, {0, 336, -3696, -560, -224, 224, -448, -448}, 256},
        {100, true, {0, 3, -33, -5, -2, 1, -4, -4}, {0, 336, -3696, -560, -224, 112, -448, -448}, 256},
        {0, false, {2, 75, -932, -148, -64, 45, -117, -118}, {8, 300, -3728, -592, -256, 180, -468, -472}, 256},
    };
    for (const auto& r : refs16) {
        transforms::QuantTables t;
        transforms::buildQuantTables(r.q, t);
        std::int32_t qc[256] = {0};
        std::int32_t dq[256] = {0};
        std::uint16_t eob = 0;
        if (r.useB) {
            transforms::quantizeB16x16(cdct16, t, scan16, qc, dq, &eob);
        } else {
            transforms::quantizeFp16x16(cdct16, t, scan16, qc, dq, &eob);
        }
        bool ok = true;
        for (int i = 0; i < 8; ++i) {
            if (qc[i] != r.qcHead[i] || dq[i] != r.dqHead[i]) ok = false;
        }
        if (eob != r.eob) ok = false;
        CHECK(ok);
    }
}

TEST_CASE("quantize fp/b 32x32 match the L0 log_scale-1 gate vectors") {
    // goldens: golden_gen qscan32 / q32fp_q100 / q32b_q100 / q32fp_q0.
    // log_scale = av1_get_tx_scale_tab[TX_32X32] = 1 (full_loop.c:22); the
    // escalated arithmetic (rounding ROUND_POWER_OF_TWO(round, log_scale)
    // full_loop.c:228, threshold << (1+log_scale) :244, >> (16-log_scale)
    // :246, dq >> log_scale :249; b: zbins :36, round add :67, >>(16-log_scale
    // +AOM_QM_BITS) :69-70, dq :74) is proven by the gate lines BEFORE the
    // host port. fp-vs-b discriminator live at log_scale 1 on this fixture:
    // qc[1] 2 (fp) vs 1 (b) and qc[5] 1 (fp) vs 0 (b). fixture = fwd2d32_dct
    // of the L0 formula (the gate recomputes it into a dedicated outDct
    // buffer after the ADST pass claims the shared one).
    std::int16_t scan32[1024];
    transforms::defaultScan32x32(scan32);
    {
        // qscan32 spot: head 16
        const std::int16_t ref[16] = {0, 1, 32, 64, 33, 2, 3, 34, 65, 96, 128, 97, 66, 35, 4, 5};
        bool scanOk = true;
        for (int i = 0; i < 16; ++i) {
            if (scan32[i] != ref[i]) scanOk = false;
        }
        CHECK(scanOk);
    }

    std::int16_t in32[1024];
    for (int r = 0; r < 32; ++r) {
        for (int c = 0; c < 32; ++c) {
            in32[r * 32 + c] = static_cast<std::int16_t>((c * 7 + r * 5 + ((c * r) & 15)) % 173) - 86;
        }
    }
    std::int32_t cdct32[1024];
    transforms::fwdTxfm2d32x32(in32, cdct32, 32, transforms::TxType::DCT_DCT);

    transforms::QuantTables t100;
    transforms::buildQuantTables(100, t100);
    {
        std::int32_t qc[1024] = {0};
        std::int32_t dq[1024] = {0};
        std::uint16_t eob = 0;
        transforms::quantizeFp32x32(cdct32, t100, scan32, qc, dq, &eob);
        bool ok = true;
        const std::int32_t qcHead[8] = {0, 2, 2, -5, 0, 1, -4, -2};
        for (int i = 0; i < 8; ++i) {
            if (qc[i] != qcHead[i]) ok = false;
        }
        if (qc[23] != -2) ok = false;  // fp discriminator at log_scale 1
        CHECK(ok);
    }
    {
        std::int32_t qc[1024] = {0};
        std::int32_t dq[1024] = {0};
        std::uint16_t eob = 0;
        transforms::quantizeB32x32(cdct32, t100, scan32, qc, dq, &eob);
        bool ok = true;
        const std::int32_t qcHead[8] = {0, 1, 2, -5, 0, 0, -4, -2};
        for (int i = 0; i < 8; ++i) {
            if (qc[i] != qcHead[i]) ok = false;
        }
        if (qc[23] != -2) ok = false;  // b agrees with fp at the [23] position under this fixture
        CHECK(ok);
    }
}

TEST_CASE("quantize fp/b 64x64 match the L0 log_scale-2 gate vectors") {
    // goldens: golden_gen qscan64 / q64fp_q100 / q64b_q100 / q64fp_q0.
    // log_scale = av1_get_tx_scale_tab[TX_64X64] = 2 (full_loop.c:22); the
    // escalated arithmetic is proven by the gate lines BEFORE the host port.
    // fp-vs-b discriminator live at log_scale 2 on this fixture: qc[6] -7 (fp)
    // vs -6 (b). fixture = fwd2d64_dct of the L7 formula (the gate recomputes
    // it). DCT-only at 64x64 (no ADST in this tree) but the helpers are
    // TxType-agnostic.
    std::int16_t scan64[4096];
    transforms::defaultScan64x64(scan64);
    {
        const std::int16_t ref[16] = {0, 1, 64, 128, 65, 2, 3, 66, 129, 192, 256, 193, 130, 67, 4, 5};
        bool scanOk = true;
        for (int i = 0; i < 16; ++i) {
            if (scan64[i] != ref[i]) scanOk = false;
        }
        CHECK(scanOk);
    }

    std::int16_t in64[4096];
    for (int r = 0; r < 64; ++r) {
        for (int c = 0; c < 64; ++c) {
            in64[r * 64 + c] = static_cast<std::int16_t>((c * 3 + r * 2 + ((c * r) & 7)) % 149) - 74;
        }
    }
    std::int32_t cdct64[4096];
    transforms::fwdTxfm2d64x64(in64, cdct64, 64, transforms::TxType::DCT_DCT);

    transforms::QuantTables t100;
    transforms::buildQuantTables(100, t100);
    {
        std::int32_t qc[4096] = {0};
        std::int32_t dq[4096] = {0};
        std::uint16_t eob = 0;
        transforms::quantizeFp64x64(cdct64, t100, scan64, qc, dq, &eob);
        bool ok = true;
        const std::int32_t qcHead[8] = {7, 8, 13, -18, 2, -8, -7, -2};
        for (int i = 0; i < 8; ++i) {
            if (qc[i] != qcHead[i]) ok = false;
        }
        if (qc[6] != -7) ok = false;  // fp discriminator at log_scale 2
        CHECK(ok);
    }
    {
        std::int32_t qc[4096] = {0};
        std::int32_t dq[4096] = {0};
        std::uint16_t eob = 0;
        transforms::quantizeB64x64(cdct64, t100, scan64, qc, dq, &eob);
        bool ok = true;
        const std::int32_t qcHead[8] = {7, 8, 13, -18, 2, -8, -6, -2};
        for (int i = 0; i < 8; ++i) {
            if (qc[i] != qcHead[i]) ok = false;
        }
        if (qc[6] != -6) ok = false;  // b discriminator
        CHECK(ok);
    }
}

TEST_CASE("gpu quant_dequant_8x8 matches host quantizeFp8x8 (dct + adst)") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const std::int16_t in8[64] = {12, 45, 3, 78, 22, 91, 6, 30,
                                  67, 8, 54, 11, 39, 71, 17, 48,
                                  2, 90, 25, 63, 7, 44, 85, 19,
                                  51, 36, 9, 77, 28, 5, 60, 83,
                                  15, 72, 41, 4, 88, 33, 26, 58,
                                  80, 13, 66, 47, 1, 95, 38, 70,
                                  24, 56, 10, 82, 31, 68, 14, 42,
                                  75, 29, 87, 20, 53, 16, 79, 34};
    std::int32_t cdct8[64];
    transforms::fwdTxfm2d8x8(in8, cdct8, 8, transforms::TxType::DCT_DCT);
    std::int32_t cadst8[64];
    transforms::fwdTxfm2d8x8(in8, cadst8, 8, transforms::TxType::ADST_ADST);

    transforms::QuantTables t;
    transforms::buildQuantTables(100, t);
    std::int16_t scan8[64];
    transforms::defaultScan8x8(scan8);

    const std::string ptx = *gpurt::compileToPtx(transforms::quantCuSource(), "compute_61");
    const std::vector<std::string> names = gpurt::ptxEntryNames(ptx);
    const auto it = std::find(names.begin(), names.end(), "quant_dequant_8x8");
    REQUIRE(it != names.end());
    gpurt::Kernel k(ptx, *it);

    gpurt::DeviceBuffer dCoeff(64 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dQuantFp(sizeof(t.quantFp));
    gpurt::DeviceBuffer dDequant(sizeof(t.dequant));
    gpurt::DeviceBuffer dRoundFp(sizeof(t.roundFp));
    gpurt::DeviceBuffer dScan(sizeof(scan8));
    gpurt::DeviceBuffer dQcoeff(64 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dDqcoeff(64 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dEob(sizeof(std::uint16_t));
    dQuantFp.uploadFrom(t.quantFp, sizeof(t.quantFp));
    dDequant.uploadFrom(t.dequant, sizeof(t.dequant));
    dRoundFp.uploadFrom(t.roundFp, sizeof(t.roundFp));
    dScan.uploadFrom(scan8, sizeof(scan8));

    CUdeviceptr pCoeff = dCoeff.get();
    CUdeviceptr pQuantFp = dQuantFp.get();
    CUdeviceptr pDequant = dDequant.get();
    CUdeviceptr pRoundFp = dRoundFp.get();
    CUdeviceptr pScan = dScan.get();
    CUdeviceptr pQcoeff = dQcoeff.get();
    CUdeviceptr pDqcoeff = dDqcoeff.get();
    CUdeviceptr pEob = dEob.get();
    void* args[] = {&pCoeff, &pQuantFp, &pDequant, &pRoundFp, &pScan, &pQcoeff, &pDqcoeff, &pEob};

    for (int pass = 0; pass < 2; ++pass) {
        const std::int32_t* fix = pass == 0 ? cdct8 : cadst8;
        dCoeff.uploadFrom(fix, 64 * sizeof(std::int32_t));
        std::int32_t refQc[64] = {0};
        std::int32_t refDq[64] = {0};
        std::uint16_t refEob = 0;
        transforms::quantizeFp8x8(fix, t, scan8, refQc, refDq, &refEob);

        k.launch(1, 1, 64, 1, args);

        std::int32_t gotQc[64] = {0};
        std::int32_t gotDq[64] = {0};
        std::uint16_t gotEob = 0;
        dQcoeff.downloadTo(gotQc, sizeof(gotQc));
        dDqcoeff.downloadTo(gotDq, sizeof(gotDq));
        dEob.downloadTo(&gotEob, sizeof(gotEob));
        bool ok = true;
        for (int i = 0; i < 64; ++i) {
            if (gotQc[i] != refQc[i] || gotDq[i] != refDq[i]) ok = false;
        }
        if (gotEob != refEob) ok = false;
        CHECK(ok);
    }
}

TEST_CASE("gpu quant_dequant_16x16 matches host quantizeFp16x16 full 256") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    // fixture = fwd2d16_dct of the C6 formula (same as the gate's fixture)
    std::int16_t in16[256];
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; ++c) {
            in16[r * 16 + c] =
                static_cast<std::int16_t>((c * 13 + r * 7 + ((c * r) & 31)) % 211) - 105;
        }
    }
    std::int32_t cdct16[256];
    transforms::fwdTxfm2d16x16(in16, cdct16, 16, transforms::TxType::DCT_DCT);
    std::int16_t scan16[256];
    transforms::defaultScan16x16(scan16);
    transforms::QuantTables t_quantFp_probe;
    transforms::buildQuantTables(100, t_quantFp_probe);

    const std::string ptx = *gpurt::compileToPtx(transforms::quantCuSource(), "compute_61");
    const std::vector<std::string> names = gpurt::ptxEntryNames(ptx);
    const auto it = std::find(names.begin(), names.end(), "quant_dequant_16x16");
    REQUIRE(it != names.end());
    gpurt::Kernel k(ptx, *it);

    gpurt::DeviceBuffer dCoeff(256 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dQuantFp(sizeof(t_quantFp_probe.quantFp));
    gpurt::DeviceBuffer dDequant(sizeof(t_quantFp_probe.dequant));
    gpurt::DeviceBuffer dRoundFp(sizeof(t_quantFp_probe.roundFp));
    gpurt::DeviceBuffer dScan(sizeof(scan16));
    gpurt::DeviceBuffer dQcoeff(256 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dDqcoeff(256 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dEob(sizeof(std::uint16_t));
    dScan.uploadFrom(scan16, sizeof(scan16));

    CUdeviceptr pCoeff = dCoeff.get();
    CUdeviceptr pQuantFp = dQuantFp.get();
    CUdeviceptr pDequant = dDequant.get();
    CUdeviceptr pRoundFp = dRoundFp.get();
    CUdeviceptr pScan = dScan.get();
    CUdeviceptr pQcoeff = dQcoeff.get();
    CUdeviceptr pDqcoeff = dDqcoeff.get();
    CUdeviceptr pEob = dEob.get();
    void* args[] = {&pCoeff, &pQuantFp, &pDequant, &pRoundFp, &pScan, &pQcoeff, &pDqcoeff, &pEob};

    for (int pass = 0; pass < 2; ++pass) {
        const int q = pass == 0 ? 100 : 0;
        transforms::QuantTables t;
        transforms::buildQuantTables(q, t);
        dQuantFp.uploadFrom(t.quantFp, sizeof(t.quantFp));
        dDequant.uploadFrom(t.dequant, sizeof(t.dequant));
        dRoundFp.uploadFrom(t.roundFp, sizeof(t.roundFp));
        dCoeff.uploadFrom(cdct16, 256 * sizeof(std::int32_t));

        std::int32_t refQc[256] = {0};
        std::int32_t refDq[256] = {0};
        std::uint16_t refEob = 0;
        transforms::quantizeFp16x16(cdct16, t, scan16, refQc, refDq, &refEob);

        k.launch(1, 1, 256, 1, args);

        std::int32_t gotQc[256] = {0};
        std::int32_t gotDq[256] = {0};
        std::uint16_t gotEob = 0;
        dQcoeff.downloadTo(gotQc, sizeof(gotQc));
        dDqcoeff.downloadTo(gotDq, sizeof(gotDq));
        dEob.downloadTo(&gotEob, sizeof(gotEob));
        bool ok = true;
        for (int i = 0; i < 256; ++i) {
            if (gotQc[i] != refQc[i] || gotDq[i] != refDq[i]) ok = false;
        }
        if (gotEob != refEob) ok = false;
        CHECK(ok);
    }
}

TEST_CASE("gpu quant_dequant_4x4 matches host quantizeFp4x4 (q0 + q100)") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    // fixture = d3_coeffs block 0 (golden_gen d3_coeffs, first 16 values)
    const std::int32_t fix[16] = {-3784, 78, 4, 54, -17, 18, 102, -68, 56, 3, 36, 120, -18, 23, 6, -22};
    const int qs[2] = {0, 100};

    const std::string ptx = *gpurt::compileToPtx(transforms::quantCuSource(), "compute_61");
    const std::vector<std::string> names = gpurt::ptxEntryNames(ptx);
    const auto it = std::find(names.begin(), names.end(), "quant_dequant_4x4");
    REQUIRE(it != names.end());
    gpurt::Kernel k(ptx, *it);

    std::int16_t scan[16];
    transforms::defaultScan4x4(scan);
    gpurt::DeviceBuffer dCoeff(sizeof(fix));
    gpurt::DeviceBuffer dQuantFp(sizeof(std::int16_t) * 2);
    gpurt::DeviceBuffer dDequant(sizeof(std::int16_t) * 2);
    gpurt::DeviceBuffer dRoundFp(sizeof(std::int16_t) * 2);
    gpurt::DeviceBuffer dScan(sizeof(scan));
    gpurt::DeviceBuffer dQcoeff(sizeof(fix));
    gpurt::DeviceBuffer dDqcoeff(sizeof(fix));
    gpurt::DeviceBuffer dEob(sizeof(std::uint16_t));
    dCoeff.uploadFrom(fix, sizeof(fix));
    dScan.uploadFrom(scan, sizeof(scan));

    CUdeviceptr pCoeff = dCoeff.get();
    CUdeviceptr pQuantFp = dQuantFp.get();
    CUdeviceptr pDequant = dDequant.get();
    CUdeviceptr pRoundFp = dRoundFp.get();
    CUdeviceptr pScan = dScan.get();
    CUdeviceptr pQcoeff = dQcoeff.get();
    CUdeviceptr pDqcoeff = dDqcoeff.get();
    CUdeviceptr pEob = dEob.get();
    void* args[] = {&pCoeff, &pQuantFp, &pDequant, &pRoundFp, &pScan, &pQcoeff, &pDqcoeff, &pEob};

    for (int qi = 0; qi < 2; ++qi) {
        transforms::QuantTables t;
        transforms::buildQuantTables(qs[qi], t);
        dQuantFp.uploadFrom(t.quantFp, sizeof(t.quantFp));
        dDequant.uploadFrom(t.dequant, sizeof(t.dequant));
        dRoundFp.uploadFrom(t.roundFp, sizeof(t.roundFp));

        std::int32_t refQc[16] = {0};
        std::int32_t refDq[16] = {0};
        std::uint16_t refEob = 0;
        transforms::quantizeFp4x4(fix, t, scan, refQc, refDq, &refEob);

        k.launch(1, 1, 16, 1, args);

        std::int32_t gotQc[16] = {0};
        std::int32_t gotDq[16] = {0};
        std::uint16_t gotEob = 0;
        dQcoeff.downloadTo(gotQc, sizeof(gotQc));
        dDqcoeff.downloadTo(gotDq, sizeof(gotDq));
        dEob.downloadTo(&gotEob, sizeof(gotEob));
        bool ok = true;
        for (int i = 0; i < 16; ++i) {
            if (gotQc[i] != refQc[i] || gotDq[i] != refDq[i]) ok = false;
        }
        if (gotEob != refEob) ok = false;
        CHECK(ok);
    }
}

TEST_CASE("quantize b 4x4 matches the Q0 gate vectors across qindices") {
    // golden: golden_gen qb_q{0,1,100,200,255} - the b path differs from fp at
    // q1/q100 (zbin pre-scan + quant_shift division vs fp rounding): e.g. at
    // q100 qcoeff[7] is 0 (fp gives -1)
    const std::int32_t fix[16] = {-3784, 78, 4, 54, -17, 18, 102, -68, 56, 3, 36, 120, -18, 23, 6, -22};
    struct Ref {
        int q;
        std::int32_t qc[16];
        std::int32_t dq[16];
        std::uint16_t eob;
    } refs[] = {
        {0,
         {-946, 20, 1, 14, -4, 5, 26, -17, 14, 1, 9, 30, -5, 6, 2, -6},
         {-3784, 80, 4, 56, -16, 20, 104, -68, 56, 4, 36, 120, -20, 24, 8, -24},
         16},
        {1,
         {-473, 10, 0, 7, -2, 2, 13, -8, 7, 0, 4, 15, -2, 3, 1, -3},
         {-3784, 80, 0, 56, -16, 16, 104, -64, 56, 0, 32, 120, -16, 24, 8, -24},
         16},
        {100,
         {-41, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0},
         {-3813, 112, 0, 0, 0, 0, 112, 0, 0, 0, 0, 112, 0, 0, 0, 0},
         14},
        {200,
         {-10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {-3890, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         1},
        {255,
         {-3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         {-4008, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
         1},
    };
    std::int16_t scan[16];
    transforms::defaultScan4x4(scan);
    for (const auto& r : refs) {
        transforms::QuantTables t;
        transforms::buildQuantTables(r.q, t);
        std::int32_t qc[16] = {0};
        std::int32_t dq[16] = {0};
        std::uint16_t eob = 0;
        transforms::quantizeB4x4(fix, t, scan, qc, dq, &eob);
        bool ok = true;
        for (int i = 0; i < 16; ++i) {
            if (qc[i] != r.qc[i] || dq[i] != r.dq[i]) ok = false;
        }
        if (eob != r.eob) ok = false;
        CHECK(ok);
    }
}

TEST_CASE("fdct64 matches svt_av1_fdct64_new golden full vector") {
    // golden: svt_av1_fdct64_new @ cos_bit=13 (fwd_cos_bit_col[4][4]), 64-value
    // input (gate line fdct64)
    const std::int32_t in[64] = {
        200, 80, -50, 30, 100, -20, 60, 10, -35, 95, 5, -70, 45, 25, -15, 55,
        65, -85, 15, -5, 75, -60, 40, 90, -25, 35, 50, -45, 20, -10, 70, -30,
        -40, 55, 10, -60, 85, -20, 35, -5, 45, -75, 25, 65, -35, 15, -25, 80,
        5, -15, 40, -55, 25, -65, 15, 35, -45, 20, -10, 60, -85, 30, 50, -20};
    const std::int32_t golden[64] = {
        605, 494, 213, 262, 261, 246, 367, 82, 157, 101, 216, 295, -17, 73, 93, 48,
        355, 274, 281, 56, 98, 208, 23, 133, 303, 259, 505, 393, 38, 180, 322, 82,
        152, 371, -123, 282, -641, -3, 523, 787, -88, 257, -27, -271, -367, -112, -358, -607,
        -243, 263, 110, 99, -588, 77, 472, -313, 682, -173, -96, -101, 118, -197, 192, 466};
    std::int32_t got[64] = {0};
    transforms::fdct64(in, got);
    bool ok = true;
    for (int i = 0; i < 64; ++i) {
        if (got[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("idct64 matches svt_av1_idct64_new golden full vector") {
    // golden: svt_av1_idct64_new @ cos_bit=12, stage_range 16x12
    // (gen_inv_range_64x64_dct); input 64 values (gate line idct64)
    const std::int32_t in[64] = {
        300, -120, 75, 200, -60, 40, 90, -15, 55, -95, 20, 65, -40, 85, -25, 10,
        30, -70, 95, -35, 60, -15, 80, 25, -50, 45, -20, 70, -90, 15, 50, -55,
        35, -65, 90, -30, 55, -10, 75, 20, -45, 40, -15, 65, -85, 10, 45, -50,
        25, -35, 35, -40, 50, -20, 70, 15, -55, 30, -25, 60, -95, 5, 40, -45};
    const std::int32_t golden[64] = {
        717, 320, 271, 289, 213, 222, 375, 336, -140, 184, 136, 154, 20, -6, -24, -228,
        -41, 57, 86, 136, 83, 192, 23, 40, 146, -156, -35, -214, -26, -75, 226, -396,
        -290, 544, 221, 326, 124, 325, 138, 698, 150, 463, 348, 423, 360, 482, 383, 1149,
        580, 848, 662, 692, 260, 44, -686, -262, 1416, 11, 160, 117, 243, 151, 94, 509};
    std::int32_t got[64] = {0};
    transforms::idct64(in, got);
    bool ok = true;
    for (int i = 0; i < 64; ++i) {
        if (got[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("fwdTxfm2d32x32 matches svt golden full 1024 both TxTypes") {
    // golden: svtd_fwd2d32x32 (av1_tranform_two_d_core_c @ TX_32X32, DCT_DCT +
    // ADST_ADST; fwd_shift_32x32 = {2,-4,0} transforms.c:125, cos_bit col 12 /
    // row 12 = fwd_cos_bit_col/row[3][3] transforms.c:19-22) - gate lines
    // fwd2d32_dct/fwd2d32_adst; fixture (c*7+r*5+((c*r)&15))%173-86, stride 32
    std::int16_t in[1024];
    for (int r = 0; r < 32; ++r)
        for (int c = 0; c < 32; ++c)
            in[r * 32 + c] = static_cast<std::int16_t>((c * 7 + r * 5 + ((c * r) & 15)) % 173) - 86;
    const std::int32_t goldenDct[1024] = {
        -14, 90, 134, -303, 11, 31, -249, -110, -22, 86, -28, -251, -27, -64, 25, 47,
        -75, -75, 17, -14, 70, -145, 1, -98, 57, -30, -40, 7, 15, 13, -94, 63,
        -566, 694, -2205, -2095, 749, -758, 205, -266, -19, 59, -139, 17, 100, -17, -89, -65,
        30, -85, -12, 60, -60, 123, -115, 118, -19, 8, 18, -75, 117, -120, -59, 83,
        -757, -846, -2072, 2130, 650, 248, 97, 286, -116, -131, 91, 60, 182, -7, -94, 31,
        17, 134, 77, -130, -33, 17, 136, -115, -32, 37, 47, -20, -43, 166, -124, 39,
        247, -506, 1186, 682, -394, -1504, 23, 53, -216, 191, -84, -32, -204, -22, 96, -32,
        42, -56, 66, -65, -24, 144, -26, -2, -30, 58, 96, -199, 38, 62, 34, -77,
        -271, -42, -673, 117, -780, 182, 1013, 83, 39, -23, 290, -131, 16, 81, 147, 88,
        -194, -27, -3, 119, 18, -48, -41, 20, 19, 17, 30, 33, -100, 136, -82, 51,
        208, -93, 139, 160, 140, 622, -36, -541, -640, 245, -260, 129, -67, -12, -86, -147,
        144, -27, 65, -33, -23, -79, -76, 125, -11, -179, 52, 20, 26, -103, 31, 44,
        -37, -166, -92, -97, -182, -295, -60, -344, 859, 275, 88, -83, 176, 18, -155, 57,
        98, 176, -206, -46, 35, 65, -30, 78, 44, -143, 9, 125, -49, -4, -28, 76,
        -195, 14, 185, 263, -120, 332, -124, 179, 46, -144, -650, -1, 13, 39, 106, -105,
        100, -145, 0, -4, 93, 25, -163, -12, 38, 8, 22, -1, -79, 5, 1, -60,
        -93, -138, -6, -194, -120, 8, 132, -236, 65, -194, 321, 460, -35, -39, 27, 104,
        -132, 37, 75, 81, -21, -22, -27, -51, 112, -37, 50, -100, -8, 117, 65, -85,
        76, 143, 58, 67, -75, 213, -27, 26, 22, -55, 89, 11, -399, -305, 173, -39,
        1, -76, 87, -141, -174, 79, 88, 91, -109, -16, 0, 62, -42, 73, -8, -57,
        -100, -190, -98, 97, -39, 19, -128, 25, 132, -85, -185, 101, 11, 322, 126, 23,
        -71, -6, 27, 44, 12, 71, 109, -110, -13, -111, 152, 20, -10, -78, 15, -19,
        91, -14, 101, 46, 32, -45, -3, -71, 264, -79, -3, 12, 83, -68, -198, -207,
        33, -56, -17, 79, -25, -34, -184, -17, 93, 89, -83, 19, 7, 80, 36, 99,
        6, -49, -148, 48, -193, 149, -1, -29, -121, 45, -39, 71, -91, 20, -14, 133,
        307, -9, -73, -5, 27, 57, 34, 49, 154, -20, -113, -69, 10, -41, -75, 8,
        -9, -21, 69, 139, -57, 70, 205, -152, 13, -49, 99, -32, -63, 53, -19, -66,
        -80, -98, -61, 22, -41, 82, -4, -144, -113, -87, 2, 138, 108, -8, -61, 37,
        68, -129, 83, 83, -217, -102, -30, -21, 35, 57, -35, -28, -22, 43, 85, -134,
        35, 46, 246, 60, -108, 75, 45, 45, 28, 114, -32, -26, -108, -122, -42, 140,
        -84, -26, -121, 182, -15, 101, 79, 7, 144, -55, -78, 97, -83, 35, -39, 55,
        -39, -9, -81, -85, -101, -45, 32, -66, 68, -61, 15, -99, 42, 26, 48, 67,
        141, -97, -54, 11, -1, 2, -67, -98, 43, -132, -6, 91, 47, -132, 56, 7,
        120, -100, 9, 144, 118, 136, -51, 25, -65, 9, -45, 42, 104, 91, -140, -150,
        26, 36, -76, 114, -90, 62, 21, 10, 137, -21, -24, 96, -65, 58, -57, 23,
        -37, 6, -143, -41, -89, -182, 35, 4, 66, -12, 128, -121, -40, 12, -15, -25,
        -34, -106, 136, 66, -94, 57, -32, -58, -7, -77, -119, -50, -57, 93, 129, -156,
        93, 84, 80, 101, 23, -66, 23, 132, -146, -25, 99, -9, -85, 137, 154, 98,
        -51, -8, 66, -53, -94, 68, 79, -68, 103, 7, 178, 63, -80, 83, -1, -160,
        -44, -23, -181, 48, 68, -65, 25, 64, -116, -70, 116, -6, -56, -57, -155, -88,
        17, 41, -38, 52, -75, 93, -59, -60, -72, -58, -49, 44, -193, 49, 59, 55,
        214, 72, -127, -10, 76, -106, -41, 46, 93, 3, 48, 20, 59, 118, 33, 43,
        11, -72, -16, 33, 71, -48, -17, 38, 179, -111, 93, 85, 28, 47, -38, -202,
        -7, -21, -75, 55, 59, 12, -53, 5, 30, 20, -161, -108, -92, -40, -54, 79,
        53, 38, 0, 57, -12, -83, -168, 46, 48, -112, -52, 45, -105, 148, 63, -30,
        -28, 30, 24, -10, -70, 13, 44, -112, 82, 111, 115, 80, 142, -91, -81, 74,
        -99, -47, -56, -7, 39, 139, 19, 83, 18, -37, 81, -8, -149, -57, 97, 63,
        59, -75, 116, -7, -156, 99, 72, -134, -64, -14, -196, -28, 21, -19, -32, 92,
        -7, 64, 81, -54, -40, -47, -3, -145, -19, -78, 143, 165, 2, -45, -101, 16,
        -38, -140, 49, 44, -93, 122, 117, -22, 68, 103, -20, -18, 108, -16, -12, 1,
        -65, 19, 67, 25, -74, 107, -51, 105, 28, 11, -213, -79, 68, 16, -57, 94,
        148, -85, 151, 88, -104, -3, -99, -194, -39, -5, -63, 45, -2, -24, 30, -53,
        -84, -24, 42, -44, 19, -6, -4, -115, 203, 85, -2, -129, 131, 31, -217, 31,
        -5, -184, -50, 45, -50, 176, 135, 43, 119, -55, -41, 97, -88, 10, 114, -54,
        61, 49, 64, -37, 4, 78, -86, -139, -198, 95, 5, -13, 89, 91, -66, 81,
        139, -48, 99, -2, -112, -31, -116, -63, 42, -113, 15, 137, -112, -40, 121, -138,
        -107, -26, -151, 3, -59, 64, 246, 116, 29, 33, 94, -92, -138, -67, -105, -62,
        -5, -17, 69, 81, -47, 79, -36, -12, 189, -83, 3, 155, -150, 34, 146, -140,
        156, -28, 165, 115, 5, -327, -99, 22, -161, -56, 101, 60, 30, 152, 110, 8,
        -18, -37, -21, -110, -34, 87, -95, -9, 107, -139, -31, 89, -128, -20, 161, -177,
        -63, -90, -266, 197, 121, -16, 39, 202, 19, -27, 89, -69, -157, -73, -67, -62,
        9, 112, 98, -36, 31, 60, -114, -33, 115, -66, 21, 147, -89, -16, 72, -112,
        172, 47, -129, -66, 124, -93, -37, 2, -78, -65, 50, 56, 60, 76, 68, -34,
        -66, 5, -34, -47, 32, 52, -41, 15, 61, -77, -40, 61, -40, -51, 74, -93,
    };
    const std::int32_t goldenAdst[1024] = {
        201, -697, 676, 783, -49, 625, 13, 103, 98, 168, 284, -8, 6, -44, 51, 154,
        37, 13, 52, -1, 211, -74, 107, -90, 64, 27, -45, 46, -20, 68, -23, 27,
        -340, 1611, 1593, -2081, -116, -633, -214, -599, -328, -66, -239, -269, -179, -175, -153, -165,
        -83, -243, -284, -93, -128, -45, -290, -81, -79, -92, -141, -185, 64, -230, -196, -87,
        -534, 1034, -2533, -1341, -194, 406, -98, 41, 76, -216, -110, -198, 163, 87, -141, -52,
        -103, 23, 35, -42, -25, -115, 64, -69, -57, -22, -99, -2, -22, 58, -166, 14,
        122, -424, -184, 281, 1368, -368, -579, -19, -266, 36, -178, -52, -86, -122, -122, -186,
        -19, -91, 62, -132, -182, 3, 37, -47, -131, 0, 92, -214, -2, -39, -3, -89,
        -274, 298, -797, 60, -237, -1282, -173, 130, 132, -275, 290, -148, -82, -106, 31, 245,
        -123, -93, -76, 6, -30, -1, 52, -80, -133, 94, 72, -40, -130, 151, -50, -10,
        52, 118, -479, -62, -15, 146, 546, 667, -658, -83, -143, 8, -124, -83, 24, -88,
        -37, -225, 59, 32, 9, -86, -77, 11, -61, -67, 40, -120, 7, -49, -62, -25,
        136, 171, -374, -183, 114, -549, 38, -674, -296, -13, 337, -119, 112, 127, -178, -37,
        -90, 202, -23, -42, -54, -57, -30, 86, 67, -93, -66, 19, 33, 2, -109, 88,
        -153, -4, -381, 143, 30, 38, -56, 112, 74, 566, -178, -428, -74, 17, 8, -183,
        104, 6, -69, -146, 35, 76, -133, 6, -35, -42, -44, 56, -1, -47, -116, -16,
        -98, -72, -238, -173, -67, -330, 97, -243, -154, -197, -264, -79, 111, 155, -74, 74,
        -82, 25, -74, 11, 163, 70, -71, -130, 85, -17, 89, -69, -56, 7, 27, -8,
        -79, 160, -66, -14, -80, -38, 169, 39, -71, -31, 173, 271, 107, -347, -59, -37,
        -16, -53, 100, -48, -125, -59, -137, 83, -48, 1, -51, -19, -126, 65, 21, -62,
        -115, -10, -332, -85, -90, -70, -85, -25, -82, -13, -211, -25, -247, -116, 86, 192,
        -67, -27, 68, -47, -68, -41, 156, 86, 19, -215, 106, 60, 16, -22, 38, -96,
        -33, -25, -100, -78, 176, -99, -32, -189, 206, 33, -25, -15, 146, 95, 107, -19,
        -159, -124, -18, 27, 8, 27, -117, -78, -104, -76, -15, 1, -104, -2, 46, 52,
        -13, 109, -228, -90, -111, -21, -121, -41, -102, 25, -170, -7, -31, -91, -168, -115,
        124, 65, 6, -54, 20, -8, -112, -35, 163, 123, 78, -29, -62, -4, 11, -30,
        -95, 49, -203, -72, -46, -43, 232, -20, -8, -53, 45, 46, 10, 48, -46, 66,
        85, 37, -123, -100, -19, 69, 29, -57, -18, -173, -162, -61, 30, 79, 18, -43,
        59, -60, -27, 104, -40, -164, -3, -80, -170, 6, 17, -96, -58, -59, 65, -150,
        -78, -164, 81, 69, -79, 82, 4, 30, -19, 93, -10, 126, 36, -49, -124, 39,
        -140, -19, -282, 58, -92, -129, 72, -26, 13, 121, -9, 52, -81, 79, 3, 69,
        -53, 35, 133, 18, -124, -90, 13, -125, 57, -21, 20, -43, -42, -152, -85, 133,
        96, -13, -190, -39, 16, -71, 51, -91, -33, -73, -98, -48, 77, -156, -54, -71,
        45, -169, -35, -32, -65, 231, 50, 34, 3, 60, -144, -34, 80, 123, -24, -55,
        34, 150, -288, 24, -43, -107, 17, -93, 56, 27, -54, 103, 105, 35, -112, 141,
        59, 29, -5, -22, -85, -42, -14, -172, 54, 9, 50, -79, 57, 37, -81, -128,
        45, -70, -210, 106, -21, -22, 33, -26, 38, 78, -177, -127, -71, -116, -11, -85,
        -38, -79, 91, 84, -18, -11, -36, 47, -22, -28, 29, -44, -135, -18, 92, 163,
        -21, -83, -98, 20, -125, -141, 89, -151, 21, -4, 45, 38, 57, 84, 151, 2,
        -78, -51, -88, 9, -8, -33, 9, 158, -65, -146, 92, 45, -27, 1, -81, -134,
        -27, 47, -125, 107, -175, 17, 139, -35, -106, 28, -90, 40, -182, -144, -74, -71,
        72, 134, -38, -45, 87, -98, -112, 45, -11, -183, 71, 6, -25, 125, 94, 33,
        22, -55, -195, -12, -79, -92, 97, -123, 66, -41, -9, 67, 67, 52, 100, -139,
        13, 85, -151, -69, 145, 25, -127, 127, 74, 33, 37, -54, -211, -16, -69, -31,
        83, 46, -128, 140, 68, -47, -63, -128, 81, -33, -158, 8, -147, 84, 136, -89,
        -99, 116, -108, -85, 95, -69, -125, -72, -40, -92, 145, 41, 94, 81, -60, 47,
        -2, -29, -289, -45, -92, -8, -52, 16, 116, 14, 16, 85, -199, -135, 129, -57,
        11, 93, 50, 26, 28, 2, 37, 31, -26, -14, -39, -68, -46, 6, -171, 52,
        14, 40, -102, 9, 14, -75, 84, -144, -11, -276, -32, 262, -42, -35, 102, -3,
        -59, -20, -81, -77, -125, -104, 21, 37, -18, 81, 90, -119, 88, 92, -126, 94,
        -29, 3, -118, 94, -114, 24, -40, 76, 93, -17, -177, 76, -102, -125, 31, -64,
        50, -27, 90, 150, 79, 52, 57, -68, -172, 18, 9, -162, 64, 53, -163, 18,
        -100, -72, -128, -4, -65, -69, -27, -237, 228, 71, -16, -8, 33, 57, -39, -14,
        45, -115, -172, -40, -156, -54, 112, 34, -5, 100, 23, -84, 27, 65, -93, 86,
        -2, -12, 50, 39, -7, 206, 54, -191, -112, 4, -171, -120, -10, 83, -71, -11,
        187, -29, 41, 102, -25, -13, 44, 0, -69, -30, -22, -47, 23, -14, -75, 3,
        -164, 32, -194, -125, -354, -39, 158, -82, 90, 131, 90, 57, 12, 25, -151, -140,
        33, -148, -79, 141, -74, -22, 26, -77, 35, -5, -20, 70, -30, 22, 18, 24,
        41, -76, 144, 184, 60, -87, 37, -101, -113, -107, -103, -47, -79, 83, 56, 52,
        166, -49, -16, 48, -132, 29, 4, -55, 105, -37, -54, 51, -92, -75, 105, -66,
        -74, -89, -367, 89, -25, -104, -42, 35, 66, 3, 124, 105, -86, -1, -96, -147,
        -6, -75, 63, 59, -74, 107, -34, -147, 107, -76, -71, 164, -71, -40, 125, -70,
        116, 128, -248, -43, 118, -87, -34, 25, -16, -150, -11, 33, -78, 52, 64, -35,
        2, -38, 26, -21, -95, 132, -45, -116, 185, -68, -121, 168, -52, -115, 172, -125,
    };
    std::int32_t got[1024] = {0};
    transforms::fwdTxfm2d32x32(in, got, 32, transforms::TxType::DCT_DCT);
    bool okDct = true;
    for (int i = 0; i < 1024; ++i) {
        if (got[i] != goldenDct[i]) okDct = false;
    }
    CHECK(okDct);

    transforms::fwdTxfm2d32x32(in, got, 32, transforms::TxType::ADST_ADST);
    bool okAdst = true;
    for (int i = 0; i < 1024; ++i) {
        if (got[i] != goldenAdst[i]) okAdst = false;
    }
    CHECK(okAdst);
}

TEST_CASE("invTxfm2dAdd32x32 matches svt golden 1024 samples both TxTypes") {
    // golden: svtd_inv2dadd32x32 (inv_txfm2d_add_c @ TX_32X32):
    // inv_shift_32x32 = {-2,-4} (inv_transforms.c:21), cos_bit 12/12 =
    // inv_cos_bit_col/row[3][3], clamps bd+8=16 / max(bd+6,16)=16, row >>2,
    // col >>4, clip add (gate lines inv2d32_dct/adst_onto_vpred). coeff
    // fixtures: cdct[i] = ((r*13+c*29+31)%89)-44, cadst[i] =
    // ((r*17+c*11+7)%79)-39; pred[r][c] = 10+3*c (V-pred)
    std::int32_t cdct[1024];
    std::int32_t cadst[1024];
    for (int i = 0; i < 1024; ++i) {
        const int r = i / 32, c = i % 32;
        cdct[i] = ((r * 13 + c * 29 + 31) % 89) - 44;
        cadst[i] = ((r * 17 + c * 11 + 7) % 79) - 39;
    }
    const std::uint8_t goldenDct[1024] = {
        11, 13, 18, 19, 23, 27, 27, 32, 34, 39, 41, 45, 45, 51, 52, 57,
        57, 71, 71, 67, 77, 71, 76, 73, 83, 83, 89, 89, 92, 96, 100, 102,
        11, 13, 18, 18, 22, 29, 28, 32, 34, 39, 42, 45, 47, 51, 54, 56,
        63, 72, 54, 65, 73, 69, 77, 78, 81, 83, 87, 87, 91, 96, 100, 102,
        10, 12, 17, 19, 18, 25, 29, 31, 34, 38, 41, 43, 44, 48, 54, 53,
        61, 54, 61, 72, 77, 70, 74, 78, 83, 84, 89, 93, 87, 94, 99, 101,
        11, 13, 17, 18, 21, 23, 26, 31, 33, 37, 44, 43, 45, 54, 54, 55,
        59, 64, 66, 68, 75, 73, 76, 79, 82, 85, 87, 96, 97, 94, 101, 103,
        11, 13, 19, 19, 21, 25, 28, 29, 34, 33, 41, 46, 49, 52, 52, 56,
        59, 61, 64, 72, 80, 68, 74, 78, 82, 84, 87, 89, 95, 97, 104, 99,
        11, 13, 18, 17, 21, 24, 26, 29, 38, 38, 38, 45, 46, 51, 53, 50,
        58, 62, 65, 66, 74, 75, 75, 79, 81, 84, 87, 93, 93, 98, 96, 98,
        12, 13, 23, 22, 22, 28, 27, 36, 37, 37, 42, 44, 50, 51, 61, 57,
        57, 63, 65, 74, 88, 62, 72, 77, 82, 86, 85, 88, 93, 95, 97, 103,
        14, 18, 26, 6, 21, 22, 27, 30, 34, 36, 39, 42, 46, 47, 53, 60,
        59, 62, 66, 65, 76, 79, 75, 79, 83, 81, 84, 92, 93, 97, 99, 102,
        11, 17, 6, 13, 28, 25, 31, 33, 38, 38, 43, 45, 50, 53, 57, 59,
        64, 68, 74, 89, 134, 32, 63, 68, 78, 77, 84, 86, 91, 92, 96, 100,
        8, 7, 14, 24, 20, 27, 24, 35, 32, 41, 36, 48, 41, 55, 47, 66,
        51, 77, 52, 105, 12, 0, 86, 58, 85, 67, 87, 83, 92, 90, 97, 98,
        9, 11, 9, 16, 21, 24, 27, 31, 33, 34, 38, 41, 44, 47, 50, 49,
        57, 55, 58, 54, 23, 102, 90, 86, 100, 89, 87, 94, 95, 98, 101, 105,
        10, 12, 14, 19, 21, 25, 23, 34, 36, 38, 40, 46, 45, 50, 51, 60,
        61, 63, 64, 70, 69, 68, 76, 72, 82, 95, 88, 93, 95, 97, 102, 103,
        9, 12, 13, 17, 22, 27, 24, 23, 35, 34, 38, 43, 46, 48, 52, 55,
        56, 58, 61, 62, 51, 83, 80, 81, 85, 85, 92, 91, 96, 93, 101, 106,
        11, 13, 15, 19, 23, 25, 30, 30, 33, 37, 42, 51, 43, 49, 51, 57,
        58, 63, 63, 69, 69, 72, 75, 73, 82, 86, 88, 91, 95, 96, 95, 101,
        10, 13, 15, 19, 23, 29, 26, 30, 34, 38, 42, 43, 51, 47, 53, 55,
        57, 59, 62, 65, 57, 80, 78, 82, 86, 86, 91, 93, 95, 96, 100, 102,
        11, 11, 16, 18, 24, 20, 23, 32, 31, 38, 38, 41, 47, 52, 52, 58,
        59, 63, 61, 70, 69, 76, 72, 70, 81, 85, 86, 90, 89, 96, 99, 102,
        10, 11, 14, 17, 20, 19, 31, 32, 35, 37, 41, 42, 46, 49, 52, 55,
        58, 64, 61, 67, 58, 79, 74, 83, 97, 85, 94, 91, 98, 100, 102, 104,
        11, 10, 16, 18, 20, 28, 30, 32, 34, 39, 39, 44, 47, 52, 52, 59,
        56, 67, 65, 74, 72, 93, 68, 87, 78, 78, 85, 87, 92, 94, 98, 100,
        5, 15, 16, 18, 24, 25, 29, 33, 35, 37, 41, 44, 46, 51, 53, 57,
        58, 62, 63, 73, 60, 92, 135, 70, 79, 82, 90, 89, 95, 97, 100, 103,
        10, 10, 14, 13, 14, 29, 28, 31, 33, 38, 38, 42, 45, 48, 52, 53,
        56, 58, 66, 58, 68, 40, 93, 101, 84, 93, 88, 97, 96, 100, 103, 106,
        9, 15, 18, 24, 13, 20, 28, 30, 34, 34, 40, 41, 44, 49, 55, 54,
        60, 61, 64, 63, 66, 72, 74, 80, 78, 85, 88, 87, 94, 97, 99, 101,
        12, 12, 15, 22, 25, 23, 30, 30, 36, 39, 39, 44, 47, 57, 51, 54,
        56, 60, 62, 63, 70, 62, 83, 85, 84, 87, 91, 91, 93, 97, 101, 103,
        8, 13, 17, 20, 20, 24, 28, 32, 32, 38, 44, 41, 50, 47, 49, 55,
        59, 61, 64, 65, 65, 72, 76, 80, 81, 84, 91, 92, 94, 97, 100, 97,
        11, 10, 13, 20, 22, 24, 27, 30, 26, 36, 41, 43, 46, 50, 54, 54,
        56, 61, 61, 62, 71, 65, 80, 82, 82, 86, 88, 93, 93, 98, 98, 105,
        7, 12, 19, 18, 21, 23, 28, 27, 35, 40, 41, 43, 47, 47, 55, 57,
        60, 62, 65, 66, 66, 73, 77, 80, 81, 84, 97, 93, 94, 98, 99, 105,
        12, 7, 12, 16, 22, 24, 28, 31, 33, 36, 39, 43, 45, 48, 48, 57,
        54, 62, 55, 57, 75, 69, 81, 83, 83, 92, 91, 85, 93, 95, 99, 103,
        3, 10, 20, 19, 27, 23, 31, 29, 36, 37, 43, 42, 49, 49, 54, 55,
        65, 62, 77, 46, 55, 74, 73, 80, 78, 85, 82, 89, 93, 96, 98, 102,
        18, 0, 11, 20, 21, 24, 29, 31, 34, 37, 40, 43, 48, 48, 53, 56,
        60, 60, 74, 80, 62, 70, 73, 80, 78, 87, 86, 90, 91, 96, 99, 103,
        32, 31, 0, 24, 15, 28, 24, 32, 32, 39, 39, 43, 45, 50, 51, 55,
        64, 64, 63, 70, 67, 71, 77, 83, 78, 85, 86, 91, 95, 97, 98, 103,
        10, 26, 16, 21, 22, 25, 32, 33, 35, 38, 40, 46, 48, 52, 53, 63,
        61, 54, 67, 70, 66, 69, 77, 82, 83, 85, 87, 90, 92, 96, 101, 103,
        13, 18, 11, 20, 20, 22, 26, 33, 34, 38, 41, 41, 46, 50, 51, 57,
        51, 60, 65, 68, 67, 70, 77, 80, 83, 86, 88, 90, 97, 93, 96, 102,
        11, 19, 13, 20, 21, 25, 25, 31, 33, 37, 37, 45, 51, 49, 53, 56,
        57, 61, 66, 69, 67, 70, 78, 81, 83, 86, 89, 90, 96, 102, 98, 103,
    };
    const std::uint8_t goldenAdst[1024] = {
        9, 14, 10, 16, 26, 26, 30, 28, 37, 41, 39, 44, 46, 50, 52, 55,
        56, 63, 62, 68, 72, 73, 76, 79, 84, 85, 88, 91, 93, 97, 100, 103,
        9, 14, 17, 13, 21, 23, 32, 33, 32, 31, 41, 42, 47, 49, 52, 56,
        60, 59, 67, 67, 68, 73, 76, 79, 81, 84, 88, 92, 96, 97, 100, 103,
        11, 12, 18, 16, 23, 22, 29, 30, 39, 42, 36, 43, 44, 53, 52, 56,
        55, 63, 61, 70, 71, 73, 75, 79, 83, 85, 87, 90, 92, 96, 100, 102,
        9, 14, 16, 19, 21, 26, 27, 34, 32, 37, 41, 43, 42, 49, 54, 62,
        63, 55, 70, 67, 65, 73, 75, 78, 80, 84, 88, 92, 96, 96, 100, 102,
        11, 12, 16, 18, 22, 24, 28, 30, 38, 42, 37, 45, 45, 47, 49, 53,
        66, 64, 59, 86, 75, 75, 75, 79, 85, 85, 88, 91, 93, 97, 100, 103,
        9, 15, 15, 19, 20, 26, 27, 34, 32, 35, 40, 42, 44, 49, 50, 52,
        53, 63, 54, 63, 79, 77, 82, 76, 82, 83, 91, 94, 99, 97, 101, 103,
        11, 12, 17, 17, 23, 23, 30, 29, 40, 42, 39, 44, 46, 48, 52, 54,
        60, 63, 64, 62, 71, 71, 80, 80, 84, 81, 86, 92, 93, 96, 99, 101,
        8, 14, 16, 20, 21, 26, 27, 34, 33, 33, 40, 42, 46, 48, 52, 53,
        56, 61, 61, 66, 72, 74, 77, 80, 81, 86, 85, 95, 99, 99, 100, 101,
        11, 13, 16, 18, 23, 25, 29, 29, 41, 44, 37, 46, 45, 50, 51, 56,
        58, 64, 62, 67, 69, 75, 76, 83, 84, 87, 84, 103, 74, 86, 94, 105,
        9, 13, 16, 19, 21, 25, 28, 34, 35, 31, 41, 41, 46, 47, 52, 53,
        57, 60, 63, 65, 71, 74, 78, 78, 78, 75, 93, 81, 79, 105, 102, 107,
        12, 12, 17, 17, 24, 23, 31, 26, 47, 46, 36, 46, 46, 49, 51, 55,
        58, 62, 61, 67, 71, 76, 72, 77, 76, 89, 89, 87, 98, 100, 101, 106,
        9, 13, 17, 19, 22, 25, 30, 35, 38, 27, 41, 38, 48, 47, 54, 50,
        57, 57, 67, 67, 72, 70, 75, 79, 82, 85, 88, 90, 90, 100, 101, 104,
        13, 10, 18, 15, 26, 21, 35, 22, 69, 59, 29, 53, 46, 49, 47, 53,
        59, 64, 64, 65, 70, 72, 76, 78, 80, 86, 87, 89, 95, 98, 100, 104,
        6, 17, 15, 25, 21, 34, 36, 62, 78, 0, 29, 28, 33, 36, 45, 47,
        53, 53, 61, 61, 68, 68, 73, 74, 79, 81, 85, 88, 89, 96, 97, 101,
        4, 19, 7, 24, 13, 39, 19, 46, 0, 3, 55, 35, 51, 46, 54, 53,
        61, 63, 65, 65, 72, 72, 77, 78, 82, 86, 88, 89, 95, 97, 101, 104,
        9, 8, 15, 20, 25, 26, 24, 29, 24, 48, 46, 45, 46, 49, 53, 56,
        58, 60, 65, 67, 72, 73, 77, 79, 83, 84, 89, 93, 93, 99, 101, 104,
        13, 10, 16, 16, 23, 28, 28, 34, 14, 28, 43, 42, 47, 49, 51, 54,
        59, 64, 63, 66, 71, 72, 76, 78, 81, 86, 87, 89, 95, 97, 100, 104,
        13, 13, 19, 17, 19, 24, 28, 41, 27, 43, 50, 43, 45, 48, 52, 55,
        58, 60, 65, 67, 71, 73, 77, 79, 82, 84, 88, 93, 93, 99, 101, 104,
        10, 12, 15, 20, 19, 23, 19, 32, 29, 23, 49, 53, 50, 52, 51, 57,
        60, 67, 63, 67, 72, 73, 77, 79, 82, 87, 87, 90, 95, 97, 101, 105,
        10, 14, 17, 19, 21, 25, 26, 28, 30, 38, 30, 46, 46, 54, 52, 54,
        56, 59, 66, 67, 72, 72, 78, 78, 82, 83, 88, 95, 94, 100, 102, 104,
        10, 14, 15, 21, 20, 26, 23, 34, 27, 36, 41, 42, 45, 50, 55, 56,
        60, 62, 63, 65, 74, 72, 76, 77, 82, 85, 86, 89, 95, 95, 100, 104,
        11, 13, 18, 18, 22, 24, 28, 29, 31, 37, 39, 42, 47, 47, 54, 53,
        61, 57, 66, 62, 73, 73, 81, 77, 79, 78, 87, 104, 95, 102, 103, 105,
        10, 13, 15, 20, 20, 26, 25, 33, 28, 36, 40, 43, 46, 50, 52, 56,
        59, 67, 61, 66, 68, 73, 74, 85, 78, 87, 56, 86, 99, 92, 102, 106,
        9, 14, 17, 20, 21, 26, 26, 31, 30, 38, 39, 44, 45, 49, 53, 56,
        58, 60, 64, 68, 70, 77, 75, 84, 83, 97, 91, 70, 95, 94, 93, 97,
        10, 13, 15, 20, 21, 26, 25, 33, 29, 37, 40, 43, 46, 50, 53, 56,
        60, 71, 60, 65, 69, 72, 74, 82, 80, 86, 88, 92, 94, 104, 98, 98,
        10, 14, 17, 19, 21, 25, 27, 31, 31, 37, 40, 43, 46, 48, 53, 55,
        59, 58, 65, 66, 72, 77, 76, 80, 79, 90, 89, 89, 92, 97, 97, 103,
        11, 12, 15, 19, 22, 26, 26, 32, 31, 37, 41, 43, 48, 50, 55, 58,
        65, 87, 54, 61, 73, 66, 71, 75, 81, 86, 89, 88, 91, 98, 98, 101,
        11, 13, 19, 17, 23, 24, 30, 29, 33, 35, 44, 40, 50, 44, 62, 54,
        77, 37, 28, 73, 59, 69, 72, 80, 81, 87, 87, 87, 92, 96, 98, 102,
        11, 11, 14, 19, 21, 26, 25, 32, 30, 36, 38, 39, 46, 49, 56, 48,
        52, 32, 76, 72, 73, 75, 79, 81, 85, 88, 91, 91, 94, 100, 101, 104,
        8, 16, 17, 20, 20, 27, 27, 34, 28, 37, 37, 46, 47, 51, 51, 53,
        58, 58, 67, 68, 70, 72, 77, 80, 83, 87, 89, 89, 94, 98, 100, 103,
        15, 6, 13, 17, 25, 27, 26, 29, 27, 37, 39, 45, 45, 49, 51, 53,
        56, 52, 68, 68, 71, 73, 77, 80, 83, 87, 90, 90, 94, 98, 100, 103,
        0, 13, 28, 23, 27, 24, 28, 31, 31, 38, 41, 45, 47, 50, 52, 54,
        58, 56, 67, 68, 71, 73, 78, 80, 84, 87, 90, 90, 94, 98, 100, 104,
    };
    std::uint8_t pred[1024];

    for (int i = 0; i < 1024; ++i) pred[i] = static_cast<std::uint8_t>(10 + 3 * (i % 32));
    transforms::invTxfm2dAdd32x32(cdct, pred, 32, transforms::TxType::DCT_DCT);
    bool okDct = true;
    for (int i = 0; i < 1024; ++i) {
        if (pred[i] != goldenDct[i]) okDct = false;
    }
    CHECK(okDct);

    for (int i = 0; i < 1024; ++i) pred[i] = static_cast<std::uint8_t>(10 + 3 * (i % 32));
    transforms::invTxfm2dAdd32x32(cadst, pred, 32, transforms::TxType::ADST_ADST);
    bool okAdst = true;
    for (int i = 0; i < 1024; ++i) {
        if (pred[i] != goldenAdst[i]) okAdst = false;
    }
    CHECK(okAdst);
}

TEST_CASE("gpu quant_dequant_32x32 matches host quantizeFp32x32 full 1024") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    // fixture = fwd2d32_dct of the L0 formula (same as the gate's fixture)
    std::int16_t in32[1024];
    for (int r = 0; r < 32; ++r) {
        for (int c = 0; c < 32; ++c) {
            in32[r * 32 + c] = static_cast<std::int16_t>((c * 7 + r * 5 + ((c * r) & 15)) % 173) - 86;
        }
    }
    std::int32_t cdct32[1024];
    transforms::fwdTxfm2d32x32(in32, cdct32, 32, transforms::TxType::DCT_DCT);
    std::int16_t scan32[1024];
    transforms::defaultScan32x32(scan32);

    const std::string ptx = *gpurt::compileToPtx(transforms::quantCuSource(), "compute_61");
    const std::vector<std::string> names = gpurt::ptxEntryNames(ptx);
    const auto it = std::find(names.begin(), names.end(), "quant_dequant_32x32");
    REQUIRE(it != names.end());
    gpurt::Kernel k(ptx, *it);

    transforms::QuantTables t_probe;
    transforms::buildQuantTables(100, t_probe);

    gpurt::DeviceBuffer dCoeff(1024 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dQuantFp(sizeof(t_probe.quantFp));
    gpurt::DeviceBuffer dDequant(sizeof(t_probe.dequant));
    gpurt::DeviceBuffer dRoundFp(sizeof(t_probe.roundFp));
    gpurt::DeviceBuffer dScan(sizeof(scan32));
    gpurt::DeviceBuffer dQcoeff(1024 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dDqcoeff(1024 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dEob(sizeof(std::uint16_t));
    dScan.uploadFrom(scan32, sizeof(scan32));

    CUdeviceptr pCoeff = dCoeff.get();
    CUdeviceptr pQuantFp = dQuantFp.get();
    CUdeviceptr pDequant = dDequant.get();
    CUdeviceptr pRoundFp = dRoundFp.get();
    CUdeviceptr pScan = dScan.get();
    CUdeviceptr pQcoeff = dQcoeff.get();
    CUdeviceptr pDqcoeff = dDqcoeff.get();
    CUdeviceptr pEob = dEob.get();
    void* args[] = {&pCoeff, &pQuantFp, &pDequant, &pRoundFp, &pScan, &pQcoeff, &pDqcoeff, &pEob};

    for (int pass = 0; pass < 2; ++pass) {
        const int q = pass == 0 ? 100 : 0;
        transforms::QuantTables t;
        transforms::buildQuantTables(q, t);
        dQuantFp.uploadFrom(t.quantFp, sizeof(t.quantFp));
        dDequant.uploadFrom(t.dequant, sizeof(t.dequant));
        dRoundFp.uploadFrom(t.roundFp, sizeof(t.roundFp));
        dCoeff.uploadFrom(cdct32, 1024 * sizeof(std::int32_t));

        std::int32_t refQc[1024] = {0};
        std::int32_t refDq[1024] = {0};
        std::uint16_t refEob = 0;
        transforms::quantizeFp32x32(cdct32, t, scan32, refQc, refDq, &refEob);

        k.launch(1, 1, 1024, 1, args);

        std::int32_t gotQc[1024] = {0};
        std::int32_t gotDq[1024] = {0};
        std::uint16_t gotEob = 0;
        dQcoeff.downloadTo(gotQc, sizeof(gotQc));
        dDqcoeff.downloadTo(gotDq, sizeof(gotDq));
        dEob.downloadTo(&gotEob, sizeof(gotEob));
        bool ok = true;
        for (int i = 0; i < 1024; ++i) {
            if (gotQc[i] != refQc[i] || gotDq[i] != refDq[i]) ok = false;
        }
        if (gotEob != refEob) ok = false;
        CHECK(ok);
    }
}

TEST_CASE("gpu quant_dequant_64x64 matches host quantizeFp64x64 full 4096") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    // fixture = fwd2d64_dct of the L7 formula (same as the gate's fixture)
    std::int16_t in64[4096];
    for (int r = 0; r < 64; ++r) {
        for (int c = 0; c < 64; ++c) {
            in64[r * 64 + c] = static_cast<std::int16_t>((c * 3 + r * 2 + ((c * r) & 7)) % 149) - 74;
        }
    }
    std::int32_t cdct64[4096];
    transforms::fwdTxfm2d64x64(in64, cdct64, 64, transforms::TxType::DCT_DCT);
    std::int16_t scan64[4096];
    transforms::defaultScan64x64(scan64);

    const std::string ptx = *gpurt::compileToPtx(transforms::quantCuSource(), "compute_61");
    const std::vector<std::string> names = gpurt::ptxEntryNames(ptx);
    const auto it = std::find(names.begin(), names.end(), "quant_dequant_64x64");
    REQUIRE(it != names.end());
    gpurt::Kernel k(ptx, *it);

    gpurt::DeviceBuffer dCoeff(4096 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dQuantFp(sizeof(transforms::QuantTables{}.quantFp));
    gpurt::DeviceBuffer dDequant(sizeof(transforms::QuantTables{}.dequant));
    gpurt::DeviceBuffer dRoundFp(sizeof(transforms::QuantTables{}.roundFp));
    gpurt::DeviceBuffer dScan(sizeof(scan64));
    gpurt::DeviceBuffer dQcoeff(4096 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dDqcoeff(4096 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dEob(sizeof(std::uint16_t));
    dScan.uploadFrom(scan64, sizeof(scan64));

    CUdeviceptr pCoeff = dCoeff.get();
    CUdeviceptr pQuantFp = dQuantFp.get();
    CUdeviceptr pDequant = dDequant.get();
    CUdeviceptr pRoundFp = dRoundFp.get();
    CUdeviceptr pScan = dScan.get();
    CUdeviceptr pQcoeff = dQcoeff.get();
    CUdeviceptr pDqcoeff = dDqcoeff.get();
    CUdeviceptr pEob = dEob.get();
    void* args[] = {&pCoeff, &pQuantFp, &pDequant, &pRoundFp, &pScan, &pQcoeff, &pDqcoeff, &pEob};

    for (int pass = 0; pass < 2; ++pass) {
        const int q = pass == 0 ? 100 : 0;
        transforms::QuantTables t;
        transforms::buildQuantTables(q, t);
        dQuantFp.uploadFrom(t.quantFp, sizeof(t.quantFp));
        dDequant.uploadFrom(t.dequant, sizeof(t.dequant));
        dRoundFp.uploadFrom(t.roundFp, sizeof(t.roundFp));
        dCoeff.uploadFrom(cdct64, 4096 * sizeof(std::int32_t));

        std::int32_t refQc[4096] = {0};
        std::int32_t refDq[4096] = {0};
        std::uint16_t refEob = 0;
        transforms::quantizeFp64x64(cdct64, t, scan64, refQc, refDq, &refEob);

        k.launch(1, 1, 1024, 1, args);

        std::int32_t gotQc[4096] = {0};
        std::int32_t gotDq[4096] = {0};
        std::uint16_t gotEob = 0;
        dQcoeff.downloadTo(gotQc, sizeof(gotQc));
        dDqcoeff.downloadTo(gotDq, sizeof(gotDq));
        dEob.downloadTo(&gotEob, sizeof(gotEob));
        bool ok = true;
        for (int i = 0; i < 4096; ++i) {
            if (gotQc[i] != refQc[i] || gotDq[i] != refDq[i]) ok = false;
        }
        if (gotEob != refEob) ok = false;
        CHECK(ok);
    }
}
