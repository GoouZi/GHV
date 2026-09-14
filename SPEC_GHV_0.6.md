# GHV 0.6 / GHVC6 Draft Specification

Status: experimental, little-endian.

## Container

GHV 0.6 retains the 96-byte `GHV1` container header and `VFRM` frame chunks used by 0.4/0.5. Header minor version is `6`.

Each video frame record stores:

- frame number;
- PTS in microseconds;
- frame type: `0=I`, `1=P`, `2=Repeat`;
- codec id: `6` for GHVC6;
- raw reconstructed YUV420 byte size;
- packed payload size;
- CRC32 of reconstructed YUV420 frame.

YUV420 frame layout is Y plane, then U, then V.

## GHVC6 frame prediction

### I frame

Each Y/U/V row uses horizontal byte prediction:

- first sample stored directly;
- later residual = current - previous sample modulo 256.

The resulting residual stream is packed using GBP6.

### P frame

Default range-0 prediction uses the previous reconstructed frame at the same coordinates. In this fast form, the P payload is a **GBP6 residual directly** (optionally wrapped in ZP06); no zero-filled motion map is stored.

A decoder identifies the form after removing an optional ZP06 wrapper:

- payload begins `GBP6`: zero-motion / same-position prediction;
- payload begins `GPM6`: local-motion prediction.

Optional GPM6 local motion uses a 32x32 luma macroblock grid. Each grid entry stores signed int8 `dx,dy`. Chroma uses the corresponding 16x16 block and vector components divided by two with truncation toward zero.

Residuals are signed modulo-256 differences. A quality-dependent dead-zone and residual step can be applied. Encoder reconstruction is closed-loop and must match decoder reconstruction exactly.

### Repeat frame

Payload size is zero and reconstructed frame equals the previous reconstructed frame.

## GBP6 residual payload

Header (`20 bytes`):

```text
4   magic = "GBP6"
2   block_size = 64
2   global_flags = 0
4   raw_size
4   block_count
2   chunk_blocks = 256
2   reserved = 0
```

Residual bytes are split into chunks of at most 256 64-byte blocks.

Chunk header (`12 bytes`):

```text
2   block_count_in_chunk
2   descriptor_bytes
4   payload_bytes
1   flags
3   reserved
```

Chunk flag bit 0: descriptor RLE.  
Chunk flag bit 1: block-local byte delta.

Descriptor mode is 4 bits per block when raw, or `(mode, run_minus_1)` byte pairs when RLE.

Modes:

```text
0       all zero
1..8    fixed bit width for 64 zig-zag signed values
9       sparse: 64-bit bitmap + one 8-bit zig-zag value per nonzero
10..15  Golomb-Rice k = mode - 10
```

Signed residual byte `s` is mapped to unsigned zig-zag code:

```text
s >= 0: 2*s
s <  0: -2*s - 1
```

Rice coding for each code `z`, parameter `k`:

```text
q = z >> k
r = z & ((1<<k)-1)
write q zero bits, then one bit, then k remainder bits
```

Bit order inside bytes is least-significant-bit first. Each Rice block is padded to the next byte boundary after 64 values.

When block-delta flag is set, each 64-byte residual block is transformed independently:

```text
d[0] = r[0]
d[i] = r[i] - r[i-1] mod 256
```

Decoder reverses this transform independently inside each block.

## GPM6 local motion payload

P frames may wrap a GBP6 residual in GPM6.

Header (`20 bytes`):

```text
4   magic = "GPM6"
2   block_size = 32
2   flags (bit0 = motion-map RLE)
2   grid_width
2   grid_height
4   motion_bytes
4   residual_bytes
```

Raw motion map is `(int8 dx, int8 dy)` per 32x32 luma grid cell in raster order.

RLE motion map uses 3-byte runs:

```text
int8 dx
int8 dy
uint8 run_minus_1
```

The residual payload immediately following the motion map is GBP6.

## ZP06 optional frame-payload wrapper

Any non-repeat GHVC6 payload may be wrapped in `ZP06` if this makes it smaller.

Header:

```text
4 magic = "ZP06"
4 unwrapped_size
```

Token stream:

- token high bit 1: zero run, length = `(token & 0x7f) + 3`;
- token high bit 0: literal run, length = `token + 1`, followed by that many literal bytes.

Encoder must use ZP06 only when the wrapped form is smaller than the original payload.

## Decoder requirements

A conforming GHVC6 decoder must:

- reconstruct closed-loop I/P/Repeat frames exactly according to this draft;
- validate bounds before reading descriptor/payload data;
- support CRC verification when requested;
- reject impossible block/chunk/motion dimensions;
- not require OpenMP; parallelism is an implementation detail, not part of the bitstream.

## Physical section order

Offsets in the 96-byte header are authoritative. A conforming reader must not assume audio precedes the index. The 0.6 direct native mux normally writes `header -> video frames -> index`, after which the wrapper may append embedded audio and patch `audio_offset`. This avoids moving/copying the complete video stream after encoding.
