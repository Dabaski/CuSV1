# TODO / in-flight register

NOTE: this file was recreated 2026-09-23 (FS6). An untracked prior copy
was deleted in error immediately before the FS5c commit (mistaken for a
stray). The content below is rebuilt from the committed series records;
any parallel-partition entries lost with the original copy are to be
re-added by their owner.

## §0 In-flight

- FS5 per-geometry emission (the running partition contexts, the v3
  artifacts, the per-geometry decode gate) — **DONE** (FS-series closed):
  4e0a685 (the wiring + the fs5g32 grid gate), a853ac9 (the per-geometry
  artifacts), 687625f (the d4 structure fix + ALL FIVE artifacts
  content-1:1 measured per geometry).
- Open (named): the filter-intra DC-deciding fixture (the FI branch is
  predicate-gated but never fires in the committed fixtures — the fs232
  fixture decided mode 1, the fs5g32 grid decided 10 5 3 7, the fs5g4
  grid decided 1 7 2 2; no DC block).
- Open (named, FS5d): the 64x64 dual-domain unification — the emission
  case runs the compacted 1024-position token domain, the legacy/GPU
  callers keep the 4096-wide facade; unification = a 1024-position GPU
  kernel + bench work.

## Lesson register

(a) The mutation-check pattern for coincidence-passing fixtures: a
    multi-block fixture may PASS under the pre-fix code when the running
    and fresh states coincide (FS5a: for uniform PARTITION_NONE grids the
    neighbor's lookup bit at the leaf's own bsl is 0 for every square
    size — partitionContextLookupLeft[32X32]=24 bit2=0, [16X16]=28
    bit1=0, [8X8]=30 bit0=0). A test that passes on first build must be
    proven discriminating by a mutation (the FS5a grid: mutating the
    left-lookup byte to a wrong-size value failed the test with the
    measured byte diffs — the ctx wiring's necessity was proven by
    mutation, not by the fixture alone).
(b) The decoders align frame dims to 8 pixels
    (aligned_width = ALIGN_POWER_OF_TWO(w, 3)): a 4x4 frame has a 2x2 mi
    grid and the 8x8 node READS a 4-symbol partition symbol; a 4x4 TU
    exists only as a partition leaf (FS5c, dav1d 1.5.4 trace-verified).
    The smallest decodable single-TU frame is 8x8.
(c) Generator-drive self-consistency traps (the FS2-delta pattern,
    repeated at FS5b): a drive with a missing/wrong symbol path and its
    read twin roundtrip against EACH OTHER (rt=1) while diverging from
    the real decoder — only the l6 bit-exactness tests against the
    independently-produced production walk expose them.