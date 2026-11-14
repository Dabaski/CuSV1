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

// ---------------------------------------------------------------------------
// C3: svt_aom_derive_input_resolution (sequence_control_set.c:120-139) and
// copy_sequence_control_set (:163-:228).
// ---------------------------------------------------------------------------

// 1:1 target: derive_input_resolution. The thresholds are the test's
// oracle: INPUT_SIZE_240p_TH 0x28500, 360p 0x4CE00, 480p 0xA1400,
// 720p 0x16DA00, 1080p 0x535200, 4K 0x140A000 (definitions.h:1819-1824).
// Each band is < TH (strict), so TH-1 stays in the band and TH moves up.
static l18_picmgmt::ResolutionRange expectedResolution(std::uint32_t inputSize) {
    if (inputSize < 0x28500u) {
        return l18_picmgmt::INPUT_SIZE_240p_RANGE;
    }
    if (inputSize < 0x4CE00u) {
        return l18_picmgmt::INPUT_SIZE_360p_RANGE;
    }
    if (inputSize < 0xA1400u) {
        return l18_picmgmt::INPUT_SIZE_480p_RANGE;
    }
    if (inputSize < 0x16DA00u) {
        return l18_picmgmt::INPUT_SIZE_720p_RANGE;
    }
    if (inputSize < 0x535200u) {
        return l18_picmgmt::INPUT_SIZE_1080p_RANGE;
    }
    if (inputSize < 0x140A000u) {
        return l18_picmgmt::INPUT_SIZE_4K_RANGE;
    }
    return l18_picmgmt::INPUT_SIZE_8K_RANGE;
}

TEST_CASE("deriveInputResolution enumerates every band boundary") {
    // The exact boundary bands: TH-1 stays, TH moves up.
    const std::uint32_t bands[] = {0x28500u, 0x4CE00u, 0xA1400u, 0x16DA00u, 0x535200u, 0x140A000u};
    for (std::uint32_t th : bands) {
        l18_picmgmt::ResolutionRange below = static_cast<l18_picmgmt::ResolutionRange>(-1);
        l18_picmgmt::ResolutionRange at = static_cast<l18_picmgmt::ResolutionRange>(-1);
        l18_picmgmt::deriveInputResolution(&below, th - 1);
        l18_picmgmt::deriveInputResolution(&at, th);
        CHECK(below == expectedResolution(th - 1));
        CHECK(at == expectedResolution(th));
        CHECK(at != below); // the strict-< boundary really moves the band
    }
    // Full-domain sweep over 0..40M in coarse steps plus 0 and the max.
    for (std::uint32_t s = 0; s <= 40000000u; s += 9973u) {
        l18_picmgmt::ResolutionRange got = static_cast<l18_picmgmt::ResolutionRange>(-1);
        l18_picmgmt::deriveInputResolution(&got, s);
        CHECK(got == expectedResolution(s));
    }
    l18_picmgmt::ResolutionRange got = static_cast<l18_picmgmt::ResolutionRange>(-1);
    l18_picmgmt::deriveInputResolution(&got, 0u);
    CHECK(got == l18_picmgmt::INPUT_SIZE_240p_RANGE);
    l18_picmgmt::deriveInputResolution(&got, 0xFFFFFFFFu);
    CHECK(got == l18_picmgmt::INPUT_SIZE_8K_RANGE);
}

