# Production-Readiness Roadmap — av1_gpu_pascal

NOTE: this file was recreated 2026-09-23 (FS6) after the original untracked
copy was deleted in error, then RESTORED THE SAME DAY by the court from its
session record, merged with the FS6-era register. The production roadmap
(§1-§9) is the deliverable the boss requested; §A0 + the lesson register
carry the live slice-level state.

Purpose: the complete work inventory between the current verified state and
PRODUCTION-READY — defined as the step before the "ffmpeg blueprint" phase
(integrating the encoder behind a real consumption pipeline) can start.

North stars (AGENTS.md):
1. Bit-exact 1:1 with the PINNED vendored SVT-AV1 reference
   (third_party/SVT-AV1, 4.2-era snapshot). Every algorithm traceable.
2. Production-ready: not just correct — fast, robust, validated, packaged.

How to use this file:
- Statuses: [x] DONE (court/Luna-verified) · [~] IN FLIGHT · [ ] OPEN ·
  (P) PARKED with a named trigger.
- Every OPEN item lands as AGENTS.md slices: TDD nano-cycle, RED evidence,
  one slice = one commit, full suite + GATE green, measured values only,
  citation grep, goldens via tools/golden_gen, deviations named.
- CONFORMANCE RULE (the TD-series lesson): internal writer/reader agreement
  proves self-consistency, never conformance. Every new bitstream surface is
  settled by the REAL DECODER (libdav1d / aomdec / ffmpeg) — content-1:1
  against the generator's recon, per geometry and per configuration.
