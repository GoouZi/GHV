# GHV 0.4 + GHA 0.2

**GHV — Goou_Zi High-efficiency Video** (`.ghv`)  
**GHA — Goou_Zi High-efficiency Audio** (`.gha`)

This is the first build after the GVID/GAUD rename, and the first compression-focused GHV revision.

## What changed in 0.4

- New `.ghv` / `.gha` extensions and new file magic. Old files are not merely renamed.
- New **GHVC3** codec.
- Adaptive 64-sample **GBP3** residual bit packing: 0..8-bit blocks plus a sparse-block mode.
- Cheap global-motion prediction for camera movement and pans.
- Repeat-frame representation for exact reconstructed duplicates.
- Native C++17 encoder core retained; vectorized NumPy fallback retained.
- Native encoding now streams records directly into the final `.ghv`; v0.3's giant temporary compressed-video file and second copy pass are gone.
- Three encoder presets: Fast / Balanced / Quality.
- Player now decodes ahead on a worker thread, presents by PTS, can drop late presentation frames, and can recover at the next keyframe after damage.
- CRC32 verification is available with `--verify`.
- GHV/GHA inspectors support `--json`.
- Cross-platform native core build files for Windows and Unix-like systems.

## Quick start on Windows

1. Install Python 3.10+ and FFmpeg.
2. Run `setup_windows.bat` once.
3. Optional but recommended: run `build_native_windows.bat` if you have Visual Studio Build Tools, MinGW-w64, or LLVM/Clang.
4. Open `GHV_Studio.bat`.

The converter will use `native/bin/ghvcore.exe` automatically when available. Without it, the NumPy reference encoder still works.

## Command line

```powershell
python ghvenc.py input.mp4 output.ghv --preset balanced
python ghvplay.py output.ghv
python ghvinfo.py output.ghv

python ghaenc.py input.wav output.gha --mode hq
python ghaplay.py output.gha
python ghainfo.py output.gha
```

`ghvenc.py` uses FFmpeg only as an input decoder to raw YUV420p/PCM. GHVC3 and GHAC1 are implemented by this project.

## Reference test in this build

Using the included 160x90 / 30-frame sample and the Balanced preset:

- previous GVID 0.3 test file: 716,902 bytes
- GHV 0.4 test file: about 376,217 bytes
- reduction versus that 0.3 test: about **47.5%**
- decoded-vs-source YUV PSNR in the included test: about **48.8 dB**
- native GHVC3 codec core on this tiny test: hundreds of frames/s (not representative of 1080p)

This is encouraging, but it does **not** mean the original real-world target has been reached. The user's real MV benchmark was previously MP4 24.6 MB, OGV 45.2 MB, GVID 1.41 GB. That MV should be re-encoded with GHV 0.4 to obtain the meaningful next datapoint.

## Current architecture

```text
MP4 / MKV / OGV / etc.
        |
        | FFmpeg input decode only
        v
     YUV420p
        |
        v
      GHVC3
  quantization
  I/P/repeat prediction
  global motion
  GBP3 adaptive bit packing
        |
        +---- GHAC1 audio
        |
        v
      .ghv
```

## Compatibility direction

The native GHVC3 core is dependency-free C++17 and deliberately uses no mandatory SIMD instruction set, so it can be compiled for x86-64, ARM64 and older CPUs supported by a C++17 compiler. The current Studio and reference player still depend on Python/NumPy/OpenCV/FFmpeg; removing those runtime dependencies is a later milestone, not something 0.4 claims to have solved.

## Next targets

The largest remaining gap is still compression. GHVC3 uses only one global motion vector per frame; a future codec revision should move to block motion vectors, transform-domain residuals, quantized coefficients, and a stronger custom entropy coder. GHAC also needs stereo decorrelation/transform coding before it can compete with mature music codecs at low bitrates.

See `SPEC_GHV_0.4.md`, `SPEC_GHA_0.2.md`, and `MIGRATION_GVID_TO_GHV.md`.
