#include <doctest.h>

#include <cstdint>

#include "resize.h"

TEST_CASE("l11_resize scaffold boots") {
    CHECK(true);
}

TEST_CASE("getDown2Length halves once for steps 1") {
    CHECK(resize::getDown2Length(16, 1) == 8);
}

TEST_CASE("getDown2Length halves twice for steps 2") {
    CHECK(resize::getDown2Length(16, 2) == 4);
}

TEST_CASE("getDown2Steps 16 to 8 is one step") {
    CHECK(resize::getDown2Steps(16, 8) == 1);
}

TEST_CASE("getDown2Steps 64 to 16 is two steps") {
    CHECK(resize::getDown2Steps(64, 16) == 2);
}

TEST_CASE("getDown2Steps range property over enumerated lengths") {
    // Range-condition audit (rule 6): the step count s must be the exact
    // halving count, i.e. s more halvings stay >= out and s+1 drop below out.
    // The in_length == 1 break case is pinned separately below.
    const int lens[] = {2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129};
    for (int in : lens) {
        const int outs[] = {1, 2, in / 3 > 0 ? in / 3 : 1, in / 2, in - 1, in};
        for (int out : outs) {
            if (out < 1) {
                continue;
            }
            const int s = resize::getDown2Steps(in, out);
            CHECK(resize::getDown2Length(in, s) >= out);
            // The reference stops on the condition OR on the length==1 break;
            // at the floor, further halvings never drop below out (1 -> 1).
            if (resize::getDown2Length(in, s) != 1) {
                CHECK(resize::getDown2Length(in, s + 1) < out);
            }
        }
    }
}

TEST_CASE("getDown2Steps length 1 break special case") {
    CHECK(resize::getDown2Steps(1, 1) == 1);
    CHECK(resize::getDown2Steps(1, 4) == 0);
    CHECK(resize::getDown2Steps(2, 1) == 1);
    CHECK(resize::getDown2Steps(3, 1) == 2);
}

static const std::uint8_t kRamp16[] = {11, 48, 85, 122, 159, 196, 233, 14, 51, 88, 125, 162, 199, 236, 17, 54};
static const std::uint8_t kExpSymEven16[] = {30, 103, 186, 124, 62, 146, 202, 51};
static const std::uint8_t kRamp4[] = {11, 48, 85, 122};
static const std::uint8_t kExpSymEven4[] = {31, 102};
static const std::uint8_t kRamp5[] = {11, 48, 85, 122, 159};
static const std::uint8_t kExpSymEven5[] = {30, 105, 158};
static const std::uint8_t kRamp6[] = {11, 48, 85, 122, 159, 196};
static const std::uint8_t kExpSymEven6[] = {30, 104, 177};
static const std::uint8_t kRamp7[] = {11, 48, 85, 122, 159, 196, 233};
static const std::uint8_t kExpSymEven7[] = {30, 103, 179, 232};
static const std::uint8_t kRamp8[] = {11, 48, 85, 122, 159, 196, 233, 14};
static const std::uint8_t kExpSymEven8[] = {30, 103, 186, 123};
static const std::uint8_t kRamp9[] = {11, 48, 85, 122, 159, 196, 233, 14, 51};
static const std::uint8_t kExpSymEven9[] = {30, 103, 186, 125, 42};
static const std::uint8_t kRamp12[] = {11, 48, 85, 122, 159, 196, 233, 14, 51, 88, 125, 162};
static const std::uint8_t kExpSymEven12[] = {30, 103, 186, 124, 62, 143};
static const std::uint8_t kRamp17[] = {11, 48, 85, 122, 159, 196, 233, 14, 51, 88, 125, 162, 199, 236, 17, 54, 91};
static const std::uint8_t kExpSymEven17[] = {30, 103, 186, 124, 62, 146, 202, 53, 88};
static const std::uint8_t kRamp31[] = {11, 48, 85, 122, 159, 196, 233, 14, 51, 88, 125, 162, 199, 236, 17, 54, 91, 128, 165, 202, 239, 20, 57, 94, 131, 168, 205, 242, 23, 60, 97};
static const std::uint8_t kExpSymEven31[] = {30, 103, 186, 124, 62, 146, 202, 52, 108, 192, 130, 68, 152, 208, 59, 94};
static const std::uint8_t kRamp32[] = {11, 48, 85, 122, 159, 196, 233, 14, 51, 88, 125, 162, 199, 236, 17, 54, 91, 128, 165, 202, 239, 20, 57, 94, 131, 168, 205, 242, 23, 60, 97, 134};
static const std::uint8_t kExpSymEven32[] = {30, 103, 186, 124, 62, 146, 202, 52, 108, 192, 130, 68, 152, 208, 58, 113};

