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
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <amvp.h>
#include <md_core.h>
#include <motion.h>
#include <picops.h>
#include <subpel.h>
#include <transform_hbd.h>

#include <algorithm>

namespace mdc = pipeline::mdc;
namespace amvp = motion::amvp;
namespace picops = pipeline::picops;

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

// trip 10: md_full_pel_search (product_coding_loop.c:1855-:1989) - the SAD
// walk over the clamped search area with the OPT mv-cost term, the
// sparse-skip rule, and the VAR/SSD dist-type branches. Oracle: a test-side
// brute force over the same (clamped) position set.
TEST_CASE("l6 mdc mdFullPelSearch finds the SAD optimum over the clamped area") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.geom.sq_size = 8;
    ctx.blk_org_x = 40;
    ctx.blk_org_y = 40;
    ctx.ref_mv.x = 0;
    ctx.ref_mv.y = 0;
    ctx.md_subpel_skip_diag_refinement = 3;  // -> MV_COST_OPT (table-free)
    ctx.full_lambda_md[0] = 1024;
    ctx.fast_lambda_md[0] = 1024;

    // src block at (40,40) in a 64x64 plane; ref plane with border 32.
    std::vector<std::uint8_t> srcPlane(64 * 64);
    std::vector<std::uint8_t> refPlane((64 + 64) * (64 + 64));  // border 32 all around
    for (int i = 0; i < 64 * 64; ++i) srcPlane[i] = static_cast<std::uint8_t>(i * 3 + 7);
    for (int i = 0; i < (64 + 64) * (64 + 64); ++i) refPlane[i] = static_cast<std::uint8_t>(i * 5 + 1);
    mdc::PicBuf input{};
    input.buf = srcPlane.data();
    input.stride = 64;
    input.width = 64;
    input.height = 64;
    mdc::PicBuf ref{};
    ref.buf = refPlane.data() + 32 * (64 + 64) + 32;  // the (0,0) sample
    ref.stride = 64 + 64;
    ref.width = 64;
    ref.height = 64;
    ref.max_width = 64;
    ref.max_height = 64;
    ref.border = 32;

    const std::uint32_t input_origin_index = 40 * 64 + 40;
    std::int16_t best_x = 0, best_y = 0;
    std::uint32_t best_cost = 0xFFFFFFFFu;
    mdc::mdFullPelSearch(&ctx, input, ref, input_origin_index, mdc::DIST_SAD, 0, 0, -2, 2, -2, 2, 1, 0, &best_x,
                         &best_y, &best_cost, 0, 100);

    // Independent brute force over the same (clamped) window: SAD + the
    // OPT mv cost = ROUND((|dy|+|dx|)<<8 * epb, 14), epb = rdmult>>6 = 16.
    const std::uint8_t* srcB = srcPlane.data() + input_origin_index;
    std::uint32_t want_cost = 0xFFFFFFFFu;
    std::int16_t want_x = 0, want_y = 0;
    for (int py = -2; py <= 2; ++py) {
        for (int px = -2; px <= 2; ++px) {
            std::uint32_t sad = 0;
            const std::uint8_t* rp = ref.buf + (40 + py) * ref.stride + (40 + px);
            for (int r = 0; r < 8; ++r)
                for (int c = 0; c < 8; ++c) {
                    const int a = srcB[r * 64 + c];
                    const int b = rp[r * ref.stride + c];
                    sad += static_cast<std::uint32_t>(a > b ? a - b : b - a);
                }
            const int dx = px * 8, dy = py * 8;
            const int adx = dx < 0 ? -dx : dx;
            const int ady = dy < 0 ? -dy : dy;
            const std::int64_t mv_cost = (((ady + adx) << 8) * 16 + 8192) >> 14;
            const std::uint32_t cost = sad + static_cast<std::uint32_t>(mv_cost);
            if (cost < want_cost) {
                want_cost = cost;
                want_x = static_cast<std::int16_t>(px * 8);
                want_y = static_cast<std::int16_t>(py * 8);
            }
        }
    }
    CHECK(best_cost == want_cost);
    CHECK(best_x == want_x);
    CHECK(best_y == want_y);
}

TEST_CASE("l6 mdc mdFullPelSearch clamps the area at the ref border and skips sparse") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.geom.sq_size = 8;
    ctx.md_subpel_skip_diag_refinement = 3;
    ctx.full_lambda_md[0] = 1024;
    ctx.fast_lambda_md[0] = 1024;

    std::vector<std::uint8_t> srcPlane(64 * 64);
    std::vector<std::uint8_t> refPlane((64 + 64) * (64 + 64));
    for (int i = 0; i < 64 * 64; ++i) srcPlane[i] = static_cast<std::uint8_t>(i * 3 + 7);
    for (int i = 0; i < (64 + 64) * (64 + 64); ++i) refPlane[i] = static_cast<std::uint8_t>(i * 5 + 1);
    mdc::PicBuf input{};
    input.buf = srcPlane.data();
    input.stride = 64;
    mdc::PicBuf ref{};
    ref.buf = refPlane.data() + 32 * (64 + 64) + 32;
    ref.stride = 64 + 64;
    ref.max_width = 64;
    ref.max_height = 64;
    ref.border = 32;

    // (a) The clamp: blk at x=1 with mvx = -64 (>>3 = -8): the start-side
    // clamp keeps (blk_org + mv>>3 + start) >= -border + 1 = -31 ->
    // start >= -31 - (1 - 8) = -24... the raw start -2 stays (1-8-2 = -9
    // > -31). End side: 1 + 8 + (-8) + end <= border + max_w - 1 = 95 ->
    // end <= 94; raw end 2 stays. So no clamp fires here; force it: put
    // the block at x = 90 (out of the 64 max but the clamp math does not
    // care) and mvx = +64: 90 + 8 + end <= 95 -> end <= -3.
    ctx.blk_org_x = 90;
    ctx.blk_org_y = 40;
    std::int16_t best_x = 0, best_y = 0;
    std::uint32_t best_cost = 0xFFFFFFFFu;
    mdc::mdFullPelSearch(&ctx, input, ref, 40 * 64 + 40, mdc::DIST_SAD, 64, 0, -2, 2, -2, 2, 1, 0, &best_x, &best_y,
                         &best_cost, 0, 100);
    // The end-side clamp pulls end_x to -3: the walk visits x in [-2, -3]
    // -> EMPTY -> no position visited -> best stays untouched.
    CHECK(best_cost == 0xFFFFFFFFu);
    CHECK(best_x == 0);
    CHECK(best_y == 0);

    // (b) The start-side clamp: blk at y=2, mvy = -64 (>>3 = -8): start_y
    // raw -2 -> 2 - 8 - 2 = -8 < -31? No (-8 > -31). Force: blk_org_y = 90,
    // mvy = 64: 90 + 8 + end_y <= 95 -> end_y <= -3.
    ctx.blk_org_x = 40;
    ctx.blk_org_y = 90;
    best_cost = 0xFFFFFFFFu;
    mdc::mdFullPelSearch(&ctx, input, ref, 40 * 64 + 40, mdc::DIST_SAD, 0, 64, -2, 2, -2, 2, 1, 0, &best_x, &best_y,
                         &best_cost, 0, 100);
    CHECK(best_cost == 0xFFFFFFFFu);

    // (c) The sparse-skip: step 2 with lev0 performed; the positions
    // inside the lev0 box at (%4 == 0, %4 == 0) are skipped. Put the lev0
    // box to cover the whole window: positions (x % 4 == 0 && y % 4 == 0)
    // within it are skipped, so with start=-2..end=2, the skipped x are
    // {0} hmm: x%4==0 within [-2,2] = {0}, y likewise. With step 2 the y
    // positions are {-2, 0, 2}: y=0 skipped; x positions {-2,0,2}: x=0
    // skipped. So (0,0) is skipped; the rest run.
    ctx.blk_org_x = 40;
    ctx.blk_org_y = 40;
    ctx.sprs_lev0_start_x = -100;
    ctx.sprs_lev0_end_x = 100;
    ctx.sprs_lev0_start_y = -100;
    ctx.sprs_lev0_end_y = 100;
    best_cost = 0xFFFFFFFFu;
    mdc::mdFullPelSearch(&ctx, input, ref, 40 * 64 + 40, mdc::DIST_SAD, 0, 0, -2, 2, -2, 2, 2, 1, &best_x, &best_y,
                         &best_cost, 0, 100);
    // The oracle over the same visited set (skip (0,0)).
    const std::uint8_t* srcB = srcPlane.data() + 40 * 64 + 40;
    std::uint32_t want_cost = 0xFFFFFFFFu;
    std::int16_t want_x = 0, want_y = 0;
    for (int py = -2; py <= 2; py += 2) {
        for (int px = -2; px <= 2; px += 2) {
            if (px == 0 && py == 0) continue;
            std::uint32_t sad = 0;
            const std::uint8_t* rp = ref.buf + (40 + py) * ref.stride + (40 + px);
            for (int r = 0; r < 8; ++r)
                for (int c = 0; c < 8; ++c) {
                    const int a = srcB[r * 64 + c];
                    const int b = rp[r * ref.stride + c];
                    sad += static_cast<std::uint32_t>(a > b ? a - b : b - a);
                }
            const int dx = px * 8, dy = py * 8;
            const std::int64_t mv_cost = (((((dy < 0 ? -dy : dy) + (dx < 0 ? -dx : dx)) << 8) * 16) + 8192) >> 14;
            const std::uint32_t cost = sad + static_cast<std::uint32_t>(mv_cost);
            if (cost < want_cost) {
                want_cost = cost;
                want_x = static_cast<std::int16_t>(px * 8);
                want_y = static_cast<std::int16_t>(py * 8);
            }
        }
    }
    CHECK(best_cost == want_cost);
    CHECK(best_x == want_x);
    CHECK(best_y == want_y);
}

TEST_CASE("l6 mdc mdFullPelSearch runs the VAR and SSD dist types and the hbd SAD") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.geom.sq_size = 8;
    ctx.md_subpel_skip_diag_refinement = 3;
    ctx.full_lambda_md[0] = 1024;
    ctx.fast_lambda_md[0] = 1024;
    ctx.blk_org_x = 40;
    ctx.blk_org_y = 40;

    std::vector<std::uint8_t> srcPlane(64 * 64);
    std::vector<std::uint8_t> refPlane((64 + 64) * (64 + 64));
    for (int i = 0; i < 64 * 64; ++i) srcPlane[i] = static_cast<std::uint8_t>(i * 3 + 7);
    for (int i = 0; i < (64 + 64) * (64 + 64); ++i) refPlane[i] = static_cast<std::uint8_t>(i * 5 + 1);
    mdc::PicBuf input{};
    input.buf = srcPlane.data();
    input.stride = 64;
    mdc::PicBuf ref{};
    ref.buf = refPlane.data() + 32 * (64 + 64) + 32;
    ref.stride = 64 + 64;
    ref.max_width = 64;
    ref.max_height = 64;
    ref.border = 32;

    // VAR: the variance at each position; oracle over the window.
    std::int16_t best_x = 0, best_y = 0;
    std::uint32_t best_cost = 0xFFFFFFFFu;
    mdc::mdFullPelSearch(&ctx, input, ref, 40 * 64 + 40, mdc::DIST_VAR, 0, 0, -1, 1, -1, 1, 1, 0, &best_x, &best_y,
                         &best_cost, 0, 100);
    VarOracle o;
    const std::uint8_t* srcB = srcPlane.data() + 40 * 64 + 40;
    std::uint32_t want_cost = 0xFFFFFFFFu;
    std::int16_t want_x = 0, want_y = 0;
    for (int py = -1; py <= 1; ++py) {
        for (int px = -1; px <= 1; ++px) {
            const std::uint8_t* rp = ref.buf + (40 + py) * ref.stride + (40 + px);
            const std::uint32_t var = o.variance(rp, static_cast<int>(ref.stride), srcB, 64, 8, 8);
            const std::int64_t mv_cost = (((((py * 8 < 0 ? -py * 8 : py * 8) + (px * 8 < 0 ? -px * 8 : px * 8)) << 8) * 16) + 8192) >> 14;
            const std::uint32_t cost = var + static_cast<std::uint32_t>(mv_cost);
            if (cost < want_cost) {
                want_cost = cost;
                want_x = static_cast<std::int16_t>(px * 8);
                want_y = static_cast<std::int16_t>(py * 8);
            }
        }
    }
    CHECK(best_cost == want_cost);
    CHECK(best_x == want_x);
    CHECK(best_y == want_y);

    // SSD: the spatial full distortion (sum of squared diffs) at (0,0).
    std::uint64_t ssd_ref = picops::spatialFullDistortion(srcPlane.data(), 40 * 64 + 40, 64,
                                                                   refPlane.data() + 32 * (64 + 64) + 32,
                                                                   (40 + 0) * ref.stride + (40 + 0), ref.stride, 8, 8);
    best_cost = 0xFFFFFFFFu;
    mdc::mdFullPelSearch(&ctx, input, ref, 40 * 64 + 40, mdc::DIST_SSD, 0, 0, 0, 0, 0, 0, 1, 0, &best_x, &best_y,
                         &best_cost, 0, 100);
    CHECK(best_cost == static_cast<std::uint32_t>(ssd_ref) + (((0 << 8) * 16 + 8192) >> 14));

    // hbd SAD: uint16 planes through sad16b at (0,0).
    std::vector<std::uint16_t> srcHbd(64 * 64);
    std::vector<std::uint16_t> refHbd((64 + 64) * (64 + 64));
    for (int i = 0; i < 64 * 64; ++i) srcHbd[i] = static_cast<std::uint16_t>(i * 37 + 11);
    for (int i = 0; i < (64 + 64) * (64 + 64); ++i) refHbd[i] = static_cast<std::uint16_t>(i * 13 + 3);
    mdc::PicBuf inputH{};
    inputH.buf = reinterpret_cast<std::uint8_t*>(srcHbd.data());
    inputH.stride = 64;
    mdc::PicBuf refH{};
    refH.buf = reinterpret_cast<std::uint8_t*>(refHbd.data() + 32 * (64 + 64) + 32);
    refH.stride = 64 + 64;
    refH.max_width = 64;
    refH.max_height = 64;
    refH.border = 32;
    best_cost = 0xFFFFFFFFu;
    mdc::mdFullPelSearch(&ctx, inputH, refH, 40 * 64 + 40, mdc::DIST_SAD, 0, 0, 0, 0, 0, 0, 1, 0, &best_x, &best_y,
                         &best_cost, 1, 100);
    const std::uint32_t sad16 = motion::sad16b(srcHbd.data() + 40 * 64 + 40, 64,
                                               refHbd.data() + (40 + 32) * (64 + 64) + (40 + 32), 64 + 64, 8, 8);
    CHECK(best_cost == sad16);
}

