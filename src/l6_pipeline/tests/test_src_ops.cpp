// l6_pipeline/tests/test_src_ops.cpp - the SRC1 source-ops surface (A6).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/src_ops_process.c (2,014 nb).
//
// BRIEF CORRECTION (measured 2026-11-12, grep-verified): the brief's "plane
// copy/pad helpers, yuv interleave/deinterleave paths, 8/16-bit copy
// kernels" do NOT exist in src_ops_process.c - the only copy/pad content is
// one CALL of svt_aom_generate_padding (:1412-:1417, defined in
// pic_manager_process.c). The file is the TPL dispenser/synthesizer + the
// SSIM-rdmult driver. Ported (the struct-free / carrier-threadable surface):
//
//   get_neighbor_samples_dc            :364-:376   13 nb (the source-window load)
//   round_floor                        :1453-:1462  9 nb
//   get_overlap_area                   :1423-:1451 27 nb (+ mi_size_*_log2 carry)
//   delta_rate_cost                    :1464-:1490 19 nb
//   svt_aom_get_perpixel_variance      :2151-:2156  6 nb (+ AV1_VAR_OFFS fold)
//   rate_estimator                     :263-:276   10 nb
//   tpl_model_update_b                 :1496-:1549 47 nb (TplStats-grid carrier)
//   tpl_model_update                   :1555-:1582 25 nb (carrier)
//   tpl_mc_flow_synthesizer            :1589-:1602 13 nb (carrier)
//   generate_lambda_scaling_factor     :188-:234   44 nb (carrier)
//   svt_aom_generate_r0beta            :1604-:1697 89 nb (carrier)
//   aom_av1_set_mb_ssim_rdmult_scaling :2158-:2236 71 nb (carrier)
//   ported total = 380 nb (+ carries: svt_log2f, the two log2 tables,
//   get_fullmv_from_mv, coded_to_superres_mi, the TplStatsCell/TplSwFrame/
//   SbGeomOrg carries - uncharged per the AN17 struct-carry precedent).
//
// PARKED NAMED (the EbPictureBufferDesc / PictureParentControlSet /
// EncodeContext / EbFifo-mutex walls):
//   SourceBasedOperationsContext + TplDispenserContext ctors/dctors :60-:114 45 nb
//   tpl_regular_setup_me_refs  :118-:151  29 nb (PCS)
//   tpl_prep_info              :158-:185  24 nb (PCS)
//   svt_tpl_init_mv_cost_params:389-:406  17 nb (mv-cost walls)
//   init_xd_tpl                :409-:421  13 nb (MacroBlockD)
//   tpl_subpel_search          :424-:520  85 nb (ME machinery)
//   tpl_mc_flow_dispenser_sb_generic :524-:1200 606 nb (the TPL dispenser)
//   get_quantize_error         :237-:261  22 nb (callee walls: the raw
//     svt_av1_quantize_fp signature + svt_av1_block_error
//     (common_dsp_rtcd.c:57) - neither ported; l3's quantize is the
//     QuantTables API, square-only)
//   assign_tpl_segments        :1210-:1321 87 nb (mutex/fifo state machine)
//   tpl_dispenser_st           :1327-:1341 15 nb
//   tpl_mc_flow_dispenser      :1349-:1420 63 nb
//   init_tpl_buffers           :1703-:1709  7 nb (enc_ctx)
//   init_tpl_segments          :1716-:1796 68 nb (tile-group walls)
//   tpl_mc_flow                :1810-:1992 171 nb (the TPL MC flow driver)
//   svt_aom_tpl_disp_kernel_iter + kernel :2000-:2122 106 nb (thread kernels)
//   sbo_send_picture_out       :2124-:2137 11 nb (fifo)
//   svt_aom_source_based_operations_kernel_iter + kernel :2243-:2298 50 nb
//   data consumed only by parked walls: tpl_blk_idx_tab :357-:359 3 nb,
//   MAX_TPL_* defines + size/tx arrays :378-:387 10 nb, delta_rate_new
//   :1358-:1365 8 nb, TplRefList :1799-:1804 6 nb, TPL_TASKS defines
//   :1203-:1205 3 nb.
//
// FP POLICY (decided up front, per the addendum's tolerance rule):
// delta_rate_cost / generate_lambda_scaling_factor / svt_aom_generate_r0beta
// / setMbSsimRdmultScaling produce doubles through std::log / std::exp /
// std::pow. Tolerance: EXACT bit equality (==) against the test-side oracle
// transcriptions - both sides run the same CRT on the same compiler with the
// same operation order, so any difference is a transcription bug, not
// rounding noise. The paired-metric caveat (the RE2 oracle precedent) is
// stated: an oracle cannot catch a substitution bug made in BOTH
// transcriptions; the integer-exact boundary cases carry that load.

#include <doctest.h>

#include <analysis.h>
#include <cmath>
#include <cstdint>
#include <vector>

#include "src_ops.h"

namespace srcops = pipeline::srcops;

// ---------------------------------------------------------------------------
// F1: the struct-free kernels.
// ---------------------------------------------------------------------------

// get_neighbor_samples_dc (src_ops_process.c:364-376): the top-left corner
// into above[-1]/left[-1] (:367), the bsize-wide top row via memcpy (:369),
// the bsize-deep left column (:371-:375).
TEST_CASE("l6 srcops getNeighborSamplesDc gathers top row, left col, corner") {
    std::uint8_t grid[64];
    for (int v = 0; v < 8; ++v)
        for (int h = 0; h < 8; ++h) grid[v * 8 + h] = (std::uint8_t)(v * 10 + h);
    std::uint8_t above[1 + 8] = {0};
    std::uint8_t left[1 + 8]  = {0};
    std::uint8_t* aboveRow    = above + 1;  // the caller owns the [-1] slot (:622-:623)
    std::uint8_t* leftCol     = left + 1;

    srcops::getNeighborSamplesDc(grid + 2 * 8 + 2, 8, aboveRow, leftCol, 4);

    CHECK(aboveRow[-1] == 11);  // src[-stride-1] = grid[1][1] (:367)
    CHECK(leftCol[-1] == 11);
    CHECK(aboveRow[0] == 12);  // the top row: grid[1][2..5] (:369)
    CHECK(aboveRow[1] == 13);
    CHECK(aboveRow[2] == 14);
    CHECK(aboveRow[3] == 15);
    // The left col starts at src - 1 = the block's OWN row (:371-:375):
    // grid[2..5][1] - the corner (row-1, col-1) is NOT part of the column.
    CHECK(leftCol[0] == 21);
    CHECK(leftCol[1] == 31);
    CHECK(leftCol[2] == 41);
    CHECK(leftCol[3] == 51);
}

