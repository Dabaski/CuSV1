# PV - function-by-function provenance table

Every port artifact in `src/l3_transforms`, `src/l4_intra`,
`src/l5_motion`, `src/l6_pipeline`, `src/l7_entropy`,
`src/l8_bitstream`, `src/l9_analysis` and `src/l10_filters` (plus the
l2 runtime and fixture classification below) traced to the pinned
vendored reference (`third_party/SVT-AV1/`, REFERENCE PINNING rule in
`AGENTS.md`). l0_core/l1_pixels are
infrastructure with no SVT symbol to trace (single policy row in the
classification section). The l9/l10 families' span-by-span anchors
(file:line per function) live in the A8/A11/A13/A14 rows of
`tools/coverage_areas.txt`; the tables below carry the family-level
record. Status vocabulary:

- **exact (mech)** - the SVT body was ported mechanically from the committed
  verbatim extract (`svt_gen.c`) with identifier/table renames only; the
  arithmetic text is byte-derived from the extract. Currently exactly two
  ports: `fdct64`/`fdct64B` and `idct64`.
- **exact (hand)** - hand-transcribed from the SVT source with the
  arithmetic text unchanged (mechanical renames, C++ `std::` spellings);
  bit-exactness is gate-proven by `tools/golden_gen`
  (expected_primitives.txt, 421 lines). All pre-extract-era ports (the
  C-series 4x4/8x8/16x16 kernels, the L-series 32x32 kernels, the helpers,
  and all of l4/l5) are this class.
- **adapted** - SVT structure is preserved but the port changes shape:
  thread mapping, buffer ownership, parameterization, or a documented
  specialization of a generic SVT function. The per-sample arithmetic is
  still 1:1 SVT; what changed is the wrapper around it.
- **policy** - project-defined decision logic that SVT does not prescribe
  at this scope. SVT primitives inside it are exact/adapted as listed.

Golden provenance: every expected value comes from `tools/golden_gen`
(committed generator; `extract.ps1` copies SVT bodies verbatim into
`svt_gen.c`), never from hand-traces. The generator's own provenance table
is in `tools/golden_gen/README.md`.

## l3_transforms (src/l3_transforms/transform.cpp)