// 1:1 target: copy_sequence_control_set (sequence_control_set.c:163-:228):
// free the dst sb/b64 arrays, whole-copy the non-pointer members, then
// deep-copy b64_geom (malloc+memcpy), sb_geom (alloc_sb_geoms +
// copy_sb_geoms), and the six frame_scale_evts / sframe_posi arrays (only
// when the src pointer is non-null). Cited: :164-:169 the frees, :171
// *dst = *src, :173-:174 the b64 copy, :177-:178 the sb geoms, :180-:200
// the evts trio, :201-:223 the sframe trio.
TEST_CASE("copySequenceControlSet deep-copies arrays and leaves src untouched") {
    using namespace l18_picmgmt;
    Scs src = {};
    src.b64Size = 64;
    src.sbSize = 128;
    // The counts must agree with the grids the real inits produce for
    // 130x64: ceil(130/64) = 3 cells across, 1 row.
    src.b64TotalCount = 3;
    src.pictureWidthInSb = 3;
    src.pictureHeightInSb = 1;
    src.frameScaleEvts.evtNum = 3;
    src.frameScaleEvts.startFrameNums = static_cast<std::int64_t*>(std::malloc(sizeof(std::int64_t) * 3));
    src.frameScaleEvts.resizeKfDenoms = static_cast<std::uint32_t*>(std::malloc(sizeof(std::uint32_t) * 3));
    src.frameScaleEvts.resizeDenoms = static_cast<std::uint32_t*>(std::malloc(sizeof(std::uint32_t) * 3));
    src.frameScaleEvts.startFrameNums[0] = 11;
    src.frameScaleEvts.startFrameNums[1] = 22;
    src.frameScaleEvts.startFrameNums[2] = 33;
    src.frameScaleEvts.resizeKfDenoms[0] = 160;
    src.frameScaleEvts.resizeKfDenoms[1] = 180;
    src.frameScaleEvts.resizeKfDenoms[2] = 200;
    src.frameScaleEvts.resizeDenoms[0] = 1000;
    src.frameScaleEvts.resizeDenoms[1] = 1001;
    src.frameScaleEvts.resizeDenoms[2] = 1002;
    src.sframePosi.sframeNum = 2;
    src.sframePosi.sframePosis = static_cast<std::uint64_t*>(std::malloc(sizeof(std::uint64_t) * 2));
    src.sframePosi.sframePosis[0] = 7;
    src.sframePosi.sframePosis[1] = 8;
    src.sframePosi.sframeQpNum = 2;
    src.sframePosi.sframeQps = static_cast<std::uint8_t*>(std::malloc(2));
    src.sframePosi.sframeQps[0] = 40;
    src.sframePosi.sframeQps[1] = 50;
    src.sframePosi.sframeQpOffsets = static_cast<std::int8_t*>(std::malloc(2));
    src.sframePosi.sframeQpOffsets[0] = 5;
    src.sframePosi.sframeQpOffsets[1] = 6;
    // src geometry arrays, filled with the real inits.
    CHECK(b64GeomInit(&src, 130, 64, &src.b64Geom) == 0);
    CHECK(sbGeomInit(&src, 130, 64, &src.sbGeom) == 0);

    // dst: poisoned scalars, non-null old geometry arrays (the free-before
    // path at :164-:169 must run), poisoned evts/sframe pointers.
    Scs dst = {};
    dst.b64TotalCount = 0xEEEE;
    dst.pictureWidthInSb = 0xEEEE;
    dst.pictureHeightInSb = 0xEEEE;
    dst.frameScaleEvts.evtNum = 0xEEEE;
    dst.sframePosi.sframeNum = 0xEEEE;
    dst.sframePosi.sframeQpNum = 0xEEEE;
    dst.sbGeom = nullptr;
    CHECK(allocSbGeoms(&dst.sbGeom, 5, 5) == 0);
    dst.b64Geom = static_cast<B64Geom*>(std::malloc(sizeof(B64Geom) * 3));

    CHECK(copySequenceControlSet(&dst, &src) == 0);

    // The scalars arrived (the *dst = *src line).
    CHECK(dst.b64TotalCount == 3);
    CHECK(dst.pictureWidthInSb == 3);
    CHECK(dst.pictureHeightInSb == 1);
    CHECK(dst.frameScaleEvts.evtNum == 3);
    CHECK(dst.sframePosi.sframeNum == 2);
    CHECK(dst.sframePosi.sframeQpNum == 2);
    // The geometry arrays are FRESH allocations (not src's pointers).
    CHECK(dst.b64Geom != src.b64Geom);
    CHECK(dst.sbGeom != src.sbGeom);
    for (int i = 0; i < 3; ++i) { // ceil(130/64)=3 x ceil(64/64)=1
        CHECK(dst.b64Geom[i].orgX == src.b64Geom[i].orgX);
        CHECK(dst.b64Geom[i].width == src.b64Geom[i].width);
        CHECK(dst.sbGeom[i].orgX == src.sbGeom[i].orgX);
        CHECK(dst.sbGeom[i].width == src.sbGeom[i].width);
    }
    // The six arrays are deep copies, independent of src.
    CHECK(dst.frameScaleEvts.startFrameNums != src.frameScaleEvts.startFrameNums);
    CHECK(dst.frameScaleEvts.startFrameNums[0] == 11);
    CHECK(dst.frameScaleEvts.startFrameNums[2] == 33);
    CHECK(dst.frameScaleEvts.resizeKfDenoms[1] == 180);
    CHECK(dst.frameScaleEvts.resizeDenoms[2] == 1002);
    CHECK(dst.sframePosi.sframePosis != src.sframePosi.sframePosis);
    CHECK(dst.sframePosi.sframePosis[1] == 8);
    CHECK(dst.sframePosi.sframeQps[1] == 50);
    CHECK(dst.sframePosi.sframeQpOffsets[0] == 5);
    dst.frameScaleEvts.startFrameNums[0] = 999;
    dst.sframePosi.sframeQps[0] = 99;
    dst.b64Geom[0].width = 77;
    dst.sbGeom[0].orgY = 77;
    CHECK(src.frameScaleEvts.startFrameNums[0] == 11);
    CHECK(src.sframePosi.sframeQps[0] == 40);
    CHECK(src.b64Geom[0].width == 64);
    CHECK(src.sbGeom[0].orgY == 0);

    // The all-null src event/sframe pointers variant: dst inherits NULLs.
    // The geometry arrays themselves must exist: the reference's memcpy at
    // :174 has no null guard (the encoder always inits geometry before a
    // copy), so a null src.b64Geom would crash the REAL function too.
    Scs bare = {};
    bare.b64TotalCount = 1;
    bare.b64Size = 64;
    bare.sbSize = 128;
    bare.pictureWidthInSb = 1;
    bare.pictureHeightInSb = 1;
    CHECK(b64GeomInit(&bare, 64, 64, &bare.b64Geom) == 0);
    CHECK(sbGeomInit(&bare, 64, 64, &bare.sbGeom) == 0);
    Scs dstBare = {};
    dstBare.frameScaleEvts.evtNum = 0xEEEE;
    dstBare.sframePosi.sframeNum = 0xEEEE;
    CHECK(copySequenceControlSet(&dstBare, &bare) == 0);
    CHECK(dstBare.b64TotalCount == 1);
    CHECK(dstBare.frameScaleEvts.startFrameNums == nullptr);
    CHECK(dstBare.frameScaleEvts.resizeKfDenoms == nullptr);
    CHECK(dstBare.frameScaleEvts.resizeDenoms == nullptr);
    CHECK(dstBare.sframePosi.sframePosis == nullptr);
    CHECK(dstBare.sframePosi.sframeQps == nullptr);
    CHECK(dstBare.sframePosi.sframeQpOffsets == nullptr);
    CHECK(dstBare.b64Geom != nullptr);
    CHECK(dstBare.sbGeom != nullptr);
    std::free(dstBare.b64Geom);
    freeSbGeoms(dstBare.sbGeom);
    std::free(bare.b64Geom);
    freeSbGeoms(bare.sbGeom);

    // teardown (the port's dctors are the parked EbDtor walls; free by hand).
    std::free(src.frameScaleEvts.startFrameNums);
    std::free(src.frameScaleEvts.resizeKfDenoms);
    std::free(src.frameScaleEvts.resizeDenoms);
    std::free(src.sframePosi.sframePosis);
    std::free(src.sframePosi.sframeQps);
    std::free(src.sframePosi.sframeQpOffsets);
    std::free(src.b64Geom);
    freeSbGeoms(src.sbGeom);
    std::free(dst.frameScaleEvts.startFrameNums);
    std::free(dst.frameScaleEvts.resizeKfDenoms);
    std::free(dst.frameScaleEvts.resizeDenoms);
    std::free(dst.sframePosi.sframePosis);
    std::free(dst.sframePosi.sframeQps);
    std::free(dst.sframePosi.sframeQpOffsets);
    std::free(dst.b64Geom);
    freeSbGeoms(dst.sbGeom);
    std::free(dstBare.frameScaleEvts.startFrameNums);
    std::free(dstBare.frameScaleEvts.resizeKfDenoms);
    std::free(dstBare.frameScaleEvts.resizeDenoms);
    std::free(dstBare.sframePosi.sframePosis);
    std::free(dstBare.sframePosi.sframeQps);
    std::free(dstBare.sframePosi.sframeQpOffsets);
}

