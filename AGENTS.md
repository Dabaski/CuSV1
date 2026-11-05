 AGENTS.md

## Prime directive: incremental TDD only (Uncle Bob's Three Laws)

You do not write a feature, function, or file in one pass. You follow Robert C.
Martin's Three Laws of TDD as a **nano-cycle** -" run on almost a second-by-second
basis, not test-file-by-test-file:

1. You may not write production code until you have written a failing test.
2. You may not write more of a test than is sufficient to fail -" and a
   compilation/syntax error counts as a failure.
3. You may not write more production code than is sufficient to pass the one
   currently failing test.
These are training wheels, not superstition -" the point isn't the literal
order of test-vs-code, it's staying in small, always-verified steps instead of
writing a pile of code and hoping it's right. Treat "one behavior" as far
smaller than a feature: often one assertion, sometimes just enough to fail to
compile. If you can split a step into two smaller ones, split it.

**Never write implementation code before there is a failing test that requires it.**
**Never write more test than is needed to fail (including "fails to compile").**
**Never write more implementation than the minimum needed to pass the current test.**

## The loop (repeat constantly -" this is a nano-cycle, not a per-feature cycle)

1. **Pick the smallest next slice.** Smaller than you think. One assertion,
   or one line that won't even compile yet. Not "handle the whole function" -"
   just the next tiny fact about its behavior.
2. **RED -" write just enough test to fail.**
   - Write only the test. Do not touch implementation files in this step.
   - Stop as soon as it fails or fails to compile -" do not pre-write further
     assertions "while you're in there."
3. **Run the test suite and confirm it fails.**
   - Actually run it. Do not assume it fails -" show the failure or compiler output.
   - A compile error is a valid, expected RED state -" treat it as step 2's
     natural first failure, then move straight to step 4 to make it compile
     and fail correctly, or pass, whichever comes first.
   - If it passes immediately with zero new code, the test is wrong or
     redundant -" fix the test, don't touch implementation.
4. **GREEN -" write the minimum code to pass.**
   - Minimum literally means minimum: hardcode a return value if that's all
     the current test demands. Generalization comes later, driven by a test
     that forces it -" not written speculatively now.
   - No extra abstraction, no handling of cases you haven't written a test for yet.
5. **Run the full test suite and confirm everything passes.**
   - Not just the new test -" the whole suite. Regressions count as failure.
   - Do not stack new increments on top of a red suite.
6. **REFACTOR (only on green, and only here do you step back from the Three Laws).**
   - Clean up naming, remove duplication, generalize hardcoded values into
     real logic if the accumulated tests now justify it -" behavior must not change.
   - Rerun the full suite after refactoring. Must stay green before continuing.
7. **Report status, then go back to step 1.**
   - One line: what tiny slice was added, test name, pass/fail state.
   - Do not batch multiple loop iterations into a single summary -" report each one.
## Hard rules

- **RED evidence is mandatory in every slice report**: the failing doctest
  line (or compile error) is shown before GREEN. Refactor-only slices declare
  themselves and show green-before/green-after instead. A test that passes on
  first build must be proven discriminating (mutation check) or it counts as
  a skipped RED.
- **Commit messages carry measured values only** - register counts, SADs,
  MSEs quoted in a commit message come from an actual run, never from
  memory or estimation.
- **NEVER make a performance change without a before/after bench
  measurement** (tools/bench -> av1_bench, same config, both numbers in the
  commit message). A perf claim without paired bench lines is not done.
- **Citation rule: existence / negative-existence claims are grep-verified
  against the tree before being written.** Never cite a symbol as missing
  (or present) without checking; precedent: the HK1 l5 citation edit wrongly
  dropped compute8x8_sad_kernel_c as nonexistent (it exists at
  motion_estimation.c:71) and had to be fixed forward in L0.
  - **Citation rule: file:line citations into src/ or tools/ are re-verified
    by `tools/cite_check.ps1`, not by memory.** Those files move with every
    commit, so a line number written three commits ago is a guess. The script
    extracts every `file.ext:NNN` from the tracked markdown, resolves the path,
    and prints the ACTUAL bytes at that line for a claim-vs-content diff. It is
    a doc audit, NOT a build input and NOT a GATE line. **Regenerate the
    figures below with `powershell -File tools\cite_check.ps1` - they are
    instrument output, not a claim, and no commit hash is quoted here because
    a number measured on a working tree cannot name the commit that the edit
    producing it will create. As measured 2026-09-28:
    363 citations, 352 resolved in-tree, **0 out-of-range**, 11 unresolvable
    (the AV1 spec markdown plus three dav1d decoder internals, which are not
    vendored - check those against the external checkouts by hand). The script
    prints the scanned doc set's size and a content hash WITH every count, so a
    disagreement about the total is settled by the doc-set identity rather
    than by argument: a citation count is a function of the doc set, and the
    doc set moves whenever any tracked doc is edited. **The out-of-range count
    went 1 -> 0 because the audit's own finding was applied**: the single
    out-of-range claim was a bare `CMakeLists.txt` line reference in
    `tools/golden_gen/README.md` meaning SVT-AV1's OWN build options
    (the MINIMAL_BUILD / RTC_BUILD definitions, lines 53 and 86), which
    resolved against the 39-line ROOT `CMakeLists.txt` instead. It is now
    qualified as SVT-AV1's, and the resolver matches a cited partial path on
    its SUFFIX rather than its bare basename, so a partial prefix can no
    longer land on an unrelated file. The failure mode the rule exists to
    catch is real and was live: `docs/cs5c_deltas.md` had 15 wrong
    `transform.cpp` line numbers because `transform.cpp` grew by ~700 lines
    after that doc was written.
- **REFERENCE PINNING**: the project's 1:1 reference is the VENDORED SVT-AV1
  snapshot in `third_party/SVT-AV1/` - a 4.2-era snapshot (CHANGELOG 4.2.0,
  2026-07-14) that is NOT byte-identical to the official v4.2.0 tag, and
  upstream master has diverged further. The vendored tree is pinned and is
  NOT to be updated without a court order (updating it would invalidate
  every golden and provenance citation). The north star is therefore
  "bit-exact 1:1 with the pinned vendored SVT-AV1 reference".
- **Goldens come from tools/golden_gen** (committed generator), cited with
  generator line + SVT provenance. Hand-traces are commentary, never
  expected values.
- **Deviations and limitations are named in the slice report**, never
  silent.
- **Range conditions are enumerated, never spot-checked** (audit rule, R-series):
  any mode/size/index range in a port (e.g. `isDr = m >= 1 && m <= 8`) must be
  checked against the reference for EVERY value it admits, not just the ones
  the current tests exercise. Precedent: B6's predict_block_8x8 shipped with
  H_PRED (m==2) inside the dr range - acute bug caught in QW3, latent V/H
  angleDelta divergence caught in the QW audit; both were invisible to the
  spot-checked subset of modes.
- **ASCII-only authoring: never write em-dashes or ANY non-ASCII characters
  in source, docs, commit messages, or reports.** Use "-" or "->" instead.
  Non-ASCII bytes are the mojibake class: PowerShell's default-encoding
  writes (Set-Content without -Encoding utf8) have corrupted this repo's
  files three times (Q1 comment lines, the todo.md incident, the CS0
  insert). The Edit tool is safe; byte-level writes must be explicit UTF-8.
  Legacy non-ASCII already in tracked files is repair-on-touch, not urgent.


- **A slice's test and its implementation land in the SAME commit, and the
  suite is GREEN at commit time. There are no red commits and no
  test-only commits.** This line replaced "Do not write test and
  implementation in the same edit/commit", which was the opposite rule and
  which the practice had already abandoned for a dozen slices. The reason
  it had to be abandoned rather than obeyed: a test-only commit is a RED
  tree, and this repo's rule for a commit is that the full suite is green
  at commit time, so the two rules are jointly unsatisfiable and the
  practice silently picked one. Keep the sequence, drop the separate
  commit: write the test, run it and capture the failing output, write the
  implementation, re-run the FULL suite, then commit both together.
- **The RED evidence does not live in the tree, it lives in the commit
  message.** The test, the failing output that motivated the code, the
  pass after, and the full-suite result are all quoted in the message
  because the tree at that commit can only ever show the green state. A
  slice with no RED evidence in its message is incomplete, even if its
  tree is green. Precedent for why the distinction matters: the frozen
  `cs7_prefix_baseline.inc` had to record the pre-fix bytes somewhere,
  because after the fix landed nothing in the repo could show them.
  **PROVENANCE of these two rules, since they arrived by accident and a
  reader deserves to know that.** They are on disk at `AGENTS.md:129-147`
  and they have no commit of their own. They were the court author's
  uncommitted edit when this file was staged for `c6836e6` - a commit
  titled for the throwaway's retirement, which is a different subject
  entirely - and they were swept in with it, so the commit that carries
  them says nothing about them. Measured, not inferred: `git log -S` for
  "There are no red commits" over `AGENTS.md` returns `c6836e6` and
  nothing else, and that commit's own diff carries these paragraphs
  verbatim. **The two rules above replaced the OPPOSITE rule** - "Do not
  write test and implementation in the same edit/commit" - which the
  practice had already stopped obeying for about a dozen slices, and
  which was jointly unsatisfiable with this file's own commit rule: a
  test-only commit is a red tree, and a commit requires a green suite.
  Two rules, each individually reasonable, cannot both be honoured, and
  the practice resolved it silently by picking one. Recording the
  replacement is the point; the accident of the carrier is noted only so
  the next reader does not go looking for a commit that is not there.
- Do not write a test you already know will pass -" if it passes on first run
  without new code, you skipped ahead or the test is too weak.
- Do not write more than one new assertion at a time. If two things need
  testing, that's two trips through the loop, not one.
- Do not generalize ahead of the tests. Hardcoded/special-cased GREEN code is
  correct and expected early on -" let a future failing test force the
  generalization, don't anticipate it.
- If a task looks big, your first job is to decompose it into an ordered list
  of small slices before writing any test. Show that list before starting the loop.
- If you get stuck failing the same test after a couple of honest attempts,
  stop and explain what you're seeing rather than guessing repeatedly.

## Parallel-work protocol

- Each agent works ONLY its assigned partition (directories named in the
  assignment). Any file outside it requires a court order first.
- The GATE (`tools/golden_gen`) and `expected_primitives.txt` are SHARED
  infrastructure: generator additions are serialized - one agent's gate-line
  additions per commit, rebase before adding; never edit
  `expected_primitives.txt` concurrently. Coordinate via the user.
- Every commit: full 7/7 suite green + GATE diff 0 (per AGENTS.md).
  If the suite is red at your commit time because the OTHER agent's WIP broke
  it, halt and report - do not fix another partition.
- Commit cadence unchanged: one slice = one commit, RED evidence, measured
  values, citation rule.
- Branch strategy: both agents commit to master sequentially through the user
  (small slices make rebasing trivial); no force-push.

