#!/usr/bin/env python3
from __future__ import annotations
import argparse, os, shutil, struct
from pathlib import Path
from ghv.container import (Header, read_header, FRAME_FMT, FRAME_SIZE, VFRM,
                           INDEX_HEAD_FMT, INDEX_ENTRY_FMT, INDX)

MAX_PACKED = 512 * 1024 * 1024  # sanity bound per frame


def scan_frames(path: Path):
    entries = []
    with open(path, 'rb') as f:
        h = read_header(f)
        f.seek(h.frames_offset)
        expected_no = 0
        size = os.path.getsize(path)
        while f.tell() + FRAME_SIZE <= size:
            off = f.tell()
            raw = f.read(FRAME_SIZE)
            if len(raw) != FRAME_SIZE:
                break
            vals = struct.unpack(FRAME_FMT, raw)
            magic, no, pts, typ, codec, meta, raw_size, packed_size, checksum = vals
            if magic != VFRM:
                break
            if typ not in (0, 1, 2) or codec not in (3, 4, 5, 6):
                break
            if no != expected_no or packed_size > MAX_PACKED:
                break
            if f.tell() + packed_size > size:
                break
            f.seek(packed_size, os.SEEK_CUR)
            entries.append((off, typ))
            expected_no += 1
    return h, entries


def repair(src: Path, dst: Path):
    if src.resolve() != dst.resolve():
        shutil.copy2(src, dst)
    h, entries = scan_frames(dst)
    if not entries:
        raise RuntimeError('No valid VFRM sequence could be recovered.')
    with open(dst, 'r+b') as f:
        f.seek(0, os.SEEK_END)
        index_offset = f.tell()
        f.write(struct.pack(INDEX_HEAD_FMT, INDX, len(entries)))
        for off, typ in entries:
            f.write(struct.pack(INDEX_ENTRY_FMT, off, typ))
        video_dur_us = (len(entries) * h.fps_den * 1_000_000) // h.fps_num if h.fps_num else 0
        duration_us = max(video_dur_us, h.duration_us if h.audio_samples else 0)
        patched = Header(flags=h.flags, width=h.width, height=h.height,
                         fps_num=h.fps_num, fps_den=h.fps_den,
                         frame_count=len(entries), keyint=h.keyint, quality=h.quality,
                         audio_rate=h.audio_rate, audio_channels=h.audio_channels,
                         audio_codec=h.audio_codec, audio_samples=h.audio_samples,
                         frames_offset=h.frames_offset, audio_offset=h.audio_offset,
                         index_offset=index_offset, duration_us=duration_us,
                         major=h.major, minor=h.minor)
        f.seek(0)
        f.write(patched.pack())
        f.flush()
    return len(entries), index_offset


def main():
    ap = argparse.ArgumentParser(description='Rebuild the GHV frame index from intact VFRM records.')
    ap.add_argument('input')
    ap.add_argument('output', nargs='?', help='default: <input>_repaired.ghv')
    ap.add_argument('--in-place', action='store_true', help='rewrite the input file header/index in place')
    a = ap.parse_args()
    src = Path(a.input)
    if a.in_place:
        dst = src
    elif a.output:
        dst = Path(a.output)
    else:
        dst = src.with_name(src.stem + '_repaired.ghv')
    n, off = repair(src, dst)
    print(f'[PASS] Rebuilt GHV index: {n} frames')
    print(f'Output: {dst}')
    print(f'New index offset: {off}')


if __name__ == '__main__':
    main()
