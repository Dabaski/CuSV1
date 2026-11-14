// A5H - the frame-header / tile-info writer surface of entropy_coding.c.
// Reference: third_party/SVT-AV1/Source/Lib/Codec/entropy_coding.c
// :2189-:3960 (the writer family between the landed intra/inter/chroma
// symbol surfaces and the OBU shells that live in l8_bitstream).
//
// Carriers (the RE8A DeblockContext precedent, AGENTS.md): the
// PictureParentControlSet / SequenceControlSet / Av1Common walls each span
// consumes are mirrored into HdrPcs / HdrSeqHeader / HdrAv1Common; the
// sub-structs (TilesInfo, FrameSize, QuantizationParams, LoopFilter,
// CdefParams, DeltaQParams, DeltaLfParams, RestorationInfo, WarpedMotionParams,
// AomFilmGrain) are mirrored field-for-field with citations.
//
// Pinned build config (EbConfigMacros.h:167-226, the non-RTC branch):
// CONFIG_ENABLE_FILM_GRAIN / CONFIG_ENABLE_HIGH_BIT_DEPTH / CONFIG_ENABLE_
// SUPERRES / CONFIG_ENABLE_GLOBAL_MOTION / CONFIG_ENABLE_RESTORATION are all
// 1, so those const-fold guards are LIVE behavior and the port keeps their
// bodies. SVT_EFFECTIVE_BIT_DEPTH is the identity (EbConfigMacros.h:293).

#pragma once

#include "entropy.h"  // AomWriter/AomWriteBitBuffer/wbWriteBit/wbWriteLiteral/...
#include "inter_symbols.h"  // TransformationType (definitions.h:1740-:1747)

#include <cstdint>