static const std::uint8_t kExpSymOdd16[] = {19, 84, 165, 169, 45, 125, 205, 83};

TEST_CASE("down2Symodd 16 ramp fixture") {
    std::uint8_t out[8] = {0};
    resize::down2Symodd(kRamp16, 16, out);
    for (int i = 0; i < 8; ++i) {
        CHECK(out[i] == kExpSymOdd16[i]);
    }
}

static const std::uint8_t kExpSymOdd4[] = {19, 86};
static const std::uint8_t kExpSymOdd5[] = {19, 85, 151};
static const std::uint8_t kExpSymOdd6[] = {19, 84, 161};
static const std::uint8_t kExpSymOdd7[] = {19, 84, 160, 225};
static const std::uint8_t kExpSymOdd8[] = {19, 84, 165, 171};
static const std::uint8_t kExpSymOdd9[] = {19, 84, 165, 170, 37};
static const std::uint8_t kExpSymOdd12[] = {19, 84, 165, 169, 45, 127};
static const std::uint8_t kExpSymOdd17[] = {19, 84, 165, 169, 45, 125, 205, 82, 77};
static const std::uint8_t kExpSymOdd31[] = {19, 84, 165, 169, 45, 125, 205, 81, 85, 171, 175, 51, 131, 211, 88, 83};
static const std::uint8_t kExpSymOdd32[] = {19, 84, 165, 169, 45, 125, 205, 81, 85, 171, 175, 51, 131, 211, 87, 93};

TEST_CASE("chooseInterpFilter boundary enumeration at in=16") {
    // choose_interp_filter resize.c:272-285: boundaries 16/13/11/9 at in=16.
    CHECK(resize::chooseInterpFilter(16, 16)[0] == 0); // 1000 row0 starts {0,0,0,128,...}
    CHECK(resize::chooseInterpFilter(16, 16)[3] == 128);
    CHECK(resize::chooseInterpFilter(16, 13)[0] == 3); // 875 row0 {3,-8,13,112,...}
    CHECK(resize::chooseInterpFilter(16, 12)[0] == 2); // 750 row0 {2,-11,25,96,...}
    CHECK(resize::chooseInterpFilter(16, 10)[0] == -1); // 625 row0 {-1,-8,33,80,...}
    CHECK(resize::chooseInterpFilter(16, 9)[0] == -1);
    CHECK(resize::chooseInterpFilter(16, 8)[0] == -3); // 500 row0 {-3,0,35,64,...}
}

static const std::uint8_t kExpInterp16to13[] = {14, 61, 105, 150, 210, 173, 7, 86, 123, 163, 240, 92, 33};

TEST_CASE("interpolateCore 16 to 13 downscale fixture") {
    std::uint8_t out[13] = {0};
    resize::interpolateCore(kRamp16, 16, out, 13, resize::chooseInterpFilter(16, 13));
    for (int i = 0; i < 13; ++i) {
        CHECK(out[i] == kExpInterp16to13[i]);
    }
}

static const std::uint8_t kExpInterp16to8[] = {30, 103, 188, 124, 60, 146, 200, 53};
static const std::uint8_t kExpInterp16to9[] = {25, 92, 158, 208, 31, 99, 167, 203, 32};
static const std::uint8_t kExpInterp16to12[] = {17, 67, 117, 162, 231, 69, 44, 109, 153, 226, 127, 26};

TEST_CASE("interpolateCore range enumeration over downscale outs") {
    // 16->8 selects the 500 table, 16->9 the 625, 16->12 the 750 (rule 6:
    // every filter selection boundary exercised, not one case).
    struct Case { int outLen; int expLen; const std::uint8_t* exp; };
    const Case cases[] = {{8, 8, kExpInterp16to8}, {9, 9, kExpInterp16to9}, {12, 12, kExpInterp16to12}};
    std::uint8_t out[13] = {0};
    for (const auto& c : cases) {
        resize::interpolateCore(kRamp16, 16, out, c.outLen, resize::chooseInterpFilter(16, c.outLen));
        for (int i = 0; i < c.expLen; ++i) {
            CHECK(out[i] == c.exp[i]);
        }
    }
}

static const std::uint8_t kExpInterp4to16[] = {8, 9, 14, 22, 32, 43, 53, 62, 71, 80, 90, 101, 111, 119, 124, 125};

