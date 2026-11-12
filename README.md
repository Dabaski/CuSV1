# CuSV1

An experimental port of selected [SVT-AV1](https://gitlab.com/AOMediaCodec/SVT-AV1)
encoder building blocks to CUDA 12.2, targeting NVIDIA Pascal (`sm_61`). Host
reference code follows the project's pinned SVT-AV1 snapshot, and CUDA kernels
are compiled at runtime with NVRTC.

> **Progress: 23.3% portable-C coverage** (36,715 of 157,718 non-blank
> reference lines), measured 2026-11-07. This source-coverage metric is not a
> claim of overall completion or production readiness. The decoder-verified
> artifacts and current scope are summarized in [project status](docs/project_status.md).

## Current state

The project includes transforms and quantization, intra prediction, frame
composition with RD-lite mode decision (distortion + rate cost, the angle
delta enumerated per block), entropy coding, structural AV1 bitstream
output, and a growing set of analysis, filtering, and utility modules. Nine
committed grayscale and 4:2:0 color artifacts have decoder conformance
results recorded in the
[conformance matrix](docs/decode_conformance.md).

CuSV1 is still an experimental project, not a production-ready encoder. The
[project status](docs/project_status.md) summarizes verified milestones and
important boundaries. See [the layer stack](docs/layers.md) for implementation
details.

## Build and test

Prerequisites: Windows, Visual Studio 2022 with MSVC, CMake 3.22+, and CUDA
Toolkit 12.2. GPU execution tests require a compatible NVIDIA device.

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

For setup, individual test targets, the golden gate, decoder verification, and
benchmarks, see [development and verification](docs/development.md).

## Documentation

- [Project status](docs/project_status.md) - progress, verified milestones,
  and remaining scope
- [Development and verification](docs/development.md) - build, tests, decoders,
  benchmarks, and reference notes
- [Layer stack](docs/layers.md) - implementation details by module
- [Bitstream](docs/bitstream.md), [emission walks](docs/emission.md), and
  [color/chroma](docs/color.md)
- [Decoder conformance](docs/decode_conformance.md) - per-artifact results
- [Provenance](docs/provenance.md) - reference sources for ported code
- [Roadmap and task register](docs/todo.md)
- [FFmpeg integration blueprint](docs/ffmpeg_integration.md)

## Reference and licenses

The pinned SVT-AV1 snapshot in `third_party/SVT-AV1/` is the 1:1 reference for
this port. It is a 4.2-era snapshot and is not byte-identical to the official
v4.2.0 tag. See its `LICENSE` and `NOTICE` files. This project is not
affiliated with or endorsed by SVT-AV1's authors or the Alliance for Open Media.

Other third-party components and their license locations are described in
[development and verification](docs/development.md).
