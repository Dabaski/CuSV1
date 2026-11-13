#pragma once

// A3M (A3 campaign): the PURE, struct-free motion-search helpers of
// third_party/SVT-AV1/Source/Lib/Codec/av1me.c, motion_estimation.c and
// mcomp.c. Struct-free port: every wall the ported spans consume
// (IntraBcContext / MeContext / PictureControlSet / EbPictureBufferDesc /
// AomVarianceFnPtr) is mirrored as a carrier struct or threaded parameter
// holding ONLY the consumed fields (the RE8A DeblockContext precedent); the
// function bodies are verbatim.
//
// Mv / MvJointType reuse the landed amvp twins (motion::amvp::Mv is mv.h's
// union Mv verbatim); the cost-table carrier is the caller's.

#include <cstdint>

#include "amvp.h"

namespace motion {

using Mv = amvp::Mv;  // mv.h:42-49 union Mv (the landed amvp twin)

// av1me.h:24-29: MAX_MVSEARCH_STEPS / MAX_FULL_PEL_VAL / MAX_FIRST_STEP.
constexpr int kMaxMvSearchSteps = 11;
constexpr int kMaxFullPelVal = (1 << (kMaxMvSearchSteps - 1)) - 1;  // 1023
constexpr int kMaxFirstStep = 1 << (kMaxMvSearchSteps - 1);         // 1024

// cabac_context_model.h:197-199 MV_LOW / MV_UPP.
constexpr int kMvLow = -(1 << 14);
constexpr int kMvUpp = 1 << 14;

// MvLimits (definitions.h:277-283, verbatim).
struct MvLimits {
    int col_min;
    int col_max;
    int row_min;
    int row_max;
};

// ---------------------------------------------------------------------------
// The diamond-search site config (av1me.h)
// ---------------------------------------------------------------------------

// SearchSite (av1me.h:32-35, verbatim).
struct SearchSite {
    Mv mv;
    int offset;
};

// SearchSiteConfig (av1me.h:37-41, verbatim; 8 * 11 + 1 = 89 sites).
struct SearchSiteConfig {
    SearchSite ss[8 * kMaxMvSearchSteps + 1];
    int ssCount;
    int searchesPerStep;
};

// av1me.c:162-188 svt_av1_init3smotion_compensation.
void init3smotionCompensation(SearchSiteConfig* cfg, int stride);

// ---------------------------------------------------------------------------
// MV cost primitives
// ---------------------------------------------------------------------------

// rd_cost.c:47-54 svt_av1_get_mv_joint (the Mv*-form twin; the landed l16_rdo
// getMvJoint travels as the packed as_int form and l5 cannot link l16 - the
// SYM1 wb-trio duplication precedent).
int getMvJoint(const Mv* mv);

// mcomp.h:138-146 svt_mv_cost. jointCost is joint_cost[4]; compCost[0] indexes
// the y row, compCost[1] the x row, each addressed at [MV_LOW] by the caller
// (the mvcost convention).
int mvCost(const Mv* mv, const int* jointCost, const int* const compCost[2]);

// The vtable members the ported spans consume (av1me.h:50-64 typedef shapes;
// the full 8-member AomVarianceFnPtr vtable is not carried - named
// deviation).
using SadFn = std::uint32_t (*)(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride);
using VarianceFn = std::uint32_t (*)(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride,
                                     std::uint32_t* sse);
using SubpixVarianceFn = std::uint32_t (*)(const std::uint8_t* a, int aStride, int xOffset, int yOffset,
                                           const std::uint8_t* b, int bStride, std::uint32_t* sse);
using SadMultiDFn = void (*)(const std::uint8_t* a, int aStride, const std::uint8_t* const bArray[], int bStride,
                             std::uint32_t* sadArray);

// The consumed IntraBcContext surface (coding_unit.h:120-145): ONLY the
// fields the ported search spans read. The reference's x travels as this
// struct (the RE8A DeblockContext precedent).
struct SearchCtx {
    const std::uint8_t* srcBuf;     // x->plane[0].src.buf (coding_unit.h:63-66 Buf2D)
    int srcStride;                  // x->plane[0].src.stride
    const std::uint8_t* refBuf;     // x->xdplane[0].pre[0].buf
    int refStride;                  // x->xdplane[0].pre[0].stride
    MvLimits mvLimits;              // x->mv_limits (coding_unit.h:124)
    Mv* secondBestMv;               // x->second_best_mv (written by the searches)
    std::uint8_t approxInterRate;   // x->approx_inter_rate (coding_unit.h:144)
    const int* nmvVecCost;          // x->nmv_vec_cost (coding_unit.h:136)
    const int* const* mvCostStack;  // x->mv_cost_stack (coding_unit.h:137)
    int errorperbit;                // x->errorperbit (coding_unit.h:130)
};

// ---------------------------------------------------------------------------
// The ME/HME carrier surface (me_context.h / pcs.h windows)
// ---------------------------------------------------------------------------

// The EbPictureBufferDesc consumed surface (the y plane only - every ported
// span reads y_buffer / y_stride and check_00_center adds width / height).
struct RefPic {
    const std::uint8_t* yBuffer;
    std::uint32_t yStride;
    std::uint32_t width;
    std::uint32_t height;
};

// SearchArea / SearchAreaMinMax / SearchInfo / SearchResults
// (me_context.h:306-:334, verbatim shapes).
struct SearchArea {
    std::uint16_t width;
    std::uint16_t height;
};
struct SearchAreaMinMax {
    SearchArea saMin;
    SearchArea saMax;
};
struct SearchInfo {
    SearchArea sa;
    Mv bestMv;
    std::uint64_t sad;
    std::uint8_t valid;
};
struct SearchResults {
    std::uint8_t listI;
    std::uint8_t refI;
    std::int16_t hmeScX;
    std::int16_t hmeScY;
    std::uint64_t hmeSad;
    std::uint8_t doRef;
};

// MeCandidate (me_sb_results.h:30-:36, verbatim bitfields).
struct MeCandidate {
    std::uint8_t direction : 2;
    std::uint8_t refIdxL0 : 2;
    std::uint8_t refIdxL1 : 2;
    std::uint8_t ref0List : 1;
    std::uint8_t ref1List : 1;
};

// The MeSbResults consumed surface (me_sb_results.h:38-:48): the three per-SB
// pools, resolved at sb_index by the caller (the pcs->pa_me_data wall).
struct MeSbResults {
    std::uint8_t* totalMeCandidateIndex;
    MeCandidate* meCandidateArray;
    Mv* meMvArray;
};

// EbMeType (me_context.h:36-44): only the ME_MCTF comparison is consumed.
constexpr int kMeMctf = 1;

// definitions.h:2032-2033 / EbSvtAv1Enc.h:35 / me_sb_results.h:28-30.
constexpr int kMaxNumOfRefPicList = 2;
constexpr int kMaxRefIdx = 4;
constexpr int kRefListMaxDepth = 4;
constexpr int kSquarePuCount = 85;
constexpr int kMaxSb64PuCountNo8x8 = 21;
constexpr int kMaxSb64PuCountWo16x16 = 5;

// motion_estimation.h:63 MAX_SAD_VALUE.
constexpr std::uint32_t kMaxSadValue = 128u * 128u * 255u;

// The consumed MeContext surface (me_context.h:331-:493 window). Every
// ported span's reads/writes; the huge MeContext shrinks to this.
struct MeCtx {
    // get_zz_sad / check_00_center / me_static_b64_bypass
    const std::uint8_t* b64SrcPtr;
    std::uint32_t b64SrcStride;
    std::uint32_t meEarlyExitTh;
    std::uint32_t meStaticB64Th;
    std::uint32_t b64Width;
    std::uint32_t b64Height;
    // check_prehme_early_exit
    std::uint32_t zzSad[kMaxNumOfRefPicList][kRefListMaxDepth];
    SearchInfo prehmeData[kMaxNumOfRefPicList][kMaxRefIdx][2];
    std::uint8_t prehmeL1EarlyExit;  // prehme_ctrl.l1_early_exit
    // me_prune_ref / hme_prune_ref_and_adjust_sr / the construct family
    std::uint8_t numOfListToSearch;
    std::uint8_t numOfRefPicToSearch[kMaxNumOfRefPicList];
    SearchResults searchResults[kMaxNumOfRefPicList][kRefListMaxDepth];
    std::uint32_t pSbBestSad[kMaxNumOfRefPicList][kMaxRefIdx][kSquarePuCount];
    std::uint32_t pSbBestMv[kMaxNumOfRefPicList][kMaxRefIdx][kSquarePuCount];
    std::uint32_t meDistortion[kSquarePuCount];
    bool enableMeHmeRefPruning;
    std::uint16_t pruneRefIfMeSadDevBiggerThanTh;
    std::uint16_t pruneRefIfHmeSadDevBiggerThanTh;
    // hme_prune / get_hme_l0
    std::uint8_t enableMeSrAdjustment;
    std::uint8_t distanceBasedHmeResizing;
    std::uint16_t reduceMeSrBasedOnMvLengthTh;
    std::uint16_t stationaryHmeSadAbsTh;
    std::uint16_t stationaryMeSrDivisor;
    std::uint16_t reduceMeSrBasedOnHmeSadAbsTh;
    std::uint16_t meSrDivisorForLowHmeSad;
    std::uint32_t reduceMeSrDivisor[kMaxNumOfRefPicList][kRefListMaxDepth];
    // get_hme_l0_search_area / get_worst_quadrant / init_me_hme_data
    SearchAreaMinMax hmeL0Sa;
    std::uint16_t numHmeSaW;
    std::uint16_t numHmeSaH;
    std::uint8_t reduceHmeL0SrThMin;
    std::uint8_t reduceHmeL0SrThMax;
    std::int16_t xHmeLevel0SearchCenter[kMaxNumOfRefPicList][kMaxRefIdx][2][2];
    std::int16_t yHmeLevel0SearchCenter[kMaxNumOfRefPicList][kMaxRefIdx][2][2];
    std::uint64_t hmeLevel0Sad[kMaxNumOfRefPicList][kMaxRefIdx][2][2];
    std::int16_t xHmeLevel1SearchCenter[kMaxNumOfRefPicList][kMaxRefIdx][2][2];
    std::int16_t yHmeLevel1SearchCenter[kMaxNumOfRefPicList][kMaxRefIdx][2][2];
    std::int16_t xHmeLevel2SearchCenter[kMaxNumOfRefPicList][kMaxRefIdx][2][2];
    std::int16_t yHmeLevel2SearchCenter[kMaxNumOfRefPicList][kMaxRefIdx][2][2];
    std::uint8_t performedPhme[kMaxNumOfRefPicList][kRefListMaxDepth][2];
    bool enableHmeFlag;
    int meType;  // EbMeType; kMeMctf = 1
    // construct_me_candidate_array
    std::int32_t pruneMeCandidatesTh;
    std::uint8_t useBestUnipredCandOnly;
    // me_static_b64_bypass: me_ds_ref_array[0][0].picture_ptr threaded
    RefPic dsRef00;
};

// B64Geom (pcs.h:406-:412, verbatim).
struct B64Geom {
    std::uint16_t orgX;
    std::uint16_t orgY;
    std::uint8_t width;
    std::uint8_t height;
    std::uint8_t isCompleteB64;
};

// The consumed PictureControlSet surface (pcs.h:862-:870, :1134-:1135 +
// scs->mrp_ctrls.only_l_bwd definitions.h:134 + scs->input_resolution
// INPUT_SIZE_480p_RANGE=2 definitions.h:1811).
struct PcsCtx {
    std::uint8_t enableMe8x8;
    std::uint8_t enableMe16x16;
    std::uint8_t maxNumberOfPusPerSb;
    std::uint8_t maxCand;
    std::uint8_t maxRefs;
    std::uint8_t maxL0;
    MeSbResults* sbResults;  // pa_me_data->me_results[sb_index], resolved
    bool onlyLBwd;
    const B64Geom* b64Geom;  // pcs->b64_geom, indexed by b64_index
    std::uint32_t* me8x8CostVariance;
    std::uint32_t* rcMeDistortion;
    std::uint32_t* me64x64Distortion;
    std::uint32_t* me32x32Distortion;
    std::uint32_t* me16x16Distortion;
    std::uint32_t* me8x8Distortion;
    bool inputResolution480pOrBelow;
};

// av1me.c:126-132 svt_aom_mv_err_cost_light.
int mvErrCostLight(const Mv* mv, const Mv* ref);

// av1me.c:134-140 mvsad_err_cost_light (static in the reference; exported -
// the cdefSign testability precedent).
int mvsadErrCostLight(const Mv* mv, const Mv* ref);

// av1me.c:142-150 svt_aom_mv_err_cost. The mvcost rows are addressed at
// [MV_LOW] by the caller (the mvcost convention).
int mvErrCost(const Mv* mv, const Mv* ref, const int* mvjcost, const int* const mvcost[2], int errorPerBit);

// av1me.c:152-160 mvsad_err_cost (static in the reference; the x wall
// threaded as SearchCtx).
int mvsadErrCost(const SearchCtx& x, const Mv* mv, const Mv* ref, int sadPerBit);

// av1me.c:195-208 svt_av1_get_mvpred_var (the AomVarianceFnPtr wall threaded
// as its consumed vf member).
int getMvpredVar(const SearchCtx& x, const Mv* bestMv, const Mv* centerMv, VarianceFn vf, int useMvcost);

// ---------------------------------------------------------------------------
// The full-pel search drivers (av1me.c)
// ---------------------------------------------------------------------------

// av1me.c:212-289 exhaustive_mesh_search (static in the reference; the
// fn_ptr wall threaded as its consumed sdf/sdx4df members).
std::uint32_t exhaustiveMeshSearch(SearchCtx& x, Mv* refMv, Mv* bestMv, int range, int step, int sadPerBit,
                                   SadFn sdf, SadMultiDFn sdx4df, const Mv* centerMv);

// av1me.c:291-422 svt_av1_diamond_search_sad_c. NEW_DIAMOND_SEARCH is NOT
// defined in the pinned tree (grep-verified: no definition in Source/), so
// the reference's compiled form has no inner refinement loop.
std::uint32_t diamondSearchSad(SearchCtx& x, const SearchSiteConfig& cfg, Mv* refMv, Mv* bestMv, int searchParam,
                               int sadPerBit, int* num00, SadFn sdf, SadMultiDFn sdx4df, const Mv* centerMv);

// av1me.c:424-487 svt_av1_refining_search_sad (static in the reference; the
// fn_ptr wall threaded as its consumed sdf/sdx4df members).
std::uint32_t refiningSearchSad(SearchCtx& x, Mv* refMv, int errorPerBit, int searchRange, SadFn sdf,
                                SadMultiDFn sdx4df, const Mv* centerMv);

// ---------------------------------------------------------------------------
// The mcomp.c cost surface
// ---------------------------------------------------------------------------

// MV_COST_TYPE (mcomp.h:24-31, verbatim order).
enum MvCostType {
    MV_COST_ENTROPY = 0,  // entropy rate of the mv
    MV_COST_L1_LOWRES,    // l1 norm (<480p)
    MV_COST_L1_MIDRES,    // l1 norm (>=480p)
    MV_COST_L1_HDRES,     // l1 norm (>=720p)
    MV_COST_OPT,
    MV_COST_NONE,
};

// svt_mv_cost_param (mcomp.h:38-49): the fields svt_mv_err_cost_ consumes.
// full_ref_mv / early_exit_th / sad_per_bit exist in the reference struct but
// are read only by the PARKED check_better spans - not carried.
struct MvCostParam {
    const Mv* refMv;       // mv_cost_params->ref_mv
    MvCostType mvCostType;
    const int* mvjcost;
    const int* mvcost[2];  // rows addressed at [MV_LOW] by the caller
    int errorPerBit;
};

// mcomp.c:42-72 svt_mv_err_cost (static INLINE in the reference; exported -
// the cdefSign testability precedent).
int mvErrCostMcomp(const Mv* mv, const Mv* refMv, const int* mvjcost, const int* const mvcost[2], int errorPerBit,
                   MvCostType mvCostType);

// mcomp.c:74-97 svt_mv_err_cost_.
int mvErrCostFromParams(const Mv* mv, const MvCostParam* mvCostParams);

// mcomp.c:775-777 svt_aom_fp_mv_err_cost.
int fpMvErrCost(const Mv* mv, const MvCostParam* mvCostParams);

// mcomp.c:248-254 get_best_diag_step (static INLINE in the reference;
// exported - the cdefSign testability precedent).
Mv getBestDiagStep(int stepSize, std::uint32_t leftCost, std::uint32_t rightCost, std::uint32_t upCost,
                   std::uint32_t downCost);

// ---------------------------------------------------------------------------
// The subpel pref-error threading (mcomp.c)
// ---------------------------------------------------------------------------

// mcomp.c:99-104 svt_get_subpel_part.
int subpelPart(int x);

// mcomp.c:106-110 svt_get_buf_from_mv (the svt_buf_2d* wall threaded as the
// buf/stride pair - the struct-free deviation).
const std::uint8_t* subpelBufFromMv(const std::uint8_t* buf, int stride, const Mv& mv);

// The consumed SUBPEL_SEARCH_VAR_PARAMS + MSBuffers surface
// (mcomp.h:66-85): the svf member and the src/ref buffer pairs. The
// upsampled-pref-error path (is_scaled) is PARKED with its driver.
struct SubpelVarParams {
    SubpixVarianceFn svf;
    const std::uint8_t* srcBuf;
    int srcStride;
    const std::uint8_t* refBuf;
    int refStride;
};

// mcomp.c:156-174 svt_estimated_pref_error (static INLINE in the reference;
// exported - the cdefSign testability precedent).
int estimatedPrefError(const Mv* thisMv, const SubpelVarParams& varParams, std::uint32_t* sse);

// ---------------------------------------------------------------------------
// The search-range surface (av1me.c)
// ---------------------------------------------------------------------------

// av1me.c:97-122 svt_av1_set_mv_search_range.
void setMvSearchRange(MvLimits* mvLimits, const Mv* mv);

// av1me.c:190-193 is_mv_in (static INLINE in the reference; exported - the
// cdefSign testability precedent).
int isMvIn(const MvLimits* mvLimits, const Mv* mv);

// av1me.c:694-707 set_subpel_mv_search_range (static INLINE under
// CONFIG_ENABLE_OBMC = 1 in the pinned default tree, EbConfigMacros.h:167;
// the four int* outs threaded as the reference's own signature).
void setSubpelMvSearchRange(const MvLimits* mvLimits, int* colMin, int* colMax, int* rowMin, int* rowMax,
                            const Mv* refMv);

// ---------------------------------------------------------------------------
// The ME tier-zero SAD aggregation family (motion_estimation.c)
// ---------------------------------------------------------------------------

// motion_estimation.c:43-69 svt_aom_compute8x4_sad_kernel_c and :71-:98
// compute8x8_sad_kernel_c: the SAME EB_ABS_DIFF loop the landed nxm helper
// (C_DEFAULT/compute_sad_c.c:21, motion::nxmSadKernelHelper / the landed
// sad8x4 / sad8x8 instances) generalizes over (height, width) - the
// ext_sad family's internal calls route to the landed instances (function-
// identity deviation, arithmetic 1:1; grep-verified no separate port).

// motion_estimation.c:100-162 svt_ext_sad_calculation_8x8_16x16_c. The
// four 8x8 quadrants of a 16x16 block: sub_sad recomputes each as an 8x4
// kernel over DOUBLED strides, shifted << 1; best SAD/MV update on strict <.
void extSadCalculation8x8_16x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                                std::uint32_t refStride, std::uint32_t* pBestSad8x8, std::uint32_t* pBestSad16x16,
                                std::uint32_t* pBestMv8x8, std::uint32_t* pBestMv16x16, std::uint32_t mv,
                                std::uint32_t* pSad16x16, std::uint32_t* pSad8x8, bool subSad);