TEST_CASE("interpolateCore 4 to 16 upscale full-clamp path") {
    // in=4, out=16: x1 = 13 > x2 = 0, so the x1 > x2 all-clamped loop runs.
    std::uint8_t out[16] = {0};
    resize::interpolateCore(kRamp4, 4, out, 16, resize::chooseInterpFilter(4, 16));
    for (int i = 0; i < 16; ++i) {
        CHECK(out[i] == kExpInterp4to16[i]);
    }
}

static const std::uint8_t kExpInterp4to8[] = {8, 18, 37, 58, 75, 96, 115, 125};
static const std::uint8_t kExpInterp9to13[] = {9, 27, 57, 83, 107, 135, 159, 175, 237, 219, 57, 5, 59};

TEST_CASE("interpolateCore upscale and negative-offset fixtures") {
    // 4->8: upscale with negative offset (three-part path); 9->13: negative
    // offset with in=9 (the offset crosses zero-scale, still three-part).
    std::uint8_t out8[8] = {0};
    resize::interpolateCore(kRamp4, 4, out8, 8, resize::chooseInterpFilter(4, 8));
    for (int i = 0; i < 8; ++i) {
        CHECK(out8[i] == kExpInterp4to8[i]);
    }
    std::uint8_t out13[13] = {0};
    resize::interpolateCore(kRamp9, 9, out13, 13, resize::chooseInterpFilter(9, 13));
    for (int i = 0; i < 13; ++i) {
        CHECK(out13[i] == kExpInterp9to13[i]);
    }
}

TEST_CASE("interpolate wrapper matches core with chosen filter") {
    std::uint8_t out[13] = {0};
    resize::interpolate(kRamp16, 16, out, 13);
    for (int i = 0; i < 13; ++i) {
        CHECK(out[i] == kExpInterp16to13[i]);
    }
    std::uint8_t out16[16] = {0};
    resize::interpolate(kRamp4, 4, out16, 16);
    for (int i = 0; i < 16; ++i) {
        CHECK(out16[i] == kExpInterp4to16[i]);
    }
}

static const std::uint8_t kExpMs16to8[] = {30, 103, 186, 124, 62, 146, 202, 51};
static const std::uint8_t kExpMs16to4[] = {74, 145, 115, 125};
static const std::uint8_t kExpMs16to13[] = {14, 61, 105, 150, 210, 173, 7, 86, 123, 163, 240, 92, 33};
static const std::uint8_t kExpMs12to5[] = {33, 137, 170, 68, 131};
static const std::uint8_t kExpMs9to2[] = {64, 95};

TEST_CASE("resizeMultistep 16 to 8 single down2 step") {
    std::uint8_t otmp[16] = {0};
    std::uint8_t out[8] = {0};
    resize::resizeMultistep(kRamp16, 16, out, 8, otmp);
    for (int i = 0; i < 8; ++i) {
        CHECK(out[i] == kExpMs16to8[i]);
    }
}

TEST_CASE("resizeMultistep composition set") {
    // 16->4: steps == 2, the otmp/otmp2 recursion really runs (brief fixture e).
    std::uint8_t otmp4[16] = {0};
    std::uint8_t out4[4] = {0};
    resize::resizeMultistep(kRamp16, 16, out4, 4, otmp4);
    for (int i = 0; i < 4; ++i) {
        CHECK(out4[i] == kExpMs16to4[i]);
    }
    // Composition equality: multistep(16->8) == down2Symeven(16->8) directly.
    {
        std::uint8_t otmp[16] = {0};
        std::uint8_t direct[8] = {0};
        std::uint8_t out8[8] = {0};
        resize::down2Symeven(kRamp16, 16, direct);
        resize::resizeMultistep(kRamp16, 16, out8, 8, otmp);
        for (int i = 0; i < 8; ++i) {
            CHECK(out8[i] == direct[i]);
        }
    }
    // 16->13: steps == 0, the interpolate-only branch.
    {
        std::uint8_t otmp[16] = {0};
        std::uint8_t out13[13] = {0};
        resize::resizeMultistep(kRamp16, 16, out13, 13, otmp);
        for (int i = 0; i < 13; ++i) {
            CHECK(out13[i] == kExpMs16to13[i]);
        }
    }
    // 12->5: one down2 step to 6, then interpolate 6->5.
    {
        std::uint8_t otmp[12] = {0};
        std::uint8_t out5[5] = {0};
        resize::resizeMultistep(kRamp12, 12, out5, 5, otmp);
        for (int i = 0; i < 5; ++i) {
            CHECK(out5[i] == kExpMs12to5[i]);
        }
    }
    // 9->2: three symodd steps on odd filtered lengths (9->5->3->2).
    {
        std::uint8_t otmp[10] = {0};
        std::uint8_t out2[2] = {0};
        resize::resizeMultistep(kRamp9, 9, out2, 2, otmp);
        for (int i = 0; i < 2; ++i) {
            CHECK(out2[i] == kExpMs9to2[i]);
        }
    }
    // 16->16: the memcpy branch.
    {
        std::uint8_t out16[16] = {0};
        resize::resizeMultistep(kRamp16, 16, out16, 16, otmp4);
        for (int i = 0; i < 16; ++i) {
            CHECK(out16[i] == kRamp16[i]);
        }
    }
}

