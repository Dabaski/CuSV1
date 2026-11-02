#include <doctest.h>
#include <analysis.h>

// 1:1 target: svt_aom_downsample_filtering_input_picture
// (third_party/SVT-AV1/Source/Lib/Codec/pic_analysis_process.c:1498-1547),
// the last Phase-A slice (A0 register, docs/todo.md). A 50-line DRIVER that
// COMPOSES two already-landed l9 primitives: downsample2d (AN6,
// pic_analysis_process.c:134-164) and generatePadding (AN10,
// pic_operators.c:266-301) - the port is branch structure and call order.
// Declaration twin: pic_analysis_process.h:28. resize.c:139 carries the
// same signature and is a DECLARATION (semicolon at resize.c:141, no body);
// the reference's only definition is pic_analysis_process.c:1498 - a future
// reader need not wonder. Only the Y plane is touched. The six pcs booleans
// are ORed in pairs inside, mirroring the reference's reads (:1502/:1503/
// :1519/:1521). The reference has NO null guards - the branch structure IS
// the guard; the port adds none.
//
// CALLERS (grep-verified): pd_process.c:3555 and
// pic_analysis_process.c:1992 (gated on !scs->allintra) are the LIVE call
// sites; resize.c:1665/:1726 are the parked Phase-D superres paths and
// temporal_filtering.c:3770 the parked Phase-C driver. All five pass the
// same three-buffer shape; the flags live inside pcs, read by the function
// itself, so the port threads the six raw booleans.
//
// ITEM-2 FINDING (recorded here because it shapes what the tests can pin):
// the sixteenth-from-quarter pass reads NO quarter pad rows. downsample2d's
// window sits at rows {half-1, half} WITHIN each cell (the pre-advance
// curLine = src + half*stride, prevLine = curLine - stride; the first cell
// reads content rows 0 and 1), and the reference passes the quarter CONTENT
// dims, so every row/col touched is content. The padding ORDER (quarter
// padding inside the level1 branch, BEFORE the sixteenth pass, :1511 before
// :1522) is therefore pinned by the reference TEXT alone, not by an
// observable test - swapping the two statements changes no output.
//
// FIXTURE GEOMETRY (why the buffers are oversized): the SVT buffers carry
// border; the port threads content pointers. The quarter buffer is 32x32
// with its content at (6,6) - border 4 written, 2 rows/cols of slack stay
// poisoned 0xEE, so a wrong-border mutation writes INSIDE the buffer (no
// UB) and fails the full-buffer compare. Same shape for sixteenth: 20x20,
// content at (5,5), border 2 written, slack poisoned. The input plane is
// the padded 32x32 with no border needed: downsample2d reads content rows
// {vi-1, vi} for odd vi in [1, 32) - all content.

