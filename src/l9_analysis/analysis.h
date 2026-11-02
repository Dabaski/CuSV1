#pragma once

#include <cstdint>

namespace analysis {

// EbColorFormat (third_party/SVT-AV1/Source/API/EbSvtAv1Formats.h:110):
// typedef enum EbColorFormat { EB_YUV400, EB_YUV420, EB_YUV422, EB_YUV444 }.
enum class ColorFormat : std::uint8_t { Yuv400 = 0, Yuv420 = 1, Yuv422 = 2, Yuv444 = 3 };

// 1:1 port of svt_compute_mean_c (third_party/SVT-AV1/Source/Lib/Codec/
// pic_analysis_process.c:194-216) at 8x8, the per-size wrapper pattern of
// l5_motion. Declaration twin: Codec/compute_mean.h:21 (classified A12 in
// tools/coverage_areas.txt; both anchors cited). VARIANCE_PRECISION = 16
// (pic_analysis_process.c:36).
std::uint64_t mean8x8(const std::uint8_t* src, std::uint32_t srcStride);

// 1:1 port of svt_compute_mean_squared_values_c (third_party/SVT-AV1/Source/
// Lib/Codec/pic_analysis_process.c:220-240) at 8x8, the per-size wrapper
// pattern of l5_motion. Declaration twin: Codec/compute_mean.h:26. uint64
// accumulation of src[hi] * src[hi] per pixel, then
// (sumSq << VARIANCE_PRECISION) / (width * height) - shift first, truncating
// division, no rounding term (pic_analysis_process.c:237).
std::uint64_t meanSquare8x8(const std::uint8_t* src, std::uint32_t srcStride);

// 1:1 port of svt_compute_sub_mean_8x8_c (third_party/SVT-AV1/Source/Lib/
// Codec/pic_analysis_process.c:242-261), the per-size wrapper pattern of
// l5_motion. Declaration twin: Codec/compute_mean.h:31. Sums the full
// 8-wide rows {0,2,4,6} (the skip = vi + vi loop, 32 px), then sum << 3
// with NO division (pic_analysis_process.c:257) - the commented-out
// formula (sum << (VARIANCE_PRECISION >> 1)) / (w*h/2) = (sum << 8) / 32
// equals sum << 3 exactly because 32 = 2^5 divides 2^8.
std::uint64_t subMean8x8(const std::uint8_t* src, std::uint16_t srcStride);

// 1:1 port of svt_aom_compute_sub_mean_squared_values_c
// (pic_analysis_process.c:263-284). Declaration twin: compute_mean.h:34.
// Generic over width/height: the skip = vi + vi loop sums
// src[hi] * src[hi] over ceil(height/2) rows (skip < input_area_height),
// then sumSq << 11 with NO division (:281) = (sumSq << VARIANCE_PRECISION)
// / (w*h/2) at (8,8) - the shift is HARDWIRED 11 for every (w,h),
// matching the code, not the comment.
std::uint64_t subMeanSquareValues(const std::uint8_t* src, std::uint32_t srcStride,
                                  std::uint32_t width, std::uint32_t height);

// 1:1 port of svt_compute_interm_var_four8x8_c (third_party/SVT-AV1/Source/
// Lib/Codec/pic_analysis_process.c:286-313). Declaration twin:
// Codec/compute_mean.h:40. COMPOSES the AN3 primitives - the reference
// calls its own sub-mean pair, it does not recompute: four pairs of
// subMean8x8 + subMeanSquareValues(src, stride, 8, 8) at blockIndex = 0,
// 8, 16, 24 accumulated +8, i.e. four side-by-side 8x8 blocks along ONE
// row origin, all reading rows {0,2,4,6}. The reference's own
// "(0,1)..(0,4)" comments are meaningless labels: the live caller
// (pic_analysis_process.c:350-359) passes the frame-strided buffer and
// advances by 32 per call.
void intermVarFour8x8(const std::uint8_t* src, std::uint16_t srcStride,
                      std::uint64_t* meanOf8x8Blocks,
                      std::uint64_t* meanOfSquared8x8Blocks);

// 1:1 port of calculate_histogram (third_party/SVT-AV1/Source/Lib/Codec/
// pic_analysis_process.c:170-188). NO header declaration twin exists:
// grep-verified, the function is file-local to its .c (the only Codec
// occurrences are the :167 comment, the :170 definition and the :560
// caller). Mechanics: the outer loop steps vertical_index by decim_step
// but selects the row by POINTER advance (src += stride * decim_step);
// the inner loop steps the column by decim_step; the PIXEL VALUE is the
// bin index into the 256-bin uint32 histogram; sum accumulates the
// SAMPLED pixels only (the caller rescales by decim_step^2 at :569 and
// pre-initializes the bins to 1 at :545-547 - the function only ADDS).
void calculateHistogram(const std::uint8_t* src, std::uint32_t width,
                        std::uint32_t height, std::uint32_t stride,
                        std::uint8_t decimStep, std::uint32_t* histogram,
                        std::uint64_t* sum);

// 1:1 port of svt_av1_count_colors_with_threshold (third_party/SVT-AV1/
// Source/Lib/Codec/pic_analysis_process.c:911-930): a 256-entry has_color
// flag array, one flag per distinct pixel value; the scan returns FALSE as
// soon as the running count exceeds num_colors_threshold (the early exit
// is part of the contract - *num_colors reads threshold + 1 on that path).
// The "> 1 colors" requirement lives at the AA-aware caller (:1118-1124),
// not here.
bool countColorsWithThreshold(const std::uint8_t* src, int stride, int rows,
                              int cols, int numColorsThreshold, int* numColors);

// 1:1 port of is_valid_palette_nb_colors (third_party/SVT-AV1/Source/Lib/
// Codec/pic_analysis_process.c:956-978, static): the has_color[] scan with
// the early-exit false at count > nb_colors_threshold, PLUS the palette
// gate its one caller (is_screen_content, :1374/:1415) relies on - a solid
// block (nb_colors <= 1) is NOT a valid palette (:973-975).
bool isValidPaletteNbColors(const std::uint8_t* src, int stride, int rows,
                            int cols, int nbColorsThreshold);

// 1:1 port of svt_av1_find_dominant_value (third_party/SVT-AV1/Source/Lib/
// Codec/pic_analysis_process.c:984-1003): the 256-bin count, argmax with a
// STRICTLY-greater update so the FIRST maximum wins ties.
std::uint8_t findDominantValue(const std::uint8_t* src, int stride, int rows, int cols);

// 1:1 port of svt_av1_dilate_block (third_party/SVT-AV1/Source/Lib/Codec/
// pic_analysis_process.c:1024-1076): copy src into dilated, then every
// pixel equal to the dominant value writes the dominant value into all
// eight of its neighbours (each bound-gated), one pass, no chaining.
void dilateBlock(const std::uint8_t* src, int srcStride, std::uint8_t* dilated,
                 int dilatedStride, int rows, int cols);

// 1:1 port of the VAR(W,H) macro instance svt_aom_variance16x16_c
// (third_party/SVT-AV1/Source/Lib/C_DEFAULT/variance.c:184-190, via
// variance_c :141-158), called the way the screen-content detectors call it
// (pic_analysis_process.c:950/:1129/:1150/:1376/:1417): b is
// svt_aom_eb_av1_var_offs (pic_analysis_process.c:936-942, constant 128) at
// ref_stride 0, folded here into the constant. diff = src[j] - 128 summed
// as int, squared as uint32; the return is
// sse - (uint32_t)(((int64_t)sum * sum) / 256) with the division
// truncating. The 128 offset cancels algebraically: the return is the
// population variance SUM of the block.
std::uint32_t variance16x16(const std::uint8_t* src, int srcStride, std::uint32_t* sse);

// 1:1 port of the VAR(W,H) instance svt_aom_variance8x8_c (C_DEFAULT/
// variance.c:184-190) at W*H = 64, same const-128 offs call shape
// (pic_analysis_process.c:1417).
std::uint32_t variance8x8(const std::uint8_t* src, int srcStride, std::uint32_t* sse);

// 1:1 ports of svt_av1_get_sby_perpixel_variance (third_party/SVT-AV1/
// Source/Lib/Codec/pic_analysis_process.c:944-952) at the two sizes the
// screen-content detectors use: var = the VAR kernel above against the
// constant-128 offs, returned as
// ROUND_POWER_OF_TWO(var, eb_num_pels_log2_lookup[bs])
// (definitions.h:457; common_utils.c:39 -> BLOCK_16X16 = 8, BLOCK_8X8 = 6),
// i.e. (var + 128) >> 8 and (var + 32) >> 6. The fn_ptr/BlockSize
// dispatch of the reference is replaced by one wrapper per size - the
// per-size wrapper pattern of l5_motion.
std::uint32_t getSbyPerpixelVariance16x16(const std::uint8_t* src, int stride);
std::uint32_t getSbyPerpixelVariance8x8(const std::uint8_t* src, int stride);

// The detector outputs: the pcs->sc_class0..5 fields (Codec/pcs.h:972-980,
// uint8_t each, assigned 0/1 by the detectors). bool in the port - the
// assigned values are identical.
struct ScClasses {
    bool scClass0;
    bool scClass1;
    bool scClass2;
    bool scClass3;
    bool scClass4;
    bool scClass5;
};

// 1:1 port of svt_aom_is_screen_content (third_party/SVT-AV1/Source/Lib/
// Codec/pic_analysis_process.c:1354-1429). The reference consumes
// pcs->enhanced_pic (the full-res luma plane); the port threads the y
// buffer + stride + dims (pcs/scs dropped, the b64Variance deviation
// pattern). Two passes: 16x16 with color_thresh 4 / var_thresh 0 feeding
// sc_class0..3 (:1386-1398), then 8x8 with color_thresh 4 / var_thresh 16
// feeding sc_class4 (:1427-1428). sc_class5 is NEVER written by this
// function (grep-verified :1386-1428) - the zero-init false is the port's
// initialization, not a reference value. The reference's
// is_valid_palette_nb_colors call passes blk_w into the rows parameter
// (:1374/:1415); moot at the square sizes the callers use - the port
// passes rows = block height, cols = block width.
ScClasses isScreenContent(const std::uint8_t* y, std::uint32_t yStride,
                          std::uint32_t width, std::uint32_t height);

// The Sc_AA_Counts carrier (pic_analysis_process.c:1078-1086, int64_t
// counts, int region counters).
struct ScAaCounts {
    std::int64_t countPhoto;
    std::int64_t countPalette;
    std::int64_t countIntrabc;
    std::int32_t regionPalette[4];
    std::int32_t regionIntrabc[4];
    std::int32_t regionPhoto[4];
};

// 1:1 port of svt_aom_sc_AA_collect_counts (third_party/SVT-AV1/Source/Lib/
// Codec/pic_analysis_process.c:1088-1190, static). The per-block classify
// chain, the fast-detection checkerboard skip (initial_col + 2*blk_w step,
// :1102-1104) and the x2 count multiplier (:1177-1187) are the reference's
// own. The fn_ptr/blk_size pair of the reference is replaced by the
// (blkW, blkH) already threaded here - the variance goes through the
// matching getSbyPerpixelVariance wrapper (BLOCK_16X16 / BLOCK_8X8 are the
// only two call shapes, :1261-1290). dilatedBlk is the caller-owned
// scratch the reference declares on its stack (:1255-1256), used only when
// a block takes the complex (dilate) branch.
ScAaCounts scAaCollectCounts(const std::uint8_t* y, std::uint32_t yStride,
                             std::uint32_t width, std::uint32_t height,
                             int blkW, int blkH,
                             int complexInitialColorThresh, int simpleColorThresh,
                             int complexFinalColorThresh, int varThresh,
                             bool fastDetection, std::uint8_t* dilatedBlk);

// 1:1 port of svt_aom_is_screen_content_antialiasing_aware (third_party/
// SVT-AV1/Source/Lib/Codec/pic_analysis_process.c:1207-1350). The
// reference consumes pcs->enhanced_pic and pcs->scs->
// fast_aa_aware_screen_detection_mode; the port threads the y buffer +
// stride + dims + the fast flag (pcs/scs dropped, the b64Variance
// deviation pattern). The two collect calls (16x16: final 6 / var 5;
// 8x8: final 8 / var 50) and all six class formulas (:1297-1321) are the
// reference's own; the caller zero-fills the sc classes for modes 0/1
// (pd_process.c:4796-4799) - NOT ported here (the detector writes all
// six unconditionally, as the reference does).
ScClasses isScreenContentAaAware(const std::uint8_t* y, std::uint32_t yStride,
                                 std::uint32_t width, std::uint32_t height,
                                 bool fastDetection);

// 1:1 port of svt_aom_is_input_luma_dominant (third_party/SVT-AV1/Source/
// Lib/Codec/pic_analysis_process.c:1440-1493) with the
// FRAME_LUMA_DOMINANT_* constants (:1431-1438): SAMPLE_STEP 8, CORE_THR 16,
// TAIL_THR 18, MIN_CORE_PCT 85, MIN_TAIL_PCT 95, NEUTRAL_THR 6,
// UV_DIFF_THR 4, MIN_NEUTRAL_PCT 75. The live caller passes pcs->
// chroma_downsampled_pic (pd_process.c:4813); the port threads the chroma
// planes + strides + the picture width/height the reference reads
// (uv_w = width >> 1, uv_h = height >> 1). The reference's
// !input_pic || color_format == EB_YUV400 || !u_buffer || !v_buffer guard
// (:1441-1443) becomes the monochrome flag - the port cannot receive a
// null buffer; the uv_w/uv_h == 0 guard (:1448-1450) is kept.
bool isInputLumaDominant(const std::uint8_t* u, const std::uint8_t* v,
                         std::uint32_t uStride, std::uint32_t vStride,
                         std::uint32_t width, std::uint32_t height,
                         bool monochrome);

// 1:1 port of svt_aom_downsample_2d_c (third_party/SVT-AV1/Source/Lib/Codec/
// pic_analysis_process.c:134-164). Declaration twin: Codec/motion_estimation.h:33-35.
// 2x2, 0-phase decimation generic over decim_step: the input pointer is
// PRE-ADVANCED by half_decim_step rows (:148) and prev_input_line is one row
// above it (:151), so the summed 2x2 window sits at rows/cols
// {half-1, half} WITHIN each decim_step cell (the cell's top-left 2x2 at
// step 2, its middle 2x2 at step 4); each output is (sum + 2) >> 2 (:157).
// Live callers pass decim_step 2 (quarter, :1504-1510) and 4 (sixteenth,
// :1530-1536). Host only: the GPU twin is parked, not built.
void downsample2d(const std::uint8_t* src, std::uint32_t srcStride,
                  std::uint32_t areaWidth, std::uint32_t areaHeight,
                  std::uint8_t* dst, std::uint32_t dstStride,
                  std::uint32_t decimStep);

// The reference's block_mean_calc_prec selector: #define BLOCK_MEAN_PREC_FULL
// 0 / BLOCK_MEAN_PREC_SUB 1 (definitions.h:2043-2044). The live reference
// always selects Sub (Globals/enc_handle.c:4339, unconditional assignment);
// the Full branch is ported because the reference carries it.
enum class BlockMeanPrec : std::uint8_t { Full = 0, Sub = 1 };

// 1:1 port of compute_b64_variance (third_party/SVT-AV1/Source/Lib/Codec/
// pic_analysis_process.c:318-442) - static there, single caller :610; this
// is the RDO-input chain's capstone, composing the landed AN primitives:
// the Sub branch is sixteen intermVarFour8x8 calls (:347-361, blk_8x8_idx =
// 8*row + 4*colGroup, blk_offset +32 per call), the Full branch is 64
// mean8x8 + meanSquare8x8 pairs (:335-346); the 16x16 fold averages the
// four 8x8 blocks at 16R + 2C {+0,+1,+8,+9} >> 2 (:364-380), the 32x32
// fold the four 16x16 blocks at 8P + 2Q {+0,+1,+4,+5} >> 2 (:383-399) and
// the 64x64 fold the four 32x32 blocks >> 2 (:402-409) - each array level
// folds from the level below, nothing recomputes from pixels. Variance =
// (sq - mean*mean) >> 16 (:415-417/:423-426/:432-435/:439-441), cast
// uint16. Named deviations from the reference signature: scs/pcs/
// b64_idx/input_luma_origin_index are dropped - y points AT the block
// origin and sbVar is ONE flat variance row (the reference writes
// pcs->variance[b64_idx], max 209 entries, me_sb_results.h:22); the
// sbVar layout is the EbMeTierZeroPu bases (me_context.h:47/:48/:52/:68):
// 64x64 at 0, 32x32 at 1, 16x16 at 5, 8x8 at 21, and ONLY 0..84 are ever
// written (85..208 untouched). The intermediate arrays the reference keeps
// on its stack are exposed as out-params for the tests (deviation, named).
// subBlockVariances threads the :412 gate (scs->allintra || aq_mode == 1
// || variance_octile): when false only the 64x64 entry is written. Host
// only: the GPU twin is parked, not built.
void b64Variance(const std::uint8_t* y, std::uint16_t yStride, BlockMeanPrec prec,
                 bool subBlockVariances, std::uint16_t* sbVar,
                 std::uint64_t* meanOf16x16Blocks,
                 std::uint64_t* meanOf16x16SquaredValuesBlocks,
                 std::uint64_t* meanOf32x32Blocks,
                 std::uint64_t* meanOf32x32SquaredValuesBlocks,
                 std::uint64_t* meanOf64x64Block,
                 std::uint64_t* meanOf64x64SquaredValuesBlock);

// 1:1 port of svt_aom_generate_padding (third_party/SVT-AV1/Source/Lib/
// Codec/pic_operators.c:266-301). src_pic points AT the first content pixel;
// the left pad is written at src_pic - padding_width. Horizontal pass first
// (replicate row[0] left and row[original_src_width - 1] right), then the
// vertical pass replicates the fully padded first/last rows over the FULL
// src_stride (row_bytes = src_stride, :275) - overhang included. The
// SVT_ERROR null guard (:268-271) becomes a silent return.
void generatePadding(std::uint8_t* srcPic, std::uint32_t srcStride,
                     std::uint32_t originalSrcWidth, std::uint32_t originalSrcHeight,
                     std::uint32_t paddingWidth, std::uint32_t paddingHeight);

// 1:1 port of svt_aom_generate_padding_compressed_10bit (third_party/
// SVT-AV1/Source/Lib/Codec/pic_operators.c:303-342) over the 2-bit-compressed
// plane: 4 pixels per byte, MSB first. The horizontal pad bytes are
// left_pixel * 0b01010101 (= the 2-bit value replicated into all four
// positions, :333) with left_pixel = (row[0] >> 6) & 3 and right_pixel =
// row[original_src_width / 4 - 1] & 3 (:330-331); the vertical pass copies
// src_stride bytes per row. The SVT_ERROR null guard (:311-314) becomes a
// silent return.
void generatePaddingCompressed10bit(std::uint8_t* srcPic, std::uint32_t srcStride,
                                    std::uint32_t originalSrcWidth, std::uint32_t originalSrcHeight,
                                    std::uint32_t paddingWidth, std::uint32_t paddingHeight);

// 1:1 port of pad_input_picture (third_party/SVT-AV1/Source/Lib/Codec/
// pic_operators.c:393-436): right pad replicates the last content pixel
// per row (first), then the bottom pad copies (original_src_width +
// pad_right) bytes of the last padded row. The SVT_ERROR null guard
// (:404-407) becomes a silent return.
void padInputPicture(std::uint8_t* srcPic, std::uint32_t srcStride,
                     std::uint32_t originalSrcWidth, std::uint32_t originalSrcHeight,
                     std::uint32_t padRight, std::uint32_t padBottom);

// 1:1 port of svt_aom_pad_input_picture_16bit (third_party/SVT-AV1/Source/
// Lib/Codec/pic_operators.c:441-479) at uint16: same right-then-bottom
// shape, memset16bit (pic_operators.h:39-45) substituted with the same
// element loop, the bottom copy sizeof(uint16_t) * (width + pad_right)
// elements. The reference has NO null guard here - none added.
void padInputPicture16bit(std::uint16_t* srcPic, std::uint32_t srcStride,
                          std::uint32_t originalSrcWidth, std::uint32_t originalSrcHeight,
                          std::uint32_t padRight, std::uint32_t padBottom);

// 1:1 port of pad_2b_compressed_input_picture (third_party/SVT-AV1/Source/
// Lib/Codec/pic_analysis_process.c:648-739, static). The 2-bit plane: 4
// pixels per byte, MSB first. pad_right comes from the 8-alignment
// (resource_coordination_process.c:717-718): pad_right = 8 - (width % 8),
// so pad_right == 4 iff width % 4 == 0 and width % 4 != 0 for every other
// nonzero pad_right. last_col = width / 4. For width % 4 != 0,
// src[last_col] is the last PARTIAL content byte (its top bits hold the
// trailing real pixels; the branch masks to keep exactly those and OR-fills
// the pad slots, plus one fully replicated byte at last_col + 1 for
// pad_right >= 5). For pad_right == 4, src[last_col] is the first pure-pad
// byte - the branch alone reads src[last_col - 1], takes its lowest 2 bits
// (the last real pixel) and writes the replicated byte at src[last_col].
// The bottom pass copies (width + pad_right) / 4 bytes per pad row (the
// padded row's exact byte count: width + pad_right is a multiple of 8).
// The :728 svt_aom_assert_err becomes assert() (the l8_bitstream pattern).
void pad2bCompressedInputPicture(std::uint8_t* srcPic, std::uint32_t srcStride,
                                 std::uint32_t originalSrcWidth, std::uint32_t originalSrcHeight,
                                 std::uint32_t padRight, std::uint32_t padBottom);

// 1:1 port of svt_aom_pad_picture_to_multiple_of_min_blk_size_dimensions
// (third_party/SVT-AV1/Source/Lib/Codec/pic_analysis_process.c:745-813).
// THE CONTRACT: the reference receives scs + input_pic where input_pic->
// width is ALREADY the padded width (resource_coordination_process.c:717-719
// updates max_input_luma_width in place) and scs->pad_right is the padding
// amount, so the content width is width - pad_right (:756). The port
// threads the same (padded width, padRight) pair and derives the content
// internally - do NOT pass the content width here. Parameters thread the
// raw values the reference consumes (scs/input_pic dropped, the b64Variance
// deviation pattern); any of the six buffers may be null (the reference's
// guards :761/:770 and :784/:793/:802). The subsampling is derived from
// colorFormat INSIDE, with the 8-bit driver's own textual form (:750-751):
// ss_x = (cf == Yuv444 ? 0 : 1), ss_y = (cf >= Yuv422 ? 0 : 1).
// is16BitInput threads the reference's
// SVT_EFFECTIVE_BIT_DEPTH(encoder_bit_depth) > EB_EIGHT_BIT; the comp
// strides are computed INTERNALLY as y_stride/4 and u_stride/4 (:780-782),
// not threaded. The 16-bit driver's own textual subsampling form (:825-826)
// is provably identical to this one for all four EbColorFormat members.
void padToMinBlkSizeDimensions(std::uint8_t* yBuffer, std::uint32_t yStride,
                               std::uint8_t* uBuffer, std::uint32_t uStride,
                               std::uint8_t* vBuffer, std::uint32_t vStride,
                               std::uint8_t* yBufferBitInc, std::uint8_t* uBufferBitInc,
                               std::uint8_t* vBufferBitInc,
                               std::uint32_t width, std::uint32_t height,
                               std::uint32_t padRight, std::uint32_t padBottom,
                               bool is16BitInput, ColorFormat colorFormat);

// 1:1 port of svt_aom_pad_picture_to_multiple_of_min_blk_size_dimensions_16bit
// (third_party/SVT-AV1/Source/Lib/Codec/pic_analysis_process.c:820-852):
// the uint16 plane driver over padInputPicture16bit. The subsampling is
// derived with the 16-bit driver's own textual form (:825-826): ss_x =
// (cf == Yuv444 ? 0 : 1), ss_y = ((cf == Yuv444 || cf == Yuv422) ? 0 : 1) -
// provably identical to the 8-bit driver's form for all four enum members
// (x: both give 0 iff cf == 3; y: both give 0 iff cf in {2, 3}). The
// reference's assert(encoder_bit_depth > EB_EIGHT_BIT) drops with the scs
// indirection; the reference casts the uint8_t* desc fields to uint16_t*,
// the port takes uint16_t* directly (named deviation). No bit-inc, no
// is16BitInput here - the reference has none.
void padToMinBlkSizeDimensions16bit(std::uint16_t* yBuffer, std::uint32_t yStride,
                                    std::uint16_t* uBuffer, std::uint32_t uStride,
                                    std::uint16_t* vBuffer, std::uint32_t vStride,
                                    std::uint32_t width, std::uint32_t height,
                                    std::uint32_t padRight, std::uint32_t padBottom,
                                    ColorFormat colorFormat);

// 1:1 port of svt_aom_pad_picture_to_multiple_of_sb_dimensions (third_party/
// SVT-AV1/Source/Lib/Codec/pic_analysis_process.c:858-866): the thin Y-only
// wrapper over generatePadding with padding_width = padding_height = border.
void padToSbDimensions(std::uint8_t* yBuffer, std::uint32_t yStride,
                       std::uint32_t width, std::uint32_t height,
                       std::uint32_t border);

// 1:1 port of svt_aom_pad_input_pictures (third_party/SVT-AV1/Source/Lib/
// Codec/pic_analysis_process.c:1549-1615), the full driver. ORDER IS THE
// CONTRACT: the min-blk edge pad runs FIRST (padRight/padBottom - the
// scs->pad_right/pad_bottom quantities), then generate_padding around the
// PADDED extent (width/height + border - the input_pic->border quantity, a
// DIFFERENT source from padRight), then the 10-bit comp border passes.
// Unlike the drivers above, the reference does NOT derive subsampling here
// - it reads scs->subsampling_x/y (:1580-1582) - so the port threads
// subsamplingX/subsamplingY explicitly, mirroring the reference's own
// divergence. is16BitInput gates the comp passes; compStrideY/Uv are
// computed internally (y_stride/4, u_stride/4, :1556-1557). The chroma
// border passes use width/height/border >> subsampling; the luma comp
// border pass uses the FULL border (the reference passes input_pic->border
// in pixels with the packed byte stride - the /4 inside the comp function
// is the reference's own arithmetic). colorFormat feeds ONLY the min-blk
// driver call (the reference derives that pass's subsampling from
// input_pic->color_format) while the border passes read the explicit
// subsamplingX/subsamplingY (the reference's scs->subsampling_x/y) - two
// different sources in the reference, threaded as such. Any buffer may be
// null (the reference's guards :1565/:1577/:1586/:1597/:1606).
void padInputPictures(std::uint8_t* yBuffer, std::uint32_t yStride,
                      std::uint8_t* uBuffer, std::uint32_t uStride,
                      std::uint8_t* vBuffer, std::uint32_t vStride,
                      std::uint8_t* yBufferBitInc, std::uint8_t* uBufferBitInc,
                      std::uint8_t* vBufferBitInc,
                      std::uint32_t width, std::uint32_t height,
                      std::uint32_t padRight, std::uint32_t padBottom,
                      std::uint32_t border,
                      std::uint16_t subsamplingX, std::uint16_t subsamplingY,
                      bool is16BitInput, ColorFormat colorFormat);

// 1:1 port of svt_aom_downsample_filtering_input_picture (third_party/
// SVT-AV1/Source/Lib/Codec/pic_analysis_process.c:1498-1547), the HME
// input-picture downsampling driver: it COMPOSES downsample2d (AN6) and
// generatePadding (AN10), so the port is branch structure and call order.
// Declaration twin: pic_analysis_process.h:28. resize.c:139 carries the
// same signature and is a DECLARATION (semicolon at resize.c:141, no body)
// - the reference's only definition is pic_analysis_process.c:1498. The
// reference reads six pcs booleans ORed in pairs (:1502 outer gate
// enable_hme/tf_enable_hme, :1503/:1521 the level1 pair read TWICE,
// :1519 the level0 pair); the port threads the six raw values and does the
// ORs inside, mirroring the reference's reads. Each generate_padding call
// uses its OWN buffer's dims and border (:1511-1516 quarter, :1539-1544
// sixteenth). The sixteenth SOURCE depends on level1: step 2 from the
// quarter buffer when the level1 pair is on (:1522-1528), step 4 from the
// input when off (:1530-1536). ORDER: the quarter padding runs INSIDE the
// level1 branch, before the sixteenth pass (:1511 before :1522) - text
// pinned, not test pinned: downsample2d's window sits at rows {half-1,
// half} WITHIN each cell over the quarter CONTENT dims, so the
// sixteenth-from-quarter pass reads NO quarter pad rows and swapping the
// two statements changes no output. The reference has NO null guards - the
// branch structure IS the guard; none added. Callers (grep-verified):
// pd_process.c:3555 and pic_analysis_process.c:1992 (the latter gated on
// !scs->allintra) are live; resize.c:1665/:1726 (Phase D) and
// temporal_filtering.c:3770 (Phase C) are parked. All pass the same
// three-buffer shape. Only the Y plane is touched.
void downsampleFilteringInputPicture(
    const std::uint8_t* inputPaddedY, std::uint32_t inputPaddedYStride,
    std::uint32_t inputPaddedWidth, std::uint32_t inputPaddedHeight,
    std::uint8_t* quarterY, std::uint32_t quarterYStride,
    std::uint32_t quarterWidth, std::uint32_t quarterHeight,
    std::uint32_t quarterBorder,
    std::uint8_t* sixteenthY, std::uint32_t sixteenthYStride,
    std::uint32_t sixteenthWidth, std::uint32_t sixteenthHeight,
    std::uint32_t sixteenthBorder,
    bool hmeEnabled, bool tfHmeEnabled, bool level1Enabled,
    bool tfLevel1Enabled, bool level0Enabled, bool tfLevel0Enabled);

}  // namespace analysis
