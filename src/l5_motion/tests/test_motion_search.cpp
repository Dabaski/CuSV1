// A3M tests: the pure motion-search helpers of av1me.c / motion_estimation.c /
// mcomp.c. Every expectation is pinned to an integer and derives from the
// cited reference span (quoted in the comment above the CHECK); the oracles
// are test-side transcriptions of the reference arithmetic, independent of the
// port.
#include <doctest.h>
#include <motion.h>
#include <motion_search.h>

#include <cstdlib>
#include <vector>

#include <algorithm>

using motion::amvp::Mv;

namespace {

// definitions.h:457/:464 ROUND_POWER_OF_TWO / ROUND_POWER_OF_TWO_64, the
// test-side oracle's rounding pair.
std::int64_t oracleRoundPow2_64(std::int64_t value, int n) {
    return (value + (((std::int64_t)1 << n) >> 1)) >> n;
}
std::uint32_t oracleRoundPow2U(std::uint32_t value, int n) {
    return (value + (((std::uint32_t)1 << n) >> 1)) >> n;
}

// rd_cost.c:47-54 svt_av1_get_mv_joint, transcribed in the test as the
// independent oracle (the Mv*-form twin; the landed l16_rdo twin travels as
// the packed as_int form).
int oracleMvJoint(const Mv& mv) {
    if (mv.y == 0) {
        return mv.x == 0 ? 0 /*MV_JOINT_ZERO*/ : 1 /*MV_JOINT_HNZVZ*/;
    } else {
        return mv.x == 0 ? 2 /*MV_JOINT_HZVNZ*/ : 3 /*MV_JOINT_HNZVNZ*/;
    }
}

// mcomp.h:138-146 svt_mv_cost, transcribed in the test as the independent
// oracle: joint_cost[joint] + comp_cost[0][clip(y)] + comp_cost[1][clip(x)],
// the clip band [MV_LOW, MV_UPP] = [-16384, 16384].
int oracleMvCost(const Mv& mv, const int* jointCost, const int* const compCost[2]) {
    const int kMvLow = -16384;
    const int kMvUpp = 16384;
    const int cy = mv.y < kMvLow ? kMvLow : (mv.y > kMvUpp ? kMvUpp : mv.y);
    const int cx = mv.x < kMvLow ? kMvLow : (mv.x > kMvUpp ? kMvUpp : mv.x);
    return jointCost[oracleMvJoint(mv)] + compCost[0][cy] + compCost[1][cx];
}

// Synthetic cost tables: joint_cost[4] plus two component rows indexed
// [MV_LOW..MV_UPP] (i.e. &row[MV_LOW] at the call site, the mvcost
// convention). Values are deterministic and sign-asymmetric so a swapped
// component row or a broken clip shows as a diff.
struct CostTables {
    int joint[4];
    std::vector<int> comp0;
    std::vector<int> comp1;

    CostTables() : joint{7, 11, 13, 17}, comp0(2 * 16384 + 1), comp1(2 * 16384 + 1) {
        for (int i = 0; i < 2 * 16384 + 1; ++i) {
            const int v = i - 16384;
            // Non-negative rows (real mv-cost tables are probabilities);
            // sign-asymmetric so a swapped row or broken clip shows.
            comp0[i] = ((13 * v + 5) % 1009 + 1009) % 1009;
            comp1[i] = ((29 * v + 71) % 1013 + 1013) % 1013;
        }
    }
    const int* row0() const { return comp0.data() + 16384; }
    const int* row1() const { return comp1.data() + 16384; }
};

}  // namespace

TEST_CASE("getMvJoint matches the reference joint split over the MV range") {
    // rd_cost.c:47-54: y == 0 splits ZERO / HNZVZ on x; y != 0 splits
    // HZVNZ / HNZVNZ on x. Exhaustive over a representative band including
    // the extremes.
    const int band[] = {-16384, -16383, -8, -1, 0, 1, 8, 16383, 16384};
    for (int y : band) {
        for (int x : band) {
            Mv mv;
            mv.x = static_cast<std::int16_t>(x);
            mv.y = static_cast<std::int16_t>(y);
            CHECK(motion::getMvJoint(&mv) == oracleMvJoint(mv));
        }
    }
}

TEST_CASE("mvCost matches the reference table walk over the MV range") {
    // mcomp.h:138-146: joint + comp[0][clip(y)] + comp[1][clip(x)]. Sweep the
    // full in-range band and the out-of-band clip both ways.
    CostTables t;
    const int* const comp[2] = {t.row0(), t.row1()};
    const int band[] = {-20000, -16384, -16383, -1, 0, 1, 16383, 16384, 20000};
    for (int y : band) {
        for (int x : band) {
            Mv mv;
            mv.x = static_cast<std::int16_t>(x);
            mv.y = static_cast<std::int16_t>(y);
            INFO("mv = (", (int)mv.x, ", ", (int)mv.y, ")");
            CHECK(motion::mvCost(&mv, t.joint, comp) == oracleMvCost(mv, t.joint, comp));
        }
    }
    // The pinned value at one interior point (comp0(-1) = (-13+5) % 1009 = -8
    // C-modulo; the oracle transcribes C's % exactly, so the pin is the
    // oracle's value at (x, y) = (-1, 2): joint HZVNZ=2 -> 13.
    Mv probe;
    probe.x = -1;
    probe.y = 2;
    CHECK(motion::mvCost(&probe, t.joint, comp) == oracleMvCost(probe, t.joint, comp));
}

TEST_CASE("mvErrCostLight is 1296 + 50*(|dx|+|dy|) over the sweep") {
    // av1me.c:126-132: uint32 arithmetic, NO *8; the return is int.
    const int band[] = {-16384, -1023, -1, 0, 1, 1023, 16384};
    for (int my : band) {
        for (int mx : band) {
            for (int ry : band) {
                for (int rx : band) {
                    Mv mv, ref;
                    mv.x = (std::int16_t)mx; mv.y = (std::int16_t)my;
                    ref.x = (std::int16_t)rx; ref.y = (std::int16_t)ry;
                    const std::uint32_t dx = (std::uint32_t)std::abs((int)mv.x - (int)ref.x);
                    const std::uint32_t dy = (std::uint32_t)std::abs((int)mv.y - (int)ref.y);
                    const int expect = (int)(1296u + 50u * (dx + dy));
                    CHECK(motion::mvErrCostLight(&mv, &ref) == expect);
                }
            }
        }
    }
    // One pinned interior point: mv=(7,-3), ref=(2,4): |5|+|7| = 12 -> 1296+600.
    Mv mv, ref;
    mv.x = 7; mv.y = -3;
    ref.x = 2; ref.y = 4;
    CHECK(motion::mvErrCostLight(&mv, &ref) == 1896);
}

TEST_CASE("mvsadErrCostLight is the *8 light form over the sweep") {
    // av1me.c:134-140: the SAME 1296+50*(...) shape with the differences
    // scaled by 8 first.
    const int band[] = {-1023, -1, 0, 1, 1023};
    for (int my : band) {
        for (int mx : band) {
            for (int ry : band) {
                for (int rx : band) {
                    Mv mv, ref;
                    mv.x = (std::int16_t)mx; mv.y = (std::int16_t)my;
                    ref.x = (std::int16_t)rx; ref.y = (std::int16_t)ry;
                    const std::uint32_t dx = (std::uint32_t)(std::abs((int)mv.x - (int)ref.x) * 8);
                    const std::uint32_t dy = (std::uint32_t)(std::abs((int)mv.y - (int)ref.y) * 8);
                    const int expect = (int)(1296u + 50u * (dx + dy));
                    CHECK(motion::mvsadErrCostLight(&mv, &ref) == expect);
                }
            }
        }
    }
}

TEST_CASE("mvErrCost matches the reference ROUND_POWER_OF_TWO_64 walk") {
    // av1me.c:142-150: diff components travel UN-scaled; the product
    // (int64)cost * error_per_bit rounds by
    // RDDIV_BITS(7) + AV1_PROB_COST_SHIFT(9) - RD_EPB_SHIFT(6)
    //   + PIXEL_TRANSFORM_ERROR_SCALE(4) = 14; a null mvcost returns 0.
    CostTables t;
    const int* const comp[2] = {t.row0(), t.row1()};
    const int eps[] = {0, 1, 7, 89, 12345};
    const int band[] = {-1023, -8, -1, 0, 1, 8, 1023};
    for (int ey : band) {
        for (int ex : band) {
            for (int r : band) {
                Mv mv, ref;
                mv.x = (std::int16_t)ex; mv.y = (std::int16_t)ey;
                ref.x = (std::int16_t)r; ref.y = (std::int16_t)(-r);
                for (int epb : eps) {
                    Mv diff;
                    diff.x = (std::int16_t)(mv.x - ref.x);
                    diff.y = (std::int16_t)(mv.y - ref.y);
                    const int expect = (int)oracleRoundPow2_64(
                        (std::int64_t)oracleMvCost(diff, t.joint, comp) * epb, 14);
                    CHECK(motion::mvErrCost(&mv, &ref, t.joint, comp, epb) == expect);
                }
            }
        }
    }
    // The null-mvcost branch returns 0.
    CHECK(motion::mvErrCost(nullptr, nullptr, t.joint, nullptr, 17) == 0);
    Mv mv, ref;
    mv.x = 5; mv.y = 6; ref.x = 1; ref.y = 2;
    CHECK(motion::mvErrCost(&mv, &ref, t.joint, nullptr, 17) == 0);
}

TEST_CASE("mvsadErrCost switches on approx_inter_rate over both forms") {
    // av1me.c:152-160: the light branch returns mvsad_err_cost_light; the
    // full branch scales the diff by 8 and rounds the
    // (unsigned)cost * sad_per_bit product by AV1_PROB_COST_SHIFT = 9.
    CostTables t;
    const int* const comp[2] = {t.row0(), t.row1()};
    motion::SearchCtx x{};
    x.nmvVecCost = t.joint;
    x.mvCostStack = comp;
    const int band[] = {-1023, -7, 0, 7, 1023};
    for (int dy : band) {
        for (int dx : band) {
            Mv mv, ref;
            mv.x = (std::int16_t)dx; mv.y = (std::int16_t)dy;
            ref.x = 0; ref.y = 0;
            // Light branch (the sad_per_bit arg is dead on this path).
            x.approxInterRate = 1;
            const std::uint32_t lx = (std::uint32_t)(std::abs(dx) * 8);
            const std::uint32_t ly = (std::uint32_t)(std::abs(dy) * 8);
            const int lightExpect = (int)(1296u + 50u * (lx + ly));
            CHECK(motion::mvsadErrCost(x, &mv, &ref, 113) == lightExpect);
            // Full branch.
            x.approxInterRate = 0;
            Mv diff;
            diff.x = (std::int16_t)(dx * 8);
            diff.y = (std::int16_t)(dy * 8);
            const std::uint32_t prod =
                (std::uint32_t)oracleMvCost(diff, t.joint, comp) * 113u;
            const int fullExpect = (int)oracleRoundPow2U(prod, 9);
            CHECK(motion::mvsadErrCost(x, &mv, &ref, 113) == fullExpect);
        }
    }
}

TEST_CASE("mvErrCostMcomp walks the MV_COST_TYPE switch exactly") {
    // mcomp.c:42-72: ENTROPY rounds (int64)cost * epb by 14; the L1 forms are
    // (lambda * (|dy| + |dx|)) >> 3 with lambda 2 / 0 / 1; OPT rounds
    // ((|dy| + |dx|) << 8) * epb by 14; NONE is 0.
    CostTables t;
    const int* const comp[2] = {t.row0(), t.row1()};
    const int band[] = {-1023, -1, 0, 1, 1023};
    for (int my : band) {
        for (int mx : band) {
            Mv mv, ref;
            mv.x = (std::int16_t)mx; mv.y = (std::int16_t)my;
            ref.x = 3; ref.y = -5;
            Mv diff;
            diff.x = (std::int16_t)(mv.x - ref.x);
            diff.y = (std::int16_t)(mv.y - ref.y);
            const int ax = std::abs((int)diff.x);
            const int ay = std::abs((int)diff.y);
            const int entExpect = (int)oracleRoundPow2_64(
                (std::int64_t)oracleMvCost(diff, t.joint, comp) * 89, 14);
            CHECK(motion::mvErrCostMcomp(&mv, &ref, t.joint, comp, 89, motion::MV_COST_ENTROPY) == entExpect);
            CHECK(motion::mvErrCostMcomp(&mv, &ref, t.joint, comp, 89, motion::MV_COST_L1_LOWRES) == ((2 * (ay + ax)) >> 3));
            CHECK(motion::mvErrCostMcomp(&mv, &ref, t.joint, comp, 89, motion::MV_COST_L1_MIDRES) == 0);
            CHECK(motion::mvErrCostMcomp(&mv, &ref, t.joint, comp, 89, motion::MV_COST_L1_HDRES) == ((1 * (ay + ax)) >> 3));
            CHECK(motion::mvErrCostMcomp(&mv, &ref, t.joint, comp, 89, motion::MV_COST_OPT) ==
                  (int)oracleRoundPow2_64((std::int64_t)((ay + ax) << 8) * 89, 14));
            CHECK(motion::mvErrCostMcomp(&mv, &ref, t.joint, comp, 89, motion::MV_COST_NONE) == 0);
        }
    }
    // ENTROPY with a null mvcost returns 0 (the branch's own guard).
    Mv mv, ref;
    mv.x = 4; mv.y = 9; ref.x = 1; ref.y = 1;
    CHECK(motion::mvErrCostMcomp(&mv, &ref, t.joint, nullptr, 89, motion::MV_COST_ENTROPY) == 0);
}

TEST_CASE("mvErrCostFromParams and fpMvErrCost thread the param carrier") {
    // mcomp.c:74-97 + :775-777: both forms dispatch to the switch through the
    // svt_mv_cost_param carrier; the carrier's consumed fields are refMv /
    // mvjcost / mvcost / errorPerBit / mvCostType.
    CostTables t;
    const int* const comp[2] = {t.row0(), t.row1()};
    Mv refMv;
    refMv.x = -3; refMv.y = 11;
    motion::MvCostParam p{};
    p.refMv = &refMv;
    p.mvjcost = t.joint;
    p.mvcost[0] = t.row0();
    p.mvcost[1] = t.row1();
    p.errorPerBit = 61;
    p.mvCostType = motion::MV_COST_ENTROPY;
    const int band[] = {-1023, -1, 0, 1, 1023};
    for (int my : band) {
        for (int mx : band) {
            Mv mv;
            mv.x = (std::int16_t)mx; mv.y = (std::int16_t)my;
            Mv diff;
            diff.x = (std::int16_t)(mv.x - refMv.x);
            diff.y = (std::int16_t)(mv.y - refMv.y);
            const int expect = (int)oracleRoundPow2_64(
                (std::int64_t)oracleMvCost(diff, t.joint, comp) * 61, 14);
            CHECK(motion::mvErrCostFromParams(&mv, &p) == expect);
            CHECK(motion::fpMvErrCost(&mv, &p) == expect);
        }
    }
    // The L1_HDRES form through the carrier: (1 * (|dy|+|dx|)) >> 3.
    p.mvCostType = motion::MV_COST_L1_HDRES;
    Mv mv;
    mv.x = 20; mv.y = -7;
    CHECK(motion::mvErrCostFromParams(&mv, &p) == (((std::abs(20 + 3) + std::abs(-7 - 11)) >> 3)));
}

TEST_CASE("getBestDiagStep picks the cheaper diagonal signs") {
    // mcomp.c:248-254: x = left <= right ? -step : +step; y = up <= down ?
    // -step : +step. Sweep the tie and the four strict cases.
    for (int step : {1, 2, 4, 8}) {
        Mv d = motion::getBestDiagStep(step, 5, 9, 3, 4);
        CHECK(d.x == -step);
        CHECK(d.y == -step);
        d = motion::getBestDiagStep(step, 9, 5, 4, 3);
        CHECK(d.x == step);
        CHECK(d.y == step);
        d = motion::getBestDiagStep(step, 7, 7, 7, 7);  // ties go negative
        CHECK(d.x == -step);
        CHECK(d.y == -step);
        d = motion::getBestDiagStep(step, 9, 5, 7, 7);
        CHECK(d.x == step);
        CHECK(d.y == -step);
    }
}

// The svf stand-in: records the exact arguments it received and returns a
// deterministic function of them, so estimatedPrefError's threading is
// pinned against independently computed expectations.
struct SvfRecorder {
    const std::uint8_t* gotRef = nullptr;
    int gotRefStride = 0;
    int gotXoff = -1;
    int gotYoff = -1;
    const std::uint8_t* gotSrc = nullptr;
    int gotSrcStride = 0;
    std::uint32_t* gotSse = nullptr;
    std::uint32_t retVal = 0;

    static std::uint32_t thunk(const std::uint8_t* a, int aStride, int xoff, int yoff, const std::uint8_t* b,
                               int bStride, std::uint32_t* sse) {
        auto* self = instance;
        self->gotRef = a;
        self->gotRefStride = aStride;
        self->gotXoff = xoff;
        self->gotYoff = yoff;
        self->gotSrc = b;
        self->gotSrcStride = bStride;
        self->gotSse = sse;
        return self->retVal;
    }

    static SvfRecorder* instance;
};
SvfRecorder* SvfRecorder::instance = nullptr;