// ---------------------------------------------------------------------------
// C4: svt_pic_buf_desc_calc (pic_buffer_desc.c:224-278) and
// svt_pic_buf_desc_assign (:280-:323) - the buffer-size layout math and the
// plane pointer assignment. Both are file-local static in the reference;
// exported for direct testability (named deviation, LR1 boxsum1 precedent).
// ---------------------------------------------------------------------------

// Independent arithmetic oracle for the calc half (NOT calling the port):
// mirrors the reference layout formulas as standalone expressions.
struct ExpectedLayout {
    std::uint16_t yStride;
    std::uint16_t uvStride;
    std::uint32_t lumaSize;
    std::uint32_t chromaSize;
    std::uint32_t bufferSize[3];
    std::uint32_t bitIncSize[3];
    std::uint32_t allocSz;
    std::uint32_t bpp;
    bool packedFlag;
};

static ExpectedLayout expectedLayout(std::uint16_t width, std::uint16_t height, std::uint16_t border,
                                     l18_picmgmt::EbBitDepth bitDepth, l18_picmgmt::EbColorFormat fmt,
                                     std::uint32_t mask, bool split) {
    ExpectedLayout e = {};
    const std::uint16_t ssX = (fmt == l18_picmgmt::EB_YUV444) ? 0 : 1;
    const std::uint16_t ssY = (fmt == l18_picmgmt::EB_YUV444 || fmt == l18_picmgmt::EB_YUV422) ? 0 : 1;
    std::uint32_t bpp = (bitDepth == l18_picmgmt::EB_EIGHT_BIT)
        ? 1u
        : (bitDepth <= l18_picmgmt::EB_SIXTEEN_BIT) ? 2u : 4u;
    if (bitDepth > l18_picmgmt::EB_EIGHT_BIT && bitDepth <= l18_picmgmt::EB_SIXTEEN_BIT && split) {
        bpp = 1u;
    }
    e.yStride = static_cast<std::uint16_t>(width + 2 * border);
    e.uvStride = static_cast<std::uint16_t>((e.yStride + ssX) >> ssX);
    e.lumaSize = static_cast<std::uint32_t>(e.yStride) * (height + 2u * border);
    e.chromaSize = static_cast<std::uint32_t>(e.uvStride) * ((height + ssY + 2u * border) >> ssY);
    e.packedFlag = bpp > 1;
    e.bpp = bpp;
    if (mask & 1u) { // PICTURE_BUFFER_DESC_Y_FLAG
        e.bufferSize[0] = e.lumaSize * bpp;
        e.allocSz += e.bufferSize[0];
        if (split) {
            e.bitIncSize[0] = e.lumaSize * bpp;
            e.allocSz += e.bitIncSize[0];
        }
    }
    if (mask & 2u) { // PICTURE_BUFFER_DESC_Cb_FLAG
        e.bufferSize[1] = e.chromaSize * bpp;
        e.allocSz += e.bufferSize[1];
        if (split) {
            e.bitIncSize[1] = e.chromaSize * bpp;
            e.allocSz += e.bitIncSize[1];
        }
    }
    if (mask & 4u) { // PICTURE_BUFFER_DESC_Cr_FLAG
        e.bufferSize[2] = e.chromaSize * bpp;
        e.allocSz += e.bufferSize[2];
        if (split) {
            e.bitIncSize[2] = e.chromaSize * bpp;
            e.allocSz += e.bitIncSize[2];
        }
    }
    return e;
}

