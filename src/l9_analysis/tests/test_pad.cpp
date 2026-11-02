#include <doctest.h>
#include <analysis.h>

// 1:1 targets: the pad family. Helper kernels from Codec/pic_operators.c
// (svt_aom_generate_padding :266-301, svt_aom_generate_padding_compressed_10bit
// :303-342, pad_input_picture :393-436, svt_aom_pad_input_picture_16bit
// :441-479, memset16bit pic_operators.h:39-45), drivers from
// Codec/pic_analysis_process.c (pad_2b_compressed_input_picture :648-739,
// svt_aom_pad_picture_to_multiple_of_min_blk_size_dimensions :745-813,
// ..._16bit :820-852, svt_aom_pad_picture_to_multiple_of_sb_dimensions
// :858-866, svt_aom_pad_input_pictures :1549-1615). The helpers charge to
// A6 (pic_operators.c is A6-classified in tools/coverage_areas.txt), the
// drivers to A11.
//
// CONTRACT (derived from resource_coordination_process.c:713-728 and pinned
// by the driver tests below): scs->pad_right = MIN_BLOCK_SIZE - (width %
// MIN_BLOCK_SIZE) with MIN_BLOCK_SIZE 8 (definitions.h:2018-2019), and
// max_input_luma_width is UPDATED IN PLACE to width + pad_right (:719), so
// by the time the drivers run, input_pic->width is the PADDED width and
// scs->pad_right the padding amount: every "original_src_width" parameter
// is the CONTENT width (width - pad_right, pic_analysis_process.c:756),
// never the buffer width. Chroma content = (width + ss_x - pad_right) >>
// ss_x (:764) and chroma pad = pad_right >> ss_x (:766).

TEST_CASE("generatePadding horizontal pass replicates edge pixels, zero vertical") {
    // width 4, height 2, stride 8, padding 2 left/right, padding_height 0.
    // src_pic points AT the first content pixel; the left pad is written at
    // src - padding_width (the reference convention). Pads poisoned 0xEE
    // before the call.
    std::uint8_t buf[2 * 8];
    for (int i = 0; i < 2 * 8; ++i) {
        buf[i] = 0xEE;
    }
    buf[2] = 10;
    buf[3] = 11;
    buf[4] = 12;
    buf[5] = 13;
    buf[8 + 2] = 20;
    buf[8 + 3] = 21;
    buf[8 + 4] = 22;
    buf[8 + 5] = 23;

    analysis::generatePadding(buf + 2, 8, 4, 2, 2, 0);

    CHECK(buf[0] == 10);
    CHECK(buf[1] == 10);
    CHECK(buf[2] == 10);
    CHECK(buf[3] == 11);
    CHECK(buf[4] == 12);
    CHECK(buf[5] == 13);
    CHECK(buf[6] == 13);
    CHECK(buf[7] == 13);
    CHECK(buf[8 + 0] == 20);
    CHECK(buf[8 + 1] == 20);
    CHECK(buf[8 + 6] == 23);
    CHECK(buf[8 + 7] == 23);
}

TEST_CASE("generatePadding vertical pass replicates the fully padded rows") {
    // 3 content rows at stride 8, content width 4, padding 2x2: TWO pad rows
    // above and TWO below (padding_height counts BOTH directions), all four
    // poisoned 0xEE before the call. The vertical copies must carry the
    // POST-horizontal row (left/right pads included), so the top pad rows'
    // corners read the horizontal pass's values, not the poison.
    std::uint8_t buf[7 * 8];
    for (int i = 0; i < 7 * 8; ++i) {
        buf[i] = 0xEE;
    }
    // content rows 2..4, content cols 2..5
    buf[2 * 8 + 2] = 10;
    buf[2 * 8 + 3] = 11;
    buf[2 * 8 + 4] = 12;
    buf[2 * 8 + 5] = 13;
    buf[3 * 8 + 2] = 20;
    buf[3 * 8 + 3] = 21;
    buf[3 * 8 + 4] = 22;
    buf[3 * 8 + 5] = 23;
    buf[4 * 8 + 2] = 30;
    buf[4 * 8 + 3] = 31;
    buf[4 * 8 + 4] = 32;
    buf[4 * 8 + 5] = 33;

    analysis::generatePadding(buf + 2 * 8 + 2, 8, 4, 3, 2, 2);

    // Both top pad rows = copy of content row 0 AFTER its horizontal pass.
    for (int r = 0; r < 2; ++r) {
        CHECK(buf[r * 8 + 0] == 10);
        CHECK(buf[r * 8 + 1] == 10);
        CHECK(buf[r * 8 + 2] == 10);
        CHECK(buf[r * 8 + 5] == 13);
        CHECK(buf[r * 8 + 6] == 13);
        CHECK(buf[r * 8 + 7] == 13);
    }
    // Both bottom pad rows = copy of content row 2 AFTER its horizontal pass.
    for (int r = 5; r < 7; ++r) {
        CHECK(buf[r * 8 + 0] == 30);
        CHECK(buf[r * 8 + 1] == 30);
        CHECK(buf[r * 8 + 6] == 33);
        CHECK(buf[r * 8 + 7] == 33);
    }
}

