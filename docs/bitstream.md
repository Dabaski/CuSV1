# The bitstream path — from decided modes to decoder bytes

How a frame's decisions become AV1 bytes: the entropy coder, the
symbol surfaces, the tile walk, the OBU container, the committed
artifact, and where decoder acceptance stands. Provenance citations
live in `docs/provenance.md`; the decode-conformance investigation in
`docs/decode_conformance.md`.

## 1. The od_ec range coder (l7)

The arithmetic coder is ported verbatim from the pinned tree's
`bitstream_unit.{h,c}` (encoder) and the vendored aom_dsp `entdec.c`
(decoder):

- Encoder: `odEcEncReset`, `odEcEncodeBoolEqQ15` (equal-probability),
  `odEcEncodeBoolQ15` (binary at f), `odEcEncodeCdfQ15` (n-symbol
  cdf), `odEcEncDone` (flush), `odEcEncTell`/`odEcEncTellFrac`.
- Decoder: `odEcDecInit`/refill/normalize, `odEcDecodeBoolQ15`,
  `odEcDecodeCdfQ15`, `odEcDecTell`.
- Encoder/decoder mutual consistency is an integration test (24-step
  mixed plan, f(g(x)) == x); every primitive is independently
  gate-verified byte-exact against the SVT C.

Deviation (named): `od_ec_dec_bits_` is declared-but-undefined in the
pinned tree — raw-bits decode is unported, not needed by the intra
symbol subset.

## 2. CDF adaptation

`updateCdf` (cabac_context_model.h:76-105) is the one adaptation
primitive: rate = 4 + (count >> 4) + (nsymbs > 3), counter capped at
32. `odEcWriteSymbol`/`odEcReadSymbol` both drive it when
`allow_update_cdf` is set, so writer and reader adapt every table
identically — the adapted-CDF-equality gate lines prove writer and
reader end each stream with identical tables.

`EcFrameContext` (`fc`) holds the frame's CDF tables: kf_y, angle
delta, filter-intra flag/mode, partition, skip, tx-type
(`intra_ext_tx_cdf` whole [3][4][13][17]) and the token tables
(txb_skip, dc_sign, eob flag/extra, base/br — q_ctx = 0 slices via
committed .inc files). `initDefaultEcFrameContext` seeds them
verbatim from `cabac_context_model.c`.

## 3. The symbol surfaces

