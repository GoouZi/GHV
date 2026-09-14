from __future__ import annotations
import struct
import numpy as np

# GHVC3 — Goou_Zi High-efficiency Video Codec 3
#
# Goals for 0.4:
#   * cut the huge GVC2 residual stream without borrowing zlib/VPx/AV1/etc.
#   * keep decoding deliberately simple and portable
#   * add cheap global motion compensation for pans / camera movement
#   * add repeat frames for static material
#
# Pipeline:
#   YUV420p -> scalar quantization -> I horizontal predictor OR
#   P global-motion predictor -> signed residual -> per-64-byte adaptive
#   bit-width packing (GBP3).
#
# GBP3 groups blocks by required zig-zag bit width 0..8. A width-0 block is
# completely free (all residuals are zero); a width-3 block costs only 24 bytes
# instead of GVC2's 64 raw bytes. No external compression library is involved.

MAGIC = b'GBP3'
BLOCK = 64
HEAD_FMT = '<4sHHII'
HEAD_SIZE = struct.calcsize(HEAD_FMT)  # 16
_BITLEN = np.array([int(i).bit_length() for i in range(256)], dtype=np.uint8)


def quality_steps(quality: int) -> tuple[int, int]:
    q = max(1, min(100, int(quality)))
    if q >= 97: return 1, 1
    if q >= 92: return 1, 2
    if q >= 86: return 2, 3
    if q >= 78: return 2, 4
    if q >= 70: return 3, 5
    if q >= 62: return 4, 7
    if q >= 54: return 5, 8
    if q >= 46: return 6, 10
    return 8, 12


