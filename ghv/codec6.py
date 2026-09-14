from __future__ import annotations
import struct
import numpy as np
from .codec4 import intra_restore

P_MAGIC = b'GPM6'
P_HEAD_FMT = '<4sHHHHII'
P_HEAD_SIZE = struct.calcsize(P_HEAD_FMT)
MV_BLOCK = 32
FLAG_MV_RLE = 1

GBP_MAGIC = b'GBP6'
BLOCK = 64
HEAD_FMT = '<4sHHIIHH'
HEAD_SIZE = struct.calcsize(HEAD_FMT)
CHUNK_FMT = '<HHIB3x'
CHUNK_SIZE = struct.calcsize(CHUNK_FMT)
CHF_DESC_RLE = 1
CHF_BLOCK_DELTA = 2


def _unwrap_zp06(data: bytes) -> bytes:
    if len(data) < 8 or data[:4] != b'ZP06':
        return data
    raw = struct.unpack_from('<I', data, 4)[0]
    out = bytearray()
    p = 8
    while p < len(data) and len(out) < raw:
        t = data[p]; p += 1
        if t & 0x80:
            run = (t & 0x7f) + 3
            if len(out) + run > raw:
                raise ValueError('bad ZP06 zero run')
            out.extend(b'\0' * run)
        else:
            n = t + 1
            if p + n > len(data) or len(out) + n > raw:
                raise ValueError('bad ZP06 literal')
            out.extend(data[p:p+n]); p += n
    if p != len(data) or len(out) != raw:
        raise ValueError('ZP06 length mismatch')
    return bytes(out)


def _unzig_scalar(z: int) -> int:
    return (z >> 1) ^ -(z & 1)


def _desc_raw(blob: bytes, count: int) -> np.ndarray:
    a = np.frombuffer(blob, dtype=np.uint8)
    out = np.empty(len(a) * 2, dtype=np.uint8)
    out[0::2] = a & 15
    out[1::2] = a >> 4
    return out[:count]


def _desc_rle(blob: bytes, count: int) -> np.ndarray:
    if len(blob) & 1:
        raise ValueError('bad GBP6 descriptor RLE')
    out = np.empty(count, dtype=np.uint8)
    pos = 0
    for i in range(0, len(blob), 2):
        m = blob[i]
        run = blob[i + 1] + 1
        if m > 15 or pos + run > count:
            raise ValueError('bad GBP6 descriptor run')
        out[pos:pos+run] = m
        pos += run
    if pos != count:
        raise ValueError('short GBP6 descriptor RLE')
    return out


class _BitReader:
    def __init__(self, data: memoryview):
        self.data = data
        self.bit = 0

    def get1(self) -> int:
        if self.bit >= len(self.data) * 8:
            raise ValueError('truncated GBP6 Rice stream')
        v = (self.data[self.bit >> 3] >> (self.bit & 7)) & 1
        self.bit += 1
        return int(v)

    def getn(self, n: int) -> int:
        v = 0
        for i in range(n):
            v |= self.get1() << i
        return v

    def used_bytes(self) -> int:
        return (self.bit + 7) // 8


