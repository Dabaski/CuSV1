#include <doctest.h>
#include <cdef.h>

// CDEF2 (PERRY-A8): the fb/lbd driver chain + cdef_copy. The strength
// derivation lives inline in the reference fb bodies (cdef.c:327-329,
// :551-553); the port factors it into cdefPriStrength/cdefSecStrength so the
// encoding contract is directly testable (named deviation, cdefSign
// precedent). cdef_strength encodes pri * CDEF_SEC_STRENGTHS + sec.
TEST_CASE("cdefPriStrength divides by 4 and shifts") {
    CHECK(l10_filters::cdefPriStrength(12, 0) == 3);
}

TEST_CASE("cdefSecStrength 12 has no secondary") {
    CHECK(l10_filters::cdefSecStrength(12, 0) == 0);
}

// Pin the rest of the encoding grid. These pass on first run against the
// already-transcribed formula; their discriminating power is proven by
// mutation M1 (swapped pri/sec derivation) below.
TEST_CASE("cdefStrength encoding grid: sec bump at 3, shifts, full range") {
    CHECK(l10_filters::cdefSecStrength(3, 0) == 4);
    CHECK(l10_filters::cdefSecStrength(2, 0) == 2);
    CHECK(l10_filters::cdefSecStrength(3, 2) == 16);
    CHECK(l10_filters::cdefPriStrength(5, 2) == 4);
    CHECK(l10_filters::cdefPriStrength(31, 0) == 7);
}

namespace {
// One 8x8 block at the origin of a padded 16-bit buffer: in[r * 144 + c].
struct FbFixture {
    std::uint16_t in[8 * l10_filters::kCdefBstride];
    std::uint8_t dir[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks];
    std::int32_t var[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks];
    l10_filters::CdefList dlist[256];
    FbFixture() {
        for (int r = 0; r < 8 * l10_filters::kCdefBstride; ++r) in[r] = 0;
        for (int i = 0; i < l10_filters::kCdefNblocks; ++i)
            for (int j = 0; j < l10_filters::kCdefNblocks; ++j) {
                dir[i][j] = 0;
                var[i][j] = 0;
            }
        for (int i = 0; i < 256; ++i) dlist[i] = {0, 0};
    }
};
} // namespace

