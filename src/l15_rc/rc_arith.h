// l15_rc - the rate-control arithmetic surface (A9, RC2).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/rc_vbr_cbr.c,
// rc_rtc_cbr.c, rc_crf_cqp.c, rc_aq.c (the four portable arithmetic files;
// rc_process.c / rc_results.c / rc_tasks.c are the named pipeline park).
// STRUCT-PARKING (the RE8A DeblockContext precedent): every wall
// (RATE_CONTROL / RateControlCfg / Picture(Parent)ControlSet /
// SequenceControlSet / EncodeContext / EbReferenceObject) is threaded as a
// carrier struct or a parameter; bodies are ported verbatim with the
// reference operation order preserved (these bodies are double arithmetic -
// pin INTEGER-valued outputs exactly, compare doubles with the epsilon
// stated in tests/test_rc_arith.cpp).
// The minq/quant tables come from the LANDED rc_tables.h registry
// (findTable); the small consumed tables and rc_process.c dependency
// helpers are carried here with their reference spans cited - not
// duplicated from rc_tables.h.
// PARKED (named): see the per-span parks in rc_arith.cpp.
#pragma once

#include <cstdint>
#include <functional>

namespace l15_rc {

// aom_rc_mode (encoder.h:32-36): AOM_VBR = 0, AOM_CBR = 1, AOM_Q = 2.
constexpr int kRcVbr = 0;
constexpr int kRcCbr = 1;
constexpr int kRcQ = 2;

// rc_process.h:59-62.
constexpr int kBoostGfHighTplLa = 2400;
constexpr int kBoostGfLowTplLa = 300;
constexpr int kBoostKfHigh = 5000;
constexpr int kBoostKfLow = 400;

// MAX_HIERARCHICAL_LEVEL (EbSvtAv1Enc.h:34) / MAX_TEMPORAL_LAYERS
// (definitions.h:2030) / MAX_MINIGOP_SIZE (definitions.h:2031) - declared
// before the carriers that size arrays by them.
constexpr int kMaxHierarchicalLevel = 6;
constexpr int kMaxTemporalLayers = 6;
constexpr int kMaxMinigopSize = 1 << (kMaxTemporalLayers - 1);

// RATE_CONTROL carrier (rc_process.h:108-202): mirrors of the consumed
// fields only; grows with the slices that consume them.
struct RcControl {
    int kfBoost;   // rc->kf_boost
    int gfuBoost;  // rc->gfu_boost

    // the buffer model (all int64 in the reference)
    std::int64_t startingBufferLevel;   // rc->starting_buffer_level
    std::int64_t optimalBufferLevel;    // rc->optimal_buffer_level
    std::int64_t maximumBufferSize;     // rc->maximum_buffer_size
    std::int64_t bufferLevel;           // rc->buffer_level
    std::int64_t bitsOffTarget;         // rc->bits_off_target
    int avgFrameBandwidth;              // rc->avg_frame_bandwidth
    int maxFrameBandwidth;              // rc->max_frame_bandwidth
    int framesSinceKey;                 // rc->frames_since_key

    // the quality window and per-frame q history (RC2c Wave B)
    int worstQuality;             // rc->worst_quality
    int bestQuality;              // rc->best_quality
    int activeWorstQuality;       // rc->active_worst_quality
    int avgFrameQindex[2];        // rc->avg_frame_qindex[FRAME_TYPES] (0=KEY, 1=INTER)
    int lastQ[2];                 // rc->last_q[FRAME_TYPES] (0=KEY, 1=INTER)
    int q1Frame;                  // rc->q_1_frame
    int prevAvgFrameBandwidth;    // rc->prev_avg_frame_bandwidth
    std::uint32_t prevAvgBaseMeDist;  // rc->prev_avg_base_me_dist
    std::uint32_t curAvgBaseMeDist;   // rc->cur_avg_base_me_dist
    double rateCorrectionFactors[kMaxTemporalLayers + 1];  // rc->rate_correction_factors
    int baselineGfInterval;       // rc->baseline_gf_interval
    int framesToKey;              // rc->frames_to_key
    int constrainedGfGroup;       // rc->constrained_gf_group
    int thisKeyFrameForced;       // rc->this_key_frame_forced
    int resizeState;              // rc->resize_state (RESIZE_STATE)
    int resizeAvgQp;              // rc->resize_avg_qp
    int resizeCount;              // rc->resize_count
    int resizeBufferUnderflow;    // rc->resize_buffer_underflow

    // the Wave D3 rate-factor updater / postencode state
    int q2Frame;                  // rc->q_2_frame
    int rc1Frame;                 // rc->rc_1_frame
    int rc2Frame;                 // rc->rc_2_frame
    int lastBoostedQindex;        // rc->last_boosted_qindex
    int percentRefreshAdjustment; // rc->percent_refresh_adjustment
    double rateRatioQdeltaAdjustment;  // rc->rate_ratio_qdelta_adjustment

    // the RTC-CBR virtual mini-GoP state (RC2c Wave C)
    int avgFrameLowMotion;        // rc->avg_frame_low_motion
    double targetSizeFactors[kMaxTemporalLayers + 1];  // rc->target_size_factors
    double rcfValues[1 + kMaxMinigopSize];             // rc->rcf_values
    double rcfKalmanP[1 + kMaxMinigopSize];            // rc->rcf_kalman_P
    double rcfKalmanR[1 + kMaxMinigopSize];            // rc->rcf_kalman_R
    int miniQopSize;              // rc->mini_qop_size
    int rcMiniGopPos;             // rc->rc_mini_gop_pos
    int rcNumLayers;              // rc->rc_num_layers
    int minRefBaseQIdx;           // rc->min_ref_base_q_idx
    int rollingTargetBits;        // rc->rolling_target_bits
    int rollingActualBits;        // rc->rolling_actual_bits
    std::int64_t totalActualBits;  // rc->total_actual_bits
    std::int64_t totalTargetBits;  // rc->total_target_bits
    int framesSinceCdfUpdate;     // rc->frames_since_cdf_update
};

// RateControlCfg carrier (the enc_ctx->rc_cfg mirrors the bodies consume;
// the reference struct lives in enc_ctx.h - fields threaded per use).
struct RcConfig {
    int mode;  // aom_rc_mode (encoder.h:32-36): 0=VBR 1=CBR 2=Q
    int underShootPct;
    int overShootPct;
    int maxInterBitratePct;
    int maxIntraBitratePct;
    std::int64_t startingBufferLevelMs;
    std::int64_t optimalBufferLevelMs;
    std::int64_t maximumBufferSizeMs;
    int minQpAllowed;  // static_config.min_qp_allowed (0..63)
    int maxQpAllowed;  // static_config.max_qp_allowed (0..63)
    int worstAllowedQ;  // rc_cfg->worst_allowed_q
    int bestAllowedQ;   // rc_cfg->best_allowed_q
    int minCr;          // rc_cfg->min_cr (the compression-ratio floor, percent)
};

// PictureParentControlSet mirrors the one-pass target arithmetic consumes.
struct PcsCarrier {
    int frameType;  // frm_hdr.frame_type (FrameType, definitions.h:1585-1591)
    bool showableFrame;  // frm_hdr.showable_frame
    std::int64_t pictureNumber;  // picture_number
    int thisFrameTarget;         // this_frame_target
    int baseFrameTarget;         // base_frame_target
    int updateType;  // update_type (SvtAv1FrameUpdateType, EbSvtAv1Enc.h:181-190)