TEST_CASE("generatePadding vertical copy spans src_stride bytes, overhang included") {
    // content 4 wide at stride 12, padding 2x1: content + 2*pad = 8 < 12, so
    // cols 8..11 of the content row are stride overhang. The horizontal pass
    // must NOT touch the overhang (sentinel 0x77 stays), while the vertical
    // copy of row_bytes = src_stride = 12 bytes MUST carry it into the top
    // pad row - a copy sized origWidth + 2*pad (8 bytes) would leave the
    // pad row's overhang poisoned.
    std::uint8_t buf[3 * 12];
    for (int i = 0; i < 3 * 12; ++i) {
        buf[i] = 0xEE;
    }
    // content row 1, cols 2..5
    buf[1 * 12 + 2] = 10;
    buf[1 * 12 + 3] = 11;
    buf[1 * 12 + 4] = 12;
    buf[1 * 12 + 5] = 13;
    // overhang sentinels on the content row
    buf[1 * 12 + 8] = 0x77;
    buf[1 * 12 + 9] = 0x88;
    buf[1 * 12 + 10] = 0x99;
    buf[1 * 12 + 11] = 0xAA;

    analysis::generatePadding(buf + 1 * 12 + 2, 12, 4, 1, 2, 1);

    // horizontal pass left the overhang alone
    CHECK(buf[1 * 12 + 8] == 0x77);
    CHECK(buf[1 * 12 + 11] == 0xAA);
    // top pad row: pads + content AND the overhang, i.e. a full src_stride copy
    CHECK(buf[0 * 12 + 0] == 10);
    CHECK(buf[0 * 12 + 5] == 13);
    CHECK(buf[0 * 12 + 8] == 0x77);
    CHECK(buf[0 * 12 + 9] == 0x88);
    CHECK(buf[0 * 12 + 10] == 0x99);
    CHECK(buf[0 * 12 + 11] == 0xAA);
}

TEST_CASE("generatePadding null pointer is a silent return") {
    analysis::generatePadding(nullptr, 8, 4, 2, 2, 2);
    CHECK(true);
}

TEST_CASE("generatePaddingCompressed10bit pads MSB-first 2-bit bytes") {
    // The 2-bit plane: 4 pixels per byte, MSB first (byte k holds pixels
    // 4k..4k+3; the packer svt_unpack_and_2bcompress_c writes pixel col+0
    // at bits 7-6). content width 8 (two bytes), height 2, comp stride 4,
    // padding 4 (one byte each side). Row bytes 0xAB 0xCD: left pixel =
    // (0xAB >> 6) & 3 = 2 -> left pad byte 2 * 0b01010101 = 0xAA; right
    // pixel = 0xCD & 3 = 1 -> right pad byte 1 * 0b01010101 = 0x55. Vertical
    // copies span the comp stride (4 bytes: pad + content + pad); padding
    // height 2 means TWO pad rows above and TWO below.
    std::uint8_t buf[6 * 4];
    for (int i = 0; i < 6 * 4; ++i) {
        buf[i] = 0xEE;
    }
    buf[2 * 4 + 1] = 0xAB;
    buf[2 * 4 + 2] = 0xCD;
    buf[3 * 4 + 1] = 0xAB;
    buf[3 * 4 + 2] = 0xCD;

    analysis::generatePaddingCompressed10bit(buf + 2 * 4 + 1, 4, 8, 2, 4, 2);

    // horizontal pads of both content rows
    for (int r = 2; r < 4; ++r) {
        CHECK(buf[r * 4 + 0] == 0xAA);
        CHECK(buf[r * 4 + 3] == 0x55);
    }
    // vertical pads: full padded rows replicated (0xAA 0xAB 0xCD 0x55)
    for (int r = 0; r < 6; ++r) {
        if (r == 2 || r == 3) {
            continue;
        }
        CHECK(buf[r * 4 + 0] == 0xAA);
        CHECK(buf[r * 4 + 1] == 0xAB);
        CHECK(buf[r * 4 + 2] == 0xCD);
        CHECK(buf[r * 4 + 3] == 0x55);
    }
}

TEST_CASE("generatePaddingCompressed10bit null pointer is a silent return") {
    analysis::generatePaddingCompressed10bit(nullptr, 4, 8, 2, 4, 2);
    CHECK(true);
}

TEST_CASE("padInputPicture right pad replicates the last content pixel") {
    // width 4, height 2, stride 8, pad_right 2, pad_bottom 0. Right pad
    // starts AT content column width (not width+left pad: this function has
    // no left pad). Pads poisoned 0xEE.
    std::uint8_t buf[2 * 8];
    for (int i = 0; i < 2 * 8; ++i) {
        buf[i] = 0xEE;
    }
    buf[0] = 10;
    buf[1] = 11;
    buf[2] = 12;
    buf[3] = 13;
    buf[8] = 20;
    buf[8 + 1] = 21;
    buf[8 + 2] = 22;
    buf[8 + 3] = 23;

    analysis::padInputPicture(buf, 8, 4, 2, 2, 0);

    CHECK(buf[4] == 13);
    CHECK(buf[5] == 13);
    CHECK(buf[8 + 4] == 23);
    CHECK(buf[8 + 5] == 23);
}

TEST_CASE("padInputPicture bottom pad copies the last content row") {
    // width 4, height 2, stride 8, pad_right 0, pad_bottom 2. The copy
    // count is original_src_width + pad_right = 4 bytes (content only).
    std::uint8_t buf[4 * 8];
    for (int i = 0; i < 4 * 8; ++i) {
        buf[i] = 0xEE;
    }
    buf[1 * 8] = 10;
    buf[1 * 8 + 1] = 11;
    buf[1 * 8 + 2] = 12;
    buf[1 * 8 + 3] = 13;

    analysis::padInputPicture(buf + 1 * 8, 8, 4, 1, 0, 2);

    for (int r = 2; r < 4; ++r) {
        CHECK(buf[r * 8 + 0] == 10);
        CHECK(buf[r * 8 + 1] == 11);
        CHECK(buf[r * 8 + 2] == 12);
        CHECK(buf[r * 8 + 3] == 13);
        // the copy stops at content width: col 4 stays poisoned
        CHECK(buf[r * 8 + 4] == 0xEE);
    }
}

