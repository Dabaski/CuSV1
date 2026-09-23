// l8_bitstream - host port of the SVT raw-bit writer + container ground
// floor (BSF2). Source: Source/Lib/Codec/entropy_coding.{h,c} (pinned
// vendored tree). Bodies are verbatim ports with camelCase names; NOINLINE
// is an icache hint, dropped (l7 precedent). Integer only, bit-exact.

#include "bitstream.h"

#include <cassert>
#include <climits>

namespace bitstream {

// svt_aom_wb_is_byte_aligned (entropy_coding.c:1343-1345)
std::int32_t wbIsByteAligned(const AomWriteBitBuffer* wb) {
    return (wb->bit_offset % CHAR_BIT == 0);
}

// svt_aom_wb_bytes_written (entropy_coding.c:1347-1349)
std::uint32_t wbBytesWritten(const AomWriteBitBuffer* wb) {
    return wb->bit_offset / CHAR_BIT + (wb->bit_offset % CHAR_BIT > 0);
}

// svt_aom_wb_write_bit_inlined (entropy_coding.c:1351-1363)
static inline void wbWriteBitInlined(AomWriteBitBuffer* wb, std::int32_t bit) {
    const std::int32_t off = static_cast<std::int32_t>(wb->bit_offset);
    const std::int32_t p   = off / CHAR_BIT;
    const std::int32_t q   = CHAR_BIT - 1 - off % CHAR_BIT;
    if (q == CHAR_BIT - 1) {
        // zero next char and write bit
        wb->bit_buffer[p] = static_cast<std::uint8_t>(bit << q);
    } else {
        wb->bit_buffer[p] = static_cast<std::uint8_t>(wb->bit_buffer[p] & ~(1 << q));
        wb->bit_buffer[p] = static_cast<std::uint8_t>(wb->bit_buffer[p] | (bit << q));
    }
    wb->bit_offset = static_cast<std::uint32_t>(off + 1);
}

// svt_aom_wb_write_literal_inlined (entropy_coding.c:1365-1370)
static inline void wbWriteLiteralInlined(AomWriteBitBuffer* wb, std::int32_t data, std::int32_t bits) {
    for (std::int32_t bit = bits - 1; bit >= 0; bit--) {
        wbWriteBit(wb, (data >> bit) & 1);
    }
}

// svt_aom_wb_write_bit (entropy_coding.c:1372-1374)
void wbWriteBit(AomWriteBitBuffer* wb, std::int32_t bit) {
    wbWriteBitInlined(wb, bit);
}

// svt_aom_wb_write_literal (entropy_coding.c:1376-1378)
void wbWriteLiteral(AomWriteBitBuffer* wb, std::int32_t data, std::int32_t bits) {
    wbWriteLiteralInlined(wb, data, bits);
}

// svt_aom_wb_write_inv_signed_literal (entropy_coding.c:1380-1382)
void wbWriteInvSignedLiteral(AomWriteBitBuffer* wb, std::int32_t data, std::int32_t bits) {
    wbWriteLiteralInlined(wb, data, bits + 1);
}

// svt_aom_uleb_size_in_bytes (entropy_coding.c:1313-1319)
std::size_t ulebSizeInBytes(std::uint64_t value) {
    std::size_t size = 0;
    do {
        ++size;
    } while ((value >>= 7) != 0);
    return size;
}

// svt_aom_uleb_encode (entropy_coding.c:1321-1341); the file-scope constants
// k_maximum_leb_128_size/value (:1310-1311) folded in as literals (verbatim
// values 8 / 0xFFFFFFFFFFFFFF).
std::int32_t ulebEncode(std::uint64_t value, std::size_t available, std::uint8_t* coded_value,
                        std::size_t* coded_size) {
    const std::size_t leb_size = ulebSizeInBytes(value);
    if (value > 0xFFFFFFFFFFFFFFull || leb_size > 8 || leb_size > available || !coded_value ||
        !coded_size) {
        return -1;
    }

    for (std::size_t i = 0; i < leb_size; ++i) {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7f);
        value >>= 7;

        if (value != 0) {
            byte |= 0x80;  // Signal that more bytes follow.
        }

        *(coded_value + i) = byte;
    }

