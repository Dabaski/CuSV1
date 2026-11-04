#include <doctest.h>
#include <filters.h>

TEST_CASE("filterMask2 applies when no condition breaches") {
    CHECK(l10_filters::filterMask2(1, 1, 100, 100, 100, 100) == -1);
}

TEST_CASE("filterMask2 disables on a p1-p0 limit breach") {
    CHECK(l10_filters::filterMask2(1, 1, 120, 100, 100, 100) == 0);
}

TEST_CASE("filterMask2 disables on a q1-q0 limit breach") {
    CHECK(l10_filters::filterMask2(1, 1, 100, 100, 100, 120) == 0);
}

TEST_CASE("filterMask2 blimit line is exact at 2*|p0-q0| + |p1-q1|/2") {
    // |p0-q0|*2 + |p1-q1|/2 = 2*2 + 0 = 4: == blimit still filters, +1 over does not.
    CHECK(l10_filters::filterMask2(1, 4, 100, 101, 99, 100) == -1);
    CHECK(l10_filters::filterMask2(1, 3, 100, 101, 99, 100) == 0);
}

TEST_CASE("filterMask applies clean and disables on the p3-p2 breach") {
    CHECK(l10_filters::filterMask(1, 4, 100, 100, 100, 100, 100, 100, 100, 100) == -1);
    CHECK(l10_filters::filterMask(1, 4, 120, 100, 100, 100, 100, 100, 100, 100) == 0);
}

TEST_CASE("filterMask disables on each of its six limit taps") {
    // limit=20, blimit=100 keeps every non-breach tap and the blimit term clean;
    // each row breaches exactly one adjacent pair by 21.
    CHECK(l10_filters::filterMask(20, 100, 121, 100, 100, 100, 100, 100, 100, 100) == 0); // p3-p2
    CHECK(l10_filters::filterMask(20, 100, 121, 121, 100, 100, 100, 100, 100, 100) == 0); // p2-p1
    CHECK(l10_filters::filterMask(20, 100, 121, 121, 121, 100, 100, 100, 100, 100) == 0); // p1-p0
    CHECK(l10_filters::filterMask(20, 100, 100, 100, 100, 100, 121, 100, 100, 100) == 0); // q1-q0
    CHECK(l10_filters::filterMask(20, 100, 100, 100, 100, 100, 100, 100, 121, 121) == 0); // q2-q1
    CHECK(l10_filters::filterMask(20, 100, 100, 100, 100, 100, 100, 100, 100, 121) == 0); // q3-q2
}

TEST_CASE("filterMask blimit line is exact") {
    // 2*|p0-q0| + |p1-q1|/2 = 4; == blimit filters, +1 over does not.
    CHECK(l10_filters::filterMask(1, 4, 100, 100, 100, 101, 99, 100, 100, 100) == -1);
    CHECK(l10_filters::filterMask(1, 3, 100, 100, 100, 101, 99, 100, 100, 100) == 0);
}

TEST_CASE("filterMask3Chroma applies clean and disables on the p2-p1 breach") {
    CHECK(l10_filters::filterMask3Chroma(1, 4, 100, 100, 100, 100, 100, 100) == -1);
    CHECK(l10_filters::filterMask3Chroma(1, 4, 120, 100, 100, 100, 100, 100) == 0);
}

TEST_CASE("filterMask3Chroma disables on each remaining limit tap") {
    CHECK(l10_filters::filterMask3Chroma(20, 100, 121, 121, 100, 100, 100, 100) == 0); // p1-p0
    CHECK(l10_filters::filterMask3Chroma(20, 100, 100, 100, 100, 121, 100, 100) == 0); // q1-q0
    CHECK(l10_filters::filterMask3Chroma(20, 100, 100, 100, 100, 100, 100, 121) == 0); // q2-q1
}

TEST_CASE("filterMask3Chroma blimit line is exact") {
    // 2*|p0-q0| + |p1-q1|/2 = 4.
    CHECK(l10_filters::filterMask3Chroma(1, 4, 100, 100, 101, 99, 100, 100) == -1);
    CHECK(l10_filters::filterMask3Chroma(1, 3, 100, 100, 101, 99, 100, 100) == 0);
}

TEST_CASE("flatMask3Chroma flat at step-1 ramp and disables on p2-p0") {
    // |p1-p0|=|q1-q0|=|p2-p0|=|q2-q0|=1 at thresh 1 -> flat.
    CHECK(l10_filters::flatMask3Chroma(1, 100, 100, 101, 101, 102, 102) == -1);
    // |p2-p0| = 2 > 1.
    CHECK(l10_filters::flatMask3Chroma(1, 103, 100, 101, 101, 102, 102) == 0);
}

TEST_CASE("flatMask3Chroma disables on each remaining threshold tap") {
    CHECK(l10_filters::flatMask3Chroma(20, 100, 121, 100, 100, 100, 100) == 0); // p1-p0
    CHECK(l10_filters::flatMask3Chroma(20, 100, 100, 100, 100, 121, 100) == 0); // q1-q0
    CHECK(l10_filters::flatMask3Chroma(20, 100, 100, 100, 100, 100, 121) == 0); // q2-q0
}

TEST_CASE("flatMask4 flat at step-1 ramp and disables on q3-q0") {
    // |p1-p0|=0, |q1-q0|=0, |p2-p0|=1, |q2-q0|=1, |p3-p0|=1, |q3-q0|=1 at thresh 1.
    CHECK(l10_filters::flatMask4(1, 101, 101, 102, 102, 103, 103, 104, 104) == -1);
    // |q3-q0| = 2 > 1.
    CHECK(l10_filters::flatMask4(1, 101, 101, 102, 102, 103, 103, 104, 105) == 0);
}

