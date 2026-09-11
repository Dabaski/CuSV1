# AV1 CUDA 12.2 Port (Pascal-targeted)

GPU-accelerated port of [SVT-AV1](https://gitlab.com/AOMediaCodec/SVT-AV1)
encoder building blocks to CUDA 12.2, targeting NVIDIA Pascal
(`sm_61` / GP104-class) hardware. The host reference paths are taken 1:1
from SVT-AV1's C implementation and re-expressed as CUDA C++ kernels
JIT-compiled at runtime via NVRTC.

> **Disclaimer:** This project is actively being worked on. Code is
> incomplete, tests may be red, and APIs will change. Kernels are
> functionally correct (bit-exact vs SVT's host C where noted); a
> benchmark harness exists (`tools/bench/`) but the composite encoder
> loop is per-block synchronous launches — see `AGENTS.md` for the
> dated baseline and the known-overhead caveat. Expect rough edges. Not
> ready for production use.

## What's implemented

Each layer is developed under incremental TDD (see `AGENTS.md`); GPU
twins are held to bit-exact agreement with the SVT host reference.

- **l0_core** — minimal shared types: `Sample` (uint8), `BlockSize`.
- **l1_pixels** — `pixels::Plane`: strided pixel buffer with left/right
  padding.
- **l2_gpurt** — NVRTC JIT + CUDA driver-API runtime: `GpuContext`,
  `DeviceBuffer`, `Kernel`, `compileToPtx`, `ptxEntryNames`; kernel
  sources are CUDA C++ strings compiled for `compute_61`.
- **l3_transforms** — SVT-AV1 fixed-point transforms, 4x4 / 8x8 /
  16x16 / 32x32 / 64x64: `fdct4`/`fadst4`, `fdct8`/`fadst8`,
  `fdct16`/`fadst16`, `fdct32`/`fadst32` and `fdct64`/`idct64`
  forward/inverse with `cospi`/`sinpi` tables, `halfBtf`, `roundShift`
  (fdct16/fadst16 are cos_bit-parameterized — 16x16 is the one
  geometry where col 13 / row 12 differ; fdct64B is
  cos_bit-parameterized — 64x64 col 13 / row 10 is the only pass below
  12; 64x64 is DCT-ONLY, no ADST exists in this tree:
  `av1_txfm_type_ls[4]` = DCT64/INVALID/INVALID/IDENTITY64);
  2D forward `fwdTxfm2d4x4`/`fwdTxfm2d8x8`/`fwdTxfm2d16x16`/
  `fwdTxfm2d32x32`/`fwdTxfm2d64x64` and inverse
  `invTxfm2dAdd4x4`/`invTxfm2dAdd8x8`/`invTxfm2dAdd16x16`/
  `invTxfm2dAdd32x32`/`invTxfm2dAdd64x64` (fwd shifts
  {2,0,0}/{2,-1,0}/{2,-2,0}/{2,-4,0}/{0,-2,-2}, inv
  {0,-4}/{-1,-4}/{-2,-4}/{-2,-4}/{-2,-4}, 8-bit clip add; the inverse
  clamps only where SVT consumes stage_range — idct16 stages 3-7,
  iadst16 stages 3/5/7, idct32 stages 3-9, iadst32 every stage,
  idct64 clamped at 16-bit range, no all-zero early-out at 16 or 32);
  every size has bit-exact GPU twins (`fwd_txfm_2d_*` /
  `inv_txfm_2d_add_*`). Integer only. Quantizer stage:
  `buildQuantTables` (luma rows of `svt_av1_build_quantizer`,
  sharpness=0), `defaultScan4x4`/`defaultScan8x8`/`defaultScan16x16`/
  `defaultScan32x32`/`defaultScan64x64` (iscan formula),
  `quantizeFp4x4`/`quantizeB4x4` + `quantizeFp8x8`/`quantizeB8x8` +
  `quantizeFp16x16`/`quantizeB16x16` + `quantizeFp32x32`/`quantizeB32x32`
  + `quantizeFp64x64`/`quantizeB64x64` (verbatim
  `quantize_fp_helper_c` / `svt_aom_quantize_b_c` semantics, log_scale
  0 at 4x4/8x8/16x16, 1 at 32x32, 2 at 64x64; dc/ac unified via
  dequant table index — this SVT tree has no `av1_quantize_dc`); GPU
  twins `quant_dequant_4x4` / `quant_dequant_8x8` /
  `quant_dequant_16x16` / `quant_dequant_32x32` /
  `quant_dequant_64x64` (quantize + dequant in one launch, bit-exact
  vs the host fp path, TxType-agnostic).
- **l4_intra** — `buildIntraPredictors` (1:1 with SVT, luma,
  size-generic over 4/8/16/32/64, DC availability variants,
  missing-neighbor fills), `dr_z1`/`z2`/`z3` + `drPredictor`, edge
  filter / upsample (with `disable_edge_filter` config and `filt_type`
  neighbor plumbing), filter-intra, smoothPredict family; GPU twins
  `predict_block_4x4`, `predict_block_8x8`, `predict_block_16x16`,
  `predict_block_32x32` (1024 threads — the CUDA block maximum —
  one thread per pixel under `__launch_bounds__(1024)`) and
  `predict_block_64x64` (1024 threads, four pixels per thread
  p = t + 1024k; filter-intra is not signalable at 64x64 so the 64
  kernel has no FI path; the corner blend is live at 16x16/32x32/64x64
  but dead at 4x4/8x8, and edge upsample never fires above 8x8); GPU
  == host verified for all 8 dr modes x angle deltas at every size.
- **l5_motion** — `motion::sad4x4` / `sad8x8` / `sad16x16` /
  `sad32x32` / `sad64x64` (strided uint8; sad8x8 mirrors SVT's
  dedicated `compute8x8_sad_kernel_c`, the rest mirror
  `svt_nxm_sad_kernel_helper_c` at their dims) + GPU kernels for
  4x4/8x8; the 16x16/32x32/64x64 D2 policies score host-side.
- **l6_pipeline** — block and frame composition and mode decision at
  4x4 / 8x8 / 16x16 / 32x32 / 64x64.
  Block level: `encodeBlock4x4` = plane window (l1) +
  `buildIntraPredictors` (l4) -> int16 residual (no clamp) ->
  `fwdTxfm2d4x4` (l3); `encodeRecon4x4` adds the inverse round trip.
  Frame level: `encodeFrameRecon4x4`/`encodeFrameRecon8x8`/
  `encodeFrameRecon16x16`/`encodeFrameRecon32x32`/
  `encodeFrameRecon64x64` run a raster grid with recon-only
  neighbors; `frameMse8` is the integer frame SSE; the D2 policy
  (`decideBlockMode4x4`/`8x8`/`16x16`/`32x32`/`64x64`) scores all 13
  PredictionModes by SAD (project-defined policy, SVT primitives);
  primitives); the Auto variants (`encodeFrameAuto4x4`/
  `encodeFrameAuto8x8`/`encodeFrameAuto16x16`/`encodeFrameAuto32x32`/
  `encodeFrameAuto64x64`) drive full frames —
  each block's mode chosen against RECONSTRUCTED edges, with the
  FR-series REAL top-right gather (above[B..2B-1] come from the
  reconstructed row above, not zeros), the chosen modes feeding
  `NeighborContext` (filt_type live). Quantized variants
  (`encodeFrameAuto4x4Q`, `encodeFrameRecon8x8Q`/`encodeFrameAuto8x8Q`,
  `encodeFrameRecon16x16Q`/`encodeFrameAuto16x16Q`) wire the FP
  quantizer at a fixed qindex after the forward transform (qcoeff =
  coded coefficients, dequantized coefficients feed the inverse —
  quantization loss feeds back through decisions). The 16x16 fwd/inv
  roundtrip is exact (recon == source); 8x8 is lossy by design.
  GPU path `predict_block_*` + `subtract_*_plane` +
  `fwd_txfm_2d_*` (+ `quant_dequant_*` in the quantized loops) +
  `inv_txfm_2d_add_*` at every size, bit-exact vs host; the GPU
  frame-auto loops reproduce the host `encodeFrameAuto*` paths
  exactly, lossless and quantized (host decides, GPU executes).
  Goldens are generated by `tools/golden_gen/` (committed) from the
  vendored SVT tree.

## Repository layout

```
src/
  l0_core/        core types, test harness
  l1_pixels/      strided pixel buffers
  l2_gpurt/       NVRTC JIT + driver-API runtime
  l3_transforms/  fixed-point forward/inverse transforms, 4x4+8x8+16x16
                  +32x32, quantizer (host + GPU)
  l4_intra/       intra prediction, 4x4+8x8+16x16+32x32 (host + GPU)
  l5_motion/      SAD / motion (host; GPU kernels for 4x4/8x8)
  l6_pipeline/    block + frame composition, 4x4+8x8+16x16 (host + GPU)
tools/
  golden_gen/     SVT golden-vector generator (mechanical verbatim
                  extraction from third_party/SVT-AV1, committed)
  bench/          av1_bench: console benchmark tool (own target, not a
                  ctest; env + host/GPU frame + per-stage timings;
                  run the exe with the CUDA toolkit bin on PATH)
third_party/
  SVT-AV1/        vendored source of truth (do not modify)
  doctest/        test framework
  hardware_docs/  perf-axis reference only (PTX ISA, GP104 whitepaper,
                  Pascal Tuning Guide, nvprof-era Profiler Users Guides
                  — concepts-only on CUDA 12.2, whose profiler is ncu)
docs/             design/research notes (e.g. native FFmpeg CUDA
                  AV1 encoder integration)
CMakeLists.txt    top-level build
AGENTS.md         TDD methodology + CUDA-specific GREEN rules
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

## Benchmarks

`tools/bench/` builds the `av1_bench` console tool: it records the GPU
name/driver/clocks at run time (nvidia-smi query), then times host and
GPU composite auto-frame encodes (lossless + q100, 4x4, 8x8, 16x16,
32x32 and 64x64 geometry — 64-frame tiled from the B7 fixture for
4/8/16, 128-frame for 32, 256-frame for 64) plus per-stage
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
