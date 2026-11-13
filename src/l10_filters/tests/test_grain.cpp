#include <doctest.h>
#include "grain.h"

#include <cstdint>
#include <vector>

namespace {
// Independent method-B transcription (grainSynthesis.c:29-:156),
// 12 per line, whitespace-split parser.
const std::int32_t kGaussB[2048] = {
    56, 568, -180, 172, 124, -84, 172, -64, -900, 24, 820, 224,
    1248, 996, 272, -8, -916, -388, -732, -104, -188, 800, 112, -652,
    -320, -376, 140, -252, 492, -168, 44, -788, 588, -584, 500, -228,
    12, 680, 272, -476, 972, -100, 652, 368, 432, -196, -720, -192,
    1000, -332, 652, -136, -552, -604, -4, 192, -220, -136, 1000, -52,
    372, -96, -624, 124, -24, 396, 540, -12, -104, 640, 464, 244,
    -208, -84, 368, -528, -740, 248, -968, -848, 608, 376, -60, -292,
    -40, -156, 252, -292, 248, 224, -280, 400, -244, 244, -60, 76,
    -80, 212, 532, 340, 128, -36, 824, -352, -60, -264, -96, -612,
    416, -704, 220, -204, 640, -160, 1220, -408, 900, 336, 20, -336,
    -96, -792, 304, 48, -28, -1232, -1172, -448, 104, -292, -520, 244,
    60, -948, 0, -708, 268, 108, 356, -548, 488, -344, -136, 488,
    -196, -224, 656, -236, -1128, 60, 4, 140, 276, -676, -376, 168,
    -108, 464, 8, 564, 64, 240, 308, -300, -400, -456, -136, 56,
    120, -408, -116, 436, 504, -232, 328, 844, -164, -84, 784, -168,
    232, -224, 348, -376, 128, 568, 96, -1244, -288, 276, 848, 832,
    -360, 656, 464, -384, -332, -356, 728, -388, 160, -192, 468, 296,
    224, 140, -776, -100, 280, 4, 196, 44, -36, -648, 932, 16,
    1428, 28, 528, 808, 772, 20, 268, 88, -332, -284, 124, -384,
    -448, 208, -228, -1044, -328, 660, 380, -148, -300, 588, 240, 540,
    28, 136, -88, -436, 256, 296, -1000, 1400, 0, -48, 1056, -136,
    264, -528, -1108, 632, -484, -592, -344, 796, 124, -668, -768, 388,
    1296, -232, -188, -200, -288, -4, 308, 100, -168, 256, -500, 204,
    -508, 648, -136, 372, -272, -120, -1004, -552, -548, -384, 548, -296,
    428, -108, -8, -912, -324, -224, -88, -112, -220, -100, 996, -796,
    548, 360, -216, 180, 428, -200, -212, 148, 96, 148, 284, 216,
    -412, -320, 120, -300, -384, -604, -572, -332, -8, -180, -176, 696,
    116, -88, 628, 76, 44, -516, 240, -208, -40, 100, -592, 344,
    -308, -452, -228, 20, 916, -1752, -136, -340, -804, 140, 40, 512,
    340, 248, 184, -492, 896, -156, 932, -628, 328, -688, -448, -616,
    -752, -100, 560, -1020, 180, -800, -64, 76, 576, 1068, 396, 660,
    552, -108, -28, 320, -628, 312, -92, -92, -472, 268, 16, 560,
    516, -672, -52, 492, -100, 260, 384, 284, 292, 304, -148, 88,
    -152, 1012, 1064, -228, 164, -376, -684, 592, -392, 156, 196, -524,
    -64, -884, 160, -176, 636, 648, 404, -396, -436, 864, 424, -728,
    988, -604, 904, -592, 296, -224, 536, -176, -920, 436, -48, 1176,
    -884, 416, -776, -824, -884, 524, -548, -564, -68, -164, -96, 692,
    364, -692, -1012, -68, 260, -480, 876, -1116, 452, -332, -352, 892,
    -1088, 1220, -676, 12, -292, 244, 496, 372, -32, 280, 200, 112,
    -440, -96, 24, -644, -184, 56, -432, 224, -980, 272, -260, 144,
    -436, 420, 356, 364, -528, 76, 172, -744, -368, 404, -752, -416,
    684, -688, 72, 540, 416, 92, 444, 480, -72, -1416, 164, -1172,
    -68, 24, 424, 264, 1040, 128, -912, -524, -356, 64, 876, -12,
    4, -88, 532, 272, -524, 320, 276, -508, 940, 24, -400, -120,
    756, 60, 236, -412, 100, 376, -484, 400, -100, -740, -108, -260,
    328, -268, 224, -200, -416, 184, -604, -564, -20, 296, 60, 892,
    -888, 60, 164, 68, -760, 216, -296, 904, -336, -28, 404, -356,
    -568, -208, -1480, -512, 296, 328, -360, -164, -1560, -776, 1156, -428,
    164, -504, -112, 120, -216, -148, -264, 308, 32, 64, -72, 72,
    116, 176, -64, -272, 460, -536, -784, -280, 348, 108, -752, -132,
    524, -540, -776, 116, -296, -1196, -288, -560, 1040, -472, 116, -848,
    -1116, 116, 636, 696, 284, -176, 1016, 204, -864, -648, -248, 356,
    972, -584, -204, 264, 880, 528, -24, -184, 116, 448, -144, 828,
    524, 212, -212, 52, 12, 200, 268, -488, -404, -880, 824, -672,
    -40, 908, -248, 500, 716, -576, 492, -576, 16, 720, -108, 384,
    124, 344, 280, 576, -500, 252, 104, -308, 196, -188, -8, 1268,
    296, 1032, -1196, 436, 316, 372, -432, -200, -660, 704, -224, 596,
    -132, 268, 32, -452, 884, 104, -1008, 424, -1348, -280, 4, -1168,
    368, 476, 696, 300, -8, 24, 180, -592, -196, 388, 304, 500,
    724, -160, 244, -84, 272, -256, -420, 320, 208, -144, -156, 156,
    364, 452, 28, 540, 316, 220, -644, -248, 464, 72, 360, 32,
    -388, 496, -680, -48, 208, -116, -408, 60, -604, -392, 548, -840,
    784, -460, 656, -544, -388, -264, 908, -800, -628, -612, -568, 572,
    -220, 164, 288, -16, -308, 308, -112, -636, -760, 280, -668, 432,
    364, 240, -196, 604, 340, 384, 196, 592, -44, -500, 432, -580,
    -132, 636, -76, 392, 4, -412, 540, 508, 328, -356, -36, 16,
    -220, -64, -248, -60, 24, -192, 368, 1040, 92, -24, -1044, -32,
    40, 104, 148, 192, -136, -520, 56, -816, -224, 732, 392, 356,
    212, -80, -424, -1008, -324, 588, -1496, 576, 460, -816, -848, 56,
    -580, -92, -1372, -112, -496, 200, 364, 52, -140, 48, -48, -60,
    84, 72, 40, 132, -356, -268, -104, -284, -404, 732, -520, 164,
    -304, -540, 120, 328, -76, -460, 756, 388, 588, 236, -436, -72,
    -176, -404, -316, -148, 716, -604, 404, -72, -88, -888, -68, 944,
    88, -220, -344, 960, 472, 460, -232, 704, 120, 832, -228, 692,
    -508, 132, -476, 844, -748, -364, -44, 1116, -1104, -1056, 76, 428,
    552, -692, 60, 356, 96, -384, -188, -612, -576, 736, 508, 892,
    352, -1132, 504, -24, -352, 324, 332, -600, -312, 292, 508, -144,
    -8, 484, 48, 284, -260, -240, 256, -100, -292, -204, -44, 472,
    -204, 908, -188, -1000, -256, 92, 1164, -392, 564, 356, 652, -28,
    -884, 256, 484, -192, 760, -176, 376, -524, -452, -436, 860, -736,
    212, 124, 504, -476, 468, 76, -472, 552, -692, -944, -620, 740,
    -240, 400, 132, 20, 192, -196, 264, -668, -1012, -60, 296, -316,
    -828, 76, -156, 284, -768, -448, -832, 148, 248, 652, 616, 1236,
    288, -328, -400, -124, 588, 220, 520, -696, 1032, 768, -740, -92,
    -272, 296, 448, -464, 412, -200, 392, 440, -200, 264, -152, -260,
    320, 1032, 216, 320, -8, -64, 156, -1016, 1084, 1172, 536, 484,
    -432, 132, 372, -52, -256, 84, 116, -352, 48, 116, 304, -384,
    412, 924, -300, 528, 628, 180, 648, 44, -980, -220, 1320, 48,
    332, 748, 524, -268, -720, 540, -276, 564, -344, -208, -196, 436,
    896, 88, -392, 132, 80, -964, -288, 568, 56, -48, -456, 888,
    8, 552, -156, -292, 948, 288, 128, -716, -292, 1192, -152, 876,
    352, -600, -260, -812, -468, -28, -120, -32, -44, 1284, 496, 192,
    464, 312, -76, -516, -380, -456, -1012, -48, 308, -156, 36, 492,
    -156, -808, 188, 1652, 68, -120, -116, 316, 160, -140, 352, 808,
    -416, 592, 316, -480, 56, 528, -204, -568, 372, -232, 752, -344,
    744, -4, 324, -416, -600, 768, 268, -248, -88, -132, -420, -432,
    80, -288, 404, -316, -1216, -588, 520, -108, 92, -320, 368, -480,
    -216, -92, 1688, -300, 180, 1020, -176, 820, -68, -228, -260, 436,
    -904, 20, 40, -508, 440, -736, 312, 332, 204, 760, -372, 728,
    96, -20, -632, -520, -560, 336, 1076, -64, -532, 776, 584, 192,
    396, -728, -520, 276, -188, 80, -52, -612, -252, -48, 648, 212,
    -688, 228, -52, -260, 428, -412, -272, -404, 180, 816, -796, 48,
    152, 484, -88, -216, 988, 696, 188, -528, 648, -116, -180, 316,
    476, 12, -564, 96, 476, -252, -364, -376, -392, 556, -256, -576,
    260, -352, 120, -16, -136, -260, -492, 72, 556, 660, 580, 616,
    772, 436, 424, -32, -324, -1268, 416, -324, -80, 920, 160, 228,
    724, 32, -516, 64, 384, 68, -128, 136, 240, 248, -204, -68,
    252, -932, -120, -480, -628, -84, 192, 852, -404, -288, -132, 204,
    100, 168, -68, -196, -868, 460, 1080, 380, -80, 244, 0, 484,
    -888, 64, 184, 352, 600, 460, 164, 604, -196, 320, -64, 588,
    -184, 228, 12, 372, 48, -848, -344, 224, 208, -200, 484, 128,
    -20, 272, -468, -840, 384, 256, -720, -520, -464, -580, 112, -120,
    644, -356, -208, -608, -528, 704, 560, -424, 392, 828, 40, 84,
    200, -152, 0, -144, 584, 280, -120, 80, -556, -972, -196, -472,
    724, 80, 168, -32, 88, 160, -688, 0, 160, 356, 372, -776,
    740, -128, 676, -248, -480, 4, -364, 96, 544, 232, -1032, 956,
    236, 356, 20, -40, 300, 24, -676, -596, 132, 1120, -104, 532,
    -1096, 568, 648, 444, 508, 380, 188, -376, -604, 1488, 424, 24,
    756, -220, -192, 716, 120, 920, 688, 168, 44, -460, 568, 284,
    1144, 1160, 600, 424, 888, 656, -356, -320, 220, 316, -176, -724,
    -188, -816, -628, -348, -228, -380, 1012, -452, -660, 736, 928, 404,
    -696, -72, -268, -892, 128, 184, -344, -780, 360, 336, 400, 344,
    428, 548, -112, 136, -228, -216, -820, -516, 340, 92, -136, 116,
    -300, 376, -244, 100, -316, -520, -284, -12, 824, 164, -548, -180,
    -128, 116, -924, -828, 268, -368, -580, 620, 192, 160, 0, -1676,
    1068, 424, -56, -360, 468, -156, 720, 288, -528, 556, -364, 548,
    -148, 504, 316, 152, -648, -620, -684, -24, -376, -384, -108, -920,
    -1032, 768, 180, -264, -508, -1268, -260, -60, 300, -240, 988, 724,
    -376, -576, -212, -736, 556, 192, 1092, -620, -880, 376, -56, -4,
    -216, -32, 836, 268, 396, 1332, 864, -600, 100, 56, -412, -92,
    356, 180, 884, -468, -436, 292, -388, -804, -704, -840, 368, -348,
    140, -724, 1536, 940, 372, 112, -372, 436, -480, 1136, 296, -32,
    -228, 132, -48, -220, 868, -1016, -60, -1044, -464, 328, 916, 244,
    12, -736, -296, 360, 468, -376, -108, -92, 788, 368, -56, 544,
    400, -672, -420, 728, 16, 320, 44, -284, -380, -796, 488, 132,
    204, -596, -372, 88, -152, -908, -636, -572, -624, -116, -692, -200,
    -56, 276, -88, 484, -324, 948, 864, 1000, -456, -184, -276, 292,
    -296, 156, 676, 320, 160, 908, -84, -1236, -288, -116, 260, -372,
    -644, 732, -756, -96, 84, 344, -520, 348, -688, 240, -84, 216,
    -1044, -136, -676, -396, -1500, 960, -40, 176, 168, 1516, 420, -504,
    -344, -364, -360, 1216, -940, -380, -212, 252, -660, -708, 484, -444,
    -152, 928, -120, 1112, 476, -260, 560, -148, -344, 108, -196, 228,
    -288, 504, 560, -328, -88, 288, -1008, 460, -228, 468, -836, -196,
    76, 388, 232, 412, -1168, -716, -644, 756, -172, -356, -504, 116,
    432, 528, 48, 476, -168, -608, 448, 160, -532, -272, 28, -676,
    -12, 828, 980, 456, 520, 104, -104, 256, -344, -4, -28, -368,
    -52, -524, -572, -556, -200, 768, 1124, -208, -512, 176, 232, 248,
    -148, -888, 604, -600, -304, 804, -156, -212, 488, -192, -804, -256,
    368, -360, -916, -328, 228, -240, -448, -472, 856, -556, -364, 572,
    -12, -156, -368, -340, 432, 252, -752, -152, 288, 268, -580, -848,
    -592, 108, -76, 244, 312, -716, 592, -80, 436, 360, 4, -248,
    160, 516, 584, 732, 44, -468, -280, -292, -156, -588, 28, 308,
    912, 24, 124, 156, 180, -252, 944, -924, -772, -520, -428, -624,
    300, -212, -1144, 32, -724, 800, -1128, -212, -1288, -848, 180, -416,
    440, 192, -576, -792, -76, -1080, 80, -532, -352, -132, 380, -820,
    148, 1112, 128, 164, 456, 700, -924, 144, -668, -384, 648, -832,
    508, 552, -52, -100, -656, 208, -568, 748, -88, 680, 232, 300,
    192, -408, -1012, -152, -252, -268, 272, -876, -664, -648, -332, -136,
    16, 12, 1152, -28, 332, -536, 320, -672, -460, -316, 532, -260,
    228, -40, 1052, -816, 180, 88, -496, -556, -672, -368, 428, 92,
    356, 404, -408, 252, 196, -176, -556, 792, 268, 32, 372, 40,
    96, -332, 328, 120, 372, -900, -40, 472, -264, -592, 952, 128,
    656, 112, 664, -232, 420, 4, -344, -464, 556, 244, -416, -32,
    252, 0, -412, 188, -696, 508, -476, 324, -1096, 656, -312, 560,
    264, -136, 304, 160, -64, -580, 248, 336, -720, 560, -348, -288,
    -276, -196, -500, 852, -544, -236, -1128, -992, -776, 116, 56, 52,
    860, 884, 212, -12, 168, 1020, 512, -552, 924, -148, 716, 188,
    164, -340, -520, -184, 880, -152, -680, -208, -1156, -300, -528, -472,
    364, 100, -744, -1056, -32, 540, 280, 144, -676, -32, -232, -280,
    -224, 96, 568, -76, 172, 148, 148, 104, 32, -296, -32, 788,
    -80, 32, -16, 280, 288, 944, 428, -484,
};
} // namespace


