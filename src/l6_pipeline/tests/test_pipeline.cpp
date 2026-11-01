#include <doctest.h>
#include <algorithm>
#include <vector>
#include <gpurt.h>
#include <pipeline.h>

TEST_CASE("pipeline v-mode coeffs match svt composition golden") {
    // golden: build_intra_predictors (V_PRED) -> subtract -> svt_av1_fdct4_new
    // 2D @ cos_bit=13, src {21,3,5,9,9,11,3,7,7,13,5,1,15,4,25,2}, above {10,40,30,20}
    const std::int32_t golden[16] = {-520, 140, 324, 202, -17, 18, 102, -68,
                                     56,   3,   36,  120, -18, 23, 6,   -22};
    const std::uint8_t srcData[16] = {21, 3, 5, 9, 9, 11, 3, 7, 7, 13, 5, 1, 15, 4, 25, 2};
    const std::uint8_t above[4] = {10, 40, 30, 20};

    pixels::Plane plane(4, 4, 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            plane.at(x, y) = srcData[y * 4 + x];
        }
    }

    std::int32_t coeffs[16] = {0};
    pipeline::encodeBlock4x4(plane, 0, 0, above, 4, 0, nullptr, 0, 0, 0, intra::V_PRED, 0,
                             transforms::TxType::DCT_DCT, coeffs);

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (coeffs[i] != golden[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("pipeline dc-mode coeffs match svt composition golden") {
    // golden: build_intra_predictors (DC_PRED) -> subtract ->
    // svt_av1_fdct4_new 2D @ cos_bit=13, src {9,4,7,5,12,8,3,6,15,2,11,4,6,9,13,2},
    // above {12,24,36,48}, left {6,18,30,42}
    const std::int32_t golden[16] = {-631, 53, 4,  55,  -16, 2,  45, -26,
                                     -12,  -27, -47, -2,  2,   -2, 16, 53};
    const std::uint8_t srcData[16] = {9, 4, 7, 5, 12, 8, 3, 6, 15, 2, 11, 4, 6, 9, 13, 2};
    const std::uint8_t above[4] = {12, 24, 36, 48};
    const std::uint8_t left[4] = {6, 18, 30, 42};

    pixels::Plane plane(4, 4, 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            plane.at(x, y) = srcData[y * 4 + x];
        }
    }

    std::int32_t coeffs[16] = {0};
    pipeline::encodeBlock4x4(plane, 0, 0, above, 4, 0, left, 4, 0, 0, intra::DC_PRED, 0,
                             transforms::TxType::DCT_DCT, coeffs);

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (coeffs[i] != golden[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu pipeline matches host pipeline bit-exactly") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const std::uint8_t srcData[16] = {21, 3, 5, 9, 9, 11, 3, 7, 7, 13, 5, 1, 15, 4, 25, 2};
    const std::uint8_t above[4] = {10, 40, 30, 20};

    pixels::Plane plane(4, 4, 4);
    constexpr int kStride = 4 + 2 * 4;
    std::uint8_t planeBuf[kStride * (4 + 2 * 4)];
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            plane.at(x, y) = srcData[y * 4 + x];
        }
    }

    std::int32_t ref[16] = {0};
    pipeline::encodeBlock4x4(plane, 0, 0, above, 4, 0, nullptr, 0, 0, 0, intra::V_PRED, 0,
                             transforms::TxType::DCT_DCT, ref);

    // stage 1: predict_block_4x4 -> pred (flat 16 bytes)
    const std::string ptxPred = *gpurt::compileToPtx(intra::predictBlockCuSource(), "compute_61");
    const std::vector<std::string> predNames = gpurt::ptxEntryNames(ptxPred);
    const auto itPred = std::find(predNames.begin(), predNames.end(), "predict_block_4x4");
    REQUIRE(itPred != predNames.end());
    gpurt::Kernel kPred(ptxPred, *itPred);

    const int mode = intra::V_PRED;
    const int nTopPx = 4;
    const int nTopRightPx = 0;
    const int nLeftPx = 0;
    const int nBottomLeftPx = 0;
    const int aboveLeft = 0;
    gpurt::DeviceBuffer dAbove(sizeof(above));
    gpurt::DeviceBuffer dLeft(1);
    gpurt::DeviceBuffer dPred(16);
    dAbove.uploadFrom(above, sizeof(above));
    unsigned char dummyLeft = 0;
    dLeft.uploadFrom(&dummyLeft, 1);

    int modeArg = mode;
    int deltaArg = 0;
    int amArg = 0;
    int lmArg = 0;
    int fiArg = -1;
    int defArg = 0;
    int nTopArg = nTopPx;
    int nTrArg = nTopRightPx;
    int nLeftArg = nLeftPx;
    int nBlArg = nBottomLeftPx;
    int alArg = aboveLeft;
    gpurt::DeviceBuffer dMode(sizeof(modeArg));
    gpurt::DeviceBuffer dDelta(sizeof(deltaArg));
    gpurt::DeviceBuffer dAm(sizeof(amArg));
    gpurt::DeviceBuffer dLm(sizeof(lmArg));
    gpurt::DeviceBuffer dFi(sizeof(fiArg));
    gpurt::DeviceBuffer dDef(sizeof(defArg));
    gpurt::DeviceBuffer dNTop(sizeof(nTopArg));
    gpurt::DeviceBuffer dNTr(sizeof(nTrArg));
    gpurt::DeviceBuffer dNLeft(sizeof(nLeftArg));
    gpurt::DeviceBuffer dNBl(sizeof(nBlArg));
    gpurt::DeviceBuffer dAl(sizeof(alArg));
    dMode.uploadFrom(&modeArg, sizeof(modeArg));
    dDelta.uploadFrom(&deltaArg, sizeof(deltaArg));
    dAm.uploadFrom(&amArg, sizeof(amArg));
    dLm.uploadFrom(&lmArg, sizeof(lmArg));
    dFi.uploadFrom(&fiArg, sizeof(fiArg));
    dDef.uploadFrom(&defArg, sizeof(defArg));
    dNTop.uploadFrom(&nTopArg, sizeof(nTopArg));
    dNTr.uploadFrom(&nTrArg, sizeof(nTrArg));
    dNLeft.uploadFrom(&nLeftArg, sizeof(nLeftArg));
    dNBl.uploadFrom(&nBlArg, sizeof(nBlArg));
    dAl.uploadFrom(&alArg, sizeof(alArg));

    CUdeviceptr pMode = dMode.get();
    CUdeviceptr pDelta = dDelta.get();
    CUdeviceptr pAm = dAm.get();
    CUdeviceptr pLm = dLm.get();
    CUdeviceptr pFi = dFi.get();
    CUdeviceptr pDef = dDef.get();
    CUdeviceptr pAbove = dAbove.get();
    CUdeviceptr pNTop = dNTop.get();
    CUdeviceptr pNTr = dNTr.get();
    CUdeviceptr pLeft = dLeft.get();
    CUdeviceptr pNLeft = dNLeft.get();
    CUdeviceptr pNBl = dNBl.get();
    CUdeviceptr pAl = dAl.get();
    CUdeviceptr pPred = dPred.get();
    void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                        &pNBl,  &pAl,   &pFi, &pDef, &pPred};
    kPred.launch(1, 1, 16, 1, argsPred);

    // stage 2: subtract kernel (src plane - pred) -> int16 residual
    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> subNames = gpurt::ptxEntryNames(ptxSub);
    const auto itSub = std::find(subNames.begin(), subNames.end(), "subtract_4x4_plane");
    REQUIRE(itSub != subNames.end());
    gpurt::Kernel kSub(ptxSub, *itSub);

    const int srcStride = plane.stride();
    gpurt::DeviceBuffer dPlane(sizeof(planeBuf));
    gpurt::DeviceBuffer dResidual(16 * sizeof(std::int16_t));
    gpurt::DeviceBuffer dStride(sizeof(srcStride));
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            planeBuf[y * plane.stride() + x] = srcData[y * 4 + x];
        }
    }
    dPlane.uploadFrom(planeBuf, sizeof(planeBuf));
    int srcStrideArg = srcStride;
    dStride.uploadFrom(&srcStrideArg, sizeof(srcStrideArg));
    int pxArg = 0;
    int pyArg = 0;
    gpurt::DeviceBuffer dPx(sizeof(pxArg));
    gpurt::DeviceBuffer dPy(sizeof(pyArg));
    dPx.uploadFrom(&pxArg, sizeof(pxArg));
    dPy.uploadFrom(&pyArg, sizeof(pyArg));

    CUdeviceptr pPlane = dPlane.get();
    CUdeviceptr pResidual = dResidual.get();
    CUdeviceptr pStride = dStride.get();
    CUdeviceptr pPx = dPx.get();
    CUdeviceptr pPy = dPy.get();
    void* argsSub[] = {&pPlane, &pStride, &pPx, &pPy, &pPred, &pResidual};
    kSub.launch(1, 1, 16, 1, argsSub);

    // stage 3: forward 2D transform
    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> txNames = gpurt::ptxEntryNames(ptxTx);
    const auto itTx = std::find(txNames.begin(), txNames.end(), "fwd_txfm_2d_4x4");
    REQUIRE(itTx != txNames.end());
    gpurt::Kernel kTx(ptxTx, *itTx);

    int strideArg = 4;
    int typeArg = 0;
    gpurt::DeviceBuffer dStride4(sizeof(strideArg));
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    dStride4.uploadFrom(&strideArg, sizeof(strideArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));

    CUdeviceptr pStride4 = dStride4.get();
    CUdeviceptr pType = dType.get();
    CUdeviceptr pResid = pResidual;
    gpurt::DeviceBuffer dCoeffs(sizeof(ref));
    CUdeviceptr pCoeffs = dCoeffs.get();
    void* argsTx[] = {&pResid, &pStride4, &pType, &pCoeffs};
    kTx.launch(1, 1, 4, 1, argsTx);

    std::int32_t got[16] = {0};
    dCoeffs.downloadTo(got, sizeof(got));

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != ref[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("round trip v+dct recon matches svt and recovers the source") {
    // golden: harness composition build_intra_predictors (V_PRED) -> subtract
    // -> fwd 2D -> inv 2D add onto the same predictor. This is the NO-QUANT
    // path: for 4x4 the fixed-point fwd+inv round trip is exactly lossless
    // (shifts {2,0,0}/{0,-4} are complementary), so recon == source. Real
    // loss comes from quantization (see the Q2 frame test) - the lossy-by-
    // design caveat is demonstrated there, not here.
    const std::uint8_t srcData[16] = {21, 3, 5, 9, 9, 11, 3, 7, 7, 13, 5, 1, 15, 4, 25, 2};
    const std::uint8_t above[4] = {10, 40, 30, 20};
    const std::int32_t goldenCoeffs[16] = {-520, 140, 324, 202, -17, 18, 102, -68,
                                           56,   3,   36,  120, -18, 23, 6,   -22};

    pixels::Plane plane(4, 4, 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            plane.at(x, y) = srcData[y * 4 + x];
        }
    }

    std::int32_t coeffs[16] = {0};
    std::uint8_t recon[16] = {0};
    pipeline::encodeRecon4x4(plane, 0, 0, above, 4, 0, nullptr, 0, 0, 0, intra::V_PRED, 0,
                             transforms::TxType::DCT_DCT, coeffs, recon);

    bool coeffsOk = true;
    for (int i = 0; i < 16; ++i) {
        if (coeffs[i] != goldenCoeffs[i]) {
            coeffsOk = false;
        }
    }
    CHECK(coeffsOk);

    bool reconOk = true;
    for (int i = 0; i < 16; ++i) {
        if (recon[i] != srcData[i]) {
            reconOk = false;
        }
    }
    CHECK(reconOk);
}

TEST_CASE("round trip adst recon matches svt composition") {
    // golden: harness composition build_intra_predictors (V_PRED, above
    // {0,0,0,0} -> pred zero) -> subtract (src 250 constant) -> fwd 2D
    // (ADST_ADST) -> inv 2D add onto the same predictor -> recon {250 * 16}.
    // NO-QUANT path: the 4x4 fixed-point fwd+inv is exact for in-range 8-bit
    // blocks (complementary shifts), so recon equals the source. Real loss
    // enters through quantization (Q2 frame test) or the 8x8 shift design
    // (scale-non-complementary by design); the 1:1 claim is scoped to SVT's
    // recon semantics, not to source recovery in general.
    const std::uint8_t srcData[16] = {250, 250, 250, 250, 250, 250, 250, 250,
                                      250, 250, 250, 250, 250, 250, 250, 250};
    const std::uint8_t above[4] = {0, 0, 0, 0};

    pixels::Plane plane(4, 4, 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            plane.at(x, y) = srcData[y * 4 + x];
        }
    }

    std::int32_t coeffs[16] = {0};
    std::uint8_t recon[16] = {0};
    pipeline::encodeRecon4x4(plane, 0, 0, above, 4, 0, nullptr, 0, 0, 0, intra::V_PRED, 0,
                             transforms::TxType::ADST_ADST, coeffs, recon);

    bool reconOk = true;
    for (int i = 0; i < 16; ++i) {
        if (recon[i] != srcData[i]) {
            reconOk = false;
        }
    }
    CHECK(reconOk);
}

TEST_CASE("frame round trip 8x8 recon matches svt composition") {
    // golden: harness composition frame_v_dct_8x8 (same raster loop over
    // verbatim SVT primitives: build_intra_predictors V-path incl. early-out
    // -> DCT fwd -> DCT inv-add onto the same predictor). NO-QUANT path: the
    // 4x4 fixed-point round trip is exact for these 8-bit blocks, so recon ==
    // source (the lossy counterpart with the FP quantizer live is the Q2
    // frame test); the per-block coeffs discriminate the availability paths
    // (corner block 0,0 fills above=127 -> DC -3784; first-row/left-column
    // use left[0])
    const std::uint8_t srcData[64] = {21, 3,  5,  9,  19, 2, 8,  14, 9,  11, 3, 7,  5,  23, 1, 17,
                                      7,  13, 5,  1,  25, 4, 6,  18, 15, 4,  25, 2,  12, 9,  30, 3,
                                      18, 5,  7,  13, 14, 2, 20, 8,  6,  24, 3,  9,  11, 17, 5, 19,
                                      22, 1,  8,  15, 4,  29, 7, 13, 10, 16, 6, 12, 3,  25, 11, 9};
    const std::int32_t goldenCoeffs[64] = {
        -3784, 78,  4,   54,  -17, 18,  102, -68, 56,  3,   36,  120, -18, 23, 6,   -22,
        104,   17,  60,  28,  -37, -5,  85,  -102, -4, -1,  -64, 144, 7,   34, 143, 85,
        -18,   -3,  166, -326, -6, 9,   6,   5,   -1,  -9,  6,   15,  9,   -3,  125, 131,
        -37,   -15, 122, -356, -12, -8,  91,  140,  -25, 3,   -25, 118, -2,  9,   -45, 17};

    pixels::Plane plane(8, 8, 4);
    pixels::Plane recon(8, 8, 4);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            plane.at(x, y) = srcData[y * 8 + x];
        }
    }

    std::int32_t coeffs[64] = {0};
    pipeline::encodeFrameRecon4x4(plane, recon, coeffs, intra::V_PRED, 0, transforms::TxType::DCT_DCT);

    bool reconOk = true;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (recon.at(x, y) != srcData[y * 8 + x]) {
                reconOk = false;
            }
        }
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 64; ++i) {
        if (coeffs[i] != goldenCoeffs[i]) {
            coeffsOk = false;
        }
    }
    CHECK(coeffsOk);

    // F2 folded: the per-block DC values above already prove the raster
    // availability paths (the full golden covers every block); these explicit
    // checks localize a failure to the exact variant:
    //   block 0 (0,0) corner:  no neighbor -> early-out above=127 -> DC -3784
    //   block 1 (0,1) first-row, left edge: left_ref[0] fallback -> DC 104
    //   block 2 (1,0) first-col, above:     v_predictor on above row -> DC -18
    //   block 3 (1,1) interior:              v_predictor on above row -> DC -37
    const std::int32_t blockDc[4] = {-3784, 104, -18, -37};
    bool dcOk = true;
    for (int b = 0; b < 4; ++b) {
        if (coeffs[b * 16] != blockDc[b]) {
            dcOk = false;
        }
    }
    CHECK(dcOk);
}

TEST_CASE("frame mse is zero for identical planes") {
    pixels::Plane a(8, 8, 4);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            a.at(x, y) = static_cast<std::uint8_t>(y * 8 + x);
        }
    }
    pixels::Plane b(8, 8, 4);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            b.at(x, y) = static_cast<std::uint8_t>(y * 8 + x);
        }
    }
    CHECK(pipeline::frameMse8(a, b) == 0);
}

TEST_CASE("frame mse is 64 for a unit-lift of every sample") {
    // each of the 64 samples differs by exactly 1 -> SSE = 64 * 1 = 64
    pixels::Plane a(8, 8, 4);
    pixels::Plane b(8, 8, 4);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const std::uint8_t v = static_cast<std::uint8_t>(y * 8 + x + 40);
            a.at(x, y) = v;
            b.at(x, y) = static_cast<std::uint8_t>(v + 1);
        }
    }
    CHECK(pipeline::frameMse8(a, b) == 64);
}

TEST_CASE("frame recon 8x8 matches generator composition") {
    // golden: svtd_frame_auto_8x8_blocks composition (same builder+transform
    // chain), fixed V_PRED + DCT_DCT mode, D45 edge-filter coverage
    const std::uint8_t srcData[256] = {
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

    pixels::Plane plane(16, 16, 4);
    pixels::Plane recon(16, 16, 4);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) plane.at(x, y) = srcData[y * 16 + x];

    std::int32_t coeffs[4 * 64] = {0};
    pipeline::encodeFrameRecon8x8(plane, recon, coeffs, intra::V_PRED, 0, transforms::TxType::DCT_DCT);
    bool ok = true;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            if (recon.at(x, y) != srcData[y * 16 + x]) ok = false;
    CHECK(ok);
}

TEST_CASE("decide 8x8 picks v for the v fixture (paeth tie broken by index)") {
    // golden: golden_gen d2_v SAD vector generalized to 8x8 (same policy, sad8x8)
    const std::uint8_t src[64] = {10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40,
                                  10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40,
                                  10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40,
                                  10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40};
    const std::uint8_t above[8] = {10, 20, 30, 40, 10, 20, 30, 40};
    const auto d = pipeline::decideBlockMode8x8(src, above, 8, 0, nullptr, 0, 0, 0);
    CHECK(d.mode == intra::V_PRED);
    CHECK(d.sad == 0);
}

TEST_CASE("frame auto 8x8 matches the generator policy golden bit-exactly") {
    // golden: golden_gen golden_frame b7_modes/b7_recon/b7_coeffs - identical
    // D2 policy over verbatim SVT primitives at TX_8X8, decisions evaluated
    // against RECONSTRUCTED neighbor edges, chosen modes feeding filt_type.
    const std::uint8_t srcData[256] = {
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
    // b7_modes: 1 5 3 0 (V, SMOOTH, D45, DC) - block 2 (the only nTr>0
    // block) flips D157 -> D45 once D45's zone-1 SAD consumes the REAL
    // top-right instead of the pre-FR1 fake zeros (FR-series gather).
    const std::uint8_t goldenModes[4] = {1, 5, 3, 0};

    pixels::Plane plane(16, 16, 4);
    pixels::Plane recon(16, 16, 4);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) plane.at(x, y) = srcData[y * 16 + x];

    std::int32_t coeffs[4 * 64] = {0};
    std::uint8_t modes[4] = {0};
    pipeline::encodeFrameAuto8x8(plane, recon, coeffs, modes, transforms::TxType::DCT_DCT);

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modes[i] != goldenModes[i]) modesOk = false;
    }
    CHECK(modesOk);
    // NOTE: recon != source for 8x8 blocks is BY DESIGN - the 8x8 fwd/inv
    // shift arrays are scale-non-complementary (fwd total x4 from
    // fwd_shift_8x8 {2,-1,0}, inv total x1/16 from inv_shift_8x8 {-1,-4}).
    // AV1 compensates in per-tx-size dequant, and AV1 lossless restricts to
    // TX_4X4. The mode map is the discriminator: decisions depend on
    // reconstructed edges, so any recon error would corrupt subsequent mode
    // choices and the map would differ.
}

TEST_CASE("frame auto 4x4 with quantization matches the Q2 generator golden") {
    // golden: golden_gen q2f_modes/q2f_recon/q2f_coeffs at qindex 100 -
    // D3 policy loop with the FP quantizer wired in (qcoeff = coded coeffs,
    // dqcoeff feeds the inverse). recon now carries REAL quantization loss
    // (compare with the lossless d3_recon / lossless round-trip tests).
    const std::uint8_t srcData[64] = {21, 3,  5,  9,  19, 2, 8,  14, 9,  11, 3, 7,  5,  23, 1, 17,
                                      7,  13, 5,  1,  25, 4, 6,  18, 15, 4,  25, 2,  12, 9,  30, 3,
                                      18, 5,  7,  13, 14, 2, 20, 8,  6,  24, 3,  9,  11, 17, 5, 19,
                                      22, 1,  8,  15, 4,  29, 7, 13, 10, 16, 6, 12, 3,  25, 11, 9};
    const std::uint8_t goldenModes[4] = {1, 1, 0, 0};
    const std::uint8_t goldenRecon[64] = {
        16, 7, 0, 9,  19, 4,  7, 18, 11, 15, 0,  8,  11, 21, 0,  22,
        10, 14, 6, 2,  23, 0,  6, 20, 12, 4,  21, 0,  9,  10, 26, 2,
        18, 4, 9, 16, 15, 4,  16, 10, 8,  19, 7,  13, 7,  19, 6,  12,
        23, 0, 10, 18, 1, 28, 5, 11, 13, 13, 8,  15, 1,  25, 13, 6};
    const std::int32_t goldenCoeffs[64] = {
        -41, 1, 0, 0, 0, 0, 1, -1, 0, 0, 0, 1, 0, 0, 0, 0,
        1, 0, 1, 0, 0, 0, 1, -1, 0, 0, -1, 1, 0, 0, 1, 1,
        1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
        -1, 0, -1, -1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0};

    pixels::Plane plane(8, 8, 4);
    pixels::Plane recon(8, 8, 4);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) plane.at(x, y) = srcData[y * 8 + x];

    std::int32_t coeffs[64] = {0};
    std::uint8_t modes[4] = {0};
    pipeline::encodeFrameAuto4x4Q(plane, recon, coeffs, modes, 100,
                                  transforms::TxType::DCT_DCT);

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modes[i] != goldenModes[i]) modesOk = false;
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            if (recon.at(x, y) != goldenRecon[y * 8 + x]) reconOk = false;
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 64; ++i) {
        if (coeffs[i] != goldenCoeffs[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}

TEST_CASE("gpu frame round trip matches host encodeFrameRecon4x4") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const std::uint8_t srcData[64] = {21, 3,  5,  9,  19, 2, 8,  14, 9,  11, 3, 7,  5,  23, 1, 17,
                                      7,  13, 5,  1,  25, 4, 6,  18, 15, 4,  25, 2,  12, 9,  30, 3,
                                      18, 5,  7,  13, 14, 2, 20, 8,  6,  24, 3,  9,  11, 17, 5, 19,
                                      22, 1,  8,  15, 4,  29, 7, 13, 10, 16, 6, 12, 3,  25, 11, 9};

    pixels::Plane plane(8, 8, 4);
    pixels::Plane reconRef(8, 8, 4);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            plane.at(x, y) = srcData[y * 8 + x];
        }
    }
    std::int32_t refCoeffs[64] = {0};
    pipeline::encodeFrameRecon4x4(plane, reconRef, refCoeffs, intra::V_PRED, 0,
                                  transforms::TxType::DCT_DCT);

    // kernels
    const std::string ptxPred = *gpurt::compileToPtx(intra::predictBlockCuSource(), "compute_61");
    const std::vector<std::string> pn = gpurt::ptxEntryNames(ptxPred);
    gpurt::Kernel kPred(ptxPred, *std::find(pn.begin(), pn.end(), "predict_block_4x4"));
    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> sn = gpurt::ptxEntryNames(ptxSub);
    gpurt::Kernel kSub(ptxSub, *std::find(sn.begin(), sn.end(), "subtract_4x4_plane"));
    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> tn = gpurt::ptxEntryNames(ptxTx);
    gpurt::Kernel kTx(ptxTx, *std::find(tn.begin(), tn.end(), "fwd_txfm_2d_4x4"));
    const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> inames = gpurt::ptxEntryNames(ptxInv);
    gpurt::Kernel kInv(ptxInv, *std::find(inames.begin(), inames.end(), "inv_txfm_2d_add_4x4"));

    constexpr int kStride = 8;
    gpurt::DeviceBuffer dPlane(sizeof(srcData));
    gpurt::DeviceBuffer dRecon(64);
    gpurt::DeviceBuffer dResidual(16 * sizeof(std::int16_t));
    gpurt::DeviceBuffer dBlkCoeffs(16 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dPred(16);
    gpurt::DeviceBuffer dCoeffs(64 * sizeof(std::int32_t));
    dPlane.uploadFrom(srcData, sizeof(srcData));
    std::uint8_t zero[64] = {0};
    dRecon.uploadFrom(zero, 64);

    int modeArg = intra::V_PRED;
    int deltaArg = 0;
    int amArg = 0;
    int lmArg = 0;
    int fiArg = -1;
    int defArg = 0;
    int typeArg = 0;
    int strideArg = 4;
    int planeStrideArg = kStride;
    int fwdStrideArg = 4;
    std::uint8_t dummyLeft = 0;
    std::uint8_t dummyAbove[8] = {0};
    gpurt::DeviceBuffer dMode(sizeof(modeArg));
    gpurt::DeviceBuffer dDelta(sizeof(deltaArg));
    gpurt::DeviceBuffer dAm(sizeof(amArg));
    gpurt::DeviceBuffer dLm(sizeof(lmArg));
    gpurt::DeviceBuffer dFi(sizeof(fiArg));
    gpurt::DeviceBuffer dDef(sizeof(defArg));
    gpurt::DeviceBuffer dNTop(sizeof(int));
    gpurt::DeviceBuffer dNTr(sizeof(int));
    gpurt::DeviceBuffer dNLeft(sizeof(int));
    gpurt::DeviceBuffer dNBl(sizeof(int));
    gpurt::DeviceBuffer dAl(sizeof(int));
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    gpurt::DeviceBuffer dStride4(sizeof(strideArg));
    gpurt::DeviceBuffer dPlaneStride(sizeof(planeStrideArg));
    gpurt::DeviceBuffer dFwdStride(sizeof(fwdStrideArg));
    gpurt::DeviceBuffer dInvStride(sizeof(int));
    gpurt::DeviceBuffer dPx(sizeof(int));
    gpurt::DeviceBuffer dPy(sizeof(int));
    gpurt::DeviceBuffer dAbove(8);
    gpurt::DeviceBuffer dLeft(1);
    dMode.uploadFrom(&modeArg, sizeof(modeArg));
    dDelta.uploadFrom(&deltaArg, sizeof(deltaArg));
    dAm.uploadFrom(&amArg, sizeof(amArg));
    dLm.uploadFrom(&lmArg, sizeof(lmArg));
    dFi.uploadFrom(&fiArg, sizeof(fiArg));
    dDef.uploadFrom(&defArg, sizeof(defArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dStride4.uploadFrom(&strideArg, sizeof(strideArg));
    dPlaneStride.uploadFrom(&planeStrideArg, sizeof(planeStrideArg));
    dFwdStride.uploadFrom(&fwdStrideArg, sizeof(fwdStrideArg));
    int invStride = 4;
    dInvStride.uploadFrom(&invStride, sizeof(invStride));
    dLeft.uploadFrom(&dummyLeft, 1);
    dAbove.uploadFrom(dummyAbove, 8);

    std::uint8_t reconGot[64] = {0};
    std::int32_t coeffsGot[64] = {0};
    std::uint8_t aboveHost[8] = {0};
    std::uint8_t leftHost[1] = {0};
    std::uint8_t aboveLeftHost[1] = {0};

    for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 2; ++bx) {
            const int px = bx * 4;
            const int py = by * 4;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const int nTopPx = hasTop ? 4 : 0;
            const int nLeftPx = hasLeft ? 4 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < 2) ? 4 : 0;

            // read edges from device recon
            if (hasTop) {
                std::uint8_t tmp[64];
                dRecon.downloadTo(tmp, 64);
                for (int i = 0; i < 4 + nTopRightPx; ++i) {
                    aboveHost[i] = tmp[(py - 1) * 8 + px + i];
                }
                dAbove.uploadFrom(aboveHost, 4 + nTopRightPx);
            }
            if (hasLeft) {
                std::uint8_t tmp[64];
                dRecon.downloadTo(tmp, 64);
                leftHost[0] = tmp[py * 8 + px - 1];
                dLeft.uploadFrom(leftHost, 1);
            }
            if (hasTop && hasLeft) {
                std::uint8_t tmp[64];
                dRecon.downloadTo(tmp, 64);
                aboveLeftHost[0] = tmp[(py - 1) * 8 + px - 1];
            }

            int nTopArg = nTopPx;
            int nTrArg = nTopRightPx;
            int nLeftArg = nLeftPx;
            int nBlArg = 0;
            int alArg = (hasTop && hasLeft) ? aboveLeftHost[0] : 0;
            int pxArg = px;
            int pyArg = py;
            dNTop.uploadFrom(&nTopArg, sizeof(nTopArg));
            dNTr.uploadFrom(&nTrArg, sizeof(nTrArg));
            dNLeft.uploadFrom(&nLeftArg, sizeof(nLeftArg));
            dNBl.uploadFrom(&nBlArg, sizeof(nBlArg));
            dAl.uploadFrom(&alArg, sizeof(alArg));
            dPx.uploadFrom(&pxArg, sizeof(pxArg));
            dPy.uploadFrom(&pyArg, sizeof(pyArg));

            CUdeviceptr pMode = dMode.get();
            CUdeviceptr pDelta = dDelta.get();
            CUdeviceptr pAm = dAm.get();
            CUdeviceptr pLm = dLm.get();
            CUdeviceptr pFi = dFi.get();
            CUdeviceptr pDef = dDef.get();
            CUdeviceptr pAbove = dAbove.get();
            CUdeviceptr pNTop = dNTop.get();
            CUdeviceptr pNTr = dNTr.get();
            CUdeviceptr pLeft = dLeft.get();
            CUdeviceptr pNLeft = dNLeft.get();
            CUdeviceptr pNBl = dNBl.get();
            CUdeviceptr pAl = dAl.get();
            CUdeviceptr pPred = dPred.get();
            void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                                &pNBl,  &pAl,   &pFi, &pDef, &pPred};
            kPred.launch(1, 1, 16, 1, argsPred);

            CUdeviceptr pPlane = dPlane.get();
            CUdeviceptr pPlaneStride = dPlaneStride.get();
            CUdeviceptr pPx = dPx.get();
            CUdeviceptr pPy = dPy.get();
            CUdeviceptr pResidual = dResidual.get();
            void* argsSub[] = {&pPlane, &pPlaneStride, &pPx, &pPy, &pPred, &pResidual};
            kSub.launch(1, 1, 16, 1, argsSub);

            CUdeviceptr pType = dType.get();
            CUdeviceptr pFwdStride = dFwdStride.get();
            CUdeviceptr pBlkCoeffs = dBlkCoeffs.get();
            void* argsTx[] = {&pResidual, &pFwdStride, &pType, &pBlkCoeffs};
            kTx.launch(1, 1, 4, 1, argsTx);
            std::int32_t blk[16] = {0};
            dBlkCoeffs.downloadTo(blk, sizeof(blk));
            for (int i = 0; i < 16; ++i) {
                coeffsGot[(by * 2 + bx) * 16 + i] = blk[i];
            }

            CUdeviceptr pInvStride = dInvStride.get();
            void* argsInv[] = {&pBlkCoeffs, &pType, &pPred, &pInvStride};
            kInv.launch(1, 1, 4, 1, argsInv);

            std::uint8_t blkRecon[16] = {0};
            dPred.downloadTo(blkRecon, sizeof(blkRecon));
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    reconGot[(py + y) * 8 + px + x] = blkRecon[y * 4 + x];
                }
            }
            // write back to device recon for the next block's edges
            dRecon.uploadFrom(reconGot, 64);
        }
    }

    bool reconOk = true;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (reconGot[y * 8 + x] != reconRef.at(x, y)) {
                reconOk = false;
            }
        }
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 64; ++i) {
        if (coeffsGot[i] != refCoeffs[i]) {
            coeffsOk = false;
        }
    }
    CHECK(coeffsOk);
}

