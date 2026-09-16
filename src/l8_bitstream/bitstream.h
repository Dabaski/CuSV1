#pragma once

#include <cstddef>
#include <cstdint>

// l8_bitstream - host port of the SVT raw-bit writer + container ground
// floor (BSF2). Source: Source/Lib/Codec/entropy_coding.{h,c} (pinned
// vendored tree). Bodies are verbatim ports with camelCase names; integer
// only, bit-exact.

namespace bitstream {

// AomWriteBitBuffer (entropy_coding.h:116-119, verbatim layout)
struct AomWriteBitBuffer {
    std::uint8_t* bit_buffer;
    std::uint32_t bit_offset;
};

// ObuType (av1_structs.h:22-32, verbatim order and values)
enum ObuType {
    OBU_SEQUENCE_HEADER        = 1,
    OBU_TEMPORAL_DELIMITER     = 2,
    OBU_FRAME_HEADER           = 3,
    OBU_TILE_GROUP             = 4,
    OBU_METADATA               = 5,
    OBU_FRAME                  = 6,
    OBU_REDUNDANT_FRAME_HEADER = 7,
    OBU_PADDING                = 15,
};

// AomCodecErr slice (definitions.h:1493-1497): only the two values consumed
// by write_uleb_obu_size (:3656-3666).
enum AomCodecErr {
    SVT_AOM_CODEC_OK = 0,
    SVT_AOM_CODEC_ERROR = 1,
};

// svt_aom_wb_is_byte_aligned (entropy_coding.c:1343-1345)
std::int32_t wbIsByteAligned(const AomWriteBitBuffer* wb);

// svt_aom_wb_bytes_written (entropy_coding.c:1347-1349)
std::uint32_t wbBytesWritten(const AomWriteBitBuffer* wb);

// svt_aom_wb_write_bit (entropy_coding.c:1372-1374; body via the inlined
// static :1351-1363)
void wbWriteBit(AomWriteBitBuffer* wb, std::int32_t bit);

// svt_aom_wb_write_literal (entropy_coding.c:1376-1378; body via the
// inlined static :1365-1370, MSB-first)
void wbWriteLiteral(AomWriteBitBuffer* wb, std::int32_t data, std::int32_t bits);

// svt_aom_wb_write_inv_signed_literal (entropy_coding.c:1380-1382): writes
// bits+1 two's-complement bits
void wbWriteInvSignedLiteral(AomWriteBitBuffer* wb, std::int32_t data, std::int32_t bits);

// svt_aom_uleb_size_in_bytes (entropy_coding.c:1313-1319)
std::size_t ulebSizeInBytes(std::uint64_t value);

// svt_aom_uleb_encode (entropy_coding.c:1321-1341). Returns
// SVT_AOM_CODEC_OK (0) or -1 on the :1323-1326 guard.
std::int32_t ulebEncode(std::uint64_t value, std::size_t available, std::uint8_t* coded_value,
                        std::size_t* coded_size);

// write_obu_header (entropy_coding.c:3639-3654): forbidden 0 / type 4b /
// obuExtension 1b / has_payload_length 1 hardcoded / reserved 0. Deviation:
// static in SVT, public here (consumed directly by the tests and by BSF3
// assembly).
std::uint32_t writeObuHeader(ObuType obu_type, std::int32_t obuExtension, std::uint8_t* const dst);

// write_uleb_obu_size (entropy_coding.c:3656-3666): uleb-encodes the payload
// size at dst + obu_header_size. Returns SVT_AOM_CODEC_OK/ERROR.
std::int32_t writeUlebObuSize(std::uint32_t obu_header_size, std::uint32_t obu_payload_size,
                              std::uint8_t* dest);

// svt_aom_encode_td_av1 (entropy_coding.c:3953-3960): OBU_TD = header byte +
// uleb size 0 = exactly 2 bytes (0x12 0x00). Return flattened from
// EbErrorType: always EB_ErrorNone (0, API/EbSvtAv1.h:123).
std::int32_t encodeTdAv1(std::uint8_t* output_bitstream_ptr);

}  // namespace bitstream