// Hand anchors read directly from grainSynthesis.c:29-:32 and :156 (entry 0,
// entry 15, entry 16, entry 31, entry 2047).
TEST_CASE("l10 grain gaussian table: count, hand anchors, range scan") {
    CHECK_EQ(l10_filters::grain::gaussianSequenceSize, 2048);
    CHECK_EQ(l10_filters::grain::gaussianSequence[0], 56);
    CHECK_EQ(l10_filters::grain::gaussianSequence[15], -8);
    CHECK_EQ(l10_filters::grain::gaussianSequence[16], -916);
    CHECK_EQ(l10_filters::grain::gaussianSequence[31], -788);
    CHECK_EQ(l10_filters::grain::gaussianSequence[2047], -484);
    for (int i = 0; i < 2048; ++i) {
        CHECK_GE(l10_filters::grain::gaussianSequence[i], -2048);
        CHECK_LE(l10_filters::grain::gaussianSequence[i], 2047);
    }
}

// diff-verify ALL entries: the port table must equal the independent
// method-B transcription entry by entry.
TEST_CASE("l10 grain gaussian table: full diff vs method-B transcription") {
    bool allEqual = true;
    for (int i = 0; i < 2048; ++i) {
        if (l10_filters::grain::gaussianSequence[i] != kGaussB[i]) {
            allEqual = false;
            FAIL("gaussian mismatch at ", i, ": port ",
                 l10_filters::grain::gaussianSequence[i], " vs B ", kGaussB[i]);
        }
    }
    CHECK(allEqual);
}

// get_random_number (grainSynthesis.c:306-:311) + init_random_generator
// (:313-:326). Hand-derived anchor: seed 0xABCD, luma_line 0.
// init: register = 0xABCD; the xor pair applies UNCONDITIONALLY - at
// luma_num = 0 the terms are (178 & 255) << 8 = 0xB200 and (105 & 255)
// = 0x0069, so register = 0xABCD ^ 0xB200 ^ 0x0069 = 0x19A4.
// First draw, bits = 11: 0x19A4 = 0001 1001 1010 0100, bit =
// b0^b1^b3^b12 = 0^0^0^1 = 1; register = (0x19A4 >> 1) | 0x8000 = 0x8CD2;
// return (0x8CD2 >> 5) & 0x7FF = 1126.
TEST_CASE("l10 grain LCG: hand-derived first draw, seed 0xABCD, 11 bits") {
    l10_filters::grain::initRandomGenerator(0, 0xABCD);
    CHECK_EQ(l10_filters::grain::getRandomNumber(11), 1126);
}

// Nonzero-row anchor, hand-derived: seed 0x0001, luma_line 32 ->
// luma_num = 1, register = 0x0001 ^ ((215 & 255) << 8) ^ ((278 & 255))
// = 0x0001 ^ 0xD700 ^ 0x0016 = 0xD717. First draw, bits = 11:
// 0xD717 = 1101 0111 0001 0111, bit = b0^b1^b3^b12 = 1^1^0^1 = 1;
// register = (0xD717 >> 1) | 0x8000 = 0xEB8B; draw = (0xEB8B >> 5) & 0x7FF
// = 1884.
TEST_CASE("l10 grain LCG: luma_line 32 row xor, seed 0x0001") {
    l10_filters::grain::initRandomGenerator(32, 0x0001);
    CHECK_EQ(l10_filters::grain::getRandomNumber(11), 1884);
}

