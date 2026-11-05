#include <doctest.h>
#include <gpurt.h>
#include <motion.h>

TEST_CASE("sad8x8 of reversed ramps across two strides is 2048") {
    std::uint8_t a[8 * 8] = {0};
    std::uint8_t b[8 * 8] = {0};
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            a[y * 8 + x] = (std::uint8_t)(y * 8 + x);
            b[y * 8 + x] = (std::uint8_t)(63 - (y * 8 + x));
        }
    }
    CHECK(motion::sad8x8(a, 8, b, 8) == 2048);
}

TEST_CASE("sad8x8 honors the src stride") {
    std::uint8_t src[8 * 12] = {0};
    std::uint8_t ref[8 * 8] = {0};
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            src[y * 12 + x] = (std::uint8_t)(y * 8 + x);
            ref[y * 8 + x] = 0;
        }
    }
    CHECK(motion::sad8x8(src, 12, ref, 8) == 2016);
}

TEST_CASE("sad4x4 ramp vs zero is 136") {
    // golden: golden_gen primitives (svt_nxm_sad_kernel_helper_c @ 4x4),
    // src = 1..16 flat stride 4, ref = 0 -> 1+2+...+16 = 136
    std::uint8_t src[16] = {0};
    std::uint8_t ref[16] = {0};
    for (int i = 0; i < 16; ++i) {
        src[i] = (std::uint8_t)(i + 1);
    }
    CHECK(motion::sad4x4(src, 4, ref, 4) == 136);
}

TEST_CASE("sad4x4 reversed ramps across stride 8 is 576") {
    // golden: golden_gen primitives (svt_nxm_sad_kernel_helper_c @ 4x4):
    // 4x4 top-left sub-block of an 8x8 buffer, src = ramp, ref = 63 - ramp
    std::uint8_t src[64] = {0};
    std::uint8_t ref[64] = {0};
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            src[y * 8 + x] = (std::uint8_t)(y * 8 + x);
            ref[y * 8 + x] = (std::uint8_t)(63 - (y * 8 + x));
        }
    }
    CHECK(motion::sad4x4(src, 8, ref, 8) == 576);
}

TEST_CASE("gpu sad4x4 matches host reference") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    std::uint8_t srcB[16] = {0};
    std::uint8_t refB[16] = {0};
    for (int i = 0; i < 16; ++i) {
        srcB[i] = (std::uint8_t)(i * 7 % 13);
        refB[i] = (std::uint8_t)(i * 5 % 11);
    }
    const std::uint32_t srcStride = 4;
    const std::uint32_t refStride = 4;
    const std::uint32_t ref = motion::sad4x4(srcB, srcStride, refB, refStride);

    const std::string ptx = *gpurt::compileToPtx(motion::sad4x4CuSource(), "compute_61");
    const std::string entry = gpurt::ptxEntryNames(ptx).at(0);
    gpurt::Kernel k(ptx, entry);

    gpurt::DeviceBuffer dSrc(sizeof(srcB));
    gpurt::DeviceBuffer dRef(sizeof(refB));
    gpurt::DeviceBuffer dOut(sizeof(ref));
    dSrc.uploadFrom(srcB, sizeof(srcB));
    dRef.uploadFrom(refB, sizeof(refB));

    unsigned int sa = srcStride;
    unsigned int sb = refStride;
    gpurt::DeviceBuffer dSa(sizeof(sa));
    gpurt::DeviceBuffer dSb(sizeof(sb));
    dSa.uploadFrom(&sa, sizeof(sa));
    dSb.uploadFrom(&sb, sizeof(sb));

    CUdeviceptr pSrc = dSrc.get();
    CUdeviceptr pSa = dSa.get();
    CUdeviceptr pRef = dRef.get();
    CUdeviceptr pSb = dSb.get();
    CUdeviceptr pOut = dOut.get();
    void* args[] = {&pSrc, &pSa, &pRef, &pSb, &pOut};
    k.launch(1, 1, 1, 1, args);

    unsigned int got = 0;
    dOut.downloadTo(&got, sizeof(got));
    CHECK(got == ref);
}

