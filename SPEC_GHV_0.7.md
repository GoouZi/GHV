# GHV 0.7 / GHVC7 Draft Specification

Status: experimental, little-endian.

GHVC7 is a native, in-tree transform video codec. It does not embed or call an
existing video codec. GHV 0.7 retains the 96-byte `GHV1` container and `VFRM`
record/index layout from GHV 0.6. The container minor version and per-frame
codec id are both `7`.

## Frame types

- `0` — I frame. Each 8x8 block selects DC, vertical, or horizontal spatial
  prediction from already reconstructed neighboring blocks.
- `1` — P frame. Each block predicts from the same position in the previous
  reconstructed frame.
- `2` — Repeat. Payload size is zero and the reconstructed frame is the
  previous reconstructed frame.

The current GHVC7 profile does not write motion vectors. The `VFRM.meta` field
is zero and remains reserved for a future versioned motion profile. A sampled
source-frame comparison may select Repeat, while scene changes and the maximum
keyframe interval select I frames.

## GTC7 transform payload

Each non-Repeat frame contains one `GTC7` payload:

```text
4  magic = "GTC7"
1  transform block size = 8
1  quality (1..100)
1  flags = 0
1  reserved = 0
4  reconstructed raw YUV420 byte size
4  total transform block count
N  packed block descriptors
M  run/level coefficient data
```

Blocks are raster ordered: all Y blocks, then U, then V. Plane edges that do
not fill 8x8 are extended by repeating the final valid row/column during the
transform. Only valid pixels are written during reconstruction.

The expected block count is:

```text
ceil(width/8) * ceil(height/8)
+ 2 * ceil((width/2)/8) * ceil((height/2)/8)
```

## Prediction

The 2-bit predictor values are:

```text
0  temporal: previous reconstructed frame, same pixel (P only)
1  DC: rounded mean of available top and left block-edge samples
2  vertical: reconstructed sample directly above
3  horizontal: reconstructed sample directly left
```

Unavailable I-frame neighbors predict 128. The encoder evaluates the three I
predictors and selects the one with the smallest estimated coded coefficient
size. Reconstruction is closed-loop and clipped to 0..255.

## Integer transform

GHVC7 uses a separable 8x8 integer Walsh-Hadamard transform. A 1-D transform
uses butterfly stages of length 1, 2, and 4. The natural outputs are permuted
into sequency order by:

```text
0, 4, 6, 2, 3, 7, 5, 1
```

The inverse unpermutes, applies the same butterfly transform in both axes, and
rounds the result after division by 64. Encoder and decoder use identical
integer arithmetic.

## Quantization

The scalar base value derived from quality is:

```text
quality >= 96: 1
quality >= 88: 2
quality >= 82: 3
quality >= 79: 4
quality >= 75: 5
quality >= 70: 7
quality >= 62: 9
otherwise:    13
```

For coefficient `(u,v)`, `frequency = u + v`:

```text
step = round(base * 8 * (16 + 2*frequency + (frequency >= 8 ? 4 : 0)) / 16)
```

Chroma steps are then multiplied by 5/4 with integer rounding. Quantized
coefficients and inverse-dequantized coefficients are signed integers.

## Descriptor and coefficient coding

Each block descriptor is 3 bits, packed least-significant-bit first across the
descriptor byte array:

```text
bits 0..1  predictor
bit 2      zero-coefficient block
```

For a zero block there is no coefficient data. Otherwise:

1. coefficients are scanned with the normative 8x8 zig-zag order;
2. trailing zeros are removed;
3. one byte stores the last retained zig-zag index (`0..63`);
4. each nonzero coefficient stores an unsigned varint zero-run followed by an
   unsigned varint signed level.

Signed level mapping:

```text
level >= 0: 2 * level
level <  0: -2 * level - 1
```

Varints use seven data bits per byte and bit 7 as continuation. Decoders must
reject varints longer than five bytes, zero encoded levels, impossible runs,
levels above the implementation safety bound, truncated data, and trailing
payload bytes.

## Decoder procedure

For each frame, a decoder validates the `VFRM` bounds and codec id. Repeat
frames reuse the previous reconstruction. For I/P frames it validates the
`GTC7` header and deterministic descriptor size, parses every block, inverse
quantizes and transforms coefficients, applies the predictor, clips pixels,
and optionally verifies the `VFRM` CRC32.

Parallel block reconstruction is an implementation detail. A scalar C++17
decoder is normative and OpenMP is optional. P blocks may be reconstructed in
parallel because their prediction uses only the previous frame; I blocks retain
raster dependencies.

## Compatibility and error handling

GHVC4/5/6 decoders remain in-tree. A GHVC7-capable player must stop or recover
the complete A/V pipeline on fatal video decode failure; it must not continue
audio indefinitely with a frozen last video frame. CRC verification is
optional during normal playback but required by `ghvverify` and benchmark
validation.
