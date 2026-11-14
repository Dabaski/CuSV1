# SVT-AV1 tests and this repo's tests - inventory and data-reuse guide

Status: inventory measured 2026-11-14 against the pinned snapshot in
`third_party/SVT-AV1/` (upstream `fd116158a00986d923db8a1fdddd0dbd5fd0e240`).
No SVT fixture has been imported yet. This document is the preparation for
that pass: it explains what SVT's tests are, what data they actually use,
where that data comes from, and how to turn it into fixtures this repo can
run against the CUDA port.

The short version: most SVT unit tests do not ship fixed test data. They
generate inputs at run time and compare a SIMD kernel against the C kernel
in the same process. "Grab all the test data" therefore means one of three
things, depending on the family:

1. copy the small number of embedded or downloaded data sets (film grain
   expected arrays, E2E video vectors),
2. dump input/output pairs from SVT's C reference path with a local
   instrumentation harness, or
3. skip the tests whose subject is not ported (API shell, SIMD dispatch,
   allocator, timing-only performance tests).

Section 9 turns this into a concrete extraction plan.

## 1. Where SVT's tests live

All paths below are relative to `third_party/SVT-AV1/`.

| Location | Contents |
| --- | --- |
| `test/` | 68 top-level unit-test `.cc` files plus shared harness headers, `TestEnv.c`, `unit_test_utility.c`, `random.h`, `acm_random.h`, `Readme.md` |
| `test/ref/` | `TxfmRef.cc`, `TxfmRef.h` - floating-point transform reference |
| `test/api_test/` | 6 `.cc` files exercising the public encoder API, `params.h` with the parameter value matrix |
| `test/e2e_test/` | 12 `.cc` files plus video sources, reference decoder wrapper, and test-vector download CMake |
| `test/benchmarking/` | Python encode-quality tooling, not part of CTest |

Measured counts (non-blank lines, trimmed lines):

| Part | Files | Non-blank lines | Test macros |
| --- | --- | --- | --- |
| Unit tests, top-level `.cc` | 68 | 28,801 | 313 `TEST`/`TEST_F`/`TEST_P` (49 `DISABLED_`) |
| API tests | 6 | 1,794 | 29 |
| E2E tests | 12 | 3,892 | 15 (5 `DISABLED_`) |

The unit test binary's source lists are split into architecture-neutral,
multi-architecture, x86-only, and ARM-only groups in
`test/CMakeLists.txt:61-134`, `test/CMakeLists.txt:137-149` and
`test/CMakeLists.txt:152-153`. The x86-only group includes the transform,
FFT, motion-estimation, PSNR, and high-bit-depth intra tests.

## 2. Build and run

SVT's tests are gated by `BUILD_TESTING`:

- `third_party/SVT-AV1/CMakeLists.txt:254-255` declares the option.
- `third_party/SVT-AV1/CMakeLists.txt:770-774` adds `test/` and the vendored
  googletest when it is on.
- `third_party/SVT-AV1/CMakeLists.txt:261` defines
  `SVT_AV1_UNIT_TEST_BUILD=1`, which keeps the C reference functions linked.
  The unit-test main rejects a deployment library that lacks them
  (`test/svt_av1_test.cc:20-27`, `test/svt_av1_test.cc:64-73`).

Three executables are built and all three are registered with CTest:

| Binary | Registration |
| --- | --- |
| `SvtAv1UnitTests` | `test/CMakeLists.txt:213` |
| `SvtAv1ApiTests` | `test/api_test/CMakeLists.txt:58` |
| `SvtAv1E2ETests` | `test/e2e_test/CMakeLists.txt:79` |

The upstream `test/Readme.md:24-43` and `test/Readme.md:46-67` give the
historical build command and the gtest runner usage:
`--gtest_list_tests`, `--gtest_filter`, and `SVT_AV1_TEST_VECTOR_PATH` for
pre-downloaded vectors.

A portable command from this repo's root, not using the stale Visual Studio
project notes:

