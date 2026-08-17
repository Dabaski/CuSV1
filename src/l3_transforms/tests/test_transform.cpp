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
    // golden: inv_txfm2d_add_c (ADST_ADST) onto the V pred {10,40,30,20} x4
    // with the E1 ADST_ADST coeffs of src {9,4,7,5,...}
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
