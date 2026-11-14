// A5H - the frame-header / tile-info writer surface of entropy_coding.c.
// Reference: third_party/SVT-AV1/Source/Lib/Codec/entropy_coding.c. Every
// function cites its span; the carrier walls are threaded per the RE8A
// pattern (see header_symbols.h).

#include "header_symbols.h"

#include <cassert>
#include <cstring>

namespace entropy {

namespace {

// definitions.h:596-:611 get_msb (MSVC _BitScanReverse form); the l7
// file-local twin (entropy.cpp:23 precedent).
inline int getMsb(std::uint32_t n) {
    assert(n != 0);
    unsigned long first_set_bit;
    _BitScanReverse(&first_set_bit, n);
    return (int)first_set_bit;
}

}  // namespace

// svt_aom_wb_is_byte_aligned (entropy_coding.c:1343-:1345)
int wbIsByteAligned(const AomWriteBitBuffer* wb) {
    return (wb->bit_offset % CHAR_BIT == 0);
}

// svt_aom_wb_bytes_written (entropy_coding.c:1347-:1349)
std::uint32_t wbBytesWritten(const AomWriteBitBuffer* wb) {
    return wb->bit_offset / CHAR_BIT + (wb->bit_offset % CHAR_BIT > 0);
}

// recenter_nonneg (entropy_coding.c:2842-:2850)
std::uint16_t recenterNonneg(std::uint16_t r, std::uint16_t v) {
    if (v > (r << 1)) {
        return v;
    } else if (v >= r) {
        return (std::uint16_t)((v - r) << 1);
    } else {
        return (std::uint16_t)(((r - v) << 1) - 1);
    }
}

// recenter_finite_nonneg (entropy_coding.c:2854-:2860)
std::uint16_t recenterFiniteNonneg(std::uint16_t n, std::uint16_t r, std::uint16_t v) {
    if ((r << 1) <= n) {
        return recenterNonneg(r, v);
    } else {
        return recenterNonneg((std::uint16_t)(n - 1 - r), (std::uint16_t)(n - 1 - v));
    }
}

// svt_aom_write_primitive_quniform (entropy_coding.c:2863-:2875)
void writePrimitiveQuniform(AomWriter* w, std::uint16_t n, std::uint16_t v) {
    if (n <= 1) {
        return;
    }
    const std::int32_t l = getMsb(n - 1) + 1;
    const std::int32_t m = (1 << l) - n;
    if (v < m) {
        odEcWriteLiteralBits(w, v, l - 1);
    } else {
        odEcWriteLiteralBits(w, m + ((v - m) >> 1), l - 1);
        odEcWriteLiteralBit(w, (v - m) & 1, 0);
    }
}

// aom_wb_write_primitive_quniform (entropy_coding.c:2877-:2889)
void wbWritePrimitiveQuniform(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t v) {
    if (n <= 1) {
        return;
    }
    const std::int32_t l = getMsb(n - 1) + 1;
    const std::int32_t m = (1 << l) - n;
    if (v < m) {
        wbWriteLiteral(wb, v, l - 1);
    } else {
        wbWriteLiteral(wb, m + ((v - m) >> 1), l - 1);
        wbWriteBit(wb, (v - m) & 1);
    }
}

// svt_aom_count_primitive_quniform (entropy_coding.c:2891-:2898)
int countPrimitiveQuniform(std::uint16_t n, std::uint16_t v) {
    if (n <= 1) {
        return 0;
    }
    const std::int32_t l = getMsb(n - 1) + 1;
    const std::int32_t m = (1 << l) - n;
    return v < m ? l - 1 : l;
}

// svt_aom_write_primitive_subexpfin (entropy_coding.c:2901-:2922)
void writePrimitiveSubexpfin(AomWriter* w, std::uint16_t n, std::uint16_t k, std::uint16_t v) {
    std::int32_t i = 0;
    std::int32_t mk = 0;
    while (1) {
        std::int32_t b = (i ? k + i - 1 : k);
        std::int32_t a = (1 << b);
        if (n <= mk + 3 * a) {
            writePrimitiveQuniform(w, (std::uint16_t)(n - mk), (std::uint16_t)(v - mk));
            break;
        } else {
            std::int32_t t = (v >= mk + a);
            odEcWriteLiteralBit(w, t, 0);
            if (t) {
                i = i + 1;
                mk += a;
            } else {
                odEcWriteLiteralBits(w, v - mk, b);
                break;
            }
        }
    }
}

// aom_wb_write_primitive_subexpfin (entropy_coding.c:2924-:2945)
void wbWritePrimitiveSubexpfin(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t k,
                               std::uint16_t v) {
    std::int32_t i = 0;
    std::int32_t mk = 0;
    while (1) {
        std::int32_t b = (i ? k + i - 1 : k);
        std::int32_t a = (1 << b);
        if (n <= mk + 3 * a) {
            wbWritePrimitiveQuniform(wb, (std::uint16_t)(n - mk), (std::uint16_t)(v - mk));
            break;
        } else {
            std::int32_t t = (v >= mk + a);
            wbWriteBit(wb, t);
            if (t) {
                i = i + 1;
                mk += a;
            } else {
                wbWriteLiteral(wb, v - mk, b);
                break;
            }
        }
    }
}

// svt_aom_count_primitive_subexpfin (entropy_coding.c:2947-:2970)
int countPrimitiveSubexpfin(std::uint16_t n, std::uint16_t k, std::uint16_t v) {
    std::int32_t count = 0;
    std::int32_t i = 0;
    std::int32_t mk = 0;
    while (1) {
        std::int32_t b = (i ? k + i - 1 : k);
        std::int32_t a = (1 << b);
        if (n <= mk + 3 * a) {
            count += countPrimitiveQuniform((std::uint16_t)(n - mk), (std::uint16_t)(v - mk));
            break;
        } else {
            std::int32_t t = (v >= mk + a);
            count++;
            if (t) {
                i = i + 1;
                mk += a;
            } else {
                count += b;
                break;
            }
        }
    }
    return count;
}

// svt_aom_write_primitive_refsubexpfin (entropy_coding.c:2975-:2977)
void writePrimitiveRefsubexpfin(AomWriter* w, std::uint16_t n, std::uint16_t k, std::uint16_t ref,
                                std::uint16_t v) {
    writePrimitiveSubexpfin(w, n, k, recenterFiniteNonneg(n, ref, v));
}

// aom_wb_write_primitive_refsubexpfin (entropy_coding.c:2979-:2982)
void wbWritePrimitiveRefsubexpfin(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t k,
                                  std::uint16_t ref, std::uint16_t v) {
    wbWritePrimitiveSubexpfin(wb, n, k, recenterFiniteNonneg(n, ref, v));
}

// svt_aom_wb_write_signed_primitive_refsubexpfin (entropy_coding.c:2984-:2990)
void wbWriteSignedPrimitiveRefsubexpfin(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t k,
                                        std::int16_t ref, std::int16_t v) {
    ref += n - 1;
    v += n - 1;
    const std::uint16_t scaled_n = (std::uint16_t)((n << 1) - 1);
    wbWritePrimitiveRefsubexpfin(wb, scaled_n, k, ref, v);
}

// svt_aom_count_primitive_refsubexpfin (entropy_coding.c:2992-:2994)
int countPrimitiveRefsubexpfin(std::uint16_t n, std::uint16_t k, std::uint16_t ref, std::uint16_t v) {
    return countPrimitiveSubexpfin(n, k, recenterFiniteNonneg(n, ref, v));
}

// write_delta_q (entropy_coding.c:2365-:2372)
void writeDeltaQ(AomWriteBitBuffer* wb, std::int32_t delta_q) {
    if (delta_q != 0) {
        wbWriteBit(wb, 1);
        wbWriteInvSignedLiteral(wb, delta_q, 6);
    } else {
        wbWriteBit(wb, 0);
    }
}

// write_profile (entropy_coding.c:2671-:2674)
void writeProfile(std::int32_t profile, AomWriteBitBuffer* wb) {
    assert(profile >= 0 && profile < 4);  // PROFILE_0..MAX_PROFILES bound
    wbWriteLiteral(wb, profile, HD_PROFILE_BITS);
}

// write_bitdepth (entropy_coding.c:2676-:2685). SVT_EFFECTIVE_BIT_DEPTH is
// the identity for the pinned HBD=1 build (EbConfigMacros.h:175/:293).
void writeBitdepth(const HdrSeqHeader* sh, AomWriteBitBuffer* wb) {
    // Profile 0/1: [0] for 8 bit, [1]  10-bit
    // Profile   2: [0] for 8 bit, [10] 10-bit, [11] - 12-bit
    wbWriteBit(wb, sh->encoder_bit_depth == HD_EB_EIGHT_BIT ? 0 : 1);
    if (sh->profile == HD_PROFESSIONAL_PROFILE &&
        sh->encoder_bit_depth != HD_EB_EIGHT_BIT) {
        wbWriteBit(wb, sh->encoder_bit_depth == HD_EB_TEN_BIT ? 0 : 1);
    }
}

// ---------------------------------------------------------------------------
// C2: the frame-header field writers.
// ---------------------------------------------------------------------------

// encode_restoration_mode (entropy_coding.c:2189-:2251). The pcs walls are
// threaded: frm_hdr.allow_intrabc, the child_pcs rst_info[3] facts, the
// scs->seq_header.sb_size flag (HD_SB_128X128), av1_cm (unused here - the
// assertions only). The reference asserts all_lossless == 0 - the caller
// owns that gate (the all_lossless branch at :3589 never calls this).
void encodeRestorationMode(HdrPcs* pcs, AomWriteBitBuffer* wb) {
    if (pcs->frm_hdr.allow_intrabc) {
        return;
    }

    const std::int32_t num_planes = 3;  // av1_num_planes(cm)
    std::int32_t all_none = 1, chroma_none = 1;
    for (std::int32_t p = 0; p < num_planes; ++p) {
        const HdrRestorationInfo* rsi = &pcs->rst_info[p];

        if (rsi->frame_restoration_type != HD_RESTORE_NONE) {
            all_none = 0;
            chroma_none &= (std::int32_t)(p == 0);
        }
        switch (rsi->frame_restoration_type) {
        case HD_RESTORE_NONE:
            wbWriteBit(wb, 0);
            wbWriteBit(wb, 0);
            break;
        case HD_RESTORE_WIENER:
            wbWriteBit(wb, 1);
            wbWriteBit(wb, 0);
            break;
        case HD_RESTORE_SGRPROJ:
            wbWriteBit(wb, 1);
            wbWriteBit(wb, 1);
            break;
        case HD_RESTORE_SWITCHABLE:
            wbWriteBit(wb, 0);
            wbWriteBit(wb, 1);
            break;
        default:
            assert(0);
        }
    }
    if (!all_none) {
        const std::int32_t sb_size = pcs->sb_size_is_128 ? 128 : 64;
        const HdrRestorationInfo* rsi = &pcs->rst_info[0];
        assert(rsi->restoration_unit_size >= sb_size);
        assert(HD_RESTORE_SWITCHABLE == 3 && 256 == RESTORATION_UNITSIZE_MAX_);  // RESTORATION_UNITSIZE_MAX == 256 (restoration.h:80)

        if (sb_size == 64) {
            wbWriteBit(wb, rsi->restoration_unit_size > 64);
        }
        if (rsi->restoration_unit_size > 64) {
            wbWriteBit(wb, rsi->restoration_unit_size > 128);
        }
    }
    if (!chroma_none) {
        wbWriteBit(wb, pcs->rst_info[1].restoration_unit_size != pcs->rst_info[0].restoration_unit_size);
        assert(pcs->rst_info[1].restoration_unit_size == pcs->rst_info[0].restoration_unit_size ||
               pcs->rst_info[1].restoration_unit_size ==
                   (pcs->rst_info[0].restoration_unit_size >> 1));
        assert(pcs->rst_info[2].restoration_unit_size == pcs->rst_info[1].restoration_unit_size);
    }
}

// encode_loopfilter (entropy_coding.c:2284-:2342). The reference asserts
// !coded_lossless (the caller owns that gate, :3592) and returns early on
// allow_intrabc. The mode_ref_delta_enabled body is the reference's own
// SVT_ERROR + commented-out block (:2305-:2340): it writes NOTHING.
void encodeLoopfilter(HdrPcs* pcs, AomWriteBitBuffer* wb) {
    assert(!pcs->frm_hdr.coded_lossless);
    if (pcs->frm_hdr.allow_intrabc) {
        return;
    }

    HdrLoopFilter* lf = &pcs->frm_hdr.loop_filter_params;

    // Encode the loop filter level and type
    wbWriteLiteral(wb, lf->filter_level[0], 6);
    wbWriteLiteral(wb, lf->filter_level[1], 6);
    if (lf->filter_level[0] || lf->filter_level[1]) {
        wbWriteLiteral(wb, lf->filter_level_u, 6);
        wbWriteLiteral(wb, lf->filter_level_v, 6);
    }
    wbWriteLiteral(wb, lf->sharpness_level, 3);

    // Write out loop filter deltas applied at the MB level based on mode or
    // ref frame (if they are enabled). The pinned reference errors out here
    // ("Loop Filter is not supported yet", :2305) and writes nothing.
    wbWriteBit(wb, lf->mode_ref_delta_enabled);
}

// encode_cdef (entropy_coding.c:2344-:2363)
void encodeCdef(const HdrPcs* pcs, AomWriteBitBuffer* wb) {
    const HdrFrameHeader* frm_hdr = &pcs->frm_hdr;

    if (frm_hdr->allow_intrabc) {
        return;
    }

    wbWriteLiteral(wb, frm_hdr->cdef_params.cdef_damping - 3, 2);
    // cdef_pri_damping & cdef_sec_damping consolidated to cdef_damping
    wbWriteLiteral(wb, frm_hdr->cdef_params.cdef_bits, 2);
    for (std::int32_t i = 0; i < pcs->nb_cdef_strengths; i++) {
        wbWriteLiteral(wb, frm_hdr->cdef_params.cdef_y_strength[i], HD_CDEF_STRENGTH_BITS);
        wbWriteLiteral(wb, frm_hdr->cdef_params.cdef_uv_strength[i], HD_CDEF_STRENGTH_BITS);
    }
}

// write_delta_q twin for the quantization span (:2365-:2372) - landed above.

// encode_quantization (entropy_coding.c:2374-:2401)
void encodeQuantization(const HdrPcs* pcs, AomWriteBitBuffer* wb) {
    const HdrFrameHeader* frm_hdr = &pcs->frm_hdr;
    wbWriteLiteral(wb, frm_hdr->quantization_params.base_q_idx, HD_QINDEX_BITS);
    writeDeltaQ(wb, frm_hdr->quantization_params.delta_q_dc[HD_PLANE_Y]);
    std::int32_t diff_uv_delta = (frm_hdr->quantization_params.delta_q_dc[HD_PLANE_U] !=
                                  frm_hdr->quantization_params.delta_q_dc[HD_PLANE_V]) ||
        (frm_hdr->quantization_params.delta_q_ac[HD_PLANE_U] !=
         frm_hdr->quantization_params.delta_q_ac[HD_PLANE_V]);

    if (diff_uv_delta) {
        wbWriteBit(wb, diff_uv_delta);
    }
    writeDeltaQ(wb, frm_hdr->quantization_params.delta_q_dc[HD_PLANE_U]);
    writeDeltaQ(wb, frm_hdr->quantization_params.delta_q_ac[HD_PLANE_U]);
    if (diff_uv_delta) {
        writeDeltaQ(wb, frm_hdr->quantization_params.delta_q_dc[HD_PLANE_V]);
        writeDeltaQ(wb, frm_hdr->quantization_params.delta_q_ac[HD_PLANE_V]);
    }
    wbWriteBit(wb, frm_hdr->quantization_params.using_qmatrix);
    if (frm_hdr->quantization_params.using_qmatrix) {
        wbWriteLiteral(wb, frm_hdr->quantization_params.qm[HD_PLANE_Y], HD_QM_LEVEL_BITS);
        wbWriteLiteral(wb, frm_hdr->quantization_params.qm[HD_PLANE_U], HD_QM_LEVEL_BITS);
        if (!diff_uv_delta) {
            assert(frm_hdr->quantization_params.qm[HD_PLANE_U] ==
                   frm_hdr->quantization_params.qm[HD_PLANE_V]);
        } else {
            wbWriteLiteral(wb, frm_hdr->quantization_params.qm[HD_PLANE_V], HD_QM_LEVEL_BITS);
        }
    }
}

// write_render_size (entropy_coding.c:2616-:2629)
void writeRenderSize(AomWriteBitBuffer* wb, HdrPcs* pcs) {
    int render_and_frame_size_different = 0;
    if (pcs->frame_resize_enabled) {
        render_and_frame_size_different = 1;
    }
    wbWriteBit(wb, render_and_frame_size_different);
    if (!render_and_frame_size_different) {
        return;
    }
    std::uint32_t render_width_minus_1 = (std::uint32_t)pcs->render_width - 1;
    std::uint32_t render_height_minus_1 = (std::uint32_t)pcs->render_height - 1;
    wbWriteLiteral(wb, (std::int32_t)render_width_minus_1, 16);
    wbWriteLiteral(wb, (std::int32_t)render_height_minus_1, 16);
}

// write_superres_scale (entropy_coding.c:2631-:2650)
void writeSuperresScale(AomWriteBitBuffer* wb, HdrPcs* pcs) {
    const HdrSeqHeader* scs = pcs->scs;
    const HdrAv1Common* cm = pcs->av1_cm;
    const std::uint8_t superres_denom = (std::uint8_t)cm->frm_size.superres_denominator;

    if (!scs->enable_superres) {
        assert(cm->frm_size.superres_denominator == HD_SCALE_NUMERATOR);
        return;
    }

    // First bit is whether to scale or not
    if (superres_denom == HD_SCALE_NUMERATOR) {
        wbWriteBit(wb, 0);  // no scaling
    } else {
        wbWriteBit(wb, 1);  // scaling, write scale factor
        assert(superres_denom >= HD_SUPERRES_SCALE_DENOMINATOR_MIN);
        assert(superres_denom < HD_SUPERRES_SCALE_DENOMINATOR_MIN + (1 << HD_SUPERRES_SCALE_BITS));
        wbWriteLiteral(wb, superres_denom - HD_SUPERRES_SCALE_DENOMINATOR_MIN,
                       HD_SUPERRES_SCALE_BITS);
    }
}

// write_frame_size (entropy_coding.c:2652-:2669). The reference drops
// frame_size_override on the floor for the non-override path (the
// (void)casts at :2654-:2655).
void writeFrameSize(HdrPcs* pcs, std::int32_t frame_size_override, AomWriteBitBuffer* wb) {
    HdrSeqHeader* scs = pcs->scs;
    HdrAv1Common* cm = pcs->av1_cm;
    const std::int32_t coded_width = cm->frm_size.superres_upscaled_width - 1;
    const std::int32_t coded_height = cm->frm_size.superres_upscaled_height - 1;

    if (frame_size_override) {
        const std::int32_t num_bits_width = scs->frame_width_bits;
        const std::int32_t num_bits_height = scs->frame_height_bits;
        wbWriteLiteral(wb, coded_width, num_bits_width);
        wbWriteLiteral(wb, coded_height, num_bits_height);
    }

    writeSuperresScale(wb, pcs);
    writeRenderSize(wb, pcs);
}
}  // namespace entropy
