# Changelog

## GHV 0.4 / GHA 0.2 — Compression & rename update

- Renamed GVID -> GHV (`.ghv`) and GAUD -> GHA (`.gha`).
- Introduced GHVC3 + GBP3 adaptive residual packing.
- Added sparse residual blocks and global-motion prediction.
- Added repeat-frame representation.
- Native encoder now streams directly into the GHV container, removing the large intermediate file/copy pass.
- Added Fast / Balanced / Quality presets.
- Added decode-ahead playback queue, PTS scheduling, late-frame dropping and keyframe recovery.
- Added JSON inspector output.
- Added CMake, Windows, Linux and macOS native-core build paths.
- Renamed embedded/standalone audio codec to GHAC1 and container to `.gha`.

Included sample: GHV 0.4 Balanced is ~47.5% smaller than the prior GVID 0.3 sample while measuring ~48.8 dB YUV PSNR against the source sample.
