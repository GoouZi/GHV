#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, os
from pathlib import Path
from ghv.gha import parse_header


def main():
    ap = argparse.ArgumentParser(description='Inspect GHA files')
    ap.add_argument('input')
    ap.add_argument('--json', action='store_true')
    a = ap.parse_args()
    data = Path(a.input).read_bytes()
    h = parse_header(data)
    d = {
        'format': 'GHA', 'version': f'{h["major"]}.{h["minor"]}', 'codec': 'GHAC1',
        'sample_rate': h['sample_rate'], 'channels': h['channels'], 'code_bits': h['bits'],
        'block_frames': h['block_frames'], 'frames': h['frame_count'],
        'duration_seconds': h['frame_count'] / h['sample_rate'] if h['sample_rate'] else 0,
        'file_size_bytes': os.path.getsize(a.input),
    }
    if a.json:
        print(json.dumps(d, indent=2, ensure_ascii=False))
    else:
        print(f'GHA {d["version"]} / GHAC1')
        print(f'Audio: {d["sample_rate"]} Hz, {d["channels"]} ch, {d["code_bits"]}-bit codes, block={d["block_frames"]}')
        print(f'Duration: {d["duration_seconds"]:.3f}s')
        print(f'File size: {d["file_size_bytes"]/(1024*1024):.3f} MiB')


if __name__ == '__main__':
    main()
