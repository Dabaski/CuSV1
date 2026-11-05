#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>

#include "palette.h"

namespace l13_palette {

namespace {

// get_msb, portable #else body (definitions.h:628-643): floor(log2(n)),
// n != 0. l7_entropy.cpp:23 / l10_filters cdef.cpp:12 precedent (the
// per-module local copy).
std::int32_t getMsb(std::uint32_t n) {
    std::int32_t log = 0;
    std::uint32_t value = n;
    assert(n != 0);
    for (std::int32_t i = 4; i >= 0; --i) {
        const std::int32_t shift = (1 << i);
        const std::uint32_t x = value >> shift;
        if (x != 0) {
            value = x;
            log += shift;
        }
    }
    return log;
}

// av1_ceil_log2 (cabac_context_model.h:366-371) with svt_log2f = get_msb
// (definitions.h:592).
std::int32_t ceilLog2(std::int32_t n) {
    if (n < 2) {
        return 0;
    }
    return getMsb(static_cast<std::uint32_t>(n - 1)) + 1;
}

} // namespace

// int_comparer (palette.c:62-66).
int intComparer(const void* a, const void* b) {
    return (*(int*)a - *(int*)b);
}

// av1_remove_duplicates (palette.c:66-78).
int removeDuplicates(int* centroids, int numCentroids) {
    int num_unique; // number of unique centroids
    int i;
    qsort(centroids, numCentroids, sizeof(*centroids), intComparer);
    // Remove duplicates.
    num_unique = 1;
    for (i = 1; i < numCentroids; ++i) {
        if (centroids[i] != centroids[i - 1]) { // found a new unique centroid
            centroids[num_unique++] = centroids[i];
        }
    }
    return num_unique;
}

// delta_encode_cost (palette.c:80-109).
int deltaEncodeCost(const int* colors, int num, int bitDepth, int minVal) {
    if (num <= 0) {
        return 0;
    }
    int bits_cost = bitDepth;
    if (num == 1) {
        return bits_cost;
    }
    bits_cost += 2;
    int max_delta = 0;
    int deltas[paletteMaxSize];
    const int min_bits = bitDepth - 3;
    for (int i = 1; i < num; ++i) {
        const int delta = colors[i] - colors[i - 1];
        deltas[i - 1] = delta;
        assert(delta >= minVal);
        if (delta > max_delta) {
            max_delta = delta;
        }
    }
    int bits_per_delta = std::max(ceilLog2(max_delta + 1 - minVal), min_bits);
    assert(bits_per_delta <= bitDepth);
    int range = (1 << bitDepth) - colors[0] - minVal;
    for (int i = 0; i < num - 1; ++i) {
        bits_cost += bits_per_delta;
        range -= deltas[i];
        bits_per_delta = std::min(bits_per_delta, ceilLog2(range));
    }
    return bits_cost;
}

// svt_av1_index_color_cache (palette.c:111-141).
int indexColorCache(const std::uint16_t* colorCache, int nCache, const std::uint16_t* colors, int nColors,
                    std::uint8_t* cacheColorFound, int* outCacheColors) {
    if (nCache <= 0) {
        for (int i = 0; i < nColors; ++i) {
            outCacheColors[i] = colors[i];
        }
        return nColors;
    }
    std::memset(cacheColorFound, 0, nCache * sizeof(*cacheColorFound));
    int n_in_cache = 0;
    int in_cache_flags[paletteMaxSize];
    std::memset(in_cache_flags, 0, sizeof(in_cache_flags));
    for (int i = 0; i < nCache && n_in_cache < nColors; ++i) {
        for (int j = 0; j < nColors; ++j) {
            if (colors[j] == colorCache[i]) {
                in_cache_flags[j] = 1;
                cacheColorFound[i] = 1;
                ++n_in_cache;
                break;
            }
        }
    }
    int j = 0;
    for (int i = 0; i < nColors; ++i) {
        if (!in_cache_flags[i]) {
            outCacheColors[j++] = colors[i];
        }
    }
    assert(j == nColors - n_in_cache);
    return j;
}

// palette_add_to_cache (palette.c:154-161).
void paletteAddToCache(std::uint16_t* cache, int* n, std::uint16_t val) {
    // Do not add an already existing value
    if (*n > 0 && val == cache[*n - 1]) {
        return;
    }
    cache[(*n)++] = val;
}

// svt_av1_palette_color_cost_y (palette.c:143-152). The PaletteModeInfo
// struct wall is threaded as the paletteColors parameter (named deviation).
int paletteColorCostY(const std::uint16_t* paletteColors, std::uint16_t* colorCache, int paletteSize, int nCache,
                      int bitDepth) {
    const int n = paletteSize;
    int out_cache_colors[paletteMaxSize];
    std::uint8_t cache_color_found[2 * paletteMaxSize];
    const int n_out_cache =
        indexColorCache(colorCache, nCache, paletteColors, n, cache_color_found, out_cache_colors);
    const int total_bits = nCache + deltaEncodeCost(out_cache_colors, n_out_cache, bitDepth, 1);
    return av1CostLiteral(total_bits);
}

// svt_av1_count_colors (pic_analysis_process.c:892-909).
int countColors(const std::uint8_t* src, int stride, int rows, int cols, int* valCount) {
    const int max_pix_val = 1 << 8;
    std::memset(valCount, 0, max_pix_val * sizeof(valCount[0]));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int this_val = src[r * stride + c];
            assert(this_val < max_pix_val);
            ++valCount[this_val];
        }
    }
    int n = 0;
    for (int i = 0; i < max_pix_val; ++i) {
        if (valCount[i]) {
            ++n;
        }
    }
    return n;
}