TEST_CASE("estimatedPrefError threads the subpel offset arithmetic exactly") {
    // mcomp.c:156-174: ref = get_buf_from_mv(ref, *this_mv) with the
    // (mv.y >> 3) * stride + (mv.x >> 3) offset (mcomp.c:106-110); the
    // subpel parts are mv & 7 (mcomp.c:99-104); svf is called with
    // (ref, ref_stride, subpel_x_q3, subpel_y_q3, src, src_stride, sse).
    SvfRecorder rec;
    SvfRecorder::instance = &rec;
    rec.retVal = 4242;

    std::vector<std::uint8_t> src(64 * 70, 9);
    std::vector<std::uint8_t> ref(64 * 70, 3);
    motion::SubpelVarParams vp{};
    vp.svf = &SvfRecorder::thunk;
    vp.srcBuf = src.data() + 3 * 64 + 5;
    vp.srcStride = 64;
    vp.refBuf = ref.data() + 11 * 64 + 13;
    vp.refStride = 64;

    std::uint32_t sse = 0;
    const int band[] = {-33, -9, -1, 0, 1, 8, 33};
    for (int my : band) {
        for (int mx : band) {
            Mv mv;
            mv.x = (std::int16_t)mx; mv.y = (std::int16_t)my;
            const int expectX = mx & 7;
            const int expectY = my & 7;
            const std::uint8_t* expectRef =
                ref.data() + (11 + (my >> 3)) * 64 + (13 + (mx >> 3));
            const int got = motion::estimatedPrefError(&mv, vp, &sse);
            CHECK(rec.gotRef == expectRef);
            CHECK(rec.gotRefStride == 64);
            CHECK(rec.gotXoff == expectX);
            CHECK(rec.gotYoff == expectY);
            CHECK(rec.gotSrc == src.data() + 3 * 64 + 5);
            CHECK(rec.gotSrcStride == 64);
            CHECK(rec.gotSse == &sse);
            CHECK(got == 4242);
        }
    }
    // The exported helpers pin the same arithmetic on their own.
    CHECK(motion::subpelPart(123) == 3);
    CHECK(motion::subpelPart(-1) == 7);  // the reference's & 7 on a negative int
    Mv zeroMv;
    zeroMv.as_int = 0;
    CHECK(motion::subpelBufFromMv(ref.data(), 64, zeroMv) == ref.data());
    Mv offMv;
    offMv.x = 8;
    offMv.y = -8;
    // (mv.y >> 3) * stride + (mv.x >> 3) = -1 * 64 + 1.
    CHECK(motion::subpelBufFromMv(ref.data() + 70 * 64, 64, offMv) == ref.data() + 69 * 64 + 1);
    SvfRecorder::instance = nullptr;
}

TEST_CASE("setMvSearchRange clamps to the [-1023,1023] window and intersects") {
    // av1me.c:97-122: col/row_min = (mv >> 3) - 1023 + !!(mv & 7);
    // col/row_max = (mv >> 3) + 1023; then MAX with (MV_LOW>>3)+1 = -2047,
    // MIN with (MV_UPP>>3)-1 = 2047, then the intersection write.
    for (int sub : {0, 1, 3, 7}) {  // the !!(mv & 7) term
        for (int full : {-2048, -2047, -1024, -1023, 0, 1, 1022, 1023, 2047, 2048}) {
            Mv mv;
            mv.x = (std::int16_t)(full * 8 + sub);
            mv.y = (std::int16_t)(full * 8 + (sub ? 8 - sub : 0));
            // Independent transcription:
            const int cmin = std::max((mv.x >> 3) - 1023 + (mv.x & 7 ? 1 : 0), -2047);
            const int rmin = std::max((mv.y >> 3) - 1023 + (mv.y & 7 ? 1 : 0), -2047);
            const int cmax = std::min((mv.x >> 3) + 1023, 2047);
            const int rmax = std::min((mv.y >> 3) + 1023, 2047);
            motion::MvLimits lim{-30000, 30000, -30000, 30000};
            motion::setMvSearchRange(&lim, &mv);
            CHECK(lim.col_min == cmin);
            CHECK(lim.row_min == rmin);
            CHECK(lim.col_max == cmax);
            CHECK(lim.row_max == rmax);
            // The intersection write: a narrower caller window stays.
            motion::MvLimits narrow{-100, 100, -50, 60};
            motion::setMvSearchRange(&narrow, &mv);
            CHECK(narrow.col_min == std::max(-100, cmin));
            CHECK(narrow.col_max == std::min(100, cmax));
            CHECK(narrow.row_min == std::max(-50, rmin));
            CHECK(narrow.row_max == std::min(60, rmax));
        }
    }
    // The full-pel pin: mv = (8192+4) subpel -> full 1024+round-up toward the
    // window: col_min = 1024 - 1023 + 1 = 2, col_max = 1024 + 1023 clamped
    // to 2047.
    Mv mv;
    mv.x = 8196;
    mv.y = 0;
    motion::MvLimits lim{-30000, 30000, -30000, 30000};
    motion::setMvSearchRange(&lim, &mv);
    CHECK(lim.col_min == 2);
    CHECK(lim.col_max == 2047);
    CHECK(lim.row_min == -1023);
    CHECK(lim.row_max == 1023);
}

TEST_CASE("isMvIn is the inclusive four-bound predicate") {
    // av1me.c:190-193: x in [col_min, col_max] AND y in [row_min, row_max].
    motion::MvLimits lim{-10, 10, -20, 20};
    Mv mv;
    mv.x = -10; mv.y = 20;
    CHECK(motion::isMvIn(&lim, &mv) == 1);
    mv.x = 10; mv.y = -20;
    CHECK(motion::isMvIn(&lim, &mv) == 1);
    mv.x = 11;
    CHECK(motion::isMvIn(&lim, &mv) == 0);
    mv.x = -11;
    CHECK(motion::isMvIn(&lim, &mv) == 0);
    mv.x = 0; mv.y = 21;
    CHECK(motion::isMvIn(&lim, &mv) == 0);
    mv.y = -21;
    CHECK(motion::isMvIn(&lim, &mv) == 0);
    mv.y = 0;
    CHECK(motion::isMvIn(&lim, &mv) == 1);
}

TEST_CASE("setSubpelMvSearchRange clamps to the subpel window") {
    // av1me.c:694-707: max_mv = 1023*8 = 8184; min = MAX(limit*8, ref-max_mv),
    // max = MIN(limit*8, ref+max_mv); then MAX(MV_LOW+1, min) /
    // MIN(MV_UPP-1, max) per component.
    for (int refv : {-16384, -8184, -100, 0, 100, 8184, 16384}) {
        Mv refMv;
        refMv.x = (std::int16_t)refv;
        refMv.y = (std::int16_t)(-refv);
        const int maxMv = 1023 * 8;
        const int minc = std::max(-2048 * 8, refMv.x - maxMv);
        const int maxc = std::min(2047 * 8, refMv.x + maxMv);
        const int minr = std::max(-2048 * 8, refMv.y - maxMv);
        const int maxr = std::min(2047 * 8, refMv.y + maxMv);
        int cmin, cmax, rmin, rmax;
        motion::MvLimits lim{-2048, 2047, -2048, 2047};
        motion::setSubpelMvSearchRange(&lim, &cmin, &cmax, &rmin, &rmax, &refMv);
        CHECK(cmin == std::max(-16384 + 1, minc));
        CHECK(cmax == std::min(16384 - 1, maxc));
        CHECK(rmin == std::max(-16384 + 1, minr));
        CHECK(rmax == std::min(16384 - 1, maxr));
    }
    // The window pins: full-pel limits map *8; the MV_LOW+1 / MV_UPP-1
    // sentinels bind at the extremes.
    Mv refMv;
    refMv.as_int = 0;
    int cmin, cmax, rmin, rmax;
    motion::MvLimits wide{-30000, 30000, -30000, 30000};
    motion::setSubpelMvSearchRange(&wide, &cmin, &cmax, &rmin, &rmax, &refMv);
    CHECK(cmin == -8184);
    CHECK(cmax == 8184);
    motion::MvLimits tight{0, 0, 0, 0};
    motion::setSubpelMvSearchRange(&tight, &cmin, &cmax, &rmin, &rmax, &refMv);
    CHECK(cmin == 0);
    CHECK(cmax == 0);
    CHECK(rmin == 0);
    CHECK(rmax == 0);
}

// The variance stand-in: returns a fixed 999 and records the exact arguments,
// so getMvpredVar's threading is pinned against independently computed
// expectations (the cost halves come from the landed oracles above).
namespace {
struct VfRec {
    const std::uint8_t* gotRef = nullptr;
    int gotSrcStride = -1;
    int gotRefStride = -1;
    static std::uint32_t thunk(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride,
                               std::uint32_t*) {
        auto* self = instance;
        self->gotRef = b;
        self->gotSrcStride = aStride;
        self->gotRefStride = bStride;
        (void)a;
        return 999;
    }
    static VfRec* instance;
};
VfRec* VfRec::instance = nullptr;
}  // namespace

TEST_CASE("getMvpredVar adds the threaded variance and the chosen cost form") {
    // av1me.c:195-208: mv = best_mv * 8 (subpel); vf over (src, ref+best_mv)
    // plus the light or full mv_err_cost when use_mvcost, else the bare vf.
    VfRec rec;
    VfRec::instance = &rec;
    std::vector<std::uint8_t> src(64 * 70);
    std::vector<std::uint8_t> ref(64 * 70);
    for (int r = 0; r < 64 * 70; ++r) {
        src[r] = (std::uint8_t)(r * 7 % 251);
        ref[r] = (std::uint8_t)(r * 13 % 251);
    }

    CostTables t;
    const int* const comp[2] = {t.row0(), t.row1()};
    motion::SearchCtx x{};
    x.srcBuf = src.data();
    x.srcStride = 64;
    x.refBuf = ref.data() + 100 * 64 + 30;  // the in_what base
    x.refStride = 64;
    x.nmvVecCost = t.joint;
    x.mvCostStack = comp;
    x.errorperbit = 37;
    x.approxInterRate = 0;

    // best_mv = (2, 3) full-pel -> the vf sees ref + (3*64 + 2), and the
    // cost form sees mv = (16, 24) vs center.
    Mv bestMv;
    bestMv.x = 2;
    bestMv.y = 3;
    Mv center;
    center.x = 1;
    center.y = 5;

    const std::uint8_t* expectRef = ref.data() + 100 * 64 + 30 + 3 * 64 + 2;
    Mv subMv;
    subMv.x = 16;
    subMv.y = 24;

    // Full path, use_mvcost = 1.
    const int expectFull =
        999 + (int)oracleRoundPow2_64((std::int64_t)oracleMvCost(subMv, t.joint, comp) * 37, 14);
    CHECK(motion::getMvpredVar(x, &bestMv, &center, &VfRec::thunk, 1) == expectFull);
    // Light path, use_mvcost = 1.
    x.approxInterRate = 1;
    const int lightExpect =
        999 + (int)(1296u + 50u * ((unsigned)std::abs(16 - 1) + (unsigned)std::abs(24 - 5)));
    CHECK(motion::getMvpredVar(x, &bestMv, &center, &VfRec::thunk, 1) == lightExpect);
    // use_mvcost = 0: the bare vf on both paths.
    CHECK(motion::getMvpredVar(x, &bestMv, &center, &VfRec::thunk, 0) == 999);
    x.approxInterRate = 0;
    CHECK(motion::getMvpredVar(x, &bestMv, &center, &VfRec::thunk, 0) == 999);
    // The vf saw exactly ref + best_mv (full-pel) with the threaded strides.
    CHECK(rec.gotRef == expectRef);
    CHECK(rec.gotSrcStride == 64);
    CHECK(rec.gotRefStride == 64);
    VfRec::instance = nullptr;
}

namespace {

// The test-side transcription oracle for the three searches. The scalar SAD
// and the search walk below are transcribed from av1me.c:212-289 /
// :291-:422 / :424-:487 independently of the port; the SAD oracle is a plain
// sum of absolute differences over 8x8.
struct OracleCtx {
    const std::uint8_t* src;
    int srcStride;
    const std::uint8_t* ref;  // the in_what base
    int refStride;
    motion::MvLimits lim;
    bool light;               // the mvsad_err_cost approx_inter_rate branch
    const int* joint;
    const int* const* comp;
};

std::uint32_t oracleSad8x8(const std::uint8_t* s, int ss, const std::uint8_t* r, int rs) {
    std::uint32_t sad = 0;
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            sad += (std::uint32_t)std::abs(s[row * ss + col] - r[row * rs + col]);
        }
    }
    return sad;
}

void oracleSad8x8x4(const std::uint8_t* s, int ss, const std::uint8_t* const r[4], int rs, std::uint32_t out[4]) {
    for (int i = 0; i < 4; ++i) {
        out[i] = oracleSad8x8(s, ss, r[i], rs);
    }
}

// av1me.c:152-160 mvsad_err_cost, transcribed (light flag threaded).
int oracleMvsadCost(const OracleCtx& o, const Mv& mv, const Mv& refMv, int sadPerBit) {
    if (o.light) {
        const std::uint32_t dx = (std::uint32_t)(std::abs((int)mv.x - (int)refMv.x) * 8);
        const std::uint32_t dy = (std::uint32_t)(std::abs((int)mv.y - (int)refMv.y) * 8);
        return (int)(1296u + 50u * (dx + dy));
    }
    Mv diff;
    diff.x = (std::int16_t)((mv.x - refMv.x) * 8);
    diff.y = (std::int16_t)((mv.y - refMv.y) * 8);
    return (int)oracleRoundPow2U((std::uint32_t)oracleMvCost(diff, o.joint, o.comp) * (std::uint32_t)sadPerBit, 9);
}

// av1me.c:291-422 svt_av1_diamond_search_sad_c, transcribed (NEW_DIAMOND_SEARCH
// is NOT defined in the pinned tree - grep-verified - so the reference's own
// compiled form has no inner refinement loop).
std::uint32_t oracleDiamond(const OracleCtx& o, const motion::SearchSiteConfig& cfg, const Mv& centerMv, Mv* refMv,
                            Mv* bestMv, Mv* secondBestMv, int searchParam, int sadPerBit, int* num00) {
    const motion::SearchSite* ss = &cfg.ss[searchParam * cfg.searchesPerStep];
    const int totSteps = (cfg.ssCount / cfg.searchesPerStep) - searchParam;

    Mv fcenter;
    fcenter.x = (std::int16_t)(centerMv.x >> 3);
    fcenter.y = (std::int16_t)(centerMv.y >> 3);
    refMv->x = (std::int16_t)std::min(std::max((int)refMv->x, o.lim.col_min), o.lim.col_max);
    refMv->y = (std::int16_t)std::min(std::max((int)refMv->y, o.lim.row_min), o.lim.row_max);
    const int refRow = refMv->y;
    const int refCol = refMv->x;
    *num00 = 0;
    bestMv->y = (std::int16_t)refRow;
    bestMv->x = (std::int16_t)refCol;

    const std::uint8_t* inWhat = o.ref + refRow * o.refStride + refCol;
    const std::uint8_t* bestAddress = inWhat;

    std::uint32_t bestsad =
        oracleSad8x8(o.src, o.srcStride, inWhat, o.refStride) +
        (std::uint32_t)oracleMvsadCost(o, *bestMv, fcenter, sadPerBit);

    int i = 1;
    int bestSite = 0;
    int lastSite = 0;

    for (int step = 0; step < totSteps; step++) {
        int allIn = 1;
        allIn &= ((bestMv->y + ss[i].mv.y) > o.lim.row_min);
        allIn &= ((bestMv->y + ss[i + 1].mv.y) < o.lim.row_max);
        allIn &= ((bestMv->x + ss[i + 2].mv.x) > o.lim.col_min);
        allIn &= ((bestMv->x + ss[i + 3].mv.x) < o.lim.col_max);

        if (allIn) {
            std::uint32_t sadArray[4];
            for (int j = 0; j < cfg.searchesPerStep; j += 4) {
                const std::uint8_t* blockOffset[4];
                for (int t = 0; t < 4; t++) {
                    blockOffset[t] = ss[i + t].offset + bestAddress;
                }
                oracleSad8x8x4(o.src, o.srcStride, blockOffset, o.refStride, sadArray);
                for (int t = 0; t < 4; t++, i++) {
                    if (sadArray[t] < bestsad) {
                        Mv thisMv;
                        thisMv.x = (std::int16_t)(bestMv->x + ss[i].mv.x);
                        thisMv.y = (std::int16_t)(bestMv->y + ss[i].mv.y);
                        sadArray[t] += (std::uint32_t)oracleMvsadCost(o, thisMv, fcenter, sadPerBit);
                        if (sadArray[t] < bestsad) {
                            bestsad = sadArray[t];
                            bestSite = i;
                        }
                    }
                }
            }
        } else {
            for (int j = 0; j < cfg.searchesPerStep; j++) {
                Mv thisMv;
                thisMv.x = (std::int16_t)(bestMv->x + ss[i].mv.x);
                thisMv.y = (std::int16_t)(bestMv->y + ss[i].mv.y);
                const bool inRange = thisMv.x >= o.lim.col_min && thisMv.x <= o.lim.col_max &&
                    thisMv.y >= o.lim.row_min && thisMv.y <= o.lim.row_max;
                if (inRange) {
                    const std::uint8_t* checkHere = ss[i].offset + bestAddress;
                    std::uint32_t thissad = oracleSad8x8(o.src, o.srcStride, checkHere, o.refStride);
                    if (thissad < bestsad) {
                        thissad += (std::uint32_t)oracleMvsadCost(o, thisMv, fcenter, sadPerBit);
                        if (thissad < bestsad) {
                            bestsad = thissad;
                            bestSite = i;
                        }
                    }
                }
                i++;
            }
        }
        if (bestSite != lastSite) {
            *secondBestMv = *bestMv;
            bestMv->y = (std::int16_t)(bestMv->y + ss[bestSite].mv.y);
            bestMv->x = (std::int16_t)(bestMv->x + ss[bestSite].mv.x);
            bestAddress += ss[bestSite].offset;
            lastSite = bestSite;
        } else if (bestAddress == inWhat) {
            (*num00)++;
        }
    }
    return bestsad;
}