// trip 11: md_full_pel_search_large_lbd (product_coding_loop.c:1771-:1840).
// The remain padding rounds the window width up to a multiple of 8, so the
// pme-kernel part covers the whole x range (the tail walk is dead by the
// assert - ported verbatim, unreachable). Oracle: a brute force over the
// padded window with the same SAD + OPT-cost model.
TEST_CASE("l6 mdc mdFullPelSearchLargeLbd covers the padded window via the pme kernel") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.geom.sq_size = 8;
    ctx.blk_org_x = 40;
    ctx.blk_org_y = 40;
    ctx.ref_mv.x = 0;
    ctx.ref_mv.y = 0;

    std::vector<std::uint8_t> srcPlane(64 * 64);
    std::vector<std::uint8_t> refPlane((64 + 64) * (64 + 64));
    for (int i = 0; i < 64 * 64; ++i) srcPlane[i] = static_cast<std::uint8_t>(i * 3 + 7);
    for (int i = 0; i < (64 + 64) * (64 + 64); ++i) refPlane[i] = static_cast<std::uint8_t>(i * 5 + 1);
    mdc::PicBuf input{};
    input.buf = srcPlane.data();
    input.stride = 64;
    mdc::PicBuf ref{};
    ref.buf = refPlane.data() + 32 * (64 + 64) + 32;
    ref.stride = 64 + 64;

    pipeline::SvtMvCostParam params;
    std::memset(&params, 0, sizeof(params));
    const pipeline::Mv2 zero_mv{};
    params.ref_mv = &zero_mv;
    params.error_per_bit = 16;
    params.mv_cost_type = pipeline::MV_COST_OPT;

    const std::uint32_t input_origin_index = 40 * 64 + 40;
    std::int16_t best_x = 0, best_y = 0;
    std::uint32_t best_cost = 0xFFFFFFFFu;
    mdc::mdFullPelSearchLargeLbd(params, &ctx, input, ref, input_origin_index, 0, 0, 0, 10, 0, 2, 1, &best_x, &best_y,
                                 &best_cost);

    // The remain padding: 10 % 8 = 2 -> remain 6 -> end 16 -> width 16, all
    // via the kernel. Brute force 16 x 3 positions.
    const std::uint8_t* srcB = srcPlane.data() + input_origin_index;
    std::uint32_t want_cost = 0xFFFFFFFFu;
    std::int16_t want_x = 0, want_y = 0;
    for (int py = 0; py <= 2; ++py) {
        for (int px = 0; px <= 15; ++px) {
            std::uint32_t sad = 0;
            const std::uint8_t* rp = ref.buf + (40 + py) * ref.stride + (40 + px);
            for (int r = 0; r < 8; ++r)
                for (int c = 0; c < 8; ++c) {
                    const int a = srcB[r * 64 + c];
                    const int b = rp[r * ref.stride + c];
                    sad += static_cast<std::uint32_t>(a > b ? a - b : b - a);
                }
            const int dx = px * 8, dy = py * 8;
            const std::int64_t mv_cost = (((((dy < 0 ? -dy : dy) + (dx < 0 ? -dx : dx)) << 8) * 16) + 8192) >> 14;
            const std::uint32_t cost = sad + static_cast<std::uint32_t>(mv_cost);
            if (cost < want_cost) {
                want_cost = cost;
                want_x = static_cast<std::int16_t>(px * 8);
                want_y = static_cast<std::int16_t>(py * 8);
            }
        }
    }
    CHECK(best_cost == want_cost);
    CHECK(best_x == want_x);
    CHECK(best_y == want_y);
}

// trip 12: the SQUARE_PU_COUNT = 85 tables (motion_estimation.h:97-:130)
// the NSQ MVC derivation consumes: partition_width / partition_height /
// pu_search_index_map. RC1 precedent: the oracle parses the reference
// header itself and diffs every entry.
TEST_CASE("l6 mdc pu tables match the reference header byte for byte") {
    std::ifstream f(REPO_ROOT "\\third_party\\SVT-AV1\\Source\\Lib\\Codec\\motion_estimation.h", std::ios::binary);
    REQUIRE(f.is_open());
    std::vector<char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const std::string text(buf.begin(), buf.end());
    // Strip // comments first: the reference initializers carry per-row
    // count comments ("// (1)", "// (4)", ...) whose digits would pollute
    // the number scan.
    std::string clean;
    bool in_comment = false;
    for (char c : text) {
        if (in_comment) {
            if (c == '\n') {
                in_comment = false;
                clean += c;
            }
        } else if (c == '/' ) {
            in_comment = true;  // tentatively; a lone '/' is dropped, fine
        } else {
            clean += c;
        }
    }
    const std::string& src_text = clean;

    // Extract the named initializer, brace-depth matched (the map's body
    // nests per-row braces).
    auto extract = [&src_text](const char* name) {
        const std::size_t at = src_text.find(name);
        REQUIRE(at != std::string::npos);
        const std::size_t open = src_text.find('{', at);
        REQUIRE(open != std::string::npos);
        int depth = 1;
        std::size_t close = open + 1;
        while (depth > 0 && close < src_text.size()) {
            if (src_text[close] == '{') {
                ++depth;
            } else if (src_text[close] == '}') {
                --depth;
            }
            ++close;
        }
        REQUIRE(depth == 0);
        return src_text.substr(open + 1, close - open - 1);
    };
    auto parse = [](const std::string& body) {
        std::vector<long> vals;
        std::string cur;
        for (char c : body) {
            if ((c >= '0' && c <= '9') || c == '-') {
                cur += c;
            } else if (!cur.empty()) {
                vals.push_back(std::strtol(cur.c_str(), nullptr, 10));
                cur.clear();
            }
        }
        if (!cur.empty()) vals.push_back(std::strtol(cur.c_str(), nullptr, 10));
        return vals;
    };
    const std::vector<long> wRef = parse(extract("partition_width[SQUARE_PU_COUNT]"));
    const std::vector<long> hRef = parse(extract("partition_height[SQUARE_PU_COUNT]"));
    const std::vector<long> mRef = parse(extract("pu_search_index_map[SQUARE_PU_COUNT][2]"));
    REQUIRE(wRef.size() == 85);
    REQUIRE(hRef.size() == 85);
    REQUIRE(mRef.size() == 170);
    for (int i = 0; i < 85; ++i) {
        CHECK(mdc::partitionWidth[i] == wRef[i]);
        CHECK(mdc::partitionHeight[i] == hRef[i]);
        CHECK(mdc::puSearchIndexMap[i][0] == mRef[i * 2]);
        CHECK(mdc::puSearchIndexMap[i][1] == mRef[i * 2 + 1]);
    }
}

// trip 13: md_nsq_motion_search (product_coding_loop.c:2021-:2190). The
// MVC derivation over the PU tables (the width/height match + the block
// window + the ME-data gate), the round-to-integer of every entry, the
// degenerate-ladder trick (full_pel_search_* = 0 -> one position per
// search) that makes the center selection directly observable, and the
// step-4 ladder finding an integer match the centers miss. The dedup of
// equal MVC entries is a pure search-count optimization - no observable
// output difference (noted, not testable without instrumentation).
namespace {
struct NsqFixture {
    std::vector<std::uint8_t> srcPlane;
    std::vector<std::uint8_t> refPlane;
    mdc::PicBuf input{};
    mdc::PicBuf ref{};
    std::vector<std::uint8_t> totalCnt;
    std::vector<l16_rdo::MeCandidate> cands;
    std::vector<pipeline::Mv2> mvArr;
    mdc::MdMeSbResults me{};
};
NsqFixture makeNsqFixture(int shift, bool list1) {
    NsqFixture fx;
    fx.srcPlane.assign(64 * 64, 0);
    fx.refPlane.assign((64 + 64) * (64 + 64), 0);
    for (int r = 0; r < 64; ++r)
        for (int c = 0; c < 64; ++c) fx.srcPlane[r * 64 + c] = static_cast<std::uint8_t>(((r * 64 + c) * 2654435761u) >> 24);
    // ref(x, y) = src(x + shift, y) for EVERY plane sample (border
    // included, modular wrap): the block sits at the origin, so the
    // negative search positions read the top/left border rows and those
    // must carry the shifted pattern too.
    for (int r = 0; r < 64 + 64; ++r)
        for (int c = 0; c < 64 + 64; ++c) {
            const int sy = ((r - 32) % 64 + 64) % 64;
            const int sx = ((c - 32 + shift) % 64 + 64) % 64;
            fx.refPlane[r * (64 + 64) + c] = fx.srcPlane[sy * 64 + sx];
        }
    fx.input.buf = fx.srcPlane.data();
    fx.input.stride = 64;
    fx.ref.buf = fx.refPlane.data() + 32 * (64 + 64) + 32;
    fx.ref.stride = 64 + 64;
    fx.ref.max_width = 64;
    fx.ref.max_height = 64;
    fx.ref.border = 32;
    fx.totalCnt.assign(85, 0);
    fx.cands.assign(85 * 23, {});
    fx.mvArr.assign(85 * 8, pipeline::Mv2{0, 0});
    fx.me.total_me_candidate_index = fx.totalCnt.data();
    fx.me.me_mv_array = fx.mvArr.data();
    fx.me.me_candidate_array = fx.cands.data();
    (void)list1;
    return fx;
}
void plantPu(NsqFixture& fx, int pu, int mv_x, bool list1) {
    fx.totalCnt[pu] = 1;
    l16_rdo::MeCandidate& c = fx.cands[pu * 23];
    c.direction = 0;
    c.ref0List = list1 ? 1 : 0;  // the list the ref_idx_l0 slot belongs to
    c.refIdxL0 = 0;
    fx.mvArr[pu * 8 + (list1 ? 4 : 0)] = pipeline::Mv2{static_cast<std::int16_t>(mv_x), 0};
}
mdc::FlCtx nsqCtx() {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_16X32;
    ctx.geom.bwidth = 16;
    ctx.geom.bheight = 32;
    ctx.geom.sq_size = 16;
    ctx.blk_org_x = 0;
    ctx.blk_org_y = 0;
    ctx.sb_origin_x = 0;
    ctx.sb_origin_y = 0;
    ctx.geom_offset_x = 0;
    ctx.geom_offset_y = 0;
    ctx.ref_mv.x = 0;
    ctx.ref_mv.y = 0;
    ctx.md_subpel_skip_diag_refinement = 3;
    ctx.fast_lambda_md[0] = 1024;
    ctx.full_lambda_md[0] = 1024;
    return ctx;
}
}  // namespace

