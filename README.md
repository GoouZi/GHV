# GHV 0.8 + GHA 0.2

**GHV — Goou_Zi High-efficiency Video** (`.ghv`)  
**GHA — Goou_Zi High-efficiency Audio** (`.gha`)

GHV is an experimental open media format built from scratch. It is not H.264/VP9/AV1 hidden behind a custom extension. GHV 0.8 uses our own transform/motion **GHVC8** video codec; embedded/standalone audio uses **GHAC1**. GHVC6/7 remain available and older files remain decodable.

> Development format: bitstream compatibility can still change before 1.0.

## Why 0.7 exists

The fixed Test A 960x544/30 compression benchmark evolved like this:

- MP4: **24.6 MB**
- OGV: **45.2 MB**
- early GVID: **1.41 GB**
- GHV 0.4: **927 MB**
- GHV 0.5: **819 MB** (historical run)
- GHV 0.6: **675.025 MiB** (current-machine rerun)
- GHV 0.7: **443.249 MiB**

Pixel-domain residual packing had reached diminishing returns. GHV 0.7 introduces a real 8x8 integer-transform architecture and cuts both fixed real-video outputs by roughly one third. It also closes a player failure mode where fatal video decode could leave audio running.

## GHV 0.7 highlights

- **GHVC7 / GTC7** native transform codec.
- 8x8 sequency-ordered integer Walsh-Hadamard transform with exact scalar inverse.
- Frequency-, quality-, and chroma-aware coefficient quantization.
- I-frame DC, vertical, and horizontal prediction selected by estimated coded size.
- Closed-loop same-position P prediction and zero-coefficient skip blocks.
- 3-bit block descriptors, zig-zag scan, trailing-zero elimination, zero-run and signed varint levels.
- OpenMP P-block encode/decode with a portable scalar fallback.
- Source-sampled Repeat detection, scene-change I frames, and bounded keyframe intervals.
- `ghvbench.py` can emit human and JSON reports with encode/decode FPS, CRC, PSNR, and SSIM.
- Player supports GHVC7 and monitors decoder/mux/presenter health. Fatal video failure now stops the complete A/V chain.
- `ghvrepair.py`, `ghvverify.py`, `ghvdoctor.py`, and `ghvinfo.py` support the versioned native codecs through GHVC8.

The first GHVC7 profile intentionally does not write motion vectors yet. Motion must be selected by actual coded cost in a future profile; the experimental GHVC6 `GPM6` code remains in-tree.

## Retained GHV 0.6 features

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

## Fixed real-video benchmark

Development machine, Balanced q78 with GHAC1 HQ audio. These are full fixed-video runs, not synthetic clips.

| Test | Codec | Size | Encode | Decode | PSNR | SSIM |
|---|---|---:|---:|---:|---:|---:|
| A 960x544 | GHVC6 | 675.025 MiB | 322.68 fps | 209.2 fps | 50.163 dB | 0.995773 |
| A 960x544 | **GHVC7** | **443.249 MiB** | 162.51 fps | **313.3 fps** | 45.688 dB | 0.985877 |
| B 1920x1080 | GHVC6 | 1350.329 MiB | 100.04 fps | 82.8 fps | 48.374 dB | 0.995662 |
| B 1920x1080 | **GHVC7** | **906.208 MiB** | 48.23 fps | **83.8 fps** | 46.894 dB | 0.991389 |

The latest profile-guided GHVC8 implementation is byte-identical to the first
GHVC8 release but substantially faster:

| Test | GHVC8 size | Encode old -> optimized | Verified decode old -> optimized |
|---|---:|---:|---:|
| A 960x544 | 288.986 MiB | 76.34 -> **140.44 fps** | 283.76 -> **448.17 fps** |
| B 1920x1080 | 553.891 MiB | 25.83 -> **45.46 fps** | 81.34 -> **141.19 fps** |
| C 3840x2160 | 4081.516 MiB | 6.16 -> **9.78 fps** | 18.73 -> **30.21 fps** |

Test C now exceeds realtime decode with CRC verification and completed a full
4K24 controlled playback run with no freeze or audio-speed event. See
`benchmarks/GHVC8_PERFORMANCE_2026-09-15.md`.

The next byte-identical GHVC8 iteration adds exact RD early rejection and
reusable decoder coefficient scratch. A/B/C now encode at **157.94 / 48.80 /
10.46 fps** and fully verified decode at **738.30 / 211.44 / 46.00 fps**.
Output size, SHA-256, PSNR, SSIM, and visual quality are unchanged. See
`benchmarks/GHVC8_PERFORMANCE_ITERATION_2_2026-09-15.md` and
`GODOT_INTEGRATION_NOTES.md`.

GHVC7 reduces Test A by **34.34%** and Test B by **32.89%**. Encoding is about half as fast as GHVC6, while optimized decode is equal or faster. Both Test B outputs completed full 104.118 s native A/V playback without a freeze on this machine. See `benchmarks/GHVC7_BENCHMARK_2026-09-14.md` for method and limitations.

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
python ghvenc.py input.mp4 output.ghv --codec 8 --preset balanced
python ghvplay.py output.ghv              # Auto buffer
python ghvverify.py output.ghv
python ghvdoctor.py output.ghv
python ghvinfo.py output.ghv
python ghvrepair.py output.ghv
python ghvbench.py input.mp4 output.ghv --preset balanced
python ghvbench.py input.mp4 output.ghv --codec 8 --preset balanced --quality-metrics --decode-frames 0 --report-json report.json
python ghvbench.py input.mp4 output.ghv --codec 8 --preset balanced --profile --json
```

For a large 1080p file, Auto is now the default. You can still force a larger cushion:

```powershell
python ghvplay.py output.ghv --engine native --buffer 24
```

Legacy GHVC6 and its experimental block motion can still be selected:

```powershell
python ghvenc.py input.mp4 output.ghv --codec 6 --preset balanced --motion-range 4
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

The normal player path uses the native decoder, a bounded decoded-frame queue, fixed-rate audio output, and an explicitly scheduled renderer. Audio/monotonic time is invariant: early video waits and late video may drop. A fatal decoder error stops both A/V paths. Use `--stats playback.json` for drift, queue, drop, underrun, freeze, and speed telemetry.

## Architecture

```text
MP4 / MKV / MOV / OGV / ...
          |
          | FFmpeg input decode only
          v
       YUV420p
          |
          v
        GHVC8
   I / P / Repeat prediction
   8x8 integer transform
   frequency-aware quantization
   zig-zag + trailing-zero removal
   zero-run + signed varint levels
   packed skip/predictor descriptors
          |
          +------ GHAC1 audio
          |
          v
        .ghv

Preferred playback:
.ghv -> native ghvdecode -> bounded YUV queue -> controlled renderer
          GHAC1 decode -> fixed-rate audio ----^ shared monotonic clock
```

FFmpeg is still used to decode source media during conversion and for the temporary fixed-rate audio output helper. FFmpeg does **not** encode or decode GHVC6/7/8 itself.

## Current target

The first hard target remains **OGV/Theora**, not AV1. GHVC8 has reached the `<300 MiB` Test A stage at nearly unchanged objective quality but is still far from the 45.2 MB historical OGV result.

Next major codec work:

- coded-cost local motion and MV prediction;
- measured Rice/Huffman/range-style coefficient entropy coding;
- encoder allocation and I-frame pipeline optimization;
- CRF-like rate control and preset curves;
- native decoder library/API instead of process-only integration;
- direct engine integrations.

See `SPEC_GHV_0.7.md`, `PROJECT_STATE.md`, `ROADMAP.md`, and `CHANGELOG.md`.
