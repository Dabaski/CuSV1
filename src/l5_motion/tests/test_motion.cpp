#include <doctest.h>
#include <gpurt.h>
#include <motion.h>
#include <hash_motion.h>
#include <global_motion.h>

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

// HM1 H1: hash.c svt_av1_crc32c_table_init (hash.c:28-50) +
// svt_av1_get_crc32c_value_c (hash.c:55-76). Empty buffer: crc starts
// 0 ^ 0xffffffff, no bytes consumed, final xor returns 0.
TEST_CASE("crc32c of empty buffer is 0") {
    motion::crc32cInit();
    CHECK(motion::getCrc32cValue(nullptr, 0) == 0);
}

// HM1 H2: the CRC-32C standard check value. Exercises the byte head
// (until 8-alignment) and byte tail (len % 8) paths of
// svt_av1_get_crc32c_value_c (hash.c:55-76).
TEST_CASE("crc32c of 123456789 is 0xE3069283") {
    motion::crc32cInit();
    const std::uint8_t buf[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(motion::getCrc32cValue(buf, 9) == 0xE3069283);
}

// HM1 H3: the quadword body (hash.c:63-70) must agree with a bitwise
// CRC-32C oracle computed independently of the table (one bit at a time
// with POLY 0x82f63b78, hash.c:15). alignas(8) forces the quadword path;
// the aligned head loop is skipped, so the quadword table selects
// crc32cTable[7..0].
static std::uint32_t crc32cBitwise(const std::uint8_t* buf, int len) {
    std::uint32_t crc = 0xffffffff;
    for (int i = 0; i < len; ++i) {
        crc ^= buf[i];
        for (int j = 0; j < 8; ++j) crc = (crc & 1) ? (crc >> 1) ^ 0x82f63b78 : crc >> 1;
    }
    return crc ^ 0xffffffff;
}
TEST_CASE("crc32c quadword path matches bitwise oracle on 16 bytes") {
    motion::crc32cInit();
    alignas(8) const std::uint8_t buf[16] = {0x0f, 0x1e, 0x2d, 0x3c, 0x4b, 0x5a, 0x69, 0x78,
                                             0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0};
    const std::uint32_t quad = motion::getCrc32cValue(buf, 16);
    CHECK(quad == crc32cBitwise(buf, 16));
}

// HM1: get_identity_hash_value (hash_motion.c:78-82). The four 8-bit
// inputs pack as-is into 32 bits: a<<24 + b<<16 + c<<8 + d.
TEST_CASE("identity hash packs four bytes big-endian") {
    CHECK(motion::getIdentityHashValue(0x12, 0x34, 0x56, 0x78) == 0x12345678u);
}

// HM2: get_xor_hash_value_hbd (hash_motion.c:84-93). Pack the lower 8
// bits of each 16-bit input at byte positions 3..0, then xor the upper
// bytes folded to the same positions.
TEST_CASE("xor hash hbd folds upper bytes onto lower packing") {
    CHECK(motion::getXorHashValueHbd(0x0102, 0x0304, 0x0506, 0x0708) == (0x02040608u ^ 0x01030507u));
}

// HM3: hash_block_size_to_index (hash_motion.c:59-76) - the full range
// enumerated: 4->0, 8->1, 16->2, 32->3, 64->4, 128->5, default -1.
TEST_CASE("block hash size index maps every legal size and -1 default") {
    CHECK(motion::hashBlockSizeToIndex(4) == 0);
    CHECK(motion::hashBlockSizeToIndex(8) == 1);
    CHECK(motion::hashBlockSizeToIndex(16) == 2);
    CHECK(motion::hashBlockSizeToIndex(32) == 3);
    CHECK(motion::hashBlockSizeToIndex(64) == 4);
    CHECK(motion::hashBlockSizeToIndex(128) == 5);
    CHECK(motion::hashBlockSizeToIndex(2) == -1);
}

// HM4: get_pixels_in_1d_char_array_by_block_2x2 (hash_motion.c:34-43).
// The gather is row-major over the 2x2 block: p[0]=(0,0), p[1]=(0,1),
// p[2]=(1,0), p[3]=(1,1) - the byte-order rule the identity hash and
// the CRC fold both depend on. Stride 8 separates the rows so the two
// rows' values are distinct.
TEST_CASE("2x2 pixel gather is row-major") {
    std::uint8_t src[2 * 8];
    for (int i = 0; i < 16; ++i) src[i] = static_cast<std::uint8_t>(i + 1);
    std::uint8_t out[4];
    motion::getPixels2x2(src, 8, out);
    CHECK(out[0] == 1);
    CHECK(out[1] == 2);
    CHECK(out[2] == 9);
    CHECK(out[3] == 10);
}

// HM5: get_block_hash_value (hash_motion.c:309-385) at 4x4 luma. The four
// 2x2 subblocks get identity hashes (row-major byte order, HM4), packed
// little-endian into 16 bytes, CRC32C-folded once. hash_value1 = (crc &
// 0xffff) + (index(4)<<16) = crc & 0xffff since index 4 -> 0. Oracle: the
// independent bitwise CRC (H3 fixture) over the hand-packed bytes.
static std::uint32_t crc32cBitwiseOf(const std::uint32_t* words, int nWords) {
    std::uint8_t bytes[16];
    for (int i = 0; i < nWords; ++i) {
        bytes[4 * i] = static_cast<std::uint8_t>(words[i]);
        bytes[4 * i + 1] = static_cast<std::uint8_t>(words[i] >> 8);
        bytes[4 * i + 2] = static_cast<std::uint8_t>(words[i] >> 16);
        bytes[4 * i + 3] = static_cast<std::uint8_t>(words[i] >> 24);
    }
    return crc32cBitwise(bytes, 4 * nWords);
}
TEST_CASE("getBlockHashValue 4x4 luma folds four identity 2x2 hashes") {
    std::uint8_t src[16];
    for (int i = 0; i < 16; ++i) src[i] = static_cast<std::uint8_t>(i);
    std::uint32_t h1 = 0, h2 = 0;
    motion::getBlockHashValue(src, 4, 4, &h1, &h2);
    // 2x2 identity hashes for p = x + 4y: (0,0)->0x00010405, (2,0)->0x02030607,
    // (0,2)->0x08090c0d, (2,2)->0x0a0b0e0f, gathered row-major.
    const std::uint32_t words[4] = {0x00010405u, 0x02030607u, 0x08090c0du, 0x0a0b0e0fu};
    const std::uint32_t crc = crc32cBitwiseOf(words, 4);
    CHECK(h1 == (crc & 0xffffu));
    CHECK(h2 == crc);
}

// HM6: getBlockHashValue at 8x8 (hash_motion.c:309-385). Sixteen 2x2
// identity hashes fold once into four CRCs (the 2x2 grid of 4x4 groups),
// then those four fold into the final CRC. h1 = (crc2 & 0xffff) + (1<<16)
// since index(8) = 1.
TEST_CASE("getBlockHashValue 8x8 folds through two CRC levels") {
    std::uint8_t src[64];
    for (int i = 0; i < 64; ++i) src[i] = static_cast<std::uint8_t>(i * 7 + 3);
    std::uint32_t h1 = 0, h2 = 0;
    motion::getBlockHashValue(src, 8, 8, &h1, &h2);
    // 16 identity hashes, row-major gather at even offsets, stride 8.
    std::uint32_t id[16];
    int k = 0;
    for (int y = 0; y < 8; y += 2)
        for (int x = 0; x < 8; x += 2)
            id[k++] = motion::getIdentityHashValue(src[y * 8 + x], src[y * 8 + x + 1],
                                                   src[(y + 1) * 8 + x], src[(y + 1) * 8 + x + 1]);
    std::uint32_t c1[4];
    for (int g = 0; g < 4; ++g) {
        std::uint32_t grp[4] = {id[2 * g], id[2 * g + 1], id[2 * g + 8], id[2 * g + 9]};
        c1[g] = crc32cBitwiseOf(grp, 4);
    }
    std::uint32_t grp2[4] = {c1[0], c1[1], c1[2], c1[3]};
    const std::uint32_t c2 = crc32cBitwiseOf(grp2, 4);
    CHECK(h1 == (c2 & 0xffffu) + (1u << 16));
    CHECK(h2 == c2);
}

// HM7: svt_av1_generate_block_2x2_hash_value (hash_motion.c:153-216)
// de-walled to plain params (named deviation: buf/stride/width/height in
// place of Yv12BufferConfig). pos advances x_end times per row then
// (width-1) once, i.e. pos = y_pos*width + x_pos with stride = pic width.
TEST_CASE("generateBlock2x2HashValue identity layout at 8x8") {
    std::uint8_t pic[64];
    for (int i = 0; i < 64; ++i) pic[i] = static_cast<std::uint8_t>(i + 11);
    std::uint32_t hash[7 * 8];
    motion::generateBlock2x2HashValue(pic, 8, 8, 8, hash);
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            const std::uint8_t* blk = pic + y * 8 + x;
            const std::uint32_t want = motion::getIdentityHashValue(
                blk[0], blk[1], blk[8], blk[9]);
            CHECK(hash[y * 8 + x] == want);
        }
    }
}