TEST_CASE("gpu sad8x8 matches host reference") {
    if (gpurt::deviceCount() == 0) {
        MESSAGE("SKIP: no CUDA device");
        return;
    }
    gpurt::GpuContext ctx;

    std::uint8_t a[64] = {0};
    std::uint8_t b[64] = {0};
    for (int i = 0; i < 64; ++i) {
        a[i] = (std::uint8_t)(i % 5);
        b[i] = (std::uint8_t)(i * 3 % 11);
    }
    const std::uint32_t aStride = 8;
    const std::uint32_t bStride = 8;
    const std::uint32_t ref = motion::sad8x8(a, aStride, b, bStride);

    const std::string ptx = *gpurt::compileToPtx(motion::sad8x8CuSource(), "compute_61");
    const std::string entry = gpurt::ptxEntryNames(ptx).at(0);
    gpurt::Kernel k(ptx, entry);

    gpurt::DeviceBuffer dA(sizeof(a));
    gpurt::DeviceBuffer dB(sizeof(b));
    gpurt::DeviceBuffer dOut(sizeof(ref));
    dA.uploadFrom(a, sizeof(a));
    dB.uploadFrom(b, sizeof(b));

    unsigned int sa = aStride;
    unsigned int sb = bStride;
    gpurt::DeviceBuffer dSa(sizeof(sa));
    gpurt::DeviceBuffer dSb(sizeof(sb));
    dSa.uploadFrom(&sa, sizeof(sa));
    dSb.uploadFrom(&sb, sizeof(sb));

    CUdeviceptr pA = dA.get();
    CUdeviceptr pSa = dSa.get();
    CUdeviceptr pB = dB.get();
    CUdeviceptr pSb = dSb.get();
    CUdeviceptr pOut = dOut.get();
    void* args[] = {&pA, &pSa, &pB, &pSb, &pOut};
    k.launch(1, 1, 1, 1, args);

    unsigned int got = 0;
    dOut.downloadTo(&got, sizeof(got));
    CHECK(got == ref);
}
// CS1: svt_aom_sad_16b_kernel_c (C_DEFAULT/compute_sad_c.c:42-61). The
// 16-bit-plane twin of the nxm helper: EB_ABS_DIFF over width x height,
// strided both sides. Fixture: 3 rows x 4 cols, src stride 5, ramp
// 1..12, ref all zero -> SAD = 10 + 26 + 42 = 78.
TEST_CASE("sad16b 3x4 ramp vs zero at stride 5 is 78") {
    std::uint16_t src[3 * 5] = {0};
    std::uint16_t ref[3 * 5] = {0};
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            src[y * 5 + x] = static_cast<std::uint16_t>(y * 4 + x + 1);
        }
    }
    CHECK(motion::sad16b(src, 5, ref, 5, 3, 4) == 78);
}

// CS1 mutation fixture for sad16b: ref nonzero so the ABS direction is
// exercised - ref 9..12 vs src 1..4 gives per-element |src-ref| = 8
// four times = 32; a signed (non-abs) difference would give -32.
TEST_CASE("sad16b abs direction: ref 9..12 vs src 1..4 is 32") {
    std::uint16_t src[4] = {1, 2, 3, 4};
    std::uint16_t ref[4] = {9, 10, 11, 12};
    CHECK(motion::sad16b(src, 4, ref, 4, 1, 4) == 32);
}

// CS1: svt_sad_loop_kernel_c (C_DEFAULT/compute_sad_c.c:63-101). Full
// search sweep; best_sad inits to 0xffffff and updates on strictly-less.
// Fixture: src 8x8 all 10; ref pitch 16, perfect 8x8 block of 10s at
// search offset (0,2); every other candidate differs. best = 0 at (0,2).
TEST_CASE("sadLoopKernel finds the zero-SAD offset (0,2)") {
    std::uint8_t src[8 * 8];
    for (int i = 0; i < 64; ++i) src[i] = 10;
    std::uint8_t ref[16 * 20] = {0};
    for (int y = 2; y < 10; ++y) {
        for (int x = 0; x < 8; ++x) ref[y * 16 + x] = 10;
    }
    std::uint64_t best = 0;
    std::int16_t xs = -1, ys = -1;
    motion::sadLoopKernel(src, 8, ref, 16, 8, 8, &best, &xs, &ys, 16, 0, 2, 4);
    CHECK(best == 0);
    CHECK(xs == 0);
    CHECK(ys == 2);
}

