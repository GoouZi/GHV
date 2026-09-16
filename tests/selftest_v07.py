#!/usr/bin/env python3
from __future__ import annotations
import os, struct, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from ghv.container import read_header, read_index, FRAME_FMT, FRAME_SIZE, VFRM


def main():
    src = ROOT / 'tests' / 'sample.mp4'
    core = ROOT / 'native' / 'bin' / ('ghvcore.exe' if os.name == 'nt' else 'ghvcore')
    dec = ROOT / 'native' / 'bin' / ('ghvdecode.exe' if os.name == 'nt' else 'ghvdecode')
    if not src.exists() or not core.exists() or not dec.exists():
        raise SystemExit('GHVC7 self-test requires sample.mp4 and built native tools')
    out = ROOT / 'tests' / '_selftest_v07.ghv'
    repaired = ROOT / 'tests' / '_selftest_v07_repaired.ghv'
    try:
        cmd = [sys.executable, '-m', 'ghv.cli.encode', str(src), str(out),
               '--preset', 'balanced', '--native', 'on', '--codec', '7']
        p = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, encoding='utf-8', errors='replace')
        if p.returncode:
            print(p.stdout); raise RuntimeError('GHVC7 encode failed')
        with open(out, 'rb') as f:
            h=read_header(f); idx=read_index(f,h)
            assert h.major==0 and h.minor==7 and h.frame_count==len(idx)>0
            f.seek(idx[0][0]); rh=f.read(FRAME_SIZE)
            vals=struct.unpack(FRAME_FMT,rh)
            assert vals[0]==VFRM and vals[4]==7 and vals[3]==0
            payload=f.read(vals[7]); assert payload[:4]==b'GTC7'
        print(f'[PASS] GHVC7 encode/container: {h.frame_count} frames')

        p=subprocess.run([str(dec),str(out),'--verify'],stdout=subprocess.DEVNULL,
                         stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
        if p.returncode: raise RuntimeError('GHVC7 native decode/CRC failed: '+p.stderr)
        print('[PASS] GHVC7 native decode + CRC')

        p=subprocess.run([sys.executable,'-m','ghv.cli.repair',str(out),str(repaired)],
                         cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                         text=True,encoding='utf-8',errors='replace')
        if p.returncode: print(p.stdout);raise RuntimeError('GHVC7 repair failed')
        with open(repaired,'rb') as f:
            rh=read_header(f);ridx=read_index(f,rh);assert rh.minor==7 and len(ridx)==rh.frame_count
        print('[PASS] GHVC7 index repair')
        print('[PASS] GHV 0.7 / GHVC7 self-test complete')
    finally:
        for path in (out,repaired):
            try:path.unlink()
            except OSError:pass


if __name__=='__main__':
    main()
