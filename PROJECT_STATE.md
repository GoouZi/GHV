# GHV / GHA Project State

Last updated: 2026-09-14

This is the handoff file for a future ChatGPT/Codex session or human contributor.

## Identity

- GHV = Goou_Zi High-efficiency Video, `.ghv`
- GHA = Goou_Zi High-efficiency Audio, `.gha`
- Current development release: **GHV 0.7 / GHA 0.2**
- Current native video codec: **GHVC7** (GHVC4/5/6 remain decodable; GHVC6 remains encodable with `--codec 6`)
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

User feedback on 0.5:

- conversion of 1080p30 is still too slow;
- large GHV files can stutter;
- one higher-resolution/large file could freeze visually while audio continued;
- a lower-resolution file played smoothly.

This strongly points to HD decoder/presentation throughput in addition to compression inefficiency.

Test B is a separate 1920x1080/30 performance and playback test. Do not mix
its values with Test A. The 2026-09-14 outputs were 1350.329 MiB (GHVC6) and
906.208 MiB (GHVC7).

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

## Reproduce

```text
python ghvbench.py input.mp4 output.ghv --codec 7 --preset balanced --quality-metrics --decode-frames 0 --report-json report.json
python ghvdoctor.py output.ghv --frames 999999 --verify
python ghvplay.py output.ghv --engine native
```

## Next major milestone

GHVC7 achieved the first architecture-level reduction and the `<500 MiB` Test A
stage, but remains far from OGV/Theora. Highest-value next work:

1. coded-cost local motion and motion-vector prediction in a versioned GHVC7 profile;
2. replace varint levels with measured Rice/canonical Huffman/range-style coding;
3. reduce encoder allocations and parallelize/pipe I-frame work;
4. add CRF-like rate control and formal Fast/Balanced/Quality/Compact curves;
5. expose `libghv` decoder/player APIs and remove the long-term ffplay dependency;
6. capture peak memory, CPU utilization, and rendered dropped-frame/A/V drift metrics.