## Test command

Determine the project's test command before starting work. Check, in order:

1. `package.json` -' `npm test`, `npm run test`, or `yarn test` / `pnpm test`
2. `Cargo.toml` -' `cargo test` (optionally `cargo test -p <crate>` for workspace members)
3. `pyproject.toml` / `setup.py` / `pytest.ini` -' `pytest`
4. `go.mod` -' `go test ./...`
5. `CMakeLists.txt` / `Makefile` -' `ctest` or `make test`
6. `*.csproj` / `*.sln` -' `dotnet test`
7. A custom script documented in the repo's README or CI config
If no test runner exists yet, the first task is to add the minimal test
harness for the language/framework in use -" itself done via the nano-cycle
(the first test: "the test runner can run a trivial passing test").

Run the full suite with whatever command applies. Run a single test by name
or file when iterating on one RED/GREEN step, then always re-run the full
suite before moving on.

### This project's actual commands (step 5 applies: CMake + ctest)

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release              # 11 targets; expect 11/11
ctest --test-dir build -C Release -R l4_intra  # one layer
```

The **GATE** is separate from ctest and is also separate from the coverage
metric: build the generator and diff it against the committed
`tools/golden_gen/expected_primitives.txt`.

```
cmake --build build --config Release --target golden_primitives
# THE REDIRECTION IS PART OF THE GATE. `1> out.txt` in Windows PowerShell
# 5.1 writes UTF-16LE WITH A BOM, so out.txt measures 1,770,750 B against a
# committed 884,953 B file and a byte comparison reports a ~886 KB difference
# on a PASSING tree. Do not "fix" that by regenerating the file.
$o = & "build\tools\golden_gen\Release\golden_primitives.exe" 2>$null
[IO.File]::WriteAllText("out.txt", (($o | Out-String) -replace "`r",""),
                        (New-Object Text.UTF8Encoding $false))
# out.txt must now be 884,851 B and byte-identical to expected_primitives.txt
Compare-Object (Get-Content tools\golden_gen\expected_primitives.txt) `
                  (Get-Content out.txt)     # must print nothing
```

**THE `1>` TRAP, measured 2026-09-28, because it is the highest-value
defect found in this arc and it is SILENT.** Three byte counts for one
passing run of the same binary: `1>` gives **1,770,750 B**, an ASCII pipe
gives **885,374 B**, and the committed file is **884,953 B**. The first is
UTF-16LE (`FF FE 63 00` = BOM then `'c'`), the second is ASCII but still
CRLF-terminated, and the third is ASCII + LF. The ASCII-vs-LF gap is exactly
**421 bytes = the line count**, which is the tell. The `1>` route is not
merely imprecise: it CANNOT match, so every run reports the gate as failed,
and the intuitive response - regenerate `expected_primitives.txt` - converts
a reporting bug into a corrupted golden that the real gate then ratifies.

Three other instruments, none of which gate a commit:
  `tools/verify_decode4.ps1` (decoder acceptance + content-1:1, real decoders),
  `tools/coverage.ps1` (the progress metric, below), and
  `tools/cite_check.ps1` (the doc citation audit). A fourth,
  `tools/doc_audit.ps1`, audits NUMERIC claims in prose; it is ADVISORY ONLY,
  always exits 0, and is deliberately wired into nothing. **It HAD a
  three-condition blind spot on the gate byte count, since FIXED in `32b2c57`;
  the mechanism is recorded here because the reasoning behind it is the
  transferable part.** The check could not fire on any of the four wrong gate
  figures this tree carried, because it needed THREE conditions on the same
  line and no stale site met all three:
  1. the line names the gate file (`expected_primitives` and friends) - its
     context guard is narrower than its own `$gateCtx`, deliberately, because
     the bare word "gate" is too loose;
  2. the unit is the abbreviation `B` - the regex ended `\s*B\b`, which the
     word "bytes" does not satisfy;
  3. **the figure is punctuated the way the regex expects** - the comma branch
     was `{2,3}` comma-groups, so a ONE-comma figure like `884,953 B` never
     matched at all, and only the unpunctuated `\d{6,9}` branch or a
     three-group figure like `1,770,750 B` did.
  Condition 3 is the sharpest of the three, because it means **this file's
  own gate figure was invisible to the check guarding it** - `884,953 B` is
  one comma, and the check could only see `884980 B`. `32b2c57` widened the
  branch to `{1,3}` and the unit to `B\b|bytes?\b`, and added a `-SelfTest`
  that PASSES 4/4 (cases A-D, including the `out.txt` UTF-16 case that must
  NOT be flagged), plus a live tree scan that now finds 6 gate candidates
  where the pre-fix scan found 4. Verified 2026-09-29.

  **PROVENANCE, corrected, because the first version of this paragraph was
  wrong in a way that is worth recording.** It said "measured at `db83ea2`",
  and a later reader took that to mean the instrument had been introduced or
  broken there. It had not. `db83ea2` touches exactly three files -
  `AGENTS.md`, `docs/decode_conformance.md`, `docs/todo.md` - and no tools
  file, which is true. But `tools/doc_audit.ps1` was **already in the tree at
  `db83ea2`**: it was created at `b246d44`, and
  `git merge-base --is-ancestor b246d44 db83ea2` succeeds. The original
  paragraph was also self-invalidating, because its worked example cited
  `AGENTS.md` line 211 as a spelled-out-"bytes" case, and `db83ea2` had
  already corrected that very line to `884,953 bytes` - the doc was citing
  evidence it had itself just repaired. The measurement underneath was real:
  the tool WAS run at `db83ea2` and DID report 0 mismatches over a tree
  holding four wrong gate figures, and that output is quoted in that commit's
  own message. The error was the provenance, never the number.
  **THE LESSON, and it is about direction of inference: a claim about what a
  commit contains is a claim to be MEASURED, never inferred from which agent
  was working on what.** The specific trap here is an inference that runs
  BACKWARDS - "this file has one commit, therefore it did not exist at the
  later commit" is false, because a file created at commit X exists at every
  commit after X. The correct reading of "exactly one commit" was "it has
  existed unchanged since it was created", which is what made the measurement
  at `db83ea2` valid in the first place. Two independent readers (a briefing
  and the fix commit's own message) made that inverted inference in the same
  direction on the same fact, which is the strongest argument in this arc
  for checking the tree instead of reasoning about it: the claim was specific,
  plausible and checkable, and no amount of careful reading would have
  produced it without asking git.

  **THE PATTERN IS THE FINDING, NOT THE FIX: conditions that each look merely
  NARROW, rather than a broken conjunction, observed THREE times in one arc.**
  - `bcf16_modes` colon-vs-space: a parser written for the ecs4 shape
    mis-reads bcf16 twice over, the colon and the three-per-leaf arity.
  - the `1>` UTF-16 redirect: a command that cannot match, whose naive fix
    corrupts the golden it is meant to verify.
  - the gate byte check above: three jointly necessary conditions, one of
    which made this file's own figure invisible to it.
  **And a FOURTH, which is the same shape with a destructive edge:** a `rm`
    aimed at a briefed path that does not exist. `%TEMP%\opencode\dav1d_td4`
    was the briefed path for the throwaway dav1d tree; the tree is actually at
    `%TEMP%\dav1d_td4`, one level up. The delete would have removed nothing,
    returned success, and reported the tree gone - indistinguishable from a
    real delete by its output. **All four share one property: the failure
    mode is to look like success.** The `1>` redirect produced a file that
    could not match and made a passing tree report as failing; the gate regex
    was narrow enough to make a wrong tree report as clean; the `bcf16`
    parser mis-read a shape it was not written for; and a wrong-path delete
    reports a removal that never happened. **Treat "looks like it worked" as
    the review question, not "looks like it failed"** - a visible failure is
    the safe case, and the dangerous ones are the ones whose output agrees
    with what you hoped. The escape is identical in each: measure the SUBJECT,
    not the exit code.
  Treat "looks a bit narrow" as a REVIEW QUESTION rather than a reassurance,
  and prefer a check whose coverage you can demonstrate - `cite_check.ps1`
  prints the doc-set hash, `coverage.ps1` prints the areas-file hash and the
  reference pin, and `doc_audit.ps1` now carries a self-test - to one whose
  silence reads as health. A stale instrument and a clean tree look
  identical, which is why a self-test is the only thing that distinguishes
  them.

### Last measured state (2026-11-04, the GR1/AN21/CV1 wave)

Quote these numbers only with their date; they are re-measured, not permanent.

- **ctest: 13/13 passed, 0 failed, 264.97 s** (re-measured 2026-11-04 at
  the GR1/AN21/CV1 wave; the suite is 13 targets since the scaffold
  1e9ac65 - l11_resize and l12_convolve joined, and l10_filters now also
  runs the grain tests; l4_intra is the slow one; `pipeline` and
  `l3_transforms` next; the rest under 0.5 s.
  Earlier runs of the same tree: the LR2 run 260.65 s (11 targets), the CDEF3+LR1 run 280.41 s, the CDEF1 run 269.66 s, the DF2 run 270.04 s, the AN12 run
  275.59 s at 10/10
  (l4_intra 213.58 s, `pipeline` 53.74 s, `l3_transforms` 7.24 s),
  a first AN12 run 269.63 s, the AN11
  run 264.78 s, a second AN11 run 264.07 s, the AN10
  run 266.17 s, the AN9
  run 262.05 s, the AN8 run
  261.66 s, the AN7 run
  263.08 s, the AN6 run
  261.59 s, the AN5 run 268.30 s, the AN4 run 270.10 s, the AN3 run
  272.67 s, the AN2 run 272.62 s, the AN1 run 263.38 s, the RT21 fix
  run 272.42 s at 9/9, a judge re-run 267.47 s).
- **GATE: 421/421 lines, diff 0.** `expected_primitives.txt` is **884,851
  bytes**, 0 CR, 0 non-ASCII (re-measured 2026-11-04 at the GR1/AN21/CV1
  wave: rebuilt, and byte-identical to the committed file; no generator
  input in the A8/LR/GR/AN21/CV slices). History, not a figure to chase:
  884,980 B was the value from RT10b through the doc-currency pass, and
  884,961 B before RT10b. The CS7 regen moved it to 884,953 B by 27 B -
  13 gate lines, all chroma-side - and the RT21 regen moved it to 884,851 B
  - five moved lines, all chroma-side (ecs44_bytes, ecs44_eobs,
  ecs44_coeffs, ecs44_recon_v, tu_color4). The earlier numbers are correct
  statements about their own commits. Always re-measure; `git log` carries
  stale numbers by design.
- **Portable-C coverage: 17.5%** (see the completion metric section;
  re-measured 2026-11-04 at the GR1/AN21/CV1 wave: 27,575 of 157,718
  non-blank lines, +1,617 from 25,958 at LR2 - GR1 +998 in A8 (the grain
  core; judge band 995-1,020), AN21 +427 in A11 (328 algorithm + 99
  resize.c tables) + 28 in A8 (the super_res.c normative table), CV1 +163
  in A13 (the convolve family, 161-163 judge band); all span-measured,
  register entries in docs/todo.md section A0; A8 prints 28% (2,942 of
  10,600), A11 prints 30% (3,600 of 12,028), A13 prints 9% (477 of
  5,357). All of it is
  COVERAGE-ONLY: it moves no conformance count; the printed percentage
  history: 23,207/157,718 = 14.7168% crosses the 14.65 boundary (CDEF1
  prints 14.7%); 22,840/157,718 = 14.4807% crosses the 14.45 boundary (DF2
  prints 14.5%); 22,745/157,718 = 14.4204% (DF1) and 22,530/157,718 =
  14.2841% (AN12) crossed 14.25, so AN12 printed 14.3% and the AN10/AN11
  runs printed
  13.8% (21,778/157,718 = 13.8082% crosses the 13.75 rounding
  boundary the AN9 13.5685% sat below). Earlier history of the same
  metric: the AN9 run measured 21,400/157,718 = 13.5685%; the AN8 run
  measured 21,347/157,718 = 13.5349%; the AN7 run measured
  20,918/157,718 = 13.2629%; the AN6 run measured 20,803/157,718 =
  13.1900%; the AN5 run 20,774/157,718 = 13.1716%; the printed
  percentage MOVED at AN4: 20,756/157,718 = 13.1602% crosses the 13.15
  rounding boundary that AN1-AN3's 13.1443% sat below, so N1 prints
  13.2% where AN1-AN3 printed 13.1%).
- **Committed artifacts: 9** `.obu` files, ALL content-1:1 on every plane
  (color4 was the last divergent one: its V 43/1024 residual was the chroma
  above-right Z-order gap, closed by RT21 at 927e697; color8 closed at CS7,
  5ce0224). Sizes (2026-11-01): d4 30 B / d8 30 B / d16 44 B / d32 47 B /
  d64 441 B / color4 183 B / color8 137 B / color16 133 B / color32 165 B.
  See `docs/decode_conformance.md` for the authoritative matrix rather than
  a summary line, because that matrix is what changes as the RT-series
  lands.

## General dev environment guidance

- **Before writing any code**, read the project's existing structure,
  conventions, and neighboring files. Mimic the style already present -"
  naming, formatting, imports, error handling, logging.
- **Never assume a library is available** even if it's well-known. Check
  `package.json`, `Cargo.toml`, `pyproject.toml`, `go.mod`, etc. for
  dependencies actually declared in the project.
- **Language/framework conventions take precedence over personal preference.**
  If the codebase uses Option over Result, or async/await over callbacks, or
  a specific linter config, follow it.
- **Run lint and typecheck** after reaching GREEN, alongside the test suite.
  Typical commands: `npm run lint`, `npm run typecheck`, `cargo clippy`,
  `ruff check`, `mypy`, `golangci-lint run`, `dotnet format --verify-no-changes`.
  If the repo doesn't document these, check the CI config (`.github/workflows/`,
  `.gitlab-ci.yml`, etc.) for the exact commands and use those. If still
  unknown, ask the user and record the answer here for future sessions.