// Same gather at bsize 8 with a 16-stride source - the strides are
// independent and the reads stop exactly at bsize.
TEST_CASE("l6 srcops getNeighborSamplesDc honors stride and bsize") {
    std::uint8_t grid[16 * 16];
    for (int v = 0; v < 16; ++v)
        for (int h = 0; h < 16; ++h) grid[v * 16 + h] = (std::uint8_t)(v * 16 + h);
    std::uint8_t above[1 + 16] = {0};
    std::uint8_t left[1 + 16]  = {0};
    std::uint8_t* aboveRow     = above + 1;
    std::uint8_t* leftCol      = left + 1;

    srcops::getNeighborSamplesDc(grid + 3 * 16 + 1, 16, aboveRow, leftCol, 8);

    // src = &grid[3][1]: the corner is src[-stride-1] = grid[2][0] = 0x20
    // (:367 - one byte before the top row's first byte).
    CHECK(aboveRow[-1] == 0x20);
    CHECK(leftCol[-1] == 0x20);
    for (int i = 0; i < 8; ++i) {
        CHECK(aboveRow[i] == (std::uint8_t)(2 * 16 + 1 + i));   // the top row: grid[2][1..8] (:369)
        // The left col starts at src - 1 = grid[3][0] (:371-:375).
        CHECK(leftCol[i] == (std::uint8_t)((3 + i) * 16));      // grid[3..10][0]
    }
}

// round_floor (src_ops_process.c:1453-1462): the negative branch
// -(1 + (-ref_pos - 1) / bsize_pix) floors toward negative infinity.
TEST_CASE("l6 srcops roundFloor floors toward negative infinity") {
    CHECK(srcops::roundFloor(7, 4) == 1);     // :1458
    CHECK(srcops::roundFloor(8, 4) == 2);
    CHECK(srcops::roundFloor(0, 4) == 0);
    CHECK(srcops::roundFloor(-1, 4) == -1);   // -(1 + 0/4) = -1 (:1456)
    CHECK(srcops::roundFloor(-4, 4) == -1);   // -(1 + 3/4) = -1
    CHECK(srcops::roundFloor(-5, 4) == -2);   // -(1 + 4/4) = -2
    CHECK(srcops::roundFloor(-16, 8) == -2);  // the exact negative multiple
    CHECK(srcops::roundFloor(16, 8) == 2);
}

// get_overlap_area (src_ops_process.c:1423-1451): the 4-quadrant overlap.
// BLOCK_16X16: bw = bh = 4 << mi_size_*_log2[6] = 16.
//   block 0 (:1430-:1433): width = grid_col + bw - ref_col
//                          height = grid_row + bh - ref_row
//   block 1 (:1434-:1437): width = ref_col + bw - grid_col
//   block 2 (:1438-:1441): height = ref_row + bh - grid_row
//   block 3 (:1442-:1445): both ref-based.
TEST_CASE("l6 srcops getOverlapArea four quadrants at BLOCK_16X16") {
    CHECK(srcops::getOverlapArea(0, 0, 4, 6, 0, l16_rdo::BLOCK_16X16) == 120);  // 10 * 12
    CHECK(srcops::getOverlapArea(0, 0, 4, 6, 1, l16_rdo::BLOCK_16X16) == 264);  // 22 * 12
    CHECK(srcops::getOverlapArea(0, 0, 4, 6, 2, l16_rdo::BLOCK_16X16) == 200);  // 10 * 20
    CHECK(srcops::getOverlapArea(0, 0, 4, 6, 3, l16_rdo::BLOCK_16X16) == 440);  // 22 * 20
    CHECK(srcops::getOverlapArea(0, 0, 0, 0, 0, l16_rdo::BLOCK_16X16) == 256);  // the exact tile
}

// Non-square shapes exercise the two log2 tables independently:
// BLOCK_8X8 (bw = 8, bh = 8), BLOCK_32X8 (bw = 4 << log2w[19] = 32,
// bh = 4 << log2h[19] = 8), BLOCK_64X64 (bw = bh = 64).
TEST_CASE("l6 srcops getOverlapArea at non-square and large shapes") {
    CHECK(srcops::getOverlapArea(2, 3, 5, 7, 0, l16_rdo::BLOCK_8X8) == 20);          // 4 * 5
    CHECK(srcops::getOverlapArea(0, 0, 2, 1, 0, l16_rdo::BLOCK_32X8) == 186);        // 31 * 6
    CHECK(srcops::getOverlapArea(10, 10, 30, 20, 1, l16_rdo::BLOCK_64X64) == 3256);  // 74 * 44
}

// The bw/bh derivation enumerated over EVERY BlockSize row (the R-series
// range rule) against the independent oracle 4 * l16_rdo::miSize(Wide|High)
// - the log2 tables and the plain tables are separate transcriptions that
// must agree through the algorithm (4 << log2(v) == 4 * v).
TEST_CASE("l6 srcops getOverlapArea enumerates all BlockSize rows") {
    for (int bs = 0; bs < l16_rdo::BLOCK_SIZES_ALL; ++bs) {
        const int bw      = 4 * l16_rdo::miSizeWide[bs];
        const int bh      = 4 * l16_rdo::miSizeHigh[bs];
        const auto bsEnum = (l16_rdo::BlockSize)bs;
        const int w[4]    = {11 + bw - 29, 29 + bw - 11, 11 + bw - 29, 29 + bw - 11};
        const int h[4]    = {7 + bh - 3, 7 + bh - 3, 3 + bh - 7, 3 + bh - 7};
        CHECK(srcops::getOverlapArea(7, 11, 3, 29, 0, bsEnum) == w[0] * h[0]);
        CHECK(srcops::getOverlapArea(7, 11, 3, 29, 1, bsEnum) == w[1] * h[1]);
        CHECK(srcops::getOverlapArea(7, 11, 3, 29, 2, bsEnum) == w[2] * h[2]);
        CHECK(srcops::getOverlapArea(7, 11, 3, 29, 3, bsEnum) == w[3] * h[3]);
    }
}

// delta_rate_cost (src_ops_process.c:1464-1490).
// The early out (:1468-:1470): srcrf <= 128 returns delta_rate unchanged -
// pinned at the exact boundary 128 vs 129.
TEST_CASE("l6 srcops deltaRateCost early-out at srcrf 128") {
    CHECK(srcops::deltaRateCost(12345, 1000, 128, 256) == 12345);
    CHECK(srcops::deltaRateCost(12345, 1000, 0, 256) == 12345);
}

// beta == 1 (srcrf == recrf) is 0 in BOTH branches - exact integer pins:
// the num/den branch gives log(num/den) = 0; the log_den branch gives
// log(1/beta) = 0. Both then shift by TPL_DEP_COST_SCALE_LOG2 +
// AV1_PROB_COST_SHIFT = 10.
TEST_CASE("l6 srcops deltaRateCost beta one is exactly zero") {
    CHECK(srcops::deltaRateCost(256 << 10, 256, 256, 256) == 0);   // dr = 1: the num/den branch
    CHECK(srcops::deltaRateCost(0, 256, 256, 256) == 0);           // dr = 0
    CHECK(srcops::deltaRateCost(1 << 20, 256, 256, 256) == 0);     // dr = 1024: the log_den branch
    CHECK(srcops::deltaRateCost(256 << 10, 129, 129, 256) == 0);   // just past the 128 gate
}

