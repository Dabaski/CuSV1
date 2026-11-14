// A5H - the frame-header / tile-info writer surface of entropy_coding.c.
// Reference: third_party/SVT-AV1/Source/Lib/Codec/entropy_coding.c
// :2189-:3960 (the writer family between the landed intra/inter/chroma
// symbol surfaces and the OBU shells that live in l8_bitstream).
//
// Carriers (the RE8A DeblockContext precedent, AGENTS.md): the
// PictureParentControlSet / SequenceControlSet / Av1Common walls each span
// consumes are mirrored into HdrPcs / HdrSeqHeader / HdrAv1Common; the
// sub-structs (TilesInfo, FrameSize, QuantizationParams, LoopFilter,
// CdefParams, DeltaQParams, DeltaLfParams, RestorationInfo, WarpedMotionParams,
// AomFilmGrain) are mirrored field-for-field with citations.
//
// Pinned build config (EbConfigMacros.h:167-226, the non-RTC branch):
// CONFIG_ENABLE_FILM_GRAIN / CONFIG_ENABLE_HIGH_BIT_DEPTH / CONFIG_ENABLE_
// SUPERRES / CONFIG_ENABLE_GLOBAL_MOTION / CONFIG_ENABLE_RESTORATION are all
// 1, so those const-fold guards are LIVE behavior and the port keeps their
// bodies. SVT_EFFECTIVE_BIT_DEPTH is the identity (EbConfigMacros.h:293).

#pragma once

#include "entropy.h"  // AomWriter/AomWriteBitBuffer/wbWriteBit/wbWriteLiteral/...
#include "inter_symbols.h"  // TransformationType (definitions.h:1740-:1747)

#include <cstdint>