// av1me.c:212-289 exhaustive_mesh_search, transcribed.
std::uint32_t oracleExhaustive(const OracleCtx& o, const Mv& centerMv, const Mv& refMv, int range, int step,
                               int sadPerBit, Mv* bestMv, Mv* secondBestMv) {
    Mv fcenter = centerMv;
    fcenter.x = (std::int16_t)std::min(std::max((int)fcenter.x, o.lim.col_min), o.lim.col_max);
    fcenter.y = (std::int16_t)std::min(std::max((int)fcenter.y, o.lim.row_min), o.lim.row_max);
    *bestMv = fcenter;
    std::uint32_t bestSad =
        oracleSad8x8(o.src, o.srcStride, o.ref + fcenter.y * o.refStride + fcenter.x, o.refStride) +
        (std::uint32_t)oracleMvsadCost(o, fcenter, refMv, sadPerBit);
    const int colStep = (step > 1) ? step : 4;
    const int startRow = std::max(-range, o.lim.row_min - fcenter.y);
    const int startCol = std::max(-range, o.lim.col_min - fcenter.x);
    const int endRow = std::min(range, o.lim.row_max - fcenter.y);
    const int endCol = std::min(range, o.lim.col_max - fcenter.x);
    for (int r = startRow; r <= endRow; r += step) {
        for (int c = startCol; c <= endCol; c += colStep) {
            if (step > 1) {
                Mv mv;
                mv.x = (std::int16_t)(fcenter.x + c);
                mv.y = (std::int16_t)(fcenter.y + r);
                std::uint32_t sad =
                    oracleSad8x8(o.src, o.srcStride, o.ref + mv.y * o.refStride + mv.x, o.refStride);
                if (sad < bestSad) {
                    sad += (std::uint32_t)oracleMvsadCost(o, mv, refMv, sadPerBit);
                    if (sad < bestSad) {
                        bestSad = sad;
                        *secondBestMv = *bestMv;
                        *bestMv = mv;
                    }
                }
            } else {
                if (c + 3 <= endCol) {
                    std::uint32_t sads[4];
                    const std::uint8_t* addrs[4];
                    for (int i = 0; i < 4; ++i) {
                        addrs[i] = o.ref + (fcenter.y + r) * o.refStride + (fcenter.x + c + i);
                    }
                    oracleSad8x8x4(o.src, o.srcStride, addrs, o.refStride, sads);
                    for (int i = 0; i < 4; ++i) {
                        if (sads[i] < bestSad) {
                            Mv mv;
                            mv.x = (std::int16_t)(fcenter.x + c + i);
                            mv.y = (std::int16_t)(fcenter.y + r);
                            const std::uint32_t sad = sads[i] + (std::uint32_t)oracleMvsadCost(o, mv, refMv, sadPerBit);
                            if (sad < bestSad) {
                                bestSad = sad;
                                *secondBestMv = *bestMv;
                                *bestMv = mv;
                            }
                        }
                    }
                } else {
                    for (int i = 0; i < endCol - c; ++i) {
                        Mv mv;
                        mv.x = (std::int16_t)(fcenter.x + c + i);
                        mv.y = (std::int16_t)(fcenter.y + r);
                        std::uint32_t sad =
                            oracleSad8x8(o.src, o.srcStride, o.ref + mv.y * o.refStride + mv.x, o.refStride);
                        if (sad < bestSad) {
                            sad += (std::uint32_t)oracleMvsadCost(o, mv, refMv, sadPerBit);
                            if (sad < bestSad) {
                                bestSad = sad;
                                *secondBestMv = *bestMv;
                                *bestMv = mv;
                            }
                        }
                    }
                }
            }
        }
    }
    return bestSad;
}

// av1me.c:424-487 svt_av1_refining_search_sad, transcribed (the neighbors
// order {0,-1},{-1,0},{1,0},{0,1}; the start cost and every per-neighbor
// cost reference FCENTER; best_site == -1 breaks).
std::uint32_t oracleRefining(const OracleCtx& o, const Mv& centerMv, Mv* refMv, int errorPerBit, int searchRange,
                             Mv* secondBestMv) {
    static const int neighbors[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
    Mv fcenter;
    fcenter.x = (std::int16_t)(centerMv.x >> 3);
    fcenter.y = (std::int16_t)(centerMv.y >> 3);
    const std::uint8_t* bestAddress = o.ref + refMv->y * o.refStride + refMv->x;
    std::uint32_t bestSad = oracleSad8x8(o.src, o.srcStride, bestAddress, o.refStride) +
        (std::uint32_t)oracleMvsadCost(o, *refMv, fcenter, errorPerBit);
    for (int i = 0; i < searchRange; i++) {
        int bestSite = -1;
        const bool allIn = (refMv->y - 1) > o.lim.row_min && (refMv->y + 1) < o.lim.row_max &&
            (refMv->x - 1) > o.lim.col_min && (refMv->x + 1) < o.lim.col_max;

        if (allIn) {
            std::uint32_t sads[4];
            const std::uint8_t* positions[4] = {bestAddress - o.refStride, bestAddress - 1, bestAddress + 1,
                                                bestAddress + o.refStride};
            oracleSad8x8x4(o.src, o.srcStride, positions, o.refStride, sads);
            for (int j = 0; j < 4; ++j) {
                if (sads[j] < bestSad) {
                    Mv mv;
                    mv.x = (std::int16_t)(refMv->x + neighbors[j][0]);
                    mv.y = (std::int16_t)(refMv->y + neighbors[j][1]);
                    sads[j] += (std::uint32_t)oracleMvsadCost(o, mv, fcenter, errorPerBit);
                    if (sads[j] < bestSad) {
                        bestSad = sads[j];
                        bestSite = j;
                    }
                }
            }
        } else {
            for (int j = 0; j < 4; ++j) {
                Mv mv;
                mv.x = (std::int16_t)(refMv->x + neighbors[j][0]);
                mv.y = (std::int16_t)(refMv->y + neighbors[j][1]);
                const bool inRange = mv.x >= o.lim.col_min && mv.x <= o.lim.col_max && mv.y >= o.lim.row_min &&
                    mv.y <= o.lim.row_max;
                if (inRange) {
                    std::uint32_t sad =
                        oracleSad8x8(o.src, o.srcStride, o.ref + mv.y * o.refStride + mv.x, o.refStride);
                    if (sad < bestSad) {
                        sad += (std::uint32_t)oracleMvsadCost(o, mv, fcenter, errorPerBit);
                        if (sad < bestSad) {
                            bestSad = sad;
                            bestSite = j;
                        }
                    }
                }
            }
        }

        if (bestSite == -1) {
            break;
        } else {
            *secondBestMv = *refMv;
            refMv->y = (std::int16_t)(refMv->y + neighbors[bestSite][1]);
            refMv->x = (std::int16_t)(refMv->x + neighbors[bestSite][0]);
            bestAddress = o.ref + refMv->y * o.refStride + refMv->x;
        }
    }
    return bestSad;
}

// The search fixture: an 8x8 src block plus a 160x160 ref picture whose
// every pixel is high-contrast noise EXCEPT one 8x8 window equal to src at
// the given (dx, dy) offset from the in_what base - a unique zero-SAD
// position.
struct SearchFixture {
    std::vector<std::uint8_t> src;
    std::vector<std::uint8_t> ref;
    static constexpr int kStride = 160;

    SearchFixture(int zeroDx, int zeroDy) : src(8 * 8), ref(kStride * kStride) {
        for (int r = 0; r < 8; ++r) {
            for (int c = 0; c < 8; ++c) {
                src[r * 8 + c] = (std::uint8_t)(64 + 3 * r + 5 * c);
            }
        }
        for (int r = 0; r < kStride; ++r) {
            for (int c = 0; c < kStride; ++c) {
                ref[r * kStride + c] = (std::uint8_t)(((r * 37 + c * 61) & 1) ? 255 : 0);
            }
        }
        for (int r = 0; r < 8; ++r) {
            for (int c = 0; c < 8; ++c) {
                ref[(40 + zeroDy + r) * kStride + (40 + zeroDx + c)] = src[r * 8 + c];
            }
        }
    }

    motion::SearchCtx ctx(int baseRow, int baseCol, bool light, const CostTables& t,
                          const int* const comp[2], Mv* secondBest) const {
        motion::SearchCtx x{};
        x.srcBuf = src.data();
        x.srcStride = 8;
        x.refBuf = ref.data() + baseRow * kStride + baseCol;
        x.refStride = kStride;
        x.mvLimits = {-64, 64, -64, 64};
        x.secondBestMv = secondBest;
        x.approxInterRate = light ? 1 : 0;
        x.nmvVecCost = t.joint;
        x.mvCostStack = comp;
        x.errorperbit = 61;
        return x;
    }

    OracleCtx oracle(int baseRow, int baseCol, bool light, const CostTables& t, const int* const comp[2]) const {
        OracleCtx o;
        o.src = src.data();
        o.srcStride = 8;
        o.ref = ref.data() + baseRow * kStride + baseCol;
        o.refStride = kStride;
        o.lim = {-64, 64, -64, 64};
        o.light = light;
        o.joint = t.joint;
        o.comp = comp;
        return o;
    }
};

// The landed SAD family wires the searches: motion::sad8x8 (the landed nxm
// instance) and motion::sad8x8x4d.
std::uint32_t landedSad8x8(const std::uint8_t* a, int aStride, const std::uint8_t* b, int bStride) {
    return motion::sad8x8(a, (std::uint32_t)aStride, b, (std::uint32_t)bStride);
}
void landedSad8x8x4(const std::uint8_t* a, int aStride, const std::uint8_t* const bArray[], int bStride,
                    std::uint32_t* sadArray) {
    motion::sad8x8x4d(a, (std::uint32_t)aStride, bArray, (std::uint32_t)bStride, sadArray);
}

}  // namespace

TEST_CASE("diamondSearchSad matches the transcription oracle over both gating paths") {
    // av1me.c:291-422: search_param selects the ss ladder offset; the all_in
    // path checks 4 sites per sdx4df call, the bounded path gates each site
    // with is_mv_in; moves update second_best_mv; stalled steps at the
    // in_what base count num00.
    CostTables t;
    const int* const comp[2] = {t.row0(), t.row1()};
    motion::SearchSiteConfig cfg;
    motion::init3smotionCompensation(&cfg, SearchFixture::kStride);

    for (int light = 0; light <= 1; ++light) {
        // Case A: wide limits -> the all_in 4-at-a-time path; the zero sits
        // one len-8 site move from the start (search_param 7 -> len 8), so
        // the walk takes exactly that move and then stalls (every other
        // site's noise SAD plus cost exceeds the winner's total).
        {
            SearchFixture fx(-8, 0);
            Mv secondPort, secondOracle;
            secondPort.as_int = 0;
            secondOracle.as_int = 0;
            motion::SearchCtx x = fx.ctx(40, 40, light != 0, t, comp, &secondPort);
            OracleCtx o = fx.oracle(40, 40, light != 0, t, comp);
            Mv centerMv;
            centerMv.as_int = 0;
            Mv refPort, refOracle;
            refPort.as_int = 0;
            refOracle.as_int = 0;
            Mv bestPort, bestOracle;
            int num00Port = -1, num00Oracle = -1;
            const std::uint32_t got = motion::diamondSearchSad(x, cfg, &refPort, &bestPort, 7, 113, &num00Port,
                                                               &landedSad8x8, &landedSad8x8x4, &centerMv);
            const std::uint32_t want =
                oracleDiamond(o, cfg, centerMv, &refOracle, &bestOracle, &secondOracle, 7, 113, &num00Oracle);
            CHECK(got == want);
            CHECK(bestPort.x == bestOracle.x);
            CHECK(bestPort.y == bestOracle.y);
            CHECK(num00Port == num00Oracle);
            CHECK(secondPort.x == secondOracle.x);
            CHECK(secondPort.y == secondOracle.y);
            // The one-move pin: best (-8, 0), second_best (0, 0), no stalled
            // step at the base (num00 0).
            CHECK(bestPort.x == -8);
            CHECK(bestPort.y == 0);
            CHECK(secondPort.x == 0);
            CHECK(secondPort.y == 0);
            CHECK(num00Port == 0);
            if (light) {
                // The light start site's total: SAD 0 + 1296 + 50*8*8.
                CHECK(got == 4496u);
            }
        }
        // Case B: tight limits -> the bounded per-site path with is_mv_in
        // gating; ref_mv starts outside and clamps.
        {
            SearchFixture fx(3, -5);
            Mv secondPort, secondOracle;
            secondPort.as_int = 0;
            secondOracle.as_int = 0;
            motion::SearchCtx x = fx.ctx(40, 40, light != 0, t, comp, &secondPort);
            x.mvLimits = {-6, 6, -6, 6};
            OracleCtx o = fx.oracle(40, 40, light != 0, t, comp);
            o.lim = {-6, 6, -6, 6};
            Mv centerMv;
            centerMv.as_int = 0;
            Mv refPort, refOracle;
            refPort.x = 30;  // outside; clamps to 6
            refPort.y = -30;
            refOracle = refPort;
            Mv bestPort, bestOracle;
            int num00Port = -1, num00Oracle = -1;
            const std::uint32_t got = motion::diamondSearchSad(x, cfg, &refPort, &bestPort, 9, 113, &num00Port,
                                                               &landedSad8x8, &landedSad8x8x4, &centerMv);
            const std::uint32_t want =
                oracleDiamond(o, cfg, centerMv, &refOracle, &bestOracle, &secondOracle, 9, 113, &num00Oracle);
            CHECK(got == want);
            CHECK(bestPort.x == bestOracle.x);
            CHECK(bestPort.y == bestOracle.y);
            CHECK(num00Port == num00Oracle);
            CHECK(secondPort.x == secondOracle.x);
            CHECK(secondPort.y == secondOracle.y);
            // ref_mv clamped into the limits.
            CHECK(refPort.x == 6);
            CHECK(refPort.y == -6);
        }
    }

    // Case C (degenerate): a flat surface - every SAD is 0, no site improves,
    // every step is a stalled step at the in_what base, so num00 counts all
    // of them and the returned cost is the start cost alone.
    {
        SearchFixture fx(0, 0);
        std::fill(fx.ref.begin(), fx.ref.end(), 0);
        std::fill(fx.src.begin(), fx.src.end(), 0);
        Mv secondPort, secondOracle;
        secondPort.as_int = 0;
        secondOracle.as_int = 0;
        motion::SearchCtx x = fx.ctx(40, 40, true, t, comp, &secondPort);
        OracleCtx o = fx.oracle(40, 40, true, t, comp);
        Mv centerMv;
        centerMv.as_int = 0;
        Mv refPort, refOracle;
        refPort.as_int = 0;
        refOracle.as_int = 0;
        Mv bestPort, bestOracle;
        int num00Port = -1, num00Oracle = -1;
        const std::uint32_t got = motion::diamondSearchSad(x, cfg, &refPort, &bestPort, 7, 113, &num00Port,
                                                           &landedSad8x8, &landedSad8x8x4, &centerMv);
        const std::uint32_t want =
            oracleDiamond(o, cfg, centerMv, &refOracle, &bestOracle, &secondOracle, 7, 113, &num00Oracle);
        CHECK(got == want);
        CHECK(num00Port == 4);  // tot_steps = 11 - 7 = 4 stalled steps
        CHECK(num00Port == num00Oracle);
        CHECK(bestPort.x == 0);
        CHECK(bestPort.y == 0);
        // The start cost: SAD 0 + the light cost of (0,0) vs fcenter (0,0).
        CHECK(got == 1296u);
        CHECK(secondPort.x == 0);
        CHECK(secondPort.y == 0);  // never moved
    }
}

// The scalar 8x4 SAD oracle (motion_estimation.c:43-69's body shape - the
// same EB_ABS_DIFF loop the landed nxm helper generalizes).
std::uint32_t oracleSad8x4(const std::uint8_t* s, std::uint32_t ss, const std::uint8_t* r, std::uint32_t rs) {
    std::uint32_t sad = 0;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 8; ++col) {
            sad += (std::uint32_t)std::abs(s[row * ss + col] - r[row * rs + col]);
        }
    }
    return sad;
}

TEST_CASE("extSadCalculation8x8_16x16 tracks the best SADs and MVs") {
    // motion_estimation.c:100-162: four 8x8 SADs of a 16x16 block (sub_sad
    // recomputes each as an 8x4 kernel over DOUBLED strides, << 1); each
    // 8x8 best updates only on strict <, with the RAW mv packed in; the
    // 16x16 SAD is the four-sum and *p_sad16x16 receives it.
    std::vector<std::uint8_t> src(16 * 24);
    std::vector<std::uint8_t> ref(16 * 24);
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; ++c) {
            src[r * 24 + c] = (std::uint8_t)(r * 11 + c * 3);
            ref[r * 24 + c] = (std::uint8_t)(r * 7 + c * 5 + 9);
        }
    }
    const std::uint32_t srcStride = 24;
    const std::uint32_t refStride = 24;

    for (int sub = 0; sub <= 1; ++sub) {
        // The best arrays start HIGH so every quadrant improves.
        std::uint32_t best8[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        std::uint32_t best16[1] = {0xFFFFFFFFu};
        std::uint32_t bestMv8[4] = {0xDEAD, 0xDEAD, 0xDEAD, 0xDEAD};
        std::uint32_t bestMv16[1] = {0xDEAD};
        std::uint32_t sad8[4] = {0, 0, 0, 0};
        std::uint32_t sad16[1] = {0};
        const std::uint32_t mv = 0x00120034u;  // y=0x12, x=0x34
        motion::extSadCalculation8x8_16x16(src.data(), srcStride, ref.data(), refStride, best8, best16, bestMv8,
                                           bestMv16, mv, sad16, sad8, sub != 0);

        // The oracle: four scalar quadrant SADs.
        std::uint32_t want8[4];
        if (sub) {
            want8[0] = oracleSad8x4(src.data(), 2 * srcStride, ref.data(), 2 * refStride) << 1;
            want8[1] = oracleSad8x4(src.data() + 8, 2 * srcStride, ref.data() + 8, 2 * refStride) << 1;
            want8[2] = oracleSad8x4(src.data() + 8 * srcStride, 2 * srcStride, ref.data() + 8 * refStride,
                                    2 * refStride) << 1;
            want8[3] = oracleSad8x4(src.data() + 8 * srcStride + 8, 2 * srcStride, ref.data() + 8 * refStride + 8,
                                    2 * refStride) << 1;
        } else {
            want8[0] = oracleSad8x8(src.data(), srcStride, ref.data(), refStride);
            want8[1] = oracleSad8x8(src.data() + 8, srcStride, ref.data() + 8, refStride);
            want8[2] = oracleSad8x8(src.data() + 8 * srcStride, srcStride, ref.data() + 8 * refStride, refStride);
            want8[3] = oracleSad8x8(src.data() + 8 * srcStride + 8, srcStride, ref.data() + 8 * refStride + 8,
                                    refStride);
        }
        CHECK(sad8[0] == want8[0]);
        CHECK(sad8[1] == want8[1]);
        CHECK(sad8[2] == want8[2]);
        CHECK(sad8[3] == want8[3]);
        CHECK(best8[0] == want8[0]);
        CHECK(best8[1] == want8[1]);
        CHECK(best8[2] == want8[2]);
        CHECK(best8[3] == want8[3]);
        for (int i = 0; i < 4; ++i) {
            CHECK(bestMv8[i] == mv);
        }
        CHECK(sad16[0] == want8[0] + want8[1] + want8[2] + want8[3]);
        CHECK(best16[0] == sad16[0]);
        CHECK(bestMv16[0] == mv);
    }
}

