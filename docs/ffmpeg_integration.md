# Blueprint: Import the AV1 CUDA encoder as a native FFmpeg encoder

## Context & assumptions

- **End goal:** a new AV1 encoder in FFmpeg (`-c:v av1_gpu`) that runs the CUDA
  pipeline and emits a **decodable AV1 bitstream**.
- **Mechanism:** native FFmpeg CUDA encoder — implement the `AVCodec` inside
  `libavcodec`, using `libavutil/hwcontext_cuda` for device management (the
  `av1_nvenc` model), not an external-library wrapper.
- **Scope:** FFmpeg-integration focused. It **assumes the encoder core is
  completed separately** (quantization, chroma, inter/motion-comp, partition
  tree, entropy coding, OBU/bitstream writer). That core is a hard prerequisite
  and is the bulk of the remaining work.
- **Perf:** correctness-first. Throughput is tracked in a separate benchmark
  pass (AGENTS.md rule), never gating the RED/GREEN loop.
- **Repo reality:** the current port (`l6_pipeline::encodeFrameAuto4x4`) emits
  `coeffs[]`/`modes[]` to host arrays — no bitstream. There is no quantizer, no
  chroma, no inter, no entropy coder. The wiring below starts from the
  *assumed-complete* encoder library, not today's code.

## Prerequisite: the encoder-core C contract FFmpeg will consume

Before any FFmpeg work, the CUDA encoder must expose a stable C ABI (this is the
"library ABI" boundary). FFmpeg's native encoder calls this, not the C++
`pipeline::*`/`gpurt::*` directly.

```c
typedef struct Av1GpuCfg {
    int width, height;            // coded dims (must be multiples of 8/64)
    int fps_num, fps_den;         // timebase
    int bitrate;                  // or crf / qp
    int crf; int qp; int preset;  // preset maps to the D2/D3 policy + mode set
    int gop_size;                 // intra period
} Av1GpuCfg;

typedef struct Av1GpuEnc Av1GpuEnc;

Av1GpuEnc* av1_gpu_enc_create(const Av1GpuCfg* cfg);          // owns CUDA ctx/stream, JIT-compiles kernels once
int        av1_gpu_enc_send_frame(Av1GpuEnc*, const void* y, const void* u, const void* v,
                                  const int strides[3], int64_t pts);
int        av1_gpu_enc_receive_packet(Av1GpuEnc*, uint8_t** buf, size_t* size, int64_t* pts);
void       av1_gpu_enc_destroy(Av1GpuEnc*);
```

Requirements on this core:

- **Decodable AV1 output**: `av1_gpu_enc_receive_packet` must return a complete
  OBU sequence (sequence header + frame header + tile group) that
  `ffmpeg -c:v libdav1d`/`libaom-av1` and `dav1d` decode without error.
- **Low-delay, no B-frames initially** to keep `AV_CODEC_CAP_DELAY`/frame-
  reordering out of the first integration.
- **GPU frames are optional.** Either accept a host YUV plane (upload
  internally) or accept an already-uploaded CUDA buffer (see Phase 1) — FFmpeg
  decides which via pixel-format negotiation.

## Architecture

```
AVFrame (YUV420P or CUDA)                    AVPacket (AV1 OBU)
      │                                             ▲
      ▼                                             │
libavcodec/av1_gpuenc.c  (AVCodec)
      │ send_frame / receive_packet
      ▼
av1_gpu_enc library (C ABI)  ── owns CUcontext + stream ──>
   NVRTC-compiled kernels (predict/subtract/fwd/inv/quant/entropy/…)
```

## Phase 0 — Environment & FFmpeg checkout

- Clone FFmpeg (`n6.1` or current master) into a sibling dir; **do not vendor**
  it into this repo. Pin the tag and record it.
- Decide build toolchain: the project uses MSVC; FFmpeg on Windows builds with
  MSVC or MinGW. Pick one, confirm the CUDA Toolkit 12.2 + NVRTC headers/libs
  are reachable from the FFmpeg build (`-lnvrtc -lcuda -lcudart`).
- License gate: FFmpeg is LGPL/GPL; our port is BSD-3 + AOMedia patent license.
  Document that the encoder must be built as LGPL-compatible (no GPL-only deps)
  so `--enable-av1-gpu` works on an LGPL build.