namespace {

// Deterministic input content, deliberately NON-LINEAR: the (x*y) term
// breaks the two-stage/one-stage coincidence. With the linear first draft
// (x + 4y), the step-2-of-quarter decimation is BIT-IDENTICAL to the
// step-4 middle-window decimation of the input at every cell (a linear
// field makes the two-stage average exact), so the level1-source test
// could not tell the two paths apart - the fixture was blind exactly the
// way the AN10 register's mask lesson describes. The product term makes
// the two paths disagree.
std::uint8_t inputPixel(std::uint32_t x, std::uint32_t y) {
    return static_cast<std::uint8_t>((x * 37 + y * 11 + ((x * y) & 0x1F)) & 0xFF);
}

// The landed downsample2d contract (analysis.cpp:1216-1227): the window is
// the 2x2 at cols/rows {half-1, half} of each decim_step cell, output
// (sum + 2) >> 2. At step 2 that is the cell's TOP-LEFT 2x2, at step 4 its
// MIDDLE 2x2. These helpers mirror that contract for the test's expected
// values; downsample2d itself is gate-proven (AN6).
std::uint8_t decim2(const std::uint8_t* src, std::uint32_t stride, std::uint32_t cx,
                    std::uint32_t cy) {
    const std::uint32_t x = 2 * cx;
    const std::uint32_t y = 2 * cy;
    const std::uint32_t sum = src[y * stride + x] + src[y * stride + x + 1] +
        src[(y + 1) * stride + x] + src[(y + 1) * stride + x + 1];
    return static_cast<std::uint8_t>((sum + 2) >> 2);
}

std::uint8_t decim4(const std::uint8_t* src, std::uint32_t stride, std::uint32_t cx,
                    std::uint32_t cy) {
    const std::uint32_t x = 4 * cx + 1;
    const std::uint32_t y = 4 * cy + 1;
    const std::uint32_t sum = src[y * stride + x] + src[y * stride + x + 1] +
        src[(y + 1) * stride + x] + src[(y + 1) * stride + x + 1];
    return static_cast<std::uint8_t>((sum + 2) >> 2);
}

// Expected buffer for a generatePadding pass: poison everywhere, then the
// content with its horizontal edge replication (left pad = row's first
// content pixel, right pad = row's last), then the vertical pass
// replicating the FULLY PADDED first/last content rows over the full
// stride (the overhang bytes inside the stride copy as poison, exactly as
// the reference's row_bytes = src_stride copy does). Content is a callable
// (cx, cy) -> pixel.
template <typename F>
void buildExpectedPadded(std::uint8_t* dst, std::uint32_t stride,
                         std::uint32_t contentX, std::uint32_t contentY,
                         std::uint32_t w, std::uint32_t h, std::uint32_t border,
                         std::uint32_t bufBytes, F content) {
    for (std::uint32_t i = 0; i < bufBytes; ++i) {
        dst[i] = 0xEE;
    }
    for (std::uint32_t r = 0; r < h; ++r) {
        std::uint8_t* rowBase = dst + (contentY + r) * stride;
        for (std::uint32_t i = 0; i < border; ++i) {
            rowBase[contentX - border + i] = content(0, r);
        }
        for (std::uint32_t c = 0; c < w; ++c) {
            rowBase[contentX + c] = content(c, r);
        }
        for (std::uint32_t i = 0; i < border; ++i) {
            rowBase[contentX + w + i] = content(w - 1, r);
        }
    }
    const std::uint8_t* topRow = dst + contentY * stride;
    const std::uint8_t* bottomRow = dst + (contentY + h - 1) * stride;
    for (std::uint32_t i = 1; i <= border; ++i) {
        std::uint8_t* topDst = dst + (contentY - i) * stride;
        std::uint8_t* bottomDst = dst + (contentY + h - 1 + i) * stride;
        for (std::uint32_t c = 0; c < stride; ++c) {
            topDst[c] = topRow[c];
            bottomDst[c] = bottomRow[c];
        }
    }
}

// Compares the whole buffer byte by byte (stronger than spot checks: any
// unexpected write inside the buffer fails, any missed write leaves the
// 0xEE poison visible).
void checkBuffer(const std::uint8_t* actual, const std::uint8_t* expected,
                 std::uint32_t bufBytes, const char* name) {
    for (std::uint32_t i = 0; i < bufBytes; ++i) {
        if (actual[i] != expected[i]) {
            INFO(name << " byte " << i << ": actual " << static_cast<int>(actual[i])
                      << ", expected " << static_cast<int>(expected[i]));
            REQUIRE(false);
        }
    }
    CHECK(true);
}

}  // namespace

TEST_CASE("downsampleFilteringInputPicture all-flags-off is a no-op") {
    std::uint8_t inputBuf[32 * 32];
    std::uint8_t quarterBuf[32 * 32];
    std::uint8_t sixteenthBuf[20 * 20];
    for (int i = 0; i < 32 * 32; ++i) {
        inputBuf[i] = inputPixel(static_cast<std::uint32_t>(i % 32),
                                 static_cast<std::uint32_t>(i / 32));
    }
    for (int i = 0; i < 32 * 32; ++i) {
        quarterBuf[i] = 0xEE;
    }
    for (int i = 0; i < 20 * 20; ++i) {
        sixteenthBuf[i] = 0xEE;
    }

    analysis::downsampleFilteringInputPicture(
        inputBuf, 32, 32, 32,
        quarterBuf + 6 * 32 + 6, 32, 16, 16, 4,
        sixteenthBuf + 5 * 20 + 5, 20, 8, 8, 2,
        false, false, false, false, false, false);

    for (int i = 0; i < 32 * 32; ++i) {
        CHECK(quarterBuf[i] == 0xEE);
    }
    for (int i = 0; i < 20 * 20; ++i) {
        CHECK(sixteenthBuf[i] == 0xEE);
    }
}

