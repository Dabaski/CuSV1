#pragma once

// AMVP1 (A3 campaign): the AMVP candidate machinery of
// third_party/SVT-AV1/Source/Lib/Codec/adaptive_mv_pred.c (2,038 physical /
// 1,807 non-blank lines). Struct-free port: every wall the ported spans
// consume (PictureControlSet / Av1Common / MacroBlockD / MbModeInfo /
// FrameHeader / ModeDecisionContext / EbReferenceObject) is mirrored as a
// carrier struct holding ONLY the consumed fields (the RE8A DeblockContext
// precedent); the function bodies are verbatim.
//
// PORTED (spans + nb, span-measured 2026-11-12):
//   :40-:47    get_block_mv + is_inside                    7 nb
//   :49-:55    clamp_mv_ref                                7 nb
//   :57-:128   add_ref_mv_candidate                       70 nb
//   :130-:184  scan_row_mbmi                              51 nb
//   :186-:239  scan_col_mbmi                              50 nb
//   :241-:264  scan_blk_mbmi                              22 nb
//   :266-:325  has_top_right                              52 nb
//   :327-:333  find_valid_row/col_offset                   6 nb
//   :335-:350  get_relative_dist                          13 nb
//   :352-:447  add_tpl_ref_mv                             87 nb
//   :450-:465  sort_mvp_table                             16 nb
//   :469-:648  scan_row_col_light                        162 nb
//   :651-:971  setup_ref_mv_list                         282 nb
//   :973-:981  block_center_x/y                            8 nb
//   :983-:1036 svt_aom_gm_get_motion_vector_enc           48 nb
//   :1128-:1136 count_ref_match                            9 nb
//   :1138-:1327 svt_aom_compute_inter_mode_ctx_light     178 nb
//   :1329-:1405 svt_aom_generate_av1_mvp_table           70 nb
//   :1407-:1457 svt_aom_get_av1_mv_pred_drl              45 nb
//   :1594-:1750 record_samples + av1_find_samples         135 nb
//   :1830-:1906 overlappable counters                     64 nb
//   :1908-:2000 svt_aom_is_dv_valid                       85 nb
//   :2002-:2038 ref-mv-from-stack + find-best             35 nb
//   (= 1,523 nb ported)
// PARKED (named, spans + nb + reason):
//   :1038-:1123 svt_aom_init_xd                       74 nb - the mi-grid
//     ownership wall: writes pcs->mi_grid_base / pcs->mip and reads
//     ctx->shape / blk_geom; the grid belongs to the PCS/MD framework.
//   :1459-:1490 svt_aom_update_mi_map_enc_dec         29 nb - BlkStruct /
//     segmentation / palette mi-map mutation walls.
//   :1492-:1520 svt_copy_mi_map_grid_c                29 nb - same grid wall.
//   :1522-:1539 get_mbmi                              14 nb - same grid wall.
//   :1541-:1592 svt_aom_update_mi_map                 46 nb - same grid wall.
//   :1752-:1774 svt_aom_init_wm_samples               23 nb - ctx->
//     wm_sample_info wall.
//   :1776-:1827 svt_aom_warped_motion_parameters      47 nb - calls
//     svt_aom_select_samples + svt_find_projection (warped_motion.c), which
//     are NOT ported (grep-verified 2026-11-12: no warped-motion port in
//     src/; only an l8 bitstream comment references enable_warped_motion).
//   (= 262 nb parked; 1,523 + 262 = 1,785; the 22 nb remainder is blank
//   lines inside spans and the two forward declarations :25-:28.)
//
// The build config is the DEFAULT (non-RTC) tree: RTC_BUILD=0 =>
// CONFIG_ENABLE_GLOBAL_MOTION / CONFIG_ENABLE_INTER_COMPOUND /
// CONFIG_ENABLE_INTRA_BC / CONFIG_ENABLE_TPL are all 1
// (third_party/SVT-AV1/Source/API/EbConfigMacros.h:182-230), so the
// is_global_mv_block gate and the is_intrabc_block term are LIVE here.
// The lookup tables the ported spans consume are carried locally with the
// same citation discipline as the RE8A driver.