TEST_CASE("flatMask4 disables on each remaining threshold tap") {
    CHECK(l10_filters::flatMask4(20, 100, 100, 121, 100, 100, 100, 100, 100) == 0); // p1-p0
    CHECK(l10_filters::flatMask4(20, 100, 100, 100, 100, 100, 121, 100, 100) == 0); // q1-q0
    CHECK(l10_filters::flatMask4(20, 100, 121, 100, 100, 100, 100, 100, 100) == 0); // p2-p0
    CHECK(l10_filters::flatMask4(20, 100, 100, 100, 100, 100, 100, 121, 100) == 0); // q2-q0
    CHECK(l10_filters::flatMask4(20, 121, 100, 100, 100, 100, 100, 100, 100) == 0); // p3-p0
}

TEST_CASE("hevMask is not inverted: over thresh is -1, at thresh is 0") {
    // |p1-p0| = 10: > 9 fires, == 10 does not (NOT inverted - 0 means no high variance).
    CHECK(l10_filters::hevMask(9, 110, 100, 100, 100) == -1);
    CHECK(l10_filters::hevMask(10, 110, 100, 100, 100) == 0);
    CHECK(l10_filters::hevMask(9, 100, 100, 100, 110) == -1); // the q1-q0 tap
}

TEST_CASE("signedCharClamp clamps to [-128, 127]") {
    CHECK(l10_filters::signedCharClamp(-255) == -128);
    CHECK(l10_filters::signedCharClamp(-128) == -128);
    CHECK(l10_filters::signedCharClamp(128) == 127);
    CHECK(l10_filters::signedCharClamp(255) == 127);
    CHECK(l10_filters::signedCharClamp(0) == 0);
}

TEST_CASE("filter4 inner taps at hev=0") {
    // F1: ps0 = 2, qs0 = -2; filter = 3*(qs0-ps0) = -12; filter1 = -8>>3 = -1;
    // filter2 = -9>>3 = -2 (arithmetic); oq0 = 126+1 = 127, op0 = 130-2 = 128;
    // outer adjustment (filter1+1)>>1 = 0 -> op1/oq1 unchanged.
    std::uint8_t op1 = 128, op0 = 130, oq0 = 126, oq1 = 128;
    l10_filters::filter4(-1, 2, &op1, &op0, &oq0, &oq1);
    CHECK(op1 == 128);
    CHECK(op0 == 128);
    CHECK(oq0 == 127);
    CHECK(oq1 == 128);
}

TEST_CASE("filter4 outer taps engage at hev=1 but the outer adjustment does not") {
    // F2: ps1 = 12, qs1 = -12 (hev: |p1-p0| = 10 > 1); filter = scc(24) = 24;
    // inner: 24 + 3*(-4) = 12; filter1 = 16>>3 = 2, filter2 = 15>>3 = 1;
    // oq0 = -4 -> 124, op0 = 3 -> 131; outer adjustment (2+1)>>1 = 1 masked by
    // ~hev = 0, so op1/oq1 come back unchanged through the write path.
    std::uint8_t op1 = 140, op0 = 130, oq0 = 126, oq1 = 116;
    l10_filters::filter4(-1, 1, &op1, &op0, &oq0, &oq1);
    CHECK(op1 == 140);
    CHECK(op0 == 131);
    CHECK(oq0 == 124);
    CHECK(oq1 == 116);
}

TEST_CASE("filter4 outer adjustment applies at hev=0") {
    // F3: ps0 = 6, qs0 = -6; filter = -36; filter1 = -32>>3 = -4, filter2 = -33>>3 = -5;
    // oq0 = -2 -> 126, op0 = 1 -> 129; adjustment (-4+1)>>1 = -2 unmasked:
    // oq1 = scc(0+2) = 2 -> 130, op1 = scc(0-2) = -2 -> 126.
    std::uint8_t op1 = 128, op0 = 134, oq0 = 122, oq1 = 128;
    l10_filters::filter4(-1, 6, &op1, &op0, &oq0, &oq1);
    CHECK(op1 == 126);
    CHECK(op0 == 129);
    CHECK(oq0 == 126);
    CHECK(oq1 == 130);
}

TEST_CASE("filter6 falls back to filter4 when flat=0") {
    // Same edge samples as F1; op2/oq2 untouched.
    std::uint8_t op2 = 128, op1 = 128, op0 = 130, oq0 = 126, oq1 = 128, oq2 = 128;
    l10_filters::filter6(-1, 2, 0, &op2, &op1, &op0, &oq0, &oq1, &oq2);
    CHECK(op2 == 128);
    CHECK(op1 == 128);
    CHECK(op0 == 128);
    CHECK(oq0 == 127);
    CHECK(oq1 == 128);
    CHECK(oq2 == 128);
}

TEST_CASE("filter6 flat path is the 5-tap [1,2,2,2,1] filter") {
    // On flat-domain pixels the fallback coincides with the 5-tap (checked
    // above the mutation pass), so the branch is driven with flat forced -1
    // on asymmetric samples where the two paths provably differ:
    // op1 = RPOT(24+128+256+129,3) = 67; op0 = RPOT(8+128+256+258+192,3) = 105;
    // oq0 = RPOT(64+256+258+384+250,3) = 152; oq1 = RPOT(128+258+384+750,3) = 190.
    // (The filter4 fallback on the same samples gives 64/112/145/192.)
    std::uint8_t op2 = 8, op1 = 64, op0 = 128, oq0 = 129, oq1 = 192, oq2 = 250;
    l10_filters::filter6(-1, 8, -1, &op2, &op1, &op0, &oq0, &oq1, &oq2);
    CHECK(op2 == 8);
    CHECK(op1 == 67);
    CHECK(op0 == 105);
    CHECK(oq0 == 152);
    CHECK(oq1 == 190);
    CHECK(oq2 == 250);
}