    // the RC2c Wave B fields
    int temporalLayerIndex;      // temporal_layer_index
    int hierarchicalLevels;      // hierarchical_levels (indexes kMaxDeltaPerLayer)
    int frameOffset;             // frame_offset
    bool sceneChangeFlag;        // scene_change_flag
    bool scClass1;               // sc_class1
    bool isOverlay;              // is_overlay
    bool rcResetFlag;            // rc_reset_flag
    int renderWidth;             // render_width
    int renderHeight;            // render_height
    int frameWidth;              // av1_cm->frm_size.frame_width
    int frameHeight;             // av1_cm->frm_size.frame_height
    int encoderBitDepth;         // scs->static_config.encoder_bit_depth (threaded
                                 // here because get_bits_per_mb / adjust_q_cbr
                                 // read it through ppcs->scs)

    // the RC2c Wave C fields (the ref-object / slice walls threaded)
    int sliceType;               // ppcs->slice_type (SliceType, definitions.h:1874-1878)
    int ref0BaseQIdx;            // pcs->ref_base_q_idx[REF_LIST_0][0]
    int ref1BaseQIdx;            // pcs->ref_base_q_idx[REF_LIST_1][0]
    int refList1CountTry;        // ppcs->ref_list1_count_try
    int ref0SliceType;           // pcs->ref_slice_type[REF_LIST_0][0]
    int ref0TmpLayer;            // get_ref_obj(pcs, REF_LIST_0, 0)->tmp_layer_idx

    // the RC2c Wave C2 fields (the driver-family walls threaded)
    std::int64_t targetBitRate;      // ppcs->target_bit_rate
    int numMbs;                      // scs->frame_info->num_mbs
    int vbrmaxSection;               // scs->twopass_cfg.vbrmax_section
    bool seqParamChanged;            // ppcs->seq_param_changed
    bool bitrateChanged;             // ppcs->bitrate_changed
    bool frameRateChanged;           // ppcs->frame_rate_changed
    int frameRateNumerator;          // static_config.frame_rate_numerator
    int frameRateDenominator;        // static_config.frame_rate_denominator
    int projectedFrameSize;          // ppcs->projected_frame_size
    std::uint64_t pcsTotalRate;      // ppcs->pcs_total_rate
    int loopCount;                   // ppcs->loop_count
    std::uint64_t totalNumBits;      // ppcs->total_num_bits
    int avgCntZeromv;                // ppcs->child_pcs->avg_cnt_zeromv
    int baseQIdx;                    // frm_hdr.quantization_params.base_q_idx
                                     // (definitions.h:1604-1606, the AV1 spec
                                     // quantization_params struct)

    // the RC2 Wave D2 walls (recode_loop_update_q's consumes)
    int superresUpscaledWidth;       // av1_cm->frm_size.superres_upscaled_width
    int seqProfile;                  // scs->seq_header.seq_profile (EbAv1SeqProfile)
    int maxFrameSize;                // ppcs->max_frame_size (the Q-mode bounds)

