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
    CUdeviceptr pAbove = dAbove.get();
    CUdeviceptr pNTop = dNTop.get();
    CUdeviceptr pNTr = dNTr.get();
    CUdeviceptr pLeft = dLeft.get();
    CUdeviceptr pNLeft = dNLeft.get();
    CUdeviceptr pNBl = dNBl.get();
    CUdeviceptr pAl = dAl.get();
    CUdeviceptr pPred = dPred.get();
    void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                        &pNBl,  &pAl,   &pFi, &pPred};
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