namespace entropy {

// ---------------------------------------------------------------------------
// Constants (verbatim, each cited).
// ---------------------------------------------------------------------------
#define HD_QINDEX_BITS 8                     // definitions.h:1644
#define HD_QM_LEVEL_BITS 4                   // definitions.h:1649
#define HD_CDEF_STRENGTH_BITS 6              // cdef.h:21
#define HD_PROFILE_BITS 3                    // definitions.h:432
#define HD_PRIMARY_REF_BITS 3                // definitions.h:1450
#define HD_REF_FRAMES_LOG2 3                 // definitions.h:1388
#define HD_SCALE_NUMERATOR 8                 // definitions.h:1430
#define HD_SUPERRES_SCALE_BITS 3             // definitions.h:1431
#define HD_SUPERRES_SCALE_DENOMINATOR_MIN (HD_SCALE_NUMERATOR + 1)  // :1432
#define HD_SUBEXPFIN_K 3                     // definitions.h:1721
#define HD_GM_TRANS_PREC_BITS 6              // definitions.h:1722
#define HD_GM_ABS_TRANS_BITS 12              // definitions.h:1723
#define HD_GM_ABS_TRANS_ONLY_BITS (HD_GM_ABS_TRANS_BITS - HD_GM_TRANS_PREC_BITS + 3)  // :1724
#define HD_GM_TRANS_PREC_DIFF (16 - HD_GM_TRANS_PREC_BITS)           // :1725 (WARPEDMODEL_PREC_BITS 16, definitions.h:1692)
#define HD_GM_TRANS_ONLY_PREC_DIFF (16 - 3)  // definitions.h:1726
#define HD_GM_ALPHA_PREC_BITS 15             // definitions.h:1729
#define HD_GM_ABS_ALPHA_BITS 12              // definitions.h:1730
#define HD_GM_ALPHA_PREC_DIFF (16 - HD_GM_ALPHA_PREC_BITS)           // :1731
#define HD_GM_ALPHA_MAX (1 << HD_GM_ABS_ALPHA_BITS)                  // :1735
#define HD_MAX_TILE_WIDTH (4096)             // block_structures.h:24
#define HD_MAX_TILE_AREA (4096 * 2304)       // block_structures.h:25
#define HD_MAX_TILE_COLS 64                  // definitions.h:373
#define HD_MAX_TILE_ROWS 64                  // definitions.h:372
#define HD_MI_SIZE_LOG2 2                    // definitions.h:353
#define HD_FRAME_LF_COUNT 4                  // definitions.h:388
#define HD_MAX_SEGMENTS_ 8                   // definitions.h:1674
#define HD_MAX_TILE_CNTS 128                 // definitions.h:72

// FrameType (definitions.h:1585-:1591, verbatim values).
enum HdFrameType {
    HD_KEY_FRAME = 0,
    HD_INTER_FRAME = 1,
    HD_INTRA_ONLY_FRAME = 2,
    HD_S_FRAME = 3,
};
// PRIMARY_REF_NONE (definitions.h:1451) - already in entropy.h.
#define HD_PRIMARY_REF_NONE PRIMARY_REF_NONE

// EbAv1SeqProfile (Source/API/EbSvtAv1.h:144, verbatim values).
enum HdSeqProfile { HD_MAIN_PROFILE = 0, HD_HIGH_PROFILE = 1, HD_PROFESSIONAL_PROFILE = 2 };
// EbBitDepth (Source/API/EbSvtAv1Formats.h:101-:107).
enum HdBitDepth { HD_EB_EIGHT_BIT = 8, HD_EB_TEN_BIT = 10, HD_EB_TWELVE_BIT = 12 };
// InterpFilter tail (definitions.h:818-:827): the frame-header writer needs
// SWITCHABLE (= BILINEAR + 1); inter_symbols.h carries the enum, re-exported
// below for the local spelling.
#define HD_INTERP_FILTER_SWITCHABLE ((int)entropy::SWITCHABLE)

// FrameContextIndex tail (definitions.h:1616-:1623): only REGULAR_FRAME is
// consumed (write_uncompressed_header_obu :3476).
#define HD_REGULAR_FRAME 0
// TxMode (definitions.h:1008-:1013, verbatim values).
enum HdTxMode { HD_ONLY_4X4 = 0, HD_TX_MODE_LARGEST = 1, HD_TX_MODE_SELECT = 2 };
// ReferenceMode (definitions.h:1472-:1477 via inter_symbols.h; the SELECT
// sentinel is 2).
#define HD_REFERENCE_MODE_SELECT 2
// MAX_TILE_CNTS prev_qindex bound (definitions.h:72); FRAME_LF_COUNT above.

// ---------------------------------------------------------------------------
// Carriers.
// ---------------------------------------------------------------------------
// HdrSeqHeader - the SequenceControlSet surface (pcs.h + av1_structs.h:121-:
// 267 + EbSvtAv1Enc.h static_config): the fields the sequence-header writer
// family consumes. Grown by the slices that need each field.
struct HdrSeqHeader {
    // static_config (EbSvtAv1Enc.h:312/:331/:353/:357/:361/:367/:383/:552-:555)
    int profile;                       // EbAv1SeqProfile
    int encoder_bit_depth;             // EbBitDepth value (8/10/12)
    int color_primaries;               // EbColorPrimaries
    int transfer_characteristics;      // EbTransferCharacteristics
    int matrix_coefficients;           // EbMatrixCoefficients
    int color_range;                   // EbColorRange
    int chroma_sample_position;        // EbChromaSamplePosition
    int chroma_u_dc_qindex_offset;
    int chroma_u_ac_qindex_offset;
    int chroma_v_dc_qindex_offset;
    int chroma_v_ac_qindex_offset;
    int subsampling_x;   // pcs.h (the SequenceControlSet subsampling facts)
    int subsampling_y;
    // seq_header (av1_structs.h:121-:267) - the fields the writer family
    // consumes; grown per slice.
    int enable_superres;   // av1_structs.h:251 (uint8)
    int sb_size_log2;      // av1_structs.h:199 (uint8)
    int frame_width_bits;  // av1_structs.h:156 (uint8; written by
                           // write_sequence_header :2772-:2776)
    int frame_height_bits; // av1_structs.h:160 (uint8)
    // C4: the sequence-header writer family.
    int still_picture;                    // av1_structs.h:128
    int reduced_still_picture_header;     // :132
    int max_frame_width;                  // :167 (uint16)
    int max_frame_height;                 // :174 (uint16)
    int frame_id_numbers_present_flag;    // :178
    int delta_frame_id_length;            // :182
    int frame_id_length;                  // :186
    int sb_size_is_128;                   // :190/:193 (sb_size == BLOCK_128X128)
    int filter_intra_level;               // :204
    int enable_intra_edge_filter;         // :207
    int enable_interintra_compound;       // :215
    int enable_masked_compound;           // :220
    int enable_warped_motion;             // :225
    int enable_dual_filter;               // :231
    int enable_order_hint;                // OrderHintInfo (av1_structs.h:79-:97)
    int enable_jnt_comp;
    int enable_ref_frame_mvs;
    int order_hint_bits;
    int seq_force_screen_content_tools;   // :240 (2 = the SELECT sentinel)
    int seq_force_integer_mv;             // :246 (2 = the SELECT sentinel)
    int cdef_level;                       // :255
    int enable_restoration;               // :259
    int film_grain_params_present;        // :265
    int decoder_model_info_present_flag;  // :139 (the branch is the reference's
                                          // SVT_ERROR shell, :3737-:3743)
    int initial_display_delay_present_flag;  // :146
    // numberSpatialLayers operating points (write_sequence_header_obu's
    // :3728-:3751 loop; MAX_NUM_OPERATING_POINTS is 1 for our surface - the
    // count is threaded).
    int op_idc;                              // EbAv1OperatingPoint.op_idc
    int seq_tier;                            // :seq_tier (written when major > 3)
    int initial_display_delay;               // :initial_display_delay
    int initial_display_delay_present_for_this_op;
    int level_major;                         // BitstreamLevel (entropy_coding.h:81-:84)
    int level_minor;
};

// ---------------------------------------------------------------------------
// Ported functions (entropy_coding.c, in span order).
// ---------------------------------------------------------------------------

// svt_aom_wb_is_byte_aligned (:1343-:1345)
int wbIsByteAligned(const AomWriteBitBuffer* wb);
// svt_aom_wb_bytes_written (:1347-:1349)
std::uint32_t wbBytesWritten(const AomWriteBitBuffer* wb);

// recenter_nonneg (:2842-:2850)
std::uint16_t recenterNonneg(std::uint16_t r, std::uint16_t v);
// recenter_finite_nonneg (:2854-:2860)
std::uint16_t recenterFiniteNonneg(std::uint16_t n, std::uint16_t r, std::uint16_t v);

// svt_aom_write_primitive_quniform (:2863-:2875) - the AomWriter (entropy
// coder) variant.
void writePrimitiveQuniform(AomWriter* w, std::uint16_t n, std::uint16_t v);
// aom_wb_write_primitive_quniform (:2877-:2889) - the AomWriteBitBuffer
// variant.
void wbWritePrimitiveQuniform(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t v);
// svt_aom_count_primitive_quniform (:2891-:2898)
int countPrimitiveQuniform(std::uint16_t n, std::uint16_t v);

// svt_aom_write_primitive_subexpfin (:2901-:2922)
void writePrimitiveSubexpfin(AomWriter* w, std::uint16_t n, std::uint16_t k, std::uint16_t v);
// aom_wb_write_primitive_subexpfin (:2924-:2945)
void wbWritePrimitiveSubexpfin(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t k,
                               std::uint16_t v);
// svt_aom_count_primitive_subexpfin (:2947-:2970)
int countPrimitiveSubexpfin(std::uint16_t n, std::uint16_t k, std::uint16_t v);

// svt_aom_write_primitive_refsubexpfin (:2975-:2977)
void writePrimitiveRefsubexpfin(AomWriter* w, std::uint16_t n, std::uint16_t k, std::uint16_t ref,
                                std::uint16_t v);
// aom_wb_write_primitive_refsubexpfin (:2979-:2982)
void wbWritePrimitiveRefsubexpfin(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t k,
                                  std::uint16_t ref, std::uint16_t v);
// svt_aom_wb_write_signed_primitive_refsubexpfin (:2984-:2990)
void wbWriteSignedPrimitiveRefsubexpfin(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t k,
                                        std::int16_t ref, std::int16_t v);
// svt_aom_count_primitive_refsubexpfin (:2992-:2994)
int countPrimitiveRefsubexpfin(std::uint16_t n, std::uint16_t k, std::uint16_t ref, std::uint16_t v);

// write_delta_q (:2365-:2372)
void writeDeltaQ(AomWriteBitBuffer* wb, std::int32_t delta_q);

// write_profile (:2671-:2674): literal(profile, PROFILE_BITS). The reference
// takes BitstreamProfile; the assert bounds it to [0, 3).
void writeProfile(std::int32_t profile, AomWriteBitBuffer* wb);

// write_bitdepth (:2676-:2685). SVT_EFFECTIVE_BIT_DEPTH is the identity for
// the pinned HBD=1 build.
void writeBitdepth(const HdrSeqHeader* sh, AomWriteBitBuffer* wb);


// ---------------------------------------------------------------------------
// Frame-header carriers (C2). Sub-struct mirrors, field-for-field.
// ---------------------------------------------------------------------------
// Plane (definitions.h:683).
enum HdPlane { HD_PLANE_Y = 0, HD_PLANE_U = 1, HD_PLANE_V = 2, HD_MAX_PLANES = 3 };
// RestorationType (definitions.h:1421-:1428).
enum HdRestorationType {
    HD_RESTORE_NONE = 0, HD_RESTORE_WIENER = 1, HD_RESTORE_SGRPROJ = 2,
    HD_RESTORE_SWITCHABLE = 3,
};
// BlockSize sb_size tail (definitions.h:883-:905): only the 128 flag is
// consumed (entropy_coding.c:2231).
#define HD_SB_128X128 1

// RestorationInfo consumed surface (restoration.h:224-:241).
struct HdrRestorationInfo {
    int frame_restoration_type;  // HdRestorationType
    int restoration_unit_size;
};

// QuantizationParams (av1_structs.h:339-:353) - the writer-consumed fields
// (qindex[MAX_SEGMENTS] is not consumed by any span in this surface).
struct HdrQuantizationParams {
    int base_q_idx;     // uint8 in the reference
    int delta_q_dc[3];  // int8 in the reference, [HD_MAX_PLANES]
    int delta_q_ac[3];  // int8 in the reference, [HD_MAX_PLANES]
    int using_qmatrix;
    int qm[3];          // uint8 in the reference, [HD_MAX_PLANES]
};

// DeltaQParams (av1_structs.h:355-:362).
struct HdrDeltaQParams {
    int delta_q_present;
    int delta_q_res;
};

// DeltaLfParams (av1_structs.h:364-:376).
struct HdrDeltaLfParams {
    int delta_lf_present;
    int delta_lf_res;
    int delta_lf_multi;
};

// CdefParams (av1_structs.h:378-:388); CDEF_MAX_STRENGTHS 16
// (definitions.h:1440).
#define HD_CDEF_MAX_STRENGTHS 16
struct HdrCdefParams {
    int cdef_damping;
    int cdef_bits;
    int cdef_y_strength[HD_CDEF_MAX_STRENGTHS];
    int cdef_uv_strength[HD_CDEF_MAX_STRENGTHS];
};

// LoopFilter (definitions.h:1655-:1672) - the writer-consumed fields (the
// ref_deltas/mode_deltas pairs live only in the reference's commented-out
// mode_ref_delta_update block, entropy_coding.c:2307-:2340).
struct HdrLoopFilter {
    int filter_level[2];
    int filter_level_u;
    int filter_level_v;
    int sharpness_level;
    int mode_ref_delta_enabled;
};

// FrameSize (av1_structs.h:269-:285).
struct HdrFrameSize {
    int frame_width;
    int frame_height;
    int render_width;
    int render_height;
    int superres_denominator;
    int superres_upscaled_width;
    int superres_upscaled_height;
};

// HdrSegmentationParams - the SegmentationParams writer-consumed surface
// (av1_structs.h SegmentationParams; the feature grid is the landed
// writeSegFeatures carrier shape, entropy.h:332).
struct HdrSegmentationParams {
    int segmentation_enabled;
    int segmentation_update_map;
    int segmentation_temporal_update;
    int segmentation_update_data;
    std::int16_t feature_enabled[8][8];   // MAX_SEGMENTS x SEG_LVL_MAX
    std::int16_t feature_data[8][8];
};

// FrameHeader (av1_structs.h:427-:601) - the fields the C2 spans consume;
// grown by later slices.
struct HdrSkipModeInfo {  // SkipModeInfo (av1_structs.h:402-:416)
    int skip_mode_allowed;
    int skip_mode_flag;
};

struct HdrFrameHeader {
    int allow_intrabc;
    int coded_lossless;
    int all_lossless;
    HdrQuantizationParams quantization_params;
    HdrDeltaQParams delta_q_params;
    HdrDeltaLfParams delta_lf_params;
    HdrLoopFilter loop_filter_params;
    HdrCdefParams cdef_params;
    HdrSegmentationParams segmentation_params;
    // C4: the uncompressed-header writer family.
    int show_existing_frame;           // av1_structs.h:430
    int frame_type;                    // :433 (FrameType; HD_KEY_FRAME et al)
    int show_frame;                    // :437
    int showable_frame;                // :448
    int error_resilient_mode;          // :452
    int disable_cdf_update;            // :456
    int allow_screen_content_tools;    // :460
    int force_integer_mv;              // :464
    std::int32_t current_frame_id;     // :467
    int primary_ref_frame;             // :474
    int allow_high_precision_mv;       // :513
    int interpolation_filter;          // :516 (InterpFilter; SWITCHABLE tail)
    int is_motion_mode_switchable;     // :519
    int use_ref_frame_mvs;             // :524
    int tx_mode;                       // :578 (TxMode; 0 = ONLY_4X4,
                                       // 1 = TX_MODE_SELECT)
    int reference_mode;                // :581 (ReferenceMode)
    HdrSkipModeInfo skip_mode_params;  // :584
    int allow_warped_motion;           // :588
    int reduced_tx_set;                // :592
    int frame_refs_short_signaling;    // :600
};

// HdrWarpParams - the WarpedMotionParams consumed surface (definitions.h:
// 1755-:1760): wmtype + the six wmmat entries; the alpha/beta/gamma/delta and
// invalid fields are not consumed by any span in this surface.
struct HdrWarpParams {
    TransformationType wmtype;  // definitions.h:1740-:1746 (inter_symbols.h)
    std::int32_t wmmat[6];      // :1757 (MAX_PARAMDIM = 6)
};

// HdrFilmGrainParams - the AomFilmGrain writer-consumed surface (Source/API/
// EbSvtAv1.h:355-:429, field order verbatim).
struct HdrFilmGrainParams {
    int apply_grain;                  // :357
    int update_parameters;            // :360 (the writer's runtime OUT)
    int scaling_points_y[14][2];      // :363
    int num_y_points;                 // :364 (0..14)
    int scaling_points_cb[10][2];     // :367
    int num_cb_points;                // :368 (0..10)
    int scaling_points_cr[10][2];     // :371
    int num_cr_points;                // :372 (0..10)
    int scaling_shift;                // :375 (8..11)
    int ar_coeff_lag;                 // :378 (0..3)
    int ar_coeffs_y[24];              // :381
    int ar_coeffs_cb[25];             // :382
    int ar_coeffs_cr[25];             // :383
    int ar_coeff_shift;               // :390 (6..9)
    int cb_mult;                      // :394 (8 bits)
    int cb_luma_mult;                 // :397 (8 bits)
    int cb_offset;                    // :398 (9 bits)
    int cr_mult;                      // :403 (8 bits)
    int cr_luma_mult;                 // :406 (8 bits)
    int cr_offset;                    // :407 (9 bits)
    int overlap_flag;                 // :411
    int clip_to_restricted_range;     // :414
    int chroma_scaling_from_luma;     // :419
    int grain_scale_shift;            // :422
    int random_seed;                  // :425 (uint16 in the reference)
    int ignore_ref;                   // :428
};

// HdrAv1Common (av1_common.h:58-:89) - the fields the tile + frame-size
// spans consume; C2 consumes only frm_size.

// TilesInfo (av1_structs.h:287-:337, verbatim layout).
struct HdrTilesInfo {
    std::uint16_t max_tile_width_sb;   // :289
    std::uint16_t max_tile_height_sb;  // :292
    std::uint8_t min_log2_tile_cols;   // :295
    std::uint8_t max_log2_tile_cols;   // :298
    std::uint8_t max_log2_tile_rows;   // :301
    std::uint8_t min_log2_tile_rows;   // :304
    std::uint8_t min_log2_tiles;       // :307
    std::uint8_t uniform_tile_spacing_flag;  // :311
    std::uint8_t tile_cols;            // :314
    std::uint8_t tile_rows;            // :317
    std::uint8_t tile_cols_log2;       // :320
    std::uint8_t tile_rows_log2;       // :323
    std::uint16_t tile_col_start_mi[HD_MAX_TILE_ROWS + 1];  // :327
    std::uint16_t tile_row_start_mi[HD_MAX_TILE_COLS + 1];  // :330
    std::uint16_t context_update_tile_id;                   // :333
    std::uint8_t tile_size_bytes;                           // :336
};

struct HdrAv1Common {
    HdrFrameSize frm_size;
    std::int32_t mi_rows;              // av1_common.h:59
    std::int32_t mi_cols;              // av1_common.h:60
    std::int32_t log2_tile_cols;       // av1_common.h:77 (only valid uniform)
    std::int32_t log2_tile_rows;       // av1_common.h:78 (only valid uniform)
    std::int32_t tile_width;           // av1_common.h:79 (in MI units)
    std::int32_t tile_height;          // av1_common.h:79 (in MI units)
    HdrTilesInfo tiles_info;           // av1_common.h:87
};

// HdrPcs - the PictureParentControlSet surface (pcs.h): the fields the C2
// spans consume.
struct HdrPcs {
    HdrFrameHeader frm_hdr;
    HdrSeqHeader* scs;
    HdrAv1Common* av1_cm;
    // pcs.h:1045 (bool) + :1042-:1043 (uint16).
    int frame_resize_enabled;
    int render_width;
    int render_height;
    // pcs.h:931 (int32).
    int nb_cdef_strengths;
    // seq_header.sb_size == BLOCK_128X128 (entropy_coding.c:2231).
    int sb_size_is_128;
    // child_pcs->rst_info[3] (rest_process.c; the writer reads
    // child_pcs->rst_info[p]).
    HdrRestorationInfo rst_info[3];
    // C3: the tile machinery. pcs.h:775-:777 / :1214-:1217 (uint8).
    std::uint8_t log2_sb_size;
    std::uint8_t log2_tile_cols;
    std::uint8_t log2_tile_rows;
    // pcs.h:192 (uint8) - the OUT of write_tile_info (:2584, :2602-:2612).
    std::uint8_t tile_size_bytes_minus_1;
    // pcs.h child_pcs->ec_info[tile_idx]->ec->ec_writer.pos facts
    // (entropy_coding.c:2600): the per-tile arithmetic-coder writer
    // positions; the last tile is not read (tile_cnt - 1 walk bound).
    const std::uint32_t* ec_writer_pos;
    std::uint32_t ec_writer_pos_count;
    // C4: the uncompressed-header writer family.
    std::uint64_t frame_offset;           // pcs.h:929
    std::uint32_t dpb_order_hint[8];      // pcs.h:1148 (spec 6.8.2)
    std::uint8_t refresh_frame_mask;      // Av1RpsNode (pred_structure.h:66)
    std::uint8_t ref_dpb_index[7];        // pred_structure.h:67 [ref - LAST]
    int allow_comp_inter_inter;           // pcs.h:962
    std::uint32_t large_scale_tile;       // pcs.h:930
    int refresh_frame_context;            // pcs.h:926 (RefreshFrameContextMode)
    std::uint32_t frame_context_idx;      // pcs.h:927
    std::int32_t fb_of_context_type[8];   // pcs.h:928 (the OUT mutations)
    int monochrome;                       // pcs.h:933
    HdrWarpParams global_motion[8];       // pcs->global_motion[LAST..ALTREF]
    HdrWarpParams ref_global_motion[8];   // child_pcs->ref_global_motion
    // pcs.h:934/:942/:957 - the write_uncompressed_header_obu side-effect
    // targets (:3570, :3579, :3583-:3584).
    std::int32_t prev_qindex[HD_MAX_TILE_CNTS];   // MAX_TILE_CNTS 128
    std::int32_t prev_delta_lf_from_base;
    std::int32_t prev_delta_lf[HD_FRAME_LF_COUNT];
    // The film grain wall: the AomFilmGrain params + the EbReferenceObject
    // facts (entropy_coding.c:3112-:3126: the ref list 0/1 film grain params
    // + the two map idxes + the B-slice fact) threaded for
    // write_uncompressed_header_obu's CONFIG_ENABLE_FILM_GRAIN gate.
    HdrFilmGrainParams fg;
    HdrFilmGrainParams fg_ref_0;
    HdrFilmGrainParams fg_ref_1;
    int fg_slice_is_b;
    int fg_ref_idx_0;
    int fg_ref_idx_1;
};

// encode_restoration_mode (:2189-:2251)
void encodeRestorationMode(HdrPcs* pcs, AomWriteBitBuffer* wb);
// encode_loopfilter (:2284-:2342)
void encodeLoopfilter(HdrPcs* pcs, AomWriteBitBuffer* wb);
// encode_cdef (:2344-:2363)
void encodeCdef(const HdrPcs* pcs, AomWriteBitBuffer* wb);
// encode_quantization (:2374-:2401)
void encodeQuantization(const HdrPcs* pcs, AomWriteBitBuffer* wb);

// write_render_size (:2616-:2629)
void writeRenderSize(AomWriteBitBuffer* wb, HdrPcs* pcs);
// write_superres_scale (:2631-:2650)
void writeSuperresScale(AomWriteBitBuffer* wb, HdrPcs* pcs);
// write_frame_size (:2652-:2669)
void writeFrameSize(HdrPcs* pcs, std::int32_t frame_size_override, AomWriteBitBuffer* wb);

// svt_av1_get_tile_limits (:2450-:2467)
void getTileLimits(HdrPcs* pcs);
// svt_av1_calculate_tile_cols (:2469-:2509)
void calculateTileCols(HdrPcs* pcs);
// svt_av1_calculate_tile_rows (:2511-:2535)
void calculateTileRows(HdrPcs* pcs);
// svt_aom_set_tile_info (:2537-:2579)
void setTileInfo(HdrPcs* pcs);
// write_tile_info_max_tile (:2403-:2448). MUTATES tiles_info.
// min_log2_tile_rows (:2418) - 1:1 with the reference.
void writeTileInfoMaxTile(const HdrPcs* pcs, AomWriteBitBuffer* wb);
// write_tile_info (:2581-:2614). Non-const: the reference mutates
// child_pcs->tile_size_bytes_minus_1 (:2584, :2603-:2612).
void writeTileInfo(HdrPcs* pcs, AomWriteBitBuffer* wb);

// FrameHeader (av1_structs.h:427-:601) - the fields the C2 spans consume;
// grown by later slices.
// ---------------------------------------------------------------------------
// C4: the sequence-header writer family + the OBU shells.
// ---------------------------------------------------------------------------
// frame_is_intra_only (entropy_coding.h:61-:63) / frame_is_sframe (:65-:67)
int hdFrameIsIntraOnly(const HdrPcs* pcs);
int hdFrameIsSFrame(const HdrPcs* pcs);
// frame_might_allow_ref_frame_mvs (:71-:74) / frame_might_allow_warped_motion (:77-:79)
int hdFrameMightAllowRefFrameMvs(const HdrPcs* pcs);
int hdFrameMightAllowWarpedMotion(const HdrPcs* pcs);
// av1_superres_unscaled (restoration.h:300-:302)
int hdAv1SuperresUnscaled(const HdrFrameSize* frm_size);
// write_bitstream_level (:3693-:3697)
void writeBitstreamLevel(int major, int minor, AomWriteBitBuffer* wb);
// add_trailing_bits (:3668-:3675)
void addTrailingBits(AomWriteBitBuffer* wb);
// write_obu_metadata (:3678-:3691); the SvtMetadataT wall threaded as
// (type, payload, sz); a null/empty payload writes nothing.
std::uint32_t writeObuMetadata(std::int32_t type, const std::uint8_t* payload, std::size_t sz,
                               AomWriteBitBuffer* wb);
// write_tile_group_header (:3765-:3782)
std::uint32_t writeTileGroupHeader(std::uint8_t* const dst, int startTile, int endTile,
                                   int tiles_log2, int tile_start_and_end_present_flag);
// get_ref_order_hint (:3225-:3231)
std::uint32_t getRefOrderHint(const HdrPcs* pcs, int ref_frame);
// write_color_config (:2687-:2752)
void writeColorConfig(const HdrSeqHeader* sh, AomWriteBitBuffer* wb);
// write_sequence_header (:2754-:2839); writes the derived frame_width_bits/
// frame_height_bits back (:2772-:2773).
void writeSequenceHeader(HdrSeqHeader* sh, AomWriteBitBuffer* wb);
// write_global_motion_params (:2996-:3062) / write_global_motion (:3064-:3097)
void writeGlobalMotionParams(const HdrWarpParams* params, const HdrWarpParams* ref_params,
                             AomWriteBitBuffer* wb, std::int32_t allow_hp);
void writeGlobalMotion(HdrPcs* pcs, AomWriteBitBuffer* wb);
// write_film_grain_params (:3100-:3222); the EbReferenceObject wall threaded.
void writeFilmGrainParams(HdrFilmGrainParams* pars, AomWriteBitBuffer* wb,
                          const HdrFilmGrainParams* ref_obj_0, const HdrFilmGrainParams* ref_obj_1,
                          int slice_is_b, int ref_idx_0, int ref_idx_1, int frame_type_is_inter,
                          int seq_color_mono, int scs_subsampling_x, int scs_subsampling_y);
// write_frame_size_with_refs (:3233-:3291); the ref order hints + dims facts
// threaded.
void writeFrameSizeWithRefs(HdrPcs* pcs, AomWriteBitBuffer* wb,
                            const std::uint32_t ref_order_hint[7], const int ref_dims_ok[7]);
// write_uncompressed_header_obu (:3294-:3637)
void writeUncompressedHeaderObu(HdrSeqHeader* sh, HdrPcs* pcs, AomWriteBitBuffer* wb,
                                std::uint8_t show_existing);
// write_sequence_header_obu (:3699-:3763); set_bitstream_level_tier PARKED
// (the level search) - the level carrier input.
std::uint32_t writeSequenceHeaderObu(HdrSeqHeader* sh, std::uint8_t* const dst,
                                     std::uint8_t number_spatial_layers);
// write_frame_header_obu (:3784-:3802)
std::uint32_t writeFrameHeaderObu(HdrSeqHeader* sh, HdrPcs* pcs, std::uint8_t* const dst,
                                  std::uint8_t show_existing, std::int32_t appendTrailingBits);
}  // namespace entropy