TEST_CASE("cdefFilterFb zero strength copies input to packed dst16") {
    FbFixture f;
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) f.in[r * l10_filters::kCdefBstride + c] = static_cast<std::uint16_t>(100 + r * 8 + c);
    std::uint16_t dst[64];
    for (int i = 0; i < 64; ++i) dst[i] = 0xBEEF;
    l10_filters::cdefFilterFb(nullptr, dst, 0, f.in, 0, 0, f.dir, nullptr, f.var, 0, f.dlist, 1, 0, 7, 0, 1);
    int mism = 0;
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c)
            if (dst[r * 8 + c] != f.in[r * l10_filters::kCdefBstride + c]) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefFilterFb zero strength writes 8-bit scratch with uint8 truncation") {
    FbFixture f;
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) f.in[r * l10_filters::kCdefBstride + c] = static_cast<std::uint16_t>(300 + r * 8 + c);
    std::uint8_t dst[64];
    for (int i = 0; i < 64; ++i) dst[i] = 0xEE;
    l10_filters::cdefFilterFb(dst, nullptr, 0, f.in, 0, 0, f.dir, nullptr, f.var, 0, f.dlist, 1, 0, 7, 0, 1);
    int mism = 0;
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c)
            if (dst[r * 8 + c] != static_cast<std::uint8_t>(f.in[r * l10_filters::kCdefBstride + c])) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefFilterFb walk pri=0 sec=1 matches manual cdefFilterBlock per block") {
    // Padded 16-bit buffer, 32 rows x 144 stride; two 8x8 blocks side by side
    // at (8,8) and (8,16) in element coords (by=0, bx=0 and 1, block 8x8).
    std::uint16_t in[32 * l10_filters::kCdefBstride];
    for (int i = 0; i < 32 * l10_filters::kCdefBstride; ++i) in[i] = static_cast<std::uint16_t>(200);
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 16; ++c) in[(8 + r) * l10_filters::kCdefBstride + 8 + c] = static_cast<std::uint16_t>((r * 31 + c * 7) % 251 + 1);
    std::uint8_t dir[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks] = {};
    std::int32_t var[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks] = {};
    l10_filters::CdefList dlist[2] = {{0, 0}, {0, 1}};
    std::int32_t dirinit = 0;
    std::uint16_t dst[8 * 16] = {};  // dstride 16, 8 rows, two blocks
    l10_filters::cdefFilterFb(nullptr, dst, 16, in + 8 * l10_filters::kCdefBstride + 8, 0, 0, dir, &dirinit, var, 0, dlist, 2, 1, 7, 0, 1);
    std::uint16_t exp[8 * 16] = {};
    for (int bi = 0; bi < 2; ++bi) {
        l10_filters::cdefFilterBlock(nullptr, exp + bi * 8, 16, in + (8 * l10_filters::kCdefBstride) + 8 + 8 * bi, 0, 1, 0, 7, 7,
                                     l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    }
    int mism = 0;
    for (int i = 0; i < 8 * 16; ++i)
        if (dst[i] != exp[i]) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefFilterFb dirinit stale: poisoned dir is reused, not recomputed") {
    std::uint16_t in[32 * l10_filters::kCdefBstride];
    for (int i = 0; i < 32 * l10_filters::kCdefBstride; ++i) in[i] = static_cast<std::uint16_t>(200);
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 16; ++c) in[(8 + r) * l10_filters::kCdefBstride + 8 + c] = static_cast<std::uint16_t>((r * 31 + c * 7) % 251 + 1);
    std::uint8_t dir[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks] = {};
    dir[0][0] = 5;
    dir[0][1] = 5;
    std::int32_t var[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks] = {};
    var[0][0] = 1000;
    var[0][1] = 1000;
    l10_filters::CdefList dlist[2] = {{0, 0}, {0, 1}};
    std::int32_t dirinit = 1;
    std::uint16_t dst[8 * 16] = {};
    l10_filters::cdefFilterFb(nullptr, dst, 16, in + 8 * l10_filters::kCdefBstride + 8, 0, 0, dir, &dirinit, var, 0, dlist, 2, 8, 7, 0, 1);
    std::uint16_t exp[8 * 16] = {};
    for (int bi = 0; bi < 2; ++bi)
        l10_filters::cdefFilterBlock(nullptr, exp + bi * 8, 16, in + 8 * l10_filters::kCdefBstride + 8 + 8 * bi, 1, 0, 5, 7, 7,
                                     l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    int mism = 0;
    for (int i = 0; i < 8 * 16; ++i)
        if (dst[i] != exp[i]) ++mism;
    CHECK(mism == 0);
}

// Same contract, second face: the stale call must not WRITE dir either, and
// a null dirinit must recompute (poison replaced). Both are M2-kill targets.
TEST_CASE("cdefFilterFb dirinit stale leaves dir unwritten; null dirinit recomputes") {
    std::uint16_t in[32 * l10_filters::kCdefBstride];
    for (int i = 0; i < 32 * l10_filters::kCdefBstride; ++i) in[i] = static_cast<std::uint16_t>(200);
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 16; ++c) in[(8 + r) * l10_filters::kCdefBstride + 8 + c] = static_cast<std::uint16_t>((r * 31 + c * 7) % 251 + 1);
    std::uint8_t dir[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks] = {};
    dir[0][0] = 5;
    dir[0][1] = 5;
    std::int32_t var[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks] = {};
    var[0][0] = 1000;
    var[0][1] = 1000;
    l10_filters::CdefList dlist[2] = {{0, 0}, {0, 1}};
    std::int32_t dirinit = 1;
    std::uint16_t dst[8 * 16] = {};
    l10_filters::cdefFilterFb(nullptr, dst, 16, in + 8 * l10_filters::kCdefBstride + 8, 0, 0, dir, &dirinit, var, 0, dlist, 2, 8, 7, 0, 1);
    CHECK(dir[0][0] == 5);
    CHECK(dir[0][1] == 5);
    std::int32_t fresh = 0;
    l10_filters::cdefFilterFb(nullptr, dst, 16, in + 8 * l10_filters::kCdefBstride + 8, 0, 0, dir, &fresh, var, 0, dlist, 2, 8, 7, 0, 1);
    CHECK(dir[0][0] != 5);
    CHECK(fresh == 1);
    std::uint8_t dirNullPath[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks] = {};
    dirNullPath[0][0] = 5;
    l10_filters::cdefFilterFb(nullptr, dst, 16, in + 8 * l10_filters::kCdefBstride + 8, 0, 0, dirNullPath, nullptr, var, 0, dlist, 2, 8, 7, 0, 1);
    CHECK(dirNullPath[0][0] != 5);
}