static void checkCalcAgainstOracle(const l18_picmgmt::PicBufferDesc& got, const ExpectedLayout& e,
                                   const l18_picmgmt::PicBufferDescInitData& d) {
    CHECK(got.width == d.maxWidth);
    CHECK(got.height == d.maxHeight);
    CHECK(got.maxWidth == d.maxWidth);
    CHECK(got.maxHeight == d.maxHeight);
    CHECK(got.bitDepth == d.bitDepth);
    CHECK(got.colorFormat == d.colorFormat);
    CHECK(got.yStride == e.yStride);
    CHECK(got.uStride == e.uvStride);
    CHECK(got.vStride == e.uvStride);
    CHECK(got.border == d.border);
    CHECK(got.lumaSize == e.lumaSize);
    CHECK(got.chromaSize == e.chromaSize);
    CHECK(got.packedFlag == e.packedFlag);
    CHECK(got.bufferEnableMask == d.bufferEnableMask);
    if (d.splitMode) {
        CHECK(got.yStrideBitInc == e.yStride);
        CHECK(got.uStrideBitInc == e.uvStride);
        CHECK(got.vStrideBitInc == e.uvStride);
    }
}

TEST_CASE("picBufDescCalc 8-bit 420 Y-only: strides, sizes, packed_flag") {
    l18_picmgmt::PicBufferDesc pic = {};
    l18_picmgmt::PicBufferDescInitData d = {};
    d.maxWidth = 192;
    d.maxHeight = 108;
    d.border = 32;
    d.bitDepth = l18_picmgmt::EB_EIGHT_BIT;
    d.colorFormat = l18_picmgmt::EB_YUV420;
    d.bufferEnableMask = l18_picmgmt::PICTURE_BUFFER_DESC_Y_FLAG;
    std::uint32_t bs[3] = {9, 9, 9};
    std::uint32_t bbi[3] = {9, 9, 9};
    std::uint32_t bpp = 9;
    const std::uint32_t sz = l18_picmgmt::picBufDescCalc(&pic, &d, bs, bbi, &bpp);
    checkCalcAgainstOracle(pic, expectedLayout(192, 108, 32, l18_picmgmt::EB_EIGHT_BIT,
                                               l18_picmgmt::EB_YUV420, 1u, false),
                           d);
    CHECK(bpp == 1);
    CHECK(bs[0] == pic.lumaSize);
    CHECK(bs[1] == 0);
    CHECK(bs[2] == 0);
    CHECK(bbi[0] == 0); // split off: no bit-inc sizes
    CHECK(sz == pic.lumaSize);
    CHECK(pic.bufferEnableMask == l18_picmgmt::PICTURE_BUFFER_DESC_Y_FLAG);
}

