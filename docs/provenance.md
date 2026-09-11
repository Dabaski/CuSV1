# PV — function-by-function provenance table

Every port artifact in `src/` traced to the pinned vendored reference
(`third_party/SVT-AV1/`, REFERENCE PINNING rule in `AGENTS.md`). Status
vocabulary:

- **exact** — the SVT body is ported with the arithmetic text unchanged;
  only mechanical renames were applied (identifier/table-parameter renames,
  C++ `std::` spellings). Bit-exactness is gate-proven by
  `tools/golden_gen` (expected_primitives.txt, 178 lines).
- **adapted** — SVT structure is preserved but the port changes shape:
  thread mapping, buffer ownership, parameterization, or a documented
  specialization of a generic SVT function. The per-sample arithmetic is
  still 1:1 SVT; what changed is the wrapper around it.
- **policy** — project-defined decision logic that SVT does not prescribe
  at this scope. SVT primitives inside it are exact/adapted as listed.

Golden provenance: every expected value comes from `tools/golden_gen`
(committed generator; `extract.ps1` copies SVT bodies verbatim into
`svt_gen.c`), never from hand-traces. The generator's own provenance table
is in `tools/golden_gen/README.md`.

## l3_transforms (src/l3_transforms/transform.cpp)

| SVT symbol (file:line) | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| svt_av1_fdct4_new (transforms.c) | `fdct4` (transform.cpp:3469) | exact | hand transcription, gate fdct4 |
| svt_av1_fadst4_new | `fadst4` (:3495) | exact | gate fadst4 |
| svt_av1_fdct8_new | `fdct8` (:608) | exact | gate fdct8 |
| svt_av1_fadst8_new | `fadst8` (:678) | exact | gate fadst8 |
| svt_av1_fdct16_new | `fdct16` (:1059), cos_bit-parameterized | exact | gate fdct16 (col 13 / row 12) |
| svt_av1_fadst16_new | `fadst16` (:1063), cos_bit-parameterized | exact | gate fadst16 |
| svt_av1_fdct32_new | `fdct32` (:1069), cos_bit 12 fixed | exact | gate fdct32 |
| svt_av1_fadst32_new (static, transforms.c:1908) | `fadst32` (:2895) | exact | gate fadst32 |
| svt_av1_fdct64_new (transforms.c:762) | `fdct64`/`fdct64B` (:1347/:1351) | exact | DCT-only; mechanical port of the verbatim extract, SVT pointer-swap structure preserved (bf0/bf1 alternation); fdct64B takes cos_bit (col 13 / row 10) |
| svt_av1_idct4_new | `idct4` (:87) | exact | gate idct4 |
| svt_av1_iadst4_new | `iadst4` (:115) | exact | gate iadst4 |
| svt_av1_idct8_new | `idct8` (:161) | exact | clamps only where SVT consumes stage_range |
| svt_av1_iadst8_new | `iadst8` (:222) | exact | |
| svt_av1_idct16_new | `idct16` (:302) | exact | clamp stages 3-7 |
| svt_av1_iadst16_new | `iadst16` (:437) | exact | clamp stages 3/5/7; no all-zero early-out at 16 |
| svt_av1_idct32_new (inv_transforms.c:378) | `idct32` (:3077) | exact | clamps ONLY stages 3-9 (audited) |
| svt_av1_iadst32_new (static, inv_transforms.c:1132) | `iadst32` (:3328) | exact | clamps EVERY stage; no all-zero early-out at 32 |
| svt_av1_idct64_new (inv_transforms.c:1567) | `idct64` (:2112) | exact | mechanical extract port; clamp bit 16 every stage via stageRange shim (gen_inv_range_64x64_dct) |
| round_shift / half_btf (inv_transforms.h) | `roundShift` / `halfBtf` | exact | pure renames |
| clamp_value / clamp_buf (inv_transforms.c) | `clampValue` / `clampBufIv` | exact | |
| svt_aom_eb_av1_cospi/sinpi_arr_data (inv_transforms.c) | `kCospi13`/`kCospi12`/`kCospi10`/`kSinpi*` + `cospiRow` | adapted | table ROWS verbatim; the 7-row table flattened to the three used rows (13/12/10), cospiRow = cospi_arr(cos_bit) mirror |
| fwd_shift_4x4/8x8/16x16/32x32/64x64 (transforms.c) | shift constants in fwdTxfm2d* | exact | {2,0,0}/{2,-1,0}/{2,-2,0}/{2,-4,0}/{0,-2,-2} |
| inv_shift_4x4/8x8/16x16/32x32/64x64 (inv_transforms.c:21-22) | shift constants in invTxfm2dAdd* | exact | {0,-4}/{-1,-4}/{-2,-4}/{-2,-4}/{-2,-4} |
| fwd_cos_bit_col/row, inv_cos_bit_col/row (transforms.c:19-22, inv_transforms.h:32-41) | cos_bit constants (13/13, 13/13, 13/12, 12/12, 13/10; inv 12/12) | exact | read from the extracted tables, cited |
| av1_tranform_two_d_core_c (transforms.c:2398) | `fwdTxfm2d4x4..64x64` | adapted | generic core specialized per size: bd=8 fixed, no flips, fixed TxType column; col/row 1D calls + round_shift_array(-shift) structure preserved |
| inv_txfm2d_add_c / svt_av1_inv_txfm2d_add_64x64_c | `invTxfm2dAdd4x4..64x64` | adapted | same specialization; clamps only where SVT consumes stage_range; clip_pixel_highbd add |
| av1_txfm_type_ls (inv_transforms.h:196) | 64x64 DCT-only scope | exact (finding) | {DCT64, INVALID, INVALID, IDENTITY64} — no ADST signalable at TX_64X64; IDENTITY64 column parked, not ported |
| quantize_fp_helper_c (full_loop.c:222) | `quantizeFpN` (log_scale param), `quantizeFp4x4/8x8/16x16/32x32/64x64` | adapted | verbatim semantics with the helper's own log_scale parameter; escalated arithmetic cited (full_loop.c:228/:244/:246/:249) |
| svt_aom_quantize_b_c (full_loop.c:31) | `quantizeBN`, `quantizeB4x4/8x8/16x16/32x32/64x64` | adapted | escalated arithmetic cited (:36/:67/:69-70/:74) |
| svt_aom_init_iscan (coefficients.c:345-363) | `defaultScan4x4..64x64` | adapted | formula-based port at W=H=4..64 (the iscan loop instantiated per size) |
| dc_qlookup/ac_qlookup_QTX, svt_aom_dc/ac_quant_qtx, get_qzbin_factor, invert_quant (inv_transforms.c:3357-3516) | `buildQuantTables` | adapted | luma rows of svt_av1_build_quantizer, sharpness=0; the composition is ours, each table/step is verbatim SVT |
| svt_aom_eb_av1 dc/ac QTX lookup | GPU `quant_dequant_4x4..64x64` | adapted | fp helper arithmetic per-thread at log_scale 0/1/2; eob = max nonzero scan position + 1 (policy comment in kernel) |

