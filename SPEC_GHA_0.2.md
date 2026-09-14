# GHA 0.2 draft specification

GHA = **Goou_Zi High-efficiency Audio**. Standard extension: `.gha`.

GHA 0.2 contains the custom **GHAC1** block predictive PCM codec. It is currently optimized for simplicity, deterministic decoding, and good music quality rather than state-of-the-art bitrate.

## Header

64 bytes, little-endian:

`<4sBBH4sIHHIIQQQ12s>`

- container magic: `GHAF`
- version: `0.2`
- codec identifier: `GHA1` (GHAC1)
- sample rate
- channels (1 or 2)
- residual code width (6 or 8 in 0.2)
- block size in PCM frames
- quality metadata
- PCM frame count
- block count
- data offset

## GHAC1 blocks

Default block size is 32 PCM frames. Each channel stores:

1. an exact signed 16-bit anchor sample;
2. an unsigned 16-bit adaptive scale;
3. quantized predictive level differences for the remaining samples.

HQ uses 8-bit codes. Compact uses packed 6-bit codes. Predictive levels are reconstructed relative to the exact block anchor, limiting long-term drift compared with the retired GAD1/GAD2 experiments.

## Status

GHA 0.2 is experimental. Future GHAC revisions are expected to add stereo decorrelation, transform coding and better entropy coding while keeping a low-complexity decode profile.
