// FS3: the per-size emission walk (4x4/8x8/32x32) bit-exact vs the FS2 gate
// lines. Single-TU SxS frames with the FS2 fixture (the top-half ramp);
// the production encodeFrameAuto*Q loops emit the full per-block symbol
// walk [partition iff read][skip=0][kf][FI iff][tokens] and the tile bytes
// must equal fs2S_bytes.
#include <doctest.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <pipeline.h>
#include <entropy.h>

namespace {

// the gate-line loader: loadGate("fs24_bytes ") -> the line's ints
// cap: fs264_coeffs = 4096 values + the count
struct Fs2Line {
    int n = 0;
    long long v[4200];
};

Fs2Line loadGate(const char* tag, int base = 10) {
    Fs2Line out;
    FILE* f = nullptr;
    // derive the repo root from __FILE__ (the CWD under ctest differs per
    // target; the l8 test precedent, test_bitstream.cpp tuFilePath)
    static const std::string gatePath = []() {
        const std::string f = __FILE__;
        std::string dir = f.substr(0, f.find_last_of("/\\") + 1);
        // src/l6_pipeline/tests/ -> repo root (3 up)
        for (int k = 0; k < 4; ++k) dir = dir.substr(0, dir.find_last_of("/\\"));
        return dir + "/tools/golden_gen/expected_primitives.txt";
    }();
    fopen_s(&f, gatePath.c_str(), "r");
    if (!f) {
        fprintf(stderr, "FS3 loader: gate file NOT FOUND: %s\n", gatePath.c_str());
        return out;
    }
    char line[16384];
    const size_t tlen = strlen(tag);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, tag, tlen) == 0) {
            char* p = line + tlen;
            char* ctx = nullptr;
            char* tok = strtok_s(p, " \t\r\n", &ctx);
            while (tok) {
                if (out.n < 4200) out.v[out.n++] = strtoll(tok, nullptr, base);
                tok = strtok_s(nullptr, " \t\r\n", &ctx);
            }
            break;
        }
    }
    fclose(f);
    if (out.n == 0) fprintf(stderr, "FS3 loader: tag [%s] not matched (file opened)\n", tag);
    return out;
}

}  // namespace

TEST_CASE("FS3: per-size emission walk bit-exact vs the FS2 gate lines (4x4/8x8/32x32/64x64)") {
    struct Geo {
        int S;
        entropy::TxSize ts;
        entropy::BlockSize bs;
        const char* tag;      // "fs24" etc
        bool partition;       // the walk reads a partition symbol
    };
    const Geo geos[4] = {
        {4, entropy::TX_4X4, entropy::BLOCK_4X4, "fs24", false},
        {8, entropy::TX_8X8, entropy::BLOCK_8X8, "fs28", true},
        {32, entropy::TX_32X32, entropy::BLOCK_32X32, "fs232", true},
        {64, entropy::TX_64X64, entropy::BLOCK_64X64, "fs264", true},
    };

    for (int g = 0; g < 4; ++g) {
        const int S = geos[g].S;
        const int n = S * S;

        pixels::Plane plane(S, S, 4);
        pixels::Plane recon(S, S, 4);
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x)
                plane.at(x, y) = static_cast<std::uint8_t>((y < S / 2) ? (4 * (x + y + 1)) : 0);

        std::int32_t coeffs[4096] = {0};
        std::uint8_t modes[1] = {0};
        entropy::EcFrameContext fc;
        entropy::AomWriter w{};
        unsigned char tileBuf[1024];
        memset(tileBuf, 0, sizeof(tileBuf));
        w.ec.buf = tileBuf;
        entropy::DcSignLevelCoeffNa na;
        memset(&na, 0xFF, sizeof(na));

        if (S == 4) {
            pipeline::encodeFrameAuto4x4Q(plane, recon, coeffs, modes, 100,
                                          transforms::TxType::DCT_DCT, &w, &fc, &na);
        } else if (S == 8) {
            pipeline::encodeFrameAuto8x8Q(plane, recon, coeffs, modes, 100,
                                          transforms::TxType::DCT_DCT, &w, &fc, &na);
        } else if (S == 32) {
            pipeline::encodeFrameAuto32x32Q(plane, recon, coeffs, modes, 100,
                                            transforms::TxType::DCT_DCT, &w, &fc, &na);
        } else {
            pipeline::encodeFrameAuto64x64Q(plane, recon, coeffs, modes, 100,
                                            transforms::TxType::DCT_DCT, &w, &fc, &na);
        }

        // the modes vs fs2S_modes
        char tag[40];
        snprintf(tag, sizeof(tag), "%s_modes", geos[g].tag);
        const Fs2Line wantM = loadGate(tag);
        REQUIRE(wantM.n == 1);
        CHECK((int)modes[0] == (int)wantM.v[0]);

        // the bytes vs fs2S_bytes
        snprintf(tag, sizeof(tag), "%s_bytes ", geos[g].tag);
        const Fs2Line wantB = loadGate(tag, 16);
        REQUIRE(wantB.n == 1 + (int)w.pos);  // the first value = the byte count
        for (int i = 0; i < (int)w.pos; ++i)
            CHECK((int)tileBuf[i] == (int)wantB.v[1 + i]);

        // the coeffs vs fs2S_coeffs
        snprintf(tag, sizeof(tag), "%s_coeffs", geos[g].tag);
        const Fs2Line wantC = loadGate(tag);
        REQUIRE(wantC.n == n);
        for (int i = 0; i < n; ++i) CHECK(coeffs[i] == (std::int32_t)wantC.v[i]);

        // the recon vs fs2S_recon
        snprintf(tag, sizeof(tag), "%s_recon", geos[g].tag);
        const Fs2Line wantR = loadGate(tag);
        REQUIRE(wantR.n == n);
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x)
                CHECK((int)recon.at(x, y) == (int)wantR.v[y * S + x]);
    }
}

