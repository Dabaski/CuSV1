# Development and verification

Development and verification instructions for CuSV1.

## Environment

- Windows with Visual Studio 2022 and MSVC
- CUDA Toolkit 12.2
- CMake 3.22 or newer
- NVIDIA Pascal GPU (`sm_61`) for GPU execution tests and benchmarks

The host tests can be built without running GPU-dependent behavior. Tests that
need an unavailable CUDA device or capability report `SKIP:`. Kernel source is
compiled at runtime by NVRTC for `compute_61`.

## Build and test

From the repository root, configure and build the Release targets, then run the
full CTest suite:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

To run one layer's tests, for example `l4_intra`:

```powershell
ctest --test-dir build -C Release -R l4_intra
```

The CTest suite and golden gate are separate checks. Build the primitive
generator with:

```powershell
cmake --build build --config Release --target golden_primitives
```

The generated output must be written as UTF-8 without a BOM and LF line endings
before comparing it with `tools/golden_gen/expected_primitives.txt`. Windows
PowerShell 5.1's `>` redirection writes UTF-16LE and cannot produce a valid
byte-for-byte comparison. Use the byte-safe command documented in `AGENTS.md`.
The gate inventory and generator provenance are in
[`tools/golden_gen/README.md`](../tools/golden_gen/README.md).

## Decoder verification

The committed `.obu` fixtures are checked with the real decoder workflow. Use
`tools/verify_decode4.ps1` for the default geometry, `-Geometry 4|8|16|32|64`
for a grayscale fixture, or `-Color 4|8|16|32` for a color fixture. The
authoritative results are in [decoder conformance](decode_conformance.md).

`tools/decode_handoff.ps1` runs the available decoder handoff checks and writes
a dated local report. `tools/test_decode_handoff.ps1` tests that reporting
workflow.

## Benchmarks

Build and run `tools/bench/av1_bench` separately from CTest:

```powershell
cmake --build build --config Release --target av1_bench
build\tools\bench\Release\av1_bench.exe
```

Put the CUDA Toolkit `bin` directory on `PATH` before running. The tool records
GPU and driver information, measures host and GPU frame encodes and individual
kernel stages, and verifies GPU output against the host before reporting
timings. The frame path uses host-side decisions and per-block synchronous
kernel launches, so its measurements include launch and synchronization
overhead. Performance claims require paired before/after measurements with the
same configuration; the project rules and dated baseline are in `AGENTS.md`.

## Reference and third-party components

Third-party components and reference trees:

- `third_party/SVT-AV1/` is the pinned 1:1 reference snapshot, upstream commit
  `fd116158a00986d923db8a1fdddd0dbd5fd0e240` (2026-08-15; a post-v4.2.0
  snapshot). Do not update it without revalidating the goldens and provenance.
  See its `LICENSE` and `NOTICE` files and [provenance](provenance.md).
- `third_party/aom_dsp/` contains reader-side sources used by the entropy
  decoder port. `third_party/aom/` is a working-directory libaom checkout used
  to resolve reference citations; it is ignored by git and is not included in a
  fresh clone. See `third_party/aom/LICENSE` for terms.
- `third_party/dav1d/` is a working-directory decoder checkout used for source
  review. Like the libaom checkout, it is ignored by git and is not included in
  a fresh clone.
- `third_party/doctest/` contains the test framework (MIT License).
- `third_party/hardware_docs/` contains NVIDIA reference material used for
  performance concepts.

The decoder and reference-tree evidence contract is described in `AGENTS.md`
and [decoder conformance](decode_conformance.md). This project is not
affiliated with or endorsed by SVT-AV1's authors or the Alliance for Open Media.

## Documentation index

- [Project status](project_status.md) - measured progress and scope
- [Layer stack](layers.md) - implementation details by module
- [Bitstream](bitstream.md) and [emission walks](emission.md)
- [Color and chroma](color.md)
- [Decoder conformance](decode_conformance.md)
- [Provenance](provenance.md)
- [Roadmap and task register](todo.md)
- [FFmpeg integration blueprint](ffmpeg_integration.md)
