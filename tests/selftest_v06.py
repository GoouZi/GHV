#!/usr/bin/env python3
from __future__ import annotations
import binascii, os, shutil, struct, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from ghv.container import read_header, read_index, FRAME_FMT, FRAME_SIZE, VFRM
from ghv.codec6 import decode_frame as decode6
from ghv.gha import parse_header as parse_gha, decode_pcm16le


def main():
    src = ROOT / 'tests' / 'sample.mp4'
    gha_path = ROOT / 'tests' / 'sample_v02.gha'
    native_core = ROOT / 'native' / 'bin' / ('ghvcore.exe' if os.name == 'nt' else 'ghvcore')
    native_dec = ROOT / 'native' / 'bin' / ('ghvdecode.exe' if os.name == 'nt' else 'ghvdecode')
    if not src.exists() or not gha_path.exists():
        raise SystemExit('sample fixtures missing')

    out = ROOT / 'tests' / '_selftest_v06.ghv'
    if native_core.exists():
        cmd = [sys.executable, str(ROOT / 'ghvenc.py'), str(src), str(out),
               '--preset', 'balanced', '--native', 'on', '--codec', '6']
        p = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, encoding='utf-8', errors='replace')
        if p.returncode:
            print(p.stdout)
            raise RuntimeError('GHV 0.6 native direct encode self-test failed')
        print('[PASS] Native direct .ghv encode path')
    else:
        fixture = ROOT / 'tests' / 'sample_v06.ghv'
        if not fixture.exists():
            raise SystemExit('native core not built and sample_v06.ghv fixture missing')
        shutil.copy2(fixture, out)
        print('[SKIP] Native direct encoder not built; using packaged sample_v06.ghv')

    decoded = []
    with open(out, 'rb') as f:
        h = read_header(f); idx = read_index(f, h); prev = None
        assert h.major == 0 and h.minor == 6 and h.frame_count == len(idx)
        for i, (off, ityp) in enumerate(idx):
            f.seek(off)
            rh = f.read(FRAME_SIZE)
            magic, no, pts, typ, codec, meta, raw_size, packed_size, checksum = struct.unpack(FRAME_FMT, rh)
            assert magic == VFRM and no == i and typ == ityp and codec == 6
            payload = f.read(packed_size)
            yuv = decode6(typ, payload, prev, h.width, h.height, raw_size)
            assert (binascii.crc32(yuv) & 0xffffffff) == checksum
            decoded.append(yuv); prev = yuv
    print(f'[PASS] Python GHVC6 decode + CRC: {len(decoded)} frames')

    if native_dec.exists():
        p = subprocess.run([str(native_dec), str(out), '--verify'], stdout=subprocess.DEVNULL,
                           stderr=subprocess.PIPE, text=True, encoding='utf-8', errors='replace')
        if p.returncode:
            raise RuntimeError('native GHVC6 verification failed: ' + p.stderr)
        print('[PASS] Native GHVC6 decode + CRC')
    else:
        print('[SKIP] Native decoder not built')

    gha = gha_path.read_bytes(); ah = parse_gha(gha)
    pcm, sr, ch, frames = decode_pcm16le(gha)
    assert sr == ah['sample_rate'] and ch == ah['channels'] and frames == ah['frame_count']
    assert len(pcm) == frames * ch * 2
    print(f'[PASS] GHA 0.2 / GHAC1 decode: {frames} frames @ {sr} Hz / {ch} ch')

    repaired = ROOT / 'tests' / '_selftest_v06_repaired.ghv'
    p = subprocess.run([sys.executable, str(ROOT / 'ghvrepair.py'), str(out), str(repaired)],
                       cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, encoding='utf-8', errors='replace')
    if p.returncode:
        print(p.stdout)
        raise RuntimeError('repair self-test failed')
    with open(repaired, 'rb') as f:
        rh = read_header(f); ridx = read_index(f, rh)
        assert len(ridx) == rh.frame_count == len(decoded)
    print('[PASS] GHV index repair path')

    for q in (out, repaired):
        try: q.unlink()
        except OSError: pass
    print('[PASS] GHV 0.6 / GHA 0.2 self-test complete')


if __name__ == '__main__':
    main()
