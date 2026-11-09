"""Measure a reversible motion-token representation on existing M1 outputs.

Run from the repository with python -m benchmarks.milestones.ghvc9.probe_motion FILE.
Each group of eight vectors has a literal mask. A zero bit reuses the median
predictor. A literal packs two zigzag delta bytes into nibbles when both fit
below 15, otherwise 0xff introduces the original pair. Only M1 encoder output
is supported (each even-pixel delta is in [-24,24], hence one varint byte).
"""
import argparse
from pathlib import Path
import struct

from ghv.container import FRAME_FMT, FRAME_SIZE, read_header, read_index


def pack(body, count):
    out = bytearray()
    pos = 0
    for first in range(0, count, 8):
        mask_at = len(out)
        out.append(0)
        for bit in range(min(8, count - first)):
            token = body[pos]
            pos += 1
            if token == 0:
                continue
            if token != 1:
                raise ValueError("unknown M1 motion token")
            x, y = body[pos:pos + 2]
            pos += 2
            if max(x, y) > 48:
                raise ValueError("motion delta outside encoder bounds")
            out[mask_at] |= 1 << bit
            if max(x, y) < 15:
                out.append(x | y << 4)
            else:
                out.extend((255, x, y))
    if pos != len(body):
        raise ValueError("unexpected motion body length")
    return out


def unpack(data, count):
    pos = 0
    body = bytearray()
    for first in range(0, count, 8):
        mask = data[pos]
        pos += 1
        for bit in range(min(8, count - first)):
            if mask & (1 << bit):
                pair = data[pos]
                pos += 1
                if pair == 255:
                    x, y = data[pos:pos + 2]
                    pos += 2
                else:
                    x, y = pair & 15, pair >> 4
                body.extend((1, x, y))
            else:
                body.append(0)
    if pos != len(data):
        raise ValueError("unused motion bytes")
    return body


def measure(path):
    original = packed = frames = 0
    with path.open("rb") as stream:
        header = read_header(stream)
        for offset, kind in read_index(stream, header):
            if kind != 1:
                continue
            stream.seek(offset)
            frame = struct.unpack(FRAME_FMT, stream.read(FRAME_SIZE))
            prefix = stream.read(24)
            if (frame[4] != 9 or len(prefix) != 24 or prefix[7] != 0
                    or prefix[:4] not in (b"GBP9", b"GTP9")):
                raise ValueError("expected a GHVC9 M1 P frame")
            count, size = struct.unpack_from("<II", prefix, 16)
            body = stream.read(size)
            candidate = pack(body, count)
            if unpack(candidate, count) != body:
                raise RuntimeError("round trip mismatch")
            original += size
            packed += len(candidate)
            frames += 1
    print(path.name, "P frames", frames, "motion bytes", original, "packed", packed,
          "projected file MiB", round((path.stat().st_size-original+packed)/2**20, 3))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    for source in parser.parse_args().files:
        measure(source)