TEST_CASE("padInputPicture bottom pad includes the right pad it just wrote") {
    // width 4, stride 8, pad_right 2, pad_bottom 1. The right pass runs
    // FIRST, so the bottom copy of (width + pad_right) = 6 bytes carries
    // the replicated right pad - a bottom-copy mutant that ran before the
    // right pass would leave the pad columns poisoned.
    std::uint8_t buf[4 * 8];
    for (int i = 0; i < 4 * 8; ++i) {
        buf[i] = 0xEE;
    }
    buf[1 * 8] = 10;
    buf[1 * 8 + 1] = 11;
    buf[1 * 8 + 2] = 12;
    buf[1 * 8 + 3] = 13;

    analysis::padInputPicture(buf + 1 * 8, 8, 4, 1, 2, 1);

    // right pad of the content row
    CHECK(buf[1 * 8 + 4] == 13);
    CHECK(buf[1 * 8 + 5] == 13);
    // bottom row: content + right pad, copied
    CHECK(buf[2 * 8 + 0] == 10);
    CHECK(buf[2 * 8 + 3] == 13);
    CHECK(buf[2 * 8 + 4] == 13);
    CHECK(buf[2 * 8 + 5] == 13);
    // the copy stops at width + pad_right: col 6 stays poisoned
    CHECK(buf[2 * 8 + 6] == 0xEE);
}

TEST_CASE("padInputPicture zero pad is a no-op") {
    std::uint8_t buf[2 * 8];
    for (int i = 0; i < 2 * 8; ++i) {
        buf[i] = 0xEE;
    }
    buf[0] = 10;
    buf[1] = 11;
    buf[2] = 12;
    buf[3] = 13;

    analysis::padInputPicture(buf, 8, 4, 2, 0, 0);

    CHECK(buf[4] == 0xEE);
    CHECK(buf[7] == 0xEE);
    CHECK(buf[1 * 8 + 4] == 0xEE);
}

TEST_CASE("padInputPicture null pointer is a silent return") {
    analysis::padInputPicture(nullptr, 8, 4, 2, 2, 2);
    CHECK(true);
}

TEST_CASE("padInputPicture16bit right and bottom pads at uint16") {
    // Values above 8 bits prove uint16 element semantics; stride is in
    // ELEMENTS (the reference advances temp_src_pic0 += src_stride with
    // uint16_t* and memcpy-copies sizeof(uint16_t) * count bytes). width 4,
    // height 2, stride 8, pad_right 2, pad_bottom 1.
    std::uint16_t buf[4 * 8];
    for (int i = 0; i < 4 * 8; ++i) {
        buf[i] = 0xEEEE;
    }
    buf[1 * 8 + 0] = 0x0142;
    buf[1 * 8 + 1] = 0x0243;
    buf[1 * 8 + 2] = 0x0344;
    buf[1 * 8 + 3] = 0x0445;

    analysis::padInputPicture16bit(buf + 1 * 8, 8, 4, 1, 2, 1);

    // right pad of the content row
    CHECK(buf[1 * 8 + 4] == 0x0445);
    CHECK(buf[1 * 8 + 5] == 0x0445);
    // bottom row: content + right pad (6 elements), copied
    CHECK(buf[2 * 8 + 0] == 0x0142);
    CHECK(buf[2 * 8 + 3] == 0x0445);
    CHECK(buf[2 * 8 + 4] == 0x0445);
    CHECK(buf[2 * 8 + 5] == 0x0445);
    // copy stops at width + pad_right: element 6 stays poisoned
    CHECK(buf[2 * 8 + 6] == 0xEEEE);
}

// The seven pad_2b fixtures: content width W = 8 - pad_right (the
// 8-alignment contract), padded row 8 pixels = 2 bytes. Pixel i of the
// content carries value i % 3 (values in {0,1,2}); every un-Written byte
// is poisoned 0xFF so every 2-bit slot reads 3 - a branch that leaves a
// pad slot unwritten, reads the wrong bits, or writes the wrong byte
// fails. Stride 4: bytes 2..3 of each row are overhang poison.

TEST_CASE("pad2bCompressedInputPicture pad_right 1 (content 7)") {
    // W=7: last_col = 1, byte 1 holds pixels 4,5,6 (values 1,2,0) in bits
    // 7-2 + poison bits 1-0. 0x63 & 0xfc = 0x60, last pixel (bits 3-2) = 0,
    // OR-fill 0 -> byte 1 = 0x60. No second byte (1 pad pixel fills the
    // byte's tail).
    std::uint8_t buf[2 * 4];
    for (int i = 0; i < 2 * 4; ++i) {
        buf[i] = 0xFF;
    }
    buf[0] = 0x18;  // pixels 0..3 = 0,1,2,0
    buf[1] = 0x67;  // pixels 4..6 = 1,2,1 + poison 3
    buf[4] = 0x18;
    buf[4 + 1] = 0x67;

    analysis::pad2bCompressedInputPicture(buf, 4, 7, 2, 1, 0);

    CHECK(buf[0] == 0x18);
    CHECK(buf[1] == 0x65);
    CHECK(buf[2] == 0xFF);
    CHECK(buf[3] == 0xFF);
    CHECK(buf[4] == 0x18);
    CHECK(buf[5] == 0x65);
    CHECK(buf[6] == 0xFF);
    CHECK(buf[7] == 0xFF);
}

