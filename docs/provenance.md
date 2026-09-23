# PV — function-by-function provenance table

Every port artifact in `src/l3_transforms`, `src/l4_intra`,
`src/l5_motion`, `src/l6_pipeline`, `src/l7_entropy` and
`src/l8_bitstream` (plus the l2 runtime and fixture classification
below) traced to the pinned vendored reference (`third_party/SVT-AV1/`,
REFERENCE PINNING rule in `AGENTS.md`). l0_core/l1_pixels are
infrastructure with no SVT symbol to trace (single policy row in the
classification section). Status vocabulary:

- **exact (mech)** — the SVT body was ported mechanically from the committed
  verbatim extract (`svt_gen.c`) with identifier/table renames only; the
  arithmetic text is byte-derived from the extract. Currently exactly two
  ports: `fdct64`/`fdct64B` and `idct64`.
- **exact (hand)** — hand-transcribed from the SVT source with the
  arithmetic text unchanged (mechanical renames, C++ `std::` spellings);
  bit-exactness is gate-proven by `tools/golden_gen`
  (expected_primitives.txt, 335 lines). All pre-extract-era ports (the
  C-series 4x4/8x8/16x16 kernels, the L-series 32x32 kernels, the helpers,
  and all of l4/l5) are this class.
- **adapted** — SVT structure is preserved but the port changes shape:
  thread mapping, buffer ownership, parameterization, or a documented
  specialization of a generic SVT function. The per-sample arithmetic is
  still 1:1 SVT; what changed is the wrapper around it.
- **policy** — project-defined decision logic that SVT does not prescribe
  at this scope. SVT primitives inside it are exact/adapted as listed.

Golden provenance: every expected value comes from `tools/golden_gen`
(committed generator; `extract.ps1` copies SVT bodies verbatim into
`svt_gen.c`), never from hand-traces. The generator's own provenance table
is in `tools/golden_gen/README.md`.

## l3_transforms (src/l3_transforms/transform.cpp)

