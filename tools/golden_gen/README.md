# tools/golden_gen — SVT-AV1 golden-vector generator

Generates every golden vector used by the test suites in `src/`, directly
from the vendored SVT-AV1 tree (`third_party/SVT-AV1`). The extraction is
mechanical: `extract.ps1` locates each function/table in the SVT sources by
signature and copies it **verbatim** (byte-for-byte body, provenance comment
above each). No golden is hand-typed.

## Layout

- `extract.ps1` — pulls the symbols listed below out of
  `third_party/SVT-AV1/Source/Lib/...` into `svt_gen.c` (generated, committed
  so the gate is reproducible without re-running extraction).
- `shims.h` — plumbing only (typedefs, RTCD name resolutions like
  `svt_av1_dr_prediction_z1 -> ..._c`, `clip_pixel`), each citing its SVT
  header. No arithmetic.
- `composition.c` — wires the extracts together: dispatch-table population
  (`svtd_eb_pred[13][5]` / `svtd_dc_pred[2][2][5]`, the TX_32X32 and
  TX_64X64 columns populated via the SVTD_ADAPTER32 adapters and plain
  64x64 static functions), the FIVE 2D core pairs — documented
  specializations of `av1_tranform_two_d_core_c` / `inv_txfm2d_add_c`,
  all five enumerated:
  TX_4X4 (fwd_shift {2,0,0}, cos_bit 13/13),
  TX_8X8 ({2,-1,0}, 13/13),
  TX_16X16 ({2,-2,0}, col 13 / row 12),
  TX_32X32 ({2,-4,0}, 12/12),
  TX_64X64 (DCT-only, {0,-2,-2}, col 13 / row 10);
  the default scans up to 64x64 (svt_aom_init_iscan at W=H=32/64), the
  log_scale 1/2 quantizer entries
  (`svtd_quantize_fp_32x32/_b_32x32/_64x64/_b_64x64`, n_coeffs 1024/4096),
  and the frame-policy drivers (`svtd_frame_auto_32x32_blocks/_q`,
  `svtd_frame_v_dct_32x32/_q`, `svtd_frame_auto_64x64_blocks/_q`,
  `svtd_frame_v_dct_64x64/_q` — 2x2 grids on 64x64 / 128x128 frames with the
  FR-series REAL recon top-right gather).
- `main_primitives.c` — per-primitive golden dump (diffed against
  `expected_primitives.txt` by the validation gate).
- `main_frame.c` — frame-policy composition: raster 4x4 loop where each
  block's mode is decided by the D2 policy (all 13 PredictionModes scored by
  SAD, lowest wins, tie = lowest index) against RECONSTRUCTED neighbors.
  Outputs mode map + recon + coeffs.

## Regeneration

```
powershell -ExecutionPolicy Bypass -File tools/golden_gen/extract.ps1
cmake -S tools/golden_gen -B build/golden_gen
cmake --build build/golden_gen --config Release
build\golden_gen\Release\golden_primitives.exe   # diff vs expected_primitives.txt
build\golden_gen\Release\golden_frame.exe        # D-policy frame golden
```

## Provenance table

