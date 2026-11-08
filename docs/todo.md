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
  * **A CLAIM WHOSE ARITHMETIC DOES NOT FOLLOW FROM THE TABLE IT QUOTES
    IS WITHDRAWN, and the shape is worth more than the story.** RT17's
    D45 diagnosis is WITHDRAWN, and the register carries the shape rather
    than the incident because the failure was caught by re-reading one line
    - the cheapest instrument in the repo, and one that runs while thinking.
    The premise was that `kModeToAngle[3]` is 0, so `pAngle` is 0. The table
    at `src/l4_intra/intra.cpp:476` reads
    `{0, 90, 180, 45, 135, 113, 157, 203, 67, 0, 0, 0, 0}` - **index 3 is
    45**, not 0. pAngle is therefore 45, which lands in the first dispatch
    branch at `intra.cpp:796` (`angle > 0 && angle < 90`). The 0 the report
    read is at **index 0**. The same report also cited `intra.cpp:3` for a
    table that lives at :476, so the citation itself did not point where the
    claim was.
    **Two things made it register-worthy rather than a discarded draft.**
    First, the blast radius of acting on it: the proposed fix would have put
    a new code path into `intra.cpp` AND into
    `tools/golden_gen/svt_gen.c`, and the second of those changes the GATE,
    `expected_primitives.txt`, and all nine committed `.obu` artifacts. A
    one-line arithmetic slip with that much behind it is exactly what a
    measured derivation exists to catch - and note the asymmetry, because it
    is the practical lesson: a claim whose only instrument was a REASONING
    step is one refutation away from being believed. Second, the report's
    own symptoms should have falsified it earlier. It needed TWO mechanisms
    for one alleged cause (a static-zero buffer at one leaf, a stale buffer
    at another), and its own discriminator contradicted it: **12 D45 chroma
    leaves, 2 divergent** - a dispatch that wrote nothing would have broken
    all twelve. A claim whose own evidence requires extra mechanisms and
    then contradicts the mechanism is not a diagnosis; it is a pattern that
    has not been explained yet, and those are different artifacts. Say so.
  * **A CLAIM WHOSE ONLY INSTRUMENT WAS A REASONING STEP IS ONE
    REFUTATION AWAY FROM BEING BELIEVED - and the cheapest instrument in
    the repo is re-reading the line you cited.** The companion to the
    commit-count lesson above, supplied by RT18, and the contrast is the
    whole lesson. RT17's premise was an arithmetic slip INSIDE a
    derivation, over a table it had itself quoted: it read index 0 of
    `kModeToAngle` where it needed index 3, and it survived two reports
    because the derivation was internally plausible. Nothing in it was
    wrong-looking. It was refuted by opening `src/l4_intra/intra.cpp:476`
    and counting the array - no build, no decode, no gate run. RT18, on
    the same defect, **flagged its own derivation-vs-measurement boundary
    unprompted**, saying which of its four items was derived and which
    was read. **That self-flag is the reason the slice is trustworthy, and
    the difference between those two reports is the difference between a
    claim that nearly landed a gate-moving change and one that did not.**
    The asymmetry is the practical rule: a real decoder would have
    refuted RT17 immediately and would have told you nothing about WHICH
    step was wrong. Reading the cited line gives both. So: when a claim
    rests on reasoning rather than a measurement, re-read the cited line
    before the reasoning gets any further - it is the instrument that
    runs while thinking, and it is the one most often skipped precisely
    because the derivation in front of you feels like work.
  * **A NEGATIVE RESULT IS A RESULT, and it is the easiest kind to lose.**
    RT18 eliminated a suspect rather than naming one, and the elimination
    is the durable artifact: it retires RT17's diagnosis in full, and a
    report that says "this is not it" saves the next session the work of
    re-deriving that. Record it in this register, not in the commit that
    found it. **Declining to name a mechanism is the standard here, not a
    gap** - three candidate accounts of the same recon can be mutually
    exclusive, and naming the most plausible one before a test forces it
    puts a claim in the tree that later work has to disprove rather than
    merely fail to confirm. A mechanism in the docs ahead of its test is
    the shape this arc keeps refusing.
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
  - [x] CS-series LANDED (CS0-CS5c) + CS7 fix LANDED (5ce0224) + RT21 fix
    LANDED (927e697): the chroma entropy + emission + the color container +
    the four color artifacts - register entries in section 1.1; full-plane
    conformance **4/4 CLOSED** (color16/color32 from CS5; color8 joined at
    the CS7 bottom-left gate; color4 was the last divergent one - V 43/1024
    first at 647 over UV leaves 41/42/48/56 - until the RT21 chroma
    above-right Z-order gate closed it; the RT19/RT20/RT21 record is in
    section 9).
- [x] RT-series (the chroma/edge conformance chase): RT1 the /RTC
  fixture fix, RT6 the tree-order leaf emission, RT7-a/b the
  cross-decoder instrument + the chroma FI flag surface, RT9 the
  intra-chroma tx-type LUT + per-pass 1D selection, RT10/RT10b the
  bottom-left edge extension (the luma residual eliminated), CS7
  step 1 phases (a)/(b)/(c)/(c2) the chroma bottom-left mechanism
  CONFIRMED at BOTH c8 and c4; the full record in
  docs/decode_conformance.md.
- [x] **The chroma bottom-left FIX is LANDED (CS7, 5ce0224) and this
  register said otherwise.** The item below used to read "NEXT SLICE -
  the chroma bottom-left FIX" as an open `[ ]` while the entry ABOVE it
  recorded the same fix as LANDED at `5ce0224`. Both statements were in
  the live register at once, and one of them was false. Closed now: the
  spec text it pointed at is section 9 of this file, whose own
  obligation list at the end of the entry is the record of what landed.
  **Recorded because the failure is the one this arc keeps refusing, not
  because the item mattered.** A live register that contradicts itself is
  indistinguishable from a correct one to a reader who does not re-measure
  it, and this file is the one a future session consults FIRST to decide
  what to do next - so a stale line here does not merely mislead, it
  misdirects the next slice. The general rule, and it is cheap: **when a
  commit lands, the register entry that describes it moves in the SAME
  commit.** A fix whose registration is deferred is a red register, and
  the register is the file whose job is to be true.
- [x] **RT18 - the c4 chroma divergence is CHROMA-PATH-SPECIFIC. A NEGATIVE
  result: this retires RT17's diagnosis in full, and it does not name a
  replacement.** Recorded here because a negative finding that eliminates
  a suspect is the easiest kind of result to lose, and the next session
  would otherwise re-derive the same suspect. **The finding:** luma D45 at
  c4 is **14 leaves, 14 exact**; chroma D45 is **12 leaves, 2 divergent**
  (41 and 48). Same mode, same angle 45, same `drZ1`, same host dispatch
  at `intra.cpp:796`. A fault shared by all 12 chroma leaves would have
  to be a fault of the shared path, and the two that diverge are the two
  that do not share it. The earlier report's own discriminator - 12
  leaves, 2 divergent - already said this and was read as an oddity rather
  than as a refutation.
  **The derived half, and the derivation is flagged as a derivation.**
  `der[45] = 64` (counted from the literal at `intra.cpp:1070`), and
  `drZ1`'s loop `for (int r = 0, x = dx; ...; x += dx)` with
  `fracBits = 6` and `baseInc = 1` gives `base = 1 + r` and `shift = 0` at
  every row, so `pred[r][c] = above[1 + r + c]` with no interpolation,
  and with `above[] = 255` the prediction is 255 at all 16 positions of
  both roots. I re-derived that independently and it reproduces. RT18
  labelled this item as DERIVED rather than read, unprompted; that
  self-flag is why the slice is trustworthy, and it is the contrast the
  register entry on reasoning-step claims turns on.
  **The consequence, which is a result and not a dead end: THREE candidate
  accounts of the gate's recon at the divergent positions, and no two can
  hold** - (i) R = +254 with pred = 0, (ii) R = -1 with pred = 255, (iii)
  both correct and the recon write-back wrong. Not clean mod 256 either:
  +254 against -1 differ by 255, +3 against -4 by 7. So **the gate's own
  chroma recon is not reproducible from the gate's own coefficients plus
  any self-consistent prediction**, and the remaining question is which
  of the three is being written, not which arithmetic to apply.
  **NO CAUSE IS RECORDED HERE, deliberately.** RT19 is resolving it with
  a read of the builder's actual output. Naming a mechanism before that
  read would put a claim in the tree that later work must disprove rather
  than merely fail to confirm, and RT16's residual elimination stands on
  its own arithmetic - the PREDICTION claim does not, and nothing here
  touches either.
- [x] LANDED (FI1): the filter-intra DC-deciding fixture - the FI=1 emission
  path now has a committed-fixture test (test_pipeline.cpp:5507, the 8x8
  4x4-grid fixture at q100). The D2 policy is UNCHANGED (FI stays parked as
  a candidate, pipeline.h:156): the fixture CONTENT makes FI_D157_PRED
  measurably win on block 3 (D2 argmin DC_PRED at SAD 75, FI table
  88/239/143/0/297, next-best 13-candidate 141) and the decision-override
  layer lives ONLY in the test walk (a block-for-block mirror of the
  production encodeFrameAuto4x4Q walk), so no committed artifact or gate
  line moves (GATE re-measured 884,851 B, diff 0). The walk emits
  [skip][kf][FI][tokens] with writeFilterIntra carrying flag=1 +
  FILTER_D157_PRED on b3 (11 bytes 90 d7 bd 22 4b e0 aa 79 9e 85 20, eobs
  0,10,13,0) and the l7 reader round-trips content-exact per block with
  CDFs equal; both flag branches fire in one walk (b1 flag=0, b3 flag=1).
  The production call sites still hardcode FILTER_INTRA_MODES - asserted
  in the same test (Part A). Suite 9/9 (273.77 s); coverage unchanged
  (20,659/157,718 = 13.1%, tests are not reference lines).
- [~] NAMED FOLLOW-UP (FS5d): the 64x64 dual-domain unification - the
  emission case runs the compacted 1024-position token domain; the
  legacy/GPU callers keep the 4096-wide facade; unification = a
  1024-position GPU quant kernel + bench protocol.
