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

// ---------------------------------------------------------------------------
// C5: the ctor/update family of pic_buffer_desc.c:
//   svt_picture_buffer_desc_ctor_noy8b :35-:145 (the /4 bit-inc variant;
//     y_buffer stays NULL; packed_flag forced false),
//   svt_picture_buffer_desc_noy8b_update :152-:186,
//   svt_picture_buffer_desc_update :192-:219 (does NOT touch
//     bit_depth/color_format/packed_flag/buffer_enable_mask),
//   svt_recon_picture_buffer_desc_update :417-:442,
//   svt_recon_picture_buffer_desc_ctor :450-:516 (the calloc-zeroed recon
//     buffer; no split handling),
//   svt_aom_link_eb_to_aom_buffer_desc_8bit :518-:547 and
//   svt_aom_link_eb_to_aom_buffer_desc :549-:631 (the 16-bit branch uses
//     the CONVERT_TO_BYTEPTR >>1 address transform, definitions.h).
// The EB_MALLOC/CALLOC aligned-array walls use the standard heap (named
// deviation); the reference's EB_FREE-backed dctors stay parked.
// ---------------------------------------------------------------------------

TEST_CASE("pictureBufferDescCtorNoy8b allocates with the /4 bit-inc quirk") {
    l18_picmgmt::PicBufferDesc pic = {};
    // poison: the ctor must overwrite or leave per its contract
    pic.yBuffer = reinterpret_cast<std::uint8_t*>(0x5A);
    l18_picmgmt::PicBufferDescInitData d = {};
    d.maxWidth = 128;
    d.maxHeight = 96;
    d.border = 32;
    d.bitDepth = l18_picmgmt::EB_EIGHT_BIT;
    d.colorFormat = l18_picmgmt::EB_YUV420;
    d.bufferEnableMask = l18_picmgmt::PICTURE_BUFFER_DESC_FULL_MASK;
    d.splitMode = false;
    CHECK(l18_picmgmt::pictureBufferDescCtorNoy8b(&pic, &d) == 0);
    // Layout fields.
    CHECK(pic.yStride == 192);
    CHECK(pic.uStride == 96);
    CHECK(pic.vStride == 96);
    CHECK(pic.lumaSize == 192u * 160u);
    CHECK(pic.chromaSize == 96u * 80u);
    CHECK(pic.packedFlag == false); // forced false in this ctor (:63)
    CHECK(pic.bufferEnableMask == 7u);
    // The quirk: y_buffer is set to NULL at :72 and never pointed into the
    // allocation - the Y branch only handles the bit-inc pointer.
    CHECK(pic.yBuffer == nullptr);
    // u/v at the chroma offsets ((border>>ss_x) + u_stride*(border>>ss_y)):
    // (32>>1) + 96*(32>>1) = 16 + 1536 = 1552.
    CHECK(pic.uBuffer - pic.bufferAlloc == static_cast<std::ptrdiff_t>(16 + 96u * 16));
    CHECK(pic.vBuffer - pic.bufferAlloc ==
          static_cast<std::ptrdiff_t>(pic.chromaSize + 16 + 96u * 16));
    // No split: no bit-inc pointers. The Y branch contributes NO alloc size
    // (the reference comments the luma term out at :78), so the 8-bit
    // non-split allocation is chroma + chroma only.
    CHECK(pic.yBufferBitInc == nullptr);
    CHECK(pic.uBufferBitInc == nullptr);
    CHECK(pic.vBufferBitInc == nullptr);
    CHECK(pic.bufferAllocSz == 2u * pic.chromaSize);
    std::free(pic.bufferAlloc);

    // 10-bit split: bpp stays 1 in THIS ctor (bytes_per_pixel is declared 1
    // and never recomputed, :39), bit-inc sizes are size/4.
    l18_picmgmt::PicBufferDesc pic10 = {};
    l18_picmgmt::PicBufferDescInitData d10 = {};
    d10.maxWidth = 128;
    d10.maxHeight = 96;
    d10.border = 32;
    d10.bitDepth = l18_picmgmt::EB_TEN_BIT;
    d10.colorFormat = l18_picmgmt::EB_YUV420;
    d10.bufferEnableMask = l18_picmgmt::PICTURE_BUFFER_DESC_FULL_MASK;
    d10.splitMode = true;
    CHECK(l18_picmgmt::pictureBufferDescCtorNoy8b(&pic10, &d10) == 0);
    CHECK(pic10.yStrideBitInc == 192);
    CHECK(pic10.uStrideBitInc == 96);
    CHECK(pic10.packedFlag == false);
    const std::uint32_t luma = pic10.lumaSize;
    const std::uint32_t chroma = pic10.chromaSize;
    CHECK(pic10.bufferAllocSz == luma / 4u + chroma + chroma / 4u + chroma + chroma / 4u);
    // Walk order: y_inc (at the /4 luma offset), u, u_inc, v, v_inc.
    CHECK(pic10.yBuffer == nullptr); // still never assigned
    // Chroma offset 1552 = (32>>1) + 96*(32>>1); its /4 = 388 feeds the
    // bit-inc offsets.
    CHECK(pic10.yBufferBitInc - pic10.bufferAlloc ==
          static_cast<std::ptrdiff_t>((32 + 192u * 32) / 4u));
    CHECK(pic10.uBuffer - pic10.bufferAlloc ==
          static_cast<std::ptrdiff_t>(luma / 4u + 16 + 96u * 16));
    CHECK(pic10.uBufferBitInc - pic10.bufferAlloc ==
          static_cast<std::ptrdiff_t>(luma / 4u + chroma + (16 + 96u * 16) / 4u));
    CHECK(pic10.vBuffer - pic10.bufferAlloc ==
          static_cast<std::ptrdiff_t>(luma / 4u + chroma + chroma / 4u + 16 + 96u * 16));
    CHECK(pic10.vBufferBitInc - pic10.bufferAlloc ==
          static_cast<std::ptrdiff_t>(luma / 4u + chroma + chroma / 4u + chroma + (16 + 96u * 16) / 4u));
    std::free(pic10.bufferAlloc);
}