| SVT symbol (file:line) | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| svt_av1_fdct4_new (transforms.c) | `fdct4` (transform.cpp:3469) | exact (hand) | hand transcription, gate fdct4 |
| svt_av1_fadst4_new | `fadst4` (:3495) | exact (hand) | gate fadst4 |
| svt_av1_fdct8_new | `fdct8` (:608) | exact (hand) | gate fdct8 |
| svt_av1_fadst8_new | `fadst8` (:678) | exact (hand) | gate fadst8 |
| svt_av1_fdct16_new | `fdct16` (:1059), cos_bit-parameterized | exact (hand) | gate fdct16 (col 13 / row 12) |
| svt_av1_fadst16_new | `fadst16` (:1063), cos_bit-parameterized | exact (hand) | gate fadst16 |
| svt_av1_fdct32_new | `fdct32` (:1069), cos_bit 12 fixed | exact (hand) | gate fdct32 |
| svt_av1_fadst32_new (static, transforms.c:1908) | `fadst32` (:2895) | exact (hand) | gate fadst32 |
| svt_av1_fdct64_new (transforms.c:762) | `fdct64`/`fdct64B` (:1347/:1351) | exact (mech) | DCT-only; mechanical port of the verbatim extract, SVT pointer-swap structure preserved (bf0/bf1 alternation); fdct64B takes cos_bit (col 13 / row 10) |
| svt_av1_idct4_new | `idct4` (:87) | exact (hand) | gate idct4 |
| svt_av1_iadst4_new | `iadst4` (:115) | exact (hand) | gate iadst4 |
| svt_av1_idct8_new | `idct8` (:161) | exact (hand) | clamps only where SVT consumes stage_range |
| svt_av1_iadst8_new | `iadst8` (:222) | exact (hand) | |
| svt_av1_idct16_new | `idct16` (:302) | exact (hand) | clamp stages 3-7 |
| svt_av1_iadst16_new | `iadst16` (:437) | exact (hand) | clamp stages 3/5/7; no all-zero early-out at 16 |
| svt_av1_idct32_new (inv_transforms.c:378) | `idct32` (:3077) | exact (hand) | clamps ONLY stages 3-9 (audited) |
| svt_av1_iadst32_new (static, inv_transforms.c:1132) | `iadst32` (:3328) | exact (hand) | clamps EVERY stage; no all-zero early-out at 32 |
| svt_av1_idct64_new (inv_transforms.c:1567) | `idct64` (:2112) | exact (mech) | mechanical extract port; clamp bit 16 every stage via stageRange shim (gen_inv_range_64x64_dct) |
| round_shift / half_btf (inv_transforms.h) | `roundShift` / `halfBtf` | exact (hand) | pure renames |
| clamp_value / clamp_buf (inv_transforms.c) | `clampValue` / `clampBufIv` | exact (hand) | |
| svt_aom_eb_av1_cospi/sinpi_arr_data (inv_transforms.c) | `kCospi13`/`kCospi12`/`kCospi10`/`kSinpi*` + `cospiRow` | adapted | table ROWS verbatim; the 7-row table flattened to the three used rows (13/12/10), cospiRow = cospi_arr(cos_bit) mirror |
| fwd_shift_4x4/8x8/16x16/32x32/64x64 (transforms.c) | shift constants in fwdTxfm2d* | exact (hand) | {2,0,0}/{2,-1,0}/{2,-2,0}/{2,-4,0}/{0,-2,-2} |
| inv_shift_4x4/8x8/16x16/32x32/64x64 (inv_transforms.c:21-22) | shift constants in invTxfm2dAdd* | exact (hand) | {0,-4}/{-1,-4}/{-2,-4}/{-2,-4}/{-2,-4} |
| fwd_cos_bit_col/row, inv_cos_bit_col/row (transforms.c:19-22, inv_transforms.h:32-41) | cos_bit constants (13/13, 13/13, 13/12, 12/12, 13/10; inv 12/12) | exact (hand) | read from the extracted tables, cited |
| av1_tranform_two_d_core_c (transforms.c:2398) | `fwdTxfm2d4x4..64x64` | adapted | generic core specialized per size: bd=8 fixed, no flips, fixed TxType column; col/row 1D calls + round_shift_array(-shift) structure preserved |
| inv_txfm2d_add_c / svt_av1_inv_txfm2d_add_64x64_c | `invTxfm2dAdd4x4..64x64` | adapted | same specialization; clamps only where SVT consumes stage_range; clip_pixel_highbd add |
| av1_txfm_type_ls (inv_transforms.h:196) | 64x64 DCT-only scope | exact (finding) | {DCT64, INVALID, INVALID, IDENTITY64} — no ADST signalable at TX_64X64; IDENTITY64 column parked, not ported |
| quantize_fp_helper_c (full_loop.c:222) | `quantizeFpN` (log_scale param), `quantizeFp4x4/8x8/16x16/32x32/64x64` | adapted | verbatim semantics with the helper's own log_scale parameter; escalated arithmetic cited (full_loop.c:228/:244/:246/:249) |
| svt_aom_quantize_b_c (full_loop.c:31) | `quantizeBN`, `quantizeB4x4/8x8/16x16/32x32/64x64` | adapted | escalated arithmetic cited (:36/:67/:69-70/:74) |
| svt_aom_init_iscan (coefficients.c:345-363) | `defaultScan4x4..64x64` | adapted | formula-based port at W=H=4..64 (the iscan loop instantiated per size) |
| dc_qlookup/ac_qlookup_QTX, svt_aom_dc/ac_quant_qtx, get_qzbin_factor, invert_quant (inv_transforms.c:3357-3516) | `buildQuantTables` | adapted | luma rows of svt_av1_build_quantizer, sharpness=0; the composition is ours, each table/step is verbatim SVT |
| svt_aom_eb_av1 dc/ac QTX lookup | GPU `quant_dequant_4x4..64x64` | adapted | fp helper arithmetic per-thread at log_scale 0/1/2; eob = max nonzero scan position + 1 (policy comment in kernel) |

GPU twins (fwd_txfm_2d_*, inv_txfm_2d_add_*, quant_dequant_*): **adapted** —
the device 1D bodies (`d_fdct4..d_fdct64`, `d_idct4i..d_idct64i`,
`d_fadst4..d_fadst32`, `d_iadst4i..d_iadst32i`) carry the verbatim stage
arithmetic; what differs is the launch geometry (one thread per column/row,
shared-memory staging) and mechanical renames (half_btf -> d_half_btf,
clamp_value -> d_cv, cos_bit -> bit). Every twin is bit-exact vs its host
port (tests in test_transform.cpp).

