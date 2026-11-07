#include <doctest.h>
#include <convolve.h>

#include <algorithm>

// IP1 slice 1 RED: the transcribed sub_pel_filters_4 table
// (inter_prediction.c:257-:272) diffed entry-by-entry against a second
// independent transcription in this test. TRANSCRIPTION CAVEAT (AN14
// OD_DIVU precedent): the port's table is a mechanical transcription, not
// a formula; this diff is the only guard against a typo.
TEST_CASE("subPelFilters4 matches the independent transcription, all 128 entries") {
    static const std::int16_t ref[convolve::subpelShifts][convolve::subpelTaps] = {
        {0, 0, 0, 128, 0, 0, 0, 0},   {0, 0, -4, 126, 8, -2, 0, 0},  {0, 0, -8, 122, 18, -4, 0, 0},
        {0, 0, -10, 116, 28, -6, 0, 0}, {0, 0, -12, 110, 38, -8, 0, 0}, {0, 0, -12, 102, 48, -10, 0, 0},
        {0, 0, -14, 94, 58, -10, 0, 0}, {0, 0, -12, 84, 66, -10, 0, 0}, {0, 0, -12, 76, 76, -12, 0, 0},
        {0, 0, -10, 66, 84, -12, 0, 0}, {0, 0, -10, 58, 94, -14, 0, 0}, {0, 0, -10, 48, 102, -12, 0, 0},
        {0, 0, -8, 38, 110, -12, 0, 0}, {0, 0, -6, 28, 116, -10, 0, 0}, {0, 0, -4, 18, 122, -8, 0, 0},
        {0, 0, -2, 8, 126, -4, 0, 0},
    };
    for (std::int32_t r = 0; r < convolve::subpelShifts; ++r)
        for (std::int32_t k = 0; k < convolve::subpelTaps; ++k)
            CHECK(convolve::subPelFilters4[r][k] == ref[r][k]);
}

// IP1 slice 2 RED: sub_pel_filters_8sharp (inter_prediction.c:1133-:1148),
// same diff pattern.
TEST_CASE("subPelFilters8sharp matches the independent transcription, all 128 entries") {
    static const std::int16_t ref[convolve::subpelShifts][convolve::subpelTaps] = {
        {0, 0, 0, 128, 0, 0, 0, 0},   {-2, 2, -6, 126, 8, -2, 2, 0},  {-2, 6, -12, 124, 16, -6, 4, -2},
        {-2, 8, -18, 120, 26, -10, 6, -2}, {-4, 10, -22, 116, 38, -14, 6, -2}, {-4, 10, -22, 108, 48, -18, 8, -2},
        {-4, 10, -24, 100, 60, -20, 8, -2}, {-4, 10, -24, 90, 70, -22, 10, -2}, {-4, 12, -24, 80, 80, -24, 12, -4},
        {-2, 10, -22, 70, 90, -24, 10, -4}, {-2, 8, -20, 60, 100, -24, 10, -4}, {-2, 8, -18, 48, 108, -22, 10, -4},
        {-2, 6, -14, 38, 116, -22, 10, -4}, {-2, 6, -10, 26, 120, -18, 8, -2}, {-2, 4, -6, 16, 124, -12, 6, -2},
        {0, 2, -2, 8, 126, -6, 2, -2},
    };
    for (std::int32_t r = 0; r < convolve::subpelShifts; ++r)
        for (std::int32_t k = 0; k < convolve::subpelTaps; ++k)
            CHECK(convolve::subPelFilters8sharp[r][k] == ref[r][k]);
}

