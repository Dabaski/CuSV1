# The layer stack — how everything works

Orientation doc for the port. Symbol-by-symbol provenance lives in
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
                         |
             tools/golden_gen (the gate)
             tools/bench (perf)
             tools/decode_handoff.ps1 + td0_ladder.ps1 (decoders)
```

Host reference paths are taken 1:1 from the pinned vendored SVT-AV1
snapshot and re-expressed as CUDA C++ kernels JIT-compiled at runtime
via NVRTC. Every layer is developed under incremental TDD; GPU twins
are held to bit-exact agreement with the SVT host C.

## l0_core / l1_pixels — infrastructure

`Sample` (uint8) and `BlockSize` (the project-minimal square variant)
are shared types; `pixels::Plane` is a strided buffer with left/right
padding sized for the intra edge gather. They carry SVT-shaped data so
the layers above can hold it; they have no SVT counterpart.

## l2_gpurt — GPU runtime

- NVRTC JIT: `compileToPtx` compiles CUDA C++ source strings for
  `compute_61` at runtime — no offline nvcc kernel build. Kernel
  sources are chunked raw strings (MSVC C2026 fires on raw string
  literals above ~16.8 KB; all `CuSource` functions are built from
  <= ~13.5 KB chunks).
- Driver-API runtime: `GpuContext` (owns the `CUcontext`),
  `DeviceBuffer`, `Kernel` launch, `ptxEntryNames` maps source-declared
  kernels to PTX entry names.
- `ptxas -v` register/spill review is done manually during REFACTOR on
  kernels that land (AGENTS.md checklist).

## l3_transforms — transforms + quantizer

Fixed-point transforms at every size 4x4..64x64, verbatim SVT
arithmetic:

- 1D kernels: forward fdct/fadst and inverse idct/iadst at 4/8/16/32;
  64x64 is DCT-ONLY (no ADST is signalable at TX_64X64 in this tree —
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
  via the dequant table index (`quant_ptr[rc != 0]`) — this SVT tree
  has no `av1_quantize_dc`.
- GPU twins at every size: `fwd_txfm_2d_*`, `inv_txfm_2d_add_*`,
  `quant_dequant_*` (quantize + dequant in one launch). Device 1D
  bodies carry the verbatim stage arithmetic; the launch geometry
  (one thread per column/row, shared-memory staging) is ours. Every
  twin is bit-exact vs its host port.

## l4_intra — intra prediction

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
(= `get_uv_mode`/`g_uv2y`; UV_CFL_PRED folds to DC_PRED — the
cfl_alpha AC-from-luma combine is out of scope) and
`buildIntraPredictorsUv` = fold + the size-generic builder with
FILTER_INTRA_MODES (chroma never uses filter-intra). The predictors
are plane-agnostic, so the wrapper IS the chroma dispatch — exactly
SVT's call-site fold.

## l5_motion — SAD

`sad4x4`..`sad64x64` over strided uint8: 8x8 mirrors SVT's dedicated
8x8 kernel, the rest mirror `svt_nxm_sad_kernel_helper_c` at their
dims. GPU kernels exist for 4x4/8x8; the 16x16/32x32/64x64 D2 policies
score host-side by design.

## l6_pipeline — composition + decision

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
  by the D2 policy (`decideBlockMode*`): all 13 intra modes scored by
  SAD against reconstructed edges, lowest wins, tie = lowest mode
  index. This policy is OURS (SVT selects modes via RD/trellis
  machinery we do not port); the primitives inside it are 1:1 SVT.
- Quantized variants (`*Q`) wire the FP quantizer at a fixed qindex
  after the forward transform; the dequantized coefficients feed the
  inverse, so quantization loss feeds back through decisions. Scan
  order is the fixed default scan (SVT's get_scan_order per
  mode/tx-type selection is out of scope — named policy).
- Lossy regime: the 16x16 fwd/inv roundtrip is exact (recon == source);
  8x8/32x32/64x64 are lossy by design (fwd shifts sum to -2, 0 for 64).
- Chroma frame compositions run the same grid over the 4:2:0 UV plane:
  UV-sized blocks, per-plane availability, the `uv2y` fold at the call
  site, filt_type from the UV mode map.
 - Entropy emission: the Auto/Q paths at ALL five geometries (4x4..64x64,
   FS3/FS4b) emit, per block, the partition symbol where the tree codes
   one, skip, the kf y-mode + angle-delta + filter-intra symbols (BSF1;
   FI predicate-gated: DC_PRED, bsize <= 32x32) and — in the Q path —
   the real per-block token stream (TS3, skip = 0, NA-driven contexts).
   Multi-block grids emit running partition contexts (FS5a,
   `updatePartitionContext`, the fs5g32 grid gate). The chosen modes
   feed `NeighborContext` (filt_type live).
- GPU frame paths run the per-block kernel chain (`predict_block_*` +
  `subtract_*_plane` + `fwd_txfm_2d_*` (+ `quant_dequant_*`) +
  `inv_txfm_2d_add_*`), bit-exact vs host in lossless and q100 at all
  five geometries. Host decides, GPU executes: decisions and edge
  gathers stay on the host (edges are read from the device recon
  buffer), kernels execute the per-block work.

## l7_entropy — entropy coder + symbols

The SVT/AOM entropy coder as a host port, integer only, bit-exact vs
the committed gate (see `docs/bitstream.md` for the narrative):

- od_ec range coder: encoder (equal-prob, binary, cdf-coded symbol
  primitives, flush/done, tell/tell_frac) and decoder (refill/
  normalize/decode, tell) — the decoder ports the vendored aom_dsp
  `entdec.c`. `od_ec_dec_bits_` is declared-but-undefined in the
  pinned tree and stays unported.
- `updateCdf` CDF adaptation + the write/read symbol wrappers
  (nsymbs == 2 bool routing, allow_update_cdf adaptation).
- Symbol surfaces: kf luma mode + angle delta + filter-intra
  flag/mode; partition (incl. the gathered 2-symbol XOR branches);
  skip (the first arithmetic-coded symbol of each I_SLICE block);
  tx-type (DCT_DCT through eset 2, reduced_tx_set intra); and the
  token chain — per-TU coefficients (txb_skip -> eob position ->
  base/br -> signs -> golomb) driven by the NA context model
  (`DcSignLevelCoeffNa`, packed dc_sign<<6|cul_level, OR-accumulate)
  and the per-position nz-map context LUT.
- `EcFrameContext` carries the default CDF tables (verbatim; token
  slices at q_ctx = 0); writer and reader adapt them identically
  (encoder/decoder mutual-consistency test).
- Reader halves of partition/skip/txb follow the aom decoder
  semantics (out-of-tree arbiter; no aom code is extracted).
- Named scope: LUMA DCT_DCT only, whole-block TUs (txb_count = 1 at
  tx_depth = 0). Deferred: chroma uv_mode/CFL symbols, the nonkey
  y-mode path, palette, intrabc.

## l8_bitstream — raw-bit writer + OBU

The container ground floor and the structural keyframe assembly (see
`docs/bitstream.md`): `AomWriteBitBuffer` bit/literal writers, uleb128,
OBU header + uleb payload size, temporal delimiter (exactly 2 bytes),
then `writeSequenceHeaderObu`, the frame-header walk (v1 lossless 22
bits + pad; v2 lossy 40 bits at base_q_idx = 100) and
`assembleStructuralKeyframeTU`/`...v2` packing TD + SPS + OBU_FRAME in
SVT's packer structure with the court-ratified D1 monochrome patch.
The committed artifacts
`src/l8_bitstream/tests/goldens/structural_keyframe*.obu` — five files
(d4 30B, d8 30B, d16 44B, d32 47B, d64 441B; the d32 v2 lossy TU is the
structural-keyframe milestone artifact) — prove composed TU == committed
file == gate bytes and are decoder-accepted AND content-1:1 per geometry
(tools/verify_decode4.ps1 -Geometry N).

## The frame encode flow (encodeFrameAuto16x16, step by step)

1. Raster-iterate the 16x16 grid. For each block:
2. `decideBlockMode16x16` scores all 13 intra modes by SAD of the
   predictor vs the reconstructed edges (above row + left column,
   already reconstructed by raster order); lowest SAD wins.
3. The chosen mode + neighbor modes fill `NeighborContext`
   (filt_type for the edge filter).
4. `buildIntraPredictors` (l4) builds the predictor from the
   reconstructed edges (real top-right gather).
5. Residual = source - predictor as int16 (no clamp).
6. `fwdTxfm2d16x16` (l3) transforms; the Q paths then quantize at the
   fixed qindex and dequantize.
7. `invTxfm2dAdd16x16` adds the reconstruction onto the predictor;
   the recon block is written back (later blocks predict from it).
8. Emission: the kf y-mode symbol (context pair from the decided
   neighbor modes), the angle-delta symbol when directional, the
   filter-intra flag where allowed (BSF1); Q paths append the token
   chain for the block's coefficients (TS3); partition + skip symbols
   where the tree codes them (ECP1/ECP2), with running partition
   contexts across blocks (FS5a).

The same flow runs at all five geometries (the Auto/Q loops at
4x4..64x64 emit; FS3/FS4b) — sizes differ in the transform/quant
wrappers, the token-table row (txs_ctx), the eob alphabet, and the
policy entry points; 64x64 is DCT-only (no tx-type symbol, no FI —
the token domain is the adjusted 32x32, 1024 positions). A 4x4 frame
is an 8x8 node in every decoder (8px alignment): the 4x4 TUs exist
only as partition leaves (FS5c). Each geometry has a committed
content-1:1 artifact (d4 30B / d8 30B / d16 44B / d32 47B / d64 441B;
tools/verify_decode4.ps1 -Geometry N).

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
   diffed against `committed expected_primitives.txt` (currently 335
   lines — 0 diff is a commit precondition). Round-trips and
   inequalities exit nonzero inside the generator itself.
5. `golden_frame.exe` captures the D-policy frame golden.

## Test suite map

Nine doctest targets (l0_core, l1_pixels, l2_gpurt, l3_transforms,
l4_intra, l5_motion, l7_entropy, l6_pipeline, l8_bitstream), each with
a "test runner boots" guard plus real test files. GPU-required tests
skip with a `SKIP:`-prefixed message when no CUDA device is present.
`ctest --test-dir build` runs all nine.

## Where the deep detail lives

- `AGENTS.md` — the layer map with the standing rules and citations.
- `docs/provenance.md` — the function-by-function provenance table.
- `tools/golden_gen/README.md` — the extraction inventory, gate-line
  documentation, EC3 scope statement, REFERENCE PINNING note.
- `docs/bitstream.md`, `docs/emission.md`,
  `docs/decode_conformance.md` — the bitstream path, the per-geometry
  emission walks, and decoder-acceptance status.
