// l16_rdo MD1 - the A4 mode-decision pure-arithmetic surface.
// GREEN step of the MD1 slice. Every body is a verbatim transcription of its
// cited reference span with the ModeDecisionContext/pcs walls threaded as
// carrier structs (the RE8A DeblockContext precedent); see md_slices.h for
// the span map and the carrier field citations.
#include "md_slices.h"

#include <cstdlib>

namespace l16_rdo {

// ---------------------------------------------------------------------
// MD1a: the SSIM family.
// ---------------------------------------------------------------------

// The cc1/cc2 family (enc_dec_process.c:638-643, verbatim).
static const std::int64_t cc1 = 26634;      // (64^2*(.01*255)^2
static const std::int64_t cc2 = 239708;     // (64^2*(.03*255)^2
static const std::int64_t cc1_10 = 428658;  // (64^2*(.01*1023)^2
static const std::int64_t cc2_10 = 3857925; // (64^2*(.03*1023)^2
static const std::int64_t cc1_12 = 6868593; // (64^2*(.01*4095)^2
static const std::int64_t cc2_12 = 61817334; // (64^2*(.03*4095)^2

// svt_aom_similarity (enc_dec_process.c:645-671, verbatim).
double similarity(std::uint32_t sumS, std::uint32_t sumR, std::uint32_t sumSqS,
                  std::uint32_t sumSqR, std::uint32_t sumSxr, std::int32_t count,
                  std::uint32_t bd) {
    double ssimN, ssimD;
    std::int64_t c1, c2;

    if (bd == 8) {
        // scale the constants by number of pixels
        c1 = (cc1 * count * count) >> 12;
        c2 = (cc2 * count * count) >> 12;
    } else if (bd == 10) {
        c1 = (cc1_10 * count * count) >> 12;
        c2 = (cc2_10 * count * count) >> 12;
    } else if (bd == 12) {
        c1 = (cc1_12 * count * count) >> 12;
        c2 = (cc2_12 * count * count) >> 12;
    } else {
        c1 = c2 = 0;
        // enc_dec_process.c:662 assert(0) - NDEBUG-dropped convention.
    }

    // enc_dec_process.c:665-668 - the expression shape is load-bearing for
    // the double rounding; do not reassociate.
    ssimN = (2.0 * sumS * sumR + c1) * (2.0 * count * sumSxr - 2.0 * sumS * sumR + c2);

    ssimD = ((double)sumS * sumS + (double)sumR * sumR + c1) *
        ((double)count * sumSqS - (double)sumS * sumS + (double)count * sumSqR - (double)sumR * sumR + c2);

    return ssimN / ssimD;
}

// svt_ssim_4x4_c (mode_decision.c:4238-4261, verbatim).
double ssim4x4(const std::uint8_t* s, std::uint32_t sp, const std::uint8_t* r, std::uint32_t rp) {
    const std::int32_t count = 4 * 4;

    std::uint32_t sumS = 0, sumR = 0, sumSqS = 0, sumSqR = 0, sumSxr = 0;
    std::uint32_t i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            sumS += s[j];
            sumR += r[j];
            sumSqS += (std::uint32_t)s[j] * s[j];
            sumSqR += (std::uint32_t)r[j] * r[j];
            sumSxr += (std::uint32_t)s[j] * r[j];
        }

        s += sp;
        r += rp;
    }

    double score = similarity(sumS, sumR, sumSqS, sumSqR, sumSxr, count, 8);
    return score;
}

// svt_ssim_8x8_c (mode_decision.c:4263-4289, verbatim).
double ssim8x8(const std::uint8_t* s, std::uint32_t sp, const std::uint8_t* r, std::uint32_t rp) {
    const std::int32_t count = 8 * 8;

    std::uint32_t sumS = 0, sumR = 0, sumSqS = 0, sumSqR = 0, sumSxr = 0;
    std::uint32_t i, j;
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            sumS += s[j];
            sumR += r[j];
            sumSqS += (std::uint32_t)s[j] * s[j];
            sumSqR += (std::uint32_t)r[j] * r[j];
            sumSxr += (std::uint32_t)s[j] * r[j];
        }

        s += sp;
        r += rp;
    }

    double score = similarity(sumS, sumR, sumSqS, sumSqR, sumSxr, count, 8);
    return score;
}

