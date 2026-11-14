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
#include <cstdio>
#include <cstring>
#include <vector>

#include <amvp.h>
#include <md_core.h>

namespace mdc = pipeline::mdc;
namespace amvp = motion::amvp;

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

// OBMC stubs: the threaded walls (svt_aom_obmc_motion_mode_allowed +
// svt_aom_inter_pu_prediction_av1_obmc). The prediction stub repaints the
// pred buffer so the OBMC variance differs from the simple-translation one.
namespace {
int g_obmc_pred_calls = 0;
bool g_obmc_allowed = true;
int g_last_motion_mode = 1;  // OBMC_CAUSAL
bool obmcAllowedStub(const mdc::FlCtx& ctx, const mdc::MdCand& cand) {
    (void)ctx;
    (void)cand;
    return g_obmc_allowed;
}
int motionModeAllowedStub(const mdc::FlCtx& ctx, const mdc::MdCand& cand) {
    (void)ctx;
    (void)cand;
    return g_last_motion_mode;
}
void obmcPredStub(mdc::FlCtx& ctx, mdc::MdCandBf& bf) {
    (void)ctx;
    // Repaint the 8x8 pred block at the origin with zeros so the OBMC
    // variance differs from the simple-translation one.
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) bf.pred.buf[r * bf.pred.stride + c] = 0;
    g_obmc_pred_calls++;
}
}  // namespace

// trip 4: obmc_trans_face_off (product_coding_loop.c:1040-:1136). The
// face-off: save the simple-translation results, derive the OBMC rate
// (fast_luma_rate + num_obmc - num_translation), predict via the OBMC
// driver, re-measure, cost = RDCOST(full_lambda, luma+chroma, dist<<4),
// keep OBMC when cheaper else restore + valid_luma_pred = 0.
TEST_CASE("l6 mdc obmcTransFaceOff keeps the cheaper motion mode") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.geom.sq_size = 8;
    ctx.full_lambda_md[0] = 100;
    ctx.obmcPredFn = obmcPredStub;
    ctx.obmcAllowedFn = obmcAllowedStub;
    ctx.motionModeAllowedFn = motionModeAllowedStub;
    // motion_mode_fac_bits1[8X8] = {10 translation, 25 obmc} (row bsize).
    std::int32_t fac1[entropy::BLOCK_SIZES_ALL * 2] = {};
    fac1[entropy::BLOCK_8X8 * 2 + 0] = 10;
    fac1[entropy::BLOCK_8X8 * 2 + 1] = 25;
    ctx.motionModeFacBits1 = fac1;

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

    mdc::MdCand cand{};
    cand.mode = l16_rdo::NEARESTMV;  // single-ref inter
    cand.ref_frame[0] = amvp::kLastFrame;
    cand.ref_frame[1] = amvp::kNoneFrame;
    cand.motion_mode = mdc::kSimpleTranslation;
    cand.is_interintra_used = 0;
    std::uint64_t fast_cost = 5000000;
    std::uint64_t full_cost = 0;
    mdc::MdCandBf bf{};
    bf.cand = &cand;
    bf.pred = predP;
    bf.fast_cost = &fast_cost;
    bf.full_cost = &full_cost;
    bf.fast_luma_rate = 100;
    bf.fast_chroma_rate = 7;
    bf.luma_fast_dist = 0;
    bf.valid_luma_pred = 0;

    VarOracle o;
    // Scenario 1: the OBMC prediction zeroes the pred block, so the OBMC
    // distortion is huge while the seeded simple cost is 1 -> OBMC loses
    // and the simple-translation results restore.
    fast_cost = 1;
    g_obmc_pred_calls = 0;
    mdc::obmcTransFaceOff(&bf, &ctx, input, 0);
    CHECK(g_obmc_pred_calls == 1);
    CHECK(cand.motion_mode == mdc::kSimpleTranslation);  // restored
    CHECK(fast_cost == 1);                               // restored
    CHECK(bf.fast_luma_rate == 100);
    CHECK(bf.fast_chroma_rate == 7);
    CHECK(bf.valid_luma_pred == 0);

    // Scenario 2: OBMC wins. The pred block is src ^ 1 (a known distortion),
    // the simple cost is seeded far above the zeroed-pred OBMC cost.
    for (int i = 0; i < 256; ++i) predBuf[i] = static_cast<std::uint8_t>(src[i] ^ 1);
    fast_cost = 0xFFFFFFFFFFFFFFFFull;
    bf.fast_luma_rate = 100;
    bf.valid_luma_pred = 0;
    mdc::obmcTransFaceOff(&bf, &ctx, input, 0);
    CHECK(cand.motion_mode == mdc::kObmcCausal);
    CHECK(bf.valid_luma_pred == 1);
    CHECK(bf.fast_luma_rate == 115);  // 100 + 25 - 10
    CHECK(g_obmc_pred_calls == 2);
    // fast_cost = RDCOST(100, 115 + 7, dObmc<<4); the zeroed OBMC pred vs
    // src gives dObmc = sum((src-128... the population variance of src)).
    const std::uint32_t dObmc = o.variance(predBuf, 16, src, 16, 8, 8);  // pred is zeros now
    const std::uint64_t rate = 115 + 7;
    const std::uint64_t want = (static_cast<std::uint64_t>((rate * 100 + 256)) >> 9) +
        (static_cast<std::uint64_t>(dObmc) << 4) * 128;
    CHECK(fast_cost == want);
}

