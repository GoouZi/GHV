# GHVC8 Performance Iteration 2 — 2026-09-15

This iteration changes no container or codec syntax. All fixed A/B/C outputs
have exactly the same byte length and SHA-256 as the preceding GHVC8 milestone;
therefore PSNR, SSIM, and the approved visual comparisons are unchanged.

## Results

| Test | Output | Encode | Verified decode | Encode RAM | Decode RAM |
|---|---:|---:|---:|---:|---:|
| A 960x544/30 | 303,023,656 B | 140.443 -> **157.938 fps** | 448.172 -> **738.303 fps** | 151.0 -> 152.0 MiB | 10.1 -> 12.3 MiB |
| B 1920x1080/30 | 580,797,129 B | 45.464 -> **48.795 fps** | 141.190 -> **211.439 fps** | 326.2 -> 329.7 MiB | 21.4 -> 30.9 MiB |
| C 3840x2160/24 | 4,279,779,323 B | 9.777 -> **10.464 fps** | 30.213 -> **45.997 fps** | 834.3 -> 799.7 MiB | 68.6 -> 105.9 MiB |

Test C verified decode is 1.917x realtime. The persistent thread-local decoder
scratch raises the bounded process peak, especially at 4K, but avoids repeated
large coefficient/motion initialization. Encoder memory did not grow materially.

## Encoder profile and exact RD pruning

Every candidate previously ran all six 8x8 residual blocks through transform,
quantization, reconstruction, distortion, and entropy-rate estimation. The new
path evaluates the zero candidate first and uses the accumulated block cost as
a strict lower bound. A candidate is rejected only when it cannot strictly beat
the current winner, preserving the original decision and tie order.

| Test | Candidates | Full RD complete | Early reject | Reject rate |
|---|---:|---:|---:|---:|
| A | 21,377,160 | 9,650,157 | 11,727,003 | 54.86% |
| B | 57,160,800 | 26,818,221 | 30,342,579 | 53.08% |
| C | 363,819,600 | 182,834,162 | 180,985,438 | 49.75% |

The largest encoder hotspot remains RD evaluation. Test C accumulated 2,513 s
of parallel RD CPU time, versus 137 s motion-search CPU time. End-to-end encode
improvement is 12.5% on A, 7.3% on B, and 7.0% on C.

## Decoder profile

Persistent coefficient/motion scratch is reused per worker. Only coefficient
positions touched by the prior block are cleared. All-zero blocks copy the
prediction, and DC-only blocks use a pixel-exact constant inverse-transform
path. Test C full-decode stage totals were:

- coefficient decode: 31.277 s;
- inverse transform/prediction/reconstruction: 21.795 s;
- verified CRC: 19.520 s;
- motion decode: 1.732 s;
- bitstream read: 3.663 s.

Coefficient decode remains the largest decode hotspot, but the full verified
decoder improved 52.3% at 4K.

## Playback

- Test B: 3/3 PASS; 2/10/0 late video drops, zero freezes, pitch changes,
  slowdown, or speedup. Displayed-frame max drift was 14.3/10.7/2.4 ms.
- Test C: PASS; 4314 displayed, 38 late drops, zero freezes, pitch changes,
  slowdown, or speedup; displayed-frame max drift 69.6 ms.

The B drops are not caused by codec throughput: verified decode is 211 fps and
the bounded queue reported no underrun. They correlate with transient Windows /
OpenCV presenter scheduling stalls. The player keeps the correct policy—audio
at 1.0x and late video dropped—and telemetry now records dropped-frame lateness
separately so future runs no longer hide it behind displayed-frame drift.

## Rejected experiments

- Cheap-SAD candidate sorting was tested before full RD. On the Test A clip it
  reduced early rejects (894,183 versus 938,233), increased evaluated blocks,
  and ran slower than deterministic zero-first ordering. It was removed.
- An SSE2 eight-pixel add/clamp/SSD kernel passed 200,000 randomized pixel-exact
  equivalence cases, but measured 43.658 ms versus 15.564 ms scalar (0.357x).
  Packing/unpacking and horizontal reduction dominated this narrow kernel, so
  it was removed. No SIMD code is retained merely for utilization.

## Output identity

- A: `5858806E2204857A091AC9987244D84B9B91AC98606AB7A30D70F421447A5268`
- B: `B0664C216AAFBE53D2BE0B5988B0BEF30FC0402D10665A676443C8661BC5A659`
- C: `027BD550DCE76EAA94A332D99F8C9A26049831A3169809649A0E19690B8FA611`

Machine-readable reports are `reports/test_{a,b,c}_ghvc8_perf2.json`.