| Surface | Contexts | Symbols | Notes |
| --- | --- | --- | --- |
| kf luma mode (BSF1) | getKfYModeCtx from the DECIDED neighbor modes; DC_PRED ctx when unavailable | 13 | readKfLumaMode returns the decoded mode + the raw delta symbol |
| angle delta (BSF1) | — | 7 | only when bsize >= 8x8 and the mode is directional |
| filter-intra (BSF1) | — | 2 + 5 | flag + mode; DC_PRED-only predicate, bsize <= 32 |
| partition (ECP1) | partitionPlaneContext: ctx = (left*2 + above) + bsl*4, INVALID -> 0 | 10/10/10/4/8 per bsl | forced SPLIT writes NOTHING; XOR edges use the gathered 2-symbol branches (the temporary's adaptation is discarded) |
| skip (ECP2) | getSkipContext = above_skip + left_skip of the neighbor mbmis | 2 | the FIRST arithmetic-coded symbol of each I_SLICE block |
| tx-type (TS3) | eset by getExtTxTypes, intra_dir | 5 (DCT_DCT, eset 2, reduced_tx_set intra) | gated by getExtTxTypes > 1 AND base_q_idx > 0; skipped entirely when eob == 0 (early return) |
| token chain (TS1/TS2) | NA-driven dc_sign_ctx; per-position nz-map contexts | — | see below |

The token chain per TU, in write order: txb_skip -> tx-type (q > 0)
-> eob position (get_eob_pos_token; eob_multi_size cdf switch) ->
eob extra -> base_eob + br for the last coeff -> reverse base + br
sweep -> forward signs (dc_sign_cdf at c == 0, raw bit after) ->
golomb when the level exceeds the base/br range. Contexts come from
the NA model (`DcSignLevelCoeffNa`: above/left packed
dc_sign<<6|cul_level, OR-accumulate over the TU mi extent) and the
per-position nz-map LUT. The reader is a symbol-for-symbol port of
aom's `read_coeffs_txb`.

## 4. Who emits what

- `encodeFrameAuto16x16`/`...16x16Q` emit per block (BSF1): kf mode +
  angle delta + filter-intra flag, with adaptation on; the Q path
  appends the token chain per block (TS3). Partition/skip symbols are
  NOT emitted by l6 — they belong to the generator's structural tile
  walk (ECP1/ECP2).
- The generator's `svtd_bsf3_tile_data` (v1) walks decode order:
  partition plane for the 64x64 -> 32x32 -> 16x16 tree (forced SPLIT
  silent, coded SPLIT at 32x32 ctx 8, four coded NONEs at 16x16 ctx
  4) + per-leaf skip + kf mode/angle-delta for the ratified modes,
  all skip = 1 (DC-only tiles).
- `svtd_bsf3_tile_data_v2` (TS4): the same partition/kf-mode walk
  with skip = 0 (context 0 everywhere) and the TS3-proven token chain
  appended per leaf — the only decodable composition, since the v2
  TU must carry real coefficients.

## 5. The OBU container (l8)

`AomWriteBitBuffer` writes MSB-first bits/literals; uleb128 encodes
payload sizes; `writeObuHeader` builds the header byte (forbidden 0 /
type 4 bits / extension / has_size hardcoded 1 / reserved 0) — it is
static in SVT and public here (named deviation). The temporal
delimiter `encodeTdAv1` is exactly 2 bytes (0x12 0x00).

`assembleStructuralKeyframeTU`/`...v2` pack:

1. TD;
2. the SPS OBU — payload written with phase-1 measure, then the uleb
   size rewritten (encode_sps_av1 structure); payload includes
   trailing bits; v1 and v2 SPses are byte-identical (qidx is
   frame-header state; the gate HALTs if the SPS moves);
3. OBU_FRAME — the uncompressed frame header + tile-group header
   (0 bytes for the single tile) + the tile data copied with no
   per-tile prefix at tile_cnt == 1.

Frame header v1: 22 bits + 2 pad — show_existing 0, KEY frame,
show_frame 1, disable_cdf_update 0 (the BSF4-fix; SVT keyframe
default), allow_screen_content_tools (force == 2, so the bit IS
written), frame_size_override 0, render-and-frame-size-different 0,
refresh_frame_context 1 (DISABLED — the might_bwd_adapt region went
live with the fix), then 3 byte_alignment pad bits. The dst is zeroed
first so the pad bits are guaranteed zeros (named; SVT relies on its
buffer state).

Frame header v2 (TS4, lossy): base_q_idx = 100, delta_q_present = 0
(delta_lf nested inside), encode_loopfilter zeros (the level[2]/[3]
U/V pair skipped at zero and for mono), tx_mode_select = 0 =
TX_MODE_LARGEST — 40 bits, exactly 5 bytes, no padding; CDEF/
restoration still skipped (seq cdef_level = 0, enable_restoration 0).

The monochrome (D1) patch lives in the generator's composition: three
court-ratified spans in the pinned writer (is_monochrome const 0 -> 1;
the commented-out spec mono branch live, which writes color_range and
skips subsampling + separate_uv_delta_q; the U/V quantization delta
writes skipped — the spec's num_planes guard the pinned writer
lacks). Decoder-confirmed: libdav1d/ffprobe read the stream back as
gray(pc).

## 6. The committed artifact

`src/l8_bitstream/tests/goldens/structural_keyframe.obu` (45 bytes,
v2) is produced from the generator output — never hand-typed. The l8
test proves the three-way identity: composed TU (l6 decisions through
l7 symbols + l8 assembly) == committed file == gate bytes
(tu_bytes_v2). The v1 byte stream remains gated (tu_bytes).

Decoder status (measured):

| Stream | Result |
| --- | --- |
| v1 lossless TU (23 bytes, BSF4-fix bytes) | full decode attested: libdav1d silent exit 0; ffprobe reports av1, 32x32, gray(pc) |
| v2 lossy TU (45 bytes, real token streams) | ffmpeg probe OK: av1 (libdav1d) (Main), gray(pc), 32x32 |
| TD0 probe ladder | skip=1 rungs conform bit-exact in libdav1d AND libaom; skip=0 token-chain rungs desync — open, see docs/decode_conformance.md |

## 7. The coupling invariant (BSF4-fix)

SVT couples `ec_writer.allow_update_cdf = !disable_cdf_update`
(ec_process.c:101). Both l6 emission sites hardcode
allow_update_cdf = 1 — correct ONLY because the ratified config
carries disable_cdf_update = 0 (the SVT keyframe default). Any future
disable_cdf_update = 1 config must flip the emission with it, or the
stream is unspecifiable: the writer adapts CDFs a conformant decoder
will not replay. The fix was found by the decoder itself
(libdav1d AVERROR_INVALIDDATA at the frame OBU) and is stated as an
invariant at both emission sites (pipeline.cpp).
