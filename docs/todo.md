# Production-Readiness Roadmap - av1_gpu_pascal

NOTE: this file was recreated 2026-09-23 (FS6) after the original untracked
copy was deleted in error, then RESTORED THE SAME DAY by the court from its
session record, merged with the FS6-era register. The production roadmap
(sec. 1-sec. 9) is the deliverable the boss requested; sec. A0 + the lesson register
carry the live slice-level state.

Purpose: the complete work inventory between the current verified state and
PRODUCTION-READY - defined as the step before the "ffmpeg blueprint" phase
(integrating the encoder behind a real consumption pipeline) can start.

North stars (AGENTS.md):
1. Bit-exact 1:1 with the PINNED vendored SVT-AV1 reference
   (third_party/SVT-AV1, 4.2-era snapshot). Every algorithm traceable.
2. Production-ready: not just correct - fast, robust, validated, packaged.

How to use this file:
- Statuses: [x] DONE (court/Luna-verified) - [~] IN FLIGHT - [ ] OPEN -
  (P) PARKED with a named trigger.
- Every OPEN item lands as AGENTS.md slices: TDD nano-cycle, RED evidence,
  one slice = one commit, full suite + GATE green, measured values only,
  citation grep, goldens via tools/golden_gen, deviations named.
- CONFORMANCE RULE (the TD-series lesson): internal writer/reader agreement
  proves self-consistency, never conformance. Every new bitstream surface is
  settled by the REAL DECODER (libdav1d / aomdec / ffmpeg) - content-1:1
  against the generator's recon, per geometry and per configuration.
