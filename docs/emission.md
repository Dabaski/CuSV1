# Emission walks — how each geometry becomes a decodable tile

The FS-series story: what exactly is emitted per geometry, the wire
order of every symbol walk, the 64x64 scan contract, running partition
contexts, and how the five committed artifacts are structured. The
symbol surfaces themselves are in `docs/bitstream.md`; the decoder
measurements in `docs/decode_conformance.md`; provenance in
`docs/provenance.md`.

## The conformance rule this series obeyed

Internal writer/reader agreement proves self-consistency, never
conformance. Every emission surface here was settled against the REAL
decoder (ffmpeg/libdav1d, cross-checked with libaom where noted):
content-1:1 against the generator's recon, per geometry.

## 1. The per-geometry Q-loop walks

Each Q loop (one code path per geometry) emits, per block in raster
order, with `initDefaultEcFrameContext(fc, qindex)` (the bucketed
init — the token CDF bucket follows `base_q_idx`) and an
`odEcStopEncode` tail. Optional writer/frame-context/NA args default
to nullptr (legacy callers unchanged).

| Loop | Walk per block | Where |
| --- | --- | --- |
| encodeFrameAuto4x4Q | [skip=0 ctx 0][kf mode][FI iff DC_PRED][tokens] — NO partition symbol (a 4x4 frame reads none) | pipeline.cpp:499 (skip) |
| encodeFrameAuto8x8Q | [partition: 4-symbol BLOCK_8X8 row, RUNNING ctx][skip][kf mode][angle delta iff directional][FI iff DC][tx-type iff in-chain gate][tokens] + updatePartitionContext | pipeline.cpp:681-684 |
| encodeFrameAuto16x16Q | the ratified TD5b-era shape: [kf mode + angle delta][FI flag][tokens] — no partition/skip symbols in the loop | pipeline.cpp:758/:909 (the bucketed inits) |
| encodeFrameAuto32x32Q | the 8x8Q walk with the 10-symbol BLOCK_32X32 row | pipeline.cpp:1350-1353 |
| encodeFrameAuto64x64Q | the 32x32Q walk with the 10-symbol BLOCK_64X64 row; NO FI (bsize 64 > 32); NO tx-type (DCTONLY); tokens on the TX_64X64 scan contract | pipeline.cpp:1687-1690 |

Walk-shape rationale (aom decodeframe.c decode_partition
has_rows/has_cols semantics): a 4x4 frame reads no partition symbol
(the 8x8-level walk is forced-split, the 4x4 quadrant coded directly);
8x8 reads the 4-symbol row; 16/32/64 read 10-symbol rows at their
level. The d16 artifact's [part@16] walk is hand-rolled in the test
over the l6 coefficients — the 16x16Q loop itself keeps the ratified
shape.

## The 64x64 scan contract (the emission domain)

The TX_64X64 token domain is the ADJUSTED 32x32 — documented at
`svtd_default_scan_64x64_token`, cites inline:

- av1_get_max_eob(TX_64X64) = 1024 (inv_transforms.h:129-137; the
  quantize at full_loop.c:1262, log_scale 2).
- The scan pool is W=H=32, n=1024 (svt_aom_init_iscan,
  coefficients.c:331-337/:364); the nz-map LUT row aliases the 32x32
  table (coefficients.c:274); the token chain folds 64->32
  (common_utils.h:115-128).
- The forward wrapper COMPACTS rows 1..31 in place
  (svt_handle_transform64x64_N2_N4_c, transforms.c:2700-2707); the
  inverse takes the 32-WIDE input and remaps into the zero-padded
  64x64 (svt_av1_inv_txfm2d_add_64x64_c, inv_transforms.c:2615-2628).

The production contract: compact the fwd64 output's top-left 32x32,
quantize n_coeffs = 1024 with the 1024-position token scan
(`quantizeFp64x64Token`), expand the compacted dqcoeff into the
64-wide zeroed array before inv64; coeffs[0..1023] = the compacted
qc, [1024..4095] = 0. The legacy/GPU callers keep the 4096-wide
facade path — unification is the named FS5d follow-up (a
1024-position GPU kernel + bench work).