TEST_CASE("extSadCalculation8x8_16x16 keeps a better prior best on strict less") {
    // The strict-< update: a prior best SMALLER than any quadrant SAD stays,
    // and the 16x16 sum only updates when it beats the prior too.
    std::vector<std::uint8_t> src(16 * 24, 200);
    std::vector<std::uint8_t> ref(16 * 24, 0);
    std::uint32_t best8[4] = {10, 20, 30, 40};  // all four quadrant SADs are 64*200 = 12800 > these
    std::uint32_t best16[1] = {1000000};
    std::uint32_t bestMv8[4] = {7, 7, 7, 7};
    std::uint32_t bestMv16[1] = {9};
    std::uint32_t sad8[4] = {0, 0, 0, 0};
    std::uint32_t sad16[1] = {0};
    motion::extSadCalculation8x8_16x16(src.data(), 24, ref.data(), 24, best8, best16, bestMv8, bestMv16, 0x00AA00BBu,
                                       sad16, sad8, false);
    // The quadrant SADs (12800) do NOT beat the priors; the MVs stay.
    CHECK(best8[0] == 10);
    CHECK(best8[1] == 20);
    CHECK(best8[2] == 30);
    CHECK(best8[3] == 40);
    CHECK(bestMv8[0] == 7);
    CHECK(bestMv8[1] == 7);
    CHECK(bestMv8[2] == 7);
    CHECK(bestMv8[3] == 7);
    // But the 16x16 sum 51200 beats the prior 1000000 -> updates with the mv.
    CHECK(sad16[0] == 51200u);
    CHECK(best16[0] == 51200u);
    CHECK(bestMv16[0] == 0x00AA00BBu);
}

TEST_CASE("extSadCalculation32x32_64x64 folds the 16x16 SAD grid") {
    // motion_estimation.c:164-200: p_sad32x32[k] = the four-sum of
    // p_sad16x16[4k..4k+3]; each 32x32 best updates on strict <; the 64x64
    // is the four 32x32 sum.
    std::uint32_t sad16[16];
    for (int i = 0; i < 16; ++i) {
        sad16[i] = (std::uint32_t)(i * 137 + 11);
    }
    std::uint32_t best32[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    std::uint32_t best64[1] = {0xFFFFFFFFu};
    std::uint32_t bestMv32[4] = {0, 0, 0, 0};
    std::uint32_t bestMv64[1] = {0};
    std::uint32_t sad32[4] = {0, 0, 0, 0};
    const std::uint32_t mv = 0x00FF00EEu;
    motion::extSadCalculation32x32_64x64(sad16, best32, best64, bestMv32, bestMv64, mv, sad32);

    std::uint32_t want32[4];
    for (int k = 0; k < 4; ++k) {
        want32[k] = sad16[4 * k] + sad16[4 * k + 1] + sad16[4 * k + 2] + sad16[4 * k + 3];
        CHECK(sad32[k] == want32[k]);
        CHECK(best32[k] == want32[k]);
        CHECK(bestMv32[k] == mv);
    }
    const std::uint32_t want64 = want32[0] + want32[1] + want32[2] + want32[3];
    CHECK(best64[0] == want64);
    CHECK(bestMv64[0] == mv);
}

TEST_CASE("checkPrehmeEarlyExit applies the zz and l1 gates in order") {
    // motion_estimation.c:1566-1592: the zz gate (me_early_exit_th set and
    // zz_sad below it zeroes the target SearchInfo); the l1 gate (list 1
    // copies the NEGATED list-0 mv and sad when list0 is valid and either
    // its sad < 32*32 or both mv components are < 16 in absolute value).
    motion::MeCtx ctx{};
    ctx.meEarlyExitTh = 100;
    ctx.prehmeL1EarlyExit = 0;
    ctx.zzSad[0][0] = 50;
    ctx.zzSad[0][1] = 100;  // NOT < th
    ctx.zzSad[1][0] = 200;

    // Gate 1 fires: the target SearchInfo is zeroed and marked valid.
    ctx.prehmeData[0][0][0].valid = 0;
    ctx.prehmeData[0][0][0].sad = 12345;
    ctx.prehmeData[0][0][0].bestMv.x = 99;
    ctx.prehmeData[0][0][0].bestMv.y = -99;
    CHECK(motion::checkPrehmeEarlyExit(ctx, 0, 0, 0) == true);
    CHECK(ctx.prehmeData[0][0][0].bestMv.as_int == 0);
    CHECK(ctx.prehmeData[0][0][0].sad == 0);
    CHECK(ctx.prehmeData[0][0][0].valid == 1);

    // Gate 1 misses (zz_sad == th is not < th); l1 off -> untouched, false.
    ctx.prehmeData[0][1][0].valid = 0;
    ctx.prehmeData[0][1][0].sad = 777;
    CHECK(motion::checkPrehmeEarlyExit(ctx, 0, 1, 0) == false);
    CHECK(ctx.prehmeData[0][1][0].sad == 777);
    CHECK(ctx.prehmeData[0][1][0].valid == 0);

    // Gate 2 (l1): list 1 copies the NEGATED list-0 result.
    ctx.prehmeL1EarlyExit = 1;
    ctx.zzSad[1][0] = 200;  // gate 1 misses
    ctx.prehmeData[0][0][1].valid = 1;
    ctx.prehmeData[0][0][1].sad = 500;  // < 32*32
    ctx.prehmeData[0][0][1].bestMv.x = 21;
    ctx.prehmeData[0][0][1].bestMv.y = -34;
    ctx.prehmeData[1][0][1].valid = 0;
    CHECK(motion::checkPrehmeEarlyExit(ctx, 1, 0, 1) == true);
    CHECK(ctx.prehmeData[1][0][1].bestMv.x == -21);
    CHECK(ctx.prehmeData[1][0][1].bestMv.y == 34);
    CHECK(ctx.prehmeData[1][0][1].sad == 500);
    CHECK(ctx.prehmeData[1][0][1].valid == 1);

    // Gate 2 via the small-mv clause (sad large, both components < 16).
    ctx.prehmeData[1][0][1].valid = 0;
    ctx.prehmeData[0][0][1].sad = 50000;  // NOT < 1024
    ctx.prehmeData[0][0][1].bestMv.x = 15;
    ctx.prehmeData[0][0][1].bestMv.y = -15;
    CHECK(motion::checkPrehmeEarlyExit(ctx, 1, 0, 1) == true);
    CHECK(ctx.prehmeData[1][0][1].bestMv.x == -15);
    CHECK(ctx.prehmeData[1][0][1].bestMv.y == 15);
    CHECK(ctx.prehmeData[1][0][1].sad == 50000);

    // Gate 2 misses: mv at the boundary (|x| == 16) with a large sad.
    ctx.prehmeData[1][0][1].valid = 0;
    ctx.prehmeData[0][0][1].sad = 50000;
    ctx.prehmeData[0][0][1].bestMv.x = 16;
    ctx.prehmeData[0][0][1].bestMv.y = 0;
    CHECK(motion::checkPrehmeEarlyExit(ctx, 1, 0, 1) == false);
    CHECK(ctx.prehmeData[1][0][1].valid == 0);

    // Gate 2 misses: list 0 never takes the l1 path (ref 1's zz_sad misses
    // gate 1).
    ctx.prehmeData[0][1][1].sad = 500;
    ctx.prehmeData[0][1][1].valid = 1;
    ctx.prehmeData[0][1][1].bestMv.x = 5;
    ctx.prehmeData[0][1][1].bestMv.y = 5;
    CHECK(motion::checkPrehmeEarlyExit(ctx, 0, 1, 1) == false);

    // Gate 2 misses: list0 invalid.
    ctx.prehmeData[0][0][1].valid = 0;
    CHECK(motion::checkPrehmeEarlyExit(ctx, 1, 0, 1) == false);

    // Gate 2 disabled: l1_early_exit 0 -> false even with valid list0 (ref
    // 1's zz_sad keeps gate 1 closed).
    ctx.zzSad[1][1] = 500;
    ctx.prehmeL1EarlyExit = 0;
    ctx.prehmeData[0][1][1].valid = 1;
    ctx.prehmeData[0][1][1].sad = 500;
    CHECK(motion::checkPrehmeEarlyExit(ctx, 1, 1, 1) == false);
}

TEST_CASE("mePruneRef sums the tab8x8 8x8 SADs and prunes by deviation") {
    // motion_estimation.c:1414-1455: per (list, ref) the hme_sad restarts at
    // 0 (MAX_SAD_VALUE*64 when do_ref == 0) and otherwise sums
    // p_sb_best_sad[l][r][21 + tab8x8[i]] over the 64 PUs; then, when
    // enabled with a th != 0xFFFF, refs (ri >= 1) whose (sad - best)*100
    // exceeds th*best are pruned (do_ref = 0).
    static const std::uint32_t tab8x8[64] = {
        0,  1,  4,  5,  16, 17, 20, 21, 2,  3,  6,  7,  18, 19, 22, 23,
        8,  9,  12, 13, 24, 25, 28, 29, 10, 11, 14, 15, 26, 27, 30, 31,
        32, 33, 36, 37, 48, 49, 52, 53, 34, 35, 38, 39, 50, 51, 54, 55,
        40, 41, 44, 45, 56, 57, 60, 61, 42, 43, 46, 47, 58, 59, 62, 63};
    motion::MeCtx ctx{};
    ctx.numOfListToSearch = 2;
    ctx.numOfRefPicToSearch[0] = 2;
    ctx.numOfRefPicToSearch[1] = 2;
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 4; ++r) {
            ctx.searchResults[l][r].doRef = 1;
        }
    }
    // Deterministic SAD pool: p_sb_best_sad[l][r][21 + i] = 100*l + 7*r + i.
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 4; ++r) {
            for (int i = 0; i < 64; ++i) {
                ctx.pSbBestSad[l][r][21 + i] = (std::uint32_t)(100 * l + 7 * r + i);
            }
        }
    }
    // The oracle sum per (l, r).
    std::uint64_t wantSad[2][2];
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 2; ++r) {
            std::uint64_t s = 0;
            for (int i = 0; i < 64; ++i) {
                s += ctx.pSbBestSad[l][r][21 + tab8x8[i]];
            }
            wantSad[l][r] = s;
        }
    }
    // Pruning disabled: the sums land, nothing prunes.
    ctx.enableMeHmeRefPruning = false;
    motion::mePruneRef(ctx);
    CHECK(ctx.searchResults[0][0].hmeSad == wantSad[0][0]);
    CHECK(ctx.searchResults[0][1].hmeSad == wantSad[0][1]);
    CHECK(ctx.searchResults[1][0].hmeSad == wantSad[1][0]);
    CHECK(ctx.searchResults[1][1].hmeSad == wantSad[1][1]);
    CHECK(ctx.searchResults[0][1].doRef == 1);
    CHECK(ctx.searchResults[1][1].doRef == 1);

    // Pruning enabled with th 50: the worst ref's deviation vs the best.
    // wantSad[1][1] is the largest (100 + 7 more per entry).
    ctx.enableMeHmeRefPruning = true;
    ctx.pruneRefIfMeSadDevBiggerThanTh = 50;
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 4; ++r) {
            ctx.searchResults[l][r].doRef = 1;
        }
    }
    motion::mePruneRef(ctx);
    // The best is wantSad[0][0]; wantSad[1][1] - best = 107 per-entry delta
    // scaled... compute the deviation explicitly:
    const std::uint64_t best = wantSad[0][0];
    const std::uint64_t dev11 = (wantSad[1][1] - best) * 100;
    const bool prune11 = dev11 > (std::uint64_t)50 * best;
    CHECK(ctx.searchResults[1][1].doRef == (prune11 ? 0 : 1));
    CHECK(ctx.searchResults[0][0].doRef == 1);  // ri == 0 never pruned
    // A th so large nothing prunes.
    ctx.pruneRefIfMeSadDevBiggerThanTh = 65535;
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 4; ++r) {
            ctx.searchResults[l][r].doRef = 1;
        }
    }
    motion::mePruneRef(ctx);
    CHECK(ctx.searchResults[1][1].doRef == 1);

    // The th == 0xFFFF sentinel disables pruning entirely.
    ctx.pruneRefIfMeSadDevBiggerThanTh = 0xFFFF;
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 4; ++r) {
            ctx.searchResults[l][r].doRef = 1;
        }
    }
    motion::mePruneRef(ctx);
    CHECK(ctx.searchResults[1][1].doRef == 1);

    // The do_ref == 0 branch: hme_sad becomes MAX_SAD_VALUE * 64.
    ctx.searchResults[0][1].doRef = 0;
    ctx.enableMeHmeRefPruning = false;
    motion::mePruneRef(ctx);
    CHECK(ctx.searchResults[0][1].hmeSad == (std::uint64_t)128 * 128 * 255 * 64);
    CHECK(ctx.searchResults[0][0].hmeSad == wantSad[0][0]);
}

TEST_CASE("getWorstQuadrant picks the max-SAD quadrant and guards the shape") {
    // motion_estimation.c:1736-1766: strict-> updates scanning (0,0),
    // (1,0), (0,1), (1,1); a non-2x2 quadrant shape returns untouched (the
    // svt_aom_assert_err print wall is a named deviation).
    motion::MeCtx ctx{};
    ctx.numHmeSaW = 2;
    ctx.numHmeSaH = 2;
    std::uint8_t bw = 9, bh = 9;

    // All zero: no quadrant ever exceeds max_sad 0 -> outputs stay.
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 4; ++r) {
            for (int cx = 0; cx < 2; ++cx) {
                for (int cy = 0; cy < 2; ++cy) {
                    ctx.hmeLevel0Sad[l][r][cx][cy] = 0;
                }
            }
        }
    }
    motion::getWorstQuadrant(ctx, 0, 0, &bw, &bh);
    CHECK(bw == 9);
    CHECK(bh == 9);

    // (1,0) wins: the scans run (0,0) -> (1,0) -> (0,1) -> (1,1) with
    // strict >, so the LAST strict max wins ties in scan order.
    ctx.hmeLevel0Sad[0][0][0][0] = 5;
    ctx.hmeLevel0Sad[0][0][1][0] = 50;
    ctx.hmeLevel0Sad[0][0][0][1] = 50;
    ctx.hmeLevel0Sad[0][0][1][1] = 50;
    motion::getWorstQuadrant(ctx, 0, 0, &bw, &bh);
    CHECK(bw == 1);
    CHECK(bh == 0);

    // (0,1) beats it.
    ctx.hmeLevel0Sad[0][0][0][1] = 500;
    motion::getWorstQuadrant(ctx, 0, 0, &bw, &bh);
    CHECK(bw == 0);
    CHECK(bh == 1);

    // (1,1) wins with no max_sad update (the last branch writes directly).
    ctx.hmeLevel0Sad[0][0][1][1] = 5000;
    motion::getWorstQuadrant(ctx, 0, 0, &bw, &bh);
    CHECK(bw == 1);
    CHECK(bh == 1);

    // The list/ref indexing is honored.
    ctx.hmeLevel0Sad[1][3][0][0] = 6000;
    ctx.hmeLevel0Sad[1][3][1][1] = 100;
    motion::getWorstQuadrant(ctx, 1, 3, &bw, &bh);
    CHECK(bw == 0);
    CHECK(bh == 0);

    // The guard: a non-2x2 shape leaves the outputs untouched.
    ctx.numHmeSaW = 3;
    motion::getWorstQuadrant(ctx, 0, 0, &bw, &bh);
    CHECK(bw == 0);  // unchanged from the previous call
    CHECK(bh == 0);
}

TEST_CASE("hmePruneRefAndAdjustSr prunes and retunes the SR divisors") {
    // motion_estimation.c:2289-2332: the same deviation pruning as
    // me_prune_ref over the hme_sad values; then, when
    // enable_me_sr_adjustment, the stationary / low-sad divisor assignment
    // per (list, ref).
    motion::MeCtx ctx{};
    ctx.numOfListToSearch = 2;
    ctx.enableMeHmeRefPruning = true;
    ctx.pruneRefIfHmeSadDevBiggerThanTh = 50;
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 4; ++r) {
            ctx.searchResults[l][r].doRef = 1;
            ctx.searchResults[l][r].hmeSad = 1000 + 100 * l + 10 * r;
            ctx.searchResults[l][r].hmeScX = (std::int16_t)(5 * l + r);
            ctx.searchResults[l][r].hmeScY = (std::int16_t)(-3 * r);
            ctx.reduceMeSrDivisor[l][r] = 7;
        }
    }
    ctx.enableMeSrAdjustment = 1;
    ctx.reduceMeSrBasedOnMvLengthTh = 4;
    ctx.stationaryHmeSadAbsTh = 1500;
    ctx.stationaryMeSrDivisor = 2;
    ctx.reduceMeSrBasedOnHmeSadAbsTh = 1100;
    ctx.meSrDivisorForLowHmeSad = 3;

    motion::hmePruneRefAndAdjustSr(ctx);

    // The pruning: best = 1000 (0,0); (sad - 1000)*100 > 50*1000 = 50000 ->
    // sad > 1500. (0,1)=1010 no; (0,2)=1020 no; (0,3)=1030 no; (1,*)=1100+
    // no; nothing crosses 1500 here except... none. All survive.
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 4; ++r) {
            CHECK(ctx.searchResults[l][r].doRef == 1);
        }
    }
    // The divisors: stationary needs |sc_x| <= 4 AND |sc_y| <= 4 AND
    // hme_sad < 1500. (0,0): sc (0, 0), sad 1000 -> stationary (2).
    CHECK(ctx.reduceMeSrDivisor[0][0] == 2);
    // (0,1): sc (1, -3), sad 1010 -> stationary (2).
    CHECK(ctx.reduceMeSrDivisor[0][1] == 2);
    // (1,3): sc (8, -9), sad 1130 -> |8| > 4 -> not stationary; sad 1130 >=
    // 1100 -> not low-sad -> divisor stays 7.
    CHECK(ctx.reduceMeSrDivisor[1][3] == 7);
    // (1,0): sc (5, 0) -> |5| > 4 -> not stationary; sad 1100 >= 1100 ->
    // not low-sad -> stays 7.
    CHECK(ctx.reduceMeSrDivisor[1][0] == 7);
    // A low-sad non-stationary case: craft sc large, sad 1050 (< 1100).
    ctx.searchResults[0][2].hmeScX = 40;
    ctx.searchResults[0][2].hmeSad = 1050;
    motion::hmePruneRefAndAdjustSr(ctx);
    CHECK(ctx.reduceMeSrDivisor[0][2] == 3);

    // The pruning actually fires when the deviation is large enough.
    ctx.searchResults[1][3].hmeSad = 100000;
    motion::hmePruneRefAndAdjustSr(ctx);
    CHECK(ctx.searchResults[1][3].doRef == 0);
    CHECK(ctx.searchResults[1][2].doRef == 1);  // (1,2): sad 1120 < 1500 cap
    // ri == 0 is never pruned even at a huge sad.
    ctx.searchResults[1][0].hmeSad = 1000000;
    motion::hmePruneRefAndAdjustSr(ctx);
    CHECK(ctx.searchResults[1][0].doRef == 1);

    // The th sentinel (0xFFFF) disables the pruning half.
    ctx.pruneRefIfHmeSadDevBiggerThanTh = 0xFFFF;
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 4; ++r) {
            ctx.searchResults[l][r].doRef = 1;
        }
    }
    motion::hmePruneRefAndAdjustSr(ctx);
    CHECK(ctx.searchResults[1][3].doRef == 1);

    // The stationary boundary: sad EXACTLY at stationaryHmeSadAbsTh is NOT
    // stationary (strict <); with the mv lengths inside and the sad above
    // the low-sad threshold the divisor stays untouched.
    ctx.enableMeSrAdjustment = 1;
    ctx.searchResults[0][0].hmeScX = 0;
    ctx.searchResults[0][0].hmeScY = 0;
    ctx.searchResults[0][0].hmeSad = 1500;  // == stationaryHmeSadAbsTh
    ctx.reduceMeSrDivisor[0][0] = 7;
    motion::hmePruneRefAndAdjustSr(ctx);
    CHECK(ctx.reduceMeSrDivisor[0][0] == 7);

    // The sr adjustment off: no divisor writes at all.
    ctx.enableMeSrAdjustment = 0;
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 4; ++r) {
            ctx.reduceMeSrDivisor[l][r] = 9;
        }
    }
    motion::hmePruneRefAndAdjustSr(ctx);
    CHECK(ctx.reduceMeSrDivisor[0][0] == 9);
    CHECK(ctx.reduceMeSrDivisor[1][3] == 9);
}