- **Environment-specific skips:** tests that require hardware, a specific OS
  feature, an external service, or a large binary should skip gracefully
  rather than fail. Detect availability at runtime and return early with a
  clear `eprintln!` / `print()` / `console.warn()` message starting with
  `SKIP:` so it's visible in CI output but doesn't fail the build.
- **Platform notes:** this machine runs Windows. Prefer cross-platform
  commands where possible. When a Windows-specific tool is needed, quote
  paths containing spaces and prefer full cmdlet names in PowerShell.
## CUDA/GPU-specific addendum (SVT-AV1 CUDA 12.2 port)

The Three Laws still apply, but "GREEN" needs sharper definitions for GPU
code than for ordinary application code. Correctness and performance are
separate axes here, and a naive kernel can be functionally correct while
being close to useless. Do not conflate them.

- **Correctness-green is not performance-acceptable.** A passing kernel test
  only claims "produces correct output," never "is fast enough to ship."
  Do not mark a slice as done, or move on to the next slice, on the strength
  of a passing correctness test alone if the task was a performance-motivated
  port (e.g. replacing an existing SIMD path). Track perf status separately -"
  see below.
- **Floating point comparisons need an explicit tolerance, decided up front.**
  GPU FP results will not bit-match the existing AVX2/AVX-512 CPU reference
  paths in SVT-AV1. Before writing the first RED test that compares GPU
  output to a CPU reference, decide and document the tolerance (ULP-based or
  epsilon, whichever fits the operation) in the test file itself as a
  comment. Do not pick a tolerance ad hoc per test -" inconsistent tolerances
  make it impossible to tell a real regression from noise. If a comparison
  fails and you're not sure whether it's a bug or expected rounding
  divergence, that's a stop-and-explain situation, not a guess-and-loosen-
  the-tolerance situation.
- **GPU availability is an environment skip, not a failure.** Tests requiring
  an actual CUDA device, a specific compute capability, or VRAM beyond what's
  available must use the `SKIP:`-prefixed early-return pattern already
  defined above. Do not "fix" a missing-GPU skip by weakening or removing the
  assertion -" the skip exists so the test suite stays honest on machines
  without the hardware, not so the test becomes meaningless everywhere.
- **A kernel can pass and still be badly formed.** Occupancy problems,
  register spills to local memory, uncoalesced memory access, and warp
  divergence are all invisible to a functional correctness test. When a
  kernel reaches GREEN, note in the step-report whether a compile/PTX-level
  sanity check (e.g. `nvcc --ptxas-options=-v` register/spill output) was
  reviewed. This isn't a new law-breaking step -" it's a checklist item during
  REFACTOR, not a reason to add unrequested generalization.
- **Perf regressions get their own check, run less often than the unit
  suite.** Do not fold throughput/benchmark assertions into the nano-cycle's
  RED/GREEN loop -" they're too slow and too noisy to gate every tiny slice.
  Instead, maintain a separate benchmark pass (documented once a harness
  exists) that's run at slice-group boundaries, not per-assertion. If no
  such harness exists yet when the first performance-sensitive kernel lands,
  say so explicitly rather than silently skipping perf verification.

## Benchmark harness (BM-series, tools/bench)

- Run: `cmake --build build --config Release --target av1_bench` then
  `build\tools\bench\Release\av1_bench.exe` with the CUDA toolkit `bin` on
  PATH (same requirement as the test exes). Not a ctest.
- What it reports: GPU name/driver/clocks at run time (nvidia-smi query),
  host vs GPU composite auto-frame encodes (lossless + q100, 4x4 and 8x8,
  64x64 frame tiled from the B7 fixture, 30 iters + 3 warmup, median/min),
  and per-stage single-launch kernel timings (predict/subtract/fwd/quant/
  inv, both geometries). Every GPU configuration runs an untimed
  bit-exactness verification vs host first and withholds timings on
  mismatch. MEASURED VALUES ONLY: every number quoted anywhere comes from
  an actual run on this machine.
- Known caveat baked into the tool's output header: the composite loop is
  HOST decision + per-block synchronous kernel launches - the numbers
  include per-launch/per-copy sync overhead per block. They measure the
  current structure, not the kernels' potential.
- **Rule: perf-relevant slices must quote before/after bench lines** (the
  same config from this tool, same machine, clocks as recorded at run
  time). A perf slice without bench lines is not done.
- Baseline (2026-09-10, RTX 3090, driver 591.86; clocks unlocked, SM clock
  varied 210-1695 MHz across runs - record what nvidia-smi prints):
  host 4x4L 0.374 / 4x4Q 0.403 / 8x8L 0.244 / 8x8Q 0.268 ms (median);
  gpu 4x4L 227.0 / 4x4Q 390.7 / 8x8L 93.4 / 8x8Q 99.5 ms (median);
  per-stage single launches ~0.056-0.061 ms (4x4 all stages; 8x8 predict
  0.085, rest ~0.059-0.063 ms).
- Parked perf candidates (do not build unprompted): per-block launch set
  elimination (streams/graphs/wavefront), memory pooling, clock locking
  via NVML. Clock locking is the first candidate: baselines above are
  noise-sensitive without it.


## Layer map (current)

North star: a working, bit-exact 1:1 port of SVT-AV1 on CUDA 12.2 - every algorithm traceable to the pinned vendored reference in third_party/SVT-AV1 (REFERENCE PINNING rule above); the target is bit-exact 1:1 with the pinned vendored SVT-AV1 reference, not with upstream.