// Reproducibility: re-initializing with the same seed replays the same
// walk (pin: the first 8 draws of two runs agree pairwise).
TEST_CASE("l10 grain LCG: reproducibility across re-init") {
    l10_filters::grain::initRandomGenerator(0, 0x1234);
    std::int32_t first[8];
    for (int i = 0; i < 8; ++i) {
        first[i] = l10_filters::grain::getRandomNumber(11);
    }
    l10_filters::grain::initRandomGenerator(0, 0x1234);
    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(l10_filters::grain::getRandomNumber(11), first[i]);
    }
}

// Golden 32-draw prefix, seed 0x1234, row 0 (register after init =
// 0xA05D; the values are derived from the reference LCG formula
// grainSynthesis.c:306-:311, computed externally and pinned here).
TEST_CASE("l10 grain LCG: golden 32-draw 11-bit prefix, seed 0x1234") {
    l10_filters::grain::initRandomGenerator(0, 0x1234);
    const std::int32_t expect[32] = {
        641, 1344, 672, 336, 1192, 596, 1322, 661, 1354, 677, 338, 169,
        1108, 1578, 789, 1418, 709, 1378, 689, 1368, 1708, 1878, 1963,
        981, 490, 245, 122, 1085, 1566, 1807, 903, 451};
    for (int i = 0; i < 32; ++i) {
        CHECK_EQ(l10_filters::grain::getRandomNumber(11), expect[i]);
    }
}

// Structural scan: the taps {0, 1, 3, 12} make a maximal-length LFSR -
// 65,535 register steps return any state to itself, and each
// getRandomNumber call steps the register exactly once regardless of the
// bit width. So: draw 16 values, issue 65,535 one-bit draws, and the
// 11-bit stream resumes with draws 16..23 of the original walk (the
// golden-prefix values).
TEST_CASE("l10 grain LCG: maximal period 65535 register steps") {
    l10_filters::grain::initRandomGenerator(0, 0x1234);
    for (int i = 0; i < 16; ++i) {
        l10_filters::grain::getRandomNumber(11);
    }
    for (std::int32_t i = 0; i < 65535; ++i) {
        l10_filters::grain::getRandomNumber(1);
    }
    const std::int32_t expect[8] = {
        709, 1378, 689, 1368, 1708, 1878, 1963, 981};
    bool resumed = true;
    for (int i = 0; i < 8; ++i) {
        if (l10_filters::grain::getRandomNumber(11) != expect[i]) {
            resumed = false;
        }
    }
    CHECK(resumed);
}

// init_scaling_function (grainSynthesis.c:466-:489), hand-derived
// anchors for the two-point set {(0,0), (64,32)}: dx = 64, dy = 32,
// delta = 32 * ((65536 + 32) / 64) = 32768, so lut[x] =
// (x * 32768 + 32768) >> 16 = floor((x + 1) / 2) for x in 0..63, and the
// tail fill (x >= 64) is flat at 32. Pre-filled sentinel -1000 must be
// fully overwritten (no gaps).
TEST_CASE("l10 grain initScalingFunction: two-point set hand anchors") {
    std::int32_t lut[256];
    for (int i = 0; i < 256; ++i) {
        lut[i] = -1000;
    }
    const std::int32_t pts[2][2] = {{0, 0}, {64, 32}};
    l10_filters::grain::initScalingFunction(pts, 2, lut);
    CHECK_EQ(lut[0], 0);
    CHECK_EQ(lut[1], 1);
    CHECK_EQ(lut[2], 1);
    CHECK_EQ(lut[63], 32);
    CHECK_EQ(lut[64], 32);
    CHECK_EQ(lut[255], 32);
    bool noSentinel = true;
    for (int i = 0; i < 256; ++i) {
        if (lut[i] == -1000) {
            noSentinel = false;
        }
    }
    CHECK(noSentinel);
}

namespace {

// Independent closed-form oracle for init_scaling_function, written from
// the reference semantics (grainSynthesis.c:466-:489), not from the port:
// flat below the first point's x; between consecutive points p, p+1 the
// entry at offset x is p.y + ((x * delta + 32768) >> 16) with
// delta = dy * ((65536 + (dx >> 1)) / dx); flat at/after the last point.
std::int32_t oracleScalingLut[256];

void oracleInitScaling(const std::int32_t pts[][2], int n) {
    for (int i = 0; i < 256; ++i) {
        oracleScalingLut[i] = -777;
    }
    if (n == 0) {
        return;
    }
    for (int i = 0; i < pts[0][0]; ++i) {
        oracleScalingLut[i] = pts[0][1];
    }
    for (int p = 0; p < n - 1; ++p) {
        std::int64_t dy = pts[p + 1][1] - pts[p][1];
        std::int64_t dx = pts[p + 1][0] - pts[p][0];
        std::int64_t delta = dy * ((65536 + (dx >> 1)) / dx);
        for (int x = 0; x < dx; ++x) {
            oracleScalingLut[pts[p][0] + x] =
                pts[p][1] + (std::int32_t)((x * delta + 32768) >> 16);
        }
    }
    for (int i = pts[n - 1][0]; i < 256; ++i) {
        oracleScalingLut[i] = pts[n - 1][1];
    }
}

} // namespace

// 0-mismatch formula oracle over ALL 256 entries for a 14-point luma-like
// curve, a 10-point chroma-like curve, a single-point set and a non-
// monotonic-in-y set.
TEST_CASE("l10 grain initScalingFunction: 0-mismatch oracle, four sets") {
    const std::int32_t y14[14][2] = {
        {0, 6}, {9, 13}, {19, 24}, {30, 36}, {36, 49}, {42, 62}, {49, 76},
        {58, 94}, {69, 115}, {82, 140}, {98, 169}, {118, 205}, {142, 248},
        {171, 255}};
    const std::int32_t cb10[10][2] = {
        {0, 8}, {11, 15}, {23, 23}, {36, 33}, {48, 44}, {61, 57}, {76, 74},
        {94, 95}, {117, 122}, {145, 155}};
    const std::int32_t one1[1][2] = {{100, 40}};
    const std::int32_t wiggle[3][2] = {{10, 5}, {40, 90}, {90, 20}};
    const std::int32_t* sets[4] = {&y14[0][0], &cb10[0][0], &one1[0][0],
                                   &wiggle[0][0]};
    const int ns[4] = {14, 10, 1, 3};
    for (int s = 0; s < 4; ++s) {
        std::int32_t lut[256];
        for (int i = 0; i < 256; ++i) {
            lut[i] = -777;
        }
        l10_filters::grain::initScalingFunction(
            reinterpret_cast<const std::int32_t (*)[2]>(sets[s]), ns[s], lut);
        oracleInitScaling(
            reinterpret_cast<const std::int32_t (*)[2]>(sets[s]), ns[s]);
        for (int i = 0; i < 256; ++i) {
            if (lut[i] != oracleScalingLut[i]) {
                FAIL("set ", s, " lut[", i, "] port ", lut[i], " oracle ",
                     oracleScalingLut[i]);
            }
        }
    }
    CHECK(true);
}

// num_points == 0 leaves the LUT untouched (the caller's zeroing supplies
// the flat LUT, as the reference's init_arrays does before the driver's
// scaling-function init).
TEST_CASE("l10 grain initScalingFunction: zero points leaves LUT") {
    std::int32_t lut[256];
    for (int i = 0; i < 256; ++i) {
        lut[i] = 1234;
    }
    const std::int32_t pts[1][2] = {{0, 0}};
    l10_filters::grain::initScalingFunction(pts, 0, lut);
    CHECK_EQ(lut[0], 1234);
    CHECK_EQ(lut[255], 1234);
}

// scale_lut (grainSynthesis.c:493-:504), hand-derived at bit_depth 8:
// x = index >> 0 = index; the (bit_depth - 8) branch is dead, so the
// return is lut[index] directly.
TEST_CASE("l10 grain scaleLut: bit depth 8 is the identity read") {
    std::int32_t lut[256];
    for (int i = 0; i < 256; ++i) {
        lut[i] = i * 3;
    }
    CHECK_EQ(l10_filters::grain::scaleLut(lut, 0, 8), 0);
    CHECK_EQ(l10_filters::grain::scaleLut(lut, 127, 8), 381);
    CHECK_EQ(l10_filters::grain::scaleLut(lut, 255, 8), 765);
}

// bit_depth 10 interpolation, hand-derived on lut[i] = 3*i:
// index 41: x = 10, frac = 1 -> 30 + ((3*1 + 2) >> 2) = 31;
// index 43: x = 10, frac = 3 -> 30 + ((3*3 + 2) >> 2) = 32;
// index 1019: x = 254, frac = 3 -> 762 + 2 = 764;
// index 1023: x == 255 -> the identity read, 765.
TEST_CASE("l10 grain scaleLut: bit depth 10 interpolates") {
    std::int32_t lut[256];
    for (int i = 0; i < 256; ++i) {
        lut[i] = i * 3;
    }
    CHECK_EQ(l10_filters::grain::scaleLut(lut, 41, 10), 31);
    CHECK_EQ(l10_filters::grain::scaleLut(lut, 43, 10), 32);
    CHECK_EQ(l10_filters::grain::scaleLut(lut, 1019, 10), 764);
    CHECK_EQ(l10_filters::grain::scaleLut(lut, 1023, 10), 765);
}

