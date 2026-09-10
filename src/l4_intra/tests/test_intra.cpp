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
    intra::filterIntraPredictor(dst, 4, ab + 1, left, 0, 4, 4);
    CHECK(dst[0] == 25);
}

TEST_CASE("filter intra predictor mode 1 matches svt golden") {
    // golden: svt_av1_filter_intra_predictor_c, FILTER_V_PRED, corner 10,
    // above {20,30,40,50}, left {21,31,41,51}
    unsigned char ab[6] = {10, 20, 30, 40, 50};
    const unsigned char left[4] = {21, 31, 41, 51};
    unsigned char dst[16] = {0};
    intra::filterIntraPredictor(dst, 4, ab + 1, left, 1, 4, 4);
    CHECK(dst[0] == 27);
}

TEST_CASE("filter intra predictor mode 2 matches svt golden") {
    // golden: FILTER_H_PRED, same fixture
    unsigned char ab[6] = {10, 20, 30, 40, 50};
    const unsigned char left[4] = {21, 31, 41, 51};
    unsigned char dst[16] = {0};
    intra::filterIntraPredictor(dst, 4, ab + 1, left, 2, 4, 4);
    CHECK(dst[0] == 26);
}

TEST_CASE("filter intra predictor mode 3 matches svt golden") {
    // golden: FILTER_D157_PRED, same fixture
    unsigned char ab[6] = {10, 20, 30, 40, 50};
    const unsigned char left[4] = {21, 31, 41, 51};
    unsigned char dst[16] = {0};
    intra::filterIntraPredictor(dst, 4, ab + 1, left, 3, 4, 4);
    CHECK(dst[0] == 22);
}