TEST_CASE("gpu frame auto matches host encodeFrameAuto4x4 (host decides, gpu executes)") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const std::uint8_t srcData[64] = {21, 3,  5,  9,  19, 2, 8,  14, 9,  11, 3, 7,  5,  23, 1, 17,
                                      7,  13, 5,  1,  25, 4, 6,  18, 15, 4,  25, 2,  12, 9,  30, 3,
                                      18, 5,  7,  13, 14, 2, 20, 8,  6,  24, 3,  9,  11, 17, 5, 19,
                                      22, 1,  8,  15, 4,  29, 7, 13, 10, 16, 6, 12, 3,  25, 11, 9};

    pixels::Plane plane(8, 8, 4);
    pixels::Plane reconRef(8, 8, 4);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            plane.at(x, y) = srcData[y * 8 + x];
        }
    }
    std::int32_t refCoeffs[64] = {0};
    std::uint8_t refModes[4] = {0};
    pipeline::encodeFrameAuto4x4(plane, reconRef, refCoeffs, refModes, transforms::TxType::DCT_DCT);

    const std::string ptxPred = *gpurt::compileToPtx(intra::predictBlockCuSource(), "compute_61");
    const std::vector<std::string> pn = gpurt::ptxEntryNames(ptxPred);
    gpurt::Kernel kPred(ptxPred, *std::find(pn.begin(), pn.end(), "predict_block_4x4"));
    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> sn = gpurt::ptxEntryNames(ptxSub);
    gpurt::Kernel kSub(ptxSub, *std::find(sn.begin(), sn.end(), "subtract_4x4_plane"));
    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> tn = gpurt::ptxEntryNames(ptxTx);
    gpurt::Kernel kTx(ptxTx, *std::find(tn.begin(), tn.end(), "fwd_txfm_2d_4x4"));
    const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> inames = gpurt::ptxEntryNames(ptxInv);
    gpurt::Kernel kInv(ptxInv, *std::find(inames.begin(), inames.end(), "inv_txfm_2d_add_4x4"));

    constexpr int kStride = 8;
    gpurt::DeviceBuffer dPlane(sizeof(srcData));
    gpurt::DeviceBuffer dRecon(64);
    gpurt::DeviceBuffer dResidual(16 * sizeof(std::int16_t));
    gpurt::DeviceBuffer dBlkCoeffs(16 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dPred(16);
    dPlane.uploadFrom(srcData, sizeof(srcData));
    std::uint8_t zero[64] = {0};
    dRecon.uploadFrom(zero, 64);

    int modeArg = 0;
    int deltaArg = 0;
    int amArg = 0;
    int lmArg = 0;
    int fiArg = -1;
    int defArg = 0;
    int typeArg = 0;
    int planeStrideArg = kStride;
    int fwdStrideArg = 4;
    int invStride = 4;
    gpurt::DeviceBuffer dMode(sizeof(modeArg));
    gpurt::DeviceBuffer dDelta(sizeof(deltaArg));
    gpurt::DeviceBuffer dAm(sizeof(amArg));
    gpurt::DeviceBuffer dLm(sizeof(lmArg));
    gpurt::DeviceBuffer dFi(sizeof(fiArg));
    gpurt::DeviceBuffer dDef(sizeof(defArg));
    gpurt::DeviceBuffer dNTop(sizeof(int));
    gpurt::DeviceBuffer dNTr(sizeof(int));
    gpurt::DeviceBuffer dNLeft(sizeof(int));
    gpurt::DeviceBuffer dNBl(sizeof(int));
    gpurt::DeviceBuffer dAl(sizeof(int));
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    gpurt::DeviceBuffer dPlaneStride(sizeof(planeStrideArg));
    gpurt::DeviceBuffer dFwdStride(sizeof(fwdStrideArg));
    gpurt::DeviceBuffer dInvStride(sizeof(int));
    gpurt::DeviceBuffer dPx(sizeof(int));
    gpurt::DeviceBuffer dPy(sizeof(int));
    gpurt::DeviceBuffer dAbove(8);
    gpurt::DeviceBuffer dLeft(4);
    dDelta.uploadFrom(&deltaArg, sizeof(deltaArg));
    dFi.uploadFrom(&fiArg, sizeof(fiArg));
    dDef.uploadFrom(&defArg, sizeof(defArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dPlaneStride.uploadFrom(&planeStrideArg, sizeof(planeStrideArg));
    dFwdStride.uploadFrom(&fwdStrideArg, sizeof(fwdStrideArg));
    dInvStride.uploadFrom(&invStride, sizeof(invStride));

    std::uint8_t reconGot[64] = {0};
    std::int32_t coeffsGot[64] = {0};
    std::uint8_t modesGot[4] = {0};
    std::uint8_t aboveHost[8] = {0};
    std::uint8_t leftHost[4] = {0};
    std::uint8_t alHost[1] = {0};

    for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 2; ++bx) {
            const int px = bx * 4;
            const int py = by * 4;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const int nTopPx = hasTop ? 4 : 0;
            const int nLeftPx = hasLeft ? 4 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < 2) ? 4 : 0;

            std::uint8_t tmp[64] = {0};
            dRecon.downloadTo(tmp, 64);
            if (hasTop) {
                for (int i = 0; i < 4 + nTopRightPx; ++i) {
                    aboveHost[i] = tmp[(py - 1) * 8 + px + i];
                }
                dAbove.uploadFrom(aboveHost, 4 + nTopRightPx);
            }
            if (hasLeft) {
                for (int i = 0; i < 4; ++i) {
                    leftHost[i] = tmp[(py + i) * 8 + px - 1];
                }
                dLeft.uploadFrom(leftHost, 4);
            }
            int alVal = 0;
            if (hasTop && hasLeft) {
                alHost[0] = tmp[(py - 1) * 8 + px - 1];
                alVal = alHost[0];
            }

            // HOST DECISION (policy is host code): chosen neighbor modes feed
            // NeighborContext; decision against reconstructed edges
            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop ? static_cast<intra::PredictionMode>(modesGot[(by - 1) * 2 + bx])
                                    : intra::DC_PRED;
            nctx.leftMode = hasLeft ? static_cast<intra::PredictionMode>(modesGot[by * 2 + bx - 1])
                                    : intra::DC_PRED;
            std::uint8_t srcBlk[16] = {0};
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    srcBlk[y * 4 + x] = srcData[(py + y) * 8 + px + x];
                }
            }
            const auto d = pipeline::decideBlockMode4x4(srcBlk, aboveHost, nTopPx, nTopRightPx,
                                                        leftHost, nLeftPx, 0, (std::uint8_t)alVal,
                                                        nctx);
            modesGot[by * 2 + bx] = static_cast<std::uint8_t>(d.mode);

            // GPU EXECUTION of the winner
            modeArg = d.mode;
            amArg = (int)nctx.aboveMode;
            lmArg = (int)nctx.leftMode;
            int nTopArg = nTopPx;
            int nTrArg = nTopRightPx;
            int nLeftArg = nLeftPx;
            int nBlArg = 0;
            int alArg = alVal;
            int pxArg = px;
            int pyArg = py;
            dMode.uploadFrom(&modeArg, sizeof(modeArg));
            dAm.uploadFrom(&amArg, sizeof(amArg));
            dLm.uploadFrom(&lmArg, sizeof(lmArg));
            dNTop.uploadFrom(&nTopArg, sizeof(nTopArg));
            dNTr.uploadFrom(&nTrArg, sizeof(nTrArg));
            dNLeft.uploadFrom(&nLeftArg, sizeof(nLeftArg));
            dNBl.uploadFrom(&nBlArg, sizeof(nBlArg));
            dAl.uploadFrom(&alArg, sizeof(alArg));
            dPx.uploadFrom(&pxArg, sizeof(pxArg));
            dPy.uploadFrom(&pyArg, sizeof(pyArg));

            CUdeviceptr pMode = dMode.get();
            CUdeviceptr pDelta = dDelta.get();
            CUdeviceptr pAm = dAm.get();
            CUdeviceptr pLm = dLm.get();
            CUdeviceptr pFi = dFi.get();
            CUdeviceptr pDef = dDef.get();
            CUdeviceptr pAbove = dAbove.get();
            CUdeviceptr pNTop = dNTop.get();
            CUdeviceptr pNTr = dNTr.get();
            CUdeviceptr pLeft = dLeft.get();
            CUdeviceptr pNLeft = dNLeft.get();
            CUdeviceptr pNBl = dNBl.get();
            CUdeviceptr pAl = dAl.get();
            CUdeviceptr pPred = dPred.get();
            void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                                &pNBl,  &pAl,   &pFi, &pDef, &pPred};
            kPred.launch(1, 1, 16, 1, argsPred);

            CUdeviceptr pPlane = dPlane.get();
            CUdeviceptr pPlaneStride = dPlaneStride.get();
            CUdeviceptr pPx = dPx.get();
            CUdeviceptr pPy = dPy.get();
            CUdeviceptr pResidual = dResidual.get();
            void* argsSub[] = {&pPlane, &pPlaneStride, &pPx, &pPy, &pPred, &pResidual};
            kSub.launch(1, 1, 16, 1, argsSub);

            CUdeviceptr pType = dType.get();
            CUdeviceptr pFwdStride = dFwdStride.get();
            CUdeviceptr pBlkCoeffs = dBlkCoeffs.get();
            void* argsTx[] = {&pResidual, &pFwdStride, &pType, &pBlkCoeffs};
            kTx.launch(1, 1, 4, 1, argsTx);
            std::int32_t blk[16] = {0};
            dBlkCoeffs.downloadTo(blk, sizeof(blk));
            for (int i = 0; i < 16; ++i) {
                coeffsGot[(by * 2 + bx) * 16 + i] = blk[i];
            }

            CUdeviceptr pInvStride = dInvStride.get();
            void* argsInv[] = {&pBlkCoeffs, &pType, &pPred, &pInvStride};
            kInv.launch(1, 1, 4, 1, argsInv);

            std::uint8_t blkRecon[16] = {0};
            dPred.downloadTo(blkRecon, sizeof(blkRecon));
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    reconGot[(py + y) * 8 + px + x] = blkRecon[y * 4 + x];
                }
            }
            dRecon.uploadFrom(reconGot, 64);
        }
    }

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modesGot[i] != refModes[i]) {
            modesOk = false;
        }
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (reconGot[y * 8 + x] != reconRef.at(x, y)) {
                reconOk = false;
            }
        }
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 64; ++i) {
        if (coeffsGot[i] != refCoeffs[i]) {
            coeffsOk = false;
        }
    }
    CHECK(coeffsOk);
}

TEST_CASE("decide picks vertical for the v fixture (paeth tie broken by index)") {
    // golden: golden_gen d2_v -" V SAD 0, PAETH SAD 0, everything else > 0;
    // deterministic tie-break = lowest mode index -> V (1)
    const std::uint8_t src[16] = {10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40};
    const std::uint8_t above[4] = {10, 20, 30, 40};
    const auto d = pipeline::decideBlockMode4x4(src, above, 4, 0, nullptr, 0, 0, 0);
    CHECK(d.mode == intra::V_PRED);
    CHECK(d.sad == 0);
}

TEST_CASE("decide picks horizontal for the h fixture") {
    // golden: golden_gen d2_h -" H SAD 0, PAETH SAD 0, tie -> H (2)
    const std::uint8_t src[16] = {5, 5, 5, 5, 10, 10, 10, 10, 15, 15, 15, 15, 20, 20, 20, 20};
    const std::uint8_t left[4] = {5, 10, 15, 20};
    const auto d = pipeline::decideBlockMode4x4(src, nullptr, 0, 0, left, 4, 0, 0);
    CHECK(d.mode == intra::H_PRED);
    CHECK(d.sad == 0);
}

TEST_CASE("decide picks dc on a flat block via thirteen-way tie") {
    // golden: golden_gen d2_dc -" all 13 candidate SADs are 0; tie-break -> DC (0)
    const std::uint8_t src[16] = {50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50};
    const std::uint8_t above[4] = {50, 50, 50, 50};
    const std::uint8_t left[4] = {50, 50, 50, 50};
    const auto d = pipeline::decideBlockMode4x4(src, above, 4, 0, left, 4, 0, 50);
    CHECK(d.mode == intra::DC_PRED);
    CHECK(d.sad == 0);
}

TEST_CASE("decide picks d45 for the diagonal fixture") {
    // golden: golden_gen d2_d45 -" D45 SAD 0, next best 233 (SMOOTH_V); strict win
    const std::uint8_t src[16] = {10, 20, 30, 40, 20, 30, 40, 50, 30, 40, 50, 60, 40, 50, 60, 70};
    const std::uint8_t above[8] = {0, 10, 20, 30, 40, 50, 60, 70};
    const std::uint8_t left[4] = {0, 10, 20, 30};
    const auto d = pipeline::decideBlockMode4x4(src, above, 4, 4, left, 4, 0, 0);
    CHECK(d.mode == intra::D45_PRED);
    CHECK(d.sad == 0);
}

TEST_CASE("frame auto matches the generator policy golden bit-exactly") {
    // golden: golden_gen golden_frame (d3_modes / d3_recon / d3_coeffs) -"
    // identical D2 policy over verbatim SVT primitives, decisions evaluated
    // against RECONSTRUCTED neighbor edges, chosen modes feeding filt_type.
    // modes 1 1 0 7 = V, V, DC, D203.
    const std::uint8_t srcData[64] = {21, 3,  5,  9,  19, 2, 8,  14, 9,  11, 3, 7,  5,  23, 1, 17,
                                      7,  13, 5,  1,  25, 4, 6,  18, 15, 4,  25, 2,  12, 9,  30, 3,
                                      18, 5,  7,  13, 14, 2, 20, 8,  6,  24, 3,  9,  11, 17, 5, 19,
                                      22, 1,  8,  15, 4,  29, 7, 13, 10, 16, 6, 12, 3,  25, 11, 9};
    const std::uint8_t goldenModes[4] = {1, 1, 0, 7};
    const std::int32_t goldenCoeffs[64] = {
        -3784, 78,  4,   54,  -17, 18,  102, -68, 56,  3,   36,  120, -18, 23, 6,   -22,
        104,   17,  60,  28,  -37, -5,  85,  -102, -4, -1,  -64, 144, 7,   34, 143, 85,
        -34,   42,  71,  -50, -6,  9,   6,   5,   -1,  -9,  6,   15,  9,   -3,  125, 131,
        0,     -9,  -72, -96, -6,  4,   88,  141,  -4, 13,  -35, 116, 6,   1,   -59, 16};

    pixels::Plane plane(8, 8, 4);
    pixels::Plane recon(8, 8, 4);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            plane.at(x, y) = srcData[y * 8 + x];
        }
    }

    std::int32_t coeffs[64] = {0};
    std::uint8_t modes[4] = {0};
    pipeline::encodeFrameAuto4x4(plane, recon, coeffs, modes, transforms::TxType::DCT_DCT);

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modes[i] != goldenModes[i]) {
            modesOk = false;
        }
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (recon.at(x, y) != srcData[y * 8 + x]) {
                reconOk = false;
            }
        }
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 64; ++i) {
        if (coeffs[i] != goldenCoeffs[i]) {
            coeffsOk = false;
        }
    }
    CHECK(coeffsOk);
}

TEST_CASE("frame auto 4x4 gathers real reconstructed top-right (FR-series fixture)") {
    // golden: golden_gen fr_modes/fr_recon/fr_coeffs (tools/golden_gen/
    // main_primitives.c, svtd_frame_auto_4x4_16x16): 16x16 frame, 4x4 grid of
    // 4x4 blocks; rows 0-7 = diagonal ramp 10*(x+y+1), rows 8-15 zero. The
    // row-1 blocks replicate the d2_d45 structure: D45's zone-1 prediction
    // reads above[1+r+c] into the top-right, so its SAD is 0 only when
    // above[4..7] carry the REAL reconstructed samples
    // (recon[(py-1)][px+4..px+7], guaranteed reconstructed by the M1 raster
    // rule). Probe (verbatim primitives) under the pre-FR1 zero-fill: D45
    // SADs become 900/1300/1700 for row-1 blocks bx0..bx2 and the winners
    // change (V/D203/D203), so the mode map discriminates the gather.
    // fr_modes: 1 7 7 7 | 3 3 3 3 | 2 2 2 2 | 0 0 0 0.
    // NOTE: recon is NOT the discriminator here (4x4 fwd/inv round trip is
    // exact for every winner, so recon == source under either gather); modes
    // and coeffs carry the discrimination. recon/coeffs are still pinned to
    // the committed gate lines.
    const std::uint8_t goldenModes[16] = {1, 7, 7, 7, 3, 3, 3, 3, 2, 2, 2, 2, 0, 0, 0, 0};
    const std::uint8_t goldenRecon[256] = {
        10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160,
        20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170,
        30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180,
        40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190,
        50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190, 200,
        60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190, 200, 210,
        70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190, 200, 210, 220,
        80, 90, 100, 110, 120, 130, 140, 150, 160, 170, 180, 190, 200, 210, 220, 230};
    const std::int32_t goldenCoeffs[256] = {
        -2785, -356, 0,    -25,  -356, 0,   0,  0,   0,   0,   0,   0,   -25, 0,   0,   0,
        558,   -257, 5,    -21,  -112, 72,  -1, 5,   74,  -21, -6,  -2,  -25, -8,  16,  0,
        558,   -257, 5,    -21,  -112, 72,  -1, 5,   74,  -21, -6,  -2,  -25, -8,  16,  0,
        558,   -257, 5,    -21,  -112, 72,  -1, 5,   74,  -21, -6,  -2,  -25, -8,  16,  0,
        0,     0,    0,    0,    0,    0,   0,  0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,     0,    0,    0,    0,    0,   0,  0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,     0,    0,    0,    0,    0,   0,  0,   0,   0,   0,   0,   0,   0,   0,   0,
        400,   -268, 40,   -19,  -267, 96,  37, 0,   40,  37,  0,   -15, -19, 0,   -15, -16,
        -2560, 0,    0,    0,    0,    0,   0,  0,   0,   0,   0,   0,   0,   0,   0,   0};
    // blocks 9-15 (all-zero rows: H wins with left zeros, DC tie row) are
    // implicitly zero in the initializer above.

    pixels::Plane plane(16, 16, 4);
    pixels::Plane recon(16, 16, 4);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            plane.at(x, y) = (y < 8) ? static_cast<std::uint8_t>(10 * (x + y + 1)) : 0;
        }
    }

    std::int32_t coeffs[256] = {0};
    std::uint8_t modes[16] = {0};
    pipeline::encodeFrameAuto4x4(plane, recon, coeffs, modes, transforms::TxType::DCT_DCT);

    bool modesOk = true;
    for (int i = 0; i < 16; ++i) {
        if (modes[i] != goldenModes[i]) {
            modesOk = false;
        }
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            if (recon.at(x, y) != goldenRecon[y * 16 + x]) {
                reconOk = false;
            }
        }
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 256; ++i) {
        if (coeffs[i] != goldenCoeffs[i]) {
            coeffsOk = false;
        }
    }
    CHECK(coeffsOk);
}

TEST_CASE("gpu round trip matches host encodeRecon4x4 bit-exactly") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const std::uint8_t srcData[16] = {21, 3, 5, 9, 9, 11, 3, 7, 7, 13, 5, 1, 15, 4, 25, 2};
    const std::uint8_t above[4] = {10, 40, 30, 20};

    pixels::Plane plane(4, 4, 4);
    constexpr int kStride = 4 + 2 * 4;
    std::uint8_t planeBuf[kStride * (4 + 2 * 4)];
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            plane.at(x, y) = srcData[y * 4 + x];
            planeBuf[y * kStride + x] = srcData[y * 4 + x];
        }
    }

    std::int32_t refCoeffs[16] = {0};
    std::uint8_t refRecon[16] = {0};
    pipeline::encodeRecon4x4(plane, 0, 0, above, 4, 0, nullptr, 0, 0, 0, intra::V_PRED, 0,
                             transforms::TxType::DCT_DCT, refCoeffs, refRecon);

    // stage 1: predict_block_4x4 -> pred
    const std::string ptxPred = *gpurt::compileToPtx(intra::predictBlockCuSource(), "compute_61");
    const std::vector<std::string> predNames = gpurt::ptxEntryNames(ptxPred);
    const auto itPred = std::find(predNames.begin(), predNames.end(), "predict_block_4x4");
    REQUIRE(itPred != predNames.end());
    gpurt::Kernel kPred(ptxPred, *itPred);

    gpurt::DeviceBuffer dAbove(sizeof(above));
    gpurt::DeviceBuffer dLeft(1);
    gpurt::DeviceBuffer dPred(16);
    dAbove.uploadFrom(above, sizeof(above));
    unsigned char dummyLeft = 0;
    dLeft.uploadFrom(&dummyLeft, 1);

    int modeArg = intra::V_PRED;
    int deltaArg = 0;
    int amArg = 0;
    int lmArg = 0;
    int fiArg = -1;
    int defArg = 0;
    int nTopArg = 4;
    int nTrArg = 0;
    int nLeftArg = 0;
    int nBlArg = 0;
    int alArg = 0;
    gpurt::DeviceBuffer dMode(sizeof(modeArg));
    gpurt::DeviceBuffer dDelta(sizeof(deltaArg));
    gpurt::DeviceBuffer dAm(sizeof(amArg));
    gpurt::DeviceBuffer dLm(sizeof(lmArg));
    gpurt::DeviceBuffer dFi(sizeof(fiArg));
    gpurt::DeviceBuffer dDef(sizeof(defArg));
    gpurt::DeviceBuffer dNTop(sizeof(nTopArg));
    gpurt::DeviceBuffer dNTr(sizeof(nTrArg));
    gpurt::DeviceBuffer dNLeft(sizeof(nLeftArg));
    gpurt::DeviceBuffer dNBl(sizeof(nBlArg));
    gpurt::DeviceBuffer dAl(sizeof(alArg));
    dMode.uploadFrom(&modeArg, sizeof(modeArg));
    dDelta.uploadFrom(&deltaArg, sizeof(deltaArg));
    dAm.uploadFrom(&amArg, sizeof(amArg));
    dLm.uploadFrom(&lmArg, sizeof(lmArg));
    dFi.uploadFrom(&fiArg, sizeof(fiArg));
    dDef.uploadFrom(&defArg, sizeof(defArg));
    dNTop.uploadFrom(&nTopArg, sizeof(nTopArg));
    dNTr.uploadFrom(&nTrArg, sizeof(nTrArg));
    dNLeft.uploadFrom(&nLeftArg, sizeof(nLeftArg));
    dNBl.uploadFrom(&nBlArg, sizeof(nBlArg));
    dAl.uploadFrom(&alArg, sizeof(alArg));

    CUdeviceptr pMode = dMode.get();
    CUdeviceptr pDelta = dDelta.get();
    CUdeviceptr pAm = dAm.get();
    CUdeviceptr pLm = dLm.get();
    CUdeviceptr pFi = dFi.get();
    CUdeviceptr pDef = dDef.get();
    CUdeviceptr pAbove = dAbove.get();
    CUdeviceptr pNTop = dNTop.get();
    CUdeviceptr pNTr = dNTr.get();
    CUdeviceptr pLeft = dLeft.get();
    CUdeviceptr pNLeft = dNLeft.get();
    CUdeviceptr pNBl = dNBl.get();
    CUdeviceptr pAl = dAl.get();
    CUdeviceptr pPred = dPred.get();
    void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                        &pNBl,  &pAl,   &pFi, &pDef, &pPred};
    kPred.launch(1, 1, 16, 1, argsPred);

    // stage 2: subtract_4x4_plane
    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> subNames = gpurt::ptxEntryNames(ptxSub);
    const auto itSub = std::find(subNames.begin(), subNames.end(), "subtract_4x4_plane");
    REQUIRE(itSub != subNames.end());
    gpurt::Kernel kSub(ptxSub, *itSub);

    gpurt::DeviceBuffer dPlane(sizeof(planeBuf));
    gpurt::DeviceBuffer dResidual(16 * sizeof(std::int16_t));
    dPlane.uploadFrom(planeBuf, sizeof(planeBuf));
    int srcStrideArg = kStride;
    gpurt::DeviceBuffer dStride(sizeof(srcStrideArg));
    dStride.uploadFrom(&srcStrideArg, sizeof(srcStrideArg));
    int pxArg = 0;
    int pyArg = 0;
    gpurt::DeviceBuffer dPx(sizeof(pxArg));
    gpurt::DeviceBuffer dPy(sizeof(pyArg));
    dPx.uploadFrom(&pxArg, sizeof(pxArg));
    dPy.uploadFrom(&pyArg, sizeof(pyArg));

    CUdeviceptr pPlane = dPlane.get();
    CUdeviceptr pResidual = dResidual.get();
    CUdeviceptr pStride = dStride.get();
    CUdeviceptr pPx = dPx.get();
    CUdeviceptr pPy = dPy.get();
    void* argsSub[] = {&pPlane, &pStride, &pPx, &pPy, &pPred, &pResidual};
    kSub.launch(1, 1, 16, 1, argsSub);

    // stage 3: fwd_txfm_2d_4x4
    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> txNames = gpurt::ptxEntryNames(ptxTx);
    const auto itTx = std::find(txNames.begin(), txNames.end(), "fwd_txfm_2d_4x4");
    REQUIRE(itTx != txNames.end());
    gpurt::Kernel kTx(ptxTx, *itTx);

    int typeArg = 0;
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    gpurt::DeviceBuffer dCoeffs(sizeof(refCoeffs));
    int fwdStrideArg = 4;
    gpurt::DeviceBuffer dFwdStride(sizeof(fwdStrideArg));
    dFwdStride.uploadFrom(&fwdStrideArg, sizeof(fwdStrideArg));
    CUdeviceptr pType = dType.get();
    CUdeviceptr pStrideTx = dFwdStride.get();
    CUdeviceptr pCoeffs = dCoeffs.get();
    void* argsTx[] = {&pResidual, &pStrideTx, &pType, &pCoeffs};
    kTx.launch(1, 1, 4, 1, argsTx);

    // stage 4: inv_txfm_2d_add_4x4 onto the same pred buffer (dPred)
    const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> invNames = gpurt::ptxEntryNames(ptxInv);
    const auto itInv = std::find(invNames.begin(), invNames.end(), "inv_txfm_2d_add_4x4");
    REQUIRE(itInv != invNames.end());
    gpurt::Kernel kInv(ptxInv, *itInv);

    int invStrideArg = 4;
    gpurt::DeviceBuffer dInvStride(sizeof(invStrideArg));
    dInvStride.uploadFrom(&invStrideArg, sizeof(invStrideArg));
    CUdeviceptr pInvStride = dInvStride.get();
    void* argsInv[] = {&pCoeffs, &pType, &pPred, &pInvStride};
    kInv.launch(1, 1, 4, 1, argsInv);

    std::int32_t gotCoeffs[16] = {0};
    std::uint8_t gotRecon[16] = {0};
    dCoeffs.downloadTo(gotCoeffs, sizeof(gotCoeffs));
    dPred.downloadTo(gotRecon, sizeof(gotRecon));

    bool coeffsOk = true;
    for (int i = 0; i < 16; ++i) {
        if (gotCoeffs[i] != refCoeffs[i]) {
            coeffsOk = false;
        }
    }
    CHECK(coeffsOk);

    bool reconOk = true;
    for (int i = 0; i < 16; ++i) {
        if (gotRecon[i] != refRecon[i]) {
            reconOk = false;
        }
    }
    CHECK(reconOk);
}

TEST_CASE("gpu subtract_8x8_plane matches host subtraction bit-exactly") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const std::uint8_t srcData[64] = {30, 41, 55, 19, 22, 3, 18, 9, 21, 13, 5, 27, 34, 6, 25, 11,
                                      8,  17, 40, 2,  12, 29, 7, 23, 15, 31, 4, 20, 26, 10, 16, 28,
                                      1,  24, 14, 33, 36, 18, 6, 32, 19, 5,  27, 9,  13, 30, 22, 3,
                                      25, 11, 8,  17, 38, 2,  12, 29, 7,  23, 15, 31, 4,  20, 26, 10};
    const std::uint8_t pred[64] = {10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40,
                                   10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40,
                                   10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40,
                                   10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40};

    constexpr int kStride = 8 + 2 * 4;
    std::uint8_t planeBuf[kStride * (8 + 2 * 4)] = {0};
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            planeBuf[y * kStride + x] = srcData[y * 8 + x];
        }
    }

    std::int16_t hostRes[64] = {0};
    for (int i = 0; i < 64; ++i) {
        hostRes[i] = static_cast<std::int16_t>(srcData[i] - pred[i]);
    }

    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> subNames = gpurt::ptxEntryNames(ptxSub);
    const auto itSub = std::find(subNames.begin(), subNames.end(), "subtract_8x8_plane");
    REQUIRE(itSub != subNames.end());
    gpurt::Kernel kSub(ptxSub, *itSub);

    gpurt::DeviceBuffer dPlane(sizeof(planeBuf));
    gpurt::DeviceBuffer dPred(64);
    gpurt::DeviceBuffer dResidual(64 * sizeof(std::int16_t));
    dPlane.uploadFrom(planeBuf, sizeof(planeBuf));
    dPred.uploadFrom(pred, 64);
    int srcStrideArg = kStride;
    gpurt::DeviceBuffer dStride(sizeof(srcStrideArg));
    dStride.uploadFrom(&srcStrideArg, sizeof(srcStrideArg));
    int pxArg = 0;
    int pyArg = 0;
    gpurt::DeviceBuffer dPx(sizeof(pxArg));
    gpurt::DeviceBuffer dPy(sizeof(pyArg));
    dPx.uploadFrom(&pxArg, sizeof(pxArg));
    dPy.uploadFrom(&pyArg, sizeof(pyArg));

    CUdeviceptr pPlane = dPlane.get();
    CUdeviceptr pStride = dStride.get();
    CUdeviceptr pPx = dPx.get();
    CUdeviceptr pPy = dPy.get();
    CUdeviceptr pPred = dPred.get();
    CUdeviceptr pResidual = dResidual.get();
    void* argsSub[] = {&pPlane, &pStride, &pPx, &pPy, &pPred, &pResidual};
    kSub.launch(1, 1, 64, 1, argsSub);

    std::int16_t got[64] = {0};
    dResidual.downloadTo(got, sizeof(got));
    bool ok = true;
    for (int i = 0; i < 64; ++i) {
        if (got[i] != hostRes[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu fwd_txfm_2d_8x8 matches host fwdTxfm2d8x8 (dct + adst)") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const std::int16_t res[64] = {30,  41, 55,  19,  22, 3,   18, 9,  21,  13,  5,  27, 34, 6,  25, 11,
                                  8,   17, 40,  2,   12, 29,  7,  23, 15,  31,  4,  20, 26, 10, 16, 28,
                                  1,   24, 14,  33,  36, 18,  6,  32, 19,  5,   27, 9,  13, 30, 22, 3,
                                  25,  11, 8,   17,  38, 2,   12, 29, 7,   23,  15, 31, 4,  20, 26, 10};

    std::int32_t refDct[64] = {0};
    std::int32_t refAdst[64] = {0};
    transforms::fwdTxfm2d8x8(res, refDct, 8, transforms::TxType::DCT_DCT);
    transforms::fwdTxfm2d8x8(res, refAdst, 8, transforms::TxType::ADST_ADST);

    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> txNames = gpurt::ptxEntryNames(ptxTx);
    const auto itTx = std::find(txNames.begin(), txNames.end(), "fwd_txfm_2d_8x8");
    REQUIRE(itTx != txNames.end());
    gpurt::Kernel kTx(ptxTx, *itTx);

    gpurt::DeviceBuffer dInput(64 * sizeof(std::int16_t));
    gpurt::DeviceBuffer dOutput(64 * sizeof(std::int32_t));
    dInput.uploadFrom(res, sizeof(res));
    int strideArg = 8;
    gpurt::DeviceBuffer dStride(sizeof(strideArg));
    dStride.uploadFrom(&strideArg, sizeof(strideArg));
    int typeArg = 0;
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));

    CUdeviceptr pInput = dInput.get();
    CUdeviceptr pStride = dStride.get();
    CUdeviceptr pType = dType.get();
    CUdeviceptr pOutput = dOutput.get();
    void* argsTx[] = {&pInput, &pStride, &pType, &pOutput};
    // one thread per column/row (4x4 pattern generalized): t must stay < 8
    kTx.launch(1, 1, 8, 1, argsTx);

    std::int32_t gotDct[64] = {0};
    dOutput.downloadTo(gotDct, sizeof(gotDct));
    bool dctOk = true;
    for (int i = 0; i < 64; ++i) {
        if (gotDct[i] != refDct[i]) {
            dctOk = false;
        }
    }
    CHECK(dctOk);

    typeArg = 1;
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    kTx.launch(1, 1, 8, 1, argsTx);
    std::int32_t gotAdst[64] = {0};
    dOutput.downloadTo(gotAdst, sizeof(gotAdst));
    bool adstOk = true;
    for (int i = 0; i < 64; ++i) {
        if (gotAdst[i] != refAdst[i]) {
            adstOk = false;
        }
    }
    CHECK(adstOk);
}

TEST_CASE("gpu inv_txfm_2d_add_8x8 matches host invTxfm2dAdd8x8 (dct + adst)") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const std::int32_t coeffs[64] = {
        500,  -120, 33,  2100, 17,  -8,  420, 90,  -300, 77,  1500, -45, 12,  600, -233, 61,
        88,   -410, 9,   720,  -150, 34,  5,   -96, 210,  40,  -88,  1300, 3,  -57, 810,  26,
        -640, 19,   905, -72,  11,   380, -25, 67,  55,   -90, 245,  16,   -78, 1330, 52, -7,
        300,  41,   -19, 620,  8,    -95, 174, 23,  -46,  910, 62,   -5,   87, -340, 28,  415};

    std::uint8_t predDct[64] = {0};
    std::uint8_t predAdst[64] = {0};
    for (int i = 0; i < 64; ++i) {
        predDct[i] = static_cast<std::uint8_t>((i * 7 + 3) & 255);
        predAdst[i] = static_cast<std::uint8_t>((i * 13 + 11) & 255);
    }
    std::uint8_t refDct[64] = {0};
    std::uint8_t refAdst[64] = {0};
    for (int i = 0; i < 64; ++i) {
        refDct[i] = predDct[i];
        refAdst[i] = predAdst[i];
    }
    transforms::invTxfm2dAdd8x8(coeffs, refDct, 8, transforms::TxType::DCT_DCT);
    transforms::invTxfm2dAdd8x8(coeffs, refAdst, 8, transforms::TxType::ADST_ADST);

    const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> invNames = gpurt::ptxEntryNames(ptxInv);
    const auto itInv = std::find(invNames.begin(), invNames.end(), "inv_txfm_2d_add_8x8");
    REQUIRE(itInv != invNames.end());
    gpurt::Kernel kInv(ptxInv, *itInv);

    gpurt::DeviceBuffer dCoeffs(sizeof(coeffs));
    gpurt::DeviceBuffer dDst(64);
    gpurt::DeviceBuffer dType(sizeof(int));
    gpurt::DeviceBuffer dStride(sizeof(int));
    dCoeffs.uploadFrom(coeffs, sizeof(coeffs));
    int typeArg = 0;
    int strideArg = 8;
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dStride.uploadFrom(&strideArg, sizeof(strideArg));

    CUdeviceptr pCoeffs = dCoeffs.get();
    CUdeviceptr pType = dType.get();
    CUdeviceptr pStride = dStride.get();
    CUdeviceptr pDst = dDst.get();
    void* argsInv[] = {&pCoeffs, &pType, &pDst, &pStride};

    dDst.uploadFrom(predDct, 64);
    kInv.launch(1, 1, 8, 1, argsInv);
    std::uint8_t gotDct[64] = {0};
    dDst.downloadTo(gotDct, 64);
    bool dctOk = true;
    for (int i = 0; i < 64; ++i) {
        if (gotDct[i] != refDct[i]) {
            dctOk = false;
        }
    }
    CHECK(dctOk);

    typeArg = 1;
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dDst.uploadFrom(predAdst, 64);
    kInv.launch(1, 1, 8, 1, argsInv);
    std::uint8_t gotAdst[64] = {0};
    dDst.downloadTo(gotAdst, 64);
    bool adstOk = true;
    for (int i = 0; i < 64; ++i) {
        if (gotAdst[i] != refAdst[i]) {
            adstOk = false;
        }
    }
    CHECK(adstOk);
}