// motion_estimation.c:1673-1734 get_hme_l0_search_area, transcribed (the
// me_ctx->hme_l0_sa mutation is in-place).
void oracleHmeL0Sa(motion::MeCtx& meCtx, std::uint8_t listIndex, std::uint8_t refPicIndex, std::uint16_t dist,
                   std::int16_t* saWidth, std::int16_t* saHeight) {
    if (meCtx.enableMeSrAdjustment && meCtx.distanceBasedHmeResizing) {
        int isHor = 1;
        int isVer = 1;
        int isStill = 0;

        if (meCtx.reduceHmeL0SrThMin && meCtx.reduceHmeL0SrThMax) {
            if (listIndex || refPicIndex) {
                const std::int16_t l0Mvx = meCtx.xHmeLevel0SearchCenter[0][0][0][0];
                const std::int16_t l0Mvy = meCtx.yHmeLevel0SearchCenter[0][0][0][0];
                isVer = ((std::abs((int)l0Mvx) < meCtx.reduceHmeL0SrThMin) &&
                         (std::abs((int)l0Mvy) > meCtx.reduceHmeL0SrThMax));
                isHor = ((std::abs((int)l0Mvx) > meCtx.reduceHmeL0SrThMax) &&
                         (std::abs((int)l0Mvy) < meCtx.reduceHmeL0SrThMin));
                isStill = ((std::abs((int)l0Mvx) < (meCtx.reduceHmeL0SrThMin * 3)) &&
                           (std::abs((int)l0Mvy) < (meCtx.reduceHmeL0SrThMin * 3)));
            }
        }

        int xOffset = 1;
        int yOffset = 1;
        if (!isVer) {
            yOffset = 2;
        }
        if (!isHor) {
            xOffset = 2;
        }

        if (meCtx.enableMeSrAdjustment == 2) {
            if (isStill) {
                xOffset = 4;
                yOffset = 4;
            }
        }

        meCtx.hmeL0Sa.saMin.width = (std::uint16_t)(meCtx.hmeL0Sa.saMin.width / (xOffset + refPicIndex));
        meCtx.hmeL0Sa.saMin.height = (std::uint16_t)(meCtx.hmeL0Sa.saMin.height / (yOffset + refPicIndex));
        meCtx.hmeL0Sa.saMax.width = (std::uint16_t)(meCtx.hmeL0Sa.saMax.width / (xOffset + refPicIndex));
        meCtx.hmeL0Sa.saMax.height = (std::uint16_t)(meCtx.hmeL0Sa.saMax.height / (yOffset + refPicIndex));
    }

    const std::int32_t hmeSrFactor = (dist * 5 + 7) / 8;

    std::int16_t searchAreaWidth = (std::int16_t)(meCtx.hmeL0Sa.saMin.width / meCtx.numHmeSaW);
    searchAreaWidth = (std::int16_t)std::min((((searchAreaWidth * hmeSrFactor) + 15) & ~0x0F),
                                             (((meCtx.hmeL0Sa.saMax.width / meCtx.numHmeSaW) + 15) & ~0x0F));
    std::int16_t searchAreaHeight = (std::int16_t)(meCtx.hmeL0Sa.saMin.height / meCtx.numHmeSaH);
    searchAreaHeight =
        (std::int16_t)std::min((searchAreaHeight * hmeSrFactor), meCtx.hmeL0Sa.saMax.height / meCtx.numHmeSaH);

    *saWidth = searchAreaWidth;
    *saHeight = searchAreaHeight;
}


TEST_CASE("getHmeL0SearchArea agrees with the transcription oracle") {
    // The oracle runs the same inputs through the test-side transcription;
    // the in-place hme_l0_sa mutation must agree field by field.
    motion::MeCtx ctx{};
    ctx.hmeL0Sa.saMin.width = 320;
    ctx.hmeL0Sa.saMin.height = 160;
    ctx.hmeL0Sa.saMax.width = 640;
    ctx.hmeL0Sa.saMax.height = 480;
    ctx.numHmeSaW = 4;
    ctx.numHmeSaH = 2;
    ctx.enableMeSrAdjustment = 2;
    ctx.distanceBasedHmeResizing = 1;
    ctx.reduceHmeL0SrThMin = 4;
    ctx.reduceHmeL0SrThMax = 12;
    ctx.xHmeLevel0SearchCenter[0][0][0][0] = 2;
    ctx.yHmeLevel0SearchCenter[0][0][0][0] = 3;

    motion::MeCtx b = ctx;
    std::int16_t w = 0, h = 0, wb = 0, hb = 0;
    motion::getHmeL0SearchArea(ctx, 1, 2, 33, &w, &h);
    oracleHmeL0Sa(b, 1, 2, 33, &wb, &hb);
    CHECK(w == wb);
    CHECK(h == hb);
    CHECK(ctx.hmeL0Sa.saMin.width == b.hmeL0Sa.saMin.width);
    CHECK(ctx.hmeL0Sa.saMin.height == b.hmeL0Sa.saMin.height);
    CHECK(ctx.hmeL0Sa.saMax.width == b.hmeL0Sa.saMax.width);
    CHECK(ctx.hmeL0Sa.saMax.height == b.hmeL0Sa.saMax.height);
}

TEST_CASE("getHmeL0SearchArea derives and mutates the HME L0 area") {
    // motion_estimation.c:1673-1734: the distance-based resizing mutates
    // me_ctx->hme_l0_sa in place; the width caps at the 16-rounded sa_max
    // bound, the height at the plain sa_max bound; both scale by
    // getScaledPictureDistance(dist).
    motion::MeCtx ctx{};
    ctx.hmeL0Sa.saMin.width = 320;
    ctx.hmeL0Sa.saMin.height = 160;
    ctx.hmeL0Sa.saMax.width = 640;
    ctx.hmeL0Sa.saMax.height = 480;
    ctx.numHmeSaW = 4;
    ctx.numHmeSaH = 2;
    ctx.enableMeSrAdjustment = 0;
    ctx.distanceBasedHmeResizing = 0;

    std::int16_t w = 0, h = 0;
    motion::getHmeL0SearchArea(ctx, 0, 0, 8, &w, &h);
    // No adjustment: w = min((320/4*5 + 15) & ~15, (640/4 + 15) & ~15) =
    // min((400+15)&~15, (160+15)&~15) = min(400, 160) = 160 (the sa_max cap
    // is 16-ROUNDED too); h = min(160/2*5, 480/2) = min(400, 240) = 240.
    CHECK(w == 160);
    CHECK(h == 240);
    // The ctx sa values are untouched when the adjustment is off.
    CHECK(ctx.hmeL0Sa.saMin.width == 320);

    // Adjustment on, (list 0, ref 0): the l0 center is not consulted
    // (list_index || ref_pic_index is false) -> offsets (1,1) -> division by
    // (1 + 0) = 1: sa_min unchanged; then the same derivation.
    ctx.enableMeSrAdjustment = 1;
    ctx.distanceBasedHmeResizing = 1;
    ctx.reduceHmeL0SrThMin = 4;
    ctx.reduceHmeL0SrThMax = 12;
    ctx.xHmeLevel0SearchCenter[0][0][0][0] = 100;
    ctx.yHmeLevel0SearchCenter[0][0][0][0] = 100;
    motion::getHmeL0SearchArea(ctx, 0, 0, 8, &w, &h);
    CHECK(w == 160);
    CHECK(h == 240);

    // List 1, ref 1 with a VERTICAL l0 mv (|mvx| < min, |mvy| > max):
    // is_ver 1, is_hor 0 -> x_offset 2 -> width / (2+1) = 3, height / (1+1)
    // = 2. sa_min becomes (106, 80); sa_max (213, 240).
    ctx.xHmeLevel0SearchCenter[0][0][0][0] = 1;
    ctx.yHmeLevel0SearchCenter[0][0][0][0] = 100;
    motion::getHmeL0SearchArea(ctx, 1, 1, 8, &w, &h);
    // w = min((106/4*5 + 15) & ~15, (213/4 + 15) & ~15) = min((132+15)&~15,
    // (53+15)&~15) = min(144, 64) = 64; h = min(80/2*5, 240/2) = min(200,
    // 120) = 120.
    CHECK(w == 64);
    CHECK(h == 120);
    CHECK(ctx.hmeL0Sa.saMin.width == 106);
    CHECK(ctx.hmeL0Sa.saMin.height == 80);
    CHECK(ctx.hmeL0Sa.saMax.width == 213);
    CHECK(ctx.hmeL0Sa.saMax.height == 240);

    // enable_me_sr_adjustment == 2 with a STILL l0 mv: offsets (4,4) ->
    // division by 4 + ref. Reset the sa values first.
    ctx.hmeL0Sa.saMin.width = 320;
    ctx.hmeL0Sa.saMin.height = 160;
    ctx.hmeL0Sa.saMax.width = 640;
    ctx.hmeL0Sa.saMax.height = 480;
    ctx.enableMeSrAdjustment = 2;
    ctx.xHmeLevel0SearchCenter[0][0][0][0] = 2;  // |2| < 4*3 still
    ctx.yHmeLevel0SearchCenter[0][0][0][0] = 3;
    motion::getHmeL0SearchArea(ctx, 1, 1, 8, &w, &h);
    // offsets (4,4): width / (4+1) = 320/5 = 64; height 160/5 = 32.
    // sa_max 640/5 = 128; 480/5 = 96.
    // w = min((64/4*5+15)&~15, (128/4+15)&~15) = min(80, 32) = 32 (the cap
    // divides by num_hme_sa_w BEFORE rounding); h = min(32/2*5, 96/2) = 48.
    CHECK(w == 32);
    CHECK(h == 48);
    CHECK(ctx.hmeL0Sa.saMin.width == 64);
    CHECK(ctx.hmeL0Sa.saMin.height == 32);
}

TEST_CASE("initMeHmeData resets the ME/HME state") {
    // motion_estimation.c:2787-2829: the six HME center arrays zero only
    // under enable_hme_flag; p_sb_best_mv always zeroes; per (list, ref):
    // ref_i = ri, do_ref = 1, hme_sad = MAX_U32, divisor 1, zz_sad ~0,
    // both prehme slots invalid; performed_phme zeroes; list_i is set only
    // when me_type != ME_MCTF.
    motion::MeCtx ctx{};
    ctx.enableHmeFlag = true;
    ctx.meType = 3;  // ME_OPEN_LOOP
    ctx.numOfListToSearch = 2;
    ctx.xHmeLevel0SearchCenter[1][2][1][0] = 55;
    ctx.yHmeLevel2SearchCenter[0][3][0][1] = -66;
    ctx.pSbBestMv[1][2][40] = 0xFFFFFFFFu;
    ctx.searchResults[0][1].listI = 200;
    ctx.searchResults[0][1].refI = 200;
    ctx.searchResults[0][1].doRef = 0;
    ctx.searchResults[0][1].hmeSad = 5;
    ctx.reduceMeSrDivisor[1][3] = 9;
    ctx.zzSad[1][3] = 4;
    ctx.prehmeData[1][3][0].valid = 1;
    ctx.prehmeData[1][3][1].valid = 1;
    ctx.performedPhme[0][2][1] = 77;

    motion::initMeHmeData(ctx);
    CHECK(ctx.xHmeLevel0SearchCenter[1][2][1][0] == 0);
    CHECK(ctx.yHmeLevel0SearchCenter[1][2][1][0] == 0);
    CHECK(ctx.xHmeLevel1SearchCenter[0][0][0][0] == 0);
    CHECK(ctx.yHmeLevel1SearchCenter[0][0][0][0] == 0);
    CHECK(ctx.xHmeLevel2SearchCenter[1][2][1][0] == 0);
    CHECK(ctx.yHmeLevel2SearchCenter[0][3][0][1] == 0);
    CHECK(ctx.pSbBestMv[1][2][40] == 0);
    CHECK(ctx.searchResults[0][1].listI == 0);  // me_type != MCTF -> set to li
    CHECK(ctx.searchResults[0][1].refI == 1);
    CHECK(ctx.searchResults[0][1].doRef == 1);
    CHECK(ctx.searchResults[0][1].hmeSad == 0xFFFFFFFFu);
    CHECK(ctx.reduceMeSrDivisor[1][3] == 1);
    CHECK(ctx.zzSad[1][3] == 0xFFFFFFFFu);
    CHECK(ctx.prehmeData[1][3][0].valid == 0);
    CHECK(ctx.prehmeData[1][3][1].valid == 0);
    CHECK(ctx.performedPhme[0][2][1] == 0);

    // me_type == ME_MCTF: list_i is NOT written (stays at the preset value).
    motion::MeCtx ctx2{};
    ctx2.meType = motion::kMeMctf;
    ctx2.numOfListToSearch = 1;
    ctx2.searchResults[0][0].listI = 123;
    motion::initMeHmeData(ctx2);
    CHECK(ctx2.searchResults[0][0].listI == 123);
    CHECK(ctx2.searchResults[0][0].refI == 0);
    CHECK(ctx2.searchResults[0][0].doRef == 1);

    // enable_hme_flag false: the center arrays keep their values.
    motion::MeCtx ctx3{};
    ctx3.enableHmeFlag = false;
    ctx3.xHmeLevel0SearchCenter[0][1][0][0] = 42;
    motion::initMeHmeData(ctx3);
    CHECK(ctx3.xHmeLevel0SearchCenter[0][1][0][0] == 42);
}

TEST_CASE("meStaticB64Bypass populates the static-block pyramid or refuses") {
    // motion_estimation.c:2831-2886: th 0 refuses; the scaled zz SAD vs
    // th*b64_w*b64_h decides; on the static path the SAD pyramid is
    // zz>>0 / >>2 / >>4 / >>6 per the RASTER_SCAN_CU index bands and every
    // non-(0,0) reference gets do_ref = 0.
    std::vector<std::uint8_t> ref(128 * 128);
    std::vector<std::uint8_t> b64(64 * 128);
    for (int r = 0; r < 128; ++r) {
        for (int c = 0; c < 128; ++c) {
            ref[r * 128 + c] = (std::uint8_t)(r * 3 + c);
        }
    }
    for (int r = 0; r < 64; ++r) {
        for (int c = 0; c < 64; ++c) {
            b64[r * 128 + c] = ref[(32 + r) * 128 + (32 + c)];  // exact match
        }
    }
    motion::MeCtx ctx{};
    ctx.dsRef00.yBuffer = ref.data();
    ctx.dsRef00.yStride = 128;
    ctx.dsRef00.width = 128;
    ctx.dsRef00.height = 128;
    ctx.b64SrcPtr = b64.data();
    ctx.b64SrcStride = 128;
    ctx.b64Width = 64;
    ctx.b64Height = 64;
    ctx.numOfListToSearch = 2;
    ctx.numOfRefPicToSearch[0] = 2;
    ctx.numOfRefPicToSearch[1] = 1;
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 4; ++r) {
            ctx.searchResults[l][r].doRef = 1;
        }
    }

    // th 0: refuse.
    ctx.meStaticB64Th = 0;
    CHECK(motion::meStaticB64Bypass(ctx, 32, 32) == false);

    // Exact match: zz = 0 < any th -> the bypass fires and the pyramid is
    // all zeros.
    ctx.meStaticB64Th = 100;
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 4; ++r) {
            ctx.searchResults[l][r].doRef = 1;
        }
    }
    CHECK(motion::meStaticB64Bypass(ctx, 32, 32) == true);
    CHECK(ctx.zzSad[0][0] == 0u);
    CHECK(ctx.searchResults[0][0].doRef == 1);
    CHECK(ctx.searchResults[0][1].doRef == 0);
    CHECK(ctx.searchResults[1][0].doRef == 0);
    for (int i = 0; i < 85; ++i) {
        CHECK(ctx.pSbBestSad[0][0][i] == 0u);
    }

    // A moving block: perturb EVERY sampled row so the subsampled zz is
    // large; th 100 refuses (zz*4096 >= th*4096).
    for (int r = 0; r < 64; r += 2) {
        for (int c = 0; c < 64; ++c) {
            b64[r * 128 + c] = (std::uint8_t)(ref[(32 + r) * 128 + (32 + c)] ^ 0xFF);
        }
    }
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 4; ++r) {
            ctx.searchResults[l][r].doRef = 1;
        }
    }
    CHECK(motion::meStaticB64Bypass(ctx, 32, 32) == false);
    CHECK(ctx.searchResults[0][1].doRef == 1);  // untouched by the refusal

    // The scaling: a 32x32 b64 with subsampled zz_raw Z gives zz = Z * 4.
    for (int r = 0; r < 32; ++r) {
        for (int c = 0; c < 32; ++c) {
            b64[r * 128 + c] = 7;   // uniform 7
            ref[(32 + r) * 128 + (32 + c)] = 0;  // vs uniform 0
        }
    }
    ctx.b64Width = 32;
    ctx.b64Height = 32;
    ctx.meStaticB64Th = 30000;  // the gate: 7168*4096 < 30000*1024
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 4; ++r) {
            ctx.searchResults[l][r].doRef = 1;
        }
    }
    // zz_kernel = 16 rows x 32 px x 7 = 3584; getZzSad doubles it (the
    // subsample compensation) -> l0r0_raw = 7168; the bypass scale
    // 7168 * 64*64 / (32*32) = 28672; 28672*1024 < 20000*1024 -> fires.
    const bool fired = motion::meStaticB64Bypass(ctx, 32, 32);
    CHECK(fired == true);
    CHECK(ctx.zzSad[0][0] == 28672u);
    CHECK(ctx.pSbBestSad[0][0][0] == 28672u);        // 64x64 = zz
    CHECK(ctx.pSbBestSad[0][0][1] == 28672u >> 2);   // 32x32 = zz >> 2
    CHECK(ctx.pSbBestSad[0][0][5] == 28672u >> 4);   // 16x16 = zz >> 4
    CHECK(ctx.pSbBestSad[0][0][21] == 28672u >> 6);  // 8x8 = zz >> 6
    CHECK(ctx.pSbBestSad[0][0][84] == 28672u >> 6);
}