## l4_intra (src/l4_intra/intra.cpp)

| SVT symbol (file:line) | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| build_intra_predictors (enc_intra_prediction.c:159) | `buildIntraPredictors` (:455) | exact (hand) | the ONE extract substitution: get_filt_type(xd, plane) -> svtd_filt_type generator shim, flagged inline in svt_gen.c; the host port mirrors that shim with intra::NeighborContext carrying the same two mode values |
| svt_av1_dr_prediction_z1/z2/z3_c (intra_prediction.c) | `drZ1`/`drZ2`/`drZ3` (:202/:236/:262) | exact (hand) | gate-verified per zone |
| svt_aom_dr_predictor + eb_dr_intra_derivative + get_dx/get_dy | `drPredictor` (:734) + `getDx`/`getDy` (:345/:355) + `drIntraDerivative` (:312, verbatim table) | exact (hand) | |
| dc/dc_left/dc_top/dc_128/v/h/paeth predictors (intra_prediction.c) | inline dispatch arms inside `buildIntraPredictors` (DC_PRED :631, V_PRED :660, H_PRED :666, PAETH_PRED :672 — paeth_predictor_single mirrored inline via std::abs) | exact (hand) | |
| smooth/smooth_v/smooth_h predictors (intra_prediction.c) + sm_weight_arrays (:25) | `smoothPredict`/`smoothVPredict`/`smoothHPredict` (:365/:389/:408) + `smWeightArrays` (:291, verbatim table) | exact (hand) | |
| svt_aom_intra_edge_filter_strength + svt_av1_filter_intra_edge_c + filter_intra_edge_corner | `edgeFilterStrength`/`filterIntraEdge` (:95) + corner-blend path | exact (hand) | |
| svt_aom_use_intra_edge_upsample + svt_av1_upsample_intra_edge_c | `useUpsample` + `upsampleIntraEdge` (:760) | exact (hand) | dead above 8x8 (blk_wh > 16), audited per size |
| eb_av1_filter_intra_taps + svt_av1_filter_intra_predictor_c (C_DEFAULT/filterintra_c.c) | `filterIntraPredictor` (:694) | exact (hand) | verbatim asserts bw<=32 retained — FI is NOT signalable at 64x64 |
| sm_weight_arrays (intra_prediction.c:25) | `sm_w` rows + `sm_w32`/`sm_w64` GPU constants | exact (hand) | bs=16/32/64 rows verbatim |
| mode_to_angle_map / extend_modes / av1_is_directional_mode | `kModeToAngle` (:451) / `kExtendModes` (:435) / `isDrMode` inline test | exact (hand) | |
| Corner-blend regime (enc_intra_prediction.c:600-604) | in builder + 16/32/64 GPU kernels | exact (hand) | txwpx+txhpx >= 24; live at 16/32/64, dead at 4/8 |
| — | GPU `predict_block_4x4/8x8/16x16/32x32/64x64` | adapted | per-sample arithmetic 1:1 with the host builder (which is exact); launch mapping ours (256 threads at 4/8/16; 1024 threads at 32/64 — 64x64 = FOUR pixels per thread p = t + 1024k, the stated thread-map design); edge-buffer ownership reworked for shared memory; filter-intra path absent at 64x64 (not signalable) |

## l5_motion (src/l5_motion/motion.cpp)

| SVT symbol (file:line) | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| compute8x8_sad_kernel_c (motion_estimation.c:71) | `sad8x8` (:5) | exact (hand) | dedicated 8x8 kernel mirrored; GPU 8x8 kernel adapted from it (accumulators per-thread, warp-reduce) |
| svt_nxm_sad_kernel_helper_c (compute_sad_c.c:21) | `sad4x4` (:95), `sad16x16` (:44), `sad32x32` (:61), `sad64x64` (:78) | exact (hand) | dim loops unrolled to the template dims |
| — | GPU sad 4x4/8x8 kernels | adapted | same sums; 16x16/32x32/64x64 score host-side by design |

## l6_pipeline (src/l6_pipeline/pipeline.cpp)

