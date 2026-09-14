# GHV / GHA Project State

Last updated: 2026-09-14

This is the handoff file for a future ChatGPT/Codex session or human contributor.

## Identity

- GHV = Goou_Zi High-efficiency Video, `.ghv`
- GHA = Goou_Zi High-efficiency Audio, `.gha`
- Current development release: **GHV 0.6 / GHA 0.2**
- Current native video codec: **GHVC6**
- Current audio codec: **GHAC1**
- License: MIT
- Legacy project names GVID / GAUD are retired.

## User real-world benchmark history

Same reference MV, 1080p30:

- MP4: 24.6 MB
- OGV: 45.2 MB
- early GVID: 1.41 GB
- GHV 0.4: 927 MB
- GHV 0.5: **819 MB**
- GHV 0.6: waiting for user retest

User feedback on 0.5:

- conversion of 1080p30 is still too slow;
- large GHV files can stutter;
- one higher-resolution/large file could freeze visually while audio continued;
- a lower-resolution file played smoothly.

This strongly points to HD decoder/presentation throughput in addition to compression inefficiency.

## GHV 0.6 architecture

GHVC6:

- YUV420p;
- scalar source quantization;
- I / P / Repeat frames;
- closed-loop reference reconstruction;
- GBP6 64-byte residual blocks grouped into 256-block independent chunks;
- raw/RLE descriptor maps;
- modes: zero, fixed 1..8-bit, sparse, Rice k0..5;
- block-local byte delta on P residuals;
- optional ZP06 zero-run wrapper;
- CRC32 reconstructed-frame checksum;
- experimental 32x32 GPM6 local block motion.

Important: local motion exists, but normal speed presets currently use range 0. Benchmarks show that the current motion search/prediction does not yet save bytes consistently enough to justify default conversion cost. Do not claim it as solved.

Playback:

- native `ghvdecode` supports GHVC4/5/6;
- OpenMP decode where available;
- producer/consumer frame queue;
- startup prebuffer (about half of configured queue);
- FFmpeg rawvideo input queue;
- stream-copy raw video + PCM audio into NUT;
- ffplay video-master sync;
- Auto playback buffer targets ~64 MiB decoded YUV (8–32 frames);
- native direct mux path removes Python from per-frame packed-video writes;
- zero-motion P fast path avoids motion-grid overhead for normal presets.

Tools:

- `ghvverify.py`: complete decode + CRC verification;
- `ghvdoctor.py`: measures decoder fps **and decoder→FFmpeg pipe** realtime headroom;
- `ghvbench.py`: repeatable encode/verify benchmark;
- `ghvinfo.py`: format inspection;
- `ghvrepair.py`: rebuilds the frame index from intact VFRM records.

## Internal 1080p benchmark

Development machine, 1920x1080, 30 fps, 180 frames, no audio:

- GHV 0.5 / GHVC4: 40.68 MiB, 32.48 native encode fps.
- GHV 0.6 / GHVC6 Balanced: 37.31 MiB, 38.36 native encode fps.
- 0.6 size improvement in this test: ~8.3%.
- 0.6 encoder improvement: ~18%.
- full conversion wall time: ~6.25 s -> ~5.43 s.
- 0.6 native decoder after OpenMP build: roughly 94–100 fps.
- 1080p30 raw YUV420 pipe bandwidth: ~89 MiB/s.
- measured Balanced reconstructed quality on development tests is around the high-40s dB PSNR, but quality is content-dependent.
- later 90-frame 1080p30 direct-mux/zero-motion microbenchmark: ~48.8 core encode fps, ~48.7 end-to-end video-only fps, ~151 native decode fps, ~133 decoder→FFmpeg pipe fps.

Do not present these values as universal hardware performance.

## Immediate next user test

Use the exact MV that produced 819 MB in 0.5:

```text
python ghvenc.py MV.mp4 MV_v06.ghv --preset balanced
python ghvverify.py MV_v06.ghv
python ghvdoctor.py MV_v06.ghv
python ghvplay.py MV_v06.ghv --engine native   # Auto buffer
```

Record:

- final file size;
- average conversion fps;
- Verify PASS/FAIL;
- Doctor decode fps/headroom;
- whether visual freeze still occurs;
- whether 16 vs 24 buffer changes stutter.

Also retest the separate high-resolution file that previously froze while audio continued.

## Next major milestone

Pixel-domain residual packing is reaching diminishing returns. The next compression milestone should be transform-domain **GHVC7**, not another long cycle of nibble/RLE tuning:

1. 8x8 integer transform (start with separable integer/Hadamard-like prototype, measure versus DCT-like transform);
2. coefficient quantization by quality/frequency;
3. zig-zag ordering;
4. zero-run / run-level coding;
5. entropy coding selected by measurements;
6. local motion chosen by actual coded-cost estimate, not SAD alone;
7. bounded multi-threaded encoder pipeline;
8. native decoder library API for Godot/game integration.

The first hard external size target remains OGV/Theora (45.2 MB on the reference MV).