TEST_CASE("pad2bCompressedInputPicture pad_right 2 (content 6)") {
    // W=6: last_col = 1, byte 1 holds pixels 4,5 (values 1,2) in bits 7-4 +
    // poison bits 3-0. 0x6F & 0xf0 = 0x60, last pixel (bits 5-4) = 2,
    // OR-fill (2<<2)|2 = 0x0A -> byte 1 = 0x6A.
    std::uint8_t buf[2 * 4];
    for (int i = 0; i < 2 * 4; ++i) {
        buf[i] = 0xFF;
    }
    buf[0] = 0x18;  // pixels 0..3 = 0,1,2,0
    buf[1] = 0x5F;  // pixels 4,5 = 1,1 + poison 3,3
    buf[4] = 0x18;
    buf[4 + 1] = 0x5F;

    analysis::pad2bCompressedInputPicture(buf, 4, 6, 2, 2, 0);

    CHECK(buf[0] == 0x18);
    CHECK(buf[1] == 0x55);
    CHECK(buf[2] == 0xFF);
    CHECK(buf[3] == 0xFF);
    CHECK(buf[4] == 0x18);
    CHECK(buf[5] == 0x55);
    CHECK(buf[6] == 0xFF);
    CHECK(buf[7] == 0xFF);
}

TEST_CASE("pad2bCompressedInputPicture pad_right 3 (content 5)") {
    // W=5: last_col = 1, byte 1 holds pixel 4 (value 1) in bits 7-6 +
    // poison bits 5-0. 0x7F & 0xc0 = 0x40, last pixel = 1, OR-fill
    // (1<<4)|(1<<2)|1 = 0x15 -> byte 1 = 0x55.
    std::uint8_t buf[2 * 4];
    for (int i = 0; i < 2 * 4; ++i) {
        buf[i] = 0xFF;
    }
    buf[0] = 0x18;  // pixels 0..3 = 0,1,2,0
    buf[1] = 0x7F;  // pixel 4 = 1 + poison 3,3,3
    buf[4] = 0x18;
    buf[4 + 1] = 0x7F;

    analysis::pad2bCompressedInputPicture(buf, 4, 5, 2, 3, 0);

    CHECK(buf[0] == 0x18);
    CHECK(buf[1] == 0x55);
    CHECK(buf[2] == 0xFF);
    CHECK(buf[3] == 0xFF);
    CHECK(buf[4] == 0x18);
    CHECK(buf[5] == 0x55);
    CHECK(buf[6] == 0xFF);
    CHECK(buf[7] == 0xFF);
}

TEST_CASE("pad2bCompressedInputPicture pad_right 4 reads the byte BEFORE the pad") {
    // W=4 (width % 4 == 0, the ONLY pad_right with this property): last_col
    // = 1 is the FIRST PURE-PAD byte, poisoned 0xFF here - the branch must
    // NOT read it. It reads src[last_col - 1] = byte 0 = 0x19, takes its
    // lowest 2 bits (pixel 3 = 1) and writes the replicated byte 0x55 at
    // src[1]. A mutant reading src[last_col] gets 0xFF & 3 = 3 and writes
    // 0xFF - indistinguishable from the poison, so the expectation 0x55
    // kills it.
    std::uint8_t buf[2 * 4];
    for (int i = 0; i < 2 * 4; ++i) {
        buf[i] = 0xFF;
    }
    buf[0] = 0x19;  // pixels 0..3 = 0,1,2,1
    // buf[1] stays poisoned: it must be WRITTEN, not READ
    buf[4] = 0x19;

    analysis::pad2bCompressedInputPicture(buf, 4, 4, 2, 4, 0);

    CHECK(buf[0] == 0x19);
    CHECK(buf[1] == 0x55);
    CHECK(buf[2] == 0xFF);
    CHECK(buf[3] == 0xFF);
    CHECK(buf[4] == 0x19);
    CHECK(buf[5] == 0x55);
    CHECK(buf[6] == 0xFF);
    CHECK(buf[7] == 0xFF);
}

TEST_CASE("pad2bCompressedInputPicture pad_right 5 (content 3)") {
    // W=3: last_col = 0, byte 0 holds pixels 0,1,2 (values 0,1,2) in bits
    // 7-2 + poison bits 1-0. 0x17 & 0xfc = 0x14, last pixel (bits 3-2) = 1,
    // OR-fill 1 -> byte 0 = 0x15; byte 1 = 1 replicated 4x = 0x55 (pixels
    // 4..7). pad_right 5 needs BOTH writes.
    std::uint8_t buf[2 * 4];
    for (int i = 0; i < 2 * 4; ++i) {
        buf[i] = 0xFF;
    }
    buf[0] = 0x17;  // pixels 0..2 = 0,1,1 + poison 3
    buf[4] = 0x17;

    analysis::pad2bCompressedInputPicture(buf, 4, 3, 2, 5, 0);

    CHECK(buf[0] == 0x15);
    CHECK(buf[1] == 0x55);
    CHECK(buf[2] == 0xFF);
    CHECK(buf[3] == 0xFF);
    CHECK(buf[4] == 0x15);
    CHECK(buf[5] == 0x55);
    CHECK(buf[6] == 0xFF);
    CHECK(buf[7] == 0xFF);
}

