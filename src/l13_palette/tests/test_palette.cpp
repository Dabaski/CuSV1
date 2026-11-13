#include <doctest.h>

#include <cstring>

#include "palette.h"

TEST_CASE("intComparer orders two ints ascending") {
    // 1:1 target: int_comparer (palette.c:62-66): the subtraction itself,
    // qsort's comparer contract (negative / 0 / positive).
    int a = 3;
    int b = 9;
    CHECK(l13_palette::intComparer(&a, &b) < 0);
    CHECK(l13_palette::intComparer(&b, &a) > 0);
    CHECK(l13_palette::intComparer(&a, &a) == 0);
}

TEST_CASE("removeDuplicates sorts then compacts a multiset with collisions") {
    // 1:1 target: av1_remove_duplicates (palette.c:66-78): qsort ascending,
    // then the compacting walk (a new unique value is copied into slot
    // num_unique++). Returns the unique count; the compacted prefix sits in
    // centroids[0 .. count-1].
    int centroids[] = {7, 7, 9, 9, 9, 2};
    const int n = l13_palette::removeDuplicates(centroids, 6);
    CHECK(n == 3);
    CHECK(centroids[0] == 2);
    CHECK(centroids[1] == 7);
    CHECK(centroids[2] == 9);
}

TEST_CASE("removeDuplicates empty and single inputs return 1") {
    // 1:1 pin: the reference seeds num_unique = 1 UNCONDITIONALLY
    // (palette.c:71), so num_centroids == 0 also returns 1 - reproduced,
    // not "fixed".
    int none[1] = {0};
    CHECK(l13_palette::removeDuplicates(none, 0) == 1);
    int one[1] = {42};
    CHECK(l13_palette::removeDuplicates(one, 1) == 1);
    int allSame[3] = {5, 5, 5};
    CHECK(l13_palette::removeDuplicates(allSame, 3) == 1);
    // The LAST element being a NEW unique value is what a truncated scan
    // (i < num_centroids - 1) drops: all-distinct {5,2,9} must return 3
    // with the compacted prefix {2,5,9}.
    int distinct[3] = {5, 2, 9};
    CHECK(l13_palette::removeDuplicates(distinct, 3) == 3);
    CHECK(distinct[0] == 2);
    CHECK(distinct[1] == 5);
    CHECK(distinct[2] == 9);
}

TEST_CASE("deltaEncodeCost degenerate counts") {
    // 1:1 target: delta_encode_cost (palette.c:80-109). num <= 0 -> 0;
    // num == 1 -> bit_depth (the first color's raw bits).
    const int colors[] = {10, 20};
    CHECK(l13_palette::deltaEncodeCost(colors, 0, 8, 1) == 0);
    CHECK(l13_palette::deltaEncodeCost(colors, 1, 8, 1) == 8);
    CHECK(l13_palette::deltaEncodeCost(colors, 1, 10, 1) == 10);
}

TEST_CASE("deltaEncodeCost three-color oracle") {
    // Hand-computed oracle for palette.c:80-109 with bit_depth 8, min_val 1,
    // colors {0, 1, 3}: bits_cost = 8 + 2 = 10; deltas {1, 2}, max_delta 2;
    // bits_per_delta = max(ceil_log2(2), min_bits 5) = 5; range = 255.
    // i=0: cost 15, range 254, bpd = min(5, ceil_log2(254) = 8) = 5.
    // i=1: cost 20. ceil_log2 here is av1_ceil_log2 (cabac_context_model.h:
    // 366-371) = get_msb(n-1) + 1 for n >= 2.
    const int colors[] = {0, 1, 3};
    CHECK(l13_palette::deltaEncodeCost(colors, 3, 8, 1) == 20);
}

TEST_CASE("deltaEncodeCost bits-per-delta shrinks as the range closes") {
    // bit_depth 8, min_val 1, colors {250, 252, 253}: deltas {2, 1};
    // bits_per_delta = max(ceil_log2(2), 5) = 5; range = 256-250-1 = 5.
    // The chain runs num-1 = TWO iterations:
    // i=0: cost 10+5 = 15, range 3, bpd = min(5, ceil_log2(3) = 2) = 2.
    // i=1: cost 15+2 = 17. Dropping the shrink (bpd fixed at 5) answers
    // 20; a chain that indexes deltas[i] instead of deltas[i-1] walks off
    // the deltas[] end.
    const int colors[] = {250, 252, 253};
    CHECK(l13_palette::deltaEncodeCost(colors, 3, 8, 1) == 17);
}

TEST_CASE("deltaEncodeCost four-color chain length") {
    // {0, 4, 6, 7}: deltas {4, 2, 1}, max 4, bpd = max(ceil_log2(4), 5) = 5,
    // range 255 stays wide -> three +5 adds: 10 + 15 = 25.
    const int colors[] = {0, 4, 6, 7};
    CHECK(l13_palette::deltaEncodeCost(colors, 4, 8, 1) == 25);
}