TEST_CASE("pictureBufferDescNoy8bUpdate mirrors the ctor layout without allocation") {
    l18_picmgmt::PicBufferDesc pic = {};
    pic.yBuffer = reinterpret_cast<std::uint8_t*>(0x77); // untouched by updates
    l18_picmgmt::PicBufferDescInitData d = {};
    d.maxWidth = 320;
    d.maxHeight = 180;
    d.border = 32;
    d.bitDepth = l18_picmgmt::EB_TEN_BIT;
    d.colorFormat = l18_picmgmt::EB_YUV422;
    d.bufferEnableMask = l18_picmgmt::PICTURE_BUFFER_DESC_LUMA_MASK;
    d.splitMode = true;
    CHECK(l18_picmgmt::pictureBufferDescNoy8bUpdate(&pic, &d) == 0);
    CHECK(pic.width == 320);
    CHECK(pic.height == 180);
    CHECK(pic.bitDepth == l18_picmgmt::EB_TEN_BIT);
    CHECK(pic.colorFormat == l18_picmgmt::EB_YUV422);
    CHECK(pic.yStride == 384);
    CHECK(pic.uStride == 192); // (384+1)>>1
    CHECK(pic.lumaSize == 384u * 244u);
    CHECK(pic.chromaSize == 192u * 244u); // ss_y = 0 for 422
    CHECK(pic.packedFlag == false);       // forced false (:176)
    CHECK(pic.bufferEnableMask == 1u);
    CHECK(pic.yStrideBitInc == 384);
    CHECK(pic.yBuffer == reinterpret_cast<std::uint8_t*>(0x77)); // no pointer writes
}

