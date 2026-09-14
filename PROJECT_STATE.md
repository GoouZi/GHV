# GHV / GHA Project State

Last updated: 2026-09-14

This file is intentionally written as a handoff for a future ChatGPT/Codex session or a human contributor.

## Identity

- GHV = Goou_Zi High-efficiency Video, `.ghv`
- GHA = Goou_Zi High-efficiency Audio, `.gha`
- Current video codec: GHVC4
- Current audio codec: GHAC1
- Current development release: GHV 0.5 / GHA 0.2
- License: MIT

Legacy names GVID / GAUD are retired experimental names.

## User's real-world benchmark

Same MV:

- MP4: 24.6 MB
- OGV: 45.2 MB
- early GVID: 1.41 GB
- GHV 0.4: 927 MB
- GHV 0.5: NOT YET TESTED by user at this handoff

GHV 0.4 had minor almost-invisible stutter on the MV. A different video could freeze visually while audio continued.

## Diagnosis of freeze report

There are two distinct possibilities:

1. encoded file is corrupt / decoder bug;
2. Python decoder or OpenCV display starves while independent audio keeps playing.

GHV 0.5 adds `ghvverify.py` and native `ghvdecode --verify` to separate these cases. It also makes native decode + ffplay presentation the default when available.

If `ghvverify.py file.ghv` passes but Python playback freezes, treat it primarily as a player throughput problem. If verify fails, preserve the frame number and reproduce from the source.

## GHV 0.5 architecture

Input media is decoded by FFmpeg to raw YUV420p and PCM. FFmpeg does not encode GHVC/GHAC.

GHVC4:

- scalar Y/C quantization;
- I horizontal prediction;
- temporal P prediction;
- Repeat frame;
- optional experimental global shift motion;
- reversible byte-delta on P residual stream;
- GBP4 64-byte adaptive residual packing;
- raw or RLE descriptor map;
- 0..8-bit block modes + sparse mode;
- reconstructed-frame CRC32;
- closed-loop encoder reference.

GHA/GHAC1:

- exact block anchor;
- per-channel adaptive scale;
- predictive level differences;
- 8-bit HQ or packed 6-bit Compact mode.

## Important benchmark finding

The GHV 0.4 whole-frame global motion search did not reliably help real compression architecture. On synthetic testing, searching motion could make encoding slower and sometimes produce larger residual streams. GHV 0.5 therefore sets motion range 0 in all normal presets and retains non-zero motion only as an experimental CLI option.

Do not spend another release heavily optimizing global motion. Replace it with local block motion in GHVC5.

## Internal GHV 0.5 benchmarks

Included sample:

- GHV 0.4: 376,217 bytes
- GHV 0.5: 339,745 bytes
- improvement: ~9.7%
- source-vs-decoded YUV PSNR: ~48.82 dB

Synthetic 1280x720/30fps codec-core test on development machine:

- GHVC3: ~61.8 FPS / ~9.31 MiB
- GHVC4 Balanced, motion 0: ~62.3 FPS / ~6.43 MiB
- size improvement in that test: ~31%

Do not claim these numbers as universal performance.

## Immediate next user test

Use the exact MV that produced 927 MB with GHV 0.4:

```text
python ghvenc.py MV.mp4 MV_v05.ghv --preset balanced
python ghvverify.py MV_v05.ghv
python ghvplay.py MV_v05.ghv --engine native
```

Record:

- final GHV 0.5 file size;
- average encoding FPS;
- verification PASS/FAIL;
- playback freeze/stutter status;
- CPU usage if convenient.

Also re-encode the video that froze under 0.4, then verify it before playback.

## Next codec milestone: GHVC5

GHVC4 remains a pixel-domain predictive codec. The largest remaining compression win requires transform-domain coding:

1. macroblock/block partitioning;
2. local motion vectors;
3. integer transform;
4. coefficient quantization;
5. zig-zag and zero-run coding;
6. measured entropy coder;
7. multi-threaded block/frame pipeline;
8. native library decoder.

The project's first hard size target is OGV/Theora, not AV1.
