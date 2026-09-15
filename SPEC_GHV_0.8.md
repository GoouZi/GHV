# GHV 0.8 / GHVC8 Bitstream Specification

GHV 0.8 preserves the 96-byte `GHV1` container, `VFRM`, `AUD0`, and `INDX`
records specified by GHV 0.7. Newly encoded files set both the container minor
version and video codec id to `8`. Older codec ids remain valid and decodable.
All integers are little-endian. Decoder behavior is scalar and does not depend
on SIMD or thread count.

## Frame types

- I (`type=0`): the complete GTC7 transform payload from GHV 0.7. Keeping this
  syntax makes the intra reconstruction rules identical and auditable.
- P (`type=1`): the new GTP8 motion/transform payload below.
- Repeat (`type=2`): no payload; reuse the previous reconstructed frame.

CRC32 in `VFRM` always covers the complete reconstructed YUV420p frame.

## GTP8 P-frame header

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII `GTP8` |
| 4 | 1 | luma motion-block size; currently 16 |
| 5 | 1 | quality, 1..100 |
| 6 | 1 | maximum encoded motion range |
| 7 | 1 | flags; currently zero |
| 8 | 4 | reconstructed raw YUV byte count |
| 12 | 4 | 8x8 transform-block count |
| 16 | 4 | 16x16 motion-vector count |
| 20 | 4 | motion section byte count |

The header is followed by the motion section, one-bit-per-transform-block SKIP
map, then coefficient data for non-SKIP blocks.

## Motion vectors

The luma frame is divided into raster-ordered 16x16 blocks. Edge blocks are
clamped. Vectors are signed, even, full-luma-pixel offsets. Chroma uses the
same vector divided by two.

The predictor is the component-wise median of left, top, and top-right. Missing
neighbors are zero; when top-right is missing it equals top. Each vector record
is:

- byte `0`: predictor hit; delta `(0,0)`;
- byte `1`: signed-varint `dx`, then signed-varint `dy`.

Signed values use zig-zag mapping (`0, -1, +1, -2, +2...`) followed by unsigned
LEB128. The reconstructed vector is predictor plus delta.

Encoder mode selection is not normative. The reference encoder shortlists
local even-pixel candidates by sampled SAD, then minimizes reconstructed
distortion plus a coded-byte cost across the four luma and two chroma transform
blocks. This is an RD decision, not smallest-bitstream-only selection.

## Transform blocks and SKIP

Blocks are ordered as all Y blocks, then U, then V, each in raster order. Each
plane uses 8x8 blocks and edge replication exactly as GTC7. Prediction samples
come from the previous reconstructed frame at the selected motion offset.

The descriptor map contains one bit per transform block, LSB first. `1` means
SKIP/zero residual: write no coefficient bytes and reconstruct prediction
directly. `0` means coded residual. The forward/inverse sequency-ordered 8x8
integer WHT, quantization table, zig-zag order, and reconstruction clipping are
identical to GTC7.

## Compact run/level coding

A coded block begins with one byte holding the final zig-zag index, 0..63.
Every non-zero coefficient is then represented by a token:

- Compact token: high bit set. Bits 6..4 are zero-run 0..7; bits 3..0 store
  `zigzag(level)-1`, supporting mapped values 1..16 in one byte.
- Escape token: byte zero, followed by unsigned-varint run and unsigned-varint
  zig-zag level.

Tokens with high bit clear and a non-zero value are reserved and invalid.
This coding changes rate only: coefficient values and reconstructed pixels are
unchanged compared with the equivalent escaped representation.

## Limits and validation

The reference decoder rejects invalid magic, version fields, dimensions,
counts, vector range/parity, truncated sections, invalid tokens, impossible
runs, coefficient ends above 63, trailing data, and CRC mismatch. A P or Repeat
frame without a prior reconstructed frame is invalid.

## Playback timing profile

The reference player treats decoded audio samples at their declared sample rate
as invariant. Video is scheduled against the same monotonic timeline: early
frames wait, on-time frames display, and late video frames may be dropped.
Audio rate must never be changed to compensate for video decode or render load.
Queues are bounded and a fatal decoder error terminates both presentation paths.
