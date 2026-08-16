#include <doctest.h>
#include <cmath>
#include <gpurt.h>
#include <intra.h>

namespace {

bool runBlockPredict(gpurt::GpuContext& ctx, int mode, int angleDelta, const unsigned char* above, int nTopPx,
                     int nTopRightPx, const unsigned char* left, int nLeftPx, int nBottomLeftPx, int aboveLeft,
                     const unsigned char* expected, int aboveMode = 0, int leftMode = 0, int filterIntraMode = -1,
                     int disableEdgeFilter = 0) {
    (void)ctx;
    const std::string ptx = *gpurt::compileToPtx(intra::predictBlockCuSource(), "compute_61");
    const std::vector<std::string> names = gpurt::ptxEntryNames(ptx);
    const auto it = std::find(names.begin(), names.end(), "predict_block_4x4");
    if (it == names.end()) {
        return false;
    }
    gpurt::Kernel k(ptx, *it);

    gpurt::DeviceBuffer dAbove(nTopPx > 0 ? sizeof(unsigned char) * (nTopPx + nTopRightPx) : 1);
    gpurt::DeviceBuffer dLeft(nLeftPx > 0 ? sizeof(unsigned char) * (nLeftPx + nBottomLeftPx) : 1);
    gpurt::DeviceBuffer dOut(16 * sizeof(unsigned char));
    if (nTopPx > 0) {
        dAbove.uploadFrom(above, sizeof(unsigned char) * (nTopPx + nTopRightPx));
    }
    if (nLeftPx > 0) {
        dLeft.uploadFrom(left, sizeof(unsigned char) * (nLeftPx + nBottomLeftPx));
    }

    int modeArg = mode;
    int deltaArg = angleDelta;
    int amArg = aboveMode;
    int lmArg = leftMode;
    int fiArg = filterIntraMode;
    int defArg = disableEdgeFilter;
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
    CUdeviceptr pOut = dOut.get();
    void* args[] = {&pMode, &pDelta, &pAm,   &pLm,    &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                    &pNBl,  &pAl,   &pFi,   &pDef,   &pOut};
    k.launch(1, 1, 16, 1, args);

    unsigned char got[16] = {0};
    dOut.downloadTo(got, sizeof(got));

    for (int i = 0; i < 16; ++i) {
        if (got[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("edge filter strength is 1 for 4x4 with delta 56") {
    CHECK(intra::edgeFilterStrength(4, 4, 56, 0) == 1);
}

TEST_CASE("filt type is 1 when the above neighbor is smooth") {
    // golden: svt_aom_is_smooth (intra_prediction.c:128) + get_filt_type
    // (enc_intra_prediction.c:20): luma plane -> mode is SMOOTH_PRED/V/H
    intra::NeighborContext ctx;
    ctx.aboveMode = intra::SMOOTH_PRED;
    CHECK(intra::filtType(ctx) == 1);
}

TEST_CASE("filt type is 1 when the left neighbor is smooth v") {
    intra::NeighborContext ctx;
    ctx.leftMode = intra::SMOOTH_V_PRED;
    CHECK(intra::filtType(ctx) == 1);
}

TEST_CASE("filt type is 0 for non-smooth neighbors") {
    intra::NeighborContext ctx;
    ctx.aboveMode = intra::PAETH_PRED;
    ctx.leftMode = intra::DC_PRED;
    CHECK(intra::filtType(ctx) == 0);
}

TEST_CASE("builder applies the smooth-neighbor edge filter for d135") {
    // hand-traced: filt_type=1 -> edgeFilterStrength(4,4,45,1)=1 -> 5-tap
    // {0,4,8,4,0} over above+corner {90,100,101,102,103} -> {90,98,101,102,103}
    // -> drZ2 (D135, up=0): r2c3 = above[0] = 98
    const unsigned char above[4] = {100, 101, 102, 103};
    const unsigned char left[4] = {10, 11, 12, 13};
    unsigned char dst[16] = {0};
    intra::NeighborContext ctx;
    ctx.aboveMode = intra::SMOOTH_H_PRED;
    intra::buildIntraPredictors(dst, 4, intra::D135_PRED, 0, 4, 4, 90, above, 4, 0, left, 4, 0, ctx);
    CHECK(dst[2 * 4 + 3] == 98);
}

TEST_CASE("builder skips the edge filter for non-smooth neighbors") {
    const unsigned char above[4] = {100, 101, 102, 103};
    const unsigned char left[4] = {10, 11, 12, 13};
    unsigned char dst[16] = {0};
    intra::NeighborContext ctx;
    ctx.aboveMode = intra::DC_PRED;
    intra::buildIntraPredictors(dst, 4, intra::D135_PRED, 0, 4, 4, 90, above, 4, 0, left, 4, 0, ctx);
    CHECK(dst[2 * 4 + 3] == 100);
}

TEST_CASE("gpu block predictor applies the smooth-neighbor filter like the builder") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[4] = {100, 101, 102, 103};
    const unsigned char left[4] = {10, 11, 12, 13};
    unsigned char ref[16] = {0};
    intra::NeighborContext nctx;
    nctx.aboveMode = intra::SMOOTH_H_PRED;
    intra::buildIntraPredictors(ref, 4, intra::D135_PRED, 0, 4, 4, 90, above, 4, 0, left, 4, 0, nctx);

    bool ok = runBlockPredict(ctx, intra::D135_PRED, 0, above, 4, 0, left, 4, 0, 90, ref,
                              intra::SMOOTH_H_PRED, intra::DC_PRED);
    CHECK(ok);
}

TEST_CASE("filter intra predictor mode dc matches svt golden") {
    // golden: svt_av1_filter_intra_predictor_c (filterintra_c.c:70),
    // FILTER_DC_PRED, corner 10, above {20,30,40,50}, left {21,31,41,51}
    unsigned char ab[6] = {10, 20, 30, 40, 50};
    const unsigned char left[4] = {21, 31, 41, 51};
    unsigned char dst[16] = {0};
    intra::filterIntraPredictor(dst, 4, ab + 1, left, 0);
    CHECK(dst[0] == 25);
}

TEST_CASE("filter intra predictor mode 1 matches svt golden") {
    // golden: svt_av1_filter_intra_predictor_c, FILTER_V_PRED, corner 10,
    // above {20,30,40,50}, left {21,31,41,51}
    unsigned char ab[6] = {10, 20, 30, 40, 50};
    const unsigned char left[4] = {21, 31, 41, 51};
    unsigned char dst[16] = {0};
    intra::filterIntraPredictor(dst, 4, ab + 1, left, 1);
    CHECK(dst[0] == 27);
}

TEST_CASE("filter intra predictor mode 2 matches svt golden") {
    // golden: FILTER_H_PRED, same fixture
    unsigned char ab[6] = {10, 20, 30, 40, 50};
    const unsigned char left[4] = {21, 31, 41, 51};
    unsigned char dst[16] = {0};
    intra::filterIntraPredictor(dst, 4, ab + 1, left, 2);
    CHECK(dst[0] == 26);
}

TEST_CASE("filter intra predictor mode 3 matches svt golden") {
    // golden: FILTER_D157_PRED, same fixture
    unsigned char ab[6] = {10, 20, 30, 40, 50};
    const unsigned char left[4] = {21, 31, 41, 51};
    unsigned char dst[16] = {0};
    intra::filterIntraPredictor(dst, 4, ab + 1, left, 3);
    CHECK(dst[0] == 22);
}

TEST_CASE("filter intra predictor mode 4 matches svt golden") {
    // golden: FILTER_PAETH_PRED, same fixture
    unsigned char ab[6] = {10, 20, 30, 40, 50};
    const unsigned char left[4] = {21, 31, 41, 51};
    unsigned char dst[16] = {0};
    intra::filterIntraPredictor(dst, 4, ab + 1, left, 4);
    CHECK(dst[0] == 28);
}

TEST_CASE("builder routes filter intra to the predictor early-out") {
    // golden: build_intra_predictors with use_filter_intra (mode 0) produces
    // the same 16 pixels as svt_av1_filter_intra_predictor_c on the assembled
    // edges (corner 10, above {20,30,40,50}, left {21,31,41,51})
    const unsigned char above[4] = {20, 30, 40, 50};
    const unsigned char left[4] = {21, 31, 41, 51};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::DC_PRED, 0, 4, 4, 10, above, 4, 0, left, 4, 0,
                                intra::NeighborContext(), 0);
    CHECK(dst[3] == 44);
}

TEST_CASE("gpu block predictor filter intra matches the builder") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[4] = {20, 30, 40, 50};
    const unsigned char left[4] = {21, 31, 41, 51};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::DC_PRED, 0, 4, 4, 10, above, 4, 0, left, 4, 0,
                                intra::NeighborContext(), 1);

    bool ok = runBlockPredict(ctx, intra::DC_PRED, 0, above, 4, 0, left, 4, 0, 10, ref, 0, 0, 1);
    CHECK(ok);
}

TEST_CASE("builder skips edge filtering with disable_edge_filter") {
    // enc_intra_prediction.c:183 — !disable_edge_filter wraps the whole
    // filter+upsample block; disabled: raw z1 on the unfiltered above
    // {10,20,30,40,...} -> r0c1 = (20*19 + 30*13 + 16) >> 5 = 24
    // (enabled path upsamples first: r0c1 = 17)
    const unsigned char above[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    const unsigned char left[4] = {9, 9, 9, 9};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::D67_PRED, 0, 4, 4, 7, above, 4, 4, left, 4, 0,
                                intra::NeighborContext(), -1, true);
    CHECK(dst[1] == 24);
}

TEST_CASE("intra edge upsample enabled for 4x4 with delta 23") {
    CHECK(intra::useIntraEdgeUpsample(4, 4, 23, 0) == 1);
}

TEST_CASE("intra edge upsample honors filt type at 8x8 geometry") {
    // svt_aom_use_intra_edge_upsample: type ? (blk_wh <= 8) : (blk_wh <= 16).
    // At 8x8 (blk_wh = 16) the two branches disagree, so this unit test is the
    // proof the filt_type wire reaches the upsample decision; the 4x4 builder
    // path cannot observe it (both branches true at blk_wh = 8).
    CHECK(intra::useIntraEdgeUpsample(8, 8, 23, 1) == 0);
}

TEST_CASE("intra edge upsample stays enabled for luma at 8x8 geometry") {
    CHECK(intra::useIntraEdgeUpsample(8, 8, 23, 0) == 1);
}

TEST_CASE("edge filter strength 1 filters the last edge sample") {
    unsigned char p[5] = {10, 20, 30, 40, 50};
    intra::filterIntraEdge(p, 5, 1);
    CHECK(p[4] == 48);
}

TEST_CASE("intra edge upsample interpolates the first half sample") {
    unsigned char buf[16] = {7, 10, 20, 30, 40, 50, 60, 70, 80};
    unsigned char* p = buf + 1;
    intra::upsampleIntraEdge(p, 8);
    CHECK(p[1] == 15);
}

TEST_CASE("dr z1 predicts the first pixel from the above row") {
    const unsigned char above[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    unsigned char dst[16] = {0};
    intra::drZ1(dst, 4, 4, 4, above, nullptr, 0, 27, 1);
    CHECK(dst[0] == 14);
}

TEST_CASE("dr z2 steals the first left sample for d135") {
    unsigned char a[9] = {90, 100, 101, 102, 103, 104, 105, 106, 107};
    unsigned char l[9] = {90, 10, 11, 12, 13, 14, 15, 16, 17};
    unsigned char dst[16] = {0};
    intra::drZ2(dst, 4, 4, 4, a + 1, l + 1, 0, 0, 64, 64);
    CHECK(dst[1 * 4 + 0] == 10);
}

TEST_CASE("dr z3 predicts the first pixel from the left column") {
    const unsigned char left[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    unsigned char dst[16] = {0};
    intra::drZ3(dst, 4, 4, 4, nullptr, left, 0, 1, 27);
    CHECK(dst[0] == 14);
}

TEST_CASE("angle 45 maps to step 64 in x") {
    CHECK(intra::getDx(45) == 64);
}

TEST_CASE("angle 157 maps to step 27 in y") {
    CHECK(intra::getDy(157) == 27);
}

TEST_CASE("dr predictor dispatches angle 45 to zone 1") {
    const unsigned char above[8] = {3, 1, 4, 1, 5, 9, 2, 6};
    unsigned char dst[16] = {0};
    intra::drPredictor(dst, 4, 4, 4, above, nullptr, 0, 0, 45);
    CHECK(dst[1 * 4 + 2] == 5);
}

TEST_CASE("smooth prediction weights all four edges") {
    const unsigned char above[4] = {10, 20, 30, 40};
    const unsigned char left[4] = {5, 15, 25, 35};
    unsigned char dst[16] = {0};
    intra::smoothPredict(dst, 4, 4, 4, above, left);
    CHECK(dst[0] == 8);
}

TEST_CASE("smooth v weights the vertical trend") {
    const unsigned char above[4] = {10, 20, 30, 40};
    const unsigned char left[4] = {5, 15, 25, 35};
    unsigned char dst[16] = {0};
    intra::smoothVPredict(dst, 4, 4, 4, above, left);
    CHECK(dst[0] == 10);
}

TEST_CASE("smooth h weights the horizontal trend") {
    const unsigned char above[4] = {10, 20, 30, 40};
    const unsigned char left[4] = {5, 15, 25, 35};
    unsigned char dst[16] = {0};
    intra::smoothHPredict(dst, 4, 4, 4, above, left);
    CHECK(dst[0] == 5);
}

TEST_CASE("dr predictor treats angle 90 as vertical") {
    const unsigned char above[4] = {3, 7, 11, 15};
    unsigned char dst[16] = {0};
    intra::drPredictor(dst, 4, 4, 4, above, nullptr, 0, 0, 90);
    CHECK(dst[2 * 4 + 1] == 7);
}

TEST_CASE("dr predictor treats angle 180 as horizontal") {
    const unsigned char left[4] = {5, 6, 7, 8};
    unsigned char dst[16] = {0};
    intra::drPredictor(dst, 4, 4, 4, nullptr, left, 0, 0, 180);
    CHECK(dst[2 * 4 + 3] == 7);
}

TEST_CASE("builder predicts dc from both edges") {
    const unsigned char above[4] = {10, 10, 10, 10};
    const unsigned char left[4] = {20, 20, 20, 20};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::DC_PRED, 0, 4, 4, 0, above, 4, 0, left, 4, 0);
    CHECK(dst[0] == 15);
}

TEST_CASE("builder predicts vertical from the above row") {
    const unsigned char above[4] = {3, 7, 11, 15};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::V_PRED, 0, 4, 4, 0, above, 4, 0, nullptr, 0, 0);
    CHECK(dst[2 * 4 + 1] == 7);
}

TEST_CASE("builder predicts horizontal from the left column") {
    const unsigned char left[4] = {5, 6, 7, 8};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::H_PRED, 0, 4, 4, 0, nullptr, 0, 0, left, 4, 0);
    CHECK(dst[2 * 4 + 3] == 7);
}

