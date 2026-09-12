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
| CDF_PROB_BITS/CDF_PROB_TOP/AOM_ICDF | Codec/cabac_context_model.h:39, :40, :47 | EC0 entropy-coder plumbing |
| EC_PROB_SHIFT/EC_MIN_PROB/OD_BITRES/OD_ICDF/OD_ILOG_NZ, OdEcWindow, OD_EC_WINDOW_SIZE, OD_MEASURE_EC_OVERHEAD, OdEcEnc struct | Codec/bitstream_unit.h:85-120 | EC0 od_ec encoder context |
| encoder prototypes (Emit-Lines block) | Codec/bitstream_unit.h:122-131 | EC0: the .c extracts call each other; SVT declares them here |
| BSwap64 + HToBE64 line | Codec/bitstream_unit.h:203, :162 | EC0: od_ec_enc_flush byte write (see deviation note 2 below) |
| svt_log2f = get_msb macro + portable get_msb body | Codec/definitions.h:592, :628-644 | EC0: rng leading-zero count in normalize/refill paths |
| svt_od_ec_encode_* / enc_done / tell / tell_frac, od_ec_enc_flush, propagate_carry_bwd | Codec/bitstream_unit.c:77-408 | EC0 encoder side |
| od_ec_dec typedef + struct | third_party/aom_dsp/inc/entdec.h:21, :27-51 | EC0 decoder context (vendored aom_dsp subtree, referenced by the pinned tree's test/CMakeLists.txt:82) |
| od_ec_dec_refill/normalize/init, decode_bool_q15/decode_cdf_q15, dec_tell/tell_frac | third_party/aom_dsp/src/entdec.c:78-283 | EC0 decoder side |
| AomCdfProb, update_cdf | Codec/cabac_context_model.h:31, :76-105 | EC2: the one CDF adaptation primitive in the pinned tree (shared by aom_write_symbol and aom_read_symbol_) |
| AomWriter struct, aom_stop_encode, aom_write_symbol (nsymbs==2 -> bool specialization + allow_update_cdf) | Codec/bitstream_unit.h:222-228, :245-253, :265-279 | EC2: adapted writer surface (OutputBitstreamUnit opaque via shims.h; aom_start_encode/ensure_capacity glue not extracted) |
| ACCT_STR macros, aom_read_cdf macro, aom_reader struct/typedef, aom_reader_init, aom_read_cdf_, aom_read_symbol_ | third_party/aom_dsp/inc/bitreader.h:20-47, :84-98 + src/bitreader.c:14-22 | EC2: reader-side wrapper (decode + update_cdf) |
| CONFIG_ENABLE_FILTER_INTRA (=1 line) | API/EbConfigMacros.h:203 | EC3: non-RTC defaults are live (generator builds with neither RTC_BUILD nor MINIMAL_BUILD, CMakeLists.txt:53/86); deviation note 3 |
| KF_MODE_CONTEXTS, CDF_SIZE, AOM_CDF2..AOM_CDF16 macro family | Codec/cabac_context_model.h:262, :38, :50-65 | EC3: CDF table plumbing |
| BlockSize enum, DIRECTIONAL_MODES, MAX_ANGLE_DELTA | Codec/definitions.h:883-905, :1305, :1306 | EC3: symbol surface enums |
| intra_mode_context, block_size_wide/high | Codec/common_utils.c:134-148, :286-291 | EC3: KF mode contexts + FI allowed predicate |
| svt_aom_default_kf_y_mode_cdf, default_angle_delta_cdf, default_filter_intra_mode_cdf, default_filter_intra_cdfs | Codec/cabac_context_model.c:59, :87, :614, :618 | EC3: default CDF tables (verbatim) |
| svt_aom_filter_intra_allowed_bsize, svt_aom_filter_intra_allowed | Codec/mode_decision.c:108-119 | EC3: FI signalling predicate (DC_PRED-only, palette 0, bsize <= 32x32) |

Second documented deviation (EC0): the WORDS_BIGENDIAN `#if` guard around the
HToLE/HToBE macro family (bitstream_unit.h:148-164) is dropped; the
little-endian branch line `#define HToBE64(X) BSwap64(X)` is taken verbatim.
The generator targets LE hosts (x86-64) only, where the guard's `#else`
branch is the live one.

Deviation note (EC0): `od_ec_dec_bits_` (third_party/aom_dsp/inc/entdec.h:64)
is DECLARED in the pinned tree but has NO definition anywhere in it
(grep-verified). Raw-bits decode is therefore unported; the
`od_ec_dec_bits` macro (entdec.h:24) and `OD_ACC_STR` (entdec.h:23) stay
unextracted with it. Not needed by the intra-frame symbol subset (EC3).

The only textual substitution inside an extract: `get_filt_type(xd, plane)`
inside `build_intra_predictors` is replaced by a generator-controlled global
(`svtd_filt_type`), flagged inline in `svt_gen.c`.

## Validation gate

`expected_primitives.txt` holds the golden values transcribed from the
committed tests (test_transform.cpp, test_intra.cpp, test_motion.cpp,
test_pipeline.cpp). The gate is `golden_primitives.exe` output diffed against
that file — currently **219/219 lines identical**, covering:

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
  SVT-composed drivers (`svtd_frame_auto_*` / `svtd_frame_v_dct_*`);
- entropy-coder gate lines (EC0): bool_eq / bool(f) / cdf13 encode byte
  dumps, decode-back symbol lines, the bool_eq-vs-bool(f=16384) equivalence,
  and enc tell/tell_frac (round-trip equality is also asserted in-generator;
  the generator exits nonzero on mismatch);
- CDF adaptation gate lines (EC2): binary-CDF 40-step adaptation snapshots
  (counter through the rate-4/5/6 transitions), 13-symbol adapted dump,
  dec tell, and adapted-symbol wrapper round-trips at 2 and 13 symbols
  (writer/reader adapted CDFs end identical);
- EC3 intra-frame KF symbol lines: eckf_ctx (context pairs via
  intra_mode_context), eckf_bytes (mode + angle-delta + filter-intra pair
  with adaptation), eckf_rt (decode-back), eckf_cdf_eq.

## EC3 scope statement

The EC3 symbol surface is the key-frame (intra-only) luma syntax, chosen to
match what l6_pipeline (intra-only luma, 4x4..64x64, D2-chosen modes) and the
l4 FI machinery actually produce and can decode:

- IN scope: kf luma-mode symbol (13 symbols, kf_y_cdf[top_ctx][left_ctx],
  entropy_coding.c:1030, contexts from svt_aom_get_kf_y_mode_ctx :1004-1021 +
  intra_mode_context, DC_PRED on unavailable neighbors); angle-delta symbol
  (7 symbols, only when bsize >= BLOCK_8X8 and the mode is directional,
  :1032-1037); filter-intra flag (2 symbols) + filter-intra-mode symbol (5
  symbols) gated by svt_aom_filter_intra_allowed (:5047-5060,
  mode_decision.c:108-119 - DC_PRED only, palette 0, bsize <= 32x32).
- DEFERRED (named): chroma uv_mode/CFL alphas/chroma angle-delta
  (encode_intra_chroma_mode_av1 :1077-1095 - the CH-agent boundary; deferred
  to CH coordination per the approved plan); the nonkey y_mode_cdf path
  (:1046-1058 - the pipeline is intra-only); tx_type (:317 - ext_tx sets,
  out of the current TxType scope); palette (:4362), intrabc (:4401),
  coefficient/token coding (separate series); od_ec_dec_bits raw bits
  (declared-undefined in the pinned tree, EC0 deviation).

The frame-policy golden (`golden_frame.exe`) is captured for D3.

Q0 deviation note: this SVT tree has no `av1_quantize_dc` — dc/ac handling is
unified inside the quantize helpers via `dequant_ptr[rc != 0]` /
`quant_ptr[rc != 0]` indexing (full_loop.c:239, :246). The "dc path" is table
index 0 of the extracted helpers.

REFERENCE PINNING note: every extract targets the vendored snapshot in
`third_party/SVT-AV1/` (4.2-era, CHANGELOG 4.2.0 dated 2026-07-14, NOT
byte-identical to the official v4.2.0 tag). The pinned tree is the sole 1:1
reference; updating it would invalidate every golden line above.