namespace {

// calc_dist, the AV1_K_MEANS_DIM = 1 instantiation (k_means_template.h:
// 25-31): the single squared difference.
int calcDist1(const int* p1, const int* p2) {
    int dist = 0;
    dist += (p1[0] - p2[0]) * (p1[0] - p2[0]); // SQR (utility.h:107)
    return dist;
}

} // namespace

// svt_av1_calc_indices_dim1 (k_means_template.h:33-45, DIM=1).
void calcIndicesDim1(const int* data, const int* centroids, std::uint8_t* indices, int n, int k) {
    for (int i = 0; i < n; ++i) {
        int min_dist = calcDist1(data + i, centroids);
        indices[i] = 0;
        for (int j = 1; j < k; ++j) {
            const int this_dist = calcDist1(data + i, centroids + j);
            if (this_dist < min_dist) {
                min_dist = this_dist;
                indices[i] = static_cast<std::uint8_t>(j); // k <= 8, no loss
            }
        }
    }
}

namespace {

// calc_dist, the AV1_K_MEANS_DIM = 2 instantiation (k_means_template.h:
// 25-31): the two squared differences summed.
int calcDist2(const int* p1, const int* p2) {
    int dist = 0;
    dist += (p1[0] - p2[0]) * (p1[0] - p2[0]); // SQR (utility.h:107)
    dist += (p1[1] - p2[1]) * (p1[1] - p2[1]);
    return dist;
}

} // namespace

// svt_av1_calc_indices_dim2 (k_means_template.h:33-45, DIM=2).
void calcIndicesDim2(const int* data, const int* centroids, std::uint8_t* indices, int n, int k) {
    for (int i = 0; i < n; ++i) {
        int min_dist = calcDist2(data + i * 2, centroids);
        indices[i] = 0;
        for (int j = 1; j < k; ++j) {
            const int this_dist = calcDist2(data + i * 2, centroids + j * 2);
            if (this_dist < min_dist) {
                min_dist = this_dist;
                indices[i] = static_cast<std::uint8_t>(j); // k <= 8, no loss
            }
        }
    }
}

