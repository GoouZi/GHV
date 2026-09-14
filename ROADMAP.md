# GHV / GHA Roadmap

## Current — GHV 0.6

- GHVC6 / GBP6 chunked residual codec.
- Native HD encoder/decoder.
- Rice + fixed-bit + sparse residual coding.
- ZP06 zero-run wrapper.
- Buffered native playback and playback diagnostics.
- Experimental local block motion.

## GHV 0.7 — Transform Compression

Primary objective: **large file-size reduction**, while keeping 1080p30 decoding comfortably realtime.

Planned research:

- 8x8 integer transform prototype;
- frequency-aware coefficient quantization;
- zig-zag scan;
- zero-run/run-level coefficients;
- coded-cost block mode selection;
- improved motion vectors only when they reduce final bits;
- multi-thread transform/block worker pool;
- benchmark against the fixed reference MV every build.

Target: move the reference MV from hundreds of MB toward low hundreds of MB without obvious quality collapse.

## GHV 0.8 — Native Runtime

- `libghv` decoder API;
- no Python in normal playback;
- direct audio/video synchronization in native runtime;
- seek/index API;
- frame callbacks / texture upload path;
- Godot extension prototype;
- Windows/Linux/macOS x86-64 and ARM64 validation.

## GHV 0.9 — Container/Modern Features

- metadata;
- chapters;
- multiple audio tracks;
- subtitles;
- alpha/profile experiments;
- streaming-friendly index/chunks;
- damage recovery and stronger fuzz testing.

## GHV 1.0 target

- stable public bitstream/container spec;
- stable decoder library;
- reliable cross-platform playback;
- clearly measured quality/size/speed tradeoffs;
- target OGV/Theora replacement use case first, not AV1 replacement claims.

## Completed in 0.6 HD pipeline

- Direct native GHV video mux (Python removed from per-frame packed-video copy path).
- Zero-motion fast P path.
- Auto HD playback buffering and full pipe diagnostics.
- Index repair utility.