// motion_estimation.c:164-200 svt_ext_sad_calculation_32x32_64x64_c.
void extSadCalculation32x32_64x64(const std::uint32_t* pSad16x16, std::uint32_t* pBestSad32x32,
                                  std::uint32_t* pBestSad64x64, std::uint32_t* pBestMv32x32,
                                  std::uint32_t* pBestMv64x64, std::uint32_t mv, std::uint32_t* pSad32x32);

// motion_estimation.c:202-316 svt_ext_eight_sad_calculation_8x8_16x16
// (static in the reference; exported - the cdefSign testability
// precedent). The eight search positions are ref[0..7] x-shifts.
void extEightSadCalculation8x8_16x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                                     std::uint32_t refStride, std::uint32_t mv, std::uint32_t start16x16Pos,
                                     std::uint32_t* pBestSad8x8, std::uint32_t* pBestSad16x16,
                                     std::uint32_t* pBestMv8x8, std::uint32_t* pBestMv16x16,
                                     std::uint32_t pEightSad16x16[16][8], std::uint32_t pEightSad8x8[64][8],
                                     bool subSad);

// motion_estimation.c:318-349 svt_ext_all_sad_calculation_8x8_16x16_c: the
// 4x4 walk of 16x16 blocks over the offsets table.
void extAllSadCalculation8x8_16x16(const std::uint8_t* src, std::uint32_t srcStride, const std::uint8_t* ref,
                                   std::uint32_t refStride, std::uint32_t mv, std::uint32_t* pBestSad8x8,
                                   std::uint32_t* pBestSad16x16, std::uint32_t* pBestMv8x8,
                                   std::uint32_t* pBestMv16x16, std::uint32_t pEightSad16x16[16][8],
                                   std::uint32_t pEightSad8x8[64][8], bool subSad);

