# The layer stack - how everything works

Orientation doc for CuSV1, the port. Symbol-by-symbol provenance lives in
`docs/provenance.md`; the golden generator's own inventory in
`tools/golden_gen/README.md`; the bitstream path in `docs/bitstream.md`;
decoder-acceptance status in `docs/decode_conformance.md`. Standing rules
(TDD, citation rule, REFERENCE PINNING, bench-before/after) live in
`AGENTS.md`.

## The stack at a glance

```
l0_core    shared minimal types
l1_pixels  strided pixel buffers
l2_gpurt   NVRTC JIT + CUDA driver-API runtime
l3_transforms   transforms + quantizer     (host + GPU twins)
l4_intra        intra prediction          (host + GPU twins)
l5_motion       SAD                       (host; GPU at 4x4/8x8)
l6_pipeline     composition + decision    (host + GPU frame paths)
l7_entropy      entropy coder + symbols   (host, gate-exact)
l8_bitstream    raw-bit writer + OBU      (host, gate-exact)
l9_analysis     content analysis + temporal filter
                                       (host; AN/TF/PU/SUB + hbd twins)
l10_filters     deblock/CDEF/restoration/grain
                                          (host; DF/CDEF/LR/GR, not wired)
l11_resize      resize 1D kernels         (host; AN21)
l12_convolve    convolution + interp filters (host; CV1/IP1)
l13_palette     palette + k-means + segmentation (host; PAL1/SEG1)
l14_utility     block geometry + utilities (host; U1)
l15_rc          rate-control tables + arithmetic (host; RC1/RC2)
l16_rdo         rate-estimation + trellis + symbol costs + mode config
                                       (host; RE1-RE7, wired into l6)
l17_inter       inter-prediction compound/distwtd/scale + sr convolve
                                       (host; MC1)
                          |
             tools/golden_gen (the gate)
             tools/bench (perf)
             tools/decode_handoff.ps1 + td0_ladder.ps1 (decoders)
```

Host reference paths are taken 1:1 from the pinned vendored SVT-AV1
snapshot and re-expressed as CUDA C++ kernels JIT-compiled at runtime
via NVRTC. Every layer is developed under incremental TDD; GPU twins
are held to bit-exact agreement with the SVT host C.

## l0_core / l1_pixels - infrastructure

`Sample` (uint8) and `BlockSize` (the project-minimal square variant)
are shared types; `pixels::Plane` is a strided buffer with left/right
padding sized for the intra edge gather. They carry SVT-shaped data so
the layers above can hold it; they have no SVT counterpart.

## l2_gpurt - GPU runtime

- NVRTC JIT: `compileToPtx` compiles CUDA C++ source strings for
  `compute_61` at runtime - no offline nvcc kernel build. Kernel
  sources are chunked raw strings (MSVC C2026 fires on raw string
  literals above ~16.8 KB; all `CuSource` functions are built from
  <= ~13.5 KB chunks).
- Driver-API runtime: `GpuContext` (owns the `CUcontext`),
  `DeviceBuffer`, `Kernel` launch, `ptxEntryNames` maps source-declared
  kernels to PTX entry names.
- `ptxas -v` register/spill review is done manually during REFACTOR on
  kernels that land (AGENTS.md checklist).

## l3_transforms - transforms + quantizer

A1Q (2026-11-14) added the highbd quantizer family in transform_hbd.cpp: the bd-threaded buildQuantTables, quantizeBHb/quantizeFpHb, the 10/12-bit QTX rows and txScale (270 nb). The qm/iqm branches stay parked.

TXH1 (2026-11-13) added the highbd twin in `transform_hbd.{h,cpp}`: the bd-parameterized inverse core over uint16 r/w buffers, the bd stage-range ladder, the 16-type facades, the lossless iwht pair, the recon/dispatch chain and the fwd chain. 482 nb newly charged; the TX1 surface is not re-counted.

Fixed-point transforms at every size 4x4..64x64, verbatim SVT
arithmetic:

- 1D kernels: forward fdct/fadst and inverse idct/iadst at 4/8/16/32;
  64x64 is DCT-ONLY (no ADST is signalable at TX_64X64 in this tree -
  `av1_txfm_type_ls[4]` = DCT64/INVALID/INVALID/IDENTITY64). Inverse
  clamps only where SVT consumes stage_range (idct16 stages 3-7,
  iadst16 stages 3/5/7, idct32 stages 3-9, iadst32 every stage,
  idct64 clamped at 16-bit range).
- 2D wrappers `fwdTxfm2d*` / `invTxfm2dAdd*` carry SVT's per-size
  cos_bit/shift config: fwd shifts {2,0,0}/{2,-1,0}/{2,-2,0}/{2,-4,0}/
  {0,-2,-2}, inv {0,-4}/{-1,-4}/{-2,-4}/{-2,-4}/{-2,-4}; fwd cos_bit
  13/13, 13/13, 13/12, 12/12, 13/10 (64x64 col/row 10 is the only pass
  below 12), inv 12/12 everywhere.
- Quantizer: `buildQuantTables` (luma rows of `svt_av1_build_quantizer`,
  sharpness = 0), default scans 4x4..64x64, `quantizeFp*`/`quantizeB*`
  at log_scale 0/0/0/1/2 per `av1_get_tx_scale_tab`. dc/ac is unified
  via the dequant table index (`quant_ptr[rc != 0]`) - this SVT tree
  has no `av1_quantize_dc`.
- GPU twins at every size: `fwd_txfm_2d_*`, `inv_txfm_2d_add_*`,
  `quant_dequant_*` (quantize + dequant in one launch). Device 1D
  bodies carry the verbatim stage arithmetic; the launch geometry
  (one thread per column/row, shared-memory staging) is ours. Every
  twin is bit-exact vs its host port.
