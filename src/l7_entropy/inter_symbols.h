#pragma once

// A5E - the INTER symbol + context surface of entropy_coding.c (A5).
// Reference spans: third_party/SVT-AV1/Source/Lib/Codec/entropy_coding.c
// :52-:99 (the compound-index/group-idx contexts), :1097-:1219 (skip-mode +
// intra/inter + motion-mode), :1386-:1638 (inter mode / drl / mv / interp
// filter / compound mode writers), :1640-:2102 (the reference-mode /
// comp-reference context family) and :2104-:2187 (write_ref_frames).
//
// Struct-free carriers (the RE8A DeblockContext precedent, AGENTS.md): the
// MacroBlockD / MbModeInfo / BlockModeInfo / FRAME_CONTEXT fields each span
// consumes are mirrored into InterMbMi (the BlockModeInfo/MbModeInfo
// fields), InterMbD (the MacroBlockD fields) and InterFc (the FRAME_CONTEXT
// cdf families). The neighbor access is pointer-shaped exactly like the
// reference: a NULL above/left carrier stands for xd->above_mbmi ==
// NULL / xd->left_mbmi == NULL.
//
// Pinned build config (EbConfigMacros.h:167-226, the non-RTC branch):
// CONFIG_ENABLE_OBMC / CONFIG_ENABLE_WARP / CONFIG_ENABLE_GLOBAL_MOTION /
// CONFIG_ENABLE_INTER_COMPOUND / CONFIG_ENABLE_INTRA_BC are all 1, so the
// corresponding const-fold guards in the reference are LIVE behavior and
// the port keeps their bodies.
//
// Tests: src/l7_entropy/tests/test_inter_symbols.cpp. The ported writers
// have no reader twins in SVT (the decoder side lives in aom's
// decodemv.c/decodeframe.c, out of tree), so the writer roundtrips go
// through the existing l7 reader primitives (odEcReadSymbol/odEcReadBit)
// with the same cdf rows the context getters select, plus an independent
// test-side update_cdf transcription for the adaptation-state assertions.

#include "entropy.h"  // OdEcEnc/AomWriter/AomCdfProb/updateCdf/CDF_SIZE/BlockSize

#include <cstdint>

