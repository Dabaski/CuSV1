// l13_palette - the palette + k-means kernel family (A12, PAL1).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/palette.c (837 physical,
// 752 non-blank) + k_means_template.h (114 physical, 99 non-blank).
// LANDED (every struct-free function of palette.c plus both template
// instantiations):
//   int_comparer :62-:66, av1_remove_duplicates :66-:78,
//   delta_encode_cost :80-:109 (with the av1_ceil_log2 helper twin,
//   cabac_context_model.h:366-371, svt_log2f = get_msb definitions.h:592,
//   portable get_msb body definitions.h:628-643),
//   svt_av1_index_color_cache :111-:141,
//   svt_av1_palette_color_cost_y :143-:152 (av1_cost_literal,
//   md_rate_estimation.h:31, AV1_PROB_COST_SHIFT :29 = 9),
//   palette_add_to_cache :154-:161, optimize_palette_colors :250-:270,
//   extend_palette_color_map :275-:294,
//   cache_based_centroid_refinement :330-:386,
//   svt_aom_palette_color_index_context_lookup :639-:641,
//   av1_fast_palette_color_index_context_on_edge :645-:676,
//   av1_fast_palette_color_index_context :680-:776, and
//   svt_av1_count_colors (pic_analysis_process.c:892-909, declared for
//   palette.c use at palette.c:327).
//   k_means_template.h as explicit instantiations: calc_dist :25-:31,
//   svt_av1_calc_indices :33-:45, calc_centroids :47-:74 (the
//   empty-cluster lcg_rand16 refill, random.h:23-31),
//   calc_total_dist :76-:84, svt_av1_k_means :86-111 - exported as
//   calcIndicesDim1/Dim2 + kMeansDim1/Dim2 with the palette.c dispatchers
//   av1_calc_indices :31-:39 and av1_k_means :45-53 as calcIndices/kMeans.
// PARKED (named, port nothing):
//   svt_get_palette_cache_y palette.c:164-:210 (MacroBlockD / MbModeInfo),
//   svt_aom_get_block_dimensions :217-:246 (MacroBlockD + block tables),
//   palette_rd_y :296-:325 (PaletteInfo / ModeDecisionContext),
//   search_palette_luma :388-:563 (PictureControlSet / MdContext),
//   Av1ColorMapParam + get_palette_params / get_color_map_params /
//   get_palette_params_rate / get_color_map_params_rate :565-:627
//   (FRAME_CONTEXT / EcBlkStruct / ModeDecisionCandidate /
//   MdRateEstimationContext),
//   cost_and_tokenize_map :781-:815, svt_av1_tokenize_color_map :817-:828,
//   svt_av1_cost_color_map :830-:837 (the Av1ColorMapParam wall), and
//   svt_av1_count_colors_highbd (pic_analysis_process.c:868-890, a
//   separate highbd body; the project's 8-bit discipline parks highbd,
//   l10_filters precedent).
// Deviations (all named): statics exported as plain functions
// (LR1/LR2 precedent); PaletteModeInfo walled, palette_colors threaded as
// a parameter; the lookup table pinned const (read-only in the
// reference); explicit narrowing casts where MSVC /WX flags the
// reference's implicit int->uint8_t conversions (k <= 8, no value loss);
// getMsb/ceilLog2 are per-module locals (l7/l10 precedent).
#pragma once

#include <cstdint>

