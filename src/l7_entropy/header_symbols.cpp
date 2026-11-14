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

// ---------------------------------------------------------------------------
// C3: the tile machinery (:2403-:2614).
// ---------------------------------------------------------------------------
// tile_log2 (block_structures.h:131-:135)
static std::int32_t hdTileLog2(std::int32_t blk_size, std::int32_t target) {
    std::int32_t k;
    for (k = 0; (blk_size << k) < target; k++) {}
    return k;
}

#define HD_ALIGN_POWER_OF_TWO(value, n) \
    (((value) + ((1 << (n)) - 1)) & ~((1 << (n)) - 1))  // definitions.h:996

// svt_av1_get_tile_limits (entropy_coding.c:2450-:2467)
void getTileLimits(HdrPcs* pcs) {
    HdrAv1Common* cm = pcs->av1_cm;
    const std::int32_t mi_cols = HD_ALIGN_POWER_OF_TWO(cm->mi_cols, pcs->log2_sb_size);
    const std::int32_t mi_rows = HD_ALIGN_POWER_OF_TWO(cm->mi_rows, pcs->log2_sb_size);
    const std::int32_t sb_cols = mi_cols >> pcs->log2_sb_size;
    const std::int32_t sb_rows = mi_rows >> pcs->log2_sb_size;
    const std::int32_t sb_size_log2 = pcs->log2_sb_size + HD_MI_SIZE_LOG2;
    cm->tiles_info.max_tile_width_sb = (std::uint16_t)(HD_MAX_TILE_WIDTH >> sb_size_log2);
    const std::int32_t max_tile_area_sb = HD_MAX_TILE_AREA >> (2 * sb_size_log2);
    cm->tiles_info.min_log2_tile_cols = (std::uint8_t)hdTileLog2(cm->tiles_info.max_tile_width_sb, sb_cols);
    cm->tiles_info.max_log2_tile_cols =
        (std::uint8_t)hdTileLog2(1, AOMMIN(sb_cols, HD_MAX_TILE_COLS));
    cm->tiles_info.max_log2_tile_rows =
        (std::uint8_t)hdTileLog2(1, AOMMIN(sb_rows, HD_MAX_TILE_ROWS));
    cm->tiles_info.min_log2_tile_rows = 0;  // CHKN Tiles
    cm->tiles_info.min_log2_tiles = (std::uint8_t)hdTileLog2(max_tile_area_sb, sb_cols * sb_rows);
    cm->tiles_info.min_log2_tiles =
        (std::uint8_t)AOMMAX(cm->tiles_info.min_log2_tiles, cm->tiles_info.min_log2_tile_cols);
}

// svt_av1_calculate_tile_cols (entropy_coding.c:2469-:2509)
void calculateTileCols(HdrPcs* pcs) {
    HdrAv1Common* const cm = pcs->av1_cm;
    const int mi_cols = HD_ALIGN_POWER_OF_TWO(cm->mi_cols, pcs->log2_sb_size);
    const int mi_rows = HD_ALIGN_POWER_OF_TWO(cm->mi_rows, pcs->log2_sb_size);
    const int sb_cols = mi_cols >> pcs->log2_sb_size;
    const int sb_rows = mi_rows >> pcs->log2_sb_size;
    const int sb_size_log2 = pcs->log2_sb_size;
    if (cm->tiles_info.uniform_tile_spacing_flag) {
        int size_sb = HD_ALIGN_POWER_OF_TWO(sb_cols, cm->log2_tile_cols);
        size_sb >>= cm->log2_tile_cols;
        assert(size_sb > 0);
        int i = 0;
        for (int start_sb = 0; start_sb < sb_cols; i++) {
            cm->tiles_info.tile_col_start_mi[i] = (std::uint16_t)(start_sb << sb_size_log2);
            start_sb += size_sb;
        }
        cm->tiles_info.tile_cols = (std::uint8_t)i;
        cm->tiles_info.tile_col_start_mi[i] = (std::uint16_t)(sb_cols << sb_size_log2);
        cm->tiles_info.min_log2_tile_rows =
            (std::uint8_t)AOMMAX(cm->tiles_info.min_log2_tiles - cm->log2_tile_cols, 0);
        cm->tiles_info.max_tile_height_sb = (std::uint16_t)(sb_rows >> cm->tiles_info.min_log2_tile_rows);
        cm->tile_width = size_sb << pcs->log2_sb_size;
        cm->tile_width = AOMMIN(cm->tile_width, cm->mi_cols);
    } else {
        int max_tile_area_sb = (sb_rows * sb_cols);
        int widest_tile_sb = 1;
        cm->log2_tile_cols = hdTileLog2(1, cm->tiles_info.tile_cols);
        for (int i = 0; i < cm->tiles_info.tile_cols; i++) {
            int size_sb = (cm->tiles_info.tile_col_start_mi[i + 1] - cm->tiles_info.tile_col_start_mi[i]) >>
                sb_size_log2;
            widest_tile_sb = AOMMAX(widest_tile_sb, size_sb);
        }
        if (cm->tiles_info.min_log2_tiles) {
            max_tile_area_sb >>= (cm->tiles_info.min_log2_tiles + 1);
        }
        cm->tiles_info.max_tile_height_sb =
            (std::uint16_t)AOMMAX(max_tile_area_sb / widest_tile_sb, 1);
    }
}

// svt_av1_calculate_tile_rows (entropy_coding.c:2511-:2535)
void calculateTileRows(HdrPcs* pcs) {
    HdrAv1Common* const cm = pcs->av1_cm;
    int mi_rows = HD_ALIGN_POWER_OF_TWO(cm->mi_rows, pcs->log2_sb_size);
    int sb_rows = mi_rows >> pcs->log2_sb_size;
    int sb_size_log2 = pcs->log2_sb_size;
    if (cm->tiles_info.uniform_tile_spacing_flag) {
        int size_sb = HD_ALIGN_POWER_OF_TWO(sb_rows, cm->log2_tile_rows);
        size_sb >>= cm->log2_tile_rows;
        assert(size_sb > 0);
        int i = 0;
        for (int start_sb = 0; start_sb < sb_rows; i++) {
            cm->tiles_info.tile_row_start_mi[i] = (std::uint16_t)(start_sb << sb_size_log2);
            start_sb += size_sb;
        }
        cm->tiles_info.tile_rows = (std::uint8_t)i;
        cm->tiles_info.tile_row_start_mi[i] = (std::uint16_t)(sb_rows << sb_size_log2);
        cm->tile_height = size_sb << pcs->log2_sb_size;
        cm->tile_height = AOMMIN(cm->tile_height, cm->mi_rows);
    } else {
        cm->log2_tile_rows = hdTileLog2(1, cm->tiles_info.tile_rows);
    }
}