GPU twins (fwd_txfm_2d_*, inv_txfm_2d_add_*, quant_dequant_*): **adapted** —
the device 1D bodies (`d_fdct4..d_fdct64`, `d_idct4i..d_idct64i`,
`d_fadst4..d_fadst32`, `d_iadst4i..d_iadst32i`) carry the verbatim stage
arithmetic; what differs is the launch geometry (one thread per column/row,
shared-memory staging) and mechanical renames (half_btf -> d_half_btf,
clamp_value -> d_cv, cos_bit -> bit). Every twin is bit-exact vs its host
port (tests in test_transform.cpp).

## l4_intra (src/l4_intra/intra.cpp)

| SVT symbol (file:line) | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| build_intra_predictors (enc_intra_prediction.c:159) | `buildIntraPredictors` (:455) | exact | the ONE extract substitution: get_filt_type(xd, plane) -> svtd_filt_type generator shim, flagged inline in svt_gen.c; the host port mirrors that shim with intra::NeighborContext carrying the same two mode values |
| svt_av1_dr_prediction_z1/z2/z3_c (intra_prediction.c) | `drZ1`/`drZ2`/`drZ3` (:202/:236/:262) | exact | gate-verified per zone |
| svt_aom_dr_predictor + eb_dr_intra_derivative + get_dx/get_dy | `drPredictor` (:734) + `getDx`/`getDy` (:345/:355) + `drIntraDerivative` (:312, verbatim table) | exact | |
| dc/dc_left/dc_top/dc_128/v/h/paeth predictors (intra_prediction.c) | inline dispatch arms inside `buildIntraPredictors` (DC_PRED :631, V_PRED :660, H_PRED :666, PAETH_PRED :672 — paeth_predictor_single mirrored inline via std::abs) | exact | |
| smooth/smooth_v/smooth_h predictors (intra_prediction.c) + sm_weight_arrays (:25) | `smoothPredict`/`smoothVPredict`/`smoothHPredict` (:365/:389/:408) + `smWeightArrays` (:291, verbatim table) | exact | |
| svt_aom_intra_edge_filter_strength + svt_av1_filter_intra_edge_c + filter_intra_edge_corner | `edgeFilterStrength`/`filterIntraEdge` (:95) + corner-blend path | exact | |
| svt_aom_use_intra_edge_upsample + svt_av1_upsample_intra_edge_c | `useUpsample` + `upsampleIntraEdge` (:760) | exact | dead above 8x8 (blk_wh > 16), audited per size |
| eb_av1_filter_intra_taps + svt_av1_filter_intra_predictor_c (C_DEFAULT/filterintra_c.c) | `filterIntraPredictor` (:694) | exact | verbatim asserts bw<=32 retained — FI is NOT signalable at 64x64 |
| sm_weight_arrays (intra_prediction.c:25) | `sm_w` rows + `sm_w32`/`sm_w64` GPU constants | exact | bs=16/32/64 rows verbatim |
| mode_to_angle_map / extend_modes / av1_is_directional_mode | `kModeToAngle` (:451) / `kExtendModes` (:435) / `isDrMode` inline test | exact | |
| Corner-blend regime (enc_intra_prediction.c:600-604) | in builder + 16/32/64 GPU kernels | exact | txwpx+txhpx >= 24; live at 16/32/64, dead at 4/8 |
| — | GPU `predict_block_4x4/8x8/16x16/32x32/64x64` | adapted | per-sample arithmetic 1:1 with the host builder (which is exact); launch mapping ours (256 threads at 4/8/16; 1024 threads at 32/64 — 64x64 = FOUR pixels per thread p = t + 1024k, the stated thread-map design); edge-buffer ownership reworked for shared memory; filter-intra path absent at 64x64 (not signalable) |