// The hme-side OR shapes used across the fixture cases below (the tf-side
// twins appear in their own cases - the OR pairs must hold on BOTH sides).
TEST_CASE("downsampleFilteringInputPicture level1 only: quarter decimated and padded, sixteenth untouched") {
    std::uint8_t inputBuf[32 * 32];
    std::uint8_t quarterBuf[32 * 32];
    std::uint8_t sixteenthBuf[20 * 20];
    std::uint8_t expectedQuarter[32 * 32];
    for (int i = 0; i < 32 * 32; ++i) {
        inputBuf[i] = inputPixel(static_cast<std::uint32_t>(i % 32),
                                 static_cast<std::uint32_t>(i / 32));
        quarterBuf[i] = 0xEE;
    }
    for (int i = 0; i < 20 * 20; ++i) {
        sixteenthBuf[i] = 0xEE;
    }

    analysis::downsampleFilteringInputPicture(
        inputBuf, 32, 32, 32,
        quarterBuf + 6 * 32 + 6, 32, 16, 16, 4,
        sixteenthBuf + 5 * 20 + 5, 20, 8, 8, 2,
        true, false, true, false, false, false);

    // quarter = step-2 decimation of INPUT (16x16), padded with the
    // QUARTER buffer's own border 4 (:1504-1516 - dims and border from
    // quarter_picture_ptr). Border 4 is written; the (6,6) content origin
    // leaves cols/rows 0-1 as poisoned slack, so a wrong-border argument
    // (e.g. the sixteenth border 2) shows up as untouched poison inside
    // the compare.
    buildExpectedPadded(expectedQuarter, 32, 6, 6, 16, 16, 4, 32 * 32,
                        [&](std::uint32_t cx, std::uint32_t cy) {
                            return decim2(inputBuf, 32, cx, cy);
                        });
    checkBuffer(quarterBuf, expectedQuarter, 32 * 32, "quarter");

    // level0 off: sixteenth must be untouched poison.
    for (int i = 0; i < 20 * 20; ++i) {
        CHECK(sixteenthBuf[i] == 0xEE);
    }
}

TEST_CASE("downsampleFilteringInputPicture full chain: sixteenth is step-2 of the QUARTER content, own border") {
    std::uint8_t inputBuf[32 * 32];
    std::uint8_t quarterBuf[32 * 32];
    std::uint8_t sixteenthBuf[20 * 20];
    std::uint8_t expectedQuarter[32 * 32];
    std::uint8_t expectedSixteenth[20 * 20];
    for (int i = 0; i < 32 * 32; ++i) {
        inputBuf[i] = inputPixel(static_cast<std::uint32_t>(i % 32),
                                 static_cast<std::uint32_t>(i / 32));
        quarterBuf[i] = 0xEE;
    }
    for (int i = 0; i < 20 * 20; ++i) {
        sixteenthBuf[i] = 0xEE;
    }

    analysis::downsampleFilteringInputPicture(
        inputBuf, 32, 32, 32,
        quarterBuf + 6 * 32 + 6, 32, 16, 16, 4,
        sixteenthBuf + 5 * 20 + 5, 20, 8, 8, 2,
        true, false, true, false, true, false);

    // The quarter half is T2's expectation, re-checked here because the
    // sixteenth expectation is derived FROM it.
    buildExpectedPadded(expectedQuarter, 32, 6, 6, 16, 16, 4, 32 * 32,
                        [&](std::uint32_t cx, std::uint32_t cy) {
                            return decim2(inputBuf, 32, cx, cy);
                        });
    checkBuffer(quarterBuf, expectedQuarter, 32 * 32, "quarter");

    // Sixteenth = step-2 decimation of the QUARTER CONTENT (the reference
    // reads quarter_picture_ptr->y_buffer/:width/:height, :1522-1528),
    // padded with the SIXTEENTH buffer's own border 2 (:1539-1544). A
    // wrong-source mutant (decimating INPUT at step 2) produces different
    // averages; the level1-off path (step-4 of input) is T4's case.
    buildExpectedPadded(expectedSixteenth, 20, 5, 5, 8, 8, 2, 20 * 20,
                        [&](std::uint32_t cx, std::uint32_t cy) {
                            return decim2(expectedQuarter + 6 * 32 + 6, 32, cx, cy);
                        });
    checkBuffer(sixteenthBuf, expectedSixteenth, 20 * 20, "sixteenth");
}