// HM8: svt_av1_generate_block_hash_value (hash_motion.c:192-216) de-walled
// (named deviation: picWidth/picHeight/blockSize params). Fold: dst[pos] =
// crc32c over the LE-packed 4 u32 {src[pos], src[pos+half],
// src[pos+half*picWidth], src[pos+half*picWidth+half]}, grid stride =
// picWidth, x_end = picWidth - blockSize + 1.
TEST_CASE("generateBlockHashValue folds a 4-cell block into crc32c") {
    std::uint32_t srcHash[8 * 8];
    for (int i = 0; i < 64; ++i) srcHash[i] = 0x01020304u + static_cast<std::uint32_t>(i) * 0x101u;
    std::uint32_t dstHash[8 * 8];
    motion::generateBlockHashValue(8, 8, 4, srcHash, dstHash);
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            const std::uint32_t pos = static_cast<std::uint32_t>(y * 8 + x);
            const std::uint32_t words[4] = {srcHash[pos], srcHash[pos + 2],
                                            srcHash[pos + 16], srcHash[pos + 18]};
            std::uint8_t bytes[16];
            for (int i = 0; i < 4; ++i) {
                bytes[4 * i] = static_cast<std::uint8_t>(words[i]);
                bytes[4 * i + 1] = static_cast<std::uint8_t>(words[i] >> 8);
                bytes[4 * i + 2] = static_cast<std::uint8_t>(words[i] >> 16);
                bytes[4 * i + 3] = static_cast<std::uint8_t>(words[i] >> 24);
            }
            CHECK(dstHash[pos] == crc32cBitwise(bytes, 16));
        }
    }
}

