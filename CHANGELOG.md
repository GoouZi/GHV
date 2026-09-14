# Changelog

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
