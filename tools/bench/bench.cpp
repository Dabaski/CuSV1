// tools/bench/bench.cpp
// Console benchmark tool (NOT a test; own CMake target). Measurement only -
// no optimization changes live in this series.
//
// Environment: GPU name / driver / clocks recorded at run time via
// nvidia-smi (subprocess query; no NVML linkage). If nvidia-smi is not on
// PATH the tool says so and continues.
//
// Composite loop definition (printed in the header): HOST mode decision +
// per-block GPU kernel launches, fully synchronous, no batching/streams/
// graphs. Per-block launch overhead is INCLUDED in the GPU numbers.
//
// Timing: <chrono>::steady_clock around the whole frame encode, N iterations
// after warmup; median and min reported in milliseconds. Each GPU
// configuration runs one untimed verification pass against the host output
// first (bit-exactness check) and refuses to report timings on mismatch.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <pixels.h>
#include <intra.h>
#include <motion.h>
#include <transform.h>
#include <gpurt.h>
#include <pipeline.h>

namespace {

constexpr int kFrameSize = 64;
constexpr int kIterations = 30;
constexpr int kWarmup = 3;
constexpr int kQindex = 100;

// B7 fixture (16x16), tiled 4x4x to 64x64
const std::uint8_t kFixture16[256] = {
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

void makeFixture64(std::uint8_t* dst) {
    for (int ty = 0; ty < 4; ++ty) {
        for (int tx = 0; tx < 4; ++tx) {
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 16; ++x) {
                    dst[(ty * 16 + y) * kFrameSize + tx * 16 + x] = kFixture16[y * 16 + x];
                }
            }
        }
    }
}