    *coded_size = leb_size;
    return 0;
}

// write_obu_header (entropy_coding.c:3639-3654)
std::uint32_t writeObuHeader(ObuType obu_type, std::int32_t obuExtension, std::uint8_t* const dst) {
    AomWriteBitBuffer wb = {dst, 0};
    std::uint32_t size = 0;

    wbWriteLiteral(&wb, 0, 1);  // forbidden bit.
    wbWriteLiteral(&wb, static_cast<std::int32_t>(obu_type), 4);
    wbWriteLiteral(&wb, obuExtension ? 1 : 0, 1);
    wbWriteLiteral(&wb, 1, 1);  // obu_has_payload_length_field
    wbWriteLiteral(&wb, 0, 1);  // reserved

    if (obuExtension) {
        wbWriteLiteral(&wb, obuExtension & 0xFF, 8);
    }
    size = wbBytesWritten(&wb);
    return size;
}

// write_uleb_obu_size (entropy_coding.c:3656-3666)
std::int32_t writeUlebObuSize(std::uint32_t obu_header_size, std::uint32_t obu_payload_size,
                              std::uint8_t* dest) {
    const std::uint32_t obu_size       = obu_payload_size;
    const std::uint32_t offset         = obu_header_size;
    std::size_t         coded_obu_size = 0;

    if (ulebEncode(obu_size, sizeof(obu_size), dest + offset, &coded_obu_size) != 0) {
        return SVT_AOM_CODEC_ERROR;
    }

    return SVT_AOM_CODEC_OK;
}

// svt_aom_encode_td_av1 (entropy_coding.c:3953-3960)
std::int32_t encodeTdAv1(std::uint8_t* output_bitstream_ptr) {
    assert(output_bitstream_ptr != nullptr);

    // move data and insert OBU_TD preceded by optional 4 byte size
    // OBUs are preceded/succeeded by an unsigned leb128 coded integer.
    writeUlebObuSize(writeObuHeader(OBU_TEMPORAL_DELIMITER, 0, output_bitstream_ptr), 0,
                     output_bitstream_ptr);
    return 0;  // EB_ErrorNone (API/EbSvtAv1.h:123)
}

// ---------------------------------------------------------------------------
// BSF3: structural keyframe assembly. Field values are the ratified BSF0(g)
// config; the D1 mono patch spans are named in bitstream.h and below.
// ---------------------------------------------------------------------------
// add_trailing_bits (entropy_coding.c:3668-3675): byte-aligned -> 0x80,
// else a single 1 bit (the trailing zeros are assumed - buffer zeros).
static void wbAddTrailingBits(AomWriteBitBuffer* wb) {
    if (wbIsByteAligned(wb)) {
        wbWriteLiteral(wb, 0x80, 8);
    } else {
        // assumes that the other bits are already 0s
        wbWriteBit(wb, 1);
    }
}