// HM9: the hierarchical walk (hash_motion.c:261-307). State machine pins
// the exact visit order for an 8x8 pic at block_size 4: 5x5 grid, x-outer
// y-inner, states 0->1->2->3 then halve. The visit callback is the named
// deviation seam (production inserts into a HashTable through it).
TEST_CASE("hash walk visits the 5x5 grid in state-machine order") {
    std::uint32_t picHash[64];
    for (int i = 0; i < 64; ++i) picHash[i] = static_cast<std::uint32_t>(i);
    std::vector<std::pair<int, int>> order;
    const bool ok = motion::hashWalkAddToHashMap(8, 8, 4, picHash,
        [&](int x, int y, std::uint32_t h1, std::uint32_t h2) {
            order.emplace_back(x, y);
            (void)h1; (void)h2;
            return true;
        });
    CHECK(ok);
    const std::pair<int, int> want[25] = {{0,0},{0,4},{4,0},{4,4},{2,0},{2,4},{0,2},{4,2},{2,2},
        {1,0},{1,2},{1,4},{3,0},{3,2},{3,4},{0,1},{0,3},{2,1},{2,3},{4,1},{4,3},
        {1,1},{1,3},{3,1},{3,3}};
    CHECK(order.size() == 25);
    for (int i = 0; i < 25 && i < static_cast<int>(order.size()); ++i) CHECK(order[i] == want[i]);
}