TEST_CASE("fill pair strided round trip") {
    std::uint8_t img[20];
    for (int i = 0; i < 20; ++i) {
        img[i] = (std::uint8_t)(i + 1);
    }
    std::uint8_t arr[4] = {0};
    resize::fillColToArr(img, 5, 4, arr);
    CHECK(arr[0] == 1);
    CHECK(arr[1] == 6);
    CHECK(arr[2] == 11);
    CHECK(arr[3] == 16);
    std::uint8_t img2[20] = {0};
    std::uint8_t arr2[4] = {2, 7, 12, 17};
    resize::fillArrToCol(img2, 5, 4, arr2);
    CHECK(img2[0] == 2);
    CHECK(img2[5] == 7);
    CHECK(img2[10] == 12);
    CHECK(img2[15] == 17);
}

static const std::uint8_t kPlane84[4][8] = {{5, 8, 11, 14, 17, 20, 23, 26}, {21, 24, 27, 30, 33, 36, 39, 42}, {37, 40, 43, 46, 49, 52, 55, 58}, {53, 56, 59, 62, 65, 68, 71, 74}};
static const std::uint8_t kExpPlane4x2[2][4] = {{16, 21, 28, 33}, {47, 52, 59, 64}};
static const std::uint8_t kExpPlaneHoriz[4][4] = {{7, 12, 19, 24}, {23, 28, 35, 40}, {39, 44, 51, 56}, {55, 60, 67, 72}};

TEST_CASE("resizePlaneC padded strides 8x4 to 4x2") {
    std::uint8_t in[4 * 12] = {0};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 8; ++c) {
            in[r * 12 + c] = kPlane84[r][c];
        }
    }
    std::uint8_t out[2 * 7] = {0};
    CHECK(resize::resizePlaneC(in, 4, 8, 12, out, 2, 4, 7));
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 4; ++c) {
            CHECK(out[r * 7 + c] == kExpPlane4x2[r][c]);
        }
    }
}

TEST_CASE("resizePlaneHorizontal padded strides 8x4 to 4x4") {
    std::uint8_t in[4 * 12] = {0};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 8; ++c) {
            in[r * 12 + c] = kPlane84[r][c];
        }
    }
    std::uint8_t out[4 * 7] = {0};
    CHECK(resize::resizePlaneHorizontal(in, 4, 8, 12, out, 4, 4, 7));
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            CHECK(out[r * 7 + c] == kExpPlaneHoriz[r][c]);
        }
    }
}

TEST_CASE("down2Symodd range enumeration over lengths") {
    const int lens[] = {4, 5, 6, 7, 8, 9, 12, 16, 17, 31, 32};
    const std::uint8_t* ins[] = {kRamp4, kRamp5, kRamp6, kRamp7, kRamp8, kRamp9, kRamp12, kRamp16, kRamp17, kRamp31, kRamp32};
    const int outLens[] = {2, 3, 3, 4, 4, 5, 6, 8, 9, 16, 16};
    const std::uint8_t* exps[] = {kExpSymOdd4, kExpSymOdd5, kExpSymOdd6, kExpSymOdd7, kExpSymOdd8, kExpSymOdd9, kExpSymOdd12, kExpSymOdd16, kExpSymOdd17, kExpSymOdd31, kExpSymOdd32};
    std::uint8_t out[17] = {0};
    for (int idx = 0; idx < 11; ++idx) {
        resize::down2Symodd(ins[idx], lens[idx], out);
        for (int i = 0; i < outLens[idx]; ++i) {
            CHECK(out[i] == exps[idx][i]);
        }
    }
}

