# GAUD 0.1 / GAC1 specification

GAUD is the standalone audio format paired with GVID. The container extension is `.gaud`; the first codec is **GAC1**.

The design goals for GAC1 0.1 are simple decoding, good music quality, fast vectorized implementation, exact block seeking, and a specification small enough to reimplement without a third-party audio codec.

## File header (64 bytes, little-endian)

Format string: `<4sBBH4sIHHIIQQQ12s`

| Field | Type | Meaning |
|---|---:|---|
| magic | 4 bytes | `GAUD` |
| major | u8 | 0 |
| minor | u8 | 1 |
| header_size | u16 | 64 |
| codec | 4 bytes | `GAC1` |
| sample_rate | u32 | samples/second |
| channels | u16 | 1 or 2 |
| residual_bits | u16 | 6 or 8 |
| block_frames | u32 | currently 32 |
| quality | u32 | informational 1..100 |
| frame_count | u64 | PCM frames per channel |
| block_count | u64 | number of fixed blocks |
| data_offset | u64 | 64 |
| reserved | 12 bytes | zero |

## GAC1 block

Every block has exactly `block_frames` samples per channel. The last block is padded by repeating the final input sample; `frame_count` trims the decoded result.

For each channel, the block stores:

1. An exact signed 16-bit anchor sample (sample 0).
2. An unsigned 16-bit adaptive scale.
3. Quantized predictive residual codes for samples 1..N-1.

The block record is:

`anchors[channels] + scales[channels] + packed residual codes`

Anchors are little-endian `int16`; scales are little-endian `uint16`.

### Predictor / quantizer

Let `A` be the exact first sample of a channel and `x[i]` be a later source sample.

GAC1 quantizes the absolute level relative to the anchor:

`level[i] = round((x[i] - A) / scale)`

It then stores differences of those levels:

`q[0] = level[1]`

`q[i] = level[i+1] - level[i]`

The decoder reconstructs:

`level[i] = cumulative_sum(q)[i]`

`x_hat[i+1] = A + level[i] * scale`

This matters: quantization error is bounded around each source position instead of accumulating as drift across the block. GAD2 did not have this property and was audibly worse on music.

The scale is chosen independently for every block and channel so that all level differences fit the selected residual range.

### 8-bit HQ mode

Residual range is -127..127. Stored byte is `q + 128`.

With stereo, 32-frame blocks use:

- 4 bytes anchors
- 4 bytes scales
- 62 bytes residuals
- total 70 bytes, versus 128 bytes PCM16

This is about 54.7% of PCM16 before any future entropy stage.

### 6-bit Compact mode

Residual range is -31..31. Stored code is `q + 32` (0..63; zero is currently unused by normal samples).

Four 6-bit codes are packed little-endian into three bytes:

`v = c0 | (c1<<6) | (c2<<12) | (c3<<18)`

Then write `v` as 3 little-endian bytes. Groups are padded with the zero-residual code 32 when necessary.

For stereo 32-frame blocks this is about 43.8% of PCM16.

## Seeking

GAC1 uses fixed-size blocks, so the decoder can calculate the byte position of block `n` directly without scanning prior blocks. Future GAUD revisions can add a richer metadata/index section while keeping GAC1 blocks independently decodable.