// CS1 skip-line fixture: block 16x8, skip=1 drops EVEN search rows
// (:80). The zero-SAD block sits at (0,2) - skipped - so the best is
// the first strictly-minimum odd-row candidate: (0,1) SAD 160 (row 1
// zeros vs 10s, 16 wide = 160; (0,3) ties at 160 and loses to
// strictly-less). skip=0 finds the (0,2) zero instead.
TEST_CASE("sadLoopKernel skip_search_line skips even rows") {
    std::uint8_t src[16 * 8];
    for (int i = 0; i < 128; ++i) src[i] = 10;
    std::uint8_t ref[16 * 20] = {0};
    for (int y = 2; y < 10; ++y) {
        for (int x = 0; x < 16; ++x) ref[y * 16 + x] = 10;
    }
    std::uint64_t best = 0;
    std::int16_t xs = -1, ys = -1;
    motion::sadLoopKernel(src, 16, ref, 16, 8, 16, &best, &xs, &ys, 16, 1, 1, 4);
    CHECK(best == 160);
    CHECK(xs == 0);
    CHECK(ys == 1);
    best = 0; xs = -1; ys = -1;
    motion::sadLoopKernel(src, 16, ref, 16, 8, 16, &best, &xs, &ys, 16, 0, 1, 4);
    CHECK(best == 0);
    CHECK(ys == 2);
}

// CS1 ref-advance fixture: the per-search-row ref advance is
// src_stride_raw (:97), NOT ref_stride - a raw pointer walk that may
// land mid-line of the ref plane. ref_stride 20, src_stride_raw 16,
// block 4x4 all 10. Candidate (xi,yi) reads ref[yi*16 + r*20 + xi + c].
// The zero block is written at exactly candidate (0,1)'s reads:
// offsets 16..19, 36..39, 56..59, 76..79. If the ref advanced by
// ref_stride instead, (0,1) reads 20..83/20 rows (all zero there ->
// SAD 160) and no candidate is zero.
TEST_CASE("sadLoopKernel ref advance uses src_stride_raw not ref_stride") {
    std::uint8_t src[4 * 4];
    for (int i = 0; i < 16; ++i) src[i] = 10;
    std::uint8_t ref[20 * 5] = {0};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) ref[r * 20 + 16 + c] = 10;
    }
    std::uint64_t best = 0;
    std::int16_t xs = -1, ys = -1;
    motion::sadLoopKernel(src, 4, ref, 20, 4, 4, &best, &xs, &ys, 16, 0, 2, 2);
    CHECK(best == 0);
    CHECK(xs == 0);
    CHECK(ys == 1);
}

// CS1: obmc_sad (C_DEFAULT/sad_av1.c:18-34). SAD = sum over pixels of
// ROUND_POWER_OF_TWO(abs(wsrc[x] - pre[x]*mask[x]), 12). Fixture:
// pre all 10, wsrc = 12288 (10*4096 scaled 12-bit?), mask = 4096.
// |12288 - 10*4096| = 2048 -> >>12 = 0 per pixel -> total 0? No: use
// wsrc = 45056, mask = 4096, pre = 10 -> |45056-40960| = 4096 >>12 = 1
// per pixel, 4x4 block = 16 pixels -> 16.
TEST_CASE("obmcSad 4x4 with wsrc 45056 mask 4096 pre 10 is 16") {
    std::uint8_t pre[16];
    std::int32_t wsrc[16], mask[16];
    for (int i = 0; i < 16; ++i) { pre[i] = 10; wsrc[i] = 45056; mask[i] = 4096; }
    CHECK(motion::obmcSad(pre, 4, wsrc, mask, 4, 4) == 16);
}
