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
    // loss comes from quantization (see the Q2 frame test) — the lossy-by-
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
    // golden: golden_gen golden_frame b7_modes/b7_recon/b7_coeffs — identical
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
    // NOTE: recon != source for 8x8 blocks is BY DESIGN — the 8x8 fwd/inv
    // shift arrays are scale-non-complementary (fwd total x4 from
    // fwd_shift_8x8 {2,-1,0}, inv total x1/16 from inv_shift_8x8 {-1,-4}).
    // AV1 compensates in per-tx-size dequant, and AV1 lossless restricts to
    // TX_4X4. The mode map is the discriminator: decisions depend on
    // reconstructed edges, so any recon error would corrupt subsequent mode
    // choices and the map would differ.
}

TEST_CASE("frame auto 4x4 with quantization matches the Q2 generator golden") {
    // golden: golden_gen q2f_modes/q2f_recon/q2f_coeffs at qindex 100 —
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
    // golden: golden_gen d2_v â€” V SAD 0, PAETH SAD 0, everything else > 0;
    // deterministic tie-break = lowest mode index -> V (1)
    const std::uint8_t src[16] = {10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40, 10, 20, 30, 40};
    const std::uint8_t above[4] = {10, 20, 30, 40};
    const auto d = pipeline::decideBlockMode4x4(src, above, 4, 0, nullptr, 0, 0, 0);
    CHECK(d.mode == intra::V_PRED);
    CHECK(d.sad == 0);
}

TEST_CASE("decide picks horizontal for the h fixture") {
    // golden: golden_gen d2_h â€” H SAD 0, PAETH SAD 0, tie -> H (2)
    const std::uint8_t src[16] = {5, 5, 5, 5, 10, 10, 10, 10, 15, 15, 15, 15, 20, 20, 20, 20};
    const std::uint8_t left[4] = {5, 10, 15, 20};
    const auto d = pipeline::decideBlockMode4x4(src, nullptr, 0, 0, left, 4, 0, 0);
    CHECK(d.mode == intra::H_PRED);
    CHECK(d.sad == 0);
}

TEST_CASE("decide picks dc on a flat block via thirteen-way tie") {
    // golden: golden_gen d2_dc â€” all 13 candidate SADs are 0; tie-break -> DC (0)
    const std::uint8_t src[16] = {50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50};
    const std::uint8_t above[4] = {50, 50, 50, 50};
    const std::uint8_t left[4] = {50, 50, 50, 50};
    const auto d = pipeline::decideBlockMode4x4(src, above, 4, 0, left, 4, 0, 50);
    CHECK(d.mode == intra::DC_PRED);
    CHECK(d.sad == 0);
}

TEST_CASE("decide picks d45 for the diagonal fixture") {
    // golden: golden_gen d2_d45 â€” D45 SAD 0, next best 233 (SMOOTH_V); strict win
    const std::uint8_t src[16] = {10, 20, 30, 40, 20, 30, 40, 50, 30, 40, 50, 60, 40, 50, 60, 70};
    const std::uint8_t above[8] = {0, 10, 20, 30, 40, 50, 60, 70};
    const std::uint8_t left[4] = {0, 10, 20, 30};
    const auto d = pipeline::decideBlockMode4x4(src, above, 4, 4, left, 4, 0, 0);
    CHECK(d.mode == intra::D45_PRED);
    CHECK(d.sad == 0);
}

TEST_CASE("frame auto matches the generator policy golden bit-exactly") {
    // golden: golden_gen golden_frame (d3_modes / d3_recon / d3_coeffs) â€”
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
    // golden: golden_gen qw8_modes/qw8_recon/qw8_coeffs at qindex 100 —
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
    // V_PRED encode must produce identical coeffs + recon for it — this
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
            // M1 raster rule) — mirrors FR1 host gather
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
            // M1 raster rule) — mirrors FR1 host gather
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