TEST_CASE("l6 mdc mdNsqMotionSearch picks the best MVC center and gates the PU window") {
    NsqFixture fx = makeNsqFixture(2, false);
    // PU 5 = the 16x16 PU at (0,0), inside the 16x32 block: perfect match.
    plantPu(fx, 5, -2, false);
    // PU 9 = the 16x16 PU at (0,16), also inside: same MV (dedup'd).
    plantPu(fx, 9, -2, false);
    // PU 1 = the 32x32 PU at (0,0): partition width 32 != min 16 -> the
    // width gate must reject it even though its MV is perfect.
    plantPu(fx, 1, -16, false);
    // PU 21 = the 8x8 PU at (0,0): partition 8 != 16 -> rejected.
    plantPu(fx, 21, -16, false);

    mdc::FlCtx ctx = nsqCtx();
    mdc::MdNsqCtrls ctrls{};
    ctrls.dist_type = mdc::DIST_SAD;
    ctrls.full_pel_search_width = 0;
    ctrls.full_pel_search_height = 0;
    ctrls.enable_psad = 0;

    std::int16_t me_x = 0, me_y = 0;
    mdc::mdNsqMotionSearch(&ctx, fx.input, fx.ref, 0, amvp::kLastFrame, &fx.me, &me_x, &me_y, ctrls, 8, 4, 1, 1, 85,
                           23, 100);
    CHECK(ctx.enable_psad == 0);
    // The best center is the PU-5 MV (-2 full-pel -> -16 1/8-pel); the
    // SQ (0) and the out-of-window PUs never win.
    CHECK(me_x == -16);
    CHECK(me_y == 0);
}

TEST_CASE("l6 mdc mdNsqMotionSearch ladder refines and the list-1 slot indexes") {
    // (a) The step-4 ladder reaches an integer match the centers miss.
    NsqFixture fx = makeNsqFixture(8, false);  // true match at (-8, 0)
    mdc::FlCtx ctx = nsqCtx();
    mdc::MdNsqCtrls ctrls{};
    ctrls.dist_type = mdc::DIST_SAD;
    ctrls.full_pel_search_width = 16;
    ctrls.full_pel_search_height = 0;
    ctrls.enable_psad = 0;
    std::int16_t me_x = 0, me_y = 0;
    mdc::mdNsqMotionSearch(&ctx, fx.input, fx.ref, 0, amvp::kLastFrame, &fx.me, &me_x, &me_y, ctrls, 8, 4, 1, 1, 85,
                           23, 100);
    CHECK(me_x == -64);
    CHECK(me_y == 0);

    // (b) The list-1 slot: plant the perfect MV in [pu*max_refs + max_l0]
    // for a BWDREF (list 1, ref 0) candidate.
    NsqFixture fx1 = makeNsqFixture(2, true);
    plantPu(fx1, 5, -2, true);
    mdc::FlCtx ctx1 = nsqCtx();
    std::int16_t me1_x = 0, me1_y = 0;
    mdc::mdNsqMotionSearch(&ctx1, fx1.input, fx1.ref, 0, amvp::kBwdrefFrame, &fx1.me, &me1_x, &me1_y, ctrls, 8, 4, 1,
                           1, 85, 23, 100);
    CHECK(me1_x == -16);
    CHECK(me1_y == 0);

    // (c) The MVC round-to-integer: the SQ MV 5 (1/8 pel) rounds to 8, the
    // center search at the true match (-16... shift 2 -> -16) hmm: with the
    // SQ MV rounding to 8 and the true match at -16 the PU-5 entry still
    // wins; use shift -1 (match at +8) so the ROUNDED SQ MV is the winner.
    NsqFixture fx2 = makeNsqFixture(-1, false);  // ref(x) = src(x - 1): match at (+1, 0)
    mdc::FlCtx ctx2 = nsqCtx();
    std::int16_t me2_x = 5, me2_y = 0;  // 0.625 px -> rounds to 8 = the match
    mdc::mdNsqMotionSearch(&ctx2, fx2.input, fx2.ref, 0, amvp::kLastFrame, &fx2.me, &me2_x, &me2_y, ctrls, 8, 4, 1, 1,
                           85, 23, 100);
    CHECK(me2_x == 8);
    CHECK(me2_y == 0);
}

// trip 14: md_sq_motion_search (product_coding_loop.c:2270-:2457). The
// high-motion gate (RDCOST(fast_lambda, 16, pa_me_cost) vs the per-pixel
// th), the spatial vs temporal multiplier derivation, the sparse level
// scaling formula and the lev0 box write.
namespace {
mdc::FlCtx sqCtx() {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_64X64;
    ctx.geom.bwidth = 64;
    ctx.geom.bheight = 64;
    ctx.geom.sq_size = 64;
    ctx.blk_org_x = 0;
    ctx.blk_org_y = 0;
    ctx.sb_origin_x = 0;
    ctx.sb_origin_y = 0;
    ctx.ref_mv.x = 0;
    ctx.ref_mv.y = 0;
    ctx.md_subpel_skip_diag_refinement = 3;
    ctx.fast_lambda_md[0] = 0;  // the RDCOST gate reduces to the distortion pair
    ctx.full_lambda_md[0] = 1024;
    return ctx;
}
}  // namespace

TEST_CASE("l6 mdc mdSqMotionSearch gates on distortion and expands via the sparse ladder") {
    // Planes: src patterned; ref = src shifted by (+16, 0) (the match at
    // integer +16 -> mv 128). The bordered ref plane is 144 wide with
    // border 40 so the +-32 lev0 window never trips the border clamps
    // (the clamps would shift the step-4 grid off the match).
    std::vector<std::uint8_t> srcPlane(64 * 64);
    std::vector<std::uint8_t> refPlane(144 * 144);
    for (int r = 0; r < 64; ++r)
        for (int c = 0; c < 64; ++c) srcPlane[r * 64 + c] = static_cast<std::uint8_t>(((r * 64 + c) * 2654435761u) >> 24);
    for (int r = 0; r < 144; ++r)
        for (int c = 0; c < 144; ++c) {
            const int sy = ((r - 40) % 64 + 64) % 64;
            const int sx = ((c - 40 - 16) % 64 + 64) % 64;  // ref(x) = src(x - 16)
            refPlane[r * 144 + c] = srcPlane[sy * 64 + sx];
        }
    mdc::PicBuf input{};
    input.buf = srcPlane.data();
    input.stride = 64;
    mdc::PicBuf ref{};
    ref.buf = refPlane.data() + 40 * 144 + 40;
    ref.stride = 144;
    ref.max_width = 64;
    ref.max_height = 64;
    ref.border = 40;

    mdc::FlCtx ctx = sqCtx();
    mdc::MdSqCtrls ctrls{};
    ctrls.dist_type = mdc::DIST_SAD;
    ctrls.pame_distortion_th = 1;
    ctrls.sprs_lev0_enabled = 1;
    ctrls.sprs_lev0_step = 4;
    ctrls.sprs_lev0_w = 16;
    ctrls.sprs_lev0_h = 16;
    ctrls.max_sprs_lev0_w = 64;
    ctrls.max_sprs_lev0_h = 64;
    ctrls.sprs_lev0_multiplier = 100;
    ctrls.enable_psad = 0;

    // Spatial path: one MVP above the HIGH threshold -> multiplier 3.
    std::vector<pipeline::Mv2> mvp(1);
    mvp[0] = pipeline::Mv2{3000, 0};
    ctx.mvpArr[0][0] = mvp.data();
    ctx.mvpCnt[0][0] = 1;

    std::int16_t me_x = 0, me_y = 0;
    mdc::mdSqMotionSearch(&ctx, input, ref, 0, amvp::kLastFrame, &me_x, &me_y, ctrls, 10, 8, 1, 0, 0, 0, 100);
    // The lev0 ladder: dist = |10-8| = 2 -> scaled 2; w = 100*MIN(16*3*2,
    // 64)/100 = 64 -> the +-32 step-4 window reaches +16: SAD 0 there.
    CHECK(me_x == 128);
    CHECK(me_y == 0);
    CHECK(ctx.sprs_lev0_start_x == -32);
    CHECK(ctx.sprs_lev0_end_x == 32);
    CHECK(ctx.sprs_lev0_start_y == -32);
    CHECK(ctx.sprs_lev0_end_y == 32);
    CHECK(ctx.enable_psad == 0);
}

TEST_CASE("l6 mdc mdSqMotionSearch low distortion keeps the pa-me mv and the temporal path scales") {
    std::vector<std::uint8_t> srcPlane(64 * 64);
    std::vector<std::uint8_t> refPlane(144 * 144);
    for (int r = 0; r < 64; ++r)
        for (int c = 0; c < 64; ++c) srcPlane[r * 64 + c] = static_cast<std::uint8_t>(((r * 64 + c) * 2654435761u) >> 24);
    for (int r = 0; r < 144; ++r)
        for (int c = 0; c < 144; ++c) {
            const int sy = ((r - 40) % 64 + 64) % 64;
            const int sx = ((c - 40 - 16) % 64 + 64) % 64;
            refPlane[r * 144 + c] = srcPlane[sy * 64 + sx];
        }
    mdc::PicBuf input{};
    input.buf = srcPlane.data();
    input.stride = 64;
    mdc::PicBuf ref{};
    ref.buf = refPlane.data() + 40 * 144 + 40;
    ref.stride = 144;
    ref.max_width = 64;
    ref.max_height = 64;
    ref.border = 40;

    mdc::FlCtx ctx = sqCtx();
    mdc::MdSqCtrls ctrls{};
    ctrls.dist_type = mdc::DIST_SAD;
    ctrls.pame_distortion_th = 1;
    ctrls.sprs_lev0_enabled = 1;
    ctrls.sprs_lev0_step = 4;
    ctrls.sprs_lev0_w = 16;
    ctrls.sprs_lev0_h = 16;
    ctrls.max_sprs_lev0_w = 64;
    ctrls.max_sprs_lev0_h = 64;
    ctrls.sprs_lev0_multiplier = 100;
    ctrls.enable_psad = 0;

    // (a) The gate closed: th * area >> the pa-me distortion -> no
    // expansion; the mv stays the pa-me result (the window-0 search keeps
    // the input mv).
    ctx.fast_lambda_md[0] = 0;
    std::int16_t me_x = 0, me_y = 0;
    // th huge: RDCOST(0, 16, th*4096) dwarfs any SAD.
    mdc::MdSqCtrls ctrls_hi = ctrls;
    ctrls_hi.pame_distortion_th = 0xFFFF;
    mdc::mdSqMotionSearch(&ctx, input, ref, 0, amvp::kLastFrame, &me_x, &me_y, ctrls_hi, 10, 8, 1, 0, 0, 0, 100);
    CHECK(me_x == 0);
    CHECK(me_y == 0);
    CHECK(ctx.sprs_lev0_start_x == 0);  // untouched by the skipped ladder
    CHECK(ctx.sprs_lev0_end_x == 0);

    // (b) The temporal path: mfmv0 above the MEDIUM th -> multiplier 2;
    // dist 2 -> scaled 2 -> w = 100*MIN(16*2*2, 64)/100 = 64 -> +-32.
    me_x = 0;
    me_y = 0;
    ctx.sprs_lev0_start_x = 77;  // a canary the ladder must overwrite
    mdc::mdSqMotionSearch(&ctx, input, ref, 0, amvp::kLastFrame, &me_x, &me_y, ctrls, 10, 8, 0,
                          0x11223344, 3000, 0, 100);
    CHECK(me_x == 128);
    CHECK(ctx.sprs_lev0_start_x == -32);
    CHECK(ctx.sprs_lev0_end_x == 32);

    // (c) The temporal INVALID_MV -> multiplier 0 -> no expansion.
    me_x = 0;
    me_y = 0;
    ctx.sprs_lev0_start_x = 77;
    mdc::mdSqMotionSearch(&ctx, input, ref, 0, amvp::kLastFrame, &me_x, &me_y, ctrls, 10, 8, 0,
                          motion::amvp::kInvalidMv, 3000, 0, 100);
    CHECK(me_x == 0);
    CHECK(ctx.sprs_lev0_start_x == 77);
}