TEST_CASE("filter8 falls back to filter4 when flat=0") {
    // F1 edge samples; op3/op2/oq2/oq3 untouched.
    std::uint8_t op3 = 128, op2 = 128, op1 = 128, op0 = 130, oq0 = 126, oq1 = 128, oq2 = 128, oq3 = 128;
    l10_filters::filter8(-1, 2, 0, &op3, &op2, &op1, &op0, &oq0, &oq1, &oq2, &oq3);
    CHECK(op3 == 128);
    CHECK(op2 == 128);
    CHECK(op1 == 128);
    CHECK(op0 == 128);
    CHECK(oq0 == 127);
    CHECK(oq1 == 128);
    CHECK(oq2 == 128);
    CHECK(oq3 == 128);
}

TEST_CASE("filter8 flat path is the 7-tap [1,1,1,2,1,1,1] filter") {
    // flat forced -1 on asymmetric samples where the 7-tap provably differs
    // from the fallback: op2 = RPOT(24+128+128+129+192,3) = 75;  [601, first
    // hand-derived as 701/88 - caught by this fixture, corrected]
    // op1 = RPOT(16+64+256+129+192+250,3) = 113; op0 = RPOT(8+64+128+258+192+250+16,3) = 115;
    // oq0 = RPOT(64+128+129+384+250+16+200,3) = 146; oq1 = RPOT(128+129+192+500+16+200+200,3) = 171;
    // oq2 = RPOT(129+192+250+32+200+200+200,3) = 150.
    std::uint8_t op3 = 8, op2 = 64, op1 = 128, op0 = 129, oq0 = 192, oq1 = 250, oq2 = 16, oq3 = 200;
    l10_filters::filter8(-1, 8, -1, &op3, &op2, &op1, &op0, &oq0, &oq1, &oq2, &oq3);
    CHECK(op3 == 8);
    CHECK(op2 == 75);
    CHECK(op1 == 113);
    CHECK(op0 == 115);
    CHECK(oq0 == 146);
    CHECK(oq1 == 171);
    CHECK(oq2 == 150);
    CHECK(oq3 == 200);
}

TEST_CASE("lpfHorizontal4 filters four columns independently across the pointer step") {
    // 8x8, pitch 8, edge between rows 3 and 4 (s = row 4). Columns 0..3 carry
    // F1 / flat / F2 / F1 patterns; columns 4..7 are sentinel 200.
    std::uint8_t buf[64];
    for (int i = 0; i < 64; ++i) {
        buf[i] = 200;
    }
    const int pitch = 8;
    // column 0: F1 (rows 2,3,4,5 = 128,130,126,128)
    buf[2 * 8 + 0] = 128; buf[3 * 8 + 0] = 130; buf[4 * 8 + 0] = 126; buf[5 * 8 + 0] = 128;
    // column 1: flat (all 128)
    buf[2 * 8 + 1] = 128; buf[3 * 8 + 1] = 128; buf[4 * 8 + 1] = 128; buf[5 * 8 + 1] = 128;
    // column 2: F2 (rows 2,3,4,5 = 140,130,126,116)
    buf[2 * 8 + 2] = 140; buf[3 * 8 + 2] = 130; buf[4 * 8 + 2] = 126; buf[5 * 8 + 2] = 116;
    // column 3: F1 again
    buf[2 * 8 + 3] = 128; buf[3 * 8 + 3] = 130; buf[4 * 8 + 3] = 126; buf[5 * 8 + 3] = 128;

    l10_filters::lpfHorizontal4(buf + 4 * pitch, pitch, 20, 16, 2);

    CHECK(buf[2 * 8 + 0] == 128); CHECK(buf[3 * 8 + 0] == 128);
    CHECK(buf[4 * 8 + 0] == 127); CHECK(buf[5 * 8 + 0] == 128);
    CHECK(buf[3 * 8 + 1] == 128); CHECK(buf[4 * 8 + 1] == 128);
    CHECK(buf[2 * 8 + 2] == 140); CHECK(buf[3 * 8 + 2] == 131);
    CHECK(buf[4 * 8 + 2] == 124); CHECK(buf[5 * 8 + 2] == 116);
    CHECK(buf[3 * 8 + 3] == 128); CHECK(buf[4 * 8 + 3] == 127);
    CHECK(buf[0] == 200); CHECK(buf[6 * 8 + 4] == 200); CHECK(buf[7 * 8 + 7] == 200);
}

TEST_CASE("lpfVertical4 filters four rows independently across the pitch step") {
    // 8x8, vertical edge between columns 3 and 4 (s = column 4, row 0).
    // Rows 0..3 carry F1 / flat / F2 / F1 patterns; rows 4..7 sentinel 200.
    std::uint8_t buf[64];
    for (int i = 0; i < 64; ++i) {
        buf[i] = 200;
    }
    const int pitch = 8;
    // row 0: F1 (cols 2,3,4,5 = 128,130,126,128)
    buf[0 * 8 + 2] = 128; buf[0 * 8 + 3] = 130; buf[0 * 8 + 4] = 126; buf[0 * 8 + 5] = 128;
    // row 1: flat
    buf[1 * 8 + 2] = 128; buf[1 * 8 + 3] = 128; buf[1 * 8 + 4] = 128; buf[1 * 8 + 5] = 128;
    // row 2: F2
    buf[2 * 8 + 2] = 140; buf[2 * 8 + 3] = 130; buf[2 * 8 + 4] = 126; buf[2 * 8 + 5] = 116;
    // row 3: F1
    buf[3 * 8 + 2] = 128; buf[3 * 8 + 3] = 130; buf[3 * 8 + 4] = 126; buf[3 * 8 + 5] = 128;

    l10_filters::lpfVertical4(buf + 4, pitch, 20, 16, 2);

    CHECK(buf[0 * 8 + 2] == 128); CHECK(buf[0 * 8 + 3] == 128);
    CHECK(buf[0 * 8 + 4] == 127); CHECK(buf[0 * 8 + 5] == 128);
    CHECK(buf[1 * 8 + 3] == 128); CHECK(buf[1 * 8 + 4] == 128);
    CHECK(buf[2 * 8 + 2] == 140); CHECK(buf[2 * 8 + 3] == 131);
    CHECK(buf[2 * 8 + 4] == 124); CHECK(buf[2 * 8 + 5] == 116);
    CHECK(buf[3 * 8 + 3] == 128); CHECK(buf[3 * 8 + 4] == 127);
    CHECK(buf[4 * 8 + 4] == 200); CHECK(buf[7 * 8 + 7] == 200);
}

