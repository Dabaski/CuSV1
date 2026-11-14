#include "temporal_filter.h"

#include "analysis.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>

namespace analysis {
// OD_DIVU (bitstream_unit.h:48-54) reads the magic-multiplier table that
// AN14 landed in noise_util.cpp; the extern declaration matches the
// definition there (and analysis.h:1049).
extern std::uint32_t odDivuSmallConsts[1024][2];
}  // namespace analysis

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

// OD_DIVU (bitstream_unit.h:48-54): the multiply-unroll division for
// d < OD_DIVU_DMAX (1024), plain division otherwise. svt_log2f = get_msb
// (definitions.h:591-592).
uint32_t odDivu(uint32_t x, uint32_t d) {
    if (d < 1024) {
        return (uint32_t)((odDivuSmallConsts[d - 1][0] * (uint64_t)x + odDivuSmallConsts[d - 1][1]) >> 32) >>
               getMsb(d);
    }
    return x / d;
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

// TF2: 1:1 port of calculate_squared_errors_sum_highbd
// (temporal_filtering.c:710-720); SQR(x) = (x)*(x) (utility.h:107) in
// int arithmetic, the shift applied once at the end (:719).
uint32_t calculateSquaredErrorsSumHbd(const uint16_t* s, int sStride, const uint16_t* p, int pStride,
                                      unsigned int w, unsigned int h, int shiftFactor) {
    uint32_t sum = 0;
    for (unsigned int i = 0; i < h; i++) {
        for (unsigned int j = 0; j < w; j++) {
            const int diff = static_cast<int>(s[i * sStride + j]) - static_cast<int>(p[i * pStride + j]);
            sum += static_cast<uint32_t>(diff * diff);
        }
    }
    return (sum >> shiftFactor);
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

// TF2: 1:1 port of svt_av1_apply_zz_based_temporal_filter_planewise_
// medium_hbd_partial_c (temporal_filtering.c:832-874). encoder_bit_depth
// is (void)ed by the reference (:839) and dropped here; the block-error
// pre-shifts >> 4 / >> 6 (:845, :849) are hard-coded, and the weight
// shift >> 17 (:860) matches the 8-bit twin.
void applyZzBasedTemporalFilterPlanewiseMediumPartialHbd(bool splitFlag, const uint64_t* blockError16x16,
                                                         uint64_t blockError32x32, const uint16_t* yPre,
                                                         int yPreStride, unsigned int blockWidth,
                                                         unsigned int blockHeight, uint32_t* yAccum,
                                                         uint16_t* yCount, uint32_t tfDecayFactorFp16) {
    uint32_t blockErrorFp8[4];

    if (splitFlag) {
        for (int i = 0; i < 4; ++i) {
            blockErrorFp8[i] = static_cast<uint32_t>(blockError16x16[i] >> 4);
        }
    } else {
        blockErrorFp8[0] = blockErrorFp8[1] = blockErrorFp8[2] = blockErrorFp8[3] =
            static_cast<uint32_t>(blockError32x32 >> 6);
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

// TF2: 1:1 port of svt_av1_apply_zz_based_temporal_filter_planewise_
// medium_hbd_c (temporal_filtering.c:876-912).
void applyZzBasedTemporalFilterPlanewiseMediumHbd(bool splitFlag, const uint64_t* blockError16x16,
                                                  uint64_t blockError32x32, const uint16_t* yPre, int yPreStride,
                                                  const uint16_t* uPre, const uint16_t* vPre, int uvPreStride,
                                                  unsigned int blockWidth, unsigned int blockHeight, int ssX,
                                                  int ssY, uint32_t* yAccum, uint16_t* yCount, uint32_t* uAccum,
                                                  uint16_t* uCount, uint32_t* vAccum, uint16_t* vCount,
                                                  const uint32_t* tfDecayFactorFp16, bool tfChroma) {
    applyZzBasedTemporalFilterPlanewiseMediumPartialHbd(splitFlag, blockError16x16, blockError32x32, yPre, yPreStride,
                                                        blockWidth, blockHeight, yAccum, yCount,
                                                        tfDecayFactorFp16[0 /*PLANE_Y*/]);
    if (tfChroma) {
        applyZzBasedTemporalFilterPlanewiseMediumPartialHbd(splitFlag, blockError16x16, blockError32x32, uPre,
                                                            uvPreStride, blockWidth >> ssX, blockHeight >> ssY,
                                                            uAccum, uCount, tfDecayFactorFp16[1 /*PLANE_U*/]);

        applyZzBasedTemporalFilterPlanewiseMediumPartialHbd(splitFlag, blockError16x16, blockError32x32, vPre,
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

// TF2: 1:1 port of svt_av1_apply_temporal_filter_planewise_medium_hbd_
// partial_c (temporal_filtering.c:1105-1214). shift_factor =
// (encoder_bit_depth - 8) * 2 (:1113) feeds ONLY the highbd SSE calls;
// the block-error pre-shifts >> 4 / >> 6 (:1133, :1147) are hard-coded;
// the weight shift >> 16 (:1200) matches the 8-bit twin.
void applyTemporalFilterPlanewiseMediumPartialHbd(uint16_t mvDistTh, bool splitFlag, const int16_t* mv16x16X,
                                                  const int16_t* mv16x16Y, const uint64_t* blockError16x16,
                                                  int16_t mv32x32X, int16_t mv32x32Y, uint64_t blockError32x32,
                                                  const uint16_t* ySrc, int ySrcStride, const uint16_t* yPre,
                                                  int yPreStride, unsigned int blockWidth, unsigned int blockHeight,
                                                  uint32_t* yAccum, uint16_t* yCount, uint32_t tfDecayFactorFp16,
                                                  uint32_t lumaWindowErrorQuadFp8[4], bool isChroma,
                                                  uint32_t encoderBitDepth) {
    const int shiftFactor = static_cast<int>((encoderBitDepth - 8) * 2);

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
            blockErrorFp8[i] = static_cast<uint32_t>(blockError16x16[i] >> 4);
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
            static_cast<uint32_t>(blockError32x32 >> 6);
    }
    const uint32_t bwHalf = blockWidth >> 1;
    const uint32_t bhHalf = blockHeight >> 1;
    uint32_t sum;
    sum = calculateSquaredErrorsSumHbd(ySrc, ySrcStride, yPre, yPreStride, bwHalf, bhHalf, shiftFactor);
    windowErrorQuadFp8[0] = (((sum << 4) / bwHalf) << 4) / bhHalf;

    sum = calculateSquaredErrorsSumHbd(ySrc + bwHalf, ySrcStride, yPre + bwHalf, yPreStride, bwHalf, bhHalf,
                                       shiftFactor);
    windowErrorQuadFp8[1] = (((sum << 4) / bwHalf) << 4) / bhHalf;

    sum = calculateSquaredErrorsSumHbd(ySrc + ySrcStride * bhHalf, ySrcStride, yPre + yPreStride * bhHalf, yPreStride,
                                       bwHalf, bhHalf, shiftFactor);
    windowErrorQuadFp8[2] = (((sum << 4) / bwHalf) << 4) / bhHalf;

    sum = calculateSquaredErrorsSumHbd(ySrc + ySrcStride * bhHalf + bwHalf, ySrcStride,
                                       yPre + yPreStride * bhHalf + bwHalf, yPreStride, bwHalf, bhHalf, shiftFactor);
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

// TF2: 1:1 port of svt_av1_apply_temporal_filter_planewise_medium_hbd_c
// (temporal_filtering.c:1216-1267).
void applyTemporalFilterPlanewiseMediumHbd(uint16_t mvDistTh, bool splitFlag, const int16_t* mv16x16X,
                                           const int16_t* mv16x16Y, const uint64_t* blockError16x16,
                                           int16_t mv32x32X, int16_t mv32x32Y, uint64_t blockError32x32,
                                           const uint16_t* ySrc, int ySrcStride, const uint16_t* yPre,
                                           int yPreStride, const uint16_t* uSrc, const uint16_t* vSrc,
                                           int uvSrcStride, const uint16_t* uPre, const uint16_t* vPre,
                                           int uvPreStride, unsigned int blockWidth, unsigned int blockHeight,
                                           int ssX, int ssY, uint32_t* yAccum, uint16_t* yCount, uint32_t* uAccum,
                                           uint16_t* uCount, uint32_t* vAccum, uint16_t* vCount,
                                           const uint32_t* tfDecayFactorFp16, bool tfChroma,
                                           uint32_t encoderBitDepth) {
    uint32_t lumaWindowErrorQuadFp8[4];

    applyTemporalFilterPlanewiseMediumPartialHbd(mvDistTh, splitFlag, mv16x16X, mv16x16Y, blockError16x16, mv32x32X,
                                                 mv32x32Y, blockError32x32, ySrc, ySrcStride, yPre, yPreStride,
                                                 blockWidth, blockHeight, yAccum, yCount,
                                                 tfDecayFactorFp16[0 /*PLANE_Y*/], lumaWindowErrorQuadFp8, false,
                                                 encoderBitDepth);
    if (tfChroma) {
        applyTemporalFilterPlanewiseMediumPartialHbd(mvDistTh, splitFlag, mv16x16X, mv16x16Y, blockError16x16,
                                                     mv32x32X, mv32x32Y, blockError32x32, uSrc, uvSrcStride, uPre,
                                                     uvPreStride, blockWidth >> ssX, blockHeight >> ssY, uAccum,
                                                     uCount, tfDecayFactorFp16[1 /*PLANE_U*/],
                                                     lumaWindowErrorQuadFp8, true, encoderBitDepth);

        applyTemporalFilterPlanewiseMediumPartialHbd(mvDistTh, splitFlag, mv16x16X, mv16x16Y, blockError16x16,
                                                     mv32x32X, mv32x32Y, blockError32x32, vSrc, uvSrcStride, vPre,
                                                     uvPreStride, blockWidth >> ssX, blockHeight >> ssY, vAccum,
                                                     vCount, tfDecayFactorFp16[2 /*PLANE_V*/],
                                                     lumaWindowErrorQuadFp8, true, encoderBitDepth);
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

// AM1: svt_vmaf_hpass_row_c (temporal_filtering.c:3720-3733).
void vmafHpassRow(const uint8_t* srcRow, int width, int16_t* hRow) {
    const int steps_x = 2;
    uint32_t h_acc[4] = {0};
    for (int x = -steps_x; x < width + steps_x; x++) {
        uint32_t tmp1 = x <= 0 ? srcRow[0] : x >= width ? srcRow[width - 1] : (uint32_t)srcRow[x];
        for (int s = 0; s < steps_x * 2; s += 2) {
            uint32_t tmp2 = h_acc[s] + tmp1;
            h_acc[s] = tmp1;
            tmp1 = h_acc[s + 1] + tmp2;
            h_acc[s + 1] = tmp2;
        }
        hRow[x + steps_x] = (int16_t)tmp1;
    }
}

// AM1: svt_vmaf_vpass_row_c (temporal_filtering.c:3660-3669).
void vmafVpassRow(const int16_t* r0, const int16_t* r1, const int16_t* r2, const int16_t* r3,
                  const int16_t* r4, uint8_t* blurRow, int width, int steps_x) {
    const int blur_start = 2 * steps_x;
    for (int x = 0; x < width; x++) {
        const int j = x + blur_start;
        uint32_t v = (uint32_t)r0[j] + (uint32_t)r4[j] + 4u * ((uint32_t)r1[j] + (uint32_t)r3[j]) +
            6u * (uint32_t)r2[j];
        blurRow[x] = (uint8_t)((v + 128u) >> 8);
    }
}

// AM1: vmaf_box_blur_frame (pic_analysis_process.c:1773-1799).
void vmafBoxBlurFrame(const uint8_t* lumaPlane, int stride, uint8_t* blurPlane, int width, int height,
                      int16_t* const hring[5]) {
    const int steps_x = 2;
    const int steps_y = 2;

    int16_t* r[5] = {hring[0], hring[1], hring[2], hring[3], hring[4]};
    for (int k = 0; k < 4; k++) {
        int row = k - steps_y;
        row = row < 0 ? 0 : (row >= height ? height - 1 : row);
        vmafHpassRow(lumaPlane + (size_t)row * stride, width, r[k]);
    }

    for (int m = 0; m < height; m++) {
        int row = m + steps_y;
        row = row >= height ? height - 1 : row;
        vmafHpassRow(lumaPlane + (size_t)row * stride, width, r[4]);

        vmafVpassRow(r[0], r[1], r[2], r[3], r[4], blurPlane + (size_t)m * width, width, steps_x);

        int16_t* oldest = r[0];
        r[0] = r[1];
        r[1] = r[2];
        r[2] = r[3];
        r[3] = r[4];
        r[4] = oldest;
    }
}

// AM1: svt_vmaf_apply_unsharp_row_c (temporal_filtering.c:3650-3658).
void vmafApplyUnsharpRow(const uint8_t* src, const uint8_t* blur, uint8_t* dst, int width, int amount,
                         int32_t max_delta) {
    for (int j = 0; j < width; j++) {
        int32_t detail = (int32_t)src[j] - (int32_t)blur[j];
        detail = detail > max_delta ? max_delta : detail < -max_delta ? -max_delta : detail;
        int32_t result = (int32_t)src[j] + ((detail * amount) >> 15);
        dst[j] = (uint8_t)(result < 0 ? 0 : result > 255 ? 255 : result);
    }
}

// AM1: vmaf_unsharp_apply_frame (pic_analysis_process.c:1815-1821).
void vmafUnsharpApplyFrame(const uint8_t* src, const uint8_t* blur_plane, uint8_t* dst, int width, int height,
                           int stride, int sharp_amount, int32_t delta_clip) {
    for (int y = 0; y < height; y++) {
        vmafApplyUnsharpRow(
            src + y * stride, blur_plane + y * width, dst + y * stride, width, sharp_amount, delta_clip);
    }
}

// AM1: svt_vmaf_compute_avg_mad_c (temporal_filtering.c:3622-3648).
uint32_t vmafComputeAvgMad(const uint8_t* src, int width, int height, int stride) {
    uint64_t total_activity = 0;
    int block_count = 0;
    for (int by = 0; by + 8 <= height; by += 8) {
        for (int bx = 0; bx + 8 <= width; bx += 8) {
            uint32_t sum = 0;
            for (int r = 0; r < 8; r++) {
                for (int c = 0; c < 8; c++) {
                    sum += src[(by + r) * stride + (bx + c)];
                }
            }
            uint32_t mean = sum >> 6;
            uint32_t mad = 0;
            for (int r = 0; r < 8; r++) {
                for (int c = 0; c < 8; c++) {
                    mad += (uint32_t)abs((int)src[(by + r) * stride + (bx + c)] - (int)mean);
                }
            }
            total_activity += mad;
            block_count++;
        }
    }
    if (block_count == 0) {
        return 0;
    }
    return (uint32_t)(total_activity / ((uint64_t)block_count * 64));
}

// AM1: svt_vmaf_compute_gradient_coherence_c (temporal_filtering.c:3671-3702).
float vmafComputeGradientCoherence(const uint8_t* src, int width, int height, int stride) {
    double weighted_coh = 0.0;
    double weight_sum = 0.0;
    for (int by = 1; by < height - 1; by += 16) {
        for (int bx = 1; bx < width - 1; bx += 16) {
            int64_t sum_xx = 0, sum_yy = 0, sum_xy = 0;
            int y_end = (by + 16 < height - 1) ? by + 16 : height - 1;
            int x_end = (bx + 16 < width - 1) ? bx + 16 : width - 1;
            for (int y = by; y < y_end; y++) {
                const uint8_t* row = src + y * stride;
                const uint8_t* up = src + (y - 1) * stride;
                const uint8_t* down = src + (y + 1) * stride;
                for (int x = bx; x < x_end; x++) {
                    int32_t grad_x = (int32_t)row[x + 1] - (int32_t)row[x - 1];
                    int32_t grad_y = (int32_t)down[x] - (int32_t)up[x];
                    sum_xx += (int64_t)grad_x * grad_x;
                    sum_yy += (int64_t)grad_y * grad_y;
                    sum_xy += (int64_t)grad_x * grad_y;
                }
            }
            double xx = (double)sum_xx;
            double yy = (double)sum_yy;
            double xy = (double)sum_xy;
            weighted_coh += (double)sqrtf((float)((xx - yy) * (xx - yy) + 4.0 * xy * xy));
            weight_sum += xx + yy;
        }
    }
    if (weight_sum <= 0.0) {
        return 1.0f;
    }
    return (float)(weighted_coh / weight_sum);
}

// AM1: svt_vmaf_count_detail_le_c (temporal_filtering.c:3704-3718).
uint32_t vmafCountDetailLe(const uint8_t* src, const uint8_t* blur, int width, int height, int src_stride,
                           int thresh) {
    uint32_t match_count = 0;
    for (int y = 0; y < height; y++) {
        const uint8_t* src_row = src + (size_t)y * src_stride;
        const uint8_t* blur_row = blur + (size_t)y * width;
        for (int x = 0; x < width; x++) {
            int32_t detail = abs((int32_t)src_row[x] - (int32_t)blur_row[x]);
            if (detail <= thresh) {
                match_count++;
            }
        }
    }
    return match_count;
}

// AM1: svt_estimate_noise_fp16_c (temporal_filtering.c:3556-3585).
int32_t vmafEstimateNoiseFp16(const uint8_t* src, uint16_t width, uint16_t height, uint16_t y_stride) {
    int64_t sum = 0;
    int64_t num = 0;

    for (int i = 1; i < height - 1; ++i) {
        for (int j = 1; j < width - 1; ++j) {
            const int k = i * y_stride + j;
            // Sobel gradients
            const int g_x = (src[k - y_stride - 1] - src[k - y_stride + 1]) +
                (src[k + y_stride - 1] - src[k + y_stride + 1]) + 2 * (src[k - 1] - src[k + 1]);
            const int g_y = (src[k - y_stride - 1] - src[k + y_stride - 1]) +
                (src[k - y_stride + 1] - src[k + y_stride + 1]) + 2 * (src[k - y_stride] - src[k + y_stride]);
            const int ga = abs(g_x) + abs(g_y);
            if (ga < 50 /*EDGE_THRESHOLD*/) {  // Do not consider edge pixels to estimate the noise
                // Find Laplacian
                const int v = 4 * src[k] - 2 * (src[k - 1] + src[k + 1] + src[k - y_stride] + src[k + y_stride]) +
                    (src[k - y_stride - 1] + src[k - y_stride + 1] + src[k + y_stride - 1] +
                     src[k + y_stride + 1]);
                sum += abs(v);
                ++num;
            }
        }
    }
    // If very few smooth pels, return -1 since the estimate is unreliable
    if (num < 16 /*SMOOTH_THRESHOLD*/) {
        return -65536 /*-1:fp16*/;
    }

    return (int32_t)((sum * 82137 /*SQRT_PI_BY_2_FP16*/) / (6 * num));
}

// A11D-T1: 1:1 port of svt_aom_apply_filtering_central_c
// (temporal_filtering.c:262-298). The consumed MeContext field
// (tf_chroma) and the picture field (y_stride) are threaded as
// parameters (the b64Variance carrier pattern); the src/accum/count
// pointer triples are the reference's EbByte*/uint32_t**/uint16_t**
// shapes. modifier = TF_PLANEWISE_FILTER_WEIGHT_SCALE (1000).
void applyFilteringCentral(bool tfChroma, const uint8_t* const* src,
                           uint32_t** accum, uint16_t** count,
                           uint16_t blkWidth, uint16_t blkHeight,
                           uint32_t ssX, uint32_t ssY, uint16_t srcStrideY) {
    uint16_t blkHeightCh = blkHeight >> ssY;
    uint16_t blkWidthCh  = blkWidth >> ssX;
    uint16_t srcStrideCh = srcStrideY >> ssX;

    const int modifier = 1000; /*TF_PLANEWISE_FILTER_WEIGHT_SCALE*/

    // Luma
    for (uint16_t k = 0, i = 0; i < blkHeight; i++) {
        for (uint16_t j = 0; j < blkWidth; j++) {
            accum[0 /*PLANE_Y*/][k] = modifier * src[0 /*PLANE_Y*/][i * srcStrideY + j];
            count[0 /*PLANE_Y*/][k] = modifier;
            ++k;
        }
    }

    // Chroma
    if (tfChroma) {
        for (uint16_t k = 0, i = 0; i < blkHeightCh; i++) {
            for (uint16_t j = 0; j < blkWidthCh; j++) {
                accum[1 /*PLANE_U*/][k] = modifier * src[1 /*PLANE_U*/][i * srcStrideCh + j];
                count[1 /*PLANE_U*/][k] = modifier;

                accum[2 /*PLANE_V*/][k] = modifier * src[2 /*PLANE_V*/][i * srcStrideCh + j];
                count[2 /*PLANE_V*/][k] = modifier;
                ++k;
            }
        }
    }
}

// A11D-T1b: 1:1 port of svt_aom_apply_filtering_central_highbd_c
// (temporal_filtering.c:300-334). The uint16_t src triple; the
// structure is identical to the 8-bit twin.
void applyFilteringCentralHbd(bool tfChroma, const uint16_t* const* src,
                              uint32_t** accum, uint16_t** count,
                              uint16_t blkWidth, uint16_t blkHeight,
                              uint32_t ssX, uint32_t ssY, uint16_t srcStrideY) {
    uint16_t blkHeightCh = blkHeight >> ssY;
    uint16_t blkWidthCh  = blkWidth >> ssX;
    uint16_t srcStrideCh = srcStrideY >> ssX;

    const int modifier = 1000; /*TF_PLANEWISE_FILTER_WEIGHT_SCALE*/

    // Luma
    for (uint16_t k = 0, i = 0; i < blkHeight; i++) {
        for (uint16_t j = 0; j < blkWidth; j++) {
            accum[0 /*PLANE_Y*/][k] = modifier * src[0 /*PLANE_Y*/][i * srcStrideY + j];
            count[0 /*PLANE_Y*/][k] = modifier;
            ++k;
        }
    }

    // Chroma
    if (tfChroma) {
        for (uint16_t k = 0, i = 0; i < blkHeightCh; i++) {
            for (uint16_t j = 0; j < blkWidthCh; j++) {
                accum[1 /*PLANE_U*/][k] = modifier * src[1 /*PLANE_U*/][i * srcStrideCh + j];
                count[1 /*PLANE_U*/][k] = modifier;

                accum[2 /*PLANE_V*/][k] = modifier * src[2 /*PLANE_V*/][i * srcStrideCh + j];
                count[2 /*PLANE_V*/][k] = modifier;
                ++k;
            }
        }
    }
}

// A11D-T2: 1:1 port of svt_aom_get_final_filtered_pixels_c
// (temporal_filtering.c:2426-2489). The consumed MeContext field
// (tf_chroma) is threaded as the tfChroma parameter (the b64Variance
// carrier pattern); TF_BW/TF_BH = 64 (temporal_filtering.h:23-:24). The
// 8-bit and highbd destination triples are the reference's EbByte*/and
// uint16_t** shapes; the unused one is nullptr per call. The chroma row
// advance is stride[PLANE_U] (the reference's :2468), and the chroma
// k index is shared across U/V. The division is the OD_DIVU macro
// (bitstream_unit.h:48-54) over the AN14 table.
void getFinalFilteredPixels(bool tfChroma, bool isHighbd,
                            uint8_t* const* src8, uint16_t* const* src16,
                            uint32_t** accum, uint16_t** count,
                            const uint32_t* stride,
                            int blkYSrcOffset, int blkChSrcOffset,
                            uint16_t blkWidthCh, uint16_t blkHeightCh) {
    if (!isHighbd) {
        // Process luma
        int pos = blkYSrcOffset;
        for (int i = 0, k = 0; i < 64 /*TF_BH*/; i++) {
            for (int j = 0; j < 64 /*TF_BW*/; j++, k++) {
                assert(odDivu(accum[0 /*PLANE_Y*/][k] + (count[0 /*PLANE_Y*/][k] >> 1),
                              count[0 /*PLANE_Y*/][k]) < 256);
                src8[0 /*PLANE_Y*/][pos] =
                    (uint8_t)odDivu(accum[0 /*PLANE_Y*/][k] + (count[0 /*PLANE_Y*/][k] >> 1),
                                    count[0 /*PLANE_Y*/][k]);
                pos++;
            }
            pos += stride[0 /*PLANE_Y*/] - 64 /*TF_BW*/;
        }
        // Process chroma
        if (tfChroma) {
            pos = blkChSrcOffset;
            for (int i = 0, k = 0; i < blkHeightCh; i++) {
                for (int j = 0; j < blkWidthCh; j++, k++) {
                    assert(odDivu(accum[1 /*PLANE_U*/][k] + (count[1 /*PLANE_U*/][k] >> 1),
                                  count[1 /*PLANE_U*/][k]) < 256);
                    src8[1 /*PLANE_U*/][pos] =
                        (uint8_t)odDivu(accum[1 /*PLANE_U*/][k] + (count[1 /*PLANE_U*/][k] >> 1),
                                        count[1 /*PLANE_U*/][k]);
                    // The reference's second assert reads PLANE_U (the
                    // :2468 quirk) before the V write.
                    assert(odDivu(accum[1 /*PLANE_U*/][k] + (count[1 /*PLANE_U*/][k] >> 1),
                                  count[1 /*PLANE_U*/][k]) < 256);
                    src8[2 /*PLANE_V*/][pos] =
                        (uint8_t)odDivu(accum[2 /*PLANE_V*/][k] + (count[2 /*PLANE_V*/][k] >> 1),
                                        count[2 /*PLANE_V*/][k]);
                    pos++;
                }
                pos += stride[1 /*PLANE_U*/] - blkWidthCh;
            }
        }
    } else {
        // Process luma
        int pos = blkYSrcOffset;
        for (int i = 0, k = 0; i < 64 /*TF_BH*/; i++) {
            for (int j = 0; j < 64 /*TF_BW*/; j++, k++) {
                src16[0 /*PLANE_Y*/][pos] =
                    (uint16_t)odDivu(accum[0 /*PLANE_Y*/][k] + (count[0 /*PLANE_Y*/][k] >> 1),
                                     count[0 /*PLANE_Y*/][k]);
                pos++;
            }
            pos += stride[0 /*PLANE_Y*/] - 64 /*TF_BW*/;
        }
        // Process chroma
        if (tfChroma) {
            pos = blkChSrcOffset;
            for (int i = 0, k = 0; i < blkHeightCh; i++) {
                for (int j = 0; j < blkWidthCh; j++, k++) {
                    src16[1 /*PLANE_U*/][pos] =
                        (uint16_t)odDivu(accum[1 /*PLANE_U*/][k] + (count[1 /*PLANE_U*/][k] >> 1),
                                         count[1 /*PLANE_U*/][k]);
                    src16[2 /*PLANE_V*/][pos] =
                        (uint16_t)odDivu(accum[2 /*PLANE_V*/][k] + (count[2 /*PLANE_V*/][k] >> 1),
                                         count[2 /*PLANE_V*/][k]);
                    pos++;
                }
                pos += stride[1 /*PLANE_U*/] - blkWidthCh;
            }
        }
    }
}

// A11D-T3: tf_use_64x64_pred (temporal_filtering.c:2491-2507). The
// reference's MAX(x, 1) is a uint32 ternary; the port's std::max<uint32_t>
// is identical.
std::int8_t tfUse64x64Pred(const std::uint32_t* pBestSad32x32, std::uint32_t pBestSad64x64,
                           std::uint8_t tfUsePred64x64OnlyTh) {
    std::uint32_t dist32x32 = 0;

    // 32x32
    for (unsigned i = 0; i < 4; i++) {
        dist32x32 += pBestSad32x32[i];
    }

    std::uint32_t floor32 = std::max<std::uint32_t>(dist32x32, 1);
    std::uint32_t floor64 = std::max<std::uint32_t>(pBestSad64x64, 1);
    std::int64_t dev64x64To32x32 =
        (std::int64_t)(((std::int64_t)floor64 - (std::int64_t)floor32) * 100) /
        (std::int64_t)floor32;
    if (dev64x64To32x32 < tfUsePred64x64OnlyTh) {
        return 1;
    } else {
        return 0;
    }
}

// A11D-T4: the reference's svt_psnr.c HIGHBD_VAR instances are the fn_ptr->vf_hbd_10
// dispatch targets - the A13 file set, NOT the A11 brief's port list - the
// reference's svt_psnr.c HIGHBD_VAR instances are the dependency the reference's
// convert_64x64_info_to_32x32_info hbd branch calls. The reference's
// highbd_variance64 (svt_psnr.c:125-:143) is the uint64 sse / int64 sum
// accumulation core; the reference's highbd_10_variance (:146-:153) is the
// ROUND_POWER_OF_TWO dispatch (definitions.h:457) shift *sse = (sse_long + 8) >> 4
// and *sum = (sum_long + 2) >> 2; the reference's HIGHBD_VAR(W, H) macro
// (:155-:163) is the instance body: var = sse - sum*sum/(W*H), the clamp at 0.
// The CONVERT_TO_SHORTPTR staging is not present in this port (the port takes
// uint16 directly, the CS1/LR1 precedent).
namespace {
void highbd10VarCore(const std::uint16_t* a, int aStride, const std::uint16_t* b, int bStride,
                     int w, int h, std::uint64_t* sseLong, std::int64_t* sumLong) {
    std::int64_t tsum = 0;
    std::uint64_t tsse = 0;
    for (int i = 0; i < h; ++i) {
        std::int32_t lsum = 0;
        for (int j = 0; j < w; ++j) {
            const int diff = a[j] - b[j];
            lsum += diff;
            tsse += (std::uint32_t)(diff * diff);
        }
        tsum += lsum;
        a += aStride;
        b += bStride;
    }
    *sseLong = tsse;
    *sumLong = tsum;
}
}  // namespace

std::uint32_t highbd10Var32x32(const std::uint16_t* a, int aStride, const std::uint16_t* b, int bStride,
                               std::uint32_t* sse) {
    std::uint64_t sseLong = 0;
    std::int64_t sumLong = 0;
    highbd10VarCore(a, aStride, b, bStride, 32, 32, &sseLong, &sumLong);
    *sse = (std::uint32_t)((sseLong + 8) >> 4);
    const std::int32_t sum = (std::int32_t)((sumLong + 2) >> 2);
    const std::int64_t var = (std::int64_t)(*sse) - ((std::int64_t)sum * sum) / (32 * 32);
    return (var >= 0) ? (std::uint32_t)var : 0;
}

std::uint32_t highbd10Var32x16(const std::uint16_t* a, int aStride, const std::uint16_t* b, int bStride,
                               std::uint32_t* sse) {
    std::uint64_t sseLong = 0;
    std::int64_t sumLong = 0;
    highbd10VarCore(a, aStride, b, bStride, 32, 16, &sseLong, &sumLong);
    *sse = (std::uint32_t)((sseLong + 8) >> 4);
    const std::int32_t sum = (std::int32_t)((sumLong + 2) >> 2);
    const std::int64_t var = (std::int64_t)(*sse) - ((std::int64_t)sum * sum) / (32 * 16);
    return (var >= 0) ? (std::uint32_t)var : 0;
}

// A11D-T4: 1:1 port of convert_64x64_info_to_32x32_info
// (temporal_filtering.c:2509-2569). The four 32x32 sub-blocks of the
// 64x64 luma block; each block-error = fn_ptr->vf(pred, stride << shift,
// src, stride << shift, &sse) << shift. The consumed MeContext fields
// (tf_32x32_mv_x/y[4], tf_32x32_block_split_flag[4],
// tf_16x16_block_split_flag[4][4], tf_32x32_block_error[4], idx_32x32)
// are threaded as pointer arrays (the TF1 carrier pattern); the
// pcs->tf_ctrls.sub_sampling_shift is threaded as the subSamplingShift
// parameter. The 8-bit branch resolves to the landed analysis:: variance
// instances; the hbd branch resolves to the HIGHBD_VAR instances.
void convert64x64InfoTo32x32Info(
    std::uint8_t subSamplingShift,
    std::int16_t tf64x64MvX, std::int16_t tf64x64MvY,
    std::int16_t* tf32x32MvX,          // [4] out
    std::int16_t* tf32x32MvY,          // [4] out
    int* tf32x32BlockSplitFlag,        // [4] out = 0
    int* tf16x16BlockSplitFlag,        // [4][4] out = 0 (memset 16 ints)
    std::uint64_t* tf32x32BlockError,  // [4] out
    const uint8_t* const* pred8,       // [PLANE_Y] readable (may be nullptr if highbd)
    const uint16_t* const* pred16,     // [PLANE_Y] readable (may be nullptr if 8-bit)
    const uint32_t* stridePred,        // [PLANE_Y]
    const uint8_t* const* src8,        // [PLANE_Y]
    const uint16_t* const* src16,      // [PLANE_Y]
    const uint32_t* strideSrc,         // [PLANE_Y]
    bool isHighbd) {
    // The reference's four mv copies (the reference's :2513-:2522).
    for (int i = 0; i < 4; ++i) {
        tf32x32MvX[i] = tf64x64MvX;
        tf32x32MvY[i] = tf64x64MvY;
    }
    // The reference's :2524-:2527.
    for (int i = 0; i < 4; ++i) {
        tf32x32BlockSplitFlag[i] = 0;
    }
    // The reference's memset(ctx->tf_16x16_block_split_flag, 0,
    // sizeof(ctx->tf_16x16_block_split_flag[0][0]) * 4 * 4) - 16 ints.
    for (int i = 0; i < 16; ++i) {
        tf16x16BlockSplitFlag[i] = 0;
    }
    // The reference's "Update the 32x32 block-error" loop (:2530-:2568).
    constexpr std::uint32_t kPlaneY = 0;
    constexpr std::uint32_t kBlockSize = 32;
    for (int blockRow = 0; blockRow < 2; ++blockRow) {
        for (int blockCol = 0; blockCol < 2; ++blockCol) {
            std::uint64_t distortion;
            const std::uint32_t idx32x32 = blockCol + (blockRow << 1);
            if (!isHighbd) {
                const std::uint8_t* predYPtr =
                    pred8[kPlaneY] + kBlockSize * blockRow * stridePred[kPlaneY] + kBlockSize * blockCol;
                const std::uint8_t* srcYPtr =
                    src8[kPlaneY] + kBlockSize * blockRow * strideSrc[kPlaneY] + kBlockSize * blockCol;
                std::uint32_t sse = 0;
                if (subSamplingShift) {
                    distortion = (std::uint64_t)analysis::variance32x16(
                                     predYPtr, (int)(stridePred[kPlaneY] << subSamplingShift),
                                     srcYPtr, (int)(strideSrc[kPlaneY] << subSamplingShift), &sse)
                                 << subSamplingShift;
                } else {
                    distortion = (std::uint64_t)analysis::variance32x32(
                                     predYPtr, (int)stridePred[kPlaneY], srcYPtr, (int)strideSrc[kPlaneY], &sse)
                                 << subSamplingShift;
                }
            } else {
                const std::uint16_t* predYPtr =
                    pred16[kPlaneY] + kBlockSize * blockRow * stridePred[kPlaneY] + kBlockSize * blockCol;
                const std::uint16_t* srcYPtr =
                    src16[kPlaneY] + kBlockSize * blockRow * strideSrc[kPlaneY] + kBlockSize * blockCol;
                std::uint32_t sse = 0;
                if (subSamplingShift) {
                    distortion = (std::uint64_t)highbd10Var32x16(
                                     predYPtr, (int)(stridePred[kPlaneY] << subSamplingShift),
                                     srcYPtr, (int)(strideSrc[kPlaneY] << subSamplingShift), &sse)
                                 << subSamplingShift;
                } else {
                    distortion = (std::uint64_t)highbd10Var32x32(
                                     predYPtr, (int)stridePred[kPlaneY], srcYPtr, (int)strideSrc[kPlaneY], &sse)
                                 << subSamplingShift;
                }
            }
            tf32x32BlockError[idx32x32] = distortion;
        }
    }
}

}  // namespace temporal
}  // namespace analysis