// IP1 slice 3 RED: sub_pel_filters_8smooth (inter_prediction.c:1150-:1165),
// same diff pattern.
TEST_CASE("subPelFilters8smooth matches the independent transcription, all 128 entries") {
    static const std::int16_t ref[convolve::subpelShifts][convolve::subpelTaps] = {
        {0, 0, 0, 128, 0, 0, 0, 0}, {0, 2, 28, 62, 34, 2, 0, 0},   {0, 0, 26, 62, 36, 4, 0, 0},
        {0, 0, 22, 62, 40, 4, 0, 0}, {0, 0, 20, 60, 42, 6, 0, 0},  {0, 0, 18, 58, 44, 8, 0, 0},
        {0, 0, 16, 56, 46, 10, 0, 0}, {0, -2, 16, 54, 48, 12, 0, 0}, {0, -2, 14, 52, 52, 14, -2, 0},
        {0, 0, 12, 48, 54, 16, -2, 0}, {0, 0, 10, 46, 56, 16, 0, 0}, {0, 0, 8, 44, 58, 18, 0, 0},
        {0, 0, 6, 42, 60, 20, 0, 0}, {0, 0, 4, 40, 62, 22, 0, 0},  {0, 0, 4, 36, 62, 26, 0, 0},
        {0, 0, 2, 34, 62, 28, 2, 0},
    };
    for (std::int32_t r = 0; r < convolve::subpelShifts; ++r)
        for (std::int32_t k = 0; k < convolve::subpelTaps; ++k)
            CHECK(convolve::subPelFilters8smooth[r][k] == ref[r][k]);
}

// IP1 slice 4 RED: bilinear_filters (inter_prediction.c:1166-:1181), same
// diff pattern. The rows carry exactly two nonzero taps (positions 3, 4).
TEST_CASE("bilinearFilters matches the independent transcription, all 128 entries") {
    static const std::int16_t ref[convolve::subpelShifts][convolve::subpelTaps] = {
        {0, 0, 0, 128, 0, 0, 0, 0}, {0, 0, 0, 120, 8, 0, 0, 0},  {0, 0, 0, 112, 16, 0, 0, 0},
        {0, 0, 0, 104, 24, 0, 0, 0}, {0, 0, 0, 96, 32, 0, 0, 0},  {0, 0, 0, 88, 40, 0, 0, 0},
        {0, 0, 0, 80, 48, 0, 0, 0},  {0, 0, 0, 72, 56, 0, 0, 0},  {0, 0, 0, 64, 64, 0, 0, 0},
        {0, 0, 0, 56, 72, 0, 0, 0},  {0, 0, 0, 48, 80, 0, 0, 0},  {0, 0, 0, 40, 88, 0, 0, 0},
        {0, 0, 0, 32, 96, 0, 0, 0},  {0, 0, 0, 24, 104, 0, 0, 0}, {0, 0, 0, 16, 112, 0, 0, 0},
        {0, 0, 0, 8, 120, 0, 0, 0},
    };
    for (std::int32_t r = 0; r < convolve::subpelShifts; ++r)
        for (std::int32_t k = 0; k < convolve::subpelTaps; ++k)
            CHECK(convolve::bilinearFilters[r][k] == ref[r][k]);
}

// IP1 slice 5 RED: sub_pel_filters_4smooth (inter_prediction.c:1182-:1197),
// same diff pattern.
TEST_CASE("subPelFilters4smooth matches the independent transcription, all 128 entries") {
    static const std::int16_t ref[convolve::subpelShifts][convolve::subpelTaps] = {
        {0, 0, 0, 128, 0, 0, 0, 0}, {0, 0, 30, 62, 34, 2, 0, 0}, {0, 0, 26, 62, 36, 4, 0, 0},
        {0, 0, 22, 62, 40, 4, 0, 0}, {0, 0, 20, 60, 42, 6, 0, 0}, {0, 0, 18, 58, 44, 8, 0, 0},
        {0, 0, 16, 56, 46, 10, 0, 0}, {0, 0, 14, 54, 48, 12, 0, 0}, {0, 0, 12, 52, 52, 12, 0, 0},
        {0, 0, 12, 48, 54, 14, 0, 0}, {0, 0, 10, 46, 56, 16, 0, 0}, {0, 0, 8, 44, 58, 18, 0, 0},
        {0, 0, 6, 42, 60, 20, 0, 0}, {0, 0, 4, 40, 62, 22, 0, 0},  {0, 0, 4, 36, 62, 26, 0, 0},
        {0, 0, 2, 34, 62, 30, 0, 0},
    };
    for (std::int32_t r = 0; r < convolve::subpelShifts; ++r)
        for (std::int32_t k = 0; k < convolve::subpelTaps; ++k)
            CHECK(convolve::subPelFilters4smooth[r][k] == ref[r][k]);
}