// init_arrays (grainSynthesis.c:182-:264): the prediction-position layout.
// num_pos_luma = 2*lag*(lag+1); num_pos_chroma = num_pos_luma, +1 when
// num_y_points > 0. Enumerated for EVERY lag in 0..3 (range rule).
TEST_CASE("l10 grain initArrays: pred-pos counts for every lag 0..3") {
    l10_filters::grain::AomFilmGrain params = l10_filters::grain::makeParams();
    for (int lag = 0; lag <= 3; ++lag) {
        params.ar_coeff_lag = lag;
        params.num_y_points = 0;
        l10_filters::grain::GrainWork w =
            l10_filters::grain::initArrays(&params, 64, 32, 64, 32, 0, 0);
        CHECK_EQ(w.predPosLuma.size(), 2 * lag * (lag + 1));
        CHECK_EQ(w.predPosChroma.size(), 2 * lag * (lag + 1));
    }
    params.ar_coeff_lag = 1;
    params.num_y_points = 2;
    l10_filters::grain::GrainWork w2 =
        l10_filters::grain::initArrays(&params, 64, 32, 64, 32, 0, 0);
    CHECK_EQ(w2.predPosLuma.size(), 4);
    CHECK_EQ(w2.predPosChroma.size(), 5);
}

// Pred-pos ORDER, enumerated: lag 1 builds rows top band (-1,-1),(-1,0),
// (-1,1) then left band (0,-1); chroma appends (0,0,1) when num_y_points>0.
TEST_CASE("l10 grain initArrays: pred-pos exact order, lag 1") {
    l10_filters::grain::AomFilmGrain params = l10_filters::grain::makeParams();
    params.ar_coeff_lag = 1;
    params.num_y_points = 2;
    l10_filters::grain::GrainWork w =
        l10_filters::grain::initArrays(&params, 64, 32, 64, 32, 1, 1);

    CHECK_EQ(w.predPosLuma[0], (std::array<std::int32_t, 3>{-1, -1, 0}));
    CHECK_EQ(w.predPosLuma[1], (std::array<std::int32_t, 3>{-1, 0, 0}));
    CHECK_EQ(w.predPosLuma[2], (std::array<std::int32_t, 3>{-1, 1, 0}));
    CHECK_EQ(w.predPosLuma[3], (std::array<std::int32_t, 3>{0, -1, 0}));
    CHECK_EQ(w.predPosChroma[4], (std::array<std::int32_t, 3>{0, 0, 1}));
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK_EQ(w.predPosChroma[i], w.predPosLuma[i]);
    }

    // Buffer sizing: subsampling halves the chroma line/col extras.
    // yLine = stride*2; cbLine = stride*(2>>subY); yCol = (32+2)*2;
    // cbCol = (16 + (2>>1)) * (2>>1) = 17.
    CHECK_EQ(w.yLineBuf.size(), 128u);
    CHECK_EQ(w.cbLineBuf.size(), 32u);
    CHECK_EQ(w.yColBuf.size(), 68u);
    CHECK_EQ(w.cbColBuf.size(), 17u);
    CHECK_EQ(w.lumaGrainBlock.size(), 64u);
    CHECK_EQ(w.cbGrainBlock.size(), 32u);
}

// generate_luma_grain_block (grainSynthesis.c:328-:363), lag 0: the AR
// pass still runs with zero positions (wsum = 0), adding
// ((0 + rounding) >> shift) = 0 at legal shift 7, then clamping to the
// grain range set by initGrainRange (factored from the driver body
// :916-:918). Oracle re-derives the LCG from the reference formula.
TEST_CASE("l10 grain generateLumaGrainBlock: lag 0 white-noise oracle") {
    l10_filters::grain::initGrainRange(8);
    l10_filters::grain::AomFilmGrain params = l10_filters::grain::makeParams();
    params.bit_depth = 8;
    params.grain_scale_shift = 0;
    params.ar_coeff_lag = 0;
    params.ar_coeff_shift = 7;
    params.num_y_points = 2;
    params.random_seed = 0x1234;

    const int W = 4, H = 3, STRIDE = 6; // ragged stride on purpose
    std::vector<std::int32_t> block(W * H, -999);

    l10_filters::grain::initRandomGenerator(0, params.random_seed);
    l10_filters::grain::generateLumaGrainBlock(&params, nullptr,
        block.data(), H, W, STRIDE, 0, 0, 0, 0);

    // Independent oracle: 16-bit LCG per reference :306-:311.
    std::uint16_t reg = params.random_seed;
    reg ^= (std::uint16_t)(((0 * 37 + 178) & 255) << 8);
    reg ^= (std::uint16_t)((0 * 173 + 105) & 255);
    auto draw = [&reg](int bits) {
        std::uint16_t bit = (reg ^ (reg >> 1) ^ (reg >> 3) ^ (reg >> 12)) & 1;
        reg = (reg >> 1) | (bit << 15);
        return (reg >> (16 - bits)) & ((1 << bits) - 1);
    };

    bool ok = true;
    for (int i = 0; i < H; ++i) {
        for (int j = 0; j < W; ++j) {
            std::int32_t cell = (l10_filters::grain::gaussianSequence[draw(11)] + 8) >> 4;
            // lag-0 AR pass: wsum=0, rounding=64, shift 7 -> adds 0, clamp
            // to [-128, 127] is a no-op for legal fill values.
            if (cell < -128) cell = -128;
            if (cell > 127) cell = 127;
            if (block[i * STRIDE + j] != cell) {
                ok = false;
                FAIL("cell (", i, ",", j, ") port ", block[i * STRIDE + j],
                     " oracle ", cell);
            }
        }
    }
    CHECK(ok);
    // stride gap cells untouched
    CHECK_EQ(block[0 * STRIDE + 4], -999);
    CHECK_EQ(block[1 * STRIDE + 5], -999);
}

// lag 1 with real coefficients: AR pass over the interior of a padded
// block (6x5 array, 1-cell border, interior 4x3), in-place, coeffs
// {60,60,60,60}, shift 6, seed 0x1234. Positive feedback saturates the
// lower interior at 127 - the clamp is live. Border cells keep their raw
// fill values (the pass respects the pads). Oracle re-derives the whole
// recursion independently from the reference semantics.
TEST_CASE("l10 grain generateLumaGrainBlock: lag 1 AR pass + live clamp") {
    l10_filters::grain::initGrainRange(8);
    l10_filters::grain::AomFilmGrain params = l10_filters::grain::makeParams();
    params.bit_depth = 8;
    params.grain_scale_shift = 0;
    params.ar_coeff_lag = 1;
    params.ar_coeff_shift = 6;
    params.num_y_points = 2;
    params.random_seed = 0x1234;
    for (int i = 0; i < 4; ++i) {
        params.ar_coeffs_y[i] = 60;
    }

    const int W = 6, H = 5, STRIDE = 6;
    std::vector<std::int32_t> block(W * H, -999);
    l10_filters::grain::initRandomGenerator(0, params.random_seed);
    const l10_filters::grain::PredPosRow pos[4] = {
        {-1, -1, 0}, {-1, 0, 0}, {-1, 1, 0}, {0, -1, 0}};
    l10_filters::grain::generateLumaGrainBlock(&params, pos,
        block.data(), H, W, STRIDE, 1, 1, 1, 1);

    // hand anchors (derived externally from the reference recursion)
    CHECK_EQ(block[1 * STRIDE + 1], 90); // first interior cell
    CHECK_EQ(block[1 * STRIDE + 2], 125);
    CHECK_EQ(block[2 * STRIDE + 2], 127); // saturated
    CHECK_EQ(block[3 * STRIDE + 4], 127); // saturated
    CHECK_EQ(block[0 * STRIDE + 0], 13);  // border: raw fill, untouched
    CHECK_EQ(block[4 * STRIDE + 5], -5);  // border: raw fill, untouched

    // independent oracle, same order, in-place
    std::uint16_t reg = params.random_seed;
    reg ^= (std::uint16_t)(((0 * 37 + 178) & 255) << 8);
    reg ^= (std::uint16_t)((0 * 173 + 105) & 255);
    auto draw = [&reg](int bits) {
        std::uint16_t bit = (reg ^ (reg >> 1) ^ (reg >> 3) ^ (reg >> 12)) & 1;
        reg = (reg >> 1) | (bit << 15);
        return (reg >> (16 - bits)) & ((1 << bits) - 1);
    };
    std::int32_t oracle[30] = {0};
    for (int i = 0; i < H; ++i) {
        for (int j = 0; j < W; ++j) {
            oracle[i * STRIDE + j] =
                (l10_filters::grain::gaussianSequence[draw(11)] + 8) >> 4;
        }
    }
    const int P[4][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}};
    for (int i = 1; i < H - 1; ++i) {
        for (int j = 1; j < W - 1; ++j) {
            std::int32_t wsum = 0;
            for (int p = 0; p < 4; ++p) {
                wsum += 60 * oracle[(i + P[p][0]) * STRIDE + j + P[p][1]];
            }
            std::int32_t v = oracle[i * STRIDE + j] + ((wsum + 32) >> 6);
            if (v < -128) v = -128;
            if (v > 127) v = 127;
            oracle[i * STRIDE + j] = v;
        }
    }
    bool ok = true;
    for (int i = 0; i < H; ++i) {
        for (int j = 0; j < W; ++j) {
            if (block[i * STRIDE + j] != oracle[i * STRIDE + j]) {
                ok = false;
                FAIL("cell (", i, ",", j, ") port ", block[i * STRIDE + j],
                     " oracle ", oracle[i * STRIDE + j]);
            }
        }
    }
    CHECK(ok);
}

