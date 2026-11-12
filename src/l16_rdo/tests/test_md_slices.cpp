// l16_rdo MD1 tests - the A4 mode-decision pure-arithmetic surface.
// The SSIM family (mode_decision.c:4238-4490 + enc_dec_process.c:638-671),
// the hbd psy chain (ac_bias.c:103-144 + C_DEFAULT/picture_operators_c.c:
// 176-262) and the MV/ME helpers (mode_decision.c:71-310 + :739-846).
// Independent oracles transcribe the reference arithmetic; every expectation
// cites the reference line it derives from. Integer outputs pinned exactly;
// double outputs compared bit-exactly against the test-side transcription
// (the same double expression, so the port and the oracle must agree bit for
// bit).
#include <doctest.h>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>
#include "rate_est.h"
#include "md_slices.h"

// The port's types at namespace scope (the l7 test-file convention).
using l16_rdo::similarity;
using l16_rdo::ssim4x4;
using l16_rdo::ssim8x8;
using l16_rdo::ssim4x4Hbd;
using l16_rdo::ssim8x8Hbd;
using l16_rdo::ssim8x8Blocks;
using l16_rdo::ssim4x4Blocks;
using l16_rdo::ssim;
using l16_rdo::ssim8x8BlocksHbd;
using l16_rdo::ssim4x4BlocksHbd;
using l16_rdo::ssimHbd;
using l16_rdo::spatialFullDistortionSsimKernel;
using l16_rdo::hadamardHighbdCol8FirstPass;
using l16_rdo::hadamardHighbdCol8SecondPass;
using l16_rdo::highbdHadamard8x8;
using l16_rdo::psyDistortionHbd;
using l16_rdo::psyDistortion;
using l16_rdo::checkMvValidity;
using l16_rdo::isLosslessSegment;
using l16_rdo::isInterintraAllowed;
using l16_rdo::haveNewmvInInterMode;
using l16_rdo::getMaxDrlIndex;
using l16_rdo::getMeBlockOffset;
using l16_rdo::isMeDataPresent;
using l16_rdo::mvIsAlreadyInjected;
using l16_rdo::isValidUnipredRef;
using l16_rdo::isValidBipredRef;
using l16_rdo::RefPruningCtrls;
using l16_rdo::RefFilteringRes;
using l16_rdo::MeCandidate;
using l16_rdo::MeSbResults;
using l16_rdo::InjectedMvCarrier;
using l16_rdo::BLOCK_4X4;
using l16_rdo::BLOCK_4X8;
using l16_rdo::BLOCK_8X4;
using l16_rdo::BLOCK_8X8;
using l16_rdo::BLOCK_16X8;
using l16_rdo::BLOCK_16X16;
using l16_rdo::BLOCK_32X32;
using l16_rdo::BLOCK_32X64;
using l16_rdo::BLOCK_64X64;
using l16_rdo::BLOCK_128X128;
using l16_rdo::NEWMV;
using l16_rdo::NEW_NEWMV;
using l16_rdo::NEAREST_NEWMV;
using l16_rdo::NEW_NEARESTMV;
using l16_rdo::NEAR_NEWMV;
using l16_rdo::NEW_NEARMV;
using l16_rdo::NEARESTMV;
using l16_rdo::NEARMV;
using l16_rdo::GLOBALMV;
using l16_rdo::NEAREST_NEARESTMV;
using l16_rdo::NEAR_NEARMV;
using l16_rdo::GLOBAL_GLOBALMV;
using l16_rdo::DC_PRED;

// ---------------------------------------------------------------------------
// Independent oracles (test-side transcriptions of the reference arithmetic,
// never shared with the port).
// ---------------------------------------------------------------------------

// The cc1/cc2 family (enc_dec_process.c:638-643, verbatim).
static const std::int64_t kCc1 = 26634;      // (64^2*(.01*255)^2
static const std::int64_t kCc2 = 239708;     // (64^2*(.03*255)^2
static const std::int64_t kCc1_10 = 428658;  // (64^2*(.01*1023)^2
static const std::int64_t kCc2_10 = 3857925; // (64^2*(.03*1023)^2
static const std::int64_t kCc1_12 = 6868593; // (64^2*(.01*4095)^2
static const std::int64_t kCc2_12 = 61817334; // (64^2*(.03*4095)^2

// svt_aom_similarity (enc_dec_process.c:645-671), test-side transcription.
static double oracleSimilarity(std::uint32_t sumS, std::uint32_t sumR, std::uint32_t sumSqS,
                               std::uint32_t sumSqR, std::uint32_t sumSxr, std::int32_t count,
                               std::uint32_t bd) {
    std::int64_t c1, c2;
    if (bd == 8) {
        c1 = (kCc1 * count * count) >> 12;
        c2 = (kCc2 * count * count) >> 12;
    } else if (bd == 10) {
        c1 = (kCc1_10 * count * count) >> 12;
        c2 = (kCc2_10 * count * count) >> 12;
    } else if (bd == 12) {
        c1 = (kCc1_12 * count * count) >> 12;
        c2 = (kCc2_12 * count * count) >> 12;
    } else {
        c1 = c2 = 0;
    }
    const double ssimN = (2.0 * sumS * sumR + c1) * (2.0 * count * sumSxr - 2.0 * sumS * sumR + c2);
    const double ssimD = ((double)sumS * sumS + (double)sumR * sumR + c1) *
        ((double)count * sumSqS - (double)sumS * sumS + (double)count * sumSqR - (double)sumR * sumR + c2);
    return ssimN / ssimD;
}

// svt_ssim_4x4_c (mode_decision.c:4238-4261), test-side transcription: the
// five per-block integer sums over a 4x4 window, then similarity(.., 16, 8).
static double oracleSsim4x4(const std::uint8_t* s, std::uint32_t sp, const std::uint8_t* r,
                             std::uint32_t rp) {
    std::uint32_t sumS = 0, sumR = 0, sumSqS = 0, sumSqR = 0, sumSxr = 0;
    for (std::uint32_t i = 0; i < 4; ++i) {
        for (std::uint32_t j = 0; j < 4; ++j) {
            sumS += s[j];
            sumR += r[j];
            sumSqS += (std::uint32_t)s[j] * s[j];
            sumSqR += (std::uint32_t)r[j] * r[j];
            sumSxr += (std::uint32_t)s[j] * r[j];
        }
        s += sp;
        r += rp;
    }
    return oracleSimilarity(sumS, sumR, sumSqS, sumSqR, sumSxr, 16, 8);
}

// svt_ssim_8x8_c (mode_decision.c:4263-4289), test-side transcription.
static double oracleSsim8x8(const std::uint8_t* s, std::uint32_t sp, const std::uint8_t* r,
                             std::uint32_t rp) {
    std::uint32_t sumS = 0, sumR = 0, sumSqS = 0, sumSqR = 0, sumSxr = 0;
    for (std::uint32_t i = 0; i < 8; ++i) {
        for (std::uint32_t j = 0; j < 8; ++j) {
            sumS += s[j];
            sumR += r[j];
            sumSqS += (std::uint32_t)s[j] * s[j];
            sumSqR += (std::uint32_t)r[j] * r[j];
            sumSxr += (std::uint32_t)s[j] * r[j];
        }
        s += sp;
        r += rp;
    }
    return oracleSimilarity(sumS, sumR, sumSqS, sumSqR, sumSxr, 64, 8);
}

// svt_ssim_4x4_hbd_c (mode_decision.c:4291-4314), test-side transcription:
// the same five sums over uint16 samples, then similarity(.., 16, 10).
static double oracleSsim4x4Hbd(const std::uint16_t* s, std::uint32_t sp, const std::uint16_t* r,
                                std::uint32_t rp) {
    std::uint32_t sumS = 0, sumR = 0, sumSqS = 0, sumSqR = 0, sumSxr = 0;
    for (std::uint32_t i = 0; i < 4; ++i) {
        for (std::uint32_t j = 0; j < 4; ++j) {
            sumS += s[j];
            sumR += r[j];
            sumSqS += (std::uint32_t)s[j] * s[j];
            sumSqR += (std::uint32_t)r[j] * r[j];
            sumSxr += (std::uint32_t)s[j] * r[j];
        }
        s += sp;
        r += rp;
    }
    return oracleSimilarity(sumS, sumR, sumSqS, sumSqR, sumSxr, 16, 10);
}