#include <cstdint>

namespace motion {
namespace amvp {

// ---------------------------------------------------------------------------
// Constants (each cited to its reference definition)
// ---------------------------------------------------------------------------

// definitions.h:353-354: MI_SIZE_LOG2 = 2, MI_SIZE = 4.
constexpr std::int32_t kMiSizeLog2 = 2;
constexpr std::int32_t kMiSize = 4;  // 1 << kMiSizeLog2

// inter_prediction.h:31.
constexpr std::int32_t kMvBorder = 16 << 3;

// cabac_context_model.h:197-199.
constexpr std::int32_t kMvInUseBits = 14;
constexpr std::int32_t kMvLow = -(1 << kMvInUseBits);
constexpr std::int32_t kMvUpp = 1 << kMvInUseBits;

// mv.h:21.
constexpr std::uint32_t kInvalidMv = 0x80008000;

// definitions.h:1341, :1343, :1344.
constexpr int kMaxMvRefCandidates = 2;
constexpr int kMaxRefMvStackSize = 8;
constexpr int kRefCatLevel = 640;

// definitions.h:1324-1325.
constexpr int kGlobalMvOffset = 3;
constexpr int kRefMvOffset = 4;

// definitions.h:383-384: FRAME_OFFSET_BITS = 5.
constexpr int kMaxFrameDistance = (1 << 5) - 1;

// definitions.h:447-449.
constexpr int kLeastSquaresSamplesMaxBits = 3;
constexpr int kLeastSquaresSamplesMax = 1 << kLeastSquaresSamplesMaxBits;
constexpr int kSamplesArraySize = kLeastSquaresSamplesMax * 2;

// definitions.h:729-731.
constexpr int kMiSizeW8X8 = 2;
constexpr int kMiSizeW16X16 = 4;
constexpr int kMiSizeW64X64 = 16;

// adaptive_mv_pred.c:30-31.
constexpr int kMvrefRows = 3;
constexpr int kMvrefCols = 3;

// inter_prediction.h:35-36.
constexpr int kIntrabcDelayPixels = 256;
constexpr int kIntrabcDelaySb64 = kIntrabcDelayPixels / 64;

// definitions.h:1692; :1726.
constexpr int kWarpedModelPrecBits = 16;
constexpr int kGmTransOnlyPrecDiff = kWarpedModelPrecBits - 3;

// EbSvtAv1Enc.h:134: PredStructure { ALL_INTRA, LOW_DELAY, RANDOM_ACCESS, .. }.
constexpr int kPredStructureRandomAccess = 2;

// ---------------------------------------------------------------------------
// Enum ordinals (definitions.h order; the l7/l10/l16 ports share them)
// ---------------------------------------------------------------------------

// definitions.h:882-909.
enum BlockSize {
    BLOCK_4X4 = 0,
    BLOCK_4X8,
    BLOCK_8X4,
    BLOCK_8X8,
    BLOCK_8X16,
    BLOCK_16X8,
    BLOCK_16X16,
    BLOCK_16X32,
    BLOCK_32X16,
    BLOCK_32X32,
    BLOCK_32X64,
    BLOCK_64X32,
    BLOCK_64X64,
    BLOCK_64X128,
    BLOCK_128X64,
    BLOCK_128X128,
    BLOCK_4X16,
    BLOCK_16X4,
    BLOCK_8X32,
    BLOCK_32X8,
    BLOCK_16X64,
    BLOCK_64X16,
    BLOCK_SIZES_ALL,
    BLOCK_INVALID = 255,
};

// definitions.h:1167-1206.
enum PredictionMode {
    DC_PRED = 0,
    V_PRED,
    H_PRED,
    D45_PRED,
    D135_PRED,
    D113_PRED,
    D157_PRED,
    D203_PRED,
    D67_PRED,
    SMOOTH_PRED,
    SMOOTH_V_PRED,
    SMOOTH_H_PRED,
    PAETH_PRED,
    NEARESTMV,
    NEARMV,
    GLOBALMV,
    NEWMV,
    NEAREST_NEARESTMV,
    NEAR_NEARMV,
    NEAREST_NEWMV,
    NEW_NEARESTMV,
    NEAR_NEWMV,
    NEW_NEARMV,
    GLOBAL_GLOBALMV,
    NEW_NEWMV,
    MB_MODE_COUNT,
};

// definitions.h:1357-1386 (frame-type enum): NONE_FRAME = -1, INTRA_FRAME = 0
// ... ALTREF_FRAME = 7, REF_FRAMES = 8 == LAST_BWD_FRAME.
constexpr int kNoneFrame = -1;
constexpr int kIntraFrame = 0;
constexpr int kLastFrame = 1;
constexpr int kLast2Frame = 2;
constexpr int kLast3Frame = 3;
constexpr int kGoldenFrame = 4;
constexpr int kBwdrefFrame = 5;
constexpr int kAltref2Frame = 6;
constexpr int kAltrefFrame = 7;
constexpr int kRefFrames = 8;
constexpr int kLastBwdFrame = 8;
constexpr int kTotalRefsPerFrame = 8;
constexpr int kFwdRefs = 4;
constexpr int kBwdRefs = 3;
constexpr int kTotalUnidirCompRefs = 9;
constexpr int kTotalCompRefs = kFwdRefs * kBwdRefs + kTotalUnidirCompRefs;
constexpr int kModeCtxRefFrames = kTotalRefsPerFrame + kTotalCompRefs;

// definitions.h:911-925.
enum PartitionType {
    PARTITION_NONE = 0,
    PARTITION_HORZ,
    PARTITION_VERT,
    PARTITION_SPLIT,
    PARTITION_HORZ_A,
    PARTITION_HORZ_B,
    PARTITION_VERT_A,
    PARTITION_VERT_B,
    PARTITION_HORZ_4,
    PARTITION_VERT_4,
    EXT_PARTITION_TYPES,
};

// definitions.h:1740-1746.
enum TransformationType { IDENTITY = 0, TRANSLATION = 1, ROTZOOM = 2, AFFINE = 3, TRANS_TYPES };

// ---------------------------------------------------------------------------
// Mv / CandidateMv (mv.h:42-55, verbatim)
// ---------------------------------------------------------------------------

// The nameless struct inside the union is the reference MV's own shape
// (mv.h:42-49); MSVC C4201 fires on it under /WX, so the extension is
// silenced at this one site. Naming the struct would change the field
// access (mv.x / mv.y) - the pragma is the faithful option.
#pragma warning(push)
#pragma warning(disable : 4201)
union Mv {
    struct {
        std::int16_t x;
        std::int16_t y;
    };
    std::uint32_t as_int;  // faster equality tests and copies
};
#pragma warning(pop)

struct CandidateMv {
    Mv this_mv;
    Mv comp_mv;
    std::int32_t weight;
};

// ---------------------------------------------------------------------------
// Carrier structs: mirrors of the consumed reference fields only
// ---------------------------------------------------------------------------

// TileInfo (block_structures.h:27-34): the four mi bounds the port reads.
struct TileInfo {
    std::int32_t mi_row_start;
    std::int32_t mi_row_end;
    std::int32_t mi_col_start;
    std::int32_t mi_col_end;
};

// BlockModeInfo (block_structures.h:36-95): the consumed surface - mode,
// mv[2], ref_frame[2] and the use_intrabc bit is_inter_block reads
// (CONFIG_ENABLE_INTRA_BC=1 in the default tree, block_structures.h:118-122).
struct BlockModeInfo {
    int mode;
    Mv mv[2];
    int ref_frame[2];
    unsigned use_intrabc : 1;
};

// MbModeInfo (block_structures.h:97-104): block_mi + bsize + partition.
struct MbModeInfo {
    BlockModeInfo block_mi;
    int bsize;
    int partition;
};

// MacroBlockD (coding_unit.h:61-90 window): every field a ported span reads.
struct MacroBlockD {
    TileInfo tile;
    MbModeInfo** mi;
    std::int32_t mi_stride;
    std::int32_t mi_row;
    std::int32_t mi_col;
    std::int32_t n4_w;
    std::int32_t n4_h;
    std::int32_t n8_w;
    std::int32_t n8_h;
    std::int32_t up_available;
    std::int32_t left_available;
    std::int32_t is_sec_rect;
    std::int32_t mb_to_top_edge;
    std::int32_t mb_to_bottom_edge;
    std::int32_t mb_to_left_edge;
    std::int32_t mb_to_right_edge;
    std::uint8_t ref_mv_count[kModeCtxRefFrames];  // coding_unit.h:85
};

// Av1Common (av1_common.h:58-64): the consumed surface.
struct Av1Common {
    std::int32_t mi_rows;
    std::int32_t mi_cols;
    std::int32_t ref_frame_sign_bias[kRefFrames];
    std::int32_t mi_stride;
};

// WarpedMotionParams (definitions.h:1755-1760): wmtype + wmmat[6]; the
// alpha/beta/gamma/delta/invalid tail is only read by the PARKED
// warped_motion_parameters span.
struct WarpedMotionParams {
    TransformationType wmtype;
    std::int32_t wmmat[6];
};

// OrderHintInfo (av1_structs.h:79-96): the two fields get_relative_dist reads.
struct OrderHintInfo {
    std::uint8_t enable_order_hint;
    std::uint8_t enable_jnt_comp;
    std::uint8_t enable_ref_frame_mvs;
    std::uint8_t order_hint_bits;
};

// TPL_MV_REF (coding_unit.h:39-42).
struct TplMvRef {
    Mv mfmv0;
    std::uint8_t ref_frame_offset;
};

// The threaded PictureControlSet/FrameHeader/ModeDecisionContext surface
// setup_ref_mv_list + add_tpl_ref_mv + generate_av1_mvp_table consume. One
// named field per consumed reference field:
//   sbSize              <- pcs->scs->seq_header.sb_size (the has_top_right arg)
//   useRefFrameMvs      <- ppcs->frm_hdr.use_ref_frame_mvs
//   allowHighPrecisionMv <- ppcs->frm_hdr.allow_high_precision_mv
//   sb64SqNo4xnGeom     <- ctx->sb64_sq_no4xn_geom
//   tplMvs              <- pcs->tpl_mvs
//   curOrderHint        <- ppcs->cur_order_hint
//   refOrderHints       <- ((EbReferenceObject*)pcs->ref_pic_ptr_array[l][i]
//                          ->object_ptr)->order_hint, [list][ref_idx]
//   orderHintInfo       <- ppcs->scs->seq_header.order_hint_info
struct MvRefSetup {
    int sbSize;
    std::uint8_t useRefFrameMvs;
    std::uint8_t allowHighPrecisionMv;
    std::uint8_t sb64SqNo4xnGeom;
    TplMvRef* tplMvs;
    int curOrderHint;
    int refOrderHints[2][4];
    OrderHintInfo orderHintInfo;
};

// ---------------------------------------------------------------------------
// Lookup tables (exported as data; the full 22-row domain is enumerable)
// ---------------------------------------------------------------------------

// common_utils.c:286-287.
extern const std::uint8_t kBlockSizeWide[BLOCK_SIZES_ALL];
extern const std::uint8_t kBlockSizeHigh[BLOCK_SIZES_ALL];

// common_utils.c:292-293.
extern const std::uint8_t kMiSizeWide[BLOCK_SIZES_ALL];
extern const std::uint8_t kMiSizeHigh[BLOCK_SIZES_ALL];

// inter_prediction.c:32-34.
extern const int kDivMult[32];

// ---------------------------------------------------------------------------
// MV primitives
// ---------------------------------------------------------------------------

// mv.h:70-73 clamp_mv.
void clampMv(Mv* mv, std::int32_t minCol, std::int32_t maxCol, std::int32_t minRow, std::int32_t maxRow);

// adaptive_mv_pred.c:49-55.
void clampMvRef(Mv* mv, std::int32_t bw, std::int32_t bh, const MacroBlockD* xd);

// inter_prediction.h:207-213.
void lowerMvPrecision(Mv* mv, int allowHp);

// inter_prediction.h:201-205.
void integerMvPrecision(Mv* mv);

// inter_prediction.h:215-224.
void getMvProjection(Mv* output, Mv ref, int num, int den);

// utility.h:217-223.
int convertToTransPrec(int allowHp, int coor);

// mv.h:40-42 get_block_mv.
Mv getBlockMv(const MbModeInfo& candidate, int whichMv);

// adaptive_mv_pred.c:44-47.
int isInside(const TileInfo& tile, std::int32_t miCol, std::int32_t miRow, std::int32_t row, std::int32_t col);

// adaptive_mv_pred.c:327-329 / :331-333.
std::int32_t findValidRowOffset(const TileInfo& tile, std::int32_t miRow, std::int32_t rowOffset);
std::int32_t findValidColOffset(const TileInfo& tile, std::int32_t miCol, std::int32_t colOffset);

// adaptive_mv_pred.c:335-350.
int getRelativeDist(const OrderHintInfo& oh, int a, int b);

// ---------------------------------------------------------------------------
// Ref-frame maps (inter_prediction.h)
// ---------------------------------------------------------------------------

// inter_prediction.h:487-496 av1_set_ref_frame. rf is the 2-slot out array.
void av1SetRefFrame(int* rf, std::int8_t refFrameType);

// inter_prediction.h:507-509 / :513-515.
std::uint8_t getListIdx(std::uint8_t refType);
std::uint8_t getRefFrameIdx(std::uint8_t refType);

// inter_prediction.h:447-459 av1_ref_frame_type.
std::int8_t av1RefFrameType(const int* rf);

// ---------------------------------------------------------------------------
// The candidate builder
// ---------------------------------------------------------------------------

// adaptive_mv_pred.c:57-128.
void addRefMvCandidate(const MbModeInfo& candidate, const int rf[2], std::uint8_t* refmvCount,
                        std::uint8_t* refMatchCount, std::uint8_t* newmvCount, CandidateMv refMvStack[kMaxRefMvStackSize],
                        std::int32_t len, const Mv* gmMvCandidates, const WarpedMotionParams* gmParams,
                        std::int32_t weight);

// adaptive_mv_pred.c:130-184.
void scanRowMbmi(const Av1Common& cm, const MacroBlockD& xd, std::int32_t miCol, const int rf[2],
                 std::int32_t rowOffset, CandidateMv refMvStack[kMaxRefMvStackSize], std::uint8_t* refmvCount,
                 std::uint8_t* refMatchCount, std::uint8_t* newmvCount, const Mv* gmMvCandidates,
                 const WarpedMotionParams* gmParams, std::int32_t maxRowOffset, std::int32_t* processedRows);

// adaptive_mv_pred.c:186-239.
void scanColMbmi(const Av1Common& cm, const MacroBlockD& xd, std::int32_t miRow, const int rf[2],
                 std::int32_t colOffset, CandidateMv refMvStack[kMaxRefMvStackSize], std::uint8_t* refmvCount,
                 std::uint8_t* refMatchCount, std::uint8_t* newmvCount, const Mv* gmMvCandidates,
                 const WarpedMotionParams* gmParams, std::int32_t maxColOffset, std::int32_t* processedCols);

// adaptive_mv_pred.c:241-264.
void scanBlkMbmi(const MacroBlockD& xd, std::int32_t miRow, std::int32_t miCol, const int rf[2],
                 std::int32_t rowOffset, std::int32_t colOffset, CandidateMv refMvStack[kMaxRefMvStackSize],
                 std::uint8_t* refMatchCount, std::uint8_t* newmvCount, const Mv* gmMvCandidates,
                 const WarpedMotionParams* gmParams, std::uint8_t* refmvCount);

// adaptive_mv_pred.c:266-325.
int hasTopRight(int sbSize, const MacroBlockD& xd, std::int32_t miRow, std::int32_t miCol, std::int32_t bs);

// adaptive_mv_pred.c:450-465.
void sortMvpTable(CandidateMv refMvStack[kMaxRefMvStackSize], std::uint8_t* refmvCount);

// adaptive_mv_pred.c:352-447.
int addTplRefMv(const Av1Common& cm, const MvRefSetup& setup, const MacroBlockD& xd, std::int32_t miRow,
                std::int32_t miCol, int refFrame, int blkRow, int blkCol, const Mv* gmMvCandidates,
                std::uint8_t* refmvCount, std::uint8_t twoSymetricRefs, Mv* mvRef0, int curOffset0, int curOffset1,
                CandidateMv refMvStack[kMaxRefMvStackSize], std::int16_t* modeContext);

// adaptive_mv_pred.c:469-648.
void scanRowColLight(const Av1Common& cm, const MacroBlockD& xd, std::int32_t miRow, std::int32_t miCol,
                     const int rf[2], CandidateMv refMvStack[kMaxRefMvStackSize], std::uint8_t* refmvCount,
                     const Mv* gmMvCandidates, std::int32_t maxRowOffset, std::int32_t maxColOffset);

// adaptive_mv_pred.c:651-971. The ModeDecisionContext/PictureControlSet walls
// arrive through MvRefSetup; symmetricRefs is the reference's symteric_refs.
void setupRefMvList(const MvRefSetup& setup, const Av1Common& cm, MacroBlockD& xd, int refFrame,
                    std::uint8_t* refmvCount, CandidateMv refMvStack[kMaxRefMvStackSize], const Mv* gmMvCandidates,
                    const WarpedMotionParams* gmParams, std::int32_t miRow, std::int32_t miCol,
                    std::uint8_t symmetricRefs, Mv* mvRef0, std::int16_t* modeContext);

// ---------------------------------------------------------------------------
// Global-motion MV evaluation
// ---------------------------------------------------------------------------

// adaptive_mv_pred.c:973-976 / :978-981.
int blockCenterX(int miCol, int bs);
int blockCenterY(int miRow, int bs);

// adaptive_mv_pred.c:983-1036.
Mv gmGetMotionVectorEnc(const WarpedMotionParams& gm, std::int32_t allowHp, int bsize, std::int32_t miCol,
                        std::int32_t miRow, std::int32_t isInteger);

// ---------------------------------------------------------------------------
// The light inter-mode context
// ---------------------------------------------------------------------------

// adaptive_mv_pred.c:1128-1136 (named deviation: static INLINE in the
// reference; exported, cdefSign testability precedent).
void countRefMatch(const BlockModeInfo& bmi, int targetRf, std::uint8_t* matchCount, std::uint8_t* newmvCount);

// adaptive_mv_pred.c:1138-1327. Named deviation: the reference writes
// ctx->inter_mode_ctx[ref_frame]; the port returns the value. sbSize is
// pcs->scs->seq_header.sb_size (the has_top_right arg).
std::int16_t computeInterModeCtxLight(const MacroBlockD& xd, const Av1Common& cm, std::int32_t miRow,
                                      std::int32_t miCol, int refFrame, int sbSize);

// adaptive_mv_pred.c:1329-1405. The ref loop driver: refMvStack is the
// ctx->ref_mv_stack array [kModeCtxRefFrames][kMaxRefMvStackSize],
// interModeCtx the ctx->inter_mode_ctx array, refMvCount the xd->ref_mv_count
// array. globalMotion is ppcs->global_motion (kRefFrames entries).
void generateAv1MvpTable(MacroBlockD& xd, const Av1Common& cm, const MvRefSetup& setup,
                         const WarpedMotionParams* globalMotion, std::uint8_t allowHighPrecisionMv,
                         std::uint8_t forceIntegerMv, int bsize, std::uint16_t blkOrgX, std::uint16_t blkOrgY,
                         const int* refFrames, std::uint32_t totRefs, std::uint8_t temporalLayerIndex,
                         bool predRandomAccess, CandidateMv refMvStack[kModeCtxRefFrames][kMaxRefMvStackSize],
                         std::int16_t* interModeCtx, std::uint8_t* refMvCount);

// ---------------------------------------------------------------------------
// The DRL / nearest-near assembly
// ---------------------------------------------------------------------------

// adaptive_mv_pred.c:1407-1457.
void getAv1MvPredDrl(const CandidateMv refMvStack[kModeCtxRefFrames][kMaxRefMvStackSize], MacroBlockD& xd,
                     int refFrame, std::uint8_t isCompound, int mode, std::uint8_t drlIndex, Mv nearestmv[2],
                     Mv nearmv[2], Mv refMv[2]);

// adaptive_mv_pred.c:2002-2028.
Mv getRefMvFromStack(int refIdx, const int* refFrame, int refMvIdx,
                     const CandidateMv refMvStack[kModeCtxRefFrames][kMaxRefMvStackSize], const MacroBlockD& xd);

// adaptive_mv_pred.c:2030-2038.
void findBestRefMvsFromStack(const CandidateMv refMvStack[kModeCtxRefFrames][kMaxRefMvStackSize],
                             const MacroBlockD& xd, int refFrame, Mv* nearestMv, Mv* nearMv);

// ---------------------------------------------------------------------------
// Warped-motion samples
// ---------------------------------------------------------------------------

// adaptive_mv_pred.c:1594-1605 (named deviation: static in the reference).
void recordSamples(const MbModeInfo& mbmi, int* pts, int* ptsInref, int rowOffset, int signR, int colOffset,
                   int signC);

// adaptive_mv_pred.c:1610-1750.
std::uint8_t av1FindSamples(const Av1Common& cm, int sbSize, MacroBlockD& xd, int rf0, int* pts, int* ptsInref);

// ---------------------------------------------------------------------------
// Overlappable neighbors
// ---------------------------------------------------------------------------

// adaptive_mv_pred.c:1830-1862 / :1864-1891 (named deviation: static in the
// reference).
std::uint32_t countOverlappableNbAbove(const Av1Common& cm, MacroBlockD& xd, std::int32_t miCol,
                                       std::uint32_t nbMax);
std::uint32_t countOverlappableNbLeft(const Av1Common& cm, MacroBlockD& xd, std::int32_t miRow,
                                      std::uint32_t nbMax);

// adaptive_mv_pred.c:1893-1906. Named deviation: the reference writes
// blk_ptr->overlappable_neighbors; the port returns the value.
std::uint32_t countOverlappableNeighbors(const Av1Common& cm, MacroBlockD& xd, int bsize, std::int32_t miRow,
                                         std::int32_t miCol);

// inter_prediction.h:242-244.
int isNeighborOverlappable(const MbModeInfo& mbmi);

// inter_prediction.h:378-380.
bool isMotionVariationAllowedBsize(int bsize);

// inter_prediction.h:382-388 (CONFIG_ENABLE_GLOBAL_MOTION=1 in the default tree).
int isGlobalMvBlock(int mode, int bsize, int type);

// adaptive_mv_pred.h:47-49.
bool hasOverlappableCandidates(std::uint32_t overlappableNeighbors);

// common_utils.h:315-320.
bool isChromaReference(int miRow, int miCol, int bsize, int ssX, int ssY);

// inter_prediction.h:226-237.
int checkSbBorder(int miRow, int miCol, int rowOffset, int colOffset);

// ---------------------------------------------------------------------------
// Intrabc DV validity
// ---------------------------------------------------------------------------

// adaptive_mv_pred.c:1908-2000.
int isDvValid(Mv dv, const MacroBlockD& xd, int miRow, int miCol, int bsize, int mibSizeLog2);

}  // namespace amvp
}  // namespace motion
