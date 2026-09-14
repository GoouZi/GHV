#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, os, struct
from ghv.container import read_header, read_index, FRAME_FMT, FRAME_SIZE, VFRM


def collect(path: str):
    with open(path, 'rb') as f:
        h = read_header(f)
        idx = read_index(f, h)
        codec_id = 0
        if idx:
            f.seek(idx[0][0]); raw = f.read(FRAME_SIZE)
            if len(raw) == FRAME_SIZE:
                vals = struct.unpack(FRAME_FMT, raw)
                if vals[0] == VFRM: codec_id = vals[4]
    fps = h.fps_num / h.fps_den
    n_i = sum(1 for _, t in idx if t == 0)
    n_p = sum(1 for _, t in idx if t == 1)
    n_r = sum(1 for _, t in idx if t == 2)
    return {
        'format': 'GHV', 'version': f'{h.major}.{h.minor}', 'video_codec': f'GHVC{codec_id}' if codec_id else 'unknown',
        'width': h.width, 'height': h.height, 'fps': fps, 'frames': h.frame_count,
        'i_frames': n_i, 'p_frames': n_p, 'repeat_frames': n_r, 'quality': h.quality,
        'audio_codec': 'GHAC1' if h.audio_codec == 4 else 'none',
        'audio_rate': h.audio_rate, 'audio_channels': h.audio_channels,
        'audio_samples': h.audio_samples, 'duration_seconds': h.duration_us / 1e6,
        'file_size_bytes': os.path.getsize(path), 'frames_offset': h.frames_offset,
        'audio_offset': h.audio_offset, 'index_offset': h.index_offset,
    }


def main():
    ap = argparse.ArgumentParser(description='Inspect GHV files')
    ap.add_argument('input')
    ap.add_argument('--json', action='store_true')
    a = ap.parse_args()
    d = collect(a.input)
    if a.json:
        print(json.dumps(d, indent=2, ensure_ascii=False))
        return
    print(f'GHV {d["version"]} / {d["video_codec"]}')
    print(f'Video: {d["width"]}x{d["height"]} @ {d["fps"]:.6f} fps, frames={d["frames"]}, '
          f'I={d["i_frames"]}, P={d["p_frames"]}, repeat={d["repeat_frames"]}, quality={d["quality"]}')
    print(f'Audio: {d["audio_rate"]} Hz, {d["audio_channels"]} ch, codec={d["audio_codec"]}, samples={d["audio_samples"]}')
    print(f'Duration: {d["duration_seconds"]:.3f}s')
    print(f'Offsets: frames={d["frames_offset"]}, audio={d["audio_offset"]}, index={d["index_offset"]}')
    print(f'File size: {d["file_size_bytes"]/(1024*1024):.3f} MiB')


if __name__ == '__main__':
    main()