TEST_CASE("down2Symeven range enumeration over lengths") {
    const int lens[] = {4, 5, 6, 7, 8, 9, 12, 16, 17, 31, 32};
    const std::uint8_t* ins[] = {kRamp4, kRamp5, kRamp6, kRamp7, kRamp8, kRamp9, kRamp12, kRamp16, kRamp17, kRamp31, kRamp32};
    const int outLens[] = {2, 3, 3, 4, 4, 5, 6, 8, 9, 16, 16};
    const std::uint8_t* exps[] = {kExpSymEven4, kExpSymEven5, kExpSymEven6, kExpSymEven7, kExpSymEven8, kExpSymEven9, kExpSymEven12, kExpSymEven16, kExpSymEven17, kExpSymEven31, kExpSymEven32};
    std::uint8_t out[17] = {0};
    for (int idx = 0; idx < 11; ++idx) {
        resize::down2Symeven(ins[idx], lens[idx], out);
        for (int i = 0; i < outLens[idx]; ++i) {
            CHECK(out[i] == exps[idx][i]);
        }
    }
}

TEST_CASE("hbdInterpolateCore identity 2 to 2 bd 8") {
    const std::uint16_t in[2] = {100, 200};
    std::uint16_t out[2] = {0};
    resize::hbdInterpolateCore(in, 2, out, 2, 8, resize::chooseInterpFilter(2, 2));
    CHECK(out[0] == 100);
}

TEST_CASE("hbdInterpolateCore derivation bd8 == 8-bit interpolateCore") {
    // Primary oracle (HB1 pattern): the hbd twin at bd 8 must be elementwise
    // equal to the landed mutation-proven 8-bit twin on uint8-range inputs,
    // for every branch: 16->13 (three-part middle), 4->16 (x1 > x2 all-clamp).
    std::uint16_t wide16[16];
    for (int i = 0; i < 16; ++i) {
        wide16[i] = kRamp16[i];
    }
    std::uint8_t out8bit[16] = {0};
    std::uint16_t outHbd[16] = {0};
    resize::interpolateCore(kRamp16, 16, out8bit, 13, resize::chooseInterpFilter(16, 13));
    resize::hbdInterpolateCore(wide16, 16, outHbd, 13, 8, resize::chooseInterpFilter(16, 13));
    for (int i = 0; i < 13; ++i) {
        CHECK(outHbd[i] == out8bit[i]);
    }
    std::uint16_t wide4[4];
    for (int i = 0; i < 4; ++i) {
        wide4[i] = kRamp4[i];
    }
    std::uint16_t outHbd16[16] = {0};
    resize::interpolateCore(kRamp4, 4, out8bit, 16, resize::chooseInterpFilter(4, 16));
    resize::hbdInterpolateCore(wide4, 4, outHbd16, 16, 8, resize::chooseInterpFilter(4, 16));
    for (int i = 0; i < 16; ++i) {
        CHECK(outHbd16[i] == out8bit[i]);
    }
    // Edge-contrast input (M3 killer): the initial part clamps negative taps
    // to input[0]; a flip to the wrong boundary (input[in-1]) shifts the x=0
    // sum by (in[15]-in[0])*(f0+f1+f2) = 255 for the 875 row 7 shape, which
    // crosses the rounding boundary the kRamp16 fixture (delta 43) did not.
    const std::uint8_t edgeRamp[16] = {0, 48, 85, 122, 159, 196, 233, 14, 51, 88, 125, 162, 199, 236, 17, 255};
    std::uint16_t wideEdge[16];
    for (int i = 0; i < 16; ++i) {
        wideEdge[i] = edgeRamp[i];
    }
    std::uint16_t outHbdEdge[13] = {0};
    resize::interpolateCore(edgeRamp, 16, out8bit, 13, resize::chooseInterpFilter(16, 13));
    resize::hbdInterpolateCore(wideEdge, 16, outHbdEdge, 13, 8, resize::chooseInterpFilter(16, 13));
    for (int i = 0; i < 13; ++i) {
        CHECK(outHbdEdge[i] == out8bit[i]);
    }
}

