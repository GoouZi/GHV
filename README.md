# GHV 0.6 + GHA 0.2

**GHV — Goou_Zi High-efficiency Video** (`.ghv`)  
**GHA — Goou_Zi High-efficiency Audio** (`.gha`)

GHV is an experimental open media format built from scratch. It is not H.264/VP9/AV1 hidden behind a custom extension. GHV 0.6 uses our own **GHVC6** video codec; embedded/standalone audio uses **GHAC1**.

> Development format: bitstream compatibility can still change before 1.0.

## Why 0.6 exists

The user's real 1080p/30 MV benchmark evolved like this:

- MP4: **24.6 MB**
- OGV: **45.2 MB**
- early GVID: **1.41 GB**
- GHV 0.4: **927 MB**
- GHV 0.5: **819 MB**

GHV 0.5 was a useful improvement, but 1080p conversion was still slow and large files could stutter or visually freeze while audio continued. GHV 0.6 focuses on the HD pipeline: faster encoding, much faster native decode, real prebuffering, better A/V behavior, and another compression pass.

## GHV 0.6 highlights

- **GHVC6** native codec.
- **GBP6** chunked residual format. Each 256-block chunk is independently packable/decodable.
- Parallel native packing/decoding for HD frames through OpenMP when available.
- Adaptive 64-byte residual modes:
  - zero block;
  - fixed 1–8 bit packing;
  - sparse bitmap mode;
  - custom Golomb-Rice modes (`k=0..5`).
- Per-block byte-delta transform on P residuals.
- **ZP06** zero-run wrapper is selected only when it actually makes a frame payload smaller.
- Closed-loop lossy P residual quantization to reduce temporal noise cost.
- Experimental **32x32 local block motion** (`GPM6`). It is implemented but deliberately disabled in speed-oriented presets until it wins more consistently on real footage.
- **Direct native mux**: `ghvcore` writes the final GHV header, VFRM records and index itself. Python no longer receives/copies every packed video frame.
- Zero-motion P frames use a dedicated fast path when motion search is disabled, avoiding a pointless 32x32 motion-map allocation/lookup on every 1080p frame.
- 16 MiB native pipe buffers for HD conversion.
- Native decoder now has a producer/consumer frame queue and a larger startup cushion (half of the configured queue).
- Player adds FFmpeg packet queues and makes **video the master clock**, so audio should no longer run far ahead when the video side has a temporary stall.
- `ghvdoctor.py` / `diagnose.bat` measures both native decode FPS **and the Native→FFmpeg rawvideo handoff**, so a presentation-pipe bottleneck can be separated from a codec bottleneck.
- Playback buffer defaults to **Auto** (~64 MiB of decoded YUV, bounded to 8–32 frames).
- `ghvrepair.py` / `repair.bat` can rebuild a lost/corrupt frame index from intact `VFRM` records.
- `ghvverify.py` still validates the complete decode path and CRCs.

## Internal 1080p benchmark

Development-machine test, 1920x1080, 30 fps, 180 frames, no audio. This is not a promise for every machine or video.

| Build | Size | Native encode | Notes |
|---|---:|---:|---|
| GHV 0.5 / GHVC4 | 40.68 MiB | 32.48 fps | baseline |
| GHV 0.6 / GHVC6 Balanced | 37.31 MiB | 38.36 fps | current |

On this test, 0.6 was about **8.3% smaller** and the native encoder was about **18% faster**. End-to-end conversion wall time improved from about 6.25 s to 5.43 s.

The GHV 0.6 native decoder measured roughly **94–100 fps** on the same 1080p stream, over **3x realtime** for 30 fps playback. The raw YUV420 playback pipe itself is about **89 MiB/s** at 1080p30, which is why native decode and buffering matter so much.

A source-vs-decoded test measured roughly **47 dB PSNR** in the current Balanced path. Real footage will vary.

A second 90-frame 1080p30 pipeline microbenchmark after the zero-motion/direct-mux fast path measured:

- native GHVC6 core: **~48.8 fps**;
- complete video encode path (no audio): **~48.7 fps**;
- native decode: **~151 fps**;
- native decoder → FFmpeg rawvideo sink: **~133 fps**.

This test used the development container and is only a regression reference, not a hardware guarantee.

## Windows quick start

1. Install Python 3.10+ and FFmpeg.
2. Run `setup_windows.bat`.
3. If MSVC / MinGW-w64 / Clang is present, setup builds:
   - `native/bin/ghvcore.exe`
   - `native/bin/ghvdecode.exe`
4. Open `GHV_Studio.bat`.
5. Use **Balanced** for normal testing. Native mode is strongly recommended for 1080p.

## Useful commands

```powershell
python ghvenc.py input.mp4 output.ghv --preset balanced
python ghvplay.py output.ghv              # Auto buffer
python ghvverify.py output.ghv
python ghvdoctor.py output.ghv
python ghvinfo.py output.ghv
python ghvrepair.py output.ghv
python ghvbench.py input.mp4 output.ghv --preset balanced
```

For a large 1080p file, Auto is now the default. You can still force a larger cushion:

```powershell
python ghvplay.py output.ghv --engine native --buffer 24
```

Experimental block motion can be enabled manually:

```powershell
python ghvenc.py input.mp4 output.ghv --preset balanced --motion-range 4
```

At the moment normal presets keep motion search at zero because current local motion search can still cost more bytes than it saves on some footage. The feature stays in-tree for continued work rather than being faked as a finished win.

## Diagnosing “audio continues, picture freezes”

First verify the file:

```powershell
python ghvverify.py problem.ghv
```

Then benchmark the playback decoder:

```powershell
python ghvdoctor.py problem.ghv
```

Interpretation:

- Verify **FAIL**: codec/file bug; keep the failing frame number.
- Verify **PASS**, decoder below ~1.15x realtime: machine/decoder throughput is the likely cause.
- Verify **PASS**, decoder comfortably above realtime but playback still freezes: presentation/mux/player path needs investigation.

GHV 0.6's native player path uses a decoded-frame queue plus FFmpeg input queues, and `ffplay -sync video`, specifically to attack this class of failure.

## Architecture

```text
MP4 / MKV / MOV / OGV / ...
          |
          | FFmpeg input decode only
          v
       YUV420p
          |
          v
        GHVC6
   scalar source quantization
   I / P / Repeat prediction
   optional 32x32 local motion
   closed-loop P residual quantization
   GBP6 chunked adaptive packing
   Rice / fixed-bit / sparse modes
   optional ZP06 zero-run wrapper
          |
          +------ GHAC1 audio
          |
          v
        .ghv

Preferred playback:
.ghv -> native ghvdecode -> auto-buffered raw YUV420 pipe -> FFmpeg NUT copy -> ffplay
                                              GHAC1 audio -----------^
```

FFmpeg is still used as an input decoder and presentation/mux helper. FFmpeg does **not** know how to encode or decode GHVC6 itself.

## Current target

The first hard target remains **OGV/Theora**, not AV1. The big remaining size breakthrough requires transform-domain coding rather than endlessly tuning pixel residual packing.

Next major codec work:

- 8x8 integer transform;
- coefficient quantization;
- zig-zag + zero-run coefficient coding;
- measured entropy coder;
- better local motion selection;
- native decoder library/API instead of process-only integration;
- direct engine integrations.

See `SPEC_GHV_0.6.md`, `PROJECT_STATE.md`, `ROADMAP.md`, and `CHANGELOG.md`.
