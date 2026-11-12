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
};

// PictureParentControlSet mirrors the one-pass target arithmetic consumes.
struct PcsCarrier {
    int frameType;  // frm_hdr.frame_type (FrameType, definitions.h:1585-1591)
    bool showableFrame;  // frm_hdr.showable_frame
    std::int64_t pictureNumber;  // picture_number
    int thisFrameTarget;         // this_frame_target
    int baseFrameTarget;         // base_frame_target
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

}
