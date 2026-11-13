// l15_rc - the rate-control table surface (A9, RC1).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/rc_tables.h.
// LANDED (RC1, 2026-11-05): the 18 qindex-mapping tables (QINDEX_RANGE =
// MAXQ - MINQ + 1 = 256, definitions.h:1643), mechanically extracted -
// see rc_tables.cpp; the paired-metric caveat is stated there and in the
// register. findTable is the registry lookup the diff-verify test walks.
#pragma once

#include <cstdint>

namespace l15_rc {

struct RcTable {
    const char* name;
    const std::int32_t* data;
    std::int32_t len;
};

extern const RcTable rcTables[];

const std::int32_t* findTable(const char* name, std::int32_t& len);

// av1_lambda_mode_decision8_bit_sad (lambda_rate_tables.h:23-45): the 8-bit
// SAD lambda table (256 entries, mechanically extracted; see rc_tables.cpp).
// The 10/12-bit lambda tables are the named HBD park (dead in the pinned
// RTC 8-bit build - lambda_rate_tables.h:46).
extern const std::uint32_t kLambdaModeDecision8BitSad[256];

}