// The oracle: an independent transcription of :1464-:1490 (same op order;
// the RE2 test-side oracle precedent, the paired-metric caveat stated in
// the file header).
static std::int64_t deltaRateCostOracle(std::int64_t deltaRate, std::int64_t recrfDist,
                                        std::int64_t srcrfDist, int pixNum) {
    const double beta = (double)srcrfDist / (double)recrfDist;
    if (srcrfDist <= 128) {
        return deltaRate;
    }
    const double dr     = (double)(deltaRate >> (4 + 9)) / pixNum;
    const double logDen = std::log(beta) / std::log(2.0) + 2.0 * dr;
    if (logDen > std::log(10.0) / std::log(2.0)) {
        std::int64_t rateCost = (std::int64_t)((std::log(1.0 / beta) * pixNum) / std::log(2.0) / 2.0);
        return rateCost << (4 + 9);
    }
    const double num = std::pow(2.0, logDen);
    const double den = num * beta + (1 - beta) * beta;
    std::int64_t rateCost = (std::int64_t)((pixNum * std::log(num / den)) / std::log(2.0) / 2.0);
    return rateCost << (4 + 9);
}

// The general path against the oracle, including the negative-rate case
// (num < den gives a negative rate_cost; the (int64_t) cast truncates
// toward zero, :1485) and the shifted-multiple property (:1478/:1487).
TEST_CASE("l6 srcops deltaRateCost matches the oracle on the log path") {
    CHECK(srcops::deltaRateCost(256 << 10, 100, 200, 256) == deltaRateCostOracle(256 << 10, 100, 200, 256));
    CHECK(srcops::deltaRateCost(12345, 1000, 129, 256) == deltaRateCostOracle(12345, 1000, 129, 256));
    CHECK(srcops::deltaRateCost(300 << 10, 4096, 4097, 1024) == deltaRateCostOracle(300 << 10, 4096, 4097, 1024));
    CHECK(srcops::deltaRateCost(1 << 10, 200, 1000, 256) == deltaRateCostOracle(1 << 10, 200, 1000, 256));
    CHECK(srcops::deltaRateCost(256 << 10, 100, 200, 256) % (1 << 10) == 0);
}

// svt_aom_get_perpixel_variance (src_ops_process.c:2151-2156): the offs-
// folded variance then ROUND_POWER_OF_TWO(var, eb_num_pels_log2_lookup[bs]).
// 8x8 two-value block (32 samples of 100, 32 of 200): population-variance
// SUM 160000 -> (160000 + 32) >> 6 = 2500.
TEST_CASE("l6 srcops perpixelVariance 8x8 two-value block") {
    std::uint8_t buf[64];
    for (int v = 0; v < 8; ++v)
        for (int h = 0; h < 8; ++h) buf[v * 8 + h] = (v < 4) ? 100 : 200;
    CHECK(srcops::perpixelVariance(buf, 8, l16_rdo::BLOCK_8X8) == 2500);
}

// 16x16: the two-value pattern in 8-row bands (128/128) -> var 640000 ->
// (640000 + 128) >> 8 = 2500; an all-constant block -> 0; a strided 8x8
// window (values 2v+h, var sum 1680) -> (1680 + 32) >> 6 = 26.
TEST_CASE("l6 srcops perpixelVariance 16x16, constant, strided") {
    std::uint8_t buf[256];
    for (int v = 0; v < 16; ++v)
        for (int h = 0; h < 16; ++h) buf[v * 16 + h] = (std::uint8_t)(((v / 4) % 2) ? 200 : 100);
    CHECK(srcops::perpixelVariance(buf, 16, l16_rdo::BLOCK_16X16) == 2500);

    // The flat buffer must cover the full 16x16 read (16 rows x stride 16).
    std::vector<std::uint8_t> flat(16 * 16, 7);
    CHECK(srcops::perpixelVariance(flat.data(), 16, l16_rdo::BLOCK_16X16) == 0);

    // The strided 8x8 window reads 8 rows of 8 at stride 16 (last byte at
    // 7*16+7 = 119) - the buffer covers the full read.
    std::vector<std::uint8_t> strided(128);
    for (int v = 0; v < 8; ++v)
        for (int h = 0; h < 16; ++h) strided[v * 16 + h] = (std::uint8_t)(v * 2 + h);
    CHECK(srcops::perpixelVariance(strided.data(), 16, l16_rdo::BLOCK_8X8) == 26);
}

// The dispatch consistency: perpixelVariance equals the landed l9
// per-size wrappers at both sizes (the named duplication is cross-checked).
TEST_CASE("l6 srcops perpixelVariance equals the l9 sby wrappers") {
    std::uint8_t buf[256];
    for (int v = 0; v < 16; ++v)
        for (int h = 0; h < 16; ++h) buf[v * 16 + h] = (std::uint8_t)((v * 7 + h * 3) & 0xFF);
    CHECK(srcops::perpixelVariance(buf, 16, l16_rdo::BLOCK_8X8) ==
          analysis::getSbyPerpixelVariance8x8(buf, 16));
    CHECK(srcops::perpixelVariance(buf, 16, l16_rdo::BLOCK_16X16) ==
          analysis::getSbyPerpixelVariance16x16(buf, 16));
}

// rate_estimator (src_ops_process.c:263-276). svt_log2f = get_msb
// (definitions.h:591-592); AV1_PROB_COST_SHIFT = 9 (rate_est.h:33).
// eob = 0: (0 + 1) << 9 = 512. eob = 1 with level 7 at scan[0] (position 0
// for every scan class): (2 + get_msb(8) + 1) << 9 = 3072.
TEST_CASE("l6 srcops rateEstimator boundary cases") {
    l16_rdo::initScans();
    std::int32_t qcoeff[256] = {0};
    CHECK(srcops::rateEstimator(qcoeff, 0, 2 /* TX_16X16 */) == 512);
    qcoeff[0] = 7;
    CHECK(srcops::rateEstimator(qcoeff, 1, 2) == 3072);
}

// The scan walk: values placed at scan[0..eob) accumulate
// log2f(level+1) + (level > 0). {1, 4, 0} -> (4 + 2 + 3 + 0) << 9 = 4608.
TEST_CASE("l6 srcops rateEstimator walks the DCT scan order") {
    l16_rdo::initScans();
    std::int32_t qcoeff[256] = {0};
    const auto& order        = l16_rdo::getScanOrder(2 /* TX_16X16 */, 0 /* DCT_DCT */);
    qcoeff[order.scan[0]]    = 1;
    qcoeff[order.scan[1]]    = 4;
    qcoeff[order.scan[2]]    = 0;  // the mid-scan zero: get_msb(1) = 0
    CHECK(srcops::rateEstimator(qcoeff, 3, 2) == (4 + 2 + 3 + 0) << 9);

    std::int32_t q32[1024] = {0};
    const auto& order32    = l16_rdo::getScanOrder(3 /* TX_32X32 */, 0);
    q32[order32.scan[0]]   = 2;
    q32[order32.scan[1]]   = 3;
    // (eob+1) + [msb(3)=1 +1] + [msb(4)=2 +1] = 3 + 2 + 3 = 8 -> 8 << 9.
    CHECK(srcops::rateEstimator(q32, 2, 3) == (3 + 2 + 3) << 9);
}

