# CS5c - the coefficient semantics delta table (audit only)

Every place our quantizer/entropy port differs in semantics from SVT's, named
and verdict-tagged. The pinned reference is `third_party/SVT-AV1/` (the
vendored 4.2-era snapshot). No code changes in this ticket - the deltas are
named so we know what is exact and what is a shortcut. Each verdict: EXACT
(bit-exact with the pinned reference), SHORTCUT (a documented simplification
that does not diverge for the committed fixtures), or CRITICAL (a genuine
bit-exactness divergence).

## Delta 1: Dead-quant skip

**Verdict: EXACT - no delta exists. The briefing's description is false.**

The briefing described a "whole-TU short-circuit" in quantizeFpN/quantizeBN
that SVT checks per-coefficient. The code shows otherwise:

- Our `quantizeFpN` (transform.cpp:7734-7763) iterates ALL nCoeffs
  coefficients. Each coefficient checks the zbin threshold individually:
  `(absCoeff << (1 + logScale)) >= thresh` (:7745). There is no whole-TU
  early-out, no dequant[dc] threshold pre-check, no "all AC below threshold"
  short-circuit. The loop structure matches SVT's quantize_fp_helper_c
  (full_loop.c:237-256) line-for-line: the same per-coefficient zbin gate,
  the same rounding + quant + dequant chain.

- Our `quantizeBN` (transform.cpp:7798-7808) has a pre-scan pass that counts
  trailing zeros from the scan tail: coefficients with
  `coeff * (1<<5) < zbin << 5` and `> nzbins << 5` are skipped. This matches
  SVT's svt_aom_quantize_b_c pre-scan (full_loop.c:44-55) EXACTLY: the same
  loop from n_coeffs-1 down to 0, the same `break` on the first coefficient
  outside the zbin window, the same non_zero_count decrement.

- The FP path has no pre-scan (SVT's quantize_fp_helper_c also does not -
  the zbin_ptr parameter is explicitly `(void)zbin_ptr` at full_loop.c:229).
  The FP path relies on the per-coefficient threshold check alone, and the
  eob tracks the last nonzero coefficient (full_loop.c:252-255 = our
  :7760-7764).

No size or position can be "silently zeroed" that SVT would not zero - the
per-coefficient zbin gate is the same gate SVT applies. The briefing's
description was a mischaracterization; the audit finding is that the delta
does not exist.

Citations: full_loop.c:222-255 (quantize_fp_helper_c), full_loop.c:31-55 +
:59-75 (svt_aom_quantize_b_c), transform.cpp:7724-7765 (quantizeFpN),
transform.cpp:7783-7843 (quantizeBN).

## Delta 2: log_scale arithmetic

**Verdict: EXACT. The escalated arithmetic matches svt_aom_quantize_b_c and
quantize_fp_helper_c line-for-line. No size takes a different branch than
the table says.**

The escalated arithmetic (ROUND_POWER_OF_TWO / threshold shift / quant shift
/ dq shift) is consumed by both quantizeFpN and quantizeBN:

- Rounding: our `(roundFp[k] + ((1 << logScale) >> 1)) >> logScale`
  (transform.cpp:7731-7733 FP; :7822-7823 BN) =
  `ROUND_POWER_OF_TWO(round_ptr[k], log_scale)` (full_loop.c:228 FP,
  :67 BN). The ROUND_POWER_OF_TWO macro (definitions.h:457) is
  `(((value) + ((1 << (n)) >> 1)) >> (n))` - the formulations are identical.
  At logScale 0 the inner shift is 0, degenerating to the identity.

- Threshold: our `(absCoeff << (1 + logScale)) >= thresh`
  (transform.cpp:7745) = SVT `(abs_coeff << (1 + log_scale)) >= thresh`
  (full_loop.c:244 FP). The BN path: `absCoeff * (1 << 5) >= (zbin << 5)` =
  SVT `abs_coeff * wt >= (zbins[rc != 0] << AOM_QM_BITS)` (full_loop.c:66)
  with qm_ptr = NULL, wt = 1 << AOM_QM_BITS = 32 <=.