namespace {

// lcg_next (random.h:23-26).
std::uint32_t lcgNext(std::uint32_t* state) {
    *state = static_cast<std::uint32_t>(*state * 1103515245ULL + 12345);
    return *state;
}

// lcg_rand16 (random.h:29-31).
std::uint32_t lcgRand16(std::uint32_t* state) {
    return (lcgNext(state) / 65536) % 32768;
}

// calc_centroids, the AV1_K_MEANS_DIM = 1 instantiation (k_means_template.h:
// 47-74). Empty clusters take data[lcg_rand16(&rand_state) % n] with
// rand_state seeded from data[0] EVERY call (:50).
void calcCentroidsDim1(const int* data, int* centroids, const std::uint8_t* indices, int n, int k) {
    int count[paletteMaxSize] = {0};
    std::uint32_t rand_state = static_cast<std::uint32_t>(data[0]);
    assert(n <= 32768);
    std::memset(centroids, 0, sizeof(centroids[0]) * k);
    for (int i = 0; i < n; ++i) {
        const int index = indices[i];
        assert(index < k);
        ++count[index];
        centroids[index] += data[i];
    }
    for (int i = 0; i < k; ++i) {
        if (count[i] == 0) {
            centroids[i] = data[lcgRand16(&rand_state) % n];
        } else {
            centroids[i] = (centroids[i] + (count[i] >> 1)) / count[i]; // DIVIDE_AND_ROUND (palette.c:18)
        }
    }
}

// calc_total_dist, the AV1_K_MEANS_DIM = 1 instantiation (k_means_template.h:
// 76-84).
std::int64_t calcTotalDistDim1(const int* data, const int* centroids, const std::uint8_t* indices, int n, int k) {
    std::int64_t dist = 0;
    (void)k;
    for (int i = 0; i < n; ++i) {
        dist += calcDist1(data + i, centroids + indices[i]);
    }
    return dist;
}

} // namespace

// svt_av1_k_means_dim1 (k_means_template.h:86-111, DIM=1).
void kMeansDim1(const int* data, int* centroids, std::uint8_t* indices, int n, int k, int maxItr) {
    int pre_centroids[2 * paletteMaxSize];
    std::uint8_t pre_indices[maxSbSquare];

    calcIndicesDim1(data, centroids, indices, n, k);
    std::int64_t this_dist = calcTotalDistDim1(data, centroids, indices, n, k);

    for (int i = 0; i < maxItr; ++i) {
        const std::int64_t pre_dist = this_dist;
        std::memcpy(pre_centroids, centroids, sizeof(pre_centroids[0]) * k);
        std::memcpy(pre_indices, indices, sizeof(pre_indices[0]) * n);

        calcCentroidsDim1(data, centroids, indices, n, k);
        calcIndicesDim1(data, centroids, indices, n, k);
        this_dist = calcTotalDistDim1(data, centroids, indices, n, k);

        if (this_dist > pre_dist) {
            std::memcpy(centroids, pre_centroids, sizeof(pre_centroids[0]) * k);
            std::memcpy(indices, pre_indices, sizeof(pre_indices[0]) * n);
            break;
        }
        if (!std::memcmp(centroids, pre_centroids, sizeof(pre_centroids[0]) * k)) {
            break;
        }
    }
}

namespace {

// calc_centroids, the AV1_K_MEANS_DIM = 2 instantiation (k_means_template.h:
// 47-74).
void calcCentroidsDim2(const int* data, int* centroids, const std::uint8_t* indices, int n, int k) {
    int count[paletteMaxSize] = {0};
    std::uint32_t rand_state = static_cast<std::uint32_t>(data[0]);
    assert(n <= 32768);
    std::memset(centroids, 0, sizeof(centroids[0]) * k * 2);
    for (int i = 0; i < n; ++i) {
        const int index = indices[i];
        assert(index < k);
        ++count[index];
        centroids[index * 2] += data[i * 2];
        centroids[index * 2 + 1] += data[i * 2 + 1];
    }
    for (int i = 0; i < k; ++i) {
        if (count[i] == 0) {
            std::memcpy(centroids + i * 2, data + (lcgRand16(&rand_state) % n) * 2, sizeof(centroids[0]) * 2);
        } else {
            for (int j = 0; j < 2; ++j) {
                centroids[i * 2 + j] = (centroids[i * 2 + j] + (count[i] >> 1)) / count[i];
            }
        }
    }
}

// calc_total_dist, the AV1_K_MEANS_DIM = 2 instantiation (k_means_template.h:
// 76-84).
std::int64_t calcTotalDistDim2(const int* data, const int* centroids, const std::uint8_t* indices, int n, int k) {
    std::int64_t dist = 0;
    (void)k;
    for (int i = 0; i < n; ++i) {
        dist += calcDist2(data + i * 2, centroids + indices[i] * 2);
    }
    return dist;
}

} // namespace