// trip 5: fast_loop_core (product_coding_loop.c:1209-:1293). The hadamard
// vs variance distortion paths, the mds0 pruning gates (per-class and
// global), the fast cost, valid_luma_pred, the obmc hand-off and the
// full-cost init.
TEST_CASE("l6 mdc fastLoopCore routes hadamard and variance and prunes") {
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

    mdc::MdCand cand{};
    cand.mode = l16_rdo::DC_PRED;
    std::uint64_t fast_cost = 12345;
    std::uint64_t full_cost = 999;
    mdc::MdCandBf bf{};
    bf.cand = &cand;
    bf.pred = predP;
    bf.fast_cost = &fast_cost;
    bf.full_cost = &full_cost;

    // (a) the variance path (mds0_use_hadamard_blk = 0), pruning off.
    ctx.mds0_use_hadamard_blk = 0;
    ctx.pruning_method_th = 0;
    ctx.mds0_best_cost = 0xFFFFFFFFFFFFFFFFull;
    ctx.shut_fast_rate = 0;
    mdc::fastLoopCore(&bf, &ctx, input, 0);
    VarOracle o;
    const std::uint32_t var = o.variance(predBuf, 16, src, 16, 8, 8);
    CHECK(g_pred_calls[0] == 1);
    CHECK(bf.luma_fast_dist == var);
    CHECK(fast_cost == (static_cast<std::uint64_t>(var) << 4) + 1000);
    CHECK(bf.valid_luma_pred == 1);
    CHECK(full_cost == fast_cost);

    // (b) the hadamard path: the satd from the landed hadamardPath on the
    // same buffers is the luma_fast_dist.
    ctx.mds0_use_hadamard_blk = 1;
    std::vector<std::int16_t> residual(16 * 16);
    std::vector<std::int32_t> coeff(1024);
    bf.residual.buf = reinterpret_cast<std::uint8_t*>(residual.data());
    bf.residual.stride = 16;
    ctx.txCoeffBuf = coeff.data();
    mdc::fastLoopCore(&bf, &ctx, input, 0);
    std::uint32_t satd_ref = 0;
    pipeline::hadamardPath(src, 16, 0, predBuf, 16, residual.data(), 16, coeff.data(), entropy::BLOCK_8X8, false,
                           &satd_ref);
    CHECK(bf.luma_fast_dist == satd_ref);
    CHECK(fast_cost == (static_cast<std::uint64_t>(satd_ref) << 4) + 1000);

    // (c) the global pruning gate (branch B: pruning_method_th == ~0).
    ctx.mds0_use_hadamard_blk = 0;
    ctx.pruning_method_th = 0xFF;
    ctx.pd_pass = 1;
    ctx.dist_to_cost_th = 50;
    ctx.mds0_best_cost = 1000;
    fast_cost = 12345;
    mdc::fastLoopCore(&bf, &ctx, input, 0);
    CHECK(fast_cost == mdc::kMaxModeCost);
    // The distortion cost is var*2048: (100*(var*2048 - 1000)) must exceed
    // 1000*50 for the prune; prove the non-prune side by lifting the best
    // cost above it.
    ctx.mds0_best_cost = static_cast<std::uint64_t>(var) * 2048;
    fast_cost = 12345;
    mdc::fastLoopCore(&bf, &ctx, input, 0);
    CHECK(fast_cost == (static_cast<std::uint64_t>(var) << 4) + 1000);

    // (d) the per-class gate (branch A): pruning_method_th = 1 and the
    // min me/pme distortion over area above it.
    ctx.pruning_method_th = 1;
    ctx.md_me_dist = 8000;
    ctx.md_pme_dist = 9000;  // min 8000/64 = 125 > 1
    ctx.per_class_dist_to_cost_th[0] = 50;
    ctx.mds0_best_cost_per_class[0] = 1000;
    ctx.mds0_best_cost = static_cast<std::uint64_t>(var) * 2048;  // no global prune
    fast_cost = 12345;
    mdc::fastLoopCore(&bf, &ctx, input, 0);
    CHECK(fast_cost == mdc::kMaxModeCost);
    // The class gate passes when the best per-class cost is ~0.
    ctx.mds0_best_cost_per_class[0] = 0xFFFFFFFFFFFFFFFFull;
    fast_cost = 12345;
    mdc::fastLoopCore(&bf, &ctx, input, 0);
    CHECK(fast_cost == (static_cast<std::uint64_t>(var) << 4) + 1000);

    // (e) the obmc hand-off: obmc_ctrls.enabled && trans_face_off == 1
    // routes into obmc_trans_face_off (the stub counts).
    ctx.obmc_enabled = 1;
    ctx.obmc_trans_face_off = 1;
    ctx.obmcAllowedFn = obmcAllowedStub;
    ctx.motionModeAllowedFn = motionModeAllowedStub;
    ctx.obmcPredFn = obmcPredStub;
    std::int32_t fac1[entropy::BLOCK_SIZES_ALL * 2] = {};
    fac1[entropy::BLOCK_8X8 * 2 + 0] = 10;
    fac1[entropy::BLOCK_8X8 * 2 + 1] = 25;
    ctx.motionModeFacBits1 = fac1;
    cand.mode = l16_rdo::NEARESTMV;
    cand.ref_frame[0] = amvp::kLastFrame;
    cand.ref_frame[1] = amvp::kNoneFrame;
    cand.motion_mode = mdc::kSimpleTranslation;
    cand.is_interintra_used = 0;
    g_obmc_pred_calls = 0;
    mdc::fastLoopCore(&bf, &ctx, input, 0);
    CHECK(g_obmc_pred_calls == 1);
    // The zeroed OBMC pred loses to the simple cost here -> restore. The
    // mode slot is INTER (NEARESTMV) so the fast-cost signature is +2000.
    CHECK(cand.motion_mode == mdc::kSimpleTranslation);
    CHECK(fast_cost == (static_cast<std::uint64_t>(var) << 4) + 2000);
    CHECK(full_cost == fast_cost);
}