TEST_CASE("pad2bCompressedInputPicture pad_right 6 (content 2)") {
    // W=2: last_col = 0, byte 0 holds pixels 0,1 (values 0,1) in bits 7-4 +
    // poison bits 3-0. 0x1F & 0xf0 = 0x10, last pixel (bits 5-4) = 1,
    // OR-fill (1<<2)|1 = 0x05 -> byte 0 = 0x15; byte 1 = pixel 1 replicated
    // 4x = 0b01010101 = 0x55 (pixels 4..7).
    std::uint8_t buf[2 * 4];
    for (int i = 0; i < 2 * 4; ++i) {
        buf[i] = 0xFF;
    }
    buf[0] = 0x1F;  // pixels 0,1 = 0,1 + poison 3,3
    buf[4] = 0x1F;

    analysis::pad2bCompressedInputPicture(buf, 4, 2, 2, 6, 0);

    CHECK(buf[0] == 0x15);
    CHECK(buf[1] == 0x55);
    CHECK(buf[2] == 0xFF);
    CHECK(buf[3] == 0xFF);
    CHECK(buf[4] == 0x15);
    CHECK(buf[5] == 0x55);
    CHECK(buf[6] == 0xFF);
    CHECK(buf[7] == 0xFF);
}

TEST_CASE("pad2bCompressedInputPicture pad_right 7 (content 1)") {
    // W=1: last_col = 0, byte 0 holds pixel 0 (value 1) in bits 7-6 +
    // poison bits 5-0. 0x7F & 0xc0 = 0x40, last pixel = 1, OR-fill 1 ->
    // byte 0 = 0x55; byte 1 = pixel 1 replicated 4x = 0x55 (pixels 4..7).
    std::uint8_t buf[2 * 4];
    for (int i = 0; i < 2 * 4; ++i) {
        buf[i] = 0xFF;
    }
    buf[0] = 0x7F;  // pixel 0 = 1 + poison 3,3,3
    buf[4] = 0x7F;

    analysis::pad2bCompressedInputPicture(buf, 4, 1, 2, 7, 0);

    CHECK(buf[0] == 0x55);
    CHECK(buf[1] == 0x55);
    CHECK(buf[2] == 0xFF);
    CHECK(buf[3] == 0xFF);
    CHECK(buf[4] == 0x55);
    CHECK(buf[5] == 0x55);
    CHECK(buf[6] == 0xFF);
    CHECK(buf[7] == 0xFF);
}

TEST_CASE("pad2bCompressedInputPicture bottom copies (width + pad_right) / 4 bytes") {
    // W=7, pad_right 1, height 1, pad_bottom 2, stride 4. The padded row is
    // 8 px = 2 bytes, so the bottom copy count is (7 + 1) / 4 = 2 bytes per
    // pad row, each taken from the LAST CONTENT row (not chained). Row 0's
    // overhang bytes 2..3 carry sentinels 0x77/0x88: a copy sized to the
    // full stride (4) would drag them into the pad rows, where the poison
    // 0xEE must survive. The right pass runs first, so the copied byte 1 is
    // the POST-right-pad 0x65, not the pre-pad 0x67.
    std::uint8_t buf[3 * 4];
    for (int i = 0; i < 3 * 4; ++i) {
        buf[i] = 0xEE;
    }
    buf[0] = 0x18;  // pixels 0..3 = 0,1,2,0
    buf[1] = 0x67;  // pixels 4..6 = 1,2,1 + poison 3
    buf[2] = 0x77;
    buf[3] = 0x88;

    analysis::pad2bCompressedInputPicture(buf, 4, 7, 1, 1, 2);

    // right pass on the content row
    CHECK(buf[0] == 0x18);
    CHECK(buf[1] == 0x65);
    // the source row's overhang is untouched by both passes
    CHECK(buf[2] == 0x77);
    CHECK(buf[3] == 0x88);
    // both pad rows: exactly 2 bytes copied from the content row
    CHECK(buf[4] == 0x18);
    CHECK(buf[5] == 0x65);
    CHECK(buf[6] == 0xEE);
    CHECK(buf[7] == 0xEE);
    CHECK(buf[8] == 0x18);
    CHECK(buf[9] == 0x65);
    CHECK(buf[10] == 0xEE);
    CHECK(buf[11] == 0xEE);
}

TEST_CASE("padToMinBlkSizeDimensions content 9x9 at padded 16x16, luma only") {
    // THE CONTRACT TEST. resource_coordination_process.c:717-719:
    // pad_right = MIN_BLOCK_SIZE - (width % MIN_BLOCK_SIZE) = 8 - (9 % 8) =
    // 7, and max_input_luma_width is UPDATED IN PLACE to 9 + 7 = 16. So the
    // driver receives width = 16 (PADDED) and padRight = 7; the content is
    // width - padRight = 9x9 (pic_analysis_process.c:756). A driver that
    // treated width=16 as content would replicate the POISON at column 15
    // instead of content pixel (8,y) - caught below.
    std::uint8_t buf[16 * 16];
    for (int i = 0; i < 16 * 16; ++i) {
        buf[i] = 0xEE;
    }
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 9; ++x) {
            buf[y * 16 + x] = static_cast<std::uint8_t>(x + 10 * y);
        }
    }

    analysis::padToMinBlkSizeDimensions(buf, 16, nullptr, 0, nullptr, 0, nullptr, nullptr,
                                        nullptr, 16, 16, 7, 7, false,
                                        analysis::ColorFormat::Yuv420);

    // content pixels untouched
    CHECK(buf[4 * 16 + 4] == 44);
    CHECK(buf[8 * 16 + 8] == 88);
    // right pad: the LAST CONTENT pixel of each row, not column 15's poison
    CHECK(buf[0 * 16 + 9] == 8);
    CHECK(buf[3 * 16 + 15] == 38);
    CHECK(buf[5 * 16 + 12] == 58);
    // bottom pad rows: the last content row (row 8) INCLUDING its right pad
    CHECK(buf[9 * 16 + 3] == 83);
    CHECK(buf[9 * 16 + 10] == 88);
    CHECK(buf[12 * 16 + 5] == 85);
    CHECK(buf[12 * 16 + 15] == 88);
    // the far corner
    CHECK(buf[15 * 16 + 15] == 88);
}

