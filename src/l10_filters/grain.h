#pragma once

#include <array>
#include <cstdint>
#include <vector>

// GR1 (A8 campaign): 1:1 port of the film grain synthesis core of
// third_party/SVT-AV1/Source/Lib/Codec/grainSynthesis.c (1,140 nb total).
// LANDED so far: gaussian_sequence :28-:156 (transcribed mechanically).
// The reference's file-local statics are exported as plain functions
// (LR1/LR2 precedent, named deviation); bit_depth stays symbolic with the
// 8-bit path the only live one (highbd assert-parked).

namespace l10_filters {

namespace grain {

// AomFilmGrain (Source/API/EbSvtAv1.h:355-:429): minimal field-verbatim
// copy. BRIEF CORRECTION: the struct lives in EbSvtAv1.h, not
// grainSynthesis.h - grainSynthesis.h only declares the three functions
// and includes EbSvtAv1.h. Field names and order verbatim.
struct AomFilmGrain {
    std::int32_t apply_grain; // :357
    std::int32_t update_parameters; // :360
    std::int32_t scaling_points_y[14][2]; // :363
    std::int32_t num_y_points; // :364
    std::int32_t scaling_points_cb[10][2]; // :367
    std::int32_t num_cb_points; // :368
    std::int32_t scaling_points_cr[10][2]; // :371
    std::int32_t num_cr_points; // :372
    std::int32_t scaling_shift; // :375
    std::int32_t ar_coeff_lag; // :378
    std::int32_t ar_coeffs_y[24]; // :381
    std::int32_t ar_coeffs_cb[25]; // :382
    std::int32_t ar_coeffs_cr[25]; // :383
    std::int32_t ar_coeff_shift; // :390
    std::int32_t cb_mult; // :394
    std::int32_t cb_luma_mult; // :397
    std::int32_t cb_offset; // :399
    std::int32_t cr_mult; // :403
    std::int32_t cr_luma_mult; // :406
    std::int32_t cr_offset; // :408
    std::int32_t overlap_flag; // :411
    std::int32_t clip_to_restricted_range; // :414
    std::int32_t bit_depth; // :416
    std::int32_t chroma_scaling_from_luma; // :419
    std::int32_t grain_scale_shift; // :422
    std::uint16_t random_seed; // :425
    std::int32_t ignore_ref; // :428
};

// One prediction-position row {row offset, col offset, luma flag} - the
// reference's malloc'd int32_t[3] rows, flattened to a contiguous array
// (named deviation: the jagged int32_t** shape becomes PredPosRow*).
using PredPosRow = std::array<std::int32_t, 3>;

// The init_arrays working set (grainSynthesis.c:182-:264): prediction
// positions plus the grain/line/col buffers. The reference mallocs each
// block; here std::vector owns the memory and dealloc_arrays
// (:266-:303, 27 nb) is PARKED as a no-op - RAII frees it (named
// deviation).
struct GrainWork {
    std::vector<PredPosRow> predPosLuma;
    std::vector<PredPosRow> predPosChroma;
    std::vector<std::int32_t> lumaGrainBlock;
    std::vector<std::int32_t> cbGrainBlock;
    std::vector<std::int32_t> crGrainBlock;
    std::vector<std::int32_t> yLineBuf;
    std::vector<std::int32_t> cbLineBuf;
    std::vector<std::int32_t> crLineBuf;
    std::vector<std::int32_t> yColBuf;
    std::vector<std::int32_t> cbColBuf;
    std::vector<std::int32_t> crColBuf;
};

// Test helper: a zero-initialized AomFilmGrain (the reference's callers
// build it field by field; this is port-side convenience, named
// deviation).
AomFilmGrain makeParams();

// init_arrays (grainSynthesis.c:182-:264): zero the module scaling LUTs,
// build the prediction-position tables and allocate the working buffers.
// The luma/chroma subblock statics (:160-:164) must already hold the
// values the driver wants (the reference shares the same ordering).
GrainWork initArrays(const AomFilmGrain* params, std::int32_t lumaStride,
                     std::int32_t chromaStride, std::int32_t lumaGrainSamples,
                     std::int32_t chromaGrainSamples,
    std::int32_t chromaSubsampY, std::int32_t chromaSubsampX);

// initScalingLuts: the driver's LUT init block (grainSynthesis.c:967-:975),
// factored out for direct testability (named deviation): y from
// scaling_points_y; when chroma_scaling_from_luma, cb and cr copy the y
// LUT, otherwise each comes from its own points.
void initScalingLuts(const AomFilmGrain* params,
                     const std::int32_t yPoints[][2],
                     const std::int32_t cbPoints[][2],
                     const std::int32_t crPoints[][2]);

// add_noise_to_block (grainSynthesis.c:506-:605): add the scaled grain to
// the chroma planes (subsampled luma average drives the index) and the
// luma plane. half dims in, luma covers (2*half) x (2*half). bit_depth
// stays symbolic; the reference hardcodes 8 into its scale_lut calls here.
void addNoiseToBlock(const AomFilmGrain* params, std::uint8_t* luma,
                     std::uint8_t* cb, std::uint8_t* cr,
                     std::int32_t lumaStride, std::int32_t chromaStride,
                     const std::int32_t* lumaGrain,
                     const std::int32_t* cbGrain, const std::int32_t* crGrain,
                     std::int32_t lumaGrainStride,
                     std::int32_t chromaGrainStride,
                     std::int32_t halfLumaHeight, std::int32_t halfLumaWidth,
                     std::int32_t bitDepth, std::int32_t chromaSubsampY,
                     std::int32_t chromaSubsampX);

// add_noise_to_block_hbd (grainSynthesis.c:607-:709): the uint16 twin - the
// GR1 hbd park resolves. bd-scaled cb/cr offsets ((offset << (bd-8)) -
// (1 << bd)), bd-scaled clamps, scale_lut at bit_depth. REFERENCE QUIRK
// kept 1:1: apply_cb/apply_cr gate on num_cb_points/num_cr_points ALONE
// here (:624-:626) - unlike the 8-bit twin (:522-:523) chroma_scaling_
// from_luma alone does NOT enable the chroma pass, so the two twins
// diverge for CSFL without points (pinned by test).
void addNoiseToBlockHbd(const AomFilmGrain* params, std::uint16_t* luma,
                        std::uint16_t* cb, std::uint16_t* cr,
                        std::int32_t lumaStride, std::int32_t chromaStride,
                        const std::int32_t* lumaGrain,
                        const std::int32_t* cbGrain, const std::int32_t* crGrain,
                        std::int32_t lumaGrainStride,
                        std::int32_t chromaGrainStride,
                        std::int32_t halfLumaHeight, std::int32_t halfLumaWidth,
                        std::int32_t bitDepth, std::int32_t chromaSubsampY,
                        std::int32_t chromaSubsampX);

// ver_boundary_overlap (grainSynthesis.c:816-:838): blend a 1- or 2-wide
// vertical boundary, clamping to the grain range; widths above 2 write
// nothing (the reference returns silently). File-local in the reference;
// exported for direct testability (named deviation).
void verBoundaryOverlap(std::int32_t* leftBlock, std::int32_t leftStride,
                        std::int32_t* rightBlock, std::int32_t rightStride,
                        std::int32_t* dstBlock, std::int32_t dstStride,
                        std::int32_t width, std::int32_t height);

// hor_boundary_overlap (grainSynthesis.c:840-:863): the transposed twin.
void horBoundaryOverlap(std::int32_t* topBlock, std::int32_t topStride,
                        std::int32_t* bottomBlock, std::int32_t bottomStride,
                        std::int32_t* dstBlock, std::int32_t dstStride,
                        std::int32_t width, std::int32_t height);

// film_grain_params_equal (grainSynthesis.c:709-:791): the reference's
// field gate set - 17 scalars + 6 array memcmps; update_parameters,
// bit_depth, random_seed and ignore_ref are deliberately not compared
// (reference behavior).
std::int32_t filmGrainParamsEqual(const AomFilmGrain* parsA,
                                  const AomFilmGrain* parsB);

// svt_aom_fgn_copy_rect (grainSynthesis.c:793-:803): row-wise byte copy;
// with use_high_bit_depth each pixel is two bytes (the reference's
// hbd_coeff = 2 path).
void fgnCopyRect(std::uint8_t* src, std::int32_t srcStride,
                 std::uint8_t* dst, std::int32_t dstStride, std::int32_t width,
                 std::int32_t height, std::int32_t useHighBitDepth);

// copy_area (grainSynthesis.c:805-:814): row-wise int32 copy. File-local
// in the reference; exported for direct testability (named deviation).
void copyArea(std::int32_t* src, std::int32_t srcStride, std::int32_t* dst,
              std::int32_t dstStride, std::int32_t width,
              std::int32_t height);

// svt_av1_add_film_grain_run (grainSynthesis.c:865-:1294): the frame
// driver - generate the grain template, init the scaling LUTs, then the
// 32x32-subblock walk with the ver/hor overlap machinery (line/col
// buffers). use_high_bit_depth != 0 is PARKED (named): assert, the 8-bit
// path is the only live one.
void addFilmGrainRun(const AomFilmGrain* params, std::uint8_t* luma,
                     std::uint8_t* cb, std::uint8_t* cr, std::int32_t height,
                     std::int32_t width, std::int32_t lumaStride,
                     std::int32_t chromaStride, std::int32_t useHighBitDepth,
                     std::int32_t chromaSubsampY,
                     std::int32_t chromaSubsampX);

// generate_luma_grain_block (grainSynthesis.c:328-:363): fill the grain
// block with seeded white noise, then run the AR-coefficient filter pass
// over the unpadded interior, clamping to the module grain range. The
// predPosLuma table comes from initArrays; pass nullptr only when
// ar_coeff_lag is 0 (no positions are ever read).
void generateLumaGrainBlock(const AomFilmGrain* params,
                            const PredPosRow* predPosLuma,
                            std::int32_t* lumaGrainBlock,
                            std::int32_t lumaBlockSizeY,
                            std::int32_t lumaBlockSizeX,
                            std::int32_t lumaGrainStride,
                            std::int32_t leftPad, std::int32_t topPad,
                            std::int32_t rightPad, std::int32_t bottomPad);

// generate_chroma_grain_blocks (grainSynthesis.c:365-:464): seed the cb/cr
// grain blocks (rows 7<<5 / 11<<5 of the LCG; a zero point count without
// chroma_scaling_from_luma leaves that block zero), then the AR pass over
// the interior - the (0,0,1) position averages the luma grain block over
// the subsampling window.
void generateChromaGrainBlocks(
    const AomFilmGrain* params, const PredPosRow* predPosChroma,
    const std::int32_t* lumaGrainBlock, std::int32_t* cbGrainBlock,
    std::int32_t* crGrainBlock, std::int32_t lumaGrainStride,
    std::int32_t chromaBlockSizeY, std::int32_t chromaBlockSizeX,
    std::int32_t chromaGrainStride, std::int32_t leftPad,
    std::int32_t topPad, std::int32_t rightPad, std::int32_t bottomPad,
    std::int32_t chromaSubsampY, std::int32_t chromaSubsampX);

// gaussian_sequence (grainSynthesis.c:28-:156): the AV1 spec's fixed
// 2,048-entry zero-mean Gaussian table, range [-2048, 2047], sigma ~512.
// Transcribed mechanically from the pinned reference (paired-metric
// caveat: transcription moves the coverage number without moving any
// conformance count).
extern const std::int32_t gaussianSequence[2048];

// Table extent as a named constant for the structural test.
constexpr std::int32_t gaussianSequenceSize = 2048;

// get_random_number (grainSynthesis.c:306-:311): the 16-bit LCG draw,
// bits in [1, 16]. File-local static INLINE in the reference; exported for
// direct testability (named deviation, LR1 boxsum1 precedent). Mutates the
// module register state exactly as the reference does.
std::int32_t getRandomNumber(std::int32_t bits);

// init_random_generator (grainSynthesis.c:313-:326): register = seed,
// then the per-row xor pair keyed on luma_line >> 5.
void initRandomGenerator(std::int32_t lumaLine, std::uint16_t seed);

// initGrainRange: the driver's grain_center/min/max derivation
// (grainSynthesis.c:916-:918), factored out of svt_av1_add_film_grain_run
// for direct testability of the generate/add kernels (named deviation).
// grain_center = 128 << (bit_depth - 8); min = -center;
// max = (256 << (bit_depth - 8)) - 1 - center.
void initGrainRange(std::int32_t bitDepth);

// init_scaling_function (grainSynthesis.c:466-:489): fill a 256-entry LUT
// from the scaling-point pairs {x, y}; num_points == 0 leaves the LUT
// untouched (the caller's zeroing supplies the flat LUT, as the
// reference's init_arrays does).
void initScalingFunction(const std::int32_t scalingPoints[][2],
                         std::int32_t numPoints, std::int32_t* scalingLut);

// scale_lut (grainSynthesis.c:493-:504): sample a scaling LUT at a
// bit-depth-wide index, interpolating between adjacent 8-bit entries for
// bit depths above 8.
std::int32_t scaleLut(std::int32_t* scalingLut, std::int32_t index,
                      std::int32_t bitDepth);

} // namespace grain

} // namespace l10_filters
