# GHV / GHA Project State

Last updated: 2026-09-15

This is the handoff file for a future ChatGPT/Codex session or human contributor.

## Identity

- GHV = Goou_Zi High-efficiency Video, `.ghv`
- GHA = Goou_Zi High-efficiency Audio, `.gha`
- Current development release: **GHV 0.8 / GHA 0.2**
- Current native video codec: **GHVC8** (GHVC4/5/6/7 remain decodable; GHVC6/7 remain encodable)
- Current audio codec: **GHAC1**
- License: MIT
- Legacy project names GVID / GAUD are retired.

## User real-world benchmark history

Test A reference MV, 960x544/30:

- MP4: 24.6 MB
- OGV: 45.2 MB
- early GVID: 1.41 GB
- GHV 0.4: 927 MB
- GHV 0.5: **819 MB**
- GHV 0.6: **675.025 MiB** in the 2026-09-14 fixed-video rerun
- GHV 0.7 / GHVC7: **443.249 MiB**
- GHV 0.8 / GHVC8: **288.986 MiB**

User feedback on 0.5:

- conversion of 1080p30 is still too slow;
- large GHV files can stutter;
- one higher-resolution/large file could freeze visually while audio continued;
- a lower-resolution file played smoothly.

This strongly points to HD decoder/presentation throughput in addition to compression inefficiency.

Test B is a separate 1920x1080/30 performance and playback test. Do not mix
its values with Test A. The 2026-09-14 outputs were 1350.329 MiB (GHVC6) and
906.208 MiB (GHVC7), and 553.891 MiB (GHVC8).

Test C is the fixed 3840x2160/24, 181.348 s VP9 stress source. It must not be
substituted for Test A or B or used for every parameter experiment.

## GHV 0.8 architecture

GHVC8 adds 16x16 even-pixel local motion, component-median spatial MV
prediction with delta coding, reconstructed rate-distortion decisions over YUV,
one-bit zero-residual SKIP descriptors, and compact small run/level tokens.
Independent motion searches and transform blocks are parallel; decoder output
is scalar-bitstream-identical at every supported thread count. I frames retain
the fully specified GTC7 syntax.

Playback no longer interleaves multi-megabyte raw-video packets and PCM in a
single NUT pipe. That design was measured starving ffplay audio whenever its
video demux queue applied backpressure, causing pitch drop and timeline
slowdown/catch-up. The controlled path runs audio at its declared sample rate
and schedules bounded-queue video against monotonic time. It waits for early
frames and explicitly drops only late video frames.

## GHV 0.7 architecture

GHVC7:

- native 8x8 sequency-ordered integer Walsh-Hadamard transform;
- quality/frequency/chroma-aware scalar quantization;
- I-frame DC, vertical, and horizontal block predictors selected by coded cost;
- same-position closed-loop P prediction and zero-coefficient skip blocks;
- 3-bit packed block descriptors;
- zig-zag scan, trailing-zero removal, zero-run and signed varint levels;
- sampled-source Repeat detection and existing scene/keyframe policy;
- OpenMP P-block encode and decode with a scalar fallback;
- codec id/container minor version 7; `GTC7` frame payloads.

GHVC7's first profile does not yet write motion vectors. The existing GPM6
implementation is retained in GHVC6 for continued research.

GHVC6:

- YUV420p;
- scalar source quantization;
- I / P / Repeat frames;
- closed-loop reference reconstruction;
- GBP6 64-byte residual blocks grouped into 256-block independent chunks;
- raw/RLE descriptor maps;
- modes: zero, fixed 1..8-bit, sparse, Rice k0..5;
- block-local byte delta on P residuals;
- optional ZP06 zero-run wrapper;
- CRC32 reconstructed-frame checksum;
- experimental 32x32 GPM6 local block motion.

Important: local motion exists, but normal speed presets currently use range 0. Benchmarks show that the current motion search/prediction does not yet save bytes consistently enough to justify default conversion cost. Do not claim it as solved.

Playback:

- native `ghvdecode` supports GHVC4/5/6/7;
- OpenMP decode where available;
- producer/consumer frame queue;
- startup prebuffer (about half of configured queue);
- FFmpeg rawvideo input queue;
- stream-copy raw video + PCM audio into NUT;
- ffplay video-master sync;
- Auto playback buffer targets ~64 MiB decoded YUV (8–32 frames);
- native direct mux path removes Python from per-frame packed-video writes;
- zero-motion P fast path avoids motion-grid overhead for normal presets.
- player monitors decoder, mux, and ffplay together; fatal video failure stops
  the complete A/V chain instead of allowing audio to continue alone.

Tools:

- `ghvverify.py`: complete decode + CRC verification;
- `ghvdoctor.py`: measures decoder fps **and decoder→FFmpeg pipe** realtime headroom;
- `ghvbench.py`: repeatable encode/verify/decode/quality benchmark with optional JSON report;
- `ghvinfo.py`: format inspection;
- `ghvrepair.py`: rebuilds the frame index from intact VFRM records.

## Fixed real-video benchmark (2026-09-14)

