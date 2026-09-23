# AV1 CUDA 12.2 Port (Pascal-targeted)

GPU-accelerated port of [SVT-AV1](https://gitlab.com/AOMediaCodec/SVT-AV1)
encoder building blocks to CUDA 12.2, targeting NVIDIA Pascal
(`sm_61` / GP104-class) hardware. The host reference paths are taken 1:1
from SVT-AV1's C implementation and re-expressed as CUDA C++ kernels
JIT-compiled at runtime via NVRTC.

> **Disclaimer:** This project is actively being worked on. APIs will
> change and not every SVT-AV1 feature is ported yet. Kernels are held
> to bit-exact agreement with the vendored SVT host C (enforced by the
> committed golden gate); the composite encoder loop is per-block
> synchronous launches — see `AGENTS.md` for the dated benchmark
> baseline and the known-overhead caveat. Not ready for production use.

## Status

- **First decoder-accepted AV1 bitstream** (BS-series closed): the
  pipeline emits a structural keyframe temporal unit that a reference
  decoder accepts and decodes — libdav1d exits silently and ffprobe
  reports `codec_name=av1`, 32x32, `pix_fmt=gray` (monochrome, D1
  confirmed by an independent decoder) and `color_range=pc` (the
  ratified `color_range=1`).
- **Coefficient/token coding closed** (TS-series): the lossy v2
  temporal unit (47 bytes, real token streams, `base_q_idx` = 100)
  parses under the same decoders — an ffmpeg probe reports
  `av1 (libdav1d) (Main)`, gray(pc), 32x32 — and was the
  conformance gate until the FS-series extended it to all five
  geometries.
- **Decoder conformance closed, content 1:1** (TD-series): the
  q100 token streams now decode **and reconstruct bit-identically**.
  The root cause was the coefficient-CDF qindex bucket: the spec's
  `init_coeff_cdfs` mandates selecting the token tables by
  `base_q_idx` (07.bitstream.semantics.md:1800-1820; the verbatim
  `get_q_ctx`, cabac_context_model.c:1907-1918); the writer used the
  idx-0 bucket for every frame. With the bucketed init the 7-rung
  bisect ladder is 7/7 byte-exact in the real libdav1d, and all 1024
  decoded pixels of the committed 47-byte artifact equal the
  generator's recon (the content fingerprint: the first pixels
  `05 08 0c 11`). The full resolution record:
  `docs/decode_conformance.md`.
- **Per-geometry emission closed, content 1:1 at all five sizes**
  (FS-series): every geometry (4x4, 8x8, 16x16, 32x32, 64x64) now
  emits real symbol streams (partition/skip/kf-mode/filter-intra/
  token chains) and carries a committed v3 artifact that a reference
  decoder decodes **bit-identically** — measured: d4 30B→64B,
  d8 30B→64B, d16 44B→256B, d32 47B→1024B, d64 441B→4096B decoded
  bytes all equal the generator's recon lines
  (`tools/verify_decode4.ps1 -Geometry <4|8|16|32|64>`). The 4x4
  artifact codes an 8x8 frame as one 4-symbol partition symbol
  (forced SPLIT) plus four 4x4 partition leaves: the decoders align
  frame dims to 8 px, so a 4x4 frame is an 8x8 node and a 4x4 TU
  exists only as a leaf (dav1d-trace-verified). Multi-block grids
  emit running partition contexts (`updatePartitionContext`); the
  grid conformance gate is the fs5g32 fixture. Scope: single-TU and
  the grid fixtures are gate-pinned; the filter-intra DC-deciding
  fixture and the 64x64 dual-domain GPU unification remain named
  follow-ups.
- All layers green: 9 doctest targets pass, and the golden gate
  reproduces the committed `expected_primitives.txt` 335/335 lines.

## What's implemented

Each layer is developed under incremental TDD (see `AGENTS.md`); GPU
twins are held to bit-exact agreement with the SVT host reference.
Symbol-by-symbol provenance lives in `docs/provenance.md` and
`tools/golden_gen/README.md`; the gate-line inventory in the latter.
How the stack works end to end: `docs/layers.md`; the bitstream path:
`docs/bitstream.md`; decoder-acceptance status:
`docs/decode_conformance.md`.

- **l0_core** — minimal shared types: `Sample` (uint8), `BlockSize`.
- **l1_pixels** — `pixels::Plane`: strided pixel buffer with left/right
  padding.
