# GHVC7 Fixed-Video Benchmark — 2026-09-14

Environment: Windows x64, MSVC 19.50, OpenMP (20 threads), Balanced quality
78, embedded GHAC1 HQ audio. Sizes are MiB (`bytes / 1048576`). Every output
completed native CRC verification.

## Test A — compression benchmark

Source: 960x544, 30 fps, 127.4 s, 25,857,237 bytes (24.659 MiB).

| Metric | GHVC6 / GHV 0.6 | GHVC7 / GHV 0.7 | Change |
|---|---:|---:|---:|
| Output size | 675.025 MiB | **443.249 MiB** | **-34.34%** |
| Encode speed | 322.68 fps | 162.51 fps | -49.64% |
| Native decode | 209.2 fps | **313.3 fps** | +49.76% |
| Native to FFmpeg pipe | 210.6 fps | **301.4 fps** | +43.11% |
| PSNR | 50.163 dB | 45.688 dB | -4.475 dB |
| SSIM | 0.995773 | 0.985877 | -0.009896 |

The first external size target (`<500 MiB`) is met. GHVC7 saves 243,034,335
bytes while retaining high measured fidelity. Encoding is about half as fast,
which is the main regression to address next.

## Test B — HD playback/performance stress

Source: 1920x1080, 30 fps, 104.118 s, 38,589,170 bytes (36.802 MiB).

| Metric | GHVC6 / GHV 0.6 | GHVC7 / GHV 0.7 | Change |
|---|---:|---:|---:|
| Output size | 1350.329 MiB | **906.208 MiB** | **-32.89%** |
| Encode speed | 100.04 fps | 48.23 fps | -51.79% |
| Native decode | 82.8 fps | **83.8 fps** | +1.21% |
| Native to FFmpeg pipe | 83.3 fps | **83.5 fps** | +0.24% |
| PSNR | 48.374 dB | 46.894 dB | -1.481 dB |
| SSIM | 0.995662 | 0.991389 | -0.004273 |
| Full 104 s A/V playback freeze | No | No | no regression |

Both files played to clean EOF with the native decoder, GHAC1 audio, a 22-frame
(about 65 MiB) decoded queue, CRC checking, and video-master ffplay sync. The
historical freeze did not reproduce on this machine with either newly encoded
file.

The player nevertheless had the structural failure mode described by the user:
it waited only for ffplay, so a fatal video decoder exit could leave the audio
input running. The player now monitors decoder, mux, and presenter health. A
test file with a deliberately invalid first-frame CRC stopped the full chain in
0.26 s; audio did not continue. A valid 2 s file reached clean EOF in 2.62 s.

## Method notes

- Inputs are the two fixed real videos in `测试视频`; synthetic media was used
  only for self-tests, never as the benchmark replacement.
- PSNR/SSIM compare sequential decoded YUV420 frames to FFmpeg-decoded source
  frames. Both inputs receive normalized frame-number PTS. Quality filters use
  `shortest=1:repeatlast=0` to prevent FFmpeg from repeating the final frame.
- Decode numbers include full-file CRC verification. Pipe numbers include the
  native decoder to FFmpeg rawvideo handoff.
- Peak memory, aggregate CPU utilization, and automatic rendered dropped-frame
  counts were not captured in this run and must not be inferred.