- l0_core - minimal types (Sample, BlockSize) shared across layers.
- l1_pixels - pixels::Plane (strided pixel buffer with padding).
- l2_gpurt - NVRTC JIT + driver-API runtime (GpuContext, DeviceBuffer, Kernel, ptxEntryNames); kernels are CUDA C++ source strings compiled for compute_61. No CUstream anywhere: Kernel::launch uses the hard-coded stream 0 and a cuCtxSynchronize per launch (gpurt.cpp:104-115), so there is no batching, no async copy and no graph capture.
- l3_transforms - SVT-AV1 fixed-point transforms at 4x4 / 8x8 / 16x16 (C-series) then 32x32 / 64x64 (L-series). Host 1D kernels: fdct4/fadst4, fdct8/fadst8, fdct16/fadst16 (verbatim svt_av1_fdct/fadst_N_new) and idct4/iadst4, idct8/iadst8, idct16/iadst16 (verbatim, clamps only where SVT consumes stage_range: idct16 stages 3-7, iadst16 stages 3/5/7; iadst16 has no all-zero early-out at 16); fdct32/fadst32 + idct32/iadst32 (idct32 clamps ONLY stages 3-9, iadst32 clamps EVERY stage, no all-zero early-out at 32); fdct64/idct64 DCT-only within our TxType scope (av1_txfm_type_ls[4] = {DCT64, INVALID, INVALID, IDENTITY64}, inv_transforms.h:196 - no ADST is signalable at TX_64X64; the IDENTITY64 column is parked, not ported) - ported mechanically from the committed extract svt_gen.c with the SVT pointer-swap structure preserved, fdct64B takes cos_bit. Host 2D: fwdTxfm2d4x4/8x8/16x16/32x32/64x64 + invTxfm2dAdd4x4/8x8/16x16/32x32/64x64 (64x64 DCT-only; the 64x64 fwd has net shift 0 so the roundtrip is lossy like 8x8/32x32). cos_bit findings from the SVT tables (transforms.c:19-22, inv_transforms.h:32-41): fwd 4x4 13/13, 8x8 13/13, 16x16 col 13 / row 12, 32x32 12/12, 64x64 col 13 / row 10 (the only pass below 12 - fdct16/fadst16 and fdct64B are cos_bit-parameterized via cospiRow = cospi_arr mirror); inv is 12/12 (INV_COS_BIT) at every size. Shifts: fwd {2,0,0} / {2,-1,0} / {2,-2,0} / {2,-4,0} / {0,-2,-2}; inv {0,-4} / {-1,-4} / {-2,-4} / {-2,-4} / {-2,-4}. Quantizer stage: buildQuantTables (luma rows of svt_av1_build_quantizer, sharpness=0), size-parameterized quantizeFpN/quantizeBN (verbatim semantics of quantize_fp_helper_c / svt_aom_quantize_b_c) with 4x4/8x8/16x16 (log_scale 0) / 32x32 (log_scale 1) / 64x64 (log_scale 2) wrappers per av1_get_tx_scale_tab (full_loop.c:22) - the escalated arithmetic (rounding ROUND_POWER_OF_TWO(round, log_scale), threshold << (1+log_scale), quant >> (16-log_scale), dq >> log_scale) cited in the L5/L9a commits; defaultScan4x4/8x8/16x16/32x32/64x64 (svt_aom_init_iscan formula); dc/ac unified via table index [rc != 0] - this SVT tree has no av1_quantize_dc. GPU twins at every size: fwd_txfm_2d_4x4..64x64, inv_txfm_2d_add_4x4..64x64, quant_dequant_4x4..64x64 (32x32 fwd/inv cos_bit 12 via kC12f/kC12; 64x64 d_fdct64 selects bit 13 col / 10 row; quant_dequant_64x64 = 4 scan positions per thread). MSVC toolchain limit named: raw string literals > ~16.8KB fire C2026 - all CuSource functions are += chunked (<= ~13.5KB per chunk). Integer only.
- l4_intra - buildIntraPredictors (1:1 with SVT build_intra_predictors, luma, size-generic over 4/8/16/32/64, DC availability variants, missing-neighbor fills), drZ1/2/3 + drPredictor, edge filter/upsample, smoothPredict family, filterIntraPredictor. Corner blend (enc_intra_prediction.c:190-191 8-bit, :392-393 16-bit, the guard `txwpx + txhpx >= 24`): dead at 4x4/8x8 (sums 8/16), LIVE at 16x16 (32), 32x32 (64) and 64x64 (128) - the 16x16/32x32/64x64 GPU kernels carry it. Upsample never fires at 16x16/32x32/64x64 (blk_wh 32/64/128 > 16, svt_aom_use_intra_edge_upsample). GPU twins: predict_block_4x4, predict_block_8x8, predict_block_16x16 (256 threads, shared sizing: above/left max 32 used, zone-1 maxBaseX = 31, above[31] reach within 96-byte arrays), predict_block_32x32 (1024 threads = the CUDA block maximum, __launch_bounds__(1024) after CUresult 701 at naive 1024-thread/64-reg; zone-1 maxBaseX = 63, edge-filter nPx = 65, corner blend live) and predict_block_64x64 (1024 threads, FOUR pixels per thread p = t + 1024k - the stated thread-map design; zone-1 maxBaseX = 127, edge-filter nPx = 129 within 256-byte shared arrays; filter-intra NOT signalable at 64x64 so the 64x64 kernel has no FI path).
- l5_motion - motion::sad4x4 / sad8x8 / sad16x16 / sad32x32 / sad64x64 (strided uint8) + GPU kernels for 4x4 and 8x8 only; the 16x16 / 32x32 / 64x64 D2 policies score host-side (frame GPU tests are host-decides-gpu-executes, like 8x8). Provenance: sad8x8 mirrors the dedicated 8x8 kernel compute8x8_sad_kernel_c (motion_estimation.c:71); sad4x4 / sad16x16 / sad32x32 / sad64x64 mirror svt_nxm_sad_kernel_helper_c at those dims (compute_sad_c.c:21). (HK1 correction: both citations restored - the HK1 edit had dropped compute8x8_sad_kernel_c, which does exist at motion_estimation.c:71.)
- l6_pipeline - block + frame composition and DECISION at all five geometries: 4x4 (encode
Block4x4 / encodeRecon4x4 / encodeFrameRecon4x4 / encodeFrameAuto4x4 / encodeFrameAuto4x4Q), 8x8 (encodeFrameRecon8x8 / Auto8x8 / Recon8x8Q / Auto8x8Q), 16x16 (encodeFrameRecon16x16 / Auto16x16 / Recon16x16Q / Auto16x16Q), 32x32 (encodeFrameRecon32x32 / Auto32x32 / Recon32x32Q / Auto32x32Q + decideBlockMode32x32) and 64x64 (encodeFrameRecon64x64 / Auto64x64 / Recon64x64Q / Auto64x64Q + decideBlockMode64x64, DCT-only within our TxType scope) - raster grids, intra-only, each block predicting from RECONSTRUCTED neighbors (M1 availability rules) with the FR-series REAL recon top-right gather in every variant (above[B..2B-1] = recon[(py-1)][px+B..px+2B-1]; row above fully reconstructed by raster order). Each Auto variant's mode is CHOSEN by the D2 SAD policy per geometry (decideBlockMode4x4/8x8/16x16/32x32/64x64 - policy is ours; primitives 1:1) evaluated against reconstructed edges; chosen modes feed NeighborContext (filt_type live) = plane window (l1) + buildIntraPredictors (l4) -> int16 residual (no clamp) -> fwdTxfm2d (l3) -> invTxfm2dAdd (l3 inverse) onto the same predictor. GPU frame paths: per-block kernel chains (predict_block / subtract_4x4..64x64_plane / fwd_txfm_2d / quant_dequant / inv_txfm_2d_add), edges gathered host-side from the device recon buffer; ALL FIVE geometries are bit-exact vs host in both lossless and q100 GPU frame tests. The 16x16 fwd/inv roundtrip is exact (recon == source at 16x16); 8x8 / 32x32 / 64x64 are lossy by design (fwd shift sums -2, 0 for 64). Goldens captured from SVT's own C in the committed generator (tools/golden_gen). Emission sites (TD5b): encodeFrameAuto16x16 (pipeline.cpp:832) inits the frame context with q0 (CodedLossless, bucket 0, pipeline.cpp:840) and encodeFrameAuto16x16Q (pipeline.cpp:982) passes the frame qindex (pipeline.cpp:991) - the l7 init is qindex-bucketed (see l7). FS-series (CLOSED, content-1:1 at all five geometries): the 4x4Q/8x8Q/32x32Q/64x64Q loops take optional w/fc/na (defaults nullptr) and emit the full per-block walk [partition iff the frame reads one: 8x8 = the 4-symbol row, 16/32/64 = the 10-symbol rows, 4x4 = none][skip=0 ctx 0][kf mode + the angle-delta symbol for directional at bsize >= 8X8][FI iff DC_PRED && bsize <= 32x32][writeBlockCoeffs] with initDefaultEcFrameContext(fc, qindex) (the bucketed init, see l7) - bit-exact vs the fs2S_* gate lines per size (the 64x64 emission-domain = the TX_64X64 scan contract: compact the fwd64 top-left 32x32, quantize n_coeffs=1024 with the 1024-position token scan via quantizeFp64x64Token, expand the compacted dqcoeff for inv64; the legacy/GPU callers keep the 4096 facade - the FS5d named follow-up); the 8x8Q/32x32Q/64x64Q emit RUNNING partition contexts (updatePartitionContext after each coded block; for uniform grids these coincide with fresh INVALID - the lookup bit at the leaf's own bsl is 0 for every square size, mutation-check-proven) and writeBlockCoeffs takes mi-unit args. The grid conformance gate = the fs5g32 fixture (the 2x2 of 32x32). CHROMA (CS-series, LANDED - the layer map used to describe l6 as luma-only, which was stale): the 4:2:0 chroma-emitting walk is encodeFrameChromaQ (pipeline.cpp:1785), taking srcY/srcU/srcV and reconY/reconU/reconV, with uvB = lumaB/2 (pipeline.cpp:1794) and UV block sizes {4,8,16,32} selected from lumaB {8,16,32,64} per av1_get_max_uv_txsize (common_utils.h:142-149). The chroma-ownership predicate is the AV1 is_chroma_reference rule for 4:2:0 (pipeline.cpp:1859-1861). The walk is a real superblock tree in the DECODER's Z-order DFS (zorderLeaves, pipeline.cpp:1772-1784, cited to aom decodeframe.c:1392-1397) and it is the only emitter that writes a partition value other than PARTITION_NONE: PARTITION_SPLIT at the 64x64 root and at the 32x32/16x16 interior nodes (pipeline.cpp:1839, :1965, :1969). Per leaf the order is [luma kf mode][uv_mode - ONE per block, the V TU shares the U decision][uv angle-delta where gated][luma chain][U chain][V chain], U BEFORE V, with three separate neighbour arrays naY/naU/naV. Chroma mode decision is the project's own SAD policy (decideBlockModeUv pipeline.cpp:1742, decideBlockModeUv16x16 pipeline.cpp:1720) over 13 folded candidates via intra::uv2y with UV_CFL_PRED excluded (the loop bounds, pipeline.cpp:1726 and :1748) - the CFL alpha combine is out of scope by named policy (pipeline.h:169-171). The chroma transform type is DERIVED, never signalled: transforms::intraUvTxType (transform.h:97, RT9 step 2) 1:1 with svt_aom_get_intra_uv_tx_type (mode_decision.c:2985-2997), called at pipeline.cpp:2155. The bottom-left availability rule is a single named function, `pipeline::bottomLeftPx` - **DECLARED at pipeline.h:246, DEFINED at pipeline.cpp:2469-2473** (the definition slides: 2420 -> 2459 at CS7 -> 2469 at RT21). Read the declaration and the definition as different things: the declaration has not moved since the be67d6f extraction and is the stable thing to cite in prose, while the definition slides with every edit below it in the file. The ABOVE-RIGHT twin (RT21, LANDED 927e697): chroma top-right is Z-order gated via `pipeline::topRightPx` - **DECLARED at pipeline.h:255 beside bottomLeftPx, DEFINED at pipeline.cpp:2480-2484** - the same shape with the diagonal on the other corner (bx + 1 >= grid / by <= 0 guards, owner (by-1)*grid + (bx+1), available exactly when zPos[owner] < zPos[bIdx]); the chroma gather consumes ONE count for BOTH the cAbove gather bound (pipeline.cpp:2113) and the buildIntraPredictorsUv argument (pipeline.cpp:2142), so they cannot disagree; the generator mirror is `cNTr` (composition.c:6595-6596), likewise one fact at the gather bound (:6613) and the builder call (:6733). The chroma D2 DECISION site (pipeline.cpp:2001) keeps the structural gather by named scope, and the luma twin (composition.c:6264) is measured live and PARKED (see the RT21 register entry in docs/todo.md section 9). The two call sites are `pipeline.cpp:1884` (luma) and `pipeline.cpp:2103` (chroma, the site CS7 changed - it was a literal `0` before; the line moved +10 under the RT21 topRightPx insertion), and the luma gather that consumes the count is `pipeline.cpp:1892-1894` (RT10b). Do not cite 1884 as the gate - 1885-1886 are the above/left buffer declarations. Do not cite 2469 as a call site; it is the rule. The UV side is three different things, not two: **the site CS7 changed is pipeline.cpp:2103**, now a `bottomLeftPx` call like the luma one. **Do NOT "fix" pipeline.cpp:2231.** That is `encodeFrameReconChroma16x16`, one of four standalone 16x16-UV compositions whose grid is `src.width()/16 x src.height()/16`, so on the committed 16x16 UV fixture the grid is 1, no below-left neighbour exists, and the `nBottomLeftPx = 0` there is CORRECT BY GEOMETRY rather than by omission. It is on record as a site where zero is right, so that nobody reads it as CS4, and so that nobody believes CS4 is already handled. Its siblings are pipeline.cpp:2281 (`AutoChroma16x16`), :2356 (`ReconChroma16x16Q`) and :2413 (`AutoChroma16x16Q`), same fixed 16x16 shape. The one condition that would make :2231 a real hole is a caller passing a UV plane larger than one block; no committed caller does, and the fix should either leave it alone or add the rule behind the same guard rather than special-casing the literal. Four standalone UV-plane compositions also exist at 16x16 UV (encodeFrameReconChroma16x16 pipeline.cpp:2217, AutoChroma16x16 :2267, ReconChroma16x16Q :2337, AutoChroma16x16Q :2395). Scope: single-TU + the grid fixtures gate-pinned; the FI DC-deciding fixture still open; FS5d the named dual-domain follow-up (a 1024-position GPU kernel + bench work). Per-geometry decode conformance: the five committed artifacts (d4 30B the 8x8-frame four-4x4-TU structure - the decoders align frame dims to 8 px so the 8x8 node reads the partition symbol; d8 30B; d16 44B; d32 47B committed; d64 441B) decode content-1:1 per geometry via tools/verify_decode4.ps1 -Geometry.
- l7_entropy - the od_ec writer/reader families + the full symbol surface: initDefaultEcFrameContext(fc, base_q_idx) (TD5b: the coefficient tables QINDEX-BUCKET-SELECTED per the spec's init_coeff_cdfs, 07.bitstream.semantics.md:1800-1820, via the verbatim getQCtx mirror of cabac_context_model.c:1907-1918; the 13 token-table families carried as the 4-bucket *_buckets.inc files mechanically split from the committed svt_gen.c extracts - no hand-transcribed values; previously the idx-0 bucket was used for every frame = the TS1 deviation, RESOLVED 2026-09-21: it was the q100 tile-divergence root cause), writeTxbCoeffs/writeBlockCoeffs/readBlockCoeffs (the token chain: txb_skip, tx-type, eob_pt/eob_extra, base_eob+br, reverse base+brs, dc_sign + raw signs + golomb; FS3-prep: the in-chain tx-type symbol requires tx_size_sqr_up < TX_32X32 - DCTONLY at 32x32/64x64 intra writes/reads NO symbol, entropy_coding.c:321-322), writeKfLumaMode/readKfLumaMode, writeSkip, writePartition/readPartition, writeTxType, updatePartitionContext (FS5a: wired into the 8x8Q/32x32Q/64x64Q emission loops), getKfYModeCtx (entropy.cpp:840 - the context is DERIVED from live neighbour modes, not pinned), updateCdf (the spec adaptation rate), writeGolomb/readGolomb. CONTEXT DERIVATION IS LIVE, not stubbed: getTxbCtx (entropy.cpp:1455) drives the per-coefficient skip context from the real neighbour arrays via the skip_contexts table, the nz-map offset LUT (nz_map_ctx_offset.inc) and the chroma ctx_base + 7 branch (entropy.h:605-612); the per-block call sites are entropy.cpp:1533 and :1562. Named scope: the skip symbol is always emitted as skip=0 ctx 0 (no skip decision is taken - pipeline.cpp:499, :685, :1356, :1694, :1982), the angle delta is always 0 in our emission (pipeline.h:156, named parked), and the tx sets fold reduced_tx_set=1 (entropy.cpp:1585). Provenance: every primitive cites the vendored extract; the per-bucket rows cite cabac_context_model.c (the court-cited [2]-bucket EobPt256 row {3089, 3920, ...} = token_cdfs.h:861 on the aom side). CHROMA (CS1b, LANDED): the uv_mode write/read pair (entropy_coding.c:1080-1081 write, decodemv.c:144-147/:823-837 read; alphabet UV_INTRA_MODES - !cflAllowed = 14 at bsize <= 32x32 / 13 at 64x64), the uv angle-delta pair (:1087-1092 / :830-833, gated on bsize >= BLOCK_8X8 AND the folded mode directional), a ComponentType enum threading six component-dim CDF families through both twins, and writeFilterIntra for the chroma walk. Named deferral: write_cfl_alphas (:1060-1071) - UV_CFL_PRED is not a D2 candidate (pipeline.cpp:1726), so the alphas are dead in our emission.
- l8_bitstream - the bit assembly: wb family (bit writer), uleb128, OBU header + temporal delimiter, SPS encode (writeSequenceHeaderObu, sps_obu; FS5b: maxDim-parameterized max frame dims - frame_width_bits = msb(maxDim), the default 32 = the committed walk), the frame-header v1 (writeFrameHeader, lossless q0, 22-bit walk + 2 pad = 3 bytes) and v2 (writeFrameHeaderV2, lossy q100, 40-bit walk - verified field-by-field against both aom decodeframe.c and dav1d obu.c), assembleStructuralKeyframeTUv2 (TD + SPS + OBU_FRAME; FS5b: the dims threaded through bsf3EncodeSps), the committed artifact SET goldens/structural_keyframe*.obu - the 47-byte d32 (TD5b: the v2 header + the idx-2-coded tile, the four-16x16-leaves walk; decodes exit 0 with the content 1:1 - the ecfrm_recon fingerprint 05 08 0c 11, byte-diffs 0/1024) plus the FS5 per-geometry set (d4 30B = the 8x8-frame four-4x4-TU structure - the decoders align frame dims to 8 px; d8 30B; d16 44B; d64 441B - each three-way-identity-tested and content-1:1 measured per geometry; tools/verify_decode4.ps1 -Geometry = the standing instrument). Gate: tu_bytes_v2 47 + tu_bytes_v2_d4/d8/d16/d64 (tools/golden_gen). COLOR HEADERS (CS3, LANDED - the layer map used to stop at v2): writeSequenceHeaderObuV3 (bitstream.cpp:390, the D1 un-patch: mono bit 0, the 4:2:0 colour config, chroma_sample_position = UNKNOWN, separate_uv_delta_q = 0) + writeFrameHeaderV3 (bitstream.cpp:413, v2's 40 bits + the unconditional U/V delta_q bits = 42 bits + 6 pad = 6 bytes) + assembleStructuralKeyframeTUv3 (bitstream.cpp:476). The D1 mono producers stay pinned for the mono artifact set. The committed artifact set is NINE .obu files: the five grayscale above plus color4 183B / color8 137B / color16 133B / color32 165B (color4 and color8 shrank in the chroma regens: 201 -> 198 B at CS7 then 198 -> 183 B at RT21 for color4, 139 -> 137 B at CS7 for color8; the earlier sizes are correct statements about their own commits); the per-artifact decode outcomes are in docs/decode_conformance.md.
- third_party/ - vendored SVT-AV1 (1:1 source of truth), doctest, hardware docs (perf-axis only: PTX ISA, GP104 whitepaper, Pascal Tuning Guide, Nsight-focused guides; CUDA 12.2 profiling is ncu, not nvprof). NOTE for anyone auditing third_party: it is NOT just SVT-AV1 + doctest. third_party/aom is a FULL libaom checkout (av1/decoder/decodeframe.c, av1/decoder/decodemv.c, av1/common/token_cdfs.h, av1/encoder/av1_quantize.c are all present and are the resolution target for the aom-side citations in docs/provenance.md and docs/color.md), third_party/aom_dsp carries src/entdec.c + src/bitreader.c, and third_party/dav1d is **present in the working directory but NOT tracked by git** (see the evidence contract below - "vendored" is ambiguous across exactly these two trees, and only one of the two senses is true of each).

## The evidence contract: the working directory, not git

**A measurement is durable if the WORKING DIRECTORY holds its subject, and
only then.** Git tracks what it tracks; the working directory is what makes a
claim re-checkable by the next session, and the two are not the same surface.
This was unstated, which is why the next session read a dead instrument as a
live convention. State the sense, every time.

**"Vendored" has two senses and they are not interchangeable.** Measured
2026-09-29, and note the two trees DIFFER in kind, not merely in degree:

| tree | sense A: in the working directory | sense B: tracked by git |
|---|---|---|
| `third_party/dav1d/` | PRESENT, 359 files, **no `.git` of its own**, `dav1d_api_version_major` 7 / meson `version: '1.5.4'` | **NOT vendored** - listed in `.gitignore` (line 9), `git ls-files` returns 0 |
| `third_party/aom/` | PRESENT, 1522 files excluding its own `.git` (1550 including), **has its own `.git`**, HEAD `208e9c49db` | **NOT vendored** - listed in `.gitignore` (line 8), `git ls-files` returns 0 |

So: **neither is vendored in the git sense.** Both are working-directory
checkouts, identical in KIND to the retired `%TEMP%` throwaway - differing
only in being pristine and correctly placed. `dav1d` and `aom` survive a
reboot; **neither survives a fresh clone.** A citation into either is
checkable *while this working directory stands* and is not reproducible from
the repository alone. Say which sense you mean: "vendored" unqualified is
wrong for both, and "dav1d is not vendored, therefore do not use it" is wrong
in the working-directory sense, which is the sense the standing instruction
uses. **Use both trees.** dav1d is the resolution target for the dav1d-side
citations; aom is the target for the aom-side ones.

**The contract, in the three cases that actually arise:**

0. **A BRIEF IS NOT A SOURCE.** A figure handed to you in a session is not
   citable until it is re-derived, and a brief that pairs two numbers invites
   a comparison between two different SUBJECTS. The general form: **if a
   figure cannot be re-derived, that is a finding about the BRIEF, not a
   defect in the number** - so measure the subject, and report which subject
   you measured. Both parties got this wrong in one exchange, four commits
   after this contract was written: a brief paired a `%TEMP%` throwaway's
   luma ("Y max 255, mean 233.5") against ffmpeg's ("94/24.0"), where the
   second half is real and is **color4** (measured 2026-09-29: Ymin 0, Ymax
   94, Ymean 23.9629, reproducing exactly) and the first is not citable at
   all; and the agent then compared the throwaway's **color16** against that
   color4 half, so the two numbers were about different artifacts. A brief is
   the one input with no provenance of its own, so it is exactly where a
   recollection enters the tree wearing the appearance of a measurement.
   Re-measuring is not scepticism about the brief; it is the only way to find
   out which artifact a number is about.
1. **A measurement living only in `%TEMP%` is a RECOLLECTION, not a citation.**
   It is not citable, not verifiable, and it does not become citable by being
   described accurately in a commit message. Either move the subject into the
   working directory or record the result as a claim in a doc, with the
   command that produced it.
2. **A measurement derived from `third_party/` IS citable while the working
   directory stands** - and its subject matter is **the decoder, never a build
   of it.** The reference is the source. An instrumented copy is a different
   thing and, as the retired throwaway demonstrated, a wrong arbiter.
3. **A measurement that touches neither** - a gate diff, an `.obu` decode, a
   ctest run - is citable because the committed gate, the committed
   artifacts, and the test suite carry it. That is the durable core, and it
   is why the working-directory rule is about the reference trees, not about
   every number in this repo.

**The throwaway dav1d is RETIRED (2026-09-29), not parked.** Superseding the
two-exit entry in `docs/todo.md` at `33e9199` so the next session does not
read a live convention for a dead instrument. The brief's proposed grounds
are partly right and I re-measured rather than restated them:

- **It cannot arbitrate, and the reason is sharper than "wrong numbers": it
  is an INSTRUMENTED decoder, not upstream.** Measured - `dav1d_td4/src/
  recon_tmpl.c` carries **66** RT-marked lines against its own untouched
  baseline copy's **22**. A patched arbiter is a different decoder wearing
  upstream's name, and it is exactly the "instrumented copy" this contract
  forbids.
- **It diverges on luma from a genuinely independent implementation.** Both
  decode the same artifact to YUV; luma only, first 4096 B, both 4:2:0 so
  the Y plane is the first 4096 bytes in each: on
  `structural_keyframe_color4.obu` the throwaway gives **max 160, mean
  118.1**, ffmpeg's `libaom-av1` gives **max 94, mean 24.0**. A 4.9x mean
  gap on the luma plane of a committed artifact. (`color16` gives 158/83.8
  against 93/24.1, the same direction and a 3.5x gap - measured again
  2026-09-29 and consistent with the first run.) The comparison is
  apples-to-apples by construction, not by luck: both files are 64x64 4:2:0,
  so Y occupies bytes 0-4095 in each and the luma plane is the same 4096
  bytes in both.
- **It is not in the working directory and cannot be**, being `%TEMP%`, which
  does not survive a reboot - the case-1 failure above, live.
- **It is not needed.** ffmpeg exposes `libaom-av1` (and `libdav1d`, and
  `av1`) as independent decoders, so the acceptance check has a real arbiter
  that is not this project's own code. `tools/verify_decode4.ps1` is the
  standing instrument; the throwaway was a debugging aid, and a debugging aid
  that is patched, mis-arbitrating, and non-durable has no residual value.
  **A wrong arbiter that looks like evidence is the specific harm** - it is
  worse than no arbiter, because it produces confident numbers.
- l9_analysis - content analysis (AN-series). AN1 LANDED 2026-11-01:
  analysis::mean8x8 in src/l9_analysis (host-only, CMake mirrors l5_motion,
  ctest target l9_analysis), the 8x8 port of svt_compute_mean_c
  (pic_analysis_process.c:194-216, declaration twin compute_mean.h:21 which
  is A12-classified - both anchors cited, the cross-area question named in
  coverage_areas.txt), VARIANCE_PRECISION = 16 at pic_analysis_process.c:36.
  The campaign's 22-slice decomposition (AN0 scout, phases A-D) lives in
  docs/todo.md section A0; the target area is A11 in the coverage metric
  below. AN2 LANDED 2026-11-01: analysis::meanSquare8x8, the 8x8 port of
  svt_compute_mean_squared_values_c (pic_analysis_process.c:220-240,
  declaration twin compute_mean.h:26), same layer, same test file
  (src/l9_analysis/tests/test_variance.cpp); the SSE2 twin cross-check and
  the consumer-scale cross-check are recorded in the AN2 register entry.
  AN3 LANDED 2026-11-01: the sub-mean pair - analysis::subMean8x8
  (svt_compute_sub_mean_8x8_c, pic_analysis_process.c:242-261,
  compute_mean.h:31, uint16 stride) and analysis::subMeanSquareValues
  (svt_aom_compute_sub_mean_squared_values_c, :263-284, compute_mean.h:34,
  generic w/h) - rows {0,2,4,...} via the skip = vi + vi loop, then
  << 3 / << 11 with NO division; the << 11 is the HALF-area normalization
  confirmed by the SSE2 twins (sub << 11 vs full << 10) and the consumer
   variance scale, and it is HARDWIRED for every (w,h) (the code, not the
   comment); register entry in docs/todo.md section A0.
   AN4 LANDED 2026-11-01: analysis::intermVarFour8x8, the port of
   svt_compute_interm_var_four8x8_c (pic_analysis_process.c:286-313,
   compute_mean.h:40) - the COMPOSITION that CALLS the AN3 pair, never
   recomputes: four subMean8x8 + subMeanSquareValues(stride, 8, 8) pairs at
   offsets 0, 8, 16, 24 accumulated +8, i.e. FOUR SIDE-BY-SIDE 8x8 BLOCKS
   along one row origin, all reading rows {0,2,4,6} (the reference's own
   "(0,1)..(0,4)" comments are meaningless labels and the brief's
   "16x16 raster quadrants" reading is NOT the mechanics; the live caller
   pic_analysis_process.c:350-359 passes the frame-strided buffer and
   advances blk_offset by 32 per call); register entry in docs/todo.md
   section A0. AN5 LANDED 2026-11-01: analysis::calculateHistogram
   (calculate_histogram, pic_analysis_process.c:170-188; NO header
   declaration twin - grep-verified file-local to its .c, and its real
   name is calculate_histogram, not the brief's
   svt_aom_calculate_histogram), the 256-bin PIXEL-VALUE histogram: the
   outer loop steps its counter by decim_step but selects the row by
   POINTER advance (stride * decim_step), the inner steps the column;
    sum accumulates the SAMPLED pixels (the caller rescales by
    decim_step^2 at :569 and pre-initializes bins to 1 at :545-547);
    host only, no GPU twin (parked per the brief). AN6 LANDED
    2026-11-01: analysis::downsample2d, the port of
    svt_aom_downsample_2d_c (pic_analysis_process.c:134-164, declaration
    twin Codec/motion_estimation.h:33-35 - charged to A3, the body
    counted under A11), the 2x2 0-phase decimation GENERIC over
    decim_step: the input pointer is pre-advanced by half_decim_step
    rows (:148) and prev_input_line is one stride above it (:151), so
    the summed 2x2 window sits at rows/cols {half-1, half} WITHIN each
    decim_step cell (the cell's top-left 2x2 at step 2, its MIDDLE 2x2
    at step 4 - the live callers pass 2 at :1504-1510 and 4 at
    :1530-1536); each output is (sum + 2) >> 2 (:157); host only, no
    GPU twin (parked per the brief). AN7 LANDED 2026-11-01:
    analysis::b64Variance, the port of compute_b64_variance
    (pic_analysis_process.c:318-442, static, single caller :610, NO
    header declaration twin - grep-verified file-local to its .c), the
    RDO-input chain's capstone composing the landed AN primitives:
    sixteen intermVarFour8x8 calls in the live BLOCK_MEAN_PREC_SUB
    branch (:347-361, blk_8x8_idx = 8*row + 4*colGroup, blk_offset +32
    per call) or 64 mean8x8 + meanSquare8x8 pairs in the FULL branch
    (:335-346, ported because the reference carries it; the live
    reference always selects Sub, enc_handle.c:4339), then the
    16x16/32x32/64x64 folds each >>2 over {+0,+1,+8,+9} at 16R + 2C
    and {+0,+1,+4,+5} at 8P + 2Q - every fold independent PER ARRAY
    (means from means, squared-means from squared-means), no level
    recomputes from pixels; variance = (sq - mean*mean) >> 16 with the
    sub-block loops gated on allintra || aq_mode == 1 ||
    variance_octile (:412) and the 64x64 entry unconditional
    (:438-441); the sbVar layout is the EbMeTierZeroPu bases
    (me_context.h:47/:48/:52/:68: 0/1/5/21), only 0..84 ever written;
    named deviations: scs/pcs/b64_idx/origin dropped (y at the block
    origin, sbVar ONE flat row), the stack arrays exposed as
    out-params, the :412 gate and the prec selector threaded as
    parameters; host only, no GPU twin (parked, named).
  AN8 LANDED 2026-11-01: the screen-content detector pair - TEN new
  primitives: countColorsWithThreshold (svt_av1_count_colors_with_
  threshold, pic_analysis_process.c:911-930), isValidPaletteNbColors
  (is_valid_palette_nb_colors, :956-978, static, the nb_colors <= 1
  palette gate its caller relies on), findDominantValue
  (svt_av1_find_dominant_value, :984-1003, strictly-greater update =
  first max wins ties), dilateBlock (svt_av1_dilate_block,
  :1024-1076), variance16x16/variance8x8 (the VAR(W,H) macro
  instances, C_DEFAULT/variance.c:141-190 - A14's first new lines
  since the row was written; the detectors call vf(src, stride,
  svt_aom_eb_av1_var_offs, 0, &sse) with the offs constant 128 at
  ref_stride 0 :936-942, so the 128 cancels: the return is the
  population-variance SUM, :189's (int64_t) cast + truncating division
  kept) and getSbyPerpixelVariance16x16/8x8 (svt_av1_get_sby_perpixel_
  variance, :944-952: ROUND_POWER_OF_TWO(var, eb_num_pels_log2_
  lookup[bs]), definitions.h:457 + common_utils.c:39 -> 8/6; the
  fn_ptr dispatch replaced by per-size wrappers), ScAaCounts
  (Sc_AA_Counts :1078-1086), scAaCollectCounts
  (svt_aom_sc_AA_collect_counts, :1088-1190: multiplier 2 + the
  checkerboard initial_col + 2*blk_w step under fast_detection
  :1096-1104, region_id = ((r>=h2)?2:0)+((c>=w2)?1:0) :1106-1108, the
  complex branch dilates and re-counts, the x2 fast counts
  :1177-1187), isScreenContent (svt_aom_is_screen_content, :1354-1429:
  16x16 pass thresh 4/var 0 -> sc_class0..3 :1386-1398, 8x8 pass
  var 16 -> sc_class4 :1427-1428) and isScreenContentAaAware
  (svt_aom_is_screen_content_antialiasing_aware, :1207-1350: 16x16
  collect final 6/var 5, 8x8 collect final 8/var 50, six class
  formulas :1297-1321). pcs->enhanced_pic + the scs fast flag threaded
  as parameters (b64Variance deviation pattern); sc_class5 is NEVER
  written by is_screen_content (grep-verified) - zero-init false named.
  BRIEF CORRECTION (measured against the reference's actual inputs):
  the detectors consume NONE of the AN1-AN7 primitives - AN5's
  histogram (caller :560) and AN7's b64Variance (caller :610) are
  disjoint from :911-1429; the spans call their own helpers. Caller
  context: pd_process.c:4795-4822 (mode 2 gates is_screen_content to
  <= 1080p; mode 3 = AA-aware; is_input_luma_dominant at :4813 reads
  pcs->chroma_downsampled_pic, NOT enhanced_pic). Host only, no GPU
  twin (parked, named).
  AN9 LANDED 2026-11-01: analysis::isInputLumaDominant, the port of
  svt_aom_is_input_luma_dominant (pic_analysis_process.c:1440-1493)
  with its eight FRAME_LUMA_DOMINANT_* defines (:1431-1438: SAMPLE_STEP
  8, CORE_THR 16, TAIL_THR 18, MIN_CORE_PCT 85, MIN_TAIL_PCT 95,
  NEUTRAL_THR 6, UV_DIFF_THR 4, MIN_NEUTRAL_PCT 75). SELF-CONTAINED
  (no AN1-AN8 helper calls): samples every 8th CHROMA pixel over
  (width>>1) x (height>>1), counts chroma_mag_sq = du*du + dv*dv
  against core <= 256 / tail <= 324 and the neutral box
  (|du| <= 6 && |dv| <= 6 && |uv| <= 4), returns sample_cnt &&
  core*100 >= sample*85 && (tail*100 >= sample*95 ||
  neutral*100 >= sample*75) (:1490-1492) - three nested classes
  (neutral -> core -> tail). Caller: pd_process.c:4813 passes
  pcs->chroma_downsampled_pic gated on scs->detect_luma_dominant_input
  (:4812); chroma_downsampled_pic is a 4:2:0 buffer desc carrying the
  LUMA dims (pcs.c:1192-1199) - why the >>1. Named deviations: the
  null/EB_YUV400 guard (:1441-1443) threaded as the monochrome flag;
  the uv_w/uv_h == 0 guard (:1448-1450) kept but behavior-redundant
  with the sample_cnt conjunct (dropping it is not observable). Host
  only, no GPU twin (parked, named).
  AN12 LANDED 2026-11-02: the 1D FFT kernels - BOTH families, the first
  FLOAT slice in the layer (every preceding primitive is fixed-point).
  analysis::fft1d2/4/8/16/32 = GEN_FFT_2..GEN_FFT_32
  (fft_common.h:113-414, 298 non-blank) and analysis::ifft1d2/4/8/16/32
  = GEN_IFFT_2..GEN_IFFT_32 (:416-823, 406 non-blank), transcribed
  verbatim as scalar float (T_VEC = float, load/store = point
  read/write, add/sub/mul = the float operators - the macros' own
  scalar instantiation, fft.c:82-86 fwd / :173-177 inv). N-set
  {2,4,8,16,32} for both families; no other N exists in the reference.
  The 2D drivers fft_2d_gen/ifft_2d_gen (fft.c:51/:108) are AN13, NOT
  here. TOLERANCE (the first float decision, test-header documented,
  inherited by AN13-AN15): EPSILON = 1e-4 ABSOLUTE for per-transform
  assertions - the kernels are pure float graphs with NO libm in
  :113-823, but the twiddles are 6-decimal truncations (<= 4.7e-7 each;
  measured worst port-vs-definition delta 1.01e-5 at N=16 r_1); ONE
  derived exception: the fft->ifft round-trip fixture uses n * 1e-4
  (the inverse re-sums the forward's truncation noise across n terms;
  measured deltas 0 / 0 / 9.5e-6 / 1.5e-4 / 2.4e-4 at N = 2/4/8/16/32).
  Fixture discipline: impulse (all-ones -> DC only), sinusoid at bin 1
  (energy in exactly one packed slot; N=16 r_1 = 8, N=32 r_1 = 16),
  generic non-symmetric instrument-derived vectors, ifft DC-impulse +
  hand-chosen non-symmetric spectra + the round-trip composition, plus
  stride fixtures. Six mutations captured against the FINAL fixture set
  (twiddle corrupt x2, re/im swap x1, dropped mul x1, ifft asymmetry x1,
  w16 half swap x1), all killed by named fixtures. The register's
  corrected scout errors: the anchor is fft_common.h:113-414/:416-823,
  NOT :114-330, and the "cos/sin tables" are inline literals, no tables
  exist. Host only, no GPU twin (parked, named - scout marked [G]).
- l10_filters - the 8-bit deblocking kernel family (A8 campaign, DF1+DF2).
  Ported 1:1 from the pinned deblocking_common.c: the mask/flat/hev gates
  (filterMask2/filterMask/filterMask3Chroma/flatMask3Chroma/flatMask4/
  hevMask :152-:223), the filter4/6/8 cores (:225-:317), the
  lpfHorizontal/Vertical 4/6/8 entry points (:253-:386, blimit/limit/
  thresh flattened from the SIMD_WIDTH element-[0] reads to scalars) and
  updateSharpness (:568-:589, hev_thr excluded - set by the
  loop_filter_frame_init wall, not in DF1); highbd_* and
  signed_char_clamp_high are PARKED. Host only, no GPU twin. COVERAGE-ONLY
  slice: moves no conformance count, touches no bitstream (the filter
  enable bits stay literal 0). DF2 LANDED: the 14-tap family (95 nb,
  measured 23+35+4+30+3) - filter14 (:786-:809, the 13-tap
  [1,1,1,1,1,2,2,2,1,1,1,1,1] writing op5..oq5 with RPOT(...,4), op6/oq6
  untouched), mbLpfHorizontalEdgeW (:811-:847, step=4, taps s[-7p..+6p],
  flat2 = flatMask4(1, p6, p5, p4, p0, q0, q4, q5, q6) - the OUTER flatness
  vs p0/q0) + lpfHorizontal14 (:849-:852, count=1 with step=4 = FOUR
  columns per call, pinned from the code), mbLpfVerticalEdgeW (:854-:885)
  + lpfVertical14 (:887-:889, count=4 = four rows); the else chain STACKS
  filter14 -> filter8 -> filter4 composed from the DF1 primitives (the
  mb_* edge functions are file-local static in the port as in the
  reference). Mutations M1 (swapped op5 tap order), M2 (flat2 span
  p5-for-p6), M3 (else chain reordered to filter4), M4 (RPOT shift 3) each
  killed by a named fixture; the h/v count pins are mutation-verified too.
  CDEF1 LANDED (2026-11-04): a SECOND file pair, cdef.{h,cpp} - the CDEF
  find_dir + filter kernel family of cdef.c, 367 nb: sign :16-:18,
  constrain :20-:27 (the damping/get_msb guard; cdefSign/cdefConstrain
  exported for testability, DF1 signedCharClamp precedent), the padded
  directions table :35-:56 + :58 (kCdefDirectionsPadded, the +2 offset
  folded into cdefDirections/cdefDirectionsRc accessors), adjust_strength
  :66-:70, cdefFindDir :88-:150 (THE SPEC kernel: div_table
  {0,840,420,280,210,168,140,120,105} - div_table[8] = 105 = 840/8, the
  8-pixel-bucket weight; cost MAXimizes because cost[d] is the weighted
  variance around per-line means along direction d), cdefFindDirDual
  :152-:156 + cdefFindDirList :158-:187 (list walker, CdefList +
  kCdefNblocks = 16), pri/sec taps :189-:190, cdefFilterBlock :193-:254
  (the 16-bit SPEC filter: kCdefVeryLarge 0x7F7F sentinel, max guarded,
  min unguarded-but-sentinel-never-wins, int16 sum, round-half-away
  (8+sum-(sum<0))>>4), cdefFilterBlock8bit :257-:300 (interior twin, no
  sentinel, sub = BLOCK_4X4 ? 1 : sub), cdefFindDir8bit :303-:311 +
  cdefFindDirDual8bit :313-:317 + cdefFindDir8bitList :509-:535 (widen,
  delegate), cdefTapOffFrame :427-:430 + cdefFilterBlock8bitBounded
  :437-:504 (the frame-edge kernel: off-frame taps excluded from
  sum/max/min - the geometric equivalent of the sentinel; single damping).
  Local CdefBlockSize enum (only equality used; l0_core::BlockSize lacks
  4X8/8X4 - named deviation). The 16-bit kernel is the SPEC; the 8-bit
  twins are their own 1:1 bodies and the derivation relationship is pinned
  BY TESTS (8-bit == 16-bit widened; bounded == 16-bit-with-sentinels at
  exactly the off-frame taps, both corners). Integer-only (no float in the
  family). LANDED AFTER CDEF1 (2026-11-04): CDEF2, the fb/lbd driver
  chain + the copy helpers (svt_cdef_filter_fb :322-:402,
  cdef_filter_fb_lbd_impl :542-:638, the luma/chroma wrappers
  :640-:704, the svt_cdef_filter_fb_lbd dispatch :706-:778, and
  cdef_copy.h svt_cdef_copy_rect8 :28-:74 + svt_aom_copy_rect :79-:85
  + svt_aom_copy_sb8_16 :96-:105 - 377 nb; the strength derivation
  factored out as cdefPriStrength/cdefSecStrength, cdefSign
  precedent), and CDEF3, the enc_cdef mse/search block (the four
  mse_8xn/4xn 16bit/8bit kernels, svt_aom_compute_cdef_dist_16bit/
  _8bit, cdef_apply_fb :200-:298, svt_search_one_dual_c :714-:772 +
  joint_strength_search_dual :787-:818, and cdef_copy.h
  svt_aom_fill_rect :88-:94 - 318 nb; the struct-walled driver chain
  svt_sb_compute_cdef_list / svt_av1_cdef_frame / svt_pick_cdef_from_
  qp / propagate_cdef_strength / finish_cdef_search stays parked, and
  corner_match.c/corner_detect.c stay orphaned). RESTORATION (LR1 +
  LR2, src/l10_filters/restoration.{h,cpp}, the A8 restoration.c
  spans): LR1 the math core, 243 nb - sgr_params :31-:49,
  extend_frame_lowbd :110-:126 + the svt_extend_frame entry :150-:157,
  copy_tile_lowbd :159-:164 + the dispatcher :173-:180, the
  boxsum1/boxsum2/boxsum family :430-:594 (edge handling is
  TRUNCATION - a 2D box sum clamped at the array bounds, pinned by an
  independent clamped-box oracle), svt_decode_xq :597-:608 (the
  params-aware form, NOT the brief's spec-style constants) and the
  x_by_xplus1/one_by_x tables (0-mismatch formula oracles over ALL
  entries); LR2 the selfguided restoration family, 306 nb - the fast
  internal :632-:764 (estimation rows stepped by 2, *6/*5 kernels),
  the slow internal :766-:884 (stride 1, the 3x3 *4/*3 kernel),
  svt_av1_selfguided_restoration_c :886-:922 and
  svt_apply_selfguided_restoration_c :924-:962 (6 named-parked: the
  highbd staging loop and the CONVERT_TO_SHORTPTR write). highbd
  branches assert-parked (8-bit discipline); enable_restoration stays
  0, so CDEF2/CDEF3/LR1/LR2 are all COVERAGE-ONLY. Parked still: the
  stripe machinery (setup/restore_processing_stripe_boundary,
  get_stripe_boundary_info), wiener_filter_stripe, sgrproj/wiener
  stripe + highbd, the :1040+ filter_unit/frame walkers and the tile
  walkers/boundary-line savers. Mutations: the whole-pair dir+2/dir-2 swap
  SURVIVES BY CONSTRUCTION (read multiset invariant) - sharpened to the
  minimal observable s0-only swap, killed by the asymmetric sec fixture;
  div_table[1]<->[2] killed by a weighting fixture derived with an
  independent PS replica (cost[6] 36,126,720 > cost[0] 36,114,432);
  damping-shift, sentinel-guard-inversion and edge-flag mutations each
  killed by named fixtures.


## Completion metric (the portable-C coverage number)

North star #1 above is "bit-exact 1:1 with the pinned reference". That is a
QUALITY bar, not a PROGRESS bar - it says whether what we ported is correct,
and says nothing about how much is ported. This section defines the progress
bar, and it is the number to quote when asked "how far along is the port?".

**THE NUMBER, as measured 2026-11-04 (the GR1/AN21/CV1 wave): 17.5%**
(27,575 of 157,718 non-blank lines - the covered count moved +1,617 from
25,958 at LR2: GR1 +998 in A8 (the film grain synthesis core of
grainSynthesis.c, 998 of 1,140 nb live), AN21 +455 (328 resize.c
algorithm + 99 resize.c tables in A11, + 28 super_res.c table in A8) and
CV1 +163 in A13 (the convolve.c kernel family in the new src/l12_convolve
module), so A8 prints 2,942 covered of 10,600 (28%), A11 3,600 of 12,028
(30%), A13 477 of 5,357 (9%). All of it is COVERAGE-ONLY: it moves NO
conformance count
- see the paired-metric caveat. Earlier
history of the same
metric: the DF2 run measured 22,840 = +95 from 22,745 (the 14-tap family,
23+35+4+30+3); the DF1 run measured 22,745 = +215 from 22,530 (the 8-bit
deblocking kernel family: the mask/flat/hev gates, filter4/6/8, the lpf
4/6/8 entry points, updateSharpness + the scc helper); the AN12 run
measured 22,530 = +704 from 21,826 (the AN12 FFT port,
+704 in A11 (the fft_common.h GEN_FFT_2..GEN_FFT_32 macro bodies :113-414,
298 non-blank, and the GEN_IFFT_2..GEN_IFFT_32 macro bodies :416-823,
406 non-blank; the 10 fft.c entry-point instantiation lines are NOT
counted - our analysis:: names have no instantiation shell); the
printed percentage MOVED at AN12: 22,530/157,718 = 14.2841% crosses
the 14.25 rounding boundary, so AN12 prints 14.3% where AN10/AN11 printed
13.8%; the AN12 percentage did NOT move again at DF1's +215: 22,745/157,718
= 14.4204% stays below the 14.45 boundary; DF2's +95 moved it:
22,840/157,718 = 14.4807% crosses the 14.45 boundary, so DF2 prints 14.5%;
CDEF1's +367 moved it again: 23,207/157,718 = 14.7168% crosses the 14.65
boundary, so CDEF1 prints 14.7%);
the AN11 run measured 21,826 = +48 from 21,778 (the AN11 HME
downsampling driver port); the AN10 run measured 21,778 = +378 from 21,400: the AN10
pad-family port, +142 in A6 (the pic_operators.c pad spans) and +236
in A11 (the pic_analysis_process.c pad spans); the AN9 run measured
21,400/157,718 = 13.5685%; the AN8 run
measured 21,347 = +429 from 20,918: the AN8
screen-content port, +406 in A11 (pic_analysis_process.c :911-1429, the
nine detector/helper spans) and +23 in A14 (variance.c :141-158 +
:183-190, the variance_c + VAR macro);
the AN7 run measured 20,918 = +115 from 20,803: the AN7 b64Variance port
(:318-442); the AN6 run measured 20,803 = +29 from 20,774: the AN6 downsample2d port
(:134-164); the AN5 run measured 20,774 = +115 from 20,659: the AN1
mean8x8 port, the AN2 meanSquare8x8 port, +18 each, the AN3 sub-mean
pair, +36, the AN4 intermVarFour8x8 port, +25, and the AN5
calculateHistogram port, +18, all
in pic_analysis_process.c (:170-188, :194-214, :220-240, :242-261,
:263-284, :286-313); the printed percentage MOVED at AN12: 22,530/157,718 =
14.2841% crosses the 14.25 rounding boundary, so N1 prints 14.3% where
AN10/AN11 printed 13.8%; the printed percentage did NOT move at AN11:
21,826/157,718 = 13.8386% stays below the 13.85 boundary, so N1 prints
13.8% again where AN10 printed 13.8%; the printed percentage MOVED at
AN10: 21,778/157,718 = 13.8082% crosses the 13.75 rounding boundary the
AN9 13.5685% sat below, so N1 prints 13.8% where AN9 printed 13.6%; the
printed percentage MOVED at AN9: 21,400/157,718 = 13.5685% crosses the
13.55 rounding boundary the AN8 13.5349% sat below, so N1 prints 13.6%
where AN8 printed 13.5%; the earlier
move: AN8 crossed the 13.45 boundary that AN7's 13.2629% sat below, and
AN4 crossed the 13.15 boundary that AN1-AN3's 13.1443% sat
below). Regenerate with `powershell -File tools\coverage.ps1`; never quote a
hand-typed percentage, for the same reason a hand-typed line count is not
allowed.

**Denominator: the pinned reference's portable C algorithm surface only** -
`third_party/SVT-AV1/Source/Lib/Codec` (239 files) + `.../C_DEFAULT` (15
files) = 254 files, 157,718 non-blank lines. Deliberately EXCLUDED:

- all 11 `ASM_*` directories (217,092 non-blank lines of x86/ARM SIMD). Our
  CUDA kernels are the analog of those, not a separate algorithm, so counting
  them would measure SVT's optimisation work rather than our coverage. This is
  the single biggest judgement in the metric and it is the reason the number
  is 13% rather than 6%.
- `Source/Lib/Globals`, `Source/API`, `Source/App` - the library shell, the
  public API and the CLI; none is a 1:1 algorithm target.
- the four `*_rtcd.*` files inside Codec (7,906 lines of generated
  function-pointer dispatch tables plus inline intrinsic stubs - dispatch
  glue, not portable algorithm).
- `third_party/` in its entirety.

**Metric: NON-BLANK lines.** `Measure-Object -Line` and
`[IO.File]::ReadAllLines | Where Trim` both report non-blank; the trap is that
`Get-Content | Measure-Object -Line` LOOKS like it counts physical lines and
does not. Verified on the pinned tree: `entropy_coding.c` is 4,898 non-blank
vs 5,551 physical (653 blank). Both sides of the ratio are measured the same
way, so the percentage is sound - but an absolute line count quoted without
naming its metric is not.

**The per-area judgement lives in `tools/coverage_areas.txt`, not in the
script.** `tools/coverage.ps1` MEASURES (line counts, recomputed from the
reference every run); the file JUDGES (what fraction of each area is ported),
with a one-line rationale per row that is the audit surface. A fraction with no
reason is not reviewable. The file lists explicit paths per area, so a file
claimed by two areas is reported as a classification bug and a reference file
claimed by none is reported as UNASSIGNED - counted in the denominator, so a
newly added reference file lowers the percentage loudly rather than silently.

| id | area | files | non-blank | cov% | covered |
|---|---|---|---|---|---|
| A1 | Transform/TX | 9 | 27,008 | 33% | 8,913 |
| A2 | Intra prediction | 4 | 3,478 | 90% | 3,130 |
| A3 | Inter prediction + motion estimation | 44 | 17,738 | 2% | 355 |
| A4 | Mode decision + RDO + pc_tree | 18 | 31,088 | 3% | 933 |
| A5 | Entropy coding (CABAC/OD) | 11 | 8,460 | 55% | 4,653 |
| A6 | Coding loop + PCS + block structures | 22 | 13,928 | 11% | 1,535 |
| A7 | Bitstream + OBU + headers | 5 | 1,436 | 40% | 574 |
| A8 | Post-filters (deblock/CDEF/restoration/grain) | 27 | 10,600 | 28% | 2,942 |
| A9 | Rate control | 17 | 5,951 | 0% | 0 |
| A10 | Picture management + threading | 30 | 9,662 | 0% | 0 |
| A11 | Content analysis + temporal filtering | 16 | 12,028 | 30% | 3,600 |
| A12 | Palette + segmentation + misc | 8 | 1,612 | 0% | 0 |
| A13 | Utility + platform + core headers | 24 | 5,357 | 9% | 477 |
| A14 | C_DEFAULT portable reference kernels | 15 | 1,466 | 32% | 463 |
| - | excluded: `*_rtcd.*` dispatch | 4 | 7,906 | - | 0 |
| | **TOTAL** | **254** | **157,718** | | **27,575** |

**+1.0 point = 1,577 non-blank reference lines.** So progress is legible
arithmetically: a slice worth +1 point is worth about 1,600 lines of ported
reference surface.

**THE NUMBER IS TWO NUMBERS, and the second one is not optional.** A bare LOC
percentage can be moved by transcribing a data table and cannot be moved by
fixing an algorithm. The most extreme case is in this very tree: SVT's
`q_matrices.h` is 13,424 lines of pure quant-matrix data in one file, and
transcribing it would add 8.5 points while the encoder's behaviour is
unchanged, because it sits dormant behind `using_qmatrix = 0`. (It is also
poor value per line - our `.inc` files pack at 8.87 numbers/line against the
reference's 15.97, so it costs roughly 2,835 of our lines per point, versus
about 1,580 for algorithm code.) Therefore, alongside the percentage, report
the **decoder-gated conformance count**: committed artifacts x decoders
verified content-1:1, per `docs/decode_conformance.md`. **A slice counts as
progress only if it moves both.** Neither number alone is honest - one is
gameable by transcription, the other does not scale with coverage.

**Coverage is not quality.** A5 at 55% and A1 at 33% are the two softest
fractions in the table and are the first thing to attack in review. Neither is
a claim that the ported half is bit-exact - that is north star #1 and it is
settled per-slice by the GATE and by the real decoders, not by this table.

**CITATION RULE EXTENSION.** The fractions above are judgement and must be
grep-verified against the tree before being quoted, exactly as existence and
negative-existence claims already are. Re-run `tools/coverage.ps1` after any
edit to `tools/coverage_areas.txt`; the table above and the number in this
paragraph must agree with that run or this section is stale.