TEST_CASE("picBufDescCalc chroma masks and full-mask sum") {
    l18_picmgmt::PicBufferDesc pic = {};
    l18_picmgmt::PicBufferDescInitData d = {};
    d.maxWidth = 64;
    d.maxHeight = 64;
    d.border = 16;
    d.bitDepth = l18_picmgmt::EB_EIGHT_BIT;
    d.colorFormat = l18_picmgmt::EB_YUV420;
    d.bufferEnableMask = l18_picmgmt::PICTURE_BUFFER_DESC_FULL_MASK;
    std::uint32_t bs[3] = {0, 0, 0};
    std::uint32_t bbi[3] = {0, 0, 0};
    std::uint32_t bpp = 0;
    const std::uint32_t sz = l18_picmgmt::picBufDescCalc(&pic, &d, bs, bbi, &bpp);
    const ExpectedLayout e = expectedLayout(64, 64, 16, l18_picmgmt::EB_EIGHT_BIT,
                                            l18_picmgmt::EB_YUV420, 7u, false);
    checkCalcAgainstOracle(pic, e, d);
    CHECK(sz == e.allocSz);
    CHECK(bs[0] == e.lumaSize);
    CHECK(bs[1] == e.chromaSize);
    CHECK(bs[2] == e.chromaSize);
    CHECK(sz == pic.lumaSize + 2u * pic.chromaSize);
}

TEST_CASE("picBufDescCalc 420/422/444 subsampling bands") {
    const l18_picmgmt::EbColorFormat fmts[] = {l18_picmgmt::EB_YUV400, l18_picmgmt::EB_YUV420,
                                               l18_picmgmt::EB_YUV422, l18_picmgmt::EB_YUV444};
    for (l18_picmgmt::EbColorFormat fmt : fmts) {
        l18_picmgmt::PicBufferDesc pic = {};
        l18_picmgmt::PicBufferDescInitData d = {};
        d.maxWidth = 320;
        d.maxHeight = 180;
        d.border = 32;
        d.bitDepth = l18_picmgmt::EB_EIGHT_BIT;
        d.colorFormat = fmt;
        d.bufferEnableMask = l18_picmgmt::PICTURE_BUFFER_DESC_FULL_MASK;
        std::uint32_t bs[3] = {0, 0, 0};
        std::uint32_t bbi[3] = {0, 0, 0};
        std::uint32_t bpp = 0;
        l18_picmgmt::picBufDescCalc(&pic, &d, bs, bbi, &bpp);
        checkCalcAgainstOracle(pic, expectedLayout(320, 180, 32, l18_picmgmt::EB_EIGHT_BIT, fmt, 7u, false), d);
    }
    // Spot the ss arithmetic directly: 420/400 ss_y=1 (400 counts as ss_x=1
    // ss_y=1 by the reference's ternaries), 422 ss_y=0, 444 both 0.
    l18_picmgmt::PicBufferDesc p420 = {};
    l18_picmgmt::PicBufferDescInitData d420 = {};
    d420.maxWidth = 160;
    d420.maxHeight = 90;
    d420.border = 31; // odd border: the (h + ss_y + 2b) >> ss_y rounding
    d420.bitDepth = l18_picmgmt::EB_EIGHT_BIT;
    d420.colorFormat = l18_picmgmt::EB_YUV420;
    d420.bufferEnableMask = l18_picmgmt::PICTURE_BUFFER_DESC_Cb_FLAG;
    std::uint32_t bs[3] = {0, 0, 0};
    std::uint32_t bbi[3] = {0, 0, 0};
    std::uint32_t bpp = 0;
    l18_picmgmt::picBufDescCalc(&p420, &d420, bs, bbi, &bpp);
    // y_stride = 160 + 62 = 222; u_stride = (222+1)>>1 = 111;
    // chroma_size = 111 * ((90 + 1 + 62) >> 1) = 111 * 76.
    CHECK(p420.yStride == 222);
    CHECK(p420.uStride == 111);
    CHECK(p420.chromaSize == 111u * 76u);
}

