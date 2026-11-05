#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace motion {

// hash.c:28 svt_av1_crc32c_table_init - builds the process-wide
// crc32c_table[8][256] (hash.c:25). Must run before any getCrc32cValue call.
void crc32cInit();

// hash.c:55 svt_av1_get_crc32c_value_c - the table-driven software CRC-32C
// fall-back kernel. Byte head (until 8-aligned), quadword body, byte tail,
// final 0xffffffff xor.
std::uint32_t getCrc32cValue(const std::uint8_t* buf, std::size_t len);

// hash_motion.c:78-82 get_identity_hash_value - the four 8-bit inputs pack
// as-is into a 32-bit value.
std::uint32_t getIdentityHashValue(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d);

// hash_motion.c:84-93 get_xor_hash_value_hbd - HBD 2x2 hash: pack the
// lower 8 bits of each input at byte positions 3..0, then xor the upper
// 8 bits folded to the same positions.
std::uint32_t getXorHashValueHbd(std::uint16_t a, std::uint16_t b, std::uint16_t c, std::uint16_t d);

// hash_motion.c:59-76 hash_block_size_to_index - the first 3 bits of
// hash_value1 encode the block size: 4->0 ... 128->5, default -1.
int hashBlockSizeToIndex(int blockSize);

// hash_motion.c:34-43 get_pixels_in_1d_char_array_by_block_2x2 - gather the
// 2x2 block row-major into a 4-entry 1D array: p[0]=(0,0), p[1]=(0,1),
// p[2]=(1,0), p[3]=(1,1).
void getPixels2x2(const std::uint8_t* ySrc, int stride, std::uint8_t* out1D);

// hash_motion.c:309-385 svt_av1_get_block_hash_value - the 2x2 identity
// hashes folded up through crc32c to the block hash. hash_value1 =
// (final & 0xffff) + (size_index << 16); hash_value2 = the final value.
// Named deviation: the reference stages intermediates in
// IntraBcContext::hash_value_buffer; this port uses a local buffer, and
// the 8-bit luma path only (the HBD flag arrives with its own test).
void getBlockHashValue(const std::uint8_t* ySrc, int stride, int blockSize,
                       std::uint32_t* hashValue1, std::uint32_t* hashValue2);

// hash_motion.c:153-216 svt_av1_generate_block_2x2_hash_value, de-walled
// (named deviation: plain buf/width/height/stride in place of the
// Yv12BufferConfig picture struct). Grid (width-1)x(height-1), array
// stride = picWidth. 8-bit luma path (identity hash).
void generateBlock2x2HashValue(const std::uint8_t* pic, int picWidth, int picHeight,
                               int stride, std::uint32_t* hash);

// hash_motion.c:192-216 svt_av1_generate_block_hash_value, de-walled
// (named deviation: picWidth/picHeight/blockSize in place of the picture
// struct). dst[pos] = crc32c of the LE-packed 4 u32 quad {src[pos],
// src[pos+half], src[pos+half*picWidth], src[pos+half*picWidth+half]}.
void generateBlockHashValue(int picWidth, int picHeight, int blockSize,
                            const std::uint32_t* srcHash, std::uint32_t* dstHash);

// hash_motion.c:218-307 svt_aom_rtime_alloc_svt_av1_add_to_hash_map_by_row_
// with_precal_data, de-walled: the walk visits every block of the given
// size in the hierarchical state-machine order and hands each block's
// (x, y, hash_value1 = (hash & 0xffff) + (sizeIdx << 16), hash_value2 =
// hash) to the callback; returning false aborts. Named deviation: the
// visit callback is the test/production seam replacing the direct
// HashTable insert; the table machinery arrives as its own slice.
bool hashWalkAddToHashMap(int picWidth, int picHeight, int blockSize,
                          const std::uint32_t* picHash,
                          const std::function<bool(int, int, std::uint32_t, std::uint32_t)>& visit);

// hash_motion.h:27-35 BlockHash + HashTable, with the machinery of
// hash_motion.c:20-31 (clear), :95-113 (create), :115-138 (add-to-table),
// :140-148 (count). Named deviation: buckets are std::vector<BlockHash>
// in place of svt_aom_vector + EB_MALLOC; the bucket cap
// (hash_motion.c:130) ignores entries beyond max_cand_per_bucket.
struct BlockHash {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::uint32_t hashValue2 = 0;
};
struct HashTable {
    std::vector<std::vector<BlockHash>> buckets;
};
constexpr int kCrcBits = 16;           // hash_motion.c:17
constexpr int kBlockSizeBits = 3;      // hash_motion.c:18
constexpr int kHashMaxAddr = 1 << (kCrcBits + kBlockSizeBits);
void hashTableCreate(HashTable* t);
void hashTableAddToTable(HashTable* t, std::uint32_t hashValue, const BlockHash& bh,
                         std::uint16_t maxCandPerBucket);
int hashTableCount(const HashTable* t, std::uint32_t hashValue);
void hashTableClearAll(HashTable* t);
void hashTableDestroy(HashTable* t);
bool hashWalkAddToHashMap(int picWidth, int picHeight, int blockSize,
                          const std::uint32_t* picHash,
                          const std::function<bool(int, int, std::uint32_t, std::uint32_t)>& visit);

}  // namespace motion