TEST_CASE("cdefFilterFb chroma pli=1: t=pri unadjusted and damping-1") {
    std::uint16_t in[32 * l10_filters::kCdefBstride];
    for (int i = 0; i < 32 * l10_filters::kCdefBstride; ++i) in[i] = static_cast<std::uint16_t>(200);
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 16; ++c) in[(8 + r) * l10_filters::kCdefBstride + 8 + c] = static_cast<std::uint16_t>((r * 31 + c * 7) % 251 + 1);
    std::uint8_t dir[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks] = {};
    std::int32_t var[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks] = {};
    var[0][0] = 1000;
    var[0][1] = 1000;
    l10_filters::CdefList dlist[2] = {{0, 0}, {0, 1}};
    std::int32_t dirinit = 1;
    std::uint16_t dst[8 * 16] = {};
    l10_filters::cdefFilterFb(nullptr, dst, 16, in + 8 * l10_filters::kCdefBstride + 8, 0, 0, dir, &dirinit, var, 1, dlist, 2, 8, 7, 0, 1);
    std::uint16_t exp[8 * 16] = {};
    for (int bi = 0; bi < 2; ++bi)
        l10_filters::cdefFilterBlock(nullptr, exp + bi * 8, 16, in + 8 * l10_filters::kCdefBstride + 8 + 8 * bi, 2, 0, 0, 6, 6,
                                     l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    int mism = 0;
    for (int i = 0; i < 8 * 16; ++i)
        if (dst[i] != exp[i]) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefFilterFb 422 chroma remaps dir through conv422") {
    std::uint16_t in[40 * l10_filters::kCdefBstride];
    for (int i = 0; i < 40 * l10_filters::kCdefBstride; ++i) in[i] = static_cast<std::uint16_t>(200);
    for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 4; ++c) in[(8 + r) * l10_filters::kCdefBstride + 8 + c] = static_cast<std::uint16_t>((r * 31 + c * 7) % 251 + 1);
    std::uint8_t dir[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks] = {};
    dir[0][0] = 1;
    dir[1][0] = 5;
    std::int32_t var[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks] = {};
    l10_filters::CdefList dlist[2] = {{0, 0}, {1, 0}};
    std::int32_t dirinit = 1;
    std::uint16_t dst[2 * 8 * 4] = {};
    const int preDir[2] = {dir[0][0], dir[1][0]};
    l10_filters::cdefFilterFb(nullptr, dst, 4, in + 8 * l10_filters::kCdefBstride + 8, 1, 0, dir, &dirinit, var, 1, dlist, 2, 8, 7, 0, 1);
    std::uint16_t exp[2 * 8 * 4] = {};
    const int remap[8] = {7, 0, 2, 4, 5, 6, 6, 6};
    for (int bi = 0; bi < 2; ++bi) {
        const int by = dlist[bi].by;
        const int bx = dlist[bi].bx;
        l10_filters::cdefFilterBlock(nullptr, exp + (by << 3) * 4 + (bx << 2), 4,
                                     in + 8 * l10_filters::kCdefBstride + 8 + by * 8 * l10_filters::kCdefBstride + bx * 4, 2, 0, remap[preDir[bi]],
                                     6, 6, l10_filters::CdefBlockSize::BLOCK_4X8, 0, 1);
    }
    int mism = 0;
    for (int i = 0; i < 2 * 8 * 4; ++i) {
        if (dst[i] != exp[i]) ++mism;
    }
    CHECK(mism == 0);
}