TEST_CASE("indexColorCache with an empty cache passes colors through") {
    // 1:1 target: svt_av1_index_color_cache (palette.c:111-141). n_cache
    // <= 0: out_cache_colors = colors verbatim, return n_colors, and
    // cache_color_found is NOT touched (the memset sits after the branch).
    const uint16_t colors[] = {3, 9, 200};
    int out[l13_palette::paletteMaxSize] = {0};
    uint8_t found[2 * l13_palette::paletteMaxSize] = {9, 9, 9, 9};
    const int n = l13_palette::indexColorCache(nullptr, 0, colors, 3, found, out);
    CHECK(n == 3);
    CHECK(out[0] == 3);
    CHECK(out[1] == 9);
    CHECK(out[2] == 200);
    CHECK(found[0] == 9); // untouched by the early-out branch
}

TEST_CASE("indexColorCache flags exactly the matching cache position") {
    // Enumerated range: cache_color_found is 2 * PALETTE_MAX_SIZE = 16
    // slots (palette.c:147), so every position 0..15 gets its turn as THE
    // hit; all other slots stay 0 and the hit color leaves the output.
    for (int p = 0; p < 2 * l13_palette::paletteMaxSize; ++p) {
        const uint16_t colors[] = {11, 22, 33};
        uint16_t cache[2 * l13_palette::paletteMaxSize] = {0};
        cache[p] = 22; // the single hit, at position p
        uint8_t found[2 * l13_palette::paletteMaxSize] = {0};
        int out[l13_palette::paletteMaxSize] = {0};
        const int n = l13_palette::indexColorCache(cache, p + 1, colors, 3, found, out);
        CHECK(n == 2);
        CHECK(found[p] == 1);
        CHECK(out[0] == 11);
        CHECK(out[1] == 33);
    }
}

TEST_CASE("indexColorCache keeps colors the cache misses") {
    // A cache value absent from colors leaves found[i] == 0 and keeps the
    // color in out_cache_colors.
    const uint16_t cache[] = {5, 77, 6};
    const uint16_t colors[] = {11, 22, 33};
    uint8_t found[3] = {1, 1, 1};
    int out[l13_palette::paletteMaxSize] = {0};
    const int n = l13_palette::indexColorCache(cache, 3, colors, 3, found, out);
    CHECK(n == 3);
    CHECK(found[0] == 0);
    CHECK(found[1] == 0);
    CHECK(found[2] == 0);
    CHECK(out[0] == 11);
    CHECK(out[1] == 22);
    CHECK(out[2] == 33);
}

TEST_CASE("indexColorCache duplicate colors: only the first is flagged") {
    // colors {7, 7} with cache {7}: the inner loop's break flags the FIRST
    // 7 only; the second 7 stays in out_cache_colors (palette.c:124-130).
    const uint16_t cache[] = {7};
    const uint16_t colors[] = {7, 7};
    uint8_t found[1] = {0};
    int out[l13_palette::paletteMaxSize] = {0};
    const int n = l13_palette::indexColorCache(cache, 1, colors, 2, found, out);
    CHECK(n == 1);
    CHECK(found[0] == 1);
    CHECK(out[0] == 7);
}

TEST_CASE("indexColorCache scan guard stops after all colors are found") {
    // colors {7, 9}, cache {7, 9, 7}: once n_in_cache reaches n_colors the
    // outer loop's second clause (palette.c:123) exits, so the duplicate
    // cache slot 2 is never examined and found[2] stays 0.
    const uint16_t cache[] = {7, 9, 7};
    const uint16_t colors[] = {7, 9};
    uint8_t found[3] = {0, 0, 0};
    int out[l13_palette::paletteMaxSize] = {0};
    const int n = l13_palette::indexColorCache(cache, 3, colors, 2, found, out);
    CHECK(n == 0);
    CHECK(found[0] == 1);
    CHECK(found[1] == 1);
    CHECK(found[2] == 0);
}

TEST_CASE("paletteAddToCache appends and skips an equal tail") {
    // 1:1 target: palette_add_to_cache (palette.c:154-161): appends unless
    // the value equals the CURRENT tail (cache[*n - 1]); equal values
    // elsewhere in the cache still append.
    uint16_t cache[8] = {0};
    int n = 0;
    l13_palette::paletteAddToCache(cache, &n, 5);
    CHECK(n == 1);
    CHECK(cache[0] == 5);
    l13_palette::paletteAddToCache(cache, &n, 5); // equal tail: skipped
    CHECK(n == 1);
    l13_palette::paletteAddToCache(cache, &n, 9);
    CHECK(n == 2);
    CHECK(cache[1] == 9);
    l13_palette::paletteAddToCache(cache, &n, 5); // not the tail: appended
    CHECK(n == 3);
    CHECK(cache[2] == 5);
}