- Quant: our `(absCoeff * quantFp[rc != 0]) >> (16 - logScale)` =
  SVT `(abs_coeff * quant_ptr[rc != 0]) >> (16 - log_scale)`
  (full_loop.c:246) <=. The BN quant-shift formula:
  `((((tmp * quant[rc != 0]) >> 16) + tmp) * quantShift[rc != 0]) >>
  (16 - logScale + 5)` (transform.cpp:7827-7829) =
  SVT `(((((tmp * quant_ptr[rc != 0]) >> 16) + tmp) * quant_shift_ptr[rc != 0])
  >> (16 - log_scale + AOM_QM_BITS))` (full_loop.c:69) <= (AOM_QM_BITS = 5).

- DQ: our `(tmp32 * dequant[rc != 0]) >> logScale` (transform.cpp:7754-7756
  FP; :7831-7834 BN) = SVT `(tmp32 * dequant_ptr[rc != 0]) >> log_scale`
  (full_loop.c:249 FP; :72 BN) <=.

The log_scale per size follows av1_get_tx_scale_tab (full_loop.c:22):
`{0, 0, 0, 1, 2, ...}` for the first five square sizes. Our wrappers:
quantizeFp4x4 <= logScale 0, quantizeFp8x8 <= 0, quantizeFp16x16 <= 0,
quantizeFp32x32 <= 1 (transform.cpp:7869), quantizeFp64x64 <= 2 (:7881),
quantizeFp64x64Token <= 2 (:7894). Same for the BN wrappers. The values
match the table entry-for-entry <=.

The clamp: our `if (clamped < -32768) ... if (clamped > 32767)` (two
conditionals, transform.cpp:7747-7748 FP; :7824-7825 BN) = SVT
`clamp64(abs_coeff + rounding[rc != 0], INT16_MIN, INT16_MAX)`
(full_loop.c:245 FP; `clamp(...)` :67 BN). The clamp64 helper
(definitions.h:691) does the same min/max - semantically identical <=.

Citations: full_loop.c:22 (the tx_scale_tab), :222-255 (quantize_fp_helper_c),
:31-75 (svt_aom_quantize_b_c), transform.cpp:7724-7765 (quantizeFpN),
:7783-7843 (quantizeBN), :7866-7895 (the per-size wrappers).

## Delta 3: DC/AC unified path

**Verdict: EXACT. The unified [rc != 0] indexing mirrors SVT's own design.
The vendored tree has no av1_quantize_dc - the unification is deliberate.**

Both quantize_fp_helper_c (full_loop.c:239) and svt_aom_quantize_b_c
(full_loop.c:50/:66) index the dequant/zbin/quant/round tables via
`[rc != 0]` - a 2-element table lookup where index 0 = DC (scan position 0)
and index 1 = AC. Our port uses the identical `tables.dequant[rc != 0]`,
`tables.quantFp[rc != 0]`, `tables.round[rc != 0]` indexing
(transform.cpp:7740/:7746/:7750/:7755 FP; :7803/:7819/:7823/:7828 BN) <=.

The vendored SVT tree has NO `av1_quantize_dc` function (grep-verified:
zero hits in the Codec/ directory). The aom tree (the out-of-tree arbiter,
third_party/aom/av1/encoder/) HAS av1_quantize_dc_facade
(aom av1_quantize.c:409) - but that is the decoder-side oracle, not the
ported encoder path. The single-table [rc != 0] path is correct for all
sizes: the dc_quant_qtx and ac_quant_qtx lookups (inv_transforms.c:3467/
:3484) produce the two-entry dequant table, and every quantize function
indexes it by [rc != 0] = whether scan position == 0 (DC) or not (AC).

No size takes a separate DC or AC path - the unification is SVT's design,
mirrored exactly.

Citations: full_loop.c:239/:246/:249/:265/:269 (the [rc != 0] indexing in
both FP and BN paths), transform.cpp:7740/:7750/:7755/:7803/:7819 (our
matching [rc != 0] sites), the av1_quantize_dc grep (zero hits in the
vendored Codec/ tree).

## Delta 4: 64x64 token scan

**Verdict: SHORTCUT (documented, the FS5d named follow-up). Not a
bit-exactness divergence for the committed fixtures.**

