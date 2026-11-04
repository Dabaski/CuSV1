# The chroma/color pipeline - how 4:2:0 works in this port

End-to-end: how chroma prediction, mode decision, transforms, entropy
and the color container fit together, and where color conformance
stands. The luma bitstream mechanics are in `docs/bitstream.md`; the
emission walks in `docs/emission.md`; the conformance record in
`docs/decode_conformance.md`; provenance in `docs/provenance.md`.

## 1. Prediction (CH-series)

The predictors are plane-agnostic - chroma is the SAME luma primitive
set fed the FOLDED mode at the call site, exactly SVT's fold:

- `UvPredictionMode` (the verbatim enum) folds via `uv2y` (= the
  `get_uv_mode`/`g_uv2y` mirror; UV_CFL_PRED -> DC_PRED - the
  cfl_alpha AC-from-luma combine is out of scope).
- `buildIntraPredictorsUv` = the fold + the size-generic builder with
  FILTER_INTRA_MODES (chroma never uses filter-intra per
  enc_intra_prediction.c:641).
- Chroma filt_type comes from the UV mode map
  (enc_intra_prediction.c:28-33) - numerically the luma smooth set
  because UV_SMOOTH_* folds to SMOOTH_*.

GPU: the chroma tests fold `uv2y` HOST-side (the SVT call site) and
drive the UNCHANGED kernels - 30 combos per size (14 UV modes at
delta 0 + the 8 dr modes at deltas -1/+1) at 4x4/8x8.

## 2. Mode decision (CS4)

`decideBlockModeUv` (the size-generic CH3 policy): the 13 folded
candidates via `intra::uv2y`, UV_CFL_PRED excluded (it is not a D2
  candidate, pipeline.cpp:1726), SAD scored at the UV size, tie = lowest
index. UV-sized blocks; availability is per-plane.

## 3. Transforms (CS2 + RT9)

- The quant tables are SINGLE lookups (inv_transforms.c:3467/:3484);
  the runtime call passes all deltas 0 (initial_rc_process.c:805-807),
  so U == V == Y tables - l3 reuses them verbatim, no separate chroma
  quantizer.
- RT9 (the chroma divergence root cause): the intra-chroma tx type is
  DERIVED from the LUT - `intraUvTxType` (transform.h:97), 1:1 with
  `svt_aom_get_intra_uv_tx_type` - not the luma rule; and the l3 2D
  fwd/inv kernels gained PER-PASS 1D function selection (the
  asymmetric tx types), with the 4x4/8x8 siblings joining 16x16 (RT9
  step 3a).
- RT9c: the CS2 chroma quant drives moved onto the LUT domain, and the
  cross-slice U-equality widened back to grid <= 2.

## 4. Entropy (CS1)

- `ComponentType` (Y/U/V) threads through
  `writeTxbCoeffs`/`readTxbCoeffs` AND
  `writeBlockCoeffs`/`readBlockCoeffs` - six component-dim table
  families, the [PLANE_TYPES] rows selected by ComponentType.
- The uv_mode symbol: `writeUvMode`/`readUvMode`
  (entropy_coding.c:1077-1095 write, decodemv.c:823-837 read; ctx =
  [cfl_allowed][luma_mode], the DECIDED luma mode; alphabet
  UV_INTRA_MODES minus cflAllowed = 14 at bsize <= 32x32, 13 at 64x64;
  `default_uv_mode_cdf` extracted + `uv_mode_cdfs_default.inc`
  mechanically split).
- The uv angle-delta: `writeUvAngleDelta`/`readUvAngleDelta`
  (entropy_coding.c:1087-1092, decodemv.c:830-833) - gated OFF in our
  emission: write emits nothing, read consumes nothing and returns -1.
- The chroma txb_skip_ctx: the offset-7 branch of `get_txb_ctx` made
  live (ctx {7,8,9} enumerated; the 10-branch dead, named).
- The tx-type symbol is LUMA-only in-chain (entropy_coding.c:374-376).
- Deferred: `write_cfl_alphas` (:1060-1071) - dead in our emission.

