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
from ghv import gha


COEFFICIENT_PACKETS = {'GBP9', 'GIC9', 'GTC7', 'GTP9', 'GCP9'}


def motion_fields(body, count, compact):
    fields = Counter()
    cursor = 0
    if compact:
        for first in range(0, count, 8):
            valid = min(8, count - first)
            mask = body[cursor]
            cursor += 1
            if mask >> valid:
                raise ValueError('motion mask padding')
            fields['motion_reuse_mask'] += valid
            fields['motion_mask_padding'] += 8 - valid
            for _ in range(mask.bit_count()):
                pair = body[cursor]
                cursor += 1
                if pair == 255:
                    fields['motion_escape'] += 8
                    cursor += 2
                    fields['motion_delta'] += 16
                else:
                    fields['motion_delta'] += 8
    else:
        for _ in range(count):
            token = body[cursor]
            cursor += 1
            fields['motion_reuse_token'] += 8
            if token == 1:
                for _ in range(2):
                    while True:
                        byte = body[cursor]
                        cursor += 1
                        fields['motion_delta'] += 8
                        if byte < 128:
                            break
            elif token != 0:
                raise ValueError('unknown motion token')
    if cursor != len(body):
        raise ValueError('motion length mismatch')
    return fields


def audio_fields(stream, length):
    h = gha.parse_header(stream.read(gha.HEADER_SIZE))
    count, channels, block, width = (h[k] for k in ('block_count','channels','block_frames','bits'))
    if h['data_offset'] != gha.HEADER_SIZE or length != gha.HEADER_SIZE + count * gha._record_size(block, channels, width):
        raise ValueError('unexpected GHAC1 layout')
    slots = count * (block - 1) * channels
    valid = (h['frame_count'] - count) * channels
    stored = count * gha._code_bytes(block, channels, width) * 8
    return Counter(gha_header=gha.HEADER_SIZE * 8, audio_anchors=count * channels * 16,
                   audio_steps=count * channels * 16, audio_codes=valid * width,
                   audio_final_block_padding=(slots-valid)*width, audio_pack_padding=stored-slots*width)


