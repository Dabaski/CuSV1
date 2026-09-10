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

void idct4(const std::int32_t input[4], std::int32_t output[4]);

void iadst4(const std::int32_t input[4], std::int32_t output[4]);

void idct8(const std::int32_t input[8], std::int32_t output[8]);

void iadst8(const std::int32_t input[8], std::int32_t output[8]);

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

// svt_av1_inv_txfm2d_add_4x4_c (inv_transforms.c:2591), 8-bit: coeffs -> inv
// 2D -> add onto pred block in place with clip to [0,255]
void invTxfm2dAdd4x4(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

void invTxfm2dAdd8x8(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

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

std::string quantCuSource();

}  // namespace transforms