// svt_ssim_8x8_hbd_c (mode_decision.c:4316-4339), test-side transcription.
static double oracleSsim8x8Hbd(const std::uint16_t* s, std::uint32_t sp, const std::uint16_t* r,
                                std::uint32_t rp) {
    std::uint32_t sumS = 0, sumR = 0, sumSqS = 0, sumSqR = 0, sumSxr = 0;
    for (std::uint32_t i = 0; i < 8; ++i) {
        for (std::uint32_t j = 0; j < 8; ++j) {
            sumS += s[j];
            sumR += r[j];
            sumSqS += (std::uint32_t)s[j] * s[j];
            sumSqR += (std::uint32_t)r[j] * r[j];
            sumSxr += (std::uint32_t)s[j] * r[j];
        }
        s += sp;
        r += rp;
    }
    return oracleSimilarity(sumS, sumR, sumSqS, sumSqR, sumSxr, 64, 10);
}

// The hadamard_highbd_col8_first_pass (picture_operators_c.c:176-203),
// test-side transcription.
static void oracleHadamardCol8FirstPass(const std::int16_t* srcDiff, std::ptrdiff_t srcStride,
                                         std::int16_t* coeff) {
    const std::int16_t b0 = (std::int16_t)(srcDiff[0 * srcStride] + srcDiff[1 * srcStride]);
    const std::int16_t b1 = (std::int16_t)(srcDiff[0 * srcStride] - srcDiff[1 * srcStride]);
    const std::int16_t b2 = (std::int16_t)(srcDiff[2 * srcStride] + srcDiff[3 * srcStride]);
    const std::int16_t b3 = (std::int16_t)(srcDiff[2 * srcStride] - srcDiff[3 * srcStride]);
    const std::int16_t b4 = (std::int16_t)(srcDiff[4 * srcStride] + srcDiff[5 * srcStride]);
    const std::int16_t b5 = (std::int16_t)(srcDiff[4 * srcStride] - srcDiff[5 * srcStride]);
    const std::int16_t b6 = (std::int16_t)(srcDiff[6 * srcStride] + srcDiff[7 * srcStride]);
    const std::int16_t b7 = (std::int16_t)(srcDiff[6 * srcStride] - srcDiff[7 * srcStride]);
    const std::int16_t c0 = (std::int16_t)(b0 + b2);
    const std::int16_t c1 = (std::int16_t)(b1 + b3);
    const std::int16_t c2 = (std::int16_t)(b0 - b2);
    const std::int16_t c3 = (std::int16_t)(b1 - b3);
    const std::int16_t c4 = (std::int16_t)(b4 + b6);
    const std::int16_t c5 = (std::int16_t)(b5 + b7);
    const std::int16_t c6 = (std::int16_t)(b4 - b6);
    const std::int16_t c7 = (std::int16_t)(b5 - b7);
    coeff[0] = (std::int16_t)(c0 + c4);
    coeff[7] = (std::int16_t)(c1 + c5);
    coeff[3] = (std::int16_t)(c2 + c6);
    coeff[4] = (std::int16_t)(c3 + c7);
    coeff[2] = (std::int16_t)(c0 - c4);
    coeff[6] = (std::int16_t)(c1 - c5);
    coeff[1] = (std::int16_t)(c2 - c6);
    coeff[5] = (std::int16_t)(c3 - c7);
}

// The hadamard_highbd_col8_second_pass (picture_operators_c.c:207-234),
// test-side transcription.
static void oracleHadamardCol8SecondPass(const std::int16_t* srcDiff, std::ptrdiff_t srcStride,
                                          std::int32_t* coeff) {
    const std::int32_t b0 = srcDiff[0 * srcStride] + srcDiff[1 * srcStride];
    const std::int32_t b1 = srcDiff[0 * srcStride] - srcDiff[1 * srcStride];
    const std::int32_t b2 = srcDiff[2 * srcStride] + srcDiff[3 * srcStride];
    const std::int32_t b3 = srcDiff[2 * srcStride] - srcDiff[3 * srcStride];
    const std::int32_t b4 = srcDiff[4 * srcStride] + srcDiff[5 * srcStride];
    const std::int32_t b5 = srcDiff[4 * srcStride] - srcDiff[5 * srcStride];
    const std::int32_t b6 = srcDiff[6 * srcStride] + srcDiff[7 * srcStride];
    const std::int32_t b7 = srcDiff[6 * srcStride] - srcDiff[7 * srcStride];
    const std::int32_t c0 = b0 + b2;
    const std::int32_t c1 = b1 + b3;
    const std::int32_t c2 = b0 - b2;
    const std::int32_t c3 = b1 - b3;
    const std::int32_t c4 = b4 + b6;
    const std::int32_t c5 = b5 + b7;
    const std::int32_t c6 = b4 - b6;
    const std::int32_t c7 = b5 - b7;
    coeff[0] = c0 + c4;
    coeff[7] = c1 + c5;
    coeff[3] = c2 + c6;
    coeff[4] = c3 + c7;
    coeff[2] = c0 - c4;
    coeff[6] = c1 - c5;
    coeff[1] = c2 - c6;
    coeff[5] = c3 - c7;
}

// svt_aom_highbd_hadamard_8x8_c (picture_operators_c.c:238-262), test-side
// transcription: eight first-pass column calls into a 16-bit buffer, then
// eight second-pass row calls into a 32-bit buffer.
static void oracleHighbdHadamard8x8(const std::int16_t* srcDiff, std::ptrdiff_t srcStride,
                                     std::int32_t* coeff) {
    std::int16_t buffer[64];
    std::int32_t buffer2[64];
    std::int16_t* tmp = buffer;
    for (int idx = 0; idx < 8; ++idx) {
        oracleHadamardCol8FirstPass(srcDiff, srcStride, tmp);
        tmp += 8;
        ++srcDiff;
    }
    tmp = buffer;
    for (int idx = 0; idx < 8; ++idx) {
        oracleHadamardCol8SecondPass(tmp, 8, buffer2 + 8 * idx);
        ++tmp;
    }
    for (int idx = 0; idx < 64; ++idx) coeff[idx] = buffer2[idx];
}

// hadamard_col4 (picture_operators_c.c:73-83), test-side transcription: the
// >>1 butterflies the REGULAR hadamard applies at both passes.
static void oracleHadamardCol4(const std::int16_t* srcDiff, std::ptrdiff_t srcStride,
                                std::int16_t* coeff) {
    const std::int16_t b0 = (std::int16_t)((srcDiff[0 * srcStride] + srcDiff[1 * srcStride]) >> 1);
    const std::int16_t b1 = (std::int16_t)((srcDiff[0 * srcStride] - srcDiff[1 * srcStride]) >> 1);
    const std::int16_t b2 = (std::int16_t)((srcDiff[2 * srcStride] + srcDiff[3 * srcStride]) >> 1);
    const std::int16_t b3 = (std::int16_t)((srcDiff[2 * srcStride] - srcDiff[3 * srcStride]) >> 1);
    coeff[0] = (std::int16_t)(b0 + b2);
    coeff[1] = (std::int16_t)(b1 + b3);
    coeff[2] = (std::int16_t)(b0 - b2);
    coeff[3] = (std::int16_t)(b1 - b3);
}

// svt_aom_hadamard_4x4_c (picture_operators_c.c:85-115), test-side
// transcription: four col4 column passes, four col4 passes over the
// intermediate, then the SSE2-matching transpose.
static void oracleHadamard4x4(const std::int16_t* srcDiff, std::ptrdiff_t srcStride,
                               std::int32_t* coeff) {
    std::int16_t buffer[16];
    std::int16_t buffer2[16];
    std::int16_t* tmp = buffer;
    for (int idx = 0; idx < 4; ++idx) {
        oracleHadamardCol4(srcDiff, srcStride, tmp);
        tmp += 4;
        ++srcDiff;
    }
    tmp = buffer;
    for (int idx = 0; idx < 4; ++idx) {
        oracleHadamardCol4(tmp, 4, buffer2 + 4 * idx);
        ++tmp;
    }
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) coeff[i * 4 + j] = (std::int32_t)buffer2[j * 4 + i];
}

// svt_aom_satd_c (common_dsp_rtcd.c:48-54), test-side transcription.
static std::int32_t oracleSatd(const std::int32_t* coeff, std::int32_t length) {
    std::int32_t satd = 0;
    for (std::int32_t i = 0; i < length; ++i) satd += coeff[i] < 0 ? -coeff[i] : coeff[i];
    return satd;
}