    // the A9R update_lambda consumes (rc_process.c:398-441): the frm_hdr
    // delta-q gate and the r0-delta-qp-md flag ride the ppcs wall.
    bool deltaQPresent;              // ppcs->frm_hdr.delta_q_params.delta_q_present
    bool r0DeltaQpMd;                // ppcs->r0_delta_qp_md
};

// ASSIGN_MINQ_TABLE (rc_tables.h:36-39, the CONFIG_ENABLE_HIGH_BIT_DEPTH=0
// branch): bit_depth is consumed and ignored - the 8-bit table is always
// selected. baseName is the table stem (e.g. "kf_low_motion_minq_cqp");
// returns the 256-entry QINDEX_RANGE table from the landed registry.
const std::int32_t* minqTable(const char* baseName, int bitDepth);

// get_active_quality (rc_vbr_cbr.c:31-45): verbatim.
int getActiveQuality(int q, int boost, int low, int high, const std::int32_t* lowMotionMinq,
                     const std::int32_t* highMotionMinq);

// get_kf_active_quality_tpl (rc_vbr_cbr.c:47-54): verbatim.
int getKfActiveQualityTpl(const RcControl& rc, int q, int bitDepth);

// get_gf_active_quality_tpl_la (rc_vbr_cbr.c:56-63): verbatim.
int getGfActiveQualityTplLa(const RcControl& rc, int q, int bitDepth);

// get_gf_high_motion_quality (rc_vbr_cbr.c:65-69): verbatim.
int getGfHighMotionQuality(int q, int bitDepth);

// ---- R2: the one-pass CBR target-size family ----

// rc_process.h:28.
constexpr int kFrameOverheadBits = 200;

// av1_calc_pframe_target_size_one_pass_cbr (rc_vbr_cbr.c:71-95): verbatim;
// the ppcs->scs->enc_ctx walk collapses to (rc, rcCfg).
int calcPframeTargetSizeOnePassCbr(const RcControl& rc, const RcConfig& rcCfg);

// svt_aom_reset_update_frame_target (rc_vbr_cbr.c:97-104): verbatim.
void resetUpdateFrameTarget(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs);

// av1_rc_clamp_iframe_target_size (rc_vbr_cbr.c:518-531): verbatim.
int rcClampIframeTargetSize(const RcControl& rc, const RcConfig& rcCfg, int target);

// av1_calc_iframe_target_size_one_pass_cbr (rc_vbr_cbr.c:534-551):
// verbatim; scs->new_framerate threaded as frameRate.
int calcIframeTargetSizeOnePassCbr(const RcControl& rc, const RcConfig& rcCfg, const PcsCarrier& pcs,
                                   double frameRate);

// set_rc_buffer_sizes (rc_vbr_cbr.c:591-603): verbatim;
// scs->static_config.target_bit_rate threaded as bandwidth.
void setRcBufferSizes(RcControl& rc, const RcConfig& rcCfg, std::int64_t bandwidth);

// update_buffer_level (rc_vbr_cbr.c:1475-1490): verbatim.
void updateBufferLevel(RcControl& rc, const PcsCarrier& ppcs, int encodedFrameSize);

// ---- RC2c Wave A (D-series): the quant-index arithmetic every RC body
// consumes, carried from rc_process.c / inv_transforms.c / md_process.c with
// their reference spans cited ----

// EbBitDepth (EbSvtAv1Formats.h:101-107): EB_EIGHT_BIT = 8, EB_TEN_BIT = 10,
// EB_TWELVE_BIT = 12. The 10/12-bit table branches are the
// CONFIG_ENABLE_HIGH_BIT_DEPTH=0 park (assert + return -1, release -1).
constexpr int kEbEightBit = 8;
constexpr int kEbTenBit = 10;
constexpr int kEbTwelveBit = 12;

// FrameType (definitions.h:1575-1583).
constexpr int kKeyFrame = 0;
constexpr int kInterFrame = 1;
constexpr int kIntraOnlyFrame = 2;
constexpr int kSFrame = 3;

// rate_factor_level (rc_process.h:38-46).
constexpr int kInterNormal = 0;
constexpr int kInterLow = 1;
constexpr int kInterHigh = 2;
constexpr int kGfArfLow = 3;
constexpr int kGfArfStd = 4;
constexpr int kKfStd = 5;

// SvtAv1FrameUpdateType (EbSvtAv1Enc.h:181-190).
constexpr int kSvtAv1KfUpdate = 0;
constexpr int kSvtAv1LfUpdate = 1;
constexpr int kSvtAv1GfUpdate = 2;
constexpr int kSvtAv1ArfUpdate = 3;
constexpr int kSvtAv1IntnlArfUpdate = 6;

// ResolutionRange (definitions.h:1808-1818).
constexpr int kInputSize720pRange = 3;
constexpr int kInputSize480pRange = 2;

// EncMode (EbSvtAv1Enc.h:44-61): the band boundaries the tpl level pair
// consumes (ENC_MR is the research mode).
constexpr int kEncMr = -1;
constexpr int kEncM2 = 2;
constexpr int kEncM5 = 5;
constexpr int kEncM7 = 7;
constexpr int kEncM8 = 8;
constexpr int kEncM13 = 13;

// SliceType (definitions.h:1874-1878).
constexpr int kBslice = 0;
constexpr int kIslice = 1;

// definitions.h:1641-1643, :1646.
constexpr int kMinQ = 0;
constexpr int kMaxQ = 255;
constexpr int kQindexRange = 256;
constexpr int kMaxQpValue = 63;

// rc_process.h:26, :33, :35, :36, :51, :52, :54, :56, :57.
constexpr int kBperMbNormbits = 9;
constexpr int kStaticKfGroupThresh = 99;
constexpr int kMaxGfInterval = 32;
constexpr int kMaxArfLayers = 6;
constexpr int kCriticalBufferLevel = 15;
constexpr int kOptimalBufferLevel = 70;
constexpr double kMaxGfuBoostFactor = 10.0;
constexpr double kMinBpbFactor = 0.005;
constexpr double kMaxBpbFactor = 50.0;

// quantizer_to_qindex (md_process.c:20-23).
extern const std::uint8_t kQuantizerToQindex[64];

// percents (md_process.c:25-27; FIXED_QP_OFFSET_COUNT = 6, md_process.h:1320).
extern const int kPercents[2][6];

// max_delta_per_layer (rc_vbr_cbr.c:165-166).
extern const int kMaxDeltaPerLayer[kMaxHierarchicalLevel][kMaxTemporalLayers];

// The 8-bit quantizer lookup tables (inv_transforms.c:3412-3427 and
// :3357-3372). The 10/12-bit rows are the CONFIG_ENABLE_HIGH_BIT_DEPTH=0
// park; l3_transforms carries its own file-static copy for the transform
// layer (not linkable, not duplicated here by name - this module owns ONE
// registry copy).
extern const std::int16_t kDcQLookupQtx[kQindexRange];
extern const std::int16_t kAcQLookupQtx[kQindexRange];

// svt_aom_dc_quant_qtx (inv_transforms.c:3467-3482): verbatim.
std::int16_t dcQuantQtx(int qindex, int delta, int bitDepth);

// svt_aom_ac_quant_qtx (inv_transforms.c:3484-3499): verbatim.
std::int16_t acQuantQtx(int qindex, int delta, int bitDepth);

// svt_av1_convert_qindex_to_q (rc_process.c:185-198): verbatim.
double convertQindexToQ(int qindex, int bitDepth);

// svt_av1_compute_qdelta (rc_process.c:200-222): verbatim.
int computeQdelta(double qstart, double qtarget, int bitDepth);

// svt_av1_rc_bits_per_mb (rc_process.c:254-267): verbatim.
int rcBitsPerMb(int frameType, int qindex, double correctionFactor, int bitDepth, int isScreenContent);

// find_qindex_by_rate (rc_process.c:269-287): static in the reference;
// exported for testability (the DF1 signedCharClamp precedent).
int findQindexByRate(int desiredBitsPerMb, int bitDepth, int frameType, int isScreenContent, int bestQindex,
                     int worstQindex);

// svt_av1_compute_qdelta_by_rate (rc_process.c:289-300): verbatim; the
// RATE_CONTROL* consumes best/worst_quality only - threaded as the pair.
int computeQdeltaByRate(int bestQuality, int worstQuality, int frameType, int qindex, double rateTargetRatio,
                        int bitDepth, int isScreenContent);

// svt_av1_rate_factor_deltas (rc_process.c:302-309) and
// svt_av1_rate_factor_levels (:311-319), indexed by rate_factor_level and
// SvtAv1FrameUpdateType respectively.
extern const double kRateFactorDeltas[6];
extern const int kRateFactorLevelsMap[7];

// svt_av1_get_q_index_from_qstep_ratio (rc_process.c:321-341): verbatim.
int getQIndexFromQstepRatio(int leafQindex, double qstepRatio, int bitDepth);

// svt_av1_get_cqp_kf_boost_from_r0 (rc_process.c:229-243): verbatim;
// input_resolution threaded (ResolutionRange).
int getCqpKfBoostFromR0(double r0, int framesToKey, int inputResolution);

// svt_av1_get_gfu_boost_from_r0_lap (rc_process.c:245-252): verbatim.
int getGfuBoostFromR0Lap(double minFactor, double maxFactor, double r0, int framesToKey);

// svt_av1_calculate_boost_bits (rc_process.c:641-667): verbatim.
int calculateBoostBits(int frameCount, int boost, std::int64_t totalGroupBits);

// ---- A9R (the remaining pure arithmetic): rc_process.c rd-multiplier
// family ----

// def_inter_rd_multiplier (rc_process.c:346-348): static in the reference;
// exported for testability (the DF1 signedCharClamp precedent).
double defInterRdMultiplier(int qindex);

// def_arf_rd_multiplier (rc_process.c:353-355): static in the reference.
double defArfRdMultiplier(int qindex);

// def_kf_rd_multiplier (rc_process.c:360-362): static in the reference.
double defKfRdMultiplier(int qindex);

// svt_aom_compute_rd_mult_based_on_qindex (rc_process.c:364-392): verbatim;
// the 10/12-bit branches ride on the dcQuantQtx HBD park (see its note).
int computeRdMultBasedOnQindex(int bitDepth, int updateType, int qindex);

// rd_frame_type_factor (rc_process.c:394-395), row [0] = 8-bit, row [1] =
// the 10/12-bit rows.
extern const int kRdFrameTypeFactor[2][7];

// RTC_KF_LAMBDA_BOOST (rc_process.c:396).
constexpr int kRtcKfLambdaBoost = 100;

// update_lambda (rc_process.c:398-441): static in the reference; exported
// for testability. The pcs walls thread: the ppcs frame fields ride on
// PcsCarrier (frameType, temporalLayerIndex, hierarchicalLevels, baseQIdx,
// deltaQPresent, r0DeltaQpMd); the scs->static_config.rtc /
// stats_based_sb_lambda_modulation gates thread as booleans.
std::uint32_t updateLambda(const PcsCarrier& pcs, std::uint8_t qIndex, std::uint8_t meQIndex, int bitDepth,
                           std::int64_t rdmult, bool rtc, bool statsBasedSbLambdaModulation);

// svt_aom_compute_rd_mult (rc_process.c:446-451): verbatim; the walls thread
// as for updateLambda (the reference reads pcs->ppcs->update_type off the
// carrier).
std::uint32_t computeRdMult(const PcsCarrier& pcs, std::uint8_t qIndex, std::uint8_t meQIndex, int bitDepth,
                            bool rtc, bool statsBasedSbLambdaModulation);

// svt_aom_compute_fast_lambda (rc_process.c:453-464): verbatim; the
// CONFIG_ENABLE_HIGH_BIT_DEPTH branch is the named park (the 10/12-bit SAD
// tables are dead in the pinned RTC 8-bit build), so every depth reads
// kLambdaModeDecision8BitSad.
std::uint32_t computeFastLambda(const PcsCarrier& pcs, std::uint8_t qIndex, std::uint8_t meQIndex, int bitDepth,
                                bool rtc, bool statsBasedSbLambdaModulation);

// svt_aom_lambda_assign (rc_process.c:466-496): verbatim; lambdaScaleFactor
// threads static_config.lambda_scale_factors[update_type] (the enc_settings
// default is 128 per entry); multiply_lambda is dead in the 8-bit build
// ((void)'d in the reference). For bit depths 10/12 the assert-removed else
// leaves the caller's values in place and only the scale shift applies.
void lambdaAssign(const PcsCarrier& pcs, std::uint32_t& fastLambda, std::uint32_t& fullLambda, int bitDepth,
                  std::uint8_t qpIndex, bool multiplyLambda, std::uint64_t lambdaScaleFactor, bool rtc,
                  bool statsBasedSbLambdaModulation);

// ---- A9R: initial_rc_process.c ----

// svt_aom_get_tpl_group_level (initial_rc_process.c:190-202): verbatim.
std::uint8_t getTplGroupLevel(std::uint8_t tpl, std::int8_t encMode);

// get_tpl_params_level (initial_rc_process.c:307-317): static in the
// reference; exported for testability (the DF1 signedCharClamp precedent).
std::uint8_t getTplParamsLevel(std::int8_t encMode);

// frame_is_intra_only (entropy_coding.h:61-64): the frame_type argument
// threaded.
bool frameIsIntraOnly(int frameType);

// frame_is_leaf (enc_mode_config.h:113-116): the update_type argument
// threaded (SVT_AV1_LF_UPDATE).
bool frameIsLeaf(int updateType);

// svt_aom_frame_is_kf_gf_arf (rc_process.c:55-59): the ppcs carrier reads
// frameType + updateType (PcsCarrier gains both fields).
bool frameIsKfGfArf(const PcsCarrier& pcs);

// clamp_qindex (rc_vbr_cbr.c:21-25): verbatim; RcConfig gains
// minQpAllowed/maxQpAllowed.
std::uint8_t clampQindex(const RcConfig& cfg, int qindex);

// clamp_qp (rc_crf_cqp.c:22-26): verbatim.
std::uint8_t clampQp(const RcConfig& cfg, int qp);

// ---- RC2c Wave A (AQ-series): rc_aq.c ----

// ME_TIER_ZERO_PU_8x8_0 (me_context.h:68) = 21.
constexpr int kMeTierZeroPu8x8_0 = 21;

// rc_aq.c:70-74: SUPERBLOCK_SIZE 64 (the 8x8 grid), SUBBLOCK_SIZE 8,
// SUBBLOCKS_IN_SB 64, SUBBLOCKS_IN_OCTILE 8.
constexpr int kSubblocksInSb = 64;
constexpr int kSubblocksInOctile = 8;

// svt_av1_convert_qindex_to_q_fp8 (rc_aq.c:24-37): verbatim.
std::int32_t convertQindexToQFp8(int qindex, int bitDepth);

// svt_av1_compute_qdelta_fp (rc_aq.c:39-61): verbatim.
std::int32_t computeQdeltaFp(std::int32_t qstartFp8, std::int32_t qtargetFp8, int bitDepth);

// variance_comp_int (rc_aq.c:63-65): exported (non-static in the
// reference); the qsort comparator.
int varianceCompInt(const void* a, const void* b);

// av1_get_deltaq_sb_variance_boost (rc_aq.c:76-178): verbatim; the
// variances pointer is ppcs->variance[sb_addr] (the 8x8 window sits at the
// kMeTierZeroPu8x8_0 offset).
int getDeltaqSbVarianceBoost(std::uint8_t baseQIdx, const std::uint16_t* variances, std::uint8_t strength,
                             int bitDepth, std::uint8_t octile, std::uint8_t curve);

// The frm_hdr.* fields the RC bodies consume (FrameHeader wall threaded).
struct FrameHdrCarrier {
    int baseQIdx;        // quantization_params.base_q_idx
    bool deltaQPresent;  // delta_q_params.delta_q_present
    int deltaQRes;       // delta_q_params.delta_q_res
    int deltaQDc[3];     // delta_q_dc[1..2] used
    int deltaQAc[3];     // delta_q_ac[1..2] used
};

// CyclicRefresh (pcs.h:659-712): the consumed fields.
struct CrRefreshCarrier {
    int percentRefresh;
    int maxQdeltaPerc;
    std::uint32_t sbStart;
    std::uint32_t sbEnd;
    double rateRatioQdelta;
    double rateRatioQdeltaSeg2;
    int applyCyclicRefresh;
    int rateBoostFac;
    int qindexDelta[3];
    std::uint64_t meDistortion[3];
    int actualNumSeg1Sbs;
    int actualNumSeg2Sbs;
};

// The pcs ME-statistics wall threaded (pcs.h:866-870, :1168 +
// me_sb_results.h:38-46: me_mv_array[0] of the b64th MeSbResults).
struct MeStatCarrier {
    const std::uint32_t* me8x8Distortion;    // ppcs->me_8x8_distortion
    const std::uint32_t* me64x64Distortion;  // ppcs->me_64x64_distortion
    const std::uint32_t* me8x8CostVariance;  // ppcs->me_8x8_cost_variance
    const std::int16_t* mvX;                 // pa_me_data->me_results[b]->me_mv_array[0].x
    const std::int16_t* mvY;                 // .y
    std::uint64_t normMeDist;                // ppcs->norm_me_dist
    std::uint16_t b64TotalCount;             // ppcs->b64_total_count
};

// svt_av1_variance_adjust_qp (rc_aq.c:180-278): verbatim; sb_cnt selects
// scs->sb_total_count or ppcs->b64_total_count under superres/resize.
void varianceAdjustQp(FrameHdrCarrier& hdr, std::uint8_t* sbQindex, const std::uint16_t* const* variance,
                      std::uint16_t scsSbTotalCount, std::uint16_t ppcsB64TotalCount, bool superresOrResize,
                      int varianceBoostStrength, int encoderBitDepth, std::uint8_t varianceOctile,
                      std::uint8_t varianceBoostCurve);

// is_in_cr_band (rc_aq.c:284-286): verbatim.
bool isInCrBand(std::uint32_t b64Idx, std::uint32_t sbStart, std::uint32_t sbEnd);

// svt_aom_cyclic_refresh_setup (rc_aq.c:295-342): verbatim; the
// is_cr_motion_static helper (:290-293) stays inline in the body.
void cyclicRefreshSetup(CrRefreshCarrier& cr, const MeStatCarrier& me);

// cyclic_sb_qp_assignment (rc_aq.c:350-375): verbatim; sets
// delta_q_present = 1 (rc_aq.c:354), so the header carrier is mutable; the
// SB walk is ppcs->b64_total_count long (rc_aq.c:360) - the MeStatCarrier
// carries it.
void cyclicSbQpAssignment(FrameHdrCarrier& hdr, std::uint8_t* sbQindex, const CrRefreshCarrier& cr,
                          const MeStatCarrier& me);

// svt_av1_generate_b64_me_qindex_map (rc_aq.c:380-417): verbatim; the
// pcs->slice_type / temporal_layer_index arguments threaded.
void generateB64MeQindexMap(const FrameHdrCarrier& hdr, const MeStatCarrier& me, std::uint8_t* b64MeQindex,
                            int sliceType, int temporalLayerIndex);

// svt_av1_get_deltaq_offset (rc_aq.c:419-451): verbatim.
int getDeltaqOffset(int bitDepth, int qindex, double beta, bool isIntra);

// svt_av1_normalize_sb_delta_q (rc_aq.c:554-592): verbatim.
void normalizeSbDeltaQ(FrameHdrCarrier& hdr, std::uint8_t* sbQindex, std::uint16_t scsSbTotalCount,
                       std::uint16_t ppcsB64TotalCount, bool superresOrResize);

// svt_av1_rc_init_sb_qindex (rc_aq.c:595-628): verbatim; the
// CONFIG_ENABLE_TPL svt_aom_sb_qp_derivation_tpl_la call site is the named
// park (see the AQ8/AQ9 park note in rc_arith.cpp) - aqMode/tplEnable/r0
// thread the gate so the parked call site keeps its reference shape.
void rcInitSbQindex(int rcMode, CrRefreshCarrier& cr, FrameHdrCarrier& hdr, std::uint8_t* sbQindex,
                    const std::uint16_t* const* variance, std::uint16_t pcsSbTotalCount, const MeStatCarrier& me,
                    bool enableVarianceBoost, int varianceBoostStrength, int encoderBitDepth,
                    std::uint8_t varianceOctile, std::uint8_t varianceBoostCurve, int aqMode, bool tplEnable,
                    double r0);

// ---- RC2c Wave B (V-series): the one-pass rate allocation + dynamic
// resize family (rc_vbr_cbr.c:107-:589) ----

// RESIZE_STATE (rc_process.h:94) and RESIZE_ACTION (rc_process.h:86-92).
constexpr int kResizeOrig = 0;
constexpr int kResizeThreeQuarter = 1;
constexpr int kResizeOneHalf = 2;
constexpr int kResizeNoResize = 0;       // RESIZE_ACTION NO_RESIZE
constexpr int kResizeDownThreeFour = 1;  // RESIZE_ACTION
constexpr int kResizeDownOneHalf = 2;
constexpr int kResizeUpThreeFour = -1;
constexpr int kResizeUpOrig = -2;

// SCALE_NUMERATOR 8 / SCALE_DENOMINATOR_MAX 16 / SCALE_THREE_QUATER 17
// (definitions.h:1430, :1435-1436).
constexpr int kScaleNumerator = 8;
constexpr int kScaleDenominatorMax = 16;
constexpr int kScaleThreeQuater = 17;

// DEFAULT_KF_BOOST_RT / DEFAULT_GF_BOOST_RT (rc_vbr_cbr.c:360-361).
constexpr int kDefaultKfBoostRt = 2300;
constexpr int kDefaultGfBoostRt = 2000;

// calc_active_worst_quality_no_stats_cbr (rc_vbr_cbr.c:107-163): verbatim;
// the frame_updated mutex walk (:129-131) threads the counter.
int calcActiveWorstQualityNoStatsCbr(const RcControl& rc, const PcsCarrier& ppcs, int frameUpdated);

// adjust_q_cbr (rc_vbr_cbr.c:168-205): verbatim; the encoder bit depth is
// read via ppcs (PcsCarrier.encoderBitDepth).
int adjustQcbr(const RcControl& rc, const PcsCarrier& ppcs, int q);

// get_bits_per_mb (rc_vbr_cbr.c:208-211): verbatim.
int getBitsPerMb(const PcsCarrier& ppcs, double correctionFactor, int q);

// find_closest_qindex_by_rate (rc_vbr_cbr.c:217-254): verbatim.
int findClosestQindexByRate(int desiredBitsPerMb, const PcsCarrier& ppcs, double correctionFactor,
                            int bestQindex, int worstQindex);

// get_rate_correction_factor (rc_vbr_cbr.c:256-278): verbatim; the
// rc->rc_mutex block/release is dropped (named park).
double getRateCorrectionFactor(const RcControl& rc, const RcConfig& rcCfg, const PcsCarrier& ppcs, int width,
                               int height);

// set_rate_correction_factor (rc_vbr_cbr.c:280-305): verbatim.
void setRateCorrectionFactor(RcControl& rc, const RcConfig& rcCfg, const PcsCarrier& ppcs, double factor,
                             int width, int height);

// av1_rc_regulate_q (rc_vbr_cbr.c:307-322): verbatim.
int av1RcRegulateQ(const RcControl& rc, const RcConfig& rcCfg, const PcsCarrier& ppcs, int activeBestQuality,
                   int activeWorstQuality, int width, int height);

// svt_av1_resize_reset_rc (rc_vbr_cbr.c:324-358): verbatim.
void resizeResetRc(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs, int resizeWidth, int resizeHeight,
                   int prevWidth, int prevHeight, int frameUpdated);

// set_gf_interval_update_onepass_rt (rc_vbr_cbr.c:363-380): verbatim.
bool setGfIntervalUpdateOnepassRt(RcControl& rc, const PcsCarrier& pcs);

// dynamic_resize_one_pass_cbr (rc_vbr_cbr.c:382-492): verbatim; the
// SVT_DEBUG prints are dropped (log macros); the scs walls (new_framerate,
// static_config.rtc, max_input_luma_*) thread as parameters.
void dynamicResizeOnePassCbr(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs, bool oneHalfOnly,
                             double newFramerate, bool rtc, int maxInputLumaWidth, int maxInputLumaHeight,
                             int frameUpdated);

// resize_pending_params (rc_process.h:103-106): the scs wall threaded.
struct ResizePendingCarrier {
    int resizeState;           // resize_state (RESIZE_STATE)
    std::uint8_t resizeDenom;  // resize_denom
};

// svt_aom_dynamic_resize_decision (rc_vbr_cbr.c:497-516): verbatim (the
// caller passes one_half_only = 1).
void dynamicResizeDecision(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs, double newFramerate, bool rtc,
                           int maxInputLumaWidth, int maxInputLumaHeight, int frameUpdated,
                           ResizePendingCarrier& pending);

// svt_aom_one_pass_rt_rate_alloc (rc_vbr_cbr.c:553-589): verbatim; the
// scs->static_config gates (resize_mode == RESIZE_DYNAMIC, pass ==
// ENC_SINGLE_PASS, pred_structure == LOW_DELAY) thread as booleans.
void onePassRtRateAlloc(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& pcs, double newFramerate, bool rtc,
                        bool resizeDynamic, bool singlePass, bool lowDelay, bool rcResetFlag,
                        int intraPeriodLength, int maxInputLumaWidth, int maxInputLumaHeight,
                        int frameUpdated, ResizePendingCarrier& pending);

// ---- RC2c Wave C (RTC-series): rc_rtc_cbr.c ----

// get_msb (definitions.h:596-605): the portable floor(log2) for n != 0
// (the reference asserts n != 0; the loop form returns 0 for 0).
int getMsb(std::uint32_t n);

// av1_estimate_bits_at_qindex (rc_rtc_cbr.c:42-58): verbatim; the
// rc->min_ref_base_q_idx / frame_type / me_dist arguments threaded.
double estimateBitsAtQindex(bool keyFrame, int minRefBaseQIdx, int qindex, double rcf, std::uint64_t meDist,
                            int bitDepth);

// find_closest_arg (rc_rtc_cbr.c:73-97): verbatim; the reference's
// arg_eval_fn + void* ctx pair threads as a callable.
int findClosestArg(double target, int minArg, int maxArg, const std::function<double(int)>& eval);

// rtc_compute_cr_deltaq (rc_rtc_cbr.c:103-121): verbatim; the rc/cfg
// walls thread (best/worst quality, min/max qp allowed); the key-frame
// flag propagates from the caller.
int rtcComputeCrDeltaq(const CrRefreshCarrier& cr, const RcControl& rc, const RcConfig& rcCfg, bool keyFrame,
                       int qindex, double rcf, std::uint64_t meDist, double rateRatio, int bitDepth);

// rtc_cyclic_refresh_compute_cr_qdeltas (rc_rtc_cbr.c:123-136): verbatim;
// the per-segment me_distortion rides on the CR carrier.
void rtcCyclicRefreshComputeCrQdeltas(CrRefreshCarrier& cr, const RcControl& rc, const RcConfig& rcCfg,
                                      bool keyFrame, int qindex, double rcf, int bitDepth);

// av1_estimate_frame_size (rc_rtc_cbr.c:138-163): verbatim; the CR
// weighting consumes the carrier's segment state.
int estimateFrameSize(const RcControl& rc, const RcConfig& rcCfg, const CrRefreshCarrier& cr, bool keyFrame,
                      int qindex, double rcf, int frameWidth, int frameHeight, std::uint16_t b64TotalCount,
                      bool calcSbQindex, int bitDepth);

// normalize_factors (rc_rtc_cbr.c:175-184): verbatim (the reference calls
// it in-place; the pointers may alias).
void normalizeFactors(double* dst, const double* src, int iStart, int iEnd);

// index2tl (rc_rtc_cbr.c:186-188): verbatim.
int index2tl(int index, int levels);

// calc_pframe_target_size (rc_rtc_cbr.c:190-269): verbatim; the
// pred_structure gate threads as randomAccess (RANDOM_ACCESS -> 0.5).
int calcPframeTargetSizeRtc(RcControl& rc, const RcConfig& rcCfg, const PcsCarrier& ppcs, bool randomAccess);

// cr_select_sbs (rc_rtc_cbr.c:272-298): verbatim; enc_ctx->cr_sb_index
// threads by reference (the persistent cycling index).
void crSelectSbs(CrRefreshCarrier& cr, std::uint32_t& crSbIndex, std::uint16_t sbsInFrame);

// rtc_cyclic_refresh_init (rc_rtc_cbr.c:300-360): verbatim;
// super_block_size threads (the != 64 gate); frame_is_leaf consumes
// ppcs.updateType.
void rtcCyclicRefreshInit(RcControl& rc, CrRefreshCarrier& cr, const PcsCarrier& ppcs, int rcMode,
                          int superBlockSize, std::uint32_t& crSbIndex, std::uint16_t sbsInFrame,
                          int bitDepth);

// get_rcf_index (rc_rtc_cbr.c:362-368): verbatim.
int getRcfIndex(bool keyFrame, int rcMiniGopPos, int miniQopSize);

// rtc_get_rate_correction_factor (rc_rtc_cbr.c:370-381): verbatim (NO
// fclamp on get - unlike the vbr-path getter).
double rtcGetRateCorrectionFactor(const RcControl& rc, const PcsCarrier& ppcs, int width, int height);

// rtc_set_rate_correction_factor (rc_rtc_cbr.c:383-400): verbatim (the
// pessimistic reset loop when frames_since_key < mini_qop_size).
void rtcSetRateCorrectionFactor(RcControl& rc, const PcsCarrier& ppcs, double rcf, int width, int height);

// calculate_qindex (rc_rtc_cbr.c:402-457): verbatim; the ref-object wall
// threads through PcsCarrier; the unconditional svt_aom_cyclic_refresh_
// setup call (:447) is the landed AQ4 body and consumes the ME-statistics
// carrier; super_block_size and intra_period_length thread.
int calculateQindexRtc(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs, CrRefreshCarrier& cr,
                       const MeStatCarrier& me, std::uint32_t& crSbIndex, std::uint16_t sbsInFrame,
                       int superBlockSize, int intraPeriodLength, bool randomAccess);

// ---- RC2c Wave C2 (RTC-series): the driver family ----

// svt_av1_rc_init (rc_process.c:498-536): verbatim; the scs walls thread
// (hierarchical_levels, target_bit_rate, frame_info->num_mbs,
// two_pass_cfg.vbrmax_section, frame_rate_numerator/denominator);
// newFramerate mirrors scs->new_framerate (read + written by
// svt_av1_new_framerate / av1_rc_update_framerate, pass2_strategy.c
// :877-904: MAX_MB_RATE 250, MAXRATE_1080P 2025000, the < 0.1 -> 30
// clamp).
void rcInit(RcControl& rc, const RcConfig& rcCfg, double& newFramerate, int frameRateNumerator,
            int frameRateDenominator, int hierarchicalLevels, std::int64_t targetBitRate, int numMbs,
            int vbrmaxSection);

// svt_av1_rc_calc_qindex_rtc_cbr (rc_rtc_cbr.c:459-549): verbatim; the
// resize gate and the pred-structure walls thread as booleans.
void calcQindexRtcCbr(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs, CrRefreshCarrier& cr,
                      const MeStatCarrier& me, std::uint32_t& crSbIndex, std::uint16_t sbsInFrame,
                      bool resizeDynamic, bool singlePass, bool lowDelay, bool randomAccess,
                      ResizePendingCarrier& pending, double& newFramerate);

// rtc_update_rate_correction_factors (rc_rtc_cbr.c:551-609): verbatim;
// projected_frame_size threads (ppcs->projected_frame_size); the Kalman
// filter on the log-rcf state.
void rtcUpdateRateCorrectionFactors(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs,
                                    const CrRefreshCarrier& cr, std::uint16_t b64TotalCount, int bitDepth);

// rtc_update_buffer_level (rc_rtc_cbr.c:615-626): verbatim (the INVERTED
// leaky bucket - no upper clip, floored at 0).
void rtcUpdateBufferLevel(RcControl& rc, const PcsCarrier& ppcs, int encodedFrameSize);

// svt_av1_rc_recode_decision_rtc_cbr (rc_rtc_cbr.c:630-689): verbatim;
// returns true when the recode rewrites base_q_idx.
bool recodeDecisionRtcCbr(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs, const CrRefreshCarrier& cr,
                          std::uint16_t b64TotalCount, int bitDepth);

// svt_av1_rc_postencode_update_rtc_cbr (rc_rtc_cbr.c:691-729): verbatim;
// the resize_mode == RESIZE_DYNAMIC gate threads as resizeDynamic.
void postencodeUpdateRtcCbr(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs,
                            const CrRefreshCarrier& cr, std::uint16_t b64TotalCount, int bitDepth,
                            bool resizeDynamic);

// ---- RC2 Wave D (recode helpers): rc_vbr_cbr.c :1642-:1789 ----

// RecodeLoopType (definitions.h:295-307).
constexpr int kDisallowRecode = 0;
constexpr int kAllowRecodeKfMaxBw = 1;
constexpr int kAllowRecodeKfArfGf = 2;
constexpr int kAllowRecode = 3;

// EbAv1SeqProfile (EbSvtAv1.h:144).
constexpr int kMainProfile = 0;
constexpr int kHighProfile = 1;
constexpr int kProfessionalProfile = 2;

// av1_find_qindex (rc_vbr_cbr.c:1773-1789): verbatim; the bit-depth threads
// (the reference reads the encoder bit depth through the scs wall).
int av1FindQindex(double desiredQ, int bitDepth, int bestQindex, int worstQindex);

// av1_get_compression_ratio (rc_vbr_cbr.c:1690-1699): verbatim; the
// frm_size / seq_header walls thread (superres_upscaled_width, frame_height,
// seq_profile).
double getCompressionRatio(int superresUpscaledWidth, int height, int profile,
                           std::uint64_t encodedFrameSize);

// av1_rc_compute_frame_size_bounds (rc_vbr_cbr.c:1701-1717): verbatim; the
// enc_ctx->recode_tolerance / rc_cfg->mode / ppcs->max_frame_size /
// ppcs->loop_count / rc->max_frame_bandwidth walls thread; the reference's
// two out-pointers become references.
void computeFrameSizeBounds(int mode, int recodeTolerance, int maxFrameSize, int loopCount,
                            int frameTarget, int rcMaxFrameBandwidth, int& frameUnderShootLimit,
                            int& frameOverShootLimit);

// recode_loop_test (rc_vbr_cbr.c:1756-1771): verbatim; the
// enc_ctx->recode_loop wall threads as recodeLoop (RecodeLoopType); the
// kfgfarf predicate is the landed frameIsKfGfArf.
bool recodeLoopTest(const RcControl& rc, const PcsCarrier& pcs, int recodeLoop, int highLimit,
                    int lowLimit, int q, int maxq, int minq);

// get_kf_q_tpl (rc_vbr_cbr.c:1642-1661): verbatim; the rc walls thread
// (active_worst_quality, kf_boost). The search's prev_dif is the INITIAL
// difference and is NOT refreshed inside the loop (:1650-1659).
int getKfQTpl(const RcControl& rc, int targetActiveQuality, int bitDepth);

// get_gfu_q_tpl (rc_vbr_cbr.c:1667-1688): verbatim; same prev_dif quirk
// (:1677-1686).
int getGfuQTpl(const RcControl& rc, int targetActiveQuality, int bitDepth);

// ---- RC2 Wave D3 (the vbr updater + the postencode pair + the regulated
// pair): rc_vbr_cbr.c :1349-:1472 / :1495-:1629 / :1720-:1752 ----

// AV1_PROB_COST_SHIFT (definitions.h): the rate-probability fractional bit
// count; the dummy pack shifts pcs_total_rate by it.
constexpr int kAv1ProbCostShift = 9;

// av1_estimate_bits_at_q (rc_vbr_cbr.c:1349-1353): verbatim; the frame
// type / screen-content flags thread (the reference reads them off ppcs).
int estimateBitsAtQ(int frameType, int q, int mbs, double correctionFactor, int bitDepth,
                    bool isScreenContent);

// av1_rc_update_rate_correction_factors (rc_vbr_cbr.c:1355-1472): verbatim;
// the width/height thread as parameters (the reference's own signature);
// b64_total_count threads (the C2 convention - the reference reads
// ppcs->b64_total_count).
void rcUpdateRateCorrectionFactors(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs,
                                   const CrRefreshCarrier& cr, std::uint16_t b64TotalCount, int width,
                                   int height, int bitDepth);

// RateControlIntervalParamContext (pcs.h), consumed fields only - grows
// with the slices that consume it (the carrier-grows precedent).
struct RcIntervalParamCarrier {
    int rollingTargetBits;       // rc_param_ptr->rolling_target_bits
    int rollingActualBits;       // rc_param_ptr->rolling_actual_bits
    std::int64_t totalActualBits;  // rc_param_ptr->total_actual_bits
    std::int64_t totalTargetBits;  // rc_param_ptr->total_target_bits
};

// svt_av1_rc_postencode_update_gop_const (rc_vbr_cbr.c:1495-1561): verbatim;
// the rolling/total bits go to the interval-param carrier; NO low-motion
// EMA in this twin. The enc_ctx->frame_updated counter threads as an
// int& (the frame_updated_mutex ops are the named park).
void postencodeUpdateGopConst(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs,
                              const CrRefreshCarrier& cr, RcIntervalParamCarrier& rcParam,
                              std::uint16_t b64TotalCount, int bitDepth, int& frameUpdated);

// svt_av1_rc_postencode_update (rc_vbr_cbr.c:1563-1629): verbatim; the
// low-motion EMA sits OUTSIDE the leaf exclusion (:1618-1620).
void postencodeUpdate(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs,
                      const CrRefreshCarrier& cr, std::uint16_t b64TotalCount, int bitDepth,
                      int& frameUpdated);

// get_regulated_q_overshoot (rc_vbr_cbr.c:1720-1735): verbatim; the width/
// height read off the carrier (the reference reads frm_size).
int getRegulatedQOvershoot(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs,
                           const CrRefreshCarrier& cr, std::uint16_t b64TotalCount, int qLow,
                           int qHigh, int topIndex, int bottomIndex, int bitDepth);

// get_regulated_q_undershoot (rc_vbr_cbr.c:1738-1752): verbatim.
int getRegulatedQUndershoot(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs,
                            const CrRefreshCarrier& cr, std::uint16_t b64TotalCount, int qHigh,
                            int topIndex, int bottomIndex, int bitDepth);

// ---- RC2 Wave D4: the vbr cyclic-refresh trio (rc_vbr_cbr.c :895-:998) ----

// CR_MAX_RATE_TARGET_RATIO (rc_vbr_cbr.c:987).
constexpr double kCrMaxRateTargetRatio = 4.0;

// cyclic_refresh_init (rc_vbr_cbr.c:895-972): verbatim; the scs walls
// thread (super_block_size != 64 gate, sb_total_count, static_config.
// hierarchical_levels); enc_ctx->cr_sb_end threads as the persistent
// cycling index (the crSelectSbs precedent). The I_SLICE reset writes
// percent_refresh_adjustment / rate_ratio_qdelta_adjustment.
void cyclicRefreshInit(RcControl& rc, CrRefreshCarrier& cr, const PcsCarrier& ppcs, int superBlockSize,
                       std::uint16_t sbTotalCount, std::uint32_t& crSbEnd, int hierarchicalLevels);

// compute_cr_deltaq (rc_vbr_cbr.c:978-985): static in the reference;
// exported for testability (the DF1 signedCharClamp precedent). Consumes
// the landed computeQdeltaByRate.
int computeCrDeltaq(const RcControl& rc, const CrRefreshCarrier& cr, int q, double rateRatioQdelta,
                    int bitDepth, bool scClass1);

// cyclic_refresh_compute_cr_qdeltas (rc_vbr_cbr.c:989-998): verbatim; the
// seg2 ratio clamps at kCrMaxRateTargetRatio.
void cyclicRefreshComputeCrQdeltas(const RcControl& rc, CrRefreshCarrier& cr, int baseQIdx, int bitDepth,
                                   bool scClass1);

// ---- RC2 Wave D2: the recode-loop driver (rc_vbr_cbr.c :1794-:1926) ----

// recode_loop_update_q (:1794-1926): verbatim; the enc_ctx->recode_loop /
// scs->static_config.max_bit_rate / enc_ctx->recode_tolerance walls thread
// as recodeLoop / maxBitRate / recodeTolerance; the caller-local
// out-params (loop/q/q_low/q_high/undershoot_seen/overshoot_seen/
// low_cr_seen) keep the reference's shapes. PARK (named): the
// capped_crf_reencode call (rc_crf_cqp.c, out of this file) is dropped
// at the AOM_Q + max_bit_rate + tl==0 branch - q passes through unchanged.
void recodeLoopUpdateQ(RcControl& rc, const RcConfig& rcCfg, PcsCarrier& ppcs, const CrRefreshCarrier& cr,
                       std::uint16_t b64TotalCount, int recodeLoop, std::int64_t maxBitRate,
                       int recodeTolerance, int bitDepth, int topIndex, int bottomIndex, int loopCount,
                       bool& loop, int& q, int& qLow, int& qHigh, int& undershootSeen, int& overshootSeen,
                       int& lowCrSeen);

// ROUND_POWER_OF_TWO (definitions.h:457): the rounding shift.
std::int64_t roundPowerOfTwo(std::int64_t value, int n);

}