// write_sequence_header (entropy_coding.c:2754-2839), ratified values
static void bsf3SequenceHeader(AomWriteBitBuffer* wb, int maxDim) {
    // FS5b: maxDim parameterizes the max dims (a power of two):
    // frame_width_bits = msb(maxDim) (the generator's svtd_bsf3_sequence_header
    // formula, :2757-2764). 32 -> 5 (the committed walk); 4/8/16/64 -> 2/3/4/6.
    int bits = 0;
    for (int v = maxDim; v > 1; v >>= 1) ++bits;
    wbWriteLiteral(wb, bits - 1, 4);       // frame_width_bits - 1 (:2775)
    wbWriteLiteral(wb, bits - 1, 4);       // frame_height_bits - 1 (:2776)
    wbWriteLiteral(wb, maxDim - 1, bits);  // max_frame_width - 1 (:2777)
    wbWriteLiteral(wb, maxDim - 1, bits);  // max_frame_height - 1 (:2778)
    wbWriteBit(wb, 0);              // frame_id_numbers_present_flag (:2784; sequence_control_set.c:85)
    wbWriteBit(wb, 0);              // use_128x128 (:2795; sb 64, enc_handle.c:4072-4090)
    wbWriteBit(wb, 1);              // filter_intra_level (:2797; ratified BSF0(g))
    wbWriteBit(wb, 1);              // enable_intra_edge_filter (:2798; enc_mode_config.c:2877)
    wbWriteBit(wb, 0);              // enable_interintra_compound (:2801)
    wbWriteBit(wb, 0);              // enable_masked_compound (:2802)
    wbWriteBit(wb, 0);              // enable_warped_motion (:2804)
    wbWriteBit(wb, 0);              // enable_dual_filter (:2805; sequence_control_set.c:91)
    wbWriteBit(wb, 0);              // enable_order_hint = 0 (D3; SVT default 1, sequence_control_set.c:104; :2807)
    wbWriteBit(wb, 1);              // seq_force_screen_content_tools == 2 -> choose bit 1 (:2814-2815)
    wbWriteBit(wb, 1);              // seq_force_integer_mv == 2 -> choose bit 1 (:2821-2823)
    wbWriteBit(wb, 0);              // enable_superres (:2836; enc_mode_config.c:2824)
    wbWriteBit(wb, 0);              // cdef_level (:2837)
    wbWriteBit(wb, 0);              // enable_restoration (:2838)
}

// write_color_config (entropy_coding.c:2687-2752) + D1 spans 1 and 2
static void bsf3ColorConfig(AomWriteBitBuffer* wb) {
    wbWriteBit(wb, 0);  // high_bitdepth: 8-bit (:2676-2679)
    // D1 span 1: :2689 const is_monochrome = 0 -> ratified 1; the bit IS
    // written (profile != HIGH_PROFILE, :2691-2692).
    wbWriteBit(wb, 1);  // is_monochrome = 1 (D1)
    wbWriteBit(wb, 0);  // color_description_present (:2696-2699; CP/TC/MC unspecified)
    // D1 span 2: the commented-out mono branch (:2706-2710) live:
    wbWriteBit(wb, 1);  // color_range = 1 (court-ratified full range; :2708)
    return;             // mono return (:2709): skips subsampling (:2720-2745)
                        // and separate_uv_delta_q (:2747-2751)
}

// write_sequence_header_obu (:3699-3763), ratified reduced=0 path
std::uint32_t writeSequenceHeaderObu(std::uint8_t* dst, int maxDim) {
    AomWriteBitBuffer wb = {dst, 0};
    wbWriteLiteral(&wb, 0, 3);  // profile = MAIN_PROFILE (:3705; enc_settings.c:989)
    wbWriteBit(&wb, 1);         // still_picture (:3708)
    wbWriteBit(&wb, 0);         // reduced_still_picture_header (:3712; ratified D2)
    wbWriteBit(&wb, 0);         // timing_info_present (:3717; never set in the pinned tree)
    wbWriteBit(&wb, 0);         // initial_display_delay_present_flag (:3726; ratified BSF0(g))
    wbWriteLiteral(&wb, 0, 5);  // operating_points_cnt_minus_1 (:3728-3729)
    wbWriteLiteral(&wb, 0, 12); // operating_point[0].op_idc (:3732)
    wbWriteLiteral(&wb, 0, 5);  // seq_level_idx = 0 (level 2.0, :121-129 + entropy_coding.h:81-84; :3733)
    // tier skipped (level major 2 <= 3, :3734-3736); decoder model / initial
    // display delay skipped (:3737-3750)
    bsf3SequenceHeader(&wb, maxDim);
    bsf3ColorConfig(&wb);
    wbWriteBit(&wb, 0);  // film_grain_params_present (:3757; enc_handle.c:4449)
    wbAddTrailingBits(&wb);  // (:3759)
    return wbBytesWritten(&wb);
}