// 4:4:4 (chroma_subsamp 0,0): average_luma is a direct luma read, and the
// chroma loops cover half x half (not half x half of a subsampled grid).
// Flat LUTs make every contribution exact: cb contribution
// (32*10 + 128) >> 8 = 448>>8 = 1; cr with -20: (32*-20 + 128) >> 8 =
// -512>>8 = -2.
TEST_CASE("l10 grain addNoiseToBlock: 444 direct luma read") {
    l10_filters::grain::initGrainRange(8);
    l10_filters::grain::AomFilmGrain params = l10_filters::grain::makeParams();
    params.bit_depth = 8;
    params.scaling_shift = 8;
    params.num_y_points = 0;
    params.num_cb_points = 1;
    params.num_cr_points = 1;
    params.chroma_scaling_from_luma = 0;
    params.clip_to_restricted_range = 0;
    params.cb_mult = 128;      // mult 0
    params.cb_luma_mult = 128; // mult 0
    params.cb_offset = 256 + 128; // idx constant 128
    params.cr_mult = 128;
    params.cr_luma_mult = 128;
    params.cr_offset = 256 + 128;

    const std::int32_t cbPts[1][2] = {{128, 32}};
    const std::int32_t crPts[1][2] = {{128, 32}};
    l10_filters::grain::initScalingLuts(&params, cbPts, cbPts, crPts);

    std::uint8_t luma[4 * 4] = {77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
                                77, 77, 77, 77, 77};
    std::uint8_t cb[4 * 4] = {};
    std::uint8_t cr[4 * 4] = {};
    for (int i = 0; i < 16; ++i) {
        cb[i] = 100;
        cr[i] = 100;
    }
    std::int32_t lumaGrain[16] = {}; // no y points: untouched
    std::int32_t cbGrain[16], crGrain[16];
    for (int i = 0; i < 16; ++i) {
        cbGrain[i] = 10;
        crGrain[i] = -20;
    }

    l10_filters::grain::addNoiseToBlock(&params, luma, cb, cr, 4, 4,
        lumaGrain, cbGrain, crGrain, 4, 4, 2, 2, 8, 0, 0);

    // idx = 128 for both planes; cb: 448>>8 = 1 -> 101;
    // cr: -512>>8 = -2 -> 98.
    CHECK_EQ(cb[0], 101);
    CHECK_EQ(cb[15], 101);
    CHECK_EQ(cr[0], 98);
    CHECK_EQ(cr[15], 98);
    // luma untouched (no y points)
    CHECK_EQ(luma[0], 77);
    CHECK_EQ(luma[15], 77);
}

// ver_boundary_overlap (grainSynthesis.c:816-:838), hand math:
// width 1: dst = (23*left + 22*right + 16) >> 5;
// width 2: dst0 = (27*left0 + 17*right0 + 16) >> 5, dst1 = (17*left1 +
// 27*right1 + 16) >> 5, all clamped to the grain range. Width 3+: the
// reference returns without writing anything.
TEST_CASE("l10 grain verBoundaryOverlap: width 1, 2, and no-op") {
    l10_filters::grain::initGrainRange(8);

    // width 1
    std::int32_t left1[2] = {100, -100}, right1[2] = {50, -50};
    std::int32_t dst1[2] = {-999, -999};
    l10_filters::grain::verBoundaryOverlap(left1, 1, right1, 1, dst1, 1, 1, 2);
    CHECK_EQ(dst1[0], (100 * 23 + 50 * 22 + 16) >> 5);
    CHECK_EQ(dst1[1], (-100 * 23 + -50 * 22 + 16) >> 5);

    // width 2 with a clamp: left {1000, -300}, right {0, 100}
    std::int32_t left2[4] = {1000, -300, 7, 7}, right2[4] = {0, 100, 9, 9};
    std::int32_t dst2[4] = {-999, -999, -999, -999};
    l10_filters::grain::verBoundaryOverlap(left2, 2, right2, 2, dst2, 2, 2, 2);
    CHECK_EQ(dst2[0], 127); // (23000+16)>>5 = 719 -> clamp 127
    CHECK_EQ(dst2[1], (-2384 >> 5)); // -75
    CHECK_EQ(dst2[2], 11); // row 1 col 0: (27*7 + 17*9 + 16)>>5 = 348>>5
    CHECK_EQ(dst2[3], 11); // row 1 col 1: (17*7 + 27*9 + 16)>>5 = 348>>5

    // width 3: no-op
    std::int32_t left3[6] = {1, 1, 1, 1, 1, 1}, right3[6] = {2, 2, 2, 2, 2, 2};
    std::int32_t dst3[6] = {};
    for (int i = 0; i < 6; ++i) {
        dst3[i] = 42;
    }
    l10_filters::grain::verBoundaryOverlap(left3, 3, right3, 3, dst3, 3, 3, 2);
    CHECK_EQ(dst3[0], 42);
    CHECK_EQ(dst3[5], 42);
}

// hor_boundary_overlap (grainSynthesis.c:840-:863): the transposed twin.
// height 1: dst = (23*top + 22*bottom + 16) >> 5;
// height 2: dst[0] = (27*top + 17*bottom + 16) >> 5 and
// dst[dst_stride] = (17*top[top_stride] + 27*bottom[bottom_stride] + 16) >> 5.
TEST_CASE("l10 grain horBoundaryOverlap: height 1 and 2") {
    l10_filters::grain::initGrainRange(8);

    std::int32_t top1[2] = {64, -64}, bot1[2] = {32, -32};
    std::int32_t dst1[2] = {-999, -999};
    l10_filters::grain::horBoundaryOverlap(top1, 1, bot1, 1, dst1, 1, 2, 1);
    CHECK_EQ(dst1[0], (64 * 23 + 32 * 22 + 16) >> 5);
    CHECK_EQ(dst1[1], (-64 * 23 + -32 * 22 + 16) >> 5);

    // height 2, strides 3: top row {200, 0}, bottom row {-200, 40}
    std::int32_t top2[6] = {200, 7, 7, 0, 7, 7};
    std::int32_t bot2[6] = {-200, 7, 7, 40, 7, 7};
    std::int32_t dst2[6] = {-999};
    for (int i = 0; i < 6; ++i) {
        dst2[i] = -999;
    }
    l10_filters::grain::horBoundaryOverlap(top2, 3, bot2, 3, dst2, 3, 2, 2);
    CHECK_EQ(dst2[0], (27 * 200 + 17 * -200 + 16) >> 5); // 1840>>5 = 57
    CHECK_EQ(dst2[3], (17 * 0 + 27 * 40 + 16) >> 5); // 1096>>5 = 34
    CHECK_EQ(dst2[1], (17 * 7 + 27 * 7 + 16) >> 5); // 326>>5 = 10
    CHECK_EQ(dst2[4], (17 * 7 + 27 * 7 + 16) >> 5); // 10
    CHECK_EQ(dst2[2], -999); // stride gap
    CHECK_EQ(dst2[5], -999);
}

// film_grain_params_equal (grainSynthesis.c:709-:791): every gate it
// checks, enumerated. The gates: apply_grain, overlap_flag,
// clip_to_restricted_range, chroma_scaling_from_luma, grain_scale_shift,
// ar_coeff_shift, cb_mult, cb_luma_mult, cb_offset, cr_mult, cr_luma_mult,
// cr_offset, scaling_shift, ar_coeff_lag, num_y_points, num_cb_points,
// num_cr_points, and memcmp of scaling_points_y/cb/cr and
// ar_coeffs_y/cb/cr. NOT compared (reference behavior, pinned):
// update_parameters, bit_depth, random_seed, ignore_ref.
TEST_CASE("l10 grain filmGrainParamsEqual: every gate enumerated") {
    l10_filters::grain::AomFilmGrain a = l10_filters::grain::makeParams();
    l10_filters::grain::AomFilmGrain b = l10_filters::grain::makeParams();
    a.num_y_points = 2;
    a.num_cb_points = 1;
    a.num_cr_points = 1;
    a.scaling_points_y[0][0] = 10;
    a.scaling_points_y[0][1] = 20;
    a.scaling_points_cb[0][0] = 5;
    a.scaling_points_cb[0][1] = 7;
    a.scaling_points_cr[0][0] = 3;
    a.scaling_points_cr[0][1] = 9;
    a.ar_coeffs_y[3] = 11;
    a.ar_coeffs_cb[7] = 13;
    a.ar_coeffs_cr[9] = 17;
    b = a;
    CHECK_EQ(l10_filters::grain::filmGrainParamsEqual(&a, &b), 1);

    // each compared field, one at a time
    struct Mut {
        int which;
        const char* name;
    };
    bool allKilled = true;
    for (int m = 0; m < 17; ++m) {
        l10_filters::grain::AomFilmGrain c = a;
        l10_filters::grain::AomFilmGrain d = a;
        switch (m) {
        case 0: c.apply_grain = 1; break;
        case 1: c.overlap_flag = 1; break;
        case 2: c.clip_to_restricted_range = 1; break;
        case 3: c.chroma_scaling_from_luma = 1; break;
        case 4: c.grain_scale_shift = 2; break;
        case 5: c.ar_coeff_shift = 7; break;
        case 6: c.cb_mult = 1; break;
        case 7: c.cb_luma_mult = 1; break;
        case 8: c.cb_offset = 1; break;
        case 9: c.cr_mult = 1; break;
        case 10: c.cr_luma_mult = 1; break;
        case 11: c.cr_offset = 1; break;
        case 12: c.scaling_shift = 9; break;
        case 13: c.ar_coeff_lag = 2; break;
        case 14: c.num_y_points = 3; break;
        case 15: c.num_cb_points = 2; break;
        case 16: c.num_cr_points = 2; break;
        }
        if (l10_filters::grain::filmGrainParamsEqual(&c, &d) != 0) {
            allKilled = false;
            FAIL("gate ", m, " not detected");
        }
    }
    CHECK(allKilled);

    // array bytes: one differing entry per memcmp block
    l10_filters::grain::AomFilmGrain c = a;
    c.scaling_points_y[13][1] = 99;
    CHECK_EQ(l10_filters::grain::filmGrainParamsEqual(&a, &c), 0);
    c = a;
    c.scaling_points_cb[9][0] = 99;
    CHECK_EQ(l10_filters::grain::filmGrainParamsEqual(&a, &c), 0);
    c = a;
    c.scaling_points_cr[9][1] = 99;
    CHECK_EQ(l10_filters::grain::filmGrainParamsEqual(&a, &c), 0);
    c = a;
    c.ar_coeffs_y[23] = 99;
    CHECK_EQ(l10_filters::grain::filmGrainParamsEqual(&a, &c), 0);
    c = a;
    c.ar_coeffs_cb[24] = 99;
    CHECK_EQ(l10_filters::grain::filmGrainParamsEqual(&a, &c), 0);
    c = a;
    c.ar_coeffs_cr[24] = 99;
    CHECK_EQ(l10_filters::grain::filmGrainParamsEqual(&a, &c), 0);

    // NOT compared: differing only in these still compares equal
    l10_filters::grain::AomFilmGrain e = a;
    e.bit_depth = 10;
    e.random_seed = 4242;
    e.update_parameters = 1;
    e.ignore_ref = 1;
    CHECK_EQ(l10_filters::grain::filmGrainParamsEqual(&a, &e), 1);
}