// svt_ssim_4x4_hbd_c (mode_decision.c:4291-4314, verbatim).
double ssim4x4Hbd(const std::uint16_t* s, std::uint32_t sp, const std::uint16_t* r,
                  std::uint32_t rp) {
    const std::int32_t count = 4 * 4;

    std::uint32_t sumS = 0, sumR = 0, sumSqS = 0, sumSqR = 0, sumSxr = 0;
    std::uint32_t i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            sumS += s[j];
            sumR += r[j];
            sumSqS += (std::uint32_t)s[j] * s[j];
            sumSqR += (std::uint32_t)r[j] * r[j];
            sumSxr += (std::uint32_t)s[j] * r[j];
        }

        s += sp;
        r += rp;
    }

    double score = similarity(sumS, sumR, sumSqS, sumSqR, sumSxr, count, 10);
    return score;
}

// svt_ssim_8x8_hbd_c (mode_decision.c:4316-4339, verbatim).
double ssim8x8Hbd(const std::uint16_t* s, std::uint32_t sp, const std::uint16_t* r,
                  std::uint32_t rp) {
    const std::int32_t count = 8 * 8;

    std::uint32_t sumS = 0, sumR = 0, sumSqS = 0, sumSqR = 0, sumSxr = 0;
    std::uint32_t i, j;
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            sumS += s[j];
            sumR += r[j];
            sumSqS += (std::uint32_t)s[j] * s[j];
            sumSqR += (std::uint32_t)r[j] * r[j];
            sumSxr += (std::uint32_t)s[j] * r[j];
        }

        s += sp;
        r += rp;
    }

    double score = similarity(sumS, sumR, sumSqS, sumSqR, sumSxr, count, 10);
    return score;
}

// ssim_8x8_blocks (mode_decision.c:4341-4360, verbatim; CLIP3(0, 1, v)
// inlined; the :4356/:4358 asserts are NDEBUG-dropped). Reference-static,
// exported for testability (the DF1 cdefSign precedent).
double ssim8x8Blocks(const std::uint8_t* s, std::uint32_t sp, const std::uint8_t* r,
                     std::uint32_t rp, std::uint32_t width, std::uint32_t height) {
    std::uint32_t i, j;
    int samples = 0;
    double ssimTotal = 0;

    // sample point start with each 4x4 location
    for (i = 0; i <= height - 8; i += 8, s += sp * 8, r += rp * 8) {
        for (j = 0; j <= width - 8; j += 8) {
            double v = ssim8x8(s + j, sp, r + j, rp);
            v = v < 0 ? 0 : (v > 1 ? 1 : v);  // CLIP3(0, 1, v)
            ssimTotal += v;
            samples++;
        }
    }
    ssimTotal /= samples;
    return ssimTotal;
}

// ssim_4x4_blocks (mode_decision.c:4362-4381, verbatim; CLIP3 inlined;
// the :4377/:4379 asserts are NDEBUG-dropped). Reference-static, exported
// for testability (the DF1 cdefSign precedent).
double ssim4x4Blocks(const std::uint8_t* s, std::uint32_t sp, const std::uint8_t* r,
                     std::uint32_t rp, std::uint32_t width, std::uint32_t height) {
    std::uint32_t i, j;
    int samples = 0;
    double ssimTotal = 0;

    // sample point start with each 2x2 location
    for (i = 0; i <= height - 4; i += 4, s += sp * 4, r += rp * 4) {
        for (j = 0; j <= width - 4; j += 4) {
            double v = ssim4x4(s + j, sp, r + j, rp);
            v = v < 0 ? 0 : (v > 1 ? 1 : v);  // CLIP3(0, 1, v)
            ssimTotal += v;
            samples++;
        }
    }
    ssimTotal /= samples;
    return ssimTotal;
}

