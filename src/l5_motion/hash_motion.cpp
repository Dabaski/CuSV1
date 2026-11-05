#include "hash_motion.h"

namespace motion {

namespace {

// hash.c:15 CRC-32C (iSCSI) polynomial in reversed bit order.
constexpr std::uint32_t kPoly = 0x82f63b78;
// hash.c:25 the process-wide table (zero-init; crc32cInit fills it).
std::uint32_t crc32cTable[8][256];

}  // namespace

// hash.c:28-50 svt_av1_crc32c_table_init.
void crc32cInit() {
    for (int n = 0; n < 256; n++) {
        std::uint32_t crc = n;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 1) ? (crc >> 1) ^ kPoly : crc >> 1;
        }
        crc32cTable[0][n] = crc;
    }
    for (int n = 0; n < 256; n++) {
        std::uint32_t crc = crc32cTable[0][n];
        for (int k = 1; k < 8; k++) {
            crc = crc32cTable[0][crc & 0xff] ^ (crc >> 8);
            crc32cTable[k][n] = crc;
        }
    }
}

// hash.c:55-76 svt_av1_get_crc32c_value_c. The reference reads
// *(uint64_t*)next unaligned (little-endian assumption, hash.c:54); kept
// as-is for fidelity on x64.
std::uint32_t getCrc32cValue(const std::uint8_t* buf, std::size_t len) {
    const std::uint8_t* next = buf;
    std::uint64_t crc = 0 ^ 0xffffffff;
    while (len && (reinterpret_cast<std::uintptr_t>(next) & 7) != 0) {
        crc = crc32cTable[0][(crc ^ *next++) & 0xff] ^ (crc >> 8);
        len--;
    }
    while (len >= 8) {
        crc ^= *reinterpret_cast<const std::uint64_t*>(next);
        crc = crc32cTable[7][crc & 0xff] ^ crc32cTable[6][(crc >> 8) & 0xff] ^
            crc32cTable[5][(crc >> 16) & 0xff] ^ crc32cTable[4][(crc >> 24) & 0xff] ^
            crc32cTable[3][(crc >> 32) & 0xff] ^ crc32cTable[2][(crc >> 40) & 0xff] ^
            crc32cTable[1][(crc >> 48) & 0xff] ^ crc32cTable[0][crc >> 56];
        next += 8;
        len -= 8;
    }
    while (len) {
        crc = crc32cTable[0][(crc ^ *next++) & 0xff] ^ (crc >> 8);
        len--;
    }
    return static_cast<std::uint32_t>(crc) ^ 0xffffffff;
}

// hash_motion.c:78-82 get_identity_hash_value. The four input values add up
// to 32 bits; pack them as-is.
std::uint32_t getIdentityHashValue(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
    return (static_cast<std::uint32_t>(a) << 24) + (static_cast<std::uint32_t>(b) << 16) +
        (static_cast<std::uint32_t>(c) << 8) + static_cast<std::uint32_t>(d);
}

// hash_motion.c:84-93 get_xor_hash_value_hbd. Pack the lower 8 bits of
// each input value to the 32-bit output, then xor with the upper 8 bits
// of each input value.
std::uint32_t getXorHashValueHbd(std::uint16_t a, std::uint16_t b, std::uint16_t c, std::uint16_t d) {
    std::uint32_t result = (static_cast<std::uint32_t>(a & 0x00ff) << 24) +
        (static_cast<std::uint32_t>(b & 0x00ff) << 16) + (static_cast<std::uint32_t>(c & 0x00ff) << 8) +
        static_cast<std::uint32_t>(d & 0x00ff);
    result ^= (static_cast<std::uint32_t>(a & 0xff00) << 16) + (static_cast<std::uint32_t>(b & 0xff00) << 8) +
        static_cast<std::uint32_t>(c & 0xff00) + (static_cast<std::uint32_t>(d & 0xff00) >> 8);
    return result;
}