// svt_aom_fgn_copy_rect (grainSynthesis.c:793-:803): row-wise copy with
// distinct strides; the highbd path doubles the byte count per row.
TEST_CASE("l10 grain fgnCopyRect: strides and hbd coefficient") {
    std::uint8_t src[16] = {0};
    for (int i = 0; i < 16; ++i) {
        src[i] = (std::uint8_t)i;
    }
    std::uint8_t dst[16] = {};
    // 2 rows x 4 wide, src stride 8, dst stride 4
    l10_filters::grain::fgnCopyRect(src, 8, dst, 4, 4, 2, 0);
    CHECK_EQ(dst[0], 0);
    CHECK_EQ(dst[3], 3);
    CHECK_EQ(dst[4], 8);  // row 2 from src+8
    CHECK_EQ(dst[7], 11);
    CHECK_EQ(dst[1], 1);

    // hbd: 2 bytes per pixel, reading src as a uint16 buffer
    std::uint8_t src2[8] = {0xAB, 0, 0xCD, 0, 0x11, 0, 0x22, 0};
    std::uint8_t dst2[8] = {};
    l10_filters::grain::fgnCopyRect(src2, 2, dst2, 2, 2, 2, 1);
    CHECK_EQ(dst2[0], 0xAB);
    CHECK_EQ(dst2[2], 0xCD);
    CHECK_EQ(dst2[4], 0x11);
    CHECK_EQ(dst2[6], 0x22);
}

// copy_area (grainSynthesis.c:805-:814): int32 row copy, same strides.
TEST_CASE("l10 grain copyArea: int32 rows") {
    std::int32_t src[12] = {0};
    for (int i = 0; i < 12; ++i) {
        src[i] = i * 3;
    }
    std::int32_t dst[12] = {};
    l10_filters::grain::copyArea(src, 6, dst, 3, 2, 2);
    CHECK_EQ(dst[0], 0);
    CHECK_EQ(dst[1], 3);
    CHECK_EQ(dst[3], 18);  // dst row 1 from src row 1 (offset 6): 6*3 = 18
    CHECK_EQ(dst[4], 21);
    CHECK_EQ(dst[2], 0);   // outside copied width
    CHECK_EQ(dst[5], 0);
}

namespace {

// A realistic 64x64 4:2:0 fixture for the driver: 5 luma points, 3 cb, 3
// cr, lag 2, real shifts, seed 0xABCD.
l10_filters::grain::AomFilmGrain driverParams(bool overlap,
                                              std::int32_t restricted) {
    l10_filters::grain::AomFilmGrain p = l10_filters::grain::makeParams();
    p.apply_grain = 1;
    p.bit_depth = 8;
    p.grain_scale_shift = 1;
    p.ar_coeff_lag = 2;
    p.ar_coeff_shift = 7;
    p.scaling_shift = 9;
    p.num_y_points = 5;
    p.num_cb_points = 3;
    p.num_cr_points = 3;
    p.overlap_flag = overlap;
    p.clip_to_restricted_range = restricted;
    p.chroma_scaling_from_luma = 0;
    p.random_seed = 0xABCD;
    const std::int32_t yPts[5][2] = {{0, 4}, {64, 72}, {128, 130},
                                     {192, 190}, {255, 252}};
    const std::int32_t cbPts[3][2] = {{0, 20}, {128, 130}, {255, 240}};
    const std::int32_t crPts[3][2] = {{0, 15}, {128, 125}, {255, 250}};
    for (int i = 0; i < 5; ++i) {
        p.scaling_points_y[i][0] = yPts[i][0];
        p.scaling_points_y[i][1] = yPts[i][1];
    }
    for (int i = 0; i < 3; ++i) {
        p.scaling_points_cb[i][0] = cbPts[i][0];
        p.scaling_points_cb[i][1] = cbPts[i][1];
        p.scaling_points_cr[i][0] = crPts[i][0];
        p.scaling_points_cr[i][1] = crPts[i][1];
    }
    // small nonzero AR coeffs
    for (int i = 0; i < 12; ++i) {
        p.ar_coeffs_y[i] = (i % 3) - 1; // {-1,0,1} pattern
        p.ar_coeffs_cb[i] = (i % 4) - 1;
        p.ar_coeffs_cr[i] = (i % 5) - 1;
    }
    p.ar_coeffs_cb[12] = 12;
    p.ar_coeffs_cr[12] = 10;
    return p;
}

} // namespace

// Driver integration: 64x64 4:2:0, overlap on. Pins:
// (a) determinism - two identical runs produce identical planes;
// (b) the first subblock (y=0, x=0) is applied identically with overlap
//     on and off, so luma rows 0..31 / cols 0..31 match the no-overlap run;
// (c) the planes actually changed and all outputs stay in [0, 255].
TEST_CASE("l10 grain addFilmGrainRun: 64x64 determinism + first block") {
    l10_filters::grain::AomFilmGrain p = driverParams(true, 0);

    const int W = 64, H = 64, LS = 64, CS = 32;
    std::vector<std::uint8_t> lumaA(H * LS), cbA((H / 2) * CS),
        crA((H / 2) * CS), lumaB(H * LS), cbB((H / 2) * CS),
        crB((H / 2) * CS);
    for (int i = 0; i < H * LS; ++i) {
        lumaA[i] = (std::uint8_t)(60 + (i * 13) % 90);
        lumaB[i] = lumaA[i];
    }
    for (int i = 0; i < (H / 2) * CS; ++i) {
        cbA[i] = (std::uint8_t)(120 + (i * 7) % 40);
        crA[i] = (std::uint8_t)(100 + (i * 11) % 60);
        cbB[i] = cbA[i];
        crB[i] = crA[i];
    }

    l10_filters::grain::addFilmGrainRun(&p, lumaA.data(), cbA.data(),
        crA.data(), H, W, LS, CS, 0, 1, 1);
    l10_filters::grain::addFilmGrainRun(&p, lumaB.data(), cbB.data(),
        crB.data(), H, W, LS, CS, 0, 1, 1);

    // (a) determinism
    bool deterministic = true;
    for (int i = 0; i < H * LS; ++i) {
        if (lumaA[i] != lumaB[i]) deterministic = false;
    }
    for (int i = 0; i < (H / 2) * CS; ++i) {
        if (cbA[i] != cbB[i] || crA[i] != crB[i]) deterministic = false;
    }
    CHECK(deterministic);

    // (b) first subblock equivalence vs the no-overlap run
    l10_filters::grain::AomFilmGrain pNo = driverParams(false, 0);
    std::vector<std::uint8_t> lumaC(H * LS), cbC((H / 2) * CS),
        crC((H / 2) * CS);
    for (int i = 0; i < H * LS; ++i) {
        lumaC[i] = (std::uint8_t)(60 + (i * 13) % 90);
    }
    for (int i = 0; i < (H / 2) * CS; ++i) {
        cbC[i] = (std::uint8_t)(120 + (i * 7) % 40);
        crC[i] = (std::uint8_t)(100 + (i * 11) % 60);
    }
    l10_filters::grain::addFilmGrainRun(&pNo, lumaC.data(), cbC.data(),
        crC.data(), H, W, LS, CS, 0, 1, 1);
    bool firstBlockSame = true;
    for (int r = 0; r < 32; ++r) {
        for (int c = 0; c < 32; ++c) {
            if (lumaA[r * LS + c] != lumaC[r * LS + c]) {
                firstBlockSame = false;
                FAIL("first block diff at (", r, ",", c, ")");
            }
        }
    }
    CHECK(firstBlockSame);

    // (c) changed and in range
    bool changed = false;
    for (int i = 0; i < H * LS; ++i) {
        if (lumaA[i] != (std::uint8_t)(60 + (i * 13) % 90)) changed = true;
        CHECK_GE((int)lumaA[i], 0);
        CHECK_LE((int)lumaA[i], 255);
    }
    CHECK(changed);
    // (c2) the chroma planes changed too: compare against the INITIAL
    // pattern (the run's own inputs), not against the deterministic copy.
    bool chromaChanged = false;
    for (int i = 0; i < (H / 2) * CS; ++i) {
        if (cbA[i] != (std::uint8_t)(120 + (i * 7) % 40) ||
            crA[i] != (std::uint8_t)(100 + (i * 11) % 60)) {
            chromaChanged = true;
        }
    }
    CHECK(chromaChanged);
}

