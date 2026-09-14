# Roadmap

The project goal is still: an open, efficient, small, good-looking media format that can become a practical OGV/Theora successor for game/media use.

## 0.5 — motion + transform

Primary goal: attack real-world MV size, not add decorative container features.

- 16x16 / 32x32 block motion vectors instead of one global vector per frame.
- fast hierarchical motion search.
- 8x8 integer transform / DCT-like residual transform.
- quality-dependent coefficient quantization.
- zig-zag coefficient scanning and a stronger custom entropy coder.
- multi-threaded native encoder.

## 0.6 — playback/runtime

- native C/C++ decoder library (`libghv`).
- native player with no Python/OpenCV runtime requirement.
- audio-clock-driven A/V sync.
- streaming seek/index improvements and bounded-memory audio decode.
- fuzzing and corrupt-file tests.

## 0.7 — compatibility

- Windows x86-64 / ARM64.
- Linux x86-64 / ARM64.
- macOS Intel / Apple Silicon.
- Android/ARM experiment.
- lightweight decode profile intended for older/low-end hardware.

## 1.0 target

Beat OGV/Theora on the project benchmark in size at comparable visual quality, while keeping decoding substantially simpler than AV1 and retaining an openly documented bitstream.