// IP1 slice 6 RED: the av1_interp_filter_params_list (inter_prediction.h
// :80-:84) over SWITCHABLE_FILTERS + 1 = 4 entries: each carries a pointer
// to its table, taps = SUBPEL_TAPS = 8, subpel_shifts = SUBPEL_SHIFTS = 16
// and the ascending InterpFilter id. Also pins the enum order
// (definitions.h:818-:827) that SWITCHABLE_FILTERS == BILINEAR.
TEST_CASE("interpFilterParamsList carries four entries pointing at the four 8-tap tables") {
    static_assert(convolve::InterpFilter::SWITCHABLE_FILTERS == convolve::InterpFilter::BILINEAR,
                  "SWITCHABLE_FILTERS aliases BILINEAR");
    static_assert(static_cast<int>(convolve::InterpFilter::SWITCHABLE_FILTERS) + 1 ==
                      convolve::interpFilterParamsListSize,
                  "the list holds SWITCHABLE_FILTERS + 1 entries");
    const convolve::InterpFilterParams* list = convolve::interpFilterParamsList;
    CHECK(list[0].filterPtr == &convolve::subPelFilters8[0][0]);
    CHECK(list[1].filterPtr == &convolve::subPelFilters8smooth[0][0]);
    CHECK(list[2].filterPtr == &convolve::subPelFilters8sharp[0][0]);
    CHECK(list[3].filterPtr == &convolve::bilinearFilters[0][0]);
    for (int i = 0; i < convolve::interpFilterParamsListSize; ++i) {
        CHECK(list[i].taps == 8);
        CHECK(list[i].subpelShifts == 16);
        CHECK(static_cast<int>(list[i].interpFilter) == i);
    }
}

// IP1 slice 7 RED: av1_interp_4tap (inter_prediction.h:124-:126), the two
// 4-tap entries the w <= 4 gate returns. THE QUIRK (pinned): their taps
// field is SUBPEL_TAPS = 8, not 4 - the "4-tap" tables are zero-padded to
// the 8-tap row stride, and every consumer (the kernel getter's taps *
// subpel advance, the 2D path's taps/2 center offset) reads taps = 8.
TEST_CASE("interp4tap carries two entries pointing at the 4-tap tables with taps still 8") {
    const convolve::InterpFilterParams* tap4 = convolve::interp4tap;
    CHECK(tap4[0].filterPtr == &convolve::subPelFilters4[0][0]);
    CHECK(tap4[1].filterPtr == &convolve::subPelFilters4smooth[0][0]);
    CHECK(tap4[0].taps == 8);
    CHECK(tap4[1].taps == 8);
    CHECK(tap4[0].subpelShifts == 16);
    CHECK(tap4[1].subpelShifts == 16);
    CHECK(tap4[0].interpFilter == convolve::InterpFilter::EIGHTTAP_REGULAR);
    CHECK(tap4[1].interpFilter == convolve::InterpFilter::EIGHTTAP_SMOOTH);
}

// IP1 slice 8 RED: av1_extract_interp_filter (filter.h:60-:62) - the
// packed InterpFilters uint32 (y in the low 16 bits, x in the high 16,
// per av1_make_interp_filters filter.h:64-:68) splits by the x_filter
// selector: nonzero -> high half, zero -> low half.
TEST_CASE("extractInterpFilter splits the packed uint32 by the x_filter selector") {
    const std::uint32_t packed = (2u << 16) | 1u; // x = MULTITAP_SHARP, y = EIGHTTAP_SMOOTH
    CHECK(convolve::extractInterpFilter(packed, 1) == convolve::InterpFilter::MULTITAP_SHARP);
    CHECK(convolve::extractInterpFilter(packed, 0) == convolve::InterpFilter::EIGHTTAP_SMOOTH);
    CHECK(static_cast<int>(convolve::extractInterpFilter(0xFFFFFFFFu, 1)) == 0xFFFF);
    CHECK(static_cast<int>(convolve::extractInterpFilter(0xFFFFFFFFu, 0)) == 0xFFFF);
    CHECK(static_cast<int>(convolve::extractInterpFilter(0u, 1)) == 0);
    CHECK(static_cast<int>(convolve::extractInterpFilter(0u, 0)) == 0);
}