// HM10: the hash table machinery (hash_motion.c:95-138 create/add/count,
// :20-31 clear). Named deviation: buckets are std::vector<BlockHash> in
// place of svt_aom_vector + EB_MALLOC. The bucket cap (hash_motion.c:130)
// ignores entries beyond max_cand_per_bucket.
TEST_CASE("hash table roundtrip and bucket cap") {
    motion::HashTable t;
    motion::hashTableCreate(&t);
    motion::BlockHash bh{10, 20, 0x1234};
    for (int i = 0; i < 10; ++i) {
        bh.x = static_cast<std::int16_t>(i);
        motion::hashTableAddToTable(&t, 0x5555, bh, 4);
    }
    CHECK(motion::hashTableCount(&t, 0x5555) == 4);
    motion::hashTableClearAll(&t);
    CHECK(motion::hashTableCount(&t, 0x5555) == 0);
    motion::hashTableDestroy(&t);
}

// HM11: the full build/insert/lookup roundtrip (fixture (a)). A synthetic
// 8x8 frame: 2x2 hashes (HM7) folded to the 4x4 grid (HM8), then the walk
// (HM9) inserts into the table (HM10) keyed by hash_value1 =
// (pic_hash[pos] & 0xffff) + (index(4)<<16). Every bucket entry must
// carry the position and the raw fold value (hash_value2).
TEST_CASE("frame hash roundtrip: walk inserts every block into its bucket") {
    std::uint8_t pic[64];
    for (int i = 0; i < 64; ++i) pic[i] = static_cast<std::uint8_t>(i * 3 + 7);
    std::uint32_t h2x2[7 * 8], h4x4[7 * 8];
    motion::generateBlock2x2HashValue(pic, 8, 8, 8, h2x2);
    motion::generateBlockHashValue(8, 8, 4, h2x2, h4x4);
    motion::HashTable t;
    motion::hashTableCreate(&t);
    int visited = 0;
    motion::hashWalkAddToHashMap(8, 8, 4, h4x4, [&](int x, int y, std::uint32_t h1, std::uint32_t h2) {
        ++visited;
        const std::uint32_t pos = static_cast<std::uint32_t>(y * 8 + x);
        CHECK(h2 == h4x4[pos]);
        CHECK(h1 == ((h4x4[pos] & 0xffffu) + (0u << 16)));
        motion::BlockHash bh{static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), h2};
        motion::hashTableAddToTable(&t, h1, bh, 256);
        return true;
    });
    CHECK(visited == 25);
    int total = 0;
    for (const auto& bucket : t.buckets) total += static_cast<int>(bucket.size());
    CHECK(total == 25);
    motion::hashTableDestroy(&t);
}

// GM1: convert_to_params (global_motion.c:36-49). Identity params
// {0,0,1,0,0,1} -> wmmat {0,0,65536,0,0,65536}: translation terms 0*1024,
// diag terms ((1<<15 - 1<<15) + 1<<15) * 2 = 1<<16.
TEST_CASE("convertToParams identity gives the unit warped matrix") {
    const double params[6] = {0.0, 0.0, 1.0, 0.0, 0.0, 1.0};
    std::int32_t model[6];
    motion::convertToParams(params, model);
    CHECK(model[0] == 0);
    CHECK(model[1] == 0);
    CHECK(model[2] == 65536);
    CHECK(model[3] == 0);
    CHECK(model[4] == 0);
    CHECK(model[5] == 65536);
}

// GM2: convert_to_params translation terms and the parameter clamps
// (global_motion.c:38-41). In-range: tx=50 -> floor(50*64+.5)=3200 ->
// *1024. Clamp: tx=100 -> 6400 > GM_TRANS_MAX=4096 -> 4096*1024.
TEST_CASE("convertToParams translation scaling and clamping") {
    const double params[6] = {50.0, 60.0, 1.0, 0.0, 0.0, 1.0};
    std::int32_t model[6];
    motion::convertToParams(params, model);
    CHECK(model[0] == 3200 * 1024);
    CHECK(model[1] == 3840 * 1024);
    const double paramsBig[6] = {100.0, 0.0, 1.0, 0.0, 0.0, 1.0};
    std::int32_t modelBig[6];
    motion::convertToParams(paramsBig, modelBig);
    CHECK(modelBig[0] == 4096 * 1024);
    CHECK(modelBig[2] == 65536);
}