```powershell
cmake -S third_party/SVT-AV1 -B build_svt -DBUILD_TESTING=ON
cmake --build build_svt --config Release
ctest --test-dir build_svt -C Release
```

Useful single-suite forms:

```powershell
ctest --test-dir build_svt -C Release -R SvtAv1UnitTests
SvtAv1UnitTests.exe --gtest_list_tests
SvtAv1UnitTests.exe --gtest_filter="*transform*"
SvtAv1E2ETests.exe --gtest_filter="*QpTest*"
```

The E2E binary needs two extra things:

- Downloaded video vectors. `test/e2e_test/CMakeLists.txt:88-102` creates a
  `TestVectors` target that fetches the files listed in
  `test/e2e_test/test_vector_list.txt`. The downloader uses
  `https://storage.googleapis.com/aom-test-data` and the SHA-1 checksums in
  that file (`test/e2e_test/test_data_download_worker.cmake:24`). The storage
  location is `SVT_AV1_TEST_VECTOR_PATH` when set, otherwise
  `test/vectors/` (`test/e2e_test/test_data_download_worker.cmake:26-38`).
- A libaom reference decoder built from
  `https://aomedia.googlesource.com/aom` at tag `av1-normative` with
  `CONFIG_INSPECTION=1` (`test/e2e_test/libaom_dependency.cmake:22-27`).
  The E2E binary links it (`test/e2e_test/CMakeLists.txt:51`).

## 3. What counts as test data in SVT

Everything in the unit suite falls into one of five buckets.

### 3.1 Procedurally generated inputs

59 of the 68 top-level unit `.cc` files include one of three generators:

- `test/random.h` - `SVTRandom`, used by 50 files. It wraps `std::mt19937`
  with seed 13596 (`test/random.h:142`) and `std::uniform_int_distribution`.
- `test/acm_random.h` - `ACMRandom`, used by 6 files (`av1_convolve_scale_test`,
  `BlockErrorTest`, `corner_match_test`, `DeblockTest`, `FilmGrainTest`,
  `selfguided_filter_test`). It wraps googletest's internal
  `testing::internal::Random` with seed `0xbaba`
  (`test/acm_random.h:85-86`).
- `test/unit_test_utility.h` - used by 14 files. These helpers call C
  `rand()` without an `srand()` call, for example
  `test/unit_test_utility.c:41-45` and `test/unit_test_utility.c:75`.

The inputs are not stored anywhere. They are produced inside the test loop,
often 100 to 5000 blocks per parameter combination, and are discarded when
the test returns.

**Portability warning.** The seeds are deterministic, but the generated
sequences are not portable across standard libraries:

- `std::uniform_int_distribution` is allowed to map engine output to the
  requested range differently in libstdc++ and MSVC.
- googletest's internal `Random::Generate` is an implementation detail.
- C `rand()` has no required sequence across CRTs.

Do not assume that re-implementing the generator in this repo reproduces
SVT's bytes. Dump the bytes from a running SVT test binary instead, or
re-run the extraction on the same toolchain that will consume the fixture.

### 3.2 In-process reference implementations

Most unit tests are differential: they compute the same output twice and
compare. The two usual reference styles are:

- the C kernel is the reference and the test compares a SIMD entry point
  against it. This is the dominant pattern in the deblock, CDEF, SAD,
  quantize, variance, convolve, intra, and motion tests. Expected values
  are therefore whatever the C function returns on the build machine, not a
  checked-in golden.
- a floating-point or independently written reference is the oracle. The
  transform tests use `test/ref/TxfmRef.h:39-65` for a double-precision 1D
  and 2D reference; the inverse 1D test instead runs the forward transform
  and checks the round-trip error against a per-type bound.

Some files validate a property instead of a reference, for example
`AdaptiveScanTest.cc` walks every `(tx_size, tx_type, scan position)` and
checks that the scan and inverse-scan tables agree.

### 3.3 Embedded expected data

The one substantial embedded corpus is film grain:

- `test/FilmGrainExpectedResult.h:11` - `add_grain_expected_luma[3][128*128]`
- `test/FilmGrainExpectedResult.h:1169` - `add_grain_expected_cb[3][64*64]`
- `test/FilmGrainExpectedResult.h:1369` - `add_grain_expected_cr[3][64*64]`

`FilmGrainTest.cc` compares its filtered planes against those arrays at
`test/FilmGrainTest.cc:210-218`. These arrays are portable and are the
easiest SVT data to reuse directly. The same file also constructs a fixed
`AomFilmGrain expected_film_grain` structure (`test/FilmGrainTest.cc:274`).

### 3.4 Downloaded media

Only the E2E suite uses external media. `test/e2e_test/test_vector_list.txt`
contains six entries with SHA-1 hashes:

- `park_joy_90p_10_420.y4m`
- `park_joy_90p_8_420.y4m`
- `niklas_640_480_30.yuv`
- `kirland_640_480_30.yuv`
- `screendata.y4m`
- `niklas_1280_720_30.y4m`

The E2E settings reference these by name in
`test/e2e_test/E2eTestVectors.h:58-119`. The default set is kirland and
niklas at 640x480, 60 frames (`test/e2e_test/E2eTestVectors.h:58-63`).

The path override is read from `SVT_AV1_TEST_VECTOR_PATH` in
`test/e2e_test/VideoSource.cc:184`.

### 3.5 Generated video, no file

API tests that need frames use `DummyVideoSource`, a moving color-bar
pattern. The eight bar values per plane are hard-coded in
`test/e2e_test/DummyVideoSource.h:32-36`, and the pattern repeats every 300
frames (`test/e2e_test/DummyVideoSource.h:30`). There is no file to copy.
The generator is simple enough to reproduce independently, or to dump as
raw frames.

### 3.6 Performance-only tests

`VariancePerfTest.cc`, `SubpelVariancePerfTest.cc`,
`Convolve2dSrPerfTest.cc`, `FullDistortionPerfTest.cc`, and the
`DISABLED_Speed` cases in many files measure timing only. They are not
correctness oracles. Do not import their outputs as expected data.

## 4. Unit test inventory by family

The "reuse" column is a recommendation, not a verdict on SVT's quality.
"High" means the family adds coverage that is cheap to turn into fixtures
and maps cleanly onto a ported layer. "Medium" means useful but with a
format, tolerance, or scope complication. "Low" means not worth importing.

### 4.1 Transforms, quantizers, scans, coefficient setup

Files: `FwdTxfm1dTest.cc`, `InvTxfm1dTest.cc`, `FwdTxfm2dTest.cc`,
`FwdTxfm2dApproxTest.cc`, `FwdTxfm2dAsmTest.cc`, `InvTxfm2dAsmTest.cc`,
`ForwardtransformTests.cc`, `FwhtAsmTest.cc`, `AdaptiveScanTest.cc`,
`quantize_func_test.cc`, `QuantAsmTest.cc`, `EncodeTxbAsmTest.cc`.

Data: procedural random residual/coefficient blocks; expected output is
the C fixed-point transform or the double reference, depending on file.
`FwdTxfm1dTest` uses 5000 random blocks per type; `FwdTxfm2dApproxTest`
compares N2/N4 reduced transforms against the full C transform;
`AdaptiveScanTest` is structural.

Port target: `l3_transforms` (`test_transform.cpp`, `test_txshapes.cpp`,
`test_transform_hbd.cpp`) and, for scan/token setup, `l7_entropy` and
`l16_rdo`.

Reuse: High for integer cases; Medium for the double-reference cases.
The port's existing `tools/golden_gen` already pins core shapes. SVT adds
wide random and boundary coverage, especially for the 16 transform types
and rectangular shapes. Dump C outputs; do not compare the port against
SVT's double reference unless the test header declares a tolerance first.

### 4.2 Entropy and bitstream

File: `BitstreamWriterTest.cc`.

