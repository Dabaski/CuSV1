#include <doctest.h>
#include <transform.h>

TEST_CASE("l3 txshapes scaffold boots") {
    CHECK(true);
}

TEST_CASE("fdct32B at cos_bit 12 matches the landed fdct32") {
    const std::int32_t in[32] = {5, 3, 7, 1, 9, 2, 8, 4, 6, 0, 11, 13, 10, 12, 14, 15,
                                 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    std::int32_t got[32] = {0};
    std::int32_t ref[32] = {0};
    transforms::fdct32B(in, got, 12);
    transforms::fdct32(in, ref);
    CHECK(got[0] == ref[0]);
    CHECK(got[31] == ref[31]);
}

TEST_CASE("fdct8B at cos_bit 13 matches the landed fdct8") {
    const std::int32_t in[8] = {5, 3, 7, 1, 9, 2, 8, 4};
    std::int32_t got[8] = {0};
    std::int32_t ref[8] = {0};
    transforms::fdct8B(in, got, 13);
    transforms::fdct8(in, ref);
    CHECK(got[0] == ref[0]);
    CHECK(got[7] == ref[7]);
}

TEST_CASE("fdct4B at cos_bit 13 matches the landed fdct4") {
    const std::int32_t in[4] = {5, 3, 7, 1};
    std::int32_t got[4] = {0};
    std::int32_t ref[4] = {0};
    transforms::fdct4B(in, got, 13);
    transforms::fdct4(in, ref);
    CHECK(got[0] == ref[0]);
    CHECK(got[3] == ref[3]);
}

TEST_CASE("fadst4B at cos_bit 13 matches the landed fadst4") {
    const std::int32_t in[4] = {5, 3, 7, 1};
    std::int32_t got[4] = {0};
    std::int32_t ref[4] = {0};
    transforms::fadst4B(in, got, 13);
    transforms::fadst4(in, ref);
    CHECK(got[0] == ref[0]);
    CHECK(got[3] == ref[3]);
}

TEST_CASE("fadst8B at cos_bit 13 matches the landed fadst8") {
    const std::int32_t in[8] = {5, 3, 7, 1, 9, 2, 8, 4};
    std::int32_t got[8] = {0};
    std::int32_t ref[8] = {0};
    transforms::fadst8B(in, got, 13);
    transforms::fadst8(in, ref);
    CHECK(got[0] == ref[0]);
    CHECK(got[7] == ref[7]);
}

TEST_CASE("fadst32B at cos_bit 12 matches the landed fadst32") {
    const std::int32_t in[32] = {5, 3, 7, 1, 9, 2, 8, 4, 6, 0, 11, 13, 10, 12, 14, 15,
                                 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    std::int32_t got[32] = {0};
    std::int32_t ref[32] = {0};
    transforms::fadst32B(in, got, 12);
    transforms::fadst32(in, ref);
    CHECK(got[0] == ref[0]);
    CHECK(got[31] == ref[31]);
}

TEST_CASE("rect tx log ratio over every TxSize (common_utils.c:116-132)") {
    const int wide[19] = {4, 8, 16, 32, 64, 4, 8, 8, 16, 16, 32, 32, 64, 4, 16, 8, 32, 16, 64};
    const int high[19] = {4, 8, 16, 32, 64, 8, 4, 16, 8, 32, 16, 64, 32, 16, 4, 32, 8, 64, 16};
    const int expect[19] = {0, 0, 0, 0, 0, -1, 1, -1, 1, -1, 1, -1, 1, -2, 2, -2, 2, -2, 2};
    for (int i = 0; i < 19; ++i) {
        const auto size = static_cast<transforms::TxSize>(i);
        CHECK(transforms::rectTxLogRatio(transforms::txSizeWide(size), transforms::txSizeHigh(size)) == expect[i]);
        CHECK(transforms::txSizeWide(size) == wide[i]);
        CHECK(transforms::txSizeHigh(size) == high[i]);
    }
}

TEST_CASE("fwd cfg identity at TX_4X4 DCT_DCT (landed driver constants)") {
    transforms::Txfm2dFlipCfg cfg;
    transforms::fwdTransformConfig(transforms::TxType::DCT_DCT, transforms::TxSize::TX_4X4, cfg);
    CHECK(cfg.shift[0] == 2);
    CHECK(cfg.shift[1] == 0);
    CHECK(cfg.shift[2] == 0);
    CHECK(cfg.cosBitCol == 13);
    CHECK(cfg.cosBitRow == 13);
    CHECK(cfg.txfmTypeCol == transforms::TxfmTypeLs::DCT4);
    CHECK(cfg.txfmTypeRow == transforms::TxfmTypeLs::DCT4);
    CHECK(cfg.stageNumCol == 4);
    CHECK(cfg.stageNumRow == 4);
    CHECK(cfg.udFlip == 0);
    CHECK(cfg.lrFlip == 0);
}

TEST_CASE("fwd cfg identity at the four larger squares (landed driver constants)") {
    transforms::Txfm2dFlipCfg cfg;

    transforms::fwdTransformConfig(transforms::TxType::DCT_DCT, transforms::TxSize::TX_8X8, cfg);
    CHECK(cfg.shift[0] == 2);
    CHECK(cfg.shift[1] == -1);
    CHECK(cfg.shift[2] == 0);
    CHECK(cfg.cosBitCol == 13);
    CHECK(cfg.cosBitRow == 13);
    CHECK(cfg.txfmTypeCol == transforms::TxfmTypeLs::DCT8);
    CHECK(cfg.txfmTypeRow == transforms::TxfmTypeLs::DCT8);

    // 16x16: fwd_shift_16x16 = {2,-2,0} (transforms.c:124), cos 13/12
    transforms::fwdTransformConfig(transforms::TxType::DCT_DCT, transforms::TxSize::TX_16X16, cfg);
    CHECK(cfg.shift[0] == 2);
    CHECK(cfg.shift[1] == -2);
    CHECK(cfg.shift[2] == 0);
    CHECK(cfg.cosBitCol == 13);
    CHECK(cfg.cosBitRow == 12);
    CHECK(cfg.txfmTypeCol == transforms::TxfmTypeLs::DCT16);
    CHECK(cfg.txfmTypeRow == transforms::TxfmTypeLs::DCT16);
    // non-scale stage ranges: col[i] = (fdct16_range_mult2[i]+1)>>1 with the
    // DCT16 row {0,2,4,6,7,7,7,7}; row[i] = (colLast + row[i] + 1)>>1 where
    // colLast = 7 (transforms.c:2482-2489)
    CHECK(cfg.stageRangeCol[0] == 0);
    CHECK(cfg.stageRangeCol[4] == 4);
    CHECK(cfg.stageRangeRow[4] == 7);

    // 32x32: fwd_shift_32x32 = {2,-4,0} (transforms.c:125), cos 12/12
    transforms::fwdTransformConfig(transforms::TxType::DCT_DCT, transforms::TxSize::TX_32X32, cfg);
    CHECK(cfg.shift[0] == 2);
    CHECK(cfg.shift[1] == -4);
    CHECK(cfg.shift[2] == 0);
    CHECK(cfg.cosBitCol == 12);
    CHECK(cfg.cosBitRow == 12);
    CHECK(cfg.txfmTypeCol == transforms::TxfmTypeLs::DCT32);

    // 64x64: fwd_shift_64x64 = {0,-2,-2} (transforms.c:126), cos 13/10
    transforms::fwdTransformConfig(transforms::TxType::DCT_DCT, transforms::TxSize::TX_64X64, cfg);
    CHECK(cfg.shift[0] == 0);
    CHECK(cfg.shift[1] == -2);
    CHECK(cfg.shift[2] == -2);
    CHECK(cfg.cosBitCol == 13);
    CHECK(cfg.cosBitRow == 10);
    CHECK(cfg.txfmTypeCol == transforms::TxfmTypeLs::DCT64);
    CHECK(cfg.txfmTypeRow == transforms::TxfmTypeLs::DCT64);
}

TEST_CASE("TX2 DCT_FLIPADST inv reads the DCT_ADST recon columns reversed") {
    // the lr_flip read loop (inv_transforms.c:2554-2563): temp_in[r] =
    // buf[r][w-1-c] - with ud_flip = 0 the write is normal, so the column-c
    // output of DCT_FLIPADST is the column-(w-1-c) output of DCT_ADST (same
    // row pass: both htx entries map the ADST kernel, same buf).
    const int w = 4;
    const int h = 4;
    std::int32_t coeffs[w * h];
    for (int i = 0; i < w * h; ++i) {
        coeffs[i] = (i * 17) % 37 - 18;
    }
    std::uint8_t dstFlip[w * h] = {0};
    std::uint8_t dstRef[w * h] = {0};
    transforms::invTxfm2dAdd4x4(coeffs, dstFlip, w, transforms::TxType::DCT_FLIPADST);
    transforms::invTxfm2dAdd4x4(coeffs, dstRef, w, transforms::TxType::DCT_ADST);
    int bad = 0;
    int firstBad = -1;
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            if (dstFlip[r * w + c] != dstRef[r * w + (w - 1 - c)]) {
                if (firstBad < 0) firstBad = r * w + c;
                ++bad;
            }
        }
    }
    CHECK_MESSAGE(bad == 0, "DCT_FLIPADST inv vs cols-reversed DCT_ADST inv: " << bad << " cells, first at " << firstBad);
}