// trip 6: process_cand_itr (product_coding_loop.c:1500-:1580) - the MDS0
// iteration gate over the three ctrl combinations and the two iterations.
// The sentinel FILTER_INTRA_MODES = 5 (l16 rate_est); FILTER_DC_PRED = 0.
TEST_CASE("l6 mdc processCandItr gates the three itr-0 ctrl combinations") {
    mdc::FlCtx ctx{};
    mdc::MdCand cand{};
    std::vector<std::uint64_t> reg(13, 0xFFFFFFFFFFFFFFFFull);

    // (i) reduce_filter_intra + any skip_angular: regular only at itr 0.
    ctx.reduce_filter_intra = 1;
    ctx.skip_angular_delta1_th = 10;
    ctx.skip_angular_delta2_th = -1;
    ctx.skip_angular_delta3_th = -1;
    cand.angle_delta_y = 0;
    cand.filter_intra_mode = 5;  // FILTER_INTRA_MODES sentinel
    CHECK(mdc::processCandItr(ctx, cand, 0, l16_rdo::DC_PRED, 1000, reg.data()));
    cand.angle_delta_y = 2;
    CHECK_FALSE(mdc::processCandItr(ctx, cand, 0, l16_rdo::DC_PRED, 1000, reg.data()));
    cand.angle_delta_y = 0;
    cand.filter_intra_mode = 1;  // a filter mode
    CHECK_FALSE(mdc::processCandItr(ctx, cand, 0, l16_rdo::DC_PRED, 1000, reg.data()));

    // (ii) angular only (reduce_filter_intra off): regular + filter pass,
    // angular rejected.
    ctx.reduce_filter_intra = 0;
    cand.angle_delta_y = 1;
    cand.filter_intra_mode = 5;
    CHECK_FALSE(mdc::processCandItr(ctx, cand, 0, l16_rdo::DC_PRED, 1000, reg.data()));
    cand.angle_delta_y = 0;
    CHECK(mdc::processCandItr(ctx, cand, 0, l16_rdo::DC_PRED, 1000, reg.data()));
    cand.filter_intra_mode = 2;
    CHECK(mdc::processCandItr(ctx, cand, 0, l16_rdo::DC_PRED, 1000, reg.data()));

    // (iii) reduce_filter_intra only (no angular ths): regular + angular
    // pass, filter rejected.
    ctx.skip_angular_delta1_th = -1;
    ctx.reduce_filter_intra = 1;
    cand.angle_delta_y = 3;
    cand.filter_intra_mode = 5;
    CHECK(mdc::processCandItr(ctx, cand, 0, l16_rdo::DC_PRED, 1000, reg.data()));
    cand.angle_delta_y = 0;
    cand.filter_intra_mode = 4;
    CHECK_FALSE(mdc::processCandItr(ctx, cand, 0, l16_rdo::DC_PRED, 1000, reg.data()));
}