Data: random symbols, written and read back in-process. The test checks
the writer and reader agree, not an external bitstream.

Port target: `l7_entropy` (`test_entropy.cpp`, `test_inter_symbols.cpp`)
and `l8_bitstream` (`test_bitstream.cpp`).

Reuse: Medium. Round-trip tests are already strong in this repo. The
useful additions are unusual symbol values, long coefficient runs, and
the exact symbol-boundary cases SVT generates.

### 4.3 Intra prediction

Files: `intrapred_test.cc`, `intrapred_dr_test.cc`,
`intrapred_edge_filter_test.cc`, `intrapred_cfl_test.cc`,
`FilterIntraPredTest.cc`, `highbd_intra_prediction_tests.cc`,
`subtract_avg_cfl_test.cc`.

Data: random neighbor arrays and blocks; the target predictor is compared
against a reference predictor (often the C path, sometimes a local
reference). The high-bit-depth file is x86/AVX-512 specific.
`subtract_avg_cfl_test.cc` covers the CFL subtraction helper.

Port target: `l4_intra` (`test_intra.cpp`), with CFL-adjacent pieces in
`l6_pipeline`/`l9_analysis` where the port carries them. CFL alpha
signaling is a named out-of-scope item in the port, so do not assume every
CFL helper has a CUDA counterpart.

Reuse: High for the non-CFL predictor and edge-filter cases; Medium for
CFL. These tests exercise random neighbor availability patterns, which
complements the port's hand-built edge fixtures.

### 4.4 Motion, SAD/SATD, hashes, global motion, OBMC

Files: `SadTest.cc`, `SatdTest.cc`, `OBMCSadTest.cc`,
`OBMCVarianceTest.cc`, `MotionEstimationTest.cc`, `HashTest.cc`,
`GlobalMotionUtilTest.cc`, `IntraBcUtilTest.cc`, `sad_neon_test.cc`.

Data: random blocks and strides; mostly SIMD against C. `HashTest`
checks the hardware CRC-32C kernels against the software path.
`GlobalMotionUtilTest` feeds random correspondences to the RANSAC
surface. `IntraBcUtilTest` covers `svt_aom_is_dv_valid`.

Port target: `l5_motion` (`test_motion.cpp`, `test_motion_search.cpp`,
`test_amvp.cpp`) and `l17_inter` (`test_ransac.cpp`).

Reuse: High. SAD/SATD/OBMC block sizes and strides are easy fixture
material, and the port already has the corresponding primitives. The
global-motion and DV-validity tests add boundary cases.

### 4.5 Deblocking, CDEF, restoration, film grain, Wiener

Files: `DeblockTest.cc`, `CdefTest.cc`, `SelfGuidedUtilTest.cc`,
`selfguided_filter_test.cc`, `RestorationPickTest.cc`,
`FilmGrainTest.cc`, `wiener_convolve_test.cc`.

Data: random pixels and parameters, target against C or local reference.
`FilmGrainTest` is the exception: it uses the embedded expected planes
described in section 3.3.

Port target: `l10_filters` (`test_filters.cpp`, `test_deblock_driver.cpp`,
`test_deblock_hbd.cpp`, `test_deblock_walls.cpp`, `test_cdef.cpp`,
`test_cdef2.cpp`, `test_cdef3.cpp`, `test_restoration.cpp`,
`test_restoration_pick.cpp`, `test_grain.cpp`) and `l12_convolve`
(`test_convolve.cpp`) for Wiener.

Reuse: High. This is one of the best matches between the suites. The
film-grain arrays are direct; the filter families need dumped C outputs.

### 4.6 Convolve, resize, FFT

Files: `Convolve8Test.cc`, `convolve_test.cc`,
`av1_convolve_scale_test.cc`, `ResizeTest.cc`, `FFTTest.cc`.

Data: random sources, filters, and scale parameters; target against a
reference implementation or the C path.