TEST_CASE("pictureBufferDescUpdate leaves depth format packed and mask untouched") {
    l18_picmgmt::PicBufferDesc pic = {};
    pic.bitDepth = l18_picmgmt::EB_TWELVE_BIT;
    pic.colorFormat = l18_picmgmt::EB_YUV444;
    pic.packedFlag = true;
    pic.bufferEnableMask = 0x1234u;
    l18_picmgmt::PicBufferDescInitData d = {};
    d.maxWidth = 64;
    d.maxHeight = 48;
    d.border = 16;
    d.bitDepth = l18_picmgmt::EB_EIGHT_BIT;
    d.colorFormat = l18_picmgmt::EB_YUV420;
    d.bufferEnableMask = 7u;
    d.splitMode = false;
    CHECK(l18_picmgmt::pictureBufferDescUpdate(&pic, &d) == 0);
    // The update writes ONLY width/height/max/strides/sizes/border and the
    // split strides - depth/format/packed/mask are NOT in its walk
    // (:192-:219).
    CHECK(pic.width == 64);
    CHECK(pic.height == 48);
    CHECK(pic.yStride == 96);
    CHECK(pic.uStride == 48);
    CHECK(pic.lumaSize == 96u * 80u);
    CHECK(pic.chromaSize == 48u * 40u);
    CHECK(pic.bitDepth == l18_picmgmt::EB_TWELVE_BIT); // poison survived
    CHECK(pic.colorFormat == l18_picmgmt::EB_YUV444);
    CHECK(pic.packedFlag == true);
    CHECK(pic.bufferEnableMask == 0x1234u);
    // split off: the bit-inc strides stay untouched.
    pic.yStrideBitInc = 123;
    l18_picmgmt::PicBufferDescInitData d2 = d;
    d2.splitMode = true;
    CHECK(l18_picmgmt::pictureBufferDescUpdate(&pic, &d2) == 0);
    CHECK(pic.yStrideBitInc == 96);
    CHECK(pic.uStrideBitInc == 48);
}

TEST_CASE("reconPictureBufferDescUpdate sets depth packed and mask") {
    l18_picmgmt::PicBufferDesc pic = {};
    pic.yStrideBitInc = 0xBEEF; // recon_update does NOT touch bit-inc strides
    pic.uStrideBitInc = 0xBEEF;
    l18_picmgmt::PicBufferDescInitData d = {};
    d.maxWidth = 128;
    d.maxHeight = 128;
    d.border = 32;
    d.bitDepth = l18_picmgmt::EB_TEN_BIT;
    d.colorFormat = l18_picmgmt::EB_YUV420;
    d.bufferEnableMask = l18_picmgmt::PICTURE_BUFFER_DESC_FULL_MASK;
    CHECK(l18_picmgmt::reconPictureBufferDescUpdate(&pic, &d) == 0);
    CHECK(pic.bitDepth == l18_picmgmt::EB_TEN_BIT);
    CHECK(pic.colorFormat == l18_picmgmt::EB_YUV420);
    CHECK(pic.packedFlag == true); // bit_depth > EB_EIGHT_BIT (:437)
    CHECK(pic.bufferEnableMask == 7u);
    CHECK(pic.yStride == 192);
    CHECK(pic.uStride == 96);
    CHECK(pic.lumaSize == 192u * 192u);
    CHECK(pic.chromaSize == 96u * 96u);
    CHECK(pic.yStrideBitInc == 0xBEEF); // poison survived
    // 8-bit flips packed_flag back to false.
    d.bitDepth = l18_picmgmt::EB_EIGHT_BIT;
    CHECK(l18_picmgmt::reconPictureBufferDescUpdate(&pic, &d) == 0);
    CHECK(pic.packedFlag == false);
}

TEST_CASE("reconPictureBufferDescCtor calloc-zeroes and assigns y/u/v") {
    l18_picmgmt::PicBufferDesc pic = {};
    pic.yBuffer = reinterpret_cast<std::uint8_t*>(0x44);
    l18_picmgmt::PicBufferDescInitData d = {};
    d.maxWidth = 64;
    d.maxHeight = 64;
    d.border = 16;
    d.bitDepth = l18_picmgmt::EB_EIGHT_BIT;
    d.colorFormat = l18_picmgmt::EB_YUV420;
    d.bufferEnableMask = l18_picmgmt::PICTURE_BUFFER_DESC_FULL_MASK;
    CHECK(l18_picmgmt::reconPictureBufferDescCtor(&pic, &d) == 0);
    CHECK(pic.bufferAllocSz == pic.lumaSize + 2u * pic.chromaSize);
    CHECK(pic.yBuffer - pic.bufferAlloc == static_cast<std::ptrdiff_t>(16 + 96u * 16));
    // Chroma offsets: (16>>1) + 48*(16>>1) = 8 + 384 = 392.
    CHECK(pic.uBuffer - pic.bufferAlloc == static_cast<std::ptrdiff_t>(pic.lumaSize + 8 + 48u * 8));
    CHECK(pic.vBuffer - pic.bufferAlloc ==
          static_cast<std::ptrdiff_t>(pic.lumaSize + pic.chromaSize + 8 + 48u * 8));
    CHECK(pic.packedFlag == false);
    // The calloc-zero contract: the recon ctor zero-fills the allocation
    // (EB_CALLOC_ALIGNED_ARRAY, :493) - every byte readable.
    for (std::size_t i = 0; i < pic.bufferAllocSz; ++i) {
        if (pic.bufferAlloc[i] != 0) {
            CHECK(pic.bufferAlloc[i] == 0);
            break;
        }
    }
    std::free(pic.bufferAlloc);
}

