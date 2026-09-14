# GHV 0.5 + GHA 0.2

**GHV — Goou_Zi High-efficiency Video** (`.ghv`)  
**GHA — Goou_Zi High-efficiency Audio** (`.gha`)

GHV is an experimental, open media format built from scratch for efficient game/video playback. GHV 0.5 is the speed, compression, and playback-stability update. The project does **not** wrap VP8/VP9/AV1/H.264 under a new extension: the current video codec is our own **GHVC4**, and the audio codec is **GHAC1**.

> Status: research/development format. Bitstream compatibility may still change before 1.0.

## What changed in 0.5

- New **GHVC4** video codec and **GBP4** residual packer.
- GBP4 descriptor maps can be raw 4-bit nibbles or RLE, whichever is smaller.
- P-frame residuals can use a reversible byte-delta transform before GBP4 packing.
- Closed-loop reconstruction is used by the encoder, preventing reference drift.
- Very Fast / Fast / Balanced / Quality presets.
- Global-motion search is still available as an experimental option, but is disabled in the default presets: real benchmarks showed the old whole-frame motion model could cost both speed and bytes. GHVC5 will replace it with block motion.
- Native C++17 encoder remains the preferred path.
- New native **GHVC4 decoder** (`ghvdecode`) with CRC verification.
- Player `--engine auto` now prefers native decode + FFmpeg/ffplay presentation for GHVC4. This bypasses Python frame-by-frame rendering and is intended to fix the “video freezes while audio keeps playing” class of starvation problems.
- Python player remains as a compatibility/reference renderer, with explicit decoder-starvation warnings.
- New `ghvverify.py` / `verify.bat`: verifies index structure, frame decode, and CRC so a broken file can be distinguished from a slow player.
- New `ghvbench.py`: repeatable encode + verify benchmark report.
- Native thread option exposed in the GUI/CLI. OpenMP currently matters mainly when experimental motion search is enabled; default GHVC4 packing is intentionally low-overhead.
- Windows/Linux/macOS native build scripts build both encoder and decoder.

## Why 0.5 exists

A real MV benchmark produced:

- MP4: **24.6 MB**
- OGV: **45.2 MB**
- old GVID: **1.41 GB**
- GHV 0.4: **927 MB**

GHV 0.4 was already a large improvement over the first prototype, but it remained far from the target and conversion was too slow. Another real video could also freeze visually while its audio continued. GHV 0.5 specifically attacks those two issues before the next major transform-codec step.

## Internal reference benchmarks

These are development-machine tests, not promises for your PC or your MV.

### Included 160x90 sample

- GHV 0.4: **376,217 bytes**
- GHV 0.5: **339,745 bytes**
- 0.5 is about **9.7% smaller than 0.4** on this already-small sample.
- Versus the old GVID 0.3 sample (716,902 bytes), 0.5 is about **52.6% smaller**.
- decoded-vs-source YUV PSNR: about **48.82 dB**.

### Synthetic 1280x720 / 30 fps codec-core test

- GHVC3 / GHV 0.4: about **61.8 FPS**, **9.31 MiB** video payload/file in the test.
- GHVC4 / GHV 0.5 Balanced: about **62.3 FPS**, **6.43 MiB**.
- In that synthetic test GHVC4 was roughly the same speed/slightly faster and about **31% smaller**.

Real footage can behave very differently. The meaningful benchmark is still the same MV used for 24.6 MB / 45.2 MB / 927 MB comparisons.

## Windows quick start

1. Install Python 3.10+ and FFmpeg.
2. Run `setup_windows.bat` once.
3. The setup script attempts to build `native/bin/ghvcore.exe` and `native/bin/ghvdecode.exe` when MSVC, MinGW-w64, or Clang is available.
4. Open `GHV_Studio.bat`.
5. Start with **Balanced**. Use **Very Fast** for quick iteration.

Native encoder/decoder is strongly recommended. Python/NumPy remains a reference/fallback path.

## Command line

```powershell
python ghvenc.py input.mp4 output.ghv --preset balanced
python ghvplay.py output.ghv
python ghvverify.py output.ghv
python ghvinfo.py output.ghv
python ghvbench.py input.mp4 benchmark.ghv --preset balanced

python ghaenc.py input.wav output.gha --mode hq
python ghaplay.py output.gha
python ghainfo.py output.gha
```

Force playback engine:

```powershell
python ghvplay.py output.ghv --engine native
python ghvplay.py output.ghv --engine python
```

Advanced experimental whole-frame motion search:

```powershell
python ghvenc.py input.mp4 output.ghv --preset balanced --motion-range 4
```

Default presets use motion range 0 because the current global-motion predictor often loses to simple temporal prediction. This is deliberate, not a removed feature.

## Diagnose a frozen-video file

Run:

```powershell
python ghvverify.py problem.ghv
```

- **PASS + native player works:** the `.ghv` is structurally healthy; the old freeze was a Python/display throughput problem.
- **PASS + Python player freezes:** decoder starvation/presentation is the likely bottleneck. Use the native player.
- **FAIL at a specific frame:** this is a format/encoder/file-corruption bug. The failing frame number is useful for reproduction.

## Architecture

```text
MP4 / MKV / MOV / OGV / ...
        |
        | FFmpeg input decode only
        v
      YUV420p
        |
        v
      GHVC4
  scalar quantization
  I / P / repeat prediction
  reversible P residual byte-delta
  GBP4 adaptive block packing
        |
        +---- GHAC1 audio
        |
        v
       .ghv

Playback (preferred)
.ghv -> native ghvdecode -> raw YUV420 -> FFmpeg NUT pipe -> ffplay
                 + GHA/GHAC1 decoded audio --------------------^
```

FFmpeg is currently used as an input decoder and as a convenient presentation/mux layer. It does not encode/decode GHVC4 itself.

## Compatibility direction

The native GHVC4 core is C++17 and has no dependency on libav, zlib, H.264, VP9, AV1, etc. There is no mandatory AVX/AVX2 requirement. That keeps the bitstream core portable to x86-64 and ARM64 and leaves room for a future low-end profile.

The full reference toolchain still uses Python/NumPy and FFmpeg in several places. Removing those runtime dependencies is a milestone, not something 0.5 claims to have finished.

## Next major compression step

GHVC4 is still a predictive pixel-domain codec. It cannot realistically approach H.264/VP9/AV1 or even consistently beat Theora until it stops coding most residual pixels directly.

GHVC5 is planned around:

- 16x16 / 32x32 block motion vectors;
- hierarchical/diamond motion search;
- 8x8 or 4x4 integer transform;
- quality-dependent coefficient quantization;
- zig-zag coefficient ordering;
- run/level + custom entropy coding;
- bounded worker-thread pipeline;
- native decoder library/API instead of a process-only decoder.

See `SPEC_GHV_0.5.md`, `SPEC_GHA_0.2.md`, `CHANGELOG.md`, `ROADMAP.md`, and `PROJECT_STATE.md`.