Port target: `l12_convolve` (`test_convolve.cpp`,
`test_interpfilters.cpp`), `l11_resize` (`test_resize.cpp`), and
`l9_analysis` (`test_fft1d.cpp`, `test_fft2d.cpp`).

Reuse: High for convolve and resize; Medium for FFT. The port's FFT tests
use float tolerances; SVT's FFT test is another float-to-float comparison,
so the same tolerance discipline applies.

### 4.7 Variance, quality metrics, noise, temporal filtering

Files: `VarianceTest.cc`, `HbdVarianceTest.cc`, `compute_mean_test.cc`,
`PsnrTest.cc`, `ssim_test.cc`, `VmafTest.cc`, `hadamard_test.cc`,
`noise_model_test.cc`, `TemporalFilterTestPlanewise.cc`.

Data: random planes and strides; target against C or a local reference.
`VmafTest` checks the activity-masking family; `hadamard_test` checks the
low/high-bit-depth Hadamard kernels; `noise_model_test` checks the film
grain noise model; `TemporalFilterTestPlanewise` covers the planewise and
ZZ temporal filter kernels plus the central filtering driver and noise
estimation.

Port target: `l9_analysis` (`test_variance.cpp`, `test_temporal.cpp`,
`test_noise_model.cpp`, `test_noise_util.cpp`,
`test_noise_strength.cpp`) and `l16_rdo` (`test_md_slices.cpp` for the
SSIM and high-bit-depth Hadamard pieces).

Reuse: High for variance, mean, noise, and temporal filtering; Medium for
SSIM/Hadamard because of the layer split.

### 4.8 Picture, block, palette, and prediction utilities

Files: `PackUnPackTest.cc`, `ResidualTest.cc`, `PictureOperatorTest.cc`,
`BlockErrorTest.cc`, `SpatialFullDistortionTest.cc`, `MemTest.cc`,
`PaletteModeUtilTest.cc`, `CompoundUtilTest.cc`, `WedgeUtilTest.cc`.

Data: random buffers and parameters; target against C or a local
reference. `MemTest` checks SVT's aligned allocation and copy helpers,
which this port does not use. `BlockErrorTest` checks
`svt_av1_block_error_c`, which has no obvious standalone counterpart in
this tree; the closest distortion code lives in the analysis and RDO
layers.

Port target: `l6_pipeline` (`test_picops.cpp`, `test_src_ops.cpp`),
`l9_analysis` (`test_packunpack.cpp`, `test_pad.cpp`),
`l13_palette` (`test_palette.cpp`, `test_segmentation.cpp`), and
`l17_inter` (`test_inter.cpp` for compound and wedge helpers).

Reuse: High for pack/unpack, residual, picture operations, palette, and
compound/wedge; Low for `MemTest`.

## 5. API tests

Files: `MultiEncoderTest.cc`, `SvtAv1EncApiTest.cc`,
`SvtAv1EncOnTheFlyApiTest.cc`, `SvtAv1EncParamsTest.cc`,
`SvtAv1EncRefMgmtApiTest.cc`, `SvtAv1EncRtcResolutionApiTest.cc`, plus
`params.h` and `SvtAv1EncApiTest.h`.

What they test: public API return codes, parameter validation, encoder
reuse, on-the-fly reconfiguration, reference management, and RTC
resolution changes. `params.h` is a large value matrix of default, valid,
invalid, and special values.

Data: parameter values are embedded in `params.h`; tests that encode use
`DummyVideoSource` rather than the downloaded vectors. Some API test
cases are disabled (`DISABLED_check_normal_setup` and others in
`SvtAv1EncApiTest.cc`).

Reuse: Low for the primitive CUDA port. These tests exercise the library
shell and parameter plumbing, not kernels. They become relevant only if
the project grows a public encoder API of its own.

## 6. E2E tests

Files: `SvtAv1E2ETest.cc`, `SvtAv1E2EParamsTest.cc`,
`SvtAv1RefMgmtBitstreamTest.cc`, plus the framework, video sources,
reference decoder, and decode helpers.

