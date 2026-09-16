# GHVC8 Native Performance Milestone — 2026-09-15

This run optimizes the existing GHVC8 implementation. It does **not** define a
new bitstream and therefore does not claim to be GHVC9. The complete A/B/C
outputs have the same byte length and SHA-256 as the previous GHVC8 outputs.
Objective metrics and the existing fixed 10/25/50/75/90 percent visual frames
are consequently pixel-identical.

System: Windows 11, Intel Family 6 Model 183, 20 logical cores, NVIDIA RTX
4060 Ti present but unused by the codec. Balanced q78, GHAC1 HQ audio.

## Result summary

| Test | Bytes | Encode GHVC8 old -> optimized | Decode old -> optimized | PSNR / SSIM |
|---|---:|---:|---:|---:|
| A 960x544/30 | 303,023,656 | 76.338 -> **140.443 fps** | 283.756 -> **448.172 fps** | 45.420949 / 0.984480 |
| B 1920x1080/30 | 580,797,129 | 25.826 -> **45.464 fps** | 81.341 -> **141.190 fps** | 46.671277 / 0.990330 |
| C 3840x2160/24 | 4,279,779,323 | 6.162 -> **9.777 fps** | 18.728 -> **30.213 fps** | 47.138372 / 0.987057 |

The decode numbers include complete reconstructed-frame CRC verification.
Test C is now 1.259x realtime in that stricter path.

Peak process-tree encode RAM was 151.0 MiB (A), 326.2 MiB (B), and 834.3 MiB
(C). Decoder peak RAM was 10.1, 21.4, and 68.6 MiB. Test C encoder RAM rose
from 769 MiB to 834 MiB because selected RD coefficients are retained until
serialization instead of being recomputed; this bounded 65 MiB trade buys a
large speed improvement and is recorded rather than hidden.

## Profile

Test C encoder wall time was 445.1 s:

- input decode/ingest: 31.506 s (7.1%);
- P codec: 323.640 s (72.7%);
- I codec: 66.699 s (15.0%);
- CRC: 19.622 s (4.4%);
- container writes: 1.269 s (0.3%).

Within the parallel P codec, per-thread accumulated CPU time was dominated by
RD candidate evaluation: 2,661.31 s versus 131.27 s motion SAD, 34.74 s
coefficient serialization, 27.20 s reconstruction, and 1.58 s MV coding.
These CPU totals intentionally exceed wall time because 20 OpenMP workers are
summed. RD is the unambiguous encoder hotspot.

Test C decoder wall time was about 144.1 s:

- bitstream reads: 3.929 s (2.7%);
- codec: 118.363 s (82.2%);
- verified CRC: 19.715 s (13.7%);
- other loop/allocation overhead: about 2.1 s.

Inside GHVC8 P decode, coefficient parsing took 30.499 s, inverse
transform/prediction/reconstruction 26.983 s, and MV decode 1.724 s. The
remaining P-codec time is primarily coefficient-array materialization,
allocation/initialization, and frame setup. That is the next decode target.

## Changes that measured positive

- Replaced per-macroblock motion candidate `std::vector` allocations with
  fixed scratch arrays.
- Cached quantization matrices once per frame.
- Added interior-block pointer paths that avoid per-pixel clamp work.
- Retained the winning RD coefficient blocks and removed the second forward
  transform/quantization pass. Output bytes remain identical.
- Added true zero-residual fast reconstruction: SKIP copies prediction without
  running an all-zero inverse transform.
- Added dependency-correct wavefront reconstruction for intra blocks.
- Replaced duplicated byte-at-a-time IEEE CRC-32 with a common slicing-by-8
  implementation. Historical checksums and all GHVC4-8 tests remain valid.

The 10-second Test A development clip progressed from 85.688 to 141.006 fps
with an identical SHA-256. Full Test A improved by 84.0%, B by 76.1%, and C by
58.7%. Verified decode improved by 57.9%, 73.6%, and 61.3% respectively.

No GPU or SIMD path was kept or claimed. Profiling showed RD algorithm work,
redundant transforms, CRC, and scalar bookkeeping were actionable first; a GPU
prototype was not justified without first removing that CPU waste. No tested
optimization regressed size or quality in this milestone.

## Playback acceptance

Test B completed 3/3 controlled runs with zero freeze, pitch, slowdown,
speedup, underrun, stall, or fatal events. Runs 1 and 2 dropped zero frames;
run 3 correctly dropped eight late video frames at a 41.7 ms maximum drift.
Audio speed remained exactly 1.0.

Test C completed one full controlled run: 4345 displayed, seven late video
frames dropped, zero freezes and zero audio timing events, 48.4 ms maximum
drift, bounded eight-frame queue. Result: **PASS**.

## Large-file audit

Container section offsets, per-frame index offsets, duration, and seek targets
are 64-bit in both native and Python paths. The 4,279,779,323-byte Test C file
verified and played successfully. Individual frame payload lengths and frame
count remain 32-bit format fields; Test C is safely below those per-record
limits.

Structured reports: `reports/test_a_ghvc8_perf.json`,
`reports/test_b_ghvc8_perf.json`, and `reports/test_c_ghvc8_perf.json`.

