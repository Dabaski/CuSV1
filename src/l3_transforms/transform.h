#pragma once

#include <cstdint>
#include <string>

namespace transforms {

std::int32_t cospi13(int index);

std::int32_t sinpi13(int index);

std::int32_t halfBtf(std::int32_t w0, std::int32_t in0, std::int32_t w1, std::int32_t in1, int bit);

std::int32_t roundShift(std::int64_t value, int bit);

void fdct4(const std::int32_t input[4], std::int32_t output[4]);

void fadst4(const std::int32_t input[4], std::int32_t output[4]);

void fdct8(const std::int32_t input[8], std::int32_t output[8]);

void fadst8(const std::int32_t input[8], std::int32_t output[8]);

// svt_av1_fdct16_new (transforms.c:268), cos_bit = 13
void fdct16(const std::int32_t input[16], std::int32_t output[16]);

// svt_av1_fadst16_new (transforms.c:1714), cos_bit = 13
void fadst16(const std::int32_t input[16], std::int32_t output[16]);

// svt_av1_fdct32_new (transforms.c:422), cos_bit = 12 (fwd_cos_bit[3][3])
void fdct32(const std::int32_t input[32], std::int32_t output[32]);

// static av1_fadst32_new (transforms.c:1908), cos_bit = 12
void fadst32(const std::int32_t input[32], std::int32_t output[32]);

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
    ADST_ADST,
};

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
// index 1 = ac (the "dc path" is table index 0 — this SVT tree has no
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
