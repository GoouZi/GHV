# GHV / GHA Project State

Last updated: 2026-09-16

This is the handoff file for a future ChatGPT/Codex session or human contributor.

## Identity

- Repository: **https://github.com/GoouZi/GHV**
- Current branch: **`dev/ghvc9`** (published beta is promoted to `main` after validation)
- Current project beta version: **0.9.0-beta.1**
- GHV = Goou_Zi High-efficiency Video, `.ghv`
- GHA = Goou_Zi High-efficiency Audio, `.gha`
- Current development release: **GHV 0.9 / GHA 0.2**
- Current native video codec: **GHVC9** (GHVC4 through GHVC8 remain decodable)
- Current audio codec: **GHAC1**
- GHV Studio: **0.9.0-beta.1**
- GHA Studio: **0.2.0-beta.1**
- Authoritative version source: **`VERSION.json`**
- License: Apache-2.0
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
- GHV 0.9 / GHVC9 Milestone 1: **210.616 MiB**

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

## GHV 0.9 architecture

GHVC9 retains GHVC8's reconstruction, quantization, 16x16 motion vectors,
median MV prediction, SKIP semantics, I-frame `GTC7`, frame CRC, and container
index. It changes P payloads to `GBP9`:

- encoder-only sampled-SAD ranking admits zero motion plus the best nonzero
  finalist in Balanced, reducing expensive full-RD entries by 33.33%;
- exact full RD and branch-and-bound still make the final decision;
- the zero descriptor map is followed by 256-block independent coefficient
  chunks;
- capped-unary zero runs and Rice `k=2` signed levels replace GHVC8 compact
  byte tokens;
- chunks are bounded, independently validated, and parsed in parallel;
- zero and DC-only inverse paths remain pixel exact.

No 32x32 partition or merge syntax was shipped in Milestone 1. Those changes
were deliberately kept out until their rate/complexity tradeoff can be tested
independently. Encoder presets alter search effort only; all emit one standard
GHVC9 bitstream.

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

- `ghvverify`: complete decode + CRC verification;
- `ghvdoctor`: measures decoder fps **and decoder→FFmpeg pipe** realtime headroom;
- `ghvbench`: repeatable encode/verify/decode/quality benchmark with optional JSON report;
- `ghvinfo`: format inspection;
- `ghvrepair`: rebuilds the frame index from intact VFRM records.

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

See [`../../benchmarks/milestones/ghvc7/GHVC7_BENCHMARK_2026-09-14.md`](../../benchmarks/milestones/ghvc7/GHVC7_BENCHMARK_2026-09-14.md) and the adjacent JSON report.

## Fixed GHVC8 benchmark (2026-09-15)

- Test A: 288.986 MiB, 76.338 encode fps, 283.756 decode fps,
  45.420949 dB / 0.984480.
- Test B: 553.891 MiB, 25.826 encode fps, 81.341 decode fps,
  46.671277 dB / 0.990330. Three controlled playback runs displayed all
  3121 frames with zero drops/freezes/speed events.
- Test C: 4081.516 MiB, 6.162 encode fps, 18.728 decode fps,
  47.138372 dB / 0.987057. Controlled playback failed: 1337 drops and 18
  freeze windows, while fixed-rate audio had no slowdown/pitch/speed-up event.

See [`../../benchmarks/milestones/ghvc8/GHVC8_BENCHMARK_2026-09-15.md`](../../benchmarks/milestones/ghvc8/GHVC8_BENCHMARK_2026-09-15.md) and the A/B/C JSON reports.

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
[`../../benchmarks/milestones/ghvc8/GHVC8_PERFORMANCE_2026-09-15.md`](../../benchmarks/milestones/ghvc8/GHVC8_PERFORMANCE_2026-09-15.md).

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
[`../../benchmarks/milestones/ghvc8/GHVC8_PERFORMANCE_ITERATION_2_2026-09-15.md`](../../benchmarks/milestones/ghvc8/GHVC8_PERFORMANCE_ITERATION_2_2026-09-15.md) and
[`../integrations/godot.md`](../integrations/godot.md).

## Frozen stable release and native Player (2026-09-15)