// trip 15: md_subpel_search_fixed_stage (product_coding_loop.c:2582-:2681).
// The walk is transcribed in the test over the LANDED kernels (the mefn vf
// + analysis::subpel svf): the integer baseline, the pred-variance gate
// (the var_offs constant-128 call), the four half-pel neighbours with the
// bias_fp rule, the quarter-pel round gated on max_precision, and the
// th_normalizer early exits.
TEST_CASE("l6 mdc mdSubpelSearchFixedStage walks hpel then qpel with the bias rule") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.blk_org_x = 40;
    ctx.blk_org_y = 40;

    // ref plane: a curved ramp; the src block equals the kernel's own
    // horizontal half-pel interpolation of the ramp ((a0 + a1 + 2) >> 2,
    // the {64,64} bilinear row), so the hpel (4, 0) neighbour reproduces
    // the src exactly (variance 0) while the integer baseline diff varies.
    std::vector<std::uint8_t> refPlane(144 * 144);
    std::vector<std::uint8_t> srcPlane(64 * 64);
    for (int r = 0; r < 144; ++r)
        for (int c = 0; c < 144; ++c) refPlane[r * 144 + c] = static_cast<std::uint8_t>(((c * c) >> 6) + r * 3);
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c) {
            const int a0 = refPlane[(40 + r) * 144 + (40 + c)];
            const int a1 = refPlane[(40 + r) * 144 + (41 + c)];
            srcPlane[r * 64 + c] = static_cast<std::uint8_t>((a0 + a1 + 2) >> 2);
        }
    mdc::PicBuf ref{};
    ref.buf = refPlane.data();  // the PLANE ORIGIN (blk_org indexes absolutely)
    ref.stride = 144;

    mdc::MdSubPelCtrls sub{};
    sub.abs_th_mult = 0;           // th_normalizer 0 -> no early exit
    sub.pred_variance_th = 0;      // the offs gate off
    sub.max_precision = mdc::kEighthPel;
    sub.bias_fp = 30;              // penalize the (0,0)-delta hpel/qpel

    pipeline::Mv2 me_mv{};
    me_mv.x = 0;  // mv_x_fp = 0: the integer baseline is the block itself
    me_mv.y = 0;
    const std::uint32_t got = mdc::mdSubpelSearchFixedStage(&ctx, ref, srcPlane.data(), 64, &me_mv, sub);

    // Independent transcription of the walk over the landed kernels.
    VarOracle o;
    auto svf = [](const std::uint8_t* a, int aStride, int xo, int yo, const std::uint8_t* b, int bStride,
                  std::uint32_t* sse) {
        return analysis::subpel::subPixelVariance8x8(a, aStride, xo, yo, b, bStride, sse);
    };
    (void)svf;
    const std::uint8_t* refO = refPlane.data() + 40 * 144 + 40;
    std::uint32_t want_var = o.variance(refO, 144, srcPlane.data(), 64, 8, 8);
    REQUIRE(want_var > 0);  // the baseline diff is not constant
    int want_dx = 0, want_dy = 0;
    static const int hpel_dx[4] = {4, -4, 0, 0};
    static const int hpel_dy[4] = {0, 0, 4, -4};
    for (int i = 0; i < 4; ++i) {
        const int subx = (0 + hpel_dx[i]) & 7;
        const int suby = (0 + hpel_dy[i]) & 7;
        const std::uint8_t* rp = refO + (hpel_dx[i] >> 3) + (hpel_dy[i] >> 3) * 144;
        std::uint32_t sse2;
        const std::uint32_t var = svf(rp, 144, subx, suby, srcPlane.data(), 64, &sse2);
        const std::uint32_t biased = (want_dx == 0 && want_dy == 0)
            ? static_cast<std::uint32_t>((static_cast<std::uint64_t>(var) * 30) / 100)
            : var;
        if (biased < want_var) {
            want_var = var;
            want_dx = hpel_dx[i];
            want_dy = hpel_dy[i];
        }
    }
    // max_precision = EIGHTH_PEL -> the qpel round runs.
    static const int qpel_dx[4] = {2, -2, 0, 0};
    static const int qpel_dy[4] = {0, 0, 2, -2};
    for (int i = 0; i < 4; ++i) {
        const int tot_dx = want_dx + qpel_dx[i];
        const int tot_dy = want_dy + qpel_dy[i];
        const int subx = (0 + tot_dx) & 7;
        const int suby = (0 + tot_dy) & 7;
        const std::uint8_t* rp = refO + (tot_dx >> 3) + (tot_dy >> 3) * 144;
        std::uint32_t sse2;
        const std::uint32_t var = svf(rp, 144, subx, suby, srcPlane.data(), 64, &sse2);
        const std::uint32_t biased = (want_dx == 0 && want_dy == 0)
            ? static_cast<std::uint32_t>((static_cast<std::uint64_t>(var) * 30) / 100)
            : var;
        if (biased < want_var) {
            want_var = var;
            want_dx = tot_dx;
            want_dy = tot_dy;
        }
    }
    CHECK(got == want_var);
    CHECK(me_mv.x == 0 + want_dx);
    CHECK(me_mv.y == 0 + want_dy);
    // The bias rule: the first hpel candidate is evaluated at 30% (the
    // winner moved off (0,0) delta), so the result is a real subpel mv.
    CHECK((want_dx != 0 || want_dy != 0));
}

TEST_CASE("l6 mdc mdSubpelSearchFixedStage early exits on the two thresholds") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.blk_org_x = 40;
    ctx.blk_org_y = 40;

    std::vector<std::uint8_t> refPlane(144 * 144);
    std::vector<std::uint8_t> srcPlane(64 * 64, 0);
    for (int r = 0; r < 144; ++r)
        for (int c = 0; c < 144; ++c) refPlane[r * 144 + c] = static_cast<std::uint8_t>(c * 2 + r);
    // src = the ref block shifted by a constant +2 -> the population
    // variance of the constant-diff block is 0 -> the first early exit
    // (best_var < th_normalizer) fires with abs_th_mult = 1.
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c)
            srcPlane[r * 64 + c] = static_cast<std::uint8_t>(refPlane[(40 + r) * 144 + (42 + c)]);

    mdc::PicBuf ref{};
    ref.buf = refPlane.data();  // the PLANE ORIGIN
    ref.stride = 144;

    mdc::MdSubPelCtrls sub{};
    sub.abs_th_mult = 1;  // th = 64; best_var = 0 < 64 -> exit at the baseline
    sub.pred_variance_th = 0;
    sub.max_precision = mdc::kEighthPel;
    sub.bias_fp = 0;
    pipeline::Mv2 me_mv{};
    me_mv.x = 16;  // 2 px
    me_mv.y = 0;
    const std::uint32_t got = mdc::mdSubpelSearchFixedStage(&ctx, ref, srcPlane.data(), 64, &me_mv, sub);
    CHECK(got == 0);
    CHECK(me_mv.x == 16);  // unchanged: the baseline returned
    CHECK(me_mv.y == 0);

    // The pred-variance gate: abs_th_mult = 0 (no first exit), the ref
    // block variance vs 128 is the ramp variance; th above it exits.
    mdc::MdSubPelCtrls sub2{};
    sub2.abs_th_mult = 0;
    sub2.pred_variance_th = 100000;
    sub2.max_precision = mdc::kEighthPel;
    sub2.bias_fp = 0;
    // Make the baseline variance nonzero: src = ref ^ 1 (a +/1-1 diff has
    // nonzero population variance).
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 8; ++c)
            srcPlane[r * 64 + c] = static_cast<std::uint8_t>(refPlane[(40 + r) * 144 + (40 + c)] ^ 1);
    me_mv.x = 0;
    me_mv.y = 0;
    VarOracle o2;
    const std::uint8_t* refO2 = refPlane.data() + 40 * 144 + 40;
    const std::uint32_t want2 = o2.variance(refO2, 144, srcPlane.data(), 64, 8, 8);
    const std::uint32_t got2 = mdc::mdSubpelSearchFixedStage(&ctx, ref, srcPlane.data(), 64, &me_mv, sub2);
    CHECK(got2 == want2);
    CHECK(me_mv.x == 0);
}

// trip 16: md_subpel_search (product_coding_loop.c:2462-:2578). The setup
// is the ported body: the mv_limits derivation, the two search-range
// calls (the landed l5 helpers), the mv-cost params, the ref/src buf2d
// windows and the subpel start mv. The subpel TREE is the A3M park - the
// stub records what it received and returns a canned winner.
TEST_CASE("l6 mdc mdSubpelSearch derives the limits and hands the tree the right windows") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_16X16;
    ctx.geom.bwidth = 16;
    ctx.geom.bheight = 16;
    ctx.geom.sq_size = 16;
    ctx.blk_org_x = 48;
    ctx.blk_org_y = 32;
    ctx.ref_mv.x = 24;
    ctx.ref_mv.y = -40;
    ctx.md_subpel_skip_diag_refinement = 2;
    ctx.full_lambda_md[0] = 4096;

    std::vector<std::uint8_t> srcPlane(128 * 128, 7);
    std::vector<std::uint8_t> refPlane(256 * 256, 9);
    mdc::PicBuf input{};
    input.buf = srcPlane.data();
    input.stride = 128;
    input.width = 128;
    input.height = 128;
    mdc::PicBuf refP{};
    refP.buf = refPlane.data();
    refP.stride = 256;
    refP.width = 192;
    refP.height = 192;

    mdc::MdSubPelCtrls ctrls{};
    ctrls.max_precision = mdc::kQuarterPel;
    ctrls.subpel_iters_per_step = 3;
    ctrls.subpel_search_type = mdc::kUse4Taps;
    ctrls.pred_variance_th = 11;
    ctrls.abs_th_mult = 5;
    ctrls.round_dev_th = 7;
    ctrls.skip_diag_refinement = 2;
    ctrls.bias_fp = 44;

    static mdc::MdSubpelParams seen;
    static pipeline::Mv2 seen_start;
    auto treeStub = [](mdc::FlCtx& c, const mdc::MdSubpelParams& p, pipeline::Mv2 start, pipeline::Mv2* best) {
        (void)c;
        seen = p;
        seen_start = start;
        best->x = 33;
        best->y = -17;
        return 4321;
    };

    pipeline::Mv2 me_mv{};
    me_mv.x = 33 * 8;  // best_mv.x = 33, the start re-expands to 264
    me_mv.y = -17 * 8;
    const int besterr = mdc::mdSubpelSearch(&ctx, input, refP, mdc::kSpelMe, ctrls, amvp::kLastFrame, &me_mv, 1, 2, 3,
                                            36, 36, 100, treeStub);
    CHECK(besterr == 4321);
    CHECK(me_mv.x == 33);
    CHECK(me_mv.y == -17);
    CHECK(seen_start.x == 264);
    CHECK(seen_start.y == -136);
    CHECK(seen.list_idx == 0);
    CHECK(seen.ref_idx == 0);
    CHECK(seen.allow_hp == 1);
    CHECK(seen.forced_stop == mdc::kQuarterPel);
    CHECK(seen.iters_per_step == 3);
    CHECK(seen.subpel_search_type == mdc::kUse4Taps);
    CHECK(seen.var_w == 16);
    CHECK(seen.var_h == 16);
    CHECK(seen.pred_variance_th == 11);
    CHECK(seen.abs_th_mult == 5);
    CHECK(seen.round_dev_th == 7);
    CHECK(seen.skip_diag_refinement == 2);
    CHECK(seen.bias_fp == 44);
    CHECK(seen.mv_cost_params.error_per_bit == 64);  // 4096 >> 6
    CHECK(seen.mv_cost_params.mv_cost_type == pipeline::MV_COST_ENTROPY);
    CHECK(seen.src_buf.buf == srcPlane.data() + 32 * 128 + 48);
    CHECK(seen.src_buf.stride == 128);
    CHECK(seen.ref_buf.buf == refPlane.data() + 32 * 256 + 48);
    CHECK(seen.ref_buf.stride == 256);
    CHECK(seen.ref_buf.width == 192);
    // The mv_limits derivation + the two search-range calls against the
    // independent transcription: the raw limits are row [-28, 140] / col
    // [-32, 136] (col_max = (36-3)*4+4); set_mv_search_range's ref_mv
    // window does not intersect-tighten them; set_subpel_mv_search_range
    // scales by 8 around ref_mv and clamps to the MV_LOW/MV_UPP band.
    CHECK(seen.row_min == -224);
    CHECK(seen.col_min == -256);
    CHECK(seen.row_max == 1120);
    CHECK(seen.col_max == 1088);
    // The list/ref split for a BWDREF (list 1, ref 0).
    me_mv.x = 0;
    me_mv.y = 0;
    mdc::mdSubpelSearch(&ctx, input, refP, mdc::kSpelMe, ctrls, amvp::kBwdrefFrame, &me_mv, 1, 2, 3, 36, 36, 100,
                        treeStub);
    CHECK(seen.list_idx == 1);
    CHECK(seen.ref_idx == 0);
}

