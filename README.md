# AV1 CUDA 12.2 Port (Pascal-targeted)

GPU-accelerated port of [SVT-AV1](https://gitlab.com/AOMediaCodec/SVT-AV1)
encoder building blocks to CUDA 12.2, targeting NVIDIA Pascal
(`sm_61` / GP104-class) hardware. The host reference paths are taken 1:1
from SVT-AV1's C implementation and re-expressed as CUDA C++ kernels
JIT-compiled at runtime via NVRTC.

> **Disclaimer:** This project is actively being worked on. Code is
> incomplete, tests may be red, and APIs will change. Kernels are
> functionally correct (bit-exact vs SVT's host C where noted) but
> **not** performance-validated — there is no benchmark harness yet.
> Expect rough edges. Not ready for production use.

## What's implemented

Each layer is developed under incremental TDD (see `AGENTS.md`); GPU
twins are held to bit-exact agreement with the SVT host reference.

- **l0_core** — minimal shared types: `Sample` (uint8), `BlockSize`.
- **l1_pixels** — `pixels::Plane`: strided pixel buffer with left/right
  padding.
- **l2_gpurt** — NVRTC JIT + CUDA driver-API runtime: `GpuContext`,
  `DeviceBuffer`, `Kernel`, `compileToPtx`, `ptxEntryNames`; kernel
  sources are CUDA C++ strings compiled for `compute_61`.
- **l3_transforms** — SVT-AV1 fixed-point forward transforms:
  `fdct4`, `fadst4`, `fwdTxfm2d4x4(TxType)` (4x4, cos_bit=13); plus
  `cospi`/`sinpi` tables, `halfBtf`, `roundShift`; bit-exact GPU twin.
  Integer only.
- **l4_intra** — `buildIntraPredictors` (1:1 with SVT, luma, DC
  availability variants, missing-neighbor fills), `dr_z1`/`z2`/`z3` +
  `drPredictor`, edge filter / upsample, smoothPredict family; GPU
  twin `predict_block_4x4`. Largest test surface (~75 doctest cases).
- **l5_motion** — `motion::sad8x8` (strided uint8, bit-exact with
  SVT's `compute8x8_sad_kernel_c`) + GPU kernel.
- **l6_pipeline** — 4x4 block composition:
  `pipeline::encodeBlock4x4` = plane window (l1) + `buildIntraPredictors`
  (l4) -> int16 residual (no clamp) -> `fwdTxfm2d4x4` (l3). GPU path
  `predict_block_4x4` + `subtract_4x4_plane` + `fwd_txfm_2d_4x4`,
  bit-exact vs host. Goldens captured from SVT's own C in a throwaway
  harness under `%TEMP%\svt_ref\` (never committed).

## Repository layout

```
src/
  l0_core/        core types, test harness
  l1_pixels/      strided pixel buffers
  l2_gpurt/       NVRTC JIT + driver-API runtime
  l3_transforms/  fixed-point forward transforms (host + GPU)
  l4_intra/       intra prediction (host + GPU)
  l5_motion/      SAD / motion (host + GPU)
  l6_pipeline/    4x4 block composition (host + GPU)
third_party/
  SVT-AV1/        vendored source of truth (do not modify)
  doctest/        test framework
  hardware_docs/  perf-axis reference only (PTX ISA, GP104 whitepaper,
                  Pascal Tuning Guide, Nsight/ncu guides)
CMakeLists.txt    top-level build
AGENTS.md         TDD methodology + CUDA-specific GREEN rules
.github/FUNDING.yml
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
- Goldens are captured from SVT-AV1's own C reference, never committed.

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
- **doctest** — vendored under `third_party/doctest/`, MIT License.
- **hardware_docs** — vendored NVIDIA reference material (PTX ISA,
  GP104 whitepaper, Pascal Tuning Guide, Nsight/`ncu` guides), used
  for perf-axis guidance only.