TEST_CASE("lpfHorizontal6 routes flat columns to the 5-tap and others to filter4") {
    // 8x8, edge rows 1..6 (s = row 4: p2..q2 = rows 1..6). Column 0 flat
    // (100,100,101,101,102,102 -> 5-tap writes rows 2..5, rows 1/6 untouched);
    // column 1 non-flat (128,128,130,126,128,128 -> F1 fallback). Columns
    // 2..3 flat, 4..7 sentinel 200.
    std::uint8_t buf[64];
    for (int i = 0; i < 64; ++i) {
        buf[i] = 200;
    }
    const int pitch = 8;
    const int colA = 0;
    buf[1 * 8 + colA] = 100; buf[2 * 8 + colA] = 100; buf[3 * 8 + colA] = 101;
    buf[4 * 8 + colA] = 101; buf[5 * 8 + colA] = 102; buf[6 * 8 + colA] = 102;
    const int colB = 1;
    buf[1 * 8 + colB] = 128; buf[2 * 8 + colB] = 128; buf[3 * 8 + colB] = 130;
    buf[4 * 8 + colB] = 126; buf[5 * 8 + colB] = 128; buf[6 * 8 + colB] = 128;
    for (int c = 2; c <= 3; ++c) {
        buf[1 * 8 + c] = 100; buf[2 * 8 + c] = 100; buf[3 * 8 + c] = 101;
        buf[4 * 8 + c] = 101; buf[5 * 8 + c] = 102; buf[6 * 8 + c] = 102;
    }

    l10_filters::lpfHorizontal6(buf + 4 * pitch, pitch, 20, 2, 2);

    // flat column: 5-tap outputs on rows 2..5, rows 1/6 untouched
    CHECK(buf[1 * 8 + 0] == 100);
    CHECK(buf[2 * 8 + 0] == 100);
    CHECK(buf[3 * 8 + 0] == 101);
    CHECK(buf[4 * 8 + 0] == 101);
    CHECK(buf[5 * 8 + 0] == 102);
    CHECK(buf[6 * 8 + 0] == 102);
    // fallback column: filter4 on rows 2..5, rows 1/6 untouched
    CHECK(buf[1 * 8 + 1] == 128);
    CHECK(buf[2 * 8 + 1] == 128);
    CHECK(buf[3 * 8 + 1] == 128);
    CHECK(buf[4 * 8 + 1] == 127);
    CHECK(buf[5 * 8 + 1] == 128);
    CHECK(buf[6 * 8 + 1] == 128);
    // sentinel columns untouched
    CHECK(buf[4 * 8 + 4] == 200); CHECK(buf[3 * 8 + 7] == 200);
}

TEST_CASE("lpfVertical6 routes flat rows to the 5-tap and others to filter4") {
    // 8x8, vertical edge columns 1..6 (s = column 4: p2..q2 = cols 1..6).
    // Row 0 flat, row 1 non-flat (F1 fallback), rows 2..3 flat, 4..7 sentinel.
    std::uint8_t buf[64];
    for (int i = 0; i < 64; ++i) {
        buf[i] = 200;
    }
    const int pitch = 8;
    const int rowA = 0;
    buf[rowA * 8 + 1] = 100; buf[rowA * 8 + 2] = 100; buf[rowA * 8 + 3] = 101;
    buf[rowA * 8 + 4] = 101; buf[rowA * 8 + 5] = 102; buf[rowA * 8 + 6] = 102;
    const int rowB = 1;
    buf[rowB * 8 + 1] = 128; buf[rowB * 8 + 2] = 128; buf[rowB * 8 + 3] = 130;
    buf[rowB * 8 + 4] = 126; buf[rowB * 8 + 5] = 128; buf[rowB * 8 + 6] = 128;
    for (int r = 2; r <= 3; ++r) {
        buf[r * 8 + 1] = 100; buf[r * 8 + 2] = 100; buf[r * 8 + 3] = 101;
        buf[r * 8 + 4] = 101; buf[r * 8 + 5] = 102; buf[r * 8 + 6] = 102;
    }

    l10_filters::lpfVertical6(buf + 4, pitch, 20, 2, 2);

    // flat row: 5-tap outputs on cols 2..5, cols 1/6 untouched
    CHECK(buf[0 * 8 + 1] == 100);
    CHECK(buf[0 * 8 + 2] == 100);
    CHECK(buf[0 * 8 + 3] == 101);
    CHECK(buf[0 * 8 + 4] == 101);
    CHECK(buf[0 * 8 + 5] == 102);
    CHECK(buf[0 * 8 + 6] == 102);
    // fallback row: filter4 on cols 2..5, cols 1/6 untouched
    CHECK(buf[1 * 8 + 1] == 128);
    CHECK(buf[1 * 8 + 2] == 128);
    CHECK(buf[1 * 8 + 3] == 128);
    CHECK(buf[1 * 8 + 4] == 127);
    CHECK(buf[1 * 8 + 5] == 128);
    CHECK(buf[1 * 8 + 6] == 128);
    // sentinel rows untouched
    CHECK(buf[4 * 8 + 4] == 200); CHECK(buf[7 * 8 + 3] == 200);
}