The byte-identical performance milestone is permanently anchored at commit
`8f8ec96e00468c9029c573e182520778be9d2f39` by annotated tag
`ghv-0.8-stable-perf2`, branch `backup/ghvc8-stable-perf2`, a verified complete
Git bundle, and a build-tested source ZIP. See [`recovery/GHVC8_STABLE.md`](recovery/GHVC8_STABLE.md).

GHV Player 0.1 is a separate native Windows application:

- `libghv` GHVC7/8 decode, 64-bit indexed seek, native GHAC1 PCM decode;
- bounded worker-decoded YUV420 frame queue;
- D3D11 planar YUV upload and shader conversion;
- fixed-rate WASAPI audio/device master clock;
- Win32 DPI-aware controls, drag/drop, timeline seek, volume/mute, aspect-fit
  resize, fullscreen, EOF/replay, and structured errors;
- no Python, FFmpeg, ffplay, or OpenCV dependency in the end-user player.

Test B completed 3/3 formal runs with all 3121 frames displayed and no drops,
underruns, freezes, or audio-speed events. Test C completed 2/2 formal runs
with eight late video drops per run and no underrun, freeze, pitch, slowdown,
or speedup event. See [`../../benchmarks/player/GHV_PLAYER_0.1_WINDOWS_2026-09-15.md`](../../benchmarks/player/GHV_PLAYER_0.1_WINDOWS_2026-09-15.md).

The new library is a decoder/player foundation, not yet a stable C ABI. Direct
YUV/PCM encoding and the generated-without-MP4 demo remain Phase 2 work.

## GHVC9 Milestone 1 (2026-09-16)

Balanced, unchanged q78 quantization, native CRC verification:

- Test A: **210.616 MiB**, 169.317 encode / 913.114 decode fps,
  45.355968 dB / 0.984213; fixed 10/25/50/75/90% visual PASS.
- Test B: **405.317 MiB**, 52.810 encode / 279.601 decode fps,
  46.627935 dB / 0.990219; full playback PASS with zero drops/freezes/audio events.
- Test C: **3062.501 MiB**, 11.196 encode / 59.362 decode fps
  (**2.473x realtime**), 47.084085 dB / 0.986903; full playback PASS with
  zero drops/freezes/audio events.

Relative to frozen GHVC8, output fell 27.12% / 26.82% / 24.97%, encode improved
9.08% / 8.23% / 7.00%, and verified decode improved 36.11% / 32.23% / 29.06%.
Full RD candidates fell from 21,377,160 to 14,251,440 on Test A. The dominant
encode hotspot remains exact RD evaluation. The dominant codec decode stage is
now inverse transform/prediction/reconstruction rather than coefficient parse.

Measured and rejected experiments are recorded, not hidden: whole-payload
PackBits saved only about 1.26% and slowed decode; DC prediction saved about 4%
but slowed encode about 14% and decode about 28%; Rice `k=1` was 3.8% larger;
an unchunked bitstream saved rate but serialized coefficient decode. The chunked
design and capped-unary runs were retained. See
[`../../benchmarks/milestones/ghvc9/GHVC9_MILESTONE1_2026-09-16.md`](../../benchmarks/milestones/ghvc9/GHVC9_MILESTONE1_2026-09-16.md).

This report is the latest authoritative verified benchmark. The current
development focus is GHVC9 compression/RD efficiency, inverse reconstruction
throughput, and malformed-stream robustness. New Player UI, Godot/MovieWriter,
VLC/PotPlayer, and other ecosystem integrations are intentionally paused until
the codec structure is more mature.

## Reproduce

```text
ghvbench input.mp4 output.ghv --codec 9 --preset balanced --quality-metrics --decode-frames 0 --report-json report.json
ghvdoctor output.ghv --frames 999999 --verify
ghvplay output.ghv --engine native
```

## Next major milestone

GHVC9 achieved the `<220 MiB` Test A milestone, but remains far from OGV/Theora.
Highest-value next work:

1. test 32x32/16x16 adaptive partitions and merge-like MV reuse in isolation;
2. reduce remaining full RD work with coarse lower bounds or batched finalist transforms;
3. optimize inverse transform/prediction/reconstruction without losing bit exactness;
4. revisit DC/context entropy only with a much cheaper predictor representation;
5. add malformed-stream fuzz coverage and cross-platform GHVC9 verification;
6. keep player/plugin/ecosystem expansion paused until the codec structure stabilizes.
