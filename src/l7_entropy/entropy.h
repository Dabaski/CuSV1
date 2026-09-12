#pragma once

#include <cstdint>

namespace entropy {

// OdEcEnc - bitstream_unit.h:101-120 (verbatim member layout: low/rng/cnt/
// error/buf/ptr). OdEcWindow - bitstream_unit.h:96 (uint64_t window).
typedef std::uint64_t OdEcWindow;

struct OdEcEnc {
    OdEcWindow low;
    std::uint32_t rng;
    std::int16_t cnt;
    std::int16_t error;
    unsigned char* buf;
    unsigned char* ptr;
};

// svt_od_ec_enc_reset (bitstream_unit.c:185-197)
void odEcEncReset(OdEcEnc* enc);

}  // namespace entropy