// IP1 slice 9 RED: av1_get_interp_filter_subpel_kernel (filter.h:77-:80)
// returns filter_ptr + taps * subpel. Range rule: the kernel getter itself
// admits every row 0..15 of a SUBPEL_SHIFTS table (the upsampled_pred
// callers pass subpel_q3 << 1 = 0..14, inside that range; the getter does
// NOT mask with SUBPEL_MASK - pinned by the unmasked pointer arithmetic).
TEST_CASE("getInterpFilterSubpelKernel steps one taps-row per phase over 0..15") {
    const convolve::InterpFilterParams p = convolve::interpFilterParamsList[0];
    for (std::int32_t s = 0; s <= 15; ++s)
        CHECK(convolve::getInterpFilterSubpelKernel(p, s) == p.filterPtr + p.taps * s);
    // The same arithmetic on a 4-tap-shaped entry (taps is still 8, per the
    // interp4tap quirk) and on the raw-list bilinear entry.
    const convolve::InterpFilterParams t4 = convolve::interp4tap[0];
    CHECK(convolve::getInterpFilterSubpelKernel(t4, 14) == t4.filterPtr + 8 * 14);
    const convolve::InterpFilterParams bl = convolve::interpFilterParamsList[3];
    CHECK(convolve::getInterpFilterSubpelKernel(bl, 7) == bl.filterPtr + 8 * 7);
}

// IP1 slice 10 RED: av1_get_interp_filter_params_with_block_size
// (inter_prediction.h:128-:137). Range rule, enumerated over the full
// filter set x the gate boundary {4, 5} and an 8: at w <= 4 REGULAR and
// SHARP route to the 4-tap REGULAR table, SMOOTH to the 4-tap SMOOTH
// table, and BILINEAR falls through to the 8-tap bilinear entry (the
// reference's own gate has no 4-tap bilinear branch); at w = 5 every
// filter routes to its own params_list entry.
TEST_CASE("getInterpFilterParamsWithBlockSize routes w <= 4 to the 4-tap tables except bilinear") {
    using convolve::InterpFilter;
    const auto w4 = [](InterpFilter f) { return convolve::getInterpFilterParamsWithBlockSize(f, 4); };
    CHECK(w4(InterpFilter::EIGHTTAP_REGULAR).filterPtr == &convolve::subPelFilters4[0][0]);
    CHECK(w4(InterpFilter::MULTITAP_SHARP).filterPtr == &convolve::subPelFilters4[0][0]);
    CHECK(w4(InterpFilter::EIGHTTAP_SMOOTH).filterPtr == &convolve::subPelFilters4smooth[0][0]);
    CHECK(w4(InterpFilter::BILINEAR).filterPtr == &convolve::bilinearFilters[0][0]);
    const auto w5 = [](InterpFilter f) { return convolve::getInterpFilterParamsWithBlockSize(f, 5); };
    for (int i = 0; i < convolve::interpFilterParamsListSize; ++i) {
        const auto f = static_cast<InterpFilter>(i);
        CHECK(w5(f).filterPtr == convolve::interpFilterParamsList[i].filterPtr);
        CHECK(w4(f).taps == 8);
        CHECK(w5(f).taps == 8);
    }
    CHECK(convolve::getInterpFilterParamsWithBlockSize(InterpFilter::EIGHTTAP_REGULAR, 8)
              .filterPtr == &convolve::subPelFilters8[0][0]);
}