TEST_CASE("cdefFilterFb 440 chroma remaps dir through conv440") {
    std::uint16_t in[40 * l10_filters::kCdefBstride];
    for (int i = 0; i < 40 * l10_filters::kCdefBstride; ++i) in[i] = static_cast<std::uint16_t>(200);
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) in[(8 + r) * l10_filters::kCdefBstride + 8 + c] = static_cast<std::uint16_t>((r * 31 + c * 7) % 251 + 1);
    std::uint8_t dir[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks] = {};
    dir[0][0] = 1;
    std::int32_t var[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks] = {};
    l10_filters::CdefList dlist[1] = {{0, 0}};
    std::int32_t dirinit = 1;
    std::uint16_t dst[8 * 8] = {};
    const int preDir = dir[0][0];
    l10_filters::cdefFilterFb(nullptr, dst, 8, in + 8 * l10_filters::kCdefBstride + 8, 0, 1, dir, &dirinit, var, 1, dlist, 1, 8, 7, 0, 1);
    std::uint16_t exp[8 * 8] = {};
    const int remap440[8] = {1, 2, 2, 2, 3, 4, 6, 0};
    l10_filters::cdefFilterBlock(nullptr, exp, 8, in + 8 * l10_filters::kCdefBstride + 8, 2, 0, remap440[preDir], 6, 6,
                                 l10_filters::CdefBlockSize::BLOCK_8X4, 0, 1);
    int mism = 0;
    for (int i = 0; i < 8 * 4; ++i)
        if (dst[i] != exp[i]) ++mism;
    CHECK(mism == 0);
}

// ---- CDEF2 lbd (8-bit) driver chain (cdef.c:542-778, CDEF_8BITS_PATH) ----
namespace {
struct LbdFixture {
    std::uint8_t in[32 * l10_filters::kCdefBstride];
    std::uint8_t dir[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks];
    std::int32_t var[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks];
    l10_filters::CdefList dlist[64];
    LbdFixture() {
        for (int i = 0; i < 32 * l10_filters::kCdefBstride; ++i) in[i] = 200;
        for (int i = 0; i < l10_filters::kCdefNblocks; ++i)
            for (int j = 0; j < l10_filters::kCdefNblocks; ++j) {
                dir[i][j] = 0;
                var[i][j] = 0;
            }
        for (int i = 0; i < 64; ++i) dlist[i] = {0, 0};
    }
    void fill(int rows, int cols) {
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) in[(8 + r) * l10_filters::kCdefBstride + 8 + c] = static_cast<std::uint8_t>((r * 31 + c * 7) % 251 + 1);
    }
};
} // namespace