- Non-square shapes + the full TxType surface (TX1/TX2): the generic
  cfg machinery (shift/type/cos_bit tables + `two_d_core` + dispatch
  switches) over 14 shapes, 76 admissible (shape, TxType) pairs
  (roundtrip exactness classified per shape - 8x16 exact, 16x8
  lossy-1, the reference's own asymmetry), then the full 16-type list
  the lowbd tables admit: `setFlipCfg` verbatim, the vtx/htx tables,
  `fwdIdentity*`/`invIdentity*`, the fwd/inv core flip branches, IDTX
  at the 32-class shapes and the six one-axis types at TX_32X32;
  `supported()` enumerates 165 fwd / 243 inv pairs, and the ten square
  facades route through the same config+core path the reference's own
  facades use (the landed DCT/ADST outputs unchanged; the 64x64
  inverse keeps the 4096-wide facade the port's callers expand - the
  FS5d named follow-up). The highbd inv wrappers stay parked.

## l4_intra - intra prediction

`buildIntraPredictors` is 1:1 with SVT's `build_intra_predictors`,
size-generic over 4/8/16/32/64: DC availability variants and
missing-neighbor fills, directional prediction via the z1/z2/z3 zone
kernels + `drPredictor`, edge filter/upsample, smooth family, paeth,
filter-intra. The one extract substitution in the whole port is here:
`get_filt_type(xd, plane)` is a generator-controlled shim, mirrored in
the port by `NeighborContext`.

Regime facts (audited per size): the corner blend is live at
16x16/32x32/64x64 (txwpx+txhpx >= 24), dead at 4x4/8x8; edge upsample
never fires above 8x8 (blk_wh > 16); filter-intra is not signalable at
64x64 (bsize > 32), so the 64x64 GPU kernel has no FI path.

GPU twins `predict_block_4x4/8x8/16x16/32x32/64x64`: 256 threads at
4/8/16, 1024 threads (the CUDA block maximum) at 32x32/64x64 under
`__launch_bounds__(1024)`; 64x64 runs four pixels per thread
(p = t + 1024k). Edge buffers live in shared memory. GPU == host is
verified for all 8 dr modes x angle deltas at every size.

Chroma (CH-series): `UvPredictionMode` verbatim enum, the `uv2y` fold
(= `get_uv_mode`/`g_uv2y`; UV_CFL_PRED folds to DC_PRED - the
cfl_alpha AC-from-luma combine is out of scope) and
`buildIntraPredictorsUv` = fold + the size-generic builder with
FILTER_INTRA_MODES (chroma never uses filter-intra). The predictors
are plane-agnostic, so the wrapper IS the chroma dispatch - exactly
SVT's call-site fold.

HB1 added the hbd twins: `cflPredictHbd`, `upsampleIntraEdgeHigh`
and `hbdDrZ3` with the complete 54-config dy-surface enumeration -
derivation-relationship fixtures throughout (hbd == lbd widened at
bd 8).

## l5_motion - SAD

A3M (2026-11-13) added `motion_search.{h,cpp}` (1,361 nb): the av1me.c cost/range/site family, the mcomp.c cost/subpel family, the motion_estimation.c SAD aggregation quintet and the MeCtx/PcsCtx-carried driver helpers. The full search drivers and the rtcd vtable stay parked.

`sad4x4`..`sad64x64` over strided uint8: 8x8 mirrors SVT's dedicated
8x8 kernel, the rest mirror `svt_nxm_sad_kernel_helper_c` at their
dims. GPU kernels exist for 4x4/8x8; the 16x16/32x32/64x64 D2 policies
score host-side by design.

HM1 added the motion-hash + global-motion surface
(`hash_motion.{h,cpp}`: the crc32c pair, the hash-table machinery, the
block-hash generation and walk; `global_motion.{h,cpp}`: the GM param
surface including `refine_integerized_param` with the GmControls
fields threaded as scalars) - the first inter-prediction-adjacent
analysis primitives in the port; the hbd twins and the
correspondence/PCS walls stay parked.

SW1 landed the SAD wrapper surface: the `nxmSadKernelHelper` generic +
all 66 macro instances (`sadMxN`, `sadMxNx4D`, `OBMCSADMxN`) in the
reference's own dispatch shape - `compute_sad_c.c` and `sad_av1.c`
land 100% of their portable surface. The paired-metric caveat is
stated: the instances are transcription of the reference's own
dispatch surface (the algorithm cores were already landed).

AMVP1 landed the adaptive-MV-prediction candidate machinery
(`amvp.{h,cpp}`, namespace `motion::amvp`, 1,523 nb across 23 measured
spans): the block/row/col/blk scan family, `has_top_right`,
`get_relative_dist`, the TPL-ref and MVP-table builders
(`add_tpl_ref_mv`, `sort_mvp_table`, `scan_row_col_light`,
`setup_ref_mv_list`), `gm_get_motion_vector_enc`, the light-context
inter-mode derivation, `generate_av1_mvp_table`/`get_av1_mv_pred_drl`,
the sample recording/finding pair, the overlappable counters,
`is_dv_valid` and `ref_mv_from_stack`/`find_best`. The struct walls are
threaded as carriers (the RE8A precedent); the mi-grid/mi-map mutation
quartet, `init_wm_samples` and `warped_motion_parameters` stay parked
named. 91 cases / 783 assertions.

## l6_pipeline - composition + decision

A6P (2026-11-14) added pd_helpers.{h,cpp} (1,062 nb A6): the pd_process.c ref-management/skip-mode/sframe helpers, the pic_operators pack2d/driver remainder and the coding_loop.c CFL/recon-copy helpers. The CFL helpers themselves are intra_prediction.c surface (42 nb charged to A2).

A6C (2026-11-13) added `coding_loop_helpers.{h,cpp}` (2,420 nb): the neighbour-array update/copy/reset family, the stage/sort helpers, the NSQ/subsize/uv-txsize surfaces, the MV/ME helpers, the lambda chain and update_coeff_cdf. The context-generation wall stays parked.

Block and frame composition at 4x4..64x64, intra-only. The pipeline
raster order follows SVT's decode order: every block predicts from
RECONSTRUCTED neighbors (M1 availability), with the FR-series REAL
top-right gather (above[B..2B-1] come from the reconstructed row
above, not zeros).

- Block level: plane window (l1) + intra prediction (l4) -> int16
  residual (no clamp) -> fwdTxfm2d (l3); the Recon variants add the
  inverse round trip onto the same predictor.
- Frame level: the Recon variants run a raster grid; the Auto variants
  (`encodeFrameAuto*`) drive full frames with each block's mode chosen
  by the RD-lite policy (`rdliteDecide` wired into `decideBlockMode*`):
  all 13 intra candidates scored by variance<<4 distortion + the l16
  intraFastCost rate term through the RDCOST macro (strict < tie-break),
  with the angle delta enumerated over {-3..+3} for directional
  candidates at bsize >= 8X8 (RE6). The policy is OURS (SVT's full RD
  is not ported); the primitives inside it are 1:1 SVT. Chroma UV
  stays SAD-only (the ratified rung boundary). This is the first
  emission change since the RT-series: all nine committed artifacts
  regenerated and re-verified content-1:1 (the gated golden event).
- Quantized variants (`*Q`) wire the FP quantizer at a fixed qindex
  after the forward transform; the dequantized coefficients feed the
  inverse, so quantization loss feeds back through decisions. Scan
  order is the fixed default scan (SVT's get_scan_order per
  mode/tx-type selection is out of scope - named policy).
- Lossy regime: the 16x16 fwd/inv roundtrip is exact (recon == source);
  8x8/32x32/64x64 are lossy by design (fwd shifts sum to -2, 0 for 64).
- Chroma frame compositions run the same grid over the 4:2:0 UV plane:
  UV-sized blocks, per-plane availability, the `uv2y` fold at the call
  site, filt_type from the UV mode map; the chroma-emitting Q walk
  (CS4: `decideBlockModeUv`, uv_mode ONE per block, U-before-V
  per-component chains, the chroma-ownership rule) and the RT-series
  chroma-walk fixes (partition+skip CS5b, tree-order RT6, chroma FI
  RT7-b, the LUT-domain tx-type RT9, the bottom-left extension RT10b)
  - how it works end to end: `docs/color.md`.
- Entropy emission: the Auto/Q paths at ALL five geometries (4x4..64x64,
  FS3/FS4b) emit, per block, the kf y-mode + angle-delta + filter-intra
  symbols (BSF1; FI predicate-gated: DC_PRED, bsize <= 32x32) and - in
  the Q path - the real per-block token stream (TS3; every coded block
  writes the skip symbol with the VALUE skip = 0, which is a property of
  the data not of the emission, NA-driven contexts). THE PER-SIZE SHAPES
  DIFFER, and emission.md:27-31 is AUTHORITATIVE for them: 4x4Q emits
  skip but NO partition symbol (a 4x4 block is never a partition point);
  16x16Q emits NEITHER partition NOR skip (the ratified D3-era shape);
  8x8Q/32x32Q/64x64Q emit both as applicable. Multi-block grids emit
  running partition
  contexts (FS5a,
   `updatePartitionContext`, the fs5g32 grid gate). The chosen modes
   feed `NeighborContext` (filt_type live).
- GPU frame paths run the per-block kernel chain (`predict_block_*` +
  `subtract_*_plane` + `fwd_txfm_2d_*` (+ `quant_dequant_*`) +
  `inv_txfm_2d_add_*`), bit-exact vs host in lossless and q100 at all
  five geometries. Host decides, GPU executes: decisions and edge
  gathers stay on the host (edges are read from the device recon
  buffer), kernels execute the per-block work.
- PO1 added the struct-free pic_operators kernels (`picops.{h,cpp}`,
  namespace `pipeline::picops`): the two residual kernels (no clamp,
  the uint16 wrap-cast - the l6 convention), the three
  full-distortion kernels + the 32-single dispatcher (shift-free
  int64 SQR), the yv12 copy trio and `generatePadding16Bit`. The
  pack2d/unpack2d dispatches stay parked (their callees are PU1's
  packunpack in l9). CS1 landed the lbd hadamard family
  (`hadamard4x4..32x32`); HB1 added its hbd twin
  (`highbdHadamard8x8` with the two 26 nb first/second-pass column
  helpers).
- SRC1 added the source-ops surface (`src_ops.{h,cpp}`, namespace
  `pipeline::srcops`, 380 nb ported of `src_ops_process.c`'s 2,014):
  the TPL dispenser/synthesizer and the SSIM-rdmult driver -
  `getNeighborSamplesDc`, `roundFloor`, `getOverlapArea`,
  `deltaRateCost`, `perpixelVariance` (the AN8 offs-folded instances),
  `rateEstimator`, `tplModelUpdateB`/`tplModelUpdate` (the verbatim
  non-reset-i scan quirk fixture-pinned), `tplMcFlowSynthesizer`,
  `generateLambdaScalingFactor`, `generateR0beta` and
  `setMbSsimRdmultScaling`. The pcs/scs/me/tpl walls (the dispenser,
  `tpl_mc_flow`, the kernel pair) stay parked named (1,449 nb). The
  brief's plane copy/pad helpers do not exist in this file - the only
  pad content is one call of `svt_aom_generate_padding`.

## l7_entropy - entropy coder + symbols

A5H (2026-11-14) added header_symbols.{h,cpp} (1,831 nb): the entropy_coding.c frame-header/tile-info writer surface and the primitive bit writers, with the sequence header, color config, global motion and film grain writers. The coefficient and inter symbol surfaces were already landed (A5E/TS1/TS2).

A5E (2026-11-13) added `inter_symbols.{h,cpp}` (818 nb): the inter symbol + context surface of `entropy_coding.c` (comp/intra-inter/motion-mode/inter-mode/drl/mv/filter/ref-mode families and write_ref_frames) with the 19 inter CDF default extracts. The frame-header and tile writers stay parked.

The SVT/AOM entropy coder as a host port, integer only, bit-exact vs
the committed gate (see `docs/bitstream.md` for the narrative):

- od_ec range coder: encoder (equal-prob, binary, cdf-coded symbol
  primitives, flush/done, tell/tell_frac) and decoder (refill/
  normalize/decode, tell) - the decoder ports the vendored aom_dsp
  `entdec.c`. `od_ec_dec_bits_` is declared-but-undefined in the
  pinned tree and stays unported.
- `updateCdf` CDF adaptation + the write/read symbol wrappers
  (nsymbs == 2 bool routing, allow_update_cdf adaptation).
- Symbol surfaces: kf luma mode + angle delta + filter-intra
  flag/mode; partition (incl. the gathered 2-symbol XOR branches);
  skip (the first arithmetic-coded symbol of each I_SLICE block);
  tx-type (DCT_DCT through eset 2, reduced_tx_set intra); and the
  token chain - per-TU coefficients (txb_skip -> eob position ->
  base/br -> signs -> golomb) driven by the NA context model
  (`DcSignLevelCoeffNa`, packed dc_sign<<6|cul_level, OR-accumulate)
  and the per-position nz-map context LUT.
- `EcFrameContext` carries the default CDF tables (verbatim; token
  slices at q_ctx = 0); writer and reader adapt them identically
  (encoder/decoder mutual-consistency test).
- Reader halves of partition/skip/txb follow the aom decoder
  semantics (out-of-tree arbiter; no aom code is extracted).
- Named scope: LUMA DCT_DCT only, whole-block TUs (txb_count = 1 at
  tx_depth = 0). Landed (CS1/CS4): the chroma uv_mode/uv-delta symbols
  and the per-component token chains. Landed (SYM1): the palette +
  segmentation symbol surface - the six palette CDF families, the
  palette/segmentation writers and the aom-mirroring readers
  (`tokenizeColorMap`, `writeSegFeatures`/`readSegFeatures`,
  `negInterleave`/`negDeinterleave`); the uv palette continuation is
  parked (the pinned writer emits no uv palette) and the seg TEMPORAL
  path stays parked (the SVT writer errors on it upstream). Deferred:
  the CFL alphas (UV_CFL_PRED is not a D2 candidate), the nonkey
  y-mode path, intrabc.

## l8_bitstream - raw-bit writer + OBU

The container ground floor and the structural keyframe assembly (see
`docs/bitstream.md`): `AomWriteBitBuffer` bit/literal writers, uleb128,
OBU header + uleb payload size, temporal delimiter (exactly 2 bytes),
then `writeSequenceHeaderObu`, the frame-header walk (v1 lossless 22
bits + pad; v2 lossy 40 bits at base_q_idx = 100; the CS3 color v3 -
mono bit 0, the 4:2:0 config, 42 bits, the D1 un-patch with the mono
producers pinned for the mono set) and
`assembleStructuralKeyframeTU`/`...v2` packing TD + SPS + OBU_FRAME in
SVT's packer structure. The committed artifacts
`src/l8_bitstream/tests/goldens/structural_keyframe*.obu` - five
grayscale (d4 30B, d8 30B, d16 44B, d32 47B, d64 441B; the d32 v2
lossy TU is the structural-keyframe milestone artifact) + four color
(color4 183B, color8 137B, color16 133B, color32 165B) - prove composed
TU == committed file == gate bytes; the grayscale set is
decoder-accepted AND content-1:1 per geometry
(tools/verify_decode4.ps1 -Geometry N), the color set full-plane
content 1:1 4/4 (color16/color32 from CS5, color8 at the CS7 fix, the
color4 last-43-V-bytes gap at RT21) per the conformance matrix
(docs/decode_conformance.md).

## l9_analysis - content analysis (AN-series)

Host-only primitives 1:1 with SVT's analysis C
(`pic_analysis_process.c`, `noise_model.c`, `noise_util.c`,
`fft_common.h`/`fft.c`, `firstpass.c`, and - wave 3 -
`temporal_filtering.c`, `pack_unpack_c.c`, `blend_a64_mask_c.c`,
`variance.c`); no GPU twin (parked, named).
They feed future RDO decisions; nothing in the encoder consumes them
yet. The families:

- Mean/variance: `mean8x8`, `meanSquare8x8`, the sub-mean pair
  (`subMean8x8`, `subMeanSquareValues`), `intermVarFour8x8` (composes
  the pair - four side-by-side 8x8 blocks, not quadrants),
  `b64Variance` (the RDO-input capstone: the 8x8/16x16/32x32/64x64
  pyramid, every fold per array), `variance16x16`/`variance8x8`.
- Sampling/stats: `calculateHistogram` (the 256-bin pixel histogram),
  `downsample2d` (the 2x2 0-phase decimation, generic over
  decim_step), the pad family, the HME downsampling driver
  (`downsampleFilteringInputPicture`).
- Detectors: the screen-content pair (`isScreenContent`,
  `isScreenContentAaAware` + the count/palette/dominant/dilate
  helpers) and `isInputLumaDominant`.
- FFT + noise model (the first float surface): the verbatim
  `fft1d2..fft1d32` / `ifft1d2..ifft1d32` macro bodies, the 2D
  drivers (`fft2dN`/`ifft2dN`, N = 2..32), the noise-tx family
  (the spectral filter whose inverse carries the 1/n normalization),
  the OD_DIVU division table, the noise strength solver, the
  flat-block finder and the noise-model init/update.
- Temporal filtering (TF1): the planewise kernel family of
  `temporal_filtering.c` - the `log1p_tab_fp16` table (227 entries,
  mechanically extracted), `noise_log1p`, the decay/shift factors,
  the expf/sqrt tables, the squared-error sum and both planewise
  kernel pairs (the zero-zig and non-zz forms); the consumed
  MeContext fields threaded as parameters.
- Pack/unpack + blend + VAR (PU1): `pack_unpack_c.c` whole (all 7
  kernels), `blend_a64_mask_c.c` whole, and `varianceCore` + the 20
  missing VAR instances (the general two-buffer shape; the AN8
  offs-folded shape coexists - the offs array is REAL even at
  bStride 0, finding pinned).
- Subpel variance (SUB1): the bilinear 2-tap pair
  (`filterBlock2dBilFirstPass`/`SecondPass`) + table, the SUBPIX_VAR
  macro + 22 instances, and the obmc core + OBMC pairs 22/22/22 - the
  bilinear table is [8][2] with row 0 {128, 0}, rounding
  (sum + 64) >> 7, offsets 0..7 per axis (all pinned by measurement
  over the full 64-offset grid).
- The hbd twins (HB1): `varianceHighbd` + `highbdMse16x16` in
  packunpack - the 8-bit-discipline parks resolve for A14's
  completion, with derivation-relationship fixtures throughout (hbd ==
  lbd widened at bd 8).
- Activity masking + preprocessing (AM1+AN20): the nine vmaf
  calculators + box blur + unsharp + the preprocess driver (the
  pa_ctx/pcs walls threaded), the preprocessing statistics
  (histogram bins + spatial statistics + gathering), the
  temporal_filtering.c helpers (`estimate_noise_fp16`, `avg_mad`,
  gradient coherence, the unsharp+blur row kernels) and the firstpass
  pure accumulation pair - the INPUT side for the A4 RDO campaign.
  The `gate_start +1` off-by-one is recorded as UNOBSERVABLE under
  the committed float bands, not faked.

## l10_filters - post-filter kernels (DF/CDEF/LR/GR)

A8D (2026-11-14) added cdef_driver.{h,cpp} (1,583 nb): cdef_frame/pick_from_qp/propagate/finish, the cdef_process compute_dist/seg_search/copy_recon/superres path, the rest-kernel iter, the grain hbd twins and the restoration hbd extend/copy/selfguided branches.

A8R (2026-11-13) added `restoration_pick.{h,cpp}` (831 nb): the pixel-projection/subspace family, the sgr search (encode/apply/search/count), the wiener normal equations/linsolve/decompose/score/finalize/count and both hbd twins. The RestSearchCtxt/rsc drivers stay parked.

The deblocking, CDEF and restoration kernel families, 1:1 with the
pinned `deblocking_common.c`, `cdef.c`/`cdef_copy.h`/`enc_cdef.c` and
`restoration.c`, host only.
Coverage-only: nothing here is wired into an encode loop yet (the
filter enable bits in the bitstream stay literal 0), so these slices
move no conformance count - see the paired-metric caveat in
`AGENTS.md`.

- Deblocking (DF1/DF2): the mask/flat/hev gates, `filter4/6/8`, the
  `lpfHorizontal/Vertical 4/6/8` entry points, `updateSharpness`, and
  the 14-tap long-filter family (`filter14` + the mb-level edge
  walkers).
- CDEF (CDEF1-CDEF3): `cdefFindDir` (the direction-cost kernel, the
  div_table bucket weighting), the 16-bit and 8-bit pri/sec-tap
  filter kernels plus the frame-edge bounded variant, the fb/lbd
  driver chain (`cdefFilterFb`, `cdefFilterFbLbd*`) and the copy
  helpers, then the enc_cdef mse/search block (the four mse kernels,
  the dist bodies, `cdefApplyFb`'s halo/copy walk,
  `cdefSearchOneDual`/`cdefJointStrengthSearchDual`). The 16-bit
  kernel is the spec; the 8-bit twins are their own 1:1 bodies with
  the derivation relationship pinned by tests.
- Restoration (LR1/LR2/LR3): `sgrParams`, `extendFrame`/`copyTile` (the
  lowbd forms; highbd assert-parked), the `boxsum` family (edge
  handling is TRUNCATION - a 2D box sum clamped at the array bounds),
  `decodeXq` and the `xByXplus1`/`oneByX` tables, and the
  selfguided restoration internals (fast/slow + the apply
  composition). LR3 landed the stripe/unit family:
  `wholeFrameRect`, `countUnitsInTile`, `getStripeBoundaryInfo`,
  the stripe-boundary setup/restore pair, `wienerFilterStripe`,
  `sgrprojFilterStripe` and `loopRestorationFilterUnit` - the wiener
  stripe consumes `convolve::wienerConvolveAddSrc` (the l10 link line
  gained av1_convolve by judge court order), and the pinned findings
  are that `WienerInfo`'s `alignas(16)` is LOAD-BEARING (the convolve
  selects its tap row BY ADDRESS ALIGNMENT) and the wiener stripe's
  w round-up rule differs from sgrproj's plain min. The highbd
  stripes and the tile walkers stay parked.
- Film grain (GR1): the synthesis core of `grainSynthesis.c`
  (998 nb of 1,140): `gaussian_sequence` (2,048 entries transcribed,
  the caveat stated, diff-verified), the rng pair, `init_arrays`, the
  luma/chroma grain generators, scaling, `add_noise_to_block`,
  `params_equal`, the copy helpers + boundary overlaps and the frame
  driver; the hbd path and dealloc are parked.
- A8W1 added the hbd deblocking family (`deblock_hbd.{h,cpp}`, 347 nb:
  the bd-parameterized mask/flat/hev gates, `highbdFilter4/6/8/14`,
  the `lpfHorizontal/Vertical` 4/6/8/14 entries and the mb-level edge
  walkers) and the DLF-search walls (`deblock_driver.{h,cpp}`, 423 nb:
  `disableDlfTh`, `svtCopyBuffer`, `pictureSseCalculations`,
  `tryFilterFrame`, `searchFilterLevel`, `meBasedDlfSkip`,
  `svtAv1PickFilterLevel`), with the walk's hbd dispatch resolving the
  RE8A assert-parks. 315 cases / 35,207 assertions; fourteen
  judge-adjudicated staged-fixture errata; the `svt_aom_ac_quant_qtx`
  call site, the `dlf_process` SSE persistence, the recon-pic selector
  and the temp-recon allocation stay parked named.

## l11_resize - resize 1D kernels (AN21)

The resize family of `resize.c` (`resize.c:147-489`, 328 nb): the
1D resampling kernels generic over the filter phase plus the four
interpolation tables (`resize.c:36-137`) and the 1000-set from
`super_res.c` (charged to A8). RS2 landed the hbd half
(`resize.c:489-779`, 273 nb: the highbd interpolate cores, the symmetric
even/odd downsamplers, the multistep resampler, the plane drivers) -
the only bd dependence is `clip_pixel_highbd` (measured; the hbd path
does NOT scale filter coefficients by bd), with the hbd@bd8 == 8-bit-twin
sweep over the enumerated length set. `save_YUV`, the highbd
pack/unpack helpers and the `EbPictureBufferDesc` resize driver stay
parked. Host only.

## l12_convolve - convolution kernels (CV1)

The `convolve.c` kernel family (163 nb): the scalar dot products +
filter offset/base handling, the add-src horizontal/vertical half
interpolation paths, the wiener add-src convolution (the uint16
staging contract: temp stride 128, h+6 rows, sentinel row),
`convolveHoriz`/`convolveVert` + the convolve8 pair, `ConvolveParams`
+ `getConvParamsWiener` + the `sub_pel_filters_8` table (128 entries,
diff-verified). LR3's wiener stripe depends on it. The highbd half is
parked.

IP1 added the interp-filter machinery: the `InterpFilter` surface
(`extractInterpFilter`, `getConvolveFilterParams`,
`getInterpFilterParamsWithBlockSize`, `makeInterpFilters` + the five
filter tables, mechanically transcribed + diff-verified) and
`upsampledPred` (the C_DEFAULT body's four paths; the brief had
misplaced it in inter_prediction.c - grep 0 hits there).

## l13_palette - palette + k-means + segmentation (PAL1/SEG1)

- Palette (PAL1): 389 nb of `palette.c` plus the `count_colors` body
  (its definition lives in `pic_analysis_process.c` - the one
  cross-file correction pinned), and `k_means_template.h` bodies at
  both dims (k = 2..8 enumerated; dim-2 k-means has no portable-C
  caller in the reference - ported and tested anyway).
- Segmentation (SEG1): the struct-free surface
  (`getVarianceForCu`, `calculateSegmentationData`,
  `findSegmentQps` + the params tables), with the reference's quirks
  preserved 1:1 and fixture-pinned: the ROUND identity (step_size can
  only ever be 1), the POW2 narrowing wraps, the descending bin-edge
  pairing, the transposed index pairings, and `log2f_safe` =
  `get_msb(x|1)`. The roi_map/PCS walls and the (declared-but-never-
  defined) `temporally_update_qps` stay parked.

## l14_utility - block geometry + utilities (U1 + A13U)

The `utility.c` surface (347 nb) + the `common_utils.c` dependencies
(31 nb): the block geometry builder `svt_aom_build_blk_geom`, the
md-scan walk, the coded-unit + mini-GOP stats arrays, the ns/d1-depth
offset tables and `hvsizeToBsize`. The full builder enumeration over
all 11 geometries and the GEOM_2 85-block cross-check against
`coded_unit_stats_array` (two independent transcriptions agreeing
through the algorithm) pin the tables. Host only.

## l15_rc - rate-control tables + arithmetic (RC1/RC2)

A9D (2026-11-14) added 1,691 nb: the rc_process reset/update/init/superres/sb-qindex surface, the pass2 GF allocation/interval/lap/kf/postencode families and the initial_rc tpl-group/LAD surface.

A9R (2026-11-13) added 470 nb: the rc_process.c rd-multiplier/lambda family, the initial_rc_process.c tpl-level pair + set_1pvbr_param, and the pass2_strategy.c two-pass stats/correction/GF arithmetic. The queue/PCS/SCS drivers and the TPL-lambda wall stay parked.

`rc_tables.h` in `src/l15_rc`: the 18 qindex-mapping tables x 256
entries (278 nb), mechanically extracted from the pinned reference
with a runtime diff-verify test that parses the reference file itself.
The paired-metric caveat is stated: this block moves the coverage
number by transcription, not by algorithm (the AN14 OD_DIVU precedent).
Host only.

RC2 added the rate-control arithmetic and driver families
(`rc_arith.{h,cpp}`, 2,447 nb across waves A-D): the active-quality and
one-pass target/buffer helpers, the D-series qindex/qdelta arithmetic,
the AQ series, the one-pass allocation/dynamic-resize family, the RTC
primitives and the RTC-CBR driver family (including the Kalman rcf
state), the recode helpers and `recodeLoopUpdateQ`, the VBR updater +
postencode pair + regulated pair, and the cyclic-refresh trio. The
8-bit dc/ac qlookup rows are mechanically extracted into the registry;
the 10/12-bit rows are the `CONFIG_ENABLE_HIGH_BIT_DEPTH=0` -1 branch,
pinned. Integer outputs are exact and doubles are compared at
`fabs 1e-9`; the oracles parse the reference tables and are the
authority (five hand-trace errors were caught). The TPL-lambda wall,
the mutex ops, the debug prints and the pick-q/rate-allocation shell
stay parked named.

## l16_rdo - rate estimation + trellis + symbol costs (RE-series)

A4M (2026-11-14) added md_slices2 (1,087 nb): the mode_decision.c remainder. A4C (2026-11-14) added md_core (2,125 nb): the product_coding_loop.c fast-loop/stage/search/recon-chain remainder and the md_process.c tail.

EMC2 (2026-11-13) completed the `enc_mode_config.c` conveyor in `mode_config.{h,cpp}` (4,174 nb): the remaining NIC/NSQ/intra/pd0/pd1/tx/cfl/rate-est/mds0/depth-refinement setters, the max-block-size and enc-dec derivation families, and the light-pd1/default/rtc/allintra drivers. The pic-level config coordinators stay parked.

The A4 golden-regeneration campaign, in `src/l16_rdo` (namespace
`l16_rdo`; the scaffold links av1_transforms). RE1-RE4 are the
machinery rungs (CONFORMANCE-FREE: no emission change, no golden
churn); RE5-RE7 are the WIRING rungs - the first emission changes
since the RT-series, each a gated golden event that regenerated all
nine committed artifacts and re-verified them content-1:1 in the real
decoders. EMC1 and MD1 are the later coverage-only surfaces (the
mode-config conveyor and the pure-arithmetic mode-decision helpers).

- RE1, the rate-estimation core of `md_rate_estimation.c` (485 nb
  judge-charged): the cdf->cost core, the estimate_syntax_rate INTRA
  surface, the mv class + nmv cost tables, the coefficient-rate
  estimator, the interintra-allowed helpers and the header tables
  (av1_prob_cost, ext-tx). The cfl sign-cost quirk transcribed
  verbatim (the oracle caught the first draft's sign_fac_bits on both
  planes). Parks named: update_stats (BlkStruct-walled),
  update_part_stats (pcs-walled), the inter block.
- RE2, the optimize_b trellis of `full_loop.c` (562 nb + verified
  carries; 1,222 nb total charged across A1/A4/A5/A13): the cost
  helpers, the three trellis updaters + update_skip +
  update_coeff_eob_fast, `svt_fast_optimize_b`, and the driver with
  the pcs/ctx walls threaded as OptimizeBArgs; the GENERATED scan
  orders (via svt_aom_init_iscan), the tx-size tables, and the
  court-ordered nz_map_ctx_offset.inc byte-copy runtime twin-checked
  against both the l7 copy and the reference. The RD-monotonicity
  property over the full grid; the fast path's zbin-trim boundary
  fixture sharpened by the judge addendum.
- RE3, the `rd_cost.c` symbol-cost surface (576 nb judge-charged,
  three merge-time double-counts struck): transform-type rate
  estimation, update_eob_context (the cdf state-comparing grid), the
  cost_coeffs_txb driver chain and the uv/intra fast costs. Three
  judge probes all killed.
- RE4, ac_bias + the intra candidate-injection family (462 nb
  judge-verified): the psy-distortion family (the brief's "ac_bias
  tables" do not exist - corrected), the candidate-set CONSTRUCTION
  over the enumerated intra modes + the angle-delta counter walk
  (the search's definition), `is_valid_mv_diff` and the classified
  support carries. THE ERRATA (judge-adjudicated):
  FILTER_INTRA_MODES 7 -> 5 with the definitions.h mis-citation
  fixed, and fimodeToIntradir's 5th entry DC_PRED -> PAETH_PRED - a
  latent semantic bug (an FI PAETH candidate folded to DC), latent
  safe because the FI costs are not wired.
- RE5, the RD-lite D2 wiring rung (the first gated golden event):
  `rdliteLambda` (the policy lambda, q == 0 ? 0 : q),
  `initRdliteFrameCtx` (the l7 EcFrameContext -> l16
  MdRateEstimationContext bridge), the kf-y ctx bridge, and
  `rdliteDecide` (variance<<4 distortion + the landed intraFastCost +
  the RDCOST macro, strict < tie-break) wired into
  `decideBlockMode4x4..64x64` + the generator's `svtd_rdlite_decide`
  twin; chroma UV stays SAD-only (the ratified rung boundary). The
  new mode map EXPOSED two latent FI emission bugs the SAD policy
  masked: d4's FI symbol wrote through a per-site local cdf
  re-initialized from default while the decoder reads the ADAPTING
  frame_context->filter_intra_cdfs[bsize] (all six l6 sites + the
  drives now write through the adapting cdf), and d32's
  svtd_bsf3_tile_data_v2 had NO FI write at all (latent under the
  SAD map's no-DC_PRED block set; RD-lite made blocks 2-3 DC_PRED) -
  both fixed before the regen. Artifact matrix (old -> new, ALL decode
  content-1:1): d4 30->31, d8 30->30 (content changed), d16 44->43,
  d32 47->49, d64 441->440, color4 183->199, color8 137->147,
  color16 133->136, color32 165->165; GATE 884,851 -> 884,651 B
  (-207, 96 lines). The paired conformance count re-verified
  9 artifacts x decoders - it moves WITH the campaign as designed.
- RE6, the angle-delta wiring rung (the second gated golden event):
  the emission's delta=0 park (pipeline.h:156) RESOLVES -
  `rdliteDecide` enumerates angleDelta over {-3..+3} for directional
  candidates (bsize >= 8X8; at 4x4 no variants), the
  angular-pred-level policy = 1 (the full 7-delta set; ours, named),
  ModeDecision gains the angleDelta field; 11 predictor sites predict
  at the CHOSEN delta and 7 writeKfLumaMode sites emit it; UV delta
  stays 0 (the chroma rung boundary); the mirror's
  `svtd_rdlite_decide` gains the same enumeration (17 call sites).
  New gate fixtures: the bsf1q_* set (the Q emission's chosen delta
  with QUANTIZED-recon feedback) + bsf3v2_deltas. Artifact matrix
  (old -> new): d4/d8/d16/d64 byte-identical, d32 49->50,
  color4 199->209, color8 147->146, color16 136 (bytes moved); GATE
  884,651 -> 884,897 B (+246, 427 lines). 6 mutations: 4 killed + M3
  and M5 SURVIVED their first forms and were SHARPENED (the
  distortion-dominance blind spot -> the direct rate-consumption pin;
  the no-delta-3-fixture blind spot -> the written-symbol off-by-one
  kill); the judge probe (the emission delta-symbol arithmetic +1
  off) KILLED by the l7 gate-byte fixture. The paired conformance
  count re-verified 9/9.
- RE7, the tx-type rung (the third gated golden event): the derived
  intra UV tx-type (`intraUvTxType`, the l3 `transform.h:97` twin of
  `svt_aom_get_intra_uv_tx_type`) lands in the emission; the nine
  artifacts regenerate (d4 33 / d8 30 / d16 43 / d32 66 / d64 440 /
  color4 214 / color8 161 / color16 136 / color32 165 B, all
  content-1:1) and the GATE moves to 885,347 B / 427 lines.
- EMC1, the mode-config conveyor (4,050 nb of `enc_mode_config.c`,
  struct-free via mirror carriers): slice 1 + batch A (qp-th scaling,
  pf/angular, the wn/sg/dlf level families, GM correspondence and
  `setGmControls`, HME/ME search params, the prune ctrl setters, the
  sig-deriv pair), batch B (the CDEF search/recon setters, the
  dlf/intrabc/palette setters, `sigDerivMultiProcesses`, inter-comp
  ctrls, level getters + enable wrappers, OBMC controls) and batch C
  (the MD search, subpel/rdoq/shaving, txt/interpolation-search and
  cand-reduction setters). The compile-time forks
  (`CONFIG_ENABLE_GLOBAL_MOTION`/`_LOSSLESS`/`_INTER_INTRA`) thread as
  runtime parameters; the poison-and-oracle whole-struct discipline
  pins each setter. The l16 target closes at 185 cases / 4,707,133
  assertions.
- MD1, the mode-decision pure-arithmetic surface (572 nb): the
  similarity + cc1/cc2 family (`enc_dec_process.c:638-671`), the SSIM
  family (`mode_decision.c:4238-4490`, incl. the hbd twins and the
  block walkers), the hbd hadamard trio
  (`picture_operators_c.c:176-262`), the hbd psy
  (`ac_bias.c:103-144`, landed but unwired) and the MV/ME helpers
  (`is_lossless_segment`, `check_mv_validity`, the me-block-offset
  tables, `have_newmv_in_inter_mode`, `get_max_drl_index`,
  `mv_is_already_injected`, the uni/bipred validity pairs). 23
  TEST_CASEs; coverage-only.

## l17_inter - inter prediction (MC1 + A3W + A3E)

MC1 (2026-11-12) landed the inter-prediction compound/dist-weighted/scale
surface and the sr-convolve family in `inter_prediction.{h,cpp}` (524 nb).
A3W (2026-11-13) added the warped-motion + RANSAC surface in
`warped_motion.{h,cpp}` + `ransac.{h,cpp}` (1,019 nb): the div lut, the
projection/affine/shear fit, the 8-bit warp, the shear params, the sample
selection, and the RANSAC scoring/fitting/internal walk. The hbd warp twins
and the dead `USE_LIMITED_PREC_MULT` branch stay parked. Host only.

## l18_picmgmt - picture management + threading (A10P)
A10P (2026-11-14) created the first picture-management layer (913 nb, ctest target l18_picmgmt): pic_buffer_desc layout calc/assign + ctor/update, reference_object ring/param updates, sequence_control_set resolution/copy, svt_threads synchronization primitives, and the pcs.c geometry/alloc-size helpers. Host only; the allocation/ctor/dctor walls are parked named.

## The frame encode flow (encodeFrameAuto16x16, step by step)

1. Raster-iterate the 16x16 grid. For each block:
2. `decideBlockMode16x16` scores all 13 intra candidates by RD-lite
   (variance<<4 distortion + the l16 intraFastCost rate term, RDCOST,
   strict < tie-break) against the reconstructed edges, with the
   angle delta enumerated over {-3..+3} for directional candidates
   (RE5/RE6); lowest cost wins.
3. The chosen mode + angle delta + neighbor modes fill
   `NeighborContext` (filt_type for the edge filter); the predictor
   builds at the chosen delta.
4. `buildIntraPredictors` (l4) builds the predictor from the
   reconstructed edges (real top-right gather).
5. Residual = source - predictor as int16 (no clamp).
6. `fwdTxfm2d16x16` (l3) transforms; the Q paths then quantize at the
   fixed qindex and dequantize.
7. `invTxfm2dAdd16x16` adds the reconstruction onto the predictor;
   the recon block is written back (later blocks predict from it).
8. Emission: the kf y-mode symbol (context pair from the decided
   neighbor modes), the angle-delta symbol carrying the CHOSEN delta
   when directional (RE6 - the delta=0 park resolved), the
   filter-intra flag where allowed (BSF1); Q paths append the token
   chain for the block's coefficients (TS3); partition + skip symbols
   where the tree codes them (ECP1/ECP2), with running partition
   contexts across blocks (FS5a).

The same flow runs at all five geometries (the Auto/Q loops at
4x4..64x64 emit; FS3/FS4b) - sizes differ in the transform/quant
wrappers, the token-table row (txs_ctx), the eob alphabet, and the
policy entry points; 64x64 is DCT-only (no tx-type symbol, no FI -
the token domain is the adjusted 32x32, 1024 positions). A 4x4 frame
is an 8x8 node in every decoder (8px alignment): the 4x4 TUs exist
only as partition leaves (FS5c). Each geometry has a committed
content-1:1 artifact (d4 33 B / d8 30 B / d16 43 B / d32 66 B / d64
440 B, the RE7 tx-type regen sizes; tools/verify_decode4.ps1
-Geometry N).

## The golden gate (tools/golden_gen)

Every expected value comes from a committed generator, never from
hand-traces:

1. `extract.ps1` pattern-locates each function/table in the vendored
   SVT tree and copies it VERBATIM (byte-for-byte body, provenance
   comment above) into `svt_gen.c` (generated, committed so the gate
   is reproducible without re-running extraction). One substitution is
   flagged inline: `get_filt_type(xd, plane)` -> `svtd_filt_type`.
2. `shims.h` is plumbing only (typedefs, RTCD name resolutions); no
   arithmetic.
3. `composition.c` wires the extracts into the dispatch tables and the
   frame/policy/chroma/bitstream/token drivers.
4. `golden_primitives.exe` dumps the primitive vectors; its output is
   diffed against `committed expected_primitives.txt` (currently 427
   lines, 885,347 B - 0 diff is a commit precondition). Round-trips and
   inequalities exit nonzero inside the generator itself.
5. `golden_frame.exe` captures the D-policy frame golden.

## Test suite map

Eighteen doctest targets (l0_core, l1_pixels, l2_gpurt, l3_transforms,
l4_intra, l5_motion, l7_entropy, l6_pipeline, l8_bitstream,
l9_analysis, l10_filters, l11_resize, l12_convolve, l13_palette,
l14_utility, l15_rc, l16_rdo, l17_inter), each with
a "test runner boots" guard plus real test files. GPU-required tests
skip with a `SKIP:`-prefixed message when no CUDA device is present.
`ctest --test-dir build` runs all eighteen.

## Where the deep detail lives

- `AGENTS.md` - the layer map with the standing rules and citations.
- `docs/provenance.md` - the function-by-function provenance table.
- `tools/golden_gen/README.md` - the extraction inventory, gate-line
  documentation, EC3 scope statement, REFERENCE PINNING note.
- `docs/bitstream.md`, `docs/emission.md`, `docs/color.md`,
  `docs/decode_conformance.md` - the bitstream path, the per-geometry
  emission walks, the chroma/color pipeline, and decoder-acceptance
  status.
