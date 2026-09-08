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