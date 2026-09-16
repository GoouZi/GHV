# GHV 0.9 / GHVC9 Bitstream Specification

Status: experimental development profile. GHV container layout, 64-bit index
offsets, frame headers, CRC semantics, GHAC1, I-frame `GTC7`, and Repeat frames
are unchanged from GHV 0.8. This document defines codec id 9 P frames.

## Design goals

GHVC9 Milestone 1 reduces coefficient rate and full-RD work without increasing
quantization. It retains GHVC8 16x16 even-pixel motion compensation, median MV
delta syntax, 8x8 transform, reconstruction, CRC, and quality tables.

## P payload header

Integers are little-endian. A normal GHVC9 P payload begins with `GBP9`:

| Offset | Type | Meaning |
|---:|---|---|
| 0 | char[4] | `GBP9` |
| 4 | u8 | motion block size, 16 |
| 5 | u8 | quality, 1..100 |
| 6 | u8 | even-pixel search range |
| 7 | u8 | flags, zero |
| 8 | u32 | reconstructed YUV420 byte count |
| 12 | u32 | total 8x8 transform block count |
| 16 | u32 | 16x16 motion-vector count |
| 20 | u32 | motion-body byte count |

The delta-coded motion body and one-bit zero-residual descriptor map immediately
follow and are identical to GTP8. Encoder presets change candidate search effort
only; a decoder has no preset-specific behavior.

## Chunk table

Coefficients are divided into consecutive groups of 256 transform blocks.
After the descriptor map:

1. `u32 chunk_count`, exactly `ceil(block_count / 256)`;
2. `chunk_count` little-endian `u32` compressed byte sizes;
3. the byte-aligned chunk payloads in order.

All counts, additions, and payload boundaries must be validated before decode.
Each chunk is independently bit-decodable and may be parsed in parallel.

## Coefficient bits

Zero-residual blocks consume no coefficient bits. Every nonzero block writes:

- 6-bit zig-zag last-index/EOB;
- repeated `(zero_run, signed_level)` pairs until EOB.

`zero_run` uses capped unary: runs 0..6 are `run` zero bits followed by one;
larger runs write seven zero bits, one, then the 6-bit run value.

The nonzero signed level is zig-zag mapped to an unsigned integer and coded by
Rice `k=2`. Quotients 0..30 are unary followed by two remainder bits. Larger
values write quotient escape 31 followed by the full 32-bit zig-zag value.
Unused bits at a chunk end are zero padding.

## Search presets

Presets do not alter syntax or quantization rules:

- Fast: zero motion plus one sampled-SAD finalist, with the existing Fast
  quality/search settings;
- Balanced: zero motion plus the best sampled-SAD nonzero finalist;
- Quality: zero motion plus up to three nonzero finalists.

All finalists use exact reconstructed rate-distortion evaluation and the same
branch-and-bound rule before selection.

## Compatibility and failure behavior

Codec ids 4 through 8 retain their existing decoders. GHVC9 uses the existing
GHV frame index, CRC-32 of the reconstructed frame, 64-bit container offsets,
seek, EOF, and repair rules. A decoder must reject truncated motion data,
invalid chunk counts/sizes, invalid unary/Rice codes, out-of-range runs, payload
trailing bytes, invalid dimensions, or reconstructed CRC mismatch.