TEST_CASE("downsampleFilteringInputPicture level0 with level1 off: sixteenth is step-4 of INPUT, quarter unread") {
    std::uint8_t inputBuf[32 * 32];
    std::uint8_t quarterBuf[32 * 32];
    std::uint8_t sixteenthBuf[20 * 20];
    std::uint8_t expectedSixteenth[20 * 20];
    for (int i = 0; i < 32 * 32; ++i) {
        inputBuf[i] = inputPixel(static_cast<std::uint32_t>(i % 32),
                                 static_cast<std::uint32_t>(i / 32));
        quarterBuf[i] = 0xEE;
    }
    for (int i = 0; i < 20 * 20; ++i) {
        sixteenthBuf[i] = 0xEE;
    }

    analysis::downsampleFilteringInputPicture(
        inputBuf, 32, 32, 32,
        quarterBuf + 6 * 32 + 6, 32, 16, 16, 4,
        sixteenthBuf + 5 * 20 + 5, 20, 8, 8, 2,
        true, false, false, false, true, false);

    // level1 off: the quarter buffer is NEVER touched - poisoned
    // throughout, so a wrong-source mutant (reading the quarter) decimates
    // 0xEE poison and fails the sixteenth compare below (:1529-1536: the
    // level1-off source is the INPUT at decim_step 4).
    for (int i = 0; i < 32 * 32; ++i) {
        CHECK(quarterBuf[i] == 0xEE);
    }

    // Sixteenth = step-4 decimation of INPUT (window = the MIDDLE 2x2 of
    // each 4x4 cell), padded with the sixteenth border 2.
    buildExpectedPadded(expectedSixteenth, 20, 5, 5, 8, 8, 2, 20 * 20,
                        [&](std::uint32_t cx, std::uint32_t cy) {
                            return decim4(inputBuf, 32, cx, cy);
                        });
    checkBuffer(sixteenthBuf, expectedSixteenth, 20 * 20, "sixteenth");
}

// The tf-side twins: the reference ORs enable_hme_flag ||
// tf_enable_hme_flag (:1502) and the same pairing on level1 (:1503) and
// level0 (:1519). These cases hold the SAME expectations through the tf
// side of each OR; dropping the tf operand is caught here.
TEST_CASE("downsampleFilteringInputPicture tf-side level1 only matches the hme-side case") {
    std::uint8_t inputBuf[32 * 32];
    std::uint8_t quarterBuf[32 * 32];
    std::uint8_t sixteenthBuf[20 * 20];
    std::uint8_t expectedQuarter[32 * 32];
    for (int i = 0; i < 32 * 32; ++i) {
        inputBuf[i] = inputPixel(static_cast<std::uint32_t>(i % 32),
                                 static_cast<std::uint32_t>(i / 32));
        quarterBuf[i] = 0xEE;
    }
    for (int i = 0; i < 20 * 20; ++i) {
        sixteenthBuf[i] = 0xEE;
    }

    analysis::downsampleFilteringInputPicture(
        inputBuf, 32, 32, 32,
        quarterBuf + 6 * 32 + 6, 32, 16, 16, 4,
        sixteenthBuf + 5 * 20 + 5, 20, 8, 8, 2,
        false, true, false, true, false, false);

    buildExpectedPadded(expectedQuarter, 32, 6, 6, 16, 16, 4, 32 * 32,
                        [&](std::uint32_t cx, std::uint32_t cy) {
                            return decim2(inputBuf, 32, cx, cy);
                        });
    checkBuffer(quarterBuf, expectedQuarter, 32 * 32, "quarter");
    for (int i = 0; i < 20 * 20; ++i) {
        CHECK(sixteenthBuf[i] == 0xEE);
    }
}

TEST_CASE("downsampleFilteringInputPicture tf-side level0 with level1 off matches the hme-side case") {
    std::uint8_t inputBuf[32 * 32];
    std::uint8_t quarterBuf[32 * 32];
    std::uint8_t sixteenthBuf[20 * 20];
    std::uint8_t expectedSixteenth[20 * 20];
    for (int i = 0; i < 32 * 32; ++i) {
        inputBuf[i] = inputPixel(static_cast<std::uint32_t>(i % 32),
                                 static_cast<std::uint32_t>(i / 32));
        quarterBuf[i] = 0xEE;
    }
    for (int i = 0; i < 20 * 20; ++i) {
        sixteenthBuf[i] = 0xEE;
    }

    analysis::downsampleFilteringInputPicture(
        inputBuf, 32, 32, 32,
        quarterBuf + 6 * 32 + 6, 32, 16, 16, 4,
        sixteenthBuf + 5 * 20 + 5, 20, 8, 8, 2,
        false, true, false, false, false, true);

    for (int i = 0; i < 32 * 32; ++i) {
        CHECK(quarterBuf[i] == 0xEE);
    }
    buildExpectedPadded(expectedSixteenth, 20, 5, 5, 8, 8, 2, 20 * 20,
                        [&](std::uint32_t cx, std::uint32_t cy) {
                            return decim4(inputBuf, 32, cx, cy);
                        });
    checkBuffer(sixteenthBuf, expectedSixteenth, 20 * 20, "sixteenth");
}