TEST_CASE("TX2 FLIPADST_DCT inv writes the ADST_DCT recon rows reversed") {
    // the ud_flip write loop (inv_transforms.c:2567-2577): output_w[r] gets
    // temp_out[h-1-r] - with lr_flip = 0 the whole upstream (row pass, col
    // pass inputs) is identical to ADST_DCT (FLIPADST_1D maps the ADST
    // kernel), so the recon rows come out reversed. Both dsts start at 0.
    const int w = 4;
    const int h = 4;
    std::int32_t coeffs[w * h];
    for (int i = 0; i < w * h; ++i) {
        coeffs[i] = (i * 13) % 41 - 20;
    }
    std::uint8_t dstFlip[w * h] = {0};
    std::uint8_t dstRef[w * h] = {0};
    transforms::invTxfm2dAdd4x4(coeffs, dstFlip, w, transforms::TxType::FLIPADST_DCT);
    transforms::invTxfm2dAdd4x4(coeffs, dstRef, w, transforms::TxType::ADST_DCT);
    int bad = 0;
    int firstBad = -1;
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            if (dstFlip[r * w + c] != dstRef[(h - 1 - r) * w + c]) {
                if (firstBad < 0) firstBad = r * w + c;
                ++bad;
            }
        }
    }
    CHECK_MESSAGE(bad == 0, "FLIPADST_DCT inv vs rows-reversed ADST_DCT inv: " << bad << " cells, first at " << firstBad);
}

TEST_CASE("TX2 DCT_FLIPADST fwd = row-ADST of the column-flipped intermediate") {
    // the lr_flip write loop (transforms.c:2443-2452): buf[r][w-1-c] =
    // temp_out[r] - the intermediate is written with its columns flipped, so
    // DCT_FLIPADST(x) = row-ADST of fliplr(DCT_DCT-intermediate(x)) (the row
    // pass kernel is FLIPADST_1D = the ADST kernel, inv_transforms.h:69). At
    // 4x4 the shifts are {2,0,0}: shift[1] and shift[2] are no-ops, so the
    // intermediate IS the DCT_DCT output and the oracle needs no shifts.
    const int w = 4;
    const int h = 4;
    std::int16_t src[w * h];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>(r * 11 + c * 7 + 1);
        }
    }
    std::int32_t inter[w * h] = {0};
    for (int c = 0; c < w; ++c) {
        std::int32_t colIn[4];
        std::int32_t colOut[4];
        for (int r = 0; r < h; ++r) {
            colIn[r] = src[r * w + c] * 4;  // shift[0] = 2 -> <<2
        }
        transforms::fdct4(colIn, colOut);  // col pass kernel, cos_bit 13
        for (int r = 0; r < h; ++r) {
            inter[r * w + c] = colOut[r];  // shift[1] = 0 -> no-op
        }
    }
    std::int32_t got[w * h] = {0};
    transforms::fwdTxfm2d4x4(src, got, w, transforms::TxType::DCT_FLIPADST);
    int bad = 0;
    int firstBad = -1;
    for (int r = 0; r < h; ++r) {
        std::int32_t rowIn[4];
        std::int32_t rowOut[4];
        for (int c = 0; c < w; ++c) {
            rowIn[c] = inter[r * w + (w - 1 - c)];  // flip left to right
        }
        transforms::fadst4(rowIn, rowOut);
        for (int c = 0; c < w; ++c) {
            if (got[r * w + c] != rowOut[c]) {
                if (firstBad < 0) firstBad = r * w + c;
                ++bad;
            }
        }
    }
    CHECK_MESSAGE(bad == 0, "DCT_FLIPADST vs flipped-intermediate oracle: " << bad << " cells, first at " << firstBad);
}

TEST_CASE("TX2 FLIPADST_DCT fwd = ADST_DCT of the vertically-flipped input") {
    // the ud_flip read loop (transforms.c:2430-2438): temp_in[r] =
    // input[(h-1-r)*stride + c] - the column pass sees the flipped rows, so
    // FLIPADST_DCT(x) (same kernels as ADST_DCT: col ADST, row DCT) ==
    // ADST_DCT(flipud(x)) value-for-value
    const int w = 4;
    const int h = 4;
    std::int16_t src[w * h];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>(r * 5 + c * 3);
        }
    }
    std::int16_t flipped[w * h];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            flipped[(h - 1 - r) * w + c] = src[r * w + c];
        }
    }
    std::int32_t got[w * h] = {0};
    std::int32_t ref[w * h] = {0};
    transforms::fwdTxfm2d4x4(src, got, w, transforms::TxType::FLIPADST_DCT);
    transforms::fwdTxfm2d4x4(flipped, ref, w, transforms::TxType::ADST_DCT);
    int bad = 0;
    int firstBad = -1;
    for (int i = 0; i < w * h; ++i) {
        if (got[i] != ref[i]) {
            if (firstBad < 0) firstBad = i;
            ++bad;
        }
    }
    CHECK_MESSAGE(bad == 0, "FLIPADST_DCT vs flipped ADST_DCT: " << bad << " cells, first at " << firstBad);
}