// svt_psy_distortion_hbd (ac_bias.c:103-144), test-side transcription.
static std::uint64_t oraclePsyDistortionHbd(const std::uint16_t* input, std::uint32_t inputStride,
                                             const std::uint16_t* recon, std::uint32_t reconStride,
                                             std::uint32_t width, std::uint32_t height) {
    std::uint64_t energyGap = 0;
    if (width >= 8 && height >= 8) {
        for (std::uint32_t j = 0; j < height; j += 8) {
            for (std::uint32_t i = 0; i < width; i += 8) {
                std::int32_t coeffs[64];
                oracleHighbdHadamard8x8((const std::int16_t*)input + j * inputStride + i, inputStride, coeffs);
                const std::int32_t inputEnergy = ((oracleSatd(coeffs, 64) + 2) >> 2) - ((coeffs[0] + 2) >> 2);
                oracleHighbdHadamard8x8((const std::int16_t*)recon + j * reconStride + i, reconStride, coeffs);
                const std::int32_t reconEnergy = ((oracleSatd(coeffs, 64) + 2) >> 2) - ((coeffs[0] + 2) >> 2);
                energyGap += (std::uint64_t)(inputEnergy - reconEnergy < 0 ? reconEnergy - inputEnergy
                                                                           : inputEnergy - reconEnergy);
            }
        }
    } else {
        for (std::uint64_t j = 0; j < height; j += 4) {
            for (std::uint64_t i = 0; i < width; i += 4) {
                std::int32_t coeffs[16];
                oracleHadamard4x4((const std::int16_t*)input + j * inputStride + i, inputStride, coeffs);
                const std::int32_t inputEnergy = (oracleSatd(coeffs, 16) << 1) - coeffs[0];
                oracleHadamard4x4((const std::int16_t*)recon + j * reconStride + i, reconStride, coeffs);
                const std::int32_t reconEnergy = (oracleSatd(coeffs, 16) << 1) - coeffs[0];
                energyGap += (std::uint64_t)(inputEnergy - reconEnergy < 0 ? reconEnergy - inputEnergy
                                                                           : inputEnergy - reconEnergy);
            }
        }
    }
    return energyGap << 2;
}

// ---------------------------------------------------------------------------
// MD1a: the SSIM similarity core (enc_dec_process.c:645-671).
// ---------------------------------------------------------------------------
TEST_CASE("l16_rdo similarity reproduces the svt_aom_similarity formula") {
    // enc_dec_process.c:650-659 - the bd-8/10/12 constant scaling. count=64:
    // c1 = (26634 * 4096) >> 12 = 26634, c2 = 239708 (the count*count factor
    // cancels the >>12 exactly at count 64).
    // enc_dec_process.c:665-668 - the ssim_n/ssim_d products.
    // Identity case: ssim_n == ssim_d -> score exactly 1.0.
    CHECK(similarity(6400, 6400, 640000, 640000, 640000, 64, 8) == 1.0);
    // Asymmetric case (all s=100, all r=90, 64 samples): score 0.994476...
    // derived from the same formula with c1=26634, c2=239708.
    const double want = oracleSimilarity(6400, 5760, 640000, 518400, 576000, 64, 8);
    CHECK(similarity(6400, 5760, 640000, 518400, 576000, 64, 8) == want);
    // bd=10: c1 = (428658 * 256) >> 12 = 26791 (10-bit count=16 block).
    CHECK(similarity(100, 90, 10000, 8100, 9000, 16, 10) ==
          oracleSimilarity(100, 90, 10000, 8100, 9000, 16, 10));
    // bd=12: c1 = (6868593 * 256) >> 12 = 429287.
    CHECK(similarity(100, 90, 10000, 8100, 9000, 16, 12) ==
          oracleSimilarity(100, 90, 10000, 8100, 9000, 16, 12));
}

TEST_CASE("l16_rdo similarity pins the count=16 scaling") {
    // count=16: c1 = (26634 * 256) >> 12 = 1664 (truncating shift), NOT the
    // 26634/16 = 1664.625 rounded value. The >>12 floors.
    CHECK(similarity(136, 136, 1496, 1496, 1496, 16, 8) == 1.0);
    const double asymmetric = oracleSimilarity(136, 32, 1496, 64, 272, 16, 8);
    CHECK(similarity(136, 32, 1496, 64, 272, 16, 8) == asymmetric);
    // The asymmetric score itself: ssim_n = (2*136*32+1664)*(2*16*272-2*136*32+14981)
    //   = 10368 * 14981 = 155323008
    // ssim_d = (136*136+32*32+1664)*(16*1496-136*136+16*64-32*32+14981)
    //   = 21184 * (5440+1024-1024+14981) = 21184 * 20421 = 432598464
    // score = 155323008 / 432598464.
    CHECK(asymmetric == doctest::Approx(155323008.0 / 432598464.0).epsilon(1e-15));
}