- LESSON REGISTER (paid for in blood, applies to every item below):
  * A named deviation is a deferred bug with a timestamp (TS1 q_ctx=0 slice
    -> the TD4/TD5 root cause).
  * Self-consistent gates cannot see shared-port bugs (TD1's void claim;
    the FS2-delta and FS5b S==16 drive omissions - both caught by
    bit-exactness tests against production, provenance.md deviations #10).
  * Weak fixtures are defects (constant/ramp edges, spot-checked ranges).
  * Range conditions are enumerated, never spot-checked (R-series rule).
  * Measured values only; full rebuild before any ctest claim.
  * Coincidence-passing fixtures are exposed by MUTATION, not hope (FS5a:
    uniform grids pass under fresh-INVALID ctx; the wiring's necessity was
    proven by corrupting the lookup and watching the test fail).
  * Decoders align frame dims to 8 pixels: a 4x4 frame is an 8x8 node with
    forced SPLIT and 4-symbol partition reads; a 4x4 TU exists only as a
    partition leaf (FS5c, dav1d 1.5.4 trace-verified).
  * DERIVE, DO NOT TRANSCRIBE - and the rule covers DESIGN CONSTRAINTS, not
    just measurements. When a test's expected value is a function of
    structure the code already computes, derive it from that structure.
    A hand-transcribed literal is indistinguishable from a correct value to
    the reader, it keeps asserting a stale number after the structure
    changes, and the suite stays GREEN on a wrong expectation - which is
    the same failure as a coincidence-passing fixture, one level up. Live
    instance: the chroma bottom-left fix (section 9) must derive the
    per-geometry gather count from the zpos map plus the committed gate
    modes. If the derivation is awkward, fix the ergonomics of the
    derivation; never fall back to literals.
  * A constraint that lives only in a session exchange has not been
    recorded. Session context is not a durable surface: a brief
    reconstructed from the repo will silently lose anything not written
    down, and the loss is invisible because the work still looks like it
    was specified. Put constraints where the next reader actually looks,
    before the work starts - not in the commit message, which is the one
    place a future session will not read.
  * A falsifier is the FINDING, not an expectation to adjust. If a
    prediction fails, report the failure and what it implies. An
    expectation quietly edited to match its result has produced zero
    information while looking exactly like a passing test.
  * A check whose COVERAGE you cannot demonstrate is indistinguishable
    from a check that is working. A stale instrument and a clean tree
    look identical, and a scan of a clean tree proves nothing about a
    check that would have fired on a dirty one. So a numeric-claim
    auditor gets a SELF-TEST that must fire on a constructed bad case,
    not just a tree scan. Live instance: `tools/doc_audit.ps1`'s gate
    byte check needed THREE jointly-necessary conditions - the line
    names the gate file, the unit is `B` and not `bytes`, and the
    figure's punctuation matches the regex - and no stale line in the
    tree met all three, so it reported 0 mismatches over four wrong
    gate figures. The sharpest was the third: the comma branch was
    `{2,3}` comma-groups, and a figure written the ordinary way
    (`884,953 B`) carries exactly ONE, so the file's own gate figure
    was invisible to the check guarding it. Fixed in 32b2c57
    ({1,3}, `B\b|bytes?\b`); self-test now passes 4/4 and the live
    scan finds 6 gate candidates where the pre-fix scan found 4.
    The two patterns, run against the real figures in this repo,
    which is the whole finding in four lines (old was
    `(\d{1,3}(?:,\d{3}){2,3}|\d{6,9})\s*B\b`; new is the same with
    `{1,3}` and `(?:B\b|bytes?\b)`):
      `884,953 B`      old NO  -> new MATCH
      `884,980 bytes`  old NO  -> new MATCH
      `884980 B`       old YES -> new MATCH
      `1,770,750 B`    old YES -> new MATCH
    The two "old YES" rows are why the check looked alive: it was
    matching the unpunctuated form and the three-comma-group form, and
    the ordinary one-comma form is what every number in this file
    actually uses. **That makes three jointly-necessary conditions, not
    two** (file named, unit is `B`, punctuation matches), and each of
    the three presents as a condition that merely looked narrow.
  * A NARROW condition is a broken conjunction in disguise, and it
    does not look like one. Three in one arc: a `bcf16_modes` parser
    written for the ecs4 shape (misfires on the colon and the
    three-per-leaf arity), the `1>` UTF-16 redirect, and the gate byte
    check above. None reads as suspicious on its own; each looks merely
    format-sensitive, merely awkward, or merely cautious. The useful
    form of the rule is therefore not "audit the regexes" - it is that
    any condition guarding a claim should be checked against the exact
    shape the claim is actually written in, and the fix is a
    self-test, not a wider pattern.
  * **A BRIEF IS NOT A SOURCE.** Every figure handed to an agent must be
    re-derivable from the working directory, or it is not citable - and a
    brief that pairs numbers from two different SUBJECTS invites a
    cross-subject comparison, which is a wrong number wearing a comparison's
    clothes. Live instance, and both parties were wrong in different ways.
    A brief offered the throwaway dav1d's luma as "Y max 255, mean 233.5"
    against ffmpeg's "94/24.0". Measured 2026-09-29: 94/24.0 is real and is
    **color4** under ffmpeg `libaom-av1` (Ymin 0 / Ymax 94 / Ymean 23.9629),
    and it reproduces exactly; the 255/233.5 half is the throwaway's figure
    on the same artifact, from a decoder now retired and un-citable. Two
    errors, and they compounded. The brief cited a **%TEMP% measurement as
    though it were citable** - the exact failure the evidence contract exists
    to prevent, committed four commits after the contract. And the agent
    compared the throwaway's **color16** figures against that color4 half, so
    the two numbers being compared were different artifacts - that half was
    the agent's, not the brief's. **THE RULE, in the form that generalises:
    if a figure in a brief cannot be re-derived, that is a FINDING ABOUT THE
    BRIEF, not a defect in the number, and the correct response is to
    measure the subject and report which subject you measured.** Note the
    direction of harm: a brief is the one input with no provenance of its
    own, so it is exactly where a recollection enters the tree wearing the
    appearance of a measurement. Re-measurement is not scepticism about the
    brief; it is the only way to find out which artifact a number is about.
  * DELETE NOTHING that is evidence. When a fix makes an instrument's
    own written claim stale, the instrument was RIGHT and the comment
    was wrong. 32b2c57 kept the false 884,980 in the file, annotated,
    because removing it would have deleted the evidence for the very
    defect; the self-test carries the proof instead. Same logic as the
    frozen pre-fix fixture `cs7_prefix_baseline.inc`: a post-change
    artifact cannot serve as its own baseline.
  * A claim about WHAT A COMMIT CONTAINS is measured with git, never
    inferred from a commit count or from who was working on what. The
    trap is an inference running BACKWARDS: "this file has one commit,
    so it did not exist at the later commit" is false, because a file
    created at X exists at every commit after X. Both the 09690c7
    briefing and 32b2c57's own message made that inverted inference in
    the same direction on the same fact, about `doc_audit.ps1` at
    `db83ea2` (created at b246d44, which IS an ancestor - verified with
    `git merge-base --is-ancestor`). Corrected in 09690c7. "N
    commits" and "exists at commit C" are different claims and only the
    second is load-bearing.

------------------------------------------------------------
A0. SLICE-LEVEL STATE (the live register)
------------------------------------------------------------

- [x] FS-series CLOSED (2026-09-23): full-size lossy emission at every
  geometry. FS1 ectx32/ectx64 (8.3M assertions); FS2 per-size drives +
  the 64x64 scan-contract settle; FS3 4x4/8x8/32x32 emission + the
  in-chain tx-type gate fix; FS4a the fs264_rt root cause (tile_buf[256]
  overflow) + FS4b 64x64Q emission; FS5a the running partition-context
  wiring + fs5g32 grid gate (4e0a685), FS5b the per-geometry artifacts
  (a853ac9), FS5c ALL FIVE artifacts content-1:1 in the real decoder
  (687625f) - d4 30B 64/64, d8 30B 64/64, d16 44B 256/256, d32 47B
  1024/1024, d64 441B 4096/4096 (tools/verify_decode4.ps1 -Geometry);
  FS6 docs currency (8edc02d).
  - [x] CS-series LANDED (CS0-CS5c) + CS7 fix LANDED (5ce0224): the chroma
    entropy + emission + the color container + the four color artifacts -
    register entries in section 1.1; full-plane conformance **3/4**
    (color16/color32/color8 all three planes 1:1; **color4 Y and U exact,
    V 43/1024 divergent first at 647 over UV leaves 41/42/48/56** - a
    V-only row, not "3 of 4 conform", because the percentage would bury a
    specific measured defect).
- [x] RT-series (the chroma/edge conformance chase): RT1 the /RTC
  fixture fix, RT6 the tree-order leaf emission, RT7-a/b the
  cross-decoder instrument + the chroma FI flag surface, RT9 the
  intra-chroma tx-type LUT + per-pass 1D selection, RT10/RT10b the
  bottom-left edge extension (the luma residual eliminated), CS7
  step 1 phases (a)/(b)/(c)/(c2) the chroma bottom-left mechanism
  CONFIRMED at BOTH c8 and c4; the full record in
  docs/decode_conformance.md.
- [ ] **NEXT SLICE - the chroma bottom-left FIX. THE BRIEF IS SECTION 9 OF
  THIS FILE; read it before slicing, it is the spec.** SLICE 1 IS THE
  INVARIANCE TEST and it is not optional scaffolding: the per-geometry
  gather count must be derived mechanically from the zpos map plus the
  committed gate modes, NEVER hand-transcribed as a leaf count and never
  64 typed expected values. A literal count is indistinguishable from a
  correct one, survives later zpos changes by asserting a stale number, and
  keeps the suite green on a wrong expectation. The fix commit must also
  state the `y + th >= h` equivalence (with its derivation and its one
  live-if assumption) even though the diff does not require it, and must
    report - not adjust - a c4 prediction that fails. Both of those are in
    section 9, in the same hunk as the constraint, for exactly this reason.
    The fix also OWES THREE TRACKED-DOC UPDATES, itemised at the end of the
    section 9 entry (AGENTS.md l6 - DONE, with the rule/call-site/gather
    distinction now recorded; this file's OPEN -> LANDED flip with the
    measured numbers; and the decode_conformance.md colour matrix) - a fix
    committed without them leaves three documents stating something false.
- [ ] OPEN (named): the filter-intra DC-deciding fixture (the FI branch is
  predicate-gated but never fires in the committed fixtures).
- [~] NAMED FOLLOW-UP (FS5d): the 64x64 dual-domain unification - the
  emission case runs the compacted 1024-position token domain; the
  legacy/GPU callers keep the 4096-wide facade; unification = a
  1024-position GPU quant kernel + bench protocol.
- [~] PENDING: Luna's full audit of the FS/TD state at series close.

------------------------------------------------------------
0.5 COVERAGE MAP (which roadmap items move the north-star metric)
------------------------------------------------------------

North star #1 in AGENTS.md is a QUALITY bar; the PROGRESS bar is the
portable-C coverage percentage, defined in AGENTS.md ("Completion
metric") and regenerated by `powershell -File tools\coverage.ps1`.
As measured 2026-09-27 it is **13.1%** (20,659 of 157,718 non-blank
lines in `Source/Lib/{Codec,C_DEFAULT}`). The per-area denominator
lives in `tools/coverage_areas.txt`; the script measures, that file
judges, and the two must be re-run together after any edit.

This section exists so a reader can tell, per roadmap item, whether
completing it moves the metric. Read it before picking work.

| id | SVT area | non-blank | cov% | covered | this file |
|---|---|---|---|---|---|
| A1 | Transform/TX | 27,008 | 33% | 8,913 | 1.3 |
| A2 | Intra prediction | 3,478 | 90% | 3,130 | 1.1 (CFL deferred) |
| A3 | Inter pred + ME/MC | 17,738 | 2% | 355 | 2.2, 2.3, 2.4 |
| A4 | Mode decision + RDO | 31,088 | 3% | 933 | 1.6 |
| A5 | Entropy coding | 8,460 | 55% | 4,653 | 1.3, 1.4, 2.2 |
| A6 | Coding loop + PCS | 13,928 | 10% | 1,393 | 1.3, 1.5 |
| A7 | Bitstream + OBU | 1,436 | 40% | 574 | 1.4, 1.5 |
| A8 | Post-filters | 10,600 | 0% | 0 | 3 |
| A9 | Rate control | 5,951 | 0% | 0 | 2.5 |
| A10 | Pic mgmt + threading | 9,662 | 0% | 0 | 2.4 |
| A11 | Analysis + temporal | 12,028 | 0% | 0 | **none - the AN-series gap** |
| A12 | Palette + segmentation | 1,612 | 0% | 0 | 4 |
| A13 | Utility + headers | 5,357 | 5% | 268 | - |
| A14 | C_DEFAULT kernels | 1,466 | 30% | 440 | - |
| - | excluded: rtcd dispatch | 7,906 | - | 0 | - |
| | **TOTAL** | **157,718** | | **20,659** | |

**+1.0 point = 1,577 non-blank reference lines.** So a slice is worth
about +1 point per 1,600 lines of ported reference surface.

**Read A5 and A7 together, not separately.** In this 4.2 snapshot
there is no separate obu.c / seq-header file: the whole OBU +
sequence-header + frame-header writer (2,064 physical lines,
entropy_coding.c:2104-3960) sits inside `entropy_coding.c` and is
therefore charged to A5, not A7. A7's small denominator is
structural, not a judgement that header work is unimportant.

**ITEMS THAT DO NOT MOVE THE METRIC.** These are real work and worth
doing, but they are PRODUCT work, not 1:1 port work, and a reader
should not mistake them for coverage: section 1.6 "RD-lite mode
decision" (it IS the A4 target, but the metric only moves when the
1:1 SVT algorithm is ported, not when our SAD-only policy is
extended), 1.6 "adaptive scan (default scan is policy)", 2.4
"keyframe placement + scene-change policy (OURS)", 1.7 (the CLI and
config surface), all of section 5 (GPU throughput architecture - it
makes what exists faster, it ports nothing new), all of section 6
(validation), and all of section 7 (packaging/ops).

**The two gaming vectors, named.** (1) `q_matrices.h` is 13,424 lines
of pure data and would add 8.5 points, but it sits dormant behind
`using_qmatrix = 0` and our `.inc` files pack at 8.87 numbers/line
against the reference's 15.97, so it costs ~2,835 of our lines per
point versus ~1,580 for algorithm code. (2) Any large verbatim data
table behaves the same way. The mitigation is the paired metric: a
slice counts as progress only if it moves the coverage number AND the
decoder-gated conformance count (`docs/decode_conformance.md`).

------------------------------------------------------------
0.6 RECOMMENDED CAMPAIGN (ordered by risk-adjusted metric value)
------------------------------------------------------------

The ordering is not "biggest area first". It is
(points gained) / (risk + effort), where the dominant risk term is
GOLDEN CHURN: any change to the reconstructed pixels invalidates all
nine committed `.obu` artifacts and every decode-gated claim, whereas
a change to analysis code touches no bitstream at all.

| order | target | A-id | points at 100% | why here |
|---|---|---|---|---|
| 1 | **Content analysis (AN-series)** | A11 | +7.6 | **Conformance-free.** Never touches the bitstream, so no golden regeneration and no decoder gate. Self-contained (no dependency on the recon path or l7/l8). Its output - histograms, activity maps, 8x8 variance, the noise model - is the INPUT side of the A4 RDO work that follows, so it is not throwaway. |
| 2 | Real RDO mode decision | A4 | +19.7 | Highest ceiling, hardest, and it invalidates every golden. Must follow analysis, because a rate-cost decision without activity inputs is a worse decision than the SAD policy it replaces. Budget a full golden regeneration as a planned, gated event. |
| 3 | Post-filters (deblock/CDEF/restoration) | A8 | +6.7 | Changes every recon byte: all nine goldens regenerate, plus a per-filter decoder gate. Note the payoff is SMALLER than it looks - the CDEF feature extractor (`third_party/SVT-AV1/third_party/fastfeat`, 6,015 lines) sits outside `Source/Lib` and is outside the metric, so A8 is 10,600 lines, not 16,615. Do after 2, while the conformance instrument is warm. |
| 4 | Inter + ME/MC | A3 | +11.2 | Structurally blocked on A10 (multi-frame, reference pictures) first, and on section 2.1-2.2 (the nonkey frame header and the inter entropy surface). Last. |

A10 itself is +6.1 at 100%, so the inter path (A3 + A10 + section 2.1-2.2) is a
+17-point campaign gated behind multi-frame infrastructure. That is the
largest single block of remaining work in the whole metric.

**Why not `q_matrices.h` early** despite it being the cheapest +8.5:
it is worst effort-per-point in the tree (2,835 of our lines per point
vs ~1,580), it has zero effect on encoder behaviour while
`using_qmatrix = 0`, and it would move the headline number by half a
point-decade without moving the product. If it is done at all, do it
last and label it as table transcription, not encoder progress.

------------------------------------------------------------
0. CURRENT STATE (the verified baseline - re-measured 2026-09-27 at
   commit 4b624b9; supersedes the 2026-09-23 reading, which predated
   the CS-series and the whole RT-series)
------------------------------------------------------------

[x] l0-l2: core types, Plane, NVRTC/driver runtime (compute_61 PTX JIT)
[x] l3: fwd/inv 1D+2D transforms, 4x4..64x64, DCT/ADST (DCT-only at 64);
    PER-PASS 1D selection landed (RT9 step 1/3a) so the asymmetric types
    ADST_DCT / DCT_ADST are expressible in both directions; the
    intra-chroma tx-type LUT (RT9 step 2, transform.h:97) and the
    LUT-domain chroma chains (RT9 step 3b/3c); quantizer (fp/b, all
    sizes, q0..q255 gate lines, log_scale 0/0/0/1/2); default scans
    through 64x64; the compacted 1024-position token domain at 64x64
    (the FS2 scan contract)
[x] l4: full intra predictor set, all 13 modes, 4x4..64x64, GPU kernels,
    corner-blend/upsample/edge-filter regimes, chroma fold (uv2y) +
    NeighborContext/filt_type + disable_edge_filter
[x] l5: SAD 4x4..64x64 host+GPU, strided uint8
[x] l6: mode decision (13-candidate SAD policy - OURS, documented), frame
    loops at 4x4..64x64 (Recon/Auto/ReconQ/AutoQ), real-TR gather, symbol
    emission (kf modes/delta/FI + partition + skip + token chains) at ALL
    FIVE geometries, running partition contexts (multi-block grids), AND
    the chroma-emitting SB-tree walk (encodeFrameChromaQ: the Z-order
    DFS leaf order, the chroma-ownership rule, the partition/skip
    surfaces, the bottom-left extension gather under the Z-order gate)
[x] l7: od_ec enc/dec (verbatim), update_cdf, writer/reader wrappers, KF
    mode/delta/FI surfaces, partition/skip symbols, the full token chain,
    qindex-bucketed coefficient CDFs (TD5, spec init_coeff_cdfs),
    LIVE neighbour-derived contexts (getKfYModeCtx, getTxbCtx +
    skip_contexts + nz-map + the chroma +7 base),
    updatePartitionContext (ECP1), and the chroma surfaces (uv_mode,
    uv angle-delta, six component-dim CDF families, ComponentType)
[x] l8: bit writer, uleb128, OBU/SPS/frame-header/TU assembly (v1 lossless
    22-bit, v2 lossy q100 40-bit, color v3 42-bit), per-size SPS (maxDim
    parameterized), NINE committed artifacts - grayscale d4 30B /
    d8 30B / d16 44B / d32 47B / d64 441B + color color4 201B /
    color8 139B / color16 133B / color32 165B
[x] MILESTONE: decoder-accepted AND content-bit-identical at EVERY
    geometry (libdav1d via ffmpeg; verify_decode4.ps1 -Geometry N);
    color full-plane 1:1 at color16/color32 (2/4 color artifacts -
    color4: Y and U exact, **V 43/1024 divergent** - the CS7 V-only finding,
    the one open conformance item)
  [x] Infrastructure: tools/golden_gen (committed gate, 421 lines,
      884,953 B), tools/coverage.ps1 + coverage_areas.txt +
      coverage_baseline.txt (the progress metric), tools/cite_check.ps1
      (the doc citation audit), tools/doc_audit.ps1 (advisory numeric-claim
      audit), tools/td0_ladder.ps1 (7/7),
      tools/decode_handoff.ps1, tools/bench, tools/verify_decode4.ps1,
    tools/rt7_trace_diff.ps1, tools/rt8_divergence_map.ps1
[x] Docs: README, AGENTS.md layer map + completion metric,
    provenance.md, decode_conformance.md, bitstream.md, color.md,
    layers.md, emission.md, cs5c_deltas.md, this file
[x] MEASURED 2026-09-27: ctest 9/9 (275.58 s), GATE 421/421 diff 0,
    portable-C coverage 13.1%, 9 committed .obu artifacts

------------------------------------------------------------
1. KEYFRAME ENCODER COMPLETENESS (intra-only; luma + 4:2:0 chroma since
   the CS-series - the monochrome limitation ended, see the CS register
   below)
------------------------------------------------------------

1.1 Chroma residual + uv_mode symbols (ends the monochrome limitation)
  CS-series PLANNED (CS0 scoping of record 2026-09-24, all claims
  court-verified in the pinned tree; statuses flip to [~] per slice on
  landing):
- [~] CS1 l7 chroma entropy (CS1a 4d73945 generator gate + CS1b the l7
      mirror): uv_mode write/read (entropy_coding.c:1077-1095
      write, decodemv.c:144-147/:823-837 read; ctx = [cflAllowed][luma_mode],
      the DECIDED luma mode; alphabet UV_INTRA_MODES - !cflAllowed = 14 at
      bsize <= 32x32 / 13 at 64x64; default_uv_mode_cdf cabac_context_model.c:105
      extract + uv_mode_cdfs_default.inc mechanically split) LANDED +
      the component index threading through writeTxbCoeffs/readTxbCoeffs AND
      writeBlockCoeffs/readBlockCoeffs (six component-dim table families;
      the [PLANE_TYPES] rows selected by ComponentType; LUMA-only tx-type
      gate entropy_coding.c:374-376) LANDED + writeUvAngleDelta/
      readUvAngleDelta (entropy_coding.c:1087-1092, decodemv.c:830-833;
      bsize >= 8X8 AND the FOLDED mode directional; gate enumerated over the
      full 14-mode domain) LANDED + the chroma txb_skip_ctx (offset 7 made
      live at the Block wrappers; ctx {7,8,9} enumerated; the 10-branch dead,
      named) LANDED. Gate: the ecuv set (27 lines, 362-line gate) mirrors
      byte-exact in l7 doctests; mutation-checked (the tx-type gate removal
      fails 17 assertions).       DEFERRED (named): write_cfl_alphas (:1060-1071) -
      UV_CFL_PRED is not a D2 candidate (pipeline.cpp:1726), the alphas are
      dead in our emission.
- [~] CS2 uv dequant + chroma Q drives (LANDED, generator-side): the quant
      tables are SINGLE lookups (inv_transforms.c:3467/:3484);
      svt_av1_build_quantizer (md_config_process.c:101-152) builds U/V from
      the same lookups with deltas, the runtime call passes all deltas 0
      (initial_rc_process.c:805-807) -> U == V == Y; l3 tables reused
      verbatim (source-confirmed, cited; no l3 code change). The CH3 32x32
      UV plane template generalized per reachable size {4,8,16,32}: per
      block U chain then V chain (U-before-V), D2 fold modes, fwd/FP
      quantize at q100/the CS1 chroma token chains (per-plane NA ctx, the
      :310-314 offset-7 branch)/dequant->inv->per-plane recon; the ecs2S_
      gate set (24 lines, 386-line gate); internal read-twin + cdf-equal
      asserts. l6 mirror rides CS4.
- [~] CS3 the D1 un-patch (LANDED as the spec-faithful v3 surfaces; the D1
      mono producers stay pinned - the artifact format, gate lines diff-0):
      SPS v3 color config (mono 0, no early return, csp UNKNOWN 2 bits,
      separate_uv_delta_q 0; color section 4 -> 7 bits, +3), frame-header v3
      (40 -> 42 bits, +2 = U/V delta_q 1 bit each at 0; NO diff_uv_delta
      bit - the vendored writer emits it only at 1 (:2379-2381), the aom
      reader reads it only when separate_uv_delta_q, decodeframe.c:1823-
      1825); sps_obu_v3/frame_obu_v3/tu_bytes_v3 gate lines (389-line gate);
      the mono spans' inversions cited in the code. The color .obu artifact
      + the per-plane decode check ride CS5 (CS3 stops at the gate).
      Other mono-gated spans (grep-verified): :3149 (LR flag,
      already-positioned), :3581 (deblock count, not bitstream).
- [~] CS4 l6 integration (LANDED): the chroma-emitting Q loop
      (encodeFrameChromaQ, lumaB 8/16/32/64 -> UV 4/8/16/32 per the
      av1_get_max_uv_txsize map common_utils.h:142-149): per luma block in
      raster order [luma kf mode symbol -> uv_mode symbol (writeUvMode, ONE
      uv_mode per block - the V TU shares the U-plane decision, the
      documented walk policy) -> the uv angle-delta where gated] then the
      LUMA chain -> the U chain -> the V chain (U BEFORE V, three separate
      per-plane NAs, the CS1 wrapper ctx plumbing). THE CHROMA-OWNERSHIP
      RULE (is_chroma_reference common_utils.h:315-320) implemented + the
      4x4-frame NO-chroma / (1,1)-owner asserts. ALL FOUR UV sizes landed
      (S=32 = the TX_64X64 compacted emission domain). The gate: the ecs4
      walk lines (28) + the CS2 cross-check (the U fields == the CS2 U
      fields; the V fields legitimately differ - shared mode). The v3
      SPS/frame-header NOT wired into l6 (rides CS5). The l6 mirror proved
      bit-exact vs the generator (454 assertions); the walk-order inversion
      (luma-after-UV) and the CS2 fixture mismatch (box average vs the CH3
      template 2*(i+j+2)) were both caught by the gate and fixed.
- [x] CS5 color artifacts + per-plane instrument (yuv420p: Y/U/V compared
      separately vs the generator's per-plane recon) LANDED (ecc41b5 +
      CS5b/CS5c): four committed color TUs, verify_decode4.ps1 -Color;
      decode outcomes in the conformance matrix (color16/color32
      full-plane 1:1; color4/color8 luma 1:1, chroma divergent).
- [x] CS6 docs currency (CLOSED 2026-09-27): the whole doc set re-audited
      against the tree at 4b624b9 with two new committed instruments,
      tools/cite_check.ps1 (mechanical file:line citation audit) and
      tools/coverage.ps1 (the portable-C progress metric). Fixed in this
      pass: AGENTS.md's layer map, which had ZERO mentions of chroma /
      uv_mode / uv2y / v3 / color and therefore omitted the entire landed
      CS-series and the v3 color headers; two stale pipeline.cpp
      citations (:758/:909 -> :840/:991); ~15 wrong transform.cpp line
      numbers in cs5c_deltas.md (transform.cpp grew ~700 lines after that
      doc was written); a wrong entropy.cpp partition-context citation;
      three wrong composition.c gate-line citations; the golden_gen
      README's ambiguous reference to the SVT-AV1 build options at lines
      53 and 86 (it means SVT-AV1's own CMakeLists, not the root one - both
      are shorter than line 53, so the bare name resolved to the wrong
      file); and ffmpeg_integration.md's "repo reality" section, which
      claimed "no bitstream, no quantizer, no chroma, no entropy coder" -
      all four of which landed months ago. The C-ABI sketch itself stands
      as the target contract and section 8 still governs.
  Range enumeration: uv alphabet 14/13 by bsize; both components at every
  reachable txs_ctx {4,8,16,32}; chroma txb_skip_ctx {7,8,9}; the has_uv
  parity range (4x4: only (1,1); 8x8+: all); the uv angle-delta gate
  (FALSE at 4x4 luma, per-mode otherwise); dc_sign_ctx {0,1,2} per plane.
1.2 CFL (chroma-from-luma)
- [ ] cfl_alpha symbol surface + the AC-from-luma combine (P: after 1.1)
1.3 TX surface completion
- [ ] TX_MODE_SELECT surface (av1_code_tx_size currently silent at LARGEST)
- [ ] Rectangular transforms 4x8..64x32 (2D cores + scans + the
      TX_CLASS_HORIZ/VERT nz-map rows - token chain currently 2D-only)
- [ ] Rectangular block sizes in l6 (partition tree square-only)
- [ ] Tx types beyond DCT_DCT/ADST_ADST: the full EXT_TX sets
- [ ] Inter only: vartx/recursive transform coding (txfm_partition)
1.4 Syntax breadth
- [ ] Multi-tile streams (tile_size_minus_1, tg_start/tg_end, per-tile CDF)
- [ ] show_existing_frame OBU path (the :369 call site exists)
- [ ] Segmentation enabled-path (map + features)
- [ ] Delta quant (delta_q_present=1 path)
- [ ] QMatrix (using_qmatrix=1 path)
- [ ] Superres (enable_superres=1 path)
- [ ] Film grain parameters
- [ ] Color config breadth: 4:2:0/4:2:2/4:4:4 8-bit first; high bit depth
      is its own phase
1.5 Multi-frame streams + container
- [ ] Stream sequencing: N-frame .obu/.ivf output, keyframe placement
      policy (OURS)
- [ ] IVF container writer
- [ ] Refresh_frame_flags > 0 path (intra-only today)
1.6 Keyframe encoder intelligence
- [ ] Rate estimation core (svt's md_rate_estimation.c as the reference)
- [ ] RD-lite mode decision (SAD + rate cost)
- [ ] Trellis/optimize_b (svt_av1_optimize_b, full_loop.c:1041)
- [ ] Angle-delta search (currently delta=0 fixed)
- [ ] Tx-type search at 4/8/16 (currently DCT_DCT fixed in emission)
- [ ] Adaptive scan (get_scan_order per mode/tx - default scan is policy)
- [ ] Intra partition search (recursive split evaluation)
1.7 Encoder application skeleton (the product surface begins)
- [ ] CLI: input y4m/yuv -> config -> .obu/.ivf out
- [ ] Config surface: q / keyint / mono-color / tile / threads (stubs OK)
- [ ] Settings mapping to SequenceControlSet-equivalent state

------------------------------------------------------------
2. INTER + GOP (the largest single territory jump)
------------------------------------------------------------
2.1 Nonkey frame header
- [ ] The !KEY branches of write_uncompressed_header_obu (:3294-3637)
- [ ] frame_context update/reset paths (refresh_frame_context live)
- [ ] Nonkey y-mode path (entropy_coding.c:1046-1058 - the EC3 deferral)
2.2 Inter entropy surface
- [ ] is_inter + ref frame symbols (single/compound)
- [ ] MV joint/component/drl coding + mv contexts
- [ ] Interintra/masked/warped/motion_field symbols
- [ ] Inter tx-type sets (ext tx full matrix)
2.3 Inter prediction engines (GPU territory)
- [ ] Full-pel ME at scale (SAD exists; the search strategies unported)
- [ ] Subpel interpolation (8-tap interp filters, GPU)
- [ ] Compound prediction (avg/dist-wedge/diffwtd)
- [ ] Global motion (wm params + symbols)
- [ ] Warped motion + OBMC
- [ ] Reference frame management (slot alloc, ref buffer pools, GPU)
2.4 GOP structure
- [ ] GF group / ARF / BWD / overlay structure
- [ ] Keyframe placement + scene-change policy (OURS)
2.5 Recode loops + rate control
- [ ] Encode-recode machinery, cq-level targeting, VBR modes
      (P: a quality axis, not conformance)

------------------------------------------------------------
3. IN-LOOP FILTERS (syntax currently written as zeros)
------------------------------------------------------------
3.1 Deblocking
- [ ] Filter application (edge dirs, levels, deltas) - host port then GPU
- [ ] Filter level/delta search (the encoder side)
3.2 CDEF
- [ ] CDEF application (dir/size/damping, GPU-friendly)
- [ ] CDEF search (dir + strength RDO; header surface when enabled)
3.3 Loop restoration
- [ ] Wiener + SGRProj application + search
- [ ] lr_params header surface (seq enable_restoration currently 0)
Conformance: each filter surface gets the real-decoder content gate
(header + pixels) - the TS/TD pattern, per configuration.

------------------------------------------------------------
4. SCREEN CONTENT & MISC TOOLS
------------------------------------------------------------
(P) Palette mode (symbols + palette tokens + search) - natural-video
    encoders may skip; named trigger: screen-content support.
(P) Intrabc (allow_screen_content_tools=1 path).
(P) 10/12-bit high bit depth (the *_high paths; a separate series).

------------------------------------------------------------
5. GPU THROUGHPUT ARCHITECTURE (correctness -> production speed)
------------------------------------------------------------
5.1 The serial-loop audit (the known bottleneck)
- [ ] Measure: per-block host launches + host decision + host token
      emission dominate (bench baseline 2026-09-10: composite GPU frames
      93-390 ms vs ~0.06 ms/stage single-launch = launch/sync overhead)
- [ ] Batch the kernel launches (block grids per tile, not per block)
- [ ] Streams/CUDA graphs for the per-stage pipelines
- [ ] Memory pooling (DeviceBuffer churn elimination)
- [ ] NVML clock locking for stable measurement (BM3 first candidate)
5.2 The hard problem: entropy coding on GPU
- [ ] Strategy paper FIRST: adaptive arithmetic coding is serial; options
      = (a) host emission with GPU everything-else (current), (b)
      tile-parallel CDF partitioning, (c) two-pass approximate/range
      splitting (research-grade). Decide with measurements, not vibes.
- [ ] Mode-decision GPU migration (D2 policy is embarrassingly parallel;
      SAD kernels exist)
5.3 Performance targets (the production bar)
- [ ] Baseline: SVT-AV1 CPU (preset-matched) fps/quality on this machine
- [ ] Target definition per axis (fps at q, quality delta vs SVT)
- [ ] tools/bench: extend to end-to-end fps + wall-time, per-phase
      breakdown, before/after gates for every perf slice (standing rule)
5.4 Robustness at speed
- [ ] Device-loss/OOM recovery paths, multi-GPU stance (single for now)

------------------------------------------------------------
6. CONFORMANCE & QUALITY VALIDATION
------------------------------------------------------------
6.1 Conformance
- [ ] Settings matrix decode-verified (q x size x mono/color x lossless/
      lossy x tiles) through aomdec + dav1d + ffmpeg - content-1:1 per
      cell where the generator has recon goldens; decode-acceptance
      everywhere
- [ ] MD5/CRC per stream regression corpus
6.2 Quality
- [ ] PSNR/SSIM harness vs source (frame MSE exists; extend + aggregate)
- [ ] VMAF comparison vs SVT-encoded references at matched settings
      (P until the encoder matures)
6.3 Fuzzing/stress
- [ ] Bounds stress on every l6/l7/l8 input surface
- [ ] OOM/device-loss injection; multi-run leak checks
- [ ] Random-config fuzz encoder (settings x fixture matrix, decode-gated)

------------------------------------------------------------
7. ROBUSTNESS, PACKAGING, OPS
------------------------------------------------------------
- [ ] Error-handling sweep: CUDA error propagation, fail-loud surfaces
- [ ] CI pipeline: build matrix + 9-target suite + GATE + ladder + bench
      regression thresholds on every commit
- [ ] Versioning + CHANGELOG + release tagging
- [ ] Install story (CMake install/package; CUDA runtime deps documented)
- [ ] Attribution/license review (BSD-2 + AOM patent license notices)
- [ ] Docs set completion (user guide for the CLI; API reference)

------------------------------------------------------------
8. DEFINITION OF DONE - the gate before the ffmpeg blueprint
------------------------------------------------------------
The ffmpeg blueprint may start when ALL of the following hold:
- [ ] Keyframes complete: chroma + color, all sizes, lossless+lossy,
      multi-frame streams, CLI app produces valid .ivf/.obu from y4m
- [ ] Conformance matrix green: every settings cell decodes in aomdec +
      dav1d + ffmpeg; content-1:1 wherever goldens exist
- [ ] Performance measured honestly: bench reports end-to-end throughput;
      the GPU architecture phase has landed its first wave (batched
      launches minimum) OR the gap is quantified and accepted
- [ ] Suite/GATE/ladder green at HEAD; zero named deviations unresolved
      without a court-ratified trigger
- [ ] Robustness: fuzz/stress passes; error paths fail loud and clean
- [ ] Packaging: install + version + CI all real
Inter (phase 2), loop filters (3), and screen content (4) MAY trail the
ffmpeg blueprint as long as the stream surface stays spec-valid for the
shipped feature set (mono/color intra streams).

------------------------------------------------------------
9. KNOWN DEVIATIONS & RISK REGISTER (live pointers)
------------------------------------------------------------
- docs/provenance.md deviations list (#9 TS1 RESOLVED as the TD4/TD5 root
  cause; #10 the drive self-consistency traps; #11 the 8px alignment;
  #12 FS5d OPEN)
- AomReader struct-shape deviation (l7 vs vendored layout - harmless in
  self-consistent use; the TD2 harness crash taught the exchange rule)
- 4096-facade vs 1024 token domain at 64x64 (FS5d, OPEN)
- The FI DC-deciding fixture (OPEN)
- **LANDED 2026-09-28 at `5ce0224` - the chroma bottom-left FIX. c8 is
  content-1:1 on all three planes; c4 retains a SECOND, V-only defect, and
  that is the finding, not a failed prediction.** Every number below is read
  off one run, not transcribed from a commit message.
  **RUN:** ctest `-C Release` **9/9 passed, 270.41 s** at `5ce0224`.
  **GATE:** **421/421 lines, diff 0, `expected_primitives.txt` 884,953 B**
  (0 CR, 0 non-ASCII), byte-identical to the generator output. **Regen: 13
  gate lines changed, ALL chroma-side** - `ecs4{4,8}_{bytes,modes,eobs,
  coeffs,recon_u,recon_v}` plus `tu_color4` 201 -> 198 B and `tu_color8`
  139 -> 137 B. `ecs44_recon_y` and `ecs48_recon_y` changed in **0** lines,
  and no `ecs416_*` or `ecs432_*` line changed at all.
  **PREDICTIONS, read off the run:**

  | prediction | measured | verdict |
  |---|---|---|
  | c8 U 165 -> 0 divergent | c8 U 1024/1024 content-1:1 | HELD |
  | c8 V 205 -> 0 divergent | c8 V 1024/1024 content-1:1 | HELD |
  | c4 U 244 -> 0 divergent | c4 U 1024/1024 content-1:1 | HELD |
  | c4 V 287 -> 0 divergent | c4 V **43/1024 diverge, first at 647** | **DID NOT HOLD - the finding** |
  | 0 divergent on all 3 planes, both | c8 yes; **c4 no, V only** | partial |
  | `tu_color16` / `tu_color32` byte-identical | 133 B / 165 B, unchanged | HELD, and derived not forecast |
  | luma gate lines unchanged | `ecs44_recon_y` / `ecs48_recon_y` 0 changed lines | HELD |

  **THE c4 RESIDUAL, in full, because the summary line would bury it.** Y is
  4096/4096 exact and U is 1024/1024 exact; **V diverges on 43 of 1024
  bytes, first at 647**, across exactly four 4x4 UV leaves - leaf 41 (row 5,
  col 1) 8 bytes, leaf 42 (row 5, col 2) 16, leaf 48 (row 6, col 0) 3, leaf
  56 (row 7, col 0) 16. Recomputed from the decoded plane against
    `ecs44_recon_v`, not read from a commit. **The per-leaf counts are 8, 16,
    3, 16 - three of the four are PARTIAL, not whole-leaf.** A whole-TU
    mispredict would give 16 on all four; a partial pattern points at the
    gather's extent or the Z-order predicate rather than at a whole block being
    predicted wrong. That is the next diagnostic, and it is NOT yet run.
    **This is filed as the finding per MUST SAY OUT LOUD #2, not as a caveat and
    not by rounding the conformance to "3 of 4".** The brief's `1` and `2` for
    c4 did not follow, and the mechanism is therefore not the whole story at
      c4 - which is exactly what the brief said would happen, and exactly the
    case in which the expectation must NOT be adjusted to match.

    **THE SHAPE BOUND ON c4's RESIDUAL - a bound on what the mechanism must
    look like, NOT a mechanism, and deliberately not reconciled with anything
    yet.** Re-measured 2026-09-28 by decoding
    `structural_keyframe_color4.obu` and differencing the V plane against
    `ecs44_recon_v` (43 differing bytes, first at 647):

      leaf 41 (UV r5,c1)   8 of 16 bytes
      leaf 42 (UV r5,c2)  16 of 16
      leaf 48 (UV r6,c0)   3 of 16
      leaf 56 (UV r7,c0)  16 of 16

    The arithmetic bound, and it needs no theory to hold: c4's UV plane is
    32x32 with `uvB = 32/8 = 4`, so **every leaf is a 4x4 block of 16
    bytes** (`ecs44_modes` is 192 tokens = 64 leaves at 3 per leaf, grid 8).
    A whole-block mispredict on all four leaves would therefore be
    **4 x 16 = 64 bytes**. Measured is **43**, so **at least three of the
    four leaves are PARTIALLY wrong** and at least one is wholly wrong. That
    is the whole content of the bound: the residual is not four independently
    mispredicted blocks.

    WHAT IT CONSTRAINS, stated as constraints and nothing more: whatever
    accounts for c4's V residual must be capable of affecting a PROPER
    SUBSET of one 4x4 block's 16 bytes, and of leaving a neighbouring 4x4
    block wholly correct. A mechanism that predicts a block all-or-nothing
    cannot produce this shape. That is consistent with a wrong gather EXTENT
    (wrong pixel count, or the right pixels in the wrong place) or with a
    wrong Z-order predicate, and it is also consistent with several other
    causes - which is exactly why this is filed as a bound and not as a
    mechanism. **The next diagnostic has not been run and no cause is
    asserted here.**

    **AND ONE RECONCILIATION IS RECORDED AS NOT REPRODUCING, rather than
    written as fact.** A briefing proposed that this shape is "not
    reconcilable with the sites the 8 bounded bottom-left literals account
    for, those being 4x4 and 8x4 chroma in the 4:2:0 walk, with leaf 56's V
    being 8x8". Against the tree as it stands: **`8x4` is not a size this
    walk produces** - every UV block in it is square, `uvB = lumaB/2` - and
    **leaf 56 is a 4x4 block, not 8x8**: it is UV (row 7, col 0) on the 8x8
    grid, so it holds 16 bytes, and all 16 of them differ. I also could not
    reproduce a count of 8: at c4 the bounded leaves are 2 (D203 AND
    below-left available), which is 4 gather sites across the U and V planes
    and 16 gathered pixels. So the claim is recorded here as UNVERIFIED
    rather than as the explanation, because the one part of it that the tree
    does check - leaf 56's geometry - is contradicted. If someone holds the
    actual derivation, it needs to be re-derived against the tree before it
    is allowed to explain anything; until then the shape bound stands alone,
    which is the same discipline as BOUNDED versus INERT: a
    measurement-derived constraint kept separate from the claim it constrains.

- The chroma bottom-left FIX (SPEC, retained below as the record of what the
  slice was built against). **This entry is the brief. If you are
  reconstructing the task from this repo rather than from whoever handed it
  to you, THIS is the spec - read it before slicing.**

  **State (measured, not inferred).** CS7 phases (a)/(b)/(c)/(c2) CONFIRMED
  the mechanism at BOTH c8 and c4: our gathered left columns match dav1d's
  as a bit-exact reversal, so only the EXTENSION diverges. The c16 control
  passed because dav1d's UNSET branch and ours are the SAME RULE, not the
  same accident: dav1d does `pixel_set(left - sz, left[0], sz)`
  (ipred_prepare_tmpl.c:157), and by :138 `left[0]` is the bottom-most
  left-column entry, i.e. a repeat of the LAST AVAILABLE EDGE SAMPLE -
  which is what our `cleft[S-1]` memset does. The 64 observed at c16 was
  one instance of that rule, not a corner special case, so the agreement
  is structural across content and geometry, not a flat-region
  coincidence.

  **THE FIX, mechanically.** Two sites, no new condition, no signature
  change, no threading:
  (1) the generator CS4 chroma gather in `tools/golden_gen/composition.c`:
      `cleft[S + j] = the byte at (uv_x - 1, uv_y + S + j)`, gated on the
      availability the caller supplies;
    (2) the l6 UV chain: the CS4 walk's chroma call site,
        pipeline.cpp:2093 - which at the time of this brief was a literal `0`
        in the `nBottomLeftPx` position of the builder call at :2090, and is
        now a `bottomLeftPx` call, LANDED in 5ce0224. The luma side was
        already done and is the shape it copied: the RULE is the named
        function `pipeline::bottomLeftPx`
        (DEFINED pipeline.cpp:2459-2463, DECLARED pipeline.h:246), the luma
        CALL SITE is pipeline.cpp:1884, the CHROMA call site CS7 changed is
        pipeline.cpp:2093, and the luma gather is pipeline.cpp:1892-1894
        (RT10b). DO NOT ALSO "FIX" pipeline.cpp:2182 - that is
        `encodeFrameReconChroma16x16`, one of the four standalone 16x16-UV
        compositions, whose grid is `src.width()/16 x src.height()/16`
        (pipeline.cpp:2170-2171), so on the committed 16x16 UV fixture the
        grid is 1, no below-left neighbour exists, and its `nBottomLeftPx = 0`
        is CORRECT BY GEOMETRY rather than by omission. Sibling calls :2266,
        :2321, :2394, same fixed shape. :2182 becomes a real hole only if a
        caller passes a UV plane larger than one block; no committed caller
        does, so the fix should leave it alone or put the rule behind the same
        guard rather than special-casing the literal.
  The builder keeps ownership of the USE predicate (the RT10b policy): the
  caller supplies AVAILABILITY only, because `build_intra_predictors`
  mutates one variable in sequence (seed from NEED_BOTTOMLEFT, force 0 for
  filter-intra, then OVERWRITE with `is_dr_mode && p_angle > 180`) and
  duplicating that predicate at the call site is the trap RT10b already
  paid for once.

  **SLICE 1 OPENS WITH THE INVARIANCE TEST, AND THAT CONSTRAINT IS THE
  POINT.** The per-geometry gather count must be DERIVED MECHANICALLY from
  the zpos map and the committed gate modes - **never hand-transcribe a leaf
  count, and never type 64 expected values into a test.** Concretely, for
  each geometry S in {4, 8, 16, 32} the test must:
    - rebuild `zord` / `zPos` from the Z-order construction
      (`zorderLeaves`, pipeline.cpp:1772-1782; the inverse map at
      pipeline.cpp:1851-1852) rather than hardcoding a Z-order;
    - read the chroma-referenced leaves out of the committed gate line
      `ecs4S_modes` (three values per leaf - luma, U, V; `-1` is the
      generator's non-referenced sentinel, per the ecs4 gate note in
      composition.c:6113), rather than assuming the leaf set;
      - apply the SAME gate rule the implementation uses - i.e. CALL
        `pipeline::bottomLeftPx` itself (pipeline.cpp:2459-2463), which is the
        whole point of the be67d6f extraction: the expectation is produced by
        the same named rule the code runs, not by a second transcription of
        the expression. To keep the test from being tautological, ALSO
        restate the rule INDEPENDENTLY as
        (`bx > 0 && by + 1 < grid && zPos[(by+1)*grid + (bx-1)] < zPos[b]`)
        and assert the two agree per leaf;
      - assert the implementation's gathered-pixel total equals that derived
        value, per geometry and per plane.
  **KNOWN FIXTURE LIMIT, state it rather than discover it as a surprise:**
  the `-1` sentinel does NOT occur in any committed ecs4 gate line. In all
  four, `bwMi = lumaB/4` is even (uvB 4/8/16/32 -> lumaB 8/16/32/64 ->
  bwMi 2/4/8/16), so the `!(bwMi & 0x01)` disjunct of the chroma-ownership
  rule (pipeline.cpp:1860-1861) is unconditionally true and EVERY leaf is
  chroma-referenced. Consequences, both real: (a) in these fixtures the
  modes line is a CROSS-CHECK on the leaf set, not a filter over it, so
  the derivation must still read it rather than assume all-referenced -
  the two agree today and would stop agreeing the moment lumaB became odd;
  and (b) the non-referenced branch is UNTESTED by these fixtures. Do not
  claim it covered. If the fix touches that branch, it needs its own
  fixture, and the weak-fixture rule applies to it like any other.
  **SECOND, INDEPENDENT LIMIT - the non-vacuity guard must be
  geometry-aware, and a uniform `require(wantTotal > 0)` is WRONG.** The
  rule fires only at grid >= 4 (see the derivation table under MUST SAY OUT
  LOUD #2), so at S=16 (grid 2) and S=32 (grid 1) the correct total is
  ZERO and a blanket positivity assertion is red for a structural reason,
  not a product reason. Scope the guard: require `wantTotal > 0` for S in
  {4, 8} (where it is 8 and 8), and for S in {16, 32} require
  `wantTotal == 0` TOGETHER WITH the structural reason asserted too - e.g.
  that `availLeaves == 0` because grid <= 2 admits no below-left leaf that
  precedes its depender in the visit order. Asserting the zero WITHOUT the
  reason re-introduces exactly the problem this entry exists to prevent: a
  green test that would stay green if the rule were broken at those two
  geometries. The zero is a claim; make the test state why it is true.
  WHY THIS IS A CONSTRAINT AND NOT A NICKEL: a hand-written leaf count is
  indistinguishable from a correct one to the reader, it survives every
  future change to the zpos map by continuing to assert a stale number, and
  the suite stays GREEN on a wrong expectation. That is the same failure
  family as the lesson-register's coincidence-passing fixtures (FS5a) and the
  weak-fixture rule, applied to a DESIGN CONSTRAINT rather than to a
  measurement - and the mitigation is identical: put the constraint where
  the next reader will actually look, which is why it lives here and not
  only in a session exchange. If the derivation proves awkward, fix the
  derivation's ergonomics; do NOT fall back to literals.

  **MUST SAY OUT LOUD #1 - the `y + th >= h` equivalence.** The fix's diff
  will contain no `y + th >= h`, and the next reader will diff our code
  against dav1d, see the absence, and assume a bug. It is not missing; it
  is IMPLIED. dav1d gates the bottom-left extension on
  `have_bottomleft = (!have_left || y + th >= h) ? 0 : ...` and additionally
  clamps with `imin(sz, (h - y - th) << 2)`. DERIVATION: for a WHOLE-LEAF
  block in a LEAF-ALIGNED frame, "touches the plane bottom" is equivalent to
    "the leaf is on the last row", which is equivalent to our `by + 1 < grid`
    being false - and that is already carried by the rule at
    pipeline.cpp:2459-2463, in its `by + 1 >= grid` early return
    (pipeline.cpp:2460). So both of
  dav1d's terms are already satisfied and the fix adds no condition. THE
  ONE ASSUMPTION THAT WOULD MAKE IT LIVE: a frame whose dimensions are not
  leaf-aligned, or a partial block. Our walk produces neither - every
  geometry is `width / lumaB` blocks exactly - so the equivalence is
  structural here, not fixture-dependent. Also note the Plane is padded
  (stride = width + 2*padding, rows = height + 2*padding), so a
  hypothetical overrun would be a padded READ rather than a memory-safety
  violation; this was never an emergency and no artifact was ever at risk.
  State this equivalence explicitly in the fix commit even though the diff
  does not require it - that is the whole reason it is written down.

    **MUST SAY OUT LOUD #2 - the falsifier is the finding.** Predictions,
    to be TESTED and not assumed: c8 U 165 -> 0 divergent leaves and V 205 ->
    0; c4 U 244 -> 0 and V 287 -> 0; the per-plane map at 0 divergent leaves
    on all three planes for both; `tu_color16` and `tu_color32` byte-identical;
    the luma gate lines unchanged. **If c4 does not follow, that IS the
    finding - report it; do NOT adjust the expectation.** A prediction that
    gets quietly edited to match its result has produced zero information
    while looking exactly like a passing test, and a hand-tuned expectation is
    the one outcome this whole entry exists to prevent.

    **THE STRUCTURAL BOUND, derived from the Z-order alone - so it needs
    no fixture to establish, and only four committed fixtures sample it.**
    `nAvail` = the number of leaves whose below-left neighbour both EXISTS and
    precedes them in the decode_partition visit order. It is a pure function
    of the grid, obtained by enumerating `zorderLeaves` for that grid and
    counting `bx > 0 && by+1 < grid && zPos[(by+1)*grid+(bx-1)] < zPos[b]`.
    Measured 2026-09-28 by that enumeration, with no reference to any gate
    line:

    | grid | leaves | nAvail | state |
    |---|---|---|---|
    | 1 | 1 | 0 | INERT |
    | 2 | 4 | 0 | INERT |
    | 4 | 16 | 2 | BOUNDED |
    | 8 | 64 | 14 | BOUNDED |
    | 16 | 256 | 70 | BOUNDED |
    | 32 | 1024 | 310 | BOUNDED |

    NO CLOSED FORM IS CLAIMED. Six measured points, and the enumeration is
    cheap enough that a wider configuration space should be enumerated rather
    than fitted - do not extrapolate the table.

    **THE CROSS-CHECK that makes the table trustworthy rather than merely
    plausible: grid 8 -> 14 and grid 4 -> 2 are exactly the `nAvail` values
    the committed chroma fixtures report.** The CS4 invariance test prints
    them on success (`RT11 derive S=4 ... nAvail=14`, `S=8 ... nAvail=2`), and
    they were independently derived here from the Z-order with no reference
    to that test. Two derivations, same two numbers. That is the property
    worth having: the table PREDICTS the fixtures rather than being read off
    them, which is what makes it safe to apply to a grid no fixture covers.

    **BOUNDED versus INERT, in those words, because the difference is the
    whole point.** A grid configuration is BOUNDED when the rule CAN fire
    and its total is finite and non-zero - grid >= 4, so nAvail is 2 at grid
    4 and grows. A grid configuration is INERT when the rule CANNOT fire at
    all, so there is nothing to bound and no total to predict - grid <= 2.
    An INERT configuration is not a configuration with a zero bound; it is a
    configuration where the question does not arise, because no leaf has an
    available below-left. Reporting "0" for an inert grid and "0" for a
    bounded grid that happens to gather nothing would conflate a structural
    fact with a content-dependent one.

    **16-CLOSED / 8-BOUNDED, the accounting, with the shorthand unpacked
    because it is not self-explanatory.** "16" and "8" denote the UV BLOCK
    SIZE, and the grid follows from the plane: the bcf fixtures compose over
    a 32x32 UV plane, so 16x16 UV blocks give grid 32/16 = 2 (CLOSED, nAvail
    0) and 8x8 UV blocks give grid 32/8 = 4 (BOUNDED, nAvail 2).

    **AND THE GRID-CONFIGURATION CAVEAT, which is the trap: a bound is a
    statement about a GRID CONFIGURATION, not about a committed fixture, and
    the grid is a function of BOTH the block size and the plane size.** The
    16/8 shorthand above is valid ONLY because the bcf plane is 32x32. The
    same 16x16 UV block on a 64x64 UV plane would be grid 4 and BOUNDED, not
    CLOSED. So "16-closed" is not a fact about 16x16 blocks in general; it is
    a fact about 16x16 blocks on this plane. Anyone reusing the shorthand on
    a different plane, or reading it as a claim about what a committed .obu
    contains, will get it wrong. What a FIXTURE contributes is only the
    per-leaf MODE filter on top of the bound - never the bound itself.

    **FORMAT TRAP, `bcf16_modes` is not an `ecs4` line - two differences,
    both silent.** In `tools/golden_gen/expected_primitives.txt:187` the line
    is `bcf16_modes: 1 7 2 2` - note the COLON after the name, where the
    ecs4 lines use a bare SPACE (`ecs44_modes 1 1 1 7 7 7 ...`). And the
    ARITY differs: bcf16 is ONE value per leaf, ecs4 is THREE (luma, U, V).
    So a parser written for the ecs4 shape mis-reads bcf16 twice over - the
    colon breaks a space-split, and the three-per-leaf assumption is wrong by
    a factor of three. The `1 7 2 2` are the per-leaf modes of the 2x2 grid
    = 4 leaves (src/l6_pipeline/tests/test_pipeline.cpp:4544, which declares
    `goldenModes[4] = {1, 7, 2, 2}` against a 32x32 UV plane). **AND THE
    GRID-2 REASON, which is why this fixture is the closed case and not a
    live one:** bcf16 is grid 2, and at grid 2 the decode_partition DFS
    visit order coincides with raster order, so leaf (0,1)'s below-left leaf
    (1,0) is always visited AFTER it and can never see reconstructed data.
      A reader who assumes bcf16 exercises the gather will write a test that
      passes without the gather running.

    **THE c16/c32 HALF IS ALREADY DERIVED, NOT A HOPE - do not report it as
    "a prediction that held".** Measured 2026-09-27 from the committed gate
    lines, by rebuilding the Z-order and applying the rule at
    pipeline.cpp:2459-2463: the bottom-left rule fires ONLY at grid >= 4. The
    table above extends that statement to every grid; this one adds the
    per-fixture content on top - the D203 column is the MODE filter, which a
    bound alone does not and cannot predict:

    | S | grid | leaves | below-left available | D203 | D203+avail | wantTotal |
    |---|---|---|---|---|---|---|
    | 4 | 8 | 64 | 14 | 11 | 2 | 8 |
    | 8 | 4 | 16 | 2 | 3 | 1 | 8 |
    | 16 | 2 | 4 | **0** | 1 | 0 | 0 |
    | 32 | 1 | 1 | **0** | 0 | 0 | 0 |

    WHY, structurally: at grid 2 the decode_partition DFS visit order
    coincides with raster order, so leaf (by=0,bx=1)'s below-left leaf
    (by=1,bx=0) is ALWAYS visited after it - the extension can never see
    reconstructed data; at grid 1 there is no below-left neighbour at all.
    So `tu_color16` and `tu_color32` CANNOT change under this fix. That is a
    derivation with a mechanism, and when the fix lands it should be recorded
    as one - "the rule provably does not fire at grid <= 2, therefore those
    two artifacts are unchanged BY CONSTRUCTION" - not filed alongside c8/c4
    as though four co-equal predictions happened to come true. It also means
    a c16/c32 diff is a REAL FAILURE SIGNAL, not a surprise.
    (Derived by recomputing from `tools/golden_gen/expected_primitives.txt`;
    the grid=2 row was hand-checked against the arithmetic, and grid=4 and
    grid=8 against an independent Z-order expansion. Use `int[]` arrays for
    the recomputation - a hashtable lookup that misses returns `$null`, which
    compares as less-than zero and silently inflates the counts.)


    **DOC OBLIGATIONS OWED BY THE FIX - do all three in the SAME session as
    the fix, not later.** They are owed the moment the fix lands, and each
    one leaves a tracked document stating something false if it is skipped,
    so a fix committed without them is an incomplete fix. Recorded here
    rather than in a session exchange, for the reason in the lesson register
    ("a constraint that lives only in a session exchange has not been
    recorded").
      1. **AGENTS.md, the l6 layer entry - DONE, and re-done 2026-09-28 after
         the fix, because the rule moved under it.** It first said "The
         bottom-left edge extension is gathered under the Z-order gate at
         pipeline.cpp:1885 (RT10b)", which was wrong twice over: it described
         the LUMA gather only, and it cited 1885 as "the gate" when the
         be67d6f extraction had already moved the rule out of the walk. It
         now names the DECLARATION (pipeline.h:246) and the DEFINITION
         (pipeline.cpp:2459-2463) as different things, the luma CALL SITE
         (pipeline.cpp:1884), the CHROMA call site CS7 changed
         (pipeline.cpp:2093), the luma gather (pipeline.cpp:1892-1894), and
         the four standalone compositions where zero is CORRECT BY GEOMETRY
         (:2221, :2271, :2346, :2403). Re-run `tools/cite_check.ps1` after
         any further l6 change: this document moves with the code, and the
         rule's definition has already slid from :2420 to :2459 in one
         release.
         and these three line numbers have each been wrong at least once.
    2. **todo.md section 9, this entry: OPEN -> LANDED**, with the MEASURED
       before/after for all four predictions above (not "as predicted" - the
       numbers), plus the gate line count and the full-suite result FROM THE
       RUN THAT PRODUCED THEM, quoted with that run's date. The repo's
       measured-values rule is the point: a LANDED marker with no numbers
       attached is indistinguishable from a marker written in advance.
       **Split the two halves in the writeup.** c8 and c4 are genuine
       predictions and must be reported as measured results, because they
       were genuinely at risk. c16/c32 are NOT predictions by then - they
       are already derived (see the derivation table above: the rule does
       not fire at grid <= 2), so recording them as "the prediction held"
       would credit a result to a forecast that never carried information,
       and would hide the stronger claim that is actually available, namely
       that those two artifacts are unchanged BY CONSTRUCTION.
    3. **docs/decode_conformance.md, the colour conformance matrix.** It
       currently records c4/c8 chroma divergent. Change it to whatever is
       actually measured, per plane. **If c4 does not follow the prediction,
       that asymmetry goes into the matrix AS THE FINDING** - the c8-fixed /
       c4-still-divergent row, with the mechanism still unexplained for c4 -
       not as a footnote, not as a caveat sentence, and not by quietly
       rounding the summary line to "2/4 still". A conformance matrix that
       hides an asymmetry is worse than one that shows it, because the matrix
       is the artefact a future session trusts first.

- od_ec_dec_bits_ declared-undefined in the pinned tree (named, no
  consumer)
- Bench baselines are clock-noise-sensitive without NVML lock (BM3)
- sm_61 target vs sm_86 dev box: the measurement split is documented; no
  perf claims without the harness
- PROCESS (new, 2026-09-23): untracked files are NEVER strays - deletion
    requires user/court confirmation; the court commits its authored docs
    immediately (the todo.md deletion incident)
  - PROCESS - the throwaway dav1d tree, **RETIRED 2026-09-29.** This entry
    previously read "RE-MEASURED 2026-09-28" and offered TWO exits, and it
    resolved to (b): the aom route won, so exit (b) is taken and the tree is
    retired rather than parked. **SUPERSEDED - do not treat the tree below as
    available; it lives in `%TEMP%`, and `%TEMP%` does not survive a reboot.
    The standing instrument for decoder acceptance is
    `tools/verify_decode4.ps1`, and the arbiter is whatever independent
    decoder it drives (ffmpeg's `libaom-av1` is available on this box).** The
    description is kept because the tree, the baseline copies and the two
    diffs were the evidence for a claim about a DIFFERENT thing - what RT10's
    instrumentation had touched - and that is settled. The historical record:
    the instrument lived at `%TEMP%\dav1d_td4` - a dav1d extract with **no
    `.git`** (15 top-level entries, 408 files), so its history is only what
    the patches record. Building a
    decoder from an arbiter tree for instrumentation is fine; nothing is
    extracted into the port, and the tree is NOT a substitute for the
    `SKIP`/in-repo rule.
    **WHERE THE BASELINE COPIES LIVED:** `%TEMP%\opencode\dav1d_baseline\`,
    three files - `ipred_tmpl.c` (871 L), `recon_tmpl.c` (2387 L),
    `itx_tmpl.c` (311 L). **WHERE THE DIFFS LIVED:** one level UP, in
    `%TEMP%\opencode\` - `rt12_recon_tmpl.c.diff` (8,898 B) and
    `rt12_itx_tmpl.c.diff` (5,332 B), plus the `rt12_*.err` / `rt12_c4.yuv`
    captures alongside them. Both diffs are in **`fc` format**, not unified
    diff: they begin "Comparing files ...\dav1d_baseline\<f> and
    ...\dav1d_td4\src\<f>" and use `*****` separators. A `^+`/`^-` line count
    over them returns ZERO and looks like an empty diff - that is the format,
    not a missing patch.
    **WHAT WAS ACTUALLY PATCHED, measured by line delta against the baseline
    copies:** `itx_tmpl.c` **+55 L** (311 -> 366, 0 -> 15 RT markers) and
    `recon_tmpl.c` **+104 L** (2387 -> 2491, 4 -> 38 RT markers), both pure
    additions with 0 lines removed - **+159 L across two files.** And
    `ipred_tmpl.c` is **delta 0**: it is byte-identical to the baseline copy
    and already carries 12 RT markers in BOTH. So the TD4/RT10 `ipred` patch
    is folded into the BASELINE copy rather than carried as a live
    difference - the baseline is not a pristine upstream, it is upstream plus
    the earlier patch. Do not read `ipred_tmpl.c delta 0` as "the ipred
    instrumentation was removed".
    **THE RT10 patches were deliberately LEFT IN PLACE**, per the standing
    precedent that an instrumented decoder stays instrumented across
    slices: `recon_tmpl.c` still carried `RT10 step 2 (throwaway
    instrument): arm the ipred_z3 trace`, `extern int dav1d_rt10_trace`
    and the `RT10CALL` print. Removing them would have meant re-patching
    before every use, and a tree that must be re-patched is a tree whose
    state at the moment of measurement is not the state anyone read.
    **WHY IT IS RETIRED RATHER THAN KEPT, re-measured 2026-09-29** (the brief
    proposed this outcome and the grounds are partly wrong, so the numbers
    here are mine):
    - **It is an INSTRUMENTED decoder, not upstream - the sharpest ground,
      and stronger than "the numbers are off".** Measured: `dav1d_td4/src/
      recon_tmpl.c` carries **66** RT-marked lines against the baseline copy's
      **22**. A patched arbiter is a different decoder wearing upstream's
      name. The evidence contract in AGENTS.md now says the subject matter
      of a reference measurement is the decoder, never a build of it, and
      this tree is the case that proves the rule.
    - **It diverges from an independent implementation on a committed
      artifact.** Both decode `structural_keyframe_color16.obu`; luma only,
      first 4096 B: the throwaway gives **max 158, mean 83.8**, ffmpeg's
      `libaom-av1` gives **max 93, mean 24.1**. **The brief's figures for
      this (Y max 255, mean 233.5 vs 94/24.0) do not reproduce and are NOT
      used** - they appear in no tracked doc, only in SVG path data inside
      `third_party/SVT-AV1/Docs/img/`, so they were either a different
      artifact or a different build. Written down as measured, and the
      direction of the finding is unchanged: a 3.5x mean gap on the luma
      plane.
    - **It was never citable anyway.** `%TEMP%` is not the working
      directory, and that is the case-1 failure the contract now names: a
      measurement living only in `%TEMP%` is a recollection.
    - **It is not needed.** ffmpeg exposes `libaom-av1` (and `libdav1d`, and
      `av1`) on this box, so the acceptance check has a real arbiter that is
      not this project's own code. **A wrong arbiter that looks like
      evidence is the specific harm** - worse than no arbiter, because it
      produces confident numbers.
    **THE DELETION IS NOT PENDING. IT IS DECIDED, AND THE TREE STAYS.**
    The court has ruled on it: the throwaway is **RETIRED and UNREACHABLE**,
    and that is the correct end state, not a cleanup still owed. The
    rt12_*.diff files stay too, because they are the only record of the RT10
    instrumentation delta and removing them would make that delta
    unrecoverable rather than merely hard to reach. "Unreachable
    documentation" is the right description of a %TEMP% tree: it is
    documentation nobody can rely on, which is why the contract treats it as
    non-citable, and it is why the arbiter question is settled rather than
    open. **Do not read this entry as a to-do.**

    **THE BRIEFED PATH WAS WRONG, and that is the finding - the FOURTH
    instance in this arc of an operation that looks like it worked.** Measured
    2026-09-29:

      briefed  %TEMP%\opencode\dav1d_td4   DOES NOT EXIST
      actual   %TEMP%\dav1d_td4             408 files, 15 top-level
                                            entries, ONE LEVEL UP, with a
                                            built build_td4\tools\dav1d.exe
                                            (645,120 B)

    A delete aimed at the briefed path removes nothing, returns success, and
    reports the tree gone. Same shape as the `1>` redirect: **the command's
    failure mode is to look like success, and nothing in the output
    distinguishes the two.** The escape is the same in both cases - do not
    trust the exit, measure the subject before and after (`Test-Path`, and a
    file count). A silently-successful destructive operation is worse than a
    failing one, because it removes the evidence that it never ran.
  - **THIRD-PARTY CHECKOUTS ARE NOT GIT-VENDORED, and that is the evidence
    contract's own case 2, recorded here so the rule is not learned twice.**
    Measured 2026-09-29: `third_party/dav1d/` and `third_party/aom/` are
    BOTH in `.gitignore` (lines 9 and 8) and `git ls-files` returns **0** for
    each. So "vendored" is ambiguous across exactly two senses, and only one
    is true of each tree: **present in the working directory** (dav1d 359
    files, no `.git` of its own, `dav1d_api_version_major` 7 / meson
    `1.5.4`; aom 1522 files excluding its own `.git`, which it HAS, HEAD
    `208e9c49db`) versus **tracked by git** (neither). Consequence, and it
    is the one that will be re-derived wrongly: both survive a reboot and
    **neither survives a fresh clone**, so a citation into either is
    checkable only while this working directory stands. Say which sense you
    mean. "dav1d is not vendored, therefore do not use it" is the error - it
    is not vendored in the git sense, and it IS the resolution target for
    the dav1d-side citations. The retired throwaway above is identical in
    KIND to these two, differing only in being non-durable as well as
    patched. Full statement in AGENTS.md, "The evidence contract".
  - Debug-config /RTC finding (2026-09-24, RESOLVED c462872): av1_l4_intra_tests
  in Debug aborted with Run-Time Check Failure #2 (stack around 'buf'
  corrupted). ROOT CAUSE: three test fixtures passed p = buf + 1 to
  upsampleIntraEdge, but the function writes p[-2] (the SVT reference
  svt_av1_upsample_intra_edge_c:53 - the port exact, the library NOT the
  bug); p = buf + 1 gave only 1 byte of prefix, the p[-2] write landed at
  buf[-1]. THE FIX: three test-fixture buffer enlargements (buf[16] -> [18],
  raw[18] -> [20], p adjusted +2; the third call site verified safe).
  Debug suite 119/119 clean; Release 9/9 unchanged (the /RTC check is
  Debug-only). LESSON: the SVT C functions that write before the pointer
  require the test fixtures to provide the prefix space; the Release-only
  suite cannot catch this class (no /RTC guards); the Debug build is the
  canary.