// write_uncompressed_header_obu (:3294-3637), ratified structural KF walk
// (BSF0(b): 21 bits)
std::uint32_t writeFrameHeader(std::uint8_t* dst) {
    AomWriteBitBuffer wb = {dst, 0};
    wbWriteBit(&wb, 0);          // show_existing_frame (:3333)
    wbWriteLiteral(&wb, 0, 2);   // frame_type = KEY_FRAME (:3336)
    wbWriteBit(&wb, 1);          // show_frame (:3338)
    wbWriteBit(&wb, 0);          // disable_cdf_update (:3350; BSF4-fix: SVT keyframe default 0, resource_coordination_process.c:360 - the ec coupling ec_process.c:101 allow_update_cdf = !disable_cdf_update requires 0 for the adapted emission)
    wbWriteBit(&wb, 0);          // allow_screen_content_tools (force == 2 -> bit written, :3352-3353)
    wbWriteBit(&wb, 0);          // frame_size_override_flag (:3386; frame == max)
    wbWriteBit(&wb, 0);          // render_and_frame_size_different (:2616-2624)
    wbWriteBit(&wb, 1);          // refresh_frame_context == DISABLED (BSF4-fix: might_bwd_adapt = !reduced && !disable_cdf_update = 1, :3548; default DISABLED per resource_coordination_process.c:381; :3553)
    wbWriteBit(&wb, 1);          // uniform_tile_spacing_flag (:2405; increments 0 bits for 1 tile, :2410-2425)
    wbWriteLiteral(&wb, 0, 8);   // base_q_idx (encode_quantization :2376)
    wbWriteBit(&wb, 0);          // delta_q Y dc (:2365-2372)
    // D1 span 3: U/V delta_q writes (:2385-2386) SKIPPED for mono
    wbWriteBit(&wb, 0);          // using_qmatrix (:2391)
    wbWriteBit(&wb, 0);          // segmentation_enabled (:2255)
    wbWriteBit(&wb, 1);          // reduced_tx_set (:3626)
    // NO trailing bits: appendTrailingBits = show_existing = 0 (:3858)
    return wbBytesWritten(&wb);
}

