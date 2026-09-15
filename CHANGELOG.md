# Changelog

## Unreleased — GHVC8 Native Performance

- Froze the verified byte-identical GHVC8 milestone at annotated tag
  `ghv-0.8-stable-perf2`, backup branch, verified bundle, and build-tested ZIP.
- Added a C++17 `libghv` decoder foundation with metadata, shared YUV420 frame
  ownership, 64-bit indexed seek, structured errors, and native GHAC1 decode.
- Added GHV Player 0.1 for Windows using Win32, D3D11 planar YUV rendering, a
  WASAPI audio master clock, bounded decode queues, native seek/pause/replay,
  normal player controls, and JSON acceptance telemetry.
- Added a portable Windows x64 packaging script and package smoke test; normal
  playback no longer requires Python, FFmpeg, ffplay, or OpenCV.
- Native Player Test B completed 3/3 with zero drops/freezes/audio anomalies;
  Test C completed 2/2 with eight late drops and no freeze/audio anomaly.
- Added native PlayerCore/WASAPI acceptance coverage for sequential
  10/25/50/75/90 percent indexed seeks on Tests B and C.
- Rejected the 20-worker playback policy after it consumed 69–82% whole-machine
  CPU; the measured eight-worker cap uses about 31–35% on full B/C playback.

- Added exact branch-and-bound GHVC8 RD candidate rejection without changing
  mode decisions, output bytes, or reconstructed pixels.
- Reused thread-local coefficient/motion scratch and added zero/DC-only exact
  inverse-transform fast paths.
- Added RD candidate/completion/rejection counters and zero/DC decode counters.
- Tested and rejected slower cheap-SAD ordering and narrow SSE2 kernels instead
  of retaining benchmark regressions.
- Improved A/B/C encode to 155.223/48.795/10.464 fps and verified decode to
  670.845/211.439/45.997 fps; all output SHA-256 values remain identical.
- Added late-drop-specific player telemetry and Godot/libghv integration notes.
- Added `--profile` encode/decode stage timing and structured benchmark capture with CPU/core/GPU inventory.
- Removed per-macroblock motion-search heap allocation and cached per-frame quantization tables.
- Removed the duplicate selected-block forward transform while preserving byte-identical GHVC8 output.
- Added fast interior prediction and zero-residual reconstruction paths.
- Added intra dependency-wavefront reconstruction and a shared slicing-by-8 IEEE CRC-32 implementation.
- Preserved GHVC4-8 decode compatibility; all versioned self-tests pass.
- Test A encode improved 76.338 -> **140.443 fps** and verified decode 283.756 -> **448.172 fps**.
- Test B encode improved 25.826 -> **45.464 fps** and verified decode 81.341 -> **141.190 fps**; playback remained stable 3/3.
- Test C encode improved 6.162 -> **9.777 fps** and verified decode 18.728 -> **30.213 fps**, enabling a full stable 4K playback pass.
- A/B/C output bytes, PSNR, SSIM, and SHA-256 remain exactly unchanged from the prior GHVC8 milestone.

## GHV 0.8 / GHA 0.2 — RD Motion and Stable Audio Clock

- Added native GHVC8/GTP8 with 16x16 local motion compensation.
- Added left/top/top-right component-median MV prediction and signed delta coding.
- Added quality-first rate-distortion mode selection over four luma and two chroma transform blocks.
- Reduced P-frame descriptors to a one-bit SKIP map; zero residual blocks write no coefficients.
- Added single-byte compact run/level tokens with a full-range varint escape. This entropy change does not alter reconstructed pixels.
- Parallelized independent motion searches and transform/reconstruction work with scalar fallback.
- Replaced interleaved raw-YUV NUT playback after reproducing audio starvation, pitch drop, slowdown, and catch-up behavior.
- Added a controlled native-decoder renderer with bounded queues, fixed-rate audio, monotonic scheduling, and explicit late-video dropping.
- Added `ghvplay --stats [JSON]` telemetry and `ghvbench` encode/decode peak-memory reporting.
- Added fixed 10/25/50/75/90 percent visual comparison extraction with `ghvframes.py`.
- Added `SPEC_GHV_0.8.md` and retained `SPEC_GHV_0.7.md` unchanged.
- Fixed Test A: 443.249 MiB GHVC7 -> **288.986 MiB GHVC8 (-34.80%)**, 45.421 dB / 0.984480.
- Fixed Test B: 906.208 MiB GHVC7 -> **553.891 MiB GHVC8 (-38.88%)**, 46.671 dB / 0.990330.
- Controlled Test B playback completed three consecutive runs with all 3121 frames shown, zero drops, zero freezes, and zero clock-speed events.

## GHV 0.7 / GHA 0.2 — Transform Compression

