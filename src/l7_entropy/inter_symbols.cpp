// A5E - the INTER symbol + context surface of entropy_coding.c (A5).
// Reference: third_party/SVT-AV1/Source/Lib/Codec/entropy_coding.c.
// Struct-free carriers per the RE8A DeblockContext precedent; the neighbor
// access is pointer-shaped exactly like xd->above_mbmi/left_mbmi.
// Helper spans consumed by the ported spans are duplicated locally (l7
// cannot link l5/l16/l17 - the l7-l13 duplication precedent):
//   svt_aom_get_relative_dist_enc (inter_prediction.c:276-:285);
//   has_second_ref (block_structures.h:106-:111).

#include "inter_symbols.h"

#include <cstdlib>
#include <cstring>

// The AOM_CDF initializer macros (cabac_context_model.h:62-:78) - local
// copies for the .inc includes below (the entropy.cpp:503-:517 pattern;
// the includes are the mechanical cabac_context_model.c extracts, no
// hand-transcribed values).
#define AOM_EXPAND_LIST(x) x
#define AOM_CDF2(a0) AOM_ICDF(a0), 0
#define AOM_CDF3(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF2(__VA_ARGS__))
#define AOM_CDF4(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF3(__VA_ARGS__))
#define AOM_CDF5(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF4(__VA_ARGS__))
#define AOM_CDF6(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF5(__VA_ARGS__))
#define AOM_CDF7(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF6(__VA_ARGS__))
#define AOM_CDF8(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF7(__VA_ARGS__))
#define AOM_CDF9(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF8(__VA_ARGS__))
#define AOM_CDF10(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF9(__VA_ARGS__))
#define AOM_CDF11(a0, ...) AOM_ICDF(a0), AOM_EXPAND_LIST(AOM_CDF10(__VA_ARGS__))