TEST_CASE("hbdInterpolateCore bd10/bd12 absolute fixtures, upper clip fires") {
    // Hand-derived from resize.c:489-560 + clip_pixel_highbd
    // (definitions.h:704-714). in=8, out=4 -> delta 32768, offset 8192,
    // x1=2 > x2=1 (the all-clamped branch), y walk 8320/41088/73856/106624,
    // sub_pel 32 at every x -> 500-table row 32 {-1,-4,14,55,55,14,-4,-1}
    // (measured on disk; NOTE: a text-editor read of resize.cpp once rendered
    // rows 30-63 shifted by one row - the byte-level table diff ref-vs-port
    // is the authority, 64 rows, 0 diffs). Fixture v1 was derived from the
    // misrendered row and FAILED RED against the correct implementation; the
    // corrected fixture v2 below. Input puts max under the positive taps and
    // 0 under the negative taps at x=1, so sum = 138*M + 64 -> bd 10: 1103 ->
    // clip 1023; bd 12: 4415 -> clip 4095. At x=0/x=2 sum = 64*M + 64
    // (512 at bd 10, 2048 at bd 12 - unclipped). The 8-bit twin on
    // uint8-range inputs clips the same shape at 255, so this fixture is NOT
    // covered by the derivation test.
    const std::uint16_t in10[8] = {0, 1023, 1023, 1023, 1023, 0, 0, 0};
    std::uint16_t out[4] = {0};
    resize::hbdInterpolateCore(in10, 8, out, 4, 10, resize::chooseInterpFilter(8, 4));
    CHECK(out[0] == 512);
    CHECK(out[1] == 1023);
    CHECK(out[2] == 512);
    CHECK(out[3] == 0);
    const std::uint16_t in12[8] = {0, 4095, 4095, 4095, 4095, 0, 0, 0};
    std::uint16_t out12[4] = {0};
    resize::hbdInterpolateCore(in12, 8, out12, 4, 12, resize::chooseInterpFilter(8, 4));
    CHECK(out12[0] == 2048);
    CHECK(out12[1] == 4095);
    CHECK(out12[2] == 2048);
    CHECK(out12[3] == 0);
}

TEST_CASE("hbdInterpolate wrapper matches core with chosen filter") {
    const std::uint16_t in10[8] = {0, 1023, 1023, 1023, 1023, 0, 0, 0};
    std::uint16_t viaWrapper[4] = {0};
    std::uint16_t viaCore[4] = {0};
    resize::hbdInterpolate(in10, 8, viaWrapper, 4, 10);
    resize::hbdInterpolateCore(in10, 8, viaCore, 4, 10, resize::chooseInterpFilter(8, 4));
    for (int i = 0; i < 4; ++i) {
        CHECK(viaWrapper[i] == viaCore[i]);
    }
}

TEST_CASE("hbdDown2Symeven derivation bd8 == 8-bit down2Symeven") {
    // Range-condition audit (rule 6): every length class exercised - short
    // (4, l1 > l2), initial/middle/end splits (5..9, 12), odd (17), and the
    // long pair (31, 32); the landed 8-bit twin is mutation-proven.
    const int lens[] = {4, 5, 6, 7, 8, 9, 12, 16, 17, 31, 32};
    const std::uint8_t* ins[] = {kRamp4, kRamp5, kRamp6, kRamp7, kRamp8, kRamp9, kRamp12, kRamp16, kRamp17, kRamp31, kRamp32};
    const int outLens[] = {2, 3, 3, 4, 4, 5, 6, 8, 9, 16, 16};
    std::uint8_t out8[17] = {0};
    std::uint16_t wide[32];
    std::uint16_t outH[17] = {0};
    for (int idx = 0; idx < 11; ++idx) {
        resize::down2Symeven(ins[idx], lens[idx], out8);
        for (int i = 0; i < lens[idx]; ++i) {
            wide[i] = ins[idx][i];
        }
        resize::hbdDown2Symeven(wide, lens[idx], outH, 8);
        for (int i = 0; i < outLens[idx]; ++i) {
            CHECK(outH[i] == out8[i]);
        }
    }
}

TEST_CASE("hbdDown2Symeven bd10 absolute fixture, upper clip fires") {
    // Hand-derived from resize.c:568-617, input length 12 (l1=4, l2=8:
    // initial i=0,2 / middle i=4,6 / end i=8,10), filter {56,12,-3,-1},
    // bd 10 -> clip_pixel_highbd bound 1023 (definitions.h:709-710).
    // out[2]: sum = 64 + 2046*56 + 2046*12 - 1023 = 138169 >> 7 = 1079 ->
    // clip 1023. out[4]: sum = 64 - 3069 - 1023 = -4028 >> 7 -> clip 0
    // (lower bound, bd-independent). out[3]: sum = 65536 >> 7 = 512 exact.
    const std::uint16_t in[12] = {0, 1023, 0, 1023, 1023, 1023, 1023, 0, 0, 0, 0, 0};
    std::uint16_t out[6] = {0};
    resize::hbdDown2Symeven(in, 12, out, 10);
    CHECK(out[0] == 416);
    CHECK(out[1] == 607);
    CHECK(out[2] == 1023);
    CHECK(out[3] == 512);
    CHECK(out[4] == 0);
    CHECK(out[5] == 0);
}