def bitunpack_residual6(data: bytes, expected: int) -> bytes:
    if len(data) < HEAD_SIZE:
        raise ValueError('truncated GBP6 payload')
    magic, block, flags, raw_size, blocks, chunk_blocks, reserved = struct.unpack_from(HEAD_FMT, data, 0)
    if magic != GBP_MAGIC or block != BLOCK or flags != 0 or raw_size != expected or not chunk_blocks:
        raise ValueError('unsupported GBP6 header')
    out = np.zeros(blocks * BLOCK, dtype=np.uint8)
    p = HEAD_SIZE
    first = 0
    mv = memoryview(data)

    while first < blocks:
        if p + CHUNK_SIZE > len(data):
            raise ValueError('truncated GBP6 chunk header')
        nblocks, desc_bytes, payload_bytes, cflags = struct.unpack_from(CHUNK_FMT, data, p)
        p += CHUNK_SIZE
        if not nblocks or nblocks > chunk_blocks or cflags & ~(CHF_DESC_RLE | CHF_BLOCK_DELTA):
            raise ValueError('bad GBP6 chunk header')
        if first + nblocks > blocks or p + desc_bytes + payload_bytes > len(data):
            raise ValueError('truncated GBP6 chunk')
        desc = bytes(mv[p:p+desc_bytes]); p += desc_bytes
        payload_end = p + payload_bytes
        modes = _desc_rle(desc, nblocks) if cflags & CHF_DESC_RLE else _desc_raw(desc, nblocks)
        if not (cflags & CHF_DESC_RLE) and desc_bytes != (nblocks + 1) // 2:
            raise ValueError('bad GBP6 descriptor size')
        if np.any(modes > 15):
            raise ValueError('bad GBP6 mode')

        for li, mode0 in enumerate(modes.tolist()):
            mode = int(mode0)
            base = (first + li) * BLOCK
            row = np.zeros(BLOCK, dtype=np.uint8)

            if 1 <= mode <= 8:
                need = 8 * mode
                if p + need > payload_end:
                    raise ValueError('truncated GBP6 fixed bitstream')
                packed = np.frombuffer(mv[p:p+need], dtype=np.uint8)
                bits = np.unpackbits(packed, bitorder='little')[:BLOCK*mode].reshape(BLOCK, mode)
                weights = (1 << np.arange(mode, dtype=np.uint16))
                codes = (bits.astype(np.uint16) * weights).sum(axis=1).astype(np.uint8)
                vals = np.array([_unzig_scalar(int(z)) & 0xff for z in codes], dtype=np.uint8)
                row[:] = vals
                p += need
            elif mode == 9:
                if p + 8 > payload_end:
                    raise ValueError('truncated GBP6 sparse bitmap')
                mask = np.unpackbits(np.frombuffer(mv[p:p+8], dtype=np.uint8), bitorder='little')[:BLOCK].astype(bool)
                p += 8
                nz = int(mask.sum())
                if p + nz > payload_end:
                    raise ValueError('truncated GBP6 sparse values')
                codes = np.frombuffer(mv[p:p+nz], dtype=np.uint8)
                vals = np.array([_unzig_scalar(int(z)) & 0xff for z in codes], dtype=np.uint8)
                row[mask] = vals
                p += nz
            elif 10 <= mode <= 15:
                k = mode - 10
                br = _BitReader(mv[p:payload_end])
                vals = np.empty(BLOCK, dtype=np.uint8)
                for j in range(BLOCK):
                    q = 0
                    while br.get1() == 0:
                        q += 1
                        if q > 255:
                            raise ValueError('invalid GBP6 Rice quotient')
                    rem = br.getn(k) if k else 0
                    z = (q << k) | rem
                    if z > 255:
                        raise ValueError('invalid GBP6 Rice value')
                    vals[j] = _unzig_scalar(z) & 0xff
                row[:] = vals
                p += br.used_bytes()

            if cflags & CHF_BLOCK_DELTA:
                row = (np.cumsum(row.astype(np.uint16)) & 255).astype(np.uint8)
            valid = min(BLOCK, max(0, expected - base))
            if valid:
                out[base:base+valid] = row[:valid]

        if p != payload_end:
            raise ValueError('trailing GBP6 chunk bytes')
        first += nblocks

    if first != blocks or p != len(data):
        raise ValueError('GBP6 length mismatch')
    return out[:expected].tobytes()


def _unpack_mv(blob: bytes, count: int, rle: bool) -> tuple[np.ndarray, np.ndarray]:
    x = np.empty(count, dtype=np.int8)
    y = np.empty(count, dtype=np.int8)
    if not rle:
        if len(blob) != count * 2:
            raise ValueError('bad GPM6 raw motion-map size')
        a = np.frombuffer(blob, dtype=np.int8).reshape(count, 2)
        return a[:, 0].copy(), a[:, 1].copy()
    if len(blob) % 3:
        raise ValueError('bad GPM6 RLE motion-map size')
    pos = 0
    for i in range(0, len(blob), 3):
        dx = np.int8(blob[i]).item()
        dy = np.int8(blob[i + 1]).item()
        run = blob[i + 2] + 1
        if pos + run > count:
            raise ValueError('bad GPM6 motion run')
        x[pos:pos + run] = dx
        y[pos:pos + run] = dy
        pos += run
    if pos != count:
        raise ValueError('short GPM6 motion map')
    return x, y