// ssim (mode_decision.c:4383-4390, verbatim; the :4384 assert is
// NDEBUG-dropped).
double ssim(const std::uint8_t* s, std::uint32_t sp, const std::uint8_t* r, std::uint32_t rp,
            std::uint32_t width, std::uint32_t height) {
    if ((width % 8) == 0 && (height % 8) == 0) {
        return ssim8x8Blocks(s, sp, r, rp, width, height);
    } else {
        return ssim4x4Blocks(s, sp, r, rp, width, height);
    }
}

// ssim_8x8_blocks_hbd (mode_decision.c:4392-4411, verbatim). Reference-
// static, exported for testability (the DF1 cdefSign precedent).
double ssim8x8BlocksHbd(const std::uint16_t* s, std::uint32_t sp, const std::uint16_t* r,
                        std::uint32_t rp, std::uint32_t width, std::uint32_t height) {
    std::uint32_t i, j;
    int samples = 0;
    double ssimTotal = 0;

    for (i = 0; i <= height - 8; i += 8, s += sp * 8, r += rp * 8) {
        for (j = 0; j <= width - 8; j += 8) {
            double v = ssim8x8Hbd(s + j, sp, r + j, rp);
            v = v < 0 ? 0 : (v > 1 ? 1 : v);  // CLIP3(0, 1, v)
            ssimTotal += v;
            samples++;
        }
    }
    ssimTotal /= samples;
    return ssimTotal;
}

// ssim_4x4_blocks_hbd (mode_decision.c:4413-4432, verbatim). Reference-
// static, exported for testability (the DF1 cdefSign precedent).
double ssim4x4BlocksHbd(const std::uint16_t* s, std::uint32_t sp, const std::uint16_t* r,
                        std::uint32_t rp, std::uint32_t width, std::uint32_t height) {
    std::uint32_t i, j;
    int samples = 0;
    double ssimTotal = 0;

    for (i = 0; i <= height - 4; i += 4, s += sp * 4, r += rp * 4) {
        for (j = 0; j <= width - 4; j += 4) {
            double v = ssim4x4Hbd(s + j, sp, r + j, rp);
            v = v < 0 ? 0 : (v > 1 ? 1 : v);  // CLIP3(0, 1, v)
            ssimTotal += v;
            samples++;
        }
    }
    ssimTotal /= samples;
    return ssimTotal;
}

// ssim_hbd (mode_decision.c:4434-4442, verbatim; the :4436 assert is
// NDEBUG-dropped).
double ssimHbd(const std::uint16_t* s, std::uint32_t sp, const std::uint16_t* r, std::uint32_t rp,
               std::uint32_t width, std::uint32_t height) {
    if ((width % 8) == 0 && (height % 8) == 0) {
        return ssim8x8BlocksHbd(s, sp, r, rp, width, height);
    } else {
        return ssim4x4BlocksHbd(s, sp, r, rp, width, height);
    }
}

// svt_spatial_full_distortion_ssim_kernel (mode_decision.c:4444-4490,
// verbatim). DEVIATION, named: the hbd branch's ac-bias join
// (:4473-4483) sits under #if CONFIG_ENABLE_HIGH_BIT_DEPTH and the pinned
// build's value is 0 (EbConfigMacros.h:55-57 RTC block; the
// rate_est.cpp:2225 convention), so the hbd branch computes the ssim_hbd
// score only and psyDistortion stays 0 - svt_psy_distortion_hbd is ported
// separately (below) as the RE4 hbd park resolving, but is not wired into
// this kernel.
std::uint64_t spatialFullDistortionSsimKernel(const std::uint8_t* input, std::uint32_t inputOffset,
                                              std::uint32_t inputStride, const std::uint8_t* recon,
                                              std::int32_t reconOffset, std::uint32_t reconStride,
                                              std::uint32_t areaWidth, std::uint32_t areaHeight,
                                              bool hbd, double acBias) {
    std::uint8_t m = 1;
    const std::uint32_t count = areaWidth * areaHeight;

    // SSIM
    std::uint64_t spatialDistortion;
    double ssimScore;

    // AC SAD (the reference's local `psy_distortion` is renamed
    // psyDistortionVal: the psyDistortion function name is in scope here)
    std::uint64_t psyDistortionVal = 0;

    if (!hbd) {
        ssimScore = ssim(input + inputOffset, inputStride, recon + reconOffset, reconStride,
                         areaWidth, areaHeight);
        if (acBias) {
            std::uint64_t acDistortion = psyDistortion(input + inputOffset, inputStride,
                                                       recon + reconOffset, reconStride,
                                                       areaWidth, areaHeight);
            psyDistortionVal = (std::uint64_t)(acDistortion * acBias);
        }
    } else {
        m = 8;
        ssimScore = ssimHbd((const std::uint16_t*)input + inputOffset, inputStride,
                            (const std::uint16_t*)recon + reconOffset, reconStride,
                            areaWidth, areaHeight);
        // mode_decision.c:4473-4483 (#if CONFIG_ENABLE_HIGH_BIT_DEPTH):
        // compiled out under the pinned build's CONFIG=0 - the hbd ac-bias
        // join is parked; psyDistortionVal stays 0.
    }

    spatialDistortion = (std::uint64_t)((1 - ssimScore) * count * 100 * 7 * m);
    std::uint64_t totalDistortion = spatialDistortion + psyDistortionVal;

    return totalDistortion;
}

