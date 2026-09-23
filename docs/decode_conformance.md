# Decoder conformance — status and investigation record

The project's bar is: a reference decoder accepts what we emit. This
doc records what is proven, what is measured, and what is still open
in the TD-series investigation. Provenance for every port artifact is
in `docs/provenance.md`; the bitstream construction in
`docs/bitstream.md`.

## Status summary

- The v1 lossless keyframe TU (23 bytes) is decoder-ACCEPTED: attested
full decode (libdav1d silent, exit 0; ffprobe av1/32x32/gray/pc).
- The v2 lossy TU (47 bytes, real token streams) DECODES with the
content 1:1: all 1024 decoded pixels equal the generator's recon
(byte-diffs 0/1024; the fingerprint: the first pixels `05 08 0c 11`
- the ecfrm_recon row-0 values 5 8 12 17). Measured by
`tools/verify_decode4.ps1` (ffmpeg 8.1.2 / libdav1d 1.5.3-62, exit 0).
- Per-geometry content 1:1 CLOSED (FS-series): all five committed
artifacts decode with ffmpeg exit 0 AND the decoded picture equals the
generator's recon line per geometry:

| Geometry | Artifact | Decoded | Match vs |
| --- | --- | --- | --- |
| d4 (8x8 frame, four 4x4 TUs) | structural_keyframe_d4.obu (30 B) | 64/64 | fs5g4_recon |
| d8 | structural_keyframe_d8.obu (30 B) | 64/64 | fs28_recon |
| d16 | structural_keyframe_d16.obu (44 B) | 256/256 | fs216_recon |
| d32 | structural_keyframe.obu (47 B) | 1024/1024 | ecfrm_recon |
| d64 | structural_keyframe_d64.obu (441 B) | 4096/4096 | fs264_recon |

These establish content equality for the five committed fixtures, not
every possible encoder input or full SVT encoder behavior.
Mode-decision/composition remain project-defined policy.
- Our writer <-> aom-entdec roundtrip is EXACT on every TD probe rung
(adaptation on): the arithmetic, the CDF evolution and the defaults
are internally consistent with aom's own decoder primitives.
- CLOSED (TD-series resolved): every q100 rung of the bisect ladder
conforms bit-exact in the real libdav1d — 7/7 rungs PASS (the conformance
gate). Root cause named and fixed: the coefficient-CDF qindex bucket
(TD5a/TD5b, the resolution below).

## Tooling

- `tools/decode_handoff.ps1` — runs dav1d / aomdec / ffmpeg against
  the committed artifact and writes a dated report
  (`decode_handoff_results.txt`, untracked).
- `tools/td0_ladder.ps1` — the bisect ladder driver (TD0).
- Local decoder-side oracles: `third_party/aom/` and
  `third_party/dav1d/` are cloned in place and GITIGNORED — never
  built or committed by this repo (REFERENCE PINNING for
  `third_party/SVT-AV1/` is unchanged). They serve as the
  decode-side oracle and audit source. The available conformant
  oracle on this machine is ffmpeg's libdav1d/libaom; both agree on
  every rung measured.

## TD0 — the bisect ladder