TEST_CASE("l6 mdc processCandItr gates itr 1 and the angular cost bands") {
    mdc::FlCtx ctx{};
    mdc::MdCand cand{};
    std::vector<std::uint64_t> reg(13, 0xFFFFFFFFFFFFFFFFull);

    // (iv) reduce_filter_intra + angular: itr 1 evals angular + filter,
    // rejects the regular candidate.
    ctx.reduce_filter_intra = 1;
    ctx.skip_angular_delta1_th = 10;
    ctx.skip_angular_delta2_th = -1;
    ctx.skip_angular_delta3_th = -1;
    cand.angle_delta_y = 0;
    cand.filter_intra_mode = 5;
    CHECK_FALSE(mdc::processCandItr(ctx, cand, 1, l16_rdo::DC_PRED, 1000, reg.data()));
    // Angular candidate survives the shape gate, then the cost band: the
    // best_reg mode differs -> the |delta|=1 band applies. reg_cost for the
    // mode is 2000, best 1000 -> (2000-1000)*100 = 100000 > 10*1000 ->
    // reject.
    cand.mode = l16_rdo::V_PRED;
    cand.angle_delta_y = 1;
    reg[l16_rdo::V_PRED] = 2000;
    CHECK_FALSE(mdc::processCandItr(ctx, cand, 1, l16_rdo::SMOOTH_PRED, 1000, reg.data()));
    // Lift the th: (100000 <= 100*1000) -> accept.
    ctx.skip_angular_delta1_th = 100;
    CHECK(mdc::processCandItr(ctx, cand, 1, l16_rdo::SMOOTH_PRED, 1000, reg.data()));
    // Same parent mode -> no band check at all.
    ctx.skip_angular_delta1_th = 10;
    CHECK(mdc::processCandItr(ctx, cand, 1, l16_rdo::V_PRED, 1000, reg.data()));
    // The |delta|=2 band: th1 is set but does not gate a delta-2
    // candidate; the th2 band decides.
    cand.angle_delta_y = 2;
    reg[l16_rdo::V_PRED] = 2000;
    ctx.skip_angular_delta2_th = -1;
    CHECK(mdc::processCandItr(ctx, cand, 1, l16_rdo::SMOOTH_PRED, 1000, reg.data()));
    ctx.skip_angular_delta2_th = 5;  // 100000 > 5*1000 -> reject
    CHECK_FALSE(mdc::processCandItr(ctx, cand, 1, l16_rdo::SMOOTH_PRED, 1000, reg.data()));
    ctx.skip_angular_delta2_th = 100;  // 100000 <= 100*1000 -> accept
    CHECK(mdc::processCandItr(ctx, cand, 1, l16_rdo::SMOOTH_PRED, 1000, reg.data()));
    // The |delta|=3 band uses th3.
    cand.angle_delta_y = 3;
    ctx.skip_angular_delta2_th = -1;
    ctx.skip_angular_delta3_th = 5;  // 100000 > 5*1000 -> reject
    CHECK_FALSE(mdc::processCandItr(ctx, cand, 1, l16_rdo::SMOOTH_PRED, 1000, reg.data()));
    ctx.skip_angular_delta3_th = 100;
    CHECK(mdc::processCandItr(ctx, cand, 1, l16_rdo::SMOOTH_PRED, 1000, reg.data()));

    // (v) itr 1 filter candidates: FILTER_DC_PRED always tested; the others
    // only when fimode_to_intramode[fm] == best_reg_intra_mode.
    ctx.reduce_filter_intra = 0;
    ctx.skip_angular_delta1_th = -1;
    ctx.skip_angular_delta2_th = -1;
    ctx.skip_angular_delta3_th = -1;
    ctx.reduce_filter_intra = 1;  // the filter-only branch
    cand.angle_delta_y = 0;
    cand.filter_intra_mode = 0;  // FILTER_DC_PRED
    CHECK(mdc::processCandItr(ctx, cand, 1, l16_rdo::SMOOTH_PRED, 1000, reg.data()));
    cand.filter_intra_mode = 1;  // FILTER_V_PRED -> fimode_to_intramode[1] = V_PRED
    CHECK(mdc::processCandItr(ctx, cand, 1, l16_rdo::V_PRED, 1000, reg.data()));
    CHECK_FALSE(mdc::processCandItr(ctx, cand, 1, l16_rdo::H_PRED, 1000, reg.data()));
    // The regular candidate at itr 1 with reduce-only: filter sentinel
    // candidates are rejected.
    cand.filter_intra_mode = 5;
    CHECK_FALSE(mdc::processCandItr(ctx, cand, 1, l16_rdo::V_PRED, 1000, reg.data()));
}