TEST_CASE("TX2 IDTX roundtrips exactly at 4x4") {
    // fwd IDTX = in*8 (up to the kernel's own rounding), inv IDTX divides it
    // back: the identity roundtrip is exact on this non-negative ramp
    // (measured maxDelta 0; a signed ramp would clip at 0 in clipPixelAdd,
    // which is the reference's own behavior)
    std::int16_t src[16];
    for (int i = 0; i < 16; ++i) {
        src[i] = static_cast<std::int16_t>(i * 7);
    }
    std::int32_t coeffs[16] = {0};
    transforms::fwdTxfm2d4x4(src, coeffs, 4, transforms::TxType::IDTX);
    std::uint8_t dst[16] = {0};
    transforms::invTxfm2dAdd4x4(coeffs, dst, 4, transforms::TxType::IDTX);
    int maxDelta = 0;
    for (int i = 0; i < 16; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    CHECK(maxDelta == 0);
}

TEST_CASE("TX2 fwdTxfm2d4x4 IDTX = the identity-kernel composition oracle") {
    // 4x4 shifts {2,0,0} (transforms.c:122): in<<2, fidentity4 (x5793>>12),
    // shift[1] 0, fidentity4, shift[2] 0. The oracle is the exported kernel
    // composed by hand; the scale bound |oracle - 8*in| <= 1 pins the shift
    // rule against silent constant drift (measured maxDelta 1 over this ramp).
    std::int16_t src[16];
    for (int i = 0; i < 16; ++i) {
        src[i] = static_cast<std::int16_t>(i * 7 - 40);
    }
    std::int32_t got[16] = {0};
    transforms::fwdTxfm2d4x4(src, got, 4, transforms::TxType::IDTX);
    int maxScaleDelta = 0;
    for (int i = 0; i < 16; ++i) {
        std::int32_t in4[4] = {src[i] * 4, 0, 0, 0};
        std::int32_t mid[4] = {0};
        std::int32_t expect[4] = {0};
        transforms::fwdIdentity4(in4, mid, 0);
        transforms::fwdIdentity4(mid, expect, 0);
        CHECK(got[i] == expect[0]);
        const int d = expect[0] - 8 * src[i];
        const int ad = d < 0 ? -d : d;
        if (ad > maxScaleDelta) maxScaleDelta = ad;
    }
    CHECK(maxScaleDelta <= 1);
}

TEST_CASE("TX2 invIdentity64 scales 5793*4>>12 (inv_transforms.c:2382-2389)") {
    const std::int32_t in[64] = {100, -3};
    std::int32_t got[64] = {0};
    transforms::invIdentity64(in, got, 0);
    CHECK(got[0] == 566);
    CHECK(got[1] == -17);
    CHECK(got[63] == 0);
}

TEST_CASE("TX2 invIdentity32 quadruples (inv_transforms.c:2374-2380)") {
    const std::int32_t in[32] = {100, -3};
    std::int32_t got[32] = {0};
    transforms::invIdentity32(in, got, 0);
    CHECK(got[0] == 400);
    CHECK(got[1] == -12);
    CHECK(got[31] == 0);
}

TEST_CASE("TX2 invIdentity16 scales 5793*2>>12 (inv_transforms.c:2365-2372)") {
    const std::int32_t in[16] = {100, -3, 1, 0, 7, -1, 2, 5, 9, 4, 6, 8, 3, 11, 13, 10};
    std::int32_t got[16] = {0};
    transforms::invIdentity16(in, got, 0);
    CHECK(got[0] == 283);
    CHECK(got[1] == -8);
    CHECK(got[2] == 3);
    CHECK(got[15] == 28);
}

TEST_CASE("TX2 invIdentity8 doubles (inv_transforms.c:2357-2363)") {
    const std::int32_t in[8] = {100, -3, 1, 0, 7, -1, 2, 5};
    std::int32_t got[8] = {0};
    transforms::invIdentity8(in, got, 0);
    CHECK(got[0] == 200);
    CHECK(got[1] == -6);
    CHECK(got[7] == 10);
}

TEST_CASE("TX2 invIdentity4 scales 5793>>12 (inv_transforms.c:2346-2355)") {
    const std::int32_t in[4] = {100, -3, 1, 0};
    std::int32_t got[4] = {0};
    transforms::invIdentity4(in, got, 0);
    CHECK(got[0] == 141);
    CHECK(got[1] == -4);
    CHECK(got[2] == 1);
    CHECK(got[3] == 0);
}

TEST_CASE("TX2 fwdIdentity64 scales 4*5793>>12 (transforms.c:2352-2359)") {
    const std::int32_t in[64] = {100, -3};
    std::int32_t got[64] = {0};
    transforms::fwdIdentity64(in, got, 0);
    // measured: 5793*4*100 = 2317200, +2048 = 2319248, >>12 = 566 (566.22 -> 566);
    // -3: -69516+2048 = -67468 >> 12 = -17 (-16.47 floors to -17)
    CHECK(got[0] == 566);
    CHECK(got[1] == -17);
    CHECK(got[63] == 0);
}

TEST_CASE("TX2 fwdIdentity32 quadruples (transforms.c:2344-2350)") {
    const std::int32_t in[32] = {100, -3};
    std::int32_t got[32] = {0};
    transforms::fwdIdentity32(in, got, 0);
    CHECK(got[0] == 400);
    CHECK(got[1] == -12);
    CHECK(got[31] == 0);
}

TEST_CASE("TX2 fwdIdentity16 scales 2*5793>>12 (transforms.c:2335-2342)") {
    const std::int32_t in[16] = {100, -3, 1, 0, 7, -1, 2, 5, 9, 4, 6, 8, 3, 11, 13, 10};
    std::int32_t got[16] = {0};
    transforms::fwdIdentity16(in, got, 0);
    // measured: 5793*2*100 = 1158600, +2048 = 1160648, >>12 = 283 (283.36 -> 283);
    // -3: -34758+2048 = -32710 >> 12 = -8; 1: 11586+2048 = 13634 >> 12 = 3
    CHECK(got[0] == 283);
    CHECK(got[1] == -8);
    CHECK(got[2] == 3);
    CHECK(got[15] == 28);
}

TEST_CASE("TX2 fwdIdentity8 doubles (no rounding, transforms.c:2327-2333)") {
    const std::int32_t in[8] = {100, -3, 1, 0, 7, -1, 2, 5};
    std::int32_t got[8] = {0};
    transforms::fwdIdentity8(in, got, 0);
    CHECK(got[0] == 200);
    CHECK(got[1] == -6);
    CHECK(got[7] == 10);
}

TEST_CASE("TX2 fwdIdentity4 scales by the reference's 5793>>12 rule") {
    // svt_av1_fidentity4_c (transforms.c:2318-2325): round_shift(in*5793, 12)
    const std::int32_t in[4] = {100, -3, 1, 0};
    std::int32_t got[4] = {0};
    transforms::fwdIdentity4(in, got, 0);
    // measured: 5793*100 = 579300, +2048 = 581348, >>12 = 141 (141.9 rounds 142? 581348/4096 = 141.93 -> 141)
    CHECK(got[0] == 141);
    CHECK(got[1] == -4);
    CHECK(got[2] == 1);
    CHECK(got[3] == 0);
}

TEST_CASE("TX2 flip config per type (get_flip_cfg, inv_transforms.h:139-177)") {
    // (ud_flip, lr_flip) per TxType, verbatim from get_flip_cfg: DCT_DCT..
    // ADST_ADST and the IDTX/one-axis DCT/ADST block are 0/0; FLIPADST_DCT,
    // FLIPADST_FLIPADST and V_FLIPADST flip the vertical axis (ud=1, lr=0);
    // DCT_FLIPADST, ADST_FLIPADST and H_FLIPADST flip the horizontal axis
    // (ud=0, lr=1); FLIPADST_FLIPADST flips both.
    const int expectUd[16] = {0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0};
    const int expectLr[16] = {0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1};
    for (int t = 0; t < 16; ++t) {
        const auto type = static_cast<transforms::TxType>(t);
        transforms::Txfm2dFlipCfg cfg;
        transforms::fwdTransformConfig(type, transforms::TxSize::TX_4X4, cfg);
        CHECK(cfg.udFlip == expectUd[t]);
        CHECK(cfg.lrFlip == expectLr[t]);
    }
}

TEST_CASE("fwd cfg type selection follows vtx/htx per square (ADST rows/cols)") {
    transforms::Txfm2dFlipCfg cfg;
    transforms::fwdTransformConfig(transforms::TxType::ADST_DCT, transforms::TxSize::TX_8X8, cfg);
    CHECK(cfg.txfmTypeCol == transforms::TxfmTypeLs::ADST8);
    CHECK(cfg.txfmTypeRow == transforms::TxfmTypeLs::DCT8);
    CHECK(cfg.stageNumCol == 8);
    CHECK(cfg.stageNumRow == 6);
    transforms::fwdTransformConfig(transforms::TxType::DCT_ADST, transforms::TxSize::TX_16X16, cfg);
    CHECK(cfg.txfmTypeCol == transforms::TxfmTypeLs::DCT16);
    CHECK(cfg.txfmTypeRow == transforms::TxfmTypeLs::ADST16);
    transforms::fwdTransformConfig(transforms::TxType::ADST_ADST, transforms::TxSize::TX_4X4, cfg);
    CHECK(cfg.txfmTypeCol == transforms::TxfmTypeLs::ADST4);
    CHECK(cfg.txfmTypeRow == transforms::TxfmTypeLs::ADST4);
    CHECK(cfg.stageNumCol == 7);
    CHECK(cfg.stageNumRow == 7);
}

// TX1 impulse oracle: compose the landed 1D kernels with hand-typed per-shape
// constants (shifts from the fwd_shift table, kernel sizes from the type
// table, cos bits from fwd_cos_bit_col/row), independent of the ported table
// code under test.
using Txfm1dCB = void (*)(const std::int32_t*, std::int32_t*, int);

struct TxFixture {
    int w;
    int h;
    int shift0;
    int shift1;
    int shift2;
    int colBit;
    int rowBit;
    bool rect1;
    Txfm1dCB colFn;
    Txfm1dCB rowFn;
};

static void impulseExpect(const TxFixture& f, std::int32_t* expect, bool rowImpulse, int amp, int pos) {
    std::int32_t g[64];
    for (int c = 0; c < f.w; ++c) {
        std::int32_t colIn[64];
        for (int r = 0; r < f.h; ++r) {
            const bool hit = rowImpulse ? (r == pos) : true;
            colIn[r] = hit ? (amp * (1 << f.shift0)) : 0;
        }
        f.colFn(colIn, g, f.colBit);
        if (f.shift1 < 0) {
            for (int r = 0; r < f.h; ++r) {
                g[r] = transforms::roundShift(g[r], -f.shift1);
            }
        }
        for (int r = 0; r < f.h; ++r) {
            const bool keep = rowImpulse ? true : (c == pos);
            expect[r * f.w + c] = keep ? g[r] : 0;
        }
    }
    for (int r = 0; r < f.h; ++r) {
        std::int32_t rowIn[64];
        std::int32_t hout[64];
        for (int c = 0; c < f.w; ++c) {
            rowIn[c] = expect[r * f.w + c];
        }
        f.rowFn(rowIn, hout, f.rowBit);
        for (int c = 0; c < f.w; ++c) {
            std::int32_t v = hout[c];
            if (f.shift2 < 0) {
                v = transforms::roundShift(v, -f.shift2);
            }
            if (f.rect1) {
                v = transforms::roundShift(static_cast<std::int64_t>(v) * 5793, 12);
            }
            expect[r * f.w + c] = v;
        }
    }
}

// DCT_DCT fixture for a shape: col/row kernels and bits hand-typed per shape
static TxFixture dctFixture(int w, int h, int s0, int s1, int s2, int cb, int rb, bool rect1, Txfm1dCB colFn,
                            Txfm1dCB rowFn) {
    TxFixture f = {w, h, s0, s1, s2, cb, rb, rect1, colFn, rowFn};
    return f;
}

static void checkImpulse(const TxFixture& f, bool rowImpulse, int amp, int pos, const std::int32_t* got) {
    std::int32_t expect[64 * 64] = {0};
    impulseExpect(f, expect, rowImpulse, amp, pos);
    int bad = 0;
    int firstBad = -1;
    for (int i = 0; i < f.w * f.h; ++i) {
        if (got[i] != expect[i]) {
            if (firstBad < 0) {
                firstBad = i;
            }
            ++bad;
        }
    }
    CHECK_MESSAGE(bad == 0, "impulse mismatch: " << bad << " cells, first at " << firstBad << " got "
                                                 << (firstBad >= 0 ? got[firstBad] : 0) << " expect "
                                                 << (firstBad >= 0 ? expect[firstBad] : 0));
}

TEST_CASE("fwdTxfm2d4x8 impulse-row matches the 1D composition oracle") {
    const int w = 4;
    std::int16_t input[4 * 8] = {0};
    for (int c = 0; c < w; ++c) {
        input[2 * w + c] = 37;  // impulse row r=2
    }
    std::int32_t got[4 * 8] = {0};
    transforms::fwdTxfm2d4x8(input, got, w, transforms::TxType::DCT_DCT);
    // 4x8: shift {2,-1,0} (transforms.c:127), col DCT8@13 (type_ls[1] x
    // fwd_cos_bit_col[0][1]), row DCT4@13, 2:1 sqrt2
    const TxFixture f = dctFixture(4, 8, 2, -1, 0, 13, 13, true, transforms::fdct8B, transforms::fdct4B);
    checkImpulse(f, true, 37, 2, got);
}

TEST_CASE("fwdTxfm2d4x8 impulse-column matches the 1D composition oracle") {
    const int w = 4;
    std::int16_t input[4 * 8] = {0};
    for (int r = 0; r < 8; ++r) {
        input[r * w + 1] = 29;  // impulse column c=1
    }
    std::int32_t got[4 * 8] = {0};
    transforms::fwdTxfm2d4x8(input, got, w, transforms::TxType::DCT_DCT);
    const TxFixture f = dctFixture(4, 8, 2, -1, 0, 13, 13, true, transforms::fdct8B, transforms::fdct4B);
    checkImpulse(f, false, 29, 1, got);
}

TEST_CASE("fwdTxfm2d8x4 impulse-row and impulse-column match the oracle") {
    const int w = 8;
    // 8x4: shift {2,-1,0} (transforms.c:128), col DCT4@13 (type_ls[0] x
    // fwd_cos_bit_col[1][0]), row DCT8@13 (fwd_cos_bit_row[1][0]), 2:1 sqrt2
    const TxFixture f = dctFixture(8, 4, 2, -1, 0, 13, 13, true, transforms::fdct4B, transforms::fdct8B);
    {
        std::int16_t input[8 * 4] = {0};
        for (int c = 0; c < w; ++c) {
            input[1 * w + c] = 41;
        }
        std::int32_t got[8 * 4] = {0};
        transforms::fwdTxfm2d8x4(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, true, 41, 1, got);
    }
    {
        std::int16_t input[8 * 4] = {0};
        for (int r = 0; r < 4; ++r) {
            input[r * w + 5] = 23;
        }
        std::int32_t got[8 * 4] = {0};
        transforms::fwdTxfm2d8x4(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, false, 23, 5, got);
    }
}

TEST_CASE("dispatch switch wiring agrees with the named drivers") {
    const int w = 16;
    const int h = 8;
    std::int16_t src[16 * 8];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>((r * 7 + c * 3) % 100);
        }
    }
    std::int32_t viaSwitch[16 * 8] = {0};
    std::int32_t viaDriver[16 * 8] = {0};
    transforms::fwdTxfm2d(transforms::TxSize::TX_16X8, src, viaSwitch, w, transforms::TxType::DCT_DCT);
    transforms::fwdTxfm2d16x8(src, viaDriver, w, transforms::TxType::DCT_DCT);
    int diffs = 0;
    for (int i = 0; i < w * h; ++i) {
        if (viaSwitch[i] != viaDriver[i]) {
            ++diffs;
        }
    }
    CHECK(diffs == 0);
    std::uint8_t dstA[16 * 8] = {0};
    std::uint8_t dstB[16 * 8] = {0};
    transforms::invTxfm2dAdd(transforms::TxSize::TX_16X8, viaSwitch, dstA, w, transforms::TxType::DCT_DCT);
    transforms::invTxfm2dAdd16x8(viaSwitch, dstB, w, transforms::TxType::DCT_DCT);
    int byteDiffs = 0;
    for (int i = 0; i < w * h; ++i) {
        if (dstA[i] != dstB[i]) {
            ++byteDiffs;
        }
    }
    CHECK(byteDiffs == 0);
}