Minimal 16x16 one-block streams, each isolating one tile-wire surface
(the SPS walk is parameterized by max_dim = 16 for the rungs; 32
verbatim for the artifacts). Every rung publishes `td0_rungX_tu` +
`td0_rungX_pic` (the generator's expected recon) as gate lines; the
read-back arm decodes each tile with OUR reader (the vendored aom
entdec verbatim, adaptation ON) and proves the symbols round-trip.

| Rung | Isolates | Measured result |
| --- | --- | --- |
| a | q0 header, skip=1, DC_PRED + FI flag (partition/skip/kf-mode/mode-ctx) | PASS 256/256 byte-exact |
| e | V_PRED + angle-delta symbol (127-flat V pred discriminates the mode VALUE) | PASS 256/256 |
| b | q100 header, skip=0, eob=0 TU (txb_skip only; no tx-type via the early return) | ffmpeg exit 69 (libdav1d AND libaom: "Failed to decode tile data") |
| f | b + eob=1 chain (tx-type + eob_pt + base_eob + dc sign) | ffmpeg exit 69 |
| g | f + eob=2 (a br symbol + sweep + raw sign bit) | decodes, DIVERGES 240/256 first@0 (134 vs 129) — AC coefficients lost |
| c | eob=5 base/br/sign/eob_extra surface | decodes, DIVERGES 250/256 first@0 (142 vs 129) |
| d | c + golomb (DC 20 >= MAX_BASE_BR_RANGE 15) + D203 mode | decodes, DIVERGES 250/256 first@0 (130 vs 128) |

Control: rung b with 2 zero bytes appended (uleb fixed) still fails —
not a tail/overread artifact.

Verdict: EVERY skip=1 rung is bit-exact; EVERY skip=0 TU desyncs —
the divergence class is the TU symbol stream, entered at or before
the txb_skip symbol.

## Static-arm audit (fresh aom master + dav1d cdf.c)

Every tile write site was audited symbol-for-symbol against
av1/decoder/{decodetxb.c, decodemv.c, decodeframe.c}: chain order,
eob group_start/offset_bits tables, base_eob +1s, the br loop
(BR_CDF_SIZE 4, idx += 3, COEFF_BASE_RANGE 12), the golomb threshold
(MAX_BASE_BR_RANGE 15) and read_golomb shape, dc_sign_cdf vs raw sign
bits, the nz-map is_eob position-only branch (verbatim in the vendored
SVT), the tx-type eset 2 / 5-symbol / ind-1 mapping, the FI predicate
(DC_PRED-only in BOTH trees), the CDF defaults (kf/txb_skip/eob/
tx-type rows byte-equal to dav1d's raw values), the ICDF inversion
conventions (SVT's macro omits the terminal ICDF(CDF_PROB_TOP) but
the array zero-fill makes them identical), the update_cdf formulas
(SVT's count-warmup rate == aom's 3 + (count>15) + (count>31) +
nsymbs2speed derivation — targets identical), the bool-vs-cdf
2-symbol route (f = icdf[0] = P(sym1), verified both directions), and
the dequant tables (dc_q(100) = 93 measured — matches the decoder's
observed +1-per-level behavior). ALL CLEAN.

## TD1 — writer-state trace + the aom-encoder isolation

Instrument: every tile symbol in the generator's rung path emits a
state line (W rung:symbol val/rng/low/cnt, the od_ec encoder state
AFTER the encode). The full encoder state evolution is now printable
per symbol for any rung.

Findings (measured):

(a) The fresh-aom-entenc-vs-our-writer comparison CANNOT separate:
the aom encode of the same symbol sequence produces DIFFERENT bytes
(e.g. rung f: writer 3 bytes vs aom 5 bytes for the same alphabet)
but BOTH decode identically under the aom entdec — od_ec's tail
freedom.

(b) The decisive isolation: aom's OWN entenc output for the f-sequence
fed to the DECODER still fails (exit 69) — the tile bytes are
exonerated.

(c) The cheap OBU/header variants are clean: the 9-byte tile already
had valid CDF structure, and the tile walk states match the
aom-entdec primitive-for-primitive at every symbol (verified against
the writer trace, state-for-state).

One measured boundary datum: dav1d msac MIN-term vs aom/SVT
MIN*(nsyms - ret) differs by exactly EC_MIN_PROB per boundary —
TD1-measured 62304 vs 62320 rng drift.

## TD2 — the context-equality gate (exhaustive instrument)

`golden_primitives` links the l7 twins (extern-C value/byte shims) and
enumerates the ENTIRE reachable context domain per size (TX_4X4/8x8/
16x16, TX_CLASS_2D): get_lower_levels_ctx_eob over scan_idx [0, n);
get_br_ctx_eob over pos [0, n); get_nz_map_ctx_from_stats over
(pos, stats) with stats [0, 31] (reachable clipped domain [0, 15] +
unreachable belt); get_nz_mag + get_lower_levels_ctx over the full
clipped-reachable cell space {0..3}^5 per pos (1024 combos) + clip3
belts; get_br_ctx over the raw-sum sweep {0..4}^3 per pos (sums
0..12) + saturation belts; txb_init_levels full-buffer TX_PAD_2D
state over a 16-value sweep with 0xA5 poison (stride/padding/END
participate).

Measured (this machine): ectx4 35504 / ectx8 177472 / ectx16 745296
assertions, fnv-pinned, ok — 958,272 assertions, ZERO mismatches. The
golomb gate (ectxg 131076): writeGolomb bytes == SVT write_golomb
bytes for EVERY g in [0, 65535], and our readGolomb roundtrips the
SVT-written stream for EVERY g. Raw-sign gate: 4 patterns, 0
mismatches.

Verdict: the context surface (lower_levels/nz_map from_stats + the
full nz-map offset LUT consumption/br_ctx/eob variants/txb_init_levels
buffer state) and the reverse-pass surface (golomb + signs) are
EXONERATED for the TD tile divergence.

Harness finding (named): the first shim design cast generator structs
onto l7 types and crashed (0xC0000005) — the l7 `AomReader` = {ec,
allow_update_cdf} vs the vendored bitreader.h aom_reader = {buffer,
buffer_end, ec, allow_update_cdf}: structurally different, ec at
different offsets. Harmless in l7's self-consistent use; the gate now
exchanges bytes/values only. The crash was the harness's, not the
codec's.

## Remaining hypothesis space (CLOSED by TD4/TD5)

After the exoneration, the named suspects:

1. `od_ec_enc_done` tail semantics for tiles ending on short symbols
   (the writer's final-byte flush freedom interacting with the
   decoder's end-of-buffer refill).
2. The decode-side boundary semantics: dav1d's msac MIN-term vs
   aom/SVT's MIN*(nsyms - ret) — differs by exactly EC_MIN_PROB per
   boundary (TD1-measured 62304 vs 62320 rng drift). Our reads follow
   aom/SVT; dav1d is the oracle that desyncs — this is the leading
   suspect.
3. A symbol-surface element only present in the real decoders' walk
   that our roundtrip does not model.

Next decisive instrument (per the TD0 plan): rung 8 — construct a
skip=0 tile whose eob_pt symbol uses a state-traced
(2811/30016)-pair through aom's OD_EC in isolation, tracing at the rng
level.

**Resolution (2026-09-21, TD4/TD5).** None of the three suspects was
the bug. The TD4 spec arm fetched the normative text and the real
dav1d instrument (1.5.4, throwaway build, trace-patched at
`dav1d_msac_decode_symbol_adapt_c` and `ctx_norm`) exposed the actual
divergence: at the first coefficient symbol (txb_skip) the real
decoder uses a DIFFERENT cdf row than our writer. The spec's
`init_coeff_cdfs` (07.bitstream.semantics.md:1800-1820) mandates
selecting the token tables by the frame's base_q_idx (the verbatim
get_q_ctx, cabac_context_model.c:1907-1918: q<=20 -> bucket 0,
<=60 -> 1, <=120 -> 2, else 3). The writer used the idx-0 bucket for
every frame; q100 streams require idx 2. The measured row the real
dav1d used ({3089, 3920, 6038, 9460, 14266, 19881, 25766, 29176} =
aom token_cdfs.h:861, the idx-2 EobPt256 row) confirmed it. The TD5
fix (generator `ts1_init(fc, base_qindex)` mirroring
svt_av1_default_coef_probs :1919-1938; the l7 mirror
`initDefaultEcFrameContext(fc, base_q_idx)` with mechanically
extracted 4-bucket tables) restored conformance. Both named formulas
(spec / aom entdec / dav1d msac) are identical once the
n_symbols-vs-boundary-count indexing convention is resolved (dav1d's
n_symbols = the boundary count; aom's nsymbs = the symbol count) -
the TD1 "dav1d deviates by MIN" reading was an artifact of that
convention, not a bug in either decoder.

Measured close-out (this machine): the 7-rung ladder is 7/7 PASS
(byte-exact against the expected pics AND ffmpeg exit 0, tools/
td0_ladder.ps1); the 47-byte artifact decodes exit 0 with the content
1:1 (all 1024 pixels equal the generator's recon, byte-diffs 0/1024,
the fingerprint 05 08 0c 11; tools/verify_decode4.ps1). The rung-b
exit-69 observed with ffmpeg's libdav1d (1.5.3-62) does NOT reproduce
in the real dav1d 1.5.4 (it decodes rung b cleanly) - a decoder-
version artifact, not a defect in our stream; the v2 header field
walk was verified against dav1d's own parse (reduced_txtp_set=1,
base_q_idx=100, txfm_mode=LARGEST) during TD4.

Discipline: TD0-TD4 made NO production-code change (the instruments
are committed as standing artifacts); TD5a/TD5b made the ratified
bucket-selection change (the generator + the l7 mirror) with the
ladder as the conformance gate. The TS-series closes with
decoder-acceptance AND content-1:1.