TEST_CASE("paletteColorCostY cost oracle") {
    // 1:1 target: svt_av1_palette_color_cost_y (palette.c:143-152):
    // total_bits = n_cache + delta_encode_cost(out_cache_colors,
    // n_out_cache, bit_depth, 1); the return is av1_cost_literal
    // (md_rate_estimation.h:31) = total_bits << AV1_PROB_COST_SHIFT (9).
    // Fixture: palette colors {5, 7, 10}, cache {7}: 7 is in cache ->
    // out = {5, 10}, n_out = 2; delta_encode_cost({5,10},2,8,1) =
    // 8 + 2 + 5 = 15 (delta 5, bpd = max(ceil_log2(5), 5) = 5, range 250
    // stays wide). total_bits = 1 + 15 = 16 -> 16 << 9 = 8192.
    const uint16_t paletteColors[] = {5, 7, 10};
    uint16_t cache[] = {7}; // the reference takes uint16_t* (palette.c:143)
    CHECK(l13_palette::paletteColorCostY(paletteColors, cache, 3, 1, 8) == 8192);
}

TEST_CASE("paletteColorCostY two cache hits reduce to the single-color cost") {
    // Palette {250, 252, 253}, cache {252, 253}: both hits are removed ->
    // out = {250}, n_out = 1; delta_encode_cost({250},1,8,1) = 8 (the
    // num == 1 early return, palette.c:85-87). total_bits = 2 + 8 = 10 ->
    // 10 << 9 = 5120.
    const uint16_t paletteColors[] = {250, 252, 253};
    uint16_t cache[] = {252, 253};
    CHECK(l13_palette::paletteColorCostY(paletteColors, cache, 3, 2, 8) == 5120);
}

TEST_CASE("countColors solid block returns 1") {
    // 1:1 target: svt_av1_count_colors (pic_analysis_process.c:892-909;
    // declared for palette.c use at palette.c:327). The val_count
    // histogram is zeroed over 256 entries and bumped per pixel; the
    // return is the count of non-zero bins.
    uint8_t src[16];
    std::memset(src, 7, sizeof(src));
    int valCount[256] = {0};
    const int n = l13_palette::countColors(src, 8, 2, 2, valCount);
    CHECK(n == 1);
    CHECK(valCount[7] == 4);
    CHECK(valCount[0] == 0);
}

TEST_CASE("countColors stride gap and many distinct values") {
    // rows=2, cols=3, stride=8: the inter-row gap (src[3..7], src[11..15])
    // must not be sampled. Values {1,2,3} twice -> 3 distinct, 2 each.
    uint8_t src[16];
    std::memset(src, 0xAA, sizeof(src)); // junk in the gap
    src[0] = 1; src[1] = 2; src[2] = 3;
    src[8] = 1; src[9] = 2; src[10] = 3;
    int valCount[256] = {0};
    const int n = l13_palette::countColors(src, 8, 2, 3, valCount);
    CHECK(n == 3);
    CHECK(valCount[1] == 2);
    CHECK(valCount[2] == 2);
    CHECK(valCount[3] == 2);
    CHECK(valCount[0xAA] == 0);
    // A 2x2 with four distinct values -> 4.
    uint8_t four[16];
    four[0] = 10; four[1] = 20; four[8] = 30; four[9] = 40;
    int valCount2[256] = {0};
    CHECK(l13_palette::countColors(four, 8, 2, 2, valCount2) == 4);
    CHECK(valCount2[10] == 1);
    CHECK(valCount2[40] == 1);
}

TEST_CASE("calcIndicesDim1 distance tie keeps the first centroid") {
    // 1:1 target: svt_av1_calc_indices_dim1 (k_means_template.h:33-45, the
    // AV1_K_MEANS_DIM=1 instantiation). The update is strictly
    // this_dist < min_dist (template :39), so an exact tie keeps the
    // LOWER index. |55-10| == |55-100| == 45.
    const int data[] = {55};
    const int centroids[] = {10, 100};
    uint8_t indices[1] = {0xFF};
    l13_palette::calcIndicesDim1(data, centroids, indices, 1, 2);
    CHECK(indices[0] == 0);
}

TEST_CASE("calcIndicesDim1 over the enumerated k range 2..8") {
    // The callers drive k from PALETTE_MAX_SIZE (8) down to PALETTE_MIN_SIZE
    // (2) (palette.c:451-452, :518), so every k in 2..8 is admitted. For
    // each k: centroids at 0, 100, ..., and one data point exactly ON
    // centroid k-1 (the last) plus one on centroid 0 - the argmin walk must
    // reach j = k-1 for the far point.
    for (int k = 2; k <= l13_palette::paletteMaxSize; ++k) {
        int centroids[l13_palette::paletteMaxSize];
        for (int j = 0; j < k; ++j) {
            centroids[j] = 100 * j;
        }
        const int data[] = {100 * (k - 1), 0};
        uint8_t indices[2] = {0xFF, 0xFF};
        l13_palette::calcIndicesDim1(data, centroids, indices, 2, k);
        CHECK(indices[0] == k - 1);
        CHECK(indices[1] == 0);
    }
}