// Driver with clip_to_restricted_range = 1: all outputs in the studio
// range (16..235 luma, 16..240 chroma).
TEST_CASE("l10 grain addFilmGrainRun: restricted range bounds") {
    l10_filters::grain::AomFilmGrain p = driverParams(true, 1);
    const int W = 64, H = 64, LS = 64, CS = 32;
    std::vector<std::uint8_t> luma(H * LS), cb((H / 2) * CS), cr((H / 2) * CS);
    for (int i = 0; i < H * LS; ++i) {
        luma[i] = (std::uint8_t)(10 + (i * 13) % 240);
    }
    for (int i = 0; i < (H / 2) * CS; ++i) {
        cb[i] = (std::uint8_t)(10 + (i * 7) % 230);
        cr[i] = (std::uint8_t)(10 + (i * 11) % 230);
    }
    l10_filters::grain::addFilmGrainRun(&p, luma.data(), cb.data(),
        cr.data(), H, W, LS, CS, 0, 1, 1);
    bool inRange = true;
    for (int i = 0; i < H * LS; ++i) {
        if (luma[i] < 16 || luma[i] > 235) inRange = false;
    }
    for (int i = 0; i < (H / 2) * CS; ++i) {
        if (cb[i] < 16 || cb[i] > 240 || cr[i] < 16 || cr[i] > 240) {
            inRange = false;
        }
    }
    CHECK(inRange);
}

// generate_chroma_grain_blocks (grainSynthesis.c:365-:464), case 1: no cb
// or cr points and no chroma_scaling_from_luma -> both blocks memset to 0
// and the RNG is never touched.
TEST_CASE("l10 grain generateChromaGrainBlocks: zero points memsets zero") {
    l10_filters::grain::initGrainRange(8);
    l10_filters::grain::AomFilmGrain params = l10_filters::grain::makeParams();
    params.bit_depth = 8;
    params.ar_coeff_lag = 1;
    params.ar_coeff_shift = 7;
    params.num_y_points = 0;
    params.num_cb_points = 0;
    params.num_cr_points = 0;
    params.chroma_scaling_from_luma = 0;

    std::vector<std::int32_t> luma(36, 7), cb(18, -555), cr(18, -555);
    const l10_filters::grain::PredPosRow pos[5] = {
        {-1, -1, 0}, {-1, 0, 0}, {-1, 1, 0}, {0, -1, 0}, {0, 0, 1}};
    l10_filters::grain::generateChromaGrainBlocks(&params, pos,
        luma.data(), cb.data(), cr.data(), 6, 3, 2, 6, 0, 0, 0, 0, 1, 1);
    bool allZero = true;
    for (int i = 0; i < 18; ++i) {
        if (cb[i] != 0 || cr[i] != 0) {
            allZero = false;
        }
    }
    CHECK(allZero);
}

// Full chroma case: y points present, cb/cr points present, 4:2:0
// subsampling, the (0,0,1) position averaging the luma grain block.
// Luma comes from the verified S6 kernel; the cb/cr recursion is
// re-derived independently. CB uses LCG row 7<<5, CR row 11<<5.
TEST_CASE("l10 grain generateChromaGrainBlocks: full oracle, 420") {
    l10_filters::grain::initGrainRange(8);
    l10_filters::grain::AomFilmGrain params = l10_filters::grain::makeParams();
    params.bit_depth = 8;
    params.grain_scale_shift = 0;
    params.ar_coeff_lag = 1;
    params.ar_coeff_shift = 7;
    params.num_y_points = 2;
    params.num_cb_points = 3;
    params.num_cr_points = 3;
    params.chroma_scaling_from_luma = 0;
    params.random_seed = 0x1234;
    const std::int32_t cbC[4] = {30, 20, 10, 5};
    const std::int32_t crC[4] = {25, 15, 8, 3};
    for (int i = 0; i < 4; ++i) {
        params.ar_coeffs_cb[i] = cbC[i];
        params.ar_coeffs_cr[i] = crC[i];
    }
    params.ar_coeffs_cb[4] = 40;
    params.ar_coeffs_cr[4] = 35;

    // luma grain block: 7 rows x 6 cols, stride 6
    std::vector<std::int32_t> luma(42);
    const l10_filters::grain::PredPosRow lpos[4] = {
        {-1, -1, 0}, {-1, 0, 0}, {-1, 1, 0}, {0, -1, 0}};
    l10_filters::grain::initRandomGenerator(0, params.random_seed);
    l10_filters::grain::generateLumaGrainBlock(&params, lpos, luma.data(),
        7, 6, 6, 1, 1, 1, 1);

    // chroma blocks: 5 rows x 4 cols, stride 4, pads 1
    std::vector<std::int32_t> cb(20, -999), cr(20, -999);
    const l10_filters::grain::PredPosRow cpos[5] = {
        {-1, -1, 0}, {-1, 0, 0}, {-1, 1, 0}, {0, -1, 0}, {0, 0, 1}};
    l10_filters::grain::generateChromaGrainBlocks(&params, cpos, luma.data(),
        cb.data(), cr.data(), 6, 5, 4, 4, 1, 1, 1, 1, 1, 1);

    // independent oracle
    auto mk = [&](std::uint16_t row) {
        std::uint16_t r = params.random_seed;
        std::int32_t lumaNum = row >> 5; // the reference uses luma_line >> 5
        r ^= (std::uint16_t)(((lumaNum * 37 + 178) & 255) << 8);
        r ^= (std::uint16_t)((lumaNum * 173 + 105) & 255);
        return r;
    };
    std::uint16_t reg = 0;
    auto draw = [&reg](int bits) {
        std::uint16_t bit = (reg ^ (reg >> 1) ^ (reg >> 3) ^ (reg >> 12)) & 1;
        reg = (reg >> 1) | (bit << 15);
        return (reg >> (16 - bits)) & ((1 << bits) - 1);
    };
    std::int32_t ocb[20] = {0}, ocr[20] = {0};
    reg = mk(7 << 5);
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 4; ++j) {
            ocb[i * 4 + j] = (l10_filters::grain::gaussianSequence[draw(11)] + 8) >> 4;
        }
    }
    reg = mk(11 << 5);
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 4; ++j) {
            ocr[i * 4 + j] = (l10_filters::grain::gaussianSequence[draw(11)] + 8) >> 4;
        }
    }
    const int P[4][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}};
    for (int i = 1; i < 4; ++i) {
        for (int j = 1; j < 3; ++j) {
            std::int32_t wcb = 0, wcr = 0;
            for (int p = 0; p < 4; ++p) {
                wcb += cbC[p] * ocb[(i + P[p][0]) * 4 + j + P[p][1]];
                wcr += crC[p] * ocr[(i + P[p][0]) * 4 + j + P[p][1]];
            }
            std::int32_t av = 0;
            int ly = ((i - 1) << 1) + 1;
            int lx = ((j - 1) << 1) + 1;
            for (int k = ly; k < ly + 2; ++k) {
                for (int l = lx; l < lx + 2; ++l) {
                    av += luma[k * 6 + l];
                }
            }
            av = (av + 2) >> 2; // 2x2 window: (1<<(1+1))>>1 rounding
            wcb += 40 * av;
            wcr += 35 * av;
            std::int32_t v = ocb[i * 4 + j] + ((wcb + 64) >> 7);
            if (v < -128) v = -128;
            if (v > 127) v = 127;
            ocb[i * 4 + j] = v;
            v = ocr[i * 4 + j] + ((wcr + 64) >> 7);
            if (v < -128) v = -128;
            if (v > 127) v = 127;
            ocr[i * 4 + j] = v;
        }
    }
    bool ok = true;
    for (int i = 0; i < 20; ++i) {
        if (cb[i] != ocb[i] || cr[i] != ocr[i]) {
            ok = false;
            FAIL("idx ", i, " cb ", cb[i], "/", ocb[i], " cr ", cr[i], "/",
                 ocr[i]);
        }
    }
    CHECK(ok);
}

// chroma_scaling_from_luma with zero cb/cr points: the blocks are still
// generated (seeded noise + AR updates), not zeroed.
TEST_CASE("l10 grain generateChromaGrainBlocks: from_luma generates") {
    l10_filters::grain::initGrainRange(8);
    l10_filters::grain::AomFilmGrain params = l10_filters::grain::makeParams();
    params.bit_depth = 8;
    params.ar_coeff_lag = 0;
    params.ar_coeff_shift = 7;
    params.num_y_points = 0;
    params.num_cb_points = 0;
    params.num_cr_points = 0;
    params.chroma_scaling_from_luma = 1;
    params.random_seed = 0x1234;

    std::vector<std::int32_t> luma(16, 100), cb(16, -555), cr(16, -555);
    const l10_filters::grain::PredPosRow pos[1] = {{0, 0, 1}};
    l10_filters::grain::generateChromaGrainBlocks(&params, pos, luma.data(),
        cb.data(), cr.data(), 4, 4, 4, 4, 0, 0, 0, 0, 0, 0);
    // lag 0: AR pass adds 0 and clamps the seeded fills into range; the
    // blocks must be neither -555 nor all zero.
    bool generated = true;
    for (int i = 0; i < 16; ++i) {
        if (cb[i] == -555 || cr[i] == -555) {
            generated = false;
        }
    }
    CHECK(generated);
    CHECK_NE(cb[0], 0);
    CHECK_NE(cr[0], 0);
}

namespace {

// Independent scale_lut oracle (reference :493-:504 semantics).
std::int32_t oracleScaleLut(const std::int32_t* lut, std::int32_t index,
                            std::int32_t bd) {
    std::int32_t x = index >> (bd - 8);
    if (!(bd - 8) || x == 255) {
        return lut[x];
    }
    return lut[x] + (((lut[x + 1] - lut[x]) * (index & ((1 << (bd - 8)) - 1)) +
                      (1 << (bd - 9))) >> (bd - 8));
}

} // namespace