Our quantizeFp64x64Token (transform.cpp:7891-7895) operates on the
COMPACTED 1024-position domain: the fwd64 output's top-left 32x32 is
compacted into a 32-wide buffer (transforms.c:2700-2707,
svt_handle_transform64x64_N2_N4_c), quantized with the 1024-position token
scan (the normative scan, svtd_default_scan_64x64_token), and the compacted
dqcoeff is expanded into the 64-wide zeroed array before inv64
(inv_transforms.c:2615-2628).

The legacy/GPU callers (quantizeFp64x64/quantizeB64x64 at transform.cpp:
7879-7887) keep the 4096-wide facade: the FULL 64x64 block is quantized
with n_coeffs=4096 and the full 64x64 scan. The GPU quant_dequant_64x64
kernel is a 4096-position kernel.

For the fs264 fixture the two domains' recons coincide (the outer-ring
coefficients quantize to 0 at q100 - the named FS5d deviation in
provenance.md #12). The emission domain is the compacted 1024-position
token scan (full_loop.c:1262 / inv_transforms.h:129-137); the facade keeps
the 4096-wide view. Unification = a 1024-position GPU kernel + bench work
(the FS5d named follow-up).

The mapping:
1. fwdTxfm2d64x64 produces a 4096-element output (64x64)
2. The top-left 32x32 (1024 coefficients) is compacted in place (the row
   stride 64 -> 32)
3. quantizeFp64x64Token quantizes n_coeffs=1024 with the 1024-position
   scan (log_scale 2)
4. The compacted dqcoeff (32-wide) is expanded into the 64-wide zeroed
   array (the inverse of the compaction)
5. invTxfm2dAdd64x64 consumes the expanded 64-wide dqcoeff

Where the legacy/GPU callers still see the 4096 facade: the non-emission
callers (the GPU frame paths, the Recon non-Q variants) call
quantizeFp64x64 with the full 64x64 scan - the two paths coexist (the
FS5d named deviation).

Citations: full_loop.c:1262 (the quantize at n_coeffs 1024),
inv_transforms.h:129-137 (av1_get_max_eob), transforms.c:2700-2707 (the
compaction), inv_transforms.c:2615-2628 (the expansion), transform.cpp:
7889-7895 (quantizeFp64x64Token), composition.c:6200-6203 (the emission
compaction), provenance.md #12 (the FS5d named deviation).

## Delta 5: Sharpening

**Verdict: SHORTCUT (named, with a trigger). The sharpened tables (1-7) are
not ported. The committed configs all use sharpness = 0.**

Our buildQuantTables (transform.cpp:7681-7691) hardcodes sharpness = 0 (the
comment at :7680). The SVT svt_av1_build_quantizer (md_config_process.c:
111-123) reads sharpness_val from the config: when nonzero, it adjusts
qzbin_factor and qrounding_factor by an offset derived from the sharpness
value and the q-range diff. For sharpness = 0, the adjustment branch is
DEAD (the `if` condition is false) and the base values are used.

Our port omits the adjustment branch entirely (the dead-code elimination
for sharpness = 0): qzbinFactor (transform.cpp:7674-7677) returns the base
value (q == 0 ? 64 : quant < 148 ? 84 : 80), matching
svt_aom_get_qzbin_factor (inv_transforms.c:3501-3505, EB_EIGHT_BIT) for
sharpness = 0. The qroundingFactor (q == 0 ? 64 : 48) matches
md_config_process.c:108.

If sharpness were set to 1-7, EVERY quantizer table entry would change
(the zbin, round, dequant all shift). The sharpened configs are NOT ported
and the encoder does not expose a sharpness setting. The trigger: a
sharpness != 0 config request.

Citations: md_config_process.c:107-123 (the sharpness adjustment branch),
transform.cpp:7674-7677 (qzbinFactor, sharpness=0 only), transform.cpp:
7680 (the sharpness == 0 comment), transform.cpp:7681-7691 (buildQuantTables).

## Summary

| Delta | Verdict | Impact |
| --- | --- | --- |
| Dead-quant skip | EXACT (no delta exists) | None |
| log_scale arithmetic | EXACT | None |
| DC/AC unified path | EXACT (deliberate, mirrors SVT) | None |
| 64x64 token scan | SHORTCUT (the FS5d dual-domain facade) | None for the committed fixtures |
| Sharpening | SHORTCUT (sharpness=0 only) | A named deviation with a trigger |

No CRITICAL findings. Every delta is either exact or a documented,
non-diverging shortcut for the committed fixture set.