// Shared read_refine fixtures: a bordered ref plane whose block at
// (blk_org + shift) matches the src block exactly, and stub callables.
namespace {
struct RrmFixture {
    std::vector<std::uint8_t> srcPlane;
    std::vector<std::uint8_t> refPlane;
    mdc::PicBuf input{};
    mdc::PicBuf refP{};
    std::vector<std::uint8_t> totalCnt;
    std::vector<l16_rdo::MeCandidate> cands;
    std::vector<pipeline::Mv2> mvArr;
    mdc::MdMeSbResults me{};
};
RrmFixture makeRrm(int shift_x, int shift_y) {
    RrmFixture fx;
    fx.srcPlane.assign(128 * 128, 0);
    fx.refPlane.assign(192 * 192, 0);
    for (int r = 0; r < 128; ++r)
        for (int c = 0; c < 128; ++c)
            fx.srcPlane[r * 128 + c] = static_cast<std::uint8_t>(((r * 128 + c) * 2654435761u) >> 24);
    for (int r = 0; r < 192; ++r)
        for (int c = 0; c < 192; ++c) {
            const int sy = ((r - 32 + shift_y) % 128 + 128) % 128;
            const int sx = ((c - 32 + shift_x) % 128 + 128) % 128;
            fx.refPlane[r * 192 + c] = fx.srcPlane[sy * 128 + sx];
        }
    fx.input.buf = fx.srcPlane.data();
    fx.input.stride = 128;
    fx.refP.buf = fx.refPlane.data() + 32 * 192 + 32;  // the bordered picture origin
    fx.refP.stride = 192;
    fx.refP.max_width = 128;
    fx.refP.max_height = 128;
    fx.refP.border = 32;
    fx.totalCnt.assign(85, 0);
    fx.cands.assign(85 * 23, {});
    fx.mvArr.assign(85 * 8, pipeline::Mv2{0, 0});
    fx.me.total_me_candidate_index = fx.totalCnt.data();
    fx.me.me_mv_array = fx.mvArr.data();
    fx.me.me_candidate_array = fx.cands.data();
    return fx;
}
void plantRrmMe(RrmFixture& fx, std::uint32_t block_offset, int ref, int mv_x, int mv_y) {
    fx.totalCnt[block_offset] = 1;
    l16_rdo::MeCandidate& c = fx.cands[block_offset * 23];
    c.direction = 0;
    c.ref0List = 0;
    c.refIdxL0 = static_cast<std::uint8_t>(ref);
    fx.mvArr[block_offset * 8 + ref] = pipeline::Mv2{static_cast<std::int16_t>(mv_x), static_cast<std::int16_t>(mv_y)};
}
int g_choose_calls = 0;
pipeline::Mv2 g_choose_out = {};
void chooseZero(mdc::FlCtx& ctx, std::int32_t ref_pair, pipeline::Mv2 me_mv, pipeline::Mv2* best_pred_mv) {
    (void)ctx;
    (void)ref_pair;
    (void)me_mv;
    g_choose_calls++;
    best_pred_mv[0] = g_choose_out;
}
int g_tree_calls = 0;
pipeline::Mv2 g_tree_out = {};
int g_tree_err = 0;
int treeStub(mdc::FlCtx& ctx, const mdc::MdSubpelParams& p, pipeline::Mv2 start, pipeline::Mv2* best) {
    (void)ctx;
    (void)p;
    (void)start;
    g_tree_calls++;
    *best = g_tree_out;
    return g_tree_err;
}
}  // namespace

// trip 17: read_refine_me_mvs_light_pd1 (product_coding_loop.c:2685-:2760).
TEST_CASE("l6 mdc readRefineMeMvsLightPd1 runs subpel and books the mv slots") {
    RrmFixture fx = makeRrm(3, -2);  // the exact match at integer (+3, -2) -> mv (24, -16)
    plantRrmMe(fx, 5, 0, 3, -2);

    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.geom.sq_size = 8;
    ctx.blk_org_x = 16;
    ctx.blk_org_y = 16;
    ctx.totRefFrameTypes = 1;
    ctx.refFrameTypeArr[0] = amvp::kLastFrame;  // single ref: rf[1] = NONE
    ctx.refPicArr[0][0] = &fx.refP;
    ctx.mdSubpelSearchMethod = mdc::kSubpelTree;  // the tree path
    ctx.mdSubpelMinBlkSz = 4;
    g_choose_calls = 0;
    g_tree_calls = 0;
    g_tree_out.x = 24;
    g_tree_out.y = -16;
    g_tree_err = 123;

    mdc::MdSubPelCtrls sub{};
    sub.enabled = 1;
    mdc::readRefineMeMvsLightPd1(&ctx, fx.input, &fx.me, 5, 5 * 23, 4, 8, 100, chooseZero, treeStub, sub, 0, 0, 16,
                                 16);
    CHECK(g_tree_calls == 1);
    CHECK(g_choose_calls == 1);
    CHECK(ctx.postSubpelMeMvCost[0][0] == 123);
    CHECK(ctx.md_me_dist == 123);
    CHECK(ctx.sbMeMv[0][0].x == 24);
    CHECK(ctx.sbMeMv[0][0].y == -16);
    CHECK(ctx.ref_mv.x == 0);
    CHECK(ctx.ref_mv.y == 0);  // no_mv_stack via shut_fast_rate = 0 + the stub writes (0,0)
    // shut_fast_rate skips the choose call and zeroes the ref mv.
    ctx.shut_fast_rate = 1;
    g_choose_calls = 0;
    g_tree_calls = 0;
    mdc::readRefineMeMvsLightPd1(&ctx, fx.input, &fx.me, 5, 5 * 23, 4, 8, 100, chooseZero, treeStub, sub, 0, 0, 16,
                                 16);
    CHECK(g_choose_calls == 0);
    CHECK(g_tree_calls == 1);
    CHECK(ctx.ref_mv.as_int == 0);

    // The skip_subpel gate: sq_size <= min_blk_sz -> no subpel, the raw
    // ME mv is booked (clipped).
    ctx.shut_fast_rate = 0;
    ctx.geom.sq_size = 4;
    ctx.mdSubpelMinBlkSz = 8;
    g_tree_calls = 0;
    mdc::readRefineMeMvsLightPd1(&ctx, fx.input, &fx.me, 5, 5 * 23, 4, 8, 100, chooseZero, treeStub, sub, 0, 0, 16,
                                 16);
    CHECK(g_tree_calls == 0);
    CHECK(ctx.md_me_dist == (std::uint32_t)~0);
    CHECK(ctx.sbMeMv[0][0].x == 24);
    CHECK(ctx.sbMeMv[0][0].y == -16);
}

// trip 18: read_refine_me_mvs (product_coding_loop.c:2764-:2887). The
// three me_mv derivation branches, the nsq/sq dispatch on b_w_ne_h, the
// subpel/fp-me-dist fork, and the PART_N sq write-back.
TEST_CASE("l6 mdc readRefineMeMvs derives the me_mv per branch and dispatches nsq/sq") {
    RrmFixture fx = makeRrm(0, 0);  // the exact match at (0,0) -> mv 0
    plantRrmMe(fx, 5, 0, 0, 0);

    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_16X8;  // b_w_ne_h, not the 64x128 pair
    ctx.geom.bwidth = 16;
    ctx.geom.bheight = 8;
    ctx.geom.sq_size = 16;
    ctx.blk_org_x = 0;
    ctx.blk_org_y = 0;
    ctx.sb_origin_x = 0;
    ctx.sb_origin_y = 0;
    ctx.geom_offset_x = 0;
    ctx.geom_offset_y = 0;
    ctx.totRefFrameTypes = 1;
    ctx.refFrameTypeArr[0] = amvp::kLastFrame;
    ctx.refPicArr[0][0] = &fx.refP;
    ctx.meBlockOffset = 5;
    ctx.meCandOffset = 5 * 23;
    ctx.mdSubpelSearchMethod = mdc::kSubpelTree;
    ctx.md_subpel_skip_diag_refinement = 3;  // MV_COST_OPT: table-free mv err
    ctx.enableMe8x8 = 1;
    ctx.enableMe16x16 = 1;
    g_choose_calls = 0;
    g_tree_calls = 0;
    g_tree_out.x = 0;
    g_tree_out.y = 0;
    g_tree_err = 55;

    mdc::MdSubPelCtrls sub{};
    sub.enabled = 0;  // the fp_me_dist fallback path
    mdc::MdNsqCtrls nsq{};
    nsq.enabled = 1;
    nsq.dist_type = mdc::DIST_SAD;
    nsq.full_pel_search_width = 0;
    nsq.full_pel_search_height = 0;
    nsq.enable_psad = 0;
    mdc::MdSqCtrls sq{};
    sq.enabled = 1;

    // Branch (a): blk_avail_sqi && b_w_ne_h -> the sq carrier MV rounded;
    // the NSQ search then finds the exact match at (0,0) (SAD 0 + err 0)
    // and the ladder keeps it.
    ctx.sqSbMeMv[0][0].x = 11;  // (11 + 4) & ~7 = 8
    ctx.sqSbMeMv[0][0].y = -5;  // (-5 + 4) & ~7 = -8
    ctx.updatedEnablePme = 1;
    ctx.fpMeDist[0][0] = 999;
    mdc::readRefineMeMvs(&ctx, fx.input, &fx.me, 4, 8, 23, 100, chooseZero, treeStub, sub, nsq, sq, 0, 0, 16, 16,
                         true, false);
    CHECK(g_choose_calls == 1);
    CHECK(g_tree_calls == 0);  // subpel disabled
    CHECK(ctx.fpMeMv[0][0].x == 0);
    CHECK(ctx.fpMeMv[0][0].y == 0);
    // The fp_me_dist fallback ran (subpel off + updated_enable_pme): the
    // variance at the exact match is 0 + the mv err 0.
    CHECK(ctx.fpMeDist[0][0] == 0);
    CHECK(ctx.sbMeMv[0][0].x == 0);
    CHECK(ctx.sbMeMv[0][0].y == 0);
    CHECK(ctx.subMeMv[0][0].x == 0);
    // shape == PART_N -> the sq write-back.
    ctx.shape = 0;  // PART_N
    CHECK(ctx.sqSbMeMv[0][0].x == 0);
    CHECK(ctx.sqSbMeMv[0][0].y == 0);

    // Branch (c): sqi unavailable -> the me_mv_array read (0,0 here).
    ctx.shape = 3;  // not PART_N
    ctx.sqSbMeMv[0][0].x = 77;
    ctx.sqSbMeMv[0][0].y = 77;
    ctx.fpMeDist[0][0] = 999;
    mdc::readRefineMeMvs(&ctx, fx.input, &fx.me, 4, 8, 23, 100, chooseZero, treeStub, sub, nsq, sq, 0, 0, 16, 16,
                         false, false);
    CHECK(ctx.fpMeMv[0][0].x == 0);
    CHECK(ctx.fpMeMv[0][0].y == 0);
    CHECK(ctx.sqSbMeMv[0][0].x == 77);  // not PART_N -> untouched

    // Branch (b): BLOCK_4X4 with a tested parent -> the sq MV rounded;
    // the 4x4 block takes neither NSQ (b_w_ne_h false) nor SQ.
    ctx.geom.bsize = entropy::BLOCK_4X4;
    ctx.geom.bwidth = 4;
    ctx.geom.bheight = 4;
    ctx.geom.sq_size = 4;
    ctx.sqSbMeMv[0][0].x = 11;
    ctx.sqSbMeMv[0][0].y = -5;
    ctx.fpMeDist[0][0] = 999;
    mdc::readRefineMeMvs(&ctx, fx.input, &fx.me, 4, 8, 23, 100, chooseZero, treeStub, sub, nsq, sq, 0, 0, 16, 16,
                         false, true);
    CHECK(ctx.fpMeMv[0][0].x == 8);
    CHECK(ctx.fpMeMv[0][0].y == -8);
}

// trip 19: get_sb_tpl_inter_stats (product_coding_loop.c:2895-:2955). The
// SB walk over the flat TPL stats array: the dispenser level -> block
// size/step, the intra/inter classification (is_intra_mode vs
// NEARESTMV), the per-list max ref idx and the inter percentage.
TEST_CASE("l6 mdc getSbTplInterStats walks the SB grid and classifies inter") {
    // A 64x64 SB at origin (0,0); dispenser level 0 -> 16x16 blocks, step
    // 1: 4x4 = 16 blocks in a 128-wide aligned grid.
    std::vector<mdc::TplSrcStatsWindow> stats(8 * 128);
    for (auto& s : stats) {
        s.best_mode = l16_rdo::DC_PRED;   // intra
        s.best_rf_idx = 0;
    }
    // Make blocks (1,0) and (2,1) inter: list 1 ref 2 and list 0 ref 3.
    stats[0 * 128 + 1] = mdc::TplSrcStatsWindow{l16_rdo::NEARESTMV, 6};   // list 1, ref 2
    stats[1 * 128 + 2] = mdc::TplSrcStatsWindow{l16_rdo::NEWMV, 3};       // list 0, ref 3

    mdc::TplPcsWindow ppcs{};
    ppcs.tpl_enable = 1;
    ppcs.tpl_src_data_ready = 1;
    ppcs.disable_intra_pred_nref = 0;
    ppcs.temporal_layer_index = 1;
    ppcs.hierarchical_levels = 2;
    ppcs.aligned_width = 2048;  // aligned16_width = 128
    ppcs.dispenser_search_level = 0;
    ppcs.sb_width = 64;
    ppcs.sb_height = 64;
    ppcs.tpl_src_stats = stats.data();
    ppcs.tpl_src_stride = 128;

    std::uint8_t inter_sel = 0, max0 = 0, max1 = 0;
    bool ok = mdc::getSbTplInterStats(ppcs, 0, 0, &inter_sel, &max0, &max1);
    CHECK(ok);
    CHECK(inter_sel == (2 * 100) / 16);
    CHECK(max0 == 3);
    CHECK(max1 == 2);

    // The gate: tpl off -> false and the outs untouched.
    mdc::TplPcsWindow off = ppcs;
    off.tpl_enable = 0;
    inter_sel = 7;
    CHECK_FALSE(mdc::getSbTplInterStats(off, 0, 0, &inter_sel, &max0, &max1));
    CHECK(inter_sel == 7);

    // The disable_intra_pred_nref gate at the top temporal layer.
    mdc::TplPcsWindow nref = ppcs;
    nref.temporal_layer_index = 2;
    nref.hierarchical_levels = 2;
    nref.disable_intra_pred_nref = 1;
    CHECK_FALSE(mdc::getSbTplInterStats(nref, 0, 0, &inter_sel, &max0, &max1));

    // The dispenser level 2: 64-blocks, step 4 - a 64x64 SB holds exactly
    // ONE block (the (0,0) one), so the single visited slot decides.
    std::vector<mdc::TplSrcStatsWindow> stats2(16 * 128);
    for (auto& s : stats2) {
        s.best_mode = l16_rdo::PAETH_PRED;
        s.best_rf_idx = 0;
    }
    stats2[0] = mdc::TplSrcStatsWindow{l16_rdo::NEARESTMV, 5};  // rf_idx 5 = list 1, ref 1
    mdc::TplPcsWindow lv2 = ppcs;
    lv2.dispenser_search_level = 2;
    lv2.tpl_src_stats = stats2.data();
    ok = mdc::getSbTplInterStats(lv2, 0, 0, &inter_sel, &max0, &max1);
    CHECK(ok);
    CHECK(inter_sel == 100);
    CHECK(max0 == 0);
    CHECK(max1 == 1);
}

