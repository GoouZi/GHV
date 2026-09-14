from __future__ import annotations
import struct
from dataclasses import dataclass

HEADER_FMT = '<4sBBHIIIIIIIIIHHQQQQQ8s'
HEADER_SIZE = struct.calcsize(HEADER_FMT)  # 96
FRAME_FMT = '<4sIQBBHIII'
FRAME_SIZE = struct.calcsize(FRAME_FMT)   # 32
AUDIO_FMT = '<4sIQQ'
AUDIO_SIZE = struct.calcsize(AUDIO_FMT)
INDEX_HEAD_FMT = '<4sI'
INDEX_ENTRY_FMT = '<QB7x'

MAGIC = b'GHV1'
VFRM = b'VFRM'
AUD0 = b'AUD0'
INDX = b'INDX'


def pack_motion(dx: int, dy: int) -> int:
    if not (-128 <= int(dx) <= 127 and -128 <= int(dy) <= 127):
        raise ValueError('motion vector out of int8 range')
    return (int(dx) & 0xFF) | ((int(dy) & 0xFF) << 8)


def unpack_motion(meta: int) -> tuple[int, int]:
    dx = meta & 0xFF
    dy = (meta >> 8) & 0xFF
    if dx >= 128: dx -= 256
    if dy >= 128: dy -= 256
    return dx, dy


@dataclass
class Header:
    flags: int
    width: int
    height: int
    fps_num: int
    fps_den: int
    frame_count: int
    keyint: int
    quality: int
    audio_rate: int
    audio_channels: int
    audio_codec: int
    audio_samples: int
    frames_offset: int
    audio_offset: int
    index_offset: int
    duration_us: int
    major: int = 0
    minor: int = 4

    def pack(self) -> bytes:
        return struct.pack(HEADER_FMT, MAGIC, self.major, self.minor, HEADER_SIZE,
            self.flags, self.width, self.height, self.fps_num, self.fps_den,
            self.frame_count, self.keyint, self.quality, self.audio_rate,
            self.audio_channels, self.audio_codec, self.audio_samples,
            self.frames_offset, self.audio_offset, self.index_offset,
            self.duration_us, b'\0' * 8)

    @staticmethod
    def unpack(b: bytes) -> 'Header':
        if len(b) < HEADER_SIZE:
            raise ValueError('truncated GHV header')
        vals = struct.unpack(HEADER_FMT, b[:HEADER_SIZE])
        if vals[0] != MAGIC:
            if vals[0] == b'GVID':
                raise ValueError('legacy .gvid detected; use the v0.3 legacy player or re-encode to .ghv')
            raise ValueError('not a GHV file')
        if vals[3] != HEADER_SIZE:
            raise ValueError('unsupported GHV header size')
        return Header(flags=vals[4], width=vals[5], height=vals[6], fps_num=vals[7], fps_den=vals[8],
            frame_count=vals[9], keyint=vals[10], quality=vals[11], audio_rate=vals[12],
            audio_channels=vals[13], audio_codec=vals[14], audio_samples=vals[15],
            frames_offset=vals[16], audio_offset=vals[17], index_offset=vals[18], duration_us=vals[19],
            major=vals[1], minor=vals[2])


def read_header(f) -> Header:
    f.seek(0)
    return Header.unpack(f.read(HEADER_SIZE))


def read_index(f, h: Header):
    if h.index_offset <= 0:
        raise ValueError('GHV file has no index')
    f.seek(h.index_offset)
    head = f.read(struct.calcsize(INDEX_HEAD_FMT))
    if len(head) != struct.calcsize(INDEX_HEAD_FMT):
        raise ValueError('truncated GHV index')
    magic, count = struct.unpack(INDEX_HEAD_FMT, head)
    if magic != INDX:
        raise ValueError('missing GHV index')
    entries = []
    es = struct.calcsize(INDEX_ENTRY_FMT)
    for _ in range(count):
        raw = f.read(es)
        if len(raw) != es:
            raise ValueError('truncated GHV index entry')
        off, typ = struct.unpack(INDEX_ENTRY_FMT, raw)
        entries.append((off, typ))
    return entries
