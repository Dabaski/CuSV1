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
    // -> fwd 2D -> inv 2D add onto the same predictor. For this block the
    // fixed-point round trip is exactly lossless: recon == source. The 1:1
    // claim is vs SVT's recon, not vs the original pixels.
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
    // Note: SVT's 4x4 fixed-point fwd+inv is exact for in-range 8-bit blocks,
    // so recon equals the source here; fixed-point round trips are NOT
    // guaranteed lossless in general (larger transforms / higher bit depth),
    // which is why the 1:1 claim is scoped to SVT's recon, not the source.
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
    // -> DCT fwd -> DCT inv-add onto the same predictor). The 4x4 fixed-point
    // round trip is exact for these 8-bit blocks, so recon == source; the
    // per-block coeffs discriminate the availability paths (corner block 0,0
    // fills above=127 -> DC -3784; first-row/left-column use left[0])
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
    std::uint8_t dummyAbove[4] = {0};
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
    gpurt::DeviceBuffer dAbove(4);
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
    dAbove.uploadFrom(dummyAbove, 4);

    std::uint8_t reconGot[64] = {0};
    std::int32_t coeffsGot[64] = {0};
    std::uint8_t aboveHost[4] = {0};
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
            const int nTopRightPx = 0;

            // read edges from device recon
            if (hasTop) {
                std::uint8_t tmp[64];
                dRecon.downloadTo(tmp, 64);
                for (int i = 0; i < 4; ++i) {
                    aboveHost[i] = tmp[(py - 1) * 8 + px + i];
                }
                dAbove.uploadFrom(aboveHost, 4);
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
    gpurt::DeviceBuffer dAbove(4);
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
    std::uint8_t aboveHost[4] = {0};
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
            const int nTopRightPx = 0;

            std::uint8_t tmp[64] = {0};
            dRecon.downloadTo(tmp, 64);
            if (hasTop) {
                for (int i = 0; i < 4; ++i) {
                    aboveHost[i] = tmp[(py - 1) * 8 + px + i];
                }
                dAbove.uploadFrom(aboveHost, 4);
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
