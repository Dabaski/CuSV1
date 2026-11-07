// l16_rdo - the rate-estimation + RD machinery (A4, RE1; the A4
// golden-regeneration campaign's foundation slices).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/md_rate_estimation.c
// (1,016 nb) + md_rate_estimation.h (189 nb).
// LANDED (S1-S3, 2026-11-07): the cost core - av1_prob_cost (md_rate_
// estimation.h:131-138, formula-oracle verified), costSymbol
// (av1_cost_symbol md_rate_estimation.c:35-45), getSyntaxRateFromCdf
// (svt_aom_get_syntax_rate_from_cdf :50-69, both inv_map branches).
// Constants cited verbatim; deviations named at each site.
#pragma once

#include <cstdint>

namespace l16_rdo {

// ---------------------------------------------------------------------
// Probability plumbing, verbatim values.
// CDF_PROB_BITS/CDF_PROB_TOP (cabac_context_model.h:38-40 via l7_entropy/
// entropy.h:26-28); AOM_ICDF (cabac_context_model.h:41); CDF_SIZE
// (cabac_context_model.h:42); EC_MIN_PROB (bitstream_unit.h:87-88 via
// l7_entropy/entropy.h:38-39). Module boundary: l16_rdo cannot link
// av1_entropy, so these verbatim constants are carried (the l7-l13
// duplication precedent).
// ---------------------------------------------------------------------
#define CDF_PROB_BITS 15
#define CDF_PROB_TOP (1 << CDF_PROB_BITS)
#define AOM_ICDF(x) (CDF_PROB_TOP - (x))
#define CDF_SIZE(x) ((x) + 1)
#define EC_MIN_PROB 4
// The factor to scale from cost in bits to cost in av1_prob_cost units.
// (md_rate_estimation.h:29, verbatim.)
#define AV1_PROB_COST_SHIFT 9
// Cost of coding an n bit literal, using 128 (i.e. 50%) probability for
// each bit. (md_rate_estimation.h:31, verbatim.)
#define av1_cost_literal(n) ((n) * (1 << AV1_PROB_COST_SHIFT))

// AomCdfProb (cabac_context_model.h:31).
using AomCdfProb = std::uint16_t;

// ---------------------------------------------------------------------
// av1_prob_cost (md_rate_estimation.h:127-138, verbatim): the 128-entry
// cost table round(-log2(i/256) * 512) for i = 128..255, transcribed.
// ---------------------------------------------------------------------
extern const std::uint16_t av1ProbCost[128];

// av1_cost_symbol (md_rate_estimation.c:35-45): cost of a symbol with
// probability p15 / 2^15 in av1_prob_cost units (bits << 9).
std::int32_t costSymbol(AomCdfProb p15);

// svt_aom_get_syntax_rate_from_cdf (md_rate_estimation.c:50-69): fills
// costs[0..] with the per-symbol cost of the icdf array (terminating at
// the CDF_PROB_TOP entry); when inv_map is non-null the cost is written
// at costs[inv_map[i]] instead of costs[i].
void getSyntaxRateFromCdf(std::int32_t* costs, const AomCdfProb* cdf, const std::int32_t* invMap);

// ---------------------------------------------------------------------
// MV magnitude class plumbing (cabac_context_model.h:149-:223, verbatim
// values). Module boundary: l7_entropy does not carry these, so the
// minimal set is carried here with citations (the l7-l13 duplication
// precedent).
// ---------------------------------------------------------------------
#define MV_JOINTS 4           // cabac_context_model.h:153
#define MV_CLASSES 11         // cabac_context_model.h:171
#define CLASS0_BITS 1         // cabac_context_model.h:187
#define CLASS0_SIZE (1 << CLASS0_BITS)  // cabac_context_model.h:188
#define MV_OFFSET_BITS (MV_CLASSES + CLASS0_BITS - 2)  // cabac_context_model.h:189
#define MV_FP_SIZE 4          // cabac_context_model.h:191
#define MV_MAX_BITS (MV_CLASSES + CLASS0_BITS + 2)     // cabac_context_model.h:193
#define MV_MAX ((1 << MV_MAX_BITS) - 1)                // cabac_context_model.h:194
#define MV_VALS ((MV_MAX << 1) + 1)                    // cabac_context_model.h:195

// MvClassType (cabac_context_model.h:173-185, verbatim order).
enum MvClassType {
    MvClass_0 = 0,   /* (0, 2]    integer pel */
    MvClass_1,       /* (2, 4] */
    MvClass_2,       /* (4, 8] */
    MvClass_3,       /* (8, 16] */
    MvClass_4,       /* (16, 32] */
    MvClass_5,       /* (32, 64] */
    MvClass_6,       /* (64, 128] */
    MvClass_7,       /* (128, 256] */
    MvClass_8,       /* (256, 512] */
    MvClass_9,       /* (512, 1024] */
    MvClass_10,      /* (1024, 2048] */
};

// mv_class_base (md_rate_estimation.c:366-368).
std::int32_t mvClassBase(MvClassType c);
// svt_av1_get_mv_class (md_rate_estimation.c:370-376): class of z (= |mv|-1)
// and the within-class offset out.
MvClassType getMvClass(std::int32_t z, std::int32_t* offset);

// ---------------------------------------------------------------------
// MV magnitude plumbing (cabac_context_model.h:147-:223, verbatim values;
// the minimal struct carry - l16_rdo cannot link av1_entropy, the l7-l13
// duplication precedent).
// ---------------------------------------------------------------------

// MvSubpelPrecision (cabac_context_model.h:219-223, verbatim order).
enum MvSubpelPrecision {
    MvSubpelNone = -1,
    MvSubpelLowPrecision = 0,
    MvSubpelHighPrecision,
};

// NmvComponent (cabac_context_model.h:201-:210, verbatim layout).
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

// NmvContext (cabac_context_model.h:212-:215, verbatim layout).
struct NmvContext {
    AomCdfProb joints_cdf[CDF_SIZE(MV_JOINTS)];
    NmvComponent comps[2];
};

// build_nmv_component_cost_table (md_rate_estimation.c:378-435): fills
// mvcost[-MV_MAX..MV_MAX] (the caller passes &array[MV_MAX]) with the
// per-component cost of each magnitude at the given precision.
void buildNmvComponentCostTable(std::int32_t* mvcost, const NmvComponent* mvcomp,
                                MvSubpelPrecision precision);

// ---------------------------------------------------------------------
// Token cost table plumbing (md_rate_estimation.c:486-584 +
// md_rate_estimation.h:33-44, the S7 span). Constants verbatim from
// cabac_context_model.h (the l7_entropy port carries the same values:
// entropy.h:453-:465).
// ---------------------------------------------------------------------
#define TX_SIZES 4          // cabac_context_model.h (TX_4X4..TX_32X32 + 1)
#define TX_32X32 3          // the [TX_32X32 + 1] br-cdf clamp bound
#define TX_4X4 0            // the ext-tx walk's start bound (definitions.h:955)
#define PLANE_TYPES 2       // l7 entropy.h:496 PlaneType enum
#define TXB_SKIP_CONTEXTS 13      // cabac_context_model.h
#define SIG_COEF_CONTEXTS_2D 26   // cabac_context_model.h:113
#define SIG_COEF_CONTEXTS_1D 16   // cabac_context_model.h:114
#define SIG_COEF_CONTEXTS (SIG_COEF_CONTEXTS_2D + SIG_COEF_CONTEXTS_1D)  // :116 = 42
#define SIG_COEF_CONTEXTS_EOB 4   // cabac_context_model.h
#define EOB_COEF_CONTEXTS 9       // cabac_context_model.h
#define DC_SIGN_CONTEXTS 3        // cabac_context_model.h
#define LEVEL_CONTEXTS 21         // cabac_context_model.h
#define BR_CDF_SIZE 4             // cabac_context_model.h
#define COEFF_BASE_RANGE (4 * (BR_CDF_SIZE - 1))  // :12

// LvMapEobCost (md_rate_estimation.h:33-35, verbatim layout).
struct LvMapEobCost {
    std::int32_t eobCost[2][11];
};

// LvMapCoeffCost (md_rate_estimation.h:37-44, verbatim layout).
struct LvMapCoeffCost {
    std::int32_t txbSkipCost[TXB_SKIP_CONTEXTS][2];
    std::int32_t baseEobCost[SIG_COEF_CONTEXTS_EOB][3];
    std::int32_t baseCost[SIG_COEF_CONTEXTS][8];
    std::int32_t eobExtraCost[EOB_COEF_CONTEXTS][2];
    std::int32_t dcSignCost[DC_SIGN_CONTEXTS][2];
    std::int32_t lpsCost[LEVEL_CONTEXTS][COEFF_BASE_RANGE + 1 + COEFF_BASE_RANGE + 1];
};

// The token-cdf families the reference's estimate_coefficients_rate reads
// (FRAME_CONTEXT slice: our l7 EcFrameContext carries the same families at
// entropy.h:562-574; module boundary forbids the link, so the minimal carry
// is defined here - the l7-l13 duplication precedent). CDF_SIZE(n) rows:
// n symbols + the 0 terminator.
struct FcTokenCdfs {
    AomCdfProb eobFlagCdf16[PLANE_TYPES][2][CDF_SIZE(5)];
    AomCdfProb eobFlagCdf32[PLANE_TYPES][2][CDF_SIZE(6)];
    AomCdfProb eobFlagCdf64[PLANE_TYPES][2][CDF_SIZE(7)];
    AomCdfProb eobFlagCdf128[PLANE_TYPES][2][CDF_SIZE(8)];
    AomCdfProb eobFlagCdf256[PLANE_TYPES][2][CDF_SIZE(9)];
    AomCdfProb eobFlagCdf512[PLANE_TYPES][2][CDF_SIZE(10)];
    AomCdfProb eobFlagCdf1024[PLANE_TYPES][2][CDF_SIZE(11)];
    AomCdfProb txbSkipCdf[TX_SIZES][TXB_SKIP_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb coeffBaseEobCdf[TX_SIZES][PLANE_TYPES][SIG_COEF_CONTEXTS_EOB][CDF_SIZE(3)];
    AomCdfProb coeffBaseCdf[TX_SIZES][PLANE_TYPES][SIG_COEF_CONTEXTS][CDF_SIZE(4)];
    AomCdfProb eobExtraCdf[TX_SIZES][PLANE_TYPES][EOB_COEF_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb dcSignCdf[PLANE_TYPES][DC_SIGN_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb coeffBrCdf[TX_32X32 + 1][PLANE_TYPES][LEVEL_CONTEXTS][CDF_SIZE(BR_CDF_SIZE)];
};


// ---------------------------------------------------------------------
// S8: the syntax-rate plumbing (md_rate_estimation.c:76-364) +
// S9: the interintra-allowed helpers (mode_decision.h:142-155).
// Constants verbatim; enums mirror the reference order (the l7_entropy
// port carries the same values; module boundary forbids the link, the
// l7-l13 duplication precedent).
// ---------------------------------------------------------------------
#define KF_MODE_CONTEXTS 5        // cabac_context_model.h:262
#define BlockSize_GROUPS 4        // cabac_context_model.h:227
#define SWITCHABLE_FILTERS 3      // definitions.h:824 (BILINEAR)
#define SWITCHABLE_FILTER_CONTEXTS 16  // definitions.h:328 = (3+1)*4
#define CFL_ALPHABET_SIZE 16      // definitions.h:1113
#define CFL_PRED_U 0              // definitions.h:1117 CflPredType
#define CFL_PRED_V 1
#define CFL_PRED_PLANES 2
#define CFL_SIGNS 3               // definitions.h:1119
#define CFL_JOINT_SIGNS 8         // definitions.h:1124 = 3*3-1
#define CFL_SIGN_U(js) ((((js) + 1) * 11) >> 5)   // definitions.h:1126
#define CFL_SIGN_V(js) (((js) + 1) - CFL_SIGNS * CFL_SIGN_U(js))  // :1128
#define CFL_SIGN_ZERO 0           // definitions.h:1119 enum order
#define CFL_CONTEXT_U(js) ((js) + 1 - CFL_SIGNS)  // definitions.h:1133
#define CFL_CONTEXT_V(js) (CFL_SIGN_V(js) * CFL_SIGNS + CFL_SIGN_U(js) - CFL_SIGNS)  // :1135
#define CFL_ALPHA_CONTEXTS (CFL_JOINT_SIGNS + 1 - CFL_SIGNS)  // definitions.h:1132
#define CFL_ALLOWED_TYPES 2       // definitions.h:1121
#define MAX_TX_CATS 3             // definitions.h:407 (TX_SIZES 4 - 1)
#define TX_SIZE_CONTEXTS 3        // cabac_context_model.h:229
#define MAX_TX_DEPTH 2            // definitions.h:408
#define TXFM_PARTITION_CONTEXTS 15  // definitions.h:1353 = (4-1)*6-3
#define RESTORE_SWITCHABLE_TYPES 3  // restoration enums (RESTORE_NONE..)
#define INTRA_INTER_CONTEXTS 4      // definitions.h:1346
#define INTRA_FRAME 0               // definitions.h:1359 MvReferenceFrame
// The single-inter mode block (definitions.h:1198-1199): isInterIntraAllowedMode's range.
#define SINGLE_INTER_MODE_START NEARESTMV
#define SINGLE_INTER_MODE_END NEAREST_NEARESTMV

// BLOCK_SIZES_ALL (definitions.h:883-905, verbatim order; the l7_entropy
// port carries the same 22-entry enum, entropy.h:180-203).
enum BlockSize {
    BLOCK_4X4, BLOCK_4X8, BLOCK_8X4, BLOCK_8X8, BLOCK_8X16, BLOCK_16X8,
    BLOCK_16X16, BLOCK_16X32, BLOCK_32X16, BLOCK_32X32, BLOCK_32X64,
    BLOCK_64X32, BLOCK_64X64, BLOCK_64X128, BLOCK_128X64, BLOCK_128X128,
    BLOCK_4X16, BLOCK_16X4, BLOCK_8X32, BLOCK_32X8, BLOCK_16X64, BLOCK_64X16,
    BLOCK_SIZES_ALL,
};

// PredictionMode - the intra + single-inter subset (definitions.h:1167-1206;
// INTRA_MODES = 13, NEARESTMV = 13, the single-inter block ends at
// NEAREST_NEARESTMV = 17).
enum PredictionMode {
    DC_PRED, V_PRED, H_PRED, D45_PRED, D135_PRED, D113_PRED, D157_PRED,
    D203_PRED, D67_PRED, SMOOTH_PRED, SMOOTH_V_PRED, SMOOTH_H_PRED, PAETH_PRED,
    NEARESTMV, NEARMV, GLOBALMV, NEWMV,
    NEAREST_NEARESTMV, NEAR_NEARMV, NEAREST_NEWMV, NEW_NEARESTMV, NEAR_NEWMV,
    NEW_NEARMV, GLOBAL_GLOBALMV, NEW_NEWMV,
    MB_MODE_COUNT,
    INTRA_MODES = PAETH_PRED + 1,
};

// PartitionType (definitions.h:911-925, verbatim order).
enum PartitionType {
    PARTITION_NONE, PARTITION_HORZ, PARTITION_VERT, PARTITION_SPLIT,
    PARTITION_HORZ_A, PARTITION_HORZ_B, PARTITION_VERT_A, PARTITION_VERT_B,
    PARTITION_HORZ_4, PARTITION_VERT_4, EXT_PARTITION_TYPES,
    PARTITION_TYPES = PARTITION_SPLIT + 1,  // definitions.h:923
};
#define PARTITION_CONTEXTS 20  // definitions.h:943
#define SKIP_CONTEXTS 3        // definitions.h:1313

// MvReferenceFrame subset for the ref helper (definitions.h:1358-1360).
enum MvRefFrame { MV_INTRA_FRAME = INTRA_FRAME, MV_LAST_FRAME };

// block_size_wide/high (common_utils.c:286-291, verbatim values; the width
// column). Used by filterIntraAllowedBsize (mode_decision.c:108-113).
extern const std::uint8_t blockSizeWide[BLOCK_SIZES_ALL];
extern const std::uint8_t blockSizeHigh[BLOCK_SIZES_ALL];

// svt_aom_filter_intra_allowed_bsize (mode_decision.c:108-113): the
// CONFIG_ENABLE_FILTER_INTRA guard is threaded as the parameter (our
// emission's pic_filter_intra_level value drives it).
std::int32_t filterIntraAllowedBsize(std::int32_t enable, BlockSize bs);

// The S9 interintra-allowed helpers (mode_decision.h:142-155): the
// CONFIG_ENABLE_INTER_INTRA guard on bsize is threaded as the parameter.
std::int32_t isInterIntraAllowedBsize(std::int32_t enable, BlockSize bs);
std::int32_t isInterIntraAllowedMode(PredictionMode mode);
std::int32_t isInterIntraAllowedRef(const std::int32_t rf[2]);

// eb_num_pels_log2_lookup (common_utils.c:39-40, verbatim values).
extern const std::uint8_t ebNumPelsLog2Lookup[BLOCK_SIZES_ALL];

// svt_aom_partition_cdf_length (entropy_coding.c:922-930).
std::int32_t partitionCdfLength(BlockSize bsize);
// svt_aom_allow_intrabc (entropy_coding.c:4397-4399): the reference reads
// frm_hdr->allow_screen_content_tools / frm_hdr->allow_intrabc - threaded
// as parameters (named deviation: the FrameHeader wall).
std::int32_t allowIntrabc(std::int32_t allowScreenContentTools, std::int32_t allowIntrabc);
// svt_aom_allow_palette (entropy_coding.c:4218-4222): the reference
// const-folds via CONFIG_ENABLE_PALETTE (=0 in the pinned build) - threaded
// as the parameter (named deviation; the pinned build's behavior is the
// enable=0 case).
std::int32_t allowPalette(std::int32_t enable, std::int32_t allowScreenContentTools, BlockSize bsize);
// svt_aom_get_palette_bsize_ctx (entropy_coding.c:4224-4226).
std::int32_t getPaletteBsizeCtx(BlockSize bsize);

// ---------------------------------------------------------------------
// S8: the syntax-rate tables + the token cdf carry. The shared constants
// (SKIP_CONTEXTS, CFL_*, TX_SIZE_CONTEXTS, ...) are defined in the S8
// section above; only the ext-tx family is new here.
// ---------------------------------------------------------------------
#define EXT_TX_SIZES 4             // definitions.h:1102
#define EXT_TX_SETS_INTRA 3        // definitions.h:1104
#define EXT_TX_SETS_INTER 4        // definitions.h:1103
#define EXT_TX_SET_TYPES 6         // definitions.h:1084
#define TX_TYPES 16                // definitions.h (the 16-type list)
#define UV_INTRA_MODES 14          // definitions.h:1225 (13 UV modes + UV_CFL_PRED)
#define FILTER_INTRA_MODES 7       // definitions.h:1361 enum
#define PALETTE_SIZES 7            // definitions.h:1137-1147 (TWO..EIGHT)
#define PALETTE_Y_MODE_CONTEXTS 3  // cabac_context_model.h:246
#define PALETTE_UV_MODE_CONTEXTS 2 // cabac_context_model.h
#define PALETTE_COLORS 8           // definitions.h:1146
#define PALETTE_COLOR_INDEX_CONTEXTS 5  // cabac_context_model.h:238
#define PALATTE_BSIZE_CTXS 7       // cabac_context_model.h:242
#define DIRECTIONAL_MODES 8        // definitions.h:1305
#define MAX_ANGLE_DELTA 3          // definitions.h:1306

// TxSetType (definitions.h:1071-1085, verbatim order).
enum TxSetType {
    EXT_TX_SET_DCTONLY,
    EXT_TX_SET_DCT_IDTX,
    EXT_TX_SET_DTT4_IDTX,
    EXT_TX_SET_DTT4_IDTX_1DDCT,
    EXT_TX_SET_DTT9_IDTX_1DDCT,
    EXT_TX_SET_ALL16,
};

// use_inter_ext_tx_for_txsize (md_rate_estimation.h:139-146, verbatim).
extern const std::int32_t useInterExtTxForTxsize[EXT_TX_SETS_INTER][EXT_TX_SIZES];
// use_intra_ext_tx_for_txsize (md_rate_estimation.h:145-152, verbatim).
extern const std::int32_t useIntraExtTxForTxsize[EXT_TX_SETS_INTRA][EXT_TX_SIZES];
// av1_ext_tx_set_idx_to_type (md_rate_estimation.h:151-168, verbatim).
extern const TxSetType av1ExtTxSetIdxToType[2][4];
// av1_ext_tx_inv (cabac_context_model.c:43-50, verbatim transcription).
extern const std::int32_t av1ExtTxInv[EXT_TX_SET_TYPES][TX_TYPES];

// MdRateEstimationContext (md_rate_estimation.h:49-125) - the MINIMAL
// carry: only the fields the landed spans write (S7: the token tables;
// S8: the syntax-rate tables below). The inter-only families (md_rate_
// estimation.c:270-:363) are named-PARKED - our emission is INTRA-ONLY.
struct MdRateEstimationContext {
    // S7: token tables (the reference's :486-:584 fields).
    LvMapCoeffCost coeffFacBits[TX_SIZES][PLANE_TYPES];
    LvMapEobCost   eobFracBits[7][2];
    // S8: syntax-rate tables (the reference's :92-:256 fields; the
    // inter-only families :271-:357 stay parked with the slice).
    std::int32_t partitionFacBits[20][10];
    std::int32_t partitionVertAlikeFacBits[20][2];
    std::int32_t partitionHorzAlikeFacBits[20][2];
    std::int32_t partitionVertAlike128FacBits[20][2];
    std::int32_t partitionHorzAlike128FacBits[20][2];
    std::int32_t skipModeFacBits[SKIP_CONTEXTS][2];
    std::int32_t skipFacBits[SKIP_CONTEXTS][2];
    std::int32_t yModeFacBits[KF_MODE_CONTEXTS][KF_MODE_CONTEXTS][INTRA_MODES];
    std::int32_t mbModeFacBits[4][INTRA_MODES];
    std::int32_t intraUvModeFacBits[CFL_ALLOWED_TYPES][INTRA_MODES][UV_INTRA_MODES];
    std::int32_t filterIntraFacBits[BLOCK_SIZES_ALL][2];
    std::int32_t filterIntraModeFacBits[FILTER_INTRA_MODES];
    std::int32_t switchableInterpFacBits[SWITCHABLE_FILTER_CONTEXTS][SWITCHABLE_FILTERS];
    std::int32_t paletteYsizeFacBits[PALATTE_BSIZE_CTXS][PALETTE_SIZES];
    std::int32_t paletteUvSizeFacBits[PALATTE_BSIZE_CTXS][PALETTE_SIZES];
    std::int32_t paletteYcolorFacBits[PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS][PALETTE_COLORS];
    std::int32_t paletteUvColorFacBits[PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS][PALETTE_COLORS];
    std::int32_t paletteYmodeFacBits[PALATTE_BSIZE_CTXS][PALETTE_Y_MODE_CONTEXTS][2];
    std::int32_t paletteUvModeFacBits[PALETTE_UV_MODE_CONTEXTS][2];
    std::int32_t cflAlphaFacBits[CFL_JOINT_SIGNS][CFL_PRED_PLANES][CFL_ALPHABET_SIZE];
    std::int32_t txSizeFacBits[MAX_TX_CATS][TX_SIZE_CONTEXTS][MAX_TX_DEPTH + 1];
    std::int32_t txfmPartitionFacBits[TXFM_PARTITION_CONTEXTS][2];
    std::int32_t intraTxTypeFacBits[EXT_TX_SETS_INTRA][EXT_TX_SIZES][INTRA_MODES][TX_TYPES];
    std::int32_t interTxTypeFacBits[EXT_TX_SETS_INTER][EXT_TX_SIZES][TX_TYPES];
    std::int32_t angleDeltaFacBits[DIRECTIONAL_MODES][2 * MAX_ANGLE_DELTA + 1];
    std::int32_t switchableRestoreFacBits[RESTORE_SWITCHABLE_TYPES];
    std::int32_t wienerRestoreFacBits[2];
    std::int32_t sgrprojRestoreFacBits[2];
    std::int32_t intrabcFacBits[2];
    bool         initialized;
};

// FcSyntaxCdfs: the syntax-cdf families the reference's estimate_syntax_rate
// READS LIVE (:76-:364 minus the CONFIG-dead fills). The pinned build's
// CONFIG (EbConfigMacros.h:59/:77/:83) forces pic_filter_intra_level /
// allow_screen_content_tools / enable_restoration to 0, DCE-ing the FI
// (:167-175), palette (:180-184) and restoration (:260-262) fills - the
// port mirrors that DCE by omitting those families from the carry. The
// families that landed in l7_entropy are carried with the SAME dims
// (entropy.h:555-:598); the rest straight from cabac_context_model.h
// (:261-:345). Module boundary: l16_rdo cannot link av1_entropy (the
// l7-l13 duplication precedent).
struct FcSyntaxCdfs {
    AomCdfProb partition_cdf[PARTITION_CONTEXTS][CDF_SIZE(EXT_PARTITION_TYPES)];
    AomCdfProb skip_mode_cdfs[SKIP_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb skip_cdfs[SKIP_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb kf_y_cdf[KF_MODE_CONTEXTS][KF_MODE_CONTEXTS][CDF_SIZE(INTRA_MODES)];
    AomCdfProb y_mode_cdf[4][CDF_SIZE(INTRA_MODES)];   // BlockSize_GROUPS
    AomCdfProb uv_mode_cdf[CFL_ALLOWED_TYPES][INTRA_MODES][CDF_SIZE(UV_INTRA_MODES)];
    AomCdfProb switchable_interp_cdf[SWITCHABLE_FILTER_CONTEXTS][CDF_SIZE(SWITCHABLE_FILTERS)];
    AomCdfProb cfl_sign_cdf[CDF_SIZE(CFL_JOINT_SIGNS)];
    AomCdfProb cfl_alpha_cdf[CFL_ALPHA_CONTEXTS][CDF_SIZE(CFL_ALPHABET_SIZE)];
    AomCdfProb tx_size_cdf[MAX_TX_CATS][TX_SIZE_CONTEXTS][CDF_SIZE(MAX_TX_DEPTH + 1)];
    AomCdfProb txfm_partition_cdf[TXFM_PARTITION_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb intra_ext_tx_cdf[EXT_TX_SETS_INTRA][EXT_TX_SIZES][INTRA_MODES][CDF_SIZE(TX_TYPES)];
    AomCdfProb inter_ext_tx_cdf[EXT_TX_SETS_INTER][EXT_TX_SIZES][CDF_SIZE(TX_TYPES)];
    AomCdfProb angle_delta_cdf[DIRECTIONAL_MODES][CDF_SIZE(2 * MAX_ANGLE_DELTA + 1)];
    AomCdfProb intrabc_cdf[CDF_SIZE(2)];
    // The FI families (cabac_context_model.h:329-:330): carried because the
    // port threads the FI guard as a runtime parameter (the pinned build's
    // CONFIG_ENABLE_FILTER_INTRA=0 DCEs the fill; see the function comment).
    AomCdfProb filter_intra_cdfs[BLOCK_SIZES_ALL][CDF_SIZE(2)];
    AomCdfProb filter_intra_mode_cdf[CDF_SIZE(FILTER_INTRA_MODES)];
    // The palette families (cabac_context_model.h:306-:311): same runtime-
    // guard carry as the FI families above (the pinned build's
    // CONFIG_ENABLE_PALETTE=0 DCEs the fill).
    AomCdfProb palette_y_size_cdf[PALATTE_BSIZE_CTXS][CDF_SIZE(PALETTE_SIZES)];
    AomCdfProb palette_uv_size_cdf[PALATTE_BSIZE_CTXS][CDF_SIZE(PALETTE_SIZES)];
    AomCdfProb palette_y_color_index_cdf[PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS][CDF_SIZE(PALETTE_COLORS)];
    AomCdfProb palette_uv_color_index_cdf[PALETTE_SIZES][PALETTE_COLOR_INDEX_CONTEXTS][CDF_SIZE(PALETTE_COLORS)];
    AomCdfProb palette_y_mode_cdf[PALATTE_BSIZE_CTXS][PALETTE_Y_MODE_CONTEXTS][CDF_SIZE(2)];
    AomCdfProb palette_uv_mode_cdf[PALETTE_UV_MODE_CONTEXTS][CDF_SIZE(2)];
    // The restoration families (cabac_context_model.h:331-:333): same
    // runtime-guard carry as the FI/palette families above (the pinned
    // build's CONFIG_ENABLE_RESTORATION=0 DCEs the fill).
    AomCdfProb switchable_restore_cdf[CDF_SIZE(RESTORE_SWITCHABLE_TYPES)];
    AomCdfProb wiener_restore_cdf[CDF_SIZE(2)];
    AomCdfProb sgrproj_restore_cdf[CDF_SIZE(2)];
};

// svt_aom_estimate_coefficients_rate (md_rate_estimation.c:486-584): fills
// me's token cost arrays from the fc token cdfs. nplanes = AOMMIN(3,
// PLANE_TYPES) = 2; the br-cdf index clamps tx_size to TX_32X32.
void estimateCoefficientsRate(MdRateEstimationContext* me, const FcTokenCdfs* fc);

// svt_aom_estimate_syntax_rate (md_rate_estimation.c:76-:364): the per-symbol
// cost init. The is_i_slice=true path is the port's scope (the reference
// hardcodes it); the inter block :270-:363 is PARKED (our emission is
// intra-only). The gating flags mirror the reference's parameter surface;
// the pinned CONFIG DCEs the first three (see FcSyntaxCdfs above).
void estimateSyntaxRate(MdRateEstimationContext* me, std::uint8_t pic_filter_intra_level,
                        std::uint8_t allow_screen_content_tools, std::uint8_t enable_restoration,
                        std::uint8_t allow_intrabc, const FcSyntaxCdfs* fc);

}