What they test: encode a video with specified settings, decode the
result with libaom, and compare the encoder's reconstructed frames with
the decoder output; also crash/death tests and parameter coverage runs.

Data: the six downloaded vectors, or the generated color-bar source.
Expected output is produced at run time by libaom, not stored.

Reuse: Medium and later. These are whole-encoder acceptance tests, not
primitive tests. The six video files are still the right long-term
integration corpus for this project's frame paths, and their SHA-1
hashes make them reproducible. They are large, so prefer a pinned
download script over committing the media.

## 7. This repo's suite

Measured on 2026-11-14:

| Metric | Value |
| --- | --- |
| CTest targets | 18 (`l0_core` through `l17_inter`; `pipeline` is the `l6_pipeline` target name) |
| Test sources | 83 tracked `.cpp` files under `src/*/tests/` |
| Non-blank test lines | 67,033 |
| `TEST_CASE` macros | 1,971 |
| `CHECK`/`REQUIRE` sites | 9,033 |
| CUDA `SKIP:` guards | 84 |

Framework: doctest (`third_party/doctest/doctest.h`). Each layer builds
its own executable with its own `tests/test_harness.cpp` defining
`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`; see
`src/l3_transforms/CMakeLists.txt:1-9` for the pattern. CTest registration
is the root `av1_cuda_test` helper (`CMakeLists.txt:12-20`), and the 18
layer targets are added at `CMakeLists.txt:21-40`.

Test data sources in this repo:

- Crafted inputs and independent oracles written into the test files
  (many expectations cite the SVT line they derive from).
- Runtime parsing of pinned SVT source tables and constants; for example
  `src/l15_rc/tests/test_rc.cpp:16` reads
  `third_party/SVT-AV1/Source/Lib/Codec/rc_tables.h`, and
  `src/l17_inter/tests/test_inter.cpp:30` reads
  `third_party/SVT-AV1/Source/Lib/Codec/inter_prediction.c`.
- Committed bitstream goldens under `src/l8_bitstream/tests/goldens/`
  (nine `.obu` files), checked by `tools/verify_decode4.ps1`.
- The golden gate: `tools/golden_gen` regenerates
  `tools/golden_gen/expected_primitives.txt`, compared byte-for-byte and
  separate from CTest. The byte-safe procedure is documented in
  `docs/development.md:33-45`.
- GPU tests compare device output against the host implementation and
  report `SKIP:` when no CUDA device is available
  (`docs/development.md:12-14`).

So the repo's data is repository-local and mostly generated from the
pinned SVT source. There is no downloaded media and no duplicate of SVT's
random loops.

## 8. Side-by-side

| Aspect | SVT-AV1 | This repo |
| --- | --- | --- |
| Framework | GoogleTest | doctest |
| Targets | 3 binaries: unit, API, E2E | 18 per-layer binaries |
| Registration | all three via `add_test` under `BUILD_TESTING` | 18 `add_test` calls via `av1_cuda_test` |
| Unit cases | 313 unit macros (49 disabled) | 1,971 `TEST_CASE` |
| Primary oracle | SIMD vs C, or C vs an independent reference | port vs independent oracle/golden/SVT C semantics |
| GPU | none | CUDA kernels vs host, with `SKIP:` guards |
| Unit data | generated at run time | crafted or repository-local |
| Embedded data | film-grain expected arrays | bitstream `.obu` goldens |
| Downloaded data | six E2E videos | none |
| Gate | fast unit/API/E2E pass/fail | CTest plus a separate byte-exact primitive gate |
| External deps | googletest; E2E needs libaom and vectors | doctest; CUDA optional for most tests |

The important difference is the direction of the oracle. SVT's unit tests
mostly assert "this optimized kernel equals the C kernel on this input".
Their expected values are not a portable golden corpus. This repo asserts
"this port equals the pinned SVT behavior", using explicit oracles and
generated goldens. To reuse SVT's coverage, import the inputs and the C
outputs, not the SIMD comparison harness.

## 9. Extraction plan

