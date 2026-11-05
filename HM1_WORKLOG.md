# HM1_WORKLOG: A3 (Hash-Motion + Global-Motion surface)

## Status: GREEN (Local and Judge-verified)
- **Local suite:** `l5_motion` target (34/34 test cases) verified passing locally after M5 revert.
- **Judge-verified suite:** `ctest --test-dir build -C Release --output-on-failure` = **15/15 passed, 0 failed, 277.34 sec**.
- **GATE verification:** out.txt **884,851 B** == committed `expected_primitives.txt` **884,851 B**, **hash-identical, diff 0**.
- **M5 verification:** Local `l5_motion` re-run (0.60s, 100% passed) after M5 revert.

## Per-file breakdown (ported / already-landed / parked)
| File | Action | Measured NB | Breakdown |
|---|---|---|---|
| `src/l5_motion/hash_motion.cpp` | Ported (pure hash machinery) | 236 nb | Pure machinery ported. Walls/PCS/lookahead walkers parked. |
| `src/l5_motion/hash_motion.h` | Ported (declarations) | 83 nb | Function and struct declarations. |
| `src/l5_motion/global_motion.cpp` | Ported (pure GM math) | 92 nb | Pure GM math ported. Walls/PCS/fit-failed logic parked. |
| `src/l5_motion/global_motion.h` | Ported (declarations) | 44 nb | Function and struct declarations. |
| `src/l5_motion/tests/test_motion.cpp` | Extended (34 test cases) | 604 nb | 3 new HM tests + 3 new GM tests + mutation kills. |

## Mutations killed (Phase I: M1-M5)
- **M1: Hash byte-order swap** (`hash_motion.cpp`, getPixels2x2 row-major reversal).
  - *Kill:* Test suite failed with 3 errors; HM4 (`test_motion.cpp:326, 328`) and HM5 (`test_motion.cpp:355, 356`) caught the hash mismatch.
  - *Revert:* Row-major order restored.
- **M2: Table index wrap break** (`hash_motion.cpp`, dropped bucket cap in `hashTableAddToTable`).
  - *Kill:* Compilation failed on an unused parameter `maxCandPerBucket` (C2220 treated as error).
  - *Revert:* `hashTableAddToTable` restored to enforce cap check.
- **M3: Fit iteration bound break** (`global_motion.cpp`, loop bound `i < nRefinements - 1`).
  - *Kill:* Test suite failed with 2 errors; HM1 GM4 (`test_motion.cpp:599, 600`) caught the fit iteration mismatch.
  - *Revert:* Loop bound restored to `i < nRefinements`.
- **M4: Parameter-clip drop** (`global_motion.cpp`, parameter-clip dropped in `convertToParams`).
  - *Kill:* Test suite failed with 1 error; GM2 (`test_motion.cpp:529`) caught the clip drop.
  - *Revert:* Clamp call restored.
- **M5: Wiener-conversion coefficient swap** (`global_motion.cpp`, coefficient swap in `forceWmType`).
  - *Kill:* Test suite failed with 2 errors; GM5 (`test_motion.cpp:569, 570`) caught the coefficient swap.
  - *Revert:* Matrix assignments restored.

## RED evidence (RED-first workflow)
- **RED state:** Local test suite was red during M1-M5 mutation phases (e.g., M5 produced 2 failing CHECKs at `test_motion.cpp:569-570`).
- **GREEN state:** All local checks passed after each revert.
- **GATE: RED-free:** Judge-confirmed 884,851 B hash match, 0 diff (clean UTF-8 output).

## Commit strategy
- **Commit hash:** to be determined at commit time.
- **Commit message:** based on `TDD` protocol. Full RED evidence quoted. Measured values only (Judge-run suite, GATE).
- **Contents:** Test + implementation in one commit.