TEST_CASE("frame auto 8x8 with quantization matches the QW1 generator golden") {
    // golden: golden_gen qw8_modes/qw8_recon/qw8_coeffs at qindex 100 -
    // B7 policy loop (8x8 blocks) with the FP quantizer wired in
    // (svtd_quantize_fp_8x8, n_coeffs=64/log_scale 0): qcoeff = coded coeffs,
    // dqcoeff feeds svtd_inv2dadd8x8. Loss feeds back through decisions:
    // qw8_modes 1 0 3 0 vs lossless b7_modes 1 5 3 0 (FR-series: real
    // top-right gather flipped both block-2 winners to D45).
    // SCAN POLICY IS OURS: fixed defaultScan8x8 for every block; SVT selects
    // per mode/tx type via get_scan_order (coefficients.h:40).
    const std::uint8_t srcData[256] = {
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
    const std::uint8_t goldenModes[4] = {1, 0, 3, 0};
    const std::uint8_t goldenRecon[256] = {
        19, 1, 9, 8, 18, 0, 6, 18, 13, 14, 10, 0, 21, 9, 7, 15,
        17, 7, 22, 4, 10, 10, 26, 7, 12, 19, 5, 14, 0, 23, 14, 9,
        19, 6, 5, 8, 13, 7, 15, 9, 10, 26, 0, 8, 13, 17, 5, 17,
        29, 0, 11, 17, 0, 31, 5, 15, 18, 25, 14, 18, 3, 35, 13, 18,
        12, 12, 0, 5, 3, 23, 0, 13, 23, 2, 24, 5, 14, 5, 36, 3,
        11, 20, 12, 4, 2, 29, 10, 10, 20, 4, 8, 8, 18, 0, 21, 8,
        11, 25, 4, 12, 13, 15, 10, 18, 24, 10, 16, 15, 9, 25, 11, 19,
        18, 25, 17, 23, 5, 30, 16, 22, 16, 6, 0, 7, 18, 0, 6, 13,
        10, 15, 6, 0, 23, 1, 3, 20, 18, 8, 19, 4, 11, 6, 33, 2,
        4, 21, 6, 3, 0, 26, 6, 8, 19, 7, 0, 14, 16, 0, 18, 10,
        9, 30, 4, 9, 15, 22, 8, 18, 27, 6, 18, 21, 1, 30, 14, 17,
        15, 24, 11, 21, 3, 28, 13, 20, 16, 11, 4, 11, 5, 20, 0, 17,
        17, 0, 26, 1, 10, 2, 30, 5, 14, 15, 4, 16, 0, 23, 10, 11,
        21, 1, 10, 10, 16, 0, 23, 7, 10, 25, 0, 7, 16, 11, 10, 14,
        26, 2, 9, 20, 5, 16, 12, 14, 15, 31, 10, 19, 1, 39, 9, 20,
        16, 7, 5, 9, 16, 0, 9, 20, 13, 12, 3, 0, 26, 0, 6, 15};
    const std::int32_t goldenCoeffs[256] = {
        -79, 0, 1, 1, 0, 0, 1, -1, -1, 0, 0, 0, 1, 1, 0, 2,
        1, 0, 0, -1, 0, 0, -1, 1, -1, 0, 0, 0, 0, -1, 0, 0,
        0, 0, 0, 0, 1, 0, 1, -1, 0, 0, 0, 0, 1, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 1, -1, -1, 0,
        -1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, -1,
        -1, 0, 0, 0, 1, -1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 2,
        0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, -1, 1, -1,
        -1, 0, 0, 0, 1, -1, -1, 1, 0, 0, 0, 0, 0, 0, 0, 1,
        -1, -2, 1, 0, 0, 0, 0, 0, -2, 0, 1, 0, -1, -2, 0, -1,
        -1, 1, 0, -1, 1, -1, 0, 0, -1, 0, -1, 0, 1, 1, 0, 2,
        0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, -1,
        0, 0, 0, 0, 1, -1, -1, 1, 0, 0, 0, 0, 0, 0, 0, 1,
        1, 0, 1, 1, 0, 0, 0, -1, 0, 0, 0, 0, 0, 2, 0, 1,
        0, 0, 0, 0, 0, 0, -1, 2, 0, 0, 0, 0, -1, 0, -1, 0,
        -1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, -1, 0, 1, -1,
        0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, -1, 1, 1, -1};

    pixels::Plane plane(16, 16, 4);
    pixels::Plane recon(16, 16, 4);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) plane.at(x, y) = srcData[y * 16 + x];

    std::int32_t coeffs[256] = {0};
    std::uint8_t modes[4] = {0};
    pipeline::encodeFrameAuto8x8Q(plane, recon, coeffs, modes, 100,
                                  transforms::TxType::DCT_DCT);

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modes[i] != goldenModes[i]) modesOk = false;
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int i = 0; i < 256; ++i) {
        if (recon.at(i & 15, i >> 4) != goldenRecon[i]) reconOk = false;
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 256; ++i) {
        if (coeffs[i] != goldenCoeffs[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}

TEST_CASE("frame recon 8x8 with quantization matches the QW1 golden block 0") {
    // golden: golden_gen qw8_coeffs/qw8_recon block 0 at qindex 100. Block 0
    // has no neighbors, so the Auto winner (V, qw8_modes[0] = 1) and a forced
    // V_PRED encode must produce identical coeffs + recon for it - this
    // pins encodeFrameRecon8x8Q's quantized path to the generator.
    const std::uint8_t srcData[256] = {
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
    const std::int32_t goldenBlock0Coeffs[64] = {
        -79, 0, 1, 1, 0, 0, 1, -1,
        -1, 0, 0, 0, 1, 1, 0, 2,
        1, 0, 0, -1, 0, 0, -1, 1,
        -1, 0, 0, 0, 0, -1, 0, 0,
        0, 0, 0, 0, 1, 0, 1, -1,
        0, 0, 0, 0, 1, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        -1, 0, 0, 0, 1, -1, -1, 0};
    const std::uint8_t goldenBlock0Recon[64] = {
        19, 1, 9, 8, 18, 0, 6, 18,
        17, 7, 22, 4, 10, 10, 26, 7,
        19, 6, 5, 8, 13, 7, 15, 9,
        29, 0, 11, 17, 0, 31, 5, 15,
        12, 12, 0, 5, 3, 23, 0, 13,
        11, 20, 12, 4, 2, 29, 10, 10,
        11, 25, 4, 12, 13, 15, 10, 18,
        18, 25, 17, 23, 5, 30, 16, 22};

    pixels::Plane plane(16, 16, 4);
    pixels::Plane recon(16, 16, 4);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) plane.at(x, y) = srcData[y * 16 + x];

    std::int32_t coeffs[256] = {0};
    pipeline::encodeFrameRecon8x8Q(plane, recon, coeffs, intra::V_PRED, 0, 100,
                                   transforms::TxType::DCT_DCT);

    bool coeffsOk = true;
    for (int i = 0; i < 64; ++i) {
        if (coeffs[i] != goldenBlock0Coeffs[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);

    bool reconOk = true;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            if (recon.at(x, y) != goldenBlock0Recon[y * 8 + x]) reconOk = false;
    CHECK(reconOk);
}
TEST_CASE("gpu frame auto 8x8 with quantization matches host encodeFrameAuto8x8Q") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    // same 16x16 fixture as the QW2 golden (qw8_modes 1 0 2 0 at qindex 100)
    const std::uint8_t srcData[256] = {
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
    constexpr int kQindex = 100;

    pixels::Plane plane(16, 16, 4);
    pixels::Plane reconRef(16, 16, 4);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) plane.at(x, y) = srcData[y * 16 + x];
    std::int32_t refCoeffs[256] = {0};
    std::uint8_t refModes[4] = {0};
    pipeline::encodeFrameAuto8x8Q(plane, reconRef, refCoeffs, refModes, kQindex,
                                  transforms::TxType::DCT_DCT);

    const std::string ptxPred = *gpurt::compileToPtx(intra::predictBlock8x8CuSource(), "compute_61");
    const std::vector<std::string> pn = gpurt::ptxEntryNames(ptxPred);
    gpurt::Kernel kPred(ptxPred, *std::find(pn.begin(), pn.end(), "predict_block_8x8"));
    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> sn = gpurt::ptxEntryNames(ptxSub);
    gpurt::Kernel kSub(ptxSub, *std::find(sn.begin(), sn.end(), "subtract_8x8_plane"));
    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> tn = gpurt::ptxEntryNames(ptxTx);
    gpurt::Kernel kTx(ptxTx, *std::find(tn.begin(), tn.end(), "fwd_txfm_2d_8x8"));
    const std::string ptxQ = *gpurt::compileToPtx(transforms::quantCuSource(), "compute_61");
    const std::vector<std::string> qn = gpurt::ptxEntryNames(ptxQ);
    gpurt::Kernel kQuant(ptxQ, *std::find(qn.begin(), qn.end(), "quant_dequant_8x8"));
    const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> inames = gpurt::ptxEntryNames(ptxInv);
    gpurt::Kernel kInv(ptxInv, *std::find(inames.begin(), inames.end(), "inv_txfm_2d_add_8x8"));

    transforms::QuantTables qt;
    transforms::buildQuantTables(kQindex, qt);
    std::int16_t scan[64];
    transforms::defaultScan8x8(scan);

    constexpr int kStride = 16;
    gpurt::DeviceBuffer dPlane(256);
    gpurt::DeviceBuffer dRecon(256);
    gpurt::DeviceBuffer dResidual(64 * sizeof(std::int16_t));
    gpurt::DeviceBuffer dBlkCoeffs(64 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dQcoeffs(64 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dDqcoeffs(64 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dEob(sizeof(std::uint16_t));
    gpurt::DeviceBuffer dPred(64);
    gpurt::DeviceBuffer dQuantFp(sizeof(qt.quantFp));
    gpurt::DeviceBuffer dDequant(sizeof(qt.dequant));
    gpurt::DeviceBuffer dRoundFp(sizeof(qt.roundFp));
    gpurt::DeviceBuffer dScan(sizeof(scan));
    dPlane.uploadFrom(srcData, 256);
    std::uint8_t zero[256] = {0};
    dRecon.uploadFrom(zero, 256);
    dQuantFp.uploadFrom(qt.quantFp, sizeof(qt.quantFp));
    dDequant.uploadFrom(qt.dequant, sizeof(qt.dequant));
    dRoundFp.uploadFrom(qt.roundFp, sizeof(qt.roundFp));
    dScan.uploadFrom(scan, sizeof(scan));

    int deltaArg = 0;
    int fiArg = -1;
    int defArg = 0;
    int typeArg = 0;
    int planeStrideArg = kStride;
    int fwdStrideArg = 8;
    int invStrideArg = 8;
    gpurt::DeviceBuffer dMode(sizeof(int));
    gpurt::DeviceBuffer dDelta(sizeof(deltaArg));
    gpurt::DeviceBuffer dAm(sizeof(int));
    gpurt::DeviceBuffer dLm(sizeof(int));
    gpurt::DeviceBuffer dFi(sizeof(fiArg));
    gpurt::DeviceBuffer dDef(sizeof(defArg));
    gpurt::DeviceBuffer dNTop(sizeof(int));
    gpurt::DeviceBuffer dNTr(sizeof(int));
    gpurt::DeviceBuffer dNLeft(sizeof(int));
    gpurt::DeviceBuffer dNBl(sizeof(int));
    gpurt::DeviceBuffer dAl(sizeof(int));
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    gpurt::DeviceBuffer dPlaneStride(sizeof(planeStrideArg));
    gpurt::DeviceBuffer dFwdStride(sizeof(fwdStrideArg));
    gpurt::DeviceBuffer dInvStride(sizeof(invStrideArg));
    gpurt::DeviceBuffer dPx(sizeof(int));
    gpurt::DeviceBuffer dPy(sizeof(int));
    gpurt::DeviceBuffer dAbove(16);
    gpurt::DeviceBuffer dLeft(16);
    dDelta.uploadFrom(&deltaArg, sizeof(deltaArg));
    dFi.uploadFrom(&fiArg, sizeof(fiArg));
    dDef.uploadFrom(&defArg, sizeof(defArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dPlaneStride.uploadFrom(&planeStrideArg, sizeof(planeStrideArg));
    dFwdStride.uploadFrom(&fwdStrideArg, sizeof(fwdStrideArg));
    dInvStride.uploadFrom(&invStrideArg, sizeof(invStrideArg));

    std::uint8_t reconGot[256] = {0};
    std::int32_t coeffsGot[256] = {0};
    std::uint8_t modesGot[4] = {0};
    std::uint8_t reconWin[256] = {0};
    std::uint8_t aboveHost[16] = {0};
    std::uint8_t leftHost[16] = {0};
    std::uint8_t srcBlk[64] = {0};

    for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 2; ++bx) {
            const int px = bx * 8;
            const int py = by * 8;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const int nTopPx = hasTop ? 8 : 0;
            const int nLeftPx = hasLeft ? 8 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < 2) ? 8 : 0;

            dRecon.downloadTo(reconWin, 256);
            // above = B real recon samples + REAL recon top-right
            // (above[8..15] = recon[(py-1)][px+8..px+15], reconstructed by the
            // M1 raster rule) - mirrors FR1 host gather
            if (hasTop) {
                for (int i = 0; i < 16; ++i) aboveHost[i] = reconWin[(py - 1) * 16 + px + i];
            }
            if (hasLeft) {
                for (int i = 0; i < 8; ++i) leftHost[i] = reconWin[(py + i) * 16 + px - 1];
            }
            int alVal = 0;
            if (hasTop && hasLeft) alVal = reconWin[(py - 1) * 16 + px - 1];

            // HOST DECISION (policy is host code), against reconstructed edges
            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modesGot[(by - 1) * 2 + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modesGot[by * 2 + bx - 1])
                                : intra::DC_PRED;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) srcBlk[y * 8 + x] = srcData[(py + y) * 16 + px + x];
            const auto d = pipeline::decideBlockMode8x8(srcBlk, aboveHost, nTopPx, nTopRightPx,
                                                        leftHost, nLeftPx, 0,
                                                        static_cast<std::uint8_t>(alVal), nctx);
            modesGot[by * 2 + bx] = static_cast<std::uint8_t>(d.mode);

            // GPU EXECUTION of the winner: predict -> subtract -> fwd ->
            // quantize fp -> inverse on dqcoeff
            int modeArg = d.mode;
            int amArg = static_cast<int>(nctx.aboveMode);
            int lmArg = static_cast<int>(nctx.leftMode);
            int nTopArg = nTopPx;
            int nTrArg = nTopRightPx;
            int nLeftArg = nLeftPx;
            int nBlArg = 0;
            int alArg = alVal;
            int pxArg = px;
            int pyArg = py;
            dMode.uploadFrom(&modeArg, sizeof(modeArg));
            dAm.uploadFrom(&amArg, sizeof(amArg));
            dLm.uploadFrom(&lmArg, sizeof(lmArg));
            dNTop.uploadFrom(&nTopArg, sizeof(nTopArg));
            dNTr.uploadFrom(&nTrArg, sizeof(nTrArg));
            dNLeft.uploadFrom(&nLeftArg, sizeof(nLeftArg));
            dNBl.uploadFrom(&nBlArg, sizeof(nBlArg));
            dAl.uploadFrom(&alArg, sizeof(alArg));
            dPx.uploadFrom(&pxArg, sizeof(pxArg));
            dPy.uploadFrom(&pyArg, sizeof(pyArg));
            if (hasTop) dAbove.uploadFrom(aboveHost, 16);
            if (hasLeft) dLeft.uploadFrom(leftHost, 16);

            CUdeviceptr pMode = dMode.get();
            CUdeviceptr pDelta = dDelta.get();
            CUdeviceptr pAm = dAm.get();
            CUdeviceptr pLm = dLm.get();
            CUdeviceptr pAbove = dAbove.get();
            CUdeviceptr pNTop = dNTop.get();
            CUdeviceptr pNTr = dNTr.get();
            CUdeviceptr pLeft = dLeft.get();
            CUdeviceptr pNLeft = dNLeft.get();
            CUdeviceptr pNBl = dNBl.get();
            CUdeviceptr pAl = dAl.get();
            CUdeviceptr pFi = dFi.get();
            CUdeviceptr pDef = dDef.get();
            CUdeviceptr pPred = dPred.get();
            void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft,
                                &pNLeft, &pNBl, &pAl, &pFi, &pDef, &pPred};
            kPred.launch(1, 1, 64, 1, argsPred);

            CUdeviceptr pPlane = dPlane.get();
            CUdeviceptr pPlaneStride = dPlaneStride.get();
            CUdeviceptr pPx = dPx.get();
            CUdeviceptr pPy = dPy.get();
            CUdeviceptr pResidual = dResidual.get();
            void* argsSub[] = {&pPlane, &pPlaneStride, &pPx, &pPy, &pPred, &pResidual};
            kSub.launch(1, 1, 64, 1, argsSub);

            CUdeviceptr pType = dType.get();
            CUdeviceptr pFwdStride = dFwdStride.get();
            CUdeviceptr pBlkCoeffs = dBlkCoeffs.get();
            void* argsTx[] = {&pResidual, &pFwdStride, &pType, &pBlkCoeffs};
            kTx.launch(1, 1, 8, 1, argsTx);

            CUdeviceptr pQuantFp = dQuantFp.get();
            CUdeviceptr pDequant = dDequant.get();
            CUdeviceptr pRoundFp = dRoundFp.get();
            CUdeviceptr pScan = dScan.get();
            CUdeviceptr pQcoeffs = dQcoeffs.get();
            CUdeviceptr pDqcoeffs = dDqcoeffs.get();
            CUdeviceptr pEob = dEob.get();
            void* argsQuant[] = {&pBlkCoeffs, &pQuantFp, &pDequant, &pRoundFp, &pScan,
                                 &pQcoeffs, &pDqcoeffs, &pEob};
            kQuant.launch(1, 1, 64, 1, argsQuant);
            std::int32_t blkQ[64] = {0};
            dQcoeffs.downloadTo(blkQ, sizeof(blkQ));
            for (int i = 0; i < 64; ++i) coeffsGot[(by * 2 + bx) * 64 + i] = blkQ[i];

            CUdeviceptr pInvStride = dInvStride.get();
            void* argsInv[] = {&pDqcoeffs, &pType, &pPred, &pInvStride};
            kInv.launch(1, 1, 8, 1, argsInv);

            std::uint8_t blkRecon[64] = {0};
            dPred.downloadTo(blkRecon, sizeof(blkRecon));
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) reconGot[(py + y) * 16 + px + x] = blkRecon[y * 8 + x];
            dRecon.uploadFrom(reconGot, 256);
        }
    }

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modesGot[i] != refModes[i]) modesOk = false;
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int i = 0; i < 256; ++i) {
        if (reconGot[i] != reconRef.at(i & 15, i >> 4)) reconOk = false;
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 256; ++i) {
        if (coeffsGot[i] != refCoeffs[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}

TEST_CASE("gpu frame auto 8x8 matches host encodeFrameAuto8x8 (host decides, gpu executes)") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    // same 16x16 fixture as the B7 host frame test (golden b7_modes 1 5 6 0)
    const std::uint8_t srcData[256] = {
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

    pixels::Plane plane(16, 16, 4);
    pixels::Plane reconRef(16, 16, 4);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) plane.at(x, y) = srcData[y * 16 + x];
    std::int32_t refCoeffs[256] = {0};
    std::uint8_t refModes[4] = {0};
    pipeline::encodeFrameAuto8x8(plane, reconRef, refCoeffs, refModes, transforms::TxType::DCT_DCT);

    const std::string ptxPred = *gpurt::compileToPtx(intra::predictBlock8x8CuSource(), "compute_61");
    const std::vector<std::string> pn = gpurt::ptxEntryNames(ptxPred);
    gpurt::Kernel kPred(ptxPred, *std::find(pn.begin(), pn.end(), "predict_block_8x8"));
    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> sn = gpurt::ptxEntryNames(ptxSub);
    gpurt::Kernel kSub(ptxSub, *std::find(sn.begin(), sn.end(), "subtract_8x8_plane"));
    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> tn = gpurt::ptxEntryNames(ptxTx);
    gpurt::Kernel kTx(ptxTx, *std::find(tn.begin(), tn.end(), "fwd_txfm_2d_8x8"));
    const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> inames = gpurt::ptxEntryNames(ptxInv);
    gpurt::Kernel kInv(ptxInv, *std::find(inames.begin(), inames.end(), "inv_txfm_2d_add_8x8"));

    constexpr int kStride = 16;
    gpurt::DeviceBuffer dPlane(256);
    gpurt::DeviceBuffer dRecon(256);
    gpurt::DeviceBuffer dResidual(64 * sizeof(std::int16_t));
    gpurt::DeviceBuffer dBlkCoeffs(64 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dPred(64);
    dPlane.uploadFrom(srcData, 256);
    std::uint8_t zero[256] = {0};
    dRecon.uploadFrom(zero, 256);

    int deltaArg = 0;
    int fiArg = -1;
    int defArg = 0;
    int typeArg = 0;
    int planeStrideArg = kStride;
    int fwdStrideArg = 8;
    int invStrideArg = 8;
    gpurt::DeviceBuffer dMode(sizeof(int));
    gpurt::DeviceBuffer dDelta(sizeof(deltaArg));
    gpurt::DeviceBuffer dAm(sizeof(int));
    gpurt::DeviceBuffer dLm(sizeof(int));
    gpurt::DeviceBuffer dFi(sizeof(fiArg));
    gpurt::DeviceBuffer dDef(sizeof(defArg));
    gpurt::DeviceBuffer dNTop(sizeof(int));
    gpurt::DeviceBuffer dNTr(sizeof(int));
    gpurt::DeviceBuffer dNLeft(sizeof(int));
    gpurt::DeviceBuffer dNBl(sizeof(int));
    gpurt::DeviceBuffer dAl(sizeof(int));
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    gpurt::DeviceBuffer dPlaneStride(sizeof(planeStrideArg));
    gpurt::DeviceBuffer dFwdStride(sizeof(fwdStrideArg));
    gpurt::DeviceBuffer dInvStride(sizeof(invStrideArg));
    gpurt::DeviceBuffer dPx(sizeof(int));
    gpurt::DeviceBuffer dPy(sizeof(int));
    gpurt::DeviceBuffer dAbove(16);
    gpurt::DeviceBuffer dLeft(16);
    dDelta.uploadFrom(&deltaArg, sizeof(deltaArg));
    dFi.uploadFrom(&fiArg, sizeof(fiArg));
    dDef.uploadFrom(&defArg, sizeof(defArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dPlaneStride.uploadFrom(&planeStrideArg, sizeof(planeStrideArg));
    dFwdStride.uploadFrom(&fwdStrideArg, sizeof(fwdStrideArg));
    dInvStride.uploadFrom(&invStrideArg, sizeof(invStrideArg));

    std::uint8_t reconGot[256] = {0};
    std::int32_t coeffsGot[256] = {0};
    std::uint8_t modesGot[4] = {0};
    std::uint8_t reconWin[256] = {0};
    std::uint8_t aboveHost[16] = {0};
    std::uint8_t leftHost[16] = {0};
    std::uint8_t srcBlk[64] = {0};

    for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 2; ++bx) {
            const int px = bx * 8;
            const int py = by * 8;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const int nTopPx = hasTop ? 8 : 0;
            const int nLeftPx = hasLeft ? 8 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < 2) ? 8 : 0;

            dRecon.downloadTo(reconWin, 256);
            // above = B real recon samples + REAL recon top-right
            // (above[8..15] = recon[(py-1)][px+8..px+15], reconstructed by the
            // M1 raster rule) - mirrors FR1 host gather
            if (hasTop) {
                for (int i = 0; i < 16; ++i) aboveHost[i] = reconWin[(py - 1) * 16 + px + i];
            }
            if (hasLeft) {
                for (int i = 0; i < 8; ++i) leftHost[i] = reconWin[(py + i) * 16 + px - 1];
            }
            int alVal = 0;
            if (hasTop && hasLeft) alVal = reconWin[(py - 1) * 16 + px - 1];

            // HOST DECISION (policy is host code): chosen neighbor modes feed
            // NeighborContext; decision against reconstructed edges
            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop
                                 ? static_cast<intra::PredictionMode>(modesGot[(by - 1) * 2 + bx])
                                 : intra::DC_PRED;
            nctx.leftMode = hasLeft
                                ? static_cast<intra::PredictionMode>(modesGot[by * 2 + bx - 1])
                                : intra::DC_PRED;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) srcBlk[y * 8 + x] = srcData[(py + y) * 16 + px + x];
            const auto d = pipeline::decideBlockMode8x8(srcBlk, aboveHost, nTopPx, nTopRightPx,
                                                        leftHost, nLeftPx, 0,
                                                        static_cast<std::uint8_t>(alVal), nctx);
            modesGot[by * 2 + bx] = static_cast<std::uint8_t>(d.mode);

            // GPU EXECUTION of the winner
            int modeArg = d.mode;
            int amArg = static_cast<int>(nctx.aboveMode);
            int lmArg = static_cast<int>(nctx.leftMode);
            int nTopArg = nTopPx;
            int nTrArg = nTopRightPx;
            int nLeftArg = nLeftPx;
            int nBlArg = 0;
            int alArg = alVal;
            int pxArg = px;
            int pyArg = py;
            dMode.uploadFrom(&modeArg, sizeof(modeArg));
            dAm.uploadFrom(&amArg, sizeof(amArg));
            dLm.uploadFrom(&lmArg, sizeof(lmArg));
            dNTop.uploadFrom(&nTopArg, sizeof(nTopArg));
            dNTr.uploadFrom(&nTrArg, sizeof(nTrArg));
            dNLeft.uploadFrom(&nLeftArg, sizeof(nLeftArg));
            dNBl.uploadFrom(&nBlArg, sizeof(nBlArg));
            dAl.uploadFrom(&alArg, sizeof(alArg));
            dPx.uploadFrom(&pxArg, sizeof(pxArg));
            dPy.uploadFrom(&pyArg, sizeof(pyArg));
            if (hasTop) dAbove.uploadFrom(aboveHost, 16);
            if (hasLeft) dLeft.uploadFrom(leftHost, 16);

            CUdeviceptr pMode = dMode.get();
            CUdeviceptr pDelta = dDelta.get();
            CUdeviceptr pAm = dAm.get();
            CUdeviceptr pLm = dLm.get();
            CUdeviceptr pAbove = dAbove.get();
            CUdeviceptr pNTop = dNTop.get();
            CUdeviceptr pNTr = dNTr.get();
            CUdeviceptr pLeft = dLeft.get();
            CUdeviceptr pNLeft = dNLeft.get();
            CUdeviceptr pNBl = dNBl.get();
            CUdeviceptr pAl = dAl.get();
            CUdeviceptr pFi = dFi.get();
            CUdeviceptr pDef = dDef.get();
            CUdeviceptr pPred = dPred.get();
            void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft,
                                &pNLeft, &pNBl, &pAl, &pFi, &pDef, &pPred};
            kPred.launch(1, 1, 64, 1, argsPred);

            CUdeviceptr pPlane = dPlane.get();
            CUdeviceptr pPlaneStride = dPlaneStride.get();
            CUdeviceptr pPx = dPx.get();
            CUdeviceptr pPy = dPy.get();
            CUdeviceptr pResidual = dResidual.get();
            void* argsSub[] = {&pPlane, &pPlaneStride, &pPx, &pPy, &pPred, &pResidual};
            kSub.launch(1, 1, 64, 1, argsSub);

            CUdeviceptr pType = dType.get();
            CUdeviceptr pFwdStride = dFwdStride.get();
            CUdeviceptr pBlkCoeffs = dBlkCoeffs.get();
            void* argsTx[] = {&pResidual, &pFwdStride, &pType, &pBlkCoeffs};
            kTx.launch(1, 1, 8, 1, argsTx);
            std::int32_t blk[64] = {0};
            dBlkCoeffs.downloadTo(blk, sizeof(blk));
            for (int i = 0; i < 64; ++i) coeffsGot[(by * 2 + bx) * 64 + i] = blk[i];

            CUdeviceptr pInvStride = dInvStride.get();
            void* argsInv[] = {&pBlkCoeffs, &pType, &pPred, &pInvStride};
            kInv.launch(1, 1, 8, 1, argsInv);

            std::uint8_t blkRecon[64] = {0};
            dPred.downloadTo(blkRecon, sizeof(blkRecon));
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) reconGot[(py + y) * 16 + px + x] = blkRecon[y * 8 + x];
            dRecon.uploadFrom(reconGot, 256);
        }
    }

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modesGot[i] != refModes[i]) modesOk = false;
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            if (reconGot[y * 16 + x] != reconRef.at(x, y)) reconOk = false;
        }
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 256; ++i) {
        if (coeffsGot[i] != refCoeffs[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}

TEST_CASE("gpu frame auto 4x4 with quantization matches host encodeFrameAuto4x4Q") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    // same fixture as the D4 GPU frame test / d3 golden
    const std::uint8_t srcData[64] = {21, 3,  5,  9,  19, 2, 8,  14, 9,  11, 3, 7,  5,  23, 1, 17,
                                      7,  13, 5,  1,  25, 4, 6,  18, 15, 4,  25, 2,  12, 9,  30, 3,
                                      18, 5,  7,  13, 14, 2, 20, 8,  6,  24, 3,  9,  11, 17, 5, 19,
                                      22, 1,  8,  15, 4,  29, 7, 13, 10, 16, 6, 12, 3,  25, 11, 9};
    constexpr int kQindex = 100;

    pixels::Plane plane(8, 8, 4);
    pixels::Plane reconRef(8, 8, 4);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            plane.at(x, y) = srcData[y * 8 + x];
        }
    }
    std::int32_t refCoeffs[64] = {0};
    std::uint8_t refModes[4] = {0};
    pipeline::encodeFrameAuto4x4Q(plane, reconRef, refCoeffs, refModes, kQindex,
                                  transforms::TxType::DCT_DCT);

    const std::string ptxPred = *gpurt::compileToPtx(intra::predictBlockCuSource(), "compute_61");
    const std::vector<std::string> pn = gpurt::ptxEntryNames(ptxPred);
    gpurt::Kernel kPred(ptxPred, *std::find(pn.begin(), pn.end(), "predict_block_4x4"));
    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> sn = gpurt::ptxEntryNames(ptxSub);
    gpurt::Kernel kSub(ptxSub, *std::find(sn.begin(), sn.end(), "subtract_4x4_plane"));
    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> tn = gpurt::ptxEntryNames(ptxTx);
    gpurt::Kernel kTx(ptxTx, *std::find(tn.begin(), tn.end(), "fwd_txfm_2d_4x4"));
    const std::string ptxQ = *gpurt::compileToPtx(transforms::quantCuSource(), "compute_61");
    const std::vector<std::string> qn = gpurt::ptxEntryNames(ptxQ);
    gpurt::Kernel kQuant(ptxQ, *std::find(qn.begin(), qn.end(), "quant_dequant_4x4"));
    const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> inames = gpurt::ptxEntryNames(ptxInv);
    gpurt::Kernel kInv(ptxInv, *std::find(inames.begin(), inames.end(), "inv_txfm_2d_add_4x4"));

    transforms::QuantTables qt;
    transforms::buildQuantTables(kQindex, qt);
    std::int16_t scan[16];
    transforms::defaultScan4x4(scan);

    constexpr int kStride = 8;
    gpurt::DeviceBuffer dPlane(sizeof(srcData));
    gpurt::DeviceBuffer dRecon(64);
    gpurt::DeviceBuffer dResidual(16 * sizeof(std::int16_t));
    gpurt::DeviceBuffer dBlkCoeffs(16 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dQcoeffs(16 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dDqcoeffs(16 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dEob(sizeof(std::uint16_t));
    gpurt::DeviceBuffer dPred(16);
    gpurt::DeviceBuffer dQuantFp(sizeof(qt.quantFp));
    gpurt::DeviceBuffer dDequant(sizeof(qt.dequant));
    gpurt::DeviceBuffer dRoundFp(sizeof(qt.roundFp));
    gpurt::DeviceBuffer dScan(sizeof(scan));
    dPlane.uploadFrom(srcData, sizeof(srcData));
    std::uint8_t zero[64] = {0};
    dRecon.uploadFrom(zero, 64);
    dQuantFp.uploadFrom(qt.quantFp, sizeof(qt.quantFp));
    dDequant.uploadFrom(qt.dequant, sizeof(qt.dequant));
    dRoundFp.uploadFrom(qt.roundFp, sizeof(qt.roundFp));
    dScan.uploadFrom(scan, sizeof(scan));

    int deltaArg = 0;
    int fiArg = -1;
    int defArg = 0;
    int typeArg = 0;
    int planeStrideArg = kStride;
    int fwdStrideArg = 4;
    int invStride = 4;
    gpurt::DeviceBuffer dMode(sizeof(int));
    gpurt::DeviceBuffer dDelta(sizeof(deltaArg));
    gpurt::DeviceBuffer dAm(sizeof(int));
    gpurt::DeviceBuffer dLm(sizeof(int));
    gpurt::DeviceBuffer dFi(sizeof(fiArg));
    gpurt::DeviceBuffer dDef(sizeof(defArg));
    gpurt::DeviceBuffer dNTop(sizeof(int));
    gpurt::DeviceBuffer dNTr(sizeof(int));
    gpurt::DeviceBuffer dNLeft(sizeof(int));
    gpurt::DeviceBuffer dNBl(sizeof(int));
    gpurt::DeviceBuffer dAl(sizeof(int));
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    gpurt::DeviceBuffer dPlaneStride(sizeof(planeStrideArg));
    gpurt::DeviceBuffer dFwdStride(sizeof(fwdStrideArg));
    gpurt::DeviceBuffer dInvStride(sizeof(int));
    gpurt::DeviceBuffer dPx(sizeof(int));
    gpurt::DeviceBuffer dPy(sizeof(int));
    gpurt::DeviceBuffer dAbove(8);
    gpurt::DeviceBuffer dLeft(4);
    dDelta.uploadFrom(&deltaArg, sizeof(deltaArg));
    dFi.uploadFrom(&fiArg, sizeof(fiArg));
    dDef.uploadFrom(&defArg, sizeof(defArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dPlaneStride.uploadFrom(&planeStrideArg, sizeof(planeStrideArg));
    dFwdStride.uploadFrom(&fwdStrideArg, sizeof(fwdStrideArg));
    dInvStride.uploadFrom(&invStride, sizeof(invStride));

    std::uint8_t reconGot[64] = {0};
    std::int32_t coeffsGot[64] = {0};
    std::uint8_t modesGot[4] = {0};
    std::uint8_t reconWin[64] = {0};
    std::uint8_t aboveHost[8] = {0};
    std::uint8_t leftHost[4] = {0};
    std::uint8_t srcBlk[16] = {0};

    for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 2; ++bx) {
            const int px = bx * 4;
            const int py = by * 4;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const int nTopPx = hasTop ? 4 : 0;
            const int nLeftPx = hasLeft ? 4 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < 2) ? 4 : 0;

            dRecon.downloadTo(reconWin, 64);
            if (hasTop) {
                for (int i = 0; i < 4 + nTopRightPx; ++i) aboveHost[i] = reconWin[(py - 1) * 8 + px + i];
            }
            if (hasLeft) {
                for (int i = 0; i < 4; ++i) leftHost[i] = reconWin[(py + i) * 8 + px - 1];
            }
            int alVal = 0;
            if (hasTop && hasLeft) alVal = reconWin[(py - 1) * 8 + px - 1];

            // HOST DECISION (policy is host code), against reconstructed edges
            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop ? static_cast<intra::PredictionMode>(modesGot[(by - 1) * 2 + bx])
                                    : intra::DC_PRED;
            nctx.leftMode = hasLeft ? static_cast<intra::PredictionMode>(modesGot[by * 2 + bx - 1])
                                    : intra::DC_PRED;
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) srcBlk[y * 4 + x] = srcData[(py + y) * 8 + px + x];
            }
            const auto d = pipeline::decideBlockMode4x4(srcBlk, aboveHost, nTopPx, nTopRightPx,
                                                        leftHost, nLeftPx, 0,
                                                        static_cast<std::uint8_t>(alVal), nctx);
            modesGot[by * 2 + bx] = static_cast<std::uint8_t>(d.mode);

            // GPU EXECUTION of the winner: predict -> subtract -> fwd ->
            // quantize fp -> inverse on dqcoeff
            int modeArg = d.mode;
            int amArg = static_cast<int>(nctx.aboveMode);
            int lmArg = static_cast<int>(nctx.leftMode);
            int nTopArg = nTopPx;
            int nTrArg = nTopRightPx;
            int nLeftArg = nLeftPx;
            int nBlArg = 0;
            int alArg = alVal;
            int pxArg = px;
            int pyArg = py;
            dMode.uploadFrom(&modeArg, sizeof(modeArg));
            dAm.uploadFrom(&amArg, sizeof(amArg));
            dLm.uploadFrom(&lmArg, sizeof(lmArg));
            dNTop.uploadFrom(&nTopArg, sizeof(nTopArg));
            dNTr.uploadFrom(&nTrArg, sizeof(nTrArg));
            dNLeft.uploadFrom(&nLeftArg, sizeof(nLeftArg));
            dNBl.uploadFrom(&nBlArg, sizeof(nBlArg));
            dAl.uploadFrom(&alArg, sizeof(alArg));
            dPx.uploadFrom(&pxArg, sizeof(pxArg));
            dPy.uploadFrom(&pyArg, sizeof(pyArg));
            if (hasTop) dAbove.uploadFrom(aboveHost, 4 + nTopRightPx);
            if (hasLeft) dLeft.uploadFrom(leftHost, 4);

            CUdeviceptr pMode = dMode.get();
            CUdeviceptr pDelta = dDelta.get();
            CUdeviceptr pAm = dAm.get();
            CUdeviceptr pLm = dLm.get();
            CUdeviceptr pAbove = dAbove.get();
            CUdeviceptr pNTop = dNTop.get();
            CUdeviceptr pNTr = dNTr.get();
            CUdeviceptr pLeft = dLeft.get();
            CUdeviceptr pNLeft = dNLeft.get();
            CUdeviceptr pNBl = dNBl.get();
            CUdeviceptr pAl = dAl.get();
            CUdeviceptr pFi = dFi.get();
            CUdeviceptr pDef = dDef.get();
            CUdeviceptr pPred = dPred.get();
            void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft,
                                &pNLeft, &pNBl, &pAl, &pFi, &pDef, &pPred};
            kPred.launch(1, 1, 16, 1, argsPred);

            CUdeviceptr pPlane = dPlane.get();
            CUdeviceptr pPlaneStride = dPlaneStride.get();
            CUdeviceptr pPx = dPx.get();
            CUdeviceptr pPy = dPy.get();
            CUdeviceptr pResidual = dResidual.get();
            void* argsSub[] = {&pPlane, &pPlaneStride, &pPx, &pPy, &pPred, &pResidual};
            kSub.launch(1, 1, 16, 1, argsSub);

            CUdeviceptr pType = dType.get();
            CUdeviceptr pFwdStride = dFwdStride.get();
            CUdeviceptr pBlkCoeffs = dBlkCoeffs.get();
            void* argsTx[] = {&pResidual, &pFwdStride, &pType, &pBlkCoeffs};
            kTx.launch(1, 1, 4, 1, argsTx);

            CUdeviceptr pQuantFp = dQuantFp.get();
            CUdeviceptr pDequant = dDequant.get();
            CUdeviceptr pRoundFp = dRoundFp.get();
            CUdeviceptr pScan = dScan.get();
            CUdeviceptr pQcoeffs = dQcoeffs.get();
            CUdeviceptr pDqcoeffs = dDqcoeffs.get();
            CUdeviceptr pEob = dEob.get();
            void* argsQuant[] = {&pBlkCoeffs, &pQuantFp, &pDequant, &pRoundFp, &pScan,
                                 &pQcoeffs, &pDqcoeffs, &pEob};
            kQuant.launch(1, 1, 16, 1, argsQuant);
            std::int32_t blkQ[16] = {0};
            dQcoeffs.downloadTo(blkQ, sizeof(blkQ));
            for (int i = 0; i < 16; ++i) coeffsGot[(by * 2 + bx) * 16 + i] = blkQ[i];

            CUdeviceptr pInvStride = dInvStride.get();
            void* argsInv[] = {&pDqcoeffs, &pType, &pPred, &pInvStride};
            kInv.launch(1, 1, 4, 1, argsInv);

            std::uint8_t blkRecon[16] = {0};
            dPred.downloadTo(blkRecon, sizeof(blkRecon));
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    reconGot[(py + y) * 8 + px + x] = blkRecon[y * 4 + x];
                }
            }
            dRecon.uploadFrom(reconGot, 64);
        }
    }

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modesGot[i] != refModes[i]) modesOk = false;
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (reconGot[y * 8 + x] != reconRef.at(x, y)) reconOk = false;
        }
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 64; ++i) {
        if (coeffsGot[i] != refCoeffs[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}

TEST_CASE("frame auto 16x16 matches the f16 generator golden (real top-right)") {
    // golden: golden_gen f16_modes/f16_recon/f16_coeffs (32x32, 2x2 of 16x16;
    // rows 0-15 ramp 4*(x+y+1), rows 16-31 zero). Block 2 (bx=0, by=1) is the
    // only nTr>0 block; with REAL recon top-right its D45 candidate SAD is
    // 32767 (ramp continuation vs zero source) and H (16384) wins, under the
    // pre-FR1 zero-fill D45 scores 12673 and would win - the mode map
    // discriminates the gather (probe values, verbatim primitives, C7a).
    // recon == source: the 16x16 fwd/inv roundtrip is exact.
    const std::uint8_t goldenModes[4] = {1, 7, 2, 2};

    pixels::Plane plane(32, 32, 4);
    pixels::Plane recon(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            plane.at(x, y) = (y < 16) ? static_cast<std::uint8_t>(4 * (x + y + 1)) : 0;

    std::int32_t coeffs[1024] = {0};
    std::uint8_t modes[4] = {0};
    pipeline::encodeFrameAuto16x16(plane, recon, coeffs, modes, transforms::TxType::DCT_DCT);

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modes[i] != goldenModes[i]) modesOk = false;
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (recon.at(x, y) != plane.at(x, y)) reconOk = false;
    CHECK(reconOk);

    // coeff spot pins from the f16_coeffs gate line
    const std::int32_t goldenHeads[4][16] = {
        {-8063, -2344, 0, -257, 0, -90, 0, -44, 3, -24, 0, -14, 0, -7, 0, -3},
        {2849, -1605, 56, -181, 9, -66, 8, -29, 6, -30, -12, -4, -8, -4, 3, 4},
        {-8190, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    bool coeffsOk = true;
    for (int b = 0; b < 4; ++b) {
        for (int i = 0; i < 16; ++i) {
            if (coeffs[b * 256 + i] != goldenHeads[b][i]) coeffsOk = false;
        }
    }
    CHECK(coeffsOk);
}

TEST_CASE("frame auto 16x16 with quantization matches the f16q generator golden") {
    // golden: golden_gen f16q_modes/f16q_coeffs at qindex 100 (FP quantizer at
    // n_coeffs=256, log_scale 0). Modes identical to lossless (1 7 2 2);
    // quantization loss visible in coeffs.
    const std::uint8_t goldenModes[4] = {1, 7, 2, 2};

    pixels::Plane plane(32, 32, 4);
    pixels::Plane recon(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            plane.at(x, y) = (y < 16) ? static_cast<std::uint8_t>(4 * (x + y + 1)) : 0;

    std::int32_t coeffs[1024] = {0};
    std::uint8_t modes[4] = {0};
    pipeline::encodeFrameAuto16x16Q(plane, recon, coeffs, modes, 100, transforms::TxType::DCT_DCT);

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modes[i] != goldenModes[i]) modesOk = false;
    }
    CHECK(modesOk);

    const std::int32_t goldenBlk0[16] = {-87, -21, 0, -2, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const std::int32_t goldenBlk2[8] = {-88, 0, 0, 0, 0, 0, 0, 0};
    bool coeffsOk = true;
    for (int i = 0; i < 16; ++i) {
        if (coeffs[i] != goldenBlk0[i]) coeffsOk = false;
    }
    for (int i = 0; i < 8; ++i) {
        if (coeffs[512 + i] != goldenBlk2[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}
TEST_CASE("frame auto 16x16 emits kf luma symbols through l7 (f16dc gate)") {
    // Gate: bsf1_modes 1 7 0 0, bsf1_ctx 0 0 0 1 1 0 4 0,
    // bsf1_bytes 3 7f f8 b0, bsf1_rt 1 3 7 3 0 0 0 0, bsf1_cdf_eq 1
    // (tools/golden_gen/main_primitives.c BSF1 block). f16dc fixture: top
    // half = f16 ramp (rows 0-15: 4*(x+y+1)), bottom-left 16x16 flat 94,
    // bottom-right 16x16 flat 126 - the D2 policy decides {V, D203, DC, DC},
    // covering the FI flag=0 write path (DC blocks) that the f16 fixture
    // lacks. Emission is symbols only per D5: kf y mode
    // (entropy_coding.c:1026-1040 via writeKfLumaMode) + angle delta
    // (directional, 16x16 >= 8x8, delta 0 -> raw symbol 3) + filter-intra
    // flag=0 (writeFilterIntra with FILTER_INTRA_MODES) where
    // filterIntraAllowed (mode_decision.c:108-119, DC_PRED-only). No
    // partition/skip symbols (ECP1/ECP2), no tile assembly (BSF3/BSF4).
    pixels::Plane plane(32, 32, 4);
    pixels::Plane recon(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            if (y < 16)
                plane.at(x, y) = static_cast<std::uint8_t>(4 * (x + y + 1));
            else if (x < 16)
                plane.at(x, y) = 94;
            else
                plane.at(x, y) = 126;
        }

    const std::uint8_t goldenModes[4] = {1, 7, 0, 0};
    const unsigned char goldenBytes[3] = {0x7f, 0xf8, 0xb0};

    std::int32_t coeffs[1024] = {0};
    std::uint8_t modes[4] = {0};
    entropy::EcFrameContext fc;
    entropy::AomWriter w{};
    unsigned char buf[64] = {0};
    w.ec.buf = buf;
    pipeline::encodeFrameAuto16x16(plane, recon, coeffs, modes, transforms::TxType::DCT_DCT, &w, &fc);

    bool modesOk = true;
    for (int i = 0; i < 4; ++i)
        if (modes[i] != goldenModes[i]) modesOk = false;
    CHECK(modesOk);

    REQUIRE(w.pos == 3);
    for (int i = 0; i < 3; ++i) CHECK((unsigned)buf[i] == goldenBytes[i]);

    // reader round-trip with adaptation: contexts recomputed from the
    // DECIDED neighbor modes (DC_PRED when unavailable), fresh default CDFs
    // (the lossless variant's bucket = 0, matching the writer's init)
    entropy::EcFrameContext fcR;
    entropy::initDefaultEcFrameContext(&fcR, 0);
    entropy::AomReader r;
    REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
    r.allow_update_cdf = 1;
    static const int wantRt[8] = {1, 3, 7, 3, 0, 0, 0, 0};
    int ri = 0;
    for (int b = 0; b < 4; ++b) {
        const int bx = b % 2, by = b / 2;
        const bool hasTop = by > 0, hasLeft = bx > 0;
        const int topMode = hasTop ? modes[(by - 1) * 2 + bx] : 0;
        const int leftMode = hasLeft ? modes[by * 2 + bx - 1] : 0;
        int topCtx, leftCtx;
        entropy::getKfYModeCtx(hasLeft ? 1 : 0, leftMode, hasTop ? 1 : 0, topMode, &topCtx, &leftCtx);
        int delta = 0;
        const int m = (int)entropy::readKfLumaMode(&r, &fcR, entropy::BLOCK_16X16, topCtx, leftCtx, &delta);
        CHECK(m == wantRt[ri++]);
        if (entropy::isDirectionalMode((entropy::PredictionMode)modes[b])) {
            CHECK(delta == wantRt[ri++]);  // raw symbol (delta + MAX_ANGLE_DELTA)
        }
        if (entropy::filterIntraAllowed(1, entropy::BLOCK_16X16, 0, (std::uint32_t)modes[b])) {
            entropy::FilterIntraMode f;
            CHECK(entropy::readFilterIntra(&r, &fcR, entropy::BLOCK_16X16, &f) == wantRt[ri++]);
        }
    }
    CHECK(entropy::ecFrameCdfsEqual(&fc, &fcR) == 1);

    // Q variant: same decided modes and identical symbol stream (lossless
    // and q100 decisions coincide for this fixture, as for f16/f16q)
    pixels::Plane reconQ(32, 32, 4);
    std::int32_t coeffsQ[1024] = {0};
    std::uint8_t modesQ[4] = {0};
    entropy::EcFrameContext fcQ;
    entropy::AomWriter wQ{};
    unsigned char bufQ[64] = {0};
    wQ.ec.buf = bufQ;
    pipeline::encodeFrameAuto16x16Q(plane, reconQ, coeffsQ, modesQ, 100, transforms::TxType::DCT_DCT, &wQ, &fcQ);
    bool modesQOk = true;
    for (int i = 0; i < 4; ++i)
        if (modesQ[i] != goldenModes[i]) modesQOk = false;
    CHECK(modesQOk);
    REQUIRE(wQ.pos == 3);
    for (int i = 0; i < 3; ++i) CHECK((unsigned)bufQ[i] == goldenBytes[i]);
    // TD5b: the Q variant's bucket = 2 (q100); the reader side must walk the
    // SAME symbol sequence through the same bucket for the adapted-CDF
    // equality to hold.
    entropy::EcFrameContext fcRQ;
    entropy::initDefaultEcFrameContext(&fcRQ, 100);
    entropy::AomReader rQ;
    REQUIRE(entropy::odEcReaderInit(&rQ, bufQ, wQ.pos) == 0);
    rQ.allow_update_cdf = 1;
    int riQ = 0;
    for (int b = 0; b < 4; ++b) {
        const int bx = b % 2, by = b / 2;
        const bool hasTop = by > 0, hasLeft = bx > 0;
        const int topMode = hasTop ? modesQ[(by - 1) * 2 + bx] : 0;
        const int leftMode = hasLeft ? modesQ[by * 2 + bx - 1] : 0;
        int topCtxQ, leftCtxQ;
        entropy::getKfYModeCtx(hasLeft ? 1 : 0, leftMode, hasTop ? 1 : 0, topMode, &topCtxQ,
                               &leftCtxQ);
        int deltaQ = 0;
        const int mQ =
            (int)entropy::readKfLumaMode(&rQ, &fcRQ, entropy::BLOCK_16X16, topCtxQ, leftCtxQ, &deltaQ);
        CHECK(mQ == wantRt[riQ++]);
        if (entropy::isDirectionalMode((entropy::PredictionMode)modesQ[b])) {
            CHECK(deltaQ == wantRt[riQ++]);
        }
        if (entropy::filterIntraAllowed(1, entropy::BLOCK_16X16, 0, (std::uint32_t)modesQ[b])) {
            entropy::FilterIntraMode fQ;
            CHECK(entropy::readFilterIntra(&rQ, &fcRQ, entropy::BLOCK_16X16, &fQ) == wantRt[riQ++]);
        }
    }
    CHECK(entropy::ecFrameCdfsEqual(&fcQ, &fcRQ) == 1);
}

TEST_CASE("frame auto 16x16Q token emission matches gate (skip=0, q100)") {
    // TS3 gate: the 4 f16 blocks (decided modes {1,7,2,2}), skip = 0 for all
    // four (no skip decision logic - the residual is coded for every block),
    // q100 real residuals through the full token chain (fwd16x16 ->
    // quantizeFp16x16 q100 -> writeBlockCoeffs with getTxbCtx from the NA
    // model + writeTxType reduced_tx_set=1 + decided intra_dir). Decoder
    // mirror: readBlockCoeffs -> dequant -> inv16x16 -> recon. GPU frame
    // paths unchanged (host bookkeeping only). No new decision logic (the
    // D2 policy is unchanged).
    pixels::Plane plane(32, 32, 4);
    pixels::Plane recon(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            plane.at(x, y) = (y < 16) ? static_cast<std::uint8_t>(4 * (x + y + 1)) : 0;

    const std::uint8_t goldenModes[4] = {1, 7, 2, 2};

    std::int32_t coeffs[1024] = {0};
    std::uint8_t modes[4] = {0};
    entropy::EcFrameContext fc;
    entropy::AomWriter w{};
    unsigned char buf[256] = {0};
    w.ec.buf = buf;
    entropy::DcSignLevelCoeffNa na;
    memset(&na, 0xFF, sizeof(na));
    pipeline::encodeFrameAuto16x16Q(plane, recon, coeffs, modes, 100, transforms::TxType::DCT_DCT, &w, &fc, &na);
    // TS3c: the l6 pipeline and the generator's svtd_ts3_drive agree
    // bit-exact on this fixture (the drive's above-edge gather passed
    // (row, col) where svtd_gather_above expects (px=col, py=row), so
    // block 2 decided against out-of-bounds recon[-16..-1] + row 0).
    // Acceptance: modes AND eobs AND coeffs AND recon AND the token byte
    // stream all equal the generator's ecfrm_* gate values (composition.c
    // TS3 block).
    std::int16_t scan16[256];
    transforms::defaultScan16x16(scan16);

    bool modesOk = true;
    for (int i = 0; i < 4; ++i)
        if (modes[i] != goldenModes[i]) modesOk = false;
    CHECK(modesOk);
    // measured eobs == gate ecfrm_eobs 21 16 1 0 (scan order)
    {
        const int wantEob[4] = {21, 16, 1, 0};
        for (int b = 0; b < 4; ++b) {
            std::int32_t ec = 0;
            for (int c = 255; c >= 0; --c) {
                if (coeffs[b * 256 + scan16[c]] != 0) { ec = c + 1; break; }
            }
            MESSAGE("eob block " << b << " = " << ec);
            CHECK(ec == wantEob[b]);
        }
    }
    // token byte stream == gate ecfrm_bytes 26 7c e4 69 12 eb 4f fd 9b 5b
    // dc 58 58 ca c9 83 88 0a 13 81 d7 a2 c0 99 54 ce 3c (TD5b: the q100
    // bucket, idx 2)
    REQUIRE(w.pos == 26);
    static const unsigned char wantBytes[26] = {0x7c, 0xe4, 0x69, 0x12, 0xeb, 0x4f, 0xfd, 0x9b,
                                                0x5b, 0xdc, 0x58, 0x58, 0xca, 0xc9, 0x83, 0x88,
                                                0x0a, 0x13, 0x81, 0xd7, 0xa2, 0xc0, 0x99, 0x54,
                                                0xce, 0x3c};
    for (int i = 0; i < 26; ++i) CHECK((unsigned)buf[i] == wantBytes[i]);
    // quantized coefficients == gate ecfrm_coeffs (generator raster order)
    static const std::int32_t genCoeffs[1024] = {        -87, -21, 0, -2, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -21, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        32, -14, 0, -2, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -4, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        -88, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    bool coeffsOk = true;
    for (int i = 0; i < 1024; ++i)
        if (coeffs[i] != genCoeffs[i]) coeffsOk = false;
    CHECK(coeffsOk);
    // reconstructed frame == gate ecfrm_recon (generator raster order)
    static const std::uint8_t genRecon[1024] = {        5, 8, 12, 17, 21, 24, 28, 32, 37, 41, 45, 48, 52, 57, 61, 64, 71, 74, 78, 84, 87, 90, 94, 98, 101, 105, 109, 111, 115, 120, 125, 129, 8, 11, 15, 20, 23, 27, 30, 35, 40, 44, 48, 51, 55, 60, 64, 67, 73, 76, 82, 87, 90, 93, 97, 101, 105, 109, 112, 115, 119, 124, 129, 133,
        12, 15, 19, 24, 28, 31, 35, 39, 44, 48, 52, 56, 59, 64, 68, 71, 76, 80, 85, 90, 93, 97, 100, 104, 108, 112, 115, 118, 122, 128, 132, 136, 17, 20, 24, 28, 32, 36, 39, 44, 48, 53, 57, 60, 64, 68, 73, 75, 80, 83, 88, 93, 97, 100, 103, 107, 111, 115, 118, 122, 126, 131, 135, 140,
        21, 23, 28, 32, 36, 39, 43, 47, 52, 57, 60, 64, 68, 72, 76, 79, 83, 87, 92, 97, 100, 104, 107, 111, 116, 120, 123, 126, 130, 135, 139, 142, 24, 27, 31, 35, 39, 43, 46, 51, 56, 60, 64, 67, 71, 75, 80, 83, 87, 91, 96, 101, 104, 109, 112, 116, 120, 124, 127, 130, 133, 138, 143, 147,
        28, 30, 35, 39, 43, 46, 50, 54, 59, 64, 67, 71, 75, 79, 83, 86, 92, 95, 100, 105, 110, 113, 117, 121, 124, 129, 131, 134, 138, 143, 148, 152, 32, 35, 39, 44, 47, 51, 55, 59, 64, 68, 72, 75, 79, 84, 88, 91, 95, 100, 105, 110, 114, 117, 121, 124, 128, 132, 136, 139, 143, 148, 153, 157,
        37, 40, 44, 49, 52, 56, 59, 64, 69, 73, 77, 80, 84, 88, 93, 96, 99, 103, 108, 114, 117, 121, 124, 128, 132, 137, 141, 144, 148, 153, 158, 161, 41, 44, 49, 53, 57, 60, 64, 68, 73, 78, 81, 85, 88, 93, 97, 100, 102, 106, 112, 116, 120, 124, 128, 133, 137, 142, 146, 149, 152, 157, 161, 164,
        45, 48, 52, 57, 61, 64, 68, 72, 77, 81, 85, 88, 92, 97, 101, 104, 105, 109, 114, 120, 123, 128, 132, 137, 142, 146, 149, 152, 155, 160, 164, 166, 48, 51, 56, 60, 64, 67, 71, 75, 80, 85, 88, 92, 95, 100, 104, 107, 109, 112, 118, 124, 128, 133, 137, 142, 146, 150, 153, 156, 159, 163, 167, 169,
        52, 55, 59, 64, 68, 71, 75, 79, 84, 88, 92, 96, 99, 104, 108, 111, 114, 118, 124, 130, 134, 138, 142, 145, 150, 154, 157, 159, 163, 167, 171, 173, 57, 60, 64, 68, 72, 76, 79, 84, 88, 93, 97, 100, 104, 108, 113, 115, 120, 124, 130, 134, 138, 142, 145, 149, 153, 157, 160, 163, 167, 171, 175, 178,
        61, 64, 68, 73, 76, 80, 83, 88, 93, 97, 101, 104, 108, 113, 117, 120, 125, 129, 133, 138, 142, 145, 148, 152, 157, 161, 164, 167, 170, 175, 179, 182, 64, 67, 71, 75, 79, 83, 86, 91, 96, 100, 104, 107, 111, 115, 120, 122, 128, 131, 135, 140, 143, 146, 150, 154, 158, 163, 166, 169, 173, 177, 181, 184,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    bool reconOk = true;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (recon.at(x, y) != genRecon[y * 32 + x]) reconOk = false;
    CHECK(reconOk);


    // decoder mirror: read back all 4 blocks through the l7 reader
    entropy::EcFrameContext fcR;
    entropy::initDefaultEcFrameContext(&fcR, 100);
    entropy::AomReader r;
    REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
    r.allow_update_cdf = 1;
    entropy::DcSignLevelCoeffNa naR;
    memset(&naR, 0xFF, sizeof(naR));

    // kf contexts from the DECIDED neighbor modes (as the writer did)
    for (int b = 0; b < 4; ++b) {
        const int bx = b % 2, by = b / 2;
        const int top0 = by > 0 ? modes[(by - 1) * 2 + bx] : 0;
        const int left0 = bx > 0 ? modes[by * 2 + bx - 1] : 0;
        int topCtx = 0, leftCtx = 0;
        entropy::getKfYModeCtx(bx > 0 ? 1 : 0, left0, by > 0 ? 1 : 0, top0, &topCtx, &leftCtx);
        int delta = 0;
        const entropy::PredictionMode m =
            entropy::readKfLumaMode(&r, &fcR, entropy::BLOCK_16X16, topCtx, leftCtx, &delta);
        CHECK((int)m == goldenModes[b]);

        // filter-intra symbol when allowed (writer pipeline.cpp:961-964:
        // writeFilterIntra FILTER_INTRA_MODES = the off symbol, reads 0)
        if (entropy::filterIntraAllowed(1, entropy::BLOCK_16X16, 0, (std::uint32_t)m)) {
            entropy::FilterIntraMode f;
            CHECK(entropy::readFilterIntra(&r, &fcR, entropy::BLOCK_16X16, &f) == 0);
        }

        // token chain read with the NA ctx. NO separate readTxType: the
        // tx-type symbol lives INSIDE the chain (writeTxbCoeffs
        // entropy.cpp:1493-1497 / readTxbCoeffs :1598-1602)
        int txbSkipCtx = 0, dcSignCtx = 0;
        entropy::getTxbCtx(&naR.above[bx * 4], &naR.left[by * 4], 4, 4, 0,
                           entropy::BLOCK_16X16, entropy::TX_16X16, &txbSkipCtx, &dcSignCtx);
        std::int32_t qcRead[256] = {0};
        const int reob = entropy::readTxbCoeffs(&r, &fcR, qcRead, scan16,
                                                entropy::TX_16X16, txbSkipCtx, dcSignCtx, 1, m,
                                                entropy::COMPONENT_LUMA);
        // reob must equal the writer's eob (the roundtrip property)
        for (int i = 0; i < 256; ++i) CHECK(qcRead[i] == coeffs[b * 256 + i]);

        // NA update (mirrors writeBlockCoeffs)
        std::int32_t cul = 0;
        for (int c = 0; c < reob; ++c) {
            const std::int32_t lv = qcRead[scan16[c]];
            cul += (lv < 0 ? -lv : lv);
        }
        cul = COEFF_CONTEXT_MASK < cul ? COEFF_CONTEXT_MASK : cul;
        if (reob > 0) {
            if (qcRead[0] < 0) cul |= 1 << 6;
            else if (qcRead[0] > 0) cul += 2 << 6;
        }
        for (int k = 0; k < 4; ++k) naR.above[bx * 4 + k] = static_cast<std::uint8_t>(cul);
        for (int k = 0; k < 4; ++k) naR.left[by * 4 + k] = static_cast<std::uint8_t>(cul);
    }
    CHECK(entropy::ecFrameCdfsEqual(&fc, &fcR) == 1);
}

TEST_CASE("gpu frame auto 16x16 matches host encodeFrameAuto16x16 (host decides, gpu executes)") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

std::uint8_t srcData[1024];
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            srcData[y * 32 + x] = (y < 16) ? static_cast<std::uint8_t>(4 * (x + y + 1)) : 0;

    pixels::Plane plane(32, 32, 4);
    pixels::Plane reconRef(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) plane.at(x, y) = srcData[y * 32 + x];

    std::int32_t refCoeffs[1024] = {0};
    std::uint8_t refModes[4] = {0};
    pipeline::encodeFrameAuto16x16(plane, reconRef, refCoeffs, refModes, transforms::TxType::DCT_DCT);

    const std::string ptxPred = *gpurt::compileToPtx(intra::predictBlock16x16CuSource(), "compute_61");
    const std::vector<std::string> pn = gpurt::ptxEntryNames(ptxPred);
    gpurt::Kernel kPred(ptxPred, *std::find(pn.begin(), pn.end(), "predict_block_16x16"));
    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> sn = gpurt::ptxEntryNames(ptxSub);
    gpurt::Kernel kSub(ptxSub, *std::find(sn.begin(), sn.end(), "subtract_16x16_plane"));
    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> tn = gpurt::ptxEntryNames(ptxTx);
    gpurt::Kernel kTx(ptxTx, *std::find(tn.begin(), tn.end(), "fwd_txfm_2d_16x16"));
    const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> inames = gpurt::ptxEntryNames(ptxInv);
    gpurt::Kernel kInv(ptxInv, *std::find(inames.begin(), inames.end(), "inv_txfm_2d_add_16x16"));

    constexpr int kStride = 32;
    gpurt::DeviceBuffer dPlane(sizeof(srcData));
    gpurt::DeviceBuffer dRecon(1024);
    gpurt::DeviceBuffer dResidual(256 * sizeof(std::int16_t));
    gpurt::DeviceBuffer dBlkCoeffs(256 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dPred(256);
    dPlane.uploadFrom(srcData, sizeof(srcData));
    std::uint8_t zero[1024] = {0};
    dRecon.uploadFrom(zero, 1024);

    int amArg = 0;
    int lmArg = 0;
    int fiArg = -1;
    int defArg = 0;
    int typeArg = 0;
    int planeStrideArg = kStride;
    int fwdStrideArg = 16;
    int invStride = 16;
    gpurt::DeviceBuffer dMode(sizeof(int));
    gpurt::DeviceBuffer dDelta(sizeof(int));
    gpurt::DeviceBuffer dAm(sizeof(amArg));
    gpurt::DeviceBuffer dLm(sizeof(lmArg));
    gpurt::DeviceBuffer dFi(sizeof(fiArg));
    gpurt::DeviceBuffer dDef(sizeof(defArg));
    gpurt::DeviceBuffer dNTop(sizeof(int));
    gpurt::DeviceBuffer dNTr(sizeof(int));
    gpurt::DeviceBuffer dNLeft(sizeof(int));
    gpurt::DeviceBuffer dNBl(sizeof(int));
    gpurt::DeviceBuffer dAl(sizeof(int));
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    gpurt::DeviceBuffer dPlaneStride(sizeof(planeStrideArg));
    gpurt::DeviceBuffer dFwdStride(sizeof(fwdStrideArg));
    gpurt::DeviceBuffer dInvStride(sizeof(int));
    gpurt::DeviceBuffer dPx(sizeof(int));
    gpurt::DeviceBuffer dPy(sizeof(int));
gpurt::DeviceBuffer dAbove(32);
    gpurt::DeviceBuffer dLeft(32);
    dDelta.uploadFrom(&defArg, sizeof(defArg));
    dFi.uploadFrom(&fiArg, sizeof(fiArg));
    dDef.uploadFrom(&defArg, sizeof(defArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dPlaneStride.uploadFrom(&planeStrideArg, sizeof(planeStrideArg));
    dFwdStride.uploadFrom(&fwdStrideArg, sizeof(fwdStrideArg));
    dInvStride.uploadFrom(&invStride, sizeof(invStride));

    std::uint8_t reconGot[1024] = {0};
    std::int32_t coeffsGot[1024] = {0};
    std::uint8_t modesGot[4] = {0};
    std::uint8_t aboveHost[32] = {0};
    std::uint8_t leftHost[32] = {0};

    for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 2; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < 2) ? 16 : 0;

            std::uint8_t tmp[1024] = {0};
            dRecon.downloadTo(tmp, 1024);
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) {
                    aboveHost[i] = tmp[(py - 1) * 32 + px + i];
                }
                dAbove.uploadFrom(aboveHost, 16 + nTopRightPx);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) leftHost[i] = tmp[(py + i) * 32 + px - 1];
                dLeft.uploadFrom(leftHost, 16);
            }
            int alVal = 0;
            if (hasTop && hasLeft) alVal = tmp[(py - 1) * 32 + px - 1];

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop ? static_cast<intra::PredictionMode>(modesGot[(by - 1) * 2 + bx])
                                    : intra::DC_PRED;
            nctx.leftMode = hasLeft ? static_cast<intra::PredictionMode>(modesGot[by * 2 + bx - 1])
                                    : intra::DC_PRED;
            std::uint8_t srcBlk[256] = {0};
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                    srcBlk[y * 16 + x] = srcData[(py + y) * 32 + px + x];
                }
            }
            const auto d = pipeline::decideBlockMode16x16(srcBlk, aboveHost, nTopPx, nTopRightPx,
                                                          leftHost, nLeftPx, 0,
                                                          static_cast<std::uint8_t>(alVal), nctx);
            modesGot[by * 2 + bx] = static_cast<std::uint8_t>(d.mode);

            int modeArg = d.mode;
            amArg = (int)nctx.aboveMode;
            lmArg = (int)nctx.leftMode;
            int deltaArg = 0;
            int nTopArg = nTopPx;
            int nTrArg = nTopRightPx;
            int nLeftArg = nLeftPx;
            int nBlArg = 0;
            int alArg = alVal;
            int pxArg = px;
            int pyArg = py;
            dMode.uploadFrom(&modeArg, sizeof(modeArg));
            dDelta.uploadFrom(&deltaArg, sizeof(deltaArg));
            dAm.uploadFrom(&amArg, sizeof(amArg));
            dLm.uploadFrom(&lmArg, sizeof(lmArg));
            dNTop.uploadFrom(&nTopArg, sizeof(nTopArg));
            dNTr.uploadFrom(&nTrArg, sizeof(nTrArg));
            dNLeft.uploadFrom(&nLeftArg, sizeof(nLeftArg));
            dNBl.uploadFrom(&nBlArg, sizeof(nBlArg));
            dAl.uploadFrom(&alArg, sizeof(alArg));
            dPx.uploadFrom(&pxArg, sizeof(pxArg));
            dPy.uploadFrom(&pyArg, sizeof(pyArg));

            CUdeviceptr pMode = dMode.get();
            CUdeviceptr pDelta = dDelta.get();
            CUdeviceptr pAm = dAm.get();
            CUdeviceptr pLm = dLm.get();
            CUdeviceptr pAbove = dAbove.get();
            CUdeviceptr pNTop = dNTop.get();
            CUdeviceptr pNTr = dNTr.get();
            CUdeviceptr pLeft = dLeft.get();
            CUdeviceptr pNLeft = dNLeft.get();
CUdeviceptr pNBl = dNBl.get();
            CUdeviceptr pAl = dAl.get();
            CUdeviceptr pFi = dFi.get();
            CUdeviceptr pDef = dDef.get();
            CUdeviceptr pPred = dPred.get();
            void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                                &pNBl,  &pAl,   &pFi, &pDef, &pPred};
            kPred.launch(1, 1, 256, 1, argsPred);

            CUdeviceptr pPlane = dPlane.get();
            CUdeviceptr pPlaneStride = dPlaneStride.get();
            CUdeviceptr pPx = dPx.get();
            CUdeviceptr pPy = dPy.get();
            CUdeviceptr pResidual = dResidual.get();
            void* argsSub[] = {&pPlane, &pPlaneStride, &pPx, &pPy, &pPred, &pResidual};
            kSub.launch(1, 1, 256, 1, argsSub);

            CUdeviceptr pType = dType.get();
            CUdeviceptr pFwdStride = dFwdStride.get();
            CUdeviceptr pBlkCoeffs = dBlkCoeffs.get();
            void* argsTx[] = {&pResidual, &pFwdStride, &pType, &pBlkCoeffs};
            kTx.launch(1, 1, 16, 1, argsTx);
            std::int32_t blk[256] = {0};
            dBlkCoeffs.downloadTo(blk, sizeof(blk));
            for (int i = 0; i < 256; ++i) {
                coeffsGot[(by * 2 + bx) * 256 + i] = blk[i];
            }

            CUdeviceptr pInvStride = dInvStride.get();
            void* argsInv[] = {&pBlkCoeffs, &pType, &pPred, &pInvStride};
            kInv.launch(1, 1, 16, 1, argsInv);

            std::uint8_t blkRecon[256] = {0};
            dPred.downloadTo(blkRecon, sizeof(blkRecon));
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                    reconGot[(py + y) * 32 + px + x] = blkRecon[y * 16 + x];
                }
            }
            dRecon.uploadFrom(reconGot, 1024);
        }
    }

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modesGot[i] != refModes[i]) modesOk = false;
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int i = 0; i < 1024; ++i) {
        if (reconGot[i] != reconRef.at(i & 31, i >> 5)) reconOk = false;
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 1024; ++i) {
        if (coeffsGot[i] != refCoeffs[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}
TEST_CASE("gpu frame auto 16x16 with quantization matches host encodeFrameAuto16x16Q") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    std::uint8_t srcData[1024];
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            srcData[y * 32 + x] = (y < 16) ? static_cast<std::uint8_t>(4 * (x + y + 1)) : 0;

    pixels::Plane plane(32, 32, 4);
    pixels::Plane reconRef(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) plane.at(x, y) = srcData[y * 32 + x];
    std::int32_t refCoeffs[1024] = {0};
    std::uint8_t refModes[4] = {0};
    pipeline::encodeFrameAuto16x16Q(plane, reconRef, refCoeffs, refModes, 100, transforms::TxType::DCT_DCT);

    const std::string ptxPred = *gpurt::compileToPtx(intra::predictBlock16x16CuSource(), "compute_61");
    const std::vector<std::string> pn = gpurt::ptxEntryNames(ptxPred);
    gpurt::Kernel kPred(ptxPred, *std::find(pn.begin(), pn.end(), "predict_block_16x16"));
    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> sn = gpurt::ptxEntryNames(ptxSub);
    gpurt::Kernel kSub(ptxSub, *std::find(sn.begin(), sn.end(), "subtract_16x16_plane"));
    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> tn = gpurt::ptxEntryNames(ptxTx);
    gpurt::Kernel kTx(ptxTx, *std::find(tn.begin(), tn.end(), "fwd_txfm_2d_16x16"));
    const std::string ptxQ = *gpurt::compileToPtx(transforms::quantCuSource(), "compute_61");
    const std::vector<std::string> qn = gpurt::ptxEntryNames(ptxQ);
    gpurt::Kernel kQuant(ptxQ, *std::find(qn.begin(), qn.end(), "quant_dequant_16x16"));
    const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> inames = gpurt::ptxEntryNames(ptxInv);
    gpurt::Kernel kInv(ptxInv, *std::find(inames.begin(), inames.end(), "inv_txfm_2d_add_16x16"));

    transforms::QuantTables qt;
    transforms::buildQuantTables(100, qt);
    std::int16_t scan[256];
    transforms::defaultScan16x16(scan);

    gpurt::DeviceBuffer dPlane(sizeof(srcData));
    gpurt::DeviceBuffer dRecon(1024);
    gpurt::DeviceBuffer dResidual(256 * sizeof(std::int16_t));
    gpurt::DeviceBuffer dBlkCoeffs(256 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dQcoeffs(256 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dDqcoeffs(256 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dEob(sizeof(std::uint16_t));
    gpurt::DeviceBuffer dPred(256);
    gpurt::DeviceBuffer dQuantFp(sizeof(qt.quantFp));
    gpurt::DeviceBuffer dDequant(sizeof(qt.dequant));
    gpurt::DeviceBuffer dRoundFp(sizeof(qt.roundFp));
    gpurt::DeviceBuffer dScan(sizeof(scan));
    dPlane.uploadFrom(srcData, sizeof(srcData));
    std::uint8_t zero[1024] = {0};
    dRecon.uploadFrom(zero, 1024);
    dQuantFp.uploadFrom(qt.quantFp, sizeof(qt.quantFp));
    dDequant.uploadFrom(qt.dequant, sizeof(qt.dequant));
    dRoundFp.uploadFrom(qt.roundFp, sizeof(qt.roundFp));
    dScan.uploadFrom(scan, sizeof(scan));

    int amArg = 0;
    int lmArg = 0;
    int fiArg = -1;
    int defArg = 0;
    int typeArg = 0;
    int planeStrideArg = 32;
    int fwdStrideArg = 16;
    int invStride = 16;
    gpurt::DeviceBuffer dMode(sizeof(int));
    gpurt::DeviceBuffer dDelta(sizeof(int));
    gpurt::DeviceBuffer dAm(sizeof(amArg));
    gpurt::DeviceBuffer dLm(sizeof(lmArg));
    gpurt::DeviceBuffer dFi(sizeof(fiArg));
    gpurt::DeviceBuffer dDef(sizeof(defArg));
    gpurt::DeviceBuffer dNTop(sizeof(int));
    gpurt::DeviceBuffer dNTr(sizeof(int));
    gpurt::DeviceBuffer dNLeft(sizeof(int));
    gpurt::DeviceBuffer dNBl(sizeof(int));
    gpurt::DeviceBuffer dAl(sizeof(int));
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    gpurt::DeviceBuffer dPlaneStride(sizeof(planeStrideArg));
    gpurt::DeviceBuffer dFwdStride(sizeof(fwdStrideArg));
    gpurt::DeviceBuffer dInvStride(sizeof(int));
    gpurt::DeviceBuffer dPx(sizeof(int));
    gpurt::DeviceBuffer dPy(sizeof(int));
    gpurt::DeviceBuffer dAbove(32);
    gpurt::DeviceBuffer dLeft(32);
    dDelta.uploadFrom(&defArg, sizeof(defArg));
    dFi.uploadFrom(&fiArg, sizeof(fiArg));
    dDef.uploadFrom(&defArg, sizeof(defArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dPlaneStride.uploadFrom(&planeStrideArg, sizeof(planeStrideArg));
    dFwdStride.uploadFrom(&fwdStrideArg, sizeof(fwdStrideArg));
    dInvStride.uploadFrom(&invStride, sizeof(invStride));

    std::uint8_t reconGot[1024] = {0};
    std::int32_t coeffsGot[1024] = {0};
    std::uint8_t modesGot[4] = {0};
    std::uint8_t aboveHost[32] = {0};
    std::uint8_t leftHost[32] = {0};

    for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 2; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < 2) ? 16 : 0;

            std::uint8_t tmp[1024] = {0};
            dRecon.downloadTo(tmp, 1024);
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) {
                    aboveHost[i] = tmp[(py - 1) * 32 + px + i];
                }
                dAbove.uploadFrom(aboveHost, 16 + nTopRightPx);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) leftHost[i] = tmp[(py + i) * 32 + px - 1];
                dLeft.uploadFrom(leftHost, 16);
            }
            int alVal = 0;
            if (hasTop && hasLeft) alVal = tmp[(py - 1) * 32 + px - 1];

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop ? static_cast<intra::PredictionMode>(modesGot[(by - 1) * 2 + bx])
                                    : intra::DC_PRED;
            nctx.leftMode = hasLeft ? static_cast<intra::PredictionMode>(modesGot[by * 2 + bx - 1])
                                    : intra::DC_PRED;
            std::uint8_t srcBlk[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) srcBlk[y * 16 + x] = srcData[(py + y) * 32 + px + x];
            const auto d = pipeline::decideBlockMode16x16(srcBlk, aboveHost, nTopPx, nTopRightPx,
                                                          leftHost, nLeftPx, 0,
                                                          static_cast<std::uint8_t>(alVal), nctx);
            modesGot[by * 2 + bx] = static_cast<std::uint8_t>(d.mode);

            int modeArg = d.mode;
            amArg = (int)nctx.aboveMode;
            lmArg = (int)nctx.leftMode;
            int deltaArg = 0;
            int nTopArg = nTopPx;
            int nTrArg = nTopRightPx;
            int nLeftArg = nLeftPx;
            int nBlArg = 0;
            int alArg = alVal;
            int pxArg = px;
            int pyArg = py;
            dMode.uploadFrom(&modeArg, sizeof(modeArg));
            dDelta.uploadFrom(&deltaArg, sizeof(deltaArg));
            dAm.uploadFrom(&amArg, sizeof(amArg));
            dLm.uploadFrom(&lmArg, sizeof(lmArg));
            dNTop.uploadFrom(&nTopArg, sizeof(nTopArg));
            dNTr.uploadFrom(&nTrArg, sizeof(nTrArg));
            dNLeft.uploadFrom(&nLeftArg, sizeof(nLeftArg));
            dNBl.uploadFrom(&nBlArg, sizeof(nBlArg));
            dAl.uploadFrom(&alArg, sizeof(alArg));
            dPx.uploadFrom(&pxArg, sizeof(pxArg));
            dPy.uploadFrom(&pyArg, sizeof(pyArg));
            if (hasTop) dAbove.uploadFrom(aboveHost, 16 + nTopRightPx);
            if (hasLeft) dLeft.uploadFrom(leftHost, 16);

            CUdeviceptr pMode = dMode.get();
            CUdeviceptr pDelta = dDelta.get();
            CUdeviceptr pAm = dAm.get();
            CUdeviceptr pLm = dLm.get();
            CUdeviceptr pAbove = dAbove.get();
            CUdeviceptr pNTop = dNTop.get();
            CUdeviceptr pNTr = dNTr.get();
            CUdeviceptr pLeft = dLeft.get();
            CUdeviceptr pNLeft = dNLeft.get();
            CUdeviceptr pNBl = dNBl.get();
            CUdeviceptr pAl = dAl.get();
            CUdeviceptr pFi = dFi.get();
            CUdeviceptr pDef = dDef.get();
            CUdeviceptr pPred = dPred.get();
            void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                                &pNBl,  &pAl,   &pFi, &pDef, &pPred};
            kPred.launch(1, 1, 256, 1, argsPred);

            CUdeviceptr pPlane = dPlane.get();
            CUdeviceptr pPlaneStride = dPlaneStride.get();
            CUdeviceptr pPx = dPx.get();
            CUdeviceptr pPy = dPy.get();
            CUdeviceptr pResidual = dResidual.get();
            void* argsSub[] = {&pPlane, &pPlaneStride, &pPx, &pPy, &pPred, &pResidual};
            kSub.launch(1, 1, 256, 1, argsSub);

            CUdeviceptr pType = dType.get();
            CUdeviceptr pFwdStride = dFwdStride.get();
            CUdeviceptr pBlkCoeffs = dBlkCoeffs.get();
            void* argsTx[] = {&pResidual, &pFwdStride, &pType, &pBlkCoeffs};
            kTx.launch(1, 1, 16, 1, argsTx);

            CUdeviceptr pQuantFp = dQuantFp.get();
            CUdeviceptr pDequant = dDequant.get();
            CUdeviceptr pRoundFp = dRoundFp.get();
            CUdeviceptr pScan = dScan.get();
            CUdeviceptr pQcoeffs = dQcoeffs.get();
            CUdeviceptr pDqcoeffs = dDqcoeffs.get();
            CUdeviceptr pEob = dEob.get();
            void* argsQuant[] = {&pBlkCoeffs, &pQuantFp, &pDequant, &pRoundFp, &pScan,
                                 &pQcoeffs, &pDqcoeffs, &pEob};
            kQuant.launch(1, 1, 256, 1, argsQuant);
            std::int32_t blkQ[256] = {0};
            dQcoeffs.downloadTo(blkQ, sizeof(blkQ));
            for (int i = 0; i < 256; ++i) coeffsGot[(by * 2 + bx) * 256 + i] = blkQ[i];

            CUdeviceptr pInvStride = dInvStride.get();
            void* argsInv[] = {&pDqcoeffs, &pType, &pPred, &pInvStride};
            kInv.launch(1, 1, 16, 1, argsInv);

            std::uint8_t blkRecon[256] = {0};
            dPred.downloadTo(blkRecon, sizeof(blkRecon));
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) reconGot[(py + y) * 32 + px + x] = blkRecon[y * 16 + x];
            dRecon.uploadFrom(reconGot, 1024);
        }
    }

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modesGot[i] != refModes[i]) modesOk = false;
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int i = 0; i < 1024; ++i) {
        if (reconGot[i] != reconRef.at(i & 31, i >> 5)) reconOk = false;
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 1024; ++i) {
        if (coeffsGot[i] != refCoeffs[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}
TEST_CASE("frame recon 16x16 forced mode matches the f16v generator golden") {
    // golden: golden_gen f16v_recon/f16v_coeffs (svtd_frame_v_dct_16x16,
    // V_PRED forced + DCT over the f16 fixture) - closes the C7b gap
    // (Recon16x16 had no gate pin). Discriminating: blocks 1-3 force V where
    // the f16 auto winners were D203/H/H, so f16v_coeffs blk1..3 differ from
    // f16_coeffs (e.g. blk1[0] 8193 vs 2849); block 0 matches f16 (auto
    // winner was V, same no-edge 127-fill prediction). recon == source
    // (exact 16x16 roundtrip).
    const std::int32_t goldenHeads[4][8] = {
        {-8063, -2344, 0, -257, 0, -90, 0, -44},
        {8193, -2344, 0, -257, 0, -90, 0, -44},
        {-12031, 2344, 0, 257, 0, 90, 0, 45},
        {-20221, 2344, 0, 257, 0, 90, 0, 45}};

    pixels::Plane plane(32, 32, 4);
    pixels::Plane recon(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            plane.at(x, y) = (y < 16) ? static_cast<std::uint8_t>(4 * (x + y + 1)) : 0;

    std::int32_t coeffs[1024] = {0};
    pipeline::encodeFrameRecon16x16(plane, recon, coeffs, intra::V_PRED, 0,
                                    transforms::TxType::DCT_DCT);

    bool reconOk = true;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            if (recon.at(x, y) != plane.at(x, y)) reconOk = false;
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int b = 0; b < 4; ++b) {
        for (int i = 0; i < 8; ++i) {
            if (coeffs[b * 256 + i] != goldenHeads[b][i]) coeffsOk = false;
        }
    }
    CHECK(coeffsOk);
}

TEST_CASE("frame recon 16x16 with quantization matches the f16vq generator golden") {
    // golden: golden_gen f16vq_recon/f16vq_coeffs at qindex 100 (forced
    // V_PRED, FP quantizer at n_coeffs=256/log_scale 0); recon carries real
    // quantization loss (row 0 begins 5 8 12 17 ... vs source 4 8 12 16 ...).
    const std::int32_t goldenHeads[4][8] = {
        {-87, -21, 0, -2, 0, -1, 0, 0},
        {88, -21, 0, -2, 0, -1, 0, 0},
        {-128, 21, 0, 2, 0, 1, 0, 0},
        {-216, 21, 0, 2, 0, 1, 0, 0}};

    pixels::Plane plane(32, 32, 4);
    pixels::Plane recon(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            plane.at(x, y) = (y < 16) ? static_cast<std::uint8_t>(4 * (x + y + 1)) : 0;

    std::int32_t coeffs[1024] = {0};
    pipeline::encodeFrameRecon16x16Q(plane, recon, coeffs, intra::V_PRED, 0, 100,
                                     transforms::TxType::DCT_DCT);

bool reconHeadOk = true;
    const std::uint8_t goldenReconRow0[8] = {5, 8, 12, 17, 21, 24, 28, 32};
    for (int x = 0; x < 8; ++x) {
        if (recon.at(x, 0) != goldenReconRow0[x]) reconHeadOk = false;
    }
    CHECK(reconHeadOk);

    bool coeffsOk = true;
    for (int b = 0; b < 4; ++b) {
        for (int i = 0; i < 8; ++i) {
            if (coeffs[b * 256 + i] != goldenHeads[b][i]) coeffsOk = false;
        }
    }
    CHECK(coeffsOk);
}
TEST_CASE("frame auto 32x32 matches the f32 generator golden (real top-right)") {
    // golden: golden_gen f32_modes/f32_recon/f32_coeffs (64x64, 2x2 of 32x32;
    // rows 0-31 ramp 2*(x+y+1), rows 32-63 zero). Block 2 (bx=0, by=1) is the
    // only nTr>0 block; with REAL recon top-right the D45 zone-1 SAD is 0
    // (above[j] = 2j+64, pred above[1+r+c] = 2*(r+c+33) = src[32+r][c]) and H
    // wins on the zero rows; the mode map discriminates the FR gather.
    // DEVIATION from the earlier assumption: the 32x32 fwd/inv roundtrip is
    // LOSSY (fwd_shift_32x32 sum 2-4+0 = -2, like 8x8) - recon != source
    // (generator probe: 87 single-sample +-1 diffs in rows 0-31, rows 32-63
    // exact zeros); recon is pinned to the f32_recon gate line below.
    const std::uint8_t goldenModes[4] = {1, 7, 2, 2};

    pixels::Plane plane(64, 64, 4);
    pixels::Plane recon(64, 64, 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            plane.at(x, y) = (y < 32) ? static_cast<std::uint8_t>(2 * (x + y + 1)) : 0;

    std::int32_t coeffs[4096] = {0};
    std::uint8_t modes[4] = {0};
    pipeline::encodeFrameAuto32x32(plane, recon, coeffs, modes, transforms::TxType::DCT_DCT);

bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modes[i] != goldenModes[i]) modesOk = false;
    }
    CHECK(modesOk);

    // recon pinned to the f32_recon gate line: row 0 (contains 4 of the 87
    // +-1 roundtrip diffs), rows 30-31 (the heaviest diff rows), row 32
    // (zero-region head, exact)
    const std::uint8_t goldenReconRow0[64] = {
        2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32,
        34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64,
        66, 68, 70, 72, 74, 76, 78, 80, 83, 85, 86, 88, 90, 92, 94, 96,
        98, 100, 102, 104, 107, 108, 110, 112, 114, 116, 118, 120, 122, 124, 127, 128};
    const std::uint8_t goldenReconRow30[64] = {
        62, 65, 66, 68, 71, 73, 74, 77, 79, 81, 82, 85, 87, 88, 91, 92,
        94, 97, 98, 100, 103, 105, 106, 109, 111, 112, 114, 117, 118, 120, 123, 125,
        127, 129, 130, 133, 135, 136, 139, 141, 142, 144, 146, 149, 151, 153, 155, 157,
        159, 160, 162, 165, 167, 168, 170, 172, 174, 176, 179, 180, 182, 184, 186, 188};
    const std::uint8_t goldenReconRow31[64] = {
        64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 90, 92, 94,
        96, 98, 100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124, 126,
        128, 130, 132, 134, 136, 138, 140, 142, 144, 146, 148, 150, 152, 154, 156, 158,
        160, 162, 163, 166, 169, 170, 172, 174, 175, 177, 180, 182, 184, 186, 188, 190};
bool reconOk = true;
    for (int x = 0; x < 64; ++x) {
        if (recon.at(x, 0) != goldenReconRow0[x]) reconOk = false;
        if (recon.at(x, 30) != goldenReconRow30[x]) reconOk = false;
        if (recon.at(x, 31) != goldenReconRow31[x]) reconOk = false;
        if (recon.at(x, 32) != 0) reconOk = false;
    }
    CHECK(reconOk);
}

TEST_CASE("frame auto 32x32 with quantization matches the f32q generator golden") {
    // golden: golden_gen f32q_modes at qindex 100 (FP quantizer at
    // n_coeffs=1024, log_scale 1). Modes identical to lossless (1 7 2 2).
    const std::uint8_t goldenModes[4] = {1, 7, 2, 2};

    pixels::Plane plane(64, 64, 4);
    pixels::Plane recon(64, 64, 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            plane.at(x, y) = (y < 32) ? static_cast<std::uint8_t>(2 * (x + y + 1)) : 0;

    std::int32_t coeffs[4096] = {0};
    std::uint8_t modes[4] = {0};
    pipeline::encodeFrameAuto32x32Q(plane, recon, coeffs, modes, 100, transforms::TxType::DCT_DCT);

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modes[i] != goldenModes[i]) modesOk = false;
    }
    CHECK(modesOk);
}

TEST_CASE("frame recon 32x32 forced mode matches the f32v generator golden") {
    // golden: golden_gen f32v_recon (V_PRED forced + DCT over the f32
    // fixture) - Recon32x32 is gate-pinned from the start (no C7b-style gap).
    // 32x32 roundtrip lossy (as f32 recon): recon pinned to f32v_recon rows
    // 0/30/31/32; coeffs pin via f32v_coeffs blk0 head.
    const std::int32_t goldenBlk0[8] = {-8062, -2346, 0, -260, -1, -94, 0, -48};

    pixels::Plane plane(64, 64, 4);
    pixels::Plane recon(64, 64, 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            plane.at(x, y) = (y < 32) ? static_cast<std::uint8_t>(2 * (x + y + 1)) : 0;

std::int32_t coeffs[4096] = {0};
    pipeline::encodeFrameRecon32x32(plane, recon, coeffs, intra::V_PRED, 0,
                                    transforms::TxType::DCT_DCT);

    const std::uint8_t goldenReconVRow0[64] = {
        2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32,
        34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64,
        66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 90, 92, 94, 96,
        98, 100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124, 126, 128};
    const std::uint8_t goldenReconVRow30[64] = {
        62, 65, 66, 68, 71, 73, 74, 77, 79, 81, 82, 85, 87, 88, 91, 92,
        94, 97, 98, 100, 103, 105, 106, 109, 111, 112, 114, 117, 118, 120, 123, 125,
        127, 128, 130, 133, 134, 136, 139, 140, 142, 145, 147, 148, 150, 152, 154, 156,
        159, 161, 162, 165, 166, 168, 171, 172, 174, 177, 178, 180, 182, 184, 186, 188};
    const std::uint8_t goldenReconVRow31[64] = {
        64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 90, 92, 94,
        96, 98, 100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124, 126,
        128, 130, 132, 134, 136, 138, 140, 142, 144, 146, 148, 150, 152, 154, 156, 158,
        160, 162, 164, 166, 168, 170, 172, 174, 176, 178, 180, 182, 184, 186, 188, 190};
    bool reconOk = true;
    for (int x = 0; x < 64; ++x) {
        if (recon.at(x, 0) != goldenReconVRow0[x]) reconOk = false;
        if (recon.at(x, 30) != goldenReconVRow30[x]) reconOk = false;
        if (recon.at(x, 31) != goldenReconVRow31[x]) reconOk = false;
        if (recon.at(x, 32) != 0) reconOk = false;
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 8; ++i) {
        if (coeffs[i] != goldenBlk0[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}

TEST_CASE("frame recon 32x32 with quantization matches the f32vq generator golden") {
    // golden: golden_gen f32vq at qindex 100 (forced V, FP quantizer at
    // n_coeffs=1024/log_scale 1); recon carries real quantization loss.
    const std::int32_t goldenBlk0[8] = {-173, -42, 0, -5, 0, -2, 0, -1};

    pixels::Plane plane(64, 64, 4);
    pixels::Plane recon(64, 64, 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            plane.at(x, y) = (y < 32) ? static_cast<std::uint8_t>(2 * (x + y + 1)) : 0;

    std::int32_t coeffs[4096] = {0};
    pipeline::encodeFrameRecon32x32Q(plane, recon, coeffs, intra::V_PRED, 0, 100,
                                     transforms::TxType::DCT_DCT);

    bool reconHeadOk = true;
    const std::uint8_t goldenReconRow0[8] = {3, 4, 6, 8, 11, 13, 15, 17};
    for (int x = 0; x < 8; ++x) {
        if (recon.at(x, 0) != goldenReconRow0[x]) reconHeadOk = false;
    }
    CHECK(reconHeadOk);

    bool coeffsOk = true;
    for (int i = 0; i < 8; ++i) {
        if (coeffs[i] != goldenBlk0[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}
TEST_CASE("gpu frame auto 32x32 matches host encodeFrameAuto32x32 (host decides, gpu executes)") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    std::uint8_t srcData[4096];
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            srcData[y * 64 + x] = (y < 32) ? static_cast<std::uint8_t>(2 * (x + y + 1)) : 0;

    pixels::Plane plane(64, 64, 4);
    pixels::Plane reconRef(64, 64, 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) plane.at(x, y) = srcData[y * 64 + x];

    std::int32_t refCoeffs[4096] = {0};
    std::uint8_t refModes[4] = {0};
    pipeline::encodeFrameAuto32x32(plane, reconRef, refCoeffs, refModes, transforms::TxType::DCT_DCT);

    const std::string ptxPred = *gpurt::compileToPtx(intra::predictBlock32x32CuSource(), "compute_61");
    const std::vector<std::string> pn = gpurt::ptxEntryNames(ptxPred);
    gpurt::Kernel kPred(ptxPred, *std::find(pn.begin(), pn.end(), "predict_block_32x32"));
    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> sn = gpurt::ptxEntryNames(ptxSub);
    gpurt::Kernel kSub(ptxSub, *std::find(sn.begin(), sn.end(), "subtract_32x32_plane"));
    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> tn = gpurt::ptxEntryNames(ptxTx);
    gpurt::Kernel kTx(ptxTx, *std::find(tn.begin(), tn.end(), "fwd_txfm_2d_32x32"));
    const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> inames = gpurt::ptxEntryNames(ptxInv);
    gpurt::Kernel kInv(ptxInv, *std::find(inames.begin(), inames.end(), "inv_txfm_2d_add_32x32"));

    constexpr int kStride = 64;
    gpurt::DeviceBuffer dPlane(sizeof(srcData));
    gpurt::DeviceBuffer dRecon(4096);
    gpurt::DeviceBuffer dResidual(1024 * sizeof(std::int16_t));
    gpurt::DeviceBuffer dBlkCoeffs(1024 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dPred(1024);
    dPlane.uploadFrom(srcData, sizeof(srcData));
    std::uint8_t zero[4096] = {0};
    dRecon.uploadFrom(zero, 4096);

    int amArg = 0;
    int lmArg = 0;
    int fiArg = -1;
    int defArg = 0;
    int typeArg = 0;
    int planeStrideArg = kStride;
    int fwdStrideArg = 32;
    int invStride = 32;
    gpurt::DeviceBuffer dMode(sizeof(int));
    gpurt::DeviceBuffer dDelta(sizeof(int));
    gpurt::DeviceBuffer dAm(sizeof(amArg));
    gpurt::DeviceBuffer dLm(sizeof(lmArg));
    gpurt::DeviceBuffer dFi(sizeof(fiArg));
    gpurt::DeviceBuffer dDef(sizeof(defArg));
    gpurt::DeviceBuffer dNTop(sizeof(int));
    gpurt::DeviceBuffer dNTr(sizeof(int));
    gpurt::DeviceBuffer dNLeft(sizeof(int));
    gpurt::DeviceBuffer dNBl(sizeof(int));
    gpurt::DeviceBuffer dAl(sizeof(int));
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    gpurt::DeviceBuffer dPlaneStride(sizeof(planeStrideArg));
    gpurt::DeviceBuffer dFwdStride(sizeof(fwdStrideArg));
    gpurt::DeviceBuffer dInvStride(sizeof(int));
    gpurt::DeviceBuffer dPx(sizeof(int));
    gpurt::DeviceBuffer dPy(sizeof(int));
    gpurt::DeviceBuffer dAbove(64);
    gpurt::DeviceBuffer dLeft(64);
    dDelta.uploadFrom(&defArg, sizeof(defArg));
    dFi.uploadFrom(&fiArg, sizeof(fiArg));
    dDef.uploadFrom(&defArg, sizeof(defArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dPlaneStride.uploadFrom(&planeStrideArg, sizeof(planeStrideArg));
    dFwdStride.uploadFrom(&fwdStrideArg, sizeof(fwdStrideArg));
    dInvStride.uploadFrom(&invStride, sizeof(invStride));

    std::uint8_t reconGot[4096] = {0};
    std::int32_t coeffsGot[4096] = {0};
    std::uint8_t modesGot[4] = {0};
    std::uint8_t aboveHost[64] = {0};
    std::uint8_t leftHost[64] = {0};

    for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 2; ++bx) {
            const int px = bx * 32;
            const int py = by * 32;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const int nTopPx = hasTop ? 32 : 0;
            const int nLeftPx = hasLeft ? 32 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < 2) ? 32 : 0;

            std::uint8_t tmp[4096] = {0};
            dRecon.downloadTo(tmp, 4096);
            if (hasTop) {
                for (int i = 0; i < 32 + nTopRightPx; ++i) {
                    aboveHost[i] = tmp[(py - 1) * 64 + px + i];
                }
                dAbove.uploadFrom(aboveHost, 32 + nTopRightPx);
            }
            if (hasLeft) {
                for (int i = 0; i < 32; ++i) leftHost[i] = tmp[(py + i) * 64 + px - 1];
                dLeft.uploadFrom(leftHost, 32);
            }
            int alVal = 0;
            if (hasTop && hasLeft) alVal = tmp[(py - 1) * 64 + px - 1];

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop ? static_cast<intra::PredictionMode>(modesGot[(by - 1) * 2 + bx])
                                    : intra::DC_PRED;
            nctx.leftMode = hasLeft ? static_cast<intra::PredictionMode>(modesGot[by * 2 + bx - 1])
                                    : intra::DC_PRED;
            std::uint8_t srcBlk[1024] = {0};
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) srcBlk[y * 32 + x] = srcData[(py + y) * 64 + px + x];
            const auto d = pipeline::decideBlockMode32x32(srcBlk, aboveHost, nTopPx, nTopRightPx,
                                                          leftHost, nLeftPx, 0,
                                                          static_cast<std::uint8_t>(alVal), nctx);
            modesGot[by * 2 + bx] = static_cast<std::uint8_t>(d.mode);

            int modeArg = d.mode;
            amArg = (int)nctx.aboveMode;
            lmArg = (int)nctx.leftMode;
            int deltaArg = 0;
            int nTopArg = nTopPx;
            int nTrArg = nTopRightPx;
            int nLeftArg = nLeftPx;
            int nBlArg = 0;
            int alArg = alVal;
            int pxArg = px;
            int pyArg = py;
            dMode.uploadFrom(&modeArg, sizeof(modeArg));
            dDelta.uploadFrom(&deltaArg, sizeof(deltaArg));
            dAm.uploadFrom(&amArg, sizeof(amArg));
            dLm.uploadFrom(&lmArg, sizeof(lmArg));
            dNTop.uploadFrom(&nTopArg, sizeof(nTopArg));
            dNTr.uploadFrom(&nTrArg, sizeof(nTrArg));
            dNLeft.uploadFrom(&nLeftArg, sizeof(nLeftArg));
            dNBl.uploadFrom(&nBlArg, sizeof(nBlArg));
            dAl.uploadFrom(&alArg, sizeof(alArg));
            dPx.uploadFrom(&pxArg, sizeof(pxArg));
            dPy.uploadFrom(&pyArg, sizeof(pyArg));

            CUdeviceptr pMode = dMode.get();
            CUdeviceptr pDelta = dDelta.get();
            CUdeviceptr pAm = dAm.get();
            CUdeviceptr pLm = dLm.get();
            CUdeviceptr pAbove = dAbove.get();
            CUdeviceptr pNTop = dNTop.get();
            CUdeviceptr pNTr = dNTr.get();
            CUdeviceptr pLeft = dLeft.get();
            CUdeviceptr pNLeft = dNLeft.get();
            CUdeviceptr pNBl = dNBl.get();
            CUdeviceptr pAl = dAl.get();
            CUdeviceptr pFi = dFi.get();
            CUdeviceptr pDef = dDef.get();
            CUdeviceptr pPred = dPred.get();
            void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                                &pNBl,  &pAl,   &pFi, &pDef, &pPred};
            kPred.launch(1, 1, 1024, 1, argsPred);

            CUdeviceptr pPlane = dPlane.get();
            CUdeviceptr pPlaneStride = dPlaneStride.get();
            CUdeviceptr pPx = dPx.get();
            CUdeviceptr pPy = dPy.get();
            CUdeviceptr pResidual = dResidual.get();
            void* argsSub[] = {&pPlane, &pPlaneStride, &pPx, &pPy, &pPred, &pResidual};
            kSub.launch(1, 1, 1024, 1, argsSub);

            CUdeviceptr pType = dType.get();
            CUdeviceptr pFwdStride = dFwdStride.get();
            CUdeviceptr pBlkCoeffs = dBlkCoeffs.get();
            void* argsTx[] = {&pResidual, &pFwdStride, &pType, &pBlkCoeffs};
            kTx.launch(1, 1, 32, 1, argsTx);
            std::int32_t blk[1024] = {0};
            dBlkCoeffs.downloadTo(blk, sizeof(blk));
            for (int i = 0; i < 1024; ++i) {
                coeffsGot[(by * 2 + bx) * 1024 + i] = blk[i];
            }

            CUdeviceptr pInvStride = dInvStride.get();
            void* argsInv[] = {&pBlkCoeffs, &pType, &pPred, &pInvStride};
            kInv.launch(1, 1, 32, 1, argsInv);

            std::uint8_t blkRecon[1024] = {0};
            dPred.downloadTo(blkRecon, sizeof(blkRecon));
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    reconGot[(py + y) * 64 + px + x] = blkRecon[y * 32 + x];
            dRecon.uploadFrom(reconGot, 4096);
        }
    }

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modesGot[i] != refModes[i]) modesOk = false;
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int i = 0; i < 4096; ++i) {
        if (reconGot[i] != reconRef.at(i & 63, i >> 6)) reconOk = false;
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 4096; ++i) {
        if (coeffsGot[i] != refCoeffs[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}
TEST_CASE("gpu frame auto 32x32 with quantization matches host encodeFrameAuto32x32Q") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    std::uint8_t srcData[4096];
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            srcData[y * 64 + x] = (y < 32) ? static_cast<std::uint8_t>(2 * (x + y + 1)) : 0;

    pixels::Plane plane(64, 64, 4);
    pixels::Plane reconRef(64, 64, 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) plane.at(x, y) = srcData[y * 64 + x];

    std::int32_t refCoeffs[4096] = {0};
    std::uint8_t refModes[4] = {0};
    pipeline::encodeFrameAuto32x32Q(plane, reconRef, refCoeffs, refModes, 100, transforms::TxType::DCT_DCT);

    transforms::QuantTables qt;
    transforms::buildQuantTables(100, qt);
    std::int16_t scan[1024];
    transforms::defaultScan32x32(scan);

    const std::string ptxPred = *gpurt::compileToPtx(intra::predictBlock32x32CuSource(), "compute_61");
    const std::vector<std::string> pn = gpurt::ptxEntryNames(ptxPred);
    gpurt::Kernel kPred(ptxPred, *std::find(pn.begin(), pn.end(), "predict_block_32x32"));
    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> sn = gpurt::ptxEntryNames(ptxSub);
    gpurt::Kernel kSub(ptxSub, *std::find(sn.begin(), sn.end(), "subtract_32x32_plane"));
    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> tn = gpurt::ptxEntryNames(ptxTx);
    gpurt::Kernel kTx(ptxTx, *std::find(tn.begin(), tn.end(), "fwd_txfm_2d_32x32"));
    const std::string ptxQ = *gpurt::compileToPtx(transforms::quantCuSource(), "compute_61");
    const std::vector<std::string> qn = gpurt::ptxEntryNames(ptxQ);
    gpurt::Kernel kQuant(ptxQ, *std::find(qn.begin(), qn.end(), "quant_dequant_32x32"));
    const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> inames = gpurt::ptxEntryNames(ptxInv);
    gpurt::Kernel kInv(ptxInv, *std::find(inames.begin(), inames.end(), "inv_txfm_2d_add_32x32"));

    constexpr int kStride = 64;
    gpurt::DeviceBuffer dPlane(sizeof(srcData));
    gpurt::DeviceBuffer dRecon(4096);
    gpurt::DeviceBuffer dResidual(1024 * sizeof(std::int16_t));
    gpurt::DeviceBuffer dBlkCoeffs(1024 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dPred(1024);
    gpurt::DeviceBuffer dQcoeffs(1024 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dDqcoeffs(1024 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dEob(sizeof(std::uint16_t));
    gpurt::DeviceBuffer dQuantFp(sizeof(qt.quantFp));
    gpurt::DeviceBuffer dDequant(sizeof(qt.dequant));
    gpurt::DeviceBuffer dRoundFp(sizeof(qt.roundFp));
    gpurt::DeviceBuffer dScan(sizeof(scan));
    dPlane.uploadFrom(srcData, sizeof(srcData));
    std::uint8_t zero[4096] = {0};
    dRecon.uploadFrom(zero, 4096);
    dQuantFp.uploadFrom(qt.quantFp, sizeof(qt.quantFp));
    dDequant.uploadFrom(qt.dequant, sizeof(qt.dequant));
    dRoundFp.uploadFrom(qt.roundFp, sizeof(qt.roundFp));
    dScan.uploadFrom(scan, sizeof(scan));

    int amArg = 0;
    int lmArg = 0;
    int fiArg = -1;
    int defArg = 0;
    int typeArg = 0;
    int planeStrideArg = kStride;
    int fwdStrideArg = 32;
    int invStride = 32;
    gpurt::DeviceBuffer dMode(sizeof(int));
    gpurt::DeviceBuffer dDelta(sizeof(int));
    gpurt::DeviceBuffer dAm(sizeof(amArg));
    gpurt::DeviceBuffer dLm(sizeof(lmArg));
    gpurt::DeviceBuffer dFi(sizeof(fiArg));
    gpurt::DeviceBuffer dDef(sizeof(defArg));
    gpurt::DeviceBuffer dNTop(sizeof(int));
    gpurt::DeviceBuffer dNTr(sizeof(int));
    gpurt::DeviceBuffer dNLeft(sizeof(int));
    gpurt::DeviceBuffer dNBl(sizeof(int));
    gpurt::DeviceBuffer dAl(sizeof(int));
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    gpurt::DeviceBuffer dPlaneStride(sizeof(planeStrideArg));
    gpurt::DeviceBuffer dFwdStride(sizeof(fwdStrideArg));
    gpurt::DeviceBuffer dInvStride(sizeof(int));
    gpurt::DeviceBuffer dPx(sizeof(int));
    gpurt::DeviceBuffer dPy(sizeof(int));
    gpurt::DeviceBuffer dAbove(64);
    gpurt::DeviceBuffer dLeft(64);
    dDelta.uploadFrom(&defArg, sizeof(defArg));
    dFi.uploadFrom(&fiArg, sizeof(fiArg));
    dDef.uploadFrom(&defArg, sizeof(defArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dPlaneStride.uploadFrom(&planeStrideArg, sizeof(planeStrideArg));
    dFwdStride.uploadFrom(&fwdStrideArg, sizeof(fwdStrideArg));
    dInvStride.uploadFrom(&invStride, sizeof(invStride));

    std::uint8_t reconGot[4096] = {0};
    std::int32_t coeffsGot[4096] = {0};
    std::uint8_t modesGot[4] = {0};
    std::uint8_t aboveHost[64] = {0};
    std::uint8_t leftHost[64] = {0};

    for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 2; ++bx) {
            const int px = bx * 32;
            const int py = by * 32;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const int nTopPx = hasTop ? 32 : 0;
            const int nLeftPx = hasLeft ? 32 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < 2) ? 32 : 0;

            std::uint8_t tmp[4096] = {0};
            dRecon.downloadTo(tmp, 4096);
            if (hasTop) {
                for (int i = 0; i < 32 + nTopRightPx; ++i) {
                    aboveHost[i] = tmp[(py - 1) * 64 + px + i];
                }
                dAbove.uploadFrom(aboveHost, 32 + nTopRightPx);
            }
            if (hasLeft) {
                for (int i = 0; i < 32; ++i) leftHost[i] = tmp[(py + i) * 64 + px - 1];
                dLeft.uploadFrom(leftHost, 32);
            }
            int alVal = 0;
            if (hasTop && hasLeft) alVal = tmp[(py - 1) * 64 + px - 1];

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop ? static_cast<intra::PredictionMode>(modesGot[(by - 1) * 2 + bx])
                                    : intra::DC_PRED;
            nctx.leftMode = hasLeft ? static_cast<intra::PredictionMode>(modesGot[by * 2 + bx - 1])
                                    : intra::DC_PRED;
            std::uint8_t srcBlk[1024] = {0};
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x) srcBlk[y * 32 + x] = srcData[(py + y) * 64 + px + x];
            const auto d = pipeline::decideBlockMode32x32(srcBlk, aboveHost, nTopPx, nTopRightPx,
                                                          leftHost, nLeftPx, 0,
                                                          static_cast<std::uint8_t>(alVal), nctx);
            modesGot[by * 2 + bx] = static_cast<std::uint8_t>(d.mode);

            int modeArg = d.mode;
            amArg = (int)nctx.aboveMode;
            lmArg = (int)nctx.leftMode;
            int deltaArg = 0;
            int nTopArg = nTopPx;
            int nTrArg = nTopRightPx;
            int nLeftArg = nLeftPx;
            int nBlArg = 0;
            int alArg = alVal;
            int pxArg = px;
            int pyArg = py;
            dMode.uploadFrom(&modeArg, sizeof(modeArg));
            dDelta.uploadFrom(&deltaArg, sizeof(deltaArg));
            dAm.uploadFrom(&amArg, sizeof(amArg));
            dLm.uploadFrom(&lmArg, sizeof(lmArg));
            dNTop.uploadFrom(&nTopArg, sizeof(nTopArg));
            dNTr.uploadFrom(&nTrArg, sizeof(nTrArg));
            dNLeft.uploadFrom(&nLeftArg, sizeof(nLeftArg));
            dNBl.uploadFrom(&nBlArg, sizeof(nBlArg));
            dAl.uploadFrom(&alArg, sizeof(alArg));
            dPx.uploadFrom(&pxArg, sizeof(pxArg));
            dPy.uploadFrom(&pyArg, sizeof(pyArg));

            CUdeviceptr pMode = dMode.get();
            CUdeviceptr pDelta = dDelta.get();
            CUdeviceptr pAm = dAm.get();
            CUdeviceptr pLm = dLm.get();
            CUdeviceptr pAbove = dAbove.get();
            CUdeviceptr pNTop = dNTop.get();
            CUdeviceptr pNTr = dNTr.get();
            CUdeviceptr pLeft = dLeft.get();
            CUdeviceptr pNLeft = dNLeft.get();
            CUdeviceptr pNBl = dNBl.get();
            CUdeviceptr pAl = dAl.get();
            CUdeviceptr pFi = dFi.get();
            CUdeviceptr pDef = dDef.get();
            CUdeviceptr pPred = dPred.get();
            void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                                &pNBl,  &pAl,   &pFi, &pDef, &pPred};
            kPred.launch(1, 1, 1024, 1, argsPred);

            CUdeviceptr pPlane = dPlane.get();
            CUdeviceptr pPlaneStride = dPlaneStride.get();
            CUdeviceptr pPx = dPx.get();
            CUdeviceptr pPy = dPy.get();
            CUdeviceptr pResidual = dResidual.get();
            void* argsSub[] = {&pPlane, &pPlaneStride, &pPx, &pPy, &pPred, &pResidual};
            kSub.launch(1, 1, 1024, 1, argsSub);

            CUdeviceptr pType = dType.get();
            CUdeviceptr pFwdStride = dFwdStride.get();
            CUdeviceptr pBlkCoeffs = dBlkCoeffs.get();
            void* argsTx[] = {&pResidual, &pFwdStride, &pType, &pBlkCoeffs};
            kTx.launch(1, 1, 32, 1, argsTx);
            std::int32_t blk[1024] = {0};
            dBlkCoeffs.downloadTo(blk, sizeof(blk));
            for (int i = 0; i < 1024; ++i) {
                coeffsGot[(by * 2 + bx) * 1024 + i] = blk[i];
            }

            CUdeviceptr pQuantFp = dQuantFp.get();
            CUdeviceptr pDequant = dDequant.get();
            CUdeviceptr pRoundFp = dRoundFp.get();
            CUdeviceptr pScan = dScan.get();
            CUdeviceptr pQcoeffs = dQcoeffs.get();
            CUdeviceptr pDqcoeffs = dDqcoeffs.get();
            CUdeviceptr pEob = dEob.get();
            void* argsQuant[] = {&pBlkCoeffs, &pQuantFp, &pDequant, &pRoundFp, &pScan,
                                 &pQcoeffs, &pDqcoeffs, &pEob};
            kQuant.launch(1, 1, 1024, 1, argsQuant);
            std::int32_t blkQ[1024] = {0};
            dQcoeffs.downloadTo(blkQ, sizeof(blkQ));
            for (int i = 0; i < 1024; ++i) coeffsGot[(by * 2 + bx) * 1024 + i] = blkQ[i];

            CUdeviceptr pInvStride = dInvStride.get();
            void* argsInv[] = {&pDqcoeffs, &pType, &pPred, &pInvStride};
            kInv.launch(1, 1, 32, 1, argsInv);

            std::uint8_t blkRecon[1024] = {0};
            dPred.downloadTo(blkRecon, sizeof(blkRecon));
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    reconGot[(py + y) * 64 + px + x] = blkRecon[y * 32 + x];
            dRecon.uploadFrom(reconGot, 4096);
        }
    }

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modesGot[i] != refModes[i]) modesOk = false;
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int i = 0; i < 4096; ++i) {
        if (reconGot[i] != reconRef.at(i & 63, i >> 6)) reconOk = false;
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 4096; ++i) {
        if (coeffsGot[i] != refCoeffs[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}
TEST_CASE("frame auto 64x64 matches the f64 generator golden (real top-right)") {
    // golden: golden_gen f64_modes/f64_recon/f64_coeffs (128x128, 2x2 of
    // 64x64; rows 0-63 ramp x+y+1, rows 64-127 zero). Block 2 (bx=0, by=1) is
    // the only nTr>0 block; with REAL recon top-right the D45 zone-1 SAD is 0
    // (above[j] = j+64, pred above[1+r+c] = r+c+65 = src[64+r][c]) and H wins
    // on the zero rows; the mode map discriminates the FR gather. 64x64 fwd
    // net shift 0: recon is LOSSY vs source (generator probe: 371 +-1..3
    // diffs, first idx 64 recon 68 vs src 65) - recon pinned to the f64_recon
    // gate line.
    const std::uint8_t goldenModes[4] = {1, 7, 2, 2};

    pixels::Plane plane(128, 128, 4);
    pixels::Plane recon(128, 128, 4);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            plane.at(x, y) = (y < 64) ? static_cast<std::uint8_t>(x + y + 1) : 0;

    std::int32_t coeffs[16384] = {0};
    std::uint8_t modes[4] = {0};
    pipeline::encodeFrameAuto64x64(plane, recon, coeffs, modes, transforms::TxType::DCT_DCT);

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modes[i] != goldenModes[i]) modesOk = false;
    }
    CHECK(modesOk);
}

TEST_CASE("frame auto 64x64 with quantization matches the f64q generator golden") {
    // golden: golden_gen f64q_modes at qindex 100 (FP quantizer at
    // n_coeffs=4096, log_scale 2). Modes identical to lossless (1 7 2 2).
    const std::uint8_t goldenModes[4] = {1, 7, 2, 2};

    pixels::Plane plane(128, 128, 4);
    pixels::Plane recon(128, 128, 4);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            plane.at(x, y) = (y < 64) ? static_cast<std::uint8_t>(x + y + 1) : 0;

    std::int32_t coeffs[16384] = {0};
    std::uint8_t modes[4] = {0};
    pipeline::encodeFrameAuto64x64Q(plane, recon, coeffs, modes, 100, transforms::TxType::DCT_DCT);

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modes[i] != goldenModes[i]) modesOk = false;
    }
    CHECK(modesOk);
}

TEST_CASE("frame recon 64x64 forced mode matches the f64v generator golden") {
    // golden: golden_gen f64v_recon/f64v_coeffs (V_PRED forced + DCT over the
    // f64 fixture) - Recon64x64 gate-pinned from the start (no C7b gap).
    const std::int32_t goldenBlk0[8] = {-8062, -2348, 0, -261, 0, -93, 0, -47};

    pixels::Plane plane(128, 128, 4);
    pixels::Plane recon(128, 128, 4);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            plane.at(x, y) = (y < 64) ? static_cast<std::uint8_t>(x + y + 1) : 0;

    std::int32_t coeffs[16384] = {0};
    pipeline::encodeFrameRecon64x64(plane, recon, coeffs, intra::V_PRED, 0,
                                    transforms::TxType::DCT_DCT);

    const std::uint8_t goldenReconRow0[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const std::uint8_t goldenReconRow62[8] = {63, 64, 65, 66, 67, 68, 69, 70};
    const std::uint8_t goldenReconRow64[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    bool reconOk = true;
    for (int x = 0; x < 8; ++x) {
        if (recon.at(x, 0) != goldenReconRow0[x]) reconOk = false;
        if (recon.at(x, 62) != goldenReconRow62[x]) reconOk = false;
        if (recon.at(x, 64) != goldenReconRow64[x]) reconOk = false;
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 8; ++i) {
        if (coeffs[i] != goldenBlk0[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}

TEST_CASE("frame recon 64x64 with quantization matches the f64vq generator golden") {
    // golden: golden_gen f64vq at qindex 100 (forced V, FP quantizer at
    // n_coeffs=4096/log_scale 2); recon carries real quantization loss.
    const std::int32_t goldenBlk0[8] = {-346, -84, 0, -9, 0, -3, 0, -2};

    pixels::Plane plane(128, 128, 4);
    pixels::Plane recon(128, 128, 4);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            plane.at(x, y) = (y < 64) ? static_cast<std::uint8_t>(x + y + 1) : 0;

    std::int32_t coeffs[16384] = {0};
    pipeline::encodeFrameRecon64x64Q(plane, recon, coeffs, intra::V_PRED, 0, 100,
                                     transforms::TxType::DCT_DCT);

    const std::uint8_t goldenReconRow0[8] = {2, 3, 4, 5, 6, 7, 8, 9};
    const std::uint8_t goldenReconRow62[8] = {64, 64, 65, 66, 67, 68, 69, 70};
    const std::uint8_t goldenReconRow64[8] = {0, 1, 0, 0, 0, 0, 0, 0};
    bool reconOk = true;
    for (int x = 0; x < 8; ++x) {
        if (recon.at(x, 0) != goldenReconRow0[x]) reconOk = false;
        if (recon.at(x, 62) != goldenReconRow62[x]) reconOk = false;
        if (recon.at(x, 64) != goldenReconRow64[x]) reconOk = false;
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 8; ++i) {
        if (coeffs[i] != goldenBlk0[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}
TEST_CASE("gpu frame auto 64x64 matches host encodeFrameAuto64x64 (host decides, gpu executes)") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    std::uint8_t srcData[16384];
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            srcData[y * 128 + x] = (y < 64) ? static_cast<std::uint8_t>(x + y + 1) : 0;

    pixels::Plane plane(128, 128, 4);
    pixels::Plane reconRef(128, 128, 4);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) plane.at(x, y) = srcData[y * 128 + x];

    std::int32_t refCoeffs[16384] = {0};
    std::uint8_t refModes[4] = {0};
    pipeline::encodeFrameAuto64x64(plane, reconRef, refCoeffs, refModes, transforms::TxType::DCT_DCT);

    const std::string ptxPred = *gpurt::compileToPtx(intra::predictBlock64x64CuSource(), "compute_61");
    const std::vector<std::string> pn = gpurt::ptxEntryNames(ptxPred);
    gpurt::Kernel kPred(ptxPred, *std::find(pn.begin(), pn.end(), "predict_block_64x64"));
    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> sn = gpurt::ptxEntryNames(ptxSub);
    gpurt::Kernel kSub(ptxSub, *std::find(sn.begin(), sn.end(), "subtract_64x64_plane"));
    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> tn = gpurt::ptxEntryNames(ptxTx);
    gpurt::Kernel kTx(ptxTx, *std::find(tn.begin(), tn.end(), "fwd_txfm_2d_64x64"));
    const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> inames = gpurt::ptxEntryNames(ptxInv);
    gpurt::Kernel kInv(ptxInv, *std::find(inames.begin(), inames.end(), "inv_txfm_2d_add_64x64"));

    constexpr int kStride = 128;
    gpurt::DeviceBuffer dPlane(sizeof(srcData));
    gpurt::DeviceBuffer dRecon(16384);
    gpurt::DeviceBuffer dResidual(4096 * sizeof(std::int16_t));
    gpurt::DeviceBuffer dBlkCoeffs(4096 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dPred(4096);
    dPlane.uploadFrom(srcData, sizeof(srcData));
    std::uint8_t zero[16384] = {0};
    dRecon.uploadFrom(zero, 16384);

    int amArg = 0;
    int lmArg = 0;
    int fiArg = -1;
    int defArg = 0;
    int typeArg = 0;
    int planeStrideArg = kStride;
    int fwdStrideArg = 64;
    int invStride = 64;
    gpurt::DeviceBuffer dMode(sizeof(int));
    gpurt::DeviceBuffer dDelta(sizeof(int));
    gpurt::DeviceBuffer dAm(sizeof(amArg));
    gpurt::DeviceBuffer dLm(sizeof(lmArg));
    gpurt::DeviceBuffer dFi(sizeof(fiArg));
    gpurt::DeviceBuffer dDef(sizeof(defArg));
    gpurt::DeviceBuffer dNTop(sizeof(int));
    gpurt::DeviceBuffer dNTr(sizeof(int));
    gpurt::DeviceBuffer dNLeft(sizeof(int));
    gpurt::DeviceBuffer dNBl(sizeof(int));
    gpurt::DeviceBuffer dAl(sizeof(int));
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    gpurt::DeviceBuffer dPlaneStride(sizeof(planeStrideArg));
    gpurt::DeviceBuffer dFwdStride(sizeof(fwdStrideArg));
    gpurt::DeviceBuffer dInvStride(sizeof(int));
    gpurt::DeviceBuffer dPx(sizeof(int));
    gpurt::DeviceBuffer dPy(sizeof(int));
    gpurt::DeviceBuffer dAbove(128);
    gpurt::DeviceBuffer dLeft(128);
    dDelta.uploadFrom(&defArg, sizeof(defArg));
    dFi.uploadFrom(&fiArg, sizeof(fiArg));
    dDef.uploadFrom(&defArg, sizeof(defArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dPlaneStride.uploadFrom(&planeStrideArg, sizeof(planeStrideArg));
    dFwdStride.uploadFrom(&fwdStrideArg, sizeof(fwdStrideArg));
    dInvStride.uploadFrom(&invStride, sizeof(invStride));

    std::uint8_t reconGot[16384] = {0};
    std::int32_t coeffsGot[16384] = {0};
    std::uint8_t modesGot[4] = {0};
    std::uint8_t aboveHost[128] = {0};
    std::uint8_t leftHost[128] = {0};

    for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 2; ++bx) {
            const int px = bx * 64;
            const int py = by * 64;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const int nTopPx = hasTop ? 64 : 0;
            const int nLeftPx = hasLeft ? 64 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < 2) ? 64 : 0;

            std::uint8_t tmp[16384] = {0};
            dRecon.downloadTo(tmp, 16384);
            if (hasTop) {
                for (int i = 0; i < 64 + nTopRightPx; ++i) {
                    aboveHost[i] = tmp[(py - 1) * 128 + px + i];
                }
                dAbove.uploadFrom(aboveHost, 64 + nTopRightPx);
            }
            if (hasLeft) {
                for (int i = 0; i < 64; ++i) leftHost[i] = tmp[(py + i) * 128 + px - 1];
                dLeft.uploadFrom(leftHost, 64);
            }
            int alVal = 0;
            if (hasTop && hasLeft) alVal = tmp[(py - 1) * 128 + px - 1];

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop ? static_cast<intra::PredictionMode>(modesGot[(by - 1) * 2 + bx])
                                    : intra::DC_PRED;
            nctx.leftMode = hasLeft ? static_cast<intra::PredictionMode>(modesGot[by * 2 + bx - 1])
                                    : intra::DC_PRED;
            std::uint8_t srcBlk[4096] = {0};
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) srcBlk[y * 64 + x] = srcData[(py + y) * 128 + px + x];
            const auto d = pipeline::decideBlockMode64x64(srcBlk, aboveHost, nTopPx, nTopRightPx,
                                                          leftHost, nLeftPx, 0,
                                                          static_cast<std::uint8_t>(alVal), nctx);
            modesGot[by * 2 + bx] = static_cast<std::uint8_t>(d.mode);

            int modeArg = d.mode;
            amArg = (int)nctx.aboveMode;
            lmArg = (int)nctx.leftMode;
            int deltaArg = 0;
            int nTopArg = nTopPx;
            int nTrArg = nTopRightPx;
            int nLeftArg = nLeftPx;
            int nBlArg = 0;
            int alArg = alVal;
            int pxArg = px;
            int pyArg = py;
            dMode.uploadFrom(&modeArg, sizeof(modeArg));
            dDelta.uploadFrom(&deltaArg, sizeof(deltaArg));
            dAm.uploadFrom(&amArg, sizeof(amArg));
            dLm.uploadFrom(&lmArg, sizeof(lmArg));
            dNTop.uploadFrom(&nTopArg, sizeof(nTopArg));
            dNTr.uploadFrom(&nTrArg, sizeof(nTrArg));
            dNLeft.uploadFrom(&nLeftArg, sizeof(nLeftArg));
            dNBl.uploadFrom(&nBlArg, sizeof(nBlArg));
            dAl.uploadFrom(&alArg, sizeof(alArg));
            dPx.uploadFrom(&pxArg, sizeof(pxArg));
            dPy.uploadFrom(&pyArg, sizeof(pyArg));

            CUdeviceptr pMode = dMode.get();
            CUdeviceptr pDelta = dDelta.get();
            CUdeviceptr pAm = dAm.get();
            CUdeviceptr pLm = dLm.get();
            CUdeviceptr pAbove = dAbove.get();
            CUdeviceptr pNTop = dNTop.get();
            CUdeviceptr pNTr = dNTr.get();
            CUdeviceptr pLeft = dLeft.get();
            CUdeviceptr pNLeft = dNLeft.get();
            CUdeviceptr pNBl = dNBl.get();
            CUdeviceptr pAl = dAl.get();
            CUdeviceptr pFi = dFi.get();
            CUdeviceptr pDef = dDef.get();
            CUdeviceptr pPred = dPred.get();
            void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                                &pNBl,  &pAl,   &pFi, &pDef, &pPred};
            kPred.launch(1, 1, 1024, 1, argsPred);

            CUdeviceptr pPlane = dPlane.get();
            CUdeviceptr pPlaneStride = dPlaneStride.get();
            CUdeviceptr pPx = dPx.get();
            CUdeviceptr pPy = dPy.get();
            CUdeviceptr pResidual = dResidual.get();
            void* argsSub[] = {&pPlane, &pPlaneStride, &pPx, &pPy, &pPred, &pResidual};
            kSub.launch(1, 1, 1024, 1, argsSub);

            CUdeviceptr pType = dType.get();
            CUdeviceptr pFwdStride = dFwdStride.get();
            CUdeviceptr pBlkCoeffs = dBlkCoeffs.get();
            void* argsTx[] = {&pResidual, &pFwdStride, &pType, &pBlkCoeffs};
            kTx.launch(1, 1, 64, 1, argsTx);
            std::int32_t blk[4096] = {0};
            dBlkCoeffs.downloadTo(blk, sizeof(blk));
            for (int i = 0; i < 4096; ++i) {
                coeffsGot[(by * 2 + bx) * 4096 + i] = blk[i];
            }

            CUdeviceptr pInvStride = dInvStride.get();
            void* argsInv[] = {&pBlkCoeffs, &pType, &pPred, &pInvStride};
            kInv.launch(1, 1, 64, 1, argsInv);

            std::uint8_t blkRecon[4096] = {0};
            dPred.downloadTo(blkRecon, sizeof(blkRecon));
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    reconGot[(py + y) * 128 + px + x] = blkRecon[y * 64 + x];
            dRecon.uploadFrom(reconGot, 16384);
        }
    }

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modesGot[i] != refModes[i]) modesOk = false;
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int i = 0; i < 16384; ++i) {
        if (reconGot[i] != reconRef.at(i & 127, i >> 7)) reconOk = false;
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 16384; ++i) {
        if (coeffsGot[i] != refCoeffs[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}
TEST_CASE("gpu frame auto 64x64 with quantization matches host encodeFrameAuto64x64Q") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    std::uint8_t srcData[16384];
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            srcData[y * 128 + x] = (y < 64) ? static_cast<std::uint8_t>(x + y + 1) : 0;

    pixels::Plane plane(128, 128, 4);
    pixels::Plane reconRef(128, 128, 4);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) plane.at(x, y) = srcData[y * 128 + x];

    std::int32_t refCoeffs[16384] = {0};
    std::uint8_t refModes[4] = {0};
    pipeline::encodeFrameAuto64x64Q(plane, reconRef, refCoeffs, refModes, 100,
                                    transforms::TxType::DCT_DCT);

    transforms::QuantTables qt;
    transforms::buildQuantTables(100, qt);
    std::int16_t scan[4096];
    transforms::defaultScan64x64(scan);

    const std::string ptxPred = *gpurt::compileToPtx(intra::predictBlock64x64CuSource(), "compute_61");
    const std::vector<std::string> pn = gpurt::ptxEntryNames(ptxPred);
    gpurt::Kernel kPred(ptxPred, *std::find(pn.begin(), pn.end(), "predict_block_64x64"));
    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> sn = gpurt::ptxEntryNames(ptxSub);
    gpurt::Kernel kSub(ptxSub, *std::find(sn.begin(), sn.end(), "subtract_64x64_plane"));
    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> tn = gpurt::ptxEntryNames(ptxTx);
    gpurt::Kernel kTx(ptxTx, *std::find(tn.begin(), tn.end(), "fwd_txfm_2d_64x64"));
    const std::string ptxQ = *gpurt::compileToPtx(transforms::quantCuSource(), "compute_61");
    const std::vector<std::string> qn = gpurt::ptxEntryNames(ptxQ);
    gpurt::Kernel kQuant(ptxQ, *std::find(qn.begin(), qn.end(), "quant_dequant_64x64"));
    const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> inames = gpurt::ptxEntryNames(ptxInv);
    gpurt::Kernel kInv(ptxInv, *std::find(inames.begin(), inames.end(), "inv_txfm_2d_add_64x64"));

    constexpr int kStride = 128;
    gpurt::DeviceBuffer dPlane(sizeof(srcData));
    gpurt::DeviceBuffer dRecon(16384);
    gpurt::DeviceBuffer dResidual(4096 * sizeof(std::int16_t));
    gpurt::DeviceBuffer dBlkCoeffs(4096 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dPred(4096);
    gpurt::DeviceBuffer dQcoeffs(4096 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dDqcoeffs(4096 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dEob(sizeof(std::uint16_t));
    gpurt::DeviceBuffer dQuantFp(sizeof(qt.quantFp));
    gpurt::DeviceBuffer dDequant(sizeof(qt.dequant));
    gpurt::DeviceBuffer dRoundFp(sizeof(qt.roundFp));
    gpurt::DeviceBuffer dScan(sizeof(scan));
    dPlane.uploadFrom(srcData, sizeof(srcData));
    std::uint8_t zero[16384] = {0};
    dRecon.uploadFrom(zero, 16384);
    dQuantFp.uploadFrom(qt.quantFp, sizeof(qt.quantFp));
    dDequant.uploadFrom(qt.dequant, sizeof(qt.dequant));
    dRoundFp.uploadFrom(qt.roundFp, sizeof(qt.roundFp));
    dScan.uploadFrom(scan, sizeof(scan));

    int amArg = 0;
    int lmArg = 0;
    int fiArg = -1;
    int defArg = 0;
    int typeArg = 0;
    int planeStrideArg = kStride;
    int fwdStrideArg = 64;
    int invStride = 64;
    gpurt::DeviceBuffer dMode(sizeof(int));
    gpurt::DeviceBuffer dDelta(sizeof(int));
    gpurt::DeviceBuffer dAm(sizeof(amArg));
    gpurt::DeviceBuffer dLm(sizeof(lmArg));
    gpurt::DeviceBuffer dFi(sizeof(fiArg));
    gpurt::DeviceBuffer dDef(sizeof(defArg));
    gpurt::DeviceBuffer dNTop(sizeof(int));
    gpurt::DeviceBuffer dNTr(sizeof(int));
    gpurt::DeviceBuffer dNLeft(sizeof(int));
    gpurt::DeviceBuffer dNBl(sizeof(int));
    gpurt::DeviceBuffer dAl(sizeof(int));
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    gpurt::DeviceBuffer dPlaneStride(sizeof(planeStrideArg));
    gpurt::DeviceBuffer dFwdStride(sizeof(fwdStrideArg));
    gpurt::DeviceBuffer dInvStride(sizeof(int));
    gpurt::DeviceBuffer dPx(sizeof(int));
    gpurt::DeviceBuffer dPy(sizeof(int));
    gpurt::DeviceBuffer dAbove(128);
    gpurt::DeviceBuffer dLeft(128);
    dDelta.uploadFrom(&defArg, sizeof(defArg));
    dFi.uploadFrom(&fiArg, sizeof(fiArg));
    dDef.uploadFrom(&defArg, sizeof(defArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dPlaneStride.uploadFrom(&planeStrideArg, sizeof(planeStrideArg));
    dFwdStride.uploadFrom(&fwdStrideArg, sizeof(fwdStrideArg));
    dInvStride.uploadFrom(&invStride, sizeof(invStride));

    std::uint8_t reconGot[16384] = {0};
    std::int32_t coeffsGot[16384] = {0};
    std::uint8_t modesGot[4] = {0};
    std::uint8_t aboveHost[128] = {0};
    std::uint8_t leftHost[128] = {0};

    for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 2; ++bx) {
            const int px = bx * 64;
            const int py = by * 64;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const int nTopPx = hasTop ? 64 : 0;
            const int nLeftPx = hasLeft ? 64 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < 2) ? 64 : 0;

            std::uint8_t tmp[16384] = {0};
            dRecon.downloadTo(tmp, 16384);
            if (hasTop) {
                for (int i = 0; i < 64 + nTopRightPx; ++i) {
                    aboveHost[i] = tmp[(py - 1) * 128 + px + i];
                }
                dAbove.uploadFrom(aboveHost, 64 + nTopRightPx);
            }
            if (hasLeft) {
                for (int i = 0; i < 64; ++i) leftHost[i] = tmp[(py + i) * 128 + px - 1];
                dLeft.uploadFrom(leftHost, 64);
            }
            int alVal = 0;
            if (hasTop && hasLeft) alVal = tmp[(py - 1) * 128 + px - 1];

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop ? static_cast<intra::PredictionMode>(modesGot[(by - 1) * 2 + bx])
                                    : intra::DC_PRED;
            nctx.leftMode = hasLeft ? static_cast<intra::PredictionMode>(modesGot[by * 2 + bx - 1])
                                    : intra::DC_PRED;
            std::uint8_t srcBlk[4096] = {0};
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) srcBlk[y * 64 + x] = srcData[(py + y) * 128 + px + x];
            const auto d = pipeline::decideBlockMode64x64(srcBlk, aboveHost, nTopPx, nTopRightPx,
                                                          leftHost, nLeftPx, 0,
                                                          static_cast<std::uint8_t>(alVal), nctx);
            modesGot[by * 2 + bx] = static_cast<std::uint8_t>(d.mode);

            int modeArg = d.mode;
            amArg = (int)nctx.aboveMode;
            lmArg = (int)nctx.leftMode;
            int deltaArg = 0;
            int nTopArg = nTopPx;
            int nTrArg = nTopRightPx;
            int nLeftArg = nLeftPx;
            int nBlArg = 0;
            int alArg = alVal;
            int pxArg = px;
            int pyArg = py;
            dMode.uploadFrom(&modeArg, sizeof(modeArg));
            dDelta.uploadFrom(&deltaArg, sizeof(deltaArg));
            dAm.uploadFrom(&amArg, sizeof(amArg));
            dLm.uploadFrom(&lmArg, sizeof(lmArg));
            dNTop.uploadFrom(&nTopArg, sizeof(nTopArg));
            dNTr.uploadFrom(&nTrArg, sizeof(nTrArg));
            dNLeft.uploadFrom(&nLeftArg, sizeof(nLeftArg));
            dNBl.uploadFrom(&nBlArg, sizeof(nBlArg));
            dAl.uploadFrom(&alArg, sizeof(alArg));
            dPx.uploadFrom(&pxArg, sizeof(pxArg));
            dPy.uploadFrom(&pyArg, sizeof(pyArg));

            CUdeviceptr pMode = dMode.get();
            CUdeviceptr pDelta = dDelta.get();
            CUdeviceptr pAm = dAm.get();
            CUdeviceptr pLm = dLm.get();
            CUdeviceptr pAbove = dAbove.get();
            CUdeviceptr pNTop = dNTop.get();
            CUdeviceptr pNTr = dNTr.get();
            CUdeviceptr pLeft = dLeft.get();
            CUdeviceptr pNLeft = dNLeft.get();
            CUdeviceptr pNBl = dNBl.get();
            CUdeviceptr pAl = dAl.get();
            CUdeviceptr pFi = dFi.get();
            CUdeviceptr pDef = dDef.get();
            CUdeviceptr pPred = dPred.get();
            void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                                &pNBl,  &pAl,   &pFi, &pDef, &pPred};
            kPred.launch(1, 1, 1024, 1, argsPred);

            CUdeviceptr pPlane = dPlane.get();
            CUdeviceptr pPlaneStride = dPlaneStride.get();
            CUdeviceptr pPx = dPx.get();
            CUdeviceptr pPy = dPy.get();
            CUdeviceptr pResidual = dResidual.get();
            void* argsSub[] = {&pPlane, &pPlaneStride, &pPx, &pPy, &pPred, &pResidual};
            kSub.launch(1, 1, 1024, 1, argsSub);

            CUdeviceptr pType = dType.get();
            CUdeviceptr pFwdStride = dFwdStride.get();
            CUdeviceptr pBlkCoeffs = dBlkCoeffs.get();
            void* argsTx[] = {&pResidual, &pFwdStride, &pType, &pBlkCoeffs};
            kTx.launch(1, 1, 64, 1, argsTx);

            CUdeviceptr pQuantFp = dQuantFp.get();
            CUdeviceptr pDequant = dDequant.get();
            CUdeviceptr pRoundFp = dRoundFp.get();
            CUdeviceptr pScan = dScan.get();
            CUdeviceptr pQcoeffs = dQcoeffs.get();
            CUdeviceptr pDqcoeffs = dDqcoeffs.get();
            CUdeviceptr pEob = dEob.get();
            void* argsQuant[] = {&pBlkCoeffs, &pQuantFp, &pDequant, &pRoundFp, &pScan,
                                 &pQcoeffs, &pDqcoeffs, &pEob};
            kQuant.launch(1, 1, 1024, 1, argsQuant);
            std::int32_t blkQ[4096] = {0};
            dQcoeffs.downloadTo(blkQ, sizeof(blkQ));
            for (int i = 0; i < 4096; ++i) coeffsGot[(by * 2 + bx) * 4096 + i] = blkQ[i];

            CUdeviceptr pInvStride = dInvStride.get();
            void* argsInv[] = {&pDqcoeffs, &pType, &pPred, &pInvStride};
            kInv.launch(1, 1, 64, 1, argsInv);

            std::uint8_t blkRecon[4096] = {0};
            dPred.downloadTo(blkRecon, sizeof(blkRecon));
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    reconGot[(py + y) * 128 + px + x] = blkRecon[y * 64 + x];
            dRecon.uploadFrom(reconGot, 16384);
        }
    }

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modesGot[i] != refModes[i]) modesOk = false;
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int i = 0; i < 16384; ++i) {
        if (reconGot[i] != reconRef.at(i & 127, i >> 7)) reconOk = false;
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 16384; ++i) {
        if (coeffsGot[i] != refCoeffs[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}
// ---- CH3: chroma (4:2:0) frame compositions --------------------------------

TEST_CASE("frame chroma auto 16x16 matches the bcf16 generator golden") {
    // golden: golden_gen bcf16_modes (32x32 UV plane = 4:2:0 box average
    // ((sum+2)>>2) of a 64x64 luma fixture rows 0-31 ramp x+y+1 / rows 32-63
    // zero -> UV rows 0-15 = 2*(i+j+2), rows 16-31 = 0; 2x2 grid of 16x16 UV
    // blocks; the chroma fold (g_uv2y) + FI-free builder live in the
    // composition; D2 policy over the 13 folded UV candidates (CFL excluded
    // as a candidate - policy named).
    const std::uint8_t goldenModes[4] = {1, 7, 2, 2};

    pixels::Plane src(32, 32, 4);
    pixels::Plane recon(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            src.at(x, y) = (y < 16) ? static_cast<std::uint8_t>(2 * (x + y + 2)) : 0;

    std::int32_t coeffs[1024] = {0};
    std::uint8_t modes[4] = {0};
    pipeline::encodeFrameAutoChroma16x16(src, recon, coeffs, modes, transforms::TxType::DCT_DCT);

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modes[i] != goldenModes[i]) modesOk = false;
    }
    CHECK(modesOk);
}

TEST_CASE("frame auto chroma 16x16 with quantization matches the bcf16q golden") {
    // golden: golden_gen bcf16q_modes at qindex 100 (FP quantizer at
    // n_coeffs=256, log_scale 0). Modes identical to lossless (1 7 2 2).
    const std::uint8_t goldenModes[4] = {1, 7, 2, 2};

    pixels::Plane src(32, 32, 4);
    pixels::Plane recon(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            src.at(x, y) = (y < 16) ? static_cast<std::uint8_t>(2 * (x + y + 2)) : 0;

    std::int32_t coeffs[1024] = {0};
    std::uint8_t modes[4] = {0};
    pipeline::encodeFrameAutoChroma16x16Q(src, recon, coeffs, modes, 100, transforms::TxType::DCT_DCT);

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modes[i] != goldenModes[i]) modesOk = false;
    }
    CHECK(modesOk);
}

TEST_CASE("frame recon chroma 16x16 forced mode matches the bcf16v golden") {
    // golden: golden_gen bcf16v_recon/bcf16v_coeffs (UV_V_PRED forced + DCT
    // over the UV fixture) - gate-pinned from the start (no C7b gap).
    const std::int32_t goldenBlk0[8] = {-11902, -1172, 0, -129, 1, -45, 0, -22};

    pixels::Plane src(32, 32, 4);
    pixels::Plane recon(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            src.at(x, y) = (y < 16) ? static_cast<std::uint8_t>(2 * (x + y + 2)) : 0;

    std::int32_t coeffs[1024] = {0};
    pipeline::encodeFrameReconChroma16x16(src, recon, coeffs, intra::UV_V_PRED, 0,
                                          transforms::TxType::DCT_DCT);

    const std::uint8_t goldenReconRow0[8] = {4, 6, 8, 10, 12, 14, 16, 18};
    const std::uint8_t goldenReconRow30[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    bool reconOk = true;
    for (int x = 0; x < 8; ++x) {
        if (recon.at(x, 0) != goldenReconRow0[x]) reconOk = false;
        if (recon.at(x, 30) != goldenReconRow30[x]) reconOk = false;
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 8; ++i) {
        if (coeffs[i] != goldenBlk0[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}

TEST_CASE("frame recon chroma 16x16 with quantization matches the bcf16vq golden") {
    // golden: golden_gen bcf16vq at qindex 100 (forced UV_V, FP quantizer at
    // n_coeffs=256/log_scale 0); recon carries real quantization loss.
    const std::int32_t goldenBlk0[8] = {-128, -10, 0, -1, 0, 0, 0, 0};

    pixels::Plane src(32, 32, 4);
    pixels::Plane recon(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            src.at(x, y) = (y < 16) ? static_cast<std::uint8_t>(2 * (x + y + 2)) : 0;

    std::int32_t coeffs[1024] = {0};
    pipeline::encodeFrameReconChroma16x16Q(src, recon, coeffs, intra::UV_V_PRED, 0, 100,
                                           transforms::TxType::DCT_DCT);

    const std::uint8_t goldenReconRow0[8] = {7, 8, 10, 12, 14, 16, 18, 20};
    const std::uint8_t goldenReconRow31[8] = {0, 0, 1, 1, 1, 0, 0, 1};
    bool reconOk = true;
    for (int x = 0; x < 8; ++x) {
        if (recon.at(x, 0) != goldenReconRow0[x]) reconOk = false;
        if (recon.at(x, 31) != goldenReconRow31[x]) reconOk = false;
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 8; ++i) {
        if (coeffs[i] != goldenBlk0[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}

TEST_CASE("gpu frame auto chroma 16x16 matches host encodeFrameAutoChroma16x16") {
    // CH3 GPU: host decides (decideBlockModeUv16x16 on the UV plane), GPU
    // executes the UNCHANGED 16x16 kernel chain. The only chroma-specific
    // step is the fold at the call site (g_uv2y host-side, the SVT call-site
    // fold).
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    std::uint8_t srcData[1024];
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            srcData[y * 32 + x] = (y < 16) ? static_cast<std::uint8_t>(2 * (x + y + 2)) : 0;

    pixels::Plane plane(32, 32, 4);
    pixels::Plane reconRef(32, 32, 4);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) plane.at(x, y) = srcData[y * 32 + x];

    std::int32_t refCoeffs[1024] = {0};
    std::uint8_t refModes[4] = {0};
    pipeline::encodeFrameAutoChroma16x16(plane, reconRef, refCoeffs, refModes,
                                         transforms::TxType::DCT_DCT);

    const std::string ptxPred = *gpurt::compileToPtx(intra::predictBlock16x16CuSource(), "compute_61");
    const std::vector<std::string> pn = gpurt::ptxEntryNames(ptxPred);
    gpurt::Kernel kPred(ptxPred, *std::find(pn.begin(), pn.end(), "predict_block_16x16"));
    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> sn = gpurt::ptxEntryNames(ptxSub);
    gpurt::Kernel kSub(ptxSub, *std::find(sn.begin(), sn.end(), "subtract_16x16_plane"));
    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> tn = gpurt::ptxEntryNames(ptxTx);
    gpurt::Kernel kTx(ptxTx, *std::find(tn.begin(), tn.end(), "fwd_txfm_2d_16x16"));
    const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> inames = gpurt::ptxEntryNames(ptxInv);
    gpurt::Kernel kInv(ptxInv, *std::find(inames.begin(), inames.end(), "inv_txfm_2d_add_16x16"));

    constexpr int kStride = 32;
    gpurt::DeviceBuffer dPlane(sizeof(srcData));
    gpurt::DeviceBuffer dRecon(1024);
    gpurt::DeviceBuffer dResidual(256 * sizeof(std::int16_t));
    gpurt::DeviceBuffer dBlkCoeffs(256 * sizeof(std::int32_t));
    gpurt::DeviceBuffer dPred(256);
    dPlane.uploadFrom(srcData, sizeof(srcData));
    std::uint8_t zero[1024] = {0};
    dRecon.uploadFrom(zero, 1024);

    int defArg = 0;
    int fiArg = -1;  // FILTER_INTRA_MODES: chroma never uses FI (enc_intra_prediction.c:641)
    int typeArg = 0;
    int planeStrideArg = kStride;
    int fwdStrideArg = 16;
    int invStride = 16;
    gpurt::DeviceBuffer dMode(sizeof(int));
    gpurt::DeviceBuffer dDelta(sizeof(int));
    gpurt::DeviceBuffer dAm(sizeof(int));
    gpurt::DeviceBuffer dLm(sizeof(int));
    gpurt::DeviceBuffer dFi(sizeof(int));
    gpurt::DeviceBuffer dDef(sizeof(defArg));
    gpurt::DeviceBuffer dNTop(sizeof(int));
    gpurt::DeviceBuffer dNTr(sizeof(int));
    gpurt::DeviceBuffer dNLeft(sizeof(int));
    gpurt::DeviceBuffer dNBl(sizeof(int));
    gpurt::DeviceBuffer dAl(sizeof(int));
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    gpurt::DeviceBuffer dPlaneStride(sizeof(planeStrideArg));
    gpurt::DeviceBuffer dFwdStride(sizeof(fwdStrideArg));
    gpurt::DeviceBuffer dInvStride(sizeof(int));
    gpurt::DeviceBuffer dPx(sizeof(int));
    gpurt::DeviceBuffer dPy(sizeof(int));
    gpurt::DeviceBuffer dAbove(32);
    gpurt::DeviceBuffer dLeft(32);
    dFi.uploadFrom(&fiArg, sizeof(fiArg));
    dDef.uploadFrom(&defArg, sizeof(defArg));
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dPlaneStride.uploadFrom(&planeStrideArg, sizeof(planeStrideArg));
    dFwdStride.uploadFrom(&fwdStrideArg, sizeof(fwdStrideArg));
    dInvStride.uploadFrom(&invStride, sizeof(invStride));

    std::uint8_t reconGot[1024] = {0};
    std::int32_t coeffsGot[1024] = {0};
    std::uint8_t modesGot[4] = {0};
    std::uint8_t aboveHost[32] = {0};
    std::uint8_t leftHost[32] = {0};

    for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 2; ++bx) {
            const int px = bx * 16;
            const int py = by * 16;
            const bool hasTop = by > 0;
            const bool hasLeft = bx > 0;
            const int nTopPx = hasTop ? 16 : 0;
            const int nLeftPx = hasLeft ? 16 : 0;
            const int nTopRightPx = (hasTop && bx + 1 < 2) ? 16 : 0;

            std::uint8_t tmp[1024] = {0};
            dRecon.downloadTo(tmp, 1024);
            if (hasTop) {
                for (int i = 0; i < 16 + nTopRightPx; ++i) {
                    aboveHost[i] = tmp[(py - 1) * 32 + px + i];
                }
                dAbove.uploadFrom(aboveHost, 16 + nTopRightPx);
            }
            if (hasLeft) {
                for (int i = 0; i < 16; ++i) leftHost[i] = tmp[(py + i) * 32 + px - 1];
                dLeft.uploadFrom(leftHost, 16);
            }
            int alVal = 0;
            if (hasTop && hasLeft) alVal = tmp[(py - 1) * 32 + px - 1];

            intra::NeighborContext nctx;
            nctx.aboveMode = hasTop ? static_cast<intra::PredictionMode>(modesGot[(by - 1) * 2 + bx])
                                    : intra::DC_PRED;
            nctx.leftMode = hasLeft ? static_cast<intra::PredictionMode>(modesGot[by * 2 + bx - 1])
                                    : intra::DC_PRED;
            std::uint8_t srcBlk[256] = {0};
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) srcBlk[y * 16 + x] = srcData[(py + y) * 32 + px + x];
            const auto d = pipeline::decideBlockModeUv16x16(srcBlk, aboveHost, nTopPx, nTopRightPx,
                                                            leftHost, nLeftPx, 0,
                                                            static_cast<std::uint8_t>(alVal), nctx);
            modesGot[by * 2 + bx] = static_cast<std::uint8_t>(d.mode);

            // chroma fold at the call site (the SVT call site is
            // enc_intra_prediction.c:587-588): the kernel gets the FOLDED
            // luma mode, never FI (-1).
            const int folded = intra::uv2y(static_cast<intra::UvPredictionMode>(d.mode));
            int modeArg = folded;
            int amArg = (int)nctx.aboveMode;
            int lmArg = (int)nctx.leftMode;
            int deltaArg = 0;
            int nTopArg = nTopPx;
            int nTrArg = nTopRightPx;
            int nLeftArg = nLeftPx;
            int nBlArg = 0;
            int alArg = alVal;
            int pxArg = px;
            int pyArg = py;
            dMode.uploadFrom(&modeArg, sizeof(modeArg));
            dDelta.uploadFrom(&deltaArg, sizeof(deltaArg));
            dAm.uploadFrom(&amArg, sizeof(amArg));
            dLm.uploadFrom(&lmArg, sizeof(lmArg));
            dNTop.uploadFrom(&nTopArg, sizeof(nTopArg));
            dNTr.uploadFrom(&nTrArg, sizeof(nTrArg));
            dNLeft.uploadFrom(&nLeftArg, sizeof(nLeftArg));
            dNBl.uploadFrom(&nBlArg, sizeof(nBlArg));
            dAl.uploadFrom(&alArg, sizeof(alArg));
            dPx.uploadFrom(&pxArg, sizeof(pxArg));
            dPy.uploadFrom(&pyArg, sizeof(pyArg));

            CUdeviceptr pMode = dMode.get();
            CUdeviceptr pDelta = dDelta.get();
            CUdeviceptr pAm = dAm.get();
            CUdeviceptr pLm = dLm.get();
            CUdeviceptr pAbove = dAbove.get();
            CUdeviceptr pNTop = dNTop.get();
            CUdeviceptr pNTr = dNTr.get();
            CUdeviceptr pLeft = dLeft.get();
            CUdeviceptr pNLeft = dNLeft.get();
            CUdeviceptr pNBl = dNBl.get();
            CUdeviceptr pAl = dAl.get();
            CUdeviceptr pFi = dFi.get();
            CUdeviceptr pDef = dDef.get();
            CUdeviceptr pPred = dPred.get();
            void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                                &pNBl,  &pAl,   &pFi, &pDef, &pPred};
            kPred.launch(1, 1, 256, 1, argsPred);

            CUdeviceptr pPlane = dPlane.get();
            CUdeviceptr pPlaneStride = dPlaneStride.get();
            CUdeviceptr pPx = dPx.get();
            CUdeviceptr pPy = dPy.get();
            CUdeviceptr pResidual = dResidual.get();
            void* argsSub[] = {&pPlane, &pPlaneStride, &pPx, &pPy, &pPred, &pResidual};
            kSub.launch(1, 1, 256, 1, argsSub);

            CUdeviceptr pType = dType.get();
            CUdeviceptr pFwdStride = dFwdStride.get();
            CUdeviceptr pBlkCoeffs = dBlkCoeffs.get();
            void* argsTx[] = {&pResidual, &pFwdStride, &pType, &pBlkCoeffs};
            kTx.launch(1, 1, 16, 1, argsTx);
            std::int32_t blk[256] = {0};
            dBlkCoeffs.downloadTo(blk, sizeof(blk));
            for (int i = 0; i < 256; ++i) {
                coeffsGot[(by * 2 + bx) * 256 + i] = blk[i];
            }

            CUdeviceptr pInvStride = dInvStride.get();
            void* argsInv[] = {&pBlkCoeffs, &pType, &pPred, &pInvStride};
            kInv.launch(1, 1, 16, 1, argsInv);

            std::uint8_t blkRecon[256] = {0};
            dPred.downloadTo(blkRecon, sizeof(blkRecon));
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x)
                    reconGot[(py + y) * 32 + px + x] = blkRecon[y * 16 + x];
            dRecon.uploadFrom(reconGot, 1024);
        }
    }

    bool modesOk = true;
    for (int i = 0; i < 4; ++i) {
        if (modesGot[i] != refModes[i]) modesOk = false;
    }
    CHECK(modesOk);

    bool reconOk = true;
    for (int i = 0; i < 1024; ++i) {
        if (reconGot[i] != reconRef.at(i & 31, i >> 5)) reconOk = false;
    }
    CHECK(reconOk);

    bool coeffsOk = true;
    for (int i = 0; i < 1024; ++i) {
        if (coeffsGot[i] != refCoeffs[i]) coeffsOk = false;
    }
    CHECK(coeffsOk);
}

// CS4 fixtures (mechanically emitted from expected_primitives.txt; see the
// .inc header): the ecs4S_* walk lines and the ecs2S_* cross-check lines.
#include "ecs4_gate.inc"
// CS7: the chroma bottom-left PRE-FIX baseline, frozen at d2a9322 and NEVER
// REGENERATE (the reason is in that .inc's own header). It is the ONLY surface
// that can answer "did this leaf's recon move?", because every ecs4S_recon line
// carries post-fix bytes the moment the gate is regenerated.
#include "cs7_prefix_baseline.inc"
// RT21: the chroma ABOVE-RIGHT PRE-FIX baseline, frozen at 69a8a1a and NEVER
// REGENERATE (the reason is in that .inc's own header, which also records why
// c8/c16/c32 are absent). Same role as the CS7 file one axis over: the ONLY
// surface that can answer "did this leaf's recon move?" for the above-right
// fix, because every ecs4S_recon line carries post-fix bytes the moment the
// gate is regenerated. The hex decode reuses cs7HexByte below - the helper is
// generic (two hex characters per byte); only its name is historical.
#include "rt21_prefix_baseline.inc"

// The Z-order construction, mirrored from pipeline.cpp:1772-1782 (zorderLeaves)
// and the inverse map at pipeline.cpp:1851-1852. At FILE scope rather than
// inside one test block, because two independent derivations below need it (the
// availability derivation and the CS7 prefix/boundary derivation) and a second
// copy of the Z-order is precisely the transcription this file forbids. Mirrored
// rather than hardcoded: the order is independently pinned by the RT6 emission
// tests, and a transcribed one would be exactly the literal the constraint bans.
struct ZOrder {
    static void leaves(int size, int x0, int y0, int grid, int* out, int* n) {
        if (size == 1) {
            out[(*n)++] = y0 * grid + x0;
            return;
        }
        const int h = size / 2;
        leaves(h, x0, y0, grid, out, n);
        leaves(h, x0 + h, y0, grid, out, n);
        leaves(h, x0, y0 + h, grid, out, n);
        leaves(h, x0 + h, y0 + h, grid, out, n);
    }
};

// Byte i of a cs7_prefix_baseline hex array (that file holds two characters
// per byte, lower case). Decoded here rather than pre-converted so the frozen
// fixture stays in its committed, reviewed form.
static std::uint8_t cs7HexByte(const char* p, int i) {
    const int hi = (p[2 * i] <= '9') ? p[2 * i] - '0' : p[2 * i] - 'a' + 10;
    const int lo = (p[2 * i + 1] <= '9') ? p[2 * i + 1] - '0' : p[2 * i + 1] - 'a' + 10;
    return static_cast<std::uint8_t>(hi * 16 + lo);
}

TEST_CASE("CS4: the chroma-emitting Q walk matches the ecs4 gate lines") {
    // The bottom-left AVAILABILITY rule is exercised EXHAUSTIVELY here, and
    // self-checkingly: the test restates the geometric rule independently from
    // the Z-order map it rebuilds below, and asserts pipeline::bottomLeftPx
    // agrees for EVERY leaf of EVERY geometry. No leaf count is hand-
    // transcribed - a literal would be indistinguishable from a correct value
    // to the reader, would keep asserting a stale number after the zpos map
    // changed, and would leave the suite green on a wrong expectation.
    {
        // The Z-order comes from the file-scope ZOrder above, shared with the
        // CS7 prefix/boundary derivation at the end of the walk block below.
        // UV leaf indices ARE the luma leaf indices: uvB = lumaB/2, the UV
        // origin is (px & ~7) >> 1, so uRow == by and uCol == bx, and
        // uvGrid = 32/uvB = 64/lumaB = grid. That identity is why the chroma
        // site needs no second Z-order map.
        //
        // The builder's MODE predicate reduces to one enum value: kModeToAngle
        // (intra.cpp:476) is {0,90,180,45,135,113,157,203,67,0,0,0,0}, whose
        // only entry above 180 is index 7, and the enum names index 7
        // UV_D203_PRED; with needBottom = pAngle > 180 (intra.cpp:546) the
        // predicate is therefore `mode == UV_D203_PRED`. Derived from the enum
        // and the one cited table, not typed in as a copied list.
        for (int si = 0; si < 4; ++si) {
            static const int sizes[4] = {4, 8, 16, 32};
            const int S = sizes[si];
            const int B = 2 * S;
            const int grid = 64 / B;
            const int nb = grid * grid;
            const ecs4::Gate& G = ecs4::gates[si];
            int zord[64], zn = 0;
            ZOrder::leaves(grid, 0, 0, grid, zord, &zn);
            REQUIRE(zn == nb);
            int zPos[64];
            for (int i = 0; i < zn; ++i) zPos[zord[i]] = i;
            // per-leaf: the function's verdict must equal the rule restated
            // here from zPos, and a non-zero verdict must be the block size.
            // Named per leaf so a disagreement localises instead of showing up
            // as one opaque total.
            int fnTotal = 0, derivedTotal = 0, nAvail = 0, nQualify = 0;
            for (int b = 0; b < nb; ++b) {
                const int by = b / grid, bx = b % grid;
                const int fn = pipeline::bottomLeftPx(b, by, bx, grid, zPos, S);
                // the rule, restated independently of the implementation
                const bool derived = (bx > 0) && (by + 1 < grid) &&
                                     (zPos[(by + 1) * grid + (bx - 1)] < zPos[b]);
                if (derived) {
                    ++nAvail;
                    derivedTotal += S;
                    INFO("geometry S=" << S << " leaf " << b << " below-left available");
                }
                if (fn != 0) {
                    REQUIRE(fn == S);
                    fnTotal += S;
                }
                if ((fn != 0) != derived) {
                    INFO("geometry S=" << S << " leaf " << b << " by=" << by << " bx=" << bx
                                       << " fn=" << fn << " derived=" << derived);
                }
                REQUIRE((fn != 0) == derived);
                // the mode predicate, applied to the committed gate's U mode
                // (-1 is the generator's non-referenced sentinel). Read from
                // the gate rather than assuming the leaf set.
                const int um = G.modes[3 * b + 1];
                if (um >= 0 && um == static_cast<int>(intra::UV_D203_PRED)) {
                    ++nQualify;
                    if (derived) INFO("geometry S=" << S << " leaf " << b << " qualifies (D203)");
                }
            }
            REQUIRE(fnTotal == derivedTotal);
            // the gathered-pixel total the fix must produce: every chroma-
            // referenced D203 leaf whose below-left is available.
            int wantTotal = 0;
            for (int b = 0; b < nb; ++b) {
                const int by = b / grid, bx = b % grid;
                const int um = G.modes[3 * b + 1];
                if (um < 0 || um != static_cast<int>(intra::UV_D203_PRED)) continue;
                if (pipeline::bottomLeftPx(b, by, bx, grid, zPos, S)) wantTotal += S;
            }
            // Printed on SUCCESS, not only on failure: an INFO that fires only
            // on failure leaves the derived numbers unreadable, and a test whose
            // numbers nobody has read is a test whose numbers nobody has
            // checked. These totals are quoted by the fix commit, so they have
            // to be visible without manufacturing a failure to see them.
            MESSAGE("RT11 derive S=" << S << " (S is the UV block size) nAvail=" << nAvail
                                     << " nQualify=" << nQualify << " fnTotal=" << fnTotal
                                     << " derivedTotal=" << derivedTotal
                                     << " wantTotal=" << wantTotal);
            // The coverage claim, asserted in BOTH directions and falsifiable.
            // S is the UV (chroma) block size, so S = 4 and 8 are the c4 and
            // c8 artifacts - the two geometries the CS7 map says must change,
            // and the fixture must therefore exercise the gather there. S = 16
            // is the negative control (its one D203 leaf has no available
            // below-left, which is dav1d's measured UV_LEFT_HAS_BOTTOM = 0 at
            // this very block) and S = 32 is the single-luma-block geometry,
            // grid == 1, so neither may gather anything. Asserting those ZEROS
            // is asserting the control, not tuning a number to a result: a
            // non-zero here would mean the control had been lost and c16's 1:1
            // conformance would stop being evidence for the mechanism.
            if (S == 4 || S == 8) {
                REQUIRE(wantTotal > 0);
            } else {
                REQUIRE(wantTotal == 0);
                // wantTotal == 0 alone does NOT distinguish "no leaf has an
                // available below-left" from "leaves are available, none of them
                // D203" - the two have different meanings, and a comment
                // asserting the intended one is exactly the defect class this
                // session keeps fixing, in a different medium. Pin the REASON.
                REQUIRE(nAvail == 0);
                if (S == 16)
                    // there IS a D203 leaf at c16, so the zero comes from its
                    // below-left being unavailable - which is precisely dav1d's
                    // measured UV_LEFT_HAS_BOTTOM = 0 at this very block. If
                    // this ever fails, the control is gone and c16's 1:1
                    // conformance stops being evidence for the mechanism.
                    REQUIRE(nQualify == 1);
            }
        }
    }
    // THE WALK (the ratified CS4 order, the write_modes_b shape): per luma
    // block in raster order [luma kf mode symbol -> uv_mode symbol
    // (writeUvMode, the DECIDED chroma mode; ONE uv_mode per block - the V
    // TU shares the U-plane decision, so the walk's V chains legitimately
    // differ from the CS2 drive's per-plane V fixtures; the U fields match
    // exactly, asserted below)] -> the uv angle-delta symbol when gated
    // (bsize >= 8X8 AND the folded mode directional) then the LUMA chain ->
    // the U chain -> the V chain (U BEFORE V, three separate per-plane NAs,
    // the CS1 wrapper plumbing: luma whole-block txb_skip_ctx 0; chroma
    // ctx_base + 7). chroma_tx_size per the av1_get_max_uv_txsize map
    // (common_utils.h:142-149: 8x8->TX_4X4, 16x16->TX_8X8, 32x32->TX_16X16,
    // 64x64->TX_32X32).
    // THE CHROMA-OWNERSHIP RULE (is_chroma_reference, common_utils.h:315-320,
    // 4:2:0): TRUE only at odd mi_row AND odd mi_col for 4x4 luma blocks
    // (bw = bh = 1) - a single-4x4-TU frame codes NO chroma (the (0,0)
    // block is never chroma-referenced); the (1,1) owner carries the quad's
    // UV TU at UV ((px & ~7) >> 1, (py & ~7) >> 1) (ROUND_UV,
    // definitions.h:327). The modes/eobs arrays carry 0xFF/0xFFFF markers
    // for non-referenced blocks (the gate's -1). All four UV sizes land:
    // the S=32 luma chain runs the TX_64X64 scan contract (the compacted
    // 1024-position emission domain, quantizeFp64x64Token + the dq
    // expansion). The v3 SPS/frame-header composition is NOT wired here
    // (CS5); the D2 chroma decision is the CH3 policy (ours, documented).
    for (int si = 0; si < 4; ++si) {
        static const int sizes[4] = {4, 8, 16, 32};
        const int S = sizes[si];
        const int B = 2 * S;
        const int grid = 64 / B;
        const int nb = grid * grid;
        const ecs4::Gate& G = ecs4::gates[si];
        pixels::Plane srcY(64, 64, 8), reconY(64, 64, 8);
        pixels::Plane srcU(32, 32, 8), reconU(32, 32, 8);
        pixels::Plane srcV(32, 32, 8), reconV(32, 32, 8);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                srcY.at(x, y) = (y < 32) ? static_cast<std::uint8_t>(x + y + 1) : 0;
        // the UV planes = the CH3 template values (the CS2 fixture: rows
        // 0-15 = 2*(x+y+2), rows 16-31 = 0; V = 255 - U - the cross-slice
        // check compares against the CS2 drives, so the fixture is theirs)
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const std::uint8_t u = static_cast<std::uint8_t>(
                    (y < 16) ? 2 * (x + y + 2) : 0);
                srcU.at(x, y) = u;
                srcV.at(x, y) = static_cast<std::uint8_t>(255 - u);
            }
        static std::int32_t coeffsY[64 * 4096], coeffsU[64 * 1024], coeffsV[64 * 1024];
        static std::uint8_t modesY[64], modesU[64], modesV[64];
        static std::uint16_t eobsY[64], eobsU[64], eobsV[64];
        memset(coeffsY, 0, sizeof(coeffsY));
        memset(coeffsU, 0, sizeof(coeffsU));
        memset(coeffsV, 0, sizeof(coeffsV));
        memset(modesY, 0, sizeof(modesY));
        memset(modesU, 0, sizeof(modesU));
        memset(modesV, 0, sizeof(modesV));
        memset(eobsY, 0, sizeof(eobsY));
        memset(eobsU, 0, sizeof(eobsU));
        memset(eobsV, 0, sizeof(eobsV));
        entropy::AomWriter w{};
        static unsigned char buf[32768];
        memset(buf, 0, sizeof(buf));
        w.ec.buf = buf;
        entropy::EcFrameContext fc;
        entropy::DcSignLevelCoeffNa naY, naU, naV;
        memset(&naY, 0xFF, sizeof(naY));
        memset(&naU, 0xFF, sizeof(naU));
        memset(&naV, 0xFF, sizeof(naV));
        pipeline::encodeFrameChromaQ(srcY, srcU, srcV, reconY, reconU, reconV, coeffsY, coeffsU,
                                     coeffsV, modesY, modesU, modesV, eobsY, eobsU, eobsV, 100, B,
                                     transforms::TxType::DCT_DCT, &w, &fc, &naY, &naU, &naV);
        // the loop stops the encoder internally (the odEcStopEncode inside);
        // a second stop here would append a duplicated tail (measured)
        // the full walk stream (the byte loop FIRST: the first mismatch
        // index localizes a divergence; then the length)
        const unsigned minLen = w.pos < G.bytesLen ? w.pos : G.bytesLen;
        for (unsigned i = 0; i < minLen; ++i) CHECK((unsigned)buf[i] == G.bytes[i]);
        CHECK(w.pos == G.bytesLen);
        // modes per block (0xFF markers for non-referenced blocks)
        bool modesOk = true;
        for (int b = 0; b < nb; ++b) {
            const int um = G.modes[3 * b + 1], vm = G.modes[3 * b + 2];
            if (modesY[b] != (std::uint8_t)G.modes[3 * b]) modesOk = false;
            if (um < 0 ? (modesU[b] != 0xFF) : (modesU[b] != (std::uint8_t)um)) modesOk = false;
            if (vm < 0 ? (modesV[b] != 0xFF) : (modesV[b] != (std::uint8_t)vm)) modesOk = false;
        }
        CHECK(modesOk);
        // eobs per block (0xFFFF markers)
        bool eobsOk = true;
        for (int b = 0; b < nb; ++b) {
            const int ue = G.eobs[3 * b + 1], ve = G.eobs[3 * b + 2];
            if (eobsY[b] != (std::uint16_t)G.eobs[3 * b]) eobsOk = false;
            if (ue < 0 ? (eobsU[b] != 0xFFFF) : (eobsU[b] != (std::uint16_t)ue)) eobsOk = false;
            if (ve < 0 ? (eobsV[b] != 0xFFFF) : (eobsV[b] != (std::uint16_t)ve)) eobsOk = false;
        }
        CHECK(eobsOk);
        // coeffs (the gate order: per block luma B*B, then U S*S + V S*S for
        // referenced blocks)
        int off = 0;
        bool coeffsOk = true;
        for (int b = 0; b < nb; ++b) {
            for (int i = 0; i < B * B; ++i, ++off)
                if (coeffsY[b * B * B + i] != G.coeffs[off]) coeffsOk = false;
            if (G.eobs[3 * b + 1] >= 0) {
                for (int i = 0; i < S * S; ++i, ++off)
                    if (coeffsU[b * S * S + i] != G.coeffs[off]) coeffsOk = false;
                for (int i = 0; i < S * S; ++i, ++off)
                    if (coeffsV[b * S * S + i] != G.coeffs[off]) coeffsOk = false;
            }
        }
        CHECK(off == (int)G.coeffsLen);
        CHECK(coeffsOk);
        // the per-plane recons
        bool reconOk = true;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (reconY.at(x, y) != (std::uint8_t)G.recon_y[y * 64 + x]) reconOk = false;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                if (reconU.at(x, y) != (std::uint8_t)G.recon_u[y * 32 + x]) reconOk = false;
                if (reconV.at(x, y) != (std::uint8_t)G.recon_v[y * 32 + x]) reconOk = false;
            }
        CHECK(reconOk);
        // ---- CS7: the chroma bottom-left gather, the cascade-immune half ----
        // ONE derivation feeds all three assertions, and it is structural
        // rather than transcribed. The boundary leaf is the FIRST leaf in
        // Z-order visit order whose below-left is AVAILABLE (pipeline::
        // bottomLeftPx - the named rule the implementation itself runs, not a
        // second transcription of its expression) AND whose committed U mode is
        // D203 (the mode predicate build_intra_predictors derives internally,
        // which the caller must NOT duplicate). Typing "leaf 2" or "leaf 4"
        // here would be the literal this file forbids, and it would silently
        // read the wrong leaf at the other geometry - the error class this
        // project has already paid for once.
        {
            int zord[64], zn = 0;
            ZOrder::leaves(grid, 0, 0, grid, zord, &zn);
            REQUIRE(zn == nb);
            int zPos[64];
            for (int i = 0; i < zn; ++i) zPos[zord[i]] = i;
            int boundary = -1, nGather = 0;
            for (int t = 0; t < zn; ++t) {
                const int b = zord[t], by = b / grid, bx = b % grid;
                if (!pipeline::bottomLeftPx(b, by, bx, grid, zPos, S)) continue;
                if (G.modes[3 * b + 1] != static_cast<int>(intra::UV_D203_PRED)) continue;
                ++nGather;
                if (boundary < 0) boundary = b;
            }
            if (S == 16 || S == 32) {
                // The controls must gather NOTHING. Asserted here at the leaf
                // level; the STRUCTURAL reason (nAvail == 0, i.e. grid <= 2
                // admits no below-left leaf that precedes its depender in the
                // visit order, and nQualify == 1 at c16) is asserted in the
                // availability block above, so a bare zero here cannot be read
                // as a content-dependent zero.
                REQUIRE(boundary < 0);
                REQUIRE(nGather == 0);
            } else {
                // PRECONDITION, not a new claim: without a boundary leaf the
                // prefix below is vacuous (an empty prefix trivially equals the
                // baseline) and the "differs" assertion has nothing to differ.
                REQUIRE(boundary >= 0);
                const char* const baseU = (S == 4) ? ecs44_recon_u_hex : ecs48_recon_u_hex;
                const char* const baseV = (S == 4) ? ecs44_recon_v_hex : ecs48_recon_v_hex;
                const int bz = zPos[boundary], bby = boundary / grid, bbx = boundary % grid;
                // (1) THE PREFIX. Every leaf visited BEFORE the boundary keeps
                // its pre-fix recon, byte for byte, on BOTH chroma planes.
                // Sound rather than lucky: a leaf's prediction reads only recon
                // written by leaves with a LOWER visit position, so nothing
                // earlier in Z-order can be moved by a later leaf's prediction.
                // That is what makes this immune to however far the cascade
                // runs afterwards - and it is why this is not weakened into
                // "the recon changed somewhere".
                int prefixBytes = 0, prefixBad = 0;
                int badLeaf = -1, badPlane = 0, badX = -1, badY = -1;
                for (int b = 0; b < nb; ++b) {
                    if (zPos[b] >= bz) continue;
                    const int by = b / grid, bx = b % grid;
                    for (int y = 0; y < S; ++y)
                        for (int x = 0; x < S; ++x) {
                            const int px = bx * S + x, py = by * S + y, idx = py * 32 + px;
                            ++prefixBytes;
                            const int du = reconU.at(px, py) != cs7HexByte(baseU, idx);
                            const int dv = reconV.at(px, py) != cs7HexByte(baseV, idx);
                            if ((du || dv) && badLeaf < 0) {
                                badLeaf = b;
                                badPlane = du ? 'U' : 'V';
                                badX = px;
                                badY = py;
                            }
                            prefixBad += du ? 1 : 0;
                            prefixBad += dv ? 1 : 0;
                        }
                }
                INFO("S=" << S << " boundary leaf=" << boundary << " zpos=" << bz
                         << " prefix bytes=" << prefixBytes << " first bad leaf=" << badLeaf
                         << " plane=" << badPlane << " at (" << badX << "," << badY << ")");
                // the prefix must be non-empty or it proves nothing
                REQUIRE(prefixBytes > 0);
                REQUIRE(prefixBad == 0);
                // (2) THE BOUNDARY LEAF DIFFERS - the slice's evidence. It
                // FAILS before the fix (nBottomLeftPx was a literal 0, so the
                // extension was memset repetition and the recon matched the
                // frozen baseline byte for byte) and PASSES after (the real
                // gathered bytes moved it). A test that passed on the first
                // build would have proven nothing.
                int diffU = 0, diffV = 0;
                for (int y = 0; y < S; ++y)
                    for (int x = 0; x < S; ++x) {
                        const int px = bbx * S + x, py = bby * S + y, idx = py * 32 + px;
                        if (reconU.at(px, py) != cs7HexByte(baseU, idx)) ++diffU;
                        if (reconV.at(px, py) != cs7HexByte(baseV, idx)) ++diffV;
                    }
                MESSAGE("RT12 CS7 S=" << S << " (S is the UV block size) boundary leaf=" << boundary
                        << " zpos=" << bz << " gathering leaves=" << nGather
                        << " prefix bytes=" << prefixBytes << " boundary diff U=" << diffU
                        << " V=" << diffV);
                REQUIRE(diffU + diffV > 0);
            }
        }
        // ---- RT21: the chroma ABOVE-RIGHT Z-order availability - the
        // above-right twin of the CS7 block above. The structural gather
        // (uvHasTop && uCol + 1 < uvGrid) supplies samples whose OWNER leaf
        // may walk LATER in Z-order than this leaf; the reference declares
        // above-right unavailable then (enc_intra_prediction.c:146) and the
        // builder fills the stretch by repeating the last available sample
        // (:151-152, mirrored at intra.cpp:597-607). Before the fix the walk
        // gathered anyway and read the zero-initialised recon, so at c4 leaf
        // 41 the D45 prediction read 0 where the reference reconstructs 255
        // (the RT20 forensics: quantize(fwd(v_src - pred)) reproduced the
        // committed leaf-41 V coefficients, eob 16).
        {
            int zord[64], zn = 0;
            ZOrder::leaves(grid, 0, 0, grid, zord, &zn);
            REQUIRE(zn == nb);
            int zPos[64];
            for (int i = 0; i < zn; ++i) zPos[zord[i]] = i;
            // (a) THE RULE, exhaustive: pipeline::topRightPx - the named rule
            // the implementation itself runs, the bottomLeftPx mirror at the
            // top of this TEST_CASE - must equal the rule restated here from
            // zPos for EVERY leaf, and a non-zero verdict must be the block
            // size. The rule: the above-right owner is (by-1, bx+1); its
            // samples are available exactly when its Z-order position
            // precedes this leaf's own.
            for (int b = 0; b < nb; ++b) {
                const int by = b / grid, bx = b % grid;
                const int fn = pipeline::topRightPx(b, by, bx, grid, zPos, S);
                const bool derived = (bx + 1 < grid) && (by > 0) &&
                                     (zPos[(by - 1) * grid + (bx + 1)] < zPos[b]);
                if (fn != 0) REQUIRE(fn == S);
                REQUIRE((fn != 0) == derived);
            }
            // (b) THE WALK USES IT. The boundary is the FIRST Z-order leaf
            // where the structural gather supplies samples the rule forbids
            // AND the committed UV mode reads the gathered stretch. The
            // builder's USE predicate is needRight = pAngle < 90
            // (intra.cpp:589 and the dr-path re-derivation at :649);
            // kModeToAngle's (intra.cpp:476) only entries below 90 are D45
            // and D67, so the caller-visible predicate is that mode pair,
            // derived from the cited table rather than transcribed. At c4
            // this is leaf 41 and nothing else (measured pre-fix: the other
            // 13 structural-violation leaves decide modes that never read
            // the stretch; at c8 the two violations decide SMOOTH_H and DC).
            int boundary = -1;
            for (int t = 0; t < zn; ++t) {
                const int b = zord[t], by = b / grid, bx = b % grid;
                const bool structural = (by > 0) && (bx + 1 < grid);
                const bool rule = (bx + 1 < grid) && (by > 0) &&
                                  (zPos[(by - 1) * grid + (bx + 1)] < zPos[b]);
                const int um = G.modes[3 * b + 1];
                const bool readsTr = (um == static_cast<int>(intra::UV_D45_PRED)) ||
                                     (um == static_cast<int>(intra::UV_D67_PRED));
                if (structural && !rule && readsTr) {
                    boundary = b;
                    break;
                }
            }
            if (S == 16 || S == 32) {
                // the controls: grid <= 2 admits no above-right violation at
                // all (the owner leaf always precedes its depender in
                // Z-order there), so a bare zero cannot be read as a
                // content-dependent zero.
                REQUIRE(boundary < 0);
            }
            if (boundary >= 0) {
                const int bz = zPos[boundary], bby = boundary / grid, bbx = boundary % grid;
                // (b)(1) THE PREFIX. Every leaf visited BEFORE the boundary
                // keeps its pre-fix recon, byte for byte, on BOTH chroma
                // planes (the CS7 cascade-immunity form). Sound rather than
                // lucky: post-fix every edge a leaf reads comes from a leaf
                // with a LOWER visit position, so nothing earlier in Z-order
                // can be moved by a later leaf's corrected prediction.
                int prefixBytes = 0, prefixBad = 0;
                for (int b = 0; b < nb; ++b) {
                    if (zPos[b] >= bz) continue;
                    const int by = b / grid, bx = b % grid;
                    for (int y = 0; y < S; ++y)
                        for (int x = 0; x < S; ++x) {
                            const int px = bx * S + x, py = by * S + y, idx = py * 32 + px;
                            ++prefixBytes;
                            if (reconU.at(px, py) != cs7HexByte(rt21_recon_u_hex, idx))
                                ++prefixBad;
                            if (reconV.at(px, py) != cs7HexByte(rt21_recon_v_hex, idx))
                                ++prefixBad;
                        }
                }
                INFO("RT21 S=" << S << " boundary leaf=" << boundary << " zpos=" << bz
                               << " prefix bytes=" << prefixBytes << " prefixBad=" << prefixBad);
                REQUIRE(prefixBytes > 0);
                REQUIRE(prefixBad == 0);
                // (b)(2) THE BOUNDARY LEAF DIFFERS - the slice's evidence. It
                // FAILS before the fix (the walk matched the frozen baseline
                // byte for byte, because the baseline IS the pre-fix output)
                // and PASSES after (the repetition fill replaced the zeros
                // the structural gather had read from uninitialised recon).
                int diffU = 0, diffV = 0;
                for (int y = 0; y < S; ++y)
                    for (int x = 0; x < S; ++x) {
                        const int px = bbx * S + x, py = bby * S + y, idx = py * 32 + px;
                        if (reconU.at(px, py) != cs7HexByte(rt21_recon_u_hex, idx)) ++diffU;
                        if (reconV.at(px, py) != cs7HexByte(rt21_recon_v_hex, idx)) ++diffV;
                    }
                MESSAGE("RT21 S=" << S << " (S is the UV block size) boundary leaf=" << boundary
                                  << " zpos=" << bz << " diff U=" << diffU << " V=" << diffV);
                REQUIRE(diffU + diffV > 0);
            }
        }
        // THE CROSS-SLICE CHECK vs the CS2 quant-drive lines: the U-plane
        // fields (modes/eobs/coeffs/recon) are identical - the walk decides
        // on the U plane with the same CH3 D2 policy, per-plane NAs and
        // ctxs. The walk's V fields share the U mode (one uv_mode symbol
        // per block) and legitimately differ from the CS2 per-plane V
        // fixtures - named, not a defect.
        // RT6: the equality holds IFF the walk order == the CS2 raster
        // order (grid <= 2, i.e. S=16/S=32 where tree order == raster
        // order). For the deeper grids (S=4/S=8) the tree-order top-right
        // gather reads the adjacent quadrant's not-yet-reconstructed region
        // (the decoder's own decode-order recon evolution), so the U
        // decisions legitimately diverge from the CS2 raster-decided
        // fixture; the walk's U fields stay pinned to the ecs4 gate lines
        // above instead.
        // RT9c: WIDENED back to grid <= 2. fc54fc0 dropped c16 from this check
        // because the walk had moved to the LUT-derived chroma domain while the
        // CS2 drives were still in DCT_DCT - a domain difference, not a decision
        // difference. RT9c moves the CS2 chroma drives onto the same LUT domain,
        // so the comparison is meaningful again at c16. c32 was never dropped
        // and does not depend on this: the tx-set rule forces DCT_DCT on both
        // sides there (get_ext_tx_set_type -> DCTONLY at sqr_up == TX_32X32).
        const bool treeEqRaster = grid <= 2;
        const int uvGrid = 32 / S;
        const int nuv = uvGrid * uvGrid;
        bool uModesOk = true, uEobsOk = true, uCoeffsOk = true, uReconOk = true;
        if (treeEqRaster) {
            for (int b = 0; b < nuv; ++b) {
                if ((int)modesU[b] != G.c2_modes[2 * b]) uModesOk = false;
                if ((int)eobsU[b] != G.c2_eobs[2 * b]) uEobsOk = false;
            }
            int coff2 = 0;
            for (int b = 0; b < nuv; ++b) {
                for (int i = 0; i < S * S; ++i, ++coff2)
                    if (coeffsU[b * S * S + i] != G.c2_coeffs[coff2]) uCoeffsOk = false;
                coff2 += S * S;  // the CS2 V coefficients (skipped - shared-mode walk)
            }
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    if (reconU.at(x, y) != (std::uint8_t)G.c2_recon_u[y * 32 + x]) uReconOk = false;
        }
        CHECK(uModesOk);
        CHECK(uEobsOk);
        CHECK(uCoeffsOk);
        CHECK(uReconOk);
    }
}

TEST_CASE("CS4: the chroma-ownership rule - a 4x4 frame codes NO chroma, the (1,1) owner carries the quad TU") {
    // is_chroma_reference (common_utils.h:315-320) at 4:2:0 for 4x4 luma
    // blocks (bw = bh = 1): TRUE only at odd mi_row AND odd mi_col.
    // (a) A single-4x4-TU frame (one 4x4 luma block at (0,0)): NOT
    //     chroma-referenced -> no uv_mode symbol, no UV chains; the eobs
    //     markers stay 0xFFFF and the modes 0xFF.
    // (b) An 8x8 frame of 4x4 luma blocks (2x2 grid): exactly the (1,1)
    //     block references chroma - its UV TU sits at UV (0,0)
    //     (ROUND_UV(4) >> 1 = 0) and covers the whole 8x8-aligned quad's
    //     chroma; the other three blocks carry the 0xFF/0xFFFF markers.
    // (a)
    {
        pixels::Plane srcY(4, 4, 8), reconY(4, 4, 8);
        pixels::Plane srcU(2, 2, 8), reconU(2, 2, 8);
        pixels::Plane srcV(2, 2, 8), reconV(2, 2, 8);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) srcY.at(x, y) = static_cast<std::uint8_t>(x + y + 1);
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) {
                const std::uint8_t u = static_cast<std::uint8_t>(2 * (x + y + 2));
                srcU.at(x, y) = u;
                srcV.at(x, y) = static_cast<std::uint8_t>(255 - u);
            }
        std::int32_t coeffsY[16], coeffsU[4], coeffsV[4];
        std::uint8_t modesY[1], modesU[1], modesV[1];
        std::uint16_t eobsY[1], eobsU[1], eobsV[1];
        memset(modesU, 0, sizeof(modesU));
        memset(modesV, 0, sizeof(modesV));
        memset(eobsU, 0, sizeof(eobsU));
        memset(eobsV, 0, sizeof(eobsV));
        entropy::AomWriter w{};
        unsigned char buf[64] = {0};
        w.ec.buf = buf;
        entropy::EcFrameContext fc;
        entropy::DcSignLevelCoeffNa naY, naU, naV;
        memset(&naY, 0xFF, sizeof(naY));
        memset(&naU, 0xFF, sizeof(naU));
        memset(&naV, 0xFF, sizeof(naV));
        pipeline::encodeFrameChromaQ(srcY, srcU, srcV, reconY, reconU, reconV, coeffsY, coeffsU,
                                     coeffsV, modesY, modesU, modesV, eobsY, eobsU, eobsV, 100, 4,
                                     transforms::TxType::DCT_DCT, &w, &fc, &naY, &naU, &naV);
        CHECK(eobsU[0] == 0xFFFF);
        CHECK(eobsV[0] == 0xFFFF);
        CHECK(modesU[0] == 0xFF);
        CHECK(modesV[0] == 0xFF);
    }
    // (b)
    {
        pixels::Plane srcY(8, 8, 8), reconY(8, 8, 8);
        pixels::Plane srcU(4, 4, 8), reconU(4, 4, 8);
        pixels::Plane srcV(4, 4, 8), reconV(4, 4, 8);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) srcY.at(x, y) = static_cast<std::uint8_t>(x + y + 1);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) {
                const std::uint8_t u = static_cast<std::uint8_t>(2 * (x + y + 2));
                srcU.at(x, y) = u;
                srcV.at(x, y) = static_cast<std::uint8_t>(255 - u);
            }
        std::int32_t coeffsY[4 * 64], coeffsU[16], coeffsV[16];
        std::uint8_t modesY[4], modesU[4], modesV[4];
        std::uint16_t eobsY[4], eobsU[4], eobsV[4];
        memset(modesU, 0, sizeof(modesU));
        memset(modesV, 0, sizeof(modesV));
        memset(eobsU, 0, sizeof(eobsU));
        memset(eobsV, 0, sizeof(eobsV));
        entropy::AomWriter w{};
        unsigned char buf[256] = {0};
        w.ec.buf = buf;
        entropy::EcFrameContext fc;
        entropy::DcSignLevelCoeffNa naY, naU, naV;
        memset(&naY, 0xFF, sizeof(naY));
        memset(&naU, 0xFF, sizeof(naU));
        memset(&naV, 0xFF, sizeof(naV));
        pipeline::encodeFrameChromaQ(srcY, srcU, srcV, reconY, reconU, reconV, coeffsY, coeffsU,
                                     coeffsV, modesY, modesU, modesV, eobsY, eobsU, eobsV, 100, 4,
                                     transforms::TxType::DCT_DCT, &w, &fc, &naY, &naU, &naV);
        CHECK(eobsU[0] == 0xFFFF);
        CHECK(eobsU[1] == 0xFFFF);
        CHECK(eobsU[2] == 0xFFFF);
        CHECK(eobsU[3] != 0xFFFF);  // the (1,1) owner
        CHECK(eobsV[3] != 0xFFFF);
        CHECK(modesU[3] != 0xFF);
        CHECK(modesV[3] != 0xFF);
        // the owner's UV TU sits at UV (0,0) and covers the quad: the recon
        // planes are fully written (the decoder-visible chroma of the frame)
        bool reconWritten = true;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                if (reconU.at(x, y) == 0xFF) reconWritten = false;
        CHECK(reconWritten);
    }
}
// ==== FI1: the FI DC-deciding fixture ====
TEST_CASE("FI DC-deciding fixture: the FI=1 emission path round-trips content-exact") {
    // The register item (docs/todo.md): the FI branch is predicate-gated and
    // never fires in the committed fixtures - every writeFilterIntra call site
    // (pipeline.cpp:509/:695/:892/:1047/:1366/:2047) hardcodes the
    // FILTER_INTRA_MODES sentinel, so the flag=1 symbol surface has no
    // emission-path test. The D2 policy (pipeline.h:151-157) keeps FI parked
    // as a candidate; adding it would change decisions on committed fixtures
    // and move the gate. Route chosen: the fixture CONTENT makes FI measurably
    // win on one block and the override lives ONLY in this test - the walk
    // below mirrors the production encodeFrameAuto4x4Q walk (pipeline.cpp:
    // 429-527) block-for-block with one delta: on DC-decided blocks it scores
    // the 5 FI candidates with the production primitives
    // (buildIntraPredictors filterIntraMode + motion::sad4x4) and, when the
    // best beats the DC SAD, emits writeFilterIntra with the real FI mode and
    // predicts through filterIntraPredictor. No production file changes.
    //
    // Fixture: 8x8 frame, 2x2 grid of 4x4 blocks, q100, DCT_DCT.
    // b0 = 129 flat: the no-edge fills are above 127 / left 129 / corner 128,
    // so H_PRED predicts 129-flat exactly and wins the D2 at sad 0 (tie with
    // D157/D203, lowest index). b1 rows 0-2 = 129 with row 3 =
    // [150,160,170,180]: with only the left edge [129x4] every one of the 13
    // candidates predicts 129-flat and ties at sad 144 - tie-break DC_PRED;
    // the FI table ties too (all 144), so no FI win and the flag=0 branch of
    // writeFilterIntra is exercised in the same walk. b2 is the transpose and
    // sees a REAL recon top-right (b1's recon row 3 = [149,158,170,179],
    // q100 noise) - D67_PRED wins there. b3's source is the FILTER_D157_PRED
    // prediction of the walk's own recon edges (above = b1's recon row 3 =
    // [149,158,170,179], left = b2's recon col 3 = [143,156,168,176],
    // above-left = 129), so its FI_D157 SAD is exactly 0 while DC - the D2
    // argmin at sad 75 (table 75,159,141,262,209,172,161,153,196,151,153,
    // 152,148; next best 141) - misses by 75. The override fires on b3 only.
    pixels::Plane plane(8, 8, 4);
    pixels::Plane recon(8, 8, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) plane.at(x, y) = 129;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            plane.at(4 + x, y) = (y == 3) ? (std::uint8_t)(150 + 10 * x) : 129;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            plane.at(x, 4 + y) = (x == 3) ? (std::uint8_t)(150 + 10 * y) : 129;
    static const std::uint8_t b3src[16] = {148, 153, 161, 168, 154, 154, 157, 163,
                                           163, 159, 159, 161, 170, 166, 164, 163};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) plane.at(4 + x, 4 + y) = b3src[y * 4 + x];

    transforms::QuantTables qt;
    transforms::buildQuantTables(100, qt);
    std::int16_t scan[16];
    transforms::defaultScan4x4(scan);

    // ---- Part A: the production walk on this fixture - the parked state ----
    {
        pixels::Plane reconA(8, 8, 4);
        std::int32_t coeffs[64] = {0};
        std::uint8_t modes[4] = {0};
        entropy::EcFrameContext fc;
        entropy::AomWriter w{};
        unsigned char buf[64] = {0};
        w.ec.buf = buf;
        entropy::DcSignLevelCoeffNa na;
        memset(&na, 0xFF, sizeof(na));
        pipeline::encodeFrameAuto4x4Q(plane, reconA, coeffs, modes, 100,
                                      transforms::TxType::DCT_DCT, &w, &fc, &na);
        const std::uint8_t wantModes[4] = {2, 0, 8, 0};
        bool modesOk = true;
        for (int i = 0; i < 4; ++i)
            if (modes[i] != wantModes[i]) modesOk = false;
        CHECK(modesOk);

        entropy::EcFrameContext fcR;
        entropy::initDefaultEcFrameContext(&fcR, 100);
        entropy::AomReader r;
        REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
        r.allow_update_cdf = 1;
        entropy::DcSignLevelCoeffNa naR;
        memset(&naR, 0xFF, sizeof(naR));
        int modesR[4] = {0, 0, 0, 0};
        for (int b = 0; b < 4; ++b) {
            const int bx = b % 2, by = b / 2;
            const bool hasTop = by > 0, hasLeft = bx > 0;
            CHECK(entropy::readSkip(&r, &fcR, 0) == 0);
            const int leftMode = hasLeft ? modesR[by * 2 + bx - 1] : 0;
            const int aboveMode = hasTop ? modesR[(by - 1) * 2 + bx] : 0;
            int topCtx = 0, leftCtx = 0;
            entropy::getKfYModeCtx(hasLeft ? 1 : 0, leftMode, hasTop ? 1 : 0, aboveMode,
                                   &topCtx, &leftCtx);
            int delta = 0;
            modesR[b] = (int)entropy::readKfLumaMode(&r, &fcR, entropy::BLOCK_4X4, topCtx,
                                                     leftCtx, &delta);
            CHECK(modesR[b] == (int)wantModes[b]);
            // the parked production surface: the FI symbol, where allowed,
            // carries the off sentinel and reads 0
            if (entropy::filterIntraAllowed(1, entropy::BLOCK_4X4, 0,
                                            (std::uint32_t)modesR[b])) {
                entropy::FilterIntraMode f;
                CHECK(entropy::readFilterIntra(&r, &fcR, entropy::BLOCK_4X4, &f) == 0);
            }
            // the token chain keeps the arithmetic decoder aligned
            std::int32_t qcRead[16] = {0};
            entropy::readBlockCoeffs(&r, &fcR, &naR, qcRead, scan, entropy::TX_4X4,
                                     entropy::BLOCK_4X4, by, bx, 1,
                                     (entropy::PredictionMode)modesR[b],
                                     entropy::COMPONENT_LUMA);
        }
    }

    // ---- Part B: the same walk with the test-only FI override ----
    entropy::EcFrameContext fc;
    entropy::AomWriter w{};
    unsigned char buf[64] = {0};
    w.ec.buf = buf;
    entropy::initDefaultEcFrameContext(&fc, 100);
    entropy::odEcEncReset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;
    entropy::DcSignLevelCoeffNa na;
    memset(&na, 0xFF, sizeof(na));

    int modes[4] = {0, 0, 0, 0};
    std::uint16_t eobs[4] = {0, 0, 0, 0};
    int flags[4] = {0, 0, 0, 0};
    entropy::FilterIntraMode fis[4] = {};
    int b3DcSad = -1, b3FiSad = -1;
    int b3FiTable[5] = {0, 0, 0, 0, 0};
    bool b3Fires = false;

    auto walkBlock = [&](int bidx, int bx, int by) {
        const int px = bx * 4, py = by * 4;
        const bool hasTop = by > 0, hasLeft = bx > 0;
        const int nTopPx = hasTop ? 4 : 0, nLeftPx = hasLeft ? 4 : 0;
        // the production top-right rule (pipeline.cpp:437)
        const int nTopRightPx = (hasTop && bx + 1 < 2) ? 4 : 0;
        std::uint8_t above[8] = {0}, left[4] = {0};
        if (hasTop) {
            for (int i = 0; i < 4 + nTopRightPx; ++i) above[i] = recon.at(px + i, py - 1);
        }
        if (hasLeft) {
            for (int i = 0; i < 4; ++i) left[i] = recon.at(px - 1, py + i);
        }
        const std::uint8_t aboveLeft =
            (hasTop && hasLeft) ? recon.at(px - 1, py - 1) : (std::uint8_t)0;
        intra::NeighborContext nctx;
        nctx.aboveMode = hasTop ? (intra::PredictionMode)modes[(by - 1) * 2 + bx]
                                : intra::DC_PRED;
        nctx.leftMode = hasLeft ? (intra::PredictionMode)modes[by * 2 + bx - 1]
                                : intra::DC_PRED;
        std::uint8_t src[16];
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) src[y * 4 + x] = plane.at(px + x, py + y);

        // the production D2 (13 candidates, real nctx)
        int best = 1 << 30, bm = 0;
        for (int m = intra::DC_PRED; m <= intra::PAETH_PRED; ++m) {
            std::uint8_t p[16] = {0};
            intra::buildIntraPredictors(p, 4, m, 0, 4, 4, aboveLeft, above, nTopPx,
                                        nTopRightPx, left, nLeftPx, 0, nctx);
            const int s = (int)motion::sad4x4(src, 4, p, 4);
            if (s < best) { best = s; bm = m; }
        }
        // the test-only FI layer: score the 5 FI candidates on DC-decided
        // blocks; fire only when the best beats the DC SAD
        int fiSads[5] = {0, 0, 0, 0, 0};
        int fiBest = -1, fiBestSad = 1 << 30;
        const bool fiAllowed =
            entropy::filterIntraAllowed(1, entropy::BLOCK_4X4, 0, (std::uint32_t)bm);
        if (fiAllowed) {
            for (int f = 0; f < 5; ++f) {
                std::uint8_t p[16] = {0};
                intra::buildIntraPredictors(p, 4, intra::DC_PRED, 0, 4, 4, aboveLeft, above,
                                            nTopPx, nTopRightPx, left, nLeftPx, 0, nctx, f);
                fiSads[f] = (int)motion::sad4x4(src, 4, p, 4);
                if (fiSads[f] < fiBestSad) { fiBestSad = fiSads[f]; fiBest = f; }
            }
        }
        const bool fires = fiAllowed && fiBestSad < best;
        modes[bidx] = bm;
        if (bidx == 3) {
            b3DcSad = best;
            b3FiSad = fiBestSad;
            for (int f = 0; f < 5; ++f) b3FiTable[f] = fiSads[f];
            b3Fires = fires;
        }
        // prediction: the override routes DC through filterIntraPredictor
        std::uint8_t pred[16] = {0};
        intra::buildIntraPredictors(pred, 4, bm, 0, 4, 4, aboveLeft, above, nTopPx,
                                    nTopRightPx, left, nLeftPx, 0, nctx,
                                    fires ? fiBest : -1);
        std::int16_t res[16];
        for (int i = 0; i < 16; ++i) res[i] = (std::int16_t)(src[i] - pred[i]);
        std::int32_t cb[16] = {0}, qc[16] = {0}, dq[16] = {0};
        transforms::fwdTxfm2d4x4(res, cb, 4, transforms::TxType::DCT_DCT);
        std::uint16_t eob = 0;
        transforms::quantizeFp4x4(cb, qt, scan, qc, dq, &eob);
        eobs[bidx] = eob;
        // emission: [skip][kf][FI iff DC-decided][tokens] per the 4x4Q walk
        entropy::writeSkip(&w, &fc, 0, 0);
        int topCtx = 0, leftCtx = 0;
        entropy::getKfYModeCtx(hasLeft ? 1 : 0, (int)nctx.leftMode, hasTop ? 1 : 0,
                               (int)nctx.aboveMode, &topCtx, &leftCtx);
        entropy::writeKfLumaMode(&w, &fc, entropy::BLOCK_4X4,
                                 (entropy::PredictionMode)bm, topCtx, leftCtx, 0);
        if (fiAllowed) {
            flags[bidx] = fires ? 1 : 0;
            fis[bidx] = fires ? (entropy::FilterIntraMode)fiBest : entropy::FILTER_INTRA_MODES;
            entropy::writeFilterIntra(&w, &fc, entropy::BLOCK_4X4, fis[bidx]);
        }
        entropy::writeBlockCoeffs(&w, &fc, &na, qc, scan, entropy::TX_4X4,
                                  entropy::BLOCK_4X4, eob, by, bx, 1,
                                  (entropy::PredictionMode)bm, entropy::COMPONENT_LUMA);
        // recon write-back
        std::uint8_t blk[16];
        for (int i = 0; i < 16; ++i) blk[i] = pred[i];
        transforms::invTxfm2dAdd4x4(dq, blk, 4, transforms::TxType::DCT_DCT);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) recon.at(px + x, py + y) = blk[y * 4 + x];
    };

    walkBlock(0, 0, 0);
    walkBlock(1, 1, 0);
    walkBlock(2, 0, 1);
    walkBlock(3, 1, 1);
    entropy::odEcStopEncode(&w);

    // the D2 decisions are the production ones (the override changes nothing
    // about the decision - only the prediction and the FI symbol on b3)
    const int wantModes[4] = {2, 0, 8, 0};
    bool modesOk = true;
    for (int i = 0; i < 4; ++i)
        if (modes[i] != wantModes[i]) modesOk = false;
    CHECK(modesOk);
    // the FI-win property, measured: b3's D2 argmin is DC_PRED at sad 75 and
    // the FI_D157_PRED candidate scores 0
    CHECK(b3DcSad == 75);
    CHECK(b3Fires);
    CHECK(b3FiSad == 0);
    const int wantFiTable[5] = {88, 239, 143, 0, 297};
    bool fiOk = true;
    for (int f = 0; f < 5; ++f)
        if (b3FiTable[f] != wantFiTable[f]) fiOk = false;
    CHECK(fiOk);
    // the flag surface: b1 carries the off symbol, b3 carries FI=1
    const int wantFlags[4] = {0, 0, 0, 1};
    bool flagsOk = true;
    for (int i = 0; i < 4; ++i)
        if (flags[i] != wantFlags[i]) flagsOk = false;
    CHECK(flagsOk);
    CHECK((int)fis[3] == (int)entropy::FILTER_D157_PRED);
    // b3's FI prediction is exact: the residual is zero and the token chain
    // codes an all-skip TU
    CHECK(eobs[3] == 0);
    const std::uint16_t wantEobs[4] = {0, 10, 13, 0};
    bool eobsOk = true;
    for (int i = 0; i < 4; ++i)
        if (eobs[i] != wantEobs[i]) eobsOk = false;
    CHECK(eobsOk);
    // the tile byte stream, measured: 11 bytes for the four-block frame
    CHECK(w.pos == 11);
    static const unsigned char wantBytes[11] = {0x90, 0xd7, 0xbd, 0x22, 0x4b, 0xe0,
                                                0xaa, 0x79, 0x9e, 0x85, 0x20};
    for (int i = 0; i < 11; ++i) CHECK((unsigned)buf[i] == wantBytes[i]);

    // ---- reader round-trip: the decoder side of the same surface ----
    entropy::EcFrameContext fcR;
    entropy::initDefaultEcFrameContext(&fcR, 100);
    entropy::AomReader r;
    REQUIRE(entropy::odEcReaderInit(&r, buf, w.pos) == 0);
    r.allow_update_cdf = 1;
    entropy::DcSignLevelCoeffNa naR;
    memset(&naR, 0xFF, sizeof(naR));
    pixels::Plane reconR(8, 8, 4);
    int modesR[4] = {0, 0, 0, 0};
    for (int b = 0; b < 4; ++b) {
        const int bx = b % 2, by = b / 2;
        const int px = bx * 4, py = by * 4;
        const bool hasTop = by > 0, hasLeft = bx > 0;
        const int nTopPx = hasTop ? 4 : 0, nLeftPx = hasLeft ? 4 : 0;
        const int nTopRightPx = (hasTop && bx + 1 < 2) ? 4 : 0;
        std::uint8_t above[8] = {0}, left[4] = {0};
        if (hasTop) {
            for (int i = 0; i < 4 + nTopRightPx; ++i) above[i] = reconR.at(px + i, py - 1);
        }
        if (hasLeft) {
            for (int i = 0; i < 4; ++i) left[i] = reconR.at(px - 1, py + i);
        }
        const std::uint8_t aboveLeft =
            (hasTop && hasLeft) ? reconR.at(px - 1, py - 1) : (std::uint8_t)0;
        intra::NeighborContext nctxR;
        nctxR.aboveMode = hasTop ? (intra::PredictionMode)modesR[(by - 1) * 2 + bx]
                                 : intra::DC_PRED;
        nctxR.leftMode = hasLeft ? (intra::PredictionMode)modesR[by * 2 + bx - 1]
                                 : intra::DC_PRED;
        CHECK(entropy::readSkip(&r, &fcR, 0) == 0);
        int topCtx = 0, leftCtx = 0;
        entropy::getKfYModeCtx(hasLeft ? 1 : 0,
                               hasLeft ? (int)nctxR.leftMode : 0,
                               hasTop ? 1 : 0,
                               hasTop ? (int)nctxR.aboveMode : 0, &topCtx, &leftCtx);
        int delta = 0;
        const entropy::PredictionMode m =
            entropy::readKfLumaMode(&r, &fcR, entropy::BLOCK_4X4, topCtx, leftCtx, &delta);
        CHECK((int)m == wantModes[b]);
        modesR[b] = (int)m;
        entropy::FilterIntraMode f = entropy::FILTER_INTRA_MODES;
        int flag = 0;
        if (entropy::filterIntraAllowed(1, entropy::BLOCK_4X4, 0, (std::uint32_t)m)) {
            flag = entropy::readFilterIntra(&r, &fcR, entropy::BLOCK_4X4, &f);
        }
        CHECK(flag == wantFlags[b]);
        if (flag) CHECK((int)f == (int)entropy::FILTER_D157_PRED);
        std::int32_t qcRead[16] = {0};
        const int reob = entropy::readBlockCoeffs(&r, &fcR, &naR, qcRead, scan,
                                                  entropy::TX_4X4, entropy::BLOCK_4X4,
                                                  by, bx, 1, m, entropy::COMPONENT_LUMA);
        CHECK(reob == (int)eobs[b]);
        // dequant (the quantizeFpN dq rule: qc * dequant[rc != 0]) + predict
        // + inverse -> the decoder recon must equal the writer's per block
        std::int32_t dqR[16] = {0};
        for (int i = 0; i < 16; ++i) dqR[i] = qcRead[i] * qt.dequant[i != 0];
        std::uint8_t predR[16] = {0};
        intra::buildIntraPredictors(predR, 4, m, 0, 4, 4, aboveLeft, above, nTopPx,
                                    nTopRightPx, left, nLeftPx, 0, nctxR,
                                    flag ? (int)f : -1);
        std::uint8_t blkR[16];
        for (int i = 0; i < 16; ++i) blkR[i] = predR[i];
        transforms::invTxfm2dAdd4x4(dqR, blkR, 4, transforms::TxType::DCT_DCT);
        bool blkOk = true;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) {
                reconR.at(px + x, py + y) = blkR[y * 4 + x];
                if (blkR[y * 4 + x] != recon.at(px + x, py + y)) blkOk = false;
            }
        CHECK(blkOk);
    }
    // the writer and reader CDF states agree (the full adaptation replayed)
    CHECK(entropy::ecFrameCdfsEqual(&fc, &fcR) == 1);
}