// Full-oracle noise case: 4:2:0, rectangular window (half 3x2 -> luma
// 6x4, chroma 3x2), mixed-sign grain, clip_to_restricted_range = 1 (the
// 16..235 / 16..240 clamps live). LUTs from 3-point curves via the
// oracle; every plane cell checked.
TEST_CASE("l10 grain addNoiseToBlock: full oracle, restricted range") {
    l10_filters::grain::initGrainRange(8);
    l10_filters::grain::AomFilmGrain params = l10_filters::grain::makeParams();
    params.bit_depth = 8;
    params.scaling_shift = 8;
    params.num_y_points = 3;
    params.num_cb_points = 2;
    params.num_cr_points = 2;
    params.chroma_scaling_from_luma = 0;
    params.clip_to_restricted_range = 1;
    params.cb_mult = 155;
    params.cb_luma_mult = 170;
    params.cb_offset = 256 + 60;
    params.cr_mult = 150;
    params.cr_luma_mult = 165;
    params.cr_offset = 256 + 40;

    const std::int32_t yPts[3][2] = {{0, 0}, {128, 128}, {255, 255}};
    const std::int32_t cbPts[2][2] = {{0, 10}, {255, 240}};
    const std::int32_t crPts[2][2] = {{0, 5}, {255, 250}};
    l10_filters::grain::initScalingLuts(&params, yPts, cbPts, crPts);

    std::uint8_t luma[8 * 6] = {};
    std::uint8_t cb[4 * 3] = {};
    std::uint8_t cr[4 * 3] = {};
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 4; ++j) {
            luma[i * 8 + j] = (std::uint8_t)(i * 37 + j * 53 + 8);
        }
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 2; ++j) {
            cb[i * 4 + j] = (std::uint8_t)(90 + i * 30 + j * 10);
            cr[i * 4 + j] = (std::uint8_t)(70 + i * 45 + j * 15);
        }
    }
    std::int32_t lumaGrain[6 * 4], cbGrain[3 * 2], crGrain[3 * 2];
    for (int i = 0; i < 24; ++i) {
        lumaGrain[i] = (i * 7 + i * 11) - 30;
    }
    for (int i = 0; i < 6; ++i) {
        cbGrain[i] = 25 - i * 4;
        crGrain[i] = -(i * 9 + 10);
    }

    l10_filters::grain::addNoiseToBlock(&params, luma, cb, cr, 8, 4,
        lumaGrain, cbGrain, crGrain, 4, 2, 3, 2, 8, 1, 1);

    // oracle: 3-point y curve, 2-point chroma curves
    std::int32_t oy[256], ocbLut[256], ocrLut[256];
    oracleInitScaling(yPts, 3);
    for (int i = 0; i < 256; ++i) {
        oy[i] = oracleScalingLut[i];
    }
    oracleInitScaling(cbPts, 2);
    for (int i = 0; i < 256; ++i) {
        ocbLut[i] = oracleScalingLut[i];
    }
    oracleInitScaling(crPts, 2);
    for (int i = 0; i < 256; ++i) {
        ocrLut[i] = oracleScalingLut[i];
    }

    std::uint8_t ol[8 * 6], ocb[4 * 3], ocr[4 * 3];
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 4; ++j) {
            ol[i * 8 + j] = (std::uint8_t)(i * 37 + j * 53 + 8);
        }
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 2; ++j) {
            ocb[i * 4 + j] = (std::uint8_t)(90 + i * 30 + j * 10);
            ocr[i * 4 + j] = (std::uint8_t)(70 + i * 45 + j * 15);
        }
    }

    const std::int32_t rnd = 128;
    // chroma pass
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 2; ++j) {
            std::int32_t avg =
                (ol[(i << 1) * 8 + (j << 1)] + ol[(i << 1) * 8 + (j << 1) + 1] + 1) >> 1;
            std::int32_t idx = ((avg * 42 + 27 * ocb[i * 4 + j]) >> 6) + 60;
            if (idx < 0) idx = 0;
            if (idx > 255) idx = 255;
            std::int32_t v = ocb[i * 4 + j] +
                ((oracleScaleLut(ocbLut, idx, 8) * cbGrain[i * 2 + j] + rnd) >> 8);
            if (v < 16) v = 16;
            if (v > 240) v = 240;
            ocb[i * 4 + j] = (std::uint8_t)v;
            idx = ((avg * 37 + 22 * ocr[i * 4 + j]) >> 6) + 40;
            if (idx < 0) idx = 0;
            if (idx > 255) idx = 255;
            v = ocr[i * 4 + j] +
                ((oracleScaleLut(ocrLut, idx, 8) * crGrain[i * 2 + j] + rnd) >> 8);
            if (v < 16) v = 16;
            if (v > 240) v = 240;
            ocr[i * 4 + j] = (std::uint8_t)v;
        }
    }
    // luma pass
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 4; ++j) {
            std::int32_t v = ol[i * 8 + j] +
                ((oracleScaleLut(oy, ol[i * 8 + j], 8) * lumaGrain[i * 4 + j] +
                  rnd) >> 8);
            if (v < 16) v = 16;
            if (v > 235) v = 235;
            ol[i * 8 + j] = (std::uint8_t)v;
        }
    }
    bool ok = true;
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (luma[i * 8 + j] != ol[i * 8 + j]) {
                ok = false;
                FAIL("luma (", i, ",", j, ") port ", luma[i * 8 + j],
                     " oracle ", ol[i * 8 + j]);
            }
        }
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 2; ++j) {
            if (cb[i * 4 + j] != ocb[i * 4 + j]) {
                ok = false;
                FAIL("cb (", i, ",", j, ") port ", cb[i * 4 + j], " oracle ",
                     ocb[i * 4 + j]);
            }
            if (cr[i * 4 + j] != ocr[i * 4 + j]) {
                ok = false;
                FAIL("cr (", i, ",", j, ") port ", cr[i * 4 + j], " oracle ",
                     ocr[i * 4 + j]);
            }
        }
    }
    CHECK(ok);
}

// add_noise_to_block (grainSynthesis.c:506-:605), hand-derived single-cell
// case: luma 4x4 (stride 8), half dims 2x2, subsampling 1x1 so chroma is
// 2x2 (stride 4). Scaling LUTs flat: y lut = 64 everywhere, cb = 32, cr =
// 32 (single scaling point). cb_grain = +20 everywhere, cr_grain = -30.
// scaling_shift 8 -> rounding 128; every contribution is exact.
// clip_to_restricted_range 0 -> [0, 255] clamps.
// Luma cell: (64 * grain + 128) >> 8. Chroma index for cb with
// cb_mult = 160, cb_luma_mult = 160, cb_offset = 50:
// idx = ((avg*160 + 160*cb) >> 6) + 50 clamped to [0, 255];
// contribution = (32 * grain + 128) >> 8.
TEST_CASE("l10 grain addNoiseToBlock: flat LUT single-plane hand math") {
    l10_filters::grain::initGrainRange(8);
    l10_filters::grain::AomFilmGrain params = l10_filters::grain::makeParams();
    params.bit_depth = 8;
    params.scaling_shift = 8;
    params.num_y_points = 1;
    params.num_cb_points = 1;
    params.num_cr_points = 1;
    params.chroma_scaling_from_luma = 0;
    params.clip_to_restricted_range = 0;
    params.cb_mult = 160;
    params.cb_luma_mult = 160;
    params.cb_offset = 256 + 50;
    params.cr_mult = 160;
    params.cr_luma_mult = 160;
    params.cr_offset = 256 + 50;

    const std::int32_t yPts[1][2] = {{128, 64}};
    const std::int32_t cbPts[1][2] = {{128, 32}};
    const std::int32_t crPts[1][2] = {{128, 32}};
    l10_filters::grain::initScalingLuts(&params, yPts, cbPts, crPts);

    std::uint8_t luma[8 * 4] = {};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            luma[i * 8 + j] = (std::uint8_t)(10 * i + j); // 0..13
        }
    }
    std::uint8_t cb[4 * 2] = {40, 60, 80, 100};
    std::uint8_t cr[4 * 2] = {200, 150, 100, 50};
    std::int32_t lumaGrain[4 * 4] = {40};
    for (int i = 0; i < 16; ++i) {
        lumaGrain[i] = 40;
    }
    std::int32_t cbGrain[2 * 2] = {20, 20, 20, 20};
    std::int32_t crGrain[2 * 2] = {-30, -30, -30, -30};

    l10_filters::grain::addNoiseToBlock(&params, luma, cb, cr, 8, 4,
        lumaGrain, cbGrain, crGrain, 4, 2, 2, 2, 8, 1, 1);

    // Chroma: average_luma = (l00 + l01 + 1) >> 1 over the 2-pixel pair.
    // Cell (0,0): avg = (0+1+1)>>1 = 1; cb idx = ((1*160 + 160*40) >> 6) + 50
    // = (6560>>6) + 50 = 102 + 50 = 152; contribution =
    // (32*20 + 128)>>8 = 768>>8 = 3; cb = 40 + 3 = 43.
    // cr idx = ((1*160 + 160*200) >> 6) + 50 = 552; clamp 255;
    // contribution = (-960 + 128)>>8 = -832>>8 = -4; cr = 200 - 4 = 196.
    CHECK_EQ(cb[0], 43);
    CHECK_EQ(cr[0], 196);
    // Luma cell (0,0): (64*40 + 128) >> 8 = 10; clamp(0 + 10) = 10.
    // Cell (1,1): pixel 11 + 10 = 21.
    CHECK_EQ(luma[0], 10);
    CHECK_EQ(luma[1 * 8 + 1], 21);
}
