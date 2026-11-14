#include <doctest.h>

#include <cstdlib>
#include <cstring>

#include "picmgmt.h"

// Scaffold smoke test (the documented first test: the test runner can run a
// trivial passing test). Not a ported-span assertion; the real coverage lives
// in the TEST_CASEs below, each cited to its reference span.
TEST_CASE("l18_picmgmt test runner is alive") {
    CHECK(1 + 1 == 2);
}

// 1:1 target: svt_aom_get_out_buffer_size (pcs.c:374-379): the raw 4:2:0
// frame size / 4, as the starting bitstream capacity (the entropy writer
// grows on demand).
TEST_CASE("getOutBufferSize returns raw 4:2:0 frame size over 4") {
    CHECK(l18_picmgmt::getOutBufferSize(1920, 1080) == 1920u * 1080u * 3u / 2u / 4u);
    CHECK(l18_picmgmt::getOutBufferSize(64, 64) == 64u * 64u * 3u / 2u / 4u);
    // The /4 is the whole expression's trailing division: 3/2 happens first
    // on the pixel count, so an odd-pixel product floors before the /4.
    CHECK(l18_picmgmt::getOutBufferSize(1, 1) == 1u * 1u * 3u / 2u / 4u);
    CHECK(l18_picmgmt::getOutBufferSize(0, 0) == 0u);
}

// 1:1 target: svt_aom_get_max_allocated_me_refs (pcs.c:88-93): the ME
// results allocation sizes. The cand count adds the list0*list1
// cross-product, the list0 unary-minus term and the +1 quirk iff
// list1 == 3. The list0 - 1 term is INTEGER arithmetic in the reference, so
// list0 == 0 yields -1 which truncates to 255 in the uint8_t out-param -
// pinned, not fixed.
TEST_CASE("getMaxAllocatedMeRefs sums both lists plus the cross and quirks") {
    std::uint8_t maxRef = 0xEE;
    std::uint8_t maxCand = 0xEE;
    l18_picmgmt::getMaxAllocatedMeRefs(1, 1, &maxRef, &maxCand);
    CHECK(maxRef == 2);
    CHECK(maxCand == 1 + 1 + (1 * 1) + (1 - 1) + 0); // 3

    l18_picmgmt::getMaxAllocatedMeRefs(3, 3, &maxRef, &maxCand);
    CHECK(maxRef == 6);
    CHECK(maxCand == 3 + 3 + (3 * 3) + (3 - 1) + 1); // 16, the list1==3 +1 quirk fires

    l18_picmgmt::getMaxAllocatedMeRefs(2, 3, &maxRef, &maxCand);
    CHECK(maxRef == 5);
    CHECK(maxCand == 2 + 3 + (2 * 3) + (2 - 1) + 1); // 12

    l18_picmgmt::getMaxAllocatedMeRefs(0, 0, &maxRef, &maxCand);
    CHECK(maxRef == 0);
    CHECK(maxCand == 255); // (0 + 0 + 0 - 1 + 0) truncated into uint8_t

    l18_picmgmt::getMaxAllocatedMeRefs(0, 3, &maxRef, &maxCand);
    CHECK(maxRef == 3);
    // 0 + 3 + (0*3) + (0-1) + 1 = 3: the -1 and the +1 cancel exactly.
    CHECK(maxCand == 3);
}

// 1:1 target: set_restoration_unit_size (pcs.c:29-41): the reference ignores
// width/height/sx/sy entirely ((void)-casts them) and writes the constant
// RESTORATION_UNITSIZE_MAX 256 (restoration.h:80) to plane 0, then s = 0
// shifts (a no-op) to planes 1 and 2.
TEST_CASE("setRestorationUnitSize writes 256 to all three planes") {
    l18_picmgmt::RestorationInfo rst[3] = {{-1}, {-2}, {-3}};
    l18_picmgmt::setRestorationUnitSize(1920, 1080, 1, 1, rst);
    CHECK(rst[0].restorationUnitSize == 256);
    CHECK(rst[1].restorationUnitSize == 256); // 256 >> 0
    CHECK(rst[2].restorationUnitSize == 256);
}