// trip 20: perform_md_reference_pruning (product_coding_loop.c:2957-:3045).
// The early-distortion collection, the closest-multiplier shortcut and the
// deviation-threshold branch over the full (group, list, ref) grid.
TEST_CASE("l6 mdc performMdReferencePruning fills the filtering verdicts") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.totRefFrameTypes = 2;
    ctx.refFrameTypeArr[0] = amvp::kLastFrame;    // list 0 ref 0
    ctx.refFrameTypeArr[1] = amvp::kBwdrefFrame;  // list 1 ref 0
    ctx.meBlockOffset = 5;
    ctx.meCandOffset = 5 * 23;

    RrmFixture fx = makeRrm(0, 0);
    fx.totalCnt[5] = 1;  // ME data present for the block
    l16_rdo::MeCandidate& c = fx.cands[5 * 23];
    c.direction = 0;
    c.ref0List = 0;
    c.refIdxL0 = 0;

    ctx.bestFpMvpDist[0][0] = 1000;
    ctx.bestFpMvpDist[1][0] = 2000;
    ctx.fpMeDist[0][0] = 800;   // the ME data present -> min 800
    ctx.fpMeDist[1][0] = 3000;  // present too -> min(3000, 2000) = 2000

    l16_rdo::RefPruningCtrls pruning{};
    // check_closest_multiplier = 0 -> the deviation branch.
    pruning.maxDevToBest[0] = 50;              // PA_ME_GROUP
    for (int g = 1; g < TOT_INTER_GROUP; ++g) pruning.maxDevToBest[g] = (std::uint32_t)~0;
    pruning.useTplInfoOffset = 0;
    pruning.checkClosestMultiplier = 0;

    mdc::performMdReferencePruning(&ctx, pruning, &fx.me, nullptr, 30);
    // min_dist = 800. dev[l0r0] = 0 -> every group: 0 < th -> do_ref=1.
    // dev[l1r0] = (2000-800)*100/800 = 150 -> do_ref only where th > 150:
    // gi=0 th=50 -> no; gi!=0 th=~0 -> yes.
    for (int g = 0; g < TOT_INTER_GROUP; ++g) {
        CHECK(ctx.refFilteringRes[g][0][0] == 1);
        CHECK(ctx.refFilteringRes[g][1][0] == (g == 0 ? 0 : 1));
        // ri != 0 slots keep the 0xFE preset: dev is huge, so the th =
        // ~0 groups enable them and th = 50 does not (the reference's own
        // preset interaction).
        CHECK(ctx.refFilteringRes[g][0][1] == (g == 0 ? 0 : 1));
    }

    // The closest-multiplier shortcut: th = 1 * 64 * 30 / 24 = 80; both
    // early[0] (800) and early[4] (2000) are >= 80 -> the shortcut does
    // NOT fire. Make the distortions tiny to fire it.
    ctx.bestFpMvpDist[0][0] = 10;
    ctx.bestFpMvpDist[1][0] = 20;
    ctx.fpMeDist[0][0] = 5;
    ctx.fpMeDist[1][0] = 6;
    pruning.checkClosestMultiplier = 1;
    mdc::performMdReferencePruning(&ctx, pruning, &fx.me, nullptr, 30);
    // ri == 0 (every list) + every group -> do_ref = 1; ri != 0: the shortcut writes ri==0 always, plus every slot for
    // the th = ~0 groups (g != 0 here).
    for (int g = 0; g < TOT_INTER_GROUP; ++g) {
        CHECK(ctx.refFilteringRes[g][0][0] == 1);
        CHECK(ctx.refFilteringRes[g][1][0] == 1);
        // ri != 0: the shortcut writes ri==0 always, plus every slot for
        // the th = ~0 groups (g != 0 here).
        CHECK(ctx.refFilteringRes[g][0][2] == (g == 0 ? 0 : 1));
    }

    // The mixed case: only early[0] under th -> the shortcut must NOT
    // fire (the conjunction), so the list-1 verdicts follow the deviation
    // branch (dev[l1r0] = (2000-800)*100/800 = 150 -> th 50 rejects g0).
    ctx.bestFpMvpDist[0][0] = 10;
    ctx.bestFpMvpDist[1][0] = 2000;
    ctx.fpMeDist[0][0] = 5;
    ctx.fpMeDist[1][0] = 3000;
    mdc::performMdReferencePruning(&ctx, pruning, &fx.me, nullptr, 30);
    CHECK(ctx.refFilteringRes[0][0][0] == 1);  // early[0] = 5 < 80... no wait
    CHECK(ctx.refFilteringRes[0][1][0] == 0);  // the deviation branch: 150 > 50
    CHECK(ctx.refFilteringRes[1][1][0] == 1);  // th = ~0
    pruning.checkClosestMultiplier = 0;

    // The TPL offset: use_tpl_info_offset with a tpl window whose TPL
    // blocks picked list-0 refs only -> sb_max_list1 = 0, so the list-1
    // ref-1 pair gains the offset and its threshold drops below the dev.
    std::vector<mdc::TplSrcStatsWindow> stats(8 * 128);
    for (auto& s : stats) {
        s.best_mode = l16_rdo::NEARESTMV;
        s.best_rf_idx = 0;  // list 0 ref 0
    }
    mdc::TplPcsWindow tpl{};
    tpl.tpl_enable = 1;
    tpl.tpl_src_data_ready = 1;
    tpl.temporal_layer_index = 0;
    tpl.hierarchical_levels = 0;
    tpl.aligned_width = 2048;
    tpl.dispenser_search_level = 0;
    tpl.sb_width = 64;
    tpl.sb_height = 64;
    tpl.tpl_src_stats = stats.data();
    tpl.tpl_src_stride = 128;
    ctx.totRefFrameTypes = 3;
    ctx.refFrameTypeArr[2] = amvp::kAltref2Frame;  // list 1 ref 1
    pruning.checkClosestMultiplier = 0;
    pruning.useTplInfoOffset = 30;
    for (int g = 0; g < TOT_INTER_GROUP; ++g) pruning.maxDevToBest[g] = 100;
    ctx.bestFpMvpDist[0][0] = 1000;
    ctx.bestFpMvpDist[1][0] = 1000;
    ctx.bestFpMvpDist[1][1] = 1360;  // dev = (1360-800)*100/800 = 70
    ctx.fpMeDist[0][0] = 800;
    ctx.fpMeDist[1][0] = 800;
    mdc::performMdReferencePruning(&ctx, pruning, &fx.me, &tpl, 30);
    // dev[l0r0] = 0 < 100 -> 1; dev[l1r0] = 0 < 100 -> 1; dev[l1r1] = 70:
    // the offset 30 drops th to 70 -> 70 < 70 false -> 0.
    CHECK(ctx.refFilteringRes[0][0][0] == 1);
    CHECK(ctx.refFilteringRes[0][1][0] == 1);
    CHECK(ctx.refFilteringRes[0][1][1] == 0);
    // Without the TPL window the same dev passes th = 100.
    mdc::performMdReferencePruning(&ctx, pruning, &fx.me, nullptr, 30);
    CHECK(ctx.refFilteringRes[0][1][1] == 1);
}

// trip 21: build_single_ref_mvp_array (product_coding_loop.c:3050-:3141).
// The NEAREST + dedup'd NEAR injection, the rounding + clip, the
// shut_fast_rate single-{0} path and the best-FP-MVP selection.
TEST_CASE("l6 mdc buildSingleRefMvpArray injects the nearest and near MVPs") {
    RrmFixture fx = makeRrm(4, 0);  // the exact match at integer (+4, 0) -> mv (32, 0)
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.blk_org_x = 40;
    ctx.blk_org_y = 40;
    ctx.totRefFrameTypes = 1;
    ctx.refFrameTypeArr[0] = amvp::kLastFrame;
    ctx.refPicArr[0][0] = &fx.refP;

    // The ref_mv_stack for LAST_FRAME (frame_type 1): NEAREST (-32, 0)
    // (the exact match: ref(x) = src(x+4) minimizes at dx = -4), NEAR[0]
    // (24, 0), NEAR[1] (-32, 0) (a NEAREST duplicate -> dedup'd).
    pipeline::Mv2 stack[3];
    stack[0] = pipeline::Mv2{-32, 0};
    stack[1] = pipeline::Mv2{24, 0};
    stack[2] = pipeline::Mv2{-32, 0};
    mdc::MdRefMvStack rms{};
    rms.thisMv[amvp::kLastFrame] = stack;

    std::vector<pipeline::Mv2> mvp(8);
    ctx.mvpArr[0][0] = mvp.data();

    mdc::buildSingleRefMvpArray(&ctx, fx.input, 100, rms);
    CHECK(ctx.mvpCnt[0][0] == 2);  // NEAR[1] dedup'd against NEAREST
    CHECK(mvp[0].x == -32);
    CHECK(mvp[0].y == 0);
    CHECK(mvp[1].x == 24);
    CHECK(mvp[1].y == 0);
    // The best-FP-MVP: the (-32, 0) MVP sits on the exact match -> var 0.
    CHECK(ctx.bestFpMvpIdx[0][0] == 0);
    CHECK(ctx.bestFpMvpDist[0][0] == 0);

    // shut_fast_rate: the single {0} MVP, count 1.
    ctx.shut_fast_rate = 1;
    mdc::buildSingleRefMvpArray(&ctx, fx.input, 100, rms);
    CHECK(ctx.mvpCnt[0][0] == 1);
    CHECK(mvp[0].as_int == 0);
}