| SVT symbol | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| av1_tranform_two_d_core_c / quantize / iscan via l3 | `encodeFrameRecon4x4..64x64`, `encodeFrameAuto4x4..64x64`, `encodeFrameAuto4x4Q` + `encodeFrameRecon8x8Q..64x64Q` + `encodeFrameAuto8x8Q..64x64Q` (pipeline.cpp:1195+ for the 32x32 block, :1386 for Auto64x64Q) | adapted | composition: plane window (l1) + buildIntraPredictors (l4) -> int16 residual -> fwdTxfm2d -> invTxfm2dAdd; M1 availability + FR-series REAL recon top-right gather follow SVT's raster decode order; note: there is no encodeFrameRecon4x4Q in the tree (the 4x4 quantized API is encodeFrameAuto4x4Q alone, pipeline.h:58) |
| — | `decideBlockMode4x4/8x8/16x16/32x32/64x64` (decideBlockMode64x64 at :679) | policy | D2 policy is OURS: all 13 PredictionModes scored by SAD (SVT primitives), lowest wins, tie = lowest mode index; SVT selects modes via RD/trellis machinery we do not port |
| — | fixed `defaultScan*` for every block in the Q frame loops | policy | SVT selects scan order per mode/tx-type via get_scan_order; ours is fixed default scan (named in each loop's comment) |
| — | `NeighborContext` (filt_type plumbing) | policy | carries the two neighbor modes the generator shim needs; value semantics match enc_intra_prediction.c:186 |
| kf luma symbol emission via l7 (BSF1) | in `encodeFrameAuto16x16`/`encodeFrameAuto16x16Q` | adapted | per block in raster order: getKfYModeCtx (entropy_coding.c:1004-1021) from the DECIDED neighbor modes + writeKfLumaMode (:1026-1040) + angle delta (directional, delta 0) + writeFilterIntra flag=0 where filterIntraAllowed (mode_decision.c:108-119); allow_update_cdf = 1 forced, ends with odEcStopEncode — see the BSF4-fix coupling invariant (deviations, item 6) |
| token emission via l7 (TS3) | in the 16x16 Q path (`entropy::writeBlockCoeffs`, pipeline.cpp:984-988) | adapted | skip = 0 for all blocks (no skip decision), NA-driven contexts, intra_dir = the decided mode; the ecfrm_* gate lines pin modes AND eobs AND coeffs AND recon AND the byte stream equal to the generator drive |
| per-geometry Q emission (FS3/FS4) | in `encodeFrameAuto4x4Q`/`encodeFrameAuto8x8Q`/`encodeFrameAuto32x32Q`/`encodeFrameAuto64x64Q` (the optional w/fc/na emission params; the per-block walk [partition iff the frame reads one][skip][kf mode + the angle-delta symbol][FI iff the predicate][writeBlockCoeffs]) | adapted | the walk shape mirrors encode_partition_av1 + encode_skip_coeff_av1 + encode_intra_luma_mode_kf_av1 + av1_write_tx_type + the token chain, order per the aom decodeframe.c read order; bit-exact vs the fs2S_* gate lines per size (the 64x64 emission-domain = the TX_64X64 scan contract via quantizeFp64x64Token, n_coeffs=1024 over the compacted 32-wide quadrant, full_loop.c:1262); named deviation: the Auto64x64Q keeps the 4096-wide facade for the legacy/GPU callers, the emission case runs the compacted token domain (the FS5d dual-domain follow-up) |
| running partition contexts (FS5a) | in the 8x8Q/32x32Q/64x64Q emission blocks (updatePartitionContext after each coded block; the 4x4Q/16x16Q emit no partition symbol — named) | adapted | coding_loop.c:1700-1713 semantics via l7; for uniform PARTITION_NONE grids the running ctxs coincide with fresh INVALID cells (the lookup bit at the leaf's own bsl is 0 for every square size — the mutation check proved the fixture discriminates wrong ctxs) |

## l7_entropy (src/l7_entropy/entropy.cpp)

| SVT symbol (file:line) | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| svt_od_ec_enc_* encoder family (bitstream_unit.c:77-408) | `odEcEncReset`/`odEcEncodeBoolEqQ15`/`odEcEncodeBoolQ15`/`odEcEncodeCdfQ15`/`odEcEncDone`/`odEcEncTell`/`odEcEncTellFrac` | exact (hand) | verbatim semantics; named deviations: OD_MEASURE_EC_OVERHEAD #if blocks omitted (upstream 0), EB_UNLIKELY -> plain if, NOINLINE dropped, asserts dropped; two explicit static_cast<int16_t> where SVT's C narrows implicitly |
| OdEcEnc struct (bitstream_unit.h:101-120) | `OdEcEnc` + `OdEcWindow` | exact (hand) | verbatim member layout |
| od_ec_dec family (third_party/aom_dsp entdec.c:78-283) | `OdEcDec`, `odEcDecInit`, refill/normalize, `odEcDecodeBoolQ15`/`odEcDecodeCdfQ15`, `odEcDecTell` | exact (hand) | vendored aom_dsp subtree; explicit casts where SVT's C narrows; see deviation: od_ec_dec_bits_ |
| update_cdf (cabac_context_model.h:76-105) | `updateCdf` | exact (hand) | the one CDF adaptation primitive; rate = 4 + (count>>4) + (nsymbs>3), counter capped at 32 |
| aom_write_symbol (bitstream_unit.h:265-279), aom_stop_encode (:245-253) | `odEcWriteSymbol`/`odEcStopEncode` + `AomWriter` | adapted | nsymbs==2 -> bool(cdf[0]) routing + allow_update_cdf adaptation verbatim; buffer_parent/ownership glue dropped (caller assigns ec.buf) |
| aom_reader family (bitreader.c:14-22, bitreader.h:84-98) | `AomReader`/`odEcReaderInit`/`odEcReadCdf`/`odEcReadSymbol` + `odEcReadBit` (p=16384) | adapted | reader struct shape deviates from the vendored one (see deviations, item 7); ACCT_STR dropped |
| BlockSize enum (definitions.h:883-905), PredictionMode intra subset (:1169-1204), FilterIntraMode (:1295-1302), TxSize/TxType/TxSetType/TxClass/PartitionType/PlaneType | layer enums | exact (hand) | full SVT BlockSize mirrored because filter_intra_cdfs indexes by it; l0_core::BlockSize stays the project-minimal variant |
| svt_aom_get_kf_y_mode_ctx (entropy_coding.c:1004-1021) | `getKfYModeCtx` | exact (hand) | flattened: neighbor modes as explicit args (SVT reads from xd) |
| encode_intra_luma_mode_kf_av1 (:1026-1040) + angle-delta (:1032-1037) | `writeKfLumaMode`/`readKfLumaMode` | exact (hand) | ctx pair passed in; reader returns the decoded mode + raw delta symbol |
| filter-intra pair (:5047-5060) + predicate (mode_decision.c:108-119) | `writeFilterIntra`/`readFilterIntra`/`filterIntraAllowed`(+Bsize) | exact (hand) | CONFIG_ENABLE_FILTER_INTRA=1 resolved from the non-RTC defaults (EbConfigMacros.h:203) |
| default CDF tables (cabac_context_model.c:59, :87, :134-155, :157, :594-596, :614, :618, :801-1860) | `EcFrameContext` tables + `initDefaultEcFrameContext` (COPY_CDF of :740-767 slice; TD5b: the coefficient tables bucket-selected via the getQCtx mirror of :1907-1918) | exact (hand) | kf_y/angle_delta/filter_intra(_mode)/partition/skip tables + intra_ext_tx_cdf whole [3][4][13][17]; the 13 token families carried as the 4-bucket *_buckets.inc files mechanically split from the committed svt_gen.c extracts (no hand-transcribed values), bucket[idx] = get_q_ctx(base_q_idx) per the spec's init_coeff_cdfs |
| svt_aom_partition_cdf_length (entropy_coding.c:922-930), partition_plane_context (:945-960), encode_partition_av1 (:962-978), update_partition_context (coding_loop.c:1700-1713), gather cdfs (cabac_context_model.h:373-405) | `partitionCdfLength`/`partitionPlaneContext`/`writePartition`/`updatePartitionContext`/`partitionGather*` | exact (hand) | forced split writes NOTHING; gathered 2-symbol XOR branches with the temporary's adaptation discarded; INVALID 0xFF -> 0 |
| aom read_partition (decodeframe.c:1266-1293) | `readPartition` | adapted | aom decoder semantics, out-of-tree arbiter (no aom code extracted); gathered reads non-adapting |
| av1_get_skip_context (:983-989) + encode_skip_coeff_av1 (:995-1000) | `getSkipContext`/`writeSkip`/`readSkip` | exact (hand) | read twin = aom read_skip_txfm (decodemv semantics, arbiter); SEG_LVL_SKIP implicit-1 branch deferred with segmentation |
| LUMA DCT_DCT token chain (entropy_coding.c:355-544) | `writeTxbCoeffs`/`readTxbCoeffs` | exact (hand) | writer = svt write path, reader = aom decodetxb.c read_coeffs_txb symbol-for-symbol (incremental lower-level/br contexts, group-start derivation) |
| get_txb_ctx (entropy_coding.c:248-315) | `getTxbCtx` + `DcSignLevelCoeffNa` NA model | adapted | above[64]/left[64] flat uint8, packed (dc_sign << 6 | cul_level), sweep + OR-accumulate; txb_skip_ctx = 0 (whole-block TUs, :298-299); skip_contexts/chroma branches ported but dead for our luma case |
| eb_av1_nz_map_ctx_offset[19] (coefficients.c:24-303) + level helpers (coefficients.h:28-200) | `getNzMag`/`getNzMapCtxFromStats`/`getLowerLevelsCtx`(+Eob)/`getBrCtx`(+Eob)/`getPaddedIdx` + nz_map_ctx_offset.inc | exact (hand) | the full per-position pointer array (an initial hardcoded [TX_SIZES][26] guess was caught by the gate); nz-map is_eob position-only branch verbatim |
| svt_av1_txb_init_levels_c (rd_cost.c:93-105) | `txbInitLevels` | exact (hand) | abs/clamp level init, TX_PAD_2D state |
| svt_aom_get_nz_map_contexts_c (C_DEFAULT/encode_txb_ref_c.c:17-44) | `getNzMapContexts` | exact (hand) | |
| write_golomb (entropy_coding.c:236-243) + aom read_golomb twin | `writeGolomb`/`readGolomb` | exact (hand) | read twin from aom decodetxb.c; gate-enumerated byte-identical over [0,65535] (TD2) |
| tx-type surface (entropy_coding.c:317-353, common_utils.c:195-209, cabac_context_model.c:34-50) | `writeTxType`/`readTxType`/`getExtTxSetType`/`getExtTxSet`/`getExtTxTypes` + ExtTx tables | exact (hand) | reader mirror = aom decodetxb.c av1_read_tx_type; gated by getExtTxTypes > 1 AND base_q_idx > 0; DCT_DCT through eset 2 (DTT4_IDTX, 5 symbols, reduced_tx_set=1 intra) |
| odEcWriteLiteralBit(s) (bitstream_unit.h:255-263) | `odEcWriteLiteralBit`/`odEcWriteLiteralBits` | exact (hand) | |

## l8_bitstream (src/l8_bitstream/bitstream.cpp)

| SVT symbol (file:line) | Port artifact(s) | Status | Notes |
| --- | --- | --- | --- |
| AomWriteBitBuffer (entropy_coding.h:116-119) + wb family (entropy_coding.c:1343-1382) | `AomWriteBitBuffer`, `wbIsByteAligned`/`wbBytesWritten`/`wbWriteBit`/`wbWriteLiteral`/`wbWriteInvSignedLiteral` | exact (hand) | NOINLINE hint dropped (l7 precedent); bodies via the inlined statics :1351-1370 |
| svt_aom_uleb_size_in_bytes + svt_aom_uleb_encode (entropy_coding.c:1313-1341) | `ulebSizeInBytes`/`ulebEncode` | exact (hand) | k_maximum_leb_128 constants folded, verbatim values (16383 = ff 7f measured) |
| write_obu_header (entropy_coding.c:3639-3654) | `writeObuHeader` | adapted | deviation: static in SVT, public here (consumed by tests + BSF3) |
| write_uleb_obu_size (:3656-3666), svt_aom_encode_td_av1 (:3953-3960) | `writeUlebObuSize`/`encodeTdAv1` | exact (hand) | TD return flattened EbErrorType -> 0 (API/EbSvtAv1.h:123); TD = exactly 2 bytes (12 00) |
| ObuType (av1_structs.h:22-32), AomCodecErr (definitions.h:1493-1497) | layer enums | exact (hand) | OK/ERROR slice only (consumed by write_uleb_obu_size) |
| write_sequence_header_obu (:3699-3763) + add_trailing_bits (:3668-3675) | `writeSequenceHeaderObu` | adapted | ratified BSF0(g) field values (profile 0, still_picture=1, monochrome per D1, 32x32 = max dims, filter-intra 1, order_hint 0 per D3, ...); payload only (header + uleb in the packer) |
| write_uncompressed_header_obu (:3294-3637) | `writeFrameHeader`/`writeFrameHeaderV2` | adapted | v1 = the BSF4-fix 22-bit walk (disable_cdf_update = 0 -> might_bwd_adapt region LIVE, refresh_frame_context = DISABLED emitted; 22 bits + 2 pad); v2 = lossy walk (base_q_idx = 100, delta_q block live, encode_loopfilter zeros, tx_mode_select = TX_MODE_LARGEST = 40 bits, 5 bytes, NO padding) |
| write_frame_header_av1 (:3843-3920), encode_sps_av1 (:3925-3948) | `assembleStructuralKeyframeTU`/`assembleStructuralKeyframeTUv2` | adapted | phase-1 measure / phase-2 uleb rewrite (SPS); TG header 0 bytes + tile copy with no per-tile prefix at tile_cnt == 1; v1 dst zeroed first (spec byte_alignment zeros — named); SPS byte-identical between v1/v2 (gate-HALTed if it moves) |
| D1 mono patch spans (entropy_coding.c:2689, :2706-2710, :2385-2386) | generator-side only (composition.c) | policy | court-ratified monochrome still-picture writer: is_monochrome const 0 -> 1, commented spec mono branch live, U/V quantization delta writes skipped (the num_planes guard the pinned writer lacks) — see deviations, item 5 |
| — | committed artifact `src/l8_bitstream/tests/goldens/structural_keyframe.obu` (45 bytes, v2) | policy | produced from the generator output (never hand-typed); three-way identity composed TU == file == gate bytes; v1 bytes remain gated (tu_bytes) |

## GPU runtime (l2_gpurt), infrastructure, and fixtures

- l2_gpurt (NVRTC JIT + driver API): **policy** (our infrastructure; no SVT
  counterpart).
- l0_core (Sample, BlockSize) and l1_pixels (pixels::Plane strided buffer):
  **policy** — infrastructure with no SVT symbol to trace (they exist so the
  layers above can hold SVT-shaped data; the data layout they carry is
  defined by the SVT calls listed above).
- Test/golden fixtures (ramp frames, (11+7i)%251-style edge arrays, corner
  fill 7/127/128 regimes): **policy** - fixtures are ours; the code under
  test is SVT's.
- `svtd_gather_above` / frame drivers in tools/golden_gen/composition.c:
  **adapted** — documented specializations of the SVT composition with the
  get_filt_type shim; the D2 decision inside them mirrors the host policy.

## Known deviations (named, per the standing rule)

1. ADST at TX_64X64: not ported and not signalable — av1_txfm_type_ls[4] =
   {DCT64, INVALID, INVALID, IDENTITY64} (inv_transforms.h:196). The
   IDENTITY64 column is parked. No fadst64/iadst64 exist in this tree.
2. get_filt_type: shimmed (generator-controlled global) — flagged inline in
   svt_gen.c and mirrored by NeighborContext in the port.
3. GPU kernels are thread-mapped re-expressions of the exact host
   arithmetic (adapted), not verbatim C — their bit-exactness is enforced
   by tests against the host paths, and the host paths are gate-enforced
   against the verbatim generator.
4. The composite bench/GPU frame loops run host decisions + per-block
   launches (structure ours); this is a measurement-harness shape, not a
   port of SVT's schedule.
5. D1 monochrome patch (BSF3): the pinned SVT OBU writer is
   hardcoded non-mono; the generator applies three court-ratified spans
   (entropy_coding.c:2689 const 0 -> 1; :2706-2710 the commented spec
   mono branch live; :2385-2386 the U/V quantization delta writes
   skipped — the spec's num_planes guard the pinned writer lacks,
   decodeframe.c:5121-5122). Decoder-confirmed: libdav1d/ffprobe read
   the stream back as gray.
6. THE COUPLING (BSF4-fix): SVT sets ec_writer.allow_update_cdf =
   !disable_cdf_update (ec_process.c:101). Both l6 emission sites hardcode
   allow_update_cdf = 1, correct ONLY because the ratified config
   carries disable_cdf_update = 0 (SVT keyframe default,
   resource_coordination_process.c:360); any future disable_cdf_update
   = 1 config must flip the emission with it, or the stream is
   unspecifiable (the writer adapts CDFs a conformant decoder will not
   replay). Found by the decoder (libdav1d AVERROR_INVALIDDATA), fixed
   as an invariant.
7. l7 `AomReader` struct shape: {ec, allow_update_cdf} (aom-upstream
   shape) vs the vendored bitreader.h aom_reader = {buffer, buffer_end,
   ec, allow_update_cdf} — the buffer-ownership glue is unported
   (harmless in l7's self-consistent use). Found by the TD2 gate harness
   (casting one onto the other crashed 0xC0000005); the harness now
   exchanges bytes/values only (l7_ctx_shim.cpp extern-C shims).
8. `od_ec_dec_bits_` (entdec.h:64) is DECLARED in the pinned tree with
   NO definition anywhere in it (grep-verified) — raw-bits decode is
   unported, not needed by the intra symbol subset.
9. TS1 token-tables deviation — RESOLVED 2026-09-21 (TD5b): the writer
   emitted the default (idx-0) coefficient CDF bucket for every frame
   regardless of base_q_idx. The spec's init_coeff_cdfs
   (07.bitstream.semantics.md:1800-1820; the verbatim get_q_ctx,
   cabac_context_model.c:1907-1918) mandates the bucket by base_q_idx
   (q100 -> idx 2). Named the q100 tile-divergence root cause by the
   TD4 spec arm + the real-dav1d row exhibit (aom token_cdfs.h:861),
   ratified by the court, fixed in TD5a (generator) and TD5b (the l7
   mirror); the TD0 ladder flipped to 7/7 byte-exact and the 47-byte
   artifact decodes content-1:1 (byte-diffs 0/1024).
10. FS-series generator-drive self-consistency traps — both caught by
    the l6 bit-exactness tests, both fixed:
    (a) FS3 (11a0e7e): svtd_fs2_drive omitted the angle-delta symbol
    after a directional kf mode at bsize >= 8x8; its read twin omitted
    it too, so the drive's rt=1 was self-consistent with its own
    omission — the l6 emission (writeKfLumaMode emits the delta,
    entropy_coding.c:1030-1037) diverged on the fs28/fs232/fs264 bytes
    and the regenerated gate exposed it.
    (b) FS5b (a853ac9): the new S==16 drive branch fell through the
    fwd/quant/recon switches into the 4x4 machinery (svtd_fwd2d4x4 etc.
    on a 256-entry block); the fs216 lines were self-consistent (rt 15 1)
    but wrong — the l6 hand-rolled walk diverged at the token chain
    (24 bytes vs 6) and the bit-exactness test caught it.
11. FS5c 4x4-frame structure (687625f): the decoders align frame dims
    to 8 pixels (aligned_width = ALIGN_POWER_OF_TWO(w, 3)), so a 4x4
    frame has a 2x2 mi grid and the 8x8 node READS a 4-symbol partition
    symbol (partition_plane_context bsl=0, the fresh ctx-0 row
    {19132, 25510, 30392}; dav1d 1.5.4 trace-verified). A 4x4 TU exists
    only as a partition leaf; the d4 artifact codes the 8x8 frame as
    [part@8 SPLIT][4x 4x4-TU leaf walks]. A single-4x4-TU tile desyncs
    the real decoder at its first symbol.
12. FS5d (OPEN, named follow-up): the Auto64x64Q carries two domains -
    the legacy (non-emission) callers keep the 4096-wide facade (the
    GPU twin quant_dequant_64x64 is a 4096-position kernel), the
    emission case runs the vendored compacted 1024-position token
    domain. Unification = a 1024-position GPU kernel + bench work;
    for the fs264 fixture the two domains' recons coincide (the
    outer-ring coefficients quantize to 0 at q100).