#pragma once

#include <cstdint>
#include <string>

namespace transforms {

std::int32_t cospi13(int index);

std::int32_t sinpi13(int index);

std::int32_t halfBtf(std::int32_t w0, std::int32_t in0, std::int32_t w1, std::int32_t in1, int bit);

std::int32_t roundShift(std::int64_t value, int bit);

void fdct4(const std::int32_t input[4], std::int32_t output[4]);

// TX1: the same kernel with the reference's cos_bit parameter (12 at the
// TX_4X16 row pass per fwd_cos_bit_row[0][2])
void fdct4B(const std::int32_t input[4], std::int32_t output[4], int cosBit);

void fadst4(const std::int32_t input[4], std::int32_t output[4]);

// TX1: the same kernel with the reference's cos_bit parameter (12 at the
// TX_4X16 ADST row pass per fwd_cos_bit_row[0][2]); sinpi row follows the bit
void fadst4B(const std::int32_t input[4], std::int32_t output[4], int cosBit);

void fdct8(const std::int32_t input[8], std::int32_t output[8]);

// TX1: the same kernel with the reference's cos_bit parameter (12 at the
// TX_8X32 row pass per fwd_cos_bit_row[1][3])
void fdct8B(const std::int32_t input[8], std::int32_t output[8], int cosBit);

void fadst8(const std::int32_t input[8], std::int32_t output[8]);

// TX1: the same kernel with the reference's cos_bit parameter (12 at the
// TX_8X32 ADST row pass per fwd_cos_bit_row[1][3])
void fadst8B(const std::int32_t input[8], std::int32_t output[8], int cosBit);

// svt_av1_fdct16_new (transforms.c:268), cos_bit = 13
void fdct16(const std::int32_t input[16], std::int32_t output[16]);

// TX1: the cos_bit-parameterized form the cfg machinery dispatches through
void fdct16B(const std::int32_t input[16], std::int32_t output[16], int cosBit);

// svt_av1_fadst16_new (transforms.c:1714), cos_bit = 13
void fadst16(const std::int32_t input[16], std::int32_t output[16]);

// TX1: the cos_bit-parameterized form the cfg machinery dispatches through
void fadst16B(const std::int32_t input[16], std::int32_t output[16], int cosBit);

// svt_av1_fdct32_new (transforms.c:422), cos_bit = 12 (fwd_cos_bit[3][3])
void fdct32(const std::int32_t input[32], std::int32_t output[32]);

// TX1: the same kernel with the reference's cos_bit parameter (13 at the
// TX_32X16 row pass, 11 at the TX_32X64 / TX_64X32 row passes)
void fdct32B(const std::int32_t input[32], std::int32_t output[32], int cosBit);

// static av1_fadst32_new (transforms.c:1908), cos_bit = 12
void fadst32(const std::int32_t input[32], std::int32_t output[32]);

// TX1: the same kernel with the reference's cos_bit parameter (13 at the
// TX_32X16 ADST row pass, 11 at the TX_32X64 / TX_64X32 ADST row passes)
void fadst32B(const std::int32_t input[32], std::int32_t output[32], int cosBit);

// svt_av1_idct32_new (inv_transforms.c:378), cos_bit = 12; stage_range
// consumed at the butterfly stages per the C3-style audit (L3 slice)
void idct32(const std::int32_t input[32], std::int32_t output[32]);

// static av1_iadst32_new (inv_transforms.c:1132), cos_bit = 12
void iadst32(const std::int32_t input[32], std::int32_t output[32]);

// svt_av1_fdct64_new (transforms.c:762), cos_bit = 13 (fwd_cos_bit_col[4][4]);
// verbatim body (pointer-swap structure preserved). B variant takes cos_bit
// (TX_64X64 row pass uses 10 per fwd_cos_bit_row[4][4])
void fdct64(const std::int32_t input[64], std::int32_t output[64]);
void fdct64B(const std::int32_t input[64], std::int32_t output[64], int cosBit);

// svt_av1_idct64_new (inv_transforms.c:1567), cos_bit = 12; clamp stages per
// the source audit; DCT-only (no ADST at 64x64, inv_transforms.h:196)
void idct64(const std::int32_t input[64], std::int32_t output[64]);

void idct4(const std::int32_t input[4], std::int32_t output[4]);

void iadst4(const std::int32_t input[4], std::int32_t output[4]);

void idct8(const std::int32_t input[8], std::int32_t output[8]);

void iadst8(const std::int32_t input[8], std::int32_t output[8]);

// svt_av1_idct16_new (inv_transforms.c:215), cos_bit = 12 (INV_COS_BIT);
// stage_range consumed at stages 3-7 only (clamp_value on the butterfly
// adds), shim {16,...} proven by gen_inv_range_16x16_dct
void idct16(const std::int32_t input[16], std::int32_t output[16]);

// svt_av1_iadst16_new (inv_transforms.c:927), cos_bit = 12; stage_range
// consumed at stages 3/5/7; no all-zero early-out at 16
void iadst16(const std::int32_t input[16], std::int32_t output[16]);

enum class TxType {
    DCT_DCT,
    ADST_DCT,
    DCT_ADST,
    ADST_ADST,
};

// The square-up transform size class, i.e. the range of txsize_sqr_up_map
// (definitions.h). l3 is a per-size layer with no TxSize enum, so the LUT
// takes the square-up class directly rather than a TxSize; the gate
// comparisons below are SVT's own, in the same order.
enum class TxSizeSqUp {
    TX_4X4,
    TX_8X8,
    TX_16X16,
    TX_32X32,
    TX_64X64,
};

// TX1: the full TxSize shape list, verbatim order of definitions.h:955-974
// (the fwd_txfm_shift_ls / inv_txfm_shift_ls indexing order)
enum class TxSize {
    TX_4X4,
    TX_8X8,
    TX_16X16,
    TX_32X32,
    TX_64X64,
    TX_4X8,
    TX_8X4,
    TX_8X16,
    TX_16X8,
    TX_16X32,
    TX_32X16,
    TX_32X64,
    TX_64X32,
    TX_4X16,
    TX_16X4,
    TX_8X32,
    TX_32X8,
    TX_16X64,
    TX_64X16,
};

// tx_size_wide / tx_size_high (common_utils.c:116-122)
int txSizeWide(TxSize size);
int txSizeHigh(TxSize size);

// get_rect_tx_log_ratio (inv_transforms.h:293-315)
int rectTxLogRatio(int col, int row);

// TX1: TxfmType (inv_transforms.h:84-101), the 1D kernel classes the cfg
// tables name. IDENTITY* and the ADST-at-64 entry are carried for table
// fidelity; our TxType scope can only reach DCT* and ADST4..ADST32.
enum class TxfmTypeLs {
    DCT4,
    DCT8,
    DCT16,
    DCT32,
    DCT64,
    ADST4,
    ADST8,
    ADST16,
    ADST32,
    IDENTITY4,
    IDENTITY8,
    IDENTITY16,
    IDENTITY32,
    IDENTITY64,
    INVALID,
};

// TX1: Txfm2dFlipCfg (inv_transforms.h:103-116). stageRangeCol/Row hold the
// NON-SCALE ranges (set_fwd_txfm_non_scale_range, transforms.c:2471-2492);
// the cores compute the final bd-8 ranges locally.
struct Txfm2dFlipCfg {
    TxSize txSize;
    int udFlip;
    int lrFlip;
    const std::int8_t* shift;
    std::int8_t cosBitCol;
    std::int8_t cosBitRow;
    std::int8_t stageRangeCol[12];
    std::int8_t stageRangeRow[12];
    TxfmTypeLs txfmTypeCol;
    TxfmTypeLs txfmTypeRow;
    int stageNumCol;
    int stageNumRow;
};

// TX1: svt_aom_transform_config (transforms.c:2494-2510)
void fwdTransformConfig(TxType type, TxSize size, Txfm2dFlipCfg& cfg);

// TX1: svt_av1_get_inv_txfm_cfg (inv_transforms.c:2470-2494)
void invTransformConfig(TxType type, TxSize size, Txfm2dFlipCfg& cfg);

// TX1: inv_txfm2d_add_c (inv_transforms.c:2496-2579); txfmBuf is the
// caller-provided w*h + 2*max(w,h) scratch
void invTxfm2dAddCore(const std::int32_t* input, std::uint8_t* dst, std::uint32_t stride,
                      const Txfm2dFlipCfg& cfg, std::int32_t* txfmBuf);

// TX1: svt_av1_inv_txfm2d_add_4x8_c (inv_transforms.c:2630-2635)
void invTxfm2dAdd4x8(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// TX1: svt_av1_inv_txfm2d_add_8x4_c (inv_transforms.c:2637-2642)
void invTxfm2dAdd8x4(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// TX1: svt_av1_inv_txfm2d_add_8x16_c (inv_transforms.c:2644-2650)
void invTxfm2dAdd8x16(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// TX1: svt_av1_inv_txfm2d_add_16x8_c (inv_transforms.c:2652-2658)
void invTxfm2dAdd16x8(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// TX1: svt_av1_inv_txfm2d_add_16x32_c (inv_transforms.c:2660-2666)
void invTxfm2dAdd16x32(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// TX1: svt_av1_inv_txfm2d_add_32x16_c (inv_transforms.c:2668-2674)
void invTxfm2dAdd32x16(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// TX1: svt_av1_inv_txfm2d_add_4x16_c (inv_transforms.c:2736-2742)
void invTxfm2dAdd4x16(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// TX1: svt_av1_inv_txfm2d_add_16x4_c (inv_transforms.c:2743-2749)
void invTxfm2dAdd16x4(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// TX1: svt_av1_inv_txfm2d_add_8x32_c (inv_transforms.c:2750-2756)
void invTxfm2dAdd8x32(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// TX1: svt_av1_inv_txfm2d_add_32x8_c (inv_transforms.c:2758-2764)
void invTxfm2dAdd32x8(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// TX1: svt_av1_inv_txfm2d_add_64x32_c (inv_transforms.c:2676-2690); coeffs is
// the COMPACTED 32x32 block (max_eob 1024, inv_transforms.h:129-137)
void invTxfm2dAdd64x32(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// TX1: svt_av1_inv_txfm2d_add_32x64_c (inv_transforms.c:2692-2704); coeffs is
// the COMPACTED 32x32 block (max_eob 1024)
void invTxfm2dAdd32x64(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// TX1: svt_av1_inv_txfm2d_add_16x64_c (inv_transforms.c:2706-2718); coeffs is
// the COMPACTED 16x32 block (max_eob 512)
void invTxfm2dAdd16x64(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// TX1: svt_av1_inv_txfm2d_add_64x16_c (inv_transforms.c:2720-2734); coeffs is
// the COMPACTED 32x16 block (max_eob 512)
void invTxfm2dAdd64x16(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// TX1: the dispatch admissible-set predicates: fwd mirrors the dispatch
// asserts of av1_estimate_transform_default, inv mirrors the type table plus
// the square-wrapper asserts
bool fwdTxfmSupported(TxSize size, TxType type);
bool invTxfmSupported(TxSize size, TxType type);

// TX1: the dispatch switches, av1_estimate_transform_default
// (transforms.c:3185-3360) and highbd_inv_txfm_add (inv_transforms.c:3167-3236)
void fwdTxfm2d(TxSize size, const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);
void invTxfm2dAdd(TxSize size, const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// TX1: av1_tranform_two_d_core_c (transforms.c:2398-2469); `buf` is the
// caller-provided w*h intermediate (the facade's intermediate_transform_buffer)
void fwdTxfm2dCore(const std::int16_t* input, std::uint32_t stride, std::int32_t* output,
                   const Txfm2dFlipCfg& cfg, std::int32_t* buf);

// TX1: svt_av1_fwd_txfm2d_4x8_c (transforms.c:2790-2797)
void fwdTxfm2d4x8(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// TX1: svt_av1_fwd_txfm2d_8x4_c (transforms.c:2781-2788)
void fwdTxfm2d8x4(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// TX1: svt_av1_fwd_txfm2d_8x16_c (transforms.c:2736-2743)
void fwdTxfm2d8x16(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// TX1: svt_av1_fwd_txfm2d_16x8_c (transforms.c:2727-2734)
void fwdTxfm2d16x8(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// TX1: svt_av1_fwd_txfm2d_16x32_c (transforms.c:2718-2725)
void fwdTxfm2d16x32(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// TX1: svt_av1_fwd_txfm2d_32x16_c (transforms.c:2709-2716)
void fwdTxfm2d32x16(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// TX1: svt_av1_fwd_txfm2d_32x64_c (transforms.c:2616-2625)
void fwdTxfm2d32x64(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// TX1: svt_av1_fwd_txfm2d_64x32_c (transforms.c:2594-2602)
void fwdTxfm2d64x32(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// TX1: svt_av1_fwd_txfm2d_4x16_c (transforms.c:2772-2779)
void fwdTxfm2d4x16(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// TX1: svt_av1_fwd_txfm2d_16x4_c (transforms.c:2763-2770)
void fwdTxfm2d16x4(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// TX1: svt_av1_fwd_txfm2d_8x32_c (transforms.c:2754-2761)
void fwdTxfm2d8x32(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// TX1: svt_av1_fwd_txfm2d_32x8_c (transforms.c:2745-2752)
void fwdTxfm2d32x8(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// TX1: svt_av1_fwd_txfm2d_16x64_c (transforms.c:2655-2664)
void fwdTxfm2d16x64(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// TX1: svt_av1_fwd_txfm2d_64x16_c (transforms.c:2633-2641)
void fwdTxfm2d64x16(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// svt_aom_get_intra_uv_tx_type (mode_decision.c:2985-2997). `mode` is the
// ALREADY-FOLDED luma-space intra mode (0..12): the reference folds internally
// via get_uv_mode, but l3 must not depend on l4, so the caller folds with
// intra::uv2y (the port's 1:1 copy of get_uv_mode, common_utils.h:130-133).
// That fold is the one place the SVT shape is bent; it is a pure table lookup
// with an identical value. `reduced_tx_set` is 1 (the ratified config, entropy.cpp
// writeTxType) and `is_inter` is 0, so get_ext_tx_set_type can only return
// DCTONLY or DTT4_IDTX.
TxType intraUvTxType(int mode, TxSizeSqUp sqrUp);

void fwdTxfm2d4x4(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

void fwdTxfm2d8x8(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// av1_tranform_two_d_core_c at TX_16X16: fwd_shift_16x16 = {2,-2,0}
// (transforms.c:124), cos_bit col 13 / row 12 (fwd_cos_bit_col/row[2][2],
// transforms.c:19-22)
void fwdTxfm2d16x16(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// av1_tranform_two_d_core_c at TX_32X32: fwd_shift_32x32 = {2,-4,0}
// (transforms.c:125), cos_bit col 12 / row 12 (fwd_cos_bit_col/row[3][3],
// transforms.c:19-22)
void fwdTxfm2d32x32(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// av1_tranform_two_d_core_c at TX_64X64: fwd_shift_64x64 = {0,-2,-2}
// (transforms.c:126), cos_bit col 13 / row 10 (fwd_cos_bit_col/row[4][4],
// transforms.c:19-22). DCT-ONLY at 64x64 (no ADST exists in this tree,
// inv_transforms.h:196) - type must be DCT_DCT.
void fwdTxfm2d64x64(const std::int16_t* input, std::int32_t* output, std::uint32_t stride, TxType type);

// svt_av1_inv_txfm2d_add_64x64_c / inv_txfm2d_add_c, TX_64X64:
// inv_shift_64x64 = {-2,-4} (inv_transforms.c:22), cos_bit 12/12, rows then
// columns with clamps 16/16; add via clip_pixel_highbd(pred +
// round_shift(out, 4), 8). DCT-only.
void invTxfm2dAdd64x64(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// svt_av1_inv_txfm2d_add_4x4_c (inv_transforms.c:2591), 8-bit: coeffs -> inv
// 2D -> add onto pred block in place with clip to [0,255]
void invTxfm2dAdd4x4(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

void invTxfm2dAdd8x8(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// svt_av1_inv_txfm2d_add_16x16_c / inv_txfm2d_add_c, TX_16X16: inv_shift_16x16
// = {-2,-4} (inv_transforms.c:20), cos_bit 12/12 (inv_cos_bit_col/row[2][2]),
// rows then columns with clamps 16/16; add via clip_pixel_highbd(pred +
// round_shift(out, 4), 8)
void invTxfm2dAdd16x16(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

// svt_av1_inv_txfm2d_add_32x32_c / inv_txfm2d_add_c, TX_32X32: inv_shift_32x32
// = {-2,-4} (inv_transforms.c:21), cos_bit 12/12 (inv_cos_bit_col/row[3][3]),
// rows then columns with clamps 16/16; add via clip_pixel_highbd(pred +
// round_shift(out, 4), 8)
void invTxfm2dAdd32x32(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

std::string invTxfmCuSource();

std::string fwdTxfmCuSource();

// Luma quantizer tables at sharpness == 0, mirroring the luma rows of
// svt_av1_build_quantizer (md_config_process.c:106-135). Index 0 = dc,
// index 1 = ac (the "dc path" is table index 0 - this SVT tree has no
// separate av1_quantize_dc).
struct QuantTables {
    std::int16_t quant[2];        // y_quant (:130 via svt_aom_invert_quant)
    std::int16_t quantShift[2];   // y_quant_shift (:130)
    std::int16_t quantFp[2];      // y_quant_fp (:131)
    std::int16_t roundFp[2];      // y_round_fp (:127, :132)
    std::int16_t zbin[2];         // y_zbin (:133)
    std::int16_t round[2];        // y_round (:108, :134)
    std::int16_t dequant[2];      // y_dequant_qtx (:135)
};

void buildQuantTables(std::int32_t qindex, QuantTables& tables);

// default (up-right diagonal) scan for 4x4, svt_aom_init_iscan formula
// (coefficients.c:345-363) at W=H=4
void defaultScan4x4(std::int16_t scan[16]);

// quantize_fp_helper_c (full_loop.c:222) at log_scale 0 = TX_4X4
// (svt_av1_quantize_fp_c, full_loop.c:286). qm/iqm NULL branch.
void quantizeFp4x4(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                   std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob);

// svt_aom_quantize_b_c (full_loop.c:31) at log_scale 0, qm/iqm NULL branch.
void quantizeB4x4(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                  std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob);

// TX_8X8 entries: same helpers at n_coeffs=64, log_scale 0
// (av1_get_tx_scale_tab[TX_8X8] = 0, full_loop.c:22 + :1617).
void quantizeFp8x8(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                   std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob);

void quantizeB8x8(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                  std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob);

// default (up-right diagonal) scan for 8x8, same svt_aom_init_iscan formula
// (coefficients.c:345-363) at W=H=8
void defaultScan8x8(std::int16_t scan[64]);

// TX_16X16 entries: same helpers at n_coeffs=256, log_scale 0
// (av1_get_tx_scale_tab[TX_16X16] = 0, full_loop.c:22 + :1617).
void quantizeFp16x16(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                     std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob);

void quantizeB16x16(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                    std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob);

// default (up-right diagonal) scan for 16x16, same svt_aom_init_iscan formula
// (coefficients.c:345-363) at W=H=16
void defaultScan16x16(std::int16_t scan[256]);

// TX_32X32 entries (L5): same helpers at n_coeffs=1024, log_scale 1
// (av1_get_tx_scale_tab[TX_32X32] = 1, full_loop.c:22)
void quantizeFp32x32(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                     std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob);

void quantizeB32x32(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                    std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob);

// default (up-right diagonal) scan for 32x32, same svt_aom_init_iscan formula
// (coefficients.c:345-363) at W=H=32
void defaultScan32x32(std::int16_t scan[1024]);

// TX_64X64 entries (L9): same helpers at n_coeffs=4096, log_scale 2
// (av1_get_tx_scale_tab[TX_64X64] = 2, full_loop.c:22). DCT-only at 64x64
// (no ADST exists in this tree), but the fp/b helpers are TxType-agnostic.
void quantizeFp64x64(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                     std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob);

void quantizeB64x64(const std::int32_t* coeff, const QuantTables& tables, const std::int16_t* scan,
                    std::int32_t* qcoeff, std::int32_t* dqcoeff, std::uint16_t* eob);

// FS4: the TX_64X64 EMISSION-domain quantize (the FS2 scan contract):
// n_coeffs = 1024 over the COMPACTED 32-wide quadrant (full_loop.c:1262
// passes n_coeffs = av1_get_max_eob(TX_64X64) = 1024; inv_transforms.h:129-137),
// log_scale 2 (av1_get_tx_scale_tab, full_loop.c:22). The caller compacts the
// fwd64 output's top-left 32x32 first (transforms.c:2700-2707) and passes the
// normative 1024-position token scan (the W=H=32 scan pool,
// coefficients.c:331-337).
void quantizeFp64x64Token(const std::int32_t* coeff, const QuantTables& tables,
                          const std::int16_t* scan, std::int32_t* qcoeff, std::int32_t* dqcoeff,
                          std::uint16_t* eob);

// default (up-right diagonal) scan for 64x64, same svt_aom_init_iscan formula
// (coefficients.c:345-363) at W=H=64
void defaultScan64x64(std::int16_t scan[4096]);

std::string quantCuSource();

}  // namespace transforms