TEST_CASE("padToMinBlkSizeDimensions chroma 4:2:0 at content 9x9") {
    // Chroma arithmetic at ss_x = ss_y = 1 (the 8-bit driver derives
    // subsampling from color_format, pic_analysis_process.c:750-751):
    // chroma content = (width + ss_x - padRight) >> ss_x = (16 + 1 - 7) >> 1
    // = 5 = ceil(9/2) (:764) and chroma pad = padRight >> ss_x = 7 >> 1 = 3
    // (:766) - floor(7/2), consistent because pad_right = (-content) mod 8
    // has the parity of content, so content + chroma pad = 8 = the chroma
    // plane width. The chroma planes are 8x8, poisoned 0xDD.
    std::uint8_t y[16 * 16];
    std::uint8_t u[8 * 8];
    std::uint8_t v[8 * 8];
    for (int i = 0; i < 16 * 16; ++i) {
        y[i] = 0xEE;
    }
    for (int i = 0; i < 8 * 8; ++i) {
        u[i] = 0xDD;
        v[i] = 0xCC;
    }
    for (int yy = 0; yy < 9; ++yy) {
        for (int xx = 0; xx < 9; ++xx) {
            y[yy * 16 + xx] = static_cast<std::uint8_t>(xx + 10 * yy);
        }
    }
    for (int yy = 0; yy < 5; ++yy) {
        for (int xx = 0; xx < 5; ++xx) {
            u[yy * 8 + xx] = static_cast<std::uint8_t>(100 + xx + 10 * yy);
            v[yy * 8 + xx] = static_cast<std::uint8_t>(150 + xx + 10 * yy);
        }
    }

    analysis::padToMinBlkSizeDimensions(y, 16, u, 8, v, 8, nullptr, nullptr, nullptr,
                                        16, 16, 7, 7, false,
                                        analysis::ColorFormat::Yuv420);

    // chroma right pad: the LAST CONTENT chroma pixel of each row
    CHECK(u[0 * 8 + 5] == 104);
    CHECK(u[2 * 8 + 7] == 124);
    CHECK(v[1 * 8 + 6] == 164);
    // chroma bottom pad: the last content chroma row incl. its right pad
    CHECK(u[5 * 8 + 2] == 142);
    CHECK(u[5 * 8 + 6] == 144);
    CHECK(u[7 * 8 + 7] == 144);
    CHECK(v[6 * 8 + 3] == 193);
    CHECK(v[7 * 8 + 7] == 194);
}

TEST_CASE("padToMinBlkSizeDimensions chroma-null (monochrome) is a no-op") {
    // The :761/:770 guards: null u/v buffers skip the chroma padding. The
    // luma pass must still run (the canary row asserts it did).
    std::uint8_t y[16 * 16];
    for (int i = 0; i < 16 * 16; ++i) {
        y[i] = 0xEE;
    }
    for (int yy = 0; yy < 9; ++yy) {
        for (int xx = 0; xx < 9; ++xx) {
            y[yy * 16 + xx] = static_cast<std::uint8_t>(xx + 10 * yy);
        }
    }

    analysis::padToMinBlkSizeDimensions(y, 16, nullptr, 0, nullptr, 0, nullptr, nullptr,
                                        nullptr, 16, 16, 7, 7, false,
                                        analysis::ColorFormat::Yuv400);

    CHECK(y[0 * 16 + 9] == 8);
    CHECK(y[15 * 16 + 15] == 88);
}

TEST_CASE("padToMinBlkSizeDimensions pads the 2b bit-inc planes when is16BitInput") {
    // The :779-807 branches: is16_bit_input true routes the bit_inc buffers
    // through pad_2b_compressed_input_picture with the comp strides computed
    // INTERNALLY (y_stride/4 = 4, u_stride/4 = 2 - the :780-782 arithmetic).
    // Luma comp content 9 wide, pad 7 -> the ==7 branch: byte 2 holds pixel
    // 8 (value 2, bits 7-6) + poison; 0xBF & 0xc0 = 0x80, fill 2 -> 0xAA,
    // byte 3 = 0xAA. Chroma comp content 5 wide, pad 3 -> the ==3 branch:
    // byte 1 holds pixel 4 (value 1) + poison; 0x7F -> 0x55.
    std::uint8_t yBit[16 * 4];
    std::uint8_t uBit[8 * 2];
    std::uint8_t vBit[8 * 2];
    for (int i = 0; i < 16 * 4; ++i) {
        yBit[i] = 0xBF;
    }
    for (int i = 0; i < 8 * 2; ++i) {
        uBit[i] = 0x7F;
        vBit[i] = 0x7F;
    }
    for (int r = 0; r < 9; ++r) {
        yBit[r * 4 + 0] = 0x18;
        yBit[r * 4 + 1] = 0x65;
    }
    for (int r = 0; r < 5; ++r) {
        uBit[r * 2 + 0] = 0x18;
        vBit[r * 2 + 0] = 0x18;
    }

    analysis::padToMinBlkSizeDimensions(nullptr, 16, nullptr, 8, nullptr, 8,
                                        yBit, uBit, vBit, 16, 16, 7, 7, true,
                                        analysis::ColorFormat::Yuv420);

    // luma comp rows 0 and 8 (content) and a bottom pad row: identical
    const int lumaRows[3] = {0, 8, 12};
    for (int i = 0; i < 3; ++i) {
        const int r = lumaRows[i];
        CHECK(yBit[r * 4 + 0] == 0x18);
        CHECK(yBit[r * 4 + 1] == 0x65);
        CHECK(yBit[r * 4 + 2] == 0xAA);
        CHECK(yBit[r * 4 + 3] == 0xAA);
    }
    // chroma comp: content row and bottom pad row
    const int chromaRows[3] = {0, 4, 6};
    for (int i = 0; i < 3; ++i) {
        const int r = chromaRows[i];
        CHECK(uBit[r * 2 + 0] == 0x18);
        CHECK(uBit[r * 2 + 1] == 0x55);
        CHECK(vBit[r * 2 + 0] == 0x18);
        CHECK(vBit[r * 2 + 1] == 0x55);
    }
}