// Mode-indexed stubs: the prediction stub fills the 8x8 pred block with
// (i + mode) & 0xFF so the variance (the pd0 cost) is mode-dependent and
// oracle-computable; the fast-cost stubs give mode-indexed signatures.
namespace {
void predFillByMode(mdc::FlCtx& ctx, mdc::MdCandBf& bf) {
    (void)ctx;
    const std::uint8_t m = static_cast<std::uint8_t>(bf.cand->mode);
    // A mode-dependent SHAPE (not a shift): variance varies with the mode.
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c)
            bf.pred.buf[r * bf.pred.stride + c] =
                static_cast<std::uint8_t>((r * bf.pred.stride + c) * (m + 1) & 0xFF);
}
std::uint64_t fastCostByMode(mdc::FlCtx& ctx, mdc::MdCandBf& bf, std::uint64_t lambda, std::uint64_t dist) {
    (void)ctx;
    (void)lambda;
    (void)dist;
    return 1000u * (static_cast<unsigned>(bf.cand->mode) + 1) + 7;
}
}  // namespace

// trip 7: md_stage_0_pd0 (product_coding_loop.c:1448-:1464) - the two-slot
// ping-pong: the buffer flips only when a new best appears; ties keep the
// earlier buffer. The pd0 cost IS the variance (fast_loop_core_pd0 writes
// the variance straight into *fast_cost).
TEST_CASE("l6 mdc mdStage0Pd0 ping-pongs the buffers on new bests only") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.geom.sq_size = 8;
    ctx.predTable[0] = predFillByMode;
    ctx.predTable[1] = predFillByMode;
    ctx.mds0_best_cost = 0xFFFFFFFFFFFFFFFFull;

    std::uint8_t srcBuf[16 * 16] = {};
    mdc::PicBuf input{};
    input.buf = srcBuf;
    input.stride = 16;
    std::uint8_t predStorage[16 * 16] = {};
    mdc::PicBuf predP{};
    predP.buf = predStorage;
    predP.stride = 16;

    VarOracle o;
    // The per-mode variance of the stub-filled block vs the zero source.
    // The stub writes the strided 8x8 window (r*stride + c), so the
    // scratch replicates that layout, not a contiguous 64-run.
    const int kModes[3] = {l16_rdo::DC_PRED, l16_rdo::H_PRED, l16_rdo::PAETH_PRED};
    std::uint8_t scratch[16 * 16] = {};
    std::uint64_t wantVar[3];
    for (int k = 0; k < 3; ++k) {
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c)
                scratch[r * 16 + c] = static_cast<std::uint8_t>((r * 16 + c) * (kModes[k] + 1) & 0xFF);
        wantVar[k] = o.variance(scratch, 16, srcBuf, 16, 8, 8);
    }
    REQUIRE(wantVar[0] != wantVar[1]);
    REQUIRE(wantVar[1] != wantVar[2]);

    mdc::MdCand cands[3]{};
    cands[0].mode = static_cast<l16_rdo::PredictionMode>(kModes[0]);
    cands[1].mode = static_cast<l16_rdo::PredictionMode>(kModes[1]);
    cands[2].mode = static_cast<l16_rdo::PredictionMode>(kModes[2]);
    std::uint64_t costs[3] = {};
    mdc::MdCandBf bufs[2];
    for (int i = 0; i < 2; ++i) {
        bufs[i].cand = nullptr;
        bufs[i].pred = predP;
        bufs[i].fast_cost = &costs[i];
        bufs[i].full_cost = &costs[i];
    }
    mdc::MdCandBf* ptrs[2] = {&bufs[0], &bufs[1]};
    ctx.cand_bf_ptr_array = ptrs;

    mdc::mdStage0Pd0(&ctx, input, 0, 0, cands, 3);
    // cand0 (wantVar0) wins at buf0 and flips; cand1 (wantVar1) compares
    // against wantVar0; cand2 lands in the other slot. The winner is the
    // candidate whose variance is smallest.
    const int best_k = wantVar[0] < wantVar[1] ? (wantVar[0] < wantVar[2] ? 0 : 2)
                                               : (wantVar[1] < wantVar[2] ? 1 : 2);
    const std::uint64_t best_cost = wantVar[best_k];
    CHECK(ctx.mds0_best_cost == best_cost);
    CHECK(ctx.mds0_best_idx == (best_k == 0 ? 0 : 1));
    // The last candidate processed (index 2) sits in the slot NOT flipped
    // by the best: the flip only happened on new bests.
    CHECK((bufs[0].cand == &cands[2] || bufs[1].cand == &cands[2]));

    // Tie case: two candidates with the same mode produce equal variances;
    // the second does not steal the best (strict <).
    ctx.mds0_best_cost = 0xFFFFFFFFFFFFFFFFull;
    cands[1].mode = cands[0].mode;
    mdc::mdStage0Pd0(&ctx, input, 0, 0, cands, 2);
    CHECK(ctx.mds0_best_cost == wantVar[0]);
    CHECK(ctx.mds0_best_idx == 0);
}

