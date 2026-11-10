// l6_pipeline/rdlite.h - the RE5 RD-lite decision context surface.
// Includes l16_rdo/rate_est.h ISOLATED: rate_est.h's #define set contains
// TX_SIZES/TX_4X4/TX_16X16/TX_32X32/PLANE_TYPES/FILTER_INTRA_MODES, which
// poison the l7_entropy ENUM members of the same names (the rate_est.h
// :518-:520 note records the hazard for l3; the same hazard hits l7 here),
// and several shared-constant macros are redefined with a different token
// spelling (BR_CDF_SIZE "(4)" vs "4"), which fires C4005 under /W4 /WX.
// So: include entropy.h first, rate_est.h under a C4005 suppression, then
// #undef exactly the six enum poisons (the l7 enumerators become visible
// again; the coincident-value l16 macro redefinitions that remain active
// are harmless), and capture the one l16 constant the l6 surface consumes
// as a constexpr BEFORE the undef.
#pragma once

#include <cstdint>
#include <entropy.h>

#pragma warning(push)
#pragma warning(disable : 4005)
#include <rate_est.h>
#pragma warning(pop)

namespace l6_rdlite {

// FILTER_INTRA_MODES (rate_est.h), captured before the undef: the
// IntraFastCandidate::filterIntraMode "none" sentinel.
constexpr std::int32_t kFilterIntraModes = FILTER_INTRA_MODES;

}  // namespace l6_rdlite

#undef TX_SIZES
#undef TX_4X4
#undef TX_16X16
#undef TX_32X32
#undef PLANE_TYPES
#undef FILTER_INTRA_MODES

namespace pipeline {

// RE5: the frame-level RD-lite decision context: the RE1 rate-estimation
// surface (one per frame) + the named lambda.
// RE7: the frame's qindex is carried so the per-candidate tx-type
// derivation applies the decoder's q0 gate (dav1d recon_tmpl.c:363-365,
// aom decodemv.c av1_read_tx_type:638-640: qidx == 0 -> DCT_DCT implicit).
struct RdliteFrameCtx {
    l16_rdo::MdRateEstimationContext me;
    std::uint32_t lambda;
    std::int32_t qindex;
};

// Derives me from the frame's l7 context (initDefaultEcFrameContext at the
// frame's qindex - the same cdfs the emission uses) via the landed
// l16_rdo::estimateSyntaxRate + estimateCoefficientsRate +
// estimateIntraInterRate (the brief's init pair).
void initRdliteFrameCtx(RdliteFrameCtx* ctx, std::int32_t qindex);

}  // namespace pipeline
