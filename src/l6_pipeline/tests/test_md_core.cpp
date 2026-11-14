// A4C tests: the product_coding_loop.c remainder (fast loops, ME searches,
// PME, ref pruning, inverse-recon) + the md_process.c tail. Independent
// oracles throughout: the variance dispatch against test-side variance
// transcriptions, the fast-loop cost flows against hand-computed RDCOST
// values, the stage walks over enumerated candidate counts and cost
// orderings (ties included), the search ladders against brute-force
// full-search replicas, and the recon chains against the landed l3
// transforms.
#include <doctest.h>
#include <cstdint>
#include <cstring>
#include <vector>

#include <md_core.h>

namespace mdc = pipeline::mdc;

namespace {

// The 8-bit variance transcription oracle (C_DEFAULT/variance.c VAR(W,H)
// shape): sse + sum over the a/b window, return = sse - sum*sum/area with
// the truncating division.
struct VarOracle {
    std::uint32_t sse;
    std::uint32_t variance(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride, int w, int h) {
        std::int32_t sum = 0;
        std::uint64_t sq = 0;
        for (int r = 0; r < h; ++r) {
            for (int c = 0; c < w; ++c) {
                const std::int32_t d = static_cast<std::int32_t>(a[r * aStride + c]) -
                    static_cast<std::int32_t>(b[r * bStride + c]);
                sum += d;
                sq += static_cast<std::uint64_t>(d) * static_cast<std::uint64_t>(d);
            }
        }
        sse = static_cast<std::uint32_t>(sq);
        return sse - static_cast<std::uint32_t>((static_cast<std::int64_t>(sum) * sum) / (w * h));
    }
};

}  // namespace

// trip 1: the mefn vf dispatch (av1me.h:57-69 AomVarianceFnPtr.svf/vf) - the
// bsize -> (W, H) mapping. Oracle: the test-side VAR transcription at the
// dims each BlockSize names.
TEST_CASE("l6 mdc mefnVf dispatches bsize to the right-dim variance") {
    VarOracle o;
    std::uint8_t a[128 * 128];
    std::uint8_t b[128 * 128];
    for (int i = 0; i < 128 * 128; ++i) {
        a[i] = static_cast<std::uint8_t>(i * 5 + 3);
        b[i] = static_cast<std::uint8_t>(i * 3 + 11);
    }
    struct Case {
        entropy::BlockSize bsize;
        int w;
        int h;
    };
    const Case cases[] = {
        {entropy::BLOCK_4X4, 4, 4},     {entropy::BLOCK_8X8, 8, 8},     {entropy::BLOCK_16X16, 16, 16},
        {entropy::BLOCK_32X32, 32, 32}, {entropy::BLOCK_8X16, 8, 16},   {entropy::BLOCK_16X8, 16, 8},
        {entropy::BLOCK_4X8, 4, 8},     {entropy::BLOCK_8X4, 8, 4},     {entropy::BLOCK_32X64, 32, 64},
        {entropy::BLOCK_64X32, 64, 32}, {entropy::BLOCK_4X16, 4, 16},   {entropy::BLOCK_16X4, 16, 4},
        {entropy::BLOCK_8X32, 8, 32},   {entropy::BLOCK_32X8, 32, 8},   {entropy::BLOCK_16X64, 16, 64},
        {entropy::BLOCK_64X16, 64, 16},
    };
    for (const Case& tc : cases) {
        mdc::VarianceFn vf = mdc::mefnVf(tc.bsize);
        REQUIRE(vf != nullptr);
        std::uint32_t sse = 0;
        const std::uint32_t got = vf(a, 128, b, 128, &sse);
        const std::uint32_t want = o.variance(a, 128, b, 128, tc.w, tc.h);
        CHECK(sse == o.sse);
        CHECK_MESSAGE(got == want, "bsize dispatch dims wrong");
    }
}