// svt_aom_set_tile_info (entropy_coding.c:2537-:2579)
void setTileInfo(HdrPcs* pcs) {
    HdrAv1Common* cm = pcs->av1_cm;
    getTileLimits(pcs);
    // configure tile columns
    cm->tiles_info.uniform_tile_spacing_flag = 1;
    cm->log2_tile_cols = AOMMAX((std::int32_t)pcs->log2_tile_cols, cm->tiles_info.min_log2_tile_cols);
    cm->log2_tile_cols = AOMMIN(cm->log2_tile_cols, cm->tiles_info.max_log2_tile_cols);
    calculateTileCols(pcs);
    // configure tile rows
    if (cm->tiles_info.uniform_tile_spacing_flag) {
        cm->log2_tile_rows = AOMMAX((std::int32_t)pcs->log2_tile_rows, cm->tiles_info.min_log2_tile_rows);
        cm->log2_tile_rows = AOMMIN(cm->log2_tile_rows, cm->tiles_info.max_log2_tile_rows);
    } else {
        int i = 0;
        const int mi_rows = HD_ALIGN_POWER_OF_TWO(cm->mi_rows, pcs->log2_sb_size);
        const int sb_rows = mi_rows >> pcs->log2_sb_size;
        const int sb_size_log2 = pcs->scs->sb_size_log2;
        for (int start_sb = 0; start_sb < sb_rows && i < HD_MAX_TILE_ROWS; i++) {
            cm->tiles_info.tile_row_start_mi[i] = (std::uint16_t)(start_sb << sb_size_log2);
            start_sb += cm->tiles_info.max_tile_height_sb;
        }
        cm->tiles_info.tile_rows = (std::uint8_t)i;
        cm->tiles_info.tile_row_start_mi[i] = (std::uint16_t)(sb_rows << sb_size_log2);
    }
    calculateTileRows(pcs);
}

// write_tile_info_max_tile (entropy_coding.c:2403-:2448). MUTATES
// tiles_info.min_log2_tile_rows at :2418 - 1:1 with the reference.
void writeTileInfoMaxTile(const HdrPcs* pcs, AomWriteBitBuffer* wb) {
    HdrAv1Common* cm = pcs->av1_cm;
    wbWriteBit(wb, cm->tiles_info.uniform_tile_spacing_flag);
    if (cm->tiles_info.uniform_tile_spacing_flag) {
        // Uniform spaced tiles with power-of-two number of rows and columns
        // tile columns
        std::int32_t ones = cm->log2_tile_cols - cm->tiles_info.min_log2_tile_cols;
        while (ones--) {
            wbWriteBit(wb, 1);
        }
        if (cm->log2_tile_cols < cm->tiles_info.max_log2_tile_cols) {
            wbWriteBit(wb, 0);
        }
        // rows
        cm->tiles_info.min_log2_tile_rows =
            (std::uint8_t)AOMMAX(cm->tiles_info.min_log2_tiles - cm->log2_tile_cols, 0);
        ones = cm->log2_tile_rows - cm->tiles_info.min_log2_tile_rows;
        while (ones--) {
            wbWriteBit(wb, 1);
        }
        if (cm->log2_tile_rows < cm->tiles_info.max_log2_tile_rows) {
            wbWriteBit(wb, 0);
        }
    } else {
        // NON uniform_tile_spacing_flag not supported yet (SVT_ERROR shell;
        // the explicit-tiles code is commented out upstream).
        assert(0);
    }
}

