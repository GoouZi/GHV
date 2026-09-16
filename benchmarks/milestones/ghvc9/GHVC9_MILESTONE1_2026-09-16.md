# GHVC9 Milestone 1 Benchmark — 2026-09-16

## Scope and method

This is the first bitstream-changing iteration after frozen GHVC8 commit
`8f8ec96e00468c9029c573e182520778be9d2f39`. The stable tag and backup branch
were not moved. All full runs used Balanced q78, embedded GHAC1 audio, native
CRC verification, full PSNR/SSIM, and the same fixed A/B/C sources as GHVC8.

Machine: Intel Family 6 Model 183, 20 logical cores, Windows 11, NVIDIA RTX
4060 Ti present but unused by the codec. Values are machine-specific. Container
bpp includes audio and container overhead.

## Summary

| Test | GHVC8 | GHVC9 | Reduction | bpp/frame | bits/frame |
|---|---:|---:|---:|---:|---:|
| A 960x544/30 | 303,023,656 B / 288.986 MiB | **220,846,827 B / 210.616 MiB** | **27.119%** | 0.885157 | 462,264 |
| B 1920x1080/30 | 580,797,129 B / 553.891 MiB | **425,005,482 B / 405.317 MiB** | **26.823%** | 0.525371 | 1,089,408 |
| C 3840x2160/24 | 4,279,779,323 B / 4081.516 MiB | **3,211,264,955 B / 3062.501 MiB** | **24.965%** | 0.711692 | 5,903,061 |

| Test | Encode fps GHVC8 -> GHVC9 | Conversion time GHVC8 -> GHVC9 | Decode fps GHVC8 -> GHVC9 |
|---|---:|---:|---:|
| A | 155.223 -> **169.317** (+9.08%) | 24.62 -> **22.82 s** | 670.845 -> **913.114** (+36.11%) |
| B | 48.795 -> **52.810** (+8.23%) | 63.96 -> **59.36 s** | 211.439 -> **279.601** (+32.23%) |
| C | 10.464 -> **11.196** (+7.00%) | 415.90 -> **388.99 s** | 45.997 -> **59.362** (+29.06%) |

The old conversion times are frame-count/fps estimates; GHVC9 values are
measured end-to-end benchmark wall time.

## Quality

| Test | PSNR GHVC8 -> GHVC9 | SSIM GHVC8 -> GHVC9 | Visual |
|---|---:|---:|---:|
| A | 45.421 -> **45.355968 dB** | 0.984480 -> **0.984213** | **PASS** |
| B | 46.671 -> **46.627935 dB** | 0.990330 -> **0.990219** | objective PASS |
| C | 47.138 -> **47.084085 dB** | 0.987057 -> **0.986903** | objective PASS |

Test A fixed 10/25/50/75/90% source-versus-decoded comparisons were inspected.
Text and anime line edges remained clean; no new obvious blocking, ringing,
banding, ghosting, or motion smear was visible. Quantization tables are
unchanged. The tiny objective movement comes from the smaller motion shortlist,
not stronger quantization. Images are in [`visual/test_a`](visual/test_a/).

## Design and profile

GHVC9 does not add a decoder-visible motion mode in Milestone 1. It retains
GHVC8 16x16 motion, median MV prediction, delta vectors, SKIP, and exact RD.
Balanced ranks candidates with sampled SAD and sends only zero motion plus the
best nonzero finalist into exact RD. Coefficients use bounded 256-block chunks,
capped-unary zero runs, and signed Rice `k=2` levels.

| Test | Full RD candidates GHVC8 -> GHVC9 | Completed GHVC8 -> GHVC9 | RD blocks GHVC8 -> GHVC9 |
|---|---:|---:|---:|
| A | 21,377,160 -> **14,251,440** (-33.33%) | 9,650,157 -> **8,842,828** | 107,578,790 -> **75,545,356** (-29.77%) |
| B | 57,160,800 -> **38,107,200** (-33.33%) | 26,818,221 -> **24,531,939** | 299,902,284 -> **209,140,438** (-30.26%) |
| C | 363,819,600 -> **242,546,400** (-33.33%) | 182,834,162 -> **161,862,106** | 1,970,416,348 -> **1,362,746,879** (-30.84%) |