- **l2_gpurt** — NVRTC JIT + CUDA driver-API runtime: `GpuContext`,
  `DeviceBuffer`, `Kernel`, `compileToPtx`, `ptxEntryNames`; kernel
  sources are CUDA C++ strings compiled for `compute_61`.
- **l3_transforms** — SVT-AV1 fixed-point transforms at every size
  4x4..64x64, host plus bit-exact GPU twins: the verbatim 1D kernels
  (forward fdct/fadst, inverse idct/iadst; 64x64 is DCT-only — no ADST
  is signalable at TX_64X64 in this tree), the 2D wrappers
  (`fwdTxfm2d*`/`invTxfm2dAdd*`) carrying SVT's per-size cos_bit/shift
  configuration (16x16 is the one geometry with differing fwd col/row
  cos_bit 13/12; 64x64 col/row 13/10 is the only pass below 12), and
  the quantizer stage: `buildQuantTables`, default scans,
  `quantizeFp*`/`quantizeB*` at log_scale 0/0/0/1/2 (dc/ac unified via
  the dequant table index — this SVT tree has no `av1_quantize_dc`),
  with GPU quantize+dequant twins (`quant_dequant_*`).
- **l4_intra** — intra prediction 1:1 with SVT's
  `build_intra_predictors`, size-generic over 4/8/16/32/64 (DC
  availability variants, missing-neighbor fills): directional
  prediction (z1/z2/z3 + drPredictor), edge filter/upsample, smooth
  family, filter-intra, paeth. GPU twins `predict_block_*` at every
  size — 32x32/64x64 run 1024 threads (the CUDA block maximum; 64x64
  does four pixels per thread and has no filter-intra path, which is
  not signalable at that size), the corner blend is live at 16x16 and
  above, edge upsample never fires above 8x8, and GPU == host is
  verified for all 8 dr modes x angle deltas at every size. Chroma:
  `UvPredictionMode` (UV_CFL_PRED folds to DC_PRED — the cfl_alpha
  AC-from-luma combine is out of scope), the `uv2y` fold and
  `buildIntraPredictorsUv`; the predictors are plane-agnostic and
  chroma never uses filter-intra.
- **l5_motion** — SAD at 4x4/8x8/16x16/32x32/64x64 over strided uint8
  (8x8 mirrors SVT's dedicated 8x8 kernel, the rest mirror
  `svt_nxm_sad_kernel_helper_c`); GPU kernels for 4x4/8x8, the larger
  dims score host-side.
- **l6_pipeline** — block and frame composition and mode decision at
  4x4..64x64. `encodeBlock4x4` composes plane window (l1) + intra
  prediction (l4) + residual + forward transform (l3); `encodeRecon4x4`
  adds the inverse round trip. Frame level: the Recon variants run a
  raster grid with recon-only neighbors; the Auto variants drive full
  frames with each block's mode chosen by the D2 policy
  (`decideBlockMode*` — all 13 intra modes scored by SAD against
  reconstructed edges; project-defined policy on SVT primitives).
  Quantized variants (`*Q`) wire the FP quantizer at a fixed qindex so
  quantization loss feeds back through decisions. The 16x16 fwd/inv
  roundtrip is exact; 8x8/32x32/64x64 are lossy by design (64x64
  DCT-only within our TxType scope). Chroma frame compositions run the
  same grid over the 4:2:0 UV plane (UV-sized blocks, per-plane
  availability, the `uv2y` fold at the call site). Entropy emission
  runs per geometry: the 16x16 Auto/Q paths emit the kf y-mode +
  angle delta + filter-intra flag (BSF1) and the real per-block
  token stream (TS3, skip = 0); the 4x4Q/8x8Q/32x32Q/64x64Q loops
  emit the full per-block walk [partition iff the frame reads one]
  [skip][kf mode + angle delta][filter-intra iff the predicate]
  [token chain] with the qindex-bucketed frame context (FS3/FS4) —
  bit-exact vs the generator's per-size gate lines; multi-block
  grids emit running partition contexts (`updatePartitionContext`,
  the fs5g32 grid gate). Scope: the single-TU and grid fixtures are
  gate-pinned; the filter-intra DC-deciding fixture and the 64x64
  dual-domain GPU unification (a 1024-position GPU kernel + bench
  work) are named follow-ups. GPU frame paths run the per-block
  kernel chain (predict, subtract, forward, quantize where
  configured, inverse-add), bit-exact vs host in lossless and q100
  at all five geometries (host decides, GPU executes).