- Added native **GHVC7 / GTC7**, an in-tree 8x8 integer transform codec.
- Added frequency-, quality-, and chroma-aware quantization.
- Added I-frame DC, vertical, and horizontal block prediction selected by coded size.
- Added closed-loop same-position P prediction and zero-coefficient skip blocks.
- Added packed 3-bit block descriptors, zig-zag scan, trailing-zero removal, zero-run and signed varint coefficient coding.
- Added source-sampled Repeat detection so lossy reference drift does not hide repeated source frames.
- Parallelized GHVC7 P-block forward and inverse transforms with OpenMP while retaining scalar C++17 fallback.
- Added `--codec 6|7`; GHVC6 remains encodable and GHVC4/5/6 remain decodable.
- Native decoder, player, verifier, doctor, info, and repair paths now accept GHVC7.
- Player now monitors decoder, FFmpeg mux, and ffplay concurrently. Fatal video/mux failure stops the complete A/V chain instead of allowing audio to continue with a frozen picture.
- Upgraded `ghvbench.py` with codec selection, safe Windows console relay, source metadata, decode benchmarking, optional full-file PSNR/SSIM, and saved JSON reports.
- Added `tests/selftest_v07.py`; retained and explicitly pinned the v0.6 regression test to GHVC6.
- Fixed multi-configuration CMake output paths so native executables land in `native/bin`.
- Added `SPEC_GHV_0.7.md` and fixed-video benchmark reports.
- Fixed Test A result: 675.025 MiB GHVC6 -> **443.249 MiB GHVC7 (-34.34%)**.
- Fixed Test B result: 1350.329 MiB GHVC6 -> **906.208 MiB GHVC7 (-32.89%)**.
- Test B GHVC7 native decode reached 83.8 fps / 83.5 fps through the FFmpeg pipe, about 2.8x realtime.
- Both Test B outputs completed full 104.118 s A/V playback without a freeze on the development machine.

## GHV 0.6 / GHA 0.2 — HD Pipeline Update

- Native **direct mux** path: ghvcore can write the final `.ghv` video stream/index/header itself; Python only appends audio and patches fixed header fields.
- Added zero-motion P-frame fast path for normal presets; removes unnecessary motion-grid work when search range is 0.
- Python GHVC6 reference decoder fixed/expanded for GBP6 Rice, ZP06 and zero-motion P payloads.
- Playback buffer now supports Auto sizing (~64 MiB decoded cushion, bounded 8–32 frames).
- Native decoder startup prebuffer now fills roughly half of the configured queue.
- `ghvdoctor.py` now benchmarks the complete native-decoder → FFmpeg rawvideo handoff, not only codec decode.
- Added `ghvrepair.py` / `repair.bat` and Studio **Repair Index** action.
- Added `compact` encoder preset.
- Added `tests/selftest_v06.py` covering direct encode, Python/native decode, CRC, GHA and index repair.
- GHVC6 introduced.
- GBP6 replaces the default GHVC residual payload:
  - 256-block independent chunks;
  - OpenMP-friendly encode/decode;
  - zero, fixed 1–8 bit, sparse and custom Rice modes;
  - block-local byte delta for P residuals.
- Added ZP06 optional zero-run wrapper; kept only when smaller.
- Added closed-loop residual dead-zone/step quantization.
- Added experimental GPM6 32x32 local block-motion payload.
- Speed presets default motion search to zero after benchmarks showed that motion search is not yet a universal win.
- Increased native FFmpeg/core pipe buffers for 1080p.
- Native decoder now supports GHVC4/5/6 and uses a producer/consumer decoded-frame queue.
- Added real startup frame prebuffer.
- Native playback mux uses input queues and stream-copy into NUT.
- ffplay now uses video master clock to prevent audio racing ahead during a video stall.
- Added `ghvdoctor.py` / `diagnose.bat` for decode-speed and realtime-headroom diagnostics.
- Studio exposes playback-buffer size and Diagnose action.
- Native decoder now builds with OpenMP when available on Windows/Linux/macOS.
- Container minor version bumped to 0.6.

Real user benchmark entering this release: GHV 0.5 reduced the reference 1080p30 MV from 927 MB (0.4) to **819 MB**.

Internal 1080p180-frame benchmark: 0.5 40.68 MiB / 32.48 fps -> 0.6 37.31 MiB / 38.36 fps; native decode around 94–100 fps after OpenMP build.

## GHV 0.5 / GHA 0.2

- GHVC4 / GBP4.
- Descriptor RLE and P byte delta.
- Native decoder + native-first playback.
- Verify and benchmark utilities.
- Default global motion disabled after poor cost/benefit in tests.

## GHV 0.4 / GHA 0.2

- GVID/GAUD renamed to GHV/GHA.
- GHVC3/GBP3.
- Native streaming encoder path.
- First substantial compression update.