## l5_motion (src/l5_motion/motion.cpp)

| SVT symbol (file:line) | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| compute8x8_sad_kernel_c (motion_estimation.c:71) | `sad8x8` (:5) | exact | dedicated 8x8 kernel mirrored; GPU 8x8 kernel adapted from it (accumulators per-thread, warp-reduce) |
| svt_nxm_sad_kernel_helper_c (compute_sad_c.c:21) | `sad4x4` (:95), `sad16x16` (:44), `sad32x32` (:61), `sad64x64` (:78) | exact | dim loops unrolled to the template dims |
| — | GPU sad 4x4/8x8 kernels | adapted | same sums; 16x16/32x32/64x64 score host-side by design |

## l6_pipeline (src/l6_pipeline/pipeline.cpp)

| SVT symbol | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| av1_tranform_two_d_core_c / quantize / iscan via l3 | `encodeFrameRecon4x4..64x64`, `encodeFrameAuto4x4..64x64`, `encodeFrameRecon4x4Q..64x64Q`, `encodeFrameAuto4x4Q..64x64Q` (pipeline.cpp:1195+ for the 32x32 block, :1386 for Auto64x64Q) | adapted | composition: plane window (l1) + buildIntraPredictors (l4) -> int16 residual -> fwdTxfm2d -> invTxfm2dAdd; M1 availability + FR-series REAL recon top-right gather follow SVT's raster decode order |
| — | `decideBlockMode4x4/8x8/16x16/32x32/64x64` (decideBlockMode64x64 at :679) | policy | D2 policy is OURS: all 13 PredictionModes scored by SAD (SVT primitives), lowest wins, tie = lowest mode index; SVT selects modes via RD/trellis machinery we do not port |
| — | fixed `defaultScan*` for every block in the Q frame loops | policy | SVT selects scan order per mode/tx-type via get_scan_order; ours is fixed default scan (named in each loop's comment) |
| — | `NeighborContext` (filt_type plumbing) | policy | carries the two neighbor modes the generator shim needs; value semantics match enc_intra_prediction.c:186 |

## GPU runtime (l2_gpurt) and fixtures

- l2_gpurt (NVRTC JIT + driver API): **policy** (our infrastructure; no SVT
  counterpart).
- Test/golden fixtures (ramp frames, (11+7i)%251-style edge arrays, corner
  fill 7/127/128 regimes): **policy** — fixtures are ours; the code under
  test is SVT's.
- `svtd_gather_above` / frame drivers in tools/golden_gen/composition.c:
  **adapted** — documented specializations of the SVT composition with the
  get_filt_type shim; the D2 decision inside them mirrors the host policy.

## Known deviations (named, per the standing rule)

1. ADST at TX_64X64: not ported and not signalable — av1_txfm_type_ls[4] =
   {DCT64, INVALID, INVALID, IDENTITY64} (inv_transforms.h:196). The
   IDENTITY64 column is parked. No fadst64/iadst64 exist in this tree.
2. get_filt_type: shimmed (generator-controlled global) — flagged inline in
   svt_gen.c and mirrored by NeighborContext in the port.
3. GPU kernels are thread-mapped re-expressions of the exact host
   arithmetic (adapted), not verbatim C — their bit-exactness is enforced
   by tests against the host paths, and the host paths are gate-enforced
   against the verbatim generator.
4. The composite bench/GPU frame loops run host decisions + per-block
   launches (structure ours); this is a measurement-harness shape, not a
   port of SVT's schedule.