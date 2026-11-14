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