// trip 8: md_stage_0_light_pd1 (product_coding_loop.c:1466-:1498) - the
// interp-filter default (SWITCHABLE -> 0, else the broadcast pair), the
// mds_do_chroma write and the same ping-pong over light-PD1 costs
// (shut_fast_rate -> the variance << 4).
TEST_CASE("l6 mdc mdStage0LightPd1 seeds interp filters and walks the slots") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.geom.sq_size = 8;
    ctx.predTable[0] = predFillByMode;
    ctx.predTable[1] = predFillByMode;
    ctx.fastCostTable[0] = fastCostByMode;
    ctx.fastCostTable[1] = fastCostByMode;
    ctx.shut_fast_rate = 1;
    ctx.mds0_best_cost = 0xFFFFFFFFFFFFFFFFull;

    std::uint8_t srcBuf[16 * 16] = {};
    mdc::PicBuf input{};
    input.buf = srcBuf;
    input.stride = 16;
    std::uint8_t predStorage[16 * 16] = {};
    mdc::PicBuf predP{};
    predP.buf = predStorage;
    predP.stride = 16;

    VarOracle o;
    std::uint8_t scratch[16 * 16] = {};
    const int kModes[2] = {l16_rdo::DC_PRED, l16_rdo::H_PRED};
    std::uint64_t wantVar[2];
    for (int k = 0; k < 2; ++k) {
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c)
                scratch[r * 16 + c] = static_cast<std::uint8_t>((r * 16 + c) * (kModes[k] + 1) & 0xFF);
        wantVar[k] = o.variance(scratch, 16, srcBuf, 16, 8, 8);
    }

    mdc::MdCand cands[2]{};
    cands[0].mode = static_cast<l16_rdo::PredictionMode>(kModes[0]);
    cands[1].mode = static_cast<l16_rdo::PredictionMode>(kModes[1]);
    std::uint64_t costs[2] = {};
    mdc::MdCandBf bufs[2];
    for (int i = 0; i < 2; ++i) {
        bufs[i].cand = nullptr;
        bufs[i].pred = predP;
        bufs[i].fast_cost = &costs[i];
        bufs[i].full_cost = &costs[i];
    }
    mdc::MdCandBf* ptrs[2] = {&bufs[0], &bufs[1]};

    // SWITCHABLE (= 4) -> the 0 default.
    mdc::mdStage0LightPd1(&ctx, ptrs, cands, 2, input, 0, mdc::kSwitchable);
    CHECK(ctx.mds_do_chroma == 0);
    CHECK(cands[0].interp_filters == 0);
    CHECK(cands[1].interp_filters == 0);
    CHECK(cands[0].tx_depth == 0);
    CHECK(ctx.mds0_best_cost == (wantVar[0] < wantVar[1] ? wantVar[0] << 4 : wantVar[1] << 4));
    CHECK(ctx.mds0_best_idx == (wantVar[0] < wantVar[1] ? 0 : 1));

    // A fixed filter broadcasts to the packed pair.
    ctx.mds0_best_cost = 0xFFFFFFFFFFFFFFFFull;
    mdc::mdStage0LightPd1(&ctx, ptrs, cands, 2, input, 0, mdc::kEighttapRegular);
    CHECK(cands[0].interp_filters == mdc::av1BroadcastInterpFilter(mdc::kEighttapRegular));
}