TEST_CASE("cdefFilterFbLbdImpl zero strength copies packed 8-byte rows") {
    LbdFixture f;
    f.fill(8, 8);
    std::uint8_t dst[64];
    for (int i = 0; i < 64; ++i) dst[i] = 0xEE;
    std::int32_t dirinit = 0;
    l10_filters::cdefFilterFbLbdImpl(dst, 0, f.in + 8 * l10_filters::kCdefBstride + 8, 0, 0, 0, 0, 8, 8, 0, 0, f.dir, &dirinit,
                                     f.var, 0, f.dlist, 1, 0, 7, 0, 1, l10_filters::CdefBlockSize::BLOCK_8X8, 3, 3, 1);
    int mism = 0;
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c)
            if (dst[r * 8 + c] != f.in[(8 + r) * l10_filters::kCdefBstride + 8 + c]) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefFilterFbLbdImpl zero strength copies packed 4-byte rows at 4x4") {
    LbdFixture f;
    f.fill(4, 4);
    std::uint8_t dst[16];
    for (int i = 0; i < 16; ++i) dst[i] = 0xEE;
    std::int32_t dirinit = 0;
    l10_filters::cdefFilterFbLbdImpl(dst, 0, f.in + 8 * l10_filters::kCdefBstride + 8, 0, 0, 0, 0, 4, 4, 1, 1, f.dir, &dirinit,
                                     f.var, 1, f.dlist, 1, 0, 7, 0, 1, l10_filters::CdefBlockSize::BLOCK_4X4, 2, 2, 0);
    int mism = 0;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (dst[r * 4 + c] != f.in[(8 + r) * l10_filters::kCdefBstride + 8 + c]) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefFilterFbLbdImpl luma walk: find_dir_8bit fresh + interior kernel") {
    LbdFixture f;
    f.fill(8, 16);
    f.dir[0][0] = 5;
    f.dir[0][1] = 5;
    l10_filters::CdefList dlist[2] = {{0, 0}, {0, 1}};
    std::int32_t dirinit = 0;
    std::uint8_t dst[8 * 16];
    for (int i = 0; i < 8 * 16; ++i) dst[i] = 0xEE;
    l10_filters::cdefFilterFbLbdImpl(dst, 16, f.in + 8 * l10_filters::kCdefBstride + 8, 0, 0, 0, 0, 8, 16, 0, 0, f.dir, &dirinit,
                                     f.var, 0, dlist, 2, 8, 7, 0, 1, l10_filters::CdefBlockSize::BLOCK_8X8, 3, 3, 1);
    std::uint8_t expDir[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks] = {};
    std::int32_t expVar[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks] = {};
    l10_filters::cdefFindDir8bitList(f.in + 8 * l10_filters::kCdefBstride + 8, dlist, expVar, 2, 0, expDir);
    std::uint8_t exp[8 * 16] = {};
    for (int bi = 0; bi < 2; ++bi) {
        const int by = dlist[bi].by;
        const int bx = dlist[bi].bx;
        const int t = l10_filters::cdefAdjustStrength(2, expVar[by][bx]);
        l10_filters::cdefFilterBlock8bit(exp + (by << 3) * 16 + (bx << 3), 16,
                                         f.in + 8 * l10_filters::kCdefBstride + 8 + by * 8 * l10_filters::kCdefBstride + bx * 8, t, 0,
                                         expDir[by][bx], 7, l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    }
    int mism = 0;
    for (int i = 0; i < 8 * 16; ++i)
        if (dst[i] != exp[i]) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefFilterFbLbdImpl edge dispatch: left-edge block bounded, right neighbour interior") {
    LbdFixture f;
    f.fill(8, 16);
    f.dir[0][0] = 3;
    f.dir[0][1] = 3;
    f.var[0][0] = 1000;
    f.var[0][1] = 1000;
    l10_filters::CdefList dlist[2] = {{0, 0}, {0, 1}};
    std::int32_t dirinit = 1;
    std::uint8_t dst[8 * 16];
    for (int i = 0; i < 8 * 16; ++i) dst[i] = 0xEE;
    l10_filters::cdefFilterFbLbdImpl(dst, 16, f.in + 8 * l10_filters::kCdefBstride + 8, 0, 1, 0, 0, 8, 16, 0, 0, f.dir, &dirinit,
                                     f.var, 0, dlist, 2, 8, 7, 0, 1, l10_filters::CdefBlockSize::BLOCK_8X8, 3, 3, 1);
    std::uint8_t exp[8 * 16] = {};
    const int t = l10_filters::cdefAdjustStrength(2, 1000);
    l10_filters::cdefFilterBlock8bitBounded(exp, 16, f.in + 8 * l10_filters::kCdefBstride + 8, t, 0, 3, 7,
                                            l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1, 0, 1, 0, 0);
    l10_filters::cdefFilterBlock8bit(exp + 8, 16, f.in + 8 * l10_filters::kCdefBstride + 16, t, 0, 3, 7,
                                     l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    int mism = 0;
    for (int i = 0; i < 8 * 16; ++i)
        if (dst[i] != exp[i]) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefFilterFbLbdImpl edge dispatch: right-edge block bounded via bxLast") {
    LbdFixture f;
    f.fill(8, 16);
    f.dir[0][0] = 3;
    f.dir[0][1] = 3;
    f.var[0][0] = 1000;
    f.var[0][1] = 1000;
    l10_filters::CdefList dlist[2] = {{0, 0}, {0, 1}};
    std::int32_t dirinit = 1;
    std::uint8_t dst[8 * 16];
    for (int i = 0; i < 8 * 16; ++i) dst[i] = 0xEE;
    l10_filters::cdefFilterFbLbdImpl(dst, 16, f.in + 8 * l10_filters::kCdefBstride + 8, 1, 0, 0, 1, 8, 16, 0, 0, f.dir, &dirinit,
                                     f.var, 0, dlist, 2, 8, 7, 0, 1, l10_filters::CdefBlockSize::BLOCK_8X8, 3, 3, 1);
    std::uint8_t exp[8 * 16] = {};
    const int t = l10_filters::cdefAdjustStrength(2, 1000);
    l10_filters::cdefFilterBlock8bitBounded(exp, 16, f.in + 8 * l10_filters::kCdefBstride + 8, t, 0, 3, 7,
                                            l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1, 1, 0, 0, 0);
    l10_filters::cdefFilterBlock8bitBounded(exp + 8, 16, f.in + 8 * l10_filters::kCdefBstride + 16, t, 0, 3, 7,
                                            l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1, 1, 0, 0, 1);
    int mism = 0;
    for (int i = 0; i < 8 * 16; ++i)
        if (dst[i] != exp[i]) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefFilterFbLbdImpl edge dispatch: bottom row bounded via byLast, top row interior") {
    LbdFixture f;
    f.fill(16, 8);
    f.dir[0][0] = 3;
    f.dir[1][0] = 3;
    f.var[0][0] = 1000;
    f.var[1][0] = 1000;
    l10_filters::CdefList dlist[2] = {{0, 0}, {1, 0}};
    std::int32_t dirinit = 1;
    std::uint8_t dst[16 * 8];
    for (int i = 0; i < 16 * 8; ++i) dst[i] = 0xEE;
    l10_filters::cdefFilterFbLbdImpl(dst, 8, f.in + 8 * l10_filters::kCdefBstride + 8, 0, 0, 1, 0, 16, 8, 0, 0, f.dir, &dirinit,
                                     f.var, 0, dlist, 2, 8, 7, 0, 1, l10_filters::CdefBlockSize::BLOCK_8X8, 3, 3, 1);
    std::uint8_t exp[16 * 8] = {};
    const int t = l10_filters::cdefAdjustStrength(2, 1000);
    l10_filters::cdefFilterBlock8bit(exp, 8, f.in + 8 * l10_filters::kCdefBstride + 8, t, 0, 3, 7,
                                     l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1);
    l10_filters::cdefFilterBlock8bitBounded(exp + 8 * 8, 8, f.in + 8 * l10_filters::kCdefBstride + 8 + 8 * l10_filters::kCdefBstride, t, 0, 3, 7,
                                            l10_filters::CdefBlockSize::BLOCK_8X8, 0, 1, 0, 0, 1, 0);
    int mism = 0;
    for (int i = 0; i < 16 * 8; ++i)
        if (dst[i] != exp[i]) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefFilterFbLbdImpl chroma 422: dir remapped, t=pri, no find_dir") {
    LbdFixture f;
    f.fill(8, 4);
    f.dir[0][0] = 1;
    l10_filters::CdefList dlist[1] = {{0, 0}};
    std::int32_t dirinit = 1;
    std::uint8_t dst[8 * 4];
    for (int i = 0; i < 8 * 4; ++i) dst[i] = 0xEE;
    l10_filters::cdefFilterFbLbdImpl(dst, 4, f.in + 8 * l10_filters::kCdefBstride + 8, 0, 0, 0, 0, 8, 4, 1, 0, f.dir, &dirinit,
                                     f.var, 1, dlist, 1, 8, 7, 0, 1, l10_filters::CdefBlockSize::BLOCK_4X8, 2, 3, 0);
    std::uint8_t exp[8 * 4] = {};
    l10_filters::cdefFilterBlock8bit(exp, 4, f.in + 8 * l10_filters::kCdefBstride + 8, 2, 0, 0, 6,
                                     l10_filters::CdefBlockSize::BLOCK_4X8, 0, 1);
    int mism = 0;
    for (int i = 0; i < 8 * 4; ++i)
        if (dst[i] != exp[i]) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefFilterFbLbdImpl strength=0 dstride!=0: dir-only pass, dst untouched") {
    LbdFixture f;
    f.fill(8, 16);
    std::int32_t dirinit = 0;
    std::uint8_t dst[8 * 16];
    for (int i = 0; i < 8 * 16; ++i) dst[i] = 0xEE;
    l10_filters::cdefFilterFbLbdImpl(dst, 16, f.in + 8 * l10_filters::kCdefBstride + 8, 0, 0, 0, 0, 8, 16, 0, 0, f.dir, &dirinit,
                                     f.var, 0, f.dlist, 1, 0, 7, 0, 1, l10_filters::CdefBlockSize::BLOCK_8X8, 3, 3, 1);
    int mism = 0;
    for (int i = 0; i < 8 * 16; ++i)
        if (dst[i] != 0xEE) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefFilterFbLbd routes xdec=ydec=0 to the luma-pinned impl") {
    LbdFixture f;
    f.fill(8, 16);
    f.dir[0][0] = 3;
    f.dir[0][1] = 3;
    f.var[0][0] = 1000;
    f.var[0][1] = 1000;
    l10_filters::CdefList dlist[2] = {{0, 0}, {0, 1}};
    std::int32_t dirinitA = 0;
    std::int32_t dirinitB = 0;
    std::uint8_t dstA[8 * 16];
    std::uint8_t dstB[8 * 16];
    for (int i = 0; i < 8 * 16; ++i) {
        dstA[i] = 0xEE;
        dstB[i] = 0xEE;
    }
    l10_filters::cdefFilterFbLbd(dstA, 16, f.in + 8 * l10_filters::kCdefBstride + 8, 0, 1, 0, 0, 8, 16, 0, 0, f.dir, &dirinitA,
                                 f.var, 0, dlist, 2, 8, 7, 0, 1);
    l10_filters::cdefFilterFbLbdImpl(dstB, 16, f.in + 8 * l10_filters::kCdefBstride + 8, 0, 1, 0, 0, 8, 16, 0, 0, f.dir, &dirinitB,
                                     f.var, 0, dlist, 2, 8, 7, 0, 1, l10_filters::CdefBlockSize::BLOCK_8X8, 3, 3, 1);
    int mism = 0;
    for (int i = 0; i < 8 * 16; ++i)
        if (dstA[i] != dstB[i]) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefFilterFbLbd routes (1,1) chroma and (1,0) runtime variants") {
    LbdFixture f;
    f.fill(8, 4);
    f.dir[0][0] = 1;
    l10_filters::CdefList dlist[1] = {{0, 0}};
    std::int32_t dirinitA = 1;
    std::int32_t dirinitB = 1;
    std::uint8_t dstA[8 * 4];
    std::uint8_t dstB[8 * 4];
    for (int i = 0; i < 8 * 4; ++i) {
        dstA[i] = 0xEE;
        dstB[i] = 0xEE;
    }
    // (1,1) routes to the chroma wrapper: impl(4X4, 2, 2, is_luma 0, pli 1).
    l10_filters::cdefFilterFbLbd(dstA, 4, f.in + 8 * l10_filters::kCdefBstride + 8, 0, 0, 0, 0, 4, 4, 1, 1, f.dir, &dirinitA,
                                 f.var, 1, dlist, 1, 8, 7, 0, 1);
    l10_filters::cdefFilterFbLbdImpl(dstB, 4, f.in + 8 * l10_filters::kCdefBstride + 8, 0, 0, 0, 0, 4, 4, 1, 1, f.dir, &dirinitB,
                                     f.var, 1, dlist, 1, 8, 7, 0, 1, l10_filters::CdefBlockSize::BLOCK_4X4, 2, 2, 0);
    int mism = 0;
    for (int i = 0; i < 8 * 4; ++i)
        if (dstA[i] != dstB[i]) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefFilterFbLbd routes (1,0) to the runtime-variant impl (4X8, 2, 3)") {
    LbdFixture f;
    f.fill(8, 4);
    f.dir[0][0] = 1;
    l10_filters::CdefList dlist[1] = {{0, 0}};
    std::int32_t dirinitA = 1;
    std::int32_t dirinitB = 1;
    std::uint8_t dstA[8 * 4];
    std::uint8_t dstB[8 * 4];
    for (int i = 0; i < 8 * 4; ++i) {
        dstA[i] = 0xEE;
        dstB[i] = 0xEE;
    }
    l10_filters::cdefFilterFbLbd(dstA, 4, f.in + 8 * l10_filters::kCdefBstride + 8, 0, 0, 0, 0, 8, 4, 1, 0, f.dir, &dirinitA,
                                 f.var, 1, dlist, 1, 8, 7, 0, 1);
    std::uint8_t dirB[l10_filters::kCdefNblocks][l10_filters::kCdefNblocks];
    for (int i = 0; i < l10_filters::kCdefNblocks; ++i)
        for (int j = 0; j < l10_filters::kCdefNblocks; ++j) dirB[i][j] = f.dir[i][j];
    dirB[0][0] = 1;
    l10_filters::cdefFilterFbLbdImpl(dstB, 4, f.in + 8 * l10_filters::kCdefBstride + 8, 0, 0, 0, 0, 8, 4, 1, 0, dirB, &dirinitB,
                                     f.var, 1, dlist, 1, 8, 7, 0, 1, l10_filters::CdefBlockSize::BLOCK_4X8, 2, 3, 0);
    int mism = 0;
    for (int i = 0; i < 8 * 4; ++i)
        if (dstA[i] != dstB[i]) ++mism;
    CHECK(mism == 0);
}

// ---- CDEF2 copy helpers (cdef.c:72-79 + cdef_copy.h) ----------------------
TEST_CASE("cdefCopyRect8To16 widens u8 rows to u16 with strides") {
    std::uint8_t src[6 * 10];
    for (int i = 0; i < 6 * 10; ++i) src[i] = static_cast<std::uint8_t>(i * 3 + 1);
    std::uint16_t dst[6 * 8];
    for (int i = 0; i < 6 * 8; ++i) dst[i] = 0xBEEF;
    l10_filters::cdefCopyRect8To16(dst, 8, src, 10, 6, 8);
    int mism = 0;
    for (int r = 0; r < 6; ++r)
        for (int c = 0; c < 8; ++c)
            if (dst[r * 8 + c] != src[r * 10 + c]) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefCopyRect8 copies 32-wide rows from offset origin") {
    std::uint8_t src[20 * 40];
    for (int i = 0; i < 20 * 40; ++i) src[i] = static_cast<std::uint8_t>(i * 7 + 3);
    std::uint8_t dst[4 * 34];
    for (int i = 0; i < 4 * 34; ++i) dst[i] = 0xEE;
    l10_filters::cdefCopyRect8(dst, 34, src, 3, 5, 40, 4, 32);
    int mism = 0;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 32; ++c)
            if (dst[r * 34 + c] != src[(3 + r) * 40 + 5 + c]) ++mism;
    CHECK(mism == 0);
}