// Stub prediction / fast-cost callables: record which table slot fired.
namespace {
int g_pred_calls[2];
l16_rdo::PredictionMode g_pred_mode_seen[2];
void predIntra(mdc::FlCtx& ctx, mdc::MdCandBf& bf) {
    (void)ctx;
    g_pred_calls[0]++;
    g_pred_mode_seen[0] = bf.cand->mode;
}
void predInter(mdc::FlCtx& ctx, mdc::MdCandBf& bf) {
    (void)ctx;
    g_pred_calls[1]++;
    g_pred_mode_seen[1] = bf.cand->mode;
}
std::uint64_t fastCostIntra(mdc::FlCtx& ctx, mdc::MdCandBf& bf, std::uint64_t lambda, std::uint64_t dist) {
    (void)ctx;
    (void)bf;
    (void)lambda;
    return dist + 1000;  // a recognizable intra signature
}
std::uint64_t fastCostInter(mdc::FlCtx& ctx, mdc::MdCandBf& bf, std::uint64_t lambda, std::uint64_t dist) {
    (void)ctx;
    (void)bf;
    (void)lambda;
    return dist + 2000;  // a recognizable inter signature
}
}  // namespace

// trip 2: fast_loop_core_pd0 (product_coding_loop.c:966-:979): prediction
// via the pd0 table slot selected by is_inter_mode, then the variance at
// (cu_origin, input_origin) written to *fast_cost.
TEST_CASE("l6 mdc fastLoopCorePd0 predicts through the mode slot and stores the variance") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.geom.sq_size = 8;
    ctx.predTable[0] = predIntra;
    ctx.predTable[1] = predInter;
    g_pred_calls[0] = g_pred_calls[1] = 0;

    std::uint8_t src[16 * 16];
    std::uint8_t predBuf[16 * 16];
    for (int i = 0; i < 256; ++i) {
        src[i] = static_cast<std::uint8_t>(i * 7 + 1);
        predBuf[i] = static_cast<std::uint8_t>(i * 5 + 2);
    }
    mdc::PicBuf input{};
    input.buf = src;
    input.stride = 16;
    mdc::PicBuf predP{};
    predP.buf = predBuf;
    predP.stride = 16;

    std::uint64_t fast_cost = 0xDEADBEEF;
    mdc::MdCand cand{};
    cand.mode = l16_rdo::PAETH_PRED;  // intra -> table slot 0
    mdc::MdCandBf bf{};
    bf.cand = &cand;
    bf.pred = predP;
    bf.fast_cost = &fast_cost;

    mdc::fastLoopCorePd0(&bf, &ctx, input, 5 * 16 + 3, 2 * 16 + 1);

    CHECK(g_pred_calls[0] == 1);
    CHECK(g_pred_calls[1] == 0);
    // The variance is taken at cu_origin (pred) vs input_origin (src).
    VarOracle o;
    const std::uint32_t want = o.variance(predBuf + 2 * 16 + 1, 16, src + 5 * 16 + 3, 16, 8, 8);
    CHECK(fast_cost == want);
}

