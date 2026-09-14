# Changelog

## GHV 0.5 / GHA 0.2 — Speed, compression & playback stability

- Introduced **GHVC4** and **GBP4**.
- Added RLE-compressed GBP4 descriptor maps.
- Added reversible byte-delta transform for P residuals.
- Kept closed-loop reconstruction to prevent encoder/decoder reference drift.
- Added Very Fast preset and native thread control.
- Disabled expensive global-motion search in normal presets after benchmarks showed it could make current GHVC4 both slower and larger; retained `--motion-range` as an experimental override.
- Added native C++17 GHVC4 decoder (`ghvdecode`).
- Added native-first playback path to avoid Python/OpenCV frame starvation.
- Added `ghvverify.py` / `verify.bat` for index/decode/CRC validation.
- Added explicit Python-player starvation diagnostics.
- Added `ghvbench.py` for repeatable encode + verify tests.
- Build scripts now produce both native encoder and decoder on Windows/Linux/macOS.
- Retained GHVC3 decoding in the Python reference player for GHV 0.4 compatibility.

Development benchmarks:

- included sample: 376,217 bytes (GHV 0.4) -> 339,745 bytes (GHV 0.5), ~9.7% smaller;
- sample YUV PSNR remains ~48.82 dB;
- synthetic 1280x720/30 test: ~9.31 MiB -> ~6.43 MiB, ~31% smaller, while native codec-core speed stayed around 62 FPS on the development machine.

Real-world baseline awaiting GHV 0.5 retest: MP4 24.6 MB / OGV 45.2 MB / GHV 0.4 927 MB.

## GHV 0.4 / GHA 0.2 — Compression & rename update

- Renamed GVID -> GHV (`.ghv`) and GAUD -> GHA (`.gha`).
- Introduced GHVC3 + GBP3 adaptive residual packing.
- Added sparse residual blocks and global-motion prediction.
- Added repeat-frame representation.
- Native encoder streams directly into the GHV container, removing the large intermediate file/copy pass.
- Added Fast / Balanced / Quality presets.
- Added decode-ahead playback queue, PTS scheduling, late-frame dropping and keyframe recovery.
- Added JSON inspector output.
- Added CMake, Windows, Linux and macOS native-core build paths.
- Renamed embedded/standalone audio codec to GHAC1 and container to `.gha`.
