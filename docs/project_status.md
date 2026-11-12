# Project status

## At a glance

CuSV1 is an experimental, correctness-first port of selected SVT-AV1 encoder
building blocks to CUDA 12.2 for NVIDIA Pascal (`sm_61`). Host reference
implementations are ported from the pinned SVT-AV1 snapshot; CUDA kernels are
JIT-compiled with NVRTC.

**Portable-C coverage: 23.7%**, measured 2026-11-11 by
`powershell -File tools\coverage.ps1` (37,453 of 157,718 non-blank reference
lines). This is a source-coverage progress measure, not an overall completion
or production-readiness claim. The denominator, exclusions, per-area
judgements, and paired conformance measure are documented by the coverage
instrument and `AGENTS.md`.

## Verified milestones

- The project has 16 CTest targets. The full Release suite last measured
  16/16 passing on 2026-11-05.
- The golden primitive gate last measured 427/427 lines with a zero diff on
  2026-11-10 (the RE6 regen).
- Nine committed structural AV1 artifacts cover five grayscale geometries and
  four 4:2:0 color geometries. Their decoder acceptance and reconstruction
  comparisons are recorded per artifact and plane in
  [decoder conformance](decode_conformance.md).
- The host and GPU frame paths are verified against each other at the five
  square geometries for the configurations recorded in the layer and
  conformance docs.

These fixtures establish the behavior they exercise. They do not establish
full SVT-AV1 encoder behavior or conformance for every possible input.

## Implemented areas

The current stack includes shared types and pixel planes, a CUDA driver/NVRTC
runtime, transforms and quantization, intra prediction, motion primitives,
frame composition, entropy coding, structural bitstream assembly, content
analysis, post-filter kernels, resize and convolution kernels, palette and
segmentation utilities, general utilities, and rate-control lookup tables.

The detailed layer-by-layer inventory and boundaries are in
[the layer stack](layers.md). Emission order is in [emission walks](emission.md),
the bitstream path in [bitstream](bitstream.md), and the 4:2:0 implementation
in [color and chroma](color.md).

## Important scope boundaries

- CuSV1 is not a production-ready encoder. Full inter prediction and motion
  compensation, broader partition decisions, rate control integration,
  multi-frame sequencing, and a stable public encoder API remain incomplete.
- Many analysis, filtering, and utility ports are host-only and are not yet
  connected to frame encoding. In-loop filter enable bits remain disabled.
- The frame mode-decision policy is project-defined; the port does not claim
  end-to-end equivalence with SVT-AV1's encoder decisions.
- The current GPU frame path makes decisions and gathers edges on the host and
  performs per-block GPU work synchronously. See [development and tools](development.md)
  for benchmark details.

The [FFmpeg integration blueprint](ffmpeg_integration.md) describes a future
integration target and assumes the encoder core is completed separately.