Test A accumulated profiler CPU time: motion 7,360.64 ms, exact RD 89,840.5
ms, coefficient entropy 2,343.17 ms, reconstruction 1,439.75 ms. Exact RD is
still the largest encoder hotspot.

Test A decode: coefficient parse 533.58 ms and inverse transform/prediction/
reconstruction 1,493.94 ms. Test C: 5,149.21 ms and 22,705.3 ms respectively.
The largest codec decode hotspot has moved to inverse reconstruction. Full-file
CRC remains separately visible (19,979.6 ms on C).

## Preset sample

The preset check used the same 10.067-second A clip. Presets intentionally use
different quality/search settings, so the size row is not a same-quality codec
comparison.

| Preset | Bytes | Encode fps | Decode fps |
|---|---:|---:|---:|
| Fast | 12,516,773 | **177.633** | **1133.280** |
| Balanced | 16,411,936 | **164.728** | **850.081** |
| Quality | 31,952,641 | **105.745** | **840.485** |

The preset is encoder-only. Every file is standard GHVC9 and uses the same
decoder.

## Playback and memory

| Test | Displayed / dropped | Freeze / pitch / slow / speed-up | Max A/V drift | Player peak |
|---|---:|---:|---:|---:|
| B | 3121 / **0** | **0 / 0 / 0 / 0** | 18.683 ms | 168.7 MiB |
| C | 4352 / **0** | **0 / 0 / 0 / 0** | 15.979 ms | 290.9 MiB |

Test C decode is **2.473x realtime**. Encode peak memory was 152.8 MiB (A),
333.3 MiB (B), and 812.5 MiB (C), so the new chunk tables did not introduce
unbounded candidate memory. Decoder benchmark peaks were 13.2/34.9/124.6 MiB.

## Rejected experiments

- Whole-payload PackBits saved only about 1.26% and slowed decode: rejected.
- A DC predictor prototype saved about 4% but slowed encode about 14% and
  decode about 28%: rejected rather than hiding the CPU regression.
- The first unchunked bit coder saved about 22%, but serialized coefficient
  materialization and regressed decode: replaced by bounded chunks.
- Rice `k=1` produced 18,474,257 bytes versus 17,793,652 for `k=2` on A10
  (about 3.8% larger): rejected.
- Capped-unary zero runs reduced the chunked A10 output from 17,793,652 to
  16,411,936 bytes (7.77%): retained.
- Adaptive 32x32/16x16 partitions and merge-like syntax were not mixed into
  this milestone. They remain isolated Milestone 2 experiments.

## Integrity and compatibility

- GHVC4/5/6/7/8/9 self-tests: PASS.
- GHVC9 malformed chunk table: rejected safely.
- libghv codec-9 open, indexed seek, decode, and audio smoke test: PASS.
- A/B/C native full verify and reconstructed CRC: PASS.
- GHVC9 SHA-256:
  - A: `5587DB122F4A55C43A7B4626CEDF9354DDFE594C460424DDEFDFBABC47F9E977`
  - B: `4421BF1260628C3012E6CE18976EB531FE15F2087100B98D1384B06D1EDFDC4D`
  - C: `056D8A8BCDABFEB6DEF6869D1D5EB7A5D2DE583BB62D936C7B83F8DF0AC4C54B`

GHVC9 is intentionally not SHA-identical to GHVC8 because the bitstream changed.
The frozen GHVC8 tag and backup branch remain untouched.

Raw reports:

- `reports/GHVC9_M1_TEST_A_FINAL_2026-09-16.json`
- `reports/GHVC9_M1_TEST_B_2026-09-16.json`
- `reports/GHVC9_M1_TEST_C_2026-09-16.json`
- `reports/GHVC9_M1_PLAYBACK_B_2026-09-16.json`
- `reports/GHVC9_M1_PLAYBACK_C_2026-09-16.json`
- `reports/GHVC9_PRESET_{fast,balanced,quality}_A10_2026-09-16.json`
