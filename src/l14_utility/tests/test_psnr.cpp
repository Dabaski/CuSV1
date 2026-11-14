#include <doctest.h>
#include <psnr.h>

// Independent flat oracle: the straightforward SSE over w x h.
namespace {
std::uint32_t flatSse(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, int w, int h) {
    std::uint32_t sse = 0;
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            const int d = (int)a[i * aStride + j] - (int)b[i * bStride + j];
            sse += (std::uint32_t)(d * d);
        }
    }
    return sse;
}
}  // namespace

TEST_CASE("variance accumulates the strided SSE") {
    std::uint8_t a[5 * 3];
    std::uint8_t b[5 * 3];
    for (int i = 0; i < 15; ++i) {
        a[i] = (std::uint8_t)(i * 7);
        b[i] = (std::uint8_t)(i * 5 + 1);
    }
    const std::uint32_t expected = flatSse(a, 5, b, 5, 4, 3);
    CHECK(l14_utility::variance(a, 5, b, 5, 4, 3) == expected);
}

TEST_CASE("mse16x16 is the 16x16 variance") {
    std::uint8_t a[18 * 16];
    std::uint8_t b[18 * 16];
    for (int i = 0; i < 18 * 16; ++i) {
        a[i] = (std::uint8_t)(i * 13);
        b[i] = (std::uint8_t)(i * 11 + 3);
    }
    const std::uint32_t expected = flatSse(a, 18, b, 18, 16, 16);
    CHECK(l14_utility::mse16x16(a, 18, b, 18) == expected);
}

// The strip decomposition: dw column strip (full height) + dh row strip
// (cols 0..width-dw) + the 16x16 grid tile the image exactly, so
// get_sse == the flat oracle for every dim. The sweep hits the
// aligned / 1-off / prime / padded-stride boundaries.
TEST_CASE("getSse equals the flat oracle across the dim sweep") {
    const int astr = 48, bstr = 44;
    static std::uint8_t a[48 * 35], b[44 * 35];
    for (int i = 0; i < astr * 35; ++i) {
        a[i] = (std::uint8_t)(i * 17 + i / astr);
    }
    for (int i = 0; i < bstr * 35; ++i) {
        b[i] = (std::uint8_t)(i * 5 + i / bstr);
    }
    const int dims[][2] = {{40, 35}, {32, 32}, {16, 16}, {17, 33}, {1, 1}, {15, 16}, {16, 15},
                           {31, 31}, {33, 17}, {40, 16}, {40, 1},  {1, 35}, {39, 19}};
    for (const auto& d : dims) {
        CHECK(l14_utility::getSse(a, astr, b, bstr, d[0], d[1]) == (int64_t)flatSse(a, astr, b, bstr, d[0], d[1]));
    }
}
