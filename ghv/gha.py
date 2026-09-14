from __future__ import annotations
import math
import struct
import numpy as np

# GHA 0.2 container + GHAC1 codec.
# GHAC1 is our own block predictive PCM codec. It uses exact block anchors,
# adaptive per-channel step sizes, closed-form predictive levels and either
# 8-bit or packed 6-bit residual codes. No external audio codec is used.

MAGIC = b'GHAF'
CODEC = b'GHA1'
HEADER_FMT = '<4sBBH4sIHHIIQQQ12s'
HEADER_SIZE = struct.calcsize(HEADER_FMT)  # 64


def _round_div_signed(x: np.ndarray, d: np.ndarray) -> np.ndarray:
    # x: (..., channels), d broadcastable (..., channels), both int64/int32.
    ax = np.abs(x.astype(np.int64))
    dd = d.astype(np.int64)
    q = (ax + (dd // 2)) // dd
    return np.where(x < 0, -q, q).astype(np.int32)


def _pack6(codes: np.ndarray, bias: int = 32) -> np.ndarray:
    # codes shape (blocks, n), values 0..63. Pack 4x6 bits -> 3 bytes.
    n = codes.shape[1]
    groups = (n + 3) // 4
    padded = np.full((codes.shape[0], groups * 4), bias, dtype=np.uint8)
    padded[:, :n] = codes
    g = padded.reshape(codes.shape[0], groups, 4).astype(np.uint32)
    v = g[:, :, 0] | (g[:, :, 1] << 6) | (g[:, :, 2] << 12) | (g[:, :, 3] << 18)
    out = np.empty((codes.shape[0], groups, 3), dtype=np.uint8)
    out[:, :, 0] = v & 0xFF
    out[:, :, 1] = (v >> 8) & 0xFF
    out[:, :, 2] = (v >> 16) & 0xFF
    return out.reshape(codes.shape[0], groups * 3)


def _unpack6(packed: np.ndarray, n_codes: int) -> np.ndarray:
    groups = packed.shape[1] // 3
    p = packed.reshape(packed.shape[0], groups, 3).astype(np.uint32)
    v = p[:, :, 0] | (p[:, :, 1] << 8) | (p[:, :, 2] << 16)
    out = np.empty((packed.shape[0], groups, 4), dtype=np.uint8)
    out[:, :, 0] = v & 0x3F
    out[:, :, 1] = (v >> 6) & 0x3F
    out[:, :, 2] = (v >> 12) & 0x3F
    out[:, :, 3] = (v >> 18) & 0x3F
    return out.reshape(packed.shape[0], groups * 4)[:, :n_codes]


def _code_bytes(block_frames: int, channels: int, bits: int) -> int:
    n = (block_frames - 1) * channels
    if bits == 8:
        return n
    if bits == 6:
        return ((n + 3) // 4) * 3
    raise ValueError('GHAC1 supports 6-bit or 8-bit residuals')


def _record_size(block_frames: int, channels: int, bits: int) -> int:
    return channels * 4 + _code_bytes(block_frames, channels, bits)


def encode_pcm16le(pcm: bytes, sample_rate: int, channels: int, *, bits: int = 8,
                   block_frames: int = 32, quality: int = 90) -> bytes:
    if channels not in (1, 2):
        raise ValueError('GHAC1 currently supports mono or stereo')
    if bits not in (6, 8):
        raise ValueError('GHAC1 bits must be 6 or 8')
    if block_frames < 8 or block_frames > 1024:
        raise ValueError('invalid GHAC1 block size')
    if sample_rate <= 0:
        raise ValueError('invalid sample rate')

    s = np.frombuffer(pcm, dtype='<i2')
    usable = len(s) - (len(s) % channels)
    s = s[:usable]
    frame_count = usable // channels
    frames = s.reshape(frame_count, channels) if frame_count else np.empty((0, channels), dtype='<i2')
    block_count = (frame_count + block_frames - 1) // block_frames if frame_count else 0
    cb = _code_bytes(block_frames, channels, bits)
    recsize = _record_size(block_frames, channels, bits)
    total = HEADER_SIZE + block_count * recsize
    out = bytearray(total)
    duration_us = (frame_count * 1_000_000) // sample_rate if frame_count else 0
    header = struct.pack(HEADER_FMT, MAGIC, 0, 2, HEADER_SIZE, CODEC, int(sample_rate), int(channels),
                         int(bits), int(block_frames), int(max(1, min(100, quality))),
                         int(frame_count), int(block_count), HEADER_SIZE, b'\0' * 12)
    out[:HEADER_SIZE] = header
    if not block_count:
        return bytes(out)

    limit = 127 if bits == 8 else 31
    bias = 128 if bits == 8 else 32
    # Work on thousands of blocks at a time: fast NumPy core, low Python overhead.
    chunk_blocks = 8192
    dst = HEADER_SIZE
    last_frame = frames[-1].astype(np.int32)

    for b0 in range(0, block_count, chunk_blocks):
        bn = min(chunk_blocks, block_count - b0)
        start = b0 * block_frames
        end = min(frame_count, (b0 + bn) * block_frames)
        need = bn * block_frames
        src = frames[start:end]
        if len(src) < need:
            pad = np.repeat(last_frame.reshape(1, channels).astype('<i2'), need - len(src), axis=0)
            src = np.concatenate((src, pad), axis=0)
        blocks = src.reshape(bn, block_frames, channels).astype(np.int32)
        anchors = blocks[:, 0, :]
        deltas = np.diff(blocks, axis=1)
        max_delta = np.max(np.abs(deltas.astype(np.int64)), axis=1) if block_frames > 1 else np.zeros((bn, channels), dtype=np.int64)
        scale = np.maximum(1, (max_delta + limit - 1) // limit).astype(np.int32)

        # Quantize absolute positions relative to the exact anchor, then transmit
        # differences of the quantized levels. This avoids cumulative drift.
        rel = blocks[:, 1:, :] - anchors[:, None, :]
        q = None
        for _ in range(4):
            levels = _round_div_signed(rel, scale[:, None, :])
            z = np.zeros((bn, 1, channels), dtype=np.int32)
            q = np.diff(np.concatenate((z, levels), axis=1), axis=1)
            over = np.max(np.abs(q), axis=1) > limit
            if not np.any(over):
                break
            # Increase only the channel scales that overflowed.
            maxq = np.max(np.abs(q), axis=1)
            factor = np.maximum(1, (maxq + limit - 1) // limit)
            scale = np.where(over, scale * factor, scale)
        if q is None:
            raise RuntimeError('GHAC1 internal quantizer failure')
        q = np.clip(q, -limit, limit)
        codes = (q.reshape(bn, -1) + bias).astype(np.uint8)
        code_bytes = codes if bits == 8 else _pack6(codes, bias=bias)

        rec = np.empty((bn, recsize), dtype=np.uint8)
        a_bytes = anchors.astype('<i2').view(np.uint8).reshape(bn, channels * 2)
        s_bytes = np.clip(scale, 1, 65535).astype('<u2').view(np.uint8).reshape(bn, channels * 2)
        rec[:, :channels * 2] = a_bytes
        rec[:, channels * 2:channels * 4] = s_bytes
        rec[:, channels * 4:] = code_bytes[:, :cb]
        rb = rec.tobytes()
        out[dst:dst + len(rb)] = rb
        dst += len(rb)

    return bytes(out)


def parse_header(data: bytes):
    if len(data) < HEADER_SIZE:
        raise ValueError('truncated GHA header')
    vals = struct.unpack_from(HEADER_FMT, data, 0)
    if vals[0] != MAGIC:
        raise ValueError('not a GHA file')
    if vals[3] != HEADER_SIZE or vals[4] != CODEC:
        raise ValueError('unsupported GHA/GHAC codec')
    return {
        'major': vals[1], 'minor': vals[2], 'sample_rate': vals[5], 'channels': vals[6],
        'bits': vals[7], 'block_frames': vals[8], 'quality': vals[9], 'frame_count': vals[10],
        'block_count': vals[11], 'data_offset': vals[12],
    }


def decode_pcm16le(data: bytes) -> tuple[bytes, int, int, int]:
    h = parse_header(data)
    sr = int(h['sample_rate']); channels = int(h['channels']); bits = int(h['bits'])
    block_frames = int(h['block_frames']); frame_count = int(h['frame_count']); block_count = int(h['block_count'])
    if channels not in (1, 2) or bits not in (6, 8):
        raise ValueError('unsupported GHAC1 parameters')
    cb = _code_bytes(block_frames, channels, bits)
    recsize = _record_size(block_frames, channels, bits)
    need = HEADER_SIZE + block_count * recsize
    if len(data) < need:
        raise ValueError('truncated GHAC1 payload')
    out = bytearray(frame_count * channels * 2)
    if frame_count == 0:
        return bytes(out), sr, channels, frame_count
    bias = 128 if bits == 8 else 32
    chunk_blocks = 8192
    written_frames = 0
    src_off = HEADER_SIZE
    out_off = 0

    for b0 in range(0, block_count, chunk_blocks):
        bn = min(chunk_blocks, block_count - b0)
        nbytes = bn * recsize
        rec = np.frombuffer(data, dtype=np.uint8, count=nbytes, offset=src_off).reshape(bn, recsize)
        src_off += nbytes
        a_raw = rec[:, :channels * 2].copy().reshape(-1)
        s_raw = rec[:, channels * 2:channels * 4].copy().reshape(-1)
        anchors = a_raw.view('<i2').reshape(bn, channels).astype(np.int32)
        scale = s_raw.view('<u2').reshape(bn, channels).astype(np.int32)
        packed = rec[:, channels * 4:channels * 4 + cb]
        if bits == 8:
            codes = packed[:, : (block_frames - 1) * channels]
        else:
            codes = _unpack6(packed, (block_frames - 1) * channels)
        q = codes.astype(np.int16) - bias
        q = q.reshape(bn, block_frames - 1, channels).astype(np.int32)
        levels = np.cumsum(q, axis=1, dtype=np.int32)
        samples = np.empty((bn, block_frames, channels), dtype=np.int32)
        samples[:, 0, :] = anchors
        samples[:, 1:, :] = anchors[:, None, :] + levels * scale[:, None, :]
        samples = np.clip(samples, -32768, 32767).astype('<i2').reshape(-1, channels)
        take = min(len(samples), frame_count - written_frames)
        bb = samples[:take].tobytes()
        out[out_off:out_off + len(bb)] = bb
        out_off += len(bb); written_frames += take

    return bytes(out), sr, channels, frame_count


def snr_db(original_pcm: bytes, decoded_pcm: bytes) -> float:
    a = np.frombuffer(original_pcm, dtype='<i2').astype(np.float64)
    b = np.frombuffer(decoded_pcm, dtype='<i2').astype(np.float64)
    n = min(len(a), len(b))
    if n == 0:
        return float('inf')
    a = a[:n]; b = b[:n]
    noise = np.mean((a - b) ** 2)
    sig = np.mean(a ** 2)
    if noise <= 0:
        return float('inf')
    return 10.0 * math.log10(max(sig, 1e-30) / noise)
