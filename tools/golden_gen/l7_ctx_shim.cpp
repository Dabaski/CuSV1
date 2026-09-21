// TD2 context-equality gate shim: extern "C" wrappers exposing the l7
// ported twins (namespace entropy) to the C generator drive, so the
// exhaustive enumeration can assert bit-exact SVT-extract-vs-l7 equality
// in one process. Plumbing only - no arithmetic here.
#include <cstdint>
#include <cstring>
#include "entropy.h"

extern "C" {

int l7_getPaddedIdx(int idx, int bwl) {
    return entropy::getPaddedIdx(idx, bwl);
}

int l7_getNzMag(const std::uint8_t* levels, int bwl, int tx_class) {
    return entropy::getNzMag(levels, bwl, static_cast<entropy::TxClass>(tx_class));
}

int l7_getNzMapCtxFromStats(int stats, int coeff_idx, int bwl, int tx_size, int tx_class) {
    return entropy::getNzMapCtxFromStats(stats, coeff_idx, bwl,
                                         static_cast<entropy::TxSize>(tx_size),
                                         static_cast<entropy::TxClass>(tx_class));
}

int l7_getLowerLevelsCtxEob(int bwl, int height, int scan_idx) {
    return entropy::getLowerLevelsCtxEob(bwl, height, scan_idx);
}

int l7_getLowerLevelsCtx(const std::uint8_t* levels, int coeff_idx, int bwl, int tx_size,
                         int tx_class) {
    return entropy::getLowerLevelsCtx(levels, coeff_idx, bwl,
                                      static_cast<entropy::TxSize>(tx_size),
                                      static_cast<entropy::TxClass>(tx_class));
}

int l7_getBrCtxEob(int c, int bwl, int tx_class) {
    return entropy::getBrCtxEob(c, bwl, static_cast<entropy::TxClass>(tx_class));
}

int l7_getBrCtx(const std::uint8_t* levels, int c, int bwl, int tx_class) {
    return entropy::getBrCtx(levels, c, bwl, static_cast<entropy::TxClass>(tx_class));
}

void l7_txbInitLevels(const std::int32_t* coeff, int width, int height,
                      std::uint8_t* levels) {
    entropy::txbInitLevels(coeff, width, height, levels);
}

// TD2c: the golomb + raw-sign surface. The l7 AomReader/AomWriter layouts
// mirror the AOM-upstream structs, which differ from the vendored SVT
// bitreader.h shape (the SVT reader carries buffer/buffer_end before ec) -
// so the shims do NOT cast generator structs onto l7 types; they run the
// l7 codec end-to-end locally and exchange only bytes/values. (TD2 gate
// finding: the naive cast crashed - the layouts are structurally
// different, named in the TD2 report.)
int l7_writeGolombToBuf(int level, unsigned char* out, unsigned cap, unsigned* outSize) {
    static unsigned char sbuf[64];
    entropy::AomWriter w;
    w.ec.buf = sbuf;
    entropy::odEcEncReset(&w.ec);
    w.allow_update_cdf = 1;
    w.pos = 0;
    entropy::writeGolomb(&w, level);
    entropy::odEcStopEncode(&w);
    if (w.pos > cap) return -2;
    if (w.pos > 0) memcpy(out, sbuf, w.pos);
    *outSize = w.pos;
    return 0;
}

int l7_readGolombFromBytes(const unsigned char* buffer, unsigned size, int* out) {
    entropy::AomReader r;
    if (entropy::odEcReaderInit(&r, buffer, size)) return -1;
    *out = entropy::readGolomb(&r);
    return 0;
}

// 3 writes: raw bit, dc-sign cdf symbol, raw bit (the chain's sign order).
int l7_signtrace(const unsigned char* buffer, unsigned size, int dcsign0, int* out) {
    entropy::AomReader r;
    if (entropy::odEcReaderInit(&r, buffer, size)) return -1;
    out[0] = entropy::odEcReadBit(&r);
    const entropy::AomCdfProb row[3] = { static_cast<entropy::AomCdfProb>(dcsign0), 0, 0 };
    out[1] = entropy::odEcReadCdf(&r, row, 2);
    out[2] = entropy::odEcReadBit(&r);
    out[3] = entropy::odEcReadBit(&r);
    return 0;
}

} // extern "C"