TEST_CASE("reconPictureBufferDescCtor 10-bit packs two bytes per pixel") {
    l18_picmgmt::PicBufferDesc pic = {};
    l18_picmgmt::PicBufferDescInitData d = {};
    d.maxWidth = 64;
    d.maxHeight = 64;
    d.border = 16;
    d.bitDepth = l18_picmgmt::EB_TEN_BIT;
    d.colorFormat = l18_picmgmt::EB_YUV420;
    d.bufferEnableMask = l18_picmgmt::PICTURE_BUFFER_DESC_FULL_MASK;
    d.splitMode = true; // recon ctor IGNORES split_mode (no bit-inc walk)
    CHECK(l18_picmgmt::reconPictureBufferDescCtor(&pic, &d) == 0);
    CHECK(pic.bufferAllocSz == 2u * (pic.lumaSize + 2u * pic.chromaSize));
    CHECK(pic.packedFlag == true);
    // No bit-inc pointers written: poison in, poison out.
    std::uint8_t* firstAlloc = pic.bufferAlloc;
    pic.yBufferBitInc = reinterpret_cast<std::uint8_t*>(0x88);
    l18_picmgmt::PicBufferDescInitData d2 = d;
    CHECK(l18_picmgmt::reconPictureBufferDescCtor(&pic, &d2) == 0);
    CHECK(pic.yBufferBitInc == reinterpret_cast<std::uint8_t*>(0x88));
    std::free(firstAlloc);
    std::free(pic.bufferAlloc);
}

TEST_CASE("linkEbToAomBufferDesc8bit copies the field set") {
    l18_picmgmt::PicBufferDesc src = {};
    src.yBuffer = reinterpret_cast<std::uint8_t*>(0x1000);
    src.uBuffer = reinterpret_cast<std::uint8_t*>(0x2000);
    src.vBuffer = reinterpret_cast<std::uint8_t*>(0x3000);
    src.width = 13; // odd: uv_width = width / 2 = 6 (integer division, :527)
    src.height = 65;
    src.yStride = 96;
    src.uStride = 48;
    src.border = 32;
    l18_picmgmt::Yv12BufferConfig aom = {};
    aom.flags = 0xEE;
    l18_picmgmt::linkEbToAomBufferDesc8bit(&src, &aom);
    CHECK(aom.yBuffer == src.yBuffer);
    CHECK(aom.uBuffer == src.uBuffer);
    CHECK(aom.vBuffer == src.vBuffer);
    CHECK(aom.yWidth == 13);
    CHECK(aom.uvWidth == 6); // 13/2, NOT (13+1)>>1
    CHECK(aom.yHeight == 65);
    CHECK(aom.uvHeight == 32);
    CHECK(aom.yStride == 96);
    CHECK(aom.uvStride == 48);
    CHECK(aom.border == 32);
    CHECK(aom.subsamplingX == 1);
    CHECK(aom.subsamplingY == 1);
    CHECK(aom.yCropWidth == 13);
    CHECK(aom.uvCropWidth == 6);
    CHECK(aom.yCropHeight == 65);
    CHECK(aom.uvCropHeight == 32);
    CHECK(aom.flags == 0);
}

