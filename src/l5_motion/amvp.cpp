// AMVP1: the AMVP candidate machinery of adaptive_mv_pred.c - the bodies
// (the batch RED was the judge's linker output; groups G3-G12 landed here).
#include "amvp.h"

#include <cstdlib>

namespace motion {
namespace amvp {

// common_utils.c:286-287, :292-293 (the five table surfaces are exported from
// amvp.cpp because the tests enumerate the full domain; the bodies of the
// ported spans do not land in this RED step).
const std::uint8_t kBlockSizeWide[BLOCK_SIZES_ALL] = {4,  4,  8,  8,   8,   16, 16, 16, 32, 32, 32,
                                                      64, 64, 64, 128, 128, 4,  16, 8,  32, 16, 64};
const std::uint8_t kBlockSizeHigh[BLOCK_SIZES_ALL] = {4,  8,  4,   8,  16,  8,  16, 32, 16, 32, 64,
                                                      32, 64, 128, 64, 128, 16, 4,  32, 8,  64, 16};
const std::uint8_t kMiSizeWide[BLOCK_SIZES_ALL] = {1, 1, 2, 2, 2, 4, 4, 4, 8, 8, 8, 16, 16, 16, 32, 32, 1, 4, 2, 8, 4, 16};
const std::uint8_t kMiSizeHigh[BLOCK_SIZES_ALL] = {1, 2, 1, 2, 4, 2, 4, 8, 4, 8, 16, 8, 16, 32, 16, 32, 4, 1, 8, 2, 16, 4};
const int kDivMult[32] = {0,    16384, 8192, 5461, 4096, 3276, 2730, 2340, 2048, 1820, 1638,
                          1489, 1365,  1260, 1170, 1092, 1024, 963,  910,  862,  819,  780,
                          744,  712,   682,  655,  630,  606,  585,  564,  546,  528};

// --- MV primitives ---------------------------------------------------------
// common_utils.h clamp (the reference's helper; the port keeps it local to the
// translation unit).

int32_t clampInt(int32_t value, int32_t low, int32_t high) {
    return value < low ? low : (value > high ? high : value);
}

// mv.h:70-73 clamp_mv, verbatim.
void clampMv(Mv* mv, int32_t min_col, int32_t max_col, int32_t min_row, int32_t max_row) {
    mv->x = (int16_t)clampInt(mv->x, min_col, max_col);
    mv->y = (int16_t)clampInt(mv->y, min_row, max_row);
}

// inter_prediction.h:201-205 integer_mv_precision, verbatim.
void integerMvPrecision(Mv* mv) {
    // round to nearest multiple of 8, ties (|rem|==4) toward zero, matching original
    mv->x = (mv->x + 3 + (mv->x < 0)) & ~7;
    mv->y = (mv->y + 3 + (mv->y < 0)) & ~7;
}

// inter_prediction.h:207-213 lower_mv_precision, verbatim.
void lowerMvPrecision(Mv* mv, int allow_hp) {
    if (allow_hp) {
        return;
    }
    mv->x = (mv->x + (mv->x < 0)) & ~1;
    mv->y = (mv->y + (mv->y < 0)) & ~1;
}

// ROUND_POWER_OF_TWO_SIGNED (the aom macro; local helper, named deviation).
static int32_t roundPowerOfTwoSigned(int32_t value, int n) {
    return value < 0 ? -(((-value) + (1 << (n - 1))) >> n) : ((value + (1 << (n - 1))) >> n);
}

// adaptive_mv_pred.c:49-55 clamp_mv_ref, verbatim (MV_BORDER = kMvBorder).
void clampMvRef(Mv* mv, int32_t bw, int32_t bh, const MacroBlockD* xd) {
    clampMv(mv,
            xd->mb_to_left_edge - bw * 8 - kMvBorder,
            xd->mb_to_right_edge + bw * 8 + kMvBorder,
            xd->mb_to_top_edge - bh * 8 - kMvBorder,
            xd->mb_to_bottom_edge + bh * 8 + kMvBorder);
}

// utility.h:217-223 convert_to_trans_prec, verbatim
// (WARPEDMODEL_PREC_BITS = kWarpedModelPrecBits = 16).
int convertToTransPrec(int allowHp, int coor) {
    if (allowHp) {
        return roundPowerOfTwoSigned(coor, kWarpedModelPrecBits - 3);
    } else {
        return roundPowerOfTwoSigned(coor, kWarpedModelPrecBits - 2) * 2;
    }
}

// adaptive_mv_pred.c:40-42 get_block_mv, verbatim.
Mv getBlockMv(const MbModeInfo& candidate, int32_t whichMv) {
    return candidate.block_mi.mv[whichMv];
}

// inter_prediction.h:215-224 get_mv_projection, verbatim
// (AOMMIN/AOMMAX -> min/max, MAX_FRAME_DISTANCE -> kMaxFrameDistance,
//  div_mult -> kDivMult, MV_UPP/MV_LOW -> kMvUpp/kMvLow).
void getMvProjection(Mv* output, Mv ref, int num, int den) {
    den = den < kMaxFrameDistance ? den : kMaxFrameDistance;
    num = num > 0 ? (num < kMaxFrameDistance ? num : kMaxFrameDistance)
                  : (num > -kMaxFrameDistance ? num : -kMaxFrameDistance);
    const int mv_row    = roundPowerOfTwoSigned(ref.y * num * kDivMult[den], 14);
    const int mv_col    = roundPowerOfTwoSigned(ref.x * num * kDivMult[den], 14);
    const int clamp_max = kMvUpp - 1;
    const int clamp_min = kMvLow + 1;
    output->y           = (int16_t)clampInt(mv_row, clamp_min, clamp_max);
    output->x           = (int16_t)clampInt(mv_col, clamp_min, clamp_max);
}

// adaptive_mv_pred.c:44-47 is_inside, verbatim (the reference's Pos* mi_pos
// pair flattened to two ints - the struct-free port).
int isInside(const TileInfo& tile, int32_t miCol, int32_t miRow, int32_t row, int32_t col) {
    return !(miRow + row < tile.mi_row_start || miCol + col < tile.mi_col_start ||
             miRow + row >= tile.mi_row_end || miCol + col >= tile.mi_col_end);
}

// adaptive_mv_pred.c:327-329 / :331-333 find_valid_row/col_offset, verbatim
// (clamp -> clampInt).
int32_t findValidRowOffset(const TileInfo& tile, int32_t miRow, int32_t rowOffset) {
    return clampInt(rowOffset, tile.mi_row_start - miRow, tile.mi_row_end - miRow - 1);
}

int32_t findValidColOffset(const TileInfo& tile, int32_t miCol, int32_t colOffset) {
    return clampInt(colOffset, tile.mi_col_start - miCol, tile.mi_col_end - miCol - 1);
}

// adaptive_mv_pred.c:335-350 get_relative_dist, verbatim (the asserts are
// fixture-enumerated in the tests).
int getRelativeDist(const OrderHintInfo& oh, int a, int b) {
    if (!oh.enable_order_hint) {
        return 0;
    }
    const int bits = oh.order_hint_bits;
    int diff       = a - b;
    const int m    = 1 << (bits - 1);
    diff           = (diff & (m - 1)) - (diff & m);
    return diff;
}

// inter_prediction.h:396-407 comp_ref0 / :409-420 comp_ref1, the two 9-entry
// uni-directional-compound LUTs, verbatim.
static int compRef0(int refIdx) {
    static const int lut[kTotalUnidirCompRefs] = {kLastFrame,   kLastFrame,  kLastFrame,  kBwdrefFrame, kLast2Frame,
                                                  kLast2Frame,  kLast3Frame, kBwdrefFrame, kAltref2Frame};
    return lut[refIdx];
}

static int compRef1(int refIdx) {
    static const int lut[kTotalUnidirCompRefs] = {kLast2Frame,  kLast3Frame, kGoldenFrame, kAltrefFrame, kLast3Frame,
                                                  kGoldenFrame, kGoldenFrame, kAltref2Frame, kAltrefFrame};
    return lut[refIdx];
}

// inter_prediction.h:428-441 get_uni_comp_ref_idx, verbatim.
static int8_t getUniCompRefIdx(const int* rf) {
    if (rf[1] <= kIntraFrame) {
        return -1;
    }
    if ((rf[0] < kBwdrefFrame) && (rf[1] >= kBwdrefFrame)) {
        return -1;
    }
    for (int8_t refIdx = 0; refIdx < kTotalUnidirCompRefs; ++refIdx) {
        if (rf[0] == compRef0(refIdx) && rf[1] == compRef1(refIdx)) {
            return refIdx;
        }
    }
    return -1;
}

// inter_prediction.h:447-459 av1_ref_frame_type, verbatim
// (FWD_RF_OFFSET = ref - LAST_FRAME, BWD_RF_OFFSET = ref - BWDREF_FRAME,
// definitions.h:1393-1394).
int8_t av1RefFrameType(const int* rf) {
    if (rf[1] > kIntraFrame) {
        const int8_t uniCompRefIdx = getUniCompRefIdx(rf);
        if (uniCompRefIdx >= 0) {
            return kTotalRefsPerFrame + kFwdRefs * kBwdRefs + uniCompRefIdx;
        } else {
            return (int8_t)(kTotalRefsPerFrame + (rf[0] - kLastFrame) + (rf[1] - kBwdrefFrame) * kFwdRefs);
        }
    }
    return (int8_t)rf[0];
}

// inter_prediction.h:461-485 ref_frame_map (TOTAL_COMP_REFS = 21 pairs).
static const int kRefFrameMap[kTotalCompRefs][2] = {
    {kLastFrame, kBwdrefFrame},   {kLast2Frame, kBwdrefFrame},  {kLast3Frame, kBwdrefFrame},
    {kGoldenFrame, kBwdrefFrame}, {kLastFrame, kAltref2Frame},  {kLast2Frame, kAltref2Frame},
    {kLast3Frame, kAltref2Frame}, {kGoldenFrame, kAltref2Frame}, {kLastFrame, kAltrefFrame},
    {kLast2Frame, kAltrefFrame},  {kLast3Frame, kAltrefFrame},  {kGoldenFrame, kAltrefFrame},
    {kLastFrame, kLast2Frame},    {kLastFrame, kLast3Frame},    {kLastFrame, kGoldenFrame},
    {kBwdrefFrame, kAltrefFrame}, {kLast2Frame, kLast3Frame},   {kLast2Frame, kGoldenFrame},
    {kLast3Frame, kGoldenFrame},  {kBwdrefFrame, kAltref2Frame}, {kAltref2Frame, kAltrefFrame}};

// inter_prediction.h:487-496 av1_set_ref_frame, verbatim.
void av1SetRefFrame(int* rf, int8_t refFrameType) {
    if (refFrameType >= kTotalRefsPerFrame) {
        rf[0] = kRefFrameMap[refFrameType - kTotalRefsPerFrame][0];
        rf[1] = kRefFrameMap[refFrameType - kTotalRefsPerFrame][1];
    } else {
        rf[0] = refFrameType;
        rf[1] = kNoneFrame;
    }
}

// inter_prediction.h:498-515: REFS_PER_FRAME = FWD_REFS + BWD_REFS = 7
// (definitions.h:1389); the two 8-entry LUTs verbatim.
uint8_t getListIdx(uint8_t refType) {
    static const uint8_t lut[kFwdRefs + kBwdRefs + 1] = {0, 0, 0, 0, 0, 1, 1, 1};
    return lut[refType];
}

uint8_t getRefFrameIdx(uint8_t refType) {
    static const uint8_t lut[kFwdRefs + kBwdRefs + 1] = {0, 0, 1, 2, 3, 0, 1, 2};
    return lut[refType];
}

// block_structures.h:118-124 is_inter_block / :116-119 is_intrabc_block
// (CONFIG_ENABLE_INTRA_BC=1 in the default tree).
static int isInterBlock(const BlockModeInfo& blockMi) {
    return blockMi.use_intrabc || blockMi.ref_frame[0] > kIntraFrame;
}

// mode_decision.c:274-277 svt_aom_have_newmv_in_inter_mode, verbatim.
static int haveNewmvInInterMode(int mode) {
    return (mode == NEWMV || mode == NEW_NEWMV || mode == NEAREST_NEWMV || mode == NEW_NEARESTMV ||
            mode == NEAR_NEWMV || mode == NEW_NEARMV);
}

// inter_prediction.h:378-380 is_motion_variation_allowed_bsize, verbatim.
bool isMotionVariationAllowedBsize(int bsize) {
    return kBlockSizeWide[bsize] >= 8 && kBlockSizeHigh[bsize] >= 8;
}

// inter_prediction.h:382-388 is_global_mv_block (CONFIG_ENABLE_GLOBAL_MOTION=1
// in the default tree, so the CONFIG branch is live).
int isGlobalMvBlock(int mode, int bsize, int type) {
    return (mode == GLOBALMV || mode == GLOBAL_GLOBALMV) && type > TRANSLATION &&
        isMotionVariationAllowedBsize(bsize);
}

// adaptive_mv_pred.c:57-128 add_ref_mv_candidate, verbatim
// (is_inter_block -> isInterBlock, is_global_mv_block -> isGlobalMvBlock,
//  svt_aom_have_newmv_in_inter_mode -> haveNewmvInInterMode; the reference's
//  weight-even assert is fixture-enumerated in the tests).
void addRefMvCandidate(const MbModeInfo& candidate, const int rf[2], std::uint8_t* refmvCount,
                       std::uint8_t* refMatchCount, std::uint8_t* newmvCount,
                       CandidateMv refMvStack[kMaxRefMvStackSize], std::int32_t len, const Mv* gmMvCandidates,
                       const WarpedMotionParams* gmParams, std::int32_t weight) {
    if (!isInterBlock(candidate.block_mi)) {
        return;  // for intrabc
    }
    if (rf[1] == kNoneFrame) {
        // single reference frame
        for (std::int32_t ref = 0; ref < 2; ++ref) {
            if (candidate.block_mi.ref_frame[ref] == rf[0]) {
                Mv thisRefmv;
                if (isGlobalMvBlock(candidate.block_mi.mode, candidate.bsize, gmParams[rf[0]].wmtype)) {
                    thisRefmv = gmMvCandidates[0];
                } else {
                    thisRefmv = getBlockMv(candidate, ref);
                }
                std::int32_t index;
                for (index = 0; index < *refmvCount; ++index) {
                    if (refMvStack[index].this_mv.as_int == thisRefmv.as_int) {
                        refMvStack[index].weight += weight * len;
                        break;
                    }
                }
                // Add a new item to the list.
                if (index == *refmvCount && *refmvCount < kMaxRefMvStackSize) {
                    refMvStack[index].this_mv = thisRefmv;
                    refMvStack[index].weight  = weight * len;
                    ++(*refmvCount);
                }
                if (haveNewmvInInterMode(candidate.block_mi.mode)) {
                    ++*newmvCount;
                }
                ++*refMatchCount;
            }
        }
    } else {
        // compound reference frame
        if (candidate.block_mi.ref_frame[0] == rf[0] && candidate.block_mi.ref_frame[1] == rf[1]) {
            Mv thisRefmv[2];
            for (std::int32_t ref = 0; ref < 2; ++ref) {
                if (isGlobalMvBlock(candidate.block_mi.mode, candidate.bsize, gmParams[rf[ref]].wmtype)) {
                    thisRefmv[ref] = gmMvCandidates[ref];
                } else {
                    thisRefmv[ref] = getBlockMv(candidate, ref);
                }
            }
            std::int32_t index;
            for (index = 0; index < *refmvCount; ++index) {
                if ((refMvStack[index].this_mv.as_int == thisRefmv[0].as_int) &&
                    (refMvStack[index].comp_mv.as_int == thisRefmv[1].as_int)) {
                    refMvStack[index].weight += weight * len;
                    break;
                }
            }
            // Add a new item to the list.
            if (index == *refmvCount && *refmvCount < kMaxRefMvStackSize) {
                refMvStack[index].this_mv = thisRefmv[0];
                refMvStack[index].comp_mv = thisRefmv[1];
                refMvStack[index].weight  = weight * len;
                ++(*refmvCount);
            }
            if (haveNewmvInInterMode(candidate.block_mi.mode)) {
                ++*newmvCount;
            }
            ++*refMatchCount;
        }
    }
}

// adaptive_mv_pred.c:266-325 has_top_right, verbatim (mi_size_wide ->
// kMiSizeWide; the bs-power-of-2 assert is fixture-enumerated).
int hasTopRight(int sbSize, const MacroBlockD& xd, std::int32_t miRow, std::int32_t miCol, std::int32_t bs) {
    if (bs > kMiSizeWide[BLOCK_64X64]) {
        return 0;
    }
    // The bottom of two horizontal rectangles never has a top right (as the
    // block to the right won't have been decoded)
    if (xd.n8_w > xd.n8_h) {
        if (xd.is_sec_rect) {
            return 0;
        }
    }
    // The left hand of two vertical rectangles always has a top right (as
    // the block above will have been decoded)
    if (xd.n8_w < xd.n8_h) {
        if (!xd.is_sec_rect) {
            return 1;
        }
    }
    const std::int32_t sbMiSize = kMiSizeWide[sbSize];
    const std::int32_t maskRow  = miRow & (sbMiSize - 1);
    const std::int32_t maskCol  = miCol & (sbMiSize - 1);
    // In a split partition all apart from the bottom right has a top right
    std::int32_t hasTr = !((maskRow & bs) && (maskCol & bs));
    // For each 4x4 group of blocks, when the bottom right is decoded the
    // blocks to the right have not been decoded therefore the bottom right
    // does not have a top right
    while (bs < sbMiSize) {
        if (maskCol & bs) {
            if ((maskCol & (2 * bs)) && (maskRow & (2 * bs))) {
                hasTr = 0;
                break;
            }
        } else {
            break;
        }
        bs <<= 1;
    }
    // The bottom left square of a Vertical A (in the old format) does not
    // have a top right as it is decoded before the right hand rectangle of
    // the partition
    if (xd.mi[0]->partition == PARTITION_VERT_A) {
        if (xd.n8_w == xd.n8_h) {
            if (maskRow & bs) {
                return 0;
            }
        }
    }
    return hasTr;
}

// adaptive_mv_pred.c:130-184 scan_row_mbmi, verbatim (AOMMIN/AOMMAX ->
// min/max; the candidate-bsize assert is fixture-enumerated).
void scanRowMbmi(const Av1Common& cm, const MacroBlockD& xd, std::int32_t miCol, const int rf[2],
                 std::int32_t rowOffset, CandidateMv refMvStack[kMaxRefMvStackSize], std::uint8_t* refmvCount,
                 std::uint8_t* refMatchCount, std::uint8_t* newmvCount, const Mv* gmMvCandidates,
                 const WarpedMotionParams* gmParams, std::int32_t maxRowOffset, std::int32_t* processedRows) {
    std::int32_t endMi = xd.n8_w < cm.mi_cols - miCol ? xd.n8_w : cm.mi_cols - miCol;
    endMi              = endMi < kMiSizeWide[BLOCK_64X64] ? endMi : kMiSizeWide[BLOCK_64X64];
    const std::int32_t n8w8  = kMiSizeWide[BLOCK_8X8];
    const std::int32_t n8w16 = kMiSizeWide[BLOCK_16X16];
    std::int32_t i;
    std::int32_t colOffset = 0;
    const std::int32_t shift = 0;
    if (abs(rowOffset) > 1) {
        colOffset = 1;
        if (miCol & 0x01 && xd.n8_w < n8w8) {
            --colOffset;
        }
    }
    const std::int32_t useStep16 = (xd.n8_w >= 16);
    MbModeInfo** const candidateMi0 = xd.mi + rowOffset * xd.mi_stride;
    for (i = 0; i < endMi;) {
        const MbModeInfo* const candidate      = candidateMi0[colOffset + i];
        const std::int32_t candidateBsize = candidate->bsize;
        const std::int32_t n8w             = kMiSizeWide[candidateBsize];
        std::int32_t len = xd.n8_w < n8w ? xd.n8_w : n8w;
        if (useStep16) {
            len = n8w16 > len ? n8w16 : len;
        } else if (abs(rowOffset) > 1) {
            len = len > n8w8 ? len : n8w8;
        }
        std::int32_t weight = 2;
        if (xd.n8_w >= n8w8 && xd.n8_w <= n8w) {
            const std::int32_t inc = -maxRowOffset + rowOffset + 1 < kMiSizeHigh[candidateBsize]
                                         ? -maxRowOffset + rowOffset + 1
                                         : kMiSizeHigh[candidateBsize];
            weight          = weight > (inc << shift) ? weight : (inc << shift);
            *processedRows  = inc - rowOffset - 1;
        }
        addRefMvCandidate(*candidate, rf, refmvCount, refMatchCount, newmvCount, refMvStack, len, gmMvCandidates,
                          gmParams, weight);
        i += len;
    }
}

// adaptive_mv_pred.c:186-239 scan_col_mbmi, verbatim (the row twin of
// scan_row_mbmi: n8_h/mi_size_high, the candidate indexed down mi_stride).
void scanColMbmi(const Av1Common& cm, const MacroBlockD& xd, std::int32_t miRow, const int rf[2],
                 std::int32_t colOffset, CandidateMv refMvStack[kMaxRefMvStackSize], std::uint8_t* refmvCount,
                 std::uint8_t* refMatchCount, std::uint8_t* newmvCount, const Mv* gmMvCandidates,
                 const WarpedMotionParams* gmParams, std::int32_t maxColOffset, std::int32_t* processedCols) {
    std::int32_t endMi = xd.n8_h < cm.mi_rows - miRow ? xd.n8_h : cm.mi_rows - miRow;
    endMi              = endMi < kMiSizeHigh[BLOCK_64X64] ? endMi : kMiSizeHigh[BLOCK_64X64];
    const std::int32_t n8h8  = kMiSizeHigh[BLOCK_8X8];
    const std::int32_t n8h16 = kMiSizeHigh[BLOCK_16X16];
    std::int32_t i;
    std::int32_t rowOffset = 0;
    const std::int32_t shift = 0;
    if (abs(colOffset) > 1) {
        rowOffset = 1;
        if (miRow & 0x01 && xd.n8_h < n8h8) {
            --rowOffset;
        }
    }
    const std::int32_t useStep16 = (xd.n8_h >= 16);
    for (i = 0; i < endMi;) {
        const MbModeInfo* const candidate      = xd.mi[(rowOffset + i) * xd.mi_stride + colOffset];
        const std::int32_t candidateBsize = candidate->bsize;
        const std::int32_t n8h             = kMiSizeHigh[candidateBsize];
        std::int32_t len = xd.n8_h < n8h ? xd.n8_h : n8h;
        if (useStep16) {
            len = n8h16 > len ? n8h16 : len;
        } else if (abs(colOffset) > 1) {
            len = len > n8h8 ? len : n8h8;
        }
        std::int32_t weight = 2;
        if (xd.n8_h >= n8h8 && xd.n8_h <= n8h) {
            const std::int32_t inc = -maxColOffset + colOffset + 1 < kMiSizeWide[candidateBsize]
                                         ? -maxColOffset + colOffset + 1
                                         : kMiSizeWide[candidateBsize];
            weight          = weight > (inc << shift) ? weight : (inc << shift);
            *processedCols  = inc - colOffset - 1;
        }
        addRefMvCandidate(*candidate, rf, refmvCount, refMatchCount, newmvCount, refMvStack, len, gmMvCandidates,
                          gmParams, weight);
        i += len;
    }
}

// adaptive_mv_pred.c:241-264 scan_blk_mbmi, verbatim (the Pos struct flattened
// into the two offsets; is_inside -> isInside).
void scanBlkMbmi(const MacroBlockD& xd, std::int32_t miRow, std::int32_t miCol, const int rf[2],
                 std::int32_t rowOffset, std::int32_t colOffset, CandidateMv refMvStack[kMaxRefMvStackSize],
                 std::uint8_t* refMatchCount, std::uint8_t* newmvCount, const Mv* gmMvCandidates,
                 const WarpedMotionParams* gmParams, std::uint8_t* refmvCount) {
    // Analyze a single 8x8 block motion information.
    if (isInside(xd.tile, miCol, miRow, rowOffset, colOffset)) {
        const MbModeInfo* const candidate = xd.mi[rowOffset * xd.mi_stride + colOffset];
        addRefMvCandidate(*candidate, rf, refmvCount, refMatchCount, newmvCount, refMvStack,
                          kMiSizeWide[BLOCK_8X8], gmMvCandidates, gmParams, 2);
    }
}

// adaptive_mv_pred.c:352-447 add_tpl_ref_mv, verbatim (pcs->tpl_mvs ->
// setup.tplMvs, pcs->ppcs->frm_hdr.allow_high_precision_mv ->
// setup.allowHighPrecisionMv, the Pos struct flattened, is_inside ->
// isInside, GLOBALMV_OFFSET -> kGlobalMvOffset).
int addTplRefMv(const Av1Common& cm, const MvRefSetup& setup, const MacroBlockD& xd, std::int32_t miRow,
                std::int32_t miCol, int refFrame, int blkRow, int blkCol, const Mv* gmMvCandidates,
                std::uint8_t* refmvCount, std::uint8_t twoSymetricRefs, Mv* mvRef0, int curOffset0, int curOffset1,
                CandidateMv refMvStack[kMaxRefMvStackSize], std::int16_t* modeContext) {
    const int posRow = (miRow & 0x01) ? blkRow : blkRow + 1;
    const int posCol = (miCol & 0x01) ? blkCol : blkCol + 1;
    if (!isInside(xd.tile, miCol, miRow, posRow, posCol)) {
        return 0;
    }
    const TplMvRef* prevFrameMvs = setup.tplMvs + ((miRow + posRow) >> 1) * (cm.mi_stride >> 1) +
        ((miCol + posCol) >> 1);
    if (prevFrameMvs->mfmv0.as_int == kInvalidMv) {
        return 0;
    }
    const uint16_t weightUnit = 1;
    int idx;
    Mv thisRefmv;
    if (twoSymetricRefs) {
        if (refFrame == kLastFrame) {
            getMvProjection(&thisRefmv, prevFrameMvs->mfmv0, curOffset0, prevFrameMvs->ref_frame_offset);
            lowerMvPrecision(&thisRefmv, setup.allowHighPrecisionMv);
            // store for future use
            (*mvRef0) = thisRefmv;
        } else {
            if (refFrame == kBwdrefFrame) {
                thisRefmv.y = -mvRef0->y;
                thisRefmv.x = -mvRef0->x;
            } else {
                thisRefmv = (*mvRef0);
            }
        }
    } else {
        getMvProjection(&thisRefmv, prevFrameMvs->mfmv0, curOffset0, prevFrameMvs->ref_frame_offset);
        lowerMvPrecision(&thisRefmv, setup.allowHighPrecisionMv);
    }
    // single ref case could be detected by ref_frame
    if (refFrame < kLastBwdFrame) {
        if (blkRow == 0 && blkCol == 0) {
            if (abs(thisRefmv.y - gmMvCandidates[0].y) >= 16 || abs(thisRefmv.x - gmMvCandidates[0].x) >= 16) {
                *modeContext |= (1 << kGlobalMvOffset);
            }
        }
        for (idx = 0; idx < *refmvCount; ++idx) {
            if (thisRefmv.as_int == refMvStack[idx].this_mv.as_int) {
                refMvStack[idx].weight += 2 * weightUnit;
                break;
            }
        }
        if (idx == *refmvCount && *refmvCount < kMaxRefMvStackSize) {
            refMvStack[idx].this_mv.as_int = thisRefmv.as_int;
            refMvStack[idx].weight         = 2 * weightUnit;
            ++(*refmvCount);
        }
    } else {
        // Process compound inter mode
        Mv compRefmv;
        if (twoSymetricRefs) {
            compRefmv.y = -mvRef0->y;
            compRefmv.x = -mvRef0->x;
        } else {
            getMvProjection(&compRefmv, prevFrameMvs->mfmv0, curOffset1, prevFrameMvs->ref_frame_offset);
            lowerMvPrecision(&compRefmv, setup.allowHighPrecisionMv);
        }
        if (blkRow == 0 && blkCol == 0) {
            if (abs(thisRefmv.y - gmMvCandidates[0].y) >= 16 || abs(thisRefmv.x - gmMvCandidates[0].x) >= 16 ||
                abs(compRefmv.y - gmMvCandidates[1].y) >= 16 || abs(compRefmv.x - gmMvCandidates[1].x) >= 16) {
                *modeContext |= (1 << kGlobalMvOffset);
            }
        }
        for (idx = 0; idx < *refmvCount; ++idx) {
            if (thisRefmv.as_int == refMvStack[idx].this_mv.as_int &&
                compRefmv.as_int == refMvStack[idx].comp_mv.as_int) {
                refMvStack[idx].weight += 2 * weightUnit;
                break;
            }
        }
        if (idx == *refmvCount && *refmvCount < kMaxRefMvStackSize) {
            refMvStack[idx].this_mv.as_int = thisRefmv.as_int;
            refMvStack[idx].comp_mv.as_int = compRefmv.as_int;
            refMvStack[idx].weight         = 2 * weightUnit;
            ++(*refmvCount);
        }
    }
    return 1;
}

// adaptive_mv_pred.c:450-465 sort_mvp_table, verbatim (rank the likelihood
// and assign nearest and near mvs).
void sortMvpTable(CandidateMv refMvStack[kMaxRefMvStackSize], std::uint8_t* refmvCount) {
    std::uint8_t len = *refmvCount;
    while (len > 0) {
        std::uint8_t nrLen = 0;
        for (std::uint8_t idx = 1; idx < len; ++idx) {
            if (refMvStack[idx - 1].weight < refMvStack[idx].weight) {
                CandidateMv tmpMv      = refMvStack[idx - 1];
                refMvStack[idx - 1]    = refMvStack[idx];
                refMvStack[idx]        = tmpMv;
                nrLen                  = idx;
            }
        }
        len = nrLen;
    }
}

// adaptive_mv_pred.c:469-648 scan_row_col_light, verbatim (the compound
// branch :479-576 then the single-ref branch :577-647; ABS -> abs,
// AOMMIN/AOMMAX -> min/max, MAX_MV_REF_CANDIDATES -> kMaxMvRefCandidates).
void scanRowColLight(const Av1Common& cm, const MacroBlockD& xd, std::int32_t miRow, std::int32_t miCol,
                     const int rf[2], CandidateMv refMvStack[kMaxRefMvStackSize], std::uint8_t* refmvCount,
                     const Mv* gmMvCandidates, std::int32_t maxRowOffset, std::int32_t maxColOffset) {
    std::uint8_t miWidth  = kMiSizeWide[BLOCK_64X64] < xd.n8_w ? (std::uint8_t)kMiSizeWide[BLOCK_64X64]
                                                              : (std::uint8_t)xd.n8_w;
    miWidth               = miWidth < cm.mi_cols - miCol ? miWidth : (std::uint8_t)(cm.mi_cols - miCol);
    std::uint8_t miHeight = kMiSizeHigh[BLOCK_64X64] < xd.n8_h ? (std::uint8_t)kMiSizeHigh[BLOCK_64X64]
                                                              : (std::uint8_t)xd.n8_h;
    miHeight              = miHeight < cm.mi_rows - miRow ? miHeight : (std::uint8_t)(cm.mi_rows - miRow);
    std::uint8_t miSize   = miWidth < miHeight ? miWidth : miHeight;

    // Multiple ref frames path
    if (rf[1] > kNoneFrame) {
        // we get here only when refMVCount=0 or 1
        Mv refId[2][2], refDiff[2][2];
        std::uint8_t refIdCount[2] = {0}, refDiffCount[2] = {0};

        // scan ROW=-1 again but with more relaxed constraints
        for (std::int32_t idx = 0; abs(maxRowOffset) >= 1 && idx < miSize;) {
            const MbModeInfo* const candidate      = xd.mi[-xd.mi_stride + idx];
            const std::int32_t candidateBsize = candidate->bsize;
            for (std::uint8_t rfIdx = 0; rfIdx < 2; ++rfIdx) {
                const int canRf = candidate->block_mi.ref_frame[rfIdx];
                for (std::uint8_t cmpIdx = 0; cmpIdx < 2; ++cmpIdx) {
                    if (canRf == rf[cmpIdx] && refIdCount[cmpIdx] < 2) {
                        refId[cmpIdx][refIdCount[cmpIdx]] = candidate->block_mi.mv[rfIdx];
                        ++refIdCount[cmpIdx];
                    } else if (canRf > kIntraFrame && refDiffCount[cmpIdx] < 2) {
                        Mv thisMv = candidate->block_mi.mv[rfIdx];
                        if (cm.ref_frame_sign_bias[canRf] != cm.ref_frame_sign_bias[rf[cmpIdx]]) {
                            thisMv.y = -thisMv.y;
                            thisMv.x = -thisMv.x;
                        }
                        refDiff[cmpIdx][refDiffCount[cmpIdx]] = thisMv;
                        ++refDiffCount[cmpIdx];
                    }
                }
            }
            idx += kMiSizeWide[candidateBsize];
        }

        // scan COL=-1 again but with more relaxed constraints
        for (std::int32_t idx = 0; abs(maxColOffset) >= 1 && idx < miSize;) {
            const MbModeInfo* const candidate      = xd.mi[idx * xd.mi_stride - 1];
            const std::int32_t candidateBsize = candidate->bsize;
            for (std::uint8_t rfIdx = 0; rfIdx < 2; ++rfIdx) {
                const int canRf = candidate->block_mi.ref_frame[rfIdx];
                for (std::uint8_t cmpIdx = 0; cmpIdx < 2; ++cmpIdx) {
                    if (canRf == rf[cmpIdx] && refIdCount[cmpIdx] < 2) {
                        refId[cmpIdx][refIdCount[cmpIdx]] = candidate->block_mi.mv[rfIdx];
                        ++refIdCount[cmpIdx];
                    } else if (canRf > kIntraFrame && refDiffCount[cmpIdx] < 2) {
                        Mv thisMv = candidate->block_mi.mv[rfIdx];
                        if (cm.ref_frame_sign_bias[canRf] != cm.ref_frame_sign_bias[rf[cmpIdx]]) {
                            thisMv.y = -thisMv.y;
                            thisMv.x = -thisMv.x;
                        }
                        refDiff[cmpIdx][refDiffCount[cmpIdx]] = thisMv;
                        ++refDiffCount[cmpIdx];
                    }
                }
            }
            idx += kMiSizeHigh[candidateBsize];
        }

        // Build up the compound mv predictor
        Mv compList[kMaxMvRefCandidates + 1][2];
        for (std::uint8_t idx = 0; idx < 2; ++idx) {
            std::uint8_t compIdx = 0;
            for (std::uint8_t listIdx = 0; listIdx < refIdCount[idx] && compIdx < kMaxMvRefCandidates;
                 ++listIdx, ++compIdx) {
                compList[compIdx][idx] = refId[idx][listIdx];
            }
            for (std::uint8_t listIdx = 0; listIdx < refDiffCount[idx] && compIdx < kMaxMvRefCandidates;
                 ++listIdx, ++compIdx) {
                compList[compIdx][idx] = refDiff[idx][listIdx];
            }
            for (; compIdx < kMaxMvRefCandidates; ++compIdx) {
                compList[compIdx][idx] = gmMvCandidates[idx];
            }
        }

        // fill the stack, increment the counter
        if (*refmvCount) {  // RefMvCount=1
            if (compList[0][0].as_int == refMvStack[0].this_mv.as_int &&
                compList[0][1].as_int == refMvStack[0].comp_mv.as_int) {
                refMvStack[*refmvCount].this_mv = compList[1][0];
                refMvStack[*refmvCount].comp_mv = compList[1][1];
            } else {
                refMvStack[*refmvCount].this_mv = compList[0][0];
                refMvStack[*refmvCount].comp_mv = compList[0][1];
            }
            refMvStack[*refmvCount].weight = 2;
            ++(*refmvCount);
        } else {  // RefMvCount=0
            for (std::uint8_t idx = 0; idx < kMaxMvRefCandidates; ++idx) {
                refMvStack[*refmvCount].this_mv = compList[idx][0];
                refMvStack[*refmvCount].comp_mv = compList[idx][1];
                refMvStack[*refmvCount].weight  = 2;
                ++(*refmvCount);
            }
        }
    } else {
        // Handle single reference frame extension
        // if count is still < 2, re-scan ROW=-1 with less constraints.
        // Order is already fixed. the added candidates are stored as we go at
        // the bottom of the Stack.
        for (std::int32_t idx = 0; abs(maxRowOffset) >= 1 && idx < miSize && *refmvCount < kMaxMvRefCandidates;) {
            const MbModeInfo* const candidate      = xd.mi[-xd.mi_stride + idx];
            const std::int32_t candidateBsize = candidate->bsize;
            for (std::int32_t rfIdx = 0; rfIdx < 2; ++rfIdx) {
                if (candidate->block_mi.ref_frame[rfIdx] > kIntraFrame) {
                    Mv thisMv = candidate->block_mi.mv[rfIdx];
                    if (cm.ref_frame_sign_bias[candidate->block_mi.ref_frame[rfIdx]] !=
                        cm.ref_frame_sign_bias[rf[0]]) {
                        thisMv.y = -thisMv.y;
                        thisMv.x = -thisMv.x;
                    }
                    std::int8_t stackIdx;
                    for (stackIdx = 0; stackIdx < *refmvCount; ++stackIdx) {
                        const Mv stackMv = refMvStack[stackIdx].this_mv;
                        if (thisMv.as_int == stackMv.as_int) {
                            break;
                        }
                    }
                    if (stackIdx == *refmvCount) {
                        refMvStack[stackIdx].this_mv = thisMv;
                        refMvStack[stackIdx].weight  = 2;
                        ++(*refmvCount);
                    }
                }
            }
            idx += kMiSizeWide[candidateBsize];
        }

        // if count is still < 2, re-scan COL=-1 with less constraints. the
        // added candidates are stored as we go at the bottom of the Stack.
        for (std::int32_t idx = 0; abs(maxColOffset) >= 1 && idx < miSize && *refmvCount < kMaxMvRefCandidates;) {
            const MbModeInfo* const candidate      = xd.mi[idx * xd.mi_stride - 1];
            const std::int32_t candidateBsize = candidate->bsize;
            for (std::uint8_t rfIdx = 0; rfIdx < 2; ++rfIdx) {
                if (candidate->block_mi.ref_frame[rfIdx] > kIntraFrame) {
                    Mv thisMv = candidate->block_mi.mv[rfIdx];
                    if (cm.ref_frame_sign_bias[candidate->block_mi.ref_frame[rfIdx]] !=
                        cm.ref_frame_sign_bias[rf[0]]) {
                        thisMv.y = -thisMv.y;
                        thisMv.x = -thisMv.x;
                    }
                    std::int8_t stackIdx;
                    for (stackIdx = 0; stackIdx < *refmvCount; ++stackIdx) {
                        const Mv stackMv = refMvStack[stackIdx].this_mv;
                        if (thisMv.as_int == stackMv.as_int) {
                            break;
                        }
                    }
                    if (stackIdx == *refmvCount) {
                        refMvStack[stackIdx].this_mv = thisMv;
                        refMvStack[stackIdx].weight  = 2;
                        ++(*refmvCount);
                    }
                }
            }
            idx += kMiSizeHigh[candidateBsize];
        }

        for (std::uint8_t idx = *refmvCount; idx < kMaxMvRefCandidates; ++idx) {
            refMvStack[idx].this_mv.as_int = gmMvCandidates[0].as_int;
        }
    }
}

// adaptive_mv_pred.c:651-971 setup_ref_mv_list, verbatim (the
// ModeDecisionContext/PictureControlSet walls arrive through MvRefSetup:
// pcs->scs->seq_header.sb_size -> setup.sbSize,
// ppcs->frm_hdr.use_ref_frame_mvs -> setup.useRefFrameMvs,
// ctx->sb64_sq_no4xn_geom -> setup.sb64SqNo4xnGeom,
// ppcs->cur_order_hint -> setup.curOrderHint,
// ((EbReferenceObject*)pcs->ref_pic_ptr_array[l][i]->object_ptr)->order_hint ->
// setup.refOrderHints[l][i], ppcs->scs->seq_header.order_hint_info ->
// setup.orderHintInfo. symteric_refs [sic] -> symmetricRefs;
// MVREF_ROWS = MVREF_COLS = 3, adaptive_mv_pred.c:30-31).
void setupRefMvList(const MvRefSetup& setup, const Av1Common& cm, MacroBlockD& xd, int refFrame,
                    std::uint8_t* refmvCount, CandidateMv refMvStack[kMaxRefMvStackSize], const Mv* gmMvCandidates,
                    const WarpedMotionParams* gmParams, std::int32_t miRow, std::int32_t miCol,
                    std::uint8_t symmetricRefs, Mv* mvRef0, std::int16_t* modeContext) {
    const std::int32_t bs         = xd.n8_w > xd.n8_h ? xd.n8_w : xd.n8_h;
    const std::int32_t hasTr      = hasTopRight(setup.sbSize, xd, miRow, miCol, bs);
    const TileInfo* const tile    = &xd.tile;
    std::int32_t maxRowOffset = 0, maxColOffset = 0;
    const std::int32_t rowAdj = (xd.n8_h < kMiSizeHigh[BLOCK_8X8]) && (miRow & 0x01);
    const std::int32_t colAdj = (xd.n8_w < kMiSizeWide[BLOCK_8X8]) && (miCol & 0x01);
    std::int32_t processedRows = 0;
    std::int32_t processedCols = 0;

    int rf[2];
    av1SetRefFrame(rf, (std::int8_t)refFrame);
    *modeContext = 0;
    *refmvCount  = 0;

    // Find valid maximum row/col offset.
    if (xd.up_available) {
        maxRowOffset = -(3 << 1) + rowAdj;
        if (xd.n8_h < kMiSizeHigh[BLOCK_8X8]) {
            maxRowOffset = -(2 << 1) + rowAdj;
        }
        maxRowOffset = findValidRowOffset(*tile, miRow, maxRowOffset);
    }
    if (xd.left_available) {
        maxColOffset = -(3 << 1) + colAdj;
        if (xd.n8_w < kMiSizeWide[BLOCK_8X8]) {
            maxColOffset = -(2 << 1) + colAdj;
        }
        maxColOffset = findValidColOffset(*tile, miCol, maxColOffset);
    }

    std::uint8_t colMatchCount = 0;
    std::uint8_t rowMatchCount = 0;
    std::uint8_t newmvCount    = 0;

    // ROW-1: scan the first above row mode info. row_offset = -1;
    if (abs(maxRowOffset) >= 1) {
        scanRowMbmi(cm, xd, miCol, rf, -1, refMvStack, refmvCount, &rowMatchCount, &newmvCount, gmMvCandidates,
                    gmParams, maxRowOffset, &processedRows);
    }
    // COL-1: scan the first left column mode info. col_offset = -1;
    if (abs(maxColOffset) >= 1) {
        scanColMbmi(cm, xd, miRow, rf, -1, refMvStack, refmvCount, &colMatchCount, &newmvCount, gmMvCandidates,
                    gmParams, maxColOffset, &processedCols);
    }
    // TOP-RIGHT: check top-right boundary
    if (hasTr) {
        scanBlkMbmi(xd, miRow, miCol, rf, -1, xd.n8_w, refMvStack, &rowMatchCount, &newmvCount, gmMvCandidates,
                    gmParams, refmvCount);
    }

    const std::uint8_t nearestMatch = (rowMatchCount > 0) + (colMatchCount > 0);

    for (std::int32_t idx = 0; idx < *refmvCount; ++idx) {
        refMvStack[idx].weight += kRefCatLevel;
    }

    // MFMV - get candidates from reference frames - orderHint has to be on,
    // in order to scale the vectors.
    if (setup.useRefFrameMvs) {
        int isAvailable = 0;
        int blkRowEnd, blkColEnd, stepW, stepH, allowExtension;
        if (setup.sb64SqNo4xnGeom) {
            blkRowEnd     = xd.n4_w;
            blkColEnd     = xd.n4_w;
            stepW         = (xd.n4_w >= kMiSizeW64X64) ? kMiSizeW16X16 : kMiSizeW8X8;
            stepH         = stepW;
            allowExtension = (xd.n4_w >= kMiSizeW8X8) && (xd.n4_w < kMiSizeW64X64);
        } else {
            blkRowEnd = xd.n4_h < kMiSizeHigh[BLOCK_64X64] ? xd.n4_h : kMiSizeHigh[BLOCK_64X64];
            blkColEnd = xd.n4_w < kMiSizeWide[BLOCK_64X64] ? xd.n4_w : kMiSizeWide[BLOCK_64X64];
            allowExtension = (xd.n4_h >= kMiSizeHigh[BLOCK_8X8]) && (xd.n4_h < kMiSizeHigh[BLOCK_64X64]) &&
                (xd.n4_w >= kMiSizeWide[BLOCK_8X8]) && (xd.n4_w < kMiSizeWide[BLOCK_64X64]);
            stepH = (xd.n4_h >= kMiSizeHigh[BLOCK_64X64]) ? kMiSizeHigh[BLOCK_16X16] : kMiSizeHigh[BLOCK_8X8];
            stepW = (xd.n4_w >= kMiSizeWide[BLOCK_64X64]) ? kMiSizeWide[BLOCK_16X16] : kMiSizeHigh[BLOCK_8X8];
        }

        int curOffset0;
        int curOffset1 = 0;
        const std::uint8_t listIdx0      = getListIdx((std::uint8_t)rf[0]);
        const std::uint8_t refIdxL0      = getRefFrameIdx((std::uint8_t)rf[0]);
        const int curFrameIndex          = setup.curOrderHint;
        const int frame0Index            = setup.refOrderHints[listIdx0][refIdxL0];
        curOffset0 = getRelativeDist(setup.orderHintInfo, curFrameIndex, frame0Index);

        if (rf[1] != kNoneFrame) {
            const std::uint8_t listIdx1  = getListIdx((std::uint8_t)rf[1]);
            const std::uint8_t refIdxL1  = getRefFrameIdx((std::uint8_t)rf[1]);
            const int frame1Index        = setup.refOrderHints[listIdx1][refIdxL1];
            curOffset1 = getRelativeDist(setup.orderHintInfo, curFrameIndex, frame1Index);
        }

        for (int blkRow = 0; blkRow < blkRowEnd; blkRow += stepH) {
            for (int blkCol = 0; blkCol < blkColEnd; blkCol += stepW) {
                const int ret = addTplRefMv(cm, setup, xd, miRow, miCol, refFrame, blkRow, blkCol, gmMvCandidates,
                                            refmvCount, symmetricRefs, mvRef0, curOffset0, curOffset1, refMvStack,
                                            modeContext);
                if (blkRow == 0 && blkCol == 0) {
                    isAvailable = ret;
                }
                ++mvRef0;
            }
        }

        if (isAvailable == 0) {
            *modeContext |= (1 << kGlobalMvOffset);
        }

        if (allowExtension) {
            const int voffset = setup.sb64SqNo4xnGeom
                                    ? xd.n4_h
                                    : (kMiSizeHigh[BLOCK_8X8] > xd.n4_h ? kMiSizeHigh[BLOCK_8X8] : xd.n4_h);
            const int hoffset = setup.sb64SqNo4xnGeom
                                    ? xd.n4_h
                                    : (kMiSizeWide[BLOCK_8X8] > xd.n4_w ? kMiSizeWide[BLOCK_8X8] : xd.n4_w);
            const int tplSamplePos[3][2] = {{voffset, -2}, {voffset, hoffset}, {voffset - 2, hoffset}};
            for (int i = 0; i < 3; ++i) {
                const int blkRow = tplSamplePos[i][0];
                const int blkCol = tplSamplePos[i][1];
                if (!checkSbBorder(miRow, miCol, blkRow, blkCol)) {
                    continue;
                }
                addTplRefMv(cm, setup, xd, miRow, miCol, refFrame, blkRow, blkCol, gmMvCandidates, refmvCount,
                            symmetricRefs, mvRef0, curOffset0, curOffset1, refMvStack, modeContext);
                ++mvRef0;
            }
        }
    }  // End temporal MVP

    // TOP-LEFT: scan the second outer area.
    std::uint8_t dummyNewmvCount = 0;
    scanBlkMbmi(xd, miRow, miCol, rf, -1, -1, refMvStack, &rowMatchCount, &dummyNewmvCount, gmMvCandidates, gmParams,
                refmvCount);

    // ROW-3 COL-3 ROW-5 COL-5
    for (std::int32_t idx = 2; idx <= 3; ++idx) {
        const std::int32_t rowOffset = -(idx << 1) + 1 + rowAdj;
        const std::int32_t colOffset = -(idx << 1) + 1 + colAdj;
        if (abs(rowOffset) <= abs(maxRowOffset) && abs(rowOffset) > processedRows) {
            scanRowMbmi(cm, xd, miCol, rf, rowOffset, refMvStack, refmvCount, &rowMatchCount, &dummyNewmvCount,
                        gmMvCandidates, gmParams, maxRowOffset, &processedRows);
        }
        if (abs(colOffset) <= abs(maxColOffset) && abs(colOffset) > processedCols) {
            scanColMbmi(cm, xd, miRow, rf, colOffset, refMvStack, refmvCount, &colMatchCount, &dummyNewmvCount,
                        gmMvCandidates, gmParams, maxColOffset, &processedCols);
        }
    }

    // Mode Context Derivation based on 3 counters
    const std::uint8_t refMatchCount = (rowMatchCount > 0) + (colMatchCount > 0);
    switch (nearestMatch) {
    case 0:
        if (refMatchCount >= 1) {
            *modeContext |= 1;
        }
        if (refMatchCount == 1) {
            *modeContext |= (1 << kRefMvOffset);
        } else if (refMatchCount >= 2) {
            *modeContext |= (2 << kRefMvOffset);
        }
        break;
    case 1:
        *modeContext |= (newmvCount > 0) ? 2 : 3;
        if (refMatchCount == 1) {
            *modeContext |= (3 << kRefMvOffset);
        } else if (refMatchCount >= 2) {
            *modeContext |= (4 << kRefMvOffset);
        }
        break;
    case 2:
    default:
        if (newmvCount >= 1) {
            *modeContext |= 4;
        } else {
            *modeContext |= 5;
        }
        *modeContext |= (5 << kRefMvOffset);
        break;
    }

    // Rank the likelihood and assign nearest and near mvs.
    if (*refmvCount > 1) {
        sortMvpTable(refMvStack, refmvCount);
    }

    // finish the Tables. If table is not full, re-scan ROW-1 and COL-1
    if (*refmvCount < kMaxMvRefCandidates) {
        scanRowColLight(cm, xd, miRow, miCol, rf, refMvStack, refmvCount, gmMvCandidates, maxRowOffset, maxColOffset);
    }

    // Clamp the final MVs
    for (std::uint8_t idx = 0; idx < *refmvCount; ++idx) {
        clampMvRef(&refMvStack[idx].this_mv, xd.n8_w << kMiSizeLog2, xd.n8_h << kMiSizeLog2, &xd);
        if (rf[1] > kNoneFrame) {
            clampMvRef(&refMvStack[idx].comp_mv, xd.n8_w << kMiSizeLog2, xd.n8_h << kMiSizeLog2, &xd);
        }
    }
}

// adaptive_mv_pred.c:973-976 / :978-981 block_center_x/y, verbatim.
int blockCenterX(int miCol, int bs) {
    const int bw = kBlockSizeWide[bs];
    return miCol * kMiSize + bw / 2 - 1;
}

int blockCenterY(int miRow, int bs) {
    const int bh = kBlockSizeHigh[bs];
    return miRow * kMiSize + bh / 2 - 1;
}

// adaptive_mv_pred.c:983-1036 svt_aom_gm_get_motion_vector_enc, verbatim
// (GM_TRANS_ONLY_PREC_DIFF = kGmTransOnlyPrecDiff = 13; the wmmat[0]/[1]
// swap is the documented AV1 spec bug the reference preserves,
// https://crbug.com/aomedia/3328; the ROTZOOM asserts are fixture-pinned).
Mv gmGetMotionVectorEnc(const WarpedMotionParams& gm, std::int32_t allowHp, int bsize, std::int32_t miCol,
                        std::int32_t miRow, std::int32_t isInteger) {
    Mv res;
    if (gm.wmtype == IDENTITY) {
        res.as_int = 0;
        return res;
    }
    if (gm.wmtype == TRANSLATION) {
        // All global motion vectors are stored with WARPEDMODEL_PREC_BITS (16)
        // bits of fractional precision. After the right shifts, there are 3
        // fractional bits of precision. If allow_hp is false, the bottom bit
        // is always zero. Note: the spec's assignments are accidentally
        // reversed, and this incorrect logic must be kept to match the spec.
        res.y = (std::int16_t)(gm.wmmat[0] >> kGmTransOnlyPrecDiff);
        res.x = (std::int16_t)(gm.wmmat[1] >> kGmTransOnlyPrecDiff);
    } else {
        const std::int32_t* mat = gm.wmmat;
        const int x = blockCenterX(miCol, bsize);
        const int y = blockCenterY(miRow, bsize);
        const int xc = (mat[2] - (1 << kWarpedModelPrecBits)) * x + mat[3] * y + mat[0];
        const int yc = mat[4] * x + (mat[5] - (1 << kWarpedModelPrecBits)) * y + mat[1];
        const int tx = convertToTransPrec(allowHp, xc);
        const int ty = convertToTransPrec(allowHp, yc);
        res.y = (std::int16_t)ty;
        res.x = (std::int16_t)tx;
    }
    if (isInteger) {
        integerMvPrecision(&res);
    }
    return res;
}

// adaptive_mv_pred.c:1128-1136 count_ref_match (named deviation: static
// INLINE in the reference; exported, cdefSign testability precedent).
void countRefMatch(const BlockModeInfo& bmi, int targetRf, std::uint8_t* matchCount, std::uint8_t* newmvCount) {
    if (bmi.ref_frame[0] == targetRf || bmi.ref_frame[1] == targetRf) {
        ++*matchCount;
        if (haveNewmvInInterMode(bmi.mode)) {
            ++*newmvCount;
        }
    }
}

// adaptive_mv_pred.c:1138-1327 svt_aom_compute_inter_mode_ctx_light, verbatim
// (ctx->blk_org_x/y >> MI_SIZE_LOG2 arrive as miCol/miRow; the reference
// writes ctx->inter_mode_ctx[ref_frame], the port returns the value -
// named deviation; sbSize = pcs->scs->seq_header.sb_size; MVREF_ROWS =
// MVREF_COLS = 3, adaptive_mv_pred.c:30-31; the GLOBALMV_OFFSET bit stays 0
// when MFMV is off - no TPL scan sets it).
std::int16_t computeInterModeCtxLight(const MacroBlockD& xd, const Av1Common& cm, std::int32_t miRow,
                                      std::int32_t miCol, int refFrame, int sbSize) {
    const TileInfo* const tile = &xd.tile;
    int rf[2];
    av1SetRefFrame(rf, (std::int8_t)refFrame);
    const int targetRf = rf[0];

    // Block size >= 8x8 in LPD1, so row_adj = col_adj = 0.
    std::int32_t maxRowOffset = 0, maxColOffset = 0;
    std::int32_t processedRows = 0, processedCols = 0;
    if (xd.up_available) {
        maxRowOffset = -(3 << 1);
        maxRowOffset = findValidRowOffset(*tile, miRow, maxRowOffset);
    }
    if (xd.left_available) {
        maxColOffset = -(3 << 1);
        maxColOffset = findValidColOffset(*tile, miCol, maxColOffset);
    }

    std::uint8_t rowMatch = 0, colMatch = 0, newmvCount = 0;
    const std::int32_t n8w = xd.n8_w;
    const std::int32_t n8h = xd.n8_h;
    const std::int32_t miStride = xd.mi_stride;

    // ---- ROW -1 ----
    if (abs(maxRowOffset) >= 1) {
        std::int32_t endMi = n8w < cm.mi_cols - miCol ? n8w : cm.mi_cols - miCol;
        endMi = endMi < kMiSizeWide[BLOCK_64X64] ? endMi : kMiSizeWide[BLOCK_64X64];
        const std::int32_t useStep16 = (n8w >= 16);
        const std::int32_t n8w16 = kMiSizeWide[BLOCK_16X16];
        MbModeInfo** const rowMi = xd.mi - miStride;
        for (std::int32_t i = 0; i < endMi;) {
            const MbModeInfo* cand = rowMi[i];
            std::int32_t len = n8w < kMiSizeWide[cand->bsize] ? n8w : kMiSizeWide[cand->bsize];
            if (useStep16) {
                len = n8w16 > len ? n8w16 : len;
            }
            if (n8w <= kMiSizeWide[cand->bsize]) {
                processedRows = -maxRowOffset < kMiSizeHigh[cand->bsize] ? -maxRowOffset : kMiSizeHigh[cand->bsize];
            }
            if (isInterBlock(cand->block_mi)) {
                countRefMatch(cand->block_mi, targetRf, &rowMatch, &newmvCount);
            }
            i += len;
        }
    }

    // ---- COL -1 ----
    if (abs(maxColOffset) >= 1) {
        std::int32_t endMi = n8h < cm.mi_rows - miRow ? n8h : cm.mi_rows - miRow;
        endMi = endMi < kMiSizeHigh[BLOCK_64X64] ? endMi : kMiSizeHigh[BLOCK_64X64];
        const std::int32_t useStep16 = (n8h >= 16);
        const std::int32_t n8h16 = kMiSizeHigh[BLOCK_16X16];
        for (std::int32_t i = 0; i < endMi;) {
            const MbModeInfo* cand = xd.mi[i * miStride - 1];
            std::int32_t len = n8h < kMiSizeHigh[cand->bsize] ? n8h : kMiSizeHigh[cand->bsize];
            if (useStep16) {
                len = n8h16 > len ? n8h16 : len;
            }
            if (n8h <= kMiSizeHigh[cand->bsize]) {
                processedCols = -maxColOffset < kMiSizeWide[cand->bsize] ? -maxColOffset : kMiSizeWide[cand->bsize];
            }
            if (isInterBlock(cand->block_mi)) {
                countRefMatch(cand->block_mi, targetRf, &colMatch, &newmvCount);
            }
            i += len;
        }
    }

    // ---- TOP-RIGHT ----
    if (hasTopRight(sbSize, xd, miRow, miCol, n8w > n8h ? n8w : n8h)) {
        if (miCol + n8w < tile->mi_col_end && miRow > tile->mi_row_start) {
            const MbModeInfo* cand = xd.mi[-miStride + n8w];
            if (isInterBlock(cand->block_mi)) {
                countRefMatch(cand->block_mi, targetRf, &rowMatch, &newmvCount);
            }
        }
    }

    const std::uint8_t nearestMatch = (rowMatch > 0) + (colMatch > 0);

    // ---- TOP-LEFT ----
    if (miCol > tile->mi_col_start && miRow > tile->mi_row_start) {
        const MbModeInfo* cand = xd.mi[-miStride - 1];
        if (isInterBlock(cand->block_mi) &&
            (cand->block_mi.ref_frame[0] == targetRf || cand->block_mi.ref_frame[1] == targetRf)) {
            ++rowMatch;
        }
    }

    // Once both rowMatch and colMatch are > 0, ref_match_count = 2 and
    // further outer-row/col scanning cannot change the mode_ctx output.
    if (!(rowMatch > 0 && colMatch > 0)) {
        // ---- Outer rows/cols (3, 5) ----
        for (std::int32_t idx = 2; idx <= 3; ++idx) {
            const std::int32_t rowOffset = -(idx << 1) + 1;
            const std::int32_t colOffset = -(idx << 1) + 1;
            if (abs(rowOffset) <= abs(maxRowOffset) && abs(rowOffset) > processedRows) {
                std::int32_t endMi = n8w < cm.mi_cols - miCol ? n8w : cm.mi_cols - miCol;
                endMi = endMi < kMiSizeWide[BLOCK_64X64] ? endMi : kMiSizeWide[BLOCK_64X64];
                const std::int32_t n8w16 = kMiSizeWide[BLOCK_16X16];
                const std::int32_t useStep16 = (n8w >= 16);
                MbModeInfo** const rowMi = xd.mi + rowOffset * miStride;
                for (std::int32_t i = 0; i < endMi;) {
                    const MbModeInfo* cand = rowMi[1 + i];
                    std::int32_t len = n8w < kMiSizeWide[cand->bsize] ? n8w : kMiSizeWide[cand->bsize];
                    if (useStep16) {
                        len = n8w16 > len ? n8w16 : len;
                    } else {
                        len = len > kMiSizeWide[BLOCK_8X8] ? len : kMiSizeWide[BLOCK_8X8];
                    }
                    if (n8w <= kMiSizeWide[cand->bsize]) {
                        processedRows = (-maxRowOffset + rowOffset + 1 < kMiSizeHigh[cand->bsize]
                                             ? -maxRowOffset + rowOffset + 1
                                             : kMiSizeHigh[cand->bsize]) - rowOffset - 1;
                    }
                    if (isInterBlock(cand->block_mi) &&
                        (cand->block_mi.ref_frame[0] == targetRf || cand->block_mi.ref_frame[1] == targetRf)) {
                        ++rowMatch;
                    }
                    i += len;
                }
            }
            if (abs(colOffset) <= abs(maxColOffset) && abs(colOffset) > processedCols) {
                std::int32_t endMi = n8h < cm.mi_rows - miRow ? n8h : cm.mi_rows - miRow;
                endMi = endMi < kMiSizeHigh[BLOCK_64X64] ? endMi : kMiSizeHigh[BLOCK_64X64];
                const std::int32_t n8h16 = kMiSizeHigh[BLOCK_16X16];
                const std::int32_t useStep16 = (n8h >= 16);
                for (std::int32_t i = 0; i < endMi;) {
                    const MbModeInfo* cand = xd.mi[(1 + i) * miStride + colOffset];
                    std::int32_t len = n8h < kMiSizeHigh[cand->bsize] ? n8h : kMiSizeHigh[cand->bsize];
                    if (useStep16) {
                        len = n8h16 > len ? n8h16 : len;
                    } else {
                        len = len > kMiSizeHigh[BLOCK_8X8] ? len : kMiSizeHigh[BLOCK_8X8];
                    }
                    if (n8h <= kMiSizeHigh[cand->bsize]) {
                        processedCols = (-maxColOffset + colOffset + 1 < kMiSizeWide[cand->bsize]
                                             ? -maxColOffset + colOffset + 1
                                             : kMiSizeWide[cand->bsize]) - colOffset - 1;
                    }
                    if (isInterBlock(cand->block_mi) &&
                        (cand->block_mi.ref_frame[0] == targetRf || cand->block_mi.ref_frame[1] == targetRf)) {
                        ++colMatch;
                    }
                    i += len;
                }
            }
            // Early exit if both sides matched
            if (rowMatch > 0 && colMatch > 0) {
                break;
            }
        }
    }

    // ---- Mode Context Derivation ----
    std::int16_t modeCtx = 0;
    const std::uint8_t refMatchCount = (rowMatch > 0) + (colMatch > 0);
    switch (nearestMatch) {
    case 0:
        if (refMatchCount >= 1) {
            modeCtx |= 1;
        }
        if (refMatchCount == 1) {
            modeCtx |= (1 << kRefMvOffset);
        } else if (refMatchCount >= 2) {
            modeCtx |= (2 << kRefMvOffset);
        }
        break;
    case 1:
        modeCtx |= (newmvCount > 0) ? 2 : 3;
        if (refMatchCount == 1) {
            modeCtx |= (3 << kRefMvOffset);
        } else if (refMatchCount >= 2) {
            modeCtx |= (4 << kRefMvOffset);
        }
        break;
    default:
        modeCtx |= (newmvCount >= 1) ? 4 : 5;
        modeCtx |= (5 << kRefMvOffset);
        break;
    }
    return modeCtx;
}

// inter_prediction.h:312-343 / :345-376 compound_ref0_mode / compound_ref1_mode,
// the two inter-compound LUTs verbatim (the assert is fixture-pinned).
static int compoundRef0Mode(int mode) {
    static const int lut[MB_MODE_COUNT] = {MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT,
                                           MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT,
                                           MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT,
                                           MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT,
                                           MB_MODE_COUNT, NEARESTMV, NEARMV, NEARESTMV, NEWMV, NEARMV, NEWMV,
                                           GLOBALMV, NEWMV};
    return lut[mode];
}

static int compoundRef1Mode(int mode) {
    static const int lut[MB_MODE_COUNT] = {MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT,
                                           MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT,
                                           MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT,
                                           MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT, MB_MODE_COUNT,
                                           MB_MODE_COUNT, NEARESTMV, NEARMV, NEWMV, NEARESTMV, NEWMV, NEARMV,
                                           GLOBALMV, NEWMV};
    return lut[mode];
}

// adaptive_mv_pred.c:1329-1405 svt_aom_generate_av1_mvp_table, verbatim (the
// ctx/pcs/blk_geom walls threaded as parameters: ctx->ref_mv_stack ->
// refMvStack [kModeCtxRefFrames][kMaxRefMvStackSize], ctx->inter_mode_ctx ->
// interModeCtx, xd->ref_mv_count stays on the carrier; bsize = blk_geom->bsize;
// pred_structure == RANDOM_ACCESS -> predRandomAccess).
void generateAv1MvpTable(MacroBlockD& xd, const Av1Common& cm, const MvRefSetup& setup,
                         const WarpedMotionParams* globalMotion, std::uint8_t allowHighPrecisionMv,
                         std::uint8_t forceIntegerMv, int bsize, std::uint16_t blkOrgX, std::uint16_t blkOrgY,
                         const int* refFrames, std::uint32_t totRefs, std::uint8_t temporalLayerIndex,
                         bool predRandomAccess, CandidateMv refMvStack[kModeCtxRefFrames][kMaxRefMvStackSize],
                         std::int16_t* interModeCtx, std::uint8_t* refMvCount) {
    const std::int32_t miRow = blkOrgY >> kMiSizeLog2;
    const std::int32_t miCol = blkOrgX >> kMiSizeLog2;

    std::uint8_t symtericRefs = 0;
    Mv mvRef0[64];
    if (temporalLayerIndex > 0) {
        if (predRandomAccess) {
            if (totRefs == 3 && refFrames[0] == kLastFrame && refFrames[1] == kBwdrefFrame &&
                refFrames[2] == kLastBwdFrame) {
                symtericRefs = 1;
            }
        }
    }

    // 128x128 OFF, 4xN OFF, SQ only
    for (std::uint32_t refIt = 0; refIt < totRefs; ++refIt) {
        const int refFrame = refFrames[refIt];
        int rf[2];
        av1SetRefFrame(rf, (std::int8_t)refFrame);

        xd.ref_mv_count[refFrame] = 0;
        for (int s = 0; s < kMaxRefMvStackSize; ++s) {
            refMvStack[refFrame][s] = CandidateMv{};
        }

        Mv gmMv[2];
        if (refFrame == kIntraFrame) {
            gmMv[0].as_int = gmMv[1].as_int = 0;
        } else {
            if (refFrame < kRefFrames) {
                gmMv[0] = gmGetMotionVectorEnc(globalMotion[refFrame], allowHighPrecisionMv, bsize, miCol, miRow,
                                               forceIntegerMv);
                gmMv[1].as_int = 0;
            } else {
                gmMv[0] = gmGetMotionVectorEnc(globalMotion[rf[0]], allowHighPrecisionMv, bsize, miCol, miRow,
                                               forceIntegerMv);
                gmMv[1] = gmGetMotionVectorEnc(globalMotion[rf[1]], allowHighPrecisionMv, bsize, miCol, miRow,
                                               forceIntegerMv);
            }
        }

        setupRefMvList(setup, cm, xd, refFrame, &refMvCount[refFrame], refMvStack[refFrame], gmMv, globalMotion,
                       miRow, miCol, symtericRefs, mvRef0, &interModeCtx[refFrame]);
    }
}

// adaptive_mv_pred.c:1407-1457 svt_aom_get_av1_mv_pred_drl, verbatim (the
// ctx->ref_mv_stack/blk_ptr->av1xd walls threaded as parameters; the
// compound_ref mode LUTs above).
void getAv1MvPredDrl(const CandidateMv refMvStack[kModeCtxRefFrames][kMaxRefMvStackSize], MacroBlockD& xd,
                     int refFrame, std::uint8_t isCompound, int mode, std::uint8_t drlIndex, Mv nearestmv[2],
                     Mv nearmv[2], Mv refMv[2]) {
    if (!isCompound && mode != GLOBALMV) {
        nearestmv[0] = refMvStack[refFrame][0].this_mv;
        nearmv[0]    = refMvStack[refFrame][1].this_mv;
    }
    if (isCompound && mode != GLOBAL_GLOBALMV) {
        const std::int32_t refMvIdx = drlIndex + 1;
        nearestmv[0]                = refMvStack[refFrame][0].this_mv;
        nearestmv[1]                = refMvStack[refFrame][0].comp_mv;
        nearmv[0]                   = refMvStack[refFrame][refMvIdx].this_mv;
        nearmv[1]                   = refMvStack[refFrame][refMvIdx].comp_mv;
    } else if (drlIndex > 0 && mode == NEARMV) {
        const Mv curMv = refMvStack[refFrame][1 + drlIndex].this_mv;
        nearmv[0]      = curMv;
    }

    refMv[0] = nearestmv[0];
    refMv[1] = nearestmv[1];

    if (isCompound) {
        std::int32_t refMvIdx = drlIndex;
        // Special case: NEAR_NEWMV and NEW_NEARMV modes use 1 + mbmi->ref_mv_idx
        // (like NEARMV) instead of mbmi->ref_mv_idx (like NEWMV)
        if (mode == NEAR_NEWMV || mode == NEW_NEARMV) {
            refMvIdx = 1 + drlIndex;
        }
        if (compoundRef0Mode(mode) == NEWMV) {
            refMv[0] = refMvStack[refFrame][refMvIdx].this_mv;
        }
        if (compoundRef1Mode(mode) == NEWMV) {
            refMv[1] = refMvStack[refFrame][refMvIdx].comp_mv;
        }
    } else {
        if (mode == NEWMV) {
            if (xd.ref_mv_count[refFrame] > 1) {
                refMv[0] = refMvStack[refFrame][drlIndex].this_mv;
            }
        }
    }
}

// adaptive_mv_pred.c:1594-1605 record_samples (named deviation: static in
// the reference). Samples returned are at 1/8-pel precision: the neighbor
// block center point's coordinates relative to the left-top pixel of the
// current block.
void recordSamples(const MbModeInfo& mbmi, int* pts, int* ptsInref, int rowOffset, int signR, int colOffset,
                   int signC) {
    const std::uint8_t bw = kBlockSizeWide[mbmi.bsize];
    const std::uint8_t bh = kBlockSizeHigh[mbmi.bsize];
    const int x = colOffset * kMiSize + signC * (bw > kMiSize ? bw : kMiSize) / 2 - 1;
    const int y = rowOffset * kMiSize + signR * (bh > kMiSize ? bh : kMiSize) / 2 - 1;
    pts[0]       = (x * 8);
    pts[1]       = (y * 8);
    ptsInref[0]  = (x * 8) + mbmi.block_mi.mv[0].x;
    ptsInref[1]  = (y * 8) + mbmi.block_mi.mv[0].y;
}

// adaptive_mv_pred.c:1610-1750 av1_find_samples, verbatim (has_top_right ->
// hasTopRight, is_inside -> isInside, the Pos struct flattened,
// LEAST_SQUARES_SAMPLES_MAX -> kLeastSquaresSamplesMax).
std::uint8_t av1FindSamples(const Av1Common& cm, int sbSize, MacroBlockD& xd, int rf0, int* pts, int* ptsInref) {
    const int miRow = xd.mi_row;
    const int miCol = xd.mi_col;
    const int upAvailable = xd.up_available;
    const int leftAvailable = xd.left_available;
    int i, miStep = 1, np = 0;
    const TileInfo* const tile = &xd.tile;
    int doTl = 1;
    int doTr = 1;

    // scan the nearest above rows
    if (upAvailable) {
        const int miRowOffset = -1;
        MbModeInfo* mbmi = xd.mi[miRowOffset * xd.mi_stride];
        std::uint8_t n4w = kMiSizeWide[mbmi->bsize];
        if (xd.n4_w <= n4w) {
            // Handle "current block width <= above block width" case.
            int colOffset = -miCol % n4w;
            if (colOffset < 0) {
                doTl = 0;
            }
            if (colOffset + n4w > xd.n4_w) {
                doTr = 0;
            }
            if (mbmi->block_mi.ref_frame[0] == rf0 && mbmi->block_mi.ref_frame[1] == kNoneFrame) {
                recordSamples(*mbmi, pts, ptsInref, 0, -1, colOffset, 1);
                pts += 2;
                ptsInref += 2;
                ++np;
                if (np >= kLeastSquaresSamplesMax) {
                    return kLeastSquaresSamplesMax;
                }
            }
        } else {
            // Handle "current block width > above block width" case.
            for (i = 0; i < (xd.n4_w < cm.mi_cols - miCol ? xd.n4_w : cm.mi_cols - miCol); i += miStep) {
                const int miColOffset = i;
                mbmi = xd.mi[miColOffset + miRowOffset * xd.mi_stride];
                n4w = kMiSizeWide[mbmi->bsize];
                miStep = xd.n4_w < n4w ? xd.n4_w : n4w;
                if (mbmi->block_mi.ref_frame[0] == rf0 && mbmi->block_mi.ref_frame[1] == kNoneFrame) {
                    recordSamples(*mbmi, pts, ptsInref, 0, -1, i, 1);
                    pts += 2;
                    ptsInref += 2;
                    ++np;
                    if (np >= kLeastSquaresSamplesMax) {
                        return kLeastSquaresSamplesMax;
                    }
                }
            }
        }
    }

    // scan the nearest left columns
    if (leftAvailable) {
        const int miColOffset = -1;
        MbModeInfo* mbmi = xd.mi[miColOffset];
        std::uint8_t n4h = kMiSizeHigh[mbmi->bsize];
        if (xd.n4_h <= n4h) {
            // Handle "current block height <= above block height" case.
            int rowOffset = -miRow % n4h;
            if (rowOffset < 0) {
                doTl = 0;
            }
            if (mbmi->block_mi.ref_frame[0] == rf0 && mbmi->block_mi.ref_frame[1] == kNoneFrame) {
                recordSamples(*mbmi, pts, ptsInref, rowOffset, 1, 0, -1);
                pts += 2;
                ptsInref += 2;
                ++np;
                if (np >= kLeastSquaresSamplesMax) {
                    return kLeastSquaresSamplesMax;
                }
            }
        } else {
            // Handle "current block height > above block height" case.
            for (i = 0; i < (xd.n4_h < cm.mi_rows - miRow ? xd.n4_h : cm.mi_rows - miRow); i += miStep) {
                const int miRowOffset = i;
                mbmi = xd.mi[miColOffset + miRowOffset * xd.mi_stride];
                n4h = kMiSizeHigh[mbmi->bsize];
                miStep = xd.n4_h < n4h ? xd.n4_h : n4h;
                if (mbmi->block_mi.ref_frame[0] == rf0 && mbmi->block_mi.ref_frame[1] == kNoneFrame) {
                    recordSamples(*mbmi, pts, ptsInref, i, 1, 0, -1);
                    pts += 2;
                    ptsInref += 2;
                    ++np;
                    if (np >= kLeastSquaresSamplesMax) {
                        return kLeastSquaresSamplesMax;
                    }
                }
            }
        }
    }

    // Top-left block
    if (doTl && leftAvailable && upAvailable) {
        MbModeInfo* mbmi = xd.mi[-1 + -1 * xd.mi_stride];
        if (mbmi->block_mi.ref_frame[0] == rf0 && mbmi->block_mi.ref_frame[1] == kNoneFrame) {
            recordSamples(*mbmi, pts, ptsInref, 0, -1, 0, -1);
            pts += 2;
            ptsInref += 2;
            ++np;
            if (np >= kLeastSquaresSamplesMax) {
                return kLeastSquaresSamplesMax;
            }
        }
    }

    // Top-right block
    if (doTr && hasTopRight(sbSize, xd, miRow, miCol, xd.n4_w > xd.n4_h ? xd.n4_w : xd.n4_h)) {
        // Pos trb_pos = {-1, xd->n4_w};
        if (isInside(*tile, miCol, miRow, -1, xd.n4_w)) {
            const int miRowOffset = -1;
            const int miColOffset = xd.n4_w;
            MbModeInfo* mbmi = xd.mi[miColOffset + miRowOffset * xd.mi_stride];
            if (mbmi->block_mi.ref_frame[0] == rf0 && mbmi->block_mi.ref_frame[1] == kNoneFrame) {
                recordSamples(*mbmi, pts, ptsInref, 0, -1, xd.n4_w, 1);
                ++np;
                if (np >= kLeastSquaresSamplesMax) {
                    return kLeastSquaresSamplesMax;
                }
            }
        }
    }

    return (std::uint8_t)np;
}

// adaptive_mv_pred.c:2002-2028 svt_av1_get_ref_mv_from_stack, verbatim.
Mv getRefMvFromStack(int refIdx, const int* refFrame, int refMvIdx,
                     const CandidateMv refMvStack[kModeCtxRefFrames][kMaxRefMvStackSize], const MacroBlockD& xd) {
    const std::int8_t refFrameType = av1RefFrameType(refFrame);
    const CandidateMv* currRefMvStack = refMvStack[refFrameType];
    Mv refMv;
    refMv.as_int = kInvalidMv;
    if (refFrame[1] > kIntraFrame) {
        if (refIdx == 0) {
            refMv = currRefMvStack[refMvIdx].this_mv;
        } else {
            refMv = currRefMvStack[refMvIdx].comp_mv;
        }
    } else {
        if (refMvIdx < xd.ref_mv_count[refFrameType]) {
            refMv = currRefMvStack[refMvIdx].this_mv;
        } else {
            // got this from decoder read_intrabc_info global_mvs[ref_frame]
            refMv.as_int = kInvalidMv;
        }
    }
    return refMv;
}

// adaptive_mv_pred.c:2030-2038 svt_av1_find_best_ref_mvs_from_stack, verbatim.
void findBestRefMvsFromStack(const CandidateMv refMvStack[kModeCtxRefFrames][kMaxRefMvStackSize],
                             const MacroBlockD& xd, int refFrame, Mv* nearestMv, Mv* nearMv) {
    const int refIdx = 0;
    const int refFrames[2] = {refFrame, kNoneFrame};
    *nearestMv = getRefMvFromStack(refIdx, refFrames, 0, refMvStack, xd);
    lowerMvPrecision(nearestMv, 0);
    *nearMv = getRefMvFromStack(refIdx, refFrames, 1, refMvStack, xd);
    lowerMvPrecision(nearMv, 0);
}

// inter_prediction.h:378-380 check_sb_border, verbatim (mi_size_wide ->
// kMiSizeWide).
int checkSbBorder(int miRow, int miCol, int rowOffset, int colOffset) {
    const int sbMiSize = kMiSizeWide[BLOCK_64X64];
    const int row = miRow & (sbMiSize - 1);
    const int col = miCol & (sbMiSize - 1);
    if (row + rowOffset < 0 || row + rowOffset >= sbMiSize || col + colOffset < 0 ||
        col + colOffset >= sbMiSize) {
        return 0;
    }
    return 1;
}

// inter_prediction.h:242-244 is_neighbor_overlappable, verbatim (AV1 spec
// 7.10.3: overlappable neighbours not relevant for intra frames).
int isNeighborOverlappable(const MbModeInfo& mbmi) {
    return mbmi.block_mi.ref_frame[0] > kIntraFrame;
}

// adaptive_mv_pred.c:1830-1862 count_overlappable_nb_above (named deviation:
// static in the reference), verbatim.
std::uint32_t countOverlappableNbAbove(const Av1Common& cm, MacroBlockD& xd, std::int32_t miCol,
                                       std::uint32_t nbMax) {
    std::uint32_t nbCount = 0;
    if (!xd.up_available) {
        return nbCount;
    }
    // prev_row_mi points into the mi array, starting at the beginning of
    // the previous row.
    MbModeInfo** prevRowMi = xd.mi - miCol - 1 * xd.mi_stride;
    const int endCol = miCol + xd.n4_w < cm.mi_cols ? miCol + xd.n4_w : cm.mi_cols;
    std::uint8_t miStep;
    for (int aboveMiCol = miCol; aboveMiCol < endCol && nbCount < nbMax; aboveMiCol += miStep) {
        MbModeInfo** aboveMi = prevRowMi + aboveMiCol;
        miStep = kMiSizeWide[aboveMi[0]->bsize] < kMiSizeWide[BLOCK_64X64] ? kMiSizeWide[aboveMi[0]->bsize]
                                                                          : kMiSizeWide[BLOCK_64X64];
        // If we're considering a block with width 4, it should be treated as
        // half of a pair of blocks with chroma information in the second. Move
        // above_mi_col back to the start of the pair if needed, set above_mbmi
        // to point at the block with chroma information, and set mi_step to 2
        // to step over the entire pair at the end of the iteration.
        if (miStep == 1) {
            aboveMiCol &= ~1;
            aboveMi = prevRowMi + aboveMiCol + 1;
            miStep = 2;
        }
        if (isNeighborOverlappable(*aboveMi[0])) {
            ++nbCount;
        }
    }
    return nbCount;
}

// adaptive_mv_pred.c:1864-1891 count_overlappable_nb_left (named deviation:
// static in the reference), verbatim.
std::uint32_t countOverlappableNbLeft(const Av1Common& cm, MacroBlockD& xd, std::int32_t miRow,
                                      std::uint32_t nbMax) {
    std::uint32_t nbCount = 0;
    if (!xd.left_available) {
        return nbCount;
    }
    // prev_col_mi points into the mi array, starting at the top of the
    // previous column
    MbModeInfo** prevColMi = xd.mi - 1 - miRow * xd.mi_stride;
    const int endRow = miRow + xd.n4_h < cm.mi_rows ? miRow + xd.n4_h : cm.mi_rows;
    std::uint8_t miStep;
    for (int leftMiRow = miRow; leftMiRow < endRow && nbCount < nbMax; leftMiRow += miStep) {
        MbModeInfo** leftMi = prevColMi + leftMiRow * xd.mi_stride;
        miStep = kMiSizeHigh[leftMi[0]->bsize] < kMiSizeHigh[BLOCK_64X64] ? kMiSizeHigh[leftMi[0]->bsize]
                                                                         : kMiSizeHigh[BLOCK_64X64];
        if (miStep == 1) {
            leftMiRow &= ~1;
            leftMi = prevColMi + (leftMiRow + 1) * xd.mi_stride;
            miStep = 2;
        }
        if (isNeighborOverlappable(*leftMi[0])) {
            ++nbCount;
        }
    }
    return nbCount;
}

// adaptive_mv_pred.c:1893-1906 svt_av1_count_overlappable_neighbors, verbatim
// (the pcs/blk_ptr walls threaded; the reference writes
// blk_ptr->overlappable_neighbors, the port returns the value - named
// deviation).
std::uint32_t countOverlappableNeighbors(const Av1Common& cm, MacroBlockD& xd, int bsize, std::int32_t miRow,
                                         std::int32_t miCol) {
    if (!isMotionVariationAllowedBsize(bsize)) {
        return 0;
    }
    std::uint32_t overlappableNeighbors = countOverlappableNbAbove(cm, xd, miCol, 0xFFFFFFFFu);
    overlappableNeighbors += countOverlappableNbLeft(cm, xd, miRow, 0xFFFFFFFFu);
    return overlappableNeighbors;
}

// adaptive_mv_pred.h:47-49 has_overlappable_candidates (the BlkStruct wall
// threaded: the counter arrives as a parameter).
bool hasOverlappableCandidates(std::uint32_t overlappableNeighbors) {
    return (overlappableNeighbors != 0);
}

// common_utils.h:315-320 is_chroma_reference, verbatim.
bool isChromaReference(int miRow, int miCol, int bsize, int ssX, int ssY) {
    const int bw = kMiSizeWide[bsize];
    const int bh = kMiSizeHigh[bsize];
    return ((miRow & 0x01) || !(bh & 0x01) || !ssY) && ((miCol & 0x01) || !(bw & 0x01) || !ssX);
}

// adaptive_mv_pred.c:1908-2000 svt_aom_is_dv_valid, verbatim
// (block_size_wide/high -> kBlockSizeWide/High, MI_SIZE -> kMiSize,
//  INTRABC_DELAY_SB64 -> kIntrabcDelaySb64).
int isDvValid(Mv dv, const MacroBlockD& xd, int mi_row, int mi_col, int bsize, int mib_size_log2) {
    const int bw             = kBlockSizeWide[bsize];
    const int bh             = kBlockSizeHigh[bsize];
    const int scale_px_to_mv = 8;
    // Disallow subpixel for now
    // SUBPEL_MASK is not the correct scale
    if (((dv.y & (scale_px_to_mv - 1)) || (dv.x & (scale_px_to_mv - 1)))) {
        return 0;
    }

    const TileInfo* const tile = &xd.tile;
    // Is the source top-left inside the current tile?
    const int src_top_edge  = mi_row * kMiSize * scale_px_to_mv + dv.y;
    const int tile_top_edge = tile->mi_row_start * kMiSize * scale_px_to_mv;
    if (src_top_edge < tile_top_edge) {
        return 0;
    }
    const int src_left_edge  = mi_col * kMiSize * scale_px_to_mv + dv.x;
    const int tile_left_edge = tile->mi_col_start * kMiSize * scale_px_to_mv;
    if (src_left_edge < tile_left_edge) {
        return 0;
    }
    // Is the bottom right inside the current tile?
    const int src_bottom_edge  = (mi_row * kMiSize + bh) * scale_px_to_mv + dv.y;
    const int tile_bottom_edge = tile->mi_row_end * kMiSize * scale_px_to_mv;
    if (src_bottom_edge > tile_bottom_edge) {
        return 0;
    }
    const int src_right_edge  = (mi_col * kMiSize + bw) * scale_px_to_mv + dv.x;
    const int tile_right_edge = tile->mi_col_end * kMiSize * scale_px_to_mv;
    if (src_right_edge > tile_right_edge) {
        return 0;
    }

    // Special case for sub 8x8 chroma cases, to prevent referring to chroma
    // pixels outside current tile.
    for (int plane = 1; plane < 3 /* av1_num_planes(cm)*/; ++plane) {
        if (isChromaReference(mi_row, mi_col, bsize, 1, 1)) {
            if (bw < 8) {
                if (src_left_edge < tile_left_edge + 4 * scale_px_to_mv) {
                    return 0;
                }
            }
            if (bh < 8) {
                if (src_top_edge < tile_top_edge + 4 * scale_px_to_mv) {
                    return 0;
                }
            }
        }
    }

    // Is the bottom right within an already coded SB? Also consider additional
    // constraints to facilitate HW decoder.
    const int max_mib_size       = 1 << mib_size_log2;
    const int active_sb_row      = mi_row >> mib_size_log2;
    const int active_sb64_col    = (mi_col * kMiSize) >> 6;
    const int sb_size            = max_mib_size * kMiSize;
    const int src_sb_row         = ((src_bottom_edge >> 3) - 1) / sb_size;
    const int src_sb64_col       = ((src_right_edge >> 3) - 1) >> 6;
    const int total_sb64_per_row = ((tile->mi_col_end - tile->mi_col_start - 1) >> 4) + 1;
    const int active_sb64        = active_sb_row * total_sb64_per_row + active_sb64_col;
    const int src_sb64           = src_sb_row * total_sb64_per_row + src_sb64_col;
    if (src_sb64 >= active_sb64 - kIntrabcDelaySb64) {
        return 0;
    }

    // Wavefront constraint: use only top left area of frame for reference.
    const int gradient  = 1 + kIntrabcDelaySb64 + (sb_size > 64);
    const int wf_offset = gradient * (active_sb_row - src_sb_row);
    if (src_sb_row > active_sb_row || src_sb64_col >= active_sb64_col - kIntrabcDelaySb64 + wf_offset) {
        return 0;
    }

    //add a SW-Wavefront constraint
    if (sb_size == 64) {
        if (src_sb64_col > active_sb64_col + (active_sb_row - src_sb_row)) {
            return 0;
        }
    } else {
        const int src_sb128_col    = ((src_right_edge >> 3) - 1) >> 7;
        const int active_sb128_col = (mi_col * kMiSize) >> 7;

        if (src_sb128_col > active_sb128_col + (active_sb_row - src_sb_row)) {
            return 0;
        }
    }

    return 1;
}

}  // namespace amvp
}  // namespace motion
