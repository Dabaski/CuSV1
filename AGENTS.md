 AGENTS.md
 
## Prime directive: incremental TDD only (Uncle Bob's Three Laws)
 
You do not write a feature, function, or file in one pass. You follow Robert C.
Martin's Three Laws of TDD as a **nano-cycle** â€” run on almost a second-by-second
basis, not test-file-by-test-file:
 
1. You may not write production code until you have written a failing test.
2. You may not write more of a test than is sufficient to fail â€” and a
   compilation/syntax error counts as a failure.
3. You may not write more production code than is sufficient to pass the one
   currently failing test.
These are training wheels, not superstition â€” the point isn't the literal
order of test-vs-code, it's staying in small, always-verified steps instead of
writing a pile of code and hoping it's right. Treat "one behavior" as far
smaller than a feature: often one assertion, sometimes just enough to fail to
compile. If you can split a step into two smaller ones, split it.
 
**Never write implementation code before there is a failing test that requires it.**
**Never write more test than is needed to fail (including "fails to compile").**
**Never write more implementation than the minimum needed to pass the current test.**
 
## The loop (repeat constantly â€” this is a nano-cycle, not a per-feature cycle)
 
1. **Pick the smallest next slice.** Smaller than you think. One assertion,
   or one line that won't even compile yet. Not "handle the whole function" â€”
   just the next tiny fact about its behavior.
2. **RED â€” write just enough test to fail.**
   - Write only the test. Do not touch implementation files in this step.
   - Stop as soon as it fails or fails to compile â€” do not pre-write further
     assertions "while you're in there."
3. **Run the test suite and confirm it fails.**
   - Actually run it. Do not assume it fails â€” show the failure or compiler output.
   - A compile error is a valid, expected RED state â€” treat it as step 2's
     natural first failure, then move straight to step 4 to make it compile
     and fail correctly, or pass, whichever comes first.
   - If it passes immediately with zero new code, the test is wrong or
     redundant â€” fix the test, don't touch implementation.
4. **GREEN â€” write the minimum code to pass.**
   - Minimum literally means minimum: hardcode a return value if that's all
     the current test demands. Generalization comes later, driven by a test
     that forces it â€” not written speculatively now.
   - No extra abstraction, no handling of cases you haven't written a test for yet.
5. **Run the full test suite and confirm everything passes.**
   - Not just the new test â€” the whole suite. Regressions count as failure.
   - Do not stack new increments on top of a red suite.
6. **REFACTOR (only on green, and only here do you step back from the Three Laws).**
   - Clean up naming, remove duplication, generalize hardcoded values into
     real logic if the accumulated tests now justify it â€” behavior must not change.
   - Rerun the full suite after refactoring. Must stay green before continuing.
7. **Report status, then go back to step 1.**
   - One line: what tiny slice was added, test name, pass/fail state.
   - Do not batch multiple loop iterations into a single summary â€” report each one.
## Hard rules

- **RED evidence is mandatory in every slice report**: the failing doctest
  line (or compile error) is shown before GREEN. Refactor-only slices declare
  themselves and show green-before/green-after instead. A test that passes on
  first build must be proven discriminating (mutation check) or it counts as
  a skipped RED.
- **Commit messages carry measured values only** - register counts, SADs,
  MSEs quoted in a commit message come from an actual run, never from
  memory or estimation.
- **Goldens come from tools/golden_gen** (committed generator), cited with
  generator line + SVT provenance. Hand-traces are commentary, never
  expected values.
- **Deviations and limitations are named in the slice report**, never
  silent.
- **Range conditions are enumerated, never spot-checked** (audit rule, R-series):
  any mode/size/index range in a port (e.g. `isDr = m >= 1 && m <= 8`) must be
  checked against the reference for EVERY value it admits, not just the ones
  the current tests exercise. Precedent: B6's predict_block_8x8 shipped with
  H_PRED (m==2) inside the dr range — acute bug caught in QW3, latent V/H
  angleDelta divergence caught in the QW audit; both were invisible to the
  spot-checked subset of modes.

 
- Do not write test and implementation in the same edit/commit.
- Do not write a test you already know will pass â€” if it passes on first run
  without new code, you skipped ahead or the test is too weak.
- Do not write more than one new assertion at a time. If two things need
  testing, that's two trips through the loop, not one.
- Do not generalize ahead of the tests. Hardcoded/special-cased GREEN code is
  correct and expected early on â€” let a future failing test force the
  generalization, don't anticipate it.
- If a task looks big, your first job is to decompose it into an ordered list
  of small slices before writing any test. Show that list before starting the loop.
- If you get stuck failing the same test after a couple of honest attempts,
  stop and explain what you're seeing rather than guessing repeatedly.
## Test command
 
Determine the project's test command before starting work. Check, in order:
 
1. `package.json` â†’ `npm test`, `npm run test`, or `yarn test` / `pnpm test`
2. `Cargo.toml` â†’ `cargo test` (optionally `cargo test -p <crate>` for workspace members)
3. `pyproject.toml` / `setup.py` / `pytest.ini` â†’ `pytest`
4. `go.mod` â†’ `go test ./...`
5. `CMakeLists.txt` / `Makefile` â†’ `ctest` or `make test`
6. `*.csproj` / `*.sln` â†’ `dotnet test`
7. A custom script documented in the repo's README or CI config
If no test runner exists yet, the first task is to add the minimal test
harness for the language/framework in use â€” itself done via the nano-cycle
(the first test: "the test runner can run a trivial passing test").
 