TEST_CASE("padToMinBlkSizeDimensions16bit content 9x9 at padded 16x16, 4:2:0") {
    // The uint16 driver (:820-852): same contract (padded width + padRight
    // in, content derived), same chroma arithmetic, uint16 elements with
    // stride in ELEMENTS. Values above 8 bits prove uint16 semantics. The
    // reference casts the uint8_t* buffer desc fields to uint16_t*; the
    // port takes uint16_t* directly (named deviation).
    std::uint16_t y[16 * 16];
    std::uint16_t u[8 * 8];
    std::uint16_t v[8 * 8];
    for (int i = 0; i < 16 * 16; ++i) {
        y[i] = 0xEEEE;
    }
    for (int i = 0; i < 8 * 8; ++i) {
        u[i] = 0xDDDD;
        v[i] = 0xCCCC;
    }
    for (int yy = 0; yy < 9; ++yy) {
        for (int xx = 0; xx < 9; ++xx) {
            y[yy * 16 + xx] = static_cast<std::uint16_t>(0x100 + xx + 10 * yy);
        }
    }
    for (int yy = 0; yy < 5; ++yy) {
        for (int xx = 0; xx < 5; ++xx) {
            u[yy * 8 + xx] = static_cast<std::uint16_t>(0x200 + xx + 10 * yy);
            v[yy * 8 + xx] = static_cast<std::uint16_t>(0x300 + xx + 10 * yy);
        }
    }

    analysis::padToMinBlkSizeDimensions16bit(y, 16, u, 8, v, 8, 16, 16, 7, 7,
                                             analysis::ColorFormat::Yuv420);

    // luma content untouched, right pad = last content pixel per row
    CHECK(y[4 * 16 + 4] == 0x12C);
    CHECK(y[0 * 16 + 9] == 0x108);
    CHECK(y[3 * 16 + 15] == 0x100 + 38);
    // bottom pad rows carry the last content row incl. right pad
    CHECK(y[9 * 16 + 3] == 0x153);
    CHECK(y[12 * 16 + 15] == 0x158);
    CHECK(y[15 * 16 + 15] == 0x158);
    // chroma content 5, pad 3
    CHECK(u[0 * 8 + 5] == 0x204);
    CHECK(u[2 * 8 + 7] == 0x218);
    CHECK(u[5 * 8 + 6] == 0x22C);
    CHECK(v[1 * 8 + 6] == 0x30E);
    CHECK(v[7 * 8 + 7] == 0x32C);
}

TEST_CASE("padToSbDimensions maps border to both padding width and height") {
    // The thin wrapper (:858-866): generate_padding on the Y plane only,
    // with padding_width = padding_height = border. Width 8, height 4,
    // border 2, stride 12: TWO pad rows above and TWO below. A mutant
    // passing border as width only (height 0) leaves the top/bottom pad
    // rows poisoned.
    std::uint8_t buf[8 * 12];
    for (int i = 0; i < 8 * 12; ++i) {
        buf[i] = 0xEE;
    }
    for (int yy = 0; yy < 4; ++yy) {
        for (int xx = 0; xx < 8; ++xx) {
            buf[(yy + 2) * 12 + xx + 2] = static_cast<std::uint8_t>(xx + 10 * yy);
        }
    }

    analysis::padToSbDimensions(buf + 2 * 12 + 2, 12, 8, 4, 2);

    // horizontal: left/right pads of the content rows
    CHECK(buf[2 * 12 + 0] == 0);
    CHECK(buf[2 * 12 + 1] == 0);
    CHECK(buf[2 * 12 + 10] == 7);
    CHECK(buf[2 * 12 + 11] == 7);
    // vertical: the four pad rows replicate the fully padded edge rows;
    // the top rows carry row 2's pads (left 0, right 7), the bottom rows
    // row 5's (left 30, right 37)
    const int padRowBases[2] = {0, 6};
    const int leftPads[2] = {0, 30};
    const int rightPads[2] = {7, 37};
    for (int i = 0; i < 2; ++i) {
        for (int rr = padRowBases[i]; rr < padRowBases[i] + 2; ++rr) {
            CHECK(buf[rr * 12 + 0] == leftPads[i]);
            CHECK(buf[rr * 12 + 1] == leftPads[i]);
            CHECK(buf[rr * 12 + 10] == rightPads[i]);
            CHECK(buf[rr * 12 + 11] == rightPads[i]);
        }
    }
    CHECK(buf[0 * 12 + 2] == 0);
    CHECK(buf[7 * 12 + 9] == 37);
}