// motion_estimation.c:351-406 svt_ext_eight_sad_calculation_32x32_64x64_c.
void extEightSadCalculation32x32_64x64(const std::uint32_t pSad16x16[16][8], std::uint32_t* pBestSad32x32,
                                       std::uint32_t* pBestSad64x64, std::uint32_t* pBestMv32x32,
                                       std::uint32_t* pBestMv64x64, std::uint32_t mv, std::uint32_t pSad32x32[4][8]);

// motion_estimation.c:1152-1160 svt_aom_get_scaled_picture_distance
// (DIVIDE_AND_CEIL(dist * 5, 8), utility.h:97).
std::uint16_t getScaledPictureDistance(std::uint16_t dist);

// motion_estimation.c:1162-1182 apply_me_sa_boost (static in the reference;
// exported - the cdefSign testability precedent) with the
// search_area_multipliers table (motion_estimation.c:1164-:1168).
void applyMeSaBoost(std::int16_t* width, std::int16_t* height, std::uint64_t hmeSad, int scClassMeBoost);

// ---------------------------------------------------------------------------
// The MeCtx-carried helpers (motion_estimation.c)
// ---------------------------------------------------------------------------

// motion_estimation.c:1540-1564 get_zz_sad (static in the reference). The
// nxm SAD of the b64 source vs the ref block at the SB origin over
// (sb_height>>1, sb_width) with both strides doubled, then << 1.
std::uint32_t getZzSad(const RefPic& refPic, const MeCtx& meCtx, std::uint32_t sbOriginX, std::uint32_t sbOriginY,
                       std::uint32_t sbWidth, std::uint32_t sbHeight);