TEST_CASE("hbdDown2Symodd derivation bd8 == 8-bit down2Symodd") {
    const int lens[] = {4, 5, 6, 7, 8, 9, 12, 16, 17, 31, 32};
    const std::uint8_t* ins[] = {kRamp4, kRamp5, kRamp6, kRamp7, kRamp8, kRamp9, kRamp12, kRamp16, kRamp17, kRamp31, kRamp32};
    const int outLens[] = {2, 3, 3, 4, 4, 5, 6, 8, 9, 16, 16};
    std::uint8_t out8[17] = {0};
    std::uint16_t wide[32];
    std::uint16_t outH[17] = {0};
    for (int idx = 0; idx < 11; ++idx) {
        resize::down2Symodd(ins[idx], lens[idx], out8);
        for (int i = 0; i < lens[idx]; ++i) {
            wide[i] = ins[idx][i];
        }
        resize::hbdDown2Symodd(wide, lens[idx], outH, 8);
        for (int i = 0; i < outLens[idx]; ++i) {
            CHECK(outH[i] == out8[i]);
        }
    }
}

TEST_CASE("hbdDown2Symodd bd10 absolute fixture, upper clip fires") {
    // Hand-derived from resize.c:619-668, input length 11 (l1=4, l2=8:
    // initial i=0,2 / middle i=4,6 / end i=8,10), filter {64,35,0,-3},
    // bd 10. out[2]: sum = 64 + 1023*64 + 2046*35 = 137146 >> 7 = 1071 ->
    // clip 1023. out[0]/out[4]: sum = 64 - 3069 < 0 -> clip 0.
    const std::uint16_t in[11] = {0, 0, 0, 1023, 1023, 1023, 0, 0, 0, 0, 0};
    std::uint16_t out[6] = {0};
    resize::hbdDown2Symodd(in, 11, out, 10);
    CHECK(out[0] == 0);
    CHECK(out[1] == 256);
    CHECK(out[2] == 1023);
    CHECK(out[3] == 256);
    CHECK(out[4] == 0);
    CHECK(out[5] == 0);
}

TEST_CASE("hbdResizeMultistep derivation bd8 == 8-bit resizeMultistep") {
    // Every branch: memcpy (16->16), one down2 step (16->8), two steps with
    // the otmp/otmp2 ping-pong (16->4), steps==0 interpolate-only (16->13),
    // down2-then-interpolate (12->5), odd symodd chain (9->2).
    struct Case { int inLen; int outLen; const std::uint8_t* in; };
    const Case cases[] = {
        {16, 8, kRamp16}, {16, 4, kRamp16}, {16, 13, kRamp16}, {12, 5, kRamp12}, {9, 2, kRamp9},
    };
    std::uint8_t out8[17] = {0};
    std::uint8_t otmp8[17] = {0};
    std::uint16_t wide[17];
    std::uint16_t outH[17] = {0};
    std::uint16_t otmpH[17] = {0};
    for (const auto& c : cases) {
        resize::resizeMultistep(c.in, c.inLen, out8, c.outLen, otmp8);
        for (int i = 0; i < c.inLen; ++i) {
            wide[i] = c.in[i];
        }
        resize::hbdResizeMultistep(wide, c.inLen, outH, c.outLen, otmpH, 8);
        for (int i = 0; i < c.outLen; ++i) {
            CHECK(outH[i] == out8[i]);
        }
    }
    // memcpy branch: in == out length.
    std::uint16_t out16[16] = {0};
    for (int i = 0; i < 16; ++i) {
        wide[i] = kRamp16[i];
    }
    resize::hbdResizeMultistep(wide, 16, out16, 16, otmpH, 8);
    for (int i = 0; i < 16; ++i) {
        CHECK(out16[i] == wide[i]);
    }
}

TEST_CASE("hbdResizeMultistep bd10 composition equalities") {
    // 12->6 at bd 10: steps == 1, the final step writes output directly; the
    // composition must equal a direct hbdDown2Symeven call on the same
    // bd-10-range input (fixture-A input, carrying values the 8-bit domain
    // cannot hold).
    const std::uint16_t in[12] = {0, 1023, 0, 1023, 1023, 1023, 1023, 0, 0, 0, 0, 0};
    std::uint16_t direct[6] = {0};
    std::uint16_t out[6] = {0};
    std::uint16_t otmp[12] = {0};
    resize::hbdDown2Symeven(in, 12, direct, 10);
    resize::hbdResizeMultistep(in, 12, out, 6, otmp, 10);
    for (int i = 0; i < 6; ++i) {
        CHECK(out[i] == direct[i]);
    }
    // 16->13 at bd 10: steps == 0, the hbdInterpolate-only branch; equal to
    // hbdInterpolate directly.
    const std::uint16_t wide16[16] = {11, 48, 85, 122, 159, 196, 233, 14, 51, 88, 125, 162, 199, 236, 17, 54};
    std::uint16_t direct13[13] = {0};
    std::uint16_t out13[13] = {0};
    std::uint16_t otmp16[16] = {0};
    resize::hbdInterpolate(wide16, 16, direct13, 13, 10);
    resize::hbdResizeMultistep(wide16, 16, out13, 13, otmp16, 10);
    for (int i = 0; i < 13; ++i) {
        CHECK(out13[i] == direct13[i]);
    }
}

