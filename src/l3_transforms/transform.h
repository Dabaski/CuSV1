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

// svt_av1_inv_txfm2d_add_4x4_c (inv_transforms.c:2591), 8-bit: coeffs -> inv
// 2D -> add onto pred block in place with clip to [0,255]
void invTxfm2dAdd4x4(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

void invTxfm2dAdd8x8(const std::int32_t* coeffs, std::uint8_t* dst, std::uint32_t stride, TxType type);

std::string invTxfmCuSource();

std::string fwdTxfmCuSource();

}  // namespace transforms