- [~] PENDING: Luna's full audit of the FS/TD state at series close.
- [~] STATE REFRESH (2026-11-04, second pass, after CDEF2 + AN14 merged
  to master at 8795371):
  everything done so far, so the next slice does not have to infer it
  from the per-entry history. LANDED on master: the l0-l8 layers
  (transforms/quant, intra, motion scaffolding, entropy+od_ec, bitstream
  assembly + nine committed .obu artifacts, all decode content-1:1 via
  ffmpeg libaom-av1 + libdav1d), the AN-series AN1-AN17 COMPLETE
  (mean/variance family, histogram, downsample2d, b64Variance, the
  screen-content pair, luma-dominant, the pad family, the HME
  downsampling driver, the fft1d+fft2d float kernels, the noise-util tx
  family + OD_DIVU table, the noise strength solver, the flat-block
  finder + noise model init/update) and the A8 campaign DF1+DF2+CDEF1+
  CDEF2 (the deblocking kernel family + the CDEF find_dir/filter/
  fb-lbd/copy family, all in src/l10_filters; CDEF1 came via 416dfa1/
  5b8ed9b/e136487, CDEF2 via 80b95cb, AN14 via fccaa4e/8795371).
  MEASURED NOW (2026-11-04, this tree, by the instruments): ctest 11/11
  passed, 0 failed, 277.83 s; GATE byte-identical 884,851 B, 421 lines,
  diff 0; portable-C coverage 15.9% (25,097 / 157,718); cite_check 506
  citations, 495 resolved in-tree, 0 out-of-range, 11 unresolved (the
  known external set); doc_audit PASS + self-test 4/4. Phase B COMPLETE
  (AN12-AN14 landed 2026-11-02..04). STILL OPEN: CDEF3 (enc_cdef
  mse/search + svt_aom_fill_rect, the last named A8-kernel item; the
  pick_cdef_from_qp/finish_cdef_search spans are struct-walled, named);
  Phase C/D (AN18-AN22) parked behind A3/GOP per the decomposition
  above; after those, the remaining portable-math blocks named by the
  scouts: the restoration boxsum/selfguided/wiener-solver math
  (restoration.c :31-:962, Scout REST's tables ready) and the resize 1D
  kernels (resize.c :221-:430 + the :147-:219 closure, Scout D's
  tables ready). IN FLIGHT (this session): CDEF3 and LR1 as two
  parallel swarm writers, each in its own worktree off this commit per
  the AUTOSWARM.md scale-up pattern. Worktree hygiene: av1_a8 (a8-
  kernels at 80b95cb) and av1_an14 (an14-noiseutil at fccaa4e) are both
  ancestors of master now - their content is fully landed; RE-MEASURED
  later the same day: neither worktree is a stale checkout any more,
  both hold IN-FLIGHT uncommitted work (av1_a8: modified
  src/l10_filters/cdef.{h,cpp} + CMakeLists.txt, the CDEF3 writer;
  av1_an14: untracked src/l10_filters/restoration.{h,cpp}, the LR1
  writer) - do not remove these worktrees or reset their working
  trees; the hash-cited branch tips predate that work.
- [x] **AN-SERIES decomposition (AN0 scout, accepted by the judge
  2026-11-01; this section is its DURABLE HOME - the scout's report lived
  only in %TEMP%, which the evidence contract does not count).** 22
  ordered slices, Phases A-D, homes in src/l9_analysis (+ per-phase files
  as noted), tests src/l9_analysis/tests/test_*.cpp (doctest), CMake
  mirroring l5_motion; [G] marks a later GPU-twin slice. Per-file counts
  RE-MEASURED 2026-11-01 by the AN1 agent ([IO.File]::ReadAllLines |
  Where Trim, the coverage metric) - every scout figure reproduced,
  total 12,028: temporal_filtering.c 3755 / temporal_filtering.h 113 /
  noise_model.c 2210 / noise_model.h 324 / noise_util.c 361 /
  noise_util.h 53 / pic_analysis_process.c 1805 /
  pic_analysis_process.h 26 / pic_analysis_results.c 24 /
  pic_analysis_results.h 30 / resize.c 1945 / resize.h 62 / fft.c 212 /
  fft_common.h 806 / firstpass.c 210 / firstpass.h 92. Reference root:
  third_party/SVT-AV1/Source/Lib/Codec/.
  Phase A - RDO-feeding primitives (host): AN1 mean8x8
   (pic_analysis_process.c:194 + compute_mean.h:21) LANDED; AN2
   meanSquare8x8 (:220 + compute_mean.h:26) LANDED; AN3 the sub-mean pair
   (:242/:263 + compute_mean.h:31/:34) LANDED; AN4 interm_var_four8x8
   (:286/:313 + compute_mean.h:40) LANDED; AN5 calculate_histogram
   (:170/:188) LANDED; AN6 downsample_2d
  (:134); AN7 the b64 variance pyramid (compute_b64_variance :318); AN8
  the screen-content detectors (:1354, antialiasing-aware :1207); AN9
  luma-dominant (:1440); AN10 the pad family (:648, :745, :820, :858,
  :1549); AN11 downsample_filtering_input_picture (:1498) LANDED -
  PHASE A COMPLETE (all eleven slices, measured trajectory in the AN11
  register entry below). Phase B -
  FFT + noise model: AN12 fft1d kernels (fft_common.h:114-:330); AN13
  fft2d/ifft2d (fft.c:51/:108); AN14 noise_util (:20, :70-:104, the
  OD_DIVU table :114); AN15 the flat block finder (noise_model.c:399-:634);
  AN16 the noise strength solver (noise_model.c:104-:397); AN17 the
  noise model init/update (noise_model.c:636-:1147). Phase C -
  temporal filtering (parkable behind A3/GOP work): AN18 planewise
  filter primitives (temporal_filtering.c:930, hbd :1105, central :262);
  AN19 decay factor + expf table (:568-:737); AN20 firstpass pure
  accumulation (firstpass.c:133/:141). Phase D - resize (parkable):
  AN21 resize 1D kernels (resize.c:221-:430, 8-bit only); AN22 the
  resize_frame driver (resize.c:881).
  Coverage trajectory (the scout's arithmetic, derived not measured):
  Phase A ~1,185 ref lines -> ~+0.75 pt; Phase B ~2,140 (excl. grain
  params) -> ~+1.36 pt; Phase C ~600 -> ~+0.38 pt; Phase D ~480 ->
  ~+0.30 pt; full program excl. parked ~10,720 lines -> ~+6.8 pt; A11
  trajectory 0 -> ~9.9% after Phase A, ~27.5% after B, ~40% after C,
  ~44% after D.
  The three named exclusions (plus one): firstpass IS the rate-control
  first pass - output_stats (firstpass.c:83) writes into the TWO_PASS
  stats buffer, so port only AN20's pure accumulation and the rest waits
  for A9; temporal_filtering.c is GOP-coupled - the per-block planewise
  filters are portable, the driver is not (the subpel search drives
  interpolated references, A3 machinery); noise_model
  get_grain_parameters (noise_model.c:1159) writes AomFilmGrain - an A8
  bitstream surface, parked (film grain would invite golden-regen
  temptation for zero RDO value). The scout's fourth: the superres
  decision (resize.c:1165-:1416, scale refs :1686+) is RC-coupled and
  not sliced.
  SCOUT ANCHOR CORRECTIONS (found by the AN1 re-derivation, the table
  above carries the corrected values): svt_compute_sub_mean_8x8_c
  :225->:242, svt_aom_compute_sub_mean_squared_values_c :240->:263,
  svt_compute_interm_var_four8x8_c :264->:286,
  svt_aom_downsample_filtering_input_picture :1499->:1498. The compute_mean.h
  A12 cross-area question is named in tools/coverage_areas.txt (both the
  A11 and A12 rows), not silently resolved.
- [x] AN1 LANDED (2026-11-01): analysis::mean8x8, the first AN-series
  slice. New layer src/l9_analysis (CMake mirrors l5_motion: av1_analysis
  links av1_core; ctest target l9_analysis). 1:1 target svt_compute_mean_c
  at 8x8 - pic_analysis_process.c:194-216, declaration twin
  compute_mean.h:21 (A12-classified; both anchors cited), VARIANCE_PRECISION
  = 16 at pic_analysis_process.c:36. Derivation done BEFORE the test: uint64
  accumulation of every pixel row-by-row with the stride advance, then
  (sum << (VARIANCE_PRECISION >> 1)) / (width * height) - shift FIRST,
  truncating division, no rounding term (:211; contrast the decimation
  filter's (sum + 2) >> 2 at :157). At 8x8 that is sum * 4 exactly, so no
  8x8 test can discriminate shift-then-divide from divide-then-shift; the
  order is settled by the :211 citation, not by a test. RED: the test's
  compile failure (C1083 'analysis.h') captured before any implementation;
  GREEN: the port, zero warnings under /W4 /WX. Three assertions, all
  hand-computed: constant-128 -> 32768; stride-12 ramp 0..63 (sum 2016) ->
  8064, which discriminates a stride-ignoring implementation by
  construction; all-255 (sum 16320) -> 65280. The scout's RED sketch
  expected value ((128ull*64) << 8) / 64 = 32768 CONFIRMED by the
  derivation - no correction (the function name landed as analysis::mean8x8,
  not the sketch's computeMean8x8; the brief's name). Suite 9 -> 10
  targets, all green. GATE re-measured 884,851 B, diff 0. Coverage: A11
  0.00 -> 0.0015 (18 non-blank ref lines, pic_analysis_process.c:194-214);
  20,677/157,718 = 13.1% at one decimal - unchanged at printed precision,
  the movement is in the covered column.
- [x] AN2 LANDED (2026-11-01): analysis::meanSquare8x8, the 8x8 port of
  svt_compute_mean_squared_values_c - pic_analysis_process.c:220-240,
  declaration twin compute_mean.h:26, VARIANCE_PRECISION = 16 at :36.
  Derivation BEFORE the test: uint64 accumulation of src[hi]*src[hi]
  (uint8 promotes to int, 255*255 = 65025; 64 terms max 4,161,600), then
  (sumSq << 16) / (w*h) - shift FIRST, truncating division, no rounding
  term (:237). At 8x8 the division is exact for every sumSq (64 = 2^6
  divides 2^16) = sumSq * 1024, so no 8x8 test can discriminate
  shift-then-divide from divide-then-shift; the order is settled by the
  :237 citation. Cross-checks: the SSE2 twin
  svt_compute_mean_of_squared_values8x8_sse2_intrin
  (ASM_SSE2/compute_mean_intrin_sse2.c:76-114) sums all 8 rows and
  returns sumSq << 10 - consistent; the consumer variance formula
  (meanSq - mean*mean) >> VARIANCE_PRECISION (:415-417) needs meanSq on a
  << 16 scale, which sumSq << 10 on the 64-pixel sum delivers.
  SKETCH VERDICT: the scout's "9<<16 scaled" for a constant-3 block is
  CONFIRMED by the derivation: sumSq = 576, (576 << 16)/64 = 589824 =
  9 << 16.
  RED (captured): test_variance.cpp(52,5): error C2039: 'meanSquare8x8':
  is not a member of 'analysis'.
  GREEN: the 8x8 port, zero warnings under /W4 /WX. Three assertions:
  constant-3 -> 589824 (the square-vs-mean discriminator; NOT
  accumulator-discriminating, 576 << 16 fits uint32); stride-12 ramp
  0..63 (sumSq 85344 = 63*64*127/6) -> 87392256; all-255 (sumSq 4161600)
  -> 4261478400, which discriminates the uint64 accumulator (a uint32
  accumulator wraps at the << 16). Assertions 2 and 3 passed on first
  build and were proven discriminating by mutation checks (failing
  values in the commit message): stride-advance-removed -> 1146880;
  uint32 accumulator -> 20283392 / 33619968. The first mutation attempt
  was INVALID (the mutant failed to compile, C4100 unreferenced parameter
  under /W4 /WX, and the swallowed build output made the stale binary
  report 6/6 PASS) - recorded because a non-compiling mutation looks
  like a pass unless the build output is inspected.
  MEASURED (2026-11-01): suite 10/10 passed, 0 failed, 272.62 s; GATE
  re-measured 884,851 B, 0 CR, 0 non-ASCII, diff 0; coverage A11 18 ->
  36 covered (the 18 non-blank ref lines of :220-240, measured with the
  coverage metric's own counter), total 20,677 -> 20,695 of 157,718 =
  13.1% at one decimal, unchanged at printed precision; the paired
  conformance count unchanged (9 artifacts, all content-1:1).
- [x] AN3 LANDED (2026-11-01): the sub-mean pair, TWO functions one slice:
  analysis::subMean8x8 = svt_compute_sub_mean_8x8_c
  (pic_analysis_process.c:242-261, declaration twin compute_mean.h:31,
  uint16 stride mirror) and analysis::subMeanSquareValues =
  svt_aom_compute_sub_mean_squared_values_c (:263-284, declaration twin
  compute_mean.h:34, generic w/h). Derivations BEFORE any test:
  subMean8x8's skip = vi + vi loop ASSIGNS skip in the for-update (never
  increments), so the summed rows are {0,2,4,6} - 32 px of the full
  8-wide row - then sum << 3 with NO division (:257); the commented-out
  formula (sum << 8)/(w*h/2) equals sum << 3 exactly (32 = 2^5 divides
  2^8), so nothing is lost. subMeanSquareValues: same mechanics gated on
  input_area_height, rows of SQUARES, then sumSq << 11 with NO division
  (:281) - the comment there drops the /2 and would give sumSq << 10 at
  8x8, but the LIVE code shifts 11 = (sumSq << 16)/(w*h/2), the HALF-area
  normalization; THREE anchors: the SSE2 twin
  svt_aom_compute_subd_mean_of_squared_values8x8_sse2_intrin
  (ASM_SSE2/compute_mean_intrin_sse2.c:15-51) returns << 11 at :50, the
  FULL-square twin returns << 10 at :113 (the 2x ratio IS the half-area),
  and the consumer variance formula (meanSq - mean*mean) >> 16
  (:415-441) is fed by BOTH branches of compute_b64_variance, so the
  scales must agree. 1:1 rule: port the CODE (<< 11 unconditionally), not
  the comment.
  RED (captured, one fixture both functions): test_variance.cpp(105,5)
  error C2039 'subMean8x8': is not a member of 'analysis' and
  test_variance.cpp(130,5) error C2039 'subMeanSquareValues': is not a
  member of 'analysis'. The brief's discriminator fixture: even rows 10,
  odd rows 200, stride 12 - the sub-mean (2560) differs from the full
  mean8x8 (26880) of the same block BY CONSTRUCTION, so a uniform fixture
  could not have passed trivially and none was used.
  GREEN: both functions, zero warnings under /W4 /WX. Four assertions:
  subMean8x8 -> 2560 (rows {0,2,4,6} sum 320 << 3; also
  stride-discriminating - a stride-ignoring mutant would read 4240 from
  bytes 0..63); subMeanSquareValues (8,8) -> 6553600 (sumSq 3200 << 11;
  the full meanSquare8x8 of the same block is 1313996800); (8,9) ->
  8192000 (rows {0,2,4,6,8} = ceil(9/2) rows, sumSq 4000 - pins the
  generic loop's odd-height mechanics the only live caller (8,8) cannot
  see); (4,4) -> 1638400 (sumSq 800 << 11 - pins the HARDWIRED shift: a
  comment-faithful (w,h)-derived shift would be 13 at (4,4), i.e.
  6553600). Assertions 3 and 4 passed on first build and were proven
  discriminating by mutation checks (captured values in the commit
  message): skip < height-1 -> 6553600 != 8192000 (only the odd-height
  assertion failed); comment-faithful shift -> 6553600 != 1638400 (only
  the (4,4) assertion failed).
  MEASURED (2026-11-01): suite 10/10 passed, 0 failed, 272.67 s; GATE
  re-measured 884,851 B, 0 CR, 0 non-ASCII, diff 0; coverage A11 36 ->
  72 covered (the 17 non-blank ref lines of :242-261 + the 19 of
  :263-284, measured with the coverage counter), total 20,695 -> 20,731
  of 157,718 = 13.1% at one decimal, unchanged at printed precision; the
  paired conformance count unchanged (9 artifacts, all content-1:1).
- [x] AN4 LANDED (2026-11-01): analysis::intermVarFour8x8, the port of
  svt_compute_interm_var_four8x8_c - pic_analysis_process.c:286-313,
  declaration twin compute_mean.h:40. Derivation BEFORE the test: the
  function CALLS the AN3 primitives, it does not recompute them - four
  pairs of svt_compute_sub_mean_8x8_c +
  svt_aom_compute_sub_mean_squared_values_c(src, stride, 8, 8) at
  block_index = 0, 8, 16, 24 (:290-:312) - so the port composes
  analysis::subMean8x8 + analysis::subMeanSquareValues, the reference's
  unrolled +8 accumulation kept verbatim (uint32 blockIndex, uint16
  stride pass-through). BRIEF CORRECTION recorded: the assignment said
  "one 16x16 input -> 4 raster quadrants"; the reference's mechanics are
  FOUR SIDE-BY-SIDE 8x8 BLOCKS along one row origin - its own
  "(0,1)..(0,4)" comments are meaningless labels, and the live caller
  (compute_b64_variance, pic_analysis_process.c:350-359) passes the
  frame-strided buffer and advances blk_offset by 32 per call (offset 16
  on a literal 16x16 tile at stride 16 would read row 1; no caller does
  that). The port mirrors the reference, not the brief.
  RED (captured): test_variance.cpp(204,15): error C2039
  'intermVarFour8x8': is not a member of 'analysis' (+ C3861 identifier
  not found). GREEN: the composition, zero warnings under /W4 /WX.
  Fixture: 32-wide strip, stride 32, blocks constant {10, 20, 30, 40} on
  all 8 rows + a poison 9th row (99s, never read); hand-derived
  mean[k] = (32*v_k) << 3 = 256*v_k -> 2560/5120/7680/10240 and
  meanSq[k] = (32*v_k^2) << 11 = 65536*v_k^2 -> 6553600/26214400/
  58982400/104857600 - four DISTINCT values per array, so offset/order
  mutations fail by construction. MUTATION (captured): the (0,2) step
  +8 -> +16 gives steps 0,16,24,32 and kills 6 of the 8 assertions -
  mean[1] 7680 != 5120, meanSq[1] 58982400 != 26214400, mean[2] 10240 !=
  7680, meanSq[2] 104857600 != 58982400, mean[3] 2560 != 10240 and the
  meanSq[3] ERROR (its values line beyond the captured excerpt) - while
  block 0 stays green: per-block offset discrimination proven.
  INSTRUMENT INCIDENT, fixed in-session: a PowerShell Set-Content
  mutation attempt wrote a UTF-8 BOM into analysis.cpp (EF BB BF) - the
  -replace pattern used CRLF against the LF-only file, silently no-oped,
  and Set-Content still rewrote the file. Caught by a post-write byte
  check (the mojibake class AGENTS.md names), stripped with
  [IO.File]::WriteAllBytes, the mutation re-applied with the Edit tool.
  The tree is 0-CR again (verified after the strip).
  MEASURED (2026-11-01): suite 10/10 passed, 0 failed, 270.10 s; GATE
  re-measured 884,851 B, byte-identical, diff 0; coverage A11 72 -> 97
  covered (the 25 non-blank ref lines of :286-313, measured with the
  coverage metric's own counter), total 20,731 -> 20,756 of 157,718;
  the printed percentage MOVED for the first time in the AN series:
  20,756/157,718 = 13.1602% crosses the 13.15 rounding boundary that
  AN1-AN3's 13.1443% sat below, so coverage.ps1 now prints 13.2%
  (AN1-AN3 printed 13.1%); the paired conformance count unchanged
  (9 artifacts, all content-1:1).
- [x] AN5 LANDED (2026-11-01): analysis::calculateHistogram, the port
  of calculate_histogram - pic_analysis_process.c:170-188. BRIEF
  CORRECTIONS (both grep-verified): the function's real name is
  calculate_histogram, NOT the brief's svt_aom_calculate_histogram
  (the only Codec occurrences are the :167 comment, the :170 definition
  and the :560 caller), and it has NO header declaration twin - the
  function is file-local to its .c, so the definition is the only
  anchor (the brief's "read its header decl" has no target).
  Derivation BEFORE the test: the outer loop steps its counter by
  decim_step but selects the row by POINTER advance
  (src += stride * decim_step, :184); the inner loop steps the column
  by decim_step; the PIXEL VALUE is the bin index into the 256-bin
  uint32 histogram; sum (uint64) accumulates the SAMPLED pixels only
  (:182) - the caller rescales by decim_step^2 (:569) and
  pre-initializes the bins to 1 (:545-547), so the port only ADDS on
  top and mirrors the reference's two-read shape. Host only, NO GPU
  twin (the brief's "optional later" stays parked, not built).
  RED (captured): test_variance.cpp(242,15): error C2039
  'calculateHistogram': is not a member of 'analysis' (+ C3861
  identifier not found). GREEN: the port, zero warnings under /W4 /WX.
  Three TEST_CASEs, all hand-derived: the brief's ramp (16x16 stride 16
  step 1, src[i] = i: every bin 1, sum of bins 256 = the counted
  pixels, histogram[0] = histogram[255] = 1, *sum = 0+1+...+255 =
  32640); stride != width (4x4 at stride 8, cols 4-7 poison 99s:
  bins 0..15 = 1, histogram[99] = 0, *sum = 120, bin sum 16);
  decim_step (4x4 step 2, src[y][x] = 10y+x: sampled (0,0)=0 (0,2)=2
  (2,0)=20 (2,2)=22, bin sum 4, *sum = 44, odd bins 1/3/21/23 stay 0).
  MUTATIONS (all captured, all reverted): M1 bin-index
  histogram[src[hi]] -> histogram[hi] kills 3 cases - ramp:
  histogram[0] 16 != 1 and histogram[255] 0 != 1 while binSum 256 and
  *sum 32640 still pass (the two specific-bin assertions carry the
  bin-index discrimination exactly as the brief predicted); stride:
  histogram[15] 0 != 1; decim: histogram[2] 2 != 1, histogram[20]
  0 != 1, histogram[22] 0 != 1. M2 inner step hi += decimStep ->
  hi += 1 kills only the decim case (binSum 8 != 4, histogram[1]
  1 != 0, sum 92 != 44). M3 stride-ignoring row advance
  src += width * decimStep (+ a behavior-neutral (void)stride to
  satisfy /W4 /WX - the bare stride-dropping mutant is INVALID under
  /W4 /WX: C4100 unreferenced parameter, the AN2 lesson live) kills
  only the stride case (histogram[15] 0 != 1, histogram[99] 8 != 0,
  sum 820 != 120). M3's first attempt ALSO tripped the AN2
  stale-binary trap: the Edit left a non-compiling tree (C2143 from a
  partial match) and the run executed the stale M2 binary; caught
  because the build errors were inspected before trusting the run
  output.
  MEASURED (2026-11-01): suite 10/10 passed, 0 failed, 268.30 s; GATE
  re-measured 884,851 B, byte-identical, diff 0; coverage A11 97 ->
  115 (the 18 non-blank ref lines of :170-188, measured with the
  coverage metric's own counter), total 20,756 -> 20,774 of 157,718;
  printed percentage stays 13.2% (20,774/157,718 = 13.1716%); the
  paired conformance count unchanged (9 artifacts, all content-1:1).
- [x] AN6 LANDED (2026-11-01): analysis::downsample2d, the port of
  svt_aom_downsample_2d_c - pic_analysis_process.c:134-164, declaration
  twin Codec/motion_estimation.h:33-35 (grep-verified: that header is the
  function's only declaration; the downsample_2d call sites at
  :1504/:1522/:1530 go through the rtcd pointer, not the _c symbol).
  Derivation BEFORE the test: input_stripe_stride = stride * decim_step
  (:144); half_decim_step = decim_step >> 1 (:146); the input pointer is
  PRE-ADVANCED by half_decim_step rows (:148) and prev_input_line is one
  stride above it (:151), so the summed 2x2 window sits at rows/cols
  {half-1, half} WITHIN each decim_step cell - the cell's own top-left
  2x2 at step 2, its MIDDLE 2x2 at step 4 (vi/hi step half -> half-1
  anchor); sum of the four pixels prev[hi-1] + prev[hi] + cur[hi-1] +
  cur[hi] (:154-156), output (sum + 2) >> 2 (:157); per-row advances
  input_samples += input_stripe_stride / decim_samples += decim_stride
  (:159-160). Live callers: decim_step 2 (quarter, :1504-1510) and 4
  (sixteenth, :1530-1536) - the generic step is the reference's own
  surface, not a generalization. Const on the input is the port's only
  deviation (the reference takes uint8_t* and never writes it). Host
  only, NO GPU twin (the scout's [G] stays parked, not built).
  RED (captured): test_variance.cpp(230,15) / (257,15) / (281,15):
  error C2039 'downsample2d': is not a member of 'analysis' (+ C3861
  identifier not found) - all three TEST_CASEs fail to compile. GREEN:
  the port, zero warnings under /W4 /WX. Four TEST_CASEs, all
  hand-derived: uniform 4x4 step 2 (all 10s -> all 10, the (40+2)>>2 =
  10 sanity case, NOT rounding-discriminating since 40>>2 is also 10);
  the ramp {0,4,8,12}/{1,5,9,13}/{2,6,10,14}/{3,7,11,15} at stride 8
  with 99s in cols 4-7 -> out {3,11,5,13} - every 2x2 sum is congruent
  2 mod 4, so the +2 IS discriminated (a +2-less mutant gives
  {2,10,4,12}), and rows AND cols are non-uniform so window mispairing
  fails; step 4 on an 8x8 ramp (src[y][x] = 8y+x) -> out
  {14,18,46,50}, the middle-2x2 phase (a cell-origin mutant gives
  out[0][0] = 20>>2 = 5); stride 12 ramp with 99s in cols 4-11 -> the
  same {3,11,5,13}, poison never read by the faithful port.
  MUTATIONS (all captured, all reverted): M1 the +2 dropped kills the
  ramp case (dst {2,10,4,12} vs {3,11,5,13}) and the step-4 case
  ({13,17,45,49} vs {14,18,46,50}) - the uniform case passes as
  derived; M2 the window anchored at the cell origin
  (hi - halfDecimStep .. +1) kills ONLY the step-4 case (all four
  outputs wrong; step 2 is unaffected because half-1 = hi-half there) -
  the phase discrimination is exactly the brief's 0-phase reading;
  M3 the stride-ignoring row advance (curLine += areaWidth * decimStep,
  with a behavior-neutral (void)inputStripeStride for /W4 /WX) kills
  the SECOND output row of both the ramp case (dst[2] 4 != 5, dst[3]
  12 != 13 - the faithful advance is stride*decim_step = 16, the
  mutant's 8 lands one row early) and the stride-12 case (dst[2]/
  dst[3] read the poison band). STALE-BINARY INCIDENT (the AN5 lesson
  live, caught in-session): the M3 revert ran ctest without rebuilding
  and read the mutant binary as a failure; rebuilt, the reverted tree
  is green again.
  MEASURED (2026-11-01): suite 10/10 passed, 0 failed, 261.59 s; GATE
  re-measured 884,851 B, byte-identical, diff 0; coverage A11 115 ->
  144 (the 29 non-blank ref lines of :134-164, measured with the
  coverage metric's own counter), total 20,774 -> 20,803 of 157,718;
  printed percentage stays 13.2% (20,803/157,718 = 13.1900%); the
  paired conformance count unchanged (9 artifacts, all content-1:1).
  DOC FIX in the same commit: the A11 row's prose cited the mean span
  as :194-216 - the AN1 register measured :194-214 (18 lines, the
  function ends at the :214 closing brace; :215 is blank and :216 is
  the NEXT function's comment opener). The 115 total was consistent
  with :194-214 all along; the prose span is corrected here, no
  counted number moves.
- [x] AN7 LANDED (2026-11-01): analysis::b64Variance, the port of
  compute_b64_variance - pic_analysis_process.c:318-442, static,
  single caller :610, NO header declaration twin (grep-verified
  file-local to its .c: the only occurrences are the :315-317 comment,
  the :318 definition and the :610 caller). The RDO-input chain's
  capstone: it composes the landed AN primitives exactly as the
  reference composes its own. Derivation BEFORE the test: the
  BLOCK_MEAN_PREC_SUB branch (:347-361) is SIXTEEN intermVarFour8x8
  calls (AN4), each filling FOUR consecutive 8x8 entries at
  blk_8x8_idx = 8*row + 4*colGroup, blk_offset advancing 32 per call;
  the BLOCK_MEAN_PREC_FULL branch (:335-346) is 64 mean8x8 +
  meanSquare8x8 pairs (AN1 + AN2); the 16x16 fold (:364-380) averages
  the four 8x8 blocks at first_8x8_blk_idx = 16R + 2C {+0, +1, +8, +9}
  >> 2; the 32x32 fold (:383-399) the four 16x16 blocks at
  first_16x16_blk_idx = 8P + 2Q {+0, +1, +4, +5} >> 2; the 64x64 fold
  (:402-409) the four 32x32 blocks >> 2 - every fold is independent
  PER ARRAY (means fold from means, squared-means from squared-means),
  no level recomputes from pixels; variance = (sq - mean*mean) >>
  VARIANCE_PRECISION (:415-417/:423-426/:432-435/:439-441), uint16,
  with the sub-block loops gated on scs->allintra ||
  static_config.aq_mode == 1 || static_config.variance_octile (:412)
  and the 64x64 entry written UNCONDITIONALLY (:438-441). The sbVar
  layout is the EbMeTierZeroPu bases (me_context.h:47/:48/:52/:68):
  64x64 at 0, 32x32 at 1, 16x16 at 5, 8x8 at 21 - only 0..84 are ever
  written (MAX_ME_PU_COUNT 209, me_sb_results.h:22; entries 85..208
  stay whatever the caller held). Prec selector:
  definitions.h:2043-2044 (#define BLOCK_MEAN_PREC_FULL 0 /
  BLOCK_MEAN_PREC_SUB 1); the live reference ALWAYS selects Sub
  (Globals/enc_handle.c:4339, unconditional assignment) - the Full
  branch is ported because the reference carries it. No-underflow
  note (derived, both paths): Cauchy-Schwarz over the block keeps
  sq >= mean^2 at every level, so the uint64 subtraction cannot wrap.
  Fixture arithmetic machine-checked BEFORE the tests (the an67
  derivation script in the session temp dir, NOT the port): block 0
  samples 10s -> mean8[0] = 2560, sq8[0] = 6553600; blocks k>=1
  uniform v=k+1 -> mean8[k] = 256(k+1), sq8[k] = 65536(k+1)^2;
  mean16[0] = (2560+512+2304+2560)>>2 = 1984 and mean16[i>0] =
  256*k1(i) + 1408 with k1(i) = 16R + 2C (the v-sum of any 16x16
  quadrant is 4*k1 + 22); sq16[0] = 65536*(100+4+81+100)>>2 = 4669440
  and sq16[i>0] = 16384*(4k1^2 + 44k1 + 186); var16[0] = 733184>>16 =
  11 (11.1875) while var16[i>0] = 1064960>>16 = 16 (16.25, CONSTANT
  because every quadrant's value multiset is {k+1, k+2, k+9, k+10});
  var32[0] = 4640512>>16 = 70 (70.8125), var32[1..3] = 5324800>>16 =
  81 (81.25); var64 = 21865200>>16 = 333 (333.66); all 64 var8 are 0
  (block 0 samples a constant 10-half, the rest are uniform). The
  FULL path diverges EXACTLY where block 0 is non-uniform (uniform
  blocks give identical values on both paths, derived): mean8[0] =
  3840, sq8[0] = 16384000, var8[0] = 25, mean16[0] = 2304, sq16[0] =
  7127040, var16[0] = 27 (27.75), mean32[0] = 3936, sq32[0] =
  20123648, mean64 = 8376, sq64 = 91841536, var64 = 330 (330.75).
  NAMED DEVIATIONS (all in the header comment): scs/pcs/b64_idx/
  input_luma_origin_index dropped - y points AT the block origin and
  sbVar is ONE flat variance row; the intermediate arrays the
  reference keeps on its stack (mean16/sq16/mean32/sq32/mean64/sq64)
  are exposed as out-params for the tests; the :412 gate threaded as
  the bool subBlockVariances; the prec selector threaded as the
  BlockMeanPrec enum (Full = 0, Sub = 1). Host only, NO GPU twin
  (parked, named).
  RED (captured): test_variance.cpp(389,15)/(462,15)/(501,15): error
  C2039 'b64Variance': is not a member of 'analysis' (+ C3083
  'BlockMeanPrec', C2065 'Sub', C3861 identifier not found) - all
  three TEST_CASEs fail to compile. GREEN: the port, zero warnings
  under /W4 /WX. Three TEST_CASEs: the SUB pyramid asserting ALL 16
  mean16 + ALL 16 sq16 + all 4 mean32/sq32 + mean64/sq64 against the
  hand-derived literals plus the full sbVar placement (0/1..4/5..20/
  21..84 and the untouched 85..95 tail poisoned 0xBEEF); the FULL
  divergence (the nine block-0-carrying values above, with
  sbVar[22] == 0 vs sbVar[21] == 25 pinning the divergence to block
  0, and the block-0-free quartets re-asserted unchanged); the gate
  (only sbVar[0] may move, six sub-block slots re-checked as 0xBEEF,
  mean64/sq64 still produced). MUTATIONS (all captured, all
  reverted): M1 the 16x16 fold's +8 -> +4 (the row-half confusion)
  kills ALL THREE cases; M2 the :412 gate dropped kills ONLY the gate
  case (six 0xBEEF slots written) - its first build was INVALID under
  /W4 /WX (C4100 unreferenced parameter, the AN2/AN5 lesson live) and
  the ctest run between the invalid build and the fix read the stale
  M1 binary, caught by inspecting the build errors before trusting
  the run output; the valid mutant uses the AN5 behavior-neutral
  (void)subBlockVariances pattern; M3 the 8x8 base 21 -> 20 kills
  the SUB case's last 8x8 slot (sbVar[84] = 0xBEEF != 0) and the
  FULL case's sbVar[21] == 25.
  MEASURED (2026-11-01): suite 10/10 passed, 0 failed, 263.08 s;
  GATE re-measured 884,851 B, byte-identical, diff 0; coverage A11
  144 -> 259 (the 115 non-blank ref lines of :318-442, measured with
  the coverage metric's own counter), total 20,803 -> 20,918 of
  157,718; the printed percentage MOVED: 13.2% -> 13.3%
  (20,918/157,718 = 13.2629% crosses the 13.25 rounding boundary the
  AN6 13.1900% sat below); the paired conformance count unchanged
  (9 artifacts, all content-1:1). The brief's ~+0.15 pt estimate for
  the two slices measured +0.073 pt (AN7's 115 lines) + 0.018 pt
  (AN6's 29 lines) = +0.092 pt total (13.1716% -> 13.2629%) - the
  estimate was optimistic, the measured values stand.
 - [x] AN8 LANDED (2026-11-01): the screen-content detector pair, TEN new
   primitives in src/l9_analysis: analysis::countColorsWithThreshold
   (svt_av1_count_colors_with_threshold, pic_analysis_process.c:911-930),
   analysis::isValidPaletteNbColors (is_valid_palette_nb_colors,
   :956-978, static), analysis::findDominantValue
   (svt_av1_find_dominant_value, :984-1003), analysis::dilateBlock
   (svt_av1_dilate_block, :1024-1076), analysis::variance16x16 /
   analysis::variance8x8 (the VAR(W,H) macro instances
   svt_aom_variance16x16_c / 8x8_c, C_DEFAULT/variance.c:141-190, called
   the way the detectors call them - b = svt_aom_eb_av1_var_offs
   :936-942, constant 128, at ref_stride 0, so the 128 cancels and the
   return is the population-variance SUM; :189's (int64_t) cast and
   TRUNCATING division kept), analysis::getSbyPerpixelVariance16x16/8x8
   (svt_av1_get_sby_perpixel_variance, :944-952:
   ROUND_POWER_OF_TWO(var, eb_num_pels_log2_lookup[bs]),
   definitions.h:457, common_utils.c:39 -> BLOCK_16X16 = 8, BLOCK_8X8 =
   6; the fn_ptr/BlockSize dispatch replaced by one wrapper per size,
   the l5 per-size pattern), the ScAaCounts carrier (Sc_AA_Counts,
   :1078-1086), analysis::scAaCollectCounts
   (svt_aom_sc_AA_collect_counts, :1088-1190, static: multiplier 2 +
   checkerboard initial_col + 2*blk_w step under fast_detection,
   :1096-1104; region_id = ((r >= h2)?2:0) + ((c >= w2)?1:0),
   :1106-1108; the complex branch dilates and re-counts against
   complex_final_color_thresh; the x2 fast counts :1177-1187),
   analysis::isScreenContent (svt_aom_is_screen_content, :1354-1429:
   16x16 pass color_thresh 4 / var_thresh 0 feeding sc_class0..3
   :1386-1398, then 8x8 pass var_thresh 16 feeding sc_class4 :1427-1428)
   and analysis::isScreenContentAaAware
   (svt_aom_is_screen_content_antialiasing_aware, :1207-1350: 16x16
   collect final 6 / var 5, 8x8 collect final 8 / var 50
   (:1276-1278), the six class formulas :1297-1321). pcs->enhanced_pic
   and the pcs->scs fast flag are threaded as parameters (the b64Variance
   deviation pattern); sc_class5 is NOT written by is_screen_content
   (grep-verified :1386-1428) - the port's zero-init false is named.
   BRIEF CORRECTION, checked against the reference's actual inputs: the
   detectors consume NONE of the AN1-AN7 primitives - the spans call
   their own count/palette/dominant/dilate/variance helpers; AN5's
   histogram (caller :560) and AN7's b64Variance (caller :610) are
   disjoint from :911-1429. The A14 row gained variance.c's variance_c +
   VAR macro (23 lines, 0.30 -> 0.316). Caller context measured:
   pd_process.c:4795-4822 (mode 2 -> is_screen_content only at
   <= 1080p; mode 3 -> AA-aware; the luma-dominant call at :4813 reads
   pcs->chroma_downsampled_pic, NOT enhanced_pic).
   RED (captured): compile errors C2039 'countColorsWithThreshold' /
   'isValidPaletteNbColors' / 'ScClasses'+'isScreenContent' /
   'ScAaCounts'+'scAaCollectCounts' / 'isScreenContentAaAware': is not
   a member of 'analysis' (test_variance.cpp:597/630/826/930-931/
   1008+1030); the fast-detection case RED'd behaviorally (3 failing
   region assertions on first run - MY hand-derivation was wrong, not
   the port: the fast checkerboard visits regions 2/3 TWICE each, so
   the pre-multiplier region palette is {1,1,2,2} not {2,2,2,2}; the
   test's expectations were fixed, the implementation untouched).
   GREEN: the ten primitives + the two detector tests + the mixed
   scAaCollectCounts fixture (checkerboard / dilate-complex / photo /
   solid-ignored blocks, region ids, fast x2). One stale-binary trap
   hit and caught: slice 3's build failed on a missing <cstring>
   (memset) and the run output read the OLD exe - the run was
   discarded, the include fixed, green re-measured.
   MUTATIONS (all captured, all reverted): M1 countColors early exit
   disabled -> 2 fails; M2 palette solid gate (<= 1) disabled -> 1
   fail; M3 palette early exit disabled -> 1 fail (load-bearing for
   the boolean: without it a 5-color block falls through the <= 1 gate
   to TRUE); M4 findDominant > -> >= -> 1 fail; M5 dilate corner
   dilates: removing ONE corner SURVIVED (the other three still reach
   the target) -> the mutant was strengthened to all four corners (the
   true 4-direction-only mutant) -> 1 fail - the fixture's target cell
   has all four 4-adjacent neighbours non-dominant and only its
   diagonals dominant; M6 variance16x16 truncating division -> ceil ->
   1 fail; M7 sby8 rounding term +32 dropped -> 1 fail; M8 isScreen-
   Content 16x16 palette gate hardwired true -> 4 fails (the smooth
   ramp's negative path); M9 AA region-pass multiplier *10 -> *1 -> 2
   fails (the checkerboard's 16-per-region count sits exactly at the
   boundary); M10 isScreenContent class0 multiplier *10 -> *1 -> 2
   fails (4096 > 4096 is false - strict inequality pinned).
   MEASURED (2026-11-01): suite 10/10 passed, 0 failed, 261.66 s;
   GATE unchanged 884,851 B byte-identical, diff 0 (no generator
   input); coverage A11 259 -> 665 (+406: the nine :911-1429 spans
   above), A14 440 -> 463 (+23: variance.c :141-158 + :183-190),
   total 20,918 -> 21,347 of 157,718; the printed percentage MOVED:
    13.3% -> 13.5% (21,347/157,718 = 13.5349% crosses the 13.45
    boundary the AN7 13.2629% sat below); the paired conformance count
    unchanged (9 artifacts, all content-1:1).
 - [x] AN9 LANDED (2026-11-01): analysis::isInputLumaDominant, the port
   of svt_aom_is_input_luma_dominant (pic_analysis_process.c:1440-1493)
   with its eight FRAME_LUMA_DOMINANT_* defines (:1431-1438: SAMPLE_STEP
   8, CORE_THR 16, TAIL_THR 18, MIN_CORE_PCT 85, MIN_TAIL_PCT 95,
   NEUTRAL_THR 6, UV_DIFF_THR 4, MIN_NEUTRAL_PCT 75). The detector is
   SELF-CONTAINED (no AN1-AN8 helper calls): it samples every 8th pixel
   of the CHROMA planes over (width>>1) x (height>>1), counts per sample
   chroma_mag_sq = du*du + dv*dv (du = u-128, dv = v-128, uv = u-v,
   uint32 mag) against the core/tail thresholds (<= 256 / <= 324) and
   the neutral box (|du| <= 6 && |dv| <= 6 && |uv| <= 4), then returns
   sample_cnt && core*100 >= sample*85 && (tail*100 >= sample*95 ||
   neutral*100 >= sample*75) (:1490-1492) - a MEAN-COMPARISON-FREE
   percentage test on three nested classes (neutral -> core -> tail:
   the neutral box implies mag <= 72 < 256 <= 324). Caller context
   (read, cited): pd_process.c:4813 passes pcs->chroma_downsampled_pic,
   NOT enhanced_pic, gated on scs->detect_luma_dominant_input
   (:4812); chroma_downsampled_pic is a 4:2:0 buffer desc carrying the
   LUMA dims (pcs.c:1192-1199), which is why the function derives
   uv_w/uv_h by >>1. NAMED DEVIATIONS: the !input_pic ||
   color_format == EB_YUV400 || !u_buffer || !v_buffer guard
   (:1441-1443) is threaded as the monochrome flag (the port cannot
   receive a null buffer; the u/v pointers are parameters); the
   uv_w/uv_h == 0 guard (:1448-1450) is kept but is behavior-redundant
   with the sample_cnt conjunct (dropping it is not observable - the
   empty loop also returns false; the test pins the OBSERVABLE
   behavior). Host only, no GPU twin (parked, named).
   RED (captured): compile error C2039 'isInputLumaDominant': is not a
   member of 'analysis' (test_variance.cpp:1067). GREEN: the port plus
   six test cases: the neutral (luma-heavy) frame TRUE, the
   chroma-heavy frame FALSE, and three 20-sample boundary fixtures at
   80x64 - A: core 17/20 (85%, INCLUSIVE) with tail 18 (90%) and
   neutral 14 (70%) -> FALSE; B: core 17/20 with neutral 15/20 (75%,
   INCLUSIVE) -> TRUE via the neutral branch; C: core 16/20 (80%,
   FAILS) with tail 20/20 PASSING -> FALSE (the core conjunct is
   NECESSARY, not another alternative) - plus the monochrome guard and
   the width-1 case. Fixture construction: per-sample classes via
   u = 140 (du 12, mag 144: core not neutral), u = 145 (du 17, mag
   289: tail not core), u = 200/v = 100 (mag 5968: outside tail);
   classes nest neutral -> core -> tail.
   MUTATIONS (all captured, all reverted): M1 the core/tail
   classification made unconditional (|| true, the /W4 /WX-safe form
   after the first build was INVALID - unused-variable C2220, the
   AN7 stale-binary lesson: the run after an invalid build reads the
   OLD exe) -> 1 fail (the chroma-heavy frame must stay false); M2
   the neutral threshold 75 -> 65 -> 1 fail (fixture A flips TRUE);
   M3 the >= comparisons -> > -> 1 fail (fixture B flips FALSE at
   EITHER inclusive boundary); M4 the core conjunct neutralized
   (|| true, the /W4 /WX-safe form after the first drop was INVALID
   for the same unused-symbol reason) -> 1 fail (fixture C flips
   TRUE); M5 the monochrome guard neutralized -> 1 fail (the
   neutral-chroma monochrome case must not fall through to sampling).
   MEASURED (2026-11-01): suite 10/10 passed, 0 failed, 262.05 s;
   GATE unchanged 884,851 B byte-identical, diff 0 (no generator
   input); coverage A11 665 -> 718 (+53: the :1431-1493 span, 45
   function + 8 defines), total 21,347 -> 21,400 of 157,718; the
   printed percentage MOVED: 13.5% -> 13.6% (21,400/157,718 =
   13.5685% crosses the 13.55 boundary the AN8 13.5349% sat below);
   the paired conformance count unchanged (9 artifacts, all
   content-1:1).
 - [x] AN10 LANDED (2026-11-02): the pad family, ELEVEN functions
    across two reference files. Helper
   kernels from Codec/pic_operators.c (A6-classified, the lines
   charge to A6's rationale): analysis::generatePadding
   (svt_aom_generate_padding, pic_operators.c:266-301),
   analysis::generatePaddingCompressed10bit
   (svt_aom_generate_padding_compressed_10bit, :303-342),
   analysis::padInputPicture (pad_input_picture, :393-436),
   analysis::padInputPicture16bit (svt_aom_pad_input_picture_16bit,
   :441-479) and memset16bit (pic_operators.h:39-45) substituted
   with the same element loop inside padInputPicture16bit. Drivers
   from Codec/pic_analysis_process.c (A11): analysis::
   pad2bCompressedInputPicture (pad_2b_compressed_input_picture,
   :648-739, static), analysis::padToMinBlkSizeDimensions
   (svt_aom_pad_picture_to_multiple_of_min_blk_size_dimensions,
   :745-813), analysis::padToMinBlkSizeDimensions16bit (..._16bit,
   :820-852), analysis::padToSbDimensions
   (svt_aom_pad_picture_to_multiple_of_sb_dimensions, :858-866) and
   analysis::padInputPictures (svt_aom_pad_input_pictures,
   :1549-1615, the full driver). NAMED EXCLUSION, grep-verified:
   svt_aom_generate_padding16_bit (pic_operators.c:348-388) is NOT
   ported - its callers are enc_dec_process.c:1120/1128/1136/
   1183/1191/1199 (reference-picture border padding) and
   resize.c:1068/1072/1080, none inside the pad family's call
   graph. THE CONTRACT (derived from
   resource_coordination_process.c:717-728 and pinned by the
   odd-content fixture): pad_right = MIN_BLOCK_SIZE - (width %
   MIN_BLOCK_SIZE) with MIN_BLOCK_SIZE 8 (definitions.h:2018-2019)
   and max_input_luma_width UPDATED IN PLACE (:719), so every
   driver receives the PADDED width and the padding amount; the
   content width is width - pad_right (pic_analysis_process.c:756).
   The chroma content is (width + ss_x - pad_right) >> ss_x (:764)
   = ceil(content/2) at 4:2:0 and the chroma pad is pad_right >>
   ss_x (:766) = FLOOR(pad/2), NOT ceil - the pair is consistent
   because pad_right = (-content) mod 8 has the parity of content,
   so content/2-ceiling + pad/2-floor = the chroma plane width
   exactly (the brief's "ceil(pad/2)" parenthetical was wrong; its
   own numbers, chroma content 5 pad 3 at content 9 pad 7, are the
   floor ones). THE 2-BIT CORRESPONDENCE (stated as a derivation in
   the commit message): pad_right == 4 iff width % 4 == 0; for
   every other nonzero pad_right width % 4 != 0, so src[last_col]
   (= width/4) is the last PARTIAL content byte whose top bits hold
   the trailing real pixels - each branch masks to keep exactly
   those (0xc0 one pixel, 0xf0 two, 0xfc three) and OR-fills the
   pad slots, plus one fully replicated byte at last_col + 1 for
   pad_right >= 5; the ==4 branch ALONE reads src[last_col - 1]
   (the last full content byte) because src[last_col] there is the
   first pure-pad byte. The subsampling THREE-SOURCE check: the
   8-bit driver derives from color_format (:750-751), the 16-bit
   driver uses a different textual form (:825-826), padInputPictures
   reads scs->subsampling_x/y (:1580-1582); with EbColorFormat =
   {EB_YUV400, EB_YUV420, EB_YUV422, EB_YUV444} = {0,1,2,3}
   (API/EbSvtAv1Formats.h:110) the two DERIVATION forms are
   PROVABLY equivalent for all four members (x: 0 iff cf == 3 in
   both; y: 0 iff cf in {2,3} in both - ">=" over {2,3} and the
   explicit {444, 422} pair are the same set); the port mirrors the
   divergence instead of unifying it, and padInputPictures threads
   BOTH sources (colorFormat feeds only the min-blk driver call,
   the explicit subsampling only the border passes). NAMED
   DEVIATIONS (the b64Variance pattern): scs/EbPictureBufferDesc
   dropped, the raw parameters threaded; the comp strides stay
   internal (y_stride/4, u_stride/4, :780-782/:1556-1557);
   SVT_ERROR null guards (:268-271/:311-314/:404-407) become silent
   returns; the :761/:770/:784/:793/:802/:1565/:1577/:1586/:1597/
   :1606 buffer guards become null-pointer checks; the :728
   svt_aom_assert_err becomes assert() (<cassert>, the l8_bitstream
   pattern); the 16-bit driver's assert drops with the scs
   indirection; the reference casts the uint8_t* desc fields to
   uint16_t*, the port takes uint16_t* directly. Host only, no GPU
   twin (parked, named).
   RED (captured): compile errors C2039 for generatePadding
   (test_pad.cpp:43), generatePaddingCompressed10bit (:161),
   padInputPicture (:203), padInputPicture16bit (:299),
   pad2bCompressedInputPicture (:334), padToMinBlkSizeDimensions
   (:549), padToMinBlkSizeDimensions16bit (the 16-arg call site),
   padToSbDimensions, padInputPictures (:805); behavioral REDs: the
   generatePadding vertical pass (10 failed assertions), each 2b
   branch as it landed (2-4 failed assertions per pad_right), the
   bottom copy count (4), the 16bit chroma (4). THREE fixture bugs
   of mine caught by the loop, all fixed in the TESTS (the ports
   were right): padding_height 2 needs TWO pad rows each side (the
   first vertical fixtures under-allocated and the /GS cookie fired
   0xC0000409 AFTER correct output - diagnosed with an instrumented
   standalone repro), the padToSbDimensions fixture same shape, and
   the padInputPictures/comp fixtures must pass the CONTENT ORIGIN
   (src + border offset), not the array base - passing the base
   made the min-blk pass read poison as the last content pixel.
   GREEN: 28 test cases / 373 assertions in tests/test_pad.cpp (75
   cases + 685 assertions total with test_variance.cpp), CMakeLists
   gains tests/test_pad.cpp.
   MUTATIONS (all captured, all reverted; the Copy-Item restore
   preserves the file timestamp so MSBuild skips recompile - touch
   the file after every restore, the stale-binary lesson in a new
   costume, hit and identified at the M0 revert): M0a generatePadding
   row_bytes -> origWidth + 2*pad -> 4 fails (the overhang sentinel
   must be carried); M1 the ==7 branch mask 0xc0 -> 0xf0 -> 2 fails;
   M2 the ==4 branch read [last_col] instead of [last_col - 1] ->
   2 fails; M3 the ==1 branch mask 0xfc -> 0xf8 -> 2 fails (this
   one SURVIVED before the fixture strengthening - the original
   fixtures gave the last real pixel a value with a zero low bit,
   invisible to mask-widening; every fixture's last real pixel is
   now value 1 = 0b01 so masks are caught in BOTH directions); M4
   the ==3 branch mask 0xc0 -> 0x80 -> 2 fails; M5a the
   padToMinBlkSizeDimensions uBuffer guard neutralized -> NO fail
   (an EQUIVALENT mutant: pad_input_picture's own SVT_ERROR guard
   :404-407 makes the driver guard behavior-redundant in the
   reference too - named, not counted); M5b BOTH guards neutralized
   -> the monochrome test crashes (exit 1, 0 assertions run) ->
   killed.
   MEASURED (2026-11-02): suite 10/10 passed, 0 failed, 261.64 s;
   GATE unchanged 884,851 B byte-identical, 421 lines, diff 0 (no
   generator input); coverage A6 1,393 -> 1,535 (+142: the four
   pic_operators helpers 28 + 36 + 38 + 34 + memset16bit 6), A11
   718 -> 954 (+236: 79 + 59 + 29 + 9 + 60 for the five driver
   spans), total 21,400 -> 21,778 of 157,718; the printed percentage
   MOVED: 13.6% -> 13.8% (21,778/157,718 = 13.8082%); the paired
    conformance count unchanged (9 artifacts, all content-1:1 - the
    pad family is encoder-input prep and emits no bitstream).
 - [x] AN11 LANDED (2026-11-02): analysis::downsampleFilteringInputPicture
    (svt_aom_downsample_filtering_input_picture, pic_analysis_process.c:
    :1498-1547), the HME input-picture downsampling driver - the LAST
    Phase-A slice, and the simplest: a 50-line branch structure that
    COMPOSES two already-landed l9 primitives, downsample2d (AN6) and
    generatePadding (AN10). Declaration twin pic_analysis_process.h:28
    (same file set as the body - no cross-area question). resize.c:139
    carries the same signature and is a DECLARATION (semicolon at :141,
    no body) - grep-verified and stated in the port comment; the
    reference's only definition is :1498. THE CONTRACT: outer gate
    enable_hme || tf_enable_hme (:1502); the level1 branch (:1503-1517)
    runs downsample_2d input->quarter at step 2 (:1504-1510) then
    generate_padding with the QUARTER's own dims and border (:1511-1516);
    the level0 branch (:1519-1545) selects the sixteenth SOURCE off the
    level1 pair read a SECOND time - step 2 from the QUARTER buffer when
    on (:1522-1528), step 4 from the INPUT when off (:1530-1536) - then
    generate_padding with the SIXTEENTH's own dims and border
    (:1539-1544). The six pcs booleans are threaded raw with the ORs
    inside (the b64Variance interface-flattening pattern); the reference
    has NO null guards - the branch structure IS the guard, none added.
    Callers (grep-verified): pd_process.c:3555 and
    pic_analysis_process.c:1992 (gated on !scs->allintra) are LIVE;
    resize.c:1665/:1726 (Phase D) and temporal_filtering.c:3770 (Phase C)
    are parked; all pass the same three-buffer shape, the flags live
    inside pcs. ITEM-2 FINDING (the brief's order question, answered):
    the sixteenth-from-quarter pass reads NO quarter pad rows -
    downsample2d's window sits at rows/cols {half-1, half} WITHIN each
    cell over the quarter CONTENT dims (the pre-advance curLine = src +
    half*stride; prevLine = curLine - stride = content row 0 at the
    first cell), so every touched row/col is content; the padding ORDER
    (quarter pad BEFORE the sixteenth pass, :1511 before :1522) is
    therefore pinned by the reference TEXT, not by any observable test -
    swapping the two statements changes no output. Recorded in the port
    comment as text-pinned-not-test-pinned.
    RED (captured): compile error C2039 at test_downsample_filtering.cpp
    :146 (the missing declaration), then behavioral REDs per branch: T2
    quarter byte 66 actual 238 (poison) vs expected 3 (the first left-pad
    cell of the top pad row), T3 sixteenth byte 63 actual 238 vs
    expected 8, T4 sixteenth byte 63 actual 238 vs expected 8 (the
    decim4 first cell = 8, by the reference's own middle-window formula).
    GREEN: 8 test cases / 5704 assertions in tests/test_downsample_
    filtering.cpp (CMakeLists gains the file): T1 all-flags-off no-op,
    T2 hme+level1 (quarter written+padded, sixteenth untouched), T3 the
    full chain (sixteenth = step-2 of the QUARTER content, own border),
    T4 level0 with level1 OFF (sixteenth = step-4 of INPUT, the quarter
    buffer poisoned AND unread), T5 both hme flags off with all levels
    on (writes nothing), T2b/T4b the tf-side twins of T2/T4, and T6
    level1-via-TF-only (level0 on via hme): BOTH level1 reads (:1503 and
    :1521) must see it - the quarter pass runs AND the sixteenth takes
    the step-2-from-QUARTER source. Full-buffer compares against
    constructed expected planes (poison outside the padded extent), the
    quarter fixture sized with border slack so a wrong-border mutant
    writes INSIDE the buffer instead of off its end.
    MUTATIONS (all captured, all reverted, every run on a build VERIFIED
    error-free): M1 sixteenth source input-step-2 when level1 on -> 2
    fail (T3, T6); M2 sixteenth source quarter-step-4 when level1 off ->
    2 fail (T4, T4b); M3 quarter padding dropped -> 4 fail (T2, T2b, T3,
    T6); M4 sixteenth padding dropped -> 4 fail (T3, T4, T4b, T6); M5
    sixteenth padding given the quarter border -> 4 fail (T3, T4, T4b,
    T6); M7 the hme gate dropped -> 1 fail (T5, 720 failed assertions);
    M8 the tf side of the outer gate dropped -> 2 fail (T2b, T4b);
    M9-outer the tf side of the OUTER level1 OR dropped -> 2 fail (T2b,
    T6); M9-inner the tf side of the INNER source OR dropped -> 1 fail
    (T6); M10 the tf side of the level0 OR dropped -> 1 fail (T4b); M11
    the inner source gate swapped to the level0 pair -> 2 fail (T4,
    T4b). THREE instrument lessons, each a known pattern in a new
    costume: (1) STALE MUTANT BINARY - commenting out a padding call
    makes its params unreferenced, the /WX build FAILS, and the previous
    binary remains in place, so the run LOOKS like a capture while
    measuring nothing (the AN10 Copy-Item timestamp lesson one level
    up); fixed by verified builds + a rebuild-verify after every revert.
    (2) The Edit tool matched on CONTENT ignoring indentation: an
    8-space oldString landed on the 12-space INNER gate instead of the
    8-space outer one (both read `if (level1Enabled || tfLevel1Enabled)
    {`), so the mutation ran against the wrong gate - the surviving T2b
    run was the tell; context-pinned edits (neighbour line included)
    from then on. (3) LINEAR FIXTURE BLINDNESS - the AN10 mask lesson
    again: the first input fixture (x + 4y) made the two-stage
    step-2-of-quarter decimation BIT-IDENTICAL to the one-stage step-4
    middle-window decimation at every cell (a linear field averages
    exactly), so T6 could not tell the two sixteenth sources apart and
    M9-inner SURVIVED it; the input is now non-linear (x*37 + y*11 +
    ((x*y) & 0x1F)) and every mutation above is re-run against the FINAL
    fixture.
    MEASURED (2026-11-02): suite 10/10 passed, 0 failed, 263.27 s;
    GATE unchanged 884,851 B byte-identical, 421 lines, diff 0 (no
    generator input); coverage A11 954 -> 1002 (+48: the :1498-1547
    span, 50 physical / 48 non-blank), total 21,778 -> 21,826 of
    157,718; the printed percentage does NOT move: 21,826/157,718 =
    13.8386% stays below the 13.85 boundary, so 13.8% prints again; the
    paired conformance count unchanged (9 artifacts, all content-1:1 -
    the HME driver is encoder-input prep and emits no bitstream).
    Host only, no GPU twin (parked, named).
    PHASE A COMPLETE: the scout priced Phase A at ~1,185 ref lines ->
    ~+0.75 pt from the 13.1% baseline (20,659); the measured Phase-A sum
    is 21,826 - 20,659 = 1,167 lines = +0.738 pt - the arithmetic HELD
    (the 0.012 gap is the scout's own rounding). Derived from the
    measured runs above, not promised.
   FOLLOW-UP CLOSED (2026-11-02, this entry's flagged gap): the
   AN10 report's stale last-measured block in AGENTS.md was
   refreshed by the doc-currency commit that closes it - ctest
   re-measured 10/10 at 266.17 s, GATE re-measured unchanged, and
   the coverage headline carries the AN10 numbers above.

 - [x] AN12 LANDED (2026-11-02): analysis::fft1d2, fft1d4, fft1d8,
    fft1d16, fft1d32 + ifft1d2, ifft1d4, ifft1d8, ifft1d16, ifft1d32
    (svt_aom_fft1d_2/4/8/16/32_float at fft_common.h:113-414 and
    svt_aom_ifft1d_2/4/8/16/32_float at fft_common.h:416-823, macros
    GEN_FFT_2..GEN_FFT_32 and GEN_IFFT_2..GEN_IFFT_32; the entry-point
    instantiations are fft.c:82-86 (fwd) and fft.c:173-177 (inv); the
    fwd declarations fft_common.h:49-52 are the only header twins, the
    ifft family declares none - grep-verified). Phase B opens, the first
    FLOAT slice in the layer: every preceding primitive is fixed-point;
    these are IEEE-754 single-precision host math with no rounding mode
    of their own and the port mirrors the reference expression graph
    exactly (the macros' scalar float path: T_VEC = float, load = point
    read, store = point write, add/sub/mul = the float operators).
    N-SET FOUND: {2, 4, 8, 16, 32} for BOTH families; no other N exists
    in the reference. The brief's anchor fft_common.h:114-330 was a
    scout error - the GEN_FFT_32 macro alone spans :246-414; corrected
    here. The brief's "cos/sin tables" do not exist as tables: the
    twiddles are inline 6-decimal float literals (k_weight2..k_weight8).
    TOLERANCE DISCIPLINE (the first float decision, documented in the
    test header and inherited by AN13-AN15): EPSILON = 1e-4 ABSOLUTE,
    not bit-equality. WHY: the kernels are pure float expression graphs
    with no libm call anywhere in :113-823 (structural check), but the
    twiddle constants are 6-decimal truncations (<= 4.7e-7 error each)
    that accumulate over butterfly depth; the fixtures assert the port
    against full-precision DFT-definition values, so bit-equality is
    impossible by construction. 1e-4 covers the truncation amplitude at
    the fixture magnitudes (O(1)..O(32); measured worst port-vs-
    definition delta 1.01e-5 at N=16 r_1, 10x margin). Held for every
    per-transform assertion; NO per-test variation. ONE DERIVED
    exception, documented in the test: the fft->ifft ROUND-TRIP fixture
    re-sums the forward's truncation noise across n terms in the inverse
    butterfly, so its bound is n * kEpsilon (measured deltas 0 / 0 /
    9.5e-6 / 1.5e-4 / 2.4e-4 at N = 2/4/8/16/32; the N=16/32 deltas
    EXCEED 1e-4 exactly as derived, every delta >= 10x below n * 1e-4).
    RED (captured): fwd - the three generic fixtures' FIRST hand-derived
    expected vectors were WRONG (incomplete derivations) and the suite
    fired on them - a finding, not papered over; the falsifier caught
    THREE fixture bugs while the port was right (verified against an
    independent full-precision DFT instrument: worst delta 1.01e-5).
    The corrected fixtures are instrument-derived, documented as such.
    inv - the missing-definition RED: all five ifft1dN definitions
    renamed, rebuild fails to resolve the symbols, then implementation
    landed. Round-trip - the N=16/32 round-trip fixtures FAILED with
    deltas 1.5e-4 / 2.4e-4, measured, diagnosed as the derived n*kEpsilon
    bound (above), fixture tolerance corrected to the DERIVED bound.
    GREEN: 108 test cases / 6422 assertions, 0 failed, in tests/
    test_fft1d.cpp (CMakeLists gains the file): per N in {2,4,8,16,32} -
    impulse (all ones -> DC only), sinusoid at bin 1 (energy in exactly
    one packed slot; N=16 r_1 = 8, N=32 r_1 = 16), generic non-symmetric
    (every slot differs, instrument-derived), plus stride fixtures at
    N=2, the ifft DC-impulse family (packed [N,0..] -> N everywhere),
    the ifft definition-derived generic spectra (hand-chosen packed
    vectors NOT the forward of any signal), and the round-trip
    composition per N. 1e-4 standing, n*1e-4 round-trip only.
    MUTATIONS (all captured, all reverted, all against the FINAL fixture
    set, every rebuild verified error-free): M1 fft1d8 twiddle
    0.707107f -> 0.707f -> 3 fail (:159 generic, :179 sinusoid, :477
    round-trip N=8); M2 fft1d4 re/im swap output[1]<->output[3] -> 3
    fail (:113 sinusoid, :126 generic, :470 round-trip N=4); M3 fft1d8
    slot-1 twiddle mul dropped -> 3 fail (:159 :179 :477); M4 ifft1d8
    store-7 add->sub -> 2 fail (:402 ifft generic, :477 round-trip);
    M5 fft1d32 twiddle 0.980785f -> 0.9807f -> 3 fail (:297 :336 :501);
    M6 fft1d16 store-9 w16[1] -> w16[0] -> 3 fail (:230 :320 :487).
    Every mutation caught by at least one named fixture; none survives.
    MEASURED (2026-11-02): suite 10/10 passed, 0 failed, 275.59 s (an
    earlier run of the same tree measured 269.63 s);
    GATE unchanged 884,851 B, 421 lines, byte-identical, diff 0 (no
    generator input); coverage A11 fraction 0.0833 -> 0.1418 (+704
    non-blank ref lines: the 298 GEN_FFT_* + 406 GEN_IFFT_* macro
    bodies; the 10 fft.c entry-point instantiation lines are noted in
    the rationale but not counted - our names differ), total
    21,826 -> 22,530 of 157,718; the printed percentage MOVED: 13.8%
    -> 14.3% (22,530/157,718 = 14.2841% crosses the 14.25 boundary);
    the paired conformance count unchanged (9 artifacts, all
    content-1:1 - the FFT kernels emit no bitstream). Host only, no GPU
    twin (parked, named - the scout marked [G] possible).

  - [x] AN13 LANDED (2026-11-03, PERRY): analysis::fft2d2, fft2d4, fft2d8,
    fft2d16, fft2d32 + ifft2d2, ifft2d4, ifft2d8, ifft2d16, ifft2d32 in
    src/l9_analysis/fft2d.cpp, composing the LANDED AN12 1D kernels - the
    port never re-implements a 1D butterfly. Reference:
    third_party/SVT-AV1/Source/Lib/Codec/fft.c (189 nb / 237 phys):
    simple_transpose :15-21, unpack_2d_output :33-49 (the 10-line contract
    comment :23-32), svt_aom_fft_2d_gen :51-64 (the fwd driver), the five
    GEN_FFT instantiations :66-86 + five fwd entries :88-106,
    svt_aom_ifft_2d_gen :108-171 (the inv driver - a REAL/IMAG
    split-and-pack rearrangement, NOT a 5-pass mirror; the brief's
    five-pass inverse reading was wrong and is corrected here), the five
    GEN_IFFT instantiations :173-177 + five inv entries :179-237. The
    fwd driver IS the five-pass composition: column tform (stride n),
    simple_transpose, second-axis tform, simple_transpose, unpack.
    NORMALIZATION FINDING: fft.c carries zero arithmetic divisions
    (grep-verified: the only '/' hits are n/2 index halvings and comment
    text); noise_util.c:96-102 noise_tx_inverse does ifft(...) THEN
    data[i] /= n - the normalization lives at the CALLER, the port stays
    1:1 unnormalized, and the 2D roundtrip ifft2d(fft2d(f)) = n*n*f is
    pinned by fixtures at tolerance n*n*kEpsilon (the AN12 roundtrip
    discipline inherited). THE UNPACK CONTRACT: output[2*(y*n+x)] =
    Re F(y,x), output[2*(y*n+x)+1] = Im F(y,x) for ALL y but ONLY x in
    0..n/2 - n*(n/2+1) complex values in a 2*n*n float buffer whose tail
    is never written (noise_util.c:65 caller zeroes it). POISON DERIVATION
    (fixture-relevant): the working passes (pass 1/3 write the full n*n
    floats of output) clobber the first n*n floats, so the slots assertable
    as never-written poison are exactly the unpack-unwritten pairs
    (y,x) with 2*(y*n+x) >= n*n; at n=4 those are {22,23,30,31} (the
    fixture first asserted {6,7,14,15} too and RED fired - corrected).
    vec_size = 1 at every reference _c entry; fft_single == fft_multi at
    vec_size 1 (fft.c:136-138 loop is empty) - both folded with named
    notes. RED (captured): fwd - LNK2019 unresolved analysis::fft2d2
    (skeleton test, no definitions); then two fixture defects the suite
    caught (AN12 lesson): the unpack-tail poison set was over-broad
    ({6,7,14,15} are pass-3-written) and the rank-1 expected vector
    missed the y_extra mirror slot F(3,1)=4 at index 26. GREEN: 115
    test cases / 6436 assertions, 0 failed, in tests/test_fft2d.cpp
    (CMakeLists gains fft2d.cpp + the file): per N in {2,4,8,16,32} -
    impulse all-ones (DC only, poison-checked tail), the n=4 rank-1
    separability fixture (outer product -> F(1,1)=4 AND mirror F(3,1)=4),
    the roundtrip n*n*f at every N. MUTATIONS (all captured, all
    reverted, all against the FINAL fixture set): M1 swap the two
    simpleTranspose calls -> 8 named fixtures fail; M2 drop the unpack
    cross terms -> 8 + warning C4189 for the dead xExtra; M3 wrong
    stride in the first tform pass (1 instead of n) -> 8; M4 normalize
    in the wrong place (fwd driver, before the unpack) -> rank-1 +
    poison checks. Every mutation caught by at least one named fixture;
    none survives. MEASURED (2026-11-03): GATE unchanged 884,851 B,
    421 lines, byte-identical, diff 0 (analysis-only code, no generator
    input); coverage A11 fraction 0.1418 -> 0.1576 (+190 non-blank ref
    lines, recount done by direct non-blank measurement 2026-11-03 - the
    scout's 189 estimate is corrected: the fft.c 2D span :15-171 minus the
    10 macro-instantiation lines :82-86/:173-177, PLUS the five inv entry
    wrappers :179-237 (55 nb, the reference lists its six function-pointer
    args over eleven lines each) = :15-65 45 + :66-81 12 + :87-106 15 +
    :108-171 63 + :179-237 55 = 190; :82-86 and :173-177 stay NOT counted
    per the AN12 ruling - our per-size names have no instantiation shell).

  - [x] AN16 LANDED (2026-11-03, PERRY): the noise strength solver - the
    WHOLE noise_model.c:104-397 block (267 nb, measured) ported into
    src/l9_analysis/noise_model.cpp + the struct/enum surface in
    analysis.h: equation_system_free/clear/copy/init/solve (:104-166),
    noise_strength_solver_clear/copy (:168-178), num_coeffs (:181-190,
    DIAMOND lag*(lag+1) / SQUARE (n*n)/2 with the reference's INTEGER
    division), noise_state_init (:192-201, k_num_bins 20, ar_gain 1.0),
    set_chroma_coefficient_fallback_soln (:203-212), the lut init/free
    pair (:214-234), get_bin_index (:236-240), get_value (:242-248),
    add_measurement (:250-265), solver_solve (:267-303), solver_init/
    get_center (:305-322), update_piecewise_linear_residual (:327-351)
    and fit_piecewise (:353-397). The linsolve DEPENDENCY (Scout B3):
    mathutils.h:22-65 (43 nb, A13-classified) ported as the port's own
    copy in the l9 TU, exposed in the analysis namespace for the tests
    (named deviation - the reference keeps it static INLINE per-includer);
    fclamp (definitions.h:696-698, 3 nb) private in the TU. STRUCTS: the
    four POD carriers (AomEquationSystem/AomNoiseStrengthLut/
    AomNoiseStrengthSolver/AomNoiseState, noise_model.h:30-35/:41-44/
    :69-76/:166-174) with the malloc/free pointers as std::vector (named
    deviation; allocation cannot fail so the init/free failure branches
    are dead) - the solve()'s A-POINTER SWAP dance (:275-:299) becomes a
    local vector copy + assign + restore, and fit's memmove becomes
    vector::erase, both named. TOLERANCE: doubles throughout, harness
    tolerance 1e-12 RELATIVE documented in the test header (the
    AN12/AN13 discipline); every expected value is a hand-derived closed
    form over the reference's own equations, never port output. THE
    REGULARIZATION MATH (fixture-load-bearing): the k_alpha Laplacian has
    row sums zero at EVERY row (boundary rows fold i_lo/i_hi onto the
    diagonal), so CONSTANT strength curves are in its kernel and the
    +1/8192 diagonal cancels against the +mean/8192 b term - T8 recovers
    a constant 7.0 EXACTLY through the full solve. RED/TEST FINDINGS:
    (1) T9's first design (symmetric two-endpoint, stds 2/4) is EXACTLY
    k_alpha-INVARIANT - the closed-form derivative (1+e)[e(s0+s2) - 2t]/
    det^2 with t = mean*e = (s0+s2)/2*e vanishes identically; MEASURED:
    T9 PASSED under k_alpha 2.0 -> 3.0. Redone ASYMMETRIC (std 2 once at
    bin 0, std 8 thrice at bin 2, d2 = 3, mean = 6.5; general Cramer form
    x1 = (PSt + Pk(s2+t) + kS(s0+t))/det) - the mutation now fires.
    (2) The survival condition is avg = r*(255/num_bins)/
    (points[min+1][0] - points[min-1][0]) - the residual's dx (255/
    num_bins, HARDCODED 255, NOT max_intensity) and the lut-span dx are
    DIFFERENT factors; the brief's r-threshold estimates had to be
    re-derived through both. (3) T17's first fixture carried a
    hand-arithmetic defect (b = [7,5] for x = [3,1] under A = [[2,1],
    [1,3]]; the correct b is [7,6]) - the suite caught it, the port was
    right. (4) A tail-anchored edit adding clear/copy silently DELETED
    updatePiecewiseLinearResidual + fitPiecewise (LNK2019 caught it;
    restored verbatim). GREEN: 140 test cases / 7015 assertions,
    0 failed, in tests/test_noise_strength.cpp. Named quirks pinned:
    solve() restores A bit-for-bit but LEAVES the +mean/8192 b write in
    place (the reference modifies solver->eqns.b outside the copied-A
    protection - a second solve accumulates again; T18); the stale
    residual entries beyond the recomputed window are kept and re-read
    exactly as the reference leaves them (structure ported; T19 pins the
    smallest-residual-first order and the windowed recompute). MUTATIONS
    (14, all against the FINAL fixture set, all reverted, all killed by
    named fixtures): kAlpha 2->3 -> T9; diag 1/8192 dropped -> T8b+T9;
    b mean/8192 dropped -> T9; num_coeffs formulas swapped -> T4a/T4b;
    get_value interp swap -> T7; fclamp dropped -> T6; add_measurement
    b-weight swap -> T13; tol x2 -> T11b; tol /2 -> T11c; dx
    255->max_intensity -> T12 (10-bit, the brief's named killer); largest-
    first removal order -> T19; recompute-after-removal skipped -> T19;
    singular guard disabled -> T2; pivoting disabled -> T3. MEASURED
    (2026-11-03): GATE unchanged 884,851 B, 421 lines, byte-identical,
    diff 0 (analysis-only code, no generator input); coverage A11
    fraction 0.1576 -> 0.1798 (+267 nb = the whole span) and A13
    0.05 -> 0.0586 (+46 nb: linsolve 43 + fclamp 3); total
    14.5% -> 14.7% (23,248 / 157,718; the committed pre-AN16 state
    already carried A8's DF1 +215, so the AN16-attributable delta is
    +313). Host only, no GPU twin (parked, named - a 20-bin dense double
    least-squares solve is a poor GPU fit and the AN12/AN13 GPU parking
    covers the family).

  - [x] AN17 LANDED (2026-11-04, PERRY): the flat-block finder + the noise
    model init/update - the noise_model.c spans :27-:102 (the
    GET_BLOCK_MEAN / GET_NOISE_VAR helper family, 69 nb), :399-:634 (the
    finder, 218 nb) and :636-:1157 (the model, 460 nb) ported into
    src/l9_analysis/noise_model.cpp + the struct surface in analysis.h.
    FINDER: flatBlockFinderInit (:399-:454, A rows [yd, xd, 1] with
    yd = (y - bs/2.)/(bs/2.), at_a_inv = inv(A^T A) via THREE
    equation_system_solve calls against unit vectors - composing AN16's
    solver, never re-implemented), flatBlockFinderFree (:456-:463),
    flatBlockFinderExtractBlock (:465-:508, the kLowPolyNumParams = 3
    specialized multiply_mat_1_n_3/_3_3_1/_n_3_1 path from
    noise_model.h:322-:366, plane fit then block -= plane), the
    IndexAndscore + compare_scores pair (:510-:518, file-local) and
    flatBlockFinderRun (:520-:634: interior gradient covariance g_xx/g_xy/
    g_yy + var over the (bs-2)^2 window, trace/det/e1/e2, norm = e1,
    ratio = e1/max(e2, 1e-6), is_flat = trace < 0.15/1024 && ratio < 1.25
    && norm < 0.08/1024 && var > 0.005/n, the sigmoid weights
    {-6682, -0.2056, 13087, -12434, 2.5694}, score forced 0 when
    var <= k_var, flat_blocks = is_flat ? 255 : 0, qsort ASCENDING and the
    top-10th-percentile union flat_blocks[index] or-equals 1 for every
    score >= scores[num*90/100].score). MODEL: noiseModelInit (:636-:697,
    kMaxLag = 4 gates, three channels x combined+latest at n + (c > 0)
    columns, the diamond/square coords walk), noiseModelFree (:699-:714),
    extractArRow (the EXTRACT_AR_ROW lowbd instantiation :719-:756),
    addBlockObservationsInternal (:759-:803, the unrolled-by-8
    normal-equation accumulate, accumulation order preserved),
    addBlockObservations (:805-:890, the y/x start-end walk: y_start/
    x_start drop to 0 on a flat left/upper neighbour, x_end = min(w-side -
    lag, flat-right ? block : block - lag) - the :845-:849 branch),
    addNoiseStdObservations (:892-:956, the num_samples > block_size gate,
    block_mean from the ALT luma plane at (x_o << sub, y_o << sub) with
    the FULL block_size, noise_var over the (w >> sub, h >> sub) plane,
    luma_strength = luma_gain * get_value(luma_solver, block_mean),
    uncorr_std = sqrt(max(noise_var/16, noise_var - (corr*luma_strength)^2)),
    adjusted = uncorr_std / ar_gain), arEquationSystemSolve (:958-:996,
    var = mean diagonal/num_observations over n - is_chroma entries,
    sum_covar with the chroma b correction bi -= A[i][n-1]*x[n-1],
    noise_var = max(var - sum_covar, 1e-6), ar_gain = max(1,
    sqrt(max(var/noise_var, 1e-6)))), noiseModelUpdate (:1019-:1145, the
    block_size gates, the >1-flat-block gate, per-channel
    add/solve/std/solve then the combined OVERWRITE from latest +
    re-solve - the combined state is REPLACED each update, not
    accumulated, ported as-is) and noiseModelSaveLatest (:1147-:1157).
    DEVIATIONS (named): the EB_MALLOC_ALIGNED/malloc scratch becomes
    std::vector and the allocation-failure branches are dead (:410-:416,
    :539-:545, :667-:671, :823-:826, :830-:834); the NOISE_MODEL_LOG_SCORE
    debug blocks (:547-:549, :605-:607, :611-:613, :615-:617) dropped; the
    highbd instantiations of all three macro families (:54, :92, :757) and
    the use_highbd dispatcher branches (:58-:60, :97-:100, :852-:876 = 25
    nb) are PARKED (8-bit discipline, use_highbd threads through the
    params but only the lowbd path exists); the coords malloc becomes
    vector<array<int32_t,2>>; the structs stay in analysis.h; the :29-:36
    pack2d extern declarations are NOT AN17's family (they resolve to
    pic_operators.c and are unclaimed). THE QSORT QUESTION (Scout B3):
    verified from the code - the union marks by score >= threshold where
    the threshold is scores[top_nth].score, so every tie AT the boundary
    is marked regardless of qsort's tie order; the map is tie-invariant,
    pinned by T5c (three distinct + 17 identical zero scores, map
    bit-for-bit). THE ZERO-SCORE EDGE (Scout B3's derivation, pinned):
    var = 0 -> is_flat = 0 AND score = 0 -> threshold = 0 -> EVERY block
    or-equals 1 (T5b; a single ramp block returns 1 with flat[0] == 1).
    RED/TEST FINDINGS: (1) the g_xx == g_yy bitwise + g_xy == 0 fixtures
    (the a=2 cross-checkerboard, the (2,2) cross) drive the reference's
    OWN sqrt(trace*trace - 4*det) into the NaN domain through cancellation
    rounding (det = fl(gxx*gxx) vs trace^2 = (2gxx)^2 round
    independently; the sub-ulp difference goes negative) - is_flat reads
    false and the score goes NaN; NOT pinned as a fixture (qsort on NaN
    is implementation-defined) and avoided by construction: every flat
    fixture keeps g_xy != 0 (T5d) or trace == 0 exactly (the checkerboards,
    where sqrt(0 - 0) = 0 is exact). (2) THREE hand-derivation defects
    caught by the suite, all test-side: the T4 fixture data (row [10,20,
    20,20] instead of [0,0,10,20] - the clamped reads never touched the
    intended pixels), T8's A[2][1] expectation (60, not 12) and T12's
    normalization (A entries carry ONE factor 1/255^2 via buffer_norm, so
    x = [0, 1] and ar_gain = sqrt(16.5/(65025*1e-6)) = 15.93, not the
    /255^4 pair first derived). (3) The Edit tool fuzzy-matched a tail
    anchor twice: once deleting updatePiecewiseLinearResidual +
    noiseStrengthSolverFitPiecewise from noise_model.cpp (LNK2019 caught
    it, restored verbatim) and once replacing T5b's flat[0] == 1 with
    == 255 (the suite caught it) - tail anchors now carry unique
    context. MUTATIONS (6, all run against the final fixture set, all
    reverted, all killed by NAMED fixtures): sigmoid w0<->w2 swap ->
    T5e (the marked set flips {Y,Z} -> {Z,X}: Y 1 -> 0); w0<->w1 and
    w1<->w2 swaps -> T5c (every special-block score saturates to 0 or 1
    and A gets marked - map differs); k_trace 0.15 -> 0.05 -> T5d block 0
    (trace 1.26618e-4 > 4.8828e-5 -> flat[0] 255 -> 1); k_ratio 1.25 ->
    2.5 -> T5d block 1 (ratio 1.55192 < 2.5 -> flat[1] 1 -> 255);
    90th-percentile index 90 -> 80 -> T5c (threshold 0 marks all 20);
    EXTRACT_AR_ROW coordinate order swap -> T7 (up != left residuals);
    the :845-:849 flat-right branch removed -> T9 fixture 2 (98
    observations -> 91). MEASURED (2026-11-04): ctest 11/11 passed,
    0 failed, 266.52 s; GATE unchanged 884,851 B, byte-identical, diff 0
    (analysis-only code, no generator input); coverage A11 fraction
    0.1798 -> 0.2351 (2163 -> 2828, +665: 664 nb claimed of the 747
    in-scope, the 83 parked lines itemized above) and the total
    14.8% -> 15.2% (24,008 / 157,718); cite_check 500 citations, 489
    resolved, 0 out-of-range (11 unresolved = the known external decoder
    set); doc_audit PASS (exit 0; the 3 candidates it flags are dated
    historical run records - 5ce0224's own GATE figure 884,953 B and the
    AN4/CS-era percentages - correct for their own commits). Host only,
    no GPU twin (parked, named - the finder is a per-block double
    least-squares fit and the solver family is the AN16 parking).

  - [x] **A8-CAMPAIGN OPENED (2026-11-03, PERRY-A8/DF1): the deblocking
    kernel family is ported in src/l10_filters (l10_filters:: namespace,
    ctest target l10_filters - the suite went 10 -> 11 targets).** The
    user set the campaign target at 20% portable-C coverage; scout F
    measured the path and the A8 kernel campaign is its core, the
    deblocking kernels being the easiest pure-math block. **THE
    PAIRED-METRIC CAVEAT (verbatim, applies to every A8-kernel slice):**
    this slice is COVERAGE-ONLY - it moves no conformance count, touches
    no bitstream (the filter enable bits stay literal 0), and must not be
    read as progress toward decoder conformance. The A8 driver wiring
    that WOULD move conformance is a separate, larger campaign,
    deliberately not opened. **THE DF1/DF2 SPLIT:** DF1 (this slice) =
    the 8-bit mask/flat/hev gates, the filter4/6/8 cores, the lpf 4/6/8
    h+v entry points and update_sharpness; DF2 (a later commit) =
    filter14 + mb_lpf_*_edge_w + lpf_h/v_14_c (:786-:889, 95 nblk).
    Parked by name inside DF1's range: highbd_flat_mask3_chroma
    :194-:203, highbd_filter_mask2/filter_mask/flat_mask4 :388-:430, the
    :431-:567 and :591-:780 highbd blocks, signed_char_clamp_high
    :34-:44. Reference: third_party/SVT-AV1/Source/Lib/Codec/
    deblocking_common.c (782 nb; the 8-bit pure surface is 308 nblk, DF1
    scope 213 nblk per the brief). Every anchor was re-verified against
    the tree: all 16 spans match (filter_mask2 :152-:158, filter_mask
    :160-:171, filter_mask3_chroma :173-:182, flat_mask3_chroma
    :184-:192, flat_mask4 :205-:215, hev_mask :218-:223, filter4
    :225-:251, lpf_horizontal_4_c :253-:267, lpf_vertical_4_c :269-:283,
    filter6 :285-:299, filter8 :301-:317, lpf_horizontal_6_c :319-:335,
    lpf_vertical_6_c :337-:353, lpf_horizontal_8_c :355-:371,
    lpf_vertical_8_c :373-:386, svt_aom_update_sharpness :568-:589;
    MAX_LOOP_FILTER 63 at definitions.h:1652, SIMD_WIDTH 16 at :1653,
    LoopFilterThresh at :1678-:1682, lfthr[MAX_LOOP_FILTER+1] at :1685).
    NO anchor correction was needed - the scout's tables all verified.
    **MEASURED span counts (non-blank): the listed DF1 spans sum to 212
    nblk (78+28+60+28+18), and the port additionally carries the
    signed_char_clamp helper :30-:32 (3 nblk) that filter4 requires, so
    the ported reference surface is 215 nblk; the brief's "~213" sat
    between the two and the coverage fraction was set from the measured
    215.** RED evidence: the first slice's build failed with
    "error C1083: Cannot open include file: 'filters.h'" (the
    missing-layer compile failure), then each function landed behind a
    failing assertion or unresolved-symbol link (LNK2019 x9); three
    fixtures fired on WRONG hand-derivations or non-discriminating shapes
    of mine and were corrected as findings, not papered over (the filter8
    op2 derivation 701->601, i.e. 88->75; the filterMask six-tap
    enumeration rows 5/6 initially re-breached q1-q0; the filter6
    flat-path fixture was NOT discriminating on flat-domain pixels -
    fallback and 5-tap coincide there - and was replaced by an asymmetric
    instrument-driven fixture; the F3 outer-adjustment fixture passed on
    first build and is declared the named killer-fixture for the
    outer-adjustment mutations). GREEN: 32 test cases / 1209 assertions,
    0 failed (tests/test_filters.cpp; av1_l10_filters_tests). MUTATIONS
    (all captured against the FINAL fixture set, all reverted, all killed
    by named fixtures): M1 filter6 tap swap (q0*3+...+p2) -> 3 fail
    (filter6 flat + lpfHorizontal6 flat col + lpfVertical6 flat row);
    M2 scc low clamp -128 -> -127 -> 2 fail (signedCharClamp fixture);
    M3 hev_mask inverted (~hev) -> 11 fail (hevMask fixture + F2 + the
    F2-derived lpf patterns); M4 mblim 2*(lvl+2) -> 2*lvl -> 5 fail
    (both updateSharpness fixtures); M5 lpfHorizontal4 ++s -> s += pitch
    -> 4 fail (per-column fixture); M6 filterMask2 blimit *2 dropped ->
    1 fail (blimit-line fixture). Port-shape deviations (named):
    blimit/limit/thresh flattened from const uint8_t* (SIMD_WIDTH
    element-[0] reads) to scalar uint8_t parameters; explicit
    static_cast<int8_t> narrowing casts added where the reference relies
    on implicit int8 assignment (MSVC /W4 hygiene; semantics unchanged);
    signedCharClamp exported (file-local static INLINE in the reference)
    so the clamp CONSTANTS are directly testable; LoopFilterInfoN carries
    the lfthr table only (lvl[] belongs to the loop_filter_frame_init
    driver). MEASURED (2026-11-03): ctest 11/11 (the time is in the
    commit message); GATE byte-identical, diff 0 (no generator input);
    coverage A8 0.00 -> 0.0203 (+215 non-blank ref lines: 212 DF1 spans
    + 3 scc), total 22,530 -> 22,745 of 157,718; the printed percentage
    MOVED: 14.3% -> 14.4% (22,745/157,718 = 14.4242%); the paired
    conformance count UNCHANGED (9 artifacts, all content-1:1) - exactly
    the coverage-only shape the caveat names.
  - [x] **A8/DF2 (2026-11-03, PERRY-A8/DF2): the 14-tap family landed**
    (filter14 :786-:809, mb_lpf_horizontal_edge_w :811-:847,
    lpf_horizontal_14_c :849-:852, mb_lpf_vertical_edge_w :854-:885,
    lpf_vertical_14_c :887-:889; measured 23+35+4+30+3 = 95 nb, the
    DF1/DF2 split's promised span, deblocking_common.c 782 nb -> A8 at
    310 nb). Anchors re-verified against the tree; the COUNT SEMANTICS
    were pinned from the code, not the ambiguous brief: the horizontal
    entry passes count=1 and mb_lpf_horizontal_edge_w loops
    step*count = 4 columns (step=4 folded in), the vertical entry passes
    count=4 rows. The else chain STACKS filter14 -> filter8 -> filter4
    composed from the DF1 primitives (the mb_* edge functions are
    file-local static in the port, as in the reference). RED evidence:
    C2039 filter14 (S1), 11 behavioral REDs on the 13-tap lines (impulse
    fixtures: p6 impulse for the op side, q6 impulse for the oq side -
    oq0 has NO p6 tap so the p6 impulse cannot discriminate it; oq0 has
    p5..p0+q0..q6), C2039 lpfHorizontal14, LNK2019 lpfVertical14; three
    tests passed by design and were proven discriminating by mutation
    (op6/oq6-untouched via a stray-write mutation; mask=0 via dropping
    filter4's mask gate; the h/v count pins via count 1->2 / 4->8).
    Fixture findings, not papered over: the first S13 edit landed the
    untouched checks in the WRONG fixture (the q6 impulse, where
    op6=0/oq6=32) and failed for that reason; the first all-0 fixture
    set garbage pre-values on written taps, but every written tap is
    ALSO its own input (p5 = *op5 read before write), so the fixture was
    incoherent - corrected to a true all-zero block. MUTATIONS (captured
    against the final fixture set, all reverted, each killed by a named
    fixture): M1 op5 tap order swapped (p6*2/p5*7) -> "filter14 op5 tap
    order" (16 vs 11); M2 flat2 span p5-for-p6 -> "filter14 flat2
    boundary: core flat, outer broken" (BOTH op5 and oq0 assertions
    failed: flat2 flipped 0->1 sends the block down the 13-tap path);
    M3 else chain reordered (else->filter4) -> "filter14 else-stack:
    flat2=0 flat=1 takes the filter8 path" (op2 stayed 99); M4 RPOT
    shift 4->3 -> "filter14 saturation: all-255" (op5 = 254, not 255).
    GREEN: 40 test cases / 1240 assertions, 0 failed. MEASURED
    (2026-11-03): coverage A8 0.0203 -> 0.0292 (+95 nb), total
    22,745 -> 22,840 of 157,718; the printed percentage MOVED:
    14.4% -> 14.5% (22,840/157,718 = 14.4807% crosses the 14.45
    boundary); GATE byte-identical diff 0; the paired conformance count
    UNCHANGED (9 artifacts, all content-1:1) - COVERAGE-ONLY again.
  - [x] **A8/CDEF1 (2026-11-04, PERRY-A8): the CDEF find_dir + filter kernel
    family landed** in src/l10_filters/cdef.{h,cpp} + tests/test_cdef.cpp
    (test target registered). Spans (cdef.c, 367 nb measured): sign :16-:18,
    constrain :20-:27, eb_cdef_directions_padded :35-:56 (+ :58), 
    adjust_strength :66-:70, svt_aom_cdef_find_dir_c :88-:150 (the SPEC
    kernel; cost MAXimizes because cost[d] is the weighted variance of the
    input around its per-line means along direction d - the sum(x^2) term
    cancels across directions), find_dir_dual_c :152-:156, cdef_find_dir
    :158-:187 (list walker), pri/sec taps :189-:190 ({{4,2},{3,3}} /
    {{2,1},{2,1}}), svt_cdef_filter_block_c :193-:254 (the 16-bit SPEC
    filter: CDEF_VERY_LARGE 0x7F7F, max guarded vs sentinel, min
    unguarded-but-sentinel-never-wins, int16 sum, y = clamp(x +
    ((8+sum-(sum<0))>>4), min, max)), filter_block_8bit_c :257-:300,
    find_dir_8bit_c :303-:311, find_dir_dual_8bit_c :313-:317,
    eb_cdef_directions_rc :408-:422, cdef_tap_off_frame :427-:430,
    filter_block_8bit_bounded_c :437-:504 and cdef_find_dir_8bit :509-:535.
    Named deviations: cdefSign/cdefConstrain exported (file-local static
    INLINE in the reference) for direct testability - DF1
    signedCharClamp precedent; a local CdefBlockSize enum (only equality is
    used; l0_core::BlockSize lacks 4X8/8X4, shared header not extended);
    getMsb file-local (definitions.h:628-643 portable fallback). Fixture
    derivations: the var fixture FAILED FIRST (215040 vs the brief-worked
    1720320) because div_table[8] = 105 = 840/8, the 8-pixel-bucket weight;
    the port was right, the hand-derivation was wrong. The 16-bit kernel is
    the SPEC the 8-bit twins replicate - the derivation relationship is
    pinned by tests (8-bit == 16-bit on widened sentinel-free input; bounded
    == 16-bit with sentinels at exactly the off-frame tap positions, both
    corners), not by shared code. Integer-only verified (no float/double
    code in cdef.{h,cpp}). MUTATIONS (all reverted, against the FINAL set):
    M1 whole-pair dir+2/dir-2 swap SURVIVES BY CONSTRUCTION (the read
    multiset {+d1,-d1,+d2,-d2} is permutation-invariant and sum/max/min are
    order-independent) - sharpened to the minimal observable form (s0's
    offset dir+4 -> dir), killed by the asymmetric sec fixture (129 vs 128)
    plus the 8-bit==16-bit pin; M2 div_table[1]<->[2] (840<->420) survived
    the pre-existing set and was killed by a NEW weighting fixture (dir 6
    wins by 36,126,720 vs 36,114,432, unique; derived with an independent
    PowerShell replica of the cost walk, replica verified to reproduce the
    215040 var fixture exactly; mutant flips to dir 0); M3 damping shift
    (-1) killed by the constrain boundary pin (8 vs 0); M4 sentinel guard
    inversion killed by the 208-clamp fixture (210 vs 208); M5 bounded edge
    flag (r >= rows -> r > rows) killed by the bottom+right corner
    sentinel-equivalence fixture; BONUS partial[0]/partial[4] swap killed by
    the dir 3/4/5 sweeps + the weighting fixture.     GREEN: 87 test cases / 1281 assertions, 0 failed (l10_filters; the
    DF1/DF2 cases included; +47 cases / +41 assertions over DF2).
    MEASURED (2026-11-04): coverage A8 0.0292 -> 0.0639 (+367 nb), total
    22,840 -> 23,207 of 157,718; the printed percentage MOVED: 14.5% ->
    14.7% (23,207/157,718 = 14.7168% crosses the 14.65 boundary); GATE
    byte-identical 884,851 B; the paired conformance count UNCHANGED (9
    artifacts, all content-1:1) - COVERAGE-ONLY. PARKED: cdef_filter_fb and
    the CDEF_8BITS_PATH fb/lbd drivers (:322-:778 remainder - CDEF2),
    svt_aom_copy_rect8 (cdef_copy.h - CDEF2), enc_cdef mse/search (CDEF3).
|  - [x] **A8/CDEF2 (2026-11-04, PERRY-A8): the fb/lbd driver chain + the
|    copy helpers landed** in src/l10_filters/cdef.{h,cpp} +
|    tests/test_cdef2.cpp (registered; the CDEF1 test file untouched).
|    Spans, 377 nb measured span-by-span: svt_aom_copy_rect8_8bit_to_16bit_c
|    cdef.c:72-:79 (8), svt_cdef_filter_fb :322-:402 (76 - the brief's
|    81/97/65 counts did not reproduce; 76/92/64 measured),
|    cdef_filter_fb_lbd_impl :542-:638 (92), the luma/chroma wrappers
|    :640-:704 (64), the dispatch svt_cdef_filter_fb_lbd :706-:778 (73),
|    and cdef_copy.h svt_cdef_copy_rect8 :28-:74 (47), svt_aom_copy_rect
|    :79-:85 (7) and svt_aom_copy_sb8_16 :96-:105 (10). Named deviations:
|    cdefPriStrength/cdefSecStrength factored out of the fb bodies
|    (:327-:329 / :551-:553; cdefSign precedent) so the encoding contract
|    s = pri*4 + sec, sec codes {0,1,2,3} -> {0,1,2,4}, is directly
|    testable; the impl exported as a plain function where the reference
|    AOM_FORCE_INLINEs it into the wrappers (perf property, not behavior).
|    Anchor notes: PLANE_Y = 0 = the first enum value (definitions.h:683),
|    so pli != 0 is chroma; CDEF_8BITS_PATH has NO definition in Codec or
|    the cmake files - it is a build-system define, and the :506/:779
|    guard wraps the 8-bit list walker :509-:535, the impl/wrappers/
|    dispatch :542-:778 AND the cdef_copy.h include (:507), while the
|    16-bit fb driver :322-:402 is OUTSIDE (always compiled); the brief's
|    "s=12 -> pri 3/sec 1?" derived to pri 3/sec 0 (12%4 = 0); copy_rect
|    landed as copy_sb8_16's is_16bit-branch dependency despite the brief's
|    park note (named), fill_rect parked (callers all in enc_cdef.c).
|    GREEN: 113 test cases / 1316 assertions, 0 failed (l10_filters,
|    +26 cases / +35 assertions over CDEF1); full ctest 11/11, 271.56 s.
|    MUTATIONS (all reverted, against the FINAL set): M1 swapped pri/sec
|    derivation killed by 13 cases; M2 dirinit stale-check dropped killed
|    by 5 (the poisoned-dir fixtures); M3 edgeRight off-by-one (bxLast-1)
|    killed by the named right-edge fixture; M4 swapped v/h halo offsets
|    in copy_rect8 killed by the two offset fixtures; M5 fb t-selection
|    swapped killed by 4 (incl. the chroma pli=1 t=pri fixture); M6
|    always-conv422 killed by the conv440 fixture. MEASURED (2026-11-04,
|    a8-kernels): coverage A8 0.0639 -> 0.0994 (+377 nb), total
|    24,375 -> 24,752 of 157,718; the printed percentage MOVED:
|    15.5% -> 15.7%; GATE byte-identical 884,851 B; the paired conformance
|    count UNCHANGED (9 artifacts, all content-1:1) - COVERAGE-ONLY.
|    PARKED (named): corner_match.c/
|    corner_detect.c (global-motion machinery, ORPHANED per Scout CDEF).
|  - [x] **A8/LR1 (2026-11-04): the restoration math core landed** in
|    src/l10_filters/restoration.{h,cpp} + tests/test_restoration.cpp
|    (registered; new test file, l10_filters::restoration namespace).
|    Spans, 243 nb measured span-by-span against the pinned restoration.c
|    (1534 nb): sgr_params :31-:49 (19), extend_frame_lowbd :110-:126 (17),
|    the svt_extend_frame entry :150-:157 (8), copy_tile_lowbd :159-:164
|    (6), the copy_tile dispatcher :173-:180 (8), boxsum1 :430-:495 (61),
|    boxsum2 :497-:584 (83), boxsum :586-:594 (9), svt_decode_xq :597-:608
|    (12), x_by_xplus1 :610-:625 (16), one_by_x :627-:630 (4). The brief's
|    ~370-450 nb estimate did not reproduce - the named spans sum to 243.
|    BRIEF CORRECTION (grep-verified): svt_decode_xq in this tree is the
|    params-aware version (r[0]==0 -> xq[0]=0, xq[1]=(1<<7)-xqd[1];
|    r[1]==0 -> xq[1]=0; else xq[1]=(1<<7)-xq[0]-xqd[1]), NOT the brief's
|    spec-style "xqd[0]==0 -> 96 else 128-3*xqd[0], xqd[1] -> 5*xqd[1]";
|    no 96 or 5* constant exists in :597-:608. Semantics finding: the
|    boxsum family's edge handling is TRUNCATION, not border reads - the
|    outermost rows/cols of the given array get 2-sums (r=1) / 3-sums
|    (r=2); composed, the walk is a 2D box sum clamped at the array
|    bounds, pinned by an independent clamped-box oracle on 7x9 fixtures
|    (distinct values, stride 12, sentinel padding proving no OOB reads).
|    Data tables pinned by 0-mismatch formula oracles over ALL entries:
|    x_by_xplus1[x] = floor((256*x + (x+1)/2) / (x+1)) with entry 0 = 1
|    and entry 255 = 256; one_by_x[x] = floor((4096 + (x+1)/2) / (x+1)).
|    Named deviations: file-local statics exported for testability
|    (cdefSign precedent); highbd branches assert-parked
|    (extend_frame_highbd :128-:148, copy_tile_highbd :166-:171 -
|    unreachable in our emission, enable_restoration stays 0);
|    svt_memcpy -> memcpy. GREEN: 133 test cases / 2237 assertions, 0
|    failed (l10_filters, +20 cases / +921 assertions over CDEF2); full
|    ctest 11/11, 290.27 s.
|    MUTATIONS (all reverted, against the FINAL set): M-sgr-row0 (r0 2->3)
|    killed by the row-0 test; M-sgr-row14 (s1 -1->0) killed by the 16-row
|    identity fixture; M-boxsum-clamp (top-edge a+b -> b+c) killed by the
|    7x9 oracle (all-ones cannot see direction); M-boxsum-sqr (vertical
|    else-branch squaring dropped) killed by the sqr fixture; M-boxsum2-
|    bound (i<height-3 -> height-2) killed by the oracle; M-boxsum-dispatch
|    (r==1/r==2 swap) killed by the dispatch fixture; M-decode-xq
|    ((1<<7) -> (1<<6) in the else branch, the adapted form of the brief's
|    96->128 swap) killed by the else-branch fixture; M-xbyx-entry (entry
|    103, 254 -> 999) killed by the formula oracle - and the REVERT itself
|    corrupted row 13 (restored with row-12 content), caught at entries
|    104-109 by the same oracle before any commit; M-onebyx-entry (entry
|    17, 195 -> 999) killed by the formula oracle; M-extend-bottom (bottom
|    fill emptied) killed by the extend fixture; M-copytile-stride (src
|    reads dst_stride) killed as a compile error (src_stride unreferenced,
|    C2220 warning-as-error).
|    MEASURED (2026-11-04, an14-noiseutil): coverage A8 0.0994 -> 0.1224
|    (+243 nb), total 25,097 -> 25,340 of 157,718; the printed percentage
|    MOVED: 15.9% -> 16.1% (25,340/157,718 = 16.0715% crosses the 16.05
|    boundary); GATE byte-identical 884,851 B; the paired conformance
|    count UNCHANGED (9 artifacts, all content-1:1) - COVERAGE-ONLY.
|    PARKED (named): the stripe machinery setup/restore_processing_
|    stripe_boundary :249-:397 and get_stripe_boundary_info :216
|    (RestorationTileLimits / RestorationLineBuffers struct-coupled),
|    wiener_filter_stripe :399 (RestorationUnitInfo + the convolve.c A13
|    dependency), the selfguided family :632-:962 (LR2), sgrproj/wiener
|    stripe + highbd :987-:1035, the :1040+ filter_unit/frame walkers,
|    :1124+ the tile walkers and boundary-line savers.
|  - [x] **A8/CDEF3 (2026-11-04, judge-merged from the parallel writer):
|    the enc_cdef mse/search block landed** in src/l10_filters/cdef.{h,cpp}
|    + tests/test_cdef3.cpp (registered in the CMake test list - the
|    writer's commit had left that line uncommitted; the judge added it at
|    merge). Spans, 318 nb MEASURED by the judge span-by-span against the
|    pinned enc_cdef.c (1017 nb) + cdef_copy.h: the four mse kernels
|    mse_8xn_16bit :24-:35 (12), mse_4xn_16bit :37-:48 (12), mse_8xn_8bit
|    :50-:61 (12), mse_4xn_8bit :63-:76 (13); the two dist bodies
|    svt_aom_compute_cdef_dist_16bit :77-:113 (37) and _8bit :115-:151
|    (37, the <8-rows/8-cols dispatch over the mse kernels); cdef_apply_fb
|    :200-:298 (99, the halo/copy walk calling the CDEF2 copy helpers +
|    the filter driver, inside #if CDEF_8BITS_PATH); svt_search_one_dual_c
|    :714-:772 (59, the greedy /3 search over (lev0,lev1) windows) and
|    joint_strength_search_dual :787-:818 (31); cdef_copy.h
|    svt_aom_fill_rect :88-:94 (6, the CDEF2-parked helper, now landed).
|    WRITER-CLAIM CORRECTION (the judge's re-measure, named): the writer's
|    register line and coverage row quoted "A8 10% (1054/10600)" - that is
|    the PRE-CDEF3 count (its branch had no LR1) and the writer never
|    moved the fraction; the judge measured the spans at 318 nb and set
|    A8 0.1224 -> 0.1524 (1615/10600, 15.2%; the judge's first run printed
|    1372 because it added 318 to the PRE-LR1 1054 instead of the post-LR1
|    1297 - caught by the regenerated tool run, see the CDEF3 register
|    entry), total 25,340 -> 25,658 (16.3%).
|    The commit message carried RED + mutation evidence but no suite or
|    byte figures - the judge's post-merge run is the measured record:
|    full ctest 11/11, GATE byte-identical 884,851 B diff 0. Mutations
|    named by the writer (mse stride swap, dist accumulation drop, search
|    window-init corruption, halo offset swap, fill_rect bound off-by-one)
|    each reverted after killing a named fixture. Coverage-only slice:
|    moves no conformance count, filter enable bits stay literal 0.
|    PARKED (named): svt_sb_compute_cdef_list :153-:199, svt_av1_cdef_frame
|    :300-:713, svt_pick_cdef_from_qp :823-:875, propagate_cdef_strength
|    :875-:897, finish_cdef_search :897-:1017 (the struct-walled driver
|    chain); corner_match.c/
|    corner_detect.c (global-motion machinery, ORPHANED per Scout CDEF).
|  - [x] **A8/LR2 (2026-11-04, PERRY-LR2, ff-merged at 7cca15c): the
|    selfguided restoration family lands** in src/l10_filters/restoration.
|    {h,cpp} + tests/test_restoration.cpp. Spans of restoration.c
|    :632-:962, 306 nb MEASURED by the writer span-by-span (the judge
|    re-ran coverage.ps1 in the worktree and spot-checked every cited
|    restoration.h constant line - 11/11 match): the fast internal
|    :632-:764 (125 nb; estimation rows stepped by 2, row-parity *6/*5
|    kernels, assert(r == 2) at the filter stage), the slow internal
|    :766-:884 (111 nb; row stride 1, the 3x3 *4/*3 kernel, nb = 5),
|    svt_av1_selfguided_restoration_c :886-:922 (34 nb; dgd32 staging +
|    the r[0]/r[1] skip pair) and svt_apply_selfguided_restoration_c
|    :924-:962 (36 nb; decodeXq composition + u/v accumulation + clip).
|    300 claimed (6 named-parked: the highbd staging loop :894-:898 = 5,
|    the :956 CONVERT_TO_SHORTPTR write = 1). The SGR constant set added
|    to our restoration.h (SGRPROJ_BORDER_HORZ/VERT :41-:42,
|    SGRPROJ_PRJ_BITS :94, SGRPROJ_RST_BITS :96, SGRPROJ_SGR_BITS/:SGR
|    :98-:99, MAX_RADIUS :110, SGRPROJ_MTABLE_BITS :112,
|    SGRPROJ_RECIP_BITS :113, RESTORATION_PROC_UNIT_PELS :76-:78,
|    RESTORATION_UNITPELS_MAX :84). RED: five compile-error slices + two
|    fixture-calibration REDs, both MEASURED findings, not code bugs:
|    (1) full-range LCG inputs drive z >= 255 everywhere, so A[k] = 256,
|    B[k] = 0 and BOTH kernels collapse to dst = 16*dgd - the fast/slow
|    pair is indistinguishable there, fixture moved to mid-variance;
|    (2) a BRIGHT outlier does not fire w < 0 (the filter preserves it),
|    the clip-low fixture moved to a DARK outlier. 13 new TEST_CASEs
|    (binary 162 cases / 3,142 assertions): three-estimator-regime
|    clamped-box oracles (saturated / mid / flat with hand-derived
|    fast 1602 / slow 1600), the r==0 skip sentinels, both clip bounds
|    with calibration CHECKs, eps-10 never-reads-skip. Mutations M1-M9:
|    M1 (even-row *6/*5 swap), M2 (odd-row (k-1) drop), M3 (RECIP shift
|    12->11), M4 (min(z,255) -> always-255, killed by the flat fixture
|    1602 -> 1600), M5 (inverted a*n < b*b guard), M6 (oneByX[n-1] ->
|    [n-2]) each killed by named fixtures; M7 (fast stride i+=2 -> i+=1)
|    SURVIVES BY CONSTRUCTION and is proven dead code, sharpened to
|    i+=3 (killed by 7); M9 measured COMPOUND with M8 still applied
|    (procedural slip, disclosed: 10 fail, 3 slow-specific beyond M8's
|    7). Writer's tree run: full suite 11/11 (269.14 s), GATE
|    byte-identical 884,851 B diff 0. Judge post-merge re-measure on
|    master: full suite 11/11 (260.65 s), GATE byte-identical
|    884,851 B diff 0. A8 fraction 0.1524 -> 0.1807 (1,615 -> 1,915 of
|    10,600), total 25,658 -> 25,958 (16.5%). Coverage-only slice: moves
|    no conformance count, enable_restoration stays literal 0. Named
|    deviations: the four reference statics exported (LR1 precedent);
|    highbd branches assert-parked with CONVERT_TO_SHORTPTR parked;
|    AOMMIN -> std::min(z, 255u); ROUND_POWER_OF_TWO via file-local
|    roundPowerOfTwoU32/roundPowerOfTwoI32 (definitions.h:457 shape);
|    clip_pixel_highbd via clipPixelHighbd (definitions.h:704-:714
|    switch shape); bit_depth kept symbolic, nothing hardcoded to 8;
|    REFACTOR: decodeXq's literal (1 << 7) -> (1 << sgrprojPrjBits),
|    green-before/after.
|    PARKED (named): sgrproj_filter_stripe :964+ and the stripe
|    machinery, sgrproj/wiener stripe + highbd :987-:1035, the :1040+
|    filter_unit/frame walkers, the :1124+ tile walkers.
|  - [x] **A8/GR1 (2026-11-04, PERRY-GR1, cherry-picked 582a409): the
|    film grain synthesis core lands** in src/l10_filters/grain.{h,cpp}
|    + tests/test_grain.cpp (namespace l10_filters::grain). 998 nb
|    claimed of grainSynthesis.c's 1,140 (judge band 995-1,020):
|    gaussian_sequence :28-:181 (2,048 entries transcribed, caveat
|    STATED, diff-verified), the rng pair :306-:328, init_arrays
|    :182-:266, luma/chroma grain generators :328-:466, scaling
|    :466-:506, add_noise_to_block :506-:607, params_equal :709-:793,
|    copy helpers + boundary overlaps :793-:865, the frame driver
|    :865-:1140. hbd :607-:709 (92) + dealloc :266-:287 (18)
|    named-parked. ~30 TEST_CASEs; 15 mutations killed by named
|    fixtures. BRIEF CORRECTIONS (writer, grep-verified): no noise_shape
|    parameter exists in the reference; AomFilmGrain lives in
|    EbSvtAv1.h, not grainSynthesis.h. RED: 12 slices, plus measured
|    hand-math anchor corrections on both port and oracle sides (the
|    row-xor is luma_line >> 5; the 2x2 luma average is (sum+2)>>2;
|    (32*20+128)>>8 = 3). Writer run: 13/13 (277.10 s), GATE
|    byte-identical 884,851 B. Coverage-only.
|  - [x] **A11/AN21 (2026-11-04, PERRY-AN21, cherry-picked f815055):
|    the resize 1D kernel family lands** in src/l11_resize (namespace
|    resize, new module - the scaffold commit 1e9ac65 pre-wired its
|    CMake). resize.c :147-:489 = 328 nb algorithm (judge re-measured
|    328) + the four interp tables resize.c :36-:137 (99) + the 1000-set
|    from super_res.c :20-:47 (28, charged to A8). 21 new TEST_CASEs, 15
|    nano-slices, 8 mutations killed (M8's killer is the 12->5
|    full-clamp path); one property-RED was a TEST BUG (the
|    in_length==1 break), corrected with M3 proving the corrected
|    property's discrimination; two oracle incidents disclosed (typo +
|    truncation-toward-zero). Parked: highbd :489-:781, save_YUV
|    :782-:791, pack/unpack_highbd :791-:881, the EbPictureBufferDesc
|    resize_frame driver :881+. Writer run: 13/13 (281.94 s), GATE
|    byte-identical 884,851 B. Coverage-only.
|  - [x] **A13/CV1 (2026-11-04, PERRY-CV1, ff-merged 3a1d0c9): the
|    convolve.c kernel family lands** in src/l12_convolve (namespace
|    convolve, new module). 163 nb (judge re-measured 161-163 band):
|    scalar products + filter offset/base, add_src horiz/vert hip, the
|    wiener convolve add-src (the uint16 staging contract: temp stride
|    128, h+6 rows, sentinel row), convolve_horiz/vert + the convolve8
|    pair; ConvolveParams + get_conv_params_wiener + the sub_pel_filters_8
|    table (128 entries, diff-verified). highbd :155-:253 (93) parked. 14
|    TEST_CASEs, 6 mutations killed. Writer run: 13/13 (287.33 s), GATE
|    byte-identical 884,851 B. LR3 (the restoration stripe family)
|    depends on CV1 and is the next wave. Coverage-only.
|    WAVE-CLOSE (judge, post-merge re-measure on master): full ctest
|    13/13 (264.97 s - the suite is 13 targets now: l11_resize +
|    l12_convolve joined at the scaffold), GATE byte-identical
|    884,851 B diff 0, coverage 17.5% (27,575/157,718): A8 28% (2,942),
|    A11 30% (3,600), A13 9% (477).
|  - [x] **A8/LR3 (2026-11-04, PERRY-LR3, ff-merged b888d90): the
|    restoration stripe/unit family lands** in src/l10_filters/restoration
|    - 249 nb ported (whole_frame_rect, count_units_in_tile,
|    get_stripe_boundary_info, setup/restore_processing_stripe_boundary,
|    wiener_filter_stripe, sgrproj_filter_stripe,
|    svt_av1_loop_restoration_filter_unit), 571 nb named-parked (the
|    highbd stripes, the Av1Common-walled filter_frame pair, the tile
|    walkers). The wiener stripe consumes the landed
|    convolve::wienerConvolveAddSrc - the l10 link line gained
|    av1_convolve under a JUDGE COURT ORDER (one line, named in the
|    commit). FINDING: WienerInfo's alignas(16) is LOAD-BEARING - the
|    convolve selects its tap row BY ADDRESS ALIGNMENT (measured offV=15).
|    FINDING: the wiener stripe's w round-up rule differs from sgrproj's
|    plain AOMMIN; M1 pinned it by the overflow-region observation. 13 new
|    TEST_CASEs (206 in the l10 exe), 6 mutations killed. Writer run
|    14/14 (280.56 s), GATE byte-identical 884,851 B. The stale-exe trap
|    hit once (C4100-as-C2220) and was named - later greens verified by
|    case count. Coverage-only.
|  - [x] **A12/PAL1 (2026-11-04, PERRY-PAL1, cherry-picked 81edc4d): the
|    palette + k-means family lands** in src/l13_palette (new module,
|    scaffold e128cbb) - 389 nb of palette.c (+18 of
|    pic_analysis_process.c charged to A11; count_colors' body lives
|    there), k_means_template.h bodies both dims (k 2..8 enumerated). 39
|    TEST_CASEs / 371 assertions, 9 mutations killed (M5/M8/M9 survived
|    their first form and were sharpened - disclosed). BRIEF CORRECTIONS:
|    count_colors is DEFINED in pic_analysis_process.c:868-:909; dim-2
|    k-means has no portable-C caller (palette.c:526 is dim 1) - ported +
|    tested anyway. Writer run 14/14 (276.64 s), GATE byte-identical
|    884,851 B; one non-reproducing l10 launch flake named (0xc0000374).
|    A12 0.00 -> 0.2413. Coverage-only.
|  - [x] **A1/TX1 (2026-11-04, PERRY-TX1, cherry-picked 6634c20): the
|    non-square 2D transform family lands** in src/l3_transforms - 14
|    shapes enumerated (the brief's 12 missed 8x16/16x8; the writer
|    caught it), the generic cfg machinery (shift/type/cos_bit tables +
|    two_d_core + dispatch switches) generalized, 76 admissible
|    (shape,TxType) pairs enumerated (46 fwd / 62 inv), roundtrip
|    exactness classified per shape (8x16 exact, 16x8 lossy-1 - the
|    reference itself is asymmetric, measured and pinned). 113 TEST_CASEs
|    / 444 assertions, 7 mutations killed. JUDGE CORRECTION: the writer's
|    span set claimed 1,148 nb, of which 220 (inv_transforms.c
|    :2901-:3137, ALL highbd_inv_txfm_add_* wrappers) are highbd and
|    NAMED-PARKED - A1 charges 928, 0.33 -> 0.3643. Writer run 14/14
|    (278.55 s), GATE byte-identical 884,851 B. Host only, no GPU twins
|    (named). Coverage-only.
|  - [x] **WAVE-2 CLOSE (judge, post-merge re-measure on master)**: full
|    ctest 14/14 (272.63 s - l13_palette is the 14th target since
|    e128cbb), GATE byte-identical 884,851 B diff 0, coverage 18.5%
|    (29,157/157,718): A1 36% (9,839), A8 30% (3,191), A11 30% (3,618),
|    A12 24% (389). The 20% push needs ~+2,350 more; wave 3 opens
|    immediately.
|  - [x] **A11/TF1 (2026-11-04, PERRY-TF1, ff-merged 736f2cc): the
|    temporal filtering kernel family lands** in src/l9_analysis/
|    temporal_filter.{h,cpp} + tests/test_temporal.cpp (namespace
|    analysis::temporal) - 534 nb of temporal_filtering.c (judge
|    re-measured exact, span-by-span): the log1p_tab_fp16 table
|    :339-:565 (227, mechanically extracted, 0 hand-typed), noise_log1p
|    :568-:586, decay/shift factors :589-:619, the expf/sqrt tables
|    :624-:677, the squared-error sum :697-:707 and both planewise
|    kernel pairs (zz :738-:814, non-zz :930-:1087). Consumed MeContext
|    fields threaded as parameters. PARKED named: the hbd twins, the
|    subpel searches, block_plane_wise, apply_filtering_central (wall
|    deep). 21 TEST_CASEs, 6 mutations killed. BRIEF CORRECTIONS: the
|    writer's span boundaries supersede the brief's (measured); the
|    brief's "partial (<=) vs full (<) split" phrasing did not
|    reproduce - the actual half-open subblock/quadrant rule pinned via
|    M3. Writer run 14/14 (280.64 s), GATE byte-identical 884,851 B.
|    A11 0.3008 -> 0.3452. Coverage-only.
|  - [x] **A14/PU1 (2026-11-04, PERRY-PU1, cherry-picked 64778f3): the
|    pack/unpack + blend + VAR family lands** in src/l9_analysis/
|    packunpack.{h,cpp} + the analysis PU1 block - pack_unpack_c.c WHOLE
|    (183 nb, all 7 kernels), blend_a64_mask_c.c WHOLE (31 nb, the
|    vmask blend - its reference name carries "highbd" but it is
|    struct-free; judgment named) and variance.c varianceCore + the 20
|    missing VAR instances (general two-buffer shape; AN8's offs-folded
|    shape coexists) = +236. FINDING pinned: at bStride 0 varianceCore
|    still indexes b[0..w-1] - the offs array is REAL. 9 mutations
|    killed. PARKED named: SUBPIX/OBMC_VAR families, the bilinear
|    helpers, upsampled_pred, the highbd wrappers. Writer run 14/14
|    (281.62 s), GATE byte-identical 884,851 B. A14 0.316 -> 0.4768.
|    Coverage-only.
|  - [x] **A6/PO1 (2026-11-04, PERRY-PO1, cherry-picked 0b06713): the
|    struct-free pic_operators kernels land** in src/l6_pipeline/
|    picops.{h,cpp} + tests/test_picops.cpp (namespace pipeline::picops)
|    - 199 nb: the two residual kernels (NO clamp, the uint16 wrap-cast
|    - matches the l6 convention), the three full-distortion kernels +
|    the 32-single dispatcher (shift-free int64 SQR - the brief's
|    shift-table claim did not reproduce, corrected), the yv12 copy trio
|    and generatePadding16Bit (struct-free; AN10 had NAMED it excluded -
|    now landed in picops). PARKED 172 named (the pack2d/unpack2d
|    dispatches - callees are PU1's packunpack; the five
|    EbPictureBufferDesc-walled pic drivers). 14 TEST_CASEs, 11 mutation
|    kills (the naive predOffset mutant is a compile-kill under /WX -
|    behavioral kill used instead). INCIDENT disclosed: picops.cpp was
|    overwritten mid-session by out-of-band activity (recovered; the
|    Copy-Item-timestamp/stale-obj lesson recorded in the commit).
|    memset32bit grep-verified DEAD. Writer run 14/14 (278.73 s + the
|    first full 280.06 s), GATE byte-identical 884,851 B. A6 0.1102 ->
|    0.1245. Coverage-only.
|  - [x] **WAVE-3 CLOSE (judge, post-merge re-measure on master)**: full
|    ctest 14/14 (278.04 s), GATE byte-identical 884,851 B diff 0,
|    coverage 19.1% (30,126/157,718): A6 12% (1,734), A11 35% (4,152),
|    A14 48% (699). ~+1,400 nb to 20%; wave 4 opens immediately.
|  - [x] **A12/SEG1 (2026-11-04, PERRY-SEG1, ff-merged 81e5020): the
|    struct-free segmentation surface lands** in
|    src/l13_palette/segmentation.{h,cpp} + tests/test_segmentation.cpp
|    (namespace l13_palette::segmentation) - 116 nb ported
|    (get_variance_for_cu :23-:84, calculate_segmentation_data
|    :260-:271, find_segment_qps :273-:326 + 3 nb of params tables);
|    165 parked (the roi_map/PCS walls); temporally_update_qps
|    grep-verified DEAD (declared, defined nowhere). QUIRKS PRESERVED
|    1:1 and fixture-pinned: the ROUND identity (step_size can only
|    ever be 1), the POW2 narrowing wraps, the descending bin-edge
|    pairing, the transposed getVarianceForCu index pairings, log2f_safe
|    = get_msb(x|1). 17 TEST_CASEs, 6 mutations killed. Writer run
|    15/15 (289.45 s - l14_utility is the 15th target), GATE
|    byte-identical 884,851 B. A12 0.2413 -> 0.3133 (389 + 116 = 505 of
|    1,612). Coverage-only.
|  - [x] **A13/U1 (2026-11-04, PERRY-U1, cherry-picked bf6d225): the
|    utility.c surface lands** in src/l14_utility (new module, scaffold
|    0f0cf7e; namespace l14_utility) - 347 nb of utility.c (the block
|    geometry builder svt_aom_build_blk_geom :332-:413 = 82, the md-scan
|    walk :227-:289, the coded-unit + mini-GOP stats arrays, the
|    ns/d1-depth-offset tables, hvsizeToBsize...) + 31 nb of
|    common_utils.c dependencies (ssSizeLookup + getPlaneBlockSize) =
|    378 nb to A13. 26 TEST_CASEs / 647 assertions incl. the FULL
|    builder enumeration over all 11 geoms + the GEOM_2 85-block
|    cross-check against coded_unit_stats_array (two independent
|    transcriptions agreeing through the algorithm). 6 mutations killed
|    - M6 by CRASH (the mutated gate overruns the 41-entry test array;
|    fail-fast 0xC0000409, mechanism named). Writer run 15/15 (289.39
|    s), GATE byte-identical 884,851 B. A13 0.0890 -> 0.1596. PARK: the
|    FIXED_POINT_ASSERT_TEST breakpoint (5 nb, dead under NDEBUG).
|    Coverage-only.
|  - [x] **A14/CS1 (2026-11-04, PERRY-CS1, cherry-picked 401a526): the
|    C_DEFAULT small-file sweep lands** (multi-home) - 322 ported / 177
|    recorded already-landed / 246 parked. NEW: picture_operators_c.c
|    195 nb (the WHOLE hadamard family + pictureCopy + copyWxh -> l6
|    picops; PO1's file was Codec/pic_operators.c, this sibling was
|    untouched), inter_prediction_c.c 28 nb (buildCompoundDiffwtdMaskD16
|    -> new src/l9_analysis/cdefault_extra), cfl_c.c 25 nb
|    (cflPredictLbd -> l4), compute_sad_c.c 52 + sad_av1.c 22 (sad16b +
|    sadLoopKernel + obmcSad -> l5). The overlap audit CONFIRMED
|    already-landed: encode_txb_ref (40, l7), filterintra (93, l4),
|    intra upsample (20, l4), compute_mean.h (AN1-AN4). 7 mutations
|    killed; 3 fixture-side non-reproductions disclosed. INCIDENT: a
|    hung cl.exe (PID 5752, ~5h17m at 11.6 CPU-seconds = mspdb/AV
|    deadlock, not a slow compile) wedged the writer's build; the judge
|    killed it and the writer resumed with no work lost - the
|    kill-the-hung-compiler recipe joins the ops notes. Writer run
|    15/15 (~301 s sum), GATE byte-identical 884,851 B. Charges: A2 +25
|    (0.9074), A3 +74 (0.0242), A14 +223 (0.6290). Coverage-only.
|  - [x] **WAVE-4 CLOSE (judge, post-merge re-measure on master)**: full
|    ctest 15/15 (294.41 s), GATE byte-identical 884,851 B diff 0,
|    coverage 19.6% (30,943/157,718): A2 91% (3,156), A3 2% (429), A12
|    31% (505), A13 16% (855), A14 63% (922). PARSE-BUG EPISODE: the
|    first post-merge tool run printed A12 as 0/0 and TOTAL 30,438 - an
|    embedded pipe in the SEG1 sentence (get_msb(x|1)) split the
|    pipe-delimited row; caught by the -11 covered move, sentence
|    rewritten, and the TRUE total is 30,943 not 30,448 (the broken
|    parse had also dropped A12's whole 505). ~+575 nb to 20%; wave 5
|    (the hash pair + global motion) opens immediately.
|  - [x] **A3/HM1 (2026-11-05, PERRY-HM1 + judge finalize, ff-merged
|    0152825): the motion-hash + global-motion surface lands** in
|    src/l5_motion/hash_motion.{h,cpp} + global_motion.{h,cpp} - 473 nb
|    judge-charge (crc32c pair hash.c :28-:70 = 41; the hash-table
|    machinery + block-hash generation + the walk, hash_motion.c
|    :20-:341 = 269; the GM param surface global_motion.c :30-:212 =
|    163 incl. refine_integerized_param with GmControls threaded as
|    scalars); parks 137 named (the two hbd twins + the correspondence/
|    PCS walls). 21 TEST_CASEs; M1-M5 killed, RED lines quoted in the
|    commit. TRANSPORT WEDGE INCIDENT (named, the ops lesson): the
|    writer's session wedged after the question "your shell caps at
|    120 s, run the suite for me" - the judge ran suite (15/15,
|    278.39 s) + GATE and sent the confirmation; the send appeared
|    undelivered (wait replayed a stale final), a cancel+resume hit
|    session_busy, and the judge nearly stomped the LIVE writer (its
|    worktree was mid-M-phase when the judge attempted a verification
|    mutation - the mutation was aborted, the file restored, the
|    worktree left alone). The writer was ALIVE the whole time
|    (session_busy was true) and completed properly: worklog
|    HM1_WORKLOG.md + commit. LESSONS: (a) wait's stale-final replay
|    is NOT evidence of death - check worktree mtimes/git state before
|    touching anything; (b) never mutate-verify a worktree that has
|    recent mtime activity; (c) children should write worklogs early
|    and often, not only at finalize.
|  - [x] **A9/RC1 (2026-11-05, judge slice, e0a8ac2): rc_tables.h
|    lands** in src/l15_rc (new module, scaffold 1909438) - 18
|    qindex-mapping tables x 256, 278 nb, mechanically extracted, the
|    runtime diff-verify test parses the reference file itself. RED:
|    C2039 findTable + the STALE-EXE TRAP during the strcmp include
|    miss (a ctest 'pass' over a failed build - caught, re-run clean).
|    PAIRED-METRIC CAVEAT STATED: transcription, not algorithm (the
|    AN14 OD_DIVU precedent). Suite 16/16 (277.79 s), GATE
|    byte-identical 884,851 B.
|  - [x] **WAVE-5 CLOSE / THE 20% LINE (judge, post-merge re-measure on
|    master)**: full ctest 16/16 (277.79 s), GATE byte-identical
|    884,851 B diff 0, coverage **20.1% (31,693/157,718)**: A3 5% (901),
|    A9 5% (278). **THE 20% TARGET IS CROSSED** - the campaign arc:
|    13.1 (AN1-3 era) -> 16.5 (LR2) -> 17.5 (wave 1) -> 18.5 (wave 2)
|    -> 19.1 (wave 3) -> 19.6 (wave 4) -> 20.1 (wave 5).
|  - [x] **A1/TX2 (2026-11-05, e2c5cf4): the remaining TxType surface
|    lands** in src/l3_transforms - the FULL 16-type list the lowbd
|    tables admit (definitions.h:1031-1047): set_flip_cfg verbatim
|    (inv_transforms.h:139-181, 39 nb), the vtx/htx tables all 16 rows
|    (:45-:80, 36 nb), fwdIdentity4..64/invIdentity4..64
|    (transforms.c:2318-:2359 + inv_transforms.c:2346-:2389, 78 nb;
|    the reference's fidentity/iidentity renamed per the port's
|    fwd/inv prefix convention, named deviation), the fwd/inv core
|    flip branches (transforms.c:2430-:2452 + inv_transforms.c:
|    2554-:2577, 47 nb - the TX1 fixed-zero-flips deviation
|    RESOLVED), IDTX joined at the 32-class shapes and the six
|    one-axis types at TX_32X32 through the CONFIG=1 fallback branch
|    (EbConfigMacros.h:246; the 64-axis shapes stay DCT_DCT-only),
|    supported() 165 fwd / 243 inv (+300 pairs). The ten square
|    facades route through the TX1 config+core path (the reference's
|    own facade shape) with the landed DCT/ADST outputs unchanged;
|    invTxfm2dAdd64x64 keeps the landed 4096 facade - routing it
|    through the compacting form broke the FS3 emission walk
|    (pipeline 1/16 red) and was REVERTED in-slice (FS5d stands).
|    204 nb measured. RED evidence: S1-S12 (compile C2039/LNK2019
|    REDs per kernel, the S2 SIGSEGV out-of-bounds kVtxTab[4] read,
|    two ORACLE BUGS named inside the tests (S8's DCT_FLIPADST
|    intermediate, S12a's row-2 condition) and the S6 negative-ramp
|    clip (the reference's own clipPixelAdd behavior)); M1-M5 killed
|    (the flip-axis group swap, the ud_flip read-branch inversion,
|    the fidtx16_range_mult2 corruption, the IDENTITY dispatch
|    break, the identity scale corruption). Suite 16/16 (285.87 s);
|    GATE byte-identical 884,851 B diff 0 - no generator input.
|    REGISTER-LAG NOTE (recorded at bf32b4c): this entry and SYM1's
|    belong in the slice's own commit; the state refresh
|    (AGENTS.md + coverage_areas.txt) was left uncommitted too and
|    landed at bf32b4c - the "register moves in the SAME commit"
|    rule was violated twice by wave 6 and is hereby repaired.
|  - [x] **A5/SYM1 (2026-11-05, c1ec838): the palette + segmentation
|    symbol surface lands** in src/l7_entropy - six palette CDF
|    families + the EcSegProbs struct (cabac_context_model.c:
|    :477-:586, the nine mechanical .inc extracts via the committed
|    tools/extract_palette_cdfs.ps1), allowPalette +
|    getPaletteBsizeCtx (FINDING pinned: the reference's
|    bsize >= BLOCK_8X8 gate is ENUM order, not geometry -
|    BLOCK_4X16/16X4 pass it; the enumerated 22-bsize want[] table
|    pins the quirk), deltaEncodePaletteColors, writeUniform/
|    readUniform + readLiteralBits, writePaletteColorsY/
|    readPaletteColorsY + the local indexColorCache twin
|    (DUPLICATED from l13_palette - l7 cannot link l13; both port
|    palette.c:111-141), writePaletteModeInfo (the uv continuation
|    PARKED - the pinned writer emits no uv palette,
|    entropy_coding.c:4371 asserts palette_size[1] == 0), the
|    fast-context family twin (palette.c:639-776, also duplicated),
|    tokenizeColorMap (palette.c:781-828; PAL1 had parked it) +
|    packMapTokens + getColorIndexContext + decodeColorMapTokens -
|    TWO roundtrip-caught defects named (the cdf-row pointer indexed
|    the ELEMENT dimension instead of the [ctx]-th ROW; an
|    out-of-range fixture map value, the fixture's bug), the wb trio
|    duplicated into l7 (entropy_coding.c:1343-1382; l7 cannot link
|    l8) + rbRead* + writeSegFeatures/readSegFeatures (a third
|    caught defect: the seg-enabled bit was a parameter; the reader
|    reads enabled itself and FORCES update_map=1/temporal=0/
|    update_data=1 at PRIMARY_REF_NONE), getSegmentId/
|    updateSegmentationMap/getSpatialSegPrediction/negInterleave/
|    negDeinterleave/writeSegmentId/readSegmentId. SVT-side 634 nb
|    (481 new-to-the-port + 153 duplicated with l13) + 242 nb aom
|    reader twins (out-of-tree, not in the denominator). M1-M6
|    killed (M3's palette-size ctx off-by-one is INVISIBLE to the
|    roundtrips - both sides share the shifted row; the pure-function
|    enumeration is the only guard). Suite 16/16 (278.64 s); GATE
|    byte-identical 884,851 B diff 0. Parks named: the seg TEMPORAL
|    path (the SVT writer SVT_ERRORs on it, entropy_coding.c:
|    :4895-:4903), write_palette_colors_uv (does not exist in the
|    pinned encoder).
|  - [x] **A3+A13+A14/IP1 (2026-11-07, PERRY-IP1, ff-merged 3205c8e):
|    the interp filter-params machinery + upsampled_pred land** in
|    src/l12_convolve - the five filter tables (sub_pel_filters_4
|    inter_prediction.c :257-:272 + _8sharp/_8smooth/bilinear/_4smooth
|    :1133-:1197, 80 nb, mechanically transcribed + diff-verified), the
|    params/getters surface (A3 +105), the enums/struct (A13 +34) and
|    av1_get_filter + svt_aom_upsampled_pred_c from C_DEFAULT/variance.c
|    :71-:137 (A14 +64 - BRIEF CORRECTION: the brief misplaced
|    upsampled_pred in inter_prediction.c; grep 0 hits there; the body
|    (void)s every struct param and carries four paths). 33 TEST_CASEs /
|    1,941 assertions; 7 mutations: 6 killed + M2 (intermediate_height
|    +1) SURVIVES BY CONSTRUCTION (a dead staged row is
|    output-invisible - genuine reference-side equivalence, sharpened to
|    a killed M2b); M7 FINDING: composition tests are BLIND to
|    shared-selection mutations - the enumeration tests discriminate.
|    Writer run 16/16 (273.07 s), GATE byte-identical 884,851 B.
|  - [x] **A14/SUB1 (2026-11-07, PERRY-SUB1, cherry-picked 16cfad2):
|    the subpel variance family lands** in src/l9_analysis/subpel
|    (namespace analysis::subpel) - the bilinear 2-tap pair + table,
|    the SUBPIX_VAR macro + 22 instances, the obmc core + OBMC_VAR/
|    OBMC_SUBPIX_VAR + 22 pairs = 149 nb new-to-the-port. FOUR BRIEF
|    CORRECTIONS by measurement: bilinear_filters_2t is [8][2] with
|    row 0 = {128, 0} (the {128-16i, 16i} oracle 0-mismatch), the
|    rounding is (sum + 64) >> 7, the counts are 22/22/22, the offset
|    grid is 0..7 per axis (the composition pinned over the FULL
|    64-offset grid). 15 TEST_CASEs, 7 mutations killed (M3's
|    second-pass dims swap needed a fatal-abort overrun to die). Two
|    mutant reverts were silent no-ops - final state grep-verified
|    clean, 219/219 (the stale-exe lesson again, in mutation
|    discipline). Writer run 16/16 (272.35 s), GATE byte-identical
|    884,851 B.
|  - [x] **JUDGE OPS ENTRY (the red scaffold + the silent-drop edit)**:
|    the wave-7 scaffold commit cfff19d landed RED (LNK2019 on
|    analysis::buildCompoundDiffwtdMaskD16) because the judge built the
|    l9 CMake edit from a stale read of the file and the edit tool
|    tolerated the mismatch, silently REPLACING cdefault_extra.cpp with
|    subpel.cpp on the shared add_library line. Fixed forward at
|    4f52507 (both lines kept); full suite green again (269.97 s).
|    LESSON: re-read shared build files IMMEDIATELY before editing;
|    a scaffold commit is a real commit needing the same GREEN gate.
|  - [x] **WAVE-7 CLOSE (judge, post-merge re-measure on master)**: full
|    ctest 16/16 (267.78 s after the power cut interrupted a first
|    attempt; the GATE run survived and was re-run byte-identical
|    884,851 B diff 0), coverage 20.8% (32,731/157,718): A3 6% (1,006),
|    A13 17% (889), A14 77% (1,135). A14's remaining 331 nb: the hbd
|    twins (named-parked) + the per-size SAD wrappers.
|  - [x] **A14/SW1 (2026-11-07, 4fe6acc): the SAD wrapper instances -
|    compute_sad_c.c + sad_av1.c land 100%** - the sadMxN macro + all
|    22 instances and the sadMxNx4D macro + all 22 instances
|    (compute_sad_c.c:103-193), the OBMCSADMxN macro + all 22 instances
|    (sad_av1.c:36-64), plus the generic helper the sadMxN macro
|    forwards to (svt_nxm_sad_kernel_helper_c :21-:40, ported verbatim
|    as nxmSadKernelHelper). The landed sad4x4..64x64 ARE the sadMxN
|    instances at those five sizes; the other 17 sadMxN sizes, all 22
|    4D wrappers and all 22 obmc wrappers are new thin instances in the
|    reference's own shape (sadMxN(m,n) = helper(height=n, width=m);
|    sadMxNx4D = the sadMxN instance per ref_array entry in index
|    order; OBMCSADMxN(m,n) = obmcSad(m=width, n=height)).
|  - [x] **A11/RS2 (2026-11-07, PERRY-RS2, ff-merged 8db61b7): the hbd
|    resize 1D family lands** in src/l11_resize - resize.c :489-:779 =
|    273 nb (judge re-measured exact), resolving the AN21 8-bit
|    discipline park (the HB1 pattern). BRIEF CORRECTION measured: the
|    hbd path does NOT scale filter coefficients by bd - the only bd
|    dependence is clip_pixel_highbd (cited per call site). 35 test
|    cases / 921 assertions incl. the full hbd@bd8 == 8-bit-twin sweep
|    over the enumerated length set + bd-10/12 clamp-firing absolutes;
|    6 mutations killed (M3's boundary flip SURVIVED its first fixture
|    - killed after an edge-contrast input; honest finding). FINDING:
|    a mutation that orphaned a parameter failed to BUILD (C4100 under
|    /WX) and left a stale exe reporting green - every mutation is now
|    compile-checked before its kill is claimed. Writer run 16/16
|    (268.09 s), GATE byte-identical 884,851 B. Coverage-only.
|  - [x] **A11/AM1+AN20 (2026-11-07, PERRY-AM1, cherry-picked ee8d249):
|    the vmaf activity-masking family lands** in src/l9_analysis -
|    pic_analysis_process.c 238 nb (the nine vmaf calculators + box
|    blur + unsharp + the preprocess driver, pa_ctx/pcs walls threaded),
|    the preprocessing statistics 89 nb (histogram bins + spatial
|    statistics + gathering), the temporal_filtering.c helpers 135 nb
|    (estimate_noise_fp16/avg_mad/gradient_coherence/the unsharp+blur
|    row kernels - the :3556-:3733 family the TF1 brief did not scout)
|    and firstpass.c AN20 the pure accumulation pair (13 nb; the
|    output_stats TWO_PASS wall stays parked) = 386 nb (judge
|    re-measured exact). 25 new TEST_CASEs (l9 251/251, 40,186
|    assertions); 11 mutations killed (M9 survived the LCG plane -
|    killed after a delta-plane strengthening); the literal gate_start
|    +1 off-by-one is UNOBSERVABLE under the committed float bands -
|    recorded, not faked. In-slice repair: a UTF-8 BOM + 3 non-ASCII
|    bytes from a Set-Content -Encoding utf8 write (the PS 5.1 trap,
|    live) stripped; all six files verified clean. Writer run 16/16
|    (265.22 s), GATE byte-identical 884,851 B. The INPUT-SIDE work for
|    the A4 RDO campaign: conformance-free.
|  - [x] **WAVE-9 CLOSE (judge, post-merge re-measure on master)**: full
|    rebuild + ctest 16/16 (264.09 s), GATE byte-identical 884,851 B
|    diff 0, coverage 21.4% (33,722/157,718): A11 40% (4,812). The A4
|    golden-regeneration campaign is blessed and opens next.
|    PAIRED-METRIC CAVEAT STATED: the instance lines move the coverage
|    number by transcription of the reference's own dispatch surface;
|    the algorithm cores were already landed and tested.
|    compute_sad_c.c and sad_av1.c land 100% of their portable surface,
|    grep-verified 0 of 66 missing. RED: per-increment C2039 compile
|    errors (sad4x8, sad8x4, sad128x128, the 22-size sweep).
|  - [x] **A14/HB1 (2026-11-07, a12ab21): the hbd twins complete the
|    four C_DEFAULT files - A14 COMPLETE** - DEVIATION RESOLUTION: the
|    hbd spans parked under the 8-bit discipline (PU1/AN8/CDEF1-era)
|    resolve as portable uint16 C; CONVERT_TO_SHORTPTR staging becomes
|    plain uint16 params (the CS1/LR1 precedent). Spans, 168 nb
|    measured span-by-span: variance.c:162-181
|    svt_aom_variance_highbd_c (16) -> packunpack::varianceHighbd;
|    variance.c:337-352 svt_aom_highbd_mse16x16_c (15) ->
|    packunpack::highbdMse16x16; cfl_c.c:46-59 svt_cfl_predict_hbd_c
|    (14) -> intra::cflPredictHbd; intra_prediction_c.c:15-37
|    upsample_intra_edge_high_c (21) -> intra::upsampleIntraEdgeHigh;
|    :64-92 highbd_dr_prediction_z3_c (27) -> intra::hbdDrZ3 (the
|    complete 54-config dy-surface enumeration);
|    picture_operators_c.c:176-:234 the two hb_col8 first/second-pass
|    helpers (26+26) + :238-:262 highbd_hadamard_8x8_c (23) ->
|    picops::highbdHadamard8x8. Brief non-reproductions named (its
|    ~17 nb z3 and 47 nb hadamard guesses did not reproduce).
|    Full-file inventory verdicts read every definition in all four
|    files and classified it; the last 44 nb = the declaration-shell/
|    glue surface, verified 1:1 before charging, license blocks
|    represented by provenance headers (not quoted).
|    derivation-relationship fixtures throughout (hbd == lbd widened at
|    bd 8); 7 mutations killed.
|  - [x] **WAVE-8 CLOSE / A14 COMPLETE (2026-11-07, 6a2bbfe)**: total
|    **21.0% (33,062/157,718)** - **A14 is the first COMPLETE area
|    (1,466/1,466 = 100%)**. Full rebuild -> ctest 16/16 (293.05 s),
|    GATE byte-identical 884,851 B diff 0, citations 0 out-of-range.
|    Remaining big blocks: A4 mode decision (3%), A10 picture mgmt (0%),
|    A3's ME driver surface (6%).
|  - [x] **A4/RE1 (2026-11-07, PERRY-RE1, cherry-picked 2b9ff79): the
|    rate-estimation core lands** in src/l16_rdo (new module, scaffold
|    bef6904; namespace l16_rdo) - the A4 golden-regeneration campaign's
|    foundation slice, CONFORMANCE-FREE (no emission change, no golden
|    churn - the wiring slices come later). md_rate_estimation.c spans:
|    svt_aom_get_syntax_rate_from_cdf :50-:75 (23 nb), the
|    estimate_syntax_rate INTRA surface :76-:269 (179 nb), the mv class
|    + nmv cost tables :370-:448 (74), svt_aom_estimate_mv_rate
|    :449-:485 (34), svt_aom_estimate_coefficients_rate :486-:607 (115),
|    the interintra-allowed helpers :608-:640 (26) + the header tables
|    (av1_prob_cost 8, the ext-tx tables 26) = 485 nb JUDGE-CHARGED
|    (the writer claimed 645; the judge struck the parked inter block
|    90 that was inside the quoted span and the 69-nb struct carry per
|    the AN17 precedent - both corrections named in the A4 row).
|    110,035 assertions; 10 mutations killed; M3's LESSON: an oracle
|    that reads the same fixture as the port cannot see a substitution
|    bug (the fixture was made divergent). The cfl sign-cost quirk
|    transcribed verbatim (the writer's first draft added sign_fac_bits
|    to BOTH planes - the oracle caught it, 128 failed). PARKED named:
|    update_stats :823-:1042 (211, BlkStruct-walled - the wiring
|    slices), update_part_stats :1043-:1105 (51, pcs-walled), the inter
|    block :270-:363 (90, intra-only emission). Writer run 17/17
|    (264.22 s), GATE byte-identical 884,851 B. The second commit
|    139b4c4 is the writer's own commit-message correction (estimates
|    -> measured, fixed forward per the HK1/L0 precedent).
|  - [x] **THE A4 GOLDEN-REGENERATION CAMPAIGN OPENS (the user blessed
|    it: 'wave 9 land, double check. go')**: the 1.6 ladder - RE1 the
|    rate-estimation core (LANDED, conformance-free) -> the RD-lite
|    SAD+rate composition -> angle-delta search -> tx-type search ->
|    trellis/optimize_b -> adaptive scan -> intra partition search.
|    The wiring slices change the emission: ALL NINE committed .obu
|    artifacts regenerate and the decoder gate re-runs as the planned
|    gated event. The conformance count moves WITH the coverage number
|    there - the paired metric working as designed.
|  - [x] **WAVE-10 CLOSE (judge, post-merge re-measure on master)**:
|    full rebuild + ctest 17/17 (262.85 s - l16_rdo is the 17th
|    target since bef6904), GATE byte-identical 884,851 B diff 0,
|    coverage 21.7% (34,207/157,718): A4 5% (1,418). The A4 campaign
|    continues: RE2 next.
------------------------------------------------------------
0.5 COVERAGE MAP (which roadmap items move the north-star metric)
------------------------------------------------------------

North star #1 in AGENTS.md is a QUALITY bar; the PROGRESS bar is the
portable-C coverage percentage, defined in AGENTS.md ("Completion
metric") and regenerated by `powershell -File tools\coverage.ps1`.
As measured 2026-11-07 (the wave-8 close) it is **21.0%** (33,062 of
157,718 non-blank lines in `Source/Lib/{Codec,C_DEFAULT}`). The table
below is REGENERATED from the live instrument at every refresh - never
hand-typed; the full percentage history and the per-slice movement
record live in `AGENTS.md` ("Completion metric") and the A0 register
above. The per-area denominator
lives in `tools/coverage_areas.txt`; the script measures, that file
judges, and the two must be re-run together after any edit.

This section exists so a reader can tell, per roadmap item, whether
completing it moves the metric. Read it before picking work.

| id | SVT area | non-blank | cov% | covered | this file |
|---|---|---|---|---|---|
| A1 | Transform/TX | 27,008 | 37% | 10,044 | 1.3 |
| A2 | Intra prediction | 3,478 | 91% | 3,156 | 1.1 (CFL deferred) |
| A3 | Inter pred + ME/MC | 17,738 | 6% | 1,006 | 2.2, 2.3, 2.4 |
| A4 | Mode decision + RDO | 31,088 | 3% | 933 | 1.6 |
| A5 | Entropy coding | 8,460 | 60% | 5,089 | 1.3, 1.4, 2.2 |
| A6 | Coding loop + PCS | 13,928 | 12% | 1,734 | 1.3, 1.5 |
| A7 | Bitstream + OBU | 1,436 | 40% | 574 | 1.4, 1.5 |
| A8 | Post-filters | 10,600 | 30% | 3,191 | 3 |
| A9 | Rate control | 5,951 | 5% | 278 | 2.5 |
| A10 | Pic mgmt + threading | 9,662 | 0% | 0 | 2.4 |
| A11 | Analysis + temporal | 12,028 | 35% | 4,152 | A0 (the AN-series, COMPLETE) |
| A12 | Palette + segmentation | 1,612 | 34% | 550 | 4 |
| A13 | Utility + headers | 5,357 | 17% | 889 | - |
| A14 | C_DEFAULT kernels | 1,466 | **100%** | 1,466 | A0 (the C_DEFAULT sweep, COMPLETE) |
| - | excluded: rtcd dispatch | 7,906 | - | 0 | - |
| | **TOTAL** | **157,718** | | **33,062** | |

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
0. CURRENT STATE (re-measured 2026-11-07 at the wave-8 close; supersedes
   the 2026-09-27 reading, which predated the AN-series waves and the A8
   campaign; the live per-slice register is section A0 above)
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
    d8 30B / d16 44B / d32 47B / d64 441B + color color4 183B /
    color8 137B / color16 133B / color32 165B
[x] MILESTONE: decoder-accepted AND content-bit-identical at EVERY
    geometry (libdav1d via ffmpeg; verify_decode4.ps1 -Geometry N);
    color full-plane 1:1 at ALL FOUR color artifacts (4/4 CLOSED:
    color16/color32 from CS5, color8 at CS7 `5ce0224`, color4 at RT21
    `927e697` - the chase record in docs/decode_conformance.md)
  [x] Infrastructure: tools/golden_gen (committed gate, 421 lines,
      884,851 B), tools/coverage.ps1 + coverage_areas.txt +
      coverage_baseline.txt (the progress metric), tools/cite_check.ps1
      (the doc citation audit), tools/doc_audit.ps1 (advisory numeric-claim
      audit), tools/td0_ladder.ps1 (7/7),
      tools/decode_handoff.ps1, tools/bench, tools/verify_decode4.ps1,
    tools/rt7_trace_diff.ps1, tools/rt8_divergence_map.ps1
[x] Docs: README, AGENTS.md layer map + completion metric,
    provenance.md, decode_conformance.md, bitstream.md, color.md,
    layers.md, emission.md, cs5c_deltas.md, this file
[x] MEASURED 2026-11-08 (wave-11 close): ctest 17/17 (295.11 s), GATE
    421/421 diff 0 (884,851 B), portable-C coverage 22.5%
    (35,428/157,718), 9 committed .obu artifacts (all content-1:1 on
    every plane)

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
- The FI DC-deciding fixture (LANDED, FI1 - test_pipeline.cpp:5507; the D2
  policy stays parked, the override is test-only, the gate did not move)
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

- **RT19/RT20 + RT21 LANDED at `927e697` - the c4 V 43-byte divergence is
  ROOT-CAUSED and FIXED, and color conformance is CLOSED 4/4.** RT18's
  negative result eliminated the shared path without naming a cause; the
  RT19/RT20 measurement arc named it, and RT21 fixed it. Every number below
  is read off commit `927e697`'s message and re-measured 2026-11-01, not
  transcribed from a brief.
  **RT19/RT20, the measurement arc in its final form.** The c4 V 43-byte
  divergence was root-caused to the chroma walk's above-right gather having
  NO Z-order availability check. At c4, leaf 41's owner block walks one
  step LATER than the leaf itself (zpos 36 vs 35), so the gather read zeros
  from the zero-initialised `cabove[65]` (composition.c:6609); the builder
  - verified 1:1 with the reference's repetition fill at intra.cpp:603-607
  - faithfully predicted 0 from the count it was given. The committed
  coefficients were proven generated against that zero edge (forward
  probe, bit-identical, eob 16), and the correctly-fed control encodes to
  all-zero coefficients (eob 0). ATTRIBUTION: the five-prediction
  derivation was run by an autoswarm child agent (Judge, glm-5.3-flash);
  P1-P5 all held.
  **RT21, the fix (`927e697`).** `topRightPx` - the ABOVE-RIGHT twin of
  bottomLeftPx, the same shape with the diagonal on the other corner
  (DECLARED pipeline.h:255 beside bottomLeftPx's pipeline.h:246; DEFINED
  pipeline.cpp:2480-2484 beside :2469-2473) - wired at the l6 builder site
  (pipeline.cpp:2073) where ONE count is consumed at BOTH the cAbove
  gather bound (:2113) and the buildIntraPredictorsUv argument (:2142), so
  the two cannot disagree; the generator mirror is `cNTr`
  (composition.c:6595-6596), likewise ONE fact consumed at the gather
  bound (:6613) and the builder call (:6733) - the two were previously the
  same structural expression written twice inline, and correcting only one
  would have re-created the bug through the back door. The chroma D2
  DECISION site (pipeline.cpp:2001) is deliberately untouched: the judge's
  scope is the builder's prediction surface.
  **REGEN (chroma-only).** 421/421 lines, 884,953 -> 884,851 B (0 CR, 0
  non-ASCII). FIVE moved lines, ALL chroma-side: ecs44_bytes 176 -> 161;
  ecs44_eobs (3 V triples: b41[2] 16 -> 0 THE fix; b48[2] and b49[2] 1 ->
  0 cascade); ecs44_coeffs (14 of 6144 values, V-side); ecs44_recon_v (43
  of 1024 bytes); tu_color4. ecs44_recon_v's moved positions are EXACTLY
  the 43 divergent V bytes the CS7 entry above records (leaf 41: 8, leaf
  42: 16, leaf 48: 3, leaf 56: 16) and every moved value goes to 255 -
  the fix corrects exactly the divergent bytes and nothing else.
  ecs44_recon_u: 0 bytes. Every ecs48_*/ecs416_*/ecs432_*/ecs2*_* line: 0.
  All modes lines: 0 (the decision sites are untouched, confirmed by the
  gate itself). color4 198 -> 183 B; color8 137 B / color16 133 B /
  color32 165 B byte-identical.
  **VERIFY, as committed and re-measured.** Suite ctest -C Release 9/9
  passed, 0 failed (the RT21 fix run 272.42 s; a judge re-run 267.47 s;
  re-measured 2026-11-01: 9/9, 278.85 s wall). GATE rebuilt and
  byte-identical to the committed regenerated file (Compare-Object
  0 lines, 884,851 B). Decoders: all four color artifacts now CONTENT 1:1
  on Y, U and V via tools/verify_decode4.ps1 (exit 0 each; re-run
  2026-11-01), with libaom-av1 and libdav1d byte-identical on all four -
  re-measured 2026-11-01: twelve planes, 0 divergent bytes each.
  **PARKED FINDING (exactly this name): the luma nTr at
  composition.c:6264 has the same structural-only gap, measured LIVE but
  UNCONSUMED.** The luma walk's above-right gather is structural-only
  (`hasTop && bx + 1 < grid`, :6264) with no Z-order check, and the same
  violating-leaf class exists there: 8 c4 + 2 c8 leaves. It is UNCONSUMED:
  no decided luma mode reads the stretch, the committed Y planes are
  content-1:1, and applying the rule shifts the D2 argmin (SMOOTH -> D45
  at 10 leaves), moving 13 gate lines. Parked, bounded by measurement,
  NOT fixed; opening it is a judge decision because it changes emitted
  luma content.

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