// IP1 slice 11 RED: av1_get_convolve_filter_params (inter_prediction.h
// :139-:145): x is the HIGH half of the packed filters and gates on w, y
// the LOW half and gates on h.
TEST_CASE("getConvolveFilterParams extracts x from the high half and y from the low half") {
    using convolve::InterpFilter;
    const std::uint32_t packed =
        convolve::makeInterpFilters(InterpFilter::EIGHTTAP_SMOOTH, InterpFilter::MULTITAP_SHARP);
    convolve::InterpFilterParams px = {};
    convolve::InterpFilterParams py = {};
    convolve::getConvolveFilterParams(packed, &px, &py, 4, 8);
    CHECK(px.filterPtr == &convolve::subPelFilters4[0][0]); // x=SHARP, w=4 -> 4-tap regular
    CHECK(py.filterPtr == &convolve::subPelFilters8smooth[0][0]); // y=SMOOTH, h=8 -> 8-tap smooth
    convolve::getConvolveFilterParams(packed, &px, &py, 8, 4);
    CHECK(px.filterPtr == &convolve::subPelFilters8sharp[0][0]); // x=SHARP, w=8 -> 8-tap sharp
    CHECK(py.filterPtr == &convolve::subPelFilters4smooth[0][0]); // y=SMOOTH, h=4 -> 4-tap smooth
}

// IP1 slice 12 RED: av1_get_filter (variance.c:71-:85), the subpel_search
// selector used by upsampled_pred. Range rule, enumerated over the whole
// SUBPEL_SEARCH_TYPE set: USE_2_TAPS -> the bilinear params_list entry,
// USE_4_TAPS -> the 4-tap REGULAR entry, USE_8_TAPS -> the 8-tap REGULAR
// entry. USE_2_TAPS_ORIG (0) is outside the asserted domain
// (assert(subpel_search >= USE_2_TAPS)); the reference returns NULL after
// assert(0) - pinned as nullptr in Release.
TEST_CASE("getFilter routes the subpel_search set to bilinear / 4tap / 8tap") {
    CHECK(convolve::getFilter(static_cast<int>(convolve::SubpelSearchType::USE_2_TAPS)) ==
          &convolve::interpFilterParamsList[3]);
    CHECK(convolve::getFilter(static_cast<int>(convolve::SubpelSearchType::USE_4_TAPS)) == &convolve::interp4tap[0]);
    CHECK(convolve::getFilter(static_cast<int>(convolve::SubpelSearchType::USE_8_TAPS)) ==
          &convolve::interpFilterParamsList[0]);
    CHECK(convolve::getFilter(static_cast<int>(convolve::SubpelSearchType::USE_2_TAPS_ORIG)) == nullptr);
}

// IP1 slice 13 RED: upsampledPred identity path (variance.c:98-:103): at
// subpel (0,0) the block is a plain row-by-row copy from ref (stride
// ref_stride) into comp_pred (stride width) for EVERY subpel_search value
// - the filter selection is irrelevant when both phases are zero.
TEST_CASE("upsampledPred at subpel (0,0) copies the source rows, all subpel_search values") {
    constexpr std::int32_t w = 8;
    constexpr std::int32_t h = 4;
    constexpr std::int32_t refStride = 12;
    std::uint8_t ref[refStride * (h + 4)] = {};
    for (std::int32_t i = 0; i < refStride * (h + 4); ++i)
        ref[i] = (std::uint8_t)((i * 31 + 5) % 241);
    const std::uint8_t* r = ref + 2 * refStride + 2;
    std::uint8_t got[h * w] = {};
    for (int search = 1; search <= 3; ++search) {
        std::memset(got, 0xAA, sizeof(got));
        convolve::upsampledPred(nullptr, nullptr, 0, 0, nullptr, got, w, h, 0, 0, r, refStride, search);
        for (std::int32_t y = 0; y < h; ++y)
            for (std::int32_t x = 0; x < w; ++x)
                CHECK(got[y * w + x] == r[y * refStride + x]);
    }
}