TEST_CASE("cdefCopyRect8 width sweep: listed widths and unlisted fallback agree") {
    std::uint8_t src[30 * 80];
    for (int i = 0; i < 30 * 80; ++i) src[i] = static_cast<std::uint8_t>(i * 5 + 11);
    const int widths[9] = {2, 32, 34, 36, 64, 66, 68, 5, 33};
    int mism = 0;
    for (int wi = 0; wi < 9; ++wi) {
        const int h = widths[wi];
        const int v = 3;
        std::uint8_t dst[3 * 70];
        for (int i = 0; i < 3 * 70; ++i) dst[i] = 0xEE;
        l10_filters::cdefCopyRect8(dst, 70, src, 7, 9, 80, v, h);
        for (int r = 0; r < v; ++r)
            for (int c = 0; c < h; ++c)
                if (dst[r * 70 + c] != src[(7 + r) * 80 + 9 + c]) ++mism;
    }
    CHECK(mism == 0);
}

TEST_CASE("cdefCopyRect16 and cdefCopySb8To16 compose the two branches") {
    std::uint16_t src16[8 * 20];
    std::uint8_t src8[8 * 20];
    for (int i = 0; i < 8 * 20; ++i) {
        src16[i] = static_cast<std::uint16_t>(i * 13 + 5);
        src8[i] = static_cast<std::uint8_t>(i * 13 + 5);
    }
    std::uint16_t dst[6 * 12];
    for (int i = 0; i < 6 * 12; ++i) dst[i] = 0xBEEF;
    l10_filters::cdefCopyRect16(dst, 12, src16 + 2 * 20 + 4, 20, 6, 10);
    int mism = 0;
    for (int r = 0; r < 6; ++r)
        for (int c = 0; c < 10; ++c)
            if (dst[r * 12 + c] != src16[(2 + r) * 20 + 4 + c]) ++mism;
    CHECK(mism == 0);
    std::uint16_t dstSb[6 * 12];
    for (int i = 0; i < 6 * 12; ++i) dstSb[i] = 0xBEEF;
    l10_filters::cdefCopySb8To16(dstSb, 12, src8, 2, 4, 20, 6, 10, 0);
    for (int r = 0; r < 6; ++r)
        for (int c = 0; c < 10; ++c)
            if (dstSb[r * 12 + c] != src8[(2 + r) * 20 + 4 + c]) ++mism;
    std::uint16_t dstSb16[6 * 12];
    for (int i = 0; i < 6 * 12; ++i) dstSb16[i] = 0xBEEF;
    l10_filters::cdefCopySb8To16(dstSb16, 12, reinterpret_cast<const std::uint8_t*>(src16), 2, 4, 20, 6, 10, 1);
    for (int r = 0; r < 6; ++r)
        for (int c = 0; c < 10; ++c)
            if (dstSb16[r * 12 + c] != src16[(2 + r) * 20 + 4 + c]) ++mism;
    CHECK(mism == 0);
}
