# GHV 0.5 / GHVC4 Bitstream Specification

Status: experimental. All integer fields are little-endian unless stated otherwise.

## 1. Container identity

File extension: `.ghv`  
Container magic: ASCII `GHV1`  
Current header version: major `0`, minor `5`  
Video codec ID: `4` = GHVC4  
Embedded audio codec ID: `4` = GHAC1/GHA 0.2 payload

The fixed GHV header remains 96 bytes in 0.5. Its binary field layout is the same container layout introduced by GHV 0.4; version 0.5 changes the preferred video codec to GHVC4.

## 2. GHV fixed header

Python struct notation:

```text
<4sBBHIIIIIIIIIHHQQQQQ8s
```

Fields in order:

```text
magic[4]          = "GHV1"
major             = 0
minor             = 5
header_size       = 96
flags
width
height
fps_num
fps_den
frame_count
keyint
quality
audio_rate
audio_channels
audio_codec
audio_samples
frames_offset
audio_offset
index_offset
duration_us
reserved[8]
```

`flags & 1` indicates embedded audio is present.

YUV420 requires even width and height.

## 3. Video frame record

Each frame begins with a 32-byte record:

```text
<4sIQBBHIII
```

```text
magic[4]      = "VFRM"
frame_number  u32
pts_us        u64
frame_type    u8
codec_id      u8  (4 for GHVC4)
meta          u16
raw_size      u32
packed_size   u32
crc32         u32
payload       packed_size bytes
```

`raw_size` for YUV420p is `width * height * 3 / 2`.

Frame types:

- `0`: I frame
- `1`: P frame
- `2`: Repeat frame

For P frames, `meta` contains signed 8-bit `dx` in bits 0..7 and signed 8-bit `dy` in bits 8..15. Default 0.5 presets use `(0,0)` because whole-frame motion search is currently experimental.

For repeat frames, `packed_size` is zero and the reconstructed frame is exactly the previous reconstructed frame.

CRC32 is calculated over the **reconstructed YUV420 frame**, not over the compressed payload.

## 4. GHVC4 prediction

### 4.1 Quantization

The source frame is scalar-quantized independently for luma and chroma according to the quality setting. The decoder does not repeat this step; the quantized reconstructed frame is what the encoder predicts from and what CRC protects.

### 4.2 I frames

Each Y, U and V row uses horizontal prediction:

- first byte of a row is stored directly;
- remaining bytes are modulo-256 differences from the previous pixel in that row.

The resulting residual byte stream is packed with GBP4.

### 4.3 P frames

A prediction frame is produced from the previous reconstructed frame. With `(dx,dy) = (0,0)` this is direct temporal prediction. Experimental non-zero motion shifts each plane with edge clamping; chroma uses integer `dx/2, dy/2`.

Residual bytes are modulo-256 signed differences between current quantized bytes and predicted bytes. Quality settings below 76 may zero very small residuals. The encoder performs closed-loop reconstruction from the residual and stores that reconstruction as the next reference.

Before GBP4, P residual streams may use the reversible byte-delta transform described below.

## 5. GBP4 residual packer

GBP4 operates on 64-byte blocks.

Header:

```text
<4sHHIII
magic[4]          = "GBP4"
block_size        = 64
flags             u16
raw_size          u32
block_count       u32
descriptor_bytes  u32
```

Flags:

- bit 0 (`1`): descriptor map is RLE-coded
- bit 1 (`2`): residual stream used byte-delta pre-transform

Unknown flag bits are invalid in 0.5.

### 5.1 Signed mapping

Residual bytes are interpreted as signed int8 and zig-zag mapped to unsigned values:

```text
 0 -> 0
-1 -> 1
+1 -> 2
-2 -> 3
+2 -> 4
...
```

### 5.2 Block modes

Each 64-byte block has a 4-bit mode:

- `0`: all mapped values are zero, no payload
- `1..8`: every mapped value is stored with exactly that many bits
- `9`: sparse block, store 64-bit non-zero bitmap followed by only non-zero mapped bytes
- `10..15`: reserved/invalid

Sparse mode is selected when `8 + nonzero_count` bytes is smaller than the equivalent fixed-width bit payload.

### 5.3 Descriptor map

Raw descriptors pack two 4-bit modes per byte.

RLE descriptors use pairs:

```text
mode:u8, run_minus_one:u8
```

A run length is therefore 1..256. The encoder writes RLE only if it is smaller than the raw nibble descriptor map.

### 5.4 Fixed-width payload order

Payload for modes 1 through 8 is written in increasing mode order. Within each selected block, its 64 codes are bit-packed little-bit-first. A block with width `w` consumes exactly `8*w` bytes.

Mode-9 sparse blocks follow after all fixed-width groups. Each sparse block stores:

```text
bitmap[8]
nonzero_values[popcount(bitmap)]
```

### 5.5 Byte-delta transform

If flag bit 1 is set, the source residual bytes `r[]` were replaced before block coding by:

```text
d[0] = r[0]
d[i] = (r[i] - r[i-1]) mod 256
```

After GBP4 decode, the original residual is restored by cumulative modulo-256 addition.

This transform is lossless and is currently used for P residuals when selected by GHVC4.

## 6. Index

At `index_offset`:

```text
magic[4] = "INDX"
count:u32
```

followed by `count` entries:

```text
offset:u64
frame_type:u8
reserved[7]
```

The index enables direct keyframe lookup and structural verification.

## 7. Embedded audio

If present, `audio_offset` points to:

```text
magic[4] = "AUD0"
codec_id:u32 = 4
payload_size:u64
audio_samples:u64
payload[payload_size]
```

The payload is a complete GHA/GHAC1 bitstream as documented in `SPEC_GHA_0.2.md`.

## 8. Decoder requirements

A conforming GHVC4 decoder must:

1. reject impossible dimensions/header sizes;
2. decode frame records in dependency order;
3. reject P/repeat frames without a previous reconstructed reference;
4. reject invalid GBP4 modes/lengths/flags;
5. reconstruct with modulo-256 arithmetic as specified;
6. optionally verify frame CRC32;
7. never use source pixels or encoder-only state to decode a frame.

The reference native decoder is `native/ghvdecode.cpp`.
