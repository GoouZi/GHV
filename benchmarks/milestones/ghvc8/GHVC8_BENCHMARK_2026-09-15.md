# GHVC8 Fixed-Video Benchmark — 2026-09-15

Hardware-specific results; do not generalize these speeds. All files use the
fixed Test A/B/C identities in `测试视频`, balanced q78, GHAC1 HQ audio, native
CRC verification, full-file decode, PSNR, and SSIM.

## Test A — 960x544, 30 fps, 127.400 s

| Metric | GHVC7 | GHVC8 |
|---|---:|---:|
| Output | 464,780,378 bytes / 453,887 KB | 303,023,656 bytes / 295,922 KB |
| Reduction | — | 34.80% |
| Encode | 162.51 fps | 76.338 fps |
| Decode | 313.3 fps | 283.756 fps |
| PSNR | 45.688338 dB | 45.420949 dB |
| SSIM | 0.985877 | 0.984480 |
| Encode peak RSS, process tree | not captured | 150.2 MiB |
| Decode peak RSS | not captured | 10.4 MiB |

The `<300 MB` stage is met without increasing quantization. Five side-by-side
frames at 10/25/50/75/90% are in `benchmarks/visuals/test_a`. Inspection found
no material new blocking, ringing, banding, motion smear, or damaged line/text
edges. Visual result: **PASS**, with the small metric regression recorded above.

## Test B — 1920x1080, ~30 fps, 104.118 s

| Metric | GHVC7 | GHVC8 |
|---|---:|---:|
| Output | 950,228,012 bytes | 580,797,129 bytes / 567,185 KB |
| Reduction | — | 38.88% |
| Encode | 48.231 fps | 25.826 fps |
| Decode | 83.8 fps | 81.341 fps / 2.71x realtime |
| PSNR | 46.893747 dB | 46.671277 dB |
| SSIM | 0.991389 | 0.990330 |
| Encode / decode peak RSS | not captured | 325.3 / 21.4 MiB |

Controlled player, three complete consecutive runs:

| Run | Shown | Dropped | Freeze | Slow/pitch/speed-up | Max drift |
|---:|---:|---:|---:|---:|---:|
| 1 | 3121 | 0 | 0 | 0 / 0 / 0 | 40.6 ms |
| 2 | 3121 | 0 | 0 | 0 / 0 / 0 | 0.84 ms |
| 3 | 3121 | 0 | 0 | 0 / 0 / 0 | 0.97 ms |

Five fixed visual comparisons are in `benchmarks/visuals/test_b`. Visual and
playback result: **PASS**.

## Test C — 3840x2160, 24 fps, 181.348 s

Source is the fixed 374,807,495-byte VP9/AAC file, approximately 16.4 Mbps
video plus 128 kbps stereo 44.1 kHz audio.

| Metric | GHVC8 |
|---|---:|
| Output | 4,279,779,323 bytes / 4081.516 MiB |
| Encode | 6.162 fps / 0.257x realtime |
| Decode | 18.728 fps / 0.780x realtime |
| PSNR / SSIM | 47.138372 dB / 0.987057 |
| Encode / decode peak RSS | 769.0 / 67.5 MiB |
| Playback wall time | 181.431 s |
| Displayed / dropped | 3015 / 1337 |
| Freeze events | 18 |
| Slowdown / speed-up / pitch change | 0 / 0 / 0 |
| Max scheduled drift | 52.0 ms |

The audio/wall timeline remained fixed and ended on time, but decode throughput
was below 24 fps and video froze/dropped heavily. Playback result: **FAIL**.
Five comparison frames are in `benchmarks/visuals/test_c`; still-frame visual
quality is high, but it does not compensate for failed motion playback.

## Playback root cause and rejected experiments

The old native-decoder -> raw YUV + PCM NUT -> ffplay chain used video master.
When raw-video demux queues filled, backpressure stopped ffplay from reading
audio packets in the same pipe. Audio queues reached zero, ffplay stretched the
timeline (measured 0.857x in one 104 s Test B run), lowered pitch, then corrected
later. Changing only the ffplay clock did not solve the shared demux blockage.

Rejected measurements:

- interleaved NUT + audio master + early frame drop: stable audio but 879
  erroneous video drops;
- interleaved NUT + no early drop: 104 s media took 124 s, 0.857x average;
- split audio/video but ffplay external-clock video: audio stayed fixed, while
  ffplay varied from 0 to 526 drops and once accumulated 6.99 s video drift.

The accepted controlled path removes interleaved NUT, starts fixed-sample-rate
audio, reads native decoded YUV into a bounded queue, and schedules rendering
against monotonic time. Early frames wait; late frames drop; media time never
slows. Fatal decode still stops the complete A/V presentation.

## Codec interpretation

GHVC8's rate reduction combines 16x16 local motion, median-predicted MV deltas,
RD selection across Y/U/V reconstructed distortion and actual byte cost,
one-bit zero-residual SKIP blocks, and compact one-byte common run/level tokens.
The entropy token change is lossless with respect to reconstructed coefficients.

The largest current bottleneck is 4K: exhaustive per-macroblock candidate RD
work reduces encode to 6.16 fps, while transform/reconstruction decode is only
18.73 fps. Next work should add coarse-to-fine motion search, buffer/scratch
reuse, SIMD SAD/transform/reconstruction, and measured adaptive coefficient
coding. A native GPU-texture renderer and native audio backend should replace
the current OpenCV/ffplay output helpers without changing the audio-master rules.