// get_msb (definitions.h:591-592): the highest set bit. svt_log2f(0) is the
// reference's UB (never called at 0 in the ported spans) - NOT pinned.
TEST_CASE("l6 srcops svtLog2f is get_msb") {
    CHECK(srcops::svtLog2f(1) == 0);
    CHECK(srcops::svtLog2f(2) == 1);
    CHECK(srcops::svtLog2f(3) == 1);
    CHECK(srcops::svtLog2f(4) == 2);
    CHECK(srcops::svtLog2f(255) == 7);
    CHECK(srcops::svtLog2f(256) == 8);
    CHECK(srcops::svtLog2f(1 << 30) == 30);
}

// ---------------------------------------------------------------------------
// F3: the TPL synthesizer carriers.
// ---------------------------------------------------------------------------

// tpl_model_update_b (src_ops_process.c:1496-1549). Fixture: BLOCK_16X16,
// synth 16 (shift 2, step 4), refAlignedWidth 256 (mi_cols_sr 64, grid
// stride 16), ref 64x64 mi. cur at mi (8, 8), mv (24, -8) -> fullmv
// (3, -1) (GET_MV_RAWPEL, mv.h:57) -> ref_pos (31, 35); grid bases
// round_floor(31,16)*16 = 16, round_floor(35,16)*16 = 32. cur_dep = 300;
// mc_dep_prop = 50*300/400 = 37; delta_rate = 20; pix_num = 256.
// Per quadrant, overlap * 337 / 256 and overlap * 20 / 256:
//   block 0 grid (16,32): overlap 13  -> cell 18: dist +17, rate +1
//   block 1 grid (16,48): overlap 3   -> cell 19: dist +3,  rate +0
//   block 2 grid (32,32): overlap 195 -> cell 34: dist +256, rate +15
//   block 3 grid (32,48): overlap 45  -> cell 35: dist +59, rate +3
// Pre-populated ref cells make the += observable.
TEST_CASE("l6 srcops tplModelUpdateB four quadrants, exact int64 divisions") {
    std::vector<srcops::TplStatsCell> grid(256);
    for (auto& c : grid) c = {0, 0, 0, 0, 0, 0, 0, 0};
    grid[18].mcDepDist = 1000;
    grid[18].mcDepRate = 50;
    grid[19].mcDepDist = 2000;
    grid[19].mcDepRate = 60;
    grid[34].mcDepDist = 3000;
    grid[34].mcDepRate = 70;
    grid[35].mcDepDist = 4000;
    grid[35].mcDepRate = 80;

    const std::uint32_t mvPacked   = 24u | ((std::uint32_t)(std::uint16_t)(std::int16_t)-8) << 16;
    const srcops::TplStatsCell cur = {100, 400, 10, 30, 20, 50, mvPacked, 0};
    srcops::TplModelUpdateBArgs a;
    a.refGrid         = grid.data();
    a.cur             = &cur;
    a.miRow           = 8;
    a.miCol           = 8;
    a.bsize           = l16_rdo::BLOCK_16X16;
    a.synthBlkSize    = 16;
    a.computeRate     = 0;
    a.refAlignedWidth = 256;
    a.refMiRows       = 64;
    a.refMiCols       = 64;
    srcops::tplModelUpdateB(a);

    CHECK(grid[18].mcDepDist == 1017);
    CHECK(grid[18].mcDepRate == 51);
    CHECK(grid[19].mcDepDist == 2003);
    CHECK(grid[19].mcDepRate == 60);
    CHECK(grid[34].mcDepDist == 3256);
    CHECK(grid[34].mcDepRate == 85);
    CHECK(grid[35].mcDepDist == 4059);
    CHECK(grid[35].mcDepRate == 83);
    // The other fields of the ref cells are untouched (the reference writes
    // only mc_dep_dist / mc_dep_rate, :1542-:1543).
    CHECK(grid[18].srcrfDist == 0);
    CHECK(grid[18].recrfDist == 0);
    CHECK(grid[0].mcDepDist == 0);
    CHECK(grid[17].mcDepDist == 0);
    CHECK(grid[20].mcDepDist == 0);
}

// The bounds gate (:1530-:1531): quadrants with grid_pos outside
// [0, ref_mi_rows * MI_SIZE) x [0, ref_mi_cols * MI_SIZE) are skipped.
// Fixture: cur at mi (28, 28), mv 0 -> ref_pos (112, 112); grid bases 112;
// bounds 32 * MI_SIZE = 128 px. Only block 0 at (112, 112) is inside;
// blocks 1-3 sit at col/row 128, outside.
TEST_CASE("l6 srcops tplModelUpdateB bounds gate skips outside quadrants") {
    std::vector<srcops::TplStatsCell> grid(320);
    for (auto& c : grid) c = {0, 0, 0, 0, 0, 0, 0, 0};
    grid[231].mcDepDist = 1000;
    grid[231].mcDepRate = 50;

    const srcops::TplStatsCell cur = {100, 400, 10, 30, 20, 50, 0, 0};
    srcops::TplModelUpdateBArgs a;
    a.refGrid         = grid.data();
    a.cur             = &cur;
    a.miRow           = 28;
    a.miCol           = 28;
    a.bsize           = l16_rdo::BLOCK_16X16;
    a.synthBlkSize    = 16;
    a.computeRate     = 0;
    a.refAlignedWidth = 512;  // mi_cols_sr 128, grid stride 32
    a.refMiRows       = 32;
    a.refMiCols       = 32;
    srcops::tplModelUpdateB(a);
    // block 0: grid (112,112) in bounds: overlap 256 -> cell
    //   ((112>>2... ref_mi_row = round_floor(112,16)*4 = 28, ref_mi_col = 28:
    //   ((28+0)>>2)*32 + ((28+0)>>2) = 7*32 + 7 = 231: +337 / +20.
    CHECK(grid[231].mcDepDist == 1337);
    CHECK(grid[231].mcDepRate == 70);
    // block 1 grid (112,128): col 128 >= 128 -> skipped (its would-be cell
    // is 232); block 2 grid (128,112): row 128 >= 128 (would-be 263);
    // block 3 grid (128,128) (would-be 264).
    CHECK(grid[232].mcDepDist == 0);
    CHECK(grid[263].mcDepDist == 0);
    CHECK(grid[264].mcDepDist == 0);
}

