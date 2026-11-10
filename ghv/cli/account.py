"""Exact physical bit accounting for native GHVC9 files."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import struct
import subprocess

from ghv.container import (read_header, read_index, FRAME_FMT, FRAME_SIZE, HEADER_SIZE,
                           AUDIO_FMT, AUDIO_SIZE, INDEX_ENTRY_FMT, INDEX_HEAD_FMT)
from ghv.paths import native_binary


def analyze(path, rows):
    totals = Counter()
    types = defaultdict(Counter)
    symbols = Counter()
    spans = []
    size = path.stat().st_size
    with path.open('rb') as stream, subprocess.Popen(
            [native_binary('ghvbitaccount')], stdin=subprocess.PIPE, stdout=subprocess.PIPE) as helper:
        header = read_header(stream)
        index = read_index(stream, header)
        if len(index) != header.frame_count:
            raise ValueError('index frame count mismatch')
        def section(name, offset, length):
            spans.append((offset, offset + length))
            totals[name] += 8 * length
        section('container_header', 0, HEADER_SIZE)
        section('index_header', header.index_offset, struct.calcsize(INDEX_HEAD_FMT))
        entry = struct.calcsize(INDEX_ENTRY_FMT)
        section('index_entries', header.index_offset + struct.calcsize(INDEX_HEAD_FMT), entry * len(index))
        if header.audio_offset:
            stream.seek(header.audio_offset)
            magic, _, length, _ = struct.unpack(AUDIO_FMT, stream.read(AUDIO_SIZE))
            if magic != b'AUD0':
                raise ValueError('audio marker mismatch')
            section('audio_header', header.audio_offset, AUDIO_SIZE)
            section('audio_body', header.audio_offset + AUDIO_SIZE, length)
        for no, (off, kind) in enumerate(index):
            stream.seek(off)
            magic, seq, pts, ft, codec, _, raw, length, crc = struct.unpack(FRAME_FMT, stream.read(FRAME_SIZE))
            if (magic, seq, ft, codec) != (b'VFRM', no, kind, 9):
                raise ValueError(f'frame/index mismatch at {off}')
            data = stream.read(length)
            if len(data) != length:
                raise ValueError('short payload')
            spans.append((off, off + FRAME_SIZE + length))
            bits = Counter(frame_header=8 * FRAME_SIZE)
            tag = data[:4].decode('ascii') if data else 'repeat'
            metrics = {}
            if tag in ('GBP9', 'GIC9'):
                intra = tag == 'GIC9'
                blocks = struct.unpack_from('<I', data, 12)[0]
                descriptors = (3 if intra else 1) * blocks
                bits['quality_field'] = 8
                bits['codec_header_other'] = 8 * ((16 if intra else 24) - 1)
                bits['predictor_mode'] = 2 * blocks if intra else 0
                bits['zero_block_map'] = blocks
                bits['descriptor_padding'] = -descriptors % 8
                if not intra:
                    bits['motion_body'] = 8 * struct.unpack_from('<I', data, 20)[0]
                    bits['chunk_table'] = 32 * (1 + (blocks + 255) // 256)
                helper.stdin.write(struct.pack('<I', length))
                helper.stdin.write(data)
                helper.stdin.flush()
                last, run, level, pad, alt, small, count = map(int, helper.stdout.readline().split())
                bits.update(end_position=last, zero_run=run, coefficient_level=level, entropy_padding=pad)
                metrics = dict(tokens=count, small_levels=small, alternative_level_bits=alt)
                symbols.update(metrics)
            elif length:
                bits['other_payload_' + tag] = 8 * length
            if sum(bits.values()) != 8 * (length + FRAME_SIZE):
                raise ValueError(f'unbalanced frame {no}')
            totals.update(bits)
            types[tag].update(bits)
            if rows:
                print(json.dumps(dict(frame=no, offset=off, pts_us=pts, kind=kind, payload=tag,
                                      payload_bytes=length, crc=crc, raw_bytes=raw, bits=bits, **metrics)), file=rows)
        helper.stdin.close()
        if helper.wait():
            raise RuntimeError('entropy reader failed')
    cursor = 0
    for start, end in sorted(spans):
        if not cursor <= start <= end <= size:
            raise ValueError('overlap or out-of-file extent')
        totals['unreferenced_bytes_bits'] += (start - cursor) * 8
        cursor = end
    totals['unreferenced_bytes_bits'] += (size - cursor) * 8
    if sum(totals.values()) != 8 * size:
        raise ValueError('unbalanced file')
    return dict(file=path.name, bytes=size, frames=len(index), bits=totals, frame_bits_by_payload=types,
                symbol_counts=symbols)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file', type=Path)
    parser.add_argument('summary', type=Path)
    parser.add_argument('--frames', type=Path)
    args = parser.parse_args()
    if args.frames:
        with args.frames.open('x', encoding='utf8') as rows:
            result = analyze(args.file, rows)
    else:
        result = analyze(args.file, None)
    with args.summary.open('x', encoding='utf8') as f:
        json.dump(result, f, indent=2)


if __name__ == '__main__':
    main()
