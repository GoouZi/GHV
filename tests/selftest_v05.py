#!/usr/bin/env python3
from __future__ import annotations
import binascii, math, shutil, struct, subprocess, sys
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from ghv.container import read_header, read_index, FRAME_FMT, FRAME_SIZE, VFRM, unpack_motion
from ghv.codec4 import decode_frame
from ghv.gha import parse_header as parse_gha, decode_pcm16le


def main():
    ghv_path = ROOT / 'tests' / 'sample_v05.ghv'
    gha_path = ROOT / 'tests' / 'sample_v02.gha'
    src_path = ROOT / 'tests' / 'sample.mp4'
    assert ghv_path.exists() and gha_path.exists() and src_path.exists()

    decoded: list[bytes] = []
    with open(ghv_path, 'rb') as f:
        h = read_header(f); idx = read_index(f, h); prev = None
        assert h.major == 0 and h.minor == 5 and h.frame_count == len(idx)
        for i, (off, typ_index) in enumerate(idx):
            f.seek(off)
            rh = f.read(FRAME_SIZE)
            magic, no, pts, typ, codec, meta, raw_size, packed_size, checksum = struct.unpack(FRAME_FMT, rh)
            assert magic == VFRM and no == i and typ == typ_index and codec == 4
            payload = f.read(packed_size)
            dx, dy = unpack_motion(meta)
            yuv = decode_frame(typ, payload, prev, h.width, h.height, raw_size, dx, dy)
            assert (binascii.crc32(yuv) & 0xFFFFFFFF) == checksum
            decoded.append(yuv); prev = yuv
    print(f'[PASS] GHV 0.5 / GHVC4 decode + CRC: {len(decoded)} frames')

    gha = gha_path.read_bytes(); ah = parse_gha(gha)
    pcm, sr, ch, frames = decode_pcm16le(gha)
    assert sr == ah['sample_rate'] and ch == ah['channels'] and frames == ah['frame_count']
    assert len(pcm) == frames * ch * 2
    print(f'[PASS] GHA 0.2 / GHAC1 decode: {frames} frames @ {sr} Hz / {ch} ch')

    ffmpeg = shutil.which('ffmpeg')
    if ffmpeg:
        raw = subprocess.check_output([ffmpeg, '-v', 'error', '-i', str(src_path), '-map', '0:v:0', '-an', '-f', 'rawvideo', '-pix_fmt', 'yuv420p', '-'])
        fs = h.width * h.height * 3 // 2
        n = min(len(decoded), len(raw) // fs)
        sse = 0.0; count = 0
        for i in range(n):
            a = np.frombuffer(raw[i*fs:(i+1)*fs], dtype=np.uint8).astype(np.float64)
            b = np.frombuffer(decoded[i], dtype=np.uint8).astype(np.float64)
            sse += float(np.sum((a-b)**2)); count += len(a)
        mse = sse / max(count, 1)
        psnr = float('inf') if mse == 0 else 10.0 * math.log10((255.0*255.0)/mse)
        print(f'[PASS] Sample YUV PSNR: {psnr:.3f} dB')
    else:
        print('[SKIP] ffmpeg not found; PSNR comparison skipped')

    native = ROOT / 'native' / 'bin' / ('ghvdecode.exe' if sys.platform.startswith('win') else 'ghvdecode')
    if native.exists():
        p = subprocess.run([str(native), str(ghv_path), '--verify'], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        if p.returncode != 0:
            raise RuntimeError('native decoder verification failed: ' + p.stderr)
        print('[PASS] Native GHVC4 decoder verification')
    else:
        print('[SKIP] native decoder not built')

    print('[PASS] GHV 0.5 / GHA 0.2 self-test complete')


if __name__ == '__main__':
    main()
