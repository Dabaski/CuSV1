#pragma once

#include <cstdint>

#include <array>
#include <vector>

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

// 1:1 port of variance_c (third_party/SVT-AV1/Source/Lib/C_DEFAULT/
// variance.c:141-158) - static there, the core the VAR(W,H) macro drives;
// exported for the tests (the AN8 statics precedent).
void varianceCore(const std::uint8_t* a, int aStride, const std::uint8_t* b,
                  int bStride, int w, int h, std::uint32_t* sse, int* sum);

// PU1: the missing VAR(W,H) macro instances of C_DEFAULT/variance.c in the
// GENERAL two-buffer shape (the reference macro :184-190; the AN8
// variance16x16/8x8 above are the offs-folded single-buffer shape). Each
// instance: varianceCore(a, aStride, b, bStride, W, H, sse, &sum), then
// *sse - (uint32_t)(((int64_t)sum * sum) / (W * H)) (:189, truncating).
// The 20 declarations below complete the VARIANCES(W, H) enumeration
// (:208-:229) - the 16x16 and 8x8 instances already landed with AN8.

// 1:1 port of the VAR(4, 4) instance svt_aom_variance4x4_c (:222).
std::uint32_t variance4x4(const std::uint8_t* a, int aStride,
                          const std::uint8_t* b, int bStride, std::uint32_t* sse);