// The outer gate: with BOTH hme flags off, every level flag on must still
// write nothing (:1502 - the branch structure IS the guard; the reference
// has no separate null/no-op path).
TEST_CASE("downsampleFilteringInputPicture hme and tf-hme off with all levels on writes nothing") {
    std::uint8_t inputBuf[32 * 32];
    std::uint8_t quarterBuf[32 * 32];
    std::uint8_t sixteenthBuf[20 * 20];
    for (int i = 0; i < 32 * 32; ++i) {
        inputBuf[i] = inputPixel(static_cast<std::uint32_t>(i % 32),
                                 static_cast<std::uint32_t>(i / 32));
        quarterBuf[i] = 0xEE;
    }
    for (int i = 0; i < 20 * 20; ++i) {
        sixteenthBuf[i] = 0xEE;
    }

    analysis::downsampleFilteringInputPicture(
        inputBuf, 32, 32, 32,
        quarterBuf + 6 * 32 + 6, 32, 16, 16, 4,
        sixteenthBuf + 5 * 20 + 5, 20, 8, 8, 2,
        false, false, true, true, true, true);

    for (int i = 0; i < 32 * 32; ++i) {
        CHECK(quarterBuf[i] == 0xEE);
    }
    for (int i = 0; i < 20 * 20; ++i) {
        CHECK(sixteenthBuf[i] == 0xEE);
    }
}

// The level1 OR is read TWICE in the reference (:1503 gates the quarter
// pass, :1521 selects the sixteenth SOURCE). This case enables level1 via
// the TF flag ONLY while level0 is on via the hme flag: BOTH reads must
// see true - the quarter pass runs AND the sixteenth takes the
// step-2-from-QUARTER source. Mutating either read (dropping the tf
// operand at :1503, or selecting the source off the level0 pair) breaks
// this case.
TEST_CASE("downsampleFilteringInputPicture level1 via tf only: both level1 reads see it") {
    std::uint8_t inputBuf[32 * 32];
    std::uint8_t quarterBuf[32 * 32];
    std::uint8_t sixteenthBuf[20 * 20];
    std::uint8_t expectedQuarter[32 * 32];
    std::uint8_t expectedSixteenth[20 * 20];
    for (int i = 0; i < 32 * 32; ++i) {
        inputBuf[i] = inputPixel(static_cast<std::uint32_t>(i % 32),
                                 static_cast<std::uint32_t>(i / 32));
        quarterBuf[i] = 0xEE;
    }
    for (int i = 0; i < 20 * 20; ++i) {
        sixteenthBuf[i] = 0xEE;
    }

    analysis::downsampleFilteringInputPicture(
        inputBuf, 32, 32, 32,
        quarterBuf + 6 * 32 + 6, 32, 16, 16, 4,
        sixteenthBuf + 5 * 20 + 5, 20, 8, 8, 2,
        true, false, false, true, true, false);

    buildExpectedPadded(expectedQuarter, 32, 6, 6, 16, 16, 4, 32 * 32,
                        [&](std::uint32_t cx, std::uint32_t cy) {
                            return decim2(inputBuf, 32, cx, cy);
                        });
    checkBuffer(quarterBuf, expectedQuarter, 32 * 32, "quarter");

    // Sixteenth = step-2 of the QUARTER content (the :1521 read saw
    // tfLevel1 true), padded with the sixteenth border.
    buildExpectedPadded(expectedSixteenth, 20, 5, 5, 8, 8, 2, 20 * 20,
                        [&](std::uint32_t cx, std::uint32_t cy) {
                            return decim2(expectedQuarter + 6 * 32 + 6, 32, cx, cy);
                        });
    checkBuffer(sixteenthBuf, expectedSixteenth, 20 * 20, "sixteenth");
}
