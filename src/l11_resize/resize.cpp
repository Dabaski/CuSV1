#include <cassert>
#include <cstring>

#include "resize.h"

namespace resize {

namespace {

// FILTER_BITS definitions.h:435
constexpr int kFilterBits = 7;
// svt_aom_av1_down2_symeven_half_filter resize.c:32
const std::int16_t kDown2SymevenHalfFilter[] = {56, 12, -3, -1};
// av1_down2_symodd_half_filter resize.c:33
const std::int16_t kDown2SymoddHalfFilter[] = {64, 35, 0, -3};
// svt_aom_av1_filteredinterp_filters500/625/750/875 resize.c:36-137 (mechanically extracted),
// filteredinterp_filters1000 = svt_av1_resize_filter_normative super_res.c:20-47 (mechanically extracted).
static const std::int16_t kFilters500[64][8] = {
    {-3, 0, 35, 64, 35, 0, -3, 0}, {-3, 0, 34, 64, 36, 0, -3, 0}, {-3, -1, 34, 64, 36, 1, -3, 0},
    {-3, -1, 33, 64, 37, 1, -3, 0}, {-3, -1, 32, 64, 38, 1, -3, 0}, {-3, -1, 31, 64, 39, 1, -3, 0},
    {-3, -1, 31, 63, 39, 2, -3, 0}, {-2, -2, 30, 63, 40, 2, -3, 0}, {-2, -2, 29, 63, 41, 2, -3, 0},
    {-2, -2, 29, 63, 41, 3, -4, 0}, {-2, -2, 28, 63, 42, 3, -4, 0}, {-2, -2, 27, 63, 43, 3, -4, 0},
    {-2, -3, 27, 63, 43, 4, -4, 0}, {-2, -3, 26, 62, 44, 5, -4, 0}, {-2, -3, 25, 62, 45, 5, -4, 0},
    {-2, -3, 25, 62, 45, 5, -4, 0}, {-2, -3, 24, 62, 46, 5, -4, 0}, {-2, -3, 23, 61, 47, 6, -4, 0},
    {-2, -3, 23, 61, 47, 6, -4, 0}, {-2, -3, 22, 61, 48, 7, -4, -1}, {-2, -3, 21, 60, 49, 7, -4, 0},
    {-1, -4, 20, 60, 49, 8, -4, 0}, {-1, -4, 20, 60, 50, 8, -4, -1}, {-1, -4, 19, 59, 51, 9, -4, -1},
    {-1, -4, 19, 59, 51, 9, -4, -1}, {-1, -4, 18, 58, 52, 10, -4, -1}, {-1, -4, 17, 58, 52, 11, -4, -1},
    {-1, -4, 16, 58, 53, 11, -4, -1}, {-1, -4, 16, 57, 53, 12, -4, -1}, {-1, -4, 15, 57, 54, 12, -4, -1},
    {-1, -4, 15, 56, 54, 13, -4, -1}, {-1, -4, 14, 56, 55, 13, -4, -1}, {-1, -4, 14, 55, 55, 14, -4, -1},
    {-1, -4, 13, 55, 56, 14, -4, -1}, {-1, -4, 13, 54, 56, 15, -4, -1}, {-1, -4, 12, 54, 57, 15, -4, -1},
    {-1, -4, 12, 53, 57, 16, -4, -1}, {-1, -4, 11, 53, 58, 16, -4, -1}, {-1, -4, 11, 52, 58, 17, -4, -1},
    {-1, -4, 10, 52, 58, 18, -4, -1}, {-1, -4, 9, 51, 59, 19, -4, -1}, {-1, -4, 9, 51, 59, 19, -4, -1},
    {-1, -4, 8, 50, 60, 20, -4, -1}, {0, -4, 8, 49, 60, 20, -4, -1}, {0, -4, 7, 49, 60, 21, -3, -2},
    {-1, -4, 7, 48, 61, 22, -3, -2}, {0, -4, 6, 47, 61, 23, -3, -2}, {0, -4, 6, 47, 61, 23, -3, -2},
    {0, -4, 5, 46, 62, 24, -3, -2}, {0, -4, 5, 45, 62, 25, -3, -2}, {0, -4, 5, 45, 62, 25, -3, -2},
    {0, -4, 5, 44, 62, 26, -3, -2}, {0, -4, 4, 43, 63, 27, -3, -2}, {0, -4, 3, 43, 63, 27, -2, -2},
    {0, -4, 3, 42, 63, 28, -2, -2}, {0, -4, 3, 41, 63, 29, -2, -2}, {0, -3, 2, 41, 63, 29, -2, -2},
    {0, -3, 2, 40, 63, 30, -2, -2}, {0, -3, 2, 39, 63, 31, -1, -3}, {0, -3, 1, 39, 64, 31, -1, -3},
    {0, -3, 1, 38, 64, 32, -1, -3}, {0, -3, 1, 37, 64, 33, -1, -3}, {0, -3, 1, 36, 64, 34, -1, -3},
    {0, -3, 0, 36, 64, 34, 0, -3},
};

static const std::int16_t kFilters625[64][8] = {
    {-1, -8, 33, 80, 33, -8, -1, 0}, {-1, -8, 31, 80, 34, -8, -1, 1}, {-1, -8, 30, 80, 35, -8, -1, 1},
    {-1, -8, 29, 80, 36, -7, -2, 1}, {-1, -8, 28, 80, 37, -7, -2, 1}, {-1, -8, 27, 80, 38, -7, -2, 1},
    {0, -8, 26, 79, 39, -7, -2, 1}, {0, -8, 25, 79, 40, -7, -2, 1}, {0, -8, 24, 79, 41, -7, -2, 1},
    {0, -8, 23, 78, 42, -6, -2, 1}, {0, -8, 22, 78, 43, -6, -2, 1}, {0, -8, 21, 78, 44, -6, -2, 1},
    {0, -8, 20, 78, 45, -5, -3, 1}, {0, -8, 19, 77, 47, -5, -3, 1}, {0, -8, 18, 77, 48, -5, -3, 1},
    {0, -8, 17, 77, 49, -5, -3, 1}, {0, -8, 16, 76, 50, -4, -3, 1}, {0, -8, 15, 76, 51, -4, -3, 1},
    {0, -8, 15, 75, 52, -3, -4, 1}, {0, -7, 14, 74, 53, -3, -4, 1}, {0, -7, 13, 74, 54, -3, -4, 1},
    {0, -7, 12, 73, 55, -2, -4, 1}, {0, -7, 11, 73, 56, -2, -4, 1}, {0, -7, 10, 72, 57, -1, -4, 1},
    {1, -7, 10, 71, 58, -1, -5, 1}, {0, -7, 9, 71, 59, 0, -5, 1}, {1, -7, 8, 70, 60, 0, -5, 1},
    {1, -7, 7, 69, 61, 1, -5, 1}, {1, -6, 6, 68, 62, 1, -5, 1}, {0, -6, 6, 68, 62, 2, -5, 1},
    {1, -6, 5, 67, 63, 2, -5, 1}, {1, -6, 5, 66, 64, 3, -6, 1}, {1, -6, 4, 65, 65, 4, -6, 1},
    {1, -6, 3, 64, 66, 5, -6, 1}, {1, -5, 2, 63, 67, 5, -6, 1}, {1, -5, 2, 62, 68, 6, -6, 0},
    {1, -5, 1, 62, 68, 6, -6, 1}, {1, -5, 1, 61, 69, 7, -7, 1}, {1, -5, 0, 60, 70, 8, -7, 1},
    {1, -5, 0, 59, 71, 9, -7, 0}, {1, -5, -1, 58, 71, 10, -7, 1}, {1, -4, -1, 57, 72, 10, -7, 0},
    {1, -4, -2, 56, 73, 11, -7, 0}, {1, -4, -2, 55, 73, 12, -7, 0}, {1, -4, -3, 54, 74, 13, -7, 0},
    {1, -4, -3, 53, 74, 14, -7, 0}, {1, -4, -3, 52, 75, 15, -8, 0}, {1, -3, -4, 51, 76, 15, -8, 0},
    {1, -3, -4, 50, 76, 16, -8, 0}, {1, -3, -5, 49, 77, 17, -8, 0}, {1, -3, -5, 48, 77, 18, -8, 0},
    {1, -3, -5, 47, 77, 19, -8, 0}, {1, -3, -5, 45, 78, 20, -8, 0}, {1, -2, -6, 44, 78, 21, -8, 0},
    {1, -2, -6, 43, 78, 22, -8, 0}, {1, -2, -6, 42, 78, 23, -8, 0}, {1, -2, -7, 41, 79, 24, -8, 0},
    {1, -2, -7, 40, 79, 25, -8, 0}, {1, -2, -7, 39, 79, 26, -8, 0}, {1, -2, -7, 38, 80, 27, -8, -1},
    {1, -2, -7, 37, 80, 28, -8, -1}, {1, -2, -7, 36, 80, 29, -8, -1}, {1, -1, -8, 35, 80, 30, -8, -1},
    {1, -1, -8, 34, 80, 31, -8, -1},
};

static const std::int16_t kFilters750[64][8] = {
    {2, -11, 25, 96, 25, -11, 2, 0}, {2, -11, 24, 96, 26, -11, 2, 0}, {2, -11, 22, 96, 28, -11, 2, 0},
    {2, -10, 21, 96, 29, -12, 2, 0}, {2, -10, 19, 96, 31, -12, 2, 0}, {2, -10, 18, 95, 32, -11, 2, 0},
    {2, -10, 17, 95, 34, -12, 2, 0}, {2, -9, 15, 95, 35, -12, 2, 0}, {2, -9, 14, 94, 37, -12, 2, 0},
    {2, -9, 13, 94, 38, -12, 2, 0}, {2, -8, 12, 93, 40, -12, 1, 0}, {2, -8, 11, 93, 41, -12, 1, 0},
    {2, -8, 9, 92, 43, -12, 1, 1}, {2, -8, 8, 92, 44, -12, 1, 1}, {2, -7, 7, 91, 46, -12, 1, 0},
    {2, -7, 6, 90, 47, -12, 1, 1}, {2, -7, 5, 90, 49, -12, 1, 0}, {2, -6, 4, 89, 50, -12, 1, 0},
    {2, -6, 3, 88, 52, -12, 0, 1}, {2, -6, 2, 87, 54, -12, 0, 1}, {2, -5, 1, 86, 55, -12, 0, 1},
    {2, -5, 0, 85, 57, -12, 0, 1}, {2, -5, -1, 84, 58, -11, 0, 1}, {2, -5, -2, 83, 60, -11, 0, 1},
    {2, -4, -2, 82, 61, -11, -1, 1}, {1, -4, -3, 81, 63, -10, -1, 1}, {2, -4, -4, 80, 64, -10, -1, 1},
    {1, -4, -4, 79, 66, -10, -1, 1}, {1, -3, -5, 77, 67, -9, -1, 1}, {1, -3, -6, 76, 69, -9, -1, 1},
    {1, -3, -6, 75, 70, -8, -2, 1}, {1, -2, -7, 74, 71, -8, -2, 1}, {1, -2, -7, 72, 72, -7, -2, 1},
    {1, -2, -8, 71, 74, -7, -2, 1}, {1, -2, -8, 70, 75, -6, -3, 1}, {1, -1, -9, 69, 76, -6, -3, 1},
    {1, -1, -9, 67, 77, -5, -3, 1}, {1, -1, -10, 66, 79, -4, -4, 1}, {1, -1, -10, 64, 80, -4, -4, 2},
    {1, -1, -10, 63, 81, -3, -4, 1}, {1, -1, -11, 61, 82, -2, -4, 2}, {1, 0, -11, 60, 83, -2, -5, 2},
    {1, 0, -11, 58, 84, -1, -5, 2}, {1, 0, -12, 57, 85, 0, -5, 2}, {1, 0, -12, 55, 86, 1, -5, 2},
    {1, 0, -12, 54, 87, 2, -6, 2}, {1, 0, -12, 52, 88, 3, -6, 2}, {0, 1, -12, 50, 89, 4, -6, 2},
    {0, 1, -12, 49, 90, 5, -7, 2}, {1, 1, -12, 47, 90, 6, -7, 2}, {0, 1, -12, 46, 91, 7, -7, 2},
    {1, 1, -12, 44, 92, 8, -8, 2}, {1, 1, -12, 43, 92, 9, -8, 2}, {0, 1, -12, 41, 93, 11, -8, 2},
    {0, 1, -12, 40, 93, 12, -8, 2}, {0, 2, -12, 38, 94, 13, -9, 2}, {0, 2, -12, 37, 94, 14, -9, 2},
    {0, 2, -12, 35, 95, 15, -9, 2}, {0, 2, -12, 34, 95, 17, -10, 2}, {0, 2, -11, 32, 95, 18, -10, 2},
    {0, 2, -12, 31, 96, 19, -10, 2}, {0, 2, -12, 29, 96, 21, -10, 2}, {0, 2, -11, 28, 96, 22, -11, 2},
    {0, 2, -11, 26, 96, 24, -11, 2},
};

static const std::int16_t kFilters875[64][8] = {
    {3, -8, 13, 112, 13, -8, 3, 0}, {2, -7, 12, 112, 15, -8, 3, -1}, {3, -7, 10, 112, 17, -9, 3, -1},
    {2, -6, 8, 112, 19, -9, 3, -1}, {2, -6, 7, 112, 21, -10, 3, -1}, {2, -5, 6, 111, 22, -10, 3, -1},
    {2, -5, 4, 111, 24, -10, 3, -1}, {2, -4, 3, 110, 26, -11, 3, -1}, {2, -4, 1, 110, 28, -11, 3, -1},
    {2, -4, 0, 109, 30, -12, 4, -1}, {1, -3, -1, 108, 32, -12, 4, -1}, {1, -3, -2, 108, 34, -13, 4, -1},
    {1, -2, -4, 107, 36, -13, 4, -1}, {1, -2, -5, 106, 38, -13, 4, -1}, {1, -1, -6, 105, 40, -14, 4, -1},
    {1, -1, -7, 104, 42, -14, 4, -1}, {1, -1, -7, 103, 44, -15, 4, -1}, {1, 0, -8, 101, 46, -15, 4, -1},
    {1, 0, -9, 100, 48, -15, 4, -1}, {1, 0, -10, 99, 50, -15, 4, -1}, {1, 1, -11, 97, 53, -16, 4, -1},
    {0, 1, -11, 96, 55, -16, 4, -1}, {0, 1, -12, 95, 57, -16, 4, -1}, {0, 2, -13, 93, 59, -16, 4, -1},
    {0, 2, -13, 91, 61, -16, 4, -1}, {0, 2, -14, 90, 63, -16, 4, -1}, {0, 2, -14, 88, 65, -16, 4, -1},
    {0, 2, -15, 86, 67, -16, 4, 0}, {0, 3, -15, 84, 69, -17, 4, 0}, {0, 3, -16, 83, 71, -17, 4, 0},
    {0, 3, -16, 81, 73, -16, 3, 0}, {0, 3, -16, 79, 75, -16, 3, 0}, {0, 3, -16, 77, 77, -16, 3, 0},
    {0, 3, -16, 75, 79, -16, 3, 0}, {0, 3, -16, 73, 81, -16, 3, 0}, {0, 4, -17, 71, 83, -16, 3, 0},
    {0, 4, -17, 69, 84, -15, 3, 0}, {0, 4, -16, 67, 86, -15, 2, 0}, {-1, 4, -16, 65, 88, -14, 2, 0},
    {-1, 4, -16, 63, 90, -14, 2, 0}, {-1, 4, -16, 61, 91, -13, 2, 0}, {-1, 4, -16, 59, 93, -13, 2, 0},
    {-1, 4, -16, 57, 95, -12, 1, 0}, {-1, 4, -16, 55, 96, -11, 1, 0}, {-1, 4, -16, 53, 97, -11, 1, 1},
    {-1, 4, -15, 50, 99, -10, 0, 1}, {-1, 4, -15, 48, 100, -9, 0, 1}, {-1, 4, -15, 46, 101, -8, 0, 1},
    {-1, 4, -15, 44, 103, -7, -1, 1}, {-1, 4, -14, 42, 104, -7, -1, 1}, {-1, 4, -14, 40, 105, -6, -1, 1},
    {-1, 4, -13, 38, 106, -5, -2, 1}, {-1, 4, -13, 36, 107, -4, -2, 1}, {-1, 4, -13, 34, 108, -2, -3, 1},
    {-1, 4, -12, 32, 108, -1, -3, 1}, {-1, 4, -12, 30, 109, 0, -4, 2}, {-1, 3, -11, 28, 110, 1, -4, 2},
    {-1, 3, -11, 26, 110, 3, -4, 2}, {-1, 3, -10, 24, 111, 4, -5, 2}, {-1, 3, -10, 22, 111, 6, -5, 2},
    {-1, 3, -10, 21, 112, 7, -6, 2}, {-1, 3, -9, 19, 112, 8, -6, 2}, {-1, 3, -9, 17, 112, 10, -7, 3},
    {-1, 3, -8, 15, 112, 12, -7, 2},
};

static const std::int16_t kFilters1000[64][8] = {
    {0, 0, 0, 128, 0, 0, 0, 0}, {0, 0, -1, 128, 2, -1, 0, 0}, {0, 1, -3, 127, 4, -2, 1, 0},
    {0, 1, -4, 127, 6, -3, 1, 0}, {0, 2, -6, 126, 8, -3, 1, 0}, {0, 2, -7, 125, 11, -4, 1, 0},
    {-1, 2, -8, 125, 13, -5, 2, 0}, {-1, 3, -9, 124, 15, -6, 2, 0}, {-1, 3, -10, 123, 18, -6, 2, -1},
    {-1, 3, -11, 122, 20, -7, 3, -1}, {-1, 4, -12, 121, 22, -8, 3, -1}, {-1, 4, -13, 120, 25, -9, 3, -1},
    {-1, 4, -14, 118, 28, -9, 3, -1}, {-1, 4, -15, 117, 30, -10, 4, -1}, {-1, 5, -16, 116, 32, -11, 4, -1},
    {-1, 5, -16, 114, 35, -12, 4, -1}, {-1, 5, -17, 112, 38, -12, 4, -1}, {-1, 5, -18, 111, 40, -13, 5, -1},
    {-1, 5, -18, 109, 43, -14, 5, -1}, {-1, 6, -19, 107, 45, -14, 5, -1}, {-1, 6, -19, 105, 48, -15, 5, -1},
    {-1, 6, -19, 103, 51, -16, 5, -1}, {-1, 6, -20, 101, 53, -16, 6, -1}, {-1, 6, -20, 99, 56, -17, 6, -1},
    {-1, 6, -20, 97, 58, -17, 6, -1}, {-1, 6, -20, 95, 61, -18, 6, -1}, {-2, 7, -20, 93, 64, -18, 6, -2},
    {-2, 7, -20, 91, 66, -19, 6, -1}, {-2, 7, -20, 88, 69, -19, 6, -1}, {-2, 7, -20, 86, 71, -19, 6, -1},
    {-2, 7, -20, 84, 74, -20, 7, -2}, {-2, 7, -20, 81, 76, -20, 7, -1}, {-2, 7, -20, 79, 79, -20, 7, -2},
    {-1, 7, -20, 76, 81, -20, 7, -2}, {-2, 7, -20, 74, 84, -20, 7, -2}, {-1, 6, -19, 71, 86, -20, 7, -2},
    {-1, 6, -19, 69, 88, -20, 7, -2}, {-1, 6, -19, 66, 91, -20, 7, -2}, {-2, 6, -18, 64, 93, -20, 7, -2},
    {-1, 6, -18, 61, 95, -20, 6, -1}, {-1, 6, -17, 58, 97, -20, 6, -1}, {-1, 6, -17, 56, 99, -20, 6, -1},
    {-1, 6, -16, 53, 101, -20, 6, -1}, {-1, 5, -16, 51, 103, -19, 6, -1}, {-1, 5, -15, 48, 105, -19, 6, -1},
    {-1, 5, -14, 45, 107, -19, 6, -1}, {-1, 5, -14, 43, 109, -18, 5, -1}, {-1, 5, -13, 40, 111, -18, 5, -1},
    {-1, 4, -12, 38, 112, -17, 5, -1}, {-1, 4, -12, 35, 114, -16, 5, -1}, {-1, 4, -11, 32, 116, -16, 5, -1},
    {-1, 4, -10, 30, 117, -15, 4, -1}, {-1, 3, -9, 28, 118, -14, 4, -1}, {-1, 3, -9, 25, 120, -13, 4, -1},
    {-1, 3, -8, 22, 121, -12, 4, -1}, {-1, 3, -7, 20, 122, -11, 3, -1}, {-1, 2, -6, 18, 123, -10, 3, -1},
    {0, 2, -6, 15, 124, -9, 3, -1}, {0, 2, -5, 13, 125, -8, 2, -1}, {0, 1, -4, 11, 125, -7, 2, 0},
    {0, 1, -3, 8, 126, -6, 2, 0}, {0, 1, -3, 6, 127, -4, 1, 0}, {0, 1, -2, 4, 127, -3, 1, 0},
    {0, 0, -1, 2, 128, -1, 0, 0},
};

std::uint8_t clipPixel(int v) {
    return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// clip_pixel_highbd definitions.h:704-714 (switch bd 8/10/12)
std::uint16_t clipPixelHbd(int v, int bd) {
    const int max = (1 << bd) - 1;
    return static_cast<std::uint16_t>(v > max ? max : (v < 0 ? 0 : v));
}

} // namespace

// svt_av1_highbd_interpolate_core_c resize.c:489
void hbdInterpolateCore(const std::uint16_t* input, int inLength, std::uint16_t* output, int outLength, int bd,
                        const std::int16_t* interpFilters) {
    // RS_SCALE_SUBPEL_BITS 14 / RS_SCALE_EXTRA_BITS 8 / RS_SCALE_EXTRA_OFF 128
    // / RS_SUBPEL_BITS 6 / RS_SUBPEL_MASK 63 (super_res.h:20-25) - IDENTICAL
    // to the 8-bit core; the ONLY bd dependence is clipPixelHbd (:523/:535/
    // :546/:557). No filter-coefficient or rounding bd scaling exists in the
    // reference (brief correction).
    const std::int32_t delta  = (((std::uint32_t)inLength << 14) + outLength / 2) / outLength;
    const std::int32_t offset = inLength > outLength
        ? (((std::int32_t)(inLength - outLength) << 13) + outLength / 2) / outLength
        : -(((std::int32_t)(outLength - inLength) << 13) + outLength / 2) / outLength;
    std::uint16_t* optr = output;
    int           x, x1, x2, sum, k, intPel, subPel;
    std::int32_t  y;

    x = 0;
    y = offset + 128;
    while ((y >> 14) < (8 / 2 - 1)) {
        x++;
        y += delta;
    }
    x1 = x;
    x  = outLength - 1;
    y  = delta * x + offset + 128;
    while ((y >> 14) + (std::int32_t)(8 / 2) >= inLength) {
        x--;
        y -= delta;
    }
    x2 = x;
    if (x1 > x2) {
        for (x = 0, y = offset + 128; x < outLength; ++x, y += delta) {
            intPel = y >> 14;
            subPel = (y >> 8) & 63;
            const std::int16_t* filter = &interpFilters[subPel * 8];
            sum = 0;
            for (k = 0; k < 8; ++k) {
                const int pk = intPel - 8 / 2 + 1 + k;
                sum += filter[k] * input[pk > inLength - 1 ? inLength - 1 : (pk < 0 ? 0 : pk)];
            }
            *optr++ = clipPixelHbd((sum + 64) >> 7, bd);
        }
    } else {
        // Initial part.
        for (x = 0, y = offset + 128; x < x1; ++x, y += delta) {
            intPel = y >> 14;
            subPel = (y >> 8) & 63;
            const std::int16_t* filter = &interpFilters[subPel * 8];
            sum = 0;
            for (k = 0; k < 8; ++k) {
                const int pk = intPel - 8 / 2 + 1 + k;
                sum += filter[k] * input[pk < 0 ? 0 : pk];
            }
            *optr++ = clipPixelHbd((sum + 64) >> 7, bd);
        }
        // Middle part.
        for (; x <= x2; ++x, y += delta) {
            intPel = y >> 14;
            subPel = (y >> 8) & 63;
            const std::int16_t* filter = &interpFilters[subPel * 8];
            sum = 0;
            for (k = 0; k < 8; ++k) {
                sum += filter[k] * input[intPel - 8 / 2 + 1 + k];
            }
            *optr++ = clipPixelHbd((sum + 64) >> 7, bd);
        }
        // End part.
        for (; x < outLength; ++x, y += delta) {
            intPel = y >> 14;
            subPel = (y >> 8) & 63;
            const std::int16_t* filter = &interpFilters[subPel * 8];
            sum = 0;
            for (k = 0; k < 8; ++k) {
                const int pk = intPel - 8 / 2 + 1 + k;
                sum += filter[k] * input[pk > inLength - 1 ? inLength - 1 : pk];
            }
            *optr++ = clipPixelHbd((sum + 64) >> 7, bd);
        }
    }
}

// choose_interp_filter resize.c:272
const std::int16_t* chooseInterpFilter(int inLength, int outLength) {
    const int outLength16 = outLength * 16;
    if (outLength16 >= inLength * 16) {
        return &kFilters1000[0][0];
    } else if (outLength16 >= inLength * 13) {
        return &kFilters875[0][0];
    } else if (outLength16 >= inLength * 11) {
        return &kFilters750[0][0];
    } else if (outLength16 >= inLength * 9) {
        return &kFilters625[0][0];
    } else {
        return &kFilters500[0][0];
    }
}

// svt_av1_interpolate_core_c resize.c:287
void interpolateCore(const std::uint8_t* input, int inLength, std::uint8_t* output, int outLength,
                     const std::int16_t* interpFilters) {
    // RS_SCALE_SUBPEL_BITS 14 / RS_SCALE_EXTRA_BITS 8 / RS_SCALE_EXTRA_OFF 128
    // / RS_SUBPEL_BITS 6 / RS_SUBPEL_MASK 63 (super_res.h:20-25).
    const std::int32_t delta  = (((std::uint32_t)inLength << 14) + outLength / 2) / outLength;
    const std::int32_t offset = inLength > outLength
        ? (((std::int32_t)(inLength - outLength) << 13) + outLength / 2) / outLength
        : -(((std::int32_t)(outLength - inLength) << 13) + outLength / 2) / outLength;
    std::uint8_t* optr = output;
    int           x, x1, x2, sum, k, intPel, subPel;
    std::int32_t  y;

    x = 0;
    y = offset + 128;
    while ((y >> 14) < (8 / 2 - 1)) {
        x++;
        y += delta;
    }
    x1 = x;
    x  = outLength - 1;
    y  = delta * x + offset + 128;
    while ((y >> 14) + (std::int32_t)(8 / 2) >= inLength) {
        x--;
        y -= delta;
    }
    x2 = x;
    if (x1 > x2) {
        for (x = 0, y = offset + 128; x < outLength; ++x, y += delta) {
            intPel = y >> 14;
            subPel = (y >> 8) & 63;
            const std::int16_t* filter = &interpFilters[subPel * 8];
            sum = 0;
            for (k = 0; k < 8; ++k) {
                const int pk = intPel - 8 / 2 + 1 + k;
                sum += filter[k] * input[pk > inLength - 1 ? inLength - 1 : (pk < 0 ? 0 : pk)];
            }
            *optr++ = clipPixel((sum + 64) >> 7);
        }
    } else {
        // Initial part.
        for (x = 0, y = offset + 128; x < x1; ++x, y += delta) {
            intPel = y >> 14;
            subPel = (y >> 8) & 63;
            const std::int16_t* filter = &interpFilters[subPel * 8];
            sum = 0;
            for (k = 0; k < 8; ++k) {
                const int pk = intPel - 8 / 2 + 1 + k;
                sum += filter[k] * input[pk < 0 ? 0 : pk];
            }
            *optr++ = clipPixel((sum + 64) >> 7);
        }
        // Middle part.
        for (; x <= x2; ++x, y += delta) {
            intPel = y >> 14;
            subPel = (y >> 8) & 63;
            const std::int16_t* filter = &interpFilters[subPel * 8];
            sum = 0;
            for (k = 0; k < 8; ++k) {
                sum += filter[k] * input[intPel - 8 / 2 + 1 + k];
            }
            *optr++ = clipPixel((sum + 64) >> 7);
        }
        // End part.
        for (; x < outLength; ++x, y += delta) {
            intPel = y >> 14;
            subPel = (y >> 8) & 63;
            const std::int16_t* filter = &interpFilters[subPel * 8];
            sum = 0;
            for (k = 0; k < 8; ++k) {
                const int pk = intPel - 8 / 2 + 1 + k;
                sum += filter[k] * input[pk > inLength - 1 ? inLength - 1 : pk];
            }
            *optr++ = clipPixel((sum + 64) >> 7);
        }
    }
}

// interpolate resize.c:360
void interpolate(const std::uint8_t* input, int inLength, std::uint8_t* output, int outLength) {
    const std::int16_t* interpFilters = chooseInterpFilter(inLength, outLength);
    interpolateCore(input, inLength, output, outLength, interpFilters);
}

// resize_multistep resize.c:366
void resizeMultistep(const std::uint8_t* input, int length, std::uint8_t* output, int olength, std::uint8_t* otmp) {
    if (length == olength) {
        memcpy(output, input, sizeof(output[0]) * length);
        return;
    }
    const int steps = getDown2Steps(length, olength);

    if (steps > 0) {
        // only denom 16 will come here and steps equals to 1
        std::uint8_t* out            = NULL;
        int      filteredlength = length;

        assert(otmp != NULL);
        std::uint8_t* otmp2 = otmp + getDown2Length(length, 1);
        for (int s = 0; s < steps; ++s) {
            const int projFilteredlength = getDown2Length(filteredlength, 1);
            const std::uint8_t* in       = (s == 0 ? input : out);
            if (s == steps - 1 && projFilteredlength == olength) {
                out = output;
            } else {
                out = (s & 1) ? otmp2 : otmp;
            }
            if (filteredlength & 1) {
                down2Symodd(in, filteredlength, out);
            } else {
                down2Symeven(in, filteredlength, out);
            }
            filteredlength = projFilteredlength;
        }
        if (filteredlength != olength) {
            interpolate(out, filteredlength, output, olength);
        }
    } else {
        // denom 9~15 comes here
        interpolate(input, length, output, olength);
    }
}

// fill_arr_to_col resize.c:404
void fillArrToCol(std::uint8_t* img, int stride, int len, const std::uint8_t* arr) {
    int i;
    std::uint8_t* iptr = img;
    const std::uint8_t* aptr = arr;
    for (i = 0; i < len; ++i, iptr += stride) {
        *iptr = *aptr++;
    }
}

// fill_col_to_arr resize.c:413
void fillColToArr(std::uint8_t* img, int stride, int len, std::uint8_t* arr) {
    int i;
    std::uint8_t* iptr = img;
    std::uint8_t* aptr = arr;
    for (i = 0; i < len; ++i, iptr += stride) {
        *aptr++ = *iptr;
    }
}

// svt_av1_resize_plane_c resize.c:422
bool resizePlaneC(const std::uint8_t* input, int height, int width, int inStride, std::uint8_t* output, int height2,
                  int width2, int outStride) {
    int i;
    std::uint8_t *intbuf, *tmpbuf, *arrbuf, *arrbuf2;

    assert(width > 0);
    assert(height > 0);
    assert(width2 > 0);
    assert(height2 > 0);

    intbuf  = new std::uint8_t[width2 * height];
    tmpbuf  = new std::uint8_t[(width > height ? width : height)];
    arrbuf  = new std::uint8_t[height];
    arrbuf2 = new std::uint8_t[height2];
    for (i = 0; i < height; ++i) {
        resizeMultistep(input + inStride * i, width, intbuf + width2 * i, width2, tmpbuf);
    }

    for (i = 0; i < width2; ++i) {
        // column to array conversion
        fillColToArr(intbuf + i, width2, height, arrbuf);
        // resize 'arrbuf' from height to height2 and store the result to arrbuf2
        resizeMultistep(arrbuf, height, arrbuf2, height2, tmpbuf);
        // array to column conversion
        fillArrToCol(output + i, outStride, height2, arrbuf2);
    }

    delete[] intbuf;
    delete[] tmpbuf;
    delete[] arrbuf;
    delete[] arrbuf2;

    return true;
}

// svt_av1_resize_plane_horizontal resize.c:464
bool resizePlaneHorizontal(const std::uint8_t* input, int height, int width, int inStride, std::uint8_t* output,
                           int height2, int width2, int outStride) {
    int i;
    std::uint8_t* tmpbuf;

    assert(width > 0);
    assert(height > 0);
    assert(width2 > 0);
    assert(height2 == height);
    (void)height2;

    tmpbuf = new std::uint8_t[(width > height ? width : height)];
    for (i = 0; i < height; ++i) {
        resizeMultistep(input + inStride * i, width, output + outStride * i, width2, tmpbuf);
    }

    delete[] tmpbuf;

    return true;
}

// highbd_interpolate resize.c:562
void hbdInterpolate(const std::uint16_t* input, int inLength, std::uint16_t* output, int outLength, int bd) {
    hbdInterpolateCore(input, inLength, output, outLength, bd, chooseInterpFilter(inLength, outLength));
}

// svt_av1_highbd_down2_symeven_c resize.c:568
void hbdDown2Symeven(const std::uint16_t* input, int length, std::uint16_t* output, int bd) {
    // Actual filter len = 2 * filter_len_half. Same fixed coefficients and
    // FILTER_BITS shift as the 8-bit twin; the ONLY bd dependence is
    // clipPixelHbd (:586/:596/:605/:614).
    const std::int16_t* filterLenHalf = kDown2SymevenHalfFilter;
    const int halfLen = sizeof(kDown2SymevenHalfFilter) / sizeof(kDown2SymevenHalfFilter[0]);
    int i, j;
    std::uint16_t* optr = output;
    int l1 = halfLen;
    int l2 = (length - halfLen);
    l1 += (l1 & 1);
    l2 += (l2 & 1);
    if (l1 > l2) {
        // Short input length.
        for (i = 0; i < length; i += 2) {
            int sum = (1 << (kFilterBits - 1));
            for (j = 0; j < halfLen; ++j) {
                sum += (input[i - j < 0 ? 0 : i - j] + input[i + 1 + j > length - 1 ? length - 1 : i + 1 + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixelHbd(sum, bd);
        }
    } else {
        // Initial part.
        for (i = 0; i < l1; i += 2) {
            int sum = (1 << (kFilterBits - 1));
            for (j = 0; j < halfLen; ++j) {
                sum += (input[i - j < 0 ? 0 : i - j] + input[i + 1 + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixelHbd(sum, bd);
        }
        // Middle part.
        for (; i < l2; i += 2) {
            int sum = (1 << (kFilterBits - 1));
            for (j = 0; j < halfLen; ++j) {
                sum += (input[i - j] + input[i + 1 + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixelHbd(sum, bd);
        }
        // End part.
        for (; i < length; i += 2) {
            int sum = (1 << (kFilterBits - 1));
            for (j = 0; j < halfLen; ++j) {
                sum += (input[i - j] + input[i + 1 + j > length - 1 ? length - 1 : i + 1 + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixelHbd(sum, bd);
        }
    }
}

// highbd_down2_symodd resize.c:619
void hbdDown2Symodd(const std::uint16_t* input, int length, std::uint16_t* output, int bd) {
    // Actual filter len = 2 * filter_len_half - 1. Same fixed coefficients
    // and FILTER_BITS shift as the 8-bit twin; bd only in clipPixelHbd.
    const std::int16_t* filterLenHalf = kDown2SymoddHalfFilter;
    const int halfLen = sizeof(kDown2SymoddHalfFilter) / sizeof(kDown2SymoddHalfFilter[0]);
    int i, j;
    std::uint16_t* optr = output;
    int l1 = halfLen - 1;
    int l2 = (length - halfLen + 1);
    l1 += (l1 & 1);
    l2 += (l2 & 1);
    if (l1 > l2) {
        // Short input length.
        for (i = 0; i < length; i += 2) {
            int sum = (1 << (kFilterBits - 1)) + input[i] * filterLenHalf[0];
            for (j = 1; j < halfLen; ++j) {
                sum += (input[i - j < 0 ? 0 : i - j] + input[i + j >= length ? length - 1 : i + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixelHbd(sum, bd);
        }
    } else {
        // Initial part.
        for (i = 0; i < l1; i += 2) {
            int sum = (1 << (kFilterBits - 1)) + input[i] * filterLenHalf[0];
            for (j = 1; j < halfLen; ++j) {
                sum += (input[i - j < 0 ? 0 : i - j] + input[i + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixelHbd(sum, bd);
        }
        // Middle part.
        for (; i < l2; i += 2) {
            int sum = (1 << (kFilterBits - 1)) + input[i] * filterLenHalf[0];
            for (j = 1; j < halfLen; ++j) {
                sum += (input[i - j] + input[i + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixelHbd(sum, bd);
        }
        // End part.
        for (; i < length; i += 2) {
            int sum = (1 << (kFilterBits - 1)) + input[i] * filterLenHalf[0];
            for (j = 1; j < halfLen; ++j) {
                sum += (input[i - j] + input[i + j >= length ? length - 1 : i + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixelHbd(sum, bd);
        }
    }
}

// highbd_resize_multistep resize.c:670
void hbdResizeMultistep(const std::uint16_t* input, int length, std::uint16_t* output, int olength,
                        std::uint16_t* otmp, int bd) {
    if (length == olength) {
        memcpy(output, input, sizeof(output[0]) * length);
        return;
    }
    const int steps = getDown2Steps(length, olength);

    if (steps > 0) {
        // only denom 16 will come here and steps equals to 1
        std::uint16_t* out            = NULL;
        int      filteredlength = length;

        assert(otmp != NULL);
        std::uint16_t* otmp2 = otmp + getDown2Length(length, 1);
        for (int s = 0; s < steps; ++s) {
            const int projFilteredlength = getDown2Length(filteredlength, 1);
            const std::uint16_t* in       = (s == 0 ? input : out);
            if (s == steps - 1 && projFilteredlength == olength) {
                out = output;
            } else {
                out = (s & 1) ? otmp2 : otmp;
            }
            if (filteredlength & 1) {
                hbdDown2Symodd(in, filteredlength, out, bd);
            } else {
                hbdDown2Symeven(in, filteredlength, out, bd);
            }
            filteredlength = projFilteredlength;
        }
        if (filteredlength != olength) {
            hbdInterpolate(out, filteredlength, output, olength, bd);
        }
    } else {
        // denom 9~15 comes here
        hbdInterpolate(input, length, output, olength, bd);
    }
}

// highbd_fill_col_to_arr resize.c:707
void hbdFillColToArr(std::uint16_t* img, int stride, int len, std::uint16_t* arr) {
    int i;
    std::uint16_t* iptr = img;
    std::uint16_t* aptr = arr;
    for (i = 0; i < len; ++i, iptr += stride) {
        *aptr++ = *iptr;
    }
}

// highbd_fill_arr_to_col resize.c:716
void hbdFillArrToCol(std::uint16_t* img, int stride, int len, const std::uint16_t* arr) {
    int i;
    std::uint16_t* iptr = img;
    const std::uint16_t* aptr = arr;
    for (i = 0; i < len; ++i, iptr += stride) {
        *iptr = *aptr++;
    }
}

// svt_av1_highbd_resize_plane_c resize.c:725
bool hbdResizePlaneC(const std::uint16_t* input, int height, int width, int inStride, std::uint16_t* output,
                     int height2, int width2, int outStride, int bd) {
    int i;
    std::uint16_t *intbuf, *tmpbuf, *arrbuf, *arrbuf2;

    assert(width > 0);
    assert(height > 0);
    assert(width2 > 0);
    assert(height2 > 0);

    intbuf  = new std::uint16_t[width2 * height];
    tmpbuf  = new std::uint16_t[(width > height ? width : height)];
    arrbuf  = new std::uint16_t[height];
    arrbuf2 = new std::uint16_t[height2];
    for (i = 0; i < height; ++i) {
        hbdResizeMultistep(input + inStride * i, width, intbuf + width2 * i, width2, tmpbuf, bd);
    }

    for (i = 0; i < width2; ++i) {
        // column to array conversion
        hbdFillColToArr(intbuf + i, width2, height, arrbuf);
        // resize 'arrbuf' from height to height2 and store the result to arrbuf2
        hbdResizeMultistep(arrbuf, height, arrbuf2, height2, tmpbuf, bd);
        // array to column conversion
        hbdFillArrToCol(output + i, outStride, height2, arrbuf2);
    }

    delete[] intbuf;
    delete[] tmpbuf;
    delete[] arrbuf;
    delete[] arrbuf2;

    return true;
}

// svt_av1_highbd_resize_plane_horizontal resize.c:761
bool hbdResizePlaneHorizontal(const std::uint16_t* input, int height, int width, int inStride, std::uint16_t* output,
                              int height2, int width2, int outStride, int bd) {
    int i;
    std::uint16_t* tmpbuf;

    assert(width > 0);
    assert(height > 0);
    assert(width2 > 0);
    assert(height2 == height);
    (void)height2;

    // Deviation: the reference passes sizeof(uint16_t) * AOMMAX(width, height)
    // as the EB_MALLOC_ARRAY COUNT (:769), a harmless 2x over-allocation; the
    // port allocates the needed entry count.
    tmpbuf = new std::uint16_t[(width > height ? width : height)];
    for (i = 0; i < height; ++i) {
        hbdResizeMultistep(input + inStride * i, width, output + outStride * i, width2, tmpbuf, bd);
    }

    delete[] tmpbuf;

    return true;
}

// get_down2_length resize.c:147
int getDown2Length(int length, int steps) {
    for (int s = 0; s < steps; ++s) {
        length = (length + 1) >> 1;
    }
    return length;
}

// get_down2_steps resize.c:154
int getDown2Steps(int inLength, int outLength) {
    int steps = 0;
    int projInLength;
    while ((projInLength = getDown2Length(inLength, 1)) >= outLength) {
        ++steps;
        inLength = projInLength;
        if (inLength == 1) {
            // Special case: we break because any further calls to getDown2Length()
            // would be with length == 1, which return 1, resulting in an infinite
            // loop.
            break;
        }
    }
    return steps;
}

// svt_av1_down2_symeven_c resize.c:170
void down2Symeven(const std::uint8_t* input, int length, std::uint8_t* output) {
    // Actual filter len = 2 * filter_len_half.
    const std::int16_t* filterLenHalf = kDown2SymevenHalfFilter;
    const int halfLen = sizeof(kDown2SymevenHalfFilter) / sizeof(kDown2SymevenHalfFilter[0]);
    int i, j;
    std::uint8_t* optr = output;
    int l1 = halfLen;
    int l2 = (length - halfLen);
    l1 += (l1 & 1);
    l2 += (l2 & 1);
    if (l1 > l2) {
        // Short input length.
        for (i = 0; i < length; i += 2) {
            int sum = (1 << (kFilterBits - 1));
            for (j = 0; j < halfLen; ++j) {
                sum += (input[i - j < 0 ? 0 : i - j] + input[i + 1 + j > length - 1 ? length - 1 : i + 1 + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixel(sum);
        }
    } else {
        // Initial part.
        for (i = 0; i < l1; i += 2) {
            int sum = (1 << (kFilterBits - 1));
            for (j = 0; j < halfLen; ++j) {
                sum += (input[i - j < 0 ? 0 : i - j] + input[i + 1 + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixel(sum);
        }
        // Middle part.
        for (; i < l2; i += 2) {
            int sum = (1 << (kFilterBits - 1));
            for (j = 0; j < halfLen; ++j) {
                sum += (input[i - j] + input[i + 1 + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixel(sum);
        }
        // End part.
        for (; i < length; i += 2) {
            int sum = (1 << (kFilterBits - 1));
            for (j = 0; j < halfLen; ++j) {
                sum += (input[i - j] + input[i + 1 + j > length - 1 ? length - 1 : i + 1 + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixel(sum);
        }
    }
}

// down2_symodd resize.c:221
void down2Symodd(const std::uint8_t* input, int length, std::uint8_t* output) {
    // Actual filter len = 2 * filter_len_half - 1.
    const std::int16_t* filterLenHalf = kDown2SymoddHalfFilter;
    const int halfLen = sizeof(kDown2SymoddHalfFilter) / sizeof(kDown2SymoddHalfFilter[0]);
    int i, j;
    std::uint8_t* optr = output;
    int l1 = halfLen - 1;
    int l2 = (length - halfLen + 1);
    l1 += (l1 & 1);
    l2 += (l2 & 1);
    if (l1 > l2) {
        // Short input length.
        for (i = 0; i < length; i += 2) {
            int sum = (1 << (kFilterBits - 1)) + input[i] * filterLenHalf[0];
            for (j = 1; j < halfLen; ++j) {
                sum += (input[i - j < 0 ? 0 : i - j] + input[i + j >= length ? length - 1 : i + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixel(sum);
        }
    } else {
        // Initial part.
        for (i = 0; i < l1; i += 2) {
            int sum = (1 << (kFilterBits - 1)) + input[i] * filterLenHalf[0];
            for (j = 1; j < halfLen; ++j) {
                sum += (input[i - j < 0 ? 0 : i - j] + input[i + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixel(sum);
        }
        // Middle part.
        for (; i < l2; i += 2) {
            int sum = (1 << (kFilterBits - 1)) + input[i] * filterLenHalf[0];
            for (j = 1; j < halfLen; ++j) {
                sum += (input[i - j] + input[i + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixel(sum);
        }
        // End part.
        for (; i < length; i += 2) {
            int sum = (1 << (kFilterBits - 1)) + input[i] * filterLenHalf[0];
            for (j = 1; j < halfLen; ++j) {
                sum += (input[i - j] + input[i + j >= length ? length - 1 : i + j]) * filterLenHalf[j];
            }
            sum >>= kFilterBits;
            *optr++ = clipPixel(sum);
        }
    }
}

} // namespace resize