def unpack_p_payload(payload: bytes, width: int, height: int, expected: int):
    if len(payload) < P_HEAD_SIZE:
        raise ValueError('truncated GPM6 payload')
    magic, block, flags, gw, gh, mv_bytes, residual_bytes = struct.unpack_from(P_HEAD_FMT, payload, 0)
    egw = (width + MV_BLOCK - 1) // MV_BLOCK
    egh = (height + MV_BLOCK - 1) // MV_BLOCK
    if magic != P_MAGIC or block != MV_BLOCK or flags & ~FLAG_MV_RLE or gw != egw or gh != egh:
        raise ValueError('unsupported GPM6 header')
    if P_HEAD_SIZE + mv_bytes + residual_bytes != len(payload):
        raise ValueError('GPM6 length mismatch')
    mv_blob = payload[P_HEAD_SIZE:P_HEAD_SIZE + mv_bytes]
    x, y = _unpack_mv(mv_blob, gw * gh, bool(flags & FLAG_MV_RLE))
    residual = bitunpack_residual6(payload[P_HEAD_SIZE + mv_bytes:], expected)
    return x, y, residual


def _predict_plane(prev: np.ndarray, mvx: np.ndarray, mvy: np.ndarray, gw: int,
                   block: int, chroma: bool) -> np.ndarray:
    ph, pw = prev.shape
    out = np.empty_like(prev)
    pblock = block // 2 if chroma else block
    for by in range((ph + pblock - 1) // pblock):
        y0 = by * pblock; y1 = min(ph, y0 + pblock)
        for bx in range((pw + pblock - 1) // pblock):
            x0 = bx * pblock; x1 = min(pw, x0 + pblock)
            bi = by * gw + bx
            dx = int(mvx[bi]); dy = int(mvy[bi])
            if chroma:
                dx = int(dx / 2); dy = int(dy / 2)
            ys = np.clip(np.arange(y0, y1, dtype=np.int32) - dy, 0, ph - 1)
            xs = np.clip(np.arange(x0, x1, dtype=np.int32) - dx, 0, pw - 1)
            out[y0:y1, x0:x1] = prev[np.ix_(ys, xs)]
    return out


def restore_p(residual: bytes, prev: bytes, width: int, height: int,
              mvx: np.ndarray | None = None, mvy: np.ndarray | None = None) -> bytes:
    pa = np.frombuffer(prev, dtype=np.uint8)
    ra = np.frombuffer(residual, dtype=np.uint8)
    if mvx is None or mvy is None:
        return ((pa.astype(np.uint16) + ra.astype(np.uint16)) & 255).astype(np.uint8).tobytes()

    ysz = width * height
    cw, ch = width // 2, height // 2
    csz = cw * ch
    gw = (width + MV_BLOCK - 1) // MV_BLOCK
    py = pa[:ysz].reshape(height, width)
    pu = pa[ysz:ysz + csz].reshape(ch, cw)
    pv = pa[ysz + csz:].reshape(ch, cw)
    pred = np.empty_like(pa)
    pred[:ysz] = _predict_plane(py, mvx, mvy, gw, MV_BLOCK, False).ravel()
    pred[ysz:ysz + csz] = _predict_plane(pu, mvx, mvy, gw, MV_BLOCK, True).ravel()
    pred[ysz + csz:] = _predict_plane(pv, mvx, mvy, gw, MV_BLOCK, True).ravel()
    return ((pred.astype(np.uint16) + ra.astype(np.uint16)) & 255).astype(np.uint8).tobytes()


def decode_frame(frame_type: int, payload: bytes, prev: bytes | None, width: int, height: int,
                 raw_size: int, dx: int = 0, dy: int = 0) -> bytes:
    if frame_type == 2:
        if prev is None:
            raise ValueError('repeat frame without previous reference')
        return prev

    payload = _unwrap_zp06(payload)
    if frame_type == 0:
        return intra_restore(bitunpack_residual6(payload, raw_size), width, height)
    if frame_type == 1:
        if prev is None:
            raise ValueError('P frame without previous reference')
        if payload[:4] == P_MAGIC:
            mvx, mvy, residual = unpack_p_payload(payload, width, height, raw_size)
            return restore_p(residual, prev, width, height, mvx, mvy)
        residual = bitunpack_residual6(payload, raw_size)
        return restore_p(residual, prev, width, height)
    raise ValueError(f'bad GHVC6 frame type {frame_type}')