def quantize_yuv420(frame: bytes, width: int, height: int, quality: int) -> bytes:
    ysz = width * height
    csz = ysz // 4
    if len(frame) != ysz + 2 * csz:
        raise ValueError('bad YUV420 frame size')
    ys, cs = quality_steps(quality)
    if ys == 1 and cs == 1:
        return frame
    a = np.frombuffer(frame, dtype=np.uint8)
    out = np.empty_like(a)
    y = a[:ysz].astype(np.uint16)
    if ys > 1:
        y = np.minimum(((y + ys // 2) // ys) * ys, 255)
    out[:ysz] = y.astype(np.uint8)
    c = a[ysz:].astype(np.uint16)
    if cs > 1:
        c = np.minimum(((c + cs // 2) // cs) * cs, 255)
    out[ysz:] = c.astype(np.uint8)
    return out.tobytes()


def intra_residual(frame: bytes, width: int, height: int) -> bytes:
    a = np.frombuffer(frame, dtype=np.uint8)
    out = np.empty_like(a)
    ysz = width * height
    cw, ch = width // 2, height // 2
    csz = cw * ch
    pos = 0
    for pw, ph, size in ((width, height, ysz), (cw, ch, csz), (cw, ch, csz)):
        p = a[pos:pos + size].reshape(ph, pw)
        d = out[pos:pos + size].reshape(ph, pw)
        d[:, 0] = p[:, 0]
        d[:, 1:] = (p[:, 1:].astype(np.int16) - p[:, :-1].astype(np.int16)).astype(np.uint8)
        pos += size
    return out.tobytes()


def intra_restore(residual: bytes, width: int, height: int) -> bytes:
    a = np.frombuffer(residual, dtype=np.uint8)
    out = np.empty_like(a)
    ysz = width * height
    cw, ch = width // 2, height // 2
    csz = cw * ch
    pos = 0
    for pw, ph, size in ((width, height, ysz), (cw, ch, csz), (cw, ch, csz)):
        p = a[pos:pos + size].reshape(ph, pw).astype(np.uint16)
        out[pos:pos + size] = (np.cumsum(p, axis=1) & 255).astype(np.uint8).ravel()
        pos += size
    return out.tobytes()


def scene_change_score(frame: bytes, prev: bytes, width: int, height: int) -> float:
    ysz = width * height
    a = np.frombuffer(frame, dtype=np.uint8, count=ysz).reshape(height, width)
    b = np.frombuffer(prev, dtype=np.uint8, count=ysz).reshape(height, width)
    aa = a[::16, ::16].astype(np.int16)
    bb = b[::16, ::16].astype(np.int16)
    return float(np.abs(aa - bb).mean())


def estimate_global_motion(frame: bytes, prev: bytes, width: int, height: int,
                           search: int = 8, sample_step: int = 10) -> tuple[int, int]:
    """Return integer (dx,dy): current(x,y) predicts from prev(x-dx,y-dy)."""
    search = max(0, min(31, int(search)))
    if search == 0 or width < 64 or height < 64:
        return 0, 0
    ysz = width * height
    cur = np.frombuffer(frame, dtype=np.uint8, count=ysz).reshape(height, width)
    old = np.frombuffer(prev, dtype=np.uint8, count=ysz).reshape(height, width)
    margin = search + 2
    ys = np.arange(margin, height - margin, sample_step, dtype=np.int32)
    xs = np.arange(margin, width - margin, sample_step, dtype=np.int32)
    if len(xs) < 4 or len(ys) < 4:
        return 0, 0
    target = cur[np.ix_(ys, xs)].astype(np.int16)

    best = (1e30, 0, 0)
    coarse = range(-search, search + 1, 2 if search >= 2 else 1)
    for dy in coarse:
        for dx in coarse:
            cand = old[np.ix_(ys - dy, xs - dx)].astype(np.int16)
            score = float(np.abs(target - cand).mean())
            if score < best[0]:
                best = (score, dx, dy)
    bx, by = best[1], best[2]
    for dy in range(max(-search, by - 1), min(search, by + 1) + 1):
        for dx in range(max(-search, bx - 1), min(search, bx + 1) + 1):
            cand = old[np.ix_(ys - dy, xs - dx)].astype(np.int16)
            score = float(np.abs(target - cand).mean())
            if score < best[0]:
                best = (score, dx, dy)
    return int(best[1]), int(best[2])


def _shift_plane(p: np.ndarray, dx: int, dy: int) -> np.ndarray:
    h, w = p.shape
    ys = np.clip(np.arange(h, dtype=np.int32) - int(dy), 0, h - 1)
    xs = np.clip(np.arange(w, dtype=np.int32) - int(dx), 0, w - 1)
    return p[np.ix_(ys, xs)]


def motion_predict(prev: bytes, width: int, height: int, dx: int, dy: int) -> bytes:
    a = np.frombuffer(prev, dtype=np.uint8)
    out = np.empty_like(a)
    ysz = width * height
    cw, ch = width // 2, height // 2
    csz = cw * ch
    y = a[:ysz].reshape(height, width)
    u = a[ysz:ysz + csz].reshape(ch, cw)
    v = a[ysz + csz:].reshape(ch, cw)
    out[:ysz] = _shift_plane(y, dx, dy).ravel()
    # 4:2:0 chroma motion is rounded toward zero; simple and deterministic.
    cdx, cdy = int(dx / 2), int(dy / 2)
    out[ysz:ysz + csz] = _shift_plane(u, cdx, cdy).ravel()
    out[ysz + csz:] = _shift_plane(v, cdx, cdy).ravel()
    return out.tobytes()


def temporal_residual_motion(frame: bytes, prev: bytes, width: int, height: int,
                             dx: int, dy: int) -> bytes:
    pred = np.frombuffer(motion_predict(prev, width, height, dx, dy), dtype=np.uint8)
    cur = np.frombuffer(frame, dtype=np.uint8)
    return (cur.astype(np.int16) - pred.astype(np.int16)).astype(np.uint8).tobytes()


def temporal_restore_motion(residual: bytes, prev: bytes, width: int, height: int,
                            dx: int, dy: int) -> bytes:
    pred = np.frombuffer(motion_predict(prev, width, height, dx, dy), dtype=np.uint8)
    r = np.frombuffer(residual, dtype=np.uint8)
    return (pred.astype(np.uint16) + r.astype(np.uint16)).astype(np.uint8).tobytes()


def _pack_desc(widths: np.ndarray) -> bytes:
    n = len(widths)
    if n & 1:
        widths = np.pad(widths, (0, 1), constant_values=0)
    x = widths.reshape(-1, 2).astype(np.uint8)
    return (x[:, 0] | (x[:, 1] << 4)).tobytes()


def _unpack_desc(data: bytes, count: int) -> np.ndarray:
    p = np.frombuffer(data, dtype=np.uint8)
    out = np.empty(len(p) * 2, dtype=np.uint8)
    out[0::2] = p & 0x0F
    out[1::2] = (p >> 4) & 0x0F
    return out[:count]


def _pack_group(vals: np.ndarray, width: int, chunk: int = 4096) -> bytes:
    if len(vals) == 0:
        return b''
    parts: list[bytes] = []
    shifts = np.arange(width, dtype=np.uint8)
    for i in range(0, len(vals), chunk):
        v = vals[i:i + chunk]
        bits = ((v[:, :, None] >> shifts[None, None, :]) & 1).astype(np.uint8)
        packed = np.packbits(bits.reshape(len(v), -1), axis=1, bitorder='little')
        parts.append(packed.tobytes())
    return b''.join(parts)


def _unpack_group(blob: memoryview, nblocks: int, width: int, chunk: int = 4096) -> np.ndarray:
    if nblocks == 0:
        return np.empty((0, BLOCK), dtype=np.uint8)
    rowbytes = 8 * width  # 64 * width bits
    out = np.empty((nblocks, BLOCK), dtype=np.uint8)
    shifts = (1 << np.arange(width, dtype=np.uint16))[None, None, :]
    p = 0
    for i in range(0, nblocks, chunk):
        n = min(chunk, nblocks - i)
        nb = n * rowbytes
        raw = np.frombuffer(blob[p:p + nb], dtype=np.uint8).reshape(n, rowbytes)
        p += nb
        bits = np.unpackbits(raw, axis=1, bitorder='little')[:, :BLOCK * width]
        bits = bits.reshape(n, BLOCK, width).astype(np.uint16)
        out[i:i + n] = np.sum(bits * shifts, axis=2, dtype=np.uint16).astype(np.uint8)
    return out


def bitpack_residual(residual: bytes) -> bytes:
    raw_size = len(residual)
    blocks = (raw_size + BLOCK - 1) // BLOCK
    padded = blocks * BLOCK
    src = np.frombuffer(residual, dtype=np.uint8)
    if padded != raw_size:
        tmp = np.zeros(padded, dtype=np.uint8)
        tmp[:raw_size] = src
        src = tmp
    mat = src.reshape(blocks, BLOCK)
    signed = mat.view(np.int8).astype(np.int16)
    zz = ((signed << 1) ^ (signed >> 15)).astype(np.uint8)
    widths = _BITLEN[np.max(zz, axis=1)]
    if np.any(widths > 8):
        raise RuntimeError('GBP3 internal width error')
    # Mode 9 is a sparse block: 64-bit nonzero bitmap + one byte per
    # nonzero zig-zag value. This is extremely effective when motion prediction
    # leaves only a handful of changed samples in a 64-byte block.
    nz = np.count_nonzero(zz, axis=1).astype(np.int16)
    sparse = (nz > 0) & ((8 + nz) < (8 * widths.astype(np.int16)))
    desc_modes = widths.copy()
    desc_modes[sparse] = 9
    desc = _pack_desc(desc_modes)
    payload = bytearray()
    for w in range(1, 9):
        payload += _pack_group(zz[desc_modes == w], w)
    if np.any(sparse):
        sv = zz[sparse]
        masks = np.packbits(sv != 0, axis=1, bitorder='little')
        for row, mask in zip(sv, masks):
            payload += mask.tobytes()
            payload += row[row != 0].tobytes()
    return struct.pack(HEAD_FMT, MAGIC, BLOCK, 0, raw_size, blocks) + desc + bytes(payload)


def bitunpack_residual(data: bytes, expected: int) -> bytes:
    if len(data) < HEAD_SIZE:
        raise ValueError('truncated GBP3 payload')
    magic, block, flags, raw_size, blocks = struct.unpack_from(HEAD_FMT, data, 0)
    if magic != MAGIC or block != BLOCK or flags != 0:
        raise ValueError('unsupported GBP3 payload')
    if raw_size != expected:
        raise ValueError('GBP3 raw size mismatch')
    desc_len = (blocks + 1) // 2
    if len(data) < HEAD_SIZE + desc_len:
        raise ValueError('truncated GBP3 descriptors')
    widths = _unpack_desc(data[HEAD_SIZE:HEAD_SIZE + desc_len], blocks)
    if np.any(widths > 9):
        raise ValueError('invalid GBP3 mode')
    p = HEAD_SIZE + desc_len
    zz = np.zeros((blocks, BLOCK), dtype=np.uint8)
    mv = memoryview(data)
    for w in range(1, 9):
        mask = widths == w
        n = int(np.count_nonzero(mask))
        need = n * 8 * w
        if p + need > len(data):
            raise ValueError('truncated GBP3 bitstream')
        zz[mask] = _unpack_group(mv[p:p + need], n, w)
        p += need
    # Sparse mode records are stored after all fixed-width groups.
    for bi in np.flatnonzero(widths == 9):
        if p + 8 > len(data):
            raise ValueError('truncated GBP3 sparse bitmap')
        maskbytes = np.frombuffer(data, dtype=np.uint8, count=8, offset=p)
        p += 8
        bits = np.unpackbits(maskbytes, bitorder='little')[:BLOCK].astype(bool)
        n = int(np.count_nonzero(bits))
        if p + n > len(data):
            raise ValueError('truncated GBP3 sparse values')
        vals = np.frombuffer(data, dtype=np.uint8, count=n, offset=p)
        p += n
        zz[int(bi), bits] = vals
    if p != len(data):
        raise ValueError('trailing bytes in GBP3 payload')
    z16 = zz.astype(np.int16)
    signed = ((z16 >> 1) ^ -(z16 & 1)).astype(np.int16)
    out = signed.astype(np.int8).view(np.uint8).reshape(-1)
    return out[:raw_size].tobytes()


def encode_frame(yuv: bytes, prev: bytes | None, width: int, height: int,
                 force_i: bool = False, scene_threshold: float = 30.0,
                 motion_range: int = 8):
    raw_size = len(yuv)
    if prev is not None and not force_i and yuv == prev:
        return 2, b'', raw_size, 0, 0
    if prev is None or force_i or scene_change_score(yuv, prev, width, height) >= scene_threshold:
        typ = 0
        dx = dy = 0
        residual = intra_residual(yuv, width, height)
    else:
        typ = 1
        dx, dy = estimate_global_motion(yuv, prev, width, height, motion_range)
        residual = temporal_residual_motion(yuv, prev, width, height, dx, dy)
    return typ, bitpack_residual(residual), raw_size, dx, dy


def decode_frame(frame_type: int, payload: bytes, prev: bytes | None,
                 width: int, height: int, raw_size: int, dx: int = 0, dy: int = 0) -> bytes:
    if frame_type == 2:
        if prev is None:
            raise ValueError('repeat frame without previous frame')
        return prev
    residual = bitunpack_residual(payload, raw_size)
    if frame_type == 0:
        return intra_restore(residual, width, height)
    if frame_type == 1:
        if prev is None:
            raise ValueError('P-frame without previous frame')
        return temporal_restore_motion(residual, prev, width, height, dx, dy)
    raise ValueError(f'unknown GHVC3 frame type {frame_type}')