TEST_CASE("linkEbToAomBufferDesc 8-bit and the 16-bit CONVERT branch") {
    l18_picmgmt::PicBufferDesc src = {};
    src.width = 13;
    src.height = 65;
    src.yStride = 96;
    src.uStride = 48;
    src.border = 32;
    l18_picmgmt::Yv12BufferConfig aom = {};
    // 8-bit branch: uv_width = (13+1)>>1 = 7, crop arithmetic with pads.
    src.yBuffer = reinterpret_cast<std::uint8_t*>(0x1000);
    src.uBuffer = reinterpret_cast<std::uint8_t*>(0x2000);
    src.vBuffer = reinterpret_cast<std::uint8_t*>(0x3000);
    src.bitDepth = l18_picmgmt::EB_EIGHT_BIT;
    l18_picmgmt::linkEbToAomBufferDesc(&src, &aom, 5, 1, false);
    CHECK(aom.yWidth == 13);
    CHECK(aom.uvWidth == 7); // (13+1)>>1, different from the 8bit variant
    CHECK(aom.yHeight == 65);
    CHECK(aom.uvHeight == 33);
    CHECK(aom.yCropWidth == 13 - 5);
    CHECK(aom.uvCropWidth == (8 + 1) >> 1); // ((13-5)+1)>>1
    CHECK(aom.yCropHeight == 65 - 1);
    CHECK(aom.uvCropHeight == (64 + 1) >> 1);
    CHECK(aom.subsamplingX == 1);
    CHECK(aom.subsamplingY == 1);
    CHECK(aom.flags == 0);
    // 16-bit branch: the pointers go through CONVERT_TO_BYTEPTR (>>1) and
    // the flag is YV12_FLAG_HIGHBITDEPTH (8).
    src.yBuffer = reinterpret_cast<std::uint8_t*>(0x1000);
    src.uBuffer = reinterpret_cast<std::uint8_t*>(0x2000);
    src.vBuffer = reinterpret_cast<std::uint8_t*>(0x3002);
    src.bitDepth = l18_picmgmt::EB_TEN_BIT;
    l18_picmgmt::linkEbToAomBufferDesc(&src, &aom, 0, 0, true);
    CHECK(aom.yBuffer == reinterpret_cast<std::uint8_t*>(0x1000 >> 1));
    CHECK(aom.uBuffer == reinterpret_cast<std::uint8_t*>(0x2000 >> 1));
    CHECK(aom.vBuffer == reinterpret_cast<std::uint8_t*>(0x3002 >> 1));
    CHECK(aom.flags == l18_picmgmt::YV12_FLAG_HIGHBITDEPTH);
    CHECK(aom.yWidth == 13);
    CHECK(aom.uvWidth == 7);
    CHECK(aom.yCropWidth == 13);
}

// ---------------------------------------------------------------------------
// C6: reference_object.c spans:
//   initialize_samples_neighboring_reference_picture_8bit :21-:44 (the
//     one-pixel ring zeroing around the reconstructed samples),
//   initialize_samples_neighboring_reference_picture :46-:55 (the y/u/v
//     wrapper with the >>1 chroma dims),
//   svt_reference_param_update :83-:125 (the SCS-derived init data, the
//     padding derivation, the 10-bit split fork, mi_rows/mi_cols),
//   svt_reference_object_reset :204-:209,
//   svt_pa_reference_param_update :239-:288 (the three PA init datas),
//   svt_tpl_reference_param_update :348-:372 (the TPL_PAD 32 luma-only
//     init data).
// ---------------------------------------------------------------------------

TEST_CASE("initializeSamples8bit zeroes the one-pixel ring only") {
    const std::uint16_t stride = 40;
    const std::uint16_t w = 9;
    const std::uint16_t h = 5;
    // The ring spans rows 0..6 around the samples at row 1: SEVEN rows,
    // not six - the bottom ring row sits BELOW the samples block (the
    // reference ctor's border supplies it).
    std::uint8_t buf[7 * 40];
    std::memset(buf, 0xFF, sizeof(buf));
    std::uint8_t* samples = buf + 1 * stride + 1; // one ring row/col in
    l18_picmgmt::initializeSamplesNeighboringReferencePicture8bit(samples, stride, w, h);
    // Top row: samples - stride - 1 == buf + 0, 1 + w + 1 bytes.
    for (int i = 0; i < 1 + w + 1; ++i) {
        CHECK(buf[i] == 0);
    }
    // Bottom row: samples + h*stride - 1 == buf + 41 + 200 - 1 = buf + 240.
    for (int i = 0; i < 1 + w + 1; ++i) {
        CHECK(buf[(1 * stride + 1) + h * stride - 1 + i] == 0);
    }
    // Left column: samples - 1 + k*stride == buf[(k+1)*stride].
    for (int k = 0; k < h; ++k) {
        CHECK(buf[(k + 1) * stride] == 0);
    }
    // Right column: samples + w + k*stride == buf[50 + k*stride].
    for (int k = 0; k < h; ++k) {
        CHECK(buf[(k + 1) * stride + w + 1] == 0);
    }
    // Interior untouched.
    for (int k = 0; k < h; ++k) {
        for (int i = 0; i < w; ++i) {
            CHECK(buf[(k + 1) * stride + 1 + i] == 0xFF);
        }
    }
}