TEST_CASE("hbdFill pair strided round trip") {
    std::uint16_t img[20];
    for (int i = 0; i < 20; ++i) {
        img[i] = (std::uint16_t)(i + 1);
    }
    std::uint16_t arr[4] = {0};
    resize::hbdFillColToArr(img, 5, 4, arr);
    CHECK(arr[0] == 1);
    CHECK(arr[1] == 6);
    CHECK(arr[2] == 11);
    CHECK(arr[3] == 16);
    std::uint16_t img2[20] = {0};
    std::uint16_t arr2[4] = {2, 7, 12, 17};
    resize::hbdFillArrToCol(img2, 5, 4, arr2);
    CHECK(img2[0] == 2);
    CHECK(img2[5] == 7);
    CHECK(img2[10] == 12);
    CHECK(img2[15] == 17);
}

TEST_CASE("hbdResizePlaneC derivation bd8 == 8-bit resizePlaneC, bd threading") {
    // Padded strides (in 12, out 7): the staging transposes must not read or
    // write the padding. bd8 derivation vs the landed 8-bit twin; then bd 10
    // on the same 8-bit-range input must be identical (the only bd-dependent
    // step is the clip bound, and no unclamped intermediate exceeds 255 on
    // this input, so the wider bound cannot change anything - the assertion
    // pins that the bd argument actually threads through every call).
    std::uint8_t in8[4 * 12] = {0};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 8; ++c) {
            in8[r * 12 + c] = kPlane84[r][c];
        }
    }
    std::uint8_t out8[2 * 7] = {0};
    CHECK(resize::resizePlaneC(in8, 4, 8, 12, out8, 2, 4, 7));
    std::uint16_t wide[4 * 12] = {0};
    for (int i = 0; i < 4 * 12; ++i) {
        wide[i] = in8[i];
    }
    std::uint16_t outH8[2 * 7] = {0};
    std::uint16_t outH10[2 * 7] = {0};
    CHECK(resize::hbdResizePlaneC(wide, 4, 8, 12, outH8, 2, 4, 7, 8));
    CHECK(resize::hbdResizePlaneC(wide, 4, 8, 12, outH10, 2, 4, 7, 10));
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 4; ++c) {
            CHECK(outH8[r * 7 + c] == out8[r * 7 + c]);
            CHECK(outH10[r * 7 + c] == out8[r * 7 + c]);
        }
    }
}

TEST_CASE("hbdResizePlaneHorizontal derivation bd8 == 8-bit, bd threading") {
    std::uint8_t in8[4 * 12] = {0};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 8; ++c) {
            in8[r * 12 + c] = kPlane84[r][c];
        }
    }
    std::uint8_t out8[4 * 7] = {0};
    CHECK(resize::resizePlaneHorizontal(in8, 4, 8, 12, out8, 4, 4, 7));
    std::uint16_t wide[4 * 12] = {0};
    for (int i = 0; i < 4 * 12; ++i) {
        wide[i] = in8[i];
    }
    std::uint16_t outH8[4 * 7] = {0};
    std::uint16_t outH10[4 * 7] = {0};
    CHECK(resize::hbdResizePlaneHorizontal(wide, 4, 8, 12, outH8, 4, 4, 7, 8));
    CHECK(resize::hbdResizePlaneHorizontal(wide, 4, 8, 12, outH10, 4, 4, 7, 10));
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            CHECK(outH8[r * 7 + c] == out8[r * 7 + c]);
            CHECK(outH10[r * 7 + c] == out8[r * 7 + c]);
        }
    }
}

TEST_CASE("down2Symeven 16 ramp fixture") {
    std::uint8_t out[8] = {0};
    resize::down2Symeven(kRamp16, 16, out);
    for (int i = 0; i < 8; ++i) {
        CHECK(out[i] == kExpSymEven16[i]);
    }
}