// ---------------------------------------------------------------------------
// MD1a: the SSIM 8-bit kernels (mode_decision.c:4238-4289).
// ---------------------------------------------------------------------------
TEST_CASE("l16_rdo ssim4x4 accumulates the five sums then scores") {
    // svt_ssim_4x4_c (mode_decision.c:4238-4261): the 4x4 window sum
    // accumulation, then similarity(.., count=16, bd=8).
    // Fixture: s = 1..16 row-major (sp=4), r = all 2 (rp=4).
    std::uint8_t s[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    std::uint8_t r[16] = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
    // Hand-derived sums: sum_s=136, sum_r=32, sum_sq_s=1496, sum_sq_r=64,
    // sum_sxr=272. score = similarity(136, 32, 1496, 64, 272, 16, 8)
    //   = 155323008 / 432598464 (see the count=16 case above).
    CHECK(ssim4x4(s, 4, r, 4) == oracleSsim4x4(s, 4, r, 4));
    CHECK(ssim4x4(s, 4, r, 4) == doctest::Approx(155323008.0 / 432598464.0).epsilon(1e-15));
    // Identical buffers score exactly 1.0 (the identity algebra of :665-:668).
    CHECK(ssim4x4(s, 4, s, 4) == 1.0);
    // Strided variant: sp=8 over a 4x4 window reads rows 0 and 4 of an 8-wide
    // buffer; the row-major fixture with sp=8 walks rows 0..3 of the same
    // bytes - the sums differ, so pin against the oracle rather than a hand
    // value.
    std::uint8_t wide[4 * 8];
    for (int i = 0; i < 32; ++i) wide[i] = (std::uint8_t)(i + 1);
    CHECK(ssim4x4(wide, 8, r, 4) == oracleSsim4x4(wide, 8, r, 4));
}

TEST_CASE("l16_rdo ssim8x8 accumulates the five sums then scores") {
    // svt_ssim_8x8_c (mode_decision.c:4263-4289): the 8x8 window, then
    // similarity(.., count=64, bd=8).
    std::uint8_t s[64];
    std::uint8_t r[64];
    for (int i = 0; i < 64; ++i) s[i] = 1;
    for (int i = 0; i < 64; ++i) r[i] = 2;
    // sum_s=64, sum_r=128, sum_sq_s=64, sum_sq_r=256, sum_sxr=128.
    // c1 = (26634*4096)>>12 = 26634, c2 = 239708.
    // ssim_n = (16384+26634)*239708 = 43018*239708
    // ssim_d = (4096+16384+26634)*239708 = 47114*239708
    // score = 43018/47114.
    CHECK(ssim8x8(s, 8, r, 8) == oracleSsim8x8(s, 8, r, 8));
    CHECK(ssim8x8(s, 8, r, 8) == doctest::Approx(43018.0 / 47114.0).epsilon(1e-15));
    CHECK(ssim8x8(s, 8, s, 8) == 1.0);
    // Strided: sp=16 over an 8-wide fixture.
    std::uint8_t wide[8 * 16];
    for (int i = 0; i < 128; ++i) wide[i] = (std::uint8_t)(i + 1);
    CHECK(ssim8x8(wide, 16, r, 8) == oracleSsim8x8(wide, 16, r, 8));
}

// ---------------------------------------------------------------------------
// MD1a: the SSIM hbd kernels (mode_decision.c:4291-4339).
// ---------------------------------------------------------------------------
TEST_CASE("l16_rdo ssim4x4Hbd and ssim8x8Hbd use the 10-bit similarity") {
    // svt_ssim_4x4_hbd_c (mode_decision.c:4291-4314) / svt_ssim_8x8_hbd_c
    // (4316-4339): the same five-sum walk over uint16 samples, then
    // similarity(.., count, bd=10).
    std::uint16_t s4[16], r4[16];
    for (int i = 0; i < 16; ++i) s4[i] = (std::uint16_t)(i + 1);
    for (int i = 0; i < 16; ++i) r4[i] = 2;
    CHECK(ssim4x4Hbd(s4, 4, r4, 4) == oracleSsim4x4Hbd(s4, 4, r4, 4));
    // 10-bit identity: score exactly 1.0.
    CHECK(ssim4x4Hbd(s4, 4, s4, 4) == 1.0);
    std::uint16_t s8[64], r8[64];
    for (int i = 0; i < 64; ++i) s8[i] = 1;
    for (int i = 0; i < 64; ++i) r8[i] = 2;
    CHECK(ssim8x8Hbd(s8, 8, r8, 8) == oracleSsim8x8Hbd(s8, 8, r8, 8));
    CHECK(ssim8x8Hbd(s8, 8, s8, 8) == 1.0);
    // The 10-bit constant scaling differs from the 8-bit one (c1 = 428658 at
    // count 64 -> 428658, not 26634): the same five sums (all-1 vs all-2,
    // every value fits uint8) score differently under the two constant
    // pairs, so the hbd score is compared against the 8-bit kernel on the
    // same VALUES held in a byte fixture (svt_ssim_8x8_c's parameter type,
    // mode_decision.c:4263).
    std::uint8_t s8l[64], r8l[64];
    for (int i = 0; i < 64; ++i) s8l[i] = 1;
    for (int i = 0; i < 64; ++i) r8l[i] = 2;
    CHECK(ssim8x8Hbd(s8, 8, r8, 8) != ssim8x8(s8l, 8, r8l, 8));
}

// ---------------------------------------------------------------------------
// MD1a: the SSIM block walkers (mode_decision.c:4341-4442).
// ---------------------------------------------------------------------------
TEST_CASE("l16_rdo ssim8x8Blocks walks the 8x8 grid") {
    // ssim_8x8_blocks (mode_decision.c:4341-4360): the 8-step outer/inner
    // walk, each block CLIP3(0,1)-clamped, then the sample average.
    // Fixture: 16x16, s all 1, r all 2 -> 4 samples of the same 8x8 score.
    std::vector<std::uint8_t> s(256, 1);
    std::vector<std::uint8_t> r(256, 2);
    const double block = 43018.0 / 47114.0;
    CHECK(ssim8x8Blocks(s.data(), 16, r.data(), 16, 16, 16) ==
          doctest::Approx(block).epsilon(1e-15));
    // The stride path: sp=24 over an 8x8 fixture region of a 24-stride buffer.
    std::vector<std::uint8_t> ws(24 * 16, 1);
    std::vector<std::uint8_t> wr(24 * 16, 2);
    CHECK(ssim8x8Blocks(ws.data(), 24, wr.data(), 24, 16, 16) ==
          doctest::Approx(block).epsilon(1e-15));
}

TEST_CASE("l16_rdo ssim4x4Blocks walks the 4x4 grid") {
    // ssim_4x4_blocks (mode_decision.c:4362-4381): the 4-step walk, 4x4
    // kernel.
    // Fixture: 12x12, s all 1, r all 2 -> 9 samples of the 4x4 score
    // (sum_s=16, sum_r=32, sum_sq_s=16, sum_sq_r=64, sum_sxr=32; count=16).
    std::vector<std::uint8_t> s(144, 1);
    std::vector<std::uint8_t> r(144, 2);
    // c1 = (26634*256)>>12 = 1664; c2 = (239708*256)>>12 = 14981.
    // ssim_n = (2*16*32+1664)*(2*16*32-2*16*32+14981) = 2688*14981
    // ssim_d = (256+1024+1664)*(16*16-256+16*64-1024+14981) = 2944*14981
    // (the constant fixture zeroes both c2-adjacent differences, so the c2
    // factors cancel: score = 2688/2944).
    const double block = 2688.0 * 14981.0 / (2944.0 * 14981.0);
    CHECK(ssim4x4Blocks(s.data(), 12, r.data(), 12, 12, 12) ==
          doctest::Approx(block).epsilon(1e-15));
    // Mixed fixture: s = 1..144 row-major (sp=12).
    std::vector<std::uint8_t> ms(144);
    for (int i = 0; i < 144; ++i) ms[i] = (std::uint8_t)(i + 1);
    // oracle via 4x4 blocks accumulation.
    double total = 0;
    int samples = 0;
    for (std::uint32_t i = 0; i <= 12 - 4; i += 4)
        for (std::uint32_t j = 0; j <= 12 - 4; j += 4) {
            double v = oracleSsim4x4(ms.data() + i * 12 + j, 12, r.data() + i * 12 + j, 12);
            if (v < 0) v = 0;
            if (v > 1) v = 1;
            total += v;
            ++samples;
        }
    CHECK(ssim4x4Blocks(ms.data(), 12, r.data(), 12, 12, 12) == doctest::Approx(total / samples).epsilon(1e-15));
}

TEST_CASE("l16_rdo ssim dispatches on divisibility") {
    // ssim (mode_decision.c:4383-4390): the 8-divisible case takes
    // ssim_8x8_blocks, the 4-divisible case ssim_4x4_blocks.
    std::vector<std::uint8_t> s16(256, 1);
    std::vector<std::uint8_t> r16(256, 2);
    const double eight = 43018.0 / 47114.0;
    CHECK(ssim(s16.data(), 16, r16.data(), 16, 16, 16) ==
          doctest::Approx(eight).epsilon(1e-15));
    // 12x12: not 8-divisible -> the 4x4 walk.
    std::vector<std::uint8_t> s12(144, 1);
    std::vector<std::uint8_t> r12(144, 2);
    const double block4 = 2688.0 * 14981.0 / (2944.0 * 14981.0);
    CHECK(ssim(s12.data(), 12, r12.data(), 12, 12, 12) ==
          doctest::Approx(block4).epsilon(1e-15));
    // The hbd twins (ssim_8x8_blocks_hbd :4392-4411, ssim_4x4_blocks_hbd
    // :4413-4432, ssim_hbd :4434-4442) walk the same grids over uint16 with
    // the bd=10 constants: c1 = (428658*4096)>>12 = 428658 at count 64 and
    // (428658*256)>>12 = 26791 at count 16 (enc_dec_process.c:655). The
    // constant fixture zeroes both c2-adjacent differences, so the score is
    // (2*sumS*sumR + c1) / (sumS^2 + sumR^2 + c1): 16x16 ->
    // (16384+428658)/(20480+428658) = 445042/449138; 4x4 ->
    // (1024+26791)/(1280+26791) = 27815/28071.
    const double eightHbd = 445042.0 / 449138.0;
    const double block4Hbd = 27815.0 / 28071.0;
    std::vector<std::uint16_t> hs16(256, 1);
    std::vector<std::uint16_t> hr16(256, 2);
    CHECK(ssimHbd(hs16.data(), 16, hr16.data(), 16, 16, 16) ==
          doctest::Approx(eightHbd).epsilon(1e-15));
    std::vector<std::uint16_t> hs12(144, 1);
    std::vector<std::uint16_t> hr12(144, 2);
    CHECK(ssimHbd(hs12.data(), 12, hr12.data(), 12, 12, 12) ==
          doctest::Approx(block4Hbd).epsilon(1e-15));
    CHECK(ssim8x8BlocksHbd(hs16.data(), 16, hr16.data(), 16, 16, 16) ==
          doctest::Approx(eightHbd).epsilon(1e-15));
    CHECK(ssim4x4BlocksHbd(hs12.data(), 12, hr12.data(), 12, 12, 12) ==
          doctest::Approx(block4Hbd).epsilon(1e-15));
}

// ---------------------------------------------------------------------------
// MD1b: the hbd psy chain (ac_bias.c:103-144 + picture_operators_c.c).
// ---------------------------------------------------------------------------
TEST_CASE("l16_rdo hadamardHighbdCol8 helpers transpose the reference bodies") {
    // picture_operators_c.c:176-203 (first pass) and :207-234 (second pass).
    // Fixture: an 8x8 int16 block with distinct values so every sum path is
    // exercised.
    std::int16_t src[8 * 10];
    for (int i = 0; i < 80; ++i) src[i] = (std::int16_t)(i * 3 - 40);
    std::int16_t coeff[8];
    std::int16_t oc[8];
    hadamardHighbdCol8FirstPass(src, 10, coeff);
    oracleHadamardCol8FirstPass(src, 10, oc);
    for (int i = 0; i < 8; ++i) CHECK(coeff[i] == oc[i]);
    // The second pass: the same 8x8 block read column-wise (stride 1 rows of
    // 8 - the reference reads a row of the 8x8 intermediate buffer).
    std::int16_t row[8] = {7, -3, 11, 5, -9, 2, 13, -1};
    std::int32_t coeff2[8];
    std::int32_t oc2[8];
    hadamardHighbdCol8SecondPass(row, 1, coeff2);
    oracleHadamardCol8SecondPass(row, 1, oc2);
    for (int i = 0; i < 8; ++i) CHECK(coeff2[i] == oc2[i]);
    // The strided second pass (the reference's stride-8 row walk).
    std::int16_t buf[8 * 8];
    for (int i = 0; i < 64; ++i) buf[i] = (std::int16_t)(i * 7 - 100);
    hadamardHighbdCol8SecondPass(buf, 8, coeff2);
    oracleHadamardCol8SecondPass(buf, 8, oc2);
    for (int i = 0; i < 8; ++i) CHECK(coeff2[i] == oc2[i]);
}

TEST_CASE("l16_rdo highbdHadamard8x8 mirrors the two-pass structure") {
    // picture_operators_c.c:238-262: eight first-pass column calls into the
    // 16-bit buffer, then eight second-pass row calls into the 32-bit buffer,
    // then the copy.
    std::int16_t src[8 * 8];
    for (int i = 0; i < 64; ++i) src[i] = (std::int16_t)(i * 5 - 120);
    std::int32_t coeff[64];
    std::int32_t oc[64];
    highbdHadamard8x8(src, 8, coeff);
    oracleHighbdHadamard8x8(src, 8, oc);
    for (int i = 0; i < 64; ++i) CHECK(coeff[i] == oc[i]);
    // The DC coefficient: the sum of all 64 source values (both passes are
    // pure +/- butterflies, so coeff[0] = sum of the block).
    std::int32_t sum = 0;
    for (int i = 0; i < 64; ++i) sum += src[i];
    CHECK(coeff[0] == sum);
    // A stride-2 fixture over a 16-wide buffer.
    std::int16_t wide[8 * 16];
    for (int i = 0; i < 128; ++i) wide[i] = (std::int16_t)(i * 11 - 300);
    highbdHadamard8x8(wide, 16, coeff);
    oracleHighbdHadamard8x8(wide, 16, oc);
    for (int i = 0; i < 64; ++i) CHECK(coeff[i] == oc[i]);
}

TEST_CASE("l16_rdo psyDistortionHbd scales the energy gap by 4") {
    // ac_bias.c:103-144. Derivations from the reference bodies:
    // - the 8x8 branch (ac_bias.c:107-122) computes energy = ((satd + 2) >> 2)
    //   - ((coeffs[0] + 2) >> 2); a CONSTANT block's highbd hadamard is
    //   DC-only (picture_operators_c.c:176-262 butterflies: coeff[0] = 64c,
    //   every other bin 0) and satd (common_dsp_rtcd.c:48-54) = sum |coeff|
    //   = 64c = coeffs[0], so input_energy == recon_energy == 0 for every
    //   constant level: the gap between all-1 and all-2 is 0.
    std::vector<std::uint16_t> in(64, 1);
    std::vector<std::uint16_t> rc(64, 2);
    CHECK(psyDistortionHbd(in.data(), 8, rc.data(), 8, 8, 8) == 0);
    // - the 4x4 branch (ac_bias.c:123-139) computes energy = (satd << 1)
    //   - coeffs[0] over svt_aom_hadamard_4x4, whose hadamard_col4 carries a
    //   >>1 (picture_operators_c.c:74-77): a constant-4x4 block gives
    //   coeff[0] = 4c and satd = 4c, so energy = 8c - 4c = 4c. all-1: 4,
    //   all-2: 8, gap 4, << 2 = 16.
    std::vector<std::uint16_t> in4(16, 1);
    std::vector<std::uint16_t> rc4(16, 2);
    CHECK(psyDistortionHbd(in4.data(), 4, rc4.data(), 4, 4, 4) == 16);
    // Identical buffers: gap 0 in both branches.
    CHECK(psyDistortionHbd(in.data(), 8, in.data(), 8, 8, 8) == 0);
    CHECK(psyDistortionHbd(in4.data(), 4, in4.data(), 4, 4, 4) == 0);
    // 16x8 (two 8x8 columns): 0 + 0 -> 0.
    std::vector<std::uint16_t> in16(16 * 8, 1);
    std::vector<std::uint16_t> rc16(16 * 8, 2);
    CHECK(psyDistortionHbd(in16.data(), 16, rc16.data(), 16, 16, 8) == 0);
    // Non-constant fixtures against the test-side transcription of
    // ac_bias.c:103-144: 8x8 (one hadamard), 16x16 (four), 12x4 and 4x8
    // (small-branch 4x4 walks), and a strided 8x8.
    std::vector<std::uint16_t> a(8 * 8), b(8 * 8);
    for (int i = 0; i < 64; ++i) {
        a[i] = (std::uint16_t)((i * 73 + 11) & 0x3FF);
        b[i] = (std::uint16_t)((i * 131 + 29) & 0x3FF);
    }
    CHECK(psyDistortionHbd(a.data(), 8, b.data(), 8, 8, 8) == oraclePsyDistortionHbd(a.data(), 8, b.data(), 8, 8, 8));
    std::vector<std::uint16_t> c(16 * 16), d(16 * 16);
    for (int i = 0; i < 256; ++i) {
        c[i] = (std::uint16_t)((i * 37 + 3) & 0x3FF);
        d[i] = (std::uint16_t)((i * 53 + 7) & 0x3FF);
    }
    CHECK(psyDistortionHbd(c.data(), 16, d.data(), 16, 16, 16) ==
          oraclePsyDistortionHbd(c.data(), 16, d.data(), 16, 16, 16));
    std::vector<std::uint16_t> e(12 * 4), f(12 * 4);
    for (int i = 0; i < 48; ++i) {
        e[i] = (std::uint16_t)((i * 91 + 5) & 0x3FF);
        f[i] = (std::uint16_t)((i * 17 + 13) & 0x3FF);
    }
    CHECK(psyDistortionHbd(e.data(), 12, f.data(), 12, 12, 4) ==
          oraclePsyDistortionHbd(e.data(), 12, f.data(), 12, 12, 4));
    std::vector<std::uint16_t> g(4 * 8), h(4 * 8);
    for (int i = 0; i < 32; ++i) {
        g[i] = (std::uint16_t)((i * 23 + 1) & 0x3FF);
        h[i] = (std::uint16_t)((i * 41 + 19) & 0x3FF);
    }
    CHECK(psyDistortionHbd(g.data(), 4, h.data(), 4, 4, 8) ==
          oraclePsyDistortionHbd(g.data(), 4, h.data(), 4, 4, 8));
    std::vector<std::uint16_t> s(10 * 8), t(10 * 8);
    for (int i = 0; i < 80; ++i) {
        s[i] = (std::uint16_t)((i * 57 + 9) & 0x3FF);
        t[i] = (std::uint16_t)((i * 29 + 21) & 0x3FF);
    }
    CHECK(psyDistortionHbd(s.data(), 10, t.data(), 10, 8, 8) ==
          oraclePsyDistortionHbd(s.data(), 10, t.data(), 10, 8, 8));
}

// ---------------------------------------------------------------------------
// MD1a: the SSIM distortion kernel (mode_decision.c:4444-4490).
// ---------------------------------------------------------------------------
TEST_CASE("l16_rdo spatialFullDistortionSsimKernel pins the 8-bit branch") {
    // mode_decision.c:4444-4490: spatial_distortion = (1 - ssim_score) *
    // count * 100 * 7 * m, plus the psy_distortion when ac_bias is set.
    // Fixture: 8x8, input all 1, recon all 2, ac_bias 0.
    // ssim_score = 43018/47114; count = 64; m = 1.
    // spatial = (uint64_t)(4096/47114 * 64 * 100 * 7) = (uint64_t)(3895.6..)
    std::vector<std::uint8_t> in(64, 1);
    std::vector<std::uint8_t> rc(64, 2);
    const std::uint64_t want = (std::uint64_t)((1 - 43018.0 / 47114.0) * 64 * 100 * 7);
    CHECK(spatialFullDistortionSsimKernel(in.data(), 0, 8, rc.data(), 0, 8, 8, 8, false, 0.0) == want);
    // ac_bias set: psy_distortion joins. psyDistortion of the all-1/all-2
    // 8x8 fixture: the RE4-landed satd-based energy gap.
    std::uint64_t psy = (std::uint64_t)((double)psyDistortion(in.data(), 8, rc.data(), 8, 8, 8) * 0.5);
    CHECK(spatialFullDistortionSsimKernel(in.data(), 0, 8, rc.data(), 0, 8, 8, 8, false, 0.5) ==
          want + psy);
    // Identical buffers: ssim 1.0 -> spatial 0; psy 0 -> total 0.
    CHECK(spatialFullDistortionSsimKernel(in.data(), 0, 8, in.data(), 0, 8, 8, 8, false, 0.5) == 0);
    // The hbd branch (m=8): the uint16 fixture travels as its byte pointer -
    // the reference's own calling convention (mode_decision.c:4444-4446 the
    // byte-typed parameters; :4467-4469 the kernel casts to uint16 inside).
    // The ssim_hbd score is the bd=10 value 445042/449138 (see the dispatch
    // test above), so wantHbd = (1 - 445042/449138)*64*100*7*8
    // = (4096/449138)*358400 = 3268.4975 -> 3268 truncated.
    std::vector<std::uint16_t> hin(64, 1);
    std::vector<std::uint16_t> hrc(64, 2);
    const std::uint64_t wantHbd = (std::uint64_t)((1 - 445042.0 / 449138.0) * 64 * 100 * 7 * 8);
    CHECK(spatialFullDistortionSsimKernel((const std::uint8_t*)hin.data(), 0, 8,
                                          (const std::uint8_t*)hrc.data(), 0, 8, 8, 8, true, 0.0) ==
          wantHbd);
}

// ---------------------------------------------------------------------------
// MD1c: the MV/ME helpers (mode_decision.c:71-310 + :739-846).
// ---------------------------------------------------------------------------
TEST_CASE("l16_rdo checkMvValidity bounds the shifted MV") {
    // mode_decision.c:86-100: the shifted MV must satisfy
    // MV_LOW < x,y < MV_UPP (cabac_context_model.h:197-199, MV_IN_USE_BITS
    // = 14; rate_est.h:834-837 carries the constants).
    // need_shift=0: the 1/8-pel limits apply directly.
    CHECK(checkMvValidity(0, 0, 0) == true);
    CHECK(checkMvValidity(16383, -16383, 0) == true);
    CHECK(checkMvValidity(16384, 0, 0) == false);   // MV_UPP is exclusive
    CHECK(checkMvValidity(-16384, 0, 0) == false);  // MV_LOW is exclusive
    CHECK(checkMvValidity(0, 16384, 0) == false);
    CHECK(checkMvValidity(0, -16384, 0) == false);
    // need_shift=1: the 1/4-pel input shifts <<1 to 1/8-pel, halving the
    // valid range: |x_mv| < 8192.
    CHECK(checkMvValidity(8191, -8191, 1) == true);
    CHECK(checkMvValidity(8192, 0, 1) == false);
    CHECK(checkMvValidity(0, -8192, 1) == false);
    // Mixed signs.
    CHECK(checkMvValidity(100, -100, 1) == true);
    CHECK(checkMvValidity(16000, -100, 1) == false);
}

TEST_CASE("l16_rdo haveNewmvInInterMode enumerates the six NEW modes") {
    // mode_decision.c:274-277: the six modes that carry a NEW component.
    CHECK(haveNewmvInInterMode(NEWMV) == 1);
    CHECK(haveNewmvInInterMode(NEW_NEWMV) == 1);
    CHECK(haveNewmvInInterMode(NEAREST_NEWMV) == 1);
    CHECK(haveNewmvInInterMode(NEW_NEARESTMV) == 1);
    CHECK(haveNewmvInInterMode(NEAR_NEWMV) == 1);
    CHECK(haveNewmvInInterMode(NEW_NEARMV) == 1);
    // The non-NEW modes return 0: NEARESTMV, NEARMV, GLOBALMV, GLOBAL_GLOBALMV,
    // NEAREST_NEARESTMV, NEAR_NEARMV and the intra modes.
    CHECK(haveNewmvInInterMode(NEARESTMV) == 0);
    CHECK(haveNewmvInInterMode(NEARMV) == 0);
    CHECK(haveNewmvInInterMode(GLOBALMV) == 0);
    CHECK(haveNewmvInInterMode(NEAREST_NEARESTMV) == 0);
    CHECK(haveNewmvInInterMode(NEAR_NEARMV) == 0);
    CHECK(haveNewmvInInterMode(GLOBAL_GLOBALMV) == 0);
    CHECK(haveNewmvInInterMode(DC_PRED) == 0);
}

TEST_CASE("l16_rdo getMaxDrlIndex derives the two-mode families") {
    // mode_decision.c:286-310: the NEW family gates on refmvCnt < 2 / == 2 /
    // > 2; the NEAR family on < 3 / == 3 / > 3; other modes stay 0.
    CHECK(getMaxDrlIndex(0, NEWMV) == 1);
    CHECK(getMaxDrlIndex(1, NEWMV) == 1);
    CHECK(getMaxDrlIndex(2, NEWMV) == 2);
    CHECK(getMaxDrlIndex(3, NEWMV) == 3);
    CHECK(getMaxDrlIndex(9, NEWMV) == 3);
    CHECK(getMaxDrlIndex(0, NEW_NEWMV) == 1);
    CHECK(getMaxDrlIndex(2, NEW_NEWMV) == 2);
    CHECK(getMaxDrlIndex(0, NEARMV) == 1);
    CHECK(getMaxDrlIndex(2, NEARMV) == 1);
    CHECK(getMaxDrlIndex(3, NEARMV) == 2);
    CHECK(getMaxDrlIndex(4, NEARMV) == 3);
    CHECK(getMaxDrlIndex(0, NEAR_NEARMV) == 1);
    CHECK(getMaxDrlIndex(3, NEAR_NEARMV) == 2);
    CHECK(getMaxDrlIndex(0, NEAR_NEWMV) == 1);
    CHECK(getMaxDrlIndex(4, NEAR_NEWMV) == 3);
    CHECK(getMaxDrlIndex(0, NEW_NEARMV) == 1);
    CHECK(getMaxDrlIndex(3, NEW_NEARMV) == 2);
    // Non-NEW/NEAR modes: 0.
    CHECK(getMaxDrlIndex(3, NEARESTMV) == 0);
    CHECK(getMaxDrlIndex(3, GLOBALMV) == 0);
    CHECK(getMaxDrlIndex(3, NEAREST_NEARESTMV) == 0);
    CHECK(getMaxDrlIndex(3, GLOBAL_GLOBALMV) == 0);
    CHECK(getMaxDrlIndex(3, DC_PRED) == 0);
}

TEST_CASE("l16_rdo isValidUnipredRef gates on the pruning ctrls") {
    // mode_decision.c:789-800: enabled=0 -> always true; otherwise the
    // do_ref flag with the closest_refs fallback for ref_idx 0.
    RefPruningCtrls pruning;
    pruning.enabled = 0;
    pruning.closestRefs[0] = 0;
    pruning.closestRefs[1] = 0;
    RefFilteringRes filtering;  // RefResults[11][2][4] (md_process.h:1136)
    std::memset(filtering, 0, sizeof(filtering));
    // disabled pruning: always valid.
    CHECK(isValidUnipredRef(pruning, filtering, 0, 0, 0) == true);
    // enabled + do_ref=1 -> valid.
    pruning.enabled = 1;
    filtering[0][0][0].doRef = 1;
    CHECK(isValidUnipredRef(pruning, filtering, 0, 0, 0) == true);
    // enabled + do_ref=0 + ref_idx 0 + closest_refs=1 -> valid (the
    // ref_idx || closest_refs disjunct).
    filtering[0][0][0].doRef = 0;
    pruning.closestRefs[0] = 1;
    CHECK(isValidUnipredRef(pruning, filtering, 0, 0, 0) == true);
    // enabled + do_ref=0 + ref_idx 0 + closest_refs=0 -> invalid.
    pruning.closestRefs[0] = 0;
    CHECK(isValidUnipredRef(pruning, filtering, 0, 0, 0) == false);
    // enabled + do_ref=0 + ref_idx 1 -> invalid regardless of closest_refs.
    pruning.closestRefs[0] = 1;
    CHECK(isValidUnipredRef(pruning, filtering, 0, 0, 1) == false);
}

TEST_CASE("l16_rdo isValidBipredRef requires both refs flagged") {
    // mode_decision.c:820-846: enabled=0 -> true; otherwise both do_ref
    // flags must hold, with the closest_refs escape hatches.
    RefPruningCtrls pruning;
    pruning.enabled = 0;
    pruning.closestRefs[0] = 0;
    pruning.closestRefs[1] = 0;
    RefFilteringRes filtering;  // RefResults[11][2][4] (md_process.h:1136)
    std::memset(filtering, 0, sizeof(filtering));
    CHECK(isValidBipredRef(pruning, filtering, 0, 0, 0, 1, 0) == true);
    pruning.enabled = 1;
    // Both refs flagged -> valid.
    filtering[0][0][0].doRef = 1;
    filtering[0][1][0].doRef = 1;
    CHECK(isValidBipredRef(pruning, filtering, 0, 0, 0, 1, 0) == true);
    // First ref unflagged + closest_refs[0]=0 -> invalid.
    filtering[0][0][0].doRef = 0;
    CHECK(isValidBipredRef(pruning, filtering, 0, 0, 0, 1, 0) == false);
    // First ref unflagged + ref_idx_0=0 + closest_refs[0]=1 -> the :834
    // disjunct (ref_idx_0 || ref_idx_1) does not fire, the function falls
    // through to true.
    pruning.closestRefs[0] = 1;
    CHECK(isValidBipredRef(pruning, filtering, 0, 0, 0, 1, 0) == true);
    // Second ref unflagged + ref_idx_0 = 1 -> the :834 disjunct fires ->
    // invalid (both must hold or the closest/idx-0 escape covers them).
    filtering[0][1][0].doRef = 0;
    CHECK(isValidBipredRef(pruning, filtering, 0, 0, 1, 1, 0) == false);
}

TEST_CASE("l16_rdo isInterintraAllowed composes the RE1 helpers") {
    // mode_decision.c:102-106: enable_inter_intra && is_interintra_allowed_
    // bsize && is_interintra_allowed_mode && is_interintra_allowed_ref. The
    // bsize helper's CONFIG_ENABLE_INTER_INTRA gate threads as interIntraCfg
    // (the isInterIntraAllowedBsize enable parameter, rate_est.cpp:1024-1026;
    // the pinned build's value is 0, EbConfigMacros.h:67-69).
    const std::int32_t rfUni[2] = {1, 0};    // LAST_FRAME + NONE_FRAME
    const std::int32_t rfBi[2] = {1, 7};     // LAST_FRAME + ALTREF_FRAME
    const std::int32_t rfIntra[2] = {0, 0};  // INTRA_FRAME first
    // CONFIG off (the pinned build's setting) or enable off: always 0.
    CHECK(isInterintraAllowed(1, 0, BLOCK_16X16, NEWMV, rfUni) == 0);
    CHECK(isInterintraAllowed(0, 1, BLOCK_16X16, NEWMV, rfUni) == 0);
    // CONFIG on: the full conjunction.
    CHECK(isInterintraAllowed(1, 1, BLOCK_16X16, NEWMV, rfUni) == 1);
    CHECK(isInterintraAllowed(1, 1, BLOCK_16X16, NEWMV, rfBi) == 0);     // rf[1] > INTRA_FRAME
    CHECK(isInterintraAllowed(1, 1, BLOCK_16X16, NEWMV, rfIntra) == 0);  // rf[0] <= INTRA_FRAME
    CHECK(isInterintraAllowed(1, 1, BLOCK_16X16, DC_PRED, rfUni) == 0);  // mode below the single-inter range
    CHECK(isInterintraAllowed(1, 1, BLOCK_16X16, GLOBAL_GLOBALMV, rfUni) == 0);  // compound mode
    // The bsize range BLOCK_8X8..BLOCK_32X32 (mode_decision.h:146).
    CHECK(isInterintraAllowed(1, 1, BLOCK_4X4, NEWMV, rfUni) == 0);
    CHECK(isInterintraAllowed(1, 1, BLOCK_4X8, NEWMV, rfUni) == 0);
    CHECK(isInterintraAllowed(1, 1, BLOCK_8X8, NEWMV, rfUni) == 1);
    CHECK(isInterintraAllowed(1, 1, BLOCK_32X32, NEWMV, rfUni) == 1);
    CHECK(isInterintraAllowed(1, 1, BLOCK_32X64, NEWMV, rfUni) == 0);
}

TEST_CASE("l16_rdo isLosslessSegment gates on the threaded CONFIG") {
    // mode_decision.c:71-84. CONFIG_ENABLE_LOSSLESS threads as losslessCfg;
    // the pinned build's value is 0 (EbConfigMacros.h:88-90, the RTC block)
    // so the :72-75 branch returns false for EVERY input.
    std::uint8_t lossless[8] = {1, 0, 1, 0, 1, 0, 1, 0};
    CHECK(isLosslessSegment(0, lossless, 1, 1) == false);
    CHECK(isLosslessSegment(0, lossless, 0, 5) == false);
    CHECK(isLosslessSegment(0, lossless, 1, 2) == false);
    // The CONFIG=1 body (:77-82), quoted-but-dead under the pinned build:
    // segmentation_enabled picks lossless[segment_id] (:79), else
    // lossless[0] (:81).
    CHECK(isLosslessSegment(1, lossless, 1, 1) == false);  // :79 lossless[1]=0
    CHECK(isLosslessSegment(1, lossless, 1, 2) == true);   // :79 lossless[2]=1
    CHECK(isLosslessSegment(1, lossless, 0, 5) == true);   // :81 lossless[0]=1
    std::uint8_t lossless0[8] = {0, 1, 1, 1, 1, 1, 1, 1};
    CHECK(isLosslessSegment(1, lossless0, 1, 3) == true);   // :79 lossless[3]=1
    CHECK(isLosslessSegment(1, lossless0, 0, 3) == false);  // :81 lossless[0]=0
}

// ---------------------------------------------------------------------------
// MD1c: the ME offset tables + walk (mode_decision.c:132-189).
// ---------------------------------------------------------------------------
TEST_CASE("l16_rdo me conversion tables transcribe the reference") {
    // me_idx_85 (mode_decision.h:167-170), me_idx_85_8x8_to_16x16_conversion
    // (coefficients.c:291-303), me_idx_16x16_to_parent_32x32_conversion
    // (coefficients.c:304-310), byte-for-byte.
    static const std::uint32_t want85[85] = {
        0,  1,  5,  21, 22, 29, 30, 6,  23, 24, 31, 32, 9,  37, 38, 45, 46, 10, 39, 40, 47, 48, 2,  7,  25, 26, 33, 34, 8,
        27, 28, 35, 36, 11, 41, 42, 49, 50, 12, 43, 44, 51, 52, 3,  13, 53, 54, 61, 62, 14, 55, 56, 63, 64, 17, 69, 70, 77,
        78, 18, 71, 72, 79, 80, 4,  15, 57, 58, 65, 66, 16, 59, 60, 67, 68, 19, 73, 74, 81, 82, 20, 75, 76, 83, 84};
    static const std::uint8_t wantConv8x8[64] = {
        5, 5, 6, 6, 7, 7, 8, 8,
        5, 5, 6, 6, 7, 7, 8, 8,
        9, 9, 10, 10, 11, 11, 12, 12,
        9, 9, 10, 10, 11, 11, 12, 12,
        13, 13, 14, 14, 15, 15, 16, 16,
        13, 13, 14, 14, 15, 15, 16, 16,
        17, 17, 18, 18, 19, 19, 20, 20,
        17, 17, 18, 18, 19, 19, 20, 20};
    static const std::uint8_t wantConv16x16[16] = {
        1, 1, 2, 2,
        1, 1, 2, 2,
        3, 3, 4, 4,
        3, 3, 4, 4};
    CHECK(std::memcmp(l16_rdo::meIdx85, want85, sizeof(want85)) == 0);
    CHECK(std::memcmp(l16_rdo::meIdx8x8To16x16Conversion, wantConv8x8, sizeof(wantConv8x8)) == 0);
    CHECK(std::memcmp(l16_rdo::meIdx16x16ToParent32x32Conversion, wantConv16x16, sizeof(wantConv16x16)) == 0);
}

TEST_CASE("l16_rdo getMeBlockOffset walks the me_idx_85 fallthrough") {
    // mode_decision.c:132-189. The switch accumulates over the fallthrough
    // chain (case 4/8 -> case 16 -> case 32), then me_idx_85[me_idx]
    // converts (:173); enable_me_8x8 = 0 folds offsets >= 21 through
    // me_idx_85_8x8_to_16x16_conversion (:175-178) and enable_me_16x16 = 0
    // folds >= 5 through me_idx_16x16_to_parent_32x32_conversion (:180-185).
    // 8x8-class (max_length 4 or 8): me_idx = 3 + (org_x & 8 ? 1 : 0)
    // + 2*(org_y & 8 ? 1 : 0) - every stage of the chain increments once.
    CHECK(getMeBlockOffset(0, 0, BLOCK_8X8, 1, 1) == 21);   // me_idx 3 -> [3] = 21
    CHECK(getMeBlockOffset(8, 0, BLOCK_8X8, 1, 1) == 22);   // me_idx 4 -> [4] = 22
    CHECK(getMeBlockOffset(0, 8, BLOCK_8X8, 1, 1) == 29);   // me_idx 5 -> [5] = 29
    CHECK(getMeBlockOffset(8, 8, BLOCK_8X8, 1, 1) == 30);   // me_idx 6 -> [6] = 30
    // (the fallthrough chain: case8 ++ +1 +2 = 4, case16 ++ = 5, case32
    // ++ = 6; me_idx_85[6] = 30, mode_decision.h:167-170)
    CHECK(getMeBlockOffset(0, 0, BLOCK_4X4, 1, 1) == 21);   // max_length 4, same chain
    CHECK(getMeBlockOffset(0, 0, BLOCK_8X4, 1, 1) == 21);   // max_length 8 (4x8/8x4)
    // 16x16-class: me_idx = 2 + 5*(org_x & 16) + 10*(org_y & 16).
    CHECK(getMeBlockOffset(0, 0, BLOCK_16X16, 1, 1) == 5);    // me_idx 2 -> [2] = 5
    CHECK(getMeBlockOffset(16, 0, BLOCK_16X16, 1, 1) == 6);   // me_idx 7 -> [7] = 6
    CHECK(getMeBlockOffset(0, 16, BLOCK_16X16, 1, 1) == 9);   // me_idx 12 -> [12] = 9
    CHECK(getMeBlockOffset(16, 16, BLOCK_16X16, 1, 1) == 10); // me_idx 17 -> [17] = 10
    CHECK(getMeBlockOffset(16, 0, BLOCK_16X8, 1, 1) == 6);    // max_length 16
    // 32x32-class: me_idx = 1 + 21*(org_x & 32) + 42*(org_y & 32) - the
    // lower org bits do NOT contribute at max_length 32 (the switch jumps
    // straight to case 32).
    CHECK(getMeBlockOffset(0, 0, BLOCK_32X32, 1, 1) == 1);    // me_idx 1 -> [1] = 1
    CHECK(getMeBlockOffset(32, 0, BLOCK_32X32, 1, 1) == 2);   // me_idx 22 -> [22] = 2
    CHECK(getMeBlockOffset(0, 32, BLOCK_32X32, 1, 1) == 3);   // me_idx 43 -> [43] = 3
    CHECK(getMeBlockOffset(32, 32, BLOCK_32X32, 1, 1) == 4);  // me_idx 64 -> [64] = 4
    CHECK(getMeBlockOffset(8, 32, BLOCK_32X32, 1, 1) == 3);   // org_x & 8 ignored at 32
    // 64x64 and larger: the default keeps me_idx 0 -> [0] = 0 (:168-170).
    CHECK(getMeBlockOffset(0, 0, BLOCK_64X64, 1, 1) == 0);
    CHECK(getMeBlockOffset(40, 40, BLOCK_64X64, 1, 1) == 0);
    CHECK(getMeBlockOffset(0, 0, BLOCK_128X128, 1, 1) == 0);
    // The disable-8x8 fold (:175-186).
    CHECK(getMeBlockOffset(0, 0, BLOCK_8X8, 0, 1) == 5);     // 21 -> conv8x8[0] = 5
    CHECK(getMeBlockOffset(0, 0, BLOCK_8X8, 0, 0) == 1);     // 5 -> conv16x16[0] = 1
    CHECK(getMeBlockOffset(0, 16, BLOCK_16X16, 0, 0) == 1);  // 9 -> conv16x16[4] = 1
    CHECK(getMeBlockOffset(32, 32, BLOCK_32X32, 0, 0) == 4); // 4 < 21, 4 < 5 -> stays 4
    CHECK(getMeBlockOffset(0, 16, BLOCK_16X16, 0, 1) == 9);  // 9 < 21 -> stays, 16x16 on
}

// ---------------------------------------------------------------------------
// MD1c: the ME data-present scan (mode_decision.c:193-212).
// ---------------------------------------------------------------------------
TEST_CASE("l16_rdo isMeDataPresent scans the per-PU candidate list") {
    // mode_decision.c:193-212: direction 0 consults (ref0_list, ref_idx_l0),
    // direction 1 (ref1_list, ref_idx_l1), direction 2 both (:200-209).
    std::uint8_t totalCounts[4] = {2, 1, 0, 3};
    MeCandidate cands[6] = {};
    cands[1].direction = 0; cands[1].ref0List = 0; cands[1].refIdxL0 = 1;
    cands[2].direction = 2; cands[2].ref0List = 0; cands[2].refIdxL0 = 3;
    cands[2].ref1List = 1;  cands[2].refIdxL1 = 0;
    cands[4].direction = 1; cands[4].ref1List = 0; cands[4].refIdxL1 = 2;
    MeSbResults results = {totalCounts, cands};
    CHECK(isMeDataPresent(0, 1, &results, 0, 1) == 1);  // cand0 direction 0 match
    CHECK(isMeDataPresent(0, 1, &results, 0, 3) == 1);  // cand1 direction 2 l0 match
    CHECK(isMeDataPresent(0, 1, &results, 1, 0) == 1);  // cand1 direction 2 l1 match
    CHECK(isMeDataPresent(0, 1, &results, 1, 3) == 0);  // direction 0 does not consult l1
    CHECK(isMeDataPresent(0, 1, &results, 0, 2) == 0);  // no match
    CHECK(isMeDataPresent(2, 0, &results, 0, 0) == 0);  // total count 0 -> loop never runs
    CHECK(isMeDataPresent(3, 4, &results, 0, 2) == 1);  // direction 1 l1 match
    CHECK(isMeDataPresent(3, 4, &results, 1, 2) == 0);  // direction 1 does not consult l0
}

// ---------------------------------------------------------------------------
// MD1c: the injected-MV redundancy walk (mode_decision.c:739-787).
// ---------------------------------------------------------------------------
namespace {
// The Mv union (mv.h:42-49): x = low 16 bits, y = high 16 bits of as_int.
static std::int32_t packMv(int x, int y) {
    return (std::int32_t)(((std::uint32_t)(std::uint16_t)y << 16) | (std::uint32_t)(std::uint16_t)x);
}
}  // namespace

TEST_CASE("l16_rdo mvIsAlreadyInjected gates the injected-mv walk") {
    // mode_decision.c:739-787. av1_set_ref_frame (inter_prediction.h:487-496)
    // sets rf[1] = NONE_FRAME (-1) for ref_type < TOTAL_REFS_PER_FRAME (8,
    // definitions.h:1377) and a ref_frame_map second entry (all >
    // INTRA_FRAME) for compounds - so the :744 `rf[1] <= INTRA_FRAME` test
    // reduces to refType < 8.
    InjectedMvCarrier carrier = {};
    carrier.injectedMvCount = 2;
    carrier.injectedRefTypes[0] = 3;
    carrier.injectedRefTypes[1] = 5;
    carrier.injectedMvs[0][0] = packMv(100, -50);
    carrier.injectedMvs[1][0] = packMv(7, 9);
    // Unipred exact-match walk (:750-754): ref type AND mv0 packed compare.
    CHECK(mvIsAlreadyInjected(carrier, packMv(100, -50), 0, 3) == true);
    CHECK(mvIsAlreadyInjected(carrier, packMv(101, -50), 0, 3) == false);
    CHECK(mvIsAlreadyInjected(carrier, packMv(7, 9), 0, 5) == true);
    CHECK(mvIsAlreadyInjected(carrier, packMv(7, 9), 0, 3) == false);  // ref type must match
    // The corrupted-MV early exit (:746-748): x = 16384 is outside the
    // is_mv_valid range at need_shift 0 (inter_prediction.h:246-248).
    carrier.corruptedMvCheck = 1;
    CHECK(mvIsAlreadyInjected(carrier, packMv(16384, 0), 0, 3) == true);
    carrier.corruptedMvCheck = 0;
    CHECK(mvIsAlreadyInjected(carrier, packMv(16384, 0), 0, 3) == false);
    // Bipred exact walk (:776-784, score_th 0): BOTH mvs compare.
    InjectedMvCarrier bi = {};
    bi.injectedMvCount = 1;
    bi.injectedRefTypes[0] = 9;
    bi.injectedMvs[0][0] = packMv(500, 500);
    bi.injectedMvs[0][1] = packMv(-500, 500);
    CHECK(mvIsAlreadyInjected(bi, packMv(500, 500), packMv(-500, 500), 9) == true);
    CHECK(mvIsAlreadyInjected(bi, packMv(500, 500), packMv(-500, 501), 9) == false);
    CHECK(mvIsAlreadyInjected(bi, packMv(500, 500), packMv(-500, 500), 10) == false);  // ref type must match
    // Bipred score walk (:762-775): score_th 10, mag_th 100; the score is
    // the L1 distance of both packed pairs against the injected pair.
    bi.redundCtrl.scoreTh = 10;
    bi.redundCtrl.magTh = 100;
    CHECK(mvIsAlreadyInjected(bi, packMv(500, 500), packMv(-500, 500), 9) == true);   // score 0
    CHECK(mvIsAlreadyInjected(bi, packMv(504, 505), packMv(-500, 500), 9) == true);   // score 9 < 10, high mag
    CHECK(mvIsAlreadyInjected(bi, packMv(505, 505), packMv(-500, 500), 9) == false);  // score 10, not < 10
    // The is_high_mag conjunct (:763-764): score < th but a component at or
    // below mag_th is NOT redundant.
    InjectedMvCarrier lo = {};
    lo.injectedMvCount = 1;
    lo.injectedRefTypes[0] = 9;
    lo.injectedMvs[0][0] = packMv(5, 5);
    lo.injectedMvs[0][1] = packMv(-5, 5);
    lo.redundCtrl.scoreTh = 10;
    lo.redundCtrl.magTh = 100;
    CHECK(mvIsAlreadyInjected(lo, packMv(9, 5), packMv(-5, 5), 9) == false);  // score 4, not high mag
    lo.redundCtrl.magTh = 3;
    CHECK(mvIsAlreadyInjected(lo, packMv(9, 5), packMv(-5, 5), 9) == true);   // score 4 < 10, high mag
    // Bipred corrupted check (:757-758): either invalid MV exits true.
    bi.corruptedMvCheck = 1;
    CHECK(mvIsAlreadyInjected(bi, packMv(504, 505), packMv(16384, 0), 9) == true);
}
