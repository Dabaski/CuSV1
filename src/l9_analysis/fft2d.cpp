// AN13: the 2D FFT drivers, composing the landed AN12 1D kernels.
// 1:1 ports of the SVT-AV1 2D FFT surface (third_party/SVT-AV1/Source/Lib/
// Codec/fft.c): simple_transpose (:15-21), unpack_2d_output (:33-49), the
// svt_aom_fft_2d_gen driver (:51-64), svt_aom_ifft_2d_gen (:108-171) and the
// ten per-size entry points (:88-106 fwd, :179-237 inv). This slice NEVER
// re-implements a 1D butterfly: every pass calls the landed analysis::fft1dN
// / analysis::ifft1dN from fft1d.cpp.

#include "analysis.h"

namespace analysis {

namespace {

// 1:1 port of simple_transpose (fft.c:15-21): b[y*n + x] = A[x*n + y]
// for y, x in 0..n-1. File-local there (static INLINE), file-local here.
void simpleTranspose(const float* input, float* output, std::int32_t n) {
    for (std::int32_t y = 0; y < n; y++) {
        for (std::int32_t x = 0; x < n; x++) {
            output[y * n + x] = input[x * n + y];
        }
    }
}

// 1:1 port of unpack_2d_output (fft.c:33-49). Contract comment :23-32
// verbatim in shape: after the row/column transforms the temp region
// holds the packed halves; the output assembles real+imag next to each
// other as output[2*(y*n+x)] = Re F(y,x), output[2*(y*n+x)+1] = Im F(y,x)
// for ALL y in 0..n-1 but ONLY x in 0..n/2 - n*(n/2+1) complex values in
// a 2*n*n float buffer whose tail is never written (caller zero-fills it,
// noise_util.c:65). The y_extra/x_extra branches :43-46 write the mirrored
// rows and guard the n/2-boundary cross terms added at :41-42; at n=2 both
// extras are false (hand-derivation pinned in the worklog).
void unpack2dOutput(const float* colFft, float* output, std::int32_t n) {
    for (std::int32_t y = 0; y <= n / 2; ++y) {
        const std::int32_t y2 = y + n / 2;
        const bool yExtra = y2 > n / 2 && y2 < n;

        for (std::int32_t x = 0; x <= n / 2; ++x) {
            const std::int32_t x2 = x + n / 2;
            const bool xExtra = x2 > n / 2 && x2 < n;
            output[2 * (y * n + x)] =
                colFft[y * n + x] - (xExtra && yExtra ? colFft[y2 * n + x2] : 0);
            output[2 * (y * n + x) + 1] =
                (yExtra ? colFft[y2 * n + x] : 0) + (xExtra ? colFft[y * n + x2] : 0);
            if (yExtra) {
                output[2 * ((n - y) * n + x)] =
                    colFft[y * n + x] + (xExtra ? colFft[y2 * n + x2] : 0);
                output[2 * ((n - y) * n + x) + 1] =
                    -colFft[y2 * n + x] + (xExtra ? colFft[y * n + x2] : 0);
            }
        }
    }
}

}

// 1:1 port of the svt_aom_fft_2d_gen driver (fft.c:51-64). Five passes:
// column tform, transpose, second-axis tform, transpose, unpack. The
// reference's vec_size parameter is 1 at every _c entry (fft.c:89-105), so
// the step-1 loops are unrolled by the compiler as plain for x = 0..n-1 -
// named deviation: vec_size folded with this note. tform always passes
// stride n to the landed 1D kernel.
void fft2dGen(const float* input, float* temp, float* output, std::int32_t n,
              void (*tform)(const float*, float*, std::int32_t)) {
    for (std::int32_t x = 0; x < n; ++x) {
        tform(input + x, output + x, n);
    }
    simpleTranspose(output, temp, n);
    for (std::int32_t x = 0; x < n; ++x) {
        tform(temp + x, output + x, n);
    }
    simpleTranspose(output, temp, n);
    unpack2dOutput(temp, output, n);
}

// 1:1 port of svt_aom_fft2x2_float_c (fft.c:88-90): the n=2 entry point,
// composing analysis::fft1d2 (landed AN12 kernel fft_common.h:113-119).
void fft2d2(const float* input, float* temp, float* output) {
    fft2dGen(input, temp, output, 2, fft1d2);
}

// 1:1 port of svt_aom_fft4x4_float_c (fft.c:92-94), composing analysis::fft1d4
// (landed AN12 kernel fft_common.h:121-136).
void fft2d4(const float* input, float* temp, float* output) {
    fft2dGen(input, temp, output, 4, fft1d4);
}

// 1:1 port of svt_aom_fft8x8_float_c (fft.c:96-98), composing analysis::fft1d8
// (landed AN12 kernel fft_common.h:138-170).
void fft2d8(const float* input, float* temp, float* output) {
    fft2dGen(input, temp, output, 8, fft1d8);
}

// 1:1 port of svt_aom_fft16x16_float_c (fft.c:100-102), composing analysis::
// fft1d16 (landed AN12 kernel fft_common.h:172-244).
void fft2d16(const float* input, float* temp, float* output) {
    fft2dGen(input, temp, output, 16, fft1d16);
}

// 1:1 port of svt_aom_fft32x32_float_c (fft.c:104-106), composing analysis::
// fft1d32 (landed AN12 kernel fft_common.h:246-414).
void fft2d32(const float* input, float* temp, float* output) {
    fft2dGen(input, temp, output, 32, fft1d32);
}

namespace {

// 1:1 port of the svt_aom_ifft_2d_gen driver (fft.c:108-171). NOT a 5-pass
// mirror of the forward: the inverse is a REAL/IMAG split-and-pack
// rearrangement. vec_size = 1 at every reference _c entry, so fft_single and
// fft_multi are the SAME landed kernel and the y < vec_size loop :136-138 is
// empty; AOMMAX(2, vec_size) = 2 - named deviation: both folded into one
// fft pointer and a single loop from y = 2.
void ifft2dGen(const float* input, float* temp, float* output, std::int32_t n,
               void (*fft)(const float*, float*, std::int32_t),
               void (*ifft)(const float*, float*, std::int32_t)) {
    // Column 0 and n/2 have conjugate symmetry, so we can directly do the
    // ifft and get real outputs (fft.c:111-120).
    for (std::int32_t y = 0; y <= n / 2; ++y) {
        output[y * n] = input[2 * y * n];
        output[y * n + 1] = input[2 * (y * n + n / 2)];
    }
    for (std::int32_t y = n / 2 + 1; y < n; ++y) {
        output[y * n] = input[2 * (y - n / 2) * n + 1];
        output[y * n + 1] = input[2 * ((y - n / 2) * n + n / 2) + 1];
    }

    for (std::int32_t i = 0; i < 2; ++i) {
        ifft(output + i, temp + i, n);
    }
    // For the other columns, since we don't have a full ifft for complex
    // inputs we have to split them into the real and imaginary counterparts.
    // Pack the real component, then the imaginary components (fft.c:125-135).
    for (std::int32_t y = 0; y < n; ++y) {
        for (std::int32_t x = 1; x < n / 2; ++x) {
            output[y * n + (x + 1)] = input[2 * (y * n + x)];
        }
        for (std::int32_t x = 1; x < n / 2; ++x) {
            output[y * n + (x + n / 2)] = input[2 * (y * n + x) + 1];
        }
    }
    for (std::int32_t y = 2; y < n; ++y) {
        fft(output + y, temp + y, n);
    }
    // Put the 0 and n/2 th results in the correct place (fft.c:143-147).
    for (std::int32_t x = 0; x < n; ++x) {
        output[x] = temp[x * n];
        output[(n / 2) * n + x] = temp[x * n + 1];
    }
    // This rearranges and transposes (fft.c:148-166).
    for (std::int32_t y = 1; y < n / 2; ++y) {
        // Fill in the real columns
        for (std::int32_t x = 0; x <= n / 2; ++x) {
            output[x + y * n] =
                temp[(y + 1) + x * n] +
                ((x > 0 && x < n / 2) ? temp[(y + n / 2) + (x + n / 2) * n] : 0);
        }
        for (std::int32_t x = n / 2 + 1; x < n; ++x) {
            output[x + y * n] = temp[(y + 1) + (n - x) * n] -
                temp[(y + n / 2) + ((n - x) + n / 2) * n];
        }
        // Fill in the imag columns
        for (std::int32_t x = 0; x <= n / 2; ++x) {
            output[x + (y + n / 2) * n] = temp[(y + n / 2) + x * n] -
                ((x > 0 && x < n / 2) ? temp[(y + 1) + (x + n / 2) * n] : 0);
        }
        for (std::int32_t x = n / 2 + 1; x < n; ++x) {
            output[x + (y + n / 2) * n] = temp[(y + 1) + ((n - x) + n / 2) * n] +
                temp[(y + n / 2) + (n - x) * n];
        }
    }
    for (std::int32_t y = 0; y < n; ++y) {
        ifft(output + y, temp + y, n);
    }
    simpleTranspose(temp, output, n);
}

}  // namespace

// 1:1 port of svt_aom_ifft2x2_float_c (fft.c:179-189), composing
// analysis::fft1d2 / analysis::ifft1d2.
void ifft2d2(const float* input, float* temp, float* output) {
    ifft2dGen(input, temp, output, 2, fft1d2, ifft1d2);
}

// 1:1 port of svt_aom_ifft4x4_float_c (fft.c:191-201), composing
// analysis::fft1d4 / analysis::ifft1d4.
void ifft2d4(const float* input, float* temp, float* output) {
    ifft2dGen(input, temp, output, 4, fft1d4, ifft1d4);
}

// 1:1 port of svt_aom_ifft8x8_float_c (fft.c:203-213), composing
// analysis::fft1d8 / analysis::ifft1d8.
void ifft2d8(const float* input, float* temp, float* output) {
    ifft2dGen(input, temp, output, 8, fft1d8, ifft1d8);
}

// 1:1 port of svt_aom_ifft16x16_float_c (fft.c:215-225), composing
// analysis::fft1d16 / analysis::ifft1d16.
void ifft2d16(const float* input, float* temp, float* output) {
    ifft2dGen(input, temp, output, 16, fft1d16, ifft1d16);
}

// 1:1 port of svt_aom_ifft32x32_float_c (fft.c:227-237), composing
// analysis::fft1d32 / analysis::ifft1d32.
void ifft2d32(const float* input, float* temp, float* output) {
    ifft2dGen(input, temp, output, 32, fft1d32, ifft1d32);
}

}  // namespace analysis