// IP1 slice 14 RED: upsampledPred horiz-only path (variance.c:104-:106):
// !subpel_y_q3 routes to convolve8_horiz with the kernel at
// (subpel_x_q3 << 1), x_step 16, dst stride width. Composition equality:
// the path must equal the LANDED convolve8Horiz called directly with the
// same kernel and offsets.
TEST_CASE("upsampledPred horiz-only path equals convolve8Horiz with the same kernel") {
    constexpr std::int32_t w = 8;
    constexpr std::int32_t h = 4;
    constexpr std::int32_t refStride = 32;
    constexpr std::int32_t subpelXQ3 = 3;
    std::uint8_t ref[refStride * (h + 8)] = {};
    for (std::int32_t i = 0; i < refStride * (h + 8); ++i)
        ref[i] = (std::uint8_t)((i * 67 + 13) % 241);
    const std::uint8_t* r = ref + 4 * refStride + 8;
    std::uint8_t got[h * w] = {};
    convolve::upsampledPred(nullptr, nullptr, 0, 0, nullptr, got, w, h, subpelXQ3, 0, r, refStride, 3);
    std::uint8_t want[h * w] = {};
    const std::int16_t* kernel =
        convolve::getInterpFilterSubpelKernel(*convolve::getFilter(3), subpelXQ3 << 1);
    convolve::convolve8Horiz(r, refStride, want, w, kernel, 16, nullptr, -1, w, h);
    for (std::int32_t i = 0; i < h * w; ++i)
        CHECK(got[i] == want[i]);
}

// IP1 slice 15 RED: upsampledPred vert-only path (variance.c:107-:109):
// !subpel_x_q3 routes to convolve8_vert with the kernel at
// (subpel_y_q3 << 1), y_step 16, dst stride width. Composition equality
// against the landed convolve8Vert.
TEST_CASE("upsampledPred vert-only path equals convolve8Vert with the same kernel") {
    constexpr std::int32_t w = 8;
    constexpr std::int32_t h = 4;
    constexpr std::int32_t refStride = 32;
    constexpr std::int32_t subpelYQ3 = 5;
    std::uint8_t ref[refStride * (h + 16)] = {};
    for (std::int32_t i = 0; i < refStride * (h + 16); ++i)
        ref[i] = (std::uint8_t)((i * 83 + 29) % 241);
    const std::uint8_t* r = ref + 8 * refStride + 8;
    std::uint8_t got[h * w] = {};
    convolve::upsampledPred(nullptr, nullptr, 0, 0, nullptr, got, w, h, 0, subpelYQ3, r, refStride, 2);
    std::uint8_t want[h * w] = {};
    const std::int16_t* kernel =
        convolve::getInterpFilterSubpelKernel(*convolve::getFilter(2), subpelYQ3 << 1);
    convolve::convolve8Vert(r, refStride, want, w, nullptr, -1, kernel, 16, w, h);
    for (std::int32_t i = 0; i < h * w; ++i)
        CHECK(got[i] == want[i]);
}