TEST_CASE("fwdTxfm2d8x4 ADST_DCT differs from DCT_DCT and roundtrips") {
    const int w = 8;
    const int h = 4;
    std::int16_t src[8 * 4];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>((r * 17 + c * 7) % 120);
        }
    }
    std::int32_t dctCoeffs[8 * 4] = {0};
    std::int32_t adstCoeffs[8 * 4] = {0};
    transforms::fwdTxfm2d8x4(src, dctCoeffs, w, transforms::TxType::DCT_DCT);
    transforms::fwdTxfm2d8x4(src, adstCoeffs, w, transforms::TxType::ADST_DCT);
    int diffs = 0;
    for (int i = 0; i < w * h; ++i) {
        if (dctCoeffs[i] != adstCoeffs[i]) {
            ++diffs;
        }
    }
    // the type table must reach the 1D kernel selection: ADST_DCT replaces the
    // column DCT4 with fadst4 (vtx_tab[1] = ADST_1D, inv_transforms.h:45-62)
    CHECK(diffs > 0);
    std::uint8_t dst[8 * 4] = {0};
    transforms::invTxfm2dAdd8x4(adstCoeffs, dst, w, transforms::TxType::ADST_DCT);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    CHECK(maxDelta == 0);
}

TEST_CASE("fwdTxfm2d4x16 ADST_ADST exercises fadst16B col and fadst4B row at 12") {
    const int w = 4;
    const int h = 16;
    std::int16_t src[4 * 16];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>((r * 9 + c * 13) % 120);
        }
    }
    std::int32_t dctCoeffs[4 * 16] = {0};
    std::int32_t adstCoeffs[4 * 16] = {0};
    transforms::fwdTxfm2d4x16(src, dctCoeffs, w, transforms::TxType::DCT_DCT);
    transforms::fwdTxfm2d4x16(src, adstCoeffs, w, transforms::TxType::ADST_ADST);
    int diffs = 0;
    for (int i = 0; i < w * h; ++i) {
        if (dctCoeffs[i] != adstCoeffs[i]) {
            ++diffs;
        }
    }
    CHECK(diffs > 0);
    std::uint8_t dst[4 * 16] = {0};
    transforms::invTxfm2dAdd4x16(adstCoeffs, dst, w, transforms::TxType::ADST_ADST);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    CHECK(maxDelta == 0);
}

TEST_CASE("TX2 IDTX cfg stage ranges follow the fidtx range_mult2 tables") {
    // set_fwd_txfm_non_scale_range (transforms.c:2471-2492) over the fidtx
    // tables: col[i] = (fidtxN_range_mult2[i]+1)>>1; row[i] =
    // (colLast + fidtxRow[i] + 1)>>1. At TX_16X16 both passes are IDENTITY16
    // (fidtx16 {3}): col[0] = 2, row[0] = (3+3+1)>>1 = 3. (Mutation target:
    // a corrupted fidtx16 entry moves these.)
    transforms::Txfm2dFlipCfg cfg;
    transforms::fwdTransformConfig(transforms::TxType::IDTX, transforms::TxSize::TX_16X16, cfg);
    CHECK(cfg.txfmTypeCol == transforms::TxfmTypeLs::IDENTITY16);
    CHECK(cfg.txfmTypeRow == transforms::TxfmTypeLs::IDENTITY16);
    CHECK(cfg.stageNumCol == 1);
    CHECK(cfg.stageNumRow == 1);
    CHECK(cfg.stageRangeCol[0] == 2);
    CHECK(cfg.stageRangeRow[0] == 3);
}

TEST_CASE("TX2 FLIPADST_DCT roundtrip delta equals the ADST_DCT roundtrip delta") {
    // the flip pair cancels: fwd reads rows bottom-up (ud_flip), inv writes
    // rows bottom-up - the roundtrip loss is the ADST_DCT one (measured equal,
    // both 0 on this ramp)
    const int w = 4;
    const int h = 4;
    std::int16_t src[w * h];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>(r * 9 + c * 5);
        }
    }
    std::int32_t coeffs[w * h] = {0};
    transforms::fwdTxfm2d4x4(src, coeffs, w, transforms::TxType::FLIPADST_DCT);
    std::uint8_t dst[w * h] = {0};
    transforms::invTxfm2dAdd4x4(coeffs, dst, w, transforms::TxType::FLIPADST_DCT);
    int maxFlip = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxFlip) maxFlip = ad;
    }
    std::int32_t coeffs2[w * h] = {0};
    transforms::fwdTxfm2d4x4(src, coeffs2, w, transforms::TxType::ADST_DCT);
    std::uint8_t dst2[w * h] = {0};
    transforms::invTxfm2dAdd4x4(coeffs2, dst2, w, transforms::TxType::ADST_DCT);
    int maxAdst = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst2[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxAdst) maxAdst = ad;
    }
    CHECK(maxFlip == maxAdst);
    CHECK(maxFlip == 0);
}