// 1:1 target: segmentation_map_ctor (pcs.c:69-77): num_elements =
// (pic_width >> MI_SIZE_LOG2) * (pic_height >> MI_SIZE_LOG2)
// (MI_SIZE_LOG2 = 2, definitions.h:353), map_size = num_elements, the data
// array calloc-zeroed. The dctor twin is segmentation_map_dctor
// (pcs.c:58-61) - EB_FREE_ARRAY(obj->data).
TEST_CASE("segmentationMapCtor sizes the mi-grid and zeroes the data") {
    l18_picmgmt::SegmentationNeighborMap seg;
    seg.mapSize = 0xDEADBEEFu;
    seg.data = reinterpret_cast<std::uint8_t*>(0x1); // poison: ctor must overwrite
    CHECK(l18_picmgmt::segmentationMapCtor(&seg, 64, 64) == 0); // EB_ErrorNone = 0
    CHECK(seg.mapSize == 16u * 16u);
    for (std::uint32_t i = 0; i < seg.mapSize; ++i) {
        CHECK(seg.data[i] == 0);
    }
    // Odd dims: floor-divide by 4 both axes.
    l18_picmgmt::SegmentationNeighborMap odd;
    CHECK(l18_picmgmt::segmentationMapCtor(&odd, 13, 7) == 0);
    CHECK(odd.mapSize == 3u * 1u);
    l18_picmgmt::segmentationMapDctor(&odd);
    l18_picmgmt::segmentationMapDctor(&seg);
}

// ---------------------------------------------------------------------------
// S5-S6: b64_geom_init (pcs.c:1488-1510), alloc_sb_geoms / free_sb_geoms /
// copy_sb_geoms (:1512-:1530), sb_geom_init (:1532-:1552). The scs argument
// is consumed only at scs->b64_size / scs->sb_size, so the carrier carries
// exactly those two fields.
// ---------------------------------------------------------------------------

// Independent arithmetic oracle: expected B64Geom for grid cell (h, v).
static l18_picmgmt::B64Geom expectedB64(std::uint16_t width, std::uint16_t height, std::uint8_t b64Size, int h,
                                        int v) {
    l18_picmgmt::B64Geom g;
    g.orgX = static_cast<std::uint16_t>(h * b64Size);
    g.orgY = static_cast<std::uint16_t>(v * b64Size);
    int w = width - g.orgX;
    int hh = height - g.orgY;
    g.width = static_cast<std::uint8_t>(w < b64Size ? w : b64Size);
    g.height = static_cast<std::uint8_t>(hh < b64Size ? hh : b64Size);
    g.isCompleteB64 = (g.width == b64Size && g.height == b64Size) ? 1 : 0;
    return g;
}