TEST_CASE("builder predicts paeth from the closest gradient neighbor") {
    const unsigned char above[4] = {10, 40, 30, 20};
    const unsigned char left[4] = {50, 60, 70, 80};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::PAETH_PRED, 0, 4, 4, 45, above, 4, 0, left, 4, 0);
    CHECK(dst[2 * 4 + 1] == 70);
}

TEST_CASE("builder predicts smooth from all four edges") {
    const unsigned char above[4] = {10, 20, 30, 40};
    const unsigned char left[4] = {5, 15, 25, 35};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::SMOOTH_PRED, 0, 4, 4, 0, above, 4, 0, left, 4, 0);
    CHECK(dst[0] == 8);
}

TEST_CASE("builder predicts d45 through the dr pipeline") {
    const unsigned char above[8] = {3, 1, 4, 1, 5, 9, 2, 6};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::D45_PRED, 0, 4, 4, 0, above, 4, 4, nullptr, 0, 0);
    CHECK(dst[1 * 4 + 2] == 5);
}

TEST_CASE("builder suspects upsample for d67 before predicting") {
    const unsigned char above[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    const unsigned char left[4] = {9, 9, 9, 9};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::D67_PRED, 0, 4, 4, 7, above, 4, 4, left, 4, 0);
    CHECK(dst[0] == 14);
}