TEST_CASE("initializeSamplesNeighboringReferencePicture rings all three planes") {
    l18_picmgmt::PicBufferDesc ref = {};
    ref.width = 64;
    ref.height = 64;
    ref.yStride = 96;
    ref.uStride = 48;
    ref.vStride = 48;
    const std::size_t ySize = 96u * 96u;
    const std::size_t cSize = 48u * 48u;
    std::uint8_t* y = static_cast<std::uint8_t*>(std::malloc(ySize));
    std::uint8_t* u = static_cast<std::uint8_t*>(std::malloc(cSize));
    std::uint8_t* v = static_cast<std::uint8_t*>(std::malloc(cSize));
    std::memset(y, 0xFF, ySize);
    std::memset(u, 0xFF, cSize);
    std::memset(v, 0xFF, cSize);
    ref.yBuffer = y + ref.yStride + 1;
    ref.uBuffer = u + ref.uStride + 1;
    ref.vBuffer = v + ref.vStride + 1;
    l18_picmgmt::initializeSamplesNeighboringReferencePicture(&ref);
    // Luma ring (samples at y + 97): top row y[0..65], bottom row
    // y[97 + 64*96 - 1 .. +65] = y[6240..6305], left col y[96 + k*96],
    // right col y[161 + k*96].
    for (int i = 0; i < 66; ++i) {
        CHECK(y[i] == 0);
        CHECK(y[97 + 64 * 96 - 1 + i] == 0);
    }
    for (int k = 0; k < 64; ++k) {
        CHECK(y[96 + k * 96] == 0);
        CHECK(y[96 + 65 + k * 96] == 0);
    }
    CHECK(y[97 + 31 * 96 + 31] == 0xFF); // interior stays
    // Chroma: 32x32 rings (samples at u + 49): top u[0..33], bottom
    // u[49 + 32*48 - 1 .. +33] = u[1584..1617], left u[48 + k*48], right
    // u[81 + k*48].
    for (int i = 0; i < 34; ++i) {
        CHECK(u[i] == 0);
        CHECK(v[49 + 32 * 48 - 1 + i] == 0);
    }
    for (int k = 0; k < 32; ++k) {
        CHECK(u[48 + k * 48] == 0);
        CHECK(v[49 + 32 + k * 48] == 0);
    }
    CHECK(u[49 + 15 * 48 + 15] == 0xFF);
    std::free(y);
    std::free(u);
    std::free(v);
}