// trip 9: md_stage_0 (product_coding_loop.c:1582-:1714) - the iterated
// class-filtered walk: the regular-cost tracking, the prune_using_best_mode
// skips, the buffer rotation over max_buffers, the mds1 count clamp and the
// MAX_CU_COST scratch poison.
TEST_CASE("l6 mdc mdStage0 walks two iterations with the class filter and poisons the scratch") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.geom.sq_size = 8;
    ctx.predTable[0] = predFillByMode;
    ctx.predTable[1] = predFillByMode;
    ctx.fastCostTable[0] = fastCostByMode;
    ctx.fastCostTable[1] = fastCostByMode;
    ctx.shut_fast_rate = 1;
    ctx.mds0_best_cost = 0xFFFFFFFFFFFFFFFFull;
    for (int i = 0; i < 5; ++i) ctx.mds0_best_cost_per_class[i] = 0xFFFFFFFFFFFFFFFFull;
    ctx.target_class = 0;
    ctx.reduce_filter_intra = 1;  // -> tot_itr = 2
    ctx.skip_angular_delta1_th = -1;
    ctx.skip_angular_delta2_th = -1;
    ctx.skip_angular_delta3_th = -1;
    ctx.prune_using_best_mode = 0;
    ctx.ifs_level = 1;  // IFS_MDS0 -> mds_do_ifs = 1

    std::uint8_t srcBuf[16 * 16] = {};
    mdc::PicBuf input{};
    input.buf = srcBuf;
    input.stride = 16;
    std::uint8_t predStorage[3][16 * 16];
    std::memset(predStorage, 0, sizeof(predStorage));

    VarOracle o;
    std::uint8_t scratch[16 * 16] = {};
    const int kModes[3] = {l16_rdo::DC_PRED, l16_rdo::H_PRED, l16_rdo::PAETH_PRED};
    std::uint64_t wantVar[3];
    for (int k = 0; k < 3; ++k) {
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 8; ++c)
                scratch[r * 16 + c] = static_cast<std::uint8_t>((r * 16 + c) * (kModes[k] + 1) & 0xFF);
        wantVar[k] = o.variance(scratch, 16, srcBuf, 16, 8, 8);
    }
    REQUIRE(wantVar[0] < wantVar[1]);
    REQUIRE(wantVar[1] < wantVar[2]);

    mdc::MdCand cands[4]{};
    cands[0].mode = l16_rdo::DC_PRED;
    cands[0].cand_class = 0;
    cands[0].filter_intra_mode = 5;  // FILTER_INTRA_MODES: a regular candidate
    cands[1].mode = l16_rdo::H_PRED;
    cands[1].cand_class = 0;
    cands[1].filter_intra_mode = 5;
    cands[2].mode = l16_rdo::V_PRED;
    cands[2].cand_class = 1;  // filtered out by the class gate
    cands[2].filter_intra_mode = 5;
    cands[3].mode = l16_rdo::PAETH_PRED;
    cands[3].cand_class = 0;
    cands[3].filter_intra_mode = 5;

    std::uint64_t costs[3];
    mdc::MdCandBf bufs[3];
    for (int i = 0; i < 3; ++i) {
        bufs[i].cand = nullptr;
        bufs[i].pred.buf = predStorage[i];
        bufs[i].pred.stride = 16;
        bufs[i].fast_cost = &costs[i];
        bufs[i].full_cost = &costs[i];
        costs[i] = 777;  // a sentinel the walk must overwrite or leave
    }
    mdc::MdCandBf* ptrs[3] = {&bufs[0], &bufs[1], &bufs[2]};
    std::uint64_t fast_cost_array[3] = {};
    ctx.fast_cost_array = fast_cost_array;
    // The buffer costs share the ctx array in the reference (each buffer's
    // fast_cost points at its slot), so the selection reads the same
    // storage the fast loop writes.
    for (int i = 0; i < 3; ++i) {
        bufs[i].fast_cost = &fast_cost_array[i];
        bufs[i].full_cost = &costs[i];
    }

    ctx.md_stage_1_count[0] = 10;
    mdc::mdStage0(&ctx, ptrs, cands, 4, input, 0, 0, 3, 0, 0, mdc::kSwitchable);

    CHECK(ctx.mds_do_ifs == 1);
    // The walk processed cands 0, 1, 3 (the class-1 cand skipped): the best
    // is the smallest mode variance among them.
    CHECK(ctx.mds0_best_cost == wantVar[0] << 4);
    CHECK(ctx.mds0_best_class == 0);
    CHECK(ctx.mds0_best_class0_cost == wantVar[0] << 4);
    CHECK(ctx.mds0_best_cost_per_class[0] == wantVar[0] << 4);
    // md_stage_1_count clamped to the processed count.
    CHECK(ctx.md_stage_1_count[0] == 3);
    // The scratch buffer holds MAX_CU_COST at the highest-cost index: the
    // argmax over the three written costs.
    const std::uint64_t w[3] = {wantVar[0] << 4, wantVar[1] << 4, wantVar[2] << 4};
    const int argmax = w[0] > w[1] ? (w[0] > w[2] ? 0 : 2) : (w[1] > w[2] ? 1 : 2);
    CHECK(fast_cost_array[argmax] == mdc::kMaxCuCost);
    // The processed cands sit at the buffers the walk rotated through: the
    // last processed (PAETH) landed at the scratch slot.
    CHECK(bufs[argmax].cand == &cands[3]);
    // The class-1 candidate never ran (its variance never touched a cost).
    CHECK(costs[0] == w[0]);
    CHECK(costs[1] == w[1]);
    CHECK(costs[2] == w[2]);
}