TEST_CASE("builder predicts d135 through zone 2") {
    const unsigned char above[8] = {100, 101, 102, 103, 104, 105, 106, 107};
    const unsigned char left[8] = {10, 11, 12, 13, 14, 15, 16, 17};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::D135_PRED, 0, 4, 4, 90, above, 4, 4, left, 4, 4);
    CHECK(dst[1 * 4 + 0] == 10);
}

TEST_CASE("builder suspects upsample for d203 too") {
    const unsigned char above[4] = {9, 9, 9, 9};
    const unsigned char left[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::D203_PRED, 0, 4, 4, 7, above, 4, 0, left, 4, 4);
    CHECK(dst[0] == 14);
}

TEST_CASE("builder upsamples above then predicts d113 through zone 2") {
    const unsigned char above[4] = {10, 20, 30, 40};
    const unsigned char left[4] = {5, 15, 25, 35};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::D113_PRED, 0, 4, 4, 7, above, 4, 0, left, 4, 0);
    CHECK(dst[3] == 37);
}

TEST_CASE("builder upsamples left then predicts d157 through zone 2") {
    const unsigned char above[4] = {10, 20, 30, 40};
    const unsigned char left[4] = {5, 15, 25, 35};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::D157_PRED, 0, 4, 4, 7, above, 4, 0, left, 4, 0);
    CHECK(dst[1] == 6);
}

