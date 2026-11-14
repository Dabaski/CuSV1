#include "grain.h"

#include <array>
#include <cassert>
#include <cstring>
#include <vector>

// AOMMIN (definitions.h:1001).
#define AOMMIN(x, y) (((x) < (y)) ? (x) : (y))

namespace l10_filters {

namespace grain {

// gaussian_sequence (grainSynthesis.c:28-:156), transcribed mechanically
// from the pinned reference.
const std::int32_t gaussianSequence[2048] = {
    56, 568, -180, 172, 124, -84, 172, -64,
    -900, 24, 820, 224, 1248, 996, 272, -8,
    -916, -388, -732, -104, -188, 800, 112, -652,
    -320, -376, 140, -252, 492, -168, 44, -788,
    588, -584, 500, -228, 12, 680, 272, -476,
    972, -100, 652, 368, 432, -196, -720, -192,
    1000, -332, 652, -136, -552, -604, -4, 192,
    -220, -136, 1000, -52, 372, -96, -624, 124,
    -24, 396, 540, -12, -104, 640, 464, 244,
    -208, -84, 368, -528, -740, 248, -968, -848,
    608, 376, -60, -292, -40, -156, 252, -292,
    248, 224, -280, 400, -244, 244, -60, 76,
    -80, 212, 532, 340, 128, -36, 824, -352,
    -60, -264, -96, -612, 416, -704, 220, -204,
    640, -160, 1220, -408, 900, 336, 20, -336,
    -96, -792, 304, 48, -28, -1232, -1172, -448,
    104, -292, -520, 244, 60, -948, 0, -708,
    268, 108, 356, -548, 488, -344, -136, 488,
    -196, -224, 656, -236, -1128, 60, 4, 140,
    276, -676, -376, 168, -108, 464, 8, 564,
    64, 240, 308, -300, -400, -456, -136, 56,
    120, -408, -116, 436, 504, -232, 328, 844,
    -164, -84, 784, -168, 232, -224, 348, -376,
    128, 568, 96, -1244, -288, 276, 848, 832,
    -360, 656, 464, -384, -332, -356, 728, -388,
    160, -192, 468, 296, 224, 140, -776, -100,
    280, 4, 196, 44, -36, -648, 932, 16,
    1428, 28, 528, 808, 772, 20, 268, 88,
    -332, -284, 124, -384, -448, 208, -228, -1044,
    -328, 660, 380, -148, -300, 588, 240, 540,
    28, 136, -88, -436, 256, 296, -1000, 1400,
    0, -48, 1056, -136, 264, -528, -1108, 632,
    -484, -592, -344, 796, 124, -668, -768, 388,
    1296, -232, -188, -200, -288, -4, 308, 100,
    -168, 256, -500, 204, -508, 648, -136, 372,
    -272, -120, -1004, -552, -548, -384, 548, -296,
    428, -108, -8, -912, -324, -224, -88, -112,
    -220, -100, 996, -796, 548, 360, -216, 180,
    428, -200, -212, 148, 96, 148, 284, 216,
    -412, -320, 120, -300, -384, -604, -572, -332,
    -8, -180, -176, 696, 116, -88, 628, 76,
    44, -516, 240, -208, -40, 100, -592, 344,
    -308, -452, -228, 20, 916, -1752, -136, -340,
    -804, 140, 40, 512, 340, 248, 184, -492,
    896, -156, 932, -628, 328, -688, -448, -616,
    -752, -100, 560, -1020, 180, -800, -64, 76,
    576, 1068, 396, 660, 552, -108, -28, 320,
    -628, 312, -92, -92, -472, 268, 16, 560,
    516, -672, -52, 492, -100, 260, 384, 284,
    292, 304, -148, 88, -152, 1012, 1064, -228,
    164, -376, -684, 592, -392, 156, 196, -524,
    -64, -884, 160, -176, 636, 648, 404, -396,
    -436, 864, 424, -728, 988, -604, 904, -592,
    296, -224, 536, -176, -920, 436, -48, 1176,
    -884, 416, -776, -824, -884, 524, -548, -564,
    -68, -164, -96, 692, 364, -692, -1012, -68,
    260, -480, 876, -1116, 452, -332, -352, 892,
    -1088, 1220, -676, 12, -292, 244, 496, 372,
    -32, 280, 200, 112, -440, -96, 24, -644,
    -184, 56, -432, 224, -980, 272, -260, 144,
    -436, 420, 356, 364, -528, 76, 172, -744,
    -368, 404, -752, -416, 684, -688, 72, 540,
    416, 92, 444, 480, -72, -1416, 164, -1172,
    -68, 24, 424, 264, 1040, 128, -912, -524,
    -356, 64, 876, -12, 4, -88, 532, 272,
    -524, 320, 276, -508, 940, 24, -400, -120,
    756, 60, 236, -412, 100, 376, -484, 400,
    -100, -740, -108, -260, 328, -268, 224, -200,
    -416, 184, -604, -564, -20, 296, 60, 892,
    -888, 60, 164, 68, -760, 216, -296, 904,
    -336, -28, 404, -356, -568, -208, -1480, -512,
    296, 328, -360, -164, -1560, -776, 1156, -428,
    164, -504, -112, 120, -216, -148, -264, 308,
    32, 64, -72, 72, 116, 176, -64, -272,
    460, -536, -784, -280, 348, 108, -752, -132,
    524, -540, -776, 116, -296, -1196, -288, -560,
    1040, -472, 116, -848, -1116, 116, 636, 696,
    284, -176, 1016, 204, -864, -648, -248, 356,
    972, -584, -204, 264, 880, 528, -24, -184,
    116, 448, -144, 828, 524, 212, -212, 52,
    12, 200, 268, -488, -404, -880, 824, -672,
    -40, 908, -248, 500, 716, -576, 492, -576,
    16, 720, -108, 384, 124, 344, 280, 576,
    -500, 252, 104, -308, 196, -188, -8, 1268,
    296, 1032, -1196, 436, 316, 372, -432, -200,
    -660, 704, -224, 596, -132, 268, 32, -452,
    884, 104, -1008, 424, -1348, -280, 4, -1168,
    368, 476, 696, 300, -8, 24, 180, -592,
    -196, 388, 304, 500, 724, -160, 244, -84,
    272, -256, -420, 320, 208, -144, -156, 156,
    364, 452, 28, 540, 316, 220, -644, -248,
    464, 72, 360, 32, -388, 496, -680, -48,
    208, -116, -408, 60, -604, -392, 548, -840,
    784, -460, 656, -544, -388, -264, 908, -800,
    -628, -612, -568, 572, -220, 164, 288, -16,
    -308, 308, -112, -636, -760, 280, -668, 432,
    364, 240, -196, 604, 340, 384, 196, 592,
    -44, -500, 432, -580, -132, 636, -76, 392,
    4, -412, 540, 508, 328, -356, -36, 16,
    -220, -64, -248, -60, 24, -192, 368, 1040,
    92, -24, -1044, -32, 40, 104, 148, 192,
    -136, -520, 56, -816, -224, 732, 392, 356,
    212, -80, -424, -1008, -324, 588, -1496, 576,
    460, -816, -848, 56, -580, -92, -1372, -112,
    -496, 200, 364, 52, -140, 48, -48, -60,
    84, 72, 40, 132, -356, -268, -104, -284,
    -404, 732, -520, 164, -304, -540, 120, 328,
    -76, -460, 756, 388, 588, 236, -436, -72,
    -176, -404, -316, -148, 716, -604, 404, -72,
    -88, -888, -68, 944, 88, -220, -344, 960,
    472, 460, -232, 704, 120, 832, -228, 692,
    -508, 132, -476, 844, -748, -364, -44, 1116,
    -1104, -1056, 76, 428, 552, -692, 60, 356,
    96, -384, -188, -612, -576, 736, 508, 892,
    352, -1132, 504, -24, -352, 324, 332, -600,
    -312, 292, 508, -144, -8, 484, 48, 284,
    -260, -240, 256, -100, -292, -204, -44, 472,
    -204, 908, -188, -1000, -256, 92, 1164, -392,
    564, 356, 652, -28, -884, 256, 484, -192,
    760, -176, 376, -524, -452, -436, 860, -736,
    212, 124, 504, -476, 468, 76, -472, 552,
    -692, -944, -620, 740, -240, 400, 132, 20,
    192, -196, 264, -668, -1012, -60, 296, -316,
    -828, 76, -156, 284, -768, -448, -832, 148,
    248, 652, 616, 1236, 288, -328, -400, -124,
    588, 220, 520, -696, 1032, 768, -740, -92,
    -272, 296, 448, -464, 412, -200, 392, 440,
    -200, 264, -152, -260, 320, 1032, 216, 320,
    -8, -64, 156, -1016, 1084, 1172, 536, 484,
    -432, 132, 372, -52, -256, 84, 116, -352,
    48, 116, 304, -384, 412, 924, -300, 528,
    628, 180, 648, 44, -980, -220, 1320, 48,
    332, 748, 524, -268, -720, 540, -276, 564,
    -344, -208, -196, 436, 896, 88, -392, 132,
    80, -964, -288, 568, 56, -48, -456, 888,
    8, 552, -156, -292, 948, 288, 128, -716,
    -292, 1192, -152, 876, 352, -600, -260, -812,
    -468, -28, -120, -32, -44, 1284, 496, 192,
    464, 312, -76, -516, -380, -456, -1012, -48,
    308, -156, 36, 492, -156, -808, 188, 1652,
    68, -120, -116, 316, 160, -140, 352, 808,
    -416, 592, 316, -480, 56, 528, -204, -568,
    372, -232, 752, -344, 744, -4, 324, -416,
    -600, 768, 268, -248, -88, -132, -420, -432,
    80, -288, 404, -316, -1216, -588, 520, -108,
    92, -320, 368, -480, -216, -92, 1688, -300,
    180, 1020, -176, 820, -68, -228, -260, 436,
    -904, 20, 40, -508, 440, -736, 312, 332,
    204, 760, -372, 728, 96, -20, -632, -520,
    -560, 336, 1076, -64, -532, 776, 584, 192,
    396, -728, -520, 276, -188, 80, -52, -612,
    -252, -48, 648, 212, -688, 228, -52, -260,
    428, -412, -272, -404, 180, 816, -796, 48,
    152, 484, -88, -216, 988, 696, 188, -528,
    648, -116, -180, 316, 476, 12, -564, 96,
    476, -252, -364, -376, -392, 556, -256, -576,
    260, -352, 120, -16, -136, -260, -492, 72,
    556, 660, 580, 616, 772, 436, 424, -32,
    -324, -1268, 416, -324, -80, 920, 160, 228,
    724, 32, -516, 64, 384, 68, -128, 136,
    240, 248, -204, -68, 252, -932, -120, -480,
    -628, -84, 192, 852, -404, -288, -132, 204,
    100, 168, -68, -196, -868, 460, 1080, 380,
    -80, 244, 0, 484, -888, 64, 184, 352,
    600, 460, 164, 604, -196, 320, -64, 588,
    -184, 228, 12, 372, 48, -848, -344, 224,
    208, -200, 484, 128, -20, 272, -468, -840,
    384, 256, -720, -520, -464, -580, 112, -120,
    644, -356, -208, -608, -528, 704, 560, -424,
    392, 828, 40, 84, 200, -152, 0, -144,
    584, 280, -120, 80, -556, -972, -196, -472,
    724, 80, 168, -32, 88, 160, -688, 0,
    160, 356, 372, -776, 740, -128, 676, -248,
    -480, 4, -364, 96, 544, 232, -1032, 956,
    236, 356, 20, -40, 300, 24, -676, -596,
    132, 1120, -104, 532, -1096, 568, 648, 444,
    508, 380, 188, -376, -604, 1488, 424, 24,
    756, -220, -192, 716, 120, 920, 688, 168,
    44, -460, 568, 284, 1144, 1160, 600, 424,
    888, 656, -356, -320, 220, 316, -176, -724,
    -188, -816, -628, -348, -228, -380, 1012, -452,
    -660, 736, 928, 404, -696, -72, -268, -892,
    128, 184, -344, -780, 360, 336, 400, 344,
    428, 548, -112, 136, -228, -216, -820, -516,
    340, 92, -136, 116, -300, 376, -244, 100,
    -316, -520, -284, -12, 824, 164, -548, -180,
    -128, 116, -924, -828, 268, -368, -580, 620,
    192, 160, 0, -1676, 1068, 424, -56, -360,
    468, -156, 720, 288, -528, 556, -364, 548,
    -148, 504, 316, 152, -648, -620, -684, -24,
    -376, -384, -108, -920, -1032, 768, 180, -264,
    -508, -1268, -260, -60, 300, -240, 988, 724,
    -376, -576, -212, -736, 556, 192, 1092, -620,
    -880, 376, -56, -4, -216, -32, 836, 268,
    396, 1332, 864, -600, 100, 56, -412, -92,
    356, 180, 884, -468, -436, 292, -388, -804,
    -704, -840, 368, -348, 140, -724, 1536, 940,
    372, 112, -372, 436, -480, 1136, 296, -32,
    -228, 132, -48, -220, 868, -1016, -60, -1044,
    -464, 328, 916, 244, 12, -736, -296, 360,
    468, -376, -108, -92, 788, 368, -56, 544,
    400, -672, -420, 728, 16, 320, 44, -284,
    -380, -796, 488, 132, 204, -596, -372, 88,
    -152, -908, -636, -572, -624, -116, -692, -200,
    -56, 276, -88, 484, -324, 948, 864, 1000,
    -456, -184, -276, 292, -296, 156, 676, 320,
    160, 908, -84, -1236, -288, -116, 260, -372,
    -644, 732, -756, -96, 84, 344, -520, 348,
    -688, 240, -84, 216, -1044, -136, -676, -396,
    -1500, 960, -40, 176, 168, 1516, 420, -504,
    -344, -364, -360, 1216, -940, -380, -212, 252,
    -660, -708, 484, -444, -152, 928, -120, 1112,
    476, -260, 560, -148, -344, 108, -196, 228,
    -288, 504, 560, -328, -88, 288, -1008, 460,
    -228, 468, -836, -196, 76, 388, 232, 412,
    -1168, -716, -644, 756, -172, -356, -504, 116,
    432, 528, 48, 476, -168, -608, 448, 160,
    -532, -272, 28, -676, -12, 828, 980, 456,
    520, 104, -104, 256, -344, -4, -28, -368,
    -52, -524, -572, -556, -200, 768, 1124, -208,
    -512, 176, 232, 248, -148, -888, 604, -600,
    -304, 804, -156, -212, 488, -192, -804, -256,
    368, -360, -916, -328, 228, -240, -448, -472,
    856, -556, -364, 572, -12, -156, -368, -340,
    432, 252, -752, -152, 288, 268, -580, -848,
    -592, 108, -76, 244, 312, -716, 592, -80,
    436, 360, 4, -248, 160, 516, 584, 732,
    44, -468, -280, -292, -156, -588, 28, 308,
    912, 24, 124, 156, 180, -252, 944, -924,
    -772, -520, -428, -624, 300, -212, -1144, 32,
    -724, 800, -1128, -212, -1288, -848, 180, -416,
    440, 192, -576, -792, -76, -1080, 80, -532,
    -352, -132, 380, -820, 148, 1112, 128, 164,
    456, 700, -924, 144, -668, -384, 648, -832,
    508, 552, -52, -100, -656, 208, -568, 748,
    -88, 680, 232, 300, 192, -408, -1012, -152,
    -252, -268, 272, -876, -664, -648, -332, -136,
    16, 12, 1152, -28, 332, -536, 320, -672,
    -460, -316, 532, -260, 228, -40, 1052, -816,
    180, 88, -496, -556, -672, -368, 428, 92,
    356, 404, -408, 252, 196, -176, -556, 792,
    268, 32, 372, 40, 96, -332, 328, 120,
    372, -900, -40, 472, -264, -592, 952, 128,
    656, 112, 664, -232, 420, 4, -344, -464,
    556, 244, -416, -32, 252, 0, -412, 188,
    -696, 508, -476, 324, -1096, 656, -312, 560,
    264, -136, 304, 160, -64, -580, 248, 336,
    -720, 560, -348, -288, -276, -196, -500, 852,
    -544, -236, -1128, -992, -776, 116, 56, 52,
    860, 884, 212, -12, 168, 1020, 512, -552,
    924, -148, 716, 188, 164, -340, -520, -184,
    880, -152, -680, -208, -1156, -300, -528, -472,
    364, 100, -744, -1056, -32, 540, 280, 144,
    -676, -32, -232, -280, -224, 96, 568, -76,
    172, 148, 148, 104, 32, -296, -32, 788,
    -80, 32, -16, 280, 288, 944, 428, -484,
};

// The module's file-local state, mirroring the reference's statics
// (grainSynthesis.c:172-:180): the LCG register, the three scaling LUTs,
// the grain clamp range and the subblock sizes the driver resets.
namespace {

std::uint16_t randomRegister = 0;

std::int32_t scalingLutY[256];
std::int32_t scalingLutCb[256];
std::int32_t scalingLutCr[256];

std::int32_t grainCenter = 0;
std::int32_t grainMin = 0;
std::int32_t grainMax = 0;

std::int32_t lumaSubblockSizeY = 32;
std::int32_t lumaSubblockSizeX = 32;

std::int32_t chromaSubblockSizeY = 16;
std::int32_t chromaSubblockSizeX = 16;

// gauss_bits (grainSynthesis.c:158).
constexpr std::int32_t gaussBits = 11;

// min/max legal ranges (grainSynthesis.c:166-:170).
constexpr std::int32_t minLumaLegalRange = 16;
constexpr std::int32_t maxLumaLegalRange = 235;
constexpr std::int32_t minChromaLegalRange = 16;
constexpr std::int32_t maxChromaLegalRange = 240;

// clamp (definitions.h:687).
std::int32_t clampVal(std::int32_t v, std::int32_t low, std::int32_t high) {
    return v < low ? low : (v > high ? high : v);
}

} // namespace

// get_random_number (grainSynthesis.c:306-:311).
std::int32_t getRandomNumber(std::int32_t bits) {
    std::uint16_t bit = ((randomRegister >> 0) ^ (randomRegister >> 1) ^
        (randomRegister >> 3) ^ (randomRegister >> 12)) & 1;
    randomRegister = (randomRegister >> 1) | (bit << 15);
    return (randomRegister >> (16 - bits)) & ((1 << bits) - 1);
}

// init_random_generator (grainSynthesis.c:313-:326).
void initRandomGenerator(std::int32_t lumaLine, std::uint16_t seed) {
    std::uint16_t msb = (seed >> 8) & 255;
    std::uint16_t lsb = seed & 255;

    randomRegister = (msb << 8) + lsb;

    std::int32_t lumaNum = lumaLine >> 5;

    randomRegister ^= ((lumaNum * 37 + 178) & 255) << 8;
    randomRegister ^= ((lumaNum * 173 + 105) & 255);
}

// init_scaling_function (grainSynthesis.c:466-:489).
void initScalingFunction(const std::int32_t scalingPoints[][2],
                         std::int32_t numPoints, std::int32_t* scalingLut) {
    if (numPoints == 0) {
        return;
    }

    for (std::int32_t i = 0; i < scalingPoints[0][0]; i++) {
        scalingLut[i] = scalingPoints[0][1];
    }

    for (std::int32_t point = 0; point < numPoints - 1; point++) {
        std::int64_t delta_y = scalingPoints[point + 1][1] - scalingPoints[point][1];
        std::int64_t delta_x = scalingPoints[point + 1][0] - scalingPoints[point][0];

        std::int64_t delta = delta_y * ((65536 + (delta_x >> 1)) / delta_x);

        for (std::int32_t x = 0; x < delta_x; x++) {
            scalingLut[scalingPoints[point][0] + x] =
                scalingPoints[point][1] +
                (std::int32_t)((x * delta + 32768) >> 16);
        }
    }

    for (std::int32_t i = scalingPoints[numPoints - 1][0]; i < 256; i++) {
        scalingLut[i] = scalingPoints[numPoints - 1][1];
    }
}

// scale_lut (grainSynthesis.c:493-:504).
std::int32_t scaleLut(std::int32_t* scalingLut, std::int32_t index,
                      std::int32_t bitDepth) {
    std::int32_t x = index >> (bitDepth - 8);

    if (!(bitDepth - 8) || x == 255) {
        return scalingLut[x];
    } else {
        return scalingLut[x] +
            (((scalingLut[x + 1] - scalingLut[x]) *
                 (index & ((1 << (bitDepth - 8)) - 1)) +
                 (1 << (bitDepth - 9))) >>
                (bitDepth - 8));
    }
}

// makeParams: port-side zero-init convenience (named deviation).
AomFilmGrain makeParams() {
    AomFilmGrain p;
    std::memset(&p, 0, sizeof(p));
    return p;
}

// init_arrays (grainSynthesis.c:182-:264). The memset of the module
// scaling LUTs (:188-:190) is kept; the malloc plumbing becomes vector
// resize (named deviation); the pred-pos table build is verbatim.
GrainWork initArrays(const AomFilmGrain* params, std::int32_t lumaStride,
                     std::int32_t chromaStride, std::int32_t lumaGrainSamples,
                     std::int32_t chromaGrainSamples,
                     std::int32_t chromaSubsampY, std::int32_t chromaSubsampX) {
    std::memset(scalingLutY, 0, sizeof(scalingLutY));
    std::memset(scalingLutCb, 0, sizeof(scalingLutCb));
    std::memset(scalingLutCr, 0, sizeof(scalingLutCr));

    std::int32_t numPosLuma = 2 * params->ar_coeff_lag * (params->ar_coeff_lag + 1);
    std::int32_t numPosChroma = numPosLuma;
    if (params->num_y_points > 0) {
        ++numPosChroma;
    }

    GrainWork w;
    w.predPosLuma.resize(numPosLuma);
    w.predPosChroma.resize(numPosChroma);

    std::int32_t posArIndex = 0;

    for (std::int32_t row = -params->ar_coeff_lag; row < 0; row++) {
        for (std::int32_t col = -params->ar_coeff_lag; col < params->ar_coeff_lag + 1; col++) {
            w.predPosLuma[posArIndex] = {row, col, 0};
            w.predPosChroma[posArIndex] = {row, col, 0};
            ++posArIndex;
        }
    }

    for (std::int32_t col = -params->ar_coeff_lag; col < 0; col++) {
        w.predPosLuma[posArIndex] = {0, col, 0};
        w.predPosChroma[posArIndex] = {0, col, 0};
        ++posArIndex;
    }

    if (params->num_y_points > 0) {
        w.predPosChroma[posArIndex] = {0, 0, 1};
    }

    w.yLineBuf.resize(lumaStride * 2);
    w.cbLineBuf.resize(chromaStride * (2 >> chromaSubsampY));
    w.crLineBuf.resize(chromaStride * (2 >> chromaSubsampY));

    w.yColBuf.resize((lumaSubblockSizeY + 2) * 2);
    w.cbColBuf.resize((chromaSubblockSizeY + (2 >> chromaSubsampY)) *
                      (2 >> chromaSubsampX));
    w.crColBuf.resize((chromaSubblockSizeY + (2 >> chromaSubsampY)) *
                      (2 >> chromaSubsampX));

    w.lumaGrainBlock.resize(lumaGrainSamples);
    w.cbGrainBlock.resize(chromaGrainSamples);
    w.crGrainBlock.resize(chromaGrainSamples);
    return w;
}

// initGrainRange (factored from grainSynthesis.c:916-:918).
void initGrainRange(std::int32_t bitDepth) {
    grainCenter = 128 << (bitDepth - 8);
    grainMin = 0 - grainCenter;
    grainMax = (256 << (bitDepth - 8)) - 1 - grainCenter;
}

// generate_luma_grain_block (grainSynthesis.c:328-:363).
void generateLumaGrainBlock(const AomFilmGrain* params,
                            const PredPosRow* predPosLuma,
                            std::int32_t* lumaGrainBlock,
                            std::int32_t lumaBlockSizeY,
                            std::int32_t lumaBlockSizeX,
                            std::int32_t lumaGrainStride,
                            std::int32_t leftPad, std::int32_t topPad,
                            std::int32_t rightPad, std::int32_t bottomPad) {
    if (params->num_y_points == 0) {
        return;
    }

    std::int32_t bitDepth = params->bit_depth;
    std::int32_t gaussSecShift = 12 - bitDepth + params->grain_scale_shift;

    std::int32_t numPosLuma = 2 * params->ar_coeff_lag * (params->ar_coeff_lag + 1);
    std::int32_t roundingOffset = (1 << (params->ar_coeff_shift - 1));

    for (std::int32_t i = 0; i < lumaBlockSizeY; i++) {
        for (std::int32_t j = 0; j < lumaBlockSizeX; j++) {
            lumaGrainBlock[i * lumaGrainStride + j] =
                (gaussianSequence[getRandomNumber(gaussBits)] +
                 ((1 << gaussSecShift) >> 1)) >> gaussSecShift;
        }
    }

    for (std::int32_t i = topPad; i < lumaBlockSizeY - bottomPad; i++) {
        for (std::int32_t j = leftPad; j < lumaBlockSizeX - rightPad; j++) {
            std::int32_t wsum = 0;
            for (std::int32_t pos = 0; pos < numPosLuma; pos++) {
                wsum = wsum +
                    params->ar_coeffs_y[pos] *
                        lumaGrainBlock[(i + predPosLuma[pos][0]) * lumaGrainStride +
                                       j + predPosLuma[pos][1]];
            }
            lumaGrainBlock[i * lumaGrainStride + j] = clampVal(
                lumaGrainBlock[i * lumaGrainStride + j] +
                    ((wsum + roundingOffset) >> params->ar_coeff_shift),
                grainMin, grainMax);
        }
    }
}

// generate_chroma_grain_blocks (grainSynthesis.c:365-:464).
void generateChromaGrainBlocks(
    const AomFilmGrain* params, const PredPosRow* predPosChroma,
    const std::int32_t* lumaGrainBlock, std::int32_t* cbGrainBlock,
    std::int32_t* crGrainBlock, std::int32_t lumaGrainStride,
    std::int32_t chromaBlockSizeY, std::int32_t chromaBlockSizeX,
    std::int32_t chromaGrainStride, std::int32_t leftPad,
    std::int32_t topPad, std::int32_t rightPad, std::int32_t bottomPad,
    std::int32_t chromaSubsampY, std::int32_t chromaSubsampX) {
    std::int32_t bitDepth = params->bit_depth;
    std::int32_t gaussSecShift = 12 - bitDepth + params->grain_scale_shift;

    std::int32_t numPosChroma = 2 * params->ar_coeff_lag * (params->ar_coeff_lag + 1);
    if (params->num_y_points > 0) {
        ++numPosChroma;
    }
    std::int32_t roundingOffset = (1 << (params->ar_coeff_shift - 1));

    std::int32_t chromaGrainBlockSize = chromaBlockSizeY * chromaGrainStride;

    if (params->num_cb_points || params->chroma_scaling_from_luma) {
        initRandomGenerator(7 << 5, params->random_seed);

        for (std::int32_t i = 0; i < chromaBlockSizeY; i++) {
            for (std::int32_t j = 0; j < chromaBlockSizeX; j++) {
                cbGrainBlock[i * chromaGrainStride + j] =
                    (gaussianSequence[getRandomNumber(gaussBits)] +
                     ((1 << gaussSecShift) >> 1)) >> gaussSecShift;
            }
        }
    } else {
        std::memset(cbGrainBlock, 0,
                    sizeof(*cbGrainBlock) * chromaGrainBlockSize);
    }
    if (params->num_cr_points || params->chroma_scaling_from_luma) {
        initRandomGenerator(11 << 5, params->random_seed);

        for (std::int32_t i = 0; i < chromaBlockSizeY; i++) {
            for (std::int32_t j = 0; j < chromaBlockSizeX; j++) {
                crGrainBlock[i * chromaGrainStride + j] =
                    (gaussianSequence[getRandomNumber(gaussBits)] +
                     ((1 << gaussSecShift) >> 1)) >> gaussSecShift;
            }
        }
    } else {
        std::memset(crGrainBlock, 0,
                    sizeof(*crGrainBlock) * chromaGrainBlockSize);
    }

    for (std::int32_t i = topPad; i < chromaBlockSizeY - bottomPad; i++) {
        for (std::int32_t j = leftPad; j < chromaBlockSizeX - rightPad; j++) {
            std::int32_t wsumCb = 0;
            std::int32_t wsumCr = 0;
            for (std::int32_t pos = 0; pos < numPosChroma; pos++) {
                if (predPosChroma[pos][2] == 0) {
                    wsumCb = wsumCb +
                        params->ar_coeffs_cb[pos] *
                            cbGrainBlock[(i + predPosChroma[pos][0]) * chromaGrainStride +
                                         j + predPosChroma[pos][1]];
                    wsumCr = wsumCr +
                        params->ar_coeffs_cr[pos] *
                            crGrainBlock[(i + predPosChroma[pos][0]) * chromaGrainStride +
                                         j + predPosChroma[pos][1]];
                } else if (predPosChroma[pos][2] == 1) {
                    std::int32_t avLuma = 0;
                    std::int32_t lumaCoordY =
                        ((i - topPad) << chromaSubsampY) + topPad;
                    std::int32_t lumaCoordX =
                        ((j - leftPad) << chromaSubsampX) + leftPad;

                    for (std::int32_t k = lumaCoordY;
                         k < lumaCoordY + chromaSubsampY + 1; k++) {
                        for (std::int32_t l = lumaCoordX;
                             l < lumaCoordX + chromaSubsampX + 1; l++) {
                            avLuma += lumaGrainBlock[k * lumaGrainStride + l];
                        }
                    }

                    avLuma = (avLuma +
                              ((1 << (chromaSubsampY + chromaSubsampX)) >> 1)) >>
                        (chromaSubsampY + chromaSubsampX);

                    wsumCb = wsumCb + params->ar_coeffs_cb[pos] * avLuma;
                    wsumCr = wsumCr + params->ar_coeffs_cr[pos] * avLuma;
                } else {
                    // Dead branch in the reference (SVT_ERROR + exit(1),
                    // :442-:445): predPosChroma[pos][2] is written only as
                    // 0 or 1 by init_arrays, so no third value exists.
                    // Named deviation: assert instead of exit.
                    assert(false);
                }
            }
            if (params->num_cb_points || params->chroma_scaling_from_luma) {
                cbGrainBlock[i * chromaGrainStride + j] = clampVal(
                    cbGrainBlock[i * chromaGrainStride + j] +
                        ((wsumCb + roundingOffset) >> params->ar_coeff_shift),
                    grainMin, grainMax);
            }
            if (params->num_cr_points || params->chroma_scaling_from_luma) {
                crGrainBlock[i * chromaGrainStride + j] = clampVal(
                    crGrainBlock[i * chromaGrainStride + j] +
                        ((wsumCr + roundingOffset) >> params->ar_coeff_shift),
                    grainMin, grainMax);
            }
        }
    }
}

// initScalingLuts (factored from grainSynthesis.c:967-:975).
void initScalingLuts(const AomFilmGrain* params,
                     const std::int32_t yPoints[][2],
                     const std::int32_t cbPoints[][2],
                     const std::int32_t crPoints[][2]) {
    initScalingFunction(yPoints, params->num_y_points, scalingLutY);
    if (params->chroma_scaling_from_luma) {
        std::memcpy(scalingLutCb, scalingLutY, sizeof(scalingLutY));
        std::memcpy(scalingLutCr, scalingLutY, sizeof(scalingLutY));
    } else {
        initScalingFunction(cbPoints, params->num_cb_points, scalingLutCb);
        initScalingFunction(crPoints, params->num_cr_points, scalingLutCr);
    }
}

// add_noise_to_block (grainSynthesis.c:506-:605).
void addNoiseToBlock(const AomFilmGrain* params, std::uint8_t* luma,
                     std::uint8_t* cb, std::uint8_t* cr,
                     std::int32_t lumaStride, std::int32_t chromaStride,
                     const std::int32_t* lumaGrain,
                     const std::int32_t* cbGrain, const std::int32_t* crGrain,
                     std::int32_t lumaGrainStride,
                     std::int32_t chromaGrainStride,
                     std::int32_t halfLumaHeight, std::int32_t halfLumaWidth,
                     std::int32_t bitDepth, std::int32_t chromaSubsampY,
                     std::int32_t chromaSubsampX) {
    std::int32_t cbMult = params->cb_mult - 128;
    std::int32_t cbLumaMult = params->cb_luma_mult - 128;
    std::int32_t cbOffset = params->cb_offset - 256;

    std::int32_t crMult = params->cr_mult - 128;
    std::int32_t crLumaMult = params->cr_luma_mult - 128;
    std::int32_t crOffset = params->cr_offset - 256;

    std::int32_t roundingOffset = (1 << (params->scaling_shift - 1));

    std::int32_t applyY = params->num_y_points > 0 ? 1 : 0;
    std::int32_t applyCb =
        (params->num_cb_points > 0 || params->chroma_scaling_from_luma) ? 1 : 0;
    std::int32_t applyCr =
        (params->num_cr_points > 0 || params->chroma_scaling_from_luma) ? 1 : 0;

    if (params->chroma_scaling_from_luma) {
        cbMult = 0;
        cbLumaMult = 64;
        cbOffset = 0;

        crMult = 0;
        crLumaMult = 64;
        crOffset = 0;
    }

    std::int32_t minLuma, maxLuma, minChroma, maxChroma;

    if (params->clip_to_restricted_range) {
        minLuma = minLumaLegalRange;
        maxLuma = maxLumaLegalRange;

        minChroma = minChromaLegalRange;
        maxChroma = maxChromaLegalRange;
    } else {
        minLuma = minChroma = 0;
        maxLuma = maxChroma = 255;
    }

    for (std::int32_t i = 0; i < (halfLumaHeight << (1 - chromaSubsampY)); i++) {
        for (std::int32_t j = 0; j < (halfLumaWidth << (1 - chromaSubsampX)); j++) {
            std::int32_t averageLuma = 0;
            if (chromaSubsampX) {
                averageLuma =
                    (luma[(i << chromaSubsampY) * lumaStride + (j << chromaSubsampX)] +
                     luma[(i << chromaSubsampY) * lumaStride +
                          (j << chromaSubsampX) + 1] + 1) >> 1;
            } else {
                averageLuma =
                    luma[(i << chromaSubsampY) * lumaStride + j];
            }
            if (applyCb) {
                cb[i * chromaStride + j] = (std::uint8_t)clampVal(
                    cb[i * chromaStride + j] +
                        ((scaleLut(scalingLutCb,
                                   clampVal(((averageLuma * cbLumaMult +
                                              cbMult * cb[i * chromaStride + j]) >> 6) +
                                          cbOffset,
                                   0, (256 << (bitDepth - 8)) - 1),
                                   8) *
                              cbGrain[i * chromaGrainStride + j] +
                          roundingOffset) >> params->scaling_shift),
                    minChroma, maxChroma);
            }

            if (applyCr) {
                cr[i * chromaStride + j] = (std::uint8_t)clampVal(
                    cr[i * chromaStride + j] +
                        ((scaleLut(scalingLutCr,
                                   clampVal(((averageLuma * crLumaMult +
                                              crMult * cr[i * chromaStride + j]) >> 6) +
                                          crOffset,
                                   0, (256 << (bitDepth - 8)) - 1),
                                   8) *
                              crGrain[i * chromaGrainStride + j] +
                          roundingOffset) >> params->scaling_shift),
                    minChroma, maxChroma);
            }
        }
    }

    if (applyY) {
        for (std::int32_t i = 0; i < (halfLumaHeight << 1); i++) {
            for (std::int32_t j = 0; j < (halfLumaWidth << 1); j++) {
                luma[i * lumaStride + j] = (std::uint8_t)clampVal(
                    luma[i * lumaStride + j] +
                        ((scaleLut(scalingLutY, luma[i * lumaStride + j], 8) *
                              lumaGrain[i * lumaGrainStride + j] +
                          roundingOffset) >> params->scaling_shift),
                    minLuma, maxLuma);
            }
        }
    }
}

// add_noise_to_block_hbd (grainSynthesis.c:607-:709). 1:1: the uint16
// planes are the reference's own signature; the bd-scaled offsets/clamps
// and the scale_lut bit_depth are the only differences from the 8-bit
// twin. The apply gate here is num_cb_points/num_cr_points ONLY
// (:624-:626) - the 8-bit twin's CSFL clause (:522-:523) is NOT carried,
// so the twins diverge for CSFL without points (reference behavior,
// pinned by test).
void addNoiseToBlockHbd(const AomFilmGrain* params, std::uint16_t* luma,
                        std::uint16_t* cb, std::uint16_t* cr,
                        std::int32_t lumaStride, std::int32_t chromaStride,
                        const std::int32_t* lumaGrain,
                        const std::int32_t* cbGrain, const std::int32_t* crGrain,
                        std::int32_t lumaGrainStride,
                        std::int32_t chromaGrainStride,
                        std::int32_t halfLumaHeight, std::int32_t halfLumaWidth,
                        std::int32_t bitDepth, std::int32_t chromaSubsampY,
                        std::int32_t chromaSubsampX) {
    std::int32_t cbMult = params->cb_mult - 128;
    std::int32_t cbLumaMult = params->cb_luma_mult - 128;
    // offset value depends on the bit depth
    std::int32_t cbOffset =
        (params->cb_offset << (bitDepth - 8)) - (1 << bitDepth);

    std::int32_t crMult = params->cr_mult - 128;
    std::int32_t crLumaMult = params->cr_luma_mult - 128;
    std::int32_t crOffset =
        (params->cr_offset << (bitDepth - 8)) - (1 << bitDepth);

    std::int32_t roundingOffset = (1 << (params->scaling_shift - 1));

    std::int32_t applyY = params->num_y_points > 0 ? 1 : 0;
    std::int32_t applyCb = params->num_cb_points > 0 ? 1 : 0;
    std::int32_t applyCr = params->num_cr_points > 0 ? 1 : 0;

    if (params->chroma_scaling_from_luma) {
        cbMult = 0;
        cbLumaMult = 64;
        cbOffset = 0;

        crMult = 0;
        crLumaMult = 64;
        crOffset = 0;
    }

    std::int32_t minLuma, maxLuma, minChroma, maxChroma;

    if (params->clip_to_restricted_range) {
        minLuma = minLumaLegalRange << (bitDepth - 8);
        maxLuma = maxLumaLegalRange << (bitDepth - 8);

        minChroma = minChromaLegalRange << (bitDepth - 8);
        maxChroma = maxChromaLegalRange << (bitDepth - 8);
    } else {
        minLuma = minChroma = 0;
        maxLuma = maxChroma = (256 << (bitDepth - 8)) - 1;
    }

    for (std::int32_t i = 0; i < (halfLumaHeight << (1 - chromaSubsampY)); i++) {
        for (std::int32_t j = 0; j < (halfLumaWidth << (1 - chromaSubsampX)); j++) {
            std::int32_t averageLuma = 0;
            if (chromaSubsampX) {
                averageLuma = (luma[(i << chromaSubsampY) * lumaStride + (j << chromaSubsampX)] +
                               luma[(i << chromaSubsampY) * lumaStride +
                                    (j << chromaSubsampX) + 1] + 1) >> 1;
            } else {
                averageLuma = luma[(i << chromaSubsampY) * lumaStride + j];
            }
            if (applyCb) {
                cb[i * chromaStride + j] = (std::uint16_t)clampVal(
                    cb[i * chromaStride + j] +
                        ((scaleLut(scalingLutCb,
                                   clampVal(((averageLuma * cbLumaMult +
                                              cbMult * cb[i * chromaStride + j]) >> 6) +
                                          cbOffset,
                                   0, (256 << (bitDepth - 8)) - 1),
                                   bitDepth) *
                          cbGrain[i * chromaGrainStride + j] +
                      roundingOffset) >> params->scaling_shift),
                    minChroma, maxChroma);
            }

            if (applyCr) {
                cr[i * chromaStride + j] = (std::uint16_t)clampVal(
                    cr[i * chromaStride + j] +
                        ((scaleLut(scalingLutCr,
                                   clampVal(((averageLuma * crLumaMult +
                                              crMult * cr[i * chromaStride + j]) >> 6) +
                                          crOffset,
                                   0, (256 << (bitDepth - 8)) - 1),
                                   bitDepth) *
                          crGrain[i * chromaGrainStride + j] +
                      roundingOffset) >> params->scaling_shift),
                    minChroma, maxChroma);
            }
        }
    }

    if (applyY) {
        for (std::int32_t i = 0; i < (halfLumaHeight << 1); i++) {
            for (std::int32_t j = 0; j < (halfLumaWidth << 1); j++) {
                luma[i * lumaStride + j] = (std::uint16_t)clampVal(
                    luma[i * lumaStride + j] +
                        ((scaleLut(scalingLutY, luma[i * lumaStride + j], bitDepth) *
                              lumaGrain[i * lumaGrainStride + j] +
                          roundingOffset) >> params->scaling_shift),
                    minLuma, maxLuma);
            }
        }
    }
}

// ver_boundary_overlap (grainSynthesis.c:816-:838).
void verBoundaryOverlap(std::int32_t* leftBlock, std::int32_t leftStride,
                        std::int32_t* rightBlock, std::int32_t rightStride,
                        std::int32_t* dstBlock, std::int32_t dstStride,
                        std::int32_t width, std::int32_t height) {
    if (width == 1) {
        while (height) {
            *dstBlock = clampVal((*leftBlock * 23 + *rightBlock * 22 + 16) >> 5,
                                 grainMin, grainMax);
            leftBlock += leftStride;
            rightBlock += rightStride;
            dstBlock += dstStride;
            --height;
        }
        return;
    } else if (width == 2) {
        while (height) {
            dstBlock[0] =
                clampVal((27 * leftBlock[0] + 17 * rightBlock[0] + 16) >> 5,
                         grainMin, grainMax);
            dstBlock[1] =
                clampVal((17 * leftBlock[1] + 27 * rightBlock[1] + 16) >> 5,
                         grainMin, grainMax);
            leftBlock += leftStride;
            rightBlock += rightStride;
            dstBlock += dstStride;
            --height;
        }
        return;
    }
}

// hor_boundary_overlap (grainSynthesis.c:840-:863).
void horBoundaryOverlap(std::int32_t* topBlock, std::int32_t topStride,
                        std::int32_t* bottomBlock, std::int32_t bottomStride,
                        std::int32_t* dstBlock, std::int32_t dstStride,
                        std::int32_t width, std::int32_t height) {
    if (height == 1) {
        while (width) {
            *dstBlock = clampVal((*topBlock * 23 + *bottomBlock * 22 + 16) >> 5,
                                 grainMin, grainMax);
            ++topBlock;
            ++bottomBlock;
            ++dstBlock;
            --width;
        }
        return;
    } else if (height == 2) {
        while (width) {
            dstBlock[0] = clampVal(
                (27 * topBlock[0] + 17 * bottomBlock[0] + 16) >> 5, grainMin,
                grainMax);
            dstBlock[dstStride] = clampVal(
                (17 * topBlock[topStride] + 27 * bottomBlock[bottomStride] + 16) >> 5,
                grainMin, grainMax);
            ++topBlock;
            ++bottomBlock;
            ++dstBlock;
            --width;
        }
        return;
    }
}

// film_grain_params_equal (grainSynthesis.c:709-:791).
std::int32_t filmGrainParamsEqual(const AomFilmGrain* parsA,
                                  const AomFilmGrain* parsB) {
    if (parsA->apply_grain != parsB->apply_grain) return 0;
    if (parsA->overlap_flag != parsB->overlap_flag) return 0;
    if (parsA->clip_to_restricted_range != parsB->clip_to_restricted_range) return 0;
    if (parsA->chroma_scaling_from_luma != parsB->chroma_scaling_from_luma) return 0;
    if (parsA->grain_scale_shift != parsB->grain_scale_shift) return 0;
    if (parsA->ar_coeff_shift != parsB->ar_coeff_shift) return 0;
    if (parsA->cb_mult != parsB->cb_mult) return 0;
    if (parsA->cb_luma_mult != parsB->cb_luma_mult) return 0;
    if (parsA->cb_offset != parsB->cb_offset) return 0;
    if (parsA->cr_mult != parsB->cr_mult) return 0;
    if (parsA->cr_luma_mult != parsB->cr_luma_mult) return 0;
    if (parsA->cr_offset != parsB->cr_offset) return 0;
    if (parsA->scaling_shift != parsB->scaling_shift) return 0;
    if (parsA->ar_coeff_lag != parsB->ar_coeff_lag) return 0;
    if (parsA->num_y_points != parsB->num_y_points) return 0;
    if (parsA->num_cb_points != parsB->num_cb_points) return 0;
    if (parsA->num_cr_points != parsB->num_cr_points) return 0;
    if (std::memcmp(parsA->scaling_points_y, parsB->scaling_points_y,
                    sizeof(parsB->scaling_points_y))) return 0;
    if (std::memcmp(parsA->scaling_points_cb, parsB->scaling_points_cb,
                    sizeof(parsB->scaling_points_cb))) return 0;
    if (std::memcmp(parsA->scaling_points_cr, parsB->scaling_points_cr,
                    sizeof(parsB->scaling_points_cr))) return 0;
    if (std::memcmp(parsA->ar_coeffs_y, parsB->ar_coeffs_y,
                    sizeof(parsB->ar_coeffs_y))) return 0;
    if (std::memcmp(parsA->ar_coeffs_cb, parsB->ar_coeffs_cb,
                    sizeof(parsB->ar_coeffs_cb))) return 0;
    if (std::memcmp(parsA->ar_coeffs_cr, parsB->ar_coeffs_cr,
                    sizeof(parsB->ar_coeffs_cr))) return 0;
    return 1;
}

// svt_aom_fgn_copy_rect (grainSynthesis.c:793-:803); svt_memcpy -> memcpy.
void fgnCopyRect(std::uint8_t* src, std::int32_t srcStride,
                 std::uint8_t* dst, std::int32_t dstStride, std::int32_t width,
                 std::int32_t height, std::int32_t useHighBitDepth) {
    std::int32_t hbdCoeff = useHighBitDepth ? 2 : 1;
    while (height) {
        std::memcpy(dst, src, width * sizeof(uint8_t) * hbdCoeff);
        src += srcStride * hbdCoeff;
        dst += dstStride * hbdCoeff;
        --height;
    }
}

// copy_area (grainSynthesis.c:805-:814); SVT_MEMCPY -> memcpy.
void copyArea(std::int32_t* src, std::int32_t srcStride, std::int32_t* dst,
              std::int32_t dstStride, std::int32_t width,
              std::int32_t height) {
    while (height) {
        std::memcpy(dst, src, width * sizeof(*src));
        src += srcStride;
        dst += dstStride;
        --height;
    }
}

// svt_av1_add_film_grain_run (grainSynthesis.c:865-:1294). The AOMMIN
// ternary and the raw pointer walks mirror the reference; the malloc
// working set is the GrainWork from initArrays.
void addFilmGrainRun(const AomFilmGrain* params, std::uint8_t* luma,
                     std::uint8_t* cb, std::uint8_t* cr, std::int32_t height,
                     std::int32_t width, std::int32_t lumaStride,
                     std::int32_t chromaStride, std::int32_t useHighBitDepth,
                     std::int32_t chromaSubsampY,
                     std::int32_t chromaSubsampX) {
    (void)useHighBitDepth; // highbd branch parked (named deviation)

    randomRegister = params->random_seed;

    std::int32_t leftPad = 3;
    std::int32_t rightPad = 3;
    std::int32_t topPad = 3;
    std::int32_t bottomPad = 0;

    std::int32_t arPadding = 3;

    lumaSubblockSizeY = 32;
    lumaSubblockSizeX = 32;

    chromaSubblockSizeY = lumaSubblockSizeY >> chromaSubsampY;
    chromaSubblockSizeX = lumaSubblockSizeX >> chromaSubsampX;

    std::int32_t lumaBlockSizeY =
        topPad + 2 * arPadding + lumaSubblockSizeY * 2 + bottomPad;
    std::int32_t lumaBlockSizeX =
        leftPad + 2 * arPadding + lumaSubblockSizeX * 2 + 2 * arPadding + rightPad;

    std::int32_t chromaBlockSizeY =
        topPad + (2 >> chromaSubsampY) * arPadding + chromaSubblockSizeY * 2 +
        bottomPad;
    std::int32_t chromaBlockSizeX =
        leftPad + (2 >> chromaSubsampX) * arPadding + chromaSubblockSizeX * 2 +
        (2 >> chromaSubsampX) * arPadding + rightPad;

    std::int32_t lumaGrainStride = lumaBlockSizeX;
    std::int32_t chromaGrainStride = chromaBlockSizeX;

    std::int32_t overlap = params->overlap_flag;
    std::int32_t bitDepth = params->bit_depth;

    initGrainRange(bitDepth);

    GrainWork w = initArrays(params, lumaStride, chromaStride,
                             lumaBlockSizeY * lumaBlockSizeX,
                             chromaBlockSizeY * chromaBlockSizeX,
                             chromaSubsampY, chromaSubsampX);

    generateLumaGrainBlock(params, w.predPosLuma.data(), w.lumaGrainBlock.data(),
                           lumaBlockSizeY, lumaBlockSizeX, lumaGrainStride,
                           leftPad, topPad, rightPad, bottomPad);

    generateChromaGrainBlocks(params, w.predPosChroma.data(),
                              w.lumaGrainBlock.data(), w.cbGrainBlock.data(),
                              w.crGrainBlock.data(), lumaGrainStride,
                              chromaBlockSizeY, chromaBlockSizeX,
                              chromaGrainStride, leftPad, topPad, rightPad,
                              bottomPad, chromaSubsampY, chromaSubsampX);

    // LUT init (the driver block :967-:975, through the factored helper).
    initScalingLuts(params, params->scaling_points_y, params->scaling_points_cb,
                    params->scaling_points_cr);

    std::int32_t* yLineBuf = w.yLineBuf.data();
    std::int32_t* cbLineBuf = w.cbLineBuf.data();
    std::int32_t* crLineBuf = w.crLineBuf.data();
    std::int32_t* yColBuf = w.yColBuf.data();
    std::int32_t* cbColBuf = w.cbColBuf.data();
    std::int32_t* crColBuf = w.crColBuf.data();
    std::int32_t* lumaGrainBlock = w.lumaGrainBlock.data();
    std::int32_t* cbGrainBlock = w.cbGrainBlock.data();
    std::int32_t* crGrainBlock = w.crGrainBlock.data();
    (void)bitDepth;

    for (std::int32_t y = 0; y < height / 2; y += (lumaSubblockSizeY >> 1)) {
        initRandomGenerator(y * 2, params->random_seed);

        for (std::int32_t x = 0; x < width / 2; x += (lumaSubblockSizeX >> 1)) {
            std::int32_t offsetY = getRandomNumber(8);
            std::int32_t offsetX = (offsetY >> 4) & 15;
            offsetY &= 15;

            std::int32_t lumaOffsetY = leftPad + 2 * arPadding + (offsetY << 1);
            std::int32_t lumaOffsetX = topPad + 2 * arPadding + (offsetX << 1);

            std::int32_t chromaOffsetY = topPad +
                (2 >> chromaSubsampY) * arPadding + offsetY * (2 >> chromaSubsampY);
            std::int32_t chromaOffsetX = leftPad +
                (2 >> chromaSubsampX) * arPadding + offsetX * (2 >> chromaSubsampX);

            if (overlap && x) {
                verBoundaryOverlap(yColBuf, 2,
                    lumaGrainBlock + lumaOffsetY * lumaGrainStride + lumaOffsetX,
                    lumaGrainStride, yColBuf, 2, 2,
                    AOMMIN(lumaSubblockSizeY + 2, height - (y << 1)));

                verBoundaryOverlap(cbColBuf, 2 >> chromaSubsampX,
                    cbGrainBlock + chromaOffsetY * chromaGrainStride +
                        chromaOffsetX,
                    chromaGrainStride, cbColBuf, 2 >> chromaSubsampX,
                    2 >> chromaSubsampX,
                    AOMMIN(chromaSubblockSizeY + (2 >> chromaSubsampY),
                           (height - (y << 1)) >> chromaSubsampY));

                verBoundaryOverlap(crColBuf, 2 >> chromaSubsampX,
                    crGrainBlock + chromaOffsetY * chromaGrainStride +
                        chromaOffsetX,
                    chromaGrainStride, crColBuf, 2 >> chromaSubsampX,
                    2 >> chromaSubsampX,
                    AOMMIN(chromaSubblockSizeY + (2 >> chromaSubsampY),
                           (height - (y << 1)) >> chromaSubsampY));

                std::int32_t i = y ? 1 : 0;

                addNoiseToBlock(params,
                    luma + ((y + i) << 1) * lumaStride + (x << 1),
                    cb + ((y + i) << (1 - chromaSubsampY)) * chromaStride +
                        (x << (1 - chromaSubsampX)),
                    cr + ((y + i) << (1 - chromaSubsampY)) * chromaStride +
                        (x << (1 - chromaSubsampX)),
                    lumaStride, chromaStride,
                    yColBuf + i * 4,
                    cbColBuf + i * (2 - chromaSubsampY) * (2 - chromaSubsampX),
                    crColBuf + i * (2 - chromaSubsampY) * (2 - chromaSubsampX),
                    2, (2 - chromaSubsampX),
                    AOMMIN(lumaSubblockSizeY >> 1, height / 2 - y) - i,
                    1, bitDepth, chromaSubsampY, chromaSubsampX);
            }

            if (overlap && y) {
                if (x) {
                    horBoundaryOverlap(yLineBuf + (x << 1), lumaStride,
                                       yColBuf, 2, yLineBuf + (x << 1),
                                       lumaStride, 2, 2);

                    horBoundaryOverlap(
                        cbLineBuf + x * (2 >> chromaSubsampX), chromaStride,
                        cbColBuf, 2 >> chromaSubsampX,
                        cbLineBuf + x * (2 >> chromaSubsampX), chromaStride,
                        2 >> chromaSubsampX, 2 >> chromaSubsampY);

                    horBoundaryOverlap(
                        crLineBuf + x * (2 >> chromaSubsampX), chromaStride,
                        crColBuf, 2 >> chromaSubsampX,
                        crLineBuf + x * (2 >> chromaSubsampX), chromaStride,
                        2 >> chromaSubsampX, 2 >> chromaSubsampY);
                }

                horBoundaryOverlap(yLineBuf + ((x ? x + 1 : 0) << 1), lumaStride,
                    lumaGrainBlock + lumaOffsetY * lumaGrainStride + lumaOffsetX +
                        (x ? 2 : 0),
                    lumaGrainStride, yLineBuf + ((x ? x + 1 : 0) << 1),
                    lumaStride,
                    AOMMIN(lumaSubblockSizeX - ((x ? 1 : 0) << 1),
                           width - ((x ? x + 1 : 0) << 1)),
                    2);

                horBoundaryOverlap(cbLineBuf + ((x ? x + 1 : 0) << (1 - chromaSubsampX)),
                    chromaStride,
                    cbGrainBlock + chromaOffsetY * chromaGrainStride +
                        chromaOffsetX + ((x ? 1 : 0) << (1 - chromaSubsampX)),
                    chromaGrainStride,
                    cbLineBuf + ((x ? x + 1 : 0) << (1 - chromaSubsampX)),
                    chromaStride,
                    AOMMIN(chromaSubblockSizeX - ((x ? 1 : 0) << (1 - chromaSubsampX)),
                           (width - ((x ? x + 1 : 0) << 1)) >> chromaSubsampX),
                    2 >> chromaSubsampY);

                horBoundaryOverlap(crLineBuf + ((x ? x + 1 : 0) << (1 - chromaSubsampX)),
                    chromaStride,
                    crGrainBlock + chromaOffsetY * chromaGrainStride +
                        chromaOffsetX + ((x ? 1 : 0) << (1 - chromaSubsampX)),
                    chromaGrainStride,
                    crLineBuf + ((x ? x + 1 : 0) << (1 - chromaSubsampX)),
                    chromaStride,
                    AOMMIN(chromaSubblockSizeX - ((x ? 1 : 0) << (1 - chromaSubsampX)),
                           (width - ((x ? x + 1 : 0) << 1)) >> chromaSubsampX),
                    2 >> chromaSubsampY);

                addNoiseToBlock(params,
                    luma + (y << 1) * lumaStride + (x << 1),
                    cb + (y << (1 - chromaSubsampY)) * chromaStride +
                        (x << (1 - chromaSubsampX)),
                    cr + (y << (1 - chromaSubsampY)) * chromaStride +
                        (x << (1 - chromaSubsampX)),
                    lumaStride, chromaStride,
                    yLineBuf + (x << 1),
                    cbLineBuf + (x << (1 - chromaSubsampX)),
                    crLineBuf + (x << (1 - chromaSubsampX)),
                    lumaStride, chromaStride, 1,
                    AOMMIN(lumaSubblockSizeX >> 1, width / 2 - x), bitDepth,
                    chromaSubsampY, chromaSubsampX);
            }

            std::int32_t i = overlap && y ? 1 : 0;
            std::int32_t j = overlap && x ? 1 : 0;

            addNoiseToBlock(params,
                luma + ((y + i) << 1) * lumaStride + ((x + j) << 1),
                cb + ((y + i) << (1 - chromaSubsampY)) * chromaStride +
                    ((x + j) << (1 - chromaSubsampX)),
                cr + ((y + i) << (1 - chromaSubsampY)) * chromaStride +
                    ((x + j) << (1 - chromaSubsampX)),
                lumaStride, chromaStride,
                lumaGrainBlock + (lumaOffsetY + (i << 1)) * lumaGrainStride +
                    lumaOffsetX + (j << 1),
                cbGrainBlock + (chromaOffsetY + (i << (1 - chromaSubsampY))) *
                        chromaGrainStride +
                    chromaOffsetX + (j << (1 - chromaSubsampX)),
                crGrainBlock + (chromaOffsetY + (i << (1 - chromaSubsampY))) *
                        chromaGrainStride +
                    chromaOffsetX + (j << (1 - chromaSubsampX)),
                lumaGrainStride, chromaGrainStride,
                AOMMIN(lumaSubblockSizeY >> 1, height / 2 - y) - i,
                AOMMIN(lumaSubblockSizeX >> 1, width / 2 - x) - j, bitDepth,
                chromaSubsampY, chromaSubsampX);

            if (overlap) {
                if (x) {
                    // Copy overlapped column buffer to line buffer
                    copyArea(yColBuf + (lumaSubblockSizeY << 1), 2,
                             yLineBuf + (x << 1), lumaStride, 2, 2);

                    copyArea(cbColBuf + (chromaSubblockSizeY << (1 - chromaSubsampX)),
                             2 >> chromaSubsampX,
                             cbLineBuf + (x << (1 - chromaSubsampX)),
                             chromaStride, 2 >> chromaSubsampX,
                             2 >> chromaSubsampY);

                    copyArea(crColBuf + (chromaSubblockSizeY << (1 - chromaSubsampX)),
                             2 >> chromaSubsampX,
                             crLineBuf + (x << (1 - chromaSubsampX)),
                             chromaStride, 2 >> chromaSubsampX,
                             2 >> chromaSubsampY);
                }

                // Copy grain to the line buffer for overlap with a bottom block
                copyArea(lumaGrainBlock +
                             (lumaOffsetY + lumaSubblockSizeY) * lumaGrainStride +
                             lumaOffsetX + ((x ? 2 : 0)),
                         lumaGrainStride, yLineBuf + ((x ? x + 1 : 0) << 1),
                         lumaStride,
                         AOMMIN(lumaSubblockSizeX, width - (x << 1)) - (x ? 2 : 0),
                         2);

                copyArea(cbGrainBlock +
                             (chromaOffsetY + chromaSubblockSizeY) *
                                 chromaGrainStride +
                             chromaOffsetX + (x ? 2 >> chromaSubsampX : 0),
                         chromaGrainStride,
                         cbLineBuf + ((x ? x + 1 : 0) << (1 - chromaSubsampX)),
                         chromaStride,
                         AOMMIN(chromaSubblockSizeX,
                                ((width - (x << 1)) >> chromaSubsampX)) -
                             (x ? 2 >> chromaSubsampX : 0),
                         2 >> chromaSubsampY);

                copyArea(crGrainBlock +
                             (chromaOffsetY + chromaSubblockSizeY) *
                                 chromaGrainStride +
                             chromaOffsetX + (x ? 2 >> chromaSubsampX : 0),
                         chromaGrainStride,
                         crLineBuf + ((x ? x + 1 : 0) << (1 - chromaSubsampX)),
                         chromaStride,
                         AOMMIN(chromaSubblockSizeX,
                                ((width - (x << 1)) >> chromaSubsampX)) -
                             (x ? 2 >> chromaSubsampX : 0),
                         2 >> chromaSubsampY);

                // Copy grain to the column buffer for overlap with the next
                // block to the right
                copyArea(lumaGrainBlock + lumaOffsetY * lumaGrainStride +
                             lumaOffsetX + lumaSubblockSizeX,
                         lumaGrainStride, yColBuf, 2, 2,
                         AOMMIN(lumaSubblockSizeY + 2, height - (y << 1)));

                copyArea(cbGrainBlock + chromaOffsetY * chromaGrainStride +
                             chromaOffsetX + chromaSubblockSizeX,
                         chromaGrainStride, cbColBuf, 2 >> chromaSubsampX,
                         2 >> chromaSubsampX,
                         AOMMIN(chromaSubblockSizeY + (2 >> chromaSubsampY),
                                (height - (y << 1)) >> chromaSubsampY));

                copyArea(crGrainBlock + chromaOffsetY * chromaGrainStride +
                             chromaOffsetX + chromaSubblockSizeX,
                         chromaGrainStride, crColBuf, 2 >> chromaSubsampX,
                         2 >> chromaSubsampX,
                         AOMMIN(chromaSubblockSizeY + (2 >> chromaSubsampY),
                                (height - (y << 1)) >> chromaSubsampY));
            }
        }
    }

    // dealloc_arrays (:1282-:1293): RAII no-op (named deviation).
}

} // namespace grain

} // namespace l10_filters