// The compute_rate path (:1522-:1524): with srcrf 100 (<= 128) the
// delta_rate_cost early-out returns mc_dep_rate unchanged, so the rate
// increments use (delta_rate + mc_dep_rate) = 20 + 20.
TEST_CASE("l6 srcops tplModelUpdateB computeRate threads deltaRateCost") {
    std::vector<srcops::TplStatsCell> grid(256);
    for (auto& c : grid) c = {0, 0, 0, 0, 0, 0, 0, 0};
    const std::uint32_t mvPacked   = 24u | ((std::uint32_t)(std::uint16_t)(std::int16_t)-8) << 16;
    const srcops::TplStatsCell cur = {100, 400, 10, 30, 20, 50, mvPacked, 0};
    srcops::TplModelUpdateBArgs a;
    a.refGrid         = grid.data();
    a.cur             = &cur;
    a.miRow           = 8;
    a.miCol           = 8;
    a.bsize           = l16_rdo::BLOCK_16X16;
    a.synthBlkSize    = 16;
    a.computeRate     = 1;
    a.refAlignedWidth = 256;
    a.refMiRows       = 64;
    a.refMiCols       = 64;
    srcops::tplModelUpdateB(a);
    // rate += 40 * overlap / 256: block 0: 520/256 = 2; block 1: 120/256 = 0;
    // block 2: 7800/256 = 30; block 3: 1800/256 = 7.
    CHECK(grid[18].mcDepRate == 2);
    CHECK(grid[19].mcDepRate == 0);
    CHECK(grid[34].mcDepRate == 30);
    CHECK(grid[35].mcDepRate == 7);
    // The dist increments are unchanged from the base fixture.
    CHECK(grid[18].mcDepDist == 17);
    CHECK(grid[34].mcDepDist == 256);
}

// tpl_model_update (src_ops_process.c:1555-1582). The verbatim `i` quirk:
// the scan position persists across cells (:1567 declares it outside the
// loops), so once a lookup pushes i past the window, a later cell whose
// ref poc sits at a LOWER window index cannot match (:1574-:1577).
// Fixture: 3 frames (pocs 7, 8, 99), the current frame is 1; the walk
// bsize BLOCK_16X16 with synth 8 (propagation BLOCK_8X8, shift 1, step 2,
// pix_num 64) visits cells 0, 1, 8, 9; cell 0's poc (99 = frames[2])
// matches at i = 2; cells 1/8/9 (poc 7 = frames[0]) then cannot match.
TEST_CASE("l6 srcops tplModelUpdate pins the non-reset scan-index quirk") {
    std::vector<srcops::TplStatsCell> g0(16), g1(16), g2(16);
    for (int i = 0; i < 16; ++i) {
        g0[i] = {0, 0, 0, 0, 0, 0, 0, 7};
        g1[i] = {100, 400, 10, 30, 20, 50, 0, 99};
        g2[i] = {0, 0, 0, 0, 0, 0, 0, 0};
    }
    g1[1].refFramePoc = 7;  // the lower-window-index cells
    g1[8].refFramePoc = 7;
    g1[9].refFramePoc = 7;
    g2[0].mcDepDist   = 1000;
    g2[0].mcDepRate   = 50;
    std::vector<srcops::TplSwFrame> frames = {{g0.data(), 7, 64, 8, 8},
                                              {g1.data(), 8, 64, 8, 8},
                                              {g2.data(), 99, 64, 8, 8}};
    srcops::TplModelUpdateArgs a;
    a.frames       = frames.data();
    a.frameIdx     = 1;
    a.framesInSw   = 3;
    a.miRow        = 0;
    a.miCol        = 0;
    a.synthBlkSize = 8;
    a.computeRate  = 0;
    a.bsize        = l16_rdo::BLOCK_16X16;
    srcops::tplModelUpdate(a);
    CHECK(g2[0].mcDepDist == 1337);  // 1000 + 337 (the one update)
    CHECK(g2[0].mcDepRate == 70);
    CHECK(g2[1].mcDepDist == 0);  // the quirk: blocked by the stale i
    CHECK(g2[8].mcDepDist == 0);
    CHECK(g2[9].mcDepDist == 0);
    for (int i = 0; i < 16; ++i) {
        CHECK(g0[i].mcDepDist == 0);  // frames[0] never written
        if (i != 0) CHECK(g2[i].mcDepDist == 0);
    }
}

// The control: every cell's ref poc = frames[2] -> all four propagate
// (each contributes (300+37)*64/64 = 337 dist, 20 rate).
TEST_CASE("l6 srcops tplModelUpdate all cells matching propagate") {
    std::vector<srcops::TplStatsCell> g0(16), g1(16), g2(16);
    for (int i = 0; i < 16; ++i) {
        g0[i] = {0, 0, 0, 0, 0, 0, 0, 7};
        g1[i] = {100, 400, 10, 30, 20, 50, 0, 99};
        g2[i] = {0, 0, 0, 0, 0, 0, 0, 0};
    }
    g2[0].mcDepDist = 1000;
    g2[0].mcDepRate = 50;
    g2[1].mcDepDist = 2000;
    g2[1].mcDepRate = 60;
    g2[8].mcDepDist = 3000;
    g2[8].mcDepRate = 70;
    g2[9].mcDepDist = 4000;
    g2[9].mcDepRate = 80;
    std::vector<srcops::TplSwFrame> frames = {{g0.data(), 7, 64, 8, 8},
                                              {g1.data(), 8, 64, 8, 8},
                                              {g2.data(), 99, 64, 8, 8}};
    srcops::TplModelUpdateArgs a;
    a.frames       = frames.data();
    a.frameIdx     = 1;
    a.framesInSw   = 3;
    a.miRow        = 0;
    a.miCol        = 0;
    a.synthBlkSize = 8;
    a.computeRate  = 0;
    a.bsize        = l16_rdo::BLOCK_16X16;
    srcops::tplModelUpdate(a);
    CHECK(g2[0].mcDepDist == 1337);
    CHECK(g2[0].mcDepRate == 70);
    CHECK(g2[1].mcDepDist == 2337);
    CHECK(g2[1].mcDepRate == 80);
    CHECK(g2[8].mcDepDist == 3337);
    CHECK(g2[8].mcDepRate == 90);
    CHECK(g2[9].mcDepDist == 4337);
    CHECK(g2[9].mcDepRate == 100);
    CHECK(g2[2].mcDepDist == 0);
    for (int i = 0; i < 16; ++i) CHECK(g0[i].mcDepDist == 0);
}

