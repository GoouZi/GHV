# GVID 0.1 binary specification

All integer fields are little-endian.

## File header (96 bytes)

Magic is `GVID`. Version is `0.1`.

Fields in order:

- magic[4]
- major u8
- minor u8
- header_size u16
- flags u32 (`bit0 = audio present`)
- width u32
- height u32
- fps_num u32
- fps_den u32
- frame_count u32
- keyint u32
- quality u32
- audio_rate u32
- audio_channels u16
- audio_codec u16 (`0 none`, `1 GAD1`)
- audio_samples u64
- frames_offset u64
- audio_offset u64
- index_offset u64
- duration_us u64
- reserved[8]

## Video frame chunk

Each frame begins with a 32-byte `VFRM` header:

- magic `VFRM`
- frame_no u32
- pts_us u64
- frame_type u8 (`0 I`, `1 P`)
- codec u8 (`1 GVC1`)
- reserved u16
- raw_size u32
- packed_size u32
- fnv1a32_of_reconstructed_yuv u32
- payload[packed_size]

## GVC1

1. RGB24 is converted to integer YUV 4:2:0.
2. Y and chroma are scalar-quantized according to the quality setting.
3. I-frame predictor: each plane row uses the previous sample in the same row. The residual is modulo 256.
4. P-frame predictor: previous reconstructed frame, byte-for-byte in YUV420. Residual is modulo 256.
5. The encoder tries both I and P (unless keyframe-forced) and selects P only when it is meaningfully smaller.
6. Residual bytes are coded with GZR1.

### GZR1 packetization

Each token uses the top two bits as a type and the low six bits as `length-1` (1..64):

- `00llllll`: literal residual run, followed by raw bytes.
- `01llllll`: zero residual run, no payload.
- `10llllll`: small signed-residual run. Residual bytes are interpreted as signed modulo-256 deltas, zigzag-mapped to 0..15, then packed two 4-bit values per byte.
- `11xxxxxx`: reserved for a future entropy/back-reference mode.

This makes tiny changes (±1..±7) substantially cheaper without using DEFLATE, zlib, VPx, H.26x or AV1.

## Audio chunk

`AUD0` header:

- magic `AUD0`
- codec u32 (`1 GAD1`)
- packed_size u64
- sample_frames u64
- codec payload

GAD1 uses independent adaptive predictors per channel and stores two signed 4-bit delta codes per byte. It is experimental in 0.1.

## Index

At `index_offset`:

- magic `INDX`
- count u32
- repeated entries:
  - absolute frame chunk offset u64
  - frame_type u8
  - reserved[7]

The index lets a player jump backward to the nearest I-frame and decode forward.