TEST_CASE("lpfHorizontal8 routes flat columns to the 7-tap and others to filter4") {
    // 8x8, edge rows 0..7 (s = row 4: p3..q3 = rows 0..7). Column 0 flat
    // (101,101,102,102,103,103,104,104 -> 7-tap writes rows 1..6, rows 0/7
    // untouched); column 1 non-flat (128,128,128,130,126,128,128,128 -> F1).
    std::uint8_t buf[64];
    for (int i = 0; i < 64; ++i) {
        buf[i] = 200;
    }
    const int pitch = 8;
    const int flatCol[8] = {101, 101, 102, 102, 103, 103, 104, 104};
    for (int r = 0; r < 8; ++r) {
        buf[r * 8 + 0] = static_cast<std::uint8_t>(flatCol[r]);
        buf[r * 8 + 2] = static_cast<std::uint8_t>(flatCol[r]);
        buf[r * 8 + 3] = static_cast<std::uint8_t>(flatCol[r]);
    }
    const int fbCol[8] = {128, 128, 128, 130, 126, 128, 128, 128};
    for (int r = 0; r < 8; ++r) {
        buf[r * 8 + 1] = static_cast<std::uint8_t>(fbCol[r]);
    }

    l10_filters::lpfHorizontal8(buf + 4 * pitch, pitch, 20, 2, 2);

    // flat column: 7-tap outputs on rows 1..6, rows 0/7 untouched
    CHECK(buf[0 * 8 + 0] == 101);
    CHECK(buf[1 * 8 + 0] == 102);
    CHECK(buf[2 * 8 + 0] == 102);
    CHECK(buf[3 * 8 + 0] == 102);
    CHECK(buf[4 * 8 + 0] == 103);
    CHECK(buf[5 * 8 + 0] == 103);
    CHECK(buf[6 * 8 + 0] == 104);
    CHECK(buf[7 * 8 + 0] == 104);
    // fallback column: F1 on rows 2..5, rows 0/1/6/7 untouched
    CHECK(buf[0 * 8 + 1] == 128);
    CHECK(buf[1 * 8 + 1] == 128);
    CHECK(buf[2 * 8 + 1] == 128);
    CHECK(buf[3 * 8 + 1] == 128);
    CHECK(buf[4 * 8 + 1] == 127);
    CHECK(buf[5 * 8 + 1] == 128);
    CHECK(buf[6 * 8 + 1] == 128);
    CHECK(buf[7 * 8 + 1] == 128);
    // sentinel columns untouched
    CHECK(buf[4 * 8 + 4] == 200); CHECK(buf[0 * 8 + 7] == 200);
}

TEST_CASE("lpfVertical8 routes flat rows to the 7-tap and others to filter4") {
    // 8x8, vertical edge columns 0..7 (s = column 4). Row 0 flat, row 1
    // non-flat (F1), rows 2..3 flat, rows 4..7 sentinel.
    std::uint8_t buf[64];
    for (int i = 0; i < 64; ++i) {
        buf[i] = 200;
    }
    const int pitch = 8;
    const int flatRow[8] = {101, 101, 102, 102, 103, 103, 104, 104};
    for (int c = 0; c < 8; ++c) {
        buf[0 * 8 + c] = static_cast<std::uint8_t>(flatRow[c]);
        buf[2 * 8 + c] = static_cast<std::uint8_t>(flatRow[c]);
        buf[3 * 8 + c] = static_cast<std::uint8_t>(flatRow[c]);
    }
    const int fbRow[8] = {128, 128, 128, 130, 126, 128, 128, 128};
    for (int c = 0; c < 8; ++c) {
        buf[1 * 8 + c] = static_cast<std::uint8_t>(fbRow[c]);
    }

    l10_filters::lpfVertical8(buf + 4, pitch, 20, 2, 2);

    // flat row: 7-tap outputs on cols 1..6, cols 0/7 untouched
    CHECK(buf[0 * 8 + 0] == 101);
    CHECK(buf[0 * 8 + 1] == 102);
    CHECK(buf[0 * 8 + 2] == 102);
    CHECK(buf[0 * 8 + 3] == 102);
    CHECK(buf[0 * 8 + 4] == 103);
    CHECK(buf[0 * 8 + 5] == 103);
    CHECK(buf[0 * 8 + 6] == 104);
    CHECK(buf[0 * 8 + 7] == 104);
    // fallback row: F1 on cols 2..5, cols 0/1/6/7 untouched
    CHECK(buf[1 * 8 + 0] == 128);
    CHECK(buf[1 * 8 + 1] == 128);
    CHECK(buf[1 * 8 + 2] == 128);
    CHECK(buf[1 * 8 + 3] == 128);
    CHECK(buf[1 * 8 + 4] == 127);
    CHECK(buf[1 * 8 + 5] == 128);
    CHECK(buf[1 * 8 + 6] == 128);
    CHECK(buf[1 * 8 + 7] == 128);
    // sentinel rows untouched
    CHECK(buf[4 * 8 + 4] == 200); CHECK(buf[7 * 8 + 0] == 200);
}

TEST_CASE("updateSharpness ladder at sharpness 0") {
    // s=0: lim = lvl (min 1); mblim = 2*(lvl+2) + lim.
    l10_filters::LoopFilterInfoN lfi;
    l10_filters::updateSharpness(&lfi, 0);
    CHECK(lfi.lfthr[0].lim[0] == 1);
    CHECK(lfi.lfthr[0].mblim[0] == 5);
    CHECK(lfi.lfthr[63].lim[0] == 63);
    CHECK(lfi.lfthr[63].mblim[0] == 193);
}