// tpl_mc_flow_synthesizer (src_ops_process.c:1589-1602): synth 16 walks
// BLOCK_16X16 over mi_rows/mi_cols = 8 (4 calls at (0,0),(0,4),(4,0),
// (4,4)) with shift 2 / grid stride 4 / pix_num 256. Two frames: frame 1
// is the current grid, frame 0 the reference (poc 42) - the common
// cross-frame call shape of :1578 (tpl_model_update_b reads the cur
// cell's mc_dep_dist/mc_dep_rate as propagation INPUTS, so the self-
// mapping variant would feed every cell's own fields into its own
// increment; the separate-grid shape keeps the accumulators clean and
// the input-feed is pinned in the tplModelUpdateB fixtures).
TEST_CASE("l6 srcops tplMcFlowSynthesizer walks the synth grid") {
    std::vector<srcops::TplStatsCell> g0(16), g1(16);
    for (int i = 0; i < 16; ++i) {
        g0[i] = {0, 0, 0, 0, 0, 0, 0, 0};
        g1[i] = {100, 400, 10, 30, 20, 50, 0, 42};
    }
    g0[0].mcDepDist = 1000;
    g0[0].mcDepRate = 50;
    g0[1].mcDepDist = 2000;
    g0[1].mcDepRate = 60;
    g0[4].mcDepDist = 3000;
    g0[4].mcDepRate = 70;
    g0[5].mcDepDist = 4000;
    g0[5].mcDepRate = 80;
    std::vector<srcops::TplSwFrame> frames = {{g0.data(), 42, 64, 8, 8},
                                              {g1.data(), 43, 64, 8, 8}};
    srcops::TplMcFlowSynthesizerArgs a;
    a.frames       = frames.data();
    a.frameIdx     = 1;
    a.framesInSw   = 2;
    a.synthBlkSize = 16;
    a.computeRate  = 0;
    a.miRows       = 8;
    a.miCols       = 8;
    srcops::tplMcFlowSynthesizer(a);
    // Each call: cur_dep 300, mc_dep_prop 50*300/400 = 37, delta_rate 20,
    // block-0 overlap 256 (blocks 1-3 zero-overlap) -> ref cell += 337/+20.
    CHECK(g0[0].mcDepDist == 1337);
    CHECK(g0[0].mcDepRate == 70);
    CHECK(g0[1].mcDepDist == 2337);
    CHECK(g0[1].mcDepRate == 80);
    CHECK(g0[4].mcDepDist == 3337);
    CHECK(g0[4].mcDepRate == 90);
    CHECK(g0[5].mcDepDist == 4337);
    CHECK(g0[5].mcDepRate == 100);
    CHECK(g0[2].mcDepDist == 0);
    CHECK(g0[8].mcDepDist == 0);
    // The cur grid is only read, never written.
    CHECK(g1[0].mcDepDist == 50);
}

// synth 32 folds the walk bsize to BLOCK_32X32 (:1592) - a single call at
// mi (0,0) for mi_rows = mi_cols = 8 (the step is 8), and the propagation
// follows synth 32 (shift 3, pix_num 1024, ref stride mi_cols_sr >> 3).
TEST_CASE("l6 srcops tplMcFlowSynthesizer synth 32 folds bsize to 32X32") {
    std::vector<srcops::TplStatsCell> g0(16), g1(16);
    for (int i = 0; i < 16; ++i) {
        g0[i] = {0, 0, 0, 0, 0, 0, 0, 0};
        g1[i] = {100, 400, 10, 30, 20, 50, 0, 42};
    }
    g0[0].mcDepDist = 1000;
    g0[0].mcDepRate = 50;
    std::vector<srcops::TplSwFrame> frames = {{g0.data(), 42, 128, 8, 8},
                                              {g1.data(), 43, 128, 8, 8}};
    srcops::TplMcFlowSynthesizerArgs a;
    a.frames       = frames.data();
    a.frameIdx     = 1;
    a.framesInSw   = 2;
    a.synthBlkSize = 32;
    a.computeRate  = 0;
    a.miRows       = 8;
    a.miCols       = 8;
    srcops::tplMcFlowSynthesizer(a);
    // One BLOCK_32X32 call: ref cell 0 gets (300 + 37) * 1024/1024 = 337.
    CHECK(g0[0].mcDepDist == 1337);
    CHECK(g0[0].mcDepRate == 70);
    CHECK(g0[1].mcDepDist == 0);  // no second call
    CHECK(g0[4].mcDepDist == 0);
    CHECK(g0[5].mcDepDist == 0);
    CHECK(g1[0].mcDepDist == 50);
}

// ---------------------------------------------------------------------------
// F4: the RDO-driver carriers.
// ---------------------------------------------------------------------------

// coded_to_superres_mi (resize.h:71-73): (mi * denom + SCALE_NUMERATOR/2) /
// SCALE_NUMERATOR with SCALE_NUMERATOR = 8 (definitions.h:1430).
TEST_CASE("l6 srcops codedToSuperresMi") {
    CHECK(srcops::codedToSuperresMi(4, 8) == 4);   // denom 8 = the identity scale
    CHECK(srcops::codedToSuperresMi(3, 8) == 3);
    CHECK(srcops::codedToSuperresMi(1, 16) == 2);  // 2x upscale
    CHECK(srcops::codedToSuperresMi(5, 12) == 8);  // (60 + 4) / 8
    CHECK(srcops::codedToSuperresMi(0, 16) == 0);
}

// The lambda oracle: an independent transcription of :188-:234 for ONE
// factor cell (row, col) - the same op order; the file-header caveat
// applies. RDCOST(rd_cost.h:36-38) = ROUND_POWER_OF_TWO(rate*rm, 9) +
// (dist << RDDIV_BITS).
static double lambdaSfOracle(int miRows, int unscaledWidth, int synthBlkSize, int baseRdmult, double r0,
                             std::int64_t mcDepCostBase, const srcops::TplStatsCell* grid, int row, int col) {
    const int offset        = synthBlkSize == 8 ? 1 : synthBlkSize == 16 ? 2 : 3;
    const int step          = 1 << offset;
    const int miColsSr      = ((unscaledWidth + 15) / 16) << 2;
    const int blockSize     = synthBlkSize == 32 ? l16_rdo::BLOCK_32X32 : l16_rdo::BLOCK_16X16;
    const int numMiW        = l16_rdo::miSizeWide[blockSize];
    const int numMiH        = l16_rdo::miSizeHigh[blockSize];
    const int stride        = miColsSr >> offset;
    std::int64_t recrfSum   = 0;
    std::int64_t mcDepSum   = 0;
    for (int miRow = row * numMiH; miRow < (row + 1) * numMiH; miRow += step) {
        for (int miCol = col * numMiW; miCol < (col + 1) * numMiW; miCol += step) {
            if (miRow >= miRows || miCol >= miColsSr) {
                continue;
            }
            const int index1 = (miRow >> offset) * stride + (miCol >> offset);
            const std::int64_t mcDepDelta =
                (((std::int64_t)grid[index1].mcDepRate * baseRdmult + 256) >> 9) +
                ((std::int64_t)grid[index1].mcDepDist << 7);
            recrfSum += grid[index1].recrfDist;
            mcDepSum += mcDepDelta;
        }
    }
    double scalingFactors = 1.2;
    if (mcDepCostBase && (recrfSum > 0)) {
        const double rk = ((double)(recrfSum << 7)) / ((recrfSum << 7) + mcDepSum);
        scalingFactors += rk / r0;
    }
    return scalingFactors;
}

// generate_lambda_scaling_factor (src_ops_process.c:188-234): the grid
// sums + the rk/r0 factor. unscaledWidth 32 (mi_cols_sr 8), synth 16
// (offset 2, stride 2), miRows 8 (2x2 factor cells), baseRdmult 64,
// r0 0.8, mcDepCostBase 5000.
TEST_CASE("l6 srcops generateLambdaScalingFactor grid walk") {
    std::vector<srcops::TplStatsCell> grid(8);
    for (auto& c : grid) c = {0, 0, 0, 0, 0, 0, 0, 0};
    grid[0].mcDepRate = 1024;
    grid[0].mcDepDist = 512;
    grid[0].recrfDist = 100;
    grid[2].recrfDist = 200;
    double factors[4] = {0, 0, 0, 0};
    srcops::GenerateLambdaScalingFactorArgs a;
    a.grid          = grid.data();
    a.factorsOut    = factors;
    a.miRows        = 8;
    a.unscaledWidth = 32;
    a.synthBlkSize  = 16;
    a.baseRdmult    = 64;
    a.r0            = 0.8;
    a.mcDepCostBase = 5000;
    srcops::generateLambdaScalingFactor(a);
    // RDCOST(64, 1024, 512) = (1024*64 + 256) >> 9 + (512 << 7)
    //                       = 128 + 65536 = 65664.
    CHECK(factors[1] == 1.2);  // recrf 0 -> the guard fails: c exactly
    CHECK(factors[3] == 1.2);
    CHECK(factors[0] == lambdaSfOracle(8, 32, 16, 64, 0.8, 5000, grid.data(), 0, 0));
    CHECK(factors[2] == lambdaSfOracle(8, 32, 16, 64, 0.8, 5000, grid.data(), 1, 0));
    // Structural: cell 2's rk = 1.0 exactly, so its factor exceeds cell 0's.
    CHECK(factors[2] > factors[0]);
}