// motion_estimation.c:1060-1126 check_00_center (static in the reference).
// COST_PRECISION = 8 (lambda_rate_tables.h:20).
std::uint64_t check00Center(const RefPic& refPic, const MeCtx& meCtx, std::uint32_t sbOriginX,
                            std::uint32_t sbOriginY, std::uint32_t sbWidth, std::uint32_t sbHeight,
                            std::int16_t* xSearchCenter, std::int16_t* ySearchCenter, std::uint32_t zzSad);

// motion_estimation.c:1566-1592 check_prehme_early_exit (static in the
// reference).
bool checkPrehmeEarlyExit(MeCtx& meCtx, std::uint8_t listI, std::uint8_t refI, std::uint8_t srI);

// motion_estimation.c:1414-1455 me_prune_ref (static in the reference).
// ME_TIER_ZERO_PU_8x8_0 = 21 (me_context.h:54); tab8x8 is
// motion_estimation.h:87-:95.
void mePruneRef(MeCtx& meCtx);

// motion_estimation.c:1736-1766 get_worst_quadrant (static in the
// reference). Named deviation: the svt_aom_assert_err print wall becomes a
// plain early return (outputs untouched on a non-2x2 shape).
void getWorstQuadrant(const MeCtx& meCtx, std::uint32_t listIndex, std::uint32_t refPicIndex, std::uint8_t* bestW,
                      std::uint8_t* bestH);