- **l7_entropy** — the entropy coder, host port, integer only,
  bit-exact vs the committed gate. The od_ec range encoder and decoder
  (equal-prob, binary and cdf-coded symbol primitives, byte flush,
  tell/tell_frac; the decoder side ports the vendored aom_dsp
  `entdec.c` — `od_ec_dec_bits_` is declared-but-undefined in the
  pinned tree and stays unported), CDF adaptation (`updateCdf`) and
  the write/read symbol wrappers. Symbol level: kf luma mode + angle
  delta + filter-intra flag/mode, partition, skip, and the token
  chain — per-TU coefficients (txb_skip -> eob position -> base/br ->
  signs -> golomb) driven by the NA context model and the
  per-position nz-map context LUT, plus the tx-type symbol through
  `intra_ext_tx_cdf`. Named scope: LUMA DCT_DCT only, token CDF
  slices at q_ctx = 0, whole-block TUs; writer and reader adapt CDFs
  identically (encoder/decoder mutual-consistency test). Deferred:
  chroma uv_mode/CFL symbols, the nonkey y-mode path, palette,
  intrabc.
- **l8_bitstream** — raw-bit writer + OBU container ground floor
  (bit/literal/inv-signed-literal writers, uleb128, OBU header +
  uleb payload size, temporal delimiter) and structural keyframe
  assembly: sequence-header OBU payload (the max frame dims
  parameterized; `frame_width_bits` = msb(dims)), the uncompressed
  frame-header walk and the full temporal unit packer (TD + SPS +
  OBU_FRAME in SVT's packer structure, with the court-ratified D1
  monochrome patch — decoder-confirmed gray). The lossless TU
  carries 22 header bits + 2 pad; the lossy v2 (TS4) carries
  `base_q_idx` = 100 and a 40-bit header with real token streams.
  The committed artifact set
  `src/l8_bitstream/tests/goldens/structural_keyframe*.obu` — the
  47-byte d32 (the four-16x16-leaves walk, TD5b) plus the
  per-geometry FS5 set (d4 30B, d8 30B, d16 44B, d64 441B) — each
  proves the three-way identity composed TU == committed file ==
  gate bytes and is decoder-accepted with content 1:1 (the d32 =
  all 1024 pixels == ecfrm_recon; the d4 = the 8x8-frame structure
  stated above). The BSF4-fix ec coupling is stated as an invariant
  at both l6 emission sites: allow_update_cdf = 1 only because the
  ratified config carries disable_cdf_update = 0.

## Repository layout

```
src/
  l0_core/        core types, test harness
  l1_pixels/      strided pixel buffers
  l2_gpurt/       NVRTC JIT + driver-API runtime
  l3_transforms/  fixed-point forward/inverse transforms, 4x4..64x64
                  (DCT-only at 64x64), quantizer (host + GPU)
  l4_intra/       intra prediction, 4x4..64x64 (host + GPU, chroma
                  via the uv2y fold)
  l5_motion/      SAD / motion (host; GPU kernels for 4x4/8x8)
  l6_pipeline/    block + frame composition, 4x4..64x64 (host + GPU)
  l7_entropy/     entropy coding: od_ec range coder, CDF adaptation,
                  partition/skip/kf-mode/filter-intra + token/txb +
                  tx-type symbols (host)
l8_bitstream/   raw-bit writer + OBU ground floor, structural keyframe
                TU assembly (+ the committed artifact set:
                structural_keyframe*.obu — d4/d8/d16/d32/d64)
tools/
  golden_gen/     SVT golden-vector generator (mechanical verbatim
                  extraction from third_party/SVT-AV1, committed)
  bench/          av1_bench: console benchmark tool (own target, not a
                  ctest; env + host/GPU frame + per-stage timings;
                  run the exe with the CUDA toolkit bin on PATH)
  decode_handoff.ps1             decoder acceptance runner
                                 (dav1d/aomdec/ffmpeg) + self-test
td0_ladder.ps1                 decoder-conformance bisect ladder
                               (TD-series closed; see
docs/decode_conformance.md)
verify_decode4.ps1             the per-geometry decode instrument:
                               artifact decode + content-1:1 vs the
                               gate's recon line (exit 0 = pass;
                               -Geometry 4|8|16|32|64, default 32)
third_party/
  SVT-AV1/        vendored source of truth (do not modify; pinned
                  snapshot — see Third-party notices)
  doctest/        test framework
  hardware_docs/  perf-axis reference only (PTX ISA, GP104 whitepaper,
                  Pascal Tuning Guide, nvprof-era Profiler Users Guides
                  — concepts-only on CUDA 12.2, whose profiler is ncu)
docs/             design/research notes (e.g. native FFmpeg CUDA
                  AV1 encoder integration)
CMakeLists.txt    top-level build
AGENTS.md         TDD methodology + CUDA-specific GREEN rules + standing
                  rules (bench-before/after, citation verification,
                  REFERENCE PINNING); the detailed layer map
```