TEST_CASE("calcIndicesDim2 squared 2-D distance and tie rule") {
    // 1:1 target: svt_av1_calc_indices_dim2 (k_means_template.h:33-45, the
    // AV1_K_MEANS_DIM=2 instantiation): dist = (d0-c0)^2 + (d1-c1)^2.
    // Tie: |55-10|^2 + 0 == |55-100|^2 + 0 -> the first centroid wins.
    const int data[] = {55, 0};
    const int centroids[] = {10, 0, 100, 0};
    uint8_t indices[1] = {0xFF};
    l13_palette::calcIndicesDim2(data, centroids, indices, 1, 2);
    CHECK(indices[0] == 0);
    // Distinct 2-D argmin: {5, 5} vs {0,0} = 50, {0,9} = 41, {9,9} = 32
    // -> index 2.
    const int data2[] = {5, 5};
    const int centroids2[] = {0, 0, 0, 9, 9, 9};
    uint8_t indices2[1] = {0xFF};
    l13_palette::calcIndicesDim2(data2, centroids2, indices2, 1, 3);
    CHECK(indices2[0] == 2);
}

TEST_CASE("calcIndicesDim2 over the enumerated k range 2..8") {
    // Same admitted k range as dim 1 (2..8, palette.c:451-452/:518): for
    // each k, k centroids along the diagonal {100j, 100j} and two data
    // points ON the first and last centroids.
    for (int k = 2; k <= l13_palette::paletteMaxSize; ++k) {
        int centroids[2 * l13_palette::paletteMaxSize];
        for (int j = 0; j < k; ++j) {
            centroids[2 * j] = 100 * j;
            centroids[2 * j + 1] = 100 * j;
        }
        const int data[] = {0, 0, 100 * (k - 1), 100 * (k - 1)};
        uint8_t indices[2] = {0xFF, 0xFF};
        l13_palette::calcIndicesDim2(data, centroids, indices, 2, k);
        CHECK(indices[0] == 0);
        CHECK(indices[1] == k - 1);
    }
}