TEST_CASE("referenceParamUpdate derives the init data and rings 8-bit refs") {
    l18_picmgmt::PicBufferDesc refPic = {};
    refPic.width = 1;
    refPic.height = 1;
    refPic.packedFlag = true;    // update() does not write packed_flag - poison stays
    refPic.yStrideBitInc = 0xEE; // split stays OFF at 8-bit: poison survives
    refPic.bitDepth = static_cast<l18_picmgmt::EbBitDepth>(0xEE);
    refPic.colorFormat = l18_picmgmt::EB_YUV444;
    refPic.bufferEnableMask = 0x1234u;    l18_picmgmt::RefObject refObj = {};
    refObj.referencePicture = &refPic;
    l18_picmgmt::Scs scs = {};
    scs.maxInputLumaWidth = 128;
    scs.maxInputLumaHeight = 96;
    scs.encoderBitDepth = 8;
    scs.staticConfig.encoderColorFormat = l18_picmgmt::EB_YUV420;
    scs.staticConfig.superresMode = 0; // SUPERRES_NONE
    scs.staticConfig.resizeMode = 0;   // RESIZE_NONE
    scs.superBlockSize = 128;
    scs.restUnitsPerTile = 7;
    scs.b64TotalCount = 21;
    scs.mfmvEnabled = 1;
    scs.is16bitPipeline = false;
    // Real buffers for the ring zeroing: the largest layout this test
    // drives is border 288 (superres bump) -> y_stride 704, luma
    // 704x608, chroma 352x304. The samples pointers sit one ring
    // row/col in, exactly like the reference's own ctor output.
    std::uint8_t* y = static_cast<std::uint8_t*>(std::malloc(704u * 608u));
    std::uint8_t* u = static_cast<std::uint8_t*>(std::malloc(352u * 304u));
    std::uint8_t* v = static_cast<std::uint8_t*>(std::malloc(352u * 304u));
    std::memset(y, 0xFF, 704u * 608u);
    std::memset(u, 0xFF, 352u * 304u);
    std::memset(v, 0xFF, 352u * 304u);
    refPic.yBuffer = y + 704 + 1;
    refPic.uBuffer = u + 352 + 1;
    refPic.vBuffer = v + 352 + 1;
    CHECK(l18_picmgmt::referenceParamUpdate(&refObj, &scs) == 0);
    // The update wrote the layout into the reference picture. bit_depth /
    // color_format / buffer_enable_mask are NOT in picture_buffer_desc_
    // update's walk (:192-:219) - the real encoder's ctor set them at
    // creation; the poison here must survive.
    CHECK(refPic.width == 128);
    CHECK(refPic.height == 96);
    CHECK(refPic.bitDepth == static_cast<l18_picmgmt::EbBitDepth>(0xEE));
    CHECK(refPic.colorFormat == l18_picmgmt::EB_YUV444);
    CHECK(refPic.bufferEnableMask == 0x1234u);
    // padding = super_block_size + 32, no superres/resize bump.
    CHECK(refPic.border == 160);
    CHECK(refPic.yStride == 128 + 2 * 160);
    CHECK(refPic.lumaSize == 448u * 416u);
    CHECK(refPic.packedFlag == true);    // poison survived: update() never writes it
    CHECK(refPic.yStrideBitInc == 0xEE); // split_mode false in the derived init data
    // mi grid.
    CHECK(refObj.miRows == 96u >> 2);
    CHECK(refObj.miCols == 128u >> 2);

    // superres bump: padding += super_block_size -> 128+32+128 = 288.
    scs.staticConfig.superresMode = 1;
    CHECK(l18_picmgmt::referenceParamUpdate(&refObj, &scs) == 0);
    CHECK(refPic.border == 288);
    scs.staticConfig.superresMode = 0;
    scs.staticConfig.resizeMode = 2;
    CHECK(l18_picmgmt::referenceParamUpdate(&refObj, &scs) == 0);
    CHECK(refPic.border == 288); // the same bump through resize_mode
    scs.staticConfig.resizeMode = 0;

    // 10-bit: is_16bit flips bit_depth to EB_TEN_BIT and split on (the
    // 16bit init-data copy); NO ring zeroing happens on the 10-bit path
    // (:111-:113), so the y_buffer contents are not touched here.
    scs.encoderBitDepth = 10;
    scs.staticConfig.encoderBitDepth = 10;
    CHECK(l18_picmgmt::referenceParamUpdate(&refObj, &scs) == 0);
    // The 10-bit init data's bit_depth never reaches the descriptor
    // (update() does not write it) - the poison survives here too. The
    // 10-bitness is observable through the split strides only.
    CHECK(refPic.bitDepth == static_cast<l18_picmgmt::EbBitDepth>(0xEE));
    CHECK(refPic.yStrideBitInc == refPic.yStride);
    CHECK(refPic.uStrideBitInc == refPic.uStride);
    CHECK(refObj.miRows == 24u);
    CHECK(refObj.miCols == 32u);
    // The 8-bit ring zeroing ran on the FIRST (border-160) pass with
    // samples at y + 705 and y_stride 448: top ring row = y + 705 - 448
    // - 1 = y + 256, 130 cells; interior pixels survived at 0xFF.
    for (int i = 0; i < 130; ++i) {
        CHECK(y[705 - 448 - 1 + i] == 0);
    }
    CHECK(y[705 + 16 * 448 + 16] == 0xFF);
    CHECK(u[353 + 16 * 224 + 8] == 0xFF);
    std::free(y);
    std::free(u);
    std::free(v);
}

TEST_CASE("referenceObjectReset derives the mi grid from the SCS max dims") {
    l18_picmgmt::RefObject refObj = {};
    refObj.miRows = 0xEE;
    refObj.miCols = 0xEE;
    l18_picmgmt::Scs scs = {};
    scs.maxInputLumaWidth = 130;
    scs.maxInputLumaHeight = 66;
    CHECK(l18_picmgmt::referenceObjectReset(&refObj, &scs) == 0);
    CHECK(refObj.miRows == 66u >> 2);
    CHECK(refObj.miCols == 130u >> 2);
}