namespace l13_palette {

// definitions.h:380 PALETTE_MAX_SIZE / :382 PALETTE_MIN_SIZE.
constexpr std::int32_t paletteMaxSize = 8;
constexpr std::int32_t paletteMinSize = 2;
// definitions.h:338-340: MAX_SB_SIZE_LOG2 7, MAX_SB_SIZE 128,
// MAX_SB_SQUARE 128*128.
constexpr std::int32_t maxSbSize = 1 << 7;
constexpr std::int32_t maxSbSquare = maxSbSize * maxSbSize;

// int_comparer (palette.c:62-66): qsort's int comparer, the subtraction
// verbatim (may overflow on extreme inputs - kept 1:1; palette callers
// pass pixel-range values).
int intComparer(const void* a, const void* b);

// av1_remove_duplicates (palette.c:66-78): qsort ascending via intComparer,
// then the compacting walk. Returns the unique count; centroids[0..count-1]
// hold the uniques. num_centroids == 0 returns 1 (the reference's
// unconditional num_unique = 1 seed - kept 1:1).
int removeDuplicates(int* centroids, int numCentroids);

// delta_encode_cost (palette.c:80-109): the delta-chain bit cost. num <= 0
// -> 0; num == 1 -> bit_depth; otherwise bit_depth + 2 + the shrinking
// bits-per-delta chain (see the reference loop). min_val is the smallest
// legal delta (the palette caller passes 1).
int deltaEncodeCost(const int* colors, int num, int bitDepth, int minVal);

// svt_av1_index_color_cache (palette.c:111-141): splits `colors` into the
// cache-hit flags (cacheColorFound[i] = 1 iff colorCache[i] equals some
// color, first match wins via the inner break) and the not-in-cache
// remainder (outCacheColors). Returns the remainder count. Caller
// contracts pinned by the reference arrays: n_colors <= paletteMaxSize
// (in_cache_flags[PALETTE_MAX_SIZE] stack array) and
// n_cache <= 2 * paletteMaxSize (cache_color_found[2 * PALETTE_MAX_SIZE]).
int indexColorCache(const std::uint16_t* colorCache, int nCache, const std::uint16_t* colors, int nColors,
                    std::uint8_t* cacheColorFound, int* outCacheColors);

// palette_add_to_cache (palette.c:154-161): appends val unless it equals
// the current tail cache[*n - 1].
void paletteAddToCache(std::uint16_t* cache, int* n, std::uint16_t val);

// av1_cost_literal (md_rate_estimation.h:31) with AV1_PROB_COST_SHIFT = 9
// (md_rate_estimation.h:29).
constexpr std::int32_t av1ProbCostShift = 9;
constexpr std::int32_t av1CostLiteral(std::int32_t n) {
    return n << av1ProbCostShift;
}

// svt_av1_palette_color_cost_y (palette.c:143-152). DEVIATION (named): the
// reference takes const PaletteModeInfo* and reads pmi->palette_colors
// (definitions.h:1905-1909); the struct is walled, so the colors pointer
// is threaded as a parameter (the caller passes the palette_colors base).
int paletteColorCostY(const std::uint16_t* paletteColors, std::uint16_t* colorCache, int paletteSize, int nCache,
                      int bitDepth);

// svt_av1_count_colors (pic_analysis_process.c:892-909; declared for
// palette.c use at palette.c:327): the 8-bit per-value histogram over
// rows*cols pixels (max_pix_val = 256); returns the distinct-value count.
// valCount needs >= 256 entries (the search_palette_luma caller passes
// 1 << 12, palette.c:405).
int countColors(const std::uint8_t* src, int stride, int rows, int cols, int* valCount);

// k_means_template.h, the AV1_K_MEANS_DIM = 1 instantiation (included at
// palette.c:55-57). calc_indices (:33-45): indices[i] = argmin over the k
// centroids of the dim-1 squared distance; the update is STRICTLY
// this_dist < min_dist, so ties keep the lower index.
void calcIndicesDim1(const int* data, const int* centroids, std::uint8_t* indices, int n, int k);

// k_means_template.h, the AV1_K_MEANS_DIM = 2 instantiation (included at
// palette.c:58-60). Same argmin contract as dim 1 over 2-D squared
// distance; data/centroids are interleaved {c0, c1} pairs.
void calcIndicesDim2(const int* data, const int* centroids, std::uint8_t* indices, int n, int k);

// k_means_template.h svt_av1_k_means, the DIM = 1 instantiation (:86-111):
// the iteration loop with the pre_centroids/pre_indices rollback
// (this_dist > pre_dist -> restore + break) and the memcmp convergence
// break. Centroids are rounded to nearest integers via DIVIDE_AND_ROUND.
void kMeansDim1(const int* data, int* centroids, std::uint8_t* indices, int n, int k, int maxItr);

// k_means_template.h svt_av1_k_means, the DIM = 2 instantiation. Same
// loop contract; the empty-cluster refill seeds rand_state from data[0]'s
// FIRST component (k_means_template.h:50 reads data[0]).
void kMeansDim2(const int* data, int* centroids, std::uint8_t* indices, int n, int k, int maxItr);

// av1_calc_indices (palette.c:31-39): the dim dispatcher. dim 3+ asserts
// ("Untemplated k means dimension") and - as in the reference's Release
// build - leaves the outputs untouched.
void calcIndices(const int* data, const int* centroids, std::uint8_t* indices, int n, int k, int dim);

// av1_k_means (palette.c:45-53): the dim dispatcher over kMeansDim1/2.
void kMeans(const int* data, int* centroids, std::uint8_t* indices, int n, int k, int dim, int maxItr);

// optimize_palette_colors (palette.c:250-270): snaps each stride-stepped
// centroid to its nearest cache color when the difference is within
// (6 + (qp_index >> 6)) << (bit_depth - 8). n_cache <= 0 returns early.
void optimizePaletteColors(std::uint16_t* colorCache, int nCache, int nColors, int stride, int* centroids,
                           int bitDepth, std::uint8_t qpIndex);

// extend_palette_color_map (palette.c:275-294): grows an orig_width x
// orig_height color-index map to new_width x new_height in place; extra
// columns repeat the row's last valid index, extra rows repeat the last
// valid row. Equal dims return untouched.
void extendPaletteColorMap(std::uint8_t* colorMap, int origWidth, int origHeight, int newWidth, int newHeight);

// cache_based_centroid_refinement (palette.c:330-386): trials every cache
// color as a replacement for each centroid; a candidate wins on STRICT
// sse < best_sse and overwrites color_idx_map with the trial assignment.
void cacheBasedCentroidRefinement(int* data, int rows, int cols, int n, int* centroids, std::uint8_t* colorIdxMap,
                                  std::uint16_t* colorCache, int nCache);

// MAX_COLOR_CONTEXT_HASH (palette.c:639) = 8; the lookup table
// (palette.c:641) has MAX_COLOR_CONTEXT_HASH + 1 = 9 entries. DEVIATION
// (named): the reference declares a mutable global; the port pins it
// const (it is only ever read - palette.c:672/:772).
constexpr std::int32_t maxColorContextHash = 8;
extern const std::int32_t paletteColorIndexContextLookup[maxColorContextHash + 1];

// av1_fast_palette_color_index_context_on_edge (palette.c:645-676): the
// single-neighbor edge case of the fast color-index context. Writes
// *colorIdx (unchanged / +1 on a greater neighbor / 0 on an equal one)
// and ALWAYS returns context 0.
int fastPaletteColorIndexContextOnEdge(const std::uint8_t* colorMap, int stride, int r, int c, int* colorIdx);

// av1_fast_palette_color_index_context (palette.c:680-776): the encoder's
// fast color-index context over the left/top/top-left neighbors - scores
// {2,2,1}, duplicate aggregation into INVALID_COLOR_IDX (255), the
// score-desc / color-asc sort, the *colorIdx walk, and the context
// 9 - hash with hash = sum score_rank[idx] * {1,2,2}[idx]. Edge positions
// delegate to the on_edge twin.
int fastPaletteColorIndexContext(const std::uint8_t* colorMap, int stride, int r, int c, int* colorIdx);

} // namespace l13_palette