// The mcDepCostBase == 0 gate (:224): every factor stays exactly c.
TEST_CASE("l6 srcops generateLambdaScalingFactor zero base keeps c") {
    std::vector<srcops::TplStatsCell> grid(8);
    for (auto& c : grid) c = {0, 0, 0, 0, 0, 0, 0, 0};
    grid[0].recrfDist = 100;
    grid[0].mcDepRate = 1024;
    grid[0].mcDepDist = 512;
    double factors[4] = {9.0, 9.0, 9.0, 9.0};
    srcops::GenerateLambdaScalingFactorArgs a;
    a.grid          = grid.data();
    a.factorsOut    = factors;
    a.miRows        = 8;
    a.unscaledWidth = 32;
    a.synthBlkSize  = 16;
    a.baseRdmult    = 64;
    a.r0            = 0.8;
    a.mcDepCostBase = 0;
    srcops::generateLambdaScalingFactor(a);
    CHECK(factors[0] == 1.2);
    CHECK(factors[1] == 1.2);
    CHECK(factors[2] == 1.2);
    CHECK(factors[3] == 1.2);
}

// svt_aom_generate_r0beta (src_ops_process.c:1604-1697). Fixture A: the
// lambda fixture's grid; 1 SB (sbSize 64, aligned 64x64), denom 8, base
// rdmult 64. r0 = 38400 / 104064 (the oracle); the SB window sees the same
// 4 cells so rk == r0 bit-exactly and beta = r0/rk = 1.0 exactly.
TEST_CASE("l6 srcops generateR0beta fills r0, validity, beta") {
    std::vector<srcops::TplStatsCell> grid(8);
    for (auto& c : grid) c = {0, 0, 0, 0, 0, 0, 0, 0};
    grid[0].mcDepRate = 1024;
    grid[0].mcDepDist = 512;
    grid[0].recrfDist = 100;
    grid[2].recrfDist = 200;
    double factors[4] = {0, 0, 0, 0};
    double beta[1]    = {0.0};
    double r0         = 0.5;
    std::int32_t valid = 0;
    std::vector<srcops::SbGeomOrg> sbGeom = {{0, 0}};
    srcops::GenerateR0betaArgs a;
    a.grid           = grid.data();
    a.tplBetaOut     = beta;
    a.sbGeom         = sbGeom.data();
    a.lambdaFactorsOut = factors;
    a.synthBlkSize      = 16;
    a.miRowsCur      = 8;
    a.unscaledWidth  = 32;
    a.unscaledHeight = 32;
    a.alignedWidth   = 64;
    a.alignedHeight  = 64;
    a.sbSize         = 64;
    a.superresDenom  = 8;
    a.baseRdmult     = 64;
    a.r0             = &r0;
    a.tplIsValid     = &valid;
    srcops::generateR0beta(a);

    // cost_base = (300 << 7) + 65664 = 104064; r0 = 38400 / 104064.
    const std::int64_t costBase = (std::int64_t)(300 << 7) + 65664;
    CHECK(r0 == (double)(300 << 7) / (double)costBase);
    CHECK(valid == 1);
    // The lambda fill ran with the written r0 and cost base.
    CHECK(factors[0] == lambdaSfOracle(8, 32, 16, 64, r0, costBase, grid.data(), 0, 0));
    CHECK(factors[1] == 1.2);
    // The SB window sums the same 4 cells -> rk == r0 -> beta exactly 1.0.
    CHECK(beta[0] == 1.0);
}

// Fixture B: one dominant cell but only 4 walk cells - the clamp CANNOT
// fire: the first condition (:1639) needs max_dist > (base_sum/count)*100,
// and with one dominant cell base_sum = max_dist, so the clamp needs
// count > 100. With count = 4, X > (X/4)*100 = 25X is false. r0 keeps the
// computed value 12800/1280012800 and beta = r0/rk = 1.0 exactly (rk is
// the same expression over the same values).
TEST_CASE("l6 srcops generateR0beta no clamp below the 100x average") {
    std::vector<srcops::TplStatsCell> grid(8);
    for (auto& c : grid) c = {0, 0, 0, 0, 0, 0, 0, 0};
    grid[0].mcDepDist = 10000000;
    grid[0].recrfDist = 100;
    double factors[4] = {0, 0, 0, 0};
    double beta[1]    = {0.0};
    double r0         = 0.5;
    std::int32_t valid = 0;
    std::vector<srcops::SbGeomOrg> sbGeom = {{0, 0}};
    srcops::GenerateR0betaArgs a;
    a.grid           = grid.data();
    a.tplBetaOut     = beta;
    a.sbGeom         = sbGeom.data();
    a.lambdaFactorsOut = factors;
    a.synthBlkSize      = 16;
    a.miRowsCur      = 8;
    a.unscaledWidth  = 32;
    a.unscaledHeight = 32;
    a.alignedWidth   = 64;
    a.alignedHeight  = 64;
    a.sbSize         = 64;
    a.superresDenom  = 8;
    a.baseRdmult     = 64;
    a.r0             = &r0;
    a.tplIsValid     = &valid;
    srcops::generateR0beta(a);
    // mc_dep_base = 0 + (10^7 << 7) = 1280000000; cost_base = 1280012800;
    // the clamp's first condition: 1.28e9 > (1.28e9/4)*100 = 3.2e10: FALSE.
    CHECK(r0 == 12800.0 / 1280012800.0);
    CHECK(valid == 1);
    // The SB window: rk = 12800 / 1280012800 = r0 bit-exactly -> beta = 1.0.
    CHECK(beta[0] == 1.0);
    // The lambda fill ran: cell 1's recrf is 0, so its factor stays exactly c.
    CHECK(factors[1] == 1.2);
}

