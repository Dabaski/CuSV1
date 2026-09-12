// tools/golden_gen/shims.h
// Support shims for the verbatim SVT extracts in svt_gen.c. Each shim notes
// its provenance (SVT header that defines the real thing). This file is
// hand-written ON PURPOSE: it contains only type/macro plumbing, no
// arithmetic. Any arithmetic lives in the verbatim extracts.
#ifndef SVTD_SHIMS_H
#define SVTD_SHIMS_H

#include <stdint.h>
#include <stddef.h>
// CHAR_BIT for OD_EC_WINDOW_SIZE (bitstream_unit.h:97)
#include <limits.h>

// SVT uses INLINE/NOINLINE/ATTRIBUTE_PACKED macros (EbConfigMacros etc.).
// SVT writes `static INLINE` itself, so INLINE expands to plain `inline`.
#define INLINE inline
#define NOINLINE
#define ATTRIBUTE_PACKED

// DECLARE_ALIGNED(alignment, type, name) - aom_dsp_common.h
#define DECLARE_ALIGNED(alignment, type, name) type name

// svt_memcpy_c - Utility.h: plain byte copy (verbatim semantics)
#define svt_memcpy_c(dst, src, n) memcpy((dst), (src), (n))

// clip_pixel / clip_pixel_highbd - aom_dsp_common.h:
//   clip_pixel(val) = clamp to [0,255]; clip_pixel_highbd(val, bd) = clamp to
//   [0, (1<<bd)-1]
static inline int svtd_clip(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
#define clip_pixel(p) svtd_clip((p), 0, 255)
#define clip_pixel_highbd(val, bd) svtd_clip((val), 0, ((1 << (bd)) - 1))

// EB_ABS_DIFF - Utility.h
#define EB_ABS_DIFF(a, b) (abs((int)((a)) - (int)((b))))

// EB_LIKELY/EB_UNLIKELY - definitions.h:500-516; __builtin_expect under
// GNUC, identity expression under MSVC (the generator's toolchain).
#define EB_UNLIKELY(x) (x)
// EB_ASSUME - definitions.h:514-516; __builtin_assume under GNUC, no-op
// under MSVC.
#define EB_ASSUME(x) ((void)0)

// OD_WARN_UNUSED_RESULT / OD_ARG_NONNULL - bitstream_unit.h:57-75; GNUC
// function attributes, empty under MSVC.
#define OD_WARN_UNUSED_RESULT
#define OD_ARG_NONNULL(x)

#include <stdlib.h>

// RTCD dispatch names resolve to the _c implementations in the generator
// (common_dsp_rtcd.h declares svt_av1_dr_prediction_z1/z2/z3 as function
// pointers defaulting to the _c variants; svt_memcpy dispatches likewise).
#define svt_av1_dr_prediction_z1 svt_av1_dr_prediction_z1_c
#define svt_av1_dr_prediction_z2 svt_av1_dr_prediction_z2_c
#define svt_av1_dr_prediction_z3 svt_av1_dr_prediction_z3_c
#define svt_av1_filter_intra_predictor svt_av1_filter_intra_predictor_c
#define svt_av1_filter_intra_edge svt_av1_filter_intra_edge_c
#define svt_av1_upsample_intra_edge svt_av1_upsample_intra_edge_c
#define svt_memcpy svt_memcpy_c

// TranHigh - inv_transforms.h:263
typedef int64_t TranHigh;

// EbBitDepth - API/EbSvtAv1Formats.h:101. The API header is outside the
// extractor's Source/Lib scope; the full enumerator list is required because
// svt_aom_get_qzbin_factor's switch (inv_transforms.c:3501) references
// EB_TEN_BIT/EB_TWELVE_BIT unconditionally (only EB_EIGHT_BIT is exercised).
typedef enum {
    EB_EIGHT_BIT     = 8,
    EB_TEN_BIT       = 10,
    EB_TWELVE_BIT    = 12,
    EB_SIXTEEN_BIT   = 16,  // Not supported
    EB_THIRTYTWO_BIT = 32,  // Not supported
} EbBitDepth;

// TxSize: the generator exercises TX_4X4 (=0), TX_8X8 (=1), TX_16X16 (=2),
// TX_32X32 (=3) and TX_64X64 (=4) from SVT's TxSize enum.
typedef enum { TX_4X4 = 0, TX_8X8 = 1, TX_16X16 = 2, TX_32X32 = 3, TX_64X64 = 4 } TxSize;
// tx_size_wide/high for the sizes the generator uses (SVT tables in
// av1_common data; 4/8/16/32/64).
static const int32_t tx_size_wide[5] = {4, 8, 16, 32, 64};
static const int32_t tx_size_high[5] = {4, 8, 16, 32, 64};

// MAX_TXFM_STAGE_NUM - transforms.h; MAX_BLOCK_DIM / MAX_UPSAMPLE_SZ -
// intra_prediction.h / definitions.h; MAX_TX_SIZE - definitions.h:410
#define MAX_TXFM_STAGE_NUM 33
#define MAX_BLOCK_DIM 64
#define MAX_UPSAMPLE_SZ 16
#define MAX_TX_SIZE (1 << 6)

// MacroBlockD is opaque here: the luma 8-bit builder only touches xd via
// get_filt_type, which the generator shims out (see svt_gen.c).
typedef void MacroBlockD;

// OutputBitstreamUnit is opaque here: AomWriter only holds a pointer to it
// (bitstream_unit.h:227) for buffer-ownership glue the generator does not
// exercise (the drivers point ec.buf at their own buffer, aom_start_encode
// bitstream_unit.h:230-236).
typedef void OutputBitstreamUnit;

// TxfmFunc - inv_transforms.h:259
typedef void (*TxfmFunc)(const int32_t* input, int32_t* output, int8_t cos_bit,
                         const int8_t* stage_range);

// Dispatch tables the extracted builder indexes (svt_aom_eb_pred /
// svt_aom_dc_pred). SVT populates them with the intra_pred_sized macro over
// every TxSize (intra_prediction.c:1402); the generator instantiates the
// TX_4X4 column only. Size 13 = INTRA_MODES (definitions.h:1204,
// PAETH_PRED + 1). Declared here because build_intra_predictors references
// them; defined and populated in composition.c.
typedef void (*SvtdPredFn)(uint8_t* dst, ptrdiff_t stride, const uint8_t* above, const uint8_t* left);
extern SvtdPredFn svtd_eb_pred[13][5];
extern SvtdPredFn svtd_dc_pred[2][2][5];
#define svt_aom_eb_pred svtd_eb_pred
#define svt_aom_dc_pred svtd_dc_pred

// get_filt_type shim state (see svt_gen.c build_intra_predictors)
extern int32_t svtd_filt_type;

#endif  // SVTD_SHIMS_H