| Extracted symbol | SVT file | What it feeds |
| --- | --- | --- |
| svt_av1_fdct4_new / svt_av1_fadst4_new | Codec/transforms.c | l3 fwd goldens |
| svt_av1_fdct8_new / svt_av1_fadst8_new, svt_av1_fdct16_new / svt_av1_fadst16_new | Codec/transforms.c | l3 fwd goldens (8x8/16x16) |
| svt_av1_fdct32_new, static av1_fadst32_new | Codec/transforms.c | l3 fwd goldens (32x32; fadst32 is static - extractor pattern matches the static name) |
| svt_av1_fdct64_new | Codec/transforms.c:762 | l3 fwd golden fdct64 (DCT-only) |
| fwd_shift_4x4/8x8/16x16/32x32/64x64, fwd_cos_bit_col/row | Codec/transforms.c | fwd 2D core config (fwd_shift_64x64 = {0,-2,-2}; cos_bit row[4][4] = 10, the only pass below 12) |
| svt_av1_idct4_new / svt_av1_iadst4_new | Codec/inv_transforms.c | l3 inverse goldens |
| svt_av1_idct8_new/iadst8_new, svt_av1_idct16_new/iadst16_new | Codec/inv_transforms.c | l3 inverse goldens (8x8/16x16) |
| svt_av1_idct32_new, static av1_iadst32_new | Codec/inv_transforms.c | l3 inverse goldens (32x32) |
| svt_av1_idct64_new | Codec/inv_transforms.c:1567 | l3 inverse golden idct64 (DCT-only; no ADST at 64x64, av1_txfm_type_ls[4] = DCT64/INVALID/INVALID/IDENTITY64, inv_transforms.h:196) |
| g_uv2y, fimode_to_intradir | Codec/common_utils.c:14, :33 | CH0 chroma fold: get_uv_mode (common_utils.h:130-133) maps UvPredictionMode -> luma PredictionMode (UV_CFL_PRED -> DC_PRED); chroma never uses FI (enc_intra_prediction.c:641) |
| UvPredictionMode enum | Codec/definitions.h:1210-1227 | UV_DC..UV_PAETH + UV_CFL_PRED (+UV_INTRA_MODES/UV_MODE_INVALID sentinels) |
| inv_shift_4x4/8x8/16x16/32x32/64x64, INV_COS_BIT, inv_cos_bit_col/row | Codec/inv_transforms.{c,h} | inv 2D core config (inv_shift_64x64 = {-2,-4}) |
| svt_av1_gen_inv_stage_range (recomputed inline, cited) | Codec/inv_transforms.c:44, inv_transforms.h:221-222 | gen_inv_range_{8x8,16x16,32x32_dct,64x64_dct} gate lines (stage_range shim per size; 64x64 DCT-only, 12 x 16) |
| half_btf, round_shift, round_shift_array_c, clamp_value, clamp_buf, clamp64 | Codec/inv_transforms.{c,h}, definitions.h | transform helpers |
| svt_aom_eb_av1_cospi_arr_data / sinpi_arr_data | Codec/inv_transforms.c | cos_bit 12/13 table rows |
| sm_weight_arrays, sm_weights_sanity_checks, divide_round | Codec/intra_prediction.c | smoothPredict |
| eb_dr_intra_derivative, get_dx, get_dy | Codec/intra_prediction.c | drZ1/2/3 + drPredictor |
| svt_av1_dr_prediction_z1/z2/z3_c | Codec/intra_prediction.c | directional zones |
| svt_aom_use_intra_edge_upsample, svt_aom_intra_edge_filter_strength, svt_av1_filter_intra_edge_c, filter_intra_edge_corner | Codec/intra_prediction.c | edge pipeline |
| dc_128/dc_left/dc_top/dc/v/h/smooth/smooth_v/smooth_h/paeth predictors | Codec/intra_prediction.c | builder dispatch |
| svt_aom_dr_predictor | Codec/intra_prediction.c | dr dispatch |
| extend_modes, mode_to_angle_map, av1_is_directional_mode, need-flags enum | Codec/intra_prediction.{c,h} | builder config |
| build_intra_predictors (get_filt_type shimmed) | Codec/enc_intra_prediction.c | whole prediction path |
| svt_av1_upsample_intra_edge_c | C_DEFAULT/intra_prediction_c.c | edge upsample |
| FILTER_INTRA_SCALE_BITS, eb_av1_filter_intra_taps, svt_av1_filter_intra_predictor_c | C_DEFAULT/filterintra_c.c | filter-intra |
| svt_nxm_sad_kernel_helper_c | C_DEFAULT/compute_sad_c.c | SAD scoring (4x4/16x16/32x32/64x64 dims; 8x8 has its own dedicated kernel in motion_estimation.c:71) |
| svt_aom_quantize_b_c, quantize_fp_helper_c | Codec/full_loop.c:31, :222 | quantize entries at log_scale = av1_get_tx_scale_tab[TxSize] = 0/0/0/1/2 (full_loop.c:22; fp at TX_4X4 via log_scale 0, full_loop.c:286; escalated arithmetic at :228/:244/:246/:249 and b at :36/:67/:69-70/:74) |
| dc_qlookup_QTX, ac_qlookup_QTX, svt_aom_dc/ac_quant_qtx, svt_aom_get_qzbin_factor, svt_aom_invert_quant | Codec/inv_transforms.c:3412, :3357, :3467, :3484, :3501, :3516 | quantizer tables (qindex {0,1,100,200,255}) |
| TranLow, QmVal, clamp, MINQ, MAXQ, QINDEX_RANGE | Codec/definitions.h:986, :987, :687, :1641-1643 | quantizer plumbing |
| AOM_QM_BITS | Codec/inv_transforms.h:27 | quantizer plumbing |
| EbBitDepth (shim, full enum) | API/EbSvtAv1Formats.h:101 | get_qzbin_factor switch needs all enumerators |

The only textual substitution inside an extract: `get_filt_type(xd, plane)`
inside `build_intra_predictors` is replaced by a generator-controlled global
(`svtd_filt_type`), flagged inline in `svt_gen.c`.

## Validation gate

`expected_primitives.txt` holds the golden values transcribed from the
committed tests (test_transform.cpp, test_intra.cpp, test_motion.cpp,
test_pipeline.cpp). The gate is `golden_primitives.exe` output diffed against
that file — currently **178/178 lines identical**, covering:

- transforms: fdct/fadst/idct/iadst 1D vectors at 4/8/16/32/64 (fdct64/idct64
  DCT-only), fwd2d/inv2d gate lines at 4x4/8x8/16x16/32x32 (DCT + ADST) and
  64x64 (DCT-only), gen_inv_range stage-range lines for 8x8/16x16/32x32/64x64;
- quantizer: qscan4x4..64x64 (default scan up to 4096 coeffs), luma quantizer
  tables at qindex {0,1,100,200,255}, fp/b quantize+dequantize+eob vectors at
  log_scale 0/1/2 with fp-vs-b discriminators;
- builder gate lines: b4/b8/b16/b32/b64 families (V/DC/DC128/D45/D135/D203/
  smooth/paeth/fiv) with the upsample/corner-blend regime differences per
  size;
- frame-policy gate lines: f16/f32/f64 mode maps + recon + coeffs (lossless
  + q100) and forced-mode f16v/f32v/f64v (+q) recon/coeffs captured from the
  SVT-composed drivers (`svtd_frame_auto_*` / `svtd_frame_v_dct_*`).

The frame-policy golden (`golden_frame.exe`) is captured for D3.

Q0 deviation note: this SVT tree has no `av1_quantize_dc` — dc/ac handling is
unified inside the quantize helpers via `dequant_ptr[rc != 0]` /
`quant_ptr[rc != 0]` indexing (full_loop.c:239, :246). The "dc path" is table
index 0 of the extracted helpers.

REFERENCE PINNING note: every extract targets the vendored snapshot in
`third_party/SVT-AV1/` (4.2-era, CHANGELOG 4.2.0 dated 2026-07-14, NOT
byte-identical to the official v4.2.0 tag). The pinned tree is the sole 1:1
reference; updating it would invalidate every golden line above.