| SVT symbol (file:line) | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| svt_av1_fdct4_new (transforms.c) | `fdct4` (transform.cpp:3469) | exact (hand) | hand transcription, gate fdct4 |
| svt_av1_fadst4_new | `fadst4` (:3495) | exact (hand) | gate fadst4 |
| svt_av1_fdct8_new | `fdct8` (:608) | exact (hand) | gate fdct8 |
| svt_av1_fadst8_new | `fadst8` (:678) | exact (hand) | gate fadst8 |
| svt_av1_fdct16_new | `fdct16` (:1059), cos_bit-parameterized | exact (hand) | gate fdct16 (col 13 / row 12) |
| svt_av1_fadst16_new | `fadst16` (:1063), cos_bit-parameterized | exact (hand) | gate fadst16 |
| svt_av1_fdct32_new | `fdct32` (:1069), cos_bit 12 fixed | exact (hand) | gate fdct32 |
| svt_av1_fadst32_new (static, transforms.c:1908) | `fadst32` (:2895) | exact (hand) | gate fadst32 |
| svt_av1_fdct64_new (transforms.c:762) | `fdct64`/`fdct64B` (:1347/:1351) | exact (mech) | DCT-only; mechanical port of the verbatim extract, SVT pointer-swap structure preserved (bf0/bf1 alternation); fdct64B takes cos_bit (col 13 / row 10) |
| svt_av1_idct4_new | `idct4` (:87) | exact (hand) | gate idct4 |
| svt_av1_iadst4_new | `iadst4` (:115) | exact (hand) | gate iadst4 |
| svt_av1_idct8_new | `idct8` (:161) | exact (hand) | clamps only where SVT consumes stage_range |
| svt_av1_iadst8_new | `iadst8` (:222) | exact (hand) | |
| svt_av1_idct16_new | `idct16` (:302) | exact (hand) | clamp stages 3-7 |
| svt_av1_iadst16_new | `iadst16` (:437) | exact (hand) | clamp stages 3/5/7; no all-zero early-out at 16 |
| svt_av1_idct32_new (inv_transforms.c:378) | `idct32` (:3077) | exact (hand) | clamps ONLY stages 3-9 (audited) |
| svt_av1_iadst32_new (static, inv_transforms.c:1132) | `iadst32` (:3328) | exact (hand) | clamps EVERY stage; no all-zero early-out at 32 |
| svt_av1_idct64_new (inv_transforms.c:1567) | `idct64` (:2112) | exact (mech) | mechanical extract port; clamp bit 16 every stage via stageRange shim (gen_inv_range_64x64_dct) |
| round_shift / half_btf (inv_transforms.h) | `roundShift` / `halfBtf` | exact (hand) | pure renames |
| clamp_value / clamp_buf (inv_transforms.c) | `clampValue` / `clampBufIv` | exact (hand) | |
| svt_aom_eb_av1_cospi/sinpi_arr_data (inv_transforms.c) | `kCospi13`/`kCospi12`/`kCospi10`/`kSinpi*` + `cospiRow` | adapted | table ROWS verbatim; the 7-row table flattened to the three used rows (13/12/10), cospiRow = cospi_arr(cos_bit) mirror |
| fwd_shift_4x4/8x8/16x16/32x32/64x64 (transforms.c) | shift constants in fwdTxfm2d* | exact (hand) | {2,0,0}/{2,-1,0}/{2,-2,0}/{2,-4,0}/{0,-2,-2} |
| inv_shift_4x4/8x8/16x16/32x32/64x64 (inv_transforms.c:21-22) | shift constants in invTxfm2dAdd* | exact (hand) | {0,-4}/{-1,-4}/{-2,-4}/{-2,-4}/{-2,-4} |
| fwd_cos_bit_col/row, inv_cos_bit_col/row (transforms.c:19-22, inv_transforms.h:32-41) | cos_bit constants (13/13, 13/13, 13/12, 12/12, 13/10; inv 12/12) | exact (hand) | read from the extracted tables, cited |
| av1_tranform_two_d_core_c (transforms.c:2398) | `fwdTxfm2d4x4..64x64` | adapted | generic core specialized per size: bd=8 fixed, no flips, fixed TxType column; col/row 1D calls + round_shift_array(-shift) structure preserved |
| inv_txfm2d_add_c / svt_av1_inv_txfm2d_add_64x64_c | `invTxfm2dAdd4x4..64x64` | adapted | same specialization; clamps only where SVT consumes stage_range; clip_pixel_highbd add |
| av1_txfm_type_ls (inv_transforms.h:196) | 64x64 DCT-only scope | exact (finding) | {DCT64, INVALID, INVALID, IDENTITY64} - no ADST signalable at TX_64X64; IDENTITY64 column parked, not ported |
| quantize_fp_helper_c (full_loop.c:222) | `quantizeFpN` (log_scale param), `quantizeFp4x4/8x8/16x16/32x32/64x64` | adapted | verbatim semantics with the helper's own log_scale parameter; escalated arithmetic cited (full_loop.c:228/:244/:246/:249) |
| svt_aom_quantize_b_c (full_loop.c:31) | `quantizeBN`, `quantizeB4x4/8x8/16x16/32x32/64x64` | adapted | escalated arithmetic cited (:36/:67/:69-70/:74) |
| svt_aom_init_iscan (coefficients.c:345-363) | `defaultScan4x4..64x64` | adapted | formula-based port at W=H=4..64 (the iscan loop instantiated per size) |
| dc_qlookup/ac_qlookup_QTX, svt_aom_dc/ac_quant_qtx, get_qzbin_factor, invert_quant (inv_transforms.c:3357-3516) | `buildQuantTables` | adapted | luma rows of svt_av1_build_quantizer, sharpness=0; the composition is ours, each table/step is verbatim SVT |
| svt_aom_eb_av1 dc/ac QTX lookup | GPU `quant_dequant_4x4..64x64` | adapted | fp helper arithmetic per-thread at log_scale 0/1/2; eob = max nonzero scan position + 1 (policy comment in kernel) |
| svt_aom_get_intra_uv_tx_type | `intraUvTxType` (transform.h:97) | exact (hand) | RT9 step 2: the intra-chroma tx type is DERIVED from the LUT (not the luma rule) - the c16 U/V divergence root cause |
| - | the l3 2D fwd/inv kernels gain PER-PASS 1D function selection | adapted | RT9 steps 1/3a: the 2D kernels selected ONE 1D function for both passes - wrong for asymmetric tx types; the 4x4/8x8 siblings joined 16x16 |
| inv_transforms.h + transforms.c + inv_transforms.c | TX1 the non-square 2D transform family: the generic cfg machinery (shift/type/cos_bit tables + two_d_core + dispatch switches) over 14 shapes, 76 admissible (shape, TxType) pairs (46 fwd / 62 inv), per-shape roundtrip classification (8x16 exact, 16x8 lossy-1 - the reference's own asymmetry, measured and pinned) | adapted | A1 charges 928 (the writer's 1,148 claim included 220 nb of highbd inv wrappers at inv_transforms.c:2901-:3137, NAMED-PARKED); host only, no GPU twins |
| definitions.h:1031-1047 + inv_transforms.h + transforms.c + inv_transforms.c | TX2 the remaining TxType surface: `setFlipCfg` (set_flip_cfg verbatim, inv_transforms.h:139-181), the vtx/htx tables all 16 rows, `fwdIdentity4..64`/`invIdentity4..64` (the reference's fidentity/iidentity renamed, named deviation), the fwd/inv core flip branches (transforms.c:2430-:2452, inv_transforms.c:2554-:2577 - the TX1 fixed-zero-flips deviation RESOLVED), IDTX at the 32-class shapes + the six one-axis types at TX_32X32 (the CONFIG=1 fallback branch, EbConfigMacros.h:246), `supported()` 165 fwd / 243 inv | exact (hand) | 204 nb measured span-by-span; the ten square facades route through the TX1 config+core path (the reference's own facade shape, svt_av1_transform_two_d_*_c / svt_av1_inv_txfm2d_add_*_c) with the landed DCT/ADST outputs unchanged; invTxfm2dAdd64x64 keeps the landed 4096 facade (no compaction - the port's callers expand, FS5d; the compacting form broke the FS3 emission walk and was reverted in-slice) |

GPU twins (fwd_txfm_2d_*, inv_txfm_2d_add_*, quant_dequant_*): **adapted** -
the device 1D bodies (`d_fdct4..d_fdct64`, `d_idct4i..d_idct64i`,
`d_fadst4..d_fadst32`, `d_iadst4i..d_iadst32i`) carry the verbatim stage
arithmetic; what differs is the launch geometry (one thread per column/row,
shared-memory staging) and mechanical renames (half_btf -> d_half_btf,
clamp_value -> d_cv, cos_bit -> bit). Every twin is bit-exact vs its host
port (tests in test_transform.cpp).

## l4_intra (src/l4_intra/intra.cpp)

| SVT symbol (file:line) | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| build_intra_predictors (enc_intra_prediction.c:159) | `buildIntraPredictors` (:455) | exact (hand) | the ONE extract substitution: get_filt_type(xd, plane) -> svtd_filt_type generator shim, flagged inline in svt_gen.c; the host port mirrors that shim with intra::NeighborContext carrying the same two mode values |
| svt_av1_dr_prediction_z1/z2/z3_c (intra_prediction.c) | `drZ1`/`drZ2`/`drZ3` (:202/:236/:262) | exact (hand) | gate-verified per zone |
| svt_aom_dr_predictor + eb_dr_intra_derivative + get_dx/get_dy | `drPredictor` (:734) + `getDx`/`getDy` (:345/:355) + `drIntraDerivative` (:312, verbatim table) | exact (hand) | |
| dc/dc_left/dc_top/dc_128/v/h/paeth predictors (intra_prediction.c) | inline dispatch arms inside `buildIntraPredictors` (DC_PRED :631, V_PRED :660, H_PRED :666, PAETH_PRED :672 - paeth_predictor_single mirrored inline via std::abs) | exact (hand) | |
| smooth/smooth_v/smooth_h predictors (intra_prediction.c) + sm_weight_arrays (:25) | `smoothPredict`/`smoothVPredict`/`smoothHPredict` (:365/:389/:408) + `smWeightArrays` (:291, verbatim table) | exact (hand) | |
| svt_aom_intra_edge_filter_strength + svt_av1_filter_intra_edge_c + filter_intra_edge_corner | `edgeFilterStrength`/`filterIntraEdge` (:95) + corner-blend path | exact (hand) | |
| svt_aom_use_intra_edge_upsample + svt_av1_upsample_intra_edge_c | `useUpsample` + `upsampleIntraEdge` (:760) | exact (hand) | dead above 8x8 (blk_wh > 16), audited per size |
| eb_av1_filter_intra_taps + svt_av1_filter_intra_predictor_c (C_DEFAULT/filterintra_c.c) | `filterIntraPredictor` (:694) | exact (hand) | verbatim asserts bw<=32 retained - FI is NOT signalable at 64x64 |
| sm_weight_arrays (intra_prediction.c:25) | `sm_w` rows + `sm_w32`/`sm_w64` GPU constants | exact (hand) | bs=16/32/64 rows verbatim |
| mode_to_angle_map / extend_modes / av1_is_directional_mode | `kModeToAngle` (:451) / `kExtendModes` (:435) / `isDrMode` inline test | exact (hand) | |
| Corner-blend regime (enc_intra_prediction.c:600-604) | in builder + 16/32/64 GPU kernels | exact (hand) | txwpx+txhpx >= 24; live at 16/32/64, dead at 4/8 |
| - | GPU `predict_block_4x4/8x8/16x16/32x32/64x64` | adapted | per-sample arithmetic 1:1 with the host builder (which is exact); launch mapping ours (256 threads at 4/8/16; 1024 threads at 32/64 - 64x64 = FOUR pixels per thread p = t + 1024k, the stated thread-map design); edge-buffer ownership reworked for shared memory; filter-intra path absent at 64x64 (not signalable) |
| C_DEFAULT/cfl_c.c:46-59 + intra_prediction_c.c:15-37 + :64-92 | HB1 (l4 share): the hbd twins `cflPredictHbd`, `upsampleIntraEdgeHigh`, `hbdDrZ3` with the complete 54-config dy-surface enumeration | exact (hand) | 62 nb (14 + 21 + 27); the 8-bit-discipline parks RESOLVE for A14's completion; derivation-relationship fixtures (hbd == lbd widened at bd 8); CONVERT_TO_SHORTPTR staging -> plain uint16 params (the CS1/LR1 precedent) |

## l5_motion (src/l5_motion/motion.cpp)

| SVT symbol (file:line) | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| compute8x8_sad_kernel_c (motion_estimation.c:71) | `sad8x8` (:5) | exact (hand) | dedicated 8x8 kernel mirrored; GPU 8x8 kernel adapted from it (accumulators per-thread, warp-reduce) |
| svt_nxm_sad_kernel_helper_c (compute_sad_c.c:21) | `sad4x4` (:95), `sad16x16` (:44), `sad32x32` (:61), `sad64x64` (:78) | exact (hand) | dim loops unrolled to the template dims |
| - | GPU sad 4x4/8x8 kernels | adapted | same sums; 16x16/32x32/64x64 score host-side by design |
| hash.c:28-:70 + hash_motion.c:20-:341 + global_motion.c:30-:212 | HM1: `hash_motion.{h,cpp}` (the crc32c pair, the hash-table machinery, the block-hash generation and the walk) + `global_motion.{h,cpp}` (the GM param surface incl. `refine_integerized_param` with the GmControls fields threaded as scalars) | exact (hand) | 473 nb judge-charge (41 + 269 + 163); 137 nb parked named (the two hbd twins + the correspondence/PCS walls); 21 TEST_CASEs, M1-M5 killed |
| C_DEFAULT/compute_sad_c.c:103-193 + C_DEFAULT/sad_av1.c:36-64 (+ the :21-:40 helper) | SW1: the SAD wrapper instances - `nxmSadKernelHelper` (the svt_nxm_sad_kernel_helper_c verbatim port) + the sadMxN macro + all 22 instances, the sadMxNx4D macro + all 22 instances, and the OBMCSADMxN macro + all 22 instances | exact (hand) | compute_sad_c.c and sad_av1.c land 100% of their portable surface, grep-verified 0 of 66 missing; the landed sad4x4..64x64 ARE the sadMxN instances at those five sizes; the PAIRED-METRIC CAVEAT STATED (instances are transcription of the reference's own dispatch surface; the algorithm cores were already landed) |

## l6_pipeline (src/l6_pipeline/pipeline.cpp)

| SVT symbol | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| av1_tranform_two_d_core_c / quantize / iscan via l3 | `encodeFrameRecon4x4..64x64`, `encodeFrameAuto4x4..64x64`, `encodeFrameAuto4x4Q` + `encodeFrameRecon8x8Q..64x64Q` + `encodeFrameAuto8x8Q..64x64Q` (pipeline.cpp:1206 for the 32x32Q block, :1567 for Auto64x64Q) | adapted | composition: plane window (l1) + buildIntraPredictors (l4) -> int16 residual -> fwdTxfm2d -> invTxfm2dAdd; M1 availability + FR-series REAL recon top-right gather follow SVT's raster decode order; note: there is no encodeFrameRecon4x4Q in the tree (the 4x4 quantized API is encodeFrameAuto4x4Q alone, pipeline.h:59) |
| - | `decideBlockMode4x4/8x8/16x16/32x32/64x64` (pipeline.cpp:183/:269/:717/:739/:761) | policy | the RD-lite policy is OURS (RE5): all 13 intra candidates scored by `rdliteDecide` - variance<<4 distortion + the l16 intraFastCost rate term through the RDCOST macro (strict < tie-break), the lambda = q == 0 ? 0 : q; RE6 adds the angle-delta enumeration {-3..+3} for directional candidates at bsize >= 8X8; chroma UV stays SAD-only (the rung boundary); SVT's full RD is not ported; the primitives inside are 1:1 SVT |
| - | fixed `defaultScan*` for every block in the Q frame loops | policy | SVT selects scan order per mode/tx-type via get_scan_order; ours is fixed default scan (named in each loop's comment) |
| - | `NeighborContext` (filt_type plumbing) | policy | carries the two neighbor modes the generator shim needs; value semantics match enc_intra_prediction.c:186 |
| kf luma symbol emission via l7 (BSF1) | in `encodeFrameAuto16x16`/`encodeFrameAuto16x16Q` | adapted | per block in raster order: getKfYModeCtx (entropy_coding.c:1004-1021) from the DECIDED neighbor modes + writeKfLumaMode (:1026-1040) + angle delta (directional, delta 0) + writeFilterIntra flag=0 where filterIntraAllowed (mode_decision.c:108-119); allow_update_cdf = 1 forced, ends with odEcStopEncode - see the BSF4-fix coupling invariant (deviations, item 6) |
| token emission via l7 (TS3) | in the 16x16 Q path (`entropy::writeBlockCoeffs`, pipeline.cpp:1072) | adapted | skip = 0 for all blocks (no skip decision), NA-driven contexts, intra_dir = the decided mode; the ecfrm_* gate lines pin modes AND eobs AND coeffs AND recon AND the byte stream equal to the generator drive |
| per-geometry Q emission (FS3/FS4) | in `encodeFrameAuto4x4Q`/`encodeFrameAuto8x8Q`/`encodeFrameAuto32x32Q`/`encodeFrameAuto64x64Q` (the optional w/fc/na emission params; the per-block walk [partition iff the frame reads one][skip][kf mode + the angle-delta symbol][FI iff the predicate][writeBlockCoeffs]) | adapted | the walk shape mirrors encode_partition_av1 + encode_skip_coeff_av1 + encode_intra_luma_mode_kf_av1 + av1_write_tx_type + the token chain, order per the aom decodeframe.c read order; bit-exact vs the fs2S_* gate lines per size (the 64x64 emission-domain = the TX_64X64 scan contract via quantizeFp64x64Token, n_coeffs=1024 over the compacted 32-wide quadrant, full_loop.c:1262); named deviation: the Auto64x64Q keeps the 4096-wide facade for the legacy/GPU callers, the emission case runs the compacted token domain (the FS5d dual-domain follow-up) |
| running partition contexts (FS5a) | in the 8x8Q/32x32Q/64x64Q emission blocks (updatePartitionContext after each coded block; the 4x4Q/16x16Q emit no partition symbol - named) | adapted | coding_loop.c:1700-1713 semantics via l7; for uniform PARTITION_NONE grids the running ctxs coincide with fresh INVALID cells (the lookup bit at the leaf's own bsl is 0 for every square size - the mutation check proved the fixture discriminates wrong ctxs) |
| chroma emission (CS4/CS5b + RT fixes) | `decideBlockModeUv` (pipeline.cpp:1742; the size-generic CH3 UV D2 policy: 13 folded candidates, UV_CFL_PRED excluded, the loop bound at :1748) + `decideBlockModeUv16x16` (pipeline.cpp:1720, loop bound :1726) + the chroma-emitting Q walk (lumaB 8/16/32/64 -> UV tx 4/8/16/32 per av1_get_max_uv_txsize common_utils.h:142-149) | adapted | per luma block: [luma kf mode][uv_mode - ONE per block, the V TU shares the U decision][uv delta where gated][luma chain][U chain][V chain - U BEFORE V, three per-plane NAs]; the partition+skip surfaces landed in the chroma walk (CS5b), the leaf emission follows decode_partition tree order (RT6), the chroma FI flag surface (RT7-b); the chroma-ownership rule is_chroma_reference (common_utils.h:315-320) + the 4x4-frame NO-chroma / (1,1)-owner asserts; the walk-order inversion + the CS2 fixture mismatch were both caught by the gate |
| pic_operators.c + C_DEFAULT/picture_operators_c.c | PO1: `pipeline::picops` in src/l6_pipeline/picops.{h,cpp} - the two residual kernels (NO clamp, the uint16 wrap-cast), the three full-distortion kernels + the 32-single dispatcher (shift-free int64 SQR), the yv12 copy trio, `generatePadding16Bit` (struct-free; AN10 had named it excluded) | exact (hand) | 199 nb; 172 nb parked named (the pack2d/unpack2d dispatches - callees are PU1's packunpack - and the five EbPictureBufferDesc-walled pic drivers); memset32bit grep-verified DEAD; CS1 later routed the C_DEFAULT sibling's hadamard family + pictureCopy + copyWxh into the same file |
| C_DEFAULT/picture_operators_c.c:176-:262 | HB1 (l6 share): the hbd hadamard surface in picops - the two hb_col8 first/second-pass column helpers + `highbdHadamard8x8` (the lbd `hadamard4x4..32x32` landed at CS1) | exact (hand) | 75 nb (26 + 26 + 23); brief non-reproductions named (the ~47 nb hadamard guess did not reproduce - measured 23 nb at :238-:262); the 8-bit-discipline park RESOLVES |

## l7_entropy (src/l7_entropy/entropy.cpp)

| SVT symbol (file:line) | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| svt_od_ec_enc_* encoder family (bitstream_unit.c:77-408) | `odEcEncReset`/`odEcEncodeBoolEqQ15`/`odEcEncodeBoolQ15`/`odEcEncodeCdfQ15`/`odEcEncDone`/`odEcEncTell`/`odEcEncTellFrac` | exact (hand) | verbatim semantics; named deviations: OD_MEASURE_EC_OVERHEAD #if blocks omitted (upstream 0), EB_UNLIKELY -> plain if, NOINLINE dropped, asserts dropped; two explicit static_cast<int16_t> where SVT's C narrows implicitly |
| OdEcEnc struct (bitstream_unit.h:101-120) | `OdEcEnc` + `OdEcWindow` | exact (hand) | verbatim member layout |
| od_ec_dec family (third_party/aom_dsp entdec.c:78-283) | `OdEcDec`, `odEcDecInit`, refill/normalize, `odEcDecodeBoolQ15`/`odEcDecodeCdfQ15`, `odEcDecTell` | exact (hand) | vendored aom_dsp subtree; explicit casts where SVT's C narrows; see deviation: od_ec_dec_bits_ |
| update_cdf (cabac_context_model.h:76-105) | `updateCdf` | exact (hand) | the one CDF adaptation primitive; rate = 4 + (count>>4) + (nsymbs>3), counter capped at 32 |
| aom_write_symbol (bitstream_unit.h:265-279), aom_stop_encode (:245-253) | `odEcWriteSymbol`/`odEcStopEncode` + `AomWriter` | adapted | nsymbs==2 -> bool(cdf[0]) routing + allow_update_cdf adaptation verbatim; buffer_parent/ownership glue dropped (caller assigns ec.buf) |
| aom_reader family (bitreader.c:14-22, bitreader.h:84-98) | `AomReader`/`odEcReaderInit`/`odEcReadCdf`/`odEcReadSymbol` + `odEcReadBit` (p=16384) | adapted | reader struct shape deviates from the vendored one (see deviations, item 7); ACCT_STR dropped |
| BlockSize enum (definitions.h:883-905), PredictionMode intra subset (:1169-1204), FilterIntraMode (:1295-1302), TxSize/TxType/TxSetType/TxClass/PartitionType/PlaneType | layer enums | exact (hand) | full SVT BlockSize mirrored because filter_intra_cdfs indexes by it; l0_core::BlockSize stays the project-minimal variant |
| svt_aom_get_kf_y_mode_ctx (entropy_coding.c:1004-1021) | `getKfYModeCtx` | exact (hand) | flattened: neighbor modes as explicit args (SVT reads from xd) |
| encode_intra_luma_mode_kf_av1 (:1026-1040) + angle-delta (:1032-1037) | `writeKfLumaMode`/`readKfLumaMode` | exact (hand) | ctx pair passed in; reader returns the decoded mode + raw delta symbol |
| filter-intra pair (:5047-5060) + predicate (mode_decision.c:108-119) | `writeFilterIntra`/`readFilterIntra`/`filterIntraAllowed`(+Bsize) | exact (hand) | CONFIG_ENABLE_FILTER_INTRA=1 resolved from the non-RTC defaults (EbConfigMacros.h:203) |
| default CDF tables (cabac_context_model.c:59, :87, :134-155, :157, :594-596, :614, :618, :801-1860) | `EcFrameContext` tables + `initDefaultEcFrameContext` (COPY_CDF of :740-767 slice; TD5b: the coefficient tables bucket-selected via the getQCtx mirror of :1907-1918) | exact (hand) | kf_y/angle_delta/filter_intra(_mode)/partition/skip tables + intra_ext_tx_cdf whole [3][4][13][17]; the 13 token families carried as the 4-bucket *_buckets.inc files mechanically split from the committed svt_gen.c extracts (no hand-transcribed values), bucket[idx] = get_q_ctx(base_q_idx) per the spec's init_coeff_cdfs |
| svt_aom_partition_cdf_length (entropy_coding.c:922-930), partition_plane_context (:945-960), encode_partition_av1 (:962-978), update_partition_context (coding_loop.c:1700-1713), gather cdfs (cabac_context_model.h:373-405) | `partitionCdfLength`/`partitionPlaneContext`/`writePartition`/`updatePartitionContext`/`partitionGather*` | exact (hand) | forced split writes NOTHING; gathered 2-symbol XOR branches with the temporary's adaptation discarded; INVALID 0xFF -> 0 |
| aom read_partition (decodeframe.c:1266-1293) | `readPartition` | adapted | aom decoder semantics, out-of-tree arbiter (no aom code extracted); gathered reads non-adapting |
| av1_get_skip_context (:983-989) + encode_skip_coeff_av1 (:995-1000) | `getSkipContext`/`writeSkip`/`readSkip` | exact (hand) | read twin = aom read_skip_txfm (decodemv semantics, arbiter); SEG_LVL_SKIP implicit-1 branch deferred with segmentation |
| LUMA DCT_DCT token chain (entropy_coding.c:355-544) | `writeTxbCoeffs`/`readTxbCoeffs` | exact (hand) | writer = svt write path, reader = aom decodetxb.c read_coeffs_txb symbol-for-symbol (incremental lower-level/br contexts, group-start derivation) |
| get_txb_ctx (entropy_coding.c:248-315) | `getTxbCtx` + `DcSignLevelCoeffNa` NA model | adapted | above[64]/left[64] flat uint8, packed (dc_sign << 6 | cul_level), sweep + OR-accumulate; txb_skip_ctx = 0 (whole-block TUs, :298-299); skip_contexts/chroma branches ported but dead for our luma case |
| eb_av1_nz_map_ctx_offset[19] (coefficients.c:24-303) + level helpers (coefficients.h:28-200) | `getNzMag`/`getNzMapCtxFromStats`/`getLowerLevelsCtx`(+Eob)/`getBrCtx`(+Eob)/`getPaddedIdx` + nz_map_ctx_offset.inc | exact (hand) | the full per-position pointer array (an initial hardcoded [TX_SIZES][26] guess was caught by the gate); nz-map is_eob position-only branch verbatim |
| svt_av1_txb_init_levels_c (rd_cost.c:93-105) | `txbInitLevels` | exact (hand) | abs/clamp level init, TX_PAD_2D state |
| svt_aom_get_nz_map_contexts_c (C_DEFAULT/encode_txb_ref_c.c:17-44) | `getNzMapContexts` | exact (hand) | |
| write_golomb (entropy_coding.c:236-243) + aom read_golomb twin | `writeGolomb`/`readGolomb` | exact (hand) | read twin from aom decodetxb.c; gate-enumerated byte-identical over [0,65535] (TD2) |
| tx-type surface (entropy_coding.c:317-353, common_utils.c:195-209, cabac_context_model.c:34-50) | `writeTxType`/`readTxType`/`getExtTxSetType`/`getExtTxSet`/`getExtTxTypes` + ExtTx tables | exact (hand) | reader mirror = aom decodetxb.c av1_read_tx_type; gated by getExtTxTypes > 1 AND base_q_idx > 0; DCT_DCT through eset 2 (DTT4_IDTX, 5 symbols, reduced_tx_set=1 intra) |
| odEcWriteLiteralBit(s) (bitstream_unit.h:255-263) | `odEcWriteLiteralBit`/`odEcWriteLiteralBits` | exact (hand) | |
| uv_mode surface (entropy_coding.c:1077-1095 write, decodemv.c:823-837 read) | `writeUvMode`/`readUvMode` + the ComponentType enum | exact (hand) | CS1b: ctx = [cfl_allowed][luma_mode] (the DECIDED luma mode); alphabet UV_INTRA_MODES - !cflAllowed = 14 at bsize <= 32x32 / 13 at 64x64; default_uv_mode_cdf (cabac_context_model.c:105) + uv_mode_cdfs_default.inc |
| uv angle delta (:1087-1092, decodemv.c:830-833) | `writeUvAngleDelta`/`readUvAngleDelta` | exact (hand) | bsize >= 8X8 AND the FOLDED mode directional; gate enumerated over the full 14-mode domain; GATED OFF in our emission (write emits nothing, read consumes nothing and returns -1) |
| component threading | ComponentType through writeTxbCoeffs/readTxbCoeffs/writeBlockCoeffs/readBlockCoeffs + the chroma txb_skip_ctx offset-7 branch live (get_txb_ctx :310-314, ctx {7,8,9} enumerated; the 10-branch dead - named) | adapted | six component-dim table families, the [PLANE_TYPES] rows selected by ComponentType; the tx-type symbol is LUMA-only in-chain (entropy_coding.c:374-376); write_cfl_alphas (:1060-1071) DEFERRED - dead in our emission |
| SYM1 the palette + segmentation symbol surface | six palette CDF families + the EcSegProbs struct (cabac_context_model.c:477-:586, the nine mechanical .inc extracts via tools/extract_palette_cdfs.ps1), `allowPalette` + `getPaletteBsizeCtx` (the pinned FINDING: the reference's bsize >= BLOCK_8X8 gate is ENUM order, not geometry - BLOCK_4X16/16X4 pass it), `deltaEncodePaletteColors`, `writeUniform`/`readUniform` + `readLiteralBits`, `writePaletteColorsY`/`readPaletteColorsY` + the local indexColorCache twin (DUPLICATED from l13 - l7 cannot link l13), `writePaletteModeInfo` (uv continuation PARKED - the pinned writer emits no uv palette, entropy_coding.c:4371), the fast-context family twin (palette.c:639-776, duplicated), `tokenizeColorMap` (palette.c:781-828) + `packMapTokens` + `getColorIndexContext` + `decodeColorMapTokens`, the wb trio duplicated into l7 + `rbRead*` + `writeSegFeatures`/`readSegFeatures`, `getSegmentId`/`updateSegmentationMap`/`getSpatialSegPrediction`/`negInterleave`/`negDeinterleave`/`writeSegmentId`/`readSegmentId` | exact (hand) | SVT-side 634 nb (481 new-to-the-port + 153 duplicated with l13) + 242 nb aom reader twins (out-of-tree, not in the denominator); THREE roundtrip-caught defects named (the SY8b cdf-row pointer indexed the ELEMENT dimension instead of the [ctx]-th ROW; the SY9 seg-enabled bit was a parameter where the reader must read it itself and FORCE update_map=1/temporal=0/update_data=1 at PRIMARY_REF_NONE; an out-of-range fixture map value was the fixture's bug); parks named: write_palette_colors_uv (does not exist in the pinned encoder), the uv palette families landed-but-unreachable (DCE'd upstream), the seg TEMPORAL path (the SVT writer SVT_ERRORs on it, entropy_coding.c:4895-4903) |

## l8_bitstream (src/l8_bitstream/bitstream.cpp)

| SVT symbol (file:line) | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| AomWriteBitBuffer (entropy_coding.h:116-119) + wb family (entropy_coding.c:1343-1382) | `AomWriteBitBuffer`, `wbIsByteAligned`/`wbBytesWritten`/`wbWriteBit`/`wbWriteLiteral`/`wbWriteInvSignedLiteral` | exact (hand) | NOINLINE hint dropped (l7 precedent); bodies via the inlined statics :1351-1370 |
| svt_aom_uleb_size_in_bytes + svt_aom_uleb_encode (entropy_coding.c:1313-1341) | `ulebSizeInBytes`/`ulebEncode` | exact (hand) | k_maximum_leb_128 constants folded, verbatim values (16383 = ff 7f measured) |
| write_obu_header (entropy_coding.c:3639-3654) | `writeObuHeader` | adapted | deviation: static in SVT, public here (consumed by tests + BSF3) |
| write_uleb_obu_size (:3656-3666), svt_aom_encode_td_av1 (:3953-3960) | `writeUlebObuSize`/`encodeTdAv1` | exact (hand) | TD return flattened EbErrorType -> 0 (API/EbSvtAv1.h:123); TD = exactly 2 bytes (12 00) |
| ObuType (av1_structs.h:22-32), AomCodecErr (definitions.h:1493-1497) | layer enums | exact (hand) | OK/ERROR slice only (consumed by write_uleb_obu_size) |
| write_sequence_header_obu (:3699-3763) + add_trailing_bits (:3668-3675) | `writeSequenceHeaderObu` | adapted | ratified BSF0(g) field values (profile 0, still_picture=1, monochrome per D1, 32x32 = max dims, filter-intra 1, order_hint 0 per D3, ...); payload only (header + uleb in the packer) |
| write_uncompressed_header_obu (:3294-3637) | `writeFrameHeader`/`writeFrameHeaderV2` | adapted | v1 = the BSF4-fix 22-bit walk (disable_cdf_update = 0 -> might_bwd_adapt region LIVE, refresh_frame_context = DISABLED emitted; 22 bits + 2 pad); v2 = lossy walk (base_q_idx = 100, delta_q block live, encode_loopfilter zeros, tx_mode_select = TX_MODE_LARGEST = 40 bits, 5 bytes, NO padding) |
| write_frame_header_av1 (:3843-3920), encode_sps_av1 (:3925-3948) | `assembleStructuralKeyframeTU`/`assembleStructuralKeyframeTUv2` | adapted | phase-1 measure / phase-2 uleb rewrite (SPS); TG header 0 bytes + tile copy with no per-tile prefix at tile_cnt == 1; v1 dst zeroed first (spec byte_alignment zeros - named); SPS byte-identical between v1/v2 (gate-HALTed if it moves) |
| D1 mono patch spans (entropy_coding.c:2689, :2706-2710, :2385-2386) | generator-side only (composition.c) | policy | court-ratified monochrome still-picture writer: is_monochrome const 0 -> 1, commented spec mono branch live, U/V quantization delta writes skipped (the num_planes guard the pinned writer lacks) - see deviations, item 5. CS3 UN-PATCHED it for color (the mono producers stay pinned for the mono artifact set, gate lines diff-0) |
| the color header v3 (CS3) | the SPS v3 color config + frame-header v3 in the color packer path | adapted | the D1 un-patch as the spec-faithful non-mono walks: mono bit 0, no early return, csp UNKNOWN 2 bits, separate_uv_delta_q 0 (color section 4 -> 7 bits); frame header 40 -> 42 bits (the U/V delta_q 1 bit each at 0; NO diff_uv_delta bit - the vendored writer emits it only at 1, :2379-2381); gate lines sps_obu_v3/frame_obu_v3/tu_bytes_v3 |
| - | the committed artifact set `src/l8_bitstream/tests/goldens/structural_keyframe*.obu` (the grayscale five: d4 30B, d8 30B, d16 44B, d32 47B v2, d64 441B; the color four: color4 183B, color8 137B, color16 133B, color32 165B) | policy | produced from the generator output (never hand-typed); three-way identity composed TU == file == gate bytes per artifact; the grayscale set decoder-accepted AND content-1:1 per geometry (FS5c, verify_decode4.ps1 -Geometry N); the color set full-plane content 1:1 4/4 (color16/color32 from CS5, color8 at CS7 `5ce0224`, color4 at RT21 `927e697` - the decode_conformance.md matrix); v1 bytes remain gated (tu_bytes) |

## l9_analysis (src/l9_analysis) - the AN-series

Host-only content analysis, 1:1 with the pinned analysis C; no GPU
twin (parked, named). Span-by-span anchors in the A11/A13/A14 rows of
`tools/coverage_areas.txt`; the family-level record:

| SVT source | Port family | Status | Notes |
| --- | --- | --- | --- |
| pic_analysis_process.c | mean/variance family: `mean8x8`, `meanSquare8x8`, `subMean8x8`, `subMeanSquareValues`, `intermVarFour8x8`, `b64Variance`, `variance16x16`/`variance8x8` | exact (hand) | svt_compute_mean_c :194-214, svt_compute_mean_squared_values_c :220-240, the sub-mean pair :242-284, svt_compute_interm_var_four8x8_c :286-313 (COMPOSES the pair - four side-by-side 8x8s along one row origin, not quadrants), compute_b64_variance :318-442 (every fold per array; the Full branch ported alongside the live Sub); the VAR macro instances from C_DEFAULT/variance.c |
| pic_analysis_process.c | sampling/stats: `calculateHistogram`, `downsample2d`, the pad family (`padInputPicture`, `padInputPicture16bit`, `padInputPictures`), `downsampleFilteringInputPicture` | exact (hand) | calculate_histogram :170-188 (file-local, no header twin), svt_aom_downsample_2d_c :134-164 (declaration twin motion_estimation.h:33-35, A3-charged), the pad spans :648-:866 + :1549-1615, the HME driver :1498-1547 |
| pic_analysis_process.c | detectors: `countColorsWithThreshold`, `isValidPaletteNbColors`, `findDominantValue`, `dilateBlock`, `getSbyPerpixelVariance16x16/8x8`, `scAaCollectCounts`, `isScreenContentAaAware`, `isScreenContent`, `isInputLumaDominant` | exact (hand) | the AN8 spans :911-1429 (the AA-aware form :1207-1350, the plain form :1354-1429) and AN9 :1440-1493 with its FRAME_LUMA_DOMINANT_* defines; the detectors consume NONE of the mean/variance primitives (checked against the reference's actual inputs) |
| fft_common.h + fft.c | `fft1d2..fft1d32` / `ifft1d2..ifft1d32` (the GEN_FFT/GEN_IFFT macro bodies, :113-414/:416-823) + the 2D drivers `fft2dN`/`ifft2dN` (fft.c:51/:108) | exact (hand) | float, the macros' own scalar instantiation (T_VEC = float); the fft.c entry-instantiation lines are not ported (our names are direct); the 2D inverse is the REAL/IMAG split-and-pack rearrangement, NOT a five-pass mirror; the fft->ifft roundtrip uses n * 1e-4 tolerance (test-header documented) |
| noise_util.c | the noise-tx family: `noisePsdGetDefaultValue`, `noiseTxMalloc`, `noiseTxForward`, `noiseTxFilterC`, `noiseTxInverse`, `noiseTxFree` + the OD_DIVU table | exact (hand) | :20-:111 (87 nb) - the spectral filter k_beta = 1.1f, the 1/n normalization lives in noise_tx_inverse (the composed forward->inverse is identity); the OD_DIVU table :113-:370 (258 nb) is transcription - the paired-metric caveat applies, stated; diff-verified + sentinel rows in test_noise_util.cpp |
| noise_model.c | the noise strength solver (:104-397), the flat-block finder (:399-634) + the model init/update (:636-1157): `noiseStrengthSolver*`, `flatBlockFinderInit/Free/ExtractBlock/Run`, `getBlockMean`/`getNoiseVar`, `noiseModelInit/Free/Update/SaveLatest`, `extractArRow`, `addBlockObservations(Internal)`, `addNoiseStdObservations`, `arEquationSystemSolve` | exact (hand) | AN16/AN17; named deviations: malloc-failure branches, the NOISE_MODEL_LOG_SCORE debug blocks and the use_highbd ternary parked (8-bit discipline); AomNoiseStatus -> enum class with the reference's own ordinals; the linsolve + fclamp dependencies counted under A13 |
| temporal_filtering.c | TF1: the planewise temporal filtering kernel family in src/l9_analysis/temporal_filter.{h,cpp} (namespace analysis::temporal) | exact (hand) | 534 nb measured span-by-span (judge exact): the log1p_tab_fp16 table :339-:565 (227, mechanically extracted, 0 hand-typed), noise_log1p :568-:586, the decay/shift factors :589-:619, the expf/sqrt tables :624-:677, the squared-error sum :697-:707 and both planewise kernel pairs (zz :738-:814, non-zz :930-:1087); the consumed MeContext fields threaded as parameters; the brief's "partial vs full split" phrasing did not reproduce (the half-open subblock/quadrant rule pinned via M3); parked named: the hbd twins, the subpel searches, block_plane_wise, apply_filtering_central |
| pack_unpack_c.c + blend_a64_mask_c.c + variance.c | PU1: `packunpack.{h,cpp}` - pack_unpack_c.c WHOLE (all 7 kernels), blend_a64_mask_c.c WHOLE (the vmask blend; its reference name carries "highbd" but the body is struct-free, judgment named), `varianceCore` + the 20 missing VAR instances | exact (hand) | 236 nb (183 + 31 + 22); the general two-buffer shape coexists with AN8's offs-folded shape; the FINDING pinned: at bStride 0 varianceCore still indexes b[0..w-1] - the offs array is REAL; parked named: SUBPIX/OBMC_VAR, the bilinear helpers, upsampled_pred, the highbd wrappers |
| variance.c (the SUBPIX/OBMC spans) | SUB1: the subpel variance family in src/l9_analysis/subpel.{h,cpp} - the bilinear 2-tap pair (`filterBlock2dBilFirstPass`/`filterBlock2dBilSecondPass`) + table, the SUBPIX_VAR macro + 22 instances, the obmc core + OBMC_VAR/OBMC_SUBPIX_VAR + 22 pairs | exact (hand) | 149 nb new-to-the-port; FOUR BRIEF CORRECTIONS by measurement: bilinear_filters_2t is [8][2] with row 0 = {128, 0}, the rounding is (sum + 64) >> 7, the counts are 22/22/22, the offset grid is 0..7 per axis (pinned over the FULL 64-offset grid); 15 TEST_CASEs, 7 mutations killed; upsampled_pred -> IP1 landed it (l12); the highbd wrappers -> HB1 |
| C_DEFAULT/variance.c:162-181 + :337-352 | HB1 (l9 share): `varianceHighbd` + `highbdMse16x16` in packunpack.{h,cpp} | exact (hand) | 31 nb; the 8-bit-discipline parks RESOLVE for A14's completion; derivation-relationship fixtures (hbd == lbd widened at bd 8) |
| pic_analysis_process.c (the vmaf spans) + temporal_filtering.c :3556-:3733 + firstpass.c | AM1+AN20: the vmaf activity-masking family - the nine vmaf calculators + box blur + unsharp + the preprocess driver (pa_ctx/pcs walls threaded), the preprocessing statistics (histogram bins + spatial statistics + gathering), the temporal_filtering.c helpers (`estimateNoiseFp16`, `avgMad`, gradient coherence, the unsharp+blur row kernels) and the firstpass pure accumulation pair | exact (hand) | 386 nb (238 + 89 + 135 + 13, judge re-measured exact); 25 TEST_CASEs, 11 mutations killed (M9 killed after a delta-plane strengthening); the gate_start +1 off-by-one UNOBSERVABLE under the committed float bands - recorded, not faked; the output_stats TWO_PASS wall stays parked; the INPUT side for the A4 RDO campaign, conformance-free |

## l10_filters (src/l10_filters) - the DF/CDEF families

Host-only post-filter kernel families; coverage-only (nothing is
wired into an encode loop, the filter enable bits stay 0). Span
anchors in the A8 row of `tools/coverage_areas.txt`; the family-level
record:

| SVT source | Port family | Status | Notes |
| --- | --- | --- | --- |
| deblocking_common.c | the mask/flat/hev gates (`filterMask2/filterMask/filterMask3Chroma/flatMask3Chroma/flatMask4/hevMask`), `filter4/6/8`, the `lpfHorizontal/Vertical 4/6/8` entry points, `updateSharpness` | exact (hand) | DF1 (:152-:589); blimit/limit/thresh flattened from the SIMD_WIDTH element-[0] reads to scalars; hev_thr excluded (set by the loop_filter_frame_init wall); highbd_* parked |
| deblocking_common.c | the 14-tap family: `filter14`, `mbLpfHorizontalEdgeW`, `lpfHorizontal14`, `mbLpfVerticalEdgeW`, `lpfVertical14` | exact (hand) | DF2 (:786-:889); flat2 = the OUTER flatness vs p0/q0; the h/v count pins (count=1 step=4 / count=4) mutation-verified; the else chain stacks filter14 -> filter8 -> filter4 from the DF1 primitives |
| cdef.c + cdef_copy.h + enc_cdef.c | `cdefFindDir` (+ dual/list + the 8-bit twins), `cdefFilterBlock` (16-bit) / `cdefFilterBlock8bit` / `cdefFilterBlock8bitBounded`, `cdefPriStrength`/`cdefSecStrength`, the fb/lbd driver chain (`cdefFilterFb`, `cdefFilterFbLbdImpl`, the luma/chroma wrappers, `cdefFilterFbLbd`), the copy helpers (`cdefCopyRect8To16`, `cdefCopyRect8`, `cdefCopyRect16`, `cdefCopySb8To16`, `cdefFillRect`) | exact (hand) | CDEF1 (find_dir + filter kernel family, 367 nb) + CDEF2 (the fb/lbd drivers + copy helpers, 377 nb); the direction table is the padded +2-offset form; the 16-bit kernel is the SPEC, the 8-bit twins their own 1:1 bodies with the derivation pinned BY TESTS; the strength derivation factored out for testability (cdefSign precedent); corner_match/corner_detect orphaned |
| enc_cdef.c | the mse/search block: `cdefMse8xn/4xn16bit`, `cdefMse8xn/4xn8bit`, `cdefComputeDist16bit/8bit`, `cdefApplyFb` (the halo/copy walk, inside CDEF_8BITS_PATH), `cdefSearchOneDual`, `cdefJointStrengthSearchDual` | exact (hand) | CDEF3 (318 nb measured by the judge span-by-span; the mse kernels mse_8xn_16bit :24-:35 through mse_4xn_8bit :63-:76, the dist bodies :77-:151, cdef_apply_fb :200-:298, the dual searches :714-:818); PARKED (named): svt_sb_compute_cdef_list, svt_av1_cdef_frame, svt_pick_cdef_from_qp, propagate_cdef_strength, finish_cdef_search (the struct-walled driver chain) |
| restoration.c | the math core: `sgrParams` (:31-:49), `extendFrameLowbd` + the `extendFrame` entry, `copyTileLowbd` + the dispatcher, the `boxsum` family (:430-:594), `decodeXq` (:597-:608), the `xByXplus1`/`oneByX` tables | exact (hand) | LR1 (243 nb measured span-by-span); the boxsum edge handling is TRUNCATION (a 2D box sum clamped at the array bounds), pinned by an independent clamped-box oracle; the data tables carry 0-mismatch formula oracles over ALL entries; svt_decode_xq is the params-aware form (r[0]==0 -> xq[0]=0, xq[1]=(1<<7)-xqd[1]; else xq[1]=(1<<7)-xq[0]-xqd[1]), not the spec-style constants; file-local statics exported for testability (cdefSign precedent); highbd assert-parked (8-bit discipline) |
| restoration.c :632-:962 | the selfguided restoration family: `selfguidedRestorationFastInternal` (estimation rows stepped by 2, the *6/*5 kernels), `selfguidedRestorationInternal` (stride 1, the 3x3 *4/*3 kernel), `selfguidedRestoration` (dgd32 staging + the r[0]/r[1] skip pair), `applySelfguidedRestoration` | exact (hand) | LR2 (306 nb measured, 6 named-parked: the highbd staging loop and the CONVERT_TO_SHORTPTR write); the SGR constant set (SGRPROJ_* ) carried in restoration.h |
| restoration.c :216-:397 + :399-:477 + :1040-area walkers | LR3: the stripe/unit family - `wholeFrameRect`, `countUnitsInTile`, `getStripeBoundaryInfo`, the `setupProcessingStripe`/`restoreProcessingStripe` pair, `wienerFilterStripe`, `sgrprojFilterStripe`, `loopRestorationFilterUnit` | exact (hand) | 249 nb ported, 571 nb named-parked (the highbd stripes, the Av1Common-walled filter_frame pair, the tile walkers); the wiener stripe consumes convolve::wienerConvolveAddSrc (the l10 link line gained av1_convolve under a JUDGE COURT ORDER, one line); FINDINGS pinned: WienerInfo's alignas(16) is LOAD-BEARING (the convolve selects its tap row BY ADDRESS ALIGNMENT, measured offV=15) and the wiener stripe's w round-up rule differs from sgrproj's plain AOMMIN |
| grainSynthesis.c | GR1: the film grain synthesis core in src/l10_filters/grain.{h,cpp} (namespace l10_filters::grain) - `gaussianSequence` (:28-:181, 2,048 entries transcribed, caveat STATED, diff-verified), the rng pair (:306-:328), `initArrays` (:182-:266), the luma/chroma grain generators (:328-:466), scaling (:466-:506), `addNoiseToBlock` (:506-:607), `filmGrainParamsEqual` (:709-:793), the copy helpers + boundary overlaps (:793-:865) and the frame driver `addFilmGrainRun` (:865-:1140) | exact (hand) | 998 nb of 1,140; hbd :607-:709 (92) + dealloc :266-:287 (18) named-parked; the brief's noise_shape parameter does not exist in the reference (corrected); ~30 TEST_CASEs, 15 mutations killed |

## l11_resize (src/l11_resize) - AN21

| SVT source | Port family | Status | Notes |
| --- | --- | --- | --- |
| resize.c :147-:489 + :36-:137 + super_res.c :20-:47 | the resize 1D kernel family (`resize` namespace): the resampling algorithm generic over the filter phase + the four interpolation tables + the 1000-set | exact (hand) | 328 nb algorithm (judge re-measured) + 99 nb tables + 28 nb charged to A8; 21 TEST_CASEs, 8 mutations killed; one property-RED was a TEST BUG (the in_length==1 break) corrected with M3 proving the correction's discrimination; parked named: save_YUV :782-:791, pack/unpack_highbd :791-:881, the EbPictureBufferDesc resize_frame driver :881+ |
| resize.c :489-:779 | RS2: the hbd resize 1D family - the highbd interpolate cores, the symmetric even/odd downsamplers, the multistep resampler, the fill pair and the plane drivers | exact (hand) | 273 nb (judge re-measured exact), resolving the AN21 8-bit discipline park (the HB1 pattern); BRIEF CORRECTION measured: the hbd path does NOT scale filter coefficients by bd - the only bd dependence is clip_pixel_highbd (cited per call site); the hbd@bd8 == 8-bit-twin sweep over the enumerated length set + bd-10/12 clamp-firing absolutes; 35 TEST_CASEs / 921 assertions, 6 mutations killed (M3's boundary flip SURVIVED its first fixture - killed after an edge-contrast input); the stale-exe lesson re-learned: every mutation is now compile-checked before its kill is claimed |

## l12_convolve (src/l12_convolve) - CV1

| SVT source | Port family | Status | Notes |
| --- | --- | --- | --- |
| convolve.c | the convolution kernel family (`convolve` namespace): scalar products + `getFilterOffset`/base, the add-src horiz/vert hip paths, the wiener add-src (`wienerConvolveAddSrc`, the uint16 staging contract: temp stride 128, h+6 rows, sentinel row), `convolveHoriz`/`convolveVert` + the convolve8 pair, `ConvolveParams` + `getConvParamsWiener` + the `sub_pel_filters_8` table (128 entries, diff-verified) | exact (hand) | 163 nb (judge band 161-163); highbd :155-:253 (93) parked; 14 TEST_CASEs, 6 mutations killed; LR3's wiener stripe depends on it |
| inter_prediction.c (the filter tables) + C_DEFAULT/variance.c:71-:137 | IP1: the interp filter-params machinery + `upsampledPred` in l12_convolve - the five filter tables (sub_pel_filters_4/8sharp/8smooth/bilinear/4smooth, 80 nb, mechanically transcribed + diff-verified), `extractInterpFilter`, `getConvolveFilterParams`, `getInterpFilterParamsWithBlockSize`, `makeInterpFilters` + the enums/struct | exact (hand) | +203 across A3 (+105 tables + params/getters), A13 (+34 enums/struct), A14 (+64 av1_get_filter + upsampled_pred - BRIEF CORRECTION: the brief misplaced upsampled_pred in inter_prediction.c, grep 0 hits there; the body (void)s every struct param and carries four paths); 33 TEST_CASEs / 1,941 assertions; 7 mutations (6 killed + M2 SURVIVES BY CONSTRUCTION - a dead staged row is output-invisible, sharpened to a killed M2b); M7 FINDING: composition tests are BLIND to shared-selection mutations - the enumeration tests discriminate |

## l13_palette (src/l13_palette) - PAL1 + SEG1

| SVT source | Port family | Status | Notes |
| --- | --- | --- | --- |
| palette.c (+ pic_analysis_process.c :868-:909) + k_means_template.h | PAL1: the palette + k-means family (the palette builders/tokenizers + `countColors` + k-means bodies both dims, k = 2..8 enumerated) | exact (hand) | 389 nb of palette.c (+18 of pic_analysis_process.c charged to A11 - count_colors' body lives THERE); BRIEF CORRECTIONS pinned: dim-2 k-means has no portable-C caller (palette.c:526 is dim 1) - ported + tested anyway; 39 TEST_CASEs, 9 mutations killed (three sharpened after surviving their first form, disclosed) |
| segmentation.c + segmentation_params.{c,h} | SEG1: the struct-free segmentation surface (`getVarianceForCu` :23-:84, `calculateSegmentationData` :260-:271, `findSegmentQps` :273-:326 + the params tables) | exact (hand) | 116 nb ported, 165 parked (the roi_map/PCS walls); `temporally_update_qps` grep-verified DEAD (declared, defined nowhere); the QUIRKS preserved 1:1 and fixture-pinned: the ROUND identity (step_size can only ever be 1), the POW2 narrowing wraps, the descending bin-edge pairing, the transposed getVarianceForCu index pairings, log2f_safe = get_msb(x\|1); 17 TEST_CASEs, 6 mutations killed |

## l14_utility (src/l14_utility) - U1

| SVT source | Port family | Status | Notes |
| --- | --- | --- | --- |
| utility.c + common_utils.c (ssSizeLookup + getPlaneBlockSize) | U1: the utility surface (`l14_utility` namespace) - the block geometry builder `svt_aom_build_blk_geom` (:332-:413 = 82), the md-scan walk (:227-:289), the coded-unit + mini-GOP stats arrays, the ns/d1-depth-offset tables, `hvsizeToBsize` | exact (hand) | 347 + 31 = 378 nb to A13; the FULL builder enumeration over all 11 geoms + the GEOM_2 85-block cross-check against coded_unit_stats_array (two independent transcriptions agreeing through the algorithm); 26 TEST_CASEs / 647 assertions; 6 mutations killed - M6 by CRASH (the mutated gate overruns the 41-entry test array, fail-fast 0xC0000409, mechanism named); park named: the FIXED_POINT_ASSERT_TEST breakpoint (dead under NDEBUG) |

## l15_rc (src/l15_rc) - RC1

| SVT source | Port family | Status | Notes |
| --- | --- | --- | --- |
| (the rate-control qindex-mapping table source file) | RC1: `rc_tables.h` - 18 qindex-mapping tables x 256 | exact (mech) | 278 nb, mechanically extracted; the runtime diff-verify test parses the reference file itself; the PAIRED-METRIC CAVEAT STATED (transcription, not algorithm - the AN14 OD_DIVU precedent); RED included the STALE-EXE TRAP (a ctest 'pass' over a failed build - caught, re-run clean) |

## l16_rdo (src/l16_rdo) - the RE-series (A4 machinery)

The A4 golden-regeneration campaign's machinery rungs; CONFORMANCE-FREE
(no emission change, no golden churn - the wiring rung opens next).
Span-by-span anchors in the A4 row of `tools/coverage_areas.txt`; the
family-level record:

| SVT source | Port family | Status | Notes |
| --- | --- | --- | --- |
| md_rate_estimation.c :50-:640 (+ the header tables) | RE1: the rate-estimation core - `getSyntaxRateFromCdf`, the estimate_syntax_rate INTRA surface, the mv class + nmv cost tables (`getMvClass`/`mvClassBase`), the coefficient-rate estimator, the interintra-allowed helpers, av1_prob_cost + the ext-tx tables | exact (hand) | 485 nb JUDGE-CHARGED (the writer's 645 struck: the parked inter block 90 + the 69-nb struct carry per the AN17 precedent); 110,035 assertions, 10 mutations killed; M3's LESSON: an oracle reading the same fixture as the port cannot see a substitution bug (the fixture was made divergent); the cfl sign-cost quirk verbatim (the first draft's sign_fac_bits on both planes caught by the oracle); parks named: update_stats (BlkStruct-walled), update_part_stats (pcs-walled), the inter block |
| full_loop.c :613-:1236 + the verified carries | RE2: the optimize_b trellis - the cost helpers, the three trellis updaters + update_skip + update_coeff_eob_fast, `fastOptimizeB`, the driver with the pcs/ctx walls threaded as OptimizeBArgs; the GENERATED eb_av1_scan_orders (via svt_aom_init_iscan), the tx-size tables, get_eob_pos_token/get_txsize_entropy_ctx, cabac + rd_cost carries, and the court-ordered nz_map_ctx_offset.inc byte-copy (runtime twin-checked against both the l7 copy and third_party coefficients.c) | exact (hand) | 1,222 nb total charged (A1 +1,043, A4 +33, A5 +30, A13 +116; carries verified as function/table surface, not struct carries, per the AN17 precedent); 17 TEST_CASEs / 611,254 assertions incl. the RD-MONOTONICITY property over the full grid + the full test-side transcription oracle; 9 writer mutations + the judge's J3 survivor sharpened (the fast path's zbin-trim had no boundary fixture; the flip band absCoeff in [29,56] at shift 0); BRIEF CORRECTIONS: the span map drifted, "scan tables landed in l3" was WRONG, the tx-helper family lives in l7 (unlinkable) |
| rd_cost.c :107-:640 + the carries | RE3: the rd_cost.c symbol-cost surface - transform_type_rate_estimation, update_eob_context (the cdf state-comparing grid), cost_skip_txb, the one_eob + eob_loop + cost_coeffs_txb driver, the uv fast rate + intra fast cost; the mv trio subset, av1_ext_tx_ind + update_cdf + get_unsigned_bits/write_uniform_cost, the ext-tx getters, is_comp_ref_allowed | exact (hand) | 576 nb JUDGE-CHARGED (the writer's 583 minus THREE double-counts caught at merge: build_nmv = RE1's span, get_qc_dqc/get_coeff_dist = RE2's spans, the nz-map twin = the RE2 .inc re-listed); 24 TEST_CASEs incl. the 19-shape x 16-type tx-type grid; 8 writer mutations + THREE JUDGE PROBES all killed (the eobFlag cdf16 adaptation nsymbs corruption caught by BOTH the state assert and the state-comparing memcmp grid; the CFL high-nibble index corruption; the driver's not-skip base-cost swap); parked named: the inter surfaces (~1,074 nb) + the in-body palette tail |
| ac_bias.c + the candidate injectors (mode_decision.c) | RE4: ac_bias + the intra candidate-injection family - the psy-distortion family (the brief's 'ac_bias tables' do not exist - corrected; the hbd twin parked), the candidate-set CONSTRUCTION over the enumerated intra modes + the angle-delta counter walk (the search's definition), `isValidMvDiff` + the classified support carries | exact (hand) | 462 nb (117 + 201 + 14 + 144, judge-verified span-by-span; the InjectedCandidate carry uncharged per the AN17 precedent); 39 TEST_CASEs (114 total, 644,596 assertions); 9 mutations killed (M9 the psyFullDist llrint SURVIVED its first fixture - sharpened with a half-to-even discriminator, the RE2 J3 pattern); THE ERRATA (judge-adjudicated at 9e999ef): FILTER_INTRA_MODES 7 -> 5 with the definitions.h:1361 mis-citation fixed (the reference's count is 5 at definitions.h:1295-:1302) AND fimodeToIntradir's 5th entry DC_PRED -> PAETH_PRED (=12, the reference's fimode_to_intramode at definitions.h:1304) - a latent semantic bug, latent-safe because the FI costs are not wired; the test oracle mirrored both errors, fixed together; parked named: the palette injection, the sframe/me candidates (inter), the psy hbd twin; the decision loops LANDED at RE5/RE6 |
| (the l6 wiring - pipeline.cpp + composition.c) | RE5: the RD-lite D2 wiring rung (the first gated golden event) - `rdliteLambda` (the policy lambda, q == 0 ? 0 : q), `initRdliteFrameCtx` (the l7 EcFrameContext -> l16 MdRateEstimationContext bridge), the kf-y ctx bridge, `rdliteDecide` (variance<<4 distortion + intraFastCost + RDCOST, strict < tie-break) wired into decideBlockMode4x4..64x64 + the generator svtd_rdlite_decide twin; chroma UV stays SAD-only | policy (the composition is ours; every consumed primitive is 1:1 from RE1-RE4) | the new mode map EXPOSED two latent FI emission bugs the SAD map masked (d4: the FI symbol through a per-site local cdf re-initialized from default while the decoder reads the ADAPTING frame_context->filter_intra_cdfs[bsize]; d32: svtd_bsf3_tile_data_v2 had NO FI write at all) - both fixed before the regen; artifact matrix d4 30->31 / d16 44->43 / d32 47->49 / d64 441->440 / color4 183->199 / color8 137->147 / color16 133->136, ALL decode content-1:1; GATE 884,851 -> 884,651 B (-207, 96 lines); the paired conformance count re-verified 9 artifacts x decoders |
| (the l6 wiring - pipeline.cpp + composition.c) | RE6: the angle-delta wiring rung (the second gated golden event) - the emission delta=0 park (pipeline.h:156) RESOLVES: rdliteDecide enumerates angleDelta over {-3..+3} for directional candidates (bsize >= 8X8; at 4x4 no variants), the angular-pred-level policy = 1 (ours, named), ModeDecision gains the angleDelta field; 11 predictor sites + 7 writeKfLumaMode sites; UV delta stays 0 (the chroma rung boundary); the mirror gains the enumeration (17 call sites); new gate fixtures bsf1q_* + bsf3v2_deltas | policy (as RE5) | artifact matrix: d4/d8/d16/d64 byte-identical, d32 49->50, color4 199->209, color8 147->146, color16 136 moved; GATE 884,651 -> 884,897 B (+246, 427 lines); 6 mutations (4 killed + M3/M5 SURVIVED their first forms and were SHARPENED: the distortion-dominance blind spot -> the direct rate-consumption pin; the no-delta-3-fixture blind spot -> the written-symbol off-by-one kill); the judge probe (the emission delta-symbol arithmetic +1 off) killed by the l7 gate-byte fixture; the paired conformance count re-verified 9/9 |

## GPU runtime (l2_gpurt), infrastructure, and fixtures

- l2_gpurt (NVRTC JIT + driver API): **policy** (our infrastructure; no SVT
  counterpart).
- l0_core (Sample, BlockSize) and l1_pixels (pixels::Plane strided buffer):
  **policy** - infrastructure with no SVT symbol to trace (they exist so the
  layers above can hold SVT-shaped data; the data layout they carry is
  defined by the SVT calls listed above).
- Test/golden fixtures (ramp frames, (11+7i)%251-style edge arrays, corner
  fill 7/127/128 regimes): **policy** - fixtures are ours; the code under
  test is SVT's.
- `svtd_gather_above` / frame drivers in tools/golden_gen/composition.c:
  **adapted** - documented specializations of the SVT composition with the
  get_filt_type shim; the D2 decision inside them mirrors the host policy.

## Known deviations (named, per the standing rule)

1. ADST at TX_64X64: not ported and not signalable - av1_txfm_type_ls[4] =
   {DCT64, INVALID, INVALID, IDENTITY64} (inv_transforms.h:196). The
   IDENTITY64 column is parked. No fadst64/iadst64 exist in this tree.
2. get_filt_type: shimmed (generator-controlled global) - flagged inline in
   svt_gen.c and mirrored by NeighborContext in the port.
3. GPU kernels are thread-mapped re-expressions of the exact host
   arithmetic (adapted), not verbatim C - their bit-exactness is enforced
   by tests against the host paths, and the host paths are gate-enforced
   against the verbatim generator.
4. The composite bench/GPU frame loops run host decisions + per-block
   launches (structure ours); this is a measurement-harness shape, not a
   port of SVT's schedule.
5. D1 monochrome patch (BSF3): the pinned SVT OBU writer is
   hardcoded non-mono; the generator applies three court-ratified spans
   (entropy_coding.c:2689 const 0 -> 1; :2706-2710 the commented spec
   mono branch live; :2385-2386 the U/V quantization delta writes
   skipped - the spec's num_planes guard the pinned writer lacks,
   decodeframe.c:5121-5122). Decoder-confirmed: libdav1d/ffprobe read
   the stream back as gray.
6. THE COUPLING (BSF4-fix): SVT sets ec_writer.allow_update_cdf =
   !disable_cdf_update (ec_process.c:101). Both l6 emission sites hardcode
   allow_update_cdf = 1, correct ONLY because the ratified config
   carries disable_cdf_update = 0 (SVT keyframe default,
   resource_coordination_process.c:360); any future disable_cdf_update
   = 1 config must flip the emission with it, or the stream is
   unspecifiable (the writer adapts CDFs a conformant decoder will not
   replay). Found by the decoder (libdav1d AVERROR_INVALIDDATA), fixed
   as an invariant.
7. l7 `AomReader` struct shape: {ec, allow_update_cdf} (aom-upstream
   shape) vs the vendored bitreader.h aom_reader = {buffer, buffer_end,
   ec, allow_update_cdf} - the buffer-ownership glue is unported
   (harmless in l7's self-consistent use). Found by the TD2 gate harness
   (casting one onto the other crashed 0xC0000005); the harness now
   exchanges bytes/values only (l7_ctx_shim.cpp extern-C shims).
8. `od_ec_dec_bits_` (entdec.h:64) is DECLARED in the pinned tree with
   NO definition anywhere in it (grep-verified) - raw-bits decode is
   unported, not needed by the intra symbol subset.
9. TS1 token-tables deviation - RESOLVED 2026-09-21 (TD5b): the writer
   emitted the default (idx-0) coefficient CDF bucket for every frame
   regardless of base_q_idx. The spec's init_coeff_cdfs
   (07.bitstream.semantics.md:1800-1820; the verbatim get_q_ctx,
   cabac_context_model.c:1907-1918) mandates the bucket by base_q_idx
   (q100 -> idx 2). Named the q100 tile-divergence root cause by the
   TD4 spec arm + the real-dav1d row exhibit (aom token_cdfs.h:861),
   ratified by the court, fixed in TD5a (generator) and TD5b (the l7
   mirror); the TD0 ladder flipped to 7/7 byte-exact and the 47-byte
   artifact decodes content-1:1 (byte-diffs 0/1024).
10. FS-series generator-drive self-consistency traps - both caught by
    the l6 bit-exactness tests, both fixed:
    (a) FS3 (11a0e7e): svtd_fs2_drive omitted the angle-delta symbol
    after a directional kf mode at bsize >= 8x8; its read twin omitted
    it too, so the drive's rt=1 was self-consistent with its own
    omission - the l6 emission (writeKfLumaMode emits the delta,
    entropy_coding.c:1030-1037) diverged on the fs28/fs232/fs264 bytes
    and the regenerated gate exposed it.
    (b) FS5b (a853ac9): the new S==16 drive branch fell through the
    fwd/quant/recon switches into the 4x4 machinery (svtd_fwd2d4x4 etc.
    on a 256-entry block); the fs216 lines were self-consistent (rt 15 1)
    but wrong - the l6 hand-rolled walk diverged at the token chain
    (24 bytes vs 6) and the bit-exactness test caught it.
11. FS5c 4x4-frame structure (687625f): the decoders align frame dims
    to 8 pixels (aligned_width = ALIGN_POWER_OF_TWO(w, 3)), so a 4x4
    frame has a 2x2 mi grid and the 8x8 node READS a 4-symbol partition
    symbol (partition_plane_context bsl=0, the fresh ctx-0 row
    {19132, 25510, 30392}; dav1d 1.5.4 trace-verified). A 4x4 TU exists
    only as a partition leaf; the d4 artifact codes the 8x8 frame as
    [part@8 SPLIT][4x 4x4-TU leaf walks]. A single-4x4-TU tile desyncs
    the real decoder at its first symbol.
12. FS5d (OPEN, named follow-up): the Auto64x64Q carries two domains -
    the legacy (non-emission) callers keep the 4096-wide facade (the
    GPU twin quant_dequant_64x64 is a 4096-position kernel), the
    emission case runs the vendored compacted 1024-position token
    domain. Unification = a 1024-position GPU kernel + bench work;
    for the fs264 fixture the two domains' recons coincide (the
    outer-ring coefficients quantize to 0 at q100).