from __future__ import annotations
import struct
import numpy as np

# GHVC5 residual payload (GBP5), introduced by GHV 0.6.
# The stream is split into independently decodable chunks so native encoder and
# decoder can parallelize HD frames without changing the simple GHVC prediction
# model.

MAGIC = b'GBP5'
BLOCK = 64
HEAD_FMT = '<4sHHIIHH'   # magic, block, flags, raw_size, blocks, chunk_blocks, reserved
HEAD_SIZE = struct.calcsize(HEAD_FMT)  # 20
CHUNK_FMT = '<HHIB3x'    # nblocks, desc_bytes, payload_bytes, flags
CHUNK_SIZE = struct.calcsize(CHUNK_FMT)  # 12
CHF_DESC_RLE = 1
CHF_BLOCK_DELTA = 2


def _unzig(z: np.ndarray) -> np.ndarray:
    z16 = z.astype(np.int16)
    return ((z16 >> 1) ^ -(z16 & 1)).astype(np.int8).view(np.uint8)


def _desc_raw(blob: bytes, count: int) -> np.ndarray:
    a = np.frombuffer(blob, dtype=np.uint8)
    out = np.empty(len(a) * 2, dtype=np.uint8)
    out[0::2] = a & 15
    out[1::2] = a >> 4
    return out[:count]


def _desc_rle(blob: bytes, count: int) -> np.ndarray:
    if len(blob) & 1:
        raise ValueError('bad GBP5 RLE descriptor stream')
    out = np.empty(count, dtype=np.uint8)
    pos = 0
    for i in range(0, len(blob), 2):
        mode = blob[i]
        run = blob[i + 1] + 1
        if mode > 9 or pos + run > count:
            raise ValueError('bad GBP5 RLE descriptor')
        out[pos:pos + run] = mode
        pos += run
    if pos != count:
        raise ValueError('short GBP5 RLE descriptor stream')
    return out


def bitunpack_residual(data: bytes, expected: int) -> bytes:
    if len(data) < HEAD_SIZE:
        raise ValueError('truncated GBP5 payload')
    magic, block, flags, raw_size, blocks, chunk_blocks, reserved = struct.unpack_from(HEAD_FMT, data, 0)
    if magic != MAGIC or block != BLOCK or flags != 0 or raw_size != expected or not chunk_blocks:
        raise ValueError('unsupported GBP5 header')
    out = np.zeros(blocks * BLOCK, dtype=np.uint8)
    p = HEAD_SIZE
    first = 0
    mv = memoryview(data)
    while first < blocks:
        if p + CHUNK_SIZE > len(data):
            raise ValueError('truncated GBP5 chunk header')
        nblocks, desc_bytes, payload_bytes, cflags = struct.unpack_from(CHUNK_FMT, data, p)
        p += CHUNK_SIZE
        if not nblocks or nblocks > chunk_blocks or cflags & ~(CHF_DESC_RLE | CHF_BLOCK_DELTA):
            raise ValueError('bad GBP5 chunk header')
        if first + nblocks > blocks or p + desc_bytes + payload_bytes > len(data):
            raise ValueError('truncated GBP5 chunk')
        desc = bytes(mv[p:p + desc_bytes]); p += desc_bytes
        payload_end = p + payload_bytes
        modes = _desc_rle(desc, nblocks) if cflags & CHF_DESC_RLE else _desc_raw(desc, nblocks)
        if not (cflags & CHF_DESC_RLE) and desc_bytes != (nblocks + 1) // 2:
            raise ValueError('bad GBP5 descriptor size')
        if np.any(modes > 9):
            raise ValueError('bad GBP5 mode')
        for li, mode0 in enumerate(modes.tolist()):
            mode = int(mode0)
            base = (first + li) * BLOCK
            row = np.zeros(BLOCK, dtype=np.uint8)
            if 1 <= mode <= 8:
                need = 8 * mode
                if p + need > payload_end:
                    raise ValueError('truncated GBP5 bitstream')
                packed = np.frombuffer(mv[p:p + need], dtype=np.uint8)
                bits = np.unpackbits(packed, bitorder='little')[:BLOCK * mode].reshape(BLOCK, mode)
                weights = (1 << np.arange(mode, dtype=np.uint16))
                codes = (bits.astype(np.uint16) * weights).sum(axis=1).astype(np.uint8)
                row[:] = _unzig(codes)
                p += need
            elif mode == 9:
                if p + 8 > payload_end:
                    raise ValueError('truncated GBP5 sparse bitmap')
                mask = np.unpackbits(np.frombuffer(mv[p:p + 8], dtype=np.uint8), bitorder='little')[:BLOCK].astype(bool)
                p += 8
                nz = int(mask.sum())
                if p + nz > payload_end:
                    raise ValueError('truncated GBP5 sparse values')
                codes = np.frombuffer(mv[p:p + nz], dtype=np.uint8)
                row[mask] = _unzig(codes)
                p += nz
            if cflags & CHF_BLOCK_DELTA:
                row = (np.cumsum(row.astype(np.uint16)) & 255).astype(np.uint8)
            valid = min(BLOCK, max(0, expected - base))
            if valid:
                out[base:base + valid] = row[:valid]
        if p != payload_end:
            raise ValueError('trailing GBP5 chunk bytes')
        first += nblocks
    if first != blocks or p != len(data):
        raise ValueError('GBP5 length mismatch')
    return out[:expected].tobytes()


def decode_frame(frame_type: int, payload: bytes, prev: bytes | None, width: int, height: int,
                 raw_size: int, dx: int = 0, dy: int = 0) -> bytes:
    from .codec4 import intra_restore, temporal_restore_motion
    if frame_type == 2:
        if prev is None:
            raise ValueError('repeat frame without previous reference')
        return prev
    residual = bitunpack_residual(payload, raw_size)
    if frame_type == 0:
        return intra_restore(residual, width, height)
    if frame_type == 1:
        if prev is None:
            raise ValueError('P frame without previous reference')
        return temporal_restore_motion(residual, prev, width, height, dx, dy)
    raise ValueError(f'bad GHVC5 frame type {frame_type}')