TEST_CASE("b64GeomInit lays out the b64 grid over ragged edges") {
    // (a) ragged right column and bottom row: 70x52 with b64 64.
    l18_picmgmt::Scs scs = {};
    scs.b64Size = 64;
    l18_picmgmt::B64Geom* geoms = nullptr;
    CHECK(l18_picmgmt::b64GeomInit(&scs, 70, 52, &geoms) == 0);
    CHECK(geoms != nullptr);
    // picture_b64_width = ceil(70/64) = 2, height = ceil(52/64) = 1: two cells.
    CHECK((70 + 63) / 64 == 2);
    for (int v = 0; v < 1; ++v) {
        for (int h = 0; h < 2; ++h) {
            const l18_picmgmt::B64Geom& g = geoms[v * 2 + h];
            const l18_picmgmt::B64Geom e = expectedB64(70, 52, 64, h, v);
            CHECK(g.orgX == e.orgX);
            CHECK(g.orgY == e.orgY);
            CHECK(g.width == e.width);
            CHECK(g.height == e.height);
            CHECK(g.isCompleteB64 == e.isCompleteB64);
        }
    }
    std::free(geoms);

    // (b) odd non-multiple-of-8 dims: 13x7 -> 1 cell, incomplete, w=13 h=7.
    geoms = nullptr;
    CHECK(l18_picmgmt::b64GeomInit(&scs, 13, 7, &geoms) == 0);
    CHECK(geoms[0].orgX == 0);
    CHECK(geoms[0].orgY == 0);
    CHECK(geoms[0].width == 13);
    CHECK(geoms[0].height == 7);
    CHECK(geoms[0].isCompleteB64 == 0);
    std::free(geoms);

    // (c) exact fit: 128x128 -> all four cells complete.
    geoms = nullptr;
    CHECK(l18_picmgmt::b64GeomInit(&scs, 128, 128, &geoms) == 0);
    for (int v = 0; v < 2; ++v) {
        for (int h = 0; h < 2; ++h) {
            CHECK(geoms[v * 2 + h].isCompleteB64 == 1);
            CHECK(geoms[v * 2 + h].orgX == 64 * h);
            CHECK(geoms[v * 2 + h].orgY == 64 * v);
            CHECK(geoms[v * 2 + h].width == 64);
            CHECK(geoms[v * 2 + h].height == 64);
        }
    }
    std::free(geoms);

    // (d) the old pointer is freed and replaced: re-init into the same slot.
    geoms = nullptr;
    CHECK(l18_picmgmt::b64GeomInit(&scs, 64, 64, &geoms) == 0);
    l18_picmgmt::B64Geom* first = geoms;
    // 130 wide -> ceil(130/64) = 3 cells; the LAST cell is the ragged one.
    CHECK(l18_picmgmt::b64GeomInit(&scs, 130, 64, &geoms) == 0);
    CHECK(geoms != first);
    CHECK(geoms[1].width == 64);
    CHECK(geoms[2].orgX == 128);
    CHECK(geoms[2].width == 2); // 130 - 128
    CHECK(geoms[2].isCompleteB64 == 0);
    std::free(geoms);
}

TEST_CASE("sbGeomInit and the sb-geom alloc/copy/free trio") {
    l18_picmgmt::Scs scs = {};
    scs.sbSize = 128;
    l18_picmgmt::SbGeom* geoms = nullptr;
    CHECK(l18_picmgmt::sbGeomInit(&scs, 300, 140, &geoms) == 0);
    CHECK(geoms != nullptr);
    // picture_sb_width = ceil(300/128) = 3, height = ceil(140/128) = 2.
    for (int i = 0; i < 6; ++i) {
        const int h = i % 3;
        const int v = i / 3;
        CHECK(geoms[i].orgX == 128 * h);
        CHECK(geoms[i].orgY == 128 * v);
        CHECK(geoms[i].width == (h == 2 ? 300 - 256 : 128));
        CHECK(geoms[i].height == (v == 1 ? 140 - 128 : 128));
    }
    // copy_sb_geoms into a second array: per-element equality.
    l18_picmgmt::SbGeom* dst = nullptr;
    CHECK(l18_picmgmt::allocSbGeoms(&dst, 3, 2) == 0);
    std::memset(dst, 0xEE, sizeof(l18_picmgmt::SbGeom) * 6);
    l18_picmgmt::copySbGeoms(dst, geoms, 3, 2);
    for (int i = 0; i < 6; ++i) {
        CHECK(dst[i].orgX == geoms[i].orgX);
        CHECK(dst[i].orgY == geoms[i].orgY);
        CHECK(dst[i].width == geoms[i].width);
        CHECK(dst[i].height == geoms[i].height);
    }
    std::free(dst);
    l18_picmgmt::freeSbGeoms(geoms);
    l18_picmgmt::freeSbGeoms(nullptr); // the reference guards the null (pcs.c:1520-:1524)

    // re-init into the same slot: the old array is freed first (pcs.c:1535).
    geoms = nullptr;
    CHECK(l18_picmgmt::sbGeomInit(&scs, 128, 128, &geoms) == 0);
    l18_picmgmt::SbGeom* first = geoms;
    CHECK(l18_picmgmt::sbGeomInit(&scs, 128, 128, &geoms) == 0);
    CHECK(geoms != first);
    l18_picmgmt::freeSbGeoms(geoms);
}