Balanced q78, GHAC1 HQ audio, native CRC verification:

- Test A (960x544): GHVC6 675.025 MiB -> GHVC7 **443.249 MiB (-34.34%)**.
- Test A encode: 322.68 -> 162.51 fps; decode: 209.2 -> **313.3 fps**.
- Test A quality: 50.163 -> 45.688 dB PSNR; 0.995773 -> 0.985877 SSIM.
- Test B (1920x1080): GHVC6 1350.329 MiB -> GHVC7 **906.208 MiB (-32.89%)**.
- Test B encode: 100.04 -> 48.23 fps; decode: 82.8 -> **83.8 fps**.
- Test B pipe: 83.3 -> 83.5 fps (about 2.78x realtime).
- Test B quality: 48.374 -> 46.894 dB PSNR; 0.995662 -> 0.991389 SSIM.
- Both Test B files completed full 104.118 s native A/V playback without a
  freeze on this machine. A deliberately corrupt first-frame CRC stopped the
  complete A/V chain in 0.26 s.

Do not present these values as universal hardware performance.

See `benchmarks/GHVC7_BENCHMARK_2026-09-14.md` and the adjacent JSON report.

## Fixed GHVC8 benchmark (2026-09-15)

- Test A: 288.986 MiB, 76.338 encode fps, 283.756 decode fps,
  45.420949 dB / 0.984480.
- Test B: 553.891 MiB, 25.826 encode fps, 81.341 decode fps,
  46.671277 dB / 0.990330. Three controlled playback runs displayed all
  3121 frames with zero drops/freezes/speed events.
- Test C: 4081.516 MiB, 6.162 encode fps, 18.728 decode fps,
  47.138372 dB / 0.987057. Controlled playback failed: 1337 drops and 18
  freeze windows, while fixed-rate audio had no slowdown/pitch/speed-up event.

See `benchmarks/GHVC8_BENCHMARK_2026-09-15.md` and the A/B/C JSON reports.

## GHVC8 native performance milestone (2026-09-15)

The bitstream and reconstructed pixels are unchanged. Full-file SHA-256 hashes
for all A/B/C outputs match the earlier GHVC8 files exactly.

- Test A: encode **140.443 fps**, verified decode **448.172 fps**, 151.0 MiB encode peak.
- Test B: encode **45.464 fps**, verified decode **141.190 fps**, controlled playback 3/3 with zero clock/freeze events.
- Test C: encode **9.777 fps**, verified decode **30.213 fps / 1.259x realtime**, 834.3 MiB encode peak and 68.6 MiB decode peak.
- Test C full playback: PASS, 4345 displayed / 7 late drops, zero freeze/pitch/slowdown/speedup, 48.4 ms max drift.

The encoder profiler identifies RD candidate evaluation as the dominant CPU
hotspot. The decoder is now dominated by GHVC8 P-frame coefficient
materialization/parse and reconstruction; verified CRC is still 13.7% of Test
C wall time even after slicing-by-8 acceleration. See
`benchmarks/GHVC8_PERFORMANCE_2026-09-15.md`.

## GHVC8 performance iteration 2 (2026-09-15)

No bitstream change was made. Strict accumulated-cost RD rejection preserves
the original candidate/tie order and all A/B/C SHA-256 values. Persistent
thread-local decode scratch plus exact zero/DC-only paths improve full verified
decode without changing pixels.

- Test A: **155.223 encode / 670.845 decode fps**.
- Test B: **48.795 encode / 211.439 decode fps**; playback 3/3 PASS with
  2/10/0 scheduling drops and zero freeze/audio-speed events.
- Test C: **10.464 encode / 45.997 decode fps (1.917x realtime)**; playback
  PASS with 38 late drops, zero freeze/pitch/slowdown/speedup.
- A/B/C sizes, PSNR, SSIM, visual result, and SHA-256 are exactly unchanged.

RD evaluation remains the largest encode hotspot; coefficient decode is the
largest decode hotspot. Narrow SSE2 and cheap-SAD ordering experiments were
measured and rejected. See
`benchmarks/GHVC8_PERFORMANCE_ITERATION_2_2026-09-15.md` and
`GODOT_INTEGRATION_NOTES.md`.

## Reproduce

```text
python ghvbench.py input.mp4 output.ghv --codec 8 --preset balanced --quality-metrics --decode-frames 0 --report-json report.json
python ghvdoctor.py output.ghv --frames 999999 --verify
python ghvplay.py output.ghv --engine native
```

## Next major milestone

GHVC8 achieved the `<300 MiB` Test A stage, but remains far from OGV/Theora.
Highest-value next work:

1. reduce remaining full RD work with a bitstream-preserving coarse lower bound or batched transforms;
2. replace the remaining escape levels with measured adaptive Rice/canonical Huffman coding;
3. compact/reuse decoder coefficient scratch and continue SIMD evaluation;
4. add CRF-like rate control and formal Fast/Balanced/Quality/Compact curves;
5. expose `libghv` decoder/player APIs and remove the long-term ffplay dependency;
6. capture peak memory, CPU utilization, and rendered dropped-frame/A/V drift metrics.