def analyze(path, rows):
    totals = Counter()
    types = defaultdict(Counter)
    symbols = Counter()
    frame_counts = Counter()
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
        index_pad_bits = (entry - struct.calcsize('<QB')) * 8 * len(index)
        totals['index_entries'] -= index_pad_bits
        totals['index_padding'] = index_pad_bits
        if header.audio_offset:
            stream.seek(header.audio_offset)
            magic, _, length, _ = struct.unpack(AUDIO_FMT, stream.read(AUDIO_SIZE))
            if magic != b'AUD0':
                raise ValueError('audio marker mismatch')
            section('audio_header', header.audio_offset, AUDIO_SIZE)
            spans.append((header.audio_offset + AUDIO_SIZE, header.audio_offset + AUDIO_SIZE + length))
            totals.update(audio_fields(stream,length))
        for no, (off, kind) in enumerate(index):
            stream.seek(off)
            magic, seq, pts, ft, codec, _, raw, length, crc = struct.unpack(FRAME_FMT, stream.read(FRAME_SIZE))
            if (magic, seq, ft, codec) != (b'VFRM', no, kind, 9):
                raise ValueError(f'frame/index mismatch at {off}')
            if off + FRAME_SIZE + length > size:
                raise ValueError('frame extent exceeds file')
            data = stream.read(length)
            if len(data) != length:
                raise ValueError('short payload')
            spans.append((off, off + FRAME_SIZE + length))
            bits = Counter(frame_header=8 * FRAME_SIZE)
            tag = data[:4].decode('ascii', errors='backslashreplace') if data else 'repeat'
            frame_counts[tag] += 1
            metrics = {}
            if tag in COEFFICIENT_PACKETS:
                intra = tag in ('GIC9','GTC7')
                blocks = struct.unpack_from('<I', data, 12)[0]
                descriptors = (3 if intra else 1) * blocks
                bits['quality_field'] = 8
                bits['codec_header_other'] = 8 * ((16 if intra else 24) - 1)
                bits['predictor_mode'] = 2 * blocks if intra else 0
                bits['zero_block_map'] = blocks
                bits['descriptor_padding'] = -descriptors % 8
                if not intra:
                    vectors, motion_bytes = struct.unpack_from('<II',data,16)
                    bits.update(motion_fields(data[24:24+motion_bytes],vectors,bool(data[7]&1)))
                if tag == 'GBP9':
                    bits['chunk_table'] = 32 * (1 + (blocks + 255) // 256)
                helper.stdin.write(struct.pack('<I', length))
                helper.stdin.write(data)
                helper.stdin.flush()
                counts = helper.stdout.readline().split()
                if len(counts) != 10:
                    raise ValueError(f'incomplete coefficient accounting for frame {no} ({tag})')
                last, run, level, pad, alt, small, count, tags, dc_mode, dc_delta = map(int, counts)
                bits.update(end_position=last, zero_run=run, coefficient_level=level, entropy_padding=pad, token_tags=tags,
                            dc_prediction_mode=dc_mode, dc_delta=dc_delta)
                metrics = dict(tokens=count, small_levels=small, alternative_level_bits=alt)
                symbols.update(metrics)
            elif tag == 'GZP9':
                # The RLE stream spans motion, descriptors and coefficients.
                # One encoded run can cross field boundaries in the expanded data.
                bits['rle_header'] = 64
                expanded = 4
                cursor = 8
                while cursor < length:
                    token = data[cursor]
                    cursor += 1
                    bits['rle_control'] += 8
                    stored = 1 if token & 128 else token + 1
                    expanded += (token & 127) + 3 if token & 128 else stored
                    bits['rle_values'] += stored * 8
                    cursor += stored
                if cursor != length or expanded != struct.unpack_from('<I', data, 4)[0]:
                    raise ValueError('invalid GZP9 lengths')
            elif length:
                bits['other_payload_' + tag] = 8 * length
            if sum(bits.values()) != 8 * (length + FRAME_SIZE):
                raise ValueError(f'unbalanced frame {no}')
            totals.update(bits)
            types[tag].update(bits)
            if rows:
                print(json.dumps(dict(frame=no, offset=off, pts_us=pts, kind=kind, payload=tag,
                                      payload_bytes=length, crc=crc, quality=data[5] if tag in COEFFICIENT_PACKETS else None, flags=data[7] if tag in COEFFICIENT_PACKETS else None, raw_bytes=raw, bits=bits, **metrics)), file=rows)
        helper.stdin.close()
        if helper.wait():
            raise RuntimeError('entropy reader failed')
    cursor = 0
    for start, end in sorted(spans):
        if not cursor <= start <= end <= size:
            raise ValueError('overlap or out-of-file extent')
        totals['unreferenced'] += (start - cursor) * 8
        cursor = end
    totals['unreferenced'] += (size - cursor) * 8
    if sum(totals.values()) != 8 * size:
        raise ValueError('unbalanced file')
    return dict(file=path.name, bytes=size, frames=len(index), bits=totals, frame_bits_by_payload=types, frame_counts=frame_counts,
                symbol_counts=symbols)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file', type=Path)
    parser.add_argument('summary', type=Path)
    parser.add_argument('--frames', type=Path)
    args = parser.parse_args()
    destinations = [args.summary] + ([args.frames] if args.frames else [])
    if len({p.resolve() for p in destinations}) != len(destinations) or any(p.exists() for p in destinations):
        parser.error('report paths must be distinct new files')
    if args.frames:
        with args.frames.open('x', encoding='utf8') as rows:
            result = analyze(args.file, rows)
    else:
        result = analyze(args.file, None)
    with args.summary.open('x', encoding='utf8') as f:
        json.dump(result, f, indent=2)


if __name__ == '__main__':
    main()