// motion_estimation.c:2289-2332 hme_prune_ref_and_adjust_sr (static in the
// reference).
void hmePruneRefAndAdjustSr(MeCtx& meCtx);

// motion_estimation.c:1673-1734 get_hme_l0_search_area (static in the
// reference). MUTATES me_ctx->hme_l0_sa in place.
void getHmeL0SearchArea(MeCtx& meCtx, std::uint8_t listIndex, std::uint8_t refPicIndex, std::uint16_t dist,
                        std::int16_t* saWidth, std::int16_t* saHeight);

// motion_estimation.c:2787-2829 init_me_hme_data (static INLINE in the
// reference).
void initMeHmeData(MeCtx& meCtx);

// motion_estimation.c:2831-2886 me_static_b64_bypass (static in the
// reference). The RASTER_SCAN_CU bands: 64x64 = 0, 32x32 = 1..4,
// 16x16 = 5..20, 8x8 = 21..84 (definitions.h:2068+).
bool meStaticB64Bypass(MeCtx& meCtx, std::uint32_t b64OriginX, std::uint32_t b64OriginY);

// motion_estimation.c:2738-2785 compute_distortion (static in the
// reference). b64Index indexes the PcsCtx output arrays and the b64Geom
// entry.
void computeDistortion(const PcsCtx& pcs, std::uint32_t b64Index, const MeCtx& meCtx);

// motion_estimation.c:2334-2443 construct_me_candidate_array_mrp_off
// (static in the reference). sbIndex is consumed only to resolve
// me_results[sb_index], which the PcsCtx carrier already holds (the named
// sb_index deviation).
void constructMeCandidateArrayMrpOff(PcsCtx& pcs, MeCtx& meCtx, std::uint32_t numOfListToSearch);

// motion_estimation.c:2445-2496 construct_me_candidate_array_single_ref.
void constructMeCandidateArraySingleRef(PcsCtx& pcs, MeCtx& meCtx, std::uint32_t numOfListToSearch);

// motion_estimation.c:2498-2634 construct_me_candidate_array (the MRP-ON
// general form).
void constructMeCandidateArray(PcsCtx& pcs, MeCtx& meCtx, std::uint32_t numOfListToSearch);

}  // namespace motion