### Step 0: decide scope per family

Use the reuse column in section 4. Start with High-value integer families:
deblock/CDEF/restoration/grain, intra, SAD/variance, quantize, pack/unpack,
palette, compound/wedge. Treat the double-reference transform cases and
API tests as later. Do not import perf tests.

### Step 1: build a C-path dump harness

Do not modify the pinned tree in a way that must be committed. Put the
harness under `tools/svt_fixture_dump/` (a future slice) and link or reuse
the SVT build's object libraries. The harness must:

1. call `reset_test_env()` (`test/TestEnv.c:37-40`) or the explicit `_c`
   symbols so the C path is the one being recorded, never a SIMD path;
2. reproduce each test's input loop exactly, including the generator and
   seed that SVT uses (`SVTRandom` seed 13596, `ACMRandom` seed 0xbaba, or
   the file's own choice);
3. write the input and the C output as raw typed buffers, plus a text
   metadata file;
4. cap the iteration count when importing the 100-5000 block loops, but
   keep every parameter combination and every boundary input. Record the
   original loop count in the metadata.

Alternative for a small case: run the existing SVT test binary under a
debugger or add a local, uncommitted print hook. This is acceptable for a
one-off but is harder to repeat than a dedicated harness.

### Step 2: dump only what maps to a ported symbol

For each fixture, record the SVT translation unit, the C symbol, and the
port symbol. If no port symbol exists yet, park the fixture with a named
reason rather than adding a test that cannot run. Examples of current
non-matches: `MemTest` (SVT allocator), `BlockErrorTest`
(`svt_av1_block_error_c` has no standalone port counterpart), API
conformance, and the E2E whole-encoder path.

### Step 3: choose a fixture format

Proposed layout:

```text
testdata/svt/<family>/<case>/
    meta.txt        source file, test name, C symbol, commit, seed, params,
                    dimensions, element type, original loop count
    input.bin       one or more raw little-endian buffers
    expected.bin    the recorded C output
    README.md       command used, SHA-256 of each file, license note
```

Rules:

- Raw binary, little-endian, fixed-width integer types; no host-endian
  ambiguity.
- Keep each fixture under a few megabytes. For large planes, store the
  minimal block the kernel consumes, not the whole frame.
- A manifest at `testdata/svt/manifest.txt` lists SHA-256, source, and
  port target, so a stale fixture is detectable.
- For the six E2E videos, do not commit the media. Add a pinned download
  script keyed by the SHA-1 values in
  `third_party/SVT-AV1/test/e2e_test/test_vector_list.txt` and a metadata
  note.

### Step 4: wire fixtures into the existing layer tests

Each layer already has a doctest executable. Add a fixture loader in that
layer's `tests/` directory, not a new test framework. Keep the fixtures
read-only and the loader simple. Integer fixtures should compare
bit-exactly. Float fixtures must declare a tolerance in the test header
before the first comparison, as required by the project rules.

### Step 5: record provenance

Every import must state:

- pinned SVT commit `fd116158a00986d923db8a1fdddd0dbd5fd0e240`;
- SVT file and test name;
- SVT C symbol and port symbol;
- how the input was generated and whether the dump was sampled;
- SHA-256 of the fixture files;
- license statement for the SVT code and, for downloaded media, the
  aom-test-data terms.

## 10. Risks and review questions

- **Random-sequence portability.** The three generators are deterministic
  on one toolchain but not portable. This is the main reason to dump
  instead of regenerate. If a fixture is later regenerated on another
  toolchain, its bytes may differ; treat that as a new fixture, not as a
  regression.
- **SIMD vs C drift.** SVT's unit tests sometimes compare against AVX2 or
  AVX-512 entry points, not C. Always record the C path. The `_avx`,
  `_sse`, and `_neon` test families are not port targets.
- **Disabled tests.** 49 unit cases and 5 E2E cases carry `DISABLED_`.
  They do not run by default and must not be cited as expected behavior
  without enabling them deliberately.
- **Float tolerances.** SVT's transform tests use per-type `max_error`
  values. Those are SVT's tolerances, not this project's. Decide the
  tolerance for the CUDA fixture in the test file header first.
- **Scope creep from API/E2E.** The API suite tests the library shell;
  the E2E suite tests a full encoder. Neither maps onto a single layer
  today. Keep them out of the first import, except as a long-term media
  corpus.
- **Pinned tree.** Do not update `third_party/SVT-AV1/` to make an
  extraction easier. The snapshot is the reference; any harness lives
  outside it or stays uncommitted.
- **Repository size.** Prefer small, deduplicated fixtures and a download
  script for media. Do not commit multi-megabyte YUV files without a
  storage decision.

## Appendix A: SVT unit-test files by group

Transforms and quant: `FwdTxfm1dTest.cc`, `InvTxfm1dTest.cc`,
`FwdTxfm2dTest.cc`, `FwdTxfm2dApproxTest.cc`, `FwdTxfm2dAsmTest.cc`,
`InvTxfm2dAsmTest.cc`, `ForwardtransformTests.cc`, `FwhtAsmTest.cc`,
`AdaptiveScanTest.cc`, `quantize_func_test.cc`, `QuantAsmTest.cc`,
`EncodeTxbAsmTest.cc`.

Entropy: `BitstreamWriterTest.cc`.

Intra: `intrapred_test.cc`, `intrapred_dr_test.cc`,
`intrapred_edge_filter_test.cc`, `intrapred_cfl_test.cc`,
`FilterIntraPredTest.cc`, `highbd_intra_prediction_tests.cc`,
`subtract_avg_cfl_test.cc`.

Motion: `SadTest.cc`, `SatdTest.cc`, `OBMCSadTest.cc`,
`OBMCVarianceTest.cc`, `MotionEstimationTest.cc`, `HashTest.cc`,
`GlobalMotionUtilTest.cc`, `IntraBcUtilTest.cc`, `sad_neon_test.cc`.

Filters: `DeblockTest.cc`, `CdefTest.cc`, `SelfGuidedUtilTest.cc`,
`selfguided_filter_test.cc`, `RestorationPickTest.cc`,
`FilmGrainTest.cc`, `wiener_convolve_test.cc`.

Convolve/resize/FFT: `Convolve8Test.cc`, `convolve_test.cc`,
`av1_convolve_scale_test.cc`, `ResizeTest.cc`, `FFTTest.cc`.

Statistics: `VarianceTest.cc`, `HbdVarianceTest.cc`,
`compute_mean_test.cc`, `PsnrTest.cc`, `ssim_test.cc`, `VmafTest.cc`,
`hadamard_test.cc`, `noise_model_test.cc`,
`TemporalFilterTestPlanewise.cc`.

Utilities: `PackUnPackTest.cc`, `ResidualTest.cc`,
`PictureOperatorTest.cc`, `BlockErrorTest.cc`,
`SpatialFullDistortionTest.cc`, `MemTest.cc`, `PaletteModeUtilTest.cc`,
`CompoundUtilTest.cc`, `WedgeUtilTest.cc`.

Support files with no tests: `svt_av1_test.cc` (gtest main),
`warp_filter_test_util.cc` (helper), `frame_error_test.cc` (includes only,
no cases).

## Appendix B: quick command reference

```powershell
# configure and build SVT tests
cmake -S third_party/SVT-AV1 -B build_svt -DBUILD_TESTING=ON
cmake --build build_svt --config Release

# list and filter unit cases
SvtAv1UnitTests.exe --gtest_list_tests
SvtAv1UnitTests.exe --gtest_filter="*Quant*:*Cdef*"

# download E2E media
cmake --build build_svt --target TestVectors
$env:SVT_AV1_TEST_VECTOR_PATH = "C:\path\to\vectors"

# this repo's suite
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

Do not mix the two build trees: this repo's `build/` does not build SVT's
tests, and `build_svt/` does not build the CUDA port.