TEST_CASE("kMeansDim1 exact trace with an empty-cluster LCG refill") {
    // 1:1 target: svt_av1_k_means_dim1 (k_means_template.h:86-111, DIM=1)
    // with calc_centroids (:47-74) and calc_total_dist (:76-84).
    // data = {10..100 step 10}, k = 3, init {10, 100, 200}, max_itr = 3.
    // Initial indices: 10-50 -> 0, 60-100 -> 1; cluster 2 EMPTY.
    // i=0: calc_centroids: c0 = DR(150,5) = 30, c1 = DR(450,5) = 90, c2
    // empty -> lcg_rand16 seeded with data[0]=10: state = 10*1103515245 +
    // 12345 mod 2^32 = 2445230203; rand16 = 2445230203/65536 % 32768 =
    // 4543; 4543 % 10 = 3 -> c2 = data[3] = 40. Reassign: 40,50,60 -> 2.
    // i=1: c0 = DR(60,3) = 20, c1 = DR(340,4) = 85, c2 = DR(150,3) = 50.
    // i=2: same means -> memcmp break. FINAL: centroids {20, 85, 50},
    // indices {0,0,0,2,2,2,1,1,1,1}.
    const std::uint32_t seed = 10;
    const std::uint32_t state = static_cast<std::uint32_t>(seed * 1103515245ULL + 12345);
    CHECK(state == 2445230203);
    const std::uint32_t rand16 = (state / 65536) % 32768;
    CHECK(rand16 == 4543);
    const int data[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    CHECK(data[rand16 % 10] == 40);
    int centroids[] = {10, 100, 200};
    uint8_t indices[10] = {0};
    l13_palette::kMeansDim1(data, centroids, indices, 10, 3, 3);
    CHECK(centroids[0] == 20);
    CHECK(centroids[1] == 85);
    CHECK(centroids[2] == 50);
    const uint8_t want[10] = {0, 0, 0, 2, 2, 2, 1, 1, 1, 1};
    for (int i = 0; i < 10; ++i) {
        CHECK(indices[i] == want[i]);
    }
}

TEST_CASE("kMeansDim2 exact trace with an empty-cluster LCG refill") {
    // 1:1 target: svt_av1_k_means_dim2 (k_means_template.h:86-111, DIM=2).
    // data = {(0,0), (0,10), (100,0), (100,10)}, k = 3,
    // init {(0,0), (100,10), (50,50)}, max_itr = 3.
    // Initial: (0,0)->0, (0,10)->0 (100), (100,0)->1 (100), (100,10)->1;
    // cluster 2 EMPTY. rand_state = data[0] component 0 = 0 -> lcg state
    // 12345 -> rand16 = 0 -> idx 0 -> c2 = (0,0). i=0 means:
    // c0 = (0,5) (DR(0,2), DR(10,2)), c1 = (100,5); reassign (0,0)->2.
    // i=1: c0 = (0,10), c1 = (100,5), c2 = (0,0). i=2: same -> break.
    // FINAL: centroids {0,10,100,5,0,0}, indices {2,0,1,1}.
    const int data[] = {0, 0, 0, 10, 100, 0, 100, 10};
    int centroids[] = {0, 0, 100, 10, 50, 50};
    uint8_t indices[4] = {0};
    l13_palette::kMeansDim2(data, centroids, indices, 4, 3, 3);
    CHECK(centroids[0] == 0);
    CHECK(centroids[1] == 10);
    CHECK(centroids[2] == 100);
    CHECK(centroids[3] == 5);
    CHECK(centroids[4] == 0);
    CHECK(centroids[5] == 0);
    const uint8_t want[4] = {2, 0, 1, 1};
    for (int i = 0; i < 4; ++i) {
        CHECK(indices[i] == want[i]);
    }
}

TEST_CASE("kMeansDim1 continues on an equal-distance iteration") {
    // The rollback guard is this_dist > pre_dist (k_means_template.h:102)
    // - an EQUAL distance does NOT roll back. data {0,1,5,6}, k=2, init
    // {0,6}: initial assign {0,0,1,1}, dist 2. i=0: means DR(1,2)=1 and
    // DR(11,2)=6 -> {1,6}; reassign unchanged, dist 2 (EQUAL) and the
    // means MOVED -> no rollback, no memcmp break. i=1 stabilizes.
    // FINAL {1,6}, indices {0,0,1,1}. A >= mutant rolls back to {0,6}
    // at i=0 and answers centroids[0] == 0.
    const int data[] = {0, 1, 5, 6};
    int centroids[] = {0, 6};
    uint8_t indices[4] = {0};
    l13_palette::kMeansDim1(data, centroids, indices, 4, 2, 3);
    CHECK(centroids[0] == 1);
    CHECK(centroids[1] == 6);
    CHECK(indices[0] == 0);
    CHECK(indices[1] == 0);
    CHECK(indices[2] == 1);
    CHECK(indices[3] == 1);
}

TEST_CASE("kMeansDim1 refills two empty clusters from one LCG chain") {
    // Two empty clusters drain the SAME rand_state (seeded data[0] = 200
    // here): pick 1 idx 5, pick 2 idx 4 (state 1659729249 -> 3806640262,
    // rand16 25325 -> 25316). data {200,200,200,200,100,100,100,90}, k=4,
    // init {200,100,150,50}: i=0 gives c2 = data[5] = 100,
    // c3 = data[4] = 100. Reassign: the 100s tie across c2 (first min,
    // index 2) and 90 stays at c1 (98). i=1: c1 = DR(90,1) = 90,
    // c2 = DR(300,3) = 100, c3 refilled with data[5] = 100; i=2
    // stabilizes. FINAL: centroids {200,90,100,100}, indices
    // {0,0,0,0,2,2,2,1} (the 100s tie across c2/c3 -> first min 2; the 90
    // sits alone at c1). A multiplier+1 mutant moves pick 2 to idx 7 ->
    // c3 = data[7] = 90 at i=0 and converges to {200,100,100,90}.
    const int data[] = {200, 200, 200, 200, 100, 100, 100, 90};
    int centroids[] = {200, 100, 150, 50};
    uint8_t indices[8] = {0};
    l13_palette::kMeansDim1(data, centroids, indices, 8, 4, 3);
    CHECK(centroids[0] == 200);
    CHECK(centroids[1] == 90);
    CHECK(centroids[2] == 100);
    CHECK(centroids[3] == 100);
    const uint8_t want[8] = {0, 0, 0, 0, 2, 2, 2, 1};
    for (int i = 0; i < 8; ++i) {
        CHECK(indices[i] == want[i]);
    }
}

TEST_CASE("kMeansDim1 and kMeansDim2 hold exact-mean inits over k 2..8") {
    // With init centroids exactly ON the cluster means and every cluster
    // populated, calc_indices keeps the assignment, calc_centroids
    // reproduces the means (DIVIDE_AND_ROUND of the exact sums), and the
    // memcmp break fires at i=0 - for EVERY admitted k (2..8, the
    // palette.c:518 drive range). The centroid pins alone are blind to a
    // no-op (the init IS the answer), so the INDICES are asserted too:
    // data point 2j sits 7 away from centroid j and >= 93 from every
    // other centroid, so the assignment is j for both of its points. An
    // early-return implementation leaves the zero-initialized indices and
    // fails idx[2] == 1 for every k >= 2.
    for (int k = 2; k <= l13_palette::paletteMaxSize; ++k) {
        // dim 1: cluster j holds {100j, 100j+14} -> mean 100j + 7.
        int data1[2 * l13_palette::paletteMaxSize];
        int init1[l13_palette::paletteMaxSize];
        for (int j = 0; j < k; ++j) {
            data1[2 * j] = 100 * j;
            data1[2 * j + 1] = 100 * j + 14;
            init1[j] = 100 * j + 7;
        }
        int cent1[l13_palette::paletteMaxSize];
        std::memcpy(cent1, init1, sizeof(init1));
        uint8_t idx1[2 * l13_palette::paletteMaxSize] = {0};
        l13_palette::kMeansDim1(data1, cent1, idx1, 2 * k, k, 5);
        for (int j = 0; j < k; ++j) {
            CHECK(cent1[j] == init1[j]);
            CHECK(idx1[2 * j] == j);
            CHECK(idx1[2 * j + 1] == j);
        }
        // dim 2: cluster j holds {(100j, 100j), (100j+14, 100j+6)}
        // -> mean (100j + 7, 100j + 3).
        int data2[4 * l13_palette::paletteMaxSize];
        int init2[2 * l13_palette::paletteMaxSize];
        for (int j = 0; j < k; ++j) {
            data2[4 * j] = 100 * j;
            data2[4 * j + 1] = 100 * j;
            data2[4 * j + 2] = 100 * j + 14;
            data2[4 * j + 3] = 100 * j + 6;
            init2[2 * j] = 100 * j + 7;
            init2[2 * j + 1] = 100 * j + 3;
        }
        int cent2[2 * l13_palette::paletteMaxSize];
        std::memcpy(cent2, init2, sizeof(init2));
        uint8_t idx2[2 * l13_palette::paletteMaxSize] = {0};
        l13_palette::kMeansDim2(data2, cent2, idx2, 2 * k, k, 5);
        for (int j = 0; j < 2 * k; ++j) {
            CHECK(cent2[j] == init2[j]);
        }
        for (int j = 0; j < k; ++j) {
            CHECK(idx2[2 * j] == j);
            CHECK(idx2[2 * j + 1] == j);
        }
    }
}

TEST_CASE("calcIndices and kMeans dispatch dim 1 and 2, no-op otherwise") {
    // 1:1 targets: av1_calc_indices (palette.c:31-39) and av1_k_means
    // (:45-53): dim == 1 -> the dim1 body, dim == 2 -> the dim2 body,
    // anything else asserts (dead in Release) and leaves the outputs
    // untouched.
    const int data1[] = {55};
    const int cent1[] = {10, 100};
    uint8_t idx[1] = {0xFF};
    l13_palette::calcIndices(data1, cent1, idx, 1, 2, 1);
    CHECK(idx[0] == 0);
    const int data2[] = {55, 0};
    const int cent2[] = {10, 0, 100, 0};
    idx[0] = 0xFF;
    l13_palette::calcIndices(data2, cent2, idx, 1, 2, 2);
    CHECK(idx[0] == 0);
    idx[0] = 0xFF;
    l13_palette::calcIndices(data2, cent2, idx, 1, 2, 3); // neither: no-op
    CHECK(idx[0] == 0xFF);
    int cent3[] = {10, 100};
    uint8_t idxK[2] = {0xFF, 0xFF};
    l13_palette::kMeans(data1, cent3, idxK, 1, 2, 1, 2);
    CHECK(idxK[0] == 0);
    l13_palette::kMeans(data1, cent3, idxK, 1, 2, 3, 2); // neither: no-op
    CHECK(idxK[1] == 0xFF);
}

TEST_CASE("optimizePaletteColors snaps centroids near a cache color") {
    // 1:1 target: optimize_palette_colors (palette.c:250-270): each
    // centroid (stride-stepped) snaps to its NEAREST cache color when
    // min_diff <= (6 + (qp_index >> 6)) << (bit_depth - 8). Cache
    // {12, 190}, qp_index 0, bd 8 -> threshold 6: |10-12| = 2 snaps,
    // |200-190| = 10 stays.
    uint16_t cache[] = {12, 190};
    int centroids[] = {10, 200};
    l13_palette::optimizePaletteColors(cache, 2, 2, 1, centroids, 8, 0);
    CHECK(centroids[0] == 12);
    CHECK(centroids[1] == 200);
    // qp_index 64 -> threshold 7; 200->190 diff 10 still stays; a
    // centroid 8 from the cache snaps.
    int centroids2[] = {5, 200};
    l13_palette::optimizePaletteColors(cache, 2, 2, 1, centroids2, 8, 64);
    CHECK(centroids2[0] == 12);
    CHECK(centroids2[1] == 200);
    // Nearest-of-two cache selection: 185 is 5 from 190, 173 from 12 ->
    // snaps to 190.
    int centroids3[] = {185, 200};
    l13_palette::optimizePaletteColors(cache, 2, 2, 1, centroids3, 8, 255); // thr 9
    CHECK(centroids3[0] == 190);
}

TEST_CASE("optimizePaletteColors stride steps over interleaved slots") {
    // stride 2: n_colors = 2 visits centroids[0] and centroids[2] only;
    // centroids[1] / [3] are untouched (palette.c:255 steps i += stride).
    uint16_t cache[] = {12};
    int centroids[] = {10, 777, 200, 777};
    l13_palette::optimizePaletteColors(cache, 1, 2, 2, centroids, 8, 0);
    CHECK(centroids[0] == 12);
    CHECK(centroids[1] == 777);
    CHECK(centroids[2] == 200); // 190 away, above threshold 6
    CHECK(centroids[3] == 777);
}

TEST_CASE("optimizePaletteColors snaps exactly at the threshold boundary") {
    // The comparison is min_diff <= min_threshold (palette.c:266). qp 0,
    // bd 8 -> threshold 6; a centroid 6 away from the cache color snaps
    // (a < mutant would leave it).
    uint16_t cache[] = {0};
    int centroids[] = {6};
    l13_palette::optimizePaletteColors(cache, 1, 1, 1, centroids, 8, 0);
    CHECK(centroids[0] == 0);
}

TEST_CASE("extendPaletteColorMap fills edge rows and columns") {
    // 1:1 target: extend_palette_color_map (palette.c:275-294): each row is
    // memmoved to its new stride, the extra columns copy the row's last
    // valid column, and the extra rows copy the last valid row.
    // 2x2 {1,2; 3,4} -> 3x3: {1,2,2; 3,4,4; 3,4,4}. The input is packed
    // at orig_width (row 1 = map[2..3]).
    uint8_t map[9] = {1, 2, 3, 4, 0, 0, 0, 0, 0};
    l13_palette::extendPaletteColorMap(map, 2, 2, 3, 3);
    const uint8_t want[9] = {1, 2, 2, 3, 4, 4, 3, 4, 4};
    for (int i = 0; i < 9; ++i) {
        CHECK(map[i] == want[i]);
    }
    // Equal dims: the early-out leaves the map untouched.
    uint8_t same[4] = {9, 8, 7, 6};
    l13_palette::extendPaletteColorMap(same, 2, 2, 2, 2);
    CHECK(same[0] == 9);
    CHECK(same[3] == 6);
}

TEST_CASE("cacheBasedCentroidRefinement accepts a strict SSE improvement") {
    // 1:1 target: cache_based_centroid_refinement (palette.c:330-386):
    // per centroid c, every cache candidate (skipping candidate ==
    // original) is trialed by FULL reassignment (first-min ties); the
    // candidate wins only on sse < best_sse (STRICT, palette.c:377) and
    // then color_idx_map is overwritten with the trial map.
    // data {0,1,1,1}, centroids {0,2}, map {0,1,1,1}, cache {1}:
    // baseline sse 3. c=0 with candidate 1: reassign -> {0,0,0,0},
    // sse 1 < 3 -> map OVERWRITTEN to {0,0,0,0}, centroid[0] = 1.
    // c=1 (original 2, candidate 1): the baseline is NOT recomputed
    // (the stale 3 is reused, palette.c:345); reassign -> {0,0,0,0},
    // sse 1 < 3 -> centroid[1] = 1. FINAL: centroids {1, 1},
    // map {0,0,0,0}.
    int data[] = {0, 1, 1, 1};
    int centroids[] = {0, 2};
    uint8_t map[] = {0, 1, 1, 1};
    uint16_t cache[] = {1};
    l13_palette::cacheBasedCentroidRefinement(data, 2, 2, 2, centroids, map, cache, 1);
    CHECK(centroids[0] == 1);
    CHECK(centroids[1] == 1);
    CHECK(map[0] == 0);
    CHECK(map[1] == 0);
    CHECK(map[2] == 0);
    CHECK(map[3] == 0);
}

TEST_CASE("cacheBasedCentroidRefinement keeps centroids on ties") {
    // data {0,0,3,3}, centroids {0,4}, map {0,0,1,1}, cache {2}: every
    // trial's sse equals or exceeds the baseline 2, and the STRICT < at
    // palette.c:377 keeps everything (a <= mutant flips centroid[1] to a
    // tie-sse candidate).
    int data[] = {0, 0, 3, 3};
    int centroids[] = {0, 4};
    uint8_t map[] = {0, 0, 1, 1};
    uint16_t cache[] = {2};
    l13_palette::cacheBasedCentroidRefinement(data, 2, 2, 2, centroids, map, cache, 1);
    CHECK(centroids[0] == 0);
    CHECK(centroids[1] == 4);
    CHECK(map[0] == 0);
    CHECK(map[3] == 1);
}

TEST_CASE("paletteColorIndexContextLookup carries the reference's 9 entries") {
    // 1:1 target: svt_aom_palette_color_index_context_lookup
    // (palette.c:641), MAX_COLOR_CONTEXT_HASH 8 (:639) -> int[9]:
    // {-1, -1, 0, -1, -1, 4, 3, 2, 1}. Negative values are invalid.
    const int want[9] = {-1, -1, 0, -1, -1, 4, 3, 2, 1};
    for (int i = 0; i < 9; ++i) {
        CHECK(l13_palette::paletteColorIndexContextLookup[i] == want[i]);
    }
}

TEST_CASE("fastPaletteColorIndexContextOnEdge mutates color_idx and returns 0") {
    // 1:1 target: av1_fast_palette_color_index_context_on_edge
    // (palette.c:645-676): the single neighbor is ABOVE on a left edge
    // (c == 0) else LEFT; current = *color_idx = color_map[r][c];
    // neighbor > current -> (*color_idx)++; neighbor == current -> 0;
    // neighbor < current -> unchanged. The context return is 0 ALWAYS
    // (the hash dance at :667-:675 resolves lookup[2] == 0).
    const uint8_t map[4] = {5, 7, 9, 11}; // stride 2
    int idx = -1;
    CHECK(l13_palette::fastPaletteColorIndexContextOnEdge(map, 2, 0, 1, &idx) == 0);
    CHECK(idx == 7); // left 5 < current 7: unchanged
    idx = -1;
    CHECK(l13_palette::fastPaletteColorIndexContextOnEdge(map, 2, 1, 0, &idx) == 0);
    CHECK(idx == 9); // above 5 < current 9: unchanged
    const uint8_t mapUp[4] = {7, 5, 0, 0};
    idx = -1;
    CHECK(l13_palette::fastPaletteColorIndexContextOnEdge(mapUp, 2, 0, 1, &idx) == 0);
    CHECK(idx == 6); // left 7 > current 5: +1
    const uint8_t mapEq[4] = {3, 3, 0, 0};
    idx = -1;
    CHECK(l13_palette::fastPaletteColorIndexContextOnEdge(mapEq, 2, 0, 1, &idx) == 0);
    CHECK(idx == 0); // left == current: 0
}

TEST_CASE("fastPaletteColorIndexContext all-distinct neighbors and edge delegate") {
    // 1:1 target: av1_fast_palette_color_index_context (palette.c:680-776).
    // map 2x3 {{5,2,x}, {6,8,x}}: at (1,1): left 6, top 2, tl 5,
    // current 8. All distinct, scores {2,2,1}; equal scores keep ascending
    // color order (the SWAP fires only on color_rank[0] > color_rank[1],
    // palette.c:735). No neighbor >= current -> *color_idx stays 8.
    // Hash = 2*1 + 2*2 + 1*2 = 8 -> context 9 - 8 = 1 (palette.c:771).
    const uint8_t map[6] = {5, 2, 0, 6, 8, 0};
    int idx = -1;
    CHECK(l13_palette::fastPaletteColorIndexContext(map, 3, 1, 1, &idx) == 1);
    CHECK(idx == 8);
    // Edge delegate: (0,1) has only a left neighbor -> the on_edge twin
    // answers: return 0, color_idx per the single-neighbor rule.
    const uint8_t edgeMap[6] = {5, 7, 0, 0, 0, 0};
    idx = -1;
    CHECK(l13_palette::fastPaletteColorIndexContext(edgeMap, 3, 0, 1, &idx) == 0);
    CHECK(idx == 7);
}

TEST_CASE("fastPaletteColorIndexContext duplicate aggregation branches") {
    // The three merge branches + the all-equal branch, each with a
    // hand-derived context and *color_idx outcome.
    // (a) left == top: scores[0] = 4, INVALID shift pulls tl into slot 1.
    // map {{5,7,9}, {7,4,x}}: left 7, top 7, tl 5, current 4 -> hash
    // 4*1 + 1*2 = 6 -> ctx 3; walk: 7 > 4 -> 5, 5 > 4 -> 6.
    const uint8_t mapA[6] = {5, 7, 9, 7, 4, 0};
    int idx = -1;
    CHECK(l13_palette::fastPaletteColorIndexContext(mapA, 3, 1, 1, &idx) == 3);
    CHECK(idx == 6);
    // (b) left == top-left: scores[0] = 3, top stays.
    // map {{6,8,9}, {6,3,x}}: hash 3*1 + 2*2 = 7 -> ctx 2; walk: 6>3, 8>3 -> 5.
    const uint8_t mapB[6] = {6, 8, 9, 6, 3, 0};
    idx = -1;
    CHECK(l13_palette::fastPaletteColorIndexContext(mapB, 3, 1, 1, &idx) == 2);
    CHECK(idx == 5);
    // (c) top == top-left: scores[1] = 3, then SWAP(0,1) (score 2 < 3)
    // puts top first. map {{6,6,9}, {9,3,x}}: hash 3*1 + 2*2 = 7 -> ctx 2;
    // walk: 6>3 -> 4, 9>3 -> 5.
    const uint8_t mapC[6] = {6, 6, 9, 9, 3, 0};
    idx = -1;
    CHECK(l13_palette::fastPaletteColorIndexContext(mapC, 3, 1, 1, &idx) == 2);
    CHECK(idx == 5);
    // (d) all equal: scores[0] = 5, num_valid 1. map {{4,4,4}, {4,3,x}}:
    // hash 5*1 = 5 -> ctx 4; walk: 4 > 3 -> 4.
    const uint8_t mapD[6] = {4, 4, 4, 4, 3, 0};
    idx = -1;
    CHECK(l13_palette::fastPaletteColorIndexContext(mapD, 3, 1, 1, &idx) == 4);
    CHECK(idx == 4);
}

TEST_CASE("fastPaletteColorIndexContext equal-score tie break drives color_idx") {
    // The SWAP tie-break (palette.c:735): equal scores sort colors
    // ASCENDING. map {{3,5,x}, {9,5,x}}: left 9, top 5, tl 3, current 5.
    // Correct: colors {9,5,3} scores {2,2,1} -> swap 0/1 -> {5,9,3};
    // walk: 5 == 5 -> idx 0, break. A flipped tie-break keeps {9,5,3}:
    // 9 > 5 -> 6, then 5 == 5 -> idx 1.
    const uint8_t map[6] = {3, 5, 0, 9, 5, 0};
    int idx = -1;
    CHECK(l13_palette::fastPaletteColorIndexContext(map, 3, 1, 1, &idx) == 1);
    CHECK(idx == 0);
}