// TS4: the lossy header v2 (write_uncompressed_header_obu :3294-3637
// lossy walk). The 22 structural bits with base_q_idx = 100 (VALUE change,
// still 8 bits), then the delta_q block LIVE (base_q_idx > 0,
// :3565-3587): delta_q_present 1 bit = 0 (delta_lf is nested INSIDE
// delta_q_present - not written at 0, court-verified nesting), then
// all_lossless = 0 -> encode_loopfilter (:2290-2299): loop_filter_level[0]
// 6 bits = 0, loop_filter_level[1] 6 bits = 0 (the level[2]/[3] U/V pair
// skipped at zero AND for mono, :2296-2299), sharpness 3 bits = 0,
// mode_ref_delta_enabled 1 bit = 0 (deltas skipped), CDEF/restoration
// still skipped (seq cdef_level = 0 / enable_restoration = 0),
// tx_mode_select 1 bit = 0 = TX_MODE_LARGEST (:3603-3607), reduced_tx_set
// 1 bit = 1. Total 22 + 18 = 40 bits = exactly 5 bytes, NO byte_alignment
// padding.
std::uint32_t writeFrameHeaderV2(std::uint8_t* dst) {
    AomWriteBitBuffer wb = {dst, 0};
    wbWriteBit(&wb, 0);           // show_existing_frame (:3333)
    wbWriteLiteral(&wb, 0, 2);    // frame_type = KEY_FRAME (:3336)
    wbWriteBit(&wb, 1);           // show_frame (:3338)
    wbWriteBit(&wb, 0);           // disable_cdf_update (:3350; BSF4-fix)
    wbWriteBit(&wb, 0);           // allow_screen_content_tools (:3352-3353)
    wbWriteBit(&wb, 0);           // frame_size_override_flag (:3386)
    wbWriteBit(&wb, 0);           // render_and_frame_size_different (:2616-2624)
    wbWriteBit(&wb, 1);           // refresh_frame_context == DISABLED (:3553)
    wbWriteBit(&wb, 1);           // uniform_tile_spacing_flag (:2405)
    wbWriteLiteral(&wb, 100, 8);  // base_q_idx = 100 (encode_quantization :2376)
    wbWriteBit(&wb, 0);           // delta_q Y dc (:2365-2372)
    // D1 span 3: U/V delta_q writes (:2385-2386) SKIPPED for mono
    wbWriteBit(&wb, 0);           // using_qmatrix (:2391)
    wbWriteBit(&wb, 0);           // segmentation_enabled (:2255)
    wbWriteBit(&wb, 0);           // delta_q_present (:3565-3587; delta_lf nested)
    wbWriteLiteral(&wb, 0, 6);    // loop_filter_level[0] (:2290-2299)
    wbWriteLiteral(&wb, 0, 6);    // loop_filter_level[1] (U/V pair [2]/[3] skipped)
    wbWriteLiteral(&wb, 0, 3);    // loop_filter_sharpness
    wbWriteBit(&wb, 0);           // loop_filter_delta_enabled (deltas skipped)
    wbWriteBit(&wb, 0);           // tx_mode_select = 0 -> TX_MODE_LARGEST (:3603-3607)
    wbWriteBit(&wb, 1);           // reduced_tx_set (:3626)
    // NO trailing bits: appendTrailingBits = show_existing = 0 (:3858)
    return wbBytesWritten(&wb);
}

// svt_aom_encode_sps_av1 (:3925-3948) structure: phase 1 measure, phase 2
// rewrite (the payload size depends on its own content; the content is
// stable across the two writes)
static std::uint32_t bsf3EncodeSps(std::uint8_t* dst, int maxDim) {
    const std::uint32_t obu_header_size = writeObuHeader(OBU_SEQUENCE_HEADER, 0, dst);
    const std::uint32_t obu_payload_size = writeSequenceHeaderObu(dst + obu_header_size, maxDim);
    const std::size_t length_field_size = ulebSizeInBytes(obu_payload_size);
    std::size_t coded_size = 0;
    ulebEncode(obu_payload_size, sizeof(obu_payload_size), dst + obu_header_size, &coded_size);
    writeSequenceHeaderObu(dst + obu_header_size + length_field_size,
                           maxDim);  // phase 2 rewrite
    return obu_header_size + static_cast<std::uint32_t>(length_field_size) + obu_payload_size;
}

// svt_aom_write_frame_header_av1 (:3843-3920) structure for the single-tile
// OBU_FRAME
static std::uint32_t bsf3FrameObu(std::uint8_t* dst, const std::uint8_t* tile_data,
                                  std::uint32_t tile_size) {
    const std::uint32_t obu_header_size = writeObuHeader(OBU_FRAME, 0, dst);
    const std::uint32_t frame_hdr_size = writeFrameHeader(dst + obu_header_size);
    const std::uint32_t tg_hdr_size = 0;  // single tile: write_tile_group_header writes 0 bytes (:3770-3772)
    const std::uint32_t obu_payload_size = frame_hdr_size + tg_hdr_size + tile_size;
    const std::size_t length_field_size = ulebSizeInBytes(obu_payload_size);
    std::size_t coded_size = 0;
    ulebEncode(obu_payload_size, sizeof(obu_payload_size), dst + obu_header_size, &coded_size);
    const std::uint32_t write_offset = obu_header_size + static_cast<std::uint32_t>(length_field_size);
    writeFrameHeader(dst + write_offset);  // phase 2 rewrite (:3896)
    // tile data copy (:3902-3915); no per-tile size prefix at tile_cnt == 1
    // (:3906-3908)
    for (std::uint32_t i = 0; i < tile_size; ++i) {
        dst[write_offset + frame_hdr_size + tg_hdr_size + i] = tile_data[i];
    }
    return write_offset + obu_payload_size;
}