// GM3: get_wmtype (global_motion.c:51-61) with WARPEDMODEL_PREC_BITS=16.
// Identity: [5]=[2]=1<<16, [4]=[3]=0, [0]=[1]=0. Translation: same but
// [0] or [1] nonzero. Rotzoom: [2]==[5] and [4]==-[3]. Else affine.
TEST_CASE("getWmtype classifies identity translation rotzoom affine") {
    const std::int32_t id[6] = {0, 0, 1 << 16, 0, 0, 1 << 16};
    CHECK(motion::getWmtype(id) == motion::WmTypeIdentity);
    const std::int32_t tr[6] = {100, 200, 1 << 16, 0, 0, 1 << 16};
    CHECK(motion::getWmtype(tr) == motion::WmTypeTranslation);
    const std::int32_t rz[6] = {100, 200, 300, -100, 100, 300};
    CHECK(motion::getWmtype(rz) == motion::WmTypeRotzoom);
    const std::int32_t af[6] = {100, 200, 300, -100, 100, 400};
    CHECK(motion::getWmtype(af) == motion::WmTypeAffine);
}

// GM4: add_param_offset (global_motion.c:63-95). Zero-centering, the
// type-dependent scale/clamp, the offset add, and the re-rescale must all
// round-trip. WARPEDMODEL_PREC_BITS = 16, GM_TRANS_PREC_DIFF = 10,
// GM_ALPHA_PREC_DIFF = 1, GM_TRANS_MAX = GM_ALPHA_MAX = 4096.
TEST_CASE("HM1 GM4 addParamOffset translation in-range")
{
    // wmmat[0] = 3200*1024 (3276800): zero-center -> >>10 = 3200, add
    // offset 100 -> 3300, clamp in-range, *1024 -> 3379200.
    CHECK(motion::addParamOffset(0, 3200 * 1024, 100) == 3300 * 1024);
}

// GM5: force_wmtype (global_motion.c:97-113). The fallthrough chain zeros
// terms below the forced type: IDENTITY clears [0..1] and pins the
// diagonal, TRANSLATION also pins [3], ROTZOOM mirrors [4]/[5] off
// [2]/[3], AFFINE touches nothing but the stored type.
TEST_CASE("HM1 GM5 forceWmType")
{
    std::int32_t mat[6] = {100, -50, 65536, 32, -32, 65536};
    motion::forceWmType(mat, motion::WmTypeIdentity);
    CHECK(mat[0] == 0);
    CHECK(mat[2] == 65536);
    CHECK(mat[3] == 0);
    CHECK(mat[4] == 0);
    CHECK(mat[5] == 65536);
}

// GM6: is_enough_erroradvantage (global_motion.c:30-33). Both gates must
// hold: the ratio threshold and the ratio*cost product threshold, per
// erroradv_type (TR_0 = 0.65/20000, TR_1 = 0.50/15000, TR_2 = 0.45/14000).
TEST_CASE("HM1 GM6 isEnoughErroradvantage")
{
    CHECK(motion::isEnoughErroradvantage(0.6, 100, 0));
    CHECK(!motion::isEnoughErroradvantage(0.66, 100, 0));
    CHECK(!motion::isEnoughErroradvantage(0.6, 40000, 0));
    CHECK(motion::isEnoughErroradvantage(0.49, 30000, 1));
    CHECK(!motion::isEnoughErroradvantage(0.5, 30000, 1));
    CHECK(motion::isEnoughErroradvantage(0.44, 31000, 2));
    CHECK(!motion::isEnoughErroradvantage(0.45, 31000, 2));
}

TEST_CASE("HM1 GM7 refineIntegerizedParam descends the mock convex surface") {
    // The error surface lives in translation units: one refinement step
    // unit moves wmmat by step << 10 (GM_TRANS_PREC_DIFF = 10, see GM4),
    // so the decoded translation is wmmat >> 10. The minimum is at
    // translation {11, -7} = wmmat {11264, -7168}.
    auto errFn = [](const std::int32_t* w) -> std::int64_t {
        const std::int64_t dx = (w[0] >> 10) - 11;
        const std::int64_t dy = (w[1] >> 10) + 7;
        return dx * dx + dy * dy;
    };
    std::int32_t wmmat[6] = {0};
    motion::refineIntegerizedParam(wmmat, motion::WmTypeTranslation, 5, errFn, 1000000, false, 0.0, 0);
    CHECK(wmmat[0] == 11 * 1024);
    CHECK(wmmat[1] == -7 * 1024);
}
