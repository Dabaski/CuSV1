// tools/golden_gen/main_frame.c
// Frame-policy composition: raster 4x4 loop over an 8x8 frame where each
// block's mode is DECIDED by the D2 policy (all 13 PredictionModes, scored
// by SAD(prediction vs source), lowest SAD wins, tie-break = lowest mode
// index) evaluated against RECONSTRUCTED neighbors. The policy is THIS
// PROJECT'S (documented, not SVT's — SVT uses full RD); the primitives it
// scores with are verbatim SVT.
// Output: mode map (4 blocks), recon frame (64), coeffs (64).
#include "composition.c"

// SAD 4x4 between the source block and a candidate prediction, both flat
// 4-strided (the caller materializes the candidate prediction).
static uint32_t svtd_sad4x4(const uint8_t* src, const uint8_t* pred) {
    return svt_nxm_sad_kernel_helper_c(src, 4, pred, 4, 4, 4);
}

// D2 policy: pick the mode minimizing SAD; ties go to the lowest mode index.
static int svtd_decide(const uint8_t* src, const uint8_t* above, int n_top, int n_topright,
                       const uint8_t* left, int n_left, int n_bottomleft, uint8_t above_left,
                       uint32_t* best_sad_out) {
    uint32_t best_sad = 0;
    int best_mode = -1;
    for (int m = 0; m <= PAETH_PRED; ++m) {
        uint8_t pred[16];
        svtd_call_builder(pred, m, 0, FILTER_INTRA_MODES, 0, above, n_top, n_topright, left, n_left,
                          n_bottomleft, above_left);
        const uint32_t sad = svtd_sad4x4(src, pred);
        if (best_mode < 0 || sad < best_sad) {
            best_sad = sad;
            best_mode = m;
        }
    }
    *best_sad_out = best_sad;
    return best_mode;
}

static void svtd_frame_auto_8x8(const uint8_t* src, uint8_t* recon, int32_t* coeffs, int* modes) {
    const int fstride = 8;
    const int gridW = 2, gridH = 2;
    memset(recon, 0, 64);
    for (int by = 0; by < gridH; ++by) {
        for (int bx = 0; bx < gridW; ++bx) {
            const int px = bx * 4, py = by * 4;
            const int hasTop = by > 0, hasLeft = bx > 0;
            const int nTop = hasTop ? 4 : 0;
            const int nLeft = hasLeft ? 4 : 0;
            const int nTr = (hasTop && bx + 1 < gridW) ? 4 : 0;
            uint8_t above[9] = {0};
            uint8_t left[9] = {0};
            uint8_t al = 0;
            if (hasTop) for (int i = 0; i < 4; ++i) above[i] = recon[(py - 1) * fstride + px + i];
            if (hasLeft) for (int i = 0; i < 4; ++i) left[i] = recon[(py + i) * fstride + px - 1];
            if (hasTop && hasLeft) al = recon[(py - 1) * fstride + px - 1];

            // NeighborContext for filt_type: chosen modes of the above/left
            // blocks (SMOOTH* -> 1). Unknown (first row/col) -> DC -> 0.
            const int aboveMode = hasTop ? modes[(by - 1) * gridW + bx] : DC_PRED;
            const int leftMode = hasLeft ? modes[by * gridW + bx - 1] : DC_PRED;
            svtd_filt_type = ((aboveMode == SMOOTH_PRED || aboveMode == SMOOTH_V_PRED ||
                               aboveMode == SMOOTH_H_PRED) ||
                              (leftMode == SMOOTH_PRED || leftMode == SMOOTH_V_PRED ||
                               leftMode == SMOOTH_H_PRED))
                                 ? 1
                                 : 0;

            const uint8_t srcblk[16] = {
                src[(py + 0) * fstride + px + 0], src[(py + 0) * fstride + px + 1],
                src[(py + 0) * fstride + px + 2], src[(py + 0) * fstride + px + 3],
                src[(py + 1) * fstride + px + 0], src[(py + 1) * fstride + px + 1],
                src[(py + 1) * fstride + px + 2], src[(py + 1) * fstride + px + 3],
                src[(py + 2) * fstride + px + 0], src[(py + 2) * fstride + px + 1],
                src[(py + 2) * fstride + px + 2], src[(py + 2) * fstride + px + 3],
                src[(py + 3) * fstride + px + 0], src[(py + 3) * fstride + px + 1],
                src[(py + 3) * fstride + px + 2], src[(py + 3) * fstride + px + 3]};

            uint32_t best_sad = 0;
            const int mode = svtd_decide(srcblk, above, nTop, nTr, left, nLeft, 0, al, &best_sad);
            modes[by * gridW + bx] = mode;

            uint8_t pred[16];
            svtd_call_builder(pred, mode, 0, FILTER_INTRA_MODES, 0, above, nTop, nTr, left, nLeft, 0, al);
            int16_t res[16];
            for (int i = 0; i < 16; ++i) res[i] = (int16_t)(srcblk[i] - pred[i]);
            int32_t cb[16];
            svtd_fwd2d4x4(res, 4, cb, svt_av1_fdct4_new);
            for (int i = 0; i < 16; ++i) coeffs[(by * gridW + bx) * 16 + i] = cb[i];
            svtd_inv2dadd4x4(cb, pred, 4, svt_av1_idct4_new);
            for (int i = 0; i < 16; ++i) recon[(py + (i >> 2)) * fstride + px + (i & 3)] = pred[i];
        }
    }
}

int main(void) {
    svtd_populate_dispatch();

    const uint8_t src[64] = {21, 3,  5,  9,  19, 2, 8,  14, 9,  11, 3, 7,  5,  23, 1, 17,
                             7,  13, 5,  1,  25, 4, 6,  18, 15, 4,  25, 2,  12, 9,  30, 3,
                             18, 5,  7,  13, 14, 2, 20, 8,  6,  24, 3,  9,  11, 17, 5, 19,
                             22, 1,  8,  15, 4,  29, 7, 13, 10, 16, 6, 12, 3,  25, 11, 9};
    uint8_t recon[64];
    int32_t coeffs[64];
    int modes[4] = {0, 0, 0, 0};
    svtd_frame_auto_8x8(src, recon, coeffs, modes);
    printf("d3_modes:");
    for (int i = 0; i < 4; ++i) printf(" %d", modes[i]);
    printf("\n");
    printf("d3_recon:");
    for (int i = 0; i < 64; ++i) printf(" %d", recon[i]);
    printf("\n");
    printf("d3_coeffs:");
    for (int i = 0; i < 64; ++i) printf(" %d", coeffs[i]);
    printf("\n");
    return 0;
}