// IP1 slice 16 RED: the 2-D path (variance.c:110-:135). Two pins in one
// walk over an enumerated grid (height, subpel_y_q3):
// (a) intermediate_height = (((height - 1) * 8 + subpel_y_q3) >> 3) +
//     filter->taps, HAND-computed per grid point (not derived from the
//     port);
// (b) composition equality: the path must equal manual staging with the
//     LANDED convolve8Horiz into a maxSbSize-strided temp at ref -
//     ref_stride * (taps/2 - 1), then convolve8Vert from temp +
//     maxSbSize * (taps/2 - 1) with the same kernel_y. Any off-by-one in
//     the formula, the temp stride, or the center-tap offset corrupts the
//     non-flat fixture.
TEST_CASE("upsampledPred 2D path: intermediate_height grid + staged composition equality") {
    constexpr std::int32_t w = 8;
    constexpr std::int32_t refStride = 64;
    constexpr std::int32_t taps = 8;
    std::uint8_t ref[refStride * 64] = {};
    for (std::int32_t i = 0; i < refStride * 64; ++i)
        ref[i] = (std::uint8_t)((i * 97 + 41) % 241);
    const std::uint8_t* r = ref + 8 * refStride + 8;
    const convolve::InterpFilterParams* filter = convolve::getFilter(3);
    const std::int16_t* kernelX = nullptr;
    const std::int16_t* kernelY = nullptr;
    std::uint8_t want[16 * w] = {};
    // Hand-computed intermediate_height per grid point, from
    // (((h-1)*8 + s) >> 3) + 8: h=1 s=1 -> (1>>3)+8 = 8; h=1 s=7 ->
    // (7>>3)+8 = 8; h=4 s=1 -> (25>>3)+8 = 11; h=4 s=7 -> (31>>3)+8 = 11;
    // h=16 s=1 -> (121>>3)+8 = 23; h=16 s=7 -> (127>>3)+8 = 23.
    const std::int32_t handIh[3][2] = {{8, 8}, {11, 11}, {23, 23}};
    const std::int32_t heights[3] = {1, 4, 16};
    const std::int32_t subpelYs[2] = {1, 7};
    for (std::int32_t gi = 0; gi < 3; ++gi) {
        const std::int32_t h = heights[gi];
        for (std::int32_t sj = 0; sj < 2; ++sj) {
            const std::int32_t subpelYQ3 = subpelYs[sj];
            const std::int32_t subpelXQ3 = 2;
            // (a) the port-facing formula must reproduce the hand value.
            const std::int32_t ih = (((h - 1) * 8 + subpelYQ3) >> 3) + taps;
            CHECK(ih == handIh[gi][sj]);
            // (b) manual staging per variance.c:110-:135 with the landed
            // primitives.
            kernelX = convolve::getInterpFilterSubpelKernel(*filter, subpelXQ3 << 1);
            kernelY = convolve::getInterpFilterSubpelKernel(*filter, subpelYQ3 << 1);
            std::uint8_t temp[convolve::maxSbSize * (32 + taps)] = {};
            convolve::convolve8Horiz(r - refStride * ((taps >> 1) - 1), refStride, temp, convolve::maxSbSize,
                                     kernelX, 16, nullptr, -1, w, ih);
            convolve::convolve8Vert(temp + convolve::maxSbSize * ((taps >> 1) - 1), convolve::maxSbSize, want, w,
                                    nullptr, -1, kernelY, 16, w, h);
            std::uint8_t got[16 * w] = {};
            convolve::upsampledPred(nullptr, nullptr, 0, 0, nullptr, got, w, h, subpelXQ3, subpelYQ3, r, refStride,
                                    3);
            for (std::int32_t i = 0; i < h * w; ++i)
                CHECK(got[i] == want[i]);
        }
    }
}

// RANGE AUDIT (R-series rule): the per-axis subpel_q3 range 0..7 was only
// spot-checked above (x = 3 and 2, y = 5, 1 and 7). This sweep pins EVERY
// nonzero phase on both single-axis paths against the direct convolve8
// composition; phase 0 on an axis is the identity/other-axis branch and is
// covered by the identity test. Expected green; a failure is a found bug.
TEST_CASE("upsampledPred single-axis paths over the full subpel_q3 range 1..7") {
    constexpr std::int32_t w = 8;
    constexpr std::int32_t h = 4;
    constexpr std::int32_t refStride = 32;
    std::uint8_t ref[refStride * 64] = {};
    for (std::int32_t i = 0; i < refStride * 64; ++i)
        ref[i] = (std::uint8_t)((i * 71 + 17) % 241);
    const std::uint8_t* r = ref + 8 * refStride + 8;
    for (std::int32_t s = 1; s <= 7; ++s) {
        std::uint8_t gotH[h * w] = {};
        std::uint8_t wantH[h * w] = {};
        convolve::upsampledPred(nullptr, nullptr, 0, 0, nullptr, gotH, w, h, s, 0, r, refStride, 3);
        const std::int16_t* kH = convolve::getInterpFilterSubpelKernel(*convolve::getFilter(3), s << 1);
        convolve::convolve8Horiz(r, refStride, wantH, w, kH, 16, nullptr, -1, w, h);
        for (std::int32_t i = 0; i < h * w; ++i)
            CHECK(gotH[i] == wantH[i]);
        std::uint8_t gotV[h * w] = {};
        std::uint8_t wantV[h * w] = {};
        convolve::upsampledPred(nullptr, nullptr, 0, 0, nullptr, gotV, w, h, 0, s, r, refStride, 3);
        const std::int16_t* kV = convolve::getInterpFilterSubpelKernel(*convolve::getFilter(3), s << 1);
        convolve::convolve8Vert(r, refStride, wantV, w, nullptr, -1, kV, 16, w, h);
        for (std::int32_t i = 0; i < h * w; ++i)
            CHECK(gotV[i] == wantV[i]);
    }
}