// hash_motion.c:59-76 hash_block_size_to_index. The hash value (hash_value1)
// consists of two parts: the first 3 bits relate to the block size and the
// remaining 16 bits are the crc values. This function gets the first 3 bits.
int hashBlockSizeToIndex(int blockSize) {
    switch (blockSize) {
    case 4: return 0;
    case 8: return 1;
    case 16: return 2;
    case 32: return 3;
    case 64: return 4;
    case 128: return 5;
    default: return -1;
    }
}

// hash_motion.c:34-43 get_pixels_in_1d_char_array_by_block_2x2. Row-major
// 2x2 gather: row 0 at (0,0),(0,1); row 1 at (1,0),(1,1) via the stride.
void getPixels2x2(const std::uint8_t* ySrc, int stride, std::uint8_t* out1D) {
    const std::uint8_t* pPel = ySrc;
    int index = 0;
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2; j++) {
                out1D[index++] = pPel[j];
            }
            pPel += stride;
        }
}

// hash_motion.c:309-385 svt_av1_get_block_hash_value (8-bit luma path).
// Stage 1: 2x2 identity hashes into buffer[0] at (y>>1)*subW + (x>>1).
// Stage 2: fold pairs of adjacent subhashes through crc32c (the 16-byte
// little-endian pack of four u32) until one value remains. The fold loop's
// src/dst buffers alternate each level, exactly as the reference's
// src_idx/dst_idx swap does.
void getBlockHashValue(const std::uint8_t* ySrc, int stride, int blockSize,
                       std::uint32_t* hashValue1, std::uint32_t* hashValue2) {
    const int addValue = hashBlockSizeToIndex(blockSize) << 16;
    const std::uint32_t crcMask = (1u << 16) - 1;

    constexpr int kBufferSize = 4096;  // hash.h:30 AOM_BUFFER_SIZE_FOR_BLOCK_HASH
    std::uint32_t buffer[2][kBufferSize];

    int subWidth = blockSize >> 1;
    for (int y = 0; y < blockSize; y += 2) {
        for (int x = 0; x < blockSize; x += 2) {
            std::uint8_t pixels[4];
            getPixels2x2(ySrc + y * stride + x, stride, pixels);
            buffer[0][(y >> 1) * subWidth + (x >> 1)] =
                getIdentityHashValue(pixels[0], pixels[1], pixels[2], pixels[3]);
        }
    }

    int srcIdx = 0;
    std::uint32_t toHash[4];
    for (int sub = 4; sub <= blockSize; sub *= 2, srcIdx ^= 1) {
        const int dstIdx = 1 - srcIdx;
        const int dstSub = subWidth >> 1;
        int dstPos = 0;
        for (int y = 0; y < dstSub; ++y) {
            for (int x = 0; x < dstSub; ++x) {
                const int srcPos = (y << 1) * subWidth + (x << 1);
                toHash[0] = buffer[srcIdx][srcPos];
                toHash[1] = buffer[srcIdx][srcPos + 1];
                toHash[2] = buffer[srcIdx][srcPos + subWidth];
                toHash[3] = buffer[srcIdx][srcPos + subWidth + 1];
                buffer[dstIdx][dstPos++] = getCrc32cValue(
                    reinterpret_cast<const std::uint8_t*>(toHash), sizeof(toHash));
            }
        }
        subWidth = dstSub;
    }

    *hashValue1 = (buffer[srcIdx][0] & crcMask) + static_cast<std::uint32_t>(addValue);
    *hashValue2 = buffer[srcIdx][0];
}

// hash_motion.c:153-216 svt_av1_generate_block_2x2_hash_value (8-bit luma
// path; the HBD xor path is a named park until its test exists). The pos
// state machine: pos++ per x_pos, then += width-1 (=1 here) per row, i.e.
// pos = y_pos * picWidth + x_pos.
void generateBlock2x2HashValue(const std::uint8_t* pic, int picWidth, int picHeight,
                               int stride, std::uint32_t* hash) {
    const int width = 2;
    const int height = 2;
    const int xEnd = picWidth - width + 1;
    const int yEnd = picHeight - height + 1;
    int pos = 0;
    for (int yPos = 0; yPos < yEnd; yPos++) {
        for (int xPos = 0; xPos < xEnd; xPos++) {
            std::uint8_t pixels[4];
            getPixels2x2(pic + yPos * stride + xPos, stride, pixels);
            hash[pos] = getIdentityHashValue(pixels[0], pixels[1], pixels[2], pixels[3]);
            pos++;
        }
        pos += width - 1;
    }
}

