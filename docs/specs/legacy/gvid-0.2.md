# GVID 0.2 bitstream notes

The outer container remains compatible with the 0.1 96-byte header and `VFRM` / `AUD0` / `INDX` chunks. Files written by the new encoder use header version `0.2`.

## Video codec id 2 — GVC2

Input/reconstruction format is planar 8-bit YUV420p. Each frame is either:

- type 0: I frame, horizontal prediction reset on every plane row;
- type 1: P frame, modulo-256 difference from the previous reconstructed frame.

Scene cuts are detected using a sampled luma SAD so the encoder does not need to fully encode both I and P candidates.

### GBL2 residual packet

Each predicted residual byte stream is split into 64-byte blocks. A block mode is two bits:

- `0`: all zero; no block payload;
- `1`: all signed residuals fit `[-8,+7]`; zigzag values are packed two per byte;
- `2`: raw 64-byte modulo-256 residual;
- `3`: reserved.

Modes are packed four per byte. Small-block payloads are stored contiguously in block order, followed by raw-block payloads in block order. This layout is deliberately friendly to vectorized/SIMD implementations.

GBL2 header (`little endian`):

`<4sHHIIII>` = magic `GBL2`, block size, flags, original residual size, block count, small-block count, raw-block count.

The `VFRM` checksum field is CRC-32 for codec id 2. Codec id 1 continues to use the old FNV-1a checksum.

## Audio codec id 2 — GAD2

GAD2 is an experimental 4-bit block-reset delta codec. Blocks reset their first sample and per-channel step to cap drift, then pack quantized sample deltas into nibbles. It is intentionally lightweight and will likely change again.