// trip 3: fast_loop_core_light_pd1 (product_coding_loop.c:981-:1038). The
// three decision gates over poisoned carriers: (a) the cand-elimination
// intra skip (best_dist < th with the DC/non-DC th fold), (b) the
// distortion-cost early exit (RDCOST(lambda, 0, dist<<4) > mds0_best_cost),
// (c) the fast-cost write (shut_fast_rate -> raw dist; else the mode-slot
// fast-cost fn). RDCOST(rd_cost.h:36-38) at rate 0 = dist << RDDIV_BITS(7).
TEST_CASE("l6 mdc fastLoopCoreLightPd1 runs the elimination and early-exit gates") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.geom.sq_size = 8;
    ctx.predTable[0] = predIntra;
    ctx.predTable[1] = predInter;
    ctx.fastCostTable[0] = fastCostIntra;
    ctx.fastCostTable[1] = fastCostInter;
    ctx.full_lambda_md[0] = 100;
    g_pred_calls[0] = g_pred_calls[1] = 0;

    std::uint8_t src[16 * 16];
    std::uint8_t predBuf[16 * 16];
    for (int i = 0; i < 256; ++i) {
        src[i] = static_cast<std::uint8_t>(i * 7 + 1);
        predBuf[i] = static_cast<std::uint8_t>(i * 5 + 2);
    }
    mdc::PicBuf input{};
    input.buf = src;
    input.stride = 16;
    mdc::PicBuf predP{};
    predP.buf = predBuf;
    predP.stride = 16;

    // A best-buffer carrying a known luma_fast_dist.
    std::uint64_t best_cost_slot = 0;
    mdc::MdCand best_cand{};
    best_cand.mode = l16_rdo::PAETH_PRED;
    mdc::MdCandBf best_bf{};
    best_bf.cand = &best_cand;
    best_bf.fast_cost = &best_cost_slot;
    best_bf.luma_fast_dist = 10;
    mdc::MdCandBf* bufs[2] = {&best_bf, nullptr};
    ctx.cand_bf_ptr_array = bufs;
    ctx.mds0_best_idx = 0;

    // (a) elimination: intra candidate, elim enabled, best_dist 10 < th.
    // th = dc_only_th(=5) * area(64) = 320 -> skip fires. The elimination
    // gate sits inside `mds0_best_cost != ~0`, so seed a finite best cost.
    ctx.cand_elim_enabled = 1;
    ctx.dc_only_th = 5;
    ctx.skip_dc_th = 3;
    ctx.mds0_best_cost = 1000000000000ull;

    std::uint64_t fast_cost = 12345;
    mdc::MdCand cand{};
    cand.mode = l16_rdo::V_PRED;  // intra, non-DC -> dc_only_th path
    mdc::MdCandBf bf{};
    bf.cand = &cand;
    bf.pred = predP;
    bf.fast_cost = &fast_cost;
    mdc::fastLoopCoreLightPd1(&bf, &ctx, input, 0);
    CHECK(fast_cost == mdc::kMaxModeCost);
    CHECK(g_pred_calls[0] == 0);  // skipped before prediction
    // DC candidate uses skip_dc_th instead: th = 3*64 = 192 > 10 -> skip.
    cand.mode = l16_rdo::DC_PRED;
    fast_cost = 12345;
    mdc::fastLoopCoreLightPd1(&bf, &ctx, input, 0);
    CHECK(fast_cost == mdc::kMaxModeCost);
    // th = 0 -> no elimination (10 < 0 false); the prediction + variance
    // path runs.
    ctx.dc_only_th = 0;
    cand.mode = l16_rdo::V_PRED;
    fast_cost = 12345;
    mdc::fastLoopCoreLightPd1(&bf, &ctx, input, 0);
    CHECK(g_pred_calls[0] == 1);
    VarOracle o;
    const std::uint32_t var = o.variance(predBuf, 16, src, 16, 8, 8);
    // (b) early exit: distortion_cost = RDCOST(100, 0, var<<4) = var<<11.
    const std::uint64_t dist_cost = static_cast<std::uint64_t>(var) << 11;
    ctx.mds0_best_cost = dist_cost - 1;
    fast_cost = 12345;
    mdc::fastLoopCoreLightPd1(&bf, &ctx, input, 0);
    CHECK(fast_cost == mdc::kMaxModeCost);
    // (c) shut_fast_rate: fast_cost = luma_fast_dist = var << 4. The ~0
    // best cost silences both gates.
    ctx.mds0_best_cost = 0xFFFFFFFFFFFFFFFFull;
    ctx.shut_fast_rate = 1;
    fast_cost = 12345;
    mdc::fastLoopCoreLightPd1(&bf, &ctx, input, 0);
    CHECK(fast_cost == (static_cast<std::uint64_t>(var) << 4));
    CHECK(bf.fast_luma_rate == 0);
    CHECK(bf.fast_chroma_rate == 0);
    CHECK(bf.full_dist == o.sse);
    // (d) the fast-cost table: intra slot with the shifted dist.
    ctx.shut_fast_rate = 0;
    fast_cost = 12345;
    mdc::fastLoopCoreLightPd1(&bf, &ctx, input, 0);
    CHECK(fast_cost == (static_cast<std::uint64_t>(var) << 4) + 1000);
    // inter candidate routes to the inter slot.
    cand.mode = l16_rdo::NEARESTMV;
    fast_cost = 12345;
    g_pred_calls[1] = 0;
    mdc::fastLoopCoreLightPd1(&bf, &ctx, input, 0);
    CHECK(g_pred_calls[1] == 1);
    CHECK(fast_cost == (static_cast<std::uint64_t>(var) << 4) + 2000);
}
