# GHVC9 motion search and size · 2026-11-18

## Full files

Same source SHA-256, Balanced q78, one nonzero finalist, compact motion and coefficients, native CRC verification, HQ audio. Radius 4 measurements are the November 10 full suite. Radius 12 was encoded once per source on November 18. Quality is measured against the source. Throughput on different dates includes system load variation.

| Source | MiB 4 → 12 | Reduction | PSNR dB 4 → 12 | SSIM 4 → 12 | Encode fps 4 → 12 | Decode fps 4 → 12 |
|---|---:|---:|---:|---:|---:|---:|
| A | 171.204 → 167.035 | 2.43% | 45.356 → 45.568 | 0.984213 → 0.985391 | 136.21 → 149.96 | 731.41 → 882.13 |
| B | 324.400 → 282.368 | 12.96% | 46.628 → 46.962 | 0.990219 → 0.991537 | 48.56 → 51.85 | 254.61 → 270.65 |
| C | 2346.136 → 2253.120 | 3.96% | 47.084 → 47.573 | 0.986903 → 0.988814 | 10.72 → 10.96 | 55.98 → 55.80 |

## Motion experiment

The search adds even-pixel horizontal and vertical candidates out to the specified radius. Diagonal candidates stay at ±2. All candidates are ranked by sampled luma SAD. Finalists are evaluated with a transform, quantization and reconstructed distortion. The RD calculation uses zero as its vector predictor during parallel selection and a byte-rate proxy. The stored vectors use the spatial median predictor. The candidate pool and the selection cost can therefore be improved independently.

The screening clips were the existing 302-frame A10 and 240-frame C10, plus ten seconds of B starting at 30 seconds decoded to lossless FFV1. Audio was omitted on all clips. Trials used 1 or 3 nonzero finalists (Balanced or Quality, with quality, radius, key interval and scene threshold explicitly pinned).

| Clip | Radius | Finalists | Video-only container bytes | Motion bytes | PSNR dB | SSIM | Encode fps |
|---|---:|---:|---:|---:|---:|---:|---:|
| A | 4 | 1 | 13291376 | 270597 | 45.376 | 0.982247 | 146.60 |
| A | 12 | 1 | 12654767 | 373037 | 45.709 | 0.984170 | 151.69 |
| A | 4 | 3 | 13297409 | 338479 | 45.565 | 0.983141 | 124.64 |
| A | 12 | 3 | 12566378 | 480592 | 45.977 | 0.985351 | 126.04 |
| B | 4 | 1 | 50332016 | 988255 | 46.162 | 0.987323 | 37.87 |
| B | 12 | 1 | 45933841 | 1508262 | 46.431 | 0.988584 | 37.94 |
| B | 4 | 3 | 50266714 | 1217028 | 46.210 | 0.987395 | 30.10 |
| B | 12 | 3 | 45396253 | 1825013 | 46.621 | 0.989239 | 30.64 |
| C | 4 | 1 | 135555386 | 4026497 | 46.704 | 0.985071 | 11.12 |
| C | 12 | 1 | 131192263 | 6841557 | 47.158 | 0.987014 | 10.99 |
| C | 4 | 3 | 136119463 | 5431457 | 46.857 | 0.985590 | 8.75 |
| C | 12 | 3 | 131307481 | 9314980 | 47.386 | 0.987762 | 8.01 |

## Fixed-reference check

For each clip take the first P frame at or after each quartile. Decode the preceding frame with the radius-4 Balanced binary output as the reference. Pass that same reference and current source YUV into the native encode and pack routines at q78 for all four configurations. This removes prediction-chain divergence. SSE is summed over reconstructed Y, U and V samples. The ledger includes individual frames and all settings.

| Clip, 3 P frames | Blocks choosing a vector beyond 4 at radius 12 | Total blocks | Extra motion bytes | Other packet bytes saved | Total bytes saved | SSE change |
|---|---:|---:|---:|---:|---:|---:|
| A | 627 | 6120 | 1061 | 4191 | 3130 | -47799 |
| B | 6113 | 24480 | 8305 | 76106 | 67801 | -390081 |
| C | 17228 | 97200 | 38941 | 34339 | -4602 | -457093 |

On the three fixed-reference 4K frames the wider search reduced squared error while increasing aggregate packet size. The full prediction chain measures the effect that matters for storage. Three finalists did not deliver a consistent additional size reduction across clips. The opt-in radius 12 uses existing GHVC9 syntax and quantization. Motion and reconstructed pixels change. Existing presets continue to select their prior search range.

A separate physical-cost probe counted current three-byte motion escapes that could fit in one byte if the existing even-pixel deltas were represented in steps of two pixels. On the radius-12 A/B/C excerpts this would save 95,474 / 468,236 / 2,598,006 bytes before any new signaling. These are syntax estimates. The files tested here use the existing motion syntax.

## Reproduction

```powershell
python -m ghv.cli.suite benchmarks/output/radius12-repeat "../测试视频/Test A.mp4" "../测试视频/Test B.mp4" "../测试视频/Test C.mp4" --build native/build_m2 --repeat 3 --change compression --motion-range 12
```

For single-file conversion use `python -m ghv.cli.encode input.mp4 output.ghv --codec 9 --preset balanced --compact-motion --compact-coefficients --motion-range 12`. Open the result with the current native GHV Player.

## Decoder and player

Separate decoder experiment on the same November 10 streams. A prototype fills a 16-bit run/level lookahead table with the exact compact-run prefixes. All timed runs verified the stored reconstruction CRCs.

| Clip | Existing decode fps | Prototype decode fps |
|---|---:|---:|
| A | 820.083 | 790.249 |
| B | 246.363 | 236.387 |
| C | 52.357 | 52.591 |

A uses three paired runs and B/C use one pair each. The A median fell with the expanded lookup table. The prototype was rejected and production retains the existing decoder.

On full A, the current decoder was also profiled against the November 10 streams before and after the zero-run syntax change. The following are medians of three verified decodes per stream. These instrumented phase times describe that run configuration.

| Native phase | Earlier syntax ms | Current syntax ms |
|---|---:|---:|
| coeff_decode_ms | 699.6 | 749.3 |
| inverse_recon_ms | 1748.0 | 1769.7 |
| motion_decode_ms | 92.3 | 92.9 |
| verify_crc_ms | 1086.6 | 1088.7 |

Visible native 4K window, muted WASAPI playback to EOF: 4349 displayed, 3 dropped, 0 freezes, 0 underruns, 33.10 ms maximum AV drift. All three skips occurred at one presentation poll at audio clock 80.699614 s. Their PTS were 80.541518, 80.583185 and 80.624851 s. The queue depths were 8, 7 and 6 and each frame had been ready for at least 383 ms. This identifies a late presentation decision with ready frames. The trace does not identify which operation delayed the poll. The original seven drops had no per-event trace.

[All individual measurements and player telemetry](reports/motion_radius_2026-11-18.json).
