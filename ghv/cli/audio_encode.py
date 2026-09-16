#!/usr/bin/env python3
from __future__ import annotations
import argparse, json, os, shutil, subprocess, time
from pathlib import Path
from ghv.gha import encode_pcm16le, parse_header
from ghv.version import version_summary


def find_tool(name, explicit=None):
    if explicit:
        return explicit
    p = shutil.which(name)
    if p:
        return p
    if os.name == 'nt':
        for g in [rf'E:\\ffmpeg\\bin\\{name}.exe', rf'C:\\ffmpeg\\bin\\{name}.exe']:
            if os.path.exists(g):
                return g
    raise SystemExit(f'{name} not found. Put FFmpeg in PATH or pass --{name}.')


def probe(path, ffprobe):
    data = json.loads(subprocess.check_output([ffprobe, '-v', 'error', '-print_format', 'json', '-show_streams', path], text=True, encoding='utf-8', errors='replace'))
    aud = [s for s in data.get('streams', []) if s.get('codec_type') == 'audio']
    if not aud:
        raise SystemExit('No audio stream found')
    return aud[0]


def main():
    ap = argparse.ArgumentParser(description='Encode FFmpeg-readable audio/video to GHA 0.2 / GHAC1')
    ap.add_argument('--version', action='version', version=version_summary())
    ap.add_argument('input')
    ap.add_argument('output')
    ap.add_argument('--mode', choices=['hq', 'compact'], default='hq')
    ap.add_argument('--rate', type=int, default=0, help='0 = preserve source rate')
    ap.add_argument('--channels', type=int, choices=[1, 2], default=0, help='0 = preserve mono/stereo, clamp >2 to stereo')
    ap.add_argument('--ffmpeg')
    ap.add_argument('--ffprobe')
    args = ap.parse_args()

    ffmpeg = find_tool('ffmpeg', args.ffmpeg)
    ffprobe = find_tool('ffprobe', args.ffprobe)
    s = probe(args.input, ffprobe)
    sr = args.rate or int(s.get('sample_rate') or 48000)
    ch = args.channels or max(1, min(2, int(s.get('channels') or 2)))
    bits = 8 if args.mode == 'hq' else 6
    quality = 92 if bits == 8 else 74
    cmd = [ffmpeg, '-v', 'error', '-i', args.input, '-map', '0:a:0', '-vn', '-f', 's16le', '-acodec', 'pcm_s16le', '-ac', str(ch), '-ar', str(sr), '-']
    print(f'[GHA] Decode input -> PCM16 {sr} Hz / {ch} ch', flush=True)
    t = time.perf_counter()
    pcm = subprocess.check_output(cmd)
    print(f'[GHA] Encoding GHAC1 {bits}-bit ({args.mode})...', flush=True)
    gha = encode_pcm16le(pcm, sr, ch, bits=bits, block_frames=32, quality=quality)
    Path(args.output).write_bytes(gha)
    dt = max(.001, time.perf_counter() - t)
    h = parse_header(gha)
    ratio = (len(gha) / len(pcm) * 100.0) if pcm else 0
    print(f'[GHA] Done: {args.output}', flush=True)
    print(f'[GHA] PCM {len(pcm)/(1024*1024):.2f} MiB -> GHA {len(gha)/(1024*1024):.2f} MiB ({ratio:.1f}% of PCM)', flush=True)
    print(f'[GHA] frames={h["frame_count"]} elapsed={dt:.2f}s', flush=True)


if __name__ == '__main__':
    main()
