# GHV / GHA Roadmap

## Current — GHV 0.7

- GHVC7 / GTC7 transform-domain codec.
- 8x8 integer WHT, frequency-aware quantization, zig-zag, zero-run and varint levels.
- I-frame DC/vertical/horizontal prediction and temporal P/skip blocks.
- Parallel P-block encode/decode with scalar fallback.
- Fixed Test A: 675.025 MiB -> 443.249 MiB (-34.34%).
- Fixed Test B: 1350.329 MiB -> 906.208 MiB (-32.89%).
- Player fatal-video monitoring stops the whole A/V chain.
- Repeatable human + JSON benchmark with optional PSNR/SSIM.

## Retained — GHV 0.6

- GHVC6 / GBP6 chunked residual codec.
- Native HD encoder/decoder.
- Rice + fixed-bit + sparse residual coding.
- ZP06 zero-run wrapper.
- Buffered native playback and playback diagnostics.
- Experimental local block motion.

## GHV 0.8 — Motion, Entropy, Native Runtime

Primary objective: push Test A below 250 MiB without losing comfortable realtime decode.

Planned research:

- coded-cost 8/16/32-block local motion and MV prediction;
- Rice/canonical Huffman/range-style coefficient coding selected by measurements;
- encoder buffer reuse, persistent workers, and I-frame pipeline optimization;
- CRF-like quality/rate control and complete preset curves;
- benchmark against both fixed real videos every build;
- `libghv` decoder API;
- no Python in normal playback;
- direct audio/video synchronization in native runtime;
- seek/index API and frame callbacks / texture upload path;
- Godot extension prototype.

Target: preserve at least the current ~46 dB real-video PSNR class while producing another architecture-level rate reduction.

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
