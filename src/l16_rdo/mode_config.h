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

}  // namespace mode_config
}  // namespace l16_rdo