## Running partition contexts (FS5a)

The 8x8Q/32x32Q/64x64Q emission blocks gather the partition context
from RUNNING `pAboveRun`/`pLeftRun` arrays and call
`updatePartitionContext` after each coded block;
`writeBlockCoeffs` takes mi-unit args in all five loops (4x4: by,bx;
8x8: by*2,bx*2; 16x16: by*4; 32x32: by*8,bx*8; 64x64: by*16,bx*16).

For uniform grids the running contexts coincide with fresh INVALID
cells: the neighbor's partition-context-lookup bit at the leaf's own
bsl is 0 for every square size (partitionContextLookupLeft[32X32]=24
bit2=0, [16X16]=28 bit1=0, [8X8]=30 bit0=0, entropy.cpp:615-619).
This was mutation-check-proven — the uniform fixture PASSES under
fresh-INVALID ctx, so the wiring's necessity was proven by corrupting
the left-lookup byte (28 instead of 24: bit2=1 -> ctx 10 != 8) and
watching the test fail with the measured diffs (REQUIRE(60 == 61),
CHECK(39 == 122)); reverted. Lesson: coincidence-passing fixtures are
exposed by MUTATION, not hope (the lesson register, `docs/todo.md`).

The grid conformance gate is the fs5g32 fixture (the 2x2 of 32x32 at
64x64, q100, running ctxs + the running coefficient NA + the recon
feedback): modes 10 5 3 7, eobs 78 1024 984 142, bytes 353, rt
1 1 1 1.

## The per-geometry artifacts

Five committed artifacts, one per geometry
(`src/l8_bitstream/tests/goldens/`), each proving the three-way
identity (composed TU == committed file == gate bytes) and decoding
content-1:1 (`tools/verify_decode4.ps1 -Geometry <4|8|16|32|64>`;
ffmpeg 8.1.2 / libdav1d 1.5.3-62, exit 0):

| Artifact | TU size | Structure (the decoder-consistent walk) | Decoded == |
| --- | --- | --- | --- |
| structural_keyframe_d4.obu | 30 B | an 8x8 FRAME: [part@8 SPLIT, ctx 0][4x [skip][kf, no delta: bsize < 8X8][FI iff DC][tx][tokens]] — the decoders align frame dims to 8 px, so the 8x8 node reads the 4-symbol partition symbol and a 4x4 TU exists only as a leaf (dav1d 1.5.4 trace-verified) | fs5g4_recon 64/64 |
| structural_keyframe_d8.obu | 30 B | [part@8, 4-symbol row][skip][kf][delta][FI?][tokens] | fs28_recon 64/64 |
| structural_keyframe_d16.obu | 44 B | [part@16, 10-symbol row][skip][kf][delta][FI?][tokens] (hand-rolled walk over the l6 coeffs) | fs216_recon 256/256 |
| structural_keyframe.obu | 47 B | the four-16x16-leaves walk (the TD5b-era committed structure) | ecfrm_recon 1024/1024 (fingerprint 05 08 0c 11) |
| structural_keyframe_d64.obu | 441 B | [part@64, 10-symbol row][skip][kf][delta][tokens] (no FI at 64) | fs264_recon 4096/4096 |

The per-size SPS is parameterized by maxDim (frame_width_bits =
msb(maxDim); svtd_bsf3_encode_sps_dims; the l8
`writeSequenceHeaderObu` maxDim default 32 = the committed walk) —
gate lines sps_obu_d4/d8/d32/d64.

## Named follow-ups (open)

- The filter-intra DC-deciding fixture: the FI branch is
  predicate-gated but never fires in the committed fixtures (the
  fs232 fixture decided mode 1; the grid decided 10 5 3 7 — no DC
  block).
- FS5d: the 64x64 dual-domain unification — the emission case runs
  the vendored compacted token domain while the legacy/GPU callers
  keep the 4096 facade (the GPU quant_dequant_64x64 is a 4096-position
  kernel); for the fs264 fixture the two domains' recon coincide (the
  outer-ring coefficients quantize to 0 at q100).