double medianOf(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double minOf(const std::vector<double>& v) {
    double m = v[0];
    for (double d : v) {
        if (d < m) m = d;
    }
    return m;
}

void printEnv() {
    std::printf("== environment (nvidia-smi query at run time) ==\n");
    const char* cmd =
        "nvidia-smi --query-gpu=name,driver_version,clocks.sm,clocks.mem "
        "--format=csv,noheader";
    FILE* p = _popen(cmd, "r");
    if (p == nullptr) {
        std::printf("nvidia-smi unavailable; env not recorded\n");
        return;
    }
    char line[512] = {0};
    while (std::fgets(line, sizeof(line), p) != nullptr) {
        std::printf("GPU: %s", line);
    }
    const int rc = _pclose(p);
    if (rc != 0) {
        std::printf("nvidia-smi query failed (exit %d); env not recorded\n", rc);
    }
}

// Runs fn kIterations times after kWarmup untimed runs; reports median/min.
template <class F>
void timeLoop(const char* name, F&& fn) {
    for (int i = 0; i < kWarmup; ++i) {
        fn();
    }
    std::vector<double> ms;
    ms.reserve(kIterations);
    for (int i = 0; i < kIterations; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        const auto t1 = std::chrono::steady_clock::now();
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::printf("  %-40s median %9.3f  min %9.3f\n", name, medianOf(ms), minOf(ms));
}

// ---- GPU composite frame runner --------------------------------------------
// Structure identical to the bit-exactness-tested loops in test_pipeline.cpp:
// host decision per block (13-candidate SAD policy), then per-block kernel
// chain. All state lives in device buffers; after the first frame the recon
// buffer holds the deterministic result, so every timed frame does identical
// work (verified by the untimed verification pass).
//
// KNOWN INEFFICIENCY (named for BM2): one launch set PER BLOCK, fully
// synchronous - the numbers below include per-launch overhead per block.

template <int B>
class GpuFrame {
public:
    GpuFrame(const std::uint8_t* src, int qindex)
        : src_(src), quantized_(qindex >= 0), qt_{}, scan_{} {
        const std::string predSrc =
            (B == 4) ? intra::predictBlockCuSource()
                     : (B == 8) ? intra::predictBlock8x8CuSource() : intra::predictBlock16x16CuSource();
        const std::string ptxPred = *gpurt::compileToPtx(predSrc, "compute_61");
        const std::vector<std::string> pn = gpurt::ptxEntryNames(ptxPred);
        const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
        const std::vector<std::string> sn = gpurt::ptxEntryNames(ptxSub);
        const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
        const std::vector<std::string> tn = gpurt::ptxEntryNames(ptxTx);
        const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
        const std::vector<std::string> in = gpurt::ptxEntryNames(ptxInv);
        const char* predEntry = (B == 4) ? "predict_block_4x4" : (B == 8) ? "predict_block_8x8" : "predict_block_16x16";
        const char* subEntry = (B == 4) ? "subtract_4x4_plane" : (B == 8) ? "subtract_8x8_plane" : "subtract_16x16_plane";
        const char* txEntry = (B == 4) ? "fwd_txfm_2d_4x4" : (B == 8) ? "fwd_txfm_2d_8x8" : "fwd_txfm_2d_16x16";
        const char* invEntry = (B == 4) ? "inv_txfm_2d_add_4x4" : (B == 8) ? "inv_txfm_2d_add_8x8" : "inv_txfm_2d_add_16x16";
        kPred_ = std::make_unique<gpurt::Kernel>(ptxPred, *std::find(pn.begin(), pn.end(), predEntry));
        kSub_ = std::make_unique<gpurt::Kernel>(ptxSub, *std::find(sn.begin(), sn.end(), subEntry));
        kTx_ = std::make_unique<gpurt::Kernel>(ptxTx, *std::find(tn.begin(), tn.end(), txEntry));
        kInv_ = std::make_unique<gpurt::Kernel>(ptxInv, *std::find(in.begin(), in.end(), invEntry));
        if (quantized_) {
            if constexpr (B == 4) {
                transforms::defaultScan4x4(scan_);
            } else if constexpr (B == 8) {
                transforms::defaultScan8x8(scan_);
            } else {
                transforms::defaultScan16x16(scan_);
            }
            transforms::buildQuantTables(qindex, qt_);
            const std::string ptxQ = *gpurt::compileToPtx(transforms::quantCuSource(), "compute_61");
            const std::vector<std::string> qn = gpurt::ptxEntryNames(ptxQ);
            const char* qEntry =
                (B == 4) ? "quant_dequant_4x4" : (B == 8) ? "quant_dequant_8x8" : "quant_dequant_16x16";
            kQuant_ =
                std::make_unique<gpurt::Kernel>(ptxQ, *std::find(qn.begin(), qn.end(), qEntry));
            dQuantFp_.uploadFrom(qt_.quantFp, sizeof(qt_.quantFp));
            dDequant_.uploadFrom(qt_.dequant, sizeof(qt_.dequant));
            dRoundFp_.uploadFrom(qt_.roundFp, sizeof(qt_.roundFp));
            dScan_.uploadFrom(scan_, sizeof(scan_));
        }
        dPlane_.uploadFrom(src, static_cast<std::size_t>(kFrameSize * kFrameSize));
        std::uint8_t zero[kFrameSize * kFrameSize] = {0};
        dRecon_.uploadFrom(zero, static_cast<std::size_t>(kFrameSize * kFrameSize));
        // static kernel args (uploaded once; mirrors the test-loop setup)
        int deltaArg = 0;
        int fiArg = -1;
        int defArg = 0;
        int typeArg = 0;
        int planeStrideArg = kFrameSize;
        int fwdStrideArg = B;
        int invStrideArg = B;
        dDelta_.uploadFrom(&deltaArg, sizeof(deltaArg));
        dFi_.uploadFrom(&fiArg, sizeof(fiArg));
        dDef_.uploadFrom(&defArg, sizeof(defArg));
        dType_.uploadFrom(&typeArg, sizeof(typeArg));
        dPlaneStride_.uploadFrom(&planeStrideArg, sizeof(planeStrideArg));
        dFwdStride_.uploadFrom(&fwdStrideArg, sizeof(fwdStrideArg));
        dInvStride_.uploadFrom(&invStrideArg, sizeof(invStrideArg));
        const int grid = (kFrameSize / B) * (kFrameSize / B);
        recon_.assign(static_cast<std::size_t>(kFrameSize * kFrameSize), 0);
        coeffs_.assign(static_cast<std::size_t>(kFrameSize * kFrameSize), 0);
        modes_.assign(static_cast<std::size_t>(grid), 0);
    }

    bool verify(const pixels::Plane& reconRef, const std::int32_t* refCoeffs,
                const std::uint8_t* refModes, bool verbose = false) {
        runFrame();
        // decisions first: the mode map localizes the first divergent block
        int modeDivs = 0;
        std::size_t firstModeDiv = modes_.size();
        for (std::size_t i = 0; i < modes_.size(); ++i) {
            if (modes_[i] != refModes[i]) {
                if (verbose && modeDivs < 6) {
                    const int gridW = kFrameSize / B;
                    std::printf("    mode div blk=%zu (bx=%d by=%d) gpu=%d host=%d\n", i,
                                static_cast<int>(i) % gridW, static_cast<int>(i) / gridW,
                                modes_[i], refModes[i]);
                }
                if (modeDivs == 0) firstModeDiv = i;
                ++modeDivs;
            }
        }
        if (modeDivs > 0 && verbose) {
            std::printf("    total mode diffs: %d (first blk %zu)\n", modeDivs, firstModeDiv);
            std::printf("    recon row0 gpu:");
            for (int x = 0; x < 16; ++x) {
                std::printf(" %d", recon_[static_cast<std::size_t>(x)]);
            }
            std::printf("\n    recon row0 host:");
            for (int x = 0; x < 16; ++x) {
                std::printf(" %d", reconRef.at(x, 0));
            }
            std::printf("\n");
            return false;
        }
        for (int i = 0; i < kFrameSize * kFrameSize; ++i) {
            const int x = i % kFrameSize;
            const int y = i / kFrameSize;
            if (recon_[static_cast<std::size_t>(i)] != reconRef.at(x, y)) {
                if (verbose) {
                    std::printf("    recon div idx=%d (x=%d y=%d) gpu=%d host=%d\n", i, x, y,
                                recon_[static_cast<std::size_t>(i)], reconRef.at(x, y));
                }
                return false;
            }
            if (coeffs_[static_cast<std::size_t>(i)] != refCoeffs[i]) {
                if (verbose) {
                    const int blk = i / (B * B);
                    const int gridW = kFrameSize / B;
                    std::printf("    coeff div idx=%d blk=%d (bx=%d by=%d) gpu=%d host=%d\n", i,
                                blk, blk % gridW, blk / gridW,
                                coeffs_[static_cast<std::size_t>(i)], refCoeffs[i]);
                }
                return false;
            }
        }
        return true;
    }

    void runFrame() {
        constexpr int kStride = kFrameSize;
        const int gridW = kFrameSize / B;
        const int gridH = kFrameSize / B;
        constexpr int kBlk = B * B;

        std::uint8_t reconWin[kFrameSize * kFrameSize] = {0};
        // above = B real recon samples + REAL recon top-right (FR1 host
        // gather: above[B..2B-1] = recon[(py-1)][px+B..px+2B-1], reconstructed
        // by the M1 raster rule)
        std::uint8_t aboveHost[2 * B] = {0};
        std::uint8_t leftHost[2 * B] = {0};
        std::uint8_t srcBlk[kBlk] = {0};

        for (int by = 0; by < gridH; ++by) {
            for (int bx = 0; bx < gridW; ++bx) {
                const int px = bx * B;
                const int py = by * B;
                const bool hasTop = by > 0;
                const bool hasLeft = bx > 0;
                const int nTopPx = hasTop ? B : 0;
                const int nLeftPx = hasLeft ? B : 0;
                const int nTopRightPx = (hasTop && bx + 1 < gridW) ? B : 0;

                dRecon_.downloadTo(reconWin, sizeof(reconWin));
                if (hasTop) {
                    for (int i = 0; i < B + nTopRightPx; ++i) {
                        aboveHost[i] = reconWin[(py - 1) * kStride + px + i];
                    }
                }
                if (hasLeft) {
                    for (int i = 0; i < B; ++i) {
                        leftHost[i] = reconWin[(py + i) * kStride + px - 1];
                    }
                }
                int alVal = 0;
                if (hasTop && hasLeft) alVal = reconWin[(py - 1) * kStride + px - 1];

                intra::NeighborContext nctx;
                nctx.aboveMode =
                    hasTop ? static_cast<intra::PredictionMode>(modes_[(by - 1) * gridW + bx])
                           : intra::DC_PRED;
                nctx.leftMode =
                    hasLeft ? static_cast<intra::PredictionMode>(modes_[by * gridW + bx - 1])
                            : intra::DC_PRED;
                for (int y = 0; y < B; ++y) {
                    for (int x = 0; x < B; ++x) {
                        srcBlk[y * B + x] =
                            src_[static_cast<std::size_t>((py + y) * kStride + px + x)];
                    }
                }
                std::uint32_t bestSad = 0;
                int bestMode = -1;
                if constexpr (B == 4) {
                    const auto d = pipeline::decideBlockMode4x4(
                        srcBlk, aboveHost, nTopPx, nTopRightPx, leftHost, nLeftPx, 0,
                        static_cast<std::uint8_t>(alVal), nctx);
                    bestSad = d.sad;
                    bestMode = d.mode;
                } else if constexpr (B == 8) {
                    const auto d = pipeline::decideBlockMode8x8(
                        srcBlk, aboveHost, nTopPx, nTopRightPx, leftHost, nLeftPx, 0,
                        static_cast<std::uint8_t>(alVal), nctx);
                    bestSad = d.sad;
                    bestMode = d.mode;
                } else {
                    const auto d = pipeline::decideBlockMode16x16(
                        srcBlk, aboveHost, nTopPx, nTopRightPx, leftHost, nLeftPx, 0,
                        static_cast<std::uint8_t>(alVal), nctx);
                    bestSad = d.sad;
                    bestMode = d.mode;
                }
                (void)bestSad;
                modes_[static_cast<std::size_t>(by * gridW + bx)] =
                    static_cast<std::uint8_t>(bestMode);
                int modeArg = bestMode;
                int amArg = static_cast<int>(nctx.aboveMode);
                int lmArg = static_cast<int>(nctx.leftMode);
                int nTopArg = nTopPx;
                int nTrArg = nTopRightPx;
                int nLeftArg = nLeftPx;
                int nBlArg = 0;
                int alArg = alVal;
                int pxArg = px;
                int pyArg = py;
                dMode_.uploadFrom(&modeArg, sizeof(modeArg));
                dAm_.uploadFrom(&amArg, sizeof(amArg));
                dLm_.uploadFrom(&lmArg, sizeof(lmArg));
                dNTop_.uploadFrom(&nTopArg, sizeof(nTopArg));
                dNTr_.uploadFrom(&nTrArg, sizeof(nTrArg));
                dNLeft_.uploadFrom(&nLeftArg, sizeof(nLeftArg));
                dNBl_.uploadFrom(&nBlArg, sizeof(nBlArg));
                dAl_.uploadFrom(&alArg, sizeof(alArg));
                dPx_.uploadFrom(&pxArg, sizeof(pxArg));
                dPy_.uploadFrom(&pyArg, sizeof(pyArg));
                // real edges: above[0..B-1] recon row + REAL recon top-right
                // in [B..2B) (FR1 gather); matches the host composition
                if (hasTop) dAbove_.uploadFrom(aboveHost, sizeof(aboveHost));
                if (hasLeft) dLeft_.uploadFrom(leftHost, sizeof(leftHost));

                CUdeviceptr pMode = dMode_.get();
                CUdeviceptr pDelta = dDelta_.get();
                CUdeviceptr pAm = dAm_.get();
                CUdeviceptr pLm = dLm_.get();
                CUdeviceptr pAbove = dAbove_.get();
                CUdeviceptr pNTop = dNTop_.get();
                CUdeviceptr pNTr = dNTr_.get();
                CUdeviceptr pLeft = dLeft_.get();
                CUdeviceptr pNLeft = dNLeft_.get();
                CUdeviceptr pNBl = dNBl_.get();
                CUdeviceptr pAl = dAl_.get();
                CUdeviceptr pFi = dFi_.get();
                CUdeviceptr pDef = dDef_.get();
                CUdeviceptr pPred = dPred_.get();
                void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft,
                                    &pNLeft, &pNBl, &pAl, &pFi, &pDef, &pPred};
                kPred_->launch(1, 1, static_cast<unsigned>(kBlk), 1, argsPred);

                CUdeviceptr pPlane = dPlane_.get();
                CUdeviceptr pPlaneStride = dPlaneStride_.get();
                CUdeviceptr pPx = dPx_.get();
                CUdeviceptr pPy = dPy_.get();
                CUdeviceptr pResidual = dResidual_.get();
                void* argsSub[] = {&pPlane, &pPlaneStride, &pPx, &pPy, &pPred, &pResidual};
                kSub_->launch(1, 1, static_cast<unsigned>(kBlk), 1, argsSub);

                CUdeviceptr pType = dType_.get();
                CUdeviceptr pFwdStride = dFwdStride_.get();
                CUdeviceptr pBlkCoeffs = dBlkCoeffs_.get();
                void* argsTx[] = {&pResidual, &pFwdStride, &pType, &pBlkCoeffs};
                kTx_->launch(1, 1, static_cast<unsigned>(B), 1, argsTx);

                CUdeviceptr pInvStride = dInvStride_.get();
                if (quantized_) {
                    CUdeviceptr pQcoeffs = dQcoeffs_.get();
                    CUdeviceptr pDqcoeffs = dDqcoeffs_.get();
                    CUdeviceptr pEob = dEob_.get();
                    CUdeviceptr pQuantFp = dQuantFp_.get();
                    CUdeviceptr pDequant = dDequant_.get();
                    CUdeviceptr pRoundFp = dRoundFp_.get();
                    CUdeviceptr pScan = dScan_.get();
                    CUdeviceptr pBlkCoeffsQ = dBlkCoeffs_.get();
                    void* argsQuant[] = {&pBlkCoeffsQ, &pQuantFp, &pDequant, &pRoundFp, &pScan,
                                         &pQcoeffs, &pDqcoeffs, &pEob};
                    kQuant_->launch(1, 1, static_cast<unsigned>(kBlk), 1, argsQuant);
                    std::int32_t blkQ[kBlk] = {0};
                    dQcoeffs_.downloadTo(blkQ, sizeof(blkQ));
                    for (int i = 0; i < kBlk; ++i) {
                        coeffs_[static_cast<std::size_t>((by * gridW + bx) * kBlk + i)] = blkQ[i];
                    }
                    void* argsInv[] = {&pDqcoeffs, &pType, &pPred, &pInvStride};
                    kInv_->launch(1, 1, static_cast<unsigned>(B), 1, argsInv);
                } else {
                    std::int32_t blk[kBlk] = {0};
                    dBlkCoeffs_.downloadTo(blk, sizeof(blk));
                    for (int i = 0; i < kBlk; ++i) {
                        coeffs_[static_cast<std::size_t>((by * gridW + bx) * kBlk + i)] = blk[i];
                    }
                    void* argsInv[] = {&pBlkCoeffs, &pType, &pPred, &pInvStride};
                    kInv_->launch(1, 1, static_cast<unsigned>(B), 1, argsInv);
                }

                std::uint8_t blkRecon[kBlk] = {0};
                dPred_.downloadTo(blkRecon, sizeof(blkRecon));
                for (int y = 0; y < B; ++y) {
                    for (int x = 0; x < B; ++x) {
                        recon_[static_cast<std::size_t>((py + y) * kStride + px + x)] =
                            blkRecon[y * B + x];
                    }
                }
                dRecon_.uploadFrom(recon_.data(), static_cast<std::size_t>(kFrameSize * kFrameSize));
            }
        }
    }

private:
    const std::uint8_t* src_;
    bool quantized_;
    transforms::QuantTables qt_{};
    std::int16_t scan_[B * B] = {0};
    std::unique_ptr<gpurt::Kernel> kPred_;
    std::unique_ptr<gpurt::Kernel> kSub_;
    std::unique_ptr<gpurt::Kernel> kTx_;
    std::unique_ptr<gpurt::Kernel> kInv_;
    std::unique_ptr<gpurt::Kernel> kQuant_;  // valid only when quantized_
    gpurt::DeviceBuffer dPlane_{static_cast<std::size_t>(kFrameSize * kFrameSize)};
    gpurt::DeviceBuffer dRecon_{static_cast<std::size_t>(kFrameSize * kFrameSize)};
    gpurt::DeviceBuffer dResidual_{static_cast<std::size_t>(B * B) * sizeof(std::int16_t)};
    gpurt::DeviceBuffer dBlkCoeffs_{static_cast<std::size_t>(B * B) * sizeof(std::int32_t)};
    gpurt::DeviceBuffer dQcoeffs_{static_cast<std::size_t>(B * B) * sizeof(std::int32_t)};
    gpurt::DeviceBuffer dDqcoeffs_{static_cast<std::size_t>(B * B) * sizeof(std::int32_t)};
    gpurt::DeviceBuffer dEob_{sizeof(std::uint16_t)};
    gpurt::DeviceBuffer dPred_{static_cast<std::size_t>(B * B)};
    gpurt::DeviceBuffer dQuantFp_{sizeof(transforms::QuantTables::quantFp)};
    gpurt::DeviceBuffer dDequant_{sizeof(transforms::QuantTables::dequant)};
    gpurt::DeviceBuffer dRoundFp_{sizeof(transforms::QuantTables::roundFp)};
    gpurt::DeviceBuffer dScan_{sizeof(scan_)};
    gpurt::DeviceBuffer dMode_{sizeof(int)};
    gpurt::DeviceBuffer dDelta_{sizeof(int)};
    gpurt::DeviceBuffer dAm_{sizeof(int)};
    gpurt::DeviceBuffer dLm_{sizeof(int)};
    gpurt::DeviceBuffer dFi_{sizeof(int)};
    gpurt::DeviceBuffer dDef_{sizeof(int)};
    gpurt::DeviceBuffer dNTop_{sizeof(int)};
    gpurt::DeviceBuffer dNTr_{sizeof(int)};
    gpurt::DeviceBuffer dNLeft_{sizeof(int)};
    gpurt::DeviceBuffer dNBl_{sizeof(int)};
    gpurt::DeviceBuffer dAl_{sizeof(int)};
    gpurt::DeviceBuffer dType_{sizeof(int)};
    gpurt::DeviceBuffer dPlaneStride_{sizeof(int)};
    gpurt::DeviceBuffer dFwdStride_{sizeof(int)};
    gpurt::DeviceBuffer dInvStride_{sizeof(int)};
    gpurt::DeviceBuffer dPx_{sizeof(int)};
    gpurt::DeviceBuffer dPy_{sizeof(int)};
    gpurt::DeviceBuffer dAbove_{static_cast<std::size_t>(2 * B)};
    gpurt::DeviceBuffer dLeft_{static_cast<std::size_t>(2 * B)};
    std::vector<std::uint8_t> recon_;
    std::vector<std::int32_t> coeffs_;
    std::vector<std::uint8_t> modes_;
};

// ---- BM2: per-stage single-block kernel timings ----------------------------

template <int B>
void stageBench(const std::uint8_t* src64, const pixels::Plane& reconRef) {
    constexpr int kBlk = B * B;
    const char* tag = (B == 4) ? "4x4" : (B == 8) ? "8x8" : "16x16";

    const std::string predSrc =
            (B == 4) ? intra::predictBlockCuSource()
                     : (B == 8) ? intra::predictBlock8x8CuSource() : intra::predictBlock16x16CuSource();
    const std::string ptxPred = *gpurt::compileToPtx(predSrc, "compute_61");
    const std::vector<std::string> pn = gpurt::ptxEntryNames(ptxPred);
    const std::string ptxSub = *gpurt::compileToPtx(pipeline::subtractCuSource(), "compute_61");
    const std::vector<std::string> sn = gpurt::ptxEntryNames(ptxSub);
    const std::string ptxTx = *gpurt::compileToPtx(transforms::fwdTxfmCuSource(), "compute_61");
    const std::vector<std::string> tn = gpurt::ptxEntryNames(ptxTx);
    const std::string ptxInv = *gpurt::compileToPtx(transforms::invTxfmCuSource(), "compute_61");
    const std::vector<std::string> in = gpurt::ptxEntryNames(ptxInv);
    const std::string ptxQ = *gpurt::compileToPtx(transforms::quantCuSource(), "compute_61");
    const std::vector<std::string> qn = gpurt::ptxEntryNames(ptxQ);
    const char* predEntry =
        (B == 4) ? "predict_block_4x4" : (B == 8) ? "predict_block_8x8" : "predict_block_16x16";
    const char* subEntry = (B == 4) ? "subtract_4x4_plane" : (B == 8) ? "subtract_8x8_plane" : "subtract_16x16_plane";
    const char* txEntry = (B == 4) ? "fwd_txfm_2d_4x4" : (B == 8) ? "fwd_txfm_2d_8x8" : "fwd_txfm_2d_16x16";
    const char* invEntry =
        (B == 4) ? "inv_txfm_2d_add_4x4" : (B == 8) ? "inv_txfm_2d_add_8x8" : "inv_txfm_2d_add_16x16";
    const char* qEntry = (B == 4) ? "quant_dequant_4x4" : (B == 8) ? "quant_dequant_8x8" : "quant_dequant_16x16";
    gpurt::Kernel kPred(ptxPred, *std::find(pn.begin(), pn.end(), predEntry));
    gpurt::Kernel kSub(ptxSub, *std::find(sn.begin(), sn.end(), subEntry));
    gpurt::Kernel kTx(ptxTx, *std::find(tn.begin(), tn.end(), txEntry));
    gpurt::Kernel kInv(ptxInv, *std::find(in.begin(), in.end(), invEntry));
    gpurt::Kernel kQuant(ptxQ, *std::find(qn.begin(), qn.end(), qEntry));

    // one mid-frame block with both edges: block (bx=1, by=1)
    const int px = B, py = B;
    std::uint8_t above[2 * B] = {0};
    std::uint8_t left[2 * B] = {0};
    for (int i = 0; i < B; ++i) above[i] = reconRef.at(px + i, py - 1);
    for (int i = 0; i < B; ++i) left[i] = reconRef.at(px - 1, py + i);
    const int al = reconRef.at(px - 1, py - 1);
    std::uint8_t srcBlk[kBlk] = {0};
    for (int y = 0; y < B; ++y) {
        for (int x = 0; x < B; ++x) {
            srcBlk[y * B + x] = src64[(py + y) * kFrameSize + px + x];
        }
    }

    transforms::QuantTables qt;
    transforms::buildQuantTables(kQindex, qt);
    std::int16_t scan[B * B] = {0};
    if constexpr (B == 4) {
        transforms::defaultScan4x4(scan);
    } else if constexpr (B == 8) {
        transforms::defaultScan8x8(scan);
    } else {
        transforms::defaultScan16x16(scan);
    }

    gpurt::DeviceBuffer dAbove(sizeof(above));
    gpurt::DeviceBuffer dLeft(sizeof(left));
    gpurt::DeviceBuffer dPred(kBlk);
    gpurt::DeviceBuffer dPlane(static_cast<std::size_t>(kFrameSize * kFrameSize));
    gpurt::DeviceBuffer dResidual(static_cast<std::size_t>(kBlk) * sizeof(std::int16_t));
    gpurt::DeviceBuffer dBlkCoeffs(static_cast<std::size_t>(kBlk) * sizeof(std::int32_t));
    gpurt::DeviceBuffer dQcoeffs(static_cast<std::size_t>(kBlk) * sizeof(std::int32_t));
    gpurt::DeviceBuffer dDqcoeffs(static_cast<std::size_t>(kBlk) * sizeof(std::int32_t));
    gpurt::DeviceBuffer dEob(sizeof(std::uint16_t));
    gpurt::DeviceBuffer dQuantFp(sizeof(qt.quantFp));
    gpurt::DeviceBuffer dDequant(sizeof(qt.dequant));
    gpurt::DeviceBuffer dRoundFp(sizeof(qt.roundFp));
    gpurt::DeviceBuffer dScan(sizeof(scan));
    dAbove.uploadFrom(above, sizeof(above));
    dLeft.uploadFrom(left, sizeof(left));
    dPlane.uploadFrom(src64, static_cast<std::size_t>(kFrameSize * kFrameSize));
    dQuantFp.uploadFrom(qt.quantFp, sizeof(qt.quantFp));
    dDequant.uploadFrom(qt.dequant, sizeof(qt.dequant));
    dRoundFp.uploadFrom(qt.roundFp, sizeof(qt.roundFp));
    dScan.uploadFrom(scan, sizeof(scan));

    int modeArg = intra::V_PRED;
    int deltaArg = 0;
    int amArg = 0;
    int lmArg = intra::V_PRED;
    int fiArg = -1;
    int defArg = 0;
    int nTopArg = B;
    int nTrArg = B;
    int nLeftArg = B;
    int nBlArg = 0;
    int alArg = al;
    int typeArg = 0;
    int planeStrideArg = kFrameSize;
    int txfmStrideArg = B;
    int pxArg = px;
    int pyArg = py;
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
    gpurt::DeviceBuffer dType(sizeof(typeArg));
    gpurt::DeviceBuffer dPlaneStride(sizeof(planeStrideArg));
    gpurt::DeviceBuffer dTxfmStride(sizeof(txfmStrideArg));
    gpurt::DeviceBuffer dPx(sizeof(pxArg));
    gpurt::DeviceBuffer dPy(sizeof(pyArg));
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
    dType.uploadFrom(&typeArg, sizeof(typeArg));
    dPlaneStride.uploadFrom(&planeStrideArg, sizeof(planeStrideArg));
    dTxfmStride.uploadFrom(&txfmStrideArg, sizeof(txfmStrideArg));
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
    CUdeviceptr pPlane = dPlane.get();
    CUdeviceptr pPlaneStride = dPlaneStride.get();
    CUdeviceptr pPx = dPx.get();
    CUdeviceptr pPy = dPy.get();
    CUdeviceptr pResidual = dResidual.get();
    CUdeviceptr pType = dType.get();
    CUdeviceptr pTxfmStride = dTxfmStride.get();
    CUdeviceptr pBlkCoeffs = dBlkCoeffs.get();
    CUdeviceptr pQcoeffs = dQcoeffs.get();
    CUdeviceptr pDqcoeffs = dDqcoeffs.get();
    CUdeviceptr pEob = dEob.get();
    CUdeviceptr pQuantFp = dQuantFp.get();
    CUdeviceptr pDequant = dDequant.get();
    CUdeviceptr pRoundFp = dRoundFp.get();
    CUdeviceptr pScan = dScan.get();

    std::printf("  -- geometry %s (block bx=1 by=1, V_PRED, both edges) --\n", tag);
    void* argsPred[] = {&pMode, &pDelta, &pAm, &pLm, &pAbove, &pNTop, &pNTr, &pLeft, &pNLeft,
                        &pNBl, &pAl, &pFi, &pDef, &pPred};
    timeLoop("predict_block", [&] { kPred.launch(1, 1, static_cast<unsigned>(kBlk), 1, argsPred); });

    void* argsSub[] = {&pPlane, &pPlaneStride, &pPx, &pPy, &pPred, &pResidual};
    timeLoop("subtract_plane", [&] { kSub.launch(1, 1, static_cast<unsigned>(kBlk), 1, argsSub); });

    void* argsTx[] = {&pResidual, &pTxfmStride, &pType, &pBlkCoeffs};
    timeLoop("fwd_txfm_2d", [&] { kTx.launch(1, 1, static_cast<unsigned>(B), 1, argsTx); });

    void* argsQuant[] = {&pBlkCoeffs, &pQuantFp, &pDequant, &pRoundFp, &pScan, &pQcoeffs,
                         &pDqcoeffs, &pEob};
    timeLoop("quant_dequant", [&] {
        kQuant.launch(1, 1, static_cast<unsigned>(kBlk), 1, argsQuant);
    });

    void* argsInv[] = {&pDqcoeffs, &pType, &pPred, &pTxfmStride};
    timeLoop("inv_txfm_2d_add", [&] {
        kInv.launch(1, 1, static_cast<unsigned>(B), 1, argsInv);
    });
    std::printf("  context: composite covers %d blocks/frame; per block the loop issues\n",
                (kFrameSize / B) * (kFrameSize / B));
    std::printf("           10 small H2D arg uploads + recon download + recon upload +\n");
    std::printf("           coeff download + %d launches (lossless) / %d (q100),\n", 4, 5);
    std::printf("           each a sync point\n");
}

}  // namespace

