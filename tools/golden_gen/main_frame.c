// tools/golden_gen/main_frame.c
// Frame-policy composition: raster 4x4 loop over an 8x8 frame where each
// block's mode is DECIDED by the D2 policy (all 13 PredictionModes, scored
// by SAD(prediction vs source), lowest SAD wins, tie-break = lowest mode
// index) evaluated against RECONSTRUCTED neighbors. The policy is THIS
// PROJECT'S (documented, not SVT's ??? SVT uses full RD); the primitives it
// scores with are verbatim SVT.
// Output: mode map (4 blocks), recon frame (64), coeffs (64).
#include "composition.c"

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