TEST_CASE("TX2 V_FLIPADST/H_FLIPADST = single-axis flipped one-axis transforms") {
    // V_FLIPADST: vtx FLIPADST_1D (col = ADST kernel), htx IDTX_1D (row =
    // fidentity4), ud_flip = 1 (rows read bottom-up); H_FLIPADST: vtx IDTX_1D,
    // htx FLIPADST_1D (row = ADST kernel), lr_flip = 1 (intermediate columns
    // written right-to-left). Impulse fixtures through the landed 1D kernels.
    const int w = 4;
    const int h = 4;
    // impulse row r=2, amp 37: V_FLIPADST - the flip moves it to r=1 before
    // the column pass
    std::int16_t rowImp[w * h] = {0};
    for (int c = 0; c < w; ++c) {
        rowImp[2 * w + c] = 37;
    }
    std::int32_t gotV[w * h] = {0};
    transforms::fwdTxfm2d4x4(rowImp, gotV, w, transforms::TxType::V_FLIPADST);
    std::int32_t colIn[4];
    std::int32_t colOut[4];
    for (int r = 0; r < h; ++r) {
        colIn[r] = (r == h - 1 - 2 ? 37 : 0) * 4;  // flipped read, then <<2
    }
    transforms::fadst4(colIn, colOut);  // col pass, shift[1] = 0
    int bad = 0;
    for (int r = 0; r < h; ++r) {
        std::int32_t rowScaled[4] = {colOut[r], colOut[r], colOut[r], colOut[r]};
        std::int32_t rowOut[4] = {0};
        transforms::fwdIdentity4(rowScaled, rowOut, 0);  // row pass, shift[2] = 0
        for (int c = 0; c < w; ++c) {
            if (gotV[r * w + c] != rowOut[c]) ++bad;
        }
    }
    CHECK_MESSAGE(bad == 0, "V_FLIPADST impulse-row mismatch: " << bad << " cells");
    // impulse column c=1, amp 29: H_FLIPADST - the col pass is identity, then
    // the intermediate column lands flipped at w-1-1 = 2, then the row-ADST
    std::int16_t colImp[w * h] = {0};
    for (int r = 0; r < h; ++r) {
        colImp[r * w + 1] = 29;
    }
    std::int32_t gotH[w * h] = {0};
    transforms::fwdTxfm2d4x4(colImp, gotH, w, transforms::TxType::H_FLIPADST);
    bad = 0;
    std::int32_t one[4] = {29 * 4, 0, 0, 0};
    std::int32_t scaled[4] = {0};
    transforms::fwdIdentity4(one, scaled, 0);  // col pass = identity kernel
    for (int r = 0; r < h; ++r) {
        std::int32_t rowIn[4] = {0, 0, scaled[0], 0};  // flipped write: column 2
        std::int32_t rowOut[4] = {0};
        transforms::fadst4(rowIn, rowOut);
        for (int c = 0; c < w; ++c) {
            if (gotH[r * w + c] != rowOut[c]) ++bad;
        }
    }
    CHECK_MESSAGE(bad == 0, "H_FLIPADST impulse-column mismatch: " << bad << " cells");
}

TEST_CASE("TX2 V_DCT/H_DCT = DCT on one axis, identity on the other") {
    // V_DCT: vtx DCT_1D (col pass), htx IDTX_1D (row pass = fidentity4);
    // H_DCT: vtx IDTX_1D (col pass = fidentity4), htx DCT_1D. Impulse fixtures
    // through the landed 1D kernels + the exported identity kernel.
    const int w = 4;
    const int h = 4;
    // impulse row r=2, amp 37: V_DCT
    std::int16_t rowImp[w * h] = {0};
    for (int c = 0; c < w; ++c) {
        rowImp[2 * w + c] = 37;
    }
    std::int32_t gotV[w * h] = {0};
    transforms::fwdTxfm2d4x4(rowImp, gotV, w, transforms::TxType::V_DCT);
    std::int32_t colIn[4];
    std::int32_t colOut[4];
    for (int r = 0; r < h; ++r) {
        colIn[r] = (r == 2 ? 37 : 0) * 4;  // shift[0] = 2 -> <<2
    }
    transforms::fdct4(colIn, colOut);  // shift[1] = 0
    int bad = 0;
    for (int r = 0; r < h; ++r) {
        std::int32_t rowScaled[4] = {colOut[r], colOut[r], colOut[r], colOut[r]};
        std::int32_t rowOut[4] = {0};
        transforms::fwdIdentity4(rowScaled, rowOut, 0);  // row pass, shift[2] = 0
        for (int c = 0; c < w; ++c) {
            if (gotV[r * w + c] != rowOut[c]) ++bad;
        }
    }
    CHECK_MESSAGE(bad == 0, "V_DCT impulse-row mismatch: " << bad << " cells");
    // impulse column c=1, amp 29: H_DCT
    std::int16_t colImp[w * h] = {0};
    for (int r = 0; r < h; ++r) {
        colImp[r * w + 1] = 29;
    }
    std::int32_t gotH[w * h] = {0};
    transforms::fwdTxfm2d4x4(colImp, gotH, w, transforms::TxType::H_DCT);
    bad = 0;
    std::int32_t colScaled[4];
    for (int r = 0; r < h; ++r) {
        std::int32_t one[4] = {29 * 4, 0, 0, 0};  // the impulse column: every row feeds 29
        std::int32_t scaled[4] = {0};
        transforms::fwdIdentity4(one, scaled, 0);
        colScaled[r] = scaled[0];
    }
    for (int r = 0; r < h; ++r) {
        std::int32_t rowIn[4] = {0, colScaled[0], 0, 0};  // buf row: impulse at column 1
        std::int32_t rowOut[4] = {0};
        transforms::fdct4(rowIn, rowOut);
        for (int c = 0; c < w; ++c) {
            if (gotH[r * w + c] != rowOut[c]) ++bad;
        }
    }
    CHECK_MESSAGE(bad == 0, "H_DCT impulse-column mismatch: " << bad << " cells");
}

TEST_CASE("TX2 dispatch admissible set over every (TxSize, TxType) pair") {
    // enumerated from the reference: the fwd dispatch (transforms.c:3185-3360)
    // asserts DCT_DCT (IDTX parked) at TX_32X16/16X32/32X8/8X32/32X64/64X32/
    // 64X16/16X64 and at the squares TX_32X32/TX_64X64, so the fwd gate is the
    // type table (av1_txfm_type_ls, inv_transforms.h:191-196) plus those
    // asserts; TX2 widens the assert columns to the 16-type enum: IDTX joins
    // the (DCT_DCT || IDTX) asserts at TX_32X16/16X32/32X8/8X32/32X32, the six
    // one-axis types join TX_32X32 through the transform_two_d_32x32_c
    // fallback branch (:3329-3331, live at the default build's
    // CONFIG_ENABLE_NON_DCT_LARGE_TX=1, EbConfigMacros.h:246), and the small
    // shapes (both axes <= 16) carry no assert, so all 16 cfg-valid types are
    // admitted. The inv wrappers (inv_transforms.c:2901-3137) assert only at
    // TX_32X32 (DCT_DCT or IDTX switch, :2924-2932) and TX_64X64
    // (DCT_DCT, :2940), so the inv gate elsewhere is the type table alone:
    // ADST/FLIPADST on a 64 axis is INVALID, the one-axis and IDTX types are
    // cfg-valid there. The first four columns of every row are the landed
    // TX1 table, byte-identical. Order within each row: DCT_DCT, ADST_DCT,
    // DCT_ADST, ADST_ADST, FLIPADST_DCT, DCT_FLIPADST, FLIPADST_FLIPADST,
    // ADST_FLIPADST, FLIPADST_ADST, IDTX, V_DCT, H_DCT, V_ADST, H_ADST,
    // V_FLIPADST, H_FLIPADST.
    const bool fwdSupported[19][16] = {
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 4x4
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 8x8
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 16x16
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1},  // 32x32
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},  // 64x64
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 4x8
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 8x4
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 8x16
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 16x8
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},  // 16x32
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},  // 32x16
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},  // 32x64
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},  // 64x32
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 4x16
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 16x4
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},  // 8x32
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},  // 32x8
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},  // 16x64
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},  // 64x16
    };
    const bool invSupported[19][16] = {
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 4x4
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 8x8
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 16x16
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0},  // 32x32
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},  // 64x64
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 4x8
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 8x4
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 8x16
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 16x8
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 16x32
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 32x16
        {1, 0, 1, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 0, 1},  // 32x64
        {1, 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 0},  // 64x32
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 4x16
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 16x4
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 8x32
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},  // 32x8
        {1, 0, 1, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 0, 1},  // 16x64
        {1, 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 0},  // 64x16
    };
    int fwdTrue = 0;
    int invTrue = 0;
    for (int s = 0; s < 19; ++s) {
        for (int t = 0; t < 16; ++t) {
            const auto size = static_cast<transforms::TxSize>(s);
            const auto type = static_cast<transforms::TxType>(t);
            CHECK(transforms::fwdTxfmSupported(size, type) == (fwdSupported[s][t] != 0));
            CHECK(transforms::invTxfmSupported(size, type) == (invSupported[s][t] != 0));
            fwdTrue += fwdSupported[s][t];
            invTrue += invSupported[s][t];
        }
    }
    CHECK(fwdTrue == 165);
    CHECK(invTrue == 243);
}

