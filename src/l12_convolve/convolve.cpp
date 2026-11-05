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