TEST_CASE("updateSharpness caps blockInsideLimit at 9-sharpness when sharpness>0") {
    // s=7, lvl=63: 63>>2 = 15 -> cap 2 -> lim 2, mblim 2*65+2 = 132.
    // s=7, lvl=20: 20>>2 = 5 -> cap 2 -> lim 2, mblim 46.
    // s=4, lvl=63: shift 1 only -> 31 -> cap 5 -> lim 5, mblim 135.
    l10_filters::LoopFilterInfoN lfi;
    l10_filters::updateSharpness(&lfi, 7);
    CHECK(lfi.lfthr[63].lim[0] == 2);
    CHECK(lfi.lfthr[63].mblim[0] == 132);
    CHECK(lfi.lfthr[20].lim[0] == 2);
    CHECK(lfi.lfthr[20].mblim[0] == 46);
    l10_filters::updateSharpness(&lfi, 4);
    CHECK(lfi.lfthr[63].lim[0] == 5);
    CHECK(lfi.lfthr[63].mblim[0] == 135);
}

TEST_CASE("updateSharpness does not touch hev_thr") {
    // hev_thr is set by svt_av1_loop_filter_frame_init (outside DF1), never
    // by update_sharpness: all 64 triplets must survive untouched.
    l10_filters::LoopFilterInfoN lfi;
    for (int lvl = 0; lvl <= 63; ++lvl) {
        for (int b = 0; b < 16; ++b) {
            lfi.lfthr[lvl].hev_thr[b] = 0xAB;
        }
    }
    l10_filters::updateSharpness(&lfi, 0);
    l10_filters::updateSharpness(&lfi, 7);
    for (int lvl = 0; lvl <= 63; ++lvl) {
        for (int b = 0; b < 16; ++b) {
            CHECK(lfi.lfthr[lvl].hev_thr[b] == 0xAB);
        }
    }
}

// DF2. The impulse fixtures force mask/flat/flat2 = -1: filter14 takes the
// gates as arguments and never verifies flatness itself, so the 13-tap core
// is directly observable. p6 impulse: p6 = 32, every other tap 0.
TEST_CASE("filter14 13-tap core: p6 impulse drives op5") {
    std::uint8_t op6 = 32, op5 = 0, op4 = 0, op3 = 0, op2 = 0, op1 = 0, op0 = 0;
    std::uint8_t oq0 = 0, oq1 = 0, oq2 = 0, oq3 = 0, oq4 = 0, oq5 = 0, oq6 = 7;
    l10_filters::filter14(-1, 2, -1, -1, &op6, &op5, &op4, &op3, &op2, &op1, &op0, &oq0, &oq1, &oq2, &oq3, &oq4,
                          &oq5, &oq6);
    // op5 = RPOT(p6*7, 4) = (224+8)>>4 = 14.
    CHECK(op5 == 14);
    // op4 = RPOT(p6*5, 4) = (160+8)>>4 = 10.
    CHECK(op4 == 10);
    // op3 = RPOT(p6*4, 4) = (128+8)>>4 = 8.
    CHECK(op3 == 8);
    // op2 = RPOT(p6*3, 4) = (96+8)>>4 = 6.
    CHECK(op2 == 6);
    // op1 = RPOT(p6*2, 4) = (64+8)>>4 = 4.
    CHECK(op1 == 4);
    // op0 = RPOT(p6, 4) = (32+8)>>4 = 2.
    CHECK(op0 == 2);
    // The outer taps are NEVER written by the 13-tap path: op6 keeps its
    // input (32) and oq6 keeps its sentinel (7; every computed output in
    // this fixture is 0/2/4/6/8/10/14, so any stray write is caught).
    CHECK(op6 == 32);
    CHECK(oq6 == 7);
}

// q6 impulse: q6 = 32, every other tap 0 - the q-side mirror of the p6
// impulse (oq0..oq5 carry q6 with weights 1,2,3,4,5,7; the p-side is 0).
TEST_CASE("filter14 13-tap core: q6 impulse drives oq0") {
    std::uint8_t op6 = 0, op5 = 0, op4 = 0, op3 = 0, op2 = 0, op1 = 0, op0 = 0;
    std::uint8_t oq0 = 0, oq1 = 0, oq2 = 0, oq3 = 0, oq4 = 0, oq5 = 0, oq6 = 32;
    l10_filters::filter14(-1, 2, -1, -1, &op6, &op5, &op4, &op3, &op2, &op1, &op0, &oq0, &oq1, &oq2, &oq3, &oq4,
                          &oq5, &oq6);
    // oq0 = RPOT(q6, 4) = (32+8)>>4 = 2.
    CHECK(oq0 == 2);
    // oq1 = RPOT(q6*2, 4) = (64+8)>>4 = 4.
    CHECK(oq1 == 4);
    // oq2 = RPOT(q6*3, 4) = (96+8)>>4 = 6.
    CHECK(oq2 == 6);
    // oq3 = RPOT(q6*4, 4) = (128+8)>>4 = 8.
    CHECK(oq3 == 8);
    // oq4 = RPOT(q6*5, 4) = (160+8)>>4 = 10.
    CHECK(oq4 == 10);
    // oq5 = RPOT(q6*7, 4) = (224+8)>>4 = 14.
    CHECK(oq5 == 14);
}

// ELSE-STACKING (S14): flat2=0 must fall through to filter8's 7-tap path
// (filter14's else chain stacks filter8, which itself stacks filter4).
// Fixture: mask/flat forced -1, flat2 forced 0; the 7-tap computes
// op2 = (p3*3 + p2*2 + p1 + p0 + q0 + 4)>>3 = (300+198+100+100+101+4)>>3
// = 100, distinct from the pre-value 99 (no-op) and from the 13-tap path.
TEST_CASE("filter14 else-stack: flat2=0 flat=1 takes the filter8 path") {
    std::uint8_t op6 = 200, op5 = 201, op4 = 202, op3 = 100, op2 = 99, op1 = 100, op0 = 100;
    std::uint8_t oq0 = 101, oq1 = 101, oq2 = 101, oq3 = 101, oq4 = 105, oq5 = 106, oq6 = 107;
    l10_filters::filter14(-1, 2, -1, 0, &op6, &op5, &op4, &op3, &op2, &op1, &op0, &oq0, &oq1, &oq2, &oq3, &oq4,
                          &oq5, &oq6);
    CHECK(op2 == 100);
}

