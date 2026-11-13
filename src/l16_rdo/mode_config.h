// l16_rdo - EMC1: the A4 mode-config surface.
// Reference: third_party/SVT-AV1/Source/Lib/Codec/enc_mode_config.c
// (9,339 non-blank lines) - the encoder preset/control derivation surface.
// Struct-parking pattern (the RE8A DeblockContext precedent): the
// reference pokes PictureControlSet / PictureParentControlSet /
// MeContext / Av1Common / ModeDecisionContext / SequenceControlSet; the
// mirror carriers below hold EXACTLY the fields each ported span
// consumes, and the walls thread as parameters. No reference headers.
#pragma once

#include <cstdint>

namespace l16_rdo {
namespace mode_config {

// ---------------------------------------------------------------------------
// The shared enums (verbatim orders; the l7/l13/l16 duplication precedent -
// l16_rdo cannot link the other layers).
// ---------------------------------------------------------------------------

// EncMode (EbSvtAv1Enc.h:44-61, verbatim).
enum EncMode {
    ENC_MR = -1,  // Research mode with higher quality than M0
    ENC_M0 = 0,
    ENC_M1 = 1,
    ENC_M2 = 2,
    ENC_M3 = 3,
    ENC_M4 = 4,
    ENC_M5 = 5,
    ENC_M6 = 6,
    ENC_M7 = 7,
    ENC_M8 = 8,
    ENC_M9 = 9,
    ENC_M10 = 10,
    ENC_M11 = 11,
    ENC_M12 = 12,
    ENC_M13 = 13,
    MAX_ENC_PRESET = ENC_M13,
};

// ResolutionRange (definitions.h:1808-1817, verbatim).
enum ResolutionRange {
    INPUT_SIZE_240p_RANGE = 0,
    INPUT_SIZE_360p_RANGE = 1,
    INPUT_SIZE_480p_RANGE = 2,
    INPUT_SIZE_720p_RANGE = 3,
    INPUT_SIZE_1080p_RANGE = 4,
    INPUT_SIZE_4K_RANGE = 5,
    INPUT_SIZE_8K_RANGE = 6,
    INPUT_SIZE_COUNT = 7,
};

// InputCoeffLvl (definitions.h:262-267, verbatim; consumed as
// pcs->coeff_lvl == HIGH_LVL by get_dlf_level_default :1493).
enum InputCoeffLvl {
    VLOW_LVL = 0,
    LOW_LVL = 1,
    NORMAL_LVL = 2,
    HIGH_LVL = 3,
    INPUT_COEFF_LVLS,
};

// TransformationType (definitions.h:1740-1745, verbatim).
enum TransformationType {
    IDENTITY = 0,
    TRANSLATION = 1,
    ROTZOOM = 2,
    AFFINE = 3,
    TRANS_TYPES,
};

// GM_LEVEL (definitions.h:235-244, verbatim).
enum GmDownsampleLevel {
    GM_FULL = 0,
    GM_DOWN = 1,
    GM_DOWN16 = 2,
    GM_ADAPT_0 = 3,
    GM_ADAPT_1 = 4,
};

// CorrespondenceMethod (pcs.h:504-510, verbatim).
enum CorrespondenceMethod {
    MV_64x64 = 0,
    MV_32x32 = 1,
    MV_16x16 = 2,
    MV_8x8 = 3,
    CORNERS = 4,
};

// LpfPickMethod (deblocking_filter.h:24-33, verbatim).
enum LpfPickMethod {
    LPF_PICK_FROM_FULL_IMAGE,
    LPF_PICK_FROM_SUBIMAGE,
    LPF_PICK_FROM_Q,
    LPF_PICK_MINIMAL_LPF,
};

// ---------------------------------------------------------------------------
// Slice 1: the standalone arithmetic.
// ---------------------------------------------------------------------------

// MAX_PD0_LVL (enc_mode_config.c:20), MAX_INTRA_LEVEL (:22),
// MAX_TXS_LEVEL (:24), MAX_QP_VALUE (definitions.h:1646).
constexpr std::uint8_t kMaxPd0Lvl = 8;
constexpr std::uint8_t kMaxIntraLevel = 10;
constexpr std::uint8_t kMaxTxsLevel = 6;
constexpr std::uint32_t kMaxQpValue = 63;

// pf_gi (enc_mode_config.c:21, verbatim; consumed by set_cdef_search_
// controls :919-:1171 as the cdef primary/secondary filter-strength
// ladder). The reference declares it non-const static; the port pins it
// read-only (named deviation: the consumers only read).
extern const std::uint8_t pfGi[16];
// angular_pred_level (enc_mode_config.c:23, verbatim; consumed by
// set_intra_ctrls :6627+ and sig_deriv_pre_analysis_scs :2873).
extern const std::uint8_t angularPredLevel[kMaxIntraLevel];

// svt_aom_get_qp_based_th_scaling_factors (enc_mode_config.c:26-54).
void getQpBasedThScalingFactors(bool enableQpBasedThScaling, std::uint32_t* retQWeight,
                                std::uint32_t* retQWeightDenom, std::uint32_t qp);

// svt_aom_get_wn_filter_level_default (:1365-1381) / _rtc (:1383-1392) /
// _allintra (:1394-1407). input_resolution is the ResolutionRange ordinal.
std::uint8_t getWnFilterLevelDefault(EncMode encMode, std::uint8_t inputResolution, bool isNotLastLayer);
std::uint8_t getWnFilterLevelRtc(std::uint8_t inputResolution);
std::uint8_t getWnFilterLevelAllIntra(EncMode encMode);

// svt_aom_get_sg_filter_level_default (:1410-1426) / _rtc (:1428-1437) /
// _allintra (:1439-1448).
std::uint8_t getSgFilterLevelDefault(EncMode encMode, std::uint8_t inputResolution, std::uint8_t fastDecode);
std::uint8_t getSgFilterLevelRtc(std::uint8_t inputResolution, std::uint8_t fastDecode);
std::uint8_t getSgFilterLevelAllIntra(EncMode encMode);

// DlfPcs - the threaded PictureControlSet surface the dlf level family
// reads: ref_skip_percentage (pcs.h:381) + coeff_lvl (pcs.h:351).
struct DlfPcs {
    std::uint8_t refSkipPercentage;
    std::uint8_t coeffLvl;  // InputCoeffLvl ordinal
};

// dlf_level_modulation (enc_mode_config.c:1450-1472).
void dlfLevelModulation(const DlfPcs& pcs, std::uint8_t* defaultDlfLevel, std::uint8_t modulationMode);
// get_dlf_level_default (:1474-1523).
std::uint8_t getDlfLevelDefault(const DlfPcs& pcs, EncMode encMode, std::uint8_t isNotLastLayer,
                                std::uint8_t fastDecode, ResolutionRange resolution, int isBase);
// get_dlf_level_rtc (:1525-1546).
std::uint8_t getDlfLevelRtc(const DlfPcs& pcs, EncMode encMode, int isBase);
// get_dlf_level_allintra (:1548-1567).
std::uint8_t getDlfLevelAllIntra(EncMode encMode, std::uint8_t fastDecode, ResolutionRange resolution);

// ---------------------------------------------------------------------------
// set_gm_controls (:2523-2625): the GmControls mirror (pcs.h:512-552,
// verbatim field order and types) + the carrier.
// ---------------------------------------------------------------------------
struct GmControls {
    std::uint8_t enabled;                 // 0: disable GM, 1: enable GM
    std::uint8_t identiyExit;             // [sic] pcs.h:517
    std::uint8_t searchStartModel;        // TransformationType
    std::uint8_t searchEndModel;          // TransformationType
    std::uint8_t skipIdentity;
    std::uint8_t bypassBasedOnMe;
    std::uint8_t paramsRefinementSteps;
    std::uint8_t downsampleLevel;         // GM_LEVEL
    std::uint8_t corners;                 // 1:1/4 .. 4:all
    std::uint8_t chessRfn;
    std::uint8_t matchSz;                 // NxN window, must be odd
    bool injPsqGlb;                       // inject global only if parent SQ is global
    bool ppEnabled;                       // enable pre-processor for GM
    bool refIdx0Only;                     // limit the search to ref index 0
    std::uint8_t rfnEarlyExit;            // 0: off, 1: early exit
    std::uint8_t correspondenceMethod;    // CorrespondenceMethod
};

// The carrier: exactly the PictureParentControlSet fields the setter reads
// (pcs->gm_ctrls :2524 + pcs->input_resolution :2589/:2609).
struct GmPpcs {
    GmControls gmCtrls;
    std::uint8_t inputResolution;  // ResolutionRange ordinal
};

// The :2589-2591 correspondence fold, factored for testability (the
// cdefSign precedent): res <= 480p -> MV_8x8, res <= 1080p -> MV_16x16,
// else MV_32x32.
std::uint8_t getGmCorrespondence(std::uint8_t inputResolution);

// svt_aom_set_gm_controls (:2523-2625). NAMED DEVIATION: the reference
// const-folds CONFIG_ENABLE_GLOBAL_MOTION at build time; the pinned/
// generator build configures neither RTC_BUILD nor MINIMAL_BUILD
// (tools/golden_gen/extract.ps1:378-380), so the macro resolves to 1
// there (EbConfigMacros.h:182-183) and the 0 default belongs to the
// RTC+MINIMAL section (:61-63). The port threads the fork as the runtime
// parameter so both arms stay testable (the allowPalette precedent).
// The switch's default case assert(0)s in the reference and writes
// nothing; the port leaves the struct untouched (documented UB).
void setGmControls(GmPpcs& pcs, std::uint8_t gmLevel, int configEnableGlobalMotion);

// ---------------------------------------------------------------------------
// EMC1 slice 2, batch A: the ME/HME setter families
// (enc_mode_config.c:223-:897).
// Carriers: SearchArea/SearchAreaMinMax and the five ctrl structs mirror
// the MeContext members (me_context.h) verbatim; MeSearchParams holds
// exactly the MeContext fields the batch spans write; MeModeCfg folds the
// ppcs/scs wall surface (each field annotated with its reference path -
// the folded two-struct wall is a named deviation: the reference passes
// separate SequenceControlSet/PictureParentControlSet pointers, the port
// folds them into one carrier because the field sets are disjoint).
// ---------------------------------------------------------------------------
// SearchArea (me_context.h:306-309) + SearchAreaMinMax (:311-314).
struct SearchArea {
    std::uint16_t width;
    std::uint16_t height;
};
struct SearchAreaMinMax {
    SearchArea saMin;
    SearchArea saMax;
};
// SEARCH_REGION_COUNT (me_context.h:304).
constexpr int kSearchRegionCount = 2;

// MeHmeRefPruneCtrls (me_context.h:262-272, verbatim field order/types).
struct MeHmeRefPruneCtrls {
    bool enableMeHmeRefPruning;
    std::uint16_t pruneRefIfHmeSadDevBiggerThanTh;
    std::uint16_t pruneRefIfMeSadDevBiggerThanTh;
    std::uint32_t zzSadTh;
    std::uint16_t zzSadPct;
    std::uint32_t phmeSadTh;
    std::uint16_t phmeSadPct;
};

// MeSrCtrls (me_context.h:274-287, verbatim).
struct MeSrCtrls {
    std::uint8_t enableMeSrAdjustment;
    std::uint16_t reduceMeSrBasedOnMvLengthTh;
    std::uint16_t stationaryHmeSadAbsTh;
    std::uint16_t stationaryMeSrDivisor;
    std::uint16_t reduceMeSrBasedOnHmeSadAbsTh;
    std::uint16_t meSrDivisorForLowHmeSad;
    std::uint8_t distanceBasedHmeResizing;
};

// Me8x8VarCtrls (me_context.h:293-302, verbatim).
struct Me8x8VarCtrls {
    std::uint8_t enabled;
    std::uint32_t meSrDiv4Th;
    std::uint32_t meSrDiv2Th;
    std::uint32_t meSrMult2Th;
};

// MvBasedSearchAdj (me_context.h:339-347, verbatim).
struct MvBasedSearchAdj {
    bool enabled;
    bool nearestRefOnly;
    std::uint16_t mvSizeTh;
    std::uint16_t saMultiplier;
};

// PreHmeCtrls (me_context.h:323-328, verbatim minus the SearchInfo
// arrays the family never touches).
struct PreHmeCtrls {
    std::uint8_t enable;
    SearchAreaMinMax prehmeSaCfg[kSearchRegionCount];
    std::uint8_t skipSearchLine;
    std::uint8_t l1EarlyExit;
};

// The MeContext surface the batch-A spans write (me_context.h spans
// cited per field: num_hme_sa_w/h :404 (uint16_t), the search-area
// members, the five ctrl structs, and the scalars :388/:466-:483).
struct MeSearchParams {
    SearchAreaMinMax hmeL0Sa;
    SearchArea hmeL1Sa;
    SearchArea hmeL2Sa;
    SearchAreaMinMax hmeL0SaDefaultTf;
    SearchAreaMinMax meSa;
    std::uint16_t numHmeSaW;
    std::uint16_t numHmeSaH;
    MeHmeRefPruneCtrls meHmePruneCtrls;
    MvBasedSearchAdj mvBasedSaAdj;
    MeSrCtrls meSrAdjustmentCtrls;
    Me8x8VarCtrls me8x8VarCtrls;
    PreHmeCtrls prehmeCtrl;
    bool enableHmeFlag;
    bool enableHmeLevel0Flag;
    bool enableHmeLevel1Flag;
    bool enableHmeLevel2Flag;
    std::uint8_t hmeSearchMethod;
    std::uint8_t meSearchMethod;
    std::uint8_t reduceHmeL0SrThMin;
    std::uint8_t reduceHmeL0SrThMax;
    std::int32_t pruneMeCandidatesTh;
    std::uint8_t scClassMeBoost;
    std::uint8_t useBestUnipredCandOnly;
    std::uint32_t meEarlyExitTh;
    std::uint32_t meStaticB64Th;
    std::uint32_t meSafeLimitZzTh;
    std::uint32_t prevMeStageBasedExitTh;
};

// The folded ppcs/scs wall surface (named deviation, see the batch
// header): each field cites its reference path.
struct MeModeCfg {
    EncMode encMode;                     // pcs->enc_mode
    bool rtcTune;                        // scs->static_config.rtc
    std::uint32_t qp;                    // scs->static_config.qp
    bool hmeQpBasedThScaling;            // scs->qp_based_th_scaling_ctrls.hme_qp_based_th_scaling
    bool meQpBasedThScaling;             // .../me_qp_based_th_scaling
    bool tfMeQpBasedThScaling;           // .../tf_me_qp_based_th_scaling
    std::uint8_t hierarchicalLevels;     // pcs->hierarchical_levels
    bool scClass5;                       // pcs->sc_class5 (uint8_t, truthiness)
    bool enableHmeFlag;                  // pcs->enable_hme_flag
    bool enableHmeLevel0Flag;            // pcs->enable_hme_level0_flag
    bool enableHmeLevel1Flag;            // pcs->enable_hme_level1_flag
    bool enableHmeLevel2Flag;            // pcs->enable_hme_level2_flag
    std::uint8_t useBestMeUnipredCandOnly;  // pcs->use_best_me_unipred_cand_only
    bool frameIsBoosted;                 // frame_is_boosted(pcs) - the pcs wall
    ResolutionRange inputResolution;     // scs->input_resolution
    std::uint8_t mrpSafeLimitNref;       // scs->mrp_ctrls.safe_limit_nref
    std::uint32_t mrpSafeLimitZzTh;      // scs->mrp_ctrls.safe_limit_zz_th
    std::uint8_t tfHmeMeLevel;           // pcs->tf_ctrls.hme_me_level
    std::uint8_t tfQpOpt;                // pcs->tf_ctrls.qp_opt (uint8_t, truthiness)
    bool tfEnableHmeFlag;                // pcs->tf_enable_hme_flag
    bool tfEnableHmeLevel0Flag;          // pcs->tf_enable_hme_level0_flag
    bool tfEnableHmeLevel1Flag;          // pcs->tf_enable_hme_level1_flag
    bool tfEnableHmeLevel2Flag;          // pcs->tf_enable_hme_level2_flag
};

// set_hme_search_params (enc_mode_config.c:223-283). The rtc/qp/scaling
// reads are the SCS surface (:224/:263-266 via pcs->scs), the enc-mode
// reads the PCS surface (:225/:276).
void setHmeSearchParams(const MeModeCfg& scs, const MeModeCfg& pcs, MeSearchParams& meCtx,
                        ResolutionRange inputResolution);
// set_me_search_params (:288-351). enc_mode + hierarchical_levels read
// from PCS (:290/:295), rtc + the qp-scaling pair from SCS (:291/:344-346).
void setMeSearchParams(const MeModeCfg& scs, const MeModeCfg& pcs, MeSearchParams& meCtx,
                       ResolutionRange inputResolution);
// svt_aom_set_me_hme_ref_prune_ctrls (:353-434).
void setMeHmeRefPruneCtrls(MeSearchParams& meCtx, std::uint8_t pruneLevel);
// svt_aom_set_mv_based_sa_ctrls (:436-459).
void setMvBasedSaCtrls(MeSearchParams& meCtx, std::uint8_t mvSaAdjLevel);
// svt_aom_set_me_sr_adjustment_ctrls (:461-532); the enable_hme_level
// post-processing reads meCtx's hme-level flags.
void setMeSrAdjustmentCtrls(MeSearchParams& meCtx, std::uint8_t srAdjustmentLevel);
// svt_aom_set_me_8x8_var_ctrls (:534-556).
void setMe8x8VarCtrls(MeSearchParams& meCtx, std::uint8_t level);
// svt_aom_set_prehme_ctrls (:559-614).
void setPrehmeCtrls(MeSearchParams& meCtx, std::uint8_t level);
// tf_set_me_hme_params_oq (:619-701).
void tfSetMeHmeParamsOq(MeSearchParams& meCtx, const MeModeCfg& pcs);
// svt_aom_sig_deriv_me (:708-849): the scs/pcs walls both fold into
// MeModeCfg (the same carrier serves both parameters - the field sets
// are disjoint).
void sigDerivMe(const MeModeCfg& scs, const MeModeCfg& pcs, MeSearchParams& meCtx);
// svt_aom_sig_deriv_me_tf (:856-897).
void sigDerivMeTf(const MeModeCfg& pcs, MeSearchParams& meCtx);

// ---------------------------------------------------------------------------
// EMC1 slice 2, batch B: the cdef search/recon setter family
// (enc_mode_config.c:899-1240).
// ---------------------------------------------------------------------------
// TOTAL_STRENGTHS = CDEF_PRI_STRENGTHS 16 * CDEF_SEC_STRENGTHS 4
// (cdef.h:22-23, pcs.h:50).
constexpr int kCdefTotalStrengths = 64;

// CdefSearchControls (pcs.h:557-592) minus pred_y_f/pred_uv_f - the two
// fields the span (:899-1206) neither reads nor writes (the carrier holds
// exactly the consumed surface; named exclusion).
struct CdefSearchCtrls {
    std::uint8_t enabled;
    std::uint8_t firstPassFsNum;
    std::uint8_t defaultFirstPassFs[kCdefTotalStrengths];
    std::uint8_t defaultSecondPassFsNum;
    std::uint8_t defaultSecondPassFs[kCdefTotalStrengths];
    std::int8_t defaultFirstPassFsUv[kCdefTotalStrengths];
    std::int8_t defaultSecondPassFsUv[kCdefTotalStrengths];
    std::int8_t useReferenceCdefFs;
    std::uint8_t subsamplingFactor;
    std::uint8_t searchBestRefFs;
    std::uint8_t skipTh;
    bool uvFromY;
    bool useQpStrength;
};

// set_cdef_search_controls (:899-1206). isBase = frame_is_boosted(pcs)
// (:901), isNotHighestLayer = !frame_is_leaf(pcs) (:902) - both threaded
// as parameters (the RE8A wall-threading precedent). The switch's default
// case assert(0)s in the reference and writes nothing; the port leaves
// the struct untouched (documented UB).
void setCdefSearchCtrls(CdefSearchCtrls& ctrls, std::uint8_t cdefSearchLevel, bool isBase,
                        bool isNotHighestLayer);

// CdefReconControls (pcs.h:594-602, verbatim).
struct CdefReconCtrls {
    std::uint16_t zeroFsCostBias;
    std::uint8_t zeroFilterStrengthLvl;
    std::uint16_t prevCdefDistTh;
};

// set_cdef_recon_controls (:1208-1240). The default case assert(0)s in
// the reference and writes nothing; the port leaves the struct untouched
// (documented UB).
void setCdefReconCtrls(CdefReconCtrls& ctrls, std::uint8_t cdefReconLevel);

// DlfCtrls (pcs.h:604-625, verbatim field order/types; pick_method is an
// LpfPickMethod value).
struct DlfCtrls {
    std::uint8_t enabled;
    std::uint8_t sbBasedDlf;
    std::uint8_t pickMethod;
    bool dlfAvg;
    bool useRefAvgY;
    bool useRefAvgUv;
    std::uint8_t earlyExitConvergence;
    std::uint8_t zeroFilterStrengthLvl;
    std::uint16_t prevDlfDistTh;
};

// svt_aom_set_dlf_controls (:1569-1665). The default case assert(0)s in
// the reference and writes nothing; the port leaves the struct untouched
// (documented UB).
void setDlfCtrls(DlfCtrls& ctrls, std::uint8_t dlfLevel);

// MAX_MESH_STEP (pcs.h:123) + MeshPattern (pcs.h:125-128, verbatim).
constexpr int kMaxMeshStep = 4;
struct MeshPattern {
    int range;
    int interval;
};

// IntrabcCtrls (pcs.h:627-649, verbatim field order/types).
struct IntrabcCtrls {
    bool enabled;
    bool paletteHint;
    bool nsqParentGating;
    bool b4ParentGating;
    std::uint8_t maxBlockSizeHash;   // 0 disables hash search
    std::uint16_t maxCandPerBucket;
    std::uint64_t exhaustiveMeshThresh;  // ~0 = always allow mesh search
    std::int32_t meshSearchMvDiffThreshold;
    MeshPattern meshPatterns[kMaxMeshStep];
    bool meshQpScaling;
    std::uint8_t searchDir;  // 0 = Left + Top, 1 = Top only
};

// MAX_INTRABC_LEVEL (enc_mode_config.c:1670).
constexpr std::uint8_t kMaxIntrabcLevel = 7;

// set_intrabc_level (:1672-1851). Case MAX_INTRABC_LEVEL is the literal
// case 7 (:1827). Levels 6-7 write everything EXCEPT the mesh surface
// (mv_diff threshold / patterns / qp scaling). The default case
// assert(0)s in the reference and writes nothing; the port leaves the
// struct untouched (documented UB).
void setIntrabcLevel(IntrabcCtrls& ctrls, std::uint8_t ibcLevel);

// PaletteCtrls (pcs.h:651-657, verbatim field order/types).
struct PaletteCtrls {
    std::uint8_t enabled;
    std::uint8_t dominantColorStep;
    std::uint8_t kmeanColorStep;
    bool centroidRefinement;
    std::uint8_t kMeansMaxItr;
};

// set_palette_level (:1856-1930). dominant_color_step = (uint8_t)~0 at
// levels 3-9. The default case assert(0)s in the reference and writes
// nothing; the port leaves the struct untouched (documented UB).
void setPaletteLevel(PaletteCtrls& ctrls, std::uint8_t paletteLevel);

// WnFilterCtrls (av1_common.h:295-313, verbatim field order/types).
struct WnFilterCtrls {
    bool enabled;
    std::uint8_t filterTapLvl;  // 1: Y-7tap/UV-5tap, 2: Y-5tap/UV-5tap, 3: Y-3tap/UV-3tap
    bool useRefinement;
    bool maxOneRefinementStep;
    bool usePrevFrameCoeffs;
    bool useChroma;
};

// svt_aom_set_wn_filter_ctrls (:1242-1301). The Av1Common wall folds into
// the carrier. The default case assert(0)s in the reference and writes
// nothing (documented UB).
void setWnFilterCtrls(WnFilterCtrls& ctrls, std::uint8_t wnFilterLvl);

// SgFilterCtrls (av1_common.h:44-62, verbatim; PLANE_TYPES = 2,
// definitions.h:685).
constexpr int kPlaneTypes = 2;
struct SgFilterCtrls {
    bool enabled;
    std::int8_t startEp[kPlaneTypes];
    std::int8_t endEp[kPlaneTypes];
    std::int8_t epInc[kPlaneTypes];
    std::int8_t refine[kPlaneTypes];
    bool useChroma;
};

// svt_aom_set_sg_filter_ctrls (:1303-1362). The default case assert(0)s
// in the reference and writes nothing (documented UB).
void setSgFilterCtrls(SgFilterCtrls& ctrls, std::uint8_t sgFilterLvl);

// ---------------------------------------------------------------------------
// Batch B: the small helper family.
// ---------------------------------------------------------------------------

// svt_aom_get_gm_core_level (:181-199). The CONFIG_ENABLE_GLOBAL_MOTION
// fork is threaded as the runtime parameter (the setGmControls
// precedent): configEnableGlobalMotion = 0 -> always 0.
std::uint8_t getGmCoreLevel(EncMode encMode, bool superResOff, int configEnableGlobalMotion);

// svt_aom_derive_gm_level (:201-215). The pcs wall folds into the two
// consumed scalars (enc_mode, slice_type == I_SLICE).
std::uint8_t deriveGmLevel(EncMode encMode, bool isISlice, bool superResOff,
                           int configEnableGlobalMotion);

// svt_aom_derive_input_resolution (sequence_control_set.c:120-139) with
// the definitions.h:1819-1824 thresholds. NAMED DEVIATION: the EbErrorType
// return (always EB_ErrorNone) is dropped.
ResolutionRange deriveInputResolution(std::uint32_t inputSize);

// svt_aom_get_max_can_count (:1936-1981).
std::uint16_t getMaxCanCount(EncMode encMode, bool rtc);

// ---------------------------------------------------------------------------
// Batch B: the sig_deriv_multi_processes family (:1988-2518) - the three
// per-tune compositions default/rtc/allintra. The pcs/scs/frm_hdr/cm walls
// fold into two carriers; every field annotated with its reference path.
// ---------------------------------------------------------------------------
// MULTI_PASS_PD_ON (definitions.h:2062).
constexpr std::uint8_t kMultiPassPdOn = 1;
// RTC_LOW_VARIANCE_TH / RTC_ULTRA_LOW_VARIANCE_TH (enc_mode_config.c:17-18).
constexpr std::uint32_t kRtcLowVarianceTh = 1000;
constexpr std::uint32_t kRtcUltraLowVarianceTh = 350;
// DEFAULT (EbSvtAv1Enc.h:63).
constexpr std::int32_t kDefaultSentinel = -1;

// The ppcs surface: the consumed wall inputs + the written outputs. The
// three derived walls are threaded as bools annotated with their
// derivation (the MeModeCfg frameIsBoosted precedent):
//   isBase            = frame_is_boosted(ppcs) (enc_mode_config.h:108-110,
//                       the kf/gf/arf update_type wall)
//   isNotHighestLayer = !frame_is_leaf(ppcs) (:902, the cdef setter's
//                       internal wall - NOT the same as isNotLastLayer)
//   isNotLastLayer    = !pcs->is_highest_layer (:1996, default variant,
//                       feeds wn/sg only)
struct MultiProcPpcs {
    GmPpcs gm;  // gm.gmCtrls + gm.inputResolution - the :2524 setter target
    EncMode encMode;                     // pcs->enc_mode
    bool isISlice;                       // pcs->slice_type == I_SLICE
    bool isBase;                         // frame_is_boosted(ppcs)
    bool scClass5;                       // pcs->sc_class5
    bool isNotLastLayer;                 // !pcs->is_highest_layer (:1996)
    bool useFlatIpp;                     // pcs->hierarchical_levels == 0 (:2196, rtc)
    bool isNotHighestLayer;              // !frame_is_leaf(ppcs) (:902)
    std::uint32_t picAvgVariance;        // pcs->pic_avg_variance (:2205/:2302, rtc)
    std::uint8_t tfHmeMeLevel;           // pcs->tf_ctrls.hme_me_level (:2018/:2217)
    // writes:
    bool enableHmeFlag;
    bool enableHmeLevel0Flag;
    bool enableHmeLevel1Flag;
    bool enableHmeLevel2Flag;
    bool tfEnableHmeFlag;
    bool tfEnableHmeLevel0Flag;
    bool tfEnableHmeLevel1Flag;
    bool tfEnableHmeLevel2Flag;
    std::uint8_t multiPassPdLevel;       // = MULTI_PASS_PD_ON (:2046/:2245/:2376)
    IntrabcCtrls intrabcCtrls;           // pcs->intrabc_ctrls
    std::uint8_t paletteLevel;           // pcs->palette_level
    PaletteCtrls paletteCtrls;           // pcs->palette_ctrls
    CdefSearchCtrls cdefSearchCtrls;     // pcs->cdef_search_ctrls
    CdefReconCtrls cdefReconCtrls;       // pcs->cdef_recon_ctrls
    std::uint8_t cdefLevel;              // pcs->cdef_level (:2115/:2328/:2462)
    bool allowIntrabc;                   // frm_hdr->allow_intrabc (:2070/:2250/:2403)
    bool allowScreenContentTools;        // frm_hdr->allow_screen_content_tools (:2095/:2284/:2425)
    WnFilterCtrls wnFilterCtrls;         // cm->wn_filter_ctrls
    SgFilterCtrls sgFilterCtrls;         // cm->sg_filter_ctrls
    bool enableRestoration;              // pcs->enable_restoration (:2157/:2352/:2502)
    std::uint8_t frameEndCdfUpdateMode;  // (:2162/:2357/:2507)
    std::uint8_t hbdMd;                  // pcs->hbd_md (:2168/:2360/:2510)
    std::uint16_t maxCanCount;           // pcs->max_can_count (:2182/:2365/:2517)
    std::uint8_t useBestMeUnipredCandOnly;  // (:2184/:2366; NOT written by allintra)
};

// The scs surface (DEFAULT = -1, EbSvtAv1Enc.h:63; SVT_EFFECTIVE_BIT_DEPTH
// (bd) = (bd) in the pinned build, EbConfigMacros.h:293).
struct MultiProcScs {
    std::uint8_t fastDecode;             // static_config.fast_decode
    bool enableIntrabc;                  // static_config.enable_intrabc
    std::int32_t cdefLevel;              // static_config.cdef_level
    std::uint8_t seqCdefLevel;           // seq_header.cdef_level (truthiness)
    bool seqEnableRestoration;           // seq_header.enable_restoration
    std::uint32_t maxInitialInputLumaWidth;
    std::uint32_t maxInitialInputLumaHeight;
    std::int32_t hbdMds;                 // static_config.hbd_mds (:2169)
    std::uint8_t encoderBitDepth;        // scs->encoder_bit_depth (:2167)
    std::int32_t enableHbdModeDecision;  // scs->enable_hbd_mode_decision (:2359/:2508)
};

// svt_aom_sig_deriv_multi_processes_default (:1988-2188).
void sigDerivMultiProcessesDefault(MultiProcPpcs& pcs, const MultiProcScs& scs,
                                   int configEnableGlobalMotion);
// svt_aom_sig_deriv_multi_processes_rtc (:2190-2367). The FTR_RTC_INTER_
// PALETTE fork (EbDebugMacros.h:37 = 1 in the pinned build) is threaded
// as the runtime parameter (the CONFIG_ENABLE_GLOBAL_MOTION precedent).
void sigDerivMultiProcessesRtc(MultiProcPpcs& pcs, const MultiProcScs& scs,
                               int configEnableGlobalMotion, int ftrRtcInterPalette);
// svt_aom_sig_deriv_multi_processes_allintra (:2369-2518).
void sigDerivMultiProcessesAllIntra(MultiProcPpcs& pcs, const MultiProcScs& scs);

// ---------------------------------------------------------------------------
// Batch B rung 4: set_inter_comp_controls (:2627-2709), the level getters,
// the sg/restoration enable wrappers (:2711-2781), the pre-analysis
// derivations (:2788-2942).
// ---------------------------------------------------------------------------
// The MD_COMP_TYPE tail (definitions.h:1264-1269): AVG=0, DIST=1, DIFF0=2,
// WEDGE=3, TYPES=4.
constexpr std::uint8_t kMdCompDist = 1;
constexpr std::uint8_t kMdCompTypes = 4;

// InterCompCtrls (mode_decision.h, verbatim field order/types).
struct InterCompCtrls {
    std::uint8_t totCompTypes;
    bool doMe;
    bool doPme;
    bool doNearestNearest;
    bool doNearNear;
    bool doNearestNearNew;
    bool do3x3Bi;
    bool doGlobal;
    std::uint8_t pred0ToPred1Mult;
    std::uint16_t maxMvLength;
    bool skipOnRefInfo;
    bool useRate;
    bool noSymDist;
};

// set_inter_comp_controls (:2627-2709). The ModeDecisionContext wall folds
// into the carrier. The default case assert(0)s and writes nothing
// (documented UB).
void setInterCompCtrls(InterCompCtrls& ctrls, std::uint8_t interCompMode);

// get_inter_compound_level (:8839-8850).
std::uint8_t getInterCompoundLevel(EncMode encMode);
// get_filter_intra_level_default (:8852-8863) / _rtc (:8865-8869) /
// _allintra (:8871-8882).
std::uint8_t getFilterIntraLevelDefault(EncMode encMode);
std::uint8_t getFilterIntraLevelRtc();
std::uint8_t getFilterIntraLevelAllIntra(EncMode encMode);
// svt_aom_get_inter_intra_level (:8884-8894).
std::uint8_t getInterIntraLevel(EncMode encMode, std::uint8_t transitionPresent);
// svt_aom_get_intra_mode_levels_allintra (:5429-5453).
void getIntraModeLevelsAllIntra(EncMode encMode, std::uint32_t* intraLevel,
                                std::uint32_t* distBasedAngIntraLevel);
// svt_aom_get_enable_me_8x8 (:146-173) / _16x16 (:175-179).
std::uint8_t getEnableMe8x8(EncMode encMode, std::uint8_t inputResolution, bool rtcTune);
std::uint8_t getEnableMe16x16(EncMode encMode);

// svt_aom_get_enable_sg_{default,rtc,allintra} (:2711-2727).
std::uint8_t getEnableSgDefault(EncMode encMode, std::uint8_t inputResolution,
                                std::uint8_t fastDecode);
std::uint8_t getEnableSgRtc(std::uint8_t inputResolution, std::uint8_t fastDecode);
std::uint8_t getEnableSgAllIntra(EncMode encMode);
// svt_aom_get_enable_restoration_{default,rtc,allintra} (:2733-2781).
// configEnableRestoration is the DEFAULT-threaded config value.
std::uint8_t getEnableRestorationDefault(EncMode encMode, std::int8_t configEnableRestoration,
                                         std::uint8_t inputResolution, std::uint8_t fastDecode);
std::uint8_t getEnableRestorationRtc(std::int8_t configEnableRestoration,
                                     std::uint8_t inputResolution, std::uint8_t fastDecode);
std::uint8_t getEnableRestorationAllIntra(EncMode encMode, std::int8_t configEnableRestoration);

// The pcs surface of svt_aom_sig_deriv_pre_analysis_pcs (:2788-2811).
struct PreAnalysisPcs {
    EncMode encMode;                       // pcs->enc_mode
    std::uint32_t maxInputLumaWidth;       // scs->max_input_luma_width
    std::uint32_t maxInputLumaHeight;      // scs->max_input_luma_height
    bool rtcTune;                          // scs->static_config.rtc
    // writes:
    bool enableMe16x16;
    bool enableMe8x8;
    bool enableHmeFlag;
    bool enableHmeLevel0Flag;
    bool enableHmeLevel1Flag;
    bool enableHmeLevel2Flag;
    bool tfEnableHmeFlag;
    bool tfEnableHmeLevel0Flag;
    bool tfEnableHmeLevel1Flag;
    bool tfEnableHmeLevel2Flag;
};

// svt_aom_sig_deriv_pre_analysis_pcs (:2788-2811).
void sigDerivPreAnalysisPcs(PreAnalysisPcs& pcs);

// The five CONFIG forks of svt_aom_sig_deriv_pre_analysis_scs (:2818-2918)
// threaded as runtime values (the CONFIG_ENABLE_GLOBAL_MOTION precedent;
// the pinned build resolves ALL FIVE to 1 - EbConfigMacros.h:178-208).
struct PreAnalysisCfg {
    int superres;       // CONFIG_ENABLE_SUPERRES
    int interIntra;     // CONFIG_ENABLE_INTER_INTRA
    int filterIntra;    // CONFIG_ENABLE_FILTER_INTRA
    int interCompound;  // CONFIG_ENABLE_INTER_COMPOUND
    int restoration;    // CONFIG_ENABLE_RESTORATION
};

// The scs surface of svt_aom_sig_deriv_pre_analysis_scs (:2818-2918).
struct PreAnalysisScs {
    bool allintra;                          // scs->allintra
    bool rtcTune;                           // static_config.rtc
    std::uint8_t fastDecode;                // static_config.fast_decode
    std::int32_t superresMode;              // static_config.superres_mode (SUPERRES_NONE = 0)
    std::int32_t enableRestorationFiltering;  // static_config.enable_restoration_filtering
    std::int32_t cdefLevel;                 // static_config.cdef_level
    std::uint32_t maxInitialInputLumaWidth;
    std::uint32_t maxInitialInputLumaHeight;
    // writes (the seq_header surface):
    bool seqEnableSuperres;
    bool seqEnableInterintraCompound;
    std::uint8_t seqFilterIntraLevel;
    std::uint8_t seqEnableJntComp;
    std::uint8_t seqEnableMaskedCompound;
    std::uint8_t seqEnableIntraEdgeFilter;
    std::uint8_t seqEnableRestoration;
    std::uint8_t seqCdefLevel;
    bool seqEnableWarpedMotion;
};

// svt_aom_sig_deriv_pre_analysis_scs (:2818-2918).
void sigDerivPreAnalysisScs(PreAnalysisScs& scs, std::int8_t encMode, const PreAnalysisCfg& cfg);

// The pcs surface of svt_aom_is_ref_same_size (:2925-2941). The
// EbReferenceObject wall folds into per-slot present/width/height arrays
// (the [2][8] bound is the test's enumeration bound; the reference reads
// only [list][ref_idx] for ref_idx < ref_list{0,1}_count_try).
struct RefSameSizePcs {
    bool isBSlice;                        // pcs->slice_type == B_SLICE
    std::uint8_t refListCountTry[2];      // ppcs->ref_list{0,1}_count_try
    bool refPresent[2][8];                // ref_pic_ptr_array[list][idx] != NULL
    std::uint32_t refPicWidth[2][8];      // ref_obj->reference_picture->width
    std::uint32_t refPicHeight[2][8];     // ...->height
    std::uint32_t frameWidth;             // ppcs->frame_width
    std::uint32_t frameHeight;            // ppcs->frame_height
};

// svt_aom_is_ref_same_size (:2925-2941).
bool isRefSameSize(const RefSameSizePcs& pcs, std::uint8_t listIdx, std::uint8_t refIdx);

// ObmcControls (mode_decision.h, verbatim field order/types).
struct ObmcCtrls {
    std::uint8_t enabled;
    std::uint8_t maxBlkSizeToRefine;
    std::uint8_t maxBlkSize;
    std::uint8_t refineLevel;
    std::uint8_t transFaceOff;
    std::uint8_t fpelSearchRange;
    std::uint8_t fpelSearchDiag;
};

// set_obmc_controls (:2943-3007). The ModeDecisionContext wall folds into
// the carrier. NOTE: the default arm is NOT assert(0) - it writes
// enabled = 0 (:3003-3005).
void setObmcCtrls(ObmcCtrls& ctrls, std::uint8_t obmcMode);

// dimensions_require_8x8 (:3012-3028). Assumes max block 64x64 (ISLICEs
// never use 128x128); not NSQ aware.
bool dimensionsRequire8x8(std::uint16_t alignedWidth, std::uint16_t alignedHeight);

// ---------------------------------------------------------------------------
// Batch C rung 1: set_depth_removal_level_controls (:3030-3343) and its
// get_sb128_me_data dependency (:63-115).
// ---------------------------------------------------------------------------
// DepthRemovalCtrls (md_process.h:213-223, verbatim field order/types).
struct DepthRemovalCtrls {
    std::uint8_t enabled;
    std::uint8_t disallowBelow64x64;  // remove 32x32 and below f(me_dist, var)
    std::uint8_t disallowBelow32x32;  // remove 16x16 and below f(me_dist, var)
    std::uint8_t disallowBelow16x16;  // remove 8x8 and below f(me_dist, var)
    std::uint8_t disallow4x4;
};

// LOW_8x8_DIST_VAR_TH / HIGH_8x8_DIST_VAR_TH (enc_mode_config.c:10-11).
constexpr std::uint32_t kLow8x8DistVarTh = 25000;
constexpr std::uint32_t kHigh8x8DistVarTh = 50000;
// MAX_SIGNED_VALUE (utility.h:165, ((signed)(~0u >> 1))).
constexpr std::int32_t kMaxSignedValue = 2147483647;

// The :3068-3224 switch, factored for testability (the getGmCorrespondence
// precedent): the per-level locals the switch folds. enabledWritten is
// false ONLY for level > 15 - the reference switch has NO default, so
// enabled keeps the struct's PRIOR state there (a documented prior-state
// read, as in the reference); the locals keep the pre-switch init values.
struct DepthRemovalLevelParams {
    bool enabledWritten;
    std::uint8_t enabled;
    std::uint64_t disallow4x4CostThMultiplier;
    std::uint64_t disallowBelow16x16CostThMultiplier;
    std::uint64_t disallowBelow32x32CostThMultiplier;
    std::uint64_t disallowBelow64x64CostThMultiplier;
    std::int64_t dev16x16To8x8Th;
    std::int64_t dev32x32To16x16Th;
    std::int8_t qpScaleFactor;
};

// The switch fold (:3068-3224).
void getDepthRemovalLevelParams(std::uint8_t level, DepthRemovalLevelParams& params);

// The pcs/ppcs/ref-object wall surface (the folded-wall carrier is the
// MeModeCfg precedent; each field cites its reference path). The ME
// arrays carry BOTH paths: the SB64 path reads [sbIndex] (:3265-3269),
// the SB128 path reads [me_sb_addr .. me_sb_addr + pw + 1] through
// get_sb128_me_data (:63-115) - the test supplies arrays of
// me_pic_width_in_sb * me_pic_height_in_sb entries.
struct DepthRemovalPcs {
    bool isISlice;                    // pcs->slice_type == I_SLICE
    std::uint64_t pictureNumber;      // pcs->picture_number
    bool rtc;                         // pcs->scs->static_config.rtc
    std::uint32_t superBlockSize;     // pcs->scs->super_block_size
    std::uint8_t b64Size;             // pcs->scs->b64_size (SB128 path)
    std::uint32_t alignedWidth;       // pcs->ppcs->aligned_width (SB128 path)
    std::uint32_t alignedHeight;      // pcs->ppcs->aligned_height
    std::uint8_t sbGeomWidth;         // pcs->ppcs->sb_geom[sbIndex].width
    std::uint8_t sbGeomHeight;        // ... .height
    bool modulateByQp;                // :3055 - frm_hdr.delta_q_params.delta_q_present
                                      //   || ppcs->r0_delta_qp_md, OR-folded
    std::uint32_t pictureQp;          // pcs->ppcs->picture_qp (:3056/:3275/:3287)
    RefSameSizePcs ref;               // the svt_aom_is_ref_same_size wall (embedded)
    std::uint64_t refPocL0;           // ref_obj_l0->ref_poc (:3240)
    std::uint64_t refPocL1;           // ref_obj_l1->ref_poc (:3248)
    std::uint8_t refSbMinSqSizeL0;    // ref_obj_l0->sb_min_sq_size[sbIndex] (:3241)
    std::uint8_t refSbMinSqSizeL1;    // ref_obj_l1->sb_min_sq_size[sbIndex] (:3249)
    const std::uint32_t* me64x64Distortion;  // pcs.h:867
    const std::uint32_t* me32x32Distortion;  // pcs.h:868
    const std::uint32_t* me16x16Distortion;  // pcs.h:869
    const std::uint32_t* me8x8Distortion;    // pcs.h:870
    const std::uint32_t* me8x8CostVariance;  // pcs.h:866
};

// The ctx surface (the ModeDecisionContext fields the span consumes).
struct DepthRemovalCtx {
    std::uint32_t fastLambdaMd[2];    // ctx->fast_lambda_md (EB_8_BIT_MD = 0)
    std::uint16_t sbIndex;            // ctx->sb_index (md_process.h:1179)
    std::uint32_t sbOriginX;          // ctx->sb_origin_x (get_sb128_me_data)
    std::uint32_t sbOriginY;          // ctx->sb_origin_y
    std::uint8_t sbQindex;            // ctx->sb_ptr->qindex (coding_unit.h:270)
    std::uint8_t disallow4x4;         // ctx->disallow_4x4 (read + write :3333-3339)
};

// set_depth_removal_level_controls (:3030-3343), 1:1. NOTE: the
// disallow_below_* fields are READ (the || term :3320/:3325/:3330) -
// the caller supplies live prior state, as the reference context does.
void setDepthRemovalLevelCtrls(DepthRemovalCtrls& ctrls, const DepthRemovalPcs& pcs,
                               DepthRemovalCtx& ctx, std::uint8_t depthRemovalLevel);


// ---------------------------------------------------------------------------
// Batch C rung 2: the MD search setters (:3348-3631).
// ---------------------------------------------------------------------------
// DistortionType (definitions.h:871, verbatim).
enum DistortionType {
    SAD = 0,
    VAR = 1,
    SSD = 2,
    DIST_TYPES = 3,
};

// TxCoeffShape (definitions.h:2044-2050, verbatim).
enum TxCoeffShape {
    DEFAULT_SHAPE = 0,
    N2_SHAPE = 1,
    N4_SHAPE = 2,
    ONLY_DC_SHAPE = 3,
};

// MIN_SIGNED_VALUE (utility.h:164, ~0 - ((signed)(~0u >> 1)) = INT32_MIN).
constexpr std::int32_t kMinSignedValue = -2147483647 - 1;

// MdNsqMotionSearchCtrls (md_process.h:291-302, verbatim field order/types).
struct MdNsqMotionSearchCtrls {
    std::uint8_t enabled;                // 0: NSQ ME @ MD OFF; 1: ON
    std::uint8_t distType;               // DistortionType
    std::uint8_t fullPelSearchWidth;
    std::uint8_t fullPelSearchHeight;
    std::uint8_t enablePsad;
};

// md_nsq_motion_search_controls (:3348-3373). The default case assert(0)s
// in the reference and writes nothing; the port leaves the struct
// untouched (documented UB).
void setMdNsqMotionSearchCtrls(MdNsqMotionSearchCtrls& ctrls, std::uint8_t mdNsqMvSearchLevel);

// MdPmeCtrls (md_process.h:354-382, verbatim field order/types).
struct MdPmeCtrls {
    std::uint8_t enabled;                // 0: PME @ MD OFF; 1: ON
    std::uint8_t distType;               // DistortionType
    std::int32_t earlyCheckMvThMultiplier;
    std::uint8_t fullPelSearchWidth;
    std::uint8_t fullPelSearchHeight;
    std::int32_t preFpPmeToMeCostTh;
    std::int32_t preFpPmeToMeMvTh;
    std::int32_t postFpPmeToMeCostTh;
    std::int32_t postFpPmeToMeMvTh;
    std::uint8_t enablePsad;
    std::uint8_t saQWeight;
};

// svt_aom_md_pme_search_controls (:3375-3451). The default case assert(0)s
// in the reference and writes nothing (documented UB).
void setMdPmeCtrls(MdPmeCtrls& ctrls, std::uint8_t mdPmeLevel);

// SubresCtrls (md_process.h:278-285, verbatim).
struct SubresCtrls {
    std::uint8_t step;                   // residual sub-sampling step (0: OFF)
    std::uint8_t oddToEvenDeviationTh;
};

// set_subres_controls (:3453-3476). NOTE: the post-switch adjustment reads
// step (:3471) - for level > 2 the switch wrote nothing, so step is the
// struct's PRIOR state (a documented prior-state read, as in the
// reference) and oddToEvenDeviationTh is written from it either way.
void setSubresCtrls(SubresCtrls& ctrls, std::uint8_t subresLevel);

// PfCtrls (md_process.h:287-289, verbatim).
struct PfCtrls {
    std::uint8_t pfShape;                // TxCoeffShape
};

// set_pf_controls (:3478-3503). The #if !CONFIG_ENABLE_TX_PF_N2 assert
// (:3498-3502) is COMPILED OUT in the pinned build (EbConfigMacros.h
// resolves CONFIG_ENABLE_TX_PF_N2 = 1; the pinned build is neither
// RTC_BUILD nor MINIMAL_BUILD) - named, nothing to thread. The default
// case assert(0)s in the reference and writes nothing (documented UB).
void setPfCtrls(PfCtrls& ctrls, std::uint8_t pfLevel);

// MdSqMotionSearchCtrls (md_process.h:304-352, verbatim field order/types).
struct MdSqMotionSearchCtrls {
    std::uint8_t enabled;                // 0: SQ ME @ MD OFF; 1: ON
    std::uint8_t distType;               // DistortionType
    std::uint16_t pameDistortionTh;
    std::uint8_t sprsLev0Enabled;
    std::uint8_t sprsLev0Step;
    std::uint16_t sprsLev0W;
    std::uint16_t sprsLev0H;
    std::uint16_t maxSprsLev0W;
    std::uint16_t maxSprsLev0H;
    std::int16_t sprsLev0Multiplier;
    std::uint8_t sprsLev1Enabled;
    std::uint8_t sprsLev1Step;
    std::uint16_t sprsLev1W;
    std::uint16_t sprsLev1H;
    std::uint16_t maxSprsLev1W;
    std::uint16_t maxSprsLev1H;
    std::int16_t sprsLev1Multiplier;
    std::uint8_t sprsLev2Enabled;
    std::uint8_t sprsLev2Step;
    std::uint16_t sprsLev2W;
    std::uint16_t sprsLev2H;
    std::uint8_t enablePsad;
};

// md_sq_motion_search_controls (:3508-3628). The default case assert(0)s
// in the reference and writes nothing (documented UB).
void setMdSqMotionSearchCtrls(MdSqMotionSearchCtrls& ctrls, std::uint8_t mdSqMvSearchLevel);


// ---------------------------------------------------------------------------
// Batch C rung 3: the subpel/rdoq/shaving setters (:3633-3962).
// ---------------------------------------------------------------------------
// SUBPEL_SEARCH_TYPE (definitions.h:829-834, verbatim).
enum SubpelSearchType {
    USE_2_TAPS_ORIG = 0,  // used in temporal filtering
    USE_2_TAPS = 1,
    USE_4_TAPS = 2,
    USE_8_TAPS = 3,
};
// SUBPEL_SEARCH_METHODS (definitions.h:836-840, verbatim).
enum SubpelSearchMethods {
    SUBPEL_TREE = 0,
    SUBPEL_TREE_PRUNED = 1,  // prunes 1/2-pel searches
    SUBPEL_FIXED_STAGE_SEARCH = 2,
};
// SUBPEL_FORCE_STOP (definitions.h:842, verbatim).
enum SubpelForceStop {
    EIGHTH_PEL = 0,
    QUARTER_PEL = 1,
    HALF_PEL = 2,
    FULL_PEL = 3,
};

// MdSubPelSearchCtrls (md_process.h:384-418, verbatim field order/types).
struct MdSubPelSearchCtrls {
    std::uint8_t enabled;                // 0: OFF, 1: ON
    std::uint8_t subpelSearchType;       // SUBPEL_SEARCH_TYPE
    std::uint8_t maxPrecision;           // SUBPEL_FORCE_STOP
    std::uint8_t subpelSearchMethod;     // SUBPEL_SEARCH_METHODS
    std::int32_t subpelItersPerStep;
    std::int32_t predVarianceTh;
    std::uint8_t absThMult;
    std::int32_t roundDevTh;
    std::uint8_t skipDiagRefinement;
    std::uint8_t minBlkSz;
    std::uint8_t mvpTh;
    std::int32_t hpMvTh;
    std::int32_t biasFp;
};

// md_subpel_me_controls (:3633-3774). Cases 7-10 write the
// SUBPEL_FIXED_STAGE_SEARCH surface ONLY (the reference's own comment
// :3730-3732: subpel_search_type / subpel_iters_per_step / round_dev_th /
// skip_diag_refinement / mvp_th / hp_mv_th are unused there and stay
// untouched). The default case assert(0)s and writes nothing (documented
// UB).
void setMdSubpelMeCtrls(MdSubPelSearchCtrls& ctrls, std::uint8_t mdSubpelMeLevel);
// md_subpel_pme_controls (:3779-3846). The default case assert(0)s and
// writes nothing (documented UB).
void setMdSubpelPmeCtrls(MdSubPelSearchCtrls& ctrls, std::uint8_t mdSubpelPmeLevel);

// RdoqCtrls (md_process.h:429-441, verbatim field order/types).
struct RdoqCtrls {
    std::uint8_t enabled;
    std::uint16_t cutOffNum;
    std::uint16_t cutOffDenum;
    std::uint8_t skipUv;                 // 1: RDOQ luma only
    std::uint8_t dctDctOnly;             // 1: RDOQ DCT_DCT only
    std::uint8_t eobTh;
    std::uint8_t eobFastTh;
};

// set_rdoq_controls (:3851-3906). NOTE: case 1 does NOT write
// cut_off_denum (the reference leaves it); the whole-struct test pins the
// poison surviving there. The default case assert(0)s and writes nothing
// (documented UB).
void setRdoqCtrls(RdoqCtrls& ctrls, std::uint8_t rdoqLevel);

// CoeffShavingCtrls (md_process.h:443-448, verbatim field order/types).
struct CoeffShavingCtrls {
    std::uint8_t enabled;
    std::int32_t levelThreshold;         // max abs(quantized_level) eligible
    std::int32_t zeroGapThreshold;       // min zero-gap to justify removal
    std::int32_t rdZeroStrength;         // RD-zero aggressiveness [0..10]
};

// set_coeff_shaving_controls (:3911-3935). The default case assert(0)s
// and writes nothing (documented UB).
void setCoeffShavingCtrls(CoeffShavingCtrls& ctrls, std::uint8_t coeffShavingLevel);


// ---------------------------------------------------------------------------
// Batch C rung 4: svt_aom_set_txt_controls (:3964-4137) +
// set_interpolation_search_level_ctrls (:4139-4162).
// ---------------------------------------------------------------------------
// MAX_TX_TYPE_GROUP (definitions.h:1055).
constexpr std::uint8_t kMaxTxTypeGroup = 6;

// TxtControls (md_process.h:140-170, verbatim field order/types).
struct TxtCtrls {
    std::uint8_t enabled;
    std::uint8_t txtGroupInterLt16x16;
    std::uint8_t txtGroupInterGtEq16x16;
    std::uint8_t txtGroupIntraLt16x16;
    std::uint8_t txtGroupIntraGtEq16x16;
    std::uint32_t earlyExitDistTh;
    std::uint32_t earlyExitCoeffTh;
    std::uint16_t satdEarlyExitThIntra;
    std::uint16_t satdEarlyExitThInter;
    std::uint16_t txtRateCostTh;
    std::uint16_t satdThQWeight;
};

// svt_aom_set_txt_controls (:3964-4137). The default case assert(0)s and
// writes nothing (documented UB).
void setTxtCtrls(TxtCtrls& ctrls, std::uint8_t txtLevel);

// IfsLevel (definitions.h:858-864, verbatim).
enum IfsLevel {
    IFS_OFF = 0,
    IFS_MDS0 = 1,
    IFS_MDS1 = 2,
    IFS_MDS2 = 3,
    IFS_MDS3 = 4,
};

// InterpolationSearchCtrls (md_process.h:660-665, verbatim).
struct InterpolationSearchCtrls {
    std::uint8_t level;                  // IfsLevel
};

// set_interpolation_search_level_ctrls (:4139-4162). The default case
// assert(0)s and writes nothing (documented UB).
void setInterpolationSearchLevelCtrls(InterpolationSearchCtrls& ctrls,
                                      std::uint8_t interpolationSearchLevel);


// ---------------------------------------------------------------------------
// Batch C rung 5: set_cand_reduction_ctrls (:4164-4394).
// ---------------------------------------------------------------------------
// Pd1Level (definitions.h:748-763, verbatim; REGULAR_PD1 is negative so
// that LPD1 can start at 0).
enum Pd1Level {
    REGULAR_PD1 = -1,
    LPD1_LVL_0 = 0,
    LPD1_LVL_1 = 1,
    LPD1_LVL_2 = 2,
    LPD1_LVL_3 = 3,
    LPD1_LVL_4 = 4,
    LPD1_LVL_5 = 5,
    LPD1_LVL_6 = 6,
    LPD1_LEVELS = 7,
};

// RedundantCandCtrls (md_process.h:675-678, verbatim).
struct RedundantCandCtrls {
    std::int32_t scoreTh;
    std::int32_t magTh;
};
// NearCountCtrls (md_process.h:181-188, verbatim).
struct NearCountCtrls {
    std::uint8_t enabled;
    std::uint8_t nearCount;
    std::uint8_t nearNearCount;
};
// UseNeighbouringModeCtrls (md_process.h:680-682, verbatim).
struct UseNeighbouringModeCtrls {
    std::uint8_t enabled;
};
// CandEliminationCtlrs (md_process.h:523-533, verbatim - enabled is
// uint32_t here, NOT uint8_t).
struct CandEliminationCtlrs {
    std::uint32_t enabled;
    std::uint16_t dcOnlyTh;
    std::uint16_t skipDcTh;
};
// CandReductionCtrls (md_process.h:812-821, verbatim field order).
struct CandReductionCtrls {
    RedundantCandCtrls redundantCandCtrls;
    NearCountCtrls nearCountCtrls;
    std::uint8_t lpd1MvpBestMeList;
    UseNeighbouringModeCtrls useNeighbouringModeCtrls;
    CandEliminationCtlrs candEliminationCtrls;
    std::uint8_t reduceUnipredCandidates;
    std::uint8_t reduceFilterIntra;
};

// The pcs wall surface (:4390-4391): the tail gate that zeroes
// lpd1_mvp_best_me_list.
struct CandReductionPcs {
    std::uint8_t refList0CountTry;       // ppcs->ref_list0_count_try
    std::uint8_t refList1CountTry;       // ppcs->ref_list1_count_try
    bool useBestMeUnipredCandOnly;       // ppcs->use_best_me_unipred_cand_only
};

// The ctx wall surface (:4169-4171). isNotLastLayer = !frame_is_leaf
// (ppcs) threaded (the batch-B wall precedent); rtc +
// hierarchicalLevels are the use_flat_ipp fold's raw inputs (:4171).
struct CandReductionCtx {
    std::int8_t pd1Level;                // ctx->lpd1_ctrls.pd1_level
    bool isNotLastLayer;                 // !frame_is_leaf(pcs->ppcs)
    bool rtc;                            // pcs->scs->static_config.rtc
    std::uint8_t hierarchicalLevels;     // pcs->ppcs->hierarchical_levels
};

// set_cand_reduction_ctrls (:4164-4394), 1:1. The level switch writes
// 0-8 of the eleven leaf fields (level-dependent); the tail gate
// (:4389-4393) runs on EVERY level including 0 and the default arm.
void setCandReductionCtrls(CandReductionCtrls& ctrls, std::uint8_t candReductionLevel,
                           const CandReductionPcs& pcs, const CandReductionCtx& ctx,
                           std::uint32_t pictureQp, std::uint32_t me8x8CostVariance,
                           std::uint32_t me64x64Distortion, std::uint8_t l0WasSkip,
                           std::uint8_t l1WasSkip, std::uint8_t refSkipPerc);

// ---------------------------------------------------------------------------
// EMC2 rung 1: the unported :63-:145 SB128 walls + the :3937-:3963 gap
// (set_sq_txs_ctrls, a set_nsq_search_ctrls dependency) + the first
// :4396+ setters.
// ---------------------------------------------------------------------------
// ME_TIER_ZERO_PU_64x64 (me_context.h:47) = 0.
constexpr std::uint32_t kMeTierZeroPu64x64 = 0;

// The pcs wall of get_sb128_me_data (:63-115): the b64 grid + the five ME
// arrays (pcs.h:866-870; the DepthRemovalPcs precedent - the caller
// supplies arrays of me_pic_width_in_sb * me_pic_height_in_sb entries).
struct Sb128Pcs {
    std::uint32_t b64Size;  // pcs->scs->b64_size
    std::uint32_t alignedWidth;
    std::uint32_t alignedHeight;
    const std::uint32_t* me64x64Distortion;
    const std::uint32_t* me32x32Distortion;
    const std::uint32_t* me16x16Distortion;
    const std::uint32_t* me8x8Distortion;
    const std::uint32_t* me8x8CostVariance;
};
// The ctx wall (:67-:68): sb_origin_x/sb_origin_y (md_process.h:1180-1181).
struct Sb128Ctx {
    std::uint32_t sbOriginX;
    std::uint32_t sbOriginY;
};

// get_sb128_me_data (enc_mode_config.c:63-115): average the 2x2 (max) of
// b64 cells covering the SB; dist = sum/count, cost-var = elementwise MAX.
void getSb128MeData(const Sb128Pcs& pcs, const Sb128Ctx& ctx, std::uint32_t* me64x64Dist,
                    std::uint32_t* me32x32Dist, std::uint32_t* me16x16Dist, std::uint32_t* me8x8Dist,
                    std::uint32_t* me8x8CostVar);

// The pcs wall of get_sb128_variance (:120-145). variance is ppcs->
// variance[b64_index][tier]; the carrier takes the flattened tier column
// (the address of [0][ME_TIER_ZERO_PU_64x64]) + the tier-count row stride.
struct Sb128VarPcs {
    std::uint32_t b64Size;
    std::uint32_t alignedWidth;
    std::uint32_t alignedHeight;
    const std::uint16_t* variance64;  // &variance[0][kMeTierZeroPu64x64]
    std::uint32_t varianceStride;     // the tier count (row stride)
};

// get_sb128_variance (enc_mode_config.c:120-145): sum the 2x2 (max-count)
// b64 variances and divide by the count. uint16 wrap-cast on the sum, as
// in the reference.
void getSb128Variance(const Sb128VarPcs& pcs, const Sb128Ctx& ctx, std::uint16_t* variance);

// NsqPsqTxsCtrls (md_process.h:420-427, verbatim).
struct NsqPsqTxsCtrls {
    std::uint8_t enabled;
    std::uint32_t hvToSqTh;
    std::uint32_t hToVTh;
};

// set_sq_txs_ctrls (enc_mode_config.c:3937-3963). The default case
// assert(0)s and writes nothing (documented UB).
void setSqTxsCtrls(NsqPsqTxsCtrls& ctrls, std::uint8_t psqTxsLvl);

// CHROMA_MODE_* (definitions.h:2053-2055).
constexpr std::uint8_t kChromaMode0 = 0;
constexpr std::uint8_t kChromaMode1 = 1;
constexpr std::uint8_t kChromaMode2 = 2;

// UvCtrls (md_process.h:810-825, verbatim field order/types).
struct UvCtrls {
    std::uint8_t enabled;
    std::uint8_t uvMode;  // CHROMA_MODE_0/1/2
    std::uint8_t indUvLastMds;
    std::uint8_t skipIndUvIfOnlyDc;
    std::uint8_t uvNicScalingNum;
    std::uint32_t interVsIntraCostTh;
};

// svt_aom_set_chroma_controls (:4396-4463). The ctx NULL arm (the
// memory-allocation call shape) is threaded out - the port always has the
// carrier, and the returned uv_mode is the ctx->uv_ctrls.uv_mode write.
// The default case assert(0)s in the reference and writes nothing but the
// trailing uv_mode write still happens there (uv_mode keeps its 0 init);
// the port returns the same 0 (documented).
std::uint8_t setChromaCtrls(UvCtrls& ctrls, std::uint8_t uvLevel);

// WmCtrls (md_process.h:591-606, verbatim field order/types).
struct WmCtrls {
    std::uint8_t enabled;
    std::uint8_t useWmForMvp;
    std::uint8_t refinementIterations;
    std::uint8_t refineDiag;
    std::uint8_t refineLevel;  // 0: before MDS0, 1: at MDS1, 2: at MDS3
    std::uint16_t lowerBandTh;
    std::uint16_t upperBandTh;
    bool shutApproxIfNotMds0;
};

// MAX_WARP_LVL (enc_mode_config.c:4465).
constexpr std::uint8_t kMaxWarpLvl = 4;

// svt_aom_set_wm_controls (:4467-4521). The default case assert(0)s and
// writes nothing (documented UB).
void setWmCtrls(WmCtrls& ctrls, std::uint8_t wmLevel);

// svt_aom_get_nic_level_default (:4521-4545) / _rtc (:4547-4557) /
// _allintra (:4559-4586).
std::uint8_t getNicLevelDefault(EncMode encMode, std::uint8_t isBase);
std::uint8_t getNicLevelRtc(EncMode encMode);
std::uint8_t getNicLevelAllIntra(EncMode encMode);

// MdStage / MdStagingMode (definitions.h:775-782, verbatim ordinals).
constexpr std::uint8_t kMdStage1 = 1;
constexpr std::uint8_t kMdStage2 = 2;
constexpr std::uint8_t kMdStage3 = 3;
constexpr std::uint8_t kMdStagingMode0 = 0;
constexpr std::uint8_t kMdStagingMode1 = 1;

// MD_STAGE_NICS_SCAL_NUM (definitions.h:798-817, verbatim; 16 levels x
// [S0,S1,S2,S3]; the port drops the always-unread S0 column).
extern const std::uint8_t kMdStageNicsScalNum[16][4];

// NicScalingCtrls (md_process.h:450-457, verbatim).
struct NicScalingCtrls {
    std::uint8_t stage1ScalingNum;  // <x>/16 post-stage 0 NICS
    std::uint8_t stage2ScalingNum;  // <x>/16 post-stage 1 NICS
    std::uint8_t stage3ScalingNum;  // <x>/16 post-stage 2 NICS
};

// NicPruningCtrls (md_process.h:459-514, verbatim field order/types).
struct NicPruningCtrls {
    std::uint64_t mds1ClassTh;
    std::uint8_t mds1BandCnt;
    std::uint64_t mds2ClassTh;
    std::uint8_t mds2BandCnt;
    std::uint64_t mds3ClassTh;
    std::uint8_t iMds3ClassThMult;
    std::uint8_t mds3BandCnt;
    std::uint64_t mds1CandBaseThIntra;
    std::uint64_t mds1CandBaseThInter;
    std::uint16_t mds1CandThRankFactor;
    std::uint64_t mds2CandBaseTh;
    std::uint16_t mds2CandThRankFactor;
    std::uint16_t mds2RelativeDevTh;
    std::uint64_t mds3CandBaseTh;
    bool enableSkippingMds1;
    std::uint8_t mergeInterCandsMult;
};

// NicCtrls (md_process.h:516-521, verbatim).
struct NicCtrls {
    NicPruningCtrls pruningCtrls;
    NicScalingCtrls scalingCtrls;
    std::uint8_t mdStagingMode;
};

// svt_aom_set_nic_controls (:4588-4966), 1:1. ctx may be NULL (the
// memory-allocation call shape - the reference's own contract); the
// return is the nic_scaling_level index into MD_STAGE_NICS_SCAL_NUM.
// The default case assert(0)s and writes nothing (documented UB).
std::uint8_t setNicCtrls(NicCtrls* ctrls, std::uint8_t nicLevel);

// ---------------------------------------------------------------------------
// EMC2 rung 2: set_nsq_geom_ctrls (:4968-:5022) + set_nsq_search_ctrls
// (:5024-:5375).
// ---------------------------------------------------------------------------
// NsqGeomCtrls (md_process.h:535-544, verbatim field order/types).
struct NsqGeomCtrls {
    bool enabled;
    std::uint8_t minNsqBlockSize;  // e.g. 8 = 8x8, 16 = 16x16
    std::uint8_t allowHv4;         // disallow H4/V4 when off
    std::uint8_t allowHvaHvb;      // disallow HA/HB/VA/VB when off
};

// svt_aom_set_nsq_geom_ctrls (:4968-5022), 1:1. The reference builds a
// zero-initialized LOCAL, switches, then writes the three out-params (if
// non-NULL) and memcpy's the local into ctx. The out-params keep the
// reference's nullable contract; the ctx NULL arm is threaded out (the
// memcpy always runs). The default case assert(0)s in the reference and
// writes the zero-init local through (documented).
void setNsqGeomCtrls(NsqGeomCtrls& ctrls, std::uint8_t nsqGeomLevel, std::uint8_t* allowHvaHvb,
                     std::uint8_t* allowHv4, std::uint8_t* minNsqBsize);

// PdPass (definitions.h:736-739, verbatim ordinals).
constexpr std::uint8_t kPdPass0 = 0;
constexpr std::uint8_t kPdPass1 = 1;

// NsqSearchCtrls (md_process.h:546-584, verbatim field order/types).
struct NsqSearchCtrls {
    bool enabled;
    std::uint32_t sqWeight;               // (uint32_t)~0 = off
    std::uint32_t hvWeight;               // only active when sqWeight on
    std::uint32_t maxPart0ToPart1Dev;
    std::uint32_t nsqSplitCostTh;         // percentage * 10
    std::uint32_t lowerDepthSplitCostTh;  // percentage * 100
    std::uint32_t hVsVSplitRateTh;
    std::uint32_t nonHvSplitRateTh;
    std::uint32_t rateThOffsetLte16;
    std::uint32_t componentMultipleTh;
    std::uint8_t psqTxsLvl;
    std::uint8_t subDepthBlockLvl;
};

// The pcs/scs wall surface of set_nsq_search_ctrls (:5024-:5375). The
// SB64 path reads ppcs->me_8x8_distortion / me_8x8_cost_variance
// [ctx->sb_index]; the SB128 path calls get_sb128_me_data on the
// embedded Sb128Pcs surface. Each field cites its reference path.
struct NsqSearchPcs {
    bool isISlice;                   // pcs->slice_type == I_SLICE
    EncMode encMode;                 // pcs->enc_mode
    bool mimicOnlyTx4x4;             // pcs->mimic_only_tx_4x4
    std::uint32_t sbSize;            // pcs->scs->super_block_size
    std::uint32_t sbIndex;           // ctx->sb_index (SB64 path)
    const std::uint32_t* me8x8Distortion;    // ppcs-> (SB64 path)
    const std::uint32_t* me8x8CostVariance;  // ppcs-> (SB64 path)
    Sb128Pcs sb128;                  // the SB128 gather surface
    std::uint32_t sbOriginX;         // ctx->sb_origin_x (SB128 gather)
    std::uint32_t sbOriginY;         // ctx->sb_origin_y
    std::uint32_t qp;                // scs->static_config.qp
    bool nsqQpBasedThScaling;        // scs->qp_based_th_scaling_ctrls.
                                     //   nsq_qp_based_th_scaling
    std::uint8_t pdPass;             // ctx->pd_pass (PdPass ordinal)
};

// set_nsq_search_ctrls (:5024-5375), 1:1: the mimic/me-dist level
// modulation (:5029-:5053), the 20-row switch (:5055-:5336), the QP
// scaling tail (:5338-:5349), the PD_PASS_0 overwrite (:5351-:5362) and
// the set_sq_txs_ctrls chaining (:5364). The default case assert(0)s
// and writes nothing, but the QP tail still runs on the untouched
// fields (as in the reference).
void setNsqSearchCtrls(NsqSearchCtrls& ctrls, NsqPsqTxsCtrls& psqTxsCtrls, const NsqSearchPcs& pcs,
                       std::uint8_t nsqSearchLevel);

// ---------------------------------------------------------------------------
// EMC2 rung 3: the intra-mode level getters (:5377-:5427),
// set_inter_intra_ctrls (:5455-:5481), set_pd0_ctrls (:5483-:5601) and
// set_lpd1_ctrls (:5603-:5937).
// ---------------------------------------------------------------------------

// svt_aom_get_intra_mode_levels_default (:5377-5404) / _rtc
// (:5406-5427). (get_intra_mode_levels_allintra landed in EMC1.)
void getIntraModeLevelsDefault(EncMode encMode, bool isISlice, bool isBase, int transitionPresent,
                               std::uint32_t* intraLevel, std::uint32_t* distBasedAngIntraLevel);
void getIntraModeLevelsRtc(EncMode encMode, bool isISlice, int transitionPresent, bool useFlatIpp,
                           std::uint32_t* intraLevel, std::uint32_t* distBasedAngIntraLevel);

// InterIntraCompCtrls (md_process.h:81-86, verbatim field order/types).
struct InterIntraCompCtrls {
    std::uint8_t enabled;
    std::uint8_t useRdModel;   // 1: curvefit model, 0: SSE
    std::uint8_t wedgeModeSq;  // 0: no wedge, 1: inj. wedge II, 2: only if better
    std::uint8_t wedgeModeNsq;
};

// set_inter_intra_ctrls (:5455-5481). The default case assert(0)s and
// writes nothing (documented UB).
void setInterIntraCtrls(InterIntraCompCtrls& ctrls, std::uint8_t interIntraLevel);

// Pd0Ctrls (md_process.h:691-703, verbatim field order/types).
constexpr int kPd0Levels = 7;  // PD0_LEVELS (definitions.h:749)
struct Pd0Ctrls {
    std::uint8_t pd0Level;  // Pd0Level
    bool usePd0Detector[kPd0Levels];
    std::uint8_t useRefInfo[kPd0Levels];
    std::uint32_t me8x8CostVarianceTh[kPd0Levels];
    std::uint32_t edgeDistTh[kPd0Levels];
    std::uint16_t neighMeDistShift[kPd0Levels];
};

// set_pd0_ctrls (:5483-5601). hbdMd is ctx->hbd_md (SVT_EFFECTIVE_HBD_MD
// is the identity in the pinned build, EbConfigMacros.h:295): the hbd
// early-out forces PD0_LVL_0 and returns (:5487-5490). The default case
// assert(0)s and writes nothing (documented UB).
void setPd0Ctrls(Pd0Ctrls& ctrls, std::uint8_t lpd0Lvl, std::uint8_t hbdMd);

// Lpd1Ctrls (md_process.h:706-728, verbatim field order/types).
constexpr int kLpd1Levels = 7;  // LPD1_LEVELS (definitions.h:762)
struct Lpd1Ctrls {
    std::uint8_t pd1Level;  // Pd1Level
    bool useLpd1Detector[kLpd1Levels];
    std::uint8_t useRefInfo[kLpd1Levels];
    std::uint32_t costThDist[kLpd1Levels];
    std::uint32_t costThRate[kLpd1Levels];
    std::uint32_t nzCoeffTh[kLpd1Levels];
    std::uint16_t maxMvLength[kLpd1Levels];
    std::uint32_t me8x8CostVarianceTh[kLpd1Levels];
    std::uint32_t skipPd0EdgeDistTh[kLpd1Levels];
    std::uint16_t skipPd0MeShift[kLpd1Levels];
};

// set_lpd1_ctrls (:5603-5937). Case N (1-8) writes pd1_level =
// LPD1_LVL_{N-1} and fills detector rows [0..N-1]; unwritten rows keep
// the struct's prior state (as in the reference). The default case
// assert(0)s and writes nothing (documented UB).
void setLpd1Ctrls(Lpd1Ctrls& ctrls, std::uint8_t lpd1Lvl);

// ---------------------------------------------------------------------------
// EMC2 rung 4: set_bipred3x3_controls (:5939-:5974) +
// set_dist_based_ref_pruning_controls (:5976-:6231).
// ---------------------------------------------------------------------------
// Bipred3x3Controls (md_process.h:190-200, verbatim field order/types).
struct Bipred3x3Ctrls {
    std::uint8_t enabled;      // refine the ME-MV(s) of bipred @ mds0
    std::uint8_t searchDiag;
    std::uint8_t useBestList;
    std::uint8_t useL0L1Dev;
};

// svt_aom_set_bipred3x3_controls (:5939-5974). The default case
// assert(0)s and writes nothing (documented UB).
void setBipred3x3Ctrls(Bipred3x3Ctrls& ctrls, std::uint8_t bipred3x3Injection);

// InterCandGroup (md_process.h:65-78, verbatim ordinals).
constexpr int kTotInterGroup = 11;
constexpr int kPaMeGroup = 0;
constexpr int kUni3x3Group = 1;
constexpr int kBi3x3Group = 2;
constexpr int kNrstNewNearGroup = 3;
constexpr int kNrstNearGroup = 4;
constexpr int kPredMeGroup = 5;
constexpr int kGlobalGroup = 6;
constexpr int kWarpGroup = 7;
constexpr int kObmcGroup = 8;
constexpr int kInterIntraGroup = 9;
constexpr int kInterCompGroup = 10;

// RefPruningControls (md_process.h:202-211, verbatim field order/types).
struct RefPruningCtrls {
    std::uint8_t enabled;
    std::uint32_t maxDevToBest[kTotInterGroup];
    std::uint8_t useTplInfoOffset;
    std::uint8_t checkClosestMultiplier;
    std::uint8_t closestRefs[kTotInterGroup];
};

// svt_aom_set_dist_based_ref_pruning_controls (:5976-6231). The default
// case assert(0)s and writes nothing (documented UB).
void setDistBasedRefPruningCtrls(RefPruningCtrls& ctrls, std::uint8_t distBasedRefPruningLevel);

// ---------------------------------------------------------------------------
// EMC2 rung 5: set_txs_controls (:6233-:6303), set_filter_intra_ctrls
// (:6305-:6324), set_spatial_sse_full_loop_level (:6326-:6347),
// compute_intra_pd0_th / compute_subres_th (:6349-:6370),
// set_lpd1_tx_skip_decision_ctrls (:6372-:6408), set_lpd1_tx_ctrls
// (:6410-:6460), set_cfl_ctrls (:6462-:6498), set_rate_est_ctrls
// (:6500-:6547).
// ---------------------------------------------------------------------------
// TxsControls (md_process.h:487-507, verbatim field order/types).
struct TxsCtrls {
    std::uint8_t enabled;
    std::uint8_t prevDepthCoeffExitTh;
    std::uint8_t intraClassMaxDepthSq;
    std::uint8_t intraClassMaxDepthNsq;
    std::uint8_t interClassMaxDepthSq;
    std::uint8_t interClassMaxDepthNsq;
    int depth1TxtGroupOffset;
    int depth2TxtGroupOffset;
    std::int32_t quadrantThSf;
};

// set_txs_controls (:6233-6303). The pcs->mimic_only_tx_4x4 wall forces
// level 1 (:6238-6240). NOTE: the default arm is NOT assert(0) - it
// writes enabled = 0 (:6297-6299). Case MAX_TXS_LEVEL - 1 = 5.
void setTxsCtrls(TxsCtrls& ctrls, std::uint8_t txsLevel, bool mimicOnlyTx4x4);

// FilterIntraCtrls (md_process.h:835-840, verbatim).
// FILTER_DC_PRED = 0 .. FILTER_PAETH_PRED = 4 (definitions.h:1296-1302).
struct FilterIntraCtrls {
    bool enabled;
    std::uint8_t maxFilterIntraMode;
};

// set_filter_intra_ctrls (:6305-6324). The default case assert(0)s and
// writes nothing (documented UB).
void setFilterIntraCtrls(FilterIntraCtrls& ctrls, std::uint8_t fiLvl);

// SpatialSseLevel (definitions.h:864-869, verbatim ordinals):
// SSSE_MDS1 = 0, SSSE_MDS2 = 1, SSSE_MDS3 = 2, SSSE_OFF = 3.
struct SpatialSseCtrls {
    std::uint8_t level;  // SpatialSseLevel
};

// set_spatial_sse_full_loop_level (:6326-6347). The default case
// assert(0)s and writes nothing (documented UB).
void setSpatialSseFullLoopLevel(SpatialSseCtrls& ctrls, std::uint8_t spatialSseFullLoopLevel);

// The RDCOST fold (rd_cost.h:36-38): ROUND_POWER_OF_TWO(R*RM,
// AV1_PROB_COST_SHIFT) + D<<RDDIV_BITS, with AV1_PROB_COST_SHIFT = 9
// (md_rate_estimation.h:29) and RDDIV_BITS = 7 (rd_cost.h:35).
constexpr int kAv1ProbCostShift = 9;
constexpr int kRddivBits = 7;
constexpr int kEb8BitMd = 0;   // definitions.h:1857
constexpr int kEb10BitMd = 1;  // definitions.h:1858

// The ctx surface of compute_intra_pd0_th / compute_subres_th: the
// fast-lambda pair + hbd_md; scs->super_block_size threads as the
// scalar (the two reference bodies are the same arithmetic).
struct RdCostCtx {
    std::uint32_t fastLambdaMd[2];
    std::uint8_t hbdMd;
};

// compute_intra_pd0_th (:6349-6360).
std::uint64_t computeIntraPd0Th(std::uint32_t superBlockSize, const RdCostCtx& ctx);
// compute_subres_th (:6361-6370).
std::uint64_t computeSubresTh(std::uint32_t superBlockSize, const RdCostCtx& ctx);

// Lpd1TxSkipDecisionCtrls (md_process.h:730-740, verbatim).
struct Lpd1TxSkipDecisionCtrls {
    int skipTxScoreTh;
    std::uint16_t distEnergyTh;
    std::uint16_t rdSkipTh;
};

// set_lpd1_tx_skip_decision_ctrls (:6372-6408). The default case
// assert(0)s and writes nothing (documented UB).
void setLpd1TxSkipDecisionCtrls(Lpd1TxSkipDecisionCtrls& ctrls, std::uint8_t level);

// Lpd1TxCtrls (md_process.h:742-750, verbatim field order/types).
struct Lpd1TxCtrls {
    std::uint8_t zeroYCoeffExit;
    std::uint8_t chromaDetectorLevel;
    std::uint32_t useMds3ShortcutsTh;
    std::uint8_t useUvShortcutsOnYCoeffs;
};

// set_lpd1_tx_ctrls (:6410-6460). The default case assert(0)s and
// writes nothing (documented UB).
void setLpd1TxCtrls(Lpd1TxCtrls& ctrls, std::uint8_t lpd1TxLevel);

// CflCtrls (md_process.h:756-761, verbatim).
struct CflCtrls {
    bool enabled;
    std::uint8_t itrTh;
    std::uint8_t cplxTh;
};

// set_cfl_ctrls (:6462-6498). The default case assert(0)s and writes
// nothing (documented UB).
void setCflCtrls(CflCtrls& ctrls, std::uint8_t cflLevel);

// MdRateEstCtrls (md_process.h:763-776, verbatim field order/types).
struct MdRateEstCtrls {
    bool updateSkipCtxDcSignCtx;
    bool updateSkipCoeffCtx;
    std::uint8_t coeffRateEstLvl;  // 0: OFF, 1: full, 2: low-nz approx
    std::int8_t lpd0QpOffset;
    std::uint8_t pd0FastCoeffEstLevel;
};

// set_rate_est_ctrls (:6500-6547). The default case assert(0)s and
// writes nothing (documented UB).
void setRateEstCtrls(MdRateEstCtrls& ctrls, std::uint8_t rateEstLevel);

// ---------------------------------------------------------------------------
// EMC2 rung 6: get_sb_tpl_intra_stats (:6552-:6609) + set_intra_ctrls
// (:6611-:6796).
// ---------------------------------------------------------------------------
// PredictionMode ordinals consumed here (definitions.h:1168-1204):
// DC_PRED = 0, V_PRED = 1 .. D67_PRED = 8 (the directional band),
// SMOOTH_PRED = 9, SMOOTH_V_PRED = 10, SMOOTH_H_PRED = 11,
// PAETH_PRED = 12, INTRA_MODE_END = 13.
constexpr std::uint8_t kDcPred = 0;
constexpr std::uint8_t kVPred = 1;
constexpr std::uint8_t kD67Pred = 8;
constexpr std::uint8_t kSmoothPred = 9;
constexpr std::uint8_t kSmoothHPred = 11;
constexpr std::uint8_t kPaethPred = 12;
constexpr std::uint8_t kIntraModeEnd = 13;

// The TPL wall surface of get_sb_tpl_intra_stats (:6552-6609). The
// reference walks ppcs->pa_me_data->tpl_src_stats_buffer (one TplSrcStats
// per 16x16 cell, row stride = aligned16_width cells); the carrier
// compacts the consumed best_mode column into its own array (the named
// fold - the struct deref reduces to the best_mode field).
struct SbTplPcs {
    bool tplEnable;                    // ppcs->tpl_ctrls.enable
    bool tplSrcDataReady;              // ppcs->tpl_src_data_ready
    std::uint8_t temporalLayerIndex;   // pcs->temporal_layer_index
    std::uint32_t hierarchicalLevels;  // ppcs->hierarchical_levels
    bool disableIntraPredNref;         // ppcs->tpl_ctrls.disable_intra_pred_nref
    std::uint32_t alignedWidth;        // ppcs->aligned_width
    std::uint8_t dispenserSearchLevel;  // ppcs->tpl_ctrls.dispenser_search_level
    std::uint8_t sbGeomWidth;          // ppcs->sb_geom[ctx->sb_index].width
    std::uint8_t sbGeomHeight;         // ... .height
    const std::uint8_t* tplBestModes;  // compacted best_mode per 16x16 cell
};
struct SbTplCtx {
    std::uint32_t sbOriginX;  // ctx->sb_origin_x
    std::uint32_t sbOriginY;  // ctx->sb_origin_y
};

// get_sb_tpl_intra_stats (:6552-6609). Returns 1 with the three outputs
// written when TPL data is usable; returns 0 with the outputs untouched
// otherwise (the gate :6565-:6567).
bool getSbTplIntraStats(const SbTplPcs& pcs, const SbTplCtx& ctx, int* sbAngIntraCount,
                        std::uint8_t* sbMaxIntra, int* sbIntraCount);

// IntraCtrls (md_process.h:778-789, verbatim field order/types).
struct IntraCtrls {
    std::uint8_t enableIntra;
    std::uint8_t intraModeEnd;
    std::uint8_t angularPredLevel;
    std::uint8_t pruneUsingBestMode;
    bool pruneUsingEdgeInfo;
    std::int8_t skipAngularDelta1Th;
    std::int8_t skipAngularDelta2Th;
    std::int8_t skipAngularDelta3Th;
};

// The pcs/ppcs wall surface of set_intra_ctrls (:6611-6796). The TPL
// sub-surface is the getSbTplIntraStats carrier embedded.
struct IntraSetPcs {
    bool skipIntra;                     // pcs->skip_intra
    bool isISlice;                      // pcs->slice_type == I_SLICE
    bool tplEnable;                     // ppcs->tpl_ctrls.enable
    std::uint8_t tplIntraModeEnd;       // ppcs->tpl_ctrls.intra_mode_end
    std::uint8_t refIntraPercentage;    // pcs->ref_intra_percentage
    SbTplPcs tpl;                       // the stats surface
    SbTplCtx tplCtx;                    // ctx->sb_origin/sb_index fold
};

// The ctx surface: pd_pass read; skip_intra written (:6789-:6795).
struct IntraSetCtx {
    std::uint8_t pdPass;
    bool skipIntra;
};

// set_intra_ctrls (:6611-6796), 1:1. The reference's
// assert(IMPLIES(islice, intra_level > 0)) (:6620) is a debug-only
// contract on the CALLER (the port drops it, named); the default arms
// assert(0) and write nothing (documented UB).
void setIntraCtrls(IntraCtrls& ctrls, const IntraSetPcs& pcs, IntraSetCtx& ctx,
                   std::uint8_t intraLevel, std::uint8_t distBasedAngIntraLevel);

// ---------------------------------------------------------------------------
// EMC2 rung 7: set_tx_shortcut_ctrls (:6798-:6838), set_mds0_controls
// (:6840-:6861), set_skip_sub_depth_ctrls (:6863-:6890) and
// set_block_based_depth_refinement_controls (:6892-:7065).
// ---------------------------------------------------------------------------
// TxShortcutCtrls (md_process.h:791-802, verbatim field order/types).
struct TxShortcutCtrls {
    std::uint32_t bypassTxTh;
    std::uint8_t applyPfOnCoeffs;
    std::uint8_t chromaDetectorLevel;
    std::uint32_t useMds3ShortcutsTh;
};

// set_tx_shortcut_ctrls (:6798-6838). The trailing debug assert
// (:6832-:6837) is a level-configuration invariant (chroma detector on
// for ref frames at <= M9) compiled out under NDEBUG in the tested
// config - named, nothing to thread. The default case assert(0)s and
// writes nothing (documented UB).
void setTxShortcutCtrls(TxShortcutCtrls& ctrls, std::uint8_t txShortcutLevel);

// Mds0Ctrls (md_process.h:804-810, verbatim).
constexpr int kCandClassTotal = 5;  // CAND_CLASS_TOTAL (definitions.h:772)
struct Mds0Ctrls {
    std::uint8_t pruningMethodTh;
    std::uint16_t perClassDistToCostTh[kCandClassTotal];
    std::uint16_t distToCostTh;
};

// set_mds0_controls (:6840-6861). Level 1 does NOT write
// per_class_dist_to_cost_th[CAND_CLASS_4]; level 2 does NOT write the
// per_class array. The default case assert(0)s and writes nothing
// (documented UB).
void setMds0Ctrls(Mds0Ctrls& ctrls, std::uint8_t mds0Level);

// SkipSubDepthCtrls (md_process.h:823-833, verbatim).
struct SkipSubDepthCtrls {
    std::uint8_t enabled;
    std::uint8_t maxSize;
    int quadDeviationTh;
    std::uint8_t coeffPerc;
};

// set_skip_sub_depth_ctrls (:6863-6890). The default case assert(0)s
// and writes nothing (documented UB).
void setSkipSubDepthCtrls(SkipSubDepthCtrls& ctrls, std::uint8_t skipSubDepthLvl);

// DepthRefinementCtrls (md_process.h:231-276, verbatim field order/
// types). MAX_RANGE_CNT = 8 (md_process.h:225).
constexpr int kMaxRangeCnt = 8;
// PD0_DEPTH_* (md_process.h:227-229).
constexpr std::uint8_t kPd0DepthNoRestriction = 0;
constexpr std::uint8_t kPd0DepthAdaptive = 1;
constexpr std::uint8_t kPd0DepthPredPartOnly = 2;
struct DepthRefinementCtrls {
    std::uint8_t mode;
    std::uint8_t s1ParentToCurrentTh;
    std::uint8_t s2ParentToCurrentTh;
    std::uint8_t e1SubToCurrentTh;
    std::uint8_t e2SubToCurrentTh;
    std::uint16_t parentMaxCostThMult;
    std::uint8_t coeffLvlModulation;
    std::uint8_t costBandBasedModulation;
    std::uint16_t maxCostMultiplier;
    std::uint8_t maxBandCnt;
    std::int64_t decrementPerBand[kMaxRangeCnt];
    std::uint32_t lowerDepthSplitCostTh;
    std::uint32_t splitRateTh;
    std::uint8_t limitMaxMinToPd0;
    std::uint8_t useRefInfo;
    std::uint8_t qWeight;
    std::uint32_t pd0UnavailModeDepth;
};

// set_block_based_depth_refinement_controls (:6892-7065). The switch
// has NO default arm - levels > 10 write nothing (documented).
void setBlockBasedDepthRefinementCtrls(DepthRefinementCtrls& ctrls,
                                       std::uint8_t blockBasedDepthRefinementLevel);

// ---------------------------------------------------------------------------
// EMC2 rung 8: get_max_block_size_{default,rtc,allintra} (:7067-:7160),
// set_depth_early_exit_ctrls (:7258-:7281), the disallow_4x4/8x8
// getters (:8245-:8290) and the nsq_geom_level getters (:8292-:8330).
// ---------------------------------------------------------------------------
// The pcs/scs wall surface of get_max_block_size_rtc/allintra
// (:7071-:7160). The SB64 path reads ppcs->me_8x8_cost_variance /
// ppcs->variance at [ctx->sb_index]; the SB128 path gathers through
// getSb128MeData / getSb128Variance.
struct MaxBlkPcs {
    std::uint32_t superBlockSize;   // scs->super_block_size
    std::uint8_t sbGeomWidth;       // ppcs->sb_geom[ctx->sb_index].width
    std::uint8_t sbGeomHeight;      // ... .height
    bool isISlice;                  // pcs->slice_type == I_SLICE (rtc only)
    EncMode encMode;                // pcs->enc_mode
    bool capMaxSizeQpScaling;  // scs->qp_based_th_scaling_ctrls.
                               //   cap_max_size_qp_based_th_scaling
    std::uint32_t qp;               // scs->static_config.qp
    std::uint32_t sbIndex;          // ctx->sb_index (SB64 path)
    const std::uint32_t* me8x8CostVariance;  // ppcs-> (SB64 path)
    Sb128Pcs sb128;                 // the SB128 ME gather surface
    std::uint32_t sbOriginX;        // ctx->sb_origin (SB128 path)
    std::uint32_t sbOriginY;
    const std::uint16_t* variance64;  // &ppcs->variance[0][tier0] (allintra)
    std::uint32_t varianceStride;     // the tier-count row stride
};

// get_max_block_size_default (:7067-7069): the cap IS the SB size.
std::uint32_t getMaxBlockSizeDefault(std::uint32_t superBlockSize);
// get_max_block_size_rtc (:7071-7116).
std::uint32_t getMaxBlockSizeRtc(const MaxBlkPcs& pcs);
// get_max_block_size_allintra (:7118-7143).
std::uint32_t getMaxBlockSizeAllIntra(const MaxBlkPcs& pcs);

// DepthEarlyExitCtrls (md_process.h:586-595, verbatim).
struct DepthEarlyExitCtrls {
    std::uint16_t splitCostTh;  // percentage * 10
    std::uint16_t earlyExitTh;  // percentage * 10
};

// set_depth_early_exit_ctrls (:7258-7281). The default case assert(0)s
// and writes nothing (documented UB).
void setDepthEarlyExitCtrls(DepthEarlyExitCtrls& ctrls, std::uint8_t earlyExitLevel);

// svt_aom_get_disallow_4x4_{default,rtc,allintra} (:8245-:8270).
bool getDisallow4x4Default(EncMode encMode);
bool getDisallow4x4Rtc();
bool getDisallow4x4AllIntra(EncMode encMode);
// svt_aom_get_disallow_8x8_{default,rtc,allintra} (:8272-:8290). The
// rtc variant consumes dimensions_require_8x8 (EMC1).
bool getDisallow8x8Default();
bool getDisallow8x8Rtc(EncMode encMode, std::uint16_t alignedWidth, std::uint16_t alignedHeight);
bool getDisallow8x8AllIntra();

// svt_aom_get_nsq_geom_level_{default,rtc,allintra} (:8292-:8330).
// coeffLvl is the InputCoeffLvl ordinal (HIGH_LVL = 3).
std::uint8_t getNsqGeomLevelDefault(EncMode encMode, std::uint8_t coeffLvl);
std::uint8_t getNsqGeomLevelRtc();
std::uint8_t getNsqGeomLevelAllIntra(EncMode encMode);

// ---------------------------------------------------------------------------
// EMC2 rung 9: svt_aom_sig_deriv_enc_dec_common (:7162-:7256) and
// svt_aom_sig_deriv_enc_dec_pd0 (:7283-:7454) - the per-SB
// compositions of the rungs above.
// ---------------------------------------------------------------------------
// The pcs wall surface of sig_deriv_enc_dec_common. The depth-removal
// wall threads through the caller-supplied DepthRemovalPcs/
// DepthRemovalCtx (EMC1 carriers) as separate parameters.
struct EncDecCommonPcs {
    EncMode encMode;                         // pcs->enc_mode
    bool isISlice;                           // pcs->slice_type == I_SLICE
    std::uint8_t picBlockBasedDepthRefinementLevel;  // pcs->
    std::uint8_t picPd0Lvl;                  // pcs->pic_pd0_lvl
    bool picDisallow4x4;                     // pcs->pic_disallow_4x4
    std::uint8_t picDepthRemovalLevel;       // pcs->pic_depth_removal_level
    std::uint8_t picLpd1Lvl;                 // pcs->pic_lpd1_lvl
    std::uint8_t nsqGeomLevel;               // pcs->nsq_geom_level
    std::uint16_t b64GeomWidth;              // ppcs->b64_geom[ctx->sb_index].width
    std::uint16_t b64GeomHeight;             // ... .height
    std::uint32_t me8x8CostVariance;         // ppcs->[ctx->sb_index] (the M11 band)
    std::uint32_t baseQIdx;                  // frm_hdr...base_q_idx (cyclic refresh)
    // the get_max_block_size walls (:7071-:7143):
    bool capMaxSizeQpScaling;  // scs->qp_based_th_scaling_ctrls.
                               //   cap_max_size_qp_based_th_scaling
    std::uint32_t qp;          // scs->static_config.qp
    const std::uint16_t* variance64;  // &ppcs->variance[0][tier0] (allintra)
    std::uint32_t varianceStride;
};
struct EncDecCommonScs {
    bool allintra;      // scs->allintra
    bool rtcTune;       // scs->static_config.rtc
    std::int32_t maxTxSize;  // scs->static_config.max_tx_size
};
// The written-and-read ctx surface (each field cites its site).
struct EncDecCommonCtx {
    // walls read by the composition:
    std::uint8_t hbdMd;       // ctx->hbd_md (set_pd0_ctrls early-out)
    std::uint8_t sbQindex;    // ctx->sb_ptr->qindex (:7224)
    std::uint32_t qpIndex;    // ctx->qp_index (:7242)
    bool isBase;              // frame_is_boosted(ppcs) (:7226)
    bool isNotLastLayer;      // !frame_is_leaf(ppcs) (:7233)
    // written:
    DepthRefinementCtrls depthRefinementCtrls;
    bool predDepthOnly;
    bool picPredDepthOnly;
    Pd0Ctrls pd0Ctrls;
    DepthRemovalCtrls depthRemovalCtrls;
    bool disallow8x8;
    bool disallow4x4;
    std::uint32_t maxBlockSize;
    std::uint8_t pd1LvlRefinement;
    NsqGeomCtrls nsqGeomCtrls;
    Lpd1Ctrls lpd1Ctrls;
};

// svt_aom_sig_deriv_enc_dec_common (:7162-7256), 1:1. drPcs/drCtx are
// the set_depth_removal_level_controls walls (EMC1); the composition
// owns the same lifecycle the reference does.
void sigDerivEncDecCommon(EncDecCommonPcs& pcs, const EncDecCommonScs& scs,
                          EncDecCommonCtx& ctx, const DepthRemovalPcs& drPcs,
                          DepthRemovalCtx& drCtx);

// The pcs wall surface of sig_deriv_enc_dec_pd0 (:7283-7454).
struct EncDecPd0Pcs {
    EncMode encMode;                     // pcs->enc_mode
    bool isISlice;                       // pcs->slice_type == I_SLICE
    std::uint8_t transitionPresent;      // ppcs->transition_present
    std::uint8_t pd0CostBiasWeight;      // pcs->pd0_cost_bias_weight
    std::uint32_t me64x64Distortion;     // ppcs->[ctx->sb_index] (SB64 path)
    std::uint32_t me8x8Distortion;       // ppcs->[ctx->sb_index] (bias weight)
    std::uint32_t me8x8CostVariance;     // ppcs->[ctx->sb_index] (bias mod)
    std::uint8_t hbdMd;                  // pcs->hbd_md
    std::uint32_t baseQIdx;              // frm_hdr...base_q_idx
    bool b64IsComplete;                  // b64_geom->is_complete_b64
    std::uint32_t rateEstLevel;          // pcs->rate_est_level
    std::uint32_t sbIndex;               // ctx->sb_index
    bool sb128Path;                      // seq_header.sb_size == BLOCK_128X128
    bool isNotLastLayer;                 // !frame_is_leaf(ppcs) (:7300)
    Sb128Pcs sb128;                      // the SB128 gather surface
    std::uint32_t sbOriginX;
    std::uint32_t sbOriginY;
    // the set_intra_ctrls wall (embedded):
    bool skipIntra;
    bool tplEnable;
    std::uint8_t tplIntraModeEnd;
    std::uint8_t refIntraPercentage;
    SbTplPcs tpl;
    SbTplCtx tplCtx;
};
struct EncDecPd0Scs {
    bool allintra;
    bool rtcTune;
};
// The written-and-read ctx surface of the pd0 composition.
struct EncDecPd0Ctx {
    // read:
    Pd0Ctrls pd0Ctrls;                   // pd0_level (:7290)
    DepthRemovalCtrls depthRemovalCtrls;  // (:7424-:7430)
    bool disallow4x4;                    // ctx->disallow_4x4 (:7408)
    bool disallow8x8;                    // ctx->disallow_8x8 (:7425)
    std::uint32_t fastLambdaMd[2];       // ctx->fast_lambda_md
    std::uint8_t hbdMd;                  // ctx->hbd_md (the lambda pick)
    bool picPredDepthOnly;               // ctx->pic_pred_depth_only (:7320)
    // written:
    bool mdDisallowNsqSearch;
    bool shutFastRate;
    DepthEarlyExitCtrls depthEarlyExitCtrls;
    IntraCtrls intraCtrls;
    IntraSetCtx intraSetCtx;             // the set_intra_ctrls ctx (pd_pass)
    std::uint32_t parentCostBias;
    bool pd0UseSrcSamples;
    UvCtrls uvCtrls;
    PfCtrls pfCtrls;
    SubresCtrls subresCtrls;
    MdRateEstCtrls rateEstCtrls;
    bool approxInterRate;
};

// svt_aom_sig_deriv_enc_dec_pd0 (:7283-7454), 1:1. The reference's
// early return at PD0_LVL_6 (:7385-7387) leaves the chroma/pf/subres/
// rate-est tail unwritten (pinned by test).
void sigDerivEncDecPd0(EncDecPd0Pcs& pcs, const EncDecPd0Scs& scs, EncDecPd0Ctx& ctx);

// ---------------------------------------------------------------------------
// EMC2 rung 10: svt_aom_sig_deriv_enc_dec_light_pd1_default
// (:7456-:7653) and svt_aom_sig_deriv_enc_dec_light_pd1_rtc
// (:7655-:7889).
// ---------------------------------------------------------------------------
// The pcs/ppcs wall surface of both light-PD1 compositions. The ref-
// object reads (:7487-:7499 / :7683-:7695) thread as the pre-read
// scalars + the two is_ref_same_size gates (the EbReferenceObject wall
// folded - the MeModeCfg precedent).
struct LightPd1Pcs {
    EncMode encMode;                    // pcs->enc_mode
    bool useFlatIpp;                    // ppcs->hierarchical_levels == 0 (rtc)
    ResolutionRange inputResolution;    // ppcs->input_resolution (default)
    std::uint32_t pictureQp;            // ppcs->picture_qp
    std::uint32_t me8x8CostVariance;    // pre-read; ~0 when no ref (:7477)
    std::uint32_t me64x64Distortion;    // pre-read; ~0 when no ref (:7478)
    std::uint8_t l0WasSkip, l1WasSkip;
    std::uint8_t l0Was64x64Mvp, l1Was64x64Mvp;
    std::uint8_t refSkipPerc;           // pcs->ref_skip_percentage
    std::uint8_t candReductionLevel;    // pcs->cand_reduction_level
    std::uint8_t rdoqLevel;             // pcs->rdoq_level
    std::uint8_t coeffShavingLevel;     // pcs->coeff_shaving_level
    std::uint8_t meSubpelLevel;         // pcs->me_subpel_level
    std::uint32_t rateEstLevel;         // pcs->rate_est_level
    std::uint8_t approxInterRate;       // pcs->approx_inter_rate
    std::uint8_t intraLevel;            // pcs->intra_level
    bool skipIntra;                     // pcs->skip_intra
    // the set_cand_reduction_ctrls wall (EMC2-C0 / EMC1 carrier fields):
    std::uint8_t refList0CountTry;
    std::uint8_t refList1CountTry;
    bool useBestMeUnipredCandOnly;
    bool isNotLastLayer;                // !frame_is_leaf(pcs->ppcs)
    bool rtcTune;                       // pcs->scs->static_config.rtc
    std::uint8_t hierarchicalLevels;    // ppcs->hierarchical_levels
    // the set_intra_ctrls wall (embedded):
    bool tplEnable;
    std::uint8_t tplIntraModeEnd;
    std::uint8_t refIntraPercentage;
    SbTplPcs tpl;
    SbTplCtx tplCtx;
};

// The written ctx surface of both light-PD1 compositions (each field
// cites its write site; L = default :7502-:7652, R = rtc :7699-:7888).
struct LightPd1Ctx {
    Lpd1Ctrls lpd1Ctrls;                // read: pd1_level (:7459/:7658)
    std::uint16_t lpd1GlobalmvBypassTh; // = 0 (:7500/:7697)
    CandReductionCtrls candReductionCtrls;
    RdoqCtrls rdoqCtrls;
    CoeffShavingCtrls coeffShavingCtrls;
    MdSubPelSearchCtrls mdSubPelCtrls;
    Lpd1TxSkipDecisionCtrls lpd1TxSkipDecisionCtrls;
    Lpd1TxCtrls lpd1TxCtrls;
    std::uint8_t lpd1BlkSkipLumaRdPct;  // (:7593-:7599 / :7816-:7822)
    std::uint8_t lpd1ChromaSkipEnergyTh;
    MdRateEstCtrls rateEstCtrls;
    std::uint8_t approxInterRate;
    PfCtrls pfCtrls;
    IntraCtrls intraCtrls;
    IntraSetCtx intraSetCtx;
    bool shutFastRate;
    UvCtrls uvCtrls;
    CflCtrls cflCtrls;
    bool mdDisallowNsqSearch;
    bool newNearestInjection;
    bool blkSkipDecision;
    SubresCtrls subresCtrls;
    InterIntraCompCtrls interIntraCtrls;
};

// svt_aom_sig_deriv_enc_dec_light_pd1_default (:7456-7653), 1:1.
void sigDerivEncDecLightPd1Default(LightPd1Pcs& pcs, LightPd1Ctx& ctx);
// svt_aom_sig_deriv_enc_dec_light_pd1_rtc (:7655-7889), 1:1.
void sigDerivEncDecLightPd1Rtc(LightPd1Pcs& pcs, LightPd1Ctx& ctx);

// ---------------------------------------------------------------------------
// EMC2 rung 11: svt_aom_sig_deriv_enc_dec_{default,rtc,allintra}
// (:7891-:8007 / :8009-:8120 / :8121-:8243) - the per-SB tune
// compositions.
// ---------------------------------------------------------------------------
// The shared pcs wall surface of the three compositions (each field
// cites its site; D = default, R = rtc, A = allintra; the (A 0)
// markers are the allintra literals).
struct EncDecTunePcs {
    EncMode encMode;                      // pcs->enc_mode
    ResolutionRange inputResolution;      // ppcs->input_resolution (default)
    std::uint32_t pictureQp;              // ppcs->picture_qp
    std::uint8_t refSkipPerc;             // pcs->ref_skip_percentage
    std::uint8_t nsqSearchLevel;          // pcs->nsq_search_level
    std::uint8_t nicLevel;                // pcs->nic_level
    std::uint8_t candReductionLevel;      // pcs->cand_reduction_level
    std::uint8_t txtLevel;                // pcs->txt_level
    std::uint8_t txShortcutLevel;         // pcs->tx_shortcut_level
    std::uint8_t interpolationSearchLevel;  // pcs-> (A passes 0)
    std::uint8_t chromaLevel;             // pcs->chroma_level
    std::uint8_t cflLevel;                // pcs->cfl_level
    bool newNearestNearCombInjection;     // pcs-> (A passes 0)
    std::uint8_t wmLevel;                 // pcs->wm_level (A passes 0)
    bool unipred3x3Injection;             // pcs-> (A passes 0)
    std::uint8_t bipred3x3Injection;      // pcs-> (A passes 0)
    std::uint8_t interCompoundMode;       // pcs->inter_compound_mode (A 0)
    std::uint8_t distBasedRefPruning;     // pcs-> (A 0)
    std::uint8_t spatialSseFullLoopLevel; // pcs->
    std::uint8_t rdoqLevel;               // pcs->rdoq_level
    std::uint8_t coeffShavingLevel;       // pcs->
    std::uint8_t picObmcLevel;            // ppcs->pic_obmc_level (A 0)
    std::uint8_t interIntraLevel;         // pcs->inter_intra_level (A 0)
    std::uint8_t txsLevel;                // pcs->txs_level
    std::uint8_t picFilterIntraLevel;     // pcs->pic_filter_intra_level
    bool allowIntrabc;                    // frm_hdr->allow_intrabc
    std::uint8_t mdPaletteLevel;          // ppcs->palette_level
    std::uint8_t mdSqMvSearchLevel;       // pcs->md_sq_mv_search_level (A 0)
    std::uint8_t mdNsqMvSearchLevel;      // pcs->md_nsq_mv_search_level (A 0)
    std::uint8_t mdPmeLevel;              // pcs->md_pme_level (A 0)
    std::uint8_t meSubpelLevel;           // pcs->me_subpel_level (A 0)
    std::uint8_t pmeSubpelLevel;          // pcs->pme_subpel_level (A 0)
    std::uint32_t rateEstLevel;           // pcs->rate_est_level
    std::uint8_t approxInterRate;         // pcs->approx_inter_rate
    std::uint8_t intraLevel;              // pcs->intra_level
    std::uint8_t distBasedAngIntraLevel;  // pcs->dist_based_ang_intra_level
    std::uint8_t mds0Level;               // pcs->mds0_level
    bool gmEnabled;                       // ppcs->gm_ctrls.enabled (A: global 0)
    bool skipIntra;                       // pcs->skip_intra (the intra wall)
    bool tplEnable;                       // ppcs->tpl_ctrls.enable
    std::uint8_t tplIntraModeEnd;         // ppcs->tpl_ctrls.intra_mode_end
    std::uint8_t refIntraPercentage;      // pcs->ref_intra_percentage
    SbTplPcs tpl;                         // the TPL stats surface
    SbTplCtx tplCtx;
    // the set_nsq_search_ctrls wall (EMC2-C2 carrier fields):
    bool isISlice;                        // pcs->slice_type == I_SLICE
    bool mimicOnlyTx4x4;                  // pcs->mimic_only_tx_4x4
    std::uint32_t superBlockSize;         // scs->super_block_size
    std::uint32_t sbIndex;                // ctx->sb_index
    const std::uint32_t* me8x8Distortion;      // ppcs-> (SB64 nsq path)
    const std::uint32_t* me8x8CostVariance;    // ppcs-> (SB64 nsq path)
    Sb128Pcs sb128;                       // the SB128 nsq gather surface
    std::uint32_t sbOriginX;
    std::uint32_t sbOriginY;
    bool nsqQpBasedThScaling;             // scs->qp_based_th_scaling_ctrls.
    std::uint32_t qp;                     // scs->static_config.qp
    std::uint8_t pdPass;                  // ctx->pd_pass
    // the set_cand_reduction_ctrls wall:
    std::uint8_t refList0CountTry;
    std::uint8_t refList1CountTry;
    bool useBestMeUnipredCandOnly;
    bool isNotLastLayer;
    bool rtcTune;
    std::uint8_t hierarchicalLevels;
};

// The written ctx surface of the three compositions (D :7891-:8006,
// R :8009-:8119, A :8121-:8242).
struct EncDecTuneCtx {
    // prior-state walls the composition reads (written by
    // sig_deriv_enc_dec_common earlier in the SB walk):
    NsqGeomCtrls nsqGeomCtrls;             // md_disallow_nsq_search / redundant_blk
    std::uint8_t pd0LevelWall;             // the allintra txs fold reads pd0_level
    NsqSearchCtrls nsqSearchCtrls;
    NsqPsqTxsCtrls nsqPsqTxsCtrls;
    NicCtrls nicCtrls;                     // set_nic_controls (nonnull ctx)
    CandReductionCtrls candReductionCtrls;
    TxtCtrls txtCtrls;
    TxShortcutCtrls txShortcutCtrls;
    InterpolationSearchCtrls interpolationSearchCtrls;
    UvCtrls uvCtrls;
    CflCtrls cflCtrls;
    bool mdDisallowNsqSearch;
    bool globalMvInjection;
    bool newNearestInjection;
    bool newNearestNearCombInjection;
    WmCtrls wmCtrls;
    bool unipred3x3Injection;
    Bipred3x3Ctrls bipred3x3Ctrls;
    InterCompCtrls interCompCtrls;
    RefPruningCtrls refPruningCtrls;
    SpatialSseCtrls spatialSseCtrls;
    bool blkSkipDecision;
    RdoqCtrls rdoqCtrls;
    CoeffShavingCtrls coeffShavingCtrls;
    bool redundantBlk;
    DepthEarlyExitCtrls depthEarlyExitCtrls;
    ObmcCtrls obmcCtrls;
    InterIntraCompCtrls interIntraCtrls;
    TxsCtrls txsCtrls;
    FilterIntraCtrls filterIntraCtrls;
    bool mdAllowIntrabc;
    std::uint8_t mdPaletteLevel;
    PfCtrls pfCtrls;
    MdSqMotionSearchCtrls mdSqCtrls;
    MdNsqMotionSearchCtrls mdNsqCtrls;
    MdPmeCtrls mdPmeCtrls;
    MdSubPelSearchCtrls mdSubPelCtrls;
    MdSubPelSearchCtrls mdSubPelPmeCtrls;
    MdRateEstCtrls rateEstCtrls;
    std::uint8_t approxInterRate;
    bool shutFastRate;
    IntraCtrls intraCtrls;
    IntraSetCtx intraSetCtx;
    bool mds0UseHadamardSb;
    Mds0Ctrls mds0Ctrls;
    SubresCtrls subresCtrls;
    std::uint32_t parentCostBias;
    SkipSubDepthCtrls skipSubDepthCtrls;
    std::uint8_t tuneSsimLevel;            // SSIM_LVL_0 = 0
};

// svt_aom_sig_deriv_enc_dec_default (:7891-8007), 1:1.
void sigDerivEncDecDefault(EncDecTunePcs& pcs, EncDecTuneCtx& ctx);
// svt_aom_sig_deriv_enc_dec_rtc (:8009-8120), 1:1.
void sigDerivEncDecRtc(EncDecTunePcs& pcs, EncDecTuneCtx& ctx);
// svt_aom_sig_deriv_enc_dec_allintra (:8121-8243), 1:1.
void sigDerivEncDecAllIntra(EncDecTunePcs& pcs, EncDecTuneCtx& ctx);

// ---------------------------------------------------------------------------
// EMC2 rung 12: the nsq_search-level getters (:8330-:8493), the
// bypass-encdec getters (:8494-:8543), set_cdf_controls (:8544-:8585),
// the update_cdf-level getters (:8591-:8627), the chroma-level getters
// (:8628-:8672), set_pic_pd0_lvl_{default,rtc,allintra}
// (:8673-:8838), get_obmc_level (:8896-:8933) and mfmv_controls
// (:8934-:8980).
// ---------------------------------------------------------------------------
// MAX_TEMPORAL_LAYERS (definitions.h:2030).
constexpr int kMaxTemporalLayers = 6;

// The pcs/ppcs wall surface of the three nsq_search-level getters.
struct NsqSearchLvlPcs {
    bool isBase;                      // ppcs->temporal_layer_index == 0 (default)
    bool r0Gen;                       // ppcs->r0_gen
    bool isISlice;                    // pcs->slice_type == I_SLICE
    double r0;                        // ppcs->r0
    std::uint8_t temporalLayerIndex;  // pcs->temporal_layer_index
    std::uint8_t seqQpMod;            // scs->seq_qp_mod
};

// svt_aom_get_nsq_search_level_default (:8330-8405).
std::uint8_t getNsqSearchLevelDefault(EncMode encMode, const NsqSearchLvlPcs& pcs,
                                      std::uint8_t coeffLvl, std::uint32_t qp);
// svt_aom_get_nsq_search_level_rtc (:8407-8437).
std::uint8_t getNsqSearchLevelRtc(const NsqSearchLvlPcs& pcs, std::uint8_t coeffLvl,
                                  std::uint32_t qp);
// svt_aom_get_nsq_search_level_allintra (:8439-8492).
std::uint8_t getNsqSearchLevelAllIntra(const NsqSearchLvlPcs& pcs, EncMode encMode,
                                       std::uint8_t coeffLvl, std::uint32_t qp);

// NSQ_MODULATION_MIN_LEVEL (enc_mode_config.c:8356).
constexpr int kNsqModulationMinLevel = 8;

// svt_aom_get_bypass_encdec_{default,rtc,allintra} (:8494-:8543).
// SVT_EFFECTIVE_BIT_DEPTH is the identity in the pinned build
// (EbConfigMacros.h:293); EB_EIGHT_BIT = 8.
std::uint8_t getBypassEncdecDefault(EncMode encMode, std::uint8_t encoderBitDepth);
std::uint8_t getBypassEncdecRtc(EncMode encMode, std::uint8_t encoderBitDepth);
std::uint8_t getBypassEncdecAllIntra(EncMode encMode);

// CdfControls (pcs.h:130-135, verbatim).
struct CdfCtrls {
    std::uint8_t updateMv;
    std::uint8_t updateSe;
    std::uint8_t updateCoef;
    std::uint8_t enabled;
};

// The pcs wall surface of set_cdf_controls (:8544-8585).
struct CdfSetPcs {
    bool isISlice;           // pcs->slice_type == I_SLICE
    std::uint32_t rateEstLevel;  // pcs->rate_est_level
    std::uint8_t rdoqLevel;  // pcs->rdoq_level
};

// set_cdf_controls (:8544-8585). The CONFIG_ENABLE_MD_CDF_UPDATE
// trailing assert is a runtime invariant compiled out in the pinned
// build (named, nothing to thread). The default case assert(0)s and
// writes nothing (documented UB).
void setCdfCtrls(CdfCtrls& ctrls, const CdfSetPcs& pcs, std::uint8_t updateCdfLevel);

// svt_aom_get_update_cdf_level_{default,rtc,allintra} (:8591-:8627).
// isIslice is the SliceType == I_SLICE fold.
std::uint8_t getUpdateCdfLevelDefault(EncMode encMode, bool isIslice, std::uint8_t isBase);
std::uint8_t getUpdateCdfLevelRtc(EncMode encMode, bool isIslice);
std::uint8_t getUpdateCdfLevelAllIntra(EncMode encMode);

// svt_aom_get_chroma_level_{default,rtc,allintra} (:8628-:8672).
std::uint8_t getChromaLevelDefault(EncMode encMode, bool isIslice);
std::uint8_t getChromaLevelRtc(EncMode encMode);
std::uint8_t getChromaLevelAllIntra(EncMode encMode);

// The pcs/ppcs wall surface of the three set_pic_pd0_lvl setters.
struct PicPd0Pcs {
    std::uint8_t temporalLayerIndex;  // ppcs->temporal_layer_index (default)
    bool isISlice;                    // pcs->slice_type == I_SLICE
    std::uint8_t transitionPresent;   // ppcs->transition_present
    std::uint8_t coeffLvl;            // pcs->coeff_lvl
    ResolutionRange inputResolution;  // ppcs->input_resolution
    std::uint8_t seqQpMod;            // scs->seq_qp_mod
    std::uint32_t qp;                 // scs->static_config.qp
    std::uint32_t superBlockSize;     // scs->super_block_size
    bool isBase;                      // rtc: frame_is_boosted(ppcs)
    // write:
    std::uint8_t picPd0Lvl;
};

// set_pic_pd0_lvl_default (:8673-8791).
void setPicPd0LvlDefault(PicPd0Pcs& pcs, EncMode encMode);
// set_pic_pd0_lvl_rtc (:8793-8822).
void setPicPd0LvlRtc(PicPd0Pcs& pcs, EncMode encMode);
// set_pic_pd0_lvl_allintra (:8824-8838).
void setPicPd0LvlAllIntra(PicPd0Pcs& pcs, EncMode encMode);

// svt_aom_get_obmc_level (:8896-8933).
std::uint8_t getObmcLevel(EncMode encMode, std::uint32_t qp, std::uint8_t seqQpMod);

// The pcs/ppcs/ref-object wall surface of mfmv_controls (:8934-8980).
struct MfmvPcs {
    bool isBase;                    // ppcs->temporal_layer_index == 0
    bool scsTpl;                    // ppcs->scs->tpl
    bool r0Gen;                     // pcs->ppcs->r0_gen
    double r0;                      // pcs->ppcs->r0
    bool isISlice;                  // pcs->slice_type == I_SLICE
    bool isBSlice;                  // pcs->slice_type == B_SLICE
    std::uint8_t refList1CountTry;  // ppcs->ref_list1_count_try
    bool refL0MfmvUsed;             // ref_obj_l0->is_mfmv_used
    bool refL1MfmvUsed;             // ref_obj_l1->is_mfmv_used
    // write:
    bool useRefFrameMvs;            // ppcs->frm_hdr.use_ref_frame_mvs
};

// mfmv_controls (:8934-8980). The default case assert(0)s and writes
// nothing but the leading use_ref_frame_mvs = 0 (:8940) (documented).
void mfmvCtrls(MfmvPcs& pcs, std::uint8_t mfmvLevel);

}  // namespace mode_config
}  // namespace l16_rdo