// write_tile_info (entropy_coding.c:2581-:2614). Non-const: the reference
// mutates child_pcs->tile_size_bytes_minus_1 (:2584, :2603-:2612).
void writeTileInfo(HdrPcs* pcs, AomWriteBitBuffer* wb) {
    HdrAv1Common* const cm = pcs->av1_cm;
    const std::uint16_t tile_cnt = (std::uint16_t)(cm->tiles_info.tile_rows * cm->tiles_info.tile_cols);
    pcs->tile_size_bytes_minus_1 = 0;
    getTileLimits(pcs);
    writeTileInfoMaxTile(pcs, wb);
    if ((std::uint32_t)cm->tiles_info.tile_rows * cm->tiles_info.tile_cols > 1) {
        // tile id used for cdf update; force each frame to update their data
        wbWriteLiteral(wb, (std::int32_t)(cm->tiles_info.tile_rows * cm->tiles_info.tile_cols - 1),
                       cm->log2_tile_cols + cm->log2_tile_rows);
        // Number of bytes in tile size - 1
        std::uint32_t max_tile_size = 0;
        for (int tile_idx = 0; tile_idx < (int)tile_cnt - 1; tile_idx++) {
            max_tile_size = AOMMAX(max_tile_size, pcs->ec_writer_pos[tile_idx]);
        }
        if (max_tile_size >> 24 != 0) {
            pcs->tile_size_bytes_minus_1 = 3;
        } else if (max_tile_size >> 16 != 0) {
            pcs->tile_size_bytes_minus_1 = 2;
        } else if (max_tile_size >> 8 != 0) {
            pcs->tile_size_bytes_minus_1 = 1;
        } else {
            pcs->tile_size_bytes_minus_1 = 0;
        }
        wbWriteLiteral(wb, pcs->tile_size_bytes_minus_1, 2);  // Jing: Change 3 to smaller size
    }
}
// ---------------------------------------------------------------------------
// C4: the sequence-header writer family + the OBU shells.
// ---------------------------------------------------------------------------
// frame_is_intra_only (entropy_coding.h:61-:63)
int hdFrameIsIntraOnly(const HdrPcs* pcs) {
    return pcs->frm_hdr.frame_type == HD_KEY_FRAME || pcs->frm_hdr.frame_type == HD_INTRA_ONLY_FRAME;
}
// frame_is_sframe (entropy_coding.h:65-:67)
int hdFrameIsSFrame(const HdrPcs* pcs) {
    return pcs->frm_hdr.frame_type == HD_S_FRAME;
}
// frame_might_allow_ref_frame_mvs (entropy_coding.h:71-:74)
int hdFrameMightAllowRefFrameMvs(const HdrPcs* pcs) {
    return !pcs->frm_hdr.error_resilient_mode && pcs->scs->enable_ref_frame_mvs &&
        pcs->scs->enable_order_hint && !hdFrameIsIntraOnly(pcs);
}
// frame_might_allow_warped_motion (entropy_coding.h:77-:79)
int hdFrameMightAllowWarpedMotion(const HdrPcs* pcs) {
    return !pcs->frm_hdr.error_resilient_mode && !hdFrameIsIntraOnly(pcs) &&
        pcs->scs->enable_warped_motion;
}
// av1_superres_unscaled (restoration.h:300-:302)
int hdAv1SuperresUnscaled(const HdrFrameSize* frm_size) {
    return frm_size->frame_width == frm_size->superres_upscaled_width;
}
// major_minor_to_seq_level_idx (entropy_coding.h:81-:84)
std::uint8_t hdMajorMinorToSeqLevelIdx(int major, int minor) {
    return (std::uint8_t)(((major - 2) << 2) + minor);  // LEVEL_MAJOR_MIN 2, LEVEL_MINOR_BITS 2
}
// write_bitstream_level (entropy_coding.c:3693-:3697)
void writeBitstreamLevel(int major, int minor, AomWriteBitBuffer* wb) {
    const std::uint8_t seq_level_idx = hdMajorMinorToSeqLevelIdx(major, minor);
    wbWriteLiteral(wb, seq_level_idx, 5);  // LEVEL_BITS = 3 + 2 (definitions.h:390-:392)
}
// add_trailing_bits (entropy_coding.c:3668-:3675)
void addTrailingBits(AomWriteBitBuffer* wb) {
    if (wbIsByteAligned(wb)) {
        wbWriteLiteral(wb, 0x80, 8);
    } else {
        // assumes that the other bits are already 0s
        wbWriteBit(wb, 1);
    }
}
// write_obu_metadata (entropy_coding.c:3678-:3691). The SvtMetadataT wall is
// threaded: (type, payload, sz); a null/empty payload writes nothing.
std::uint32_t writeObuMetadata(std::int32_t type, const std::uint8_t* payload, std::size_t sz,
                               AomWriteBitBuffer* wb) {
    if (!payload || sz == 0) {
        return 0;
    }
    wbWriteLiteral(wb, type, 8);
    for (std::size_t i = 0; i < sz; ++i) {
        wbWriteLiteral(wb, payload[i], 8);
    }
    addTrailingBits(wb);
    return wbBytesWritten(wb);
}
// write_tile_group_header (entropy_coding.c:3765-:3782)
std::uint32_t writeTileGroupHeader(std::uint8_t* const dst, int startTile, int endTile,
                                   int tiles_log2, int tile_start_and_end_present_flag) {
    AomWriteBitBuffer wb = {dst, 0};
    std::uint32_t size = 0;
    if (!tiles_log2) {
        return size;
    }
    wbWriteBit(&wb, tile_start_and_end_present_flag);
    if (tile_start_and_end_present_flag) {
        wbWriteLiteral(&wb, startTile, tiles_log2);
        wbWriteLiteral(&wb, endTile, tiles_log2);
    }
    size = wbBytesWritten(&wb);
    return size;
}
// get_ref_order_hint (entropy_coding.c:3225-:3231)
std::uint32_t getRefOrderHint(const HdrPcs* pcs, int ref_frame) {
    const int ref_idx = pcs->ref_dpb_index[ref_frame - 1];  // [ref - LAST_FRAME]
    if (ref_idx == 0xFF) {                                  // INVALID_IDX pinned to a byte
        return 0xFFFFFFFFu;
    }
    return pcs->dpb_order_hint[ref_idx];
}
// write_color_config (entropy_coding.c:2687-:2752)
void writeColorConfig(const HdrSeqHeader* sh, AomWriteBitBuffer* wb) {
    writeBitdepth(sh, wb);
    const int is_monochrome = 0;  // monochrome is not supported yet
    // monochrome bit
    if (sh->profile != HD_HIGH_PROFILE) {
        wbWriteBit(wb, is_monochrome);
    } else {
        assert(!is_monochrome);
    }
    if (sh->color_primaries == 2 && sh->transfer_characteristics == 2 &&
        sh->matrix_coefficients == 2) {  // EB_CICP_*_UNSPECIFIED (EbSvtAv1Formats.h:24/:47/:75)
        wbWriteBit(wb, 0);  // No color description present
    } else {
        wbWriteBit(wb, 1);  // Color description present
        wbWriteLiteral(wb, sh->color_primaries, 8);
        wbWriteLiteral(wb, sh->transfer_characteristics, 8);
        wbWriteLiteral(wb, sh->matrix_coefficients, 8);
    }
    /* if (is_monochrome) { ... } - commented out upstream */
    if (sh->color_primaries == 1 && sh->transfer_characteristics == 13 &&
        sh->matrix_coefficients == 0) {  // BT_709 / SRGB / IDENTITY
        /* commented-out asserts upstream */
    } else {
        // 0: [16, 235] (i.e. xvYCC), 1: [0, 255]
        wbWriteBit(wb, sh->color_range);
        if (sh->profile == HD_MAIN_PROFILE) {
            // 420 only
            assert(sh->subsampling_x == 1 && sh->subsampling_y == 1);
        } else if (sh->profile == HD_HIGH_PROFILE) {
            // 444 only
            assert(sh->subsampling_x == 0 && sh->subsampling_y == 0);
        } else if (sh->profile == HD_PROFESSIONAL_PROFILE) {
            if (sh->encoder_bit_depth == HD_EB_TWELVE_BIT) {
                // 420, 444 or 422
                wbWriteBit(wb, sh->subsampling_x);
                if (sh->subsampling_x == 0) {
                    assert(sh->subsampling_y == 0 && "4:4:0 subsampling not allowed in AV1");
                } else {
                    wbWriteBit(wb, sh->subsampling_y);
                }
            } else {
                // 422 only
                assert(sh->subsampling_x == 1 && sh->subsampling_y == 0);
            }
        }
        if (sh->matrix_coefficients == 0) {  // EB_CICP_MC_IDENTITY
            assert(sh->subsampling_x == 0 && sh->subsampling_y == 0);
        }
        if (sh->subsampling_x == 1 && sh->subsampling_y == 1) {
            wbWriteLiteral(wb, sh->chroma_sample_position, 2);
        }
    }
    const bool separate_uv_delta_q = (sh->chroma_u_ac_qindex_offset !=
                                      sh->chroma_v_ac_qindex_offset ||
                                      sh->chroma_u_dc_qindex_offset !=
                                          sh->chroma_v_dc_qindex_offset);
    wbWriteBit(wb, separate_uv_delta_q);
}