// svt_av1_k_means_dim2 (k_means_template.h:86-111, DIM=2).
void kMeansDim2(const int* data, int* centroids, std::uint8_t* indices, int n, int k, int maxItr) {
    int pre_centroids[2 * paletteMaxSize];
    std::uint8_t pre_indices[maxSbSquare];

    calcIndicesDim2(data, centroids, indices, n, k);
    std::int64_t this_dist = calcTotalDistDim2(data, centroids, indices, n, k);

    for (int i = 0; i < maxItr; ++i) {
        const std::int64_t pre_dist = this_dist;
        std::memcpy(pre_centroids, centroids, sizeof(pre_centroids[0]) * k * 2);
        std::memcpy(pre_indices, indices, sizeof(pre_indices[0]) * n);

        calcCentroidsDim2(data, centroids, indices, n, k);
        calcIndicesDim2(data, centroids, indices, n, k);
        this_dist = calcTotalDistDim2(data, centroids, indices, n, k);

        if (this_dist > pre_dist) {
            std::memcpy(centroids, pre_centroids, sizeof(pre_centroids[0]) * k * 2);
            std::memcpy(indices, pre_indices, sizeof(pre_indices[0]) * n);
            break;
        }
        if (!std::memcmp(centroids, pre_centroids, sizeof(pre_centroids[0]) * k * 2)) {
            break;
        }
    }
}

// av1_calc_indices (palette.c:31-39).
void calcIndices(const int* data, const int* centroids, std::uint8_t* indices, int n, int k, int dim) {
    if (dim == 1) {
        calcIndicesDim1(data, centroids, indices, n, k);
    } else if (dim == 2) {
        calcIndicesDim2(data, centroids, indices, n, k);
    } else {
        assert(0 && "Untemplated k means dimension");
    }
}

// av1_k_means (palette.c:45-53).
void kMeans(const int* data, int* centroids, std::uint8_t* indices, int n, int k, int dim, int maxItr) {
    if (dim == 1) {
        kMeansDim1(data, centroids, indices, n, k, maxItr);
    } else if (dim == 2) {
        kMeansDim2(data, centroids, indices, n, k, maxItr);
    } else {
        assert(0 && "Untemplated k means dimension");
    }
}

// optimize_palette_colors (palette.c:250-270).
void optimizePaletteColors(std::uint16_t* colorCache, int nCache, int nColors, int stride, int* centroids,
                           int bitDepth, std::uint8_t qpIndex) {
    if (nCache <= 0) {
        return;
    }
    for (int i = 0; i < nColors * stride; i += stride) {
        int min_diff = abs(static_cast<int>(centroids[i]) - static_cast<int>(colorCache[0]));
        int idx = 0;
        for (int j = 1; j < nCache; ++j) {
            const int this_diff = abs(static_cast<int>(centroids[i]) - static_cast<int>(colorCache[j]));
            if (this_diff < min_diff) {
                min_diff = this_diff;
                idx = j;
            }
        }
        const int min_threshold = (6 + (qpIndex >> 6)) << (bitDepth - 8);
        if (min_diff <= min_threshold) {
            centroids[i] = colorCache[idx];
        }
    }
}

// extend_palette_color_map (palette.c:275-294).
void extendPaletteColorMap(std::uint8_t* colorMap, int origWidth, int origHeight, int newWidth, int newHeight) {
    int j;
    assert(newWidth >= origWidth);
    assert(newHeight >= origHeight);
    if (newWidth == origWidth && newHeight == origHeight) {
        return;
    }
    for (j = origHeight - 1; j >= 0; --j) {
        std::memmove(colorMap + j * newWidth, colorMap + j * origWidth, origWidth);
        // Copy last column to extra columns.
        std::memset(colorMap + j * newWidth + origWidth, colorMap[j * newWidth + origWidth - 1],
                    newWidth - origWidth);
    }
    // Copy last row to extra rows.
    for (j = origHeight; j < newHeight; ++j) {
        std::memcpy(colorMap + j * newWidth, colorMap + (origHeight - 1) * newWidth, newWidth);
    }
}