// ELSE-STACKING (S15): flat=0 falls THROUGH filter8 to filter4 (the chain
// is filter14-else -> filter8-else -> filter4). Fixture values make
// hev fire (|p1-p0| = 10 > thresh 2): filter4 computes op0 = 102 while
// the filter8 7-tap would give 100 and no-op leaves the pre-value 100.
TEST_CASE("filter14 else-stack: flat=0 falls through to the filter4 path") {
    std::uint8_t op6 = 200, op5 = 201, op4 = 202, op3 = 100, op2 = 100, op1 = 90, op0 = 100;
    std::uint8_t oq0 = 110, oq1 = 100, oq2 = 100, oq3 = 100, oq4 = 105, oq5 = 106, oq6 = 107;
    l10_filters::filter14(-1, 2, 0, -1, &op6, &op5, &op4, &op3, &op2, &op1, &op0, &oq0, &oq1, &oq2, &oq3, &oq4,
                          &oq5, &oq6);
    CHECK(op0 == 102);
}

// ELSE-STACKING (S16): mask=0 disables the whole stacked chain - filter4
// still runs but its mask gate writes the originals back, so every tap
// is unchanged. hev still fires in this fixture (|p1-p0| = 10) to prove
// it is the MASK gate, not hev, that holds the chain.
TEST_CASE("filter14 else-stack: mask=0 leaves every tap unchanged") {
    std::uint8_t op6 = 200, op5 = 201, op4 = 202, op3 = 100, op2 = 100, op1 = 90, op0 = 100;
    std::uint8_t oq0 = 110, oq1 = 100, oq2 = 100, oq3 = 100, oq4 = 105, oq5 = 106, oq6 = 107;
    l10_filters::filter14(0, 2, -1, -1, &op6, &op5, &op4, &op3, &op2, &op1, &op0, &oq0, &oq1, &oq2, &oq3, &oq4,
                          &oq5, &oq6);
    CHECK(op0 == 100);
}

// SATURATION EXTREMES (S17): all taps 255 - the tap sum is 16*255 = 4080,
// RPOT(...,4) lands exactly on 255. Under an RPOT shift-3 mutation this
// reads (4084)>>3 = 510 -> 254 after the uint8 truncation, so the
// assertion is the named killer for M4 (the RPOT shift).
TEST_CASE("filter14 saturation: all-255 flat block stays 255") {
    std::uint8_t op6 = 255, op5 = 255, op4 = 255, op3 = 255, op2 = 255, op1 = 255, op0 = 255;
    std::uint8_t oq0 = 255, oq1 = 255, oq2 = 255, oq3 = 255, oq4 = 255, oq5 = 255, oq6 = 255;
    l10_filters::filter14(-1, 2, -1, -1, &op6, &op5, &op4, &op3, &op2, &op1, &op0, &oq0, &oq1, &oq2, &oq3, &oq4,
                          &oq5, &oq6);
    CHECK(op5 == 255);
    CHECK(oq5 == 255);
}

TEST_CASE("filter14 saturation: all-0 block stays 0 (no wrap)") {
    // NOTE: every written tap is also READ as its input (p5 = *op5 etc.),
    // so pre-values cannot be garbage on this fixture - all inputs 0, all
    // outputs 0, the assertion is the no-underflow/no-wrap extreme.
    std::uint8_t op6 = 0, op5 = 0, op4 = 0, op3 = 0, op2 = 0, op1 = 0, op0 = 0;
    std::uint8_t oq0 = 0, oq1 = 0, oq2 = 0, oq3 = 0, oq4 = 0, oq5 = 0, oq6 = 0;
    l10_filters::filter14(-1, 2, -1, -1, &op6, &op5, &op4, &op3, &op2, &op1, &op0, &oq0, &oq1, &oq2, &oq3, &oq4,
                          &oq5, &oq6);
    CHECK(op5 == 0);
    CHECK(oq5 == 0);
}

// M1 KILLER (two-tap op5): with p6 and p5 BOTH nonzero the p6*7/p5*2 vs
// p6*2/p5*7 swap becomes visible: real op5 = (224+32+8)>>4 = 16, the
// swapped order gives (64+112+8)>>4 = 11.
TEST_CASE("filter14 op5 tap order: p6 carries 7, p5 carries 2") {
    std::uint8_t op6 = 32, op5 = 16, op4 = 0, op3 = 0, op2 = 0, op1 = 0, op0 = 0;
    std::uint8_t oq0 = 0, oq1 = 0, oq2 = 0, oq3 = 0, oq4 = 0, oq5 = 0, oq6 = 0;
    l10_filters::filter14(-1, 2, -1, -1, &op6, &op5, &op4, &op3, &op2, &op1, &op0, &oq0, &oq1, &oq2, &oq3, &oq4,
                          &oq5, &oq6);
    CHECK(op5 == 16);
}

// M2 KILLER (S22b, the flat2 boundary): the block is flat at the CORE
// (flat=-1) but broken at the OUTER span by p6=104 vs p0=101 -> flat2=0,
// so the filter8 path must run (op5 untouched, oq0 gets the 7-tap 101
// over the pre-value 100). Under the flat2-p5-for-p6 mutation flat2
// becomes 1 and the 13-tap writes op5 = 102 instead.
TEST_CASE("filter14 flat2 boundary: core flat, outer broken takes filter8") {
    std::uint8_t buf[16 * 16];
    for (int i = 0; i < 16 * 16; ++i) {
        buf[i] = 100;
    }
    // column 2: p6=104 (row 0) breaks flat2; the core rows are within 1.
    buf[0 * 16 + 2] = 104;                     // p6
    buf[4 * 16 + 2] = 101;                     // p2
    buf[5 * 16 + 2] = 101;                     // p1
    buf[6 * 16 + 2] = 101;                     // p0
    buf[9 * 16 + 2] = 101;                     // q2
    l10_filters::lpfHorizontal14(buf + 7 * 16 + 2, 16, 255, 255, 2);
    CHECK(buf[1 * 16 + 2] == 100); // op5 untouched by the filter8 path
    CHECK(buf[7 * 16 + 2] == 101); // oq0 = (101+101+101+200+100+101+100+4)>>3 = 101
}