int main() {
    printEnv();

    std::printf(
        "\ncomposite loop: HOST mode decision + per-block GPU kernel launches\n"
        "(fully synchronous, ONE LAUNCH SET PER BLOCK - the known inefficiency;\n"
        " no streams/graphs/batching; per-launch overhead is in the GPU numbers)\n"
        "frame: %dx%d (B7 fixture tiled), host Q policy (13 SAD candidates)\n"
        "iterations: %d (+%d warmup), median / min in ms\n",
        kFrameSize, kFrameSize, kIterations, kWarmup);
    std::printf("NOTE: measurement only; the kernels are unmodified.\n");

    std::uint8_t src64[kFrameSize * kFrameSize];
    makeFixture64(src64);

    pixels::Plane src(kFrameSize, kFrameSize, 4);
    for (int y = 0; y < kFrameSize; ++y) {
        for (int x = 0; x < kFrameSize; ++x) {
            src.at(x, y) = src64[static_cast<std::size_t>(y * kFrameSize + x)];
        }
    }

    // host references (lossless + q100, both geometries)
    pixels::Plane recon4L(kFrameSize, kFrameSize, 4);
    pixels::Plane recon4Q(kFrameSize, kFrameSize, 4);
    pixels::Plane recon8L(kFrameSize, kFrameSize, 4);
    pixels::Plane recon8Q(kFrameSize, kFrameSize, 4);
    pixels::Plane recon16L(kFrameSize, kFrameSize, 4);
    pixels::Plane recon16Q(kFrameSize, kFrameSize, 4);
    std::vector<std::int32_t> coeffs4L(static_cast<std::size_t>(kFrameSize * kFrameSize), 0);
    std::vector<std::int32_t> coeffs4Q(static_cast<std::size_t>(kFrameSize * kFrameSize), 0);
    std::vector<std::int32_t> coeffs8L(static_cast<std::size_t>(kFrameSize * kFrameSize), 0);
    std::vector<std::int32_t> coeffs8Q(static_cast<std::size_t>(kFrameSize * kFrameSize), 0);
    std::vector<std::int32_t> coeffs16L(static_cast<std::size_t>(kFrameSize * kFrameSize), 0);
    std::vector<std::int32_t> coeffs16Q(static_cast<std::size_t>(kFrameSize * kFrameSize), 0);
    std::vector<std::uint8_t> modes4L((kFrameSize / 4) * (kFrameSize / 4), 0);
    std::vector<std::uint8_t> modes4Q((kFrameSize / 4) * (kFrameSize / 4), 0);
    std::vector<std::uint8_t> modes8L((kFrameSize / 8) * (kFrameSize / 8), 0);
    std::vector<std::uint8_t> modes8Q((kFrameSize / 8) * (kFrameSize / 8), 0);
    std::vector<std::uint8_t> modes16L((kFrameSize / 16) * (kFrameSize / 16), 0);
    std::vector<std::uint8_t> modes16Q((kFrameSize / 16) * (kFrameSize / 16), 0);

    std::printf("\n== host frame encode ==\n");
    timeLoop("host 4x4 lossless", [&] { pipeline::encodeFrameAuto4x4(src, recon4L, coeffs4L.data(), modes4L.data(), transforms::TxType::DCT_DCT); });
    timeLoop("host 4x4 q100", [&] { pipeline::encodeFrameAuto4x4Q(src, recon4Q, coeffs4Q.data(), modes4Q.data(), kQindex, transforms::TxType::DCT_DCT); });
    timeLoop("host 8x8 lossless", [&] { pipeline::encodeFrameAuto8x8(src, recon8L, coeffs8L.data(), modes8L.data(), transforms::TxType::DCT_DCT); });
    timeLoop("host 8x8 q100", [&] { pipeline::encodeFrameAuto8x8Q(src, recon8Q, coeffs8Q.data(), modes8Q.data(), kQindex, transforms::TxType::DCT_DCT); });
    timeLoop("host 16x16 lossless", [&] { pipeline::encodeFrameAuto16x16(src, recon16L, coeffs16L.data(), modes16L.data(), transforms::TxType::DCT_DCT); });
    timeLoop("host 16x16 q100", [&] { pipeline::encodeFrameAuto16x16Q(src, recon16Q, coeffs16Q.data(), modes16Q.data(), kQindex, transforms::TxType::DCT_DCT); });


    if (gpurt::deviceCount() == 0) {
        std::printf("\nSKIP: no CUDA device; GPU benches not run\n");
        return 0;
    }
    gpurt::GpuContext ctx;

    std::printf("\n== GPU composite frame encode (per-block launches) ==\n");
    {
        GpuFrame<4> g4L(src64, -1);
        if (!g4L.verify(recon4L, coeffs4L.data(), modes4L.data(), false)) {
            std::printf("  gpu 4x4 lossless VERIFY FAILED; timings withheld\n");
        } else {
            timeLoop("gpu 4x4 lossless", [&] { g4L.runFrame(); });
        }
    }
    {
        GpuFrame<4> g4Q(src64, kQindex);
        if (!g4Q.verify(recon4Q, coeffs4Q.data(), modes4Q.data(), false)) {
            std::printf("  gpu 4x4 q100 VERIFY FAILED; timings withheld\n");
        } else {
            timeLoop("gpu 4x4 q100", [&] { g4Q.runFrame(); });
        }
    }
    {
        GpuFrame<8> g8L(src64, -1);
        if (!g8L.verify(recon8L, coeffs8L.data(), modes8L.data(), false)) {
            std::printf("  gpu 8x8 lossless VERIFY FAILED; timings withheld\n");
        } else {
            timeLoop("gpu 8x8 lossless", [&] { g8L.runFrame(); });
        }
    }
    {
        GpuFrame<8> g8Q(src64, kQindex);
        if (!g8Q.verify(recon8Q, coeffs8Q.data(), modes8Q.data(), false)) {
            std::printf("  gpu 8x8 q100 VERIFY FAILED; timings withheld\n");
        } else {
            timeLoop("gpu 8x8 q100", [&] { g8Q.runFrame(); });
        }
    }
    {
        GpuFrame<16> g16L(src64, -1);
        if (!g16L.verify(recon16L, coeffs16L.data(), modes16L.data(), false)) {
            std::printf("  gpu 16x16 lossless VERIFY FAILED; timings withheld\n");
        } else {
            timeLoop("gpu 16x16 lossless", [&] { g16L.runFrame(); });
        }
    }
    {
        GpuFrame<16> g16Q(src64, kQindex);
        if (!g16Q.verify(recon16Q, coeffs16Q.data(), modes16Q.data(), false)) {
            std::printf("  gpu 16x16 q100 VERIFY FAILED; timings withheld\n");
        } else {
            timeLoop("gpu 16x16 q100", [&] { g16Q.runFrame(); });
        }
    }

    std::printf("\n== per-stage single-block launches (ONE launch per timed iteration; each\n");
    std::printf("   Kernel::launch is synchronous, so these numbers are dominated by\n");
    std::printf("   launch+sync overhead, not kernel work - same caveat as the composite) ==\n");
    stageBench<4>(src64, recon4L);
    stageBench<8>(src64, recon8L);
    stageBench<16>(src64, recon16L);
    return 0;
}