Run the full suite with whatever command applies. Run a single test by name
or file when iterating on one RED/GREEN step, then always re-run the full
suite before moving on.
 
## General dev environment guidance
 
- **Before writing any code**, read the project's existing structure,
  conventions, and neighboring files. Mimic the style already present â€”
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
  port (e.g. replacing an existing SIMD path). Track perf status separately â€”
  see below.
- **Floating point comparisons need an explicit tolerance, decided up front.**
  GPU FP results will not bit-match the existing AVX2/AVX-512 CPU reference
  paths in SVT-AV1. Before writing the first RED test that compares GPU
  output to a CPU reference, decide and document the tolerance (ULP-based or
  epsilon, whichever fits the operation) in the test file itself as a
  comment. Do not pick a tolerance ad hoc per test â€” inconsistent tolerances
  make it impossible to tell a real regression from noise. If a comparison
  fails and you're not sure whether it's a bug or expected rounding
  divergence, that's a stop-and-explain situation, not a guess-and-loosen-
  the-tolerance situation.
- **GPU availability is an environment skip, not a failure.** Tests requiring
  an actual CUDA device, a specific compute capability, or VRAM beyond what's
  available must use the `SKIP:`-prefixed early-return pattern already
  defined above. Do not "fix" a missing-GPU skip by weakening or removing the
  assertion â€” the skip exists so the test suite stays honest on machines
  without the hardware, not so the test becomes meaningless everywhere.
- **A kernel can pass and still be badly formed.** Occupancy problems,
  register spills to local memory, uncoalesced memory access, and warp
  divergence are all invisible to a functional correctness test. When a
  kernel reaches GREEN, note in the step-report whether a compile/PTX-level
  sanity check (e.g. `nvcc --ptxas-options=-v` register/spill output) was
  reviewed. This isn't a new law-breaking step â€” it's a checklist item during
  REFACTOR, not a reason to add unrequested generalization.
- **Perf regressions get their own check, run less often than the unit
  suite.** Do not fold throughput/benchmark assertions into the nano-cycle's
  RED/GREEN loop â€” they're too slow and too noisy to gate every tiny slice.
  Instead, maintain a separate benchmark pass (documented once a harness
  exists) that's run at slice-group boundaries, not per-assertion. If no
  such harness exists yet when the first performance-sensitive kernel lands,
  say so explicitly rather than silently skipping perf verification.
 

## Layer map (current)

North star: a working, bit-exact 1:1 port of SVT-AV1 on CUDA 12.2 - every algorithm traceable to the vendored C source.

- l0_core â€” minimal types (Sample, BlockSize) shared across layers.
- l1_pixels â€” pixels::Plane (strided pixel buffer with padding).
- l2_gpurt â€” NVRTC JIT + driver-API runtime (GpuContext, DeviceBuffer, Kernel, ptxEntryNames); kernels are CUDA C++ source strings compiled for compute_61.
- l3_transforms â€” SVT-AV1 fixed-point forward transforms: fdct4, fadst4, fwdTxfm2d4x4(TxType) (4x4, cos_bit=13) plus a bit-exact GPU twin; cospi/sinpi cos_bit=13 tables and halfBtf/roundShift. Integer only. Also the quantizer stage (Q1): buildQuantTables (luma rows of svt_av1_build_quantizer, sharpness=0), quantizeFp4x4 / quantizeB4x4 (verbatim semantics of quantize_fp_helper_c / svt_aom_quantize_b_c at log_scale 0), defaultScan4x4 (svt_aom_init_iscan formula); dc/ac unified via table index [rc != 0] â€” this SVT tree has no av1_quantize_dc.
- l4_intra â€” buildIntraPredictors (1:1 with SVT build_intra_predictors, luma, DC availability variants, missing-neighbor fills), drZ1/2/3 + drPredictor, edge filter/upsample, smoothPredict family; GPU twin predict_block_4x4.
- l5_motion â€” motion::sad8x8 (strided uint8, bit-exact with compute8x8_sad_kernel_c) + GPU kernel.
- l6_pipeline - block + frame composition and DECISION: encodeBlock4x4 / encodeRecon4x4 / encodeFrameRecon4x4 (raster 4x4 grid, intra-only, each block predicting from RECONSTRUCTED neighbors - M1 availability rules) plus encodeFrameAuto4x4, where each block's mode is CHOSEN by the D2 SAD policy evaluated against reconstructed edges (policy is ours; primitives 1:1) and chosen modes feed NeighborContext (filt_type live) = plane window (l1) + buildIntraPredictors (l4) -> int16 residual (no clamp) -> fwdTxfm2d4x4 (l3) -> invTxfm2dAdd4x4 (l3 inverse) onto the same predictor. Goldens captured from SVT's own C in the committed generator (tools/golden_gen).
- third_party/ â€” vendored SVT-AV1 (1:1 source of truth), doctest, hardware docs (perf-axis only: PTX ISA, GP104 whitepaper, Pascal Tuning Guide, Nsight-focused guides; CUDA 12.2 profiling is ncu, not nvprof).