## Target hardware

- NVIDIA Pascal, `sm_61` (GP104-class, e.g. GTX 1080).
- CUDA Toolkit 12.2.
- Kernels compiled at runtime via NVRTC (no offline `nvcc` kernel
  build); `ptxas -v` register/spill review is done manually during
  REFACTOR on kernels that land.
- Primary dev platform: Windows + MSVC.

## Building

Prerequisites: CUDA Toolkit 12.2, CMake 3.22+, MSVC (VS 2022).

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Run a single layer's tests:

```
ctest --test-dir build -R l4_intra
```

## Testing

- Test framework: [doctest](https://github.com/doctest/doctest), vendored
  under `third_party/doctest/`.
- Each layer has a `tests/test_harness.cpp` ("test runner boots") guard
  plus real test files (`test_core.cpp`, `test_pixels.cpp`, ...).
- GPU-required tests follow the `SKIP:` convention from `AGENTS.md`:
  when no CUDA device / required compute capability is present, tests
  return early with a `SKIP:`-prefixed message rather than failing.
- Goldens are produced by `tools/golden_gen/` — a committed generator
  that mechanically extracts functions verbatim from the vendored
  SVT-AV1 tree (see `tools/golden_gen/README.md`); its validation gate
  diffs generator output against committed tests.
- Decode handoff: `tools/decode_handoff.ps1` runs dav1d / aomdec /
  ffmpeg against the committed `.obu` artifact (dated report written
  to `decode_handoff_results.txt` at the repo root, kept untracked).
  `tools/test_decode_handoff.ps1` is the self-test (fake decoder,
  report-content check).

## Benchmarks

`tools/bench/` builds the `av1_bench` console tool: it records the GPU
name/driver/clocks at run time (nvidia-smi query), then times host and
GPU composite auto-frame encodes (lossless + q100, 4x4, 8x8, 16x16,
32x32 and 64x64 geometry — 64x64 frame tiled from the B7 fixture for
4x4/8x8/16x16, 128x128 for 32x32, 256x256 for 64x64) plus per-stage
single-launch kernel timings. Every GPU configuration runs an untimed
bit-exact verification pass against the host output first and refuses
to report timings on mismatch. The composite loop is host decision +
per-block synchronous launches — the tool's header names this known
overhead. Dated baseline numbers and the bench-before/after rule for
perf-relevant slices live in `AGENTS.md`.

## Methodology

Development follows Uncle Bob's Three Laws of TDD as a nano-cycle
(one assertion per RED/GREEN loop), with CUDA-specific extensions:
correctness-green != performance-acceptable, explicit FP tolerances
decided up front, `SKIP:` for missing GPUs, and PTX-level sanity
checks during REFACTOR. See `AGENTS.md` for the full rules.

## Third-party notices

- **SVT-AV1** — vendored under `third_party/SVT-AV1/` as the 1:1 source
  of truth. See its top-level `LICENSE` and `NOTICE` for terms. This
  project is a derivative port and is not affiliated with or endorsed
  by the SVT-AV1 authors or the Alliance for Open Media.
  **Reference pinning:** the vendored tree is a 4.2-era snapshot
  (CHANGELOG 4.2.0, 2026-07-14), NOT byte-identical to the official
  v4.2.0 tag, and upstream master has diverged further. The snapshot is
  pinned and is not updated — the target is bit-exact 1:1 with this
  pinned vendored reference, and updating it would invalidate every
  committed golden and provenance citation.
- **doctest** — vendored under `third_party/doctest/`, MIT License.
- **hardware_docs** — vendored NVIDIA reference material (PTX ISA,
  GP104 whitepaper, Pascal Tuning Guide, nvprof-era Profiler Users
  Guides), perf-axis only; our profile on CUDA 12.2 is `ncu` — the
  guides are used for concepts only.