TEST_CASE("invTxfm2dAdd16x64 roundtrips the packed 16x32 coefficient block") {
    const int w = 16;
    const int h = 64;
    // the facade consumes the COMPACTED 16x32 block (max_eob 512) and expands
    // it into the 16x64 coefficient area (top half kept, bottom zeroed)
    std::int16_t src[16 * 64];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>(r / 4 + c / 2);  // smooth
        }
    }
    std::int32_t coeffs[16 * 64] = {0};
    transforms::fwdTxfm2d16x64(src, coeffs, w, transforms::TxType::DCT_DCT);
    std::int32_t packed[16 * 32];
    for (int i = 0; i < 16 * 32; ++i) {
        packed[i] = coeffs[i];
    }
    std::uint8_t dst[16 * 64] = {0};
    transforms::invTxfm2dAdd16x64(packed, dst, w, transforms::TxType::DCT_DCT);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    // measured on the smooth ramp: fwd sum {0,-2,0} = -2 plus the 16x32
    // spectrum truncation of the packed facade, 1 LSB
    CHECK(maxDelta == 1);
}

TEST_CASE("invTxfm2dAdd64x16 roundtrips the packed 32x16 coefficient block") {
    const int w = 64;
    const int h = 16;
    // the facade consumes the COMPACTED 32x16 block (max_eob 512): each input
    // row is 32 wide, expanded into the left half of a 64-wide row
    std::int16_t src[64 * 16];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>(r + c / 4);  // smooth
        }
    }
    std::int32_t coeffs[64 * 16] = {0};
    transforms::fwdTxfm2d64x16(src, coeffs, w, transforms::TxType::DCT_DCT);
    std::int32_t packed[32 * 16];
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 32; ++c) {
            packed[r * 32 + c] = coeffs[r * 64 + c];
        }
    }
    std::uint8_t dst[64 * 16] = {0};
    transforms::invTxfm2dAdd64x16(packed, dst, w, transforms::TxType::DCT_DCT);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    // measured on the smooth ramp: fwd sum {2,-4,0} = -2 plus the 32x16
    // spectrum truncation of the packed facade, 1 LSB
    CHECK(maxDelta == 1);
}

TEST_CASE("invTxfm2dAdd32x64 roundtrips the packed 32x32 coefficient block") {
    const int w = 32;
    const int h = 64;
    // the facade consumes the COMPACTED 32x32 block (max_eob 1024) and expands
    // it into the 32x64 coefficient area (top half kept, bottom zeroed)
    std::int16_t src[32 * 64];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>(r / 2 + c);  // smooth, no wrap
        }
    }
    std::int32_t coeffs[32 * 64] = {0};
    transforms::fwdTxfm2d32x64(src, coeffs, w, transforms::TxType::DCT_DCT);
    std::int32_t packed[32 * 32];
    for (int i = 0; i < 32 * 32; ++i) {
        packed[i] = coeffs[i];
    }
    std::uint8_t dst[32 * 64] = {0};
    transforms::invTxfm2dAdd32x64(packed, dst, w, transforms::TxType::DCT_DCT);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    // measured on the smooth ramp: fwd sum {0,-2,-2} = -4 plus the 32x32
    // spectrum truncation of the packed facade, 1 LSB
    CHECK(maxDelta == 1);
}

TEST_CASE("invTxfm2dAdd64x32 roundtrips the packed 32x32 coefficient block") {
    const int w = 64;
    const int h = 32;
    // the facade consumes the COMPACTED 32x32 block (max_eob 1024,
    // inv_transforms.h:129-137) and expands it into the 64x32 coefficient area
    std::int16_t src[64 * 32];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>(r + c / 4);  // smooth, no wrap
        }
    }
    std::int32_t coeffs[64 * 32] = {0};
    transforms::fwdTxfm2d64x32(src, coeffs, w, transforms::TxType::DCT_DCT);
    std::int32_t packed[32 * 32];
    for (int r = 0; r < 32; ++r) {
        for (int c = 0; c < 32; ++c) {
            packed[r * 32 + c] = coeffs[r * 64 + c];
        }
    }
    std::uint8_t dst[64 * 32] = {0};
    transforms::invTxfm2dAdd64x32(packed, dst, w, transforms::TxType::DCT_DCT);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    // measured on the smooth ramp: fwd sum {2,-4,-2} = -4 plus the 32x32
    // spectrum truncation of the packed facade, 1 LSB
    CHECK(maxDelta == 1);
}

TEST_CASE("invTxfm2dAdd32x8 roundtrips a ramp with the measured delta bound") {
    const int w = 32;
    const int h = 8;
    std::int16_t src[32 * 8];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>((r * 3 + c * 7) % 90);
        }
    }
    std::int32_t coeffs[32 * 8] = {0};
    transforms::fwdTxfm2d32x8(src, coeffs, w, transforms::TxType::DCT_DCT);
    std::uint8_t dst[32 * 8] = {0};
    transforms::invTxfm2dAdd32x8(coeffs, dst, w, transforms::TxType::DCT_DCT);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    // measured: fwd sum {2,-2,0} = 0 but the DCT8-col / DCT32-row order loses
    // 1 LSB on this ramp (32x8 is the lossy twin of the exact 8x32)
    CHECK(maxDelta == 1);
}

TEST_CASE("invTxfm2dAdd8x32 roundtrips a ramp with the measured delta bound") {
    const int w = 8;
    const int h = 32;
    std::int16_t src[8 * 32];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>((r * 7 + c * 13) % 90);
        }
    }
    std::int32_t coeffs[8 * 32] = {0};
    transforms::fwdTxfm2d8x32(src, coeffs, w, transforms::TxType::DCT_DCT);
    std::uint8_t dst[8 * 32] = {0};
    transforms::invTxfm2dAdd8x32(coeffs, dst, w, transforms::TxType::DCT_DCT);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    CHECK(maxDelta == 0);
}

TEST_CASE("invTxfm2dAdd16x4 roundtrips a ramp with the measured delta bound") {
    const int w = 16;
    const int h = 4;
    std::int16_t src[16 * 4];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>((r * 9 + c * 5) % 90);
        }
    }
    std::int32_t coeffs[16 * 4] = {0};
    transforms::fwdTxfm2d16x4(src, coeffs, w, transforms::TxType::DCT_DCT);
    std::uint8_t dst[16 * 4] = {0};
    transforms::invTxfm2dAdd16x4(coeffs, dst, w, transforms::TxType::DCT_DCT);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    CHECK(maxDelta == 0);
}

TEST_CASE("invTxfm2dAdd4x16 roundtrips a ramp with the measured delta bound") {
    const int w = 4;
    const int h = 16;
    std::int16_t src[4 * 16];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>((r * 11 + c * 7) % 90);
        }
    }
    std::int32_t coeffs[4 * 16] = {0};
    transforms::fwdTxfm2d4x16(src, coeffs, w, transforms::TxType::DCT_DCT);
    std::uint8_t dst[4 * 16] = {0};
    transforms::invTxfm2dAdd4x16(coeffs, dst, w, transforms::TxType::DCT_DCT);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    // measured: fwd sum {2,-1,0} = 1, lossy by 1 LSB on this ramp
    CHECK(maxDelta == 1);
}

TEST_CASE("invTxfm2dAdd32x16 roundtrips a ramp with the measured delta bound") {
    const int w = 32;
    const int h = 16;
    std::int16_t src[32 * 16];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>((r * 5 + c * 3) % 90);
        }
    }
    std::int32_t coeffs[32 * 16] = {0};
    transforms::fwdTxfm2d32x16(src, coeffs, w, transforms::TxType::DCT_DCT);
    std::uint8_t dst[32 * 16] = {0};
    transforms::invTxfm2dAdd32x16(coeffs, dst, w, transforms::TxType::DCT_DCT);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    // measured: fwd sum {2,-4,0} = -2, lossy by 1 LSB on this ramp
    CHECK(maxDelta == 1);
}