TEST_CASE("l6 mdc mdStage0 pruneUsingBestMode skips H after V and smooth after DC") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.geom.sq_size = 8;
    ctx.predTable[0] = predFillByMode;
    ctx.predTable[1] = predFillByMode;
    ctx.fastCostTable[0] = fastCostByMode;
    ctx.fastCostTable[1] = fastCostByMode;
    ctx.shut_fast_rate = 1;
    ctx.mds0_best_cost = 0xFFFFFFFFFFFFFFFFull;
    for (int i = 0; i < 5; ++i) ctx.mds0_best_cost_per_class[i] = 0xFFFFFFFFFFFFFFFFull;
    ctx.target_class = 0;
    ctx.reduce_filter_intra = 1;
    ctx.skip_angular_delta1_th = -1;
    ctx.skip_angular_delta2_th = -1;
    ctx.skip_angular_delta3_th = -1;
    ctx.prune_using_best_mode = 1;
    ctx.intra_mode_end = 12;

    std::uint8_t srcBuf[16 * 16] = {};
    mdc::PicBuf input{};
    input.buf = srcBuf;
    input.stride = 16;
    std::uint8_t predStorage[3][16 * 16];
    std::memset(predStorage, 0, sizeof(predStorage));

    mdc::MdCand cands[3]{};
    // Order: H first (best_reg starts INTRA_INVALID so H runs), then DC
    // (runs, becomes the best regular), then SMOOTH (skipped: best == DC).
    cands[0].mode = l16_rdo::H_PRED;
    cands[0].cand_class = 0;
    cands[0].filter_intra_mode = 5;
    cands[1].mode = l16_rdo::DC_PRED;
    cands[1].cand_class = 0;
    cands[1].filter_intra_mode = 5;
    cands[2].mode = l16_rdo::SMOOTH_PRED;
    cands[2].cand_class = 0;
    cands[2].filter_intra_mode = 5;

    std::uint64_t costs[3];
    mdc::MdCandBf bufs[3];
    for (int i = 0; i < 3; ++i) {
        bufs[i].cand = nullptr;
        bufs[i].pred.buf = predStorage[i];
        bufs[i].pred.stride = 16;
        bufs[i].full_cost = &costs[i];
    }
    mdc::MdCandBf* ptrs[3] = {&bufs[0], &bufs[1], &bufs[2]};
    std::uint64_t fast_cost_array[3] = {};
    ctx.fast_cost_array = fast_cost_array;
    for (int i = 0; i < 3; ++i) bufs[i].fast_cost = &fast_cost_array[i];
    ctx.md_stage_1_count[0] = 10;

    mdc::mdStage0(&ctx, ptrs, cands, 3, input, 0, 0, 3, 0, 0, mdc::kSwitchable);
    // Only H and DC processed; the best is DC (v0 < v2 by the *1 vs *3
    // shapes).
    CHECK(ctx.md_stage_1_count[0] == 2);
    VarOracle o;
    std::uint8_t scratch[16 * 16] = {};
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) scratch[r * 16 + c] = static_cast<std::uint8_t>((r * 16 + c) * 1 & 0xFF);
    const std::uint64_t v_dc = o.variance(scratch, 16, srcBuf, 16, 8, 8);
    CHECK(ctx.mds0_best_cost == v_dc << 4);
    CHECK(ctx.mds0_best_class0_cost == v_dc << 4);
}