TEST_CASE("computeDistortion folds the PU pyramid into the per-b64 stats") {
    // motion_estimation.c:2738-2785: dist_64 = me_distortion[0]; the 32x32
    // fold adds [1..4], 16x16 adds [5..20], 8x8 adds [21..84]; the variance
    // is sum((d - mean)^2)/64 over the 64 8x8 entries; rc_me_distortion
    // takes the 8x8 sum at <=480p else the 16x16 sum; the four normalized
    // distortions scale by 64*64 / (b64 w*h).
    std::vector<std::uint32_t> variance(2, 0);
    std::vector<std::uint32_t> rcDist(2, 0);
    std::vector<std::uint32_t> d64(2, 0), d32(2, 0), d16(2, 0), d8(2, 0);
    motion::B64Geom geom[2]{};
    geom[0].width = geom[1].width = 64;
    geom[0].height = geom[1].height = 64;
    motion::PcsCtx pcs{};
    pcs.me8x8CostVariance = variance.data();
    pcs.rcMeDistortion = rcDist.data();
    pcs.me64x64Distortion = d64.data();
    pcs.me32x32Distortion = d32.data();
    pcs.me16x16Distortion = d16.data();
    pcs.me8x8Distortion = d8.data();
    pcs.b64Geom = geom;

    motion::MeCtx ctx{};
    for (int i = 0; i < 85; ++i) {
        ctx.meDistortion[i] = (std::uint32_t)(i * 3 + 1);
    }
    // 480p-or-below branch.
    pcs.inputResolution480pOrBelow = true;
    motion::computeDistortion(pcs, 0, ctx);

    std::uint32_t dist64 = ctx.meDistortion[0];
    std::uint32_t dist32 = 0, dist16 = 0, dist8 = 0;
    for (int i = 0; i < 4; ++i) dist32 += ctx.meDistortion[1 + i];
    for (int i = 0; i < 16; ++i) dist16 += ctx.meDistortion[5 + i];
    for (int i = 0; i < 64; ++i) dist8 += ctx.meDistortion[21 + i];
    const std::uint64_t mean8 = dist8 / 64ull;
    std::uint64_t sumOfSq = 0;
    for (int i = 0; i < 64; ++i) {
        const std::int64_t diff = (std::int64_t)ctx.meDistortion[21 + i] - (std::int64_t)mean8;
        sumOfSq += (std::uint64_t)(diff * diff);
    }
    CHECK(d64[0] == dist64);
    CHECK(d32[0] == dist32);
    CHECK(d16[0] == dist16);
    CHECK(d8[0] == dist8);
    CHECK(variance[0] == (std::uint32_t)(sumOfSq / 64));
    CHECK(rcDist[0] == dist8);  // the 480p branch

    // The 720p+ branch: rc_me_distortion takes the 16x16 sum.
    pcs.inputResolution480pOrBelow = false;
    motion::computeDistortion(pcs, 1, ctx);
    CHECK(rcDist[1] == dist16);
    CHECK(variance[1] == (std::uint32_t)(sumOfSq / 64));

    // The partial-b64 normalization: a 32x32 b64 (w*h = 1024) scales every
    // fold by 4096/1024 = 4.
    geom[0].width = geom[1].width = 32;
    geom[0].height = geom[1].height = 32;
    motion::computeDistortion(pcs, 0, ctx);
    CHECK(d64[0] == dist64 * 4);
    CHECK(d32[0] == dist32 * 4);
    CHECK(d16[0] == dist16 * 4);
    CHECK(d8[0] == dist8 * 4);
}

TEST_CASE("extEightSadCalculation8x8_16x16 evaluates the eight x-shifts") {
    // motion_estimation.c:202-316: the eight search positions are ref[0..7]
    // horizontal shifts; four quadrant SADs per position (sub_sad: 8x4
    // kernels over doubled strides, << 1); the 16x16 SAD lands in
    // p_eight_sad16x16[start][s]; best updates pack (y<<16)|(uint16)(x+s)
    // with strict-< first-wins ties.
    std::vector<std::uint8_t> src(16 * 32);
    std::vector<std::uint8_t> ref(16 * 32);
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 24; ++c) {
            src[r * 32 + c] = (std::uint8_t)(31 + 2 * r + 3 * c);
            ref[r * 32 + c] = (std::uint8_t)(5 * r + c + 77);
        }
    }
    const std::uint32_t srcStride = 32;
    const std::uint32_t refStride = 32;

    for (int sub = 0; sub <= 1; ++sub) {
        std::uint32_t best8[16];
        std::uint32_t best16[16];
        std::uint32_t bestMv8[16];
        std::uint32_t bestMv16[16];
        for (int i = 0; i < 16; ++i) {
            best8[i] = 0xFFFFFFFFu;
            best16[i] = 0xFFFFFFFFu;
            bestMv8[i] = 0;
            bestMv16[i] = 0;
        }
        std::uint32_t eightSad16[16][8];
        std::uint32_t eightSad8[64][8];  // (void)ed by the reference
        const std::uint32_t mv = 0x00030005u;  // _MVYT=3, _MVXT=5
        const std::uint32_t startPos = 2;
        // The reference advances the best arrays INTERNALLY by
        // 4*start_16x16_pos (8x8) and start_16x16_pos (16x16) - the base
        // arrays are passed, not pre-offset ones.
        motion::extEightSadCalculation8x8_16x16(src.data(), srcStride, ref.data(), refStride, mv, startPos, best8,
                                                best16, bestMv8, bestMv16, eightSad16, eightSad8, sub != 0);

        // The oracle sweep over the eight shifts - filled completely before
        // the argmin comparisons below.
        std::uint32_t want[8][4];
        std::uint32_t wantSum[8];
        for (int s = 0; s < 8; ++s) {
            if (sub) {
                want[s][0] = oracleSad8x4(src.data(), 2 * srcStride, ref.data() + s, 2 * refStride) << 1;
                want[s][1] = oracleSad8x4(src.data() + 8, 2 * srcStride, ref.data() + 8 + s, 2 * refStride) << 1;
                want[s][2] = oracleSad8x4(src.data() + 8 * srcStride, 2 * srcStride, ref.data() + 8 * refStride + s,
                                          2 * refStride) << 1;
                want[s][3] = oracleSad8x4(src.data() + 8 * srcStride + 8, 2 * srcStride,
                                          ref.data() + 8 * refStride + 8 + s, 2 * refStride) << 1;
            } else {
                want[s][0] = oracleSad8x8(src.data(), srcStride, ref.data() + s, refStride);
                want[s][1] = oracleSad8x8(src.data() + 8, srcStride, ref.data() + 8 + s, refStride);
                want[s][2] = oracleSad8x8(src.data() + 8 * srcStride, srcStride, ref.data() + 8 * refStride + s,
                                          refStride);
                want[s][3] = oracleSad8x8(src.data() + 8 * srcStride + 8, srcStride,
                                          ref.data() + 8 * refStride + 8 + s, refStride);
            }
            wantSum[s] = want[s][0] + want[s][1] + want[s][2] + want[s][3];
            CHECK(eightSad16[startPos][s] == wantSum[s]);
        }
        // Strict-< first-wins: the winning shift is the FIRST s achieving
        // the minimum.
        for (int k = 0; k < 4; ++k) {
            int argmin = 0;
            for (int t = 1; t < 8; ++t) {
                if (want[t][k] < want[argmin][k]) {
                    argmin = t;
                }
            }
            CHECK(best8[4 * startPos + k] == want[argmin][k]);
            const std::int16_t xmv = (std::int16_t)(mv & 0xFFFF) + (std::int16_t)argmin;
            const std::int16_t ymv = (std::int16_t)((mv >> 16) & 0xFFFF);
            CHECK(bestMv8[4 * startPos + k] == (((std::uint32_t)(std::uint16_t)ymv << 16) | (std::uint16_t)xmv));
        }
        {
            int argminSum = 0;
            for (int t2 = 1; t2 < 8; ++t2) {
                if (wantSum[t2] < wantSum[argminSum]) {
                    argminSum = t2;
                }
            }
            CHECK(best16[startPos] == wantSum[argminSum]);
            const std::int16_t xmv16 = (std::int16_t)(mv & 0xFFFF) + (std::int16_t)argminSum;
            const std::int16_t ymv16 = (std::int16_t)((mv >> 16) & 0xFFFF);
            CHECK(bestMv16[startPos] == (((std::uint32_t)(std::uint16_t)ymv16 << 16) | (std::uint16_t)xmv16));
        }
        // The other start positions are untouched.
        for (int p = 0; p < 16; ++p) {
            if ((std::uint32_t)p != startPos) {
                CHECK(best16[p] == 0xFFFFFFFFu);
                CHECK(bestMv16[p] == 0);
            }
        }
        for (int i = 0; i < 16; ++i) {
            if (i < 4 * (int)startPos || i >= 4 * (int)startPos + 4) {
                CHECK(best8[i] == 0xFFFFFFFFu);
                CHECK(bestMv8[i] == 0);
            }
        }
    }
}

TEST_CASE("extAllSadCalculation8x8_16x16 walks the Z-order block offsets") {
    // motion_estimation.c:318-349: the 4x4 loop of 16x16 blocks; the block
    // at (y, x) runs ext_eight with start_16x16_pos = offsets[4y+x] where
    // offsets = {0,1,4,5, 2,3,6,7, 8,9,12,13, 10,11,14,15}; the block bases
    // are src + 16y*stride + 16x and ref + 16y*ref_stride + 16x.
    std::vector<std::uint8_t> src(64 * 72);
    std::vector<std::uint8_t> ref(64 * 72);
    for (int r = 0; r < 64; ++r) {
        for (int c = 0; c < 72; ++c) {
            src[r * 72 + c] = (std::uint8_t)(r * 3 + c * 5);
            ref[r * 72 + c] = (std::uint8_t)(r + c * 7 + 41);
        }
    }
    const std::uint32_t stride = 72;
    static const int offsets[16] = {0, 1, 4, 5, 2, 3, 6, 7, 8, 9, 12, 13, 10, 11, 14, 15};

    for (int sub = 0; sub <= 1; ++sub) {
        std::uint32_t best8[64];
        std::uint32_t best16[16];
        std::uint32_t bestMv8[64];
        std::uint32_t bestMv16[16];
        std::uint32_t eightSad16[16][8];
        std::uint32_t eightSad8[64][8];
        for (int i = 0; i < 64; ++i) {
            best8[i] = 0xFFFFFFFFu;
            bestMv8[i] = 0;
        }
        for (int i = 0; i < 16; ++i) {
            best16[i] = 0xFFFFFFFFu;
            bestMv16[i] = 0;
        }
        const std::uint32_t mv = 0x00020004u;
        motion::extAllSadCalculation8x8_16x16(src.data(), stride, ref.data(), stride, mv, best8, best16, bestMv8,
                                              bestMv16, eightSad16, eightSad8, sub != 0);

        // The oracle: per block (y, x), the scalar quadrant SADs at each
        // shift land in eightSad16[offsets[4y+x]][s], and the best arrays
        // track the minima with the packed mv.
        for (int by = 0; by < 4; ++by) {
            for (int bx = 0; bx < 4; ++bx) {
                const int off = offsets[4 * by + bx];
                const std::uint8_t* bsrc = src.data() + 16 * by * stride + 16 * bx;
                const std::uint8_t* bref = ref.data() + 16 * by * stride + 16 * bx;
                for (int s = 0; s < 8; ++s) {
                    std::uint32_t want[4];
                    if (sub) {
                        want[0] = oracleSad8x4(bsrc, 2 * stride, bref + s, 2 * stride) << 1;
                        want[1] = oracleSad8x4(bsrc + 8, 2 * stride, bref + 8 + s, 2 * stride) << 1;
                        want[2] = oracleSad8x4(bsrc + 8 * stride, 2 * stride, bref + 8 * stride + s, 2 * stride) << 1;
                        want[3] = oracleSad8x4(bsrc + 8 * stride + 8, 2 * stride, bref + 8 * stride + 8 + s,
                                               2 * stride) << 1;
                    } else {
                        want[0] = oracleSad8x8(bsrc, stride, bref + s, stride);
                        want[1] = oracleSad8x8(bsrc + 8, stride, bref + 8 + s, stride);
                        want[2] = oracleSad8x8(bsrc + 8 * stride, stride, bref + 8 * stride + s, stride);
                        want[3] = oracleSad8x8(bsrc + 8 * stride + 8, stride, bref + 8 * stride + 8 + s, stride);
                    }
                    CHECK(eightSad16[off][s] == want[0] + want[1] + want[2] + want[3]);
                }
            }
        }
        // The 64 8x8 bests and 16 16x16 bests each hold their block's min.
        for (int off = 0; off < 16; ++off) {
            CHECK(best16[off] < 0xFFFFFFFFu);
            CHECK(bestMv16[off] != 0);
        }
        for (int i = 0; i < 64; ++i) {
            CHECK(best8[i] < 0xFFFFFFFFu);
            CHECK(bestMv8[i] != 0);
        }
    }
}

TEST_CASE("getScaledPictureDistance rounds dist*5/8 up") {
    // motion_estimation.c:1152-1160: DIVIDE_AND_CEIL(dist * 5, 8) =
    // (dist*5 + 7) / 8 over the uint16 domain.
    for (int d = 0; d <= 65535; d += 97) {
        const std::uint16_t want = (std::uint16_t)((d * 5 + 7) / 8);
        CHECK(motion::getScaledPictureDistance((std::uint16_t)d) == want);
    }
    CHECK(motion::getScaledPictureDistance(0) == 0);
    CHECK(motion::getScaledPictureDistance(1) == 1);   // (5+7)/8 = 1
    CHECK(motion::getScaledPictureDistance(8) == 5);   // 40/8
    CHECK(motion::getScaledPictureDistance(9) == 6);   // 45+7=52/8=6
    CHECK(motion::getScaledPictureDistance(65535) == 40960);
}

TEST_CASE("applyMeSaBoost scales by the boost table band") {
    // motion_estimation.c:1162-1182: the band index from hme_sad vs
    // {2,3,4}*64*64; the multiplier table rows are boost 1..3 x
    // {1.0,1.0,3.0,4.0,5.0} / {1.0,1.0,2.5,3.5,4.5} / {1.0,1.0,2.0,2.5,3.5};
    // the outputs are int16 narrowings of width*mult / height*mult.
    static const double table[3][5] = {{1.0, 1.0, 3.0, 4.0, 5.0},
                                       {1.0, 1.0, 2.5, 3.5, 4.5},
                                       {1.0, 1.0, 2.0, 2.5, 3.5}};
    // Band indices: 12288 = 3*64*64 is NOT > 12288, so it stays at index 2;
    // 16385 = 4*64*64+1 crosses the last threshold.
    const std::uint64_t bands[5] = {0, 2 * 64 * 64, 2 * 64 * 64 + 1, 3 * 64 * 64, 4 * 64 * 64 + 1};
    const int wantIndex[5] = {0, 0, 2, 2, 4};
    for (int b = 0; b < 5; ++b) {
        for (int boost = 1; boost <= 3; ++boost) {
            for (int w0 : {7, 64, 100}) {
                for (int h0 : {9, 32, 128}) {
                    std::int16_t w = (std::int16_t)w0;
                    std::int16_t h = (std::int16_t)h0;
                    motion::applyMeSaBoost(&w, &h, bands[b], boost);
                    const double mult = table[boost - 1][wantIndex[b]];
                    CHECK(w == (std::int16_t)((double)w0 * mult));
                    CHECK(h == (std::int16_t)((double)h0 * mult));
                }
            }
        }
    }
    // The exact band edges: 2*64*64 = 8192 is NOT > 8192 (index 0);
    // 8193 is (index 2).
    std::int16_t w = 10, h = 10;
    motion::applyMeSaBoost(&w, &h, 8192, 1);
    CHECK(w == 10);
    CHECK(h == 10);
    w = 10;
    h = 10;
    motion::applyMeSaBoost(&w, &h, 8193, 1);
    CHECK(w == 30);
    CHECK(h == 30);
    // The band boundaries continue: > 3*64*64 -> index 3 (x4); > 4*64*64 ->
    // index 4 (x5).
    w = 10;
    motion::applyMeSaBoost(&w, &h, 3 * 64 * 64 + 1, 1);
    CHECK(w == 40);
    w = 10;
    motion::applyMeSaBoost(&w, &h, 4 * 64 * 64 + 1, 1);
    CHECK(w == 50);
}

TEST_CASE("getZzSad is the subsampled 64x64 zero-motion SAD") {
    // motion_estimation.c:1540-1564: the nxm SAD of the b64 source vs the
    // ref block at the SB origin, over (sb_height>>1, sb_width) with BOTH
    // strides doubled, then << 1.
    std::vector<std::uint8_t> ref(128 * 128);
    std::vector<std::uint8_t> b64(64 * 128);
    for (int r = 0; r < 128; ++r) {
        for (int c = 0; c < 128; ++c) {
            ref[r * 128 + c] = (std::uint8_t)(r * 3 + c);
        }
    }
    for (int r = 0; r < 64; ++r) {
        for (int c = 0; c < 64; ++c) {
            b64[r * 128 + c] = ref[(64 + r) * 128 + (64 + c)];  // exact match
        }
    }
    motion::RefPic pic;
    pic.yBuffer = ref.data();
    pic.yStride = 128;
    pic.width = 128;
    pic.height = 128;
    motion::MeCtx ctx{};
    ctx.b64SrcPtr = b64.data();
    ctx.b64SrcStride = 128;
    // Exact match: the subsampled SAD reads rows 0,2,..62 (32 rows x 64 px)
    // and doubles -> 0.
    CHECK(motion::getZzSad(pic, ctx, 64, 64, 64, 64) == 0u);
    // Perturb one sampled pixel: rows 0,2,..62 are the sampled rows; row 4
    // col 3 is sampled -> the SAD doubles to 2*|diff|.
    b64[4 * 128 + 3] = (std::uint8_t)(ref[(64 + 4) * 128 + (64 + 3)] + 9);
    CHECK(motion::getZzSad(pic, ctx, 64, 64, 64, 64) == 18u);
    // A perturbation on an UNSAMPLED row (row 5) does not move the SAD.
    b64[4 * 128 + 3] = ref[(64 + 4) * 128 + (64 + 3)];
    b64[5 * 128 + 3] = (std::uint8_t)(ref[(64 + 5) * 128 + (64 + 3)] + 200);
    CHECK(motion::getZzSad(pic, ctx, 64, 64, 64, 64) == 0u);
}

