#include "convolve.h"

#include <cassert>
#include <cstring>

namespace convolve {

// inter_prediction.c:241-:256: sub_pel_filters_8, transcribed.
alignas(256) const InterpKernel subPelFilters8[16] = {
    {0, 0, 0, 128, 0, 0, 0, 0},     {0, 2, -6, 126, 8, -2, 0, 0},    {0, 2, -10, 122, 18, -4, 0, 0},
    {0, 2, -12, 116, 28, -8, 2, 0}, {0, 2, -14, 110, 38, -10, 2, 0}, {0, 2, -14, 102, 48, -12, 2, 0},
    {0, 2, -16, 94, 58, -12, 2, 0}, {0, 2, -14, 84, 66, -12, 2, 0},  {0, 2, -14, 76, 76, -14, 2, 0},
    {0, 2, -12, 66, 84, -14, 2, 0}, {0, 2, -12, 58, 94, -16, 2, 0},  {0, 2, -12, 48, 102, -14, 2, 0},
    {0, 2, -10, 38, 110, -14, 2, 0}, {0, 2, -8, 28, 116, -12, 2, 0}, {0, 0, -4, 18, 122, -10, 2, 0},
    {0, 0, -2, 8, 126, -6, 2, 0},
};

// inter_prediction.c:257-:272: sub_pel_filters_4, transcribed.
alignas(256) const InterpKernel subPelFilters4[16] = {
    {0, 0, 0, 128, 0, 0, 0, 0},     {0, 0, -4, 126, 8, -2, 0, 0},   {0, 0, -8, 122, 18, -4, 0, 0},
    {0, 0, -10, 116, 28, -6, 0, 0}, {0, 0, -12, 110, 38, -8, 0, 0}, {0, 0, -12, 102, 48, -10, 0, 0},
    {0, 0, -14, 94, 58, -10, 0, 0}, {0, 0, -12, 84, 66, -10, 0, 0}, {0, 0, -12, 76, 76, -12, 0, 0},
    {0, 0, -10, 66, 84, -12, 0, 0}, {0, 0, -10, 58, 94, -14, 0, 0}, {0, 0, -10, 48, 102, -12, 0, 0},
    {0, 0, -8, 38, 110, -12, 0, 0}, {0, 0, -6, 28, 116, -10, 0, 0}, {0, 0, -4, 18, 122, -8, 0, 0},
    {0, 0, -2, 8, 126, -4, 0, 0},
};

// inter_prediction.c:1133-:1148: sub_pel_filters_8sharp, transcribed.
alignas(256) const InterpKernel subPelFilters8sharp[16] = {
    {0, 0, 0, 128, 0, 0, 0, 0},   {-2, 2, -6, 126, 8, -2, 2, 0},  {-2, 6, -12, 124, 16, -6, 4, -2},
    {-2, 8, -18, 120, 26, -10, 6, -2}, {-4, 10, -22, 116, 38, -14, 6, -2}, {-4, 10, -22, 108, 48, -18, 8, -2},
    {-4, 10, -24, 100, 60, -20, 8, -2}, {-4, 10, -24, 90, 70, -22, 10, -2}, {-4, 12, -24, 80, 80, -24, 12, -4},
    {-2, 10, -22, 70, 90, -24, 10, -4}, {-2, 8, -20, 60, 100, -24, 10, -4}, {-2, 8, -18, 48, 108, -22, 10, -4},
    {-2, 6, -14, 38, 116, -22, 10, -4}, {-2, 6, -10, 26, 120, -18, 8, -2}, {-2, 4, -6, 16, 124, -12, 6, -2},
    {0, 2, -2, 8, 126, -6, 2, -2},
};

// inter_prediction.c:1150-:1165: sub_pel_filters_8smooth, transcribed.
alignas(256) const InterpKernel subPelFilters8smooth[16] = {
    {0, 0, 0, 128, 0, 0, 0, 0}, {0, 2, 28, 62, 34, 2, 0, 0},   {0, 0, 26, 62, 36, 4, 0, 0},
    {0, 0, 22, 62, 40, 4, 0, 0}, {0, 0, 20, 60, 42, 6, 0, 0},  {0, 0, 18, 58, 44, 8, 0, 0},
    {0, 0, 16, 56, 46, 10, 0, 0}, {0, -2, 16, 54, 48, 12, 0, 0}, {0, -2, 14, 52, 52, 14, -2, 0},
    {0, 0, 12, 48, 54, 16, -2, 0}, {0, 0, 10, 46, 56, 16, 0, 0}, {0, 0, 8, 44, 58, 18, 0, 0},
    {0, 0, 6, 42, 60, 20, 0, 0}, {0, 0, 4, 40, 62, 22, 0, 0},  {0, 0, 4, 36, 62, 26, 0, 0},
    {0, 0, 2, 34, 62, 28, 2, 0},
};

// inter_prediction.c:1166-:1181: bilinear_filters, transcribed.
alignas(256) const InterpKernel bilinearFilters[16] = {
    {0, 0, 0, 128, 0, 0, 0, 0}, {0, 0, 0, 120, 8, 0, 0, 0},  {0, 0, 0, 112, 16, 0, 0, 0},
    {0, 0, 0, 104, 24, 0, 0, 0}, {0, 0, 0, 96, 32, 0, 0, 0},  {0, 0, 0, 88, 40, 0, 0, 0},
    {0, 0, 0, 80, 48, 0, 0, 0},  {0, 0, 0, 72, 56, 0, 0, 0},  {0, 0, 0, 64, 64, 0, 0, 0},
    {0, 0, 0, 56, 72, 0, 0, 0},  {0, 0, 0, 48, 80, 0, 0, 0},  {0, 0, 0, 40, 88, 0, 0, 0},
    {0, 0, 0, 32, 96, 0, 0, 0},  {0, 0, 0, 24, 104, 0, 0, 0}, {0, 0, 0, 16, 112, 0, 0, 0},
    {0, 0, 0, 8, 120, 0, 0, 0},
};

// inter_prediction.c:1182-:1197: sub_pel_filters_4smooth, transcribed.
alignas(256) const InterpKernel subPelFilters4smooth[16] = {
    {0, 0, 0, 128, 0, 0, 0, 0}, {0, 0, 30, 62, 34, 2, 0, 0}, {0, 0, 26, 62, 36, 4, 0, 0},
    {0, 0, 22, 62, 40, 4, 0, 0}, {0, 0, 20, 60, 42, 6, 0, 0}, {0, 0, 18, 58, 44, 8, 0, 0},
    {0, 0, 16, 56, 46, 10, 0, 0}, {0, 0, 14, 54, 48, 12, 0, 0}, {0, 0, 12, 52, 52, 12, 0, 0},
    {0, 0, 12, 48, 54, 14, 0, 0}, {0, 0, 10, 46, 56, 16, 0, 0}, {0, 0, 8, 44, 58, 18, 0, 0},
    {0, 0, 6, 42, 60, 20, 0, 0}, {0, 0, 4, 40, 62, 22, 0, 0},  {0, 0, 4, 36, 62, 26, 0, 0},
    {0, 0, 2, 34, 62, 30, 0, 0},
};

// inter_prediction.h:80-:84.
const InterpFilterParams interpFilterParamsList[interpFilterParamsListSize] = {
    {(const std::int16_t*)subPelFilters8, subpelTaps, subpelShifts, InterpFilter::EIGHTTAP_REGULAR},
    {(const std::int16_t*)subPelFilters8smooth, subpelTaps, subpelShifts, InterpFilter::EIGHTTAP_SMOOTH},
    {(const std::int16_t*)subPelFilters8sharp, subpelTaps, subpelShifts, InterpFilter::MULTITAP_SHARP},
    {(const std::int16_t*)bilinearFilters, subpelTaps, subpelShifts, InterpFilter::BILINEAR},
};

// inter_prediction.h:124-:126 (taps stays SUBPEL_TAPS = 8 - the reference's
// own zero-padded 4-tap representation).
const InterpFilterParams interp4tap[2] = {
    {(const std::int16_t*)subPelFilters4, subpelTaps, subpelShifts, InterpFilter::EIGHTTAP_REGULAR},
    {(const std::int16_t*)subPelFilters4smooth, subpelTaps, subpelShifts, InterpFilter::EIGHTTAP_SMOOTH},
};

// filter.h:60-:62.
InterpFilter extractInterpFilter(std::uint32_t filters, std::int32_t xFilter) {
    return (InterpFilter)((filters >> (xFilter ? 16 : 0)) & 0xffff);
}

// filter.h:77-:80.
const std::int16_t* getInterpFilterSubpelKernel(const InterpFilterParams filterParams, std::int32_t subpel) {
    return filterParams.filterPtr + filterParams.taps * subpel;
}

// inter_prediction.h:128-:137.
InterpFilterParams getInterpFilterParamsWithBlockSize(InterpFilter interpFilter, std::int32_t w) {
    if (w <= 4 && (interpFilter == InterpFilter::MULTITAP_SHARP || interpFilter == InterpFilter::EIGHTTAP_REGULAR)) {
        return interp4tap[0];
    } else if (w <= 4 && interpFilter == InterpFilter::EIGHTTAP_SMOOTH) {
        return interp4tap[1];
    }
    return interpFilterParamsList[static_cast<int>(interpFilter)];
}

// filter.h:64-:68.
std::uint32_t makeInterpFilters(InterpFilter yFilter, InterpFilter xFilter) {
    const std::uint16_t y16 = (std::uint16_t)((int)yFilter & 0xffff);
    const std::uint16_t x16 = (std::uint16_t)((int)xFilter & 0xffff);
    return y16 | ((std::uint32_t)x16 << 16);
}

// inter_prediction.h:139-:145.
void getConvolveFilterParams(std::uint32_t interpFilters, InterpFilterParams* paramsX, InterpFilterParams* paramsY,
                             std::int32_t w, std::int32_t h) {
    InterpFilter filterX = extractInterpFilter(interpFilters, 1);
    InterpFilter filterY = extractInterpFilter(interpFilters, 0);
    *paramsX = getInterpFilterParamsWithBlockSize(filterX, w);
    *paramsY = getInterpFilterParamsWithBlockSize(filterY, h);
}

// variance.c:71-:85.
const InterpFilterParams* getFilter(std::int32_t subpelSearch) {
    assert(subpelSearch >= (int)SubpelSearchType::USE_2_TAPS);
    switch (subpelSearch) {
    case (int)SubpelSearchType::USE_2_TAPS:
        return &interpFilterParamsList[(int)InterpFilter::BILINEAR];
    case (int)SubpelSearchType::USE_4_TAPS:
        return &interp4tap[(int)InterpFilter::EIGHTTAP_REGULAR];
    case (int)SubpelSearchType::USE_8_TAPS:
        return &interpFilterParamsList[(int)InterpFilter::EIGHTTAP_REGULAR];
    default:
        assert(0);
        return nullptr;
    }
}

// variance.c:88-:137 (svt_aom_upsampled_pred_c). The xd/cm/mi_row/mi_col/
// mv params are voided exactly as the reference does (variance.c:91-:95);
// they are MacroBlockD*/AV1Common*/Mv* there - struct-walled, so void*
// slots here (named deviation).
void upsampledPred(void* xd, void* cm, std::int32_t miRow, std::int32_t miCol, void* mv, std::uint8_t* compPred,
                   std::int32_t width, std::int32_t height, std::int32_t subpelXQ3, std::int32_t subpelYQ3,
                   const std::uint8_t* ref, std::int32_t refStride, std::int32_t subpelSearch) {
    (void)xd;
    (void)cm;
    (void)miRow;
    (void)miCol;
    (void)mv;
    const InterpFilterParams* filter = getFilter(subpelSearch);
    assert(filter != nullptr);
    // variance.c:98-:103.
    if (!subpelXQ3 && !subpelYQ3) {
        for (std::int32_t i = 0; i < height; i++) {
            std::memcpy(compPred, ref, width * sizeof(*compPred));
            compPred += width;
            ref += refStride;
        }
    } else if (!subpelYQ3) {
        // variance.c:104-:106.
        const std::int16_t* const kernel = getInterpFilterSubpelKernel(*filter, subpelXQ3 << 1);
        convolve8Horiz(ref, refStride, compPred, width, kernel, 16, nullptr, -1, width, height);
    } else if (!subpelXQ3) {
        // variance.c:107-:109.
        const std::int16_t* const kernel = getInterpFilterSubpelKernel(*filter, subpelYQ3 << 1);
        convolve8Vert(ref, refStride, compPred, width, nullptr, -1, kernel, 16, width, height);
    } else {
        // variance.c:110-:135. The temp buffer is the reference's
        // DECLARE_ALIGNED(16, uint8_t, temp[((MAX_SB_SIZE * 2 + 16) + 16) *
        // MAX_SB_SIZE]) shape.
        alignas(16) std::uint8_t temp[((maxSbSize * 2 + 16) + 16) * maxSbSize];
        const std::int16_t* const kernelX = getInterpFilterSubpelKernel(*filter, subpelXQ3 << 1);
        const std::int16_t* const kernelY = getInterpFilterSubpelKernel(*filter, subpelYQ3 << 1);
        const std::int32_t intermediateHeight = (((height - 1) * 8 + subpelYQ3) >> 3) + filter->taps;
        assert(intermediateHeight <= (maxSbSize * 2 + 16) + 16);
        convolve8Horiz(ref - refStride * ((filter->taps >> 1) - 1), refStride, temp, maxSbSize, kernelX, 16, nullptr,
                       -1, width, intermediateHeight);
        convolve8Vert(temp + maxSbSize * ((filter->taps >> 1) - 1), maxSbSize, compPred, width, nullptr, -1, kernelY,
                      16, width, height);
    }
}

namespace {

// definitions.h:457: ROUND_POWER_OF_TWO on signed int (arithmetic shift).
std::int32_t roundPowerOfTwoI32(std::int32_t value, std::int32_t n) {
    return (value + (((1 << n) >> 1))) >> n;
}

// definitions.h:700-:702.
std::uint8_t clipPixel(std::int32_t val) {
    return (std::uint8_t)((val > 255) ? 255 : (val < 0) ? 0 : val);
}

// definitions.h:672-:674.
std::int32_t clampI32(std::int32_t value, std::int32_t low, std::int32_t high) {
    return value < low ? low : (value > high ? high : value);
}

// convolve.c:46-:52 (despite the "highbd" name, this is the scalar product
// the 8-bit add-src vert pass consumes: its source is the uint16
// intermediate buffer).
std::int32_t highbdVertScalarProduct(const std::uint16_t* a, std::ptrdiff_t aStride, const std::int16_t* b) {
    std::int32_t sum = 0;
    for (std::int32_t k = 0; k < subpelTaps; ++k) {
        sum += a[k * aStride] * b[k];
    }
    return sum;
}

} // namespace

// convolve.h:72-:96.
ConvolveParams getConvParamsWiener(std::int32_t bd) {
    ConvolveParams convParams;
    convParams.round0 = kWienerRound0Bits;
    convParams.round1 = 2 * filterBits - convParams.round0;
    const std::int32_t intbufrange = bd + filterBits - convParams.round0 + 2;
    if (intbufrange > 16) {
        convParams.round0 += intbufrange - 16;
        convParams.round1 -= intbufrange - 16;
    }
    return convParams;
}

// convolve.c:106-:153.
void wienerConvolveAddSrc(const std::uint8_t* src, std::ptrdiff_t srcStride, std::uint8_t* dst,
                          std::ptrdiff_t dstStride, const std::int16_t* filterX, const std::int16_t* filterY,
                          std::int32_t w, std::int32_t h, const ConvolveParams* convParams) {
    const std::int32_t xStepQ4 = 16;
    const std::int32_t yStepQ4 = 16;
    const InterpKernel* const filtersX = getFilterBase(filterX);
    const std::int32_t x0Q4 = getFilterOffset(filterX, filtersX);

    const InterpKernel* const filtersY = getFilterBase(filterY);
    const std::int32_t y0Q4 = getFilterOffset(filterY, filtersY);

    std::uint16_t temp[wienerMaxExtSize * maxSbSize];
    const std::int32_t intermediateHeight = (((h - 1) * yStepQ4 + y0Q4) >> subpelBits) + subpelTaps - 1;

    // convolve.c:121-:126: the last temp row is zeroed so the vert pass's
    // 8th-tap read stays initialized (its weight is 0 for wiener filters).
    std::memset(temp + (intermediateHeight * maxSbSize), 0, maxSbSize);

    assert(w <= maxSbSize);
    assert(h <= maxSbSize);
    assert(yStepQ4 <= 32);
    assert(xStepQ4 <= 32);

    convolveAddSrcHorizHip(src - srcStride * (subpelTaps / 2 - 1), srcStride, temp, maxSbSize, filtersX, x0Q4,
                           xStepQ4, w, intermediateHeight, convParams->round0);
    convolveAddSrcVertHip(temp + maxSbSize * (subpelTaps / 2 - 1), maxSbSize, dst, dstStride, filtersY, y0Q4,
                          yStepQ4, w, h, convParams->round1);
}

// convolve.c:253-:268.
void convolveHoriz(const std::uint8_t* src, std::ptrdiff_t srcStride, std::uint8_t* dst, std::ptrdiff_t dstStride,
                   const InterpKernel* xFilters, std::int32_t x0Q4, std::int32_t xStepQ4, std::int32_t w,
                   std::int32_t h) {
    src -= subpelTaps / 2 - 1;
    for (std::int32_t y = 0; y < h; ++y) {
        std::int32_t xQ4 = x0Q4;
        for (std::int32_t x = 0; x < w; ++x) {
            const std::uint8_t* srcX = &src[xQ4 >> subpelBits];
            const std::int16_t* xFilter = xFilters[xQ4 & subpelMask];
            const std::int32_t sum = horzScalarProduct(srcX, xFilter);
            dst[x] = clipPixel(roundPowerOfTwoI32(sum, filterBits));
            xQ4 += xStepQ4;
        }
        src += srcStride;
        dst += dstStride;
    }
}

// convolve.c:288-:298.
void convolve8Horiz(const std::uint8_t* src, std::ptrdiff_t srcStride, std::uint8_t* dst, std::ptrdiff_t dstStride,
                    const std::int16_t* filterX, std::int32_t xStepQ4, const std::int16_t* filterY,
                    std::int32_t yStepQ4, std::int32_t w, std::int32_t h) {
    const InterpKernel* const filtersX = getFilterBase(filterX);
    const std::int32_t x0Q4 = getFilterOffset(filterX, filtersX);

    (void)filterY;
    (void)yStepQ4;

    convolveHoriz(src, srcStride, dst, dstStride, filtersX, x0Q4, xStepQ4, w, h);
}

namespace {

// convolve.c:245-:251.
std::int32_t vertScalarProduct(const std::uint8_t* a, std::ptrdiff_t aStride, const std::int16_t* b) {
    std::int32_t sum = 0;
    for (std::int32_t k = 0; k < subpelTaps; ++k) {
        sum += a[k * aStride] * b[k];
    }
    return sum;
}

} // namespace

// convolve.c:270-:286.
void convolveVert(const std::uint8_t* src, std::ptrdiff_t srcStride, std::uint8_t* dst, std::ptrdiff_t dstStride,
                  const InterpKernel* yFilters, std::int32_t y0Q4, std::int32_t yStepQ4, std::int32_t w,
                  std::int32_t h) {
    src -= srcStride * (subpelTaps / 2 - 1);

    for (std::int32_t x = 0; x < w; ++x) {
        std::int32_t yQ4 = y0Q4;
        for (std::int32_t y = 0; y < h; ++y) {
            const std::uint8_t* srcY = &src[(yQ4 >> subpelBits) * srcStride];
            const std::int16_t* yFilter = yFilters[yQ4 & subpelMask];
            const std::int32_t sum = vertScalarProduct(srcY, srcStride, yFilter);
            dst[y * dstStride] = clipPixel(roundPowerOfTwoI32(sum, filterBits));
            yQ4 += yStepQ4;
        }
        ++src;
        ++dst;
    }
}

// convolve.c:300-:310.
void convolve8Vert(const std::uint8_t* src, std::ptrdiff_t srcStride, std::uint8_t* dst, std::ptrdiff_t dstStride,
                   const std::int16_t* filterX, std::int32_t xStepQ4, const std::int16_t* filterY,
                   std::int32_t yStepQ4, std::int32_t w, std::int32_t h) {
    const InterpKernel* const filtersY = getFilterBase(filterY);
    const std::int32_t y0Q4 = getFilterOffset(filterY, filtersY);

    (void)filterX;
    (void)xStepQ4;

    convolveVert(src, srcStride, dst, dstStride, filtersY, y0Q4, yStepQ4, w, h);
}

// convolve.c:54-:57.
const InterpKernel* getFilterBase(const std::int16_t* filter) {
    return (const InterpKernel*)(((std::intptr_t)filter) & ~((std::intptr_t)0xFF));
}

// convolve.c:59-:61.
std::int32_t getFilterOffset(const std::int16_t* f, const InterpKernel* base) {
    return (std::int32_t)((const InterpKernel*)(std::intptr_t)f - base);
}

// convolve.c:30-:36.
std::int32_t horzScalarProduct(const std::uint8_t* a, const std::int16_t* b) {
    std::int32_t sum = 0;
    for (std::int32_t k = 0; k < subpelTaps; ++k) {
        sum += a[k] * b[k];
    }
    return sum;
}

// convolve.c:63-:82 (bd is fixed at 8; the highbd twin is parked).
void convolveAddSrcHorizHip(const std::uint8_t* src, std::ptrdiff_t srcStride, std::uint16_t* dst,
                            std::ptrdiff_t dstStride, const InterpKernel* xFilters, std::int32_t x0Q4,
                            std::int32_t xStepQ4, std::int32_t w, std::int32_t h, std::int32_t round0Bits) {
    const std::int32_t bd = 8;
    src -= subpelTaps / 2 - 1;
    for (std::int32_t y = 0; y < h; ++y) {
        std::int32_t xQ4 = x0Q4;
        for (std::int32_t x = 0; x < w; ++x) {
            const std::uint8_t* srcX = &src[xQ4 >> subpelBits];
            const std::int16_t* xFilter = xFilters[xQ4 & subpelMask];
            const std::int32_t rounding = ((std::int32_t)srcX[subpelTaps / 2 - 1] << filterBits) +
                (1 << (bd + filterBits - 1));
            const std::int32_t sum = horzScalarProduct(srcX, xFilter) + rounding;
            dst[x] = (std::uint16_t)clampI32(roundPowerOfTwoI32(sum, round0Bits), 0, wienerClampLimit(round0Bits, bd) - 1);
            xQ4 += xStepQ4;
        }
        src += srcStride;
        dst += dstStride;
    }
}

// convolve.c:84-:104 (bd is fixed at 8; the highbd twin is parked).
void convolveAddSrcVertHip(const std::uint16_t* src, std::ptrdiff_t srcStride, std::uint8_t* dst,
                           std::ptrdiff_t dstStride, const InterpKernel* yFilters, std::int32_t y0Q4,
                           std::int32_t yStepQ4, std::int32_t w, std::int32_t h, std::int32_t round1Bits) {
    const std::int32_t bd = 8;
    src -= srcStride * (subpelTaps / 2 - 1);

    for (std::int32_t x = 0; x < w; ++x) {
        std::int32_t yQ4 = y0Q4;
        for (std::int32_t y = 0; y < h; ++y) {
            const std::uint16_t* srcY = &src[(yQ4 >> subpelBits) * srcStride];
            const std::int16_t* yFilter = yFilters[yQ4 & subpelMask];
            const std::int32_t rounding = ((std::int32_t)srcY[(subpelTaps / 2 - 1) * srcStride] << filterBits) -
                (1 << (bd + round1Bits - 1));
            const std::int32_t sum = highbdVertScalarProduct(srcY, srcStride, yFilter) + rounding;
            dst[y * dstStride] = clipPixel(roundPowerOfTwoI32(sum, round1Bits));
            yQ4 += yStepQ4;
        }
        ++src;
        ++dst;
    }
}

} // namespace convolve