// Fixture B2: the clamp FIRES - the walk must see more than 100 cells
// (the reference's own condition: max_dist > (base_sum/count)*100 with
// base_sum = max_dist needs count > 100). 32 row-steps x 4 col-steps =
// 128 cells; one dominant cell -> max > 100*avg = 1e9 and
// max > base_sum*9/10 -> r0 = 1.0 (:1639-:1641); the SB beta = 1.0/rk.
TEST_CASE("l6 srcops generateR0beta outlier clamp fires past 100 cells") {
    std::vector<srcops::TplStatsCell> grid(128);
    for (auto& c : grid) c = {0, 0, 0, 0, 0, 0, 0, 0};
    grid[0].mcDepDist = 10000000;
    grid[0].recrfDist = 100;
    std::vector<double> factors(128, 0.0);
    double beta[4]     = {0.0, 0.0, 0.0, 0.0};
    double r0         = 0.5;
    std::int32_t valid = 0;
    std::vector<srcops::SbGeomOrg> sbGeom = {{0, 0}, {64, 0}, {0, 64}, {64, 64}};
    srcops::GenerateR0betaArgs a;
    a.grid           = grid.data();
    a.tplBetaOut     = beta;
    a.sbGeom         = sbGeom.data();
    a.lambdaFactorsOut = factors.data();
    a.synthBlkSize      = 16;
    a.miRowsCur      = 128;
    a.unscaledWidth  = 64;   // mi_cols_sr 16, grid stride 4
    a.unscaledHeight = 512;  // the SB-loop row guard mi_rows 128
    a.alignedWidth   = 128;
    a.alignedHeight  = 128;  // the SB grid 2x2
    a.sbSize         = 64;
    a.superresDenom  = 8;
    a.baseRdmult     = 64;
    a.r0             = &r0;
    a.tplIsValid     = &valid;
    srcops::generateR0beta(a);
    // mc_dep_base = 1.28e9; count = 128; the clamp: 1.28e9 >
    // (1.28e9/128)*100 = 1e9 AND > 1.28e9*9/10.
    CHECK(r0 == 1.0);
    CHECK(valid == 1);
    // SB 0's window covers cells 0..15: rk = 12800/1280012800; beta = r0/rk.
    CHECK(beta[0] == 1.0 / (12800.0 / 1280012800.0));
    // SBs 1/2/3: their windows are either past mi_cols_sr or recrf-zero
    // -> beta exactly 1.0 (:1686-:1692).
    CHECK(beta[1] == 1.0);
    CHECK(beta[2] == 1.0);
    CHECK(beta[3] == 1.0);
    // The lambda fill ran with the clamped r0: the recrf-zero cells keep c.
    CHECK(factors[1] == 1.2);
}

// Fixture C: the all-zero grid leaves r0 untouched and tpl_is_valid 0
// (:1643-:1645); the lambda fill still runs (factors = c, the mcDepCostBase
// 0 gate) and beta = 1.0 (recrf 0).
TEST_CASE("l6 srcops generateR0beta zero grid keeps r0") {
    std::vector<srcops::TplStatsCell> grid(8);
    for (auto& c : grid) c = {0, 0, 0, 0, 0, 0, 0, 0};
    double factors[4] = {0, 0, 0, 0};
    double beta[1]    = {9.0};
    double r0         = 0.7;
    std::int32_t valid = 1;
    std::vector<srcops::SbGeomOrg> sbGeom = {{0, 0}};
    srcops::GenerateR0betaArgs a;
    a.grid           = grid.data();
    a.tplBetaOut     = beta;
    a.sbGeom         = sbGeom.data();
    a.lambdaFactorsOut = factors;
    a.synthBlkSize      = 16;
    a.miRowsCur      = 8;
    a.unscaledWidth  = 32;
    a.unscaledHeight = 32;
    a.alignedWidth   = 64;
    a.alignedHeight  = 64;
    a.sbSize         = 64;
    a.superresDenom  = 8;
    a.baseRdmult     = 64;
    a.r0             = &r0;
    a.tplIsValid     = &valid;
    srcops::generateR0beta(a);
    CHECK(valid == 0);
    CHECK(r0 == 0.7);
    CHECK(beta[0] == 1.0);
    CHECK(factors[0] == 1.2);  // the lambda fill's c (mcDepCostBase 0)
    CHECK(factors[3] == 1.2);
}

// aom_av1_set_mb_ssim_rdmult_scaling (src_ops_process.c:2158-2236).
// Fixture A: a uniform 32x32 frame (mi 8x8) - every 8x8 variance is 0, so
// every factor is the curve's floor 17.492222 and the log-sum normalization
// divides all four by the same value.
TEST_CASE("l6 srcops setMbSsimRdmultScaling uniform frame") {
    std::vector<std::uint8_t> y(32 * 32, 7);
    double factors[4] = {0, 0, 0, 0};
    srcops::SetMbSsimRdmultScalingArgs a;
    a.yBuffer    = y.data();
    a.yStride    = 32;
    a.miCols     = 8;
    a.miRows     = 8;
    a.factorsOut = factors;
    srcops::setMbSsimRdmultScaling(a);
    // The oracle for the normalization walk (:2210 + :2219 + :2224-:2235):
    // log_sum accumulates log(var) per cell in walk order, then
    // exp(log_sum / (num_rows * num_cols)), then each factor /= log_sum.
    double logSum = 0.0;
    for (int i = 0; i < 4; ++i) logSum += std::log(17.492222);
    logSum = std::exp(logSum / 4.0);
    CHECK(factors[0] == 17.492222 / logSum);
    CHECK(factors[1] == 17.492222 / logSum);
    CHECK(factors[2] == 17.492222 / logSum);
    CHECK(factors[3] == 17.492222 / logSum);
}

// Fixture B: the top-left 16x16 carries the two-value pattern INSIDE each
// 8x8 block (rows 0-3 = 100, rows 4-7 = 200 of every 8x8) so all four of
// its 8x8 variances are 2500; the rest is flat. The high-variance cell's
// factor exceeds the flat cells' (the curve is monotone in var).
TEST_CASE("l6 srcops setMbSsimRdmultScaling non-uniform cell") {
    std::vector<std::uint8_t> y(32 * 32, 7);
    for (int v = 0; v < 16; ++v)
        for (int h = 0; h < 16; ++h) y[v * 32 + h] = (std::uint8_t)((v % 8 < 4) ? 100 : 200);
    double factors[4] = {0, 0, 0, 0};
    srcops::SetMbSsimRdmultScalingArgs a;
    a.yBuffer    = y.data();
    a.yStride    = 32;
    a.miCols     = 8;
    a.miRows     = 8;
    a.factorsOut = factors;
    srcops::setMbSsimRdmultScaling(a);
    // cell (0,0): var = 4 * 2500 / 4 = 2500; the curve at 2500 (:2202).
    const double curve = 67.035434 * (1.0 - std::exp(-0.0021489 * 2500.0)) + 17.492222;
    // The curve output stays inside the reference's assert range
    // (:2208: 17 < var < 85).
    CHECK(curve > 17.0);
    CHECK(curve < 85.0);
    // The normalization oracle in the reference's walk order
    // ((0,0) first, then (0,1), (1,0), (1,1)).
    double logSum = 0.0;
    logSum += std::log(curve);
    for (int i = 0; i < 3; ++i) logSum += std::log(17.492222);
    logSum = std::exp(logSum / 4.0);
    CHECK(factors[0] == curve / logSum);
    CHECK(factors[3] == 17.492222 / logSum);
    CHECK(factors[0] > factors[3]);
}