// trip 22: pme_search (product_coding_loop.c:3150-:3325). The early-exit
// chain (the mvp-direction check, the pre/post fp dev gates), the qp-
// scaled search area and the md_pme_dist fold.
TEST_CASE("l6 mdc pmeSearch runs the early-exit chain and the full-pel search") {
    RrmFixture fx = makeRrm(0, 0);  // the exact match at (0,0) -> mv 0
    plantRrmMe(fx, 5, 0, 0, 0);

    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_8X8;
    ctx.geom.bwidth = 8;
    ctx.geom.bheight = 8;
    ctx.geom.sq_size = 8;
    ctx.blk_org_x = 16;
    ctx.blk_org_y = 16;
    ctx.sb_origin_x = 0;
    ctx.sb_origin_y = 0;
    ctx.totRefFrameTypes = 1;
    ctx.refFrameTypeArr[0] = amvp::kLastFrame;
    ctx.refPicArr[0][0] = &fx.refP;
    ctx.meBlockOffset = 5;
    ctx.meCandOffset = 5 * 23;
    ctx.md_subpel_skip_diag_refinement = 3;
    ctx.full_lambda_md[0] = 1024;
    ctx.md_pme_dist = (std::uint32_t)~0;
    std::vector<pipeline::Mv2> mvp(8);
    ctx.mvpArr[0][0] = mvp.data();
    mvp[0] = pipeline::Mv2{0, 0};  // the best MVP: the exact match
    ctx.mvpCnt[0][0] = 1;
    ctx.bestFpMvpIdx[0][0] = 0;
    ctx.bestFpMvpDist[0][0] = 0;
    ctx.fpMeMv[0][0].x = 0;
    ctx.fpMeMv[0][0].y = 0;
    ctx.fpMeDist[0][0] = 0;  // the ME MV is already perfect
    ctx.subMeMv[0][0].x = 0;
    ctx.subMeMv[0][0].y = 0;
    ctx.postSubpelMeMvCost[0][0] = 77;

    for (int l = 0; l < 2; ++l) for (int r = 0; r < 4; ++r) ctx.pmeResDist[l][r] = (std::uint32_t)~0;  // the caller's reset
    mdc::MdPmeCtrls pme{};
    pme.dist_type = mdc::DIST_SAD;
    pme.full_pel_search_width = 8;
    pme.full_pel_search_height = 8;
    pme.early_check_mv_th_multiplier = (-2147483647 - 1);  // off
    pme.pre_fp_pme_to_me_cost_th = 50;
    pme.pre_fp_pme_to_me_mv_th = 4;
    pme.post_fp_pme_to_me_cost_th = 50;
    pme.post_fp_pme_to_me_mv_th = 4;
    pme.enable_psad = 0;
    pme.sa_q_weight = 0;

    mdc::MdSubPelCtrls subPme{};
    subPme.enabled = 0;
    mdc::MdSubPelCtrls subMe{};
    subMe.subpel_search_method = mdc::kSubpelTree;

    g_choose_calls = 0;
    g_tree_calls = 0;
    l16_rdo::RefPruningCtrls pruning{};
    l16_rdo::RefFilteringRes filtering = {};
    for (int g = 0; g < TOT_INTER_GROUP; ++g)
        for (int l = 0; l < 2; ++l)
            for (int r = 0; r < 4; ++r) filtering[g][l][r].doRef = 1;  // every ref valid

    // The pre-FP gate: the ME MV == the best MVP and dev = ((1-1)*100)/1
    // = 0 < 50 -> PME = ME_MV, no search.
    mdc::pmeSearch(&ctx, fx.input, &fx.me, pme, subPme, 100, 30, 0, 0, 1, pruning, filtering, treeStub, subMe,
                   chooseZero, 0, 0, 16, 16);
    CHECK(g_tree_calls == 0);
    CHECK(ctx.validPmeMv[0][0] == 1);
    CHECK(ctx.pmeResDist[0][0] == 77);
    CHECK(ctx.bestPmeMv[0][0].x == 0);
    CHECK(ctx.md_pme_dist == 77);

    // The full search: make the ME MV far away so the pre gate misses.
    ctx.fpMeMv[0][0].x = 400;
    ctx.fpMeMv[0][0].y = 0;
    ctx.fpMeDist[0][0] = 5000;
    ctx.md_pme_dist = (std::uint32_t)~0;
    for (int l = 0; l < 2; ++l) for (int r = 0; r < 4; ++r) ctx.pmeResDist[l][r] = (std::uint32_t)~0;
    g_tree_calls = 0;
    g_tree_out.x = 8;
    g_tree_out.y = 0;
    g_tree_err = 99;
    subPme.enabled = 1;
    mdc::pmeSearch(&ctx, fx.input, &fx.me, pme, subPme, 100, 30, 0, 0, 1, pruning, filtering, treeStub, subMe,
                   chooseZero, 0, 0, 16, 16);
    // The post-FP gate: |400 - 8| = 392 > 4 and dev = ((? - 5000+..)*100)/
    // ... the post mv th misses -> the search result books.
    CHECK(g_tree_calls == 1);
    CHECK(ctx.bestPmeMv[0][0].x == 8);
    CHECK(ctx.validPmeMv[0][0] == 1);
    CHECK(ctx.pmeResDist[0][0] == 99);
    CHECK(ctx.md_pme_dist == 99);

    // The pme_ref0_only skip: a LAST2 (list 0 ref 1) pair is skipped
    // entirely (no search, no verdict) while LAST (ref 0) runs.
    ctx.totRefFrameTypes = 2;
    ctx.refFrameTypeArr[1] = amvp::kLast2Frame;  // list 0 ref 1
    ctx.bestFpMvpDist[0][1] = 10;
    ctx.fpMeDist[0][1] = 5;
    ctx.md_pme_dist = (std::uint32_t)~0;
    for (int l = 0; l < 2; ++l) for (int r = 0; r < 4; ++r) ctx.pmeResDist[l][r] = (std::uint32_t)~0;
    g_tree_calls = 0;
    mdc::pmeSearch(&ctx, fx.input, &fx.me, pme, subPme, 100, 30, 0, 1, 1, pruning, filtering, treeStub, subMe,
                   chooseZero, 0, 0, 16, 16);
    CHECK(g_tree_calls == 1);  // only the ref-0 pair reached the search
    CHECK(ctx.validPmeMv[0][1] == 0);  // the skipped pair never set a verdict
    CHECK(ctx.pmeResDist[0][1] == (std::uint32_t)~0);
}

// trip 23: the inverse-recon chain (product_coding_loop.c:695-:953 +
// full_loop.c:1755-:1789 + inv_transforms.c:3139-:3165). The 8-bit wrapper
// routes through the landed l3 invTxfmAddStagingHb; the test verifies the
// recon = pred + invTxfm2dAdd composition against the landed l3 inverse.
TEST_CASE("l6 mdc invTransformReconWrapper 8bit composes the staging inverse") {
    const int N = 8;
    std::vector<std::int32_t> coeff(N * N, 0);
    // A DCT_DCT-ish sparse pattern: DC 1000, one AC 500.
    coeff[0] = 1000;
    coeff[1] = 500;
    std::vector<std::uint8_t> predBuf(N * N);
    std::vector<std::uint8_t> reconBuf(N * N);
    for (int i = 0; i < N * N; ++i) predBuf[i] = static_cast<std::uint8_t>(i * 7 + 1);

    mdc::invTransformReconWrapper(coeff.data(), 0, predBuf.data(), 0, N, reconBuf.data(), 0, N, false,
                                  entropy::TX_8X8, entropy::DCT_DCT, 2, false);

    // The landed l3 twin: the same dqcoeff through invTxfm2dAdd8x8 + pred.
    std::vector<std::int32_t> dq(N * N, 0);
    std::memcpy(dq.data(), coeff.data(), sizeof(std::int32_t) * N * N);
    std::vector<std::uint16_t> recon16(N * N);
    transforms::invTxfm2dAddHb(dq.data(), recon16.data(), N, transforms::TxSize::TX_8X8,
                               transforms::TxType::DCT_DCT, 8);
    for (int i = 0; i < N * N; ++i) {
        const int want = std::clamp<int>(predBuf[i] + recon16[i], 0, 255);
        CHECK(reconBuf[i] == want);
    }
}

TEST_CASE("l6 mdc invTransformReconWrapper forces the eob when r != w") {
    const int N = 8;
    std::vector<std::int32_t> coeff(N * N, 0);
    coeff[0] = 1000;  // eob 1 would zero everything else; the forced eob copies all
    std::vector<std::uint8_t> predBuf(N * N, 0);
    std::vector<std::uint8_t> reconBuf(N * N, 0);
    // r != w: the recon writes into a separate buffer.
    mdc::invTransformReconWrapper(coeff.data(), 0, predBuf.data(), 0, N, reconBuf.data(), 0, N, false,
                                  entropy::TX_8X8, entropy::DCT_DCT, 1, false);
    // The staging path with the forced max eob (64) replicates the full
    // inverse; the same coeff with eob 1 through the landed twin:
    std::vector<std::int32_t> dq(N * N, 0);
    std::memcpy(dq.data(), coeff.data(), sizeof(std::int32_t) * N * N);
    std::vector<std::uint16_t> recon16(N * N, 0);
    transforms::TxfmParamHb p{};
    p.txType = transforms::TxType::DCT_DCT;
    p.txSize = transforms::TxSize::TX_8X8;
    p.eob = mdc::av1GetMaxEob(entropy::TX_8X8);
    p.lossless = 0;
    p.bd = 8;
    p.isHbd = 1;
    transforms::invTxfmAddStagingHb(dq.data(), predBuf.data(), N, reconBuf.data(), N, p);
    std::vector<std::uint16_t> recon16b(N * N);
    transforms::invTxfm2dAddHb(dq.data(), recon16b.data(), N, transforms::TxSize::TX_8X8,
                               transforms::TxType::DCT_DCT, 8);
    for (int i = 0; i < N * N; ++i) {
        CHECK(reconBuf[i] == std::clamp<int>(recon16b[i], 0, 255));
    }
    CHECK(mdc::av1GetMaxEob(entropy::TX_8X8) == 64);
    CHECK(mdc::av1GetMaxEob(entropy::TX_64X64) == 1024);
    CHECK(mdc::av1GetMaxEob(entropy::TX_16X64) == 512);
}

// trip 24: av1_perform_inverse_transform_recon_luma (product_coding_loop.c
// :695-:752): the txOrg walk, the has_coeff/else-copy branches, and the
// recon = pred + inverse composition at 16x16 tx_depth 0.
TEST_CASE("l6 mdc av1PerformInverseTransformReconLuma composes the recon") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_16X16;
    ctx.geom.bwidth = 16;
    ctx.geom.bheight = 16;
    ctx.geom.sq_size = 16;
    ctx.blk_org_x = 32;
    ctx.blk_org_y = 32;
    ctx.sb_origin_x = 0;
    ctx.sb_origin_y = 0;
    pipeline::buildTxOrg();
    std::vector<std::uint8_t> cflTemp(64 * 64, 0);
    ctx.cfl_temp_luma_recon = cflTemp.data();

    const int STRIDE = 64;
    std::vector<std::uint8_t> predBuf(STRIDE * 64);
    std::vector<std::uint8_t> reconBuf(64 * STRIDE);
    for (int i = 0; i < STRIDE * 64; ++i) predBuf[i] = static_cast<std::uint8_t>(i * 3 + 5);

    mdc::MdCand cand{};
    cand.mode = l16_rdo::DC_PRED;
    cand.tx_depth = 0;
    mdc::MdCandBf bf{};
    bf.cand = &cand;
    bf.pred.buf = predBuf.data();
    bf.pred.stride = STRIDE;
    bf.recon.buf = reconBuf.data();
    bf.recon.stride = STRIDE;
    std::vector<std::int32_t> coeff(16 * 16, 0);
    bf.rec_coeff.buf = reinterpret_cast<std::uint8_t*>(coeff.data());
    bf.rec_coeff.stride = STRIDE;
    bf.y_has_coeff = 1;
    bf.eob_y[0] = 3;
    bf.transform_type[0] = static_cast<std::uint8_t>(entropy::DCT_DCT);
    coeff[0] = 800;
    coeff[1] = -300;

    mdc::av1PerformInverseTransformReconLuma(&ctx, &bf);

    // The landed l3 twin: the 16x16 inverse of the same dqcoeff + pred.
    std::vector<std::uint16_t> recon16(16 * 16);
    transforms::invTxfm2dAddHb(coeff.data(), recon16.data(), 16, transforms::TxSize::TX_16X16,
                               transforms::TxType::DCT_DCT, 8);
    // The txOrg origin for (16X16, intra, depth 0) is (0, 0) - the recon
    // lands in the CFL temp buffer at the block origin (32, 32) in the
    // 64-strided buffer; the PRED read is at the candidate buffer origin
    // (the per-candidate buffer models the block at (0,0)).
    for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 16; ++c) {
            const int want = std::clamp<int>(predBuf[r * STRIDE + c] + recon16[r * 16 + c], 0, 255);
            CHECK(cflTemp[(32 + r) * STRIDE + 32 + c] == want);
        }

    // The no-coeff branch: the pred copies into the CFL temp.
    bf.y_has_coeff = 0;
    for (int i = 0; i < 64 * STRIDE; ++i) cflTemp[i] = 0xEE;
    mdc::av1PerformInverseTransformReconLuma(&ctx, &bf);
    for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 16; ++c) {
            CHECK(cflTemp[(32 + r) * STRIDE + 32 + c] == predBuf[r * STRIDE + c]);
        }
}