// write_sequence_header (entropy_coding.c:2754-:2839)
void writeSequenceHeader(HdrSeqHeader* sh, AomWriteBitBuffer* wb) {
    const std::int32_t max_frame_width = sh->max_frame_width;
    const std::int32_t max_frame_height = sh->max_frame_height;
    unsigned frame_width_bits = (unsigned)getMsb((std::uint32_t)max_frame_width);
    unsigned frame_height_bits = (unsigned)getMsb((std::uint32_t)max_frame_height);
    if (max_frame_width > (1 << frame_width_bits)) {
        ++frame_width_bits;
    }
    if (max_frame_height > (1 << frame_height_bits)) {
        ++frame_height_bits;
    }
    // AV1 spec requires at least 1 bit for frame dimensions
    if (frame_width_bits < 1) {
        frame_width_bits = 1;
    }
    if (frame_height_bits < 1) {
        frame_height_bits = 1;
    }
    sh->frame_width_bits = (int)frame_width_bits;
    sh->frame_height_bits = (int)frame_height_bits;

    wbWriteLiteral(wb, frame_width_bits - 1, 4);
    wbWriteLiteral(wb, frame_height_bits - 1, 4);
    wbWriteLiteral(wb, max_frame_width - 1, frame_width_bits);
    wbWriteLiteral(wb, max_frame_height - 1, frame_height_bits);

    if (!sh->reduced_still_picture_header) {
        wbWriteBit(wb, sh->frame_id_numbers_present_flag);
        if (sh->frame_id_numbers_present_flag) {
            // We must always have delta_frame_id_length < frame_id_length,
            // in order for a frame to be referenced with a unique delta.
            // Avoid wasting bits by using a coding that enforces this restriction.
            wbWriteLiteral(wb, sh->delta_frame_id_length - 2, 4);
            wbWriteLiteral(wb,
                           (sh->frame_id_length) - (sh->delta_frame_id_length) - 1, 3);
        }
    }

    wbWriteBit(wb, sh->sb_size_is_128 ? 1 : 0);
    wbWriteBit(wb, sh->filter_intra_level);
    wbWriteBit(wb, sh->enable_intra_edge_filter);

    if (!sh->reduced_still_picture_header) {
        wbWriteBit(wb, sh->enable_interintra_compound);
        wbWriteBit(wb, sh->enable_masked_compound);
        wbWriteBit(wb, sh->enable_warped_motion);
        wbWriteBit(wb, sh->enable_dual_filter);

        wbWriteBit(wb, sh->enable_order_hint);

        if (sh->enable_order_hint) {
            wbWriteBit(wb, sh->enable_jnt_comp);
            wbWriteBit(wb, sh->enable_ref_frame_mvs);
        }

        if (sh->seq_force_screen_content_tools == 2) {
            wbWriteBit(wb, 1);
        } else {
            wbWriteBit(wb, 0);
            wbWriteBit(wb, sh->seq_force_screen_content_tools);
        }
        //
        if (sh->seq_force_screen_content_tools > 0) {
            if (sh->seq_force_integer_mv == 2) {
                wbWriteBit(wb, 1);
            } else {
                wbWriteBit(wb, 0);
                wbWriteBit(wb, sh->seq_force_integer_mv);
            }
        } else {
            assert(sh->seq_force_integer_mv == 2);
        }
        if (sh->enable_order_hint) {
            wbWriteLiteral(wb, sh->order_hint_bits - 1, 3);
        }
    }

    wbWriteBit(wb, sh->enable_superres);
    wbWriteBit(wb, sh->cdef_level);
    wbWriteBit(wb, sh->enable_restoration);
}
// default_warp_params (definitions.h:1774-:1779)
static HdrWarpParams hdDefaultWarpParams() {
    HdrWarpParams p;
    p.wmtype = (TransformationType)0;  // IDENTITY
    p.wmmat[0] = 0;
    p.wmmat[1] = 0;
    p.wmmat[2] = 1 << 16;  // WARPEDMODEL_PREC_BITS
    p.wmmat[3] = 0;
    p.wmmat[4] = 0;
    p.wmmat[5] = 1 << 16;
    return p;
}
// svt_aom_film_grain_params_equal (grainSynthesis.c:709-:793) - the writer
// consumed surface only: the fields the reference compares before the
// update_parameters decision. DEVIATION (named): the reference compares the
// FULL struct field-by-field; the fields below are all of them in the
// HdrFilmGrainParams carrier.
static bool grainParamsEqual(const HdrFilmGrainParams* a, const HdrFilmGrainParams* b) {
    return a->apply_grain == b->apply_grain && a->num_y_points == b->num_y_points &&
        a->num_cb_points == b->num_cb_points && a->num_cr_points == b->num_cr_points &&
        a->scaling_shift == b->scaling_shift && a->ar_coeff_lag == b->ar_coeff_lag &&
        a->ar_coeff_shift == b->ar_coeff_shift && a->grain_scale_shift == b->grain_scale_shift &&
        a->random_seed == b->random_seed &&
        a->chroma_scaling_from_luma == b->chroma_scaling_from_luma &&
        a->overlap_flag == b->overlap_flag && a->clip_to_restricted_range == b->clip_to_restricted_range &&
        a->cb_mult == b->cb_mult && a->cb_luma_mult == b->cb_luma_mult && a->cb_offset == b->cb_offset &&
        a->cr_mult == b->cr_mult && a->cr_luma_mult == b->cr_luma_mult && a->cr_offset == b->cr_offset;
}
// write_global_motion_params (entropy_coding.c:2996-:3062)
void writeGlobalMotionParams(const HdrWarpParams* params, const HdrWarpParams* ref_params,
                             AomWriteBitBuffer* wb, std::int32_t allow_hp) {
    const int type = params->wmtype;  // TransformationType
    wbWriteBit(wb, type != 0);        // IDENTITY
    if (type != 0) {
        wbWriteBit(wb, type == 2);  // ROTZOOM
        if (type != 2) {
            wbWriteBit(wb, type == 1);  // TRANSLATION
        }
    }
    if (type >= 2) {  // ROTZOOM
        std::int16_t ref2 = (std::int16_t)((ref_params->wmmat[2] >> HD_GM_ALPHA_PREC_DIFF) -
                                           (1 << HD_GM_ALPHA_PREC_BITS));
        std::int16_t v2 = (std::int16_t)((params->wmmat[2] >> HD_GM_ALPHA_PREC_DIFF) -
                                         (1 << HD_GM_ALPHA_PREC_BITS));
        std::int16_t ref3 = (std::int16_t)(ref_params->wmmat[3] >> HD_GM_ALPHA_PREC_DIFF);
        std::int16_t v3 = (std::int16_t)(params->wmmat[3] >> HD_GM_ALPHA_PREC_DIFF);
        wbWriteSignedPrimitiveRefsubexpfin(wb, HD_GM_ALPHA_MAX + 1, HD_SUBEXPFIN_K, ref2, v2);
        wbWriteSignedPrimitiveRefsubexpfin(wb, HD_GM_ALPHA_MAX + 1, HD_SUBEXPFIN_K, ref3, v3);
    }
    if (type >= 3) {  // AFFINE
        std::int16_t ref4 = (std::int16_t)(ref_params->wmmat[4] >> HD_GM_ALPHA_PREC_DIFF);
        std::int16_t v4 = (std::int16_t)(params->wmmat[4] >> HD_GM_ALPHA_PREC_DIFF);
        std::int16_t ref5 = (std::int16_t)((ref_params->wmmat[5] >> HD_GM_ALPHA_PREC_DIFF) -
                                          (1 << HD_GM_ALPHA_PREC_BITS));
        std::int16_t v5 = (std::int16_t)((params->wmmat[5] >> HD_GM_ALPHA_PREC_DIFF) -
                                         (1 << HD_GM_ALPHA_PREC_BITS));
        wbWriteSignedPrimitiveRefsubexpfin(wb, HD_GM_ALPHA_MAX + 1, HD_SUBEXPFIN_K, ref4, v4);
        wbWriteSignedPrimitiveRefsubexpfin(wb, HD_GM_ALPHA_MAX + 1, HD_SUBEXPFIN_K, ref5, v5);
    }
    if (type >= 1) {  // TRANSLATION
        const std::int32_t trans_bits = (type == 1) ? HD_GM_ABS_TRANS_ONLY_BITS - !allow_hp
                                                    : HD_GM_ABS_TRANS_BITS;
        const std::int32_t trans_prec_diff = (type == 1) ? HD_GM_TRANS_ONLY_PREC_DIFF + !allow_hp
                                                         : HD_GM_TRANS_PREC_DIFF;
        wbWriteSignedPrimitiveRefsubexpfin(wb, (std::uint16_t)((1 << trans_bits) + 1),
                                           HD_SUBEXPFIN_K,
                                           (std::int16_t)(ref_params->wmmat[0] >> trans_prec_diff),
                                           (std::int16_t)(params->wmmat[0] >> trans_prec_diff));
        wbWriteSignedPrimitiveRefsubexpfin(wb, (std::uint16_t)((1 << trans_bits) + 1),
                                           HD_SUBEXPFIN_K,
                                           (std::int16_t)(ref_params->wmmat[1] >> trans_prec_diff),
                                           (std::int16_t)(params->wmmat[1] >> trans_prec_diff));
    }
}