// ---------------------------------------------------------------------
// MD1b: the hbd psy chain.
// ---------------------------------------------------------------------

// hadamard_highbd_col8_first_pass (picture_operators_c.c:176-203, verbatim).
void hadamardHighbdCol8FirstPass(const std::int16_t* srcDiff, std::ptrdiff_t srcStride,
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

// hadamard_highbd_col8_second_pass (picture_operators_c.c:207-234, verbatim).
void hadamardHighbdCol8SecondPass(const std::int16_t* srcDiff, std::ptrdiff_t srcStride,
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

// svt_aom_highbd_hadamard_8x8_c (picture_operators_c.c:238-262, verbatim;
// the :259-:262 tail copy is the loop below).
void highbdHadamard8x8(const std::int16_t* srcDiff, std::ptrdiff_t srcStride,
                       std::int32_t* coeff) {
    std::int16_t buffer[64];
    std::int32_t buffer2[64];
    std::int16_t* tmpBuf = &buffer[0];
    for (int idx = 0; idx < 8; ++idx) {
        hadamardHighbdCol8FirstPass(srcDiff, srcStride, tmpBuf);
        tmpBuf += 8;
        ++srcDiff;
    }

    tmpBuf = &buffer[0];
    for (int idx = 0; idx < 8; ++idx) {
        hadamardHighbdCol8SecondPass(tmpBuf, 8, buffer2 + 8 * idx);
        ++tmpBuf;
    }

    for (int idx = 0; idx < 64; ++idx) {
        coeff[idx] = buffer2[idx];
    }
}

// svt_psy_distortion_hbd (ac_bias.c:103-144, verbatim). DEVIATION, named:
// the reference span sits under #if CONFIG_ENABLE_HIGH_BIT_DEPTH
// (ac_bias.c:101) and the pinned build's CONFIG is 0 (EbConfigMacros.h
// :55-57; the rate_est.cpp:2225 convention) - MD1 ports the body anyway
// (the RE4 hbd park resolves, the TF1->TF2 precedent) as a standalone
// helper; the only upstream caller (get_svt_psy_full_dist :155 and the
// SSIM kernel :4475) is compiled out under the pinned CONFIG.
std::uint64_t psyDistortionHbd(const std::uint16_t* input, std::uint32_t inputStride,
                               const std::uint16_t* recon, std::uint32_t reconStride,
                               std::uint32_t width, std::uint32_t height) {
    std::uint64_t energyGap = 0;

    if (width >= 8 && height >= 8) { /* >8x8 */
        for (std::uint32_t j = 0; j < height; j += 8) {
            for (std::uint32_t i = 0; i < width; i += 8) {
                std::int32_t coeffs[64];

                highbdHadamard8x8((const std::int16_t*)input + j * inputStride + i, inputStride, coeffs);

                std::int32_t inputEnergy = ((satd(coeffs, 64) + 2) >> 2) - ((coeffs[0] + 2) >> 2);

                highbdHadamard8x8((const std::int16_t*)recon + j * reconStride + i, reconStride, coeffs);

                std::int32_t reconEnergy = ((satd(coeffs, 64) + 2) >> 2) - ((coeffs[0] + 2) >> 2);

                energyGap += (std::uint64_t)std::abs(inputEnergy - reconEnergy);
            }
        }
    } else {
        for (std::uint64_t j = 0; j < height; j += 4) { /* 4x4, 4x8, 4x16, 8x4, and 16x4 */
            for (std::uint64_t i = 0; i < width; i += 4) {
                std::int32_t coeffs[16];

                // HBD coefficients can fit in 16 bits, so the regular Hadamard
                // 4x4 function can be used here safely (ac_bias.c:128)
                hadamard4x4((const std::int16_t*)input + j * inputStride + i, inputStride, coeffs);

                std::int32_t inputEnergy = (satd(coeffs, 16) << 1) - coeffs[0];

                hadamard4x4((const std::int16_t*)recon + j * reconStride + i, reconStride, coeffs);

                std::int32_t reconEnergy = (satd(coeffs, 16) << 1) - coeffs[0];

                energyGap += (std::uint64_t)std::abs(inputEnergy - reconEnergy);
            }
        }
    }

    // Energy is scaled to approximately match equivalent 8-bit strengths
    return energyGap << 2;
}

// ---------------------------------------------------------------------
// MD1c: the MV/ME helpers.
// ---------------------------------------------------------------------

// check_mv_validity (mode_decision.c:86-100, verbatim; is_mv_valid inlined
// from inter_prediction.h:246-248, MV_LOW/MV_UPP from rate_est.h:834-837).
bool checkMvValidity(std::int16_t xMv, std::int16_t yMv, std::uint8_t needShift) {
    //go to 1/8th if input is 1/4pel
    const std::int16_t y = (std::int16_t)(yMv << needShift);
    const std::int16_t x = (std::int16_t)(xMv << needShift);
    /* AV1 limits
      -16384 < MV_x_in_1/8 or MV_y_in_1/8 < 16384
      which means in full pel:
      -2048 < MV_x_in_full_pel or MV_y_in_full_pel < 2048
    */
    return y > MV_LOW && y < MV_UPP && x > MV_LOW && x < MV_UPP;
}

// svt_is_interintra_allowed (mode_decision.c:102-106, verbatim over the
// RE1-ported helpers).
std::int32_t isInterintraAllowed(std::uint8_t enableInterIntra, std::uint8_t interIntraCfg,
                                 BlockSize bsize, PredictionMode mode, const std::int32_t refFrame[2]) {
    return enableInterIntra && isInterIntraAllowedBsize(interIntraCfg, bsize) &&
        isInterIntraAllowedMode(mode) && isInterIntraAllowedRef(refFrame);
}

// svt_av1_is_lossless_segment (mode_decision.c:71-84, verbatim; the
// CONFIG_ENABLE_LOSSLESS threads as losslessCfg - the pinned build's value
// is 0, EbConfigMacros.h:88-90, which selects the :72-75 branch).
bool isLosslessSegment(std::uint8_t losslessCfg, const std::uint8_t* lossless,
                       std::uint8_t segEnabled, std::int8_t segmentId) {
    if (!losslessCfg) {  // mode_decision.c:72-75 (#if !CONFIG_ENABLE_LOSSLESS)
        (void)lossless;
        (void)segEnabled;
        (void)segmentId;
        return false;
    }
    // mode_decision.c:77-82 (the CONFIG=1 body, quoted-but-dead under the
    // pinned build).
    if (segEnabled) {
        return lossless[segmentId] != 0;
    } else {
        return lossless[0] != 0;
    }
}

// The me_idx_85 table (mode_decision.h:167-170, verbatim).
const std::uint32_t meIdx85[85] = {
    0,  1,  5,  21, 22, 29, 30, 6,  23, 24, 31, 32, 9,  37, 38, 45, 46, 10, 39, 40, 47, 48, 2,  7,  25, 26, 33, 34, 8,
    27, 28, 35, 36, 11, 41, 42, 49, 50, 12, 43, 44, 51, 52, 3,  13, 53, 54, 61, 62, 14, 55, 56, 63, 64, 17, 69, 70, 77,
    78, 18, 71, 72, 79, 80, 4,  15, 57, 58, 65, 66, 16, 59, 60, 67, 68, 19, 73, 74, 81, 82, 20, 75, 76, 83, 84};
// me_idx_85_8x8_to_16x16_conversion (coefficients.c:291-303, verbatim).
const std::uint8_t meIdx8x8To16x16Conversion[64] = {
    5, 5, 6, 6, 7, 7, 8, 8,
    5, 5, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12,
    9, 9, 10, 10, 11, 11, 12, 12,
    13, 13, 14, 14, 15, 15, 16, 16,
    13, 13, 14, 14, 15, 15, 16, 16,
    17, 17, 18, 18, 19, 19, 20, 20,
    17, 17, 18, 18, 19, 19, 20, 20};
// me_idx_16x16_to_parent_32x32_conversion (coefficients.c:304-310, verbatim).
const std::uint8_t meIdx16x16ToParent32x32Conversion[16] = {
    1, 1, 2, 2,
    1, 1, 2, 2,
    3, 3, 4, 4,
    3, 3, 4, 4};

// svt_aom_get_me_block_offset (mode_decision.c:132-189, verbatim; the
// :179/:182 asserts are NDEBUG-dropped).
std::uint32_t getMeBlockOffset(std::uint32_t orgX, std::uint32_t orgY, BlockSize bsize,
                               std::uint8_t enableMe8x8, std::uint8_t enableMe16x16) {
    const int bwidth = blockSizeWide[bsize];
    const int bheight = blockSizeHigh[bsize];
    const std::uint32_t maxLength = (std::uint32_t)(bwidth > bheight ? bwidth : bheight);  // MAX

    std::uint32_t meIdx = 0;
    switch (maxLength) {
    case 4:
    case 8:
        meIdx++;
        if (orgX & 8) { // (org_x % 16) / 8
            meIdx += 1;
        }
        if (orgY & 8) { // (org_y % 16) / 8
            meIdx += 2;
        }
        // AOM_FALLTHROUGH_INTENDED (mode_decision.c:149)
        // fallthrough
    case 16:
        meIdx++;
        if (orgX & 16) { // (org_x % 32) / 16
            meIdx += 5;
        }
        if (orgY & 16) { // (org_y % 32) / 16
            meIdx += 10;
        }
        // AOM_FALLTHROUGH_INTENDED (mode_decision.c:158)
        // fallthrough
    case 32:
        meIdx++;
        if (orgX & 32) { // (org_x % 64) / 32
            meIdx += 21;
        }
        if (orgY & 32) { // (org_y % 64) / 32
            meIdx += 42;
        }
        break;
    default:
        // me_idx = 0;
        break;
    }

    std::uint32_t meBlockOffset = meIdx85[meIdx]; // convert idx to me_idx

    if (!enableMe8x8) {
        if (meBlockOffset >= MAX_SB64_PU_COUNT_NO_8X8) {
            meBlockOffset = meIdx8x8To16x16Conversion[meBlockOffset - MAX_SB64_PU_COUNT_NO_8X8];
        }
        if (!enableMe16x16) {
            if (meBlockOffset >= MAX_SB64_PU_COUNT_WO_16X16) {
                meBlockOffset = meIdx16x16ToParent32x32Conversion[meBlockOffset - MAX_SB64_PU_COUNT_WO_16X16];
            }
        }
    }

    return meBlockOffset;
}

// svt_aom_is_me_data_present (mode_decision.c:193-212, verbatim; the :199
// assert is NDEBUG-dropped; total_me_cnt widens uint8 -> uint32, value-
// identical after promotion, so the uint32 loop counter compares unsigned
// to unsigned under /W4).
std::uint8_t isMeDataPresent(std::uint32_t meBlockOffset, std::uint32_t meCandOffset,
                             const MeSbResults* meResults, std::uint8_t listIdx,
                             std::uint8_t refIdx) {
    const std::uint32_t totalMeCnt = meResults->totalMeCandidateIndex[meBlockOffset];
    const MeCandidate* meBlockResults = &meResults->meCandidateArray[meCandOffset];
    for (std::uint32_t meCandI = 0; meCandI < totalMeCnt; ++meCandI) {
        const MeCandidate* meCand = &meBlockResults[meCandI];
        if (meCand->direction == 0 || meCand->direction == 2) {
            if (listIdx == meCand->ref0List && refIdx == meCand->refIdxL0) {
                return 1;
            }
        }
        if (meCand->direction == 1 || meCand->direction == 2) {
            if (listIdx == meCand->ref1List && refIdx == meCand->refIdxL1) {
                return 1;
            }
        }
    }
    return 0;
}

// svt_aom_have_newmv_in_inter_mode (mode_decision.c:274-277, verbatim).
std::int32_t haveNewmvInInterMode(PredictionMode mode) {
    return (mode == NEWMV || mode == NEW_NEWMV || mode == NEAREST_NEWMV || mode == NEW_NEARESTMV ||
            mode == NEAR_NEWMV || mode == NEW_NEARMV);
}

// svt_aom_get_max_drl_index (mode_decision.c:286-310, verbatim).
std::uint8_t getMaxDrlIndex(std::uint8_t refmvCnt, PredictionMode mode) {
    std::uint8_t maxDrl = 0;

    if (mode == NEWMV || mode == NEW_NEWMV) {
        if (refmvCnt < 2) {
            maxDrl = 1;
        } else if (refmvCnt == 2) {
            maxDrl = 2;
        } else {
            maxDrl = 3;
        }
    }

    if (mode == NEARMV || mode == NEAR_NEARMV || mode == NEAR_NEWMV || mode == NEW_NEARMV) {
        if (refmvCnt < 3) {
            maxDrl = 1;
        } else if (refmvCnt == 3) {
            maxDrl = 2;
        } else {
            maxDrl = 3;
        }
    }

    return maxDrl;
}

// svt_aom_is_valid_unipred_ref (mode_decision.c:789-800, verbatim; the
// ctx->ref_pruning_ctrls / ctx->ref_filtering_res walls thread as the
// pruning / filtering carriers).
bool isValidUnipredRef(const RefPruningCtrls& pruning, const RefFilteringRes filtering,
                       std::uint8_t interCandGroup, std::uint8_t listIdx, std::uint8_t refIdx) {
    if (!pruning.enabled) {
        return true;
    }
    if (!filtering[interCandGroup][listIdx][refIdx].doRef &&
        (refIdx || !pruning.closestRefs[interCandGroup])) {
        return false;
    } else {
        return true;
    }
}

// is_valid_bipred_ref (mode_decision.c:820-839, verbatim over the carriers).
bool isValidBipredRef(const RefPruningCtrls& pruning, const RefFilteringRes filtering,
                      std::uint8_t interCandGroup, std::uint8_t listIdx0, std::uint8_t refIdx0,
                      std::uint8_t listIdx1, std::uint8_t refIdx1) {
    if (!pruning.enabled) {
        return true;
    }
    // Both ref should be 1 for bipred refs to be valid: if 1 is not best_refs
    // then there is a chance to exit the injection (mode_decision.c:825)
    if (!filtering[interCandGroup][listIdx0][refIdx0].doRef ||
        !filtering[interCandGroup][listIdx1][refIdx1].doRef) {
        // Check whether we should check the closest, if no then there no need
        // to move forward and return false (mode_decision.c:828)
        if (!pruning.closestRefs[interCandGroup]) {
            return false;
        }

        // Else check if ref are LAST and BWD, if not then return false
        // (mode_decision.c:833)
        if (refIdx0 || refIdx1) {
            return false;
        }
    }
    return true;
}

// mv_is_already_injected (mode_decision.c:739-787, verbatim over the
// InjectedMvCarrier; the Mv halves unpack from the packed as_int forms -
// mv.h:42-49, x low / y high).
bool mvIsAlreadyInjected(const InjectedMvCarrier& carrier, std::int32_t mv0, std::int32_t mv1,
                         std::uint8_t refType) {
    // av1_set_ref_frame (inter_prediction.h:487-496): rf[1] is NONE_FRAME
    // (-1) for ref_type < TOTAL_REFS_PER_FRAME (definitions.h:1377) and a
    // ref_frame_map second entry (all > INTRA_FRAME) for compounds - the
    // :744 `rf[1] <= INTRA_FRAME` test holds exactly for refType <
    // TOTAL_REFS_PER_FRAME.
    const bool unipred = refType < TOTAL_REFS_PER_FRAME;  // mode_decision.c:744

    const std::int16_t mv0x = (std::int16_t)((std::uint32_t)mv0 & 0xFFFFu);
    const std::int16_t mv0y = (std::int16_t)(((std::uint32_t)mv0 >> 16) & 0xFFFFu);
    const std::int16_t mv1x = (std::int16_t)((std::uint32_t)mv1 & 0xFFFFu);
    const std::int16_t mv1y = (std::int16_t)(((std::uint32_t)mv1 >> 16) & 0xFFFFu);

    if (unipred) {  // Unipred Candidate (mode_decision.c:743-754)
        // First check the validity of the candidate MV, and exit if invalid MV
        if (carrier.corruptedMvCheck && !checkMvValidity(mv0x, mv0y, 0)) {
            return true;
        }

        for (int candIdx = 0; candIdx < carrier.injectedMvCount; candIdx++) {
            if (carrier.injectedRefTypes[candIdx] == (std::int32_t)refType &&
                carrier.injectedMvs[candIdx][0] == mv0) {
                return true;
            }
        }
    } else {  // Bipred Candidate (mode_decision.c:755-785)
        // First check the validity of the candidate MV, and exit if invalid MV
        if (carrier.corruptedMvCheck &&
            (!checkMvValidity(mv0x, mv0y, 0) || !checkMvValidity(mv1x, mv1y, 0))) {
            return true;
        }

        const RedundantCandCtrls& redundCtrl = carrier.redundCtrl;  // :761
        if (redundCtrl.scoreTh) {
            const std::uint8_t isHighMag =
                (std::abs(mv0x) > redundCtrl.magTh) && (std::abs(mv0y) > redundCtrl.magTh) &&
                (std::abs(mv1x) > redundCtrl.magTh) && (std::abs(mv1y) > redundCtrl.magTh);
            for (int candIdx = 0; candIdx < carrier.injectedMvCount; candIdx++) {
                if (carrier.injectedRefTypes[candIdx] == (std::int32_t)refType) {
                    const std::int16_t inj0x = (std::int16_t)((std::uint32_t)carrier.injectedMvs[candIdx][0] & 0xFFFFu);
                    const std::int16_t inj0y = (std::int16_t)(((std::uint32_t)carrier.injectedMvs[candIdx][0] >> 16) & 0xFFFFu);
                    const std::int16_t inj1x = (std::int16_t)((std::uint32_t)carrier.injectedMvs[candIdx][1] & 0xFFFFu);
                    const std::int16_t inj1y = (std::int16_t)(((std::uint32_t)carrier.injectedMvs[candIdx][1] >> 16) & 0xFFFFu);
                    const int score = std::abs(inj0x - mv0x) + std::abs(inj0y - mv0y) +
                        std::abs(inj1x - mv1x) + std::abs(inj1y - mv1y);

                    if (score == 0 || (score < redundCtrl.scoreTh && isHighMag)) {
                        return true;
                    }
                }
            }
        } else {
            for (int candIdx = 0; candIdx < carrier.injectedMvCount; candIdx++) {
                if (carrier.injectedRefTypes[candIdx] == (std::int32_t)refType &&
                    carrier.injectedMvs[candIdx][0] == mv0 &&
                    carrier.injectedMvs[candIdx][1] == mv1) {
                    return true;
                }
            }
        }
    }
    return false;
}

}  // namespace l16_rdo