TEST_CASE("picBufDescCalc bit-depth ladder and split_mode bit-inc sizes") {
    const l18_picmgmt::EbBitDepth depths[] = {l18_picmgmt::EB_EIGHT_BIT, l18_picmgmt::EB_TEN_BIT,
                                              l18_picmgmt::EB_TWELVE_BIT, l18_picmgmt::EB_SIXTEEN_BIT,
                                              l18_picmgmt::EB_THIRTYTWO_BIT};
    for (l18_picmgmt::EbBitDepth depth : depths) {
        for (int split = 0; split <= 1; ++split) {
            l18_picmgmt::PicBufferDesc pic = {};
            l18_picmgmt::PicBufferDescInitData d = {};
            d.maxWidth = 128;
            d.maxHeight = 96;
            d.border = 32;
            d.bitDepth = depth;
            d.colorFormat = l18_picmgmt::EB_YUV420;
            d.bufferEnableMask = l18_picmgmt::PICTURE_BUFFER_DESC_FULL_MASK;
            d.splitMode = split != 0;
            std::uint32_t bs[3] = {0, 0, 0};
            std::uint32_t bbi[3] = {0, 0, 0};
            std::uint32_t bpp = 0;
            const std::uint32_t sz = l18_picmgmt::picBufDescCalc(&pic, &d, bs, bbi, &bpp);
            const ExpectedLayout e = expectedLayout(128, 96, 32, depth, l18_picmgmt::EB_YUV420, 7u, split != 0);
            checkCalcAgainstOracle(pic, e, d);
            CHECK(bpp == e.bpp);
            CHECK(sz == e.allocSz);
            CHECK(bs[0] == e.bufferSize[0]);
            CHECK(bs[1] == e.bufferSize[1]);
            CHECK(bbi[0] == e.bitIncSize[0]);
        }
    }
    // Spot the split quirk: 10-bit split forces bpp 1 (bytes_per_pixel = 1),
    // so the bit-inc sizes EQUAL the plane sizes; 10-bit non-split is bpp 2.
    l18_picmgmt::PicBufferDesc pic = {};
    l18_picmgmt::PicBufferDescInitData d = {};
    d.maxWidth = 128;
    d.maxHeight = 96;
    d.border = 32;
    d.bitDepth = l18_picmgmt::EB_TEN_BIT;
    d.colorFormat = l18_picmgmt::EB_YUV420;
    d.bufferEnableMask = l18_picmgmt::PICTURE_BUFFER_DESC_Y_FLAG;
    d.splitMode = true;
    std::uint32_t bs[3] = {0, 0, 0};
    std::uint32_t bbi[3] = {0, 0, 0};
    std::uint32_t bpp = 0;
    const std::uint32_t sz = l18_picmgmt::picBufDescCalc(&pic, &d, bs, bbi, &bpp);
    CHECK(bpp == 1);
    CHECK(pic.packedFlag == false); // bpp == 1 -> packed_flag false (pic_buffer_desc.c:245)
    CHECK(bs[0] == bbi[0]);
    CHECK(sz == bs[0] + bbi[0]);
}

