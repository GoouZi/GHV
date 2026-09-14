# GHV / GHA Roadmap

Goal: an open, efficient, compact, good-looking media format that can become a practical OGV/Theora successor for game/media use, with a decoder simple enough to port widely.

## 0.5 — current: speed + stability bridge

- GHVC4 / GBP4 descriptor compression and residual preprocessing.
- native encoder + native decoder executable.
- native-first playback presentation path.
- file verifier and benchmark tool.
- default path avoids global-motion search that was not paying for itself.

## 0.6 — GHVC5 transform compression

This is the next major file-size step.

- 16x16 / 32x32 block motion vectors.
- hierarchical / diamond / early-exit motion search.
- intra block predictors.
- 8x8 or 4x4 integer transform (DCT-like but specified exactly with integer arithmetic).
- luma/chroma quantization matrices.
- zig-zag coefficient order.
- zero-run + run/level representation.
- custom canonical Huffman or range/rANS-style entropy stage after measurement.
- bounded multi-threaded frame/block worker pipeline.
- rate-control experiments (target quality first, target bitrate later).

## 0.7 — native runtime

- `libghv` decoder API instead of process-only `ghvdecode`.
- standalone native player with no Python/OpenCV runtime requirement.
- audio-clock-driven A/V synchronization.
- native GHAC decoder path.
- seek without decoding from frame zero.
- bounded-memory streaming/index reader.
- corrupted-file recovery and fuzz tests.

## 0.8 — modern container features

Only after compression/playback fundamentals are solid:

- metadata tags and cover/thumbnail chunks;
- subtitles/captions;
- multiple audio tracks;
- chapters;
- alpha-video profile experiment;
- streaming-friendly index placement;
- color metadata (matrix/range/transfer/primaries);
- optional 10-bit profile research.

## 0.9 — compatibility

- Windows x86-64 / ARM64.
- Linux x86-64 / ARM64.
- macOS Intel / Apple Silicon.
- Android/ARM experiment.
- lightweight decode profile for older/low-end hardware.
- Godot integration / game-engine API experiments.

## 1.0 target

- Beat OGV/Theora on the project benchmark in size at comparable visual quality.
- Stable A/V playback and seeking.
- Fully documented public bitstream.
- C/C++ decoder implementation with clear portability profile.
- Backwards-compatibility policy for 1.x.

Beating H.264/AV1 immediately is not a 1.0 requirement. First target: become meaningfully better than the project's OGV baseline while staying open and practical.