// hash_motion.c:192-216 svt_av1_generate_block_hash_value. pos advances
// xEnd times per row then += blockSize-1, i.e. pos = yPos*picWidth + xPos.
void generateBlockHashValue(int picWidth, int picHeight, int blockSize,
                            const std::uint32_t* srcHash, std::uint32_t* dstHash) {
    const int xEnd = picWidth - blockSize + 1;
    const int yEnd = picHeight - blockSize + 1;
    const int srcSize = blockSize >> 1;
    std::uint32_t p[4];
    int pos = 0;
    for (int yPos = 0; yPos < yEnd; yPos++) {
        for (int xPos = 0; xPos < xEnd; xPos++) {
            p[0] = srcHash[pos];
            p[1] = srcHash[pos + srcSize];
            p[2] = srcHash[pos + srcSize * picWidth];
            p[3] = srcHash[pos + srcSize * picWidth + srcSize];
            dstHash[pos] = getCrc32cValue(reinterpret_cast<const std::uint8_t*>(p), sizeof(p));
            pos++;
        }
        pos += blockSize - 1;
    }
}

// hash_motion.c:218-307 the hierarchical walk. State machine: phase at
// step: (0,0) -> (step/2,0) -> (0,step/2) -> (step/2,step/2) -> halve.
// x-outer, y-inner loops.
bool hashWalkAddToHashMap(int picWidth, int picHeight, int blockSize,
                          const std::uint32_t* picHash,
                          const std::function<bool(int, int, std::uint32_t, std::uint32_t)>& visit) {
    const int xEnd = picWidth - blockSize + 1;
    const int yEnd = picHeight - blockSize + 1;
    const int addValue = hashBlockSizeToIndex(blockSize) << 16;
    const int crcMask = (1 << 16) - 1;
    int step = blockSize;
    int xOffset = 0, yOffset = 0;
    while (step > 1) {
        for (int xPos = xOffset; xPos < xEnd; xPos += step) {
            for (int yPos = yOffset; yPos < yEnd; yPos += step) {
                const int pos = yPos * picWidth + xPos;
                const std::uint32_t hashValue1 =
                    (picHash[pos] & static_cast<std::uint32_t>(crcMask)) + static_cast<std::uint32_t>(addValue);
                if (!visit(xPos, yPos, hashValue1, picHash[pos])) return false;
            }
        }
        if (xOffset == 0 && yOffset == 0) {
            xOffset = step / 2;
        } else if (xOffset == step / 2 && yOffset == 0) {
            xOffset = 0;
            yOffset = step / 2;
        } else if (xOffset == 0 && yOffset == step / 2) {
            xOffset = step / 2;
        } else {
            step /= 2;
            xOffset = step / 2;
            yOffset = 0;
        }
    }
    return true;
}

// hash_motion.c:20-31 clear; :95-113 create; :115-138 add-to-table with
// the bucket cap (hash_motion.c:130 - ignore entries beyond
// max_cand_per_bucket); :140-148 count.
void hashTableCreate(HashTable* t) {
    t->buckets.assign(static_cast<std::size_t>(kHashMaxAddr), {});
}
void hashTableAddToTable(HashTable* t, std::uint32_t hashValue, const BlockHash& bh,
                         std::uint16_t maxCandPerBucket) {
    auto& bucket = t->buckets[hashValue];
    if (bucket.size() >= maxCandPerBucket) return;
    bucket.push_back(bh);
}
int hashTableCount(const HashTable* t, std::uint32_t hashValue) {
    return static_cast<int>(t->buckets[hashValue].size());
}
void hashTableClearAll(HashTable* t) {
    for (auto& bucket : t->buckets) bucket.clear();
}
void hashTableDestroy(HashTable* t) {
    t->buckets.clear();
}

}  // namespace motion