TEST_CASE("picBufDescCalc full-domain oracle over the whole grid") {
    const std::uint16_t widths[] = {1, 7, 8, 13, 64, 70, 193};
    const std::uint16_t heights[] = {1, 5, 16, 31, 90, 127};
    const std::uint16_t borders[] = {0, 32, 64};
    const l18_picmgmt::EbBitDepth depths[] = {l18_picmgmt::EB_EIGHT_BIT, l18_picmgmt::EB_TEN_BIT,
                                              l18_picmgmt::EB_TWELVE_BIT, l18_picmgmt::EB_SIXTEEN_BIT,
                                              l18_picmgmt::EB_THIRTYTWO_BIT};
    const l18_picmgmt::EbColorFormat fmts[] = {l18_picmgmt::EB_YUV400, l18_picmgmt::EB_YUV420,
                                               l18_picmgmt::EB_YUV422, l18_picmgmt::EB_YUV444};
    const std::uint32_t masks[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
    for (std::uint16_t w : widths) {
        for (std::uint16_t h : heights) {
            for (std::uint16_t b : borders) {
                for (l18_picmgmt::EbBitDepth depth : depths) {
                    for (l18_picmgmt::EbColorFormat fmt : fmts) {
                        for (std::uint32_t mask : masks) {
                            for (int split = 0; split <= 1; ++split) {
                                l18_picmgmt::PicBufferDesc pic = {};
                                pic.packedFlag = true; // poison: calc must set it
                                l18_picmgmt::PicBufferDescInitData d = {};
                                d.maxWidth = w;
                                d.maxHeight = h;
                                d.border = b;
                                d.bitDepth = depth;
                                d.colorFormat = fmt;
                                d.bufferEnableMask = mask;
                                d.splitMode = split != 0;
                                std::uint32_t bs[3] = {0xEE, 0xEE, 0xEE};
                                std::uint32_t bbi[3] = {0xEE, 0xEE, 0xEE};
                                std::uint32_t bpp = 0xEE;
                                const std::uint32_t sz =
                                    l18_picmgmt::picBufDescCalc(&pic, &d, bs, bbi, &bpp);
                                const ExpectedLayout e = expectedLayout(w, h, b, depth, fmt, mask, split != 0);
                                checkCalcAgainstOracle(pic, e, d);
                                CHECK(bpp == e.bpp);
                                CHECK(sz == e.allocSz);
                                for (int i = 0; i < 3; ++i) {
                                    CHECK(bs[i] == e.bufferSize[i]);
                                    CHECK(bbi[i] == e.bitIncSize[i]);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// 1:1 target: svt_pic_buf_desc_assign (pic_buffer_desc.c:280-:323): the
// plane pointers into a caller-provided base. The oracle computes the byte
// offsets independently from the same layout quantities.
static std::size_t yOffset(const l18_picmgmt::PicBufferDesc& p, std::uint32_t bpp) {
    return (static_cast<std::size_t>(p.border) + static_cast<std::size_t>(p.yStride) * p.border) * bpp;
}
static std::size_t uvOffset(const l18_picmgmt::PicBufferDesc& p, std::uint32_t bpp, std::uint16_t ssX,
                            std::uint16_t ssY) {
    return (static_cast<std::size_t>(p.border >> ssX) +
            static_cast<std::size_t>(p.uStride) * (p.border >> ssY)) * bpp;
}

TEST_CASE("picBufDescAssign points every plane at the right offset") {
    l18_picmgmt::PicBufferDesc pic = {};
    l18_picmgmt::PicBufferDescInitData d = {};
    d.maxWidth = 192;
    d.maxHeight = 108;
    d.border = 32;
    d.bitDepth = l18_picmgmt::EB_EIGHT_BIT;
    d.colorFormat = l18_picmgmt::EB_YUV420;
    d.bufferEnableMask = l18_picmgmt::PICTURE_BUFFER_DESC_FULL_MASK;
    std::uint32_t bs[3];
    std::uint32_t bbi[3];
    std::uint32_t bpp = 0;
    const std::uint32_t sz = l18_picmgmt::picBufDescCalc(&pic, &d, bs, bbi, &bpp);
    std::uint8_t* base = static_cast<std::uint8_t*>(std::malloc(sz));
    std::memset(base, 0, sz);
    l18_picmgmt::picBufDescAssign(&pic, &d, base, bs, bbi, bpp);
    CHECK(pic.bufferAlloc == base);
    CHECK(pic.yBuffer - base == static_cast<std::ptrdiff_t>(yOffset(pic, bpp)));
    // u sits after the Y plane.
    CHECK(pic.uBuffer - base ==
          static_cast<std::ptrdiff_t>(bs[0] + uvOffset(pic, bpp, 1, 1)));
    // v sits after the U plane.
    CHECK(pic.vBuffer - base ==
          static_cast<std::ptrdiff_t>(bs[0] + bs[1] + uvOffset(pic, bpp, 1, 1)));
    // The y bit-inc pointer is NULL without split (pic_buffer_desc.c:291).
    CHECK(pic.yBufferBitInc == nullptr);
    CHECK(pic.uBufferBitInc == nullptr);
    CHECK(pic.vBufferBitInc == nullptr);
    std::free(base);
}

TEST_CASE("picBufDescAssign split-mode bit-inc plane offsets") {
    l18_picmgmt::PicBufferDesc pic = {};
    l18_picmgmt::PicBufferDescInitData d = {};
    d.maxWidth = 128;
    d.maxHeight = 96;
    d.border = 32;
    d.bitDepth = l18_picmgmt::EB_TEN_BIT;
    d.colorFormat = l18_picmgmt::EB_YUV420;
    d.bufferEnableMask = l18_picmgmt::PICTURE_BUFFER_DESC_FULL_MASK;
    d.splitMode = true; // 10-bit split: bpp forced to 1, bit-inc sizes == plane sizes
    std::uint32_t bs[3];
    std::uint32_t bbi[3];
    std::uint32_t bpp = 0;
    const std::uint32_t sz = l18_picmgmt::picBufDescCalc(&pic, &d, bs, bbi, &bpp);
    CHECK(bpp == 1);
    std::uint8_t* base = static_cast<std::uint8_t*>(std::malloc(sz));
    std::memset(base, 0, sz);
    l18_picmgmt::picBufDescAssign(&pic, &d, base, bs, bbi, bpp);
    // Order: y, y_inc, u, u_inc, v, v_inc (the assigned_space walk).
    CHECK(pic.yBuffer - base == static_cast<std::ptrdiff_t>(yOffset(pic, bpp)));
    CHECK(pic.yBufferBitInc - base ==
          static_cast<std::ptrdiff_t>(bs[0] + yOffset(pic, bpp)));
    CHECK(pic.uBuffer - base ==
          static_cast<std::ptrdiff_t>(bs[0] + bbi[0] + uvOffset(pic, bpp, 1, 1)));
    CHECK(pic.uBufferBitInc - base ==
          static_cast<std::ptrdiff_t>(bs[0] + bbi[0] + bs[1] + uvOffset(pic, bpp, 1, 1)));
    CHECK(pic.vBuffer - base ==
          static_cast<std::ptrdiff_t>(bs[0] + bbi[0] + bs[1] + bbi[1] + uvOffset(pic, bpp, 1, 1)));
    CHECK(pic.vBufferBitInc - base ==
          static_cast<std::ptrdiff_t>(bs[0] + bbi[0] + bs[1] + bbi[1] + bs[2] + uvOffset(pic, bpp, 1, 1)));
    std::free(base);
}

TEST_CASE("picBufDescAssign partial masks leave unowned planes NULL") {
    const std::uint32_t masks[] = {0u, 1u, 2u, 4u, 3u, 5u, 6u};
    for (std::uint32_t mask : masks) {
        l18_picmgmt::PicBufferDesc pic = {};
        // Poison all three: assign writes ONLY the owned planes (the
        // reference has no else-branch; the real ctors rely on the
        // EB_NEW calloc-zero, so unowned planes RETAIN their prior value).
        pic.yBuffer = reinterpret_cast<std::uint8_t*>(0x11);
        pic.uBuffer = reinterpret_cast<std::uint8_t*>(0x22);
        pic.vBuffer = reinterpret_cast<std::uint8_t*>(0x33);
        l18_picmgmt::PicBufferDescInitData d = {};
        d.maxWidth = 64;
        d.maxHeight = 64;
        d.border = 16;
        d.bitDepth = l18_picmgmt::EB_EIGHT_BIT;
        d.colorFormat = l18_picmgmt::EB_YUV420;
        d.bufferEnableMask = mask;
        std::uint32_t bs[3];
        std::uint32_t bbi[3];
        std::uint32_t bpp = 0;
        const std::uint32_t sz = l18_picmgmt::picBufDescCalc(&pic, &d, bs, bbi, &bpp);
        std::uint8_t* base = static_cast<std::uint8_t*>(std::malloc(sz > 0 ? sz : 1));
        l18_picmgmt::picBufDescAssign(&pic, &d, base, bs, bbi, bpp);
        CHECK((mask & 1u) ? (pic.yBuffer == base + yOffset(pic, bpp))
                          : (pic.yBuffer == reinterpret_cast<std::uint8_t*>(0x11)));
        // u sits after the owned planes BEFORE it (Y when owned).
        const std::size_t uBase = ((mask & 1u) ? bs[0] : 0u);
        CHECK((mask & 2u) ? (pic.uBuffer == base + uBase + uvOffset(pic, bpp, 1, 1))
                          : (pic.uBuffer == reinterpret_cast<std::uint8_t*>(0x22)));
        CHECK((mask & 4u) ? (pic.vBuffer == base + uBase + ((mask & 2u) ? bs[1] : 0u) + uvOffset(pic, bpp, 1, 1))
                          : (pic.vBuffer == reinterpret_cast<std::uint8_t*>(0x33)));
        CHECK(pic.bufferAlloc == base);
        std::free(base);
    }
}
