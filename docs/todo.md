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
- [ ] OPEN (named): the filter-intra DC-deciding fixture (the FI branch is
  predicate-gated but never fires in the committed fixtures).
- [~] NAMED FOLLOW-UP (FS5d): the 64x64 dual-domain unification - the
  emission case runs the compacted 1024-position token domain; the
  legacy/GPU callers keep the 4096-wide facade; unification = a
  1024-position GPU quant kernel + bench protocol.
- [~] PENDING: Luna's full audit of the FS/TD state at series close.

------------------------------------------------------------
0. CURRENT STATE (the verified baseline - 2026-09-23)
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
[x] l6: mode decision (13-candidate SAD policy - OURS, documented), frame
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
      fails 17 assertions). DEFERRED (named): write_cfl_alphas (:1060-1071) -
      UV_CFL_PRED is not a D2 candidate (pipeline.cpp:1721), the alphas are
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
- [ ] CS5 color artifacts + per-plane instrument (yuv420p: Y/U/V compared
      separately vs the generator's per-plane recon) + USER decode checks.
- [ ] CS6 docs currency (currency item noted: docs/ffmpeg_integration.md's
      "repo reality" section is stale - pre-quantizer/entropy/bitstream;
      the C-ABI sketch stands as the target contract, the section 8 gate
      governs).
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
- od_ec_dec_bits_ declared-undefined in the pinned tree (named, no
  consumer)
- Bench baselines are clock-noise-sensitive without NVML lock (BM3)
- sm_61 target vs sm_86 dev box: the measurement split is documented; no
  perf claims without the harness
- PROCESS (new, 2026-09-23): untracked files are NEVER strays - deletion
  requires user/court confirmation; the court commits its authored docs
  immediately (the todo.md deletion incident)
- Debug-config /RTC finding (2026-09-24, observed, NOT fixed - out of the
  CS1 agent's partition): av1_l4_intra_tests.exe in Debug aborts with
  Run-Time Check Failure #2 (stack around 'buf' corrupted); all in-tree
  changes to its link set at observation time were comment-only, so the
  corruption is almost certainly pre-existing (Release, the standing suite
  config, has /RTC off and passes 9/9). Court to route to the l4 owner or
  order a fix; Debug ctest runs will block on the modal dialog until then.