// TS4 v2 packer: identical structure with the lossy header v2. The SPS is
// shared (byte-identical at lossy - qidx is frame-header state; the
// generator HALTs if sps_obu_v2 moves).
static std::uint32_t bsf3FrameObuV2(std::uint8_t* dst, const std::uint8_t* tile_data,
                                    std::uint32_t tile_size) {
    const std::uint32_t obu_header_size = writeObuHeader(OBU_FRAME, 0, dst);
    const std::uint32_t frame_hdr_size = writeFrameHeaderV2(dst + obu_header_size);
    const std::uint32_t tg_hdr_size = 0;  // single tile: 0 bytes (:3770-3772)
    const std::uint32_t obu_payload_size = frame_hdr_size + tg_hdr_size + tile_size;
    const std::size_t length_field_size = ulebSizeInBytes(obu_payload_size);
    std::size_t coded_size = 0;
    ulebEncode(obu_payload_size, sizeof(obu_payload_size), dst + obu_header_size, &coded_size);
    const std::uint32_t write_offset = obu_header_size + static_cast<std::uint32_t>(length_field_size);
    writeFrameHeaderV2(dst + write_offset);  // phase 2 rewrite (:3896)
    for (std::uint32_t i = 0; i < tile_size; ++i) {
        dst[write_offset + frame_hdr_size + tg_hdr_size + i] = tile_data[i];
    }
    return write_offset + obu_payload_size;
}

std::uint32_t assembleStructuralKeyframeTUv2(std::uint8_t* dst, const std::uint8_t* tile_data,
                                             std::uint32_t tile_size, int maxDim) {
    // Zero the region first: the byte-aligned 40-bit header needs no pad
    // bits, but the tile offset must be exact; zeroing guarantees the spec's
    // zero state for any future pad (named, as in the v1 assembler).
    // Upper bound: 2 (TD) + SPS (<= 16) + FRAME (<= 96); zero 128 to cover
    // every ratified-config TU.
    for (std::uint32_t i = 0; i < 128; ++i) dst[i] = 0;
    std::uint32_t offset = 0;
    encodeTdAv1(dst + offset);
    offset += 2;  // TD_SIZE (packetization_process.c:300)
    offset += bsf3EncodeSps(dst + offset, maxDim);
    offset += bsf3FrameObuV2(dst + offset, tile_data, tile_size);
    return offset;
}

std::uint32_t assembleStructuralKeyframeTU(std::uint8_t* dst, const std::uint8_t* tile_data,
                                           std::uint32_t tile_size) {
    // Zero the region first: the 3 pad bits after the 21-bit frame header
    // must be the spec's byte_alignment zero bits (the SVT packer relies on
    // its OutputBitstreamUnit buffer state; we guarantee zeros - named).
    // Upper bound: 2 (TD) + SPS (<= 16) + FRAME (<= 64); zero 128 to cover
    // every ratified-config TU.
    for (std::uint32_t i = 0; i < 128; ++i) dst[i] = 0;
    std::uint32_t offset = 0;
    encodeTdAv1(dst + offset);
    offset += 2;  // TD_SIZE (packetization_process.c:300)
    offset += bsf3EncodeSps(dst + offset, 32);
    offset += bsf3FrameObu(dst + offset, tile_data, tile_size);
    return offset;
}

}  // namespace bitstream