- LESSON REGISTER (paid for in blood, applies to every item below):
  * A named deviation is a deferred bug with a timestamp (TS1 q_ctx=0 slice
    -> the TD4/TD5 root cause).
  * Self-consistent gates cannot see shared-port bugs (TD1's void claim;
    the FS2-delta and FS5b S==16 drive omissions — both caught by
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
  (687625f) — d4 30B 64/64, d8 30B 64/64, d16 44B 256/256, d32 47B
  1024/1024, d64 441B 4096/4096 (tools/verify_decode4.ps1 -Geometry);
  FS6 docs currency (8edc02d).
- [ ] OPEN (named): the filter-intra DC-deciding fixture (the FI branch is
  predicate-gated but never fires in the committed fixtures).
- [~] NAMED FOLLOW-UP (FS5d): the 64x64 dual-domain unification — the
  emission case runs the compacted 1024-position token domain; the
  legacy/GPU callers keep the 4096-wide facade; unification = a
  1024-position GPU quant kernel + bench protocol.
- [~] PENDING: Luna's full audit of the FS/TD state at series close.

------------------------------------------------------------
0. CURRENT STATE (the verified baseline — 2026-09-23)
------------------------------------------------------------

[x] l0-l2: core types, Plane, NVRTC/driver runtime (compute_61 PTX JIT)
[x] l3: fwd/inv 1D+2D transforms, 4x4..64x64, DCT/ADST (DCT-only at 64);
    quantizer (fp/b, all sizes, q0..q255 gate lines, log_scale 0/0/0/1/2);
    default scans through 64x64; the compacted 1024-position token domain
    at 64x64 (the FS2 scan contract)
[x] l4: full intra predictor set, all modes, 4x4..64x64, GPU kernels,
    corner-blend/upsample/edge-filter regimes, chroma fold (uv2y) +
    NeighborContext/filt_type + disable_edge_filter
[x] l5: SAD 4x4..64x64 host+GPU, strided uint8
[x] l6: mode decision (13-candidate SAD policy — OURS, documented), frame
    loops at 4x4..64x64 (Recon/Auto/ReconQ/AutoQ), real-TR gather, symbol
    emission (kf modes/delta/FI + partition + skip + token chains) at ALL
    FIVE geometries, running partition contexts (multi-block grids)
[x] l7: od_ec enc/dec (verbatim), update_cdf, writer/reader wrappers, KF
    mode/delta/FI surfaces, partition/skip symbols, the full token chain,
    qindex-bucketed coefficient CDFs (TD5, spec init_coeff_cdfs),
    updatePartitionContext (ECP1)
[x] l8: bit writer, uleb128, OBU/SPS/frame-header/TU assembly (v1/v2),
    per-size SPS (maxDim parameterized), FIVE committed artifacts
    (d4 30B / d8 30B / d16 44B / d32 47B / d64 441B)
[x] MILESTONE: decoder-accepted AND content-bit-identical at EVERY
    geometry (libdav1d via ffmpeg; verify_decode4.ps1 -Geometry N)
[x] Infrastructure: tools/golden_gen (committed gate, 335 lines, the
    exhaustive ectx4..ectx64+ectxg context gates), tools/td0_ladder.ps1
    (7/7), tools/decode_handoff.ps1, tools/bench, tools/verify_decode4.ps1
[x] Docs: README, AGENTS.md layer map, provenance.md, decode_conformance.md
    (TD CLOSED + FS5 table), bitstream.md, layers.md, this file

------------------------------------------------------------
1. KEYFRAME ENCODER COMPLETENESS (intra-only, currently monochrome)
------------------------------------------------------------

1.1 Chroma residual + uv_mode symbols (ends the monochrome limitation)
- [ ] uv_mode symbol surface in l7 (writer/reader, EC3 deferral):
      UV mode CDFs + the g_uv2y fold at the token surface (prediction fold
      exists; the SYNTAX fold at the entropy layer does not)
- [ ] Chroma coefficient chains: per-plane token coding (component_type,
      uv txb ctx branch, the chroma skip_contexts table — ported but dead)
- [ ] 4:2:0 plane plumbing through l6 emission + recon (UV plane loops,
      per-plane NA/cul_level)
- [ ] separate_uv_delta_q + UV dequant fields (the D1 mono patch inverts)
- [ ] Chroma frame artifacts + real-decoder content-1:1 per plane
- [ ] uv_mode in the decision (chroma mode policy — OURS)
1.2 CFL (chroma-from-luma)
- [ ] cfl_alpha symbol surface + the AC-from-luma combine (P: after 1.1)
1.3 TX surface completion
- [ ] TX_MODE_SELECT surface (av1_code_tx_size currently silent at LARGEST)
- [ ] Rectangular transforms 4x8..64x32 (2D cores + scans + the
      TX_CLASS_HORIZ/VERT nz-map rows — token chain currently 2D-only)
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
- [ ] Adaptive scan (get_scan_order per mode/tx — default scan is policy)
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
- [ ] Nonkey y-mode path (entropy_coding.c:1046-1058 — the EC3 deferral)
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
- [ ] Filter application (edge dirs, levels, deltas) — host port then GPU
- [ ] Filter level/delta search (the encoder side)
3.2 CDEF
- [ ] CDEF application (dir/size/damping, GPU-friendly)
- [ ] CDEF search (dir + strength RDO; header surface when enabled)
3.3 Loop restoration
- [ ] Wiener + SGRProj application + search
- [ ] lr_params header surface (seq enable_restoration currently 0)
Conformance: each filter surface gets the real-decoder content gate
(header + pixels) — the TS/TD pattern, per configuration.

------------------------------------------------------------
4. SCREEN CONTENT & MISC TOOLS
------------------------------------------------------------
(P) Palette mode (symbols + palette tokens + search) — natural-video
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
      lossy x tiles) through aomdec + dav1d + ffmpeg — content-1:1 per
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
8. DEFINITION OF DONE — the gate before the ffmpeg blueprint
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
- AomReader struct-shape deviation (l7 vs vendored layout — harmless in
  self-consistent use; the TD2 harness crash taught the exchange rule)
- 4096-facade vs 1024 token domain at 64x64 (FS5d, OPEN)
- The FI DC-deciding fixture (OPEN)
- od_ec_dec_bits_ declared-undefined in the pinned tree (named, no
  consumer)
- Bench baselines are clock-noise-sensitive without NVML lock (BM3)
- sm_61 target vs sm_86 dev box: the measurement split is documented; no
  perf claims without the harness
- PROCESS (new, 2026-09-23): untracked files are NEVER strays — deletion
  requires user/court confirmation; the court commits its authored docs
  immediately (the todo.md deletion incident)
