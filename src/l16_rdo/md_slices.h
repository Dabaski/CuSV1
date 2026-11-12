// l16_rdo MD1 - the A4 mode-decision pure-arithmetic surface.
// Reference spans:
//   - the SSIM family: mode_decision.c:4238-4490 + enc_dec_process.c:638-671
//     (svt_aom_similarity + the cc1/cc2 constants)
//   - the hbd psy chain: ac_bias.c:103-144 (svt_psy_distortion_hbd) +
//     C_DEFAULT/picture_operators_c.c:176-262 (hadamard_highbd_col8_first_
//     pass/second_pass + svt_aom_highbd_hadamard_8x8_c)
//   - the MV/ME helpers: mode_decision.c:71-130 (is_lossless_segment,
//     check_mv_validity, is_interintra_allowed), :132-221 (me_block_offset,
//     is_me_data_present), :274-310 (have_newmv_in_inter_mode,
//     get_max_drl_index), :739-846 (mv_is_already_injected,
//     is_valid_unipred_ref, is_valid_bipred_ref)
// STRUCT-PARKING precedent (RE8A DeblockContext): the consumed
// ModeDecisionContext/pcs fields travel as carrier structs with the
// reference field names; bodies verbatim.
#pragma once

#include <cstdint>
#include <cstddef>

#include "rate_est.h"