TEST_CASE("check00Center applies the four edge corrections and the cost tie") {
    // motion_estimation.c:1060-1126: the zero-MV SAD (zz_sad when
    // me_early_exit_th, else the subsampled kernel, both << 1); the four
    // search-center corrections against pad 63 and the picture bounds; the
    // hme SAD at the corrected center; MIN cost; the zero-cost tie zeroes
    // both centers; the hme SAD is the return.
    std::vector<std::uint8_t> ref(128 * 128);
    std::vector<std::uint8_t> b64(64 * 128);
    for (int r = 0; r < 128; ++r) {
        for (int c = 0; c < 128; ++c) {
            ref[r * 128 + c] = (std::uint8_t)(((r * 37 + c * 61) & 1) ? 250 : 5);
        }
    }
    // b64 matches the ref block at the SB origin (the zero-MV position).
    for (int r = 0; r < 64; ++r) {
        for (int c = 0; c < 64; ++c) {
            b64[r * 128 + c] = ref[(64 + r) * 128 + (64 + c)];
        }
    }
    motion::RefPic pic;
    pic.yBuffer = ref.data();
    pic.yStride = 128;
    pic.width = 128;
    pic.height = 128;

    // Case A: me_early_exit_th set -> the zero SAD is the threaded zz_sad.
    motion::MeCtx ctx{};
    ctx.b64SrcPtr = b64.data();
    ctx.b64SrcStride = 128;
    ctx.meEarlyExitTh = 1;
    {
        std::int16_t xsc = 3, ysc = -4;
        const std::uint64_t got = motion::check00Center(pic, ctx, 64, 64, 64, 64, &xsc, &ysc, 777u);
        // The zero SAD = 777 << 1; the hme SAD = the subsampled SAD at the
        // center offset (64+3, 64-4) << 1. The zero cost (777<<1 << 8) is
        // far smaller -> the centers zero out; the return is the hme SAD.
        std::uint32_t hmeSub = 0;
        for (int r = 0; r < 64; r += 2) {
            for (int c = 0; c < 64; ++c) {
                hmeSub += (std::uint32_t)std::abs((int)b64[r * 128 + c] -
                                                  (int)ref[(64 + r - 4) * 128 + (64 + c + 3)]);
            }
        }
        const std::uint64_t wantHme = (std::uint64_t)hmeSub << 1;
        CHECK(got == wantHme);
        CHECK(xsc == 0);
        CHECK(ysc == 0);
    }
    // Case B: me_early_exit_th zero -> the zero SAD is recomputed (0 for an
    // exact match) and the zero cost ties-or-wins -> centers zeroed.
    ctx.meEarlyExitTh = 0;
    {
        std::int16_t xsc = 0, ysc = 1;
        const std::uint64_t got = motion::check00Center(pic, ctx, 64, 64, 64, 64, &xsc, &ysc, 0u);
        std::uint32_t hmeSub = 0;
        for (int r = 0; r < 64; r += 2) {
            for (int c = 0; c < 64; ++c) {
                hmeSub += (std::uint32_t)std::abs((int)b64[r * 128 + c] -
                                                  (int)ref[(64 + r + 1) * 128 + (64 + c)]);
            }
        }
        CHECK(got == ((std::uint64_t)hmeSub << 1));
        CHECK(xsc == 0);
        CHECK(ysc == 0);
    }
    // Case C: the edge corrections bind. The right-edge correction always
    // lands the center at width-1-org (xsc' = width-1-org = 199-80 = 119);
    // place the b64 content to match the ref block AT that corrected center
    // so the hme SAD is 0, the hme cost wins, and the centers keep the
    // corrected values.
    {
        std::vector<std::uint8_t> ref2(320 * 320);
        for (int r = 0; r < 320; ++r) {
            for (int c = 0; c < 320; ++c) {
                ref2[r * 320 + c] = (std::uint8_t)((r * 13 + c * 7) ^ 0x5A);
            }
        }
        // b64 matches the ref block at (80+119, 80+119) = (199, 199).
        for (int r = 0; r < 64; ++r) {
            for (int c = 0; c < 64; ++c) {
                b64[r * 128 + c] = ref2[(199 + r) * 320 + (199 + c)];
            }
        }
        motion::RefPic pic2;
        pic2.yBuffer = ref2.data();
        pic2.yStride = 320;
        pic2.width = 200;
        pic2.height = 200;
        ctx.meEarlyExitTh = 1;
        std::int16_t xsc = 200, ysc = 200;  // wildly out
        const std::uint64_t got = motion::check00Center(pic2, ctx, 80, 80, 64, 64, &xsc, &ysc, 0xFFFFFFu);
        // The x correction: 80+200 = 280 > 199 -> xsc = 280-199 corrected to
        // width-1-org = 119; the y correction the same. The hme block at
        // (199, 199) matches b64 exactly -> hme SAD 0 < zero cost -> the
        // centers KEEP the corrected values.
        CHECK(xsc == 119);
        CHECK(ysc == 119);
        CHECK(got == 0);
    }
    // Case D: the LEFT-edge corrections bind. The corrected center is
    // -63-org (negative); the RefPic's yBuffer sits inside a padded
    // allocation (the reference's own border discipline) so the corrected
    // block is readable and the hme SAD 0 lets the corrected values survive.
    {
        std::vector<std::uint8_t> alloc(512 * 512);
        for (int r = 0; r < 512; ++r) {
            for (int c = 0; c < 512; ++c) {
                alloc[r * 512 + c] = (std::uint8_t)((r * 29 + c * 17) ^ 0xA5);
            }
        }
        // The logical origin sits 256 pixels in; org (80, 80) is relative to
        // it. The corrected center: org + (-200) = -120 < -63 -> xsc =
        // -63-80 = -143; the hme block is at logical (-63, -63) = physical
        // (193, 193).
        for (int r = 0; r < 64; ++r) {
            for (int c = 0; c < 64; ++c) {
                b64[r * 128 + c] = alloc[(193 + r) * 512 + (193 + c)];
            }
        }
        motion::RefPic pic3;
        pic3.yBuffer = alloc.data() + 256 * 512 + 256;
        pic3.yStride = 512;
        pic3.width = 200;
        pic3.height = 200;
        ctx.meEarlyExitTh = 1;
        std::int16_t xsc = -200, ysc = -200;
        const std::uint64_t got = motion::check00Center(pic3, ctx, 80, 80, 64, 64, &xsc, &ysc, 0xFFFFFFu);
        CHECK(xsc == -143);
        CHECK(ysc == -143);
        CHECK(got == 0);
    }
}