// The remaining VARIANCES(W, H) instances, in the reference's own order
// (C_DEFAULT/variance.c:208-:229). The 16x16 and 8x8 shapes are the
// offs-folded single-buffer declarations above (AN8).
std::uint32_t variance128x128(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance128x64(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance64x128(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance64x64(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance64x32(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance32x64(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance32x32(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance32x16(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance16x32(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance16x8(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance8x16(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance8x4(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance4x8(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance4x16(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance16x4(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance8x32(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance32x8(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance16x64(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);
std::uint32_t variance64x16(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, std::uint32_t* sse);

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

// 1:1 ports of the SVT-AV1 1D FFT kernels
// (third_party/SVT-AV1/Source/Lib/Codec/fft_common.h:113-414, macro
// GEN_FFT_2/4/8/16/32 with the float suffix). The macros expand verbatim
// to the five functions below; the float suffix is the only instantiation
// used by the live consumer (fft.c:82-86). The first float slice in the
// layer - all preceding primitives are fixed-point, these carry no rounding
// mode choice of their own: the kernel arithmetic is exactly the reference
// float expression graph, evaluated under the same IEEE-754 single
// precision.
// Declaration twin: fft_common.h:49-52 (svt_aom_fft1d_4/8/16/32_float;
// svt_aom_fft1d_2_float is instantiated by the macro with no separate
// forward declaration in the reference - the GEN_FFT_2 macro expansion
// itself is the source of truth).
// Real-input transform: n floats in, n floats out packed as
// [r_0..r_{n/2}, i_1..i_{n/2-1}] (fft_common.h:24-27).
void fft1d2(const float* input, float* output, std::int32_t stride);
void fft1d4(const float* input, float* output, std::int32_t stride);
void fft1d8(const float* input, float* output, std::int32_t stride);
void fft1d16(const float* input, float* output, std::int32_t stride);
void fft1d32(const float* input, float* output, std::int32_t stride);

// 1:1 ports of the SVT-AV1 1D INVERSE FFT kernels (fft_common.h:416-823,
// macro GEN_IFFT_2/4/8/16/32 with the float suffix; declaration twin:
// none - the reference declares no ifft1d prototypes in fft_common.h, the
// macro expansions at fft.c:173-177 are the source of truth; grep-verified).
// The input is the same packed conjugate-symmetric spectrum, the output is
// the UNNORMALIZED real inverse (n * the forward's input signal).
void ifft1d2(const float* input, float* output, std::int32_t stride);
void ifft1d4(const float* input, float* output, std::int32_t stride);
void ifft1d8(const float* input, float* output, std::int32_t stride);
void ifft1d16(const float* input, float* output, std::int32_t stride);
void ifft1d32(const float* input, float* output, std::int32_t stride);

// AN13: the 2D FFT drivers (third_party/SVT-AV1/Source/Lib/Codec/fft.c),
// composing the landed AN12 1D kernels - the port never re-implements a
// 1D butterfly. COMPOSITION (fft_2d_gen, fft.c:51-64): five passes -
// fft1dN(input + x, output + x, n) per column x, simple_transpose,
// fft1dN(temp + x, output + x, n) per second-axis row, simple_transpose,
// then unpack_2d_output. NO scaling in any pass (fft.c has zero arithmetic
// divisions - verified).
//
// THE UNPACK CONTRACT (fft.c:23-49): output is 2*n*n floats holding
// n*(n/2+1) complex values interleaved as output[2*(y*n+x)] = Re F(y,x),
// output[2*(y*n+x)+1] = Im F(y,x) for ALL y but ONLY x in 0..n/2; the tail
// of the output buffer is NEVER written by the port (the caller zeroes it,
// noise_util.c:65). simple_transpose and unpack_2d_output are file-local
// helpers in fft2d.cpp (the reference keeps them static INLINE in fft.c).
// vec_size is 1 at every reference _c entry point (fft.c:89-105 /
// :180-236); the port folds it - named deviation, noted per function.
//
// NORMALIZATION FINDING (verified fft.c:1-237): no scaling anywhere; the
// caller divides (noise_util.c:96-102 does ifft then data[i] /= n). So
// ifft2d(fft2d(f)) == n*n * f.
// Entry-point names mirror the reference's per-size wrappers
// svt_aom_fft2x2_float_c ... svt_aom_fft32x32_float_c (fft.c:88-106) and
// svt_aom_ifft2x2_float_c ... svt_aom_ifft32x32_float_c (fft.c:179-237).
void fft2d2(const float* input, float* temp, float* output);
void fft2d4(const float* input, float* temp, float* output);
void fft2d8(const float* input, float* temp, float* output);
void fft2d16(const float* input, float* temp, float* output);
void fft2d32(const float* input, float* temp, float* output);

void ifft2d2(const float* input, float* temp, float* output);
void ifft2d4(const float* input, float* temp, float* output);
void ifft2d8(const float* input, float* temp, float* output);
void ifft2d16(const float* input, float* temp, float* output);
void ifft2d32(const float* input, float* temp, float* output);

// AN16: the noise strength solver (third_party/SVT-AV1/Source/Lib/Codec/
// noise_model.c:104-397). linsolve is the port's own private copy of the
// dependency at Codec/mathutils.h:22-65 (A13-classified header; the
// reference keeps it static INLINE so every includer carries a copy - the
// port carries one copy in the l9 TU and exposes it here for the tests,
// named deviation). Verbatim dense Gaussian elimination with partial
// pivoting, tiny_near_zero 1e-16; returns 0 on a singular system.
int32_t linsolve(int32_t n, double* a, int32_t stride, double* b, double* x);

// AomNoiseShape (noise_model.h:150): DIAMOND = 0, SQUARE = 1.
enum class AomNoiseShape : std::int32_t { Diamond = 0, Square = 1 };

// AomNoiseModelParams (noise_model.h:154-160): shape, lag, bit depth, and
// the uint8/uint16 input selector - pure data, POD in the port.
struct AomNoiseModelParams {
    AomNoiseShape shape;
    std::int32_t lag;
    std::int32_t bitDepth;
    std::int32_t useHighbd;
};

// 1:1 port of num_coeffs (noise_model.c:181-190): the AR-model basis is
// 2*lag+1 wide; DIAMOND keeps lag*(lag+1) coefficients, SQUARE (n*n)/2 with
// the reference's own INTEGER division (lag 1 -> 4, lag 2 -> 12).
int32_t numCoeffs(const AomNoiseModelParams params);

// AomEquationSystem (noise_model.h:30-35): the dense double system A x = b.
// The reference's malloc'd A/b/x pointers become std::vector (named
// deviation: allocation cannot fail, so equation_system_free's failure
// branches are dead and free() becomes a vector reset).
struct AomEquationSystem {
    std::vector<double> a;
    std::vector<double> b;
    std::vector<double> x;
    std::int32_t n = 0;
};

// AomNoiseStrengthSolver (noise_model.h:69-76): the piecewise-linear noise
// strength model over num_bins bins covering
// [min_intensity, max_intensity].
struct AomNoiseStrengthSolver {
    AomEquationSystem eqns;
    double minIntensity = 0;
    double maxIntensity = 0;
    std::int32_t numBins = 0;
    std::int32_t numEquations = 0;
    double total = 0;
};

// 1:1 port of svt_aom_noise_strength_solver_init (noise_model.c:305-316):
// zero-fills the solver, sets num_bins, min_intensity 0,
// max_intensity (1 << bit_depth) - 1, and inits the num_bins x num_bins
// equation system (zeroed A/b/x, n = num_bins).
int32_t noiseStrengthSolverInit(AomNoiseStrengthSolver* solver, int32_t numBins,
                                int32_t bitDepth);

// 1:1 port of svt_aom_noise_strength_solver_get_center (noise_model.c:
// 318-322): the x coordinate of bin i, i/(n-1)*range + min_intensity.
double noiseStrengthSolverGetCenter(const AomNoiseStrengthSolver* solver, int32_t i);

// 1:1 port of noise_strength_solver_get_bin_index (noise_model.c:236-240):
// fclamp (definitions.h:696-698) into [min_intensity, max_intensity], then
// the linear map (n-1)*(val - min)/range. Static in the reference; exposed
// for the tests (named deviation, the AN8 detector pattern).
double noiseStrengthSolverGetBinIndex(const AomNoiseStrengthSolver* solver, double value);

// 1:1 port of noise_strength_solver_get_value (noise_model.c:242-248):
// the linear interpolation (1-a)*x[i0] + a*x[i1] between the neighbouring
// bin values, i1 clamped by AOMMIN. Static in the reference; exposed for
// the tests (named deviation, the AN8 detector pattern).
double noiseStrengthSolverGetValue(const AomNoiseStrengthSolver* solver, double x);

// 1:1 port of svt_aom_noise_strength_solver_add_measurement (noise_model.c:
// 250-265): one observation (block_mean, noise_std) becomes the 2x2
// weighted normal-equation accumulation across the two neighbouring bins
// (weights (1-a)^2, a(1-a), a(1-a), a^2 into A; (1-a)*std, a*std into b),
// plus total += std and num_equations++.
void noiseStrengthSolverAddMeasurement(AomNoiseStrengthSolver* solver, double blockMean,
                                       double noiseStd);

// 1:1 port of equation_system_solve (noise_model.c:145-166): copies A and
// b into temporaries, runs linsolve(n, A, n, b, x) writing x, returns 0 on
// a singular system. A and b are left unchanged (non-destructive).
int32_t equationSystemSolve(AomEquationSystem* eqns);

// 1:1 port of svt_aom_noise_strength_solver_solve (noise_model.c:267-303):
// the k_alpha = 2*num_equations/n Laplacian regularization (i_lo and i_hi
// -= k_alpha, diagonal += 2*k_alpha, boundary rows folding onto the
// diagonal), then the +1/8192 diagonal and the +mean/8192 b smoothing.
// NAMED QUIRK, ported as-is: the b write is PERSISTENT on solver->eqns.b
// (the reference modifies b in place, outside the copied-A protection), so
// solve() called twice accumulates mean/8192 twice. The reference's
// A-pointer swap dance becomes a local vector copy + assign + restore
// (named deviation, same semantics).
int32_t noiseStrengthSolverSolve(AomNoiseStrengthSolver* solver);

// AomNoiseStrengthLut (noise_model.h:41-44): the piecewise-linear curve as
// (x, y) points. The reference's double(*)[2] malloc block becomes a
// vector of pairs (named deviation); the fit's memmove becomes erase.
struct AomNoiseStrengthLut {
    std::vector<std::array<double, 2>> points;
    std::int32_t numPoints = 0;
};

// 1:1 port of svt_aom_noise_strength_lut_init (noise_model.c:214-225):
// num_points zero-filled (x, y) pairs; returns 0 for a null lut.
int32_t noiseStrengthLutInit(AomNoiseStrengthLut* lut, int32_t numPoints);

// 1:1 port of svt_aom_noise_strength_lut_free (noise_model.c:227-234):
// releases the points and zeroes the count (a vector reset here).
void noiseStrengthLutFree(AomNoiseStrengthLut* lut);

// 1:1 port of svt_aom_noise_strength_solver_fit_piecewise (noise_model.c:
// 353-397): the tolerance is max_intensity*0.00625/255; the lut starts as
// all num_bins solver points and interior points are greedily removed
// smallest-residual-first while num_points > 2, stopping when
// num_points <= max_output_points AND the smallest avg residual exceeds
// the tolerance. Endpoints are never removed. The residual around a
// removed index is the ONLY thing recomputed (update_piecewise_linear_
// residual, :327-351, dx = 255./num_bins HARDCODED - not max_intensity;
// a 10-bit fixture pins the difference); stale residual entries beyond
// the recomputed window are kept and re-read, exactly as the reference
// leaves them.
int32_t noiseStrengthSolverFitPiecewise(const AomNoiseStrengthSolver* solver,
                                        int32_t maxOutputPoints, AomNoiseStrengthLut* lut);

// 1:1 ports of the equation-system memory family (noise_model.c:104-129):
// free releases the storage (a vector reset here), clear zeroes A/b/x in
// place, copy duplicates A/b/x (dst sized as src), init sizes and zeroes.
void equationSystemFree(AomEquationSystem* eqns);
void equationSystemClear(AomEquationSystem* eqns);
void equationSystemCopy(AomEquationSystem* dst, const AomEquationSystem* src);

// 1:1 ports of noise_strength_solver_clear/copy (noise_model.c:168-178):
// clear zeroes the equations and the measurement counters (geometry
// untouched); copy duplicates equations + counters.
void noiseStrengthSolverClear(AomNoiseStrengthSolver* solver);
void noiseStrengthSolverCopy(AomNoiseStrengthSolver* dest, const AomNoiseStrengthSolver* src);

// AomNoiseState (noise_model.h:166-174): the per-channel noise-model state
// - the AR equation system plus the strength solver.
struct AomNoiseState {
    AomEquationSystem eqns;
    AomNoiseStrengthSolver strengthSolver;
    std::int32_t numObservations = 0;
    double arGain = 0;
};

// 1:1 port of noise_state_init (noise_model.c:192-201): k_num_bins = 20,
// ar_gain = 1.0, num_observations = 0, and the strength solver over 20
// bins at the given bit depth. Returns 0 only if the caller passed a
// broken n (the reference's allocation-failure branch is dead here).
int32_t noiseStateInit(AomNoiseState* state, int32_t n, int32_t bitDepth);

// 1:1 port of set_chroma_coefficient_fallback_soln (noise_model.c:203-212):
// zeroes every solution entry, then solves ONLY the last diagonal for the
// luma-correlation fallback when |A[last][last]| > 1e-6.
void setChromaCoefficientFallbackSoln(AomEquationSystem* eqns);

// ---------------------------------------------------------------------------
// AN17: the flat-block finder + the noise model init/update
// (third_party/SVT-AV1/Source/Lib/Codec/noise_model.c:27-:102 helper span,
// :399-:634 flat-block finder, :636-:1157 noise model init/update).
// Composes the AN16 landed solver primitives.
// ---------------------------------------------------------------------------

// 1:1 port of get_block_mean (noise_model.c:39-62, the GET_BLOCK_MEAN macro's
// lowbd instantiation): the mean of the block_size x block_size region at
// (x_o, y_o), clamped to the picture bounds (max_h = min(h - y_o,
// block_size), max_w = min(w - x_o, block_size)); the divisor is the CLAMPED
// area max_w * max_h. The highbd instantiation (:54) is PARKED (8-bit
// discipline) and the use_highbd dispatcher branch (:58-:60) collapses with
// it - named deviation.
double getBlockMean(const std::uint8_t* data, std::int32_t w, std::int32_t h,
                    std::int32_t stride, std::int32_t xO, std::int32_t yO,
                    std::int32_t blockSize);

// 1:1 port of get_noise_var (noise_model.c:66-102, the GET_NOISE_VAR macro's
// lowbd instantiation): the population variance of the per-pixel residual
// (data - denoised) over the block_size_x x block_size_y region at (x_o,
// y_o), clamped to the picture bounds; var = E[n^2] - E[n]^2 with the
// CLAMPED area as divisor. The highbd instantiation (:92) is PARKED (8-bit
// discipline) and the use_highbd dispatcher branch (:97-:101) collapses
// with it - named deviation.
double getNoiseVar(const std::uint8_t* data, const std::uint8_t* denoised,
                   std::int32_t stride, std::int32_t w, std::int32_t h,
                   std::int32_t xO, std::int32_t yO,
                   std::int32_t blockSizeX, std::int32_t blockSizeY);

// kLowPolyNumParams (noise_util.h:19): the low-order planar model
// <[y x 1], [a1 a2 a3]> = b used by the flat-block finder.
constexpr std::int32_t kLowPolyNumParams = 3;

// AomFlatBlockFinder (noise_model.h:120-127): precomputed design matrix A
// (block_size^2 rows of [yd, xd, 1]) and its normal-equation inverse
// at_a_inv = inv(A^T A). The reference mallocs A/at_a_inv; the port holds
// std::vector members (named deviation - allocation cannot fail).
struct AomFlatBlockFinder {
    std::vector<double> atAInv;
    std::vector<double> a;
    std::int32_t numParams = 0;
    std::int32_t blockSize = 0;
    double normalization = 0;
    std::int32_t useHighbd = 0;
};

// 1:1 port of svt_aom_flat_block_finder_init (noise_model.c:399-454):
// builds A with rows [((y - bs/2.)/(bs/2.)), ((x - bs/2.)/(bs/2.)), 1],
// accumulates M = A^T A into an equation system, then obtains
// at_a_inv = inv(M) via kLowPolyNumParams solves against unit vectors.
// The reference's malloc-failure branch (:410-:416) is dead here - named
// deviation. Returns 1 on success.
int32_t flatBlockFinderInit(AomFlatBlockFinder* blockFinder, int32_t blockSize,
                            int32_t bitDepth, int32_t useHighbd);

// 1:1 port of svt_aom_flat_block_finder_free (noise_model.c:456-463):
// releases storage and zeroes the struct (a vector reset here).
void flatBlockFinderFree(AomFlatBlockFinder* blockFinder);

// 1:1 port of svt_aom_flat_block_finder_extract_block_c (noise_model.c:
// 465-:508). Extracts the block_size^2 block at (offsx, offsy) - each
// sample clamped into the picture bounds - normalized by
// 1 / (2^bit_depth - 1), fits the kLowPolyNumParams planar model through
// A / at_a_inv, writes the fitted plane into plane[] and leaves
// block[] = normalized data - plane (the residual). The use_highbd branch
// (:477-:485) is PARKED with the highbd macro family - named deviation.
// The reference keeps this function extern ("_c" suffix, called through a
// dispatch pointer); the port exposes it directly (the AN8 detector
// pattern).
void flatBlockFinderExtractBlock(const AomFlatBlockFinder* blockFinder,
                                 const std::uint8_t* data, std::int32_t w,
                                 std::int32_t h, std::int32_t stride,
                                 std::int32_t offsx, std::int32_t offsy,
                                 double* plane, double* block);

// 1:1 port of svt_aom_flat_block_finder_run (noise_model.c:520-:634).
// Per block: extract the plane residual, accumulate the gradient covariance
// (g_xx, g_xy, g_yy) and the interior mean/variance over the
// (block_size-2)^2 interior, then the features trace/det/e1/e2, norm = e1,
// ratio = e1 / max(e2, 1e-6); is_flat = trace < 0.15/1024 && ratio < 1.25
// && norm < 0.08/1024 && var > 0.005/n; the sigmoid score with weights
// {-6682, -0.2056, 13087, -12434, 2.5694} ([var, ratio, trace, norm,
// offset]) is recorded only when var > 0.005/n, else 0. flat_blocks[i] =
// is_flat ? 255 : 0, then the scores are sorted ASCENDING by compare_scores
// and every block with score >= scores[num_blocks * 90 / 100].score gets
// flat_blocks[index] |= 1 (the top-10th-percentile union; a zero-score
// field therefore marks every block - the reference's own behavior, pinned
// by a fixture). Returns num_flat. The reference's malloc-failure branch
// (:539-:545) and the NOISE_MODEL_LOG_SCORE debug blocks (:547-:549,
// :605-:607, :611-:617) are dead/absent here - named deviations; the
// mallocs become vectors.
int32_t flatBlockFinderRun(const AomFlatBlockFinder* blockFinder,
                           const std::uint8_t* data, std::int32_t w,
                           std::int32_t h, std::int32_t stride,
                           std::uint8_t* flatBlocks);

// AomNoiseStatus (noise_model.h:191-198): the noise_model_update result.
enum class AomNoiseStatus : std::int32_t {
    Ok = 0,
    InvalidArgument = 1,
    InsufficientFlatBlocks = 2,
    DifferentNoiseType = 3,
    InternalError = 4,
    InsufficientNoisePixels = 5,
};

// AomNoiseModel (noise_model.h:181-188): the per-channel combined + latest
// noise states plus the AR coordinate offsets. The reference's
// int32_t (*)[2] malloc'd coords become a vector of (x, y) pairs (named
// deviation); bit_depth is threaded verbatim.
struct AomNoiseModel {
    AomNoiseModelParams params;
    AomNoiseState combinedState[3];
    AomNoiseState latestState[3];
    std::vector<std::array<std::int32_t, 2>> coords;
    std::int32_t n = 0;
    std::int32_t bitDepth = 0;
};

// 1:1 port of svt_aom_noise_model_init (noise_model.c:636-:697). k_max_lag
// = 4 (:27); the lag gates return 0. After zeroing the model it stores the
// params, runs noiseStateInit (AN16) for all three channels with
// n + (c > 0) columns (combined AND latest), sets model->n and builds
// coords: the walk y in [-lag, 0] (max_x = (y == 0) ? -1 : lag), x in
// [-lag, max_x]; DIAMOND keeps |x| <= y + lag, SQUARE keeps everything.
// The coords malloc-failure branch (:667-:671) is dead here - named
// deviation. Returns 1 on success.
int32_t noiseModelInit(AomNoiseModel* model, const AomNoiseModelParams params);

// 1:1 port of svt_aom_noise_model_free (noise_model.c:699-:714): releases
// the coords and every per-channel equation system, then zeroes the model
// (vector resets here - allocation cannot fail).
void noiseModelFree(AomNoiseModel* model);

// 1:1 port of the EXTRACT_AR_ROW macro's lowbd instantiation
// (noise_model.c:719-:756). Extracts the neighborhood defined by coords
// around (x, y) from the difference between data and denoised into
// buffer[0..num_coords), returns val = data[y*stride + x] -
// denoised[y*stride + x], and - when BOTH alt_data and alt_denoised are
// non-null - averages (alt_data - alt_denoised) over the (1 <<
// sub_log2[0]) x (1 << sub_log2[1]) block at luma coordinates
// ((x << sub_log2[0]), (y << sub_log2[1])) into buffer[num_coords]. The
// highbd instantiation (:757) is PARKED with the other uint16 macro
// families - named deviation.
double extractArRow(const std::array<std::int32_t, 2>* coords,
                    std::int32_t numCoords,
                    const std::uint8_t* data, const std::uint8_t* denoised,
                    std::int32_t stride, const std::int32_t* subLog2,
                    const std::uint8_t* altData, const std::uint8_t* altDenoised,
                    std::int32_t altStride, std::int32_t x, std::int32_t y,
                    double* buffer);

// 1:1 port of svt_av1_add_block_observations_internal_c (noise_model.c:
// 759-:803). One observation: buffer_norm[i] = buffer[i] * recp_sqr_norm,
// b[i] += buffer_norm[i] * val, and A[i][j] += buffer_norm[i] * buffer[j]
// over the n x n normal equations. The reference unrolls both loops by 8
// before the tails; the port keeps the same accumulation order (the
// unroll contributes no reassociation of the adds - each element is an
// independent +=).
void addBlockObservationsInternal(std::uint32_t n, const double val,
                                  const double recpSqrNorm, const double* buffer,
                                  double* bufferNorm, double* b, double* a);

// 1:1 port of add_block_observations (noise_model.c:805-:890, static
// there; exposed for the tests - the AN8 detector pattern). For every flat
// block: y_start/x_start drop to 0 when the left/upper neighbour block is
// also flat (else lag), y_end = min((h >> sub[1]) - by*(block >> sub[1]),
// block >> sub[1]) and x_end = min((w >> sub[0]) - bx*(block >> sub[0]) -
// lag, flat-right ? (block >> sub[0]) : (block >> sub[0]) - lag) - the
// :845-:849 flat-right-neighbour branch - and each (x, y) in the window
// contributes extractArRow + addBlockObservationsInternal against the
// LATEST state of channel c. num_observations accumulates
// max(y_end - y_start, 0) * max(x_end - x_start, 0). The
// EB_MALLOC_ALIGNED scratch buffers become vectors (named deviation; the
// failure branches :823-:826/:830-:834 are dead). The
// params.use_highbd ternary (:852-:876) is PARKED with the highbd macro
// family - the port always takes the lowbd path. Returns 1.
int32_t addBlockObservations(AomNoiseModel* noiseModel, int32_t c,
                             const std::uint8_t* data, const std::uint8_t* denoised,
                             int32_t w, int32_t h, int32_t stride,
                             const std::int32_t* subLog2,
                             const std::uint8_t* altData, const std::uint8_t* altDenoised,
                             int32_t altStride, const std::uint8_t* flatBlocks,
                             int32_t blockSize, int32_t numBlocksW, int32_t numBlocksH);

// 1:1 port of add_noise_std_observations (noise_model.c:892-:956, static
// there; exposed for the tests - the AN8 detector pattern). For every flat
// block with more than block_size samples (num_samples_w * num_samples_h >
// block_size): block_mean = getBlockMean over the alt (luma) plane when
// present, else the data plane, at (x_o << sub[0], y_o << sub[1]) with the
// FULL block_size; noise_var = getNoiseVar over the (block >> sub) region
// of the (w >> sub, h >> sub) plane; luma_strength = (c > 0) ?
// latest_state[0].ar_gain * noiseStrengthSolverGetValue(luma_solver,
// block_mean) : 0; corr = (c > 0) ? coeffs[num_coords] : 0;
// uncorr_std = sqrt(max(noise_var / 16, noise_var - (corr * luma_strength)^2));
// adjusted = uncorr_std / latest_state[c].ar_gain; one
// noiseStrengthSolverAddMeasurement(solver, block_mean, adjusted). The
// use_highbd arguments to the two helpers are PARKED with the highbd macro
// family (the lowbd paths are taken) - named deviation.
void addNoiseStdObservations(AomNoiseModel* noiseModel, int32_t c, const double* coeffs,
                             const std::uint8_t* data, const std::uint8_t* denoised,
                             int32_t w, int32_t h, int32_t stride, const std::int32_t* subLog2,
                             const std::uint8_t* altData, int32_t altStride,
                             const std::uint8_t* flatBlocks, int32_t blockSize,
                             int32_t numBlocksW, int32_t numBlocksH);

// 1:1 port of ar_equation_system_solve (noise_model.c:958-:996, static
// there; exposed for the tests - the AN8 detector pattern). Solves the
// state's equation system (equationSystemSolve); on failure ar_gain is
// reset to 1 and 0 is returned. On success: var = mean over the first
// (n - is_chroma) diagonals of A[i][i] / num_observations; sum_covar
// sums (bi * x[i]) / num_observations where the chroma case removes the
// correlated part, bi -= A[i][n-1] * x[n-1]; noise_var = max(var -
// sum_covar, 1e-6); ar_gain = max(1, sqrt(max(var / noise_var, 1e-6))).
int32_t arEquationSystemSolve(AomNoiseState* state, int32_t isChroma);

// 1:1 port of noise_model_update (noise_model.c:1019-:1145, static there;
// exposed for the tests - the AN8 detector pattern). Gates: block_size
// <= 1 or block_size < lag*2 + 1 -> InvalidArgument. Clears the three
// latest equation systems / observation counts / strength solvers, then
// requires MORE than one flat block (else InsufficientFlatBlocks). Per
// channel 0..2 (breaking out when a plane pointer is null): sub = the
// chroma_sub_log2 for c > 0 else {0, 0}, alt = the luma plane for c > 0;
// addBlockObservations -> arEquationSystemSolve (chroma failure falls back
// via setChromaCoefficientFallbackSoln, luma failure returns
// InsufficientNoisePixels) -> addNoiseStdObservations with coeffs =
// latest eqns.x -> noiseStrengthSolverSolve (failure: InternalError);
// then the combined state is OVERWRITTEN from the latest (num_observations,
// equation_system_copy, ar solve with the same fallback split,
// noise_strength_solver_copy, strength solve). Returns Ok.
AomNoiseStatus noiseModelUpdate(AomNoiseModel* noiseModel,
                                const std::uint8_t* const data[3],
                                const std::uint8_t* const denoised[3],
                                std::int32_t w, std::int32_t h,
                                const std::int32_t* stride,
                                const std::int32_t* chromaSubLog2,
                                const std::uint8_t* flatBlocks,
                                std::int32_t blockSize);

// 1:1 port of svt_aom_noise_model_save_latest (noise_model.c:1147-:1157):
// per channel, copies the latest equation systems into the combined state
// (AR eqns and strength-solver eqns), plus num_equations,
// num_observations and ar_gain.
void noiseModelSaveLatest(AomNoiseModel* noiseModel);


// ============================================================================
// AN14: noise_util (third_party/SVT-AV1/Source/Lib/Codec/noise_util.c:20-121
// + the OD_DIVU table :113-:370, noise_util.h:24-30 the struct).
// THE CONSUMER (grep-verified 2026-11-04): the noise_tx family feeds ONLY
// wiener_denoise_2d in noise_model.c (the future AN17b) in the 8-bit port
// path; the OD_DIVU table feeds nothing else in Codec (divide-unroll macro
// users only).
// THE PORT SHAPE (named): fft/ifft are PLAIN function pointers to the landed
// analysis::fft2dN/ifft2dN (no std::function - the reference stores C
// function pointers, and ours resolve to real functions, so the plain
// pointer type is exact). The reference's signature is
// void(*)(const float* input, float* temp, float* output) - identical to
// fft2dN's (input, temp, output) per analysis.h:508-518. Justified: an enum +
// call-site switch would diverge from noise_util.c:30-50's pointer-store
// structure for no gain.
// MEMALIGN (named): svt_aom_memalign(32, ...) -> std::aligned_alloc(32, ...)
// with the size rounded UP to the 32 multiple aligned_alloc requires
// (C++17); svt_aom_free -> std::free. The 32-byte alignment is kept: the
// reference requires it "for compatibility with existing SIMD
// implementations" (noise_util.h:40, :51), so even though the landed scalar
// fft2d kernels read floats pointwise and do NOT require it, the struct
// contract preserves it - a future SIMD twin inherits the guarantee.
// Both buffers are memset to zero up front (noise_util.c:63-66): the forward
// transform's real-only outputs (the last-column/row imaginary slots) are
// never written, so zero-init keeps the tx_block deterministic.
// ============================================================================
struct NoiseTx {
    float*  tx_block;
    float*  temp;
    std::int32_t block_size;
    void (*fft)(const float*, float*, float*);
    void (*ifft)(const float*, float*, float*);
};

// noise_util.c:20-22: (factor*factor / 10000) * block_size * block_size / 8.
// ALL FLOAT except the int32 block_size which promotes: block_size*block_size
// is int math (<= 1024, no overflow), then promotes to float for the divide.
// Hand-checked: bs=8 f=2.5 -> 0.005f exactly representable? (6.25/10000)*64/8
// = 6.25*8/10000 = 0.005 - the reference computes float((f*f)/10000)*64/8 in
// that order; the tests pin the exact float bits against a same-order
// float expression.
float noisePsdGetDefaultValue(std::int32_t blockSize, float factor);

// noise_util.c:24-71. malloc analog: returns nullptr on unsupported sizes
// (2,4,8,16,32 supported) and on allocation failure; the SVT_ERROR print is
// DROPPED (no stderr in the port) - the nullptr return is the contract.
// Buffers: 2*bs*bs floats each, 32-byte aligned, zero-initialized.
NoiseTx* noiseTxMalloc(std::int32_t blockSize);
void     noiseTxFree(NoiseTx* noiseTx);

// noise_util.c:70-72: one fft call (data -> temp workspace, tx_block output).
void noiseTxForward(NoiseTx* noiseTx, const float* data);

// noise_util.c:74-94: k_beta = 1.1f; k_beta_m1_div_k_beta computed per call
// as (k_beta - 1.0f) / k_beta (the reference's float rounding of that
// division is preserved, NOT re-derived in double); per-pair power
// p = re^2 + im^2; threshold branch p > psd_mul_k_beta && p > 1e-6 (the
// literal 1e-6 is DOUBLE - p promotes - the port keeps the double comparison
// exactly); val = (p - psd)/AOMMAX(p, k_eps) with k_eps = 1e-6f, scaling
// BOTH components; else scale both by k_beta_m1_div_k_beta. Mutates
// block_ptr IN PLACE, 2 floats per pair, bs*bs pairs.
void noiseTxFilterC(std::int32_t blockSize, float* blockPtr, const float psd);

// noise_util.c:96-102: n = block_size*block_size; ifft then data[i] /= n -
// THE NORMALIZATION LIVES HERE (the AN13 finding: with n = side length,
// ifft2d(fft2d(f)) == n*n * f because each of the two 1D passes carries an
// unnormalized gain n; noise_util's divisor is block_size*block_size, which
// IS that n*n. So the composed forward->inverse roundtrip is IDENTITY:
// n*n*f / n*n = f). The test pins data[i] = input[i] within the landed
// tolerance (AN12/13: EPSILON 1e-4 absolute on the roundtrip).
void noiseTxInverse(NoiseTx* noiseTx, float* data);

// OD_DIVU (noise_util.c:113-370): OD_DIVU_DMAX = 1024, uint32_t [1024][2].
// TRANSCRIBED MECHANICALLY (read the reference lines, emit our table) - the
// paired-metric caveat applies: this block moves the coverage number by
// transcription, not by algorithm. The diff check (test_noise_util.cpp:
// every one of the 2048 constants matches the reference EXACTLY) IS the
// test for this span. Sentinel rows: {0xFFFFFFFF, 0xFFFFFFFF} at powers of
// two reproduce x/d exactly through the magic-multiplier formula:
// d=1: M = 2^32-1 = 0xFFFFFFFF, h1 = (M+1)/2^32 = 1, so
//   (M*x) hi32 = (2^32-1)*x >> 32 = x - 1 (for x >= 1) then the h1 add and
//   the reference's lowbit adjustment give x. d=8: M = 0xE38E38E4, h1 = 8.
extern std::uint32_t odDivuSmallConsts[1024][2];
}  // namespace analysis
