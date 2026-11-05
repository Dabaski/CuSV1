#include "temporal_filter.h"

namespace analysis {
namespace temporal {

// log1p(x) for x in [-1..6], step 1/32 values in Fixed Points shift 16
// (temporal_filtering.c:339-565); mechanically transcribed from the
// pinned reference.
const int32_t log1pTabFp16[225] = {
    -2147483647 - 1, -227130, -181704, -155131, -136278, -121654, -109705, -99603,
    -90852, -83133, -76228, -69982, -64279, -59033, -54177, -49655,
    -45426, -41452, -37707, -34163, -30802, -27604, -24555, -21642,
    -18853, -16178, -13607, -11134, -8751, -6451, -4229, -2080,
    0, 2016, 3973, 5872, 7719, 9514, 11262, 12964,
    14623, 16242, 17821, 19363, 20870, 22342, 23783, 25192,
    26572, 27923, 29247, 30545, 31818, 33066, 34291, 35494,
    36674, 37834, 38974, 40095, 41196, 42279, 43345, 44394,
    45426, 46442, 47442, 48428, 49399, 50355, 51298, 52228,
    53145, 54049, 54940, 55820, 56688, 57545, 58390, 59225,
    60050, 60864, 61668, 62462, 63247, 64023, 64789, 65547,
    66296, 67036, 67769, 68493, 69209, 69917, 70618, 71312,
    71998, 72677, 73349, 74015, 74673, 75326, 75971, 76611,
    77244, 77871, 78492, 79108, 79717, 80321, 80920, 81513,
    82101, 82683, 83261, 83833, 84400, 84963, 85521, 86074,
    86622, 87166, 87705, 88240, 88771, 89297, 89820, 90338,
    90852, 91362, 91868, 92370, 92868, 93363, 93854, 94341,
    94825, 95305, 95782, 96255, 96725, 97191, 97654, 98114,
    98571, 99024, 99475, 99922, 100366, 100808, 101246, 101681,
    102114, 102544, 102971, 103395, 103816, 104235, 104651, 105065,
    105476, 105884, 106290, 106693, 107094, 107492, 107888, 108282,
    108673, 109062, 109449, 109833, 110215, 110595, 110973, 111348,
    111722, 112093, 112462, 112830, 113195, 113558, 113919, 114278,
    114635, 114990, 115344, 115695, 116044, 116392, 116738, 117082,
    117424, 117765, 118103, 118440, 118776, 119109, 119441, 119771,
    120100, 120426, 120752, 121075, 121397, 121718, 122037, 122354,
    122670, 122984, 123297, 123608, 123918, 124227, 124534, 124839,
    125143, 125446, 125747, 126047, 126346, 126643, 126939, 127233,
    127527};

// T[X] = exp(-(X)/16) for x in [0..7], step 1/16 values in Fixed Points
// shift 16 (temporal_filtering.c:624-634); mechanically transcribed from
// the pinned reference.
const int32_t expfTabFp16[129] = {
    65536, 61565, 57835, 54331, 51039, 47947, 45042, 42313, 39749, 37341, 35078, 32953, 30957,
    29081, 27319, 25664, 24109, 22648, 21276, 19987, 18776, 17638, 16570, 15566, 14623, 13737,
    12904, 12122, 11388, 10698, 10050, 9441,  8869,  8331,  7827,  7352,  6907,  6488,  6095,
    5726,  5379,  5053,  4747,  4459,  4189,  3935,  3697,  3473,  3262,  3065,  2879,  2704,
    2541,  2387,  2242,  2106,  1979,  1859,  1746,  1640,  1541,  1447,  1360,  1277,  1200,
    1127,  1059,  995,   934,   878,   824,   774,   728,   683,   642,   603,   566,   532,
    500,   470,   441,   414,   389,   366,   343,   323,   303,   285,   267,   251,   236,
    222,   208,   195,   184,   172,   162,   152,   143,   134,   126,   118,   111,   104,
    98,    92,    86,    81,    76,    72,    67,    63,    59,    56,    52,    49,    46,
    43,    41,    38,    36,    34,    31,    30,    28,    26,    24,    23,    21};

// exp(-x) for x in [0..7] (temporal_filtering.c:666-677); mechanically
// transcribed from the pinned reference.
const double expfTab[81] = {
    1,        0.904837, 0.818731, 0.740818, 0.67032,  0.606531, 0.548812, 0.496585,
    0.449329, 0.40657,  0.367879, 0.332871, 0.301194, 0.272532, 0.246597, 0.22313,
    0.201896, 0.182683, 0.165299, 0.149569, 0.135335, 0.122456, 0.110803, 0.100259,
    0.090718, 0.082085, 0.074274, 0.067206, 0.06081,  0.055023, 0.049787, 0.045049,
    0.040762, 0.036883, 0.033373, 0.030197, 0.027324, 0.024724, 0.022371, 0.020242,
    0.018316, 0.016573, 0.014996, 0.013569, 0.012277, 0.011109, 0.010052, 0.009095,
    0.00823,  0.007447, 0.006738, 0.006097, 0.005517, 0.004992, 0.004517, 0.004087,
    0.003698, 0.003346, 0.003028, 0.002739, 0.002479, 0.002243, 0.002029, 0.001836,
    0.001662, 0.001503, 0.00136,  0.001231, 0.001114, 0.001008, 0.000912, 0.000825,
    0.000747, 0.000676, 0.000611, 0.000553, 0.0005,   0.000453, 0.00041,  0.000371,
    0.000335
};

// value [i:0-15] (sqrt((float)i)*65536.0 (temporal_filtering.c:637-652));
// mechanically transcribed from the pinned reference.
const uint32_t sqrtArrayFp16[16] = {0,
                                    65536,
                                    92681,
                                    113511,
                                    131072,
                                    146542,
                                    160529,
                                    173391,
                                    185363,
                                    196608,
                                    207243,
                                    217358,
                                    227023,
                                    236293,
                                    245213,
                                    253819};

namespace {

// get_msb (definitions.h:596-612, MSVC branch semantics); valid for n != 0.
int32_t getMsb(uint32_t n) {
    int32_t position = 0;
    while (n > 1) {
        n >>= 1;
        ++position;
    }
    return position;
}

}  // namespace

// 1:1 port of sqrt_fast (temporal_filtering.c:655-664).
uint32_t sqrtFast(uint32_t x) {
    if (x > 15) {
        const int log2Half = getMsb(x) >> 1;
        const int mul2 = log2Half << 1;
        int base = static_cast<int>(x >> (mul2 - 2));
        return sqrtArrayFp16[base] >> (17 - log2Half);
    }
    return sqrtArrayFp16[x] >> 16;
}

// 1:1 port of calculate_tf_shift_factor (temporal_filtering.c:610-619);
// the consumed MeContext field tf_64x64_block_error is threaded as a
// parameter.
uint8_t calculateTfShiftFactor(uint64_t tf64x64BlockError) {
    const uint64_t blockErr = tf64x64BlockError >> 12;
    if (blockErr < 200) {  // LOW_ERROR_THRESHOLD (temporal_filtering.h:65)
        return 14;
    } else if (blockErr < 2000) {  // MED_ERROR_THRESHOLD (temporal_filtering.h:66)
        return 13;
    }
    return 12;  // Default value
}

// 1:1 port of svt_av1_calculate_decay_factor (temporal_filtering.c:589-607).
void calculateDecayFactor(uint32_t tfDecayFactorFp16[3], int32_t nDecayFp10, uint32_t qDecayFp8,
                          int decayControlCu, int decayControlCv, int32_t const0dot7Fp16,
                          const int32_t* noiseLevelsLog1pFp16, uint8_t shiftFactor, bool tfChroma) {
    tfDecayFactorFp16[0 /*PLANE_Y*/] =
        static_cast<uint32_t>((static_cast<int64_t>(nDecayFp10) * static_cast<int64_t>(nDecayFp10)) *
                              static_cast<int64_t>(qDecayFp8) >>
                              shiftFactor);

    if (tfChroma) {
        nDecayFp10 =
            (decayControlCu * (const0dot7Fp16 + noiseLevelsLog1pFp16[1 /*PLANE_U*/])) / (static_cast<int32_t>(1) << 6);
        tfDecayFactorFp16[1 /*PLANE_U*/] =
            static_cast<uint32_t>((static_cast<int64_t>(nDecayFp10) * static_cast<int64_t>(nDecayFp10)) *
                                  static_cast<int64_t>(qDecayFp8) >>
                                  shiftFactor);
        nDecayFp10 =
            (decayControlCv * (const0dot7Fp16 + noiseLevelsLog1pFp16[2 /*PLANE_V*/])) / (static_cast<int32_t>(1) << 6);
        tfDecayFactorFp16[2 /*PLANE_V*/] =
            static_cast<uint32_t>((static_cast<int64_t>(nDecayFp10) * static_cast<int64_t>(nDecayFp10)) *
                                  static_cast<int64_t>(qDecayFp8) >>
                                  shiftFactor);
    }
}

// 1:1 port of calculate_squared_errors_sum (temporal_filtering.c:697-707).
uint32_t calculateSquaredErrorsSum(const uint8_t* s, int sStride, const uint8_t* p, int pStride,
                                   unsigned int w, unsigned int h) {
    uint32_t sum = 0;
    for (unsigned int i = 0; i < h; i++) {
        for (unsigned int j = 0; j < w; j++) {
            const int diff = static_cast<int>(s[i * sStride + j]) - static_cast<int>(p[i * pStride + j]);
            sum += static_cast<uint32_t>(diff * diff);
        }
    }
    return sum;
}

namespace {

// 1:1 with the reference weight math (temporal_filtering.c:764-767 and
// :1014-1023): scaled_diff16 = min(avg_err_fp10 / max(decay >> 10, 1),
// 7 * 16), weight = (expf_tab_fp16[scaled_diff16] * TF_WEIGHT_SCALE) >>
// shift where TF_WEIGHT_SCALE = 1000 (temporal_filtering.h:33). The
// shift is 17 for the zz kernels (:767) and 16 for the non-zz kernels
// (:1023).
uint32_t tfWeight(uint64_t avgErrFp10, uint32_t tfDecayFactorFp16, int shift) {
    uint32_t divisor = tfDecayFactorFp16 >> 10;
    if (divisor < 1) {
        divisor = 1;
    }
    uint64_t scaledDiff64 = avgErrFp10 / divisor;
    uint32_t scaledDiff16 = scaledDiff64 > (7u * 16) ? (7u * 16) : static_cast<uint32_t>(scaledDiff64);
    return static_cast<uint32_t>((static_cast<uint64_t>(expfTabFp16[scaledDiff16]) * 1000u /*TF_WEIGHT_SCALE*/) >>
                                 shift);
}

}  // namespace

// 1:1 port of svt_av1_apply_zz_based_temporal_filter_planewise_medium_
// partial_c (temporal_filtering.c:738-781).
void applyZzBasedTemporalFilterPlanewiseMediumPartial(bool splitFlag, const uint64_t* blockError16x16,
                                                      uint64_t blockError32x32, const uint8_t* yPre,
                                                      int yPreStride, unsigned int blockWidth,
                                                      unsigned int blockHeight, uint32_t* yAccum,
                                                      uint16_t* yCount, uint32_t tfDecayFactorFp16) {
    uint32_t blockErrorFp8[4];

    if (splitFlag) {
        for (int i = 0; i < 4; ++i) {
            blockErrorFp8[i] = static_cast<uint32_t>(blockError16x16[i]);
        }
    } else {
        blockErrorFp8[0] = blockErrorFp8[1] = blockErrorFp8[2] = blockErrorFp8[3] =
            static_cast<uint32_t>(blockError32x32 >> 2);
    }

    // Calculation for every quarter
    for (unsigned int subblockIdx = 0; subblockIdx < 4; subblockIdx++) {
        uint32_t avgErrFp10 = blockErrorFp8[subblockIdx] << 2;

        uint32_t adjustedWeight = tfWeight(avgErrFp10, tfDecayFactorFp16, 17);

        int xOffset = static_cast<int>((subblockIdx % 2) * blockWidth / 2);
        int yOffset = static_cast<int>((subblockIdx / 2) * blockHeight / 2);

        for (unsigned int i = 0; i < blockHeight / 2; i++) {
            for (unsigned int j = 0; j < blockWidth / 2; j++) {
                const int k = static_cast<int>((i + yOffset) * yPreStride + j + xOffset);
                const int pixelValue = yPre[k];
                yCount[k] = static_cast<uint16_t>(yCount[k] + adjustedWeight);
                yAccum[k] += adjustedWeight * static_cast<uint32_t>(pixelValue);
            }
        }
    }
}

// 1:1 port of svt_av1_apply_zz_based_temporal_filter_planewise_medium_c
// (temporal_filtering.c:783-814).
void applyZzBasedTemporalFilterPlanewiseMedium(bool splitFlag, const uint64_t* blockError16x16,
                                               uint64_t blockError32x32, const uint8_t* yPre, int yPreStride,
                                               const uint8_t* uPre, const uint8_t* vPre, int uvPreStride,
                                               unsigned int blockWidth, unsigned int blockHeight, int ssX,
                                               int ssY, uint32_t* yAccum, uint16_t* yCount, uint32_t* uAccum,
                                               uint16_t* uCount, uint32_t* vAccum, uint16_t* vCount,
                                               const uint32_t* tfDecayFactorFp16, bool tfChroma) {
    applyZzBasedTemporalFilterPlanewiseMediumPartial(splitFlag, blockError16x16, blockError32x32, yPre, yPreStride,
                                                     blockWidth, blockHeight, yAccum, yCount,
                                                     tfDecayFactorFp16[0 /*PLANE_Y*/]);
    if (tfChroma) {
        applyZzBasedTemporalFilterPlanewiseMediumPartial(splitFlag, blockError16x16, blockError32x32, uPre,
                                                         uvPreStride, blockWidth >> ssX, blockHeight >> ssY,
                                                         uAccum, uCount, tfDecayFactorFp16[1 /*PLANE_U*/]);

        applyZzBasedTemporalFilterPlanewiseMediumPartial(splitFlag, blockError16x16, blockError32x32, vPre,
                                                         uvPreStride, blockWidth >> ssX, blockHeight >> ssY,
                                                         vAccum, vCount, tfDecayFactorFp16[2 /*PLANE_V*/]);
    }
}

// 1:1 port of svt_av1_apply_temporal_filter_planewise_medium_partial_c
// (temporal_filtering.c:930-1037).
void applyTemporalFilterPlanewiseMediumPartial(uint16_t mvDistTh, bool splitFlag, const int16_t* mv16x16X,
                                               const int16_t* mv16x16Y, const uint64_t* blockError16x16,
                                               int16_t mv32x32X, int16_t mv32x32Y, uint64_t blockError32x32,
                                               const uint8_t* ySrc, int ySrcStride, const uint8_t* yPre,
                                               int yPreStride, unsigned int blockWidth, unsigned int blockHeight,
                                               uint32_t* yAccum, uint16_t* yCount, uint32_t tfDecayFactorFp16,
                                               uint32_t lumaWindowErrorQuadFp8[4], bool isChroma) {
    uint32_t distanceThresholdFp16 = (static_cast<uint32_t>(mvDistTh) << 16) / 10;
    if (distanceThresholdFp16 < (1u << 16)) {
        distanceThresholdFp16 = 1u << 16;
    }

    // Calculation for every quarter
    uint32_t dFactorFp8[4];
    uint32_t blockErrorFp8[4];
    uint32_t chromaWindowErrorQuadFp8[4];
    uint32_t* windowErrorQuadFp8 = isChroma ? chromaWindowErrorQuadFp8 : lumaWindowErrorQuadFp8;

    if (splitFlag) {
        for (int i = 0; i < 4; ++i) {
            const int32_t col = mv16x16X[i];
            const int32_t row = mv16x16Y[i];
            const uint32_t distanceFp4 = sqrtFast(static_cast<uint32_t>(col * col + row * row) << 8);
            uint32_t d = (distanceFp4 << 12) / (distanceThresholdFp16 >> 8);
            dFactorFp8[i] = d < (1u << 8) ? (1u << 8) : d;
            blockErrorFp8[i] = static_cast<uint32_t>(blockError16x16[i]);
        }
    } else {
        tfDecayFactorFp16 <<= 1;
        const int32_t col = mv32x32X;
        const int32_t row = mv32x32Y;

        const uint32_t distanceFp4 = sqrtFast(static_cast<uint32_t>(col * col + row * row) << 8);
        uint32_t d = (distanceFp4 << 12) / (distanceThresholdFp16 >> 8);
        d = d < (1u << 8) ? (1u << 8) : d;
        dFactorFp8[0] = dFactorFp8[1] = dFactorFp8[2] = dFactorFp8[3] = d;
        blockErrorFp8[0] = blockErrorFp8[1] = blockErrorFp8[2] = blockErrorFp8[3] =
            static_cast<uint32_t>(blockError32x32 >> 2);
    }
    const uint32_t bwHalf = blockWidth >> 1;
    const uint32_t bhHalf = blockHeight >> 1;
    uint32_t sum;
    sum = calculateSquaredErrorsSum(ySrc, ySrcStride, yPre, yPreStride, bwHalf, bhHalf);
    windowErrorQuadFp8[0] = (((sum << 4) / bwHalf) << 4) / bhHalf;

    sum = calculateSquaredErrorsSum(ySrc + bwHalf, ySrcStride, yPre + bwHalf, yPreStride, bwHalf, bhHalf);
    windowErrorQuadFp8[1] = (((sum << 4) / bwHalf) << 4) / bhHalf;

    sum = calculateSquaredErrorsSum(ySrc + ySrcStride * bhHalf, ySrcStride, yPre + yPreStride * bhHalf, yPreStride,
                                    bwHalf, bhHalf);
    windowErrorQuadFp8[2] = (((sum << 4) / bwHalf) << 4) / bhHalf;

    sum = calculateSquaredErrorsSum(ySrc + ySrcStride * bhHalf + bwHalf, ySrcStride,
                                    yPre + yPreStride * bhHalf + bwHalf, yPreStride, bwHalf, bhHalf);
    windowErrorQuadFp8[3] = (((sum << 4) / bwHalf) << 4) / bhHalf;

    if (isChroma) {
        for (int i = 0; i < 4; ++i) {
            windowErrorQuadFp8[i] = (windowErrorQuadFp8[i] * 5 + lumaWindowErrorQuadFp8[i]) / 6;
        }
    }

    // Calculation for every quarter
    for (unsigned int subblockIdx = 0; subblockIdx < 4; subblockIdx++) {
        uint32_t combinedErrorFp8 =
            (windowErrorQuadFp8[subblockIdx] * 5u /*TF_WINDOW_BLOCK_BALANCE_WEIGHT*/ + blockErrorFp8[subblockIdx]) /
            (5u + 1);

        uint64_t avgErrFp10 =
            static_cast<uint64_t>(combinedErrorFp8 >> 3) * static_cast<uint64_t>(dFactorFp8[subblockIdx] >> 3);
        uint32_t adjustedWeight = tfWeight(avgErrFp10, tfDecayFactorFp16, 16);

        int xOffset = static_cast<int>((subblockIdx % 2) * blockWidth / 2);
        int yOffset = static_cast<int>((subblockIdx / 2) * blockHeight / 2);

        for (unsigned int i = 0; i < blockHeight / 2; i++) {
            for (unsigned int j = 0; j < blockWidth / 2; j++) {
                const int k = static_cast<int>((i + yOffset) * yPreStride + j + xOffset);
                const int pixelValue = yPre[k];
                yCount[k] = static_cast<uint16_t>(yCount[k] + adjustedWeight);
                yAccum[k] += adjustedWeight * static_cast<uint32_t>(pixelValue);
            }
        }
    }
}

// 1:1 port of svt_av1_apply_temporal_filter_planewise_medium_c
// (temporal_filtering.c:1039-1087).
void applyTemporalFilterPlanewiseMedium(uint16_t mvDistTh, bool splitFlag, const int16_t* mv16x16X,
                                        const int16_t* mv16x16Y, const uint64_t* blockError16x16,
                                        int16_t mv32x32X, int16_t mv32x32Y, uint64_t blockError32x32,
                                        const uint8_t* ySrc, int ySrcStride, const uint8_t* yPre, int yPreStride,
                                        const uint8_t* uSrc, const uint8_t* vSrc, int uvSrcStride,
                                        const uint8_t* uPre, const uint8_t* vPre, int uvPreStride,
                                        unsigned int blockWidth, unsigned int blockHeight, int ssX, int ssY,
                                        uint32_t* yAccum, uint16_t* yCount, uint32_t* uAccum, uint16_t* uCount,
                                        uint32_t* vAccum, uint16_t* vCount, const uint32_t* tfDecayFactorFp16,
                                        bool tfChroma) {
    uint32_t lumaWindowErrorQuadFp8[4];

    applyTemporalFilterPlanewiseMediumPartial(mvDistTh, splitFlag, mv16x16X, mv16x16Y, blockError16x16, mv32x32X,
                                              mv32x32Y, blockError32x32, ySrc, ySrcStride, yPre, yPreStride,
                                              blockWidth, blockHeight, yAccum, yCount,
                                              tfDecayFactorFp16[0 /*PLANE_Y*/], lumaWindowErrorQuadFp8, false);
    if (tfChroma) {
        applyTemporalFilterPlanewiseMediumPartial(mvDistTh, splitFlag, mv16x16X, mv16x16Y, blockError16x16, mv32x32X,
                                                  mv32x32Y, blockError32x32, uSrc, uvSrcStride, uPre, uvPreStride,
                                                  blockWidth >> ssX, blockHeight >> ssY, uAccum, uCount,
                                                  tfDecayFactorFp16[1 /*PLANE_U*/], lumaWindowErrorQuadFp8, true);

        applyTemporalFilterPlanewiseMediumPartial(mvDistTh, splitFlag, mv16x16X, mv16x16Y, blockError16x16, mv32x32X,
                                                  mv32x32Y, blockError32x32, vSrc, uvSrcStride, vPre, uvPreStride,
                                                  blockWidth >> ssX, blockHeight >> ssY, vAccum, vCount,
                                                  tfDecayFactorFp16[2 /*PLANE_V*/], lumaWindowErrorQuadFp8, true);
    }
}

int32_t noiseLog1pFp16(int32_t noiseLevelFp16) {
    int32_t baseFp16 = (65536 /*1:fp16*/ + noiseLevelFp16);

    if (baseFp16 <= 0) {
        return (-2147483647 - 1) /*-MAX*/;
    } else if (baseFp16 < (458752) /*7:fp16*/) {
        // Approximate value:
        int32_t id = baseFp16 >> 11; // step 1/32 so multiply by 32 is reduce shift of 5
        int32_t rest = baseFp16 & 0x7FF; // 11 bits
        int32_t diff = ((rest * (log1pTabFp16[id + 1] - log1pTabFp16[id])) >> 11);
        int32_t valFp16 = log1pTabFp16[id] + diff;
        return valFp16;
    } else {
        // approximation to line(fp16): y = 1860*x + 116456
        return ((1860 * (noiseLevelFp16 >> 8)) >> 8) + 116456;
    }
}
}
}
