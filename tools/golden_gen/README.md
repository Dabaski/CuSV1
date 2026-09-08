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
- `composition.c` — wires the extracts together: dispatch-table population,
  the two 4x4 2D cores (documented specializations of
  `av1_tranform_two_d_core_c` / `inv_txfm2d_add_c` to TX_4X4/8-bit), and the
  frame-policy driver.
- `main_primitives.c` — per-primitive golden dump (diffed against
  `expected_primitives.txt` by the validation gate).
- `main_frame.c` — frame-policy composition: raster 4x4 loop where each
  block's mode is decided by the D2 policy (all 13 PredictionModes scored by
  SAD, lowest wins, tie = lowest index) against RECONSTRUCTED neighbors.
  Outputs mode map + recon + coeffs.

## Regeneration

```
pwsh tools/golden_gen/extract.ps1        # regenerates svt_gen.c from SVT
cmake -S tools/golden_gen -B build/golden_gen
cmake --build build/golden_gen --config Release
build\golden_gen\Release\golden_primitives.exe   # diff vs expected_primitives.txt
build\golden_gen\Release\golden_frame.exe        # D-policy frame golden
```

## Provenance table

| Extracted symbol | SVT file | What it feeds |
| --- | --- | --- |
| svt_av1_fdct4_new / svt_av1_fadst4_new | Codec/transforms.c | l3 fwd goldens |
| fwd_shift_4x4, fwd_cos_bit_col/row | Codec/transforms.c | fwd 2D core config |
| svt_av1_idct4_new / svt_av1_iadst4_new | Codec/inv_transforms.c | l3 inverse goldens |
| half_btf, round_shift, round_shift_array_c, clamp_value, clamp_buf, clamp64 | Codec/inv_transforms.{c,h}, definitions.h | transform helpers |
| inv_shift_4x4, INV_COS_BIT, inv_cos_bit_col/row | Codec/inv_transforms.{c,h} | inv 2D core config |
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
| svt_nxm_sad_kernel_helper_c | C_DEFAULT/compute_sad_c.c | SAD scoring |
| svt_aom_quantize_b_c, quantize_fp_helper_c | Codec/full_loop.c:31, :222 | Q1 quantize (fp = TX_4X4 via log_scale 0, full_loop.c:286) |
| dc_qlookup_QTX, ac_qlookup_QTX, svt_aom_dc/ac_quant_qtx, svt_aom_get_qzbin_factor, svt_aom_invert_quant | Codec/inv_transforms.c:3412, :3357, :3467, :3484, :3501, :3516 | Q0/Q1 quantizer tables |
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
that file — currently **71/71 lines identical** (55 primitives + 16 Q0
quantizer lines: default scan 4x4, luma quantizer tables at qindex
{0,1,100,200,255}, and fp/b quantize+dequantize+eob vectors for the
d3_coeffs block-0 fixture). The frame-policy golden (`golden_frame.exe`) is
captured for D3.

Q0 deviation note: this SVT tree has no `av1_quantize_dc` — dc/ac handling is
unified inside the quantize helpers via `dequant_ptr[rc != 0]` /
`quant_ptr[rc != 0]` indexing (full_loop.c:239, :246). The "dc path" is table
index 0 of the extracted helpers.