TEST_CASE("paReferenceParamUpdate fills the three PA init datas") {
    l18_picmgmt::PicBufferDesc inPic = {};
    l18_picmgmt::PicBufferDesc quart = {};
    l18_picmgmt::PicBufferDesc sixteenth = {};
    // The depth/format/mask fields are NOT in picture_buffer_desc_update's
    // walk - poison them and pin the survival (the real ctor set them).
    inPic.bitDepth = static_cast<l18_picmgmt::EbBitDepth>(0xEE);
    inPic.colorFormat = l18_picmgmt::EB_YUV444;
    inPic.bufferEnableMask = 0x1234u;
    inPic.packedFlag = true;
    quart.bitDepth = static_cast<l18_picmgmt::EbBitDepth>(0xEE);
    quart.bufferEnableMask = 0x4321u;
    l18_picmgmt::PaRefObject pa = {};
    pa.inputPaddedPic = &inPic;
    pa.quarterDownsampledPicturePtr = &quart;
    pa.sixteenthDownsampledPicturePtr = &sixteenth;
    l18_picmgmt::Scs scs = {};
    scs.maxInputLumaWidth = 128;
    scs.maxInputLumaHeight = 96;
    scs.border = 80;
    scs.b64Size = 64;
    scs.restUnitsPerTile = 4;
    CHECK(l18_picmgmt::paReferenceParamUpdate(&pa, &scs) == 0);
    // ref: the SCS dims and border land; depth/format/mask survive.
    CHECK(inPic.width == 128);
    CHECK(inPic.height == 96);
    CHECK(inPic.bitDepth == 0xEE);
    CHECK(inPic.colorFormat == l18_picmgmt::EB_YUV444);
    CHECK(inPic.bufferEnableMask == 0x1234u);
    CHECK(inPic.border == 80);
    CHECK(inPic.packedFlag == true);
    CHECK(inPic.yStride == 128 + 2 * 80);
    CHECK(inPic.uStride == (128 + 2 * 80 + 1) >> 1);
    // quarter: dims >>1, border = b64_size >> 1.
    CHECK(quart.width == 64);
    CHECK(quart.height == 48);
    CHECK(quart.bitDepth == 0xEE);
    CHECK(quart.bufferEnableMask == 0x4321u);
    CHECK(quart.border == 32);
    // sixteenth: dims >>2, border = b64_size >> 2.
    CHECK(sixteenth.width == 32);
    CHECK(sixteenth.height == 24);
    CHECK(sixteenth.border == 16);
    CHECK(sixteenth.yStride == 32 + 2 * 16);
    CHECK(sixteenth.lumaSize == 64u * 56u);
}

TEST_CASE("tplReferenceParamUpdate fills the TPL_PAD luma-only init data") {
    l18_picmgmt::PicBufferDesc tpl = {};
    tpl.bitDepth = static_cast<l18_picmgmt::EbBitDepth>(0xEE);
    tpl.colorFormat = l18_picmgmt::EB_YUV444;
    tpl.bufferEnableMask = 0x4321u;
    tpl.packedFlag = true;
    l18_picmgmt::TplRefObject tplRef = {};
    tplRef.refPicturePtr = &tpl;
    l18_picmgmt::Scs scs = {};
    scs.maxInputLumaWidth = 100;
    scs.maxInputLumaHeight = 60;
    scs.sbTotalCount = 9;
    CHECK(l18_picmgmt::tplReferenceParamUpdate(&tplRef, &scs) == 0);
    CHECK(tpl.width == 100);
    CHECK(tpl.height == 60);
    // Depth/format/mask are not in update()'s walk - poison survives.
    CHECK(tpl.bitDepth == 0xEE);
    CHECK(tpl.colorFormat == l18_picmgmt::EB_YUV444);
    CHECK(tpl.bufferEnableMask == 0x4321u);
    CHECK(tpl.border == 32); // TPL_PAD (encode_context.h:35)
    CHECK(tpl.packedFlag == true);
    // y_stride = 100 + 64 = 164, chroma stride = (164+1)>>1 = 82,
    // luma_size = 164 * (60 + 64) = 164 * 124.
    CHECK(tpl.yStride == 164);
    CHECK(tpl.uStride == 82);
    CHECK(tpl.lumaSize == 164u * 124u);
}