## 5. The chroma-emitting walk (CS4/CS5/RT)

Per luma block in raster order (lumaB -> the UV tx = lumaB/2 per the
`av1_get_max_uv_txsize` map): [luma kf mode] -> [uv_mode - ONE per
block; the V TU shares the U decision] -> [luma chain] -> [U chain] ->
[V chain], three separate per-plane NAs. The partition + skip symbols
landed in the chroma walk (CS5b), the leaf emission follows
decode_partition's hierarchical tree order (RT6), the chroma
filter-intra flag surface is emitted where gated (RT7-b). The
chroma-ownership rule (`is_chroma_reference`) gates which blocks own
chroma. The full walk table + the artifact structures:
`docs/emission.md`.

## 6. The color container (CS3)

The D1 mono patch is UN-PATCHED for color streams: the SPS v3 color
config (mono bit 0, no early return, csp UNKNOWN 2 bits,
separate_uv_delta_q 0 - the color section 4 -> 7 bits) and the frame
header v3 (40 -> 42 bits: the U/V delta_q 1 bit each at 0; NO
diff_uv_delta bit - the vendored writer emits it only at 1,
:2379-2381). The D1 mono producers stay pinned for the mono artifact
set (gate lines diff-0). Gate lines: sps_obu_v3/frame_obu_v3/
tu_bytes_v3.

## 7. The color artifacts + conformance

Four committed color artifacts
(`src/l8_bitstream/tests/goldens/structural_keyframe_color*.obu`),
each three-way-identity-tested and decoded with the per-plane
instrument (`tools/verify_decode4.ps1 -Color <4|8|16|32>`, yuv420p
compared plane-by-plane):

- color32 (165 B), color16 (133 B), color8 (137 B) and color4
  (183 B): FULL-PLANE content 1:1 (all three planes equal the
  generator's recon). color16/color32 from CS5; color8 joined at the
  CS7 fix (`5ce0224`, the chroma bottom-left Z-order gate); color4 at
  RT21 (`927e697`, the chroma above-right Z-order gate - its last 43
  divergent V bytes of 1024, across UV leaves 41/42/48/56).

Full-plane color conformance is 4/4, CLOSED - the honest per-plane
matrix and the full chase record are in `docs/decode_conformance.md`.

## 8. The resolved chase: the chroma edge extension (CS7 + RT21)

The c8/c4 chroma residuals were resolved in two slices; the full
record (the per-plane matrix, the divergence maps, the RT21 regen
analysis) is in `docs/decode_conformance.md`. The pre-fix state: the
luma plane was content 1:1 after the RT10b bottom-left edge
extension, while c8 carried U 165/1024 and V 205/1024 and c4 U
244/1024 and V 287/1024. The investigation note below stays true and
is why the chroma got its own measurement slice before any fix: the
chroma chains run `buildIntraPredictorsUv` (a different builder with
its own edge setup and n_bottomleft call sites), and dav1d's U/V edge
availability comes from a SEPARATE `dav1d_prepare_intra_edges` call
handed LUMA coordinates (recon_tmpl.c:1455-1478) - the flags are not
even the same variables.

- CS7 (`5ce0224`): the chroma bottom-left gather became Z-order gated
  (`pipeline::bottomLeftPx` - the below-left leaf counts as available
  exactly when its Z position precedes the block's). c8 went exact on
  all three planes and c4's U followed.
- RT21 (`927e697`): the chroma above-right gather got the same
  Z-order gate (`pipeline::topRightPx`). c4's remaining V 43/1024
  closed: leaf 41's owner block walks one step later (zpos 36 vs 35),
  so the gather had read zeros from the zero-initialised cabove[65];
  the regen moved exactly those 43 bytes, all to 255.

`enable_intra_edge_filter = 1` was already correct in the v3 header
and confirmed set decoder-side - the FOOTPRINT was the suspect, never
the flag, and that is where both fixes landed.