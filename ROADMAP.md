# GHV / GHA Roadmap

## Current — GHV 0.9 / GHVC9 Milestone 1

- Predictor-ranked motion shortlist; Balanced sends zero plus one nonzero
  finalist to exact RD instead of three nonzero candidates.
- `GBP9` P frames with independently decodable 256-block coefficient chunks,
  capped-unary zero runs, and signed Rice `k=2` levels.
- Test A: 210.616 MiB, 169.317 encode / 913.114 decode fps,
  45.356 dB / 0.984213, fixed-position visual PASS.
- Test B: 405.317 MiB, 52.810 encode / 279.601 decode fps, full playback PASS.
- Test C: 3062.501 MiB, 11.196 encode / 59.362 decode fps (2.473x realtime),
  full playback PASS.
- GHVC4-8 decode compatibility, 64-bit index, CRC, seek, EOF, and safe corrupt
  chunk rejection retained.
- Codec work is the active priority; Godot, MovieWriter, UI, VLC/PotPlayer, and
  other ecosystem work is intentionally paused.

## Frozen — GHV 0.8

- GHVC8/GTP8 16x16 local motion with RD selection.
- Median MV prediction and delta-coded vectors.
- Explicit zero-residual SKIP map and compact one-byte run/level classes.
- Audio-master/monotonic controlled renderer with bounded queues and JSON telemetry.
- Test A 288.986 MiB at 45.421 dB / 0.984480.
- Test B 553.891 MiB at 46.671 dB / 0.990330; three controlled playback passes.
- Profile-guided native milestone: A/B/C encode 140.44/45.46/9.78 fps and
  verified decode 448.17/141.19/30.21 fps with byte-identical output.
- Second byte-identical iteration: A/B/C encode 155.22/48.80/10.46 fps and
  verified decode 670.85/211.44/46.00 fps; Test C is 1.92x realtime.
- Test C now completes controlled 4K24 playback without freeze or audio-speed events.
- Frozen stable commit/tag/branch plus verified Git bundle and source snapshot.
- GHV Player 0.1 for Windows: native `libghv`, D3D11, WASAPI, Win32 UI,
  bounded queues, indexed seek, portable package, and no Python/FFmpeg runtime.
- Formal native-player playback: Test B 3/3 with zero drops; Test C 2/2 with
  eight late drops each and zero freeze/audio-rate anomaly.

## Retained — GHV 0.7

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

## Deferred — ecosystem and libghv expansion

- versioned opaque C decoder handles and ABI/ownership contract;
- incremental GHAC decode and reusable frame/audio pools;
- direct YUV420 + PCM encoder push/finalize API;
- generated-GHV-without-MP4 test/demo;
- manual packaged-player UI stress for drag/drop, seek, resize, and fullscreen;
- platform backends after Windows; do not claim untested macOS/mobile support.

These are deliberately paused until compression and bitstream structure mature:

- Godot GDExtension / MovieWriter;
- player UI rewrite and additional platform backends;
- VLC, PotPlayer, mobile, and other ecosystem integrations;
- stable public C encoder ABI.

## Next — GHVC9 Milestone 2

Primary objective: push Test A below 180 MiB without losing current visual
quality, faster-than-GHVC8 encoding, or 2x-realtime 4K24 decode.

Implemented foundations and continuing research:

- adaptive 32x32/16x16 motion partition and merge-like MV reuse;
- cheaper predictor-first/early-accept bounds before exact RD;
- measured DC/context coefficient coding with strict rate-vs-CPU acceptance;
- batched transform/SAD SIMD only when microbenchmarks and real files both win;
- CRF-like quality/rate control and complete preset curves;
- benchmark A first, then B, then C only for accepted milestones;
- expand malformed bitstream and fuzz-like tests.

Target: preserve the current ~45-47 dB / >=0.98 SSIM class while producing
another architecture-level rate reduction.

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