TEST_CASE("filter intra predictor mode 4 matches svt golden") {
    // golden: FILTER_PAETH_PRED, same fixture
    unsigned char ab[6] = {10, 20, 30, 40, 50};
    const unsigned char left[4] = {21, 31, 41, 51};
    unsigned char dst[16] = {0};
    intra::filterIntraPredictor(dst, 4, ab + 1, left, 4, 4, 4);
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

TEST_CASE("filter intra predictor 8x8 matches svt golden") {
    // golden: svt_av1_filter_intra_predictor_c at TX_8X8, FILTER_V_PRED,
    // corner 10, above {20..90}, left {21,31..91} — the two-column strip case
    // (bw=8, strips at c=1,5). Gate line b5_fiv8.
    unsigned char ab[10] = {10, 20, 30, 40, 50, 60, 70, 80, 90};
    const unsigned char left[8] = {21, 31, 41, 51, 61, 71, 81, 91};
    unsigned char dst[64] = {0};
    intra::filterIntraPredictor(dst, 8, ab + 1, left, 1, 8, 8);
    // dump actual values for debugging
    for (int i = 0; i < 8; ++i) {
        std::uint8_t row[8];
        for (int c = 0; c < 8; ++c) row[c] = dst[i * 8 + c];
        (void)row;
    }
    // full-vector golden from gate line b5_fiv8 (verified identical to host)
    const unsigned char golden[64] = {
        27, 34, 43, 51, 61, 70, 80, 90,
        33, 38, 45, 53, 62, 71, 81, 90,
        39, 42, 48, 54, 63, 71, 81, 90,
        46, 46, 50, 56, 64, 72, 82, 90,
        52, 50, 53, 57, 65, 72, 82, 90,
        59, 54, 55, 59, 66, 73, 83, 90,
        65, 58, 58, 60, 67, 73, 83, 90,
        72, 62, 60, 62, 68, 74, 84, 90};
    bool ok = true;
    for (int i = 0; i < 64; ++i) {
        if (dst[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("builder v 8x8 matches svt golden") {
    // golden: build_intra_predictors TX_8X8 V_PRED, above {31,12,77,4,50,23,68,15}
    // gate line b5_v8
    const unsigned char above[8] = {31, 12, 77, 4, 50, 23, 68, 15};
    unsigned char dst[64] = {0};
    intra::buildIntraPredictors(dst, 8, intra::V_PRED, 0, 8, 8, 0, above, 8, 0, nullptr, 0, 0);
    const unsigned char golden[64] = {
        31, 12, 77, 4, 50, 23, 68, 15,
        31, 12, 77, 4, 50, 23, 68, 15,
        31, 12, 77, 4, 50, 23, 68, 15,
        31, 12, 77, 4, 50, 23, 68, 15,
        31, 12, 77, 4, 50, 23, 68, 15,
        31, 12, 77, 4, 50, 23, 68, 15,
        31, 12, 77, 4, 50, 23, 68, 15,
        31, 12, 77, 4, 50, 23, 68, 15};
    bool ok = true;
    for (int i = 0; i < 64; ++i) {
        if (dst[i] != golden[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("builder d67 8x8 exercises the upsample path") {
    // golden: build_intra_predictors TX_8X8 D67_PRED, above (8+8 topright),
    // left {9x8}, corner 7 — blk_wh=16, delta=-23, 0<d<40 → upsample IS live
    // gate line b5_d67_8
    const unsigned char above[16] = {10, 20, 30, 100, 50, 60, 70, 80, 90, 40, 25, 66, 11, 72, 33, 58};
    const unsigned char left[8] = {9, 9, 9, 9, 9, 9, 9, 9};
    unsigned char dst[64] = {0};
    intra::buildIntraPredictors(dst, 8, intra::D67_PRED, 0, 8, 8, 7, above, 8, 8, left, 8, 0);
    bool ok = true;
    // spot-check from the generator output (full vector too long to inline):
    // gate line b5_d67_8 begins: 14 21 63 82 51 64 74 88 18 27 90 59 57 68 78 90 ...
    const unsigned char goldenHead[16] = {14, 21, 63, 82, 51, 64, 74, 88, 18, 27, 90, 59, 57, 68, 78, 90};
    for (int i = 0; i < 16; ++i) {
        if (dst[i] != goldenHead[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("builder dc128 8x8 fills 128 with no neighbors") {
    // golden: build_intra_predictors TX_8X8 DC_PRED, no edges — gate line b5_dc128_8
    unsigned char dst[64] = {0};
    intra::buildIntraPredictors(dst, 8, intra::DC_PRED, 0, 8, 8, 0, nullptr, 0, 0, nullptr, 0, 0);
    bool ok = true;
    for (int i = 0; i < 64; ++i) {
        if (dst[i] != 128) ok = false;
    }
    CHECK(ok);
}

// ---- B16: 16x16 builder (TX_16X16 column) ----------------------------------
// fixture edges shared with the generator (main_primitives.c B16 block):
// above16[0..15] top, above16[16..31] top-right, left16[0..15] left
namespace {
const unsigned char kAbove16[32] = {11, 22, 33, 44, 55, 66, 77, 88,
                                    99, 110, 120, 130, 140, 150, 160, 170,
                                    180, 190, 200, 210, 220, 230, 240, 250,
                                    245, 235, 225, 215, 205, 195, 185, 175};
const unsigned char kLeft16[32] = {5, 15, 25, 35, 45, 55, 65, 75,
                                   85, 95, 105, 115, 125, 135, 145, 155,
                                   165, 175, 185, 195, 205, 215, 225, 235,
                                   245, 250, 240, 230, 220, 210, 200, 190};
}  // namespace

TEST_CASE("builder v 16x16 matches svt golden") {
    // golden: build_intra_predictors TX_16X16 V_PRED, above16[0..15]
    // gate line b16_v
    unsigned char dst[256] = {0};
    intra::buildIntraPredictors(dst, 16, intra::V_PRED, 0, 16, 16, 0, kAbove16, 16, 0, nullptr, 0, 0);
    const unsigned char goldenRow[16] = {11, 22, 33, 44, 55, 66, 77, 88,
                                         99, 110, 120, 130, 140, 150, 160, 170};
    bool ok = true;
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; ++c) {
            if (dst[r * 16 + c] != goldenRow[c]) ok = false;
        }
    }
    CHECK(ok);
}

TEST_CASE("builder dc 16x16 averages 32 edge samples") {
    // golden: gate line b16_dc — DC over above16+left16 (sum 2750/32 = 85.94
    // -> 86 with rounding)
    unsigned char dst[256] = {0};
    intra::buildIntraPredictors(dst, 16, intra::DC_PRED, 0, 16, 16, 7, kAbove16, 16, 0, kLeft16, 16, 0);
    bool ok = true;
    for (int i = 0; i < 256; ++i) {
        if (dst[i] != 86) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("builder dc128 16x16 fills 128 with no neighbors") {
    // golden: gate line b16_dc128
    unsigned char dst[256] = {0};
    intra::buildIntraPredictors(dst, 16, intra::DC_PRED, 0, 16, 16, 0, nullptr, 0, 0, nullptr, 0, 0);
    bool ok = true;
    for (int i = 0; i < 256; ++i) {
        if (dst[i] != 128) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("builder d45 16x16 consumes real top-right zone 1") {
    // golden: gate line b16_d45 — need_right at p_angle 45: numTop = 32 from
    // above16+TR; upsample OFF at blk_wh 32 (svt_aom_use_intra_edge_upsample
    // returns 0); edge filter strength 2 (filt_str(16,16,-23,0): d=23 -> 2).
    // head from the gate line: 23 33 44 55 66 77 88 99 110 120 130 140 150
    // 160 170 180 ...
    unsigned char dst[256] = {0};
    intra::buildIntraPredictors(dst, 16, intra::D45_PRED, 0, 16, 16, 7, kAbove16, 16, 16, kLeft16, 16, 0);
    const unsigned char goldenHead[16] = {23, 33, 44, 55, 66, 77, 88, 99,
                                          110, 120, 130, 140, 150, 160, 170, 180};
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (dst[i] != goldenHead[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("builder d135 16x16 zone 2 matches svt golden head") {
    // golden: gate line b16_d135 head
    unsigned char dst[256] = {0};
    intra::buildIntraPredictors(dst, 16, intra::D135_PRED, 0, 16, 16, 7, kAbove16, 16, 0, kLeft16, 16, 0);
    const unsigned char goldenHead[16] = {8, 15, 23, 33, 44, 55, 66, 77,
                                          88, 99, 110, 120, 130, 140, 150, 159};
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (dst[i] != goldenHead[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("builder d203 16x16 zone 3 extends left edge to 32") {
    // golden: gate line b16_d203 head — need_bottom: numLeft = 32 (replication
    // past 16 exercises the extension path)
    unsigned char dst[256] = {0};
    intra::buildIntraPredictors(dst, 16, intra::D203_PRED, 0, 16, 16, 7, kAbove16, 16, 0, kLeft16, 16, 0);
    const unsigned char goldenHead[16] = {11, 14, 18, 22, 26, 30, 34, 39,
                                          43, 47, 51, 56, 60, 64, 68, 73};
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (dst[i] != goldenHead[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("builder smooth 16x16 uses the bs=16 weight row") {
    // golden: gate line b16_sm head — sm_weight_arrays[16..31]
    unsigned char dst[256] = {0};
    intra::buildIntraPredictors(dst, 16, intra::SMOOTH_PRED, 0, 16, 16, 7, kAbove16, 16, 0, kLeft16, 16, 0);
    const unsigned char goldenHead[16] = {9, 24, 39, 52, 66, 79, 91, 102,
                                          113, 123, 131, 139, 147, 154, 160, 165};
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (dst[i] != goldenHead[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("builder paeth 16x16 matches svt golden head") {
    // golden: gate line b16_paeth head (row 0 = above row: pTop wins on the
    // flat corner gradient)
    unsigned char dst[256] = {0};
    intra::buildIntraPredictors(dst, 16, intra::PAETH_PRED, 0, 16, 16, 7, kAbove16, 16, 0, kLeft16, 16, 0);
    const unsigned char goldenHead[16] = {11, 22, 33, 44, 55, 66, 77, 88,
                                          99, 110, 120, 130, 140, 150, 160, 170};
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (dst[i] != goldenHead[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("builder filter-intra 16x16 strips at c=1,5,9,13") {
    // golden: gate line b16_fiv (FILTER_V_PRED at TX_16X16: fb buffer 17x17,
    // row strips rr odd, column strips c=1,5,9,13)
    unsigned char dst[256] = {0};
    intra::buildIntraPredictors(dst, 16, intra::V_PRED, 0, 16, 16, 10, kAbove16 + 1, 16, 0,
                                kLeft16, 16, 0, intra::NeighborContext(),
                                static_cast<int>(intra::FilterIntraMode::FILTER_V_PRED));
    const unsigned char goldenHead[16] = {19, 31, 43, 54, 65, 77, 88, 99,
                                          110, 120, 130, 140, 150, 160, 170, 180};
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (dst[i] != goldenHead[i]) ok = false;
    }
    CHECK(ok);
}

TEST_CASE("16x16 upsample never fires (blk_wh 32 exceeds both limits)") {
    // svt_aom_use_intra_edge_upsample (intra_prediction.c:1773):
    // type ? (blk_wh <= 8) : (blk_wh <= 16); 16+16 = 32 fails both
    CHECK(intra::useIntraEdgeUpsample(16, 16, 0, 0) == 0);
    CHECK(intra::useIntraEdgeUpsample(16, 16, 23, 0) == 0);
    CHECK(intra::useIntraEdgeUpsample(16, 16, 23, 1) == 0);
    CHECK(intra::useIntraEdgeUpsample(16, 16, 39, 0) == 0);
}

TEST_CASE("builder dc128 8x8 fills 128 with no neighbors") {
    // golden: build_intra_predictors TX_8X8 DC_PRED, no edges — gate line b5_dc128_8
    unsigned char dst[64] = {0};
    intra::buildIntraPredictors(dst, 8, intra::DC_PRED, 0, 8, 8, 0, nullptr, 0, 0, nullptr, 0, 0);
    bool ok = true;
    for (int i = 0; i < 64; ++i) {
        if (dst[i] != 128) ok = false;
    }
    CHECK(ok);
}

bool runBlockPredict8x8(gpurt::GpuContext& ctx, int mode, int angleDelta, const unsigned char* above,
                        int nTopPx, int nTopRightPx, const unsigned char* left, int nLeftPx,
                        int nBottomLeftPx, int aboveLeft, const unsigned char* expected,
                        int aboveMode = 0, int leftMode = 0, int filterIntraMode = -1,
                        int disableEdgeFilter = 0) {
    (void)ctx;
    const std::string ptx = *gpurt::compileToPtx(intra::predictBlock8x8CuSource(), "compute_61");
    const std::vector<std::string> names = gpurt::ptxEntryNames(ptx);
    const auto it = std::find(names.begin(), names.end(), "predict_block_8x8");
    if (it == names.end()) {
        return false;
    }
    gpurt::Kernel k(ptx, *it);

    gpurt::DeviceBuffer dAbove(nTopPx > 0 ? sizeof(unsigned char) * (nTopPx + nTopRightPx) : 1);
    gpurt::DeviceBuffer dLeft(nLeftPx > 0 ? sizeof(unsigned char) * (nLeftPx + nBottomLeftPx) : 1);
    gpurt::DeviceBuffer dOut(64);
    if (nTopPx > 0) dAbove.uploadFrom(above, sizeof(unsigned char) * (nTopPx + nTopRightPx));
    if (nLeftPx > 0) dLeft.uploadFrom(left, sizeof(unsigned char) * (nLeftPx + nBottomLeftPx));

    int modeArg = mode, deltaArg = angleDelta, amArg = aboveMode, lmArg = leftMode;
    int fiArg = filterIntraMode, defArg = disableEdgeFilter;
    int nTopArg = nTopPx, nTrArg = nTopRightPx, nLeftArg = nLeftPx, nBlArg = nBottomLeftPx;
    int alArg = aboveLeft;
    gpurt::DeviceBuffer dMode(4), dDelta(4), dAm(4), dLm(4), dFi(4), dDef(4);
    gpurt::DeviceBuffer dNTop(4), dNTr(4), dNLeft(4), dNBl(4), dAl(4);
    dMode.uploadFrom(&modeArg, 4); dDelta.uploadFrom(&deltaArg, 4);
    dAm.uploadFrom(&amArg, 4); dLm.uploadFrom(&lmArg, 4);
    dFi.uploadFrom(&fiArg, 4); dDef.uploadFrom(&defArg, 4);
    dNTop.uploadFrom(&nTopArg, 4); dNTr.uploadFrom(&nTrArg, 4);
    dNLeft.uploadFrom(&nLeftArg, 4); dNBl.uploadFrom(&nBlArg, 4);
    dAl.uploadFrom(&alArg, 4);

    CUdeviceptr pMode = dMode.get(), pDelta = dDelta.get(), pAm = dAm.get(), pLm = dLm.get();
    CUdeviceptr pAbove = dAbove.get(), pNTop = dNTop.get(), pNTr = dNTr.get();
    CUdeviceptr pLeft = dLeft.get(), pNLeft = dNLeft.get(), pNBl = dNBl.get(), pAl = dAl.get();
    CUdeviceptr pFi = dFi.get(), pDef = dDef.get(), pOut = dOut.get();
    void* args[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                    &pNBl, &pAl, &pFi, &pDef, &pOut};
    k.launch(1, 1, 64, 1, args);

    unsigned char got[64] = {0};
    dOut.downloadTo(got, 64);
    for (int i = 0; i < 64; ++i) {
        if (got[i] != expected[i]) return false;
    }
    return true;
}

bool runBlockPredict16x16(gpurt::GpuContext& ctx, int mode, int angleDelta, const unsigned char* above,
                          int nTopPx, int nTopRightPx, const unsigned char* left, int nLeftPx,
                          int nBottomLeftPx, int aboveLeft, const unsigned char* expected,
                          int aboveMode = 0, int leftMode = 0, int filterIntraMode = -1,
                          int disableEdgeFilter = 0) {
    (void)ctx;
    const std::string ptx = *gpurt::compileToPtx(intra::predictBlock16x16CuSource(), "compute_61");
    const std::vector<std::string> names = gpurt::ptxEntryNames(ptx);
    const auto it = std::find(names.begin(), names.end(), "predict_block_16x16");
    if (it == names.end()) {
        return false;
    }
    gpurt::Kernel k(ptx, *it);

    gpurt::DeviceBuffer dAbove(nTopPx > 0 ? sizeof(unsigned char) * (nTopPx + nTopRightPx) : 1);
    gpurt::DeviceBuffer dLeft(nLeftPx > 0 ? sizeof(unsigned char) * (nLeftPx + nBottomLeftPx) : 1);
    gpurt::DeviceBuffer dOut(256);
    if (nTopPx > 0) dAbove.uploadFrom(above, sizeof(unsigned char) * (nTopPx + nTopRightPx));
    if (nLeftPx > 0) dLeft.uploadFrom(left, sizeof(unsigned char) * (nLeftPx + nBottomLeftPx));

    int modeArg = mode, deltaArg = angleDelta, amArg = aboveMode, lmArg = leftMode;
    int fiArg = filterIntraMode, defArg = disableEdgeFilter;
    int nTopArg = nTopPx, nTrArg = nTopRightPx, nLeftArg = nLeftPx, nBlArg = nBottomLeftPx;
    int alArg = aboveLeft;
    gpurt::DeviceBuffer dMode(4), dDelta(4), dAm(4), dLm(4), dFi(4), dDef(4);
    gpurt::DeviceBuffer dNTop(4), dNTr(4), dNLeft(4), dNBl(4), dAl(4);
    dMode.uploadFrom(&modeArg, 4); dDelta.uploadFrom(&deltaArg, 4);
    dAm.uploadFrom(&amArg, 4); dLm.uploadFrom(&lmArg, 4);
    dFi.uploadFrom(&fiArg, 4); dDef.uploadFrom(&defArg, 4);
    dNTop.uploadFrom(&nTopArg, 4); dNTr.uploadFrom(&nTrArg, 4);
    dNLeft.uploadFrom(&nLeftArg, 4); dNBl.uploadFrom(&nBlArg, 4);
    dAl.uploadFrom(&alArg, 4);

    CUdeviceptr pMode = dMode.get(), pDelta = dDelta.get(), pAm = dAm.get(), pLm = dLm.get();
    CUdeviceptr pAbove = dAbove.get(), pNTop = dNTop.get(), pNTr = dNTr.get();
    CUdeviceptr pLeft = dLeft.get(), pNLeft = dNLeft.get(), pNBl = dNBl.get(), pAl = dAl.get();
    CUdeviceptr pFi = dFi.get(), pDef = dDef.get(), pOut = dOut.get();
    void* args[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                    &pNBl, &pAl, &pFi, &pDef, &pOut};
    k.launch(1, 1, 256, 1, args);

    unsigned char got[256] = {0};
    dOut.downloadTo(got, 256);
    for (int i = 0; i < 256; ++i) {
        if (got[i] != expected[i]) return false;
    }
    return true;
}

TEST_CASE("gpu block predictor 16x16 matches builder across all zones") {
    if (gpurt::deviceCount() == 0) { MESSAGE("SKIP: no CUDA device"); return; }
    gpurt::GpuContext ctx;
    // zone sweep mirroring the b16 gate fixtures: V (angle 90 collapse),
    // D45 w/ real TR (zone 1, extension + clamp), D135 (zone 2),
    // D203 (zone 3, bottom-left extension), SMOOTH (w16 row), PAETH,
    // DC both-edges, DC-128, filter-intra strip case
    bool okV = true, okD45 = true, okD135 = true, okD203 = true, okSm = true, okPa = true;
    bool okDc = true, okDc128 = true, okFi = true;
    {
        unsigned char ref[256] = {0};
        intra::buildIntraPredictors(ref, 16, intra::V_PRED, 0, 16, 16, 0, kAbove16, 16, 0, nullptr, 0, 0);
        okV = runBlockPredict16x16(ctx, intra::V_PRED, 0, kAbove16, 16, 0, nullptr, 0, 0, 0, ref);
    }
    {
        unsigned char ref[256] = {0};
        intra::buildIntraPredictors(ref, 16, intra::D45_PRED, 0, 16, 16, 7, kAbove16, 16, 16, kLeft16, 16, 0);
        okD45 = runBlockPredict16x16(ctx, intra::D45_PRED, 0, kAbove16, 16, 16, kLeft16, 16, 0, 7, ref);
    }
    {
        unsigned char ref[256] = {0};
        intra::buildIntraPredictors(ref, 16, intra::D135_PRED, 0, 16, 16, 7, kAbove16, 16, 0, kLeft16, 16, 0);
        okD135 = runBlockPredict16x16(ctx, intra::D135_PRED, 0, kAbove16, 16, 0, kLeft16, 16, 0, 7, ref);
    }
    {
        unsigned char ref[256] = {0};
        intra::buildIntraPredictors(ref, 16, intra::D203_PRED, 0, 16, 16, 7, kAbove16, 16, 0, kLeft16, 16, 0);
        okD203 = runBlockPredict16x16(ctx, intra::D203_PRED, 0, kAbove16, 16, 0, kLeft16, 16, 0, 7, ref);
    }
    {
        unsigned char ref[256] = {0};
        intra::buildIntraPredictors(ref, 16, intra::SMOOTH_PRED, 0, 16, 16, 7, kAbove16, 16, 0, kLeft16, 16, 0);
        okSm = runBlockPredict16x16(ctx, intra::SMOOTH_PRED, 0, kAbove16, 16, 0, kLeft16, 16, 0, 7, ref);
    }
    {
        unsigned char ref[256] = {0};
        intra::buildIntraPredictors(ref, 16, intra::PAETH_PRED, 0, 16, 16, 7, kAbove16, 16, 0, kLeft16, 16, 0);
        okPa = runBlockPredict16x16(ctx, intra::PAETH_PRED, 0, kAbove16, 16, 0, kLeft16, 16, 0, 7, ref);
    }
    {
        unsigned char ref[256] = {0};
        intra::buildIntraPredictors(ref, 16, intra::DC_PRED, 0, 16, 16, 7, kAbove16, 16, 0, kLeft16, 16, 0);
        okDc = runBlockPredict16x16(ctx, intra::DC_PRED, 0, kAbove16, 16, 0, kLeft16, 16, 0, 7, ref);
    }
    {
        unsigned char ref[256] = {0};
        intra::buildIntraPredictors(ref, 16, intra::DC_PRED, 0, 16, 16, 0, nullptr, 0, 0, nullptr, 0, 0);
        okDc128 = runBlockPredict16x16(ctx, intra::DC_PRED, 0, nullptr, 0, 0, nullptr, 0, 0, 0, ref);
    }
    {
        unsigned char ref[256] = {0};
        intra::buildIntraPredictors(ref, 16, intra::V_PRED, 0, 16, 16, 10, kAbove16 + 1, 16, 0,
                                    kLeft16, 16, 0, intra::NeighborContext(),
                                    static_cast<int>(intra::FilterIntraMode::FILTER_V_PRED));
        okFi = runBlockPredict16x16(ctx, intra::V_PRED, 0, kAbove16 + 1, 16, 0, kLeft16, 16, 0, 10, ref,
                                    0, 0, static_cast<int>(intra::FilterIntraMode::FILTER_V_PRED));
    }
    CHECK(okV);
    CHECK(okD45);
    CHECK(okD135);
    CHECK(okD203);
    CHECK(okSm);
    CHECK(okPa);
    CHECK(okDc);
    CHECK(okDc128);
    CHECK(okFi);
}

// ---- HK2: R-series delta enumeration, completed ----------------------------
// ONE loop-driven test per geometry: GPU kernel == host builder for ALL dr
// modes (V_PRED..D67_PRED) x deltas {-3,-2,-1,+1,+2,+3} (delta=0 is covered
// by the per-mode zone tests above). 48 combos per geometry.
// HOST-PIN REASONING (stated per the R-series rule): the host builder is
// 1:1-pinned against verbatim-SVT gate goldens at representative
// (mode, delta) combos - b9_vd1_8 / b9_hm1_8 / b9_vd1_4 / b9_hm1_4
// (delta +1 V / -1 H at both geometries, R-series) plus the zone goldens
// b16_d45/b16_d135/b16_d203 and b5_d67_8/b5_d45ef_8 (delta 0 zones 1/2/3).
// This enumeration therefore completes the range rule: any kernel dispatch
// divergence for an untested (mode, delta) pair shows up here as GPU != host
// against a host whose representative points are gate-pinned.
// Edges: above = first 2B of kAbove16 (B above + B top-right), left =
// kLeft16, corner 7, both neighbor modes DC (filt_type 0).

TEST_CASE("hk2 delta enumeration 4x4: gpu == host for all 8 dr modes x deltas -3..3 (excl 0)") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;
    bool allOk = true;
    for (int m = intra::V_PRED; m <= intra::D67_PRED; ++m) {
        for (int delta = -3; delta <= 3; ++delta) {
            if (delta == 0) continue;
            unsigned char ref[16] = {0};
            intra::buildIntraPredictors(ref, 4, static_cast<intra::PredictionMode>(m), delta, 4, 4, 7,
                                        kAbove16, 4, 4, kLeft16, 4, 0);
            const bool ok =
                runBlockPredict(ctx, m, delta, kAbove16, 4, 4, kLeft16, 4, 0, 7, ref);
            if (!ok) allOk = false;
        }
    }
    CHECK(allOk);
}

TEST_CASE("hk2 delta enumeration 8x8: gpu == host for all 8 dr modes x deltas -3..3 (excl 0)") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;
    bool allOk = true;
    for (int m = intra::V_PRED; m <= intra::D67_PRED; ++m) {
        for (int delta = -3; delta <= 3; ++delta) {
            if (delta == 0) continue;
            unsigned char ref[64] = {0};
            intra::buildIntraPredictors(ref, 8, static_cast<intra::PredictionMode>(m), delta, 8, 8, 7,
                                        kAbove16, 8, 8, kLeft16, 8, 0);
            const bool ok =
                runBlockPredict8x8(ctx, m, delta, kAbove16, 8, 8, kLeft16, 8, 0, 7, ref);
            if (!ok) allOk = false;
        }
    }
    CHECK(allOk);
}

TEST_CASE("hk2 delta enumeration 16x16: gpu == host for all 8 dr modes x deltas -3..3 (excl 0)") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;
    bool allOk = true;
    for (int m = intra::V_PRED; m <= intra::D67_PRED; ++m) {
        for (int delta = -3; delta <= 3; ++delta) {
            if (delta == 0) continue;
            unsigned char ref[256] = {0};
            intra::buildIntraPredictors(ref, 16, static_cast<intra::PredictionMode>(m), delta, 16, 16,
                                        7, kAbove16, 16, 16, kLeft16, 16, 0);
            const bool ok =
                runBlockPredict16x16(ctx, m, delta, kAbove16, 16, 16, kLeft16, 16, 0, 7, ref);
            if (!ok) allOk = false;
        }
    }
    CHECK(allOk);
}

TEST_CASE("gpu block predictor 8x8 v matches builder") {
    if (gpurt::deviceCount() == 0) { MESSAGE("SKIP: no CUDA device"); return; }
    gpurt::GpuContext ctx;
    const unsigned char above[8] = {31, 12, 77, 4, 50, 23, 68, 15};
    unsigned char ref[64] = {0};
    intra::buildIntraPredictors(ref, 8, intra::V_PRED, 0, 8, 8, 0, above, 8, 0, nullptr, 0, 0);
    bool ok = runBlockPredict8x8(ctx, intra::V_PRED, 0, above, 8, 0, nullptr, 0, 0, 0, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor 8x8 dc128 matches builder") {
    if (gpurt::deviceCount() == 0) { MESSAGE("SKIP: no CUDA device"); return; }
    gpurt::GpuContext ctx;
    unsigned char ref[64] = {0};
    intra::buildIntraPredictors(ref, 8, intra::DC_PRED, 0, 8, 8, 0, nullptr, 0, 0, nullptr, 0, 0);
    bool ok = runBlockPredict8x8(ctx, intra::DC_PRED, 0, nullptr, 0, 0, nullptr, 0, 0, 0, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor 8x8 d67 upsample matches builder") {
    if (gpurt::deviceCount() == 0) { MESSAGE("SKIP: no CUDA device"); return; }
    gpurt::GpuContext ctx;
    const unsigned char above[16] = {10, 20, 30, 100, 50, 60, 70, 80, 90, 40, 25, 66, 11, 72, 33, 58};
    const unsigned char left[8] = {9, 9, 9, 9, 9, 9, 9, 9};
    unsigned char ref[64] = {0};
    intra::buildIntraPredictors(ref, 8, intra::D67_PRED, 0, 8, 8, 7, above, 8, 8, left, 8, 0);
    bool ok = runBlockPredict8x8(ctx, intra::D67_PRED, 0, above, 8, 8, left, 8, 0, 7, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor 8x8 filter intra matches builder") {
    if (gpurt::deviceCount() == 0) { MESSAGE("SKIP: no CUDA device"); return; }
    gpurt::GpuContext ctx;
    const unsigned char above[9] = {10, 20, 30, 40, 50, 60, 70, 80, 90};
    const unsigned char left[8] = {21, 31, 41, 51, 61, 71, 81, 91};
    unsigned char ref[64] = {0};
    intra::buildIntraPredictors(ref, 8, intra::V_PRED, 0, 8, 8, 10, above + 1, 8, 0, left, 8, 0,
                                intra::NeighborContext(), 1);
    bool ok = runBlockPredict8x8(ctx, intra::V_PRED, 0, above + 1, 8, 0, left, 8, 0, 10, ref, 0, 0, 1);
    CHECK(ok);
}

TEST_CASE("gpu block predictor 8x8 paeth matches builder") {
    if (gpurt::deviceCount() == 0) { MESSAGE("SKIP: no CUDA device"); return; }
    gpurt::GpuContext ctx;
    const unsigned char above[8] = {10, 40, 30, 20, 60, 25, 45, 35};
    const unsigned char left[8] = {50, 60, 70, 80, 55, 65, 75, 85};
    unsigned char ref[64] = {0};
    intra::buildIntraPredictors(ref, 8, intra::PAETH_PRED, 0, 8, 8, 45, above, 8, 0, left, 8, 0);
    bool ok = runBlockPredict8x8(ctx, intra::PAETH_PRED, 0, above, 8, 0, left, 8, 0, 45, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor 8x8 smooth matches builder") {
    if (gpurt::deviceCount() == 0) { MESSAGE("SKIP: no CUDA device"); return; }
    gpurt::GpuContext ctx;
    const unsigned char above[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    const unsigned char left[8] = {5, 15, 25, 35, 45, 55, 65, 75};
    unsigned char ref[64] = {0};
    intra::buildIntraPredictors(ref, 8, intra::SMOOTH_PRED, 0, 8, 8, 0, above, 8, 0, left, 8, 0);
    bool ok = runBlockPredict8x8(ctx, intra::SMOOTH_PRED, 0, above, 8, 0, left, 8, 0, 0, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor 8x8 angle delta d67-1 matches builder") {
    if (gpurt::deviceCount() == 0) { MESSAGE("SKIP: no CUDA device"); return; }
    gpurt::GpuContext ctx;
    const unsigned char above[16] = {10, 20, 30, 100, 50, 60, 70, 80, 90, 40, 25, 66, 11, 72, 33, 58};
    const unsigned char left[8] = {9, 9, 9, 9, 9, 9, 9, 9};
    unsigned char ref[64] = {0};
    intra::buildIntraPredictors(ref, 8, intra::D67_PRED, -1, 8, 8, 7, above, 8, 8, left, 8, 0);
    bool ok = runBlockPredict8x8(ctx, intra::D67_PRED, -1, above, 8, 8, left, 8, 0, 7, ref);
    CHECK(ok);
}

TEST_CASE("gpu block predictor 8x8 h matches builder") {
    // QW3 finding: predict_block_8x8's isDr range (m >= 1) wrongly captures
    // H_PRED (m==2): the dr override (pAngle defaults to 90 -> needLeft=0)
    // skipped the left fill, so H read zeroed shared memory. The 4x4 kernel
    // and the host (kExtendModes + kModeToAngle[2]=180) are correct.
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char left[8] = {14, 3, 8, 13, 17, 9, 19, 26};
    unsigned char ref[64] = {0};
    intra::buildIntraPredictors(ref, 8, intra::H_PRED, 0, 8, 8, 0, left, 0, 0, left, 8, 0);
    bool okNoTop = runBlockPredict8x8(ctx, intra::H_PRED, 0, left, 0, 0, left, 8, 0, 0, ref);
    CHECK(okNoTop);

    const unsigned char above[16] = {31, 12, 77, 4, 50, 23, 68, 15, 9, 41, 27, 63, 11, 55, 38, 72};
    for (int i = 0; i < 64; ++i) ref[i] = 0;
    intra::buildIntraPredictors(ref, 8, intra::H_PRED, 0, 8, 8, 7, above, 8, 8, left, 8, 0);
    bool okWithTop = runBlockPredict8x8(ctx, intra::H_PRED, 0, above, 8, 8, left, 8, 0, 7, ref);
    CHECK(okWithTop);
}

TEST_CASE("gpu block predictor honors angle delta in zone 2 for v and h at 8x8") {
    // R-series: kernel isDr parity. goldens: golden_gen b9_vd1_8 / b9_hm1_8
    // (verbatim build_intra_predictors, pAngle = mode_to_angle_map + delta*3:
    // V delta=+1 -> 93 zone 2; H delta=-1 -> 177 zone 2). The pre-R1 kernels
    // ignored delta for V/H (isDr started at m>=3), so GPU != host here.
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[16] = {31, 12, 77, 4, 50, 23, 68, 15, 9, 41, 27, 63, 11, 55, 38, 72};
    const unsigned char left[8] = {14, 3, 8, 13, 17, 9, 19, 26};

    {
        // golden: golden_gen b9_vd1_8
        const unsigned char ref[64] = {
            30, 13, 74, 8, 48, 24, 66, 18, 29, 13, 72, 11, 45, 26, 64, 20,
            28, 14, 69, 15, 43, 27, 62, 23, 27, 15, 66, 18, 40, 28, 60, 26,
            26, 15, 63, 22, 38, 30, 58, 29, 25, 16, 61, 25, 35, 31, 56, 31,
            24, 17, 58, 29, 33, 32, 54, 34, 23, 17, 55, 33, 31, 34, 52, 37};
        unsigned char host[64] = {0};
        intra::buildIntraPredictors(host, 8, intra::V_PRED, 1, 8, 8, 7, above, 8, 8, left, 8, 0);
        bool hostOk = true;
        for (int i = 0; i < 64; ++i) {
            if (host[i] != ref[i]) hostOk = false;
        }
        CHECK(hostOk);
        bool gpuOk = runBlockPredict8x8(ctx, intra::V_PRED, 1, above, 8, 8, left, 8, 0, 7, host);
        CHECK(gpuOk);
    }
    {
        // golden: golden_gen b9_hm1_8 (pAngle 177, zone 2)
        const unsigned char ref[64] = {
            14, 13, 13, 13, 13, 12, 12, 12, 4,  4,  5,  5,  6,  6,  7,  8,
            8,  7,  7,  7,  7,  6,  6,  6,  13, 13, 12, 12, 12, 12, 12, 12,
            17, 17, 17, 17, 17, 16, 16, 16, 9,  10, 10, 11, 11, 11, 12, 12,
            18, 18, 17, 17, 16, 16, 15, 15, 26, 25, 25, 25, 25, 24, 24, 24};
        unsigned char host[64] = {0};
        intra::buildIntraPredictors(host, 8, intra::H_PRED, -1, 8, 8, 7, above, 8, 8, left, 8, 0);
        bool hostOk = true;
        for (int i = 0; i < 64; ++i) {
            if (host[i] != ref[i]) hostOk = false;
        }
        CHECK(hostOk);
        bool gpuOk = runBlockPredict8x8(ctx, intra::H_PRED, -1, above, 8, 8, left, 8, 0, 7, host);
        CHECK(gpuOk);
    }
}

TEST_CASE("gpu block predictor honors angle delta for v and h at 4x4") {
    // R-series: 4x4 kernel parity (goldens: golden_gen b9_vd1_4 / b9_hm1_4)
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char above[16] = {31, 12, 77, 4, 50, 23, 68, 15, 9, 41, 27, 63, 11, 55, 38, 72};
    const unsigned char left[4] = {14, 3, 8, 13};

    {
        // golden: golden_gen b9_vd1_4
        const unsigned char ref[16] = {30, 13, 74, 8, 29, 13, 72, 12, 28, 14, 69, 16, 27, 15, 66, 19};
        unsigned char host[16] = {0};
        intra::buildIntraPredictors(host, 4, intra::V_PRED, 1, 4, 4, 7, above, 4, 4, left, 4, 0);
        bool hostOk = true;
        for (int i = 0; i < 16; ++i) {
            if (host[i] != ref[i]) hostOk = false;
        }
        CHECK(hostOk);
        bool gpuOk = runBlockPredict(ctx, intra::V_PRED, 1, above, 4, 4, left, 4, 0, 7, host);
        CHECK(gpuOk);
    }
    {
        // golden: golden_gen b9_hm1_4 (pAngle 177, zone 2)
        const unsigned char ref[16] = {14, 13, 13, 13, 4, 4, 5, 5, 8, 7, 7, 7, 13, 13, 12, 12};
        unsigned char host[16] = {0};
        intra::buildIntraPredictors(host, 4, intra::H_PRED, -1, 4, 4, 7, above, 4, 4, left, 4, 0);
        bool hostOk = true;
        for (int i = 0; i < 16; ++i) {
            if (host[i] != ref[i]) hostOk = false;
        }
        CHECK(hostOk);
        bool gpuOk = runBlockPredict(ctx, intra::H_PRED, -1, above, 4, 4, left, 4, 0, 7, host);
        CHECK(gpuOk);
    }
}

TEST_CASE("gpu block predictor 8x8 d45 edge filtered matches builder") {
    // coverage fold-in from B6: filt_edge8 at strength 1 (delta=-45, d>=40)
    if (gpurt::deviceCount() == 0) { MESSAGE("SKIP: no CUDA device"); return; }
    gpurt::GpuContext ctx;
    const unsigned char above[16] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 15, 25, 35, 45, 55, 65, 75};
    const unsigned char left[8] = {12, 22, 32, 42, 52, 62, 72, 82};
    unsigned char ref[64] = {0};
    intra::buildIntraPredictors(ref, 8, intra::D45_PRED, 0, 8, 8, 5, above, 8, 8, left, 8, 0);
    bool ok = runBlockPredict8x8(ctx, intra::D45_PRED, 0, above, 8, 8, left, 8, 0, 5, ref);
    CHECK(ok);
}

TEST_CASE("builder applies edge filtering for d67 by default") {
    // golden: svt_av1_transform_two_d-style harness, build_intra_predictors
    // D67 4x4, corner 7, above {10,20,30,100,50,60,70,80} (n_top=4, n_tr=4),
    // disable_edge_filter=0 -> upsample applies; r0c1 = 21
    const unsigned char above[8] = {10, 20, 30, 100, 50, 60, 70, 80};
    const unsigned char left[4] = {9, 9, 9, 9};
    unsigned char dst[16] = {0};
    intra::buildIntraPredictors(dst, 4, intra::D67_PRED, 0, 4, 4, 7, above, 4, 4, left, 4, 0,
                                intra::NeighborContext(), -1, false);
    CHECK(dst[1] == 21);
}

TEST_CASE("builder skips the edge filter/upsample when disabled") {
    // golden: same harness, disable_edge_filter=1 -> raw z1 (no upsample);
    // r0c1 = 24. The two goldens differ at this pixel, so this test fails if
    // the !disable_edge_filter wrap is removed (the flag would be ignored).
    const unsigned char above[8] = {10, 20, 30, 100, 50, 60, 70, 80};
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

    const unsigned char above[8] = {10, 20, 30, 100, 50, 60, 70, 80};
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




TEST_CASE("gpu block predictor dr corner fill derives above-left from left edge when top missing") {
    // B8 finding: enc_intra_prediction.c fill at lines 572-582 - with nTopPx==0
    // and nLeftPx>0, aboveRow[-1] = leftRef[0], NOT the raw aboveLeft arg.
    // D113 z2 reads leftCol[-1] for column 0, so a raw 0 corner corrupts it.
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    const unsigned char left[4] = {14, 3, 8, 13};
    unsigned char ref[16] = {0};
    intra::buildIntraPredictors(ref, 4, intra::D113_PRED, 0, 4, 4, 0, nullptr, 0, 0, left, 4, 0);

    bool ok = runBlockPredict(ctx, intra::D113_PRED, 0, nullptr, 0, 0, left, 4, 0, 0, ref);
    CHECK(ok);
}
