// l6_pipeline - the coding-loop pure helpers (A6, A6C).
// Reference files: product_coding_loop.c (the A4 md-coding-loop surface),
// coding_loop.c, md_process.c. The struct-free convention (PO1/SRC1 and
// the RE8A DeblockContext / EMC1 carrier precedents): every wall the
// ported spans consume travels as a carrier struct with the reference
// field names; the bodies are verbatim. Tables the spans need that are
// already ported are REUSED (entropy:: l7, motion::amvp l5, l16_rdo);
// tables not yet ported anywhere land here as extern data with the
// common_utils.c / definitions.h citation.
//
// PARKED (named, not faked):
//   product_coding_loop_init_fast_loop :958-:964 - the body sets
//     ctx->tx_depth = ctx->blk_ptr->block_mi.tx_depth = 0 then calls
//     svt_aom_coding_loop_context_generation (rd_cost.c:1475+), whose body
//     is dominated by the ModeDecisionContext/xd state wall (kf ctx, inter
//     ctx, skip ctx, neighbour ref counts). Not ported.
//   md_update_all_neighbour_arrays' svt_aom_update_mi_map call (:672) -
//     the mi-map mutation quartet is parked per AMVP1 (the
//     update_mi_map_enc_dec / copy_mi_map_grid_c / get_mbmi /
//     update_mi_map walls); the ported span keeps the gate and the
//     neighbour-array update, the mi-map call named-parked.
//   av1_lambda_assign_md's pcs->scs->static_config.rtc /
//     stats_based_sb_lambda_modulation / lambda_weight / update_type
//     inputs travel as scalars on the LambdaPcs carrier (the walls are
//     threaded, not represented).
#pragma once

#include <cstdint>
#include <cstddef>

#include <entropy.h>
#include <amvp.h>
#include <utility.h>
#include <rc_arith.h>

// The l16_rdo surface joins here. rate_est.h redefines four macros
// BR_CDF_SIZE / PARTITION_CONTEXTS / TX_PAD_VER / TX_PAD_2D that
// entropy.h already defined - with IDENTICAL values (checked: 4/(4),
// 20/product, (0+4)/(sum), the same TX_PAD_2D formula), so the C4005
// pairs are benign; the pragma window is the join device.
#pragma warning(push)
#pragma warning(disable : 4005)
#include <md_slices.h>
#include <mode_config.h>
#pragma warning(pop)

// rate_est.h's token-table macros (TX_SIZES / TX_32X32 / TX_4X4 / TX_16X16,
// :137-:140) shadow the entropy::TxSize enumerator names in any TU that
// sees both headers; the values are the same ordinals, so the undefs
// restore the qualified-enum reads the l6 signatures need. PLANE_TYPES
// (:141) is not re-qualified by this layer and stays.
#undef TX_SIZES
#undef TX_32X32
#undef TX_4X4
#undef TX_16X16