TEST_CASE("invTxfm2dAdd16x32 roundtrips a ramp with the measured delta bound") {
    const int w = 16;
    const int h = 32;
    std::int16_t src[16 * 32];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>((r * 3 + c * 5) % 90);
        }
    }
    std::int32_t coeffs[16 * 32] = {0};
    transforms::fwdTxfm2d16x32(src, coeffs, w, transforms::TxType::DCT_DCT);
    std::uint8_t dst[16 * 32] = {0};
    transforms::invTxfm2dAdd16x32(coeffs, dst, w, transforms::TxType::DCT_DCT);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    // measured: fwd sum {2,-4,0} = -2, lossy by 1 LSB on this ramp
    CHECK(maxDelta == 1);
}

TEST_CASE("invTxfm2dAdd16x8 roundtrips a ramp with the measured delta bound") {
    const int w = 16;
    const int h = 8;
    std::int16_t src[16 * 8];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>((r * 13 + c * 3) % 90);
        }
    }
    std::int32_t coeffs[16 * 8] = {0};
    transforms::fwdTxfm2d16x8(src, coeffs, w, transforms::TxType::DCT_DCT);
    std::uint8_t dst[16 * 8] = {0};
    transforms::invTxfm2dAdd16x8(coeffs, dst, w, transforms::TxType::DCT_DCT);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    // measured: fwd sum {2,-2,0} = 0 but the DCT8-col / DCT16-row order loses
    // 1 LSB on this ramp (16x8 is the lossy twin of the exact 8x16)
    CHECK(maxDelta == 1);
}

TEST_CASE("invTxfm2dAdd8x16 roundtrips a ramp with the measured delta bound") {
    const int w = 8;
    const int h = 16;
    std::int16_t src[8 * 16];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>((r * 3 + c * 9) % 90);
        }
    }
    std::int32_t coeffs[8 * 16] = {0};
    transforms::fwdTxfm2d8x16(src, coeffs, w, transforms::TxType::DCT_DCT);
    std::uint8_t dst[8 * 16] = {0};
    transforms::invTxfm2dAdd8x16(coeffs, dst, w, transforms::TxType::DCT_DCT);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    CHECK(maxDelta == 0);
}

TEST_CASE("invTxfm2dAdd8x4 roundtrips a ramp with the measured delta bound") {
    const int w = 8;
    const int h = 4;
    std::int16_t src[8 * 4];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>((r * 5 + c * 11) % 80);
        }
    }
    std::int32_t coeffs[8 * 4] = {0};
    transforms::fwdTxfm2d8x4(src, coeffs, w, transforms::TxType::DCT_DCT);
    std::uint8_t dst[8 * 4] = {0};
    transforms::invTxfm2dAdd8x4(coeffs, dst, w, transforms::TxType::DCT_DCT);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    CHECK(maxDelta == 0);
}

TEST_CASE("invTxfm2dAdd4x8 roundtrips a ramp with the measured delta bound") {
    const int w = 4;
    const int h = 8;
    std::int16_t src[4 * 8];
    for (int r = 0; r < h; ++r) {
        for (int c = 0; c < w; ++c) {
            src[r * w + c] = static_cast<std::int16_t>((r * 7 + c * 3) % 80);
        }
    }
    std::int32_t coeffs[4 * 8] = {0};
    transforms::fwdTxfm2d4x8(src, coeffs, w, transforms::TxType::DCT_DCT);
    std::uint8_t dst[4 * 8] = {0};
    transforms::invTxfm2dAdd4x8(coeffs, dst, w, transforms::TxType::DCT_DCT);
    int maxDelta = 0;
    for (int i = 0; i < w * h; ++i) {
        const int d = src[i] - static_cast<int>(dst[i]);
        const int ad = d < 0 ? -d : d;
        if (ad > maxDelta) maxDelta = ad;
    }
    CHECK(maxDelta == 0);
}

TEST_CASE("inv cfg identity at TX_8X8 and TX_16X8 (landed driver constants)") {
    transforms::Txfm2dFlipCfg cfg;
    // 8x8: inv_shift_8x8 = {-1,-4} (inv_transforms.c:19), cos 12/12
    transforms::invTransformConfig(transforms::TxType::DCT_DCT, transforms::TxSize::TX_8X8, cfg);
    CHECK(cfg.shift[0] == -1);
    CHECK(cfg.shift[1] == -4);
    CHECK(cfg.cosBitCol == 12);
    CHECK(cfg.cosBitRow == 12);
    CHECK(cfg.txfmTypeCol == transforms::TxfmTypeLs::DCT8);
    CHECK(cfg.txfmTypeRow == transforms::TxfmTypeLs::DCT8);
    CHECK(cfg.udFlip == 0);
    CHECK(cfg.lrFlip == 0);
    // 16x8: inv_shift_16x8 = {-1,-4} (inv_transforms.c:26)
    transforms::invTransformConfig(transforms::TxType::ADST_ADST, transforms::TxSize::TX_16X8, cfg);
    CHECK(cfg.shift[0] == -1);
    CHECK(cfg.shift[1] == -4);
    CHECK(cfg.txfmTypeCol == transforms::TxfmTypeLs::ADST8);
    CHECK(cfg.txfmTypeRow == transforms::TxfmTypeLs::ADST16);
}

TEST_CASE("fwdTxfm2d64x16 impulse-row and impulse-column match the oracle") {
    const int w = 64;
    // 64x16: shift {2,-4,0} (transforms.c:140), col DCT16@13
    // (fwd_cos_bit_col[4][2]), row DCT64@12 (fwd_cos_bit_row[4][2]); 4:1, no
    // sqrt2
    const TxFixture f = dctFixture(64, 16, 2, -4, 0, 13, 12, false, transforms::fdct16B, transforms::fdct64B);
    {
        std::int16_t input[64 * 16] = {0};
        for (int c = 0; c < w; ++c) {
            input[7 * w + c] = 77;
        }
        std::int32_t got[64 * 16] = {0};
        transforms::fwdTxfm2d64x16(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, true, 77, 7, got);
    }
    {
        std::int16_t input[64 * 16] = {0};
        for (int r = 0; r < 16; ++r) {
            input[r * w + 33] = 66;
        }
        std::int32_t got[64 * 16] = {0};
        transforms::fwdTxfm2d64x16(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, false, 66, 33, got);
    }
}

TEST_CASE("fwdTxfm2d16x64 impulse-row and impulse-column match the oracle") {
    const int w = 16;
    // 16x64: shift {0,-2,0} (transforms.c:139), col DCT64@13
    // (fwd_cos_bit_col[2][4]), row DCT16@12 (fwd_cos_bit_row[2][4]); 4:1, no
    // sqrt2
    const TxFixture f = dctFixture(16, 64, 0, -2, 0, 13, 12, false, transforms::fdct64B, transforms::fdct16B);
    {
        std::int16_t input[16 * 64] = {0};
        for (int c = 0; c < w; ++c) {
            input[31 * w + c] = 55;
        }
        std::int32_t got[16 * 64] = {0};
        transforms::fwdTxfm2d16x64(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, true, 55, 31, got);
    }
    {
        std::int16_t input[16 * 64] = {0};
        for (int r = 0; r < 64; ++r) {
            input[r * w + 9] = 42;
        }
        std::int32_t got[16 * 64] = {0};
        transforms::fwdTxfm2d16x64(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, false, 42, 9, got);
    }
}

TEST_CASE("fwdTxfm2d32x8 impulse-row and impulse-column match the oracle") {
    const int w = 32;
    // 32x8: shift {2,-2,0} (transforms.c:138), col DCT8@13
    // (fwd_cos_bit_col[3][1]), row DCT32@12 (fwd_cos_bit_row[3][1]); 4:1, no
    // sqrt2
    const TxFixture f = dctFixture(32, 8, 2, -2, 0, 13, 12, false, transforms::fdct8B, transforms::fdct32B);
    {
        std::int16_t input[32 * 8] = {0};
        for (int c = 0; c < w; ++c) {
            input[6 * w + c] = 88;
        }
        std::int32_t got[32 * 8] = {0};
        transforms::fwdTxfm2d32x8(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, true, 88, 6, got);
    }
    {
        std::int16_t input[32 * 8] = {0};
        for (int r = 0; r < 8; ++r) {
            input[r * w + 17] = 31;
        }
        std::int32_t got[32 * 8] = {0};
        transforms::fwdTxfm2d32x8(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, false, 31, 17, got);
    }
}

TEST_CASE("fwdTxfm2d8x32 impulse-row and impulse-column match the oracle") {
    const int w = 8;
    // 8x32: shift {2,-2,0} (transforms.c:137), col DCT32@12
    // (fwd_cos_bit_col[1][3]), row DCT8@12 (fwd_cos_bit_row[1][3]); 4:1, no
    // sqrt2
    const TxFixture f = dctFixture(8, 32, 2, -2, 0, 12, 12, false, transforms::fdct32B, transforms::fdct8B);
    {
        std::int16_t input[8 * 32] = {0};
        for (int c = 0; c < w; ++c) {
            input[13 * w + c] = 91;
        }
        std::int32_t got[8 * 32] = {0};
        transforms::fwdTxfm2d8x32(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, true, 91, 13, got);
    }
    {
        std::int16_t input[8 * 32] = {0};
        for (int r = 0; r < 32; ++r) {
            input[r * w + 6] = 73;
        }
        std::int32_t got[8 * 32] = {0};
        transforms::fwdTxfm2d8x32(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, false, 73, 6, got);
    }
}

