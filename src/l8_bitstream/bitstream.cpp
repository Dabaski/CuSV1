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

}  // namespace bitstream