namespace l16_rdo {

// ---------------------------------------------------------------------
// Carrier structs (the RE8A DeblockContext precedent): mirrors of the
// consumed ModeDecisionContext fields the MD1 helpers read.
// ---------------------------------------------------------------------

// RefResults (md_process.h:55-58): the per-(group, list, ref) filtering
// verdict the unipred/bipred validators consume.
struct RefResults {
    std::uint8_t doRef;  // md_process.h:57 do_ref
};

// RefPruningControls (md_process.h:202-211): the pruning controls the
// validators read. TOT_INTER_GROUP = 11 (md_process.h:65-80: PA_ME_GROUP
// .. INTER_COMP_GROUP); REF_LIST_MAX_DEPTH = 4 (EbSvtAv1Enc.h:35);
// MAX_NUM_OF_REF_PIC_LIST = 2 (definitions.h:2032).
#define TOT_INTER_GROUP 11
#define MAX_NUM_OF_REF_PIC_LIST 2
#define REF_LIST_MAX_DEPTH 4
// TOTAL_REFS_PER_FRAME (definitions.h:1377: ALTREF_FRAME - INTRA_FRAME + 1
// = 7 - 0 + 1); the mvIsAlreadyInjected unipred/bipred split (mode_decision.c
// :744 `rf[1] <= INTRA_FRAME` reduces to refType < TOTAL_REFS_PER_FRAME via
// av1_set_ref_frame, inter_prediction.h:487-496).
#define TOTAL_REFS_PER_FRAME 8
// MAX_SB64_PU_COUNT_NO_8X8 / MAX_SB64_PU_COUNT_WO_16X16 (me_sb_results.h:26-27).
#define MAX_SB64_PU_COUNT_NO_8X8 21
#define MAX_SB64_PU_COUNT_WO_16X16 5

struct RefPruningCtrls {
    std::uint8_t enabled;                            // :204
    std::uint32_t maxDevToBest[TOT_INTER_GROUP];     // :206
    std::uint8_t useTplInfoOffset;                   // :207
    std::uint8_t checkClosestMultiplier;             // :208
    std::uint8_t closestRefs[TOT_INTER_GROUP];       // :210
};

// The ref_filtering_res array shape (md_process.h:1136).
using RefFilteringRes = RefResults[TOT_INTER_GROUP][MAX_NUM_OF_REF_PIC_LIST][REF_LIST_MAX_DEPTH];

// ---------------------------------------------------------------------
// MD1a: the SSIM family.
// ---------------------------------------------------------------------

// svt_aom_similarity (enc_dec_process.c:645-671): the SSIM score from the
// five per-block integer sums; bd selects the cc1/cc2 constant pair.
double similarity(std::uint32_t sumS, std::uint32_t sumR, std::uint32_t sumSqS,
                  std::uint32_t sumSqR, std::uint32_t sumSxr, std::int32_t count,
                  std::uint32_t bd);

// svt_ssim_4x4_c (mode_decision.c:4238-4261) / svt_ssim_8x8_c (:4263-4289):
// the five-sum walk over the 4x4 / 8x8 window, then similarity(.., 16/64, 8).
double ssim4x4(const std::uint8_t* s, std::uint32_t sp, const std::uint8_t* r, std::uint32_t rp);
double ssim8x8(const std::uint8_t* s, std::uint32_t sp, const std::uint8_t* r, std::uint32_t rp);

// svt_ssim_4x4_hbd_c (mode_decision.c:4291-4314) / svt_ssim_8x8_hbd_c
// (:4316-4339): the uint16 twins; similarity(.., 16/64, 10).
double ssim4x4Hbd(const std::uint16_t* s, std::uint32_t sp, const std::uint16_t* r,
                  std::uint32_t rp);
double ssim8x8Hbd(const std::uint16_t* s, std::uint32_t sp, const std::uint16_t* r,
                  std::uint32_t rp);

// ssim_8x8_blocks (mode_decision.c:4341-4360) / ssim_4x4_blocks (:4362-4381):
// the 8-step / 4-step grid walk, CLIP3(0,1) per sample, averaged.
double ssim8x8Blocks(const std::uint8_t* s, std::uint32_t sp, const std::uint8_t* r,
                     std::uint32_t rp, std::uint32_t width, std::uint32_t height);
double ssim4x4Blocks(const std::uint8_t* s, std::uint32_t sp, const std::uint8_t* r,
                     std::uint32_t rp, std::uint32_t width, std::uint32_t height);
// ssim (mode_decision.c:4383-4390): 8-divisible -> ssim8x8Blocks, else
// ssim4x4Blocks.
double ssim(const std::uint8_t* s, std::uint32_t sp, const std::uint8_t* r, std::uint32_t rp,
            std::uint32_t width, std::uint32_t height);

// ssim_8x8_blocks_hbd (mode_decision.c:4392-4411) / ssim_4x4_blocks_hbd
// (:4413-4432) / ssim_hbd (:4434-4442): the uint16 twins.
double ssim8x8BlocksHbd(const std::uint16_t* s, std::uint32_t sp, const std::uint16_t* r,
                        std::uint32_t rp, std::uint32_t width, std::uint32_t height);
double ssim4x4BlocksHbd(const std::uint16_t* s, std::uint32_t sp, const std::uint16_t* r,
                        std::uint32_t rp, std::uint32_t width, std::uint32_t height);
double ssimHbd(const std::uint16_t* s, std::uint32_t sp, const std::uint16_t* r, std::uint32_t rp,
               std::uint32_t width, std::uint32_t height);

// svt_spatial_full_distortion_ssim_kernel (mode_decision.c:4444-4490): the
// SSIM spatial-distortion kernel; hbd selects the uint16 branch (m=8). The
// hbd branch's ac-bias join (:4473-4483) sits under
// #if CONFIG_ENABLE_HIGH_BIT_DEPTH - compiled out under the pinned build's
// CONFIG=0 (EbConfigMacros.h:55-57; the rate_est.cpp:2225 convention), so
// the hbd branch computes the ssim_hbd score only; the 8-bit branch joins
// psyDistortion (rate_est.h:939) when acBias is set (:4460-4464).
std::uint64_t spatialFullDistortionSsimKernel(const std::uint8_t* input, std::uint32_t inputOffset,
                                              std::uint32_t inputStride, const std::uint8_t* recon,
                                              std::int32_t reconOffset, std::uint32_t reconStride,
                                              std::uint32_t areaWidth, std::uint32_t areaHeight,
                                              bool hbd, double acBias);

// ---------------------------------------------------------------------
// MD1b: the hbd psy chain.
// ---------------------------------------------------------------------

// hadamard_highbd_col8_first_pass (picture_operators_c.c:176-203) /
// hadamard_highbd_col8_second_pass (:207-234).
void hadamardHighbdCol8FirstPass(const std::int16_t* srcDiff, std::ptrdiff_t srcStride,
                                 std::int16_t* coeff);
void hadamardHighbdCol8SecondPass(const std::int16_t* srcDiff, std::ptrdiff_t srcStride,
                                  std::int32_t* coeff);

// svt_aom_highbd_hadamard_8x8_c (picture_operators_c.c:238-262).
void highbdHadamard8x8(const std::int16_t* srcDiff, std::ptrdiff_t srcStride,
                       std::int32_t* coeff);

// svt_psy_distortion_hbd (ac_bias.c:103-144): the hbd "AC Bias" energy gap;
// the 8x8 branch walks highbdHadamard8x8, the small branch the regular
// hadamard4x4 (rate_est.h:932). DEVIATION, named: the span sits under
// #if CONFIG_ENABLE_HIGH_BIT_DEPTH (ac_bias.c:101) and the pinned build's
// CONFIG is 0, so the upstream callers (get_svt_psy_full_dist :155, the
// SSIM kernel :4475) compile it out - MD1 ports the body as a standalone
// helper (the RE4 hbd park resolves, the TF1->TF2 precedent).
std::uint64_t psyDistortionHbd(const std::uint16_t* input, std::uint32_t inputStride,
                               const std::uint16_t* recon, std::uint32_t reconStride,
                               std::uint32_t width, std::uint32_t height);

// ---------------------------------------------------------------------
// MD1c: the MV/ME helpers.
// ---------------------------------------------------------------------

// check_mv_validity (mode_decision.c:86-100): the shifted-MV range check
// (is_mv_valid, inter_prediction.h:246-248; MV_LOW/MV_UPP via
// rate_est.h:834-837).
bool checkMvValidity(std::int16_t xMv, std::int16_t yMv, std::uint8_t needShift);

// svt_is_interintra_allowed (mode_decision.c:102-106): the conjunction over
// the RE1-ported helpers (rate_est.h:291-293); the bsize helper's
// CONFIG_ENABLE_INTER_INTRA gate threads as interIntraCfg (the
// isInterIntraAllowedBsize enable parameter, rate_est.cpp:1024-1026; the
// pinned build's value is 0, EbConfigMacros.h:67-69 RTC block).
std::int32_t isInterintraAllowed(std::uint8_t enableInterIntra, std::uint8_t interIntraCfg,
                                 BlockSize bsize, PredictionMode mode, const std::int32_t refFrame[2]);

// svt_av1_is_lossless_segment (mode_decision.c:71-84). CONFIG_ENABLE_LOSSLESS
// threads as the first parameter (the RE-series deviation: the pinned build's
// CONFIG is threaded as a parameter; the pinned value is 0, EbConfigMacros.h
// :88-90 RTC block, which selects the :72-75 always-false branch). segEnabled
// is the frame header's segmentation_enabled (:78).
bool isLosslessSegment(std::uint8_t losslessCfg, const std::uint8_t* lossless,
                       std::uint8_t segEnabled, std::int8_t segmentId);

// svt_aom_get_me_block_offset (mode_decision.c:132-189): the ME PU offset
// from the block origin, over the me_idx_85 table (mode_decision.h:167-170)
// and the conversion tables (coefficients.c:291-310).
std::uint32_t getMeBlockOffset(std::uint32_t orgX, std::uint32_t orgY, BlockSize bsize,
                               std::uint8_t enableMe8x8, std::uint8_t enableMe16x16);

// The me_idx_85 table (mode_decision.h:167-170, verbatim) and the
// conversion tables (coefficients.c:291-310, verbatim).
extern const std::uint32_t meIdx85[85];
extern const std::uint8_t meIdx8x8To16x16Conversion[64];
extern const std::uint8_t meIdx16x16ToParent32x32Conversion[16];

// MeCandidate (me_sb_results.h:29-35) + MeSbResults (:38-46) as the
// carrier for is_me_data_present.
struct MeCandidate {
    std::uint8_t direction : 2;
    std::uint8_t refIdxL0 : 2;
    std::uint8_t refIdxL1 : 2;
    std::uint8_t ref0List : 1;
    std::uint8_t ref1List : 1;
};
struct MeSbResults {
    std::uint8_t* totalMeCandidateIndex;   // :40
    MeCandidate* meCandidateArray;         // :43
};

// svt_aom_is_me_data_present (mode_decision.c:193-212).
std::uint8_t isMeDataPresent(std::uint32_t meBlockOffset, std::uint32_t meCandOffset,
                             const MeSbResults* meResults, std::uint8_t listIdx,
                             std::uint8_t refIdx);

// svt_aom_have_newmv_in_inter_mode (mode_decision.c:274-277).
std::int32_t haveNewmvInInterMode(PredictionMode mode);

// svt_aom_get_max_drl_index (mode_decision.c:286-310).
std::uint8_t getMaxDrlIndex(std::uint8_t refmvCnt, PredictionMode mode);

// svt_aom_is_valid_unipred_ref (mode_decision.c:789-800): the pruning
// carrier threads (ctx, inter_cand_group, list_idx, ref_idx).
bool isValidUnipredRef(const RefPruningCtrls& pruning, const RefFilteringRes filtering,
                       std::uint8_t interCandGroup, std::uint8_t listIdx, std::uint8_t refIdx);

// is_valid_bipred_ref (mode_decision.c:820-846).
bool isValidBipredRef(const RefPruningCtrls& pruning, const RefFilteringRes filtering,
                      std::uint8_t interCandGroup, std::uint8_t listIdx0, std::uint8_t refIdx0,
                      std::uint8_t listIdx1, std::uint8_t refIdx1);

// mv_is_already_injected (mode_decision.c:739-787): the redundant-candidate
// carrier threads the injected-mv walk (injected_mv_count, injected_mvs,
// injected_ref_types) + the RedundantCandCtrls (md_process.h:675-678) +
// corrupted_mv_check. The Mv halves travel as their packed as_int forms
// (the RE3/RE4 precedent).
struct RedundantCandCtrls {
    std::int32_t scoreTh;  // md_process.h:676
    std::int32_t magTh;    // md_process.h:677
};
struct InjectedMvCarrier {
    std::int32_t injectedMvCount;              // :750 ctx->injected_mv_count
    std::int32_t injectedRefTypes[64];         // :751
    std::int32_t injectedMvs[64][2];           // :751/:767 packed as_int pairs
    std::uint8_t corruptedMvCheck;             // :746 ctx->corrupted_mv_check
    RedundantCandCtrls redundCtrl;             // :761
};
bool mvIsAlreadyInjected(const InjectedMvCarrier& carrier, std::int32_t mv0, std::int32_t mv1,
                         std::uint8_t refType);

}  // namespace l16_rdo