namespace entropy {

// ---------------------------------------------------------------------------
// Constants (verbatim, each cited).
// ---------------------------------------------------------------------------
#define HD_QINDEX_BITS 8                     // definitions.h:1644
#define HD_QM_LEVEL_BITS 4                   // definitions.h:1649
#define HD_CDEF_STRENGTH_BITS 6              // cdef.h:21
#define HD_PROFILE_BITS 3                    // definitions.h:432
#define HD_PRIMARY_REF_BITS 3                // definitions.h:1450
#define HD_REF_FRAMES_LOG2 3                 // definitions.h:1388
#define HD_SCALE_NUMERATOR 8                 // definitions.h:1430
#define HD_SUPERRES_SCALE_BITS 3             // definitions.h:1431
#define HD_SUPERRES_SCALE_DENOMINATOR_MIN (HD_SCALE_NUMERATOR + 1)  // :1432
#define HD_SUBEXPFIN_K 3                     // definitions.h:1721
#define HD_GM_TRANS_PREC_BITS 6              // definitions.h:1722
#define HD_GM_ABS_TRANS_BITS 12              // definitions.h:1723
#define HD_GM_ABS_TRANS_ONLY_BITS (HD_GM_ABS_TRANS_BITS - HD_GM_TRANS_PREC_BITS + 3)  // :1724
#define HD_GM_TRANS_PREC_DIFF (16 - HD_GM_TRANS_PREC_BITS)           // :1725 (WARPEDMODEL_PREC_BITS 16, definitions.h:1692)
#define HD_GM_TRANS_ONLY_PREC_DIFF (16 - 3)  // definitions.h:1726
#define HD_GM_ALPHA_PREC_BITS 15             // definitions.h:1729
#define HD_GM_ABS_ALPHA_BITS 12              // definitions.h:1730
#define HD_GM_ALPHA_PREC_DIFF (16 - HD_GM_ALPHA_PREC_BITS)           // :1731
#define HD_GM_ALPHA_MAX (1 << HD_GM_ABS_ALPHA_BITS)                  // :1735
#define HD_MAX_TILE_WIDTH (4096)             // block_structures.h:24
#define HD_MAX_TILE_AREA (4096 * 2304)       // block_structures.h:25
#define HD_MAX_TILE_COLS 64                  // definitions.h:373
#define HD_MAX_TILE_ROWS 64                  // definitions.h:372
#define HD_MI_SIZE_LOG2 2                    // definitions.h:353
#define HD_FRAME_LF_COUNT 4                  // definitions.h:388
#define HD_MAX_SEGMENTS_ 8                   // definitions.h:1674
#define HD_MAX_TILE_CNTS 128                 // definitions.h:72

// FrameType (definitions.h:1585-:1591, verbatim values).
enum HdFrameType {
    HD_KEY_FRAME = 0,
    HD_INTER_FRAME = 1,
    HD_INTRA_ONLY_FRAME = 2,
    HD_S_FRAME = 3,
};
// PRIMARY_REF_NONE (definitions.h:1451) - already in entropy.h.
#define HD_PRIMARY_REF_NONE PRIMARY_REF_NONE

// EbAv1SeqProfile (Source/API/EbSvtAv1.h:144, verbatim values).
enum HdSeqProfile { HD_MAIN_PROFILE = 0, HD_HIGH_PROFILE = 1, HD_PROFESSIONAL_PROFILE = 2 };
// EbBitDepth (Source/API/EbSvtAv1Formats.h:101-:107).
enum HdBitDepth { HD_EB_EIGHT_BIT = 8, HD_EB_TEN_BIT = 10, HD_EB_TWELVE_BIT = 12 };
// InterpFilter tail (definitions.h:818-:827): the frame-header writer needs
// SWITCHABLE (= BILINEAR + 1); inter_symbols.h carries the enum, re-exported
// below for the local spelling.
#define HD_INTERP_FILTER_SWITCHABLE ((int)entropy::SWITCHABLE)

// FrameContextIndex tail (definitions.h:1616-:1623): only REGULAR_FRAME is
// consumed (write_uncompressed_header_obu :3476).
#define HD_REGULAR_FRAME 0

// ---------------------------------------------------------------------------
// Carriers.
// ---------------------------------------------------------------------------
// HdrSeqHeader - the SequenceControlSet surface (pcs.h + av1_structs.h:121-:
// 267 + EbSvtAv1Enc.h static_config): the fields the sequence-header writer
// family consumes. Grown by the slices that need each field.
struct HdrSeqHeader {
    // static_config (EbSvtAv1Enc.h:312/:331/:353/:357/:361/:367/:383/:552-:555)
    int profile;                       // EbAv1SeqProfile
    int encoder_bit_depth;             // EbBitDepth value (8/10/12)
    int color_primaries;               // EbColorPrimaries
    int transfer_characteristics;      // EbTransferCharacteristics
    int matrix_coefficients;           // EbMatrixCoefficients
    int color_range;                   // EbColorRange
    int chroma_sample_position;        // EbChromaSamplePosition
    int chroma_u_dc_qindex_offset;
    int chroma_u_ac_qindex_offset;
    int chroma_v_dc_qindex_offset;
    int chroma_v_ac_qindex_offset;
    // seq_header (av1_structs.h) - added by later slices.
};

// ---------------------------------------------------------------------------
// Ported functions (entropy_coding.c, in span order).
// ---------------------------------------------------------------------------

// svt_aom_wb_is_byte_aligned (:1343-:1345)
int wbIsByteAligned(const AomWriteBitBuffer* wb);
// svt_aom_wb_bytes_written (:1347-:1349)
std::uint32_t wbBytesWritten(const AomWriteBitBuffer* wb);

// recenter_nonneg (:2842-:2850)
std::uint16_t recenterNonneg(std::uint16_t r, std::uint16_t v);
// recenter_finite_nonneg (:2854-:2860)
std::uint16_t recenterFiniteNonneg(std::uint16_t n, std::uint16_t r, std::uint16_t v);

// svt_aom_write_primitive_quniform (:2863-:2875) - the AomWriter (entropy
// coder) variant.
void writePrimitiveQuniform(AomWriter* w, std::uint16_t n, std::uint16_t v);
// aom_wb_write_primitive_quniform (:2877-:2889) - the AomWriteBitBuffer
// variant.
void wbWritePrimitiveQuniform(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t v);
// svt_aom_count_primitive_quniform (:2891-:2898)
int countPrimitiveQuniform(std::uint16_t n, std::uint16_t v);

// svt_aom_write_primitive_subexpfin (:2901-:2922)
void writePrimitiveSubexpfin(AomWriter* w, std::uint16_t n, std::uint16_t k, std::uint16_t v);
// aom_wb_write_primitive_subexpfin (:2924-:2945)
void wbWritePrimitiveSubexpfin(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t k,
                               std::uint16_t v);
// svt_aom_count_primitive_subexpfin (:2947-:2970)
int countPrimitiveSubexpfin(std::uint16_t n, std::uint16_t k, std::uint16_t v);

// svt_aom_write_primitive_refsubexpfin (:2975-:2977)
void writePrimitiveRefsubexpfin(AomWriter* w, std::uint16_t n, std::uint16_t k, std::uint16_t ref,
                                std::uint16_t v);
// aom_wb_write_primitive_refsubexpfin (:2979-:2982)
void wbWritePrimitiveRefsubexpfin(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t k,
                                  std::uint16_t ref, std::uint16_t v);
// svt_aom_wb_write_signed_primitive_refsubexpfin (:2984-:2990)
void wbWriteSignedPrimitiveRefsubexpfin(AomWriteBitBuffer* wb, std::uint16_t n, std::uint16_t k,
                                        std::int16_t ref, std::int16_t v);
// svt_aom_count_primitive_refsubexpfin (:2992-:2994)
int countPrimitiveRefsubexpfin(std::uint16_t n, std::uint16_t k, std::uint16_t ref, std::uint16_t v);

// write_delta_q (:2365-:2372)
void writeDeltaQ(AomWriteBitBuffer* wb, std::int32_t delta_q);

// write_profile (:2671-:2674): literal(profile, PROFILE_BITS). The reference
// takes BitstreamProfile; the assert bounds it to [0, 3).
void writeProfile(std::int32_t profile, AomWriteBitBuffer* wb);

// write_bitdepth (:2676-:2685). SVT_EFFECTIVE_BIT_DEPTH is the identity for
// the pinned HBD=1 build.
void writeBitdepth(const HdrSeqHeader* sh, AomWriteBitBuffer* wb);

}  // namespace entropy