// write_global_motion (entropy_coding.c:3064-:3097)
void writeGlobalMotion(HdrPcs* pcs, AomWriteBitBuffer* wb) {
    static const HdrWarpParams hd_warp_default = hdDefaultWarpParams();
    const HdrFrameHeader* frm_hdr = &pcs->frm_hdr;
    for (int frame = 1; frame <= 7; ++frame) {  // LAST_FRAME..ALTREF_FRAME
        const HdrWarpParams* ref_params = (frm_hdr->primary_ref_frame != HD_PRIMARY_REF_NONE)
            ? &pcs->ref_global_motion[frame]
            : &hd_warp_default;
        writeGlobalMotionParams(&pcs->global_motion[frame], ref_params, wb,
                                frm_hdr->allow_high_precision_mv);
    }
}

// write_film_grain_params (entropy_coding.c:3100-:3222). The EbReferenceObject
// wall is threaded: (ref_grain_params, ignore_ref_fact); the
// get_ref_frame_map_idx wall is threaded: ref_idx (3 bits when the params
// are not updated).
void writeFilmGrainParams(HdrFilmGrainParams* pars, AomWriteBitBuffer* wb,
                          const HdrFilmGrainParams* ref_obj_0, const HdrFilmGrainParams* ref_obj_1,
                          int slice_is_b, int ref_idx_0, int ref_idx_1, int frame_type_is_inter,
                          int seq_color_mono, int scs_subsampling_x, int scs_subsampling_y) {
    const HdrFrameHeader* frm_hdr = NULL;
    (void)frm_hdr;
    wbWriteBit(wb, pars->apply_grain);
    if (!pars->apply_grain) {
        return;
    }
    wbWriteLiteral(wb, pars->random_seed, 16);
    if (frame_type_is_inter) {
        pars->update_parameters = 1;
        if (!pars->ignore_ref) {
            if (grainParamsEqual(ref_obj_0, pars)) {
                pars->update_parameters = 0;
                wbWriteBit(wb, pars->update_parameters);
                wbWriteLiteral(wb, ref_idx_0, 3);
                return;
            } else if (slice_is_b) {
                if (grainParamsEqual(ref_obj_1, pars)) {
                    pars->update_parameters = 0;
                    wbWriteBit(wb, pars->update_parameters);
                    wbWriteLiteral(wb, ref_idx_1, 3);
                    return;
                }
            }
        }
        wbWriteBit(wb, pars->update_parameters);
        if (!pars->update_parameters) {
            return;
        }
    } else {
        pars->update_parameters = 1;
    }
    // Scaling functions parameters
    wbWriteLiteral(wb, pars->num_y_points, 4);  // max 14
    for (std::int32_t i = 0; i < pars->num_y_points; i++) {
        wbWriteLiteral(wb, pars->scaling_points_y[i][0], 8);
        wbWriteLiteral(wb, pars->scaling_points_y[i][1], 8);
    }
    if (!seq_color_mono) {
        wbWriteBit(wb, pars->chroma_scaling_from_luma);
    }
    if (seq_color_mono || pars->chroma_scaling_from_luma ||
        ((scs_subsampling_x == 1) && (scs_subsampling_y == 1) && (pars->num_y_points == 0))) {
        pars->num_cb_points = 0;
        pars->num_cr_points = 0;
    } else {
        wbWriteLiteral(wb, pars->num_cb_points, 4);  // max 10
        for (std::int32_t i = 0; i < pars->num_cb_points; i++) {
            wbWriteLiteral(wb, pars->scaling_points_cb[i][0], 8);
            wbWriteLiteral(wb, pars->scaling_points_cb[i][1], 8);
        }
        wbWriteLiteral(wb, pars->num_cr_points, 4);  // max 10
        for (std::int32_t i = 0; i < pars->num_cr_points; i++) {
            wbWriteLiteral(wb, pars->scaling_points_cr[i][0], 8);
            wbWriteLiteral(wb, pars->scaling_points_cr[i][1], 8);
        }
    }
    wbWriteLiteral(wb, pars->scaling_shift - 8, 2);  // 8 + value
    // AR coefficients
    wbWriteLiteral(wb, pars->ar_coeff_lag, 2);
    std::int32_t num_pos_luma = 2 * pars->ar_coeff_lag * (pars->ar_coeff_lag + 1);
    std::int32_t num_pos_chroma = num_pos_luma;
    if (pars->num_y_points > 0) {
        ++num_pos_chroma;
    }
    if (pars->num_y_points) {
        for (std::int32_t i = 0; i < num_pos_luma; i++) {
            wbWriteLiteral(wb, pars->ar_coeffs_y[i] + 128, 8);
        }
    }
    if (pars->num_cb_points || pars->chroma_scaling_from_luma) {
        for (std::int32_t i = 0; i < num_pos_chroma; i++) {
            wbWriteLiteral(wb, pars->ar_coeffs_cb[i] + 128, 8);
        }
    }
    if (pars->num_cr_points || pars->chroma_scaling_from_luma) {
        for (std::int32_t i = 0; i < num_pos_chroma; i++) {
            wbWriteLiteral(wb, pars->ar_coeffs_cr[i] + 128, 8);
        }
    }
    wbWriteLiteral(wb, pars->ar_coeff_shift - 6, 2);  // 8 + value
    wbWriteLiteral(wb, pars->grain_scale_shift, 2);
    if (pars->num_cb_points) {
        wbWriteLiteral(wb, pars->cb_mult, 8);
        wbWriteLiteral(wb, pars->cb_luma_mult, 8);
        wbWriteLiteral(wb, pars->cb_offset, 9);
    }
    if (pars->num_cr_points) {
        wbWriteLiteral(wb, pars->cr_mult, 8);
        wbWriteLiteral(wb, pars->cr_luma_mult, 8);
        wbWriteLiteral(wb, pars->cr_offset, 9);
    }
    wbWriteBit(wb, pars->overlap_flag);
    wbWriteBit(wb, pars->clip_to_restricted_range);
}
// write_frame_size_with_refs (entropy_coding.c:3233-:3291). The
// EbReferenceObject wall is threaded: (ref_order_hint[7], ref_dims_ok[7])
// per ref slot - ref_dims_ok is the spec 5.9.7 "both the super-res upscaled
// size and the render size should be checked" fact, pinned to 1 by the
// caller (write_render_size fixes render_and_frame_size_different to 0).
void writeFrameSizeWithRefs(HdrPcs* pcs, AomWriteBitBuffer* wb,
                            const std::uint32_t ref_order_hint[7], const int ref_dims_ok[7]) {
    (void)pcs;
    for (std::uint32_t ref_frame = 1; ref_frame <= 7; ++ref_frame) {  // LAST..ALTREF
        int found = 0;
        const std::uint32_t ref_hint = ref_order_hint[ref_frame - 1];
        if ((std::int32_t)ref_hint != 0xFFFFFFFFu) {
            found = ref_dims_ok[ref_frame - 1];
        }
        wbWriteBit(wb, found);
        if (found) {
            writeSuperresScale(wb, pcs);
            return;
        }
    }
    // not found
    const int frame_size_override = 1;  // Always equal to 1 in this function
    writeFrameSize(pcs, frame_size_override, wb);
}
// write_uncompressed_header_obu (entropy_coding.c:3294-:3637). The pcs
// side-effect walls are carried: fb_of_context_type / prev_qindex /
// prev_delta_lf* / refresh_frame_context mutations on HdrPcs; the
// segmentation grid goes through the landed writeSegFeatures; the film
// grain wall is threaded (fg carrier).
void writeUncompressedHeaderObu(HdrSeqHeader* sh, HdrPcs* pcs, AomWriteBitBuffer* wb,
                                std::uint8_t show_existing) {
    HdrAv1Common* const cm = pcs->av1_cm;
    const std::uint16_t tile_cnt =
        (std::uint16_t)(cm->tiles_info.tile_rows * cm->tiles_info.tile_cols);
    (void)tile_cnt;
    HdrFrameHeader* frm_hdr = &pcs->frm_hdr;
    if (!sh->reduced_still_picture_header) {
        if (show_existing) {
            wbWriteBit(wb, 1);  // show_existing_frame
            wbWriteLiteral(wb, frm_hdr->show_existing_frame, 3);
            if (sh->frame_id_numbers_present_flag) {
                assert(0);  // frame_id_numbers_present_flag not supported (SVT_ERROR shell)
            }
            return;
        } else {
            wbWriteBit(wb, 0);  // show_existing_frame
        }
        wbWriteLiteral(wb, frm_hdr->frame_type, 2);
        wbWriteBit(wb, frm_hdr->show_frame);
        if (!frm_hdr->show_frame) {
            wbWriteBit(wb, frm_hdr->showable_frame);
        }
        if (frm_hdr->frame_type == HD_S_FRAME) {
            assert(frm_hdr->error_resilient_mode);
        } else if (!(frm_hdr->frame_type == HD_KEY_FRAME && frm_hdr->show_frame)) {
            wbWriteBit(wb, frm_hdr->error_resilient_mode);
        }
    }

    wbWriteBit(wb, frm_hdr->disable_cdf_update);

    if (sh->seq_force_screen_content_tools == 2) {
        wbWriteBit(wb, frm_hdr->allow_screen_content_tools);
    } else {
        assert(frm_hdr->allow_screen_content_tools == sh->seq_force_screen_content_tools);
    }

    if (frm_hdr->allow_screen_content_tools) {
        if (sh->seq_force_integer_mv == 2) {
            wbWriteBit(wb, frm_hdr->force_integer_mv);
        } else {
            assert(frm_hdr->force_integer_mv == sh->seq_force_integer_mv);
        }
    } else {
        assert(frm_hdr->force_integer_mv == 0);
    }

    const std::int32_t frame_size_override_flag =
        hdFrameIsSFrame(pcs) || pcs->frame_resize_enabled
        ? 1
        : ((pcs->av1_cm->frm_size.superres_upscaled_width != sh->max_frame_width) ||
           (pcs->av1_cm->frm_size.superres_upscaled_height != sh->max_frame_height));

    if (!sh->reduced_still_picture_header) {
        if (sh->frame_id_numbers_present_flag) {
            const std::int32_t frame_id_len = sh->frame_id_length;
            wbWriteLiteral(wb, frm_hdr->current_frame_id, frame_id_len);
        }
        if (!hdFrameIsSFrame(pcs)) {
            wbWriteBit(wb, frame_size_override_flag);
        }
        if (sh->enable_order_hint) {
            wbWriteLiteral(wb, (std::int32_t)pcs->frame_offset, sh->order_hint_bits);
        }
        if (!frm_hdr->error_resilient_mode && !hdFrameIsIntraOnly(pcs)) {
            wbWriteLiteral(wb, frm_hdr->primary_ref_frame, HD_PRIMARY_REF_BITS);
        }
    } else {  // reduced_still_picture_header
        assert(frame_size_override_flag == 0);
    }
    if (frm_hdr->frame_type == HD_KEY_FRAME) {
        if (!frm_hdr->show_frame) {
            wbWriteLiteral(wb, pcs->refresh_frame_mask, 8 /* REF_FRAMES */);
        }
    } else {
        if (frm_hdr->frame_type == HD_INTRA_ONLY_FRAME) {
            std::int32_t updated_fb = -1;
            for (std::int32_t i = 0; i < 8; ++i) {  // REF_FRAMES
                // If more than one frame is refreshed, it doesn't matter which one
                // we pick, so pick the first.
                if (pcs->refresh_frame_mask & (1 << i)) {
                    updated_fb = i;
                    break;
                }
            }
            assert(updated_fb >= 0);
            pcs->fb_of_context_type[pcs->frame_context_idx] = updated_fb;
            wbWriteLiteral(wb, pcs->refresh_frame_mask, 8 /* REF_FRAMES */);
        } else if (frm_hdr->frame_type == HD_INTER_FRAME || hdFrameIsSFrame(pcs)) {
            if (frm_hdr->frame_type == HD_INTER_FRAME) {
                wbWriteLiteral(wb, pcs->refresh_frame_mask, 8 /* REF_FRAMES */);
            } else {
                assert(hdFrameIsSFrame(pcs) && pcs->refresh_frame_mask == 0xFF);
            }
            // write ref order hint map into bitstream
            if (frm_hdr->error_resilient_mode && sh->enable_order_hint) {
                for (std::int32_t ref_idx = 0; ref_idx < 8; ++ref_idx) {  // REF_FRAMES
                    wbWriteLiteral(wb, (std::int32_t)pcs->dpb_order_hint[ref_idx],
                                   sh->order_hint_bits);
                }
            }
            std::int32_t updated_fb = -1;
            for (std::int32_t i = 0; i < 8; ++i) {  // REF_FRAMES
                // If more than one frame is refreshed, it doesn't matter which one
                // we pick, so pick the first.
                if (pcs->refresh_frame_mask & (1 << i)) {
                    updated_fb = i;
                    break;
                }
            }
            // large scale tile sometimes won't refresh any fbs
            if (updated_fb >= 0) {
                pcs->fb_of_context_type[pcs->frame_context_idx] = updated_fb;
            }
        }
    }

    if (frm_hdr->frame_type == HD_KEY_FRAME) {
        writeFrameSize(pcs, frame_size_override_flag, wb);
        assert(hdAv1SuperresUnscaled(&(pcs->av1_cm->frm_size)) || !(frm_hdr->allow_intrabc));
        if (frm_hdr->allow_screen_content_tools && hdAv1SuperresUnscaled(&(pcs->av1_cm->frm_size))) {
            wbWriteBit(wb, frm_hdr->allow_intrabc);
        }
        // all eight fbs are refreshed, pick one that will live long enough
        pcs->fb_of_context_type[HD_REGULAR_FRAME] = 0;
    } else {
        if (frm_hdr->frame_type == HD_INTRA_ONLY_FRAME) {
            writeFrameSize(pcs, frame_size_override_flag, wb);
            assert(hdAv1SuperresUnscaled(&(pcs->av1_cm->frm_size)) || !(frm_hdr->allow_intrabc));
            if (frm_hdr->allow_screen_content_tools &&
                hdAv1SuperresUnscaled(&(pcs->av1_cm->frm_size))) {
                wbWriteBit(wb, frm_hdr->allow_intrabc);
            }
        } else if (frm_hdr->frame_type == HD_INTER_FRAME || hdFrameIsSFrame(pcs)) {
            assert(frm_hdr->frame_refs_short_signaling == 0);
            // NOTE: Error resilient mode turns off frame_refs_short_signaling
            //       automatically.
            if (sh->enable_order_hint) {
                wbWriteBit(wb, frm_hdr->frame_refs_short_signaling);
            }
            if (frm_hdr->frame_refs_short_signaling) {
                wbWriteLiteral(wb, pcs->ref_dpb_index[0], HD_REF_FRAMES_LOG2);   // LAST
                wbWriteLiteral(wb, pcs->ref_dpb_index[3], HD_REF_FRAMES_LOG2);   // GOLDEN
            }
            for (std::int32_t ref_frame = 1; ref_frame <= 7; ++ref_frame) {  // LAST..ALTREF
                assert(pcs->ref_dpb_index[ref_frame - 1] != 0xFF);
                if (!frm_hdr->frame_refs_short_signaling) {
                    wbWriteLiteral(wb, pcs->ref_dpb_index[ref_frame - 1], HD_REF_FRAMES_LOG2);
                }
            }
            if (!pcs->frm_hdr.error_resilient_mode && frame_size_override_flag) {
                // write_frame_size_with_refs: the ref order hints + dims facts
                // (threaded; the dims fact is pinned per the spec 5.9.7 note).
                std::uint32_t hints[7];
                int dims_ok[7];
                for (int i = 0; i < 7; ++i) {
                    hints[i] = pcs->ref_dpb_index[i] == 0xFF ? 0xFFFFFFFFu
                                                             : pcs->dpb_order_hint[pcs->ref_dpb_index[i]];
                    dims_ok[i] = 1;
                }
                writeFrameSizeWithRefs(pcs, wb, hints, dims_ok);
            } else {
                writeFrameSize(pcs, frame_size_override_flag, wb);
            }
            if (frm_hdr->force_integer_mv) {
                frm_hdr->allow_high_precision_mv = 0;
            } else {
                wbWriteBit(wb, frm_hdr->allow_high_precision_mv);
            }
#define HD_LOG_SWITCHABLE_FILTERS 2
            wbWriteBit(wb, pcs->frm_hdr.interpolation_filter == HD_INTERP_FILTER_SWITCHABLE);
            if (pcs->frm_hdr.interpolation_filter != HD_INTERP_FILTER_SWITCHABLE) {
                wbWriteLiteral(wb, pcs->frm_hdr.interpolation_filter, HD_LOG_SWITCHABLE_FILTERS);
            }
            wbWriteBit(wb, frm_hdr->is_motion_mode_switchable);
            if (hdFrameMightAllowRefFrameMvs(pcs)) {
                wbWriteBit(wb, frm_hdr->use_ref_frame_mvs);
            }
        }
    }

    const std::int32_t might_bwd_adapt =
        !(sh->reduced_still_picture_header) && !(frm_hdr->disable_cdf_update);
    if (pcs->large_scale_tile) {
        pcs->refresh_frame_context = 0;  // REFRESH_FRAME_CONTEXT_DISABLED
    }
    if (might_bwd_adapt) {
        wbWriteBit(wb, pcs->refresh_frame_context == 0);
    }

    writeTileInfo(pcs, wb);

    encodeQuantization(pcs, wb);
    // encode_segmentation :2253-:2282 - the landed SYM1 writeSegFeatures.
    writeSegFeatures(wb, pcs->frm_hdr.segmentation_params.segmentation_enabled,
                     pcs->frm_hdr.segmentation_params.segmentation_update_map,
                     pcs->frm_hdr.segmentation_params.segmentation_temporal_update,
                     pcs->frm_hdr.segmentation_params.segmentation_update_data,
                     pcs->frm_hdr.primary_ref_frame,
                     pcs->frm_hdr.segmentation_params.feature_enabled,
                     pcs->frm_hdr.segmentation_params.feature_data);

    if (frm_hdr->quantization_params.base_q_idx > 0) {
        wbWriteBit(wb, frm_hdr->delta_q_params.delta_q_present);
        if (frm_hdr->delta_q_params.delta_q_present) {
            wbWriteLiteral(wb, getMsb((std::uint32_t)frm_hdr->delta_q_params.delta_q_res | 1), 2);
            for (std::uint16_t tile_idx = 0; tile_idx < tile_cnt; tile_idx++) {
                pcs->prev_qindex[tile_idx] = frm_hdr->quantization_params.base_q_idx;
            }
            if (frm_hdr->allow_intrabc) {
                assert(frm_hdr->delta_lf_params.delta_lf_present == 0);
            } else {
                wbWriteBit(wb, frm_hdr->delta_lf_params.delta_lf_present);
            }
            if (frm_hdr->delta_lf_params.delta_lf_present) {
                wbWriteLiteral(wb, getMsb((std::uint32_t)frm_hdr->delta_lf_params.delta_lf_res | 1), 2);
                pcs->prev_delta_lf_from_base = 0;
                wbWriteBit(wb, frm_hdr->delta_lf_params.delta_lf_multi);
                const std::int32_t frame_lf_count = pcs->monochrome == 0 ? HD_FRAME_LF_COUNT
                                                                         : HD_FRAME_LF_COUNT - 2;
                for (std::int32_t lf_id = 0; lf_id < frame_lf_count; ++lf_id) {
                    pcs->prev_delta_lf[lf_id] = 0;
                }
            }
        }
    }

    if (frm_hdr->all_lossless) {
        assert(hdAv1SuperresUnscaled(&(pcs->av1_cm->frm_size)));
    } else {
        if (!frm_hdr->coded_lossless) {
            encodeLoopfilter(pcs, wb);
            if (sh->cdef_level) {
                encodeCdef(pcs, wb);
            }
        }
        if (sh->enable_restoration) {
            encodeRestorationMode(pcs, wb);
        }
    }
    if (frm_hdr->coded_lossless) {
        assert(1);  // assert(frm_hdr->tx_mode == ONLY_4X4)
    } else {
        wbWriteBit(wb, frm_hdr->tx_mode == HD_TX_MODE_SELECT);
    }

    if (pcs->allow_comp_inter_inter) {
        const std::int32_t use_hybrid_pred =
            frm_hdr->reference_mode == HD_REFERENCE_MODE_SELECT;
        wbWriteBit(wb, use_hybrid_pred);
    }

    if (frm_hdr->skip_mode_params.skip_mode_allowed) {
        wbWriteBit(wb, frm_hdr->skip_mode_params.skip_mode_flag);
    }

    if (hdFrameMightAllowWarpedMotion(pcs)) {
        wbWriteBit(wb, frm_hdr->allow_warped_motion);
    } else {
        assert(!frm_hdr->allow_warped_motion);
    }

    wbWriteBit(wb, frm_hdr->reduced_tx_set);

    if (!hdFrameIsIntraOnly(pcs)) {
        writeGlobalMotion(pcs, wb);
    }
    // CONFIG_ENABLE_FILM_GRAIN = 1 (EbConfigMacros.h:170-:172, the pinned
    // build): the write is gated by the seq flag + the show bits (:3633-:3635).
    if (sh->film_grain_params_present && (frm_hdr->show_frame || frm_hdr->showable_frame)) {
        const int is_mono = sh->subsampling_x == 0 ? 1 : 0;  // the mono override fact
        writeFilmGrainParams(&pcs->fg, wb, &pcs->fg_ref_0, &pcs->fg_ref_1, pcs->fg_slice_is_b,
                             pcs->fg_ref_idx_0, pcs->fg_ref_idx_1,
                             frm_hdr->frame_type == HD_INTER_FRAME, is_mono, sh->subsampling_x,
                             sh->subsampling_y);
    }
}
// write_sequence_header_obu (entropy_coding.c:3699-:3763). set_bitstream_level_tier
// (:1111) is PARKED (the level-derivation search, not a bit writer): scs->level
// is threaded as the carrier input (level_major/level_minor). OP_POINTS_
// CNT_MINUS_1_BITS 5 / OP_POINTS_IDC_BITS 12 (definitions.h:399-:400); the
// decoder_model_info branch is the reference's SVT_ERROR shell (:3737-:3743).
std::uint32_t writeSequenceHeaderObu(HdrSeqHeader* sh, std::uint8_t* const dst,
                                     std::uint8_t number_spatial_layers) {
    AomWriteBitBuffer wb = {dst, 0};
    std::uint32_t size = 0;
    writeProfile(sh->profile, &wb);
    wbWriteBit(&wb, sh->still_picture);
    assert(!(!sh->still_picture) || !(bool)sh->reduced_still_picture_header);  // IMPLIES
    wbWriteBit(&wb, sh->reduced_still_picture_header);
    if (sh->reduced_still_picture_header) {
        writeBitstreamLevel(sh->level_major, sh->level_minor, &wb);
    } else {
        wbWriteBit(&wb, 0);  // timing_info_present (the reference SVT_ERRORs on 1)
        wbWriteBit(&wb, sh->initial_display_delay_present_flag);
        const std::uint8_t operating_points_cnt_minus_1 =
            number_spatial_layers > 1 ? (std::uint8_t)(number_spatial_layers - 1) : 0;
        wbWriteLiteral(&wb, operating_points_cnt_minus_1, 5);   // OP_POINTS_CNT_MINUS_1_BITS
        for (std::int32_t i = 0; i < operating_points_cnt_minus_1 + 1; i++) {
            wbWriteLiteral(&wb, sh->op_idc, 12);                // OP_POINTS_IDC_BITS
            writeBitstreamLevel(sh->level_major, sh->level_minor, &wb);
            if (sh->level_major > 3) {
                wbWriteBit(&wb, sh->seq_tier);
            }
            if (sh->decoder_model_info_present_flag) {
                assert(0);  // decoder_model_info_present_flag not supported (SVT_ERROR shell)
            }
            if (sh->initial_display_delay_present_flag) {
                wbWriteBit(&wb, sh->initial_display_delay_present_for_this_op);
                if (sh->initial_display_delay_present_for_this_op) {
                    assert(sh->initial_display_delay <= 10);
                    wbWriteLiteral(&wb, sh->initial_display_delay - 1, 4);
                }
            }
        }
    }
    writeSequenceHeader(sh, &wb);
    writeColorConfig(sh, &wb);
    wbWriteBit(&wb, sh->film_grain_params_present);
    addTrailingBits(&wb);
    size = wbBytesWritten(&wb);
    return size;
}

// write_frame_header_obu (entropy_coding.c:3784-:3802)
std::uint32_t writeFrameHeaderObu(HdrSeqHeader* sh, HdrPcs* pcs, std::uint8_t* const dst,
                                  std::uint8_t show_existing, std::int32_t appendTrailingBits) {
    AomWriteBitBuffer wb = {dst, 0};
    std::uint32_t total_size = 0;
    writeUncompressedHeaderObu(sh, pcs, &wb, show_existing);
    if (appendTrailingBits) {
        addTrailingBits(&wb);
    }
    if (show_existing) {
        total_size = wbBytesWritten(&wb);
        return total_size;
    }
    total_size = wbBytesWritten(&wb);
    return total_size;
}
}  // namespace entropy