TEST_CASE("fwdTxfm2d16x4 impulse-row and impulse-column match the oracle") {
    const int w = 16;
    // 16x4: shift {2,-1,0} (transforms.c:136), col DCT4@13
    // (fwd_cos_bit_col[2][0]), row DCT16@13 (fwd_cos_bit_row[2][0]); 4:1, no
    // sqrt2
    const TxFixture f = dctFixture(16, 4, 2, -1, 0, 13, 13, false, transforms::fdct4B, transforms::fdct16B);
    {
        std::int16_t input[16 * 4] = {0};
        for (int c = 0; c < w; ++c) {
            input[1 * w + c] = 66;
        }
        std::int32_t got[16 * 4] = {0};
        transforms::fwdTxfm2d16x4(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, true, 66, 1, got);
    }
    {
        std::int16_t input[16 * 4] = {0};
        for (int r = 0; r < 4; ++r) {
            input[r * w + 11] = 84;
        }
        std::int32_t got[16 * 4] = {0};
        transforms::fwdTxfm2d16x4(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, false, 84, 11, got);
    }
}

TEST_CASE("fwdTxfm2d4x16 impulse-row and impulse-column match the oracle") {
    const int w = 4;
    // 4x16: shift {2,-1,0} (transforms.c:135), col DCT16@13
    // (fwd_cos_bit_col[0][2]), row DCT4@12 (fwd_cos_bit_row[0][2]); 4:1 so NO
    // sqrt2 (rect_type = -2, transforms.c:2460 gates on abs == 1)
    const TxFixture f = dctFixture(4, 16, 2, -1, 0, 13, 12, false, transforms::fdct16B, transforms::fdct4B);
    {
        std::int16_t input[4 * 16] = {0};
        for (int c = 0; c < w; ++c) {
            input[9 * w + c] = 57;
        }
        std::int32_t got[4 * 16] = {0};
        transforms::fwdTxfm2d4x16(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, true, 57, 9, got);
    }
    {
        std::int16_t input[4 * 16] = {0};
        for (int r = 0; r < 16; ++r) {
            input[r * w + 3] = 39;
        }
        std::int32_t got[4 * 16] = {0};
        transforms::fwdTxfm2d4x16(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, false, 39, 3, got);
    }
}

TEST_CASE("fwdTxfm2d64x32 impulse-row and impulse-column match the oracle") {
    const int w = 64;
    // 64x32: shift {2,-4,-2} (transforms.c:134), col DCT32@12
    // (fwd_cos_bit_col[4][3]), row DCT64@11 (fwd_cos_bit_row[4][3]), 2:1 sqrt2
    const TxFixture f = dctFixture(64, 32, 2, -4, -2, 12, 11, true, transforms::fdct32B, transforms::fdct64B);
    {
        std::int16_t input[64 * 32] = {0};
        for (int c = 0; c < w; ++c) {
            input[17 * w + c] = 63;
        }
        std::int32_t got[64 * 32] = {0};
        transforms::fwdTxfm2d64x32(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, true, 63, 17, got);
    }
    {
        std::int16_t input[64 * 32] = {0};
        for (int r = 0; r < 32; ++r) {
            input[r * w + 41] = 21;
        }
        std::int32_t got[64 * 32] = {0};
        transforms::fwdTxfm2d64x32(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, false, 21, 41, got);
    }
}

TEST_CASE("fwdTxfm2d32x64 impulse-row and impulse-column match the oracle") {
    const int w = 32;
    // 32x64: shift {0,-2,-2} (transforms.c:133), col DCT64@13
    // (fwd_cos_bit_col[3][4]), row DCT32@11 (fwd_cos_bit_row[3][4]), 2:1 sqrt2
    const TxFixture f = dctFixture(32, 64, 0, -2, -2, 13, 11, true, transforms::fdct64B, transforms::fdct32B);
    {
        std::int16_t input[32 * 64] = {0};
        for (int c = 0; c < w; ++c) {
            input[23 * w + c] = 53;
        }
        std::int32_t got[32 * 64] = {0};
        transforms::fwdTxfm2d32x64(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, true, 53, 23, got);
    }
    {
        std::int16_t input[32 * 64] = {0};
        for (int r = 0; r < 64; ++r) {
            input[r * w + 30] = 71;
        }
        std::int32_t got[32 * 64] = {0};
        transforms::fwdTxfm2d32x64(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, false, 71, 30, got);
    }
}

TEST_CASE("fwdTxfm2d32x16 impulse-row and impulse-column match the oracle") {
    const int w = 32;
    // 32x16: shift {2,-4,0} (transforms.c:132), col DCT16@13
    // (fwd_cos_bit_col[3][2]), row DCT32@13 (fwd_cos_bit_row[3][2]), 2:1 sqrt2
    const TxFixture f = dctFixture(32, 16, 2, -4, 0, 13, 13, true, transforms::fdct16B, transforms::fdct32B);
    {
        std::int16_t input[32 * 16] = {0};
        for (int c = 0; c < w; ++c) {
            input[11 * w + c] = 47;
        }
        std::int32_t got[32 * 16] = {0};
        transforms::fwdTxfm2d32x16(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, true, 47, 11, got);
    }
    {
        std::int16_t input[32 * 16] = {0};
        for (int r = 0; r < 16; ++r) {
            input[r * w + 21] = 15;
        }
        std::int32_t got[32 * 16] = {0};
        transforms::fwdTxfm2d32x16(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, false, 15, 21, got);
    }
}

TEST_CASE("fwdTxfm2d16x32 impulse-row and impulse-column match the oracle") {
    const int w = 16;
    // 16x32: shift {2,-4,0} (transforms.c:131), col DCT32@12
    // (fwd_cos_bit_col[2][3]), row DCT16@13 (fwd_cos_bit_row[2][3]), 2:1 sqrt2
    const TxFixture f = dctFixture(16, 32, 2, -4, 0, 12, 13, true, transforms::fdct32B, transforms::fdct16B);
    {
        std::int16_t input[16 * 32] = {0};
        for (int c = 0; c < w; ++c) {
            input[7 * w + c] = 19;
        }
        std::int32_t got[16 * 32] = {0};
        transforms::fwdTxfm2d16x32(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, true, 19, 7, got);
    }
    {
        std::int16_t input[16 * 32] = {0};
        for (int r = 0; r < 32; ++r) {
            input[r * w + 13] = 61;
        }
        std::int32_t got[16 * 32] = {0};
        transforms::fwdTxfm2d16x32(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, false, 61, 13, got);
    }
}

TEST_CASE("fwdTxfm2d16x8 impulse-row and impulse-column match the oracle") {
    const int w = 16;
    // 16x8: shift {2,-2,0} (transforms.c:130), col DCT8@13
    // (fwd_cos_bit_col[2][1]), row DCT16@13 (fwd_cos_bit_row[2][1]), 2:1 sqrt2
    const TxFixture f = dctFixture(16, 8, 2, -2, 0, 13, 13, true, transforms::fdct8B, transforms::fdct16B);
    {
        std::int16_t input[16 * 8] = {0};
        for (int c = 0; c < w; ++c) {
            input[3 * w + c] = 44;
        }
        std::int32_t got[16 * 8] = {0};
        transforms::fwdTxfm2d16x8(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, true, 44, 3, got);
    }
    {
        std::int16_t input[16 * 8] = {0};
        for (int r = 0; r < 8; ++r) {
            input[r * w + 9] = 27;
        }
        std::int32_t got[16 * 8] = {0};
        transforms::fwdTxfm2d16x8(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, false, 27, 9, got);
    }
}

TEST_CASE("fwdTxfm2d8x16 impulse-row and impulse-column match the oracle") {
    const int w = 8;
    // 8x16: shift {2,-2,0} (transforms.c:129), col DCT16@13
    // (fwd_cos_bit_col[1][2]), row DCT8@13 (fwd_cos_bit_row[1][2]), 2:1 sqrt2
    const TxFixture f = dctFixture(8, 16, 2, -2, 0, 13, 13, true, transforms::fdct16B, transforms::fdct8B);
    {
        std::int16_t input[8 * 16] = {0};
        for (int c = 0; c < w; ++c) {
            input[5 * w + c] = 33;
        }
        std::int32_t got[8 * 16] = {0};
        transforms::fwdTxfm2d8x16(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, true, 33, 5, got);
    }
    {
        std::int16_t input[8 * 16] = {0};
        for (int r = 0; r < 16; ++r) {
            input[r * w + 2] = 51;
        }
        std::int32_t got[8 * 16] = {0};
        transforms::fwdTxfm2d8x16(input, got, w, transforms::TxType::DCT_DCT);
        checkImpulse(f, false, 51, 2, got);
    }
}