namespace pipeline {

// The l7 partition-context lookup (entropy.cpp:635-:637, the
// definitions.h:1547-:1574 table) is a const namespace-scope definition -
// internal linkage in C++ - and the l7 header does not export it; the
// pair mirrors the same 22-row values here (the l16-duplication
// precedent; the values were diff-read from the entropy.cpp definition).
extern const std::uint8_t partitionContextLookupAbove[entropy::BLOCK_SIZES_ALL];
extern const std::uint8_t partitionContextLookupLeft[entropy::BLOCK_SIZES_ALL];

// definitions.h:907 BLOCK_INVALID = 255. The l7 entropy BlockSize mirror
// (the l6 header dependency) omits the sentinel; the value carries here.
constexpr entropy::BlockSize kBlockInvalid = static_cast<entropy::BlockSize>(255);

// mv.h:42-55 (the Mv union) via the l5 port; Mv2 is the local alias so the
// l6 signature does not name another layer's namespace in the header.
using Mv2 = motion::amvp::Mv;
// MvReferenceFrame ordinals (definitions.h:1358-1368) via the l5 constants
// + the one l16 value this slice writes (INVALID_REF = 0xF,
// mode_decision.h:206).
constexpr std::int32_t kInvalidRef = 0xF;

// ---------------------------------------------------------------------------
// Shared constants (each cited)
// ---------------------------------------------------------------------------

// definitions.h:327 ROUND_UV; :324/:325 MAX_TXB_COUNT / MAX_TXB_COUNT_UV.
constexpr std::uint32_t kRoundUvX = 8;
constexpr std::uint32_t kMaxTxbCount = 16;
constexpr std::uint32_t kMaxTxbCountUv = 4;
constexpr std::uint32_t kMaxVartxDepth = 2;  // definitions.h:408 MAX_TX_DEPTH
constexpr std::uint32_t kMiSizeLog2 = 2;     // definitions.h:353 MI_SIZE_LOG2
// definitions.h:2052-2055 ChromaSearchMode: CHROMA_MODE_1 = 1.
constexpr std::uint32_t kChromaMode1 = 1;
// product_coding_loop.c:47 DIVIDE_AND_ROUND.
constexpr std::uint32_t kDivideAndRoundShift = 1;
// coding_unit.h:36-37 MAX_CU_COST / MAX_MODE_COST + md_rate_estimation.h:29
// AV1_PROB_COST_SHIFT.
constexpr std::uint64_t kMaxCuCost = 0xFFFFFFFFFFFFFFFFull >> 1;
constexpr std::uint64_t kMaxModeCost = 13754408443200ull * 8;
constexpr std::int32_t kAv1ProbCostShift = 9;

// ---------------------------------------------------------------------------
// The neighbour array unit (neighbor_arrays.h:22-51 + the primitive
// family; the reference file is A3's per tools/coverage_areas.txt - the
// primitives land here because the A6C spans are their only consumers).
// ---------------------------------------------------------------------------

// neighbor_arrays.h:26-39: the array kind ordinals and the mask macros.
enum NaArrayType { NA_LEFT = 0, NA_TOP = 1, NA_TOPLEFT = 2, NA_INVALID_ = ~0u };
constexpr std::uint8_t kNaLeftMask = 1 << NA_LEFT;
constexpr std::uint8_t kNaTopMask = 1 << NA_TOP;
constexpr std::uint8_t kNaTopLeftMask = 1 << NA_TOPLEFT;
constexpr std::uint8_t kNaFullMask = kNaLeftMask | kNaTopMask | kNaTopLeftMask;
constexpr std::uint8_t kNaTopAndLeftOnlyMask = kNaLeftMask | kNaTopMask;
// neighbor_arrays.h:30 (the reset fill value, via the enum's ~0).
constexpr std::uint8_t kNaInvalidFill = 0xFF;
// neighbor_arrays.h:23-24.
constexpr std::uint8_t kPuNeighborArrayGranularity = 4;
constexpr std::uint8_t kSampleNeighborArrayGranularity = 1;

// neighbor_arrays.h:41-51, the consumed field set. DEVIATION (named): the
// reference ctor EB_MALLOCs the three arrays; the port's carrier holds
// caller-owned buffers (the test helper sizes them from the same
// arithmetic), so dctor/EB_FREE are represented by nothing.
struct NeighborArrayUnit {
    std::uint8_t* left_array;
    std::uint8_t* top_array;
    std::uint8_t* top_left_array;
    std::uint32_t left_array_size;
    std::uint32_t top_array_size;
    std::uint32_t top_left_array_size;
    std::uint8_t unit_size;
    std::uint8_t granularity_log2;
};

// svt_aom_neighbor_array_unit_ctor (neighbor_arrays.c:25-52), the sizing +
// granularity arithmetic only (the allocation is the caller's, named
// deviation above).
void neighborArrayUnitCtor(NeighborArrayUnit* na_unit_ptr, std::uint32_t max_picture_width,
                           std::uint32_t max_picture_height, std::uint32_t unit_size,
                           std::uint8_t granularity_normal, std::uint8_t type_mask);

// svt_aom_neighbor_array_unit_reset (neighbor_arrays.c:54-68).
void neighborArrayUnitReset(NeighborArrayUnit* na_unit_ptr);

// neighbor_arrays.h:59-71: the index helpers.
std::uint32_t getNeighborArrayUnitLeftIndex(const NeighborArrayUnit* na_unit_ptr, std::uint32_t loc_y);
std::uint32_t getNeighborArrayUnitTopIndex(const NeighborArrayUnit* na_unit_ptr, std::uint32_t loc_x);
std::uint32_t getNeighborArrayUnitTopLeftIndex(const NeighborArrayUnit* na_unit_ptr, std::int32_t loc_x,
                                               std::int32_t loc_y);

// neighbor_arrays.h:132-191 (unit_size=1 path via the CONFIG=0 fold).
void neighborArrayUnitSampleWrite(NeighborArrayUnit* na_unit_ptr, const std::uint8_t* src_ptr, std::uint32_t stride,
                                  std::uint32_t src_origin_x, std::uint32_t src_origin_y,
                                  std::uint32_t pic_origin_x, std::uint32_t pic_origin_y, std::uint32_t bw,
                                  std::uint32_t bh, std::uint8_t mask);
// neighbor_arrays.h:329-373, the 16-bit twin (unit_size=2; the CONFIG=0
// inlined twin).
void neighborArrayUnit16bitSampleWrite(NeighborArrayUnit* na_unit_ptr, const std::uint16_t* src_ptr,
                                       std::uint32_t stride, std::uint32_t src_origin_x,
                                       std::uint32_t src_origin_y, std::uint32_t pic_origin_x,
                                       std::uint32_t pic_origin_y, std::uint32_t bw, std::uint32_t bh,
                                       std::uint8_t mask);

// neighbor_arrays.h:210-232.
void updateReconNeighborArray(NeighborArrayUnit* na_unit_ptr, const std::uint8_t* src_ptr_top,
                              const std::uint8_t* src_ptr_left, std::uint32_t pic_origin_x,
                              std::uint32_t pic_origin_y, std::uint32_t bw, std::uint32_t bh);
// neighbor_arrays.h:238-258.
void updateReconNeighborArray16bit(NeighborArrayUnit* na_unit_ptr, const std::uint16_t* src_ptr_top,
                                   const std::uint16_t* src_ptr_left, std::uint32_t pic_origin_x,
                                   std::uint32_t pic_origin_y, std::uint32_t bw, std::uint32_t bh);

// neighbor_arrays.h:271-299 (the generic copy, unit_size read from the NA).
void copyNeighArr(const NeighborArrayUnit* na_src, NeighborArrayUnit* na_dst, std::uint32_t org_x,
                  std::uint32_t org_y, std::uint32_t bw, std::uint32_t bh, std::uint8_t mask);
// neighbor_arrays.h:306-323 (the Shape-B specialized copy, unit_size=1,
// granularity 4).
void copyNeighArrPu(const NeighborArrayUnit* na_src, NeighborArrayUnit* na_dst, std::uint32_t org_x,
                    std::uint32_t org_y, std::uint32_t bw, std::uint32_t bh, std::uint8_t mask);

// neighbor_arrays.h:383-422 (the generic mode write).
void neighborArrayUnitModeWrite(NeighborArrayUnit* na_unit_ptr, const void* value, std::uint32_t org_x,
                                std::uint32_t org_y, std::uint32_t bw, std::uint32_t bh, std::uint8_t mask);
// neighbor_arrays.h:431-452 (the Shape-B mode write, unit_size=1).
void neighborArrayUnitModeWritePu(NeighborArrayUnit* na_unit_ptr, const void* value, std::uint32_t org_x,
                                  std::uint32_t org_y, std::uint32_t bw, std::uint32_t bh, std::uint8_t mask);

// ---------------------------------------------------------------------------
// common_utils helpers (the A13 file's inline surface the spans consume)
// ---------------------------------------------------------------------------

// BlockSize - the l7 entropy mirror (definitions.h:882-909 verbatim order)
// is reused; Part (definitions.h:929-940, verbatim order) mirrors here -
// H4/V4 sit at indexes 3/4, PART_S is the LAST shape.
enum Part {
    PART_N = 0,
    PART_H,
    PART_V,
    PART_H4,
    PART_V4,
    PART_HA,
    PART_HB,
    PART_VA,
    PART_VB,
    PART_S,
    PART_TOTAL  // from_part_to_shape[PART_S + 1] = 11 entries
};

// common_utils.c:265-283 (verbatim values).
extern const std::uint8_t numNsPerShape[9];
extern const std::uint32_t nsBlkOffsetMd[9];
extern const std::uint32_t nsBlkOffset128Md[9];
extern const entropy::PartitionType fromShapeToPart[10];
extern const Part fromPartToShape[11];

// common_utils.c:313-345 (verbatim; [partition][sqr index]).
extern const entropy::BlockSize subsizeLookup[10][6];

// common_utils.h:194-204 get_sqr_bsize_idx.
std::int32_t getSqrBsizeIdx(entropy::BlockSize bsize);
// common_utils.h:212-223 get_partition_subsize.
entropy::BlockSize getPartitionSubsize(entropy::BlockSize bsize, entropy::PartitionType partition);

// common_utils.h:239-313 partition_mi_offset: mi_row/mi_col in/out.
entropy::BlockSize partitionMiOffset(entropy::BlockSize bsize, Part shape, unsigned nsi, int* mi_row, int* mi_col);

// common_utils.h:142-149 av1_get_max_uv_txsize (via the ss_size_lookup in
// l14... no: l6 links l14? it does NOT; the ssSizeLookup table
// (common_utils.c:239-263) lands here - U1's copy is in l14_utility which
// l6 does not link). blocksize_to_txsize (common_utils.c:214-237).
extern const entropy::BlockSize ssSizeLookup[22][2][2];
extern const entropy::TxSize blocksizeToTxsize[22];
entropy::BlockSize getPlaneBlockSize(entropy::BlockSize bsize, int subsampling_x, int subsampling_y);
entropy::TxSize av1GetMaxUvTxsize(entropy::BlockSize bsize, int subsampling_x, int subsampling_y);

// common_utils.h:100-113 av1_get_adjusted_tx_size.
entropy::TxSize av1GetAdjustedTxSize(entropy::TxSize tx_size);

// common_utils.c:43-61 eb_max_txsize_lookup + coefficients.h:44-46
// tx_size_2d (the hadamard_path dependencies).
extern const entropy::TxSize ebMaxTxsizeLookup[22];
extern const std::int32_t txSize2d[7];

// ---------------------------------------------------------------------------
// tx_org (transforms.h:41-42 + transforms.c:52-90): the per-(bsize,
// is_inter, tx_depth) transform-block origins, BUILT at init in the
// reference (the table replaces a ~17KB const table).
// ---------------------------------------------------------------------------

struct Position {
    std::uint32_t x;
    std::uint32_t y;
};
extern Position txOrg[22][2][3][16];
void buildTxOrg();

// ---------------------------------------------------------------------------
// The md neighbour-update family (product_coding_loop.c:121-:689 +
// svt_aom_copy_neighbour_arrays :421-:657; the carriers hold exactly the
// consumed ModeDecisionContext / PictureControlSet fields).
// ---------------------------------------------------------------------------

// BlkStruct consumed window (coding_unit.h:147-181): the neighbour recon
// pointers + quant_dc + tx_depth carrier.
struct MdBlk {
    const std::uint8_t* neigh_left_recon[3];
    const std::uint8_t* neigh_top_recon[3];
    const std::uint16_t* neigh_left_recon_16bit[3];
    const std::uint16_t* neigh_top_recon_16bit[3];
    std::uint8_t quant_dc_y[kMaxTxbCount];
    std::uint8_t quant_dc_u[kMaxTxbCountUv];
    std::uint8_t quant_dc_v[kMaxTxbCountUv];
    std::uint8_t tx_depth;
};

// ModeDecisionContext consumed window (the carriers only): is_inter comes
// from the block_mi mode (is_inter_block, block_structures.h:118-122);
// the rest are the ctrl/state fields :180-:418 read.
struct MdCtx {
    std::uint8_t is_inter;          // block_structures.h is_inter_block
    std::uint8_t skip_intra;        // ctx->skip_intra
    std::uint8_t pd0_use_src_samples;  // ctx->pd0_use_src_samples
    std::uint8_t pd_pass;           // PD_PASS_0=0 / PD_PASS_1=1
    std::uint8_t shut_fast_rate;    // ctx->shut_fast_rate
    std::uint8_t update_skip_ctx_dc_sign_ctx;  // rate_est_ctrls
    std::uint8_t update_skip_coeff_ctx;        // rate_est_ctrls
    std::uint8_t use_neighbouring_mode_ctrls_enabled;  // cand_reduction_ctrls
    std::uint8_t txs_ctrls_enabled; // ctx->txs_ctrls.enabled
    std::uint8_t uv_mode;           // ctx->uv_ctrls.uv_mode (ChromaSearchMode)
    std::uint8_t inter_intra_comp_ctrls_enabled;
    std::uint8_t effective_bit_depth_gt_8;  // SVT_EFFECTIVE_BIT_DEPTH(encoder_bit_depth) > EB_EIGHT_BIT
    std::uint8_t bypass_encdec;     // ctx->bypass_encdec
    std::uint8_t hbd_md;            // SVT_EFFECTIVE_HBD_MD(ctx->hbd_md)
    std::uint16_t tile_index;       // ctx->tile_index (the carrier's tile slice)
    std::uint8_t frm_hdr_tx_mode_select;  // TX_MODE_SELECT bit
    MdBlk blk;
    // The ctx-owned neighbour arrays (the write targets).
    NeighborArrayUnit* leaf_partition_na;
    NeighborArrayUnit* luma_dc_sign_level_coeff_na;
    NeighborArrayUnit* cb_dc_sign_level_coeff_na;
    NeighborArrayUnit* cr_dc_sign_level_coeff_na;
    NeighborArrayUnit* txfm_context_array;
    NeighborArrayUnit* recon_neigh_y;
    NeighborArrayUnit* recon_neigh_cb;
    NeighborArrayUnit* recon_neigh_cr;
    NeighborArrayUnit* luma_recon_na_16bit;
    NeighborArrayUnit* cb_recon_na_16bit;
    NeighborArrayUnit* cr_recon_na_16bit;
};

// The pcs-owned [slot][tile_idx] arrays: the port's carrier holds the two
// slots (NA_TOT_CNT = 2, pcs.h:40-45) for ONE tile - the tile_idx
// dimension is flattened away (the test supplies the per-tile view; the
// update walk reads slot MD_NEIGHBOR_ARRAY_INDEX = 1, the copy walk reads
// src/dst slots).
constexpr std::uint32_t kMdNeighborArrayIndex = 1;  // pcs.h:44
constexpr std::uint32_t kNaTotCnt = 2;              // pcs.h:40-45
struct MdPcs {
    std::uint8_t slice_type;  // 0 = I_SLICE, 2 = B_SLICE (SliceType)
    std::uint8_t hbd_md;      // pcs->hbd_md (the SVT_EFFECTIVE fold input)
    std::uint8_t scs_encoder_bit_depth;
    std::uint8_t pic_bypass_encdec;  // pcs->pic_bypass_encdec
    NeighborArrayUnit* mdleaf_partition_na[kNaTotCnt];
    NeighborArrayUnit* md_tx_depth_1_luma_dc_sign_level_coeff_na[kNaTotCnt];
    NeighborArrayUnit* md_tx_depth_2_luma_dc_sign_level_coeff_na[kNaTotCnt];
    NeighborArrayUnit* md_cb_dc_sign_level_coeff_na[kNaTotCnt];
    NeighborArrayUnit* md_cr_dc_sign_level_coeff_na[kNaTotCnt];
    NeighborArrayUnit* md_luma_recon_na[kNaTotCnt];
    NeighborArrayUnit* md_tx_depth_1_luma_recon_na[kNaTotCnt];
    NeighborArrayUnit* md_tx_depth_2_luma_recon_na[kNaTotCnt];
    NeighborArrayUnit* md_cb_recon_na[kNaTotCnt];
    NeighborArrayUnit* md_cr_recon_na[kNaTotCnt];
    NeighborArrayUnit* md_luma_recon_na_16bit[kNaTotCnt];
    NeighborArrayUnit* md_tx_depth_1_luma_recon_na_16bit[kNaTotCnt];
    NeighborArrayUnit* md_tx_depth_2_luma_recon_na_16bit[kNaTotCnt];
    NeighborArrayUnit* md_cb_recon_na_16bit[kNaTotCnt];
    NeighborArrayUnit* md_cr_recon_na_16bit[kNaTotCnt];
    NeighborArrayUnit* md_y_dcs_na[kNaTotCnt];
    NeighborArrayUnit* md_txfm_context_array[kNaTotCnt];
};

// mode_decision_update_neighbor_arrays_pd0 (product_coding_loop.c:121-:144).
// shape/nsi walk the pc_tree partition; blk_ptr is pc_tree->block_data[shape][0]
// (the ctx->recon_neigh_y write target travels on the MdCtx carrier).
void modeDecisionUpdateNeighborArraysPd0(const MdCtx& ctx, entropy::PartitionType partition,
                                         entropy::BlockSize bsize, int mi_row, int mi_col, const MdBlk& blk);

// mode_decision_update_neighbor_arrays (product_coding_loop.c:149-:418).
// The pcs-owned [MD_NEIGHBOR_ARRAY_INDEX][tile_idx] arrays travel on the
// MdPcs carrier; the ctx-owned write targets on MdCtx.
void modeDecisionUpdateNeighborArrays(const MdPcs& pcs, const MdCtx& ctx, entropy::BlockSize bsize, int mi_row,
                                      int mi_col);

// md_update_all_neighbour_arrays (product_coding_loop.c:661-:674): the
// tested blk's carrier + the pc_tree fields; the shape folds from the
// partition via from_part_to_shape (:678). The svt_aom_update_mi_map
// call (:672) is NAMED-PARKED (the AMVP1 mi-map quartet) - the port keeps
// the gate and the neighbour-array walk, and returns whether the gate
// would have fired (the parked call's condition).
bool mdUpdateAllNeighbourArrays(const MdPcs& pcs, const MdCtx& ctx_base, const MdBlk& blk,
                                entropy::PartitionType partition, entropy::BlockSize bsize, int mi_row,
                                int mi_col, unsigned nsi);

// md_update_all_neighbour_arrays_multiple (:676-:689): walk the shape's
// blocks; the tested gate lives on the caller's tested_blk view.
void mdUpdateAllNeighbourArraysMultiple(const MdPcs& pcs, const MdCtx& ctx_base, const MdBlk* blks,
                                        const std::uint8_t* tested_blk, entropy::PartitionType partition,
                                        entropy::BlockSize bsize, int mi_row, int mi_col,
                                        std::uint32_t shape_block_cnt, bool* mi_map_fired);

// svt_aom_copy_neighbour_arrays (product_coding_loop.c:421-:657).
void copyNeighbourArrays(const MdPcs& pcs, const MdCtx& ctx, std::uint32_t src_idx, std::uint32_t dst_idx,
                         entropy::BlockSize bsize, int mi_row, int mi_col);

// svt_aom_reset_mode_decision_neighbor_arrays (md_process.c:692-:720):
// the depth walk over the two slots (NA_TOT_CNT = 2, pcs.h:40-45), the
// tile dimension flattened on the carrier. The 8-bit / 16-bit gates
// (SVT_EFFECTIVE_HBD_MD + pic_bypass_encdec) thread on the carrier.
void resetModeDecisionNeighborArrays(const MdPcs& pcs);

// encode_pass_update_recon_sample_neighbour_arrays (coding_loop.c:96-:184).
void encodePassUpdateReconSampleNeighbourArrays(NeighborArrayUnit* lumaReconSampleNeighborArray,
                                                NeighborArrayUnit* cbReconSampleNeighborArray,
                                                NeighborArrayUnit* crReconSampleNeighborArray,
                                                const std::uint8_t* recon_y, std::uint32_t recon_y_stride,
                                                const std::uint8_t* recon_u, std::uint32_t recon_u_stride,
                                                const std::uint8_t* recon_v, std::uint32_t recon_v_stride,
                                                std::uint32_t org_x, std::uint32_t org_y, std::uint32_t width,
                                                std::uint32_t height, std::uint32_t bwidth_uv,
                                                std::uint32_t bheight_uv, std::uint32_t component_mask,
                                                bool is_16bit);
constexpr std::uint32_t kPictureBufferDescLumaMask = 1 << 0;   // picture_buffer_desc.h:43-46
constexpr std::uint32_t kPictureBufferDescChromaMask = 1 << 1;

// ---------------------------------------------------------------------------
// The sort / stage-count family (product_coding_loop.c:1299-:1446).
// ---------------------------------------------------------------------------

// CandClass (definitions.h:766-773) + MdStagingMode (:777-782) + the
// MD_STAGE_NICS table (:788-:796, definitions.h) + NICS_SCALING_LEVELS.
constexpr std::uint32_t kCandClassTotal = 5;
constexpr std::uint32_t kNicsPicType = 3;
constexpr std::uint32_t kMdStageNicsScalDenum = 16;  // definitions.h:796
extern const std::uint32_t mdStageNics[kNicsPicType][kCandClassTotal];

// NicScalingCtrls (definitions.h, the consumed fields).
struct NicScalingCtrls {
    std::uint32_t stage1_scaling_num;
    std::uint32_t stage2_scaling_num;
    std::uint32_t stage3_scaling_num;
};

// svt_aom_set_nics (product_coding_loop.c:1299-:1333). The qp-weight gate
// is scs->qp_based_th_scaling_ctrls.nic_max_qp_based_th_scaling (the
// l16_rdo::mode_config getQpBasedThScalingFactors enable flag).
void setNics(const NicScalingCtrls& scaling_ctrls, std::uint32_t mds1_count[kCandClassTotal],
             std::uint32_t mds2_count[kCandClassTotal], std::uint32_t mds3_count[kCandClassTotal],
             std::uint8_t pic_type, std::uint32_t qp, std::uint8_t nic_max_qp_based_th_scaling);

// MdStagingMode (definitions.h:777-782).
enum MdStagingMode { MD_STAGING_MODE_0 = 0, MD_STAGING_MODE_1 = 1, MD_STAGING_MODE_2 = 2 };

// set_md_stage_counts (product_coding_loop.c:1335-:1354). picType =
// I_SLICE ? 0 : !is_highest_layer ? 1 : 2.
struct MdStageCountsCtx {
    std::uint32_t md_stage_1_count[kCandClassTotal];
    std::uint32_t md_stage_2_count[kCandClassTotal];
    std::uint32_t md_stage_3_count[kCandClassTotal];
    std::uint8_t md_staging_mode;  // ctx->nic_ctrls.md_staging_mode
    std::uint8_t bypass_md_stage_1;
    std::uint8_t bypass_md_stage_2;
};
void setMdStageCounts(const NicScalingCtrls& scaling_ctrls, MdStageCountsCtx* ctx, std::uint8_t pic_type,
                      std::uint32_t qp, std::uint8_t nic_max_qp_based_th_scaling);

// sort_fast_cost_based_candidates (:1356-:1377): the buffer costs arrive
// as the flat array; candBuffIndices is the output (sized input_buffer_count).
void sortFastCostBasedCandidates(const std::uint64_t* buffer_fast_costs, std::uint32_t input_buffer_start_idx,
                                 std::uint32_t input_buffer_count, std::uint32_t* cand_buff_indices);

// sort_full_cost_based_candidates (:1379-:1393).
void sortFullCostBasedCandidates(const std::uint64_t* buffer_full_costs, std::uint32_t num_of_cand_to_sort,
                                 std::uint32_t* cand_buff_indices);

// construct_best_sorted_arrays_md_stage_3 (:1395-:1407): concat the
// per-class sorted index arrays in class order.
void constructBestSortedArraysMdStage3(const std::uint32_t* const* cand_buff_indices_per_class,
                                       const std::uint32_t* md_stage_3_count,
                                       std::uint32_t* best_candidate_index_array,
                                       std::uint32_t md_stage_3_total_count);

// perform_ind_uv_search_last_mds (:1413-:1446): the per-candidate window
// (mode / use_intrabc / uv_mode / full_cost) + the two uv_ctrls.
struct IndUvCandidate {
    std::int32_t mode;          // PredictionMode
    std::uint8_t use_intrabc;
    std::uint32_t uv_mode;      // UvPredictionMode
    std::uint64_t full_cost;
};
struct IndUvCtrls {
    std::uint8_t skip_ind_uv_if_only_dc;  // uv_ctrls.skip_ind_uv_if_only_dc
    std::uint32_t inter_vs_intra_cost_th;  // uv_ctrls.inter_vs_intra_cost_th
};
bool performIndUvSearchLastMds(const IndUvCandidate* buffer_candidates, const std::uint32_t* best_cand_idx_array,
                               std::uint32_t mds3_cand_count, const IndUvCtrls& ctrls);

// ---------------------------------------------------------------------------
// The MV / ME helpers (product_coding_loop.c:65-:119, :1716-:1853,
// :1992-:2017, :2195-:2268 + mcomp.c/mv.h/rd_cost.c chains).
// ---------------------------------------------------------------------------

// determine_best_references (product_coding_loop.c:65-:119). The MeSbResults
// carrier (me_sb_results.h:38-46) + the pcs consumed fields.
struct BestRefPcs {
    std::uint8_t slice_type;          // I_SLICE=0, B_SLICE=2
    std::uint8_t ref_list0_count_try;
    std::uint8_t ref_list1_count_try;
};
// determine_best_references (product_coding_loop.c:65-:119). sb_results is
// pcs->ppcs->pa_me_data->me_results[ctx->me_sb_addr] (the caller's slice);
// the offsets are the ctx fields the reference indexes with.
void determineBestReferences(const BestRefPcs& pcs, const l16_rdo::MeSbResults* sb_results,
                             std::uint32_t me_block_offset, std::uint32_t me_cand_offset,
                             std::int32_t* ref_arr, std::uint8_t* tot_ref);

// clip_mv_on_pic_boundary (product_coding_loop.c:2195-:2212), the
// EbPictureBufferDesc window flattened to (max_width, max_height, border).
void clipMvOnPicBoundary(std::int32_t blk_org_x, std::int32_t blk_org_y, std::int32_t bwidth, std::int32_t bheight,
                         std::int32_t ref_max_width, std::int32_t ref_max_height, std::int32_t ref_border,
                         std::int16_t* mvx, std::int16_t* mvy);

// motion_estimation.h:66-71, the MV-size thresholds.
constexpr std::int32_t kMediumTemporalMvTh = 2048;
constexpr std::int32_t kLowTemporalMvTh = 1024;
constexpr std::int32_t kHighSpatialMvTh = 2048;
constexpr std::int32_t kMediumSpatialMvTh = 512;
constexpr std::int32_t kLowSpatialMvTh = 256;

// check_spatial_mv_size (:2219-:2241): the mvp arrays flattened.
std::uint8_t checkSpatialMvSize(const Mv2* mvp_array, std::uint8_t mvp_count, const std::int16_t* me_mv_x,
                                const std::int16_t* me_mv_y);

// check_temporal_mv_size (:2248-:2265): the TPL_MV_REF carrier flattened
// to the mfmv0 pair.
std::uint8_t checkTemporalMvSize(std::uint32_t mfmv0_as_int, std::int16_t mfmv0_x, std::int16_t mfmv0_y);

// derive_me_offsets (:1992-:2017): the ctx out-fields via the carrier.
struct DeriveMeOffsetsCtx {
    std::uint32_t geom_offset_x;
    std::uint32_t geom_offset_y;
    std::uint32_t me_sb_addr;
    std::uint32_t me_block_offset;
    std::uint32_t me_cand_offset;
};
struct DeriveMeOffsetsInput {
    std::uint8_t sb_size_is_128;         // scs->seq_header.sb_size == BLOCK_128X128
    std::uint32_t b64_size;              // scs->b64_size
    std::uint32_t aligned_width;         // ppcs->aligned_width
    std::uint8_t enable_me_8x8;          // ppcs->enable_me_8x8
    std::uint8_t enable_me_16x16;        // ppcs->enable_me_16x16
    std::uint32_t sb_ptr_index;          // ctx->sb_ptr->index
    std::uint32_t blk_org_x;             // ctx->blk_org_x
    std::uint32_t blk_org_y;             // ctx->blk_org_y
    entropy::BlockSize bsize;            // ctx->blk_geom->bsize
    std::uint32_t max_cand;              // ppcs->pa_me_data->max_cand
};
void deriveMeOffsets(const DeriveMeOffsetsInput& in, DeriveMeOffsetsCtx* ctx);

// MvJointType (rd_cost.h:20-25, verbatim) + svt_av1_get_mv_joint
// (rd_cost.c:47-53). DEVIATION (named): the reference's fifth enumerator
// MV_JOINTS is shadowed by the MV_JOINTS macro (= 4, rate_est.h:63 /
// cabac_context_model.h:153) in every TU that sees both, so the port
// carries the macro count and the four real ordinals.
enum MvJointType { MV_JOINT_ZERO, MV_JOINT_HNZVZ, MV_JOINT_HZVNZ, MV_JOINT_HNZVNZ };
MvJointType av1GetMvJoint(const Mv2& mv);

// rd_cost.c:55-60 mv_cost + mcomp.h:138-142 svt_mv_cost (the same body).
std::int32_t mvCost(const Mv2& mv, const std::int32_t* joint_cost, const std::int32_t* const* comp_cost);

// rd_cost.h:342-343 + mcomp.h:40-72: the svt_mv_err_cost chain.
constexpr std::int32_t kRddivBits = 7;
constexpr std::int32_t kRdEpbShift = 6;
constexpr std::int32_t kPixelTransformErrorScale = 4;  // mcomp.h:40
constexpr std::int32_t kSseLambdaLowres = 2;           // mcomp.c:32 SSE_LAMBDA_LOWRES
constexpr std::int32_t kSseLambdaMidres = 0;           // mcomp.c:34 SSE_LAMBDA_MIDRES
constexpr std::int32_t kSseLambdaHdres = 1;            // mcomp.c:36 SSE_LAMBDA_HDRES
enum MvCostType {
    MV_COST_ENTROPY = 0,
    MV_COST_L1_LOWRES = 1,
    MV_COST_L1_MIDRES = 2,
    MV_COST_L1_HDRES = 3,
    MV_COST_OPT = 4,
    MV_COST_NONE = 5
};
struct SvtMvCostParam {
    const Mv2* ref_mv;
    Mv2 full_ref_mv;
    std::uint32_t early_exit_th;
    MvCostType mv_cost_type;
    std::int32_t error_per_bit;
    std::int32_t sad_per_bit;
    const std::int32_t* mvjcost;
    const std::int32_t* mvcost[2];
};
std::int32_t mvErrCost(const Mv2& mv, const Mv2& ref_mv, const std::int32_t* mvjcost,
                       const std::int32_t* const* mvcost, std::int32_t error_per_bit, MvCostType mv_cost_type);
std::int32_t fpMvErrCost(const Mv2& mv, const SvtMvCostParam& params);

// mv.h:56-63: GET_MV_RAWPEL + get_fullmv_from_mv.
Mv2 getFullmvFromMv(const Mv2& subpel_mv);

// svt_init_mv_cost_params (product_coding_loop.c:1842-:1853): the ctx
// consumed fields (blk_geom->sq_size, md_subpel_me_ctrls.skip_diag_
// refinement, md_rate_est_ctx->nmv_vec_cost / nmvcoststack) as scalars.
struct InitMvCostParamsCtx {
    std::uint32_t sq_size;                 // ctx->blk_geom->sq_size
    std::uint8_t skip_diag_refinement;     // md_subpel_me_ctrls
    const std::int32_t* nmv_vec_cost;      // md_rate_est_ctx
    const std::int32_t* nmvcoststack0;
    const std::int32_t* nmvcoststack1;
};
void svtInitMvCostParams(const InitMvCostParamsCtx& ctx, SvtMvCostParam* mv_cost_params, const Mv2* ref_mv,
                         std::uint8_t base_q_idx, std::uint32_t rdmult, std::uint8_t hbd_md);

// svt_aom_get_sad_per_bit (mode_decision.c:2086-:2100) + its table
// (the av1_lambda_mode_decision..sad_per_bit pair, lambda_rate_tables.h).
std::int32_t getSadPerBit(int qidx, int is_hbd);

// svt_pme_sad_loop_kernel_c (product_coding_loop.c:1716-:1769).
void svtPmeSadLoopKernel(const SvtMvCostParam* mv_cost_params, const std::uint8_t* src, std::uint32_t src_stride,
                         const std::uint8_t* ref, std::uint32_t ref_stride, std::uint32_t block_height,
                         std::uint32_t block_width, std::uint32_t* best_cost, std::int16_t* best_mvx,
                         std::int16_t* best_mvy, std::int16_t search_position_start_x,
                         std::int16_t search_position_start_y, std::int16_t search_area_width,
                         std::int16_t search_area_height, std::int16_t search_step, std::int16_t mvx,
                         std::int16_t mvy);

// ---------------------------------------------------------------------------
// The lambda chain (rc_process.c:346-:464, the A9 file) +
// av1_lambda_assign_md (md_process.c:730-:765).
// ---------------------------------------------------------------------------

// rc_process.c:346-:362.
double defInterRdMultiplier(int qindex);
double defArfRdMultiplier(int qindex);
double defKfRdMultiplier(int qindex);

// SvtAv1FrameUpdateType (the svT enum order): KF_UPDATE=0, LF_UPDATE=1,
// GF_UPDATE=2, ARF_UPDATE=3, INTNL_ARF_UPDATE=4, LF_UPDATE_2.. the
// reference enum is EbSvtAv1.h's; the rd mult only branches KF / GF|ARF /
// else, so the three-way fold carries the whole surface.
enum SvtFrameUpdateType { SVT_AV1_KF_UPDATE = 0, SVT_AV1_GF_UPDATE, SVT_AV1_ARF_UPDATE,
                          SVT_AV1_INTNL_ARF_UPDATE, SVT_AV1_LF_UPDATE };

// rc_process.c:364-:385.
std::int32_t computeRdMultBasedOnQindex(int bit_depth, int update_type, int qindex);

// rc_process.c:394-:441: rd_frame_type_factor (:394-:395) + update_lambda.
extern const int rdFrameTypeFactor[2][7];
struct LambdaPcs {
    int update_type;                     // ppcs->update_type
    std::uint8_t frame_type;             // 0 = KEY_FRAME
    std::uint8_t temporal_layer_index;
    std::uint8_t hierarchical_levels;
    std::uint8_t rtc;                    // scs->static_config.rtc
    std::uint8_t stats_based_sb_lambda_modulation;
    std::uint8_t delta_q_present;        // frm_hdr.delta_q_params.delta_q_present
    std::uint8_t r0_delta_qp_md;         // ppcs->r0_delta_qp_md
    std::uint8_t base_q_idx;             // frm_hdr base_q_idx
};
std::uint32_t updateLambda(const LambdaPcs& pcs, std::uint8_t q_index, std::uint8_t me_q_index, int bit_depth,
                           std::int64_t rdmult);

// rc_process.c:446-:464 (+ the 8-bit sad table lambda_rate_tables.h:23-:39;
// the 10/12-bit tables are dead under the pinned build's
// CONFIG_ENABLE_HIGH_BIT_DEPTH=0, lambda_rate_tables.h:41).
extern const std::uint32_t lambdaModeDecision8BitSad[256];
std::uint32_t computeRdMult(const LambdaPcs& pcs, std::uint8_t q_index, std::uint8_t me_q_index, int bit_depth);
std::uint32_t computeFastLambda(const LambdaPcs& pcs, std::uint8_t q_index, std::uint8_t me_q_index, int bit_depth);

// md_process.c:722-:765: LAMBDA_MOD_INTRA_TH 50 / SCALING_FACTOR 138 +
// av1_lambda_assign_md. EbBitDepth: EB_EIGHT_BIT = 0, EB_TEN_BIT = 1.
// The carrier threads BOTH temporal_layer_index readings the span makes
// (pcs->temporal_layer_index at :737 and ppcs->temporal_layer_index inside
// update_lambda :403) plus the ppcs frame-type / hierarchical fields.
constexpr std::int32_t kLambdaModIntraTh = 50;
constexpr std::int32_t kLambdaModIntraScalingFactor = 138;
struct LambdaAssignCtx {
    std::uint8_t qp_index;    // ctx->qp_index
    std::uint8_t me_q_index;  // ctx->me_q_index
    std::uint32_t full_lambda_md[2];
    std::uint32_t fast_lambda_md[2];
    std::uint32_t full_sb_lambda_md[2];
};
struct LambdaAssignPcs {
    std::uint8_t rtc;                              // scs->static_config.rtc
    std::uint8_t stats_based_sb_lambda_modulation; // scs->stats_based_sb_lambda_modulation
    std::uint8_t pcs_temporal_layer_index;         // pcs->temporal_layer_index (:737)
    std::uint8_t ref_intra_percentage;             // pcs->ref_intra_percentage
    std::uint32_t lambda_weight;                   // pcs->lambda_weight
    std::uint32_t lambda_scale_factor;             // static_config.lambda_scale_factors[update_type]
    std::uint8_t update_type;                      // ppcs->update_type
    std::uint8_t ppcs_frame_type;                  // ppcs->frm_hdr.frame_type (0 = KEY_FRAME)
    std::uint8_t ppcs_temporal_layer_index;        // ppcs->temporal_layer_index (:403)
    std::uint8_t ppcs_hierarchical_levels;         // ppcs->hierarchical_levels
    std::uint8_t delta_q_present;                  // frm_hdr.delta_q_params.delta_q_present
    std::uint8_t r0_delta_qp_md;                   // ppcs->r0_delta_qp_md
    std::uint8_t base_q_idx;                       // frm_hdr base_q_idx
};
void av1LambdaAssignMd(const LambdaAssignPcs& pcs, LambdaAssignCtx* ctx);

// ---------------------------------------------------------------------------
// setup_mds / setup_pc_tree (md_process.c:158-:211): the MdScan / PC_TREE
// trees over index-linked flat storage (the reference links siblings by
// pointer arithmetic; the port stores the SPLIT offsets and the base
// indices).
// ---------------------------------------------------------------------------

// MdScan (md_process.h, the consumed fields): the tree is built by
// index: split[i] sits at base + i*blocks_to_skip + 1.
struct MdScan {
    std::uint32_t mds_idx;
    entropy::BlockSize bsize;
    std::uint32_t index;
    std::uint32_t split[4];   // 0 = absent
    std::uint8_t has_split;
};

// setup_mds (md_process.c:158-:185): fills the flat MdScan array starting
// at mds[0] (*mds_idx walks the same geom d1/ns offsets). geomTable is the
// scs->blk_geom_mds surface - get_blk_geom_mds(scs->blk_geom_mds,
// *mds_idx) folds to geomTable[*mds_idx]; the l14 builder fills it.
void setupMds(entropy::BlockSize bsize, const int min_sq_size, const l14_utility::BlockGeom* geom_table,
              MdScan* mds, std::uint32_t* mds_idx, int index);

// setup_pc_tree (md_process.c:187-:211): the flat PC_TREE carrier; the
// tested_blk pointer arithmetic becomes a base index into the caller's
// [sb_blocks][PART_S][4] array.
struct PcTree {
    entropy::BlockSize bsize;
    std::uint32_t index;
    std::uint32_t tested_blk_base;  // the row base into the tested array
    std::uint32_t split[4];
    std::uint8_t has_split;
};
void setupPcTree(PcTree* pc_tree, std::uint32_t tested_blk_base, int index, entropy::BlockSize bsize,
                 const int min_sq_size);

// ---------------------------------------------------------------------------
// coding_loop.c misc: the residual dispatcher (:69-:91), the frame-MV
// copy (:1038-:1070), the qcoeff copy (:1429-:1465), the coeff-cdf update
// (:1468-:1593), and hadamard_path (:1138-:1207).
// ---------------------------------------------------------------------------

// svt_aom_residual_kernel (coding_loop.c:69-:91): the hbd fold over the
// PO1 kernels. The uint8_t* params stay byte buffers; the hbd path
// advances the offsets in uint16 ELEMENTS per the reference cast.
void residualKernel(const std::uint8_t* input, std::uint32_t input_offset, std::uint32_t input_stride,
                    const std::uint8_t* pred, std::uint32_t pred_offset, std::uint32_t pred_stride,
                    std::int16_t* residual, std::uint32_t residual_offset, std::uint32_t residual_stride,
                    bool hbd, std::uint32_t area_width, std::uint32_t area_height);

// coding_loop.c:1036 REFMVS_LIMIT + av1_copy_frame_mvs (:1038-:1070).
// MV_REF (block_structures.h) is the two-word {ref_frame; mv.as_int};
// the out arrays carry ref_frame (the MvReferenceFrame ordinal,
// NONE_FRAME = -1) + the packed as_int halves.
constexpr std::int32_t kRefmvsLimit = (1 << 12) - 1;
constexpr std::int32_t kNoneFrame = -1;  // definitions.h:1357
struct CopyFrameMvsMi {
    std::int32_t ref_frame[2];
    std::int32_t mv[2][2];  // [idx][x,y] the block_mi.mv values
};
void av1CopyFrameMvs(const CopyFrameMvsMi& mi, std::int32_t mi_row, std::int32_t mi_col, int x_mis, int y_mis,
                     std::int32_t* frame_mvs_ref_frame, std::uint32_t* frame_mvs_mv, std::int32_t frame_mvs_stride,
                     const std::int8_t* ref_frame_side);

// copy_qcoeffs (coding_loop.c:1429-:1465). The ep/md coeff pointers are
// pre-offset by the caller (ep = coeff_buffer_sb + ctx->coded_area_sb_
// update, md = blk_ptr->coeff_tmp + blk_coded_area - the reference's own
// offset arithmetic).
struct CopyQcoeffsCtx {
    std::uint8_t txb_itr;
    std::uint8_t has_uv;
};
void copyQcoeffs(const CopyQcoeffsCtx& ctx, entropy::BlockSize bsize, std::uint8_t tx_depth,
                 const std::int32_t* ep_coeff_y, const std::int32_t* md_coeff_y, const std::int32_t* ep_coeff_u,
                 const std::int32_t* md_coeff_u, const std::int32_t* ep_coeff_v, const std::int32_t* md_coeff_v,
                 std::uint16_t y_has_coeff, std::uint8_t u_has_coeff, std::uint8_t v_has_coeff);

// update_coeff_cdf (coding_loop.c:1468-:1593) - the l16 carriers
// (FcTokenCdfs/FcSyntaxCdfs/MdRateEstimationContext + CostCoeffsTxbArgs).
struct UpdateCoeffCdfCarrier {
    std::uint8_t tx_depth;
    std::uint8_t txb_itr;
    std::uint8_t has_uv;
    std::uint8_t is_inter;
    entropy::BlockSize bsize;
    entropy::TxSize tx_size;
    entropy::TxSize tx_size_uv;
    entropy::TxType tx_type;
    entropy::TxType tx_type_uv;
    std::uint16_t y_has_coeff;
    std::uint8_t u_has_coeff;
    std::uint8_t v_has_coeff;
    std::uint16_t eob_y;
    std::uint16_t eob_u;
    std::uint16_t eob_v;
    std::uint8_t quant_dc_y;
    std::uint8_t quant_dc_u;
    std::uint8_t quant_dc_v;
    std::uint32_t blk_org_x;
    std::uint32_t blk_org_y;
    std::uint32_t coded_area_sb_update;
    std::uint32_t coded_area_sb_uv_update;
    const std::int32_t* coeff_y;    // quantized_coeff + coded_area
    const std::int32_t* coeff_u;
    const std::int32_t* coeff_v;
    std::uint8_t reduced_tx_set;
    std::int32_t lumaMode;
    std::int32_t filterIntraMode;
    std::uint8_t* levels_buf;
    std::int8_t* coeff_contexts;
    std::int32_t fast_coeff_est_level;
    std::int32_t subres_step;
    l16_rdo::FcTokenCdfs* ec_ctx;
    l16_rdo::FcSyntaxCdfs* ec_syntax_ctx;
    l16_rdo::MdRateEstimationContext* md_rate_est_ctx;
    // the ep NA update arrays (the getTxbCtx read + the dc-sign write).
    const std::uint8_t* ep_luma_na_above;
    const std::uint8_t* ep_luma_na_left;
    const std::uint8_t* ep_cb_na_above;
    const std::uint8_t* ep_cb_na_left;
    const std::uint8_t* ep_cr_na_above;
    const std::uint8_t* ep_cr_na_left;
    int luma_txb_w_unit;
    int luma_txb_h_unit;
    NeighborArrayUnit* ep_luma_dc_sign_na_write;
    NeighborArrayUnit* ep_cb_dc_sign_na_write;
    NeighborArrayUnit* ep_cr_dc_sign_na_write;
};
void updateCoeffCdf(const UpdateCoeffCdfCarrier& c, std::uint64_t* y_txb_coeff_bits,
                    std::uint64_t* cb_txb_coeff_bits, std::uint64_t* cr_txb_coeff_bits);

// svt_aom_satd_c (common_dsp_rtcd.c:48-55; the file is excluded from the
// coverage denominator as rtcd glue - the kernel is portable C and the
// hadamard_path dependency).
std::int32_t satd(const std::int64_t* coeff, int length);

// hadamard_path (product_coding_loop.c:1138-:1207): the cand_bf buffers +
// ctx window flattened. The bsize -> tx_size fold (AOMMIN(TX_32X32,
// eb_max_txsize_lookup)) and the tile walk are 1:1; the residual kernel is
// PO1's, the hadamards are PO1's, satd is here.
void hadamardPath(const std::uint8_t* input, std::uint32_t input_stride, std::uint32_t input_origin_index,
                  const std::uint8_t* pred, std::uint32_t pred_stride, std::int16_t* residual,
                  std::uint32_t residual_stride, std::int32_t* coeff, entropy::BlockSize bsize, bool hbd,
                  std::uint32_t* satd_cost_out);

}  // namespace pipeline