**Acceptance:** `configure --enable-cuda` builds on this machine.

## Phase 1 — CUDA device/hwcontext reconciliation (biggest integration risk)

Today `gpurt` creates its **own** `CUcontext` (`cuCtxCreate` in a static lambda,
`src/l2_gpurt/gpurt.cpp:21`) and calls `cuCtxSynchronize` per launch
(`src/l2_gpurt/gpurt.cpp:111`). A native FFmpeg encoder gets its context from
`av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_CUDA)` → `AVCUDAContext->cuda_ctx`.
Two live contexts on the same device will clash.

- Add a mode to `Av1GpuEnc` to **adopt** an existing `CUcontext` (FFmpeg's)
  instead of creating one, and route launches through FFmpeg's stream
  (`AVCUDAContext->stream`).
- Remove the per-launch `cuCtxSynchronize` for the async encode path (keep it
  only for correctness tests), and rely on `avcodec`'s `AVFrame` refcount + an
  explicit `cuStreamSynchronize`/`av_hwframe` sync at packet boundaries.
- NVRTC JIT compile **once per encode session** at `av1_gpu_enc_create`; cache
  the PTX/module so the first frame isn't a ~100 ms JIT stall.

**Acceptance:** an encode session uses FFmpeg's `CUcontext`/stream, no "multiple
contexts" error, no memory leak across frame submissions.

## Phase 2 — `libavcodec/av1_gpuenc.c`

Implement the `AVCodec`:

```c
AVCodec ff_av1_gpu_encoder = {
    .name = "av1_gpu",
    .long_name = "AV1 (CUDA GPU encoder)",
    .type = AVMEDIA_TYPE_VIDEO,
    .id   = AV_CODEC_ID_AV1,
    .init = av1_gpu_init,
    .send_frame = av1_gpu_send_frame,
    .receive_packet = av1_gpu_receive_packet,
    .close = av1_gpu_close,
    .pix_fmts = { AV_PIX_FMT_CUDA, AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
    .capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_HARDWARE
                    | AV_CODEC_CAP_ENCODER_FLUSH,
    .hw_configs = (const AVCodecHWConfig[]){ ... },
    .priv_data_size = sizeof(Av1GpuEncCtx),
    .priv_class = &av1_gpu_class,   // AVOption: crf, qp, preset, gop, bitrate
    .defaults = ...,
};
```

- `Av1GpuEncCtx` holds `AVHWDeviceContext*` + the `Av1GpuEnc*` handle + the
  `AVCodecContext` mapping (timebase, framerate, dimensions).
- `av1_gpu_init`: read `AVOption`s → `Av1GpuCfg`, create the CUDA hwdevice
  context if none supplied, call `av1_gpu_enc_create`.
- `av1_gpu_send_frame`: convert frame to the expected pixel format, hand the
  data (host or `CUdeviceptr`) to `av1_gpu_enc_send_frame`.
- `av1_gpu_receive_packet`: wrap the returned OBU bytes in an `AVPacket`, set
  `pts`, `duration`, `flags` (keyframe detection for the first/intra frame).
- `av1_gpu_close`: destroy the encoder handle, release the hwdevice context.

**Acceptance:** `ffmpeg -encoders | grep av1_gpu` shows it; a trivial encode of a
tiny YUV4MPEG file runs without crash.

## Phase 3 — Build integration in FFmpeg

- `libavcodec/Makefile`: add `av1_gpuenc.o` to `OBJS` (guarded by the new config
  flag).
- `libavcodec/allcodecs.c`: add `extern AVCodec ff_av1_gpu_encoder;` and
  `REGISTER_ENCODER(AV1_GPU, av1_gpu)` in the AV1 section.
- `configure`: add `--enable-av1-gpu` (and reuse `--enable-cuda`). Add a
  `check_lib`/`require` block for `nvrtc` + `cuda` + `cudart`, and set the
  include/lib dirs so the Windows CUDA 12.2 install is found (CUDA `include/`,
  `lib/x64/`).
- Keep it **disabled by default**; `--enable-av1-gpu` opt-in.

**Acceptance:** `./configure --enable-av1-gpu && make` produces a binary with the
encoder, on Windows.

## Phase 4 — Pixel format & frame-handling path

- Advertise `AV_PIX_FMT_CUDA` and `AV_PIX_FMT_YUV420P` in `pix_fmts`; set
  `hw_configs` with `AV_CODEC_HW_CONFIG_METHOD_AD_HOC`/
  `AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX`.
- If FFmpeg picks `AV_PIX_FMT_CUDA`, frames arrive as `AV_HWFRAME` and you pass
  `CUdeviceptr`s directly to the library (zero-copy). If it picks
  `AV_PIX_FMT_YUV420P`, upload in `send_frame`.
- Handle dimension alignment (SVT/AV1 require multiples of 8/64) — pad/reflect
  in the encoder library, and set `AVCodecContext->width/height` to the padded
  dims so output framing is correct.
- Threading: start single-threaded (`FF_THREAD_SLICE` disabled); the GPU is the
  bottleneck. Document that slice threading must not split a frame mid-raster
  (M1 neighbor dependency).

**Acceptance:** both `AV_PIX_FMT_CUDA` (via `-hwaccel cuda` / `-vf hwupload`) and
plain `AV_PIX_FMT_YUV420P` paths produce identical packets.

## Phase 5 — Validation harness (correctness-first)

Follow the repo's `SKIP:` convention — no GPU ⇒ skip, not fail.

- **Decode validation (the gate):** encode a known YUV, pipe to
  `ffmpeg -c:v libdav1d -f null -`, assert decode exit 0 and frame count/size
  match. Also decode with `libaom-av1` for cross-decoder confidence.
- **Signal check:** decode the same input with `libsvtav1`/`libaom-av1` and
  compare PSNR/SSIM — loose bounds only (the mode-decision policy is this
  project's, not SVT's, so **no bit-exactness vs. any reference**; that 1:1
  guarantee is at the *primitive* level).
- **Hardware-frame test:** run the CUDA-frame path (`-hwaccel cuda`) and the
  software-frame path; assert identical packets.
- **Golden where possible:** reuse the committed `tools/golden_gen` primitives
  for the GPU-block sub-stages (predict/subtract/fwd/inv), but only at the block
  level — the frame-level policy is ours.
- Add these as doctest-style integration tests in the FFmpeg wrapper or as a
  CMake/ctest target that shells out to ffmpeg, so they run in CI where a GPU
  exists.

**Acceptance:** a reproducible command that encodes a clip and decodes it
error-free; a `SKIP:` note on GPU-less machines.

## Phase 6 — Performance (correctness-first, separate pass)

- AGENTS.md rule: correctness-green ≠ performance-acceptable, but throughput is
  **not** gated per slice.
- At slice-group boundaries, run a benchmark pass (e.g. `ffmpeg -benchmark` on a
  1080p clip): record fps, and capture `ptxas -v` register/spill for any new
  kernel.
- Known hot spots to optimize later (not in the initial wiring): per-block
  host↔device round trips in `encodeFrameAuto4x4` (the test does a download per
  block, `src/l6_pipeline/tests/test_pipeline.cpp:507`), per-launch
  `cuCtxSynchronize`, and JIT compile at startup.

## Risks & open questions

1. **Encoder-core completion is the critical path.** There is no quantizer,
   chroma, inter, entropy coder, or OBU writer today. "A month around the clock"
   realistically buys the core, not the FFmpeg layer — the wiring here is the
   *last* ~2 weeks.
2. **Context ownership.** `gpurt` creates its own `CUcontext`; it must adopt
   FFmpeg's. This touches every kernel path and is the most likely source of
   subtle breakage.
3. **Windows FFmpeg build.** MSVC+CMake FFmpeg is doable but fiddly (CUDA lib
   search, `--extra-cflags/ldflags`). Confirm the exact toolchain before Phase 2.
4. **License.** Ensure the encoder stays LGPL-compatible for an LGPL FFmpeg
   build; document the AOMedia patent license.
5. **Threading/refcount.** Frame lifecycle and `AVFrame` reuse must be reconciled
   with async GPU work; start single-threaded.
6. **The `AVOption` surface** (crf/qp/preset/bitrate/gop) needs defining —
   decide early what maps to the D2/D3 policy and what is ignored.