TEST_CASE("extEightSadCalculation32x32_64x64 folds the eight-shift 16x16 grid") {
    // motion_estimation.c:351-406: per shift s, p_sad32x32[k][s] is the
    // four-sum of p_sad16x16[4k..4k+3][s]; the 32x32/64x64 bests update on
    // strict < with the packed (y, x+s) mv.
    std::uint32_t sad16[16][8];
    for (int i = 0; i < 16; ++i) {
        for (int s = 0; s < 8; ++s) {
            sad16[i][s] = (std::uint32_t)(1000 + i * 137 + s * 53);
        }
    }
    std::uint32_t best32[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    std::uint32_t best64[1] = {0xFFFFFFFFu};
    std::uint32_t bestMv32[4] = {0, 0, 0, 0};
    std::uint32_t bestMv64[1] = {0};
    std::uint32_t sad32[4][8];
    const std::uint32_t mv = 0x00060008u;  // y=6, x=8
    motion::extEightSadCalculation32x32_64x64(sad16, best32, best64, bestMv32, bestMv64, mv, sad32);

    for (int s = 0; s < 8; ++s) {
        std::uint32_t want32[4];
        std::uint32_t sum = 0;
        for (int k = 0; k < 4; ++k) {
            want32[k] = sad16[4 * k][s] + sad16[4 * k + 1][s] + sad16[4 * k + 2][s] + sad16[4 * k + 3][s];
            CHECK(sad32[k][s] == want32[k]);
            sum += want32[k];
        }
        // With monotonically increasing sad16 in i, the first shift (s=0)
        // holds every strict-< minimum; later shifts never improve.
        if (s == 0) {
            for (int k = 0; k < 4; ++k) {
                CHECK(best32[k] == want32[k]);
                const std::int16_t xmv = (std::int16_t)(mv & 0xFFFF);
                const std::int16_t ymv = (std::int16_t)((mv >> 16) & 0xFFFF);
                CHECK(bestMv32[k] == (((std::uint32_t)(std::uint16_t)ymv << 16) | (std::uint16_t)xmv));
            }
            CHECK(best64[0] == sum);
            CHECK(bestMv64[0] == (((std::uint32_t)(std::uint16_t)6 << 16) | (std::uint16_t)8));
        } else {
            CHECK(best32[0] < want32[0]);  // the s=0 winner survives
        }
    }
}

TEST_CASE("refiningSearchSad walks one-px neighbors until the stall") {
    // av1me.c:424-487: the four neighbors {0,-1},{-1,0},{1,0},{0,1}; all_in
    // uses the sdx4df quartet, the bounded path gates each with is_mv_in;
    // every cost references FCENTER; best_site == -1 breaks the walk.
    CostTables t;
    const int* const comp[2] = {t.row0(), t.row1()};

    for (int light = 0; light <= 1; ++light) {
        // Case A: the zero one pixel up from the start; the walk takes that
        // step and then stalls (every other neighbor's noise SAD plus cost
        // exceeds the winner's total).
        {
            SearchFixture fx(0, -1);
            Mv secondPort, secondOracle;
            secondPort.as_int = 0;
            secondOracle.as_int = 0;
            motion::SearchCtx x = fx.ctx(40, 40, light != 0, t, comp, &secondPort);
            OracleCtx o = fx.oracle(40, 40, light != 0, t, comp);
            Mv centerMv;
            centerMv.as_int = 0;
            Mv refPort, refOracle;
            refPort.as_int = 0;
            refOracle.as_int = 0;
            const std::uint32_t got = motion::refiningSearchSad(x, &refPort, 113, 4, &landedSad8x8, &landedSad8x8x4,
                                                                &centerMv);
            const std::uint32_t want = oracleRefining(o, centerMv, &refOracle, 113, 4, &secondOracle);
            CHECK(got == want);
            CHECK(refPort.x == refOracle.x);
            CHECK(refPort.y == refOracle.y);
            CHECK(refPort.x == 0);
            CHECK(refPort.y == -1);
            CHECK(secondPort.x == 0);
            CHECK(secondPort.y == 0);
            if (light) {
                // The light cost of (0,-1) vs fcenter (0,0).
                CHECK(got == 1696u);
            }
        }
        // Case B: tight limits around the start -> the bounded path; the
        // zero at (3, 2) reached by the walk within the limits.
        {
            SearchFixture fx(3, 2);
            Mv secondPort, secondOracle;
            secondPort.as_int = 0;
            secondOracle.as_int = 0;
            motion::SearchCtx x = fx.ctx(40, 40, light != 0, t, comp, &secondPort);
            x.mvLimits = {-8, 8, -8, 8};
            OracleCtx o = fx.oracle(40, 40, light != 0, t, comp);
            o.lim = {-8, 8, -8, 8};
            Mv centerMv;
            centerMv.as_int = 0;
            Mv refPort, refOracle;
            refPort.as_int = 0;
            refOracle.as_int = 0;
            const std::uint32_t got = motion::refiningSearchSad(x, &refPort, 113, 8, &landedSad8x8, &landedSad8x8x4,
                                                                &centerMv);
            const std::uint32_t want = oracleRefining(o, centerMv, &refOracle, 113, 8, &secondOracle);
            CHECK(got == want);
            CHECK(refPort.x == refOracle.x);
            CHECK(refPort.y == refOracle.y);
            CHECK(refPort.x == 3);
            CHECK(refPort.y == 2);
            CHECK(secondPort.x == secondOracle.x);
            CHECK(secondPort.y == secondOracle.y);
        }
    }

    // Case C (degenerate): search_range 0 - the loop never runs; the start
    // cost returns with ref_mv untouched.
    {
        SearchFixture fx(0, 0);
        Mv secondPort, secondOracle;
        secondPort.as_int = 0;
        secondOracle.as_int = 0;
        motion::SearchCtx x = fx.ctx(40, 40, true, t, comp, &secondPort);
        OracleCtx o = fx.oracle(40, 40, true, t, comp);
        Mv centerMv;
        centerMv.as_int = 0;
        Mv refPort, refOracle;
        refPort.as_int = 0;
        refOracle.as_int = 0;
        const std::uint32_t got =
            motion::refiningSearchSad(x, &refPort, 113, 0, &landedSad8x8, &landedSad8x8x4, &centerMv);
        const std::uint32_t want = oracleRefining(o, centerMv, &refOracle, 113, 0, &secondOracle);
        CHECK(got == want);
        CHECK(got == 1296u);  // SAD(start) 0 + light cost (0,0)
        CHECK(refPort.x == 0);
        CHECK(refPort.y == 0);
        CHECK(secondPort.x == 0);
        CHECK(secondPort.y == 0);  // never written
    }
}

TEST_CASE("exhaustiveMeshSearch matches the transcription oracle over both steps") {
    // av1me.c:212-289: the step>1 pass checks a strided grid; the step==1
    // pass checks every location (4-at-a-time via sdx4df with the tail
    // fallback). Both passes must agree with the transcription over the
    // light and full cost forms, and the unique zero must be found.
    CostTables t;
    const int* const comp[2] = {t.row0(), t.row1()};

    for (int light = 0; light <= 1; ++light) {
        // Case A: step = 4, the zero at a grid point (multiples of 4 from
        // the center).
        {
            SearchFixture fx(-8, 12);
            Mv secondPort, secondOracle;
            secondPort.as_int = 0;
            secondOracle.as_int = 0;
            motion::SearchCtx x = fx.ctx(40, 40, light != 0, t, comp, &secondPort);
            OracleCtx o = fx.oracle(40, 40, light != 0, t, comp);
            Mv centerMv;
            centerMv.as_int = 0;  // fcenter = (0, 0)
            Mv refMv;
            refMv.as_int = 0;
            Mv bestPort, bestOracle;
            const std::uint32_t got = motion::exhaustiveMeshSearch(x, &refMv, &bestPort, 32, 4, 113, &landedSad8x8,
                                                                   &landedSad8x8x4, &centerMv);
            const std::uint32_t want = oracleExhaustive(o, centerMv, refMv, 32, 4, 113, &bestOracle, &secondOracle);
            CHECK(got == want);
            CHECK(bestPort.x == bestOracle.x);
            CHECK(bestPort.y == bestOracle.y);
            CHECK(bestPort.x == -8);
            CHECK(bestPort.y == 12);
            CHECK(secondPort.x == secondOracle.x);
            CHECK(secondPort.y == secondOracle.y);
        }
        // Case B: step = 1, the zero at a non-grid point; the exhaustive
        // pass must find it exactly.
        {
            SearchFixture fx(5, -7);
            Mv secondPort, secondOracle;
            secondPort.as_int = 0;
            secondOracle.as_int = 0;
            motion::SearchCtx x = fx.ctx(40, 40, light != 0, t, comp, &secondPort);
            OracleCtx o = fx.oracle(40, 40, light != 0, t, comp);
            Mv centerMv;
            centerMv.as_int = 0;
            Mv refMv;
            refMv.as_int = 0;
            Mv bestPort, bestOracle;
            const std::uint32_t got = motion::exhaustiveMeshSearch(x, &refMv, &bestPort, 32, 1, 113, &landedSad8x8,
                                                                   &landedSad8x8x4, &centerMv);
            const std::uint32_t want = oracleExhaustive(o, centerMv, refMv, 32, 1, 113, &bestOracle, &secondOracle);
            CHECK(got == want);
            CHECK(bestPort.x == bestOracle.x);
            CHECK(bestPort.y == bestOracle.y);
            CHECK(bestPort.x == 5);
            CHECK(bestPort.y == -7);
            CHECK(secondPort.x == secondOracle.x);
            CHECK(secondPort.y == secondOracle.y);
        }
    }

    // Case C (degenerate): the mv limits collapse the range so the loops do
    // not run; the result is the clamped center and its initial cost.
    {
        SearchFixture fx(0, 0);
        Mv secondPort, secondOracle;
        secondPort.as_int = 0;
        secondOracle.as_int = 0;
        motion::SearchCtx x = fx.ctx(40, 40, true, t, comp, &secondPort);
        x.mvLimits = {0, 0, 0, 0};
        OracleCtx o = fx.oracle(40, 40, true, t, comp);
        o.lim = {0, 0, 0, 0};
        Mv centerMv;
        centerMv.x = 37;  // out of the limits; clamped to 0
        centerMv.y = -41;
        Mv refMv;
        refMv.as_int = 0;
        Mv bestPort, bestOracle;
        const std::uint32_t got = motion::exhaustiveMeshSearch(x, &refMv, &bestPort, 8, 4, 113, &landedSad8x8,
                                                               &landedSad8x8x4, &centerMv);
        const std::uint32_t want = oracleExhaustive(o, centerMv, refMv, 8, 4, 113, &bestOracle, &secondOracle);
        CHECK(got == want);
        CHECK(bestPort.x == 0);
        CHECK(bestPort.y == 0);
        // The pinned initial cost: SAD(center) + 1296 + 50*8*(0+0) = 1296 for
        // an exact match at the center.
        CHECK(got == 1296u);
    }
}

TEST_CASE("init3smotionCompensation builds the 89-site diamond ladder") {
    // av1me.c:162-188: ss[0] is the {0,0} center; then 11 steps of 8 sites,
    // len = 1024, 512, ..., 1; offset = mv.y * stride + mv.x; ss_count = 89;
    // searches_per_step = 8.
    motion::SearchSiteConfig cfg;
    motion::init3smotionCompensation(&cfg, 100);
    CHECK(cfg.ssCount == 89);
    CHECK(cfg.searchesPerStep == 8);
    CHECK(cfg.ss[0].mv.as_int == 0);
    CHECK(cfg.ss[0].offset == 0);

    // Independent transcription of the 8-site pattern per step (av1me.c:
    // 170-177 order: {0,-len} {0,len} {-len,0} {len,0} {-len,-len} {len,-len}
    // {-len,len} {len,len}).
    static const int pat[8][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    int len = motion::kMaxFirstStep;
    int count = 1;
    while (len > 0) {
        for (int i = 0; i < 8; ++i) {
            const int expectX = pat[i][0] * len;
            const int expectY = pat[i][1] * len;
            CHECK(cfg.ss[count].mv.x == expectX);
            CHECK(cfg.ss[count].mv.y == expectY);
            CHECK(cfg.ss[count].offset == expectY * 100 + expectX);
            ++count;
        }
        len /= 2;
    }
    CHECK(count == 89);
}

TEST_CASE("constructMeCandidateArrayMrpOff builds unipred plus bipred") {
    // motion_estimation.c:2334-2443: per PU (z_to_raster mapping) the two
    // unipred candidates (list order) plus one BI_PRED when both lists
    // survive; the me_mv slots are [pu*max_refs + (list ? max_l0 : 0) + 0];
    // the ref0_list/ref1_list writes truncate through their 1-bit fields
    // exactly as the reference's 24 writes do; total_me_candidate_index is
    // memset to 1 over number_of_pus and only the bipred branch overwrites.
    motion::PcsCtx pcs{};
    const std::uint8_t maxPus = 85;
    pcs.enableMe8x8 = 1;
    pcs.enableMe16x16 = 1;
    pcs.maxNumberOfPusPerSb = maxPus;
    pcs.maxCand = 23;
    pcs.maxRefs = 7;
    pcs.maxL0 = 4;
    std::vector<std::uint8_t> totalIdx(maxPus, 0xEE);
    std::vector<motion::MeCandidate> cands(maxPus * 23);
    std::vector<Mv> mvs(maxPus * 7);
    motion::MeSbResults sb{};
    sb.totalMeCandidateIndex = totalIdx.data();
    sb.meCandidateArray = cands.data();
    sb.meMvArray = mvs.data();
    pcs.sbResults = &sb;

    motion::MeCtx ctx{};
    ctx.numOfListToSearch = 2;
    ctx.numOfRefPicToSearch[0] = 1;
    ctx.numOfRefPicToSearch[1] = 1;
    ctx.searchResults[0][0].doRef = 1;
    ctx.searchResults[1][0].doRef = 1;
    ctx.pruneMeCandidatesTh = 0;
    ctx.useBestUnipredCandOnly = 0;
    for (int i = 0; i < 85; ++i) {
        ctx.pSbBestSad[0][0][i] = (std::uint32_t)(100 + i);
        ctx.pSbBestSad[1][0][i] = (std::uint32_t)(200 + i);
        ctx.pSbBestMv[0][0][i] = (std::uint32_t)(0x00110000u + i);  // y=17
        ctx.pSbBestMv[1][0][i] = (std::uint32_t)(0x00220000u + i);  // y=34
    }

    motion::constructMeCandidateArrayMrpOff(pcs, ctx, 2);

    // PU 0 (n_idx 0 -> raster 0): three candidates.
    CHECK(cands[0 * 23 + 0].direction == 0);
    CHECK(cands[0 * 23 + 0].refIdxL0 == 0);
    CHECK(cands[0 * 23 + 0].refIdxL1 == 0);
    CHECK(cands[0 * 23 + 0].ref0List == 0);
    CHECK(cands[0 * 23 + 0].ref1List == 0);  // the 24 write truncates to 0
    CHECK(cands[0 * 23 + 1].direction == 1);
    CHECK(cands[0 * 23 + 1].ref0List == 0);  // the 24 write truncates to 0
    CHECK(cands[0 * 23 + 1].ref1List == 1);
    CHECK(cands[0 * 23 + 2].direction == 2);  // BI_PRED
    CHECK(cands[0 * 23 + 2].ref0List == 0);
    CHECK(cands[0 * 23 + 2].ref1List == 1);
    CHECK(totalIdx[0] == 3);
    // The mv slots: list0 at [pu*7 + 0], list1 at [pu*7 + 4].
    CHECK(mvs[0 * 7 + 0].as_int == 0x00110000u);
    CHECK(mvs[0 * 7 + 4].as_int == 0x00220000u);
    // me_distortion[0] = min(100, 200) = 100.
    CHECK(ctx.meDistortion[0] == 100);
    // A later PU: n_idx 7 -> raster 9 (z_to_raster[7] = 9).
    CHECK(ctx.meDistortion[9] == 100 + 7);
    CHECK(cands[9 * 23 + 2].direction == 2);
    CHECK(totalIdx[9] == 3);
    // The memset(1) tail: PUs beyond the touched range stay 1... every PU
    // with a bipred gets 3; PUs 0..84 all have bipred here -> all 3.
    for (int i = 0; i < 85; ++i) {
        CHECK(totalIdx[i] == 3);
    }
}

TEST_CASE("constructMeCandidateArrayMrpOff prunes and honors the flags") {
    // The list1 do_ref = 0 path: one unipred candidate, no bipred, total
    // stays at the memset value 1; the use_best_unipred_cand_only path
    // stores the losing list's MV but adds no second unipred; the pruning
    // threshold clears blk_do_ref and kills the bipred.
    motion::PcsCtx pcs{};
    pcs.enableMe8x8 = 1;
    pcs.enableMe16x16 = 1;
    pcs.maxNumberOfPusPerSb = 85;
    pcs.maxCand = 23;
    pcs.maxRefs = 7;
    pcs.maxL0 = 4;
    std::vector<std::uint8_t> totalIdx(85, 0xEE);
    std::vector<motion::MeCandidate> cands(85 * 23);
    std::vector<Mv> mvs(85 * 7);
    motion::MeSbResults sb{totalIdx.data(), cands.data(), mvs.data()};
    pcs.sbResults = &sb;

    motion::MeCtx ctx{};
    ctx.numOfListToSearch = 2;
    ctx.numOfRefPicToSearch[0] = 1;
    ctx.numOfRefPicToSearch[1] = 1;
    ctx.pruneMeCandidatesTh = 0;
    ctx.useBestUnipredCandOnly = 0;
    for (int i = 0; i < 85; ++i) {
        ctx.pSbBestSad[0][0][i] = 100;
        ctx.pSbBestSad[1][0][i] = 300;
        ctx.pSbBestMv[0][0][i] = 0x00110000u;
        ctx.pSbBestMv[1][0][i] = 0x00220000u;
    }

    // Case A: list1 pruned via do_ref = 0.
    ctx.searchResults[0][0].doRef = 1;
    ctx.searchResults[1][0].doRef = 0;
    motion::constructMeCandidateArrayMrpOff(pcs, ctx, 2);
    CHECK(cands[0].direction == 0);
    CHECK(totalIdx[0] == 1);  // the memset value; no bipred overwrote it
    CHECK(ctx.meDistortion[0] == 100);

    // Case B: use_best_unipred_cand_only with list0 the winner.
    for (int i = 0; i < 85; ++i) {
        totalIdx[i] = 0xEE;
        cands[i] = motion::MeCandidate{};
    }
    ctx.searchResults[1][0].doRef = 1;
    ctx.useBestUnipredCandOnly = 1;
    motion::constructMeCandidateArrayMrpOff(pcs, ctx, 2);
    // List0 unipred only; list1's MV still stored at its slot.
    CHECK(cands[0].direction == 0);
    CHECK(mvs[0 * 7 + 0].as_int == 0x00110000u);
    CHECK(mvs[0 * 7 + 4].as_int == 0x00220000u);
    // The bipred branch still fires (blk_do_ref survived for both).
    CHECK(totalIdx[0] == 2);  // unipred + bipred
    CHECK(cands[0 * 23 + 1].direction == 2);

    // Case C: the pruning threshold kills the losing list AND the bipred.
    for (int i = 0; i < 85; ++i) {
        totalIdx[i] = 0xEE;
        cands[i] = motion::MeCandidate{};
    }
    ctx.useBestUnipredCandOnly = 0;
    ctx.pruneMeCandidatesTh = 1;  // (300-100)*100 = 20000 > 100*1
    motion::constructMeCandidateArrayMrpOff(pcs, ctx, 2);
    CHECK(cands[0].direction == 0);
    CHECK(cands[0 * 23 + 1].direction == 0);  // no bipred (blk_do_ref[1] = 0)
    CHECK(totalIdx[0] == 1);
}

TEST_CASE("constructMeCandidateArraySingleRef writes the one-candidate shape") {
    // motion_estimation.c:2445-2496: me_distortion from list0 only; one
    // unipred candidate (direction 0) when do_ref; the MV slot is
    // [pu*max_refs + 0]; a do_ref = 0 walk writes nothing beyond the memset.
    motion::PcsCtx pcs{};
    pcs.enableMe8x8 = 0;  // the WO_16X16 shape: only the first 5 PUs used
    pcs.enableMe16x16 = 0;
    pcs.maxNumberOfPusPerSb = 85;
    pcs.maxCand = 23;
    pcs.maxRefs = 7;
    pcs.maxL0 = 4;
    std::vector<std::uint8_t> totalIdx(85, 0xEE);
    std::vector<motion::MeCandidate> cands(85 * 23);
    std::vector<Mv> mvs(85 * 7);
    motion::MeSbResults sb{totalIdx.data(), cands.data(), mvs.data()};
    pcs.sbResults = &sb;

    motion::MeCtx ctx{};
    ctx.numOfListToSearch = 1;
    ctx.numOfRefPicToSearch[0] = 1;
    ctx.numOfRefPicToSearch[1] = 0;
    ctx.searchResults[0][0].doRef = 1;
    for (int i = 0; i < 85; ++i) {
        ctx.pSbBestSad[0][0][i] = 555;
        ctx.pSbBestMv[0][0][i] = 0x00030007u;
    }

    motion::constructMeCandidateArraySingleRef(pcs, ctx, 1);
    // The memset(1) covers number_of_pus = 5 (WO_16X16).
    for (int i = 0; i < 5; ++i) {
        CHECK(totalIdx[i] == 1);
    }
    CHECK(totalIdx[5] == 0xEE);  // beyond number_of_pus untouched
    // PU 0: raster 0; the candidate at slot 0.
    CHECK(cands[0].direction == 0);
    CHECK(cands[0].refIdxL0 == 0);
    CHECK(cands[0].ref0List == 0);
    CHECK(cands[0].ref1List == 0);
    CHECK(mvs[0 * 7 + 0].as_int == 0x00030007u);
    CHECK(ctx.meDistortion[0] == 555);
    // PU 4 (n_idx 4 -> raster 4).
    CHECK(ctx.meDistortion[4] == 555);
    CHECK(mvs[4 * 7 + 0].as_int == 0x00030007u);

    // do_ref = 0: the memset still ran, but no candidate/MV writes.
    ctx.searchResults[0][0].doRef = 0;
    for (int i = 0; i < 85 * 23; ++i) {
        cands[i].direction = 3;  // the 2-bit field's sentinel
    }
    motion::constructMeCandidateArraySingleRef(pcs, ctx, 1);
    CHECK(totalIdx[0] == 1);
    CHECK(cands[0].direction == 3);
}

TEST_CASE("constructMeCandidateArray enumerates the MRP-ON candidate set") {
    // motion_estimation.c:2498-2634: pu_index = (n_idx > 4) ? z_to_raster :
    // n_idx; best distortion over every allowed (list, ref); the unipred
    // walk list-major then ref; the three bipred sets (cross-list, the
    // list0 (0,k) pairs, the (BWD, ALT) list1 pair) with the only_l_bwd
    // gate; total_me_candidate_index = the candidate count.
    motion::PcsCtx pcs{};
    pcs.enableMe8x8 = 1;
    pcs.enableMe16x16 = 1;
    pcs.maxNumberOfPusPerSb = 85;
    pcs.maxCand = 23;
    pcs.maxRefs = 7;
    pcs.maxL0 = 4;
    std::vector<std::uint8_t> totalIdx(85, 0xEE);
    std::vector<motion::MeCandidate> cands(85 * 23);
    std::vector<Mv> mvs(85 * 7);
    motion::MeSbResults sb{totalIdx.data(), cands.data(), mvs.data()};
    pcs.sbResults = &sb;

    motion::MeCtx ctx{};
    ctx.numOfListToSearch = 2;
    ctx.numOfRefPicToSearch[0] = 2;
    ctx.numOfRefPicToSearch[1] = 2;
    ctx.pruneMeCandidatesTh = 0;
    for (int l = 0; l < 2; ++l) {
        for (int r = 0; r < 2; ++r) {
            ctx.searchResults[l][r].doRef = 1;
            for (int n = 0; n < 85; ++n) {
                ctx.pSbBestSad[l][r][n] = (std::uint32_t)(100 * l + 10 * r + 5 + n);
                ctx.pSbBestMv[l][r][n] = (std::uint32_t)(0x00A00000u * (l + 1) + r + 1);
            }
        }
    }
    pcs.onlyLBwd = false;

    motion::constructMeCandidateArray(pcs, ctx, 2);

    // PU 0: unipred (0,0) (0,1) (1,0) (1,1), then cross-list bipred
    // (0,0) (0,1) (1,0) (1,1), then the list0 (0,1) pair -> total 9.
    CHECK(cands[0].direction == 0);
    CHECK(cands[0].refIdxL0 == 0);
    CHECK(cands[0].refIdxL1 == 0);
    CHECK(cands[1].direction == 0);
    CHECK(cands[1].refIdxL0 == 1);
    CHECK(cands[2].direction == 1);
    CHECK(cands[2].refIdxL0 == 0);
    CHECK(cands[3].direction == 1);
    CHECK(cands[3].refIdxL1 == 1);
    CHECK(cands[4].direction == 2);
    CHECK(cands[4].refIdxL0 == 0);
    CHECK(cands[4].refIdxL1 == 0);
    CHECK(cands[5].direction == 2);
    CHECK(cands[5].refIdxL0 == 0);
    CHECK(cands[5].refIdxL1 == 1);
    CHECK(cands[6].direction == 2);
    CHECK(cands[6].refIdxL0 == 1);
    CHECK(cands[7].direction == 2);
    CHECK(cands[7].refIdxL1 == 1);
    CHECK(cands[8].direction == 2);  // the list0 (0,1) pair
    CHECK(cands[8].refIdxL0 == 0);
    CHECK(cands[8].refIdxL1 == 1);
    CHECK(cands[8].ref0List == 0);
    CHECK(cands[8].ref1List == 0);
    CHECK(totalIdx[0] == 9);
    // The MV slots: [pu*7 + (list ? 4 : 0) + ref].
    CHECK(mvs[0 * 7 + 0].as_int == 0x00A00000u + 1);
    CHECK(mvs[0 * 7 + 1].as_int == 0x00A00000u + 2);
    CHECK(mvs[0 * 7 + 4].as_int == 0x01400000u + 1);
    CHECK(mvs[0 * 7 + 5].as_int == 0x01400000u + 2);
    // me_distortion[0] = min over the four = 5.
    CHECK(ctx.meDistortion[0] == 5);

    // The pu mapping: raster 6 is fed by n_idx 6 (z_to_raster[6] = 6, the
    // identity run reaches index 6); n_idx 1 -> pu 1 (identity for
    // n_idx <= 4).
    CHECK(ctx.meDistortion[6] == 5 + 6);
    CHECK(ctx.meDistortion[1] == 5 + 1);

    // only_l_bwd = 1: only the (0,0) cross-list bipred; sets 2/3 skipped.
    for (int i = 0; i < 85 * 23; ++i) {
        cands[i] = motion::MeCandidate{};
    }
    for (int i = 0; i < 85; ++i) {
        totalIdx[i] = 0xEE;
    }
    pcs.onlyLBwd = true;
    motion::constructMeCandidateArray(pcs, ctx, 2);
    CHECK(totalIdx[0] == 5);  // 4 unipred + (0,0) bipred
    CHECK(cands[4].direction == 2);
    CHECK(cands[4].refIdxL0 == 0);
    CHECK(cands[4].refIdxL1 == 0);
    CHECK(cands[5].direction == 0);  // no (0,1) list0 pair

    // The (BWD, ALT) set: list1 with 3 refs and do_ref on 0 and 2.
    for (int i = 0; i < 85 * 23; ++i) {
        cands[i] = motion::MeCandidate{};
    }
    for (int i = 0; i < 85; ++i) {
        totalIdx[i] = 0xEE;
    }
    pcs.onlyLBwd = false;
    ctx.numOfRefPicToSearch[1] = 3;
    ctx.searchResults[1][2].doRef = 1;
    ctx.pSbBestSad[1][2][0] = 3;  // the new best (below 5)
    ctx.pSbBestMv[1][2][0] = 0x00300000u;
    motion::constructMeCandidateArray(pcs, ctx, 2);
    // Unipred now 5 (l0r0, l0r1, l1r0, l1r1, l1r2); cross-list bipred 6;
    // list0 pair 1; (BWD, ALT) 1 -> total 13.
    CHECK(totalIdx[0] == 13);
    CHECK(cands[12].direction == 2);
    CHECK(cands[12].refIdxL0 == 0);
    CHECK(cands[12].refIdxL1 == 2);
    CHECK(cands[12].ref0List == 1);
    CHECK(cands[12].ref1List == 1);
    CHECK(ctx.meDistortion[0] == 3);

    // The pruning: a huge th kills the far candidates but the (0,0) pairs
    // survive; best = 50.
    for (int i = 0; i < 85 * 23; ++i) {
        cands[i] = motion::MeCandidate{};
    }
    for (int i = 0; i < 85; ++i) {
        totalIdx[i] = 0xEE;
    }
    ctx.pruneMeCandidatesTh = 1;  // dev*100 > 3 -> any sad above 3 pruned
    motion::constructMeCandidateArray(pcs, ctx, 2);
    // Only the sad-50 candidate (l1r2) survives the unipred walk; bipred:
    // every pair needs both do_ref alive - the pruned refs have
    // blk_do_ref = 0, so only pairs among {l1r2} survive... none (list0
    // refs are all pruned). Total = 1.
    CHECK(totalIdx[0] == 1);
    CHECK(cands[0].direction == 1);
    CHECK(cands[0].refIdxL0 == 2);
}