// cache_based_centroid_refinement (palette.c:330-386).
void cacheBasedCentroidRefinement(int* data, int rows, int cols, int n, int* centroids, std::uint8_t* colorIdxMap,
                                  std::uint16_t* colorCache, int nCache) {
    const int total = rows * cols;
    std::uint8_t temp_map[maxSbSquare];

    // Compute baseline SSE
    std::uint64_t baseline_sse = 0;
    for (int i = 0; i < total; i++) {
        int diff = data[i] - centroids[colorIdxMap[i]];
        baseline_sse += static_cast<std::uint64_t>(diff) * diff;
    }

    for (int c = 0; c < n; c++) {
        int original = centroids[c];
        int best_val = original;
        std::uint64_t best_sse = baseline_sse;

        for (int k = 0; k < nCache; k++) {
            int candidate = colorCache[k];
            if (candidate == original) {
                continue;
            }
            centroids[c] = candidate;

            // Reassign pixels
            for (int i = 0; i < total; i++) {
                int best_idx = 0;
                int best_dist = abs(data[i] - centroids[0]);
                for (int j = 1; j < n; j++) {
                    int dist = abs(data[i] - centroids[j]);
                    if (dist < best_dist) {
                        best_dist = dist;
                        best_idx = j;
                    }
                }
                temp_map[i] = static_cast<std::uint8_t>(best_idx);
            }

            // Compute SSE
            std::uint64_t sse = 0;
            for (int i = 0; i < total; i++) {
                int diff = data[i] - centroids[temp_map[i]];
                sse += static_cast<std::uint64_t>(diff) * diff;
            }

            if (sse < best_sse) {
                best_sse = sse;
                best_val = candidate;
                std::memcpy(colorIdxMap, temp_map, total);
            }
        }

        centroids[c] = best_val;
    }
}

// svt_aom_palette_color_index_context_lookup (palette.c:641). Negative
// values are invalid (palette.c:640 comment).
const std::int32_t paletteColorIndexContextLookup[maxColorContextHash + 1] = {-1, -1, 0, -1, -1, 4, 3, 2, 1};

// av1_fast_palette_color_index_context_on_edge (palette.c:645-676).
int fastPaletteColorIndexContextOnEdge(const std::uint8_t* colorMap, int stride, int r, int c, int* colorIdx) {
    const bool has_left = (c - 1 >= 0);
    const bool has_above = (r - 1 >= 0);
    assert(r > 0 || c > 0);
    assert(has_above ^ has_left);
    assert(colorIdx);
    (void)has_left;

    const std::uint8_t color_neighbor = has_above ? colorMap[(r - 1) * stride + (c - 0)]
                                                  : colorMap[(r - 0) * stride + (c - 1)];
    // If the neighbor color has higher index than current color index, then we
    // move up by 1.
    *colorIdx = colorMap[r * stride + c];
    const std::uint8_t current_color = static_cast<std::uint8_t>(*colorIdx);
    if (color_neighbor > current_color) {
        (*colorIdx)++;
    } else if (color_neighbor == current_color) {
        *colorIdx = 0;
    }

    // Get hash value of context.
    // The non-diagonal neighbors get a weight of 2.
    const std::uint8_t color_score = 2;
    const std::uint8_t hash_multiplier = 1;
    const std::uint8_t color_index_ctx_hash =
        static_cast<std::uint8_t>(color_score * hash_multiplier);

    // Lookup context from hash.
    const int color_index_ctx = paletteColorIndexContextLookup[color_index_ctx_hash];
    assert(color_index_ctx == 0);
    (void)color_index_ctx;
    return 0;
}

namespace {

// NUM_PALETTE_NEIGHBORS (palette.c:642): left, top-left and top.
constexpr std::uint8_t numPaletteNeighbors = 3;
// INVALID_COLOR_IDX (palette.c:643).
constexpr std::uint8_t invalidColorIdx = 255;

} // namespace

