#!/usr/bin/env python3
from __future__ import annotations
import argparse, os, shutil, subprocess, tempfile, wave
from pathlib import Path
from ghv.gha import decode_pcm16le, parse_header


def find_ffplay(explicit=None):
    if explicit:
        return explicit
    p = shutil.which('ffplay')
    if p:
        return p
    if os.name == 'nt':
        for g in [r'E:\ffmpeg\bin\ffplay.exe', r'C:\ffmpeg\bin\ffplay.exe']:
            if os.path.exists(g):
                return g
    return None


def main():
    ap = argparse.ArgumentParser(description='GHA 0.2 / GHAC1 reference player')
    ap.add_argument('input')
    ap.add_argument('--info', action='store_true')
    ap.add_argument('--ffplay')
    args = ap.parse_args()
    data = Path(args.input).read_bytes()
    h = parse_header(data)
    dur = h['frame_count'] / h['sample_rate'] if h['sample_rate'] else 0
    print(f'GHA {h["major"]}.{h["minor"]} / GHAC1 | {h["sample_rate"]} Hz | {h["channels"]} ch | {h["bits"]}-bit codes | {dur:.3f}s')
    if args.info:
        return
    ffplay = find_ffplay(args.ffplay)
    if not ffplay:
        raise SystemExit('ffplay not found')
    pcm, sr, ch, frames = decode_pcm16le(data)
    tmp = tempfile.NamedTemporaryFile(prefix='gha_', suffix='.wav', delete=False)
    p = tmp.name
    tmp.close()
    try:
        with wave.open(p, 'wb') as w:
            w.setnchannels(ch)
            w.setsampwidth(2)
            w.setframerate(sr)
            w.writeframes(pcm)
        subprocess.call([ffplay, '-autoexit', '-loglevel', 'quiet', p])
    finally:
        try:
            os.unlink(p)
        except OSError:
            pass


if __name__ == '__main__':
    main()