// S22c (the flat2 boundary's VICE VERSA): outer span flat (flat2=-1) but
// the core broken (flat=0) -> the chain falls through filter8 to filter4:
// hev fires (|p1-p0| = 10 > thresh 2) and oq0 = 107 (the filter4 result);
// the filter8 7-tap would give 101 and no-op leaves 100.
TEST_CASE("filter14 flat2 boundary: outer flat, core broken takes filter4") {
    std::uint8_t buf[16 * 16];
    for (int i = 0; i < 16 * 16; ++i) {
        buf[i] = 100;
    }
    buf[5 * 16 + 2] = 90;  // p1 breaks the core flatness
    buf[7 * 16 + 2] = 110; // q0
    l10_filters::lpfHorizontal14(buf + 7 * 16 + 2, 16, 255, 255, 2);
    CHECK(buf[7 * 16 + 2] == 107); // oq0 = filter4's scc(qs0 - filter1) ^ 0x80
}

// ENTRY POINTS (S18-S21). The 13-tap is an averager: a perfectly flat block
// maps to itself, so the entry-point fixtures use a block that is flat by
// every gate but split across the edge: p-group 102 (p6..p0), q0/q1 100,
// q2/q3 102, outer q 100. blimit/limit = 255 so the mask never fires.
// The 13-tap then moves op0 102->101 and oq0/oq1 100->101.
TEST_CASE("lpfHorizontal14 filters four columns through the 13-tap path") {
    std::uint8_t buf[16 * 16];
    for (int i = 0; i < 16 * 16; ++i) {
        buf[i] = 100;
    }
    // column 2 (the first filtered column): rows 0-6 = 102 (p6..p0),
    // rows 7-8 = 100 (q0, q1), rows 9-10 = 102 (q2, q3), rows 11-13 = 100.
    for (int r = 0; r <= 6; ++r) {
        buf[r * 16 + 2] = 102;
    }
    buf[9 * 16 + 2] = 102;
    buf[10 * 16 + 2] = 102;
    l10_filters::lpfHorizontal14(buf + 7 * 16 + 2, 16, 255, 255, 2);
    // op0 (row 6) = RPOT(...13-tap..., 4) = (1622+8)>>4 = 101.
    CHECK(buf[6 * 16 + 2] == 101);
}

// COUNT SEMANTICS (S19, pinned from deblocking_common.c:849-852): the entry
// passes count=1 and the loop covers step*count = 4 columns (cols 2..5 for
// s at col 2). Column 6, given the same split-block pattern, must stay
// UNTOUCHED - a count>=2 mutation would move its op0 102->101.
TEST_CASE("lpfHorizontal14 count=1 filters exactly four columns") {
    std::uint8_t buf[16 * 16];
    for (int i = 0; i < 16 * 16; ++i) {
        buf[i] = 100;
    }
    for (int c = 2; c <= 6; ++c) {
        for (int r = 0; r <= 6; ++r) {
            buf[r * 16 + c] = 102;
        }
        buf[9 * 16 + c] = 102;
        buf[10 * 16 + c] = 102;
    }
    l10_filters::lpfHorizontal14(buf + 7 * 16 + 2, 16, 255, 255, 2);
    CHECK(buf[6 * 16 + 2] == 101); // in span: filtered
    CHECK(buf[6 * 16 + 6] == 102); // one past the four-column span: untouched
}

// S20: the vertical twin filters FOUR ROWS (count=4, deblocking_common.c:
// 887-889), taps contiguous, pointer += pitch per row. Same split-block
// fixture transposed: s at (row 2, col 7), p-group cols 0-6 = 102,
// q0/q1 = 100, q2/q3 = 102, outer q = 100.
TEST_CASE("lpfVertical14 filters four rows through the 13-tap path") {
    std::uint8_t buf[16 * 16];
    for (int i = 0; i < 16 * 16; ++i) {
        buf[i] = 100;
    }
    for (int r = 2; r <= 5; ++r) {
        for (int c = 0; c <= 6; ++c) {
            buf[r * 16 + c] = 102;
        }
        buf[r * 16 + 9] = 102;
        buf[r * 16 + 10] = 102;
    }
    l10_filters::lpfVertical14(buf + 2 * 16 + 7, 16, 255, 255, 2);
    // op0 (col 6) = (1622+8)>>4 = 101.
    CHECK(buf[2 * 16 + 6] == 101);
}

// S21: the vertical entry passes count=4 exactly - row 6 (one past the
// four-row span rows 2..5) must stay untouched.
TEST_CASE("lpfVertical14 count=4 filters exactly four rows") {
    std::uint8_t buf[16 * 16];
    for (int i = 0; i < 16 * 16; ++i) {
        buf[i] = 100;
    }
    for (int r = 2; r <= 6; ++r) {
        for (int c = 0; c <= 6; ++c) {
            buf[r * 16 + c] = 102;
        }
        buf[r * 16 + 9] = 102;
        buf[r * 16 + 10] = 102;
    }
    l10_filters::lpfVertical14(buf + 2 * 16 + 7, 16, 255, 255, 2);
    CHECK(buf[2 * 16 + 6] == 101); // in span: filtered
    CHECK(buf[6 * 16 + 6] == 102); // one past the four-row span: untouched
}