// av1_fast_palette_color_index_context (palette.c:680-776).
int fastPaletteColorIndexContext(const std::uint8_t* colorMap, int stride, int r, int c, int* colorIdx) {
    assert(r > 0 || c > 0);

    const bool has_above = (r - 1 >= 0);
    const bool has_left = (c - 1 >= 0);
    assert(has_above || has_left);
    if (has_above ^ has_left) {
        return fastPaletteColorIndexContextOnEdge(colorMap, stride, r, c, colorIdx);
    }

    // This goes in the order of left, top, and top-left. This has the advantage
    // that unless anything here are not distinct or invalid, this will already
    // be in sorted order. Furthermore, if either of the first two is
    // invalid, we know the last one is also invalid.
    std::uint8_t color_neighbors[numPaletteNeighbors];
    color_neighbors[0] = colorMap[(r - 0) * stride + (c - 1)];
    color_neighbors[1] = colorMap[(r - 1) * stride + (c - 0)];
    color_neighbors[2] = colorMap[(r - 1) * stride + (c - 1)];

    // Aggregate duplicated values.
    // Since our array is so small, using a couple if statements is faster
    std::uint8_t scores[numPaletteNeighbors] = {2, 2, 1};
    std::uint8_t num_invalid_colors = 0;
    if (color_neighbors[0] == color_neighbors[1]) {
        scores[0] = static_cast<std::uint8_t>(scores[0] + scores[1]);
        color_neighbors[1] = invalidColorIdx;
        num_invalid_colors += 1;

        if (color_neighbors[0] == color_neighbors[2]) {
            scores[0] = static_cast<std::uint8_t>(scores[0] + scores[2]);
            num_invalid_colors += 1;
        }
    } else if (color_neighbors[0] == color_neighbors[2]) {
        scores[0] = static_cast<std::uint8_t>(scores[0] + scores[2]);
        num_invalid_colors += 1;
    } else if (color_neighbors[1] == color_neighbors[2]) {
        scores[1] = static_cast<std::uint8_t>(scores[1] + scores[2]);
        num_invalid_colors += 1;
    }

    const std::uint8_t num_valid_colors = static_cast<std::uint8_t>(numPaletteNeighbors - num_invalid_colors);

    std::uint8_t* color_rank = color_neighbors;
    std::uint8_t* score_rank = scores;

    // Sort everything
    if (num_valid_colors > 1) {
        if (color_neighbors[1] == invalidColorIdx) {
            scores[1] = scores[2];
            color_neighbors[1] = color_neighbors[2];
        }

        // We need to swap the first two elements if they have the same score but
        // the color indices are not in the right order
        if (score_rank[0] < score_rank[1] || (score_rank[0] == score_rank[1] && color_rank[0] > color_rank[1])) {
            const std::uint8_t tmp_score = score_rank[0];
            const std::uint8_t tmp_color = color_rank[0];
            score_rank[0] = score_rank[1];
            color_rank[0] = color_rank[1];
            score_rank[1] = tmp_score;
            color_rank[1] = tmp_color;
        }
        if (num_valid_colors > 2) {
            if (score_rank[0] < score_rank[2]) {
                const std::uint8_t tmp_score = score_rank[0];
                const std::uint8_t tmp_color = color_rank[0];
                score_rank[0] = score_rank[2];
                color_rank[0] = color_rank[2];
                score_rank[2] = tmp_score;
                color_rank[2] = tmp_color;
            }
            if (score_rank[1] < score_rank[2]) {
                const std::uint8_t tmp_score = score_rank[1];
                const std::uint8_t tmp_color = color_rank[1];
                score_rank[1] = score_rank[2];
                color_rank[1] = color_rank[2];
                score_rank[2] = tmp_score;
                color_rank[2] = tmp_color;
            }
        }
    }

    // If any of the neighbor colors has higher index than current color index,
    // then we move up by 1 unless the current color is the same as one of the
    // neighbors.
    *colorIdx = colorMap[r * stride + c];
    const std::uint8_t current_color = static_cast<std::uint8_t>(*colorIdx);
    for (int idx = 0; idx < num_valid_colors; idx++) {
        if (color_rank[idx] > current_color) {
            (*colorIdx)++;
        } else if (color_rank[idx] == current_color) {
            *colorIdx = idx;
            break;
        }
    }

    // Get hash value of context.
    std::uint8_t color_index_ctx_hash = 0;
    static const std::uint8_t hash_multipliers[numPaletteNeighbors] = {1, 2, 2};
    for (int idx = 0; idx < num_valid_colors; ++idx) {
        color_index_ctx_hash = static_cast<std::uint8_t>(color_index_ctx_hash + score_rank[idx] * hash_multipliers[idx]);
    }
    assert(color_index_ctx_hash > 0);
    assert(color_index_ctx_hash <= maxColorContextHash);

    // Lookup context from hash.
    const int color_index_ctx = 9 - color_index_ctx_hash;
    assert(color_index_ctx == paletteColorIndexContextLookup[color_index_ctx_hash]);
    assert(color_index_ctx >= 0);
    assert(color_index_ctx < 5); // PALETTE_COLOR_INDEX_CONTEXTS (cabac_context_model.h:238)
    return color_index_ctx;
}

} // namespace l13_palette