TEST_CASE("FS5: 2x2 grid of 32x32 at 64x64 matches the fs5g32 gate (running partition ctxs)") {
    // The grid fixture: the fs2 64x64 fixture on a 64x64 frame, 2x2 raster of
    // 32x32 blocks, q100. The gate (svtd_fs5_grid_drive) emits the RUNNING
    // partition contexts (ecpart_update_ctx after each coded block) and the
    // running coefficient NA (mi-unit bookkeeping). RED slice: the production
    // loop still emits a fresh INVALID partition ctx per block and passes
    // pixel-based mi args to writeBlockCoeffs - the walk must diverge at
    // block 2+.
    const int S = 64;
    const int n = S * S;

    pixels::Plane plane(S, S, 4);
    pixels::Plane recon(S, S, 4);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
            plane.at(x, y) = static_cast<std::uint8_t>((y < S / 2) ? (4 * (x + y + 1)) : 0);

    std::int32_t coeffs[4096] = {0};
    std::uint8_t modes[4] = {0};
    entropy::EcFrameContext fc;
    entropy::AomWriter w{};
    unsigned char tileBuf[1024];
    memset(tileBuf, 0, sizeof(tileBuf));
    w.ec.buf = tileBuf;
    entropy::DcSignLevelCoeffNa na;
    memset(&na, 0xFF, sizeof(na));

    pipeline::encodeFrameAuto32x32Q(plane, recon, coeffs, modes, 100,
                                    transforms::TxType::DCT_DCT, &w, &fc, &na);

    const Fs2Line wantM = loadGate("fs5g32_modes");
    REQUIRE(wantM.n == 4);
    for (int b = 0; b < 4; ++b) CHECK((int)modes[b] == (int)wantM.v[b]);

    const Fs2Line wantB = loadGate("fs5g32_bytes ", 16);
    REQUIRE(wantB.n == 1 + (int)w.pos);
    for (int i = 0; i < (int)w.pos; ++i)
        CHECK((int)tileBuf[i] == (int)wantB.v[1 + i]);

    const Fs2Line wantC = loadGate("fs5g32_coeffs");
    REQUIRE(wantC.n == 4096);
    for (int i = 0; i < 4096; ++i) CHECK(coeffs[i] == (std::int32_t)wantC.v[i]);

    const Fs2Line wantR = loadGate("fs5g32_recon");
    REQUIRE(wantR.n == n);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x)
            CHECK((int)recon.at(x, y) == (int)wantR.v[y * S + x]);
}