// trip 25: av1_perform_inverse_transform_recon (product_coding_loop.c
// :754-:953): the luma + chroma walk at 32x32 (uv tx 16x16), all three
// has_coeff branches through the landed l3 twins.
TEST_CASE("l6 mdc av1PerformInverseTransformRecon composes luma and chroma recon") {
    mdc::FlCtx ctx{};
    ctx.geom.bsize = entropy::BLOCK_32X32;
    ctx.geom.bwidth = 32;
    ctx.geom.bheight = 32;
    ctx.geom.sq_size = 32;
    ctx.blk_org_x = 0;
    ctx.blk_org_y = 0;
    ctx.has_uv = 1;
    ctx.uv_mode = 1;  // CHROMA_MODE_1
    ctx.mds_subres_step = 0;
    pipeline::buildTxOrg();

    const int STRIDE = 64;
    const int UVSTRIDE = 32;
    std::vector<std::uint8_t> predBuf(STRIDE * 64);
    std::vector<std::uint8_t> predU(UVSTRIDE * 32);
    std::vector<std::uint8_t> predV(UVSTRIDE * 32);
    std::vector<std::uint8_t> reconY(STRIDE * 64);
    std::vector<std::uint8_t> reconU(UVSTRIDE * 32);
    std::vector<std::uint8_t> reconV(UVSTRIDE * 32);
    for (int i = 0; i < STRIDE * 64; ++i) predBuf[i] = static_cast<std::uint8_t>(i * 3 + 5);
    for (int i = 0; i < UVSTRIDE * 32; ++i) {
        predU[i] = static_cast<std::uint8_t>(i * 5 + 1);
        predV[i] = static_cast<std::uint8_t>(i * 7 + 2);
    }

    mdc::MdCand cand{};
    cand.mode = l16_rdo::DC_PRED;
    cand.tx_depth = 0;
    mdc::MdCandBf bf{};
    bf.cand = &cand;
    bf.pred.buf = predBuf.data();
    bf.pred.stride = STRIDE;
    bf.pred.u_buf = predU.data();
    bf.pred.v_buf = predV.data();
    bf.pred.u_stride = UVSTRIDE;
    bf.pred.v_stride = UVSTRIDE;
    bf.recon.buf = reconY.data();
    bf.recon.stride = STRIDE;
    bf.recon.u_buf = reconU.data();
    bf.recon.v_buf = reconV.data();
    bf.recon.u_stride = UVSTRIDE;
    bf.recon.v_stride = UVSTRIDE;
    std::vector<std::int32_t> coeffY(32 * 32, 0);
    std::vector<std::int32_t> coeffU(16 * 16, 0);
    std::vector<std::int32_t> coeffV(16 * 16, 0);
    bf.rec_coeff.buf = reinterpret_cast<std::uint8_t*>(coeffY.data());
    bf.rec_coeff.u_buf = reinterpret_cast<std::uint8_t*>(coeffU.data());
    bf.rec_coeff.v_buf = reinterpret_cast<std::uint8_t*>(coeffV.data());
    bf.rec_coeff.stride = STRIDE;
    bf.rec_coeff.u_stride = UVSTRIDE;
    bf.rec_coeff.v_stride = UVSTRIDE;
    bf.eob_y[0] = 5;
    bf.eob_u[0] = 3;
    bf.eob_v[0] = 2;
    bf.transform_type[0] = static_cast<std::uint8_t>(entropy::DCT_DCT);
    bf.transform_type_uv = static_cast<std::uint8_t>(entropy::DCT_DCT);
    coeffY[0] = 900;
    coeffY[3] = -200;
    coeffU[0] = 400;
    coeffV[0] = -300;

    ctx.blk_y_has_coeff = 1;
    ctx.blk_u_has_coeff = 1;
    ctx.blk_v_has_coeff = 1;

    mdc::MdGeom geom = ctx.geom;
    mdc::av1PerformInverseTransformRecon(&ctx, &bf, geom);

    // The oracle: the same TxfmParamHb the wrapper builds (the eob forced
    // to the tx 2d size when r != w, bd 8, isHbd 1), staged through the
    // landed l3 entry on fresh buffers - the core math is pinned by l3's
    // own tests; this pins the offsets, strides and param building.
    auto stageOracle = [](const std::int32_t* coeff, std::uint32_t coeffOff, const std::uint8_t* pred, uint32_t predOff,
                          uint32_t predStride, uint32_t reconStride, entropy::TxSize txSize, std::uint32_t eob) {
        std::vector<std::uint8_t> predCopy(predStride * 64, 0);
        for (int r = 0; r < 32; ++r)
            std::memcpy(predCopy.data() + predOff + r * predStride, pred + predOff + r * predStride, 32);
        std::vector<std::uint8_t> reconCopy(reconStride * 64, 0);
        transforms::TxfmParamHb p{};
        p.txType = transforms::TxType::DCT_DCT;
        p.txSize = static_cast<transforms::TxSize>(txSize);
        p.eob = static_cast<int>(eob);
        p.lossless = 0;
        p.bd = 8;
        p.isHbd = 1;
        transforms::invTxfmAddStagingHb(coeff + coeffOff, predCopy.data() + predOff, predStride,
                                        reconCopy.data() + predOff, reconStride, p);
        return reconCopy;
    };
    {
        const std::vector<std::uint8_t> wantY =
            stageOracle(coeffY.data(), 0, predBuf.data(), 0, STRIDE, STRIDE, entropy::TX_32X32, 1024);
        for (int r = 0; r < 32; ++r)
            for (int c = 0; c < 32; ++c) CHECK(reconY[r * STRIDE + c] == wantY[r * STRIDE + c]);
        const std::vector<std::uint8_t> wantU = stageOracle(coeffU.data(), 0, predU.data(), 0, UVSTRIDE, UVSTRIDE, entropy::TX_16X16, 256);
        for (int r = 0; r < 16; ++r)
            for (int c = 0; c < 16; ++c) CHECK(reconU[r * UVSTRIDE + c] == wantU[r * UVSTRIDE + c]);
        const std::vector<std::uint8_t> wantV = stageOracle(coeffV.data(), 0, predV.data(), 0, UVSTRIDE, UVSTRIDE, entropy::TX_16X16, 256);
        for (int r = 0; r < 16; ++r)
            for (int c = 0; c < 16; ++c) CHECK(reconV[r * UVSTRIDE + c] == wantV[r * UVSTRIDE + c]);
    }

    // The no-coff paths copy pred into the recon planes.
    ctx.blk_y_has_coeff = 0;
    ctx.blk_u_has_coeff = 0;
    ctx.blk_v_has_coeff = 0;
    for (int i = 0; i < STRIDE * 64; ++i) reconY[i] = 0xDD;
    for (int i = 0; i < UVSTRIDE * 32; ++i) {
        reconU[i] = 0xCC;
        reconV[i] = 0xBB;
    }
    mdc::av1PerformInverseTransformRecon(&ctx, &bf, geom);
    for (int r = 0; r < 32; ++r)
        for (int c = 0; c < 32; ++c) CHECK(reconY[r * STRIDE + c] == predBuf[r * STRIDE + c]);
    for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 16; ++c) {
            CHECK(reconU[r * UVSTRIDE + c] == predU[r * UVSTRIDE + c]);
            CHECK(reconV[r * UVSTRIDE + c] == predV[r * UVSTRIDE + c]);
        }

    // The split-mask case: luma has coeffs, chroma does not - the luma
    // inverse runs while both chroma planes copy.
    ctx.blk_y_has_coeff = 1;
    ctx.blk_u_has_coeff = 0;
    ctx.blk_v_has_coeff = 0;
    for (int i = 0; i < STRIDE * 64; ++i) reconY[i] = 0xDD;
    for (int i = 0; i < UVSTRIDE * 32; ++i) {
        reconU[i] = 0xCC;
        reconV[i] = 0xBB;
    }
    mdc::av1PerformInverseTransformRecon(&ctx, &bf, geom);
    {
        const std::vector<std::uint8_t> wantY2 =
            stageOracle(coeffY.data(), 0, predBuf.data(), 0, STRIDE, STRIDE, entropy::TX_32X32, 1024);
        for (int r = 0; r < 32; ++r)
            for (int c = 0; c < 32; ++c) CHECK(reconY[r * STRIDE + c] == wantY2[r * STRIDE + c]);
    }
    for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 16; ++c) {
            CHECK(reconU[r * UVSTRIDE + c] == predU[r * UVSTRIDE + c]);
            CHECK(reconV[r * UVSTRIDE + c] == predV[r * UVSTRIDE + c]);
        }
}

// trip 26: svt_aom_reset_mode_decision (md_process.c:767-:791) +
// svt_aom_mode_decision_configure_sb (:796-:812).
TEST_CASE("l6 mdc resetModeDecision walks the tile group and configureSb assigns the lambda") {
    mdc::FlCtx ctx{};
    // The NA carriers: two slots, each a tiny array.
    std::vector<std::uint8_t> storage(2 * 16 * 16);
    pipeline::NeighborArrayUnit nas[2][16];
    mdc::MdPcsNa carrier{};
    for (int s = 0; s < 2; ++s) {
        for (int k = 0; k < 16; ++k) {
            nas[s][k].left_array = storage.data() + (s * 16 + k) * 16;
            nas[s][k].top_array = nullptr;
            nas[s][k].top_left_array = nullptr;
            nas[s][k].left_array_size = 16;
            nas[s][k].top_array_size = 0;
            nas[s][k].top_left_array_size = 0;
            nas[s][k].unit_size = 1;
            nas[s][k].granularity_log2 = 0;
        }
    }
    carrier.mdleaf_partition_na[0] = &nas[0][0];
    carrier.mdleaf_partition_na[1] = &nas[1][0];
    carrier.md_luma_recon_na[0] = &nas[0][1];
    carrier.md_luma_recon_na[1] = &nas[1][1];
    carrier.md_tx_depth_1_luma_recon_na[0] = &nas[0][2];
    carrier.md_tx_depth_1_luma_recon_na[1] = &nas[1][2];
    carrier.md_tx_depth_2_luma_recon_na[0] = &nas[0][3];
    carrier.md_tx_depth_2_luma_recon_na[1] = &nas[1][3];
    carrier.md_cb_recon_na[0] = &nas[0][4];
    carrier.md_cb_recon_na[1] = &nas[1][4];
    carrier.md_cr_recon_na[0] = &nas[0][5];
    carrier.md_cr_recon_na[1] = &nas[1][5];
    carrier.md_luma_recon_na_16bit[0] = &nas[0][6];
    carrier.md_luma_recon_na_16bit[1] = &nas[1][6];
    carrier.md_tx_depth_1_luma_recon_na_16bit[0] = &nas[0][7];
    carrier.md_tx_depth_1_luma_recon_na_16bit[1] = &nas[1][7];
    carrier.md_tx_depth_2_luma_recon_na_16bit[0] = &nas[0][8];
    carrier.md_tx_depth_2_luma_recon_na_16bit[1] = &nas[1][8];
    carrier.md_cb_recon_na_16bit[0] = &nas[0][9];
    carrier.md_cb_recon_na_16bit[1] = &nas[1][9];
    carrier.md_cr_recon_na_16bit[0] = &nas[0][10];
    carrier.md_cr_recon_na_16bit[1] = &nas[1][10];
    carrier.md_y_dcs_na[0] = &nas[0][11];
    carrier.md_y_dcs_na[1] = &nas[1][11];
    carrier.md_tx_depth_1_luma_dc_sign_level_coeff_na[0] = &nas[0][12];
    carrier.md_tx_depth_1_luma_dc_sign_level_coeff_na[1] = &nas[1][12];
    carrier.md_cb_dc_sign_level_coeff_na[0] = &nas[0][13];
    carrier.md_cb_dc_sign_level_coeff_na[1] = &nas[1][13];
    carrier.md_cr_dc_sign_level_coeff_na[0] = &nas[0][14];
    carrier.md_cr_dc_sign_level_coeff_na[1] = &nas[1][14];
    carrier.md_txfm_context_array[0] = &nas[0][15];
    carrier.md_txfm_context_array[1] = &nas[1][15];

    // Preset every array with 0x11; the reset must fill the 8-bit sets
    // with 0xFF (hbd 0: the 8-bit set resets, the 16-bit set does not
    // without bypass_encdec).
    for (auto& b : storage) b = 0x11;
    mdc::MdTileGroupInfo tg{1, 2, 0, 2};  // rows [1,2), cols [0,2): 2 tiles
    mdc::resetModeDecision(&ctx, tg, 2, 0 /* hbd */, 0 /* bypass */, carrier, 0);
    // Slot 0 arrays: reset. Slot 1 (the second tile in the walk): reset.
    CHECK(nas[0][0].left_array[3] == 0xFF);
    CHECK(nas[1][1].left_array[5] == 0xFF);
    CHECK(nas[0][6].left_array[0] == 0x11);  // the 16-bit set untouched (hbd 0, no bypass)
    // segment_index != 0: no reset.
    for (auto& b : storage) b = 0x11;
    mdc::resetModeDecision(&ctx, tg, 2, 0, 0, carrier, 1);
    CHECK(nas[0][0].left_array[3] == 0x11);
    // The bypass_encdec write.
    CHECK(ctx.bypass_encdec == 0);
    mdc::resetModeDecision(&ctx, tg, 2, 0, 1, carrier, 1);
    CHECK(ctx.bypass_encdec == 1);
    // With bypass AND segment 0 the 16-bit set resets too.
    for (auto& b : storage) b = 0x11;
    mdc::resetModeDecision(&ctx, tg, 2, 0, 1, carrier, 0);
    CHECK(nas[0][6].left_array[0] == 0xFF);
    CHECK(nas[0][0].left_array[3] == 0xFF);

    // configureSb: delta_q_present -> sb_qp; else base_q_idx.
    mdc::FlCtx ctx2{};
    pipeline::LambdaAssignPcs pcs{};
    pcs.rtc = 1;
    pcs.delta_q_present = 1;
    pcs.r0_delta_qp_md = 0;
    pcs.base_q_idx = 100;
    ctx2.full_lambda_md[0] = 7;
    ctx2.fast_lambda_md[0] = 9;
    mdc::modeDecisionConfigureSb(&ctx2, 41, 43, pcs);
    CHECK(ctx2.qp_index == 41);
    CHECK(ctx2.me_q_index == 43);
    CHECK(ctx2.hbd_pack_done == 0);
    // The lambda chain ran (the rtc path folds to the base tables).
    CHECK(ctx2.full_lambda_md[0] != 7);
    // delta_q off -> the base q index.
    pcs.delta_q_present = 0;
    mdc::modeDecisionConfigureSb(&ctx2, 41, 43, pcs);
    CHECK(ctx2.qp_index == 100);
}