namespace entropy {

namespace {

// The default inter tables (the mechanical extracts).
static const AomCdfProb newmv_cdf_default[NEWMV_MODE_CONTEXTS][CDF_SIZE(2)] = {
#include "inter_newmv_cdfs_default.inc"
};
static const AomCdfProb zeromv_cdf_default[GLOBALMV_MODE_CONTEXTS][CDF_SIZE(2)] = {
#include "inter_zeromv_cdfs_default.inc"
};
static const AomCdfProb refmv_cdf_default[REFMV_MODE_CONTEXTS][CDF_SIZE(2)] = {
#include "inter_refmv_cdfs_default.inc"
};
static const AomCdfProb drl_cdf_default[DRL_MODE_CONTEXTS][CDF_SIZE(2)] = {
#include "inter_drl_cdfs_default.inc"
};
static const AomCdfProb
    inter_compound_mode_cdf_default[INTER_MODE_CONTEXTS][CDF_SIZE(INTER_COMPOUND_MODES)] = {
#include "inter_compound_mode_cdfs_default.inc"
};
static const AomCdfProb motion_mode_cdf_default[BLOCK_SIZES_ALL][CDF_SIZE(MOTION_MODES)] = {
#include "inter_motion_mode_cdfs_default.inc"
};
static const AomCdfProb obmc_cdf_default[BLOCK_SIZES_ALL][CDF_SIZE(2)] = {
#include "inter_obmc_cdfs_default.inc"
};
static const AomCdfProb comp_inter_cdf_default[COMP_INTER_CONTEXTS][CDF_SIZE(2)] = {
#include "inter_comp_inter_cdfs_default.inc"
};
static const AomCdfProb single_ref_cdf_default[REF_CONTEXTS][SINGLE_REFS - 1][CDF_SIZE(2)] = {
#include "inter_single_ref_cdfs_default.inc"
};
static const AomCdfProb comp_ref_type_cdf_default[COMP_REF_TYPE_CONTEXTS][CDF_SIZE(2)] = {
#include "inter_comp_ref_type_cdfs_default.inc"
};
static const AomCdfProb uni_comp_ref_cdf_default[UNI_COMP_REF_CONTEXTS][UNIDIR_COMP_REFS - 1]
                                                [CDF_SIZE(2)] = {
#include "inter_uni_comp_ref_cdfs_default.inc"
};
static const AomCdfProb comp_ref_cdf_default[REF_CONTEXTS][FWD_REFS - 1][CDF_SIZE(2)] = {
#include "inter_comp_ref_cdfs_default.inc"
};
static const AomCdfProb comp_bwdref_cdf_default[REF_CONTEXTS][BWD_REFS - 1][CDF_SIZE(2)] = {
#include "inter_comp_bwdref_cdfs_default.inc"
};
static const AomCdfProb compound_index_cdf_default[COMP_INDEX_CONTEXTS][CDF_SIZE(2)] = {
#include "inter_compound_idx_cdfs_default.inc"
};
static const AomCdfProb comp_group_idx_cdf_default[COMP_GROUP_IDX_CONTEXTS][CDF_SIZE(2)] = {
#include "inter_comp_group_idx_cdfs_default.inc"
};
static const AomCdfProb skip_mode_cdfs_default[SKIP_MODE_CONTEXTS][CDF_SIZE(2)] = {
#include "inter_skip_mode_cdfs_default.inc"
};
static const AomCdfProb intra_inter_cdf_default[INTRA_INTER_CONTEXTS][CDF_SIZE(2)] = {
#include "inter_intra_inter_cdfs_default.inc"
};
static const AomCdfProb
    switchable_interp_cdf_default[SWITCHABLE_FILTER_CONTEXTS][CDF_SIZE(SWITCHABLE_FILTERS)] = {
#include "inter_switchable_interp_cdfs_default.inc"
};
static const NmvContext nmv_context_default = {
#include "inter_nmv_context_default.inc"
};

}  // namespace

// initDefaultInterFrameContext - the inter families' COPY_CDF extract of
// svt_aom_init_mode_probs (cabac_context_model.c:734-:788 + the
// fc->nmvc = default_nmv_context tail :791).
void initDefaultInterFrameContext(InterFc* fc) {
    std::memcpy(fc->newmv_cdf, newmv_cdf_default, sizeof(newmv_cdf_default));
    std::memcpy(fc->zeromv_cdf, zeromv_cdf_default, sizeof(zeromv_cdf_default));
    std::memcpy(fc->refmv_cdf, refmv_cdf_default, sizeof(refmv_cdf_default));
    std::memcpy(fc->drl_cdf, drl_cdf_default, sizeof(drl_cdf_default));
    std::memcpy(fc->inter_compound_mode_cdf, inter_compound_mode_cdf_default,
                sizeof(inter_compound_mode_cdf_default));
    std::memcpy(fc->motion_mode_cdf, motion_mode_cdf_default, sizeof(motion_mode_cdf_default));
    std::memcpy(fc->obmc_cdf, obmc_cdf_default, sizeof(obmc_cdf_default));
    std::memcpy(fc->comp_inter_cdf, comp_inter_cdf_default, sizeof(comp_inter_cdf_default));
    std::memcpy(fc->single_ref_cdf, single_ref_cdf_default, sizeof(single_ref_cdf_default));
    std::memcpy(fc->comp_ref_type_cdf, comp_ref_type_cdf_default,
                sizeof(comp_ref_type_cdf_default));
    std::memcpy(fc->uni_comp_ref_cdf, uni_comp_ref_cdf_default,
                sizeof(uni_comp_ref_cdf_default));
    std::memcpy(fc->comp_ref_cdf, comp_ref_cdf_default, sizeof(comp_ref_cdf_default));
    std::memcpy(fc->comp_bwdref_cdf, comp_bwdref_cdf_default, sizeof(comp_bwdref_cdf_default));
    std::memcpy(fc->compound_index_cdf, compound_index_cdf_default,
                sizeof(compound_index_cdf_default));
    std::memcpy(fc->comp_group_idx_cdf, comp_group_idx_cdf_default,
                sizeof(comp_group_idx_cdf_default));
    std::memcpy(fc->skip_mode_cdfs, skip_mode_cdfs_default, sizeof(skip_mode_cdfs_default));
    std::memcpy(fc->intra_inter_cdf, intra_inter_cdf_default, sizeof(intra_inter_cdf_default));
    std::memcpy(fc->switchable_interp_cdf, switchable_interp_cdf_default,
                sizeof(switchable_interp_cdf_default));
    fc->nmvc = nmv_context_default;
}
namespace {

// inter_prediction.c:276-:285 svt_aom_get_relative_dist_enc, verbatim
// (l17_inter cannot be linked; the l7-l13 duplication precedent).
inline int getRelativeDistEnc(int enable_order_hint, int order_hint_bits, int ref_hint,
                              int order_hint) {
    if (!enable_order_hint) {
        return 0;
    }
    int diff = ref_hint - order_hint;
    const int m = 1 << (order_hint_bits - 1);
    diff = (diff & (m - 1)) - (diff & m);
    return diff;
}

// block_structures.h:106-:111 has_second_ref, verbatim (the
// CONFIG_ENABLE_INTER_COMPOUND guard is live at 1 in the pinned build).
inline int hasSecondRef(const InterMbMi* mi) { return mi->ref_frame[1] > INTRA_FRAME; }

// definitions.h:628-:644 get_msb, the portable body (MSVC branch);
// svt_log2f_safe(x) = get_msb((x) | 1) (definitions.h:591).
inline int getMsb(std::uint32_t n) {
    int log = 0;
    std::uint32_t value = n;
    for (int i = 4; i >= 0; --i) {
        const int shift = (1 << i);
        const std::uint32_t x = value >> shift;
        if (x != 0) {
            value = x;
            log += shift;
        }
    }
    return log;
}

// md_rate_estimation.c:366-:376 mv_class_base + svt_av1_get_mv_class,
// verbatim (svt_log2f_safe = get_msb(x|1), definitions.h:591; the l16_rdo
// twin cannot be linked - the l7-l13 duplication precedent).
inline std::int32_t mvClassBase(MvClassType c) { return c ? CLASS0_SIZE << (c + 2) : 0; }
inline MvClassType getMvClassLocal(std::int32_t z, std::int32_t* offset) {
    const MvClassType c =
        (z >= CLASS0_SIZE * 4096) ? MV_CLASS_10 : (MvClassType)getMsb((std::uint32_t)(z >> 3) | 1);
    if (offset) {
        *offset = z - mvClassBase(c);
    }
    return c;
}

// block_structures.h:124-:127 is_inter_block, verbatim (the
// CONFIG_ENABLE_INTRA_BC term is live at 1; the port's carrier has no
// use_intrabc flag, so the term is the pinned 0 - named deviation).
inline int isInterBlock(const InterMbMi* mi) { return mi->ref_frame[0] > INTRA_FRAME; }

// block_structures.h:113-:117 has_uni_comp_refs, verbatim.
inline int hasUniCompRefs(const InterMbMi* mi) {
    return hasSecondRef(mi) &&
        !((mi->ref_frame[0] >= BWDREF_FRAME) ^ (mi->ref_frame[1] >= BWDREF_FRAME));
}

// inter_prediction.h:378-:381 is_motion_variation_allowed_bsize, verbatim.
inline int isMotionVariationAllowedBsize(BlockSize bsize) {
    return (blockSizeWide[bsize] >= 8 && blockSizeHigh[bsize] >= 8);
}

// inter_prediction.h:382-:388 is_global_mv_block, verbatim (the
// CONFIG_ENABLE_GLOBAL_MOTION guard is live at 1 in the pinned build).
inline int isGlobalMvBlock(PredictionMode mode, BlockSize bsize, TransformationType type) {
    return (mode == GLOBALMV || mode == GLOBAL_GLOBALMV) && type > TRANSLATION &&
        isMotionVariationAllowedBsize(bsize);
}

// definitions.h:1610-:1612 is_inter_singleref_mode, verbatim.
inline int isInterSinglerefMode(PredictionMode mode) {
    return mode >= SINGLE_INTER_MODE_START && mode < SINGLE_INTER_MODE_END;
}

// inter_prediction.h:391-:393 have_nearmv_in_inter_mode, verbatim.
inline std::uint32_t haveNearmvInInterMode(PredictionMode mode) {
    return (mode == NEARMV || mode == NEAR_NEARMV || mode == NEAR_NEWMV || mode == NEW_NEARMV);
}

// inter_prediction.h:255-:257 is_comp_ref_allowed, verbatim.
inline int isCompRefAllowed(BlockSize bsize) {
    return AOMMIN(blockSizeWide[bsize], blockSizeHigh[bsize]) >= 8;
}

// filter.h:60-:62 av1_extract_interp_filter, verbatim.
inline InterpFilter extractInterpFilter(std::uint32_t filters, int x_filter) {
    return static_cast<InterpFilter>((filters >> (x_filter ? 16 : 0)) & 0xffff);
}

// common_utils.c:292-:293 mi_size_wide / mi_size_high, verbatim.
const std::uint8_t miSizeWide[BLOCK_SIZES_ALL] = {1, 1, 2, 2, 2, 4, 4, 4, 8, 8, 8, 16, 16, 16, 32, 32,
                                                  1, 4, 2, 8, 4, 16};
const std::uint8_t miSizeHigh[BLOCK_SIZES_ALL] = {1, 2, 1, 2, 4, 2, 4, 8, 4, 8, 16, 8, 16, 32, 16, 32,
                                                  4, 1, 8, 2, 16, 4};

// definitions.h:1603-:1608 is_inter_compound_mode, verbatim (the
// CONFIG_ENABLE_INTER_COMPOUND guard is live at 1 in the pinned build).
inline int isInterCompoundMode(PredictionMode mode) {
    return mode >= NEAREST_NEARESTMV && mode <= NEW_NEWMV;
}

}  // namespace

// svt_aom_get_comp_index_context_enc :52-:78
int getCompIndexContextEnc(const InterMbD* xd, int cur_frame_index, int bck_frame_index,
                           int fwd_frame_index, int enable_order_hint, int order_hint_bits) {
    const int fwd = abs(getRelativeDistEnc(enable_order_hint, order_hint_bits, fwd_frame_index,
                                           cur_frame_index));
    const int bck = abs(getRelativeDistEnc(enable_order_hint, order_hint_bits, cur_frame_index,
                                           bck_frame_index));

    const InterMbMi* const above_mi = xd->above_mi;
    const InterMbMi* const left_mi = xd->left_mi;
    const int offset = fwd == bck;
    int above_ctx = 0, left_ctx = 0;

    if (above_mi) {
        if (hasSecondRef(above_mi)) {
            above_ctx = above_mi->compound_idx;
        } else if (above_mi->ref_frame[0] == ALTREF_FRAME) {
            above_ctx = 1;
        }
    }

    if (left_mi) {
        if (hasSecondRef(left_mi)) {
            left_ctx = left_mi->compound_idx;
        } else if (left_mi->ref_frame[0] == ALTREF_FRAME) {
            left_ctx = 1;
        }
    }
    return above_ctx + left_ctx + 3 * offset;
}

// svt_aom_get_comp_group_idx_context_enc :80-:99
int getCompGroupIdxContextEnc(const InterMbD* xd) {
    const InterMbMi* const above_mi = xd->above_mi;
    const InterMbMi* const left_mi = xd->left_mi;
    int above_ctx = 0, left_ctx = 0;
    if (above_mi) {
        if (hasSecondRef(above_mi)) {
            above_ctx = above_mi->comp_group_idx;
        } else if (above_mi->ref_frame[0] == ALTREF_FRAME) {
            above_ctx = 3;
        }
    }
    if (left_mi) {
        if (hasSecondRef(left_mi)) {
            left_ctx = left_mi->comp_group_idx;
        } else if (left_mi->ref_frame[0] == ALTREF_FRAME) {
            left_ctx = 3;
        }
    }
    return AOMMIN(5, above_ctx + left_ctx);
}

// av1_get_skip_mode_context :1097-:1103
int getSkipModeContext(const InterMbD* xd) {
    const InterMbMi* const above_mi = xd->above_mi;
    const InterMbMi* const left_mi = xd->left_mi;
    const int above_skip_mode = above_mi ? above_mi->skip_mode : 0;
    const int left_skip_mode = left_mi ? left_mi->skip_mode : 0;
    return above_skip_mode + left_skip_mode;
}

// encode_skip_mode_av1 :1109-:1115
void writeSkipMode(AomWriter* w, InterFc* fc, const InterMbD* xd, int skip_mode_flag) {
    const int context_index = getSkipModeContext(xd);
    odEcWriteSymbol(w, skip_mode_flag ? 1 : 0, fc->skip_mode_cdfs[context_index], 2);
}

// svt_av1_get_intra_inter_context :1126-:1141
int getIntraInterContext(const InterMbD* xd) {
    const InterMbMi* const above_mbmi = xd->above_mi;
    const InterMbMi* const left_mbmi = xd->left_mi;
    const int has_above = xd->up_available;
    const int has_left = xd->left_available;

    if (has_above && has_left) {  // both edges available
        const int above_intra = !isInterBlock(above_mbmi);
        const int left_intra = !isInterBlock(left_mbmi);
        return left_intra && above_intra ? 3 : left_intra || above_intra;
    } else if (has_above || has_left) {  // one edge available
        return 2 * !isInterBlock(has_above ? above_mbmi : left_mbmi);
    } else {
        return 0;
    }
}

// write_is_inter :1147-:1151
void writeIsInter(AomWriter* w, InterFc* fc, const InterMbD* xd, int is_inter) {
    const int ctx = getIntraInterContext(xd);
    odEcWriteSymbol(w, is_inter, fc->intra_inter_cdf[ctx], 2);
}

// svt_aom_motion_mode_allowed :1159-:1195
MotionMode motionModeAllowed(int is_motion_mode_switchable, int force_integer_mv,
                             int allow_warped_motion, int gm_type_rf0,
                             std::uint8_t num_proj_ref, std::uint32_t overlappable_neighbors,
                             BlockSize bsize, std::int32_t rf0, std::int32_t rf1,
                             PredictionMode mode) {
    (void)rf0;  // named deviation: the reference reads gm_type for rf0 only
    if (!is_motion_mode_switchable) {
        return SIMPLE_TRANSLATION;
    }

    if (force_integer_mv == 0) {
        const TransformationType gm_type = (TransformationType)gm_type_rf0;
        if (isGlobalMvBlock(mode, bsize, gm_type)) {
            return SIMPLE_TRANSLATION;
        }
    }
    if (isMotionVariationAllowedBsize(bsize) && isInterSinglerefMode(mode) &&
        rf1 != INTRA_FRAME && !(rf1 > INTRA_FRAME)) {  // is_motion_variation_allowed_compound
        if (overlappable_neighbors == 0) {
            return SIMPLE_TRANSLATION;
        }

        if (allow_warped_motion && num_proj_ref >= 1) {
            if (force_integer_mv) {
                return OBMC_CAUSAL;
            }
            return WARPED_CAUSAL;
        }
        return OBMC_CAUSAL;
    } else {
        return SIMPLE_TRANSLATION;
    }
}

// write_motion_mode :1201-:1217
void writeMotionMode(AomWriter* w, InterFc* fc, BlockSize bsize, PredictionMode mode,
                     std::uint8_t num_proj_ref, std::uint32_t overlappable_neighbors,
                     std::int32_t rf0, std::int32_t rf1, MotionMode motion_mode,
                     int is_motion_mode_switchable, int force_integer_mv, int allow_warped_motion,
                     int gm_type_rf0) {
    const MotionMode last_motion_mode_allowed =
        motionModeAllowed(is_motion_mode_switchable, force_integer_mv, allow_warped_motion,
                          gm_type_rf0, num_proj_ref, overlappable_neighbors, bsize, rf0, rf1,
                          mode);
    switch (last_motion_mode_allowed) {
    case SIMPLE_TRANSLATION:
        break;
    case OBMC_CAUSAL:
        odEcWriteSymbol(w, motion_mode == OBMC_CAUSAL, fc->obmc_cdf[bsize], 2);
        break;
    default:
        odEcWriteSymbol(w, motion_mode, fc->motion_mode_cdf[bsize], MOTION_MODES);
    }
}

// write_inter_mode :1386-:1404
void writeInterMode(AomWriter* w, InterFc* fc, PredictionMode mode, std::int16_t mode_ctx) {
    const std::int16_t newmv_ctx = mode_ctx & NEWMV_CTX_MASK;
    odEcWriteSymbol(w, mode != NEWMV, fc->newmv_cdf[newmv_ctx], 2);

    if (mode != NEWMV) {
        const std::int16_t zeromv_ctx = (mode_ctx >> GLOBALMV_OFFSET) & GLOBALMV_CTX_MASK;
        odEcWriteSymbol(w, mode != GLOBALMV, fc->zeromv_cdf[zeromv_ctx], 2);

        if (mode != GLOBALMV) {
            const std::int16_t refmv_ctx = (mode_ctx >> REFMV_OFFSET) & REFMV_CTX_MASK;
            odEcWriteSymbol(w, mode != NEARESTMV, fc->refmv_cdf[refmv_ctx], 2);
        }
    }
}

// write_drl_idx :1407-:1443
void writeDrlIdx(AomWriter* w, InterFc* fc, PredictionMode mode, const std::int8_t drl_ctx[2],
                 const std::int8_t drl_ctx_near[2], std::uint8_t drl_index) {
    const std::int32_t new_mv = mode == NEWMV || mode == NEW_NEWMV;
    if (new_mv) {
        for (int idx = 0; idx < 2; ++idx) {
            if (drl_ctx[idx] != -1) {
                const std::uint8_t ctx = (std::uint8_t)drl_ctx[idx];
                odEcWriteSymbol(w, drl_index != idx, fc->drl_cdf[ctx], 2);
                if (drl_index == idx) {
                    return;
                }
            }
        }
        return;
    }

    if (haveNearmvInInterMode(mode)) {
        // TODO(jingning): Temporary solution to compensate the NEARESTMV offset.
        for (int idx = 1; idx < 3; ++idx) {
            if (drl_ctx_near[idx - 1] != -1) {
                const std::uint8_t ctx = (std::uint8_t)drl_ctx_near[idx - 1];
                odEcWriteSymbol(w, drl_index != (idx - 1), fc->drl_cdf[ctx], 2);
                if (drl_index == (idx - 1)) {
                    return;
                }
            }
        }
        return;
    }
}

// encode_mv_component :1445-:1481
void encodeMvComponent(AomWriter* w, std::int32_t comp, NmvComponent* mvcomp,
                       MvSubpelPrecision precision) {
    std::int32_t offset;
    const std::int32_t sign = comp < 0;
    const std::int32_t mag = sign ? -comp : comp;
    const MvClassType mv_class = getMvClassLocal(mag - 1, &offset);
    const std::int32_t d = offset >> 3;          // int32_t mv data
    const std::int32_t fr = (offset >> 1) & 3;   // fractional mv data
    const std::int32_t hp = offset & 1;          // high precision mv data

    // Sign
    odEcWriteSymbol(w, sign, mvcomp->sign_cdf, 2);

    // Class
    odEcWriteSymbol(w, mv_class, mvcomp->classes_cdf, MV_CLASSES);

    // Integer bits
    if (mv_class == MV_CLASS_0) {
        odEcWriteSymbol(w, d, mvcomp->class0_cdf, CLASS0_SIZE);
    } else {
        const std::int32_t n = mv_class + CLASS0_BITS - 1;  // number of bits
        for (std::int32_t i = 0; i < n; ++i) {
            odEcWriteSymbol(w, (d >> i) & 1, mvcomp->bits_cdf[i], 2);
        }
    }
    // Fractional bits
    if (precision > MV_SUBPEL_NONE) {
        odEcWriteSymbol(w, fr, mv_class == MV_CLASS_0 ? mvcomp->class0_fp_cdf[d] : mvcomp->fp_cdf,
                        MV_FP_SIZE);
    }

    // High precision bit
    if (precision > MV_SUBPEL_LOW_PRECISION) {
        odEcWriteSymbol(w, hp, mv_class == MV_CLASS_0 ? mvcomp->class0_hp_cdf : mvcomp->hp_cdf, 2);
    }
}

// av1_get_mv_joint_diff :1485-:1493
MvJointType getMvJointDiff(const std::int32_t diff[2]) {
    if (diff[0] == 0) {
        return diff[1] == 0 ? MV_JOINT_ZERO : MV_JOINT_HNZVZ;
    }
    return diff[1] == 0 ? MV_JOINT_HZVNZ : MV_JOINT_HNZVNZ;
}

// svt_av1_encode_mv :1495-:1519
void encodeMv(int force_integer_mv, AomWriter* w, const Mv& mv, const Mv& ref, NmvContext* mvctx,
              int usehp) {
    // The y-component (row component) of the MV is coded first
    std::int32_t diff[2] = {mv.y - ref.y, mv.x - ref.x};
    const MvJointType j = getMvJointDiff(diff);

    if (force_integer_mv) {
        usehp = MV_SUBPEL_NONE;
    }
    odEcWriteSymbol(w, j, mvctx->joints_cdf, MV_JOINTS);
    if (mvJointVertical(j)) {
        encodeMvComponent(w, diff[0], &mvctx->comps[0], (MvSubpelPrecision)usehp);
    }

    if (mvJointHorizontal(j)) {
        encodeMvComponent(w, diff[1], &mvctx->comps[1], (MvSubpelPrecision)usehp);
    }
}

// svt_aom_get_ref_filter_type :1522-:1527
InterpFilter getRefFilterType(const InterMbMi* ref_mi, int dir, std::int32_t ref_frame) {
    return ((ref_mi->ref_frame[0] == ref_frame || ref_mi->ref_frame[1] == ref_frame)
                ? extractInterpFilter(ref_mi->interp_filters, dir & 0x01)
                : SWITCHABLE_FILTERS);
}

// svt_aom_get_pred_context_switchable_interp :1530-:1573
int getPredContextSwitchableInterp(std::int32_t rf0, std::int32_t rf1, const InterMbD* xd, int dir) {
    const std::int32_t ctx_offset = (rf1 > INTRA_FRAME) * INTER_FILTER_COMP_OFFSET;
    const std::int32_t ref_frame = rf0;

    int filter_type_ctx = ctx_offset + (dir & 0x01) * INTER_FILTER_DIR_OFFSET;
    int left_type = SWITCHABLE_FILTERS;
    int above_type = SWITCHABLE_FILTERS;

    if (xd->left_available) {
        left_type = getRefFilterType(xd->left_mi, dir, ref_frame);
    }

    if (xd->up_available) {
        above_type = getRefFilterType(xd->above_mi, dir, ref_frame);
    }

    if (left_type == above_type) {
        filter_type_ctx += left_type;
    } else if (left_type == SWITCHABLE_FILTERS) {
        filter_type_ctx += above_type;
    } else if (above_type == SWITCHABLE_FILTERS) {
        filter_type_ctx += left_type;
    } else {
        filter_type_ctx += SWITCHABLE_FILTERS;
    }
    return filter_type_ctx;
}

// svt_aom_is_nontrans_global_motion :1575-:1596
int isNontransGlobalMotion(const InterMbMi* block_mi, BlockSize bsize, const int* gmWmtype) {
    // First check if all modes are GLOBALMV
    if (block_mi->mode != GLOBALMV && block_mi->mode != GLOBAL_GLOBALMV) {
        return 0;
    }

    if (AOMMIN(miSizeWide[bsize], miSizeHigh[bsize]) < 2) {
        return 0;
    }
    const std::uint8_t is_compound = isInterCompoundMode(block_mi->mode) ? 1u : 0u;
    // Now check if all global motion is non translational
    for (int ref = 0; ref < 1 + is_compound; ++ref) {
        if (gmWmtype[block_mi->ref_frame[ref]] == TRANSLATION) {
            return 0;
        }
    }
    return 1;
}

// av1_is_interp_needed :1598-:1612
int isInterpNeeded(const InterMbMi* block_mi, BlockSize bsize, const int* gmWmtype) {
    if (block_mi->skip_mode) {
        return 0;
    }

    if (block_mi->motion_mode == WARPED_CAUSAL) {
        return 0;
    }

    if (isNontransGlobalMotion(block_mi, bsize, gmWmtype)) {
        return 0;
    }

    return 1;
}

// write_mb_interp_filter :1614-:1631
void writeMbInterpFilter(AomWriter* w, InterFc* fc, BlockSize bsize, std::int32_t rf0,
                         std::int32_t rf1, int interpolation_filter, int enable_dual_filter,
                         const InterMbMi* mbmi, const InterMbD* xd, const int* gmWmtype) {
    if (interpolation_filter != SWITCHABLE || !isInterpNeeded(mbmi, bsize, gmWmtype)) {
        return;
    }

    const int max_dir = enable_dual_filter ? 2 : 1;
    for (int dir = 0; dir < max_dir; ++dir) {
        const int ctx = getPredContextSwitchableInterp(rf0, rf1, xd, dir);
        const InterpFilter filter = extractInterpFilter(mbmi->interp_filters, dir);
        odEcWriteSymbol(w, filter, fc->switchable_interp_cdf[ctx], SWITCHABLE_FILTERS);
    }
}

// write_inter_compound_mode :1633-:1638
void writeInterCompoundMode(AomWriter* w, InterFc* fc, PredictionMode mode, std::int16_t mode_ctx) {
    odEcWriteSymbol(w, INTER_COMPOUND_OFFSET(mode), fc->inter_compound_mode_cdf[mode_ctx],
                    INTER_COMPOUND_MODES);
}

// svt_aom_get_reference_mode_context_new :1839-:1881
int getReferenceModeContext(const InterMbD* xd) {
    int ctx;
    const InterMbMi* const above_mbmi = xd->above_mi;
    const InterMbMi* const left_mbmi = xd->left_mi;
    const int has_above = xd->up_available;
    const int has_left = xd->left_available;

    if (has_above && has_left) {  // both edges available
        if (!hasSecondRef(above_mbmi) && !hasSecondRef(left_mbmi)) {
            // neither edge uses comp pred (0/1)
            ctx = IS_BACKWARD_REF_FRAME(above_mbmi->ref_frame[0]) ^
                IS_BACKWARD_REF_FRAME(left_mbmi->ref_frame[0]);
        } else if (!hasSecondRef(above_mbmi)) {
            // one of two edges uses comp pred (2/3)
            ctx = 2 + (IS_BACKWARD_REF_FRAME(above_mbmi->ref_frame[0]) || !isInterBlock(above_mbmi));
        } else if (!hasSecondRef(left_mbmi)) {
            // one of two edges uses comp pred (2/3)
            ctx = 2 + (IS_BACKWARD_REF_FRAME(left_mbmi->ref_frame[0]) || !isInterBlock(left_mbmi));
        } else {  // both edges use comp pred (4)
            ctx = 4;
        }
    } else if (has_above || has_left) {  // one edge available
        const InterMbMi* edge_mbmi = has_above ? above_mbmi : left_mbmi;

        if (!hasSecondRef(edge_mbmi)) {
            // edge does not use comp pred (0/1)
            ctx = IS_BACKWARD_REF_FRAME(edge_mbmi->ref_frame[0]);
        } else {
            // edge uses comp pred (3)
            ctx = 3;
        }
    } else {  // no edges available (1)
        ctx = 1;
    }
    return ctx;
}

// svt_aom_get_reference_mode_cdf :1642-:1644
AomCdfProb* getReferenceModeCdf(InterFc* fc, const InterMbD* xd) {
    return fc->comp_inter_cdf[getReferenceModeContext(xd)];
}

// svt_aom_get_comp_reference_type_context_new :1701-:1770
int getCompReferenceTypeContext(const InterMbD* xd) {
    int pred_context;
    const InterMbMi* const above_mbmi = xd->above_mi;
    const InterMbMi* const left_mbmi = xd->left_mi;
    const int above_in_image = xd->up_available;
    const int left_in_image = xd->left_available;

    if (above_in_image && left_in_image) {  // both edges available
        const int above_intra = !isInterBlock(above_mbmi);
        const int left_intra = !isInterBlock(left_mbmi);

        if (above_intra && left_intra) {  // intra/intra
            pred_context = 2;
        } else if (above_intra || left_intra) {  // intra/inter
            const InterMbMi* inter_mbmi = above_intra ? left_mbmi : above_mbmi;

            if (!hasSecondRef(inter_mbmi)) {  // single pred
                pred_context = 2;
            } else {  // comp pred
                pred_context = 1 + 2 * hasUniCompRefs(inter_mbmi);
            }
        } else {  // inter/inter
            const int a_sg = !hasSecondRef(above_mbmi);
            const int l_sg = !hasSecondRef(left_mbmi);
            const std::int32_t frfa = above_mbmi->ref_frame[0];
            const std::int32_t frfl = left_mbmi->ref_frame[0];

            if (a_sg && l_sg) {  // single/single
                pred_context =
                    1 + 2 * (!(IS_BACKWARD_REF_FRAME(frfa) ^ IS_BACKWARD_REF_FRAME(frfl)));
            } else if (l_sg || a_sg) {  // single/comp
                const int uni_rfc = a_sg ? hasUniCompRefs(left_mbmi) : hasUniCompRefs(above_mbmi);

                if (!uni_rfc) {  // comp bidir
                    pred_context = 1;
                } else {  // comp unidir
                    pred_context =
                        3 + (!(IS_BACKWARD_REF_FRAME(frfa) ^ IS_BACKWARD_REF_FRAME(frfl)));
                }
            } else {  // comp/comp
                const int a_uni_rfc = hasUniCompRefs(above_mbmi);
                const int l_uni_rfc = hasUniCompRefs(left_mbmi);

                if (!a_uni_rfc && !l_uni_rfc) {  // bidir/bidir
                    pred_context = 0;
                } else if (!a_uni_rfc || !l_uni_rfc) {  // unidir/bidir
                    pred_context = 2;
                } else {  // unidir/unidir
                    pred_context = 3 + (!((frfa == BWDREF_FRAME) ^ (frfl == BWDREF_FRAME)));
                }
            }
        }
    } else if (above_in_image || left_in_image) {  // one edge available
        const InterMbMi* edge_mbmi = above_in_image ? above_mbmi : left_mbmi;

        if (!isInterBlock(edge_mbmi)) {  // intra
            pred_context = 2;
        } else {  // inter
            if (!hasSecondRef(edge_mbmi)) {  // single pred
                pred_context = 2;
            } else {  // comp pred
                pred_context = 4 * hasUniCompRefs(edge_mbmi);
            }
        }
    } else {  // no edges available
        pred_context = 2;
    }
    return pred_context;
}

// svt_aom_get_comp_reference_type_cdf :1656-:1659
AomCdfProb* getCompReferenceTypeCdf(InterFc* fc, const InterMbD* xd) {
    const int pred_context = getCompReferenceTypeContext(xd);
    return fc->comp_ref_type_cdf[pred_context];
}

// svt_av1_get_pred_context_uni_comp_ref_p :1780-:1793
int getPredContextUniCompRefP(const InterMbD* xd) {
    const std::uint8_t* const ref_counts = xd->neighbors_ref_counts;

    // Count of forward references (L, L2, L3, or G)
    const int frf_count = ref_counts[LAST_FRAME] + ref_counts[LAST2_FRAME] + ref_counts[LAST3_FRAME] +
        ref_counts[GOLDEN_FRAME];
    // Count of backward references (b or A)
    const int brf_count = ref_counts[BWDREF_FRAME] + ref_counts[ALTREF2_FRAME] + ref_counts[ALTREF_FRAME];

    const int pred_context = (frf_count == brf_count) ? 1 : ((frf_count < brf_count) ? 0 : 2);
    return pred_context;
}

// svt_av1_get_pred_context_uni_comp_ref_p1 :1803-:1815
int getPredContextUniCompRefP1(const InterMbD* xd) {
    const std::uint8_t* const ref_counts = xd->neighbors_ref_counts;

    // Count of LAST2
    const int last2_count = ref_counts[LAST2_FRAME];
    // Count of LAST3 or GOLDEN
    const int last3_or_gld_count = ref_counts[LAST3_FRAME] + ref_counts[GOLDEN_FRAME];

    const int pred_context =
        (last2_count == last3_or_gld_count) ? 1 : ((last2_count < last3_or_gld_count) ? 0 : 2);
    return pred_context;
}

// svt_av1_get_pred_context_uni_comp_ref_p2 :1825-:1837
int getPredContextUniCompRefP2(const InterMbD* xd) {
    const std::uint8_t* const ref_counts = xd->neighbors_ref_counts;

    // Count of LAST3
    const int last3_count = ref_counts[LAST3_FRAME];
    // Count of GOLDEN
    const int gld_count = ref_counts[GOLDEN_FRAME];

    const int pred_context = (last3_count == gld_count) ? 1 : ((last3_count < gld_count) ? 0 : 2);
    return pred_context;
}

// svt_aom_get_pred_cdf_uni_comp_ref_p :1661-:1664
AomCdfProb* getPredCdfUniCompRefP(InterFc* fc, const InterMbD* xd) {
    const int pred_context = getPredContextUniCompRefP(xd);
    return fc->uni_comp_ref_cdf[pred_context][0];
}

// svt_aom_get_pred_cdf_uni_comp_ref_p1 :1666-:1669
AomCdfProb* getPredCdfUniCompRefP1(InterFc* fc, const InterMbD* xd) {
    const int pred_context = getPredContextUniCompRefP1(xd);
    return fc->uni_comp_ref_cdf[pred_context][1];
}

// svt_aom_get_pred_cdf_uni_comp_ref_p2 :1671-:1674
AomCdfProb* getPredCdfUniCompRefP2(InterFc* fc, const InterMbD* xd) {
    const int pred_context = getPredContextUniCompRefP2(xd);
    return fc->uni_comp_ref_cdf[pred_context][2];
}

// get_pred_context_ll2_or_l3gld :1918-:1931
int getPredContextLl2OrL3Gld(const std::uint8_t* ref_counts) {
    // Count of LAST + LAST2
    const std::int32_t last_last2_count = ref_counts[LAST_FRAME] + ref_counts[LAST2_FRAME];
    // Count of LAST3 + GOLDEN
    const std::int32_t last3_gld_count = ref_counts[LAST3_FRAME] + ref_counts[GOLDEN_FRAME];

    const std::int32_t pred_context =
        (last_last2_count == last3_gld_count) ? 1 : ((last_last2_count < last3_gld_count) ? 0 : 2);
    return pred_context;
}

// get_pred_context_last_or_last2 :1934-:1946
int getPredContextLastOrLast2(const std::uint8_t* ref_counts) {
    // Count of LAST
    const std::int32_t last_count = ref_counts[LAST_FRAME];
    // Count of LAST2
    const std::int32_t last2_count = ref_counts[LAST2_FRAME];

    const std::int32_t pred_context =
        (last_count == last2_count) ? 1 : ((last_count < last2_count) ? 0 : 2);
    return pred_context;
}

// get_pred_context_last3_or_gld :1949-:1961
int getPredContextLast3OrGld(const std::uint8_t* ref_counts) {
    // Count of LAST3
    const std::int32_t last3_count = ref_counts[LAST3_FRAME];
    // Count of GOLDEN
    const std::int32_t gld_count = ref_counts[GOLDEN_FRAME];

    const std::int32_t pred_context = (last3_count == gld_count) ? 1 : ((last3_count < gld_count) ? 0 : 2);
    return pred_context;
}

// get_pred_context_brfarf2_or_arf :1965-:1976
int getPredContextBrfArf2OrArf(const std::uint8_t* ref_counts) {
    // Counts of BWDREF, ALTREF2, or ALTREF frames (b, A2, or A)
    const std::int32_t brfarf2_count = ref_counts[BWDREF_FRAME] + ref_counts[ALTREF2_FRAME];
    const std::int32_t arf_count = ref_counts[ALTREF_FRAME];

    const std::int32_t pred_context =
        (brfarf2_count == arf_count) ? 1 : ((brfarf2_count < arf_count) ? 0 : 2);
    return pred_context;
}

// get_pred_context_brf_or_arf2 :1979-:1991
int getPredContextBrfOrArf2(const std::uint8_t* ref_counts) {
    // Count of BWDREF frames (b)
    const std::int32_t brf_count = ref_counts[BWDREF_FRAME];
    // Count of ALTREF2 frames (A2)
    const std::int32_t arf2_count = ref_counts[ALTREF2_FRAME];

    const std::int32_t pred_context = (brf_count == arf2_count) ? 1 : ((brf_count < arf2_count) ? 0 : 2);
    return pred_context;
}

// svt_av1_get_pred_context_comp_ref_p :1998-:2001
int getPredContextCompRefP(const InterMbD* xd) {
    return getPredContextLl2OrL3Gld(xd->neighbors_ref_counts);
}
// svt_av1_get_pred_context_comp_ref_p1 :2005-:2008
int getPredContextCompRefP1(const InterMbD* xd) {
    return getPredContextLastOrLast2(xd->neighbors_ref_counts);
}
// svt_av1_get_pred_context_comp_ref_p2 :2012-:2015
int getPredContextCompRefP2(const InterMbD* xd) {
    return getPredContextLast3OrGld(xd->neighbors_ref_counts);
}
// svt_av1_get_pred_context_comp_bwdref_p :2018-:2021
int getPredContextCompBwdrefP(const InterMbD* xd) {
    return getPredContextBrfArf2OrArf(xd->neighbors_ref_counts);
}
// svt_av1_get_pred_context_comp_bwdref_p1 :2024-:2027
int getPredContextCompBwdrefP1(const InterMbD* xd) {
    return getPredContextBrfOrArf2(xd->neighbors_ref_counts);
}

// svt_aom_get_pred_cdf_comp_ref_p :1676-:1679
AomCdfProb* getPredCdfCompRefP(InterFc* fc, const InterMbD* xd) {
    const int pred_context = getPredContextCompRefP(xd);
    return fc->comp_ref_cdf[pred_context][0];
}
// svt_aom_get_pred_cdf_comp_ref_p1 :1681-:1684
AomCdfProb* getPredCdfCompRefP1(InterFc* fc, const InterMbD* xd) {
    const int pred_context = getPredContextCompRefP1(xd);
    return fc->comp_ref_cdf[pred_context][1];
}
// svt_aom_get_pred_cdf_comp_ref_p2 :1686-:1689
AomCdfProb* getPredCdfCompRefP2(InterFc* fc, const InterMbD* xd) {
    const int pred_context = getPredContextCompRefP2(xd);
    return fc->comp_ref_cdf[pred_context][2];
}
// svt_aom_get_pred_cdf_comp_bwdref_p :1691-:1694
AomCdfProb* getPredCdfCompBwdrefP(InterFc* fc, const InterMbD* xd) {
    const int pred_context = getPredContextCompBwdrefP(xd);
    return fc->comp_bwdref_cdf[pred_context][0];
}
// svt_aom_get_pred_cdf_comp_bwdref_p1 :1696-:1699
AomCdfProb* getPredCdfCompBwdrefP1(InterFc* fc, const InterMbD* xd) {
    const int pred_context = getPredContextCompBwdrefP1(xd);
    return fc->comp_bwdref_cdf[pred_context][1];
}

// svt_av1_get_pred_context_single_ref_p1 :2032-:2045
int getPredContextSingleRefP1(const InterMbD* xd) {
    const std::uint8_t* const ref_counts = xd->neighbors_ref_counts;

    // Count of forward reference frames
    const std::int32_t fwd_count = ref_counts[LAST_FRAME] + ref_counts[LAST2_FRAME] +
        ref_counts[LAST3_FRAME] + ref_counts[GOLDEN_FRAME];
    // Count of backward reference frames
    const std::int32_t bwd_count = ref_counts[BWDREF_FRAME] + ref_counts[ALTREF2_FRAME] +
        ref_counts[ALTREF_FRAME];

    const std::int32_t pred_context = (fwd_count == bwd_count) ? 1 : ((fwd_count < bwd_count) ? 0 : 2);
    return pred_context;
}
// svt_av1_get_pred_context_single_ref_p2 :2074-:2077
int getPredContextSingleRefP2(const InterMbD* xd) {
    return getPredContextBrfArf2OrArf(xd->neighbors_ref_counts);
}
// svt_av1_get_pred_context_single_ref_p3 :2080-:2083
int getPredContextSingleRefP3(const InterMbD* xd) {
    return getPredContextLl2OrL3Gld(xd->neighbors_ref_counts);
}
// svt_av1_get_pred_context_single_ref_p4 :2086-:2089
int getPredContextSingleRefP4(const InterMbD* xd) {
    return getPredContextLastOrLast2(xd->neighbors_ref_counts);
}
// svt_av1_get_pred_context_single_ref_p5 :2092-:2095
int getPredContextSingleRefP5(const InterMbD* xd) {
    return getPredContextLast3OrGld(xd->neighbors_ref_counts);
}
// svt_av1_get_pred_context_single_ref_p6 :2098-:2101
int getPredContextSingleRefP6(const InterMbD* xd) {
    return getPredContextBrfOrArf2(xd->neighbors_ref_counts);
}

// svt_aom_get_pred_cdf_single_ref_p1..p6 :2047-:2069
AomCdfProb* getPredCdfSingleRefP1(InterFc* fc, const InterMbD* xd) {
    return fc->single_ref_cdf[getPredContextSingleRefP1(xd)][0];
}
AomCdfProb* getPredCdfSingleRefP2(InterFc* fc, const InterMbD* xd) {
    return fc->single_ref_cdf[getPredContextSingleRefP2(xd)][1];
}
AomCdfProb* getPredCdfSingleRefP3(InterFc* fc, const InterMbD* xd) {
    return fc->single_ref_cdf[getPredContextSingleRefP3(xd)][2];
}
AomCdfProb* getPredCdfSingleRefP4(InterFc* fc, const InterMbD* xd) {
    return fc->single_ref_cdf[getPredContextSingleRefP4(xd)][3];
}
AomCdfProb* getPredCdfSingleRefP5(InterFc* fc, const InterMbD* xd) {
    return fc->single_ref_cdf[getPredContextSingleRefP5(xd)][4];
}
AomCdfProb* getPredCdfSingleRefP6(InterFc* fc, const InterMbD* xd) {
    return fc->single_ref_cdf[getPredContextSingleRefP6(xd)][5];
}

// svt_aom_collect_neighbors_ref_counts_new :1883-:1908
void collectNeighborsRefCounts(InterMbD* xd) {
    std::memset(xd->neighbors_ref_counts, 0, sizeof(xd->neighbors_ref_counts));
    std::uint8_t* const ref_counts = xd->neighbors_ref_counts;

    const InterMbMi* const above_mbmi = xd->above_mi;
    const InterMbMi* const left_mbmi = xd->left_mi;
    const int above_in_image = xd->up_available;
    const int left_in_image = xd->left_available;

    // Above neighbor
    if (above_in_image && isInterBlock(above_mbmi)) {
        ref_counts[above_mbmi->ref_frame[0]]++;
        if (hasSecondRef(above_mbmi)) {
            ref_counts[above_mbmi->ref_frame[1]]++;
        }
    }

    // Left neighbor
    if (left_in_image && isInterBlock(left_mbmi)) {
        ref_counts[left_mbmi->ref_frame[0]]++;
        if (hasSecondRef(left_mbmi)) {
            ref_counts[left_mbmi->ref_frame[1]]++;
        }
    }
}

// write_ref_frames :2104-:2187
void writeRefFrames(AomWriter* w, InterFc* fc, const InterMbD* xd, const InterMbMi* mbmi,
                    BlockSize bsize, int reference_mode) {
    const int is_compound = hasSecondRef(mbmi);
    {
        // does the feature use compound prediction or not
        // (if not specified at the frame/segment level)
        if (reference_mode == REFERENCE_MODE_SELECT) {
            if (isCompRefAllowed(bsize)) {
                odEcWriteSymbol(w, is_compound, getReferenceModeCdf(fc, xd), 2);
            }
        }

        if (is_compound) {
            const CompReferenceType comp_ref_type =
                hasUniCompRefs(mbmi) ? UNIDIR_COMP_REFERENCE : BIDIR_COMP_REFERENCE;
            odEcWriteSymbol(w, comp_ref_type, getCompReferenceTypeCdf(fc, xd), 2);

            if (comp_ref_type == UNIDIR_COMP_REFERENCE) {
                const int bit = mbmi->ref_frame[0] == BWDREF_FRAME;
                odEcWriteSymbol(w, bit, getPredCdfUniCompRefP(fc, xd), 2);

                if (!bit) {
                    const int bit1 =
                        mbmi->ref_frame[1] == LAST3_FRAME || mbmi->ref_frame[1] == GOLDEN_FRAME;
                    odEcWriteSymbol(w, bit1, getPredCdfUniCompRefP1(fc, xd), 2);
                    if (bit1) {
                        const int bit2 = mbmi->ref_frame[1] == GOLDEN_FRAME;
                        odEcWriteSymbol(w, bit2, getPredCdfUniCompRefP2(fc, xd), 2);
                    }
                }
                return;
            }

            const int bit =
                (mbmi->ref_frame[0] == GOLDEN_FRAME || mbmi->ref_frame[0] == LAST3_FRAME);
            odEcWriteSymbol(w, bit, getPredCdfCompRefP(fc, xd), 2);

            if (!bit) {
                const int bit1 = mbmi->ref_frame[0] == LAST2_FRAME;
                odEcWriteSymbol(w, bit1, getPredCdfCompRefP1(fc, xd), 2);
            } else {
                const int bit2 = mbmi->ref_frame[0] == GOLDEN_FRAME;
                odEcWriteSymbol(w, bit2, getPredCdfCompRefP2(fc, xd), 2);
            }

            const int bit_bwd = mbmi->ref_frame[1] == ALTREF_FRAME;
            odEcWriteSymbol(w, bit_bwd, getPredCdfCompBwdrefP(fc, xd), 2);

            if (!bit_bwd) {
                odEcWriteSymbol(w, mbmi->ref_frame[1] == ALTREF2_FRAME,
                                getPredCdfCompBwdrefP1(fc, xd), 2);
            }
        } else {
            const int bit0 =
                (mbmi->ref_frame[0] <= ALTREF_FRAME && mbmi->ref_frame[0] >= BWDREF_FRAME);
            odEcWriteSymbol(w, bit0, getPredCdfSingleRefP1(fc, xd), 2);

            if (bit0) {
                const int bit1 = mbmi->ref_frame[0] == ALTREF_FRAME;
                odEcWriteSymbol(w, bit1, getPredCdfSingleRefP2(fc, xd), 2);
                if (!bit1) {
                    odEcWriteSymbol(w, mbmi->ref_frame[0] == ALTREF2_FRAME,
                                    getPredCdfSingleRefP6(fc, xd), 2);
                }
            } else {
                const int bit2 =
                    (mbmi->ref_frame[0] == LAST3_FRAME || mbmi->ref_frame[0] == GOLDEN_FRAME);
                odEcWriteSymbol(w, bit2, getPredCdfSingleRefP3(fc, xd), 2);
                if (!bit2) {
                    const int bit3 = mbmi->ref_frame[0] != LAST_FRAME;
                    odEcWriteSymbol(w, bit3, getPredCdfSingleRefP4(fc, xd), 2);
                } else {
                    const int bit4 = mbmi->ref_frame[0] != LAST3_FRAME;
                    odEcWriteSymbol(w, bit4, getPredCdfSingleRefP5(fc, xd), 2);
                }
            }
        }
    }
}

}  // namespace entropy