namespace entropy {

// ---------------------------------------------------------------------------
// Constants (verbatim values, each cited).
// ---------------------------------------------------------------------------

// definitions.h:818-827 (verbatim order).
enum InterpFilter {
    EIGHTTAP_REGULAR,
    EIGHTTAP_SMOOTH,
    MULTITAP_SHARP,
    BILINEAR,
    INTERP_FILTERS_ALL,
    SWITCHABLE_FILTERS = BILINEAR,  // 3
    SWITCHABLE = SWITCHABLE_FILTERS + 1,
};

// definitions.h:74-75 (filter.h) - the switchable-interp context offsets.
// The expansions carry the entropy:: qualification so the macros are usable
// outside the namespace (the test file uses them).
#define INTER_FILTER_COMP_OFFSET (entropy::SWITCHABLE_FILTERS + 1)
#define INTER_FILTER_DIR_OFFSET ((entropy::SWITCHABLE_FILTERS + 1) * 2)
// definitions.h:328 = (3+1)*4 = 16.
#define SWITCHABLE_FILTER_CONTEXTS ((entropy::SWITCHABLE_FILTERS + 1) * 4)

// definitions.h:1310-:1333.
#define INTER_MODES (1 + 3)                    // 1 + NEWMV - NEARESTMV
#define INTER_COMPOUND_MODES (1 + 7)           // 1 + NEW_NEWMV - NEAREST_NEARESTMV
#define SKIP_MODE_CONTEXTS 3                   // definitions.h:1315
#define COMP_INDEX_CONTEXTS 6                  // definitions.h:1316
#define COMP_GROUP_IDX_CONTEXTS 6              // definitions.h:1317
#define NEWMV_MODE_CONTEXTS 6                  // definitions.h:1319
#define GLOBALMV_MODE_CONTEXTS 2               // definitions.h:1320
#define REFMV_MODE_CONTEXTS 6                  // definitions.h:1321
#define DRL_MODE_CONTEXTS 3                    // definitions.h:1322
#define GLOBALMV_OFFSET 3                      // definitions.h:1324
#define REFMV_OFFSET 4                         // definitions.h:1325
#define NEWMV_CTX_MASK ((1 << GLOBALMV_OFFSET) - 1)                       // 7
#define GLOBALMV_CTX_MASK ((1 << (REFMV_OFFSET - GLOBALMV_OFFSET)) - 1)    // 1
#define REFMV_CTX_MASK ((1 << (8 - REFMV_OFFSET)) - 1)                    // 15
#define INTER_MODE_CONTEXTS 8                   // definitions.h:1332
#define INTRA_INTER_CONTEXTS 4                  // definitions.h:1346
#define COMP_INTER_CONTEXTS 5                   // definitions.h:1347
#define REF_CONTEXTS 3                          // definitions.h:1348
#define COMP_REF_TYPE_CONTEXTS 5                // definitions.h:1350
#define UNI_COMP_REF_CONTEXTS 3                 // definitions.h:1351

// definitions.h:1357-:1386 (the reference-frame enum; TOTAL_REFS_PER_FRAME
// = 8 is the neighbor-count array bound).
#define NONE_FRAME (-1)
#define INTRA_FRAME 0
#define LAST_FRAME 1
#define LAST2_FRAME 2
#define LAST3_FRAME 3
#define GOLDEN_FRAME 4
#define BWDREF_FRAME 5
#define ALTREF2_FRAME 6
#define ALTREF_FRAME 7
#define REF_FRAMES 8
#define TOTAL_REFS_PER_FRAME (ALTREF_FRAME - INTRA_FRAME + 1)
#define FWD_REFS (GOLDEN_FRAME - LAST_FRAME + 1)
#define BWD_REFS (ALTREF_FRAME - BWDREF_FRAME + 1)
#define SINGLE_REFS (FWD_REFS + BWD_REFS)

// inter_prediction.h:251-252 (verbatim).
#define CHECK_BACKWARD_REFS(rf) (((rf) >= BWDREF_FRAME) && ((rf) <= ALTREF_FRAME))
#define IS_BACKWARD_REF_FRAME(rf) CHECK_BACKWARD_REFS(rf)

// definitions.h:1397-:1411 (verbatim order).
enum UniDirCompRef {
    LAST_LAST2_FRAMES,
    LAST_LAST3_FRAMES,
    LAST_GOLDEN_FRAMES,
    BWDREF_ALTREF_FRAMES,
    LAST2_LAST3_FRAMES,
    LAST2_GOLDEN_FRAMES,
    LAST3_GOLDEN_FRAMES,
    BWDREF_ALTREF2_FRAMES,
    ALTREF2_ALTREF_FRAMES,
    TOTAL_UNIDIR_COMP_REFS,
    UNIDIR_COMP_REFS = BWDREF_ALTREF_FRAMES + 1,  // 4
};

// definitions.h:1472-:1477 (verbatim order).
enum ReferenceMode {
    SINGLE_REFERENCE = 0,
    COMPOUND_REFERENCE = 1,
    REFERENCE_MODE_SELECT = 2,
    REFERENCE_MODES = 3,
};

// definitions.h:1105-:1109 (verbatim order).
enum CompReferenceType { UNIDIR_COMP_REFERENCE = 0, BIDIR_COMP_REFERENCE = 1, COMP_REFERENCE_TYPES = 2 };

// definitions.h:1230-:1234 (verbatim order).
enum MotionMode { SIMPLE_TRANSLATION = 0, OBMC_CAUSAL = 1, WARPED_CAUSAL = 2, MOTION_MODES = 3 };

// definitions.h:1740-:1747 (verbatim order).
enum TransformationType { IDENTITY = 0, TRANSLATION = 1, ROTZOOM = 2, AFFINE = 3, TRANS_TYPES = 4 };

// The PredictionMode inter tail (definitions.h:1169-:1207). The l7
// entropy.h enum stops at INTRA_MODES = 13; the inter symbols extend it
// with the same verbatim values (NEARESTMV == 13 == INTRA_MODES).
constexpr PredictionMode NEARESTMV = static_cast<PredictionMode>(13);
constexpr PredictionMode NEARMV = static_cast<PredictionMode>(14);
constexpr PredictionMode GLOBALMV = static_cast<PredictionMode>(15);
constexpr PredictionMode NEWMV = static_cast<PredictionMode>(16);
constexpr PredictionMode NEAREST_NEARESTMV = static_cast<PredictionMode>(17);
constexpr PredictionMode NEAR_NEARMV = static_cast<PredictionMode>(18);
constexpr PredictionMode NEAREST_NEWMV = static_cast<PredictionMode>(19);
constexpr PredictionMode NEW_NEARESTMV = static_cast<PredictionMode>(20);
constexpr PredictionMode NEAR_NEWMV = static_cast<PredictionMode>(21);
constexpr PredictionMode NEW_NEARMV = static_cast<PredictionMode>(22);
constexpr PredictionMode GLOBAL_GLOBALMV = static_cast<PredictionMode>(23);
constexpr PredictionMode NEW_NEWMV = static_cast<PredictionMode>(24);
constexpr PredictionMode MB_MODE_COUNT = static_cast<PredictionMode>(25);
constexpr PredictionMode INTRA_MODE_END = NEARESTMV;
constexpr PredictionMode SINGLE_INTER_MODE_START = NEARESTMV;
constexpr PredictionMode SINGLE_INTER_MODE_END = NEAREST_NEARESTMV;
constexpr PredictionMode COMP_INTER_MODE_START = NEAREST_NEARESTMV;
constexpr PredictionMode COMP_INTER_MODE_END = MB_MODE_COUNT;
// cabac_context_model.h:232.
#define INTER_COMPOUND_OFFSET(mode) (static_cast<std::uint8_t>((mode) - NEAREST_NEARESTMV))

// MV plumbing (cabac_context_model.h:149-:223; l16_rdo cannot link l7 so
// the minimal set is carried here with citations - the l7-l13 duplication
// precedent in reverse).
#define MV_JOINTS 4
enum MvJointType { MV_JOINT_ZERO = 0, MV_JOINT_HNZVZ = 1, MV_JOINT_HZVNZ = 2, MV_JOINT_HNZVNZ = 3 };
inline int mvJointVertical(MvJointType type) { return type == MV_JOINT_HZVNZ || type == MV_JOINT_HNZVNZ; }
inline int mvJointHorizontal(MvJointType type) { return type == MV_JOINT_HNZVZ || type == MV_JOINT_HNZVNZ; }
#define MV_CLASSES 11
enum MvClassType { MV_CLASS_0 = 0, MV_CLASS_1, MV_CLASS_2, MV_CLASS_3, MV_CLASS_4, MV_CLASS_5, MV_CLASS_6,
                   MV_CLASS_7, MV_CLASS_8, MV_CLASS_9, MV_CLASS_10 };
#define CLASS0_BITS 1
#define CLASS0_SIZE (1 << CLASS0_BITS)
#define MV_OFFSET_BITS (MV_CLASSES + CLASS0_BITS - 2)
#define MV_FP_SIZE 4
enum MvSubpelPrecision { MV_SUBPEL_NONE = -1, MV_SUBPEL_LOW_PRECISION = 0, MV_SUBPEL_HIGH_PRECISION = 1 };

// mv.h:42-:49 (verbatim layout; the as_int union arm is not consumed).
struct Mv {
    std::int16_t x;
    std::int16_t y;
};

// ---------------------------------------------------------------------------
// NmvComponent / NmvContext (cabac_context_model.h:201-:215, verbatim
// layout). Named with the camelCase convention; member names verbatim.
// ---------------------------------------------------------------------------
struct NmvComponent {
    AomCdfProb classes_cdf[CDF_SIZE(MV_CLASSES)];
    AomCdfProb class0_fp_cdf[CLASS0_SIZE][CDF_SIZE(MV_FP_SIZE)];
    AomCdfProb fp_cdf[CDF_SIZE(MV_FP_SIZE)];
    AomCdfProb sign_cdf[CDF_SIZE(2)];
    AomCdfProb class0_hp_cdf[CDF_SIZE(2)];
    AomCdfProb hp_cdf[CDF_SIZE(2)];
    AomCdfProb class0_cdf[CDF_SIZE(CLASS0_SIZE)];
    AomCdfProb bits_cdf[MV_OFFSET_BITS][CDF_SIZE(2)];
};

struct NmvContext {
    AomCdfProb joints_cdf[CDF_SIZE(MV_JOINTS)];
    NmvComponent comps[2];
};

// ---------------------------------------------------------------------------
// Carriers (RE8A DeblockContext precedent): the BlockModeInfo/MbModeInfo
// and MacroBlockD fields each span consumes.
// ---------------------------------------------------------------------------
// InterMbMi - the BlockModeInfo/MbModeInfo block_mi surface (block_
// structures.h:36-:104): the fields the inter contexts and writers read.
// The compound_idx/comp_group_idx/skip_mode fields keep the reference's
// bit flags as ints (0/1).
struct InterMbMi {
    PredictionMode mode;
    std::int32_t ref_frame[2];       // MvReferenceFrame
    std::uint32_t interp_filters;    // InterpFilters (two 16-bit packed)
    MotionMode motion_mode;
    std::uint8_t num_proj_ref;
    int compound_idx;                // block_mi.compound_idx
    int comp_group_idx;              // block_mi.comp_group_idx
    int skip_mode;                   // block_mi.skip_mode
    int skip;                        // block_mi.skip
};

// InterFc - the FRAME_CONTEXT inter surface (cabac_context_model.h:278-:326
// + the FRAME_CONTEXT tail): the cdf families the inter spans write. The
// reference reads them through ec_writer->fc (the frame-level context) and
// xd->tile_ctx (the per-tile adaptive context) - both are FrameContexts
// structs in the reference, so one carrier shape covers both call sites.
struct InterFc {
    AomCdfProb newmv_cdf[NEWMV_MODE_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb zeromv_cdf[GLOBALMV_MODE_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb refmv_cdf[REFMV_MODE_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb drl_cdf[DRL_MODE_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb inter_compound_mode_cdf[INTER_MODE_CONTEXTS][CDF_SIZE(INTER_COMPOUND_MODES)];
    AomCdfProb motion_mode_cdf[BLOCK_SIZES_ALL][CDF_SIZE(MOTION_MODES)];
    AomCdfProb obmc_cdf[BLOCK_SIZES_ALL][CDF_SIZE(2)];
    AomCdfProb comp_inter_cdf[COMP_INTER_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb single_ref_cdf[REF_CONTEXTS][SINGLE_REFS - 1][CDF_SIZE(2)];
    AomCdfProb comp_ref_type_cdf[COMP_REF_TYPE_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb uni_comp_ref_cdf[UNI_COMP_REF_CONTEXTS][UNIDIR_COMP_REFS - 1][CDF_SIZE(2)];
    AomCdfProb comp_ref_cdf[REF_CONTEXTS][FWD_REFS - 1][CDF_SIZE(2)];
    AomCdfProb comp_bwdref_cdf[REF_CONTEXTS][BWD_REFS - 1][CDF_SIZE(2)];
    AomCdfProb compound_index_cdf[COMP_INDEX_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb comp_group_idx_cdf[COMP_GROUP_IDX_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb skip_mode_cdfs[SKIP_MODE_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb intra_inter_cdf[INTRA_INTER_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb switchable_interp_cdf[SWITCHABLE_FILTER_CONTEXTS][CDF_SIZE(SWITCHABLE_FILTERS)];
    NmvContext nmvc;
};

// InterMbD - the MacroBlockD surface (coding_unit.h:81-:111) the inter
// spans consume: the two neighbor mbmi pointers (NULL when the edge is
// unavailable - the reference's xd->above_mbmi/left_mbmi are NULLed
// outside the frame), the availability flags, the mi-stride (the switchable
// interp context's above walk uses mi[-mi_stride]) and the neighbor ref
// counts (coding_unit.h:102, the av1_zero'd TOTAL_REFS_PER_FRAME array
// svt_aom_collect_neighbors_ref_counts_new fills).
struct InterMbD {
    const InterMbMi* above_mi;
    const InterMbMi* left_mi;
    std::int32_t up_available;
    std::int32_t left_available;
    std::int32_t mi_stride;
    std::uint8_t neighbors_ref_counts[TOTAL_REFS_PER_FRAME];
};

// ---------------------------------------------------------------------------
// Ported functions (entropy_coding.c, in the task's order).
// ---------------------------------------------------------------------------

// svt_aom_get_comp_index_context_enc :52-:78. The pcs wall is threaded:
// the seq header's order-hint fields + the two frame-index pairs. The
// relative-dist helper is the l17_inter::getRelativeDistEnc twin
// (inter_prediction.c:276-:285), duplicated because l7 cannot link l17
// (the l7-l13 duplication precedent).
int getCompIndexContextEnc(const InterMbD* xd, int cur_frame_index, int bck_frame_index,
                           int fwd_frame_index, int enable_order_hint, int order_hint_bits);

// svt_aom_get_comp_group_idx_context_enc :80-:99.
int getCompGroupIdxContextEnc(const InterMbD* xd);

// av1_get_skip_mode_context :1097-:1103.
int getSkipModeContext(const InterMbD* xd);

// encode_skip_mode_av1 :1109-:1115: the skip-mode flag symbol through
// skip_mode_cdfs[av1_get_skip_mode_context(xd)].
void writeSkipMode(AomWriter* w, InterFc* fc, const InterMbD* xd, int skip_mode_flag);

// svt_av1_get_intra_inter_context :1126-:1141.
int getIntraInterContext(const InterMbD* xd);

// write_is_inter :1147-:1151.
void writeIsInter(AomWriter* w, InterFc* fc, const InterMbD* xd, int is_inter);

// svt_aom_motion_mode_allowed :1159-:1195. The CONFIG_ENABLE_OBMC/WARP
// guards are live (pinned build = 1), so only the runtime frame-header
// gates (is_motion_mode_switchable / force_integer_mv / allow_warped_
// motion), the num_proj_ref warped gate and the global-motion wmtype for
// rf0 are threaded.
MotionMode motionModeAllowed(int is_motion_mode_switchable, int force_integer_mv,
                             int allow_warped_motion, int gm_type_rf0,
                             std::uint8_t num_proj_ref, std::uint32_t overlappable_neighbors,
                             BlockSize bsize, std::int32_t rf0, std::int32_t rf1,
                             PredictionMode mode);

// write_motion_mode :1201-:1217. The frame-header/global-motion walls
// threaded; the allowed mode is derived inside.
void writeMotionMode(AomWriter* w, InterFc* fc, BlockSize bsize, PredictionMode mode,
                     std::uint8_t num_proj_ref, std::uint32_t overlappable_neighbors,
                     std::int32_t rf0, std::int32_t rf1, MotionMode motion_mode,
                     int is_motion_mode_switchable, int force_integer_mv, int allow_warped_motion,
                     int gm_type_rf0);

// write_inter_mode :1386-:1404. Deviation (named): the blk_org_x/y
// parameters are (void)'d by the reference - dropped.
void writeInterMode(AomWriter* w, InterFc* fc, PredictionMode mode, std::int16_t mode_ctx);

// write_drl_idx :1407-:1443. The mbmi/blk_ptr walls threaded: the mode,
// the drl_ctx[2] / drl_ctx_near[2] (the -1 sentinels included) and the
// drl_index.
void writeDrlIdx(AomWriter* w, InterFc* fc, PredictionMode mode, const std::int8_t drl_ctx[2],
                 const std::int8_t drl_ctx_near[2], std::uint8_t drl_index);

// encode_mv_component :1445-:1481.
void encodeMvComponent(AomWriter* w, std::int32_t comp, NmvComponent* mvcomp,
                       MvSubpelPrecision precision);

// av1_get_mv_joint_diff :1485-:1493.
MvJointType getMvJointDiff(const std::int32_t diff[2]);

// svt_av1_encode_mv :1495-:1520. The pcs wall is threaded: force_integer_mv.
void encodeMv(int force_integer_mv, AomWriter* w, const Mv& mv, const Mv& ref, NmvContext* mvctx,
              int usehp);

// svt_aom_get_ref_filter_type :1522-:1527 (exposed for the switchable-interp
// test surface; static in the reference).
InterpFilter getRefFilterType(const InterMbMi* ref_mi, int dir, std::int32_t ref_frame);

// svt_aom_get_pred_context_switchable_interp :1530-:1573.
int getPredContextSwitchableInterp(std::int32_t rf0, std::int32_t rf1, const InterMbD* xd, int dir);

// svt_aom_is_nontrans_global_motion :1575-:1596. The pcs wall is threaded:
// the global-motion wmtype per ref slot (gmWmtype[rf]).
int isNontransGlobalMotion(const InterMbMi* block_mi, BlockSize bsize, const int* gmWmtype);

// av1_is_interp_needed :1598-:1612.
int isInterpNeeded(const InterMbMi* block_mi, BlockSize bsize, const int* gmWmtype);

// write_mb_interp_filter :1614-:1631. The FrameHeader/EntropyCoder walls
// threaded: interpolation_filter, enable_dual_filter and the cdf pointer
// (the InterFc switchable_interp family).
void writeMbInterpFilter(AomWriter* w, InterFc* fc, BlockSize bsize, std::int32_t rf0,
                         std::int32_t rf1, int interpolation_filter, int enable_dual_filter,
                         const InterMbMi* mbmi, const InterMbD* xd, const int* gmWmtype);

// write_inter_compound_mode :1633-:1638.
void writeInterCompoundMode(AomWriter* w, InterFc* fc, PredictionMode mode, std::int16_t mode_ctx);

// == The reference-mode / comp-reference context family ==
// svt_aom_get_reference_mode_context_new :1839-:1881.
int getReferenceModeContext(const InterMbD* xd);

// svt_aom_get_reference_mode_cdf :1642-:1644.
AomCdfProb* getReferenceModeCdf(InterFc* fc, const InterMbD* xd);

// svt_aom_get_comp_reference_type_context_new :1701-:1770.
int getCompReferenceTypeContext(const InterMbD* xd);

// svt_aom_get_comp_reference_type_cdf :1656-:1659.
AomCdfProb* getCompReferenceTypeCdf(InterFc* fc, const InterMbD* xd);

// svt_av1_get_pred_context_uni_comp_ref_p/:p1/:p2 :1780-:1837.
int getPredContextUniCompRefP(const InterMbD* xd);
int getPredContextUniCompRefP1(const InterMbD* xd);
int getPredContextUniCompRefP2(const InterMbD* xd);

// The cdf getters :1661-:1674.
AomCdfProb* getPredCdfUniCompRefP(InterFc* fc, const InterMbD* xd);
AomCdfProb* getPredCdfUniCompRefP1(InterFc* fc, const InterMbD* xd);
AomCdfProb* getPredCdfUniCompRefP2(InterFc* fc, const InterMbD* xd);

// == Common context functions for both comp and single ref ==
// get_pred_context_ll2_or_l3gld :1918-:1931.
int getPredContextLl2OrL3Gld(const std::uint8_t* ref_counts);
// get_pred_context_last_or_last2 :1934-:1946.
int getPredContextLastOrLast2(const std::uint8_t* ref_counts);
// get_pred_context_last3_or_gld :1949-:1961.
int getPredContextLast3OrGld(const std::uint8_t* ref_counts);
// get_pred_context_brfarf2_or_arf :1965-:1976.
int getPredContextBrfArf2OrArf(const std::uint8_t* ref_counts);
// get_pred_context_brf_or_arf2 :1979-:1991.
int getPredContextBrfOrArf2(const std::uint8_t* ref_counts);

// == Context functions for comp ref == (the wrappers :1998-:2026)
int getPredContextCompRefP(const InterMbD* xd);
int getPredContextCompRefP1(const InterMbD* xd);
int getPredContextCompRefP2(const InterMbD* xd);
int getPredContextCompBwdrefP(const InterMbD* xd);
int getPredContextCompBwdrefP1(const InterMbD* xd);

// The comp-ref cdf getters :1676-:1698.
AomCdfProb* getPredCdfCompRefP(InterFc* fc, const InterMbD* xd);
AomCdfProb* getPredCdfCompRefP1(InterFc* fc, const InterMbD* xd);
AomCdfProb* getPredCdfCompRefP2(InterFc* fc, const InterMbD* xd);
AomCdfProb* getPredCdfCompBwdrefP(InterFc* fc, const InterMbD* xd);
AomCdfProb* getPredCdfCompBwdrefP1(InterFc* fc, const InterMbD* xd);

// == Context functions for single ref == (the getters :2032-:2100)
int getPredContextSingleRefP1(const InterMbD* xd);
int getPredContextSingleRefP2(const InterMbD* xd);
int getPredContextSingleRefP3(const InterMbD* xd);
int getPredContextSingleRefP4(const InterMbD* xd);
int getPredContextSingleRefP5(const InterMbD* xd);
int getPredContextSingleRefP6(const InterMbD* xd);

// The single-ref cdf getters :2047-:2069.
AomCdfProb* getPredCdfSingleRefP1(InterFc* fc, const InterMbD* xd);
AomCdfProb* getPredCdfSingleRefP2(InterFc* fc, const InterMbD* xd);
AomCdfProb* getPredCdfSingleRefP3(InterFc* fc, const InterMbD* xd);
AomCdfProb* getPredCdfSingleRefP4(InterFc* fc, const InterMbD* xd);
AomCdfProb* getPredCdfSingleRefP5(InterFc* fc, const InterMbD* xd);
AomCdfProb* getPredCdfSingleRefP6(InterFc* fc, const InterMbD* xd);

// svt_aom_collect_neighbors_ref_counts_new :1883-:1908.
void collectNeighborsRefCounts(InterMbD* xd);

// write_ref_frames :2104-:2187. The pcs wall is threaded:
// frm_hdr->reference_mode; the mbmi's bsize is carried.
void writeRefFrames(AomWriter* w, InterFc* fc, const InterMbD* xd, const InterMbMi* mbmi,
                    BlockSize bsize, int reference_mode);

// initDefaultInterFrameContext - the inter surface's COPY_CDF extract of
// svt_aom_init_mode_probs (cabac_context_model.c:734-:788): fills the
// InterFc families with the default tables (newmv/zeromv/refmv/drl/
// inter_compound_mode/motion_mode/obmc/comp_inter/single_ref/comp_ref_
// type/uni_comp_ref/comp_ref/comp_bwdref/compound_index/comp_group_idx/
// skip_mode/intra_inter/switchable_interp/nmvc).
void initDefaultInterFrameContext(InterFc* fc);

}  // namespace entropy