TEST_CASE("gpu dr z1 matches host reference") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    const int dx = 27;
    const int upsampleAbove = 0;
    unsigned char ref[16] = {0};
    intra::drZ1(ref, 4, 4, 4, above, nullptr, upsampleAbove, dx, 1);

    const std::string ptx = *gpurt::compileToPtx(intra::drZ1CuSource(), "compute_61");
    const std::string entry = gpurt::ptxEntryNames(ptx).at(0);
    gpurt::Kernel k(ptx, entry);

    gpurt::DeviceBuffer dAbove(sizeof(above));
    gpurt::DeviceBuffer dOut(sizeof(ref));
    dAbove.uploadFrom(above, sizeof(above));

    int dxArg = dx;
    int upArg = upsampleAbove;
    gpurt::DeviceBuffer dDx(sizeof(dxArg));
    gpurt::DeviceBuffer dUp(sizeof(upArg));
    dDx.uploadFrom(&dxArg, sizeof(dxArg));
    dUp.uploadFrom(&upArg, sizeof(upArg));

    CUdeviceptr pAbove = dAbove.get();
    CUdeviceptr pDx = dDx.get();
    CUdeviceptr pUp = dUp.get();
    CUdeviceptr pOut = dOut.get();
    void* args[] = {&pAbove, &pDx, &pUp, &pOut};
    k.launch(1, 1, 16, 1, args);

    unsigned char got[16] = {0};
    dOut.downloadTo(got, sizeof(got));

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != ref[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu dr z1 matches host reference with upsampled edge") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    unsigned char raw[18] = {0};
    unsigned char* p = raw + 1;
    p[-1] = 7;
    for (int i = 0; i < 8; ++i) {
        p[i] = (unsigned char)(10 * (i + 1));
    }
    intra::upsampleIntraEdge(p, 8);
    const unsigned char* above = raw;

    const int dx = 27;
    const int upsampleAbove = 1;
    unsigned char ref[16] = {0};
    intra::drZ1(ref, 4, 4, 4, above, nullptr, upsampleAbove, dx, 1);

    const std::string ptx = *gpurt::compileToPtx(intra::drZ1CuSource(), "compute_61");
    const std::string entry = gpurt::ptxEntryNames(ptx).at(0);
    gpurt::Kernel k(ptx, entry);

    gpurt::DeviceBuffer dAbove(sizeof(raw));
    gpurt::DeviceBuffer dOut(sizeof(ref));
    dAbove.uploadFrom(raw, sizeof(raw));

    int dxArg = dx;
    int upArg = upsampleAbove;
    gpurt::DeviceBuffer dDx(sizeof(dxArg));
    gpurt::DeviceBuffer dUp(sizeof(upArg));
    dDx.uploadFrom(&dxArg, sizeof(dxArg));
    dUp.uploadFrom(&upArg, sizeof(upArg));

    CUdeviceptr pAbove = dAbove.get();
    CUdeviceptr pDx = dDx.get();
    CUdeviceptr pUp = dUp.get();
    CUdeviceptr pOut = dOut.get();
    void* args[] = {&pAbove, &pDx, &pUp, &pOut};
    k.launch(1, 1, 16, 1, args);

    unsigned char got[16] = {0};
    dOut.downloadTo(got, sizeof(got));

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != ref[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu dr z3 matches host reference") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char left[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    const int dy = 27;
    const int upsampleLeft = 0;
    unsigned char ref[16] = {0};
    intra::drZ3(ref, 4, 4, 4, nullptr, left, upsampleLeft, 1, dy);

    const std::string ptx = *gpurt::compileToPtx(intra::drZ3CuSource(), "compute_61");
    const std::string entry = gpurt::ptxEntryNames(ptx).at(0);
    gpurt::Kernel k(ptx, entry);

    gpurt::DeviceBuffer dLeft(sizeof(left));
    gpurt::DeviceBuffer dOut(sizeof(ref));
    dLeft.uploadFrom(left, sizeof(left));

    int dyArg = dy;
    int upArg = upsampleLeft;
    gpurt::DeviceBuffer dDy(sizeof(dyArg));
    gpurt::DeviceBuffer dUp(sizeof(upArg));
    dDy.uploadFrom(&dyArg, sizeof(dyArg));
    dUp.uploadFrom(&upArg, sizeof(upArg));

    CUdeviceptr pLeft = dLeft.get();
    CUdeviceptr pDy = dDy.get();
    CUdeviceptr pUp = dUp.get();
    CUdeviceptr pOut = dOut.get();
    void* args[] = {&pLeft, &pDy, &pUp, &pOut};
    k.launch(1, 1, 16, 1, args);

    unsigned char got[16] = {0};
    dOut.downloadTo(got, sizeof(got));

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != ref[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu dr z2 matches host reference") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    unsigned char a[9] = {90, 100, 101, 102, 103, 104, 105, 106, 107};
    unsigned char l[9] = {90, 10, 11, 12, 13, 14, 15, 16, 17};
    unsigned char ref[16] = {0};
    intra::drZ2(ref, 4, 4, 4, a + 1, l + 1, 0, 0, 64, 64);

    const int dx = 64;
    const int dy = 64;
    const int upsampleAbove = 0;
    const int upsampleLeft = 0;
    const std::string ptx = *gpurt::compileToPtx(intra::drZ2CuSource(), "compute_61");
    const std::string entry = gpurt::ptxEntryNames(ptx).at(0);
    gpurt::Kernel k(ptx, entry);

    gpurt::DeviceBuffer dAbove(sizeof(a));
    gpurt::DeviceBuffer dLeft(sizeof(l));
    gpurt::DeviceBuffer dOut(sizeof(ref));
    dAbove.uploadFrom(a, sizeof(a));
    dLeft.uploadFrom(l, sizeof(l));

    int dxArg = dx;
    int dyArg = dy;
    int upAArg = upsampleAbove;
    int upLArg = upsampleLeft;
    gpurt::DeviceBuffer dDx(sizeof(dxArg));
    gpurt::DeviceBuffer dDy(sizeof(dyArg));
    gpurt::DeviceBuffer dUpA(sizeof(upAArg));
    gpurt::DeviceBuffer dUpL(sizeof(upLArg));
    dDx.uploadFrom(&dxArg, sizeof(dxArg));
    dDy.uploadFrom(&dyArg, sizeof(dyArg));
    dUpA.uploadFrom(&upAArg, sizeof(upAArg));
    dUpL.uploadFrom(&upLArg, sizeof(upLArg));

    CUdeviceptr pAbove = dAbove.get() + 1;
    CUdeviceptr pLeft = dLeft.get() + 1;
    CUdeviceptr pDx = dDx.get();
    CUdeviceptr pDy = dDy.get();
    CUdeviceptr pUpA = dUpA.get();
    CUdeviceptr pUpL = dUpL.get();
    CUdeviceptr pOut = dOut.get();
    void* args[] = {&pAbove, &pLeft, &pDx, &pDy, &pUpA, &pUpL, &pOut};
    k.launch(1, 1, 16, 1, args);

    unsigned char got[16] = {0};
    dOut.downloadTo(got, sizeof(got));

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != ref[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu dr z2 matches host reference with upsampled above") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    unsigned char aboveRaw[16];
    unsigned char leftRaw[16];
    for (int i = 0; i < 16; ++i) {
        aboveRaw[i] = 200;
        leftRaw[i] = 200;
    }
    unsigned char* ap = aboveRaw + 8;
    unsigned char* lp = leftRaw + 8;
    ap[-1] = 7;
    ap[0] = 10;
    ap[1] = 20;
    ap[2] = 30;
    ap[3] = 40;
    lp[-1] = 7;
    lp[0] = 5;
    lp[1] = 15;
    lp[2] = 25;
    lp[3] = 35;
    intra::upsampleIntraEdge(ap, 4);

    unsigned char ref[16] = {0};
    intra::drZ2(ref, 4, 4, 4, ap, lp, 1, 0, 27, 151);

    const int dx = 27;
    const int dy = 151;
    const int upsampleAbove = 1;
    const int upsampleLeft = 0;
    const std::string ptx = *gpurt::compileToPtx(intra::drZ2CuSource(), "compute_61");
    const std::string entry = gpurt::ptxEntryNames(ptx).at(0);
    gpurt::Kernel k(ptx, entry);

    gpurt::DeviceBuffer dAbove(sizeof(aboveRaw));
    gpurt::DeviceBuffer dLeft(sizeof(leftRaw));
    gpurt::DeviceBuffer dOut(sizeof(ref));
    dAbove.uploadFrom(aboveRaw, sizeof(aboveRaw));
    dLeft.uploadFrom(leftRaw, sizeof(leftRaw));

    int dxArg = dx;
    int dyArg = dy;
    int upAArg = upsampleAbove;
    int upLArg = upsampleLeft;
    gpurt::DeviceBuffer dDx(sizeof(dxArg));
    gpurt::DeviceBuffer dDy(sizeof(dyArg));
    gpurt::DeviceBuffer dUpA(sizeof(upAArg));
    gpurt::DeviceBuffer dUpL(sizeof(upLArg));
    dDx.uploadFrom(&dxArg, sizeof(dxArg));
    dDy.uploadFrom(&dyArg, sizeof(dyArg));
    dUpA.uploadFrom(&upAArg, sizeof(upAArg));
    dUpL.uploadFrom(&upLArg, sizeof(upLArg));

    CUdeviceptr pAbove = dAbove.get() + 8;
    CUdeviceptr pLeft = dLeft.get() + 8;
    CUdeviceptr pDx = dDx.get();
    CUdeviceptr pDy = dDy.get();
    CUdeviceptr pUpA = dUpA.get();
    CUdeviceptr pUpL = dUpL.get();
    CUdeviceptr pOut = dOut.get();
    void* args[] = {&pAbove, &pLeft, &pDx, &pDy, &pUpA, &pUpL, &pOut};
    k.launch(1, 1, 16, 1, args);

    unsigned char got[16] = {0};
    dOut.downloadTo(got, sizeof(got));

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != ref[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu dr dispatch matches host predictor for angle 45") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    unsigned char above[16] = {0};
    for (int i = 0; i < 16; ++i) {
        above[i] = 200;
    }
    unsigned char* ap = above + 4;
    ap[0] = 3;
    ap[1] = 1;
    ap[2] = 4;
    ap[3] = 1;
    ap[4] = 5;
    ap[5] = 9;
    ap[6] = 2;
    ap[7] = 6;
    unsigned char left[16] = {0};
    for (int i = 0; i < 16; ++i) {
        left[i] = 200;
    }
    unsigned char* lp = left + 4;
    lp[-1] = 0;
    lp[0] = 5;

    unsigned char ref[16] = {0};
    const int angle = 45;
    intra::drPredictor(ref, 4, 4, 4, ap, lp, 0, 0, angle);

    const int dx = 64;
    const int dy = 1;
    const int upsampleAbove = 0;
    const int upsampleLeft = 0;
    const std::string ptx = *gpurt::compileToPtx(intra::drPredictCuSource(), "compute_61");
    const std::string entry = gpurt::ptxEntryNames(ptx).at(0);
    gpurt::Kernel k(ptx, entry);

    gpurt::DeviceBuffer dAbove(sizeof(above));
    gpurt::DeviceBuffer dLeft(sizeof(left));
    gpurt::DeviceBuffer dOut(sizeof(ref));
    dAbove.uploadFrom(above, sizeof(above));
    dLeft.uploadFrom(left, sizeof(left));

    int dxArg = dx;
    int dyArg = dy;
    int upAArg = upsampleAbove;
    int upLArg = upsampleLeft;
    int angleArg = angle;
    gpurt::DeviceBuffer dDx(sizeof(dxArg));
    gpurt::DeviceBuffer dDy(sizeof(dyArg));
    gpurt::DeviceBuffer dUpA(sizeof(upAArg));
    gpurt::DeviceBuffer dUpL(sizeof(upLArg));
    gpurt::DeviceBuffer dAngle(sizeof(angleArg));
    dDx.uploadFrom(&dxArg, sizeof(dxArg));
    dDy.uploadFrom(&dyArg, sizeof(dyArg));
    dUpA.uploadFrom(&upAArg, sizeof(upAArg));
    dUpL.uploadFrom(&upLArg, sizeof(upLArg));
    dAngle.uploadFrom(&angleArg, sizeof(angleArg));

    CUdeviceptr pAbove = dAbove.get() + 4;
    CUdeviceptr pLeft = dLeft.get() + 4;
    CUdeviceptr pDx = dDx.get();
    CUdeviceptr pDy = dDy.get();
    CUdeviceptr pUpA = dUpA.get();
    CUdeviceptr pUpL = dUpL.get();
    CUdeviceptr pAngle = dAngle.get();
    CUdeviceptr pOut = dOut.get();
    void* args[] = {&pAbove, &pLeft, &pDx, &pDy, &pUpA, &pUpL, &pAngle, &pOut};
    k.launch(1, 1, 16, 1, args);

    unsigned char got[16] = {0};
    dOut.downloadTo(got, sizeof(got));

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != ref[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu dr dispatch matches host predictor for angle 135") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    unsigned char above[16];
    unsigned char left[16];
    for (int i = 0; i < 16; ++i) {
        above[i] = 200;
        left[i] = 200;
    }
    unsigned char* ap = above + 4;
    unsigned char* lp = left + 4;
    ap[-1] = 90;
    for (int i = 0; i < 8; ++i) {
        ap[i] = (unsigned char)(100 + i);
    }
    lp[-1] = 90;
    for (int i = 0; i < 8; ++i) {
        lp[i] = (unsigned char)(10 + i);
    }

    unsigned char ref[16] = {0};
    const int angle = 135;
    intra::drPredictor(ref, 4, 4, 4, ap, lp, 0, 0, angle);

    const int dx = 64;
    const int dy = 64;
    const int upsampleAbove = 0;
    const int upsampleLeft = 0;
    const std::string ptx = *gpurt::compileToPtx(intra::drPredictCuSource(), "compute_61");
    const std::string entry = gpurt::ptxEntryNames(ptx).at(0);
    gpurt::Kernel k(ptx, entry);

    gpurt::DeviceBuffer dAbove(sizeof(above));
    gpurt::DeviceBuffer dLeft(sizeof(left));
    gpurt::DeviceBuffer dOut(sizeof(ref));
    dAbove.uploadFrom(above, sizeof(above));
    dLeft.uploadFrom(left, sizeof(left));

    int dxArg = dx;
    int dyArg = dy;
    int upAArg = upsampleAbove;
    int upLArg = upsampleLeft;
    int angleArg = angle;
    gpurt::DeviceBuffer dDx(sizeof(dxArg));
    gpurt::DeviceBuffer dDy(sizeof(dyArg));
    gpurt::DeviceBuffer dUpA(sizeof(upAArg));
    gpurt::DeviceBuffer dUpL(sizeof(upLArg));
    gpurt::DeviceBuffer dAngle(sizeof(angleArg));
    dDx.uploadFrom(&dxArg, sizeof(dxArg));
    dDy.uploadFrom(&dyArg, sizeof(dyArg));
    dUpA.uploadFrom(&upAArg, sizeof(upAArg));
    dUpL.uploadFrom(&upLArg, sizeof(upLArg));
    dAngle.uploadFrom(&angleArg, sizeof(angleArg));

    CUdeviceptr pAbove = dAbove.get() + 4;
    CUdeviceptr pLeft = dLeft.get() + 4;
    CUdeviceptr pDx = dDx.get();
    CUdeviceptr pDy = dDy.get();
    CUdeviceptr pUpA = dUpA.get();
    CUdeviceptr pUpL = dUpL.get();
    CUdeviceptr pAngle = dAngle.get();
    CUdeviceptr pOut = dOut.get();
    void* args[] = {&pAbove, &pLeft, &pDx, &pDy, &pUpA, &pUpL, &pAngle, &pOut};
    k.launch(1, 1, 16, 1, args);

    unsigned char got[16] = {0};
    dOut.downloadTo(got, sizeof(got));

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != ref[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu smooth matches reference weights") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[4] = {10, 20, 30, 40};
    const unsigned char left[4] = {5, 15, 25, 35};
    unsigned char ref[16] = {0};
    intra::smoothPredict(ref, 4, 4, 4, above, left);

    const std::string ptx = *gpurt::compileToPtx(intra::smoothPredictCuSourceRef(), "compute_61");
    const std::string entry = gpurt::ptxEntryNames(ptx).at(0);
    gpurt::Kernel k(ptx, entry);

    gpurt::DeviceBuffer dAbove(sizeof(above));
    gpurt::DeviceBuffer dLeft(sizeof(left));
    gpurt::DeviceBuffer dOut(sizeof(ref));
    dAbove.uploadFrom(above, sizeof(above));
    dLeft.uploadFrom(left, sizeof(left));

    CUdeviceptr pAbove = dAbove.get();
    CUdeviceptr pLeft = dLeft.get();
    CUdeviceptr pOut = dOut.get();
    void* args[] = {&pAbove, &pLeft, &pOut};
    k.launch(1, 1, 16, 1, args);

    unsigned char got[16] = {0};
    dOut.downloadTo(got, sizeof(got));

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != ref[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu smooth v matches reference weights") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[4] = {10, 20, 30, 40};
    const unsigned char left[4] = {5, 15, 25, 35};
    unsigned char ref[16] = {0};
    intra::smoothVPredict(ref, 4, 4, 4, above, left);

    const std::string ptx = *gpurt::compileToPtx(intra::smoothPredictCuSourceRef(), "compute_61");
    const std::vector<std::string> names = gpurt::ptxEntryNames(ptx);
    const auto it = std::find(names.begin(), names.end(), "smooth_v_4x4");
    REQUIRE(it != names.end());
    gpurt::Kernel k(ptx, *it);

    gpurt::DeviceBuffer dAbove(sizeof(above));
    gpurt::DeviceBuffer dLeft(sizeof(left));
    gpurt::DeviceBuffer dOut(sizeof(ref));
    dAbove.uploadFrom(above, sizeof(above));
    dLeft.uploadFrom(left, sizeof(left));

    CUdeviceptr pAbove = dAbove.get();
    CUdeviceptr pLeft = dLeft.get();
    CUdeviceptr pOut = dOut.get();
    void* args[] = {&pAbove, &pLeft, &pOut};
    k.launch(1, 1, 16, 1, args);

    unsigned char got[16] = {0};
    dOut.downloadTo(got, sizeof(got));

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != ref[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu smooth h matches reference weights") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[4] = {10, 20, 30, 40};
    const unsigned char left[4] = {5, 15, 25, 35};
    unsigned char ref[16] = {0};
    intra::smoothHPredict(ref, 4, 4, 4, above, left);

    const std::string ptx = *gpurt::compileToPtx(intra::smoothPredictCuSourceRef(), "compute_61");
    const std::vector<std::string> names = gpurt::ptxEntryNames(ptx);
    const auto it = std::find(names.begin(), names.end(), "smooth_h_4x4");
    REQUIRE(it != names.end());
    gpurt::Kernel k(ptx, *it);

    gpurt::DeviceBuffer dAbove(sizeof(above));
    gpurt::DeviceBuffer dLeft(sizeof(left));
    gpurt::DeviceBuffer dOut(sizeof(ref));
    dAbove.uploadFrom(above, sizeof(above));
    dLeft.uploadFrom(left, sizeof(left));

    CUdeviceptr pAbove = dAbove.get();
    CUdeviceptr pLeft = dLeft.get();
    CUdeviceptr pOut = dOut.get();
    void* args[] = {&pAbove, &pLeft, &pOut};
    k.launch(1, 1, 16, 1, args);

    unsigned char got[16] = {0};
    dOut.downloadTo(got, sizeof(got));

    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (got[i] != ref[i]) {
            ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("gpu block predictor dc matches builder") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[4] = {10, 10, 10, 10};
    const unsigned char left[4] = {20, 20, 20, 20};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::DC_PRED, 0, 4, 4, 0, above, 4, 0, left, 4, 0);

    bool ok = runBlockPredict(ctx, intra::DC_PRED, 0, above, 4, 0, left, 4, 0, 0, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor vertical matches builder") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[4] = {3, 7, 11, 15};
    const unsigned char left[4] = {0, 0, 0, 0};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::V_PRED, 0, 4, 4, 0, above, 4, 0, left, 4, 0);

    bool ok = runBlockPredict(ctx, intra::V_PRED, 0, above, 4, 0, left, 4, 0, 0, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor horizontal matches builder") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[4] = {0, 0, 0, 0};
    const unsigned char left[4] = {5, 6, 7, 8};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::H_PRED, 0, 4, 4, 0, above, 4, 0, left, 4, 0);

    bool ok = runBlockPredict(ctx, intra::H_PRED, 0, above, 4, 0, left, 4, 0, 0, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor paeth matches builder") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[4] = {10, 40, 30, 20};
    const unsigned char left[4] = {50, 60, 70, 80};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::PAETH_PRED, 0, 4, 4, 45, above, 4, 0, left, 4, 0);

    bool ok = runBlockPredict(ctx, intra::PAETH_PRED, 0, above, 4, 0, left, 4, 0, 45, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor smooth matches builder") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[4] = {10, 20, 30, 40};
    const unsigned char left[4] = {5, 15, 25, 35};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::SMOOTH_PRED, 0, 4, 4, 0, above, 4, 0, left, 4, 0);

    bool ok = runBlockPredict(ctx, intra::SMOOTH_PRED, 0, above, 4, 0, left, 4, 0, 0, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor d45 matches builder") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[8] = {3, 1, 4, 1, 5, 9, 2, 6};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::D45_PRED, 0, 4, 4, 0, above, 4, 4, nullptr, 0, 0);

    bool ok = runBlockPredict(ctx, intra::D45_PRED, 0, above, 4, 4, nullptr, 0, 0, 0, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor d67 matches builder") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    const unsigned char left[4] = {9, 9, 9, 9};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::D67_PRED, 0, 4, 4, 7, above, 4, 4, left, 4, 0);

    bool ok = runBlockPredict(ctx, intra::D67_PRED, 0, above, 4, 4, left, 4, 0, 7, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor d203 matches builder") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[4] = {9, 9, 9, 9};
    const unsigned char left[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::D203_PRED, 0, 4, 4, 7, above, 4, 0, left, 4, 4);

    bool ok = runBlockPredict(ctx, intra::D203_PRED, 0, above, 4, 0, left, 4, 4, 7, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor d135 matches builder") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[8] = {100, 101, 102, 103, 104, 105, 106, 107};
    const unsigned char left[8] = {10, 11, 12, 13, 14, 15, 16, 17};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::D135_PRED, 0, 4, 4, 90, above, 4, 4, left, 4, 4);

    bool ok = runBlockPredict(ctx, intra::D135_PRED, 0, above, 4, 4, left, 4, 4, 90, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor d113 matches builder") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[4] = {10, 20, 30, 40};
    const unsigned char left[4] = {5, 15, 25, 35};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::D113_PRED, 0, 4, 4, 7, above, 4, 0, left, 4, 0);

    bool ok = runBlockPredict(ctx, intra::D113_PRED, 0, above, 4, 0, left, 4, 0, 7, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor d157 matches builder") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[4] = {10, 20, 30, 40};
    const unsigned char left[4] = {5, 15, 25, 35};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::D157_PRED, 0, 4, 4, 7, above, 4, 0, left, 4, 0);

    bool ok = runBlockPredict(ctx, intra::D157_PRED, 0, above, 4, 0, left, 4, 0, 7, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor honors angle delta") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    const unsigned char left[4] = {9, 9, 9, 9};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::D67_PRED, -1, 4, 4, 7, above, 4, 4, left, 4, 0);

    bool ok = runBlockPredict(ctx, intra::D67_PRED, -1, above, 4, 4, left, 4, 0, 7, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor honors angle delta in zone 2") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[4] = {10, 20, 30, 40};
    const unsigned char left[4] = {5, 15, 25, 35};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::D135_PRED, -1, 4, 4, 7, above, 4, 0, left, 4, 0);

    bool ok = runBlockPredict(ctx, intra::D135_PRED, -1, above, 4, 0, left, 4, 0, 7, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor skips edge filtering when disabled like the builder") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    const unsigned char left[4] = {9, 9, 9, 9};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::D67_PRED, 0, 4, 4, 7, above, 4, 4, left, 4, 0,
                                intra::NeighborContext(), -1, true);

    bool ok = runBlockPredict(ctx, intra::D67_PRED, 0, above, 4, 4, left, 4, 0, 7, ref, 0, 0, -1, 1);
    CHECK(ok);
}

TEST_CASE("builder dc falls back to dc top when left is missing") {
    const unsigned char above[4] = {10, 20, 30, 40};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::DC_PRED, 0, 4, 4, 0, above, 4, 0, nullptr, 0, 0);
    CHECK(dst[0] == 25);
}

TEST_CASE("builder dc falls back to dc left when top is missing") {
    const unsigned char left[4] = {20, 40, 60, 80};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::DC_PRED, 0, 4, 4, 0, nullptr, 0, 0, left, 4, 0);
    CHECK(dst[0] == 50);
}

TEST_CASE("builder dc fills 128 when both edges are missing") {
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::DC_PRED, 0, 4, 4, 0, nullptr, 0, 0, nullptr, 0, 0);
    CHECK(dst[0] == 128);
}

TEST_CASE("gpu block predictor dc falls back to dc top") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[4] = {10, 20, 30, 40};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::DC_PRED, 0, 4, 4, 0, above, 4, 0, nullptr, 0, 0);

    bool ok = runBlockPredict(ctx, intra::DC_PRED, 0, above, 4, 0, nullptr, 0, 0, 0, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor dc falls back to dc left with top missing") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char left[4] = {20, 40, 60, 80};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::DC_PRED, 0, 4, 4, 0, nullptr, 0, 0, left, 4, 0);

    bool ok = runBlockPredict(ctx, intra::DC_PRED, 0, nullptr, 0, 0, left, 4, 0, 0, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor dc fills 128 with no edges") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::DC_PRED, 0, 4, 4, 0, nullptr, 0, 0, nullptr, 0, 0);

    bool ok = runBlockPredict(ctx, intra::DC_PRED, 0, nullptr, 0, 0, nullptr, 0, 0, 0, ref);
    CHECK(ok);
}