TEST_CASE("padInputPictures: edge pad first, then border pad around the padded extent") {
    // The composition (:1549-1615): pad_picture_to_multiple_of_min_blk_size
    // FIRST (content 9x9 at padded 16x16, padRight/padBottom 7 - the scs
    // quantities), THEN generate_padding around the padded extent (width 16,
    // height 16, border 2 - the input_pic->border quantity, a DIFFERENT
    // source). The corner checks catch an order bug: the top border row is
    // copied AFTER the min-blk right pad exists, so its column border+9
    // reads pixel(8,y), not poison.
    std::uint8_t y[20 * 20];
    std::uint8_t u[10 * 10];
    std::uint8_t v[10 * 10];
    for (int i = 0; i < 20 * 20; ++i) {
        y[i] = 0xEE;
    }
    for (int i = 0; i < 10 * 10; ++i) {
        u[i] = 0xDD;
        v[i] = 0xCC;
    }
    for (int yy = 0; yy < 9; ++yy) {
        for (int xx = 0; xx < 9; ++xx) {
            y[(yy + 2) * 20 + xx + 2] = static_cast<std::uint8_t>(xx + 10 * yy);
        }
    }
    for (int yy = 0; yy < 5; ++yy) {
        for (int xx = 0; xx < 5; ++xx) {
            u[(yy + 1) * 10 + xx + 1] = static_cast<std::uint8_t>(100 + xx + 10 * yy);
            v[(yy + 1) * 10 + xx + 1] = static_cast<std::uint8_t>(150 + xx + 10 * yy);
        }
    }

    // the content ORIGIN is at (border, border) inside the array: the
    // border passes write at src - border and rows above it
    analysis::padInputPictures(y + 2 * 20 + 2, 20, u + 1 * 10 + 1, 10, v + 1 * 10 + 1, 10,
                               nullptr, nullptr, nullptr,
                               16, 16, 7, 7, 2, 1, 1, false,
                               analysis::ColorFormat::Yuv420);

    // content untouched
    CHECK(y[5 * 20 + 5] == 33);
    // top border row = copy of padded row 0 AFTER its min-blk right pad:
    // col 2+9 = pixel(8,0) = 8 (an order bug leaves 0xEE here)
    CHECK(y[0 * 20 + 0] == 0);
    CHECK(y[0 * 20 + 9] == 7);
    CHECK(y[0 * 20 + 11] == 8);
    // bottom border row = copy of padded row 15 (= the min-blk bottom pad)
    CHECK(y[19 * 20 + 0] == 80);
    CHECK(y[19 * 20 + 11] == 88);
    CHECK(y[19 * 20 + 19] == 88);
    // horizontal border pads of a middle padded row (row 8 = content row 6)
    CHECK(y[8 * 20 + 0] == 60);
    CHECK(y[8 * 20 + 19] == 68);
    // chroma: border >> subsampling = 1; the chroma border row also sees
    // the post-edge-pad row (col 1+5 = uc(4,0) = 104)
    CHECK(u[0 * 10 + 0] == 100);
    CHECK(u[0 * 10 + 6] == 104);
    CHECK(u[9 * 10 + 9] == 144);
    CHECK(v[0 * 10 + 6] == 154);
    CHECK(v[9 * 10 + 0] == 190);
}

TEST_CASE("padInputPictures: comp border rows - full border on luma, border >> ss on chroma") {
    // The :1560-1573 / :1594-1614 comp passes: the LUMA comp plane takes
    // the FULL pixel border with the packed stride (padding_height = 2
    // comp rows; padding_width = 2 px -> 2/4 = 0 horizontal bytes), the
    // chroma comp planes take border >> ss = 1 row. The 8-bit planes are
    // null here; their guards make that a no-op.
    std::uint8_t yBit[20 * 4];
    std::uint8_t uBit[10 * 2];
    std::uint8_t vBit[10 * 2];
    for (int i = 0; i < 20 * 4; ++i) {
        yBit[i] = 0xEE;
    }
    for (int i = 0; i < 10 * 2; ++i) {
        uBit[i] = 0xEE;
        vBit[i] = 0xEE;
    }
    // content comp rows AFTER the min-blk edge pad (the composition runs
    // the edge pad first, so these bytes are its output)
    for (int r = 2; r < 18; ++r) {
        yBit[r * 4 + 0] = 0x18;
        yBit[r * 4 + 1] = 0x65;
        yBit[r * 4 + 2] = 0xAA;
        yBit[r * 4 + 3] = 0xAA;
    }
    for (int r = 1; r < 9; ++r) {
        uBit[r * 2 + 0] = 0x18;
        uBit[r * 2 + 1] = 0x55;
        vBit[r * 2 + 0] = 0x18;
        vBit[r * 2 + 1] = 0x55;
    }

    // the comp content ORIGIN sits border rows in (2 comp rows luma, 1
    // chroma): the border passes write above it
    analysis::padInputPictures(nullptr, 16, nullptr, 8, nullptr, 8,
                               yBit + 2 * 4, uBit + 1 * 2, vBit + 1 * 2,
                               16, 16, 7, 7, 2, 1, 1, true,
                               analysis::ColorFormat::Yuv420);

    // luma comp: two border rows above and below, each a full 4-byte copy
    for (int r = 0; r < 20; ++r) {
        CHECK(yBit[r * 4 + 0] == 0x18);
        CHECK(yBit[r * 4 + 1] == 0x65);
        CHECK(yBit[r * 4 + 2] == 0xAA);
        CHECK(yBit[r * 4 + 3] == 0xAA);
    }
    // chroma comp: one border row above and below
    for (int r = 0; r < 10; ++r) {
        CHECK(uBit[r * 2 + 0] == 0x18);
        CHECK(uBit[r * 2 + 1] == 0x55);
        CHECK(vBit[r * 2 + 0] == 0x18);
        CHECK(vBit[r * 2 + 1] == 0x55);
    }
}
