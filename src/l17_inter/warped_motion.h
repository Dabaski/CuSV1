#pragma once

#include <cstdint>

#include "inter_prediction.h"

// l17_inter - the warped-motion surface (A3W). Ported 1:1 from
// third_party/SVT-AV1/Source/Lib/Codec/warped_motion.c. Every ported function
// cites its reference span; parks and deviations are named at their
// declaration. Namespace l17_inter per the slice brief; ConvolveParams /
// ConvBufType are the layer's shared types (namespace inter, one type in the
// reference too).
namespace l17_inter {

// ---------- constants ----------

// warped_motion.h:22-:27 - bits of precision used for the model.
constexpr int kWarpedModelPrecBits = 16;                            // WARPEDMODEL_PREC_BITS
constexpr int kWarpedModelTransClamp = 128 << kWarpedModelPrecBits; // :25 WARPEDMODEL_TRANS_CLAMP
constexpr int kWarpedModelNondiagaffineClamp =
    1 << (kWarpedModelPrecBits - 3);                                // :26 WARPEDMODEL_NONDIAGAFFINE_CLAMP

// warped_motion.h:30-:35 - subpel precision for warped interpolation.
constexpr int kWarpedPixelPrecBits = 6;                                  // WARPEDPIXEL_PREC_BITS
constexpr int kWarpedPixelPrecShifts = 1 << kWarpedPixelPrecBits;        // :31 WARPEDPIXEL_PREC_SHIFTS
constexpr int kWarpParamReduceBits = 6;                                  // :33 WARP_PARAM_REDUCE_BITS
constexpr int kWarpeddiffPrecBits = kWarpedModelPrecBits - kWarpedPixelPrecBits; // :35 WARPEDDIFF_PREC_BITS

// definitions.h:435/:430 - consumed by the warp filter's rounding chain.
constexpr int kFilterBits = 7;        // FILTER_BITS (filter.h:22)
constexpr int kDistPrecisionBits = 4; // DIST_PRECISION_BITS

// TransformationType, definitions.h:1740-:1746 (verbatim order).
enum TransformationType {
    Identity = 0,   // IDENTITY
    Translation,    // TRANSLATION
    Rotzoom,        // ROTZOOM
    Affine,         // AFFINE
    TransTypes,     // TRANS_TYPES
};

constexpr int kMaxParamDim = 6; // definitions.h:1748, MAX_PARAMDIM

// WarpedMotionParams, definitions.h:1755-:1760 (verbatim field set; the
// wmmat order documents the homography [m2 m3 m0; m4 m5 m1]).
struct WarpedMotionParams {
    TransformationType wmtype;
    std::int32_t wmmat[kMaxParamDim];
    std::int16_t alpha;
    std::int16_t beta;
    std::int16_t gamma;
    std::int16_t delta;
    std::int8_t invalid;
};

// default_warp_params, definitions.h:1774-:1779.
constexpr WarpedMotionParams defaultWarpParams = {Identity,
                                                  {0, 0, (1 << kWarpedModelPrecBits), 0, 0,
                                                   (1 << kWarpedModelPrecBits)},
                                                  0,
                                                  0,
                                                  0,
                                                  0,
                                                  0};

// definitions.h:354.
constexpr int kMiSize = 4; // MI_SIZE

// ---------- BlockSize mirror ----------

// definitions.h:883-:905 (verbatim order) - the l7/l5 mirror precedent; the
// l0_core BlockSize is the project-minimal 2-value enum and cannot index the
// block_size_wide/high tables.
enum BlockSize {
    BLOCK_4X4,
    BLOCK_4X8,
    BLOCK_8X4,
    BLOCK_8X8,
    BLOCK_8X16,
    BLOCK_16X8,
    BLOCK_16X16,
    BLOCK_16X32,
    BLOCK_32X16,
    BLOCK_32X32,
    BLOCK_32X64,
    BLOCK_64X32,
    BLOCK_64X64,
    BLOCK_64X128,
    BLOCK_128X64,
    BLOCK_128X128,
    BLOCK_4X16,
    BLOCK_16X4,
    BLOCK_8X32,
    BLOCK_32X8,
    BLOCK_16X64,
    BLOCK_64X16,
    kBlockSizesAll,
};

// block_size_wide / block_size_high (common_utils.c:286-:291, verbatim).
extern const std::uint8_t blockSizeWide[kBlockSizesAll];
extern const std::uint8_t blockSizeHigh[kBlockSizesAll];

// ---------- div_lut + resolve_divisor ----------

// warped_motion.c:291-:293.
constexpr int kDivLutPrecBits = 14;  // DIV_LUT_PREC_BITS
constexpr int kDivLutBits = 8;       // DIV_LUT_BITS
constexpr int kDivLutNum = 1 << 8;   // DIV_LUT_NUM

// div_lut, warped_motion.c:295-:313 (257 entries). MECHANICALLY EXTRACTED
// from the reference file (no hand-typed values); the caveat is stated: this
// is a transcription, not an algorithm - the tests diff-verify it
// entry-by-entry against the reference file itself and pin the
// 2^22/(256+i) approximation property.
extern const std::uint16_t divLut[kDivLutNum + 1];

// resolve_divisor_64, warped_motion.c:317-:332. Decomposes a divisor D such
// that 1/D = y/2^shift, with y at DIV_LUT_PREC_BITS precision. Precondition:
// D != 0 (the reference reads get_msb(D) unguarded).
std::int16_t resolveDivisor64(std::uint64_t d, std::int16_t* shift);

// resolve_divisor_32, warped_motion.c:334-:349.
std::int16_t resolveDivisor32(std::uint32_t d, std::int16_t* shift);

// svt_aom_warped_filter, warped_motion.c:57-:216 (the ACTIVE
// WARPEDPIXEL_PREC_BITS == 6 branch; the == 5 branch is dead in the pinned
// build). MECHANICALLY EXTRACTED into warped_filter_table.inc (the l7
// bucket-.inc precedent); transcription caveat stated, entry-by-entry
// diff-verified against the reference file in the tests.
extern const std::int16_t warpedFilter[kWarpedPixelPrecShifts * 3 + 1][8];

// ---------- get_mult_shift (the ACTIVE branch) ----------

// USE_LIMITED_PREC_MULT is 0 in the pinned build (warped_motion.c:218), so
// the active implementations are the #else bodies :276-:288 and the dead
// #if branch - resolve_multiplier_64 :224-:239 plus the limited-precision
// get_mult_shift pair :241-:272 - is PARKED, named (dead code in the
// reference build).

// get_mult_shift_ndiag, warped_motion.c:276-:281. Clamps to
// [-NONDIAGAFFINE_CLAMP+1, NONDIAGAFFINE_CLAMP-1].
std::int32_t getMultShiftNdiag(std::int64_t pX, std::int16_t iDet, int shift);

// get_mult_shift_diag, warped_motion.c:283-:288. Clamps to
// [2^16-NONDIAGAFFINE_CLAMP+1, 2^16+NONDIAGAFFINE_CLAMP-1].
std::int32_t getMultShiftDiag(std::int64_t pX, std::int16_t iDet, int shift);

// ---------- affine gates ----------

// is_affine_valid, warped_motion.c:351-:354 (file-local static in the
// reference; exported for testability, the DF1 signedCharClamp precedent).
bool isAffineValid(const WarpedMotionParams* wm);

// is_affine_shear_allowed, warped_motion.c:356-:363 (file-local static;
// exported for testability). Returns 0 when the fast warp filter's fixed
// 8-tap constraint is violated, 1 otherwise.
bool isAffineShearAllowed(std::int16_t alpha, std::int16_t beta, std::int16_t gamma, std::int16_t delta);

// svt_get_shear_params, warped_motion.c:907-:933. Derives alpha/beta/gamma/
// delta from wmmat and returns false on an invalid affine set (mat[2] <= 0
// or the shear gate above).
bool getShearParams(WarpedMotionParams* wm);

// ---------- find_projection ----------

// Mv, mv.h:42-:49 (the 1/8-pel motion vector; the amvp.h union minus the
// as_int view, which this surface never consumes).
struct Mv {
    std::int16_t x;
    std::int16_t y;
};

// find_affine_int, warped_motion.c:365-:471 (file-local static; exported for
// testability). Solves the least-squares affine over the sample pairs.
// Returns TRUE on the singular (det == 0) failure, false on success - the
// reference's own polarity.
bool findAffineInt(int np, const std::int32_t* pts1, const std::int32_t* pts2, BlockSize bsize, Mv mv,
                   WarpedMotionParams* wm, int miRow, int miCol);

// svt_find_projection, warped_motion.c:473-:485. Returns TRUE on failure
// (singular solve or shear-incompatible model), false on success.
bool findProjection(int np, const std::int32_t* pts1, const std::int32_t* pts2, BlockSize bsize, Mv mv,
                    WarpedMotionParams* wm, int miRow, int miCol);

// ---------- warp ----------

// svt_av1_warp_affine_c, warped_motion.c:571-:684 (the 8-bit warp; the
// highbd twin :719-:827 is PARKED, named).
void warpAffineC(const std::int32_t* mat, const std::uint8_t* ref, int width, int height, int stride,
                 std::uint8_t* pred, int pCol, int pRow, int pWidth, int pHeight, int pStride, int subsamplingX,
                 int subsamplingY, inter::ConvolveParams* convParams, std::int16_t alpha, std::int16_t beta,
                 std::int16_t gamma, std::int16_t delta);

// svt_warp_plane, warped_motion.c:686-:717. The ROTZOOM expansion
// (wmmat[5] = wmmat[2], wmmat[4] = -wmmat[3]) then dispatch to warpAffineC.
// The use_hbd dispatcher svt_av1_warp_plane :868-:904 and highbd_warp_plane
// :829-:866 are PARKED with the highbd twin.
void warpPlane(WarpedMotionParams* wm, const std::uint8_t* ref, int width, int height, int stride, std::uint8_t* pred,
               int pCol, int pRow, int pWidth, int pHeight, int pStride, int subsamplingX, int subsamplingY,
               inter::ConvolveParams* convParams);

// svt_aom_select_samples, warped_motion.c:935-:955. Selects samples
// according to the motion vector difference, compacting the survivor pairs
// to the front of both arrays; returns the count floored at 1.
std::uint8_t selectSamples(Mv mv, std::int32_t* pts, std::int32_t* ptsInref, int len, BlockSize bsize);

} // namespace l17_inter
